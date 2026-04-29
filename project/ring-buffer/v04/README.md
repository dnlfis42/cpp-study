# v04

## 변경 사항

```cpp
// 제거
std::size_t read_pos_{0};   // modular [0, N)
std::size_t write_pos_{0};  // modular [0, N)
std::size_t size_{0};

// 추가
std::uint64_t read_pos_{0};   // monotonic
std::uint64_t write_pos_{0};  // monotonic
// size() = static_cast<std::size_t>(write_pos_ - read_pos_)
```

## 설계 의도

- **시퀀스 카운터**: `read_pos_`/`write_pos_`를 무한 증가 uint64*t로 관리한다. 이동은 순수 덧셈(`+= n`), 인덱싱 시점에만 `& (N-1)` 마스크를 적용한다. uint64_t overflow는 unsigned modular 뺄셈으로 `size() = write_pos* - read*pos*`가 항상 정확하다.
- **`size_` 제거**: Producer는 `write_pos_`만, Consumer는 `read_pos_`만 수정한다. `size_` 공유 변수가 없으므로 `std::atomic` + acquire/release만으로 lock-free SPSC queue로 확장 가능하다.

## 측정

### WriteRead: v03 vs v04

`size_` 제거와 시퀀스 카운터 방식이 단일 스레드 성능에 미치는 영향을 측정한다.

**가설**

`size_` 수정 제거 이득과 `size()` 계산(`write_pos_ - read_pos_`) 비용이 상쇄돼 v03과 동등할 것.

**측정 결과**

| chunk | v03 mean | v04 mean |    CV |
| ----: | -------: | -------: | ----: |
|    64 |  5.92 ns |  6.54 ns | 0.25% |
|   256 |  11.9 ns |  11.9 ns | 0.55% |
|  1024 |  26.7 ns |  26.8 ns | 9.14% |
|  4096 |  97.0 ns |  96.1 ns | 4.37% |
| 16384 |   845 ns |   830 ns | 1.04% |
| 65536 |  3958 ns |  3850 ns | 1.72% |

**분석**

전 구간에서 v03과 동등하다. `size_` 수정 제거 이득과 `size()` 계산 비용이 상쇄됐다.

chunk = 1024에서 CV가 9.14%로 높다. v03과 동일한 패턴으로 glibc memcpy runtime dispatch 경계 구간이다.

**결론**

단일 스레드 성능 회귀 없이 시퀀스 카운터 구조로 전환됐다.

### ZeroCopy: v03 vs v04

**측정 결과**

| chunk | v03 mean | v04 mean |    CV |
| ----: | -------: | -------: | ----: |
|    64 |  1.34 ns |  1.35 ns | 0.04% |
|   256 |  6.23 ns |  6.18 ns | 0.02% |
|  1024 |  27.7 ns |  28.0 ns | 7.86% |
|  4096 |  95.1 ns |  95.4 ns | 6.76% |
| 16384 |   417 ns |   414 ns | 0.47% |
| 65536 |  2078 ns |  2014 ns | 0.39% |

**결론**

zero-copy 경로도 v03과 동등하다.

## 개선

`size_` 제거로 Producer/Consumer가 각자 자신의 카운터만 수정하는 구조가 됐다. 단일 스레드 성능 회귀 없이 SPSC 확장 기반을 확립했다.

## 트레이드오프

v04의 가치는 단일 스레드 벤치로 측정되지 않는다. SPSC 실증은 별도 프로젝트에서 진행해야 한다.

## 과제

- SPSC 확장: `std::atomic<std::uint64_t>` + acquire/release로 lock-free SPSC queue를 구현할 수 있다.
- false sharing: Producer/Consumer 카운터를 서로 다른 캐시 라인에 배치(`alignas(64)`)하면 멀티스레드 환경에서 캐시 라인 경합을 제거할 수 있다.
