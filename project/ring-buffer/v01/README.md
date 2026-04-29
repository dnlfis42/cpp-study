# v01

## 설계 의도

- **`unique_ptr<byte[]>` 선택**: `vector<byte>`도 충분하지만 소유권 구조를 명확히 하고 `size()` 이름을 "읽을 수 있는 바이트 수"로 예약하기 위해 선택했다. 성능 차이는 없다.
- **bool all-or-nothing I/O**: 실제 사용 패턴에서 partial transfer는 쓰이지 않는다. recv 시나리오는 `write_ptr` + `move_write_pos`, send/parse 시나리오는 완결 메시지 단위다. `linear-buffer`와 동일한 의미로 통일한다.
- **`readable_size` / `writable_size`**: wrap이 있는 원형 버퍼 전용 개념. `available()`은 총 가용 공간이지만 wrap 직전까지 연속으로 쓸 수 있는 구간은 별도로 필요하다. scatter/gather I/O의 `iovec`과 연결된다.

## 측정

### WriteRead vs ZeroCopy

`BM_WriteRead`: `write(n)` + `read(n)`, memcpy 2회, 대역폭 `n * 2` 기준.
`BM_ZeroCopy`: `memcpy(write_ptr, src, n)` + `move_write_pos` + `move_read_pos`, memcpy 1회, 대역폭 `n` 기준.

**가설**

WriteRead는 memcpy 2회이므로 ZeroCopy 대비 약 2배 시간이 걸릴 것. n = 16384(버퍼 32 KB = L1 크기)에서 대역폭 급락이 있을 것.

**측정 결과**

WriteRead:

| 조건    |    mean |    CV | 비고            |
| :------ | ------: | ----: | :-------------- |
| n=64    | 23.4 ns | 0.75% | 5.09 Gi/s       |
| n=256   | 26.5 ns | 0.50% | 17.96 Gi/s      |
| n=1024  | 40.6 ns | 0.33% | 47.01 Gi/s      |
| n=4096  |  101 ns | 1.31% | 75.38 Gi/s (L1) |
| n=16384 |  852 ns | 0.67% | 35.84 Gi/s (L2) |
| n=65536 | 3809 ns | 1.26% | 32.06 Gi/s (L2) |

ZeroCopy:

| 조건    |    mean |    CV | 비고            |
| :------ | ------: | ----: | :-------------- |
| n=64    | 19.2 ns | 0.11% | 3.11 Gi/s       |
| n=256   | 20.9 ns | 0.07% | 11.42 Gi/s      |
| n=1024  | 27.8 ns | 0.41% | 34.31 Gi/s      |
| n=4096  | 56.9 ns | 0.41% | 67.04 Gi/s (L1) |
| n=16384 |  415 ns | 0.83% | 36.81 Gi/s (L2) |
| n=65536 | 1995 ns | 0.33% | 30.60 Gi/s (L2) |

**분석**

n >= 4096에서 WriteRead 시간은 ZeroCopy의 1.8~2.1배다(n=4096: 101 / 56.9 = 1.78, n=16384: 852 / 415 = 2.05). memcpy 횟수 차이가 시간 비율로 직결된다. n=64에서는 1.22배에 그치는데, 함수 호출·인덱스 계산 오버헤드가 memcpy 비용 대비 커지기 때문이다.

ZeroCopy는 n=4096(버퍼 8 KB)에서 67.04 Gi/s로 정점이다. n=16384(버퍼 32 KB = L1 크기)에서 36.81 Gi/s로 급락하며 L2 대역폭으로 수렴한다.

**결론**

memcpy 횟수가 처리량 직결 인자다. L1 핫 구간의 상한은 n=4096(버퍼 8 KB)이다.

### Wrap 오버헤드

`BM_Wrap_WriteRead`: 버퍼 크기 `n + n / 2`, 초기 오프셋 `n / 2`로 write·read 경로에 wrap을 강제한다. split memcpy 두 번 호출이 반드시 발생한다.

**가설**

소/중 크기에서 split memcpy 두 번 호출 비용으로 오버헤드가 생길 것. 대역폭이 지배적인 대용량에서는 차이가 없을 것.

**측정 결과**

| 조건    | WriteRead | Wrap_WriteRead | 오버헤드 |
| :------ | --------: | -------------: | -------: |
| n=64    |   23.4 ns |        25.2 ns |     7.7% |
| n=256   |   26.5 ns |        29.4 ns |    10.9% |
| n=1024  |   40.6 ns |        44.2 ns |     8.9% |
| n=4096  |    101 ns |         105 ns |     4.0% |
| n=16384 |    852 ns |         831 ns |    -2.5% |
| n=65536 |   3809 ns |        3899 ns |     2.4% |

**분석**

n <= 1024에서 7~11% 오버헤드가 일관되게 나타난다. n=4096에서 4%로 줄고, n=16384에서 -2.5%로 반전된다. 이 반전은 두 벤치가 사용하는 버퍼 크기 차이(WriteRead: 2n = 32 KB, Wrap_WriteRead: 1.5n = 24 KB)로 인한 캐시 적재 패턴 차이로 해석된다. n=4096의 CV가 3.06%로 다른 지점(0.1~1.3%)보다 높아 wrap 경계 위치에 따른 캐시 라인 정렬 변동이 있음을 시사한다.

**결론**

wrap 오버헤드는 n <= 1024에서 ~10%, 대용량에서는 캐시 크기 차이가 결과를 역전시킬 수 있다.

## 개선

고정 용량 원형 바이트 버퍼의 기반을 확립한다. `write_ptr` + `move_write_pos` 경로로 zero-copy recv를 수용하고, bool all-or-nothing I/O로 `linear-buffer`와 동일한 사용 패턴을 유지한다.

## 트레이드오프

wrap을 지원하는 대가로 `% capacity_` 나눗셈과 `size_` 별도 관리 비용이 든다. `linear-buffer`와 동일 조건(단방향 memcpy) 기준으로 10~20% 느리다.

| 항목          | linear-buffer             | ring-buffer       |
| :------------ | :------------------------ | :---------------- |
| 위치          | 고정                      | 순환 (wrap)       |
| 상태 변수     | `read_pos_`, `write_pos_` | + `size_`         |
| `available()` | `capacity - write_pos`    | `capacity - size` |
| wrap 산술     | 없음                      | `% capacity_`     |

## 과제

- `% capacity_` 나눗셈: capacity를 2의 거듭제곱으로 고정하면 `& (N-1)` 비트마스크로 대체 가능.
- `size_` 별도 관리: empty/full 구분을 위해 3변수를 유지한다. 시퀀스 카운터 방식으로 제거 가능.
