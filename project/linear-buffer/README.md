# Linear Buffer

고정 용량 선형 바이트 버퍼. 메시지 단위 직렬화/역직렬화에 사용.

- **linear (non-ring)**: `read_pos` / `write_pos` 단방향 전진, `clear()`로 리셋
- **C++20** (char8_t, 이후 버전에서 std::span)
- 네트워크 프로토콜 메시지 한 단위를 담는 용도

## 성능 종합

쓰기 경로 (v01·v02 동일):

| n (bytes) | 버퍼 크기 |            RawWrite |    CV |       ZeroCopyWrite |    CV | 비고           |
| --------: | --------: | ------------------: | ----: | ------------------: | ----: | :------------- |
|        64 |     128 B | 2.71 ns (22.0 Gi/s) | 0.41% | 2.35 ns (25.4 Gi/s) | 0.04% | microarch 지배 |
|      4096 |     8 KiB | 51.1 ns (74.8 Gi/s) | 1.12% | 50.8 ns (75.1 Gi/s) | 1.03% | L1             |
|     32768 |    64 KiB |  965 ns (31.6 Gi/s) | 0.30% |  965 ns (31.7 Gi/s) | 0.17% | L2             |
|     65536 |   128 KiB | 1920 ns (31.8 Gi/s) | 0.25% | 1930 ns (31.6 Gi/s) | 0.47% | L2             |

환경: Intel (L1 32 KiB × 6), gcc 13.3, `-O3`, CPU 3.0 GHz 고정, `taskset -c 2`

## 버전

### v01

**개선**

raw I/O와 operator 직렬화를 단일 버퍼 위에 통합했다. 두 인터페이스가 `fail_` 상태를 공유하여 혼용 시에도 일관된 오류 처리가 가능하다. 쓰기 경로에 zero-copy 접근(`write_ptr()` + `move_write_pos(n)`)을 제공하며, 래퍼 비용은 n ≥ 4 KiB에서 측정 수준 없음(74.8 Gi/s).

**트레이드오프**

`clear()`가 위치와 오류 상태를 함께 리셋한다. 오류 상태만 초기화하는 경로는 없다.

**과제**

- 읽기 경로에 zero-copy 없음. `read(byte*, n)` / `peek(byte*, n)`은 항상 memcpy.

### v02

**개선**

읽기 경로에 zero-copy API(`read_span()`, `read(n)`, `peek(n)`)를 추가했다. 기존 쓰기 경로 회귀 없음 (n=4096: 74.7 Gi/s, v01과 동일).

**트레이드오프**

반환된 span의 수명 관리 책임이 호출자에게 있다. `clear()` 이후 접근은 dangling이며 컴파일러가 잡지 않는다.

**과제**

- span read 경로의 신뢰할 수 있는 벤치 없음. `DoNotOptimize(span.data())`가 객체 전체가 아닌 포인터만 관찰해 결과가 왜곡된다.
