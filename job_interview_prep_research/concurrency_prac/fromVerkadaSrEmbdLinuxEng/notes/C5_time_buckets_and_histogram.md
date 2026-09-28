# C5. 시간 버킷 — 이벤트를 세는 값싼 방법

> **이 노트를 읽고 나면**
> - "최근 5분 타입별 이벤트 수"를 이벤트 수와 **무관한** 메모리·시간으로 답할 수 있다.
> - lazy advance(게으른 전진)를 직접 쓰고, 왜 청소 스레드가 필요 없는지 설명할 수 있다.
> - 절대 버킷 번호로 stale 슬롯을 구분해 **eviction 패스가 아예 필요 없어지는** 기법을 쓸 수 있다.
>
> **선행**: [C4. 슬라이딩 윈도우와 단조 덱](C4_sliding_window_and_monotonic_deque.md) — "샘플을 다 들고 있으면 왜 지는가"를 먼저 본다.
>
> **이 개념을 쓰는 문제**
> - [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) **Part 2** — `evq_count_since(type, since_us)`. 링 버킷 + lazy advance의 교과서 예다.
> - [04_temp_single_flight](../04_temp_single_flight/question_note) **Part 2** — `temp_range_stats()`. 버킷 안에 min/max/sum/count를 넣고 끝단만 원시 샘플로 정확히 센다.
> - [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) Part 2 — 해시맵의 시간 만료를 버킷으로 돌리는 변형.

---

## 1. 왜 이게 필요한가

03번 지문의 Part 2는 이렇게 못을 박는다.

> `evq_count_since()` — Must run in **O(EVQ_NBUCKETS)**, never O(number of events).

"이벤트 개수에 비례하면 안 된다"는 요구다. 왜 개수가 위험한지 숫자로 보자.

- 게이트웨이 한 대에 출입문 8개. 탬퍼(tamper) 경보가 터지면 문마다 **초당 수백 개**가 나온다.
- 5분 창에 초당 200개면 **60,000개**. 이벤트 구조체가 24 B면 **1.4 MB**.
  64 MiB RAM 게이트웨이에서 이건 "메모리가 입력 속도에 비례해 자란다"는 뜻이고, 상한이 없다.
- 그리고 모니터링 질의는 **바로 그 버스트 중에** 가장 많이 들어온다. 6만 개 리스트를 훑는 동안
  샘플러는 이벤트를 기록할 수 없다.
- 창을 벗어난 것을 버리는 것도 공짜가 아니다. 조용한 밤을 지나 아침에 첫 질의가 오면
  만료 패스 하나가 수만 개를 한 번에 지운다 — 잠금 안에서의 지연 스파이크다.

`03_event_tailer_shutdown/event_queue_solution.c`가 같은 이야기를 주석으로 남겨 둔다.

```
 * Why not keep the events themselves?
 *   A per-event list [...] makes count_since() O(number of events in the window),
 *   and the number of events is exactly the thing we do not control: a tamper
 *   storm on 8 doors can produce thousands of events per second, and a burst is
 *   when a monitoring query is most likely to be running.
```

**발상의 전환**: 질문이 "무슨 이벤트였나"가 아니라 **"몇 개였나"**다. 개수만 필요하면
이벤트를 보관할 이유가 없다. 시간을 고정 폭으로 자르고 **칸마다 카운터 하나**만 두면 된다.
버스트가 와도 늘어나는 것은 `uint32_t` 하나의 값뿐이다.

## 2. 그림으로 먼저

타임라인을 고정 폭으로 자르고, 그 칸들을 **링으로 감는다.**

```svg
<svg viewBox="0 0 680 320" role="img" aria-label="타임라인을 버킷으로 자르고 링으로 감는 그림">
  <text class="lbl" x="8" y="16">① 타임라인을 버킷 폭(1 s)으로 자른다. 이벤트는 자기 버킷의 카운터를 1 올린다.</text>
  <line class="muted" x1="20" y1="76" x2="650" y2="76"/>
  <rect class="box" x="20" y="30" width="86" height="46" rx="4"/><text x="63" y="58" text-anchor="middle">b=97</text>
  <rect class="box" x="106" y="30" width="86" height="46" rx="4"/><text x="149" y="58" text-anchor="middle">b=98</text>
  <rect class="fill-soft" x="192" y="30" width="86" height="46" rx="4"/><text x="235" y="58" text-anchor="middle">b=99</text>
  <rect class="fill-soft" x="278" y="30" width="86" height="46" rx="4"/><text x="321" y="58" text-anchor="middle">b=100</text>
  <text class="lbl" x="63" y="94" text-anchor="middle">n=[2,0,1]</text>
  <text class="lbl" x="149" y="94" text-anchor="middle">n=[0,0,0]</text>
  <text class="lbl" x="235" y="94" text-anchor="middle">n=[5,1,0]</text>
  <text class="lbl" x="321" y="94" text-anchor="middle">n=[1,0,0]</text>
  <text class="lbl" x="380" y="58">타입별 카운터 배열 — 이벤트 본문은 버린다</text>
  <text class="lbl" x="380" y="76">버킷 번호 b = ts / BUCKET_US</text>

  <text class="lbl" x="8" y="140">② 버킷 8개만 실제로 존재한다. b % 8 로 링에 감긴다 → 메모리 고정.</text>
  <rect class="box" x="30" y="158" width="66" height="44" rx="4"/><text x="63" y="185" text-anchor="middle">0</text>
  <rect class="box" x="100" y="158" width="66" height="44" rx="4"/><text x="133" y="185" text-anchor="middle">1</text>
  <rect class="fill-soft" x="170" y="158" width="66" height="44" rx="4"/><text x="203" y="185" text-anchor="middle">2</text>
  <rect class="box" x="240" y="158" width="66" height="44" rx="4"/><text x="273" y="185" text-anchor="middle">3</text>
  <rect class="box" x="310" y="158" width="66" height="44" rx="4"/><text x="343" y="185" text-anchor="middle">…</text>
  <rect class="box" x="380" y="158" width="66" height="44" rx="4"/><text x="413" y="185" text-anchor="middle">7</text>
  <text class="lbl" x="63" y="220" text-anchor="middle">b=96,104</text>
  <text class="lbl" x="203" y="220" text-anchor="middle">b=98,106</text>
  <line class="accent" x1="446" y1="180" x2="470" y2="180"/>
  <path class="dash" d="M470 180 C 520 180 520 250 63 250"/>
  <line class="accent" x1="63" y1="250" x2="63" y2="206"/><polygon class="accent" points="63,206 58,216 68,216"/>
  <text class="lbl" x="250" y="266" text-anchor="middle">한 바퀴 돌면 같은 슬롯을 재사용한다 — 옛 값은 반드시 0으로 지워야 한다</text>
  <text class="lbl" x="8" y="300">③ b=106 이 오면 슬롯 2 는 b=98 의 카운터를 들고 있다. 이것이 stale 슬롯 문제다.</text>
</svg>
```

정확히 그 stale 슬롯을 다루는 두 가지 방법이 이 노트의 4·5절이다.

```
lazy advance — 새 이벤트가 올 때, 건너뛴 버킷만 0으로 지운다

cur = 100 (마지막으로 본 버킷)          새 이벤트 ts → b = 103
slot:   0    1    2    3    4    5    6    7
b:     96   97   98   99  100  101  102  103        ← 슬롯이 담고 있던 버킷
       ↑                   ↑    ↑    ↑    ↑
     오래됨              cur   지운다 지운다 지운다(그리고 여기에 기록)

지우는 개수 = b - cur = 3.  b - cur >= 8 이면 memset 한 번으로 전체 초기화.
```

## 3. 개념 (용어를 하나씩)

### 버킷(bucket)과 버킷 번호

**버킷** = 고정 폭의 시간 구간. **버킷 번호** = `b = ts / BUCKET_US`, 즉 에포크부터 센 절대 번호다.
나눗셈 하나로 "이 이벤트가 어느 칸인가"가 끝난다. 이 번호가 **절대값**인 것이 중요하다 —
`b % NBUCKETS`가 저장 위치이고, `b` 자체는 "어느 시대의 칸인가"를 말해 준다.

### 히스토그램(histogram)

값의 범위를 칸으로 나누고 칸마다 개수를 세는 구조. 시간 버킷은 **가로축이 시간인 히스토그램**이다.
답은 "칸 단위로 반올림된 개수"이고, 정확한 개수가 아니다. 그 손실이 이 구조의 가격이다.

### 메모리 계산 (먼저 계산하고 고른다)

`메모리 = 버킷 수 × 타입 수 × 카운터 크기`. 03번의 기본값으로:

| 설정 | 계산 | 메모리 | 해상도 |
| --- | --- | --- | --- |
| 300 버킷 × 1 s (기본) | 300 × 5 × 4 B | **6 KB** | ±1 s |
| 3000 버킷 × 100 ms | 3000 × 5 × 4 B | 60 KB | ±100 ms |
| 5 버킷 × 60 s | 5 × 5 × 4 B | 100 B | ±60 s |
| 이벤트 리스트 (비교) | 60,000 × 24 B | **1.4 MB**, 상한 없음 | 정확 |

**trade-off는 정밀도 ↔ 메모리**이고, 둘 다 **이벤트 개수와 무관**하다. 이게 버킷의 전부다.
면접에서는 이 표를 말로 하면 된다: *"Memory is buckets × types × 4 bytes — it does not depend on
the event rate at all. The price is resolution: the answer is rounded to one bucket."*

카운터 폭도 정해야 한다. `uint32_t`는 한 버킷에 43억 개까지 센다 — 1초 버킷이면 넘칠 수 없다.
`uint16_t`(65535)로 줄이면 메모리는 절반이지만 버스트에서 감싸 돌 수 있다. 감싸 돌면 개수가
**줄어든** 것처럼 보이므로, 줄일 거면 포화(saturating) 증가로 막는다.

### lazy advance (게으른 전진)

버킷을 지우는 일을 **다음 이벤트가 올 때까지 미루는 것**. 타이머도, 청소 스레드도 없다.

## 4. 코드로 보기

03번 해답의 삽입 경로다. `03_event_tailer_shutdown/event_queue_solution.c` 그대로다.

```c
static void cnt_add(const struct Event *e)
{
    uint64_t b = e->timestamp / (uint64_t)EVQ_BUCKET_US;   /* (1) 버킷 번호 */

    pthread_mutex_lock(&g_cnt.lock);
    if (e->type < 0 || e->type >= EV_TYPE_COUNT) {
        g_cnt.unknown++;             /* never index an array with vendor data */
        pthread_mutex_unlock(&g_cnt.lock);
        return;                                           /* (2) */
    }

    if (!g_cnt.primed) {
        g_cnt.cur = b;
        g_cnt.primed = true;                              /* (3) 첫 이벤트 */
    } else if (b > g_cnt.cur) {
        if (b - g_cnt.cur >= (uint64_t)EVQ_NBUCKETS) {
            memset(g_cnt.n, 0, sizeof g_cnt.n);          /* (4) 한 바퀴 이상 */
        } else {
            for (uint64_t k = g_cnt.cur + 1; k <= b; k++)
                memset(g_cnt.n[k % (uint64_t)EVQ_NBUCKETS], 0, sizeof g_cnt.n[0]);
        }
        g_cnt.cur = b;                                    /* (5) */
    }

    /* A stamp older than the whole window has no bucket to live in any more. */
    if (b + (uint64_t)EVQ_NBUCKETS > g_cnt.cur)           /* (6) */
        g_cnt.n[b % (uint64_t)EVQ_NBUCKETS][e->type]++;

    pthread_mutex_unlock(&g_cnt.lock);
}
```

한 줄씩.

- **(1)** 나눗셈 한 번. 버킷 폭이 2의 거듭제곱 µs면 시프트로 바뀐다. 굳이 최적화할 필요는 없다.
- **(2)** 벤더가 준 `type`으로 **배열을 절대 바로 인덱싱하지 않는다.** **이 검사가 없으면?**
  벤더가 `type = 99`를 주는 순간 `n[b][99]`를 써서 다른 버킷의 카운터를 오염시키거나 BSS를 넘어간다.
- **(3)** 첫 이벤트로 `cur`을 초기화한다. **이게 없으면?** `cur = 0`이므로 첫 이벤트의
  `b - cur`이 에포크 이후의 초 수(약 17억)가 되어 `(4)`의 전체 memset을 매번 타게 된다 —
  틀리지는 않지만 첫 이벤트가 이상하게 비싸진다.
- **(4)** `b - cur >= NBUCKETS`면 **살아남을 버킷이 하나도 없다.** 루프를 170만 번 돌리는 대신
  `memset` 한 번으로 끝낸다. 밤새 조용했던 게이트웨이의 첫 이벤트가 샘플러를 멈추지 않게 하는 장치다.
- **(5)** `cur`은 **b가 더 클 때만** 전진한다. 시각이 뒤로 간 이벤트로는 절대 후퇴하지 않는다.
  후퇴를 허용하면 이미 지운 미래 버킷을 다시 "살아 있는" 것으로 오해한다.
- **(6)** 창보다 오래된 타임스탬프는 **셀 자리가 없다** — 버리는 게 맞다. 조건을
  `b < cur - (NBUCKETS-1)`로 쓰면 `cur`이 작을 때 `uint64_t`가 감기므로, 덧셈 형태로 쓴다.

질의는 버킷을 **더하기만** 한다.

```c
    uint64_t nowb = now / (uint64_t)EVQ_BUCKET_US;
    uint64_t lo   = since_us / (uint64_t)EVQ_BUCKET_US;     /* 부분 버킷 전체 포함 */
    uint64_t window_lo = (nowb + 1 >= NB) ? nowb - (NB - 1) : 0;
    if (lo < window_lo) lo = window_lo;                     /* 창 밖 → 창 시작으로 clamp */
    ...
        uint64_t hi = g_cnt.cur < nowb ? g_cnt.cur : nowb;  /* 미래 버킷은 없다 */
        uint64_t ring_lo = (g_cnt.cur + 1 >= NB) ? g_cnt.cur - (NB - 1) : 0;
        if (lo < ring_lo) lo = ring_lo;                     /* 이미 0으로 재사용된 칸 */
        for (uint64_t b = lo; b <= hi; b++) { ... total += row[type]; }
```

**clamp 세 번이 이 함수의 전부다.** `lo`를 창 시작으로, `lo`를 링이 실제로 담고 있는 범위로,
`hi`를 `min(cur, nowb)`로. 하나라도 빠지면 루프가 수백만 번 돌거나 stale 카운터를 더한다.

## 5. 단계별로 만들어 보기

### v0 (틀림) — 청소 스레드가 버킷을 비운다

```c
static void *cleaner(void *a) {            /* 1초마다 깨어나 다음 칸을 비운다 */
    (void)a;
    for (;;) { usleep(BUCKET_US); pthread_mutex_lock(&L);
               cur = (cur + 1) % NB; memset(n[cur], 0, sizeof n[0]);
               pthread_mutex_unlock(&L); }
}
```

동작할 것 같지만 틀리고, 비싸다.
- 스레드가 스케줄되지 못하면(부하, 우선순위 역전) 버킷 정렬이 **시각과 어긋난다.** 시계가 두 개인 셈이다.
- 이벤트가 0개인 조용한 밤에도 8시간 동안 28,800번 깨어난다. 배터리·전력 예산이 있는 장치에서는 실격이다.
- 종료 처리와 잠금이 하나 더 늘어난다. **아무도 묻지 않는데 유지되는 상태**는 버그의 원천이다.

원칙: **아무도 보지 않는 동안 정확할 필요가 없다.** 다음 이벤트나 질의가 올 때 맞으면 된다.

### v1 (동작) — lazy advance

4절의 `cnt_add()`가 v1이다. 비용을 계산해 보면 청소 스레드보다 싸다는 게 분명하다.
전진하는 버킷 수는 **최대 NBUCKETS**로 잘리고(그 이상이면 memset 한 번), 초당 이벤트가 많을 때는
버킷이 거의 안 바뀌므로 지울 것도 없다. 스크래치 측정으로 **이벤트당 평균 0.199행**이었다(7절).

### v2 (더 좋음) — 절대 버킷 번호를 슬롯에 저장

04번 해답은 **지우는 일 자체를 없앤다.** 슬롯에 그 슬롯이 어느 버킷인지를 함께 적어 둔다.
`04_temp_single_flight/temp_cache_solution.c`의 선언이 핵심이다.

```c
typedef struct {
    uint64_t minute;   /* ABSOLUTE minute index this slot describes.        */
                       /* Index is minute % TEMP_NBUCKETS, so a slot from   */
                       /* an earlier hour is recognised by minute != m and  */
                       /* needs no separate eviction pass.                  */
    uint64_t first;    /* free-running raw index of its first sample        */
    uint32_t count;
    float    min, max;
    double   sum;      /* double: 3600 s of floats would lose bits in float */
} bucket_t;
```

기록할 때 슬롯 번호가 다르면 그 자리에서 초기화한다. 이것이 **곧** eviction이다.

```c
    uint64_t m = ts / TEMP_BUCKET_US;
    bucket_t *b = &h.b[m % TEMP_NBUCKETS];

    if (b->minute != m) {                    /* slot belongs to an older    */
        b->minute = m;                       /* hour (or was never used):   */
        b->first  = h.head;                  /* recycle it. That IS the     */
        b->count  = 0;                       /* eviction — no pass needed.  */
        b->sum    = 0.0;
        b->min = b->max = c;
    }
```

읽을 때도 `b->minute != m`이면 그냥 건너뛴다. 장점 세 가지.
- 건너뛴 버킷을 지우는 루프가 **없다** — 최악의 경우도 없고, memset 특례도 필요 없다.
- `cur`을 전진시키는 로직이 사라져 시계 역행에 강하다. 슬롯은 자기 신분을 스스로 증명한다.
- 대가는 슬롯마다 `uint64_t` 8 B. 60개면 480 B다. 거의 언제나 이쪽이 낫다.

### v2+ — 버킷에 min/max/sum/count를 넣어 범위 통계로

카운터 하나 대신 **부분 집계(partial aggregate)**를 넣으면 개수 질의가 통계 질의가 된다.
`min`, `max`, `sum`, `count`는 모두 **결합적(associative)**이라 버킷 결과를 그냥 합칠 수 있다.
평균은 저장하지 않는다 — 평균은 결합적이지 않다. `sum`과 `count`를 저장하고 마지막에 나눈다.

`float`이 아니라 `double sum`인 이유는 C4의 마지막 함정과 같다. 3600초 분량의 `float` 덧셈은
비트를 잃는다.

### 하이브리드 — 끝단 부분 버킷만 원시 샘플로 정확히

04번 지문은 임의의 `[t0, t1]`을 받는다. 버킷 경계와 안 맞는 부분이 **양 끝에 최대 2개** 생긴다.
해답은 원시 샘플 링을 따로 두고 그 두 개만 훑는다.

```c
        if (t0 <= bs && be <= t1) {
            /* whole minute inside the range: O(1), this is the whole point */
            ...
            n   += b->count;
            sum += b->sum;
            continue;
        }
        /* partial minute (only ever the first and/or last of the range):
         * walk this bucket's raw samples, bounded by one minute of data. */
```

비용은 **온전한 버킷 수(≤ 60) + 1분 분량의 원시 샘플 × 2**로 고정된다. 이벤트 개수와 무관하고
답은 정확하다. 04번 harness의 실측:

```
    full window: n=4000 min=15.002746 max=34.988514 avg=25.152767
      (brute force 15.002746/34.988514/25.152768), raw samples scanned=45
```

4000개 샘플의 통계를 내면서 원시 샘플은 **45개**만 만졌다. 이게 하이브리드의 값이다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| stale 슬롯을 0으로 안 지움 | 한 바퀴 돈 뒤 개수가 갑자기 과거 값만큼 커진다 | `b % NB`가 같은 슬롯을 재사용한다 | lazy advance로 건너뛴 칸 memset, 또는 슬롯에 절대 번호 저장 |
| `b - cur`이 클 때도 루프 | 조용한 밤 뒤 첫 이벤트에서 수백만 회 반복, 잠금 점유 | 절대 버킷 번호 차이는 무한히 커질 수 있다 | `b - cur >= NBUCKETS`면 전체 `memset` 한 번 |
| 질의에서 `lo`를 clamp 안 함 | `since_us = 0`이면 루프가 에포크부터 돈다(수천만 회) | 버킷 번호가 절대값이라 0은 1970년이다 | `lo`를 창 시작과 링 보유 범위로 두 번 clamp |
| `cur`을 과거로도 후퇴시킴 | 개수가 오르내리고 미래 칸의 쓰레기가 더해진다 | NTP·순서 뒤바뀜으로 오래된 ts가 온다 | `b > cur`일 때만 전진, 오래된 ts는 `(6)`처럼 버린다 |
| 벤더 `type`으로 바로 인덱싱 | 무작위 메모리 오염, 재현 안 되는 개수 이상 | `type`은 신뢰할 수 없는 입력이다 | `0 <= type < COUNT` 검사 후 `unknown++` |
| 버킷 반올림을 문서화 안 함 | 리뷰어가 "±1 s 틀리는데요"라고 지적 | 부분 버킷을 통째로 포함하므로 과다 계수된다 | 헤더에 "resolution is one bucket"을 명시(03번 방식) |
| 평균을 버킷에 저장 | 버킷을 합칠 때 평균의 평균이 되어 틀린다 | 평균은 결합적이지 않다 | `sum`과 `count`를 저장하고 질의 끝에서 나눈다 |
| `float sum` | 몇 시간 뒤 avg가 미세하게 어긋난다 | 크기 차 큰 값의 반복 가산은 비트를 잃는다 | 버킷 합은 `double`, 정수면 `int64_t` |
| 카운터를 `uint16_t`로 | 버스트에서 개수가 줄어든 것처럼 보인다 | 65535에서 감싸 돈다 | `uint32_t`, 또는 포화 증가로 상한 고정 |

## 7. 손으로 확인하기

### (a) 03번 모범답안 — 버킷 만료가 실제로 도는 모드로

```sh
cd 03_event_tailer_shutdown
./main.sh fast sol        # 20 ms x 32 버킷 = 640 ms 창
```

```
   queue capacity 16, buckets 32 x 20000 us (window 640000 us)
  [PASS] since 0 (whole window)             type=-1 got 19 want 19
  [PASS] since now - 1 bucket               type=-1 got 0 want 0
  [PASS] since older than the window        type=-1 got 19 want 19
ALL CHECKS PASSED (0 failed)
```

세 줄이 각각 4절의 clamp 세 개를 검사한다. 창 전체 / 최근 한 버킷만 / 창보다 오래된 `since`.

### (b) 브루트포스와 맞대 보기 — 두 변형을 동시에

`bucket_check.c`(스크래치)는 같은 입력을 세 구현에 먹인다. A = lazy advance(03번 방식),
B = 절대 버킷 번호(04번 방식), 그리고 전수 스캔 브루트포스. 실제 출력:

```
bucket width 100 ms, 8 buckets (800 ms window)

t=  10 ms type=0 -> bucket 0 | count_since(0,ANY): A=1 B=1  exact=1
t=  20 ms type=0 -> bucket 0 | count_since(0,ANY): A=2 B=2  exact=2
t=  90 ms type=1 -> bucket 0 | count_since(0,ANY): A=3 B=3  exact=3
t= 150 ms type=0 -> bucket 1 | count_since(0,ANY): A=4 B=4  exact=4
t= 260 ms type=2 -> bucket 2 | count_since(0,ANY): A=5 B=5  exact=5
t= 270 ms type=0 -> bucket 2 | count_since(0,ANY): A=6 B=6  exact=6
t= 830 ms type=1 -> bucket 8 | count_since(0,ANY): A=4 B=4  exact=4
t= 840 ms type=1 -> bucket 8 | count_since(0,ANY): A=5 B=5  exact=5

200008 events, 951 queries
variant A (lazy advance)  mismatches vs brute force: 0
variant B (absolute idx)  mismatches vs brute force: 0
lazy advance work: 39892 rows zeroed, 410 full wipes  (0.199 rows per event)
bucket rounding vs exact: equal 812, over 139 (worst +7), under 0
memory: A = 96 B, B = 192 B, brute force list = 3200128 B for 200008 events
RESULT: PASS
```

읽는 법.
- `t=830 ms` 줄에서 개수가 6 → 4로 **줄었다**. 버킷 8이 생기면서 창은 버킷 1~8이 되고,
  버킷 0에 있던 3개가 창을 벗어난다. 아무 만료 코드도 부르지 않았는데 사라진다 — 링 재사용이 곧 만료다.
- **0.199 rows per event** — lazy advance가 이벤트당 지우는 행이 평균 0.2개다.
  청소 스레드라면 이벤트가 0개인 시간에도 버킷 폭마다 한 번씩 깨어났을 것이다.
- **over 139 (worst +7)** — 버킷 반올림으로 951번 중 139번이 과다 계수됐고, 최악은 +7개였다.
  **틀린 것이 아니라 정의된 동작이다.** 그래서 지문과 헤더에 반올림 규칙을 적는다.
- **메모리 96 B vs 3.2 MB** — 20만 이벤트에서 33,000배 차이다.

## 8. 자가 점검

```check
Q: 버킷의 메모리는 무엇에 비례하는가? 이벤트가 초당 1개일 때와 10,000개일 때 메모리 차이는?
A: 버킷 수 × 타입 수 × 카운터 크기에만 비례한다. 이벤트율과 완전히 무관하므로 두 경우의 메모리는 똑같다. 03번 기본값(300 × 5 × 4 B)이면 6 KB다. 대가는 해상도 — 답이 한 버킷 단위로 반올림된다.

Q: lazy advance가 백그라운드 청소 스레드보다 나은 이유 세 가지.
A: (1) 시각과 버킷 정렬이 어긋날 수 없다 — 지우는 시점이 데이터의 타임스탬프로 결정된다. (2) 조용할 때 아무 일도 하지 않는다(전력, 깨어남 없음). (3) 스레드·잠금·종료 처리가 하나 줄어든다. 원칙은 "아무도 보지 않는 동안 정확할 필요가 없다"다.

Q: 밤새 조용했던 게이트웨이에 첫 이벤트가 왔다. 왜 memset 특례가 필요한가?
A: 버킷 번호 차이 b - cur이 (8시간 / 버킷 폭), 1초 버킷이면 28,800이 된다. 건너뛴 칸을 하나씩 지우면 잠금을 잡은 채 수만 번 반복한다. 그런데 NBUCKETS개보다 많이 건너뛰었다면 살아남을 칸이 하나도 없으므로 전체 memset 한 번이 정답이고 상한도 고정된다.

Q: 슬롯에 절대 버킷 번호를 저장하면 무엇이 사라지는가?
A: eviction 패스 전체가 사라진다. 기록·조회 때 slot->minute != m이면 그 슬롯은 과거 시대의 것이므로 재초기화하거나 건너뛴다. 건너뛴 칸을 지우는 루프, memset 특례, cur 전진 로직이 모두 필요 없다. 대가는 슬롯마다 8바이트다.

Q: since_us가 버킷 경계 중간을 가리키면 답은 어떻게 되는가? 왜 그게 허용되는가?
A: since가 든 버킷을 통째로 포함하므로 실제보다 많이 센다(측정상 최악 +7개). 부분 버킷 안에서 이벤트의 정확한 시각을 버렸기 때문에 정밀하게 셀 방법이 없다. 그래서 03번 헤더는 "Resolution is one bucket: the answer includes the whole bucket that contains since_us"라고 계약으로 적는다. 명시되지 않은 반올림은 버그이고, 명시된 반올림은 설계다.

Q: 버킷에 평균을 저장하면 왜 틀리는가?
A: 버킷을 합칠 때 평균의 평균이 되고, 버킷마다 표본 수가 다르면 가중치가 틀어진다. min/max/sum/count는 결합적이라 버킷 결과를 그대로 합칠 수 있으므로 이것들을 저장하고, 평균은 질의 끝에서 sum/count로 계산한다.

Q: 정확한 개수가 필요해서 버킷을 쓰면 안 되는 경우는?
A: (1) 개수뿐 아니라 이벤트 내용(누가, 어느 문)이 필요할 때 — 세는 구조가 아니라 저장 구조가 필요하다. (2) 이벤트율이 낮아 창에 수십 개뿐일 때 — 리스트 스캔이 더 싸고 정확하며 코드도 짧다. (3) 임의의 [t0,t1]에 정확한 답이 요구될 때 — 04번처럼 버킷 + 끝단 원시 샘플 하이브리드로 가거나 전부 보관한다.
```

## 9. 요약 카드

| 항목 | 답 |
| --- | --- |
| 문제 신호 | "최근 N분 동안 몇 개", "O(buckets), never O(events)", 버스트가 있는 입력 |
| 자료구조 | `counter[NBUCKETS][NTYPES]` + `cur`(마지막 버킷) 또는 슬롯마다 절대 버킷 번호 |
| 버킷 번호 | `b = ts / BUCKET_US` (절대). 저장 위치는 `b % NBUCKETS` |
| 삽입 | 타입 검증 → lazy advance(건너뛴 칸만 0, 한 바퀴 이상이면 memset) → `n[b%NB][type]++` |
| 질의 | `lo`를 창 시작·링 범위로 clamp, `hi = min(cur, nowb)`, 버킷을 더한다 (≤ NBUCKETS회) |
| 메모리 | 버킷 수 × 타입 수 × 4 B. **이벤트율과 무관**. 대가는 한 버킷만큼의 반올림 |
| 확장 | 버킷에 min/max/sum/count(결합적인 것만). 평균은 저장 금지, 마지막에 나눈다 |
| 정확한 끝단 | 온전한 버킷은 O(1)로 접고, 양 끝 부분 버킷만 원시 샘플로 훑는다(04번: 4000개 중 45개) |
| 쓰지 말 곳 | 내용이 필요할 때, 이벤트가 원래 적을 때, 반올림이 허용되지 않을 때 |

한 줄 암기: **"셀 것만 세라. 칸은 다음 이벤트가 청소한다 — 아니면 칸이 자기 번호를 들고 있게 하라."**

이전: [C4. 슬라이딩 윈도우와 단조 덱](C4_sliding_window_and_monotonic_deque.md)
