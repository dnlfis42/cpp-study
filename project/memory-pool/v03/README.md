# memory-pool v03 — slab allocator (size class + intrusive free list)

arena 트랙에서 분기. 개별 free 도입, size class로 임의 크기를 버킷으로 압축.

## 이전 버전 대비 변화

```diff
- // arena: 개별 free 없음, reset/destroy만
+ // slab: size class별 free list, 개별 free 지원

- explicit MemoryPool(size_t capacity_or_chunk_size);
+ MemoryPool() noexcept;  // 인자 없음, 고정 kSlabSize=64KiB

- void* allocate(size_t n);                     // 임의 크기
+ void* allocate(size_t n);                     // 2의 거듭제곱으로 올림 (16~1024)
+ void deallocate(void* p, size_t n) noexcept;  // 신규

- std::byte* buf_ / std::vector<Chunk>          // arena 메타
+ std::array<SizeClass, 7> classes_             // 7개 독립 풀
+ struct SizeClass { slot_size, free_head, slabs }
+ intrusive free list (free 슬롯 첫 8B에 next 포인터)
```

- **arena → slab 패러다임 전환**: 개별 free 도입 (arena 정체성 포기)
- **size class 7개**: 16, 32, 64, 128, 256, 512, 1024 (2의 거듭제곱)
- **intrusive free list**: free 슬롯 메모리 자체가 리스트 노드. 메타 0.
- **deallocate 시 n 필수**: 사용자가 alloc 시 요청한 n 그대로 넘겨야 함 (size class 동일하게 매핑)
- **align 인자 미지원**: 슬롯 자체 정렬(2의 거듭제곱 size = 같은 크기 정렬)만 보장

## 핵심 설계

### slab의 정체성

`arena`가 "개별 free 포기 → 단순/빠름"이었다면, `slab`은 "같은 크기로 제한 → 개별 free를 LIFO로 즉시 재활용":

- **free는 list에 push** (1 store). **alloc은 list head pop** (1 load + 1 store).
- 메타데이터 0. 블록 헤더/size tag 없음.
- 단편화 0. 모든 슬롯이 size class 고정이라 never merge, never split.

### size class 라우팅

```cpp
static constexpr std::size_t size_class_of(std::size_t n) noexcept {
    return n <= kMinSize ? 0 : std::bit_width(n - 1) - 4;
}
// 16→0, 17→1, 32→1, 33→2, 1024→6
```

`std::bit_width` (C++20) = `floor(log2(x)) + 1`. 단일 LZCNT/BSR 명령어. 16이 최소라 `- 4` = `log2(16)` 빼서 인덱스 0부터.

### intrusive free list — 핵심 트릭

free 슬롯의 **첫 8바이트**를 "다음 free 슬롯 포인터"로 사용:

```
slab 64KiB (64B 슬롯 클래스):
[slot0]→[slot1]→[slot2]→...→[slot1023]→nullptr
 (slot0의 첫 8B가 slot1의 주소 저장)

free_head = slot0
```

alloc:

```cpp
void* slot = free_head;
free_head = *reinterpret_cast<void**>(slot);  // slot의 첫 8B 로드
return slot;
```

dealloc:

```cpp
*reinterpret_cast<void**>(p) = free_head;     // p의 첫 8B에 옛 head 저장
free_head = p;
```

**슬롯 ≥ 8B 필수** (포인터 담을 공간). kMinSize=16이라 충족.

### reset은 munmap 전부

arena의 reset은 "slab 보유, bump 위치만 복귀"였는데 slab에선 의미 달라짐. 초기 spec에서는 "모든 객체 free 상태로 slab 보유"를 고민했지만, per-slab free count 없이는 구현 복잡도↑. 단순화: **reset = 전부 munmap + free_head = nullptr**. dtor와 의미 동일, 단 풀 객체는 살아남음.

→ **v03에서 `shrink_to_fit` 없음**. reset이 해당 기능 흡수.

### 비범위 (v03에서 안 함)

- 임의 크기 / align 인자 (size class 고정)
- ptr만으로 slab 식별 (deallocate에 n 필수) — v04 이후 alignment trick 카드
- 개별 slab 해제 (per-slab free count 없음)
- 멀티스레드 (락 / atomic / thread-local)

## API

| 카테고리  | 시그니처                                           | 설명                                                       |
| --------- | -------------------------------------------------- | ---------------------------------------------------------- |
| ctor      | `MemoryPool() noexcept`                            | lazy. 첫 alloc 때 해당 size class slab mmap.               |
| dtor      | `~MemoryPool()`                                    | 모든 slab munmap.                                          |
| 이동      | `MemoryPool(MemoryPool&&) noexcept` 외             | classes\_ 이전, other의 free_head nullify.                 |
| 상태      | `std::size_t total_capacity() const noexcept`      | 모든 slab capacity 합. O(size class 수).                   |
| 상태 변경 | `void* allocate(std::size_t n)`                    | n>1024 → throw. 가장 가까운 2^k로 올림. mmap 실패 → throw. |
| 상태 변경 | `void deallocate(void* p, std::size_t n) noexcept` | assert(n ∈ (0, 1024]) + assert(p slot_size 정렬).          |
| 상태 변경 | `void reset() noexcept`                            | 모든 slab munmap + free_head nullify.                      |

**정적 상수**

- `kMinSize = 16`, `kMaxSize = 1024`, `kSlabSize = 65536` (64 KiB), `kClassCount = 7`

## 벤치마크

환경: 3.0 GHz 고정, `taskset -c 2`, 10 repetitions + aggregates_only.

### 수치

| 벤치                                       | 평균      | 처리량   | 비고                                 |
| ------------------------------------------ | --------- | -------- | ------------------------------------ |
| `BM_PoolV03_AllocDealloc_Pair/16`          | 2.66 ns   | —        | size 무관, CV 0.03%                  |
| `BM_PoolV03_AllocDealloc_Pair/64`          | 2.66 ns   | —        |                                      |
| `BM_PoolV03_AllocDealloc_Pair/1024`        | 2.66 ns   | —        |                                      |
| `BM_Malloc_Free_Pair/16`                   | 15.9 ns   | —        | tcache hit 경로                      |
| `BM_Malloc_Free_Pair/64`                   | 15.9 ns   | —        |                                      |
| `BM_Malloc_Free_Pair/1024`                 | 15.9 ns   | —        |                                      |
| `BM_PoolV03_AllocBatch_DeallocBatch/64`    | 216 ns    | 591 M/s  | item당 1.69 ns                       |
| `BM_PoolV03_AllocBatch_DeallocBatch/1024`  | 5300 ns   | 386 M/s  | item당 2.59 ns                       |
| `BM_PoolV03_AllocBatch_DeallocBatch/16384` | 126046 ns | 260 M/s  | item당 3.85 ns                       |
| `BM_Malloc_Free_Batch/64`                  | 1409 ns   | 91 M/s   | item당 11 ns                         |
| `BM_Malloc_Free_Batch/1024`                | 22965 ns  | 89 M/s   | item당 11.2 ns                       |
| `BM_Malloc_Free_Batch/16384`               | 389855 ns | 84 M/s   | item당 11.9 ns (CV 7.5% — outlier)   |
| `BM_PoolV03_AllocateReset_Batch/64`        | 42306 ns  | 1.5 M/s  | item당 661 ns (mmap+init amortize X) |
| `BM_PoolV03_AllocateReset_Batch/1024`      | 47311 ns  | 21.7 M/s | item당 46 ns                         |
| `BM_PoolV03_AllocateReset_Batch/16384`     | 754530 ns | 21.7 M/s | item당 46 ns                         |

**v03 pair vs malloc pair — 약 6배 차이** (LIFO sweet spot).
**v03 batch vs malloc batch — 약 4~6배** (batch 커질수록 v03 우위 약화).

### 원인 분해

**1. alloc/dealloc pair 2.66 ns — size에 무관**

3가지 size (16/64/1024) 모두 2.66 ns/pair 완전 동일. 의미:

- `size_class_of` + `classes_[idx]` + free list pop/push의 **총 비용이 size에 의존 X**
- branch predictor 100% 적중, cache 100% hit (같은 슬롯 반복)
- alloc 1회 ≈ dealloc 1회 ≈ **1.33 ns** — **v01 bump 1.34 ns와 동등**

→ **free list pop/push 한 번이 bump 한 번과 같은 비용**. "개별 free는 느리다"는 편견을 깸. 단, 아래 #3 참고.

**2. malloc pair 15.9 ns — tcache도 이만큼 나옴**

glibc tcache가 가장 빠른 경로인데도 v03 대비 6배. 이유:

- chunk header 갱신 (prev_size, size+flags)
- 스레드/arena 디스패치
- malloc 호환성 분기 (small/large/huge 판단)
- metadata 위치가 user data와 떨어져 있어 cache miss 유발

→ "tcache hit 경로"도 v03의 단순함에 비하면 6× 비용. **단순함 자체가 성능**.

**3. batch 패턴에서 단위 비용 증가 (1.69 → 3.85 ns)**

pair 1.33 ns → batch 16384 3.85 ns. 거의 3배. 이유:

- **free list가 free 순서대로 꼬임**. alloc 시 ptr이 메모리에서 zigzag → hardware prefetcher 무력화
- batch 16384 × 64B = 1MB 작업셋. L2(256KB) 초과 → cache miss
- arena(v01/v02)의 "sequential write" 우위를 잃음

→ **slab의 아킬레스건**. LIFO pair에선 cache 100% hit이지만 free list가 길어지면 비국소 접근. arena가 prefetcher 협력으로 batch 커져도 변동 없던 것과 대비.

**4. AllocateReset 패턴에서 slab은 최악 (arena 대비 20×)**

v01/v02 batch = ~2 ns/item. v03 batch 16384 = 46 ns/item. 매 사이클 munmap + mmap + slab init(1024 슬롯 포인터 write) 비용.

- 작은 batch(64): 661 ns/item (amortize 안 됨, 대부분 mmap/init 비용)
- 큰 batch(16384): 46 ns/item (mmap/init 비용이 amortize되지만 여전히 arena의 20배)

→ **"slab으로 arena 패턴을 따라 할 수는 없다"**. 패러다임이 다름. reset 자주 쓰는 워크로드면 v01/v02가 옳음.

**5. v01 bump 1.34 ns = v03 alloc 1.33 ns — 같은 본질**

두 숫자가 우연이 아님. 둘 다:

- 포인터 한 번 로드/저장
- 분기 예측 100% 적중
- cache hot

arena의 "bump"와 slab의 "free list pop"이 hot path에서 **같은 물리적 일**을 함. 진짜 다른 건 **어떤 메모리 접근 패턴을 만드는가** — arena는 sequential, slab은 LIFO 재활용.

### 관찰

- v03 pair CV 0.03% — 측정 안정성 극한. 같은 슬롯만 들락거리므로 환경 잡음이 들어갈 여지 0.
- batch 16384에서 malloc CV 7.5% 튀었음 — outlier 가능. 재측정 시 안정 예상.
- size class 분기 오버헤드가 측정 한계 이하 — `bit_width` + 배열 인덱스가 그만큼 가볍다.

## 다음 버전 힌트

- **v04 후보 — ptr → slab O(1) 룩업**: slab-aligned mmap (over-allocate + trim) 으로 `p & ~(kSlabSize-1)` = slab 시작. `deallocate(void* p)` (size 없이) 가능.
- **멀티스레드**: per-thread cache + size class 락. glibc tcache의 구조 직접 구현.
- **per-slab free count 부활**: 부분 shrink (빈 slab만 해제) 가능. ptr→slab 룩업과 묶임.
- **size class 정책 다양화**: 16B 간격 (glibc tcache 스타일) — 메모리 효율 향상, 버킷 수 증가.
- **align 지원**: slot_size > align이면 자연스레 지원. slot_size < align 요구는 별도 handling 필요.
