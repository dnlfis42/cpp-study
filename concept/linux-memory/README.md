# Linux Memory (note)

리눅스의 메모리 모델 학습 노트. ELF / 가상 메모리 구조 / heap 관리 (brk·mmap·glibc malloc) / allocator 계보.

## Executable and Linkable Format (ELF)

실행 파일, 오브젝트 코드, 공유 라이브러리의 표준 파일 규격.

- **ELF header**: 파일 자체에 대한 메타정보 (헤더, 정보, 사이즈 등)
- **Program header table**: 실행 정보
- **`.text`**: 실행 가능한 명령어 (read + execute)
- **`.rodata`**: 읽기 전용 데이터. 문자열 리터럴, `const` 전역 등. read만 가능, execute 불가
- **`.data`**: 값으로 초기화된 전역/정적 변수
- **`.bss`** (block started by symbol): 0으로 초기화되는 정적 변수 영역. 컴파일러/링커가 사용. 파일에 데이터를 저장하지 않고 **크기 정보만 저장**해서 실행 파일 크기를 줄임. 다른 영역은 크기+내용을 모두 담아 시작 전에 초기화에 사용.
- **heap**: 힙 영역
- **stack**: 스택 영역. 기본 1 MB를 미리 예약 후 콜 스택을 push/pop. **큰 주소 → 작은 주소**로 자람. guard 페이지가 reserve 안 된 영역에 닿으면 stack overflow → 1 MB 전체를 온전히 쓸 수는 없음.

## 가상 메모리 레이아웃 (x86-64)

```
높은 주소
─── 0xffff_ffff_ffff_ffff
    kernel space
─── 0xffff_8000_0000_0000

    non-canonical (사용 불가 영역)

─── 0x0000_8000_0000_0000
    stack
    ↓ (자람 방향)
    ...
    memory mapping segment
    ...
    ↑ brk / program break
    heap (start_brk)
    ...
    .bss
    .data
    .rodata
    .text
    ...
─── 0x0000_0000_0000_0000
낮은 주소
```

## Heap

### `brk` / `sbrk`

- `sbrk(n)`: 증가분 `n`을 받아 내부적으로 `brk` 호출. **이전 break 주소 반환** → 새로 늘어난 영역의 시작 주소를 즉시 사용 가능.
- `brk`: 힙의 끝점 (program break)을 직접 지정.

메인 아레나는 단일 연속 VMA. brk는 **끝점만** 움직일 수 있음 → 끝에 살아있는 청크가 하나라도 있으면 그 아래 빈 공간 전부가 인질이 됨. 또한 할당/반납이 임의 순서로 일어나는 특성 탓에 페이지 중간중간 사용 중인 파편이 생겨 내부 단편화가 심함.

→ "할당 영역 전체가 미사용이어야 OS로 반환된다"는 조건을 충족시키기 어려움.

### `mmap`

페이지 정렬(보통 4 KB 경계)된 주소를 제공. **독립된 VMA**로 관리되어 메인 아레나와 분리됨.

`mmap`이 `brk`와 다른 핵심: `mmap`은 매번 새 VMA, `brk`는 단일 VMA의 program break를 늘리고 줄이는 것.

**lazy-commit**: 가상 영역을 commit해도 실제로 접근하기 전까지는 물리 메모리에 안 올라감. 첫 접근 시 page fault 발생 → OS가 처리 후 명령어 재실행 → 유저 스페이스는 인지 못함.

작은 요청을 `mmap`으로 처리하면 최소 4 KB 단위라 내부 단편화가 큼. heap은 큰 덩어리를 미리 받아 free list로 재활용해 syscall을 줄이는데, `mmap`은 항상 syscall이라 성능 차이.

### glibc `malloc`

내부적으로 **arena(스레드별 free list)** 를 유지.

- 작은 요청 → arena의 free list에서 즉시 반환 (**syscall 없음**)
- arena가 모자라면 `brk`로 heap 확장 또는 `mmap`으로 새 영역 할당
- `M_MMAP_THRESHOLD` (기본 128 KB): 이 크기 이상은 `brk` 대신 `mmap`으로 처리

> 경로 다이어그램은 추후 추가.

## Memory Allocator 계보

| Allocator          | 핵심 변화        | 해결한 문제         | 새로 생긴 문제        |
| ------------------ | ---------------- | ------------------- | --------------------- |
| dlmalloc           | 단일 arena       | 베이스라인          | 멀티스레드 지원 안 됨 |
| ptmalloc2          | 멀티 arena       | arena 락 경합 감소  |                       |
| ptmalloc2 + tcache | per-thread cache | 작은 할당의 락 제거 |                       |
| jemalloc           |                  |                     |                       |
| tcmalloc           |                  |                     |                       |

### tcache

**장점**

- 작은 할당의 락 제거 (per-thread라 경합 0)

**단점**

- 한 번만 동적 할당해도 리스트 관리를 위해 큰 영역이 미리 잡힘
- **producer-consumer 누수 패턴**: A 스레드가 alloc, B 스레드가 free → free된 청크가 B의 tcache로 들어감 → A는 계속 alloc만 하므로 자기 tcache는 비어 있고, B의 tcache는 점점 차서 못 비워짐 → 메모리 회수가 안 됨

**용어**

- **size class**: malloc 요청 크기를 일정 간격 버킷으로 묶은 것. glibc tcache는 16 B ~ 1 KB까지 16 B 간격으로 64개 버킷.
- **깊이**: 각 size class별 캐시 슬롯 수. glibc 기본 7개.

> TODO: 청크 (chunk) 정의 — 헤더 구조, prev_size/size 필드, free chunk vs in-use chunk
