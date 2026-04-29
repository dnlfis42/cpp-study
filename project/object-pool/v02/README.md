# v02

## 변경 사항

```cpp
// 추가
class Deleter {
public:
    Deleter() noexcept;
    explicit Deleter(ObjectPool* pool) noexcept;
    void operator()(T* obj) const noexcept;
};
using Handle = std::unique_ptr<T, Deleter>;

[[nodiscard]]
Handle acquire_unique() noexcept;
```

## 설계 의도

### RAII Handle

`acquire_unique()`가 반환하는 Handle은 스코프 이탈 시 자동으로 `release`를 호출한다. release 누락으로 인한 풀 고갈을 구조적으로 차단한다.

### Deleter 설계

`Deleter`가 `ObjectPool*`를 멤버로 보유. Handle이 소멸될 때 생성된 풀로 반납된다. `pool_` 기본값 `nullptr`는 acquire 실패 시 빈 Handle을 안전하게 처리한다.

`sizeof(Handle) = sizeof(T*) + sizeof(Deleter) = 16 B`.

### Raw API 병행 유지

v02는 Raw API(`acquire`/`release`)를 그대로 제공한다. Hot path 극한 최적화가 필요한 경우 Raw를, 일반적인 경우 Handle을 선택한다.

### 이동 불가 (non-movable)

v01과 동일한 이유에 더해, Handle outstanding 중 이동 시 `Deleter::pool_`이 dangling이 된다. 컴파일 타임에 "핸들 없음"을 보장할 수 없으므로 move를 `= delete`로 차단한다.

## 측정

### Raw vs Handle 오버헤드

`acquire/release` 단일 반복. hot loop.

**측정 결과**

| 벤치        |    mean |    CV |
| :---------- | ------: | ----: |
| `BM_Raw`    | 2.45 ns | 0.11% |
| `BM_Handle` | 4.13 ns | 0.25% |

**분석**

Handle 오버헤드 +1.68 ns (+69%): Deleter 생성/소멸 + `DoNotOptimize`가 `16 B` Handle 전체를 관찰 강제하는 비용.

Raw(2.45 ns)는 v01(2.35 ns) 대비 오차 범위 수준. 구현 동일.

**결론**

+1.68 ns로 release 누락 구조적 차단. new/delete(20.0 ns) 대비 Handle도 **4.8× 빠름**.

## 개선

`unique_ptr<T, Deleter>` Handle로 release 누락 구조적 차단. 오버헤드 `+1.68 ns`. new/delete 대비 Handle도 **4.8× 빠름** (`4.13 ns`).

## 트레이드오프

`+1.68 ns` (+69%). `sizeof(Handle)` = `16 B`.

## 과제

- Raw API 병행 제공으로 release 누락 경로 여전히 존재
- T가 DefaultConstructible이어야 함
- 단일 스레드 전용
