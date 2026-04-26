# cache alignment / false sharing (note)

cache line 단위 충돌 측정. **같은 cache line의 다른 변수도 충돌하는가, 얼마나 충돌하는가**를 직접 실측.

> 본인 가설: "같은 페이지에서 일어나는 작업이 빠르다" — 이 노트는 그 가설의 한 layer 더 아래(같은 cache line) 측정.

## 핵심 개념

### Cache line

CPU 캐시의 **이동 단위는 64 B (보통)**. 메모리 ↔ L1/L2/L3 사이는 64B 단위로만 오감. 변수 1바이트만 읽어도 그 변수가 속한 64B line 전체가 cache로 올라옴.

### MESI cache coherence

multi-core 시스템에서 같은 cache line이 여러 core에 동시에 존재할 수 있음. 일관성 유지를 위해 line 상태를 추적:

- **M**odified: 한 core가 line을 수정, 다른 core는 모름
- **E**xclusive: 한 core만 가지고 있음, 깨끗
- **S**hared: 여러 core가 같은 깨끗한 사본
- **I**nvalid: 무효

write 시 line이 다른 core에 있으면 **invalidation 메시지** → 그 core의 사본 무효화 → 자기가 단독 점유. 이게 "cache line ping-pong"의 메커니즘.

### False sharing

두 스레드가 *서로 다른 변수*를 update하는데 그 둘이 *같은 cache line*에 있으면, MESI는 line 단위로 동작하므로 충돌. 변수가 다른데도 **MESI 입장에선 같은 line의 충돌**.

해결: 변수 사이에 padding을 넣거나 `alignas(64)`로 다른 line으로 분리.

## 측정

환경: i7-9750H (6 physical core × 2 HT = 12 logical), L1 64 KiB, L2 256 KiB, L3 12 MiB. 코어 2와 4 핀(다른 물리 코어, HT sibling 회피). 3.0 GHz 고정.

워크로드: 각 스레드가 100M번 카운터 증가. `asm volatile("" : "+m"(x))`로 컴파일러 fold 차단.

### 수치 (per-op = per-thread ns/op)

| 시나리오                                  |      per-op | 추가 비용                          |
| ----------------------------------------- | ----------: | ---------------------------------- |
| [1] single thread, plain                  |     1.82 ns | (baseline)                         |
| [3] **aligned plain**, 다른 물리 코어     | **1.82 ns** | 0 (perfect parallel)               |
| [7] aligned plain, HT sibling             |     1.92 ns | +0.10 (HT 자체 비용 ~5%)           |
| [2] false sharing plain, 다른 물리 코어   |     2.50 ns | +0.68 (cross-core MESI ping-pong)  |
| [6] **false sharing plain, HT sibling**   | **8.04 ns** | **+6.22 (HT MOMC, amortize 불가)** |
| [5] aligned atomic (LOCK only)            |     6.04 ns | +4.22 (LOCK prefix)                |
| [4] **shared atomic** (LOCK + contention) | **44.7 ns** | +42.88 (LOCK + contention)         |

CV 모두 0.1~3% 이내, 측정 안정.

### 비용 가산 모델

```
[4] = base RMW + LOCK overhead + cross-core contention
    = 1.82  + 4.22  + 38.66
    = 44.7 ns ✓

[6] = base RMW + HT cost + HT false sharing penalty
    = 1.82  + 0.10 + 6.12
    = 8.04 ns ✓
```

각 layer가 **깔끔하게 가산적**. 분리된 비용 정리:

- **base RMW**: 1.82 ns. inc + load/store, cache hot.
- **HT 자체 비용**: +0.10 ns. modern multi-port execution + store buffer가 거의 다 흡수.
- **cross-core MESI ping-pong**: +0.68 ns/op. line transfer를 store buffer가 batch로 amortize.
- **HT MOMC (Memory Order Machine Clear)**: +6.22 ns/op. 매 conflict마다 pipeline flush, batching 불가.
- **LOCK prefix**: +4.22 ns. uncontended cache line이라도 atomic은 비쌈, store buffer batching 불가.
- **cross-core LOCK contention**: +38.66 ns. LOCK이 batching 막아 매 op마다 line acquire 강제.

## 발견

### 1. plain RMW의 false sharing은 1.4×만 — 텍스트북 10×와 다름

predicted 5-10× 인데 실측 1.4×. 이유: **modern Intel의 store buffer가 ping-pong 비용을 amortize**.

- `inc qword ptr [m]` (LOCK 없음): cache line을 단독 점유 안 해도 됨
- 한 코어가 line을 받으면 store buffer에 inc 결과를 batching → 다음 transfer 사이에 N개 inc가 모임
- line transfer 비용(~10 ns)이 N개 op에 분산 → per-op 추가 0.7 ns

→ **"plain false sharing은 modern HW가 상당 부분 가려준다"**. 텍스트북 horror story는 보통 atomic이 함께 있을 때.

### 2. LOCK prefix는 그 자체로 비쌈 (uncontended에서도 3.3×)

[5] aligned atomic = 6.04 ns. 같은 cache line에 다른 코어가 안 닿는데도 plain 대비 3.3× 느림.

이유: `lock` prefix가 store buffer batching을 막음. 매 atomic op가 cache line을 exclusive 상태로 받아 commit해야 다음 op 진행. 4-5 cycle (~1.5 ns) 추가 + memory ordering 비용.

→ **atomic은 contention 없어도 비용이 있다**. lock-free 코드를 atomic으로 도배하면 단일 thread도 느려짐.

### 3. LOCK + contention = 폭발 (24.6×)

[4] shared atomic = 44.7 ns. [5] uncontended atomic 6.04에서 **+38.7 ns**가 contention 순수 비용.

LOCK prefix는 batching을 막으므로 매 op마다 line acquire 강제 → 두 코어가 매 op마다 line ping-pong → cross-core 전송이 amortize 안 됨.

→ **"plain에서는 N op당 1 transfer, atomic에서는 1 op당 1 transfer"**. amortize 가능 여부가 차이의 본질.

### 4. aligned plain은 완벽 병렬 (2-thread = 2× throughput)

[3] 182 ms (200M ops) ≈ [1] 182 ms (100M ops). **per-thread ns/op 동일**. cache line이 분리되면 두 코어가 완전 독립. modern multi-core의 이상적 시나리오.

→ **데이터를 cache line 단위로 분리하면 NUMA가 아닌 한 perfect linear scaling**.

### 5. HT sibling false sharing은 cross-core보다 9× 나쁨 — 직관 정반대

처음 가설: "HT는 L1 공유 → cache line 전송 없음 → false sharing 없음."

**틀림**. 실측:

- [7] aligned HT sibling: 1.92 ns (HT 자체 비용 +0.10 ns만, **거의 공짜**)
- [6] false sharing HT sibling: 8.04 ns (false sharing 페널티 **+6.22 ns**)
- 같은 false sharing이 cross-core면 +0.68 ns. **HT가 9.1× 나쁨**.

원인 — **Memory Order Machine Clear (MOMC)**:

- HT_A가 line의 어떤 위치를 speculatively read → HT_B가 그 line에 write → HT_A의 speculative work invalidate → **pipeline flush** (수백 cycle)
- cross-core MESI는 line 단위 invalidation → store buffer가 batch로 amortize 가능
- HT MOMC는 매 conflict마다 즉시 flush → amortize 불가

부수 발견 — **HT 자체는 거의 공짜 (memory-bound 워크로드)**:

- [7] vs [3]: HT 페널티 5%만
- modern CPU의 multi-port + store buffer가 두 logical thread를 잘 흡수
- "HT면 throughput 절반" 같은 단순 모델은 ALU-bound에서나 맞음

→ **OS scheduler가 데이터 공유하는 두 thread를 HT sibling으로 묶으면 위험**. 같은 worker pool 안의 데이터 경쟁이라면 **다른 물리 코어로 강제 분산**이 정답. taskset/sched_setaffinity로 수동 제어 가능.

### 6. perf로 MOMC 직접 관측 — 가설 숫자로 확정

`perf stat -e machine_clears.memory_ordering`로 각 시나리오의 pipeline flush 횟수 직접 카운트.

per-op MOMC 비율 (100M iter × 2 thread × 7 reps = 1.4G ops):

| 시나리오                          | MOMC 총합 |    per-op | 의미                     |
| --------------------------------- | --------: | --------: | ------------------------ |
| [3] aligned plain, 다른 물리 코어 |       288 |        ~0 | 사실상 0                 |
| [7] aligned plain, HT sibling     |       283 |        ~0 | 사실상 0                 |
| [5] aligned atomic                |       592 |        ~0 | LOCK 자체는 MOMC 안 만듦 |
| [2] false sharing, 다른 물리 코어 |       53M |     0.038 | 26 op당 1번 flush        |
| **[6] false sharing, HT sibling** |  **538M** | **0.384** | **2-3 op당 1번 flush**   |
| [4] shared atomic                 |      683M |     0.488 | 2 op당 1번 flush         |

**핵심**: [6] vs [2] = MOMC 비율 **10.1×**. perf 페널티 비율 (6.22 / 0.68 = 9.1×)과 거의 일치. **MOMC가 페널티의 직접 원인**임을 숫자로 확정.

부수 발견:

- LOCK prefix 자체는 MOMC를 만들지 않음 ([5] = 0). LOCK은 다른 mechanism (cache line ownership)으로 직렬화.
- **하지만 LOCK + contention([4])은 MOMC 0.488** — 두 코어가 같은 line 경쟁하면 LOCK도 speculative read 무효화 유발.
- → **MOMC는 false sharing 전용이 아닌 cross-thread memory ordering 충돌의 일반 신호**.

### 7. 본인 가설 ("같은 페이지 locality") 확장

지금까지 검증한 layer:

- **같은 cache line, 같은 코어 (single thread)**: store buffer + L1 hit (~1.8 ns)
- **같은 cache line, HT sibling, plain**: ~4.4× (MOMC pipeline flush)
- **같은 cache line, 다른 물리 코어, plain**: ~1.4× (MESI amortize)
- **같은 cache line, 다른 물리 코어, atomic**: ~25× (LOCK + contention)
- **같은 page, 다른 cache line**: TLB hit (concept/huge-page 측정)
- **다른 page**: TLB miss (concept/huge-page 측정)

→ "locality" 개념이 **여러 layer로 분해**됨. 각 layer마다 비용 다르고, **같은 layer 안에서도 thread topology와 op 종류가 비용을 다시 갈라놓음**.

## 함정

- **modern HW의 store buffer가 plain false sharing을 가려서 측정이 약하게 나올 수 있음**. 텍스트북 horror을 재현하려면 atomic 이상의 무거운 op 필요.
- `volatile uint64_t`로 fold 차단은 C++20에서 deprecated (`++volatile` 금지). `asm volatile("" : "+m"(x))` 사용.
- **HT sibling을 같은 코어로 잘못 핀하면 false sharing이 거의 안 보임** (L1 공유). `lscpu` + `/proc/cpuinfo`로 물리 코어 확인 필수.
- `std::hardware_destructive_interference_size`는 C++17 표준이지만 컴파일러/stdlib에 따라 미구현. `__cpp_lib_hardware_interference_size`로 가드.

## 다음 측정 카드

- ~~**HT sibling vs 다른 물리 코어 비교**~~ → 측정 완료. 직관 정반대 결과 (발견 #5)
- **cache line 크기 sweep** — 64 B, 128 B padding 효과 비교 (Intel 일부는 prefetcher가 인접 line까지 가져와서 128 B 권장)
- **`memory_order`별 비용** (`relaxed` vs `acq_rel` vs `seq_cst`) — 별도 노트 `concept/atomic-order/`로 (본 측정은 `relaxed`만)
- **다양한 atomic 연산** (`load`/`store`/`exchange`/`CAS`) 의 LOCK 비용 비교
- **3+ thread scaling** — contention이 thread 수에 따라 어떻게 커지는지
- **NUMA cross-node** (가능한 시스템에서) — local L3 vs remote L3 비용
- ~~**MOMC 직접 관측**~~ → 측정 완료. 가설 숫자로 확정 (발견 #6). [6] HT는 cross-core [2]보다 MOMC 10× 더 많음, perf 페널티 9×와 일치.

## 참고

- 측정 코드: `concept/cache-alignment/bench/cache_align_false_sharing_bench.cpp`
- 실행: `script/run_cache_align_false_sharing_bench.sh` (전체) 또는 `bin/cache_align_false_sharing_bench <N>` (시나리오 1~7 단일)
- perf 측정: `perf stat -e machine_clears.memory_ordering bin/cache_align_false_sharing_bench <N>`
- Intel SDM Vol. 3 §8.10 — Locked atomic operations
- Intel SDM Vol. 3 §11.10 — Memory ordering machine clears
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007) — false sharing classic
