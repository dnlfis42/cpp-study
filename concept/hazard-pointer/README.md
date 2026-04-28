# hazard-pointer

lock-free 자료구조에서 사용 중인 노드의 해제를 안전하게 지연시키는 메커니즘.

## 문제: lock-free에서 노드 해제가 왜 어려운가

lock-free 스택의 pop을 단순하게 구현하면:

```cpp
Node* old_head = head.load();
head.compare_exchange(old_head, old_head->next);
delete old_head;  // 위험
```

`delete old_head`가 위험한 이유: 다른 스레드가 `old_head`를 아직 읽고 있을 수 있다.

```
Thread 1: old_head = head.load()  -- A를 가리킴
          (잠깐 멈춤)
Thread 2: pop -> A 해제
Thread 1: old_head->next 읽기     -- use-after-free
```

mutex라면 lock이 이를 막지만, lock-free는 그 보호가 없다.

## 아이디어

해제 타이밍을 미루는 것으로 해결한다.

> "내가 이 포인터를 쓰고 있다"를 전역에 등록해두면,
> 다른 스레드가 해제하기 전에 그 등록을 확인하고 미룬다.

**읽는 쪽 (pop):**

```
1. head 로드
2. hazard table에 주소 등록 (protect)
3. head가 바뀌지 않았는지 재확인
4. 안전하게 next 접근 + CAS
5. hazard 해제 + retire list에 추가
```

**해제하는 쪽 (scan):**

```
1. retire list에 추가 (즉시 delete 안 함)
2. list가 충분히 쌓이면 scan
3. scan: 전역 hazard table 수집
4. hazard table에 없는 것만 delete
```

핵심 용어:

- hazard pointer — "나 지금 이 주소 쓰고 있어" 선언
- retire list — 해제 예정 노드 대기열
- scan — retire list와 hazard table 교차 확인, 안전한 것만 해제

## 구현

파일 구성:

- `include/hazard_pointer.hpp` — `HazardTable` (슬롯 관리·보호·수집) + `RetireList` (지연 해제·scan)
- `include/lf_stack.hpp` — hazard-pointer를 적용한 MPMC lock-free 스택

설계 제약 (단순화):

- 스레드 수: 컴파일 타임 상수 `MAX_THREADS`
- 스레드당 hazard pointer 1개
- retire list가 `2 * MAX_THREADS` 초과 시 scan

빌드·테스트:

```bash
cmake --workflow --preset debug-test
cmake --workflow --preset asan-test   # use-after-free 탐지
```

## 한계 및 미해결 문제

- ABA 미해결 — 해제는 막지만 같은 주소 재사용은 막지 못함. CAS 자체의 ABA는 tagged pointer나 버전 카운터로 별도 해결 필요.
- scan 비용 O(P²) — P = 스레드 수. 스레드가 늘수록 scan이 비싸짐.
- 지연 해제 — 최악 O(P²)개 노드가 retire list에 대기.
- 스레드 수 제한 — `MAX_THREADS` 초과 시 `acquire_slot`이 무한 spin.

## 대안

| 방법 | 특징 |
| :--- | :--- |
| **hazard pointer** | 읽는 쪽이 등록. 구현 단순. 스레드 수 제한. |
| **RCU** | 쓰는 쪽이 grace period 대기. 읽기 오버헤드 없음. 리눅스 커널 사용. |
| **epoch-based reclamation** | 전역 epoch 카운터. hazard pointer보다 오버헤드 낮음. |
| **tagged pointer** | 포인터에 버전 카운터 내장. ABA 직접 해결. 포인터 비트 제약. |

## 참고

- Maged M. Michael, "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects" — IEEE TPDS 2004
- Anthony Williams, "C++ Concurrency in Action" 7장
- Herb Sutter, "Lock-Free Programming" (CppCon 2014)
