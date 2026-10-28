<!-- theme: pastel -->
# Scout AI — 온사이트(Final 4라운드) 대비 노트

> **작성일**: 2026-10-26(월) · **상태**: Technical screen 완료(10-26 10:00, Jason MacDonald) → 결과 대기 · **Final 형식**: CEO 30 · CTO 30 · HW 엔지니어 45 · 임베디드 엔지니어 45
> **같이 볼 것**: [연습 사이트 — R 그룹(실제 기출 + 변형)](technicalScreenPrep/index.html) · [면접관 노트 (Jason)](2026-10-25_scout_interviewer_jason_prep.html) · [D-3 노트](2026-10-23_scout_screen_D-3_prep.html) · [포지션 컨텍스트](scout_ai_embedded_context.html)
> 신뢰도: `[확인됨]` = Don의 실제 면접 기록·리크루터 안내 · `[추정]` = 그것을 바탕으로 한 판단. 온사이트 진행은 아직 확정 아님 — 통과 연락이 오면 날짜에 맞춰 §8 플랜을 역산한다

---

## 0. TL;DR

- **스크린 실제 문제** [확인됨]: 경험 이야기 중간에 바로 CoderPad. 좌표 평면의 자동차 A·B가 **원을 그리며 움직일 때 언제 충돌하는지 timestamp를 출력**. 상태 = 위치 (x,y) · 다음 위치 · 속도 · 회전 속도. 면접관은 **C++ 뼈대**로 시작했고 Don은 C로 풀었다. `init_car` / `update_car` / `did_collide` + `while` 루프 구조를 잡았지만 **함수 시그니처를 확신 없이 "대충" 정했다**
- **예측이 또 빗나갔다**: 리크루터 힌트(센서·과열 감지)가 아니라 **로보틱스 시뮬레이션 + 2D 기하**였다. Verkada에 이어 두 번째(W5). 온사이트는 "문제 유형 맞히기"보다 **처음 보는 시뮬레이션 문제를 구조화하는 능력**에 투자한다(§4)
- **접근이 갈린 지점** [확인됨]: Don은 처음에 **원의 반지름**을 파라미터로 하겠다고 했고, 면접관은 **x, y, heading, speed, rotational speed**로 다음 위치를 계산하는 솔루션을 준비해 왔다(§1.2). 면접관이 준 파라미터를 그대로 struct에 넣는 것이 먼저
- **가장 큰 교훈 = API 설계**: 상태 → `struct`, 변화 → `update(state, dt)`, 조건 → `check()`, 구동 → `t`와 `t_max`가 있는 루프. 이 4단계를 30초 안에 말하고 쓰는 연습(§4 템플릿)
- **새 드릴 R1~R4** (연습 사이트 맨 위 그룹): [R1 실제 기출](technicalScreenPrep/problems/R1_circular_car_collision/index.html) — **Solution 1**(면접관 방식: x, y, heading, speed, omega로 다음 위치 계산) · **Solution 2**(Don의 첫 제안: 원의 중심·반지름), C++ 버전 포함 · [R2 휠 오도메트리 + 웨이포인트](technicalScreenPrep/problems/R2_diff_drive_odometry/index.html) · [R3 모터 PID 시뮬](technicalScreenPrep/problems/R3_pid_speed_control/index.html) · [R4 지오펜스](technicalScreenPrep/problems/R4_geofence_polygon/index.html)
- **온사이트 4라운드는 성격이 다 다르다**: CEO = 미션·동기·주인의식, CTO = 자율주행 출신(Kodiak·Uber ATG) → **센서 데이터·시간 동기·시스템 설계**, HW 엔지니어 = bring-up·회로·계측, 임베디드 엔지니어 = **코딩(R 계열 유력) + RTOS/Linux 깊이**(§5)
- **C++ 대비**: 면접관이 C++로 제시한다. 쓰기는 C로 해도 되지만 **C++ 뼈대를 읽고 그 위에 바로 채울 수 있어야** 시간 손해가 없다(§6)

---

## 1. 스크린 복기 (2026-10-26, Jason MacDonald)

### 1.1 진행

| 항목 | 내용 |
|---|---|
| 흐름 | 경험 이야기 → 이야기 도중 **바로 CoderPad 링크** → 좌표 평면에 자동차 A·B를 그려 설명 [확인됨] |
| 문제 | 두 자동차가 원을 그리며 이동. **언제 collide하는지** 맞히기. 출력 = timestamp [확인됨] |
| 주어진 상태 | Location (x, y) · Next point · Velocity · Rotational velocity [확인됨 — Don 메모] |
| 언어 | 아무 언어나 가능. 면접관은 **C++로 제시**, Don은 **C** 선택 [확인됨] |
| Don이 잡은 구조 | `struct`로 점을 계속 추적. `init_car(x,y)` · `update_car(Car, x, y)` · `did_collide(carA, carB)` + `main`의 `while(1)` 루프에서 update → collide 검사 → `printf` [확인됨] |
| Don의 느낌 | "세 함수를 잘 못 잡아서 대충 만들었다", "workload가 빡세 보이지만 재밌어 보임" [확인됨] |
| 결과 | 대기 중 |

### 1.2 두 가지 접근 — Don의 첫 제안 vs 면접관의 솔루션

| | Don이 처음 제안 | 면접관이 준비해 온 솔루션 |
|---|---|---|
| 파라미터 | 원의 **반지름(radius)** 을 파라미터로 | **x, y, heading, rotational speed, speed** [확인됨] |
| 위치 구하는 법 | 원(중심·반지름)으로 위치를 표현 | 파라미터로 **다음 위치를 계산** → 시간을 한 스텝씩 진행 |
| 연습 사이트 | [R1](technicalScreenPrep/problems/R1_circular_car_collision/index.html) **Solution 2** (Part C) | [R1](technicalScreenPrep/problems/R1_circular_car_collision/index.html) **Solution 1** (Part A·B) |

- **반지름은 필요 없다**: heading을 매 스텝 `omega·dt`만큼 돌리면서 heading 방향으로 `speed·dt`만큼 가면, 결과적으로 반지름 `speed/omega`의 원이 그려진다. 면접관의 "Next point"는 이 **계산된 다음 위치**를 말한 것이다
- **면접관 방식이 더 일반적인 이유**: 직선(ω = 0), 속도·조향이 바뀌는 경우에도 코드가 그대로 간다. 실제 차량 펌웨어 루프와 같은 구조다. 반지름 방식은 ω가 일정할 때만 쓰는 지름길이다
- **다음에 이렇게**: 처음 3분 안에 "Do you want me to step the state forward with heading, speed and omega, or describe each path as a circle?"라고 묻고, 면접관이 준 파라미터를 **그대로** struct에 넣는다

**Don의 뼈대 → 고치면 좋았을 3가지**

| Don의 메모 | 문제 | 이렇게 |
|---|---|---|
| `update_car(Car, x, y)` | 다음 위치를 **밖에서 넣어 주는** 모양. C에서 `Car`를 값으로 받으면 수정이 호출자에게 안 보임 | `void update_car(Car *c, double dt)` — 다음 위치를 heading·speed·omega로 **안에서 계산** |
| `while (1)` | 안 만나면 **영원히 안 끝남** | `for (k = 0; k*dt <= t_max; k++)` + 없으면 `-1` |
| 루프에 시간 변수 없음 | 출력이 timestamp인데 `t`가 없음 | `t = k*dt`를 반환 |

### 1.3 이 문제가 알려 주는 것 [추정]

- Scout의 "Scout engineering type of problem"은 **센서 바이트 처리**보다 **물리 세계를 코드로 모델링**하는 쪽이다. 회사가 UGV·드론 자율 운용을 하므로 자연스럽다
- 평가 포인트는 정답 숫자보다 **상태 모델링, 시간 처리, 종료 조건, 수치 오차, 엣지 케이스(안 만남·이미 겹침·빠른 차)** 를 말하며 짜는지일 것이다
- 면접관(Jason)의 반자율 경로 추종(Charles Machine Works) 경력과 맞닿아 있다. **온사이트 임베디드 라운드도 같은 계열(경로·제어·기하) 변형**일 가능성이 높다

---

## 2. 실제 문제 — 모범답안 (용어 정의부터 main까지)

차트·테스트·C++ 버전은 [R1](technicalScreenPrep/problems/R1_circular_car_collision/index.html). 아래 두 프로그램은 그대로 복사해 `./run.sh R1 model1`, `./run.sh R1 model2`로 돌아간다.

### 2.0 용어 정의

| 이름 | 코드 | 단위 | 뜻 | 예제 값 |
|---|---|---|---|---|
| 위치 | `x`, `y` | m | 평면 위 차의 중심 좌표. +x = 오른쪽, +y = 위 | A (4, 0) · B (6, 3) |
| **heading** | `heading` (θ) | rad | 차가 **바라보는(진행하는) 방향**. +x축이 0, **반시계가 +**. 90° = π/2 = 위 | A 90° · B 0° |
| speed | `speed` (v) | m/s | heading 방향으로 1초에 가는 거리 | A 2.0 · B 1.2 |
| **omega** | `omega` (ω) | rad/s | **rotational speed** — heading이 1초에 도는 각도. + = 왼쪽(반시계), − = 오른쪽(시계), 0 = 직진 | A +0.5 · B −0.4 |
| **dt** | `DT` | s | **time step** — 루프 한 번에 흐르는 시간. 작을수록 정확·느림. 펌웨어에선 타이머 주기 | 0.01 |
| k, t | `k`, `t` | —, s | 스텝 번호와 현재 시각 `t = k·dt` (출력할 timestamp) | 1282 → 12.82 |
| 충돌 거리 | `COLLIDE_DIST` | m | 두 중심 거리가 이 이하면 충돌 | 1.0 |
| 최대 시간 | `T_MAX` | s | 이때까지 안 만나면 종료 (`while(1)` 방지) | 60 |
| 회전 반경 | (계산값) | m | `speed / omega`의 절댓값. **Solution 1은 저장 안 함**, Solution 2에서만 사용 | A 4 · B 3 |

**한 스텝(dt)에 일어나는 일**: ① `dist = speed·dt` ② `x += dist·cos(heading)`, `y += dist·sin(heading)` ③ `heading += omega·dt` ④ 거리 확인. `cos/sin`은 라디안 → degree는 `× π/180`.

### 2.1 모범답안 — Solution 1 (면접관 방식: 파라미터로 다음 위치 계산)

```c
/*
 * R1 model answer - Solution 1 (the interviewer's approach)
 *
 * Each car is described by exactly the parameters the interviewer gave:
 *   x, y      position on the plane (meters)
 *   heading   the direction the car is pointing (radians)
 *             0 = pointing along +x (right), PI/2 = +y (up), PI = -x, -PI/2 = -y
 *   speed     how far the car moves per second along its heading (m/s)
 *   omega     rotational speed: how fast the heading turns (rad/s)
 *             omega > 0 turns left (counter-clockwise), omega < 0 turns right
 *
 * We do not know the "next point" in advance - we COMPUTE it from these parameters.
 * Time advances in small steps of DT seconds. In one step:
 *   the car moves speed*DT meters along its current heading, then
 *   the heading turns by omega*DT radians.
 * Repeating this traces a circle of radius speed/omega without ever storing a radius.
 *
 * Build & run:  ./run.sh R1 model1
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define PI           3.14159265358979323846
#define DEG(d)       ((d) * PI / 180.0)   /* degrees -> radians (cos/sin take radians) */
#define COLLIDE_DIST 1.0                  /* m: centers this close = collision */
#define DT           0.01                 /* s: one simulation step (100 steps per second) */
#define T_MAX        60.0                 /* s: stop if no collision by then */

typedef struct {
    double x, y;      /* m */
    double heading;   /* rad, 0 = +x, counter-clockwise positive */
    double speed;     /* m/s along the heading */
    double omega;     /* rad/s, + = turn left, - = turn right */
} Car;

Car init_car(double x, double y, double heading, double speed, double omega)
{
    Car c = { x, y, heading, speed, omega };
    return c;
}

/* Parameters -> next position. Called once per time step. */
void update_car(Car *c, double dt)
{
    double dist = c->speed * dt;              /* meters travelled in this step */
    c->x += dist * cos(c->heading);           /* x part of the move */
    c->y += dist * sin(c->heading);           /* y part of the move */
    c->heading += c->omega * dt;              /* turn for the next step */
}

/* Collision = distance between centers <= COLLIDE_DIST (compare squares, no sqrt). */
bool did_collide(const Car *a, const Car *b)
{
    double dx = a->x - b->x;
    double dy = a->y - b->y;
    return dx * dx + dy * dy <= COLLIDE_DIST * COLLIDE_DIST;
}

int main(void)
{
    /*                x    y    heading   speed  omega            */
    Car a = init_car(4.0, 0.0, DEG(90),  2.0,  +0.5);   /* points up, turns left  */
    Car b = init_car(6.0, 3.0, DEG(0),   1.2,  -0.4);   /* points right, turns right */

    for (long k = 0; ; k++) {                 /* k = step number */
        double t = k * DT;                    /* current time (s); k*DT avoids += drift */
        if (t > T_MAX) {
            printf("no collision within %.0f s\n", T_MAX);
            return 0;
        }
        if (did_collide(&a, &b)) {
            printf("collision at t = %.2f s (step %ld)\n", t, k);
            printf("  A at (%.3f, %.3f)   B at (%.3f, %.3f)   distance %.3f m\n",
                   a.x, a.y, b.x, b.y, hypot(a.x - b.x, a.y - b.y));
            return 0;
        }
        update_car(&a, DT);                   /* both cars move to their next point */
        update_car(&b, DT);
    }
}
```

출력:
```
collision at t = 12.82 s (step 1282)
  A at (3.969, 0.506)   B at (3.260, 1.206)   distance 0.997 m
```

- k=0→1: A는 위(90°)를 보므로 (4, 0) → (4, 0.02), heading 1.5708 → 1.5758. B는 오른쪽(0°)이라 (6, 3) → (6.012, 3), heading 0 → −0.004
- 면접장에서 말할 순서: "struct with exactly your five parameters → `update_car` computes the next point → `did_collide` with squared distance → loop with `t = k·dt` and `T_MAX`"

### 2.2 모범답안 — Solution 2 (Don의 첫 제안: 원의 중심·반지름)

추가 용어: **반지름** `r = |v/ω|` · **중심** `cx = x − (v/ω)·sin θ`, `cy = y + (v/ω)·cos θ` (ω > 0이면 차의 왼쪽, < 0이면 오른쪽) · **위상** `phase` = 중심에서 본 차의 각도(heading과 다름), `phase0 = atan2(y − cy, x − cx)` · **위치 공식** `x(t) = cx + r·cos(phase0 + ω·t)`, `y(t) = cy + r·sin(phase0 + ω·t)`

```c
/*
 * R1 model answer - Solution 2 (Don's first idea: describe each path by its circle)
 *
 * Same inputs as Solution 1 (x, y, heading, speed, omega). With constant speed and
 * omega the car stays on ONE circle, so we convert the parameters to that circle once:
 *   r       radius of the path            r = |speed / omega|        (meters)
 *   cx, cy  center of the path            to the LEFT of the car when omega > 0,
 *                                         to the RIGHT when omega < 0
 *   phase   angle of the car seen from the center (radians, 0 = +x, CCW positive)
 *   phase0  that angle at t = 0
 * The car goes around the center at the same rate its heading turns, so
 *   phase(t) = phase0 + omega * t
 *   x(t) = cx + r * cos(phase(t)),   y(t) = cy + r * sin(phase(t))
 * Position at ANY time comes straight from the formula - no stepping, no drift.
 * Limitation: omega == 0 (straight line) has no circle.
 *
 * Build & run:  ./run.sh R1 model2
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define PI           3.14159265358979323846
#define DEG(d)       ((d) * PI / 180.0)
#define COLLIDE_DIST 1.0                  /* m */
#define DT           0.01                 /* s: how often we check the distance */
#define T_MAX        60.0                 /* s */

typedef struct {
    double cx, cy;    /* center of the circle (m) */
    double r;         /* radius (m) */
    double omega;     /* rad/s, + = counter-clockwise around the center */
    double phase0;    /* angle from center to car at t = 0 (rad) */
} CircleCar;

/* Convert the interviewer's parameters to a circle. Returns false for a straight line. */
bool circle_from_params(double x, double y, double heading, double speed, double omega,
                        CircleCar *out)
{
    if (fabs(omega) < 1e-9) return false;     /* omega == 0: no circle */
    double rs = speed / omega;                /* signed radius: + center on the left */
    out->cx = x - rs * sin(heading);          /* left of heading = heading + 90 deg */
    out->cy = y + rs * cos(heading);
    out->r = fabs(rs);
    out->omega = omega;
    out->phase0 = atan2(y - out->cy, x - out->cx);
    return true;
}

/* Position at time t, straight from the formula. */
void position_at(const CircleCar *c, double t, double *x, double *y)
{
    double phase = c->phase0 + c->omega * t;
    *x = c->cx + c->r * cos(phase);
    *y = c->cy + c->r * sin(phase);
}

int main(void)
{
    CircleCar a, b;
    /*                    x    y    heading  speed  omega */
    circle_from_params(4.0, 0.0, DEG(90), 2.0, +0.5, &a);
    circle_from_params(6.0, 3.0, DEG(0),  1.2, -0.4, &b);
    printf("A: center (%.2f, %.2f) r = %.2f   B: center (%.2f, %.2f) r = %.2f\n",
           a.cx, a.cy, a.r, b.cx, b.cy, b.r);

    for (long k = 0; ; k++) {
        double t = k * DT;
        if (t > T_MAX) {
            printf("no collision within %.0f s\n", T_MAX);
            return 0;
        }
        double ax, ay, bx, by;
        position_at(&a, t, &ax, &ay);         /* jump straight to time t */
        position_at(&b, t, &bx, &by);
        double dx = ax - bx, dy = ay - by;
        if (dx * dx + dy * dy <= COLLIDE_DIST * COLLIDE_DIST) {
            printf("collision at t = %.2f s (step %ld)\n", t, k);
            printf("  A at (%.3f, %.3f)   B at (%.3f, %.3f)   distance %.3f m\n",
                   ax, ay, bx, by, hypot(dx, dy));
            return 0;
        }
    }
}
```

출력:
```
A: center (0.00, 0.00) r = 4.00   B: center (6.00, 0.00) r = 3.00
collision at t = 12.83 s (step 1283)
  A at (3.965, 0.526)   B at (3.260, 1.222)   distance 0.991 m
```

### 2.3 두 답이 12.82 vs 12.83인 이유 + 비교 한 문장

- 정확한 충돌 시각은 **12.822241 s**. 0.01 간격 검사라면 정답은 그 직후인 **12.83** → Solution 2(공식이라 정확)
- Solution 1은 매 스텝 접선으로 직진하는 오차(Euler)가 쌓여 살짝 일찍 판정 → 12.82. dt를 줄이거나 정확한 호 공식 `x += (v/ω)(sin θ1 − sin θ0)`, `y += (v/ω)(cos θ0 − cos θ1)` + 이분법이면 12.822241 (R1 Part B)
- Solution 2의 한계: ω = 0(직선)이면 원이 없고, 속도·조향이 바뀌면 원을 다시 계산해야 함

> "A circle description gives me the position at any time directly, but stepping the state with heading, speed and omega is more general — it handles straight lines and changing inputs, and it's how the real control loop runs. So I'd write the step version and use the closed form as an optimization or a cross-check."

---

## 3. 온사이트 코딩 예상 — 빈도순 (실제 기출 반영)

| 순위 | 유형 | 근거 | 연습 |
|---|---|---|---|
| 1 | **운동 시뮬레이션 + 충돌/근접 시각** (원·직선, N대, 사각형) | [확인됨] 스크린 실제 문제. 변형이 가장 쉬움 | [R1](technicalScreenPrep/problems/R1_circular_car_collision/index.html) |
| 2 | **오도메트리 · 경로/웨이포인트 추종** | Jason 반자율 경로 추종, Scout UGV | [R2](technicalScreenPrep/problems/R2_diff_drive_odometry/index.html) |
| 3 | **제어 루프 시뮬레이션 (PID, 포화, windup)** | 모터·차량 제어, 시간 루프 같은 구조 | [R3](technicalScreenPrep/problems/R3_pid_speed_control/index.html) |
| 4 | **2D 기하 — 지오펜스, 선분 교차, 점-다각형** | 자율 운용 안전 경계, 같은 기하 계열 | [R4](technicalScreenPrep/problems/R4_geofence_polygon/index.html) |
| 5 | 센서 시계열 상태 머신(과열·히스테리시스) | [확인됨] 리크루터 힌트 — 다른 면접관이면 여전히 가능 | [T1-1](technicalScreenPrep/problems/T1-1_overheat_intervals/index.html) · [T1-2](technicalScreenPrep/problems/T1-2_streaming_hysteresis/index.html) |
| 6 | 시간 동기·타임스탬프 정렬 | CTO 배경(자율주행), 센서 퓨전 | [J4](technicalScreenPrep/problems/J4_time_sync_offset/index.html) |
| 7 | ISR → 링버퍼 → 태스크 | 임베디드 기본기 | [E3](technicalScreenPrep/problems/E3_isr_to_task_pipeline/index.html) · [T2-1](technicalScreenPrep/problems/T2-1_ring_buffer/index.html) |
| 8 | 바이트 스트림 패킷 / CAN 프레임 디코드 | 차량 통신 | [E2](technicalScreenPrep/problems/E2_extract_packet/index.html) · [J1](technicalScreenPrep/problems/J1_can_signal_decode/index.html) |
| 9 | HAL 위 센서 드라이버 | JD 핵심 업무 | [E1](technicalScreenPrep/problems/E1_i2c_sensor_driver/index.html) |

**R1 변형으로 나올 만한 것** — 같은 코드에 한 줄씩 바꿔 연습
- 차가 3대 이상 → 모든 쌍 검사, 첫 충돌 쌍과 시각
- 한 차는 직선, 한 차는 원 → ω = 0 분기
- 속도가 시간에 따라 변함(가속) → 호 공식 대신 작은 dt
- "충돌 대신 **최소 거리**와 그 시각을 출력" → 루프에서 최솟값 추적
- "**충돌을 피하도록** B의 속도를 바꿔라" → 예측 TTC가 임계값보다 짧으면 감속

---

## 4. 처음 보는 시뮬레이션 문제 — 5분 템플릿

> 스크린에서 막힌 지점(함수 모양 정하기)을 없애는 순서. 매 연습마다 첫 5분을 이 순서로.

| 단계 | 질문 | 코드 |
|---|---|---|
| ① 상태 | "무엇이 시간에 따라 변하나? 무엇이 고정인가?" | `typedef struct { ... } Thing;` |
| ② 단위·규약 | 초/미터? 라디안? 각도 기준축·방향? 크기가 있나? | 맨 위 주석 3줄 |
| ③ 변화 | "dt 동안 상태가 어떻게 바뀌나?" | `void step(Thing *s, double dt);` |
| ④ 조건 | "무엇을 찾나? 언제 멈추나?" | `bool done(const Thing *a, ...);` |
| ⑤ 구동 | 시간 변수, 최대 시간, 출력 형식, 못 찾으면? | `for (t = 0; t < t_max; t += dt) ...` → `-1` |

```c
/* units: seconds, meters, radians (0 = +x, CCW positive) */
typedef struct { double x, y, heading, speed, omega; } State;

static void step(State *s, double dt)          { /* physics for dt */ }
static bool event(const State *a, const State *b) { /* the thing we look for */ return false; }

double run(State a, State b, double dt, double t_max) {
    for (double t = 0.0; t <= t_max; t += dt) {
        if (event(&a, &b)) return t;
        step(&a, dt);
        step(&b, dt);
    }
    return -1.0;
}
```

- **처음엔 단순 루프로 끝까지 돌게** 만들고(15분), 남은 시간에 정밀도(이분법)·엣지 케이스·테스트를 올린다. Neros 교훈 W1과 같은 원칙: **완주 먼저**
- 말할 문장: "I'll get a working fixed-step version first, then talk about error and refine."

---

## 5. 라운드별 준비

### 5.1 CEO 30분 — Colby Adcock

| 나올 질문 | 준비 방향 |
|---|---|
| Why Scout? Why now? | "physical AI·로보틱스 칩/엣지로 가는 커리어 방향" + "Fury가 믿을 수 있는 센서 데이터를 받게 만드는 사람" ([컨텍스트 §2.9](scout_ai_embedded_context.html)) |
| How do you feel about working on autonomous weapons / defense? | **Don 본인의 입장을 정리해 둘 것**. 회사는 실사격 데모까지 했다. 애매한 답이 가장 나쁘다 |
| Startup pace — "urgency, relentless work" | 빠른 bring-up·마감 경험(Apple NPI, SK hynix bring-up)에서 구체적 한 장면 |
| What would you own in the first 90 days? | Sunnyvale 첫 임베디드 엔지니어 → 센서 bring-up 체크리스트, HIL/데이터 품질 툴, 조지아 팀과 인터페이스 정리 |
| Clearance / U.S. Person | 사실대로. TS 가능성에 대한 준비 상태 |

### 5.2 CTO 30분 — Collin Otis (Kodiak Robotics 창립 엔지니어, Uber ATG)

| 나올 질문 | 준비 방향 · 연습 |
|---|---|
| How would you make sensor data trustworthy for a learned model? | 타임스탬프 정확도, 프레임 드롭 감지, 캘리브레이션 버전 관리, 데이터 품질 메트릭(지연·지터·누락) — [J4](technicalScreenPrep/problems/J4_time_sync_offset/index.html), 면접관 노트 Q15 |
| Camera + IMU + GPS time sync on a vehicle | HW 트리거, PPS, PTP, MCU 타임스탬프 → 호스트 시계 매핑 |
| Design the sensor/compute architecture for a UGV | MCU(안전·실시간) vs Linux 컴퓨트(모델·카메라) 분담, 워치독·fail-safe, 인터페이스 프로토콜 — 면접관 노트 Q7 |
| What breaks when you scale from 1 to 100 vehicles? | 스토리 A(Apple factory test-node): 개체 편차, 자동화 테스트, 로그 기반 원인 분리 |
| A technical deep-dive on your hardest project | 준비 문서 §7 스토리 A·B·C. 숫자는 확실한 것만 |

### 5.3 HW 엔지니어 45분

| 나올 질문 | 준비 방향 |
|---|---|
| Walk me through bringing up a new sensor board | 전원 레일 → 리셋/클럭 → 버스 확인(스코프·LA) → WHO_AM_I → 데이터 검증. [E1](technicalScreenPrep/problems/E1_i2c_sensor_driver/index.html) |
| I2C pull-up sizing, bus stuck low recovery | 버스 정전 용량·rise time으로 저항 계산, 9 clock 복구 |
| Read this schematic / where would you probe? | 전원 시퀀스, 레벨 시프터, 종단 저항, 테스트 포인트 |
| CAN wiring problems in a vehicle | 120 Ω 양단 종단(합 60 Ω), 접지, 스텁 길이, 진동 커넥터 — 면접관 노트 Q3 |
| EMI / noise when motors run | 접지 루프, 차폐, 필터, 측정 방법 |
| Power: brown-out, battery monitoring | BOR, 전류 감시 — [J2](technicalScreenPrep/problems/J2_coulomb_counter/index.html) |

### 5.4 임베디드 엔지니어 45분 — 코딩 + 깊이

- **코딩**: §3 순위 1~4(R1~R4) 위주. 면접관이 C++ 뼈대를 줄 가능성 → §6
- **깊이 질문**: [면접관 노트](2026-10-25_scout_interviewer_jason_prep.html) Q1~Q16(CAN, Linux 센서, RTOS 메모리, 부트로더, 포팅, 테스트) 그대로 재사용
- 같은 조지아 팀의 다른 엔지니어일 수도 있음 [추정] → "동료로서 같이 일하기 편한가"를 같이 본다: 생각을 소리 내어, 질문을 먼저

---

## 6. C++ 뼈대가 주어질 때

면접관이 C++로 시작한다 [확인됨]. 아래만 읽을 수 있으면 C로 바꾸거나 그대로 채우는 데 문제없다.

| C++ | 의미 | C로 |
|---|---|---|
| `struct Car { double x, y; void update(double dt); };` | 멤버 함수 | `void car_update(Car *c, double dt);` |
| `const Car& other` | 복사 없는 읽기 전용 참조 | `const Car *other` |
| `std::vector<Car> cars;` | 가변 길이 배열 | `Car cars[MAX]; size_t n;` |
| `std::optional<double>` | 값이 없을 수도 있음 | `-1` 반환 또는 `bool f(..., double *out)` |
| `for (auto& c : cars)` | 범위 for | `for (size_t i = 0; i < n; i++)` |
| `std::min`, `std::abs`, `M_PI` | 유틸리티 | `fmin`, `fabs`, 직접 정의한 `PI` |

- 말할 문장: "I'm more fluent in C for embedded work, so I'll write it in C — the structure maps one to one. Happy to switch if you prefer."
- R1 README §6에 **같은 풀이의 C++ 버전**이 있다. 한 번 소리 내어 읽기

---

## 7. 역질문 (라운드별 1~2개)

| 라운드 | 질문 |
|---|---|
| CEO | "What does success look like for the first embedded engineer in Sunnyvale a year from now?" |
| CEO | "How do you balance speed with the reliability a fielded military system needs?" |
| CTO | "How does sensor timing and calibration quality show up in Fury's training and evaluation today?" |
| CTO | "Where is the boundary between the embedded team and the autonomy team on the vehicle compute?" |
| HW | "What does the sensor kit look like on the ISV / ATV today, and what's the most painful part of integration?" |
| HW | "How do you test hardware before it goes to the range?" |
| 임베디드 | "The coding question was a motion simulation — is that close to the day-to-day work, e.g. a simulator or on-vehicle safety checks?" |
| 임베디드 | "How do the Georgia team and Sunnyvale split ownership today?" |

---

## 8. 준비 플랜 (온사이트 날짜가 오면 역산)

| 블록 | 할 것 | 완료 기준 |
|---|---|---|
| 오늘 저녁(10-26) | 스크린 복기 — **R1을 빈 파일에서 35분** (`./run.sh R1`) | 단순 루프 + 정확한 답 모두 PASS |
| 1일차 | R2 타이머 35분 + §5.1 CEO 답(특히 국방 입장) 글로 정리 | 오도메트리 PASS, 입장 3문장 |
| 2일차 | R3 타이머 30분 + CTO 시스템 설계 질문 2개를 화이트보드처럼 말하기 | 녹음 5분씩 |
| 3일차 | R4 타이머 35분 + HW 라운드 질문 표 소리 내어 | — |
| 4일차 | **모의 45분 ×2**: R1 변형(§3 목록) 한 개 + 처음 보는 변형 하나 — §4 템플릿 첫 5분 엄수 | 15분 안에 단순 버전 돌아감 |
| 전날 | 스토리 A·B·C, 역질문, §6 C++ 표 훑기, 일찍 자기 | — |

- 매일 한 번 **C++ 뼈대 읽기**(R1 C++ 버전) 5분
- 결과 연락이 오면 [포지션 컨텍스트](scout_ai_embedded_context.html) 로그와 이 노트의 상단 상태를 갱신

---

## 9. 결과 대기 중 리크루터에게 (선택)

> Hi Bobby, thank you for setting up today's technical screen with Jason. I really enjoyed the car-collision simulation problem — it was a fun way to talk about modeling motion and timing on a robot. I'm very excited about the role and look forward to hearing about next steps.

- 짧게, 오늘 안에. 보낼지는 Don이 결정
