# 07. 주기 실행 + 취소 가능한 대기 — drift 없이 100ms마다, 종료는 즉시

> **이 노트를 다 읽으면**
> - `sleep(period)` 루프가 왜 주기를 밀리게 하는지, 절대 deadline이 어떻게 그걸 없애는지 설명할 수 있다
> - `pthread_cond_timedwait` 의 기본 시계가 CLOCK_REALTIME이라는 함정과 Linux/macOS 대처를 말할 수 있다
> - 주기를 놓쳤을 때 따라잡기(catch-up)를 하면 왜 보통 더 나빠지는지 설명할 수 있다
>
> **선행**: `00_start_here` 의 §4 조건 변수, §7 종료와 정리. `06_thread_pool` 의 취소 신호 얘기와 이어진다.
>
> **연습**: `make run N=07` (내 구현) · `make sol N=07` (모범답안) · `make tsan N=07`

---

## 0. 한 문장으로

**"N ms마다 실행하되 종료 신호가 오면 즉시 깨어나라"를 절대 deadline + `cond_timedwait` 으로 푸는 문제다.**
게이트웨이의 센서 샘플링·모뎀 폴링·keepalive처럼 주기적으로 돌면서도 재부팅 요청에 수십 ms 안에 반응해야 하는 스레드가 전부 이 모양이다.

---

## 1. 왜 필요한가 — 비유로 시작

매시 정각에 종을 치는 시계탑을 생각한다. 종 치는 데 3분이 걸린다.

```
 A — "종 치고 나서 60분 세기" (sleep 루프)
   12:00 종 → 12:03 종료 → +60분 → 13:03 → 14:06 → 15:09 ...  매 시간 3분씩 = drift
 B — "다음 정각은 13:00이라고 미리 정해두기" (절대 deadline)
   12:00 종 → 12:03 종료 → 13:00까지 대기 → 13:00 종 → 14:00 ...  누적 오차 없음
```

두 번째 문제는 취소다. `sleep(100ms)` 로 자고 있으면 종료 신호가 와도 최대 100ms를 더 자고 나서야
반응한다. 주기가 5초면 종료에 5초다. `pthread_cond_timedwait` 은 **타임아웃과 신호를 동시에
기다리는** 함수라 이 문제를 한 번에 해결한다. 베어메탈 대응: 하드웨어 타이머를 auto-reload로
걸어두는 것이 B, ISR에서 `TIM->ARR` 를 현재 카운트 기준으로 다시 세팅하는 것이 A다.

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 | Don이 아는 것과의 대응 |
| --- | --- | --- | --- |
| drift | 주기가 조금씩 밀려 누적되는 오차 | 상대 대기는 작업 시간을 포함하지 않는다 | 소프트웨어 타이머 재장전 오차 |
| 절대 deadline | "몇 시에"로 표현한 다음 실행 시각 | 누적 오차를 원천 차단 | 타이머 auto-reload |
| `CLOCK_MONOTONIC` | 부팅 후 단조 증가, 절대 되돌아가지 않음 | 경과 시간 측정용 | 자유 구동 하드웨어 카운터 |
| `CLOCK_REALTIME` | 벽시계 시각. NTP가 앞뒤로 조정 | 사람이 읽는 시각용 | RTC |
| `pthread_cond_timedwait` | 신호 또는 타임아웃 중 먼저 오는 것까지 대기 | 취소 가능한 sleep | 타임아웃 있는 이벤트 대기 |
| spurious wakeup | 아무도 안 깨웠는데 깨어나는 것 | POSIX가 허용한다 | 노이즈로 뜬 인터럽트 |
| catch-up | 놓친 주기를 몰아서 실행 | 보통은 **하지 말아야** 한다 | 밀린 ISR을 몰아 처리 |

---

## 3. 문제 읽기

요구는 세 줄이지만 제약은 더 나온다.

1. **주기가 밀리면 안 된다** → 대기 기준은 "지금 + period"가 아니라 "이전 deadline + period"다.
2. **종료 신호에 즉시 반응** → `sleep` 대신 조건 변수 대기. `stop` 플래그는 mutex로 보호한다.
3. **이식성** → 조건 변수의 기본 시계가 플랫폼마다 다르다. 모르면 NTP 조정 때 타이머가 멈춘다.

```c
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    bool            stop;
    unsigned long   ticks, late_ticks;
    int64_t         period_ns;
} Periodic;
```

`late_ticks` 가 이 설계의 성격을 보여준다. **주기를 놓치는 일은 일어난다고 가정하고 몇 번 놓쳤는지를 센다.**

---

## 4. 단계별로 만들기

### 4-1. 시계를 고른다

```c
static int64_t now_mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}
```

경과 시간을 다룰 때는 무조건 `CLOCK_MONOTONIC` 이다. `CLOCK_REALTIME` 은 NTP나 관리자가 시각을
바꾸면 **뒤로 갈 수 있다.** 게이트웨이는 부팅 직후 시각이 엉뚱했다가 NTP 동기화로 크게 점프하는
일이 흔해서, realtime으로 주기를 재면 타이머가 몇 분씩 멈추거나 폭주한다. 모든 시각을 `int64_t`
나노초로 통일한 것도 `timespec` 산술 실수를 줄이려는 것이다.

### 4-2. 조건 변수의 시계를 바꾼다 — 이식성 함정

```c
    pthread_mutex_init(&p->m, NULL);
#if defined(__linux__)
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);  /* 시각 점프 방어 */
    pthread_cond_init(&p->cv, &attr);
    pthread_condattr_destroy(&attr);
#else
    pthread_cond_init(&p->cv, NULL);                     /* macOS: 아래 relative 사용 */
#endif
```

**`pthread_cond_timedwait` 의 기본 시계는 CLOCK_REALTIME이다.** 아무 설정 없이 쓰면 넘겨준 절대
시각을 벽시계 기준으로 해석한다. NTP가 시각을 10초 뒤로 돌리면 이미 지났어야 할 deadline이 다시
10초 미래가 되어 대기가 늘어지고, 앞으로 점프하면 타임아웃이 즉시 터진다.

Linux에서는 `pthread_condattr_setclock` 으로 그 조건 변수가 쓰는 시계 자체를 MONOTONIC으로 바꾼다.
이후 넘기는 절대 시각은 monotonic 기준으로 해석된다. **macOS에는 `setclock` 이 없다.** 대신
`pthread_cond_timedwait_relative_np` 라는 비표준 확장이 있어서 절대 시각이 아니라 "지금부터 얼마"를
넘긴다. 상대 대기는 벽시계 조정의 영향을 받지 않으므로 목적은 같다(`_np` 는 non-portable).
이 `#if` 하나를 설명할 수 있으면 "POSIX API를 실제로 이식해본 사람"으로 보인다.

### 4-3. 대기 — while + deadline 재계산

```c
bool periodic_wait_until(Periodic *p, int64_t deadline_ns)
{
    bool stopped = false;
    pthread_mutex_lock(&p->m);
    while (!p->stop) {
        int64_t remain = deadline_ns - now_mono_ns();
        if (remain <= 0) break;                 /* 시간 다 됨 → 다음 주기 실행 */
```

루프 조건이 `!p->stop` 이라 종료가 오면 즉시 빠져나온다. 안에서는 매 바퀴 남은 시간을 **다시
계산한다** — spurious wakeup으로 깨어나도 원래 deadline을 그대로 쓰기 위해서다. `remain <= 0`
이면 이미 시각이 지났으니 대기 없이 나간다.

```c
#if defined(__APPLE__)
        struct timespec rel = { .tv_sec  = (time_t)(remain / NS_PER_S),
                                .tv_nsec = (long)(remain % NS_PER_S) };
        int rc = pthread_cond_timedwait_relative_np(&p->cv, &p->m, &rel);
#else
        struct timespec abs = { .tv_sec  = (time_t)(deadline_ns / NS_PER_S),
                                .tv_nsec = (long)(deadline_ns % NS_PER_S) };
        int rc = pthread_cond_timedwait(&p->cv, &p->m, &abs);
#endif
        if (rc == ETIMEDOUT) break;
        /* rc == 0 이어도 spurious wakeup일 수 있다 → while로 다시 검사 */
    }
    stopped = p->stop;
    pthread_mutex_unlock(&p->m);
    return stopped;
}
```

macOS 쪽은 `remain`(상대), Linux 쪽은 `deadline_ns`(절대)를 넘긴다. `rc == ETIMEDOUT` 이면 deadline에 도달한 것이니 `break` 해서 다음 주기를 실행한다. `rc == 0` 이면 누가 깨웠거나 spurious
wakeup이다 — **`rc == 0` 을 곧바로 "종료 신호"로 해석하면 안 된다.** 다시 `while (!p->stop)` 로
돌아가 진짜로 `stop` 이 섰는지 확인한다. `if` 로 바꾸면 spurious wakeup 한 번에 주기가 깨진다.
반환값 `stopped` 는 `true` 면 "종료해라", `false` 면 "이번 주기 실행해라"다.

### 4-4. 취소 — 플래그는 락 안에서, broadcast는 락 밖에서

```c
void periodic_stop(Periodic *p)
{
    pthread_mutex_lock(&p->m);
    p->stop = true;
    pthread_mutex_unlock(&p->m);
    pthread_cond_broadcast(&p->cv);             /* 대기 중인 모든 스레드 즉시 기상 */
}
```

`stop` 을 락 안에서 세우지 않으면 "대기 직전에 플래그를 읽고, 플래그가 서고 broadcast가 나간 뒤에
잠드는" lost wakeup이 가능하고, 그러면 최악의 경우 주기 전체만큼 종료가 늦는다. 락 안에서 세우면
`while (!p->stop)` 검사와 플래그 세팅이 겹칠 수 없다. `broadcast` 인 이유는 같은 `Periodic` 을
여러 스레드가 대기할 수 있어서다.

### 4-5. 호출자 — drift 방지와 catch-up 포기

```c
        next += g_p.period_ns;
        if (next < now_mono_ns()) {                   /* 주기를 놓쳤다 */
            g_p.late_ticks++;
            next = now_mono_ns() + g_p.period_ns;     /* 따라잡기 포기(catch-up 방지) */
        }
```

첫 줄이 drift 방지의 전부다. `next = now + period` 가 아니라 `next += period` — 기준이 이전 deadline이므로 작업이 얼마나 걸렸든 다음 실행 시각은 고정 격자 위에 남는다.

둘째 블록이 더 중요한 판단이다. 작업이 주기보다 오래 걸려 `next` 가 이미 과거가 되면
`next += period` 를 계속 하는 한 deadline이 계속 과거이고, `remain <= 0` 이 연속으로 참이 되어
**대기 없이 몰아서 실행**된다 — catch-up burst다. 센서 샘플링에서 이건 거의 항상 잘못이다.

- 밀린 만큼 몰아 찍은 샘플은 거의 같은 시각의 값이라 정보가 없다.
- CPU가 이미 바쁜 상황에 일감을 몰아넣어 더 밀리게 만든다 — 악순환이다.
- 업로드 큐에 버스트가 들어가 05의 drop을 유발한다.

그래서 따라잡기를 포기하고 `next` 를 현재 기준으로 리셋하되, `late_ticks` 로 **몇 번 놓쳤는지는
반드시 남긴다.** 반대로 과금 미터링·누적 카운터처럼 "총 횟수"가 의미를 갖는 작업은 밀린 만큼
실행하는 게 맞다. **판단 기준은 05와 같다 — 데이터의 의미.**

---

## 5. 전체 흐름 따라가기

주기 10ms, 작업 2ms일 때 정상 동작과 종료를 같이 본다.

```
 시각(ms)  sampler 스레드                        main 스레드
 -----------------------------------------------------------------
   0.0     next=10.0, cond_timedwait(abs=10.0)   nanosleep(200ms)
  10.0     ETIMEDOUT → ticks=1, 작업 2ms
  12.0     next = 10.0 + 10.0 = 20.0             ← 지금(12.0)+10 이 아니다
  12.0     wait_until(20.0) → 8ms 대기
  20.0     ETIMEDOUT → ticks=2, 작업 2ms → next=30.0
   ...
 195.0     wait_until(200.0) 대기 중
 200.0                                           periodic_stop():
 200.0                                             lock; stop=true; unlock
 200.0                                             broadcast(cv)
 200.1     rc=0 → while 재검사 → stop==true
 200.1     return true → 루프 break → 종료        pthread_join 반환
```

마지막 부분이 "취소 가능한 대기"의 값이다. `sleep` 이었다면 200.0에 종료를 요청해도 210.0까지
자고 있었을 텐데, broadcast가 대기를 즉시 깨워 **0.1ms 만에** 종료된다. 12.0에서 `next = 20.0`
인 것도 다시 본다 — 작업에 2ms를 썼지만 다음 실행은 여전히 10ms 격자 위의 20.0이다.
`sleep(10ms)` 였다면 22.0, 34.0, 46.0으로 매 주기 2ms씩 밀린다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `sleep(period)` 루프 | 주기가 작업 시간만큼 계속 밀림 | 대기 기준이 작업 종료 시점 | 절대 deadline + `next += period` |
| `next = now + period` | 위와 동일한 drift | 기준이 또 현재 시각 | `next += period` |
| `CLOCK_REALTIME` 그대로 사용 | NTP 조정 때 타이머가 멈추거나 폭주 | `cond_timedwait` 기본 시계가 realtime | Linux는 `condattr_setclock`, macOS는 `_relative_np` |
| 대기를 `if` 로 작성 | 가끔 주기가 짧아짐 | spurious wakeup을 타임아웃으로 오인 | `while` + `remain` 재계산 |
| `stop` 을 락 없이 세팅 | 드물게 종료가 한 주기 늦음 | lost wakeup | 락 안에서 세팅 후 broadcast |
| 놓친 주기를 전부 따라잡음 | 버스트 실행, 큐 폭증, 더 밀림 | catch-up burst | 리셋하고 `late_ticks` 만 센다 |
| 놓친 것을 세지 않음 | 샘플 수가 모자란데 원인 불명 | 조용한 손실 | `late_ticks` 를 텔레메트리로 보고 |

---

## 7. 직접 확인하기

```sh
make sol N=07
```

```
ticks=17 late=1 elapsed=197.8ms avg_period=11.64ms stop_latency=90us
07 PASS
```

읽는 법은 이렇다.

- `ticks=17` — 200ms 동안 10ms 주기면 이론상 20틱이고 테스트는 15~25를 허용한다. 범용 OS에서 정확히 20이 안 나오는 게 정상이다.
- `late=1` — 주기를 한 번 놓쳤다. 작업 2ms + 스케줄링 지터가 10ms를 넘긴 순간이 한 번 있었다는 뜻이다.
- `avg_period=11.64ms` — 목표보다 크지만, 봐야 할 것은 "실행 시간을 늘려도 이 값이 계속 커지지 않는다"는 점이다. drift가 있으면 평균이 계속 올라간다.
- `stop_latency=90us` — 핵심 숫자다. 종료 요청부터 join까지 0.09ms. `sleep` 루프였다면 최대 한 주기였을 것이고, `assert(stop_latency_us < 5000)` 가 이걸 지킨다.

여러 번 돌려 `ticks` 와 `late` 가 흔들리는 걸 보는 것도 의미가 있다. **범용 OS이므로 주기 보장이 아니라 "누적되지 않는 평균"을 보장하는 설계다.**

`make tsan N=07` 은 경고가 없어야 한다. `stop`, `period_ns` 는 전부 `p->m` 안에서 접근하고,
`ticks`/`late_ticks` 는 sampler 스레드만 증가시키며 main은 join 이후에 읽는다. 더 정확한 주기가
필요하면 Linux의 `timerfd_create(CLOCK_MONOTONIC, ...)` 를 `epoll` 에 넣는 편이 낫다 — 놓친
만료 횟수를 커널이 대신 세준다.

---

## 8. 면접에서 말하기

- 주기 실행은 상대 sleep이 아니라 절대 deadline으로 만든다. 다음 deadline을 이전 deadline에 주기를 더해 구하면 작업 시간이 누적되지 않는다.
- 대기는 cond_timedwait으로 한다. 타임아웃과 종료 신호를 동시에 기다릴 수 있어서 종료 지연이 주기와 무관해진다.
- cond_timedwait의 기본 시계는 CLOCK_REALTIME이라 NTP 조정에 취약하다. Linux는 condattr로 CLOCK_MONOTONIC을 지정하고, macOS는 setclock이 없어 relative 버전을 쓴다.
- spurious wakeup이 있으니 반드시 while 안에서 남은 시간을 다시 계산한다.
- 주기를 놓쳤을 때 따라잡기는 하지 않는다. 몰아 찍은 값은 정보가 없고 시스템만 더 밀리게 하므로 놓친 횟수만 보고한다. 더 엄밀하게 가려면 Linux의 timerfd를 epoll 루프에 넣는다.

- I schedule on absolute deadlines and advance the next deadline by adding the period to the previous one, so work time never accumulates into drift.
- The wait is a timed condition wait, so a stop signal wakes the thread immediately instead of after one full period.
- The default clock for pthread_cond_timedwait is CLOCK_REALTIME, which NTP can move. On Linux I set CLOCK_MONOTONIC through a condattr; macOS has no setclock, so I use the relative_np variant.
- Spurious wakeups mean the wait has to sit in a while loop that recomputes the remaining time each pass.
- When a period is missed I do not try to catch up — bursting samples adds no information and makes the backlog worse; I reset the deadline and count the miss. For tighter timing on Linux I'd use timerfd with epoll, which also reports missed expirations.

---

## 9. 요약 & 체크리스트

주기 실행의 정답은 "절대 deadline + 취소 가능한 대기"다. deadline은 `next += period` 로 갱신해
작업 시간이 누적되지 않게 하고, 대기는 `pthread_cond_timedwait` 으로 해서 종료 신호가 오면 주기와
무관하게 즉시 깨어난다. 대기는 반드시 `while` 안에서 남은 시간을 재계산하고 `ETIMEDOUT` 과
`rc == 0` 을 구분한다. 시계는 `CLOCK_MONOTONIC` — Linux는 `condattr_setclock`, macOS는
`_relative_np`. 주기를 놓치면 따라잡지 말고 리셋한 뒤 `late_ticks` 로 보고한다.

- [ ] `sleep` 루프가 drift를 만드는 이유를 타임라인으로 그릴 수 있다
- [ ] `next += period` 와 `next = now + period` 의 차이를 설명할 수 있다
- [ ] `cond_timedwait` 의 기본 시계가 무엇이고 왜 문제인지 말할 수 있다
- [ ] Linux/macOS 이식 분기 두 줄을 각각 왜 쓰는지 안다
- [ ] `while` + `remain` 재계산이 spurious wakeup을 흡수하는 방식과, `stop` 을 락 안에서 세워야 하는 이유(lost wakeup)를 설명할 수 있다
- [ ] catch-up burst가 샘플링에서 왜 나쁜지, 반대로 맞는 경우는 언제인지 구분할 수 있다
- [ ] `stop_latency` 가 이 설계의 핵심 지표인 이유를 안다
