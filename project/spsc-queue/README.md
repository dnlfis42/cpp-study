# spsc-queue

단일 생산자 단일 소비자 락-프리 큐. 스레드 간 메시지 전달 저지연 구현.

- **SPSC**: producer/consumer 각각 1스레드만 — head/tail 변수에 실제 경합 없음
- **ring buffer**: 고정 크기 배열 + wrap-around 인덱스, 1슬롯 낭비로 full/empty 구분
- **template\<T, N\>**: 컴파일 타임 용량, 2의 거듭제곱 강제 (`& (N-1)` bitmask)
- **C++20**

**학습 목표**:

- mutex → atomic 전환으로 lock-free 동기화 체감
- MESI 프로토콜, false sharing, 캐시라인 소유권 설계 실측
- 벤치 설계가 결과를 완전히 뒤바꾸는 사례 직접 경험

## 버전 히스토리

> **주의**: v01~v04는 "always-full" 벤치(producer 스핀, consumer 1회 pop) 기준.  
> v05부터 "burst 25-75%" 벤치(균형 workload) 기준. 직접 비교 불가.

|        버전 | 주요 변화                               | 벤치 조건    | 결과                                            |
| ----------: | :-------------------------------------- | :----------- | :---------------------------------------------- |
| [v01](v01/) | `std::mutex` 기반 — 기준선              | always-full  | ~220 ns/pop (N 무관, 병목 = futex contention)   |
| [v02](v02/) | `atomic` acquire/release — 뮤텍스 제거  | always-full  | ~81 ns/pop (2.7×↑, 병목 = 데이터 캐시라인 이동) |
| [v03](v03/) | `alignas(64)` — false sharing 제거 시도 | always-full  | ~84 ns/pop (악화. always-full에서 효과 없음)    |
| [v04](v04/) | cached head/tail — cross-core read 감소 | always-full  | ~98~104 ns/pop (악화. 이득 경로 없음)           |
| [v05](v05/) | alignas(64) + cached + 공동 배치        | burst 25-75% | **~45 ns/pop** (v02 동조건 대비 -34%, CV 0.04%) |

## API (공통)

| 카테고리 | 함수                                              |
| :------- | :------------------------------------------------ |
| 상태     | `capacity()`, `size()`, `empty()`, `full()`       |
| 삽입     | `push(const T&) -> bool` — 가득 차면 false        |
| 추출     | `pop() -> std::optional<T>` — 비어 있으면 nullopt |

`size()`, `empty()`, `full()`은 relaxed 읽기 — 두 스레드에서 동시에 호출 시 스냅샷 불일치 가능. 용도에 따라 근사값으로 사용.

## 성능 종합

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2,4`, `T=alignas(64) Msg{byte[64]}`

### burst 25-75% 벤치 (균형 workload)

| 버전 | 구조                             |     N=1024 |     N=4096 |    N=16384 |
| ---: | :------------------------------- | ---------: | ---------: | ---------: |
|  v02 | atomic acquire/release           |     ~68 ns |     ~67 ns |     ~68 ns |
|  v03 | + alignas(64)                    |     ~55 ns |     ~55 ns |     ~55 ns |
|  v04 | + cached index (alignas 없음)    |     ~72 ns |     ~72 ns |     ~72 ns |
|  v05 | alignas(64) + cached + 공동 배치 | **~45 ns** | **~45 ns** | **~45 ns** |

N 전체에서 무관 — 활성 슬롯(head~tail 사이)이 항상 L1 안에 있기 때문.

## 전체 발견

### 1. 뮤텍스가 SPSC에서 얼마나 나쁜가

v01(mutex) ~220 ns vs v02(atomic) ~81 ns — **2.7× 차이**. 원인은 `futex(FUTEX_WAIT/WAKE)` syscall 왕복. 논리적으로 경합이 없는 SPSC에서 락을 걸면 커널 진입 + 컨텍스트 스위치 비용을 매 pop마다 지불한다.

### 2. 벤치 설계가 결론을 뒤집는다

v03(alignas)는 always-full 벤치에서 악화(-3 ns), burst 벤치에서 개선(-13 ns)으로 **방향이 반대**. v04(cached)도 마찬가지. always-full은 현실 workload를 대표하지 않는다 — 큐가 항상 꽉 찼다면 이미 시스템 설계 문제다.

**교훈**: 최적화 효과를 측정하기 전에 workload가 현실적인지 먼저 확인해야 한다.

### 3. false sharing: 쓰기만 무효화 유발

MESI에서 읽기(load)는 Shared 상태로 여러 코어가 동시에 가질 수 있어 무효화가 없다. **쓰기(store)만** 다른 코어의 복사본을 Invalid로 만든다. 따라서 false sharing 분석은 "어느 스레드가 어느 변수에 쓰는가"에 집중해야 한다.

### 4. alignas 단독은 always-full에서 효과 없다

always-full 상태에서 producer는 매 push마다 full 조건(`next == head_cached_` 또는 직접 head* 읽기)에 걸려 상대방 캐시라인을 읽는다. head*/tail\_ 분리와 무관하게 캐시라인 이동이 매번 발생한다.

균형 workload에서는 push가 대부분 성공(full 조건 미도달)하여 head\_ 읽기 빈도가 낮아지고, 그때 비로소 false sharing 제거 효과가 나타난다.

### 5. cached index는 alignas 없이 역효과

v04(cached only)가 v02보다 느린 이유: `head_`와 `tail_`이 같은 캐시라인에 있으면 consumer가 `head_`를 쓰는 순간 producer의 캐시라인 전체가 무효화된다. cached index로 load 횟수를 줄여도 write로 인한 무효화는 막을 수 없다. 여기에 추가 비교 연산 비용까지 붙어 역효과.

### 6. 캐시라인 소유권 설계 — v05의 핵심

```
캐시라인 1 (consumer 전용): head_ + tail_cached_
캐시라인 2 (producer 전용): tail_ + head_cached_
```

각 스레드의 atomic index와 그 cached copy를 같은 캐시라인에 배치. 정상 경로에서 producer는 캐시라인 2만, consumer는 캐시라인 1만 접근한다. 상대방 캐시라인 이동은 경계 조건(full/empty 근접)에서만 발생.

### 7. 데이터 캐시라인 이동은 본질적 비용

producer가 `buf_[tail_]`에 쓰고 consumer가 `buf_[head_]`에서 읽는 한, 데이터 캐시라인은 반드시 코어 간 이동한다. 이건 SPSC의 목적 자체 — 데이터 전달 — 이라 제거 불가능하다. v05의 ~45 ns에서 상당 부분이 이 비용이다.

### 8. MESI 캐시라인 이동은 수십~수백 사이클

invalidation은 1사이클에 전파되지 않는다. write → store buffer → L1 반영 → coherence interconnect로 invalidation 메시지 → 상대방 L1 Invalid 전환 → ack. 이 왕복이 ~수십~수백 사이클이고, producer가 Invalid 캐시라인을 읽으려 할 때 stall이 발생한다. 이게 캐시라인 ping-pong 비용의 정체다.

## 측정 방법론

- **3.0 GHz 고정 + taskset -c 2,4**: 열 스로틀/코어 마이그레이션 차단. 두 코어 고정으로 NUMA 효과 배제
- **10 repetitions + aggregates_only**: mean/stddev/CV 확인. CV > 1%면 재측정
- **T = alignas(64) Msg{byte[64]}**: 슬롯 하나 = 캐시라인 하나. 데이터 false sharing 제거, 큐 메커니즘 순수 측정
- **burst 25-75% workload**: producer가 75% 도달 시 대기, 25%까지 소진 후 재개. 현실적인 fill rate 시뮬레이션

## 미해결 / 다음 단계

- **데이터 캐시라인 비용 정확 측정**: `perf stat`으로 cache-miss 횟수 확인, ~45 ns 중 인덱스 비용 vs 데이터 비용 분리
- **T 크기별 민감도**: T=4(int), T=64(Msg), T=256일 때 throughput 변화 측정
- **relaxed vs acquire/release 실측 비교**: x86에서 동일한 MOV로 컴파일되는지 어셈블리 확인

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test

# 버전별 개별 벤치 (always-full)
./script/run_spscq_v01_bench.sh
./script/run_spscq_v02_bench.sh
./script/run_spscq_v03_bench.sh
./script/run_spscq_v04_bench.sh
./script/run_spscq_v05_bench.sh

# 통합 벤치 (burst 25-75%, v02~v05 비교)
./script/run_spscq_bench.sh
```
