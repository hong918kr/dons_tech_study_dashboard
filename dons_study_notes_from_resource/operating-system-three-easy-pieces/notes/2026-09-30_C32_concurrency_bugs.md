# Ch.32 흔한 동시성 버그 — 원자성 위반, 순서 위반, 그리고 교착

> 📖 원문: [32. Common Concurrency Problems](../book-md/C32_common_concurrency_problems.md) · [PDF p.379](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=379) · ⏱️ 읽기 약 45분 · 🔗 선행: [Ch.28](2026-09-30_C28_locks.md), [Ch.30](2026-09-30_C30_condition_variables.md), [Ch.31](2026-09-30_C31_semaphores.md)

## 0. 한눈에 보기

- 실제 오픈소스(MySQL, Apache, Mozilla, OpenOffice)의 동시성 버그 105개를 분석한 연구(Lu et al. [L+08])에 따르면 **약 70% 는 교착이 아닌 버그**, 30% 가 교착이다.
- 비교착 버그의 **97%** 는 두 종류: **원자성 위반(atomicity violation)** — "같이 실행돼야 할 코드가 쪼개짐" → 락으로 해결, **순서 위반(order violation)** — "A 다음 B 여야 하는데 순서 보장 없음" → CV/세마포어로 해결.
- 교착의 **4가지 필요조건**: 상호 배제, hold-and-wait, 비선점(no preemption), 순환 대기(circular wait). 하나라도 깨면 교착은 없다.
- 대응은 세 갈래: **예방(prevention)** — 조건 하나를 구조적으로 제거 (실전 1순위는 **락 순서 정하기**), **회피(avoidance)** — 전역 지식으로 스케줄링 (Banker's algorithm, 임베디드 정도에서만), **탐지·복구(detect & recover)** — 그래프에서 사이클 찾고 재시작 (DB).

> **CRUX: HOW TO HANDLE COMMON CONCURRENCY BUGS** — "Concurrency bugs tend to come in a variety of common patterns. Knowing which ones to look out for is the first step to writing more robust, correct concurrent code."
>
> → 동시성 버그는 몇 가지 흔한 패턴으로 나타난다. 무엇을 조심해야 하는지 아는 것이 더 견고하고 올바른 동시성 코드를 쓰는 첫걸음이다.

> **CRUX: HOW TO DEAL WITH DEADLOCK** — "How should we build systems to prevent, avoid, or at least detect and recover from deadlock? Is this a real problem in systems today?"
>
> → 교착을 예방하거나, 회피하거나, 최소한 탐지하고 복구하는 시스템을 어떻게 만들까? 이게 오늘날 시스템에서도 실제 문제인가?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 원자성 위반(atomicity violation) | 원자적이어야 할 코드 구간이 실행 중 쪼개짐 | `if (p) fputs(p)` 사이에 `p=NULL` |
| 순서 위반(order violation) | A→B 순서가 필요한데 강제되지 않음 | 초기화 전에 새 스레드가 포인터 사용 |
| 교착(deadlock) | 서로가 쥔 자원을 기다리며 아무도 진행 못함 | T1: L1→L2, T2: L2→L1 |
| 상호 배제(mutual exclusion) | 자원을 한 번에 한 스레드만 가짐 | 락 |
| hold-and-wait | 쥔 채로 다른 걸 기다림 | L1 쥐고 L2 대기 |
| 비선점(no preemption) | 쥔 자원을 강제로 뺏을 수 없음 | 락은 주인만 풂 |
| 순환 대기(circular wait) | 대기 관계가 원을 이룸 | T1→L2→T2→L1→T1 |
| 락 순서(lock ordering) | 모든 코드가 같은 순서로 락 획득 | 주소가 작은 락 먼저 |
| trylock | 못 잡으면 바로 실패 리턴 | `pthread_mutex_trylock` |
| livelock | 계속 움직이지만 진척이 없음 | 둘 다 양보만 반복 |
| lock-free / wait-free | 락 없이 원자 명령(CAS)으로 갱신 | CAS 루프로 리스트 삽입 |
| Banker's algorithm | 자원 요청을 "안전 상태"일 때만 허용 | Dijkstra 1964 |
| wait-for graph | 스레드 → (기다리는 자원의 주인) 간선 그래프 | 사이클 = 교착 |

## 2. 어떤 버그가 있나 (32.1)

Lu et al. 의 연구 [L+08] — 성숙한 코드베이스에서 실제로 발견되고 고쳐진 동시성 버그를 분류했다 (Fig 32.1):

| Application | 무엇 | 비교착 | 교착 | 합계 |
|---|---|---|---|---|
| MySQL | DB 서버 | 14 | 9 | 23 |
| Apache | 웹 서버 | 13 | 4 | 17 |
| Mozilla | 웹 브라우저 | 41 | 16 | 57 |
| OpenOffice | 오피스 | 6 | 2 | 8 |
| 합계 | | 74 | 31 | 105 |

계산: 비교착 74/105 = **70.5%**, 교착 31/105 = **29.5%**. "교착" 이 교과서에서 가장 유명하지만 실제로는 비교착 버그가 2배 이상 많다.

## 3. 비교착 버그 (32.2)

### 3.1 원자성 위반 (MySQL)

```c
Thread 1::
if (thd->proc_info) {
    ...
    fputs(thd->proc_info, ...);
    ...
}

Thread 2::
thd->proc_info = NULL;
```

T1 이 non-NULL 확인 **후**, `fputs` **전**에 인터럽트되고 T2 가 NULL 을 넣으면 T1 은 NULL 역참조로 죽는다. 정의(Lu et al.): *"The desired serializability among multiple memory accesses is violated (i.e. a code region is intended to be atomic, but the atomicity is not enforced during execution)."* — 코드가 "확인과 사용은 한 덩어리" 라고 **가정(atomicity assumption)** 했는데 그 가정이 강제되지 않았다.

```svg
<svg viewBox="0 0 700 220" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C32-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">원자성 위반: check 와 use 사이에 끼어든 쓰기</text>
  <text x="20" y="70" fill="currentColor" font-weight="bold">T1</text>
  <text x="20" y="150" fill="currentColor" font-weight="bold">T2</text>
  <line x1="60" y1="90" x2="680" y2="90" stroke="currentColor" marker-end="url(#C32-arrow)"/>
  <line x1="60" y1="170" x2="680" y2="170" stroke="currentColor" marker-end="url(#C32-arrow)"/>
  <text x="670" y="110" fill="currentColor" text-anchor="end">시간</text>
  <rect x="80" y="50" width="170" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="165" y="70" text-anchor="middle" fill="currentColor">if (proc_info != NULL)</text>
  <rect x="455" y="50" width="205" height="30" fill="none" stroke="#d9534f" stroke-width="2"/>
  <text x="557" y="70" text-anchor="middle" fill="currentColor">fputs(proc_info) → 크래시</text>
  <rect x="290" y="130" width="150" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="365" y="150" text-anchor="middle" fill="currentColor">proc_info = NULL</text>
  <rect x="78" y="40" width="586" height="46" fill="none" style="stroke:var(--accent)" stroke-dasharray="6,4"/>
  <text x="370" y="38" text-anchor="middle" style="fill:var(--accent)">프로그래머가 "원자적" 이라고 가정한 구간 (락 없음)</text>
  <line x1="365" y1="128" x2="365" y2="92" stroke="#d9534f" stroke-width="2" marker-end="url(#C32-arrow)"/>
  <text x="20" y="205" fill="currentColor">해결: 두 스레드 모두 proc_info 에 접근할 때 같은 proc_info_lock 을 잡는다 → 점선 구간이 진짜로 원자적이 됨</text>
</svg>
```

**해결**: 공유 변수 접근에 락을 두른다. 핵심은 **두 쪽 모두** (그리고 그 구조체에 접근하는 모든 코드가) 같은 락을 잡는 것.

```c
pthread_mutex_t proc_info_lock = PTHREAD_MUTEX_INITIALIZER;

Thread 1::
pthread_mutex_lock(&proc_info_lock);
if (thd->proc_info) {
    ...
    fputs(thd->proc_info, ...);
    ...
}
pthread_mutex_unlock(&proc_info_lock);

Thread 2::
pthread_mutex_lock(&proc_info_lock);
thd->proc_info = NULL;
pthread_mutex_unlock(&proc_info_lock);
```

### 3.2 순서 위반 (Mozilla)

```c
Thread 1::
void init() {
    ...
    mThread = PR_CreateThread(mMain, ...);
    ...
}

Thread 2::
void mMain(...) {
    ...
    mState = mThread->State;
    ...
}
```

T2(새 스레드)는 `mThread` 가 이미 초기화돼 있다고 가정한다. 하지만 새 스레드가 **생성 직후 바로 돌면** `PR_CreateThread` 의 리턴값이 `mThread` 에 대입되기 전이라 NULL 역참조로 죽는다 (초기값이 NULL 이 아니면 더 이상한 일이 생긴다). 정의: *"The desired order between two (groups of) memory accesses is flipped (i.e., A should always be executed before B, but the order is not enforced during execution)."*

**해결: 순서를 강제한다** — Ch.30 의 CV 패턴 그대로 (상태 변수 + 락 + CV):

```c
pthread_mutex_t mtLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  mtCond = PTHREAD_COND_INITIALIZER;
int mtInit = 0;

Thread 1::
void init() {
    ...
    mThread = PR_CreateThread(mMain, ...);
    // signal that the thread has been created...
    pthread_mutex_lock(&mtLock);
    mtInit = 1;
    pthread_cond_signal(&mtCond);
    pthread_mutex_unlock(&mtLock);
    ...
}

Thread 2::
void mMain(...) {
    ...
    // wait for the thread to be initialized...
    pthread_mutex_lock(&mtLock);
    while (mtInit == 0)
        pthread_cond_wait(&mtCond, &mtLock);
    pthread_mutex_unlock(&mtLock);
    mState = mThread->State;
    ...
}
```

`mThread` 자체를 상태 변수로 쓸 수도 있지만 단순하게 `mtInit` 를 따로 뒀다. 세마포어(초기값 0)로도 된다.

### 3.3 정리

- 비교착 버그의 **97%** 가 원자성 위반 또는 순서 위반. 자동 검사 도구도 이 두 패턴에 집중해야 한다.
- 실제로는 책의 예처럼 깔끔하게 안 고쳐지는 경우도 많다 (자료 구조 재설계가 필요한 경우 등).
- 구분 요령: **"이 두 접근이 쪼개지면 안 된다" → 원자성 → 락. "이게 저거보다 먼저여야 한다" → 순서 → CV/세마포어/join.**

## 4. 교착 버그 (32.3)

### 4.1 가장 단순한 교착

```text
Thread 1:        Thread 2:
  lock(L1);        lock(L2);
  lock(L2);        lock(L1);
```

**반드시** 교착이 나는 건 아니다. T1 이 L1 을 잡은 직후 문맥 교환 → T2 가 L2 를 잡고 L1 을 시도 → 둘 다 영원히 대기. 자원 할당 그래프(Fig 32.2)에 **사이클**이 생긴다.

```svg
<svg viewBox="0 0 700 260" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C32-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="175" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 32.2 교착 의존 그래프</text>
  <rect x="40" y="50" width="100" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="90" y="75" text-anchor="middle" fill="currentColor">Thread 1</text>
  <ellipse cx="260" cy="70" rx="50" ry="20" fill="none" stroke="currentColor"/>
  <text x="260" y="75" text-anchor="middle" fill="currentColor">Lock L1</text>
  <ellipse cx="90" cy="200" rx="50" ry="20" fill="none" stroke="currentColor"/>
  <text x="90" y="205" text-anchor="middle" fill="currentColor">Lock L2</text>
  <rect x="210" y="180" width="100" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="260" y="205" text-anchor="middle" fill="currentColor">Thread 2</text>
  <line x1="208" y1="70" x2="142" y2="70" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C32-arrow2)"/>
  <text x="175" y="62" text-anchor="middle" fill="currentColor">Holds</text>
  <line x1="90" y1="92" x2="90" y2="178" stroke="#d9534f" stroke-width="2" stroke-dasharray="6,4" marker-end="url(#C32-arrow2)"/>
  <text x="96" y="140" fill="currentColor">Wanted by T1</text>
  <line x1="142" y1="200" x2="208" y2="200" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C32-arrow2)"/>
  <text x="175" y="235" text-anchor="middle" fill="currentColor">Holds</text>
  <line x1="260" y1="178" x2="260" y2="92" stroke="#d9534f" stroke-width="2" stroke-dasharray="6,4" marker-end="url(#C32-arrow2)"/>
  <text x="255" y="140" text-anchor="end" fill="currentColor">Wanted by T2</text>
  <line x1="350" y1="30" x2="350" y2="250" stroke="currentColor" stroke-dasharray="3,4"/>
  <text x="525" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">4 조건 → 하나씩 깨는 예방책</text>
  <g fill="currentColor">
    <text x="365" y="60">① 상호 배제</text><text x="500" y="60">→ lock-free (CAS)</text>
    <text x="365" y="100">② hold-and-wait</text><text x="500" y="100">→ 전역 락으로 한꺼번에 획득</text>
    <text x="365" y="140">③ 비선점</text><text x="500" y="140">→ trylock 실패 시 내려놓기</text>
    <text x="365" y="180">④ 순환 대기</text><text x="500" y="180">→ 락 순서 (가장 실용적)</text>
  </g>
  <rect x="360" y="163" width="330" height="26" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="365" y="225" fill="currentColor">네 개가 모두 성립해야 교착. 하나만 깨도 충분.</text>
</svg>
```

### 4.2 왜 교착이 생기나

"락 순서만 맞추면 되잖아?" — 큰 시스템에선 어렵다.

1. **컴포넌트 간 복잡한 의존성**: VM 이 페이지를 읽으려면 파일 시스템을 부르고, 파일 시스템은 블록을 읽을 페이지가 필요해서 VM 을 부른다. 순환이 설계에서 자연스럽게 생긴다.
2. **캡슐화(encapsulation)**: 구현을 숨기라고 배웠지만 락과는 상극이다. Java `Vector.AddAll()`: `v1.AddAll(v2)` 는 내부적으로 v1, v2 의 락을 (예: v1 → v2 순으로) 잡는다. 다른 스레드가 동시에 `v2.AddAll(v1)` 을 부르면 **호출자는 전혀 모르는 채** 교착 가능 [J+08].

### 4.3 교착의 4가지 조건 [C+71]

- **상호 배제(mutual exclusion)**: 스레드가 자원을 독점한다 (락).
- **hold-and-wait**: 이미 받은 자원을 쥔 채 다른 자원을 기다린다.
- **비선점(no preemption)**: 쥔 자원을 강제로 빼앗을 수 없다.
- **순환 대기(circular wait)**: 각 스레드가 다음 스레드가 원하는 자원을 쥔 원형 사슬이 있다.

**하나라도 성립하지 않으면 교착은 불가능**하다. 그래서 예방 기법은 각각 조건 하나를 겨냥한다.

## 5. 예방(Prevention)

### 5.1 순환 대기 깨기 — 락 순서

가장 실용적이고 가장 많이 쓴다. 락이 L1, L2 둘이면 **항상 L1 먼저**. 이것만으로 순환이 생길 수 없다.

- **전체 순서(total ordering)**: 모든 락에 순위. 큰 시스템에선 비현실적일 수 있다.
- **부분 순서(partial ordering)**: 관련 락 그룹끼리만 순서. 리눅스 `mm/filemap.c` 맨 위 주석에 열 가지 순서 그룹이 적혀 있다: `i_mutex` before `i_mmap_mutex`, `i_mmap_mutex` before `private_lock` before `swap_lock` before `mapping->tree_lock` 등.
- 한계: 순서는 **관례**일 뿐이라 한 명이 어기면 끝. 코드 전체를 깊이 이해해야 한다.

> **TIP — ENFORCE LOCK ORDERING BY LOCK ADDRESS**: `do_something(mutex_t *m1, mutex_t *m2)` 를 누가 `(L1, L2)`, 누가 `(L2, L1)` 로 부를지 모른다면, **락의 주소**로 순서를 정한다. 높은 주소→낮은 주소든 반대든, 항상 같은 규칙이면 된다.

```c
if (m1 > m2) {   // grab locks in high-to-low address order
    pthread_mutex_lock(m1);
    pthread_mutex_lock(m2);
} else {
    pthread_mutex_lock(m2);
    pthread_mutex_lock(m1);
}
// Code assumes that m1 != m2 (it is not the same lock)
```

OSTEP 숙제 `vector-global-order.c` 가 정확히 이 방식이다 (같은 벡터면 한 번만 잠그는 특수 처리까지 포함).

### 5.2 hold-and-wait 깨기 — 한꺼번에 획득

```c
lock(prevention);
lock(L1);
lock(L2);
...
unlock(prevention);
```

전역 `prevention` 락을 먼저 잡으면, 락 획득 도중에 끼어들기가 없어 순서가 달라도 안전하다. 단, **모든** 락 획득 전에 prevention 을 잡아야 한다. 문제점: (1) 캡슐화와 상극 — 미리 어떤 락이 필요한지 다 알아야 함 (2) 필요한 시점보다 일찍 잡으니 **동시성이 떨어짐**.

### 5.3 비선점 깨기 — trylock

```c
top:
    lock(L1);
    if (trylock(L2) == -1) {   // 실패하면
        unlock(L1);            // 쥔 걸 스스로 내려놓고 (= 자발적 선점)
        goto top;              // 처음부터 다시
    }
```

다른 스레드가 반대 순서(L2→L1)로 같은 프로토콜을 써도 교착은 없다. 새 문제: **livelock** — 둘이 계속 서로 양보하며 실패만 반복 (교착은 아니지만 진척 없음). 해법: 재시도 전 **랜덤 지연(backoff)**. 또 다른 문제: 중간에 메모리 할당 등 다른 자원을 얻었다면 실패 시 그것도 정리해야 하고, 락이 하위 루틴에 묻혀 있으면 `goto top` 이 어렵다. Java Vector 같은 제한된 경우에만 깔끔하다.

### 5.4 상호 배제 깨기 — lock-free / wait-free

강력한 하드웨어 명령(compare-and-swap)으로 **락 없이** 자료 구조를 갱신한다 (Herlihy [H91]).

```c
int CompareAndSwap(int *address, int expected, int new) {
    if (*address == expected) { *address = new; return 1; }   // 성공
    return 0;                                                  // 실패
}
void AtomicIncrement(int *value, int amount) {
    do {
        int old = *value;
    } while (CompareAndSwap(value, old, old + amount) == 0);
}
```

리스트 머리에 삽입 — 락 버전과 CAS 버전:

```c
void insert(int value) {                 // 락 버전
    node_t *n = malloc(sizeof(node_t));
    assert(n != NULL);
    n->value = value;
    lock(listlock);                      // 임계 구역은 포인터 두 줄뿐
    n->next = head;
    head = n;
    unlock(listlock);
}
void insert(int value) {                 // CAS 버전
    node_t *n = malloc(sizeof(node_t));
    assert(n != NULL);
    n->value = value;
    do {
        n->next = head;
    } while (CompareAndSwap(&head, n->next, n) == 0);
}
```

(락을 malloc 뒤에서 잡는 이유: malloc 은 스레드 안전하고 시간이 걸리니 임계 구역을 최소화.) CAS 버전은 그 사이 다른 스레드가 head 를 바꿨으면 실패하고 새 head 로 재시도한다. 락이 없으니 교착은 없지만 **livelock 은 여전히 가능**하고, 삽입·삭제·조회를 모두 lock-free 로 만드는 건 매우 어렵다 (ABA 문제, 메모리 회수 등).

## 6. 회피(Avoidance) — 스케줄링으로

어떤 스레드가 어떤 락을 잡을지 **미리 알면**, 교착이 생길 조합을 동시에 안 돌리면 된다. CPU 2개, 스레드 4개:

```text
        T1    T2    T3    T4
L1     yes   yes    no    no
L2     yes   yes   yes    no
```

T1 과 T2 만 동시에 안 돌면 된다 (T3 는 락 하나뿐이라 누구와 겹쳐도 교착 불가):

```text
CPU 1:  T3  T4
CPU 2:  T1  T2
```

경합이 더 심한 경우:

```text
        T1    T2    T3    T4
L1     yes   yes   yes    no
L2     yes   yes   yes    no

CPU 1:  T4
CPU 2:  T1  T2  T3
```

T1, T2, T3 를 한 CPU 에 몰아야 하니 전체 시간이 크게 늘어난다. **교착이 무서워서 동시성을 포기**하는 보수적 방식이다.

**Banker's algorithm (새 예제로 계산)** — Dijkstra [D64]. 자원 한 종류 10개, 스레드 3개:

```text
       할당(Alloc)  최대(Max)  남은 필요(Need = Max - Alloc)
T1         3           7             4
T2         2           4             2
T3         2           9             7
가용(Available) = 10 - (3+2+2) = 3
```

**안전 상태 검사**: Need ≤ Available 인 스레드를 하나씩 "끝까지 돌렸다 치고" 자원을 회수해 본다.

```text
Available=3 : T2 (Need 2 ≤ 3) 완료 → Available = 3 + 2 = 5
Available=5 : T1 (Need 4 ≤ 5) 완료 → Available = 5 + 3 = 8
Available=8 : T3 (Need 7 ≤ 8) 완료 → Available = 8 + 2 = 10
안전 순서 <T2, T1, T3> 존재 → 현재 상태는 SAFE
```

이제 T1 이 2개를 더 요청하면? 가정 상태: T1 Alloc=5, Need=2, Available=1. Need ≤ 1 인 스레드가 **없다** (T1:2, T2:2, T3:7) → **UNSAFE** → 요청 거절(T1 대기). 반면 T2 가 1개 요청하면: T2 Alloc=3, Need=1, Available=2 → T2(1≤2) → 5 → T1(4≤5) → 8 → T3(7≤8) → SAFE → 허용.

문제: 모든 작업의 최대 요구량을 미리 알아야 하고, 동시성을 깎는다. 그래서 **전체 작업과 락을 다 아는 임베디드 시스템** 같은 제한된 환경에서만 쓸모 있다.

## 7. 탐지와 복구(Detect and Recover)

교착이 **가끔** 나는 걸 허용하고, 나면 수습한다. OS 가 1년에 한 번 멈추면 그냥 재부팅하는 것도 현실적인 "해법"이다.

> **TIP — DON'T ALWAYS DO IT PERFECTLY (TOM WEST'S LAW)**: "Not everything worth doing is worth doing well." 드물고 피해가 작은 문제에 큰 노력을 쏟지 마라. 단, 우주왕복선을 만든다면 이 조언은 무시하라.

많은 DB 가 이 방식이다: **교착 탐지기(deadlock detector)** 가 주기적으로 자원 그래프를 만들어 **사이클**을 찾고, 있으면 희생자(victim) 트랜잭션을 abort/재시작한다. 자료 구조 복구가 복잡하면 사람이 개입하기도 한다. (7.2 에서 바로 이 탐지기를 만들어 돌렸다.)

## 8. 요약 (32.4)

- 비교착 버그(원자성·순서 위반)가 더 흔하지만 대체로 고치기 쉽다.
- 교착: 실전 최선은 **락 획득 순서를 정해서 예방**하는 것. lock-free 자료 구조도 리눅스 등 실제 시스템에 들어오고 있지만 일반성과 개발 난이도가 한계.
- 어쩌면 최선은 **락이 필요 없는 프로그래밍 모델**: MapReduce 처럼 병렬 계산을 락 없이 기술하게 하는 것. "락은 본질적으로 문제가 많다. 꼭 필요할 때만 쓰자."

## 9. 직접 해보기

환경: Apple M2 (8코어), macOS 26.4.1, Apple clang 21. 내 코드는 모두 `cc -Wall -Wextra -O0 -pthread`, **경고 0개**.

### 9.1 비교착 버그 재현 (`code/C32_nondeadlock_bugs.c`)

진짜로 NULL 을 역참조하면 첫 번에 죽어서 통계를 못 내므로, "use" 시점에 포인터를 다시 읽어 NULL 이면 **"죽었을 횟수"** 로 센다. 순서 위반은 원래 창(create 리턴 → 대입)이 너무 짧으면 거의 안 보이므로, **init() 의 "..." 부분에 50µs 다른 작업이 끼어 있는 경우**도 같이 돌린다.

```c
// C32_nondeadlock_bugs.c — Ch.32.2 비교착 버그 2종을 "크래시 없이" 관찰 가능하게 재현한다.
//  (A) atomicity violation (MySQL proc_info 패턴):
//      T1: if (p != NULL) { ...; use(p); }      T2: p = NULL; ...; p = buf;
//      check 와 use 사이에 p 가 NULL 로 바뀌면 진짜 코드는 fputs(NULL) 로 죽는다.
//      여기선 use 직전에 p 를 다시 읽어 NULL 이면 "죽었을 횟수" 로 센다.
//  (B) order violation (Mozilla mThread 패턴):
//      init() 이 스레드를 만들고 나서 mThread 에 대입하는데, 새 스레드가 먼저 mThread->State 를 읽는다.
//      여기선 mThread 가 아직 NULL 인 걸 본 횟수를 센다. 고친 버전은 CV 로 순서를 강제.
//
// build: cc -Wall -Wextra -O0 -pthread code/C32_nondeadlock_bugs.c -o .work/bin/C32_nondeadlock_bugs
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// ---------------- (A) atomicity violation ----------------
#define A_LOOPS 2000000
static char info_buf[] = "running query";
static char *volatile proc_info = info_buf;          // 공유 포인터 (volatile: 컴파일러가 재읽기를 없애지 않도록)
static pthread_mutex_t proc_info_lock = PTHREAD_MUTEX_INITIALIZER;
static int use_lock_A = 0;
static long would_crash = 0, used_ok = 0;
static volatile int a_stop = 0;

static void *t1_reader(void *arg) {
    (void)arg;
    for (long i = 0; i < A_LOOPS; i++) {
        if (use_lock_A) pthread_mutex_lock(&proc_info_lock);
        if (proc_info != NULL) {                     // check
            __asm__ volatile("" ::: "memory");       // "..." (다른 일)
            char *p = proc_info;                     // use: 원래는 fputs(thd->proc_info, ...)
            if (p == NULL) would_crash++; else used_ok++;
        }
        if (use_lock_A) pthread_mutex_unlock(&proc_info_lock);
    }
    a_stop = 1;
    return NULL;
}
static void *t2_writer(void *arg) {
    (void)arg;
    while (!a_stop) {
        if (use_lock_A) pthread_mutex_lock(&proc_info_lock);
        proc_info = NULL;                            // thd->proc_info = NULL;
        proc_info = info_buf;                        // (다음 쿼리 시작)
        if (use_lock_A) pthread_mutex_unlock(&proc_info_lock);
    }
    return NULL;
}
static void run_A(int lock) {
    use_lock_A = lock; would_crash = used_ok = 0; a_stop = 0;
    pthread_t a, b;
    pthread_create(&b, NULL, t2_writer, NULL);
    pthread_create(&a, NULL, t1_reader, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("(A) atomicity %-9s: checks passed=%ld, NULL at use (would crash)=%ld\n",
           lock ? "LOCKED" : "unlocked", used_ok + would_crash, would_crash);
}

// ---------------- (B) order violation ----------------
typedef struct { int State; } thr_t;
static thr_t *volatile mThread = NULL;
static thr_t thr_storage = { 42 };
static pthread_mutex_t mtLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  mtCond = PTHREAD_COND_INITIALIZER;
static int mtInit = 0;
static int fix_B = 0;
static long saw_null = 0, saw_ok = 0;

static void *mMain(void *arg) {
    (void)arg;
    if (fix_B) {
        pthread_mutex_lock(&mtLock);
        while (mtInit == 0) pthread_cond_wait(&mtCond, &mtLock);
        pthread_mutex_unlock(&mtLock);
    }
    thr_t *t = mThread;                              // mState = mThread->State;
    if (t == NULL) saw_null++; else { saw_ok++; (void)t->State; }
    return NULL;
}
static void spin_us(int us) {                        // init() 의 "..." 부분 (다른 초기화 작업) 흉내
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    do { clock_gettime(CLOCK_MONOTONIC, &b); }
    while ((b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000 < us);
}
static void run_B(int fix, int trials, int widen_us) {
    fix_B = fix; saw_null = saw_ok = 0;
    for (int i = 0; i < trials; i++) {
        mThread = NULL; mtInit = 0;
        pthread_t th;
        pthread_create(&th, NULL, mMain, NULL);      // mThread = PR_CreateThread(mMain, ...);
        if (widen_us) spin_us(widen_us);             // create 와 대입 사이에 다른 일이 끼어 있다면?
        mThread = &thr_storage;                      // 대입은 create 가 "리턴한 뒤"에야 일어난다
        if (fix) {
            pthread_mutex_lock(&mtLock);
            mtInit = 1;
            pthread_cond_signal(&mtCond);
            pthread_mutex_unlock(&mtLock);
        }
        pthread_join(th, NULL);
    }
    printf("(B) order     %-9s: window +%2dus, %d trials, child saw mThread==NULL (would crash) %ld times\n",
           fix ? "CV-fixed" : "unfixed", widen_us, trials, saw_null);
}

int main(void) {
    run_A(0);
    run_A(1);
    run_B(0, 2000, 0);
    run_B(0, 2000, 50);
    run_B(1, 2000, 50);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C32_nondeadlock_bugs.c -o .work/bin/C32_nondeadlock_bugs && .work/bin/C32_nondeadlock_bugs
(A) atomicity unlocked : checks passed=1051778, NULL at use (would crash)=2263
(A) atomicity LOCKED   : checks passed=2000000, NULL at use (would crash)=0
(B) order     unfixed  : window + 0us, 2000 trials, child saw mThread==NULL (would crash) 0 times
(B) order     unfixed  : window +50us, 2000 trials, child saw mThread==NULL (would crash) 2000 times
(B) order     CV-fixed : window +50us, 2000 trials, child saw mThread==NULL (would crash) 0 times
```

해석:

- (A) 락 없이 200만 번 중 **2,263번** 은 확인 통과 후 사용 시점에 NULL 이었다 = 진짜 코드면 2,263번 크래시 (같은 프로그램을 여러 번 돌리면 7 ~ 2,263 처럼 실행마다 크게 달랐다 — 재현이 들쭉날쭉한 게 이 버그의 본성). 락을 두르면 0.
- (B) 창이 "create 리턴 → 대입" 뿐이면 2000번 중 **0번** (다른 실행에선 1번). **테스트로는 거의 안 잡힌다.** 그런데 init() 에 50µs 짜리 다른 일이 끼면 **2000번 중 2000번** 크래시. 코드 한 줄 추가로 잠복 버그가 매번 터지는 버그로 바뀐다. CV 로 순서를 강제하면 0.

### 9.2 진짜 교착 + wait-for 그래프 탐지기 + 예방 2종 (`code/C32_deadlock_detect.c`)

두 스레드가 반대 순서로 L0, L1 을 잡는다. 감시 스레드가 100ms 마다 "누가 무엇을 쥐고 무엇을 기다리나" 표로 wait-for 그래프를 만들어 사이클을 찾는다 — DB 교착 탐지기와 같은 원리. macOS 에는 `pthread_mutex_timedlock` 이 없어서 이런 바깥 감시자가 실용적이다. 같은 코드에서 `ordered`(주소 순서), `trylock`(내려놓고 랜덤 백오프) 모드도 돌린다.

```c
// C32_deadlock_detect.c — Ch.32.3 교착 상태를 진짜로 만들고, "탐지(detect)" 하고, 예방책 2가지로 비교한다.
//  모드 (argv[1]):
//   deadlock : T1 = lock(L1); lock(L2)   T2 = lock(L2); lock(L1)   (첫 락 후 잠깐 쉬어서 최악 순서 강제)
//   ordered  : 락 주소 순서대로 잡기 (TIP: ENFORCE LOCK ORDERING BY LOCK ADDRESS) → circular wait 제거
//   trylock  : lock(first); trylock(second) 실패 시 first 를 놓고 랜덤 백오프 후 재시도 → no-preemption 우회
//  감시자(watchdog) 스레드: 각 스레드가 "쥔 락 / 기다리는 락" 을 표에 기록하면, 100ms 마다
//  wait-for 그래프(T → 그 락을 쥔 T') 를 만들고 사이클을 찾는다 (DB 의 deadlock detector 와 같은 원리).
//  macOS 에는 pthread_mutex_timedlock 이 없어서 이런 바깥 감시자가 실용적인 방법이다.
//
// build: cc -Wall -Wextra -O0 -pthread code/C32_deadlock_detect.c -o .work/bin/C32_deadlock_detect
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NT 2
#define NL 2
#define LOOPS 100000

static pthread_mutex_t L[NL] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };
static pthread_mutex_t table_m = PTHREAD_MUTEX_INITIALIZER;   // 아래 두 표 보호
static int owner[NL]   = { -1, -1 };   // 락 l 을 쥔 스레드
static int waiting[NT] = { -1, -1 };   // 스레드 t 가 기다리는 락
static long progress[NT];
static long retries = 0;
static enum { DEADLOCK, ORDERED, TRYLOCK } mode;

static void tracked_lock(int t, int l) {
    pthread_mutex_lock(&table_m); waiting[t] = l; pthread_mutex_unlock(&table_m);
    pthread_mutex_lock(&L[l]);
    pthread_mutex_lock(&table_m); waiting[t] = -1; owner[l] = t; pthread_mutex_unlock(&table_m);
}
static int tracked_trylock(int t, int l) {
    if (pthread_mutex_trylock(&L[l]) != 0) return 0;
    pthread_mutex_lock(&table_m); owner[l] = t; pthread_mutex_unlock(&table_m);
    return 1;
}
static void tracked_unlock(int t, int l) {
    (void)t;
    pthread_mutex_lock(&table_m); owner[l] = -1; pthread_mutex_unlock(&table_m);
    pthread_mutex_unlock(&L[l]);
}

static void *worker(void *arg) {
    int t = (int)(long)arg;
    int a = (t == 0) ? 0 : 1, b = (t == 0) ? 1 : 0;   // T0: L0→L1,  T1: L1→L0  (서로 반대 순서)
    if (mode == ORDERED && (uintptr_t)&L[a] > (uintptr_t)&L[b]) { int x = a; a = b; b = x; }
    unsigned seed = (unsigned)t + 1;
    for (long i = 0; i < LOOPS; i++) {
        if (mode == TRYLOCK) {
            for (;;) {
                tracked_lock(t, a);
                if (tracked_trylock(t, b)) break;
                tracked_unlock(t, a);                      // 쥔 걸 내려놓고(스스로 선점) 다시
                __atomic_fetch_add(&retries, 1, __ATOMIC_RELAXED);
                usleep(rand_r(&seed) % 50);                // 랜덤 백오프 → livelock 확률 낮춤
            }
        } else {
            tracked_lock(t, a);
            if (i == 0 && mode == DEADLOCK) usleep(10000); // 첫 락을 쥔 채 잠깐 → 상대도 첫 락을 잡게 됨
            tracked_lock(t, b);
        }
        progress[t]++;                                      // critical section
        tracked_unlock(t, b);
        tracked_unlock(t, a);
    }
    return NULL;
}

// wait-for 그래프에서 사이클 찾기: T → (T 가 기다리는 락의 owner)
static int find_cycle(char *out, size_t n) {
    for (int start = 0; start < NT; start++) {
        int t = start, steps = 0;
        char buf[256]; int len = 0;
        while (steps <= NT) {
            int l = waiting[t];
            if (l < 0) break;
            int o = owner[l];
            if (o < 0) break;
            len += snprintf(buf + len, sizeof buf - len, "T%d -(wants L%d held by)-> ", t, l);
            t = o; steps++;
            if (t == start) { snprintf(out, n, "%sT%d", buf, t); return 1; }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *m = argc > 1 ? argv[1] : "deadlock";
    mode = !strcmp(m, "ordered") ? ORDERED : !strcmp(m, "trylock") ? TRYLOCK : DEADLOCK;
    printf("mode=%s, %d threads x %d iterations, opposite lock order\n", m, NT, LOOPS);

    pthread_t th[NT];
    for (long i = 0; i < NT; i++) pthread_create(&th[i], NULL, worker, (void *)i);

    for (int tick = 1; tick <= 50; tick++) {          // 최대 5초 감시
        usleep(100000);
        if (progress[0] == LOOPS && progress[1] == LOOPS) break;
        char cyc[256];
        pthread_mutex_lock(&table_m);
        int found = find_cycle(cyc, sizeof cyc);
        pthread_mutex_unlock(&table_m);
        if (found) {
            printf("watchdog @%dms: progress T0=%ld T1=%ld\n", tick * 100, progress[0], progress[1]);
            printf("watchdog: CYCLE in wait-for graph: %s\n", cyc);
            printf("watchdog: recovery = kill & restart (like a DB aborting a victim); exiting with status 2\n");
            exit(2);
        }
    }
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    printf("done: T0=%ld T1=%ld iterations", progress[0], progress[1]);
    if (mode == TRYLOCK) printf(", trylock retries=%ld", retries);
    printf("\n");
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C32_deadlock_detect.c -o .work/bin/C32_deadlock_detect
$ .work/bin/C32_deadlock_detect deadlock
mode=deadlock, 2 threads x 100000 iterations, opposite lock order
watchdog @100ms: progress T0=0 T1=0
watchdog: CYCLE in wait-for graph: T0 -(wants L1 held by)-> T1 -(wants L0 held by)-> T0
watchdog: recovery = kill & restart (like a DB aborting a victim); exiting with status 2
(exit status 2)
$ .work/bin/C32_deadlock_detect ordered
mode=ordered, 2 threads x 100000 iterations, opposite lock order
done: T0=100000 T1=100000 iterations
(exit status 0)
$ .work/bin/C32_deadlock_detect trylock
mode=trylock, 2 threads x 100000 iterations, opposite lock order
done: T0=100000 T1=100000 iterations, trylock retries=648
(exit status 0)
```

해석:

- `deadlock`: 첫 감시(100ms)에 이미 진척 0/0, **사이클 T0 → L1(T1 소유) → T1 → L0(T0 소유) → T0** 발견. 복구는 DB 처럼 "희생자 abort" 대신 프로세스 종료(exit 2)로 단순화했다. (스레드만 골라 죽이면 쥔 락과 깨진 불변식이 남는다 — 그래서 현실의 복구는 트랜잭션 롤백이나 재시작이다.)
- `ordered`: 둘 다 낮은 주소 락부터 → 순환 대기 불가 → 20만 번 모두 완료.
- `trylock`: 교착은 없지만 **648번** 은 두 번째 락을 못 얻어 내려놓고 다시 했다. 낭비가 생기지만 진행은 한다.

### 9.3 OSTEP 숙제 `threads-bugs` — vector_add 의 다섯 가지 버전

숙제는 C 코드다. 원본 폴더를 더럽히지 않으려고 `.work/bin/hw/` 로 빌드했다.

```text
$ cd .tools/ostep-homework/threads-bugs
$ for f in vector-deadlock vector-global-order vector-try-wait vector-avoid-hold-and-wait vector-nolock; do
    cc -o ../../../.work/bin/hw/$f $f.c -Wall -pthread -O; done
vector-nolock.c:15:5: error: invalid output constraint '=a' in asm
   15 |                  :"=a" (value)                  
      |                   ^
1 error generated.
```

`vector-nolock.c` 는 x86 인라인 어셈블리 `lock; xaddl` 로 fetch-and-add 를 구현해서 **Apple Silicon(arm64)에서는 컴파일되지 않는다**. arm64 라면 `__atomic_fetch_add(&v, n, __ATOMIC_SEQ_CST)` (→ ARMv8.1 `LDADD` 명령)로 바꿔야 한다. 나머지 넷은 빌드됐다.

**(a) `vector-deadlock` — 교착이 "가끔" 나는 걸 확인**

```text
$ cd ../../../.work/bin/hw
$ ./vector-deadlock -n 2 -l 1 -v -d      # 3번 반복, 3번 모두 정상 종료
->add(0, 1)
<-add(0, 1)
              ->add(1, 0)
              <-add(1, 0)
$ f=0; for i in $(seq 1 10); do perl -e 'alarm 3; exec @ARGV' ./vector-deadlock -n 2 -l 10000 -d >/dev/null 2>&1 || f=$((f+1)); done; echo "deadlock -d hangs: $f/10"
deadlock -d hangs: 10/10
```

`-d` 는 스레드마다 `vector_add(v1,v2)` / `vector_add(v2,v1)` 로 순서를 엇갈리게 한다. 한 번만 부르면(`-l 1`) 첫 스레드가 끝난 뒤 두 번째가 시작해서 교착이 안 난다. 1만 번 반복하면 **10번 중 10번** 멈춘다 (3초 알람으로 종료). "가끔" 의 확률은 반복 횟수와 함께 급격히 1 로 간다.

**(b) 예방책들의 비용 비교** (`-l 100000 -d`, `-p` 는 스레드마다 다른 벡터 쌍 = 경합 없음, `-t` 시간 출력)

```text
$ ./vector-global-order -n 2 -l 100000 -d -t        →  Time: 0.01 seconds
$ ./vector-global-order -n 2 -l 100000 -d -p -t     →  Time: 0.01 seconds
$ ./vector-try-wait -n 2 -l 100000 -d -t            →  Retries: 6656253  Time: 0.52 seconds
$ ./vector-try-wait -n 2 -l 100000 -d -p -t         →  Retries: 0        Time: 0.01 seconds
$ ./vector-avoid-hold-and-wait -n 2 -l 100000 -d -t     →  Time: 0.02 seconds
$ ./vector-avoid-hold-and-wait -n 2 -l 100000 -d -p -t  →  Time: 0.01 seconds
$ ./vector-global-order -n 8 -l 100000 -d -t        →  Time: 0.41 seconds
$ ./vector-global-order -n 8 -l 100000 -d -p -t     →  Time: 0.02 seconds
$ ./vector-try-wait -n 8 -l 100000 -d -t            →  (30초 알람까지 안 끝남, user 229s / real 30s = 8코어 전부 헛돔)
$ ./vector-try-wait -n 8 -l 1000 -d -t              →  Retries: 2654488  Time: 0.28 seconds
$ ./vector-try-wait -n 8 -l 100000 -d -p -t         →  Retries: 0        Time: 0.02 seconds
$ ./vector-avoid-hold-and-wait -n 8 -l 100000 -d -t     →  Time: 1.80 seconds
$ ./vector-avoid-hold-and-wait -n 8 -l 100000 -d -p -t  →  Time: 0.13 seconds
```

(실제 출력의 줄바꿈만 한 줄로 합쳤다.) 해석:

- **global-order 가 가장 싸다.** 락을 더 잡지도, 재시도하지도 않는다. 경합이 있으면(8스레드 0.41s) 느려지지만 그건 벡터 2개를 8명이 나눠 쓰는 본질적 비용.
- **try-wait 는 경합이 생기면 폭발한다.** 2스레드에서도 재시도 665만 번, 8스레드에서는 30초 동안 8코어를 다 태우고도 못 끝냈다 — 숙제 코드는 백오프가 없고 첫 trylock 도 바쁜 대기라서 사실상 **livelock**. 경합이 없으면(`-p`) 재시도 0.
- **avoid-hold-and-wait** 는 전역 락 하나가 모든 획득을 직렬화해서, 경합 없는 `-p` 에서도 8스레드 0.13s 로 global-order(0.02s)보다 6배 느리다. "동시성을 깎는다"는 책의 말이 숫자로 보인다.

## 10. 펌웨어 엔지니어의 눈으로

- **ISR ↔ 태스크 공유 변수는 원자성 위반의 단골.** 태스크가 `if (q->count) { item = q->buf[q->head]; ... }` 하는 사이 ISR 이 `count` 와 `head` 를 바꾸는 패턴. 32비트 MCU 에서 64비트 타임스탬프를 두 번에 읽는 것(상위/하위 사이에 캐리 발생)도 같은 종류다. 해법은 짧은 critical section(`__disable_irq`/BASEPRI), 또는 상위-하위-상위 재읽기, 또는 SPSC 링으로 쓰는 쪽을 하나로 제한.
- **순서 위반 = "초기화 전에 IRQ 활성화".** NVIC 에서 인터럽트를 켠 뒤 핸들러가 쓰는 디스크립터 링/콜백 포인터를 초기화하면, 부팅 중 펜딩 인터럽트가 NULL 콜백을 부른다. 드라이버 probe 에서 `request_irq` 를 자료 구조 준비 **뒤**에 하는 게 리눅스의 관례인 이유. DMA 디스크립터를 메모리에 쓰고 **배리어(DMB/DSB, `wmb()`) 없이** doorbell 을 울리는 것도 하드웨어 버전의 순서 위반이다.
- **SSD FW 의 교착은 자원 순서로 예방한다.** 쓰기 경로가 write buffer 슬롯 → NAND 채널 → 매핑 테이블 락 순서로 잡는데, GC 가 매핑 테이블 락 → write buffer 순서로 잡으면 순환이 생긴다. 실무에서는 "버퍼 → 채널 → 메타데이터" 같은 전역 순서를 문서화하고, 위반을 디버그 빌드에서 잡는 **lockdep 류 검사기**를 둔다 (리눅스 `lockdep` 이 런타임에 락 순서 그래프를 만들어 사이클 가능성만으로 경고하는 것과 같은 원리).
- **워치독(watchdog) = 탐지와 복구.** 임베디드에서 교착을 다루는 가장 흔한 방식은 사실 Tom West 식이다: 각 태스크가 주기적으로 체크인하고, 하나라도 빠지면 HW 워치독이 리셋 (9.2 의 감시자 + exit 와 같은 구조). SSD 라면 리셋 후 전원 손실 복구 경로로 FTL 을 재구성한다. 리셋 원인 레지스터와 크래시 덤프에 "누가 무엇을 쥐고 무엇을 기다렸나" 를 남겨 두면 디버깅이 훨씬 쉬워진다.
- **Banker's algorithm 류의 "미리 다 아는" 회피는 하드 실시간에서 실제로 쓴다.** 정적으로 정의된 태스크 집합에 **우선순위 상한 프로토콜(priority ceiling)** 을 적용하면 교착과 무한 priority inversion 이 모두 사라진다 (OSEK/AUTOSAR 의 OS 리소스). 모든 태스크와 리소스를 컴파일 타임에 아는 임베디드라서 가능한 일 — 책이 말한 "limited environments" 의 실제 사례.
- **AI 가속기 런타임**: 여러 스트림/큐가 서로의 이벤트(fence)를 기다리는 그래프에 사이클이 생기면 GPU 행(hang)이 된다 (큐 A 가 B 의 fence 대기, B 가 A 의 fence 대기). 드라이버의 TDR(timeout detection & recovery)이 바로 탐지-복구다. 커맨드 제출 시 의존성을 DAG 로 유지(= 순환 대기 예방)하는 게 정석.

## 11. 면접 질문

### Q1. 교착의 4가지 필요조건과 각각을 깨는 방법을 말하라.
<details>
<summary>답 보기</summary>

**상호 배제** → lock-free/wait-free 자료 구조(CAS), 공유 자체를 없애기(per-CPU 데이터, 메시지 패싱). **hold-and-wait** → 필요한 락을 한꺼번에 획득(전역 prevention 락, 또는 all-or-nothing 요청). **비선점** → `trylock` 실패 시 쥔 락을 놓고 재시도(+랜덤 백오프로 livelock 완화). **순환 대기** → **락 획득 순서를 전역적으로 정하기** (주소 순서, 계층 순서). 실무에서 가장 많이 쓰는 건 순환 대기 제거이고, 리눅스는 `lockdep` 으로 순서 위반을 런타임에 검출한다.

</details>

### Q2. 두 개의 락을 인자로 받는 함수(예: `transfer(acct *a, acct *b)`)를 교착 없이 구현하라.
<details>
<summary>답 보기</summary>

호출자가 `(a,b)` 와 `(b,a)` 를 동시에 부를 수 있으므로 **인자 순서가 아니라 고유한 전역 순서**로 잡는다: 락 주소(또는 계좌 ID)가 작은 쪽 먼저. `if (a == b)` 이면 한 번만 잠근다(같은 락 두 번 → 자기 교착). 해제는 순서 무관. 대안으로 `trylock` + 백오프도 가능하지만 경합이 크면 재시도가 폭발한다(숙제 실측: 8스레드에서 사실상 livelock). 순서 방식이 가장 싸다(실측 0.01s vs try-wait 0.52s).

</details>

### Q3. 원자성 위반과 순서 위반의 차이, 그리고 각각의 대표적 해결책은?
<details>
<summary>답 보기</summary>

**원자성 위반**: 한 덩어리여야 할 여러 메모리 접근(check-then-use, read-modify-write) 사이에 다른 스레드가 끼어듦 → 같은 **락**으로 묶거나 원자 명령 사용. **순서 위반**: A 가 B 보다 먼저 일어나야 하는데 보장이 없음(초기화 전 사용, 생성 직후 실행) → **CV + 상태 변수, 세마포어(초기값 0), join, 배리어**로 순서를 강제. Lu et al. 연구에서 비교착 버그의 97% 가 이 둘. 둘 다 재현이 들쭉날쭉해서(실측: 200만 번 중 7~2,263번) 테스트보다 **코드 리뷰와 도구(TSan, 정적 분석)** 가 중요하다.

</details>

### Q4. 교착과 livelock 의 차이는? livelock 은 어떻게 막나?
<details>
<summary>답 보기</summary>

**교착**: 스레드들이 블록된 채 아무것도 안 함 (CPU 0%). **livelock**: 스레드들이 계속 실행되며 상태를 바꾸지만(예: 락을 잡았다 놓았다) 아무도 진척이 없음 (CPU 100%). trylock-후퇴 프로토콜, 충돌 후 동시에 재전송하는 네트워크 등에서 생긴다. 해결: **랜덤(지수) 백오프**로 대칭성 깨기, 우선순위/나이 기반 승자 결정(wound-wait, wait-die), 재시도 횟수 초과 시 전역 락으로 승격. 숙제 `vector-try-wait` 8스레드 실측에서 30초간 8코어를 다 쓰고도 못 끝난 게 livelock 의 모습이다.

</details>

### Q5. 교착 탐지는 어떻게 구현하나? 탐지 후 복구는?
<details>
<summary>답 보기</summary>

자원마다 소유자, 스레드마다 기다리는 자원을 기록해서 **wait-for 그래프**(T → 기다리는 자원의 소유자 T')를 만들고 주기적으로(또는 대기 시작 시) **사이클 탐지**(DFS). 자원 인스턴스가 여러 개면 자원 할당 그래프 + 축약 알고리즘. 복구: DB 는 **희생자 트랜잭션 abort + 롤백**(가장 적게 일한/젊은 트랜잭션 선택), OS/임베디드는 프로세스 kill 또는 **워치독 리셋**. 스레드만 강제 종료하면 쥔 락과 깨진 불변식이 남으므로, 복구 단위는 상태를 버릴 수 있는 단위(트랜잭션, 프로세스, 시스템)여야 한다.

</details>

### Q6. lock-free 자료 구조가 교착을 없애는데 왜 기본 선택이 아닌가?
<details>
<summary>답 보기</summary>

(1) **정확히 만들기 매우 어렵다** — ABA 문제, 메모리 회수(hazard pointer, epoch, RCU), 메모리 순서(acquire/release) 등. (2) **일반성이 낮다** — 스택/큐/카운터는 되지만 여러 객체를 원자적으로 바꾸는 연산은 어렵다. (3) **livelock/기아 가능** — lock-free 는 "누군가는 진행"만 보장하고 특정 스레드의 진행은 보장 안 한다 (wait-free 는 보장하지만 더 어렵고 느림). (4) 경합이 심하면 CAS 재시도와 캐시라인 핑퐁으로 오히려 느릴 수 있다. 그래서 핫스팟에만 검증된 라이브러리(리눅스 `llist`, RCU)로 쓴다.

</details>

## 12. 자가 점검 & 숙제

### 퀴즈 1. 다음은 원자성 위반인가 순서 위반인가? "스레드 A 가 `buf` 를 malloc 하고 `ready=1` 을 세우기 전에, 스레드 B 가 `buf` 에 쓴다."
<details>
<summary>정답</summary>

**순서 위반**. "A 의 초기화 → B 의 사용" 순서가 강제되지 않은 것. 해결: B 가 `while (!ready) cond_wait` 로 기다리고 A 가 `ready=1; signal`. (락만 둘러서는 순서가 보장되지 않는다.)

</details>

### 퀴즈 2. `lock(prevention)` 방식에서, 어떤 스레드가 prevention 없이 L2 만 잡는 코드가 하나라도 있으면 어떻게 되나?
<details>
<summary>정답</summary>

그 스레드는 hold-and-wait 를 하지 않으므로(락 1개만) 그 자체로 교착을 만들진 않는다. 하지만 L2 를 잡은 채 **다른 락을 하나라도 더** 기다리는 경로가 prevention 밖에 있으면 보장이 깨진다. 규칙은 "여러 락을 잡는 모든 경로는 prevention 을 먼저" 다. (책의 표현으로는 "any time any thread grabs a lock, it first acquires the global prevention lock".)

</details>

### 퀴즈 3. 6장 Banker's 예제(총 10개; Alloc 3/2/2, Max 7/4/9)에서 T3 가 1개를 요청하면 허용되나?
<details>
<summary>정답</summary>

가정 상태: T3 Alloc=3, Need=6, Available=2. T2(Need 2 ≤ 2) → Available 4 → T1(Need 4 ≤ 4) → Available 7 → T3(Need 6 ≤ 7) → 완료. 안전 순서 <T2, T1, T3> 존재 → **SAFE, 허용**.

</details>

### 퀴즈 4. CPU 가 하나뿐인 시스템에서도 교착이 생길 수 있나?
<details>
<summary>정답</summary>

**생긴다.** 교착은 병렬성이 아니라 **인터리빙** 문제다. T1 이 L1 을 잡은 뒤 타이머 인터럽트로 T2 로 전환, T2 가 L2 를 잡고 L1 을 기다리며 잠들면, T1 이 다시 돌아 L2 를 기다리며 잠든다. 단일 코어 RTOS 에서도 흔하다.

</details>

### 숙제 (OSTEP `threads-bugs`, 원문 v0.91 에 문항이 없어 숙제 README 기준)

- `vector-deadlock -n 2 -l 1 -v` 를 `-d` 유무로 여러 번 → **교착이 확률적으로만 나타남**을 확인하고, `-l` 을 키우며 멈추는 비율이 어떻게 변하는지 기록.
- `vector-global-order.c` 에서 `v_dst == v_src` 특수 처리를 지우고 `-n 2 -d` 로 같은 벡터를 넘기는 경로를 만들면? → **자기 교착(self-deadlock)** 확인.
- `vector-try-wait.c` 에 랜덤 백오프(`usleep(rand()%50)`)를 넣고 `-n 8 -d -t` 를 다시 재기 → **livelock 완화 효과** 측정. (arm64 에서 `vector-nolock.c` 를 돌리려면 `fetch_and_add` 를 `__atomic_fetch_add` 로 바꾸기.)

## 13. 다음으로

- [Ch.33 이벤트 기반 동시성](2026-09-30_C33_event_based_concurrency.md): 락 자체를 안 쓰는(단일 스레드 이벤트 루프) 접근 — 이 장의 버그들을 구조적으로 피하는 다른 길.
- [Ch.31 세마포어](2026-09-30_C31_semaphores.md): 식사하는 철학자 = 순환 대기 제거의 예.
- 저장장치 쪽에서 "탐지 후 복구"의 끝판왕: [Ch.42 크래시 일관성과 저널링](2026-09-30_C42_crash_consistency_journaling.md).
