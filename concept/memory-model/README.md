# memory-model

C++ 메모리 모델은 멀티스레드 프로그램의 correctness를 추론하는 언어 수준의 규칙이다. 하드웨어가 어떻게 동작하는지와 무관하게, "이 코드가 올바른가"를 판단하는 기준을 제공한다.

---

## 1. 왜 메모리 모델이 필요한가

하드웨어는 성능을 위해 재정렬을 한다 (OoO 실행, store buffer). 컴파일러도 최적화를 위해 재정렬을 한다. 멀티스레드에서는 이 재정렬이 예상치 못한 결과를 만든다.

```cpp
int data = 0;
bool ready = false;

// 스레드 A
data = 42;
ready = true;   // 컴파일러/CPU가 이걸 먼저 실행할 수 있음

// 스레드 B
while (!ready) {}
assert(data == 42);  // 실패할 수 있음
```

메모리 모델은 "어떤 조건에서 스레드 B가 스레드 A의 store를 볼 수 있는가"를 정의한다.

---

## 2. SC-DRF

C++ 메모리 모델의 핵심 계약:

> **데이터 레이스가 없는 프로그램은 순차 일관성(Sequential Consistency)을 보장받는다.**

### Sequential Consistency

모든 스레드의 연산이 전역적으로 하나의 순서로 일어나는 것처럼 보이는 것. 가장 직관적인 실행 모델 — "코드 순서대로 동작한다"는 느낌.

```
SC 보장 시:
스레드 A: x=1, y=2
스레드 B: r1=y, r2=x

r1=2이면 반드시 r2=1 (y가 보였으면 x도 보여야 함)
```

### Data Race

두 스레드가 같은 변수에 동시에 접근하는데 둘 중 하나라도 write면 → 데이터 레이스 → **UB**.

```cpp
int x = 0;
스레드 A: x = 1;       // write
스레드 B: int r = x;   // read, 동기화 없음 → 데이터 레이스
```

DRF = 데이터 레이스가 없음 = 모든 공유 변수 접근이 동기화되어 있음.

### 계약

```
개발자:           데이터 레이스 없게 만들게 (atomic + 올바른 memory_order)
컴파일러/하드웨어: SC처럼 동작하는 것을 보장해줄게
```

레이스가 하나라도 있으면 계약 파기 → 어떤 결과도 보장 안 됨.

---

## 3. happens-before

"A happens-before B" = **A의 결과가 B에게 반드시 보인다.**

형식적 정의보다 의미에 집중하면:

```
A hb→ B  라면:
  - A가 쓴 값을 B가 읽을 수 있음
  - A 이전의 모든 store도 B에게 보임
```

### 같은 스레드 내

프로그램 순서(sequenced-before)가 곧 happens-before.

```cpp
x = 1;       // A
int r = x;   // B

A hb→ B → r == 1 보장
```

### 스레드 간 — synchronizes-with

다른 스레드 사이에서 happens-before를 만들려면 **synchronizes-with** 관계가 필요하다.

```
A synchronizes-with B  →  A happens-before B
```

synchronizes-with를 만드는 것들:

- `release` store → `acquire` load (같은 변수, release가 먼저)
- mutex unlock → mutex lock
- `thread::join()`
- `memory_order_seq_cst` 연산들 사이

---

## 4. release / acquire

happens-before를 스레드 간에 연결하는 가장 기본적인 도구.

```cpp
std::atomic<bool> ready{false};
int data = 0;

// 스레드 A (producer)
data = 42;                              // (1)
ready.store(true, memory_order_release); // (2)

// 스레드 B (consumer)
while (!ready.load(memory_order_acquire)) {} // (3)
assert(data == 42);                          // (4) 항상 성공
```

### 왜 성공하는가

```
(1) sequenced-before (2)   → (1) hb→ (2)
(2) synchronizes-with (3)  → (2) hb→ (3)  [release-acquire 쌍]
(3) sequenced-before (4)   → (3) hb→ (4)

hb는 전이적(transitive): (1) hb→ (4)
→ data = 42가 assert에서 보임
```

### release의 의미

release store는 "이 store 이전의 모든 store가 완료됐음"을 선언한다. release 이전 연산들이 release 아래로 재정렬되지 않는다.

### acquire의 의미

acquire load는 "이 load 이후의 모든 load/store가 이 값을 본 다음에 실행됨"을 선언한다. acquire 이후 연산들이 acquire 위로 재정렬되지 않는다.

```
[모든 이전 store들]
      ↓
release store       ← 위로 올라올 수 없는 장벽
━━━━━━━━━━━━━━━━━━━
acquire load        ← 아래로 내려올 수 없는 장벽
      ↓
[모든 이후 load/store들]
```

---

## 5. memory_order_seq_cst

`memory_order_seq_cst`(기본값)는 acquire/release보다 강한 보장 — **전역 순서(total order)**를 모든 seq_cst 연산 사이에 부여한다.

```cpp
std::atomic<int> x{0}, y{0};

// 스레드 A          // 스레드 B
x.store(1);          y.store(1);

// 스레드 C          // 스레드 D
int r1 = x.load();   int r3 = y.load();
int r2 = y.load();   int r4 = x.load();
```

acquire/release만으로는 `r1=1, r2=0, r3=1, r4=0` 이 가능하다 — A와 B의 store 순서가 C와 D에게 다르게 보일 수 있음.

seq_cst는 이를 금지한다. 모든 seq_cst 연산이 **하나의 전역 순서**를 공유하므로, C와 D가 보는 x, y의 순서가 일치한다.

### IRIW (Independent Reads of Independent Writes)

위 시나리오를 IRIW라고 부른다. x86은 TSO라 하드웨어 수준에서 IRIW를 허용하지 않지만, ARM/POWER는 허용한다. seq_cst는 어떤 하드웨어에서도 IRIW를 금지한다.

### 비용

x86에서 seq_cst store는 `MFENCE` 또는 `XCHG`로 컴파일된다. acquire/release는 단순 MOV. seq_cst는 전역 순서 보장을 위해 더 비싼 명령어를 쓴다.

---

## 6. relaxed

순서 보장이 전혀 없는 가장 약한 memory_order. 원자성(atomicity)만 보장 — 찢어진 읽기(torn read)는 없지만, 다른 스레드가 언제 이 값을 볼지는 보장 안 됨.

```cpp
// 사용 예: 단순 카운터, 중단 플래그
std::atomic<bool> running{true};

// 스레드 A
running.store(false, memory_order_relaxed);

// 스레드 B
while (running.load(memory_order_relaxed)) { ... }
```

`running`의 정확한 전파 타이밍은 보장 안 되지만, 결국 false를 보게 된다. 그리고 false를 본 시점에 다른 데이터의 가시성을 보장할 필요가 없을 때 적합하다.

happens-before를 만들지 않는다 — synchronizes-with 관계 없음.

---

## 7. 요약 — memory_order 선택 기준

| memory_order | 보장                  | 용도                              |
| :----------- | :-------------------- | :-------------------------------- |
| `relaxed`    | 원자성만              | 카운터, 플래그 (순서 무관)        |
| `acquire`    | 이후 연산 재정렬 금지 | load 측 — 데이터 읽기 전          |
| `release`    | 이전 연산 재정렬 금지 | store 측 — 데이터 쓴 후           |
| `acq_rel`    | acquire + release     | RMW (fetch_add, CAS 등)           |
| `seq_cst`    | 전역 순서             | 기본값, 가장 안전하지만 가장 비쌈 |

### 실전 규칙

데이터를 전달할 때: **producer는 release, consumer는 acquire.**

```
producer: data 쓰기 → release store (flag)
consumer: acquire load (flag) → data 읽기
```

이 패턴이 성립하면 happens-before가 생기고 SC-DRF 계약이 유지된다.
