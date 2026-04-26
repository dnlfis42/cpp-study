# Object Pool v06

## v05 대비 변화

**MemoryPool v03 slab을 백엔드로 교체.** 자체 `Node[]` + free list 관리를 제거하고 memory-pool에 위임.

```cpp
// v05: 자체 Node + chunk 관리
struct Node { T data; Node* next; };
std::vector<std::unique_ptr<Node[]>> chunks_;
Node* head_free_;
std::size_t chunk_size_;   // 생성자 파라미터
void grow();               // 내부 청크 확장

// v06: MemoryPool v03 위임
mempool::v03::MemoryPool pool_;  // slab 할당/해제 전담
// Node, chunks_, head_free_, grow() 전부 제거
```

## 핵심 설계

### placement new + explicit 소멸자

v05는 `grow()`에서 `Node[]` 전체를 미리 생성 — T의 생성자가 acquire 전에 호출됨. v06은 slot 획득 후 placement new:

```cpp
// acquire
void* raw = pool_.allocate(sizeof(T));   // raw bytes
T* obj = new (raw) T{args...};           // 쓸 때만 생성

// release
obj->~T();                               // 쓴 만큼만 소멸
pool_.deallocate(obj, sizeof(T));
```

결과:
- T가 default-constructible 아니어도 됨 — `acquire(args...)` 로 생성자 인자 전달
- non-trivial T는 acquire/release마다 ctor/dtor 호출 (v05는 grow 시 일괄)
- trivial T는 placement new = no-op (컴파일러 elide)

### 생성자 예외 안전

```cpp
void* raw = pool_.allocate(sizeof(T));
try {
    T* obj = new (raw) T{args...};
    ++in_use_;
    return Handle{obj, Deleter{this}};
} catch (...) {
    pool_.deallocate(raw, sizeof(T));   // slot 반납 후 re-throw
    throw;
}
```

T 생성자가 예외를 던져도 slot leak 없음.

### 타입 제약

```cpp
static_assert(sizeof(T) <= mempool::v03::MemoryPool::kMaxSize);  // 최대 1024 B
static_assert(alignof(T) <= sizeof(T));  // slot 정렬 보장 범위 내
```

mempool v03의 size class 상한(1024 B)을 초과하는 T는 컴파일 타임 오류.

## API

| 카테고리     | 함수                                              | v05 대비      |
| :----------- | :------------------------------------------------ | :------------ |
| 상태         | `in_use() -> size_t`                              | 유지          |
| 상태         | `total_capacity() -> size_t` (bytes)              | 변경 (bytes)  |
| 획득         | `acquire(args...) -> Handle` (throws)             | 인자 전달 추가 |
| Type aliases | `Deleter`, `Handle`                               | 유지          |
| 제거         | `capacity()`, `available()`, `chunk_size()`       | 제거          |

`chunk_size` 파라미터 없음 — slab 크기(64 KiB)는 mempool v03이 고정 관리.

## 벤치마크

환경: i7-9750H, gcc 13.3, `-O3`, 3.0 GHz 고정, `taskset -c 2`, 10 repetitions

- T = `struct Item { char buf[64]; }` (trivial — ctor/dtor no-op)
- hot loop: steady-state acquire/release만 반복 (grow 비용 없음)

스크립트: [../../script/run_objpool_v06_bench.sh](../../script/run_objpool_v06_bench.sh)

### 실측 — v05 vs v06

| 구현 | median | 차이 |
| :--- | -----: | ---: |
| v05 (chunk 64) | 4.49 ns | baseline |
| v05 (chunk 1024) | 4.49 ns | 0 |
| v05 (chunk 16384) | 4.49 ns | 0 |
| **v06 (MemoryPool v03)** | **4.63 ns** | **+0.14 ns (+3%)** |

CV 전부 0.1% 이하.

### 원인 분해

v05와 v06의 hot path 차이:

```
v05: null check → free list pop → --available_ → Handle 생성
v06: null check → size_class_of (compile-time) → free list pop → ++in_use_ → Handle 생성
```

두 구현 모두 counter update 1회(v05: `--available_`, v06: `++in_use_`)를 포함. 차이는 `pool_.allocate()` 함수 호출 경계 — inline 여부에 따라 0~1 cycle.

### 관찰

- **MemoryPool v03 backend overhead ≈ 0**: +3% (0.14 ns)는 slab 메커니즘이 아닌 함수 호출 경계에서 기인
- **v05 chunk_size 불변**: hot loop은 첫 grow 이후 `head_free_` pop만 반복 — chunk_size는 steady-state에 무관
- **trivial T 기준**: non-trivial T는 acquire/release마다 ctor/dtor 비용 추가 — v05(grow 시 일괄)보다 느려짐
- **교차 검증 결론**: memory-pool v03 slab은 object-pool의 실용적 backend — 성능 손해 없이 메모리 관리 위임 가능
