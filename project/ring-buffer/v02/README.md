# Ring Buffer v02

## v01 대비 변화

`capacity`를 **컴파일 타임 템플릿 파라미터**로 이동 + **비트마스크** 치환.

```cpp
// v01
class RingBuffer {
    RingBuffer(std::size_t capacity);
    std::size_t capacity_;       // runtime
    // tail_ = (tail_ + n) % capacity_;  // div 명령
};

// v02
template <std::size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    static constexpr std::size_t capacity() { return N; }
    // tail_ = (tail_ + n) & (N - 1);    // and 명령
};
```

## 설계 의도

### compile-time capacity

1. **`%` → `&` 치환 가능**: runtime `% capacity_`는 `div` 명령(~20 cycle)이지만 `& (N-1)`는 `and` 1 cycle
2. **타입 안전성**: `RingBuffer<16>`과 `RingBuffer<32>`는 다른 타입 — 혼용 시 컴파일 차단
3. **`capacity()` constexpr**: 호출자가 상수로 사용 가능

### N은 2의 거듭제곱 강제

`static_assert((N & (N - 1)) == 0)` — 비트마스크 wrap이 유효하려면 필수.

## 예상되는 성능 함정

⚠ **compile-time memcpy 크기는 컴파일러가 `rep movsq`를 인라인할 수 있음.**

- `memcpy(dst, src, n)`에서 `n`이 compile-time 상수로 전파되면
- gcc가 `rep movsq` (마이크로코드 문자열 복사) 인라인 가능
- **4KB 규모에서 SIMD 언롤 대비 느릴 수 있음** (IPC 0.05 수준으로 급락)
- 작은 chunk(≤256B)에선 `%` → `&` 이득 > `rep movsq` 페널티로 빨라지지만,
- 중간 chunk(1~4KB)에서 **역전**될 가능성 있음

이 현상은 **벤치로 확인**하고, 발견되면 v03에서 `[[gnu::noinline, gnu::noclone]]` 래퍼로 회피 예정.

## 측정 방법론

단순 시간 측정으론 원인 파악 불가. 다음 조합 필요:

- **`perf stat -e cycles,instructions,branches,branch-misses`**: IPC 산출 → `rep movsq` 징후는 IPC 극저(0.05)
- **`objdump -d`**: 실제 생성된 명령어 확인 (`rep movsq` 직접 확인 가능)
- 숫자만 보고 가설 세우면 안 됨 — 이 현상은 "SIMD가 느리다"로 오해되기 쉬움

## API

v01과 동일 (시그니처만 template 인스턴스로 바뀜):

| 카테고리       | 함수                                                                                         |
| :------------- | :------------------------------------------------------------------------------------------- |
| 상태           | `capacity()` (static constexpr), `size`, `available`, `empty`, `full`, `clear`               |
| zero-copy      | `read_ptr`, `write_ptr`, `readable_size`, `writable_size`, `move_read_pos`, `move_write_pos` |
| raw I/O (bool) | `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`                                        |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

`BENCHMARK_TEMPLATE<N>` 형식으로 N = 32, 128, 512, 2048, 8192, 16384, 32768, 65536, 131072.
실제 전송 chunk = N/2 (wrap 없는 상황).

벤치 항목:

- **BM_WriteRead<N>**: `write(N/2) + read(N/2)` — copy 기반
- **BM_ZeroCopy<N>**: `memcpy(write_ptr, ...) + move_*_pos` — 단방향 memcpy

스크립트: [../script/run_bench_v02.sh](../script/run_bench_v02.sh)

### 결과 (v01 vs v02, 동일 chunk)

**ZeroCopy 비교:**

| chunk | v01 (runtime) | v02 (compile-time) |      비율 |
| ----: | ------------: | -----------------: | --------: |
|    16 |       19.3 ns |       **0.72 ns**† |  27× 빠름 |
|    64 |       19.3 ns |       **1.34 ns**† |  14× 빠름 |
|   256 |       20.6 ns |            6.44 ns | 3.2× 빠름 |
|  1024 |       28.7 ns |            26.1 ns |      비슷 |
|  4096 |       57.2 ns |       **98.9 ns**‡ | 1.7× 느림 |
|  8192 |        107 ns |        **182 ns**‡ | 1.7× 느림 |
| 16384 |        418 ns |             415 ns |      비슷 |
| 32768 |        979 ns |             972 ns |      비슷 |
| 65536 |       1989 ns |            2019 ns |      비슷 |

† elision 의심 수치 (1 ns 이하는 실제 memcpy로 불가능)
‡ **`rep movsq` 인라인으로 역전** — 아래 검증 참고

### 검증: `perf stat`로 IPC 측정

`rep movsq` 진단에 결정적인 시그널은 **IPC 극저**:

```bash
sudo perf stat -e cycles,instructions,branches,branch-misses \
  taskset -c 2 ./build/release/bin/bench_ringbuf_v02 \
  --benchmark_filter='BM_ZeroCopy<8192>$' --benchmark_min_time=3s
```

|              | v01 (chunk=4096) | v02 (chunk=4096) |
| :----------- | ---------------: | ---------------: |
| Time         |          45.7 ns |      **76.3 ns** |
| Instructions |            47.3B |       **1.26 B** |
| Cycles       |            18.2B |            20.5B |
| **IPC**      |             2.61 |         **0.06** |
| insn/iter    |             ~503 |              ~22 |

**IPC 40배 차이**는 마이크로코드 `rep movsq` 시그니처:

- v01: SIMD 언롤 루프 (`vmovdqu xmm` 반복) → 명령어 많고 IPC 높음
- v02: `rep movsq` 하나가 4KB 처리 → 명령어 극소, 마이크로코드 serialization으로 IPC 붕괴

### objdump 증거

```
$ objdump -d --demangle build/release/bin/bench_ringbuf_v02 \
  | grep -A 50 'BM_ZeroCopy<8192ul>' | grep -E 'rep|memcpy'

db5c:  f3 48 a5   rep movsq %ds:(%rsi),%es:(%rdi)
```

v02 본체에 `rep movsq` 인라인 직접 확인.

### 교훈

1. **"compile-time 정보 많음 = 최적화 많음"이 아님** — 컴파일러가 보수적 경로(`rep movsq`) 선택 가능
2. **IPC 0.05~0.1 = 마이크로코드 의심** — `rep movs*`, `div`, `cpuid` 등
3. **숫자만 보면 가설 방향이 틀릴 수 있음** — 이 현상은 "SIMD가 느리다"로 오해되기 쉬움
4. **objdump로 최종 증거** — 추측 대신 실제 명령어 확인

### v03에서 해결

`[[gnu::noinline, gnu::noclone]]` 래퍼로 `std::memcpy` 호출을 감싸 `rep movsq` 인라인 차단 예정.

- `noinline`: 호출 지점 인라인 금지
- `noclone`: IPA-CP가 compile-time n을 constprop clone으로 만드는 것 차단
- 결과: 본체의 `n`이 항상 runtime → `call memcpy@plt` → glibc 런타임 dispatch (SIMD 언롤)
