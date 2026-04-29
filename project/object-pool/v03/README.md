# v03

## 변경 사항

```cpp
// 제거
T* acquire() noexcept;
void release(T* obj) noexcept;
[[nodiscard]]
Handle acquire_unique() noexcept;

// 변경
class Deleter {
    Deleter(ObjectPool* pool, Node* node) noexcept; // node_ 추가
private:
    ObjectPool* pool_;
    Node* node_;  // 추가
};

// 추가
[[nodiscard]]
Handle acquire() noexcept;
```

## 설계 의도

### UB 제거

v02의 `release(T* obj)`는 `reinterpret_cast<Node*>(obj)`로 T*에서 Node*를 역산했다. `data`가 Node의 첫 멤버라 표준-레이아웃 T에서는 동작하지만, 엄밀히는 pointer-interconvertible 조건 위반이다.

Deleter에 `Node*`를 직접 저장하면 `acquire()` 시점에 이미 Node 주소를 알고 있으므로 역산이 불필요하다. `release(Node* node)`는 포인터 산술만으로 index를 복원한다.

`sizeof(Deleter)`: `8 B` -> `16 B`, `sizeof(Handle)`: `16 B` -> `24 B`.

### Raw API 제거

`acquire() -> T*` + `release(T*)` 경로를 삭제했다. Handle이 유일한 공개 경로이므로 release 누락이 구조적으로 불가능하다.

### acquire() rename

Raw API가 사라지면서 이름 충돌이 없어졌다. `acquire_unique()` -> `acquire()`.

## 측정

### Handle 크기 비용

통합 벤치(objpool_bench). `acquire()` -> `DoNotOptimize(h)` -> 소멸자 단일 반복. hot loop.

**측정 결과**

| 벤치          |    mean |    CV |
| :------------ | ------: | ----: |
| BM_v02_Handle | 4.16 ns | 0.19% |
| BM_v03_Handle | 5.53 ns | 0.23% |

**분석**

+1.37 ns (+33%): Deleter에 `Node*` 하나 추가된 비용. Deleter 생성 시 포인터 저장 1회 추가, `DoNotOptimize`가 `24 B` Handle 전체를 관찰 강제하면서 레지스터 압력 증가.

**결론**

+1.37 ns로 `reinterpret_cast` UB 제거. new/delete(`20.0 ns`) 대비 여전히 **3.6× 빠름**.

## 개선

Deleter에 `Node*` 직접 저장, `reinterpret_cast` 제거. Raw API 제거. Handle이 유일한 공개 경로. `acquire_unique()` -> `acquire()`.

## 트레이드오프

`sizeof(Handle)` `16 B` -> `24 B`. `+1.37 ns` (+33% vs v02).

## 과제

- 인덱스 free list: `base + idx * sizeof(Node)` 곱셈 비용
- T가 DefaultConstructible이어야 함
- 고정 크기 (capacity 불변)
- 단일 스레드 전용
