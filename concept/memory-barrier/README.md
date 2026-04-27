# memory-barrier

메모리 배리어는 재정렬을 막는 명시적 장치다. memory-model이 "무엇을 보장해야 하는가"를 정의한다면, memory-barrier는 "어떻게 그 보장을 강제하는가"를 다룬다.

배리어는 두 종류가 있다:

- **컴파일러 배리어**: 컴파일러의 재정렬을 막음
- **CPU 배리어**: 하드웨어의 재정렬을 막음

---

## 1. 컴파일러 배리어

컴파일러는 관찰 가능한 동작(observable behavior)이 바뀌지 않는 한 명령어를 자유롭게 재정렬한다. 싱글스레드 관점에서는 안전하지만, 멀티스레드에서는 문제가 된다.

### GCC 컴파일러 배리어

```cpp
asm volatile("" ::: "memory");
```

- `asm volatile`: 컴파일러가 이 명령어를 제거하거나 이동하지 못하게 함
- `"memory"` clobber: "이 지점에서 메모리가 바뀔 수 있다" — 컴파일러가 이 경계를 넘어 메모리 접근을 재정렬하지 않음

**컴파일러 배리어만으로는 CPU 재정렬을 막지 못한다.** 생성된 어셈블리 명령어 순서는 보장되지만, CPU가 실행할 때 다시 재정렬할 수 있다.

### 언제 쓰는가

```cpp
// 시그널 핸들러와 메인 스레드 사이 (같은 코어)
volatile bool flag = false;
int data = 0;

// 메인 스레드
data = 42;
asm volatile("" ::: "memory");  // 컴파일러 배리어
flag = true;

// 시그널 핸들러 (같은 코어에서 실행)
if (flag) {
    assert(data == 42);  // 컴파일러 재정렬은 막혔으므로 안전
}
```

시그널 핸들러는 항상 같은 코어에서 실행되므로 CPU 재정렬은 문제가 안 된다 — 컴파일러 배리어만으로 충분한 드문 경우.

---

## 2. CPU 배리어 (x86)

x86은 TSO(Total Store Order)라 대부분의 재정렬이 하드웨어 수준에서 이미 금지되어 있다. 허용되는 재정렬은 **store→load** 하나뿐이다.

### x86 배리어 명령어

| 명령어   | 의미                                                 |
| :------- | :--------------------------------------------------- |
| `mfence` | 모든 load/store가 이 경계를 넘지 못함 (full barrier) |
| `sfence` | 모든 store가 이 경계를 넘지 못함 (store barrier)     |
| `lfence` | 모든 load가 이 경계를 넘지 못함 (load barrier)       |

x86에서 `sfence`와 `lfence`는 일반 메모리에서는 거의 필요 없다. `mfence`는 seq_cst store를 구현할 때 사용된다.

### store→load 재정렬 예시

```
코어 A          코어 B
x = 1           y = 1
r1 = y          r2 = x
```

x86 TSO에서 `r1=0, r2=0`이 가능하다 — store가 store buffer에 있는 동안 load가 먼저 실행될 수 있어서. `mfence`로 막을 수 있다:

```
코어 A          코어 B
x = 1           y = 1
mfence          mfence
r1 = y          r2 = x
```

---

## 3. atomic_signal_fence vs atomic_thread_fence

C++11은 두 종류의 fence를 제공한다.

### atomic_signal_fence

```cpp
std::atomic_signal_fence(std::memory_order_release);
```

**컴파일러 배리어만** 삽입한다. CPU 배리어 명령어를 생성하지 않는다.

용도: 시그널 핸들러와 메인 스레드 사이 (같은 코어, CPU 재정렬 불필요).

x86에서 `asm volatile("" ::: "memory")`와 동일한 효과.

### atomic_thread_fence

```cpp
std::atomic_thread_fence(std::memory_order_release);
```

**컴파일러 배리어 + CPU 배리어**를 삽입한다. 다른 코어와의 동기화에 사용.

x86에서 `memory_order_release` fence는 컴파일러 배리어만 (TSO가 이미 보장). `memory_order_seq_cst` fence는 `mfence` 생성.

---

## 4. release/acquire와 fence의 관계

`memory_order_release`/`acquire`는 특정 atomic 연산에 묵시적으로 붙는 배리어다. `atomic_thread_fence`는 변수와 무관하게 명시적으로 배리어를 세운다.

### 묵시적 (release/acquire store/load)

```cpp
// release store: data 쓰기가 store 위로 올라올 수 없음
data = 42;
flag.store(true, memory_order_release);

// acquire load: data 읽기가 load 아래로 내려올 수 없음
while (!flag.load(memory_order_acquire)) {}
use(data);
```

### 명시적 (fence)

```cpp
// fence release: 이전 모든 store가 fence 아래로 내려갈 수 없음
data = 42;
atomic_thread_fence(memory_order_release);
flag.store(true, memory_order_relaxed);  // store 자체는 relaxed

// fence acquire: 이후 모든 load가 fence 위로 올라갈 수 없음
while (!flag.load(memory_order_relaxed)) {}
atomic_thread_fence(memory_order_acquire);
use(data);
```

fence는 여러 변수에 걸친 배리어가 필요할 때 유용하다. release store는 그 store 하나에만 적용되지만, fence는 그 지점의 모든 store에 적용된다.

---

## 5. x86에서 acquire/release 비용

x86 TSO는 이미 load→load, load→store, store→store 재정렬을 하드웨어가 금지한다. 따라서:

- **acquire load** → 단순 `MOV` (추가 명령어 없음)
- **release store** → 단순 `MOV` (추가 명령어 없음)
- **seq_cst store** → `MOV` + `MFENCE` 또는 `XCHG`

acquire/release가 x86에서 "공짜에 가깝다"는 말이 여기서 나온다. 컴파일러 재정렬만 막으면 되고, CPU 배리어 명령어가 추가되지 않는다.

ARM/POWER에서는 acquire/release도 실제 배리어 명령어(`dmb`, `isb` 등)를 생성한다.

---

## 6. 요약

```
컴파일러 배리어: asm volatile("" ::: "memory")
  → 컴파일러 재정렬만 차단. CPU 재정렬은 그대로.

atomic_signal_fence: 컴파일러 배리어만
  → 시그널 핸들러용. CPU 명령어 없음.

atomic_thread_fence: 컴파일러 + CPU 배리어
  → 스레드 간 동기화. memory_order에 따라 CPU 명령어 생성.

x86 TSO: store→load 재정렬만 허용
  → acquire/release = MOV (추가 비용 없음)
  → seq_cst store = MFENCE 필요
```

실전에서는 `atomic_thread_fence`를 직접 쓸 일이 드물다. `memory_order_release`/`acquire`로 대부분 해결된다. fence가 필요한 경우는 여러 relaxed 연산을 한꺼번에 묶어서 순서를 보장할 때다.
