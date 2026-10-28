# R3 · Motor Speed PID with a Simulated Plant

> **ID**: R3 · **Priority**: P1 · **Pattern**: fixed-rate control loop + plant model + saturation + anti-windup + metrics from a time series · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh R3` / `./run.sh R3 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 모터 속도를 목표값에 맞추는 PID 한 스텝(A)을 만들고, 모터 모델과 함께 루프를 돌려서 응답 성능 지표(overshoot, rise/settling time, 정상상태 오차)를 계산한다(B).
- **상황**: UGV 구동 모터를 100 Hz(`dt = 0.01 s`) PID 루프로 속도 제어. 모터(plant) 모델 `motor_step()`은 주어진다(`K = 10` rad/s per V, `tau = 0.5 s`). 전압은 0 … 12 V로 제한되니 모터는 최대 `10 × 12 = 120` rad/s.
- **배경 개념**
  - **P/I/D 항**: 오차 `e = setpoint − measured`. P = 지금 오차에 비례(`kp·e`), I = 오차를 시간에 걸쳐 누적한 값(남는 오차를 없앰), D = 변화 속도에 반응해서 급하게 변하는 걸 눌러줌. 출력 `u = P + I + D`.
  - **saturation(포화)**: 계산한 `u`가 actuator 한계(여기선 0 … 12 V)를 넘으면 잘라낸다(clamp).
  - **windup**: 출력이 이미 포화돼서 더 못 올리는데 I가 계속 쌓이는 현상. 나중에 목표에 도달해도 쌓인 I 때문에 크게 overshoot한다. **anti-windup**은 이걸 막는 장치(여기선 conditional integration).
  - **derivative on measurement**: D를 오차가 아니라 측정값 변화로 계산(`-kd · (meas − prev_meas) / dt`). setpoint가 갑자기 바뀌어도 D가 튀지(kick) 않는다.
- **입력**: Part A는 `Pid`(설정 `kp, ki, kd, u_min, u_max, anti_windup` + 상태 `integ, prev_meas, primed, u_last`), `setpoint`, `measured`, `dt`. Part B는 `Pid`, `Motor`, setpoint 배열 `setpoints[n_steps]`, `dt`, 호출자가 준 `log` 배열.
- **출력**: A는 clamp된 출력 전압 `u`. B는 `SimResult`(`valid`, `overshoot_pct`, `rise_time_s`, `settling_time_s`, `ss_error`)와 채워진 `log`.
- **규칙**
  - Part A: `integ += ki · e · dt`(ki를 곱한 상태로 저장, 단위 V). anti-windup이 켜져 있고 출력이 포화되면서 오차가 포화 방향을 더 미는 경우엔 이번 스텝 `integ` 갱신 안 함. 첫 호출은 이전 측정값이 없으니 `D = 0`. `dt <= 0`(또는 NaN)이면 상태를 안 바꾸고 이전 출력 반환.
  - Part B 루프: `k = 0 … n_steps-1`마다 `u = pid_step(...)` → `motor_step(m, u, dt)` → `log[k] = m->omega`. `log[k]`는 `t = (k+1)·dt`의 속도.
  - Part B 지표는 **마지막 setpoint 구간**에서만 계산: `s0` = 끝까지 값이 `r = setpoints[n-1]`과 같은 첫 index, step은 `y0`(step 직전 속도)에서 `r`로. 시간은 step 시점부터 잰다.
    - overshoot: step 방향으로 `r`을 넘어간 최대량 ÷ `r`의 절댓값 × 100(음수면 0). rise time: step의 90 % 도달 시각 − 10 % 도달 시각(90 % 미도달이면 `-1`). settling time: 그 뒤로 모든 샘플이 `r`의 ±2 % 안에 머무는 첫 시각(마지막 샘플이 밖이면 `-1`). `ss_error = r − log[n-1]`.
  - `n_steps == 0`, `dt <= 0`, `NULL` 포인터 → `valid = false`. `malloc` 금지.
- **예제 풀어 읽기**: 0 → 60 rad/s step, 300 스텝(3 s). P only(kp 0.5)는 50 rad/s에서 멈춰 `ss_error = 10`, 54(90 %)에 못 가서 rise/settling 모두 `-1`. PI(kp 0.5, ki 2)는 오차는 없애지만 overshoot 25.56 %, settling 1.17 s. 같은 gain에 anti-windup을 켜면 overshoot 0.04 %, rise 0.34 s, settling 0.57 s.
- **놓치기 쉬운 것**: 도달 불가능한 150 rad/s를 0.5 s 준 뒤 100 rad/s로 바꾸는 windup 데모(지표는 100 rad/s 구간 기준, anti-windup 없으면 overshoot 19.93 %·settling 3.81 s, 있으면 0.52 s); setpoint가 점프해도 측정값이 그대로면 D = 0; 포화 중 anti-windup이면 `integ`가 0 그대로, 없으면 10까지 쌓임; 아래쪽 clamp(0 V); `dt`가 0이나 음수일 때 상태 유지; `simulate()` guard 3종.
- **구현할 함수**
  - Part A: `pid_step(c, setpoint, measured, dt)` (`pid_init`, `motor_init`, `motor_step`은 주어짐)
  - Part B: `simulate(c, m, setpoints, n_steps, dt, log)`

## 1. Problem

A UGV drive motor is speed-controlled by a PID loop that runs at **100 Hz** (`dt = 0.01 s`). The motor (the *plant*) is already modeled for you as a first-order system:

```
d(omega)/dt = (K*u - omega) / tau        K = 10 rad/s per V,  tau = 0.5 s
```

`motor_step()` is **given**. It advances the motor by `dt` with the exact discretization `omega += (K*u - omega) * (1 - exp(-dt/tau))`. The drive voltage is limited to **0 … 12 V**, so the motor can never exceed `K * 12 = 120 rad/s`.

**Part A — `pid_step()`**: one controller update.

- `u = P + I + D`, then clamp to `[u_min, u_max]`.
- Store the integral already multiplied by `ki`: `integ += ki * e * dt` (units: volts).
- **Anti-windup** (when `c->anti_windup` is true): **conditional integration**. If the output would saturate *and* the error pushes further into saturation, do not update `integ` on this step.
- **Derivative on measurement**, not on error: `D = -kd * (meas - prev_meas) / dt`. On the very first call there is no previous measurement, so `D = 0`.
- If `dt <= 0` (or NaN), change no state and return the previous output.

**Part B — `simulate()`**: run the loop and compute step-response metrics from the logged speed.

- For `k = 0 … n_steps-1`: `u = pid_step(c, setpoints[k], m->omega, dt)`, `motor_step(m, u, dt)`, `log[k] = m->omega`. So `log[k]` is the speed at `t = (k+1)*dt`.
- Metrics are measured on the **last setpoint segment**: `s0` is the first index such that `setpoints[s0..n-1]` all equal `r = setpoints[n-1]`. The step happens at `t = s0*dt`, from `y0` (the speed just before step `s0`) to `r`. Times are measured from the step.
  - `overshoot_pct = max(0, max dir*(log - r)) / |r| * 100` (`dir = +1` for a step up, `-1` for a step down)
  - `rise_time_s` = time of first sample ≥ 90 % of the step − time of first sample ≥ 10 %; `-1` if 90 % is never reached
  - `settling_time_s` = time of the first sample after which **every** sample stays within ±2 % of `|r|`; `-1` if the last sample is outside
  - `ss_error = r - log[n-1]`
- `n_steps == 0`, `dt <= 0`, or a `NULL` pointer → `valid = false`. No `malloc`; the caller provides `log`.

```c
typedef struct { double omega, K, tau; } Motor;
void motor_step(Motor *m, double u_volts, double dt);          /* given */

typedef struct {
    double kp, ki, kd, u_min, u_max;  bool anti_windup;         /* config */
    double integ, prev_meas;  bool primed;  double u_last;      /* state  */
} Pid;

typedef struct {
    bool valid;
    double overshoot_pct, rise_time_s, settling_time_s, ss_error;
} SimResult;

double    pid_step(Pid *c, double setpoint, double measured, double dt);
SimResult simulate(Pid *c, Motor *m, const double *setpoints, size_t n_steps,
                   double dt, double *log);
```

### Example

```
step 0 -> 60 rad/s at t = 0, 300 steps (3 s), dt = 0.01, u in [0, 12] V

controller                    overshoot  rise    settling  ss_error
P only   (kp 0.5)             0 %        -1      -1        10.000
PI       (kp 0.5, ki 2)       25.56 %    0.27 s  1.17 s    ~0
PI + anti-windup (same gains) 0.04 %     0.34 s  0.57 s    ~0
```

### Constraints

- `0 <= n_steps <= 100000`, `dt` is normally `0.01`.
- Setpoints are copied values, so exact `==` comparison between setpoints is fine.
- `<math.h>` is allowed. Plain `double` math (host simulation).

### Clarifying questions to ask (first 3 minutes)

1. Is the loop period fixed, or should I use a measured `dt` each tick? What if `dt` is 0 or negative?
2. Which anti-windup do you want — conditional integration, clamping the integrator, or back-calculation?
3. Derivative on error or on measurement? (On measurement avoids a kick when the setpoint jumps.)
4. Overshoot relative to what — the final setpoint, or the step size? Settling band ±2 % or ±5 %?
5. In which order inside one tick: read sensor → compute `u` → apply → log? Which time does `log[k]` belong to?
6. If the setpoint changes several times, which step do the metrics describe?

## 2. Your workspace

`stub.c`를 복사해서 시작하거나 `./run.sh R3`로 바로 테스트하세요. `motor_step()`, `pid_init()`, `motor_init()`은 주어진 코드이고, `pid_step()`과 `simulate()` 두 함수만 채우면 됩니다. 테스트 7묶음이 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — pid_step()의 순서</summary>

1. `dt` 가드 → 2. `e = sp - meas` → 3. `D`는 `meas - prev_meas`로 계산하고 `prev_meas` 갱신 → 4. `i_try = integ + ki*e*dt`로 **시험 삼아** 적분 → 5. `u = P + i_try + D` → 6. 포화 && 같은 방향이면 `i_try`를 버리고 `u = P + integ + D`, 아니면 `integ = i_try`로 확정 → 7. clamp, `u_last` 저장.
핵심은 "적분을 먼저 확정하지 말고, 포화 여부를 본 다음 확정"하는 것입니다.

</details>

<details>
<summary>Hint 2 — simulate()의 루프와 구간 찾기</summary>

한 틱은 **측정(`m->omega`) → `pid_step` → `motor_step` → `log[k]` 기록** 순서입니다. 루프가 끝난 뒤 `setpoints`를 **뒤에서부터** 훑어 마지막 값과 같은 구간의 시작 `s0`를 찾습니다. `y0`는 `s0 == 0`이면 루프 전 `m->omega`, 아니면 `log[s0-1]`입니다. 0으로 두면 setpoint가 바뀌는 테스트(3번)가 틀립니다.

</details>

<details>
<summary>Hint 3 — settling time은 뒤에서부터</summary>

"밴드에 처음 들어간 시점"이 아니라 "**마지막으로 밴드 밖에 있던 샘플의 다음**"입니다. 끝에서부터 밴드 안인 동안 `k--` 하면 한 번의 역방향 스캔으로 끝납니다. 마지막 샘플이 밖이면 `-1`.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

같은 0 → 60 rad/s 스텝에 세 제어기를 붙인 실제 `solution.c` 출력입니다(2 s까지, 0.02 s 간격으로 다운샘플).

<div class="chart" data-src="chart1.svg" data-caption="P는 50에서 멈춤(ss_error 10). PI는 오차를 없애지만 포화 중 적분이 쌓여 25.56 % 오버슈트, 1.17 s에 정착. 같은 게인에 anti-windup만 켜면 0.04 %, 0.57 s"></div>

세 가지를 말로 짚으면 됩니다.

- **P만 쓰면 정상상태 오차가 남는다**: 정상상태에서 `ω = K·kp·e`이고 `e = r − ω`이므로 `ω = 60 · 5/6 = 50`, 오차 10. 출력 전압을 만들려면 오차가 남아 있어야 합니다. 그래서 90 %(54)에 도달하지 못해 `rise_time = -1`, 밴드(58.8~61.2)에도 못 들어가 `settling = -1`입니다.
- **I가 오차를 없앤다**: 정상상태에서 `integ`가 필요한 전압 6 V를 혼자 들고 있게 됩니다(`e = 0`이어도 출력 유지).
- **포화 + 적분 = windup**: 처음엔 `kp·e = 30 V`라 출력이 12 V에 붙습니다. 이 동안 적분이 계속 쌓이면(최대 19.02 V) ω가 60을 넘은 뒤에도 출력이 안 내려와 75.34까지 올라갑니다. 흥미로운 점은 windup 쪽 rise time(0.27 s)이 더 짧다는 것 — 더 오래 12 V를 밀었기 때문이고, 대가가 오버슈트입니다.

### 4.2 제어 루프 블록 다이어그램

<div class="chart" data-src="chart2.svg" data-caption="한 틱 = 측정 → pid_step → 포화 → motor_step. D는 오차가 아니라 측정값에서, anti-windup은 포화 블록에서 적분기로 되먹임"></div>

**anti-windup 방식 선택 — conditional integration(조건부 적분)**: 포화된 방향으로 오차가 더 밀 때만 적분을 멈춥니다. 장점은 코드 3줄, 튜닝 파라미터가 없음. 대안인 **integrator clamping**(`integ`를 `[u_min, u_max]`로 자르기)은 더 단순하지만 P가 큰 상황에서는 여전히 과하게 쌓일 수 있고, **back-calculation**(`integ += (ki·e + (u_sat − u_raw)/Tt)·dt`)은 가장 부드럽지만 추적 시정수 `Tt`라는 파라미터가 하나 더 생깁니다.

**windup 극단 예 — 도달 불가능한 setpoint (테스트 3)**: 0.5 s 동안 150 rad/s(최대 120이라 불가능)를 요구한 뒤 100으로 낮춥니다.

<div class="chart" data-src="chart3.svg" data-caption="위: ω, 아래: 적분항 integ. anti-windup이 없으면 integ가 115.17 V까지 쌓여 setpoint를 낮춘 뒤에도 t ≈ 3.7 s까지 12 V 고정, 오버슈트 19.93 %, 정착 3.81 s. 조건부 적분은 포화 동안 integ를 0에 묶어 0.52 s에 정착"></div>

포화가 풀리려면 쌓인 115 V를 음의 오차로 다 갚아야 합니다(오차 −20 × ki 2 = 초당 약 40 V씩). 실제 로봇에서 이건 "명령을 줄였는데 바퀴가 몇 초간 계속 최대 속도로 돈다"는 뜻이고, 안전 문제입니다.

### 4.3 손으로 따라가기 (trace)

PI (`kp 0.5, ki 2`), r = 60, dt = 0.01. 플랜트 계수 `1 − exp(−0.01/0.5) = 0.019801`이라 12 V 첫 스텝은 ω = 120 × 0.019801 = 2.376.

| k | meas ω | e | P | i_try | u_raw | anti-windup 판단 | u | integ (AW) | integ (no AW) |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 0.000 | 60.000 | 30.000 | 1.2000 | 31.200 | 포화 & e>0 → **freeze** | 12.000 | 0 | 1.2000 |
| 1 | 2.376 | 57.624 | 28.812 | 1.1525 | 29.964 | freeze | 12.000 | 0 | 2.3525 |
| 17 | 34.588 | 25.412 | 12.706 | 0.5082 | 13.214 | freeze | 12.000 | 0 | 15.0428 |
| 18 | 36.279 | 23.721 | 11.861 | 0.4744 | 12.335 | freeze → u = P + integ | **11.861** | 0 | 15.5173 |
| 19 | 37.909 | 22.091 | 11.045 | 0.4418 | 11.487 | 포화 아님 → **commit** | 11.487 | 0.4418 | 15.9585 |

(i_try·u_raw 열은 AW 쪽 값입니다. no-AW 쪽은 k = 45까지 계속 12 V이고, 그때 ω는 이미 72.18로 setpoint 60을 넘었습니다.)

k = 18 줄이 미묘한 포인트입니다. 적분을 얼린 뒤 다시 계산한 `u`가 12 V 아래로 내려오므로, 그 틱은 사실 포화가 아닙니다. 다음 틱부터 적분이 재개됩니다.

**메트릭 스캔 (PI, no AW)**: 10 % = 6 → 첫 도달 t = 0.03 s(ω 6.988), 90 % = 54 → t = 0.30 s(54.143) ⇒ rise 0.27 s. 밴드 하한 58.8에 **처음** 들어가는 건 0.34 s지만 이후 75.34까지 튀어 나갔다 돌아오므로, 역방향 스캔으로 찾은 정착 시점은 1.17 s(ω 61.169 < 61.2)입니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `if (!(dt > 0.0))`는 `dt <= 0`과 NaN을 한 번에 거릅니다(`NaN > 0`은 false). `dt`로 나누는 D 계산보다 **먼저** 있어야 합니다.
- `integ`에 `ki`를 미리 곱해 저장하므로, 운전 중 `ki`를 바꿔도 출력이 튀지 않습니다(`ki * Σe` 형태로 저장하면 `ki`가 바뀌는 순간 적분항 전체가 비례해서 점프).
- D는 `-kd * d(meas)/dt`. setpoint 계단은 D에 전혀 안 들어가고(테스트 4), 첫 호출은 `primed == false`라 0입니다.
- 메트릭은 루프와 분리된 **배열 스캔 세 번**입니다: setpoint 역방향(구간 시작), log 순방향(10 %/90 %/peak), log 역방향(정착). 각각 O(n).
- `dir`을 곱해 두면 내림 스텝도 같은 코드로 처리됩니다.

### 4.5 복잡도

- 시간 **O(n)** — 시뮬레이션 1회 + 스캔 3회.
- 추가 메모리 **O(1)** — `log`는 호출자 버퍼. `pid_step`은 틱당 O(1), 상태 4개(`integ`, `prev_meas`, `primed`, `u_last`).

---

## 5. 임베디드 관점으로 말할 것

- **고정 주기 timer ISR vs 측정된 dt**: MCU에서는 하드웨어 타이머 인터럽트(또는 PWM 주기에 동기된 ADC/엔코더 캡처)가 정확히 10 ms마다 루프를 돌리므로 `dt`를 상수로 씁니다 — 나눗셈도 `1/dt` 상수 곱으로 바뀝니다. RTOS 태스크처럼 지터가 있으면 측정된 `dt`를 쓰되, `dt`가 0/음수/비정상적으로 큰 경우를 가드해야 합니다(이 문제의 `dt <= 0` 가드). D항은 지터에 특히 민감합니다.
- **FPU 없는 MCU → 고정소수점(Q15/Q31) PID**: 오차·게인을 Q15로, 적분 누산기는 `int32_t`(또는 `int64_t`)로 두고, 누산할 때 **포화 덧셈**(Cortex-M4의 `QADD`, `__SSAT`)을 씁니다. CMSIS-DSP에 `arm_pid_q15`가 있지만 사용자 범위 clamp(0~12 V 같은)와 anti-windup은 없어서 직접 감싸야 합니다.
- **엔코더 속도 추정 노이즈 → D 필터**: 10 ms 동안의 카운트 차이로 속도를 구하면 저속에서 양자화 계단이 생기고, D는 그 계단을 `1/dt` = 100배로 키웁니다. 그래서 D에 1차 저역통과(`N`=5~20)를 걸거나, 저속에서는 펄스 주기 측정(1/T 방식)으로 바꿉니다. 이동평균은 [T1-3](../T1-3_moving_average/index.html)과 같은 구조입니다.
- **CANopen 드라이브는 이미 안쪽 루프가 있다 (cascade)**: CiA 402 드라이브는 내부에 전류 루프(수~수십 kHz)와 속도 루프(~1 kHz)를 갖고 있고, 상위 컨트롤러는 Profile Velocity / Cyclic Synchronous Velocity 모드로 속도 setpoint만 RPDO로 보냅니다(SYNC 주기). 상위에서 PID를 또 짠다면 보통 **바깥 루프(위치/헤딩)**이고, 안쪽 루프보다 5~10배 느리게 잡아야 두 루프가 싸우지 않습니다. Jason의 Charles Machine Works 경험과 바로 연결되는 얘기입니다.
- **안전**: 출력 clamp는 소프트웨어 마지막 방어선이 아닙니다. 드라이브 쪽 전류 제한·속도 제한·watchdog(SYNC/heartbeat 끊기면 정지)이 따로 있어야 하고, 엔코더 값이 안 바뀌는데 출력이 포화돼 있으면(stall/엔코더 단선) 고장으로 판단해 끕니다.
- 이 문제는 실제 스크린 문제([R1](../R1_circular_car_collision/index.html))와 같은 틀 — **상태 구조체 + update 함수 + 고정 dt 루프 + 결과를 배열 스캔으로 판정** — 입니다. 적분 누적과 `dt` 처리는 [J2](../J2_coulomb_counter/index.html)와도 같습니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| How would you tune the gains? | 이 플랜트는 1차라 해석적으로 잡을 수 있습니다: `ki/kp = 1/τ = 2`로 PI의 zero가 플랜트 pole을 상쇄하면 폐루프가 시정수 `τ/(K·kp)`의 1차가 됩니다(kp 0.5 → 0.1 s). 모델이 없으면 Ziegler–Nichols: I·D를 끄고 kp를 올려 지속 진동하는 `Ku`, 주기 `Tu`를 찾은 뒤 `Kp = 0.6Ku, Ti = Tu/2, Td = Tu/8`. 단 ZN은 공격적이라(오버슈트 큼) 출발점으로만 쓰고, 실제로는 스텝 응답 로그를 보며 줄입니다. |
| The encoder speed is noisy. What happens to D? | D가 노이즈를 `1/dt`배로 증폭해 출력이 떨립니다. `D_f += α·(D_raw − D_f)` 같은 1차 필터(필터 극점은 루프 대역의 5~10배)를 걸거나, 속도 루프에서는 D를 아예 빼고 PI만 씁니다(실무에서 속도 루프는 대부분 PI). |
| What is bumpless transfer? | 수동→자동 전환이나 게인 변경 순간 출력이 튀지 않게 하는 것. 전환 시 `integ = u_current − P − D`로 초기화하면 첫 출력이 직전 출력과 같아집니다. `integ`에 ki를 곱해 저장하는 것도 같은 목적입니다. |
| In fixed point, what can overflow? | 적분 누산기. `int32_t`에 Q15 오차 × 게인을 매 틱 더하면 windup 상황에서 몇 초 만에 넘칠 수 있고, 넘치면 부호가 뒤집혀 출력이 최대→최소로 튑니다. 포화 덧셈 + 누산기 자체를 `[u_min, u_max]`로 clamp + anti-windup으로 막습니다. 곱셈은 `(int32_t)a * b >> 15`로 중간값을 넓은 타입에서. |
| What if the control loop overruns its period? | 먼저 측정: 루프 시작/끝에 `DWT->CYCCNT`를 찍어 최악 실행 시간과 overrun 카운터를 남깁니다. 한 번 놓치면 다음 틱에 실제 `dt`(2배)를 쓰거나 그 틱을 건너뛰고, 연속으로 놓치면 안전 정지(출력 0, 브레이크)로 갑니다. 근본 해결은 ISR에서 무거운 일 빼기, 우선순위 조정, 계산을 고정소수점으로. |
| How would you test this on hardware? | ① 지금처럼 같은 `pid_step` 코드를 호스트에서 플랜트 모델과 단위 테스트(이 파일). ② HIL: MCU에서 실제 코드를 돌리고 플랜트는 PC/FPGA가 시뮬레이션. ③ 벤치: 바퀴를 띄우거나 다이나모에 물리고 스텝 명령 → CAN/UART로 ω 로그 → 같은 메트릭 함수로 overshoot/settling 판정해 합격 기준과 비교. ④ 고장 주입: 엔코더 단선, 전원 저하, setpoint 폭주. |
| Why the exact discretization instead of Euler? | `ω += dt/τ·(K·u − ω)`(Euler)도 `dt ≪ τ`면 거의 같지만, `exp` 형태는 `u`가 한 틱 동안 일정(ZOH)하다는 가정에서 오차가 0이고 `dt`가 커져도 발산하지 않습니다. MCU에서는 `1 − exp(−dt/τ)`를 상수로 미리 계산합니다. |
| What about a step down or a negative setpoint? | 메트릭은 `dir`로 이미 처리됩니다. 양방향 구동이면 `u_min = −12`로 바꾸고, anti-windup 조건의 `push_low`가 그대로 동작합니다. |

## 7. 자주 하는 실수

- D를 **오차로** 미분 → setpoint 계단에서 `kd·Δr/dt`만큼 출력이 튐(derivative kick) → 테스트 4 실패
- 적분을 **먼저 확정**한 뒤 포화를 확인 → anti-windup이 한 틱씩 늦고, 테스트 5의 `integ == 0` 실패
- `dt` 가드를 D 나눗셈 **뒤에** 둠 → `dt = 0`에서 inf/NaN이 상태에 들어감 → 테스트 6 실패
- 루프 순서 혼동: `log[k]`에 `motor_step` **전** 값을 기록 → 모든 시간이 한 틱씩 어긋남
- `y0 = 0`으로 고정 → setpoint가 바뀌는 테스트 3에서 rise/overshoot 기준이 틀림
- settling을 **밴드에 처음 들어간 시점**으로 계산 → PI(no AW)에서 1.17 s가 아니라 0.34 s가 나옴
- P-only에서 90 %에 도달하지 못하는 경우를 처리하지 않아 `t90 − t10`에 쓰레기 값 → `-1` 규칙을 지킬 것
- overshoot를 % 단위로 안 바꾸거나(×100 누락) 아래로 튄 값을 음수 overshoot로 보고
