# Linux Memory

## Executable and Linkable Format (ELF)

실행 파일, 오브젝트 코드, 공유 라이브러리 등의 표준 파일 규격

ELF header

해당 파일의 기본적인 설명(헤더, 정보, 사이즈 등 표기)

Program header table

실행 정보

.text

실행 가능한 명령어(read + execute)

.rodata

read-only 데이터; 문자열 리터럴, const 전역 등. read만, execute 불가

.data

값 초기화 된 전역/정적 변수

.bss (block started by symbol)

오직 제로 값으로 표시된 정적으로 할당된 변수가 포함된 데이터 세그먼트의 일부

컴파일러나 링커에 의해 사용된다.

파일에 데이터를 저장하지 않고 크기 정보만 저장하므로 실행 파일의 크기가 줄어든다.

다른 영역은 크기 + 내용까지 들어있어 프로그램 시작 전 객체들을 초기화한다.

heap

힙 영역

stack

스택 영역

기본적으로 1MB를 미리 예약받고 콜 스택을 쌓거나 빼며 동작.

큰 주소에서 작은 주소로 늘어남.

guard 페이지가 reserve 되지 않은 공간을 찌를 때 stack overflow 발생. 따라서 1MB 전체를 온전히 사용할 수는 없다.

## vm 구조

```
높은 주소
--- 0xffff ffff ffff ffff
kernel space
--- 0xfff8 0000 0000 0000

non canonical

--- 0x0008 0000 0000 0000
stack
아래 화살표
...

memory mapping segment

... brk, program break
위 화살표
heap start_brk
...

.bss
.data
.rodata
.text
...
--- 0x0000 0000 0000 0000
```

## Heap

`brk` / `sbrk`

sbrk는 증가분 계산해서 brk 호출. brk는 힙 끝을 늘린다. sbrk는 새로 증가된 공간을 사용하기 쉽게끔 이전 주소값을 반환한다.

메인 아레나는 하나의 단일 공간이므로 brk쪽의 페이지 단위의 반납이 강제된다. 하지만 특성상 중간 중간의 할당/반납의 랜덤성 덕분에 내부 파편화가 심해져서 페이지 중간 중간에 사용중인 파편들이 존재할 수 있다. 또한 brk는 끝점만 움직일 수 있으므로 끝에 살아있는 청크가 하나라도 있으면 아래 빈 공간이 전부 인질이 된다.

따라서 메모리가 OS로 돌아가기 위한 조건인 할당 영역의 완전한 미사용을 충족시키기 어렵다.

`mmap`

페이지 정렬(페이지 경계(보통은 4KB)에 맞추겠다는 의미)된 주소를 제공한다. 독립된 VMA로써 메인 아레나와 별개된 공간이다.

mmap이 brk와 다른 점은 mmap은 독립된 VMA로 관리되고 brk는 이미 존재하는 단일 VMA인 곳에서 program break 값을 늘리고 줄이는 데 치중하기 때문이다.

commit을 해도 직접 메모리를 사용하지 않는 이상 물리 메모리에 올라가지 않는다. 이걸 lazy-commit이라고 한다. lazy-commit 시 page fault가 발생하는데 유저 스페이스에서는 인지하지 못하고 os 단에서 해결하고 다시 해당 명령어를 수행한다.

작은 요청을 mmap으로 처리할 시 4kb의 거대한 공간을 최소로 받음으로써 내부 단편화가 굉장히 심해진다. 기본 힙은 큰 덩어리로 미리 받아둔 영역에서 free list로 재활용하여 syscall 호출을 최소화시켰는데 비해 mmap은 항상 syscall이므로 성능상의 문제가 발생한다.

glibc `malloc`

glibc `malloc`은 내부적으로 arena(스레드 별 free list)를 유지한다.

작은 요청은 arena에서 free list를 뒤져서 즉시 반환한다. -> syscall 없음.

arena가 모자란다면 brk로 heap을 확장하거나 mmap으로새 영역을 할당받는다.

경로 다이어그램은 그냥 너가 만들어라.

`M_MMAP_THRESHOLD`는 brk 말고 mmap으로 처리해야 하는 경계값이다.

## Memory Allocator

| Allocator          | 핵심 변화        | 해결한 문제        | 새로 생긴 문제       |
| ------------------ | ---------------- | ------------------ | -------------------- |
| dlmalloc           | 단일 arena       | 베이스라인         | 멀티스레드 지원 안함 |
| ptmalloc2          | 멀티 arena       | arena 락 경합 감소 |                      |
| ptmalloc2 + tcache | per-thread cache | 작은 할당 락 제거  |                      |
| jemalloc           |                  |                    |                      |
| tcmalloc           |                  |                    |                      |

### tcache

장점

작은 할당 락을 제거

단점

한번만 동적 할당 해도 큰 영역의 공간이 리스트 관리를 위해 할당됨.

A가 할당, B가 free -> B의 tcache소유 -> producer-Consumer 누수 패턴.

size class: malloc 요청 크기를 일정 간격 버킷으로 묶은 것. glibc tcache는 16b ~ 1kb까지 16b 간격으로 64개 버킷 존재.

깊이: 각 size class 별로 캐시 슬롯 수. glibc 기본 7개.

청크는 뭐야?
