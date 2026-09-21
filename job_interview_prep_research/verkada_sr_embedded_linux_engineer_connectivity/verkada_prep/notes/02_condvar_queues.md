# 🔐 조건 변수 & 블로킹 큐 (Condition Variables & Blocking Queues) — Q11~20

> 이 세트가 **Verkada 2차 인터뷰 1순위**다. 리크루터 메일이 "thread safety"와
> "synchronization — mutexes, condition variables"를 명시했고, 그 둘을 한 문제로
> 확인하는 업계 표준 문제가 **bounded blocking queue**다. GC31-E 게이트웨이에서
> 도어·센서·모뎀 스레드가 이벤트를 만들고 업로더가 LTE로 올리는 구조 그대로라
> "실제 제품 얘기"로 이어가기도 가장 쉽다. 평가되는 건 돌아가는 코드가 아니라
> **while-wait / broadcast / 종료 프로토콜 / drop 카운터**를 스스로 짚는가다.

---

## 1. 핵심 아이디어

조건 변수(condition variable)는 **락이 아니다.** "지금은 조건이 안 맞으니 나를
재워두고, 누가 조건을 바꾸면 깨워줘"라는 **대기열(wait queue)** 이다. 항상
mutex와 한 쌍으로 쓴다.

```
           ┌──────────────── mutex m ────────────────┐
생산자 ──▶ │  count == cap ?  → cond_wait(not_full)  │ ──▶ 잠듦
           │  아니면 넣고 count++                     │
           └─────────────────────────────────────────┘
                      │ cond_signal(not_empty)
                      ▼
           ┌──────────────── mutex m ────────────────┐
소비자 ──▶ │  count == 0 ?    → cond_wait(not_empty) │ ──▶ 잠듦
           │  아니면 꺼내고 count--                   │
           └─────────────────────────────────────────┘
```

`pthread_cond_wait(cv, m)` 한 줄이 원자적으로 하는 일:

1. mutex를 **놓는다**
2. 이 스레드를 cv의 대기열에 넣고 **잠든다**
3. 깨어나면 mutex를 **다시 잡고** 리턴한다

1과 2가 원자적이기 때문에 "조건을 확인하고 wait로 들어가는 사이에 signal이
지나가버리는" **lost wakeup**이 생기지 않는다. 그래서 조건 검사와 wait는 반드시
같은 mutex 안에서 해야 한다.

### 상태 표현: `head + count`

```c
원소 i  ==  buf[(head + i) % cap]
push    :  buf[(head + count) % cap] = ev; count++;
pop     :  *out = buf[head]; head = (head+1) % cap; count--;
```

`head/tail` 두 인덱스만 쓰면 `head == tail`이 full인지 empty인지 모호하다.
`head + count` 표현은 그 모호성이 **구조적으로 없다** — `count == 0`이 empty,
`count == cap`이 full. 인터뷰에서 링버퍼 함정 논쟁을 건너뛸 수 있다.

---

## 2. `while`이지 `if`가 아니다 ⭐

```c
while (q->count == q->cap && !q->closed)      // ✅
    pthread_cond_wait(&q->not_full, &q->m);

if (q->count == q->cap) pthread_cond_wait(...); // ❌ 감점
```

이유가 **세 가지**다. 하나만 말하면 반쪽짜리 답이다.

| 이유 | 설명 |
|---|---|
| **① spurious wakeup** | POSIX가 명시적으로 허용한다. 시그널 처리나 구현 최적화 때문에 아무도 signal하지 않았는데 깨어날 수 있다. |
| **② stolen wakeup** | 깨어나 mutex를 다시 잡기까지 시간이 걸린다. 그 사이 다른 생산자가 락을 먼저 잡고 슬롯을 채가면, 내가 깼을 때 큐는 다시 가득 차 있다. (**대기자 여럿 = 필연**) |
| **③ 다중 술어(predicate)** | `closed` 같은 조건이 추가되면 깨어난 이유가 "슬롯이 생겨서"인지 "종료라서"인지 재확인해야 한다. |

> **한 줄 정리**: `cond_wait`는 "조건이 참이 됐다"를 보장하지 않는다.
> **"조건을 다시 확인해볼 만하다"** 는 힌트일 뿐이다.

---

## 3. `signal` vs `broadcast` — 언제 무엇을

| | 깨우는 수 | 쓰는 곳 |
|---|---|---|
| `pthread_cond_signal` | 대기자 중 **하나** | 자원이 **1개** 생겼을 때 (push 1개 → not_empty, pop 1개 → not_full, semaphore post) |
| `pthread_cond_broadcast` | 대기자 **전부** | **종료(close/shutdown)**, 한 번에 **여러 슬롯**이 빌 때(batch pop), 대기자마다 **술어가 다를 때** |

**종료 때 signal을 쓰면 확실히 hang한다.** 대기자가 3명인데 1명만 깨우고, 그
1명은 false를 받고 그냥 리턴해버리므로 나머지 2명은 영원히 잠든다.
(깨어난 스레드가 릴레이로 다시 signal하는 방식도 가능하지만, 종료 경로에서
그 복잡도를 감수할 이유가 없다.)

반대로 평상시 push마다 broadcast를 쓰면 **thundering herd** — N명이 깨어나
1명 빼고 전부 다시 잠든다. 컨텍스트 스위치 낭비.

### signal은 unlock **뒤**에 (성능 최적화)

```c
q->count++;
pthread_mutex_unlock(&q->m);        // 먼저 놓고
pthread_cond_signal(&q->not_empty); // 그 다음 깨운다
```

락을 쥔 채 signal하면 깨어난 스레드가 곧바로 mutex에서 다시 막힌다
(**hurry-up-and-wait**). 정확성 문제는 아니고 순수 성능 최적화다 — 면접에서
"정확성에는 영향 없고 컨텍스트 스위치를 한 번 아끼는 최적화입니다"라고
정확히 구분해서 말하면 점수가 붙는다.
(단, condvar를 파괴/해제하는 코드가 동시에 돌 수 있는 구조라면 락 안에서
signal하는 편이 안전하다 — trade-off로 언급할 것.)

---

## 4. 종료 프로토콜 — 인터뷰가 가장 깊게 파는 곳

큐 자체보다 **"어떻게 깔끔하게 끝내는가"** 가 진짜 문제다. 규칙 5개:

```c
void bq_close(bq_t *q) {
    pthread_mutex_lock(&q->m);
    if (q->closed) { pthread_mutex_unlock(&q->m); return; }  // ④ 멱등
    q->closed = true;                                        // ① 락 안에서
    pthread_mutex_unlock(&q->m);
    pthread_cond_broadcast(&q->not_empty);                   // ② 둘 다
    pthread_cond_broadcast(&q->not_full);
}
```

1. **`closed`는 mutex 안에서 세팅한다** — 그것이 condvar 술어의 일부이기 때문.
   락 밖에서 세우면 "조건 확인 → closed 세팅 → broadcast → wait 진입" 순서로
   **lost wakeup**이 생긴다.
2. **두 condvar를 모두 broadcast** — `not_full`을 빼먹으면 가득 찬 큐에서
   대기하던 생산자가 영원히 잠든다. "종료가 안 돼요" 버그 1위.
3. **`closed && empty` 일 때만 pop이 false** — `closed`만으로 false를 주면
   close 시점에 큐에 남아 있던 이벤트가 통째로 사라진다(drain 실패).
   반면 **push는 closed 즉시 거부**한다 — 비대칭이 의도된 설계다.
4. **이중 close는 멱등** — watchdog 경로와 정상 종료 경로가 둘 다 부를 수 있다.
5. **close ≠ destroy** — `close()` → 모든 스레드 `join()` → `destroy()` 순서.
   대기자가 있는 상태에서 mutex/condvar를 파괴하면 UB다.

---

## 5. 논블로킹·타임아웃·배치 변형

### `try_push` / `try_pop`
조건이 안 맞으면 **기다리지 않고 즉시 false**. 실시간 경로(제어 루프, 오디오
콜백, ISR 하위 스레드)는 블록하면 데드라인을 놓친다.
> ⚠️ 용어 주의: **논블로킹(non-blocking) ≠ 무락(lock-free)**. mutex는 여전히
> 잡는다 — 다만 *조건*을 기다리지 않을 뿐이다.

### timed pop — 이식성 함정
```c
#if defined(__APPLE__)
    struct timespec rel = { ms/1000, (ms%1000)*1000000L };
    rc = pthread_cond_timedwait_relative_np(cv, m, &rel);   // macOS 확장
#else
    clock_gettime(CLOCK_REALTIME, &ts); /* + ms, nsec 캐리 */
    rc = pthread_cond_timedwait(cv, m, &ts);                // 절대시각
#endif
```

| 플랫폼 | 방법 | 주의 |
|---|---|---|
| **Linux (실제 타깃)** | `pthread_condattr_setclock(&a, CLOCK_MONOTONIC)` + 절대시각 | 정석. NTP가 시계를 뒤로 돌려도 안전 |
| **macOS (연습 환경)** | `pthread_cond_timedwait_relative_np` | `condattr_setclock` 자체가 없다 |
| 공통 | 그냥 `CLOCK_REALTIME` 절대시각 | 시계가 뒤로 점프하면 타임아웃이 늘어난다 |

그리고 **재대기할 때 남은 시간을 다시 계산**해야 한다. spurious wakeup마다
`timeout_ms`를 새로 주면 타임아웃이 영원히 갱신된다. monotonic 기준 **절대
마감(deadline)** 을 먼저 고정하고 `remain = deadline - now()`를 넘긴다.

### batch pop
LTE 업로드는 요청 하나당 헤더·핸드셰이크 오버헤드가 크다. 이벤트 1개마다 올리면
전력과 데이터 요금이 전부 오버헤드로 나간다. **있는 만큼 한 번에** 꺼내면
락 획득 횟수도 n분의 1. 여러 슬롯이 동시에 비므로 깨우기는 **broadcast**.

---

## 6. 가득 찼을 때 무엇을 버릴 것인가 (backpressure)

링크가 끊기면 업로더가 막히고, 그 사이에도 센서 스레드는 계속 샘플을 만든다.
메모리는 유한하다 → **정책을 고른다.**

| 정책 | 동작 | 적합한 데이터 | 부적합 |
|---|---|---|---|
| `BLOCK` | 생산자를 막는다 | 손실이 곧 버그인 감사 이벤트 | 실시간 경로(막히면 데드라인 미스), ISR |
| `DROP_NEWEST` | 방금 것을 버린다 | **과거 이력** 보존 — 로그, 감사 추적 | 최신 상태가 중요한 데이터 |
| `DROP_OLDEST` | 가장 오래된 것을 밀어낸다 | **최신 상태** 보존 — 텔레메트리, 온도, 링크 품질, 알람 | 순서 보장이 필요한 스트림 |

**정책은 데이터의 의미가 결정한다.** 그리고 버린 개수는 **반드시 카운트해서 함께
보고**한다 — 조용한 손실(silent loss)이 디버깅하기 제일 어렵다.

검증 가능한 불변식 하나를 코드에 박아두면 인터뷰에서 강력하다:

```
produced == consumed + dropped + queued
```

---

## 7. 워커 풀 + graceful shutdown

```
submit ──▶ [ task queue (bounded) ] ──▶ worker × N ──▶ fn(arg)  ★ 락 밖에서
              cond: work / slot
```

- **작업 실행은 반드시 락 밖에서.** 락을 쥔 채 `fn()`을 돌리면 워커 N개가
  직렬화되어 풀이 스레드 1개짜리가 된다.
- **종료 모드 2개**: `DRAIN`(남은 작업 다 처리 — 정상 재부팅) vs
  `STOP`(큐는 버림 — watchdog/긴급). 버린 개수는 `discarded`로 센다.
- **`shutdown`과 `destroy`를 분리한다.** 합쳐두면 shutdown 직후 `submit`이
  해제된 큐를 만져 **use-after-free**. `shutdown` = 상태전환 + broadcast + join,
  `destroy` = free. 그 사이에 카운터를 읽어볼 수 있는 것도 장점.
- **join을 빼먹으면?** 워커가 아직 도는 중에 메모리가 free된다 — UAF.
- 카운터 세 개(`executed` / `discarded` / `rejected`)로 "몇 개가 어디로 갔는지"
  전부 설명할 수 있어야 한다.

---

## 8. counting semaphore를 직접 만들기

"동시 업로드는 최대 2개" 같은 자원 제한. macOS는 POSIX 무명 세마포어
(`sem_init`)가 **미구현**이라 직접 만드는 게 이식성 정답이자 단골 문제다.

```c
void csem_wait(csem_t *s) {
    pthread_mutex_lock(&s->m);
    while (s->value == 0)                 // if 아니라 while
        pthread_cond_wait(&s->cv, &s->m);
    s->value--;
    pthread_mutex_unlock(&s->m);
}
void csem_post(csem_t *s) {
    pthread_mutex_lock(&s->m);
    s->value++;
    pthread_mutex_unlock(&s->m);
    pthread_cond_signal(&s->cv);          // 자리 1개 → 1명만
}
```

불변식: `value >= 0`, 그리고 **동시에 자원을 쓰는 스레드 수 ≤ 초기 value**.
`value == 1`이면 mutex처럼 보이지만 다르다 — **세마포어는 소유자가 없어서
다른 스레드가 post해도 된다**(mutex는 잠근 스레드가 풀어야 한다).

---

## 9. 흔한 함정 (인터뷰 감점 포인트)

- [ ] `if (cond) cond_wait(...)` — **가장 큰 감점**. 이유 3가지를 말할 것.
- [ ] 종료 때 `signal` — 대기자 하나만 깨어나고 나머지는 영원히 잠듦.
- [ ] `not_full` broadcast 누락 — 가득 찬 큐의 생산자가 종료 못 함.
- [ ] `closed`를 락 밖에서 세팅 → lost wakeup.
- [ ] `closed`만으로 pop을 false → 남은 이벤트 유실. **`closed && empty`** 여야 함.
- [ ] 락 잡은 채로 I/O·작업 실행 → 풀이 직렬화, 지연 폭증.
- [ ] `shutdown`에서 free까지 해버림 → 직후 `submit`이 UAF.
- [ ] `join` 누락 상태로 `destroy` → 대기 중 mutex/condvar 파괴 = UB.
- [ ] drop을 조용히 함 → 카운터 없으면 현장에서 원인 추적 불가.
- [ ] 타임아웃 재대기에서 `timeout_ms`를 새로 줌 → 타임아웃이 영원히 갱신.
- [ ] `cap == 0`을 방어 안 함 → `% cap`에서 0 나누기.
- [ ] `volatile`로 동기화를 대신하려 함 — 원자성도 순서도 보장하지 않는다.

---

## 10. 면접에서 말할 것 (한국어 + 영어)

### 시작할 때 (요구사항 확인)
- "생산자와 소비자가 각각 몇 개인가요? 큐가 가득 차면 블록인가요, 드롭인가요?
  종료는 어떻게 알리나요?"
  > *"How many producers and consumers? On a full queue, do we block the producer
  > or drop? And how does shutdown get signalled?"*
- "API를 먼저 정하겠습니다 — init / push / pop / close / destroy. 이 다섯 개면
  사용자가 오용할 여지가 적습니다."
  > *"Let me pin the API first: init, push, pop, close, destroy. That's a small
  > enough surface that callers can't easily misuse it."*

### while vs if
- "조건 검사는 `if`가 아니라 `while`입니다. 이유가 셋인데요 — POSIX가 허용하는
  spurious wakeup, 깨어나서 락을 다시 잡는 사이에 다른 스레드가 슬롯을 채가는
  경우, 그리고 `closed` 같은 술어가 여러 개일 때 깨어난 이유를 재확인해야 하기
  때문입니다."
  > *"The predicate check is a `while` loop, not an `if`. Three reasons: POSIX
  > permits spurious wakeups; between waking and re-acquiring the mutex another
  > thread can steal the slot I was woken for; and with more than one predicate —
  > here, `closed` — I have to re-check why I woke up."*
- "`cond_wait`가 리턴했다는 건 조건이 참이라는 뜻이 아니라, 다시 확인해볼
  만하다는 힌트일 뿐입니다."
  > *"A return from `cond_wait` is a hint that the predicate is worth re-checking
  > — not a guarantee that it holds."*

### signal vs broadcast
- "평소에는 한 번에 자리가 하나 생기니까 `signal`이면 충분합니다. 하지만 종료
  때는 대기자가 몇 명인지 모르기 때문에 반드시 `broadcast`입니다."
  > *"In steady state one slot frees at a time, so `signal` is enough. On shutdown
  > I don't know how many waiters there are, so it has to be `broadcast`."*
- "`broadcast`를 남발하면 thundering herd가 생깁니다 — N개가 깨어나 하나 빼고
  전부 다시 잠들죠."
  > *"Broadcasting on every push causes a thundering herd: N threads wake up and
  > all but one go straight back to sleep."*

### 락 밖에서 일하기
- "작업 실행이나 I/O는 락을 놓고 합니다. 임계 구역은 상태 변경만 담당합니다.
  락을 쥔 채 작업을 돌리면 워커 N개가 사실상 하나가 됩니다."
  > *"Work and I/O happen outside the lock — the critical section only mutates
  > state. If I ran the task while holding the mutex, N workers would collapse
  > into one."*
- "signal은 unlock 뒤에 보냅니다. 정확성에는 영향이 없고, 깨어난 스레드가
  곧바로 락에서 다시 막히는 걸 피하는 최적화입니다."
  > *"I signal after unlocking. It doesn't change correctness — it just avoids
  > waking a thread that immediately blocks again on the mutex."*

### drop 카운터
- "버린 데이터는 반드시 카운트해서 함께 보고합니다. 조용한 손실이 현장에서
  제일 디버깅하기 어렵습니다."
  > *"Every dropped item is counted and reported. Silent loss is the hardest
  > failure to debug in the field."*
- "드롭 정책은 데이터의 의미가 결정합니다. 최신 상태가 중요한 텔레메트리는
  drop-oldest, 이력이 중요한 감사 로그는 drop-newest, 손실이 곧 버그인
  경로만 block입니다."
  > *"The drop policy follows the meaning of the data: drop-oldest for telemetry
  > where only the latest state matters, drop-newest for audit logs where history
  > matters, and blocking only where losing an item is a correctness bug."*
- "불변식 하나를 테스트로 박아둡니다 — produced == consumed + dropped + queued."
  > *"I assert one invariant in the stress test: produced equals consumed plus
  > dropped plus what's still queued."*

### edge case 목록 (스스로 먼저 읊을 것)
- "제가 확인할 edge case는 이겁니다: 빈 큐 pop, 가득 찬 큐 push, close 후 push,
  close 시점에 대기 중이던 스레드, 이중 close, close 후 남은 항목의 drain,
  spurious wakeup, 그리고 destroy 전에 join이 끝났는지."
  > *"The edge cases I want to cover: pop on empty, push on full, push after
  > close, threads already blocked when close happens, a double close, draining
  > items that were still queued at close time, spurious wakeups, and making sure
  > every thread is joined before destroy."*
- "close와 destroy를 분리합니다. close는 상태 전환과 broadcast만 하고, 자원
  해제는 join이 끝난 뒤 destroy에서 합니다 — 안 그러면 use-after-free입니다."
  > *"I keep close and destroy separate: close only flips state and broadcasts;
  > freeing happens in destroy after every thread is joined. Otherwise it's a
  > use-after-free."*

### 테스트 전략
- "단위 테스트로 FIFO 순서와 경계(빈/가득)를 확인하고, 스트레스 테스트로
  불변식을 확인한 다음, ThreadSanitizer로 돌려서 race가 없는지 봅니다."
  > *"Unit tests for FIFO order and the empty/full boundaries, a stress test that
  > asserts the accounting invariant, and then a run under ThreadSanitizer to
  > confirm there's no data race."*

---

## 11. 이 문제 세트 (Q11~20)

| # | 함수 | 난이도 | 포인트 |
|---|---|---|---|
| 11 | `bq_init` / `bq_destroy` / `bq_count` | easy | 저장소 소유권(owns_buf), 필드 불변식, cap==0 방어 |
| 12 | `bq_push` | medium | while-wait, closed 거부, unlock 후 signal |
| 13 | `bq_pop` | medium | while-wait, `closed && empty` 판정 |
| 14 | `bq_close` / `bq_is_closed` | medium | 락 안에서 플래그, 두 condvar broadcast, drain, 멱등 |
| 15 | `bq_try_push` / `bq_try_pop` | easy | 논블로킹 ≠ 무락, 실패 시 상태 불변 |
| 16 | `cond_wait_ms` / `bq_pop_timed` | hard | macOS `_relative_np` vs 절대시각, monotonic deadline 재계산 |
| 17 | `bq_pop_batch` | medium | min(count,max), FIFO, 여러 슬롯 → broadcast |
| 18 | `tq_*` (drop 정책) | medium | BLOCK/DROP_NEWEST/DROP_OLDEST, dropped 카운터, 불변식 |
| 19 | `pool_*` | hard | DRAIN vs STOP, 락 밖 실행, shutdown↔destroy 분리, join |
| 20 | `csem_*` | easy | mutex+condvar로 세마포어, signal로 충분, value>=0 |

---

## 12. 체크리스트

- [ ] `while (predicate) cond_wait(...)` 를 손이 저절로 치는가 (이유 3개 암기)
- [ ] 종료는 `broadcast`, 평상시는 `signal` — 이유를 즉답할 수 있는가
- [ ] `closed`를 mutex 안에서 세팅하는 이유(lost wakeup)를 말할 수 있는가
- [ ] `closed && empty` 비대칭(push는 즉시 거부)을 설명할 수 있는가
- [ ] 이중 close / close 후 push / 대기 중 close 를 먼저 언급하는가
- [ ] `close → join → destroy` 순서와 그 이유(UAF)를 말할 수 있는가
- [ ] 작업 실행을 락 밖에서 하는 이유를 말할 수 있는가
- [ ] drop 정책 3종과 각각 어떤 데이터에 맞는지 예를 들 수 있는가
- [ ] `produced == consumed + dropped + queued` 불변식을 테스트로 쓸 수 있는가
- [ ] `pthread_cond_timedwait`의 절대시각/기준 시계 문제와 Linux
      `pthread_condattr_setclock(CLOCK_MONOTONIC)` 해법을 말할 수 있는가
- [ ] 논블로킹(non-blocking)과 무락(lock-free)의 차이를 구분해 쓰는가
- [ ] mutex+condvar로 counting semaphore를 10줄 안에 짤 수 있는가
- [ ] TSan으로 검증한다는 얘기를 먼저 꺼내는가
