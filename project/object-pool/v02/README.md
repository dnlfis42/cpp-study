# Object Pool v02

## v01 대비 추가

**RAII Handle**: `std::unique_ptr<T, Deleter>` 타입 별칭. `acquire_unique()`가 소멸 시 자동으로 `release` 호출하는 Handle 반환.

```cpp
{
    auto h = pool.acquire_unique();
    *h = 42;
    // ...
} // 스코프 이탈 → Handle 소멸 → release 자동 호출
```

Raw API (`acquire`/`release`)는 **그대로 병행 제공** — hot path 최적화 vs 안전성 선택권.

## 설계 결정

### Deleter는 pool 포인터를 멤버로

```cpp
class Deleter {
public:
    Deleter() noexcept : pool_{nullptr} {}
    explicit Deleter(ObjectPool* pool) noexcept : pool_{pool} {}
    void operator()(T* obj) const noexcept {
        if (pool_ != nullptr && obj != nullptr) pool_->release(obj);
    }
private:
    ObjectPool* pool_;
};
```

- **`pool_`이 nullptr 기본**: `Handle{}` 기본 생성자 / acquire 실패 시 빈 Handle 케이스 지원
- **포인터 크기(8B)**: `sizeof(Handle) = sizeof(T*) + sizeof(Deleter) = 16B` — `shared_ptr`(16~32B)과 유사하지만 atomic ref count 없음

### Handle이 outstanding인 동안 pool move 금지

```cpp
auto h = pool.acquire_unique();
auto p2 = std::move(pool);  // ⚠ h 내부 Deleter의 pool_ → 원래 주소(dangling)
```

`std::unique_ptr`의 본질적 한계. 사용자 책임으로 남김 (문서 계약). 런타임 체크(pool ID 등)는 **실무 관용에 없음** — 비용 0 원칙.

### Raw + Handle 병행

- Raw: hot path 최소 오버헤드 (~2.4ns)
- Handle: 안전성 (release 누락 구조적 차단)
- 둘 다 제공하여 **사용자가 상황별 선택**

v04에서 Raw API를 private로 내려 Handle 전용으로 좁히는 건 별개 설계 결정.

## API

| 카테고리     | 함수                                                        |
| :----------- | :---------------------------------------------------------- |
| 상태         | `capacity()`, `available()`, `in_use()`                     |
| Raw          | `acquire() -> T*`, `release(T*)`                            |
| RAII         | `acquire_unique() -> Handle`                                |
| Type aliases | `Deleter` (nested class), `Handle = unique_ptr<T, Deleter>` |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

- **BM_Pool_Raw**: v01과 동일 경로 — 비교 기준
- **BM_Pool_Handle**: `acquire_unique()` + Handle 소멸 → Deleter 호출

스크립트: [../script/run_bench_v02.sh](../script/run_bench_v02.sh)

### 실측

| 벤치                                 | Time (mean) |            vs Raw |
| :----------------------------------- | ----------: | ----------------: |
| Pool Raw (cap 64/1024/16384 동일)    |     2.44 ns |              기준 |
| Pool Handle (cap 64/1024/16384 동일) |     4.15 ns | **+1.7 ns (70%)** |

CV < 0.4%, 극도로 안정. capacity 무관.

### 관찰

- **Handle 오버헤드 ~1.7ns (70% 상대 증가)**: Deleter 생성/소멸 + pool\_ nullptr 체크 + `DoNotOptimize(h)`가 16B 객체 관찰 강제하는 비용 포함
- **절대값은 여전히 작음**: new/delete(21ns) 대비 Handle(4.15ns)도 **5× 빠름**
- **실무 권장**: 대부분 `acquire_unique()` — release 누락 구조적 차단의 가치가 1.7ns보다 크다. Hot path 극한에서만 Raw

## 다음 버전 힌트

- **v03**: free list 자료구조를 **링크드 리스트**(`Node { T; size_t next; }`)로 — 구조적 순수성 실험
