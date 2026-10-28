# R1 · Circular Car Collision

> **ID**: R1 · **Priority**: P0 · **Pattern**: state struct + step the state (Solution 1) / closed-form circle (Solution 2) + bisection · **Time**: 35 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh R1` / `./run.sh R1 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 평면 위에서 각자 원을 그리며 달리는 자동차 두 대가 **처음 부딪히는 시각(초)** 을 구한다. 없으면 `-1`.
- **상황**: 각 차의 상태는 `x, y`(m), `heading`(진행 방향, rad, +x축이 0, 반시계가 +), `speed`(m/s), `omega`(회전 속도, rad/s, +는 왼쪽/반시계, −는 오른쪽/시계). speed와 omega가 일정하니 차는 원을 그린다(`omega == 0`이면 직선). 용어 표는 아래 §0-2.
- **입력**: 차 두 대(`Car`), `t_max`(이때까지 안 만나면 포기), Part A는 time step `dt`, Part B/C는 정밀도 `tol`.
- **출력**: 첫 충돌 시각(초). `t_max` 전에 충돌 없으면 `-1`, `t = 0`에 이미 붙어 있으면 `0`.
- **규칙**
  - 충돌 = 두 중심 거리 `<= COLLIDE_DIST`(1.0 m). 차마다 반지름 같은 크기는 없다.
  - Part A (Solution 1, 면접관 방식): 상태에서 **다음 위치**를 계산해 시간을 `dt`씩 진행. `x += speed·dt·cos(heading)`, `y += speed·dt·sin(heading)`, `heading += omega·dt`. 매 스텝 충돌 확인.
  - Part B (Solution 1 refined): 원호를 따라가는 정확한 스텝 + 충돌 시각을 이분법(bisection)으로 `tol`까지 좁히기.
  - Part C (Solution 2, Don 방식): 차를 원(중심 `cx, cy`, 반지름 `r`, `omega`, 시작 각 `phase0`)으로 바꿔서 시각 `t`의 위치를 바로 계산. `omega == 0`이면 원이 없으니 `false`.
  - 동적 할당 금지, `<math.h>` 사용 가능.
- **예제 풀어 읽기**: A = (4, 0), heading 90°, speed 2.0, omega +0.5 → 중심 (0, 0), 반지름 4인 원을 반시계로. B = (6, 3), heading 0°, speed 1.2, omega −0.4 → 중심 (6, 0), 반지름 3인 원을 시계로. Solution 1은 `dt = 0.5`면 12.50 s, `dt = 0.01`이면 12.82 s. refined와 Solution 2는 둘 다 12.822241 s.
- **놓치기 쉬운 것**: 거리가 정확히 1.0이면 충돌, 1.0001이면 아님; 평행하게 5 m 떨어진 직진 차는 `-1`; 시작부터 0.8 m면 `0`; 정면 직진 충돌(4.5 s); 빠른 차를 `dt = 1`로 돌리면 서로 뚫고 지나가서(tunneling) 놓침 → refined는 0.45 s를 찾아야 함; `t_max = 10`이면 `-1`; `omega == 0`은 원 변환 실패; 원 방식 위치가 refined 스텝 위치와 같아야 함.
- **구현할 함수** (모범답안: Solution 1 → §4.1, Solution 2 → §4.2)
  - Part A: `update_car(c, dt)`, `did_collide(a, b)`, `sim_collision_time(a, b, dt, t_max)` (`init_car`는 주어짐)
  - Part B: `update_car_exact(c, dt)`, `find_collision_time(a, b, t_max, tol)`
  - Part C: `circle_from_car(c, out)`, `circle_pos(k, t, x, y)`, `circle_collision_time(a, b, t_max, tol)` (`circle_dist2`는 주어짐)

**실제 기출** — 2026-10-26 Scout AI technical screen(면접관 Jason MacDonald). 경험 이야기 도중 CoderPad 링크가 왔고, 좌표 평면에 자동차 A·B를 그린 뒤 "둘이 원을 그리며 움직일 때 언제 충돌하는지 timestamp를 내라"는 문제였다. 면접관은 C++ 뼈대로 시작했고 Don은 C로 풀었다.

**면접 중 갈린 두 가지 접근** [확인됨 — Don 기억]
- **Don이 처음 제안한 방식**: 원의 **반지름(radius)** 을 파라미터로 둔다 → 이 문서의 **Solution 2**
- **면접관이 준비해 온 솔루션**: 차 상태를 **x, y, heading, speed, rotational speed** 로 두고, 이 파라미터로 **다음 위치를 계산**해 시간을 진행시킨다 → 이 문서의 **Solution 1** (면접에서 기대한 답)

둘 다 같은 답(12.822241 s)이 나온다. 면접관 방식이 기본이므로 **Solution 1을 먼저 손에 익히고**, Solution 2는 "이렇게도 풀 수 있다"는 비교로 말할 수 있게 준비한다.

## 0-2. 용어 정의

> 모범답안(main까지 전부): **Solution 1 → §4.1**, **Solution 2 → §4.2**. 실행: `./run.sh R1 model1`, `./run.sh R1 model2`

<div class="chart" data-src="chart5.svg" data-caption="한 스텝(dt) 동안 일어나는 일: heading 방향으로 speed·dt만큼 이동(x 성분 cos, y 성분 sin) → heading이 ω·dt만큼 회전 → 다음 스텝"></div>

| 이름 | 코드 | 단위 | 뜻 | 예제 값 |
|---|---|---|---|---|
| 위치 | `x`, `y` | m | 평면 위 차의 중심 좌표. +x = 오른쪽, +y = 위쪽 | A (4, 0), B (6, 3) |
| **heading** | `heading` (θ) | rad | 차가 **바라보는(진행하는) 방향**의 각도. +x축이 0, **반시계 방향이 +**. 90° = π/2 = 위(+y), 180° = 왼쪽, −90° = 아래 | A 90° (위), B 0° (오른쪽) |
| speed | `speed` (v) | m/s | heading 방향으로 **1초에 가는 거리** | A 2.0, B 1.2 |
| **omega** | `omega` (ω) | rad/s | **rotational speed** — heading이 1초에 몇 rad 도는지. **+ = 왼쪽(반시계)**, **− = 오른쪽(시계)**, 0 = 직진 | A +0.5 (1초에 28.6° 왼쪽), B −0.4 |
| **dt** | `DT` | s | **time step** — 시간을 잘게 자른 한 칸. 루프 한 번 = dt초 경과. 작을수록 정확하고 느림. 실제 펌웨어에선 타이머 주기(100 Hz → 10 ms) | 0.01 s (1초에 100번 계산) |
| k | `k` | — | 스텝 번호 (0, 1, 2, …) | 충돌 시 1282 |
| t | `t` | s | 현재 시각 = `k * dt`. 출력할 **timestamp** | 12.82 s |
| 충돌 거리 | `COLLIDE_DIST` | m | 두 중심 거리가 이 값 이하면 충돌 (면접에서 정할 값) | 1.0 |
| 최대 시간 | `T_MAX` | s | 이때까지 안 만나면 "충돌 없음" — `while(1)` 무한 루프 방지 | 60 |
| 회전 반경 | (계산값) | m | 원의 반지름 = `speed / omega`. **Solution 1은 저장하지 않음**, Solution 2만 사용 | A 4, B 3 |
| 한 바퀴 시간 | (계산값) | s | `2π / omega`의 절댓값 | A 12.57, B 15.71 |

**한 스텝에서 일어나는 일 (Solution 1의 전부)**
1. 이번 스텝에 갈 거리: `dist = speed × dt`
2. 그 거리를 heading 방향으로 나눠 더하기: `x += dist × cos(heading)`, `y += dist × sin(heading)`
3. heading 돌리기: `heading += omega × dt`
4. `t = k × dt`에서 두 차 거리 확인 → `COLLIDE_DIST` 이하면 `t` 출력

`cos`·`sin`은 **라디안**을 받으므로 degree는 `× π / 180`으로 바꿉니다(`DEG()` 매크로).

## 1. Problem

Two cars drive on a flat 2D plane. Each car's state is:

- `x, y` — position in meters
- `heading` — direction of travel in radians (`0` = +x axis, counter-clockwise positive)
- `speed` — m/s along the heading
- `omega` — rotational speed in rad/s (`> 0` turns left / CCW, `< 0` turns right / CW)

From these parameters you can always compute the car's **next position**. Speed and omega are constant, so each car drives on a circle (a straight line when `omega == 0`). The cars **collide when their centers are within `COLLIDE_DIST` = 1.0 m**.

Report the **timestamp of the first collision** (seconds), or `-1` if there is none before `t_max`.

```c
#define COLLIDE_DIST 1.0
typedef struct { double x, y, heading, speed, omega; } Car;

/* Part A - Solution 1: parameters -> next position, step time */
Car    init_car(double x, double y, double heading, double speed, double omega);
void   update_car(Car *c, double dt);
bool   did_collide(const Car *a, const Car *b);
double sim_collision_time(Car a, Car b, double dt, double t_max);

/* Part B - Solution 1 refined: exact step + exact timestamp */
void   update_car_exact(Car *c, double dt);
double find_collision_time(Car a, Car b, double t_max, double tol);

/* Part C - Solution 2: describe each path by its circle */
typedef struct { double cx, cy, r, omega, phase0; } CircleCar;
bool   circle_from_car(const Car *c, CircleCar *out);          /* false if omega == 0 */
void   circle_pos(const CircleCar *k, double t, double *x, double *y);
double circle_collision_time(const CircleCar *a, const CircleCar *b, double t_max, double tol);
```

### Example

```
A: x=4, y=0, heading=90 deg, speed=2.0 m/s, omega=+0.5 rad/s   (turns left)
B: x=6, y=3, heading= 0 deg, speed=1.2 m/s, omega=-0.4 rad/s   (turns right)
COLLIDE_DIST = 1.0 m

Solution 1, dt = 0.5  : 12.50 s     (simple loop, Euler step)
Solution 1, dt = 0.01 : 12.82 s
Solution 1 refined    : 12.822241 s (exact step + bisection)
Solution 2 (circle)   : 12.822241 s (A: center (0,0) r=4,  B: center (6,0) r=3)
```

### Constraints

- `dt > 0`, `t_max > 0`, `tol > 0`. Return `0` if the cars already collide at `t = 0`.
- Speeds and omegas are constant for the whole run.
- No dynamic allocation. `<math.h>` is allowed.

### Clarifying questions to ask (first 3 minutes)

1. What counts as a collision — the centers within some distance? What distance? Does touching count?
2. Units: seconds, meters, radians? Heading measured from which axis, which direction is positive?
3. Are speed and rotational speed constant? Can omega be zero or negative?
4. Output: the first collision time only? How precise — is a fixed time step OK?
5. What if they never collide? Is there a time limit?
6. "Do you want me to step the state forward in time, or is a closed-form circle description fine?" ← 이번 면접에서 갈린 지점. **먼저 물어보면** 면접관이 원하는 쪽으로 바로 간다

## 2. Your workspace

`./run.sh R1`로 테스트. 테스트 12개(Part A 4 · Part B 5 · Part C 3). **Part A만 먼저 25분 안에** 끝내는 것이 목표이고, B·C는 남은 시간에.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 면접관의 상태 그대로: "파라미터로 다음 위치 계산"</summary>

`dt` 동안 차는 지금 heading 방향으로 `speed·dt`만큼 가고, heading은 `omega·dt`만큼 돕니다.

```c
c->x += c->speed * cos(c->heading) * dt;
c->y += c->speed * sin(c->heading) * dt;
c->heading += c->omega * dt;
```

반지름이나 원의 중심은 **필요 없습니다**. omega가 heading을 계속 돌리기 때문에 결과적으로 원을 그립니다.

</details>

<details>
<summary>Hint 2 — Don의 뼈대에서 바꿀 3곳</summary>

1. `update_car(Car, x, y)` → `update_car(Car *c, double dt)`: 다음 위치는 밖에서 넣는 게 아니라 **안에서 계산**. C는 값 전달이라 포인터가 필요
2. `while (1)` → `for (k = 0; k*dt <= t_max; k++)`: 안 만나면 끝나야 함
3. 시간 `t = k*dt`를 들고 다니다가 충돌 시 반환

</details>

<details>
<summary>Hint 3 — 정확한 timestamp (Part B)와 원 공식 (Part C)</summary>

- **Part B**: omega가 일정하면 dt가 커도 정확한 호 공식이 있습니다. `r = speed/omega`, `th1 = th0 + omega·dt` → `x += r(sin th1 − sin th0)`, `y += r(cos th0 − cos th1)`. 그다음 거친 스텝 `dt = 0.5·COLLIDE_DIST/(|vA|+|vB|)`로 가다가, 처음 충돌한 스텝 안에서 이분법
- **Part C**: 원의 중심은 차의 **왼쪽**으로 `speed/omega`만큼: `cx = x − (v/ω)·sin(heading)`, `cy = y + (v/ω)·cos(heading)`. 그러면 `pos(t) = center + r·(cos(φ0 + ωt), sin(φ0 + ωt))`

</details>

---

## 4. 풀이

### 4.1 모범답안 — Solution 1 (면접관 방식, main까지 전부)

CoderPad에 처음부터 이렇게 치면 됩니다. 상수 → struct → 3개 함수 → main 순서.

<div class="code" data-src="model1.c"></div>

**실행 결과** (`./run.sh R1 model1`)

```
collision at t = 12.82 s (step 1282)
  A at (3.969, 0.506)   B at (3.260, 1.206)   distance 0.997 m
```

**처음 몇 스텝 손으로 따라가기** (dt = 0.01)

| k | t | A (x, y) | A heading | B (x, y) | B heading | 거리 |
|---|---|---|---|---|---|---|
| 0 | 0.00 | (4.0000, 0.0000) | 1.5708 (90°) | (6.0000, 3.0000) | 0.0000 (0°) | 3.6056 |
| 1 | 0.01 | (4.0000, 0.0200) | 1.5758 | (6.0120, 3.0000) | −0.0040 | 3.5956 |
| 2 | 0.02 | (3.9999, 0.0400) | 1.5808 | (6.0240, 3.0000) | −0.0080 | 3.5858 |
| … | | | | | | |
| 1280 | 12.80 | | | | | 1.0196 |
| 1281 | 12.81 | | | | | 1.0084 |
| **1282** | **12.82** | (3.969, 0.506) | | (3.260, 1.206) | | **0.9970 ≤ 1.0 → 출력** |

- k=0→1: A는 위(90°)를 보고 있으므로 `x += 0.02·cos 90° = 0`, `y += 0.02·sin 90° = 0.02` → (4, 0.02). 그다음 heading이 `0.5 × 0.01 = 0.005` rad 늘어 1.5758
- B는 오른쪽(0°)을 보고 있으므로 x만 `1.2 × 0.01 = 0.012` 늘고, heading은 `−0.4 × 0.01 = −0.004` → 오른쪽으로 조금씩 꺾임

**main을 짤 때 체크할 것**
- `update_car`는 **포인터**(`Car *`)로 받는다 → main의 `a`, `b`가 실제로 바뀐다
- 루프 순서: **검사 → 이동**. 그래야 `t = 0`에 이미 겹친 경우도 0을 출력
- 시간은 `t = k * DT` (0.01을 계속 더하면 오차가 쌓임)
- `T_MAX`로 반드시 끝나게

### 4.2 모범답안 — Solution 2 (반지름 방식, main까지 전부)

같은 입력(x, y, heading, speed, omega)을 **한 번만** 원으로 바꾸고, 각 시각의 위치를 공식으로 바로 구합니다.

**추가 용어**
| 이름 | 코드 | 뜻 | 예제 A / B |
|---|---|---|---|
| 반지름 | `r` | `speed / omega`의 절댓값 | 4 / 3 |
| 중심 | `cx, cy` | omega > 0이면 차의 **왼쪽**(heading + 90°), < 0이면 **오른쪽**으로 r만큼. 식: `cx = x − (v/ω)·sin θ`, `cy = y + (v/ω)·cos θ` (v/ω의 부호가 왼쪽/오른쪽을 정함) | (0, 0) / (6, 0) |
| 위상 | `phase` | **중심에서 본** 차의 각도(heading과 다름!). heading과 같은 속도 ω로 변함 | — |
| 시작 위상 | `phase0` | t = 0일 때의 위상 = `atan2(y − cy, x − cx)` | 0 / π/2 |
| 위치 공식 | `position_at` | `x(t) = cx + r·cos(phase0 + ω·t)`, `y(t) = cy + r·sin(phase0 + ω·t)` | — |

<div class="code" data-src="model2.c"></div>

**실행 결과** (`./run.sh R1 model2`)

```
A: center (0.00, 0.00) r = 4.00   B: center (6.00, 0.00) r = 3.00
collision at t = 12.83 s (step 1283)
  A at (3.965, 0.526)   B at (3.260, 1.222)   distance 0.991 m
```

**검산**
| t | A (x, y) | B (x, y) | 거리 |
|---|---|---|---|
| 0.00 | (4.0000, 0.0000) | (6.0000, 3.0000) | 3.6056 — Solution 1과 같음 |
| 0.01 | (4.0000, 0.0200) | (6.0120, 3.0000) | 3.5956 |
| 12.82 | (3.9679, 0.5059) | (3.2554, 1.2112) | 1.0026 — 아직 충돌 아님 |
| **12.83** | (3.9653, 0.5257) | (3.2603, 1.2222) | **0.9910 → 출력** |

**두 모범답안의 답이 12.82 vs 12.83으로 다른 이유**
- 정확한 충돌 시각은 **12.822241 s**(§4.6). 0.01 간격으로 검사하면 정답은 그 직후 칸인 **12.83**
- Solution 2는 위치가 공식이라 정확 → 12.83
- Solution 1은 매 스텝 접선으로 직진하는 오차(Euler)가 쌓여 실제보다 살짝 앞서 충돌로 판정 → 12.82. dt를 줄이거나(§4.5) 정확한 호 공식을 쓰면(§4.6) 사라짐
- 면접에서 이 차이를 **말로 설명할 수 있으면** 가점

### 4.3 두 가지 모델 — 같은 운동, 다른 표현

<div class="chart" data-src="chart4.svg" data-caption="Solution 1(면접관): 현재 상태 → 다음 점을 반복 계산. Solution 2(Don의 첫 제안): 원의 중심·반지름·위상으로 임의 시각의 위치를 바로 계산"></div>

| | **Solution 1 — 상태를 한 스텝씩 진행** (면접관) | **Solution 2 — 원으로 표현** (Don) |
|---|---|---|
| 파라미터 | x, y, heading, speed, omega | 중심 (cx, cy), 반지름 r, 각속도 ω, 시작 위상 φ0 |
| 위치 계산 | `update(dt)`를 반복 → 다음 점 | `pos(t)`를 공식으로 바로 |
| 반지름 | 필요 없음(결과적으로 r = v/ω가 됨) | 핵심 파라미터 |
| omega = 0 (직선) | 그대로 동작 | **원이 없음** → 따로 처리해야 함 |
| 속도·조향이 바뀜 | 매 스텝 새 값을 넣으면 그대로 동작 | 원이 바뀔 때마다 다시 계산 |
| 오차 | Euler는 dt만큼 오차(§4.3) | 공식이라 누적 오차 없음 |
| 실제 로봇 코드와 | **같은 구조**(센서·제어 입력이 매 주기 바뀜) | 계획·검증용 |

**왜 면접관은 Solution 1을 원했나** [추정]: 실제 차량은 조향과 속도가 매 주기 바뀝니다. "현재 상태 + 입력 → 다음 상태"를 반복하는 구조가 펌웨어·시뮬레이터의 기본 루프이고, 직선(ω = 0)이나 가속으로 문제를 바꿔도 코드가 그대로 갑니다. Solution 2는 "이 문제처럼 ω가 일정할 때만" 쓸 수 있는 지름길입니다.

**면접에서 이렇게 말하면 좋았다**: "I could describe each path by its circle — center and radius — but stepping the state with heading, speed and omega is more general: it handles straight lines and changing inputs. I'll do the step version, and mention the closed form as an optimization."

### 4.4 예제 그림

<div class="chart" data-src="chart1.svg" data-caption="예제 궤적: A(파랑, 반시계)와 B(보라, 시계). 반지름 4 m·3 m는 Solution 1에선 v/ω로 '결과적으로' 생기고, Solution 2에선 직접 파라미터. 빨간 원 두 개 = t = 12.822 s 접촉 순간"></div>

<div class="chart" data-src="chart2.svg" data-caption="두 중심 사이 거리 d(t). 빨간 점선 아래가 충돌. 오른쪽: 거친 스텝 [12.8125, 12.96875] 안에서 28번 반으로 나눠 12.822241 s"></div>

### 4.5 Solution 1 — 손으로 따라가기 (dt = 0.5, Euler)

| k | t | A (x, y) | A heading | B (x, y) | B heading | 거리 |
|---|---|---|---|---|---|---|
| 0 | 0.0 | (4.0000, 0.0000) | 1.5708 | (6.0000, 3.0000) | 0.0000 | 3.6056 |
| 1 | 0.5 | (4.0000, 1.0000) | 1.8208 | (6.6000, 3.0000) | −0.2000 | 3.2802 |
| 2 | 1.0 | (3.7526, 1.9689) | 2.0708 | (7.1880, 2.8808) | −0.4000 | 3.5544 |
| 3 | 1.5 | (3.2732, 2.8465) | 2.3208 | (7.7407, 2.6471) | −0.6000 | 4.4720 |
| … | | | | | | |
| 25 | 12.5 | (3.9812, −0.1317) | | (3.3477, 0.5705) | | **0.9457 ≤ 1** → 반환 12.50 |

- k=1에서 A는 heading 90°(위쪽)로 2·0.5 = 1 m 가서 (4, 1). 그다음에 heading이 0.25 rad 돕니다. **이것이 "파라미터로 다음 위치 구하기"** 입니다
- 같은 t = 12.5에서 정확한 위치로는 거리가 1.3163이라 아직 충돌이 아닙니다. Euler 오차가 쌓여서 **0.32 s 일찍** 충돌로 판정했습니다. dt = 0.01이면 12.82, dt = 0.001이면 12.822

<div class="chart" data-src="chart3.svg" data-caption="같은 차 A를 dt = 0.5 s Euler로 한 바퀴(주황) vs 정확한 원(초록). 접선 방향으로 가기 때문에 최대 1.00 m 어긋남 → 충돌 시각이 달라지는 이유"></div>

### 4.6 Solution 1 refined — 정확한 스텝 + 이분법

| 단계 | 값 |
|---|---|
| 거친 스텝 | 0.5 × 1.0 / (2.0 + 1.2) = **0.15625 s** (한 스텝에 거리가 0.5 m보다 많이 줄 수 없음 → 뚫고 지나가지 않음) |
| 첫 충돌 스텝 | 83번째, [12.8125, 12.96875] |
| 이분법 | 28번 → 폭 < 1e-9 s |
| 결과 | **12.822241 s** |

### 4.7 Solution 2 — 원으로 바꾸기

| 차 | 상태 | 중심 = (x − (v/ω)·sinθ, y + (v/ω)·cosθ) | r = \|v/ω\| | φ0 |
|---|---|---|---|---|
| A | (4, 0), 90°, 2.0, +0.5 | (4 − 4·1, 0 + 4·0) = **(0, 0)** | 4 | 0 |
| B | (6, 3), 0°, 1.2, −0.4 | (6 − (−3)·0, 3 + (−3)·1) = **(6, 0)** | 3 | π/2 |

그다음 `pos(t)`로 거리를 바로 계산해 같은 거친 스텝 + 이분법 → **12.822241 s**. Solution 1 refined와 위치가 매 시각 일치합니다(테스트 11).

### 4.8 테스트 포함 전체 정답 (Part A·B·C, `solution.c`)

<details>
<summary>💡 정답 코드 펼치기 (35분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `sim_collision_time`은 시간을 `t += dt`로 더하지 않고 `k * dt`로 만듭니다. 0.01을 1000번 더하면 부동소수점 오차가 쌓이기 때문입니다.
- `did_collide`는 `dx² + dy² <= D²`로 **제곱끼리 비교**합니다(`sqrt` 없음).
- 시뮬레이션 함수는 `Car`를 **값으로** 받습니다. 호출자의 상태를 바꾸지 않고, 복사본으로 "미리 가 보기"가 쉽습니다.
- `update_car_exact`의 `r = speed/omega`는 **계산해서 나온 값**입니다. 파라미터는 여전히 면접관의 다섯 개뿐입니다.
- `circle_from_car`는 ω = 0이면 `false`를 반환합니다. 직선은 원이 없기 때문에 Solution 2만으로는 처리할 수 없습니다.

### 4.9 복잡도

- Solution 1 (dt 고정): `O(t_max / dt)`. 정확도를 10배 올리려면 10배 느려짐
- Solution 1 refined / Solution 2: 거친 스텝 `O(t_max / dt_coarse)` + 이분법 `O(log(dt/tol))`. 예제는 83스텝 + 28번
- 메모리 `O(1)`

---

## 5. 임베디드 관점으로 말할 것

- **Solution 1 = 실제 펌웨어 루프**: 타이머 ISR이나 RTOS 주기 태스크가 `dt`마다 `update(state, inputs, dt)`를 부릅니다. `dt`는 상수가 아니라 **측정한 경과 시간**을 씁니다(지터 대응). 면접관이 이 구조를 원한 이유로 보입니다.
- **수치 오차 = 안전 문제**: Euler 오차가 차 크기만큼 커지면 충돌 판정이 틀립니다. "dt를 줄인다 / 정확한 적분 / 오차만큼 안전 여유를 더한다" 중 무엇을 고를지 말할 수 있어야 합니다.
- **FPU 사정**: Cortex-M4F/M7은 single precision FPU만 있는 경우가 많아 `double`이 느립니다. MCU에서는 `float` + `sinf/cosf`, 또는 고정소수점 + 룩업 테이블.
- **실제 차량에서는 예측을 계속 갱신**: 매 주기 센서로 상태를 다시 받고 "앞으로 T초 안에 충돌하나?"(time-to-collision)를 다시 계산합니다. 면접관 Jason은 반자율 경로 추종 경력이 있어 이 이야기를 반길 가능성이 큽니다 [추정].

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why did you want a radius parameter? Do you need it? | 필요 없습니다. heading을 omega로 계속 돌리면 결과적으로 반지름 v/ω의 원이 됩니다. 반지름·중심 표현(Solution 2)은 ω가 일정할 때 임의 시각 위치를 바로 구하는 **최적화**이고, 직선이나 입력 변화에는 상태 진행(Solution 1)이 더 일반적입니다. |
| Your answer depends on `dt`. Why? | Euler는 dt 동안 접선으로 직진하기 때문입니다. dt = 0.5 → 12.50, 0.01 → 12.82, 정확한 값 12.822241. 정확한 호 공식을 쓰거나 dt를 줄입니다. |
| How do you avoid missing a collision? | 한 스텝에 거리가 최대 `(|vA|+|vB|)·dt` 변하므로, 이를 충돌 거리의 절반 이하로 잡습니다. dt가 크면 빠른 차가 서로 뚫고 지나갑니다(테스트 8: dt = 1 → −1, 정답 0.45). |
| What if speed or steering change over time? | Solution 1이 그대로 갑니다. 매 스텝 새 speed/omega를 넣으면 됩니다. Solution 2는 원이 바뀔 때마다 다시 계산해야 합니다. |
| Can you solve it in closed form? | 직선끼리는 상대 위치가 `p + v·t`라 2차 방정식(closest point of approach). 원끼리는 sin/cos가 섞여 일반적인 닫힌 해가 없어 수치적으로 근을 찾습니다. |
| What about N cars? | 매 스텝 모든 쌍 `O(N²)`. 격자로 공간을 나눠 이웃 칸만 비교하면 평균 `O(N)`. |
| How would you test it? | 손으로 답이 나오는 경우(정면 직선 충돌 4.5 s, 평행 = −1, 처음부터 겹침 = 0), 아주 작은 dt와 비교, 두 솔루션이 같은 답을 내는지, 빠른 차(tunneling). |
| In C++? | 아래 코드. `struct Car`에 `update()`·`collides()` 멤버, 결과는 `std::optional<double>`. |

면접관이 C++로 시작했으므로, **Solution 1을 C++로** 쓰면 이렇습니다.

```cpp
#include <cmath>
#include <optional>

constexpr double kCollideDist = 1.0;

struct Car {
    double x, y, heading, speed, omega;

    void update(double dt) {                 // parameters -> next position
        x += speed * std::cos(heading) * dt;
        y += speed * std::sin(heading) * dt;
        heading += omega * dt;
    }
    bool collides(const Car& o) const {
        double dx = x - o.x, dy = y - o.y;
        return dx * dx + dy * dy <= kCollideDist * kCollideDist;
    }
};

std::optional<double> first_collision(Car a, Car b, double dt, double t_max) {
    for (long k = 0; k * dt <= t_max; ++k) {
        if (a.collides(b)) return k * dt;
        a.update(dt);
        b.update(dt);
    }
    return std::nullopt;
}
```

## 7. 자주 하는 실수

- **반지름부터 정하려다 시간 소모** — 면접관이 준 파라미터(x, y, heading, speed, omega)만으로 다음 위치가 나온다. 이번 면접에서 갈린 지점
- `update_car(Car, x, y)`처럼 **값으로 받은 구조체를 수정** → 호출한 쪽은 그대로. `Car *`로 받기
- `while (1)`에 **종료 조건 없음** → 안 만나면 무한 루프
- 시간을 `t += dt`로 누적 → 오차. `t = k * dt`
- **각도 단위 혼동**(degree를 `cos`에 그대로)
- heading을 먼저 돌리고 이동 vs 이동 후 회전 — 둘 다 Euler지만 답이 조금 다르다. **어느 쪽인지 말하고** 일관되게
- Solution 2에서 ω = 0을 처리하지 않아 `speed / omega`로 0 나누기
