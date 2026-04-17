# Object Pool v03

## v02 대비 변화

free list 자료구조를 **인덱스 기반 링크드 리스트**로 교체.

```cpp
// v01/v02: 별도 벡터를 스택으로
std::vector<T> storage_;
std::vector<std::size_t> free_list_;  // 빈 인덱스 스택

// v03: Node에 next 인덱스를 intrusive하게 심음
struct Node {
    T data;
    std::size_t next;   // 빈 슬롯일 때만 의미 — 다음 빈 Node 인덱스
};
std::vector<Node> storage_;
std::size_t head_free_;                // 첫 빈 Node의 인덱스
std::size_t available_;                // 명시적 카운터
static constexpr std::size_t SENTINEL = std::size_t(-1);
```

## 설계 의도

### 구조적 순수성

"free list"라는 이름에 **실제 링크드 리스트** 자료구조 매핑. 추상과 구현 일치 → 코드 리뷰/교육용 명확성.

### 메모리 지역성

`next`가 `T data` 바로 옆 슬롯(Node 내부) → **release 시 data와 next가 같은 캐시라인에 load**. v01/v02는 별도 벡터라 data touch와 free_list touch가 다른 라인.

### intrusive로 가는 브릿지

v05에서 `union Slot { T value; size_t next; }`로 **T와 next의 메모리 공유**로 진화. v03은 그 중간 단계 — **메모리는 공유 안 하지만 구조는 근접**.

## 예상되는 성능 함정

⚠ **단일 스레드 hot path에선 v02보다 느릴 가능성.**

- **sizeof(Node) = sizeof(T) + sizeof(size_t)**: T에 next가 추가로 붙어 슬롯 크기 증가 → cache stride 증가
- **간접 참조 추가**: `storage_[idx].next`로 next 획득, `storage_[idx].data`로 T 접근 — 포인터 연산 한 단계 더
- **별도 `available_` 카운터 갱신**: v01/v02는 `free_list_.size()`로 암시적이지만 여기선 명시적 증감 필요

구조적 이득(순수성, 캐시 지역성)이 있지만 **hot loop 산술 비용 증가**로 역전될 수 있음. 벤치로 확인.

## API

v02와 동일. 구현만 바뀜.

| 카테고리     | 함수                                                        |
| :----------- | :---------------------------------------------------------- |
| 상태         | `capacity()`, `available()`, `in_use()`                     |
| Raw          | `acquire() -> T*`, `release(T*)`                            |
| RAII         | `acquire_unique() -> Handle`                                |
| Type aliases | `Deleter` (nested class), `Handle = unique_ptr<T, Deleter>` |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

- **BM_Pool_Raw**: v01/v02와 동일 시나리오 — 자료구조 차이만 드러남
- **BM_Pool_Handle**: v02와 동일 Handle 경로

스크립트: [../script/run_bench_v03.sh](../script/run_bench_v03.sh)

### 실측

| 벤치                            |     v02 |         v03 |                차이 |
| :------------------------------ | ------: | ----------: | ------------------: |
| Raw (cap 64/1024/16384 동일)    | 2.44 ns | **3.02 ns** | **+0.58 ns (+24%)** |
| Handle (cap 64/1024/16384 동일) | 4.15 ns | **4.90 ns** | **+0.75 ns (+18%)** |

CV < 1%, 안정적. 예측대로 v02 대비 약 25% 느려짐 (원본 회고와 일치).

### 관찰

- **Raw가 더 크게 느려짐 (24%)**: Node 구조의 hot path 영향 직접 노출
- **Handle은 상대적 희석 (18%)**: Deleter 비용이 이미 섞여있어 비율은 더 작음
- **capacity 무관**: hot path는 `head_free_` 기준 LIFO 재사용 → 같은 슬롯 반복. cache stride 영향 직접 측정 안 됨 (큰 워크로드에선 나타날 것)
- 원인 분해:
  - `sizeof(Node) = sizeof(Item)(64) + sizeof(size_t)(8) = 72B` — 캐시 라인 2개 걸침
  - 간접 참조 한 단계 추가 (`storage_[idx].next` / `.data`)
  - 명시적 `available_` 카운터 store

### 평가

**성능 손해 (24%) vs 구조적 이득 (코드 명확성 + intrusive 브릿지)** 트레이드오프. 학습 맥락에선 감수할 만하지만, 실무 라이브러리 성능 목적이면 v01/v02 구조 유지가 나음.

v05에서 `union Slot`으로 **메모리 공유**하면 sizeof 이득 회복 가능 — 대신 생성자 제어 복잡도 증가. 각 선택의 비용과 이득을 버전별로 분리 측정하는 게 이 연작의 목적.

## 다음 버전 힌트

- **v04**: Raw API를 private로 내리고 **Handle 전용**으로 API 축소. 타입 시스템으로 release 누락/cross-pool 반환 전면 차단
