# SPSC Queue v03

## v02 대비 변화

**`alignas(64)`로 false sharing 제거 시도.**

```cpp
// v02
std::atomic<std::size_t> head_;
std::atomic<std::size_t> tail_;

// v03
alignas(64) std::atomic<std::size_t> head_;
alignas(64) std::atomic<std::size_t> tail_;
```

## 설계 의도

`head_`와 `tail_`이 같은 캐시라인에 있으면, producer가 `tail_`을 쓸 때 consumer 코어의 캐시라인이 무효화되고, consumer가 `head_`를 쓸 때 producer 코어가 무효화된다. `alignas(64)`로 각각을 별도 캐시라인에 배치해 이 false sharing을 제거하는 것이 목표였다.

## API

v02와 동일. 시그니처 변화 없음.

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2,4`

스크립트: [../../script/run_spscq_v03_bench.sh](../../script/run_spscq_v03_bench.sh)

### 실측

|     N | v02 (atomic) | v03 (alignas) |               차이 |
| ----: | -----------: | ------------: | -----------------: |
|  1024 |     ~81.6 ns |      ~85.0 ns | **+3.4 ns (악화)** |
|  4096 |     ~82.5 ns |      ~84.0 ns |     +1.5 ns (악화) |
| 16384 |     ~81.1 ns |      ~84.3 ns |     +3.2 ns (악화) |

CV는 v02의 ~4% → v03의 ~0.3%로 안정. 수치 자체는 신뢰할 수 있음.

### 원인 분해

가설이 틀렸다. **`head_`/`tail_` false sharing은 병목이 아니었다.**

v02의 ~81 ns 병목은 **데이터 캐시라인 이동**이다:

- producer가 `buf_[tail_]`에 쓰면 해당 캐시라인이 producer 코어에서 Modified 상태가 됨
- consumer가 `buf_[head_]`에서 읽으려면 MESI 프로토콜에 의해 그 캐시라인을 producer 코어에서 가져와야 함
- 이건 실제 데이터 전달의 본질적인 비용 — `alignas(64)`로는 제거 불가능

`alignas(64)`는 잘못된 문제를 해결했다. 오히려 struct 크기가 커져 (~24 bytes → ~192 bytes) 미미하게 느려졌다.

### 관찰

- **false sharing 제거가 항상 답은 아님**: 병목이 어디 있는지 먼저 확인해야 함
- **SPSC의 본질적 비용**: producer→consumer 데이터 전달은 캐시라인 이동을 수반함. 이 비용이 지배적일 때 인덱스 패딩은 효과 없음
- **측정이 중요**: 가설을 세우고 측정으로 반증한 사례 — ~81 ns가 false sharing 때문이라는 가설이 기각됨
