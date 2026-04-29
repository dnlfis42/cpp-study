# object-pool

`new`/`delete` 반복 비용을 제거하는 고정/가변 크기 객체 풀. free list 자료구조 진화와 RAII Handle로 성능과 자원 안전성을 동시에 확보한다.

- **Handle (RAII)**: `unique_ptr<T, Deleter>`로 release 누락 구조적 차단
- **free list 진화**: vector 스택 -> 인덱스 링크드 리스트 -> 포인터 연결로 hot path 최적화 탐색
- **청크 기반 가변 크기**: grow 중에도 기존 포인터 안정성 유지, capacity 예측 불필요
- **T 생명주기 제어**: placement new + explicit dtor, non-default-constructible T 지원

## 성능 종합

hot loop, `char buf[64]`, `chunk_size = 64`, i7-9750H, Release:

| 조건       |      mean |      CV | 비고                  |
| :--------- | --------: | ------: | :-------------------- |
| new/delete | `20.0 ns` | `1.50%` | 베이스라인            |
| v01 Raw    | `2.35 ns` | `0.40%` | 기준                  |
| v02 Raw    | `2.44 ns` | `0.11%` | 구현 동일             |
| v02 Handle | `4.16 ns` | `0.19%` | Deleter +1.7 ns       |
| v03 Handle | `5.53 ns` | `0.23%` | Node 구조 +33%        |
| v04 Handle | `4.50 ns` | `0.02%` | 포인터 free list -19% |
| v05 Handle | `4.67 ns` | `0.18%` | mempool 위임 +4%      |

## 버전

### v01

**개선**: vector 스택 free list + LIFO로 new/delete 대비 **8.5× 빠름** (`2.35 ns` vs `20.0 ns`).

**과제**:

- Raw API release 누락을 `[[nodiscard]]`로 경고하지만 컴파일 타임 차단은 아님

### v02

**개선**: `unique_ptr<T, Deleter>` Handle로 release 누락 구조적 차단. 오버헤드 `+1.7 ns`. new/delete 대비 Handle도 **4.8× 빠름** (`4.16 ns`).

**트레이드오프**: `+1.7 ns` (+69%). `sizeof(Handle)` = `16 B`.

**과제**:

- `release(T*)` 내 `reinterpret_cast<Node*>`: pointer-interconvertible 조건 위반
- Raw API 병행 제공으로 release 누락 경로 여전히 존재

### v03

**개선**: Deleter에 `Node*` 직접 저장, `reinterpret_cast` 제거. Raw API 제거. Handle이 유일한 공개 경로. `acquire_unique()` -> `acquire()`.

**트레이드오프**: `sizeof(Handle)` `16 B` -> `24 B`. `+1.37 ns` (+33% vs v02).

**과제**:

- 인덱스 free list: `base + idx * sizeof(Node)` 곱셈 비용
- T DefaultConstructible 요구
- 고정 크기

### v04

**개선**: `Node* next` 포인터 free list로 곱셈 제거. v03 대비 `-1.03 ns` (-19%). 청크 기반 grow로 capacity 예측 불필요. 기존 포인터 안정성 유지.

**트레이드오프**: T DefaultConstructible 요구 (`grow()` 시 `Node[]` default-construct). `acquire()` `noexcept` 제거.

**과제**:

- non-default-constructible T 지원 불가
- `grow()` 시 미사용 슬롯도 T 일괄 생성. 소멸자 비호출로 생명주기 직접 제어 불가

### v05

**개선**: placement new + explicit dtor로 T 생명주기 직접 제어. Non-default-constructible T 지원. `sizeof(Handle)` `24 B` -> `16 B`. mempool 위임으로 free list 구현 제거.

**트레이드오프**: trivial T hot loop 기준 `+0.17 ns` (+4% vs v04). `sizeof(T)` <= `1024 B` 제한 (mempool v03).

**과제**:

- reset 함수 template non-type parameter 미구현: 소멸자 비호출 + 상태 초기화 결합
- 단일 스레드 전용
