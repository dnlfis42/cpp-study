# object-pool

고정/가변 크기 객체 풀. `new`/`delete` 반복 비용을 제거하고 RAII로 자원 안전성 확보.

- **free list 기반**: 빈 슬롯 추적 자료구조 — vector 스택 → 인덱스 링크드 리스트 → 포인터 연결 Node 진화
- **Handle (RAII)**: `std::unique_ptr<T, Deleter>`로 release 누락 구조적 차단
- **가변 크기**: 청크 기반으로 포인터 안정성 유지한 채 성장

**학습 목표**:

- new/delete 대비 풀링 이득 체감
- free list 자료구조 탐색 (벡터 스택 → 인덱스 링크드 리스트 → 포인터 연결)
- RAII Handle로 자원 안전성
- 청크 기반 가변 구조 + 포인터 안정성

## 버전 히스토리

|        버전 | 주요 변화                                                                             | 결과                                                                                                           |
| ----------: | :------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------- |
| [v01](v01/) | `vector<T>` + `vector<size_t>` 스택 free list, Raw API                                | 베이스라인                                                                                                     |
| [v02](v02/) | RAII Handle (`unique_ptr<T, Deleter>`) + `acquire_unique()` 추가                      | release 누락 구조적 차단. Handle 오버헤드 ~0.5ns (사실상 공짜)                                                 |
| [v03](v03/) | free list를 **인덱스 링크드 리스트**로 (`Node { T data; size_t next; }`)              | 구조적 순수성, cache locality 이득. 단, sizeof(Node) 증가로 hot path 느려질 수 있음                            |
| [v04](v04/) | Raw API를 private로 — **Handle 전용 공개**. `acquire()`가 Handle 반환                 | 타입 시스템으로 release 누락/cross-pool 반환 영구 차단. 성능은 v03 Handle과 동등                               |
| [v05](v05/) | **청크 기반 가변 풀** — `Node { T; Node* next; }` 포인터 연결. lazy 초기화, 자동 성장 | 초기 capacity 예측 불필요. 포인터 안정성(성장 중에도 기존 주소 불변). 인덱스→포인터 전환으로 v04보다 빠를 수도 |

## 성능 종합

chunk_size = 64 기준 (hot loop, 같은 Item):

|         | API    | Storage                  | Time (ns) | 비고            |
| ------: | :----- | :----------------------- | --------: | :-------------- |
| **v01** | Raw    | `vec<T>` + `vec<size_t>` |      2.42 | 기준            |
| **v02** | Raw    | 동일                     |      2.44 | 구현 동일       |
| **v02** | Handle | 동일                     |      4.15 | Deleter +1.7ns  |
| **v03** | Raw    | `vec<Node>` + idx        |      3.02 | Node 구조 +24%  |
| **v03** | Handle | 동일                     |      4.90 | Raw + Deleter   |
| **v04** | Handle | 동일                     |      4.86 | API 축소 비용 0 |
| **v05** | Handle | **chunk<Node>** + `ptr`  |  **4.48** | **v04보다 -8%** |

베이스라인: **new/delete = 21 ns**. 모든 풀 버전이 약 4~8× 빠름.

## 전체 발견

### 1. Handle 오버헤드는 실무적으로 공짜

**+1.7ns / +70% 상대 증가**지만 절대값 미미 (hot path도 4ns 수준). release 누락 방지라는 **영구적 안전성**과 맞바꾸면 합리적.

### 2. 인덱스 vs 포인터 free list

같은 링크드 리스트 구현이라도:

- 인덱스(v03/v04): `base + idx * sizeof(Node)` — 곱셈 + 덧셈
- 포인터(v05): 직접 역참조 — 1 cycle 절약

hot path에서 **~7~8% 일관되게 빠름**. v05가 v04보다 빠른 주요 원인.

### 3. Node 구조 도입의 비용

v02 → v03 전환에서 **Raw path가 24% 느려짐**. 원인 분해:

- `sizeof(Node)` 증가 → cache stride 증가 (이 벤치에선 LIFO라 크게 안 보임)
- 간접 참조 한 단계 추가 (`storage_[idx].next` / `.data`)
- 명시적 `available_` 카운터 store

**교훈**: "구조적 순수성"은 공짜가 아니다. 자료구조 선택은 성능 측정과 짝.

### 4. Raw API private화(v04) 비용 제로

API 표면 축소로 안전성 획득. 성능은 **v03 Handle과 동등**. gcc의 인라인 결정에 public/private 여부가 영향 없음 확인.

### 5. 청크 기반 가변 풀의 의외성

가변성을 위해 추가 자료구조(`vector<unique_ptr<Node[]>>`) 도입했지만 **hot path엔 미반영**. 한 번 grow 후엔 `head_free_` 포인터만 사용 → **고정 풀보다 오히려 빠를 수 있음**. 포인터 안정성까지 포함해 **실무 기본값으로 적합**.

### 6. Cross-pool 포인터 반환 — 실무는 assert

런타임 체크(pool ID 태그) 가능하지만 STL/allocator 관용은 **assert + 문서 계약**. C++ "don't pay for what you don't use" 일치. 디버그에서 잡히면 충분.

### 7. 스킵한 intrusive union (원본 v05)

`union Slot { T value; size_t next; }`로 메모리 공유 + placement new 생애주기 관리. 메모리 절약이 가능하지만:

- hot path ~20% 느림 (원본 회고)
- TCP 서버 맥락 T는 보통 커서 이득 희석
- 복잡도 대비 실익 적음

이 연작에선 생략. 학습 가치는 `concept/placement-new/` 같은 별도 짧은 실험으로 담당 예정.

## 측정 방법론

- **hot loop만으론 부족**: `NoWarmup` vs `Warmup` 실측 결과 동일 — LIFO가 같은 슬롯 반복 참조라 warmup 효과 희석. **현실 워크로드 측정엔 다중 holding + 교체 패턴이 필요**
- **new/delete 베이스라인**: 단일 스레드에선 glibc malloc도 이미 빠름. 멀티스레드 락 경합/단편화/커널 호출 회피 같은 풀의 **진짜 이점은 마이크로벤치로 측정 불가**
- **모든 버전 동일 출력 포맷** (`BM_Pool_*_vNN<Cap>`): 버전별/사이즈별 대조 즉시 가능
- **일관성 원칙**: 버전당 변경점 하나 — v01→v02 (Handle), v02→v03 (Node), v03→v04 (API), v04→v05 (청크+가변). 원인/결과 분리

## 미해결 / 다음 단계

- **Multi-in-flight 벤치 미작성**: 여러 Handle 동시 보유 + 교체 패턴에서 cache locality 측정 기회
- **멀티스레드**: 현재 모든 버전 단일 스레드. `std::atomic` + memory order로 SPSC 풀은 별도 프로젝트(`cpp-spsc-queue`)에서
- **intrusive union 실험**: `concept/placement-new/` 같은 짧은 예제로 분리 학습 예정
- **memory-pool backing**: 청크 할당 자체를 arena/memory-pool에서 받아 누수 구조적 불가능하게 — `cpp-memory-pool` 프로젝트 선행 필요
- **v06 (미래)**: memory-pool 기반 재구성. 두 프로젝트 교차 검증

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test

# 버전별 벤치
./project/object-pool/script/run_bench_v01.sh
./project/object-pool/script/run_bench_v02.sh
./project/object-pool/script/run_bench_v03.sh
./project/object-pool/script/run_bench_v04.sh
./project/object-pool/script/run_bench_v05.sh
```
