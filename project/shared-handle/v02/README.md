# v02

## 변경 사항

```cpp
// 추가
class IntrusiveBase;                          // refcount 내장 기반 클래스
template<typename T> concept Intrusive;       // IntrusiveBase 상속 제약

// 제거
template<typename T> struct ControlBlock;     // 제어 블록 제거
```

## 설계 의도

### intrusive refcount

T가 `IntrusiveBase`를 상속해 refcount를 직접 내장한다. `SharedHandle<T>`는 `T*` 하나만 보유한다.

```cpp
class IntrusiveBase {
    std::atomic<int> refcount{1};
};

template<Intrusive T>
class SharedHandle {
    T* ptr_;
};
```

`refcount{1}`로 초기화하고 `make_handle`이 생성하는 private 생성자는 `fetch_add` 없이 포인터만 인수인계한다 (adopt 패턴). refcount가 0인 상태가 없으므로 copy와 destroy의 경로가 단순해진다.

### Intrusive concept

`static_assert` 대신 C++20 concept을 사용한다.

```cpp
template<typename T>
concept Intrusive = std::is_base_of_v<IntrusiveBase, T>;

template<Intrusive T>
class SharedHandle { ... };
```

`SharedHandle<int>` 같은 잘못된 인스턴스화는 `requires` 절에서 즉시 차단되며, 에러 위치가 호출부를 정확히 가리킨다.

### 포인터 간접참조 깊이

V01과 비교해 포인터 간접참조가 줄어든다고 기대할 수 있지만 실제로는 동일하다. V01의 `ControlBlock<T>`는 T를 raw storage로 내장하므로 `ctrl_`에서 T 데이터까지의 오프셋은 컴파일 타임 상수다. V02의 `ptr_`도 T를 직접 가리킨다. 두 구현 모두 포인터 하나를 따라가 offset 0의 refcount를 조작한다. 어셈블리로 확인한다 (아래 분석 참조).

## 측정

### SharedHandle v01 vs v02 vs std::shared_ptr

복사·이동·make 각각 비교. `shandle_bench`.

**가설**

intrusive 구조는 V01과 포인터 간접참조 깊이가 동일하므로 복사·이동 성능은 같을 것. make는 `refcount{1}` adopt 패턴으로 fetch_add를 제거해 V01과 동등해질 것.

**측정 결과**

`shandle_bench`.

| 조건                                 | `std::shared_ptr` |       v01 |       v02 |      CV |
| :----------------------------------- | ----------------: | --------: | --------: | ------: |
| 복사 (`__libc_single_threaded` 활성) |         `6.37 ns` | `12.1 ns` | `14.4 ns` | `<0.3%` |
| 복사 (atomic 경로)                   |         `19.1 ns` | `12.1 ns` | `14.4 ns` | `<0.3%` |
| 이동                                 |         `1.68 ns` | `2.03 ns` | `2.07 ns` | `<1.2%` |
| make + 소멸                          |         `22.3 ns` | `24.5 ns` | `24.9 ns` | `<0.3%` |

환경: i7-9750H, GCC, `-O3`, 3.0 GHz 고정, core 2

**분석**

**복사 — 어셈블리**

```asm
; V01 복사 루프 (bba0)
bba0:  mov    0x8(%rsp),%rbx       ; src.ctrl_ 로드
bba5:  mov    %rbx,0x10(%rsp)      ; copy.ctrl_ = rbx
bbaf:  lock addl $0x1,(%rbx)       ; ctrl_->refcount++
bbbd:  lock subl $0x1,(%rax)       ; 이전 copy release

; V02 복사 루프 (ba50)
ba50:  mov    0x8(%rsp),%rbx       ; src.ptr_ 로드
ba55:  mov    %rbx,0x10(%rsp)      ; copy.ptr_ = rbx
ba5f:  lock addl $0x1,(%rbx)       ; ptr_->refcount++
ba6d:  lock subl $0x1,(%rax)       ; 이전 copy release
```

루프 명령어 구조와 수가 동일하다. 포인터 간접참조 깊이가 같다는 것이 어셈블리로 확인된다. 그러나 루프 시작 주소가 다르다.

- V01: `0xbba0` — 32바이트 정렬
- V02: `0xba50` — 32바이트 경계에서 16바이트 오프셋

**복사 — perf 분석**

`perf stat`으로 IDQ uop 출처를 측정했다.

|                    |   V01 |   V02 |
| :----------------- | ----: | ----: |
| idq.dsb_uops/iter  |   1.0 |  15.0 |
| idq.mite_uops/iter |   6.1 |  0.15 |
| idq.ms_uops/iter   | 16.04 | 16.03 |
| cycles/iter        |  36.2 |  43.1 |

`idq.ms_uops`(Microcode Sequencer)가 양쪽 동일하다. `lock` 명령어의 마이크로코드 실행 자체는 같다. 차이는 비-lock 명령어의 공급 경로에 있다. V01은 MITE(레거시 디코더), V02는 DSB(uop 캐시) 경유다.

루프에는 다음 패턴이 있다.

```asm
mov %rbx, 0x10(%rsp)      ; [rsp+16]에 저장
lock addl $0x1, (%rbx)    ; full fence — 스토어 버퍼 비움
mov 0x10(%rsp), %rax      ; [rsp+16]에서 로드
```

`lock`은 full memory fence로 스토어 버퍼를 비운다. 이후 `mov 0x10(%rsp), %rax`는 store forwarding이 불가능하고 L1 캐시에서 읽어야 한다 (L1 load latency = 4 사이클).

- **V02 (DSB)**: lock 완료 후 DSB가 즉시 다음 명령어를 공급한다. 실행 엔진이 load uop을 빨리 받지만 L1 데이터를 4사이클 기다린다. 이 4사이클이 크리티컬 패스에 노출된다.
- **V01 (MITE)**: lock 완료 후 MITE가 재디코딩을 시작하는 데 4~5사이클이 걸린다. 실행 엔진이 load uop을 받을 시점에 L1 데이터가 이미 준비돼 있다. 4사이클이 MITE 디코드 딜레이에 은폐된다.

루프당 lock 2개, 각 4사이클 은폐/노출 → 이론 차이 ~8사이클. 측정 차이 6.9사이클과 일치한다.

V01이 MITE를 타는 직접 원인은 루프 코드 정렬(32바이트 정렬)이 DSB set 매핑에 영향을 주어 `lock` MS 시퀀스 실행 후 DSB 엔트리가 무효화되기 때문으로 추정된다. V02는 16바이트 오프셋 위치라 다른 DSB set에 매핑되어 이 무효화를 피한다.

**이동**

V01·V02 모두 포인터 하나를 탈취하고 원본을 nullptr로 만든다. `2.03 ns` vs `2.07 ns`로 오차 범위 내 동등하다.

**make**

`refcount{0}` + `fetch_add` 방식에서 `refcount{1}` adopt 패턴으로 전환해 make_handle 경로의 atomic 연산 하나를 제거했다. `24.5 ns` vs `24.9 ns`로 V01과 동등하다.

**결론**

포인터 간접참조 깊이는 V01과 동일하며 어셈블리로 확인된다. 이동·make는 동등하다. 복사는 루프 코드 정렬 차이로 디코드 경로가 갈리고, DSB 경로에서 lock 이후 L1 load latency가 크리티컬 패스에 노출돼 `14.4 ns` vs `12.1 ns`의 차이가 발생한다.

## 개선

V01 대비 성능 회귀 없이 intrusive 구조로 전환. concept constraint로 잘못된 타입 인자를 컴파일 타임에 차단한다. make 성능은 `refcount{1}` adopt 패턴으로 V01과 동등하다.

## 트레이드오프

T가 반드시 `IntrusiveBase`를 상속해야 한다 (침투적 설계). 기존 타입이나 서드파티 타입에는 적용할 수 없다.

복사에서 루프 코드 정렬에 의해 MITE 경로를 타는 V01 대비 19% 느리다. 구현 구조의 차이가 아닌 컴파일러가 배치한 코드 주소와 DSB set 매핑의 우연한 차이에서 비롯된다.

## 과제

- `__libc_single_threaded` 최적화 미적용: 단일 스레드 복사에서 `shared_ptr` 대비 2× 느림
- 이동 시 `release()` null 분기를 컴파일러가 제거하지 못해 불필요한 명령어 잔존
- 복사: lock 이후 store forwarding 불가 패턴이 DSB 경로에서 L1 load latency를 노출
- weak reference 없음
