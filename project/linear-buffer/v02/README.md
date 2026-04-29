# v02

## 변경 사항

```cpp
// 추가
std::span<const std::byte> read_span() const noexcept;
std::span<const std::byte> peek(std::size_t n) const noexcept;
std::span<const std::byte> read(std::size_t n) noexcept;
```

## 설계 의도

v01의 `read(byte*, n)` / `peek(byte*, n)`은 항상 memcpy. span 반환 버전은 버퍼 내부를 직접 가리키는 뷰를 반환해 복사를 생략한다.

반환된 span의 수명: `clear()`나 해당 영역 덮어쓰기 전까지만 유효.

## 측정

### RawWrite vs ZeroCopyWrite

span API 추가 후 기존 쓰기 경로 회귀 검증. 측정 경로는 v01과 동일.

**가설**

span API는 읽기 경로에만 추가되었으므로 쓰기 경로 수치는 v01과 동일할 것.

**측정 결과**

RawWrite:

| 조건    |    mean |    CV | 비고           |
| :------ | ------: | ----: | :------------- |
| n=64    | 2.70 ns | 0.28% | 22.1 Gi/s      |
| n=4096  | 51.1 ns | 1.10% | 74.7 Gi/s (L1) |
| n=32768 |  967 ns | 0.40% | 31.6 Gi/s (L2) |
| n=65536 | 1989 ns | 0.64% | 30.7 Gi/s (L2) |

ZeroCopyWrite:

| 조건    |    mean |    CV | 비고           |
| :------ | ------: | ----: | :------------- |
| n=64    | 2.35 ns | 0.02% | 25.4 Gi/s      |
| n=4096  | 50.8 ns | 1.02% | 75.1 Gi/s (L1) |
| n=32768 |  964 ns | 0.29% | 31.7 Gi/s (L2) |
| n=65536 | 1990 ns | 0.45% | 30.7 Gi/s (L2) |

**분석**

v01 수치와 사실상 동일하다. span API 추가가 기존 경로 코드 생성에 영향을 주지 않았음을 확인했다.

**결론**

기존 경로 회귀 없음. span API 추가의 비용은 측정 수준에서 없다.

## 개선

읽기 경로에 zero-copy API를 추가했다. v01에서 `read(byte*, n)` / `peek(byte*, n)`이 항상 memcpy였던 한계를 span 반환으로 우회할 수 있게 됐다. 기존 경로 수치는 v01과 동일(n=4096: 74.7 Gi/s)하여 추가 비용이 없음을 확인했다.

## 트레이드오프

반환된 span의 수명 관리 책임이 호출자에게 있다. `clear()` 이후 접근은 dangling이며 컴파일러가 잡아주지 않는다.

## 과제

- span read 경로 자체의 신뢰할 수 있는 벤치가 없다. `DoNotOptimize(span.data())`가 객체 전체가 아닌 포인터만 관찰해 결과가 왜곡된다.
