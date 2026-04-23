# memory-pool

raw 바이트 단위 메모리 풀. arena → 청크 리스트 → slab → 멀티스레드 순으로 진화.

- **arena (bump allocator)**: 개별 free 없음, reset/destroy로만 회수. 단명·일괄 폐기 워크로드에 압도적
- **mmap 백엔드**: 페이지 정렬, lazy commit, page fault는 OS가 처리
- **정렬 마스킹**: `(pos + align - 1) & ~(align - 1)` — 명시 정렬 비용 zero
- **RAII 자원 클래스**: mmap raw 자원에 대해 수동 move + dtor 가드 패턴
- **청크 리스트 (v02~)**: 부족 시 자동 성장. small/large 이원화로 임의 크기 수용
- **slab + intrusive free list (v03~)**: size class별 LIFO 재활용. 개별 free 도입

**학습 목표**:

- malloc/glibc 내부 (brk vs mmap, arena, tcache)와의 비교 감각
- arena의 정체성: "개별 free를 포기하면 얼마나 단순/빨라지는가"
- raw 자원 RAII (`std::byte*` + `munmap`)의 수동 move 구현
- 정렬 마스킹 트릭과 alignment 트레이드오프
- sequential write가 hardware prefetcher와 협력하는 방식 (실측)

이론 배경은 `concept/linux-memory/`.

## 버전 히스토리

|        버전 | 주요 변화                                                         | 결과                                                                                                            |
| ----------: | :---------------------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------- |
| [v01](v01/) | 단일 청크 arena (mmap 백엔드, bump + reset, 명시 정렬, 수동 move) | bump 1회 = 1.34 ns. malloc/free 페어 대비 ~17배. CV < 0.25%                                                     |
| [v02](v02/) | 청크 리스트 + small/large 이원화. 인덱스 기반 current. 자동 성장  | small bump 2.4 ns (v01 대비 +1ns). malloc 대비 ~9.4배. 청크 성장 정상상태 무료. large path = mmap+munmap 4.3 µs |
| [v03](v03/) | slab 분기 (size class 7개 + intrusive free list). 개별 free 도입  | alloc/dealloc pair 2.66 ns (size 무관). malloc pair 대비 6×. arena 패턴엔 부적합 (20×↓)                         |

## API 요약

### v01 (단일 청크)

| 카테고리  | 시그니처                                 | 설명                                                           |
| --------- | ---------------------------------------- | -------------------------------------------------------------- |
| ctor      | `MemoryPool(size_t capacity)`            | 내부 `mmap`. 실패 시 `std::bad_alloc`                          |
| dtor      | `~MemoryPool()`                          | `munmap` (move된 빈 객체는 no-op)                              |
| 이동      | `MemoryPool(MemoryPool&&) noexcept` 외   | mmap 자원 소유권 이전, 원본은 빈 상태(buf=nullptr, capacity=0) |
| 상태      | `capacity / available / in_use`          | `std::size_t`, noexcept                                        |
| 상태 변경 | `void* allocate(size_t n)`               | 기본 `alignof(std::max_align_t)` 정렬로 위임                   |
| 상태 변경 | `void* allocate(size_t n, size_t align)` | 명시 정렬 (2의 거듭제곱). 부족 시 `std::bad_alloc`             |
| 상태 변경 | `void reset() noexcept`                  | bump 포인터를 시작 위치로. OS 메모리 반환은 없음               |

### v02 (청크 리스트 + small/large)

| 카테고리  | 시그니처                                   | 설명                                                                   |
| --------- | ------------------------------------------ | ---------------------------------------------------------------------- |
| ctor      | `MemoryPool(size_t chunk_size)`            | 첫 small 청크 즉시 mmap. 실패 시 `std::bad_alloc`                      |
| dtor      | `~MemoryPool()`                            | 모든 small + large 청크 munmap                                         |
| 이동      | `MemoryPool(MemoryPool&&) noexcept` 외     | 모든 청크 + current 인덱스 이전                                        |
| 상태      | `chunk_size / large_threshold`             | 고정값 / `chunk_size / 2`                                              |
| 상태      | `total_in_use / total_capacity`            | 모든 청크 합. **O(청크 수)** — hot path 호출 비추                      |
| 상태 변경 | `void* allocate(size_t n[, size_t align])` | `n > threshold` → large dedicated 청크. 그 외 → small bump (자동 성장) |
| 상태 변경 | `void reset() noexcept`                    | small 보존, large 전부 munmap                                          |

### v03 (slab + intrusive free list)

| 카테고리  | 시그니처                                      | 설명                                               |
| --------- | --------------------------------------------- | -------------------------------------------------- |
| ctor      | `MemoryPool() noexcept`                       | lazy. 첫 alloc 시 size class별 64 KiB slab mmap    |
| dtor      | `~MemoryPool()`                               | 모든 slab munmap                                   |
| 이동      | `MemoryPool(MemoryPool&&) noexcept` 외        | classes\_ 이전, other free_head nullify            |
| 상태      | `total_capacity`                              | 모든 slab capacity 합. O(size class 수)            |
| 상태 변경 | `void* allocate(size_t n)`                    | `n > 1024` throw. 가장 가까운 2^k로 올림 (16~1024) |
| 상태 변경 | `void deallocate(void* p, size_t n) noexcept` | n 필수 (size class 동일 매핑). intrusive push      |
| 상태 변경 | `void reset() noexcept`                       | 모든 slab munmap (shrink_to_fit 흡수)              |

## 성능 종합

3.0 GHz 고정, `taskset -c 2`, 10 reps, item 크기 64B 기준.

### 단일/batch (small path)

| 시나리오              | v01 (ns/item) | v02 (ns/item) | malloc (ns/item) |      v02 vs malloc |
| --------------------- | ------------: | ------------: | ---------------: | -----------------: |
| 단일 allocate         |          1.34 |    4.42 (주1) |                — |                  — |
| batch 64              |          1.35 |          2.42 |               22 |           **9.1×** |
| batch 1024            |          1.35 |          2.43 |             22.5 |           **9.3×** |
| batch 16384           |          1.34 |          2.39 |             23.3 |           **9.7×** |
| 64B 명시 정렬         |          1.34 |             — |                — |       (오버헤드 0) |
| chunk 자동 성장 batch |             — |     2.42–2.43 |                — | (Batch와 동등 — 0) |

(주1) v02 단일 allocate는 `total_in_use()` (O(청크)) 호출 포함의 측정 함정. 공정 비교는 batch.

### large path (v02 only)

| 시나리오                | v02 (ns) | malloc (ns) |     비율 |
| ----------------------- | -------: | ----------: | -------: |
| 96 KiB allocate + reset |     4303 |        37.3 | **115×** |

malloc 96 KiB는 `M_MMAP_THRESHOLD`(128KiB) 미만 → arena 사용 + 반복 alloc/free 즉시 재사용. v02 large는 매번 정직한 mmap+munmap pair.

### v03 slab — 개별 free 패러다임

| 시나리오                             |    v03 (ns) | malloc (ns) |              비율 |
| ------------------------------------ | ----------: | ----------: | ----------------: |
| alloc/dealloc pair (LIFO, size 무관) |        2.66 |        15.9 |          **6.0×** |
| batch alloc/dealloc 64               | 1.69 (item) |          11 |          **6.5×** |
| batch alloc/dealloc 1024             | 2.59 (item) |        11.2 |          **4.3×** |
| batch alloc/dealloc 16384            | 3.85 (item) |        11.9 |          **3.1×** |
| **AllocateReset 16384** (arena 패턴) |   46 (item) |           — | v01 대비 **20×↓** |

**v03 alloc 1.33 ns ≈ v01 bump 1.34 ns** — slab의 free list pop이 arena의 bump와 같은 비용. 단, batch에서 free list 비순차 접근으로 단위 비용 증가 (1.7 → 3.85 ns/item).

## 전체 발견

### 1. bump 1회 = ~4 cycles

1.34 ns @ 3 GHz. 정렬 마스킹 + capacity 비교 + add + 포인터 반환. 컴파일러가 사실상 `pos += n` 한 줄 수준으로 인라인. arena의 hot path가 얼마나 vestigial인지 직접 확인.

### 2. malloc tcache hit도 22 ns

glibc tcache가 syscall 없이 처리하는 경로인데도 bump보다 17배 느림. free list 추적 + 스레드/arena 디스패치 + 블록 헤더 갱신 등 분기와 메타데이터 처리가 본질적 비용. **시스템 콜 유무가 전부가 아니다**.

### 3. 정렬 마스킹의 위력

`(p + align - 1) & ~(align - 1)`은 단 1 명령어. 16B vs 64B 정렬에서 측정 가능한 차이 없음. 명시 정렬 API를 디폴트로 노출해도 안전 — SIMD/cache-line 정렬을 자유롭게 강제할 수 있음.

### 4. arena의 진짜 강점은 메모리 패턴

arena는 batch 크기 64~16384에서 ns/op 변동 1% 이내. malloc은 같은 구간에서 45 → 43 M/s로 약하게 느려짐. 이유: arena는 **sequential write**라 hardware prefetcher가 다음 cache line을 미리 가져옴. malloc은 free list가 비국소 접근이라 prefetcher가 도움이 안 됨. **"코드가 짧다"보다 "메모리 접근 패턴이 협력적이다"가 더 본질적인 우위**.

### 5. raw 자원 RAII는 수동 move 필수

raw `std::byte*` + dtor에서 `munmap` 패턴은 **`= default` move 금지**. raw pointer는 trivially copyable이라 move=copy → 두 객체가 같은 영역 가리킴 → double munmap. 명시적 nullify + dtor의 `if (buf_)` 가드가 정석. 이 패턴은 fd, socket, mmap, GPU 핸들 등 raw 자원 래핑마다 반복 등장.

### 6. lazy commit은 hot loop에 보이지 않는다

`MAP_POPULATE`로 ctor에서 미리 fault 처리한 변형과 일반 풀의 hot loop 비용이 동일 (둘 다 2.03 ns/op). 1 MiB 풀의 256 페이지 fault (~1.28 ms)가 ~600 ms 런타임에 분산되어 0.002 ns/op로 측정 한계 아래. **장수명 풀에서 lazy commit은 사실상 무료**, 단명 풀에서만 MAP_POPULATE가 의미 있음.

### 7. store buffer + write combining의 위력

bump-only 1.34 ns vs bump+1B write 2.03 ns vs bump+64B memset 2.14 ns. **1B와 64B write 차이가 0.11 ns뿐**. CPU store buffer가 단일 store를 흡수하고, cache line 채우기는 write combining으로 처리. → **메모리 풀에서 새 객체 zero-fill이 사실상 공짜**, 보안/안전성 차원의 zero-init을 망설일 이유 없음.

### 8. unsigned underflow 가드

`n > capacity_ - aligned_pos` 체크에서 aligned*pos가 capacity*보다 클 경우 unsigned underflow → 거대한 값 → 통과. 정렬 올림이 capacity를 넘는 시나리오(`MemoryPool(8)` + `allocate(1, 64)`)에서 발생. **`aligned_pos > capacity_`를 먼저 체크**.

### 9. 유연성의 가격은 ~1 ns/op (v02)

v01 1.34 ns → v02 2.4 ns (batch). 추가된 일은:

- threshold 비교 1회
- vector 인덱싱 (`small_chunks_[current_]`) — data ptr 로드 + offset 계산

3-4 cycle ≈ 1 ns. **"임의 크기 + 자동 성장"의 정확한 비용**. 단일 청크 + 단일 멤버 접근의 v01에서 정확히 그만큼 단순함을 잃음. 워크로드가 v01의 단순 패턴이면 v02로 갈 이유 없음.

### 10. 청크 자동 성장은 정상 상태 hot loop에 무료 (v02)

reset이 small 청크 보존 정책 → mmap은 첫 사이클에만. 이후 `++current_`로 다음 청크 인덱스 증가 + buf 반환. 청크 경계 비용은 평균화되어 측정 한계 이하. 단일 청크 batch와 다중 청크 batch의 ns/item이 동일 (2.42 vs 2.43).

→ **peak 메모리 잡힌 후엔 v02 = v01 + 1ns**. "자동 성장"의 비용은 전부 첫 사이클(콜드)에 몰림.

### 11. large path는 정직한 syscall pair (v02)

96 KiB mmap+munmap pair = 4.3 µs. malloc 동일 크기는 37 ns (115× 빠름) — glibc가 M_MMAP_THRESHOLD(128KiB) 미만은 arena로 처리하고 반복 alloc/free는 free list 재사용.

v02 large는 **재사용 안 되는 일회성 큰 버퍼**용. 같은 크기 반복이면 풀 의미 없음 — malloc이 나음. "large는 예외 경로답게" 결정의 정직한 트레이드오프.

### 12. total_in_use는 O(청크 수) — hot path 호출 금지 (v02)

v02는 사용자에게 cheap한 잔량 조회 API를 제공하지 않음. `total_in_use()`/`total_capacity()` 모두 vector 순회. 사용자가 풀 잔량 모니터링이 필요하면 외부 카운터를 직접 관리해야 함. v02의 계약: **"끝없이 주는 풀" — 사용자는 fit 미리 체크할 일이 없다**.

### 13. free list pop = bump (v03)

v01 bump 1.34 ns ≈ v03 alloc 1.33 ns ≈ v03 dealloc 1.33 ns. 우연이 아님. 셋 다:

- 포인터 한 번 로드/저장
- branch predictor 100% 적중
- cache 100% hit (LIFO 패턴)

→ **"개별 free는 느리다"는 편견을 깸**. arena의 단순함이 slab의 LIFO에서도 그대로 재현됨. 진짜 다른 건 메모리 접근 패턴 (sequential vs LIFO 재활용).

### 14. slab의 아킬레스건 — free list 비순차 접근 (v03)

v03 pair 1.33 ns → batch 16384 3.85 ns (2.9× 증가). 이유: **free list가 free된 순서대로 꼬임** → alloc 시 ptr이 메모리에서 zigzag → hardware prefetcher 무력화.

arena가 sequential write로 batch 커져도 변동 없던 것과 정반대. **slab은 LIFO sweet spot에서만 진짜 강함**, 길게 늘어진 free list는 prefetcher 협력 깨짐.

### 15. arena 패턴은 slab의 worst case (v03)

v01/v02 batch alloc-only + reset = ~2 ns/item. v03 같은 패턴 = 46 ns/item (16384). **20배 느림**.

이유: slab의 reset은 munmap, 다음 사이클은 fresh mmap + slab init (1024 슬롯 포인터 write). arena의 "bump 위치만 복귀"와 비교 불가.

→ **패러다임 매칭 중요**. reset 자주 쓰는 워크로드면 arena, 개별 free 패턴이면 slab. 둘은 양극단 sweet spot이라 서로 흉내 내려 들면 가격을 치름.

### 16. malloc tcache hit도 v03의 6배 (v03)

malloc pair 15.9 ns vs v03 pair 2.66 ns. tcache가 가장 빠른 malloc 경로인데도. 이유:

- chunk header 갱신 (prev_size, size+flags)
- 스레드/arena 디스패치
- malloc 호환 분기 (small/large/huge 판단)
- metadata가 user data와 떨어진 위치 → cache miss

→ **단순함 자체가 성능**. v03이 size class 7개로 제약을 받아들인 가격으로 chunk header를 0으로 만든 결과.

## 측정 방법론

- CPU 상한 3.0 GHz 고정 (`cpupower frequency-set -u 3.0GHz`)
- `taskset -c 2`로 코어 고정
- google-benchmark `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`
- CV < 1% 목표 (실측 0.25% 이하 달성)
- 스크립트: `script/run_bench_v01.sh`

## 미해결 / 다음 단계

**v04 이후 후보**:

- slab + ptr→slab O(1) 룩업: slab-aligned mmap (over-allocate + trim) → `deallocate(void* p)` (size 없이) 가능
- per-slab free count 부활: 부분 shrink (빈 slab만 해제). ptr→slab 룩업과 묶임
- 멀티스레드: per-thread cache + size class 락. glibc tcache의 구조 직접 구현
- size class 정책 다양화: 16B 간격 (glibc tcache 스타일) — 메모리 효율 향상, 버킷 수 증가
- chunk 크기 동적 정책 (geometric growth) — v02 진화. peak 모르는 워크로드에서 청크 수 log 억제
- align > 4096 (huge alignment) 지원

**미정 / 측정 필요**:

- ~~`MAP_POPULATE` 플래그로 page fault 선처리 시 첫 touch 비용 변화~~ → 측정 완료. hot loop 영향 0 (발견 #6)
- ~~v02 청크 자동 성장의 hot path 비용~~ → 측정 완료. 정상 상태 0 (발견 #10)
- ~~v03 size class 분기 오버헤드~~ → 측정 완료. size에 무관 (발견 #13)
- 큰 풀(>10 MiB)에서 TLB pressure 영향
- huge page 사용 시 (`MAP_HUGETLB`) 수치 변화
- 단명 풀(매 iteration ctor/dtor) 시나리오에서 MAP_POPULATE 효과 측정
- v02 large path의 재사용 정책 (현재는 매번 munmap) — 마지막 large 청크 1개 보유 시나리오 비교
- v03 free list가 메모리에서 zigzag 정도 정량화 (cache miss profiling)

## 빌드/테스트

```bash
# 빌드 + 테스트
cmake --workflow --preset debug
ctest --preset test -R mempool

# 벤치 (sudo 필요 — cpupower)
cmake --workflow --preset release
./project/memory-pool/script/run_bench_v01.sh
./project/memory-pool/script/run_bench_v02.sh
./project/memory-pool/script/run_bench_v03.sh
```
