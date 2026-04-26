# SPSC Queue v02

## v01 대비 변화

**뮤텍스 제거 → atomic head/tail.**

```cpp
// v01
mutable std::mutex mutex_;
std::size_t head_;
std::size_t tail_;

// v02
std::atomic<std::size_t> head_;
std::atomic<std::size_t> tail_;
```

push/pop에서 `lock_guard` 제거, 각 load/store에 memory_order 명시.

## 핵심 설계

### acquire/release 쌍

SPSC의 핵심 관찰: producer는 `tail_`만 씀, consumer는 `head_`만 씀. 두 인덱스 사이에 실제 경합이 없으므로 락 없이 memory_order만으로 정확성을 보장할 수 있다.

```cpp
// push (producer)
std::size_t head = head_.load(std::memory_order_acquire);  // consumer가 어디까지 읽었는지
buf_[tail_] = val;                                          // 데이터 쓰기
tail_.store(next, std::memory_order_release);               // 데이터 준비 완료 알림

// pop (consumer)
std::size_t tail = tail_.load(std::memory_order_acquire);  // producer가 어디까지 썼는지
auto tmp = std::move(buf_[head_]);                          // 데이터 읽기
head_.store(next, std::memory_order_release);               // 슬롯 반환 알림
```

- `tail_.store(release)` → `tail_.load(acquire)`: producer의 데이터 쓰기가 consumer에게 보임
- `head_.store(release)` → `head_.load(acquire)`: consumer의 슬롯 반환이 producer에게 보임

seq_cst는 필요 없다 — 두 스레드 간 단방향 동기화 두 쌍으로 충분.

## API

v01과 동일. 시그니처 변화 없음.

| 카테고리 | 함수                                              |
| :------- | :------------------------------------------------ |
| 상태     | `capacity()`, `size()`, `empty()`, `full()`       |
| 삽입     | `push(const T&) -> bool` — 가득 차면 false        |
| 추출     | `pop() -> std::optional<T>` — 비어 있으면 nullopt |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2,4`

스크립트: [../../script/run_spscq_v02_bench.sh](../../script/run_spscq_v02_bench.sh)

### 실측

|     N | v01 (mutex) | v02 (atomic) |               차이 |
| ----: | ----------: | -----------: | -----------------: |
|  1024 |     ~220 ns |     ~81.6 ns | **-138 ns (-63%)** |
|  4096 |     ~220 ns |     ~82.5 ns | **-138 ns (-63%)** |
| 16384 |     ~220 ns |     ~81.1 ns | **-139 ns (-63%)** |

### 원인 분해

- **v01 병목**: futex syscall 왕복 (~220 ns) — 커널 진입 + 컨텍스트 스위치
- **v02 병목**: 캐시라인 ping-pong (~81 ns) — `head_`와 `tail_`이 같은 캐시라인에 존재. producer가 `tail_`을 쓰면 consumer 코어의 캐시라인이 무효화되고, consumer가 `head_`를 쓰면 producer 코어가 무효화됨. MESI 프로토콜 왕복이 매 push/pop마다 발생.
- N이 여전히 무관한 이유: 병목이 인덱스 연산이 아니라 코어 간 캐시 동기화이기 때문

### 관찰

- **뮤텍스 제거만으로 2.7× 향상**: futex syscall이 얼마나 비쌌는지 확인
- **false sharing이 새 병목**: 논리적으로 독립된 두 변수가 물리적으로 같은 캐시라인에 있어 불필요한 캐시 무효화 발생
- **CV < 0.3%**: 뮤텍스 제거 후 측정 안정성도 크게 향상

## 다음 버전 힌트

v03 목표: **false sharing 제거 → `alignas(64)`**.

- `head_`와 `tail_` 각각을 별도 캐시라인에 배치
- producer는 `tail_` 캐시라인만, consumer는 `head_` 캐시라인만 invalidate
