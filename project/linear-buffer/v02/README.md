# v02

## 변경 사항

```diff
+std::span<const std::byte> read_span() const noexcept;
+std::span<const std::byte> peek(std::size_t n) const noexcept;
+std::span<const std::byte> read(std::size_t n) noexcept;
```

## 설계 의도

v01의 `read(byte*, n)`은 항상 memcpy. span 반환 버전은 버퍼 내부를 직접 가리키는 뷰를 반환해 복사를 생략.

반환된 span의 수명: `clear()`나 해당 영역 덮어쓰기 전까지만 유효.

## 벤치마크

### 1. RawWrite

v01과 동일 경로.

**결과**

| 시나리오 |    mean |    CV | 비고      |
| :------- | ------: | ----: | :-------- |
| n=64     | 2.70 ns | 0.28% | 22.1 Gi/s |
| n=4096   | 51.1 ns | 1.10% | 74.7 Gi/s |
| n=32768  |  967 ns | 0.40% | 31.6 Gi/s |
| n=65536  | 1989 ns | 0.64% | 30.7 Gi/s |

### 2. ZeroCopyWrite

v01과 동일 경로.

**결과**

| 시나리오 |    mean |    CV | 비고      |
| :------- | ------: | ----: | :-------- |
| n=64     | 2.35 ns | 0.02% | 25.4 Gi/s |
| n=4096   | 50.8 ns | 1.02% | 75.1 Gi/s |
| n=32768  |  964 ns | 0.29% | 31.7 Gi/s |
| n=65536  | 1990 ns | 0.45% | 30.7 Gi/s |

**결론**

v01과 수치 동일: span API 추가가 기존 경로에 회귀 없음.
