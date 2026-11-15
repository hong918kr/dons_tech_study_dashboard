---
id: ARCH-A08
time: 110분
updated: 2026-11-11
---
# ARCH-A08 전력 모델: DVFS와 Race-to-Idle 에너지 계산

## 학습 목표

- `P = α·C·V²·f + P_leak(V,T) + P_static` 과 OPP(Operating Performance Point) 표로, 같은 작업을 마감 D 안에 끝내는 계획들의 **에너지(J)** 를 손으로도, 코드로도 계산한다. 비교 창은 항상 `[0, D]` 전체다.
- idle(클럭 게이팅: 누설은 계속)과 sleep(파워 게이팅: `P_sleep` + 진입/복귀 에너지·시간)을 구분하고, **sleep 손익분기 시간**(Linux DT `min-residency-us` 와 같은 개념)을 유도한다.
- race-to-idle / race-to-sleep / pace / 최적 OPP 를 비교하고, **critical speed**(사이클당 에너지가 최소인 주파수)가 누설과 정적 전력 때문에만 생긴다는 것을 숫자로 보인다.
- 누설 배율·온도·플랫폼 전력·슬립 전력·전환 에너지·메모리 바운드 비율을 쓸어 **승자가 바뀌는 지점(crossover)** 을 찾고, "race-to-idle 이 항상 이기나?" 에 조건을 붙여 답한다.
- 듀티 사이클 센서 노드의 배터리 수명을 일 단위로 추정하고(`mAh × V × 3.6 = J`), 어떤 항이 지배하는지 민감도 표로 말한다.

## 1. 문제

"race-to-idle 이 항상 이기나요?" 는 모바일 SoC·AI 가속기·저전력 MCU 면접에서 단골로 나오고, 답이 "예/아니오" 가 아니라 **"무엇이 켜져 있느냐에 달렸다"** 이기 때문에 숫자로 말할 수 있어야 한다.

DVFS(Dynamic Voltage and Frequency Scaling)는 전압과 주파수를 같이 내려 전력을 줄인다. 동적 에너지는 V² 에 비례하므로 낮은 OPP 가 유리해 보인다. 그런데 느리게 돌면 **켜져 있는 시간**이 늘고, 그 시간 내내 누설과 정적 전력이 샌다. 반대로 빨리 끝내고(race) 깊이 자면(sleep) 그 몫을 아낀다. 이 레슨은 그 줄다리기를 계산기로 만든다. 수식의 모든 숫자는 **대표값/가정**이고 특정 칩 데이터시트 값이 아니다. 실리콘에서 이 파라미터를 측정으로 **맞추는(fit)** 일은 SIV-A03 이 하고, 여기서는 맞춰진 모델을 **쓴다**.

| 기호 | 의미 | 기준값 (가정) | 단위 |
|---|---|---|---|
| α, C | 활동도, 스위칭 커패시턴스 | 0.25, 1.0 nF (αC = 0.25 nF) | –, F |
| `P_leak` | `V·I0·scale·e^(k_v(V−V0))·e^((T−T0)/T_s)` | I0 = 50 mA @ V0 0.8 V, T0 25 °C, k_v 3 /V, T_s 25 °C | W |
| `P_static` | 켜져 있으면 V 와 무관하게 드는 몫 (PLL, 레귤레이터, always-on, 플랫폼) | 20 mW | W |
| idle | 클럭 게이팅. `P_leak(V_idle) + P_static` | V_idle = 실행 V (옵션: OPP0 V) | W |
| sleep | 파워 게이팅. `P_sleep`, 진입+복귀 `e_trans`, `t_trans` | 2 mW, 0.5 mJ, 2 ms | W, J, s |
| OPP 표 | (300 MHz, 0.60 V) (600, 0.70) (1000, 0.80) (1400, 0.90) (1800, 1.00) | f 순증가, V 비감소 | Hz, V |
| 작업 | N = 10⁹ cycle, 마감 D = 4 s, 메모리 스톨 비율 m (f_ref = 1.8 GHz 기준) | m = 0 | cycle, s |

아래 그림은 Part B 기준 모델의 실제 계산값이다. **면적 = 에너지**. 높은 OPP 는 키가 크지만 짧고, 낮은 OPP 는 낮지만 길다. 승부는 휴식 구간에서 갈린다.

```svg
<svg viewBox="0 0 760 335" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="8" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 작업 (10⁹ cycle, D = 4 s), 전력-시간 그래프. 높이 100 px/W, 폭 150 px/s</text>
  <g fill="currentColor">
    <text x="8" y="64" font-weight="bold">race-to-idle</text><text x="8" y="80">OPP4 + idle 0.694 J</text>
    <text x="8" y="134" font-weight="bold">race-to-sleep</text><text x="8" y="150">OPP4 + sleep 0.319 J</text>
    <text x="8" y="204" font-weight="bold">pace</text><text x="8" y="220">OPP0 + sleep 0.213 J</text>
    <text x="8" y="274" font-weight="bold">optimal</text><text x="8" y="290">OPP1 + sleep 0.204 J</text>
  </g>
  <g stroke="currentColor">
    <rect x="150" y="34" width="83" height="56" class="f-acc"/>
    <rect x="233" y="79" width="517" height="11" class="f-warn"/>
    <rect x="150" y="104" width="83" height="56" class="f-acc"/>
    <rect x="233" y="158" width="517" height="2" class="f-ok"/>
    <rect x="150" y="224" width="500" height="6" class="f-acc"/>
    <rect x="650" y="228" width="100" height="2" class="f-ok"/>
    <rect x="150" y="288" width="250" height="12" class="f-acc"/>
    <rect x="400" y="298" width="350" height="2" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="156" y="52">0.561 W</text><text x="156" y="68">0.556 s</text>
    <text x="300" y="74">idle 0.111 W × 3.44 s = 0.383 J (누설 @1.0 V + P_static)</text>
    <text x="156" y="122">0.561 W</text>
    <text x="300" y="150">sleep 2 mW × 3.44 s + 0.5 mJ = 0.0074 J</text>
    <text x="300" y="218">0.0635 W × 3.33 s = 0.212 J</text>
    <text x="600" y="246">sleep 0.67 s</text>
    <text x="300" y="283">0.119 W × 1.67 s = 0.199 J</text>
  </g>
  <g stroke="currentColor"><path d="M150 312 L750 312"/>
    <path d="M150 308 L150 316"/><path d="M300 308 L300 316"/><path d="M450 308 L450 316"/><path d="M600 308 L600 316"/><path d="M750 308 L750 316"/></g>
  <g fill="currentColor"><text x="146" y="328">0</text><text x="292" y="328">1 s</text><text x="442" y="328">2 s</text><text x="592" y="328">3 s</text><text x="726" y="328">4 s</text></g>
</svg>
```

### Part A — 계산기

- `p_dyn(m, V, f)`, `p_leak(m, V)` (온도·누설 배율 포함), `opp_table_ok(opps, n)` (f 순증가 · V 비감소 · 유한 양수), `run_time(job, f)` (계산 몫만 1/f 로 준다).
- `sleep_breakeven(m, P_idle)`: 휴식이 이 시간보다 길어야 sleep 이 idle 보다 싸다. `t_trans` 보다 짧을 수 없고, `P_idle ≤ P_sleep` 이면 ∞.
- `eval_opp(...)`: OPP 하나로 실행 + 휴식(idle 만 / 싼 쪽) 의 `t_run, E_run, slack, rest, E_rest, E_total, feasible`.
- 검사: 손으로 계산한 값(유효숫자 10 자리)과 상대오차 1e-9 이내. 손익분기 ±1 % 에서 sleep/idle 이 뒤집힘, `slack < t_trans` 면 sleep 불가, 마감 초과는 infeasible, slack 0 이면 휴식 없음.

### Part B — 언제 무엇이 이기나

- `plan_strategy`: race-to-idle(최고 OPP + idle), race-to-sleep(최고 OPP + 싼 휴식), pace(마감을 지키는 최저 OPP), optimal(가능한 OPP 중 최소 에너지).
- `e_per_cycle`, `critical_opp`: 누설·정적 0 이면 critical = 최저 OPP, 정적만 있어도 내부 최소가 생기고, 85 °C 면 위로 올라간다. 누설이 V 에 지수적으로 줄지 않으면(`k_v = 0`) 최고 OPP 가 최적 = race 가 이긴다.
- 스윕 + crossover(이분법): 누설 배율, Tj, P_static, P_sleep, e_trans, 메모리 바운드 비율 m. 완전 메모리 바운드(m = 1)면 누설 배율과 무관하게 최저 OPP. crossover 양쪽에서 승자가 실제로 바뀌는지도 CHECK.

### Part C — 듀티 사이클 센서 노드

- 10 s 마다 깨어나 MCU 200 k cycle @ 64 MHz, 센서 측정, 무선 TX + RX(ACK 창), 나머지는 슬립. 부하 1.8 V, 레귤레이터 효율(활성 85 % / 경부하 70 %) + Iq, CR2032 급 220 mAh @ 3.0 V, 사용 가능 80 %, 자가방전 1 %/년. 모두 **대표값**.
- `batt_energy_j`, `node_split`(항목별 배터리 쪽 평균 µW), `node_life_days` → 일. 민감도 표(각 항을 절반으로 줄이면 수명이 몇 % 느나), 주기 스윕(0.1 s … 1 h)에서 지배 항이 바뀌는 지점.

## 2. Problem (English)

> **Energy calculator for DVFS vs race-to-idle.** A core has an OPP table `{(f_i, V_i)}` (f strictly increasing, V non-decreasing). Power while running at `(V, f)` is `P = α·C·V²·f + P_leak(V, T) + P_static`, with `P_leak = V·I0·s·e^(k_v(V−V0))·e^((T−T0)/T_s)`. While clock-gated (idle) the core draws `P_leak(V_idle) + P_static`; while power-gated (sleep) it draws `P_sleep`, and entering+leaving sleep costs `e_trans` joules and `t_trans` seconds.
>
> A job of `N` cycles must finish within deadline `D`. A fraction `m` of its runtime at `f_ref` is memory stall that does not shrink with frequency: `t_run(f) = N(1−m)/f + N·m/f_ref`; during stalls the activity factor is scaled by `alpha_stall`. Energy is counted over the whole window `[0, D]`.
>
> A. Implement `p_dyn`, `p_leak`, `opp_table_ok`, `run_time`, `sleep_breakeven(P_idle)` and `eval_opp(i, policy)` (policy = idle only, or the cheaper of idle/sleep when the slack allows it). Example: at 1.8 GHz / 1.0 V with the default parameters, `E_run = 0.31173 J`, idle for the remaining 3.444 s costs 0.38270 J, sleeping costs 0.00738 J.
>
> B. Implement the strategies race-to-idle, race-to-sleep, pace (lowest feasible OPP) and optimal (min energy over feasible OPPs), plus `e_per_cycle(V, f)` and the critical OPP. Show that without leakage and static power the lowest OPP is always optimal; sweep leakage, temperature, static power, sleep power, transition energy and memory-boundedness and report the crossover points.
>
> C. Estimate the battery life (days) of a duty-cycled sensor node: wake period, MCU active cycles at a µA/MHz current model, sensor and radio TX/RX energy, sleep current, regulator efficiency (active / light-load) and quiescent current, battery capacity (mAh at V), derating and self-discharge. Produce a sensitivity table.
>
> Follow-ups: Does race-to-idle always win? Why does a critical speed exist? What happens on a memory-bound workload? How would a governor decide whether to enter a deep idle state? Which term dominates a coin-cell node's lifetime, and how does that change with the wake period?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st             # macOS 는 -lm 불필요 (Linux 는 끝에 -lm)
```

TODO 는 12 개다: Part A 6 개(`p_dyn`, `p_leak`, `opp_table_ok`, `run_time`, `sleep_breakeven`, `eval_opp`), Part B 3 개(`plan_strategy`, `e_per_cycle`, `critical_opp`), Part C 3 개(`batt_energy_j`, `node_split`, `node_life_days`). crossover 이분법, 표 출력, 민감도 계산은 하네스가 준다. 처음 실행하면 (실제 출력 일부, exit 1):

```text
== Part A: 계산기 (대표값 모델, Tj 25 °C) ==
FAIL starter.c:307  near(p_dyn(&m, 1.00, 1800e6), 0.45, 1e-12)
FAIL starter.c:308  near(p_dyn(&m, 0.60, 300e6), 0.027, 1e-12)
FAIL starter.c:309  near(p_dyn(&m, 1.20, 1000e6) / p_dyn(&m, 0.60, 1000e6), 4.0, 1e-12)
...
FAIL starter.c:545  dom_fast == T_MCU && dom_slow == T_SLEEP
FAIL starter.c:550  near(node_life_days(&inf), cap, 1e-6)
   주기 → ∞ 상한: 0.0 일 (0.00 년) — 슬립·Iq·자가방전만으로 정해진다
6/59 passed
```

통과하는 6 개는 "기본값이 우연히 맞는" 부정 검사들이다(예: 스텁 `opp_table_ok` 가 늘 false 라 "잘못된 표를 거부" 가 통과). 정답을 다 채우면 59/59.

## 4. 힌트

<details><summary>힌트 1 — 동적 에너지는 f 와 무관하다</summary>

`E_dyn = P_dyn · t = αCV²f · N/f = αCV²N`. 주파수는 지워지고 **V² 만** 남는다. 그러니 "주파수를 낮추면 동적 에너지가 준다" 는 틀린 말이고, 줄이는 것은 **전압**이다. f 는 오직 "켜져 있는 시간" 을 통해 누설·정적 몫에 영향을 준다.
</details>

<details><summary>힌트 2 — 손익분기는 직선 두 개의 교점</summary>

휴식 시간 s 에 대해 idle 은 `P_idle · s` (원점을 지나는 직선), sleep 은 `e_trans + P_sleep · (s − t_trans)` (절편 있는 완만한 직선). 교점 `s* = (e_trans − P_sleep·t_trans) / (P_idle − P_sleep)`. 그리고 sleep 은 `s ≥ t_trans` 일 때만 물리적으로 가능하니 `max(s*, t_trans)`. `P_idle ≤ P_sleep` 이면 교점이 없다(∞).
</details>

<details><summary>힌트 3 — eval_opp 의 세 구간</summary>

계산 시간 `t_c = N(1−m)/f` 에는 `P_dyn`, 스톨 시간 `t_m = N·m/f_ref` 에는 `alpha_stall·P_dyn`, 둘 다에 `P_leak(V) + P_static`. slack = `D − t_run` (음수면 0 + infeasible). 휴식은 idle 을 기본으로 계산해 두고, 정책이 허락하고 `slack ≥ t_trans` 이고 sleep 이 **엄격히** 싸면 바꾼다. idle 전압은 `idle_drops_v` 에 따라 실행 V 또는 OPP0 V.
</details>

<details><summary>힌트 4 — optimal 은 모든 feasible OPP 를 다 본다</summary>

pace 는 "마감을 지키는 가장 낮은 OPP" 라는 **규칙**이고 optimal 은 **최소값**이다. 둘이 같다고 가정하지 마라. 기준 모델에서 pace 는 300 MHz, optimal 은 600 MHz 다 — 300 MHz 는 너무 오래 켜져 있어서 누설·정적 몫이 동적 절약을 넘는다.
</details>

<details><summary>힌트 5 — 배터리는 에너지로, 부하는 부하 전압으로</summary>

`mAh` 는 전하량이다. 에너지로 바꾸려면 전압을 곱한다: `mAh × V × 3.6 = J` (1 mAh = 3.6 C). 부하 전류(1.8 V 쪽)를 배터리 mAh 로 그대로 나누면 레귤레이터 변환을 무시하는 것이다. 이 레슨은 모든 항을 **배터리 쪽 평균 전력(W)** 으로 바꿔 더한다: 부하 에너지 ÷ 효율 ÷ 주기.
</details>

## 5. 풀이

### Part A — 손으로 한 번, 코드로 한 번

**OPP4 (1.8 GHz, 1.0 V), m = 0, D = 4 s:**

| 단계 | 식 | 값 |
|---|---|---|
| P_dyn | 0.25 × 1e-9 × 1.0² × 1.8e9 | 0.45000 W |
| P_leak | 1.0 × 0.05 × e^(3×0.2) × e^0 = 0.05 × 1.82212 | 0.091106 W |
| P_on | 0.45 + 0.091106 + 0.02 | 0.561106 W |
| t_run | 1e9 / 1.8e9 | 0.55556 s |
| E_run | αCV²N = 0.25 J  +  (0.091106 + 0.02) × 0.55556 = 0.061725 J | **0.311726 J** |
| slack | 4 − 0.55556 | 3.44444 s |
| idle | (0.091106 + 0.02) × 3.44444 | 0.382698 J → 합 **0.694424 J** |
| sleep | 0.5 mJ + 2 mW × (3.44444 − 0.002) | 0.007385 J → 합 **0.319110 J** |
| 손익분기 | (0.5e-3 − 2e-3 × 2e-3) / (0.111106 − 0.002) | **4.546 ms** (> t_trans 2 ms) |

**OPP0 (300 MHz, 0.6 V):** P_dyn = 0.25e-9 × 0.36 × 3e8 = 0.027 W, P_leak = 0.6 × 0.05 × e^(−0.6) = 0.016464 W, t_run = 3.3333 s, E_run = 0.25e-9 × 0.36 × 1e9 (= 0.09 J) + 0.036464 × 3.3333 (= 0.12155 J) = **0.21155 J**. slack 0.6667 s → sleep 0.00183 J → 합 **0.21338 J**.

전체 표 (solution 의 실제 출력):

```text
   OPP   f[MHz]  V     P_dyn[W]  P_leak[W] P_on[W]   t_run[s]  E_run[J]  E/cyc[nJ] slack[s] rest  E_rest[J] E_total[J]
   0      300   0.60  0.02700   0.01646   0.06346   3.3333    0.21155   0.2115    0.6667   sleep 0.00183   0.21338
   1      600   0.70  0.07350   0.02593   0.11943   1.6667    0.19905   0.1990    2.3333   sleep 0.00516   0.20421
   2     1000   0.80  0.16000   0.04000   0.22000   1.0000    0.22000   0.2200    3.0000   sleep 0.00650   0.22650
   3     1400   0.90  0.28350   0.06074   0.36424   0.7143    0.26017   0.2602    3.2857   sleep 0.00707   0.26724
   4     1800   1.00  0.45000   0.09111   0.56111   0.5556    0.31173   0.3117    3.4444   sleep 0.00738   0.31911
   idle 전력 @1.00 V = 0.11111 W, sleep 손익분기 = 4.546 ms (t_trans 2.0 ms)
```

읽는 법:
- **전력 순서와 에너지 순서는 다르다.** P_on 은 OPP0 이 OPP4 의 1/9 이지만 에너지는 2/3 수준이다. 시간이 6 배라서.
- **E_run 의 동적 몫(αCV²N)은 0.09 → 0.25 J 로 V² 를 따라가고**, 누설+정적 몫(×시간)은 0.122 → 0.062 J 로 반대로 간다. 두 곡선의 합이 600 MHz 에서 최소 = critical OPP.
- **race-to-idle 의 진짜 손해는 실행이 아니라 휴식에서** 난다. 0.383 J 짜리 idle 은 실행 에너지(0.312 J)보다 크다. 같은 race 라도 sleep 으로 쉬면 0.319 J.

**sleep 손익분기의 의미.** 4.546 ms 보다 짧게 쉴 거면 power-gate 하지 말고 클럭만 꺼라. 하네스는 `slack = 1.01 × t_be` 에서 sleep, `0.99 × t_be` 에서 idle 이 나오는 것을 CHECK 한다. 실제 시스템에서 이 숫자는 Linux devicetree 의 `arm,idle-state` 노드에 `entry-latency-us` / `exit-latency-us` / `min-residency-us` 로 들어가고, cpuidle governor(menu, teo 등)가 "다음 깨어날 때까지 예상 시간" 과 비교해 상태를 고른다. 예측이 틀리면(깊이 잤는데 곧 인터럽트가 오면) 이 레슨의 손익분기 아래쪽 손해를 그대로 낸다.

**idle 중 전압을 내리면?** `idle_drops_v = true` 로 OPP0 전압에서 쉬면 race-to-idle 의 휴식 에너지가 0.383 J → (0.01646 + 0.02) × 3.444 = 0.126 J 로 준다(CHECK). 다만 전압 전환 자체의 시간·에너지(레귤레이터 slew, PLL 재잠금)는 이 모델에 없다 — 면접 Q7.

### Part B — 언제 무엇이 이기나

**기준 모델 (실제 출력):**

```text
  race-to-idle    OPP4 1800 MHz 1.00 V  t_run=0.5556 s  E_run=0.31173 J  slack=3.4444 s idle  E_rest=0.38270 J  E=0.69442 J
  race-to-sleep   OPP4 1800 MHz 1.00 V  t_run=0.5556 s  E_run=0.31173 J  slack=3.4444 s sleep E_rest=0.00738 J  E=0.31911 J
  pace            OPP0  300 MHz 0.60 V  t_run=3.3333 s  E_run=0.21155 J  slack=0.6667 s sleep E_rest=0.00183 J  E=0.21338 J
  optimal         OPP1  600 MHz 0.70 V  t_run=1.6667 s  E_run=0.19905 J  slack=2.3333 s sleep E_rest=0.00516 J  E=0.20421 J
```

**critical speed 는 왜 생기나.** 계산만 하는 작업(m = 0)의 사이클당 에너지:

```text
E/cycle(f) = α·C·V(f)²  +  (P_leak(V(f)) + P_static) / f
             └ V 와 함께 증가 ┘   └ 1/f 로 감소 (V 가 같이 내려가며 누설도 감소) ┘
```

첫 항만 있으면 f 를 내릴수록 좋다(단조). 두 번째 항이 있어야 내부 최소가 생긴다. 실제 출력:

```text
  E/cycle [nJ]         300     600    1000    1400    1800 MHz   critical
  leak=0 static=0   0.0900  0.1225  0.1600  0.2025  0.2500   OPP0  300 MHz
  static only       0.1567  0.1558  0.1800  0.2168  0.2611   OPP1  600 MHz
  leak only         0.1449  0.1657  0.2000  0.2459  0.3006   OPP0  300 MHz
  base 25C          0.2115  0.1990  0.2200  0.2602  0.3117   OPP1  600 MHz
  base 85C          0.7616  0.6322  0.6209  0.6951  0.8190   OPP2 1000 MHz
  k_v=0 leak x16    1.7567  1.0892  0.8200  0.7311  0.7056   OPP4 1800 MHz
```

- **누설·정적 0** → 최저 OPP 가 critical, optimal = pace (하네스가 "낮은 OPP 일수록 E_total ≤" 단조성을 CHECK).
- **25 °C 누설만** 으로는 부족하다(OPP0 그대로). 이 모델의 누설은 `V·e^(3V)` 로 전압과 함께 가파르게 줄어서, 낮은 OPP 에서 시간이 늘어도 누설 **전력**이 그만큼 떨어진다. **V 와 무관한 정적 전력(20 mW)** 이 들어가야 600 MHz 에 최소가 생긴다.
- **85 °C** (누설 e^2.4 ≈ 11 배) → critical 이 1000 MHz 로 올라간다. 뜨거울수록 빨리 끝내는 쪽으로.
- **`k_v = 0` (누설이 V 에 비례만) + 16 배** → 낮은 전압이 누설을 별로 못 줄이니 최고 OPP 가 critical 이자 optimal. **race-to-idle 이 이기는 조건의 본질 = "켜져 있는 동안 드는 몫이 전압을 낮춰도 안 줄어든다".**

OPP 사이 전압을 선형 보간(가정)해 10 MHz 간격으로 훑은 연속 곡선 (`demo_energy_curve.c` 실제 출력):

```text
curve             f_crit[MHz]  V_crit  E/cycle_min[nJ]  E/cycle@1800[nJ]  E/cycle@300[nJ]
leak=0 static=0        300     0.600   0.0900           0.2500            0.0900
base 25C               480     0.660   0.1957           0.3117            0.2115
base 85C               820     0.755   0.6103           0.8190            0.7616
base 105C              870     0.768   1.1518           1.5028            1.5030
```

```svg
<svg viewBox="0 0 760 350" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">E/cycle(f) — demo_energy_curve.c 실제 값 (OPP 사이 V 선형 보간), ● = 연속 critical frequency</text>
  <g stroke="currentColor">
    <path d="M60 300 L700 300"/><path d="M60 300 L60 30"/>
    <path d="M60 300 L60 305"/><path d="M188 300 L188 305"/><path d="M359 300 L359 305"/><path d="M529 300 L529 305"/><path d="M700 300 L700 305"/>
    <path d="M55 210 L60 210"/><path d="M55 120 L60 120"/><path d="M55 30 L60 30"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 4" opacity="0.4"><path d="M60 210 L700 210"/><path d="M60 120 L700 120"/></g>
  <g fill="currentColor">
    <text x="50" y="320">300</text><text x="178" y="320">600</text><text x="345" y="320">1000</text><text x="515" y="320">1400</text><text x="680" y="320">1800 MHz</text>
    <text x="22" y="304">0</text><text x="16" y="214">0.3</text><text x="16" y="124">0.6</text><text x="16" y="34">0.9</text>
    <text x="10" y="342">nJ/cycle. 300 MHz 쪽은 "오래 켜져 있는 값", 1800 MHz 쪽은 "V² 값" 이 지배</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" points="60,273 81,271 103,270 124,268 145,267 167,265 188,263 209,262 231,261 252,259 273,258 295,256 316,255 337,253 359,252 380,250 401,249 423,247 444,246 465,244 487,243 508,241 529,239 551,238 572,236 593,234 615,232 636,231 657,229 679,227 700,225"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="60,237 81,239 103,241 124,241 145,241 167,241 188,240 209,240 231,240 252,239 273,238 295,237 316,236 337,235 359,234 380,233 401,231 423,230 444,228 465,227 487,225 508,224 529,222 551,220 572,218 593,217 615,215 636,213 657,211 679,209 700,206"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="60,72 81,86 103,96 124,103 145,107 167,109 188,110 209,113 231,115 252,116 273,117 295,117 316,116 337,115 359,114 380,112 401,110 423,107 444,105 465,102 487,99 508,95 529,91 551,88 572,83 593,79 615,75 636,70 657,65 679,60 700,54"/>
  <circle cx="60" cy="273" r="5" class="f-ok" stroke="currentColor"/>
  <circle cx="137" cy="241" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="282" cy="117" r="5" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="470" y="262">누설·정적 0 (점선): 최소 = 최저 OPP</text>
    <text x="470" y="196">기준 25 °C: 최소 480 MHz, 0.196 nJ</text>
    <text x="300" y="148">기준 85 °C: 최소 820 MHz, 0.610 nJ</text>
  </g>
</svg>
```

곡선이 바닥에서 **평평하다**는 것도 읽어야 한다. 25 °C 곡선은 400–650 MHz 에서 최소값(0.1957 nJ)의 2 % 안쪽이다(같은 보간으로 50 MHz 간격 별도 계산). 그러니 "정확한 critical frequency" 보다 "그 아래로는 내리지 마라" 가 실무 규칙이다. 300 MHz 는 critical 아래라 최소보다 8 % 비싸고, 1800 MHz 는 59 % 비싸다.

**누설 배율 스윕 (기준 모델, 실제 출력):**

```text
   scale  leak%@1.8GHz  race-idle  race-sleep   pace     optimal(OPP)  승자(race-sleep vs pace)
     0.0     0.0%       0.3300     0.2685     0.1585     0.1585(0)   pace
     1.0    16.2%       0.6944     0.3191     0.2134     0.2042(1)   pace
     4.0    43.7%       1.7877     0.4710     0.3780     0.3339(1)   pace
     8.0    60.8%       3.2454     0.6734     0.5975     0.5065(2)   pace
    16.0    75.6%       6.1608     1.0783     1.0366     0.8265(2)   pace
    32.0    86.1%      11.9916     1.8882     1.9147     1.4665(2)   race
    64.0    92.5%      23.6531     3.5078     3.6709     2.7465(2)   race
```

(0.5, 2 배 줄은 생략, 전체는 `./so`.) `leak%` = 1.8 GHz 에서 총전력 중 누설 비율. 누설이 86 % 가 돼야 race-to-sleep 이 pace 를 이긴다. 그때도 **optimal 은 1000 MHz** 다. 지수 누설 모델에서는 "최고 OPP 로 race" 가 최적이 되지 않는다. 대신 "최저 OPP 로 pace" 가 점점 나빠진다.

**crossover 요약 (race-to-sleep == pace, 이분법, 실제 출력):**

```text
   누설 배율 (기준)                 : 25.781 ×
   Tj (기준)                        : 106.24 °C
   P_static (기준)                  : 58.06 mW   (optimal 이 최고 OPP 가 되는 P_static: 346.8 mW)
   P_sleep (P_static 100 mW)        : 43.94 mW
   e_trans (P_static 100 mW, N=1e7, D=40 ms) : 1.932 mJ
   mem_frac (P_static 100 mW)       : 0.5099
```

| 스윕 | 기준점 | crossover | 해석 |
|---|---|---|---|
| 누설 배율 | 기준 | 25.8 × | FF 코너·고온 칩이 아니면 pace 가 race 보다 낫다 |
| Tj | 기준 | 106.2 °C | = 25 + 25·ln(25.8) — 온도는 누설 배율의 로그 (CHECK) |
| P_static | 기준 | 58 mW | **플랫폼 전력이 크면 race**. 347 mW 를 넘으면 optimal 자체가 최고 OPP |
| P_sleep | P_static 100 mW | 43.9 mW | sleep 이 진짜로 깊어야 race-to-sleep 이 의미 있다 |
| e_trans | P_static 100 mW, 10⁷ cycle / 40 ms 주기 작업 | 1.93 mJ | 짧은 주기 작업은 진입·복귀 비용에 민감 |
| mem_frac m | P_static 100 mW | 0.51 | 메모리 바운드가 절반을 넘으면 race 의 이득(시간 단축)이 사라진다 |

**P_static 100 mW (플랫폼 전력 큼) 에서의 전략 표와 m 스윕 (실제 출력):**

```text
  race-to-idle    OPP4 1800 MHz 1.00 V  t_run=0.5556 s  E_run=0.35617 J  slack=3.4444 s idle  E_rest=0.65825 J  E=1.01442 J
  race-to-sleep   OPP4 1800 MHz 1.00 V  t_run=0.5556 s  E_run=0.35617 J  slack=3.4444 s sleep E_rest=0.00738 J  E=0.36355 J
  pace            OPP0  300 MHz 0.60 V  t_run=3.3333 s  E_run=0.47821 J  slack=0.6667 s sleep E_rest=0.00183 J  E=0.48004 J
  optimal         OPP2 1000 MHz 0.80 V  t_run=1.0000 s  E_run=0.30000 J  slack=3.0000 s sleep E_rest=0.00650 J  E=0.30650 J
   m     race-sleep  pace(OPP)     optimal(OPP)
   0.00    0.3636      0.4800(0)     0.3065(2)
   0.25    0.3198      0.3792(0)     0.2578(2)
   0.50    0.2761      0.2783(0)     0.2092(2)
   0.75    0.2323      0.1775(0)     0.1516(1)
   1.00    0.1886      0.0766(0)     0.0766(0)
```

- 이 조건에서는 race-to-sleep 이 pace 를 이긴다. 그래도 **race-to-idle 은 1.01 J 로 최악**이다. "race-to-idle" 이라는 말은 사실 **"race-to-deep-sleep"** 일 때만 성립한다.
- m 이 커질수록 optimal 이 내려간다. m = 1 이면 실행 시간이 f 와 무관하니 높은 f 는 순수 낭비다(스톨 중에도 클럭은 돌고 `alpha_stall` 만큼 스위칭한다). 하네스는 누설 배율 0.1–1000 전부에서 m = 1 → OPP0 을 CHECK 한다. 실제 시스템에서 메모리 바운드 판정은 PMU 의 스톨 사이클 카운터로 하고(이벤트 이름은 코어마다 다르다), 메모리 버스 자체의 DVFS 는 Linux devfreq 의 영역이다.

**모델이 빠뜨린 것** (면접에서 먼저 말하면 좋다): DVFS 전환 시간·에너지(전압 slew, PLL 재잠금), 온도-누설 되먹임(연습 3, 제어는 SIV-A04), V-f 관계의 칩별 편차(binning·AVS), 여러 코어·클러스터 공유 레일(한 레일의 V 는 가장 빠른 코어가 정한다), 캐시 상태 손실(sleep 후 캐시 재워밍 비용은 `e_trans` 에 뭉뚱그렸다), DRAM·디스플레이 같은 플랫폼 전력의 상태 의존성.

### Part C — 듀티 사이클 센서 노드

**손계산 (기준 노드, 주기 10 s):**

| 항목 | 부하 에너지 / 회 | 배터리 쪽 평균 | 계산 |
|---|---|---|---|
| MCU | 1.8 V × (50 µA/MHz × 64 MHz + 0.1 mA = 3.3 mA) × 3.125 ms = 18.56 µJ | 2.184 µW | ÷ 0.85 ÷ 10 s |
| 센서 | 1.8 × 1.0 mA × 10 ms = 18.0 µJ | 2.118 µW | |
| TX | 1.8 × 6 mA × 1.5 ms = 16.2 µJ | 1.906 µW | |
| RX (ACK 창) | 1.8 × 5 mA × 1 ms = 9.0 µJ | 1.059 µW | |
| 슬립 | 1.8 V × 2 µA × (10 − 0.0156)/10 | 5.135 µW | ÷ 0.70 (경부하 효율) |
| 레귤레이터 Iq | 0.5 µA × 3.0 V | 1.500 µW | 배터리에 바로 |
| 자가방전 | 1 % × 2376 J / 1 년 | 0.753 µW | 단순화: 선형 |
| 합 | | **14.654 µW** | |

배터리: 220 mAh × 3.0 V × 3.6 = **2376 J**, 사용 가능 80 % = 1900.8 J → 1900.8 / 14.654e-6 / 86400 = **1501.3 일 (4.11 년)**. 단위 검산 하나 더: 슬립 10 µA 만, 효율 1, 손실 0 이면 220 mAh / 10 µA = 22 000 h = 916.67 일 (CHECK).

**민감도 표 (실제 출력):**

```text
   항목              배터리쪽 평균[µW]  비중    절반이면 수명[일]  Δ
   MCU active            2.184         14.9%      1622.1         +8.1%
   sensor                2.118         14.5%      1618.2         +7.8%
   radio TX              1.906         13.0%      1605.7         +7.0%
   radio RX              1.059          7.2%      1557.5         +3.7%
   sleep                 5.135         35.0%      1820.1        +21.2%
   reg Iq                1.500         10.2%      1582.2         +5.4%
   self-discharge        0.753          5.1%      1540.9         +2.6%
   합계                 14.654        → 수명 1501.3 일 (4.11 년), 사용 가능 에너지 1900.8 J
```

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">기준 노드 (주기 10 s) 배터리 쪽 평균 전력 — 실제 출력, 75 px/µW</text>
  <g fill="currentColor">
    <text x="10" y="44">MCU active</text><text x="10" y="70">sensor</text><text x="10" y="96">radio TX</text><text x="10" y="122">radio RX</text>
    <text x="10" y="148">sleep</text><text x="10" y="174">reg Iq</text><text x="10" y="200">self-discharge</text>
  </g>
  <g stroke="currentColor">
    <rect x="150" y="31" width="164" height="18" class="f-acc"/>
    <rect x="150" y="57" width="159" height="18" class="f-acc"/>
    <rect x="150" y="83" width="143" height="18" class="f-acc"/>
    <rect x="150" y="109" width="79" height="18" class="f-acc"/>
    <rect x="150" y="135" width="385" height="18" class="f-warn"/>
    <rect x="150" y="161" width="113" height="18" class="f-mut"/>
    <rect x="150" y="187" width="56" height="18" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="320" y="44">2.18 µW 14.9 %</text><text x="315" y="70">2.12 µW 14.5 %</text><text x="299" y="96">1.91 µW 13.0 %</text>
    <text x="235" y="122">1.06 µW 7.2 %</text><text x="541" y="148">5.14 µW 35.0 %</text><text x="269" y="174">1.50 µW 10.2 %</text>
    <text x="212" y="200">0.75 µW 5.1 %</text>
    <text x="10" y="228">활성 4 항 합 7.27 µW ≈ 고정 3 항(슬립·Iq·자가방전) 합 7.39 µW — 주기 10 s 는 두 세계의 경계다.</text>
    <text x="10" y="244">주기 1 s → 활성 72.7 µW 가 지배 (275 일). 주기 60 s → 고정 손실이 지배 (2556 일).</text>
  </g>
</svg>
```

**주기 스윕 (실제 출력):**

```text
   주기[s]  활성합[µW]  슬립+Iq+자가[µW]  수명[일]   최대 항목
       0.1    726.618       6.593              30.0   MCU active
       1.0     72.662       7.316             275.1   MCU active
      10.0      7.266       7.388            1501.3   sleep
      60.0      1.211       7.395            2556.4   sleep
     600.0      0.121       7.396            2926.6   sleep
    3600.0      0.020       7.396            2966.4   sleep
   주기 → ∞ 상한: 2974.5 일 (8.15 년) — 슬립·Iq·자가방전만으로 정해진다
```

결론:
- **평균 전력 = A / T + B** (A = 회당 활성 에너지, B = 고정 손실). T 가 작으면 A/T 가, 크면 B 가 지배한다. 10 s 근처가 경계다. 경계 위에서 "무선 패킷을 줄이자" 는 노력은 거의 효과가 없고, 슬립 전류·레귤레이터 경부하 효율·Iq 를 봐야 한다.
- 주기를 아무리 늘려도 **8.15 년 상한**이 있다. 그리고 그 전에 코인셀의 실사용 수명(자가방전·전해질 열화, 제조사 권장)과 제품 요구가 먼저 걸린다. 자가방전을 "공칭의 % / 년, 선형" 으로 둔 것은 단순화다.
- 이 모델이 **안 잡는 것**: 코인셀은 내부 저항이 커서 mA 펄스에서 전압이 처지고(컷오프 → 조기 종료), 그래서 실제 설계는 버퍼 커패시터를 둔다. 저온에서 용량이 줄고, 펄스 부하가 사용 가능 용량을 깎는다. 이 효과들을 이 레슨은 `derate` 하나로 뭉뚱그렸다. 숫자는 셀 데이터시트의 펄스 시험 곡선으로 정해야 한다.
- 펌웨어 연결: STM32 의 Stop/Standby, nRF 의 System ON idle / System OFF 처럼 MCU 저전력 모드는 "무엇이 꺼지고(클럭? 레귤레이터? SRAM?) 무엇이 남는가" 로 이 레슨의 idle/sleep 계층과 같다. 정확한 전류는 부품·조건마다 데이터시트로 확인한다. 실제 모드 설정은 BM-M11 에서 다룬다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 59/59 passed, 0.006 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 깨끗
cc -std=c11 -Wall -Wextra -Werror -O2 demo_energy_curve.c -o demo && ./demo   # 연속 critical frequency + svg 좌표
```

(Apple M6, Apple clang 21.0.0. 결정적 계산이라 타이밍·load 영향 없음. 실행 당시 `uptime` load average 는 다른 세션 때문에 15–127 이었다. 출력은 load 와 무관하게 바이트 단위로 같다.)

설계 메모:
- **바뀐 곳은 12 개 함수 본문뿐**이다. crossover 이분법은 하네스가 `setter_t` 콜백으로 파라미터 하나만 바꿔 가며 `E_race_sleep − E_pace` 의 부호 변화를 찾는다. 양 끝 부호가 같으면 NAN 이다("crossover 없음" 도 답이다).
- **에너지 창을 [0, D] 로 고정**한 것이 핵심 설계다. 실행 에너지만 비교하면 race-to-idle 의 휴식 손해(0.383 J)가 통째로 빠진다.
- **feasible 판정에 1e-12 상대 여유**: `t_run == D` 인 경계에서 부동소수 오차로 마감을 "넘는" 일을 막는다(`slack 0 → REST_NONE` CHECK).
- 기대값은 Python 으로 따로 계산해 유효숫자 10 자리로 적었고, 비교는 상대오차 1e-9 다. 지수함수가 들어가니 정확히 같음을 요구하지 않는다. 대신 모델끼리의 관계(V 두 배 → P_dyn 4 배, 온도 crossover = 25 + 25·ln(누설 crossover))는 1e-12 / 1e-6 으로 묶었다.

## 7. 자주 하는 실수

1. **전력(W)으로 비교한다.** "300 MHz 는 63 mW, 1.8 GHz 는 561 mW 라 9 배 아낀다" 는 틀린 말이다. 같은 작업의 에너지는 0.213 J vs 0.319 J (1.5 배) 다. 고친 생각: 항상 `E = ∫P dt` 를 **같은 시간 창**에서 비교한다.
2. **휴식 구간을 뺀다.** 실행 에너지만 보면 race-to-idle = 0.312 J 로 그럴듯하지만, 같은 창의 휴식까지 넣으면 0.694 J 다. 고친 코드: `p.e_total = p.e_run + p.e_rest;` 와 `slack = D − t_run`.
3. **"주파수를 내려서 동적 에너지를 아낀다".** `αCV²f × N/f = αCV²N` 이다. 전압이 그대로면(MCU 처럼 DVFS 없음) 주파수를 내려도 동적 에너지는 그대로이고, 바닥 전류 × 늘어난 시간만큼 손해다(연습 1).
4. **메모리 스톨까지 1/f 로 줄인다.** `t_run = N/f` 로 쓰면 m = 0.5 작업에서 race 의 이득을 두 배로 과대평가한다. 고친 코드: `N·(1−m)/f + N·m/f_ref`.
5. **sleep 을 공짜로 본다.** `e_trans`, `t_trans` 를 빼면 1 ms 휴식에도 power-gate 하라는 결론이 나온다. 기준 모델에서 4.5 ms 미만이면 idle 이 싸다. 고친 코드: `sleep_breakeven` 과 `slack ≥ t_trans` 검사.
6. **누설을 상수로 둔다.** 온도·전압 의존을 빼면 critical frequency 가 25 °C 의 480 MHz 에 고정된다. 85 °C 의 820 MHz 를 놓치고, 공정 코너별 차이도 못 본다.
7. **mAh 를 에너지로 쓴다.** 1.8 V 부하의 평균 전류를 3.0 V 배터리의 mAh 로 바로 나누면 레귤레이터 변환(전압비·효율)을 무시한다. 고친 코드: 모두 W 로 바꾼다. `batt_energy_j = mAh × V × 3.6`, 부하 에너지 ÷ η.
8. **슬립 전류를 MCU 데이터시트 typ 하나로 끝낸다.** 레귤레이터 Iq, 경부하 효율, 센서 대기 전류, 풀업 저항 누설이 다 빠진다. 기준 노드에서 Iq 하나가 10 % 다.

## 8. 면접 Q&A

<details><summary>Q1. race-to-idle 이 항상 이기나요?</summary>

아니다. 조건이 붙는다. (1) 휴식이 **깊은 sleep**(파워 게이팅)이어야 한다. 클럭 게이팅 idle 이면 높은 전압의 누설을 계속 내서 기준 모델에서 0.694 J 로 최악이다. (2) 켜져 있는 동안의 **V 와 무관한 전력**(플랫폼·uncore·PLL·DRAM 컨트롤러)이 커야 한다. 기준 모델에서 P_static 이 58 mW 를 넘으면 race-to-sleep 이 pace 를 이기고, 347 mW 를 넘으면 최고 OPP 자체가 최적이다. (3) 작업이 **계산 바운드**여야 한다. 스톨이 절반을 넘으면(m > 0.51) 진다. 반대로 코어 누설처럼 전압과 함께 급하게 주는 몫이 지배하면 critical speed 근처에서 도는 게 이긴다.

꼬리질문: "그럼 휴대폰 SoC 에서 실제로는?" → 코어 하나만 보면 critical speed 가 있지만, 시스템 전체로 보면 화면·메모리·SoC 패브릭이 "작업이 끝날 때까지 깨어 있는" 비용이 커서 짧은 버스트는 빨리 끝내고 깊이 재우는 쪽이 자주 유리하다고 알려져 있다(Le Sueur & Heiser 2010 이 측정으로 "DVFS 이득이 줄고 있다" 고 보고). 다만 이것은 플랫폼마다 다르니 측정으로 답한다. Linux EAS(Energy Aware Scheduling)가 OPP 별 전력 표(Energy Model)를 들고 매번 계산하는 이유다.
</details>

<details><summary>Q2. critical speed 를 유도하고, 왜 존재하는지 설명하세요.</summary>

계산 바운드 작업의 사이클당 에너지는 `e(f) = αC·V(f)² + (P_leak(V(f)) + P_static)/f` 다. 첫 항은 V 와 함께 증가하고, 둘째 항은 1/f 로 줄어든다(V 가 내려가면 누설도 준다). 첫 항만 있으면 e(f) 는 f 에 대해 단조라 최저 OPP 가 최적이다. **켜져 있는 시간에 비례하는 항**이 있어야 내부 최소가 생긴다. `de/df = 0` 에서 "f 를 조금 올려 아끼는 시간 × 켜짐 전력" = "V² 증가분" 이다. 이 모델에서 25 °C 는 480 MHz, 85 °C 는 820 MHz 다. 곡선 바닥이 평평해서(400–650 MHz 가 최소의 2 % 안) 실무 규칙은 "critical 아래로 내리지 마라" 다(Jejurikar 등 2004 의 leakage-aware DVS).

꼬리질문: "25 °C 에서 누설만 있으면 critical 이 왜 최저 OPP 로 남나요?" → 이 모델의 누설은 `V·e^(3V)` 라 0.6 V 에서 0.016 W, 1.0 V 에서 0.091 W 로 5.5 배 차이다. 시간은 6 배 늘지만 누설 **전력**이 그만큼 줄어서, 동적 항을 이길 만큼 크지 않다. 정적 전력(V 무관)이나 고온이 필요하다. 누설의 전압 의존이 약하면(`k_v = 0`) 반대로 최고 OPP 가 critical 이 된다.
</details>

<details><summary>Q3. 메모리 바운드 작업에서 DVFS 는 어떻게 해야 하나요?</summary>

스톨 시간은 코어 클럭과 무관하다. 그래서 f 를 올려도 시간이 별로 안 줄고, 스톨 중에도 클럭 트리가 돌며 전력을 낸다. 기준 모델(P_static 100 mW)에서 m = 0.51 이 race-to-sleep 과 pace 의 경계이고, m = 1 이면 어떤 누설 배율에서도 최저 OPP 가 최적이다(CHECK). 실무: PMU 로 스톨 비율을 보고(이벤트 이름은 코어마다 다르다), 코어는 내리고, 대신 메모리/버스 쪽 주파수(Linux devfreq)를 본다. 병목이 DRAM 대역폭이면 메모리 클럭을 내리면 손해다.

꼬리질문: "f_ref 에서 잰 m 이 다른 f 에서도 같은가?" → 아니다. f 를 내리면 같은 스톨 시간이 더 적은 사이클이 되니 사이클 비율이 줄어든다. 그래서 이 모델은 m 을 **f_ref 에서의 시간 비율**로 정의했다. 사이클 기반 정의를 쓰면 f 마다 다시 계산해야 한다(Miyoshi 등 2002 의 critical power slope 분석이 이 구분을 다룬다).
</details>

<details><summary>Q4. 펌웨어/OS 는 깊은 sleep 에 들어갈지를 어떻게 결정하나요?</summary>

각 idle 상태에 진입+복귀 지연과 **최소 체류 시간**(손익분기, 이 레슨의 `sleep_breakeven`)이 있다. Linux 의 `arm,idle-state` DT 바인딩은 `entry-latency-us`, `exit-latency-us`, `min-residency-us` 를 갖는다. cpuidle governor 는 다음 타이머·과거 패턴으로 idle 길이를 예측하고, 그보다 min-residency 가 짧고 exit latency 가 지연 제약(PM QoS)을 만족하는 가장 깊은 상태를 고른다. MCU 펌웨어(RTOS tickless idle)도 같다: 다음 타이머까지 시간이 stop 모드 손익분기보다 길면 stop, 아니면 WFI.

꼬리질문: "예측이 틀리면?" → 손익분기보다 짧게 깨어나면 e_trans 만 내고 이득이 없다(손해). 반대로 너무 얕게 자면 누설을 계속 낸다. 지연 측면에서는 깊은 상태의 exit latency 가 인터럽트 응답을 늦춰 실시간 마감을 깰 수 있다. 그래서 지연 제약이 손익분기보다 먼저 상태를 제한한다.
</details>

<details><summary>Q5. 코인셀 센서 노드 수명을 추정해 보세요. 무엇이 지배하나요?</summary>

전부 배터리 쪽 평균 전력으로 바꿔 더한다. 활성 항(MCU·센서·TX·RX)은 회당 에너지 ÷ 효율 ÷ 주기, 고정 항(슬립 ÷ 경부하 효율, 레귤레이터 Iq, 자가방전)은 주기와 무관하다. 기준 노드(10 s)는 14.65 µW, 1900.8 J 사용 가능 → 1501 일. 지배 항은 슬립(35 %)이다. 활성 합 7.27 µW 와 고정 합 7.39 µW 가 거의 같아서 10 s 가 경계다. 1 s 주기면 활성이 10 배(275 일), 60 s 이상이면 고정 손실이 지배하고 상한 8.15 년에 수렴한다.

꼬리질문: "그 숫자를 믿나요?" → 모델 밖의 위험이 크다. 코인셀 내부 저항 때문에 mA 펄스(TX)에서 전압이 처져 브라운아웃/컷오프가 일찍 온다(버퍼 커패시터, 펄스 시험 곡선 확인). 저온 용량 감소, 레귤레이터의 µA 부하 효율(데이터시트 곡선), 센서 대기 전류, 풀업. 검증은 실측으로 한다. 전류 프로파일러로 한 주기의 µA–mA 동적 범위를 재고(측정 사슬의 함정은 SIV-A03), 가속 시험을 한다.
</details>

<details><summary>Q6. V 를 낮추면 왜 f 도 낮춰야 하나요? OPP 표는 누가 정하나요?</summary>

게이트 지연은 대략 `V / (V − Vth)^a` 꼴(Sakurai–Newton alpha-power law, a ≈ 1.3–2)로 전압이 낮을수록 길어진다. 그래서 같은 클럭 주기 안에 임계 경로가 끝나려면(setup slack ≥ 0, ARCH-B13) 낮은 V 에서는 f 를 내려야 한다. OPP 표는 실리콘 특성화로 "각 f 에서 timing 이 통과하는 최소 V + 마진(전압 강하·노이즈·노화·온도)" 을 정한다. 칩마다 다르면 binning(퓨즈에 등급 기록)이나 AVS(Adaptive Voltage Scaling, 온칩 링 오실레이터 등으로 V 를 칩별로 조정)를 쓴다.

꼬리질문: "OPP 표에서 V 가 줄어드는 항목을 발견하면?" → `opp_table_ok` 가 거부하는 경우다. f 가 오르는데 V 가 내려가면 높은 f 에서 timing 마진이 부족할 수 있다. 표 오류(복사 실수, 단위 mV/µV 혼동)부터 의심한다. 같은 V 에 여러 f 는 정상이다(최저 전압 바닥에서 클럭만 내리는 구간).
</details>

<details><summary>Q7. DVFS 전환 순서와 비용은? 이 모델은 그걸 왜 무시해도 되나요?</summary>

올릴 때는 **V 먼저, 확인 후 f**, 내릴 때는 **f 먼저, 그다음 V** 다. 반대로 하면 잠깐이라도 "높은 f + 낮은 V" 가 되어 timing 위반이 난다. 비용은 레귤레이터 전압 slew 시간(PMIC 사양, 보통 수 µs–수십 µs 수준. 칩마다 확인), PLL 재잠금 또는 글리치 없는 클럭 전환, 그 동안 멈추거나 느려진 실행이다. 이 레슨은 작업이 수백 ms–s 단위라 전환 몇 번의 비용을 무시했다. 작업이 µs–ms 단위이거나 governor 가 OPP 를 자주 흔들면(진동) 이 비용이 지배할 수 있다. 그래서 governor 는 hysteresis 와 rate limit 을 둔다.

꼬리질문: "여러 코어가 한 레일을 공유하면?" → 레일 전압은 그 레일에서 가장 높은 OPP 를 요구하는 코어가 정한다. 느린 코어도 높은 V 로 돌아 누설이 늘어서, 코어별 레일과 클러스터 레일 사이에 면적·PMIC 비용 대 에너지의 트레이드오프가 생긴다.
</details>

<details><summary>Q8. 이 모델의 파라미터를 실리콘에서 어떻게 얻고, 모델이 맞는지 어떻게 아나요?</summary>

레일별 전력 모니터로 (T, V, f) 스윕을 재서 `P = L(T,V) + C_eff·V²f` 를 맞춘다(SIV-A03: 같은 (T, V) 에서 f 만 바꾼 슬라이스 회귀 → 절편 = 누설, 기울기 = C_eff). 정적 전력은 클럭 게이팅 idle 에서, P_sleep 과 e_trans 는 sleep 진입·복귀 전류 파형을 적분해서 얻는다. 검증은 모델 예측 vs 실측 에너지(벤치마크 작업 전체)로 한다. 이 레슨의 crossover 근처(예: m ≈ 0.5)에서 실측 승자가 바뀌는지 보는 것이 좋은 시험이다.

꼬리질문: "α 는 따로 잴 수 있나?" → 아니다. 측정에는 α·C 곱(C_eff)만 보이고, 워크로드마다 다르다. 그래서 OPP 별 전력 표는 대표 워크로드 기준이고, EAS 같은 모델도 "평균적인 상대 비용" 으로 쓴다.
</details>

## 9. 추가 연습

1. **DVFS 없는 MCU.** 고정 1.8 V, 활성 전류 `50 µA/MHz × f + 0.3 mA`, 슬립 2 µA, 작업 10⁶ cycle, 마감 100 ms. 플래시는 24 MHz 마다 wait state 1 개가 늘고(`ws = ceil(f/24 MHz) − 1`), WS 하나당 CPI +0.15(프리페치·캐시가 숨기고 남은 몫이라는 가정). 8–80 MHz 중 에너지 최적 클럭은? WS 를 무시하면?
   <details><summary>답</summary>

   `E = V·(k f + I0)·N·CPI(f)/f + V·I_sleep·(D − t)`. 실제 출력: 8 MHz 157.4 µJ, 16 → 123.9, **24 → 112.7**, 32 → 123.1 (WS 1), 48 → 116.7, 64 → 128.3 (WS 2), 80 → 140.6 µJ (WS 3). 최적은 **24 MHz**, WS 가 생기기 직전 클럭이다. WS 를 무시하면 80 MHz 가 최적이다(97.1 µJ). 전압이 고정이면 동적 몫 `V·k·N` 은 f 와 무관하고, 바닥 전류 `I0·N/f` 만 f 를 올릴수록 줄어서 race 가 이긴다. 현실의 STM32 류는 클럭에 따라 wait state 와 전압 범위(voltage scaling range)가 같이 바뀌므로 표를 데이터시트에서 확인한다(ARCH-B12).
   </details>
2. **2 년 수명 목표.** 기준 노드에서 수명이 정확히 730 일이 되는 최소 깨어남 주기를 닫힌 식으로 구하고 `node_life_days` 로 역검증하라. 어떤 주기로도 넘을 수 없는 수명은?
   <details><summary>답</summary>

   `P(T) = A/T + B`. A = 회당 배터리 쪽 활성 에너지 − 깨어 있는 동안 안 쓴 슬립 몫 = 72.662 − 0.081 = 72.581 µJ, B = 슬립(÷η) + Iq + 자가방전 = 7.396 µW. 목표 P* = 1900.8 J / (730 × 86400 s) = 30.137 µW → `T_min = A / (P* − B)` = **3.192 s**. 역검산 730.000 일. 상한은 `E_usable / B` = **2974.5 일**이다. B 가 P* 보다 크면 어떤 주기로도 목표를 못 맞춘다. 이때는 슬립 전류나 Iq 부품을 바꿔야 한다.
   </details>
3. **누설-온도 되먹임 (열폭주).** 최고 OPP 100 % 부하, 주변 25 °C, 열저항 θ [°C/W]: `Tj = 25 + θ·P(Tj)`. 반복법으로 고정점을 구하고, 고정점이 사라지는 θ 를 (a) 반복법 이분 탐색, (b) 접점 조건으로 구해 비교하라.
   <details><summary>답</summary>

   실제 출력: θ = 10 → 30.85 °C, 20 → 37.39, 30 → 45.24, 40 → 56.81, 44 → 68.73 °C (누설 0.524 W), **46 이상 고정점 없음**. 접점 조건은 `θ·b·e^((T−T0)/Ts)/Ts = 1` 과 고정점 식을 합친 `T0 + Ts·ln(Ts/(θb)) = T_amb + θa + Ts` (a = P_dyn + P_static = 0.47 W, b = 25 °C 누설 0.0911 W). 두 방법 모두 **44.0775 °C/W**, 접점 온도 70.7 °C 다. 경계 온도가 Tj_max(보통 100 °C 대)보다 한참 아래라는 점이 중요하다. "Tj 가 아직 71 °C 니까 괜찮다" 는 판단이 틀릴 수 있다. 실제 칩은 이걸 throttling 으로 막는다(SIV-A04 가 governor 를 검증).
   </details>
4. **E vs EDP vs ED²P.** 기준 작업의 실행 구간만으로 에너지, EDP(E·t), ED²P(E·t²) 를 최소로 하는 OPP 를 각각 구하라. 왜 다른가?
   <details><summary>답</summary>

   실제 출력: E 최소 **OPP1** (0.199 J = critical), EDP 최소 **OPP4** (0.173 J·s), ED²P 최소 **OPP4** (0.096 J·s²). EDP 는 "에너지와 성능을 같은 무게로" 보는 지표라 시간 단축을 더 쳐준다. 서버·데스크톱 설계는 EDP/ED²P(전압 스케일링에 거의 불변인 지표로 알려짐)를, 배터리·마감 기반 임베디드는 "마감 안에서 E 최소" 를 쓴다. 어떤 지표를 쓰는지가 결론을 바꾼다. 면접에서는 지표부터 정하고 답한다.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력:

```text
== 연습 1: 고정 전압 MCU, 플래시 wait state ==
   f[MHz]  WS  CPI   E_job[µJ]  (WS 무시 시)
       8   0   1.00   157.410    157.410
      16   0   1.00   123.885    123.885
      24   0   1.00   112.710    112.710
      32   1   1.15   123.137    107.122
      48   1   1.15   116.711    101.535
      64   2   1.30   128.256     98.741
      80   3   1.45   140.582     97.065
   최적: WS 반영 24 MHz, WS 무시 80 MHz
== 연습 2: 2 년 수명 → 최소 깨어남 주기 ==
   E_active(배터리쪽)=72.662 µJ/회  A=72.581 µJ  B=7.396 µW  P*=30.137 µW  → T_min = 3.192 s, 검산 수명 730.000 일
   어떤 주기로도 넘을 수 없는 상한 = E_usable / B = 2974.5 일
== 연습 3: 누설-온도 되먹임 (최고 OPP 100% 부하, T_amb 25 °C) ==
   θ =   10 °C/W : Tj =  30.85 °C, P = 0.5851 W (누설 0.1151 W)
   θ =   20 °C/W : Tj =  37.39 °C, P = 0.6196 W (누설 0.1496 W)
   θ =   30 °C/W : Tj =  45.24 °C, P = 0.6747 W (누설 0.2047 W)
   θ =   40 °C/W : Tj =  56.81 °C, P = 0.7951 W (누설 0.3251 W)
   θ =   44 °C/W : Tj =  68.73 °C, P = 0.9938 W (누설 0.5238 W)
   θ =   46 °C/W : 고정점 없음 (열폭주)
   θ =   50 °C/W : 고정점 없음 (열폭주)
   열폭주 경계 θ: 반복법 44.0775, 접점 식 44.0775 °C/W (접점 온도 70.72 °C)
== 연습 4: E vs EDP vs ED²P (실행만, m=0) ==
   OPP  E_run[J]  t_run[s]  EDP[J·s]  ED²P[J·s²]
   0    0.21155   3.3333    0.70516   2.35053
   1    0.19905   1.6667    0.33175   0.55291
   2    0.22000   1.0000    0.22000   0.22000
   3    0.26017   0.7143    0.18584   0.13274
   4    0.31173   0.5556    0.17318   0.09621
   최적 OPP: E 1, EDP 4, ED²P 4
7/7 passed
```

## 10. 핵심 정리

- `E_dyn = αCV²N`: **동적 에너지는 f 가 아니라 V² 로 준다.** f 는 "켜져 있는 시간" 을 통해서만 누설·정적 몫에 영향을 준다.
- 비교는 **같은 창 [0, D] 의 에너지(J)** 로 한다. race-to-idle(클럭 게이팅) 0.694 J vs race-to-sleep 0.319 J vs pace 0.213 J vs optimal 0.204 J (기준 모델).
- sleep 손익분기 = `max(t_trans, (e_trans − P_sleep·t_trans)/(P_idle − P_sleep))`. Linux `min-residency-us` 와 같은 개념이고, 기준 모델은 4.5 ms 다.
- **critical speed** 는 누설·정적 전력이 있어야 생긴다. 25 °C 480 MHz → 85 °C 820 MHz 로 뜨거울수록 오른다. 곡선 바닥은 평평하니 "그 아래로 내리지 마라" 가 규칙이다.
- race 가 이기는 조건: 깊은 sleep + 전압으로 안 줄어드는 켜짐 전력이 큼(P_static > 58 mW) + 계산 바운드(m < 0.51). 지수 누설만으로는 최고 OPP 가 최적이 되지 않는다.
- 센서 노드: `P = A/T + B`. 10 s 주기에서 활성 ≈ 고정 손실, 그보다 길면 슬립·Iq·자가방전이 지배하고 상한은 E_usable/B 다. `mAh × V × 3.6 = J`, 모든 항을 배터리 쪽 W 로 바꿔 더한다.
- 모델 밖 위험: DVFS 전환 비용, 누설-온도 되먹임(θ 44 °C/W 에서 열폭주), 공유 레일, 코인셀 펄스 전압 강하.

## 참고자료

- J. M. Rabaey, A. Chandrakasan, B. Nikolić, *Digital Integrated Circuits: A Design Perspective*, 2nd ed., 2003 — 5 장 전력: 동적 `α_{0→1}·C·V²·f`(α 정의가 0→1 전이 기준임에 주의), 누설, 단락 전류.
- N. H. E. Weste, D. M. Harris, *CMOS VLSI Design*, 4th ed., 2010 — 5 장 Power: 동적/정적 전력, DVFS, 파워 게이팅.
- T. Sakurai, A. R. Newton, "Alpha-power law MOSFET model and its applications to CMOS inverter delay and other formulas", IEEE JSSC 25(2), 1990 — 지연 vs 전압.
- R. Jejurikar, C. Pereira, R. Gupta, "Leakage Aware Dynamic Voltage Scaling for Real-Time Embedded Systems", DAC 2004 — critical speed.
- A. Miyoshi, C. Lefurgy, E. Van Hensbergen, R. Rajamony, R. Rajkumar, "Critical Power Slope: Understanding the Runtime Effects of Frequency Scaling", ICS 2002 — 메모리 바운드와 race vs DVFS.
- E. Le Sueur, G. Heiser, "Dynamic Voltage and Frequency Scaling: The Laws of Diminishing Returns", HotPower 2010 — 실측 기반 race-to-idle 논의.
- Linux kernel 문서: `Documentation/power/energy-model.rst` (Energy Model), `Documentation/scheduler/sched-energy.rst` (EAS), devicetree 바인딩 `arm/idle-states` (`entry-latency-us`, `exit-latency-us`, `min-residency-us`), cpuidle governor — 정확한 경로·필드는 커널 버전별로 확인.
- 코인셀(CR2032) 제조사 데이터시트 — 공칭 용량은 저전류 연속 방전 기준이다. 펄스 부하·저온 용량은 셀별 시험 곡선으로 확인한다(이 레슨의 220 mAh / 3.0 V / derate 0.8 은 대표값).
- 이 노트의 관련 레슨: SIV-A03 (같은 모델을 측정에서 **맞추기**), SIV-A04 (열 throttling governor 검증), ARCH-B12 (플래시 wait state), ARCH-B13 (timing slack → V-f 관계), SIV-A06 / SIV-A11 (전원 시퀀싱·전원/클럭 도메인 검증). 다음 단계: AIPERF-A08 (가속기 DVFS), NPU-A07 (NPU 전력), BM-M11 (MCU 저전력 모드 실습), ARCH-A09 (전원 시퀀서).
