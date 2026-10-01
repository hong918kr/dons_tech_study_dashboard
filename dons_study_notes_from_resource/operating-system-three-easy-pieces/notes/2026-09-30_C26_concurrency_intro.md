# Ch.26 동시성 입문 — 스레드, 경쟁 조건, 그리고 원자성에 대한 소망

> 📖 원문: [26. Concurrency: An Introduction](../book-md/C26_concurrency_an_introduction.md) · [PDF p.284](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=284) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.04](2026-09-30_C04_process.md), [Ch.06](2026-09-30_C06_limited_direct_execution.md), [Ch.13](2026-09-30_C13_address_spaces.md)

## 0. 한눈에 보기

- **스레드(thread)** 는 "주소 공간을 공유하는 프로세스"다. PC·레지스터·스택은 스레드마다 따로, 코드·힙·전역 변수는 같이 쓴다.
- 공유 변수 `counter = counter + 1` 은 기계어로 **load → add → store 3단계**다. 그 사이에 인터럽트(또는 다른 코어)가 끼어들면 업데이트가 사라진다 → **경쟁 조건(race condition)**.
- 이 Mac(M2, 8코어)에서 두 스레드가 1천만 번씩 더하면 2천만이 아니라 **약 1천만**이 나온다. 진짜 병렬이라 책(단일 CPU 기준)보다 훨씬 많이 잃는다.
- 해법의 방향: 하드웨어에서 몇 개의 **원자적(atomic)** 명령을 받고, OS의 도움을 더해 **동기화 프리미티브(락, 조건 변수)** 를 만든다. 다음 장들(27–31)의 주제.

> **THE CRUX: HOW TO PROVIDE SUPPORT FOR SYNCHRONIZATION** — "What support do we need from the hardware in order to build useful synchronization primitives? What support do we need from the OS? How can we build these primitives correctly and efficiently? How can programs use them to get the desired results?"
>
> → 쓸만한 동기화 프리미티브를 만들려면 하드웨어에서 무엇을, OS에서 무엇을 지원받아야 하나? 이를 어떻게 올바르고 효율적으로 만들고, 프로그램은 이를 어떻게 써서 원하는 결과를 얻나?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 스레드(thread) | 같은 주소 공간 안의 또 하나의 실행 흐름 (PC 하나 더) | 한 주방의 요리사 여러 명 |
| TCB(thread control block) | 스레드의 레지스터·상태를 저장하는 구조체 (PCB의 스레드 버전) | 커널의 `struct thread` |
| 스레드 문맥 교환 | 레지스터만 저장·복원, **페이지 테이블은 그대로** | TLB flush 불필요 |
| 스레드별 스택 | 스레드마다 스택이 하나씩, 주소 공간 곳곳에 흩어짐 | macOS 새 스레드 기본 512KB |
| thread-local storage | 스택 변수처럼 그 스레드만 쓰는 저장소 | `__thread int x;` |
| 경쟁 조건(race condition) | 실행 타이밍에 따라 결과가 달라지는 상황 | `counter++` 두 스레드 |
| 비결정적(indeterminate) | 실행마다 결과가 다를 수 있는 프로그램 | 10012624, 10068973, … |
| 임계 구역(critical section) | 공유 자원에 접근해서 동시에 실행되면 안 되는 코드 조각 | load/add/store 3줄 |
| 상호 배제(mutual exclusion) | 한 스레드가 임계 구역에 있으면 다른 스레드는 못 들어옴 | 화장실 열쇠 하나 |
| 원자성(atomicity) | "전부 아니면 전무" — 중간 상태가 안 보임 | `ldadd`, 트랜잭션 |
| 동기화 프리미티브 | 원자 명령 + OS로 만든 도구 | mutex, condvar, semaphore |
| 디스어셈블러 | 실행 파일을 기계어 목록으로 | `objdump -d`, `otool -tv` |

## 2. 스레드란 무엇인가 (26 도입부)

지금까지 OS는 두 가지 착시를 만들었다: CPU가 여러 개인 척(가상 CPU, [Ch.06](2026-09-30_C06_limited_direct_execution.md)), 메모리를 혼자 쓰는 척(주소 공간, [Ch.13](2026-09-30_C13_address_spaces.md)). 이번엔 **한 프로세스 안에 실행 지점을 여러 개** 두는 추상화, 스레드다.

스레드 하나의 상태는 프로세스와 거의 같다.

- 자기만의 **PC** (어디서 명령을 가져오는지)
- 자기만의 **레지스터 집합** → 같은 CPU에서 T1→T2로 바꾸려면 문맥 교환(context switch)이 필요하고, 상태는 **TCB** 에 저장한다.
- 하지만 **주소 공간은 공유** → 문맥 교환 때 페이지 테이블(=ARM의 TTBR, x86의 CR3)을 안 바꾼다. TLB도 그대로 쓸 수 있다. 그래서 스레드 전환이 프로세스 전환보다 싸다.

가장 눈에 띄는 차이는 **스택** 이다. 단일 스레드 프로세스는 스택이 하나(주소 공간 맨 아래)였지만, 스레드가 둘이면 스택도 둘이다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C26-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="160" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">단일 스레드 주소 공간</text>
  <text x="500" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">2-스레드 주소 공간</text>
  <rect x="90" y="35" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="160" y="60" text-anchor="middle" fill="currentColor">Program Code</text>
  <rect x="90" y="75" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="160" y="100" text-anchor="middle" fill="currentColor">Heap</text>
  <rect x="90" y="115" width="140" height="180" fill="none" stroke="currentColor"/>
  <text x="160" y="210" text-anchor="middle" fill="currentColor">(free)</text>
  <line x1="160" y1="120" x2="160" y2="150" stroke="currentColor" marker-end="url(#C26-arrow)"/>
  <line x1="160" y1="290" x2="160" y2="262" stroke="currentColor" marker-end="url(#C26-arrow)"/>
  <rect x="90" y="295" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="160" y="320" text-anchor="middle" fill="currentColor">Stack</text>
  <text x="80" y="40" text-anchor="end" fill="currentColor">0KB</text>
  <text x="80" y="340" text-anchor="end" fill="currentColor">16KB</text>
  <rect x="430" y="35" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="500" y="60" text-anchor="middle" fill="currentColor">Program Code</text>
  <rect x="430" y="75" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="500" y="100" text-anchor="middle" fill="currentColor">Heap (공유)</text>
  <rect x="430" y="115" width="140" height="70" fill="none" stroke="currentColor"/>
  <text x="500" y="155" text-anchor="middle" fill="currentColor">(free)</text>
  <rect x="430" y="185" width="140" height="40" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="500" y="210" text-anchor="middle" fill="currentColor">Stack (2)</text>
  <rect x="430" y="225" width="140" height="70" fill="none" stroke="currentColor"/>
  <text x="500" y="265" text-anchor="middle" fill="currentColor">(free)</text>
  <rect x="430" y="295" width="140" height="40" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="500" y="320" text-anchor="middle" fill="currentColor">Stack (1)</text>
  <text x="420" y="40" text-anchor="end" fill="currentColor">0KB</text>
  <text x="420" y="340" text-anchor="end" fill="currentColor">16KB</text>
  <text x="585" y="210" fill="currentColor">← T2 전용</text>
  <text x="585" y="320" fill="currentColor">← T1 전용</text>
  <text x="330" y="190" text-anchor="middle" fill="currentColor">힙/코드/전역은</text>
  <text x="330" y="208" text-anchor="middle" fill="currentColor">모든 스레드가 공유</text>
</svg>
```

스택이 주소 공간 중간에 박히면서 "힙은 위에서 아래로, 스택은 아래에서 위로 자란다"는 깔끔한 그림이 깨진다. 그래도 보통 괜찮다. 스레드 스택은 대개 작아도 되기 때문이다(재귀를 많이 쓰면 예외). 실제로 macOS에서 새 스레드의 기본 스택은 약 512KB, 메인 스레드는 8MB다 ([Ch.27](2026-09-30_C27_thread_api.md)의 실측 참고). 스레드 스택들 사이에는 **가드 페이지(guard page)** 가 있어서 넘치면 바로 segfault가 나게 한다.

> **펌웨어 관점 미리보기** — RTOS의 태스크가 정확히 이 모델이다. FreeRTOS `xTaskCreate()` 에 스택 크기를 직접 넘기고, 태스크들은 같은 (MMU 없는) 주소 공간을 공유한다. 차이는 범용 OS는 가드 페이지로 스택 오버플로를 잡아 주지만, 베어메탈은 stack canary/MPU 영역을 직접 걸어야 한다는 것.

## 3. 예제: 스레드 만들기와 실행 순서 (26.1)

책 Figure 26.2(t0.c): main이 "A"를 찍는 스레드와 "B"를 찍는 스레드를 만들고 `pthread_join()` 으로 둘 다 기다린다.

가능한 실행 순서는 여러 개다.

- Trace 1: main이 둘 다 만들고 join에서 기다리는 동안 T1 → T2 순으로 실행
- Trace 2: T1을 만들자마자 T1이 바로 실행 → 그 다음 T2 생성·실행 → join은 즉시 리턴
- Trace 3: **T2가 T1보다 먼저** 실행 ("B"가 "A"보다 먼저 찍힘)

핵심: **먼저 만든 스레드가 먼저 돈다는 보장은 없다.** 스레드 생성은 "함수 호출인데, 호출한 쪽으로 돌아오는 시점과 함수가 실행되는 시점이 스케줄러 마음대로"인 것이다.

이걸 숫자로 확인해 보자 (아래 §7.2). 이 Mac에서 10,000번 돌렸더니 B가 먼저 돈 경우가 **319번(3.2%)** 있었다. 드물지만 0이 아니다 — 이게 동시성 버그가 "가끔만" 나타나는 이유다.

## 4. 더 나빠지는 이유: 공유 데이터 (26.2)

Figure 26.6(t1.c): 두 스레드가 전역 `counter` 를 각각 1천만 번(1e7) `counter = counter + 1` 한다. 기대값은 20,000,000.

책은 단일 CPU에서도 19,345,221, 19,221,041 같은 틀린 값이 나온다고 했다. 이 Mac에서 직접 돌린 결과는 훨씬 극적이다 (아래 §7.1):

```text
race   counter = 10012624 (expected 20000000, lost  9987376)  0.030 s
race   counter = 10068973 (expected 20000000, lost  9931027)  0.023 s
```

**거의 정확히 절반을 잃는다.** 왜 책보다 훨씬 나쁠까? 책의 상황은 "한 CPU에서 타이머 인터럽트가 운 나쁘게 3줄 중간에 떨어질 때만" 잃는다(드묾). M2에서는 두 스레드가 **서로 다른 코어에서 진짜 동시에** load/add/store를 한다. 두 코어가 같은 값을 읽고 같은 값을 쓰는 일이 거의 매번 일어나니 절반 가까이 사라진다.

> **TIP — KNOW AND USE YOUR TOOLS** — 디스어셈블러(objdump), 디버거(gdb/lldb), 메모리 검사기(valgrind/ASan), 그리고 컴파일러 자체를 익혀라. 도구를 잘 쓸수록 더 좋은 시스템을 만든다.

## 5. 문제의 핵심: 통제되지 않는 스케줄링 (26.3)

### 5.1 `counter++` 는 명령 3개다

책은 x86 기준:

```text
mov 0x8049a1c, %eax    ; load
add $0x1, %eax         ; add
mov %eax, 0x8049a1c    ; store
```

이 Mac(arm64)에서 `objdump -d` 로 우리 프로그램을 까 보면 똑같이 3개다 (`-O0` 빌드, worker 함수의 race 경로):

```text
1000007bc:     	ldr	w8, [x9, #0x44]     ; counter 를 레지스터 w8 로 load
1000007c0:     	add	w8, w8, #0x1        ; w8 += 1
1000007c4:     	str	w8, [x9, #0x44]     ; w8 을 counter 에 store
```

`-O2` 로 `volatile int counter; counter = counter + 1;` 한 줄만 컴파일해도 `ldr w9,[x8]` / `add w9,w9,#0x1` / `str w9,[x8]` 로 같다. **`volatile` 은 "매번 메모리에서 읽고 써라"일 뿐, 원자성을 주지 않는다.** 펌웨어에서 레지스터 맵을 `volatile` 로 잡는 습관 때문에 헷갈리기 쉬운 지점이다.

### 5.2 책의 트레이스: 50 + 1 + 1 = 51 (Figure 26.7)

counter=50에서 시작, 코드는 주소 100(mov, 5바이트) / 105(add, 3바이트) / 108(mov)에 있다고 하자.

| 시점 | 실행 | T1 eax | T2 eax | counter |
|---|---|---|---|---|
| 1 | T1: mov (load) | 50 | – | 50 |
| 2 | T1: add | 51 | – | 50 |
| 3 | 인터럽트 → T1 상태 저장, T2 복원 | (51 저장됨) | 0 | 50 |
| 4 | T2: mov (load) | – | 50 | 50 |
| 5 | T2: add | – | 51 | 50 |
| 6 | T2: mov (store) | – | 51 | **51** |
| 7 | 인터럽트 → T2 저장, T1 복원 (PC=108) | 51 | – | 51 |
| 8 | T1: mov (store) | 51 | – | **51** |

`counter++` 를 두 번 실행했는데 50 → 51. 하나가 사라졌다. 핵심은 3단계에서 **T1의 eax=51이 TCB에 저장돼 있다가** 8단계에서 "옛날 값 기반의 결과"를 덮어쓴 것이다.

### 5.3 새 예제: 세 스레드면 얼마나 잃을 수 있나?

counter=0, 스레드 A, B, C가 각각 `counter++` 를 **한 번씩** 한다. 최악의 경우?

- A, B, C 모두 load → 셋 다 레지스터에 0
- 셋 다 add → 셋 다 1
- 셋 다 store → counter = 1

정답 3인데 결과 1. 일반화하면 **n개 스레드가 1번씩 → 최소 1**. 그런데 각 스레드가 여러 번(k번) 돌면 더 이상한 일이 생긴다. 2개 스레드가 k번씩 돌 때 최솟값은 몇일까? 놀랍게도 **2** 다 (자가 점검 Q4에서 풀어 보자).

### 5.4 용어 정리 (ASIDE: KEY CONCURRENCY TERMS)

- **임계 구역(critical section)**: 공유 자원(변수, 자료구조)에 접근하는 코드 조각. 여기서는 load/add/store 3줄.
- **경쟁 조건(race condition)**: 여러 실행 흐름이 거의 동시에 임계 구역에 들어가 공유 데이터를 바꾸면서 생기는 놀라운(그리고 원치 않는) 결과. 더 정확히는 **데이터 레이스(data race)** — 둘 이상이 같은 위치에 동시에 접근하고 그중 하나 이상이 쓰기이며 동기화가 없는 경우.
- **비결정적(indeterminate)**: 경쟁 조건이 하나 이상 있어서 실행할 때마다 출력이 달라지는 프로그램.
- **상호 배제(mutual exclusion)**: 한 번에 한 스레드만 임계 구역에 들어가게 보장하는 성질. 이걸 주는 도구가 락이다.

이 용어 대부분은 **다익스트라(Dijkstra)** 가 만들었다 (1968 "Cooperating Sequential Processes").

## 6. 원자성에 대한 소망 (26.4 – 26.6)

### 6.1 슈퍼 명령이 있다면?

```text
memory-add 0x8049a1c, $0x1   ; 메모리에 바로 1을 더하는, 원자적인 명령
```

하드웨어가 "인터럽트가 와도 이 명령은 실행 전이거나 완료 후, 중간 상태 없음"을 보장하면 문제 끝이다. **원자적(atomic)** = "하나의 단위로", 즉 all-or-nothing.

재미있는 건 이 슈퍼 명령이 **실제로 있다**는 것이다. ARMv8.1의 LSE(Large System Extensions) 원자 명령 `ldadd` 가 정확히 그것이다. 우리 프로그램의 `atomic_fetch_add` 는 M2에서 `ldadd` 한 개로 컴파일되고, 결과는 정확히 20,000,000이었다 ([Ch.28](2026-09-30_C28_locks.md)에서 어셈블리 확인).

하지만 일반적으로는 안 된다. 동시 B-tree를 고칠 때 "B-tree 원자적 갱신" 명령을 하드웨어에 요구할 순 없다. 그래서 전략은:

1. 하드웨어에 **몇 개의 유용한 원자 명령**만 받는다 (test-and-set, compare-and-swap, LL/SC, fetch-and-add — [Ch.28](2026-09-30_C28_locks.md)).
2. 여기에 **OS의 도움**(잠재우기/깨우기)을 더해
3. 임의 길이의 코드 덩어리를 원자적으로 실행하게 만드는 **동기화 프리미티브**(락)를 만든다.

### 6.2 또 하나의 문제: 다른 스레드를 기다리기 (26.5)

동시성의 상호작용은 "공유 변수 접근" 하나가 아니다. **한 스레드가 다른 스레드의 어떤 행동이 끝나길 기다려야** 하는 경우가 흔하다. 예: 프로세스가 디스크 I/O를 요청하고 잠들었다가, I/O가 끝나면 깨어나야 한다. 이건 원자성과 별개의 문제이고, **조건 변수(condition variable)** 로 푼다 → [Ch.30](2026-09-30_C30_condition_variables.md).

### 6.3 왜 OS 수업에서? (26.6)

답은 "역사". **OS가 최초의 동시성 프로그램**이었다. 인터럽트가 생긴 순간부터, 커널 자료구조를 고치는 도중에 인터럽트가 끼어들 수 있었다. 예: 두 프로세스가 같은 파일에 `write()` 로 append →

- 새 블록 할당 (비트맵 수정)
- 아이노드에 블록 위치 기록
- 파일 크기 갱신

이 모두가 임계 구역이다. 페이지 테이블, 프로세스 리스트, 파일 시스템 구조… 사실상 모든 커널 자료구조가 동기화 대상이다.

> **TIP — USE ATOMIC OPERATIONS** — "all or nothing" 은 컴퓨터 시스템 전체를 관통하는 아이디어다. 여러 동작을 하나로 묶은 것을 DB에서는 **트랜잭션(transaction)** 이라 부른다. 파일 시스템은 **저널링(journaling)** 이나 **copy-on-write** 로 디스크 상태를 원자적으로 바꾼다 → [Ch.42](2026-09-30_C42_crash_consistency_journaling.md).

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C26-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">멀티코어에서 counter++ 가 사라지는 순간 (M2 실측 상황)</text>
  <text x="20" y="70" fill="currentColor" font-weight="bold">코어 0 (T1)</text>
  <text x="20" y="170" fill="currentColor" font-weight="bold">코어 1 (T2)</text>
  <line x1="110" y1="65" x2="680" y2="65" stroke="currentColor" stroke-dasharray="2,4"/>
  <line x1="110" y1="165" x2="680" y2="165" stroke="currentColor" stroke-dasharray="2,4"/>
  <rect x="130" y="48" width="90" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="175" y="70" text-anchor="middle" fill="currentColor">ldr (=50)</text>
  <rect x="240" y="48" width="90" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="285" y="70" text-anchor="middle" fill="currentColor">add (=51)</text>
  <rect x="350" y="48" width="90" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="395" y="70" text-anchor="middle" fill="currentColor">str 51</text>
  <rect x="150" y="148" width="90" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="195" y="170" text-anchor="middle" fill="currentColor">ldr (=50)</text>
  <rect x="260" y="148" width="90" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="305" y="170" text-anchor="middle" fill="currentColor">add (=51)</text>
  <rect x="370" y="148" width="90" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="415" y="170" text-anchor="middle" fill="currentColor">str 51</text>
  <rect x="520" y="95" width="150" height="40" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="595" y="113" text-anchor="middle" fill="currentColor">메모리 counter</text>
  <text x="595" y="129" text-anchor="middle" fill="#d9534f" font-weight="bold">50 → 51 (52 아님)</text>
  <line x1="440" y1="70" x2="518" y2="105" stroke="currentColor" marker-end="url(#C26-arrow2)"/>
  <line x1="460" y1="168" x2="518" y2="128" stroke="currentColor" marker-end="url(#C26-arrow2)"/>
  <rect x="125" y="40" width="340" height="150" fill="none" stroke="#d9534f" stroke-dasharray="6,4"/>
  <text x="295" y="210" text-anchor="middle" fill="#d9534f">두 임계 구역이 시간상 겹침 = 경쟁 조건</text>
  <text x="350" y="245" text-anchor="middle" fill="currentColor">단일 CPU: 인터럽트가 3줄 사이에 "운 나쁘게" 떨어질 때만 발생 (책: 약 3% 손실)</text>
  <text x="350" y="265" text-anchor="middle" fill="currentColor">멀티코어: 인터럽트 없이도 진짜 동시에 겹침 (M2 실측: 약 50% 손실)</text>
  <text x="350" y="285" text-anchor="middle" fill="currentColor">→ 인터럽트 끄기로는 해결 불가, 하드웨어 원자 명령이 필요 (Ch.28)</text>
</svg>
```

## 7. 직접 해보기

### 7.1 경쟁 조건 재현 — counter 가 절반만 올라간다

`code/C26_race_counter.c`: 책의 t1.c 를 그대로 하되, 같은 프로그램에서 mutex 버전과 atomic 버전도 같이 돌려 비교한다.

```c
// C26_race_counter.c — OSTEP Fig 26.6 (t1.c) 를 macOS(Apple Silicon)에서 재현
// 두 스레드가 공유 counter 를 각각 N 번 ++ 한다.
//   mode race   : 그냥 counter = counter + 1   (경쟁 조건, 틀린 값)
//   mode mutex  : pthread_mutex 로 임계 구역 보호
//   mode atomic : C11 atomic_fetch_add (하드웨어 원자 명령 1개)
// build: cc -Wall -Wextra -O0 -pthread code/C26_race_counter.c -o .work/bin/C26_race_counter
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 10000000

static volatile int counter = 0;           // 책과 동일: volatile 은 원자성을 주지 않는다
static atomic_int acounter = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int mode = 0;                       // 0 race, 1 mutex, 2 atomic

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        if (mode == 0) {
            counter = counter + 1;         // load → add → store : 3단계
        } else if (mode == 1) {
            pthread_mutex_lock(&lock);
            counter = counter + 1;
            pthread_mutex_unlock(&lock);
        } else {
            atomic_fetch_add_explicit(&acounter, 1, memory_order_relaxed);
        }
    }
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void run(int m, const char *name) {
    pthread_t p1, p2;
    mode = m;
    counter = 0;
    atomic_store(&acounter, 0);
    double t0 = now();
    if (pthread_create(&p1, NULL, worker, "A") || pthread_create(&p2, NULL, worker, "B")) {
        perror("pthread_create");
        exit(1);
    }
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);
    double t1 = now();
    int result = (m == 2) ? atomic_load(&acounter) : counter;
    printf("%-6s counter = %8d (expected %d, lost %8d)  %.3f s\n",
           name, result, 2 * N, 2 * N - result, t1 - t0);
}

int main(void) {
    for (int r = 0; r < 5; r++) run(0, "race");
    run(1, "mutex");
    run(2, "atomic");
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C26_race_counter.c -o .work/bin/C26_race_counter && .work/bin/C26_race_counter
race   counter = 10012624 (expected 20000000, lost  9987376)  0.030 s
race   counter = 10068973 (expected 20000000, lost  9931027)  0.023 s
race   counter = 10065646 (expected 20000000, lost  9934354)  0.021 s
race   counter = 10007323 (expected 20000000, lost  9992677)  0.021 s
race   counter = 10054893 (expected 20000000, lost  9945107)  0.021 s
mutex  counter = 20000000 (expected 20000000, lost        0)  0.233 s
atomic counter = 20000000 (expected 20000000, lost        0)  0.175 s
```

읽는 법:

- **race**: 5번 다 다르고(비결정적), 매번 약 50% 손실. 두 코어가 같은 캐시라인을 두고 싸우며 대부분의 증가가 겹친다.
- **mutex**: 정확하지만 race보다 **약 10배 느리다** (0.023 → 0.233 s). 2천만 번 lock/unlock + 캐시라인 핑퐁 비용.
- **atomic**: 정확하고 mutex보다 빠르다. 하지만 race보다는 8배 느리다 — 원자 명령도 공짜가 아니다(캐시라인 독점 소유권을 매번 가져와야 함). 이 비용을 줄이는 법이 [Ch.29](2026-09-30_C29_concurrent_data_structures.md)의 sloppy counter.
- 디스어셈블 확인: `objdump -d --no-show-raw-insn --disassemble-symbols=_worker .work/bin/C26_race_counter` → race 경로가 `ldr / add / str` 3개(§5.1).

### 7.2 스레드 실행 순서는 보장되지 않는다

`code/C26_thread_order.c`: t0.c 를 10,000번 반복. 각 스레드는 시작하자마자 원자적 티켓을 뽑아 "몇 번째로 실행됐는지" 기록한다(printf 순서는 버퍼링 때문에 믿을 수 없어서).

```c
// C26_thread_order.c — OSTEP Fig 26.2 (t0.c) 를 10000 번 돌려서
// "먼저 만든 스레드가 먼저 실행된다" 는 보장이 없음을 숫자로 확인한다.
// 각 스레드는 시작하자마자 공유 티켓(atomic)을 하나 뽑아 자기 실행 순서를 기록한다.
// build: cc -Wall -Wextra -O0 -pthread code/C26_thread_order.c -o .work/bin/C26_thread_order
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_int ticket;
static int order[2];                 // order[i] = 스레드 i 가 몇 번째로 실행됐나

static void *mythread(void *arg) {
    int id = (int)(long)arg;
    order[id] = atomic_fetch_add(&ticket, 1);
    return NULL;
}

int main(void) {
    const int TRIALS = 10000;
    int a_first = 0, b_first = 0;
    for (int t = 0; t < TRIALS; t++) {
        pthread_t p1, p2;
        atomic_store(&ticket, 0);
        int rc = pthread_create(&p1, NULL, mythread, (void *)0L); assert(rc == 0);   // "A"
        rc = pthread_create(&p2, NULL, mythread, (void *)1L);     assert(rc == 0);   // "B"
        rc = pthread_join(p1, NULL); assert(rc == 0);
        rc = pthread_join(p2, NULL); assert(rc == 0);
        if (order[0] < order[1]) a_first++; else b_first++;
    }
    printf("trials=%d  A ran first: %d  B ran first: %d\n", TRIALS, a_first, b_first);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C26_thread_order.c -o .work/bin/C26_thread_order && .work/bin/C26_thread_order
trials=10000  A ran first: 9681  B ran first: 319
```

96.8%는 A가 먼저지만 **3.2%는 B가 먼저**다. "테스트 100번 돌려 봤는데 괜찮던데요"가 동시성에서 통하지 않는 이유다.

### 7.3 OSTEP 시뮬레이터: threads-intro/x86.py

가상의 x86 비슷한 CPU에서 스레드 인터리빙을 한 명령 단위로 보여 준다. `-i` 는 인터럽트 간격(명령 수), `-M` 은 추적할 메모리 주소, `-R` 은 추적할 레지스터.

```text
cd .tools/ostep-homework/threads-intro
cat looping-race-nolock.s
```

```text
# assumes %bx has loop count in it

.main
.top
# critical section
mov 2000, %ax  # get 'value' at address 2000
add $1, %ax    # increment it
mov %ax, 2000  # store it back

# see if we're still looping
sub  $1, %bx
test $0, %bx
jgt .top

halt
```

**실험 A — 인터럽트가 임계 구역 한가운데 (-i 2)**: 두 스레드가 각각 1번씩 증가.

```text
$ python3 ./x86.py -p looping-race-nolock.s -t 2 -a bx=1 -M 2000 -R ax -i 2 -c
 2000      ax          Thread 0                Thread 1
    0       0
    0       0   1000 mov 2000, %ax
    0       1   1001 add $1, %ax
    0       0   ------ Interrupt ------  ------ Interrupt ------
    0       0                            1000 mov 2000, %ax
    0       1                            1001 add $1, %ax
    0       1   ------ Interrupt ------  ------ Interrupt ------
    1       1   1002 mov %ax, 2000
    1       1   1003 sub  $1, %bx
    1       1   ------ Interrupt ------  ------ Interrupt ------
    1       1                            1002 mov %ax, 2000
    1       1                            1003 sub  $1, %bx
    ...
    1       1                            1006 halt
```

Thread 0이 load/add만 하고 쫓겨난 사이 Thread 1도 0을 읽는다. 둘 다 1을 store → **최종 x = 1** (정답 2). Figure 26.7과 똑같은 상황이다.

**실험 B — 임계 구역이 통째로 들어가는 간격 (-i 3)**:

```text
$ python3 ./x86.py -p looping-race-nolock.s -t 2 -a bx=1 -M 2000 -R ax -i 3 -c
 2000      ax          Thread 0                Thread 1
    0       0
    0       0   1000 mov 2000, %ax
    0       1   1001 add $1, %ax
    1       1   1002 mov %ax, 2000
    1       0   ------ Interrupt ------  ------ Interrupt ------
    1       1                            1000 mov 2000, %ax
    1       2                            1001 add $1, %ax
    2       2                            1002 mov %ax, 2000
    ...
```

3줄이 한 타임슬라이스 안에 끝나므로 **x = 2** (정답).

**실험 C — 루프 100번, 인터럽트 간격 스윕** (최종 x만 뽑음, 정답 200):

```text
$ for i in 1 2 3 4 5 6 7 8 9 12 15 21; do echo -n "i=$i: "; python3 ./x86.py -p looping-race-nolock.s -t 2 -a bx=100 -M 2000 -i $i -c | tail -1 | awk '{print $1}'; done
i=1: 100
i=2: 100
i=3: 200
i=4: 150
i=5: 160
i=6: 200
i=7: 157
i=8: 150
i=9: 200
i=12: 200
i=15: 200
i=21: 200
```

해석: 루프 한 바퀴는 명령 6개(mov/add/mov/sub/test/jgt). 간격이 **3의 배수(3, 6, 9, 12, 15, 21)** 면 인터럽트가 항상 임계 구역 경계(1000번지 직전이나 1003번지 직전)에 떨어져서 정답이 나온다. 그 외 간격에서는 일부 반복에서 임계 구역이 쪼개져 잃는다. **"어느 위치에서 인터럽트가 와도 안전한가?"** 를 따지는 게 곧 임계 구역을 찾는 일이다.

**실험 D — 랜덤 인터럽트, 시드만 바꿔서** (bx=3, 정답 6):

```text
$ for s in 0 1 2 3 4; do echo -n "s=$s: "; python3 ./x86.py -p looping-race-nolock.s -t 2 -a bx=3 -M 2000 -i 4 -r -s $s -c | tail -1 | awk '{print $1}'; done
s=0: 5
s=1: 3
s=2: 6
s=3: 3
s=4: 4
```

같은 프로그램인데 결과가 5, 3, 6, 3, 4 — **비결정성**의 정의 그대로다.

## 8. 펌웨어 엔지니어의 눈으로

- **ISR vs 메인 루프 = 이 장의 단일 CPU 경쟁 조건 그 자체.** SSD FW에서 NVMe doorbell ISR이 `pending_cmds++` 하고 메인 루프가 `pending_cmds--` 하면, Cortex-R/M에서도 `ldr/add/str` 사이에 인터럽트가 들어와 카운트가 틀어진다. 그래서 베어메탈은 임계 구역에서 `__disable_irq()` / `cpsid i` 를 쓴다 — [Ch.28](2026-09-30_C28_locks.md)의 "인터럽트 끄기" 해법.
- **멀티코어 SSD 컨트롤러(호스트 I/F 코어, FTL 코어, NAND 채널 코어)** 는 M2 실험과 같은 진짜 병렬이다. 인터럽트 끄기는 다른 코어를 못 막는다. 코어 간 공유 큐는 하드웨어 세마포어/메일박스 IP나 `ldrex/strex` 기반 스핀락으로 보호한다.
- **`volatile` 은 동기화가 아니다.** MMIO 레지스터에 `volatile` 을 쓰는 습관이 강한데, 그건 "컴파일러가 접근을 없애거나 합치지 마라"일 뿐, read-modify-write 원자성도, 다른 코어에 대한 순서 보장도 주지 않는다. W1C(write-1-to-clear) 레지스터가 있는 이유도 RMW 경쟁을 하드웨어에서 없애려는 것이다.
- **Apple SoC의 코프로세서**(예: AOP, SEP, ANE 펌웨어)와 AP가 공유 메모리 링으로 통신할 때도 같은 문제다. 생산자 index와 소비자 index를 **각자 한 쪽만 쓰게** 설계하면 락 없이 갈 수 있는데, 이게 NVMe SQ/CQ의 head/tail doorbell 설계와 같은 발상이다.
- **AI 가속기**의 커맨드 큐에 여러 호스트 스레드가 커맨드를 넣으면, write pointer 갱신이 임계 구역이 된다. GPU 드라이버들이 per-thread 커맨드 버퍼를 두고 마지막에 한 번만 submit하는 이유.

## 9. 면접 질문

### Q1. 스레드와 프로세스의 차이를 "문맥 교환 비용" 관점에서 설명해 보라.
<details>
<summary>답 보기</summary>

- 둘 다 레지스터(PC, SP, 범용 레지스터)를 저장·복원해야 한다 (스레드는 **TCB**, 프로세스는 PCB).
- 프로세스 전환은 **주소 공간이 바뀌므로** 페이지 테이블 베이스 레지스터(CR3/TTBR0)를 바꾸고, ASID가 없으면 **TLB flush** 가 필요하다. 이후 TLB·캐시 미스가 늘어난다.
- 같은 프로세스의 스레드 전환은 **페이지 테이블이 그대로**라 TLB와 캐시가 따뜻하게 유지된다 → 훨씬 싸다.
- 대가는 **격리 없음**: 한 스레드의 메모리 오염이 전체를 망가뜨린다.

</details>

### Q2. `counter++` 를 두 스레드가 동시에 하면 왜 틀린 값이 나오나? `volatile` 을 붙이면 해결되나?
<details>
<summary>답 보기</summary>

- `counter++` 는 **load → add → store** 3개 명령이다. 두 실행 흐름이 같은 옛 값을 load하면 한 쪽 증가가 덮어써진다 (lost update).
- 단일 CPU에선 그 사이에 **타이머 인터럽트 + 문맥 교환**이 들어올 때, 멀티코어에선 인터럽트 없이도 **진짜 동시 실행**으로 생긴다.
- `volatile` 은 **컴파일러 최적화(레지스터 캐싱, 접근 제거)만 막는다**. 원자성도, 메모리 순서도 보장하지 않는다. M2 실측에서 `volatile int` 로도 2천만 중 약 1천만만 남았다.
- 해결: **락**(mutex) 또는 **원자 명령**(`atomic_fetch_add` → arm64 `ldadd`, x86 `lock xadd`).

</details>

### Q3. 경쟁 조건(race condition)과 데이터 레이스(data race)는 같은 말인가?
<details>
<summary>답 보기</summary>

- **데이터 레이스**: 둘 이상의 스레드가 같은 메모리 위치에 동시에 접근, 적어도 하나가 쓰기, 그리고 둘 사이에 **happens-before 동기화가 없음**. C/C++ 메모리 모델에서는 **정의되지 않은 동작(UB)**. ThreadSanitizer가 잡는 대상.
- **경쟁 조건**: 더 넓은 개념. 결과가 실행 타이밍에 따라 달라지는 **논리적** 버그. 모든 접근을 락으로 감싸도(데이터 레이스 0) 생길 수 있다 — 예: `if (balance >= x) { lock; balance -= x; unlock; }` 처럼 검사와 행동 사이가 끊긴 **check-then-act**.
- 즉 데이터 레이스 없음 ≠ 경쟁 조건 없음.

</details>

### Q4. 단일 코어 RTOS에서 ISR과 태스크가 공유하는 변수를 보호하는 방법들과 그 트레이드오프는?
<details>
<summary>답 보기</summary>

- **인터럽트 마스킹**(`cpsid i` / `taskENTER_CRITICAL`): 가장 단순하고 확실. 단 **인터럽트 지연(latency)** 이 늘어나므로 아주 짧게 써야 한다. 우선순위 기반 마스킹(BASEPRI)으로 고우선 ISR은 살려 둘 수 있다.
- **원자 명령**(`ldrex/strex`, Cortex-M3+): 카운터·플래그처럼 단일 워드면 마스킹 없이 가능.
- **락-프리 설계**: 단일 생산자/단일 소비자 링 버퍼 — ISR이 tail만, 태스크가 head만 쓴다. 메모리 배리어(`dmb`)로 데이터 쓰기 → 인덱스 갱신 순서만 지키면 된다.
- **뮤텍스는 ISR에서 못 쓴다** (ISR은 잠들 수 없음). ISR → 태스크 전달은 세마포어 give/queue send(FromISR 버전)를 쓴다.

</details>

### Q5. 스레드마다 스택이 따로 있다는 사실이 버그로 이어지는 대표적인 경우는?
<details>
<summary>답 보기</summary>

- **스택 변수의 주소를 다른 스레드에 넘기는 것**: `pthread_create(&t, NULL, f, &local)` 후 생성한 함수가 리턴하면 `local` 은 사라진다 → dangling pointer. 스레드에서 스택 변수 주소를 `return` 하는 것도 같다 ([Ch.27](2026-09-30_C27_thread_api.md)).
- **스택 오버플로**: 새 스레드 스택은 작다(macOS 약 512KB, 리눅스 보통 8MB, RTOS는 수 KB). 큰 지역 배열이나 깊은 재귀가 가드 페이지를 넘으면 segfault (RTOS에선 조용한 메모리 오염).
- **루프 변수 주소 넘기기**: `for (i..) pthread_create(..., &i)` → 모든 스레드가 같은 `i` 를 보며 값이 바뀌어 있다.

</details>

## 10. 자가 점검 & 숙제

### 퀴즈

Q1. 스레드 전환 때 저장·복원하지 **않아도** 되는 것은? (a) PC (b) 스택 포인터 (c) 페이지 테이블 베이스 레지스터 (d) 범용 레지스터

<details>
<summary>정답</summary>

(c). 같은 프로세스의 스레드는 주소 공간을 공유하므로 페이지 테이블을 바꿀 필요가 없다. SP는 스레드마다 다른 스택을 가리키므로 반드시 바꿔야 한다.

</details>

Q2. x86.py 실험 C에서 `-i 3` 과 `-i 6` 은 정답(200), `-i 4` 는 150이었다. 루프 한 바퀴가 명령 6개라는 걸 써서 이유를 설명하라.

<details>
<summary>정답</summary>

간격이 3의 배수면 인터럽트가 항상 "명령 0 직전(=1000번지)" 또는 "명령 3 직전(=1003번지, sub)"에 떨어진다. 둘 다 임계 구역(1000–1002) 바깥이다. 간격 4면 경계가 4, 8(=2), 12(=0)… 로 돌아가며 일부가 임계 구역 중간(1001, 1002 앞)에 떨어져 그 반복에서 업데이트가 사라진다.

</details>

Q3. M2에서 race 버전이 책(단일 CPU 기준 약 3% 손실)보다 훨씬 많이(약 50%) 잃는 이유는?

<details>
<summary>정답</summary>

두 스레드가 서로 다른 코어에서 **진짜 동시에** 실행되기 때문이다. 단일 CPU에선 타이머 인터럽트(수 ms마다)가 3명령 사이에 떨어질 때만 손실이 나지만, 멀티코어에선 두 코어가 같은 캐시라인을 번갈아 읽고 쓰며 거의 매 반복이 겹칠 수 있다.

</details>

Q4. (도전) 두 스레드가 각각 `counter++`(load/add/store)를 **k번**(k ≥ 2) 실행한다. 인터리빙을 마음대로 고를 수 있다면 최종 counter의 최솟값은?

<details>
<summary>정답</summary>

**2**. 시나리오: ① T1이 load(0). ② T2가 k−1번을 다 실행(counter = k−1). ③ T1이 store(1) → counter = 1. ④ T2가 마지막 반복의 load(1). ⑤ T1이 나머지 k−1번을 다 실행. ⑥ T2가 add/store → counter = 2. 1은 불가능하다(마지막 store는 최소 1번의 완전한 증가 이후 읽은 값을 기반으로 하기 때문). 동시성 버그의 결과 범위가 직관보다 훨씬 넓다는 걸 보여 주는 고전 문제.

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q6** (`-i 4 -r -s 0,1,2…`): 트레이스만 보고 최종값을 맞힐 수 있나? → "어디에 인터럽트가 와도 안전한가"로 **임계 구역의 정확한 경계**를 찾는 연습.
- **Q7–Q8** (`-a bx=1` / `bx=100` 으로 `-i` 스윕): 위 실험 C처럼 **어떤 간격이 우연히 정답을 주는지** 확인 → "테스트가 통과했다"가 정답의 증거가 아님을 체득.
- **Q9–Q10** (`wait-for-me.s`): 한 스레드가 메모리 플래그를 돌면서 기다리는 코드 → **spin-wait가 CPU를 얼마나 낭비하는지** 체감. [Ch.30](2026-09-30_C30_condition_variables.md)의 동기.

## 11. 다음으로

- 다음 장 [Ch.27 Thread API](2026-09-30_C27_thread_api.md): pthread_create/join/mutex/cond 의 실제 사용법과 함정.
- 그 다음 [Ch.28 Locks](2026-09-30_C28_locks.md): 오늘의 "슈퍼 명령"(test-and-set, CAS, LL/SC, fetch-and-add)으로 락을 직접 만든다.
- "다른 스레드를 기다리기" 는 [Ch.30 조건 변수](2026-09-30_C30_condition_variables.md)에서.
