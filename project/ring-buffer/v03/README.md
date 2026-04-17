# Ring Buffer v03

## v02 대비 변화

`detail::copy_bytes` 래퍼 함수로 모든 `std::memcpy` 호출을 감싸 **`rep movsq` 인라인 차단**.

```cpp
namespace detail {
[[gnu::noinline, gnu::noclone]]
inline void copy_bytes(void* dst, const void* src, std::size_t n) noexcept {
    std::memcpy(dst, src, n);
}
}

// 사용
detail::copy_bytes(buf_.get() + write_pos_, src, first);
// 대신 std::memcpy(...)
```

클래스 내부의 모든 `std::memcpy` 호출을 `detail::copy_bytes`로 교체.

## 왜 `noinline` + `noclone` 둘 다 필요한가

### `noinline` 단독으론 부족

컴파일러가 `copy_bytes` 호출을 인라인하지 못하게 막는 건 성공. 하지만 **IPA-CP (Inter-Procedural Constant Propagation)**가 호출 지점의 compile-time 상수(`n=4096` 등)를 기반으로 **특화 clone**을 생성:

```
copy_bytes.constprop.4096:
    ... rep movsq ...   // clone 본체에서 n이 상수가 되어 rep movsq 재삽입
```

결과적으로 "노이즈 분리"가 풀려 v02와 동일 현상 재발.

### `noclone` 추가로 해결

IPA-CP가 clone을 못 만들게 막음 → 본체의 `n`은 항상 **runtime 값 유지** → `call memcpy@plt`로 glibc에 위임 → SIMD 언롤 경로 사용.

어셈블리로 검증:

```
ringbuf::v03::detail::copy_bytes(...):
    endbr64
    jmp    <memcpy@plt>    ← tail-jump, rep movsq 없음
```

## 한계: zero-copy 경로엔 적용 안 됨

v03 개선은 **copy-based 메서드**(`read`/`write`/`peek`)에만 적용. `write_ptr()` + 사용자 `memcpy` + `move_*_pos` 조합은 래퍼 미경유 → v02와 동일 성능.

```cpp
// 이 경로는 v02와 동일 (사용자가 직접 std::memcpy 호출)
std::memcpy(rb.write_ptr(), src, n);
rb.move_write_pos(n);
```

**실무 의미**: 실제 recv/send 직후 copy가 있는 경로에서만 이득. 완전 zero-copy 파이프라인이면 v02/v03 차이 없음.

## 측정 방법론

v02와 동일:

- **`perf stat -e cycles,instructions`**: IPC가 2~3 범위로 정상화되었는지 확인
- **`objdump -d`**: `copy_bytes` 심볼이 단일(clone 없음), 본체가 `jmp memcpy@plt`인지 확인

## API

v02와 동일. 구현만 바뀜.

| 카테고리       | 함수                                                                                         |
| :------------- | :------------------------------------------------------------------------------------------- |
| 상태           | `capacity()` (static constexpr), `size`, `available`, `empty`, `full`, `clear`               |
| zero-copy      | `read_ptr`, `write_ptr`, `readable_size`, `writable_size`, `move_read_pos`, `move_write_pos` |
| raw I/O (bool) | `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`                                        |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

스크립트: [../script/run_bench_v03.sh](../script/run_bench_v03.sh)

### 결과 (v01 / v02 / v03 비교, WriteRead)

| chunk | v01 (runtime) | v02 (rep movsq) | v03 (래퍼) |    v03 vs v02 |
| ----: | ------------: | --------------: | ---------: | ------------: |
|    16 |       24.1 ns |        1.08 ns† |    7.14 ns |   — (elision) |
|    64 |       24.1 ns |         3.88 ns |    5.88 ns |     1.5× 느림 |
|   256 |       27.1 ns |         12.3 ns |    14.4 ns |     1.2× 느림 |
|  1024 |       40.7 ns |         57.5 ns |    29.1 ns | **2.0× 빠름** |
|  4096 |        102 ns |         189 ns‡ |  **96 ns** | **2.0× 빠름** |
|  8192 |        245 ns |         339 ns‡ |     243 ns | **1.4× 빠름** |
| 16384 |        836 ns |          836 ns |     831 ns |          동일 |
| 32768 |       1905 ns |         1645 ns |    1720 ns |          비슷 |
| 65536 |       3682 ns |         3951 ns |    3792 ns |          비슷 |

† v02 chunk=16은 elision 의심 (1 ns 이하는 실제 memcpy로 불가)
‡ v02의 `rep movsq` 인라인 구간

**v03 chunk=4096은 v01(102 ns)보다도 빠름** — bitmask 이득(`%` → `&`)을 유지하면서 `rep movsq` 제거로 SIMD 언롤 경로 복귀. **두 이득의 조합** 실현.

### ZeroCopy는 v02와 동일 (예상대로)

| chunk | v02 ZeroCopy | v03 ZeroCopy |
| ----: | -----------: | -----------: |
|  4096 |      98.9 ns |      98.9 ns |
|  8192 |       182 ns |       182 ns |

`write_ptr + memcpy + move_*_pos` 경로는 래퍼 미경유 → 개선 적용 안 됨. **v03 개선은 copy-based 메서드(`read`/`write`/`peek`) 전용**.

### 검증: IPC 회복

```bash
sudo perf stat -e cycles,instructions \
  taskset -c 2 ./build/release/bin/bench_ringbuf_v03 \
  --benchmark_filter='BM_WriteRead<8192>$' --benchmark_min_time=3s
```

|                 | v02 (rep movsq) | v03 (jmp memcpy@plt) |
| :-------------- | --------------: | -------------------: |
| Time (chunk=4K) |         76.3 ns |            77.0 ns\* |
| Instructions    |           1.26B |              56.11 B |
| Cycles          |           20.5B |               18.9 B |
| **IPC**         |        **0.06** |          **2.96** ✅ |

\*벤치 크기 차이로 시간은 근소. 중요한 건 **IPC 50배 회복**.

IPC 정상화 = 마이크로코드 serialization 없음 = OoO 실행 복귀.

### objdump 증거

```
0000000000010060 <ringbuf::v03::detail::copy_bytes(...)>:
   10060: endbr64
   10064: jmp    <memcpy@plt>   ← tail-jump, rep movsq 없음
   10069: nopl   0x0(%rax)
```

- `rep movsq` 사라짐
- clone 심볼 없음 (단일 심볼만) → `noclone` 작동
- `call memcpy@plt` → glibc 런타임 dispatch

### 교훈

1. **IPA-CP는 함수 경계 넘어 전파**: `noinline` 하나론 부족. `copy_bytes.constprop.N` clone이 생기면 원점. `noclone`이 결정적
2. **컴파일러 최적화 회피는 어셈블리 확인 필수**: GCC의 `no_builtin` attribute는 지원 안 되는 등 제약 많음. `objdump`로 최종 확인이 유일한 방법
3. **조합 이득**: bitmask(`& (N-1)`) + runtime dispatch memcpy = v01 대비 **chunk=4096에서 6% 빠름, chunk=1024에서 40% 빠름**
4. **트레이드오프**: 작은 chunk(<256)에선 `call memcpy@plt` 오버헤드로 v02의 `rep movsq`보다 약간 느림. 실무에선 메시지/패킷 크기가 주로 수백 B~수 KB라 v03 이득 영역이 맞음

### 미해결

- `noclone`은 **gcc 전용** — clang은 unknown attribute. `RINGBUF_NOINLINE_NOCLONE` 매크로로 컴파일러별 분기했지만, clang 빌드에선 `rep movsq` 재발 가능. clang IPA-CP가 gcc보다 덜 공격적이라 실제 영향은 적을 것으로 예상
