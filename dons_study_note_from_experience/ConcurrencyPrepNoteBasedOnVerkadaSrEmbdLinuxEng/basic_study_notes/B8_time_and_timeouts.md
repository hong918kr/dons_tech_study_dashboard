# B8. 시간 다루기 — 시계, 타임아웃, 주기 실행

> **이 노트를 읽고 나면**
> - wall clock과 monotonic clock을 구분해 쓰고, NTP가 시각을 점프시켜도 깨지지 않는 코드를 쓸 수 있다
> - `pthread_cond_timedwait`에 절대 시각을 넘기고, 가짜 기상에도 타임아웃이 늘어나지 않게 만들 수 있다
> - drift 없는 주기 루프와 토큰 버킷 rate limiter를 직접 짤 수 있다
>
> **선행**: [B5](B5_condvar_and_bounded_queue.md)(조건 변수와 bounded queue) — "lock 잡고 대기, 깨면 다시 검사" 패턴
>
> **이 개념을 쓰는 문제**
> - [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note) Part 1 — 업로더의 토큰 버킷 + timed wait (주 인용원)
> - [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note) Part 1 — monitor가 "다음 deadline까지" 정확히 자는 부분
> - [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) Part 1 — `evq_pop_timed(out, timeout_us)`
> - [02_modem_rssi_window](../02_modem_rssi_window/question_note) Part 1 — 값이 stale인지 판정하는 시각 비교

---

## 1. 왜 이게 필요한가

10개 문제 전부가 시간을 다루고, 막히는 지점은 늘 같은 네 군데다.

- **타임아웃 있는 대기.** 03번 `evq_pop_timed(out, 100000)`은 이벤트가 오면 바로, 안 오면 100 ms 뒤에 돌아와야 한다. `pthread_cond_wait`은 무한정 기다리고 `usleep`은 일찍 깨울 수 없다.
- **주기 실행.** 10번 monitor는 25 ms마다 하드웨어 watchdog을 kick한다. `while(1){ kick(); usleep(25000); }`은 매 바퀴 늦어지고, 그게 쌓이면 보드가 리셋된다.
- **rate limit.** 08번 업로더는 초당 5회를 넘기면 안 된다. 고정 슬립으로 막으면 종료가 그만큼 늦는다. 하네스가 잡는다 — `deinit woke the timed wait instead of waiting it out (< 1.5 s)`.
- **시각 비교.** 08번 히스토리 링은 정렬돼 있어야 이진 탐색이 성립한다. 벤더 stamp가 wall clock이면 NTP 한 번에 순서가 뒤집힌다.

넷 모두 "어떤 시계로 재고, 절대냐 상대냐, 부호 없는 정수로 어떻게 비교하냐"의 문제다.

---

## 2. 그림으로 먼저

시계가 뒤로 점프하면 정렬된 배열이 깨진다. 08번 히스토리 링에서 실제로 일어나는 일이다.

```svg
<svg viewBox="0 0 640 180" role="img" aria-label="NTP가 시각을 뒤로 되돌렸을 때 정렬된 배열이 깨지는 타임라인">
  <text x="10" y="16" class="lbl">실제 흐른 시간 (monotonic) →</text>
  <line class="muted" x1="10" y1="28" x2="630" y2="28"/>
  <text x="10" y="60">BMS stamp</text>
  <rect class="box" x="120" y="42" width="56" height="26" rx="6"/><text x="148" y="60" text-anchor="middle">100</text>
  <rect class="box" x="216" y="42" width="56" height="26" rx="6"/><text x="244" y="60" text-anchor="middle">200</text>
  <rect class="box" x="312" y="42" width="56" height="26" rx="6"/><text x="340" y="60" text-anchor="middle">300</text>
  <rect class="fill-soft" x="408" y="42" width="56" height="26" rx="6"/><text x="436" y="60" text-anchor="middle">250</text>
  <line class="dash" x1="392" y1="28" x2="392" y2="84"/>
  <text class="lbl" x="472" y="60">← NTP가 50 뒤로</text>
  <text x="10" y="118">그냥 append</text>
  <rect class="box" x="120" y="100" width="52" height="26" rx="6"/><text x="146" y="118" text-anchor="middle">100</text>
  <rect class="box" x="172" y="100" width="52" height="26" rx="6"/><text x="198" y="118" text-anchor="middle">200</text>
  <rect class="box" x="224" y="100" width="52" height="26" rx="6"/><text x="250" y="118" text-anchor="middle">300</text>
  <rect class="fill-soft" x="276" y="100" width="52" height="26" rx="6"/><text x="302" y="118" text-anchor="middle">250</text>
  <text class="lbl" x="338" y="118">정렬 깨짐 → floor(310)이 250을 "최신"이라 답한다</text>
  <text class="lbl" x="10" y="166">고치는 법: 직전 stamp보다 전진하지 않는 샘플은 드롭 → 링은 계속 정렬 상태</text>
</svg>
```

시계의 족보는 한 장이면 된다.

```
                        뒤로 가나?      쓰는 곳
CLOCK_REALTIME          YES (NTP/date)  로그 시각, 인증서 만료, 벤더가 찍어 주는 stamp
  = gettimeofday()
CLOCK_MONOTONIC         NO              경과 시간, 타임아웃, 주기, deadline, rate limit
  = 부팅 이후 경과
```

외울 문장: **"몇 시인가"는 REALTIME, "얼마나 지났나"는 MONOTONIC.**

---

## 3. 개념 (용어를 하나씩)

### wall clock (벽시계)

사람이 쓰는 달력 시각. `CLOCK_REALTIME`, 옛 API로는 `gettimeofday()`. 1970-01-01 UTC부터 센 초 + 마이크로초다. 로그와 장비 간 시각 비교에 필요하다. 함정은 **값이 바뀐다**는 것 — NTP가 맞추고, 관리자가 `date`로 고치고, 게이트웨이는 부팅 직후 1970년이다가 네트워크가 붙으면 2026년으로 뛴다. 점프는 앞으로도 뒤로도 일어난다.

### monotonic clock (단조 시계)

절대 뒤로 가지 않고 일정 속도로만 증가하는 카운터. `clock_gettime(CLOCK_MONOTONIC, &ts)`. 기준점이 보통 부팅 시각이라 "몇 시"인지는 모른다. **두 시점의 차이**를 재려면 시계가 흔들려서는 안 되는데, 타임아웃·주기·rate limit·"마지막 heartbeat 이후 몇 ms"가 전부 차이 계산이다. macOS도 지원한다. SPEC §6이 금지한 것은 `CLOCK_MONOTONIC_RAW`와 `pthread_condattr_setclock`이고 `CLOCK_MONOTONIC` 자체는 쓸 수 있다.

### 마이크로초 타임스탬프와 `uint64_t`

시각을 "기준점 이후 마이크로초 수" 하나의 정수로 표현한 것. 뺄셈 한 번이 곧 경과 시간이다. 타입은 계산으로 결정된다. `uint32_t`는 최대 4 294 967 295 µs = **4295초 ≈ 71분**이라 안 된다. `uint64_t`는 1.84e19 µs = **584 542년**이고, 오늘 값은 약 1.79e15 µs로 **51비트**만 쓴다. `double`은 53비트 mantissa라 1.79e15가 µs 정밀도의 경계이므로 더하면 반올림이 생긴다.

### 절대 시각 vs 상대 시간, drift, 토큰 버킷

**절대(absolute)** = "1790529824.826595 시점에" = deadline. **상대(relative)** = "지금부터 200 ms 동안" = duration. `pthread_cond_timedwait`은 **절대**를 받고 `usleep`/`nanosleep`은 **상대**를 받는다. 이 구분을 놓치는 것이 초보자 실수 1위다. **drift**는 "일 하고 → 주기만큼 잔다"를 반복할 때 일 하는 시간과 깨는 지연이 매 바퀴 더해져 실제 주기가 목표보다 커지는 현상이다. 오차가 **누적**된다는 점이 핵심이다.

**토큰 버킷**은 용량 `B`인 양동이에 초당 `R`개씩 토큰이 채워지고 작업 한 번에 토큰 한 개를 쓰는 구조다. 없으면 찰 때까지 기다린다. `R`은 장기 평균 상한, `B`는 순간에 몰아 쓸 수 있는 양이다.

---

## 4. 코드로 보기

### 마이크로초 시계 두 개

```c
static uint64_t wall_us(void)          /* 벽시계: 로그·벤더 stamp 비교용 */
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec;
}

static uint64_t mono_us(void)          /* 단조 시계: 경과·타임아웃·주기 */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}
```

**`(uint64_t)` 캐스팅이 없으면?** `tv.tv_sec * 1000000`이 `long` 곱셈이 되어 32비트 타깃에서 오버플로가 난다. 캐스팅은 **곱하기 전에** 붙인다. 단위를 섞는 순간 1000배 버그가 난다.

### 시간 비교의 함정 — 부호 없는 뺄셈

`now - then > WINDOW`는 **then이 미래일 때 반드시 뒤집힌다.** 실제로 돌려 본 출력이다.

```
now=1000000 then=1500000
  now - then            = 18446744073709051616
  stale? (now-then>W)   = YES   <-- wrong
  stale? (then<now && now-then>W) = no   <-- right
```

`then`이 0.5초 **미래**인데 "2초 넘게 오래됐다"고 답한다. `1000000 - 1500000`이 -500000이 아니라 2^64 - 500000이 되기 때문이다. then이 미래일 이유는 흔하다 — 벤더가 자기 시계로 찍은 stamp, NTP가 앞으로 점프한 직후, 두 시계를 섞은 코드.

```c
/* 나쁨 */  if (now - last > WINDOW) stale();
/* 좋음 */  if (now > last && now - last > WINDOW) stale();
/* 또는 */  if (last + WINDOW < now) stale();   /* last+WINDOW 오버플로만 없으면 */
```

03번 `evq_count_since`가 같은 방어를 명시적으로 한다 — `if (since_us > now) return 0;  /* nothing has happened in the future yet */`. 08번의 `bucket_refill_locked`도 `if (now > tokens_last_us)`로 감싼 다음에야 뺀다. **규칙: 부호 없는 시각끼리는 뺄셈 전에 대소를 먼저 비교한다.**

### `pthread_cond_timedwait` — 절대 시각과 시계 선택

`pthread_cond_timedwait(cv, mutex, const struct timespec *abstime)`의 `abstime`은 **절대 시각**이고, 기본 condvar가 그 값을 해석하는 시계는 **`CLOCK_REALTIME`**이다. 이게 두 번째 함정이다 — NTP가 뒤로 점프하면 타임아웃이 늘어나고 앞으로 점프하면 즉시 만료된다. Linux는 condvar를 만들 때 시계를 바꿀 수 있다.

```c
/* Linux 전용 — 이 저장소 SPEC §6이 금지한 API다. 실무에서는 이게 정답이다. */
pthread_condattr_t at;
pthread_condattr_init(&at);
pthread_condattr_setclock(&at, CLOCK_MONOTONIC);   /* 이 한 줄이 핵심 */
pthread_cond_init(&cv, &at);
pthread_condattr_destroy(&at);
/* 이후 abstime은 clock_gettime(CLOCK_MONOTONIC) 기준으로 만든다 */
```

macOS에는 이 함수가 없고, 대신 **상대 시간**을 받는 비표준 함수가 있다. 08번 답안 그대로다.

```c
/* 08_battery_energy_pipeline/energy_store_solution.c */
static void cond_wait_us(pthread_cond_t *cv, pthread_mutex_t *m, uint64_t us)
{
#ifdef __APPLE__
    /* macOS has no condattr clock selection, but it does have a relative wait,
     * which is what we actually want and is immune to wall-clock jumps. */
    struct timespec rel;
    rel.tv_sec  = (time_t)(us / 1000000ull);
    rel.tv_nsec = (long)((us % 1000000ull) * 1000ull);
    pthread_cond_timedwait_relative_np(cv, m, &rel);
#else
    /* A default condvar waits on CLOCK_REALTIME, the same clock gettimeofday()
     * reads, so build the absolute deadline from gettimeofday(). */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t deadline = 1000000ull * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec + us;
    struct timespec abs;
    abs.tv_sec  = (time_t)(deadline / 1000000ull);
    abs.tv_nsec = (long)((deadline % 1000000ull) * 1000ull);
    pthread_cond_timedwait(cv, m, &abs);
#endif
}
```

**왜 이 짝이 맞는가**: 절대 시각을 넘기는 쪽에서는 deadline을 만드는 시계와 condvar가 보는 시계가 **같아야** 한다. `mono_us()`로 만든 값을 넘기면 deadline이 1970년대 값이 되어 즉시 timeout된다 — "timedwait이 바로 리턴한다" 버그의 정체다. 10번의 `wd_wait_until(abs_us)`도 같은 `#ifdef` 구조이고, 상단 주석이 한계를 적어 놓았다: 상대 대기는 점프에 면역이지만 **저장된 타임스탬프는 아니다.**

---

## 5. 단계별로 만들어 보기

### (가) 타임아웃 — 깨어날 때마다 남은 시간을 다시 계산한다

condvar는 이유 없이 깨어날 수 있고(spurious wakeup), 다른 소비자가 내 몫을 가져가서 predicate가 여전히 거짓일 수도 있다. 그래서 대기는 항상 루프 안에 있고, 문제는 루프마다 타임아웃을 다시 세는 방법이다.

**v0 — 틀렸다: 매번 상대 시간을 새로 준다**

```c
while (!ready) {
    if (wait_until(now_us() + 200000) == ETIMEDOUT) break;   /* 매번 200 ms 새로 */
}
```

**v1 — 맞다: 절대 deadline을 한 번만 계산한다**

```c
uint64_t deadline = now_us() + 200000;     /* 루프 밖에서 한 번 */
while (!ready) {
    if (wait_until(deadline) == ETIMEDOUT) break;
}
```

같은 조건(200 ms 예산, 60 ms마다 broadcast 4회)으로 돌린 실제 출력이다.

```
budget 200000 us, 4 wakeups 60 ms apart
v0 relative restart : 462020 us
v1 absolute deadline: 201438 us
```

v0은 약속의 **2.3배**를 기다렸다. 깨어날 때마다 예산이 리셋되기 때문이다. 다른 소비자가 push/pop만 반복해도 이 대기는 약속한 시간에 끝나지 않는다. 03번 답안이 v1 모양이고 주석이 이유다: *"The deadline is absolute on purpose — a relative wait restarted after a spurious wakeup would stretch the timeout."* 그 함수의 첫 두 줄도 중요하다 — `if (now >= deadline_us) return ETIMEDOUT;` 다음에야 `uint64_t left = deadline_us - now;`를 계산한다. 이 검사가 없으면 뺄셈이 wrap해서 `left`가 58만 년이 된다. §4의 함정이 여기서도 나온다.

### (나) 주기 실행 — drift를 없앤다

**v0 — 틀렸다: 일 하고 주기만큼 잔다**

```c
for (;;) { work(); usleep(100000); }         /* 목표 주기 100 ms */
```

실제 주기 = work 시간 + 100 ms + 스케줄 지연. 7 ms 걸리는 work로 20바퀴 돌린 측정값이다.

```
v0 sleep(period)  : 2258020 us total, 112901 us/iter (ideal 100000)
drift after 20 iters: v0 +258020 us
```

한 바퀴당 13 ms 늦고 20바퀴에 **258 ms** 밀렸다. 10번 watchdog을 이 루프로 kick하면 하드웨어 창을 넘겨 보드가 리셋된다.

**v1 — 맞다: 절대 deadline을 더해 나간다**

```c
uint64_t next = mono_us() + PERIOD_US;
for (;;) {
    work();
    uint64_t n = mono_us();
    if (next > n) usleep((useconds_t)(next - n));   /* 남은 만큼만 잔다 */
    next += PERIOD_US;                              /* 항상 +주기. 현재 시각을 안 쓴다 */
}
```

```
v1 absolute deadline: 2004467 us total, 100223 us/iter
drift after 20 iters: v1 +4467 us
```

258 ms → **4.5 ms**. 오차가 누적되지 않고 매 바퀴 독립적으로만 생긴다. 핵심은 `next += PERIOD_US` 이고, `next = mono_us() + PERIOD_US`로 쓰면 v0으로 되돌아간다.

**v2 — 주기를 놓쳤을 때 몰아서 실행하지 않는다**

work가 주기보다 오래 걸리면 `next`가 과거가 되고, 그대로 두면 밀린 만큼 연달아 실행된다. 이 catch-up burst는 부하가 높아 늦어진 상황에 일감을 더 쏟는 것이라 거의 항상 나쁘다.

```c
uint64_t now = mono_us();
if (next <= now) {
    uint64_t missed = (now - next) / PERIOD_US + 1;
    skipped += missed;                  /* 세어 둔다. 조용히 삼키지 않는다 */
    next += missed * PERIOD_US;         /* 다음 정상 슬롯으로 정렬 */
}
```

10번도 같은 정신으로 최소 대기를 둔다 — `if (wake <= now) wake = now + 200;  /* never busy-spin */`. 안 그러면 monitor가 lock을 잡은 채 코어를 태운다.

### (다) 토큰 버킷 rate limiter

```svg
<svg viewBox="0 0 620 170" role="img" aria-label="토큰 버킷: 초당 R개 채워지고 용량 B에서 넘친다">
  <text class="lbl" x="10" y="18">refill: 초당 R = 5 토큰</text>
  <line class="accent" x1="70" y1="26" x2="70" y2="50"/><polygon class="accent" points="70,54 65,44 75,44"/>
  <rect class="box" x="20" y="56" width="110" height="80" rx="8"/>
  <rect class="fill-soft" x="26" y="98" width="98" height="32" rx="5"/>
  <text x="75" y="119" text-anchor="middle">tokens</text>
  <text class="lbl" x="75" y="154" text-anchor="middle">용량 B = 2 (넘치면 버림)</text>
  <rect class="box" x="240" y="20" width="120" height="42" rx="8"/><text x="300" y="46" text-anchor="middle">업로드 큐</text>
  <line class="muted" x1="300" y1="62" x2="300" y2="88"/><polygon class="muted" points="300,92 295,82 305,82"/>
  <line class="accent" x1="130" y1="114" x2="234" y2="114"/><polygon class="accent" points="238,114 228,109 228,119"/>
  <text class="lbl" x="182" y="105" text-anchor="middle">1회 = 토큰 1</text>
  <rect class="box" x="240" y="92" width="120" height="42" rx="8"/><text x="300" y="118" text-anchor="middle">bms_upload()</text>
  <text class="lbl" x="378" y="48">토큰이 없으면 cond_wait_us(필요한 만큼)</text>
  <text class="lbl" x="378" y="72">→ 폴링도 고정 슬립도 아니다</text>
  <text class="lbl" x="378" y="96">→ deinit 이 broadcast 한 번으로 즉시 깨운다</text>
</svg>
```

정수 milli-token 버전이다. 08번 답안은 `double tokens`를 쓰지만 부동소수를 피해야 하는 환경을 위해 정수로 바꿨다.

```c
#define RATE_PER_SEC 5u
#define BURST        2u
#define MILLI        1000u              /* 1 token == 1000 milli-token */

static uint32_t mtokens;                /* milli-token, lock 아래 */
static uint64_t last_us;

static void refill(uint64_t now)
{
    if (now <= last_us) return;                   /* 뒤로 가는 시계 방어 */
    uint64_t add = (now - last_us) * RATE_PER_SEC * MILLI / 1000000ull;
    if (add == 0) return;                          /* 나머지를 버리지 않는다 */
    mtokens += (uint32_t)add;
    if (mtokens > BURST * MILLI) mtokens = BURST * MILLI;   /* 이게 '버킷'이다 */
    last_us = now;
}

static uint64_t wait_us_for_one(void)
{
    if (mtokens >= MILLI) return 0;
    uint32_t need = MILLI - mtokens;
    return (uint64_t)need * 1000000ull / (RATE_PER_SEC * MILLI) + 1;   /* +1: 올림 */
}
```

- **`if (add == 0) return;`이 없으면?** `add`가 매번 0으로 내림되는데 `last_us`는 전진한다. 시간은 흐르지만 토큰이 영원히 안 찬다. 나머지를 남기는 방법이 "0일 때 `last_us`를 안 건드리기"다.
- **상한이 없으면?** 1분 놀다 온 업로더가 300회를 연달아 쏜다. 상한이 버킷을 버킷으로 만든다.
- **`+1`이 없으면?** 정수 나눗셈 내림 때문에 조금 일찍 깨고 다시 잔다. 한 바퀴 낭비다.

12회를 통과시킨 실제 출력이다. 처음 2회는 버킷이 꽉 차 있어 즉시(burst), 이후는 200 ms 간격이다.

```
upload  1 at      0 ms  (bucket 1.000)      upload  4 at    405 ms  (bucket 0.025)
upload  2 at      0 ms  (bucket 0.000)      ...
upload  3 at    205 ms  (bucket 0.025)      upload 12 at   2005 ms  (bucket 0.025)
12 uploads in 2005 ms -> 5.98/s (limit 5/s + burst 2)
```

어떤 1초 창을 잡아도 `B + R = 7`회를 넘지 않는다. 08번 주석의 표현이 정확하다 — *"provable rather than measured"*.

**왜 08번 업로더가 이걸 쓰는가.** sampler는 절대 막히면 안 되고, 업로더는 초당 5회를 넘기면 안 되고, `deinit`은 즉시 끝나야 한다. `pthread_cond_wait`은 "토큰이 찰 때까지"를 표현할 수 없고 (아무도 signal 하지 않는다), `usleep(200000)`은 일찍 깨울 수 없어 `deinit`이 200 ms를 기다린다. **timed wait**만이 필요한 만큼 자고 broadcast로 즉시 깬다.

**타임아웃 값은 어떻게 고르나.** (1) **벤더 블로킹 대비** — 08번 BMS는 60 ms 블로킹인데 getter의 최악 지연 목표는 20 ms 미만이다(`worst getter latency < 20 ms (BMS blocks 60, throttle 200)`). 10번은 하드웨어 타임아웃의 1/4을 kick 주기로 잡았다 — 늦은 wake-up 한 번이 리셋으로 이어지지 않게 하는 여유 배수다. (2) **테스트 가능성** — 하네스가 10초 안에 끝나야 하므로 원본의 1초 블로킹을 50~100 ms로 줄였다(SPEC §3).

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| abstime에 상대 시간을 넣거나 deadline을 MONOTONIC으로 만든다 | 즉시 `ETIMEDOUT`, CPU 100% | abstime은 1970 기준 절대 시각이고 기본 condvar는 REALTIME으로 해석한다 | `gettimeofday() + timeout`으로 만들거나 Linux에서 `condattr_setclock` |
| 깰 때마다 `now + timeout`을 다시 계산 | 타임아웃이 약속의 2~3배 (200 ms → 462 ms) | 가짜 기상마다 예산이 리셋된다 | deadline을 루프 **밖**에서 한 번 계산 |
| `if (now - last > WINDOW)`로 staleness 판정 | 미래 stamp를 "아주 오래됨"으로 오판 | 부호 없는 뺄셈은 음수 대신 2^64로 wrap | `now > last &&`를 앞에 붙인다 |
| `while(1){ work(); usleep(period); }` | 20바퀴에 258 ms drift, watchdog 리셋 | work 시간과 깨는 지연이 누적된다 | `next += PERIOD`로 절대 deadline 누적 |
| 토큰 버킷에 상한이 없다 | 1분 idle 후 300회 폭주 | 상한이 없으면 버킷이 아니라 카운터다 | `if (tokens > BURST) tokens = BURST;` |
| 벤더 wall-clock stamp를 정렬 링에 그대로 append | `floor(310)`이 250을 최신이라 답하고 적분이 음수 | NTP가 뒤로 점프하면 순서가 깨진다 | 직전 stamp와 비교해 전진 안 하면 드롭 |

---

## 7. 손으로 확인하기

```sh
cd 08_battery_energy_pipeline && ./main.sh sol
# phase 4: rate limit  — 어떤 1초 창에서도 upload 수가 burst+rate 이하
# phase 5: shutdown    — "deinit took N us", N < 1500000 이어야 PASS
```

`energy_store_solution.c`의 `cond_wait_us` 호출을 `usleep(need)`로 바꾸면 phase 5의 dwell이 200 ms 근처로 뛴다. timed wait을 쓰는 이유가 그 숫자다.

```sh
cd 10_heartbeat_watchdog  && ./main.sh sol   # monitor가 다음 deadline / 다음 kick 중 이른 쪽까지만 잔다
cd 03_event_tailer_shutdown && ./main.sh sol # "pop_timed after deinit returns -1 quickly (N us)"
```

drift는 직접 재 보는 것이 가장 빠르다. §5(나)의 v0/v1을 한 파일에 넣고 `cc -std=c11 -O2 -Wall -Wextra -pthread -o drift drift.c && ./drift`로 돌린 뒤 `PERIOD_US`와 `work()`의 `usleep`을 바꿔 가며 v0의 밀림이 work 시간에 비례하는 것을 확인한다.

### 체크리스트

- [ ] `mono_us()`/`wall_us()`를 안 보고 쓰고, 기본 condvar에 넘길 deadline을 어느 시계로 만드는지 답할 수 있다
- [ ] 200 ms 예산의 대기 루프를 deadline 하나로 쓸 수 있다
- [ ] drift 없는 100 ms 주기 루프를 5줄로 쓸 수 있다
- [ ] 토큰 버킷 refill + 대기 시간 계산을 정수로 쓸 수 있다

---

## 8. 자가 점검

```check
Q: `pthread_cond_timedwait`에 `{ .tv_sec = 0, .tv_nsec = 200000000 }`(200 ms)을 넘기면 어떻게 되는가? 그리고 이 저장소는 왜 `pthread_condattr_setclock(CLOCK_MONOTONIC)`을 쓰지 않는가?
A: 즉시 ETIMEDOUT으로 리턴한다. abstime은 절대 시각이고 기본 condvar는 CLOCK_REALTIME으로 해석하므로 그 값은 1970-01-01 00:00:00.2 라는 뜻이다. 올바른 값은 gettimeofday로 읽은 지금 + 200 ms 다. condattr_setclock은 SPEC §6이 금지한 Linux 전용 API이고 macOS에는 아예 없다. macOS는 `pthread_cond_timedwait_relative_np`로 상대 시간을 넘기며, 08·03·10번 모두 `#ifdef __APPLE__`로 갈라 놓았다. 상대 대기는 벽시계 점프에 면역이라 오히려 원하던 동작에 가깝다.
```

```check
Q: 200 ms 예산의 대기 루프에서 가짜 기상이 타임아웃을 연장하지 않게 하려면?
A: 루프에 들어가기 전에 절대 deadline을 한 번 계산하고 루프 안에서는 그 값을 계속 넘긴다. 깰 때마다 `now + timeout`을 새로 만들면 예산이 리셋된다. 60 ms마다 4번 깨우는 조건에서 200 ms 예산이 462 ms가 되는 것을 측정으로 확인했다.
```

```check
Q: `now`와 `last`가 모두 `uint64_t`일 때 `now - last > WINDOW`가 위험한 이유는?
A: `last`가 미래면 뺄셈이 2^64로 wrap해서 1.8e19 같은 값이 되고, 어떤 WINDOW보다 크므로 항상 참이 된다. 벤더가 자기 시계로 찍은 stamp나 NTP가 앞으로 점프한 직후에 실제로 일어난다. `now > last`를 먼저 검사하거나 `last + WINDOW < now`로 쓴다.
```

```check
Q: `for(;;){ kick(); usleep(25000); }`이 하드웨어 watchdog에 왜 위험한가?
A: 실제 주기가 25 ms + kick 시간 + 스케줄 지연이 되고 오차가 누적된다. 측정에서 100 ms 목표가 113 ms/바퀴가 됐다. 하드웨어 창을 한 번만 넘겨도 보드가 리셋된다. `next += PERIOD`로 절대 deadline을 누적하고, 10번처럼 하드웨어 타임아웃의 1/4 주기로 여유를 둔다.
```

```check
Q: 토큰 버킷에서 burst 용량 B와 refill 속도 R은 각각 무엇을 제어하는가?
A: R은 장기 평균 상한(초당 R회), B는 순간에 몰아 쓸 수 있는 양이다. 둘을 합치면 임의의 1초 창 안의 최대 호출 수가 B + R로 증명된다. 08번은 R=5, B=2이므로 1초에 최대 7회다. B가 없으면 오래 idle했던 업로더가 폭주하고, B=1이면 잠깐 몰린 일감을 흘려보낼 수 없다.
```

```check
Q: 08번 히스토리 링은 wall clock stamp를 어떻게 방어하는가?
A: `hist_append`가 `if (hist_count > 0 && s->timestamp <= H(hist_count-1)->ts) return;`으로 전진하지 않는 샘플을 드롭한다. 정렬이 깨지면 `floor_index`의 이진 탐색이 의미를 잃고 prefix sum 뺄셈이 음수 에너지를 낸다. stamp가 100,200,300,250 순으로 들어오면 `floor(310)`이 250을 "가장 최근"이라 답하는 것을 실측으로 확인했다.
```

---

## 9. 요약 카드

| 질문 | 답 |
| --- | --- |
| "몇 시인가" | `CLOCK_REALTIME` / `gettimeofday` — 점프한다 |
| "얼마나 지났나" | `CLOCK_MONOTONIC` — 절대 뒤로 안 간다 |
| 시각 저장 타입 | `uint64_t` µs (uint32는 71분에 wrap) |
| `pthread_cond_timedwait` 인자 | **절대** 시각, 기본 시계는 **CLOCK_REALTIME**, macOS는 `..._relative_np` |
| deadline 만드는 시계 | condvar가 보는 시계와 **같아야** 하고, 루프 **밖**에서 한 번만 계산 |
| 부호 없는 시각 비교 | 뺄셈 전에 대소를 먼저 검사 |
| 주기 실행 | `next += PERIOD`, 남은 만큼만 잠, 몰아서 실행 금지 |
| rate limit | 토큰 버킷 = refill(R) + 상한(B) + timed wait |
| 왜 timed wait인가 | "시간이 됐거나 일이 왔거나"를 둘 다 깨울 수 있는 유일한 수단 |

영어로 말할 문장 셋:

- "The deadline is absolute on purpose — a relative wait restarted after a spurious wakeup would stretch the timeout."
- "Timeouts and periods go on a monotonic clock; only human-readable timestamps go on the wall clock."
- "The token bucket bounds uploads in any one-second window at burst + rate. That is provable, not measured."
