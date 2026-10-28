# R2 · Wheel Odometry and Waypoint Following

> **ID**: R2 · **Priority**: P0 · **Pattern**: encoder delta with 16-bit wrap + kinematic integration + simple controller loop · **Time**: 35 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh R2` / `./run.sh R2 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 바퀴 두 개짜리 로봇의 encoder 값으로 위치(x, y, θ)를 추적하고(A·B), 그 위치로 waypoint들을 차례로 찾아가는 시뮬레이션을 돌린다(C).
- **상황**: differential-drive 로봇(구동 바퀴 2개 + 보조 바퀴 1개). 바퀴마다 quadrature encoder가 있고 16-bit 하드웨어 카운터로 읽는다. 앞으로 굴면 증가, 뒤로 굴면 감소, 양방향으로 wrap(65535 + 1 → 0, 0 − 1 → 65535).
- **배경 개념**
  - **encoder tick**: 바퀴가 조금 돌 때마다 1씩 세는 단위. 여기선 1바퀴 = 2048 tick(x4 decode 후). 1바퀴에 가는 거리는 `2π · 바퀴 반지름`이라 tick당 거리 = `2π · wheel_radius_m / ticks_per_rev`.
  - **track width**: 왼쪽·오른쪽 바퀴 사이 거리. 두 바퀴가 간 거리가 다르면 그 차이 ÷ track width만큼 로봇이 돈다: `dθ = (dr − dl) / track_width_m`. 로봇 중심이 간 거리는 평균 `d = (dl + dr) / 2`.
  - **dead reckoning / odometry**: 바깥 센서 없이 바퀴가 간 거리만 누적해서 현재 pose(x, y, θ)를 추정하는 것.
  - **midpoint integration**: 한 스텝 동안 방향이 바뀌니까 시작 방향 θ 대신 중간 방향 `θ + dθ/2`로 이동시키는 방식. 그냥 `cos(θ)`(Euler)는 테스트 정밀도를 못 맞춘다.
- **입력**: Part A는 이전/현재 16-bit 카운터 값. Part B는 pose, 양쪽 바퀴 tick 변화량, `Robot`(`ticks_per_rev`, `wheel_radius_m`, `track_width_m`, `max_omega_rad_s`). Part C는 시작 pose, waypoint 배열 `wps[n]`, 속도 `v`, 조향 gain `k_heading`, `dt`, `reach_tol`, `t_max`.
- **출력**: A는 부호 있는 tick 수. B는 `*p` 갱신. C는 모든 waypoint 도착이면 `true` + `*t_done`, 시간 초과면 `false`.
- **규칙**
  - Part A: wrap을 넘어도 정확해야 함. 읽는 사이 이동은 32768 tick 미만이라고 가정.
  - Part B: `wrap_angle()`은 각도를 **(−π, π]** 로. `odom_update()` 후 θ도 이 범위 유지. midpoint 또는 exact arc 사용.
  - Part C: 매 스텝 `t = k · dt`. ① 현재 waypoint까지 거리 `<= reach_tol`이면 다음 것으로(여러 개 연속 가능) ② 다 도착하면 `*t_done = t`, `true` ③ `t >= t_max`면 `false`(`*t_done` 안 씀) ④ `ω = k_heading · wrap(waypoint 방향 − θ)`, `±max_omega` 로 clamp ⑤ 거리 `v·dt`, 회전 `ω·dt`만큼 이동(Part B integrator 재사용).
  - `n == 0`이면 바로 `true`, `*t_done = 0`. `malloc` 금지.
- **예제 풀어 읽기**: 1바퀴 = 2π·0.1 = 0.628319 m. `enc_delta(65530, 4) = +10`, `enc_delta(3, 65533) = -6`. 양쪽 2048 tick → (0.628319, 0, 0) 직진. (−1024, 1024) → 제자리 회전, θ = 1.256637. (1, 3) tick을 1280번 → 반지름 0.5 m 사분원, (0.5, 0.5, π/2). θ = 3.0에서 1.256637 더 돌면 −2.026548로 wrap. 정사각형 (2,0) (2,2) (0,2) (0,0) 따라가기 → 16.09 s.
- **놓치기 쉬운 것**: +32767은 그대로, +32769는 −32767로 해석됨; `wrap_angle(-π)`는 `π`(경계가 (−π, π]); 65000 → 1512는 +2048; waypoint 0개면 pose 그대로; 시작부터 `reach_tol` 안이면 `t = 0`; 시간 초과 시 `*t_done` 건드리지 않기; 170°에서 −168.7° 방향 목표는 왼쪽으로 21.3° 도는 게 최단(heading 오차를 wrap 안 하면 오른쪽으로 길게 돎).
- **구현할 함수**
  - Part A: `enc_delta(prev, now)`
  - Part B: `wrap_angle(a)`, `odom_update(p, dl_ticks, dr_ticks, r)`
  - Part C: `follow_waypoints(p, wps, n, r, v, k_heading, dt, reach_tol, t_max, t_done)`

## 1. Problem

A small differential-drive ground robot (two driven wheels, one caster) has a quadrature encoder on each wheel. Each encoder is read from a **16-bit hardware counter** that counts up when the wheel rolls forward and down when it rolls backward, and **wraps in both directions** (65535 + 1 → 0, 0 − 1 → 65535). Build the dead-reckoning layer and a tiny path follower.

**Part A — encoder delta**

- `enc_delta(prev, now)` returns the signed number of ticks moved between two counter reads.
- It must be correct across the wrap. You may assume the wheel moved fewer than 32768 ticks between reads.

**Part B — odometry**

- `wrap_angle(a)` maps any angle (radians) to the range **(−π, π]**.
- `odom_update(p, dl_ticks, dr_ticks, r)` updates the pose from the two wheel deltas:
  - meters per tick = `2π · wheel_radius_m / ticks_per_rev`
  - `d = (dl + dr) / 2` (distance of the robot center), `dθ = (dr − dl) / track_width_m`
  - Use **midpoint** integration (`x += d·cos(θ + dθ/2)`, same for `y` with `sin`) or the **exact arc**. Plain Euler (`cos(θ)`) is not accurate enough for the tests.
  - Keep `θ` in (−π, π].

**Part C — waypoint following (simulation)**

`follow_waypoints()` runs a fixed-step simulation of a unicycle model (constant forward speed `v`, turn rate `ω`). At step `k` the time is `t = k · dt`. Every step:

1. While the current waypoint is within `reach_tol` (distance `<=`), advance to the next one.
2. If all waypoints are reached, write `t` to `*t_done` and return `true`.
3. If `t >= t_max`, return `false` (do not write `*t_done`).
4. Steer: `ω = k_heading · wrap(bearing_to_waypoint − θ)`, clamped to `±r->max_omega_rad_s`.
5. Move one step: distance `v·dt`, turn `ω·dt` (reuse your Part B integrator).

`n == 0` means there is nothing to do: return `true` with `*t_done = 0`.

```c
typedef struct { double x, y, theta; } Pose;   /* m, m, rad in (-pi, pi] */
typedef struct { double x, y; } Point;
typedef struct {
    uint32_t ticks_per_rev;      /* after x4 quadrature decode */
    double   wheel_radius_m;
    double   track_width_m;
    double   max_omega_rad_s;    /* used by follow_waypoints */
} Robot;

int32_t enc_delta(uint16_t prev, uint16_t now);                         /* Part A */
double  wrap_angle(double a);                                           /* Part B */
void    odom_update(Pose *p, int32_t dl_ticks, int32_t dr_ticks, const Robot *r);
bool    follow_waypoints(Pose *p, const Point *wps, size_t n, const Robot *r,   /* Part C */
                         double v, double k_heading, double dt, double reach_tol,
                         double t_max, double *t_done);
```

### Example

```
Robot: 2048 ticks/rev, wheel radius 0.10 m, track 0.50 m, max omega 2.0 rad/s
       -> 1 revolution = 2*pi*0.1 = 0.628319 m

enc_delta(65530, 4)  = +10        enc_delta(3, 65533) = -6

odom_update({0,0,0}, 2048, 2048)  -> (0.628319, 0, 0)             straight
odom_update({1,2,0}, -1024, 1024) -> (1, 2, 1.256637)             spin in place
1280 x odom_update(.., 1, 3)      -> (0.5, 0.5, pi/2)             quarter circle, R = 0.5 m
odom_update({0,0,3.0}, -1024, 1024) -> theta = -2.026548          wrapped

follow_waypoints from (0,0,0) through (2,0) (2,2) (0,2) (0,0),
  v = 0.5, k_heading = 3, dt = 0.01, reach_tol = 0.1, t_max = 60
  -> true, t_done = 16.09 s
```

### Constraints

- `double` for the pose, integer ticks for the encoders. `<math.h>` is available.
- No `malloc`. `follow_waypoints` runs at most `t_max / dt` steps of O(1) work.
- `dt > 0`. `wps` may be `NULL` only when `n == 0`.

### Clarifying questions to ask (first 3 minutes)

1. How often are the encoders read, and what is the top wheel speed? (Is `|delta| < 32768` guaranteed?)
2. Is `θ = 0` along +x, counter-clockwise positive? Which wheel is "left"?
3. Should `θ` be wrapped to (−π, π] or [0, 2π)?
4. Midpoint or exact-arc integration? Do you want me to compare with Euler?
5. In the follower: is "reached" `<=` or `<` `reach_tol`? Is speed constant, or should it slow down near the waypoint?
6. What should happen when there are zero waypoints, or the robot already sits on one?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh R2`로 바로 테스트하세요. 테스트 14묶음(A 3 · B 6 · C 5)이 들어 있습니다. 면접에서는 Part A → B → C 순서로 하나씩 끝내고 넘어가는 것이 좋습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — Part A는 타이머 랩어라운드와 똑같다</summary>

`now - prev`를 **16비트 unsigned로 먼저 자르고**(`(uint16_t)`), 그 결과를 **`int16_t`로 다시 해석**하면 됩니다. 0x8000 미만이면 앞으로, 이상이면 뒤로 간 것입니다. `uint16_t`끼리 빼면 `int`로 승격되므로 캐스트 없이 `return now - prev;`라고 쓰면 `4 - 65530 = -65526`이 그대로 나옵니다.

</details>

<details>
<summary>Hint 2 — Part B: 이동과 회전을 한 함수로</summary>

`static void advance(Pose *p, double d, double dth)` 하나를 만들고 Part B와 Part C가 같이 쓰게 하세요. 안에서는 **이동 방향을 `θ + dth/2`(구간 중간 헤딩)**로 잡고, 마지막에 `θ = wrap_angle(θ + dth)`. `wrap_angle`은 `fmod(a, 2π)` 후 `> π`면 `−2π`, `<= −π`면 `+2π`.

</details>

<details>
<summary>Hint 3 — Part C: 각도 오차는 반드시 wrap</summary>

`bearing - θ`를 그대로 쓰면 170°에서 −169°를 볼 때 오차가 −339°가 되어 **반대 방향으로 거의 한 바퀴** 돕니다. `wrap_angle(bearing - θ)`면 +21°. 루프 순서는 "도달 검사 → 끝났나 → 시간 초과 → 조향 → 한 스텝 이동"이고, 시간은 `t += dt` 누적 대신 `t = k * dt`로 계산하면 오차가 쌓이지 않습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 엔코더 랩어라운드

16비트 카운터는 원형입니다. "두 읽기 사이의 차이"를 **모듈로 2¹⁶으로 계산한 뒤, 가까운 쪽(±32767 안)으로 해석**하면 방향까지 맞게 나옵니다. HAL 타이머 tick 비교(`(int32_t)(now - deadline) >= 0`)와 같은 원리입니다.

<div class="chart" data-src="chart1.svg" data-caption="위: 65530 → 4는 랩을 넘어 +10. 아래: 3 → 65533은 거꾸로 랩을 넘어 −6. 캐스트 없이 int로 빼면 −65526이 나와 로봇이 순간이동합니다"></div>

조건은 **두 읽기 사이 실제 이동이 32768 tick 미만**이라는 것뿐입니다. 이 로봇(2048 tick/rev)이라면 한 번 읽는 사이에 바퀴가 16바퀴 미만 돌면 되므로, 100 Hz로 읽으면 1600 rev/s까지 안전합니다. 테스트 3번이 이 한계를 보여 줍니다: `enc_delta(0, 32769)`는 실제로 +32769였더라도 `−32767`로 보입니다.

### 4.2 차동 구동 기하 — 왜 `d`와 `dθ`가 그렇게 나오나

두 바퀴는 항상 같은 점(**ICR**, instantaneous center of rotation)을 중심으로 원호를 그립니다. 바깥 바퀴가 더 많이 굴러간 만큼 몸체가 돕니다.

<div class="chart" data-src="chart3.svg" data-caption="왼쪽 바퀴 호 dl, 오른쪽 바퀴 호 dr, 중심 호 d. 회전각 dθ = (dr − dl) / track. dl = dr이면 직진(R = ∞), dl = −dr이면 제자리 회전(d = 0)"></div>

**적분 방법별 오차** (테스트 6번과 같은 반지름 0.5 m 사분원, 끝점 (0.5, 0.5)과의 거리 — 이 문서의 `solution.c` 함수들로 계산)

| 업데이트 방식 | 1280 스텝 × (1, 3) tick | 128 스텝 × (10, 30) tick | 1 스텝 × (1280, 3840) tick |
|---|---|---|---|
| Euler `cos(θ)` | 4.3e-4 m ✗ | 4.3e-3 m | 0.576 m |
| Midpoint `cos(θ + dθ/2)` | 4.4e-8 m ✓ | 4.4e-6 m | 0.078 m |
| Exact arc (chord = d·sin(h)/h) | 9.3e-15 m ✓ | 6.2e-16 m | 1.1e-16 m |

- Euler 오차는 스텝당 O(d·dθ), midpoint는 O(d·dθ²)입니다. 테스트 허용 오차 1e-6 m는 midpoint면 통과, Euler면 실패하도록 잡았습니다.
- 정답 코드는 **exact arc**를 씁니다. midpoint 코드에서 이동 거리 `d`만 현(chord) 길이 `d · sin(h)/h` (`h = dθ/2`)로 바꾸면 되므로 한 줄 차이입니다. 두 바퀴 속도가 한 주기 안에서 일정하다면 스텝 크기와 무관하게 정확합니다. `h`가 0에 가까우면 `sin(h)/h → 1`이므로 직진은 분기로 처리합니다.

### 4.3 손으로 따라가기 (trace)

**Part A + B** — 오른쪽 바퀴가 더 빨라서 왼쪽으로 살짝 도는 3주기. 두 엔코더 모두 랩을 넘습니다. (`d = 0.5 · (dl + dr) · 0.000306796`, `dθ = (dr − dl) · 0.000306796 / 0.5`)

| k | L prev → now | dl | R prev → now | dr | d (m) | dθ (rad) | pose 후 (x, y, θ) |
|---|---|---|---|---|---|---|---|
| 1 | 65400 → 65500 | 100 | 65400 → 65530 | 130 | 0.035282 | 0.018408 | (0.035280, 0.000325, 0.018408) |
| 2 | 65500 → **64** | 100 | 65530 → **124** | 130 | 0.035282 | 0.018408 | (0.070547, 0.001299, 0.036816) |
| 3 | 64 → 164 | 100 | 124 → 254 | 130 | 0.035282 | 0.018408 | (0.105791, 0.002922, 0.055223) |

k = 2에서 두 카운터 모두 0을 지나지만 delta는 그대로 100, 130입니다.

**Part C** — 테스트 14번(헤딩 오차가 ±π를 넘는 경우)의 처음 세 스텝. 출발 θ = 170°, 목표 (−1, −0.2).

| k | t (s) | θ | bearing | raw 오차 | wrap 후 | ω = 3·err (clamp ±2) |
|---|---|---|---|---|---|---|
| 0 | 0.00 | 2.967060 (170.00°) | −2.944197 (−168.69°) | −5.911257 (−338.69°) | **0.371928 (+21.31°)** | 1.1158 |
| 1 | 0.01 | 2.978218 (170.64°) | −2.942433 (−168.59°) | −5.920650 (−339.23°) | 0.362535 (+20.77°) | 1.0876 |
| 2 | 0.02 | 2.989094 (171.26°) | −2.940703 (−168.49°) | −5.929797 (−339.75°) | 0.353389 (+20.25°) | 1.0602 |

wrap을 빼면 k = 0에서 ω = 3 · (−5.91) → −2로 clamp되어 θ가 2.947060으로 **줄어듭니다**(오른쪽, 먼 길). 테스트 14번의 `p.theta > th0`가 이것을 잡습니다.

### 4.4 시뮬레이션 결과

<div class="chart" data-src="chart2.svg" data-caption="① 정사각형 경로: 각 웨이포인트 도달 시각 3.81 · 7.93 · 12.01 · 16.09 s. 모서리에서 최소 회전 반경(0.5 / 2 = 0.25 m) 때문에 바깥으로 최대 0.18 m 밀립니다. ② ±π 문제: wrap하면 1.86 s, wrap하지 않으면 한 바퀴 돌아 5.00 s"></div>

| 웨이포인트 | 도달 시각 | 도달 순간 pose (x, y, θ) |
|---|---|---|
| wp0 (2, 0) | 3.81 s | (1.9050, 0.0000, 0.0000) |
| wp1 (2, 2) | 7.93 s | (2.0123, 1.9045, 1.6987) |
| wp2 (0, 2) | 12.01 s | (0.0955, 2.0104, −3.0334) |
| wp3 (0, 0) | 16.09 s | (−0.0110, 0.0983, −1.4596) |

- wp0이 3.80 s가 아니라 3.81 s인 이유: 0.005 m를 380번 더하면 부동소수점으로 1.8999999…가 되어 거리가 0.1을 아주 조금 넘습니다. 그래서 시간 검사는 테스트에서 ±0.05 s 허용 오차를 둡니다.
- wp2에서 θ = −3.0334는 실제로는 약 186°입니다. θ가 +π 근처에서 −π 쪽으로 넘어간 상태이고, 다음 목표(0,0)의 bearing이 약 −90°라 wrap된 오차로 정상적으로 왼쪽으로 돕니다.
- `t_max = 5`로 같은 경로를 돌리면 wp0만 지나고 `false`를 반환합니다(테스트 13).

### 4.5 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (35분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `(int16_t)(uint16_t)(now - prev)`: 승격된 `int` 뺄셈 결과를 16비트로 자르고 부호 있게 해석합니다. `uint16_t` → `int16_t` 변환은 범위를 넘으면 C 표준상 implementation-defined지만 모든 2의 보수 컴파일러(gcc/clang/armcc)에서 기대대로 동작합니다. 완전히 이식성 있게 쓰려면 `uint16_t u = now - prev; return u < 0x8000u ? (int32_t)u : (int32_t)u - 65536;`.
- `advance()` 하나를 Part B(엔코더)와 Part C(시뮬레이션)가 공유합니다. 면접에서 "같은 기구학 모델을 센서 쪽과 시뮬레이션 쪽에서 둘 다 쓴다"고 말하면 설계가 깔끔해 보입니다.
- `wrap_angle`의 경계: `fmod` 결과는 (−2π, 2π)이고, `> π`와 `<= −π` 비교로 (−π, π]가 됩니다. `wrap_angle(-PI) == PI`가 테스트 8번입니다.
- 도달 검사는 `while`입니다. 웨이포인트 두 개가 겹쳐 있거나 시작 위치가 이미 첫 웨이포인트 위면 한 스텝에 여러 개를 넘깁니다(테스트 12).
- 시간은 `t = k * dt`. `t += dt`를 수천 번 하면 오차가 쌓여서 `t >= t_max` 판정이 한 스텝 어긋날 수 있습니다.

### 4.6 복잡도

- `enc_delta`, `wrap_angle`, `odom_update`: **O(1)**, 메모리 O(1).
- `follow_waypoints`: 스텝 수 **O(t_max / dt)**, 스텝마다 O(1)(`while`의 총 반복은 n 이하). 추가 메모리 O(1).

---

## 5. 임베디드 관점으로 말할 것

- **어디서 읽나 — ISR vs task**: 엔코더 카운터는 하드웨어(타이머의 encoder mode, 예: STM32 TIMx)가 세므로 CPU는 일정 주기로 **카운터만 읽으면** 됩니다. 1 kHz 타이머 ISR에서 두 카운터를 **같은 순간에** 읽어 delta를 큐에 넣고, 삼각함수가 들어간 `odom_update`는 task에서 돌리는 구성이 일반적입니다. 두 바퀴를 서로 다른 시각에 읽으면 회전 중 가짜 `dθ`가 생깁니다.
- **랩어라운드는 타이머와 같은 규칙**: "unsigned로 빼고 signed로 해석". 대신 **읽기 주기 × 최고 속도 < 32768 tick**을 보장해야 합니다. 이걸 숫자로 말할 수 있으면 좋습니다(4.1).
- **FPU 없는 MCU**: pose를 고정소수점(예: x, y는 0.1 mm 단위 `int32_t`, θ는 2¹⁶ = 한 바퀴인 `uint16_t` binary angle)으로 두면 **각도 wrap이 정수 오버플로로 공짜**가 됩니다. sin/cos는 256~1024 엔트리 룩업 테이블. Cortex-M4F 이상이면 `float`(double 아님)로 충분합니다.
- **오도메트리의 한계 — 바퀴 미끄러짐**: 휠 오도메트리는 위치 오차가 거리에 비례해 쌓이고, 특히 **헤딩 오차**가 이후 위치 오차를 키웁니다. 오프로드 UGV(Scout의 ATV)는 미끄러짐이 커서 **IMU yaw rate(자이로)를 헤딩에 퓨전**(complementary filter 또는 EKF)하고, GNSS/비주얼 오도메트리로 위치를 보정합니다. "dθ는 자이로, d는 엔코더"라는 간단한 조합만 말해도 충분히 실무적입니다.
- **Jason 연결점**: 면접관 Jason MacDonald는 Charles Machine Works에서 굴착 장비의 **semi-autonomous path following**을 했습니다. waypoint 추종, 헤딩 오차 wrap, 최소 회전 반경 때문에 생기는 오버슈트(chart2의 0.18 m), 미끄러짐 보정은 그가 직접 겪은 주제일 가능성이 큽니다. 실제 스크린 문제([R1](../R1_circular_car_collision/index.html))의 원 운동 업데이트와 이 문제의 exact arc가 같은 식이라는 점도 연결해서 말하면 좋습니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| The robot drives on gravel and the wheels slip. What do you do? | 엔코더는 "바퀴가 돈 양"이지 "몸체가 간 양"이 아닙니다. 헤딩은 **자이로 yaw rate 적분**을 주로 쓰고 엔코더 `dθ`는 보조로, complementary filter(`θ = α(θ + ω_gyro·dt) + (1−α)·θ_odom`) 또는 EKF로 합칩니다. 두 바퀴 차이 vs 자이로의 불일치가 크면 slip 감지 플래그를 올리고 그동안 엔코더 가중치를 낮춥니다. 절대 위치는 GNSS/비주얼 오도메트리로 보정합니다. |
| Euler vs midpoint vs exact arc — how big is the difference? | 4.2 표 그대로: 반지름 0.5 m 사분원을 1280 스텝으로 나누면 Euler 4.3e-4 m, midpoint 4.4e-8 m, exact 9.3e-15 m. Euler는 스텝당 O(d·dθ), midpoint는 O(d·dθ²). 한 스텝에 큰 회전이 들어오면(루프가 늦게 돈 경우) Euler 0.576 m vs midpoint 0.078 m vs exact 0이라 **exact arc가 dt 지터에 가장 강합니다**. |
| Why not pure pursuit instead of a heading P-controller? | Pure pursuit는 경로 위 lookahead 점을 향하는 원호의 곡률 `κ = 2·sin(α)/L_d`을 바로 ω = v·κ로 씁니다. 웨이포인트 사이 **선분을 따라가므로** 모서리 컷이 부드럽고 경로 이탈이 작습니다. 헤딩 P 제어는 점만 보므로 옆바람/미끄러짐에 cross-track 오차가 남습니다. 실차에서는 pure pursuit나 Stanley(cross-track + heading)가 표준이고, 이 문제의 P 제어는 30분 면접용 최소 버전입니다. |
| Why must the heading error be wrapped? | 각도는 원 위의 값이라 170°와 −169°는 21° 차이인데 단순 뺄셈은 −339°를 줍니다. wrap 없이 쓰면 먼 쪽으로 돌아서(테스트 14: 1.86 s → 5.00 s), 좁은 공간에서는 장애물에 부딪힐 수도 있습니다. 오차가 정확히 ±π일 때 좌/우가 매번 바뀌며 떨지 않도록 범위를 (−π, π]처럼 **한쪽만 닫는** 것도 의미가 있습니다. |
| The control loop period jitters (9–14 ms). Does your code care? | 고정 `dt`를 가정하면 안 됩니다. 매 주기 **하드웨어 타이머로 측정한 dt**(wrap-safe `uint32_t` 뺄셈)를 쓰고, 엔코더는 이미 "그 사이 이동량"이라 dt와 무관하게 맞습니다. 다만 속도(`d/dt`)를 계산하거나 ω를 적용할 때 실제 dt를 써야 하고, 비정상적으로 큰 dt(멈춤, 디버거)는 클램프하거나 그 주기를 버립니다. exact arc는 큰 dt에서도 정확해서 유리합니다. |
| How would you test this on real hardware? | (1) 직선 10 m를 줄자로 재서 `m_per_tick`(바퀴 반지름) 보정, (2) 제자리 10바퀴를 돌려 `track_width` 보정, (3) **UMBmark**처럼 정사각형을 시계/반시계 방향으로 각각 몇 번 돌고 출발점 대비 closing error를 재서 체계 오차(바퀴 지름 차이, 유효 트랙 폭)를 분리합니다. 같은 로그를 이 시뮬레이터에 재생(replay)해서 HIL 회귀 테스트로 씁니다. |
| What if the counter is 32-bit, or the read can be torn? | 32비트도 같은 식(`(int32_t)(now - prev)`)입니다. 8비트 MCU에서 16비트 카운터를 두 번에 나눠 읽으면 상·하위 바이트 사이에 캐리가 생길 수 있으므로 하드웨어 latch를 쓰거나 "상위 읽기 → 하위 읽기 → 상위 재확인" 루프로 읽습니다. |

## 7. 자주 하는 실수

- `now - prev`를 `int`로 계산하고 16비트로 자르지 않음 → `65530 → 4`가 −65526 (테스트 2)
- 엔코더 delta를 `uint16_t`/`uint32_t`로 저장 → 후진이 거대한 양수가 됨
- Euler(`cos(θ)`) 적분 → 사분원 테스트(6번)에서 4.3e-4 m 오차로 실패
- `θ`를 wrap하지 않음, 또는 [0, 2π)로 wrap → 테스트 7·8번 실패. `wrap_angle(-π)`를 −π로 둠
- **헤딩 오차를 wrap하지 않음** → 먼 쪽으로 돌아 감 (테스트 14). 이 문제에서 면접관이 가장 보고 싶어 하는 버그입니다
- `d`와 `dθ` 계산에서 `track_width / 2`로 나눔 → 회전량이 2배
- 도달 검사를 `if`로 한 번만 함 → 이미 웨이포인트 위에 있는 경우나 겹친 웨이포인트에서 한 스텝씩 늦어짐
- `n == 0`일 때 `wps[0]`에 접근 → `NULL` 역참조 (테스트 11)
- `t += dt` 누적으로 시간 비교 → 타임아웃 경계가 한 스텝 어긋남
