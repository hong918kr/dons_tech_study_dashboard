# R4 · Geofence Monitor

> **ID**: R4 · **Priority**: P1 · **Pattern**: 2D geometry (cross product) + streaming state with hysteresis + first-crossing time · **Time**: 35 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh R4` / `./run.sh R4 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 다각형 울타리(geofence) 안/밖을 판정하는 기하 함수들(A·B)을 만들고, GPS fix가 들어올 때마다 INSIDE/WARN/BREACH 상태를 바꾸며 이탈 시각까지 알려주는 모니터(C)를 만든다.
- **상황**: 자율 UGV는 작업 구역 안에 있어야 한다. GPS fix는 약 1초에 한 번, 이미 local ENU 미터 좌표(`x` = 동쪽, `y` = 북쪽)로 변환돼서 온다. 울타리는 **오목할 수도 있는** 단순 다각형(꼭짓점 표, 닫는 꼭짓점 반복 없음).
- **배경 개념**
  - **cross product(외적)**: `(a − o) × (b − o)`의 z 성분. 양수면 `b`가 `o → a` 선의 **왼쪽**, 음수면 오른쪽, 0이면 한 직선 위. 선분 교차 판정의 기본 도구.
  - **ray casting (even-odd)**: 점에서 한 방향으로 반직선을 쏴서 다각형 변과 몇 번 만나는지 센다. 홀수면 안, 짝수면 밖. 오목 다각형에도 통한다. 반직선이 꼭짓점을 정확히 지날 때 두 번 세는 버그가 고전.
  - **점-선분 거리(clamped projection)**: 점을 선분 직선에 수직으로 내린 위치를 선분 범위 [0, 1] 안으로 잘라서(clamp) 그 점까지 거리를 잰다.
  - **hysteresis**: WARN에 들어가는 기준(`< warn_margin`)과 나오는 기준(`>= warn_margin + 1.0 m`)을 다르게 해서 경계에서 상태가 깜빡이지 않게 한다.
- **입력**: Part A/B는 점(`Vec2`)과 다각형 꼭짓점 배열 `poly[n]`. Part C는 `fence_init(f, poly, n, warn_margin_m)`로 설정한 뒤, fix마다 `fence_update(f, pos, t_ms, breach_t_ms)`. 시각은 `uint32_t` ms.
- **출력**: A는 외적 값 / 교차 여부 + 위치 `t` / 안팎 `bool`. B는 최소 거리(잘못된 입력이면 `-1.0`). C는 이벤트(`FE_NONE`, `FE_INSIDE`, `FE_WARN`, `FE_BREACH`, `FE_TRANSIT`, `FE_FAULT`)와 필요할 때 `*breach_t_ms`.
- **규칙**
  - Part A: `seg_intersect`는 두 선분이 **한 점**에서 닿거나 교차하면 true, `t` = `p1 → p2` 위 위치(0 = `p1`, 1 = `p2`). 끝점에 닿아도 인정. 평행·collinear는 false. `point_in_polygon`은 변·꼭짓점 위 점을 **안**으로 침. `n < 3`이면 false.
  - Part B: 모든 변 선분까지의 최소 거리. `n < 3` 또는 `poly == NULL`이면 `-1.0`.
  - Part C 상태: INSIDE = 안 + 거리 `>= warn_margin`, WARN = 안 + 거리 `< warn_margin`(INSIDE로 돌아가려면 거리 `>= warn_margin + 1.0`), BREACH = 밖.
    - 이벤트는 **상태가 바뀔 때만** 반환, 아니면 `FE_NONE`.
    - BREACH로 바뀌면 `prev_fix → pos` 선분이 울타리를 처음 넘은 시각 `t_prev + u · (t_now − t_prev)`를 ms 반올림해서 기록. timestamp는 wrap 가능. 이전 fix가 없으면 `t_ms`.
    - 두 fix가 다 안인데 그 사이 선분이 울타리를 서로 다른 두 점 이상에서 넘으면(오목한 홈을 건너뜀) `FE_TRANSIT`, 첫 교차 시각 기록, `transits++`.
    - 잘못된 울타리(`n < 3`, `NULL`, 음수 margin)는 매번 `FE_FAULT`. `breach_t_ms`는 `NULL`일 수 있음. `malloc` 금지.
- **예제 풀어 읽기**: U자 울타리 60 m × 50 m, 홈은 x 20~40, y 20~50. `warn_margin = 5.0`. (56,20)은 동쪽 변에서 4 m → WARN. (58,30) → (64,38)에서 x = 60을 u = 1/3 지점에 넘음 → BREACH, `breach_t_ms = 5000 + 1000/3 = 5333`. (55,40)에서 다시 안, 5 m < 6 m → WARN. (54.5,35)는 5.5 m라 hysteresis로 WARN 유지. (50,30)은 10 m → INSIDE. (10,40)@0 → (50,40)@1000은 홈을 건너뛰어 TRANSIT, `breach_t_ms = 250`.
- **놓치기 쉬운 것**: 반직선이 꼭짓점을 지나는 경우(마름모 꼭짓점, U의 y = 20 줄)의 이중 카운트; 홈 입구(30,50)처럼 반직선이 위쪽 변 위를 지나는 경우; 경계 위 점은 안 + 거리 0; 밖에 있는 점도 거리는 양수로 계산; 평행·collinear 선분은 false, 끝점 접촉은 `t = 1.0`; 첫 fix부터 밖이면 그 시각으로 BREACH; `2^32 − 500`에서 500으로 timer wrap되는 보간 → 250; TRANSIT 후 상태는 INSIDE이고 다시 보고하지 않음.
- **구현할 함수**
  - Part A: `cross(o, a, b)`, `seg_intersect(p1, p2, q1, q2, t)`, `point_in_polygon(p, poly, n)`
  - Part B: `dist_to_polygon(p, poly, n)`
  - Part C: `fence_init(f, poly, n, warn_margin_m)`, `fence_update(f, pos, t_ms, breach_t_ms)`

## 1. Problem

An autonomous UGV must stay inside an operating area (a **geofence**). GPS fixes arrive about once per second and have already been converted to **local ENU meters** (`x` = east, `y` = north). The fence is a **simple polygon that may be concave**, given as a vertex table (no repeated closing vertex).

**Part A — geometry primitives**

- `cross(o, a, b)` returns the z component of `(a − o) × (b − o)`: positive if `b` is to the **left** of the directed line `o → a`, negative if right, zero if collinear.
- `seg_intersect(p1, p2, q1, q2, &t)` returns true if segment `p1–p2` touches or crosses segment `q1–q2` at **one point**, and writes `t` = position along `p1 → p2` (0 = `p1`, 1 = `p2`). Touching at an endpoint counts. **Parallel or collinear segments return false** (there is no single crossing point; a vehicle driving exactly along the fence line is "on the fence").
- `point_in_polygon(p, poly, n)` uses **even-odd ray casting** and must work for concave polygons. A point exactly **on an edge or a vertex counts as inside**. `n < 3` → false.

**Part B** — `dist_to_polygon(p, poly, n)` returns the minimum distance from `p` to any **edge segment** (clamped projection). `n < 3` or `poly == NULL` → `-1.0`.

**Part C — streaming monitor.** One call per GPS fix. States:

| State | Condition |
|---|---|
| `INSIDE` | inside and `dist >= warn_margin_m` |
| `WARN` | inside and `dist < warn_margin_m`. Leave WARN (or BREACH) for INSIDE only when `dist >= warn_margin_m + FENCE_HYST_M` (1.0 m) |
| `BREACH` | outside |

- Return an event **only when the state changes** (`FE_INSIDE`, `FE_WARN`, `FE_BREACH`), otherwise `FE_NONE`.
- On a change from INSIDE/WARN to BREACH, write the **interpolated time of the first fence crossing** on the segment `prev_fix → pos`: `t_prev + u * (t_now − t_prev)`, rounded to the nearest ms. Timestamps are `uint32_t` ms and **may wrap**. If there is no previous fix, the breach time is `t_ms`.
- If both fixes are inside but the segment between them crosses the fence at **two or more distinct points** (the fix jumped over the notch of a concave fence), return `FE_TRANSIT`, write the first crossing time, and increment `f->transits`.
- An invalid fence (`n < 3`, `NULL`, negative margin) returns `FE_FAULT` on every update. `breach_t_ms` may be `NULL`. No `malloc`.

```c
typedef struct { double x, y; } Vec2;            /* local ENU meters */
typedef enum { FENCE_INSIDE, FENCE_WARN, FENCE_BREACH } FenceState;
typedef enum { FE_NONE, FE_INSIDE, FE_WARN, FE_BREACH, FE_TRANSIT, FE_FAULT } FenceEvent;
typedef struct { const Vec2 *poly; size_t n; double warn_m; bool valid; FenceState state;
                 bool has_prev; Vec2 prev; uint32_t prev_t; uint32_t transits; } Fence;

double cross(Vec2 o, Vec2 a, Vec2 b);
bool   seg_intersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2, double *t);
bool   point_in_polygon(Vec2 p, const Vec2 *poly, size_t n);
double dist_to_polygon(Vec2 p, const Vec2 *poly, size_t n);
void       fence_init(Fence *f, const Vec2 *poly, size_t n, double warn_margin_m);
FenceEvent fence_update(Fence *f, Vec2 pos, uint32_t t_ms, uint32_t *breach_t_ms);
```

### Example

```
fence (U shape, 60 m x 50 m, notch x in (20,40), y in (20,50)):
  (0,0) (60,0) (60,50) (40,50) (40,20) (20,20) (20,50) (0,50)
warn_margin_m = 5.0, FENCE_HYST_M = 1.0

t_ms   fix         event
1000   (10,10)     NONE
2000   (30,8)      NONE
3000   (50,10)     NONE
4000   (56,20)     WARN       4 m from the east edge
5000   (58,30)     NONE
6000   (64,38)     BREACH     breach_t_ms = 5333  (crossed x = 60 at u = 1/3)
7000   (62,40)     NONE
8000   (55,40)     WARN       back inside, 5 m < 6 m
9000   (54.5,35)   NONE       5.5 m: hysteresis keeps WARN
10000  (50,30)     INSIDE     10 m

notch jump: (10,40) @0 -> (50,40) @1000  =>  TRANSIT, breach_t_ms = 250
```

### Constraints

- `3 <= n <= 64` for a valid fence; coordinates within ±10 km of the local origin.
- Fixes arrive in time order; consecutive fixes are less than 2^31 ms apart.
- Polygon is simple (no self-intersection); vertex order may be CW or CCW.

### Clarifying questions to ask (first 3 minutes)

1. Are positions lat/lon or already local meters?
2. Does a point exactly on the fence line count as inside or outside?
3. Can the fence be concave? Holes? Is the closing vertex repeated?
4. Level output or events on change? Is there hysteresis on the warn band?
5. GPS rate and max speed — can one fix jump over a thin part of the fence?
6. What if the very first fix is already outside, or GPS drops out?

## 2. Your workspace

`stub.c`를 복사해서 시작하거나 로컬에서 `./run.sh R4`로 바로 테스트하세요. 테스트 8개(정상 2개 + 엣지 6개)가 들어 있습니다. Part A → B → C 순서로 풀면 앞 테스트부터 하나씩 통과합니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 모든 것은 cross 하나로</summary>

`d1 = cross(q1,q2,p1)`, `d2 = cross(q1,q2,p2)`는 p1, p2가 직선 q의 **어느 쪽에 얼마나 떨어져 있는지**(부호 있는 거리 × |q|)입니다. 부호가 다르면 직선을 건넌 것이고, 건넌 위치는 `t = d1 / (d1 − d2)`입니다. 반대쪽(`d3`, `d4`)도 부호가 달라야 선분끼리 만납니다. `d1 == d2 == 0`이면 collinear → false.

</details>

<details>
<summary>Hint 2 — ray casting의 꼭짓점 함정</summary>

오른쪽(+x)으로 반직선을 쏘고 edge를 건널 때마다 `inside = !inside`. 조건은 **반개구간** `(a.y > p.y) != (b.y > p.y)` 하나면 됩니다. 꼭짓점은 한 번만 세지고, 수평 edge는 자동으로 빠지므로 `b.y − a.y`로 나눌 때 0으로 나눌 일도 없습니다. edge 위 판정(`cross == 0` + bbox 안)은 루프 안에서 먼저 하고 바로 `true`를 반환합니다.

</details>

<details>
<summary>Hint 3 — fence_update의 순서</summary>

1. `(inside?, dist, old state)` → `next state` 계산 (WARN에서 나갈 때만 `margin + 1 m`)
2. `next != old`일 때만 이벤트
3. BREACH로 바뀌면 `prev → pos` 선분을 모든 edge와 `seg_intersect` 해서 **가장 작은 u** → `t_prev + (uint32_t)(u·dt + 0.5)`, `dt = t_now − t_prev`는 `uint32_t` 뺄셈
4. inside → inside인데 서로 다른 u가 2개 이상이면 TRANSIT
5. `prev`, `prev_t` 갱신

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

문제는 세 층으로 나뉩니다. **기하 원시 함수**(cross, 선분 교차, 점-다각형, 점-선분 거리) 위에 **상태 머신**(INSIDE/WARN/BREACH + 히스테리시스)을 얹고, 상태가 BREACH로 바뀌는 순간에만 **선분 교차 파라미터 u로 정확한 시각**을 보간합니다. 실제 스크린 문제([R1](../R1_circular_car_collision/index.html), 두 차의 충돌 시각)와 같은 구조입니다 — "샘플 사이 어디서 사건이 일어났나"를 연속 시간으로 복원하는 것.

<div class="chart" data-src="chart1.svg" data-caption="예제 경로: 4000에서 WARN(4 m), 5000→6000 사이 x=60을 u=1/3에서 건너 BREACH 5333 ms, 8000 재진입 WARN, 9000은 5.5 m지만 히스테리시스(6 m)로 WARN 유지, 10000에 INSIDE. 보라 점선은 notch를 건너뛴 TRANSIT(테스트 8)"></div>

WARN 띠의 안쪽 경계가 오목한 꼭짓점 (20,20), (40,20) 근처에서 **원호**인 것에 주의하세요. 거리는 edge **선분**까지의 거리라서, 꼭짓점 근처에서는 꼭짓점까지의 거리가 됩니다. 클램프 없이 무한 직선까지 거리를 재면 (56,20)은 notch 바닥 직선 y=20 위라서 0 m가 나옵니다(정답 4 m).

### 4.2 Ray casting과 꼭짓점 케이스

<div class="chart" data-src="chart2.svg" data-caption="왼쪽: A(10,20)는 3번 건너서 inside, notch 안의 B(30,35)는 2번이라 outside. 오른쪽: 반직선이 꼭짓점 (10,0)을 지나면 naive 판정은 두 edge를 다 세어 2(오답), 반개구간 규칙은 1번만 셈"></div>

- A의 반직선 y=20은 꼭짓점 (20,20), (40,20)을 지나고 notch 바닥(수평 edge) 위를 달립니다. 반개구간 규칙에서는 수직 벽 (20,20)–(20,50), (40,50)–(40,20)이 각각 한 번, 수평 edge는 0번 → 동쪽 벽까지 합쳐 3 → inside.
- notch 입구 (30,50)도 같은 원리로 outside가 나옵니다(테스트 3). 반직선이 꼭짓점 (40,50), (60,50)과 수평 edge (40,50)–(60,50) 위를 그대로 지나는 최악 케이스인데, 반개구간 규칙에서는 0번 → outside입니다.
- 오목 다각형이어도 even-odd는 그대로 맞습니다. 볼록 전용 방법(모든 edge에 대해 cross 부호가 같은지)은 U 모양에서 틀립니다.

### 4.3 선분 교차 파라미터 u

<div class="chart" data-src="chart3.svg" data-caption="breach 구간 (58,30)→(64,38)과 동쪽 edge x=60: d1=+100, d2=−200 → u=100/300=1/3. 직선까지 거리 2 m : 4 m의 비율과 같습니다"></div>

`cross(q1,q2,p)`는 `|q2 − q1| × (p에서 직선까지의 부호 있는 거리)`입니다. 그래서 `u = d1 / (d1 − d2)`는 그냥 "거리 2 m에서 −4 m로 선형으로 변할 때 0이 되는 지점"입니다. 이 u를 시간에 그대로 쓰는 것은 **두 fix 사이 등속 직선 운동**을 가정한 것입니다(1 Hz, UGV 속도에서는 충분히 맞음).

**parallel/collinear 선택**: `d1 == d2`이면 분모가 0입니다. 같은 부호면 평행이라 교차 없음, 둘 다 0이면 collinear인데 교차점이 하나가 아니라 구간이므로 **false**로 정했습니다. fence 선 위를 따라 달리는 것은 "on fence = inside"이고, 그 선을 따라가다 밖으로 나가면 **이웃 edge**와의 교차가 잡히므로 breach 시각은 여전히 구해집니다.

### 4.4 상태 머신과 TRANSIT 설계

| old \ 입력 | outside | inside, d < 5 | inside, 5 ≤ d < 6 | inside, d ≥ 6 |
|---|---|---|---|---|
| INSIDE | **BREACH** (+시각 보간) | **WARN** | INSIDE | INSIDE |
| WARN | **BREACH** (+시각 보간) | WARN | WARN (히스테리시스) | **INSIDE** |
| BREACH | BREACH | **WARN** | **WARN** | **INSIDE** |

**TRANSIT**: 끝점 두 개만 보면 inside → inside라서 상태 변화가 없지만, 실제 차는 notch 밖(금지 구역)을 지나갔습니다. 선택지는 (a) 이벤트로 보고, (b) 카운터만 올림 — 둘 다 했습니다. `FE_TRANSIT` 이벤트(시각 포함)를 반환하고 `f->transits++`. 상태는 끝점 기준(INSIDE/WARN)으로 둡니다. 지금 차는 안에 있으니 BREACH로 래치하면 거짓이 되고, 대신 safety 쪽이 "구역 이탈 이력"으로 로그·경보를 냅니다.

교차 개수를 그냥 세면 반직선처럼 **꼭짓점을 지나는 선분이 이웃 edge 두 개와 같은 u로 두 번** 잡힙니다. 그래서 개수 대신 **서로 다른 u**인지(`u_last − u_first > 1e-6`)로 판정합니다.

### 4.5 손으로 따라가기 (trace)

| t_ms | fix | inside? | dist (m) | old → next | event | breach_t |
|---|---|---|---|---|---|---|
| 1000 | (10,10) | ✓ | 10 | INSIDE → INSIDE | NONE | |
| 2000 | (30,8) | ✓ | 8 | INSIDE → INSIDE | NONE | |
| 3000 | (50,10) | ✓ | 10 | INSIDE → INSIDE | NONE | |
| 4000 | (56,20) | ✓ | 4 | INSIDE → WARN (4 < 5) | **WARN** | |
| 5000 | (58,30) | ✓ | 2 | WARN → WARN | NONE | |
| 6000 | (64,38) | ✗ | 4 | WARN → BREACH | **BREACH** | edge x=60, u = 1/3 → 5000 + 333.3 → **5333** |
| 7000 | (62,40) | ✗ | 2 | BREACH → BREACH | NONE | |
| 8000 | (55,40) | ✓ | 5 | BREACH → WARN (5 < 6) | **WARN** | |
| 9000 | (54.5,35) | ✓ | 5.5 | WARN → WARN (5.5 < 6) | NONE | |
| 10000 | (50,30) | ✓ | 10 | WARN → INSIDE (10 ≥ 6) | **INSIDE** | |

엣지 테스트 숫자:
- **타이머 랩**(테스트 7): (52.5,25) @ 4294966796 (= 2^32 − 500, dist 7.5 m) → (62.5,25) @ 500. `dt = 500 − 4294966796 = 1000`(uint32), x=60에서 u = 0.75 → 4294966796 + 750 = 2^32 + 250 → **250**.
- **notch transit**(테스트 8): (10,40) @ 0 → (50,40) @ 1000, 두 점 모두 dist 10 m인 INSIDE. 교차 u = 0.25 (x=20), 0.75 (x=40) → **TRANSIT**, breach_t = **250**, `transits = 1`.
- **첫 fix가 밖**(테스트 6): (−3,10) @ 777 → BREACH, 이전 fix가 없으니 breach_t = **777**.

### 4.6 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `seg_intersect`는 `cross` 4번이 전부입니다. `sgn()`에 `GEO_EPS`를 둬서 "정확히 0"을 판정합니다. 끝점이 edge에 닿는 경우(테스트 5, t = 1)가 여기서 걸립니다.
- `point_in_polygon`은 `for (i = 0, j = n − 1; i < n; j = i++)` 관용구로 닫는 edge `(poly[n−1], poly[0])`까지 돕니다. 마지막 edge를 빠뜨리는 실수가 흔합니다.
- `lerp_time`: `dt`를 먼저 `uint32_t` 뺄셈으로 구하고, 오프셋만 double로 계산한 뒤 다시 `uint32_t` 덧셈 → 랩어라운드가 자연스럽게 처리됩니다. `t_prev + u * (t_now − t_prev)`를 double로 통째로 계산하면 랩 순간에 음수 시간이 나옵니다.
- 상태 계산과 이벤트 결정을 분리했습니다(`next`를 먼저 구하고 `next != old`일 때만 이벤트). 엣지 트리거 규칙을 한 줄로 지키는 방법입니다 → [T1-2](../T1-2_streaming_hysteresis/index.html)와 같은 패턴.

### 4.7 복잡도

- `point_in_polygon`, `dist_to_polygon`: **O(n)** 시간, O(1) 메모리.
- `fence_update`: fix 하나당 **O(n)** (inside 판정 + 거리 + 상태가 바뀔 때만 교차 검사). n = 8이면 edge 테스트 수십 번이라 1 Hz~10 Hz GPS에서는 무시할 수준입니다.
- `Fence`는 polygon 포인터만 들고 있어 RAM은 수십 바이트, polygon은 flash의 `const` 테이블.

---

## 5. 임베디드 관점으로 말할 것

- **lat/lon → local ENU 먼저**: 기하 연산은 항상 미터 단위 평면에서 합니다. 기준점(home 또는 fence 첫 꼭짓점) 기준 equirectangular 근사 `x = R·cos(lat0)·Δlon`, `y = R·Δlat` (R ≈ 6,371 km, 각도는 rad). u-blox 같은 GNSS는 lat/lon을 `int32` 1e-7° 단위로 주므로, **Δ를 정수로 먼저 빼고** 그다음 실수로 바꿔야 정밀도를 잃지 않습니다. fence 꼭짓점도 로드할 때 한 번만 변환해 둡니다.
- **float vs double**: Cortex-M4F/M33의 FPU는 **single precision만** 하드웨어입니다. double은 소프트웨어 에뮬레이션이라 수십 배 느립니다. local 미터로 바꾼 뒤 ±10 km 범위면 float의 해상도는 약 1 mm라 충분합니다. 반대로 lat 37.x°를 float에 그대로 넣으면 ulp가 약 0.4 m라 쓸 수 없습니다. 더 안전한 대안: **int32 센티미터 좌표 + int64 cross product** — 곱셈이 정확해서 epsilon이 아예 필요 없습니다.
- **sqrt 피하기**: WARN 판정은 `d² < margin²`로 비교하면 `sqrtf`가 필요 없습니다. 거리 값 자체가 필요한 경우(텔레메트리)만 sqrt.
- **breach 시 fail-safe**: BREACH 이벤트 → 즉시 정지(e-stop) 또는 RTL/loiter, 상태를 **래치**하고 operator ack 전까지 자율 주행 재개 금지. geofence 체크는 autonomy 스택과 **독립된 safety MCU/태스크**에서 돌리는 게 정석입니다. fence 데이터가 잘못되면(`FE_FAULT`) arm 자체를 거부합니다.
- **GPS dropout**: fix 품질(fix type, 위성 수, HDOP/accuracy)을 먼저 확인하고, 유효 fix가 T초 이상 없으면 "위치 모름" → 감속/정지(fail-safe). 마지막 위치로 계속 가면 안 됩니다. 긴 공백 뒤 첫 fix는 등속 가정이 깨지므로 보간 대신 `breach_t = t_now`(또는 "unknown") 처리, 짧은 공백은 휠 오도메트리/IMU dead-reckoning으로 메웁니다 → [R2](../R2_diff_drive_odometry/index.html).
- **샘플 사이를 봐야 하는 이유**: 1 Hz fix에 10 m/s면 fix 사이 10 m를 움직입니다. 끝점만 검사하면 얇은 notch를 통째로 건너뛰는 것을 놓칩니다 → 선분 검사(TRANSIT).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| How much error does the equirectangular lat/lon conversion introduce? | 주된 오차는 `cos(lat0)`를 기준점 위도로 고정한 것입니다. 남북으로 Δy 떨어지면 동서 스케일의 상대 오차가 약 `tan(lat0)·Δy/R`라, 위도 38° 근처에서 1 km 떨어진 곳 동서 1 km에 약 0.1 m, 10 km 범위면 약 10 m가 됩니다. 작전 구역이 수 km 이내면 GPS 잡음(수 m)보다 작고, 더 넓으면 ECEF→ENU 정식 변환이나 UTM을 씁니다. |
| You have 200 fences (keep-in and keep-out). How do you keep it fast? | fence마다 **bounding box를 미리 계산**해서 O(1) 사전 필터(박스 밖 + margin 이상 떨어지면 skip). 남은 것만 O(n) 검사. 더 많으면 균일 grid/quadtree로 근처 fence만 찾습니다. 상태가 INSIDE이고 fence까지 거리가 충분히 멀면 몇 fix 동안 검사를 건너뛸 수도 있습니다(최대 속도 × dt로 상한). |
| Can you predict the breach before it happens? | 현재 위치와 속도 벡터로 `p2 = pos + v·T_h`(예: T_h = 5 s)를 만들어 모든 edge와 `seg_intersect` → 최소 u에서 time-to-breach = `u·T_h`. R1의 충돌 시각 예측과 같은 아이디어입니다. 이 값으로 미리 감속(속도 한계 = 남은 거리 / 반응 시간)하면 BREACH 자체를 피할 수 있습니다. |
| GPS is noisy (±3 m). How do you avoid false breaches? | WARN에는 이미 히스테리시스가 있습니다. BREACH는 (1) **연속 N개 fix가 밖**일 때 확정(debounce), (2) 수신기가 주는 accuracy σ만큼 fence를 **안쪽으로 줄인** 판정, (3) `v_max·dt`보다 큰 점프 fix는 outlier로 버림, (4) IMU/오도메트리와 EKF 융합. 단, debounce는 감지를 N·dt만큼 늦추므로 그 거리만큼 margin을 키워야 합니다 — 안전 쪽 trade-off를 말로 설명하세요. |
| How do you unit-test geometry code with floating point? | 정확히 표현되는 좌표(정수, .5)로 테스트하면 결과도 정확해서 1e-6 비교가 안정적입니다. 반드시 넣을 케이스: 꼭짓점을 지나는 ray, edge 위 점, collinear/parallel, CW·CCW 둘 다, 랩어라운드. 그 위에 property test: 무작위 점을 winding-number 구현과 비교, 다각형을 평행이동/회전해도 결과 불변. |
| What if the polygon is self-intersecting or has a hole? | even-odd는 자기교차 영역을 "밖"으로 칩니다. fence 업로드 시 O(n²) 단순 다각형 검사를 한 번 해서 거부하는 게 맞습니다. 구멍(keep-out)은 별도 polygon으로 두고 "keep-in 안 AND 모든 keep-out 밖"으로 판정합니다. |

## 7. 자주 하는 실수

- Ray casting에서 `y`를 닫힌 구간 `[min, max]`로 검사 → 꼭짓점을 두 번 세어 판정이 뒤집힘 → 테스트 3 실패(다이아몬드 중심)
- edge 위 점 처리 누락, 또는 수평 edge에서 `b.y − a.y = 0`으로 나눔 → 테스트 3, 4
- collinear에서 `t = 0/0 = NaN`을 반환하거나 true를 반환 → 테스트 5
- 점-선분 거리에서 투영을 **클램프하지 않음**(무한 직선까지 거리) → (56,20)이 0 m로 나와 테스트 1 실패
- 닫는 edge `(poly[n−1], poly[0])`를 빠뜨림 → 서쪽 벽이 없어져 판정이 엉망
- 보간 시각을 double로 `t_prev + u*(t_now − t_prev)` 통째로 계산 → 랩 순간 음수 → 테스트 7 실패
- 매 fix마다 현재 상태를 이벤트로 반환(레벨 출력) → 테스트 2 실패
- 끝점만 검사하고 선분 교차를 안 봄 → notch transit 놓침 → 테스트 8 실패
- lat/lon 도(degree)를 그대로 x, y로 사용 → 경도 1°는 위도에 따라 길이가 다름(cos(lat))
