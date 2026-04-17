# Linear Buffer v02

## v01 대비 추가

C++20 `std::span` 기반 zero-copy read API 도입.

| 이름          | 시그니처                   | 동작                                               |
| :------------ | :------------------------- | :------------------------------------------------- |
| `read_span()` | `span<const byte>` (const) | 현재 읽을 수 있는 영역 전체 뷰, read_pos 이동 없음 |
| `read(n)`     | `span<const byte>` 반환    | n바이트 뷰 + read_pos 전진. 부족 시 빈 span        |
| `peek(n)`     | `span<const byte>` (const) | n바이트 뷰, read_pos 이동 없음. 부족 시 빈 span    |

**동기**: v01의 `read(byte*, n)`은 항상 memcpy로 복사. span 반환 버전은 **버퍼 내부를 직접 가리키는 뷰**를 반환해 복사 생략.

**제약 (수명)**: 반환된 span은 버퍼 유효 기간 내에서만 안전. `clear()`나 write가 해당 영역을 덮어쓰기 전까지만 유효.

## 설계 결정 (v01과 동일)

- raw I/O (`read`/`write`/`peek`): `bool` 반환, all-or-nothing
- operator<< / >>: 실패 시 `std::runtime_error`
- zero-copy 접근: `read_ptr` / `write_ptr` + `move_*_pos`
- 사용자 타입 확장: 비멤버 오버로드

## API 요약 (v02 추가분 굵게)

| 카테고리             | 함수                                                                        |
| :------------------- | :-------------------------------------------------------------------------- |
| 상태                 | `capacity`, `size`, `available`, `empty`, `clear`                           |
| zero-copy            | `read_ptr`, `write_ptr`, `move_read_pos`, `move_write_pos`, **`read_span`** |
| raw I/O (bool)       | `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`                       |
| zero-copy I/O (span) | **`read(n)`**, **`peek(n)`**                                                |
| primitive (throw)    | `operator<<`, `operator>>` — 19종 오버로드                                  |

## 벤치마크

환경: Intel (12 logical cores, L1 32 KiB × 6), gcc 13.3, `-O3`
측정 조건: CPU 상한 **3.0 GHz 고정**, `taskset -c 2`, 10 repetitions × 3 runs

`BM_ReadSpan`은 한 iteration에 `write() + read(n)`을 함께 측정. `read(n)`은 span 반환이라 memcpy 없음.

| n (bytes) | 버퍼 크기 | RawWrite (mean) | RawWrite Gi/s | ReadSpan (mean) | ReadSpan Gi/s |
| --------: | --------: | --------------: | ------------: | --------------: | ------------: |
|        16 |      32 B |         3.02 ns |      4.9 Gi/s |         2.68 ns |      5.6 Gi/s |
|        64 |     128 B |         2.45 ns |     24.3 Gi/s |         2.12 ns |     28.1 Gi/s |
|       256 |     512 B |         5.50 ns |     43.4 Gi/s |         4.36 ns |     54.7 Gi/s |
|      1024 |      2 KB |         17.3 ns |     55.3 Gi/s |         13.1 ns |     72.7 Gi/s |
|      4096 |      8 KB |         51.4 ns |     74.2 Gi/s |         47.3 ns |     80.9 Gi/s |
|      8192 |     16 KB |         94.1 ns | **81.1 Gi/s** |         90.6 ns | **84.3 Gi/s** |
|     16384 |     32 KB |          335 ns |     45.5 Gi/s |          336 ns |     45.4 Gi/s |
|     32768 |     64 KB |          968 ns |     31.5 Gi/s |          968 ns |     31.5 Gi/s |
|     65536 |    128 KB |         1938 ns |     31.5 Gi/s |         1925 ns |     31.7 Gi/s |

### 관찰

- **작은 크기(n ≤ 8192)에서 ReadSpan이 더 빠름**: `write() + read(n)`을 같이 하는데도 `write()` 단독보다 빠름
  - 이유: `BM_RawWrite`는 `DoNotOptimize(lb)`로 **LinearBuffer 전체 객체**를 관찰 강제 → 모든 내부 상태 writeback. `BM_ReadSpan`은 `DoNotOptimize(span)`으로 span(ptr+size)만 관찰 → 오버헤드 적음
  - 즉 순수 read 비용 비교라기보다 **벤치 구조상 DoNotOptimize 오버헤드 차이**가 드러난 것
- **큰 크기(n ≥ 16384)에서 수렴**: memcpy 대역폭이 지배 → DoNotOptimize 차이 묻힘. 둘 다 같은 L2 bandwidth로 수렴 (~31 Gi/s)
- **v02의 핵심 가치**: `read(n)`은 **memcpy 완전히 생략**. 순수 read에 `read(byte*, n)` 대비 거의 0 비용

### 해석상 주의

벤치 비교 시 `DoNotOptimize` 대상을 맞춰야 공정. 현재 표는 "write + span 반환 read" vs "write" 비교라 엄밀히 같은 워크로드 아님. 다음 버전이나 추가 벤치에서 정제 가능.

### 캐시 전환

v01과 동일 패턴:

- **n=8192 (L1 sweet spot)**: peak 81~84 Gi/s
- **n=16384 (L1 경계)**: 45 Gi/s로 반토막 — 32 KiB L1 한계
- **n ≥ 32768 (L2)**: 31 Gi/s 수렴

스크립트: [../script/run_bench_v02.sh](../script/run_bench_v02.sh)
