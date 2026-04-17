# Linear Buffer v01

## 개요

- 고정 용량 선형 바이트 버퍼
- `read_pos_` / `write_pos_` 단방향 전진, `clear()`로 리셋
- C++20 (char8_t 포함)

## 설계 결정

- **raw I/O (`read`/`write`/`peek`)**: `bool` 반환, all-or-nothing (실패 시 상태 변경 없음)
- **operator<< / >>**: 실패 시 `std::runtime_error` throw (프로토콜 위반 = 치명적)
- **zero-copy 접근**: `read_ptr()` / `write_ptr()` + `move_read_pos()` / `move_write_pos()`
- **사용자 타입 확장**: 비멤버 오버로드 패턴

## API 요약

| 카테고리          | 함수                                                       |
| :---------------- | :--------------------------------------------------------- |
| 상태              | `capacity`, `size`, `available`, `empty`, `clear`          |
| zero-copy         | `read_ptr`, `write_ptr`, `move_read_pos`, `move_write_pos` |
| raw I/O (bool)    | `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`      |
| primitive (throw) | `operator<<`, `operator>>` — 19종 오버로드                 |

## 벤치마크

환경: Intel (12 logical cores, L1 32 KiB × 6), gcc 13.3, `-O3`
측정 조건: CPU 상한 **3.0 GHz 고정**, `taskset -c 2`, 10 repetitions × 3 runs, 쿨다운 10s

| n (bytes) | 버퍼 크기 | RawWrite (mean) | RawWrite Gi/s | ZeroCopyWrite (mean) | ZeroCopyWrite Gi/s |
| --------: | --------: | --------------: | ------------: | -------------------: | -----------------: |
|        16 |      32 B |         3.02 ns |      4.9 Gi/s |              3.02 ns |           4.9 Gi/s |
|        64 |     128 B |         2.46 ns |     24.2 Gi/s |              2.35 ns |          25.4 Gi/s |
|       256 |     512 B |         4.92 ns |     48.5 Gi/s |              4.70 ns |          50.8 Gi/s |
|      1024 |      2 KB |         18.2 ns |     52.4 Gi/s |              16.9 ns |          56.3 Gi/s |
|      4096 |      8 KB |         50.7 ns |     75.2 Gi/s |              50.5 ns |          75.6 Gi/s |
|      8192 |     16 KB |         93.9 ns | **81.2 Gi/s** |              93.9 ns |      **81.3 Gi/s** |
|     16384 |     32 KB |          331 ns |     46.1 Gi/s |               329 ns |          46.4 Gi/s |
|     32768 |     64 KB |          969 ns |     31.5 Gi/s |               968 ns |          31.5 Gi/s |
|     65536 |    128 KB |         1979 ns |     30.8 Gi/s |              1986 ns |          30.7 Gi/s |

### 관찰

- **3회 재현성**: 거의 모든 수치가 실행 간 동일 (예: n=4096는 3회 모두 50.7 ns). CV 대부분 < 1%, 일부 0.02% 수준까지 안정
- **캐시 전환 선명**:
  - **n=8192 (L1 sweet spot)**: 81.2 Gi/s 피크
  - **n=16384 (L1 경계)**: 46 Gi/s로 반토막 — 32 KiB L1 한계
  - **n ≥ 32768 (L2)**: 31 Gi/s로 수렴
- **`write()` 오버헤드 = 0**: RawWrite vs ZeroCopyWrite 거의 동일. `available()` 체크 + 함수 호출이 완전히 인라인/최적화됨
- **변동 구간**: n=1024 (CV 4.8%), n=65536 (CV 최대 13%)은 캐시 경계/경합 영향. 구조적 원인

### 주파수 고정 전략

`performance` governor만으로는 부족 — boost/throttle 사이클 때문에 동일 크기도 매 실행마다 10~15% 편차. 주파수 상한을 base clock 수준(3.0 GHz)으로 잠그면:

- boost 동작 금지 → 발열 감소 → 스로틀 이벤트 없음
- 절대 성능은 내려가지만 **상대 비교가 정확**

스크립트: [../script/run_bench_v01.sh](../script/run_bench_v01.sh)

## 실행

```bash
cmake --workflow --preset debug
ctest --preset test

# 벤치 (CPU 고정 + 반복 측정)
./project/linear-buffer/script/run_bench_v01.sh
```
