# Ch.31 세마포어 — 정수 하나로 락과 조건 변수를 모두 표현하기

> 📖 원문: [31. Semaphores](../book-md/C31_semaphores.md) · [PDF p.361](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=361) · ⏱️ 읽기 약 55분 · 🔗 선행: [Ch.28](2026-09-30_C28_locks.md), [Ch.30](2026-09-30_C30_condition_variables.md)

## 0. 한눈에 보기

- **세마포어(semaphore)** = 정수 값 하나 + 대기 큐. 연산은 `sem_wait()`(값을 1 줄이고 음수면 잠듦)와 `sem_post()`(값을 1 늘리고 자는 사람이 있으면 하나 깨움) 둘뿐이다. Dijkstra 가 "동기화 도구는 이거 하나면 된다"며 만들었다.
- **초기값이 곧 의미**다: 1 이면 락(이진 세마포어), 0 이면 "누가 post 해 줄 때까지 기다려"(순서 맞추기/조건 변수 역할), N 이면 "자원 N 개".
- bounded buffer 는 `empty=MAX`, `full=0`, `mutex=1` 세 개로 푼다. 단, **mutex 는 empty/full 안쪽**에서 잡아야 한다. 바깥에서 잡으면 교착.
- reader-writer 락, 식사하는 철학자(→ 한 명만 포크 순서를 뒤집으면 순환 대기가 깨짐)도 세마포어로 푼다.
- 세마포어는 **락 + CV 로 쉽게 만들 수 있지만**(Zemaphore), 반대로 세마포어로 CV 를 만드는 건 의외로 어렵다. macOS 는 unnamed `sem_init()` 을 지원하지 않는다(ENOSYS) — 그래서 이 노트의 코드는 Zemaphore 와 `dispatch_semaphore` 로 돌렸다.

> **THE CRUX: HOW TO USE SEMAPHORES** — "How can we use semaphores instead of locks and condition variables? What is the definition of a semaphore? What is a binary semaphore? Is it straightforward to build a semaphore out of locks and condition variables? To build locks and condition variables out of semaphores?"
>
> → 락과 조건 변수 대신 세마포어를 어떻게 쓰는가? 세마포어의 정의는? 이진 세마포어란? 락과 CV 로 세마포어를 만드는 건 쉬운가? 반대로 세마포어로 락과 CV 를 만드는 건?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 세마포어(semaphore) | 정수 값 + 대기 큐를 가진 동기화 객체 | `sem_t s; sem_init(&s, 0, 1);` |
| sem_wait / P() | 값을 1 감소, 결과가 음수면 잠듦 | 네덜란드어 "probeer te verlagen"(내려 보기) |
| sem_post / V() | 값을 1 증가, 대기자가 있으면 하나 깨움 | "verhoog"(올리다) |
| 음수 값의 의미 | 값이 −k 이면 k 명이 대기 중 (Dijkstra 원래 정의) | 값 −2 = 두 스레드가 잠듦 |
| 이진 세마포어(binary semaphore) | 초기값 1, 락으로 사용 | 임계 구역 감싸기 |
| 카운팅 세마포어(counting semaphore) | 초기값 N, 자원 N 개 관리 | 빈 슬롯 수 `empty=MAX` |
| 순서 세마포어 | 초기값 0, "post 가 와야 통과" | 부모가 자식 종료 대기 |
| bounded buffer | empty/full/mutex 세 세마포어로 푸는 생산자-소비자 | 링 버퍼 |
| reader-writer 락 | 읽기는 여럿 동시, 쓰기는 혼자 | 리스트 조회 vs 삽입 |
| 기아(starvation) | 계속 밀려서 영원히 차례가 안 옴 | reader 가 끊이지 않으면 writer 굶음 |
| 식사하는 철학자 | 5명, 포크 5개, 양쪽 포크가 있어야 식사 | 순환 대기 교착의 교과서 예 |
| Zemaphore | 락 + CV + 정수로 만든 세마포어 (값이 음수로 안 내려감) | 책 Fig 31.16 |
| Hill's Law | "Big and dumb is better" — 단순한 게 빠를 때가 많다 | 직접 매핑 캐시 |

## 2. 세마포어의 정의 (31.1)

```c
#include <semaphore.h>
sem_t s;
sem_init(&s, 0, 1);   // 두 번째 인자 0 = 같은 프로세스의 스레드끼리 공유, 세 번째 = 초기값
```

동작 정의 (Fig 31.2, 실제로는 원자적으로 실행된다고 가정):

```c
int sem_wait(sem_t *s) {
    decrement the value of semaphore s by one
    wait if value of semaphore s is negative
}
int sem_post(sem_t *s) {
    increment the value of semaphore s by one
    if there are one or more threads waiting, wake one
}
```

세 가지 관찰:

1. `sem_wait()` 는 값이 1 이상이었으면 바로 리턴, 아니면 다음 post 까지 잠든다. 여러 스레드가 줄줄이 잠들 수 있다.
2. `sem_post()` 는 **기다리지 않는다**. 그냥 올리고, 자는 사람 있으면 하나 깨운다.
3. 값이 **음수면 그 절댓값 = 대기자 수** (Dijkstra 의 불변식). 사용자는 값을 볼 일이 없지만, 동작을 기억하는 데 좋다.

CV 와의 결정적 차이: **세마포어는 기억이 있다.** 아무도 안 잘 때 post 하면 값이 올라가 있고, 나중에 wait 하는 사람이 그걸 가져간다. Ch.30 의 lost wakeup 문제가 구조적으로 사라진다. (CV 는 기억이 없어서 상태 변수 `done` 이 따로 필요했다.)

```svg
<svg viewBox="0 0 700 250" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C31-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">이진 세마포어(초기값 1)에 스레드 3개가 몰릴 때 — 값과 대기 큐</text>
  <g>
    <rect x="20" y="45" width="120" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="80" y="66" text-anchor="middle" fill="currentColor">value = 1</text>
    <text x="80" y="84" text-anchor="middle" fill="currentColor">queue: (없음)</text>
    <text x="80" y="115" text-anchor="middle" fill="currentColor">초기</text>
  </g>
  <line x1="142" y1="70" x2="168" y2="70" stroke="currentColor" marker-end="url(#C31-arrow)"/>
  <g>
    <rect x="170" y="45" width="120" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="230" y="66" text-anchor="middle" fill="currentColor">value = 0</text>
    <text x="230" y="84" text-anchor="middle" fill="currentColor">queue: (없음)</text>
    <text x="230" y="115" text-anchor="middle" fill="currentColor">T0 wait → 통과</text>
  </g>
  <line x1="292" y1="70" x2="318" y2="70" stroke="currentColor" marker-end="url(#C31-arrow)"/>
  <g>
    <rect x="320" y="45" width="120" height="50" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
    <text x="380" y="66" text-anchor="middle" fill="currentColor">value = −2</text>
    <text x="380" y="84" text-anchor="middle" fill="currentColor">queue: T1, T2</text>
    <text x="380" y="115" text-anchor="middle" fill="currentColor">T1, T2 wait → 잠듦</text>
  </g>
  <line x1="442" y1="70" x2="468" y2="70" stroke="currentColor" marker-end="url(#C31-arrow)"/>
  <g>
    <rect x="470" y="45" width="100" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="520" y="66" text-anchor="middle" fill="currentColor">value = −1</text>
    <text x="520" y="84" text-anchor="middle" fill="currentColor">queue: T2</text>
    <text x="520" y="115" text-anchor="middle" fill="currentColor">T0 post → T1 깸</text>
  </g>
  <line x1="572" y1="70" x2="588" y2="70" stroke="currentColor" marker-end="url(#C31-arrow)"/>
  <g>
    <rect x="590" y="45" width="95" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="637" y="66" text-anchor="middle" fill="currentColor">value = 1</text>
    <text x="637" y="84" text-anchor="middle" fill="currentColor">queue: (없음)</text>
    <text x="637" y="115" text-anchor="middle" fill="currentColor">T1, T2 post 후</text>
  </g>
  <text x="20" y="160" fill="currentColor">규칙: wait = 먼저 1 빼고, 음수면 잠. post = 먼저 1 더하고, 대기자 있으면 1명 깨움.</text>
  <text x="20" y="182" fill="currentColor">불변식: value &lt; 0 이면 |value| = 대기 중인 스레드 수 (Dijkstra 정의).</text>
  <text x="20" y="204" fill="currentColor">Zemaphore / Linux 구현은 값을 0 아래로 내리지 않는다 → 대기자 수는 큐 길이로만 안다.</text>
  <text x="20" y="226" style="fill:var(--accent)">CV 와 달리 "post 가 먼저 와도 값으로 기억" → lost wakeup 이 없다.</text>
</svg>
```

## 3. 이진 세마포어 = 락 (31.2)

```c
sem_t m;
sem_init(&m, 0, X);   // X 는 얼마여야 할까?
sem_wait(&m);
// critical section
sem_post(&m);
```

정답 **X = 1**. 첫 스레드가 wait → 0 이 되고 0 은 음수가 아니므로 통과. 두 번째가 wait → −1 → 잠듦. 첫 스레드가 post → 0 으로 올리며 두 번째를 깨움. 두 번째가 끝나고 post → 1 로 복귀.

두 스레드 트레이스 (Fig 31.5):

```text
Value  Thread 0              State    Thread 1              State
  1                          Running                        Ready
  1    call sem_wait()       Running                        Ready
  0    sem_wait() returns    Running                        Ready
  0    (crit sect: begin)    Running                        Ready
  0    Interrupt; Switch→T1  Ready                          Running
  0                          Ready    call sem_wait()       Running
 -1                          Ready    decrement sem         Running
 -1                          Ready    (sem<0)→sleep         Sleeping
 -1                          Running  Switch→T0             Sleeping
 -1    (crit sect: end)      Running                        Sleeping
 -1    call sem_post()       Running                        Sleeping
  0    increment sem         Running                        Sleeping
  0    wake(T1)              Running                        Ready
  0    sem_post() returns    Running                        Ready
  0    Interrupt; Switch→T1  Ready                          Running
  0                          Ready    sem_wait() returns    Running
  0                          Ready    (crit sect)           Running
  0                          Ready    call sem_post()       Running
  1                          Ready    sem_post() returns    Running
```

**새 예제 — 세 명이 줄 서면?** T0 이 임계 구역 안(값 0)일 때 T1, T2 가 차례로 wait 하면 값은 −1, −2. T0 post → −1 (T1 깸), T1 post → 0 (T2 깸), T2 post → 1. 값이 최저 −2 까지 내려간 것 = 동시에 잠든 사람 2명.

락은 상태가 둘(잡힘/풀림)뿐이라 이렇게 쓰는 세마포어를 **이진 세마포어**라 부른다. 이진 용도만 쓴다면 일반 세마포어보다 더 단순하게 구현할 수 있다.

## 4. 세마포어로 순서 맞추기 = CV 역할 (31.3)

부모가 자식 종료를 기다리는 Fig 31.6:

```c
sem_t s;
void *child(void *arg) {
    printf("child\n");
    sem_post(&s);              // "끝났다"
    return NULL;
}
int main(int argc, char *argv[]) {
    sem_init(&s, 0, X);        // X 는?
    printf("parent: begin\n");
    pthread_t c;
    Pthread_create(&c, NULL, child, NULL);
    sem_wait(&s);              // 자식 대기
    printf("parent: end\n");
    return 0;
}
```

정답 **X = 0**. 두 경우 모두 맞다:

- **경우 1 (부모가 먼저 wait)**: 0 → −1, 부모 잠듦. 자식 post → 0, 부모 깸.
- **경우 2 (자식이 먼저 post)**: 0 → 1 (깨울 사람 없음, 하지만 **값으로 기억**). 부모 wait → 0, 안 자고 통과.

```text
경우 2 (Fig 31.8)
Value  Parent                    State    Child                         State
  0    create(Child)             Running  (Child exists; is runnable)   Ready
  0    Interrupt; Switch→Child   Ready    child runs                    Running
  0                              Ready    call sem_post()               Running
  1                              Ready    increment sem                 Running
  1                              Ready    wake(nobody)                  Running
  1                              Ready    sem_post() returns            Running
  1    parent runs               Running  Interrupt; Switch→Parent      Ready
  1    call sem_wait()           Running                                Ready
  0    decrement sem             Running                                Ready
  0    (sem≥0)→awake             Running                                Ready
  0    sem_wait() returns        Running                                Ready
```

**초기값 고르는 요령**: "시작하자마자 wait 없이 통과시켜 줄 수 있는 개수"를 세라. 락은 한 명 들여보낼 수 있으니 1. 순서 맞추기는 아직 아무 일도 안 일어났으니 0. 빈 버퍼 칸은 처음에 MAX 개 있으니 MAX.

## 5. 생산자/소비자(bounded buffer) (31.4)

### 5.1 첫 시도: empty 와 full (Fig 31.9, 31.10)

```c
int buffer[MAX];
int fill = 0, use = 0;
void put(int value) { buffer[fill] = value; fill = (fill + 1) % MAX; }  // f1, f2
int  get()          { int tmp = buffer[use]; use = (use + 1) % MAX; return tmp; }  // g1, g2

sem_t empty, full;
void *producer(void *arg) {
    for (int i = 0; i < loops; i++) {
        sem_wait(&empty);   // P1  빈 칸 하나 확보
        put(i);             // P2
        sem_post(&full);    // P3  찬 칸 하나 생김
    }
}
void *consumer(void *arg) {
    int tmp = 0;
    while (tmp != -1) {
        sem_wait(&full);    // C1
        tmp = get();        // C2
        sem_post(&empty);   // C3
        printf("%d\n", tmp);
    }
}
// main: sem_init(&empty, 0, MAX); sem_init(&full, 0, 0);
```

MAX=1, 생산자 1·소비자 1 이면 잘 동작한다. 소비자가 먼저 돌면 `full` 0 → −1 로 잠들고, 생산자가 `empty` 1 → 0 으로 통과해서 put 후 `full` 을 −1 → 0 으로 올리며 소비자를 깨운다.

**MAX > 1, 생산자 여럿이면 race**: Pa 가 `f1`(buffer[0] 에 씀) 직후 인터럽트, Pb 도 `f1` 에서 **같은 buffer[0]** 에 덮어씀 → 데이터 유실. `empty` 세마포어는 "빈 칸이 있다"만 보장할 뿐, **put() 자체의 상호 배제는 안 해 준다.**

### 5.2 상호 배제 추가 — 잘못된 위치 (Fig 31.11) → 교착

```c
void *producer(void *arg) {
    for (int i = 0; i < loops; i++) {
        sem_wait(&mutex);   // p0 (NEW)
        sem_wait(&empty);   // p1
        put(i);             // p2
        sem_post(&full);    // p3
        sem_post(&mutex);   // p4 (NEW)
    }
}
void *consumer(void *arg) {
    for (int i = 0; i < loops; i++) {
        sem_wait(&mutex);   // c0 (NEW)
        sem_wait(&full);    // c1
        int tmp = get();    // c2
        sem_post(&empty);   // c3
        sem_post(&mutex);   // c4 (NEW)
        printf("%d\n", tmp);
    }
}
```

소비자가 먼저: `mutex` 획득(c0) → `full` 에서 잠듦(c1) — **mutex 를 쥔 채로!** 생산자: `mutex` 에서 잠듦(p0). 소비자는 생산자의 `full` post 를, 생산자는 소비자의 `mutex` post 를 기다린다 → **순환 → 교착**.

```svg
<svg viewBox="0 0 700 230" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C31-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 31.11 의 교착: mutex 를 쥔 채 full 을 기다림</text>
  <rect x="40" y="80" width="150" height="60" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="115" y="105" text-anchor="middle" fill="currentColor">소비자</text>
  <text x="115" y="125" text-anchor="middle" fill="currentColor">c1: wait(full) 에서 잠</text>
  <rect x="510" y="80" width="150" height="60" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="585" y="105" text-anchor="middle" fill="currentColor">생산자</text>
  <text x="585" y="125" text-anchor="middle" fill="currentColor">p0: wait(mutex) 에서 잠</text>
  <ellipse cx="350" cy="55" rx="70" ry="22" fill="none" stroke="#d9534f" stroke-width="2"/>
  <text x="350" y="60" text-anchor="middle" fill="currentColor">mutex (=0)</text>
  <ellipse cx="350" cy="175" rx="70" ry="22" fill="none" stroke="#d9534f" stroke-width="2"/>
  <text x="350" y="180" text-anchor="middle" fill="currentColor">full (=0)</text>
  <line x1="190" y1="90" x2="282" y2="60" stroke="currentColor" marker-end="url(#C31-arrow2)"/>
  <text x="185" y="62" fill="currentColor">쥐고 있음</text>
  <line x1="510" y1="90" x2="420" y2="60" stroke="currentColor" stroke-dasharray="5,3" marker-end="url(#C31-arrow2)"/>
  <text x="440" y="95" fill="currentColor">원함</text>
  <line x1="190" y1="130" x2="282" y2="172" stroke="currentColor" stroke-dasharray="5,3" marker-end="url(#C31-arrow2)"/>
  <text x="200" y="168" fill="currentColor">원함</text>
  <line x1="510" y1="130" x2="420" y2="172" stroke="currentColor" marker-end="url(#C31-arrow2)"/>
  <text x="450" y="208" fill="currentColor">post 할 수 있는 건 생산자뿐</text>
  <text x="20" y="222" style="fill:var(--accent)" font-size="12">해법: mutex 를 empty/full 안쪽으로 → 잠들 때는 mutex 를 쥐고 있지 않게</text>
</svg>
```

### 5.3 정답 (Fig 31.12): 락 범위를 줄인다

```c
void *producer(void *arg) {
    for (int i = 0; i < loops; i++) {
        sem_wait(&empty);   // p1
        sem_wait(&mutex);   // p1.5  (MOVED MUTEX HERE...)
        put(i);             // p2
        sem_post(&mutex);   // p2.5  (... AND HERE)
        sem_post(&full);    // p3
    }
}
void *consumer(void *arg) {
    for (int i = 0; i < loops; i++) {
        sem_wait(&full);    // c1
        sem_wait(&mutex);   // c1.5
        int tmp = get();    // c2
        sem_post(&mutex);   // c2.5
        sem_post(&empty);   // c3
        printf("%d\n", tmp);
    }
}
// sem_init(&empty, 0, MAX); sem_init(&full, 0, 0); sem_init(&mutex, 0, 1);
```

핵심 규칙: **"잠들 수 있는 wait(empty/full)" 는 mutex 밖에서, "짧게 끝나는 put/get" 만 mutex 안에서.** 이게 일반 원칙 "락을 쥔 채로 다른 걸 기다리지 마라" (→ Ch.32 의 hold-and-wait) 의 구체적 예다.

**불변식으로 확인하기 (새 예제)**: 어느 순간이든 `empty + full + (진행 중인 put/get 수) = MAX`. MAX=4 에서 생산자가 3개를 넣고 소비자가 1개를 꺼낸 직후(진행 중 없음)라면 empty = 4−3+1 = 2, full = 3−1 = 2, 합 4. 생산자가 p1 을 지나 put 중이면 empty=1, full=2, 진행 중 1 → 합 4.

## 6. Reader-Writer 락 (31.5)

삽입은 리스트를 바꾸니 독점해야 하지만, 조회(lookup)끼리는 동시에 해도 된다. 그래서 **읽기는 여럿, 쓰기는 혼자** 인 락 [CHP71]:

```c
typedef struct _rwlock_t {
    sem_t lock;       // readers 카운터 보호용 이진 세마포어
    sem_t writelock;  // writer 한 명 또는 reader 여러 명
    int   readers;    // 임계 구역 안의 reader 수
} rwlock_t;

void rwlock_init(rwlock_t *rw) {
    rw->readers = 0;
    sem_init(&rw->lock, 0, 1);
    sem_init(&rw->writelock, 0, 1);
}
void rwlock_acquire_readlock(rwlock_t *rw) {
    sem_wait(&rw->lock);
    rw->readers++;
    if (rw->readers == 1)
        sem_wait(&rw->writelock);   // 첫 reader 가 writelock 획득
    sem_post(&rw->lock);
}
void rwlock_release_readlock(rwlock_t *rw) {
    sem_wait(&rw->lock);
    rw->readers--;
    if (rw->readers == 0)
        sem_post(&rw->writelock);   // 마지막 reader 가 반납
    sem_post(&rw->lock);
}
void rwlock_acquire_writelock(rwlock_t *rw) { sem_wait(&rw->writelock); }
void rwlock_release_writelock(rwlock_t *rw) { sem_post(&rw->writelock); }
```

아이디어: reader "집단"이 writelock 하나를 대표로 쥔다. 첫 reader 가 잡고, 마지막 reader 가 놓는다. 그 사이 들어오는 reader 들은 `readers++` 만 하고 바로 통과.

**문제: 공정성.** reader 가 끊임없이 들어오면 `readers` 가 0 이 되는 순간이 안 와서 **writer 가 굶는다**. 개선 힌트(원문): writer 가 대기 중이면 새 reader 진입을 막아라. 7.2 에서 "회전문(turnstile)" 세마포어 하나로 이걸 고치고 실제 대기 시간을 쟀다.

> **TIP — SIMPLE AND DUMB CAN BE BETTER (HILL'S LAW)**: 단순하고 멍청한 방법을 얕보지 마라. reader-writer 락은 멋져 보이지만 복잡하고, 복잡하면 느리다. 실제로 단순한 스핀락/뮤텍스보다 빠르지 않은 경우가 많다 [CB08]. Mark Hill 은 박사 논문에서 단순한 direct-mapped 캐시가 set-associative 보다 나은 경우를 보이며 "Big and dumb is better" 라고 요약했다.

## 7. 식사하는 철학자 (31.6)

원탁에 철학자 5명(P0..P4), 사이사이에 포크 5개(f0..f4). 생각(think)할 땐 포크가 필요 없고, 먹을(eat) 땐 **왼쪽과 오른쪽 포크 둘 다** 필요하다. 목표: 교착 없음, 기아 없음, 높은 동시성.

```c
while (1) { think(); getforks(); eat(); putforks(); }

int left(int p)  { return p; }
int right(int p) { return (p + 1) % 5; }
sem_t forks[5];   // 모두 1 로 초기화
```

**틀린 해법** (Fig 31.15): 모두 왼쪽 먼저, 그다음 오른쪽.

```c
void getforks() { sem_wait(forks[left(p)]); sem_wait(forks[right(p)]); }
void putforks() { sem_post(forks[left(p)]); sem_post(forks[right(p)]); }
```

모두가 동시에 왼쪽 포크를 잡으면 P0 은 f0, P1 은 f1 … P4 는 f4 를 쥐고, 각자 오른쪽(= 옆 사람의 왼쪽)을 영원히 기다린다.

**Dijkstra 의 해법 — 의존성 끊기**: 철학자 4 만 순서를 뒤집는다.

```c
void getforks() {
    if (p == 4) { sem_wait(forks[right(p)]); sem_wait(forks[left(p)]); }
    else        { sem_wait(forks[left(p)]);  sem_wait(forks[right(p)]); }
}
```

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C31-arrow3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="175" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">모두 왼쪽 먼저 → 순환</text>
  <text x="525" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">P4 만 오른쪽 먼저 → 순환 끊김</text>
  <circle cx="175" cy="160" r="60" fill="none" stroke="currentColor"/>
  <circle cx="525" cy="160" r="60" fill="none" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="275" y="165">P0</text><text x="206" y="66">P1</text><text x="94" y="102">P2</text><text x="94" y="228">P3</text><text x="206" y="264">P4</text>
    <text x="236" y="104">f1</text><text x="140" y="70">f2</text><text x="66" y="165">f3</text><text x="140" y="258">f4</text><text x="236" y="225">f0</text>
    <text x="625" y="165">P0</text><text x="556" y="66">P1</text><text x="444" y="102">P2</text><text x="444" y="228">P3</text><text x="556" y="264">P4</text>
    <text x="586" y="104">f1</text><text x="490" y="70">f2</text><text x="416" y="165">f3</text><text x="490" y="258">f4</text><text x="586" y="225">f0</text>
  </g>
  <path d="M262,150 Q255,120 240,112" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M200,74 Q170,70 150,72" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M96,112 Q80,135 74,152" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M100,236 Q120,252 132,256" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M215,255 Q232,245 238,234" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <text x="175" y="295" text-anchor="middle" fill="#d9534f">P0→f1(P1 소유)→…→P4→f0(P0 소유): 원</text>
  <path d="M612,150 Q605,120 590,112" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M550,74 Q520,70 500,72" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M446,112 Q430,135 424,152" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M450,236 Q470,252 482,256" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C31-arrow3)"/>
  <path d="M566,252 Q582,246 588,236" fill="none" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#C31-arrow3)"/>
  <text x="525" y="295" text-anchor="middle" fill="currentColor">P4 는 아무것도 안 쥔 채 f0 대기 → P3 가 f4 까지 얻어 식사</text>
</svg>
```

(화살표 = "다음에 집으려는 포크". 왼쪽 그림은 모두 첫 포크를 쥔 뒤 옆 사람의 포크를 기다리는 원.) 왜 교착이 불가능한가: 포크 번호로 보면 P0..P3 는 "작은 번호 → 큰 번호" 순서로 잡고, P4 는 f0 → f4 로 역시 "작은 번호 → 큰 번호" 다. **모두가 같은 전역 순서로 락을 잡으니 순환이 생길 수 없다** — Ch.32 의 lock ordering 과 같은 원리.

흡연자 문제(cigarette smoker's), 잠자는 이발사(sleeping barber) 같은 다른 "유명한" 문제들도 있다. 대부분 동시성 사고 연습용이다 [D08].

## 8. 세마포어 구현하기: Zemaphore (31.7)

락 + CV + 정수 하나면 된다 (Fig 31.16):

```c
typedef struct __Zem_t {
    int value;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} Zem_t;

void Zem_init(Zem_t *s, int value) {     // 한 스레드만 호출
    s->value = value;
    Cond_init(&s->cond);
    Mutex_init(&s->lock);
}
void Zem_wait(Zem_t *s) {
    Mutex_lock(&s->lock);
    while (s->value <= 0)
        Cond_wait(&s->cond, &s->lock);
    s->value--;
    Mutex_unlock(&s->lock);
}
void Zem_post(Zem_t *s) {
    Mutex_lock(&s->lock);
    s->value++;
    Cond_signal(&s->cond);
    Mutex_unlock(&s->lock);
}
```

Dijkstra 정의와의 차이: **값이 음수로 내려가지 않는다**(먼저 기다리고 나서 뺀다). 따라서 "음수 = 대기자 수" 불변식은 없다. 구현이 쉽고, 리눅스 구현도 이 방식이다. Ch.30 의 규칙(상태 변수 `value`, while 재확인, 락 쥐고 signal)이 그대로 들어 있다.

반대 방향 — **세마포어로 CV 만들기는 어렵다.** 경험 많은 프로그래머들이 Windows 에서 시도했다가 버그를 줄줄이 냈다 [B04]. 핵심 난점: CV 의 signal 은 "지금 자는 사람이 없으면 아무 일도 안 일어나야" 하는데, 세마포어 post 는 **기억**된다. 그래서 대기자 수를 따로 세야 하고, "대기자 수 확인 → post" 와 "대기자 등록 → 락 해제 → wait" 사이의 race 를 다 막아야 한다.

> **TIP — BE CAREFUL WITH GENERALIZATION**: Lampson 의 경고 "Don't generalize; generalizations are generally wrong." 세마포어는 락과 CV 의 일반화로 볼 수 있지만, 그 일반화가 꼭 필요한가? CV 를 세마포어 위에 만들기 어렵다는 사실은 이 일반화가 생각만큼 일반적이지 않다는 뜻일 수 있다.

## 9. 요약 (31.8)

- 세마포어는 강력하고 유연하다. 어떤 프로그래머는 락/CV 를 버리고 세마포어만 쓴다.
- 사용법의 90% 는 **초기값 선택**이다: 1(락), 0(순서), N(자원 개수).
- 더 많은 퍼즐: Allen Downey, *The Little Book of Semaphores* [D08] (무료).

## 10. 직접 해보기

환경: Apple M2 (8코어), macOS 26.4.1, Apple clang 21. 모든 코드 `cc -Wall -Wextra -O0 -pthread`, **경고 0개**.

### 10.0 먼저: macOS 에는 unnamed POSIX 세마포어가 없다

macOS 의 `<semaphore.h>` 에 `sem_init()` 선언은 있지만 **deprecated** 이고 (그냥 부르면 `-Wdeprecated-declarations` 경고), 호출하면 **−1 / errno=ENOSYS** 를 돌려준다 (아래 [0] 실측). 선택지는 셋:

- 이름 있는 세마포어 `sem_open("/name", O_CREAT, 0600, 1)` — 프로세스 간 공유용으로는 macOS 도 지원한다.
- Grand Central Dispatch 의 `dispatch_semaphore_create(n)` / `dispatch_semaphore_wait(s, timeout)` / `dispatch_semaphore_signal(s)` — macOS 의 네이티브 카운팅 세마포어. **타임아웃이 있어서** 교착 감지 데모에 좋다 (10.3).
- 책처럼 **Zemaphore** 를 직접 만든다 (10.1, 10.2).

OSTEP 숙제 `threads-sema` 의 `common_threads.h` 도 `#ifdef __linux__` 일 때만 `<semaphore.h>` 와 `Sem_init` 매크로를 정의한다. 그래서 macOS 에서 `fork-join.c` 에 `Sem_init(&s, 0);` 을 넣고 (복사본으로) 컴파일하면:

```text
$ cc -Wall -pthread fj.c -o fj        # fork-join.c 복사본에 Sem_init 한 줄 추가
fj.c:6:1: error: unknown type name 'sem_t'
    6 | sem_t s; 
      | ^
fj.c:17:5: error: call to undeclared function 'Sem_init'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   17 |     Sem_init(&s, 0);
      |     ^
2 errors generated.
```

→ macOS 에서 `threads-sema` 숙제를 하려면 `sem_t`/`Sem_*` 를 Zemaphore 로 바꿔 끼우면 된다 (아래 코드의 `Zem_t` 를 그대로 복사).

### 10.1 Zemaphore 로 락 · 순서 · bounded buffer · 교착 재현 (`code/C31_zemaphore.c`)

[0] sem_init 확인, [1] Zemaphore 구현 자체, [2] 이진 세마포어 락(4 스레드 × 100만 증가), [3] 초기값 0 으로 순서 맞추기, [4] Fig 31.12 정답 bounded buffer (10만 개, 합 검증), [5] Fig 31.11 의 잘못된 mutex 위치 → 감시 스레드가 50ms 동안 진척이 없으면 교착 판정.

```c
// C31_zemaphore.c — Ch.31 세마포어를 macOS 에서 실제로 돌리기.
//  0) macOS 의 unnamed POSIX 세마포어 sem_init() 은 미지원(-1, errno=ENOSYS) 임을 확인
//  1) 책 Fig 31.16 의 Zemaphore(mutex + cond + value) 를 직접 구현
//  2) 이진 세마포어 = 락 (초기값 1)        : 4 스레드 x 1,000,000 증가 → 정확히 4,000,000
//  3) 순서 맞추기 (초기값 0)              : parent 가 child 를 기다림
//  4) bounded buffer (Fig 31.12, 정답 버전): empty=MAX, full=0, mutex=1
//  5) 잘못된 버전 (Fig 31.11, mutex 를 바깥에서 잡음) → 교착. 감시 스레드가 진행이 멈춘 걸 감지
//
// build: cc -Wall -Wextra -O0 -pthread code/C31_zemaphore.c -o .work/bin/C31_zemaphore
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------------- Zemaphore (Fig 31.16) ----------------
typedef struct {
    int value;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} Zem_t;

static void Zem_init(Zem_t *s, int value) {
    s->value = value;
    pthread_cond_init(&s->cond, NULL);
    pthread_mutex_init(&s->lock, NULL);
}
static void Zem_wait(Zem_t *s) {
    pthread_mutex_lock(&s->lock);
    while (s->value <= 0)                 // 값이 0 이하면 잔다 (값은 절대 음수가 되지 않음)
        pthread_cond_wait(&s->cond, &s->lock);
    s->value--;
    pthread_mutex_unlock(&s->lock);
}
static void Zem_post(Zem_t *s) {
    pthread_mutex_lock(&s->lock);
    s->value++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
}

// ---------------- 0) sem_init on macOS ----------------
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static void check_sem_init(void) {
    sem_t s;
    errno = 0;
    int rc = sem_init(&s, 0, 1);
    printf("[0] sem_init(&s, 0, 1) = %d, errno = %d (%s)\n", rc, errno, strerror(errno));
}
#pragma clang diagnostic pop

// ---------------- 2) binary semaphore as lock ----------------
static Zem_t lock_sem;
static long counter = 0;
static void *adder(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000000; i++) {
        Zem_wait(&lock_sem);
        counter++;                         // critical section
        Zem_post(&lock_sem);
    }
    return NULL;
}

// ---------------- 3) ordering (parent waits for child) ----------------
static Zem_t done_sem;
static void *child(void *arg) {
    (void)arg;
    usleep(30000);
    printf("    child\n");
    Zem_post(&done_sem);                   // "끝났다" 신호
    return NULL;
}

// ---------------- 4/5) bounded buffer ----------------
#define MAX 4
static int buffer[MAX];
static int fill_i = 0, use_i = 0;
static Zem_t empty, full, mutex;
static volatile long progress = 0;        // 감시용: 처리된 아이템 수
static int loops = 100000;

static void put(int v) { buffer[fill_i] = v; fill_i = (fill_i + 1) % MAX; }
static int  get(void)  { int t = buffer[use_i]; use_i = (use_i + 1) % MAX; return t; }

static void *producer_ok(void *arg) {
    (void)arg;
    for (int i = 1; i <= loops; i++) {
        Zem_wait(&empty);                 // p1   빈 칸 하나 예약
        Zem_wait(&mutex);                 // p1.5 put 만 짧게 보호
        put(i);                           // p2
        Zem_post(&mutex);                 // p2.5
        Zem_post(&full);                  // p3   채워진 칸 하나 생김
    }
    return NULL;
}
static void *consumer_ok(void *arg) {
    long *sum = arg;
    for (int i = 0; i < loops; i++) {
        Zem_wait(&full);                  // c1
        Zem_wait(&mutex);                 // c1.5
        int tmp = get();                  // c2
        Zem_post(&mutex);                 // c2.5
        Zem_post(&empty);                 // c3
        *sum += tmp; progress++;
    }
    return NULL;
}
// Fig 31.11: mutex 를 empty/full 보다 먼저 잡는다 → 교착
static void *producer_bad(void *arg) {
    (void)arg;
    for (int i = 1; i <= loops; i++) {
        Zem_wait(&mutex);                 // p0 (NEW LINE)
        Zem_wait(&empty);                 // p1
        put(i);
        Zem_post(&full);
        Zem_post(&mutex);
    }
    return NULL;
}
static void *consumer_bad(void *arg) {
    long *sum = arg;
    for (int i = 0; i < loops; i++) {
        Zem_wait(&mutex);                 // c0 (NEW LINE) — 락을 쥔 채로
        Zem_wait(&full);                  // c1 — 데이터가 없으면 여기서 잠듦 (락은 계속 쥠!)
        int tmp = get();
        Zem_post(&empty);
        Zem_post(&mutex);
        *sum += tmp; progress++;
    }
    return NULL;
}

static void run_bb(int bad) {
    Zem_init(&empty, MAX); Zem_init(&full, 0); Zem_init(&mutex, 1);
    fill_i = use_i = 0; progress = 0;
    long sum = 0;
    pthread_t p, c;
    // 소비자를 먼저 띄워서 "빈 버퍼에서 소비자가 먼저 mutex 를 잡는" 상황을 만든다
    pthread_create(&c, NULL, bad ? consumer_bad : consumer_ok, &sum);
    usleep(20000);
    pthread_create(&p, NULL, bad ? producer_bad : producer_ok, NULL);

    long last = -1;
    for (int tick = 0; tick < 40; tick++) {   // 최대 2초 감시 (50ms 간격)
        usleep(50000);
        long cur = progress;
        if (cur == loops) break;
        if (cur == last) {
            printf("    watchdog: no progress for 50ms at %ld/%d items, mutex.value=%d full.value=%d empty.value=%d -> DEADLOCK\n",
                   cur, loops, mutex.value, full.value, empty.value);
            return;                         // 스레드는 영원히 잠들어 있음; 데모이므로 그냥 둔다
        }
        last = cur;
    }
    pthread_join(p, NULL); pthread_join(c, NULL);
    long expect = (long)loops * (loops + 1) / 2;
    printf("    consumed %ld items, sum=%ld (expected %ld) -> %s\n",
           progress, sum, expect, sum == expect ? "OK" : "MISMATCH");
}

int main(void) {
    check_sem_init();

    Zem_init(&lock_sem, 1);
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, adder, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("[2] binary Zemaphore as lock: counter = %ld (expected 4000000)\n", counter);

    printf("[3] ordering with Zemaphore initialised to 0:\n");
    Zem_init(&done_sem, 0);
    printf("    parent: begin\n");
    pthread_t ch;
    pthread_create(&ch, NULL, child, NULL);
    Zem_wait(&done_sem);
    printf("    parent: end\n");
    pthread_join(ch, NULL);

    printf("[4] bounded buffer, mutex INSIDE (Fig 31.12):\n");
    run_bb(0);
    printf("[5] bounded buffer, mutex OUTSIDE (Fig 31.11):\n");
    run_bb(1);
    return 0;                              // 교착된 스레드는 프로세스 종료와 함께 사라진다
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C31_zemaphore.c -o .work/bin/C31_zemaphore && .work/bin/C31_zemaphore
[0] sem_init(&s, 0, 1) = -1, errno = 78 (Function not implemented)
[2] binary Zemaphore as lock: counter = 4000000 (expected 4000000)
[3] ordering with Zemaphore initialised to 0:
    parent: begin
    child
    parent: end
[4] bounded buffer, mutex INSIDE (Fig 31.12):
    consumed 100000 items, sum=5000050000 (expected 5000050000) -> OK
[5] bounded buffer, mutex OUTSIDE (Fig 31.11):
    watchdog: no progress for 50ms at 0/100000 items, mutex.value=0 full.value=0 empty.value=4 -> DEADLOCK
```

해석:

- [0] `sem_init` = −1, errno 78 = `ENOSYS` "Function not implemented". 리눅스에서 쓰던 코드를 macOS 로 옮길 때 **리턴값을 안 보면 조용히 망가진다**.
- [2] 400만 정확. 락 없이 같은 걸 하면 Ch.26 에서 본 것처럼 값이 모자란다.
- [5] 교착 순간의 값이 그림과 정확히 같다: `mutex=0`(소비자가 쥠), `full=0`(소비자가 기다림), `empty=4`(생산자는 mutex 에서 막혀 empty 를 건드리지도 못함). 진척 0/100000.

### 10.2 reader-writer 락의 writer 기아 측정 (`code/C31_rwlock.c`)

reader 4명이 500ms 동안 "1ms 읽기"를 쉬지 않고 반복하고, writer 는 10ms 에 도착한다. 간단 버전(Fig 31.13)과, **회전문(turnstile)** 세마포어를 추가한 no-starve 버전을 비교한다. 회전문: writer 는 들어오면서 회전문을 잠그고(wait), reader 는 회전문을 "통과"(wait 후 바로 post)해야 들어올 수 있다. writer 가 대기 중이면 새 reader 는 회전문에서 막히고, 안에 있던 reader 들이 빠지면 writer 차례가 온다.

```c
// C31_rwlock.c — Ch.31.5 reader-writer 락 (Fig 31.13) 을 Zemaphore 로 만들고,
// "읽는 쪽이 계속 들어오면 writer 가 굶는다(starvation)" 를 실제 시간으로 잰다.
// 그리고 turnstile(회전문) 세마포어 하나를 추가한 no-starve 버전과 비교한다 (Downey, Little Book of Semaphores).
//
// build: cc -Wall -Wextra -O0 -pthread code/C31_rwlock.c -o .work/bin/C31_rwlock
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct { int value; pthread_cond_t cond; pthread_mutex_t lock; } Zem_t;
static void Zem_init(Zem_t *s, int v) { s->value = v; pthread_cond_init(&s->cond, NULL); pthread_mutex_init(&s->lock, NULL); }
static void Zem_wait(Zem_t *s) { pthread_mutex_lock(&s->lock); while (s->value <= 0) pthread_cond_wait(&s->cond, &s->lock); s->value--; pthread_mutex_unlock(&s->lock); }
static void Zem_post(Zem_t *s) { pthread_mutex_lock(&s->lock); s->value++; pthread_cond_signal(&s->cond); pthread_mutex_unlock(&s->lock); }

typedef struct {
    Zem_t lock;        // readers 카운터 보호
    Zem_t writelock;   // writer 1명 또는 reader 여러 명
    Zem_t turnstile;   // no-starve 버전에서만 사용
    int readers;
    int nostarve;
} rwlock_t;

static void rwlock_init(rwlock_t *rw, int nostarve) {
    rw->readers = 0; rw->nostarve = nostarve;
    Zem_init(&rw->lock, 1); Zem_init(&rw->writelock, 1); Zem_init(&rw->turnstile, 1);
}
static void rwlock_acquire_readlock(rwlock_t *rw) {
    if (rw->nostarve) { Zem_wait(&rw->turnstile); Zem_post(&rw->turnstile); } // writer 가 대기 중이면 여기서 막힘
    Zem_wait(&rw->lock);
    rw->readers++;
    if (rw->readers == 1) Zem_wait(&rw->writelock);   // 첫 reader 가 writelock 획득
    Zem_post(&rw->lock);
}
static void rwlock_release_readlock(rwlock_t *rw) {
    Zem_wait(&rw->lock);
    rw->readers--;
    if (rw->readers == 0) Zem_post(&rw->writelock);   // 마지막 reader 가 반납
    Zem_post(&rw->lock);
}
static void rwlock_acquire_writelock(rwlock_t *rw) {
    if (rw->nostarve) Zem_wait(&rw->turnstile);        // 회전문을 잠가 새 reader 진입 차단
    Zem_wait(&rw->writelock);
    if (rw->nostarve) Zem_post(&rw->turnstile);
}
static void rwlock_release_writelock(rwlock_t *rw) { Zem_post(&rw->writelock); }

static rwlock_t rw;
static struct timeval t0;
static double ms(void) { struct timeval t; gettimeofday(&t, NULL); return (t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_usec - t0.tv_usec) / 1e3; }

#define RUN_MS 500.0
static pthread_mutex_t stat_m = PTHREAD_MUTEX_INITIALIZER;
static int inside = 0, max_inside = 0;
static long reads_done = 0;

static void *reader(void *arg) {
    (void)arg;
    while (ms() < RUN_MS) {
        rwlock_acquire_readlock(&rw);
        pthread_mutex_lock(&stat_m); inside++; if (inside > max_inside) max_inside = inside; pthread_mutex_unlock(&stat_m);
        usleep(1000);                         // 1ms 동안 "읽기"
        pthread_mutex_lock(&stat_m); inside--; reads_done++; pthread_mutex_unlock(&stat_m);
        rwlock_release_readlock(&rw);
    }
    return NULL;
}
static void *writer(void *arg) {
    (void)arg;
    usleep(10000);                            // reader 들이 자리 잡은 뒤 10ms 에 도착
    double arrive = ms();
    rwlock_acquire_writelock(&rw);
    double got = ms();
    printf("    writer: arrived %.1fms, got lock %.1fms -> waited %.1fms\n", arrive, got, got - arrive);
    rwlock_release_writelock(&rw);
    return NULL;
}

static void run(int nostarve) {
    rwlock_init(&rw, nostarve);
    inside = max_inside = 0; reads_done = 0;
    gettimeofday(&t0, NULL);
    printf("=== %s ===\n", nostarve ? "no-starve rwlock (turnstile)" : "simple rwlock (Fig 31.13)");
    pthread_t r[4], w;
    for (int i = 0; i < 4; i++) pthread_create(&r[i], NULL, reader, NULL);
    pthread_create(&w, NULL, writer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(r[i], NULL);
    pthread_join(w, NULL);
    printf("    readers: max concurrently inside = %d, total reads = %ld in %.0fms\n", max_inside, reads_done, RUN_MS);
}

int main(void) {
    run(0);
    run(1);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C31_rwlock.c -o .work/bin/C31_rwlock && .work/bin/C31_rwlock
=== simple rwlock (Fig 31.13) ===
    writer: arrived 14.8ms, got lock 500.8ms -> waited 485.9ms
    readers: max concurrently inside = 4, total reads = 1344 in 500ms
=== no-starve rwlock (turnstile) ===
    writer: arrived 13.5ms, got lock 13.7ms -> waited 0.2ms
    readers: max concurrently inside = 4, total reads = 1314 in 500ms
```

해석: 간단 버전에서 writer 는 **약 486ms**, 즉 reader 들이 그만둘 때까지 사실상 끝까지 굶었다. 회전문 하나 추가로 **0.2ms** 로 줄었고, reader 처리량(총 reads)은 거의 그대로다. 최대 동시 reader 4 = 읽기끼리는 진짜로 병렬로 들어갔다는 증거.

### 10.3 식사하는 철학자: 교착과 Dijkstra 해법 (`code/C31_dining.c`)

macOS 네이티브 `dispatch_semaphore` 로 포크를 만든다. 최악의 스케줄을 확실히 만들려고 첫 포크를 집은 뒤 10ms 쉰다. 두 번째 포크를 2초 안에 못 얻으면 "교착"으로 판정.

```c
// C31_dining.c — Ch.31.6 식사하는 철학자. macOS 네이티브 세마포어인 dispatch_semaphore 로 포크 5개를 만든다.
//  broken : 모두 "왼쪽 → 오른쪽" 순서. 모두가 왼쪽 포크를 쥔 순간 교착.
//           (재현을 확실히 하려고 왼쪽을 쥔 뒤 10ms 쉬게 함 = 최악의 스케줄을 강제)
//  fixed  : 철학자 4만 "오른쪽 → 왼쪽" (Dijkstra 의 해법) → 순환 대기가 깨져 교착 없음
//  교착 감지: dispatch_semaphore_wait 에 2초 타임아웃을 주고, 타임아웃이 나면 "누가 무엇을 쥐고 기다리는지" 출력
//
// build: cc -Wall -Wextra -O0 -pthread code/C31_dining.c -o .work/bin/C31_dining
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define N 5
#define MEALS 20
static dispatch_semaphore_t forks[N];
static int fixed = 0;
static int meals[N];
static int holding[N];                       // 디버그용: 철학자 p 가 쥐고 있는 첫 포크 (-1 = 없음)
static volatile int deadlocked = 0;

static int left(int p)  { return p; }
static int right(int p) { return (p + 1) % N; }

// 2초 안에 못 얻으면 0 반환
static int grab(int p, int f) {
    long rc = dispatch_semaphore_wait(forks[f], dispatch_time(DISPATCH_TIME_NOW, 2LL * NSEC_PER_SEC));
    if (rc != 0) {
        printf("  P%d: holding fork %d, waited 2s for fork %d -> timeout\n", p, holding[p], f);
        deadlocked = 1;
        return 0;
    }
    return 1;
}

static void *philosopher(void *arg) {
    int p = (int)(long)arg;
    for (int i = 0; i < MEALS && !deadlocked; i++) {
        int first = left(p), second = right(p);
        if (fixed && p == N - 1) { first = right(p); second = left(p); }   // 순서를 뒤집은 한 명
        if (!grab(p, first)) return NULL;
        holding[p] = first;
        usleep(10000);                          // 첫 포크를 쥐고 잠깐 멍때림 → 최악의 interleaving 유도
        if (!grab(p, second)) return NULL;      // (교착 시: 쥔 포크는 데모라서 그냥 둔다)
        meals[p]++;                             // eat()
        dispatch_semaphore_signal(forks[first]);
        dispatch_semaphore_signal(forks[second]);
        holding[p] = -1;
    }
    return NULL;
}

static void run(int fix) {
    fixed = fix; deadlocked = 0;
    for (int i = 0; i < N; i++) { forks[i] = dispatch_semaphore_create(1); meals[i] = 0; holding[i] = -1; }
    printf("=== %s ===\n", fix ? "fixed: P4 grabs right first" : "broken: everyone grabs left first");
    pthread_t t[N];
    for (long i = 0; i < N; i++) pthread_create(&t[i], NULL, philosopher, (void *)i);
    for (int i = 0; i < N; i++) pthread_join(t[i], NULL);
    printf("  meals:");
    for (int i = 0; i < N; i++) printf(" P%d=%d", i, meals[i]);
    printf("  -> %s\n", deadlocked ? "DEADLOCK detected" : "all finished");
    // dispatch_semaphore 는 생성 값보다 작은 상태로 해제하면 크래시하므로, 교착 시엔 일부러 해제하지 않는다
}

int main(void) {
    run(0);
    run(1);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C31_dining.c -o .work/bin/C31_dining && .work/bin/C31_dining
=== broken: everyone grabs left first ===
  P0: holding fork 0, waited 2s for fork 1 -> timeout
  P1: holding fork 1, waited 2s for fork 2 -> timeout
  P2: holding fork 2, waited 2s for fork 3 -> timeout
  P3: holding fork 3, waited 2s for fork 4 -> timeout
  P4: holding fork 4, waited 2s for fork 0 -> timeout
  meals: P0=0 P1=0 P2=0 P3=0 P4=0  -> DEADLOCK detected
=== fixed: P4 grabs right first ===
  meals: P0=20 P1=20 P2=20 P3=20 P4=20  -> all finished
```

해석: 깨진 버전은 5명 모두 "자기 왼쪽(=번호 p)을 쥐고 p+1 을 기다림" — P4 는 f4 를 쥐고 f0 을 기다리므로 원이 닫혔다. 고친 버전은 같은 10ms 지연을 줘도 모두 20끼를 다 먹었다. (실행 1 의 출력 순서는 2초 타임아웃이 거의 동시에 터져서 실행마다 섞인다.)

### 10.4 OSTEP 숙제 `threads-sema`

이 폴더는 시뮬레이터가 아니라 **빈칸 채우기 C 뼈대**(`fork-join.c`, `rendezvous.c`, `barrier.c`, `reader-writer.c`, `reader-writer-nostarve.c`, `mutex-nostarve.c`)다. 10.0 에서 본 대로 macOS 에서는 세마포어 매크로가 없으니 Zemaphore 를 붙여서 풀어야 한다. `reader-writer-nostarve.c` 는 10.2 의 회전문 버전이 정답의 한 형태다.

## 11. 펌웨어 엔지니어의 눈으로

- **RTOS 의 기본 동기화 도구가 바로 세마포어다.** FreeRTOS `xSemaphoreCreateBinary/Counting`, Zephyr `k_sem`, ThreadX `tx_semaphore`. ISR 에서 쓸 수 있는 건 **give/post 쪽뿐**(`xSemaphoreGiveFromISR`, `k_sem_give`) — ISR 은 잠들 수 없으니까. "ISR 이 데이터 준비 → 태스크가 take" 는 초기값 0 세마포어 = 31.3 의 순서 맞추기 그대로.
- **이진 세마포어를 뮤텍스로 쓰지 마라 (RTOS 에서는 특히).** 세마포어는 "소유자" 개념이 없어서 **우선순위 상속(priority inheritance)** 을 못 한다. 낮은 우선순위 태스크가 쥔 상태에서 중간 우선순위가 CPU 를 먹으면 높은 우선순위가 무한정 밀린다 (Mars Pathfinder 의 priority inversion). FreeRTOS 의 `xSemaphoreCreateMutex` 는 상속을 하는 별도 객체다. Zemaphore 의 "post 는 아무나 할 수 있다" 가 장점이자 이 문제의 원인.
- **SSD 컨트롤러의 크레딧(credit) 카운터 = 카운팅 세마포어.** NAND 채널별 outstanding 명령 수, write buffer 의 빈 슬롯 수, NVMe 큐 엔트리 수를 "크레딧"으로 관리하는 건 `empty=MAX` 세마포어와 같다. 하드웨어에서는 이걸 **HW 세마포어 레지스터**나 원자적 카운터로 구현하고, 크레딧이 0 이면 명령 발행을 멈춘다 (= sem_wait 에서 잠).
- **멀티코어 SoC 의 하드웨어 세마포어/메일박스.** Apple 스타일 SoC 나 AI 가속기의 AP ↔ 코프로세서 간에는 공유 메모리 링 + 하드웨어 메일박스/세마포어 블록으로 bounded buffer 를 만든다. 5.2 의 교훈("잠들 수 있는 대기를 락 안에서 하지 마라")은 "스핀락을 쥔 채 상대 코어의 doorbell 을 기다리지 마라"로 그대로 옮겨진다 — 상대도 같은 스핀락을 잡아야 doorbell 을 울릴 수 있다면 교착이다.
- **GPU/가속기의 timeline semaphore** (Vulkan timeline semaphore, CUDA event, Metal shared event): 값이 단조 증가하는 64비트 카운터를 "N 이상이 되면 진행"으로 기다린다. 카운팅 세마포어의 일반화이고, 대기 조건이 제각각이라 구현은 Ch.30 의 covering condition 처럼 "값이 오르면 대기자들이 각자 재확인" 구조다.

## 12. 면접 질문

### Q1. 세마포어와 뮤텍스의 차이는? 이진 세마포어로 뮤텍스를 대신해도 되나?
<details>
<summary>답 보기</summary>

**뮤텍스는 소유권(ownership)이 있다** — 잠근 스레드만 풀 수 있고, 그래서 재귀 락 검사, 오류 검출, **우선순위 상속**이 가능하다. **세마포어는 그냥 카운터**라 누구나 post 할 수 있다(그래서 ISR→태스크 신호, 순서 맞추기에 쓸 수 있다). 기능적으로 이진 세마포어(초기값 1)는 락처럼 동작하지만, 실시간 시스템에서는 priority inversion 을 막지 못하므로 상호 배제에는 **뮤텍스**, 이벤트 알림/자원 개수에는 **세마포어**를 쓰는 게 원칙. 또 세마포어는 "post 를 기억"하므로 CV 와 달리 lost wakeup 이 없다.

</details>

### Q2. 세마포어로 bounded buffer 를 구현하라. mutex 는 어디서 잡아야 하나?
<details>
<summary>답 보기</summary>

`empty = MAX`, `full = 0`, `mutex = 1`. 생산자: `wait(empty); wait(mutex); put; post(mutex); post(full)`. 소비자: `wait(full); wait(mutex); get; post(mutex); post(empty)`. **mutex 는 반드시 empty/full 안쪽**. 바깥에서 잡으면 소비자가 mutex 를 쥔 채 `wait(full)` 로 잠들고, 생산자는 mutex 를 못 얻어 `post(full)` 을 못 해서 **교착**. 일반 원칙: 블로킹될 수 있는 대기를 락을 쥔 채 하지 마라 (hold-and-wait). 또 MAX>1 이고 생산자가 여럿이면 put/get 의 인덱스 갱신이 race 이므로 mutex 자체는 꼭 필요하다.

</details>

### Q3. 식사하는 철학자 문제에서 교착을 피하는 방법들을 말하라.
<details>
<summary>답 보기</summary>

(1) **자원 순서 정하기**: 한 명(또는 짝수 번호)만 포크 순서를 뒤집기 = 모든 철학자가 "번호 작은 포크 먼저" → 순환 대기 불가 (Dijkstra). (2) **동시에 앉는 인원 제한**: 초기값 4 인 세마포어로 최대 4명만 포크를 집게 하면 적어도 한 명은 두 개를 얻는다. (3) **둘 다 한 번에 획득**: 전역 락 또는 상태 기반(양 옆이 안 먹을 때만 먹음, Tanenbaum 해법) — hold-and-wait 제거. (4) **trylock + 백오프**: 두 번째를 못 얻으면 첫 번째를 내려놓음 — 대신 livelock 가능. 교착 회피와 별개로 **기아**(특정 철학자가 계속 밀림)도 따져야 한다.

</details>

### Q4. Reader-writer 락의 단점은? writer 기아를 어떻게 막나?
<details>
<summary>답 보기</summary>

단순 구현(첫 reader 가 writelock 획득, 마지막 reader 가 반납)은 reader 가 계속 들어오면 **writer 가 굶는다** (실측: 500ms 중 486ms 대기). 해법: writer 가 도착하면 **새 reader 진입을 막는** 회전문(turnstile) 세마포어 추가 → 실측 0.2ms. 또는 writer 우선 정책(대기 writer 수 카운트). 그리고 성능 면에서 RW 락은 내부 카운터 갱신이 결국 공유 캐시라인 쓰기라서, 임계 구역이 짧으면 **일반 뮤텍스보다 느린 경우가 흔하다** (Hill's Law). 읽기 위주 고성능에는 RCU/seqlock 을 검토.

</details>

### Q5. 락과 CV 로 세마포어를 구현하라. 반대로 세마포어로 CV 를 만드는 게 어려운 이유는?
<details>
<summary>답 보기</summary>

`value`, `mutex`, `cond`. wait: `lock; while (value <= 0) cond_wait; value--; unlock`. post: `lock; value++; cond_signal; unlock`. (Zemaphore. 값이 음수로 안 내려가므로 "음수=대기자 수" 불변식은 버린다.) 반대가 어려운 이유: **CV signal 은 대기자가 없으면 사라져야** 하지만 세마포어 post 는 **누적**된다. 그래서 대기자 수를 별도로 세고, "대기자 등록 → 외부 뮤텍스 해제 → 세마포어 wait" 가 원자적이지 않은 틈, broadcast 시 정확히 현재 대기자 수만큼만 post 하기 등을 다 처리해야 한다. Birrell 이 Windows 에서 이걸 하다 생긴 버그들을 정리한 글 [B04] 이 유명하다.

</details>

### Q6. 세마포어의 초기값은 어떻게 정하나? 예를 들어 "스레드 N 개가 모두 도착하면 함께 출발"(배리어)은?
<details>
<summary>답 보기</summary>

초기값 = "처음에 wait 없이 통과시킬 수 있는 수". 락 1, 순서 0, 자원 N. 배리어: `count=0` (mutex 로 보호), `barrier` 세마포어 초기값 **0**. 각 스레드: `wait(mutex); count++; if (count == N) post(barrier); post(mutex); wait(barrier); post(barrier);` — 마지막 도착자가 한 번 post 하면 회전문처럼 한 명씩 통과하면서 다음 사람을 위해 다시 post 한다. 재사용 가능한 배리어는 회전문 두 개가 필요하다 (Downey 3.7).

</details>

## 13. 자가 점검 & 숙제

### 퀴즈 1. 초기값 1 인 세마포어에 T0 가 들어가 있고, T1, T2, T3 가 차례로 wait 했다. Dijkstra 정의에서 값은? Zemaphore 에서는?
<details>
<summary>정답</summary>

Dijkstra: 1 → 0(T0) → −1, −2, −3. 값 **−3** = 대기자 3명. Zemaphore: 값은 **0** 에서 멈추고 세 명은 CV 큐에서 잠든다 (값은 음수가 되지 않음).

</details>

### 퀴즈 2. 부모-자식 순서 맞추기에서 초기값을 1 로 잘못 주면 어떻게 되나?
<details>
<summary>정답</summary>

부모의 `sem_wait` 가 1 → 0 으로 **바로 통과**해 자식이 끝나기 전에 "parent: end" 를 찍을 수 있다. 기다림이 사라진다. (자식이 나중에 post 하면 값이 1 로 남는다.)

</details>

### 퀴즈 3. Fig 31.12 에서 `post(mutex)` 와 `post(full)` 의 순서를 바꾸면(즉 `post(full)` 을 먼저) 문제가 되나?
<details>
<summary>정답</summary>

**교착은 안 생긴다.** post 는 절대 잠들지 않으므로 순서를 바꿔도 대기 순환이 생기지 않는다. 깨어난 소비자가 잠깐 mutex 에서 기다릴 뿐이다. 위험한 건 **wait 의 순서**(mutex 를 쥔 채 empty/full 을 wait)다.

</details>

### 퀴즈 4. 철학자 5명 중 P4 만 순서를 뒤집었다. P2 와 P4 를 동시에 뒤집으면 여전히 교착이 없나?
<details>
<summary>정답</summary>

없다. 교착에는 **모두**가 한 방향으로 쥔 채 원을 이뤄야 하는데, 순서를 뒤집은 철학자가 한 명이라도 있으면 그 지점에서 원이 끊긴다. (짝수/홀수 번갈아 뒤집는 것도 흔한 해법.) 단, 모두를 뒤집으면 다시 "모두 오른쪽 먼저"가 되어 교착 가능.

</details>

### 숙제 (OSTEP `threads-sema`, macOS 는 Zemaphore 로 치환)

- `fork-join.c`: 세마포어 하나로 부모가 자식을 기다리게 → **초기값 0** 의 의미 확인.
- `rendezvous.c`: 두 스레드가 서로 도착을 기다린 뒤 진행 → 세마포어 2개, **post 를 먼저, wait 를 나중에** 해야 교착이 없음을 확인.
- `reader-writer-nostarve.c`: 10.2 의 회전문 방식으로 writer 기아 제거 → 측정 코드로 writer 대기 시간 비교.

## 14. 다음으로

- [Ch.32 흔한 동시성 버그](2026-09-30_C32_concurrency_bugs.md): 이 장에서 두 번 만난 교착(잘못된 mutex 위치, 철학자)을 4가지 조건과 예방/회피/탐지로 체계화.
- [Ch.30 조건 변수](2026-09-30_C30_condition_variables.md): Zemaphore 의 부품.
- [Ch.33 이벤트 기반 동시성](2026-09-30_C33_event_based_concurrency.md): 락도 세마포어도 없이 동시성을 얻는 다른 길.
