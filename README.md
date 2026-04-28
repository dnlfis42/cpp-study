# cpp-study

C++ / 리눅스 저수준 학습 저장소. 자료구조·동시성·메모리 시스템을 직접 구현하고 측정으로 검증한다.

## 트랙

| 트랙                | 디렉터리   | 형태                          |
| :------------------ | :--------- | :---------------------------- |
| [concept](concept/) | `concept/` | 노트 전용 또는 코드+벤치      |
| [project](project/) | `project/` | vNN 버전 단위, 설계 점진 발전 |

## 빌드

### 요구사항

| 항목             | 최소 버전 |
| :--------------- | :-------- |
| C++              | 20        |
| CMake            | 3.25      |
| GTest            | -         |
| Google Benchmark | -         |

### 커맨드

| 커맨드                                 | 동작                                   |
| :------------------------------------- | :------------------------------------- |
| `cmake --workflow --preset debug`      | configure + build (Debug)              |
| `cmake --workflow --preset release`    | configure + build (Release, 벤치용)    |
| `cmake --workflow --preset asan`       | configure + build (Debug + ASAN·UBSAN) |
| `cmake --workflow --preset debug-test` | configure + build + test (Debug)       |
| `cmake --workflow --preset asan-test`  | configure + build + test (ASAN·UBSAN)  |
| `ctest --preset debug`                 | test only (debug 빌드 전제)            |
| `ctest --preset asan`                  | test only (asan 빌드 전제)             |

## 환경

### 하드웨어

| 항목      | 값                                          |
| :-------- | :------------------------------------------ |
| CPU       | Intel Core i7-9750H (6C/12T, base 2.60 GHz) |
| L1d / L1i | 32 KiB per core                             |
| L2        | 256 KiB per core                            |
| L3        | 12 MiB (shared)                             |
| OS        | Ubuntu 24.04                                |
| 컴파일러  | GCC 13.3                                    |

### 벤치마크

| 항목        | 값                                                       |
| :---------- | :------------------------------------------------------- |
| 주파수      | 3.0 GHz 고정 (governor: performance)                     |
| 코어 핀     | st: core 2 / dt: core 2,4 (non-HT) · 2,8 (HT) / mt: 가변 |
| repetitions | 10, aggregates_only                                      |
| 목표 CV     | < 1%                                                     |
