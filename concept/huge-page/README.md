# huge page (note)

리눅스 huge page (2 MiB / 1 GiB) — TLB pressure 해소 메커니즘과 실측. backing storage가 hot path에 미치는 영향만 측정 (allocator-agnostic).

## 무엇인가

기본 페이지 크기 4 KiB. **huge page = 2 MiB (또는 1 GiB)** 단위로 가상→물리 매핑. 한 TLB entry가 더 큰 영역 커버 → TLB miss 빈도 감소.

## 왜 — TLB pressure

CPU의 TLB(Translation Lookaside Buffer)는 가상→물리 주소 변환 캐시. 보통:

- L1 dTLB: 64 entries (4KB 페이지 기준 256 KiB 커버)
- L2 TLB: ~1024-1500 entries (~6 MiB 커버)

**작업셋이 L2 TLB 커버 범위 초과 → TLB miss**. miss 시 page table walk:

- 4-level page walk (x86-64) = 4번의 메모리 접근
- ~10-30 cycle (L1 PT 캐시 hit 기준), 최악 ~100ns (PT까지 RAM)

→ **데이터는 L1 hit인데 TLB miss로 인해 추가 100ns 소비** 같은 일이 일어남.

huge page 효과:

- 2 MiB 페이지: TLB entry 1개 = 2 MiB 커버. **TLB pressure 512×↓**
- 64 MiB 풀이 32개 huge page → L1 dTLB(64)에 다 들어감

## 3가지 활성화 방법

### (a) 명시적 hugepage (`MAP_HUGETLB`)

```cpp
mmap(nullptr, size, PROT_R|PROT_W,
     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
```

- 사전에 시스템에 huge page 예약 필요: `sudo sysctl -w vm.nr_hugepages=64` (64 × 2MiB)
- 예약 부족 시 `mmap` 실패 (ENOMEM)
- size는 huge page size (2 MiB) 배수
- **확정적**: 예약 성공하면 무조건 huge page

### (b) Transparent Huge Page hint (`madvise(MADV_HUGEPAGE)`)

```cpp
void* p = mmap(nullptr, size, PROT_R|PROT_W,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
madvise(p, size, MADV_HUGEPAGE);
```

- 시스템 THP (transparent huge page)가 enabled면 커널이 자동 승격 시도
- 4KB 페이지로 시작 → 백그라운드 khugepaged가 2MB로 합칠 때 합침
- 예약 불필요. 실패해도 정상 페이지로 돌아감.
- 시스템 설정: `cat /sys/kernel/mm/transparent_hugepage/enabled` → `always [madvise] never` 중 활성

### (c) THP 시스템 전역 (`always`)

`/sys/kernel/mm/transparent_hugepage/enabled`이 `always`면 madvise 없이도 자동 승격. 데스크톱 디폴트는 `madvise` (앱이 hint 줘야 적용).

## 측정 (실측)

환경: 12-core CPU, L1 dTLB 64 entries, L2 TLB ~1024, L3 12 MiB. `nr_hugepages=64`, THP=`madvise`.

### Random Read (TLB stress) — 64 MiB 풀, 4 KiB stride 16384 page touch

| 변형    | 평균 시간 | per touch |  개선 |
| ------- | --------: | --------: | ----: |
| Normal  |    130 µs |   7.95 ns |     — |
| Madvise |    117 µs |   7.17 ns | 1.10× |
| Hugetlb |    117 µs |   7.18 ns | 1.10× |

**예상보다 작은 차이 (10%)**. 이유:

- random 4KB stride → 매 접근이 **다른 cache line** → 대부분 L3 또는 RAM miss (~80-100 ns)
- TLB miss(~10-30 cycle = ~3-10 ns)는 **데이터 fetch와 overlap** (out-of-order, page walker는 독립적 unit)
- → cache miss가 dominant이면 TLB miss 비용은 거의 보이지 않음

→ **TLB의 진짜 효과는 latency-bound + cache hit 워크로드에서 드러남**. 이 측정은 throughput/cache-bound라 효과 약화.

### First Touch Sequential Write — 매 사이클 새 풀 mmap + memset 64 MiB

| 변형    | 평균 시간 |      처리량 |     개선 |
| ------- | --------: | ----------: | -------: |
| Normal  |   40.4 ms |  1.55 GiB/s |        — |
| Madvise |   6.12 ms | 10.21 GiB/s | **6.6×** |
| Hugetlb |   5.65 ms | 11.06 GiB/s | **7.2×** |

**충격적인 차이 (7배)**. 산수가 정확히 들어맞음:

- Normal: 64 MiB / 4 KiB = **16384 page fault**. fault당 ~5 µs (커널 zero-fill + map) × 16384 = ~80 ms (측정 40 ms — OOO/concurrent fault로 절반 흡수).
- Hugetlb: 64 MiB / 2 MiB = **32 page fault**. fault당 ~165 µs (2 MiB zero-fill의 메모리 대역폭 비용) × 32 = ~5.3 ms (측정 5.65 ms ✓).

→ huge page는 **fault 횟수를 512× 감소**. fault당 비용은 더 비싸지만 (2 MiB zero-fill vs 4 KiB), **총합 7× 빠름**.

## 발견

### 1. TLB와 cache는 다른 layer

같은 "메모리 locality"라도:

- **L1 cache hit**: ~1 cycle. cache line(64 B) 단위.
- **TLB hit + cache miss**: ~100 ns (RAM 가져옴). 페이지(4KB or 2MB) 단위.
- **TLB miss + cache hit**: 데이터는 L1에 있는데 page walk로 ~10-30 cycle 추가.
- **TLB miss + cache miss**: 둘 다 비싸지만 page walk가 cache fetch와 overlap → 거의 후자 비용.

→ huge page는 TLB layer만 다룸. cache hierarchy(L1/L2/L3)는 무관.

### 2. lazy commit + 작은 페이지 = page fault 폭발

첫 touch 시나리오(매번 새 mmap)는 **fault 횟수가 곧 비용**. huge page가 결정적 우위 (7×). 단명 풀, 매 요청 새 버퍼 등.

장수명 풀(서버 시작 후 한 번만 fault)은 fault가 amortize되어 huge page 효과 작아짐 (memory-pool/v01 발견 #6 참고).

### 3. TLB 효과는 워크로드에 의존

random read 4KB stride에서 10%만 빨라짐 — cache miss가 dominant라 TLB miss가 OOO에 흡수됨. **TLB의 진짜 효과**는:

- random read가 **L1/L2 hit**할 정도로 자주 재방문되는 워크로드
- 또는 **pure latency** 측정 (의존 chain pointer-chasing 등)

이런 워크로드 별도 설계 필요.

### 4. THP `madvise` ≈ `MAP_HUGETLB`

본 측정에서 두 결과가 거의 동일 (각 117 µs, 6.12 vs 5.65 ms). THP가 충분히 빠르게 승격해주면 명시 huge page와 차이 미미.

장점/단점:

- THP: 예약 불필요, 실패해도 정상 동작 → 운영 친화적
- MAP_HUGETLB: 확정적, 예측 가능 → 측정/벤치 친화적

## 함정

- **`MAP_HUGETLB`는 size가 2 MiB 배수여야 함**. 안 맞으면 EINVAL.
- **`nr_hugepages` 예약은 시스템 메모리 단편화에 취약**. 부팅 직후가 가장 안정적. 런타임 예약은 종종 실패.
- **THP가 메모리 사용량을 늘림**. 1 KB만 쓰려고 mmap한 영역이 2 MiB 통째로 잡힐 수 있음. 작은 풀에서는 부적절.
- **NUMA 시스템에서 huge page는 한 노드에 묶임** — cross-NUMA 접근 시 오히려 느려짐.

## 다음 측정 카드

- **TLB miss를 진짜 노출하는 워크로드** — pointer chase, 데이터 자체는 L1 hit (small struct random index)
- **풀 크기 sweep** (1, 16, 64, 256 MiB, 1 GiB) — TLB pressure는 풀 크기에 비례
- **memory-pool v01-v03에 huge page 통합** — 실제 allocator 워크로드에서 효과 측정
- **`MAP_HUGETLB` + `MAP_POPULATE`** — first touch 비용을 ctor로 몰아넣기. 단명 풀 시나리오 분리
- **1 GiB 페이지 (`MAP_HUGE_1GB`)** — 2 MiB와 비교. cuda-style 매우 큰 작업셋

## 참고

- `man 2 mmap` — `MAP_HUGETLB`, `MAP_HUGE_2MB`, `MAP_HUGE_1GB`
- `man 2 madvise` — `MADV_HUGEPAGE`, `MADV_NOHUGEPAGE`
- `Documentation/admin-guide/mm/transhuge.rst` (커널 문서)
- 측정 코드: `project/memory-pool/bench/mempool_huge_page_bench.cpp`
- 실행: `script/run_mempool_huge_page_bench.sh`
