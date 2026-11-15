---
id: SIV-A06
time: 110분
updated: 2026-11-08
---
# SIV-A06 보드 레벨 검증: 전원 시퀀싱과 클럭

## 학습 목표

- 스펙 문장("A가 90%에 도달한 뒤 B는 최소 100 µs 후에 10%를 넘는다")을 파형 위의 **측정 연산**(임계 통과 시각, 10–90% 전이, 정착 후 DC, glitch 이벤트)으로 번역하는 체커를 직접 작성한다.
- 판정을 PASS/FAIL 두 값이 아니라 **PASS / FAIL / INDET / INVALID** 로 나누어, "보드가 나쁜 것"과 "측정이 나쁜 것"과 "이 측정으로는 판단 불가"를 구분한다.
- 샘플 주기보다 좁은 glitch는 **볼 수 없다**는 것을 point / boxcar / peak-detect 세 방식의 실측 표로 보이고, 보고서가 자신의 사각지대를 명시하게 만든다.
- `ppm = (f_meas − f_nom)/f_nom × 1e6`, gated counter의 ±1 count 분해능, PLL 비율 트리의 정확한 기대값(유리수)으로 클럭을 판정하고, 측정점 비율로 **어느 노드가 고장인지** 좁힌다.
- 합성 불량 보드 로그(순서 위반, 느린 ramp, brownout, 전원 차단 순서, DC 오차, 오프 주파수 PLL)를 넣어 디버그 엔지니어가 바로 움직일 수 있는 리포트를 낸다.

## 1. 문제

첫 보드가 랩에 도착했을 때 펌웨어를 올리기 **전에** 하는 일이 전원과 클럭 확인이고, Rivian·NVIDIA·Apple 보드/실리콘 검증 면접의 "bring-up 첫날 무엇을 어떻게 보나?"라는 질문이 정확히 이 레슨이다.

이 레슨의 측정 대상은 가상의 **SENSE-EVB rev A** 보드다. 레일과 타이밍은 모두 **대표값(가정)** 이며, 실제 순서는 SoC 데이터시트의 power sequencing 표, PMIC OTP(공장 프로그래밍된 시퀀스), 보드 설계 문서에서 온다. 분석 로직은 합성 로그로 맥에서 검증한다.

> **env_note** — 실제 측정은 랩 장비(오실로스코프/DAQ, 주파수 카운터)가 필요하다. 합성 로그의 형식은 DAQ CSV 내보내기를 흉내 냈다. 장비가 있을 때의 절차는 §5 Part C 끝에 표로 정리했다. 이 맥에서는 장비를 쓰지 않았고, 아래 출력은 모두 합성 데이터에서 실제로 돌린 결과다.

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">전원 시퀀스: 스펙 문장을 측정 연산으로</text>
  <line x1="50" y1="280" x2="740" y2="280" stroke="currentColor"/>
  <line x1="50" y1="280" x2="50" y2="40" stroke="currentColor"/>
  <text x="700" y="298" fill="currentColor" font-size="12">t (us)</text>
  <text x="14" y="50" fill="currentColor" font-size="12">V</text>
  <path d="M50 270 L90 270 L170 90 L600 90 L640 100 L690 262 L740 270" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="600" y="82" fill="currentColor" font-size="12">VDD_1V8</text>
  <path d="M50 275 L190 275 L280 160 L510 160 L545 240 L570 272 L740 275" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="420" y="152" fill="currentColor" font-size="12">VDD_1V1 (1V8의 자식)</text>
  <line x1="50" y1="108" x2="740" y2="108" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="54" y="104" fill="currentColor" font-size="12">90%</text>
  <line x1="50" y1="252" x2="740" y2="252" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="54" y="248" fill="currentColor" font-size="12">10%</text>
  <circle cx="162" cy="108" r="4" class="f-acc" stroke="currentColor"/>
  <circle cx="202" cy="252" r="4" class="f-acc" stroke="currentColor"/>
  <line x1="162" y1="310" x2="202" y2="310" stroke="currentColor"/>
  <text x="130" y="330" fill="currentColor" font-size="12">최소 지연 min_us (1V8@90% → 1V1@10%)</text>
  <line x1="162" y1="108" x2="162" y2="310" stroke="currentColor" stroke-dasharray="2 3"/>
  <line x1="202" y1="252" x2="202" y2="310" stroke="currentColor" stroke-dasharray="2 3"/>
  <rect x="210" y="46" width="330" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="375" y="64" text-anchor="middle" fill="currentColor" font-size="12">정착 후 판정 창: DC(중앙값) + glitch 검사</text>
  <path d="M380 90 L384 128 L392 128 L396 90" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="402" y="128" fill="currentColor" font-size="12">glitch: t0, 폭, 깊이</text>
  <path d="M646 100 L690 262" fill="none" stroke="currentColor" stroke-width="3"/>
  <text x="600" y="150" fill="currentColor" font-size="12">power-down</text>
  <text x="600" y="166" fill="currentColor" font-size="12">역순 + 최소 지연</text>
  <rect x="470" y="194" width="270" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="482" y="214" fill="currentColor" font-size="12">PASS 는 "관측된 샘플에서" 위반이 없다는 뜻</text>
  <text x="482" y="232" fill="currentColor" font-size="12">Ts 보다 좁은 glitch 는 샘플 사이로 빠질 수 있다</text>
</svg>
```

체커가 다루는 입력과 심어 넣은 불량은 다음과 같다. `gen_rail_logs.py` 가 시드 고정으로 모두 생성한다.

| 파일 | 심어 넣은 것 (참값은 `truth.json`) | 체커가 내야 할 결과 |
|---|---|---|
| `board_good.csv` | 없음 (잡음 σ = max(2 mV, 0.2 % nom), 타임스탬프 지터 0.1 µs, overshoot 2.5 %) | PASS, findings 없음 |
| `board_bad_order.csv` | PMIC OTP 오류: `VDD_3V3` 가 `VDD_1V1` 보다 먼저 ramp | `order_on` FAIL, 규칙 이름과 Δ |
| `board_bad_ramp.csv` | soft-start 커패시터 오류: `VDD_3V3` 10–90 % = 1440 µs | `ramp` FAIL, 값 복원 |
| `board_bad_brownout.csv` | `VDD_1V8` 12 %/80 µs 강하, 자식 `VDD_1V1` 은 20 µs 뒤 8 %/60 µs | 1V8 = primary, 1V1 = secondary |
| `board_bad_powerdown.csv` | `VDD_0V8` 이 `VDD_1V8` 보다 먼저 꺼짐 | `order_off` FAIL |
| `board_bad_dc.csv` | FB 저항 오류: `VDD_1V1` = 1.17 V (+6.4 %) | `dc` FAIL (glitch로 중복 보고 안 함) |
| `board_bad_timestamps.csv` | 전압은 정상, 타임스탬프가 뒤로 감 + 중복 | **INVALID** (FAIL 아님) |
| `clock_meas.json` | good / PLL_DDR 잘못된 N / XO +120 ppm / APB 분주기 / 짧은 게이트 | FAIL 위치와 비율 힌트, 짧은 게이트는 INDET |

### Part A — 파형 측정과 클럭 기대값

- `first_crossing(t, v, level, rising, hold)` — 선형 보간 통과 시각, `hold` 샘플 유지 조건. `last_crossing(...)` — 전원 차단용: 끝까지 넘어간 상태로 남는 마지막 통과.
- `measure_ramp(t, v, low, high, rising)` — 10–90 %. far 통과를 먼저 잡고 거꾸로 near를 찾는다.
- `detect_glitches(t, v, lo, hi, hyst, min_samples)` — 한계 통과 → 시작, hyst 만큼 안쪽 복귀 → 종료. 폭(보간)·극값·샘플 수.
- `eval_rule(delta, min_us, max_us, u)` — 불확도 u를 반영한 3값 판정. `dc_check` — 정착 구간 중앙값의 ±tol%.
- `ppm_error`, `clock_tree_expected(tree)` — `Fraction` 으로 PLL 트리의 정확한 기대값, 사이클/없는 부모/VCO 범위 위반 거부. `counter_read`, `gate_resolution_ppm`, `required_gate_s`.

### Part B — 이 방법으로 안 보이는 것: 샘플링·잡음·타임스탬프·동률

- `check_timestamps` — nonmono/gap/short/rate → 로그 자체를 무효화. 지터 측정.
- `crossing_uncertainty(Ts, slope, sigma_v, jitter, skew)` — 한 통과의 시각 불확도, 참값 대비 커버리지 ≥ 99 % 확인. `group_simultaneous` — 동시로 볼 레일의 그룹화.
- `acquire_depth` / `detect_rate` — point vs boxcar vs peak-detect의 glitch 검출률 표.
- `detect_glitches` 로 잡음 위 false alarm vs N·Q(m) 측정, 채터링을 히스테리시스로 합치기, 임계값이 잡음 안에 있을 때 hold의 효과.
- `clock_verdict` — 가드밴드 3값 판정.

### Part C — 불량 보드 로그 캠페인과 리포트

- `measure_rail` + `check_rails` — 타임스탬프 → 레일별 측정 → 규칙 → 관측 순서 → glitch 상관(부모 → 자식 2차) → 종합 상태.
- `localize_clock_fault` + `check_clocks` — 측정 노드 비율을 트리 위에서 아래로 내려가며 고장 위치를 좁히고, 측정점 하나로 구별 불가한 후보는 `confounded` 로 밝힌다.
- 7개 로그 + 5개 클럭 보드 전부 참값으로 채점. 한계 테스트(`c8.*`): 좁은 glitch는 놓친다는 것, 캡처 부족은 INDET라는 것을 CHECK 한다.

## 2. Problem (English)

You are validating a new board. A DAQ exports multi-rail voltage captures as CSV (`t_us,<rail>,...`); a frequency counter reports edge counts for clock test points. Implement an automated checker.

```text
first_crossing(t, v, level, rising=True, hold=1, start=1) -> float | None
last_crossing(t, v, level, rising=False) -> float | None
measure_ramp(t, v, low, high, rising=True) -> (t_near, t_far, dt) | None
detect_glitches(t, v, lo, hi, hyst=0.0, min_samples=1) -> list of events
eval_rule(delta, min_us, max_us=None, u=0.0) -> "PASS" | "FAIL" | "INDET"
dc_check(level, nom, tol_pct) -> (ok, err_pct)
ppm_error(f_meas, f_nom) -> float
clock_tree_expected(tree) -> {node: Fraction}   # ValueError on cycle / unknown parent / VCO range
counter_read(f_hz, gate_s, phase) -> int
gate_resolution_ppm(f_hz, gate_s) -> float;  required_gate_s(f_hz, ppm) -> float
check_timestamps(t, Ts_nom, gap_factor=1.5) -> {ok, issues, Ts_us, jitter_us}
crossing_uncertainty(Ts, slope, sigma_v, jitter=0.0, skew=0.0) -> us
group_simultaneous(times, tie_us) -> list of groups
acquire_depth(width, depth, Ts, phase, mode) ; detect_rate(...)
clock_verdict(err_ppm, limit_ppm, u_ppm) -> "PASS" | "FAIL" | "INDET"
measure_rail(t, v, name, r, spec, ts) -> (meas, findings, glitches)
check_rails(log, spec) -> {name, status, findings, meas, blind, on_order, ts}
localize_clock_fault(tree, exp, meas_hz, tol_ppm, report_ppm=0.0) -> list of faults
check_clocks(tree, board, limits=None) -> {status, rows, faults}
```

Constraints: Python 3.9 standard library only; deterministic. A `status` is one of `PASS`, `FAIL`, `INDET`, `INVALID` (non-monotonic or gapped timestamps make the capture unusable — that is never a board FAIL). Rail tolerance applies to the settled median; excursions are glitch events. Report times are interpolated; glitch width is the time outside the limit, not outside the hysteresis re-arm level.

Example: a linear ramp `v=[0,1,2,3,4]` at `t=[0,10,20,30,40]`, `first_crossing(t,v,1.5)` → `15.0`. A DC of `1.17 V` against `1.10 V ±5 %` → `(False, +6.36)`. `eval_rule(105, 100, None, 10)` → `"INDET"`. Counter on 24 MHz with a 1 ms gate resolves ±41.7 ppm.

## 3. 시작 코드

<!-- code: starter.py -->

데이터 생성기(결정적, 이미 실행되어 있고 CSV/JSON이 함께 들어 있다):

<!-- code: gen_rail_logs.py -->

레슨 디렉터리에서 실행한다 (`python3 -I` 는 스크립트 디렉터리를 경로에서 빼므로 각 파일은 자기완결적이다).

```bash
python3 -I starter.py; echo "exit=$?"
```

실제 출력 (뒷부분):

```text
FAIL test_c7_clocks: TODO: check_clocks (Part C)
PASS c8.sub_Ts_glitch_leaves_no_trace
FAIL test_c8_limits: TODO: check_rails (Part C)
6/26 passed
exit=1
```

## 4. 힌트

<details><summary>힌트 1 — 임계 통과 찾기</summary>

"직전 샘플은 반대쪽, 이번 샘플은 넘어감"이 통과다. `(v[i-1]-level)*sign < 0 <= (v[i]-level)*sign` 로 상승/하강을 합칠 수 있다. 로그가 이미 넘어간 상태로 시작하면 통과가 없다 — `None` 을 돌려주고 호출자가 "캡처 안에 에지가 없음"으로 처리한다.
</details>

<details><summary>힌트 2 — power-down은 왜 last_crossing인가</summary>

정상 구간의 brownout도 90 %를 아래로 지나간다. "첫 하강 통과"를 쓰면 glitch를 전원 차단으로 착각한다. 전원 차단은 "그 뒤로 끝까지 아래에 머문 **마지막** 통과"다. 끝에서부터 거꾸로, 아직 위에 있는 마지막 샘플을 찾으면 된다.
</details>

<details><summary>힌트 3 — glitch 상태기계</summary>

대역 안이면 다음 샘플로. 나가면 방향(under/over)을 기억하고, 한계 밖 샘플이 나올 때마다 `i_last` 와 극값을 갱신한다. 종료 조건은 **한계보다 hyst 만큼 안쪽(rearm)** 에 닿을 때다. 폭은 시작 쪽 한계 통과와 `i_last` 다음의 한계 통과로 계산한다 — rearm 선까지가 아니다.
</details>

<details><summary>힌트 4 — 왜 3값 판정인가</summary>

Δ가 100 µs이고 불확도가 ±10 µs라면 Δ=105는 "통과"가 아니라 "이 장비로는 모른다"이다. `delta-u >= min` 이면 PASS, `delta+u < min` 이면 FAIL, 그 사이는 INDET.
</details>

<details><summary>힌트 5 — 클럭 트리</summary>

`f = parent × (mul + frac/2^bits) / div`. `Fraction` 으로 끝까지 계산하면 frac-N의 정확한 값이 나온다. 재귀 + `visiting` 집합으로 사이클을 잡고, 없는 부모는 `tree` 조회에서 잡는다.
</details>

<details><summary>힌트 6 — 고장 위치는 위에서 아래로</summary>

노드의 측정 자손 비율이 **과반 이상 같은 값이고 1이 아니면** 그 노드가 원인이다. 그 비율을 자손에서 나누어 없애고 아래로 내려가면 다중 고장도 풀린다. 측정 자손이 하나뿐이면 그 사이의 PLL과 분주기를 구별할 수 없다 — `confounded` 로 알린다.
</details>

<details><summary>힌트 7 — 체커 전체 흐름</summary>

타임스탬프 → (무효면 끝) → 레일별 측정(대역 진입, 정착 창, 중앙값, ramp, glitch) → 규칙(on/off 각각 Δ와 u) → 관측 순서 그룹화 → 부모/자식 glitch 상관 → findings 심각도로 종합 상태. DC가 틀린 레일은 glitch를 찾지 않는다(대역 밖 평탄부 전체가 "glitch"로 보이므로).
</details>

## 5. 풀이

전체 파이프라인은 "스펙을 이벤트로 번역 → 측정 → 판정(불확도 포함) → 2차 현상 정리 → 리포트"다.

### Part A — 파형에서 숫자를 뽑는다

**(1) 이벤트 정의가 곧 스펙이다.** 스펙의 한 문장은 다음 연산으로 번역된다.

| 스펙 문장 | 측정 연산 | 함정 |
|---|---|---|
| "A가 90 % 도달" | `first_crossing(A, 0.9×nom, rising, hold=3)` | hold 없으면 잡음 스파이크 |
| "B가 10 %를 넘기 전까지" | `first_crossing(B, 0.1×nom, rising, hold=3)` | 임계값이 잡음에 가까우면 조기 통과 |
| "A가 10 % 아래로 떨어진 뒤" | `last_crossing(A, 0.1×nom, falling)` | 첫 통과는 glitch일 수 있음 |
| "10–90 % ramp 시간" | `measure_ramp`, 레벨은 실측 저/고 레벨 기준 | 레벨을 nom으로 쓰면 DC 오차가 ramp로 번짐 |
| "정착 후 ±5 %" | 대역 진입 + settle_us 뒤 창의 **중앙값** | 평균은 glitch에 끌려간다 |
| "glitch 폭/깊이" | 창 내부의 대역 이탈 이벤트 | 대역과 재무장 선을 구분 |

레벨 기준은 HWLAB-B04(IEEE 181의 상태 레벨)와 같은 맥락이다. 여기서는 측정 저레벨 = 앞쪽 8개 샘플 중앙값, 고레벨 = 정착 창의 중앙값을 쓴다. 왜 `measure_ramp` 는 far를 먼저 찾나: 잡음이 10 % 근처를 여러 번 넘나들면 "첫 10 % 통과"는 앞쪽으로 치우친다. far(90 %)의 첫 통과에서 거꾸로 걸어 near의 **마지막** 통과를 잡으면 near와 far가 항상 같은 전이에 속한다.

**(2) DC와 glitch를 분리한다.** 한 샘플이 대역을 벗어나면 DC 불량인가, glitch인가? 둘을 나누지 않으면 같은 원인이 두 번 보고되거나, 짧은 이탈 하나 때문에 평균이 움직여 DC가 FAIL이 된다. 그래서 DC는 정착 창의 **중앙값**(glitch에 강건)으로 판정하고, 대역 이탈은 glitch 이벤트로 낸다. DC가 이미 대역 밖이면 glitch 검사는 건너뛴다 — 정상 구간 전체가 하나의 거대한 glitch로 나오는 중복을 막기 위해서다(`board_bad_dc` 의 결과가 DC 한 줄인 이유).

**(3) glitch 상태기계.** 시작 조건과 종료 조건을 다르게 두는 것이 히스테리시스다.

```text
대역 안 ──(lo 아래 또는 hi 위)──> 이탈 중 ──(한계 안쪽 hyst 까지 복귀)──> 대역 안
                                    │ 한계 밖이면 i_last/extreme 갱신
                                    └ rearm 전 재이탈은 같은 이벤트
```

폭은 "한계 밖에 있었던 시간"이므로 양 끝을 **한계선** 통과로 보간한다. `v=[1,1,.5,.5,1,…]`, `lo=0.9`, 재무장 없음: t0 = 1.2, t1 = 3.8, 폭 = 2.6, 샘플 2개 (`a3.times_interp`). 사각 에지의 경우 보간은 정보를 만들어 내지 않는다는 점에 주의 — 진짜 에지는 샘플 간격 안 어디든 있을 수 있다(Part C에서 실측).

**(4) 3값 판정.** 측정값은 항상 불확도와 함께 온다. `eval_rule(Δ, min, max, u)` 는

| 조건 | 결과 |
|---|---|
| Δ−u ≥ min (그리고 Δ+u ≤ max) | PASS |
| Δ+u < min (또는 Δ−u > max) | FAIL |
| 그 사이 | INDET — 측정이 부족하다는 **진짜 정보** |

INDET을 PASS로 뭉개면 위험한 보드가 출하되고, FAIL로 뭉개면 멀쩡한 보드를 며칠 디버그한다. 검증 리드는 INDET의 개수를 줄이는 것(더 빠른 샘플링, 더 짧은 프로브)을 일감으로 관리한다.

**(5) 클럭.** `ppm = (f_meas − f_nom)/f_nom × 10⁶`. 트리는 `Fraction` 으로 계산한다:

```text
XO 24 MHz ──> PLL_DDR: 24 × 88 / 1 = 2112 MHz ──> DDR_CK /2 = 1056 MHz
```

Frac-N 노드는 이진 분수로만 표현되어 정확한 기대값이 설계 공칭과 살짝 다르다. 이 레슨의 `AUD_MCLK` 는 24 MHz × (40 + 16106127/2²⁴) / 80 = **12287999.9936 Hz**로, 공칭 12.288 MHz 대비 **−0.00052 ppm** (`a5.fracN_residual_small_nonzero`). float으로 곱셈을 이어 가면 이런 잔차가 부동소수 오차와 섞여 "기대값 대비 편차"의 신뢰가 떨어진다. VCO 범위 위반은 **설정 자체의 오류**이므로 측정 전에 거부한다.

주파수 카운터는 게이트 시간 T 동안 에지를 센다. 결과 해상도는 ±1 count, 즉 **±1/T [Hz] = ±1/(f·T) [상대]**.

```text
== 5. gated counter ±1 count 분해능 (ppm)
f_hz         1ms       10ms      100ms     1s
12288000       81.38     8.138    0.8138   0.08138
24000000       41.67     4.167    0.4167   0.04167
100000000      10.00     1.000    0.1000   0.01000
1056000000      0.95     0.095    0.0095   0.00095
```

저주파 클럭일수록 게이트를 길게 해야 한다. 12.288 MHz 를 0.1 ppm 분해능으로 보려면 `required_gate_s` = 0.814 s. 현대의 reciprocal counter는 입력 사이클 수를 세고 게이트를 시간 보간기(interpolator)로 재므로 상대 분해능 ≈ t_res/T 로 입력 주파수에 덜 의존하지만, 게이트 시간의 트레이드오프는 그대로다.

### Part B — 무엇을 볼 수 없는가

**(1) 타임스탬프가 틀리면 전부 무효다.** 뒤로 가는 시간(32 비트 µs 카운터 랩어라운드, 버퍼 잘못 이어 붙이기), 중복, 샘플 드롭은 보드 결함이 아니다. `check_timestamps` 가 하나라도 찾으면 체커는 **INVALID** 를 내고 DUT에 대한 어떤 결론도 내지 않는다. `board_bad_timestamps` 는 전압이 완전히 정상이지만:

```text
=== board_bad_timestamps  STATUS=INVALID ===
INVALID rail=- check=timestamps 4 issues ['gap', 'nonmono'] first at row 300 (t=2990.0->2500.1): capture unusable, re-take
```

지터는 연속 타임스탬프 차의 표준편차로 재는데, 지터가 σ인 타임스탬프 두 개의 차이므로 측정값은 **√2·σ** 다(실측 good 보드: 0.141 µs, 참값 σ=0.1 µs). 이 √2를 모르면 지터를 과대평가한다 (`b1.jitter_measured_sqrt2`).

**(2) 임계 통과 시각의 불확도.** 한 번의 통과 시각에는 네 가지 오차가 들어간다.

```text
u = Ts/2 + skew + 3 * sqrt( sigma_t^2 + (sigma_v / slope)^2 )
```

| 항 | 의미 |
|---|---|
| Ts/2 | 파형이 샘플 간격 안에서 직선이 아닐 수 있음 (보수적으로 추가) |
| skew | 다중화 ADC의 채널 간 샘플 시각 차이 (연습 2) |
| σ_t | 타임스탬프 지터 |
| σ_v/slope | 전압 잡음이 시각 오차로 변환된 값 — 기울기가 **낮을수록** 나쁘다 |

왜 이 식을 믿나: 참값을 알고 있는 합성 ramp 300회로 확인했다(Ts=10 µs, ramp 400 µs, σ=0.2 %, 위상 분산). 실측: `u=7.40 µs`, 최악 오차 2.19 µs, RMS 0.66 µs, 300/300 포함 — **의도적으로 보수적**이다(이 파형은 거의 직선이라 Ts/2 항이 대부분 여유). `b2.empirical_coverage` 는 ≥ 99 %를 요구한다. 지수 방전의 낮은 레벨에서는 기울기가 작아져 u가 커지고, `board_bad_order` 리포트의 `u=+/-14.1us` 는 양쪽 통과의 u를 더한 값이다.

보간이 왜 필수인지도 실측했다.

```text
== 4. 임계 통과 시각 오차: 샘플 시각 그대로 vs 선형 보간 (Ts=10us, ramp 400us, sigma=0.2%nom, 1000회)
sample_time_rms=5.73us  interp_rms=0.65us  (noise/slope = 0.80us)
```

**(3) 동시 레일과 동률.** 규칙이 "0V8과 1V8은 동시에 올라와도 된다"라면 두 레일의 순서를 말할 수 없다. `group_simultaneous` 는 tie_us 이내 인접 통과를 한 그룹으로 묶고, 그룹 안은 **이름순**으로 정렬해 측정 잡음이 보고서 순서를 흔들지 않게 한다. 주의: 단일 연결이라 A−B, B−C가 tie 안이면 A−C가 tie 밖이어도 A, B, C가 한 그룹이다 (`b3.chain_is_nontransitive`). "동시"는 원래 추이적이지 않다.

**(4) 샘플링의 한계 — 핵심.** 폭 W의 사각 dip, 깊이 D = 한계의 2배, 샘플 주기 Ts, 샘플 위상은 균일 분포라고 할 때 세 방식의 검출률(실측):

```text
== 1. 단발 사각 dip 검출률 (Ts=10us, depth=2xlimit, 위상 1000 격자)
W/Ts  point  boxcar  peak
0.10  0.100  0.000   1.000
0.25  0.250  0.000   1.000
0.50  0.500  0.500   1.000
0.75  0.750  0.750   1.000
1.00  1.000  1.000   1.000
1.50  1.000  1.000   1.000
2.00  1.000  1.000   1.000
```

```svg
<svg viewBox="0 0 760 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">폭 W &lt; Ts 인 dip: 샘플러마다 보는 것이 다르다</text>
  <line x1="60" y1="130" x2="720" y2="130" stroke="currentColor"/>
  <path d="M60 60 L300 60 L300 120 L350 120 L350 60 L720 60" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="310" y="145" fill="currentColor" font-size="12">W</text>
  <circle cx="240" cy="60" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="420" cy="60" r="5" class="f-acc" stroke="currentColor"/>
  <text x="240" y="50" text-anchor="middle" fill="currentColor" font-size="12">샘플</text>
  <text x="420" y="50" text-anchor="middle" fill="currentColor" font-size="12">샘플</text>
  <rect x="60" y="170" width="200" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="160" y="190" text-anchor="middle" fill="currentColor" font-weight="bold">point (순간 샘플)</text>
  <text x="160" y="210" text-anchor="middle" fill="currentColor" font-size="12">위상이 맞아야 잡음 → P = W/Ts</text>
  <rect x="280" y="170" width="200" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="380" y="190" text-anchor="middle" fill="currentColor" font-weight="bold">boxcar (Ts 평균)</text>
  <text x="380" y="210" text-anchor="middle" fill="currentColor" font-size="12">항상 스치지만 깊이가 W/Ts배로 작아짐</text>
  <rect x="500" y="170" width="220" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="610" y="190" text-anchor="middle" fill="currentColor" font-weight="bold">peak-detect (구간 min/max)</text>
  <text x="610" y="210" text-anchor="middle" fill="currentColor" font-size="12">폭과 무관하게 깊이 보존 (모델)</text>
</svg>
```

| 방식 | 검출 조건 | 실제 대응 |
|---|---|---|
| point | 샘플이 W 안에 들어와야 함 | 저속 SAR DAQ, 일반 샘플 모드 — **Ts보다 좁은 glitch는 위상 운에 맡겨진다** |
| boxcar | 깊이 × (겹침/Ts) ≥ 한계 | 오버샘플링 평균·ΣΔ ADC — 안 놓치지만 얕아짐 |
| peak-detect | 구간 안 최솟값을 기록 | 스코프 peak-detect/envelope 모드 (단, 실제로는 ADC 네이티브 샘플률이 하한) |

**결론**: 위 표는 샘플링 **모델**의 결과이고, `Ts` 는 "반드시 보이는 glitch"의 하한이다. PASS의 의미는 "관측한 샘플에서 위반 없음"이다. 그래서 `check_rails` 는 매번 `blind:` 줄로 사각지대를 내보내고, 스펙의 `glitch_must_see_us` 보다 Ts가 길면 glitch 항목 자체를 INDET로 낸다 (`c8.coverage_indet`). 또 `c8.sub_Ts_glitch_leaves_no_trace` 는 4 µs짜리 15 % 강하를 샘플 사이에 넣어 **로그가 단 한 비트도 바뀌지 않음**을 CHECK 한다. 같은 폭을 샘플 시각에 맞추면 바로 FAIL로 잡힌다 — 깊이의 문제가 아니라 위상의 문제다.

깊이의 과소평가도 있다(V자 dip, point 샘플):

```text
== 2. V자 dip 깊이 과소평가 (point 샘플, 깊이 D=100%, 위상 평균/최악)
W/Ts   mean_seen  worst_seen
  1.0   0.500      0.000
  2.0   0.750      0.500
  3.0   0.833      0.667
  4.0   0.875      0.750
  8.0   0.938      0.875
 16.0   0.969      0.938
```

꼭짓점이 샘플 사이에 끼면 깊이를 과소평가한다. 대역 한계가 깊이의 90 %라면 W/Ts ≥ 8 정도여야 "깊이"를 믿을 수 있다. 면접에서 "12 %로 측정됐는데 실제는요?"라고 물으면 바로 이 표로 답한다.

**(5) 잡음과 임계값.** 평탄부에 σ 잡음이 있고 대역 한계까지 m·σ이면 샘플당 위반 확률은 Q(m)이다. 실측(N=500 000):

```text
== 3. 흰잡음 위 false alarm (한계까지 m sigma, N=500000 샘플)
m    measured  N*Q(m)   min_samples=2
2.0     11174   11375.1       258
3.0       648     674.9         1
4.0        15      15.8         0
5.0         0       0.1         0
```

세 가지 처방과 각각의 대가:

- 대역을 m ≥ 5 정도로 떨어뜨리고, 잡음이 대역에 가까우면 `measure_rail` 이 `WARN noise` 를 낸다.
- `min_samples=2` 로 단일 샘플 이벤트를 버리면 **흰잡음의** false alarm은 Q²으로 줄지만, 그만큼 Ts~2Ts 폭의 진짜 glitch를 놓친다. 그리고 실제 레일의 스위칭 리플은 샘플 간 상관이 있어 Q²이 성립하지 않는다.
- 히스테리시스는 false alarm을 줄이지 않는다. 대신 한계에 붙어 있는 레일의 **채터링**(이벤트 수백 개)을 몇 개의 이벤트로 합친다 (`b5.hyst_merges_chatter`). "하나의 문제 = 한 줄 보고"가 이것의 값어치다.

임계 통과의 낮은 레벨도 같은 문제다. 레벨을 잡음 안(1.5σ)에 두면 hold=1은 ramp 500 µs 전에 가짜 통과를 내고, hold=5로 바꾸면 참값 근처로 돌아온다 (`b6`).

**(6) 클럭 가드밴드.** 측정 불확도 u = ±1 count 분해능 + 카운터 타임베이스 오차. `clock_verdict` 는 레일의 eval_rule과 같은 논리다. 짧은 게이트(1 ms)에서 XO(한계 30 ppm)의 u는 41.8 ppm이라 한계보다 크다: **실측 INDET** (`c7.short_gate_u_dominates`). 측정 시간 예산이 부족할 때는 분해능을 바꾸지 못한다 — 게이트를 늘리거나 reciprocal counter를 쓴다.

### Part C — 불량 보드 캠페인과 리포트

흐름:

```text
check_rails:
  check_timestamps ──INVALID──> 끝 (측정 다시)
  Ts > must-see? ──> coverage INDET
  for rail: 대역 진입 → 정착 창 → 중앙값(DC) → ramp → glitch
  on/off 규칙: Δ, u = u_a + u_b, eval_rule
  observed on-order: group_simultaneous (tie 2·Ts)
  부모 레일에 먼저 생긴 glitch가 있으면 자식 glitch = secondary
  status: INVALID > FAIL > INDET > PASS
```

실제 리포트 (`python3 -I solution.py --report` 에서 발췌, 합성 데이터):

```text
=== board_bad_order  STATUS=FAIL ===
capture: Ts=10.00us jitter=0.142us
blind: glitch narrower than Ts=10.0us can fall between samples
observed on-order: VIN_12V < VDD_0V8 < VDD_1V8 < VDD_3V3 < VDD_1V1
FAIL rail=VDD_3V3 check=order_on rule=VDD_1V1@90%->VDD_3V3@10% dt=-718.2us min=100.0us u=+/-14.1us
  next: check PMIC OTP/EN chain and PG wiring of the named rails

=== board_bad_brownout  STATUS=FAIL ===
capture: Ts=10.00us jitter=0.144us
blind: glitch narrower than Ts=10.0us can fall between samples
observed on-order: VIN_12V < VDD_0V8 < VDD_1V8 < VDD_1V1 < VDD_3V3
FAIL rail=VDD_1V8 check=glitch t=5994.2us dur=81.7us depth=12.3% kind=under samples=8 primary
  next: re-trigger scope on this rail with peak-detect; look for load steps at t
FAIL rail=VDD_1V1 check=glitch t=6016.2us dur=57.8us depth=8.3% kind=under samples=6 secondary_of=VDD_1V8
```

읽는 법:

- **순서 위반**: 참값 Δ = (2300 + 40) − (2700 + 360) = −720 µs, 측정 −718.2 µs (`c2.delta_truth`, 허용 ±20 µs = 2·Ts). 관측 순서 줄이 PMIC 시퀀스 차이를 직접 보여 준다.
- **Brownout primary/secondary**: 심어 넣은 시작 6000.0 µs, 측정 5994.2 µs. 사각 에지는 **샘플 간격 안 어디든** 있을 수 있어 보간이 정보를 만들어 내지 못하며, 따라서 t0 허용은 Ts=10 µs다 (`c4.t0_truth`). 폭 81.7 µs(참값 80), 깊이 12.3 %(참값 12, 허용 0.5 %p ≈ 잡음).
  전원 트리 부모(`VDD_1V8`)의 강하가 먼저이므로 자식 `VDD_1V1` 은 secondary로 묶인다. 이것이 리포트의 값어치다: 디버그 엔지니어는 줄 두 개가 아니라 원인 하나를 본다.
- **ramp**, **dc**, **power-down** 은 각각 FAIL 한 줄만 낸다 — 참값 대비 `1434.9us`(1440 µs), `+6.36 %`, `dt=-818.9us`(참값 −819.7 µs). `board_good` 은 findings 0.

클럭 (`clock_meas.json`, 게이트 1 s):

```text
=== clock bad_pll_ddr  STATUS=FAIL ===
  APB_CLK   exp= 100000000.000Hz ppm=      +8.00 u=  0.11 limit=  100 PASS
  AUD_MCLK  exp=  12287999.994Hz ppm=      +7.98 u=  0.18 limit=  100 PASS
  AXI_CLK   exp= 500000000.000Hz ppm=      +8.00 u=  0.10 limit=  100 PASS
  CPU_CLK   exp=1200000000.000Hz ppm=      +8.00 u=  0.10 limit=  100 PASS
  DDR_CK    exp=1056000000.000Hz ppm=  -11355.73 u=  0.10 limit=  100 FAIL
  PCIE_REF  exp= 100000000.000Hz ppm=      +8.00 u=  0.11 limit=  300 PASS
  XO_24M    exp=  24000000.000Hz ppm=      +8.00 u=  0.14 limit=   30 PASS
FAULT node=PLL_DDR ppm=-11363.6 ratio_hint=87/88 confounded_with=['PLL_DDR', 'DDR_CK']

=== clock bad_xo  STATUS=FAIL ===
  APB_CLK   exp= 100000000.000Hz ppm=    +120.00 u=  0.11 limit=  100 FAIL
  AUD_MCLK  exp=  12287999.994Hz ppm=    +120.04 u=  0.18 limit=  100 FAIL
  AXI_CLK   exp= 500000000.000Hz ppm=    +120.00 u=  0.10 limit=  100 FAIL
  CPU_CLK   exp=1200000000.000Hz ppm=    +120.00 u=  0.10 limit=  100 FAIL
  DDR_CK    exp=1056000000.000Hz ppm=    +120.00 u=  0.10 limit=  100 FAIL
  PCIE_REF  exp= 100000000.000Hz ppm=    +120.00 u=  0.11 limit=  300 PASS
  XO_24M    exp=  24000000.000Hz ppm=    +120.00 u=  0.14 limit=   30 FAIL
FAULT node=XO_24M ppm=+120.0 ratio_hint=None confounded_with=-
```

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a06a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">클럭 고장 위치: 측정 자손의 비율이 같은 곳까지 올라간다</text>
  <rect x="20" y="110" width="110" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="75" y="130" text-anchor="middle" fill="currentColor" font-size="12">XO_24M</text>
  <text x="75" y="148" text-anchor="middle" fill="currentColor" font-size="12">측정 +8 ppm</text>
  <rect x="200" y="50" width="120" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="260" y="70" text-anchor="middle" fill="currentColor" font-size="12">PLL_SYS</text>
  <text x="260" y="88" text-anchor="middle" fill="currentColor" font-size="12">비측정</text>
  <rect x="200" y="120" width="120" height="46" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="260" y="140" text-anchor="middle" fill="currentColor" font-size="12">PLL_DDR (N=87?)</text>
  <text x="260" y="158" text-anchor="middle" fill="currentColor" font-size="12">비측정 = 구별 불가</text>
  <rect x="200" y="190" width="120" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="260" y="210" text-anchor="middle" fill="currentColor" font-size="12">PLL_CPU</text>
  <text x="260" y="228" text-anchor="middle" fill="currentColor" font-size="12">비측정</text>
  <rect x="430" y="36" width="130" height="42" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="495" y="62" text-anchor="middle" fill="currentColor" font-size="12">AXI_CLK +8</text>
  <rect x="430" y="86" width="130" height="42" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="495" y="112" text-anchor="middle" fill="currentColor" font-size="12">PCIE_REF +8</text>
  <rect x="430" y="136" width="130" height="42" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="495" y="162" text-anchor="middle" fill="currentColor" font-size="12">DDR_CK −11356 ppm</text>
  <rect x="430" y="196" width="130" height="42" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="495" y="222" text-anchor="middle" fill="currentColor" font-size="12">CPU_CLK +8</text>
  <g stroke="currentColor" fill="none" marker-end="url(#a06a)">
    <path d="M130 133 L198 73"/><path d="M130 133 L198 143"/><path d="M130 133 L198 213"/>
    <path d="M320 73 L428 57"/><path d="M320 73 L428 107"/><path d="M320 143 L428 157"/><path d="M320 213 L428 217"/>
  </g>
  <text x="580" y="60" fill="currentColor" font-size="12">공통 +8 ppm =</text>
  <text x="580" y="78" fill="currentColor" font-size="12">XO에서 온 것, 무시</text>
  <text x="580" y="150" fill="currentColor" font-size="12">비율 87/88 = N 설정?</text>
  <text x="580" y="168" fill="currentColor" font-size="12">PLL vs 분주기 구별 불가</text>
</svg>
```

알고리즘은 위에서 아래로 내려가며 "측정 자손 과반의 비율 ≠ 1"인 가장 높은 노드를 고른다. 그래서 XO 공통모드 +120 ppm이 일곱 곳에서 FAIL이어도 **원인은 한 줄**이고, `bad_xo` 에서 PCIE_REF가 PASS인 것은 한계가 300 ppm이기 때문이다. `bad_pll_ddr` 에서 공통 +8 ppm을 나누어 내고 내려가므로 비율 힌트가 `87/88` 로 깨끗하게 나온다(XO 값이 섞이지 않음).

**이 방법으로 모르는 것**을 CHECK 와 출력으로 남겼다.

| 사각지대 | 증거 | 현실적 대응 |
|---|---|---|
| PLL과 분주기의 혼동 | `confounded_with=['PLL_DDR','DDR_CK']` — 측정점이 끝에만 있으면 구별 불가 | CLKOUT 멀티플렉서로 VCO/분주 전단을 내보내기 |
| XO 불량 vs 카운터 타임베이스 불량 | `bad_xo` 에서 둘은 같은 데이터로 보인다 | 다른 기준의 두 번째 측정, GPSDO/룸 레퍼런스 |
| 측정점 없는 노드 | `PLL_CPU` 등 비측정 노드는 자손으로만 추론 | 측정점 설계 |
| Ts 미만 glitch | `c8.sub_Ts_glitch_*`, 표 1 | 더 빠른 샘플, peak-detect, 트리거 기반 캡처 |
| 정착 창 밖의 이벤트 | ramp 중 glitch는 창 밖이므로 glitch로 안 잡힌다 | 램프 구간 별도 monotonic 검사 (연습 후보) |
| INDET 보드 | 캡처 부족은 FAIL 아님 (`c8.truncated_indet`) | 캡처 길이/트리거 수정 |

**장비가 있을 때** (이 맥에서는 실행하지 않음): 멀티채널 스코프 + 스프링 팁 프로브 또는 DAQ로 레일을 동시 캡처(프로브 접지 리드 링잉은 HWLAB-B05, 리플 측정 BW는 HWLAB-B08), 전원 sequencer/PMIC 쪽 시퀀스 설정과 대조, 스코프 single-shot 트리거로 전원 인가, CSV/바이너리 내보내기 → `check_rails`. 클럭은 주파수 카운터(GPIB/USB, PyVISA + SCPI)를 게이트 시간 지정으로 측정하고, 크리스털을 직접 찍지 말고 **버퍼된 CLKOUT** 을 측정한다(프로브 커패시턴스가 크리스털을 끌어당겨 ppm을 왜곡한다).

| 비용 항목 (대표값) | 대가 |
|---|---|
| 레일 수 × 샘플률 | 스코프 8채널 초과면 DAQ + 상관 트리거 필요, 동기 손실 → skew 항 |
| 전원 사이클 반복 | 사이클당 수 초, 간헐 glitch 잡으려면 수백 회 → 연습 1 공식으로 예산 |
| 게이트 시간 | 트리 7개 노드 × 1 s = 7 s + 멀티플렉서 settle |
| 온도 코너 반복 | SIV-M08 스케줄과 곱해짐 |

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit=$?"
python3 -I -W error -X dev solution.py >/dev/null; echo "exit=$?"
python3 -I solution.py --report     # 보드별 리포트 출력
python3 -I gen_rail_logs.py         # 데이터 재생성 (sha 동일)
```

실제 출력 (뒷부분):

```text
PASS c8.report_renders
PASS c8.common_mode_reported_at_root
158/158 passed
exit=0
```

`gen_rail_logs.py` 재실행 후 `shasum` 비교: 전부 IDENTICAL. `-W error -X dev` 실행도 exit 0.

## 7. 자주 하는 실수

**1. 첫 하강 통과를 power-down으로 본다.**
정상 구간의 brownout이 power-down으로 오인되어 정상 전원 차단 규칙을 틀리게 판정한다. → 끝까지 아래에 머문 마지막 통과(`last_crossing`)를 쓴다.

**2. DC와 glitch를 같은 검사로 합친다.**
```python
# 틀림: 평균이나 샘플별 band 판정 하나로 끝낸다
ok = all(lo <= x <= hi for x in window)   # 80us 한 번 빠졌는데 '전압이 나쁨'으로 보고됨
# 고침: 중앙값으로 DC, 이탈은 glitch 이벤트로
ok, err = dc_check(statistics.median(window), nom, tol)
events = detect_glitches(wt, wv, lo, hi, hyst=0.01 * nom)
```

**3. 폭을 재무장 선 기준으로 잰다.** 재무장 선까지의 시간은 "복귀에 걸린 시간"이다. 스펙이 말하는 "한계 밖 시간"은 양 끝의 **한계선** 통과로 계산한다(`a3.hyst_merges` 의 폭 계산 참고).

**4. Ts보다 좁은 glitch에 대해 PASS를 무조건 믿는다.** 샘플 위상이 안 맞으면 로그에 흔적조차 없다. 리포트에 사각지대를 쓰고, 스펙의 must-see보다 Ts가 길면 INDET로 낸다.

**5. 타임스탬프를 그대로 믿는다.** 뒤로 가는 시간이 있으면 임계 통과 시각이 뒤죽박죽이 되어 Δ가 터무니없는 값이 되고, 그걸 "순서 위반"으로 보고한다. 먼저 `check_timestamps`, 실패면 **INVALID**.

**6. 게이트 시간 없이 ppm만 보고한다.** 12.288 MHz를 1 ms 게이트로 측정하면 ±81 ppm 분해능이다. "ppm 편차 0.0"은 양자화일 뿐이다. 불확도를 같이 보고하고 3값 판정을 쓴다. 실제로 `a5.counter_uses_quantization` 은 f·T가 정수이면 양자화 오차가 전혀 안 드러난다는 것(그래서 테스트 주파수를 비정수로 잡음)까지 포함한다.

**7. float로 PLL 트리를 곱한다.** 비교식의 부동소수 오차는 작지만 frac-N에서 "정확한 기대값"과 "공칭"을 구별하기 어렵다. `Fraction` 을 쓰고 표시할 때만 float.

**8. 낮은 임계값에 hold 없이 첫 통과를 쓴다.** 잡음 안(1.5σ)에 임계값을 두면 ramp 500 µs 전에 통과로 나온다(`b6.naive_early`). 레벨은 잡음 밖에 두고 hold로 확인한다.

**9. 평탄부 σ를 전체 창의 표준편차로 추정한다.** glitch 하나로 σ가 크게 부풀어 hyst·WARN 기준이 망가진다. 중앙값·MAD(1.4826×MAD)를 쓴다(`mad_sigma`). 단, 1 mV 단위로 반올림된 로그에서는 MAD도 양자화되어 뭉개진다 — 실측 `VDD_1V1 sigma` 가 1.5/3.0 mV로 튀는 이유다.

## 8. 면접 Q&A

<details><summary>Q1. 전원 시퀀스를 왜 맞춰야 하나요? 어기면 무슨 일이 나죠?</summary>

코어가 내려간 채 IO 레일이 올라오면 IO 패드의 ESD 다이오드를 통해 다른 레일로 전류가 흘러(back-powering, HWLAB-B02), 의도치 않은 도메인이 일부만 살아나거나 래치업 위험이 생긴다. 메모리 계열 레일은 표준에서 레일 간 관계를 규정하고, PLL/리셋 시퀀스가 전원 정착 전에 시작되면 잘못된 상태로 부팅한다. 그래서 순서는 데이터시트의 시퀀스 표 + PMIC OTP 설정 + PG 신호 체인으로 구현되어 있고, 보드 불량(EN 저항, OTP 오류, 부품 오장착)이 여기서 먼저 드러난다.

- **꼬리질문: 순서 위반인데 보드가 부팅돼요. 괜찮은 것 아닌가요?** → 그렇지 않다. 안 고장 난 것과 스펙 안이라는 것은 다르다. 신뢰성 수명(EOS/latch-up 누적), 다른 코너(저온, 다른 보드)에서 실패 가능. 판정은 스펙 기준으로 내리고, 위반을 허용한다면 부품 벤더의 면제(waiver)를 받는다.
- **꼬리질문: PG 신호만 보면 안 되나요?** → PG 임계값은 대개 공칭의 90 % 부근이고 필터링되어 있어, 짧은 glitch를 마스크할 수 있고 "ramp 시간"이나 "전압 절댓값"을 말해 주지 않는다. 또 PG가 정상이어도 레일 자체는 틀릴 수 있다(FB 저항 오류) — 아날로그 측정이 기준이다.
</details>

<details><summary>Q2. 샘플 주기보다 짧은 glitch는 어떻게 검증하죠? 체커가 PASS면 안전한가요?</summary>

아니다. point 샘플에서 폭 W &lt; Ts인 단발 dip의 검출 확률은 W/Ts이고, 위상이 안 맞으면 로그에 **아무 흔적이 없다**. 그래서 PASS를 "관측 샘플에서 위반 없음"으로 다시 정의하고, 리포트에 사각지대를 쓴다. 검증 방법은 (1) 더 빠른 샘플링 + 트리거(단발, normal 모드, 깊이 임계 트리거) (2) 스코프 peak-detect 모드 — ADC가 네이티브 속도로 min/max 보존 (3) 비교기 + 래치(glitch catcher) (4) 스펙의 "반드시 보여야 하는 최소 폭" 정의.

- **꼬리질문: 평균 내는 ADC면 안 놓치는 것 아닌가요?** → 안 놓치는 대신 **얕아진다**. 깊이가 2배 한계여도 폭이 Ts/2보다 짧으면 한계를 못 넘는다(표: W/Ts=0.25에서 boxcar 0.000). 안 놓친 것과 "넘었다고 판정되는 것"은 다르다.
- **꼬리질문: 표의 검출률 수치를 그대로 믿나요?** → 사각 펄스 + 균일 위상 모델 기준이다. 실제 ADC는 유한 acquisition aperture, 프론트엔드 안티앨리어싱 필터, 입력 대역폭이 있고 glitch는 사각이 아니다. 표의 가치는 숫자보다 **단조 관계와 한계의 형태**에 있다.
</details>

<details><summary>Q3. 왜 3값(혹은 4값) 판정을 쓰나요? 그냥 FAIL 내면 안 되나요?</summary>

"보드 불량", "측정 불량", "판단 불가"의 **후속 작업이 다르기 때문이다**. FAIL은 보드/스키매틱을 고치고, INVALID는 캡처를 다시 하고, INDET는 측정 설정(게이트, 샘플률, 트리거)을 바꾼다. 이걸 합치면 FAIL 하나당 몇 시간의 낭비가 생기고 며칠 뒤 "측정 문제였음"으로 끝나 팀이 체커를 안 믿게 된다.

- **꼬리질문: INDET이 많이 나오면 어떻게 관리하나요?** → INDET 비율을 지표로 올린다. 프로브 수·채널 수·샘플률 예산의 투자 근거가 된다.
- **꼬리질문: 불확도 u는 어떻게 믿나요?** → 참값이 있는 합성 입력으로 커버리지를 측정해 보여 준다(본문: 300/300, u=7.40 µs, 최악 2.19 µs). u는 의도적으로 보수적이며, 그 대가는 INDET 증가다.
</details>

<details><summary>Q4. ppm 에러 측정값을 어떻게 믿죠? 게이트 시간은 어떻게 정하나요?</summary>

세 가지가 쌓인다. (1) ±1 count 양자화: `1e6/(f·T)` ppm. (2) 카운터 타임베이스 오차(캘리브레이션된 OCXO/GPSDO면 ≪ 0.1 ppm 수준을 대표값으로 봄, 이번 레슨은 0.1 ppm 가정). (3) 프로브/측정점 영향 — 크리스털 핀 직접 측정은 부하 커패시터를 변경해 주파수를 끌어당긴다, 버퍼된 CLKOUT을 측정한다. 게이트는 필요한 분해능에서 역산한다: `required_gate_s = 1e6/(f·ppm)`, 12.288 MHz / 0.1 ppm → 0.81 s. 낮은 주파수가 가장 긴 게이트를 요구한다.

- **꼬리질문: 지터 측정과 뭐가 다른가요?** → ppm은 긴 평균의 **중심 주파수 오차**다. 지터는 에지 단위의 시간 흔들림이고, 긴 게이트는 지터를 평균으로 지운다. 지터는 phase noise 측정/TIE로 따로 본다(스코프, 페이즈 노이즈 분석기).
- **꼬리질문: PCIe 레퍼런스 클럭에 SSC가 켜 있으면?** → 다운스프레드(최대 약 −0.5 %, 30~33 kHz 변조)로 평균 중심이 대략 −0.25 %(≈ −2500 ppm) 내려간다. 게이트가 변조 주기의 정수 배가 아니면 부분 주기 잔차가 남는다(연습 5). ±300 ppm은 SSC 없는 Refclk의 허용치 기준이며, SSC가 켜진 상태라면 별도 규칙을 적용한다.
</details>

<details><summary>Q5. 클럭 하나만 틀렸을 때 PLL 문제인지 분주기 문제인지 어떻게 구별하나요?</summary>

측정점이 분주기 뒤에만 있으면 **구별할 수 없다**. 비율은 같기 때문이다. 이번 체커도 `confounded_with=['PLL_DDR','DDR_CK']` 로 그 사실을 보고하고 비율 힌트 `87/88` 만 낸다(N = 88을 87로 잘못 프로그래밍했다는 가설). 구별하려면 CLKOUT MUX로 PLL 출력 또는 VCO를 직접 내보내거나, 같은 PLL의 다른 분주 출력을 측정한다. 다른 출력들이 같은 비율이면 PLL, 하나만 다르면 그 분주기다. 레지스터 readback과 대조도 필수다.

- **꼬리질문: 모든 노드가 +120 ppm이면요?** → 공통모드: XO 또는 측정 기준(타임베이스) 문제다. 숫자만으로는 구별 불가, 두 번째 기준으로 재측정한다. 또 크리스털 부하 커패시터 오류(보드), XO 부품 오장착(다른 ppm 등급)을 의심한다.
- **꼬리질문: 비율 힌트가 틀릴 수 있나요?** → `limit_denominator(256)` 은 후보일 뿐이다. 레지스터 값을 읽어 확정한다. 측정 ppm이 커서 어떤 유리수와도 안 맞으면 힌트는 None — 억지 답을 안 내는 것이 중요하다.
</details>

<details><summary>Q6. 다채널 측정에서 레일 간 시각 차이는 어떻게 믿나요?</summary>

세 가지 오차원이 있다. 다중화 ADC(mux)는 채널을 순서대로 샘플해 채널 간 고정 skew가 생긴다 — 알면 보정(연습 2), 모르면 u에 skew 항으로 넣는다. 스코프 채널은 동시 샘플이지만 프로브 지연 차이(전기 길이)가 있다. 여러 장비를 묶으면 트리거/타임베이스 동기가 필요하다. 그래서 "동시" 판정에 `tie_us` 가 있고, 같은 그룹 내 순서는 **이름순**으로 고정해 측정 잡음이 보고서를 흔들지 않게 했다.

- **꼬리질문: skew를 측정으로 어떻게 구합니까?** → 같은 신호를 모든 채널에 동시에 넣어 교차 측정(calibration)한다. 채널 위치 고정 패턴이면 시스템 오차로 빠진다.
</details>

<details><summary>Q7. 이 체커가 제대로 동작하는지 어떻게 아나요?</summary>

(1) 합성 로그에 **참값이 있는 결함을 심어** 위치·규칙 이름·값을 CHECK 한다. 이번에 7개 로그 + 5개 클럭 보드, 158개 CHECK. (2) good 로그가 **조용해야 한다** — false alarm 0. (3) 측정 체인의 한계를 표로 CHECK 한다(`c8.*`). 그리고 실제 랩에서는 (4) golden board 대조 캡처로 체커 임계값이 현실 범위인지 확인 (5) 의도적 fault injection — EN 신호를 지연, 트리머로 레일 전압 약간 변경 — 으로 실제 측정 사슬에서 검출되는지 시연한다.

- **꼬리질문: 합성 로그로 통과했는데 실물에서 오탐이 난다면요?** → 가장 흔한 원인은 레일의 실제 파형이 모델과 다른 것(부하 스텝, 레귤레이터 ringing, 다른 sampling skew)이다. 첫 실물 캡처 2~3개로 모델을 고치고, 합성 로그의 모양을 실측에 맞춰 회귀를 만든다. 즉 합성은 코드 버그를 잡고, 임계값의 타당성은 실측이 준다.
</details>

<details><summary>Q8. 이 검증은 랩 일정에서 얼마나 걸리나요? 무엇을 먼저 자르나요?</summary>

(대표값 가정) 보드당 첫 전원 인가 + 시퀀스 캡처는 몇 분이지만, 첫 보드는 프로브 배치에 반나절이 든다. 자동화 대상: 시퀀스 캡처 + 체커 + 리포트, 카운터 스윕. 비용은 채널 수·전원 사이클 수·온도 코너의 곱이다. 일정이 밀리면 (1) 전 레일 동시 캡처를 유지(순서는 포기 불가) (2) 부하 스텝 glitch 탐사의 반복 횟수를 줄임 (3) 코너 수를 줄임 순으로 자른다 — 그리고 줄인 커버리지를 리포트의 `blind:` 처럼 **문서에 남긴다**. 전체 캠페인 맥락은 SIV-A09.
</details>

## 9. 추가 연습

### 연습 1 — 샘플 주기보다 좁은 glitch를 잡으려면 전원 사이클 몇 번이 필요한가

`repeats_to_catch(width, Ts, conf)`: 반복마다 위상이 독립일 때, 폭 W &lt; Ts인 glitch를 신뢰도 conf 이상 한 번은 샘플에 넣는 최소 반복 횟수.

<details><summary>답</summary>

한 번에 잡을 확률 p = W/Ts이므로 N번 안에 잡을 확률은 1 − (1−p)^N ≥ conf → N ≥ ln(1−conf)/ln(1−p). W=1 µs, Ts=10 µs, 95 % → 29회. W ≥ Ts이면 1회. 이 계산이 랩의 전원 사이클 예산이다 — 현실의 위상은 독립이 아닐 수 있으므로(전원 시퀀스가 ADC 클럭과 위상 고정인 경우 전혀 안 움직임) 반복 간 지연을 의도적으로 흔든다.
</details>

### 연습 2 — 다중화 ADC 채널 skew 보정

스캔 타임스탬프 t에서 채널 k는 `t + k*t_conv`에 샘플된다. 같은 파형 두 개를 채널 0과 3에 넣었을 때 스캔 기준 통과 시각과 보정 후 시각을 비교한다.

<details><summary>답</summary>

늦게 샘플된 채널은 같은 파형의 뒤쪽 값을 보므로 스캔 시각으로는 **먼저 통과한 것처럼** 나온다(실측 `p2.skew_misleads`). 채널 시각을 `t + k*t_conv` 로 고치면 차이가 사라진다(`p2.deskew_fixes`). 교훈: 시각 불확도는 항상 채널별 샘플 시각까지 내려가서 정의한다.
</details>

### 연습 3 — RESET_N 해제 시점 검사

모든 레일이 대역에 들어온 뒤 최소 지연 후에 RESET_N이 올라와야 한다(대표값).

<details><summary>답</summary>

`first_crossing` 으로 RESET_N의 VIH 통과 시각을 얻고, `t_rails_ok` 의 최댓값과 차이를 본다. 음수 dt(리셋이 전원보다 먼저 풀림)이면 FAIL. 실제로는 RESET_N 외에 strap 샘플 시점(PG → boot mode latch)도 같은 패턴으로 검사한다.
</details>

### 연습 4 — 시퀀스 규칙 lint

`lint_rules(rules, rails)`: 규칙이 순환이면 ValueError, 없는 레일을 쓰면 ValueError, 아니면 결정적 위상 정렬 순서.

<details><summary>답</summary>

Kahn 알고리즘 + 동률은 이름순. 체커의 입력(스펙)도 검증 대상이다 — 순환 규칙은 "만족 불가능"이고, 체커가 PASS를 낼 길이 없다. 스펙 lint는 측정 전에 돈다.
</details>

### 연습 5 — SSC 다운스프레드와 게이트 시간

`ssc_gate_error_ppm(S, mod_hz, gate_s)`: 삼각 다운스프레드(0 → −S → 0)를 게이트 동안 평균했을 때 −S/2 대비 오차.

<details><summary>답</summary>

게이트가 변조 주기의 정수 배면 평균은 −S/2 (S=5000 ppm이면 −2500 ppm). 부분 주기면 잔차가 남고, N 주기가 많아질수록 대략 S/(4N) 이하로 줄어든다(`p5.partial_period_*`). SSC가 켜 있을 때 "PPM"을 재려면 게이트를 변조 주기(약 31 µs)보다 충분히 길게 잡거나 정수 주기로 맞춘다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit=$?"
```

실제 출력 (뒷부분):

```text
PASS p5.partial_period_N=1000.25
19/19 passed
exit=0
```

측정 한계 표는 데모로도 재현할 수 있다.

<!-- code: demo_limits.py -->

```bash
python3 -I demo_limits.py
```

## 10. 핵심 정리

- 스펙 문장 하나 = 측정 연산 하나: `first_crossing` (시퀀스), `last_crossing` (power-down), 중앙값 (DC), `detect_glitches` (이벤트).
- 판정은 네 값이다: PASS / FAIL / INDET(이 측정으론 모름) / INVALID(측정이 깨짐). 후속 작업이 다르다.
- 측정값에는 불확도가 따라온다: `u = Ts/2 + skew + 3·hypot(σt, σv/slope)` — 기울기가 작을수록 나쁘다.
- 샘플 주기보다 좁은 glitch는 point 샘플의 위상 운에 달려 있다: 검출률 = W/Ts. PASS는 "관측에서 위반 없음". 리포트에 사각지대를 쓴다.
- false alarm은 N·Q(m)로 예측된다. 히스테리시스는 false alarm이 아니라 채터링을 줄인다.
- 클럭은 트리의 정확한 기대값(유리수) + `ppm` + 게이트 분해능(±1 count) + 카운터 타임베이스로 가드밴드 판정한다. `ppm = 1e6/(f·T)` 를 외운다.
- 고장 위치 = 측정 자손의 비율이 같은 가장 높은 노드. 측정점이 부족하면 "구별 불가"라고 쓴다.
- 전원과 클럭 판정은 부팅 이전의 첫 관문이고, 그 뒤 SIV-M04(PCIe), SIV-M05(DDR), SIV-M12(시퀀서)로 이어진다.

## 참고자료

- IEEE Std 181 — Transitions, Pulses, and Related Waveforms (상태 레벨, 10–90 % 정의, 이 레슨은 HWLAB-B04 내용을 축약 적용)
- PCI Express Base Specification (PCI-SIG) — Refclk ±300 ppm, SSC 30–33 kHz / 다운스프레드 최대 약 −0.5 %
- JEDEC JESD209-4 (LPDDR4) 등 — 전원 인가/차단 절차는 표준 문서의 타이밍 표를 직접 확인할 것 (본 레슨은 레일 값을 대표값으로만 사용)
- HP/Agilent(Keysight) Application Note 200, *Fundamentals of Electronic Counters* — gate time, ±1 count, reciprocal counting
- PyVISA documentation — https://pyvisa.readthedocs.io (SCPI로 스코프/카운터 자동화)
- SoC 데이터시트의 Power Sequencing 표, PMIC 데이터시트 (시퀀서/OTP, soft-start, PG 임계값)
- 관련 레슨: HWLAB-B04 (IEEE 181), HWLAB-B05 (프로브), HWLAB-B08 (리플), HWLAB-B02 (back-powering), SIV-M07 (shmoo), SIV-M08 (PVT), SIV-M12 (시퀀서), SIV-A03 (전력), SIV-A09 (캠페인), SIV-A11 (전원 도메인)
