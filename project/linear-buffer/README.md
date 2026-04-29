# Linear Buffer

고정 용량 선형 바이트 버퍼. 메시지 단위 직렬화/역직렬화에 사용.

- **linear (non-ring)**: `read_pos` / `write_pos` 단방향 전진, `clear()`로 리셋
- **C++20** (char8_t, 이후 버전에서 std::span)
- 네트워크 프로토콜 메시지 한 단위를 담는 용도

## 버전 히스토리

| 버전        | 주요 변화                                                                             | 결과                                             |
| :---------- | :------------------------------------------------------------------------------------ | :----------------------------------------------- |
| [v01](v01/) | 기본 구현. raw I/O (bool all-or-nothing) + arithmetic `operator<<`/`>>` (error state) | n=4096: 74.8 Gi/s (L1). n≥32768: ~31.7 Gi/s (L2) |
| [v02](v02/) | zero-copy read API: `read_span`, `read(n)`, `peek(n)` (span 반환)                     | 기존 경로 수치 동일. span API가 memcpy 생략      |

## API 요약

| API                                              | 역할                                            | 버전 |
| :----------------------------------------------- | :---------------------------------------------- | :--- |
| **operator**                                     |                                                 |      |
| `operator bool`                                  | 오류 상태 확인                                  | v01~ |
| `operator<<` / `operator>>`                      | 산술 타입 직렬화/역직렬화. 실패 시 `fail_` 설정 | v01~ |
| **interface**                                    |                                                 |      |
| `capacity()`, `size()`, `available()`, `empty()` | 상태 조회                                       | v01~ |
| `read_ptr()`, `write_ptr()`                      | 위치 포인터                                     | v01~ |
| `read_span()`                                    | 읽을 수 있는 영역 전체 뷰                       | v02~ |
| `set_fail()`                                     | 오류 상태 설정                                  | v01~ |
| `move_read_pos(n)` ,`move_write_pos(n)`          | n 바이트 위치 전진                              | v01~ |
| `clear`                                          | 위치·오류 상태 초기화                           | v01~ |
| `peek(byte*, n)`                                 | n 바이트 복사, 위치 이동 없음                   | v01~ |
| `peek(n)`                                        | n 바이트 뷰, 위치 이동 없음                     | v02~ |
| `read(byte*, n)`                                 | n 바이트 복사 + 소비                            | v01~ |
| `read(n)`                                        | n 바이트 뷰 + 소비                              | v02~ |
| `write(byte*, n)`                                | n 바이트 기록                                   | v01~ |

### 사용자 타입 확장 패턴

비멤버 오버로드로 확장:

```cpp
LinearBuffer& operator<<(LinearBuffer& lb, const MyType& v) {
    return lb << v.field1 << v.field2;
}
LinearBuffer& operator>>(LinearBuffer& lb, MyType& v) {
    return lb >> v.field1 >> v.field2;
}
```

## 성능 종합

| n (bytes) | 버퍼 크기 |            RawWrite |    CV |       ZeroCopyWrite |    CV | 비고           |
| --------: | --------: | ------------------: | ----: | ------------------: | ----: | :------------- |
|        64 |     128 B | 2.71 ns (22.0 Gi/s) | 0.41% | 2.35 ns (25.4 Gi/s) | 0.04% | microarch 지배 |
|      4096 |     8 KiB | 51.1 ns (74.8 Gi/s) | 1.12% | 50.8 ns (75.1 Gi/s) | 1.03% | L1             |
|     32768 |    64 KiB |  965 ns (31.6 Gi/s) | 0.30% |  965 ns (31.7 Gi/s) | 0.17% | L2             |
|     65536 |   128 KiB | 1920 ns (31.8 Gi/s) | 0.25% | 1930 ns (31.6 Gi/s) | 0.47% | L2             |

v01·v02 수치 동일: span API 추가가 기존 경로에 영향 없음

## 핵심 발견

### 1. 주파수 고정이 측정 안정성의 열쇠

**관찰**

`performance` governor만으로는 부족하다. boost/throttle 사이클로 동일 크기도 10~15% 편차가 발생했다.

**분석**

주파수 상한을 base clock 수준(3.0 GHz)으로 잠그면 CV < 1%의 안정된 측정이 가능하다. 절대 성능은 낮아지지만 상대 비교가 정확해진다.

**결론**

벤치마크 환경에서는 governor 설정만으로는 불충분하다. 주파수 상한 고정까지 필요하다.

### 2. 작은 크기 벤치는 microarchitectural 지배

**관찰**

n ≤ 256 영역(1~5 ns)은 수치 편차가 크고 재현이 어렵다.

**분석**

branch predictor 상태, OoO 스케줄링, turbo 변동 등 microarchitectural 요인이 지배하는 구간이다. 개별 수치보다 "극히 작다"는 사실 자체가 의미를 가진다.

**결론**

실질 성능 평가는 n ≥ 4 KiB 대역에서 수행할 것.

### 3. 캐시 계층 전환이 수치에 선명히 보임

**관찰**

- n = 4096 (8 KiB 버퍼): ~75 Gi/s
- n ≥ 32768: ~31 Gi/s 수렴

**분석**

두 구간의 경계는 L1 → L2 전환에 해당한다. 수치 변화가 캐시 계층 구조를 실측으로 반영한다.

**결론**

학습용 벤치로 메모리 계층 구조를 직접 확인할 수 있다.

### 4. bool all-or-nothing이 raw I/O에 적합

**관찰**

partial transfer(size_t 반환)를 초기에 고려했으나, 실제 쓰임은 세 시나리오 모두 all-or-nothing 이었다.

- recv: `recv()` 반환값으로 `move_write_pos()` 직접 처리 (`write()` 경로 미사용)
- send: 메시지 완결 필요 — 일부 전송은 의미 없음
- 헤더 파싱: 부족하면 `break` — 부분 peek 불필요

**분석**

세 경우 모두 성공/실패 이분법으로 충분하다. size_t 반환은 오히려 호출자에게 불필요한 분기를 강제한다.

**결론**

bool 시그니처로 통일하면 linear와 ring이 같은 의미로 맞춰진다.
partial 처리가 필요한 시나리오가 추가되면 재검토할 것.

### 5. zero-copy read (v02)의 DoNotOptimize 함정

**관찰**

`BM_ReadSpan`(write + span-read)이 `BM_RawWrite`(write)보다 빠르게 측정되었다.

**분석**

`DoNotOptimize(lb)`는 객체 전체를 관찰 강제하지만, `DoNotOptimize(span.data())`는 포인터만 관찰한다. 벤치 구조 자체가 결과에 영향을 주었다.

**결론**

해석 시 DoNotOptimize 대상 범위를 반드시 확인해야 한다.

### 6. operator error state가 raw I/O bool과 일관성 있음

**관찰**

`operator<<`/`>>`도 raw I/O와 동일하게 `fail_` 플래그로 실패를 기록한다.

**분석**

iostream sticky-flag 패턴과 동일하다. throw와 달리 연산을 체인하다가 마지막에 `if (!lb)` 한 번으로 확인할 수 있다.

**결론**

사용자 정의 오버로드도 `lb.set_fail()`으로 동일하게 통합되어 인터페이스 일관성이 유지된다.

## 미해결 / 다음 단계

- **primitive `operator<<`/`>>` 벤치**: elision 때문에 신뢰 가능한 수치 확보 어려움.
