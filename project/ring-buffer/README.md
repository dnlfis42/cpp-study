# ring-buffer

고정 용량 원형 바이트 버퍼. 네트워크 스트림 수신/송신 버퍼용.

- **ring (wrap-around)**: `read_pos`/`write_pos` 둘이 고정 크기 배열 위를 순환
- **contiguous region 개념**: wrap 때문에 연속 읽기/쓰기 가능 바이트는 `readable_size()`/`writable_size()`로 따로 노출
- **C++20**
- `linear-buffer`와 달리 메시지 단위가 아닌 **스트림**이 주 용도

## 버전 히스토리

|        버전 | 주요 변화                                                               | 결과                                                                                         |
| ----------: | :---------------------------------------------------------------------- | :------------------------------------------------------------------------------------------- |
| [v01](v01/) | `unique_ptr<byte[]>` 기반 runtime capacity, raw I/O bool all-or-nothing | 베이스라인                                                                                   |
| [v02](v02/) | `template<std::size_t N>` + `& (N-1)` bitmask                           | 작은 chunk 3~4× 빠름. **단, compile-time 크기로 `rep movsq` 인라인되어 4KB에서 1.7× 느려짐** |
| [v03](v03/) | `[[gnu::noinline, gnu::noclone]]` 래퍼로 `rep movsq` 회피               | 4KB에서 v02 대비 **2× 빠름**, v01보다도 빠름. IPC 0.06 → 2.96 회복                           |
| [v04](v04/) | `size_` 제거 + `uint64_t` 무한 증가 카운터 (SPSC 준비)                  | v03와 동등 (±2%). producer/consumer 카운터 분리 완성 — `cpp-spsc-queue`의 토대               |

## API (공통)

- 상태: `capacity()`, `size()`, `available()`, `empty()`, `full()`, `clear()`
- zero-copy: `read_ptr()`, `write_ptr()`, `readable_size()`, `writable_size()`, `move_read_pos()`, `move_write_pos()`
- raw I/O (bool): `read()`, `write()`, `peek()`

v02~v04는 `template<N>`이라 생성 시 용량을 타입 인자로 지정: `RingBuffer<1024> rb;`

## 성능 종합 (chunk=4096 기준)

| 버전    | Storage                            | Time (WriteRead) |      IPC | 비고                                 |
| :------ | :--------------------------------- | ---------------: | -------: | :----------------------------------- |
| v01     | runtime `capacity_` + `%`          |           102 ns |     2.61 | 베이스라인, SIMD 언롤                |
| **v02** | `template<N>` + `&` bitmask        |       **189 ns** | **0.06** | **`rep movsq` 인라인 함정**          |
| v03     | v02 + `noinline/noclone` 래퍼      |        **96 ns** |     2.96 | `rep movsq` 회피, **v01보다도 빠름** |
| v04     | v03 + `size_` 제거 + uint64 카운터 |            95 ns |     2.96 | v03와 동등, **SPSC 준비 완료**       |

환경: Intel, gcc 13.3, `-O3`, CPU 상한 3.0 GHz 고정, `taskset -c 2`

### 캐시 전환 (v03 기준)

| n (chunk) | 버퍼 크기 |   ZeroCopy |      대역폭 | 위치        |
| --------: | --------: | ---------: | ----------: | :---------- |
|      4096 |      8 KB |      57 ns |     67 Gi/s | L1          |
|  **8192** | **16 KB** | **107 ns** | **71 Gi/s** | **L1 peak** |
|     16384 |     32 KB |     418 ns |     37 Gi/s | L1 경계     |
|     65536 |    128 KB |    1989 ns |     31 Gi/s | L2          |

## 전체 발견

### 1. compile-time 정보가 많을수록 빠르지는 않다 (v02 함정)

`template<N>`으로 capacity를 compile-time 상수로 주면 `%` → `&` 치환이 가능해 **작은 chunk는 3~4× 빠름**. 하지만 memcpy의 크기도 compile-time 상수가 되면서 gcc가 **`rep movsq`(마이크로코드 문자열 복사) 인라인**을 선택 → **4KB에서 1.7× 느려짐**.

**핵심 시그널**: IPC 0.06 (마이크로코드 serialization). `perf stat`로 즉시 진단 가능.

### 2. `noinline` 단독으론 IPA-CP 차단 불가

v03에서 `[[gnu::noinline]]` 래퍼로 memcpy를 감싸도 컴파일러가 **IPA-CP로 constprop clone**(`copy_bytes.constprop.4096`) 생성 → 본체에서 `n`이 다시 상수 → `rep movsq` 재삽입. **`noclone`까지 붙여야 runtime dispatch 보장**.

gcc 전용 속성이라 clang에선 매크로 분기 필요:

```cpp
#if defined(__GNUC__) && !defined(__clang__)
#define RINGBUF_NOINLINE_NOCLONE [[gnu::noinline, gnu::noclone]]
#else
#define RINGBUF_NOINLINE_NOCLONE [[gnu::noinline]]
#endif
```

### 3. 어셈블리 확인이 최종 증거

수치는 가설 방향을 정반대로 틀리게 할 수 있음 ("SIMD가 느리다"고 오해하기 쉬움). `perf stat`의 IPC + `objdump -d`의 실제 명령어 확인이 유일한 확실한 방법.

### 4. 조합 이득: bitmask + runtime dispatch memcpy

v03에서 **두 최적화가 공존**:

- `& (N-1)` — `div` 회피 (작은 chunk에서 이득)
- `jmp memcpy@plt` — SIMD 언롤 (큰 chunk에서 이득)

결과: **v01보다 빠름** (chunk=4096 102ns → 96ns). 각 최적화를 분리 측정하니 어느 것이 어디서 효과 있는지 명확해짐.

### 5. 구조적 변화의 가치는 단일 스레드 벤치로 증명 안 됨 (v04)

`size_` 제거 + uint64 무한 카운터로 **producer/consumer 완전 분리**. 단일 스레드 성능은 v03와 동등(±2%). **진짜 가치는 SPSC로 확장했을 때만 드러남** — lock-free acquire/release 최적화의 구조적 기반이 준비됨.

### 6. Ring 전용 API: `readable_size()` / `writable_size()`

linear-buffer엔 없는 개념:

- `available()`: 총 쓸 수 있는 바이트 수 (wrap 포함)
- `writable_size()`: **현재 write_ptr에서 연속으로** 쓸 수 있는 바이트 (wrap 직전까지)

scatter I/O(`iovec`, `writev`/`readv`)와 자연스럽게 연결되는 추상화.

### 7. linear-buffer 대비 10~20% 오버헤드

같은 ZeroCopy(단방향 memcpy) 기준:

- linear n=4K: 75 Gi/s
- ring n=4K: 67 Gi/s — **~11% 느림**

원인: wrap 분기 + `% capacity_` (v01) 또는 `& (N-1)` (v02~). **wrap 의미를 위한 비용**. 스트리밍 용도면 감수할 만함.

## 측정 방법론

- **3.0 GHz 고정 + taskset**: 열 스로틀/코어 마이그레이션 차단
- **perf stat -e cycles,instructions**: IPC 측정 → 마이크로코드 의심 (< 0.5는 이상 신호)
- **objdump -d --demangle**: 실제 명령어 확인, `rep movs*` / `jmp memcpy@plt` 구분
- **버전 간 공통 벤치 포맷** (`BM_WriteRead<N>`): 사이즈/버전 대조 즉시

## 미해결 / 다음 단계

- **SPSC 실증**: v04의 Producer/Consumer 카운터 분리 가치는 `cpp-spsc-queue` 프로젝트에서 `std::atomic` + memory order로 증명
- **멀티스레드 벤치**: 2개 스레드가 동시에 write/read하는 워크로드 측정
- **scatter-gather (writev/readv)**: `writable_size`/`readable_size`를 `iovec`으로 엮어 zero-copy recv/send

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test

# 벤치 (CPU 고정 + 반복)
./project/ring-buffer/script/run_bench_v01.sh
./project/ring-buffer/script/run_bench_v02.sh
./project/ring-buffer/script/run_bench_v03.sh
./project/ring-buffer/script/run_bench_v04.sh
```
