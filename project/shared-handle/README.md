# shared-handle

`std::shared_ptr` 내부 구조를 직접 구현하며 non-intrusive vs intrusive 참조 카운팅의 성능 차이를 실측한다.

- **제어 블록 분리**: 객체와 refcount를 분리했을 때의 힙 할당·포인터 간접참조 비용
- **intrusive refcount**: refcount를 객체 내부에 내장해 제어 블록 제거
- **weak reference**: 순환 참조 해결과 그 비용
- **custom deleter/allocator**: 소멸자·할당자 주입 패턴과 overhead
- **thread-local cache**: refcount 조작 경합 감소 전략

## 성능 종합

`shandle_v01_bench`

| 조건                                 | `std::shared_ptr` | `SharedHandle` v01 |
| :----------------------------------- | ----------------: | -----------------: |
| 복사 (`__libc_single_threaded` 활성) |         `6.04 ns` |          `12.1 ns` |
| 복사 (atomic 경로)                   |         `19.1 ns` |          `12.1 ns` |
| 이동                                 |         `1.68 ns` |          `2.04 ns` |
| make + 소멸                          |         `22.1 ns` |          `24.1 ns` |

환경: i7-9750H, GCC, `-O3`, 3.0 GHz 고정, core 2

## 버전

### v01

**개선**: non-intrusive 참조 카운팅 원형 확립. atomic 복사 경로에서 `std::shared_ptr` 대비 `37%` 빠름(`lock addl` vs `lock xadd`).

**과제**:

- `__libc_single_threaded` 최적화 미적용: 단일 스레드 복사에서 `shared_ptr` 대비 2× 느림
- 이동 시 `release()` null 분기를 컴파일러가 제거하지 못해 불필요한 명령어 잔존
- weak reference 없음
