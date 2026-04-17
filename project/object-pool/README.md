# object-pool

고정/가변 크기 객체 풀. `new`/`delete` 반복 비용을 제거하고 RAII로 자원 안전성 확보.

- **free list 기반**: 빈 슬롯 추적 자료구조 — vector 스택 → 링크드 리스트 → intrusive union → 포인터 연결 node 진화
- **Handle (RAII)**: `std::unique_ptr<T, Deleter>`로 release 누락 구조적 차단
- **가변 크기**: 청크 기반으로 포인터 안정성 유지한 채 성장

**학습 목표**:

- new/delete 대비 풀링 이득 체감
- free list 자료구조 탐색 (벡터 → 링크드 리스트 → intrusive)
- RAII Handle로 자원 안전성
- 청크 기반 가변 구조

## 버전 히스토리

|        버전 | 주요 변화                                                        | 결과                                                           |
| ----------: | :--------------------------------------------------------------- | :------------------------------------------------------------- |
| [v01](v01/) | `vector<T>` + `vector<size_t>` 스택 free list, Raw API           | 베이스라인                                                     |
| [v02](v02/) | RAII Handle (`unique_ptr<T, Deleter>`) + `acquire_unique()` 추가 | release 누락 구조적 차단. Handle 오버헤드 ~0.5ns (사실상 공짜) |

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test

./project/object-pool/script/run_bench_v01.sh
```
