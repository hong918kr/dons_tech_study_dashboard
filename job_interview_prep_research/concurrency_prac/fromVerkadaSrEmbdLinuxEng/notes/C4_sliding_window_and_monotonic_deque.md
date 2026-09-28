# C4. 슬라이딩 윈도우 최솟값·최댓값 — 매번 다시 훑지 않는 법

> **이 노트를 읽고 나면**
> - "최근 60초 최소 RSSI"를 질의당 평균 O(1)로 답하는 코드를 백지에서 쓸 수 있다.
> - 왜 누적합(prefix sum)으로는 min/max를 못 하는지 한 문장으로 설명할 수 있다.
> - 단조 덱(monotonic deque)을 고정 크기 배열 + head/tail로 구현하고, 상각 O(1)을 증명할 수 있다.
>
> **선행**: [C1. 링버퍼](C1_ring_buffer.md), 그리고 [C3](C3_prefix_sum_and_range_query.md)(누적합/prefix sum) — "합은 되돌릴 수 있다"는 감각이 먼저 있어야 이 노트의 대비가 보인다.
>
> **이 개념을 쓰는 문제**
> - [02_modem_rssi_window](../02_modem_rssi_window/question_note) **Part 2** — `rssi_min_in_window()` / `rssi_max_in_window()` / `rssi_avg_in_window()`. 이 노트의 주 무대다.
> - [08_battery_energy_pipeline](../08_battery_energy_pipeline/question_note) Part 2 — 합·적분은 누적합, 첨두(peak) 전력은 이 노트의 max 덱.
> - 같은 동기 다른 도구: [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note)(시간 창 + 이진 탐색), [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note)("가장 임박한 것" = heap).

---

## 1. 왜 이게 필요한가

02번 문제의 Part 2 요구사항은 딱 한 줄이다.

> `rssi_min_in_window()` — 최근 `RSSI_WINDOW_US` 안의 최소 RSSI. **Amortised O(1), NOT O(window).**

지문이 대문자로 못을 박아 뒀다. 즉 "샘플을 다 들고 있다가 질의 때 훑는" 답은 **처음부터 배점이 없다.**
숫자를 넣어 보면 왜 그렇게 썼는지 보인다.

- 모뎀은 최악의 경우 **10 ms마다** 값을 하나 준다 → **100 Hz**.
- 제품 스펙의 창은 **60초** → 창 안에 항상 **100 × 60 = 6000개**의 샘플이 산다.
- 설치 기사 UI는 min/max/avg를 **초당 몇 번** 묻는다. 초당 4번이면 질의만 **초당 24,000번 비교**.
- 게다가 이 스캔은 **샘플러가 필요한 바로 그 mutex 안에서** 돈다. 잠금 구간이 데이터 양에 비례해
  길어지므로, 훑는 동안 무선 태스크는 값을 기록할 수 없다 — 느린 것보다 이게 더 나쁘다.

`02_modem_rssi_window/rssi_window_solution.c`의 주석이 같은 계산을 해 둔다.

```
 *   min/max over a sliding window by scanning is O(n) per query. n is not small:
 *   the modem can produce a value every 10 ms, so a 60 s window is 6000 samples,
 *   [...] that is millions of comparisons per minute inside a lock
 *   that the radio task also needs.
```

**누적합은 왜 안 되나.** 합은 되돌릴 수 있다. `sum(a..b) = P[b] - P[a-1]`이 성립하는 이유는
덧셈에 역연산인 뺄셈이 있기 때문이다. 오래된 샘플이 빠질 때도 `sum -= old` 한 줄로 끝난다.
**최솟값에는 역연산이 없다.** `min(-95, -70) = -95`인데, 여기서 -95를 빼면 -70이 남는지
그 뒤에 숨어 있던 -88이 남는지는 **버린 값들을 알아야** 답할 수 있다. 그래서 min/max는
"창에서 빠질 때 복구 가능한 만큼의 후보를 남겨 두는" 구조가 필요하다. 그 후보 집합을 최소한으로
유지하는 것이 이 노트의 전부다.

평균은 다시 합의 세계다. `sum`/`count`만 굴리면 O(1)이고 RSSI는 정수라 **정확**하다(실수 샘플이면 누적 오차 — 6절).

## 2. 그림으로 먼저

새 값이 들어올 때 **덱의 뒤쪽에서 쓸모없어진 값들이 잘려 나간다.** 이 3컷이 이 노트의 핵심이다.

```svg
<svg viewBox="0 0 680 330" role="img" aria-label="단조 덱에 새 값 -75가 들어오는 3단계">
  <text class="lbl" x="8" y="18">① -75 가 도착하기 직전. min 덱은 front → back 으로 값이 커진다(단조).</text>
  <rect class="fill-soft" x="20" y="28" width="86" height="40" rx="6"/><text x="63" y="53" text-anchor="middle">-85</text>
  <rect class="box" x="112" y="28" width="86" height="40" rx="6"/><text x="155" y="53" text-anchor="middle">-60</text>
  <text class="lbl" x="63" y="84" text-anchor="middle">front = 지금의 답</text>
  <text class="lbl" x="155" y="84" text-anchor="middle">back</text>
  <rect class="dash" x="330" y="28" width="86" height="40" rx="6"/><text x="373" y="53" text-anchor="middle">-75</text>
  <text class="lbl" x="373" y="84" text-anchor="middle">새 샘플</text>
  <line class="accent" x1="326" y1="48" x2="212" y2="48"/><polygon class="accent" points="212,48 222,43 222,53"/>

  <text class="lbl" x="8" y="130">② back 이 -60. -60 &gt;= -75 이므로 -60 은 영원히 답이 될 수 없다 → 버린다.</text>
  <rect class="fill-soft" x="20" y="140" width="86" height="40" rx="6"/><text x="63" y="165" text-anchor="middle">-85</text>
  <rect class="dash" x="112" y="140" width="86" height="40" rx="6"/><text x="155" y="165" text-anchor="middle">-60</text>
  <line class="muted" x1="118" y1="176" x2="192" y2="144"/><line class="muted" x1="118" y1="144" x2="192" y2="176"/>
  <text class="lbl" x="215" y="165">back_index-- (덱에서 제거, 링버퍼의 샘플은 그대로 남는다)</text>

  <text class="lbl" x="8" y="232">③ 이제 back(-85) &lt; -75 이므로 단조성이 유지된다 → -75 를 뒤에 붙인다.</text>
  <rect class="fill-soft" x="20" y="242" width="86" height="40" rx="6"/><text x="63" y="267" text-anchor="middle">-85</text>
  <rect class="box" x="112" y="242" width="86" height="40" rx="6"/><text x="155" y="267" text-anchor="middle">-75</text>
  <text class="lbl" x="63" y="298" text-anchor="middle">front = -85 (답 그대로)</text>
  <text class="lbl" x="230" y="267">min 질의 = front 한 번 읽기. 비교 0회.</text>
</svg>
```

만료(expiry)는 **반대쪽 끝**에서 일어난다. 시간이 흘러 창을 벗어난 샘플은 front에서 빠진다.

```
창 = 최근 1.0 s,  현재 시각 now = 1.60 s
                      │←────────── 창 안 ──────────→│
샘플:  ts  0.20  0.40  0.60  0.80  1.00  1.20  1.40  1.60
      값   -70   -85   -60   -95   -75   -75  -110   -65
               ↑만료  │
          (now-ts=1.2 > 1.0)

min 덱(인덱스로 저장):  [ -110(i=6), -65(i=7) ]      → front = -110
max 덱:                 [  -60(i=2), -65(i=7) ]      → front = -60
러닝 합/개수:           sum = -480, n = 6            → avg = -80.00
```

두 개의 덱은 **같은 샘플 링을 가리키는 두 개의 인덱스 목록**이다. 샘플 자체는 한 부에만 존재한다.

```svg
<svg viewBox="0 0 680 190" role="img" aria-label="샘플 링 하나와 그것을 가리키는 min/max 덱">
  <text class="lbl" x="8" y="18">샘플 링 s[] — (ts, rssi) 쌍. 값은 여기 한 부만 존재한다.</text>
  <rect class="box" x="20" y="26" width="64" height="36" rx="5"/><text x="52" y="49" text-anchor="middle">i=2</text>
  <rect class="box" x="88" y="26" width="64" height="36" rx="5"/><text x="120" y="49" text-anchor="middle">…</text>
  <rect class="fill-soft" x="156" y="26" width="64" height="36" rx="5"/><text x="188" y="49" text-anchor="middle">i=6</text>
  <rect class="fill-soft" x="224" y="26" width="64" height="36" rx="5"/><text x="256" y="49" text-anchor="middle">i=7</text>
  <text class="lbl" x="300" y="49">tail → head 사이가 창 안의 샘플</text>
  <rect class="box" x="30" y="118" width="190" height="40" rx="8"/><text x="125" y="143" text-anchor="middle">min 덱 = [6, 7]</text>
  <rect class="box" x="300" y="118" width="190" height="40" rx="8"/><text x="395" y="143" text-anchor="middle">max 덱 = [2, 7]</text>
  <line class="accent" x1="125" y1="116" x2="185" y2="66"/><polygon class="accent" points="185,66 177,75 184,77"/>
  <line class="muted" x1="360" y1="116" x2="70" y2="66"/><polygon class="muted" points="70,66 79,69 76,76"/>
  <text class="lbl" x="510" y="143">덱은 인덱스만 담는다 → 시각 조회 가능</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 슬라이딩 윈도우 (sliding window)

"지금부터 과거 W 시간"처럼 **양 끝이 같이 움직이는 구간**이다. 인덱스 기반 창(최근 k개)과 달리
이 문제의 창은 **시간 기반**이라, 모뎀이 조용하면 새 샘플 없이도 창이 비어 간다. 그래서 02번
해답은 append뿐 아니라 **모든 getter에서도** 만료를 돌린다.

### 덱 (deque, double-ended queue)

앞과 뒤 **양쪽에서 넣고 뺄 수 있는** 큐다. 필요한 연산이 딱 네 개다: `push_back`, `pop_back`,
`pop_front`, `front`. 이 네 개면 되므로 연결 리스트가 필요 없고, **고정 크기 배열 + 두 개의 정수
인덱스(front, back)**로 끝난다. 면접에서 `malloc`을 쓰지 않아도 된다는 뜻이다.

### 단조(monotonic) 덱

덱 안의 값이 **한 방향으로만 정렬되어 있는** 상태를 유지하는 덱이다.
- **min 덱**: front → back 으로 값이 **비감소**(작은 게 앞). front가 창의 최솟값.
- **max 덱**: front → back 으로 값이 **비증가**(큰 게 앞). front가 창의 최댓값.

정렬을 "유지한다"는 것은 정렬 알고리즘을 돌린다는 뜻이 아니다. **단조성을 깨는 값을 애초에 넣지
않는다**(넣기 전에 뒤에서 걷어낸다). 이것이 O(log n)이 아니라 상각 O(1)인 이유다.

### "쓸모없다"의 정확한 정의 — 그리고 증명

> 새 샘플 `x`가 시각 `t`에 들어왔다. min 덱의 back에 샘플 `y`가 있고, `y.v >= x.v` 이며
> `y.ts <= t` 이다(덱은 항상 시간순이므로 뒤쪽이 더 최근이지만, 그래도 새 샘플보다는 오래됐다).
> 이때 `y`는 **영구히 삭제해도 안전하다.**

증명. 미래의 어떤 질의 시각 `q`를 잡는다. `y`가 그 질의에서 창 안에 있다면 `q - y.ts <= W`다.
`t >= y.ts`이므로 `q - t <= q - y.ts <= W`, 즉 **`x`도 반드시 창 안에 있다.** 그리고 `x.v <= y.v`다.
따라서 `y`가 창 안에 있는 모든 순간에 `x`도 창 안에 있고 `x`는 `y`보다 크지 않다 —
`y`가 단독으로 최솟값이 되는 질의는 존재할 수 없다. `y`를 지워도 어떤 질의의 답도 바뀌지 않는다. ∎

핵심은 **"더 크고 더 오래됐다"는 두 조건이 동시에 성립할 때만** 버린다는 것이다.
더 크지만 더 최근이면(정상적으로 뒤에 붙는 경우) 버릴 수 없다 — 앞의 작은 값이 만료된 뒤 그 값이
답이 될 차례가 오기 때문이다. 그게 바로 덱에 여러 개를 남겨 두는 이유다.

`rssi_window_solution.c`는 이 문단을 주석으로 갖고 있다.

```
 *   A new sample smaller than the back of the deque
 *   deletes it permanently — a larger, OLDER sample can never be the minimum
 *   again, because it expires no later than the new one. The front is therefore
 *   always the answer; expiry pops the front.
```

### 상각(amortised) O(1)

"한 번의 호출이 항상 O(1)"이 아니라 "**N번 호출의 총비용이 O(N)**"이라는 뜻이다.
어떤 push는 덱을 5000개 걷어낼 수도 있다. 하지만 걷어낸 5000개는 **각자 push될 때 이미 비용을 냈고,
다시는 걷어낼 수 없다.** 각 샘플은 평생 정확히 한 번 들어가고 최대 한 번 나간다. 총 연산 ≤ 2N.

면접에서 말할 문장: *"Each sample is pushed once and popped at most once, so total work over N
samples is O(N) — amortised O(1) per query. A single call can be O(n), but the appends already paid for it."*

### eviction (만료 제거)

창을 벗어난 샘플을 실제로 버리는 일. 두 곳에서 일어난다.
- **링 앞쪽(tail)**: `sum`을 줄이고 `count`를 줄인다.
- **덱 front**: 방금 버린 인덱스가 덱의 front와 같을 때만 뺀다. 덱의 인덱스는 전부 tail 이상이고
  증가 순이므로, **버려진 샘플은 front일 수밖에 없다.** 그래서 덱을 뒤질 필요가 없다.

## 4. 코드로 보기

가장 작은 동작 예제 — **min 덱 하나만**. 인덱스 대신 값을 저장하고 시각도 같이 저장하는 가장 단순한 형태다.

```c
#define CAP 1024u                 /* 2의 거듭제곱: & 로 나머지 계산 */
#define MASK (CAP - 1u)
#define WIN_US 1000000ull         /* 창 = 1 s */

typedef struct { uint64_t ts; int v; } item_t;

static item_t q[CAP];
static uint32_t f, b;             /* free-running: 개수 = b - f */

static void push_min(uint64_t ts, int v)
{
    while (f != b && q[(b - 1u) & MASK].v >= v)   /* (1) 뒤에서 걷어내기 */
        b--;
    q[b & MASK] = (item_t){ ts, v };              /* (2) 붙이기 */
    b++;
}

static void expire(uint64_t now)
{
    while (f != b && now - q[f & MASK].ts > WIN_US)  /* (3) 앞에서 만료 */
        f++;
}

static int get_min(uint64_t now)
{
    expire(now);
    return (f == b) ? -32768 : q[f & MASK].v;      /* (4) front 가 답 */
}
```

한 줄씩.

- **(1) `>= v`** — 등호가 중요하다. 같은 값이면 더 최근 것만 남기면 된다(더 오래 산다).
  `>` 로 쓰면 동일 값이 쌓여 메모리는 더 쓰지만 답은 같다. 틀리지는 않는다.
  **이 줄이 없으면?** 덱은 그냥 모든 샘플을 담은 큐가 되고, front는 최솟값이 아니라 가장 오래된 값이 된다.
- **(2)** `b & MASK`로 원형 배열에 쓴다. `f`, `b`는 되감지 않는 자유 증가 카운터라서 `b - f`가 늘 개수다.
  **`& MASK`가 없으면?** 배열 밖을 쓴다 — 조용히 BSS가 깨진다.
- **(3) `now - ts > WIN_US`** — `>` 이다. `>=` 로 쓰면 창 경계에 걸친 샘플을 버린다("last 60 s"는 경계 포함).
  **이 줄이 없으면?** 모뎀이 조용해진 뒤에도 10분 전 -110 dBm이 계속 최솟값으로 나온다 — 02번 harness의 `window drained` 검사가 이걸 잡는다.
- **(4) 빈 덱** — "없음" 센티넬을 반환해야 한다(02번은 `RSSI_NONE (-32768)`). 0을 반환하면
  0 dBm = 완벽한 신호라는 뜻이 되어 상위 로직을 속인다.

`f != b`가 "비어 있지 않다", `b - f == CAP`이 "꽉 찼다"다. 자유 증가 카운터를 쓰면
`head == tail`이 빈 상태와 꽉 찬 상태를 동시에 뜻하는 고전적 함정이 사라진다.

## 5. 단계별로 만들어 보기

### v0 (틀림) — 최솟값을 변수 하나로 들고 있기

```c
static int g_min = 32767;
static void add(int v) { if (v < g_min) g_min = v; }   /* 최솟값 갱신 */
static int get_min(void) { return g_min; }
```

들어올 때는 잘 된다. **창에서 빠질 때 복구가 불가능하다.** -110이 만료됐을 때 다음 최솟값이
무엇인지 `g_min` 하나로는 알 수 없다 — "min에는 역연산이 없다"가 코드로 드러난 형태다.
이 버전은 신호가 한 번 나빠지면 **영원히 나쁜 값을 보고한다.**

### v1 (아직 부족) — 값만 저장하는 단조 덱

단조 덱을 넣었지만 덱에 `int v`만 담았다. 단조성 제거는 잘 된다. 그런데 **만료를 할 수 없다.**
front의 값이 -110인 건 알지만 그게 **언제 들어온 값인지 모른다.** 창 기반 만료는 시각이 필요하다.

고치는 방법은 두 가지다.
- 4절처럼 `(ts, v)` 쌍을 덱에 담는다 — 읽기 쉽지만 덱이 샘플 크기만큼 커진다.
- **샘플 링의 인덱스**만 담는다(02번 해답). 시각·값 모두 `s[idx]`에서 꺼내므로 덱은 `uint32_t`
  배열 하나(32 KiB)로 끝난다. 평균용 `sum` 때문에 **어차피 링은 있어야 하니** 인덱스가 공짜다.

### v2 (02번 해답) — 링 + min/max 두 덱 + 러닝 합

`02_modem_rssi_window/rssi_window_solution.c`의 상태 선언이 그대로 설계도다.

```c
typedef struct { uint64_t ts; int rssi; } sample_t;

static struct {
    pthread_mutex_t lock;
    sample_t s[RSSI_CAP];
    uint32_t head, tail;          /* free-running; count = head - tail        */
    uint32_t minq[RSSI_CAP];      /* sample indices, rssi non-decreasing      */
    uint32_t min_f, min_b;
    uint32_t maxq[RSSI_CAP];      /* sample indices, rssi non-increasing      */
    uint32_t max_f, max_b;
    int64_t  sum;                 /* exact: rssi_dbm is an integer            */
    uint32_t n_overflow;
} g_win = { .lock = PTHREAD_MUTEX_INITIALIZER };
```

만료는 한 군데로 모은다. **링에서 하나 버리면 덱 front도 같이 손본다** — 이 함수가 그 계약이다.

```c
static void drop_oldest(void)
{
    uint32_t idx = g_win.tail;
    g_win.sum -= g_win.s[idx & CAP_MASK].rssi;
    g_win.tail++;
    /* deque indices are strictly increasing and all >= tail, so only the front
     * can be the sample we just dropped. */
    if (g_win.min_f != g_win.min_b && g_win.minq[g_win.min_f & CAP_MASK] == idx)
        g_win.min_f++;
    if (g_win.max_f != g_win.max_b && g_win.maxq[g_win.max_f & CAP_MASK] == idx)
        g_win.max_f++;
}
```

append 쪽은 **순서가 전부**다. 같은 파일의 `win_append()`를 순서대로 읽으면 이렇다.

```c
    /* gettimeofday can step backwards (NTP): clamp so the ring stays ts-sorted */
    if (win_count() > 0) {
        uint64_t last = g_win.s[(g_win.head - 1u) & CAP_MASK].ts;
        if (ts < last) ts = last;
    }
    evict_expired(now);                 /* 1. 시간 만료 먼저 */
    if (win_count() == RSSI_CAP) {      /* 2. 그래도 꽉 찼으면 용량 만료 */
        drop_oldest();
        g_win.n_overflow++;
    }
    uint32_t idx = g_win.head;          /* 3. 새 샘플을 링에 쓴다 */
    g_win.s[idx & CAP_MASK] = (sample_t){ .ts = ts, .rssi = rssi };
    g_win.head++;
    g_win.sum += rssi;
    /* 4. 단조성 제거 — min 덱 (max 덱은 비교 방향만 <= 로 뒤집은 거울상) */
    while (g_win.min_f != g_win.min_b &&
           g_win.s[g_win.minq[(g_win.min_b - 1u) & CAP_MASK] & CAP_MASK].rssi >= rssi)
        g_win.min_b--;
    g_win.minq[g_win.min_b & CAP_MASK] = idx;
    g_win.min_b++;
```

**왜 이 순서인가.** 만료(1)를 단조성 제거(4)보다 먼저 한다. 반대로 하면 이미 창을 벗어난 항목을
대상으로 `>=` 비교를 하게 되고, 그 항목이 front라면 만료 루프가 나중에 지울 것을 지금 `min_b--`로
지우게 된다 — 같은 항목을 두 번 제거하려다 `min_f > min_b`가 되어 `min_b - min_f`가 거대한
`uint32_t`로 감긴다. 답이 아니라 **인덱스 계산이 터지는** 종류의 버그다.

평균은 링의 `sum`과 `count`로 끝난다. getter 세 개 모두 **먼저 만료를 돌린다** — 새 샘플이 없어도
시간은 흐르기 때문이다.

```c
int rssi_min_in_window(void)
{
    uint64_t now = modem_now_us();
    pthread_mutex_lock(&g_win.lock);
    evict_expired(now);                      /* samples age out with no new input */
    int r = (g_win.min_f == g_win.min_b)
              ? RSSI_NONE
              : g_win.s[g_win.minq[g_win.min_f & CAP_MASK] & CAP_MASK].rssi;
    pthread_mutex_unlock(&g_win.lock);
    return r;
}
```

`modem_now_us()`를 **잠금 밖에서** 부르는 것도 의도된 것이다. 잠금 구간에 syscall을 넣지 않는다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 덱에 값만 저장 | 만료가 아예 안 된다. 모뎀이 죽은 뒤에도 옛 최솟값이 계속 나온다 | 값만으로는 그 샘플의 시각을 알 수 없다 | 인덱스(권장) 또는 `(ts, v)` 쌍을 저장 |
| 단조성 제거를 만료보다 먼저 | `min_f > min_b`, 개수가 40억으로 감김, 엉뚱한 인덱스 접근 | 이미 만료 대상인 항목을 back에서 지우고 front에서 또 지운다 | `evict → (필요시) drop_oldest → push → 단조성 제거` 순서 고정 |
| 빈 덱에서 `q[f]` 반환 | 창이 빈 직후 쓰레기 값 또는 0 dBm | `f == b`를 검사하지 않았다 | `RSSI_NONE` 같은 센티넬을 정의하고 먼저 검사 |
| getter에서 만료를 안 돌림 | append가 멈추면 통계가 얼어붙는다. 02번 `window drained` 실패 | 창은 시간으로 움직이는데 시간은 append 없이도 간다 | min/max/avg 모두 진입 시 `evict_expired(now)` |
| `head == tail`로 꽉 찬 상태 판정 | 8192번째 샘플에서 창이 통째로 비거나 덮어써진다 | 되감는 인덱스에서 빈 상태와 꽉 찬 상태가 구분되지 않는다 | 자유 증가 카운터 + `count = head - tail`, 접근 때만 `& MASK` |
| 시각이 뒤로 가는 것을 방치 | 만료 루프가 멈추지 않거나 최신 샘플을 즉시 버린다 | NTP가 `gettimeofday()`를 뒤로 돌린다. 링이 ts 정렬을 잃는다 | 직전 ts로 clamp(해답의 `if (ts < last) ts = last;`) |
| `sum`을 float/double로 굴림 | 몇 시간 뒤 평균이 미세하게 어긋난다 | 크기 차 큰 값의 가감산은 손실이 있고 누적된다 | 정수는 `int64_t`로 정확히. 실수 샘플이면 주기적 재계산 또는 Kahan 합 |
| 덱 배열을 링 크기보다 작게 | 버스트에서 덱이 배열을 넘어 쓴다 | 최악의 경우(단조 증가 입력) 모든 샘플이 덱에 남는다 | 덱 배열도 `RSSI_CAP`과 같은 크기로 |

## 7. 손으로 확인하기

### (a) 02번 모범답안 돌려 보기

```sh
cd 02_modem_rssi_window
./main.sh sol
```

harness는 창을 1.5 s로 줄여 빌드하므로 8초 안에 만료가 실제로 일어난다. 보게 될 줄들.

```
  window drained: min                          got -32768 want -32768             PASS
  window drained: avg                          got nan want nan                   PASS
  window refilled                              min -96                            PASS
  getters never block on the modem             mean 0.56 us, worst 102 us (modem blocks 100000) PASS
ALL CHECKS PASSED (0 failed)
```

`mean 0.56 us`가 상각 O(1)의 실측이다. 벤더 호출은 100 ms를 잡아먹는데 getter는 0.56 µs다.

### (b) 무차별 대입(brute force)과 맞대 보기

`deque_check.c`(스크래치)는 같은 입력을 덱과 전수 스캔에 먹여 답을 맞춰 본다. 세 단계 —
손으로 검산 가능한 고정 수열, 무작위 12만 샘플(조용한 구간 포함), 100 Hz 정속 구간.

```sh
cc -std=c11 -O2 -Wall -Wextra -o deque_check deque_check.c && ./deque_check
```

실제 출력:

```
t= 200 ms  push  -70  ->  min= -70 max= -70 avg= -70.00  n=1
t= 400 ms  push  -85  ->  min= -85 max= -70 avg= -77.50  n=2
t= 600 ms  push  -60  ->  min= -85 max= -60 avg= -71.67  n=3
t= 800 ms  push  -95  ->  min= -95 max= -60 avg= -77.50  n=4
t=1000 ms  push  -75  ->  min= -95 max= -60 avg= -77.00  n=5
t=1200 ms  push  -75  ->  min= -95 max= -60 avg= -76.67  n=6
t=1400 ms  push -110  ->  min=-110 max= -60 avg= -83.33  n=6
t=1600 ms  push  -65  ->  min=-110 max= -60 avg= -80.00  n=6

random phase: 120008 samples, 40078 checks, 0 mismatches
deque work: 120008 pushes, 120005 min-deque pops  (pops/push = 1.000  <= 1 means amortised O(1))

100 Hz phase: window holds 101 samples
  20000 queries: deque did 20000 pushes + 19992 pops total, brute force scanned 2014950 samples (50x)
RESULT: PASS
```

읽는 법 세 가지.
- `t=1400 ms` 줄에서 `n`이 6에 머문다 — 창이 1 s이므로 `ts=0.2 s` 샘플이 만료됐다(2절 ASCII 그림과 같은 상태).
- `pops/push = 1.000` — 12만 번 push에 제거도 12만 번. **원소당 1회 이하**, 상각 O(1)의 정의 그대로다.
- 100 Hz 구간에서 창은 101개를 담고 브루트포스는 같은 답에 **50배** 더 일했다. 창을 60 s로 늘리면
  창에 6000개가 살고 이 배율은 **3000배** 쪽으로 간다. `-DCAP=8u`(링 용량 초과 경로)도 0 mismatch.

## 8. 자가 점검

```check
Q: 최근 60초 합은 누적합으로 O(1)인데 최솟값은 왜 안 되는가?
A: 덧셈에는 역연산인 뺄셈이 있어서 창에서 빠지는 값을 그냥 뺄 수 있다. min에는 역연산이 없다. min(-95,-70)에서 -95를 "빼면" 무엇이 남는지는 버려진 후보들을 알아야만 답할 수 있다. 그래서 후보 집합을 유지해야 하고, 그 집합을 최소로 줄인 것이 단조 덱이다.

Q: min 덱에서 새 값보다 큰 값을 뒤에서 지워도 안전한 이유를 한 문단으로 말해 보라.
A: 지우려는 값 y는 새 값 x보다 크거나 같고, x보다 먼저 들어왔다. 미래의 어떤 질의에서 y가 창 안에 있다면 x는 y보다 나중에 들어왔으므로 반드시 창 안에 있다. 그리고 x <= y다. 즉 y가 최솟값이 되는 순간이 존재할 수 없다. 답을 바꾸지 않으므로 영구 삭제해도 된다.

Q: 상각 O(1)이라고 말할 때, 최악의 단일 호출은 몇인가? 그래도 왜 괜찮은가?
A: 최악의 단일 호출은 O(n) — 한 push가 덱을 거의 다 걷어낼 수 있다. 하지만 걷어낸 항목은 각자 push될 때 상수 비용을 지불했고 두 번 걷어낼 수 없다. N개 샘플의 총 작업이 2N 이하이므로 평균 O(1)이다. 경성 실시간 상한이 필요하면 이 점을 명시해야 한다.

Q: 덱에 값을 담는 것과 샘플 링의 인덱스를 담는 것의 차이는?
A: 값만 담으면 시각을 모르므로 시간 기반 만료를 할 수 없다. (ts, v) 쌍을 담으면 동작하지만 덱이 커진다. 인덱스를 담으면 시각과 값을 링에서 꺼낼 수 있고 덱은 uint32_t 배열로 끝난다. 평균을 위해 링과 running sum이 어차피 필요하므로 02번 해답은 인덱스를 고른다.

Q: append 안에서 만료 제거와 단조성 제거의 순서가 왜 중요한가?
A: 만료를 먼저 해야 한다. 순서를 바꾸면 이미 창을 벗어난 항목을 back에서 지우고, 이어지는 만료 루프가 같은 항목을 front에서 또 지우려 한다. front가 back을 지나쳐 min_f > min_b가 되고, 개수 계산(min_b - min_f)이 uint32_t로 감겨 엉뚱한 인덱스를 읽는다.

Q: 창이 비었을 때 min은 무엇을 반환해야 하는가? 0은 왜 안 되는가?
A: "값 없음"을 나타내는 센티넬이어야 한다. 02번은 RSSI_NONE (-32768)을 헤더에 정의한다. 0 dBm은 실제로는 존재하지 않지만 형식적으로는 "완벽한 신호"라는 뜻이므로, 상위 로직(페일오버 판단, LED 표시)이 죽은 모뎀을 최상급 신호로 오해한다.
```

## 9. 요약 카드

| 항목 | 답 |
| --- | --- |
| 문제 신호 | 지문에 "sliding window min/max", "amortised O(1), not O(window)" |
| 자료구조 | 샘플 링(ts, v) + min 덱 + max 덱 (둘 다 인덱스) + `int64_t sum` |
| 덱 구현 | 고정 크기 2의 거듭제곱 배열 + free-running `f`, `b`. 개수 = `b - f`, 접근은 `& MASK` |
| 불변식 | min 덱: front→back 값 비감소. 모든 인덱스는 `tail` 이상이고 증가 순 |
| append 순서 | ts clamp → 시간 만료 → 용량 만료 → 링에 쓰기 → `sum +=` → 두 덱 back 정리 |
| 질의 | 잠금 밖에서 `now` → 잠금 → 만료 → front 읽기(또는 센티넬) → 잠금 해제 |
| 복잡도 | append/질의 상각 O(1) — 원소당 push 1회, pop 최대 1회. 빠뜨리기 쉬운 것: getter에서도 만료, 빈 덱 센티넬, 시계 역행 clamp |

한 줄 암기: **"큰데 더 오래된 값은 이미 죽었다 — 뒤에서 잘라낸다. 앞은 시간이 잘라낸다."**

다음: 셀 수 있는 것은 덱도 필요 없다 → [C5. 시간 버킷](C5_time_buckets_and_histogram.md)
