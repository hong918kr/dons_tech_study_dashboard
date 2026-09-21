# 06. Worker pool + graceful shutdown — 동시성 버그는 종료 코드에 산다

> **이 노트를 다 읽으면**
> - 워커 풀의 제출/실행/종료 경로를 락 규칙과 함께 직접 쓸 수 있다
> - drain 종료와 즉시 종료의 차이, 그리고 각각이 필요한 상황을 구분할 수 있다
> - 종료에서 터지는 edge case 다섯 가지를 먼저 꺼내 말할 수 있다
>
> **선행**: `00_start_here` 의 §4 조건 변수, §7 종료와 정리. `01_bounded_queue` 가 이 풀의 큐 부분이다.
>
> **연습**: `make run N=06` (내 구현) · `make sol N=06` (모범답안) · `make tsan N=06`

---

## 0. 한 문장으로

**워커 풀은 "일감 큐 + 고정된 스레드 N개"이고, 어려운 부분은 만드는 쪽이 아니라 끝내는 쪽이다.**
게이트웨이의 로그 업로드·설정 동기화·헬스체크를 스레드를 매번 만들지 않고 처리하는 구조이며,
재부팅·워치독 상황에서 깔끔히 멈출 수 있어야 한다.

---

## 1. 왜 필요한가 — 비유로 시작

식당 주방에 요리사 4명이 있다. 주문표를 한 장 뽑아 요리하고, 끝나면 다시 줄을 본다. 줄이 비면 팔짱 끼고 기다린다(= `cond_wait`). 문제는 영업 종료다.

```
 DRAIN (정상 재부팅) "새 주문은 안 받고, 꽂힌 주문표는 다 만들고 퇴근합니다"
 STOP  (워치독/긴급)  "지금 불 위에 있는 것만 끝내고 나머지 주문표는 버립니다"
 잘못된 마감          사장이 불만 끄고 나감 → 자던 요리사는 영원히 주방에 남음
                      = broadcast를 안 해서 워커가 cond_wait에서 못 깨어난 상태
```

베어메탈 대응: 태스크 큐에서 잡을 뽑아 도는 스케줄러 루프와 같다. `pthread_join` 을 빼먹고 큐를 `free` 하면 use-after-free다.

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 | Don이 아는 것과의 대응 |
| --- | --- | --- | --- |
| thread pool | 미리 만든 스레드 N개가 큐에서 일을 뽑아 처리 | 스레드 생성 비용과 개수 폭발을 막는다 | 고정 태스크 슬롯 |
| `TaskFn` | `void (*)(void *)` 함수 포인터 | 큐에 "무슨 일"을 담기 위해 | 콜백 테이블, ISR 벡터 |
| graceful shutdown | 진행 중인 것을 존중하며 멈추기(drain) | 끊긴 업로드는 데이터 손상 | 안전한 리셋 시퀀스 |
| `pthread_join` | 그 스레드가 끝날 때까지 기다림 | 자원 해제 시점을 정하기 위해 | 태스크 종료 확인 플래그 |
| `broadcast` vs `signal` | 전부 깨움 vs 하나만 깨움 | 종료는 전부 깨워야 한다 | 이벤트 플래그 전체 세트 |

---

## 3. 문제 읽기

요구를 쪼개면 상태 머신이 바로 나온다.

1. **종료 모드가 둘** → 불리언으로 부족하다. `POOL_RUNNING / POOL_DRAIN / POOL_STOP` 세 상태.
2. **drain은 남은 작업 실행** → 워커의 종료 조건이 "큐가 비었다 **또는** STOP"이다.
3. **즉시 종료는 큐 폐기** → 버린 개수를 `discarded` 로 세야 불변식이 성립한다.
4. **종료 후 submit 거부** → `pool_submit` 이 `false` 를 돌려주고 `rejected` 를 센다.
5. **자원 해제는 join 후** → `pool_shutdown` 이 join까지 끝내고 반환한다.

```c
typedef struct {
    pthread_t      *workers;
    size_t          nworkers;
    Task           *q;
    size_t          cap, head, count;
    PoolState       state;
    unsigned long   executed, rejected, discarded;
    pthread_mutex_t m;
    pthread_cond_t  work, slot;
} Pool;
```

조건 변수가 둘인 이유는 대기 이유가 둘이기 때문이다. 워커는 "일감이 없어서"(`work`), 제출자는
"자리가 없어서"(`slot`) 기다린다. 하나로 합치면 깨워야 할 쪽을 못 깨워 hang할 수 있다.
`executed`/`rejected`/`discarded` 는 05의 `dropped` 와 같다 — 세어두지 않으면 종료를 증명할 수 없다.

---

## 4. 단계별로 만들기

### 4-1. 워커가 일감을 꺼내는 부분 — 종료 판정이 여기 있다

```c
static bool pool_take(Pool *p, Task *out)
{
    pthread_mutex_lock(&p->m);
    while (p->count == 0 && p->state == POOL_RUNNING)
        pthread_cond_wait(&p->work, &p->m);
```

대기 조건은 "일감이 없고 **아직 running이면**" 잔다는 뜻이다. DRAIN이나 STOP으로 바뀌면 이 `while` 이
거짓이 되므로 broadcast로 깨어난 워커는 다시 자지 않는다. `&& p->state == POOL_RUNNING` 이 빠지면 종료를 알려도 워커가 계속 잠들어 join이 안 끝난다.

```c
    if (p->count == 0 || p->state == POOL_STOP) {
        if (p->state == POOL_STOP) {           /* 남은 작업은 버린다 */
            p->discarded += p->count;
            p->count = 0;
        }
        pthread_mutex_unlock(&p->m);
        return false;                          /* 워커 종료 */
    }
```

여기가 두 종료 모드를 가르는 조건이다. "큐가 비었거나 **또는** STOP"이므로, DRAIN 상태에서 일이
남아 있으면 둘 다 거짓이라 계속 일한다 — 이것이 drain이다. STOP이면 즉시 `false` 이고 남은 `count` 를
`discarded` 에 더한 뒤 0으로 만든다(먼저 도착한 워커가 비우므로 중복이 없다). `discarded` 를 안 세면
`executed + discarded == submitted` 가 깨진다.

```c
    *out = p->q[p->head];
    p->head = (p->head + 1) % p->cap;
    p->count--;
    p->executed++;
    pthread_mutex_unlock(&p->m);
    pthread_cond_signal(&p->slot);
    return true;
}
```

일감을 복사해 나오고 자리가 **하나** 났으니 `slot` 에 `signal` 로 충분하다(05의 배치 pop과 대비된다).

### 4-2. 일감 실행은 반드시 락 밖에서

```c
static void *worker_main(void *arg)
{
    Pool *p = arg;
    Task t;
    while (pool_take(p, &t))
        t.fn(t.arg);                           /* 작업 실행은 반드시 락 밖에서 */
    return NULL;
}
```

`pool_take` 는 락을 잡았다 놓고 나오므로 `t.fn(t.arg)` 는 락 없이 실행된다. 이게 절대적인 이유는 셋이다.

1. **병렬성**: 락을 쥔 채 실행하면 워커 4개여도 한 번에 하나만 일한다. 풀을 만든 의미가 없다.
2. **데드락**: 일감이 그 안에서 `pool_submit` 을 부르면 같은 mutex를 다시 잠그려다 데드락한다.
3. **지연**: 일감이 1초 걸리면 그동안 제출자도 다른 워커도 큐에 접근하지 못한다.

**락은 자료구조를 만지는 동안만 잡고 사용자 코드를 부를 때는 잡지 않는다** — 콜백을 부르는 모든 코드의 규칙이다.

### 4-3. 제출 — 두 번 검사한다

```c
bool pool_submit(Pool *p, TaskFn fn, void *arg)
{
    pthread_mutex_lock(&p->m);
    if (p->state != POOL_RUNNING) {            /* 종료 중에는 새 작업 거부 */
        p->rejected++;
        pthread_mutex_unlock(&p->m);
        return false;
    }
    while (p->count == p->cap && p->state == POOL_RUNNING)
        pthread_cond_wait(&p->slot, &p->m);    /* 큐가 차면 제출자가 기다린다 */
    if (p->state != POOL_RUNNING) {
        p->rejected++;
        pthread_mutex_unlock(&p->m);
        return false;
    }
```

상태 검사가 **두 번** 나온다. 처음 것은 "이미 종료 중인 풀에 제출"을, 두 번째 것은 "기다리는 동안
종료가 시작된 경우"를 막는다. `cond_wait` 은 락을 놓고 자므로 깨어나면 항상 다시 검사한다. 두 번째
검사가 없으면 종료 중인 큐에 일감이 들어가 실행되지도 `discarded` 로 세어지지도 않아 불변식이
깨진다. `rejected` 를 세는 것도 "왜 이 작업이 실행 안 됐지?"의 유일한 단서라 설계의 일부다.

그 아래는 05의 push와 같다. `p->q[(p->head + p->count) % p->cap] = (Task){ fn, arg };` 로 꼬리에 넣고
`p->count++`, unlock 뒤 `pthread_cond_signal(&p->work)`. 일감 하나면 워커 하나만 깨우면 된다 —
`broadcast` 면 4명이 깨어나 3명은 헛걸음한다(thundering herd).

### 4-4. 종료 — 상태, broadcast, join, free 순서

```c
void pool_shutdown(Pool *p, bool drain)
{
    pthread_mutex_lock(&p->m);
    p->state = drain ? POOL_DRAIN : POOL_STOP;
    pthread_mutex_unlock(&p->m);
    /* 잠든 워커와 제출자를 모두 깨운다 — signal이면 하나만 깨어나 hang */
    pthread_cond_broadcast(&p->work);
    pthread_cond_broadcast(&p->slot);

    for (size_t i = 0; i < p->nworkers; i++)
        pthread_join(p->workers[i], NULL);     /* join 후에만 자원 해제 */

    free(p->workers);
    free(p->q);
    pthread_mutex_destroy(&p->m);
    pthread_cond_destroy(&p->work);
    pthread_cond_destroy(&p->slot);
}
```

순서가 곧 정확성이다. 네 단계를 하나라도 바꾸면 버그가 난다.

1. **상태 변경은 락 안에서.** 락 밖에서 쓰면 워커가 `while` 조건을 평가하는 순간과 겹쳐 data race다.
2. **broadcast는 양쪽 다.** `work` 만 깨우면 `slot` 에서 자던 제출자가 남고, `signal` 이면 워커 1명만 깨어나 나머지는 join에서 영원히 멈춘다.
3. **join은 모든 워커에 대해.** 하나라도 빼먹으면 그 스레드가 아래에서 `free` 한 메모리를 계속 만진다.
4. **free는 join 뒤에.** 순서를 바꾸면 100% use-after-free다. ASan이 잡아주지만 이 순서는 외워두는 편이 낫다.

---

## 5. 전체 흐름 따라가기

STOP 모드 종료를 워커 2명 기준으로 따라간다.

```
 main                      W1                  W2              state   count
 ---------------------------------------------------------------------------
 submit × 10               task A 실행 중      cond_wait       RUNNING    9
 shutdown(drain=false)
   lock/state=STOP/unlock                                      STOP       9
   broadcast(work, slot)                       깨어남 → take():
   join(W1) 대기           task A 끝           state==STOP, count 이미 0
                           take(): state==STOP → return false → 종료
                           discarded += 9, count = 0          STOP       0
   join(W1)/join(W2) 반환  return false → 종료
   free(workers), free(q)
```

`discarded += 9` 를 먼저 도달한 워커가 처리하고 `count` 를 0으로 만든다는 점이 포인트다 —
두 워커가 각각 9씩 더하면 `executed + discarded == submitted` 가 깨진다. DRAIN 모드면
`state == POOL_STOP` 이 거짓이라 워커들이 큐가 빌 때까지 일하고, `count == 0` 이 된 순간에야 나간다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 종료 시 `signal` 사용 | `pthread_join` 에서 영구 hang | 워커 하나만 깨어나고 나머지는 계속 잠 | `broadcast` 를 양쪽 조건 변수에 |
| join 전에 `free` | 세그폴트, ASan use-after-free | 워커가 해제된 큐를 접근 | join 전부 끝낸 뒤 free |
| 락 잡은 채 `t.fn()` 호출 | 워커 N개인데 처리량이 1개분, 또는 데드락 | 사용자 코드가 같은 락을 다시 잡을 수 있다 | `pool_take` 에서 락을 놓고 나온 뒤 실행 |
| 종료 후 submit을 조용히 무시 | 작업이 사라졌는데 원인 불명 | 반환값도 카운터도 없음 | `false` 반환 + `rejected++` |
| `pool_shutdown` 두 번 호출 | double free, 이미 join한 스레드 join | 이 구현은 shutdown과 destroy가 붙어 있다 | 아래 설명대로 분리 + `state` 로 멱등화 |
| 오래 걸리는 blocking 작업 | 종료가 몇 초씩 지연 | 실행 중 작업은 끝까지 기다린다 | 작업에 cancel 플래그를 넘겨 스스로 빠져나오게 |

표의 마지막 두 줄은 따로 볼 값어치가 있다.

**blocking 작업과 종료 지연.** 실행 중인 일감은 끝까지 실행되므로 소켓 `recv` 에서 30초를 기다리면 종료도
30초 걸린다. 해법은 풀이 아니라 일감 쪽이다 — 취소 플래그(self-pipe / eventfd)를 인자로 넘겨 스스로
빠져나오게 하거나, 소켓이면 타임아웃 또는 `shutdown(fd, SHUT_RDWR)` 로 깨운다.

**shutdown 두 번 호출.** 이 파일의 `pool_shutdown` 은 종료와 자원 해제를 한꺼번에 하므로 두 번 부르면
이미 join한 스레드를 다시 join하고 이미 `free` 한 포인터를 다시 `free` 한다 — UB다. 테스트 case 3 주석도
`shutdown이 자원을 해제했으므로 여기서 submit하면 UAF` 라고 짚는다. 실무 API는 `pool_shutdown()`(상태만
바꾸고 join, 여러 번 안전)과 `pool_destroy()`(해제, 한 번)로 나누고, 멱등성은 `if (p->state != POOL_RUNNING) { unlock; return; }` 로 확보한다.

---

## 7. 직접 확인하기

```sh
make sol N=06
```

```
drain : submitted=5000 executed=5000 counter=5000 discarded=0
stop  : submitted=2000 executed=1947 discarded=53
06 PASS
```

첫 줄이 drain의 정의 그 자체다. 5000개 제출, 5000개 실행, 폐기 0. `counter` 가 `executed` 와 일치한다는
건 큐 통계와 실제 실행이 어긋나지 않았다는 뜻이다. 둘째 줄은 즉시 종료다. `1947 + 53 == 2000` —
**실행됐거나 버려졌거나 둘 중 하나이고 사라진 건 없다.** 합이 안 맞으면 `assert(p2.executed + p2.discarded == submitted)` 가 터진다.

`make tsan N=06` 은 깨끗해야 한다. 공유 상태가 전부 `p->m` 안에서만 접근되기 때문이다. 경고가 나온다면
카운터를 락 밖에서 읽었거나 `p->state` 를 락 없이 검사한 것이다. `main` 이 shutdown 반환 후 `p.executed` 를 읽는 건 안전하다 — **join의 happens-before가 락을 대신한다.**

---

## 8. 면접에서 말하기

- 워커 풀 자체는 bounded queue에 스레드 N개를 붙인 것이고, 어려운 건 종료 경로다.
- 종료 모드를 drain과 immediate로 나눈다. 정상 재부팅은 남은 작업을 처리하고, 워치독은 진행 중인 것만 끝낸다.
- 종료 신호는 반드시 broadcast다. signal이면 잠든 워커 중 하나만 깨어나고 나머지는 join에서 멈춘다.
- 일감 함수는 락을 놓고 호출한다. 안 그러면 병렬성이 사라지고, 일감이 다시 submit하면 데드락이다.
- 종료 후 제출은 false로 거부하고 rejected를, 버려진 작업은 discarded를 센다. blocking 작업은 종료를 지연시키므로 작업 쪽에 취소 신호를 전달한다.

- The pool is a bounded queue plus N worker threads; the interesting part is shutdown, not dispatch.
- I support two modes: drain, which finishes queued work for a clean reboot, and immediate, which finishes only in-flight tasks.
- Shutdown must broadcast, not signal — a signal wakes one worker and the rest stay blocked, so join hangs forever.
- Tasks run with the mutex released. Holding it would serialize the workers and would deadlock if a task submitted more work.
- Submits after shutdown are rejected and counted, discarded tasks are counted too, and a long blocking task delays shutdown unless I pass a cancellation signal into the task.

---

## 9. 요약 & 체크리스트

워커 풀은 세 상태(`RUNNING`/`DRAIN`/`STOP`)와 두 조건 변수(`work`/`slot`)로 굴러간다. 워커는 "일감이
없고 running이면" 자고, 깨어나서 "비었거나 STOP이면" 종료한다. 종료는 락 안에서 상태 변경 → 양쪽
broadcast → 전부 join → free 순서이며 이 순서가 곧 안전성이다. 일감은 락 밖에서 실행하고, 거부·폐기는 카운터로 남겨 `executed + discarded == submitted` 를 검증한다.

- [ ] 세 가지 상태와 각 상태에서 워커가 하는 일, 조건 변수를 두 개 쓰는 이유를 설명할 수 있다
- [ ] 종료에서 `signal` 대신 `broadcast` 를 쓰는 이유를 설명할 수 있다
- [ ] 상태 변경 → broadcast → join → free 순서를 외우고 각 단계의 이유를 안다
- [ ] 일감을 락 밖에서 실행해야 하는 이유를 세 가지 댈 수 있다
- [ ] shutdown을 두 번 불러도 안전하게 만들려면 무엇을 바꿔야 하는지 말할 수 있다
- [ ] blocking 작업이 종료를 지연시킬 때의 해법을 설명할 수 있다
