# Ch.29 락 기반 동시 자료구조 — 카운터, 리스트, 큐, 해시에 락 붙이기

> 📖 원문: [29. Lock-based Concurrent Data Structures](../book-md/C29_lock_based_concurrent_data_structures.md) · [PDF p.333](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=333) · ⏱️ 읽기 약 45분 · 🔗 선행: [Ch.28](2026-09-30_C28_locks.md)

## 0. 한눈에 보기

- 자료구조에 락을 붙여 여러 스레드가 써도 안전하게 만든 것 = **스레드 안전(thread safe)**. 가장 쉬운 방법은 **큰 락 하나**: 연산 시작에 잡고 끝에 푼다. 대부분 이걸로 충분하다.
- 느리면 그때 고친다: **카운터 → sloppy(근사) 카운터**(코어별 로컬 + 주기적 합산), **큐 → head/tail 락 2개**(Michael & Scott), **해시 → 버킷마다 락**. 리스트의 **hand-over-hand(노드별 락)** 는 이론상 동시성이 높지만 실제론 대개 더 느리다.
- 이 Mac 실측: 4 스레드 sloppy 카운터(S=1024)가 mutex 카운터보다 **약 30배** 빠르고, 로컬 카운터를 캐시라인(128B)마다 떨어뜨리지 않으면 **5배** 느려진다(false sharing). hand-over-hand 리스트는 단일 락보다 **4.6배 느렸다.**
- 교훈 세 개: 제어 흐름(early return) 주변의 락을 조심, **동시성이 높다고 빠른 게 아니다**, **조기 최적화 금지**(Knuth).

> **CRUX: HOW TO ADD LOCKS TO DATA STRUCTURES** — "When given a particular data structure, how should we add locks to it, in order to make it work correctly? Further, how do we add locks such that the data structure yields high performance, enabling many threads to access the structure at once, i.e., concurrently?"
>
> → 주어진 자료구조에 락을 어떻게 붙여야 올바르게 동작하나? 나아가 많은 스레드가 동시에 접근할 수 있도록, 높은 성능을 내게 하려면 락을 어떻게 붙여야 하나?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 스레드 안전(thread safe) | 여러 스레드가 동시에 불러도 올바른 자료구조/함수 | `malloc()` (스레드 안전) |
| 큰 락 하나(coarse lock) | 연산 전체를 락 하나로 감쌈 | 모니터(monitor) 스타일 |
| 완벽한 확장(perfect scaling) | 스레드 N개가 N배 일을 해도 걸린 시간 동일 | 이상적 목표 |
| sloppy(approximate) counter | 코어별 로컬 카운터 + 임계값마다 전역에 합산 | 리눅스 `percpu_counter` |
| 임계값 S(sloppiness) | 로컬에서 전역으로 옮기는 주기 | 클수록 빠르고 부정확 |
| false sharing | 다른 변수인데 같은 캐시라인이라 코어끼리 싸움 | 로컬 카운터 배열을 붙여 놓음 |
| hand-over-hand locking | 다음 노드 락 잡고 → 현재 노드 락 풀기 | lock coupling |
| Michael & Scott 큐 | head 락 / tail 락 분리 + 더미 노드 | enqueue와 dequeue 동시 진행 |
| 더미(dummy) 노드 | 항상 존재하는 빈 노드, head와 tail이 같은 노드를 만지지 않게 함 | 초기화 때 1개 할당 |
| 버킷별 락 해시 | 버킷(리스트)마다 독립된 락 | Java `ConcurrentHashMap` 초기 설계 |
| BKL(Big Kernel Lock) | 리눅스 초기의 커널 전체 락 | 2.6.39 즈음 제거 |
| 조기 최적화(premature optimization) | 문제가 확인되기 전에 복잡하게 만드는 것 | "모든 악의 근원" (Knuth) |

## 2. 동시 카운터 (29.1)

### 2.1 단순하지만 확장되지 않는 버전

락 없는 카운터(Figure 29.1)는 `c->value++` 한 줄짜리지만 [Ch.26](2026-09-30_C26_concurrency_intro.md)에서 본 것처럼 스레드 안전하지 않다. 락 하나를 붙이면 끝 (Figure 29.2):

```c
typedef struct __counter_t {
    int             value;
    pthread_mutex_t lock;
} counter_t;

void init(counter_t *c) {
    c->value = 0;
    Pthread_mutex_init(&c->lock, NULL);
}
void increment(counter_t *c) {
    Pthread_mutex_lock(&c->lock);
    c->value++;
    Pthread_mutex_unlock(&c->lock);
}
void decrement(counter_t *c) {
    Pthread_mutex_lock(&c->lock);
    c->value--;
    Pthread_mutex_unlock(&c->lock);
}
int get(counter_t *c) {
    Pthread_mutex_lock(&c->lock);
    int rc = c->value;
    Pthread_mutex_unlock(&c->lock);
    return rc;
}
```

가장 기본적인 패턴: **메서드 진입 시 락, 리턴 시 해제**. 객체 메서드 호출 때 자동으로 락이 걸리는 **모니터(monitor)** 와 같은 모양이다 ([부록 D 모니터](2026-09-30_C0D_monitors.md)).

**정확하다. 문제는 성능이다.** 책의 측정(4코어 2.7 GHz i5 iMac, 스레드당 100만 번): 1 스레드 약 0.03 초, **2 스레드 5초 이상**. 이 Mac에서는 (§7.1):

```text
threads   precise(mutex)   atomic(ldadd)   sloppy(S=1024)
      1            0.005           0.002            0.006
      2            0.020           0.008            0.006
      4            0.186           0.031            0.006
      8            0.204           0.320            0.012
```

10년 사이 뮤텍스 구현이 크게 좋아져서 책만큼 극적이진 않지만, **1 → 4 스레드에서 약 37배 느려진다** (일은 4배인데 시간은 37배). 이상적인 건 **완벽한 확장(perfect scaling)**: 일이 N배여도 N개 코어가 나눠 하니 시간은 그대로여야 한다.

### 2.2 확장되는 카운팅: sloppy counter

연구([B+10] Linux 멀티코어 확장성 분석)에 따르면 **확장 가능한 카운터가 없으면 리눅스의 일부 워크로드가 멀티코어에서 심각하게 느려진다.** 여러 기법 중 책이 소개하는 것이 **sloppy counter**:

- 논리적 카운터 하나를 **코어마다 로컬 카운터 하나 + 전역 카운터 하나**로 표현. 4코어면 로컬 4개 + 전역 1개. 락도 로컬마다 하나, 전역에 하나.
- 증가: 자기 코어의 **로컬 카운터만** 올린다 (로컬 락). 다른 코어와 안 싸운다 → 확장된다.
- 로컬 값이 **임계값 S** 에 도달하면 전역 락을 잡고 전역에 더한 뒤 로컬을 0으로.
- 읽기(`get`): 전역 값만 본다 → **근사값**. 정확한 값이 필요하면 모든 로컬 락과 전역 락을 (정해진 순서로, 데드락 방지) 잡고 합산 — 확장성은 포기.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C29-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">sloppy counter (S = 5): 로컬에서 자주, 전역은 가끔</text>
  <text x="85" y="55" text-anchor="middle" fill="currentColor">CPU 1</text>
  <text x="255" y="55" text-anchor="middle" fill="currentColor">CPU 2</text>
  <text x="425" y="55" text-anchor="middle" fill="currentColor">CPU 3</text>
  <text x="595" y="55" text-anchor="middle" fill="currentColor">CPU 4</text>
  <rect x="35" y="65" width="100" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="85" y="87" text-anchor="middle" fill="currentColor">L1 = 4</text>
  <text x="85" y="105" text-anchor="middle" fill="currentColor">llock[0]</text>
  <rect x="205" y="65" width="100" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="255" y="87" text-anchor="middle" fill="currentColor">L2 = 1</text>
  <text x="255" y="105" text-anchor="middle" fill="currentColor">llock[1]</text>
  <rect x="375" y="65" width="100" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="425" y="87" text-anchor="middle" fill="currentColor">L3 = 3</text>
  <text x="425" y="105" text-anchor="middle" fill="currentColor">llock[2]</text>
  <rect x="545" y="65" width="100" height="50" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="595" y="87" text-anchor="middle" fill="currentColor">L4 = 5 → 0</text>
  <text x="595" y="105" text-anchor="middle" fill="currentColor">llock[3]</text>
  <rect x="250" y="190" width="200" height="56" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="350" y="214" text-anchor="middle" fill="currentColor">Global G = 5 → 10</text>
  <text x="350" y="234" text-anchor="middle" fill="currentColor">glock (드물게만 잡힘)</text>
  <line x1="595" y1="117" x2="452" y2="200" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C29-arrow)"/>
  <text x="560" y="165" fill="currentColor">L 이 S 에 닿으면 +5 이전</text>
  <line x1="85" y1="117" x2="248" y2="205" stroke="currentColor" stroke-dasharray="3,4"/>
  <line x1="255" y1="117" x2="300" y2="188" stroke="currentColor" stroke-dasharray="3,4"/>
  <line x1="425" y1="117" x2="400" y2="188" stroke="currentColor" stroke-dasharray="3,4"/>
  <text x="350" y="280" text-anchor="middle" fill="currentColor">get() = G = 10, 진짜 값 = 10 + 4 + 1 + 3 = 18. 오차는 최대 (CPU 수) × (S − 1).</text>
</svg>
```

### 2.3 Figure 29.4 트레이스 따라가기 (S = 5)

| 시각 | L1 | L2 | L3 | L4 | G | 일어난 일 |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 | 0 | |
| 1 | 0 | 0 | 1 | 1 | 0 | L3, L4 증가 |
| 2 | 1 | 0 | 2 | 1 | 0 | |
| 3 | 2 | 0 | 3 | 1 | 0 | |
| 4 | 3 | 0 | 3 | 2 | 0 | |
| 5 | 4 | 1 | 3 | 3 | 0 | |
| 6 | 5→0 | 1 | 3 | 4 | 5 | L1이 S 도달 → G에 5 이전 |
| 7 | 0 | 2 | 4 | 5→0 | 10 | L4가 S 도달 → G에 5 이전 |

시각 7에서 진짜 합 = 0+2+4+0+10 = 16인데 `get()` 은 10을 돌려준다. 오차 6. 오차 상한은 **로컬 개수 × (S−1)** = 4 × 4 = 16. (책은 "CPU 수 × S 이하"로 말한다.)

새 계산 예 (이 Mac 실측, §7.1 실험 2): 4 스레드가 각 1,000,000번, S=1024.

- 각 로컬에 남는 값 = 1,000,000 mod 1024 = 1,000,000 − 976×1024 = 1,000,000 − 999,424 = **576**
- 전역에 안 넘어간 총합 = 4 × 576 = **2304** → `get()` = 4,000,000 − 2304 = **3,997,696**. 실측 출력과 정확히 일치한다.
- S=128이면 1,000,000 mod 128 = 64 → 4 × 64 = **256** (실측 3,999,744 ✓). S ≤ 64이면 1,000,000이 S로 나누어떨어져 오차 0.

### 2.4 sloppy counter 코드 (Figure 29.5)

```c
typedef struct __counter_t {
    int             global;            // global count
    pthread_mutex_t glock;             // global lock
    int             local[NUMCPUS];    // local count (per cpu)
    pthread_mutex_t llock[NUMCPUS];    // ... and locks
    int             threshold;         // update frequency
} counter_t;

void update(counter_t *c, int threadID, int amt) {
    int cpu = threadID % NUMCPUS;
    pthread_mutex_lock(&c->llock[cpu]);
    c->local[cpu] += amt;                     // assumes amt > 0
    if (c->local[cpu] >= c->threshold) {      // transfer to global
        pthread_mutex_lock(&c->glock);
        c->global += c->local[cpu];
        pthread_mutex_unlock(&c->glock);
        c->local[cpu] = 0;
    }
    pthread_mutex_unlock(&c->llock[cpu]);
}

int get(counter_t *c) {                       // only approximate!
    pthread_mutex_lock(&c->glock);
    int val = c->global;
    pthread_mutex_unlock(&c->glock);
    return val;
}
```

Figure 29.6(S 스윕): S가 작으면 느리지만 정확, 크면 빠르지만 전역 값이 뒤처진다. 이 **정확도/성능 트레이드오프**를 조절할 수 있게 해 주는 게 sloppy counter의 핵심이다. 책의 측정에선 S=1024에서 4 CPU × 100만 번이 1 CPU × 100만 번과 거의 같은 시간 — 완벽한 확장에 가깝다. 이 Mac에서도 그렇다: 1 스레드 0.006 s, 4 스레드 0.006 s.

한 가지 책 코드가 놓친 것: `local[NUMCPUS]` 와 `llock[NUMCPUS]` 를 **배열로 다닥다닥** 붙이면 서로 다른 CPU의 로컬 카운터가 **같은 캐시라인**에 들어간다. 코어마다 다른 변수를 써도 캐시라인 단위로 소유권이 오가서 결국 싸운다 — **false sharing**. M2 캐시라인 128B에서 `int + pthread_mutex_t` 슬롯(72B)을 붙여 두면 5.1배 느려졌다 (§7.1 실험 3). 실전 sloppy counter는 로컬을 반드시 캐시라인 정렬(`_Alignas(128)`, 리눅스 `____cacheline_aligned`)하거나 per-CPU 메모리 영역에 둔다.

## 3. 동시 연결 리스트 (29.2)

### 3.1 기본: 큰 락 하나 (Figure 29.7)

```c
int List_Insert(list_t *L, int key) {
    pthread_mutex_lock(&L->lock);
    node_t *new = malloc(sizeof(node_t));
    if (new == NULL) {
        perror("malloc");
        pthread_mutex_unlock(&L->lock);   // ← 실패 경로에서도 잊지 말고 풀어야
        return -1;
    }
    new->key  = key;
    new->next = L->head;
    L->head   = new;
    pthread_mutex_unlock(&L->lock);
    return 0;
}
```

문제는 **드문 실패 경로**다. malloc 실패 시 unlock을 잊기 쉽다. 리눅스 커널 패치 연구에 따르면 **버그의 약 40%가 이런 드물게 실행되는 경로**에 있었다.

### 3.2 다시 쓰기: 락 범위를 진짜 임계 구역으로 (Figure 29.8)

```c
void List_Insert(list_t *L, int key) {
    node_t *new = malloc(sizeof(node_t));   // 락 밖: malloc 은 스레드 안전
    if (new == NULL) { perror("malloc"); return; }
    new->key = key;

    pthread_mutex_lock(&L->lock);           // 공유 리스트를 바꾸는 2줄만 잠근다
    new->next = L->head;
    L->head   = new;
    pthread_mutex_unlock(&L->lock);
}

int List_Lookup(list_t *L, int key) {
    int rv = -1;
    pthread_mutex_lock(&L->lock);
    node_t *curr = L->head;
    while (curr) {
        if (curr->key == key) { rv = 0; break; }   // return 대신 break
        curr = curr->next;
    }
    pthread_mutex_unlock(&L->lock);
    return rv;                                     // 출구는 하나
}
```

두 가지 변화:

1. **insert**: malloc과 노드 초기화는 공유 데이터가 아니므로 락 밖으로. 실패 경로에 unlock이 아예 필요 없어진다.
2. **lookup**: 루프 안의 `return` 을 `break` 로 → **lock/unlock 지점이 하나씩**. "unlock 깜빡" 버그의 여지를 없앤다.

> **TIP — BE WARY OF LOCKS AND CONTROL FLOW** — 함수가 락 획득, 메모리 할당 같은 상태 있는 작업으로 시작하면, 에러로 일찍 리턴할 때마다 그걸 전부 되돌려야 한다. 이런 패턴 자체를 최소화하도록 코드를 구조화하라. (C에서 흔한 해법: `goto out;` 단일 정리 경로, 커널 스타일.)

### 3.3 리스트 확장하기: hand-over-hand locking

리스트 전체에 락 하나 대신 **노드마다 락**. 순회할 때 **다음 노드의 락을 먼저 잡고, 현재 노드의 락을 푼다** (손을 번갈아 잡으며 줄을 타는 모양 → hand-over-hand, lock coupling).

```svg
<svg viewBox="0 0 700 220" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C29-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">hand-over-hand: 최대 2개 노드 락만 쥐고 전진</text>
  <rect x="30" y="60" width="90" height="44" fill="none" stroke="currentColor"/>
  <text x="75" y="87" text-anchor="middle" fill="currentColor">n1</text>
  <rect x="170" y="60" width="90" height="44" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="215" y="87" text-anchor="middle" fill="currentColor">n2 🔒</text>
  <rect x="310" y="60" width="90" height="44" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="355" y="87" text-anchor="middle" fill="currentColor">n3 🔒</text>
  <rect x="450" y="60" width="90" height="44" fill="none" stroke="currentColor"/>
  <text x="495" y="87" text-anchor="middle" fill="currentColor">n4</text>
  <rect x="590" y="60" width="90" height="44" fill="none" stroke="currentColor"/>
  <text x="635" y="87" text-anchor="middle" fill="currentColor">n5</text>
  <line x1="120" y1="82" x2="168" y2="82" stroke="currentColor" marker-end="url(#C29-arrow2)"/>
  <line x1="260" y1="82" x2="308" y2="82" stroke="currentColor" marker-end="url(#C29-arrow2)"/>
  <line x1="400" y1="82" x2="448" y2="82" stroke="currentColor" marker-end="url(#C29-arrow2)"/>
  <line x1="540" y1="82" x2="588" y2="82" stroke="currentColor" marker-end="url(#C29-arrow2)"/>
  <text x="215" y="135" text-anchor="middle" fill="currentColor">② 이걸 푼다</text>
  <text x="355" y="135" text-anchor="middle" fill="currentColor">① 이걸 먼저 잡고</text>
  <text x="350" y="175" text-anchor="middle" fill="currentColor">다른 스레드는 n2 뒤를 따라오거나 n3 앞쪽을 동시에 순회할 수 있다 (동시성 ↑)</text>
  <text x="350" y="198" text-anchor="middle" fill="#d9534f">대가: 노드마다 lock/unlock 2회 → 1000 노드 순회 = 원자 연산 수천 번 (M2 실측 4.6배 느림)</text>
</svg>
```

개념적으로는 말이 된다. **하지만 실제로는 단일 락보다 빠르게 만들기 어렵다.** 순회하며 노드마다 락을 잡고 푸는 오버헤드가 너무 크다. 리스트가 크고 스레드가 많아도, 동시 순회로 얻는 이득이 "락 한 번 잡고 쭉 훑고 푼다"를 이기기 어렵다. 몇 노드마다 한 번씩 락을 잡는 하이브리드는 연구해 볼 만하다.

이 Mac 실측 (§7.2 실험 2, 1000 노드 리스트, 4 스레드 × 20,000 lookup): 단일 락 0.100 s, **hand-over-hand 0.465 s (4.6배 느림)**.

> **TIP — MORE CONCURRENCY ISN'T NECESSARILY FASTER** — 설계가 오버헤드를 많이 더하면(락을 한 번이 아니라 자주 잡고 풀면) 동시성이 높다는 건 별 의미가 없다. 단순한 설계는 비싼 연산을 드물게 쓰면 잘 동작한다. 진실을 아는 유일한 방법: **둘 다 만들어서 재 봐라.** 성능은 속일 수 없다.

## 4. 동시 큐 (29.3) — Michael & Scott 2-락 큐

큰 락 하나는 누구나 할 수 있으니 건너뛰고, Michael과 Scott의 조금 더 동시적인 큐를 본다 (Figure 29.9).

```c
typedef struct __node_t { int value; struct __node_t *next; } node_t;
typedef struct __queue_t {
    node_t          *head;
    node_t          *tail;
    pthread_mutex_t  headLock;
    pthread_mutex_t  tailLock;
} queue_t;

void Queue_Init(queue_t *q) {
    node_t *tmp = malloc(sizeof(node_t));   // 더미 노드
    tmp->next = NULL;
    q->head = q->tail = tmp;
    pthread_mutex_init(&q->headLock, NULL);
    pthread_mutex_init(&q->tailLock, NULL);
}

void Queue_Enqueue(queue_t *q, int value) {
    node_t *tmp = malloc(sizeof(node_t));
    assert(tmp != NULL);
    tmp->value = value;
    tmp->next  = NULL;
    pthread_mutex_lock(&q->tailLock);       // tail 쪽만
    q->tail->next = tmp;
    q->tail = tmp;
    pthread_mutex_unlock(&q->tailLock);
}

int Queue_Dequeue(queue_t *q, int *value) {
    pthread_mutex_lock(&q->headLock);       // head 쪽만
    node_t *tmp = q->head;                  // 현재 더미
    node_t *newHead = tmp->next;
    if (newHead == NULL) {
        pthread_mutex_unlock(&q->headLock);
        return -1;                          // 비었음
    }
    *value = newHead->value;
    q->head = newHead;                      // newHead 가 새 더미가 된다
    pthread_mutex_unlock(&q->headLock);
    free(tmp);                              // 옛 더미는 락 밖에서 free
    return 0;
}
```

- **락 두 개**: enqueue는 tail 락만, dequeue는 head 락만 → **생산자와 소비자가 동시에** 진행.
- **더미 노드의 역할**: head는 항상 "이미 꺼낸" 더미를 가리키고, 진짜 첫 원소는 `head->next`. 그래서 큐가 비어 있을 때도 head와 tail이 **서로 다른 필드를 쓴다**: enqueue는 `tail->next` 와 `tail` 을, dequeue는 `head` 만 바꾼다. 더미가 없으면 원소가 0→1, 1→0으로 바뀔 때 head와 tail을 둘 다 고쳐야 해서 두 락을 다 잡아야 한다.
- 원소가 1개일 때 enqueue가 `tail->next` 를 쓰고 dequeue가 `head->next` 를 읽는데, 이건 **같은 노드의 같은 필드**일 수 있다. 원 논문은 이 지점을 메모리 순서까지 고려해 논증한다 — 엄밀히 C11 메모리 모델로 따지면 `next` 를 원자 변수로 두는 것이 정석이다(이 노트의 실험 코드는 책을 그대로 따랐고, 1,000,000개 합계 검증은 통과했다).

```svg
<svg viewBox="0 0 700 250" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C29-arrow3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Michael &amp; Scott 2-락 큐: 더미 노드가 head 와 tail 을 떼어 놓는다</text>
  <rect x="60" y="90" width="110" height="50" fill="none" stroke="currentColor" stroke-dasharray="5,4"/>
  <text x="115" y="112" text-anchor="middle" fill="currentColor">dummy</text>
  <text x="115" y="130" text-anchor="middle" fill="currentColor">(이미 꺼낸 것)</text>
  <rect x="230" y="90" width="110" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="285" y="120" text-anchor="middle" fill="currentColor">value 7</text>
  <rect x="400" y="90" width="110" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="455" y="120" text-anchor="middle" fill="currentColor">value 9</text>
  <rect x="570" y="90" width="110" height="50" fill="none" stroke="currentColor" stroke-dasharray="2,3"/>
  <text x="625" y="120" text-anchor="middle" fill="currentColor">new (enq)</text>
  <line x1="170" y1="115" x2="228" y2="115" stroke="currentColor" marker-end="url(#C29-arrow3)"/>
  <line x1="340" y1="115" x2="398" y2="115" stroke="currentColor" marker-end="url(#C29-arrow3)"/>
  <line x1="510" y1="115" x2="568" y2="115" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#C29-arrow3)"/>
  <rect x="60" y="40" width="110" height="30" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="115" y="60" text-anchor="middle" fill="currentColor">head + headLock</text>
  <line x1="115" y1="70" x2="115" y2="88" stroke="currentColor" marker-end="url(#C29-arrow3)"/>
  <rect x="400" y="40" width="110" height="30" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="455" y="60" text-anchor="middle" fill="currentColor">tail + tailLock</text>
  <line x1="455" y1="70" x2="455" y2="88" stroke="currentColor" marker-end="url(#C29-arrow3)"/>
  <text x="200" y="180" text-anchor="middle" fill="currentColor">dequeue: head 만 한 칸 전진</text>
  <text x="200" y="198" text-anchor="middle" fill="currentColor">(7 을 꺼내고 그 노드가 새 dummy)</text>
  <text x="520" y="180" text-anchor="middle" fill="currentColor">enqueue: tail-&gt;next, tail 만 변경</text>
  <text x="350" y="235" text-anchor="middle" fill="currentColor">두 연산이 서로 다른 락 → 생산자 · 소비자가 동시에 진행</text>
</svg>
```

이 Mac 실측 (§7.2 실험 3, 생산자 2 × 50만 + 소비자 2): 큰 락 1개 0.079 s, **2-락 큐 0.053 s (약 1.5배 빠름)**, 두 경우 모두 꺼낸 값 합계 500,000,500,000 = 1부터 1,000,000까지의 합과 일치 (유실·중복 없음).

락만 쓰는 이 큐는 실제 멀티스레드 프로그램의 요구를 다 채우진 못한다. **큐가 비었거나 가득 찼을 때 기다리는** 유한 큐(bounded buffer)는 [Ch.30](2026-09-30_C30_condition_variables.md)의 주제다. (실험 코드의 소비자는 비었을 때 계속 재시도하며 돈다 — 바로 CV가 필요한 상황.)

## 5. 동시 해시 테이블 (29.4)

리사이즈하지 않는 단순한 해시 테이블. 앞에서 만든 동시 리스트를 버킷으로 쓴다 (Figure 29.10):

```c
#define BUCKETS (101)
typedef struct __hash_t { list_t lists[BUCKETS]; } hash_t;

void Hash_Init(hash_t *H) {
    for (int i = 0; i < BUCKETS; i++)
        List_Init(&H->lists[i]);
}
int Hash_Insert(hash_t *H, int key) {
    return List_Insert(&H->lists[key % BUCKETS], key);
}
int Hash_Lookup(hash_t *H, int key) {
    return List_Lookup(&H->lists[key % BUCKETS], key);
}
```

**버킷마다 락이 하나**(각 리스트의 락)이므로 서로 다른 버킷에 접근하는 연산은 동시에 진행된다. 책의 Figure 29.11(4 스레드, 스레드당 1만–5만 insert): 해시 테이블은 "훌륭하게" 확장되고 단일 락 리스트는 그렇지 않다.

이 Mac 실측 (§7.2 실험 1):

```text
      N   single-lock list(s)   hash, 101 locks(s)   speedup
  10000                0.0020               0.0006      3.5x
  50000                0.0075               0.0028      2.6x
```

해시가 2.6–3.5배 빠르다. 4 스레드인데 4배가 안 나오는 이유: insert 비용의 상당 부분이 **malloc**(락 밖이지만 할당기 내부 경쟁과 메모리 대역폭)이고, 버킷 101개 각각의 락·헤드가 캐시라인을 공유하는 false sharing도 일부 있다 (`list_t` 가 72B라 버킷 2개 정도가 한 128B 라인에 걸침).

> **TIP — AVOID PREMATURE OPTIMIZATION (KNUTH'S LAW)** — 처음엔 **큰 락 하나**로 시작하라. 거의 확실히 정확하다. 성능 문제가 확인되면 그때 다듬어라. "Premature optimization is the root of all evil." Sun OS와 리눅스도 멀티프로세서로 갈 때 처음엔 락 하나를 썼다. 리눅스에선 이름까지 있었다: **BKL(Big Kernel Lock)**. 멀티 CPU가 보편화되자 커널 안에 한 번에 한 스레드만 있는 게 병목이 되었고, 리눅스는 "락 하나를 여러 개로" 바꾸는 직진형 길을, Sun은 처음부터 동시성을 품은 새 OS(**Solaris**)를 만드는 급진적 길을 택했다.

## 6. 요약 (29.5)

- 카운터부터 리스트, 큐, 그리고 어디서나 쓰이는 해시 테이블까지 맛보았다.
- 교훈: (1) 제어 흐름 변화(early return, 에러 경로) 주변에서 락 획득/해제에 주의, (2) 동시성을 높인다고 반드시 빨라지지 않는다, (3) 성능 문제는 **존재할 때만** 고친다. 애플리케이션 전체 성능을 개선하지 않는 최적화는 가치가 없다.
- 더 볼 것: Moir & Shavit의 서베이 [MS04], B-tree 같은 구조(DB 수업), 그리고 전통적 락을 아예 쓰지 않는 **논블로킹(non-blocking)** 자료구조 ([Ch.32](2026-09-30_C32_concurrency_bugs.md)에서 맛보기).

```text
자료구조별 락 전략 정리
───────────────────────────────────────────────────────────────────────
구조      기본(큰 락)            확장 버전                      핵심 아이디어
카운터    mutex 하나             sloppy counter (코어별 로컬)    정확도를 성능과 맞바꿈
리스트    mutex 하나             hand-over-hand (노드별 락)     실전에선 대개 더 느림
큐        mutex 하나             Michael & Scott (head/tail 락)  더미 노드로 양 끝 분리
해시      mutex 하나             버킷별 락                       서로 다른 키는 서로 다른 락
```

## 7. 직접 해보기

이 장은 OSTEP 공식 시뮬레이터 숙제가 없다(`.tools/ostep-homework` 에 해당 디렉터리 없음). 대신 책이 "직접 돌려 보라"고 한 세 가지(sloppy counter, M&S 큐, 해시 vs 리스트)를 C로 만들어 측정했다.

### 7.1 precise vs atomic vs sloppy 카운터 — C29_sloppy_counter.c

```c
// C29_sloppy_counter.c — OSTEP Fig 29.2(precise) / 29.5(sloppy) 카운터를 Apple M2 에서 측정
//  실험 1: 스레드 수 1,2,4,8 — 각 스레드 1,000,000 회 증가. precise vs atomic vs sloppy(S=1024)
//  실험 2: 4 스레드, S(sloppiness) = 1 … 1024 스윕 (Fig 29.6 재현)
//  실험 3: sloppy 의 로컬 카운터를 캐시라인(128B)에 따로 두지 않으면? (false sharing)
// build: cc -Wall -Wextra -O2 -pthread code/C29_sloppy_counter.c -o .work/bin/C29_sloppy_counter
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUMCPUS 8
#define LOOPS 1000000

// ---------------- precise: 락 하나 (Fig 29.2)
typedef struct { int value; pthread_mutex_t lock; } precise_t;
static precise_t P;
static void precise_inc(void) {
    pthread_mutex_lock(&P.lock);
    P.value++;
    pthread_mutex_unlock(&P.lock);
}

// ---------------- atomic: 락 없이 하드웨어 fetch-and-add 1개
static atomic_int A;

// ---------------- sloppy (Fig 29.5). PADDED=1 이면 로컬 슬롯마다 캐시라인 하나씩
typedef struct {
    _Alignas(128) int value;
    pthread_mutex_t lock;
} padded_local_t;
typedef struct {
    int value;
    pthread_mutex_t lock;
} packed_local_t;

typedef struct {
    int global;
    pthread_mutex_t glock;
    padded_local_t local[NUMCPUS];
    int threshold;
} sloppy_t;
typedef struct {
    int global;
    pthread_mutex_t glock;
    packed_local_t local[NUMCPUS];      // 8 슬롯이 몇 개 캐시라인에 다닥다닥
    int threshold;
} sloppy_packed_t;

static sloppy_t S;
static sloppy_packed_t SP;

#define SLOPPY_UPDATE(C, tid, amt)                                  \
    do {                                                            \
        int cpu = (tid) % NUMCPUS;                                  \
        pthread_mutex_lock(&(C).local[cpu].lock);                   \
        (C).local[cpu].value += (amt);                              \
        if ((C).local[cpu].value >= (C).threshold) {                \
            pthread_mutex_lock(&(C).glock);                         \
            (C).global += (C).local[cpu].value;                     \
            pthread_mutex_unlock(&(C).glock);                       \
            (C).local[cpu].value = 0;                               \
        }                                                           \
        pthread_mutex_unlock(&(C).local[cpu].lock);                 \
    } while (0)

static int sloppy_get(void) {          // 근사값: global 만 본다
    pthread_mutex_lock(&S.glock);
    int v = S.global;
    pthread_mutex_unlock(&S.glock);
    return v;
}

static void counters_init(int threshold) {
    P.value = 0;
    pthread_mutex_init(&P.lock, NULL);
    atomic_store(&A, 0);
    S.global = 0; S.threshold = threshold;
    SP.global = 0; SP.threshold = threshold;
    pthread_mutex_init(&S.glock, NULL);
    pthread_mutex_init(&SP.glock, NULL);
    for (int i = 0; i < NUMCPUS; i++) {
        S.local[i].value = 0;  pthread_mutex_init(&S.local[i].lock, NULL);
        SP.local[i].value = 0; pthread_mutex_init(&SP.local[i].lock, NULL);
    }
}

enum { PRECISE, ATOMIC, SLOPPY, SLOPPY_PACKED };
static int kind;
static atomic_int go;

static void *worker(void *arg) {
    int tid = (int)(long)arg;
    while (!atomic_load(&go)) ;
    for (int i = 0; i < LOOPS; i++) {
        switch (kind) {
        case PRECISE: precise_inc(); break;
        case ATOMIC:  atomic_fetch_add_explicit(&A, 1, memory_order_relaxed); break;
        case SLOPPY:  SLOPPY_UPDATE(S, tid, 1); break;
        default:      SLOPPY_UPDATE(SP, tid, 1); break;
        }
    }
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 한 번 돌리고 걸린 시간(초)을 돌려준다. *final 에 최종 값.
static double run(int k, int nthreads, int threshold, int *final) {
    pthread_t t[NUMCPUS];
    counters_init(threshold);
    kind = k;
    atomic_store(&go, 0);
    for (long i = 0; i < nthreads; i++) pthread_create(&t[i], NULL, worker, (void *)i);
    double t0 = now();
    atomic_store(&go, 1);
    for (int i = 0; i < nthreads; i++) pthread_join(t[i], NULL);
    double dt = now() - t0;
    if (k == PRECISE) *final = P.value;
    else if (k == ATOMIC) *final = atomic_load(&A);
    else if (k == SLOPPY) *final = sloppy_get();
    else *final = SP.global;
    return dt;
}

int main(void) {
    int v;
    run(PRECISE, 1, 1, &v);            // 워밍업

    printf("== 실험 1: 스레드당 %d 회 증가, 걸린 시간 (초)\n", LOOPS);
    printf("threads   precise(mutex)   atomic(ldadd)   sloppy(S=1024)   sloppy get() / 진짜 합\n");
    for (int n = 1; n <= 8; n *= 2) {
        double tp = run(PRECISE, n, 1024, &v);
        double ta = run(ATOMIC, n, 1024, &v);
        double ts = run(SLOPPY, n, 1024, &v);
        printf("%7d   %14.3f   %13.3f   %14.3f   %d / %d\n", n, tp, ta, ts, v, n * LOOPS);
    }

    printf("\n== 실험 2: 4 스레드, sloppiness S 스윕 (Fig 29.6)\n");
    printf("     S    time(s)   get()       오차(진짜 4000000 - get)\n");
    for (int s = 1; s <= 1024; s *= 2) {
        double ts = run(SLOPPY, 4, s, &v);
        printf("%6d   %8.3f   %8d   %8d\n", s, ts, v, 4 * LOOPS - v);
    }

    printf("\n== 실험 3: 4 스레드, S=1024, 로컬 카운터 배치에 따른 차이\n");
    double a = run(SLOPPY, 4, 1024, &v);
    double b = run(SLOPPY_PACKED, 4, 1024, &v);
    printf("padded (슬롯당 128B 캐시라인) : %.3f s\n", a);
    printf("packed (sizeof slot = %zu B)  : %.3f s   → %.1fx 느림\n",
           sizeof(packed_local_t), b, b / a);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O2 -pthread code/C29_sloppy_counter.c -o .work/bin/C29_sloppy_counter && .work/bin/C29_sloppy_counter
== 실험 1: 스레드당 1000000 회 증가, 걸린 시간 (초)
threads   precise(mutex)   atomic(ldadd)   sloppy(S=1024)   sloppy get() / 진짜 합
      1            0.005           0.002            0.006   999424 / 1000000
      2            0.020           0.008            0.006   1998848 / 2000000
      4            0.186           0.031            0.006   3997696 / 4000000
      8            0.204           0.320            0.012   7995392 / 8000000

== 실험 2: 4 스레드, sloppiness S 스윕 (Fig 29.6)
     S    time(s)   get()       오차(진짜 4000000 - get)
     1      0.195    4000000          0
     2      0.121    4000000          0
     4      0.065    4000000          0
     8      0.047    4000000          0
    16      0.015    4000000          0
    32      0.010    4000000          0
    64      0.008    4000000          0
   128      0.011    3999744        256
   256      0.010    3999744        256
   512      0.010    3999744        256
  1024      0.008    3997696       2304

== 실험 3: 4 스레드, S=1024, 로컬 카운터 배치에 따른 차이
padded (슬롯당 128B 캐시라인) : 0.008 s
packed (sizeof slot = 72 B)  : 0.039 s   → 5.1x 느림
```

(벤치마크라 `-O2`. `-O0` 로도 경고 0 확인.) 해석:

- **실험 1**: precise(mutex)는 1→4 스레드에서 0.005 → 0.186 s (37배). sloppy(S=1024)는 0.006 → 0.006 s — **완벽한 확장**. 8 스레드에서 0.012 s로 조금 느려진 건 M2의 코어 8개 중 4개가 효율(E) 코어라서다.
- **atomic(ldadd)도 확장되지 않는다**: 8 스레드에서 0.320 s로 오히려 mutex(0.204)보다 느렸다. 락은 없지만 **모든 코어가 같은 캐시라인 하나**를 매번 독점해야 하기 때문이다. "락-프리 = 빠름"이 아니다. 확장성의 적은 락 자체보다 **공유 캐시라인에 대한 쓰기**다. (mutex가 이긴 건 macOS mutex의 first-fit 정책이 한 스레드에게 연속 획득을 몰아줘 캐시라인 이동이 줄어든 덕으로 보인다 — [Ch.28](2026-09-30_C28_locks.md) 실험 2의 handoff 1.1% 참고.)
- **실험 2 (Fig 29.6 재현)**: S=1에서 0.195 s(사실상 precise + 로컬 락 비용), S가 두 배가 될 때마다 거의 반으로 줄다가 S=32–64 부근에서 바닥(약 0.01 s). 오차는 §2.3의 계산(256, 2304)과 정확히 일치.
- **실험 3 (false sharing)**: 같은 알고리즘인데 로컬 슬롯을 캐시라인 정렬하지 않으면 **5.1배** 느리다. 책의 Figure 29.5 코드를 그대로 옮기면 이 함정에 빠진다.

### 7.2 리스트 · hand-over-hand · M&S 큐 · 해시 — C29_list_queue_hash.c

```c
// C29_list_queue_hash.c — OSTEP Ch.29 의 리스트 · 큐 · 해시를 직접 만들어 재 본다.
//  실험 1 (Fig 29.11): 4 스레드가 각자 N 개 insert. 단일 락 리스트 vs 버킷별 락 해시(101 버킷)
//  실험 2: 1000 노드 리스트에서 4 스레드 lookup. 단일 락 vs hand-over-hand(노드별 락)
//  실험 3 (Fig 29.9): Michael & Scott 2-락 큐 vs 큰 락 1개 큐. 생산자 2 + 소비자 2, 합계 검증
// build: cc -Wall -Wextra -O2 -pthread code/C29_list_queue_hash.c -o .work/bin/C29_list_queue_hash
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// =============================================================== 리스트 (Fig 29.8)
typedef struct node { int key; struct node *next; } node_t;
typedef struct { node_t *head; pthread_mutex_t lock; } list_t;

static void List_Init(list_t *L) { L->head = NULL; pthread_mutex_init(&L->lock, NULL); }

static int List_Insert(list_t *L, int key) {
    node_t *new = malloc(sizeof(*new));          // malloc 은 락 밖에서 (스레드 안전)
    if (new == NULL) { perror("malloc"); return -1; }
    new->key = key;
    pthread_mutex_lock(&L->lock);                // 진짜 임계 구역만 잠근다
    new->next = L->head;
    L->head = new;
    pthread_mutex_unlock(&L->lock);
    return 0;
}

static int List_Lookup(list_t *L, int key) {
    int rv = -1;
    pthread_mutex_lock(&L->lock);
    for (node_t *c = L->head; c; c = c->next)
        if (c->key == key) { rv = 0; break; }    // return 대신 break → 출구 하나
    pthread_mutex_unlock(&L->lock);
    return rv;
}

static long List_Count(list_t *L) {
    long n = 0;
    for (node_t *c = L->head; c; c = c->next) n++;
    return n;
}

// =============================================================== 해시 (Fig 29.10)
#define BUCKETS 101
typedef struct { list_t lists[BUCKETS]; } hash_t;
static void Hash_Init(hash_t *H) { for (int i = 0; i < BUCKETS; i++) List_Init(&H->lists[i]); }
static int Hash_Insert(hash_t *H, int key) { return List_Insert(&H->lists[key % BUCKETS], key); }

// =============================================================== hand-over-hand 리스트
typedef struct hnode { int key; struct hnode *next; pthread_mutex_t lock; } hnode_t;
typedef struct { hnode_t *head; pthread_mutex_t lock; } hlist_t;   // lock: head 포인터 보호

static int HList_Lookup(hlist_t *L, int key) {
    pthread_mutex_lock(&L->lock);
    hnode_t *c = L->head;
    if (!c) { pthread_mutex_unlock(&L->lock); return -1; }
    pthread_mutex_lock(&c->lock);                // 다음 것 잡고
    pthread_mutex_unlock(&L->lock);              // 이전 것 놓기 (손 바꿔 잡기)
    while (c) {
        if (c->key == key) { pthread_mutex_unlock(&c->lock); return 0; }
        hnode_t *n = c->next;
        if (n) pthread_mutex_lock(&n->lock);
        pthread_mutex_unlock(&c->lock);
        c = n;
    }
    return -1;
}

// =============================================================== M&S 2-락 큐 (Fig 29.9)
typedef struct qnode { int value; struct qnode *next; } qnode_t;
typedef struct {
    qnode_t *head, *tail;
    pthread_mutex_t headLock, tailLock;
} queue_t;

static void Queue_Init(queue_t *q) {
    qnode_t *tmp = malloc(sizeof(*tmp));         // 더미 노드: head 와 tail 을 떼어 놓는 장치
    assert(tmp);
    tmp->next = NULL;
    q->head = q->tail = tmp;
    pthread_mutex_init(&q->headLock, NULL);
    pthread_mutex_init(&q->tailLock, NULL);
}
static void Queue_Enqueue(queue_t *q, int value) {
    qnode_t *tmp = malloc(sizeof(*tmp));
    assert(tmp);
    tmp->value = value;
    tmp->next = NULL;
    pthread_mutex_lock(&q->tailLock);
    q->tail->next = tmp;
    q->tail = tmp;
    pthread_mutex_unlock(&q->tailLock);
}
static int Queue_Dequeue(queue_t *q, int *value) {
    pthread_mutex_lock(&q->headLock);
    qnode_t *tmp = q->head;
    qnode_t *newHead = tmp->next;
    if (newHead == NULL) { pthread_mutex_unlock(&q->headLock); return -1; }
    *value = newHead->value;
    q->head = newHead;                           // newHead 가 새 더미가 된다
    pthread_mutex_unlock(&q->headLock);
    free(tmp);
    return 0;
}

// 비교용: 큰 락 하나 큐 (head/tail 둘 다 같은 락)
static int one_lock_mode;
static pthread_mutex_t bigLock = PTHREAD_MUTEX_INITIALIZER;
static void Q_Enq(queue_t *q, int v) {
    if (!one_lock_mode) { Queue_Enqueue(q, v); return; }
    qnode_t *tmp = malloc(sizeof(*tmp)); assert(tmp);
    tmp->value = v; tmp->next = NULL;
    pthread_mutex_lock(&bigLock);
    q->tail->next = tmp; q->tail = tmp;
    pthread_mutex_unlock(&bigLock);
}
static int Q_Deq(queue_t *q, int *v) {
    if (!one_lock_mode) return Queue_Dequeue(q, v);
    pthread_mutex_lock(&bigLock);
    qnode_t *tmp = q->head, *nh = tmp->next;
    if (!nh) { pthread_mutex_unlock(&bigLock); return -1; }
    *v = nh->value; q->head = nh;
    pthread_mutex_unlock(&bigLock);
    free(tmp);
    return 0;
}

// =============================================================== 워커들
#define NT 4
static list_t gL;
static hash_t gH;
static hlist_t gHL;
static int per_thread;
static int use_hash;
static atomic_int go;

static void *insert_worker(void *arg) {
    int base = (int)(long)arg * per_thread;
    while (!atomic_load(&go)) ;
    for (int i = 0; i < per_thread; i++) {
        if (use_hash) Hash_Insert(&gH, base + i);
        else List_Insert(&gL, base + i);
    }
    return NULL;
}

#define LIST_N 1000
#define LOOKUPS 20000
static int use_hoh;
static void *lookup_worker(void *arg) {
    unsigned seed = (unsigned)(long)arg + 1;
    int found = 0;
    while (!atomic_load(&go)) ;
    for (int i = 0; i < LOOKUPS; i++) {
        int key = (int)(rand_r(&seed) % LIST_N);
        found += (use_hoh ? HList_Lookup(&gHL, key) : List_Lookup(&gL, key)) == 0;
    }
    return (void *)(long)found;
}

#define QITEMS 500000
static queue_t gQ;
static atomic_long consumed_sum, consumed_cnt;
static atomic_int producers_done;
static void *producer(void *arg) {
    int base = (int)(long)arg * QITEMS;
    while (!atomic_load(&go)) ;
    for (int i = 1; i <= QITEMS; i++) Q_Enq(&gQ, base + i);
    atomic_fetch_add(&producers_done, 1);
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    long sum = 0, cnt = 0;
    int v;
    while (!atomic_load(&go)) ;
    for (;;) {
        int done = atomic_load(&producers_done) == 2;  // 먼저 "생산 끝"을 확인하고
        if (Q_Deq(&gQ, &v) == 0) { sum += v; cnt++; }
        else if (done) break;                          // 그 뒤에도 비었으면 진짜 끝
    }
    atomic_fetch_add(&consumed_sum, sum);
    atomic_fetch_add(&consumed_cnt, cnt);
    return NULL;
}

static double run_threads(void *(*f)(void *), int n, long *ret_sum) {
    pthread_t t[NT];
    atomic_store(&go, 0);
    for (long i = 0; i < n; i++) pthread_create(&t[i], NULL, f, (void *)i);
    double t0 = now();
    atomic_store(&go, 1);
    long s = 0;
    for (int i = 0; i < n; i++) { void *r; pthread_join(t[i], &r); s += (long)r; }
    if (ret_sum) *ret_sum = s;
    return now() - t0;
}

int main(void) {
    // ---- 실험 1
    printf("== 실험 1: %d 스레드 동시 insert (스레드당 N 개)\n", NT);
    printf("      N   single-lock list(s)   hash, 101 locks(s)   speedup\n");
    for (int n = 10000; n <= 50000; n += 10000) {
        per_thread = n;
        List_Init(&gL); use_hash = 0;
        double tl = run_threads(insert_worker, NT, NULL);
        long cl = List_Count(&gL);
        Hash_Init(&gH); use_hash = 1;
        double th = run_threads(insert_worker, NT, NULL);
        long ch = 0;
        for (int b = 0; b < BUCKETS; b++) ch += List_Count(&gH.lists[b]);
        assert(cl == (long)NT * n && ch == (long)NT * n);
        printf("%7d   %19.4f   %18.4f   %6.1fx\n", n, tl, th, tl / th);
    }

    // ---- 실험 2
    List_Init(&gL);
    for (int k = LIST_N - 1; k >= 0; k--) List_Insert(&gL, k);
    pthread_mutex_init(&gHL.lock, NULL);
    gHL.head = NULL;
    for (int k = LIST_N - 1; k >= 0; k--) {
        hnode_t *h = malloc(sizeof(*h)); assert(h);
        h->key = k; h->next = gHL.head; pthread_mutex_init(&h->lock, NULL);
        gHL.head = h;
    }
    printf("\n== 실험 2: 노드 %d 개 리스트, %d 스레드가 각 %d 회 lookup\n", LIST_N, NT, LOOKUPS);
    long f1, f2;
    use_hoh = 0; double ts = run_threads(lookup_worker, NT, &f1);
    use_hoh = 1; double th = run_threads(lookup_worker, NT, &f2);
    printf("single lock   : %.3f s  (found %ld)\n", ts, f1);
    printf("hand-over-hand: %.3f s  (found %ld)  → %.1fx %s\n", th, f2,
           th > ts ? th / ts : ts / th, th > ts ? "느림" : "빠름");

    // ---- 실험 3
    printf("\n== 실험 3: 큐, 생산자 2 (각 %d 개) + 소비자 2\n", QITEMS);
    long expect = 0;
    for (long p = 0; p < 2; p++) for (long i = 1; i <= QITEMS; i++) expect += p * QITEMS + i;
    for (one_lock_mode = 1; one_lock_mode >= 0; one_lock_mode--) {
        Queue_Init(&gQ);
        atomic_store(&consumed_sum, 0); atomic_store(&consumed_cnt, 0);
        atomic_store(&producers_done, 0);
        pthread_t t[4];
        atomic_store(&go, 0);
        pthread_create(&t[0], NULL, producer, (void *)0L);
        pthread_create(&t[1], NULL, producer, (void *)1L);
        pthread_create(&t[2], NULL, consumer, NULL);
        pthread_create(&t[3], NULL, consumer, NULL);
        double t0 = now();
        atomic_store(&go, 1);
        for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
        double dt = now() - t0;
        printf("%-22s: %.3f s  items=%ld sum=%ld %s\n",
               one_lock_mode ? "one big lock" : "Michael&Scott 2-lock", dt,
               atomic_load(&consumed_cnt), atomic_load(&consumed_sum),
               atomic_load(&consumed_sum) == expect ? "(OK)" : "(MISMATCH!)");
    }
    return 0;
}
```

```text
$ cc -Wall -Wextra -O2 -pthread code/C29_list_queue_hash.c -o .work/bin/C29_list_queue_hash && .work/bin/C29_list_queue_hash
== 실험 1: 4 스레드 동시 insert (스레드당 N 개)
      N   single-lock list(s)   hash, 101 locks(s)   speedup
  10000                0.0020               0.0006      3.5x
  20000                0.0034               0.0010      3.4x
  30000                0.0049               0.0015      3.2x
  40000                0.0057               0.0019      2.9x
  50000                0.0075               0.0028      2.6x

== 실험 2: 노드 1000 개 리스트, 4 스레드가 각 20000 회 lookup
single lock   : 0.100 s  (found 80000)
hand-over-hand: 0.465 s  (found 80000)  → 4.6x 느림

== 실험 3: 큐, 생산자 2 (각 500000 개) + 소비자 2
one big lock          : 0.079 s  items=1000000 sum=500000500000 (OK)
Michael&Scott 2-lock  : 0.053 s  items=1000000 sum=500000500000 (OK)
```

해석:

- **실험 1 (Fig 29.11)**: 버킷별 락 해시가 단일 락 리스트보다 2.6–3.5배 빠르다. 모든 노드 수 검증(`assert`) 통과.
- **실험 2**: hand-over-hand가 **4.6배 느리다** — 책의 "실제로는 단일 락보다 빠르게 만들기 어렵다"를 확인. 1000 노드를 순회하면 lookup 하나에 평균 500노드 × (lock + unlock) = 약 1000번의 원자 연산. 단일 락은 2번.
- **실험 3**: M&S 2-락 큐가 큰 락보다 약 1.5배 빠르고, 합계 검증(500,000,500,000) 통과 → 유실·중복 없음.
- 실험 코드 작성 중 실제로 겪은 버그: 처음엔 소비자 종료 조건을 `producers_done == 2 && gQ.head->next == NULL` 로 썼는데, **락 없이 `gQ.head` 를 읽는 사이 다른 소비자가 그 노드를 free할 수 있다**(use-after-free). "생산 끝"을 먼저 확인하고, 그 **이후의 dequeue가 비었을 때만** 종료하도록 고쳤다 — 락 밖에서 공유 포인터를 따라가면 안 된다는 이 장의 교훈 그대로다.

## 8. 펌웨어 엔지니어의 눈으로

- **sloppy counter = SSD FW의 코어별 통계 카운터.** SMART 속성(호스트 read/write 섹터 수, NAND program/erase 횟수)을 매 I/O마다 전역 변수에 원자적으로 더하면 멀티코어 컨트롤러에서 캐시라인이 코어 사이를 튄다. 보통 코어별(또는 채널별) 로컬 카운터를 두고 주기적으로(타이머나 일정 개수마다) 합산한다. 호스트가 SMART 로그를 읽을 때만 정확히 합치면 된다 — 책의 "정확한 값이 필요하면 전부 합산"과 같다. 리눅스 `percpu_counter`, 블록 계층의 per-CPU I/O 통계도 같은 구조.
- **NVMe의 코어별 SQ/CQ 쌍**은 "카운터를 코어별로 쪼갠다"의 큐 버전이다. 리눅스 blk-mq는 CPU마다 하드웨어 큐를 매핑해서 submit 경로에서 **락 자체를 없앤다**. M&S 큐처럼 head(소비자 = 컨트롤러)와 tail(생산자 = 호스트)을 분리하고, 각 쪽 인덱스를 **한 주체만 쓰게** 한 doorbell 설계가 핵심이다.
- **false sharing은 DMA/캐시 일관성 버그와 같은 뿌리.** FW에서 DMA 버퍼와 CPU가 쓰는 변수가 같은 캐시라인에 있으면, 캐시 invalidate가 CPU 변수까지 날려 버리는 데이터 오염이 생긴다. 그래서 DMA 버퍼를 캐시라인 정렬한다. M2의 128B 라인(대부분 Arm/x86 코어는 64B)을 기억해 두자 — Apple Silicon에서 `_Alignas(64)` 로는 false sharing을 못 막는다.
- **FTL 매핑 캐시(L2P 캐시)의 동시성**: 논리 페이지 번호로 해싱한 버킷별 락(또는 영역별 락)이 일반적이다. 전역 락 하나로 시작했다가 IOPS 목표를 못 맞추면 쪼개는 진화 경로도 Knuth's Law 그대로다.
- **에러 경로의 unlock 누락**: FW에서 가장 흔한 데드락 원인 중 하나가 "NAND 읽기 에러 → 재시도 경로에서 early return → 채널 락 미해제"다. 단일 출구(`goto cleanup`) 패턴과 정적 분석(락 균형 검사)이 해법. 리눅스 커널의 sparse `__acquires/__releases` 어노테이션, clang의 `-Wthread-safety` 도 같은 목적이다.

## 9. 면접 질문

### Q1. 멀티코어에서 이벤트 카운터(예: 초당 처리 패킷 수)를 확장 가능하게 구현하라.
<details>
<summary>답 보기</summary>

- **per-CPU(또는 per-thread) 로컬 카운터** + 전역 합산. 증가 경로는 자기 로컬만 만진다 → 공유 쓰기 없음. 로컬은 **캐시라인 정렬**(false sharing 방지, M2는 128B).
- 읽기: (a) 근사로 충분하면 sloppy counter처럼 **임계값 S마다 전역에 이전**하고 전역만 읽음 (오차 ≤ CPU수 × S), (b) 정확해야 하면 읽을 때 모든 로컬을 합산 — 읽기는 느려지지만 쓰기가 압도적으로 많으면 이득.
- 로컬이 per-thread면 스레드 마이그레이션 문제가 없고 락 없이 relaxed atomic 로드/스토어만으로 가능하다.
- 실측 근거: M2에서 4 스레드 × 100만 증가: mutex 0.186 s, 단일 atomic 0.031 s, sloppy 0.006 s.

</details>

### Q2. 단일 `atomic_fetch_add` 카운터는 락이 없는데 왜 확장되지 않나?
<details>
<summary>답 보기</summary>

- 락은 없지만 **모든 코어가 같은 캐시라인에 쓴다.** 원자 RMW를 하려면 그 라인을 Exclusive/Modified 상태로 가져와야 하므로, 코어 수만큼 라인이 핑퐁한다. 처리량은 "라인 이동 지연"에 묶인다.
- 이 Mac 실측: 8 스레드에서 atomic 0.320 s, mutex 0.204 s — 오히려 락이 이겼다 (mutex가 한 스레드에게 연속 획득을 몰아줘 라인 이동이 적었음).
- 확장성의 적은 **공유 쓰기**다. 락-프리는 "블로킹이 없다"는 진행 보장이지, 빠르다는 보장이 아니다.

</details>

### Q3. Michael & Scott 2-락 큐에서 더미 노드는 왜 필요한가?
<details>
<summary>답 보기</summary>

- 더미 없이 head/tail을 실제 원소에 직접 두면, **원소 0↔1개 전이**에서 enqueue와 dequeue가 둘 다 head와 tail을 바꿔야 한다 → 두 락을 다 잡아야 하고 락 순서 문제까지 생긴다.
- 더미가 있으면 head는 항상 "이미 소비된" 노드를 가리키고 진짜 첫 원소는 `head->next`. **enqueue는 tail 쪽(`tail->next`, `tail`)만, dequeue는 `head` 만** 바꾸므로 각자 자기 락 하나로 충분하다. 빈 큐는 `head->next == NULL` 로 판단.
- 남는 미묘함: 원소가 1개일 때 enqueue의 `tail->next` 쓰기와 dequeue의 `head->next` 읽기가 같은 필드에 닿을 수 있다 → 엄밀하게는 `next` 를 원자 변수(release/acquire)로 둔다. 락-프리 버전(M&S 논블로킹 큐)은 head/tail을 CAS로 갱신한다.

</details>

### Q4. 노드별 락(hand-over-hand)이 큰 락 하나보다 느린 이유와, 그래도 동시성을 높이고 싶다면 어떤 대안이 있나?
<details>
<summary>답 보기</summary>

- 순회 중 **노드마다 lock + unlock** → 원자 연산 수가 노드 수에 비례. 각각이 캐시라인 소유권 이동을 일으킨다. 단일 락은 연산당 2번. M2 실측 1000 노드: 4.6배 느림.
- 게다가 순회는 앞에서부터 순서대로 하므로 스레드들이 **줄지어** 따라가며 실제 병렬도는 낮다.
- 대안: (1) **읽기 위주면 reader-writer 락** 또는 **RCU**(읽기는 락 없이, 쓰기만 복사 후 교체 — 리눅스 커널 리스트), (2) **분할**: 해시처럼 여러 리스트로 쪼갬, (3) **낙관적 순회**: 락 없이 찾고 대상 노드만 잠근 뒤 유효성 재검증(lazy list), (4) 락-프리 리스트(Harris, CAS + 논리 삭제 표시).

</details>

### Q5. 처음 멀티코어용 자료구조를 설계할 때 어떤 순서로 접근하나?
<details>
<summary>답 보기</summary>

1. **큰 락 하나로 정확하게** 만든다 (Knuth's Law). 단위 테스트 + TSan.
2. **측정**: 경쟁이 실제 병목인지 프로파일(락 대기 시간, 캐시 미스). 아니면 끝.
3. 병목이면 **임계 구역 축소**(malloc 등 공유 아닌 작업을 락 밖으로, Fig 29.8).
4. 그래도 부족하면 **분할**: 데이터를 독립된 조각으로 나눠 조각마다 락(버킷별 해시, per-CPU 큐/카운터). 이때 **false sharing**(캐시라인 정렬)을 챙긴다.
5. 정확도를 양보할 수 있으면 **근사**(sloppy counter).
6. 최후에 락-프리/RCU — 복잡도와 검증 비용이 크므로 측정으로 정당화될 때만.

</details>

## 10. 자가 점검 & 숙제

### 퀴즈

Q1. 4 CPU, S=100인 sloppy counter. 각 CPU가 정확히 1,234번씩 증가시킨 직후 `get()` 이 돌려주는 값과 진짜 값의 차이는?

<details>
<summary>정답</summary>

각 로컬에 남는 값 = 1234 mod 100 = 34. 전역에 안 넘어간 총합 = 4 × 34 = **136**. 진짜 값 4936, `get()` = 4800. (상한은 4 × 99 = 396.)

</details>

Q2. Figure 29.7의 `List_Insert` 를 Figure 29.8처럼 바꾼 두 가지 이점은?

<details>
<summary>정답</summary>

(1) malloc과 노드 초기화를 락 밖으로 빼서 **임계 구역이 짧아짐**(동시성 ↑). (2) malloc 실패 경로가 락을 잡기 전이라 **실패 경로에 unlock이 필요 없음** → 드문 경로의 락 누수 버그 원천 차단.

</details>

Q3. 해시 테이블(버킷 101개, 버킷별 락)에서 모든 키가 101의 배수라면 성능은?

<details>
<summary>정답</summary>

모든 키가 `key % 101 == 0` 으로 **버킷 0 하나**에 몰린다 → 사실상 단일 락 리스트와 같다(오히려 약간 더 느림). 동시성은 해시 함수가 키를 고르게 퍼뜨린다는 가정에 달려 있다. 그래서 버킷 수를 소수로 하거나 더 좋은 해시 함수를 쓴다.

</details>

Q4. 이 장의 큐 실험에서 소비자가 큐가 빌 때마다 계속 `Dequeue` 를 재시도하며 도는 것의 문제점과 해결책은?

<details>
<summary>정답</summary>

비어 있는 동안 **CPU를 헛돌린다**(spin). 생산자가 느리면 코어 하나를 통째로 낭비하고, 단일 코어에선 생산자의 실행 기회까지 뺏는다. 해결: 큐가 비었으면 **조건 변수로 잠들고** 생산자가 enqueue 후 signal → [Ch.30](2026-09-30_C30_condition_variables.md)의 bounded buffer.

</details>

### 숙제 (책에 공식 Homework는 없음 — 본문이 "직접 돌려 보라"고 한 것 + 확장)

- **sloppy counter의 정확한 `get()` 구현**: 모든 로컬 락과 전역 락을 **정해진 순서로** 잡고 합산하는 `get_exact()` 를 추가하고, 업데이트 스레드가 도는 중에 호출해 비용을 재 보라 → 락 순서와 데드락([Ch.32](2026-09-30_C32_concurrency_bugs.md)) 연습.
- **hand-over-hand 하이브리드**: "k 노드마다 한 번만 락을 바꿔 잡는" 리스트를 만들어 k를 1, 8, 64로 바꿔 가며 §7.2 실험 2와 비교 → 책이 "연구해 볼 만하다"고 한 바로 그것.
- **리사이즈 가능한 해시 테이블**: 책이 독자에게 남긴 연습. 리사이즈 중에도 다른 버킷 연산을 허용하려면 무엇을 잠가야 하나? (힌트: 전역 reader-writer 락, 또는 버킷 단위 점진적 이전.)

## 11. 다음으로

- [Ch.30 조건 변수](2026-09-30_C30_condition_variables.md): 큐가 비었을 때/가득 찼을 때 **기다리는** bounded buffer.
- [Ch.31 세마포어](2026-09-30_C31_semaphores.md): 락과 CV를 하나로 묶은 프리미티브, reader-writer 락.
- [Ch.32 흔한 동시성 버그](2026-09-30_C32_concurrency_bugs.md): 여러 락을 쓰기 시작하면 만나는 데드락과 락 순서, 그리고 논블로킹 자료구조.
