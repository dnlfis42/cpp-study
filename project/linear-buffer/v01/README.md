# v01

## 설계 의도

- **raw I/O bool all-or-nothing**: 부분 성공 없음. 실패 시 상태 변경 없음.
- **operator error state**: 실패를 sticky `fail_` 플래그로 기록. iostream 패턴과 동일. `if (!lb)`로 확인, `clear()`로 리셋.
- **zero-copy 접근**: `write_ptr()` + `move_write_pos(n)`으로 외부 recv 후 커밋. `read_ptr()` + `move_read_pos(n)`으로 내부 참조 후 소비.

## 측정

### RawWrite vs ZeroCopyWrite

`write(byte*, n)` (memcpy 래퍼)과 `write_ptr()` + `move_write_pos(n)` (직접 memcpy) 두 쓰기 경로 비교.

**가설**

`write()`는 `available()` 체크와 함수 호출이 추가되므로 ZeroCopyWrite보다 약간 느릴 것. 단, 인라인 가능하므로 오버헤드는 미미할 것.

**측정 결과**

RawWrite:

| 조건    |    mean |    CV | 비고           |
| :------ | ------: | ----: | :------------- |
| n=64    | 2.71 ns | 0.41% | 22.0 Gi/s      |
| n=4096  | 51.1 ns | 1.12% | 74.8 Gi/s (L1) |
| n=32768 |  965 ns | 0.30% | 31.6 Gi/s (L2) |
| n=65536 | 1920 ns | 0.25% | 31.8 Gi/s (L2) |

ZeroCopyWrite:

| 조건    |    mean |    CV | 비고           |
| :------ | ------: | ----: | :------------- |
| n=64    | 2.35 ns | 0.04% | 25.4 Gi/s      |
| n=4096  | 50.8 ns | 1.03% | 75.1 Gi/s (L1) |
| n=32768 |  965 ns | 0.17% | 31.7 Gi/s (L2) |
| n=65536 | 1930 ns | 0.47% | 31.6 Gi/s (L2) |

**분석**

n ≥ 4096에서 두 경로 수치가 사실상 동일하다. `write()`의 `available()` 체크와 함수 호출이 완전히 인라인·최적화되어 오버헤드가 제거된다. 가설의 "약간 느릴 것"은 틀렸고 "오버헤드 미미"는 맞았다.

n=64 구간은 두 경로 간 차이가 보이나(2.71 ns vs 2.35 ns) microarchitectural 요인이 지배하는 구간이라 수치 해석에 주의가 필요하다.

**결론**

n ≥ 4 KiB에서 `write()`는 직접 memcpy와 동등하다. 래퍼 비용은 측정 수준에서 없다.

## 개선

raw I/O와 operator 직렬화를 단일 버퍼 위에 통합했다. 두 인터페이스가 `fail_` 상태를 공유하여 혼용 시에도 일관된 오류 처리가 가능하다.

## 트레이드오프

`clear()`가 위치와 오류 상태를 함께 리셋한다. 오류 상태만 초기화하는 경로는 없다.

## 과제

- 읽기 경로에 zero-copy가 없다. `read(byte*, n)` / `peek(byte*, n)`은 항상 memcpy.
