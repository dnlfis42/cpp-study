# v01

## 설계 의도

non-intrusive 참조 카운팅의 원형. 제어 블록을 직접 구현해 `std::shared_ptr` 내부 구조를 재현하고 성능을 비교한다.

### 제어 블록 내장

`SharedHandle<T>`는 `ctrl_` 포인터 하나만 들고 있다. `std::shared_ptr`의 `ptr_` + `ctrl_` 두 포인터와 달리 `sizeof(SharedHandle<T>) = 8 B`.

```cpp
template<typename T>
class SharedHandle {
    ControlBlock<T>* ctrl_;  // T와 refcount를 함께 보관
};
```

`ControlBlock<T>`가 T를 내부에 내장하므로 `make_handle<T>(args...)`는 단일 할당으로 객체와 refcount를 함께 확보한다. `std::make_shared`와 동일한 전략.

### atomic refcount

복사 시 `fetch_add(1, memory_order_relaxed)`: refcount 증가만 원자적이면 충분.
소멸 시 `fetch_sub(1, memory_order_acq_rel)`: 결과가 0이면 `destroy_object()` 후 `delete ctrl_`.

`acq_rel` release: 다른 스레드의 소멸이 완료됐음을 보장한 뒤 객체를 해제한다.

### 이동 의미론

복사: refcount +1, `ctrl_` 복사.
이동: `ctrl_` 탈취, 원본 nullptr. refcount 변화 없음.

## 측정

### SharedHandle vs std::shared_ptr

복사·이동·make 각각 `std::shared_ptr`과 비교. `shandle_v01_bench`.

**가설**

`SharedHandle`이 포인터 하나로 더 단순한 구조이므로 유사하거나 소폭 빠를 것. `std::shared_ptr`은 표준 라이브러리 최적화가 있으므로 차이가 작을 가능성이 높다.

**측정 결과**

`shandle_v01_bench`

| 조건                                 | `std::shared_ptr` | `SharedHandle` |                CV |
| :----------------------------------- | ----------------: | -------------: | ----------------: |
| 복사 (`__libc_single_threaded` 활성) |         `6.04 ns` |      `12.1 ns` | `0.03%` / `0.03%` |
| 복사 (atomic 경로)                   |         `19.1 ns` |      `12.1 ns` | `0.02%` / `0.03%` |
| 이동                                 |         `1.68 ns` |      `2.04 ns` | `0.02%` / `0.40%` |
| make + 소멸                          |         `22.1 ns` |      `24.1 ns` | `0.13%` / `0.17%` |

환경: i7-9750H, GCC, `-O3`, 3.0 GHz 고정, core 2

**분석**

**복사**

```asm
; shared_ptr (single-threaded 경로)
b6d0:  addl $0x1, 0x8(%rax)        ; non-atomic increment

; shared_ptr (atomic 경로)
b6e5:  lock xadd %eax, (%rdx)      ; fetch_add, 이전 값 반환

; SharedHandle
b40f:  lock addl $0x1, (%rbx)      ; fetch_add, 반환값 불필요
```

`shared_ptr`은 런타임에 `__libc_single_threaded` 플래그를 확인해 단일 스레드 환경에서 `lock` 없는 `addl`로 교체한다. `SharedHandle`은 이 최적화가 없어 항상 `lock addl`을 사용하므로 단일 스레드 조건에서 2× 느리다. atomic 경로에서는 `shared_ptr`이 반환값이 필요한 `lock xadd`, `SharedHandle`이 반환값 불필요한 `lock addl`을 사용해 `SharedHandle`이 `37%` 빠르다.

**이동**

```asm
; shared_ptr 루프 (4 명령어/iter)
ba70:  movaps %xmm0, (%rsp)        ; 128비트 (ptr_ + ctrl_) 저장
ba74:  movdqa (%rsp), %xmm0        ; 128비트 로드
ba79:  mov 0x8(%rsp), %rbp
ba7e:  sub $0x1, %rax              ; 루프 카운터

; SharedHandle 루프 (9 명령어/iter)
b3d8:  mov %rbx, 0x10(%rsp)        ; ctrl_ 저장
b3dd:  movq $0x0, 0x8(%rsp)        ; h.ctrl_ = nullptr
b3e6:  mov 0x8(%rsp), %rax
b3eb:  test %rax, %rax
b3ee:  je b410                     ; nullptr이므로 항상 점프
b410:  mov 0x10(%rsp), %rbx
b415:  mov %rbx, 0x8(%rsp)
b41a:  sub $0x1, %rbp              ; 루프 카운터
b41e:  jne b3d8
```

`shared_ptr`은 `ptr_` + `ctrl_` 두 포인터를 128비트 SIMD 한 번으로 처리해 루프당 4 명령어. `SharedHandle`은 포인터가 하나지만 `release()` 안의 `if (!ctrl_)` 분기를 컴파일러가 제거하지 못해 루프당 9 명령어.

**make**

```asm
; SharedHandle
b2d0:  mov $0x44, %edi             ; 68 B 할당
b301:  lock subl $0x1, (%rax)      ; atomic 소멸

; shared_ptr
b686:  mov $0x50, %edi             ; 80 B 할당
b6d7:  cmpb $0x0, __libc_single_threaded
b6de:  jne b670                    ; single-threaded면 non-atomic 경로
```

`SharedHandle`이 12 B 작은 블록을 할당하지만, 소멸 시 `lock subl`(atomic) vs non-atomic 감소 차이가 할당 크기 이점을 상쇄해 `2 ns` 뒤처진다.

**결론**

atomic 경로에서 `SharedHandle`이 복사 비용 우위(`lock addl` vs `lock xadd`). 이동·make는 `shared_ptr` 표준 라이브러리 최적화(`__libc_single_threaded`, SIMD)에 밀린다.

## 개선

non-intrusive 참조 카운팅 원형 확립. atomic 복사 경로에서 `std::shared_ptr` 대비 `37%` 빠름(`lock addl` vs `lock xadd`).

## 과제

- `__libc_single_threaded` 최적화 미적용: 단일 스레드 복사에서 `shared_ptr` 대비 2× 느림
- 이동 시 `release()` null 분기를 컴파일러가 제거하지 못해 불필요한 명령어 잔존
- `std::shared_ptr` 대비 weak reference 없음
