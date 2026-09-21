# 05. Backpressure & drop policy — 링크가 끊겨도 죽지 않는 텔레메트리 큐

> **이 노트를 다 읽으면**
> - BLOCK / DROP_NEWEST / DROP_OLDEST 중 무엇을 고를지 **데이터의 의미**로 판단할 수 있다
> - 큐가 가득 찼을 때 세 정책이 각각 어떤 코드로 구현되는지 쓸 수 있다
> - 왜 버린 개수를 반드시 세서 밖으로 내보내야 하는지 설명할 수 있다
>
> **선행**: `00_start_here` 의 §3 뮤텍스, §4 조건 변수. `01_bounded_queue` 의 BLOCK 버전을 먼저 읽는다.
>
> **연습**: `make run N=05` (내 구현) · `make sol N=05` (모범답안) · `make tsan N=05`

---

## 0. 한 문장으로

**큐가 가득 찼을 때 무엇을 포기할지 미리 정해두는 것이 backpressure 정책이다.**
Verkada 게이트웨이가 LTE로 텔레메트리를 올리다가 링크가 느려지면 업로더가 막히는데,
그 사이에도 도어 센서·모뎀 스레드는 샘플을 계속 만든다. 이때 무엇을 버릴지가 곧 제품의 성격이다.

---

## 1. 왜 필요한가 — 비유로 시작

우체통이 있다. 집배원이 오늘 눈 때문에 못 왔는데 우편물은 계속 온다. 꽉 차면 셋 중 하나다.

```
 [BLOCK]        우편물 들고 온 사람을 문 앞에 세워둔다
                → 하나도 안 잃는다. 대신 그 사람이 다른 일을 못 한다

 [DROP_NEWEST]  새로 온 편지를 문 앞에서 돌려보낸다
                → 먼저 온 것들이 살아남는다 (이력·로그)

 [DROP_OLDEST]  맨 아래 오래된 편지를 빼서 버리고 새 걸 넣는다
                → 최신 것이 살아남는다 (현재 상태·알람)
```

**정답은 없다. 편지의 종류가 정답을 정한다.** 감사 로그는 오래된 걸 버리면 안 되고, 배터리 잔량은 최신만 남기면 된다.

베어메탈 대응: UART RX 링버퍼가 꽉 찼을 때 overrun 플래그를 세우고 새 바이트를 버리는 것이
DROP_NEWEST, 오디오 DMA에서 늦은 프레임을 건너뛰는 것이 DROP_OLDEST다.

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 | Don이 아는 것과의 대응 |
| --- | --- | --- | --- |
| backpressure | 소비자가 느릴 때 생산자 쪽으로 전달되는 "천천히" 신호 | 메모리는 유한하다 | flow control, XON/XOFF |
| bounded queue | 용량이 정해진 큐 | 무제한 큐는 OOM으로 끝난다 | 고정 크기 링버퍼 |
| drop policy | 가득 찼을 때의 행동 규칙 | 선택을 코드가 아니라 설계가 하도록 | overrun 처리 정책 |
| 조용한 손실 | 버렸는데 아무도 모르는 상태 | 디버깅이 불가능해진다 | 카운터 없는 overrun 플래그 |
| batch pop | 한 번 락 잡고 여러 개 꺼내기 | 락 횟수와 업로드 오버헤드를 줄인다 | DMA 한 번에 N 바이트 전송 |
| `pthread_cond_broadcast` | 대기 중인 **모든** 스레드를 깨움 | 여러 생산자가 자리를 기다릴 수 있다 | 여러 태스크에 이벤트 플래그 세팅 |

---

## 3. 문제 읽기

요구사항을 쪼개면 설계 제약이 바로 나온다.

1. **정책 세 가지를 하나의 자료구조로** → 정책은 런타임 필드(`DropPolicy policy`)로 들고 다닌다.
2. **BLOCK은 손실 없음** → 생산자가 `not_full` 조건 변수에서 기다려야 한다.
3. **DROP_* 은 생산자를 막지 않음** → 락 잡고, 카운터 올리고, 즉시 반환한다.
4. **버린 개수를 보고** → `dropped` 카운터가 자료구조의 1급 멤버다.
5. **종료가 있어야 함** → `closed` 플래그 + broadcast로 대기 중인 모두를 깨운다.
6. **불변식 검증 가능** → `produced == consumed + dropped + queued` 가 항상 성립해야 한다.

6번이 중요하다. 이 등식이 깨지면 샘플이 **소리 없이 사라진** 것이고, 정책 버그가 아니라 자료구조 버그다.

```c
typedef struct {
    Sample         *buf;
    size_t          cap, head, count;
    DropPolicy      policy;
    bool            closed;
    unsigned long   pushed, dropped, popped;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} TeleQueue;
```

`pushed`, `dropped`, `popped` 세 카운터가 구조체 안에 같이 있는 것이 설계 의도다.
**통계는 나중에 붙이는 게 아니라 처음부터 자료구조의 일부다.**

---

## 4. 단계별로 만들기

### 4-1. 닫힌 큐부터 걸러낸다

```c
bool tq_push(TeleQueue *q, Sample s)
{
    pthread_mutex_lock(&q->m);
    if (q->closed) { pthread_mutex_unlock(&q->m); return false; }
```

`closed` 검사를 맨 앞에 두는 이유는 종료 중인 큐에 넣어봐야 아무도 안 꺼내가기 때문이다.
`false` 를 돌려주면 생산자가 루프를 벗어난다. 이 반환값이 없으면 생산자는 종료를 알 방법이 없다.
락을 잡은 뒤에 검사하는 것도 중요하다 — 락 밖에서 `q->closed` 를 읽으면 그 자체가 data race다.

### 4-2. 가득 찼을 때 — 정책 분기

```c
    if (q->count == q->cap) {
        switch (q->policy) {
        case POLICY_BLOCK:
            while (q->count == q->cap && !q->closed)
                pthread_cond_wait(&q->not_full, &q->m);
            if (q->closed) { pthread_mutex_unlock(&q->m); return false; }
            break;
```

BLOCK은 `01_bounded_queue` 와 같다. `while` 로 감싸는 이유는 두 가지 — spurious wakeup이
있을 수 있고, 깨어났을 때 다른 생산자가 먼저 자리를 채웠을 수 있다. `if` 로 쓰면 자리가 없는데
넣어서 데이터를 덮어쓴다. 조건에 `&& !q->closed` 가 붙은 것도 놓치면 안 된다. 이게 없으면
소비자가 죽고 큐가 닫힌 뒤에도 생산자가 `not_full` 에서 영원히 잠든다. 깨어난 뒤 `q->closed` 를
다시 검사해 `false` 를 돌려주는 것까지가 한 세트다.

```c
        case POLICY_DROP_NEWEST:
            q->dropped++;
            pthread_mutex_unlock(&q->m);
            return true;                       /* 성공으로 취급하되 카운트 */
```

DROP_NEWEST는 세 줄이다. 방금 받은 샘플을 버리고, 센 다음, **`true` 를 반환한다.**
`false` 는 "큐가 닫혔다"는 뜻이라 생산자가 그걸 보고 루프를 끝내기 때문이다. 링크가 잠깐
막혔다고 센서 스레드를 죽일 수는 없다. 반환값 의미를 구분하지 않으면 "LTE가 느려지면 센서
수집이 멈춘다"는 버그가 나온다.

```c
        case POLICY_DROP_OLDEST:
            q->head = (q->head + 1) % q->cap;  /* 가장 오래된 것 폐기 */
            q->count--;
            q->dropped++;
            break;
        }
    }
```

DROP_OLDEST는 `head` 를 한 칸 밀어 맨 앞 샘플을 논리적으로 버리고, `count--` 로 자리를 만든 뒤
`break` 로 공통 삽입 경로로 내려간다. 링버퍼에서 "버린다"는 건 인덱스를 옮기는 것뿐이다.

### 4-3. 공통 삽입과 신호

```c
    q->buf[(q->head + q->count) % q->cap] = s;
    q->count++;
    q->pushed++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_empty);
    return true;
}
```

꼬리 위치는 `head + count` 를 `cap` 으로 나눈 나머지다. tail을 따로 들고 다니지 않아
"가득 참"과 "빔"이 헷갈릴 일이 없다 — `count` 하나가 진실의 원천이다. `pthread_cond_signal` 을
**unlock 뒤에** 부르는 것은 최적화다(락을 쥔 채 signal하면 깨어난 소비자가 락을 못 잡고 다시 잠든다).

### 4-4. 배치로 꺼내기

```c
    size_t n = q->count < max ? q->count : max;   /* 배치로 꺼내 업로드 효율↑ */
    for (size_t i = 0; i < n; i++) {
        out[i] = q->buf[q->head];
        q->head = (q->head + 1) % q->cap;
    }
    q->count -= n;
    q->popped += n;
    pthread_mutex_unlock(&q->m);
    if (n) pthread_cond_broadcast(&q->not_full);
    return n;                                     /* 0 = closed && empty */
```

한 번 락을 잡고 최대 `max` 개를 가져온다. LTE 업로드는 요청당 오버헤드가 크므로 16개씩 묶어
보내는 게 훨씬 싸고, 락 횟수도 1/16로 준다.

`signal` 이 아니라 `broadcast` 인 이유는 한 번에 `n` 개의 자리가 났고 생산자가 여럿일 수
있어서다. `signal` 이면 자리는 16개 났는데 한 명만 깨어나 나머지 15개가 놀게 된다. `if (n)` 은
헛된 broadcast를 막는다. 반환값 `0` 은 "닫혔고 비었다"는 뜻이라, 소비자는
`while ((n = tq_pop_batch(...)) > 0)` 한 줄로 종료 조건을 처리한다.

### 4-5. 닫기 — 양쪽 다 깨운다

```c
void tq_close(TeleQueue *q)
{
    pthread_mutex_lock(&q->m);
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}
```

두 조건 변수를 **모두** broadcast해야 한다. `not_empty` 만 깨우면 BLOCK 정책에서 자리를
기다리던 생산자가 영원히 잠들고, join하는 쪽이 같이 멈춘다.

---

## 5. 전체 흐름 따라가기

DROP_OLDEST, `cap=4` 인 큐에서 업로더가 잠시 멈춘 상황이다.

```
 생산자 P                  큐 상태 (head→)         업로더 U        dropped
 ------------------------------------------------------------------------
 push(s1)                 [s1 . . .]              (느린 링크)        0
 push(s2)                 [s1 s2 . .]                                0
 push(s3)                 [s1 s2 s3 .]                               0
 push(s4)                 [s1 s2 s3 s4]  가득                        0
 push(s5) → oldest 폐기   [s5 s2 s3 s4]  head=1                      1
 push(s6) → oldest 폐기   [s5 s6 s3 s4]  head=2                      2
                                         U 깨어남 pop_batch(16)
                                         → s3 s4 s5 s6 (4개)
                          [. . . .]      count=0                     2

 불변식: produced(6) == consumed(4) + dropped(2) + queued(0)  ✓
```

같은 상황에서 BLOCK이면 `push(s5)` 가 그 자리에서 멈춰 U가 꺼내갈 때까지 생산자가 아무 일도
못 한다. 센서 스레드라면 **샘플링 주기가 통째로 밀린다** — 실시간 경로에서 BLOCK을 금지하는 이유다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| DROP_NEWEST에서 `false` 반환 | 링크 혼잡 시 생산자가 종료해버림 | `false` 를 "닫힘"으로 쓰기로 했는데 의미 충돌 | 버려도 `true`, 손실은 `dropped` 로만 보고 |
| `dropped` 카운터 없음 | 데이터가 비는데 원인 추적 불가 | 조용한 손실이 제일 나쁘다 | 카운터를 구조체 멤버로, 텔레메트리에 같이 실어 보냄 |
| BLOCK 대기 조건에 `!closed` 누락 | 종료 시 생산자가 영영 안 깨어남 | 닫힌 뒤엔 자리가 절대 안 남 | `while (full && !closed)` + 깨어나 재검사 |
| `tq_close` 에서 한쪽만 broadcast | join에서 hang | 반대쪽 대기자가 남음 | `not_empty`, `not_full` 둘 다 broadcast |
| pop에서 `signal` 사용 | 자리는 여러 개 났는데 한 명만 깨어남 | 배치로 비웠기 때문 | `broadcast` |
| 실시간 경로에 BLOCK 적용 | 샘플링 주기가 밀리고 워치독 발동 | 생산자가 소비자 속도에 묶임 | DROP_OLDEST + 손실 카운터 |
| 무제한 큐로 회피 | 링크 장애 몇 시간 뒤 OOM kill | 메모리는 유한하다 | 용량을 정하고 정책을 고른다 |

---

## 7. 직접 확인하기

```sh
make sol N=05
```

```
  BLOCK        produced=15000 consumed=15000 dropped=0 left=0
  DROP_NEWEST  produced=15000 consumed=1616 dropped=13384 left=0
  DROP_OLDEST  produced=15000 consumed=4592 dropped=10408 left=0
05 PASS
```

세 줄이 전부 정책의 성격을 그대로 보여준다.

- BLOCK은 `dropped=0` — 하나도 안 잃었다. 대신 생산자 3개가 업로더를 기다리느라 계속 멈췄다.
- DROP_NEWEST와 DROP_OLDEST는 1만 개 넘게 버렸다. 느린 링크를 흉내낸 spin이 있으니 당연하다.
- 어느 줄이든 `consumed + dropped + left == produced` 가 맞는다. 이게 `assert` 로 걸려 있다.

DROP_NEWEST가 DROP_OLDEST보다 덜 소비한 것은 스케줄링 결과지 정책의 우열이 아니다.
**정책은 처리량이 아니라 "어느 데이터를 살릴 것인가"로 고른다.**

`make tsan N=05` 는 경고 없이 통과해야 한다. 04와 달리 모든 공유 상태가 mutex 안에서만
접근되기 때문이다. 경고가 나온다면 십중팔구 카운터를 락 밖에서 읽었거나 `closed` 를 락 없이
검사한 것이다(테스트의 `g_consumed` 는 업로더 스레드 하나만 만지므로 안전하다).

---

## 8. 면접에서 말하기

- 큐 용량이 유한하니 가득 찼을 때 행동을 정책으로 명시한다. BLOCK, 새 것 버리기, 오래된 것 버리기 세 가지다.
- 정책은 데이터의 의미가 정한다. 감사 로그는 오래된 걸 못 버리니 DROP_NEWEST, 현재 상태·알람은 최신이 중요하니 DROP_OLDEST다.
- 실시간 샘플링 경로에 BLOCK을 쓰면 소비자 지연이 생산자 주기를 밀어버리므로 쓰지 않는다.
- 버린 개수는 반드시 카운트해서 텔레메트리로 같이 올린다. 조용한 손실은 디버깅이 불가능하다.
- produced == consumed + dropped + queued 를 불변식으로 잡고 테스트에서 검증한다.

- The queue is bounded, so I make the overflow behavior an explicit policy: block, drop the newest, or drop the oldest.
- The data's meaning picks the policy — audit logs keep history so I drop the newest, while status and alarms care about freshness so I drop the oldest.
- I never block on a real-time sampling path, because consumer latency would propagate into the producer's period.
- Whatever gets dropped is counted and exported with the telemetry. Silent loss is the worst failure mode.
- I assert produced equals consumed plus dropped plus queued, so any sample that disappears shows up as a test failure.

---

## 9. 요약 & 체크리스트

bounded queue에 정책 필드 하나를 더하면 backpressure 설계가 된다. BLOCK은 `not_full` 에서
기다리되 `closed` 를 같이 검사하고, DROP_NEWEST는 카운트만 하고 `true` 로 돌아가고,
DROP_OLDEST는 `head` 를 밀어 자리를 만든 뒤 공통 삽입 경로로 내려간다. 꺼낼 때는 배치로 꺼내고
`broadcast` 로 여러 생산자를 깨운다. 종료는 `closed` + 양쪽 broadcast. 그리고 무엇을 하든
`dropped` 를 세서 밖으로 내보낸다 — 조용한 손실이 가장 나쁜 실패 모드다.

- [ ] 세 정책을 데이터 종류를 예로 들며 고를 수 있다
- [ ] DROP_NEWEST가 왜 `true` 를 반환해야 하는지 설명할 수 있다
- [ ] DROP_OLDEST가 `head++` 와 `count--` 만으로 되는 이유를 안다
- [ ] BLOCK 대기 조건에 `!closed` 가 왜 필요한지 말할 수 있다
- [ ] pop에서 `signal` 이 아니라 `broadcast` 인 이유를 설명할 수 있다
- [ ] `produced == consumed + dropped + queued` 불변식을 쓰고 검증할 수 있다
- [ ] 실시간 경로에 BLOCK을 쓰면 안 되는 이유를 한 문장으로 말할 수 있다
- [ ] 배치 pop이 락 횟수와 업로드 오버헤드를 동시에 줄인다는 걸 안다
