# Object Pool v01

## 개요

- 고정 용량 객체 풀
- `std::vector<T>` storage + `std::vector<std::size_t>` free list (스택)
- Raw API: `acquire()` → `T*`, `release(T*)`
- 고갈 시 `acquire()`는 `nullptr` 반환 (C 스타일)

## 설계 결정

### free list = "빈 인덱스 스택"

가장 단순한 모델:

- `iota(0, capacity)`로 초기화 — 모든 인덱스가 처음엔 빈 슬롯
- `pop_back()`으로 획득, `push_back()`으로 반환
- **LIFO** — 방금 반환한 슬롯이 다음 획득 대상. 캐시 친화적 (같은 슬롯 재사용)

### Raw API의 한계

```cpp
T* p = pool.acquire();
// ... 사용 ...
// pool.release(p);  ← 누락 가능
```

사용자가 `release` 호출을 까먹으면 풀이 서서히 고갈됨. **v02에서 RAII Handle로 구조적 해결**.

### assert 기반 invariant 체크

- `release(nullptr)`: assert
- `release` 대상이 이 풀에서 나온 게 아니면: assert (포인터 산술로 idx 계산 후 범위 확인)
- Double release: `free_list_.size() < capacity`로 간접 체크

**철학**: C++ 관용 — 내부 계약 위반은 디버그 assert로, release에선 비용 0.

## API

| 카테고리  | 함수                                               |
| :-------- | :------------------------------------------------- |
| 상태      | `capacity()`, `available()`, `in_use()`            |
| 획득/해제 | `acquire() -> T*` (실패 시 nullptr), `release(T*)` |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

세 가지 시나리오:

- **BM_Pool_NoWarmup**: 초기화 직후 — free list 정렬 + storage cold
- **BM_Pool_Warmup**: churn 후 — free list 섞임 + storage warm (현실 워크로드에 가까움)
- **BM_NewDelete**: glibc malloc 베이스라인

스크립트: [../script/run_bench_v01.sh](../script/run_bench_v01.sh)

### 실측

|                          | Time (mean) |  Throughput |
| :----------------------- | ----------: | ----------: |
| Pool NoWarmup (모든 cap) |     2.42 ns | 413 M ops/s |
| Pool Warmup (모든 cap)   |     2.40 ns | 416 M ops/s |
| new/delete               |     21.0 ns |  48 M ops/s |

CV 대부분 < 0.1%, 극도로 안정.

### 관찰

- **new/delete 대비 8.7× 빠름** — 단일 스레드 glibc malloc도 빠른 편인데 이 격차. 멀티스레드 락 경합까지 들어가면 차이 더 벌어짐
- **Warmup vs NoWarmup 거의 동일**: hot loop의 LIFO 특성상 같은 슬롯 반복 → warmup 효과 희석. 현실 워크로드는 다중 holding + 교체가 섞여 들어가야 의미 있는 측정
- **capacity 무관** (64, 1024, 16384 동일): hot path는 `free_list_.pop_back()` 한 번 + 포인터 반환만. capacity 크기와 독립

## 다음 버전 힌트

- **v02**: `std::unique_ptr<T, Deleter>` Handle 추가 (release 누락 차단)
