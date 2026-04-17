# Object Pool v04

## v03 대비 변화

**Raw API를 private로 내림 — Handle만 공개.**

```cpp
// v03
public:
    T* acquire();             // Raw
    void release(T*);         // Raw
    Handle acquire_unique();  // RAII
public: // 상태
    ...

// v04
public:
    Handle acquire();         // 유일한 획득 경로
public: // 상태
    ...
private:
    T* acquire_raw();         // Deleter만 호출
    void release(T*);         // Deleter만 호출
```

`acquire()`의 시그니처/이름 변경: `Handle acquire()` (v02의 `acquire_unique` 의미 계승).

## 설계 의도

### 타입 시스템으로 안전 보장

**v02/v03의 남아있던 Raw API**는 실수를 허용:

```cpp
// v02/v03
T* p = pool.acquire();  // Raw 경로
// ... release(p) 까먹으면 풀 고갈
```

v04는 **컴파일 타임에** 이런 경로 자체를 없앰:

```cpp
// v04
auto h = pool.acquire();  // Handle만 반환. Raw 획득 불가능.
// 소멸 시 자동 release. 실수 불가.
```

### cross-pool 반환 불가능

v03까지는 `poolA.release(poolB가 준 포인터)`가 기술적으로 가능. v04에선 Handle의 Deleter가 생성된 pool을 내부 저장 → **소멸 시 무조건 생성한 pool로 반환**. 타입이 아닌 런타임 보장.

### Deleter의 private release 접근

`Deleter`는 `ObjectPool`의 **중첩 클래스** → C++11부터 enclosing class의 private 멤버 접근 가능. friend 선언 없이도 `pool_->release(obj)` 호출됨.

### 성능 vs 안전 트레이드오프

- Raw 경로 없어짐 → hot path에서도 Handle 생성/소멸 필수
- v02 측정 기준 **Handle이 Raw 대비 ~1.7ns 느림**
- **이 비용으로 타입 안전성 영구 획득** — 라이브러리 관점에서 대부분 합리적

Hot path 극한 최적화가 필요하면 v01/v02 선택. 일반적으론 v04가 **권장 기본**.

## API

| 카테고리     | 함수                                                        |
| :----------- | :---------------------------------------------------------- |
| 상태         | `capacity()`, `available()`, `in_use()`                     |
| 획득         | **`acquire() -> Handle`** (유일)                            |
| Type aliases | `Deleter` (nested class), `Handle = unique_ptr<T, Deleter>` |

## 벤치마크

환경: Intel, gcc 13.3, `-O3`, 상한 3.0 GHz 고정, `taskset -c 2`

- **BM_Pool_Handle**: `acquire()` (Handle 반환) + 자동 release

Raw 경로 없으므로 `BM_Pool_Raw` 제거. v03 Handle 결과와 직접 비교 가능.

스크립트: [../script/run_bench_v04.sh](../script/run_bench_v04.sh)

### 실측: v03 Handle과 동등

| 벤치                     | v03 Handle |  v04 Handle |                   차이 |
| :----------------------- | ---------: | ----------: | ---------------------: |
| cap 64/1024/16384 (동일) |    4.90 ns | **4.86 ns** | -0.04 ns (노이즈 범위) |

CV < 1%. API 축소는 **성능 비용 제로**로 안전성만 획득.

Raw를 private로 내려도 내부 호출자(`acquire()`, `Deleter::operator()`)의 인라인은 gcc가 문제없이 처리 — public/private 여부가 최적화에 영향 없음 확인.

## 다음 버전 힌트

- **v05**: 청크 기반 **가변 크기** 풀. 고갈 시 새 청크 자동 할당. Non-intrusive `Node { T data; Node* next; }` — **인덱스 아닌 포인터로 연결**해 청크 경계 무관. 포인터 안정성 유지한 채 성장

> **intrusive union 실험 (원본 v05 설계)은 스킵**.
> `union Slot { T value; size_t next; }`로 메모리 공유 + placement new로 생애주기 수동 관리하는 패턴은 메모리 절약 vs 성능 20% 손해 트레이드오프. TCP 서버 맥락(T가 큰 세션 구조체)에선 이득 적어 이 연작에서 생략.
> 관심 있으면 `concept/placement-new/` 같은 짧은 실험으로 따로 정리 예정.
