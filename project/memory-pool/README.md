# memory-pool

raw 바이트 단위 메모리 풀. arena → 청크 리스트 → slab → 멀티스레드 순으로 진화.

- **arena (bump allocator)**: 개별 free 없음, reset/destroy로만 회수. 단명·일괄 폐기 워크로드에 압도적
- **mmap 백엔드**: 페이지 정렬, lazy commit, page fault는 OS가 처리
- **정렬 마스킹**: `(pos + align - 1) & ~(align - 1)` — 명시 정렬 비용 zero
- **RAII 자원 클래스**: mmap raw 자원에 대해 수동 move + dtor 가드 패턴
- **청크 리스트 (v02~)**: 부족 시 자동 성장. small/large 이원화로 임의 크기 수용

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

## 측정 방법론

- CPU 상한 3.0 GHz 고정 (`cpupower frequency-set -u 3.0GHz`)
- `taskset -c 2`로 코어 고정
- google-benchmark `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`
- CV < 1% 목표 (실측 0.25% 이하 달성)
- 스크립트: `script/run_bench_v01.sh`

## 미해결 / 다음 단계

**v03 이후 후보**:

- slab 분기 (size class + 개별 free) — object-pool과 교차 검증 후보
- chunk 크기 동적 정책 (geometric growth) — peak 모르는 워크로드에서 청크 수 log 억제
- align > 4096 (huge alignment) 지원
- 멀티스레드: per-arena 락 → tcache 스타일 thread-local
- shrink-to-fit: peak 후 미사용 small 청크 일부 반환

**미정 / 측정 필요**:

- ~~`MAP_POPULATE` 플래그로 page fault 선처리 시 첫 touch 비용 변화~~ → 측정 완료. hot loop 영향 0 (발견 #6)
- ~~v02 청크 자동 성장의 hot path 비용~~ → 측정 완료. 정상 상태 0 (발견 #10)
- 큰 풀(>10 MiB)에서 TLB pressure 영향
- huge page 사용 시 (`MAP_HUGETLB`) 수치 변화
- 단명 풀(매 iteration ctor/dtor) 시나리오에서 MAP_POPULATE 효과 측정
- v02 large path의 재사용 정책 (현재는 매번 munmap) — 마지막 large 청크 1개 보유 시나리오 비교

## 빌드/테스트

```bash
# 빌드 + 테스트
cmake --workflow --preset debug
ctest --preset test -R mempool

# 벤치 (sudo 필요 — cpupower)
cmake --workflow --preset release
./project/memory-pool/script/run_bench_v01.sh
./project/memory-pool/script/run_bench_v02.sh
```
