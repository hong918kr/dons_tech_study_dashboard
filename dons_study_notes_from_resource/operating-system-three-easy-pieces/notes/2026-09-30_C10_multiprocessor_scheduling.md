# Ch.10 멀티프로세서 스케줄링 — 캐시 친화성, 큐 하나 vs 큐 여러 개

> 📖 원문: [10. Multiprocessor Scheduling (Advanced)](../book-md/C10_multiprocessor_scheduling_advanced.md) · [PDF p.117](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=117) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.08](2026-09-30_C08_mlfq.md), [Ch.09](2026-09-30_C09_proportional_share.md) (동시성 파트 [Ch.26](2026-09-30_C26_concurrency_intro.md), [Ch.28](2026-09-30_C28_locks.md) 를 먼저 보면 더 쉽다)

## 0. 한눈에 보기

멀티코어가 흔해지면서 스케줄러는 "다음에 **누구를**"에 더해 "**어느 CPU 에서**"까지 정해야 한다. 새로 생기는 문제는 셋이다. (1) 하드웨어 **캐시 일관성(cache coherence)** 은 하드웨어가 해 주지만, (2) 공유 자료구조에는 여전히 **동기화(락)** 가 필요하고 그게 확장성을 깎는다. (3) 프로세스는 돌던 CPU 의 캐시에 상태를 쌓아 두므로 **캐시 친화성(cache affinity)** 을 지켜 주면 빨라진다. 이 셋 때문에 **SQMS**(큐 하나)와 **MQMS**(CPU 마다 큐)의 트레이드오프가 생기고, MQMS 의 약점인 **부하 불균형**은 **이주(migration)** 와 **작업 훔치기(work stealing)** 로 푼다.

> **CRUX: HOW TO SCHEDULE JOBS ON MULTIPLE CPUs** — "How should the OS schedule jobs on multiple CPUs? What new problems arise? Do the same old techniques work, or are new ideas required?"
>
> (여러 CPU 에 job 을 어떻게 배치할까? 어떤 새 문제가 생기나? 옛 기법이 통하나, 새 아이디어가 필요한가?)

> **CRUX: HOW TO DEAL WITH LOAD IMBALANCE** — "How should a multi-queue multiprocessor scheduler handle load imbalance, so as to better achieve its desired scheduling goals?"
>
> (다중 큐 스케줄러는 부하 불균형을 어떻게 다뤄야 원하는 목표를 달성할까?)

| | SQMS (큐 하나) | MQMS (CPU 마다 큐) |
|---|---|---|
| 구현 | 단일 CPU 정책 재사용, 쉬움 | 복잡 (배치·이주 정책 필요) |
| 확장성 | 나쁨 (락 하나에 경쟁) | 좋음 (큐마다 락) |
| 캐시 친화성 | 나쁨 (job 이 CPU 사이를 튐) | 자연스럽게 좋음 |
| 부하 균형 | 자연스럽게 좋음 | 불균형 → migration / work stealing |

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 멀티코어 (multicore) | 한 칩에 CPU 코어 여러 개 | Apple M2: P코어 4 + E코어 4 |
| 캐시 (cache) | 자주 쓰는 데이터의 작고 빠른 사본 | L1 64 KB 대, 수 ns |
| 시간 지역성 (temporal locality) | 쓴 데이터를 곧 다시 씀 | 루프 변수 |
| 공간 지역성 (spatial locality) | 주소 x 근처를 곧 씀 | 배열 순회 |
| 캐시 일관성 (cache coherence) | 여러 캐시가 같은 주소에 대해 일관된 값을 보이게 함 | D vs D′ 문제 |
| 버스 스누핑 (bus snooping) | 캐시가 버스를 엿보다 갱신을 보면 무효화/갱신 | Goodman 1983 |
| write-back 캐시 | 메모리 쓰기를 나중으로 미룸 | 스누핑을 어렵게 함 |
| 캐시 친화성 (cache affinity) | 프로세스를 같은 CPU 에서 계속 돌리면 빠름 | 캐시·TLB 가 이미 따뜻함 |
| SQMS | single-queue multiprocessor scheduling | 전역 run queue 하나 |
| MQMS | multi-queue multiprocessor scheduling | CPU 별 run queue |
| 부하 불균형 (load imbalance) | 한 CPU 는 놀고 다른 CPU 는 바쁨 | Q0 비고 Q1 에 B, D |
| 이주 (migration) | job 을 다른 CPU 큐로 옮김 | B 를 CPU 0 으로 |
| 작업 훔치기 (work stealing) | 한가한 큐가 바쁜 큐를 엿보고 job 을 가져옴 | Cilk 런타임 |
| false sharing | 다른 변수인데 같은 캐시 라인이라 서로 무효화 | 이 노트의 C 실험 |

## 2. 배경: 멀티프로세서 하드웨어 (10.1)

### 2.1 단일 CPU 와 캐시

CPU 하나에 작은 캐시(예: 64 KB)와 큰 메인 메모리가 있다. 처음 load 하면 메모리에서 가져오느라 오래 걸리고(수십~수백 ns), 그 사본을 캐시에 둔다. 다시 읽으면 캐시에서 몇 ns 만에 온다. 캐시가 통하는 이유는 **지역성**: 시간 지역성(루프에서 같은 변수 반복), 공간 지역성(배열을 순서대로).

### 2.2 CPU 가 둘이면: 캐시 일관성 문제

메모리는 하나를 공유하고 CPU 마다 캐시가 있다. 원문 시나리오:

1. CPU 1 의 프로그램이 주소 A 를 읽는다 → 메모리에서 값 **D** 를 가져와 CPU 1 캐시에 둔다.
2. 프로그램이 A 를 **D′** 로 바꾼다. write-back 캐시라 CPU 1 캐시만 바뀌고 메모리는 아직 D.
3. OS 가 이 프로그램을 **CPU 2 로 옮긴다.**
4. CPU 2 에서 A 를 다시 읽으면 CPU 2 캐시엔 없으니 메모리에서 **옛 값 D** 를 가져온다. 틀렸다!

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C10-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
    <marker id="C10-arrow-warn" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="#d9534f"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">캐시 일관성 문제 (Fig 10.2 + 원문 시나리오) 와 스누핑 해법</text>
  <rect x="60" y="40" width="160" height="40" fill="none" stroke="currentColor"/>
  <text x="140" y="65" fill="currentColor" text-anchor="middle">CPU 1</text>
  <rect x="60" y="95" width="160" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="140" y="113" fill="currentColor" text-anchor="middle">Cache 1</text>
  <text x="140" y="129" style="fill:var(--accent)" text-anchor="middle" font-weight="bold">A = D′ (dirty)</text>
  <rect x="400" y="40" width="160" height="40" fill="none" stroke="currentColor"/>
  <text x="480" y="65" fill="currentColor" text-anchor="middle">CPU 2</text>
  <rect x="400" y="95" width="160" height="40" fill="none" stroke="currentColor"/>
  <text x="480" y="113" fill="currentColor" text-anchor="middle">Cache 2</text>
  <text x="480" y="129" fill="#d9534f" text-anchor="middle" font-weight="bold">A = D (낡은 값!)</text>
  <line x1="40" y1="170" x2="600" y2="170" stroke="currentColor" stroke-width="3"/>
  <text x="610" y="175" fill="currentColor">Bus</text>
  <line x1="140" y1="135" x2="140" y2="170" stroke="currentColor"/>
  <line x1="480" y1="135" x2="480" y2="170" stroke="currentColor"/>
  <rect x="220" y="210" width="200" height="40" fill="none" stroke="currentColor"/>
  <text x="320" y="235" fill="currentColor" text-anchor="middle">Memory: A = D (아직 옛 값)</text>
  <line x1="320" y1="170" x2="320" y2="210" stroke="currentColor"/>
  <path d="M 220 70 C 280 30, 340 30, 398 60" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#C10-arrow)"/>
  <text x="260" y="38" fill="currentColor" font-size="12">③ OS 가 프로세스를 CPU 2 로 이주</text>
  <path d="M 420 210 C 470 205, 490 190, 482 140" fill="none" stroke="#d9534f" stroke-width="1.5" marker-end="url(#C10-arrow-warn)"/>
  <text x="440" y="225" fill="#d9534f" font-size="12">④ 메모리에서 D 를 읽음</text>
  <text x="20" y="152" fill="currentColor" font-size="12">①읽고 ②D′로 씀</text>
  <text x="40" y="280" fill="currentColor" font-size="12">해법(하드웨어): 각 캐시가 버스를 엿보다(snoop) 자기가 가진 줄에 대한 쓰기를 보면 무효화(invalidate) 또는 갱신(update).</text>
  <text x="40" y="296" fill="currentColor" font-size="12">CPU 2 의 읽기 요청을 CPU 1 캐시가 보고 D′ 를 직접 넘겨주므로 프로그램은 항상 D′ 를 본다.</text>
</svg>
```

이게 **캐시 일관성 문제**다. 기본 해법은 **하드웨어**가 준다: 메모리 접근을 감시해 "공유 메모리 하나"라는 착시를 유지한다. 버스 기반 시스템에서는 고전적 **버스 스누핑(bus snooping)** 을 쓴다 — 각 캐시가 메모리와 연결된 버스를 지켜보다가, 자기가 가진 데이터에 대한 갱신을 보면 자기 사본을 **무효화(invalidate)** 하거나 **갱신(update)** 한다. write-back 캐시는 메모리 쓰기가 늦게 보이므로 더 복잡하지만, 기본 원리는 같다. (보충: 현대 CPU 는 MESI/MOESI 같은 상태 기반 프로토콜과, 코어가 많으면 버스 대신 디렉터리 기반 일관성을 쓴다.)

중요한 결과: 일관성은 **정확성**을 보장할 뿐, **공짜가 아니다.** 두 CPU 가 같은 캐시 라인에 번갈아 쓰면 라인이 CPU 사이를 핑퐁하며 매번 수십 ns 가 든다. 아래 "직접 해보기"에서 이 비용을 직접 잰다(6배).

## 3. 동기화를 잊지 마라 (10.2)

하드웨어가 일관성을 맞춰 주면 프로그램(이나 OS)은 공유 데이터를 그냥 써도 될까? **아니다.** 일관성은 "한 주소의 값"을 맞출 뿐, 여러 단계로 된 **자료구조 갱신의 원자성**은 보장하지 않는다. 원문 Fig 10.3 의 리스트 pop:

```c
typedef struct __Node_t {
    int              value;
    struct __Node_t *next;
} Node_t;

int List_Pop() {
    Node_t *tmp = head;             // remember old head ...
    int value    = head->value;     // ... and its value
    head         = head->next;      // advance head to next pointer
    free(tmp);                      // free old head
    return value;                   // return value at head
}
```

두 CPU 의 스레드가 동시에 들어오면 둘 다 같은 `head` 를 `tmp` 에 담고(각자의 스택에), 같은 원소를 두 번 pop 하려 든다 → **같은 값을 두 번 반환**하고 **같은 노드를 두 번 free(double free)**.

해법은 락이다: `pthread_mutex_t m;` 을 두고 함수 시작에 `lock(&m)`, 끝에 `unlock(&m)`. 그런데 **CPU 수가 늘수록 공유 자료구조 하나에 대한 동기화된 접근은 점점 느려진다** — 이게 아래 SQMS 의 확장성 문제로 그대로 이어진다. (락 자체는 [Ch.28](2026-09-30_C28_locks.md), 동시 자료구조는 [Ch.29](2026-09-30_C29_concurrent_data_structures.md).)

## 4. 마지막 문제: 캐시 친화성 (10.3)

프로세스는 한 CPU 에서 돌면서 그 CPU 의 캐시와 **TLB** 에 상태를 잔뜩 쌓는다. 다음에도 **같은 CPU** 에서 돌면 상태가 일부 남아 있어 빠르다. 매번 다른 CPU 에서 돌면 매번 다시 실어야 해서 느리다(일관성 프로토콜 덕분에 **정확하게는** 돈다). 그래서 멀티프로세서 스케줄러는 **가능하면 같은 CPU 에 붙여 두는 것**을 고려해야 한다.

## 5. 단일 큐 스케줄링 — SQMS (10.4)

가장 단순한 방법: 단일 CPU 스케줄러를 그대로 두고, 스케줄할 job 을 **큐 하나**에 넣는다. CPU 가 둘이면 "다음 최선의 job" 대신 "다음 최선의 job 두 개"를 고르면 된다. 장점은 **단순함**.

단점 둘:

1. **확장성 부족.** 여러 CPU 가 큐 하나를 건드리니 락이 필요하고, CPU 가 늘수록 그 락 경쟁에 시간을 쓴다. (원문은 "언젠가 실측을 넣고 싶다"고 했는데 — 아래 "직접 해보기" Part 2 에서 실제로 쟀다: 8스레드에서 큐 하나는 큐 여럿보다 **20배** 느렸다.)
2. **캐시 친화성 붕괴.** job 5개(A~E), CPU 4개, 각 CPU 가 slice 마다 전역 큐에서 다음 job 을 가져가면:

```text
Queue → A → B → C → D → E → NULL

CPU 0   A  E  D  C  B  ... (repeat) ...
CPU 1   B  A  E  D  C  ... (repeat) ...
CPU 2   C  B  A  E  D  ... (repeat) ...
CPU 3   D  C  B  A  E  ... (repeat) ...
```

모든 job 이 CPU 사이를 계속 튄다 — 친화성 관점에서 정확히 반대로 하고 있다. 그래서 대부분의 SQMS 는 **친화성 장치**를 덧붙인다. 일부 job 은 붙여 두고 일부만 옮겨 부하를 맞추는 식:

```text
CPU 0   A  E  A  A  A  ... (repeat) ...
CPU 1   B  B  E  B  B  ... (repeat) ...
CPU 2   C  C  C  E  C  ... (repeat) ...
CPU 3   D  D  D  D  E  ... (repeat) ...
```

A~D 는 제자리, E 만 떠돈다. 다음 라운드엔 다른 job 을 떠돌게 해서 "친화성 공정성"까지 챙길 수 있지만, 구현이 복잡해진다.

## 6. 다중 큐 스케줄링 — MQMS (10.5)

CPU 마다 큐를 둔다. 각 큐는 RR 같은 자기 정책을 따른다. job 이 들어오면 어떤 휴리스틱(무작위, 덜 찬 큐 등)으로 **정확히 한 큐**에 넣고, 그 다음은 큐마다 독립적으로 스케줄한다 → 공유·동기화 문제가 사라진다.

**원문 예.** CPU 2개, job A, B, C, D.

```text
Q0 → A → C        Q1 → B → D

CPU 0   A A C C A A C C A A C C ...
CPU 1   B B D D B B D D B B D D ...
```

장점: 큐 수가 CPU 수에 비례해 늘어나 락/캐시 경쟁이 중심 문제가 되지 않는다(**확장성**). job 이 같은 CPU 에 머무니 **캐시 친화성**이 공짜로 따라온다.

### 6.1 그런데 새 문제: 부하 불균형

C 가 끝나면:

```text
Q0 → A            Q1 → B → D

CPU 0   A A A A A A A A A A A A ...
CPU 1   B B D D B B D D B B D D ...
```

A 가 B, D 의 **2배** CPU 를 받는다. A 와 C 가 둘 다 끝나면 Q0 가 비어 **CPU 0 이 논다.**

### 6.2 해법: 이주(migration)

job 을 한 CPU 에서 다른 CPU 로 옮긴다.

- Q0 비고 Q1 에 B, D → B 나 D 하나를 CPU 0 으로 **한 번 옮기면** 끝.
- Q0 에 A, Q1 에 B, D → 한 번 옮겨선 안 된다(3개를 2개 CPU 에 나누면 항상 한쪽이 2개). **계속 옮겨야** 한다:

```text
CPU 0   A A A A B A B A B B B B ...
CPU 1   B D B D D D D D A D A D ...
```

처음엔 A 혼자 CPU 0, B·D 가 CPU 1. 몇 slice 뒤 B 를 CPU 0 으로 옮겨 A 와 경쟁시키고 D 는 CPU 1 을 혼자 쓴다. 그 다음엔 A 를 CPU 1 로… 이렇게 돌리면 긴 구간에서 각 job 이 2/3 CPU 씩 받는다.

**계산.** 3 job, 2 CPU 이면 이상적 몫 = 2/3 = 66.7%. 이주 없이 A 혼자 CPU 0 이면 A 100%, B·D 각 50% — B·D 의 반환시간은 이상적인 경우보다 (2/3)/(1/2) = 1.33배 길어진다.

### 6.3 언제 옮길까: 작업 훔치기(work stealing)

**work stealing** [FLR98]: job 이 적은 (source) 큐가 가끔 다른 (target) 큐를 **엿본다(peek)**. target 이 (눈에 띄게) 더 차 있으면 job 을 하나 이상 **훔쳐 온다.**

자연스러운 긴장이 있다:

- 너무 **자주** 엿보면 → 오버헤드가 커지고 확장성이 떨어진다 (MQMS 를 쓴 이유 자체가 사라짐).
- 너무 **드물게** 엿보면 → 심한 부하 불균형.

적당한 임계값 찾기는 시스템 정책 설계에서 흔히 그렇듯 **흑마법**이다 (Ch.08 의 voo-doo 상수와 같은 종류).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C10-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">SQMS: 큐 하나 + 락 하나  vs  MQMS: CPU 마다 큐 + work stealing</text>
  <text x="20" y="45" fill="currentColor" font-weight="bold">SQMS</text>
  <rect x="20" y="60" width="270" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="30" y="82" fill="currentColor">전역 큐: A B C D E</text>
  <rect x="245" y="64" width="38" height="26" fill="none" stroke="#d9534f" stroke-width="2"/>
  <text x="264" y="82" fill="#d9534f" text-anchor="middle" font-size="12">lock</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="170" width="56" height="34"/><rect x="91" y="170" width="56" height="34"/>
    <rect x="162" y="170" width="56" height="34"/><rect x="233" y="170" width="56" height="34"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="48" y="192">CPU0</text><text x="119" y="192">CPU1</text><text x="190" y="192">CPU2</text><text x="261" y="192">CPU3</text>
  </g>
  <g stroke="#d9534f" stroke-width="1.5">
    <line x1="48" y1="168" x2="255" y2="96" marker-end="url(#C10-arrow2)"/>
    <line x1="119" y1="168" x2="260" y2="96" marker-end="url(#C10-arrow2)"/>
    <line x1="190" y1="168" x2="265" y2="96" marker-end="url(#C10-arrow2)"/>
    <line x1="261" y1="168" x2="270" y2="96" marker-end="url(#C10-arrow2)"/>
  </g>
  <text x="20" y="230" fill="currentColor" font-size="12">모든 CPU 가 같은 락을 두고 경쟁 → 확장성 ↓</text>
  <text x="20" y="248" fill="currentColor" font-size="12">job 이 CPU 사이를 튐 → 캐시 친화성 ↓</text>
  <text x="20" y="266" style="fill:var(--accent)" font-size="12">대신 부하 균형은 자동</text>
  <line x1="330" y1="35" x2="330" y2="275" stroke="currentColor" stroke-dasharray="3 4" opacity="0.5"/>
  <text x="350" y="45" fill="currentColor" font-weight="bold">MQMS</text>
  <rect x="350" y="60" width="140" height="34" fill="none" stroke="currentColor"/>
  <text x="360" y="82" fill="currentColor">Q0: (비었음)</text>
  <rect x="510" y="60" width="150" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="520" y="82" fill="currentColor">Q1: B D</text>
  <rect x="380" y="170" width="80" height="34" fill="none" stroke="currentColor"/>
  <text x="420" y="192" fill="currentColor" text-anchor="middle" font-size="12">CPU0 (idle)</text>
  <rect x="545" y="170" width="80" height="34" fill="none" stroke="currentColor"/>
  <text x="585" y="192" fill="currentColor" text-anchor="middle" font-size="12">CPU1</text>
  <line x1="420" y1="168" x2="420" y2="96" stroke="currentColor" marker-end="url(#C10-arrow2)"/>
  <line x1="585" y1="168" x2="585" y2="96" stroke="currentColor" marker-end="url(#C10-arrow2)"/>
  <path d="M 590 58 C 580 20, 450 20, 440 58" fill="none" style="stroke:var(--accent)" stroke-width="2.5" marker-end="url(#C10-arrow2)"/>
  <text x="455" y="30" style="fill:var(--accent)" font-size="12" font-weight="bold">peek → D 를 훔쳐 옴 (steal)</text>
  <text x="350" y="230" fill="currentColor" font-size="12">큐마다 락 → 경쟁 ↓, job 이 제자리 → 친화성 ↑</text>
  <text x="350" y="248" fill="currentColor" font-size="12">대신 부하 불균형 → 이주 필요</text>
  <text x="350" y="266" fill="currentColor" font-size="12">peek 주기: 잦으면 오버헤드, 드물면 불균형</text>
</svg>
```

## 7. Linux 멀티프로세서 스케줄러 (10.6)

Linux 커뮤니티에서도 정답은 하나로 모이지 않았다. 세 스케줄러가 나왔다 (Meehean 의 학위논문 [M11] 이 장단점을 잘 정리):

- **O(1) 스케줄러**: 다중 큐. 우선순위 기반(MLFQ 와 비슷)으로 시간에 따라 우선순위를 바꾸고, 대화형 성능에 초점.
- **CFS (Completely Fair Scheduler)**: 다중 큐. 결정론적 **비례 배분**(stride 와 비슷, [Ch.09](2026-09-30_C09_proportional_share.md) 보충 참고).
- **BFS**: 셋 중 유일한 **단일 큐**. 역시 비례 배분인데 더 복잡한 **EEVDF**(Earliest Eligible Virtual Deadline First) 기반.

→ 단일 큐, 다중 큐 **둘 다 성공할 수 있다**는 증거. (보충: 현재 Linux 는 CPU 별 runqueue + **sched domain** 계층(SMT → 코어 → 소켓/NUMA)을 따라 올라가며 주기적 load balancing 과 idle 시 balancing(일종의 work stealing)을 한다. 그리고 6.6 부터는 CFS 의 선택 로직이 바로 그 **EEVDF** 로 바뀌었다.)

## 8. 요약 (10.7)

- **SQMS**: 만들기 쉽고 부하 균형이 좋지만, 많은 CPU 로의 확장과 캐시 친화성이 본질적으로 어렵다.
- **MQMS**: 확장성과 캐시 친화성이 좋지만, 부하 불균형을 다뤄야 하고 더 복잡하다.
- 어느 쪽이든 쉬운 답은 없다. 작은 코드 변경이 큰 동작 차이를 낳는다. "뭘 하는지 정확히 알거나, 적어도 큰돈을 받을 때만" 범용 스케줄러를 만들라는 게 원문의 농담 섞인 결론.

## 9. 직접 해보기

### 9.1 OSTEP 시뮬레이터 `multi.py`

모델: 각 job 은 **실행 시간**과 **작업 집합(working set) 크기**를 가진다. CPU 캐시 크기(`-M`, 기본 100) 안에 작업 집합이 들어가고 그 CPU 에서 `-w`(기본 10) 틱 이상 돌면 캐시가 **warm** 이 되어 `-r`(기본 2)배 빨리 돈다. 기본은 전역 큐 하나(SQMS), `-p` 면 CPU 별 큐(MQMS) + `-P`(기본 30) 틱마다 다른 큐를 엿보고 훔치기.

(v0.91 원문에는 이 장의 숙제가 없지만 시뮬레이터는 같이 배포된다. 아래는 이후 판 숙제 흐름을 따라 돌린 것.)

**(a) CPU 1개, 캐시 효과.**

```text
cd .tools/ostep-homework/cpu-sched-multi
python3 ./multi.py -n 1 -L a:30:200 -c | grep Finished
Finished time 30
python3 ./multi.py -n 1 -L a:30:200 -M 300 -c | grep Finished
Finished time 20
```

계산: 작업 집합 200 > 캐시 100 이면 계속 cold → 1틱에 1 진행 → **30**. 캐시 300 이면 처음 10틱 cold(10 진행) 후 warm 으로 남은 20 을 2배속 → 10틱 → **20**.

**(b) CPU 2개, job 셋(a: ws 100, b·c: ws 50), 전역 큐 하나.** `-t -C` 로 누가 어디서 돌고 캐시가 warm(w)인지 본다.

```text
python3 ./multi.py -n 2 -L a:100:100,b:100:50,c:100:50 -c -t -C
```

```text
Scheduler central queue: ['a', 'b', 'c']

   0   a cache[   ]     b cache[   ]     
   1   a cache[   ]     b cache[   ]     
   2   a cache[   ]     b cache[   ]     
   3   a cache[   ]     b cache[   ]     
   4   a cache[   ]     b cache[   ]     
   5   a cache[   ]     b cache[   ]     
   6   a cache[   ]     b cache[   ]     
   7   a cache[   ]     b cache[   ]     
   8   a cache[   ]     b cache[   ]     
   9   a cache[w  ]     b cache[ w ]     
---------------------------------------
  10   c cache[w  ]     a cache[ w ]     
  11   c cache[w  ]     a cache[ w ]     
  12   c cache[w  ]     a cache[ w ]     
  13   c cache[w  ]     a cache[ w ]     
  14   c cache[w  ]     a cache[ w ]     
  15   c cache[w  ]     a cache[ w ]     
  16   c cache[w  ]     a cache[ w ]     
  17   c cache[w  ]     a cache[ w ]     
  18   c cache[w  ]     a cache[ w ]     
  19   c cache[  w]     a cache[w  ]     
---------------------------------------
  20   b cache[  w]     c cache[w  ]     
  21   b cache[  w]     c cache[w  ]     
  22   b cache[  w]     c cache[w  ]     
  23   b cache[  w]     c cache[w  ]     
  24   b cache[  w]     c cache[w  ]     
  25   b cache[  w]     c cache[w  ]     
  26   b cache[  w]     c cache[w  ]     
  27   b cache[  w]     c cache[w  ]     
  28   b cache[  w]     c cache[w  ]     
  29   b cache[ ww]     c cache[  w]     
---------------------------------------
  30   a cache[ ww]     b cache[  w]     
  31   a cache[ ww]     b cache[  w]     
  32   a cache[ ww]     b cache[  w]     
...
Finished time 150

Per-CPU stats
  CPU 0  utilization 100.00 [ warm 0.00 ]
  CPU 1  utilization 100.00 [ warm 0.00 ]
```

`cache[abc]` 칸은 a, b, c 순서로 "이 CPU 캐시에 warm 인가"를 뜻한다. a 는 CPU 0 → 1 → 0 … 으로 매 slice 마다 튀고, warm 이 되자마자(10틱) 다른 CPU 로 가 버린다. 그래서 **warm 상태로 실행한 시간 0%**, 총 300 단위 일을 CPU 2개로 나눠 정확히 **150틱**. 원문 5절의 "job 이 CPU 사이를 튄다"가 그대로 보인다.

**(c) 같은 워크로드에 친화성 고정(`-A`)과 CPU 별 큐(`-p`).**

```text
$ python3 ./multi.py -n 2 -L a:100:100,b:100:50,c:100:50 -A a:0,b:1,c:1 -c | grep -E 'Finished|CPU [01] '
Finished time 110
  CPU 0  utilization 50.00 [ warm 40.91 ]
  CPU 1  utilization 100.00 [ warm 81.82 ]

$ python3 ./multi.py -n 2 -L a:100:100,b:100:50,c:100:50 -p -c | grep -E 'Finished|CPU [01] '
Scheduler CPU 0 queue: ['a', 'c']
Scheduler CPU 1 queue: ['b']
Finished time 100
  CPU 0  utilization 95.00 [ warm 35.00 ]
  CPU 1  utilization 95.00 [ warm 75.00 ]
```

`-A` 계산으로 검증: CPU 0 은 a 혼자 — 작업 집합 100 = 캐시 100 이라 들어간다 → 10틱 cold + 90 을 2배속 45틱 = **55틱**에 끝, 이후 놀아서 이용률 50%. CPU 1 은 b, c(50+50=100, 둘 다 캐시에 들어감) — 각자 10틱 cold(20틱) 후 남은 90씩을 2배속으로 45+45 = 90틱 → **110틱**. 친화성만으로 150 → 110.

`-p` (CPU 별 큐, 기본 peek 30): 처음 배치는 `CPU 0: [a, c]`, `CPU 1: [b]`. CPU 0 은 a+c = 150 > 100 이라 캐시가 안 맞지만, CPU 1 이 b 를 끝내기 전후로 엿보고 훔쳐 와 균형이 잡힌다 → **100틱**.

**(d) work stealing 주기(`-P`)의 효과.**

```text
$ python3 ./multi.py -n 2 -L a:100:100,b:100:50,c:100:50 -p -P 0 -c | grep Finished
Finished time 200
$ python3 ./multi.py -n 2 -L a:100:100,b:100:50,c:100:50 -p -P 30 -c | grep Finished
Finished time 100
$ python3 ./multi.py -n 2 -L a:100:100,b:100:50,c:100:50 -p -P 5 -c | grep Finished
Finished time 90
$ python3 ./multi.py -n 2 -L a:200:50,b:50:50,c:200:50,d:50:50 -p -P 0 -c | grep Finished
Finished time 210
$ python3 ./multi.py -n 2 -L a:200:50,b:50:50,c:200:50,d:50:50 -p -P 30 -c | grep Finished
Finished time 140
$ python3 ./multi.py -n 2 -L a:200:50,b:50:50,c:200:50,d:50:50 -p -P 5 -c | grep Finished
Finished time 140
```

뒤 세 줄은 불균형 배치다: 큐 배정이 라운드 로빈이라 CPU 0 에 긴 job 둘(a, c: 200), CPU 1 에 짧은 job 둘(b, d: 50)이 간다.

- `-P 0`(훔치기 끔) = 순수 MQMS. 첫 워크로드에서 CPU 0 은 a, c 를 캐시 안 맞는 채로 200틱 돌고, CPU 1 은 b 를 55틱에 끝내고 논다 → **200**. 부하 불균형의 전형.
- 엿보기를 켜면 100, 더 자주(5) 엿보면 90. 두 번째 워크로드에선 30 이나 5 나 140 — 일정 수준 이상 자주 엿봐도 이득이 없고 (실제 시스템에선) 오버헤드만 는다. "적당한 임계값은 흑마법"의 실례.
- 두 번째 워크로드의 이론 하한: 총 일 500 을 CPU 2개가 250씩 완벽히 나누고, 각 CPU 가 job 두 개(작업 집합 50+50 = 100, 캐시에 들어감)를 각각 10틱씩 cold 로 데운 뒤(20틱에 20 진행) 나머지 230 을 2배속(115틱)으로 처리하면 약 **135틱**. 140 은 하한에 거의 붙은 좋은 균형이다.

### 9.2 C 로 재 보는 일관성 비용과 SQMS vs MQMS: `code/C10_coherence_queues.c`

- **Part 1**: 두 스레드가 같은 128B 캐시 라인 안의 서로 다른 `long` 을 atomic 증가 (false sharing) vs 128B 떨어뜨려 놓기.
- **Part 2**: "스케줄러 결정 1회 = 락 잡고 job 하나 꺼냈다가 뒤에 넣기". 모든 스레드가 큐 하나(SQMS) vs 스레드마다 자기 큐(MQMS)일 때 처리량.

macOS 는 스레드를 특정 코어에 고정하는 API 가 없어서(affinity 는 힌트) 배치는 OS 에 맡겼다. M2 의 캐시 라인은 `sysctl hw.cachelinesize` = **128** 바이트.

```c
/* C10_coherence_queues.c — 멀티프로세서 스케줄링의 두 가지 비용을 macOS 에서 직접 잰다.
 *  Part 1: 캐시 일관성(coherence) 비용 — 두 스레드가 "같은 캐시 라인" 의 서로 다른 변수를
 *          증가시킬 때(false sharing) vs 128B 떨어뜨렸을 때. (Apple M2 cache line = 128B)
 *  Part 2: SQMS vs MQMS — 모든 스레드가 락 하나로 보호되는 큐 하나를 쓸 때 vs
 *          스레드마다 자기 큐(+자기 락)를 쓸 때의 pop/push 처리량.
 * macOS 는 스레드를 특정 코어에 고정하는 API 가 없으므로(affinity 는 힌트뿐) 배치는 OS 에 맡긴다. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---------------- Part 1: false sharing ---------------- */
#define ITERS 20000000L
struct same_line { long a; long b; };                                         /* 16B 안에 둘 다 */
struct padded    { long a; char pad[120]; long b; } __attribute__((aligned(128)));

static struct same_line S __attribute__((aligned(128)));
static struct padded    P;

static void *inc_long(void *arg) {
    long *p = arg;   /* atomic add: 매번 그 캐시 라인을 '독점(M 상태)' 으로 가져와야 한다 */
    for (long i = 0; i < ITERS; i++) __atomic_fetch_add(p, 1, __ATOMIC_RELAXED);
    return NULL;
}

static double run_pair(long *x, long *y) {
    pthread_t t1, t2;
    double t0 = now_sec();
    pthread_create(&t1, NULL, inc_long, x);
    pthread_create(&t2, NULL, inc_long, y);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return now_sec() - t0;
}

/* ---------------- Part 2: one shared queue vs per-thread queues ---------------- */
#define QCAP 1024
#define OPS_PER_THREAD 2000000L
typedef struct __attribute__((aligned(128))) {   /* 큐끼리 false sharing 방지 */
    pthread_mutex_t lock;
    int buf[QCAP];
    int head, tail, count;
} queue_t;

static void q_init(queue_t *q, int njobs) {
    pthread_mutex_init(&q->lock, NULL);
    q->head = q->tail = q->count = 0;
    for (int i = 0; i < njobs; i++) { q->buf[q->tail] = i; q->tail = (q->tail + 1) % QCAP; q->count++; }
}

/* "스케줄러 한 번" = 큐에서 job 하나 꺼내고(pick next) 다시 뒤에 넣기(time slice 끝) */
static void sched_once(queue_t *q) {
    pthread_mutex_lock(&q->lock);
    int job = q->buf[q->head]; q->head = (q->head + 1) % QCAP;
    q->buf[q->tail] = job;     q->tail = (q->tail + 1) % QCAP;
    pthread_mutex_unlock(&q->lock);
}

typedef struct { queue_t *q; } warg_t;
static void *worker(void *arg) {
    queue_t *q = ((warg_t *)arg)->q;
    for (long i = 0; i < OPS_PER_THREAD; i++) sched_once(q);
    return NULL;
}

static double run_queues(int nthreads, int per_cpu) {
    queue_t *qs = aligned_alloc(128, sizeof(queue_t) * (size_t)nthreads);  /* size 는 128 의 배수여야 함 */
    if (!qs) { perror("aligned_alloc"); exit(1); }
    pthread_t th[16];
    warg_t args[16];
    for (int i = 0; i < nthreads; i++) q_init(&qs[i], 8);
    double t0 = now_sec();
    for (int i = 0; i < nthreads; i++) {
        args[i].q = per_cpu ? &qs[i] : &qs[0];
        pthread_create(&th[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double dt = now_sec() - t0;
    free(qs);
    return (double)nthreads * OPS_PER_THREAD / dt / 1e6;      /* Mops/s */
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("== Part 1: cache-line ping-pong (2 threads x 20M atomic increments) ==");
    printf("  sizeof(same_line)=%zu, offset of b in padded=%zu bytes\n",
           sizeof(struct same_line), (size_t)((char *)&P.b - (char *)&P.a));
    for (int r = 0; r < 2; r++) {
        double t_same = run_pair(&S.a, &S.b);
        double t_pad  = run_pair(&P.a, &P.b);
        printf("  run %d: same line %.3f s   padded %.3f s   (x%.1f slower when sharing)\n",
               r + 1, t_same, t_pad, t_same / t_pad);
    }

    puts("\n== Part 2: scheduler ops/s — single shared queue (SQMS) vs per-thread queues (MQMS) ==");
    puts("  threads   SQMS Mops/s   MQMS Mops/s");
    int ns[] = { 1, 2, 4, 8 };
    for (int k = 0; k < 4; k++) {
        double sq = run_queues(ns[k], 0);
        double mq = run_queues(ns[k], 1);
        printf("  %7d   %11.1f   %11.1f\n", ns[k], sq, mq);
    }
    return 0;
}
```

```text
cc -Wall -Wextra -O0 -pthread code/C10_coherence_queues.c -o .work/bin/C10_coherence_queues && .work/bin/C10_coherence_queues
```

실제 출력 (Apple M2, 경고 0개):

```text
== Part 1: cache-line ping-pong (2 threads x 20M atomic increments) ==
  sizeof(same_line)=16, offset of b in padded=128 bytes
  run 1: same line 0.313 s   padded 0.049 s   (x6.3 slower when sharing)
  run 2: same line 0.292 s   padded 0.046 s   (x6.3 slower when sharing)

== Part 2: scheduler ops/s — single shared queue (SQMS) vs per-thread queues (MQMS) ==
  threads   SQMS Mops/s   MQMS Mops/s
        1         130.1         127.5
        2          72.5         263.0
        4          34.7         437.8
        8          30.4         611.0
```

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">실측: 스케줄러 큐 연산 처리량 (Mops/s, Apple M2) — SQMS vs MQMS</text>
  <line x1="70" y1="240" x2="620" y2="240" stroke="currentColor"/>
  <line x1="70" y1="40" x2="70" y2="240" stroke="currentColor"/>
  <g fill="currentColor" font-size="11" text-anchor="end">
    <text x="64" y="244">0</text><text x="64" y="185">200</text><text x="64" y="126">400</text><text x="64" y="67">600</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 4" opacity="0.35">
    <line x1="70" y1="181" x2="620" y2="181"/><line x1="70" y1="122" x2="620" y2="122"/><line x1="70" y1="63" x2="620" y2="63"/>
  </g>
  <g stroke="currentColor">
    <rect x="110" y="201.7" width="40" height="38.3" fill="none"/>
    <rect x="230" y="218.6" width="40" height="21.4" fill="none"/>
    <rect x="350" y="229.8" width="40" height="10.2" fill="none"/>
    <rect x="470" y="231" width="40" height="9" fill="none"/>
  </g>
  <g style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="1.5">
    <rect x="154" y="202.4" width="40" height="37.6"/>
    <rect x="274" y="162.5" width="40" height="77.5"/>
    <rect x="394" y="111" width="40" height="129"/>
    <rect x="514" y="60" width="40" height="180"/>
  </g>
  <g fill="currentColor" font-size="11" text-anchor="middle">
    <text x="130" y="196">130</text><text x="174" y="196">128</text>
    <text x="250" y="213">73</text><text x="294" y="157">263</text>
    <text x="370" y="224">35</text><text x="414" y="105">438</text>
    <text x="490" y="226">30</text><text x="534" y="54">611</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="152" y="258">1 스레드</text><text x="272" y="258">2</text><text x="392" y="258">4</text><text x="512" y="258">8</text>
  </g>
  <rect x="380" y="276" width="14" height="10" fill="none" stroke="currentColor"/>
  <text x="398" y="285" fill="currentColor" font-size="12">SQMS (락 하나)</text>
  <rect x="500" y="276" width="14" height="10" style="fill:var(--accent-soft);stroke:var(--accent)"/>
  <text x="518" y="285" fill="currentColor" font-size="12">MQMS (큐마다 락)</text>
  <text x="80" y="285" fill="currentColor" font-size="12">스레드가 늘수록 SQMS 는 오히려 느려진다</text>
</svg>
```

읽는 법:
- **Part 1**: 서로 다른 변수인데도 같은 캐시 라인에 있으면 **약 6배** 느리다. 매 atomic 증가마다 라인을 자기 코어로 독점(Modified) 상태로 가져와야 하고, 상대 코어의 사본은 무효화된다 — 일관성 프로토콜이 **정확성은 지키지만 비용을 청구**하는 모습. 128B 떨어뜨리면 각자 자기 라인에서 일한다.
- **Part 2**: 1스레드에선 둘이 같다(경쟁 없음). 스레드가 늘면 SQMS 는 **절대 처리량이 줄어든다**(130 → 30): 락 획득·해제마다 락 변수와 큐 헤드가 든 캐시 라인이 코어 사이를 핑퐁하고, macOS mutex 는 경쟁 시 커널로 들어가 잠들고 깨는 비용까지 붙는다. MQMS 는 거의 선형으로 늘어난다(8스레드에서 E코어가 섞여 기울기가 줄어듦). 8스레드에서 **약 20배** 차이 — 원문이 "언젠가 넣고 싶다"던 실측.
- 주의: 측정값은 실행마다 ±수십% 흔들린다(첫 실행에선 1스레드 SQMS 가 72 Mops/s 로 나오기도 했다 — P/E 코어 중 어디에 배치되느냐에 따라). 경향(SQMS 는 감소, MQMS 는 증가)은 매번 같았다.

## 10. 펌웨어 엔지니어의 눈으로

- **NVMe 는 처음부터 MQMS 로 설계됐다.** AHCI 는 커맨드 큐 하나(32개)에 락이 필요했지만, NVMe 는 **CPU 코어마다 SQ/CQ 쌍**을 두고 각 CQ 의 MSI-X 인터럽트를 그 코어로 보낸다. Linux blk-mq 도 CPU 별 software queue → hardware queue 매핑. 이게 정확히 "큐마다 락 없이, 캐시 친화성 유지"라는 MQMS 의 장점을 I/O 경로에 적용한 것. SSD 컨트롤러 FW 쪽에서도 멀티코어 컨트롤러(예: 호스트 I/F 코어, FTL 코어, NAND 백엔드 코어)는 코어 간 **lock-free 링 버퍼/메일박스**로 일을 넘기지, 공유 큐 하나에 스핀락을 거는 구조는 피한다.
- **인터럽트 친화성 = 캐시 친화성.** I/O 완료 인터럽트를 요청을 낸 코어로 보내지 않으면 완료 처리 시 요청 구조체가 다른 코어 캐시에 있어 cross-core 캐시 미스가 난다. `irq affinity`, NVMe 의 큐-코어 매핑, RSS(네트워크)가 모두 같은 목적. Apple SoC 처럼 코프로세서(ANS 스토리지 컨트롤러 등)와 AP 가 메일박스로 통신하는 구조에서도, 어느 코어가 메일박스 인터럽트를 받는지가 지연시간을 좌우한다.
- **false sharing 은 펌웨어 공유 메모리에서 가장 흔한 성능 버그.** 멀티코어 FW 에서 코어별 통계 카운터나 링 버퍼의 head/tail 을 한 구조체에 붙여 두면 정확히 Part 1 상황이 된다. 해법도 같다: 생산자 쓰기 필드와 소비자 쓰기 필드를 **캐시 라인 경계로 분리**. 더 나아가 캐시 일관성이 없는 코어(DMA 엔진, 일부 코프로세서)와 공유할 때는 명시적 **cache clean/invalidate** 가 필요하다 — 이 장의 "하드웨어가 일관성을 맞춰 준다"는 전제가 깨지는 지점이라 면접 단골.
- **AI 가속기에서 work stealing 과 부하 불균형.** 여러 NPU 코어/텐서 엔진에 타일을 정적으로 나눠 주면(MQMS + 초기 배치) 타일 비용이 고르지 않을 때(sparse, 가변 길이 시퀀스) 꼬리 지연이 생긴다. GPU 의 persistent kernel + atomic 작업 카운터, 동적 배치 스케줄러는 work stealing 의 하드웨어/런타임판이다. 반대로 엔진별 로컬 SRAM(스크래치패드)에 가중치를 올려 두는 건 강한 "캐시 친화성" — 옮기면 재적재(DMA) 비용이 크다.
- **NUMA/칩렛.** 큰 서버·가속기 보드에선 "캐시"가 아니라 "메모리 자체"가 가까운/먼 곳으로 나뉜다. Linux sched domain 이 SMT → 코어 → NUMA 노드 순으로 이주 비용을 다르게 보는 것처럼, 멀티 다이 가속기 런타임도 다이 간 이동을 최후 수단으로 둔다.

## 11. 면접 질문

### Q1. 캐시 일관성을 하드웨어가 보장하는데 왜 락이 필요한가?
<details>
<summary>답 보기</summary>

- 일관성은 **단일 주소의 값**에 대해 "모든 코어가 결국 같은 최신 값을 본다"를 보장할 뿐, **여러 메모리 연산의 원자성**은 보장하지 않는다.
- 리스트 pop(`tmp = head; head = head->next; free(tmp)`)처럼 읽기-수정-쓰기 여러 단계가 끼어들면 두 CPU 가 같은 head 를 pop → **같은 값 두 번 반환 + double free**.
- 그래서 **락이나 atomic(CAS)** 으로 임계구역을 만든다. 또 일관성과 별개로 **메모리 순서(consistency/ordering)** 문제도 있어 barrier 가 필요할 수 있다(ARM 같은 약한 메모리 모델).

</details>

### Q2. SQMS 와 MQMS 의 장단점을 비교하라. 실제 OS 는 어느 쪽인가?
<details>
<summary>답 보기</summary>

- **SQMS**: 단순, 부하 균형 자동. 하지만 전역 락 경쟁으로 **확장성 나쁨**, job 이 CPU 사이를 튀어 **친화성 나쁨**.
- **MQMS**: 큐마다 락 → **확장성 좋음**, 제자리 실행 → **친화성 좋음**. 하지만 **부하 불균형** → migration/work stealing 필요, 엿보기 빈도 튜닝이 어렵다.
- 실제: Linux O(1)/CFS/EEVDF, FreeBSD ULE, Windows 모두 **CPU 별 runqueue + 주기적/유휴 시 load balancing**. BFS 처럼 단일 큐로 데스크톱 반응성을 노린 예외도 있다.
- 실측(C 실험): 8스레드에서 단일 락 큐가 CPU 별 큐보다 약 20배 느렸다.

</details>

### Q3. work stealing 은 어떻게 동작하고, 무엇을 튜닝해야 하나?
<details>
<summary>답 보기</summary>

- 일이 적은(혹은 비어 있는) 큐가 가끔 다른 큐를 **엿보고**, 눈에 띄게 많으면 하나 이상 **가져온다**. Cilk 런타임은 덱(deque)을 써서 주인은 한쪽 끝, 도둑은 반대쪽 끝에서 가져가 경쟁을 줄인다.
- 튜닝: **엿보는 빈도**(잦으면 원격 큐 락/캐시 미스 비용 → MQMS 의 확장성 이점 상실, 드물면 불균형), **불균형 임계값**(얼마나 차이 나야 훔칠지), **무엇을 훔칠지**(캐시가 식은 job, 오래 기다린 job 우선 — 친화성 손실 최소화).
- 시뮬레이터: 훔치기 끔 200틱 → 30틱마다 100틱 → 5틱마다 90틱, 다른 워크로드에선 30 과 5 가 같은 140.

</details>

### Q4. false sharing 이 무엇이고 어떻게 찾고 고치나?
<details>
<summary>답 보기</summary>

- 서로 다른 스레드가 **서로 다른 변수**를 쓰는데 그 변수들이 **같은 캐시 라인**에 있어서, 일관성 프로토콜이 라인을 계속 무효화/이동시키는 현상.
- 증상: 스레드를 늘려도 확장이 안 되고, 성능 카운터에서 HITM(다른 코어의 Modified 라인 히트)/coherence miss 가 많음. Linux `perf c2c` 가 정확히 이걸 찾는 도구.
- 해결: 쓰기 빈번한 per-thread/per-CPU 데이터를 **캐시 라인 크기로 정렬·패딩**(x86 64B, Apple M 128B), per-CPU 변수로 분리 후 읽을 때 합산.
- 실측: M2 에서 같은 라인 atomic 증가가 약 6배 느렸다.

</details>

### Q5. 캐시 친화성과 부하 균형이 충돌할 때 스케줄러는 어떻게 판단하나?
<details>
<summary>답 보기</summary>

- 옮기는 비용 = 캐시/TLB 재적재(working set 크기 비례) + 원격 큐 접근. 옮기는 이득 = 놀던 CPU 활용, 대기 시간 감소.
- 휴리스틱: **최근에 돈 job(cache-hot)은 옮기지 않는다**(Linux 의 `sched_migration_cost` 같은 임계), 큰 불균형일 때만 옮긴다, **가까운 CPU 부터**(같은 L2/LLC 공유 코어 → 같은 소켓 → 다른 NUMA 노드).
- idle CPU 는 즉시 훔치기(지연 최소화), busy CPU 간 균형은 주기적으로(오버헤드 최소화).
- 원문 예처럼 3 job / 2 CPU 면 한 번 이주로는 안 되고 **지속적 회전 이주**가 필요하다 — 이때는 이주 주기를 길게 잡아 친화성 손실을 상각.

</details>

## 12. 자가 점검 & 숙제

### 퀴즈 1. 캐시 크기 100, warmup 10틱, warm rate 2배. 작업 집합 80, 실행시간 50 인 job 을 CPU 하나에서 돌리면 몇 틱에 끝나나?
<details>
<summary>답 보기</summary>

80 ≤ 100 이라 캐시에 들어간다. 처음 10틱 cold(10 진행), 남은 40 을 2배속 → 20틱. 총 **30틱**.

</details>

### 퀴즈 2. 원문 SQMS 예(job 5개, CPU 4개)에서 친화성 장치가 없으면 job A 는 연속 두 slice 를 같은 CPU 에서 도는 경우가 있나?
<details>
<summary>답 보기</summary>

원문 표에서 A 는 CPU 0 → 1 → 2 → 3 → (CPU 0 에선 E D C B 다음 반복…) 순으로 매 slice 마다 다른 CPU 에서 돈다. 5 job 이 4 CPU 를 공유하면 큐 순서가 한 칸씩 밀려서 **연속으로 같은 CPU 를 잡는 일이 없다** — 친화성 최악.

</details>

### 퀴즈 3. MQMS 에서 Q0 = [A], Q1 = [B, D] 를 계속 유지하면 각 job 의 CPU 몫은? 이상적인 몫은?
<details>
<summary>답 보기</summary>

A 100%, B 50%, D 50%. 이상적으로는 2 CPU / 3 job = 각 **66.7%**. 그래서 원문처럼 B(또는 다른 job)를 주기적으로 CPU 0 으로 옮기는 **지속적 이주**가 필요하다.

</details>

### 퀴즈 4. 왜 SQMS 의 락 하나가 CPU 수가 늘수록 "더" 나빠지나? (1스레드 대비 8스레드에서 절대 처리량이 줄어든 이유)
<details>
<summary>답 보기</summary>

임계구역은 직렬로만 실행되므로 처리량 상한이 고정이다. 거기에 경쟁자가 늘면 락 변수와 큐 데이터가 든 캐시 라인이 매번 다른 코어로 이동(coherence miss)해 **임계구역 하나의 길이 자체가 늘어나고**, 실패한 획득 시도의 스핀/슬립·웨이크업 비용이 더해진다. 그래서 처리량이 평평한 게 아니라 **감소**한다(Anderson [A90] 이 분석한 spin lock 확장성 문제).

</details>

### 꼭 해볼 숙제 (시뮬레이터)

- **캐시 크기와 작업 집합**: `-n 1 -L a:30:200` 에 `-M` 을 100 → 300 으로 바꿔 30 → 20 이 되는지, `-w`, `-r` 을 바꿔 공식(cold 틱 + 남은 일/rate)을 확인 — 캐시 모델 이해.
- **친화성 고정 실험**: `-A a:0,b:1,c:1` 외에 `-A a:1,b:0,c:0` 등 다른 배치를 돌려 보고, 왜 a 를 혼자 두는 배치가 좋은지 작업 집합 합으로 설명 — 친화성 + 캐시 용량.
- **peek 주기**: `-p -P` 를 0, 1, 5, 30, 100 으로 바꾸며 Finished time 이 어떻게 변하는지, CPU 수 `-n` 을 4, 8 로 늘려 MQMS 가 어떻게 확장되는지 — work stealing 튜닝.

## 13. 다음으로

- 가상화 파트 CPU 편 정리 대화 → [원문 Ch.11 Summary Dialogue](../book-md/C11_summary_dialogue_on_cpu_virtualization.md)
- 메모리 가상화의 시작 → [Ch.13 주소 공간](2026-09-30_C13_address_spaces.md)
- 이 장의 전제 지식: 락과 확장성 → [Ch.28 Locks](2026-09-30_C28_locks.md), [Ch.29 동시 자료구조](2026-09-30_C29_concurrent_data_structures.md)
