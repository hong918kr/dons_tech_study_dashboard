# L4 · GPS PPS Time Discipline + LiDAR/Camera Matching

> **ID**: L4 · **Priority**: P1 · **Pattern**: counter unwrap + interval classification + two-pointer nearest match · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh L4` / `./run.sh L4 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: 센서 박스의 free-running 32-bit counter를 GPS **PPS**(1초 펄스)로 보정해서, 카메라·LiDAR timestamp를 **GPS 시간(ns)** 으로 바꾸고 카메라 frame과 LiDAR sweep을 짝지어 주는 코드를 짭니다.

**상황**
- 센서 박스(MPSoC + FPGA)에 counter가 하나 있습니다. nominal 100 MHz, 32-bit라서 약 **42.9 s마다 wrap**합니다. 실제 oscillator는 nominal과 조금 다릅니다(예: +25 ppm).
- GPS 수신기가 매초 **PPS 상승 edge**를 내면 FPGA가 그 순간의 counter 값을 **latch**해서 FIFO에 넣습니다. 그 배열이 `latch[]`입니다.
- 카메라 frame과 LiDAR packet도 **같은 counter**로 timestamp가 찍힙니다.
- 실제 PPS 배선에서는 이런 일이 생깁니다.
  - **missed**: 펄스가 빠짐 (GPS 신호 약함, 케이블). 다음 edge까지 2 s, 3 s …
  - **glitch**: 1초도 안 돼서 edge가 하나 더 들어옴 (ringing, EMI)
  - **bad**: 1초 단위에 맞지 않는 늦은 edge
- **ppm / ppb**: 주파수 오차 비율. 100 MHz에서 1 ppm = 초당 100 tick, 25 ppm = 초당 2,500 tick.
- **holdover**: 마지막 PPS 이후 너무 오래 PPS 없이 oscillator만 믿고 시간을 계산하는 상태.

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `pps_analyze(latch, n, nominal_hz, tol_ppm, r)` | PPS latch 배열 분석 | counter **unwrap**. latch마다 **마지막 정상 edge**에서 잰 간격 `d`로 OK / MISSED / GLITCH / BAD 분류. 정상 edge들로 `r->model`을 채우고, 실제 oscillator 주파수(Hz)와 오차(ppb) 계산. 성공 0, 잘못된 인자 `-EINVAL`, 정상 edge가 2개 미만이면 `-ENODATA` |
| `counter_to_gps_ns(m, ts, &holdover)` | counter stamp → 첫 PPS 기준 ns | ts 직전(또는 같은)의 정상 PPS를 찾아 측정 주파수로 환산. PPS와 ts 사이의 **wrap 처리**. 그 PPS에서 2 s(`PPS_HOLDOVER_S`)를 **넘으면** 변환은 하되 `holdover = true`. 첫 PPS보다 이전이거나 model이 비정상이면 `PPS_TS_INVALID` |
| `match_nearest(cam, n_cam, lidar, n_lidar, tol, pairs, cap, ms)` | 카메라 frame ↔ LiDAR sweep 짝짓기 | 카메라를 시간순으로, 아직 안 쓴 가장 가까운 sweep과 `|dt| <= tol`이면 짝. sweep 하나는 **한 번만** 사용. `cap`개까지 기록하고 **전체 짝 개수** 반환, 짝 없는 개수는 `MatchStats`에 |

**분류 규칙** (`w = nominal × tol_ppm / 1e6`, 1초당 허용 오차 tick)
- `d < nominal − w` → **GLITCH**: 그 edge는 버림
- `k = round(d / nominal)`, `|d − k × nominal| <= k × w` → `k == 1`이면 **OK**, `k >= 2`이면 **MISSED** (빠진 펄스 `k − 1`개)
- 그 밖 → **BAD**: 그 edge도 버림
- `latch[0]`은 기준 edge(REF, GPS 초 offset 0)로 믿습니다.
- 주파수 = 첫 정상 edge ~ 마지막 정상 edge 구간의 **총 tick / 총 초** (반올림). 오차 ppb는 0 쪽으로 버림.

**예시를 말로 풀면**
- counter가 4,000,000,000에서 시작, oscillator +25 ppm(초당 100,002,500 tick), tolerance 100 ppm(초당 ±10,000 tick).
- 0 ~ 10 s 동안 PPS를 받는데 **5 s 펄스가 빠졌고**, **7.25 s에 glitch**가 하나 끼었고, counter가 **2.95 s에 wrap**합니다. latch는 11개.
  - 결과: OK 8, missed 1, glitch 1, bad 0. 정상 edge 10개가 10초를 덮으니 주파수 **100,002,500 Hz**, 오차 **+25,000 ppb**.
- 이 model로 변환하면
  - 3 s PPS에서 50,001,250 tick 뒤의 stamp → **3,500,000,000 ns**
  - raw 값 **3,040,154**(wrap 직후라 숫자가 아주 작음) → **2,980,000,000 ns**
  - 빠진 5 s 근처(4 s PPS에서 1.5 s 뒤) → 5,500,000,000 ns, holdover 아님
  - 마지막 PPS(10 s)에서 2.5 s 뒤 → 12,500,000,000 ns, **holdover = true**
- 카메라 9장과 LiDAR 9 sweep(둘 다 10 Hz, tol 5 ms)을 맞추면 **짝 7개**, 짝 없는 카메라 2, 짝 없는 sweep 2, 최대 |dt| 1.6 ms.

**지켜야 할 조건**
- 정수 연산만 (`int64_t` ns), **float 금지**, `malloc` 금지.
- latch는 최대 `PPS_MAX = 64`개. 연속한 두 latch 사이는 2^32 tick(약 42.9 s)보다 짧습니다.
- 변환할 stamp는 마지막 정상 PPS에서 ±2^31 tick(약 21.4 s) 안에 있습니다.
- `cam_ns`, `lidar_ns`는 오름차순. 배열이 비어 있을 수 있습니다(`n == 0`이면 포인터가 `NULL`이어도 됨).

## 1. Problem

A vehicle sensor box (MPSoC + FPGA) has one free-running **32-bit counter**, nominally **100 MHz**, so it wraps every ~42.9 s. On every **GPS PPS** rising edge the FPGA latches the counter into a FIFO (`latch[]`). Every camera frame and every LiDAR packet is stamped with the same counter. Write the code that disciplines the counter to GPS time and pairs camera frames with LiDAR sweeps. The real oscillator is off by tens of ppm, and the PPS line can miss pulses, glitch, or deliver a bad edge.

```c
#define PPS_MAX         64
#define PPS_HOLDOVER_S  2             /* > 2 s since the PPS used -> holdover */
#define PPS_TS_INVALID  INT64_MIN
enum { PPS_REF = 0, PPS_OK, PPS_MISSED, PPS_GLITCH, PPS_BAD };

typedef struct {
    int64_t  tick[PPS_MAX];   /* unwrapped counter at each accepted edge (tick[0] = latch[0]) */
    int64_t  sec[PPS_MAX];    /* GPS seconds since the reference edge: 0, 1, 2, 4, ... */
    uint32_t n;               /* accepted edges */
    uint32_t last_raw;        /* raw latch of the last accepted edge */
    uint32_t freq_hz;         /* measured oscillator frequency */
} PpsModel;

typedef struct {
    uint32_t ok, missed, glitch, bad;   /* missed = number of missing pulses (sum of k - 1) */
    uint8_t  cls[PPS_MAX];              /* class of each latch (PPS_REF for latch[0]) */
    uint32_t freq_hz;
    int32_t  err_ppb;                   /* + = oscillator runs fast */
    PpsModel model;
} PpsReport;

typedef struct { uint32_t cam, lidar; int64_t dt_ns; } Pair;   /* dt = lidar - cam */
typedef struct { uint32_t pairs, cam_unmatched, lidar_unmatched; int64_t max_abs_dt_ns; } MatchStats;

int     pps_analyze(const uint32_t *latch, size_t n, uint32_t nominal_hz, uint32_t tol_ppm,
                    PpsReport *r);                          /* 0, -EINVAL, -ENODATA */
int64_t counter_to_gps_ns(const PpsModel *m, uint32_t ts, bool *holdover);
int     match_nearest(const int64_t *cam_ns, size_t n_cam, const int64_t *lidar_ns, size_t n_lidar,
                      int64_t tol_ns, Pair *pairs, size_t cap, MatchStats *ms);   /* total pairs */
```

**`pps_analyze`**. Unwrap the latches into 64-bit ticks. Measure each latch from the **last good edge** (`d` ticks). With `w = nominal_hz × tol_ppm / 1e6` (allowed error per second):

- `d < nominal − w` → **GLITCH**, drop the edge
- `k = round(d / nominal)`; if `|d − k × nominal| <= k × w` → **OK** when `k == 1`, **MISSED** when `k >= 2` (`k − 1` pulses missing)
- otherwise → **BAD**, drop the edge

`latch[0]` is trusted as the reference edge (GPS second offset 0). Accepted edges (REF / OK / MISSED) go into `r->model` with their unwrapped tick and GPS second offset. `freq_hz = round(span ticks / span seconds)` over the first..last accepted edge; `err_ppb = (ticks − secs × nominal) × 1e9 / (secs × nominal)`, truncated toward zero. Bad arguments (`NULL`, `n < 2`, `n > PPS_MAX`, `nominal_hz` not in `[1, 1e9]`, `tol_ppm` not in `[1, 1000]`) → `-EINVAL`. Fewer than 2 accepted edges → `-ENODATA`.

**`counter_to_gps_ns`**. Return ns since the reference edge (the caller knows from NMEA / ToD which GPS second that was). Unwrap `ts` against the last accepted edge, pick the **latest accepted edge at or before it**, and add `elapsed × 1e9 / freq_hz` (rounded). Set `*holdover` when `elapsed` is **more than** `PPS_HOLDOVER_S` seconds. `ts` before the reference edge, or an empty model → `PPS_TS_INVALID`.

**`match_nearest`**. Both arrays sorted ascending. Walk cameras in time order; each takes the **nearest still-unused** LiDAR sweep if `|dt| <= tol_ns` (tie → earlier sweep). Each sweep is used at most once. Two pointers, `O(n_cam + n_lidar)`. Write up to `cap` pairs, **return the total** number of pairs, and fill `MatchStats`.

### Example

```
nominal 100 MHz, tol 100 ppm (w = 10,000 ticks), oscillator +25 ppm = 100,002,500 ticks/s
counter starts at 4,000,000,000 and wraps at 2.95 s; PPS at 0..10 s, second 5 missing,
extra edge at 7.25 s -> 11 latches:
  4,000,000,000  4,100,002,500  4,200,005,000  5,040,204  105,042,704  305,047,704
  405,050,204    430,050,829    505,052,704    605,055,204  705,057,704

pps_analyze -> 0: ok 8, missed 1, glitch 1, bad 0, accepted 10 edges
               freq_hz 100,002,500, err_ppb 25,000

counter_to_gps_ns:
  55,041,454  (3 s PPS + 50,001,250)  -> 3,500,000,000 ns, holdover false
  3,040,154   (just after the wrap)   -> 2,980,000,000 ns, holdover false
  255,046,454 (4 s PPS + 1.5 s)       -> 5,500,000,000 ns, holdover false
  955,063,954 (10 s PPS + 2.5 s)      -> 12,500,000,000 ns, holdover TRUE
  3,999,999,000 (before latch[0])     -> PPS_TS_INVALID

match_nearest, tol 5 ms (values in ms):
  cam   0  100  200.3  300  400  600  699.6  800  900
  lidar 1.2  101.2  301.2  409.0  501.2  601.2  701.2  801.2  901.2
  -> 7 pairs, cam_unmatched 2 (200.3, 400), lidar_unmatched 2 (409.0, 501.2), max |dt| 1.6 ms
```

### Constraints

- Integer math only (`int64_t` ns), no floating point, no malloc.
- At most `PPS_MAX = 64` latches; consecutive latches are less than 2^32 ticks (~42.9 s) apart.
- A stamp passed to `counter_to_gps_ns` is within ±2^31 ticks (~21.4 s) of the last accepted edge.
- `cam_ns` / `lidar_ns` sorted ascending; either may be empty (`NULL` allowed when its count is 0).

### Clarifying questions to ask (first 3 minutes)

1. Is the PPS latched in hardware (timer input capture in the FPGA), or by an ISR that reads the counter? (decides the jitter floor)
2. How do we know which GPS second a pulse is — NMEA `RMC`/`ZDA` or a UBX time message, and does it describe the pulse just past or the next one?
3. What tolerance does the oscillator spec allow (XO ±50 ppm, TCXO ±2 ppm)? Should the frequency be one number for the window, or tracked over time?
4. What should a dropped edge (glitch or bad) do to the baseline — skip it, or re-sync to it?
5. How long may we run without PPS before the data must be flagged (holdover limit)?
6. For matching: what does each stamp mean — camera exposure start / mid-exposure, LiDAR start of sweep or a per-packet time? And what is the tolerance?

## 2. Your workspace

`./run.sh L4`로 바로 테스트하세요. 테스트 8묶음(CHECK 64개)이 있고, `at(base, f, s, extra)`가 "GPS `s`초 + `extra` tick" 시점의 raw latch 값을 만들어 줍니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — unwrap은 연속 latch의 uint32 차이를 누적</summary>

`t += (uint32_t)(latch[i] - latch[i-1])`로 64-bit 값 `t`를 만듭니다. 4,200,005,000 → 5,040,204도 차이는 100,002,500으로 나옵니다. 단, **간격 `d`는 직전 raw latch가 아니라 마지막 정상 edge부터** 잽니다. 그래야 glitch를 버려도 다음 edge가 정상 1초로 보입니다.

</details>

<details>
<summary>Hint 2 — 몇 초짜리 간격인지 먼저 반올림</summary>

`k = (d + nominal/2) / nominal`. 허용 오차도 `k`배로 늘립니다(`k × w`). `d < nominal − w`는 1초도 안 된 edge라 glitch입니다. MISSED도 GPS 초가 `k`만큼 정확히 지났다는 뜻이라 **주파수 계산에 써도 됩니다** (`sec[]`에 `k`를 더함).

</details>

<details>
<summary>Hint 3 — 변환은 "마지막 PPS 기준 signed 32-bit 거리"로 unwrap</summary>

`tu = tick[n-1] + (int32_t)(ts - last_raw)`. 그다음 `tick[j] <= tu`인 가장 뒤쪽 `j`를 찾고 `elapsed = tu - tick[j]`, `ns = sec[j] × 1e9 + (elapsed × 1e9 + f/2) / f`. 매칭은 `while (lidar[j+1] <= cam) j++`로 커서를 밀고 후보 `j`, `j+1` 두 개만 봅니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — PPS는 "정확한 1초 눈금", counter는 "자"

<div class="chart" data-src="chart1.svg" data-caption="예제: 위는 raw counter(2.95 s에 2³² → 0 wrap), 아래는 PPS latch 분류. 5 s 펄스가 없어 6 s edge가 MISSED(k = 2), 7.25 s edge는 마지막 정상 edge(7 s)에서 0.25 s라 GLITCH로 버리고 8 s는 7 s부터 잼"></div>

PPS edge 사이의 GPS 시간은 **정확히 정수 초**입니다(수신기 PPS 오차는 수십 ns). 반대로 counter는 oscillator 오차만큼 늘었다 줄었다 하는 자입니다. 그래서

1. PPS 사이의 tick 수 = 실제 oscillator 주파수 (이 예제는 100,002,500 Hz, 즉 +25 ppm)
2. 아무 stamp나 "직전 PPS의 GPS 초 + (그 PPS 이후 tick ÷ 측정 주파수)"로 GPS 시간이 됩니다.

나쁜 edge를 걸러야 1이 망가지지 않습니다. 기준을 **마지막 정상 edge**로 고정하면 glitch / bad가 몇 개 끼어도 다음 정상 edge가 제자리를 찾습니다.

### 4.2 변환과 wrap, holdover

<div class="chart" data-src="chart2.svg" data-caption="위: raw 3,040,154는 wrap 직후라 모든 latch보다 작음. 마지막 PPS 기준 signed 32-bit 거리로 unwrap하면 PPS 2 뒤 98,002,450 tick = 2.98 s. 아래: 마지막 PPS(10 s)에서 2 s까지는 정상, 넘으면 holdover"></div>

- raw 값끼리 `<` 비교로 "ts 직전 PPS"를 찾으면 wrap 뒤 stamp를 첫 PPS보다 이전이라고 판단합니다. 그래서 먼저 64-bit로 **unwrap**하고 그다음에 비교합니다.
- `(int32_t)(ts − last_raw)`는 ts가 마지막 PPS 앞뒤 ±21.4 s 안이면 항상 맞습니다. 실시간 시스템에서는 model이 매초 갱신되니 충분합니다.
- holdover는 "틀렸다"가 아니라 "**믿을 수 있는 정도가 떨어졌다**"는 표시입니다. 변환은 계속하고 데이터에 flag를 붙입니다.

### 4.3 카메라 ↔ LiDAR 매칭

<div class="chart" data-src="chart3.svg" data-caption="tol 5 ms. 200.3 ms 카메라는 sweep 201.2가 빠져서 짝 없음, 400 ms는 가장 가까운 sweep이 9.0 ms 떨어져 짝 없음. 501.2 sweep은 카메라 500이 빠져서 남음. 600 ms 카메라는 501.2를 건너뛰고 601.2와 짝"></div>

카메라를 시간순으로 보면서 LiDAR 커서 `j`를 앞으로만 밉니다. `lidar[j+1] <= cam`이면 `lidar[j]`는 지금 카메라에도, 이후 카메라에도 `lidar[j+1]`보다 멀어서 다시는 이길 수 없으니 건너뜁니다. 그러면 후보는 `j`(cam 이하 마지막)와 `j+1`(cam 초과 첫 번째) 둘뿐입니다. 짝을 지으면 `j = best + 1`로 넘겨서 같은 sweep을 두 번 쓰지 않습니다.

### 4.4 손으로 따라가기 (trace)

`pps_analyze` — 예제 (nominal 100,000,000, `w` = 10,000):

| i | raw latch | unwrapped tick | d (마지막 정상 edge부터) | k | \|d − k·nom\| | 허용 k·w | 판정 | sec |
|---|---|---|---|---|---|---|---|---|
| 0 | 4,000,000,000 | 4,000,000,000 | – | – | – | – | REF | 0 |
| 1 | 4,100,002,500 | 4,100,002,500 | 100,002,500 | 1 | 2,500 | 10,000 | OK | 1 |
| 2 | 4,200,005,000 | 4,200,005,000 | 100,002,500 | 1 | 2,500 | 10,000 | OK | 2 |
| 3 | 5,040,204 | 4,300,007,500 | 100,002,500 | 1 | 2,500 | 10,000 | OK (wrap) | 3 |
| 4 | 105,042,704 | 4,400,010,000 | 100,002,500 | 1 | 2,500 | 10,000 | OK | 4 |
| 5 | 305,047,704 | 4,600,015,000 | 200,005,000 | 2 | 5,000 | 20,000 | **MISSED** (+1) | 6 |
| 6 | 405,050,204 | 4,700,017,500 | 100,002,500 | 1 | 2,500 | 10,000 | OK | 7 |
| 7 | 430,050,829 | 4,725,018,125 | 25,000,625 | – | – | – | **GLITCH** (< 99,990,000) | – |
| 8 | 505,052,704 | 4,800,020,000 | 100,002,500 (i = 6부터) | 1 | 2,500 | 10,000 | OK | 8 |
| 9 | 605,055,204 | 4,900,022,500 | 100,002,500 | 1 | 2,500 | 10,000 | OK | 9 |
| 10 | 705,057,704 | 5,000,025,000 | 100,002,500 | 1 | 2,500 | 10,000 | OK | 10 |

span = 5,000,025,000 − 4,000,000,000 = 1,000,025,000 tick / 10 s → `freq_hz` 100,002,500, `err_ppb` = 25,000 × 1e9 / 1e9 = **25,000**.

`counter_to_gps_ns` — 같은 model (`last_raw` 705,057,704, `tick[9]` 5,000,025,000, `freq` 100,002,500):

| ts (raw) | (int32)(ts − last_raw) | unwrapped | 쓰는 PPS | elapsed tick | 결과 ns | holdover |
|---|---|---|---|---|---|---|
| 55,041,454 | −650,016,250 | 4,350,008,750 | 3 s | 50,001,250 | 3,500,000,000 | false |
| 3,040,154 | −702,017,550 | 4,298,007,450 | 2 s | 98,002,450 | 2,980,000,000 | false |
| 255,046,454 | −450,011,250 | 4,550,013,750 | 4 s (5 s 없음) | 150,003,750 | 5,500,000,000 | false |
| 905,062,704 | +200,005,000 | 5,200,030,000 | 10 s | 200,005,000 (정확히 2 s) | 12,000,000,000 | false |
| 955,063,954 | +250,006,250 | 5,250,031,250 | 10 s | 250,006,250 | 12,500,000,000 | **true** |
| 3,999,999,000 | −1,000,026,000 | 3,999,999,000 | 없음 (< tick[0]) | – | `PPS_TS_INVALID` | false |

측정 주파수 대신 **nominal 100 MHz**를 쓰면 3.5 s stamp는 3,500,012,500 ns(**+12.5 µs**), 12.5 s stamp는 12,500,062,500 ns(**+62.5 µs**)가 됩니다. 25 ppm × 2.5 s = 62.5 µs. discipline을 안 하면 오차가 PPS 이후 시간에 비례해 커집니다(테스트 6번).

`match_nearest` — 예제 (ms, tol 5):

| cam i | cam | 커서 j 이동 후 후보 | 가까운 쪽 / 거리 | 판정 | 다음 j |
|---|---|---|---|---|---|
| 0 | 0 | 1.2, 101.2 | 1.2 / 1.2 | pair (0, 0) +1.2 | 1 |
| 1 | 100 | 101.2, 301.2 | 101.2 / 1.2 | pair (1, 1) +1.2 | 2 |
| 2 | 200.3 | 301.2, 409.0 | 301.2 / 100.9 | 짝 없음 | 2 |
| 3 | 300 | 301.2, 409.0 | 301.2 / 1.2 | pair (3, 2) +1.2 | 3 |
| 4 | 400 | 409.0, 501.2 | 409.0 / 9.0 | 짝 없음 (> 5) | 3 |
| 5 | 600 | 409.0 건너뜀 → 501.2, 601.2 | 601.2 / 1.2 | pair (5, 5) +1.2, 501.2는 남음 | 6 |
| 6 | 699.6 | 701.2, 801.2 | 701.2 / 1.6 | pair (6, 6) +1.6 | 7 |
| 7 | 800 | 801.2, 901.2 | 801.2 / 1.2 | pair (7, 7) +1.2 | 8 |
| 8 | 900 | 901.2 | 901.2 / 1.2 | pair (8, 8) +1.2 | 9 |

결과: 반환 **7**, `cam_unmatched` 2, `lidar_unmatched` 2, `max_abs_dt_ns` 1,600,000.

다른 테스트에서 확인하는 숫자:

| 테스트 | 결과 |
|---|---|
| 1. 깨끗한 10 s, +25 ppm / −40 ppm | ok 10, 100,002,500 Hz / +25,000 ppb · 99,996,000 Hz / −40,000 ppb |
| 1. PPS edge jitter ±3 tick | ok 10, 100,002,500 Hz, **+25,002 ppb** (마지막 edge +2 tick이 10 s span에 2 ppb로 남음) |
| 2. 5 s 없음 / 3·4·5 s 없음 / +60 ppm에서 3·4·5 s 없음 | ok 8, missed 1, `sec[5]` = 6 / ok 6, missed 3 (k = 4) / MISSED, bad 0, 100,006,000 Hz (오차 24,000 ≤ 4w = 40,000) |
| 3. 3.25 s glitch + 6 s 뒤 50 µs ringing | glitch 2, ok 10, 정상 edge 11개 |
| 4. 2.5 s stray edge(마지막 정상에서 1.5 s) + 4 s edge 300 ppm 늦음 | bad 2, ok 1, missed 2 (버린 edge 자리는 다음 edge에서 MISSED로 셈), `sec[3]` = 5 |
| 7. cap 3 | 반환 7, 앞 3개만 기록 |
| 7. 같은 sweep 근처 카메라 2장 (0, 0.5 ms) | 짝 1, cam_unmatched 1 |
| 8. glitch만 있는 입력 | `-ENODATA`, glitch 2, freq 0 |

### 4.5 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `t += (uint32_t)(latch[i] - latch[i-1])`: unsigned 뺄셈은 modulo 2^32라 wrap이 자동으로 풀립니다. glitch edge도 unwrap에는 참여합니다(시간축은 연속이어야 하므로). 분류만 마지막 정상 edge 기준입니다.
- `k * w`: 2초짜리 간격은 오차도 2배 허용합니다. oscillator 오차가 시간에 비례해서 쌓이기 때문입니다.
- `err_ppb`의 곱셈 `(ticks − secs·nom) × 1e9`: 차이는 최대 `secs·nom × 1000 ppm` 수준이라 `int64_t`에서 넘치지 않습니다. `nominal_hz`를 1 GHz 이하로 제한한 것도 이 범위를 지키기 위해서입니다.
- `elapsed × 1e9`: elapsed가 2^33 tick 미만이므로 8.6e18 미만 → `uint64_t` 안전. `(x + f/2) / f`로 반올림합니다.
- 매칭에서 `j = best + 1`이 "sweep 한 번만 사용" 규칙의 전부입니다. 따로 `used[]` 배열이 필요 없습니다.

### 4.6 복잡도

- `pps_analyze`: 시간 O(n), 메모리 O(1) (model은 호출자 구조체 안).
- `counter_to_gps_ns`: 뒤에서부터 선형 탐색이라 최근 stamp는 O(1), 최악 O(n). latch가 많으면 이진 탐색 O(log n).
- `match_nearest`: O(n_cam + n_lidar), 추가 메모리 O(1).

---

## 5. 임베디드 관점으로 말할 것

- **PPS는 "언제", NMEA/ToD는 "몇 초"**: PPS edge에는 라벨이 없습니다. 수신기가 UART로 보내는 NMEA `RMC`/`ZDA` 또는 UBX 메시지가 그 초의 번호를 알려 줍니다. 수신기마다 "방금 지난 펄스"인지 "다음 펄스"인지가 다르니(u-blox `TIM-TP`는 다음 펄스) 데이터시트로 확인합니다. GPS 시간은 UTC보다 18 s 앞섭니다(윤초). 이 문제의 `sec[]` offset에 그 GPS 초를 더하면 절대 시간이 됩니다.
- **HW timestamp vs SW timestamp**: PPS와 sensor stamp를 FPGA의 timer **input capture**로 찍으면 오차는 1 tick(10 ns) + PPS 자체 jitter(수십 ns)입니다. Linux `pps-gpio`처럼 ISR에서 counter를 읽으면 인터럽트 지연이 수 µs ~ 수십 µs, 부하가 걸리면 그 이상 흔들립니다. 이 checker의 OK window와 jitter 통계로 두 방식의 차이가 바로 보입니다.
- **PTP / gPTP와의 관계**: Ethernet LiDAR·radar·camera는 보통 IEEE 1588 PTP 또는 차량용 **802.1AS gPTP**로 시간을 받습니다. 센서 박스가 GPS PPS로 discipline된 **grandmaster**가 되고, PHY/MAC의 HW timestamp로 Sync / Pdelay 메시지를 주고받아 각 센서가 offset과 rate를 맞춥니다. 이 문제의 "PPS로 주파수·위상 맞추기"가 grandmaster 안에서 일어나는 일이고, PTP는 그 시간을 네트워크로 나눠 주는 방법입니다. 오래된 LiDAR는 PTP 대신 PPS + NMEA를 직접 받습니다.
- **oscillator drift와 holdover**: 일반 XO는 온도 범위에서 ±20 ~ 50 ppm, TCXO는 ±0.5 ~ 2 ppm입니다. 25 ppm이면 1 s에 25 µs, 터널에서 1분 holdover면 1.5 ms가 벌어집니다. 그래서 주파수를 창 하나의 고정값이 아니라 **매초 갱신**(PI servo나 이동 평균)하고, holdover 시간과 예상 오차를 데이터에 같이 남깁니다.
- **floating point를 안 쓰는 이유**: GPS epoch 기준 ns는 약 1.4e18이고 `double`의 정수 정밀도(2^53 ≈ 9e15)를 넘습니다. 그 크기에서 double의 간격은 256 ns라서 ns 정밀도가 깨집니다. 카메라 frame 정렬 검사는 [V12](../V12_frame_timestamp_check/index.html), CSI-2 패킷 레벨은 [V9](../V9_csi2_packet_parse/index.html).

## 5.1 면접관 Iris Lee(SW·시스템 출신 Principal)가 파고들 포인트

- **"The PPS has no label. How do you know which second it is?"** → NMEA/UBX 시간 메시지를 PPS에 붙이는 규칙(이전/다음 펄스)을 수신기별로 고정하고, 메시지가 PPS 후 일정 창(예: 50 ~ 900 ms) 안에 왔는지 검사합니다. 초 번호가 1 s 어긋나면 모든 센서가 같이 1 s 틀려서 sync 검사로는 안 잡히니, 독립된 시간원(PTP grandmaster, 차량 GNSS 로그)과 교차 확인합니다. *"I pair each PPS with the time-of-day message by a fixed per-receiver rule, check the message lands in its window, and cross-check the second against an independent source, because a one-second error is common-mode and sync checks won't see it."*
- **"How would you validate the time sync end to end on the vehicle?"** → 센서 박스가 내보내는 PPS out과 GPS PPS를 스코프로 비교해 offset(ns)을 잽니다. 카메라는 PPS에 묶인 LED strobe를 찍어 frame 안 LED 위치로 노출 시각을 확인하고, LiDAR는 packet의 PPS 상태 필드와 stamp를 봅니다. 결과는 센서별 offset 분포와 카메라-LiDAR skew로 냅니다. *"Scope the box's PPS-out against GPS PPS for offset, a PPS-locked LED strobe for camera exposure time, and per-sensor offset histograms plus camera-to-LiDAR skew as the report."*
- **"GPS drops in a tunnel for 3 minutes. What happens?"** → 마지막 측정 주파수로 holdover 계속, flag를 데이터에 붙이고 예상 오차(ppm × 시간)를 같이 기록합니다. GPS가 돌아오면 시간을 한 번에 점프(step)시키지 않고 rate를 살짝 바꿔 **slew**로 따라가서 timestamp가 뒤로 가지 않게 합니다. 큰 오차일 때만 step하고 이벤트로 남깁니다. *"Keep running on the last measured frequency, flag the data with an error bound, and on reacquisition slew rather than step so timestamps never go backwards."*
- **"How does the system-validation team gate this before a release?"** → 숫자로 된 기준을 둡니다: 장시간 drive log에서 missed / glitch / bad 카운터, 주파수 오차가 oscillator 스펙 안, 센서별 GPS 대비 최대 offset(HW stamp면 µs 이하), 센서 간 최대 skew(예: 카메라-LiDAR 수 ms 이하), holdover 진입 횟수와 최장 시간, cold start 후 lock 시간. 이 함수들의 출력이 그대로 그 카운터입니다. *"Release gates are counters and bounds: zero unexplained missed pulses over the soak, max offset and max cross-sensor skew under spec, and holdover time and lock time tracked per build."*
- **"Why not just use Linux timestamps and chrony?"** → 개발용으로는 충분하지만 센서 퓨전용 stamp는 데이터가 **들어온 순간** HW에서 찍어야 합니다. SW 경로는 DMA, 인터럽트, 스케줄링 지연이 섞여 ms 단위로 흔들립니다. chrony / `phc2sys`는 시스템 시계를 PTP 하드웨어 시계(PHC)에 맞추는 데 쓰고, 센서 stamp 자체는 HW 카운터가 정합니다. *"chrony and phc2sys keep the system clock honest, but sensor stamps have to come from a hardware counter at capture, because the software path adds milliseconds of jitter."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Explain PTP and gPTP briefly. How do they relate to PPS? | master가 Sync(+Follow_Up)로 시각을 보내고, slave가 Delay_Req(1588) 또는 이웃끼리 Pdelay(802.1AS)로 링크 지연을 재서 offset = (t2 − t1) − delay를 계산합니다. 모든 timestamp는 MAC/PHY HW에서 찍습니다. gPTP는 차량용 프로파일로 모든 스위치가 time-aware(홉마다 지연 측정)라 sub-µs가 나옵니다. PPS는 grandmaster를 GPS에 묶는 입력이고, PTP는 그 시간을 네트워크로 퍼뜨리는 수단입니다. |
| Hardware vs software timestamping — numbers? | HW input capture: 1 tick(100 MHz면 10 ns) + 신호 jitter. PHY timestamp: 수 ns ~ 수십 ns. SW(ISR에서 counter 읽기): 인터럽트 지연 수 µs, 부하 시 수십 µs 이상이고 꼬리가 깁니다. 그래서 OK window를 SW 방식에 맞춰 넓히면 진짜 BAD edge(늦은 PPS)를 놓칩니다. |
| The oscillator drifts with temperature. Is one frequency per window enough? | 10 s 창이면 대부분 충분하지만, 긴 log는 구간마다 주파수가 다릅니다. 매 PPS 간격으로 주파수를 갱신하고(필터나 PI servo), 변환은 그 구간의 로컬 주파수를 씁니다. 두 PPS 사이 stamp는 양쪽 PPS로 **보간**하면 drift 영향이 거의 사라집니다(후처리일 때). |
| GPS comes back after holdover and you are 400 µs off. Step or slew? | 기본은 slew: 주파수를 몇 ppm 바꿔 수 초 ~ 수십 초에 걸쳐 따라갑니다. timestamp가 단조 증가해야 다운스트림(퓨전, 로그)이 안 깨집니다. 오차가 임계치(예: 1 ms)를 넘으면 step하고 그 시점을 이벤트로 기록합니다. Linux `chrony`의 `makestep`과 같은 정책입니다. |
| What if the very first latch is a glitch? | 이 코드는 `latch[0]`을 믿으므로 뒤 edge가 전부 BAD/GLITCH로 보일 수 있습니다. 실제로는 **연속 N개(예: 3개)가 서로 정수 초 간격으로 맞을 때** lock을 선언하고 그 첫 edge를 기준으로 삼습니다. BAD가 연속으로 계속 나오면(PPS 위상 점프) 같은 방식으로 다시 lock합니다. |
| Which time should a LiDAR sweep and a camera frame carry for matching? | 카메라는 노출 중간(rolling shutter면 줄마다 다름), LiDAR는 sweep 시작 또는 packet/point별 시각이 보통입니다. 회전형 LiDAR는 카메라 FOV를 지나는 순간에 카메라를 트리거하기도 합니다(phase lock). 기준이 다르면 고정 offset이 생기니 센서별 보정 상수를 두고 매칭 전에 뺍니다. |
| Your matching is greedy. When is it not optimal? | 카메라 i가 i+1에 더 가까운 sweep을 먼저 가져가는 경우입니다. `tol < 주기/2`이고 두 센서 주기가 같으면 한 sweep 주변에 후보 카메라가 하나뿐이라 greedy = 최적입니다. 아니면 "서로 가장 가까운 쌍(mutual nearest)"만 인정하거나, 정렬된 두 배열에 DP(O(n·m))나 Hungarian을 씁니다. |
| Why does the counter wrap matter if you unwrap anyway? | unwrap은 "연속한 두 값이 2^32 tick(42.9 s) 이내"라는 가정에 기대고 있습니다. PPS가 43 s 이상 끊기면 몇 바퀴 돌았는지 모릅니다. 그래서 HW에서 64-bit counter를 쓰거나, wrap 횟수를 세는 상위 counter를 같이 latch합니다. |

## 7. 자주 하는 실수

- 간격을 **직전 raw latch**부터 잼 → glitch 하나가 "0.25 s + 0.75 s" 두 개의 이상 간격이 되고 다음 정상 edge도 버려짐 (테스트 3번, 5번)
- raw 32-bit 값을 `<`로 비교해 직전 PPS를 찾음 → wrap 뒤 stamp가 "첫 PPS 이전"으로 나옴 (테스트 6번의 3,040,154)
- MISSED 간격을 1초로 셈 → 10 s span을 9 s로 계산해 주파수가 약 11% 크게 나옴 (테스트 2번 `sec[5] == 6`)
- 허용 오차를 `k`배로 안 늘림 → +60 ppm oscillator의 4초 gap(오차 24,000 tick > `w` 10,000)이 BAD로 잘못 분류 (테스트 2번)
- 변환에 **nominal** 주파수 사용 → 12.5 s에서 62.5 µs 오차 (테스트 6번)
- `double`로 ns 계산 → 1.4e18 근처에서 256 ns 단위로 뭉개짐
- `elapsed * 1000000000`을 `uint32_t`/`int32_t`로 계산 → overflow
- holdover를 `>=`로 판정 → 정확히 2 s에서 flag (테스트 6번 경계)
- 매칭에서 sweep을 재사용하거나 index로 짝지음 → 한쪽이 drop하면 뒤가 전부 어긋남 (테스트 7번)
