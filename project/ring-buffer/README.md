# ring-buffer

고정 용량 원형 바이트 버퍼. 네트워크 스트림 수신/송신 버퍼용.

- **ring (wrap-around)**: `read_pos`/`write_pos` 둘이 고정 크기 배열 위를 순환한다.
- **contiguous region**: wrap 때문에 연속 읽기/쓰기 가능 바이트는 `readable_size()`/`writable_size()`로 별도 노출한다.
- **bool all-or-nothing I/O**: partial transfer 없음. `read()`, `write()`, `peek()`은 전부 성공하거나 false를 반환한다.
- v02~v04는 `template<N>`이라 생성 시 용량을 타입 인자로 지정한다: `RingBuffer<1024> rb;`

## 성능 종합

WriteRead(`write(chunk)` + `read(chunk)`) 기준. 통합 벤치(`ringbuf_bench`) 측정값.

| chunk | v01 (runtime %) | v02 (bitmask) | v03 (noclone) | v04 (seq counter) |
| ----: | --------------: | ------------: | ------------: | ----------------: |
|    64 |         23.3 ns |       2.99 ns |       6.20 ns |           5.35 ns |
|  4096 |          100 ns |        191 ns |       97.2 ns |           94.2 ns |
| 65536 |         3971 ns |       4063 ns |       3989 ns |           3993 ns |

환경: Intel i5-10400 (L1 32 KiB, L2 256 KiB, L3 12 MiB), gcc 13.3, `-O3`, CPU 상한 3.0 GHz 고정, `taskset -c 2`

## 버전

### v01

**개선**

고정 용량 원형 바이트 버퍼 기반을 확립한다. `write_ptr` + `move_write_pos` 경로로 zero-copy recv를 수용하고, bool all-or-nothing I/O로 `linear-buffer`와 동일한 사용 패턴을 유지한다.

**트레이드오프**

wrap 지원 대가로 `% capacity_` 나눗셈(~20 사이클)과 `size_` 별도 관리 비용이 든다.

**과제**

- `% capacity_` 나눗셈: capacity를 2의 거듭제곱으로 고정하면 `& (N-1)` 비트마스크로 대체 가능하다.

### v02

**개선**

소용량(chunk <= 256)에서 v01 대비 최대 7.8배 빠르다(chunk=64: 23.3 ns → 2.99 ns). `%` → `&` 치환과 compile-time 크기 특화가 결합된 결과다.

**트레이드오프**

compile-time N이 memcpy 크기를 상수로 전파시켜 중간 크기(chunk ~4096)에서 `rep movsq` 인라인을 유발한다. chunk=4096 기준 v01 대비 1.9배 느리다(100 ns → 191 ns). IPC 2.61 → 0.06.

**과제**

- `rep movsq` 인라인: compile-time N 전파가 중간 크기에서 `rep movsq`를 강제해 chunk=4096 기준 v01 대비 1.9배 느리다.

### v03

**개선**

chunk=4096 기준 v02 대비 2.0배 빠르고(191 ns → 97.2 ns) v01보다도 빠르다(100 ns → 97.2 ns). `&` 비트마스크와 runtime dispatch memcpy 두 이득이 결합된 결과다. IPC 0.06 → 2.96 회복.

**트레이드오프**

`call memcpy@plt` 오버헤드로 소용량(chunk <= 256)에서 v02보다 느리다(2.99 ns → 6.20 ns). zero-copy 경로에는 개선이 적용되지 않는다.

**과제**

- `noclone` GCC 전용: clang 빌드에서는 IPA-CP가 constprop 클론을 생성해 `rep movsq`가 재발할 수 있다.

### v04

**개선**

단일 스레드 성능 회귀 없이(v03과 ±2%) Producer/Consumer가 각자 자신의 카운터만 수정하는 구조가 됐다. `std::atomic` + acquire/release만으로 lock-free SPSC queue로 확장 가능하다.

**트레이드오프**

v04의 가치는 단일 스레드 벤치로 측정되지 않는다. SPSC 실증은 별도 프로젝트에서 진행해야 한다.

**과제**

- SPSC 확장: `std::atomic<std::uint64_t>` + acquire/release로 lock-free SPSC queue를 구현할 수 있다.
- false sharing: Producer/Consumer 카운터를 서로 다른 캐시 라인에 배치(`alignas(64)`)하면 멀티스레드 환경에서 캐시 라인 경합을 제거할 수 있다.

## 참고

- [Intel 64 and IA-32 Architectures Optimization Reference Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — `rep movsq` 마이크로코드 동작
- [Linux kernel kfifo](https://elixir.bootlin.com/linux/latest/source/include/linux/kfifo.h) — 무한 증가 카운터 패턴
