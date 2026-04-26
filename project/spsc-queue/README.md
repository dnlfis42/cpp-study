# spsc-queue

단일 생산자 단일 소비자 락-프리 큐. 스레드 간 메시지 전달 저지연 구현.

- **SPSC**: producer/consumer 각각 1스레드만 — head/tail 변수에 경합 없음
- **ring buffer**: 고정 크기 배열 + wrap-around 인덱스
- **template\<T, N\>**: 컴파일 타임 용량, 2의 거듭제곱 강제
- **C++20**

## 버전 히스토리

|        버전 | 주요 변화                               | 결과                                                     |
| ----------: | :-------------------------------------- | :------------------------------------------------------- |
| [v01](v01/) | `std::mutex` 기반 — 기준선              | ~220 ns/pop (N 무관, 병목 = futex contention)            |
| [v02](v02/) | `atomic` acquire/release — 뮤텍스 제거  | ~81 ns/pop (2.7×↑, 병목 = false sharing)                 |
| [v03](v03/) | `alignas(64)` — false sharing 제거 시도 | ~84 ns/pop (악화. 병목 = 데이터 캐시라인 이동)           |
| [v04](v04/) | cached head/tail — cross-core read 감소 | ~98~104 ns/pop (악화. 벤치가 항상 full — 이득 경로 없음) |
