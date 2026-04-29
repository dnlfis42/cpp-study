# v01

## 설계 의도

베이스라인. 가장 단순한 구조로 풀링의 원형을 보여주고 new/delete 대비 이득을 실측한다.

### free list = 빈 인덱스 스택

`std::vector<size_t>`를 스택으로 사용. `iota(0, capacity)`로 초기화 후 `pop_back`으로 획득, `push_back`으로 반환.

**LIFO**: 방금 반환한 슬롯이 다음 획득 대상: 캐시 친화적.

### Raw API와 그 한계

```cpp
T* p = pool.acquire();
// ...
// pool.release(p);  <- 누락 가능
```

release 누락 시 풀이 서서히 고갈. `[[nodiscard]]`로 반환값 무시를 컴파일 경고로 잡는다.

### 이동 불가 (non-movable)

`std::mutex`·`std::atomic`처럼 리소스 매니저는 이동을 허용하지 않는다. 이동 후 원본에 남아있는 Raw 포인터(`T*`)가 이동된 풀이 아닌 원본을 참조하게 되는 상황을 컴파일 타임에 차단할 방법이 없다. C++17 guaranteed copy elision(RVO)으로 팩토리 함수 반환은 move 없이도 가능하다.

### assert 기반 invariant 체크

- `release(nullptr)`: assert
- 다른 풀의 포인터 반환: assert (포인터 산술로 idx 범위 확인)
- double release: `free_list_.size() < capacity` 간접 체크

프로그래머 버그는 debug assert로, release 빌드에서는 비용 0.

## 측정

### Pool vs new/delete

`acquire() -> release()` 단일 반복. hot loop.

**측정 결과**

| 벤치         |    mean |    CV |
| :----------- | ------: | ----: |
| BM_ObjPool   | 2.35 ns | 0.40% |
| BM_NewDelete | 20.0 ns | 1.50% |

**분석**

hot path는 `free_list_.pop_back()` 한 번 + 포인터 반환. LIFO로 항상 같은 슬롯을 재사용하므로 측정이 안정적(CV 0.40%).

new/delete는 단일 스레드에서도 glibc malloc의 프리 리스트 탐색 + 커널 경계 비용이 발생.

**결론**

new/delete 대비 **8.5× 빠름**. 멀티스레드 락 경합·단편화까지 고려하면 실무 이득은 더 크다.

## 개선

new/delete 대비 **8.5× 빠름** (`2.35 ns` vs `20.0 ns`). vector 스택 free list + LIFO로 캐시 친화적 접근.

## 과제

- Raw API release 누락을 `[[nodiscard]]`로 경고하지만 컴파일 타임 차단은 아님
- T가 DefaultConstructible이어야 함 (`vector<T> storage_(capacity)` 기본 생성)
- 단일 스레드 전용
