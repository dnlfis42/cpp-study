# memory-hierarchy

CPU가 메모리에 접근하는 방식의 전체 그림. 동시성 코드를 올바르게 이해하려면 하드웨어가 어떻게 동작하는지 알아야 한다.

---

## 1. CPU 파이프라인

현대 CPU는 명령어를 단계별로 나눠 처리한다 (fetch → decode → execute → writeback). 단계를 겹쳐 실행해서 클럭당 처리량을 높이는 것이 파이프라인이다.

### 레지스터

CPU 내부의 가장 빠른 저장소. 접근 레이턴시 1사이클. 용량은 수십 개 수준 (x86-64: rax~r15, xmm0~xmm15 등).

연산은 항상 레지스터 위에서 일어난다. 메모리 값을 쓰려면 먼저 레지스터로 load하고, 결과를 메모리에 store한다.

### 명령어 실행 흐름 (간략)

```
Fetch → Decode → Rename → Issue → Execute → Commit
```

- **Rename**: 아키텍처 레지스터(rax 등)를 물리 레지스터로 매핑. WAW/WAR 위험 제거.
- **Issue**: 준비된 명령어를 실행 유닛으로 내보냄.
- **Commit**: 순서대로 결과를 아키텍처 상태에 반영 (retire).

---

## 2. 비순서 실행 (Out-of-Order Execution)

CPU는 프로그램 순서와 다르게 명령어를 실행할 수 있다. 앞 명령어가 메모리 대기 중이어도 뒤 명령어가 준비됐으면 먼저 실행한다.

### ROB (Reorder Buffer)

발행된 명령어를 추적하는 원형 버퍼. 실행은 순서와 무관하게 일어나지만, **commit(retire)은 항상 프로그램 순서**대로 일어난다. 이를 통해 예외 처리와 아키텍처 상태의 일관성을 유지한다.

```
ROB 항목: [명령어 | 상태(실행중/완료) | 결과값 | 목적 레지스터]
          ↑ head (commit 대기)            ↑ tail (새 명령어 추가)
```

ROB가 가득 차면 새 명령어를 받지 못하고 파이프라인이 stall된다 — ROB stall.

### Store Buffer

store 명령어는 실행 즉시 캐시에 반영되지 않는다. 먼저 **store buffer**에 들어간다.

- store가 commit되면 store buffer → L1 캐시로 drain된다.
- 같은 코어의 이후 load가 같은 주소를 읽으면 store buffer에서 먼저 forwarding된다 (store-to-load forwarding).
- **다른 코어는 store buffer를 볼 수 없다** — 이것이 CPU 수준 메모리 재정렬의 핵심 원인.

### Load Buffer

투기적으로 실행된 load를 추적한다. 나중에 같은 주소에 예상치 못한 store가 끼어들면 load를 재실행한다.

### 재정렬 가시성

```
코어 A                    코어 B
x = 1  (store buffer 대기)
                          r1 = y  // 먼저 실행될 수 있음
y = 1  (store buffer 대기)
                          r2 = x  // x의 store가 아직 drain 안 됨 → 0 읽을 수 있음
```

x86은 TSO(Total Store Order)라 store→load 재정렬만 허용한다. ARM/POWER는 더 약한 모델로 더 많은 재정렬이 가능하다.

---

## 3. 캐시 계층

메모리 접근 레이턴시를 줄이기 위한 계층 구조. 빠를수록 작고, 느릴수록 크다.

### 전형적인 수치 (Intel, 참고값)

| 계층     | 레이턴시    | 용량          | 공유 범위    |
| :------- | :---------- | :------------ | :----------- |
| 레지스터 | ~1 사이클   | 수십 개       | 코어 전용    |
| L1 캐시  | ~4 사이클   | 32~64 KB      | 코어 전용    |
| L2 캐시  | ~12 사이클  | 256 KB~1 MB   | 코어 전용    |
| L3 (LLC) | ~40 사이클  | 수 MB~수십 MB | 소켓 내 공유 |
| DRAM     | ~200 사이클 | GB 단위       | 소켓 공유    |

L1 miss → L2 miss → LLC miss → DRAM 순으로 레이턴시가 5~50배씩 뛴다.

### Cache Line

캐시의 기본 전송 단위. x86에서 **64바이트**. 메모리 접근은 항상 cache line 단위로 일어난다.

```
한 int(4B)를 읽어도 → 해당 주소가 속한 64B 전체가 L1으로 올라온다
```

**공간 지역성(spatial locality)**: 인접한 데이터를 연속 접근하면 이미 올라온 cache line을 재사용 → 캐시 효율 극대화.

**false sharing**: 논리적으로 무관한 변수가 같은 cache line에 있을 때, 한 코어의 write가 다른 코어의 읽기를 방해하는 현상.

---

## 4. MESI 프로토콜

멀티코어에서 각 코어의 L1 캐시가 같은 주소에 대해 서로 다른 값을 가지면 안 된다. 이를 보장하는 것이 **캐시 일관성 프로토콜**이며, x86은 MESI를 사용한다.

### 4가지 상태

| 상태          | 의미                                     |
| :------------ | :--------------------------------------- |
| **M**odified  | 이 코어만 가짐, 메모리와 다른 값 (dirty) |
| **E**xclusive | 이 코어만 가짐, 메모리와 동일 값 (clean) |
| **S**hared    | 여러 코어가 동시에 가짐, 읽기만 가능     |
| **I**nvalid   | 유효하지 않음 (캐시에 없는 것과 같음)    |

### 상태 전이

**읽기 (load)**:

- Invalid → Shared (다른 코어도 있으면) 또는 Exclusive (혼자면)
- 다른 코어의 복사본을 Invalid로 만들지 않는다 → 읽기는 무효화 유발 안 함

**쓰기 (store)**:

- Shared/Invalid → Modified
- **다른 모든 코어의 복사본을 Invalid로 만든다** — RFO(Read For Ownership) 요청
- 쓰기만 무효화를 유발한다

### 전파 타이밍

invalidation은 1사이클에 전파되지 않는다:

```
코어 A store → store buffer → L1 반영 → coherence interconnect로 Invalidate 메시지
→ 코어 B L1 Invalid 전환 → Acknowledgement → 코어 A 완료
```

이 왕복이 수십~수백 사이클. 코어 B가 그 사이에 해당 주소를 읽으려 하면 stall 발생 — 이것이 **cache line ping-pong** 비용의 정체다.

### false sharing과 MESI

```
[cache line]: [head_ (consumer가 씀)] [tail_ (producer가 씀)]

consumer: head_.store → 이 cache line Modified → producer L1 Invalid
producer: 다음 tail_.store 전에 해당 cache line을 다시 fetch해야 함 (RFO)
```

두 변수가 논리적으로 독립적이어도, 같은 cache line에 있으면 서로의 write가 상대방 캐시를 무효화한다. `alignas(64)`로 각 변수를 별도 cache line에 배치하면 해결된다.

---

## 5. TLB / 페이징

CPU는 가상 주소로 메모리에 접근한다. 실제 물리 주소로의 변환은 **MMU(Memory Management Unit)**가 담당한다.

### 페이지 테이블

가상 주소 → 물리 주소 매핑 테이블. x86-64는 4단계 페이지 테이블(PML4 → PDP → PD → PT). 기본 페이지 크기 4KB.

페이지 테이블 자체가 메모리에 있으므로, 변환마다 최대 4번의 메모리 접근이 필요하다.

### TLB (Translation Lookaside Buffer)

페이지 테이블 변환 결과를 캐시하는 작은 하드웨어 캐시. 용량은 수십~수백 항목.

- **TLB hit**: 변환 결과 재사용 → 거의 공짜 (~1사이클)
- **TLB miss**: 페이지 테이블 워크 → ~수십 사이클 추가 (page table walk)

접근 패턴이 많은 페이지에 분산될수록 TLB miss 증가. 대용량 데이터 순회 시 성능 저하 원인 중 하나.

### Huge Page

4KB 대신 2MB(또는 1GB) 페이지를 사용. 같은 TLB 항목 수로 더 넓은 범위를 커버 → TLB miss 감소.

리눅스: `mmap(..., MAP_HUGETLB, ...)` 또는 transparent huge page(THP).

---

## 6. NUMA (Non-Uniform Memory Access)

### 하드웨어 구조 용어

| 용어             | 의미                                                                               |
| :--------------- | :--------------------------------------------------------------------------------- |
| **소켓 = 슬롯**  | 마더보드의 CPU 꽂는 자리. 물리적 커넥터                                            |
| **CPU 패키지**   | 소켓에 꽂는 칩 덩어리. 일상적으로 "CPU"라고 부르는 것                              |
| **다이 (die)**   | 패키지 안의 실리콘 조각. 패키지 하나에 다이 여러 개인 경우도 있음 (AMD EPYC)       |
| **코어**         | 다이 안에서 명령어를 독립적으로 실행하는 단위. 스레드가 실제로 올라가는 곳         |
| **하이퍼스레딩** | 코어 하나를 OS에 2개처럼 보이게 하는 기술. `lscpu`의 CPU 수가 코어 수의 2배인 이유 |

```
마더보드
└── 소켓 (= 슬롯)
    └── CPU 패키지         ← 일상어로 "CPU"
        └── 다이 (die)     ← 실리콘 조각
            ├── 코어 0     ← 명령어 실행 단위
            ├── 코어 1
            ├── ...
            ├── LLC        ← 코어들이 공유하는 L3 캐시
            └── 메모리 컨트롤러
```

**일반 데스크탑** (예: Intel i7-13700K):

```
소켓 1개 → 패키지 1개 → 다이 1개 → 코어 16개 → 하이퍼스레딩으로 OS에 24스레드
```

**서버 듀얼 소켓** (예: AMD EPYC 9654 × 2):

```
소켓 0 → 패키지 1개 → 96코어        소켓 1 → 패키지 1개 → 96코어
         로컬 메모리 연결                      로컬 메모리 연결
```

→ OS 입장에서는 코어 192개. 소켓마다 로컬 메모리가 따로 있어서 NUMA가 발생한다.

### NUMA란

멀티소켓 시스템에서 각 소켓은 자신에게 직접 연결된 로컬 메모리를 가진다. 다른 소켓의 메모리에 접근하면 **인터커넥트(QPI/UPI)** 를 거쳐야 해서 레이턴시가 2~3배 높아진다.

```
소켓 0 (코어 0~N)  ←─ QPI/UPI ─→  소켓 1 (코어 M~K)
    │                                     │
  메모리 0                              메모리 1
(로컬 접근 ~70ns)               (원격 접근 ~140ns)
```

### 영향

- 스레드가 소켓 0에서 실행되면서 소켓 1의 메모리를 읽으면 → 원격 접근 비용
- 스레드를 할당한 소켓의 메모리에서 할당하면(first-touch policy) 로컬 접근 유지
- `numactl --membind=0 --cpunodebind=0 ./program` 으로 소켓 고정 가능

멀티소켓 벤치 시 NUMA 효과를 배제하려면 `taskset -c`로 같은 소켓의 코어만 사용한다.

---

## 요약

```
레지스터(1cy) → L1(4cy) → L2(12cy) → LLC(40cy) → DRAM(200cy)

store: store buffer → L1 → coherence interconnect → 다른 코어 Invalid
load:  TLB 변환 → L1 확인 → miss면 L2/LLC/DRAM 순으로 올라옴

MESI: 읽기는 Shared(무효화 없음), 쓰기는 RFO → 다른 코어 Invalid
NUMA: 소켓 간 메모리 접근은 인터커넥트 경유 → 레이턴시 2~3배
```

동시성 코드에서 성능이 예상과 다를 때, 대부분 이 계층 중 하나에서 병목이 생긴다.
