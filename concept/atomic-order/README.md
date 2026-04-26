# atomic memory_order (note)

memory_order별 실제 비용 측정. **x86에서 memory_order가 언제 비용이 있고 없는지** 직접 실측.

## 핵심 개념

### memory_order란

CPU와 컴파일러는 성능을 위해 메모리 연산 순서를 바꿈. 싱글 스레드에선 결과가 같으니 상관없지만, 멀티 스레드에선 문제가 생김. `memory_order`는 **"이 atomic 연산 주변에서 다른 메모리 연산이 얼마나 자유롭게 재배치될 수 있는가"** 를 지정.

### relaxed

원자성만 보장. 컴파일러/CPU 재배치 제한 없음.

```cpp
counter.fetch_add(1, std::memory_order_relaxed);  // 순서 보장 없음
```

용도: 통계 카운터처럼 다른 변수와의 순서가 무관한 경우.

### acquire / release

짝을 이뤄 동기화. `release` store는 이전 쓰기가 완료됨을 보장하고, 이를 `acquire` load로 관측한 스레드에게 해당 쓰기가 보임.

```cpp
// 스레드 A
data = 42;
ready.store(true, std::memory_order_release);  // data 쓰기가 release 전에 완료

// 스레드 B
if (ready.load(std::memory_order_acquire)) {   // release와 동기화
    assert(data == 42);                         // 보장됨
}
```

x86에서 `release` store = 그냥 `MOV`. x86 TSO가 store 순서를 원래 보장하기 때문. 컴파일러 재배치만 차단.

### seq_cst

전체 스레드가 동의하는 하나의 전역 순서. 모든 스레드가 atomic 연산을 같은 순서로 관측.

```cpp
// acq_rel: 스레드 3이 "x 먼저", 스레드 4가 "y 먼저" 봐도 허용
// seq_cst: 불가능. 모든 스레드가 동일한 순서에 동의해야 함
```

x86에서 `seq_cst` store = `MOV + MFENCE`. MFENCE가 store buffer를 완전히 drain.

### x86 fetch_add의 특수성

`fetch_add`는 memory_order 관계없이 항상 `LOCK XADD` 한 가지로 컴파일됨. LOCK prefix가 이미 full memory ordering을 제공하므로 relaxed/acq_rel/seq_cst 추가 비용 없음.

## 측정

환경: i7-9750H, gcc 13.3, `-O3`, 3.0 GHz 고정, `taskset -c 2` (단일), 코어 2+4 (멀티). 100M iter × 7 reps.

### 수치

|   # | 시나리오                     |       per-op | 비고                        |
| --: | :--------------------------- | -----------: | :-------------------------- |
| [1] | fetch_add relaxed, single    |      6.04 ns | baseline                    |
| [2] | fetch_add acq_rel, single    |      6.04 ns | [1]과 동일                  |
| [3] | fetch_add seq_cst, single    |      6.04 ns | [1]과 동일                  |
| [6] | store+load relaxed, single   |      0.34 ns | baseline                    |
| [4] | store(rel)+load(acq), single |      0.67 ns | ~1 cycle 차이 (노이즈 수준) |
| [5] | store+load seq_cst, single   | **10.06 ns** | **MFENCE 비용**             |
| [7] | fetch_add relaxed, multi     |     38.51 ns | contention baseline         |
| [8] | fetch_add seq_cst, multi     |     39.07 ns | [7]과 사실상 동일           |

CV 모두 0.5% 이하.

## 발견

### 1. fetch_add는 memory_order가 비용에 영향 없음 (x86)

[1][2][3] 모두 6.04 ns로 동일. x86에서 `fetch_add`는 memory_order 관계없이 `LOCK XADD`로 컴파일됨. LOCK prefix가 이미 full memory ordering 제공 — relaxed든 seq_cst든 추가할 게 없음.

→ **x86에서 fetch_add에 relaxed를 써도 성능 이득 없음**. 가독성/이식성을 위해 의도에 맞는 memory_order 사용 권장.

### 2. seq_cst store는 MFENCE로 30× 비쌈

[5] seq_cst store+load = 10.06 ns vs [4] release+acquire = 0.67 ns. 차이의 원인:

```
release store: MOV          (~1 cycle)
seq_cst store: MOV + MFENCE (~30 cycle)
```

MFENCE는 store buffer를 완전히 drain할 때까지 파이프라인을 멈춤. **seq_cst가 필요한 경우는 드물다** — 여러 atomic 변수 간 전역 순서가 필요할 때만. 대부분의 producer-consumer 패턴은 `acq_rel`로 충분.

### 3. 멀티스레드 contention이 memory_order 차이를 압도

[7] relaxed = 38.51 ns, [8] seq_cst = 39.07 ns — 사실상 동일. fetch_add는 멀티스레드에서도 `LOCK XADD` 한 가지. contention 비용(~38 ns)이 memory_order 차이를 완전히 가림.

cache-alignment 측정의 [4] shared atomic(44.7 ns)과 비교하면: 코어 고정 없이 측정한 것과 일치하는 수준.

### 4. release+acquire는 x86에서 공짜에 가까움

[4] 0.67 ns ≈ [6] 0.34 ns × 2. 어셈블리 확인 결과 둘 다 `MOV + MOV`. 차이는 루프 오버헤드 / branch prediction 수준 — **의미 있는 memory_order 비용이 아님**.

x86 TSO(Total Store Order)가 release/acquire 의미를 하드웨어 수준에서 기본 제공하기 때문. ARM 같은 weak ordering CPU에서는 실제 barrier 명령어가 추가됨.

## 함정

- **x86 결과를 ARM에 적용하지 말 것**: ARM은 weak ordering — relaxed/acq_rel/seq_cst 비용 차이가 실재함.
- **fetch_add relaxed = seq_cst (x86)**: 이식성을 위해 의도에 맞는 memory_order 명시 권장.
- **seq_cst가 필요한 경우**: 세 개 이상 스레드가 두 개 이상 atomic 변수를 관측하고 순서 일관성이 필요할 때. 단순 producer-consumer는 해당 없음.

## 참고

- 측정 코드: `concept/atomic-order/bench/atomic_order_bench.cpp`
- 실행: `script/run_atomic_order_bench.sh`
- Intel SDM Vol. 3 §8.2 — Memory Ordering
- Herb Sutter, "atomic Weapons" (CppCon 2012) — memory_order 실용 가이드
