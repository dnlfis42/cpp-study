# SharedHandle 복사 성능 이상: 동일한 루프, 다른 성능

## 배경

측정 대상: `shandle_bench`

측정 환경:

- CPU: i7-9750H (Coffee Lake, 9th gen)
- 주파수: 3.0 GHz 고정 (`cpupower frequency-set`)
- 코어: core 2 (`taskset -c 2`)
- 컴파일러: GCC, `-O3`
- 날짜: 2026-04-30

결과:

```
BM_Copy_V01_mean    12.1 ns
BM_Copy_V02_mean    14.4 ns
```

## 이상 수치 발견

| 조건 |     V01 |     V02 |
| :--- | ------: | ------: |
| 복사 | 12.1 ns | 14.4 ns |
| 이동 | 2.03 ns | 2.07 ns |
| make | 24.5 ns | 24.9 ns |

이동·make는 오차 범위 내 동등한데 복사만 19% 차이난다. V01(non-intrusive)과 V02(intrusive)의 복사 루프는 어셈블리 수준에서 구조적으로 동일하다. 원인을 추적한다.

## 어셈블리 비교

### 핫 루프 확인

역방향 `jne`(현재 주소보다 낮은 주소로 점프)를 찾아 루프 시작 주소를 특정한다.

```bash
grep -n "BM_Copy_V01\|BM_Copy_V02" shandle_bench.asm
```

```
6090:    ba94:    75 ba    jne    ba50    ← V02 루프: ba50~ba94
6182:    bbe4:    75 ba    jne    bba0    ← V01 루프: bba0~bbe4
```

`.cold` 섹션의 `jne`는 예외 처리 경로로 핫 루프가 아니다.

### 루프 본체 추출

```bash
sed -n '/^\s*bba0:/,/^\s*bbe4:/p' shandle_bench.asm   # V01
sed -n '/^\s*ba50:/,/^\s*ba94:/p' shandle_bench.asm   # V02
```

```asm
; V01 (bba0~bbe4)
bba0:    mov    rbx, QWORD PTR [rsp+0x8]
bba5:    mov    QWORD PTR [rsp+0x10], rbx
bbaa:    test   rbx, rbx
bbad:    je     bbb3
bbaf:    lock add DWORD PTR [rbx], 0x1
bbb3:    mov    rax, QWORD PTR [rsp+0x10]
bbb8:    test   rax, rax
bbbb:    je     bbe0
bbbd:    lock sub DWORD PTR [rax], 0x1
bbc1:    jne    bbe0
...
bbe0:    sub    rbp, 0x1
bbe4:    jne    bba0

; V02 (ba50~ba94) — 명령어 순서, 오퍼랜드, 바이트 인코딩 완전히 동일
ba50:    mov    rbx, QWORD PTR [rsp+0x8]
...
ba94:    jne    ba50
```

명령어 구조가 완전히 동일하다. 차이는 루프 시작 주소뿐이다.

### 코드 정렬 확인

```
0xbba0 % 0x20 = 0x00  → V01: 32바이트 경계에 정렬
0xba50 % 0x20 = 0x10  → V02: 32바이트 경계에서 16바이트 오프셋
```

두 루프 모두 68바이트로 32바이트 블록 3개에 걸쳐 있다. 블록 수는 동일하지만 시작 주소가 다르므로 DSB set 매핑이 다르다.

## 데이터 캐시·분기 예측 확인

```bash
perf stat -e cycles,instructions,L1-dcache-load-misses,branch-misses \
    taskset -c 2 ./shandle_bench --benchmark_filter="BM_Copy_V0{1,2}" ...
```

| 항목                  |     V01 |     V02 |
| :-------------------- | ------: | ------: |
| L1-dcache-load-misses | 208,201 | 187,261 |
| branch-misses         | 141,953 | 151,122 |
| IPC                   |    0.34 |    0.28 |

L1 데이터 캐시 미스와 분기 예측 실패는 둘 다 극히 적고 거의 동일하다. 두 원인 모두 제외된다. 그러나 IPC가 다르다. 명령어 수는 동일한데 V02가 사이클을 더 소모한다. 파이프라인 stall이 원인이다.

## 프런트엔드 stall 확인

IDQ(Instruction Decode Queue) uop 공급 경로를 측정한다. `cycles`·`instructions`는 고정 카운터라 일반 PMU 슬롯을 사용하지 않으므로 multiplexing 없이 측정된다.

```bash
perf stat -e cycles,instructions,idq.dsb_uops,idq.mite_uops,idq.ms_uops \
    taskset -c 2 ./shandle_bench --benchmark_filter="BM_Copy_V0{1,2}" ...
```

정규화: `uops/iter = uops × cycles_per_iter ÷ total_cycles`

- V01: 12.1 ns × 3 GHz = 36.3 cycles/iter
- V02: 14.4 ns × 3 GHz = 43.2 cycles/iter

| 항목      | V01 /iter | V02 /iter |
| :-------- | --------: | --------: |
| dsb_uops  |      1.02 |     15.03 |
| mite_uops |      6.14 |      0.14 |
| ms_uops   |     16.12 |     16.07 |

**V01은 MITE, V02는 DSB가 비-lock uop을 공급한다.** ms_uops는 양쪽 동일(≈16)하므로 `lock` 실행 자체는 차이가 없다. lock 2개 × MS 8 uop = 16 uop/iter.

DSB가 더 빠른 공급 경로임에도 V02가 느리다. 역설이다.

## 가설: 코드 정렬이 DSB/MITE 경로를 결정한다

32바이트 정렬 여부가 DSB set 매핑을 바꾸고, 그것이 `lock` 이후 디코드 경로를 결정한다는 가설을 세운다.

검증: `-falign-loops=32`로 빌드해 V02 루프를 32바이트 정렬에 맞춘다.

```cmake
target_compile_options(shandle_bench PRIVATE -falign-loops=32)
```

align32 바이너리의 루프 시작 주소:

```
V02: 0xbb00 % 0x20 = 0x00  → 32바이트 정렬
V01: 0xbc50 % 0x20 = 0x10  → 16바이트 오프셋
```

원본과 정렬이 뒤바뀌었다.

### align32 벤치 결과

```
BM_Copy_V01_mean    14.4 ns    (원본: 12.1 ns)
BM_Copy_V02_mean    12.1 ns    (원본: 14.4 ns)
```

성능이 완전히 교체됐다.

### align32 IDQ uop

| 항목      | align32 V01 /iter | align32 V02 /iter |
| :-------- | ----------------: | ----------------: |
| dsb_uops  |             15.02 |              1.02 |
| mite_uops |              0.14 |              6.14 |
| ms_uops   |             16.07 |             16.08 |

DSB/MITE 경로도 완전히 교체됐다. ms_uops는 어느 경우에도 동일하다.

## 결론

| 조건        | 루프 정렬       | 디코드 경로 | 복사 성능 |
| :---------- | :-------------- | :---------- | --------: |
| 원본 V01    | 32바이트 정렬   | MITE        |   12.1 ns |
| 원본 V02    | 16바이트 오프셋 | DSB         |   14.4 ns |
| align32 V01 | 16바이트 오프셋 | DSB         |   14.4 ns |
| align32 V02 | 32바이트 정렬   | MITE        |   12.1 ns |

- 32바이트 정렬된 루프 → DSB 경로 → 느림 (14.4 ns)
- 16바이트 오프셋 루프 → MITE 경로 → 빠름 (12.1 ns)
- 구현(V01/V02) 차이가 아니라 코드 정렬이 성능을 결정한다
- ms_uops 동일: `lock` 실행 자체는 무관

## 미확인

- 32바이트 정렬이 DSB 경로로 이어지는 정확한 메커니즘 (DSB set 무효화 조건): Intel 내부 문서 없이 확정 불가
- DSB 경로(32바이트 정렬)에서 dsb_uops/iter가 15.03으로 MITE 경로(16바이트 오프셋)의 mite_uops/iter 6.14보다 약 8 더 많다. 루프 명령어가 동일한데 uop 총합이 다른 이유가 불명확하다. 같은 uop이 한 iteration 안에서 중복 공급됐을 가능성이 있으나 확인되지 않았다.

## 파일

| 파일                        | 설명                             |
| :-------------------------- | :------------------------------- |
| `shandle_bench`             | 원본 바이너리                    |
| `shandle_bench_align32`     | `-falign-loops=32` 적용 바이너리 |
| `shandle_bench.asm`         | 원본 디스어셈블리                |
| `shandle_bench_align32.asm` | align32 디스어셈블리             |
