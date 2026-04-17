# Ring Buffer v01

## 개요

- 고정 용량 원형 바이트 버퍼
- `read_pos_` / `write_pos_` + `size_` (3 상태 변수)
- wrap 시 두 번의 memcpy로 분할 처리
- C++20, `std::byte`, `std::unique_ptr<std::byte[]>`

## 설계 결정 (회고 포함)

### 왜 vector가 아닌 `unique_ptr<std::byte[]>`인가

처음에는 `std::vector<std::byte>`로 충분. 하지만:

- Rule of 5에서 move/copy default로 끝낼 수 있음 (unique_ptr이 소유권 명시)
- `buf_.size()`를 `capacity_` 멤버로 캐시해 `size()` 의미를 "읽을 수 있는 바이트 수"로 예약 가능

**교훈**: 성능 차이는 거의 없음 (`vector::size()`는 인라인됨). 진짜 이득은 **소유권 구조의 단순화**와 **`size()` 네이밍 의미론**.

### 왜 raw I/O가 bool all-or-nothing인가

이전 구현은 `std::size_t` 반환으로 **partial transfer** 지원 (실제 전송량 반환). 하지만 사용처를 돌아보면:

- **recv 시나리오**: `recv()`에서 받은 실제 바이트 수로 `move_write_pos()` 호출 — `write_ptr` + `move_write_pos` 조합 사용. `write()` 경로 안 탐
- **send 시나리오**: 메시지는 완결되어야 함. "일부만 보낼 수 있어"는 의미 없음 — 실패면 버퍼 폐기
- **헤더 파싱**: `peek(헤더 크기)` → 부족하면 `break` (더 기다림), 충분하면 처리
- 어느 경우든 "일부만 전송" 동작은 쓰이지 않음

그래서 **linear-buffer와 동일한 `bool` all-or-nothing 의미**로 통일. 실패 시 상태 변경 없음 보장.

### 왜 `readable_size()` / `writable_size()`가 따로 있는가

linear-buffer엔 없는 링 전용 개념:

- `available()`: 총 쓸 수 있는 바이트 수 (wrap을 포함)
- `writable_size()`: **현재 write_ptr 위치에서 연속으로** 쓸 수 있는 바이트 수 (wrap 직전까지)

wrap 상황에서 `write_ptr()` + `writable_size()`만으로는 부족할 수 있음 — 사용자가 직접 두 번에 나눠 써야 할 수도. 이는 **zero-copy recv** 같은 스캐터 I/O에서 `iovec`으로 연결됨.

## API 요약

| 카테고리       | 함수                                                                                         |
| :------------- | :------------------------------------------------------------------------------------------- |
| 상태           | `capacity`, `size`, `available`, `empty`, `full`, `clear`                                    |
| zero-copy      | `read_ptr`, `write_ptr`, `readable_size`, `writable_size`, `move_read_pos`, `move_write_pos` |
| raw I/O (bool) | `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`                                        |

## 벤치마크

환경: Intel (12 logical cores, L1 32 KiB × 6), gcc 13.3, `-O3`
측정 조건: CPU 상한 **3.0 GHz 고정**, `taskset -c 2`, 10 repetitions

벤치 항목:

- **BM_WriteRead**: `write(n) + read(n)` — 양방향 memcpy 2회. 대역폭은 `chunk * 2` 기준
- **BM_WriteReadWrap**: 초기 오프셋으로 wrap 강제. 버퍼 크기 `chunk + chunk/2`
- **BM_ZeroCopy**: `memcpy(write_ptr, src, n) + move_write_pos + move_read_pos` — 단방향 memcpy. 대역폭은 `chunk` 기준

| n (bytes) | WriteRead (mean) | WriteRead Gi/s | ZeroCopy (mean) | ZeroCopy Gi/s |
| --------: | ---------------: | -------------: | --------------: | ------------: |
|        16 |          24.1 ns |       1.2 Gi/s |         19.3 ns |      0.8 Gi/s |
|        64 |          24.1 ns |       5.0 Gi/s |         19.3 ns |      3.1 Gi/s |
|       256 |          27.1 ns |      17.6 Gi/s |         20.6 ns |     11.6 Gi/s |
|      1024 |          40.7 ns |      46.8 Gi/s |         28.7 ns |     33.2 Gi/s |
|      4096 |           102 ns |      75.1 Gi/s |         57.2 ns |     66.7 Gi/s |
|      8192 |           245 ns |      62.2 Gi/s |          107 ns | **71.4 Gi/s** |
|     16384 |           836 ns |      36.5 Gi/s |          418 ns |     36.5 Gi/s |
|     32768 |          1905 ns |      32.0 Gi/s |          979 ns |     31.2 Gi/s |
|     65536 |          3682 ns |      33.2 Gi/s |         1989 ns |     30.7 Gi/s |

### 관찰

- **WriteRead ≈ ZeroCopy × 2**: WriteRead는 memcpy 2회, ZeroCopy는 1회. 시간 비율도 대략 2배 (예: n=4096에서 102 vs 57 ns)
- **wrap 오버헤드 미미**: `WriteRead` vs `WriteReadWrap` 차이 n ≤ 4096에서 1~3 ns 수준. split memcpy 두 번 호출의 추가 비용은 작음
- **n=8192 ZeroCopy 피크 71.4 Gi/s**: 버퍼 16 KB가 L1에 여유있게 들어감
- **n ≥ 16384 (L1 초과)**: L2 대역폭으로 수렴 (~33 Gi/s 양방향, ~31 Gi/s 단방향)

### linear-buffer 대비 오버헤드

동일한 ZeroCopy(단방향 memcpy) 기준:

|     n | linear-buffer | ring-buffer | ring 오버헤드 |
| ----: | ------------: | ----------: | ------------: |
|  4096 |       75 Gi/s |     67 Gi/s |          ~11% |
|  8192 |       81 Gi/s |     71 Gi/s |          ~12% |
| 16384 |       46 Gi/s |     37 Gi/s |          ~20% |

ring이 10~20% 느린 건 **`% capacity_` 나눗셈 + `size_` 별도 관리** 오버헤드. wrap 의미를 지원하기 위한 비용.

스크립트: [../script/run_bench_v01.sh](../script/run_bench_v01.sh)

## linear-buffer와의 차이

| 항목            | linear-buffer           | ring-buffer                          |
| :-------------- | :---------------------- | :----------------------------------- |
| 위치            | 고정                    | 순환 (wrap)                          |
| 상태 변수       | `read_pos_, write_pos_` | + `size_` (empty/full 구분)          |
| `available()`   | `capacity - write_pos`  | `capacity - size`                    |
| contiguous 개념 | 필요 없음               | `writable_size()`, `readable_size()` |
| 주 용도         | 메시지 단위             | 스트림                               |

`linear-buffer`와 **대응되는 공통 API** (`write_ptr`, `read_ptr`, `move_*_pos`, `available`, `read`, `write`, `peek`)는 이름/의미를 맞춰 교차 사용 시 인지 부담 최소화.
