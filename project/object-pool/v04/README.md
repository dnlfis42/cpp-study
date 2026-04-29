# v04

## 변경 사항

```cpp
// 변경
struct Node {
    T data;
    Node* next;  // size_t next -> Node* next
};

explicit ObjectPool(std::size_t chunk_size = 64); // fixed capacity -> chunk_size

// 추가
std::size_t chunk_size() const noexcept;

// 변경: noexcept 제거 (grow() 시 bad_alloc 가능)
[[nodiscard]]
Handle acquire();
```

## 설계 의도

### 가변 크기 풀 (청크 기반)

고정 크기 풀은 capacity 예측이 필요하다. v04는 고갈 시 `grow()`로 새 청크를 할당한다.

청크를 `std::vector<std::unique_ptr<Node[]>>`로 보관하므로 기존 청크는 이동/복사되지 않는다. 이미 발급된 Handle의 포인터는 성장 중에도 유효하다.

생성자에서 할당하지 않는다. 첫 `acquire()` 시점에 첫 청크를 만든다.

### 포인터 기반 free list

v03은 `Node`의 `next`를 배열 인덱스(`size_t`)로 저장했다. `storage_[idx]`는 `base + idx * 72B`: 72가 2의 거듭제곱이 아니라 곱셈이 필요하다.

v04는 `next`를 `Node*`로 저장한다. `node->next` 접근은 단순 포인터 역참조이며, 청크 경계와 무관하게 free list를 이어붙일 수 있다.

## 측정

### Handle 비용

통합 벤치(objpool_bench). `acquire()` -> `DoNotOptimize(h)` -> 소멸자 단일 반복. hot loop.

**측정 결과**

| 벤치            |    mean |    CV |
| :-------------- | ------: | ----: |
| `BM_v03_Handle` | 5.53 ns | 0.15% |
| `BM_v04_Handle` | 4.50 ns | 0.02% |

**분석**

가변 풀인데 고정 풀보다 `-1.03 ns` (-19%) 빠르다. 둘 다 `sizeof(Handle) = 24 B`, `sizeof(Node) = 72 B`로 동일하다. 차이는 free list 접근 방식이다.

v03 acquire: `storage_[idx].next` = `base + idx * 72` (72 곱셈)
v04 acquire: `node->next` = `ptr + 64` (오프셋 역참조)

hot loop에서 곱셈 1회 제거의 효과가 `~1 ns`로 나타났다.

**결론**

포인터 기반 free list가 인덱스 기반보다 빠르다. 가변성의 페널티(`grow()`, `chunks_` 벡터)는 hot path에 없다. 한 번 grow 후엔 `head_free_` 포인터만 사용한다.

## 개선

`Node* next` 포인터 free list로 곱셈 제거. v03 대비 `-1.03 ns` (-19%). 청크 기반 grow로 capacity 예측 불필요. 기존 포인터 안정성 유지.

## 트레이드오프

T DefaultConstructible 요구 (`grow()` 시 `Node[]` default-construct). `acquire()` `noexcept` 제거.

## 과제

- non-default-constructible T 지원 불가
- `grow()` 시 미사용 슬롯도 T 일괄 생성. 소멸자 비호출로 생명주기 직접 제어 불가
- 단일 스레드 전용
