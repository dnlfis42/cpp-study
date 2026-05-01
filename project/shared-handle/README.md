# shared-handle

`std::shared_ptr` 내부 구조를 직접 구현하며 non-intrusive vs intrusive 참조 카운팅의 성능 차이를 실측한다.

- **제어 블록 분리**: 객체와 refcount를 분리했을 때의 힙 할당·포인터 간접참조 비용
- **intrusive refcount**: refcount를 객체 내부에 내장해 제어 블록 제거
- **weak reference**: 순환 참조 해결과 그 비용
- **custom deleter/allocator**: 소멸자·할당자 주입 패턴과 overhead
- **thread-local cache**: refcount 조작 경합 감소 전략

## 성능 종합

`shandle_bench`.

| 조건                                 | `std::shared_ptr` |       v01 |       v02 |
| :----------------------------------- | ----------------: | --------: | --------: |
| 복사 (`__libc_single_threaded` 활성) |         `6.37 ns` | `12.1 ns` | `14.4 ns` |
| 복사 (atomic 경로)                   |         `19.1 ns` | `12.1 ns` | `14.4 ns` |
| 이동                                 |         `1.68 ns` | `2.03 ns` | `2.07 ns` |
| make + 소멸                          |         `22.3 ns` | `24.5 ns` | `24.9 ns` |

환경: i7-9750H, GCC, `-O3`, 3.0 GHz 고정, core 2

## 버전

### v01

**개선**: non-intrusive 참조 카운팅 원형 확립. atomic 복사 경로에서 `std::shared_ptr` 대비 `37%` 빠름(`lock addl` vs `lock xadd`).

**과제**:

- `__libc_single_threaded` 최적화 미적용: 단일 스레드 복사에서 `shared_ptr` 대비 2× 느림
- 이동 시 `release()` null 분기를 컴파일러가 제거하지 못해 불필요한 명령어 잔존
- weak reference 없음

### v02

**개선**: intrusive refcount로 전환. `refcount{1}` adopt 패턴으로 make 오버헤드 제거. C++20 concept으로 타입 제약 강화. 이동·make는 v01과 동등하다.

**트레이드오프**: T가 `IntrusiveBase`를 반드시 상속해야 하는 침투적 설계.

**과제**:

- `__libc_single_threaded` 최적화 미적용
- 이동 null 분기 잔존
- weak reference 없음
