# 09. 데드락 — 락 순서와 try-lock 백오프

> **이 노트를 다 읽으면**:
> - 데드락의 네 가지 필요조건을 대고, 실무에서 어느 조건을 깨는지 말할 수 있다
> - 전역 락 순서(lock ordering)를 id/주소로 강제하는 코드를 쓸 수 있고, 순서를 못 정할 때 trylock + backoff로 대체할 수 있다
> - 우선순위 역전과 priority inheritance mutex를 설명하고, 락을 아예 없애는 actor 설계를 대안으로 제시할 수 있다
>
> **선행**: `00_start_here` 의 §3 임계구역과 뮤텍스
>
> **연습**: `make run N=09` (내 구현) · `make sol N=09` (모범답안) · `make tsan N=09`

---

## 0. 한 문장으로

자원 두 개를 **동시에** 잠가야 하는 연산에서, 스레드마다 잠그는 순서가 다르면 서로의 락을 기다리며 영원히 멈춘다 — 이걸 구조적으로 못 일어나게 만드는 문제다.

Verkada 게이트웨이에서는 두 PoE 포트 사이의 전력 예산을 옮기거나, 두 WAN 인터페이스의 통계를 한꺼번에 갱신할 때 그대로 나온다.

## 1. 왜 필요한가 — 비유로 시작

좁은 복도에서 두 사람이 마주쳤다. 둘 다 "상대가 비켜야 지나간다"고 생각하고 서 있다. 아무도 안 죽었고 CPU도 안 탄다. 그냥 **영원히 아무 일도 안 일어난다**.

젓가락 비유가 더 정확하다. 식탁에 젓가락이 두 짝 있고, 먹으려면 두 짝을 다 들어야 한다. A는 왼쪽부터, B는 오른쪽부터 든다. 각자 한 짝씩 들고 상대 손에 있는 나머지 한 짝을 기다린다.

```
  T1: lock(p1) ─── ok ───┐
                          └── lock(p2) ... 기다리는 중
  T2: lock(p2) ─── ok ───┐
                          └── lock(p1) ... 기다리는 중

  p1 은 T1 이 쥐고 있고, T1 은 p2 를 기다린다.
  p2 는 T2 가 쥐고 있고, T2 는 p1 을 기다린다.
  → 원(circular wait)이 닫혔다. 아무도 못 나간다.
```

베어메탈에서도 본 적 있다. ISR이 스핀락을 잡으려는데 그 락을 메인 루프가 쥔 채 인터럽트 비활성 구간에 들어가 있으면 시스템이 그대로 굳는다. 원인은 똑같다.

## 2. 알아야 할 개념

### 데드락의 네 가지 조건 (Coffman conditions)

**네 개가 전부 동시에 성립해야** 데드락이 생긴다. 하나만 깨면 안 생긴다.

| 조건 | 뜻 | 깰 수 있나 |
|---|---|---|
| mutual exclusion | 자원을 한 번에 한 명만 쓸 수 있다 | 보통 못 깬다. 그게 락의 목적 |
| hold and wait | 하나 쥔 채로 다음 것을 기다린다 | 깰 수 있다 → trylock, 한 번에 다 잡기 |
| no preemption | 남이 쥔 락을 강제로 뺏을 수 없다 | 깰 수 있다 → trylock 실패 시 스스로 놓기 |
| circular wait | 대기 그래프에 원이 생긴다 | **가장 깨기 쉽다 → 전역 락 순서** |

실무의 답은 거의 항상 **circular wait을 깨는 것**이다. 자원마다 전역 순위를 매기고, 언제나 낮은 쪽부터 잠근다. 그러면 원이 물리적으로 만들어질 수 없다. 코드 리뷰로도 강제된다("여기 순서 거꾸로다" 한 줄로 끝난다).

### livelock (라이브락)

데드락은 아무도 안 움직이는 것, livelock은 **모두 열심히 움직이는데 아무도 진전이 없는 것**이다. 복도에서 마주친 두 사람이 동시에 같은 쪽으로 비키기를 반복하는 상황. trylock + 재시도 방식이 잘못 설계되면 정확히 이게 된다.

### priority inversion (우선순위 역전)

낮은 우선순위 태스크가 락을 쥔 채 선점되면, 높은 우선순위 태스크가 그 락을 기다리며 막힌다. 중간 우선순위 태스크가 CPU를 계속 쓰면 낮은 쪽이 실행을 못 받아 락을 영영 못 놓는다. 결과적으로 **중간 우선순위가 최고 우선순위를 막는다**.

### priority inheritance (우선순위 상속)

락을 쥔 태스크의 우선순위를 **기다리는 쪽 중 가장 높은 값으로 일시적으로 올려주는** 것. 락을 놓으면 원래대로 돌아온다. 이러면 중간 우선순위가 끼어들지 못한다.

## 3. 문제 읽기

| 요구 | 설계에 주는 제약 |
|---|---|
| 자원 두 개를 동시에 잠금 | 두 락을 다 쥔 상태가 반드시 존재한다 |
| 호출 방향이 양쪽 다 있음 (`a→b`, `b→a`) | 인자 순서대로 잠그면 무조건 데드락 |
| 총합 보존 (`p1+p2 == 60000`) | 검사와 갱신이 같은 임계구역 안이어야 한다 |
| 순서를 못 정하는 경우도 있음 | trylock 경로가 따로 필요 |

세 번째 줄이 중요하다. "예산이 충분한가"를 락 밖에서 보고 락 안에서 빼면, 보는 사이에 남이 빼가서 음수가 될 수 있다. TOCTOU다.

## 4. 단계별로 만들기

### 4-1. 자원에 전역 순위를 부여한다

```c
typedef struct {
    int             id;
    long            budget_mw;
    pthread_mutex_t m;
} Port;
```

`id` 가 전역 순위다. 순위로 쓸 게 없으면 **주소**(`(uintptr_t)p`)를 써도 된다. 주소는 프로세스 안에서 유일하고 전순서(total order)이므로 순위로 충분하다. 힙에서 만들어 순서가 매번 달라져도 상관없다 — 한 번의 실행 안에서 **모든 스레드가 같은 기준**을 쓰기만 하면 된다.

### 4-2. 항상 낮은 쪽부터 잠근다

목표: 호출 방향과 무관하게 잠그는 순서를 하나로 고정한다.

```c
bool transfer_ordered(Port *a, Port *b, long mw)
{
    if (a == b) return false;
    /* 전역 순서: id가 작은 쪽을 먼저 잠근다. 호출 방향과 무관하게 항상 동일 */
    Port *first  = (a->id < b->id) ? a : b;
    Port *second = (a->id < b->id) ? b : a;

    pthread_mutex_lock(&first->m);
    pthread_mutex_lock(&second->m);
```

여기서 봐야 할 것 세 가지.

- `if (a == b) return false;` — 같은 포트를 두 번 잠그면 non-recursive mutex에서는 **자기 자신과 데드락**한다. 인자가 같을 수 있는 API라면 이 한 줄이 필수다.
- `first` / `second` 는 **잠그는 순서**만 정한다. 누가 주고 누가 받는지는 여전히 `a`, `b` 다.
- `transfer_ordered(&p1, &p2, ...)` 와 `transfer_ordered(&p2, &p1, ...)` 이 **똑같이 p1→p2 순으로 잠근다**. 원이 닫힐 방법이 없다.

### 4-3. 검사와 갱신을 락 안에서

```c
    bool ok = false;
    if (a->budget_mw >= mw) {                 /* 잠근 뒤에 조건 검사 */
        a->budget_mw -= mw;
        b->budget_mw += mw;
        ok = true;
    }
    pthread_mutex_unlock(&second->m);         /* 해제 순서는 역순이 관례 */
    pthread_mutex_unlock(&first->m);
    return ok;
}
```

조건 검사가 락 **안**에 있다. 밖에 있으면 검사와 차감 사이에 다른 스레드가 빼가서 예산이 음수가 된다.

해제는 `second` → `first` 역순이다. 해제 순서는 사실 정확성에 영향이 없다(어느 쪽을 먼저 놔도 데드락은 안 생긴다). 그래도 역순으로 쓰는 게 관례다. 스택처럼 읽혀서 리뷰할 때 짝이 눈에 보이기 때문이다.

### 4-4. 순서를 못 정할 때 — trylock + backoff

목표: 락 순서를 강제할 수 없는 상황(콜백, 외부 라이브러리, 락이 남의 모듈 안에 숨어 있음)에서도 데드락을 피한다.

```c
bool transfer_trylock(Port *a, Port *b, long mw)
{
    if (a == b) return false;
    for (;;) {
        pthread_mutex_lock(&a->m);
        if (pthread_mutex_trylock(&b->m) == 0) break;   /* 둘 다 확보 */

        pthread_mutex_unlock(&a->m);          /* ★ 반드시 첫 락도 놓는다 */
        atomic_fetch_add(&g_retries, 1ul);
        sched_yield();                        /* 실전: 랜덤 지수 백오프 */
    }
```

`pthread_mutex_trylock` 은 락이 비어 있으면 잡고 0을 반환하고, 이미 누가 쥐고 있으면 **기다리지 않고** 즉시 `EBUSY` 를 반환한다. 이게 네 조건 중 **no preemption** 을 깨는 도구다. 뺏지는 못해도, 못 잡으면 포기할 수는 있다.

**`pthread_mutex_unlock(&a->m)` 이 왜 절대 빠지면 안 되는가.** 이 줄이 hold-and-wait을 깬다. `b` 를 못 잡았는데 `a` 를 계속 쥐고 재시도하면, 반대 방향 스레드는 `a` 를 영원히 못 잡는다. 결국 둘 다 첫 락을 쥔 채 서로의 두 번째 락을 기다린다 — trylock을 썼는데도 데드락이 그대로 돌아온다. **첫 락을 놓는 것이 이 패턴의 전부다.**

**백오프에 왜 랜덤이 필요한가.** 두 스레드가 정확히 같은 리듬으로 "잡고 → 실패 → 놓고 → 즉시 재시도"를 반복하면, 매번 같은 지점에서 또 충돌한다. 아무도 안 막혔는데 아무도 진전이 없다 = livelock이다. 재시도 간격에 랜덤을 섞으면 위상이 어긋나서 한쪽이 먼저 통과한다. 이더넷 CSMA/CD의 truncated binary exponential backoff와 정확히 같은 논리고, 이유도 같다.

여기서는 연습용이라 `sched_yield()` 로 CPU를 한 번 양보하는 것으로 끝냈다. 실전이면 재시도 횟수에 따라 대기를 배로 늘리면서 거기에 랜덤 지터를 더하고, 상한 재시도 횟수를 넘으면 실패를 **값으로 반환**해서 상위가 정하게 한다.

### 4-5. 임계구역 본문은 똑같다

```c
    bool ok = false;
    if (a->budget_mw >= mw) {
        a->budget_mw -= mw;
        b->budget_mw += mw;
        ok = true;
    }
    pthread_mutex_unlock(&b->m);
    pthread_mutex_unlock(&a->m);
    return ok;
}
```

두 락을 다 쥔 뒤의 코드는 두 방식이 완전히 같다. **차이는 오직 두 락을 어떻게 확보하느냐**뿐이다. 이걸 말로 정리할 수 있으면 이 문제는 끝난 것이다.

## 5. 전체 흐름 따라가기

순서 고정 방식. 데드락이 생길 자리가 없다.

```
 T1: transfer_ordered(p1, p2)      T2: transfer_ordered(p2, p1)
 ─────────────────────────────────────────────────────────────
 first=p1 second=p2                first=p1 second=p2   ← 같다!
 lock(p1)  ok
                                   lock(p1)  ... 대기
 lock(p2)  ok
 예산 이동
 unlock(p2)
 unlock(p1)
                                             ok (깨어남)
                                   lock(p2)  ok
                                   예산 이동
```

trylock 방식. 원이 생기려는 순간을 스스로 풀고 나온다.

```
 T1: transfer_trylock(p1, p2)      T2: transfer_trylock(p2, p1)
 ─────────────────────────────────────────────────────────────
 lock(p1)  ok
                                   lock(p2)  ok
 trylock(p2) -> EBUSY
                                   trylock(p1) -> EBUSY
 unlock(p1)  ★
                                   unlock(p2)  ★
 yield + 재시도                     yield + 재시도
   ... 랜덤 백오프가 있으면 여기서 위상이 갈려 한쪽이 먼저 성공
   ... 없으면 같은 충돌을 반복할 수 있다 (livelock)
```

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 인자 순서대로 잠금 | `pthread_join` 에서 영원히 멈춤, CPU 0% | 반대 방향 호출과 원이 닫힘 | id/주소로 전역 순서 고정 |
| trylock 실패 후 첫 락을 안 놓음 | 그대로 데드락 | hold-and-wait이 안 깨짐 | 실패 즉시 `unlock(&a->m)` |
| 백오프에 랜덤 없음 | CPU 100%인데 진행 없음, retries 폭증 | 두 스레드가 같은 위상으로 충돌 반복 | 랜덤 지수 백오프 |
| `a == b` 검사 누락 | 같은 포트 두 번 호출 시 즉시 멈춤 | non-recursive mutex 자기 데드락 | 진입부에서 거르기 |
| 조건 검사를 락 밖에서 | 예산이 음수, 총합 불일치 | TOCTOU | 검사+갱신을 같은 임계구역에 |
| 락 쥔 채 콜백/malloc 호출 | 가끔 멈춤, 재현 어려움 | 남의 코드가 다른 락을 잡을 수 있다 | 락 구간에서는 남의 코드를 부르지 않는다 |
| 락 계층 문서 없음 | 새 코드가 들어올 때마다 재발 | 순서가 사람 머릿속에만 있음 | 헤더에 락 순위표를 주석으로 박아둔다 |

## 7. 직접 확인하기

```sh
make sol N=09
```

```
  ordered   total=60000 (expect 60000) p1=30000 p2=30000 retries=0
  trylock   total=60000 (expect 60000) p1=50000 p2=10000 retries=4
09 PASS
```

두 가지를 본다.

- `total=60000` — 총 전력 예산이 보존됐다. 두 방식 다 정확하다.
- `retries` — `ordered` 는 0이다. 재시도라는 개념 자체가 없다. `trylock` 은 실행마다 다르고, ThreadSanitizer를 켜면 스레드가 느려져 충돌이 잦아지므로 수천 회까지 올라간다. 이게 **순서 고정이 기본이어야 하는 이유**다. trylock은 낭비가 있고, 그 낭비량을 예측할 수 없다.

`p1=50000 p2=10000` 처럼 한쪽으로 쏠리는 건 정상이다. 두 방향 스레드가 같은 횟수씩 돌지 않고 스케줄러가 붙여주는 대로 진행하기 때문이다. 불변식은 **총합**이지 개별 값이 아니다.

데드락을 직접 보고 싶으면 `transfer_ordered` 의 `first`/`second` 대신 `a`/`b` 를 그대로 잠그도록 고쳐서 돌려보면 된다. 프로그램이 출력 없이 멈추고 CPU 사용률이 0%가 된다. **CPU 0%로 멈춤 = 데드락, CPU 100%로 멈춤 = livelock 또는 무한루프** — 이 구분은 현장에서 그대로 쓰는 진단법이다. 붙은 프로세스는 `lldb -p <pid>` 로 붙어 `thread backtrace all` 을 찍으면 어느 스레드가 어느 `pthread_mutex_lock` 에서 멈췄는지 바로 보인다.

## 8. 면접에서 말하기

- 데드락은 네 조건이 동시에 성립할 때만 생긴다. mutual exclusion, hold and wait, no preemption, circular wait. 실무에서는 circular wait을 깬다 — 자원마다 전역 순위를 정하고 항상 낮은 쪽부터 잠근다.
- 순위는 id나 주소면 된다. 중요한 건 값이 무엇이냐가 아니라 프로세스 안의 모든 코드가 같은 기준을 쓰느냐다. 그래서 락 계층은 헤더 주석에 표로 남긴다.
- 순서를 강제할 수 없을 때 — 콜백이나 외부 API 뒤에 락이 숨어 있을 때 — 만 trylock + backoff를 쓴다. 이때 두 번째 락에 실패하면 **첫 번째 락도 반드시 놓아야** hold-and-wait이 깨진다. 안 놓으면 trylock을 쓰고도 데드락이 난다.
- 백오프에는 랜덤을 넣는다. 안 넣으면 두 스레드가 같은 위상으로 계속 충돌해서 livelock이 된다. 이더넷 백오프와 같은 이유다.
- RTOS에서는 데드락보다 우선순위 역전이 더 흔한 사고다. 낮은 우선순위 태스크가 락을 쥔 채 선점되고, 중간 우선순위가 CPU를 잡으면 최고 우선순위가 무한정 막힌다. Mars Pathfinder가 화성에서 반복 리셋된 게 정확히 이 버그였고, 픽스는 해당 mutex의 priority inheritance를 켜는 것이었다.
- FreeRTOS에서는 `xSemaphoreCreateMutex()` 로 만든 mutex만 priority inheritance를 지원한다. `xSemaphoreCreateBinary()` 로 만든 binary semaphore는 지원하지 않는다. 상호배제에는 mutex, ISR→태스크 시그널링에는 binary semaphore로 쓰임을 나눠야 한다.
- 가장 확실한 해법은 락을 없애는 것이다. 공유 자원마다 소유 태스크를 하나 정하고 나머지는 메시지 큐로만 요청하게 하면, 락이 없으니 데드락도 우선순위 역전도 구조적으로 사라진다. 대신 지연이 큐 깊이에 묶이고 요청/응답 짝을 관리해야 한다.

English:

- Deadlock needs all four Coffman conditions. In practice I break circular wait: give every lockable resource a global rank — an id or just its address — and always take them in ascending order.
- The rank only has to be consistent process-wide, and I document the lock hierarchy in the header so reviewers can enforce it.
- When I can't impose an order, for example when a lock sits behind a third-party callback, I use trylock with backoff. If the second trylock fails I must release the first lock too, otherwise hold-and-wait survives and I still deadlock.
- The backoff needs randomized jitter, otherwise both threads retry in lockstep and you get livelock instead of deadlock — same reason Ethernet uses randomized exponential backoff.
- On an RTOS the more common failure is priority inversion: a low-priority task holding the lock gets preempted and a medium-priority task starves it, blocking the high-priority task indefinitely. That's the Mars Pathfinder bug, and the fix was enabling priority inheritance on that mutex.
- In FreeRTOS only `xSemaphoreCreateMutex` gives you priority inheritance; a binary semaphore does not, so I use mutexes for mutual exclusion and binary semaphores only for ISR-to-task signaling.
- The strongest fix is to remove the lock: give one task sole ownership of the resource and let everyone else send it messages. No lock, no deadlock, no inversion — at the cost of queueing latency.

## 9. 요약 & 체크리스트

데드락은 네 조건이 전부 맞아야 생기고, 실무의 답은 언제나 circular wait을 깨는 것이다. 자원에 전역 순위를 매기고(id 또는 주소) 항상 낮은 쪽부터 잠근다. 순서를 못 정하는 예외 상황에서만 trylock + 랜덤 백오프를 쓰고, 이때 첫 락을 놓는 것을 절대 빼먹지 않는다. RTOS로 가면 데드락보다 우선순위 역전이 더 자주 사고를 내므로 priority inheritance mutex를 쓰거나, 아예 자원을 한 태스크가 소유하고 메시지로만 접근하게 설계한다.

- [ ] Coffman 네 조건을 이름까지 댈 수 있다
- [ ] 왜 circular wait이 깨기 가장 쉬운 조건인지 설명할 수 있다
- [ ] `first`/`second` 계산이 호출 방향과 무관하게 같은 순서를 만드는 걸 보일 수 있다
- [ ] trylock 실패 시 첫 락을 놓아야 하는 이유를 말할 수 있다
- [ ] 랜덤 백오프가 없을 때 생기는 livelock을 설명할 수 있다
- [ ] CPU 0%로 멈춤과 100%로 멈춤을 구분해 진단할 수 있다
- [ ] 우선순위 역전과 priority inheritance, Mars Pathfinder를 한 묶음으로 말할 수 있다
- [ ] FreeRTOS mutex와 binary semaphore의 차이를 inheritance 관점에서 설명할 수 있다
- [ ] 락을 없애는 actor/메시지 큐 설계를 대안으로 제시하고 트레이드오프를 말할 수 있다
