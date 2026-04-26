# SPSC Queue v01

## 핵심 설계

**뮤텍스 기반 링 버퍼.** lock-free를 구현하기 전에 뮤텍스가 SPSC 성능에 얼마나 걸림돌인지 기준선 측정.

### `template<typename T, std::size_t N>` — 컴파일 타임 용량

```cpp
template <typename T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    ...
};
```

- 용량을 런타임 인자로 받지 않고 **템플릿 파라미터**로 고정 — `capacity_` 멤버 불필요
- `static_assert`로 2의 거듭제곱 강제 → 나머지 연산을 `& (N - 1)` 비트마스크로 대체

### 1슬롯 낭비 — full/empty 구분

head == tail이면 empty, `(tail + 1) & (N - 1) == head`면 full.  
N개 배열에 **최대 N-1개** 저장 가능. 구분 비트나 카운터 없이 인덱스만으로 상태 판별.

```cpp
bool empty() const noexcept { return head_ == tail_; }
bool full()  const noexcept { return ((tail_ + 1) & (N - 1)) == head_; }
std::size_t size() const noexcept { return (N + tail_ - head_) & (N - 1); }
```

### 뮤텍스 — push/pop 모두 동일 락

```cpp
bool push(const T& val) {
    std::lock_guard lock{mutex_};
    ...
}
std::optional<T> pop() {
    std::lock_guard lock{mutex_};
    ...
}
```

producer, consumer 모두 같은 `mutex_`를 잡음. SPSC인데도 **경합이 반드시 발생**. v02에서 atomic으로 제거 예정.

## API

| 카테고리 | 함수                                              |
| :------- | :------------------------------------------------ |
| 상태     | `capacity()`, `size()`, `empty()`, `full()`       |
| 삽입     | `push(const T&) -> bool` — 가득 차면 false        |
| 추출     | `pop() -> std::optional<T>` — 비어 있으면 nullopt |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2,4`

- **BM_Spsc_Throughput\<N\>**: producer 스레드가 계속 push, benchmark 루프에서 pop 1회 대기
- N을 바꿔도 경합 구조(뮤텍스)가 동일해 차이가 없는지 확인

스크립트: [../../script/run_spscq_v01_bench.sh](../../script/run_spscq_v01_bench.sh)

### 실측

10 repetitions, aggregates only (mean 기준)

|     N | 처리량 (pop 1회) |
| ----: | ---------------: |
|  1024 |          ~220 ns |
|  4096 |          ~220 ns |
| 16384 |          ~220 ns |

### 원인 분해

N이 달라져도 수치가 동일한 이유: **병목이 뮤텍스**이기 때문.

- `std::mutex`의 `lock()/unlock()` = 내부적으로 futex syscall 경쟁
- producer가 lock을 잡고 있는 동안 consumer는 대기, 반대도 마찬가지
- 링 버퍼 용량(N)은 hot path의 인덱스 연산(`& (N - 1)`) 뿐 — 이 비용은 뮤텍스 비용에 완전히 묻힘
- 실제 원소 복사(`buf_[tail_] = val`)도 `int` 4바이트 — 무시 가능

### 관찰

- **220ns = futex contention 비용**: 같은 락을 두 스레드가 매 pop/push마다 번갈아 잡을 때 전형적인 수치
- **N 무관**: capacity가 1024든 16384든 병목은 락 획득 순서와 컨텍스트 스위치 여부
- **SPSC의 전제 위반**: SPSC는 writer와 reader가 서로 다른 변수(head/tail)만 건드린다는 점을 이용해 락을 없애야 의미 있음 — v01은 그 전제를 뮤텍스로 무력화한 기준선

## 다음 버전 힌트

v02 목표: **뮤텍스 제거 → atomic head/tail**.

- producer는 `tail_`만 씀, consumer는 `head_`만 씀 → 두 인덱스 사이에 실제 경합 없음
- `tail_`을 atomic으로 선언하고 적절한 memory_order 부여
- 어떤 memory_order가 정확성(data race 없음)을 보장하는 최소 순서인가? → `acquire/release` vs `seq_cst` 비교
