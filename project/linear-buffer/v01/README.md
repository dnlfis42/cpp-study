# v01

## 설계 의도

- **raw I/O bool all-or-nothing**: 부분 성공 없음. 실패 시 상태 변경 없음.
- **operator error state**: 실패를 sticky `fail_` 플래그로 기록. iostream 패턴과 동일. `if (!lb)`로 확인, `clear()`로 리셋.
- **zero-copy 접근**: `write_ptr()` + `move_write_pos(n)`으로 외부 recv 후 커밋. `read_ptr()` + `move_read_pos(n)`으로 내부 참조 후 소비.

## 벤치마크

### 1. RawWrite

`write(byte*, n)`: memcpy 경로.

**결과**

| 시나리오 |    mean |    CV | 비고      |
| :------- | ------: | ----: | :-------- |
| n=64     | 2.71 ns | 0.41% | 22.0 Gi/s |
| n=4096   | 51.1 ns | 1.12% | 74.8 Gi/s |
| n=32768  |  965 ns | 0.30% | 31.6 Gi/s |
| n=65536  | 1920 ns | 0.25% | 31.8 Gi/s |

### 2. ZeroCopyWrite

`write_ptr()` + `move_write_pos(n)`: 버퍼 내부 직접 기록, memcpy 없음.

**결과**

| 시나리오 |    mean |    CV | 비고      |
| :------- | ------: | ----: | :-------- |
| n=64     | 2.35 ns | 0.04% | 25.4 Gi/s |
| n=4096   | 50.8 ns | 1.03% | 75.1 Gi/s |
| n=32768  |  965 ns | 0.17% | 31.7 Gi/s |
| n=65536  | 1930 ns | 0.47% | 31.6 Gi/s |

**결론**

n ≥ 4096에서 두 경로 차이 없음: `write()`의 `available()` 체크와 함수 호출이 완전히 인라인·최적화됨.
