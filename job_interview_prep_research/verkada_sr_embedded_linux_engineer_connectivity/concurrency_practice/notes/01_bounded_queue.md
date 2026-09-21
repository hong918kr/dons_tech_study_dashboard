# 01. Bounded blocking queue — mutex 1개 + condvar 2개로 스레드를 잇는 법

> **이 노트를 다 읽으면**
> - mutex 와 condition variable 로 "가득 차면 기다리고 비면 기다리는" 큐를 처음부터 짤 수 있다
> - 조건 검사에 `while` 을 쓰는 이유와 종료할 때 `broadcast` 를 쓰는 이유를 시나리오로 설명할 수 있다
> - 소비자가 남은 항목을 다 처리하고 깨끗하게 끝나는 close 프로토콜을 설계할 수 있다
>
> **선행**: `00_start_here` 의 §3 (mutex), §4 (condition variable), §7 (종료와 정리)
> **연습**: `make run N=01` (내 구현) · `make sol N=01` (모범답안) · `make tsan N=01`

---

## 0. 한 문장으로

**고정 크기 버퍼 하나를 mutex 로 지키고, "안 참"과 "안 빔" 두 조건을 condvar 두 개로 기다리게 만든 큐**다. Verkada GC31-E 게이트웨이에서 도어·센서·모뎀 스레드가 만든 이벤트를 업로더 스레드 하나가 LTE 로 올리는 구조가 정확히 이것이고, 그래서 이 문제가 2차 인터뷰 1순위다.

---

## 1. 왜 필요한가 — 비유로 시작

우편함이 8칸 있다. 우체부(생산자)는 편지를 넣고, 집주인(소비자)은 꺼내 간다.

```
                 8칸짜리 우편함 (cap = 8)
   우체부 A ─┐    ┌───┬───┬───┬───┬───┬───┬───┬───┐
   우체부 B ─┼──▶ │ e │ e │ e │   │   │   │   │   │ ──▶ 집주인
   우체부 C ─┘    └───┴───┴───┴───┴───┴───┴───┴───┘
                    ▲               ▲
                   head            tail
                  (다음에 꺼낼 칸)  (다음에 넣을 칸)   count = 3
```

**우편함이 꽉 찼을 때 우체부는 뭘 하나?** 편지를 버리거나(drop — 문제 05), 칸이 빌 때까지 기다린다(block). 출입 이벤트는 감사 로그라 하나도 잃으면 안 되므로 **기다린다**를 고른다. 이게 backpressure 다 — 소비자가 느리면 생산자가 저절로 느려진다. **우편함이 비었을 때 집주인은?** 1초마다 열어보면(폴링) CPU와 전력을 낭비하므로 **"편지 오면 깨워달라"고 하고 잔다.** 그게 condition variable 이다.

베어메탈로 번역하면 우체부는 UART/도어 ISR 이고 집주인은 파서 태스크다. 차이는 여기서 우체부가 **진짜로 잠들 수 있다**는 것 — ISR 안에서는 못 하지만 Linux 스레드에서는 할 수 있고, 그래서 drop 없이 버틸 수 있다.

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 / 내가 아는 것과의 대응 |
|---|---|---|
| bounded | 큐 용량에 상한이 있다 | 무한 큐는 소비자가 느리면 메모리를 다 먹고 OOM 으로 죽는다 |
| circular buffer | `head`/`tail` 을 `% cap` 으로 돌리는 배열 | 베어메탈 UART 링버퍼와 완전히 같은 것 |
| mutex | 임계구역 문지기. lock/unlock | `__disable_irq()` 로 감싸던 구간의 멀티코어 버전 |
| condition variable | "조건이 바뀔 때까지 락을 놓고 자는" 대기열 | DMA 완료 플래그를 폴링하던 것을 재우기로 바꾼 것 |
| spurious wakeup | 아무도 signal 안 했는데 `cond_wait` 이 반환하는 것 | POSIX 가 허용한다. 그래서 조건 검사는 `while` |
| signal vs broadcast | 하나만 깨움 vs 전부 깨움 | 항목 1개 추가 = signal, 상태 변화(close) = broadcast |
| close 프로토콜 | "더는 안 들어온다"를 알리고 모두 내보내는 절차 | 셧다운 시퀀스. 버그가 제일 많이 남는 곳 |

**condvar 는 락이 아니다**는 점만 확실히 하자. condvar 에는 "값"이 없다. 조건은 항상 `q->count == 0` 처럼 **mutex 로 보호되는 일반 변수**로 표현하고, condvar 는 그 변수가 바뀌었을 때 잠든 사람을 깨우는 **벨**일 뿐이다.

---

## 3. 문제 읽기

| 요구 | 설계에 주는 제약 |
|---|---|
| 여러 생산자, 소비자 하나 이상 | 인덱스와 count 를 **전부** 한 mutex 안에서 다룬다 |
| 가득 차면 생산자 블록 | "안 참"을 기다릴 condvar `not_full` 이 필요 |
| 비면 소비자 블록 | "안 빔"을 기다릴 condvar `not_empty` 가 필요 |
| close 후 남은 항목은 다 소비 | `pop` 은 "닫힘"이 아니라 **"닫힘 그리고 빔"** 일 때만 실패 |
| close 후 push 는 거부 | `push` 는 깬 뒤 `closed` 를 다시 확인해야 한다 |
| 대기 중인 스레드가 전부 빠져나와야 | close 는 두 condvar 모두에 **broadcast** |

condvar 를 **두 개** 쓰는 이유가 여기서 나온다. 하나로 합치면 "자리가 났다"는 신호에 소비자가, "항목이 생겼다"는 신호에 생산자가 깨어난다. 동작은 하지만(while 이 방어해 준다) 쓸데없이 깨우는 비용이 크다. **기다리는 조건이 다르면 condvar 를 나눈다.**

```c
typedef struct {
    AccessEvent    *buf;
    size_t          cap, head, tail, count;
    bool            closed;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} BQueue;
```

`count` 를 따로 두는 게 포인트다. `head == tail` 만으로는 "가득 참"과 "빔"을 구분할 수 없다. 02의 링버퍼는 한 칸을 비워서 구분하지만, 여기서는 이미 락을 잡으므로 `count` 를 그냥 세는 게 제일 읽기 쉽다. **불변식**을 먼저 적어 놓자 — 면접에서는 코드보다 이걸 먼저 말하는 게 낫다.

```
  0 <= count <= cap
  유효한 항목은 buf[head], buf[head+1], ... 총 count 개 (mod cap)
  head, tail, count, closed 는 오직 m 을 잡은 상태에서만 읽고 쓴다
```

---

## 4. 단계별로 만들기

### 4.1 초기화 — 셋 다 init 한다

```c
int bq_init(BQueue *q, size_t cap)
{
    if (cap == 0) return -1;
    q->buf = calloc(cap, sizeof *q->buf);
    if (!q->buf) return -1;
    q->cap = cap;
    q->head = q->tail = q->count = 0;
    q->closed = false;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return 0;
}
```

`cap == 0` 을 막는 이유는 `% q->cap` 에서 0 나누기가 나기 때문이다. `calloc` 실패를 삼키면 나중에 `q->buf[q->tail]` 이 널 포인터 역참조로 터진다. `pthread_cond_init(&cv, NULL)` 의 두 번째 인자는 속성이고 `NULL` 이면 기본값이다. **init 을 빼먹으면** 초기화 안 된 mutex 로 lock 하는 셈이라 정의되지 않은 동작이다 — 운 좋으면 즉시 죽고, 운 나쁘면 몇 시간 뒤에 죽는다.

### 4.2 push — 자리가 날 때까지 기다린다

```c
bool bq_push(BQueue *q, AccessEvent ev)
{
    pthread_mutex_lock(&q->m);
    /* while: spurious wakeup + 여러 생산자가 동시에 깨는 경우 방어 */
    while (q->count == q->cap && !q->closed)
        pthread_cond_wait(&q->not_full, &q->m);
```

`pthread_mutex_lock` 부터 `head/tail/count/closed` 를 만질 자격이 생긴다. `while` 의 조건은 "가득 찼고, 아직 안 닫혔다"로, 닫혔으면 기다려도 자리가 안 나므로 즉시 빠져나와야 한다. `pthread_cond_wait(&q->not_full, &q->m)` 은 **락을 놓고** 자고 깨어날 때 **락을 다시 잡고** 돌아온다 — 이 셋이 원자적이라 signal 을 놓치지 않는다.

`while` 대신 `if` 를 쓰면? 생산자가 여럿일 때 소비자의 `signal(not_full)` 하나에 두 생산자가 깨어나는 순간이 온다. 먼저 락을 잡은 쪽이 빈 칸을 채워 다시 `count == cap` 이 되는데, 둘째는 조건을 다시 안 보고 그대로 덮어쓴다 → **이벤트 하나가 소리 없이 사라진다.**

### 4.3 push — 깬 뒤 closed 재확인, 삽입, 그리고 unlock 먼저

```c
    if (q->closed) {                      /* close 이후 push는 거부 */
        pthread_mutex_unlock(&q->m);
        return false;
    }
    q->buf[q->tail] = ev;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_mutex_unlock(&q->m);          /* unlock 먼저 → 깨어난 소비자가 곧장 lock 획득 */
    pthread_cond_signal(&q->not_empty);
    return true;
}
```

`while` 을 빠져나오는 경우는 두 가지다 — 자리가 났거나, 닫혔거나. 그래서 **바로 다음 줄에서 `closed` 를 다시 확인**한다. 반환값 `false` 는 "이 이벤트는 큐에 못 들어갔다"는 뜻이고, 버릴지 로그할지는 호출자가 정한다. **여기서 `unlock` 을 빼먹으면** 그 자리에서 전체가 멈춘다. 에러 경로의 unlock 누락이 동시성 버그 1위라, 중간 `return` 지점을 손가락으로 짚어 가며 확인하는 습관을 들인다.

삽입 세 줄 사이에는 불변식이 깨져 있다(값은 들어갔는데 count 는 아직 옛날 값). 그래서 **락 안**이어야 한다. 항목 하나가 생겼으니 소비자 하나만 깨우면 되므로 `signal` 이다. 순서에도 주목하자 — 락을 쥔 채로 signal 하면 깨어난 소비자가 `cond_wait` 안에서 락을 다시 잡으려다 **즉시 다시 잠든다**. 둘 다 정확하지만 후자가 빠르고, 면접에서 짚으면 좋은 디테일이다.

### 4.4 pop — 대칭, 그리고 종료 판정

```c
bool bq_pop(BQueue *q, AccessEvent *out)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);

    if (q->count == 0) {                  /* closed && empty → 종료 신호 */
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}
```

거의 거울 대칭인데 **한 곳이 다르다**. 종료 판정이 `if (q->closed)` 가 아니라 `if (q->count == 0)` 이다. `while` 을 빠져나온 시점에 `count == 0` 이라면 조건상 반드시 `closed` 도 참이므로, 이 검사는 곧 **"닫혔고 그리고 비었다"** 를 뜻한다. `if (q->closed) return false;` 로 썼다면 close 순간 큐에 남아 있던 이벤트가 그대로 버려진다 — 출입 감사 로그에서는 그게 바로 사고다.

### 4.5 close — broadcast 두 번

```c
void bq_close(BQueue *q)
{
    pthread_mutex_lock(&q->m);
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    /* 대기 중인 모든 스레드를 깨워야 한다 → signal이 아니라 broadcast */
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}
```

`closed` 도 공유 변수이므로 **락을 잡고** 바꾼다. 그냥 대입만 하면 자고 있는 스레드가 그 변화를 볼 보장이 없다. `broadcast` 인 이유는 close 가 **모두에게 해당되는 상태 변화**이기 때문이다. `signal` 이면 한 명만 깨어나 빠져나오고 나머지는 영원히 자므로 `pthread_join` 이 끝나지 않는다. 두 condvar 모두 깨워야 한다는 것도 잊기 쉽다 — `not_empty` 만 깨우면 큐가 가득 찬 채로 `not_full` 에서 자던 생산자가 남는다.

---

## 5. 전체 흐름 따라가기

`cap = 2` 인 작은 큐로, 생산자 P1/P2 와 소비자 C 가 어떻게 맞물리는지 보자.

```
 시간 ↓  P1                      P2                      C                 count
  t0    lock                                                                 0
  t1    count(0)!=cap → 삽입                                                 1
  t2    unlock + signal(not_empty)
  t3    lock                                                                 1
  t4    count(1)!=cap → 삽입                                                 2
  t5    unlock + signal(not_empty)
  t6                            lock                                          2
  t7                            count==cap → cond_wait(not_full)  ← 락 놓고 잠  2
  t8                                                    lock                  2
  t9                                                    count!=0 → pop        1
  t10                                                   unlock + signal(not_full)
  t11                           깨어남 + 락 재획득                             1
  t12                           while 재검사: count(1)!=cap → 통과             1
  t13                           삽입 / unlock / signal(not_empty)             2
```

t7 이 핵심이다. P2 는 **락을 쥔 채로 자지 않는다**. `cond_wait` 이 락을 놓아 주기 때문에 t8 에서 소비자가 락을 잡을 수 있다. 락을 쥔 채로 잤다면 소비자가 영원히 못 들어오고 아무도 자리를 비워주지 못해 **데드락**이다. t12 도 중요하다 — 깨어났다고 바로 삽입하지 않고 **조건을 다시 본다**. 여기서는 통과하지만 생산자가 더 많았다면 다른 생산자가 그 자리를 이미 채웠을 수 있다.

종료는 이렇게 흐른다.

```
 main                                    소비자 C
 ─────────────────────────────────────   ──────────────────────────────────
 join(prod) → 생산 끝 / bq_close()
   closed = true + broadcast(둘 다)  ──▶  깨어남, while: count!=0 → 드레인
 join(cons)                        ◀──   count==0 → return false → 종료
 bq_destroy()   ← join 뒤에! 앞이면 use-after-free
```

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 조건 검사에 `while` 대신 `if` | 아주 가끔 count 언더플로, 쓰레기 이벤트, 항목 유실 | 깨어났다고 조건이 맞다는 보장이 없다 | 항상 `while (조건) cond_wait(...)` |
| close 에서 `signal` 사용 | `pthread_join` 이 안 끝나고 프로그램이 종료 안 됨 | 하나만 깨어나고 나머지는 계속 잠 | 두 condvar 모두 `broadcast` |
| `pop` 종료 조건을 `if (q->closed)` 로 | close 시점에 남아 있던 이벤트가 사라짐 | 드레인 없이 즉시 종료 | `if (q->count == 0)` 로 "닫힘 그리고 빔" 검사 |
| 에러/거부 경로에서 `unlock` 누락 | 그 시점부터 전체 정지, CPU 0% | 락이 영원히 잡혀 있음 | 모든 `return` 지점에서 unlock 확인 |
| `closed` 를 락 없이 수정 | 대기 중인 스레드가 close 를 못 봄, 간헐적 hang | 가시성 보장이 없다 | 락 잡고 수정 후 broadcast |
| condvar 하나로 합침 | 동작은 하지만 느리고 엉뚱한 쪽이 깨어남 | 기다리는 조건이 둘인데 벨이 하나 | `not_full` / `not_empty` 분리 |
| `join` 전에 `bq_destroy` | 간헐적 크래시, TSan/ASan 경고 | 아직 도는 스레드가 해제된 mutex 접근 | close → join → destroy 순서 고정 |

---

## 7. 직접 확인하기

```sh
make sol N=01
```

```
consumed=8000 (expect 8000), sum=7996000 (expect 7996000)
01 PASS
```

`consumed` 는 생산자 4개 × 2000개 = 8000. `sum` 은 seq 합이라 값이 정해져 있다. **개수만 세지 않고 합까지 검증**하는 게 포인트다 — 개수만 맞고 내용이 섞이는 버그를 잡는다. self test 는 큐 용량을 일부러 8로 잡아 블로킹 경로를 반드시 지나가게 만들고(`bq_init(&g_q, 8)`), 소비자 쪽에는 순서 검증도 넣었다.

```c
assert(ev.seq == last_seq[ev.door_id] + 1);
```

생산자별로 seq 가 1씩 증가하는지 본다. 같은 생산자가 넣은 순서는 FIFO 로 보존돼야 하므로, 이게 깨지면 큐가 항목을 건너뛰었거나 덮어썼다는 뜻이다.

```sh
make tsan N=01
```

`make tsan` 은 `starters/` 가 있으면 **내가 짠 구현**을 검사한다. 통과하면 `01 PASS` 로 끝나고 경고가 하나도 안 나온다. 락을 빠뜨렸다면 이런 게 뜬다.

```
WARNING: ThreadSanitizer: data race (pid=...)
  Write of size 8 by thread T2:          #0 bq_push ... 01_bounded_queue.c:72
  Previous read of size 8 by thread T1:  #0 bq_pop  ... 01_bounded_queue.c:81
```

"같은 주소를 두 스레드가 동기화 없이 만졌다"는 뜻이고 두 스택이 정확히 어느 줄인지 알려준다. 결과가 우연히 맞아도 잡아준다는 게 TSan 의 가치다. 프로그램이 끝나지 않고 멈춰 있으면 close/broadcast 쪽 버그다 — `Ctrl-C` 로 끊고 `bq_close` 가 두 condvar 모두에 broadcast 하는지부터 본다.

---

## 8. 면접에서 말하기

- "mutex 하나로 큐 상태 전체를 보호하고, 기다리는 조건이 둘이라 condvar 를 `not_full` / `not_empty` 로 나눴습니다. 조건 검사는 spurious wakeup 때문에 항상 `while` 입니다."
- "종료는 `closed` 를 락 안에서 세우고 두 condvar 에 broadcast 합니다. close 는 모두에게 해당되는 상태 변화라 signal 로는 부족합니다."
- "`pop` 은 '닫혔다'가 아니라 '닫혔고 그리고 비었다'일 때만 실패를 반환합니다. 그래야 close 이후에도 남은 이벤트를 전부 드레인할 수 있습니다."
- "출입 이벤트는 감사 로그라 유실이 곧 사고입니다. 그래서 drop 이 아니라 블로킹으로 backpressure 를 겁니다. 센서 텔레메트리라면 반대로 최신값만 남기는 설계를 택합니다."
- "정리 순서는 close → join → destroy 입니다. join 전에 destroy 하면 use-after-free 입니다."

- "One mutex protects the whole queue state; I use two condition variables because there are two distinct wait conditions — not full and not empty."
- "The predicate is always checked in a `while` loop, not an `if` — spurious wakeups are allowed by POSIX, and another thread may have taken the slot before we reacquire the lock."
- "`close()` sets the flag under the lock and then broadcasts on both condvars. A signal would release only one waiter and the rest would hang in join."
- "`pop()` returns false only when the queue is closed **and** empty, so consumers drain everything that was already enqueued."
- "Shutdown order is close, join, then destroy — destroying synchronization objects while a thread may still touch them is undefined behavior."

---

## 9. 요약 & 체크리스트

bounded blocking queue 는 동시성 코드의 "Hello, world" 다. 상태 전체를 mutex 하나로 묶고, 기다리는 조건마다 condvar 를 하나씩 붙이고, 조건은 `while` 로 검사하고, 종료는 플래그 + broadcast + 드레인으로 처리한다. 평가받는 건 코드가 도는지가 아니라 **while, broadcast, 종료 프로토콜, 그리고 "여기서 왜 blocking 이고 저기서는 왜 drop 인가"를 스스로 짚는가**다.

- [ ] `while (조건) pthread_cond_wait(cv, m)` 형태를 손으로 쓸 수 있다
- [ ] `cond_wait` 이 락을 놓고 자고 다시 잡고 돌아온다는 걸 설명할 수 있다
- [ ] `if` 로 썼을 때 깨지는 시나리오를 타임라인으로 그릴 수 있다
- [ ] close 에서 broadcast 를, push/pop 에서 signal 을 쓰는 기준을 말할 수 있다
- [ ] `pop` 의 종료 조건이 `count == 0` 인 이유를 설명할 수 있다
- [ ] 모든 `return` 경로에서 unlock 이 되는지 확인하는 습관이 있다
- [ ] close → join → destroy 순서를 이유와 함께 말할 수 있다
- [ ] `make sol N=01` 과 `make tsan N=01` 을 둘 다 통과시켜 봤다
