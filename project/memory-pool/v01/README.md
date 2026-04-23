# memory-pool v01 — single-chunk arena

단일 청크 bump allocator. 개별 free 없음, reset/destroy로만 회수.

## 핵심 설계

- **arena (bump allocator)**: 포인터 한 개를 끝까지 밀어 할당. 개별 `deallocate` 없음.
- **단일 청크**: 생성 시점에 한 번만 메모리 확보. 부족 시 throw. 청크 리스트는 v02에서.
- **백엔드**: 내부 `mmap`. 외부 버퍼 지원은 필요 시점에 후속 버전으로.
- **단일 스레드**: 락 없음. 멀티스레드는 후속 버전.
- **정렬**: malloc 철학을 따름 — 기본 16B, 명시 정렬은 별도 오버로드.

### arena의 정체성

개별 free를 포기함으로써 얻는 것:

- bump 포인터 1개로 할당 = 수 ns
- 메타데이터 없음 (헤더, free list, size class 모두 없음)
- 단편화 없음 (할당 순서 = 메모리 순서)

대신 단명·일괄 폐기 패턴(파서 1회 실행, 요청 1건 처리 등)에서만 의미 있음. 장수명 객체에는 부적합.

## API

| 카테고리  | 시그니처                                 | 설명                                                                |
| --------- | ---------------------------------------- | ------------------------------------------------------------------- |
| ctor      | `MemoryPool(size_t capacity)`            | 내부 `mmap`으로 capacity 바이트 확보. 실패 시 `std::bad_alloc`.     |
| dtor      | `~MemoryPool()`                          | `munmap`으로 영역 반환.                                             |
| 복사      | `delete`                                 | 비복사.                                                             |
| 이동      | `MemoryPool(MemoryPool&&) noexcept` 외   | mmap 자원 소유권 이전. 원본은 빈 상태(buf=nullptr, capacity=0).     |
| 상태      | `size_t capacity() const noexcept`       | 전체 용량                                                           |
| 상태      | `size_t available() const noexcept`      | 남은 바이트 (`capacity - in_use`, 정렬 패딩 미반영)                 |
| 상태      | `size_t in_use() const noexcept`         | 현재까지 bump된 바이트 수                                           |
| 상태 변경 | `void* allocate(size_t n)`               | 기본 정렬(`alignof(std::max_align_t)`)로 위임. 부족 시 `bad_alloc`. |
| 상태 변경 | `void* allocate(size_t n, size_t align)` | 명시 정렬. align은 2의 거듭제곱. 부족 시 `bad_alloc`.               |
| 상태 변경 | `void reset() noexcept`                  | bump 포인터를 시작 위치로 되돌림. 메모리 OS 반환은 없음.            |

`allocate(n)`은 `allocate(n, alignof(std::max_align_t))`로 위임.

### 비범위 (v01에서 안 함)

- 개별 free / deallocate
- 청크 리스트 (단일 청크만)
- 멀티스레드 (락 / atomic)
- 템플릿 `T* allocate<T>(count)` 편의 API
- aligned-storage 위에서 객체 construct (사용자가 placement new로 직접)
- 통계/디버깅 (high water mark, leak 검출 등)

## 벤치마크

환경: 3.0 GHz 고정, `taskset -c 2`, 10 repetitions + aggregates_only, CV < 0.25%.

### 수치

| 벤치                                | 평균                | 처리량  | 비고                 |
| ----------------------------------- | ------------------- | ------- | -------------------- |
| `BM_Pool_Allocate_Single`           | 1.34 ns             | —       | bump 1회 (기본 정렬) |
| `BM_Pool_Allocate_Aligned64`        | 1.34 ns             | —       | bump 1회 (64B 정렬)  |
| `BM_Pool_AllocateReset_Batch/64`    | 86.3 ns / 64회      | 742 M/s | item당 1.35 ns       |
| `BM_Pool_AllocateReset_Batch/1024`  | 1383 ns / 1024회    | 740 M/s | item당 1.35 ns       |
| `BM_Pool_AllocateReset_Batch/16384` | 22002 ns / 16384회  | 745 M/s | item당 1.34 ns       |
| `BM_Malloc_Free_Batch/64`           | 1414 ns / 64회      | 45 M/s  | item당 22 ns         |
| `BM_Malloc_Free_Batch/1024`         | 23011 ns / 1024회   | 45 M/s  | item당 22 ns         |
| `BM_Malloc_Free_Batch/16384`        | 380534 ns / 16384회 | 43 M/s  | item당 23 ns         |
| `BM_Pool_BumpWrite1_Single`         | 2.03 ns             | —       | bump + 1B write      |
| `BM_Pool_BumpWrite64_Single`        | 2.14 ns             | —       | bump + 64B memset    |
| `BM_PopPool_BumpWrite1_Single`      | 2.03 ns             | —       | MAP_POPULATE + 1B    |

**arena vs malloc — 약 17배 차이** (모든 batch 크기에서 일관).

### 원인 분해

**1. bump 1회 = ~4 cycles**

1.34 ns @ 3 GHz ≈ 4 사이클. 정렬 마스킹(`(pos + align - 1) & ~(align - 1)`) 1회 + capacity 비교 + add + 포인터 반환. 컴파일러가 거의 `pos += n` 한 줄 수준으로 인라인.

**2. malloc 22 ns/op의 정체**

glibc tcache hit 경로인데도 22 ns. free list 포인터 추적 + 스레드/arena 디스패치 + 블록 헤더 갱신 등이 누적. bump 한 줄과 비교하면 **17개 명령어만큼 무겁다**. 시스템 콜은 없지만 분기/메타데이터 처리가 본질적 비용.

**3. 정렬 오버헤드 0**

기본 정렬(`max_align_t` = 16B) vs 명시 정렬(64B) 동일하게 1.34 ns. 비트마스킹 트릭은 1 명령어이므로 측정 가능한 오버헤드 없음. **명시 정렬 API를 디폴트로 노출해도 비용 zero**.

**4. arena는 batch 크기에 완벽 선형**

64, 1024, 16384 모두 item당 1.34~1.35 ns로 일정. 1 MiB 풀이 L2(256 KiB)는 초과하지만 sequential write라 hardware prefetcher가 다음 cache line을 미리 가져옴 — 캐시 미스 비용이 가려짐.

**5. malloc은 약하게 비선형 (45 → 43 M/s)**

batch 16384 × 64B = 1 MiB 작업셋 → L1(32 KiB), L2(256 KiB) 모두 초과. malloc은 free list 추적이 비국소(non-sequential) 접근이라 prefetcher가 도움이 안 됨. arena와 가장 큰 구조적 차이.

**6. lazy commit은 hot loop에 보이지 않는다**

`MAP_POPULATE` 플래그로 ctor에서 모든 페이지를 미리 fault 처리한 변형(`MemoryPoolPopulate`)과 일반 풀의 hot loop 비용이 **완전히 동일** (둘 다 2.03 ns/op for bump + 1B write). 원인은 amortization: 1 MiB 풀 = 256 page faults × ~5 µs ≈ 1.28 ms 가 약 600 ms 런타임에 분산되어 0.002 ns/op로 측정 한계 아래.

**실무적 함의**: 풀이 장수명(서버 시작 후 계속)이면 lazy commit이 사실상 무료. 풀이 단명(요청마다 생성/파괴)이면 MAP_POPULATE로 fault를 ctor로 몰아 hot path를 깨끗하게 유지. v01의 단일 청크 패턴에서는 **lazy가 정답**이고, MAP_POPULATE는 "비용을 ctor로 이동"할 뿐 총 비용은 동일.

**7. store buffer + write combining의 위력**

bump-only 1.34 ns → bump + 1B write 2.03 ns (+0.69 ns) → bump + 64B memset 2.14 ns (+0.80 ns). **1B vs 64B write 차이가 0.11 ns뿐**. CPU의 store buffer가 단일 store를 거의 공짜로 흡수하고, cache line 채우기는 write combining으로 흡수.

→ **메모리 풀에서 새 객체 zero-fill이 사실상 공짜**. 64B 객체를 0으로 초기화하는 비용이 1B 쓰는 것과 거의 같다는 뜻. 보안/안전성 차원의 zero-init을 망설일 이유 없음.

### 관찰

- **CV 모두 0.25% 이하** — 환경 잡음 거의 없음. 3.0 GHz 고정 + `taskset -c 2`의 효과.
- **stddev ~1 ns 수준** — 단일 cycle 단위 변동까지 잡힘.
- arena의 강점은 "한 줄로 줄어든 hot path"가 아니라 **"sequential write로 prefetcher와 협력하는 메모리 패턴"** 이라는 점이 더 본질적.

### 비교 한계

- 본 벤치는 **단일 스레드, 단일 청크 가득 차지 않는 시나리오**. 실제 워크로드(멀티스레드 + 잦은 grow + free 패턴 다양) 에서는 비율이 달라질 수 있음.
- malloc은 ptmalloc2 (glibc 기본). jemalloc/tcmalloc은 다를 수 있음.

## 다음 버전 힌트 (v02)

- **청크 리스트**: 부족 시 새 청크 mmap, linked list로 관리. 청크 크기 정책 (고정 / geometric / 사용자 지정).
- 또는 **slab 분기**로 v02를 잡고 v03에서 청크 리스트 — 결정은 v01 마치고.
