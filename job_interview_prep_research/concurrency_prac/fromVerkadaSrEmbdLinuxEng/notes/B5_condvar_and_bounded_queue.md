# B5. 조건 변수와 블로킹 큐 — 기다리는 법을 배운다

> **이 노트를 읽고 나면**: (1) `pthread_cond_wait` 가 하는 세 가지와 왜 뮤텍스를 인자로 받는지 설명할 수 있다. (2) `while` 로 술어를 다시 확인하는 이유 세 가지를 댈 수 있다. (3) bounded blocking queue 를 push/pop/close/destroy 까지 직접 짜고 깔끔하게 종료시킬 수 있다.
>
> **선행**: [B4. 뮤텍스](B4_mutex.md). 락과 가시성을 먼저 알아야 한다.
>
> **이 개념을 쓰는 문제**: [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) Part 1(`evq_pop_timed`), [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) Part 1(생산자 4 + 소비자 1, 대기 줄 두 개), [04_temp_single_flight](../04_temp_single_flight/question_note)(요청 합치기), [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note)(2단 파이프라인).

---

## 1. 왜 이게 필요한가

뮤텍스는 "지금 만지지 마"만 말할 수 있다. "생길 때까지 기다려"는 못 한다. 그런데 03의 `evq_pop_timed(out, 50000)` 은 이벤트가 없으면 최대 50 ms 기다려야 하고, 07의 logger 는 큐가 빌 때마다 기다려야 한다. 뮤텍스만 있으면 방법은 하나뿐이다 — 계속 들여다보는 것(busy-wait):

```c
for (;;) {                                  /* busy-wait: 절대 이렇게 쓰지 말 것 */
    pthread_mutex_lock(&m);
    if (count > 0) { /*꺼낸다*/ pthread_mutex_unlock(&m); break; }
    pthread_mutex_unlock(&m);                /* 잡았다 놨다를 초당 수억 번 */
}
```

돌아가긴 한다. 대가가 얼마인지 재 봤다. 생산자가 2 ms 마다 하나씩 500개를 만들고(총 1초), 소비자가 (a) busy-wait, (b) 1 ms 씩 자며 폴링, (c) condvar 로 기다린다. 같은 일, 같은 결과, 세 가지 방법(`b5_cpu2.c`, Apple M2, `/usr/bin/time -p` 와 `getrusage`):

```
spin  소비 500개  CPU  1.502 s  락시도   263378685  지연 평균     1 us  최대    10 us
poll  소비 500개  CPU  0.016 s  락시도        1480  지연 평균   451 us  최대  1923 us
cv    소비 500개  CPU  0.016 s  락시도        1001  지연 평균    11 us  최대    74 us
```

| 방법 | CPU | 락 시도 | 평균 지연 | 읽는 법 |
|---|---|---|---|---|
| busy-wait | **1.502 s** | 2.6억 회 | 1 us | 1초짜리 일에 CPU 코어 하나를 다 태웠다 |
| 1 ms 폴링 | 0.016 s | 1,480 회 | **451 us** | CPU 는 살렸지만 지연이 450배 |
| condvar | 0.016 s | 1,001 회 | 11 us | 폴링의 CPU 로 busy-wait 의 지연을 얻는다 |

busy-wait 가 나쁜 이유 세 가지가 이 표에 다 있다. **CPU**: 1초 동안 코어 하나가 100% (`user 1.49`). **배터리**: 게이트웨이나 배터리 장비에서 이건 그냥 발열과 소모다 — 아무 일도 안 하면서. **락 경합**: 소비자가 초당 2.6억 번 락을 잡으니 생산자가 락을 못 잡아 전체 실행 시간이 1.47초에서 1.73초로 늘었다. 기다리는 쪽이 일하는 쪽을 느리게 만든다. 조건 변수는 "조건이 바뀔 때까지 재워 달라"를 커널에 맡기므로 락 시도가 1,001회 — 딱 필요한 만큼만 깨어났다.

## 2. 그림으로 먼저

블로킹 큐에는 **대기 줄이 두 개** 있다. 큐가 비면 소비자가, 큐가 차면 생산자가 기다린다. 둘은 서로 다른 조건을 기다리므로 조건 변수도 두 개다:

```
            mutex m 하나가 buf, head, tail, closed 를 지킨다
 생산자 ──push──►┌────────────────────────┐──pop──► 소비자
                 │ [E][E][ ][ ]  4칸 중 2 │
                 └────────────────────────┘
 꽉 차면 not_full 에서 잔다 ◄── pop 이 signal(not_full)
        push 가 signal(not_empty) ──► 비면 not_empty 에서 잔다
```

`pthread_cond_wait` 한 번이 실제로 하는 일은 세 단계다. 이 그림이 이 노트의 핵심이다:

```svg
<svg viewBox="0 0 660 270" role="img" aria-label="pthread_cond_wait 의 세 단계">
  <text class="lbl" x="130" y="16" text-anchor="middle">소비자 C</text><text class="lbl" x="520" y="16" text-anchor="middle">생산자 P</text>
  <line class="muted" x1="130" y1="24" x2="130" y2="250"/><line class="muted" x1="520" y1="24" x2="520" y2="250"/>
  <rect class="box" x="40" y="30" width="180" height="24" rx="6"/><text x="130" y="47" text-anchor="middle">lock(m)</text><rect class="box" x="40" y="60" width="180" height="24" rx="6"/><text x="130" y="77" text-anchor="middle">while (count == 0)</text>
  <rect class="fill-soft" x="40" y="90" width="180" height="42" rx="6"/><text x="130" y="108" text-anchor="middle">cond_wait(&amp;cv, &amp;m)</text>
  <text class="lbl" x="130" y="125" text-anchor="middle">① unlock + sleep (원자적)</text>
  <rect class="box" x="430" y="100" width="180" height="24" rx="6"/><text x="520" y="117" text-anchor="middle">lock(m) — 비어 있다</text><rect class="box" x="430" y="130" width="180" height="24" rx="6"/><text x="520" y="147" text-anchor="middle">count++ ; signal(cv)</text>
  <rect class="box" x="430" y="160" width="180" height="24" rx="6"/><text x="520" y="177" text-anchor="middle">unlock(m)</text>
  <line class="accent dash" x1="428" y1="142" x2="225" y2="142"/><polygon class="accentf" points="220,142 233,137 233,147"/>
  <text class="lbl" x="325" y="134" text-anchor="middle">② 깨운다</text>
  <rect class="fill-soft" x="40" y="196" width="180" height="42" rx="6"/><text x="130" y="214" text-anchor="middle">cond_wait 가 돌아온다</text>
  <text class="lbl" x="130" y="231" text-anchor="middle">③ 다시 lock(m) 한 뒤에</text>
  <text class="lbl" x="325" y="214" text-anchor="middle">깨어나도 락을 다시 잡아야 돌아온다 (P 의 unlock 후)</text>
</svg>
```

- **① unlock + sleep 을 원자적으로** — 이 둘 사이에 다른 스레드가 끼어들 틈이 없다. 그래서 뮤텍스를 인자로 받는다.
- **② 깨어남** — 다른 스레드의 `signal`/`broadcast`, 또는 아무 이유 없이(가짜 기상).
- **③ 다시 lock** — 돌아왔을 때는 반드시 락을 쥐고 있다. 그래서 `cond_wait` 다음 줄에서 공유 상태를 바로 읽어도 된다.

## 3. 개념 (용어를 하나씩)

### 조건 변수 (condition variable)

**비유**: 대기 번호표가 없는 대기실. 사람들이 들어가 잠들어 있고, 직원이 "한 명 오세요"(`signal`) 또는 "전부 오세요"(`broadcast`)를 외친다.

**정확한 정의**: **잠든 스레드의 대기 줄**. 그것뿐이다. 조건 변수는 **상태를 저장하지 않는다.** "이벤트가 3개 있다"를 기억하지 않고, 기다리는 사람이 없을 때 `signal` 을 하면 그 신호는 **그냥 사라진다.** 세마포어와의 결정적 차이가 이것이다.

그래서 조건 변수는 혼자 쓸 수 없다. 항상 셋이 한 벌이다.

- **술어(predicate)**: `count > 0` 처럼 내가 기다리는 조건. 이게 진짜 "상태"다.
- **뮤텍스**: 그 상태를 지킨다. 술어를 읽는 것도, 바꾸는 것도 락 안에서.
- **조건 변수**: "그 상태가 바뀌었다"를 알리는 수단.

```c
static pthread_mutex_t m  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;   /* 정적 초기화 */
/* 동적이면 pthread_cond_init(&cv, NULL) / pthread_cond_destroy(&cv) */
```

한 뮤텍스에 조건 변수를 여러 개 붙일 수 있다(07이 `not_empty`, `not_full` 두 개를 쓴다). 반대로 한 조건 변수를 서로 다른 뮤텍스와 섞어 쓰면 정의되지 않은 동작이다.

### `pthread_cond_wait` 가 하는 세 가지

```c
pthread_mutex_lock(&m);
while (count == 0)                    /* 술어 확인 (락 안에서) */
    pthread_cond_wait(&cv, &m);       /* ① unlock+sleep ② 깨어남 ③ 다시 lock */
count--;                              /* 여기서는 락을 쥐고 있고 count > 0 이다 */
pthread_mutex_unlock(&m);
```

세 가지를 다시 확인한다. ① **원자적으로** 뮤텍스를 풀고 잠든다. ② 신호(또는 가짜 기상)로 깨어난다. ③ **뮤텍스를 다시 잡은 뒤에** 돌아온다. ③ 때문에 `cond_wait` 가 돌아오는 시점은 "신호가 온 순간"이 아니라 "신호를 보낸 쪽이 `unlock` 하고 내가 락을 다시 얻은 순간"이고, 그 사이에 다른 스레드가 먼저 락을 잡아 항목을 가져갈 수도 있다 — `while` 이 필요한 이유 중 하나다.

### lost wakeup — 조건 변수가 뮤텍스를 인자로 받는 진짜 이유

①의 "원자적으로"가 없다면 어떻게 되는지가 이 절의 전부다. unlock 과 sleep 사이에 틈이 있으면, 그 틈에서 생산자가 지나가며 신호를 보내고, 그 신호는 **아직 잠들지 않은** 소비자를 지나쳐 사라진다. 소비자는 그다음에 잠들어서 영원히 안 깨어난다.

```svg
<svg viewBox="0 0 660 250" role="img" aria-label="lost wakeup: unlock 과 sleep 사이의 틈">
  <text class="lbl" x="130" y="16" text-anchor="middle">소비자 C</text><text class="lbl" x="520" y="16" text-anchor="middle">생산자 P</text>
  <line class="muted" x1="130" y1="24" x2="130" y2="230"/><line class="muted" x1="520" y1="24" x2="520" y2="230"/>
  <rect class="box" x="40" y="30" width="180" height="24" rx="6"/><text x="130" y="47" text-anchor="middle">lock(m): count == 0</text><rect class="box" x="40" y="60" width="180" height="24" rx="6"/><text x="130" y="77" text-anchor="middle">unlock(m)</text>
  <rect class="fill-soft" x="40" y="92" width="180" height="40" rx="6"/><text x="130" y="110" text-anchor="middle">← 틈 (gap) →</text>
  <text class="lbl" x="130" y="126" text-anchor="middle">아직 잠들지 않았다</text>
  <rect class="box" x="430" y="94" width="180" height="24" rx="6"/><text x="520" y="111" text-anchor="middle">lock(m); count++</text><rect class="box" x="430" y="124" width="180" height="24" rx="6"/><text x="520" y="141" text-anchor="middle">signal / wake</text>
  <line class="accent dash" x1="428" y1="136" x2="235" y2="120"/><polygon class="accentf" points="228,118 242,116 238,127"/>
  <text class="lbl" x="330" y="112" text-anchor="middle">기다리는 사람이 없다 → 신호가 사라진다</text>
  <rect class="box" x="40" y="160" width="180" height="24" rx="6"/><text x="130" y="177" text-anchor="middle">sleep</text><rect class="box dash" x="40" y="192" width="180" height="26" rx="6"/><text x="130" y="209" text-anchor="middle">영원히 깨지 않는다</text>
  <text class="lbl" x="330" y="205" text-anchor="middle">count 는 1인데 아무도 꺼내지 않는다</text>
</svg>
```

조건 변수를 손으로 만들어서 이 틈을 재현해 봤다(`b5_lost.c`). 잠들기는 파이프 `read` 로, 깨우기는 "기다리는 사람이 있을 때만" `write` 로 했다. `good` 은 락 안에서 `waiting = 1` 을 세우고, `bad` 는 `unlock` 뒤에 세운다:

```c
while (count == 0) {
    if (bad) { pthread_mutex_unlock(&m); usleep(1000); waiting = 1; }  /* ← 틈 */
    else     { waiting = 1; pthread_mutex_unlock(&m); }                 /* 틈이 없다 */
    char c; read(pipefd[0], &c, 1);          /* 잠든다 */
    pthread_mutex_lock(&m);
}
```

```
good (락 안에서 등록) 버전
  소비자: 하나 받았다. 정상 종료
bad (unlock 뒤에 등록) 버전
  생산자: 기다리는 사람이 없다고 보고 깨우지 않았다
  [watchdog] 1.5초 동안 소비자가 안 깨어난다 → lost wakeup
```

`pthread_cond_wait(&cv, &m)` 이 뮤텍스를 **인자로** 받는 이유가 이것이다. 커널이 "대기 줄에 등록"과 "락 해제"를 하나의 원자적 동작으로 처리하기 때문에 틈이 없다. 내가 할 일은 **술어를 락 안에서 확인하고, 락을 쥔 채로 `cond_wait` 를 부르는 것**뿐이다. 신호를 보내는 쪽도 **상태를 락 안에서 바꿔야** 한다 — 안 그러면 술어 확인과 신호 사이에 다시 틈이 생긴다.

### 왜 `if` 가 아니라 `while` 인가 — 세 가지 이유

**(1) 가짜 기상 (spurious wakeup)**: POSIX 는 아무도 신호하지 않아도 `cond_wait` 가 돌아올 수 있다고 명시한다. 리눅스에서는 시그널 처리나 futex 재시도로 실제로 일어난다. **(2) 가로채기 (stolen wakeup)**: `cond_wait` 는 락을 다시 잡아야 돌아오므로, 그 사이 `pop` 에 들어온 다른 소비자가 항목을 먼저 가져갈 수 있다. **(3) broadcast**: 항목은 하나인데 전원을 깨우면 첫 번째를 뺀 나머지는 빈 큐를 본다 — 종료에는 broadcast 를 써야 하니 반드시 생기는 상황이다.

```
(1) C : wait ────► (아무 신호 없음) 깨어남 → count 는 여전히 0
(2) P : push 1개, signal ──►
    C1: (깨어남) .... 락 대기 .... lock → count == 0   ← 내 몫을 누가 가져갔다
    C2:          lock → count-- → unlock
(3) P : push 1개, broadcast ──►
    C1: 깨어남 → lock → count-- (1개 소비)
    C2: 깨어남 → lock → count == 0                     ← if 면 빈 큐를 꺼낸다
```

(3)을 실제로 돌려 봤다. 소비자 2명, 항목 1개, `broadcast` 한 번(`b5_while.c`):

```
if 버전: 소비자 2명, 항목 1개, broadcast 한 번
  소비자 0: 하나 꺼냈다 (남은 개수 0)
  소비자 1: ★ 빈 큐에서 꺼내려 했다 (count=0)
while 버전: 소비자 2명, 항목 1개, broadcast 한 번
  소비자 0: 하나 꺼냈다 (남은 개수 0)
  소비자 1: 큐가 닫혔다 → 정상 종료
```

외울 문장: **`cond_wait` 가 돌아왔다는 것은 "조건이 참"이 아니라 "조건을 다시 확인해 봐라"라는 뜻이다.** 03 모범답안 `evq_pop_timed()` 의 주석이 정확히 이 말을 한다 — "Loop, do not trust a single wakeup: condvars wake spuriously, and another consumer may have taken the event we were signalled for."

### `signal` 이냐 `broadcast` 냐

| 상황 | 무엇을 | 왜 |
|---|---|---|
| push 로 항목 1개 추가 | `signal(not_empty)` | 항목 하나는 대기자 한 명만 만족시킨다 |
| pop 으로 빈칸 1개 확보 | `signal(not_full)` | 같은 이유. 빈칸 하나 = 생산자 한 명 |
| 큐를 닫는다 / 종료 | `broadcast` **양쪽 다** | `running` 이 바뀌면 **모든** 대기자의 술어가 무효가 된다 |
| 대기자들이 서로 다른 조건을 기다린다 | 조건 변수를 나눈다 | 한 cv 에 섞으면 잘못 깨워서 다시 자는 낭비가 생긴다 |
| 잘 모르겠다 | `broadcast` | 느릴 뿐 틀리지 않는다. `signal` 은 틀릴 수 있다 |

`broadcast` 의 대가는 **thundering herd** 다. 대기자 N 명이 전부 깨어나 같은 뮤텍스로 몰려들고, 한 명만 일하고 N-1 명은 술어를 다시 확인해 다시 잔다 — 문맥 교환 N번이 낭비된다. 07 모범답안 주석이 이 판단을 그대로 적어 두었다: "A state change that invalidates the predicate for EVERY waiter must be a broadcast. During normal operation the opposite is true: one pop frees exactly one slot, so signal is correct there and avoids a thundering herd." 반대로 종료에 `signal` 을 쓰면 어떻게 되는지도 재 봤다 — 소비자 3명이 빈 큐에서 자고 있을 때 닫는다(`b5_close.c`):

```
signal    → 깨어난 소비자 1 / 3 ← 나머지는 영원히 잠들어 있다 (join 하면 멈춘다)
broadcast → 깨어난 소비자 3 / 3
```

### 세마포어와 조건 변수

| | 세마포어 | 조건 변수 |
|---|---|---|
| 상태 | **카운터를 기억한다.** 기다리는 사람이 없을 때 `post` 해도 값이 남는다 | 기억하지 않는다. 대기자가 없으면 신호가 사라진다 |
| 짝 | 혼자 쓸 수 있다 | 반드시 뮤텍스 + 술어와 함께 |
| 표현력 | 조건이 "개수" 하나일 때만 | 임의의 술어(`count>0 && !closed`, 세 조건의 조합…) |
| 소유자 | 없다. 누가 `post` 해도 된다 (ISR 도) | 신호는 누구나 보낼 수 있지만 상태 변경은 락 안에서 |

"빈칸 개수"와 "채워진 개수"만 필요한 순수 bounded queue 라면 세마포어 두 개가 더 짧다. 하지만 `closed` 플래그, 타임아웃, drop 정책이 붙는 순간 술어가 개수 하나로 표현되지 않는다 — 이 저장소의 열 문제 모두가 그 경우다(덧붙여 macOS 는 이름 없는 `sem_init` 을 지원하지 않는다). C++ 이라면 `cv.wait(lock, []{ return count > 0 || closed; });` 한 줄이 `while` 을 **내장**한다. 표준이 술어를 받아 루프를 대신 돌려 주는 것이고, 그 이유가 위 세 가지다. C 에는 그 편의가 없으니 직접 쓴다.

## 4. 코드로 보기

가장 작은 한 쌍이다. 한 줄씩 본다.

```c
/* 소비자 */
pthread_mutex_lock(&m);                      /* (1) */
while (count == 0 && !closed)                /* (2) */
    pthread_cond_wait(&cv, &m);              /* (3) */
if (count > 0) { count--; got = 1; }         /* (4) */
pthread_mutex_unlock(&m);

/* 생산자 */
pthread_mutex_lock(&m);                      /* (5) */
count++;
pthread_cond_signal(&cv);                    /* (6) */
pthread_mutex_unlock(&m);                    /* (7) */
```

- **(1) 없으면?** `cond_wait` 가 자기가 소유하지 않은 뮤텍스를 풀려고 한다 → 정의되지 않은 동작. 술어를 읽는 것도 락 밖이라 값이 찢어진다.
- **(2) `if` 로 바꾸면?** §3의 세 가지 — 가짜 기상, 가로채기, broadcast 중 하나에서 빈 큐를 꺼낸다. `!closed` 를 빼면 종료할 때 영원히 안 깨어난다.
- **(3)** 돌아왔을 때는 락을 쥐고 있다. 그래서 (4)에서 바로 상태를 만져도 된다.
- **(5) 없으면?** 술어 변경(`count++`)이 락 밖이 된다. 소비자가 "count==0" 을 확인한 직후 신호가 지나가 lost wakeup 이 된다.
- **(6) 없으면?** 소비자는 영원히 잔다. 상태는 맞는데 아무도 알려 주지 않는다.
- **(7)** `signal` 은 락을 쥔 채로 해도 되고 풀고 해도 된다(쥔 채로 하면 깨어난 쪽이 한 번 더 잘 수 있지만 현대 구현이 최적화한다). **먼저 상태를 바꾸고 그다음에 신호**라는 순서만 지키면 된다.

07 모범답안의 실제 `pop` 이 이 모양 그대로다:

```c
/* 07_badge_audit_dedupe/audit_log_solution.c */
static bool queue_pop(struct Badge *out)
{
    pthread_mutex_lock(&q.m);
    while (q.head == q.tail && q.running)
        pthread_cond_wait(&q.not_empty, &q.m);      /* predicate in a loop */
    if (q.head == q.tail) {                         /* closed and drained */
        pthread_mutex_unlock(&q.m);
        return false;
    }
    *out = q.slot[q.tail & QMASK];
    q.tail++;
    pthread_cond_signal(&q.not_full);               /* one slot freed */
    pthread_mutex_unlock(&q.m);
    return true;
}
```

술어가 `q.head == q.tail && q.running` 두 개의 조합이라는 점을 보자. 깨어난 이유가 "항목이 왔다"인지 "닫혔다"인지 구별하는 것은 `cond_wait` 가 아니라 **술어 재확인**이다. `while` 을 나온 뒤 `head == tail` 이면 "닫히고 다 비었다"이므로 `false` 를 돌려 logger 를 끝낸다.

## 5. 단계별로 만들어 보기

bounded blocking queue 를 상태 → push → pop → close → destroy 순으로 만든다(전체 파일 `b5_bq.c`, 03/07 모범답안을 초보자용으로 줄인 것).

### 1단계 — 상태 정의

```c
#define CAP  4u                       /* 2의 거듭제곱 */
#define MASK (CAP - 1u)

struct bq {
    pthread_mutex_t m;
    pthread_cond_t  not_empty;        /* 소비자가 기다리는 줄 */
    pthread_cond_t  not_full;         /* 생산자가 기다리는 줄 */
    int             buf[CAP];
    unsigned        head, tail;       /* 자유 증가: 개수 = head - tail */
    bool            closed;
};
static unsigned bq_count(const struct bq *q) { return q->head - q->tail; }
```

`head`/`tail` 을 마스킹하지 않고 계속 증가시키면 개수가 `head - tail` 한 줄이고, `unsigned` 랩어라운드는 CAP 가 2의 거듭제곱이면 무해하다(03의 `q_depth_locked()` 와 같다). `closed` **가 없으면** 소비자가 "비었다"와 "영원히 비어 있다"를 구별할 수 없어 종료할 방법이 없다.

### 2단계 — push

```c
static int bq_push(struct bq *q, int v)
{
    pthread_mutex_lock(&q->m);
    while (bq_count(q) == CAP && !q->closed)
        pthread_cond_wait(&q->not_full, &q->m);        /* 자리가 날 때까지 */
    if (q->closed) { pthread_mutex_unlock(&q->m); return -1; }
    q->buf[q->head & MASK] = v;
    q->head++;
    pthread_cond_signal(&q->not_empty);                /* 항목 1개 = 소비자 1명 */
    pthread_mutex_unlock(&q->m);
    return 0;
}
```

`while` 의 술어가 `가득 && !닫힘` 인 이유: 닫힌 뒤에는 자리가 절대 나지 않으므로 기다리면 안 된다. `signal(not_empty)` **이 없으면** 소비자가 영원히 잔다. `closed` 확인을 빼면 닫힌 큐에 쓴다.

07은 여기서 한 걸음 더 간다. 문 스레드는 무한정 기다리면 안 되므로(기다리는 동안 그 문은 badge 를 못 읽는다) **유한 대기**를 하고, 마감 시각이 지나면 기록을 버리고 카운터를 올린다:

```c
/* 07_badge_audit_dedupe/audit_log_solution.c — audit_submit() */
uint64_t deadline = reader_now_us() + AUDIT_SUBMIT_WAIT_US;
while (q.running && (unsigned)(q.head - q.tail) == AUDIT_QUEUE_CAP) {
    uint64_t now = reader_now_us();
    if (now >= deadline)
        break;                                  /* deadline, not spurious */
    cv_wait_us(&q.not_full, &q.m, deadline - now);
}
```

`deadline` 이 **절대 시각**이라는 점이 중요하다. 가짜 기상으로 루프를 다시 돌 때 상대 시간을 다시 주면 타임아웃이 계속 늘어난다. 03의 `cond_wait_until()` 주석도 같은 말을 한다 — "The deadline is absolute on purpose."

### 3단계 — pop

```c
static int bq_pop(struct bq *q, int *out)
{
    pthread_mutex_lock(&q->m);
    while (bq_count(q) == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);
    if (bq_count(q) == 0) { pthread_mutex_unlock(&q->m); return -1; }   /* 닫히고 비었다 */
    *out = q->buf[q->tail & MASK];
    q->tail++;
    pthread_cond_signal(&q->not_full);                 /* 빈칸 1개 = 생산자 1명 */
    pthread_mutex_unlock(&q->m);
    return 0;
}
```

루프를 나온 뒤 `closed` 를 보지 않고 **개수만 보는** 것이 포인트다. 닫혔어도 남은 항목은 다 내보낸다(drain) — 07의 logger 가 종료 시점에 남은 badge 를 끝까지 기록하는 이유가 이것이고, 이미 리더에서 읽어 버린 badge 를 여기서 버리면 감사 기록이 사라진다. `pthread_cond_signal(&q->not_full);` **이 없으면** 어떻게 되는지 직접 지워 보고 `alarm(2)` 로 재 봤다:

```
(출력 없음)  exit=142   ← SIGALRM. 2초 동안 아무 진행 없음
```

생산자 둘은 `not_full` 에서 자고, 소비자는 다 비운 뒤 `not_empty` 에서 자고, 아무도 누구를 깨우지 않는다. 데드락이다. 락을 엇갈리게 잡아서가 아니라 **깨우기를 빼먹어서** 생긴 데드락 — condvar 코드에서 가장 흔한 형태다.

### 4단계 — close

```c
static void bq_close(struct bq *q)
{
    pthread_mutex_lock(&q->m);
    q->closed = true;                        /* 상태를 락 안에서 바꾼다 */
    pthread_cond_broadcast(&q->not_empty);   /* 양쪽 대기 줄 전부 */
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->m);
}
```

`closed = true` 는 **모든** 대기자의 술어를 무효화하므로 `broadcast` 이고, 한쪽만 하면 다른 쪽이 남는다. `signal` 로 바꾸면 §3의 실측처럼 3명 중 1명만 깨어나고 나머지는 `join` 에서 멈춘다.

### 5단계 — destroy, 그리고 종료 프로토콜 5단계

```c
static void bq_destroy(struct bq *q)          /* join 이 끝난 뒤에만 */
{
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->m);
}
```

순서가 전부다. 07의 `audit_deinit()` 이 정확히 이 5단계다.

1. **종료를 게시한다** — `atomic_store(&g_running, false)`. 생산자 루프가 다음 바퀴에 멈춘다.
2. **블로킹 벤더 호출에서 빼낸다** — `reader_cancel(door)`(03은 `evsrc_wake()`). 플래그는 벤더 호출 안에서 잠든 스레드에게 보이지 않는다. 이게 없으면 deinit 이 사람이 badge 를 댈 때까지, 필드에서는 몇 분을 기다린다.
3. **큐를 닫고 전원을 깨운다** — `running = false` + `broadcast(not_full)` + `broadcast(not_empty)`, 전부 락 안에서.
4. **생산자를 먼저 join** — 이후로는 아무것도 들어올 수 없다.
5. **소비자를 join** — 남은 항목을 다 비우고 "닫히고 비었다"를 보고 스스로 끝난다. 그 뒤에 `destroy` → `free`.

**왜 join 전에 free 하면 안 되는가.** 소비자 스레드는 아직 `q->m`, `q->head`, `q->buf` 를 읽고 있다. `free` 한 메모리를 읽는 것이 use-after-free 이고, 뮤텍스나 조건 변수라면 커널 대기 큐가 사라진 메모리를 가리킨다. `join` 을 빼고 `free` 한 버전을 ASan 으로 돌려 보면 바로 잡힌다:

```
==18248==ERROR: AddressSanitizer: heap-use-after-free on address 0x6100000000f0
READ of size 4 at 0x6100000000f0 thread T1
```

`join` 은 "그 스레드가 끝났다"의 유일한 증거다. 플래그를 보고 "이제 끝났겠지"로 대신할 수 없다 — 플래그를 세우는 것과 스레드가 마지막 줄을 실행하는 것 사이에는 언제나 시간이 있다.

정상 동작 확인. 생산자 2명이 10개씩, 소비자 1명(항목마다 3 ms 소요), 큐는 4칸:

```
  소비자: 큐가 닫히고 비었다 → 종료
  push 10 + 10, pop 20, 거절 0, 남은 개수 0
```

큐가 4칸뿐인데 20개가 하나도 안 버려졌다 — 생산자가 `not_full` 에서 기다렸다가 자리가 나면 넣었다. 이게 블로킹 큐가 해 주는 일이다(**backpressure**). 07처럼 생산자가 기다릴 수 없는 경우에는 유한 대기 + 드롭 카운터로 바꾼다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `while` 대신 `if` | 드물게 빈 큐에서 꺼낸다. 값이 쓰레기거나 count 가 음수 | 가짜 기상 / 가로채기 / broadcast | 술어를 `while` 로 감싼다. 항상 |
| 술어를 락 밖에서 바꾸고 signal | 소비자가 영원히 잔다 (lost wakeup) | 확인과 신호 사이에 틈이 생긴다 | 상태 변경과 signal 을 같은 임계구역에서 |
| pop 에서 `signal(not_full)` 누락 | 큐가 꽉 찬 뒤 전체가 멈춘다 | 생산자를 깨우는 사람이 없다 | 빈칸이 생겼으면 반드시 알린다 |
| 종료에 `signal` 사용 | 대기자 1명만 깨고 `join` 이 멈춘다 | 상태 변경이 모두의 술어를 무효화했다 | `broadcast`, 그리고 조건 변수 **양쪽** 다 |
| 종료 플래그만 세우고 벤더 호출은 안 깨움 | deinit 이 몇 분 걸린다 | 잠든 스레드는 플래그를 볼 수 없다 | `evsrc_wake()` / `reader_cancel()` 같은 탈출구를 함께 |
| 닫혔다고 남은 항목을 버린다 | 종료할 때마다 기록이 사라진다 | `closed` 를 개수보다 먼저 확인했다 | 루프 뒤에는 개수만 본다. 비었을 때만 끝낸다 |
| `join` 없이 `destroy`/`free` | 간헐적 크래시, ASan heap-use-after-free | 스레드가 아직 그 메모리를 쓴다 | close → join → destroy → free |
| 상대 타임아웃을 루프마다 새로 계산 | 타임아웃이 계속 늘어난다 | 가짜 기상마다 시계가 초기화된다 | 마감 시각을 절대 시각으로 한 번 계산 |
| 한 조건 변수에 서로 다른 조건 | CPU 가 낭비되고 드물게 멈춘다 | 잘못된 대기자를 깨운다 | `not_empty` / `not_full` 처럼 조건별로 나눈다 |

## 7. 손으로 확인하기

```sh
cd 03_event_tailer_shutdown && ./main.sh sol      # 종료 테스트까지 통과하는지
cd ../07_badge_audit_dedupe && ./main.sh sol      # 4생산자 1소비자, 드롭 카운트 확인
grep -n "cond_wait\|cond_signal\|cond_broadcast" audit_log_solution.c
```

`b5_*.c` 는 이 노트의 코드 블록으로 만든 스크래치 파일이다(직접 만들어 돌리면 같은 숫자가 나온다). 그리고 깨 보는 실험이 제일 남는다 — 모범답안을 `audit_log.c` / `event_queue.c` 로 복사해 놓고 하나씩 한다.

- [ ] `queue_pop()` 의 `while` 을 `if` 로 바꾼다 → 언젠가 빈 슬롯을 꺼낸다. 몇 번 돌려야 재현되는지 센다
- [ ] `audit_deinit()` 의 `broadcast` 를 `signal` 로 바꾼다 → `pthread_join` 에서 멈춘다
- [ ] `queue_pop()` 의 `pthread_cond_signal(&q.not_full)` 을 지운다 → 큐가 꽉 차고 전부 멈춘다
- [ ] `audit_deinit()` 에서 `reader_cancel()` 호출을 지운다 → 종료가 3초 걸린다(하니스의 "아무도 없는 문" 대기 시간)

## 8. 자가 점검

```check
Q: pthread_cond_wait 가 하는 일을 순서대로 세 가지 말하라.
A: 첫째, 인자로 받은 뮤텍스를 풀면서 동시에(원자적으로) 대기 줄에 들어가 잠든다. 둘째, signal/broadcast 또는 가짜 기상으로 깨어난다. 셋째, 그 뮤텍스를 다시 잡은 뒤에 호출자에게 돌아온다. 그래서 돌아온 직후에는 락을 쥐고 있고, 공유 상태를 바로 읽어도 된다.

Q: 조건 변수가 왜 뮤텍스를 인자로 받는가?
A: unlock 과 sleep 사이에 틈이 없어야 하기 때문이다. 틈이 있으면 그 사이에 생산자가 상태를 바꾸고 신호를 보내는데, 아직 잠들지 않았으므로 그 신호가 사라진다(lost wakeup). 조건 변수는 상태를 기억하지 않으므로 사라진 신호는 복구할 수 없고, 소비자는 영원히 잔다.

Q: while 을 쓰는 이유 세 가지는?
A: 가짜 기상(POSIX 가 허용한다), 가로채기(cond_wait 는 락을 다시 잡아야 돌아오므로 그 사이 다른 소비자가 항목을 가져갈 수 있다), broadcast(항목은 하나인데 여럿을 깨우면 나머지는 빈 큐를 본다). 정리하면 cond_wait 의 반환은 "조건이 참"이 아니라 "조건을 다시 확인해 봐라"다.

Q: signal 과 broadcast 를 어떻게 고르나?
A: 상태 변화가 대기자 한 명분의 술어만 참으로 만들면 signal 이다 — push 로 항목 1개, pop 으로 빈칸 1개. 상태 변화가 모든 대기자의 술어를 무효화하면 broadcast 다 — closed/running 플래그 변경. broadcast 는 thundering herd 로 문맥 교환을 낭비하지만 틀리지는 않는다. 확신이 없으면 broadcast 로 시작한다.

Q: 종료할 때 join 전에 free 하면 왜 안 되나?
A: 소비자 스레드가 아직 그 뮤텍스와 큐 배열을 읽고 있다. free 한 메모리를 읽는 것은 use-after-free 이고, 뮤텍스나 조건 변수라면 커널 대기 큐가 사라진 메모리를 가리킨다. ASan 으로 돌리면 heap-use-after-free 로 바로 잡힌다. join 이 "그 스레드가 끝났다"의 유일한 증거다.

Q: busy-wait 대신 condvar 를 쓰는 이유를 숫자로 말해 보라.
A: 같은 일(2 ms 간격 500개 소비)을 측정했을 때 busy-wait 는 CPU 1.5초를 태우고 락을 2.6억 번 잡아 생산자까지 느리게 만들었다(실행 시간 1.47초 → 1.73초). 1 ms 폴링은 CPU 는 0.016초로 줄었지만 평균 지연이 451 us 로 늘었다. condvar 는 CPU 0.016초에 평균 지연 11 us 로, 폴링의 CPU 와 busy-wait 의 응답성을 동시에 얻는다.

Q: 타임아웃이 있는 대기(evq_pop_timed)를 구현할 때 조심할 점은?
A: 마감 시각을 절대 시각으로 한 번 계산하고 루프 안에서 다시 계산하지 않는다. 상대 시간을 새로 주면 가짜 기상마다 타임아웃이 늘어난다. 또 ETIMEDOUT 으로 돌아왔어도 그 직전에 항목이 들어왔을 수 있으므로 술어를 한 번 더 확인한 뒤 실패를 돌려준다. 닫힌 뒤에는 남은 항목을 먼저 다 내보내고(drain) 비었을 때만 끝낸다.
```

## 9. 요약 카드

- 조건 변수는 **대기 줄**이고 상태가 없다. 상태는 술어(`count > 0`)에 있고, 그 술어는 뮤텍스가 지킨다.
- `cond_wait` = ① 원자적 unlock+sleep ② 깨어남 ③ **다시 lock 하고 반환**. 반환은 "조건이 참"이 아니라 "다시 확인해라".
- 술어는 항상 **`while`** 로 감싼다 — 가짜 기상 / 가로채기 / broadcast.
- 상태 변경과 `signal` 은 **같은 임계구역**에서. 그래야 lost wakeup 이 없다.
- 항목 1개/빈칸 1개는 `signal`, `closed` 같은 전원 대상 변경은 `broadcast`(조건 변수 양쪽 다).
- 대기 줄은 조건별로 하나: `not_empty` 와 `not_full` 을 섞지 않는다.
- 종료 5단계: 플래그 게시 → 벤더 블로킹 깨우기 → close + broadcast → 생산자 join → 소비자 join, 그 뒤에 destroy/free.
- 면접 한 줄: "The condition variable carries no state — the predicate does. cond_wait releases the mutex and sleeps atomically, which is why a wakeup can never be lost, and it returns holding the mutex, which is why I re-check the predicate in a while loop."
