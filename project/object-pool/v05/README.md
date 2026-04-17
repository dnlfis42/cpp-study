# Object Pool v05

## v04 대비 변화

**청크 기반 가변 크기 풀.** 초기 capacity 예측 불필요 — 고갈 시 자동으로 새 청크 할당.

```cpp
// v04 (고정)
explicit ObjectPool(std::size_t capacity);       // 초기에 전부 할당
Handle acquire() noexcept;                        // 고갈 시 빈 Handle

// v05 (가변)
explicit ObjectPool(std::size_t chunk_size = 64);
Handle acquire();                                 // 고갈 시 자동 grow (bad_alloc throw 가능)
```

## 핵심 설계

### Non-intrusive + **포인터 연결**

```cpp
struct Node {
    T data;
    Node* next;    // 인덱스 아닌 포인터 — 청크 경계 무관
};
```

v03/v04는 `size_t next`(인덱스) — 단일 배열 전제. **청크 기반에선 "index" 개념이 모호**: 어느 청크의 몇 번째? 포인터로 바꾸면 **청크가 달라도 같은 free list에 이어짐**.

### 청크 기반 storage

```cpp
std::vector<std::unique_ptr<Node[]>> chunks_;
```

- **한 청크 = `new Node[chunk_size]`** — 힙에 한 번 할당
- **기존 청크 이동/복사 없음** — `vector<unique_ptr<Node[]>>`는 새 청크 포인터만 push_back. **기존 Node 주소 영구 불변**
- **포인터 안정성** — 사용자가 들고 있는 `T*`는 성장 중에도 유효

### lazy 초기화

```cpp
ObjectPool<T> pool{64};  // chunk_size만 저장, 할당 없음
pool.capacity();          // 0 — 아직 청크 없음
auto h = pool.acquire();  // 첫 호출에서 첫 청크 할당
pool.capacity();          // 64
```

**첫 사용 전엔 메모리 점유 0** — 세션 단위 풀, 가끔 쓰는 풀에 유리.

### 자동 성장 (`grow()`)

```cpp
void grow() {
    auto new_chunk = std::make_unique<Node[]>(chunk_size_);
    // 청크 내부 연결: 0 → 1 → ... → N-1
    for (i = 0; i + 1 < chunk_size_; ++i)
        new_chunk[i].next = &new_chunk[i + 1];
    // 마지막 Node → 기존 head_free_
    new_chunk[chunk_size_ - 1].next = head_free_;
    head_free_ = &new_chunk[0];
    chunks_.push_back(std::move(new_chunk));
}
```

**새 청크가 free list의 머리**가 됨. LIFO 특성 유지 — 최근 할당된 청크가 먼저 쓰임 (캐시 친화).

## 왜 v06 (원본)이 v04보다 빠른가 (예상)

인덱스 → 포인터 전환:

- v04: `storage_.data() + idx * sizeof(Node)` — 곱셈 + 덧셈
- v05: `node->next` — 역참조 한 번
- hot path에서 **1 cycle 정도 절약**

원본 회고: **v04 3.79ns → v06 3.27ns** (~14% 빠름). 그런데 새 v04(4.86ns)와 새 v05는 어떻게 될지 측정 필요.

## 설계상 주의

- **`acquire()`가 throw 가능**: `grow()`에서 `bad_alloc`. v04는 noexcept였음
- **pool move 시 주의**: Handle outstanding 상태에서 pool move하면 Deleter dangling (v02와 동일 제약)
- **capacity 예측 필요 없지만**: chunk_size 너무 작으면 잦은 grow, 너무 크면 unused 메모리

## API

v04 + 하나 추가:

| 카테고리     | 함수                                                        |
| :----------- | :---------------------------------------------------------- |
| 상태         | `capacity()`, `available()`, `in_use()`, **`chunk_size()`** |
| 획득         | `acquire() -> Handle` (고갈 시 자동 성장, throws)           |
| Type aliases | `Deleter`, `Handle`                                         |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

- **BM_Pool_Handle**: hot loop, 첫 청크만 사용 (성장 비용 없음)
- 고정 풀(v04)과 직접 비교 가능

스크립트: [../script/run_bench_v05.sh](../script/run_bench_v05.sh)

### 실측

| chunk_size | v04 (고정, 4.86) | v05 (가변 lazy) |               차이 |
| ---------: | ---------------: | --------------: | -----------------: |
|         64 |          4.86 ns |     **4.48 ns** | **-0.38 ns (-8%)** |
|       1024 |          4.86 ns |         4.53 ns |     -0.33 ns (-7%) |
|      16384 |          4.86 ns |         4.53 ns |     -0.33 ns (-7%) |

**원인 분해** (예측과 일치):

- v04: `&storage_[idx].data` → `base + idx * sizeof(Node)` (곱셈 1, 덧셈 1)
- v05: `node->data` (역참조만)
- hot path에서 약 1 cycle 차이 → 3 GHz에서 ~0.33 ns 절약

원본 회고는 v04→v06 ~14% 빠름이었고, 우리 측정은 ~7~8% — 컴파일러/환경 차이로 크기는 달라도 방향은 일치.

### 관찰

- **가변 풀인데 고정 풀보다 빠름**: 인덱스→포인터 전환의 순수 이득. 청크 기반 자료구조의 추가 오버헤드(`chunks_` 벡터)는 hot path에 없음 (한 번 grow 후엔 `head_free_` 포인터만 사용)
- **hot loop은 grow 없음**: 첫 청크만 반복 재사용 → 가변성의 페널티 없이 이득만
- **현실 워크로드(여러 홀더 + 교체 + grow)는 별도 벤치 필요** — hot path 측정과 분리해서 봐야 함

## 최종 설계 완성

v05는 **네 TCP 서버 세션 풀 맥락의 권장 설계**:

- 연결 수 변동에 유연 (가변)
- release 누락 방지 (Handle 전용)
- 포인터 안정성 (세션 주소 영구 불변)
- lazy — idle 시 메모리 최소
