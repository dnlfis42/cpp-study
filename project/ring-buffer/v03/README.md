# v03

## 변경 사항

```cpp
// 추가
namespace detail {
[[gnu::noinline, gnu::noclone]]
inline void copy_bytes(void* dst, const void* src, std::size_t n) noexcept;
}

// 변경
// std::memcpy(...) -> detail::copy_bytes(...)
```

## 설계 의도

- **`detail::copy_bytes` 래퍼**: compile-time 크기 전파를 차단해 `rep movsq` 인라인을 막는다. `noinline`만으로는 IPA-CP가 `copy_bytes.constprop.N` 클론을 생성해 클론 본체에 `rep movsq`가 재삽입된다. `noclone`이 클론 생성 자체를 차단한다.
- **clang 분기**: `noclone`은 GCC 전용이므로 `RINGBUF_NOINLINE_NOCLONE` 매크로로 컴파일러별 분기한다. clang에서는 `noinline`만 적용된다.

## 측정

### WriteRead: v01 / v02 / v03 비교

`detail::copy_bytes` 래퍼가 `rep movsq` 역전을 해소하는지 측정한다. `BM_WriteRead`는 `write(src, chunk)` + `read(dst, chunk)`, memcpy 2회, 대역폭 `chunk * 2` 기준.

**가설**

`rep movsq` 구간(chunk ~1024~4096)에서 v03이 v02보다 빠르고 v01보다도 빠를 것. 소용량(chunk <= 256)에서는 `call memcpy@plt` 오버헤드로 v02보다 느릴 것.

**측정 결과**

| chunk | v01 mean | v02 mean | v03 mean |    CV |
| ----: | -------: | -------: | -------: | ----: |
|    64 |  23.4 ns |   3.2 ns |  5.92 ns | 0.22% |
|   256 |  26.5 ns |  12.9 ns |  11.9 ns | 0.16% |
|  1024 |  40.6 ns |  59.7 ns |  26.7 ns | 9.40% |
|  4096 |   101 ns |   188 ns |  97.0 ns | 3.79% |
| 16384 |   852 ns |  1000 ns |   845 ns | 1.30% |
| 65536 |  3809 ns |  3790 ns |  3958 ns | 1.90% |

**분석**

chunk = 1024~4096에서 v03이 v02 대비 2.0배 빠르다. `rep movsq` 역전이 해소됐다. chunk=4096 기준 v03(97 ns)은 v01(101 ns)보다도 빠르다. `&` 비트마스크 이득을 유지하면서 `rep movsq`를 제거해 두 이득이 결합된 결과다.

chunk = 1024에서 CV가 9.40%로 높다. `rep movsq`와 SIMD 경계 부근에서 런타임 dispatch 경로가 불안정하게 분기됨을 시사한다.

chunk = 64에서 v03(5.92 ns)이 v02(3.2 ns)보다 느리다. v02는 SIMD 인라인으로 직접 실행되지만 v03은 `call memcpy@plt` 오버헤드가 추가된다.

`noclone` 적용으로 `copy_bytes`가 단일 심볼로 유지되며 본체가 `jmp memcpy@plt`로 컴파일됨을 `objdump`로 확인:

```
0000000000010060 <ringbuf::v03::detail::copy_bytes(...)>:
   10060: endbr64
   10064: jmp    <memcpy@plt>
```

IPC 회복을 `perf stat -e cycles,instructions` chunk=4096 기준으로 확인:

| 버전 | instructions |  IPC |
| ---: | -----------: | ---: |
|  v02 |        1.26B | 0.06 |
|  v03 |       56.11B | 2.96 |

**결론**

`noinline` + `noclone` 조합으로 `rep movsq` 역전이 해소된다. chunk=4096 기준 v01 대비 4% 빠르고 v02 대비 1.9배 빠르다.

### ZeroCopy: v02 vs v03

zero-copy 경로(`write_ptr` + `memcpy` + `move_write_pos`)가 래퍼를 경유하지 않으므로 v02와 동일해야 한다.

**측정 결과**

| chunk | v02 mean | v03 mean |    CV |
| ----: | -------: | -------: | ----: |
|    64 |  1.34 ns |  1.34 ns | 0.08% |
|   256 |  6.23 ns |  6.23 ns | 0.10% |
|  1024 |  27.7 ns |  27.7 ns | 8.32% |
|  4096 |  95.4 ns |  95.1 ns | 6.71% |
| 16384 |   415 ns |   417 ns | 0.72% |
| 65536 |  1954 ns |  2078 ns | 1.00% |

**결론**

zero-copy 경로는 래퍼 미경유로 v02와 동일하다. `detail::copy_bytes` 개선은 copy-based 메서드(`read`/`write`/`peek`) 전용이다.

## 개선

chunk=4096 기준 v02 대비 1.9배 빠르고 v01 대비 4% 빠르다. `&` 비트마스크와 runtime dispatch memcpy 두 이득이 결합된 결과다.

## 트레이드오프

`call memcpy@plt` 오버헤드로 소용량(chunk <= 256)에서 v02보다 약간 느리다. zero-copy 경로에는 개선이 적용되지 않는다.

## 과제

- `noclone` GCC 전용: clang 빌드에서는 IPA-CP가 constprop 클론을 생성해 `rep movsq`가 재발할 수 있다.
- zero-copy 경로: 사용자가 직접 `std::memcpy`를 호출하는 경로는 래퍼를 경유하지 않아 v02와 동일하다.
