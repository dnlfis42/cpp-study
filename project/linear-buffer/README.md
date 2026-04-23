# Linear Buffer

고정 용량 선형 바이트 버퍼. 메시지 단위 직렬화/역직렬화에 사용.

- **linear (non-ring)**: `read_pos` / `write_pos` 단방향 전진, `clear()`로 리셋
- **C++20** (char8_t, 이후 버전에서 std::span)
- 네트워크 프로토콜 메시지 한 단위를 담는 용도

## 버전 히스토리

|        버전 | 주요 변화                                                                                  | 결과                                                                                       |
| ----------: | :----------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------- |
| [v01](v01/) | 기본 구현. raw I/O (bool all-or-nothing) + primitive `operator<<`/`>>` (throw)             | 베이스라인. n=4K에서 75 Gi/s, n=8K에서 81 Gi/s 피크                                        |
| [v02](v02/) | zero-copy read API — `std::span<const std::byte>` 반환 (`read_span`, `read(n)`, `peek(n)`) | memcpy 회피. 큰 크기에서 bandwidth 수렴, DoNotOptimize 오버헤드 차이로 작은 크기 해석 주의 |

각 버전 세부 내용은 해당 디렉터리 README 참고.

## API 요약 (공통)

- 상태: `capacity()`, `size()`, `available()`, `empty()`, `clear()`
- zero-copy: `read_ptr()`, `write_ptr()`, `move_read_pos()`, `move_write_pos()`
- raw I/O (bool all-or-nothing): `read(byte*, n)`, `write(byte*, n)`, `peek(byte*, n)`
- primitive 직렬화 (실패 시 throw): `operator<<`, `operator>>` — 19종 오버로드
- **v02 추가**: `read_span()`, `read(n)`, `peek(n)` (span 반환)

## 사용자 타입 확장 패턴

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

환경: Intel (L1 32 KiB × 6), gcc 13.3, `-O3`, CPU 상한 **3.0 GHz 고정**, `taskset -c 2`

| n (bytes) | 버퍼 크기 |  RawWrite |      대역폭 | 위치        |
| --------: | --------: | --------: | ----------: | :---------- |
|      4096 |      8 KB |   50.7 ns |     75 Gi/s | L1          |
|  **8192** | **16 KB** | **94 ns** | **81 Gi/s** | **L1 peak** |
|     16384 |     32 KB |    335 ns |     46 Gi/s | L1 경계     |
|     32768 |     64 KB |    969 ns |     31 Gi/s | L2          |
|     65536 |    128 KB |   1979 ns |     31 Gi/s | L2          |

## 전체 발견

### 1. 주파수 고정이 측정 안정성의 열쇠

`performance` governor만으론 부족. **boost/throttle 사이클로 동일 크기도 10~15% 편차**. 주파수 상한을 base clock 수준(3.0 GHz)으로 잠그면 CV < 1%의 극도로 안정된 측정 가능. 절대 성능은 낮아지지만 **상대 비교가 정확**.

### 2. 작은 크기 벤치는 microarchitectural 지배

n ≤ 256 영역(1~5 ns)은 branch predictor 상태, OoO 스케줄링, turbo 변동 등에 지배됨. **개별 수치보다 "극히 작다"는 사실이 의미**. 실질 성능 평가는 n ≥ 4K 대역에서.

### 3. 캐시 계층 전환이 수치에 선명히 보임

- n=8192 (16 KB 버퍼): L1 cache-hot peak **81 Gi/s**
- n=16384 (32 KB 버퍼): L1 (32 KB) 한계 도달 → **46 Gi/s로 반토막**
- n ≥ 32768: L2 영역 수렴 **~31 Gi/s**

학습용 벤치로 **메모리 계층 구조를 실측으로 확인** 가능.

### 4. bool all-or-nothing이 raw I/O에 적합

partial transfer(size_t 반환)를 처음 고려했지만:

- recv 시나리오: `recv()` 반환값으로 `move_write_pos()` — `write()` 경로 미사용
- send 시나리오: 메시지 완결 필요 — "일부 전송"은 의미 없음
- 헤더 파싱: 부족하면 `break` — 부분 peek 불필요

실제 쓰임을 돌아보면 all-or-nothing이 자연스럽고, **linear와 ring이 같은 의미로 통일** 가능.

### 5. zero-copy read (v02)의 DoNotOptimize 함정

`BM_ReadSpan`(write + span-read)이 `BM_RawWrite`(write)보다 빠르게 나옴 — 역설적. 원인: `DoNotOptimize(lb)` 전체 객체 관찰 강제 vs `DoNotOptimize(span.data())` 작은 범위 관찰. **벤치 구조가 결과에 영향** — 해석 시 DoNotOptimize 대상 범위 확인 필수.

### 6. operator throw vs raw bool의 역할 분리

- `operator<<`/`>>`: 프로토콜 위반 = 치명적 → throw로 빠르게 실패
- `read`/`write`/`peek`: 가변 크기 데이터 처리 → bool로 호출자가 결정

**같은 "실패"도 맥락에 따라 시그널링 방식이 다른 게 합리적**.

## 측정 방법론

- **3.0 GHz 고정**: `cpupower frequency-set -u 3.0GHz`로 boost 차단
- **taskset -c 2**: 단일 코어 고정, 마이그레이션 회피
- **10 repetitions + aggregates_only**: CV로 안정성 판단
- **CV > 5% 구간은 해석 주의**: 외부 인터럽트 혹은 캐시 경계 경합

## 미해결 / 다음 단계

- **primitive `operator<<`/`>>` 벤치**: elision 때문에 신뢰 가능한 수치 확보 어려움. 이 연작에선 raw I/O만 측정
- **v03 방향 미정**: alignas, SBO, template<N> 검토했으나 학습 밀도 대비 가치 제한적 → **`concept/cache-alignment/` 같은 짧은 실험으로 분리 예정**

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test

# 벤치 (CPU 고정 + 반복)
./script/run_linbuf_v01_bench.sh
./script/run_linbuf_v02_bench.sh
```
