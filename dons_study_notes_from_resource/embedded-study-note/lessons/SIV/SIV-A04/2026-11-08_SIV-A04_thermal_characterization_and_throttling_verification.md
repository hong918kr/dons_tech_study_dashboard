---
id: SIV-A04
time: 120분
updated: 2026-11-08
---
# SIV-A04 열 특성화와 Throttling 검증

## 학습 목표

- 3노드 열 RC 플랜트(die·package·heatsink)를 forward Euler로 적분하고 **안정 dt 조건**(Gershgorin 충분조건 0.1 s, 실제 한계 0.187 s)을 설명·측정할 수 있다.
- 펌웨어 thermal governor를 **믿지 않는 쪽**에서 trace checker를 작성한다: trip 임계, hysteresis, DVFS 단계 순서(V/f 순서 포함), emergency, shutdown.
- 센서 오프셋·lag·stuck을 주입해 **time-to-throttle 지연, Tj_max 초과량, 필요한 guard band**를 숫자로 낸다. 왜 같은 오프셋이 부하에 따라 지연을 수십 배 다르게 만드는지 설명한다.
- 심어 둔 governor 버그(hysteresis 부호, 단계 누락, stale 미검출 등)를 시그니처로 찍어 내고, 디버그 엔지니어가 바로 움직일 수 있는 판정 리포트를 만든다.
- 이 방법이 **못 보는 것**(센서값만 보는 checker의 사각지대, 값만 고정된 stuck, 공통모드 오프셋, 도달하지 않은 경로)을 측정으로 보여 주고, 실험실에서 어떻게 메우는지 답할 수 있다.

## 1. 문제

열 throttling은 "칩이 뜨거워지면 느려진다"가 아니라 **온도 → 센서 → 펌웨어 → DVFS → 전력 → 온도**로 도는 폐루프이고, 어느 한 고리가 조금만 틀어져도 Tj_max 초과나 성능 손실로 끝난다. 그래서 실리콘 검증 면접(Rivian/NVIDIA 등)에서 "센서 오프셋 오차가 있을 때 throttle이 얼마나 늦어지나?"는 전형적인 staff급 꼬리질문이다.

이 레슨의 DUT는 **주어진 펌웨어 governor**(버그 스위치 포함)다. 여러분은 그 바깥쪽 전부를 쓴다: 플랜트 한 스텝, trace checker, 위험도 정량화, 판정 리포트.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="20" y="22" fill="currentColor" font-weight="bold">3노드 열 RC 플랜트 (Cauer 사다리)와 폐루프</text>
  <g fill="none" stroke="currentColor" stroke-width="1.4">
    <rect x="40" y="60" width="110" height="46" class="f-acc" opacity=".25"/>
    <rect x="240" y="60" width="110" height="46" class="f-mut" opacity=".25"/>
    <rect x="440" y="60" width="110" height="46" class="f-mut" opacity=".25"/>
    <rect x="640" y="60" width="100" height="46" class="f-ok" opacity=".25"/>
    <line x1="150" y1="83" x2="240" y2="83"/>
    <line x1="350" y1="83" x2="440" y2="83"/>
    <line x1="550" y1="83" x2="640" y2="83"/>
    <line x1="95" y1="106" x2="95" y2="130"/><line x1="80" y1="130" x2="110" y2="130"/><line x1="86" y1="136" x2="104" y2="136"/>
    <line x1="295" y1="106" x2="295" y2="130"/><line x1="280" y1="130" x2="310" y2="130"/><line x1="286" y1="136" x2="304" y2="136"/>
    <line x1="495" y1="106" x2="495" y2="130"/><line x1="480" y1="130" x2="510" y2="130"/><line x1="486" y1="136" x2="504" y2="136"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="95" y="88">die Tj</text><text x="295" y="88">package</text><text x="495" y="88">heatsink</text><text x="690" y="88">ambient</text>
    <text x="195" y="76" font-size="12">R0 0.5 K/W</text><text x="395" y="76" font-size="12">R1 0.5 K/W</text><text x="595" y="76" font-size="12">R2 1.5 K/W</text>
    <text x="95" y="152" font-size="12">C0 0.2 J/K</text><text x="295" y="152" font-size="12">C1 3 J/K</text><text x="495" y="152" font-size="12">C2 50 J/K</text>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="1.4">
    <rect x="40" y="200" width="150" height="50" class="f-warn" opacity=".22"/>
    <rect x="260" y="200" width="150" height="50" class="f-acc" opacity=".25"/>
    <rect x="480" y="200" width="150" height="50" class="f-mut" opacity=".25"/>
    <path d="M95 166 L95 200" marker-end="url(#a)"/>
    <path d="M190 225 L260 225" marker-end="url(#a)"/>
    <path d="M410 225 L480 225" marker-end="url(#a)"/>
    <path d="M555 200 L555 178 L140 178 L140 106" marker-end="url(#a)"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="115" y="221">TSENS (lag + offset</text><text x="115" y="239">+ 0.25 ℃ 양자화)</text>
    <text x="335" y="221">governor (DUT)</text><text x="335" y="239">10 ms 폴링</text>
    <text x="555" y="221">DVFS (V, f)</text><text x="555" y="239">PMU 레지스터</text>
    <text x="350" y="172" font-size="12">P = a·Ceff·V²·f + Pleak(T,V)</text>
  </g>
  <defs><marker id="a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0L8 4L0 8z" fill="currentColor"/></marker></defs>
  <text x="20" y="285" fill="currentColor" font-size="12">모드 시정수: 0.093 s, 1.5 s, 79.9 s (고유값 계산) — 빠른 스파이크와 느린 램프가 공존한다.</text>
</svg>
```

주요 파라미터는 전부 **대표값(가정)** 이다. 실칩 값은 패키지 데이터시트, JESD51 측정, 또는 Foster/Cauer 네트워크 피팅에서 나온다.

| 항목 | 값 | 비고 |
|---|---|---|
| OPP | L0 2000 MHz/1000 mV … L5 500 MHz/650 mV (6단계) | level 숫자가 클수록 느림 |
| 전력 | `P = a·15nF·V²·f + 0.8·V·e^{0.02(T−25)}` | a(activity): 1.0 = 일상 최대, 1.3 = power virus |
| 임계 | thr 90 / rel 84 / emg 98 / sd 105 / Tj_max 105 ℃ | throttle, release, emergency(최저 OPP로 즉시), shutdown |
| dwell | down 100 ms / up 1000 ms | 내릴 땐 빨리, 올릴 땐 천천히 |
| 센서 | 5 ms 변환, 0.25 ℃ LSB, TSENS_SEQ 카운터 | stale 감시 100 ms |

### Part A — 플랜트와 정책 checker

- `therm_dt_limit`, `therm_step`: Gershgorin 안정 조건, forward Euler 한 스텝.
- `check_policy(trace, spec, rep)`: trip·hysteresis·단계 순서·V/f 순서·emergency·shutdown 규칙을 **센서값 기준**으로 검사해 `viol()` 로 기록한다 (규칙은 시작 코드 주석 그대로). 정상 governor 시나리오 4개(sustain, hot step, warm ambient, fan 고장)는 0건, 손으로 만든 위반 trace 12종은 코드와 시각까지 정확히 나와야 한다.

### Part B — 어려운 부분: 센서 오차의 정량화, guard band

- `metrics_from_trace`: 첫 스텝 시각·그때의 true/read 온도, peak true, Tj_max 초과 시간, shutdown 시각.
- 오프셋: 느린 램프에서 첫 스텝 시점의 true 온도로 **주입 오프셋을 복원**(±0.5 ℃). lag: hot step에서 overshoot이 단조 증가. stuck: SEQ 정지(FROZEN) vs 값만 고정(VALUE).
- `guard_search`: peak ≤ Tj_max 를 만드는 최소 guard band를 0.25 ℃ 격자에서 bisect. 선형 탐색과 일치해야 한다.
- 한계를 테스트로 박아 둔다: −16 ℃ 오프셋은 **센서값만 보는 정책 checker에는 위반 0건**이다.

### Part C — Orion-S1 / EVB-07 판정 캠페인

- `check_safety`(reference 온도 기준 TJ_EXCEEDED, stale 미검출), `verdict_of`, `render_row`.
- 가상 SoC `Orion-S1` (VDD_NPU), 보드 `EVB-07`, FW 2.3.1 에서 10개 시나리오: 정상 2 + 버그 8 (hysteresis 부호, 단계 건너뜀, 단계 누락, V/f 순서, stale 미검출, shutdown 누락, 센서 오프셋, 임계 트림 오류). 각각 판정 + 첫 시그니처가 맞아야 하고, shutdown 누락은 온도가 거기까지 안 가면 **안 보인다**는 한계도 체크한다.

## 2. Problem (English)

> **Throttle-governor validation against a lumped thermal model.**
>
> You get a 3-node thermal RC plant (die, package, heatsink, ambient), a mock SoC with a temperature sensor (`TSENS_DATA`, `TSENS_SEQ`, 5 ms conversion, 0.25 °C LSB, optional offset / first-order lag / stuck faults) and PMU V/F registers, and a firmware-style governor under test, polled every 10 ms. All parameters are representative, not from a datasheet. Time step is 1 ms forward Euler.
>
> Implement in C11:
> 1. `therm_step` and `therm_dt_limit` (Gershgorin sufficient bound `1 / max_i(sum G_i / C_i)`).
> 2. `check_policy(trace, spec, report)`: from the per-poll trace and the V/F write log, report violations of the spec: first-step temperature, release temperature (hysteresis), adjacent-level steps only (except emergency/failsafe), dwell times, response deadlines, voltage-before-frequency on the way up, emergency drop to the lowest OPP, shutdown at the shutdown threshold, chatter (direction reversals per 10 s window).
> 3. `metrics_from_trace` and `guard_search`: smallest guard band `g` (0.25 °C grid, bisection) such that the true peak junction temperature stays `<= Tj_max` when the thresholds are shifted to `Tj_max − g`.
> 4. `check_safety` (true-temperature / stale-sensor checks that do not trust the sensor), `verdict_of` (PASS / FAIL_POLICY / FAIL_SAFETY) and `render_row`.
>
> Detect planted governor bugs (hysteresis sign, skipped level, dropped level, V/F order, stale sensor ignored, missing shutdown, trim error) with the right code and location.
>
> Example: sustained full load, good governor and sensor: first step at reading 90.00 °C at t = 25.87 s, no violations. In the warm-ambient scenario (Ta = 65 °C) with the release threshold computed as `thr + hyst` (sign bug), the first violation is `HYST_UP_ABOVE_REL` at L1→L0 with reading 86.00 °C.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/SIV/SIV-A04
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter; echo exit=$?
```

실제 출력 (일부):

```text
-- Part A: plant + policy checker --
FAIL  dt limit (Gershgorin) = 0.1 s
...
PASS  sustain: good governor has 0 policy violations (got 0)
...
PASS  LIMIT: reads-low 16 C is invisible to the reading-only policy checker
...
12/72 passed
exit=1
```

> 공짜 PASS 12개에 주의: `check_policy`가 아무것도 안 하면 "정상 governor는 위반 0건"이 저절로 통과한다. 그래서 손으로 만든 위반 trace 12종과 캠페인의 버그 시나리오가 있다 — **checker 자체를 먼저 검증**한다 (SIV-M08 감사기와 같은 교훈).

## 4. 힌트

<details>
<summary>힌트 1 — Euler 한 스텝: 옛 온도로 열류를 먼저</summary>

노드 갱신 **전에** `q01, q12, q2a`를 모두 계산한다. die를 먼저 갱신하고 그 새 값으로 q01을 계산하면 (Gauss-Seidel처럼) 에너지 망원합이 깨진다. 안정 조건 충분식은 `1 / max_i (ΣG_i / C_i)`: die 노드가 `(1/0.5)/0.2 = 10 1/s` 로 지배해서 0.1 s.

</details>

<details>
<summary>힌트 2 — checker의 두 층: 규칙 위반과 마감 위반</summary>

한 줄씩 보며 `d = lv_after − lv_before` 로 일어난 일을 판정하고(TRIP_EARLY, HYST…, STEP_SKIP, 그때의 dwell), "일어나야 했는데 안 일어난 일"은 **타이머 pending**으로 잡는다: 조건이 처음 성립한 시각을 기억해 두고 `down_ms + poll_ms` 를 넘기면 TRIP_LATE, 재무장(pending 갱신)해서 한 사건이 수천 건으로 불어나지 않게 한다.

</details>

<details>
<summary>힌트 3 — stale 행은 센서 규칙에서 빼라</summary>

`seq_age_ms >= stale_ms` 인 행의 read는 낡은 값이다. SD/EMG/TRIP 규칙을 그 행에 적용하면 정상 failsafe 동작을 위반으로 찍는다 (stale → 최저 OPP 점프는 d>1 인데 정상이다). 대신 한 번이라도 stale이었으면 UP_LATE 도 끈다 (failsafe latch 중에는 안 올라가는 게 맞다).

</details>

<details>
<summary>힌트 4 — V/F 순서는 trace가 아니라 쓰기 로그에서</summary>

trace에는 폴링 끝의 level만 있어서 `V 먼저 vs f 먼저`가 안 보인다. mock PMU가 레지스터 쓰기마다 `(t, V, f)` 를 로그하므로 그 로그의 모든 항목에서 `vf_safe(V, f)` 를 확인한다. `vf_safe`는 "이 f를 지탱하는 데 필요한 OPP 전압 이상인가"이다.

</details>

<details>
<summary>힌트 5 — guard band bisect: lo/hi 먼저 확인</summary>

`guard_safe(lo=0)` 이 이미 참이면 0, `guard_safe(hi=40000)` 이 거짓이면 −1. 나머지는 `TQ_MC=250` 격자에서 `mid`를 잡고 안전하면 hi=mid. hi − lo == 250 이 되면 hi를 반환한다. 격자에 안 맞는 mid를 쓰면 ‘선형 탐색 == bisect’ 체크가 깨진다.

</details>

<details>
<summary>힌트 6 — 시그니처는 첫 발생에 위치를 담는다</summary>

`viol()` 은 코드별 첫 발생의 `(시각, L_from→L_to, 값, 임계)`를 저장한다. 리포트의 `first=` 는 시간상 가장 빠른 코드를 고른다. 온도가 임계를 넘은 **시각과 전이 위치**가 있어야 디버그 엔지니어가 움직인다.

</details>

## 5. 풀이

### Part A — 플랜트와 정책 checker

**플랜트.** `therm_step` 은 세 열류를 옛 온도로 계산하고 세 노드를 동시에 갱신한다. 정상상태는 Ta + P·ΣR 이므로 20 W면 die 95/pkg 85/sink 75 ℃ — 테스트가 ±0.01 ℃로 확인한다 (2000 s = 최장 시정수의 25배, 지수 잔차가 사실상 0).

에너지 보존은 이산 수준에서 **정확**하다: 내부 열류 항이 망원합으로 사라지므로 Euler 오차와 상관없이 `ΔE = ∫P dt − ∫q_out dt` 가 부동소수 누적오차(실측 잔차 −1.48e−09 J)까지 일치한다. 이 체크는 "q를 새 온도로 계산했다"는 버그를 잡는다.

**안정 dt.** 1 ms 스텝은 해석적으로 충분히 작은가? 데모(`demo_sweep.c`)의 실측:

```text
lambda_max = 10.711 1/s -> exact Euler limit 2/lambda = 0.1867 s, Gershgorin bound = 0.1000 s
   dt[s]    T_die@20s max|dT| step
   0.001    8.989e+01    1.750e-01
   0.100    8.989e+01    1.750e+01
   0.180    8.989e+01    3.150e+01
   0.200   -9.044e+06    1.696e+07
```

| dt | 결과 |
|---|---|
| 1 ms (사용값) | 한계의 1/100. dt 격자 수렴도 0.0005 ℃ 차이 (1 ms vs 0.25 ms) |
| 0.18 s | 안정 한계 안쪽 — 수치적으로는 안정하지만 첫 스텝에 31.5 K 점프 (정확도 없음) |
| 0.20 s | **발산** (die −9e6 ℃). 2/λmax = 0.1867 s 를 넘었다 |

Gershgorin 조건 0.1 s는 고유값을 몰라도 쓸 수 있는 **보수적** 한계이고, 정확한 한계는 2/λmax다.

**왜 플랜트를 쓰는가.** 실리콘이 나오기 전에 governor 로직을 돌려 보고, 실리콘이 나온 뒤에도 "센서가 7 ℃ 틀렸으면 어떻게 되나" 같은 실험은 실칩으로 위험·비쌈 없이 하기 어렵다. 모델은 **피팅된 뒤에야** 믿을 수 있다는 단서가 붙는다 (Q6).

**임계 trace.** 슬로 램프(a=1.0, Ta 45 ℃, 120 s + 60 s 유휴)에서:

| 사건 | 시각 | 비고 |
|---|---|---|
| 첫 down-step L0→L1 | 25.87 s | reading 90.00, true 89.875 (반올림 쪽 0.125) |
| 이후 L2까지 | — | 90 근처에서 멈춤, peak 89.89 (L2 유지, warm ambient 시나리오에서는 L4까지) |
| 120 s 이후 유휴 | up-step | read < 84 인 동안 up dwell 1 s 마다 한 단계, 끝은 L0 |

```svg
<svg viewBox="0 0 760 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="20" y="22" fill="currentColor" font-weight="bold">임계 사다리와 hysteresis (개념도 — 실측 trace 아님)</text>
  <g stroke="currentColor" stroke-dasharray="4 4" opacity=".6">
    <line x1="90" y1="50" x2="720" y2="50"/><line x1="90" y1="90" x2="720" y2="90"/><line x1="90" y1="130" x2="720" y2="130"/><line x1="90" y1="170" x2="720" y2="170"/>
  </g>
  <g fill="currentColor" text-anchor="end" font-size="12">
    <text x="84" y="54">sd 105</text><text x="84" y="94">emg 98</text><text x="84" y="134">thr 90</text><text x="84" y="174">rel 84</text>
  </g>
  <path d="M90 230 C150 200 210 145 270 131 C300 126 310 150 340 165 C370 178 400 180 430 170 C460 160 480 140 510 132 C540 126 560 160 600 185 C640 205 680 218 720 228" fill="none" stroke="currentColor" stroke-width="2"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="270" y="120">thr 넘음 → down</text>
    <text x="430" y="196">rel 아래에서 가만히 → up</text>
    <text x="510" y="120">다시 thr</text>
    <text x="600" y="204">부하 끝</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5" transform="translate(0,14)">
    <path d="M90 262 L270 262 L270 248 L340 248 L340 262 L430 262 L430 248 L520 248 L520 262 L720 262"/>
  </g>
  <text x="90" y="300" fill="currentColor" font-size="12">아래 계단 = DVFS level. thr 과 rel 사이(hysteresis 6 ℃)에서는 아무것도 하지 않는다 → chatter 없음.</text>
</svg>
```

**checker 규칙과 그 이유.**

| 규칙 | 잡는 것 | 왜 필요한가 (실제 버그) |
|---|---|---|
| TRIP_EARLY / TRIP_LATE | 임계 틀림, 스텝 드롭 | 트림값 오류, 폴링 누락, 인터럽트가 안 와서 throttle이 늦는 사고 |
| HYST_UP_ABOVE_REL | release 임계 틀림 | hysteresis 부호·단위 버그의 전형적 증상 |
| UP_DWELL / DOWN_DWELL | 타이머 문제 | 디바운스 타이머가 리셋 안 되는 등 |
| STEP_SKIP | 표 인덱스 산술 오류 | OPP 테이블이 비거나 인덱스 +2 |
| VF_ORDER | 전압 먼저 올리기 위반 | 저전압에서 고주파로 타이밍 위반 — 간헐 hang 의 대표 원인 |
| EMG_LATE / SD_MISSED / SD_EARLY | 안전 경로 | 비상 경로가 코드에만 있고 동작 안 함 |
| CHATTER | 튜닝·hysteresis 문제 | OPP 전이 비용·전압 레일 노이즈·성능 손실 |
| UP_LATE | 영원히 안 돌아오는 throttle | 성능 버그. latch 된 상태에서 복귀 경로 없음 |

**V/F 순서를 왜 따로 보나.** 올라갈 때는 **전압 먼저, 주파수 나중**, 내려갈 때는 **주파수 먼저, 전압 나중**이어야 한다. 순서가 뒤집히면 잠깐 "높은 f + 낮은 V" 상태가 되어 타이밍 위반이다 (PMIC 응답이 µs~ms라 그 창이 실제로 존재한다). 이 문제는 폴링 끝 상태만 봐서는 절대 안 보이고 레지스터 쓰기 로그에서만 보인다.

**CHATTER 는 왜 카운트로 잡나.** 데모(warm ambient, Ta 65 ℃, 120 s) 실측, 10 s 창 안의 방향 전환 최댓값:

```text
case                    max_rev  changes  chatter
good (hyst 6 C)               0        4        0
hysteresis sign bug          20      198      148
good but hyst 1 C            19      124      105
good but hyst 0.5 C          20      172      142
good, Ta=62 (limit cycle)        1        9        0
```

Ta 62 ℃ 줄은 **정상적인 느린 limit cycle** (300 s에 9번 변경, 10 s 창에 방향 전환 최대 1): L3와 L4의 정상상태 온도가 thr/rel 대역 양쪽에 있어서(계산값) 생긴다. 반면 hysteresis 1 ℃는 논리는 맞아도 10 s에 19번 방향이 바뀐다. 그래서 "up-step이 rel 아래에서 일어났나"만이 아니라 **방향 전환 수**를 본다.

### Part B — 센서 오차의 정량화

**오프셋은 trip 온도를 1:1로 옮긴다.** 느린 램프(기울기 ≈ 1 K/s 미만)에서 첫 스텝 시점의 true 온도로 주입 오프셋을 복원했다. 허용오차 ±0.5 ℃ = 양자화 2 LSB:

| 주입 오프셋 | 복원값 (thr − true@trip) |
|---|---|
| 0 | +0.12 |
| −2 ℃ | −1.88 |
| −5 ℃ | −4.88 |
| −8 ℃ | −7.88 |
| +3 ℃ | +3.12 |

+0.12는 반올림 양자화 때문이다(0.25 ℃ LSB: 실제 89.875 ℃에서 90.00으로 읽힘). 실리콘에서도 이 방법이 표준이다: **천천히 램프해서 (기울기 × 폴링 주기) ≪ 분해능으로 만들고 trip 시점의 기준 온도를 읽는다.** 빠른 램프에서 trip을 재면 측정이 lag와 섞인다.

**time-to-throttle 지연은 비선형이다.** 데모 실측 (센서 오프셋 → 첫 스텝 시각):

```text
== T2: time-to-throttle vs sensor offset (sustain a=1.0, Ta=45, from 45 C) ==
  ofs[C]  first_dn[s]   delay[s]    true@dn    peak[C]    over[s]
     5.0        16.30      -9.57      84.88      84.88        0.0
     0.0        25.87      +0.00      89.88      89.89        0.0
    -2.0        30.00      +4.13      91.88      91.88        0.0
    -5.0        36.55     +10.68      94.88      94.89        0.0
    -8.0        43.59     +17.72      97.88      97.88        0.0
   -10.0        48.59     +22.72      99.88      99.88        0.0
   -15.0        62.36     +36.49     104.88     104.88        0.0
   -20.0        78.41     +52.54     109.88     109.88       30.9
   -30.0        never        inf          -     119.62       57.5

== T2: time-to-throttle vs sensor offset (part load a=0.70, Ta=45, from 45 C) ==
  ofs[C]  first_dn[s]   delay[s]    true@dn    peak[C]    over[s]
     5.0        53.37     -22.28      84.88      84.88        0.0
     0.0        75.65      +0.00      89.88      89.88        0.0
    -2.0        86.36     +10.71      91.88      91.88        0.0
    -5.0       105.27     +29.62      94.88      94.88        0.0
    -8.0       129.17     +53.52      97.88      97.88        0.0
   -10.0       149.46     +73.81      99.88      99.88        0.0
   -15.0       237.68    +162.03     104.88     104.88        0.0
   -20.0        never        inf          -     106.41       58.5
```

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="20" y="22" fill="currentColor" font-weight="bold">오프셋 → time-to-throttle 지연 (실측, 위 표)</text>
  <g stroke="currentColor" stroke-width="0.5" opacity=".35">
    <line x1="90" y1="290" x2="720" y2="290"/><line x1="90" y1="217.5" x2="720" y2="217.5"/><line x1="90" y1="145" x2="720" y2="145"/><line x1="90" y1="72.5" x2="720" y2="72.5"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="82" y="294">0 s</text><text x="82" y="221">50 s</text><text x="82" y="149">100 s</text><text x="82" y="76">150 s</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="90" y="312">0</text><text x="240" y="312">−5</text><text x="390" y="312">−10</text><text x="540" y="312">−15</text><text x="690" y="312">−20 ℃</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="90,290.0 150,284.0 240,274.5 330,264.3 390,257.1 540,237.1 690,213.8"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" points="90,290.0 150,274.5 240,247.1 330,212.4 390,183.0 540,55.1"/>
  <g class="f-ok"><circle cx="540" cy="237.1" r="3"/><circle cx="690" cy="213.8" r="3"/></g>
  <g class="f-warn"><circle cx="540" cy="55.1" r="3"/></g>
  <text x="600" y="216" fill="currentColor" font-size="12">a=1.0 (실선): 끝까지 일정</text>
  <text x="400" y="60" fill="currentColor" font-size="12">a=0.70 (점선): 점근선 근처에서 폭증</text>
  <text x="560" y="46" fill="currentColor" font-size="12">−20 ℃: 영원히 안 걸림(∞)</text>
  <text x="20" y="334" fill="currentColor" font-size="12">가로축 = 센서 오프셋(읽는 값이 실제보다 낮음). 같은 −5 ℃ 가 부하에 따라 지연 10.7 s 또는 29.6 s.</text>
</svg>
```

해석: 온도는 지수로 점근선 `T∞ = Ta + P·ΣR` 에 다가간다. trip 임계까지 올라가는 시간 `t = −τ·ln(1 − (T−T0)/(T∞−T0))` 는 임계가 점근선에 가까울수록 **로그 발산**한다 (추가 연습 1). 부분 부하에서는 점근선(300 s 시점 peak 106.4 ℃)이 임계 근처에 있으니 −15 ℃ 오프셋은 trip을 162 s 늦추고, −20 ℃에서는 **아예 걸리지 않고** 106 ℃를 넘겨 58.5 s 머문다. "센서가 낮게 읽으면 throttle이 얼마나 늦나"에 대한 답은 숫자 하나가 아니라 **부하와 점근선의 함수**다.

**Tj_max 초과량.** −15 ℃까지는 peak가 104.88로 Tj_max 안쪽이지만 (센서가 thr에서 멈춰 줌) −20 ℃ 에서는 peak 109.88, 30.9 s 초과. 보호 구조상 초과량 ≈ `peak − Tj_max = (thr − offset) + overshoot − Tj_max`.

**lag는 overshoot을 만든다.** hot step (a=1.3, die 84 ℃, thr 6 ℃ 아래에서 출발, 처음 40 ms에 6.7 K 상승):

```text
 lag[ms] first_dn[ms]    true@dn    peak[C]  peak-thr[K] max_level
       0           40      90.72      95.94         5.94 L5
      20           60      93.16      96.94         6.95 L5
      50           80      95.18      97.81         7.81 L5
     100          110      97.60      98.88         8.88 L5
     300          200     102.09     102.09        12.09 L5
     500          270     104.03     104.03        14.03 L5
```

첫 스텝이 늦어지는 만큼 true가 더 올라간다 (`overshoot ≈ 기울기 × (lag + poll/2 + conv)`). lag 0 ms에서도 overshoot 5.94 ℃인 것은 **스텝 하나로는 물리를 못 이긴다**는 뜻이다: down-step 하나가 전력을 몇 W만 줄이고, die 모드 시정수는 0.09 s라서 다음 스텝까지 온도는 계속 오른다. lag 100 ms 이상에서는 true peak가 emergency 임계 98 ℃를 넘는다 (98.88 → 102.09 → 104.03 ℃) — 이 시나리오의 마진은 lag 하나로도 다 쓸 수 있다는 경고다.

**stuck: FROZEN vs VALUE.** 5 s에 고착 (slow ramp) 데모 실측:

```text
case                           fsafe_ms    peak[C]    over[s]  verdict
none                                  0      89.89        0.0     PASS
frozen(seq stops) good gov         5100      77.73        0.0     PASS
frozen, no stale check                0     119.62       57.5 FAIL_SAFETY
value stuck, seq live                 0     119.62       57.5 FAIL_SAFETY
```

- FROZEN: 변환이 멈춰 SEQ가 안 변한다 → stale 감시 100 ms로 `5100 ms`에 최저 OPP (latch). 시퀀스 번호 감시는 **이 고장 모드 하나**를 잡는다.
- VALUE: SEQ는 도는데 값만 고정. **SEQ 감시로는 절대 안 보인다.** throttle 없이 peak 119.6 ℃, 57.5 s 초과. 해결은 시퀀스 번호가 아니라 **교차 타당성**이다: 다른 센서와의 차이 (추가 연습 4), 또는 "전력이 크게 변했는데 온도 미분이 0이다" 같은 물리 일관성.

**guard band.** "thr를 Tj_max보다 얼마나 낮게 둬야 하나"는 시나리오로 계산한다 (`guard_search`, 구조는 rel=thr−6, emg=thr+8, sd=thr+15 고정, 초기 온도도 함께 이동):

```text
hot step (a=1.3, starts 6 C below thr):
   ofs\lag       0ms      20ms     100ms     300ms
       0.0      6.25      7.25      9.25     12.25
      -2.0      6.75      7.75     10.00     13.75
      -5.0      8.25      8.75     11.50     15.25
     -10.0     10.25     11.25     14.50     17.50
slow ramp (sustain, 60 s):
   ofs\lag       0ms      20ms
       0.0      0.00      0.00
      -2.0      2.00      2.00
      -5.0      5.00      5.00
     -10.0     10.00     10.00
```

| 관찰 | 이유 |
|---|---|
| 느린 램프: guard = \|offset\| 정확히 1:1 | 오프셋이 true 임계를 그대로 민다. 체크로 고정: −7 ℃ → guard +7.00 |
| hot step, offset 0: guard 6.25–12.25 (lag만의 항) | overshoot = 물리 + lag |
| hot step: offset −10 인데 guard는 +4 뿐 (lag 0 기준) | 오프셋이 크면 trip이 die 지수 곡선의 더 높은(기울기가 작은) 지점에서 걸려서 overshoot 항이 줄어든다. **단순 합산 오류: "guard = 오프셋 + overshoot" 를 그대로 더하면 hot step 에선 과잉, 느린 램프에선 정확** |
| shutdown 경로 | sd−offset = peak (fan 고장, 아래) |

fan 고장 시 (Rsa 20배) shutdown 지점 = reading 임계 그대로, true는 오프셋만큼 어긋남:

```text
  ofs[C]    sd_read    peak[C]     shut@[s]
     0.0      105.0     104.88        217.5
    -2.0      105.0     106.88        241.1
    -5.0      105.0     109.88        249.6
```

**guard band에는 비용이 있다** (sustain 120 s, work = Σf·a·dt 정규화):

```text
  thr[C]    work[%]     peak     maxL
   105.0      100.0   104.88        1
   100.0       96.9    99.88        2
    95.0       91.0    94.89        2
    90.0       86.6    89.89        2
    85.0       82.9    84.88        2
    80.0       74.1    79.88        3
```

thr 1 ℃ 내릴 때마다 대략 **0.6~1.8%(평균 약 1%)의 지속 성능**을 낸다 (이 모델·이 부하에서). 그래서 스펙의 ±센서 정확도를 정직하게 재서 줄이는 것이 그 자체로 제품 성능이다 (Q1).

**방법이 못 보는 것 — 측정으로.**

| 사각지대 | 실측 | 메우는 방법 |
|---|---|---|
| 일정 오프셋, 센서값만 보는 checker | −16 ℃: 정책 위반 **0건**, 그러나 true peak 106.0 ℃ (TJ_EXCEEDED) | 기준 온도(외부 프로브/다이 내 다이오드), 챔버 soak |
| 값만 고착 + SEQ 살아 있음 | SEQ 감시 무력화, peak 119.6 ℃ | 교차 센서, 물리 일관성 |
| 모든 센서의 공통모드 오프셋 | 교차 검사도 무력 (추가 연습 4) | 칩 외부 기준 필요 |
| 짧은 버스트 | 50 ms 버스트: true 91.91, read 87.75 (miss 4.16 K), throttle 안 걸림 | lag 작은 센서 위치·감지 경로, 더 짧은 폴링, 시뮬레이션으로 사양화 |
| 실행 안 된 경로 | BUG_NO_SD 는 온도가 105까지 안 가면 0건 (Part C 한계 체크) | fan-fail 지향 시나리오, 코버리지 추적 (SIV-M13) |

짧은 버스트 표 (die 80 ℃, 버스트 a=2.0, 센서 lag 50 ms):

```text
   burst  peak_true  peak_read    miss[K] throttled
     5ms      81.45      81.00       0.45       no
    20ms      85.47      83.25       2.22       no
    50ms      91.91      87.75       4.16       no
   100ms      98.64      94.00       4.63      yes
```

### Part C — Orion-S1 판정 캠페인

실행 결과 (rule: check_policy + check_safety, spec 90/84/98/105/105):

```text
=== Thermal governor verdict: SoC Orion-S1 (VDD_NPU) board EVB-07 fw 2.3.1 ===
spec thr/rel/emg/sd/Tjmax = 90.0/84.0/98.0/105.0/105.0 C
S0_good          PASS        peakTj=89.9C throttle@25.87s n_viol=0 first=-
S1_hyst_sign     FAIL_POLICY peakTj=92.3C throttle@2.76s n_viol=245 first=HYST_UP_ABOVE_REL t=3.76s L1->L0 val=86.00 lim=84.00
S2_step_skip     FAIL_POLICY peakTj=89.9C throttle@2.76s n_viol=1 first=STEP_SKIP t=36.54s L2->L4 val=90.00 lim=0.00
S3_step_drop     FAIL_POLICY peakTj=97.9C throttle@2.76s n_viol=325 first=TRIP_LATE t=36.66s L2->L2 val=90.00 lim=90.00
S4_vf_order      FAIL_POLICY peakTj=89.9C throttle@25.87s n_viol=4 first=VF_ORDER t=27.30s L0->L0 val=2.00 lim=0.93
S5_stale_undet   FAIL_SAFETY peakTj=119.6C throttle@none n_viol=23243 first=STALE_NOT_DETECTED t=5.11s L0->L0 val=0.12 lim=0.11
S6_stale_ok      PASS        peakTj=77.7C throttle@5.10s n_viol=0 first=-
S7_no_shutdown   FAIL_SAFETY peakTj=121.1C throttle@0.02s n_viol=36343 first=SD_MISSED t=217.54s L5->L5 val=105.00 lim=105.00
S8_offset_-16    FAIL_SAFETY peakTj=106.0C throttle@0.38s n_viol=33 first=TJ_EXCEEDED t=0.32s L0->L0 val=105.03 lim=105.00
S9_thr_trim      FAIL_POLICY peakTj=92.9C throttle@32.13s n_viol=179 first=TRIP_LATE t=25.99s L0->L0 val=90.00 lim=90.00
```

(n_viol은 폴링 단위로 누적된다. VF_ORDER 행의 val/lim 은 GHz/V로 읽는다: S4는 f=2.00 GHz, V=0.93 V; STALE 행의 val/lim 은 초 단위: 0.12 s ≥ 0.11 s.)

**시그니처 → 디버그 시작점.**

| 시그니처 | 스토리 | 디버그 엔지니어의 첫 손 |
|---|---|---|
| `HYST_UP_ABOVE_REL` + CHATTER (148창) | release 를 잘못 계산, rel 위에서 올라감 | governor 소스의 hysteresis 산술, DT 의 `hysteresis`/sysfs `trip_point_N_hyst` 확인 |
| `STEP_SKIP L2->L4` | 정상 상태에선 건너뛰기만 | OPP 테이블과 인덱스 산술 (+2 또는 빈 항목) |
| `TRIP_LATE L2->L2`, peak 97.9 | down-step 요청이 사라짐 | PMU/governor 요청 경로, 전이 중 요청 무시 로직, 큐 드롭 |
| `VF_ORDER` t=27.3 s | up path 순서 | 레일 설정 코드 순서, PMIC settle 대기 |
| `STALE_NOT_DETECTED` | 센서 시퀀스 감시 없음 | TSENS 클럭/전원, ISR, 감시 코드 존재 여부 |
| `SD_MISSED`, peak 121 | shutdown 경로 없음 | 안전 경로 전체 + **독립 HW trip(THERMTRIP류)** 확인 |
| `TJ_EXCEEDED` (정책 0건) | 센서 측 문제 | 챔버 soak로 trim 확인 — 먼저 센서를 의심 |
| `TRIP_LATE L0->L0` (thr 93) | 설정값 문제 | fuse/DT 의 trip 온도 트림 |

**판정 구조.** `FAIL_SAFETY`(TJ_EXCEEDED / SD_MISSED / STALE_NOT_DETECTED)는 하드웨어·제품 위험이라 바로 스톱, `FAIL_POLICY`는 스펙 불일치로 수정 후 재검증이다. S6 은 stale 고장을 **제대로 처리한 것**이므로 PASS다 — 고장이 났다고 FAIL 이 아니라, **검증 대상이 고장에 올바르게 반응했는가**가 판정이다.

**캠페인이 돌아가게 하려면.** 시나리오 10개 중 절반(S2, S3, S7 등)은 평상 부하로는 절대 도달하지 않는 지점을 찌른다 (warm ambient로 L2까지 몰아가기, fan 고장으로 shutdown 도달). SIV-M13의 커버리지 관점: 이 캠페인의 함수적 커버리지(coverpoint)는 **어떤 전이를 실제로 밟았는가**다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution; echo exit=$?
```

실제 출력 (마지막 줄, Part 구분 줄과 일부 생략):

```text
PASS  dt limit (Gershgorin) = 0.1 s
PASS  energy balance over 100 s (residual -1.48e-09 J)
PASS  fan failure: shutdown stops the climb at 105 C (peak 104.88, tolerance 0.5 = 2 LSB)
PASS  LIMIT: reads-low 16 C is invisible to the reading-only policy checker
PASS  guard band ofs -10 C lag 100 ms: bisect 14.50 C == linear 14.50 C
PASS  LIMIT: BUG_NO_SD is invisible while the workload never reaches shutdown temp (peak 89.9 C)
72/72 passed
exit=0
```

ASan/UBSan 빌드(`-fsanitize=address,undefined`)도 깨끗하게 `72/72 passed`. 측정표는 `demo_sweep.c` 에서 (Apple clang 21, arm64): `cc -std=c11 -Wall -Wextra -O2 demo_sweep.c -o demo_sweep -lm && ./demo_sweep`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고침 |
|---|---|---|
| `q01` 계산 전에 `t[0]` 를 갱신 | 에너지 망원합이 깨진다. 값은 그럴듯해서 안 보인다 | 세 열류를 옛 값으로 먼저 계산 |
| checker가 true 온도와 reading을 섞어 씀 | "정책"과 "안전"이 구분 안 돼서 오프셋이 정책 위반처럼 찍히거나 반대로 안 보임 | 정책 = read, 안전 = reference |
| TRIP_LATE 재무장 안 함 | 사건 한 번이 수천 건, 또는 첫 번째만 잡고 다음 스텝 지연을 놓침 | 조건 지속 시 `pend = t` 로 재무장 |
| stale 행에도 SD/EMG 규칙 적용 | 정상 failsafe 가 위반으로 찍힘 | `seq_age >= stale_ms` 면 센서 규칙 제외 |
| V/F 를 trace 끝값으로만 검사 | 전이 중간의 "높은 f + 낮은 V" 는 trace에 없다 | PMU 쓰기 로그에서 `vf_safe` |
| 첫 스텝을 "90 넘은 시각"으로만 측정 | 빠른 램프에서 lag과 섞여 임계를 잘못 판단 | 느린 램프 + 기준 온도 |
| 오프셋을 "일정 시간 지연"으로 환산 | 지연은 점근선 근처에서 로그 발산 — 표의 −15 ℃ 부분 부하 | 부하별로 시뮬레이션/측정 |
| SEQ만 보고 센서 건강하다고 판단 | 값만 고착된 stuck은 잡지 못함 | 교차 센서, 물리 일관성 체크 |
| guard band 를 오프셋 + overshoot 으로 합산 | 비선형: 큰 offset 에선 overshoot 항이 작아짐 (점근선·emergency) | 시나리오별 시뮬레이션 bisect로 구하기 |
| Hot 트립을 소프트웨어로만 | 펌웨어가 hang 이면 shutdown 없음 | 아날로그/독립 HW 트립(THERMTRIP 류) |

## 8. 면접 Q&A

<details>
<summary>Q1. 온도 센서 오프셋이 ±5 ℃라면 왜 위험하고, 양산에서 그 오프셋을 어떻게 줄이나요?</summary>

낮게 읽는 쪽이 위험하다 — throttle이 진짜 온도 thr + 5에서 걸린다. 느린 램프에서는 그대로 1:1로 Tj 임계를 밀고 (표 T2, guard band −5 → 5.00), 동시에 trip까지의 시간이 늘어 그 시간 동안 더 뜨겁다. 높게 읽는 쪽은 성능 손실(1 ℃당 평균 약 1%, 표 T6)이고 안전 문제는 아니다.

줄이는 법: **챔버 soak 후 1점/2점 트림**을 fuse에 기록한다. idle로 전력을 거의 없앤 뒤 soak해서 다이 온도 ≈ 챔버 온도로 만들고 읽어서 차이를 트림 값으로 굽는다. 꼬리질문 "soak을 했는데 왜 어긋나죠?" → **자기 발열**: idle이라도 전력이 있고 `ΔT = P·R`이 그대로 오프셋 편향으로 들어간다 (추가 연습 3: 2 W × 2.5 K/W → +5 K 편향). 그래서 전력을 최소화하거나 R을 재서 빼 준다.

꼬리질문 "트림 후에도 남는 것은?" → 곡률(추가 연습 3의 중간점 잔차), 센서-핫스팟 거리에 따른 **전력 의존 오프셋**(트림은 저전력에서 하고 위험은 고전력에서 온다), 패키지 응력·수명 드리프트. 트림은 잔차를 없애지 않는다 — 잔차 예산이 guard band다.

</details>

<details>
<summary>Q2. hysteresis 는 얼마로 잡나요? 왜 없으면 안 되나요?</summary>

핵심 기준: thr에서 한 단계 내려 간 OPP의 정상 온도보다 rel이 아래에 있으면 시스템은 그 단계에 머문다. `hyst_min ≈ thr − Tss(내린 OPP)` (추가 연습 5에서 7.50 K를 스캔으로 확인). 그보다 작으면 두 레벨 사이에서 limit cycle을 돈다 (데모 hyst 1 ℃: 10 s에 19번 방향 전환).

꼬리질문 "그럼 hysteresis를 크게?" → 재복귀가 늦어져 성능을 버린다. 크게 + up dwell로 **비대칭**(내릴 땐 빨리, 올릴 땐 천천히) 하는 게 보통이다. 또 그 **크기의 한계**는 튜닝 문제고, 부호·단위·보수 오류는 버그다 — 이 레슨 S1처럼 `rel`을 `thr + hyst`로 계산하면 hold 대역 없이 thr 근처에서 레벨이 계속 바뀐다 (warm 시나리오 실측 120 s에 198번 변경). 꼬리질문 "Linux에서는?" → DT 트립의 `hysteresis` 속성, `step_wise` 또는 `power_allocator` governor, 그리고 `polling-delay-passive`로 hot일 때 폴링을 빠르게.

</details>

<details>
<summary>Q3. DVFS 전이에서 왜 올릴 때 전압 먼저, 내릴 때 주파수 먼저인가요? 검증은요?</summary>

주어진 전압에서 낼 수 있는 fmax가 있다. 올라갈 때 f를 먼저 올리면 잠깐 (높은 f, 낮은 V) 상태로 타이밍 위반이다. 내려갈 땐 반대로 V를 먼저 내리면 같은 위반. 그래서 **V↑ → f↑**, **f↓ → V↓**. 검증은 폴링 끝의 level로는 불가능하고 **레지스터 쓰기 순서**를 봐야 한다 (`VF_ORDER`, S4). 실칩에서는 PMIC/레귤레이터 커맨드 로그와 V 레일을 오실로스코프로 보는 것을 같이 쓴다.

꼬리질문 "PMIC 응답이 늦으면?" → V가 목표에 도달한 뒤 f를 올려야 하므로 settle 대기(또는 PMBus 상태 폴링) 필요. 전이 중 glitch를 잡으려면 di/dt, 전압 드룹을 같이 측정한다. 꼬리질문 "이 레슨의 V/f 체크가 놓치는 것은?" → 전압이 **실제로** 도달했는지 (mock 은 즉시 도달), 그리고 그 사이의 타이밍 마진 — 전압 ramp 는 레일 측정이나 SIV-M07 shmoo 의 몫이다.

</details>

<details>
<summary>Q4. checker를 센서값 기준으로만 만들면 뭘 놓치나요? 실리콘엔 true 온도가 없잖아요.</summary>

센서 자체의 오류. −16 ℃ 오프셋에서 정책 위반은 0건이지만 true는 Tj_max를 넘는다 (데모/체크). 그래서 두 층으로 나눴다: **정책 = 펌웨어가 자기 눈에 충실한가**, **안전 = 현실이 괜찮은가**.

실리콘의 기준 온도는: 외부 다이오드/열전대를 패키지 표면에 붙여 측정(그대로 Tj 가 아님 — ΨJT 또는 ΘJC 모델로 환산), 챔버 soak(열평형에서 다이 온도 ≈ 챔버 온도), 칩 내 다이오드(TSP: 온도 민감 파라미터) 전용 측정 경로, 또는 교차 센서. 꼬리질문 "soak 온도와 동작 중 온도 차는?" → soak에선 전력 ≈ 0으로 오프셋만 보이지만 동작 중엔 전력 의존 오프셋이 더해진다. 그래서 reference를 고전력 조건에서도 **한 번은** 측정한다.

</details>

<details>
<summary>Q5. 센서가 고장 나면 펌웨어는 어떻게 알죠? 시퀀스 번호로 다 되나요?</summary>

고장 유형을 나눈다. (1) 변환 정지/클럭 게이트 → SEQ 동결 → 타임아웃으로 잡힌다 (5.10 s failsafe). (2) 값만 고착, 변환은 정상 → SEQ 로는 못 잡는다 (이 레슨의 측정 한계: peak 119.6 ℃). (3) 범위 밖 값(0, 전부 1) → 범위 검사. (4) 이상한 변화율 → 물리 일관성: 1 ms에 20 ℃ 변화는 불가능. 2개 이상의 센서가 있으면 교차 검사(추가 연습 4)로 (2)도 잡지만, 같은 레퍼런스·ADC를 공유하면 **공통모드 오프셋**은 여전히 못 잡는다.

꼬리질문 "failsafe에 들어가면 어떻게 복귀?" → 이 레슨은 latch(부팅 전까지 복귀 안 함). 제품은 센서 복구 후 재시도 가능하지만 latch 해제 조건을 명시해야 한다 — 그렇지 않으면 chatter 를 만든다. 꼬리질문 "왜 임계값은 EEPROM 말고 OTP/fuse?" → 조작·드리프트, 부팅 이전 경로, 사고 시 이력.

</details>

<details>
<summary>Q6. 이 RC 모델을 왜 믿어야 하나요? 실제 칩에서 어떻게 맞추죠?</summary>

믿기 전에 **피팅**한다. 다이에 전력 계단을 주고 온도 응답(센서 또는 다이오드)을 기록해 시정수와 ΣR을 맞춘다. JEDEC의 과도 열측정(JESD51 계열) 방법이 표준이고 Foster 네트워크는 수학적 피팅에, Cauer 네트워크는 물리 사다리 표현에 쓴다. 모델 구조는 내 레슨의 3노드가 최소형이고, 실제는 다이 안에 핫스팟별로 노드를 더 두며 hot-spot 그리드 모델까지 간다. SIM-M08 이 그 일을 맡는다.

꼬리질문 "노드 수는 몇 개?" → 시간 영역 요구로 정한다. 스파이크 → 빠른 die 노드, 분 단위 지속 → 패키지와 방열판 노드. 꼬리질문 "모델이 틀렸는지 어떻게 아나?" → 예측 vs 측정 잔차를 **보지 않은 전력 프로파일**(마지막 버스트 트레인)에서 검증한다. 잔차가 임계 마진의 일부에 들어와야 guard band 계산에 쓴다.

</details>

<details>
<summary>Q7. 이걸 진짜 칩으로 한다면 시간·장비·리스크는? 가장 먼저 무엇을 돌리나요?</summary>

장비: 챔버 또는 Thermal head(소켓 강제 온도), 외부 온도 프로브, 전력 측정(SIV-A03), 부하 생성기(power virus, NPU/CPU 조합), 로그. 시간은 **soak 시간이 지배**한다 (SIV-M08). 칩 수 × 온도 × 시나리오를 다 돌릴 수 없으므로 초기는 **typ 칩 소수, 임계 근처 지향 시나리오**로 시작한다.

순서: (1) 센서 해석/트림 sanity(soak 2점) → (2) 상온 throttle 동작(slow ramp, trip 정확도) → (3) hot step(power virus로 overshoot) → (4) 안전 경로 **자체를 시험**: 온도를 실제로 trip까지 올리거나, 센서를 강제 override 레지스터로 임계값을 먹여서 시험 — 실제 Tj_max 근처 스트레스는 수명을 쓰는 비싼 시험이라 override가 싸다. 꼬리질문 "override로 시험한 게 진짜를 증명하나?" → 판단 로직은 증명하지만 센서 경로는 증명하지 않는다. 그래서 override 시험 + 실제 온도 시험을 소량 병행한다. (5) 코너(SS/FF, 고온) + 양산 스크린 항목으로 정리(SIV-M01, M08).

</details>

<details>
<summary>Q8. governor가 hang 나면 칩은 어떻게 살아남나요?</summary>

소프트웨어 트립만 있으면 hang 시 무방비이므로, 보통 **독립 하드웨어 경로**를 둔다: 온도 비교기가 임계를 넘으면 PMU/클럭 컨트롤러가 강제 스로틀 또는 전원 차단(Intel 계열의 PROCHOT# / THERMTRIP# 이 잘 알려진 예). 검증 항목: 펌웨어를 멈춘 상태에서도 트립이 동작하는가, 임계가 펌웨어 trip보다 위에 있어서 평소엔 안 나오는가, 트립 후 재부팅 때 latch 상태를 어떻게 처리하는가. 꼬리질문 "이 레슨의 모델은 이걸 다루나?" → S7은 펌웨어 shutdown 경로만 다룬다. 독립 경로는 플랜트 쪽에 `hw_trip` 비교기를 추가하고 governor에 BUG를 넣어 시험하면 된다 (추가 과제).

</details>

## 9. 추가 연습

### 연습 1 — 1노드 해석해로 trip 시각 계산

1노드 플랜트(T0, T∞, τ)에서 임계 `T`에 도달하는 시각을 해석식으로 구하고 Euler와 비교하라. 같은 5 ℃ 오프셋이 임계 위치에 따라 지연을 얼마나 키우는지 보여라.

<details>
<summary>답 보기</summary>

`t = −τ·ln((T∞−T)/(T∞−T0))`, `T ≥ T∞` 면 ∞. T0=45, T∞=120, τ=90 s: 90 ℃ 도달 82.47 s(Euler 82.47), 95 ℃ 98.88 s. 같은 5 ℃ 지연이 90→95 ℃ 구간에서 16.4 s, 110→115 ℃ 구간에서 62.4 s로 3.8배. §5의 부분 부하 곡선과 같은 이유다.

</details>

### 연습 2 — 누설을 포함한 열폭주 임계 Rth

`T = Ta + R(Pd + P0·e^{k(T−25)})` 의 해가 존재하는 최대 R을 고정점 반복 bisect와 접선 조건으로 구해 비교하라.

<details>
<summary>답 보기</summary>

해가 사라지는 지점은 곡선이 접할 때: `R·P0·k·e^{k(T*−25)} = 1`, `T* = Ta + R·Pd + 1/k`. Pd=30 W, P0=0.8 W, k=0.02: 고정점 스캔 **2.8267 K/W**, 접선 조건 2.8267 K/W. 공칭 R=2.5면 139.9 ℃에서 정착, 열 루프 이득 `R·dPleak/dT = 0.398 < 1`. Rth가 13% 높아지면(열저항 악화) 열 폭주다: 팬 먼지, TIM 열화 같은 **수명 항목이 guard band를 먹는다.** 이 레슨의 governor는 Tj 임계에서 차단하므로 안전하지만, 스로틀 설계 시 이득 1에 대한 여유를 명시해야 한다.

</details>

### 연습 3 — 2점 트림과 자기 발열 편향

`reading = true·(1+g) + o` 를 2점 soak로 복원하라. soak 중 자기 발열로 다이가 챔버보다 뜨거울 때, 곡률이 있을 때의 잔차를 계산하라.

<details>
<summary>답 보기</summary>

이상적인 soak: `g = 0.0120, o = −2.500` 복원. 자기 발열 ΔT=5 K를 무시하고 챔버 온도를 정답으로 쓰면 **오프셋이 +5.060 K 편향**(= ΔT·(1+g)), gain은 안 변한다 — 이 5 K는 그대로 guard band에 들어와 그대로 조용히 잘못된다. 곡률 `c=6e−4`이면 2점 트림의 잔차는 중간(65 ℃)에서 **−0.960 K**. 곡률을 잡으려면 3점 또는 곡률 항을 제품 특성화에서 고정 계수로 넣는다.

</details>

### 연습 4 — 교차 센서 타당성 검사

센서 A와 B의 차이가 `lim` 을 `persist` 번 연속 초과하면 고장으로 판정하는 `cross_check` 를 작성하라. 값만 고착된 A는 잡고, 공통모드 오프셋은 못 잡음을 보여라.

<details>
<summary>답 보기</summary>

연속 카운터, 초과 시 +1, 아니면 0, `>= persist` 면 인덱스 반환. A가 샘플 200부터 고착되고 온도가 계속 오르면 차이가 3 ℃를 넘는 샘플 301 뒤 persist 5로 **305에서** 플래그 — 값이 변하지 않으니까 **온도가 움직일 때만** 잡힌다는 점에 주의. 공통모드 −10 ℃(둘 다 같은 레퍼런스 이상)는 아무것도 안 보인다.

</details>

### 연습 5 — chatter 없는 최소 hysteresis

1노드 플랜트에 두 OPP(P_hi, P_lo)와 thr, `hyst`를 넣어 방향 전환이 1번 이하가 되는 최소 hyst를 0.25 K 격자로 스캔하고 해석식과 비교하라.

<details>
<summary>답 보기</summary>

rel이 낮은 OPP의 정상 온도 Tss_lo보다 낮으면(온도가 그 아래로 안 내려가므로) up이 일어나지 않는다. `hyst_min = thr − Tss_lo`. Ta=45, R=2.5, P_lo=15 W: Tss_lo = 82.5 ℃, 해석값 **7.50 K**, 스캔 7.50 K. 1 K면 3000 s 동안 190번 방향 전환. 이 식은 정상 온도를 쓰므로 냉각 변화·부하 변화에 맞춰 **가장 차가운/약한 부하 조건**으로 계산한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice; echo exit=$?
```

실제 출력 (일부):

```text
PASS  P1 same 5 C offset: delay 16.4 s near 90 C vs 62.4 s near 115 C
PASS  P2 critical R: fixed-point scan 2.8267, tangent condition 2.8267 K/W (tol 0.01)
PASS  P2 nominal R=2.5 settles at 139.9 C
PASS  P3 self-heat 5.0 K biases offset by +5.060 K, gain unaffected
PASS  P4 value-stuck sensor flagged at sample 305 (stuck from 200, needs |a-b|>3 C first)
PASS  P4 LIMIT: common-mode -10 C offset on both sensors is not detected
PASS  P5 min hysteresis: scan 7.50 K, analytic thr-Tss(L_low)=7.50 K (tol 0.5 = scan step x2)
21/21 passed
exit=0
```

(ASan/UBSan 빌드도 `21/21 passed`.)

## 10. 핵심 정리

- 열 throttling = 폐루프. 플랜트(RC) + 센서(오프셋·lag·stuck) + governor + DVFS. 각 고리를 따로 주입·측정한다.
- Forward Euler 안정: Gershgorin 충분조건 `dt < 1/max(ΣG/C)` (0.1 s), 정확한 한계 `2/λmax` (0.187 s). 열류는 **옛 온도로 먼저** 계산, 에너지 잔차로 검증.
- checker 두 층: **정책**(read 기준, TRIP/HYST/STEP/VF/EMG/SD) + **안전**(reference 기준, TJ_EXCEEDED, STALE). 정책만 있으면 센서 오프셋이 안 보인다.
- 오프셋은 trip 온도를 1:1로 옮기지만 지연은 비선형 — 점근선 근처에서 `t = −τ ln(...)` 로 폭증, 초과하면 ∞.
- hysteresis 하한은 `thr − Tss(내린 OPP)`. 올릴 땐 dwell + 전압 먼저(V↑ → f↑), 내릴 땐 f↓ → V↓.
- SEQ 감시는 정지만 잡고 **값 고착은 못 잡는다** → 교차 센서·물리 일관성, 공통모드는 외부 기준으로.
- guard band = 측정해서 구한다(시나리오별 bisect). 오프셋 + overshoot 을 단순 합산하면 틀린다. thr 1 ℃ ≈ 지속 성능 약 1%.
- 판정은 반응의 옳음: 고장을 제대로 처리하면 PASS (S6). 도달 안 한 경로는 0건이어도 증명이 아니다 (BUG_NO_SD).

## 참고자료

- [Linux `Documentation/devicetree/bindings/thermal/thermal-zones.yaml` — trip type (active/passive/hot/critical), hysteresis, polling-delay](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/thermal/thermal-zones.yaml)
- [Linux `drivers/thermal/gov_step_wise.c`, `gov_power_allocator.c`](https://github.com/torvalds/linux/tree/master/drivers/thermal) — 스텝 governor와 IPA, sysfs `/sys/class/thermal/thermal_zone*/temp` (millidegree C)
- JEDEC JESD51 시리즈 — 패키지 열저항(ΘJA, ΘJC, ΨJT) 측정 방법, 과도 열 측정 (세부 번호는 원문 확인)
- Intel Datasheet Vol. 1 thermal 장 — PROCHOT#, THERMTRIP# 동작 설명 (독립 하드웨어 경로의 예)
- Foster/Cauer 열 네트워크: 패키지 열 모델링 참고서 전반 (SIM-M08 예정)
- [Gershgorin circle theorem](https://en.wikipedia.org/wiki/Gershgorin_circle_theorem) — 시간 스텝 안정 조건의 근거
- 관련 레슨: SIV-B02(mock MMIO), SIV-B09(timeout), SIV-B14(리포트), SIV-M01(ATE/SLT 역할), SIV-M07(shmoo/guard band), SIV-M08(챔버 soak·스케줄), SIV-M13(코버리지), SIV-A03(전력 특성화·identifiability), SIM-M08(열 RC 전체 — 미작성)
