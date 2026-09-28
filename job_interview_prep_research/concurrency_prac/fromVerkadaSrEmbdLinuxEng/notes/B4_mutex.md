# B4. 뮤텍스 — 임계구역을 지키는 가장 기본 도구

> **이 노트를 읽고 나면**: (1) 데이터를 `pthread_mutex_t` 로 감싸면 정확히 무엇이 보장되는지(상호 배제 **와** 가시성) 말할 수 있다. (2) 에러 경로에서 락이 새지 않게 쓸 수 있다. (3) 데드락 4조건을 대고 락 순서 고정 / trylock+백오프로 고칠 수 있다.
>
> **선행**: 없음. `pthread_create` / `pthread_join` 만 알면 된다.
>
> **이 개념을 쓰는 문제**: 10문제 전부. 특히 [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) 의 큐 락 + 인덱스 락, [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) 의 `g_q.lock` + `g_cnt.lock`, [01_gps_fix_cache](../01_gps_fix_cache/question_note) Part 1 의 구조체 스냅샷.
>
> **다음**: [B5. 조건 변수와 블로킹 큐](B5_condvar_and_bounded_queue.md) — 뮤텍스만으로는 "기다리기"가 안 된다.

---

## 1. 왜 이게 필요한가

07에서 문 4개가 각각 스레드를 하나 쓰고, 넷이 같은 큐에 badge 를 넣는다. 두 스레드가 같은 순간에 `head++` 를 하면 badge 하나가 사라지거나 같은 칸에 두 개가 겹쳐 쓰인다.

`head++` 는 C 로는 한 줄이지만 기계어로는 **읽고 → 1 더하고 → 쓴다** 세 단계다. 그 사이에 다른 스레드가 끼어든다. 스레드 4개가 각각 `counter++` 를 20만 번 한 결과다 (`b4_count.c`, Apple M2, `cc -std=c11 -O2 -pthread`):

```
기대값        : 800000
락 없음       : 234231
mutex 사용    : 800000
```

증가의 **70%가 사라졌다.** 크래시도 경고도 없다. 숫자만 틀리다. 이게 동시성 버그의 기본 모양이다 — 조용하고, 재현이 안 되고, 코드 리뷰로 안 잡힌다.

## 2. 그림으로 먼저

락이 없을 때 증가 하나가 사라지는 과정:

```
시간 →   스레드 A                     스레드 B                counter
  1     load  r0 <- counter (5)                                  5
  2                                   load  r0 <- counter (5)    5
  3     store counter <- 6                                       6
  4                                   store counter <- 6         6   ← 증가 하나 사라짐
```

뮤텍스를 걸면 이 겹침이 불가능해진다. 두 스레드가 같은 문을 지나야 하고, 문은 하나다:

```svg
<svg viewBox="0 0 620 200" role="img" aria-label="뮤텍스는 문이 하나인 방">
  <rect class="fill-soft" x="235" y="25" width="170" height="150" rx="10"/>
  <text x="320" y="52" text-anchor="middle">임계구역</text>
  <text class="lbl" x="320" y="80" text-anchor="middle">head, tail, buf 를</text>
  <text class="lbl" x="320" y="98" text-anchor="middle">읽고 쓰는 구간</text>
  <text class="lbl" x="320" y="124" text-anchor="middle">한 번에 한 스레드만</text>
  <rect class="box" x="20" y="40" width="120" height="42" rx="8"/><text x="80" y="66" text-anchor="middle">스레드 A</text>
  <rect class="box" x="20" y="120" width="120" height="42" rx="8"/><text x="80" y="146" text-anchor="middle">스레드 B</text>
  <line class="accent" x1="142" y1="61" x2="228" y2="74"/><polygon class="accentf" points="236,77 222,71 224,82"/>
  <text class="lbl" x="180" y="55" text-anchor="middle">lock() 성공</text>
  <line class="muted dash" x1="142" y1="141" x2="230" y2="128"/><text class="lbl" x="184" y="160" text-anchor="middle">lock() 안에서 잠듦</text>
  <rect class="box" x="470" y="80" width="130" height="42" rx="8"/><text x="535" y="106" text-anchor="middle">공유 데이터</text>
  <line class="accent" x1="407" y1="101" x2="462" y2="101"/><polygon class="accentf" points="470,101 457,96 457,106"/>
</svg>
```

점선은 "기다린다"는 뜻이다. B 는 `pthread_mutex_lock()` 안에서 **자고 있다** — CPU 를 쓰지 않고, A 가 `unlock` 할 때 커널이 깨워 준다.

## 3. 개념 (용어를 하나씩)

### 임계구역 (critical section)

**정의**: 공유 데이터를 읽거나 쓰는 코드 구간. 둘이 동시에 들어가면 결과가 깨지는 구간. 임계구역은 데이터가 아니라 **코드 구간**이다. "이 변수는 락으로 보호된다"는 말은 정확히는 "이 변수를 만지는 **모든 코드**가 같은 락을 잡는다"는 뜻이고, 한 곳이라도 빠지면 보호는 없다.

### 뮤텍스 (mutex)

**비유**: 화장실 열쇠 하나. 열쇠를 가진 사람만 들어가고, 나올 때 돌려놓는다. **정확한 정의**: 두 상태(unlocked / locked)와 **소유자(owner)** 개념을 가진 동기화 객체. `pthread_mutex_lock()` 은 unlocked 면 locked 로 바꾸고 자신을 소유자로 기록한 뒤 돌아온다. locked 면 호출한 스레드를 재워 대기 큐에 넣는다. `pthread_mutex_unlock()` 은 소유자가 불러서 unlocked 로 바꾸고 기다리는 스레드 하나를 깨운다. 비유와 다른 점 하나 — 열쇠는 남에게 넘길 수 있지만 뮤텍스는 못 넘긴다. **잠근 스레드만 푼다.**

### 선언과 초기화 — 두 가지

```c
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;   /* (a) 정적/전역 */
struct bq { pthread_mutex_t m; int buf[16]; };

void demo(void) {                                          /* (b) 동적 */
    struct bq *q = malloc(sizeof *q);
    pthread_mutex_init(&q->m, NULL);      /* 두 번째 인자 NULL = 기본 속성 */
    pthread_mutex_destroy(&q->m);         /* 아무도 안 잡고 있을 때, 해제 전에 */
    free(q);
}
```

`static`/전역은 (a) — 초기화 함수를 부를 시점 자체가 없다. malloc 한 구조체나 속성이 필요하면 (b)이고, 그때만 `destroy` 가 의미가 있다. `destroy` 를 **잠긴 상태**로 부르면 `EBUSY` 거나 정의되지 않은 동작이고, 누군가 그 뮤텍스에서 자고 있으면 영원히 못 깬다. 순서는 언제나 `join` → `destroy` → `free`. 쓰고 있는 뮤텍스를 다시 `init` 하면 소유자 정보가 날아간다 — 03의 `evq_init()` 이 재초기화 대신 **락을 잡고 필드만 리셋**하는 이유다.

### lock / unlock 규칙 세 가지

1. **잠근 스레드가 푼다.** 남이 잠근 뮤텍스를 풀면 정의되지 않은 동작이다. (세마포어는 허용된다 — 둘의 결정적 차이다)
2. **모든 탈출 경로에서 푼다.** `return`, `break`, `goto`, 에러 처리 전부. 하나만 빠져도 그 락은 영원히 잠긴 채 남는다.
3. **같은 스레드가 두 번 잠그지 않는다.** 기본 뮤텍스는 재진입이 안 된다. 자기가 잡은 락을 또 잡으면 자기를 기다린다.

3번을 눈으로 보자. 기본 뮤텍스는 그냥 멈추므로 진단용 `ERRORCHECK` 속성으로 물어본다:

```c
/* b4_twice.c */
pthread_mutexattr_t a;  pthread_mutex_t m;  pthread_mutexattr_init(&a);
pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);   /* 진단용 */
pthread_mutex_init(&m, &a);
printf("첫 lock  : %d\n", pthread_mutex_lock(&m));
printf("두 번 lock: %d\n", pthread_mutex_lock(&m));        /* 두 번째 */
```

```
첫 lock  : 0
두 번 lock: 11      ← EDEADLK. 기본 mutex 라면 이 줄에서 영원히 멈춘다
```

실전에서 이 실수는 **락을 잡은 함수가 락을 잡는 함수를 부를 때** 난다. 그래서 모범답안들은 이름으로 규칙을 만든다. `q_pop_locked()` 처럼 `_locked` 가 붙었거나 주석에 "with idx.m held" 가 있는 함수는 **호출자가 이미 락을 쥐고 있다**는 약속이다.

### 뮤텍스는 가시성도 보장한다

초보자가 거의 항상 놓치는 부분이다. 뮤텍스는 두 가지를 준다.

- **상호 배제(mutual exclusion)**: 동시에 못 들어간다.
- **가시성(visibility)**: `unlock` 이전에 한 모든 쓰기는, 그 뒤에 같은 뮤텍스를 `lock` 한 스레드에게 **반드시 보인다.**

두 번째가 왜 따로 필요한가. CPU 와 컴파일러는 쓰기를 스토어 버퍼에 담아 두고 순서를 바꿔 내보낸다. 그래서 `payload` 를 채우고 `ready = 1` 을 한 스레드에서는 순서가 맞아도 **다른 코어에서는 `ready = 1` 이 먼저 보일 수 있다.**

```svg
<svg viewBox="0 0 640 250" role="img" aria-label="unlock 이전의 쓰기가 다음 lock 에 보인다">
  <text class="lbl" x="120" y="18" text-anchor="middle">스레드 W (쓰는 쪽)</text><text class="lbl" x="470" y="18" text-anchor="middle">스레드 R (읽는 쪽)</text>
  <line class="muted" x1="120" y1="26" x2="120" y2="230"/><line class="muted" x1="470" y1="26" x2="470" y2="230"/>
  <text class="lbl" x="318" y="244" text-anchor="middle">시간 ↓</text>
  <rect class="box" x="40" y="34" width="160" height="26" rx="6"/><text x="120" y="52" text-anchor="middle">lock(m)</text>
  <rect class="box" x="40" y="68" width="160" height="26" rx="6"/><text x="120" y="86" text-anchor="middle">payload = 42,42,42</text>
  <rect class="box" x="40" y="102" width="160" height="26" rx="6"/><text x="120" y="120" text-anchor="middle">ready = 1</text>
  <rect class="fill-soft" x="40" y="136" width="160" height="26" rx="6"/><text x="120" y="154" text-anchor="middle">unlock(m)</text>
  <rect class="fill-soft" x="390" y="136" width="160" height="26" rx="6"/><text x="470" y="154" text-anchor="middle">lock(m)</text>
  <rect class="box" x="390" y="176" width="160" height="26" rx="6"/><text x="470" y="194" text-anchor="middle">payload 읽기 → 42</text>
  <line class="accent" x1="200" y1="149" x2="384" y2="149"/><polygon class="accentf" points="390,149 377,144 377,154"/>
  <text class="lbl" x="292" y="138" text-anchor="middle">happens-before</text><text class="lbl" x="292" y="178" text-anchor="middle">unlock 이전의 쓰기 전부가 lock 이후에 보인다</text>
</svg>
```

말이 아니라 실험으로 확인한다(`b4_visibility.c`). `L()`/`U()` 는 `use_lock` 일 때만 lock/unlock 하는 매크로다:

```c
/* writer: 데이터를 먼저, 플래그를 나중에 */
L();  payload[0] = payload[1] = payload[2] = 42;  ready = 1;  U();

/* reader: 플래그를 보고 데이터를 읽는다 */
for (;;) { L(); if (ready) { seen = payload[0]+payload[1]+payload[2]; U(); break; } U(); }
if (seen != 126) torn++;        /* ready 는 1인데 42 가 아직 안 보였다 */
```

```
2000000 라운드 (Apple M2, arm64, -O2)
락 없음 : ready=1 인데 값이 덜 보인 횟수 = 412
mutex   : ready=1 인데 값이 덜 보인 횟수 = 0
```

락 없는 쪽은 실행마다 44~1435회 틀렸고, 뮤텍스 쪽은 언제나 0이었다. `volatile` 을 붙였는데도 틀렸다는 점이 중요하다. **`volatile` 은 동기화가 아니다** — 컴파일러에게 "매번 읽어라"만 말하고 CPU 의 순서 재배치에는 아무 말도 안 한다.

**그래서 뮤텍스로 감싼 데이터는 atomic 이 필요 없다.** 03 모범답안의 큐 필드가 전부 평범한 정수다:

```c
/* 03_event_tailer_shutdown/event_queue_solution.c */
static struct {
    pthread_mutex_t lock;
    pthread_cond_t  cv;
    struct Event    buf[EVQ_CAPACITY];
    uint32_t        head, tail;      /* free-running 카운터 */
    uint64_t        received, dropped, popped, errors;
} g_q = { .lock = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };
```

같은 파일의 `g_running` 은 `_Atomic bool` 이다. 왜? 그 변수만 **락 밖에서** 읽힌다 — sampler 스레드가 벤더 호출 사이사이에 락 없이 확인한다.

| 접근 방식 | 필요한 것 |
|---|---|
| 항상 같은 뮤텍스 안에서만 만진다 | 평범한 변수. atomic 불필요 |
| 락 밖에서 읽거나 쓴다 (종료 플래그 등) | `_Atomic` + memory order |
| 락 안 + 락 밖이 섞인다 | 설계가 틀렸다. 하나로 정한다 |

### 락의 크기 — coarse 냐 fine 이냐

**coarse(굵은) 락**은 큰 범위를 락 하나로 덮고, **fine(잔) 락**은 자료구조별/버킷별로 쪼갠다.

| 기준 | coarse 하나 | fine 여러 개 |
|---|---|---|
| 정확성 | 쉽다. 순서 규칙이 필요 없다 | 어렵다. 데드락 위험이 생긴다 |
| 코드 | 짧다 | 어디까지 잡았는지 추적해야 한다 |
| 경합 | 코어가 늘면 병목 | 서로 다른 데이터면 병렬 진행 |
| 알맞은 때 | 임계구역이 수 마이크로초일 때 | 무관한 상태 + 측정된 경합이 있을 때 |

**기본은 큰 락 하나다. 측정한 뒤에 쪼갠다.** 쪼개는 정당한 이유는 둘뿐이다. (1) 보호하는 상태가 서로 **무관**하다, (2) 경합이 **측정**되었다. "빠를 것 같아서"는 이유가 아니다.

### 이 저장소의 실례 — 07은 왜 락이 두 개인가

```c
/* 07_badge_audit_dedupe/audit_log_solution.c */
static struct {
    pthread_mutex_t m;
    pthread_cond_t  not_empty;      /* logger waits here      */
    pthread_cond_t  not_full;       /* door threads wait here */
    struct Badge    slot[AUDIT_QUEUE_CAP];
    unsigned        head, tail;
    bool            running;
} q = {.m = PTHREAD_MUTEX_INITIALIZER, .not_empty = PTHREAD_COND_INITIALIZER,
       .not_full = PTHREAD_COND_INITIALIZER};
static struct {
    pthread_mutex_t m;              /* 완전히 다른 락 */
    dedupe_slot_t   map[AUDIT_DEDUPE_CAP];
    struct Badge    ring[AC42_DOORS][AUDIT_RECENT_PER_DOOR];
} idx = {.m = PTHREAD_MUTEX_INITIALIZER};
```

이유는 위 표의 (1)이다. 큐 락은 "아직 기록 안 된 badge 가 몇 개인가", 인덱스 락은 "이 badge 를 최근에 본 적 있나"를 지킨다. 둘 사이에 지켜야 할 불변식이 없으니 합칠 이유가 없다. 합치면 손해는 크다 — 문이 열리기 전에 불리는 `audit_is_duplicate()` 가 logger 의 큐 작업 뒤에 줄을 선다.

락을 하나로 합치고 그 락을 쥔 채 8 ms flash 쓰기를 하면 어떻게 되는지 재 봤다 (logger 가 50번 쓰는 동안, getter 가 락을 잡는 데 걸린 시간):

```
분리 락: 조회 678회, 락 잡기까지 평균 0 us, 최악 1 us
공용 락: 조회 1회, 락 잡기까지 평균 548946 us, 최악 548946 us
```

공용 락 쪽은 조회가 **0.55초 동안 한 번도 못 들어갔다.** logger 가 `unlock` 직후 곧바로 다시 `lock` 을 가져가 버려서다. 뮤텍스는 **공정하지 않다** — 줄 선 순서대로 주지 않고 방금 놓은 스레드가 다시 가져갈 수 있다(barging). 그래서 07은 `index_record()` 에서 락을 O(1) 작업에만 쓰고, 느린 `audit_store_write()` 는 **락 밖에서** 부른다.

### 데드락 (deadlock)

**정의**: 둘 이상의 스레드가 서로가 쥔 락을 기다려 아무도 진행하지 못하는 상태. 네 조건이 **동시에** 성립할 때만 생긴다(Coffman 조건). 하나만 깨면 데드락은 불가능하다.

| 조건 | 뜻 | 깨는 방법 |
|---|---|---|
| 상호 배제 | 락은 한 번에 하나만 가진다 | 못 깬다. 락의 존재 이유다 |
| 점유 후 대기 | 하나 쥔 채로 다음 걸 기다린다 | 못 잡으면 쥔 것도 놓는다(trylock) |
| 비선점 | 남의 락을 빼앗을 수 없다 | `timedlock` 으로 스스로 포기 |
| 순환 대기 | A→B 를 기다리고 B→A 를 기다린다 | **락에 전역 순서를 정한다** ← 실무의 방법 |

```
  T1 ──쥐고 있다──► A          T2 ──쥐고 있다──► B
   ▲               │            ▲               │
   └──기다린다───  B            └──기다린다───  A    ← 순환. 둘 다 영원히 멈춘다
```

### 우선순위 역전 (priority inversion)

**정의**: 낮은 우선순위 스레드가 락을 쥐고 있어 높은 우선순위 스레드가 그 락을 기다리는 상태. 중간 우선순위 스레드가 CPU 를 계속 쓰면 높은 쪽이 중간 쪽보다 늦게 실행된다. (Mars Pathfinder 를 재부팅시킨 그 버그다.)

```
높음  H:        [락 기다림 ~~~~~~~~~~~~~~~~~~~~~] 실행
중간  M:  실행 ~~~~~~~~~~~~~~~~~ 실행 (L 을 계속 밀어냄)
낮음  L: 락 잡음 ]  ← 선점당해서 unlock 을 못 한다
```

해법은 **우선순위 상속(priority inheritance)**: 락을 쥔 스레드가 그 락을 기다리는 가장 높은 우선순위를 임시로 물려받는다. L 이 H 의 우선순위로 올라가 M 을 밀어내고, `unlock` 하는 순간 내려온다. POSIX 는 `pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT)`(Linux 지원, macOS 는 아님), FreeRTOS 는 `xSemaphoreCreateMutex()` 뮤텍스에만 상속이 있고 `xSemaphoreCreateBinary()` 이진 세마포어에는 **없다.** 면접 단답 — **상호 배제에는 mutex, 신호 전달(ISR → task)에는 binary semaphore.** 이진 세마포어에는 소유자가 없어 누구의 우선순위를 올릴지 모르고, 뮤텍스는 반대로 ISR 에서 `give` 할 수 없다(소유자가 아니다).

### 친척들 — 언제 쓰고 왜 피하나

| 도구 | 하는 일 | 쓸 때 | 피하는 이유 |
|---|---|---|---|
| mutex | 상호 배제 + 가시성, 소유자 있음 | 기본값. 고민되면 이걸 쓴다 | — |
| rwlock | 읽기는 여럿 동시, 쓰기는 혼자 | 읽기가 압도적이고 임계구역이 **길** 때 | 내부 상태가 복잡해 짧은 구간에선 mutex 보다 느리다. writer starvation, 업그레이드 불가 |
| spinlock | 잠들지 않고 계속 재시도 | 임계구역이 수십 ns, 선점 없는 문맥(ISR, 커널) | 유저 공간에서 락 쥔 스레드가 선점되면 코어를 태운다. 상속 없음 |
| recursive mutex | 같은 스레드가 여러 번 잠금 | 레거시 콜백 구조를 못 고칠 때 | "내가 락을 쥐었는지 모르겠다"는 설계 결함을 덮는다. 불변식이 깨진 중간 상태로 재진입할 수 있다 |
| binary semaphore | 0/1 카운터, 소유자 없음 | ISR → task 신호, "사건이 일어났다" | 상호 배제에 쓰면 우선순위 상속이 없고, 남이 풀어도 되니 실수가 조용히 지나간다 |
| counting semaphore | N 개 자원 | 슬롯/토큰 개수 세기 | 조건이 "개수" 하나일 때만. 복잡한 조건은 condvar |

## 4. 코드로 보기

§1 의 실험을 전부 적으면 이렇다. 한 줄씩 왜 그런지 본다.

```c
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;   /* (1) */
static long counter;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 200000; i++) {
        pthread_mutex_lock(&lock);      /* (2) 여기부터 임계구역 */
        counter++;                      /* (3) 평범한 long. atomic 아님 */
        pthread_mutex_unlock(&lock);    /* (4) 여기까지 */
    }
    return NULL;
}
/* main: pthread_create x4 → pthread_join x4 → printf("%ld", counter)  (5) */
```

- **(1) 없으면?** 초기화되지 않은 뮤텍스를 잠그는 건 정의되지 않은 동작이다. 0으로 채워진 전역이 우연히 unlocked 처럼 보이기도 해서 더 나쁘다 — 이식할 때 터진다.
- **(2) 없으면?** §1 의 결과: 800000 이 아니라 234231. **(3)** 락 안이므로 `_Atomic` 이 필요 없다.
- **(4) 없으면?** 첫 바퀴에서 락이 잠긴 채 남고, 두 번째 `lock()` 에서 자기를 기다린다. 프로세스가 멈춘다.
- **(5) 없으면?** `main` 이 먼저 끝나 프로세스가 종료되고 스레드는 일을 마치기 전에 죽는다. join 뒤에는 락이 필요 없다 — join 자체가 "그 스레드의 모든 쓰기가 보인다"를 보장한다.

임계구역 안에서 하면 안 되는 것도 같이 외운다. 느린 I/O(07의 `audit_store_write()` 는 8 ms), 타임아웃 없는 블로킹 벤더 호출(03의 `evsrc_read_blocking()`), `malloc`/`free`, 로그 출력, 다른 모듈의 콜백(그쪽이 어떤 락을 잡을지 모른다).

## 5. 단계별로 만들어 보기

### v0 — 에러 경로에서 락이 샌다 (틀린 버전)

```c
static int store_bad(int i, int v)
{
    pthread_mutex_lock(&m);
    if (i < 0 || i >= 4)
        return -1;              /* ← unlock 없이 탈출. 락이 샌다 */
    slot[i] = v;
    pthread_mutex_unlock(&m);
    return 0;
}
```

이 함수는 **정상 입력으로는 완벽히 동작하고 테스트도 통과한다.** 경계값이 한 번 들어온 다음 호출부터 프로그램 전체가 멈춘다. `trylock` 으로 확인해 보면(`b4_paths.c`):

```
  store_good(9,...) 호출 뒤 trylock → 성공 (락 풀려 있음)
  store_bad(9,...)  호출 뒤 trylock → EBUSY — 락이 아직 잠겨 있다!
```

### v1 — `goto out` 으로 탈출구를 하나로

```c
static int store_good(int i, int v)
{
    int rc = -1;                    /* 실패를 기본값으로 */
    pthread_mutex_lock(&m);
    if (i < 0 || i >= 4)
        goto out;                   /* 탈출구는 하나뿐 */
    slot[i] = v;
    rc = 0;
out:
    pthread_mutex_unlock(&m);       /* 모든 경로가 여기를 지난다 */
    return rc;
}
```

C 에서 `goto out` 은 나쁜 코드가 아니라 **관용구**다. 커널 코드가 전부 이렇게 쓴다. 이유는 하나 — "모든 경로에서 푼다"를 눈으로 검증할 수 있게 만든다. 검증법도 외워 두자: **`lock` 과 `unlock` 개수를 세고, 그 사이에 `return` 이 없는지 본다.**

### v2 — 락 구간을 줄이면 검증할 것도 없어진다

```c
if (i < 0 || i >= 4) return -1;     /* 검증은 락 밖에서 */
pthread_mutex_lock(&m);
slot[i] = v;                        /* 락 안에는 공유 데이터 접근만 */
pthread_mutex_unlock(&m);
```

탈출 경로가 락 안에 없으면 검증할 것도 없다. 07의 `audit_submit()` 첫 줄이 정확히 이 모양이다 — `b == NULL || b->door >= AC42_DOORS` 검사를 `pthread_mutex_lock(&q.m)` **앞에서** 한다.

### v3 — 데드락: 엇갈린 순서를 고친다

규칙은 "`A` 를 먼저, 그다음 `B`". 한 스레드만 순서를 뒤집게 하고, 0.5초 동안 진행이 없으면 보고하는 watchdog 을 붙였다(`b4_deadlock.c`):

```c
/* 스레드 1 (규칙을 지킨다) */
pthread_mutex_lock(&A); pthread_mutex_lock(&B);  done++;
pthread_mutex_unlock(&B); pthread_mutex_unlock(&A);
/* 스레드 2 (bad 모드에서 순서를 뒤집는다) */
pthread_mutex_lock(&B); pthread_mutex_lock(&A);  done++;
pthread_mutex_unlock(&A); pthread_mutex_unlock(&B);
```

```
good: 스레드2가 A→B 로 잠근다
  끝. done=200000 (기대값 200000)
bad : 스레드2가 B→A 로 잠근다
  [watchdog] 0.5초 동안 진행 없음 → 데드락. done=361
```

361번은 성공했다. 그게 데드락의 무서운 점이다 — **대부분은 잘 돌아간다.** 고치는 방법은 한 줄: **락에 전역 순서를 정하고 모두가 그 순서로만 잠근다.** 순서는 주소순·이름순 아무거나 좋고 문서화하고 지키는 것만 중요하다. 03/07 은 더 강한 규칙을 쓴다 — **두 락을 동시에 잡는 코드가 없다.**

### v4 — 순서를 못 정할 때: trylock + 백오프

둘 다 외부 코드이거나 두 노드를 정렬할 수 없을 때는 "하나만 쥔 채 기다리지 않는다"로 조건 2를 깬다.

```c
static void lock_both(pthread_mutex_t *first, pthread_mutex_t *second)
{
    for (unsigned spin = 0; ; spin++) {
        pthread_mutex_lock(first);
        if (pthread_mutex_trylock(second) == 0)
            return;                                   /* 둘 다 잡았다 */
        pthread_mutex_unlock(first);                   /* 하나만 쥐고 기다리지 않는다 */
        usleep(1u << (spin < 6 ? spin : 6));           /* 백오프: 없으면 livelock */
    }
}
```

한 스레드는 `lock_both(&A, &B)`, 다른 스레드는 `lock_both(&B, &A)` 로 2만 번씩 돌린 결과:

```
done=40000 (기대값 40000), 재시도 14회 — 멈추지 않았다
```

`usleep` 백오프가 **핵심**이다. 없으면 둘이 동시에 실패하고 동시에 재시도해 계속 서로를 밀어내는 **livelock** 이 된다. 데드락은 멈춰 있고 livelock 은 바쁘게 도는데 아무 일도 안 한다 — 후자가 더 진단하기 어렵다. 지수 백오프(1, 2, 4, 8… us)가 재시도 시점을 어긋나게 만든다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 에러 경로에서 unlock 누락 | 그 뒤 첫 `lock()` 에서 프로세스가 멈춘다 | 락은 스레드가 죽어도 자동으로 안 풀린다 | `goto out` 으로 모으고 `lock`/`unlock` 개수를 센다 |
| 일부 코드만 락을 잡는다 | 값이 드물게 틀리고 재현이 안 된다 | 보호는 **모든** 접근이 같은 락을 잡을 때만 성립 | 그 필드를 만지는 함수를 `grep` 으로 전부 찾는다. getter 도 락을 잡는다 |
| 같은 스레드가 두 번 잠근다 | 즉시 멈춘다 (`EDEADLK`) | 기본 뮤텍스는 재진입 불가 | 내부 함수를 `_locked` 로 분리해 호출자가 락을 잡게 한다 |
| 락 순서가 함수마다 다르다 | 며칠에 한 번 멈춘다 | 순환 대기 | 전역 락 순서를 정해 주석에 쓴다. 또는 두 락을 동시에 잡지 않는다 |
| 락을 쥔 채 느린 호출 | 무관한 getter 의 지연이 ms 단위로 튄다 | 임계구역이 I/O 시간만큼 길어진다 | 락 안에서는 복사만, I/O 는 락 밖에서 |
| `volatile` 로 동기화하려 한다 | 플래그는 보이는데 데이터가 덜 보인다 | `volatile` 은 순서/가시성을 보장하지 않는다 | 뮤텍스 또는 `_Atomic` |
| 락 안의 데이터를 `_Atomic` 으로 | 느려지고 읽는 사람이 헷갈린다 | 이미 락이 가시성을 준다 | 규칙을 하나로: 락 안이면 평범한 변수 |
| 잠긴 채 `destroy` / `free` | `EBUSY` 또는 use-after-free 크래시 | 기다리던 스레드가 사라진 메모리를 만진다 | 순서는 언제나 close → join → destroy → free |

## 7. 손으로 확인하기

```sh
cd 07_badge_audit_dedupe && ./main.sh sol     # 락 두 개로도 getter 가 밀리지 않는다
grep -n "pthread_mutex" audit_log_solution.c  # 뮤텍스가 몇 개이고 무엇을 지키는지
grep -c "pthread_mutex_lock"   audit_log_solution.c   # lock 과
grep -c "pthread_mutex_unlock" audit_log_solution.c   # unlock 개수가 같아야 한다
```

이 노트의 `b4_*.c` 는 위 코드 블록을 그대로 붙여 만든 스크래치 파일이다 — 직접 만들어 돌리면 같은 숫자가 나온다. 그리고 직접 깨 보는 실험이 제일 남는다: `03_event_tailer_shutdown/event_queue_solution.c` 를 `event_queue.c` 로 복사해 놓고 하나씩 해 본다.

- [ ] `q_push()` 의 `pthread_mutex_unlock(&g_q.lock);` 을 지운다 → 두 번째 이벤트에서 멈춘다
- [ ] `evq_stats()` 의 락을 전부 지운다 → `received == popped + dropped + depth` 가 깨지는 순간이 보인다
- [ ] `cnt_add()` 가 `g_cnt.lock` 대신 `g_q.lock` 을 쓰게 바꾼다 → 통과는 하지만 왜 나쁜지 설명해 본다
- [ ] `q_push()` 안에서 `evq_stats()` 를 부른다 → 같은 락을 두 번 잡아서 즉시 멈춘다

## 8. 자가 점검

```check
Q: 뮤텍스가 보장하는 것 두 가지는?
A: 상호 배제와 가시성이다. 상호 배제는 같은 락으로 감싼 구간에 한 번에 한 스레드만 들어가는 것. 가시성은 unlock 이전에 한 모든 쓰기가 그 뒤 같은 락을 lock 한 스레드에 보이는 것이다. 두 번째 덕분에 락 안의 데이터는 atomic 이 필요 없다.

Q: 03 모범답안의 큐 필드는 평범한 uint32_t 인데 g_running 은 왜 _Atomic bool 인가?
A: 큐 필드는 항상 g_q.lock 안에서만 접근한다. 락이 상호 배제와 가시성을 모두 주므로 평범한 변수로 충분하다. g_running 은 sampler 스레드가 벤더 호출 사이에 락 없이 읽는다. 락이 없으니 가시성을 대신 줄 것이 필요해서 _Atomic 과 memory order 를 쓴다.

Q: 락을 잡은 함수에서 에러로 일찍 return 하는 코드를 리뷰에서 어떻게 잡나?
A: 함수 안의 pthread_mutex_lock 과 unlock 개수를 센다. lock 뒤에 unlock 을 지나지 않는 return / break / goto 가 있으면 락이 샌다. 고치는 방법은 탈출구를 goto out 한 곳으로 모으거나, 입력 검증을 락 밖으로 빼서 락 구간을 공유 데이터 접근만으로 줄이는 것이다.

Q: 데드락 4조건 중 실무에서 실제로 깨는 것은 어느 것이고 어떻게 깨나?
A: 순환 대기다. 락에 전역 순서를 정하고 모든 코드가 그 순서로만 잠그면 순환이 생길 수 없다. 순서를 정할 수 없으면 점유 후 대기를 깬다 — trylock 으로 두 번째 락을 시도하고 실패하면 첫 락도 놓고 백오프한 뒤 재시도한다. 백오프가 없으면 livelock 이 된다.

Q: 07은 락이 두 개인데 왜 데드락 걱정을 하지 않아도 되는가? 그리고 왜 하나로 합치지 않았나?
A: 두 락을 동시에 잡는 코드 경로가 없어서 순환 대기가 만들어질 수 없다. 합치지 않은 이유는 두 락이 무관한 상태를 지키기 때문이다. 큐 락은 미기록 badge 개수, 인덱스 락은 최근 목격 기록. 합치면 문이 열리기 전에 불리는 audit_is_duplicate 가 logger 의 큐 작업 뒤에 줄을 서게 된다.

Q: FreeRTOS 에서 상호 배제에 binary semaphore 를 쓰면 무엇이 문제인가?
A: 우선순위 상속이 없다. 이진 세마포어에는 소유자 개념이 없어서 누구의 우선순위를 올려야 할지 모른다. 낮은 우선순위 태스크가 그것을 쥔 채 중간 우선순위 태스크에 선점되면 높은 우선순위 태스크가 무한정 밀린다. 상호 배제에는 xSemaphoreCreateMutex, ISR→task 신호에는 binary semaphore 를 쓴다.

Q: 읽기가 많은 구조체에 rwlock 을 쓰면 항상 빠른가?
A: 아니다. rwlock 은 reader 수를 세는 등 내부 상태가 더 복잡해서 임계구역이 수백 ns 수준이면 mutex 보다 느리다. 이득은 임계구역이 충분히 길고 reader 가 압도적으로 많을 때만 난다. writer starvation 도 생각해야 한다. 짧은 읽기가 아주 많으면 seqlock 이나 포인터 교체가 낫다.
```

## 9. 요약 카드

- 임계구역은 데이터가 아니라 **코드 구간**이다. 그 데이터를 만지는 **모든** 경로가 같은 락을 잡아야 보호된다.
- 뮤텍스는 상호 배제 **+ 가시성**을 준다. 락 안의 데이터는 `_Atomic` 이 필요 없고, 락 밖에서 읽는 플래그만 atomic 이다.
- 규칙 셋: 잠근 스레드가 푼다 / 모든 경로에서 푼다(`goto out`) / 같은 스레드가 두 번 잠그지 않는다(`_locked` 분리).
- 정적이면 `PTHREAD_MUTEX_INITIALIZER`, 동적이면 `pthread_mutex_init` + `destroy`. 해제 순서는 close → join → destroy → free.
- 락 안에서는 복사와 O(1) 계산만. 느린 I/O, 블로킹 벤더 호출, 남의 콜백은 락 밖에서.
- 기본은 큰 락 하나. 상태가 무관하거나 경합이 측정됐을 때만 쪼갠다. 뮤텍스는 공정하지 않다(barging).
- 데드락은 순환 대기를 깨서 막는다 = **락 순서 고정**. 더 좋은 건 두 락을 동시에 잡지 않는 설계.
- 면접 한 줄: "The mutex gives me mutual exclusion and a happens-before edge, so everything I wrote before unlock is visible to the next thread that takes the lock — that is why these fields don't need to be atomic."
