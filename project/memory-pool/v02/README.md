# memory-pool v02 — multi-chunk arena (small + large)

단일 청크 → 청크 리스트. small/large 이원화로 임의 크기 요청 수용. arena 정체성(개별 free 없음) 유지.

## 이전 버전 대비 변화

```diff
- explicit MemoryPool(size_t capacity);          // 단일 청크
+ explicit MemoryPool(size_t chunk_size);        // 청크 단위 크기
+ size_t large_threshold() const noexcept;       // = chunk_size / 2
- size_t capacity() / available() / in_use();
+ size_t total_capacity() / total_in_use();
- // 단일 std::byte* buf_, size_t pos_
+ std::vector<Chunk> small_chunks_;              // 청크 리스트
+ std::vector<Chunk> large_chunks_;              // dedicated 큰 요청
+ std::size_t current_;                          // 현재 small 청크 인덱스
```

- **단일 청크 → 청크 리스트**: 부족 시 자동으로 새 청크 mmap. throw 없음.
- **small/large 이원화**: `n > chunk_size/2`면 dedicated 청크에 그 요청만 단독 배치.
- **포인터 → 인덱스**: `current_`를 raw pointer 대신 `size_t`로. vector reallocation/move에 안전.
- **eager ctor**: v01과 동일. 첫 small 청크는 ctor에서 즉시 mmap.

## 핵심 설계

### small/large 이원화

`large_threshold = chunk_size / 2`로 묶어둠. 이 결정의 핵심은 "55% 같은 chunk_size 절반 초과 요청이 연속 들어와도 small 청크의 절반 이상 낭비를 차단". 사용자가 노출 받는 파라미터는 `chunk_size` 하나.

- `n ≤ threshold` → small 경로: current 청크에 bump. 안 들어가면 다음 청크 (보존된 것 또는 새로 mmap).
- `n > threshold` → large 경로: 그 요청만을 위한 청크를 페이지 정렬 크기로 mmap.

### reset 정책

- **small 청크**: 모두 보존. `pos = 0`으로 초기화, `current_ = 0`. 다음 사이클은 첫 청크부터 재bump.
- **large 청크**: 전부 munmap. 매 사이클 fresh mmap.

이유: arena의 전형적 사용 = "요청 처리 → reset → 다음 요청 처리". 작은 객체는 사이클 간 패턴이 비슷 → 청크 보존이 효율적. 큰 객체는 매번 다른 크기일 가능성이 높고 보유 비용이 큼 → 즉시 반환.

→ **small은 정상 경로, large는 예외 경로**. 분기 위치를 명확히.

### tail abandonment

current 청크에 fit 안 하면 그냥 다음 청크로 이동. 이전 청크 tail은 사장. fit-search/first-fit 같은 검색 로직 없음 — arena의 "분기 없는 hot path" 유지.

낭비 상한: 청크당 chunk_size, 청크 수에 비례. `large_threshold = chunk_size/2` 결정이 이 낭비를 chunk_size의 50% 미만으로 보장 (small 요청은 정의상 chunk_size/2 이하).

### 인덱스 vs 포인터 (current\_)

`Chunk*` 대신 `size_t current_`를 사용. 이유:

- vector 재할당 시 raw pointer는 dangling, 인덱스는 안전
- move ctor/assignment에서 별도 재계산 불필요
- 비용: vector data ptr 로드 + offset 계산 = 1-2 cycle 추가 (벤치 결과 반영)

## API

| 카테고리  | 시그니처                                  | 설명                                                    |
| --------- | ----------------------------------------- | ------------------------------------------------------- |
| ctor      | `MemoryPool(size_t chunk_size)`           | 첫 small 청크 즉시 mmap. 실패 시 `std::bad_alloc`.      |
| dtor      | `~MemoryPool()`                           | 모든 small + large 청크 munmap.                         |
| 복사      | `delete`                                  | 비복사.                                                 |
| 이동      | `MemoryPool(MemoryPool&&) noexcept` 외    | 모든 청크 소유권 + current 인덱스 이전. 원본은 빈 상태. |
| 상태      | `size_t chunk_size() const noexcept`      | ctor 인자 그대로                                        |
| 상태      | `size_t large_threshold() const noexcept` | `n > 이 값`이면 large 분류. `chunk_size / 2`.           |
| 상태      | `size_t total_in_use() const noexcept`    | 전 청크의 pos 합. **O(청크 수)**.                       |
| 상태      | `size_t total_capacity() const noexcept`  | 전 청크의 capacity 합. **O(청크 수)**.                  |
| 상태 변경 | `void* allocate(size_t n)`                | 기본 정렬(`max_align_t`)로 위임.                        |
| 상태 변경 | `void* allocate(size_t n, size_t align)`  | 명시 정렬. **align ≤ 4096 가정**. 2의 거듭제곱.         |
| 상태 변경 | `void reset() noexcept`                   | small 청크 보존, large 청크 munmap, current=0.          |

### 비범위 (v02에서 안 함)

- 개별 free / deallocate (arena 정체성)
- 멀티스레드 (락 / atomic)
- align > 4096 (huge alignment) — 후속 버전 검토
- 청크 크기 동적 조정 (geometric growth 등)
- 메모리 압박 시 small 청크 일부 반환 (peak shrink)

## 벤치마크

환경: 3.0 GHz 고정, `taskset -c 2`, 10 repetitions + aggregates_only.

### 수치

| 벤치                                         | 평균             | 처리량   | 비고                           |
| -------------------------------------------- | ---------------- | -------- | ------------------------------ |
| `BM_PoolV02_Allocate_Single`                 | 4.42 ns          | —        | total_in_use 호출 포함 (함정)  |
| `BM_PoolV02_AllocateReset_Batch/64`          | 155 ns / 64      | 412 M/s  | item당 2.42 ns                 |
| `BM_PoolV02_AllocateReset_Batch/1024`        | 2494 ns / 1024   | 411 M/s  | item당 2.43 ns                 |
| `BM_PoolV02_AllocateReset_Batch/16384`       | 39159 ns / 16384 | 418 M/s  | item당 2.39 ns                 |
| `BM_Malloc_Free_Batch/64`                    | 1417 ns / 64     | 45 M/s   | item당 22 ns                   |
| `BM_Malloc_Free_Batch/1024`                  | 23082 ns / 1024  | 44 M/s   | item당 22.5 ns                 |
| `BM_Malloc_Free_Batch/16384`                 | 380986 ns        | 43 M/s   | item당 23.3 ns                 |
| `BM_PoolV02_AllocateReset_GrowsChunks/64`    | 155 ns / 64      | 412 M/s  | item당 2.42 ns (Batch와 동일)  |
| `BM_PoolV02_AllocateReset_GrowsChunks/1024`  | 2490 ns / 1024   | 411 M/s  | item당 2.43 ns                 |
| `BM_PoolV02_AllocateReset_GrowsChunks/16384` | 39754 ns         | 412 M/s  | item당 2.43 ns                 |
| `BM_PoolV02_LargeAllocateReset`              | 4303 ns          | —        | 96 KiB mmap+munmap 1쌍         |
| `BM_Malloc_Large` (96 KiB)                   | 37.3 ns          | —        | arena tcache hit (M_MMAP 미만) |
| `BM_PoolV02_MixedReset/64`                   | 4459 ns          | 14.6 M/s | 64 small + 1 large + reset     |
| `BM_PoolV02_MixedReset/1024`                 | 6754 ns          | 152 M/s  | 1024 small + 1 large + reset   |

**arena (v02) vs malloc (small) — 약 9.4배 차이** (v01의 17배에서 절반 가까이 줄어듦).

### 원인 분해

**1. small bump: v01 1.34 ns → v02 2.4 ns (1.8×)**

추가 비용 분석 (3GHz @ ≈3-4 cycle):

- threshold 비교 `n > (chunk_size_ >> 1)` — 1 cmp + branch
- vector 인덱싱 `small_chunks_[current_]` — data ptr 로드 + offset 계산 + dereference
- 그 외 분기/포인터 처리

**유연성("청크 자동 성장" + "임의 크기 수용")의 가격이 ~1 ns/op**. 단순 단일 청크 v01의 hot path 단순함을 정확히 그만큼 잃음.

**2. Single 4.42 ns는 측정 함정**

`if (pool.total_in_use() + kAllocSize > kChunkSize) reset();`에서 `total_in_use()`가 vector 순회(O(청크 수)). v01의 `available()`은 O(1) 멤버 접근이라 더 가벼움. **공정 비교는 batch 쪽** (item당 2.4 ns).

→ v02는 사용자에게 "내 풀이 얼마나 찼나?" 묻는 cheap API를 제공하지 않음. 사용자가 풀 잔량 모니터링이 필요한 시나리오라면 별도 카운터를 외부에서 관리하는 게 옳음.

**3. 청크 자동 성장은 정상 상태 hot loop에 무료**

`AllocateReset_Batch` (단일 청크) vs `AllocateReset_GrowsChunks` (chunk_size=4096, batch마다 ~16-256개 청크 사용) 결과가 동일 (2.42 vs 2.43 ns/item, 모든 batch 크기에서).

이유: reset이 small 청크를 보존하므로 mmap은 첫 사이클에만 발생. 이후엔 `++current_`로 다음 청크 인덱스 증가 + `small_chunks_[current_].buf` 반환만. 청크 경계 비용은 평균화되어 측정 한계 이하.

→ **"청크 리스트로 무한 성장" 정책의 hot path 패널티 0**. peak 메모리가 한 번 잡히면 정상 상태는 v01과 동등 (인덱싱 오버헤드만).

**4. Large path: 4.3 µs (mmap+munmap pair)**

96 KiB 청크 1쌍 = ~4.3 µs. 페이지 fault, syscall, kernel VMA 처리가 포함된 정직한 비용.

malloc 동일 크기는 37 ns로 **115배 빠름**. 이유:

- glibc `M_MMAP_THRESHOLD` 기본값 128 KiB → 96 KiB는 arena 사용
- 반복 alloc/free 패턴은 free list/tcache로 즉시 재사용 → syscall 0회

→ v02 large path는 **재사용 불가능한 일회성 큰 버퍼** 시나리오용. 같은 크기를 자주 alloc/free한다면 풀의 가치가 없음 — 차라리 malloc이 낫다. 이게 "large는 예외 경로답게" 결정의 정직한 트레이드오프.

**5. Mixed의 산수적 분해**

- batch 64 + large 1: 4459 ns ≈ 4300 (large) + 64 × 2.4 (small) = 4454 ✓
- batch 1024 + large 1: 6754 ns ≈ 4300 + 1024 × 2.4 = 6758 ✓

→ Mixed에서 large 1개 ≈ small 1700개의 비용. **small 다수가 large 비용을 amortize**해야 large path가 의미. 실전 워크로드 (요청당 큰 버퍼 1 + 작은 객체 다수)에 잘 맞는 구조.

### 관찰

- v02 batch CV는 64에서 0.10%, 16384에서 0.04%로 안정적. 단 1024에서 4.57% 튀었음 — 한 번의 outlier 가능 (median 2458, mean 2494). 측정 추가 필요 시 재실행.
- arena vs malloc 비율이 v01의 17×에서 v02의 9.4×로 줄었지만, 여전히 한 자릿수 µs 단위 응답에서 **~10배는 의미 있는 차이**.
- v02의 small path는 v01의 **이론적 최저 비용에서 ~1ns 멀어진 가격으로 "임의 크기 + 무한 성장"을 산다**. 그 가치가 있는지는 워크로드가 결정.

## 다음 버전 힌트

- **slab 분기 (v03 후보)**: 동일 크기 객체 N개를 size class별 free list로 — 개별 free 도입. arena와 다른 트랙.
- **chunk 크기 동적 정책**: geometric growth (×2) — peak 모르는 워크로드에서 청크 수 log 억제.
- **align > 4096 지원**: large 청크에서 user align ≥ huge page 정렬 처리.
- **멀티스레드**: per-thread current\_ 또는 thread-local 풀.
- **shrink-to-fit**: peak 후 미사용 small 청크 일부 반환.
