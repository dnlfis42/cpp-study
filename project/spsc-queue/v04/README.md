# SPSC Queue v04

## v02 대비 변화

**cached index — cross-core read 횟수 감소 시도.**

```cpp
// v02: 매번 반대쪽 인덱스를 읽음
bool push(const T& val) {
    std::size_t head = head_.load(acquire);  // 항상 읽음
    if (((tail_ + 1) & (N - 1)) == head) return false;
    ...
}

// v04: 캐시된 값으로 먼저 확인, 필요할 때만 읽음
bool push(const T& val) {
    std::size_t next = (tail_ + 1) & (N - 1);
    if (next == head_cached_) {
        head_cached_ = head_.load(acquire);  // full일 때만 읽음
        if (next == head_cached_) return false;
    }
    ...
}
```

v03의 `alignas(64)`는 제거 — false sharing이 병목이 아님이 v03에서 확인됨.

## 핵심 설계

producer는 `head_cached_`(로컬), consumer는 `tail_cached_`(로컬)를 유지한다. 각자 자신의 캐시를 먼저 확인하고, 경계 조건(full/empty)에 도달했을 때만 반대쪽 atomic 인덱스를 읽는다.

```
push 성공 경로 (큐에 공간 있음):
  next != head_cached_ → head_ 읽기 없이 즉시 push
  → cross-core acquire load 0회

push 실패 경로 (큐가 꽉 참):
  next == head_cached_ → head_.load(acquire) 1회
  → 여전히 full → return false
```

cross-core read를 줄여서 캐시라인 ping-pong 빈도를 낮추는 것이 목표.

## API

v02와 동일. 시그니처 변화 없음.

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2,4`

스크립트: [../../script/run_spscq_v04_bench.sh](../../script/run_spscq_v04_bench.sh)

### 실측

|     N | v02 (atomic) | v04 (cached) |              차이 |
| ----: | -----------: | -----------: | ----------------: |
|  1024 |     ~81.6 ns |       ~98 ns | **+17 ns (악화)** |
|  4096 |     ~82.5 ns |       ~97 ns |     +15 ns (악화) |
| 16384 |     ~81.1 ns |      ~104 ns |     +23 ns (악화) |

**N이 클수록 더 느림** — v01~v03에서는 N 무관이었으나 처음으로 N 의존성 등장.

### 원인 분해

**벤치가 cached index의 이득 경로를 타지 않는다.**

이 벤치에서 producer는 쉬지 않고 스핀하고 consumer는 pop 1회씩만 한다. 큐는 시작 직후 가득 차고 그 상태가 유지된다. 결과적으로:

- `next == head_cached_`가 **항상 true** → 매번 `head_.load(acquire)` 실행
- 캐시 덕분에 읽기를 아끼는 경로가 전혀 탐색되지 않음
- 오히려 `next == head_cached_` 비교 연산만 추가 — v02보다 느림

**N이 클수록 느린 이유**: N=16384이면 `buf_`가 64KB로 L1(32KB)을 초과. 데이터 캐시라인이 L1 미스를 일으켜 이동 비용이 추가됨.

### 관찰

- **최적화는 workload 의존적**: cached index는 producer/consumer 속도가 비슷하고 큐가 적당히 차있을 때 효과가 있음. 항상 full인 상황에선 오히려 역효과.
- **벤치 설계가 중요**: producer 스핀 + consumer 1회 pop은 실제 workload를 대표하지 않음. 최적화 효과를 측정하려면 균형 잡힌 벤치가 필요함.
- **N 의존성 등장**: 처음으로 N이 성능에 영향을 줌 — L1 캐시 초과 여부가 원인.
