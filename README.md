# cpp-study

C++ / 리눅스 저수준 학습 저장소. C++20 중심. 자료구조·시스템 호출·메모리 모델을 직접 만들면서 측정으로 검증.

## 구성

- [concept](concept/) — 학습 노트 + 짧은 실험 (linux-memory, huge-page 등)
- [project](project/) — 미니 프로젝트. 각 과제 아래 `v01`~`vNN` 진화 (linear-buffer, ring-buffer, object-pool, memory-pool)
- [script](script/) — 벤치 실행 스크립트. 모든 실행 파일 통합 명명: `<lib>_v<NN>_<test|bench>` 또는 `<lib>_<name>_bench`

## 빌드

```bash
cmake --workflow --preset debug    # 빌드 + 테스트
ctest --preset test                # 테스트만 재실행

cmake --workflow --preset release  # 벤치용 최적화 빌드
```

## 벤치 실행

CPU 주파수 고정 + 코어 핀 (sudo 필요):

```bash
./script/run_<lib>_v<NN>_bench.sh         # 예: ./script/run_mempool_v03_bench.sh
./script/run_mempool_huge_page_bench.sh   # 크로스-실험 (huge page TLB 측정)
```

벤치 규약: 3.0 GHz 고정, `taskset -c 2`, 10 repetitions + aggregates_only, CV < 1% 목표.

## 빌드 산출물 명명

| 종류             | 패턴                       | 예                        |
| ---------------- | -------------------------- | ------------------------- |
| 버전별 테스트    | `<lib>_v<NN>_test`         | `mempool_v03_test`        |
| 버전별 벤치      | `<lib>_v<NN>_bench`        | `mempool_v03_bench`       |
| 크로스 실험 벤치 | `<lib>_<experiment>_bench` | `mempool_huge_page_bench` |

라이브러리 약자: `linbuf` / `ringbuf` / `objpool` / `mempool`.
