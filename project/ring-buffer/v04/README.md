# Ring Buffer v04

## v03 대비 변화

`size_` 멤버 제거 + `read_pos_`/`write_pos_`를 **무한 증가 uint64_t 카운터**로.

```cpp
// v03
std::size_t read_pos_{0};   // modular [0, N)
std::size_t write_pos_{0};  // modular [0, N)
std::size_t size_{0};       // 명시적 tracking

// v04
std::uint64_t read_pos_{0};   // monotonic, 무한 증가
std::uint64_t write_pos_{0};  // monotonic
// size() = static_cast<std::size_t>(write_pos_ - read_pos_)
```

## 핵심 원리

1. **이동은 순수 덧셈**: `read_pos_ += n`, `write_pos_ += n` — 마스크 없음
2. **인덱싱 시점에만 마스크**: `buf_[read_pos_ & (N - 1)]`
3. **size 계산**: `write_pos_ - read_pos_` — unsigned modular 뺄셈으로 overflow 자동 처리
4. **overflow 안전**: uint64_t는 100 GB/s 기준 오버플로까지 ~5.8년 → 실무상 무한

## SPSC 준비의 의미

이 구조의 **진짜 가치**는 멀티스레드에 있음:

- **Producer는 `write_pos_`만 수정**
- **Consumer는 `read_pos_`만 수정**
- 각자 상대 카운터를 읽기만 함

이 구조는 SPSC lock-free queue의 **직접적 토대**:

```cpp
// 향후 cpp-spsc-queue에서 확장 예시
std::atomic<std::uint64_t> read_pos_;
std::atomic<std::uint64_t> write_pos_;

// Producer
const auto r = read_pos_.load(std::memory_order_acquire);
if (write_pos_.load(std::memory_order_relaxed) - r >= N) return false;
// ... write ...
write_pos_.store(write_pos_.load(relaxed) + n, std::memory_order_release);

// Consumer 대칭
```

- **false sharing 회피**: 두 카운터를 서로 다른 캐시 라인에 배치 (향후 `alignas(64)`)
- **atomic 경량화**: `size_` 공유 없음 → CAS 불필요, 단순 acquire/release

## 단일 스레드 성능 예상

v03 대비 **거의 동일** (4~10% 노이즈 범위):

- 이득: `size_` 수정 제거
- 손실: `size()` 계산이 load 1회 → sub 1회
- 서로 상쇄

**v04의 가치는 단일 스레드 벤치로 증명 안 됨**. SPSC 실증은 `cpp-spsc-queue`에서.

## API

v03과 동일. 구현만 바뀜.

| 카테고리       | 함수                                                                                         |
| :------------- | :------------------------------------------------------------------------------------------- |
| 상태           | `capacity()` (static constexpr), `size`, `available`, `empty`, `full`, `clear`               |
| zero-copy      | `read_ptr`, `write_ptr`, `readable_size`, `writable_size`, `move_read_pos`, `move_write_pos` |
| raw I/O (bool) | `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`                                        |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

스크립트: [../script/run_bench_v04.sh](../script/run_bench_v04.sh)

### 실측 결과: v03와 동등

| N (chunk)     | v03 WR (ns) | v04 WR (ns) |     Δ | v03 ZC (ns) | v04 ZC (ns) |     Δ |
| :------------ | ----------: | ----------: | ----: | ----------: | ----------: | ----: |
| 128 (64)      |        5.88 |        5.96 | +1.4% |        1.34 |        1.36 | +1.5% |
| 2048 (1024)   |        29.1 |        29.1 |    0% |        26.1 |        26.5 | +1.5% |
| 8192 (4096)   |        95.9 |        94.6 | -1.4% |        98.9 |        99.4 | +0.5% |
| 16384 (8192)  |         243 |         242 | -0.4% |         182 |         182 |    0% |
| 32768 (16384) |         831 |         829 | -0.2% |         415 |         417 | +0.5% |
| 65536 (32768) |        1720 |        1717 | -0.2% |         983 |         977 | -0.6% |

전 구간 **±2% 노이즈 범위**. `size_` 수정 제거 이득과 `size()` 계산(`write_pos_ - read_pos_`) 비용이 정확히 상쇄.

**이것이 좋은 결과인 이유:**

- **성능 회귀 없이** SPSC 준비 구조 완성
- 단일 스레드에선 "공짜로" producer/consumer 분리 달성
- 진짜 가치(lock-free acquire/release만으로 동작)는 `cpp-spsc-queue`에서 증명

## 교훈

- **단일 스레드 성능만 보는 벤치는 구조적 변화의 가치를 놓침** — v04는 "SPSC로 가는 구조"라는 목적이 있고, 그 목적은 지금 벤치로 검증 불가
- **구조를 먼저 만들고, 가치는 다음 단계에서 증명** — cpp-spsc-queue가 도착지
- **무한 카운터 패턴은 Linux kernel의 ringbuffer, DPDK 등 여러 락프리 자료구조에서 공통**
