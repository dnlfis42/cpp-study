# v05

## 변경 사항

```cpp
// 제거
struct Node { T data; Node* next; };
explicit ObjectPool(std::size_t chunk_size = 64);
std::size_t capacity() const noexcept;
std::size_t available() const noexcept;
std::size_t chunk_size() const noexcept;

// 추가
static_assert(sizeof(T) <= mempool::v03::MemoryPool::kMaxSize);

std::size_t total_capacity() const noexcept; // bytes 단위

template <typename... Args>
[[nodiscard]]
Handle acquire(Args&&... args); // placement new, args 전달

// Deleter: node_ 제거 (pool_* 만 보유)
// sizeof(Handle): 24B -> 16B
```

## 설계 의도

### T 생명주기 제어

v04는 `grow()` 시 `Node[chunk_size]`로 T를 일괄 default-construct한다. 실제로 쓰지 않을 슬롯도 포함. release 시 소멸자를 호출하지 않는다.

v05는 acquire 시 placement new, release 시 explicit 소멸자. 실제로 쓰는 슬롯에서만 ctor/dtor가 호출된다.

### Non-default-constructible T 지원

`acquire(Args&&... args)`로 임의의 생성자를 호출할 수 있다.

### Slot 오버헤드 제거

v04의 `Node`는 `T data` 외에 `Node* next` `8 B`를 포함한다. v05는 mempool의 raw slot에 T를 직접 placement new하므로 슬롯 크기 = `sizeof(T)`.

### mempool 위임

free list 관리를 `mempool::v03::MemoryPool`에 위임한다. ObjectPool은 T의 생명주기에만 집중한다.

### 선택 기준

**v04가 유리한 경우**: release 시 소멸자를 호출하지 않으므로 객체의 내부 상태가 슬롯에 유지된다. vector처럼 capacity를 가진 멤버가 있는 무거운 객체를 자주 획득/반납하는 경우, warmup 이후엔 vector heap 재할당이 발생하지 않는다. 수동으로 상태를 초기화할 수 있다면 heap 왕복을 피할 수 있다.

**v05가 유리한 경우**: 매 acquire마다 깨끗한 초기 상태가 필요한 경우. 소켓, 파일 핸들처럼 소멸자에서 리소스를 반납해야 하는 경우. non-default-constructible T가 필요한 경우.

## 측정

### Handle 비용

통합 벤치(objpool_bench). `acquire()` -> `DoNotOptimize(h)` -> 소멸자 단일 반복. hot loop. trivial T(`char buf[64]`).

**측정 결과**

| 벤치            |    mean |    CV |
| :-------------- | ------: | ----: |
| `BM_v04_Handle` | 4.50 ns | 0.03% |
| `BM_v05_Handle` | 4.67 ns | 0.18% |

**분석**

Handle은 `16 B`로 v04(`24 B`)보다 작아졌지만 +0.17 ns (+4%) 느리다. trivial T에서 placement new / `~T()`는 사실상 no-op이지만, `pool_.allocate()` / `pool_.deallocate()` 함수 호출 경계와 mempool 내부 경로가 추가된다.

**결론**

trivial T hot loop 기준으로는 v04가 빠르다. v05의 이점은 성능이 아닌 T 생명주기 제어에 있다.

## 개선

placement new + explicit dtor로 T 생명주기 직접 제어. Non-default-constructible T 지원. `sizeof(Handle)` `24 B` -> `16 B`. mempool 위임으로 free list 구현 제거.

## 트레이드오프

trivial T hot loop 기준 `+0.17 ns` (+4% vs v04). `sizeof(T)` <= `1024 B` 제한 (mempool v03).

## 과제

- `sizeof(T)` <= `1024 B` (mempool v03 제한)
- 단일 스레드 전용
- `total_capacity()`: 슬롯 수를 직접 알 수 없음 (bytes 단위)
- reset 함수 template non-type parameter 미구현: v04의 heap 유지 이점과 v05의 상태 초기화를 `if constexpr`로 비용 없이 결합 가능

  ```cpp
  template <typename T, void(*ResetFn)(T*) = nullptr>
  class ObjectPool {
      void release(T* obj) noexcept {
          if constexpr (ResetFn != nullptr) {
              ResetFn(obj); // 인라인, 분기 없음
          }
          // free list 반납 (소멸자 미호출 - heap 유지)
      }
  };

  void reset_session(Session* s) { s->buffers_.clear(); } // capacity 유지

  ObjectPool<Session, reset_session> pool;
  ```
