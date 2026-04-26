# SPSC Queue v05

## v04 대비 변화

**`alignas(64)` 추가 + cached index와 atomic index를 같은 캐시라인에 공동 배치.**

```cpp
// v04
std::unique_ptr<T[]> buf_;
std::atomic<std::size_t> head_{0};
std::size_t tail_cached_{0};
std::atomic<std::size_t> tail_{0};
std::size_t head_cached_{0};

// v05
std::unique_ptr<T[]> buf_;
alignas(64) std::atomic<std::size_t> head_{0};
std::size_t tail_cached_{0};   // ← head_와 같은 캐시라인
alignas(64) std::atomic<std::size_t> tail_{0};
std::size_t head_cached_{0};   // ← tail_과 같은 캐시라인
```

## 핵심 설계

### 캐시라인 소유권 분리

```
캐시라인 1 (consumer 전용):
    head_        ← consumer가 씀 (head_.store)
    tail_cached_ ← consumer가 읽음 (pop의 empty 체크)

캐시라인 2 (producer 전용):
    tail_        ← producer가 씀 (tail_.store)
    head_cached_ ← producer가 읽음 (push의 full 체크)
```

각 스레드가 자기 캐시라인만 접근하는 게 목표다. 경계 조건(full/empty)에서만 상대방 캐시라인을 읽어야 하고, 그 빈도가 workload에 따라 달라진다.

### v03(alignas만)과의 차이

v03는 `head_`와 `tail_`을 별도 캐시라인에 분리했지만, cached index가 없어서 매 push/pop마다 반대쪽 인덱스를 읽는다. 상대방 캐시라인으로의 fetch가 매번 발생한다.

v05는 cached index 덕분에 경계 조건에서만 상대방 캐시라인을 읽는다. 25-75% workload에서 그 빈도가 크게 줄어든다.

### v04(cached만)와의 차이

v04는 `head_`와 `tail_`이 같은 캐시라인에 있어서, consumer가 `head_`를 쓰면 producer 캐시라인 전체가 무효화된다. cached index로 load 횟수를 줄여도 쓰기로 인한 무효화는 막을 수 없다.

v05는 쓰기가 각자의 캐시라인에서만 일어난다. consumer의 `head_.store`가 producer 캐시라인을 무효화하지 않는다.

## API

v04와 동일. 시그니처 변화 없음.

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2,4`

통합 벤치: [../../bench/spscq_bench.cpp](../../bench/spscq_bench.cpp)  
스크립트: [../../script/run_spscq_bench.sh](../../script/run_spscq_bench.sh)

벤치 설계: producer가 75% 도달 시 대기, 25%까지 소진되면 재개. `alignas(64) Msg` (64바이트, 슬롯 하나 = 캐시라인 하나).

### 실측 (burst 25-75%)

|     N | v02 (기준) | v03 (align) | v04 (cache) |    v05 (둘 다) |
| ----: | ---------: | ----------: | ----------: | -------------: |
|  1024 |    ~68 ns  |     ~55 ns  |     ~72 ns  | **~45.7 ns**   |
|  4096 |    ~67 ns  |     ~55 ns  |     ~72 ns  | **~45.5 ns**   |
| 16384 |    ~68 ns  |     ~55 ns  |     ~72 ns  | **~45.4 ns**   |

CV 0.04% — 전 버전 통틀어 가장 안정적.

### 원인 분해

v02 대비 -34% 향상은 두 효과의 시너지:

1. **alignas(64)**: head_/tail_ 쓰기가 상대방 캐시라인을 무효화하지 않음 (v03에서 확인, ~19% 향상)
2. **cached index + 공동 배치**: 경계 조건에서만 상대방 캐시라인 fetch 발생. 정상 경로에서는 자기 캐시라인만 접근

v04가 v02보다 느렸던 이유도 확인됨 — alignas 없이 cached index만 쓰면 `head_`/`tail_` 쓰기 무효화가 그대로 남고 비교 연산만 추가되어 역효과.

### 관찰

- **N 무관**: 슬롯 하나가 캐시라인 하나이고 활성 영역(head~tail)이 항상 L1 안에 있어서 N이 달라도 차이 없음
- **최적화 조합의 중요성**: alignas와 cached index는 각각 단독으로 효과가 제한적이거나 역효과. 함께 적용해야 시너지가 남
- **캐시라인 소유권 설계**: 각 스레드가 자기 캐시라인만 쓰도록 멤버를 배치하는 것이 SPSC 최적화의 핵심
