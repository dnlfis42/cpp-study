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

**복사**: `std::shared_ptr`은 glibc의 `__libc_single_threaded` 플래그를 런타임에 확인해 단일 스레드 환경에서 refcount 조작을 non-atomic으로 교체한다(`addl` without `lock`). `SharedHandle`은 항상 `lock addl`을 사용하므로 이 조건에서 2× 느리다. atomic 경로(`pthread_create` 이후)에서는 `shared_ptr`이 `lock xadd`(반환값 필요), `SharedHandle`이 `lock addl`(반환값 불필요)을 사용해 `SharedHandle`이 `37%` 빠르다.

**이동**: `shared_ptr`은 `ptr_` + `ctrl_` 두 포인터를 128비트 SIMD 한 번으로 탈취한다. `SharedHandle`은 포인터 하나지만 `release()` 안의 `if (!ctrl_)` 분기를 컴파일러가 제거하지 못해 루프당 명령어가 더 많다.

**make**: `SharedHandle` 제어 블록이 68 B로 `shared_ptr`의 80 B보다 작지만, 소멸 시 `lock subl`(atomic) vs non-atomic 감소 차이가 할당 크기 이점을 상쇄한다.

**결론**

atomic 경로에서 `SharedHandle`이 복사 비용 우위(`lock addl` vs `lock xadd`). 이동·make는 `shared_ptr` 표준 라이브러리 최적화(`__libc_single_threaded`, SIMD)에 밀린다.

## 개선

non-intrusive 참조 카운팅 원형 확립. atomic 복사 경로에서 `std::shared_ptr` 대비 `37%` 빠름(`lock addl` vs `lock xadd`).

## 과제

- `__libc_single_threaded` 최적화 미적용: 단일 스레드 복사에서 `shared_ptr` 대비 2× 느림
- 이동 시 `release()` null 분기를 컴파일러가 제거하지 못해 불필요한 명령어 잔존
- `std::shared_ptr` 대비 weak reference 없음
