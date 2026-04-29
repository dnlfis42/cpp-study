# v02

## 변경 사항

```cpp
// 제거
class RingBuffer {
    explicit RingBuffer(std::size_t capacity);
    std::size_t capacity_;                     // runtime 멤버
    // pos = (pos + n) % capacity_;            // div
};

// 추가
template <std::size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    static constexpr std::size_t capacity() noexcept { return N; }
    // pos = (pos + n) & (N - 1);              // and
};
```

## 설계 의도

- **`%` -> `&` 치환**: `% capacity_`는 `div` 명령(~20 사이클)이지만 N이 2의 거듭제곱이면 `& (N-1)`은 `and` 1 사이클로 대체된다.
- **컴파일타임 capacity**: `RingBuffer<16>`과 `RingBuffer<32>`는 다른 타입이므로 혼용이 컴파일 타임에 차단된다. `capacity()`를 `constexpr`로 사용할 수 있다.
- **`static_assert`**: `(N & (N-1)) == 0` 강제. 비트마스크 wrap이 유효하려면 필수.

## 측정

### ZeroCopy: v01 vs v02

compile-time N이 memcpy 크기 전파에 미치는 영향을 측정한다. `BM_ZeroCopy`는 `memcpy(write_ptr, src, chunk)` + `move_write_pos` + `move_read_pos`, memcpy 1회, 대역폭 `chunk` 기준.

**가설**

`%` -> `&` 치환으로 전 구간에서 빨라질 것.

**측정 결과**

| chunk | v01 mean | v02 mean |    CV |
| ----: | -------: | -------: | ----: |
|    64 |  19.2 ns |  1.34 ns | 0.05% |
|   256 |  20.9 ns |  6.23 ns | 0.05% |
|  1024 |  27.8 ns |  27.7 ns | 8.28% |
|  4096 |  56.9 ns |  95.4 ns | 6.82% |
| 16384 |   415 ns |   415 ns | 0.22% |
| 65536 |  1995 ns |  1954 ns | 0.51% |

**분석**

chunk <= 256에서 v02가 3~14배 빠르다. compile-time `chunk`를 컴파일러가 인식해 SIMD 인라인으로 최적화한 결과다.

chunk = 4096에서 v01 56.9 ns 대비 v02 95.4 ns로 1.7배 역전된다. CV도 6.82%로 다른 지점(0.05~0.51%)보다 뚜렷하게 높다. compile-time `n`이 전파되면 컴파일러가 `memcpy` 대신 `rep movsq`(마이크로코드 문자열 복사)를 인라인한다. `objdump -d`로 확인:

```
db5c: f3 48 a5    rep movsq %ds:(%rsi),%es:(%rdi)
```

`rep movsq`는 명령어 1개로 수 KB를 처리하지만 마이크로코드 serialization으로 IPC가 급락한다. `perf stat -e cycles,instructions` chunk=4096 기준:

| 버전 | instructions |  IPC |
| ---: | -----------: | ---: |
|  v01 |       47.3 B | 2.61 |
|  v02 |        1.26B | 0.06 |

v02는 명령어 수가 37배 적지만 IPC가 43배 낮아 처리 시간이 늘어난다.

chunk = 1024에서는 mean은 비슷하지만 CV가 8.28%로 높아 `rep movsq`와 SIMD 경계에서 불안정하게 전환됨을 시사한다.

chunk >= 16384에서는 두 버전이 동등하다. 메모리 대역폭이 지배적이므로 wrap 산술 방식의 차이가 희석된다.

**결론**

`&` 치환 이득은 소용량에서만 유효하다. compile-time N 전파가 중간 크기(chunk ~4096)에서 `rep movsq` 역전을 유발한다.

## 개선

소용량(chunk <= 256)에서 v01 대비 최대 14배 빠르다(chunk=64: 19.2 ns -> 1.34 ns). `%` -> `&` 치환과 compile-time 크기 특화가 결합된 결과다.

## 트레이드오프

compile-time N이 memcpy 크기를 상수로 전파시켜 중간 크기(chunk ~1024~4096)에서 `rep movsq` 인라인을 유발한다. chunk=4096 기준 v01 대비 1.7배 느리다.

## 과제

- `rep movsq` 인라인: compile-time N 전파가 중간 크기(chunk ~1024~4096)에서 `rep movsq`를 강제해 chunk=4096 기준 v01 대비 1.7배 느리다.
