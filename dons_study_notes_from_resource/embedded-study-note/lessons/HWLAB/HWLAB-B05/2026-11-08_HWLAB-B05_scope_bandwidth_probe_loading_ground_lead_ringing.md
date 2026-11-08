---
id: HWLAB-B05
time: 70분
updated: 2026-11-08
---
# HWLAB-B05 스코프 대역폭·프로브 부하·그라운드 리드 링잉

## 학습 목표

- rise-time 예산 `tr_meas ≈ √(tr_sig² + tr_scope² + tr_probe²)` 와 `BW ≈ 0.35/tr` 로 "1ns 에지를 3% 오차로 보려면 몇 GHz가 필요한가"를 계산하고, 그 근사가 **정확히 맞는 조건(가우시안 응답)과 틀어지는 조건(단일 극점, 평탄 응답 스코프)** 을 시뮬레이션으로 확인한다.
- 10x 패시브 프로브(10MΩ‖12pF) + 그라운드 리드를 직렬 RLC로 모델링해 RK4로 적분하고, 링잉 주파수 `1/(2π√LC)`, 감쇠비 ζ, overshoot를 **캡처에서 직접 측정**해 이론값과 맞춘다.
- 15cm 악어 클립(~150nH)과 그라운드 스프링(~10nH)의 차이를 숫자로 말하고, 프로브 tip 용량이 DUT 에지를 늦추는 **부하 효과(loading)** 를 분리한다.
- "이 overshoot는 진짜인가, 내 프로브가 만든 것인가"를 캡처 두 장(리드/스프링)으로 판정하는 리포트를 만든다 — 링잉 주파수에서 루프 인덕턴스를 역산하는 것이 핵심 단서다.

## 1. 문제

첫 보드 브링업에서 "SPI 클럭 overshoot 60%, 수신기 abs max 위반" 이라는 버그 리포트의 절반은 **측정 체인이 만든 가짜**다 — Rivian/NVIDIA/Apple 보드·실리콘 검증 면접에서 "스코프로 무엇을 어떻게 봤고, 그 수치를 왜 믿는가"를 물을 때 가장 먼저 확인하는 것이 이 레슨의 내용이다.

스코프 화면의 파형은 DUT 신호 그 자체가 아니라 **DUT → 프로브 tip → 그라운드 리드 → 프로브/케이블 → 스코프 프론트엔드** 를 통과한 결과다. 각 단계가 대역폭을 깎고(rise time을 늘리고), 프로브 입력 용량은 DUT 노드에 짐을 얹고(loading), 그라운드 리드의 인덕턴스는 그 용량과 직렬 공진기를 만들어 원래 없던 링잉을 그린다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">측정 체인 모델 (Part B · gen_captures.py 공통)</text>
  <rect x="20" y="60" width="210" height="190" rx="8" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="30" y="80" fill="currentColor" font-size="12">DUT (Thevenin)</text>
  <circle cx="70" cy="155" r="22" fill="none" stroke="currentColor"/>
  <text x="70" y="160" fill="currentColor" text-anchor="middle" font-size="12">Vth(t)</text>
  <rect x="115" y="110" width="50" height="18" class="f-acc" stroke="currentColor"/>
  <text x="140" y="104" fill="currentColor" text-anchor="middle" font-size="12">Rs 33Ω</text>
  <g stroke="currentColor" fill="none">
    <path d="M70 133 L70 119 L115 119"/>
    <path d="M165 119 L250 119"/>
    <path d="M70 177 L70 225 L250 225"/>
  </g>
  <circle cx="200" cy="119" r="4" fill="currentColor"/>
  <text x="140" y="148" fill="currentColor" font-size="12">노드(DUT pin)</text>
  <rect x="250" y="60" width="300" height="190" rx="8" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="260" y="80" fill="currentColor" font-size="12">10x 패시브 프로브 입력 (tip 기준)</text>
  <g stroke="currentColor" fill="none">
    <path d="M250 119 L420 119 L420 140"/>
    <path d="M400 140 L440 140 M400 150 L440 150"/>
    <path d="M420 150 L420 190"/>
    <path d="M420 119 L500 119 L500 135"/>
    <rect x="492" y="135" width="16" height="40"/>
    <path d="M500 175 L500 190 L420 190"/>
    <path d="M420 190 L420 225 L400 225"/>
    <path d="M400 225 C390 210 380 240 370 225 C360 210 350 240 340 225 C330 210 320 240 310 225 C300 210 290 240 280 225 L250 225"/>
  </g>
  <text x="448" y="150" fill="currentColor" font-size="12">C 12pF</text>
  <text x="515" y="160" fill="currentColor" font-size="12">R 10MΩ</text>
  <text x="280" y="244" fill="currentColor" font-size="12">그라운드 리드 L: 150nH(15cm) / 10nH(스프링)</text>
  <rect x="590" y="100" width="150" height="70" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="665" y="125" fill="currentColor" text-anchor="middle">스코프+프로브</text>
  <text x="665" y="145" fill="currentColor" text-anchor="middle" font-size="12">1GHz 단일 극점(가정)</text>
  <text x="665" y="162" fill="currentColor" text-anchor="middle" font-size="12">+ 잡음 6mV rms</text>
  <path d="M550 160 L590 140" stroke="currentColor" fill="none"/>
  <text x="555" y="195" fill="currentColor" font-size="12">v_C 를 본다</text>
  <text x="10" y="285" fill="currentColor" font-size="12">Rs·L·C 는 직렬 RLC: f0 = 1/(2π√LC), Z0 = √(L/C), ζ = Rs/(2·Z0). 10MΩ 은 ns 영역에서는 사실상 개방이다.</text>
</svg>
```

이 레슨의 측정 도구(히스토그램 mode 상태 레벨, 10–90% 선형 보간 교차, overshoot %)는 **HWLAB-B04 가 정식으로 다루는 IEEE 181 방식의 축약판**이다. B04와 병렬로 쓰였기 때문에 B04 파일을 import 하지 않고 `starter.py` 맨 위 "B04-lite" 블록에 완성된 형태로 넣어 두었다 (수정할 필요 없음, 원리는 HWLAB-B04 참고).

### Part A — rise-time 예산과 그 근사의 가정

- `rss`, `rise_error(tr_sig, [bw...])`, `required_system_bw(tr_sig, err)`, `required_scope_bw(tr_sig, err, probe_bw)` 를 구현한다. 1ns 에지 @3% → **1.418GHz**, 2.5GHz 액티브 프로브와 쓸 때 스코프 **1.722GHz**, 500MHz 패시브 프로브로는 **불가능(None)** 이 나와야 한다.
- `single_pole_filter` (1차 RC의 정확한 이산화)를 구현하고, 같은 1.418GHz라도 스코프 응답이 가우시안이면 측정 오차가 정확히 3.00%, 단일 극점이면 3.87% 가 되는 것을 B04-lite로 **측정**한다. 이분법으로 "단일 극점이면 몇 GHz가 필요한가"를 찾는다.

### Part B — 프로브 부하와 그라운드 리드 RLC

- `series_rlc(R, L, C)` → f0, Z0, ζ, 감쇠 주파수 fd, 이상적 계단 overshoot.
- `simulate_probe(src, Rs, L, C, Rp, scope_bw)` — 상태 (i_L, v_C, y_scope) 를 RK4로 적분.
- `ring_analysis(t, v, top, i_start, hyst)` — 히스테리시스 극값 탐색 → 0 교차 간격으로 링잉 주파수, 로그 감쇠율로 ζ.
- 확인할 것: 150nH 리드 overshoot ≈ 59.6% vs 스프링 ≈ 5.1%, 측정 f_ring 이 fd 와 1% 이내, 역산한 L ≈ 150nH, tip C 1/12/100pF 에 따른 rise time 증가, 그리고 리드가 **DUT 노드 자체도 흔든다**는 것.

### Part C — BRD-A rev0: 아티팩트인가 실제인가

`captures/` 에 가상 보드 BRD-A rev0의 세 넷을 15cm 리드와 스프링으로 각각 잰 CSV 6개가 있다 (`gen_captures.py` 가 결정적으로 생성). 수신기 데이터시트 abs max를 VDDIO+0.3V = **3.6V** 로 가정한다 (HWLAB-B02 참고).
`diagnose(net)` 는 두 캡처를 비교해 `ARTIFACT / REAL / PASS` 와 엔지니어가 그대로 티켓에 붙일 수 있는 한 줄 리포트를 낸다. 심어 둔 결함: **QSPI_CLK 는 직렬 종단 저항이 DNP라 보드 자체가 250MHz로 링잉한다** — 스프링으로 재도 overshoot가 남아야 하고, 리포트가 그것을 "보드 SI 문제"로 분류해야 한다.

## 2. Problem (English)

> **Is it the signal or the probe?**
>
> Using only the Python 3 standard library:
>
> 1. *Rise-time budget.* `rise_error(tr_sig, bws, k=0.35) -> float` returns the relative rise-time error when a signal with
>    10–90% rise time `tr_sig` is viewed through cascaded stages of bandwidth `bws` (`tr_i = k/bw_i`, root-sum-square).
>    `required_system_bw(tr_sig, err)` and `required_scope_bw(tr_sig, err, probe_bw)` (return `None` if the probe alone
>    exceeds the budget). Example: `required_system_bw(1e-9, 0.03) ≈ 1.418e9`.
>    Then verify by simulation that the RSS rule is exact for Gaussian responses and *not* for a single-pole scope.
> 2. *Probe + ground lead.* Model a Thevenin source (`Rs`) driving a series ground-lead inductance `L` into the probe input
>    `C ‖ Rp`, followed by a single-pole scope. Implement `series_rlc(R, L, C)` (f0, Z0, ζ, fd, ideal-step overshoot),
>    `simulate_probe(...)` (RK4) and `ring_analysis(t, v, top, i_start, hyst)` returning the ringing frequency (from
>    zero-crossings) and ζ (from logarithmic decrement). For `Rs=33Ω, C=12pF`, a 1 ns edge shows ~60% overshoot with a
>    150 nH lead and ~5% with a 10 nH spring; the measured ringing must match `fd = f0·√(1−ζ²)` within 1%.
> 3. *Triage.* For each net, two captures (CSV `t_ns,volt`) exist: 15 cm lead and ground spring. Implement
>    `diagnose(net, vmax=3.6, c_probe=12e-12) -> (verdict, report_line, info)` where verdict is `REAL` if the spring capture
>    still exceeds `vmax`, `ARTIFACT` if only the lead capture does, else `PASS`. Back-compute the loop inductance from the
>    lead capture's ringing frequency and the probe capacitance and print it in the report.

## 3. 시작 코드

<!-- code: starter.py -->

캡처 생성기 (결정적: 같은 시드 → 바이트 단위로 같은 CSV). 캡처는 넷당 1601샘플, 파일당 ~22KB.

<!-- code: gen_captures.py -->

공진 peaking 데모 (§5 Part B에서 사용, 테스트 아님): `python3 -I demo_peaking.py`

<!-- code: demo_peaking.py -->

```bash
cd lessons/HWLAB/HWLAB-B05
python3 -I starter.py ; echo "exit=$?"     # TODO 상태: 3/22 passed, exit=1
```

실제 출력 (일부):

```text
-- B04-lite tool sanity (given) --
PASS edge_measure on an ideal 1ns Gaussian edge  [ideal 1ns edge -> tr 1.0000 ns, top 3.3000 V]
PASS 6 captures load (1601 samples each)  [samples per capture [1601, 1601, 1601, 1601, 1601, 1601]]
-- Part A: rise-time budget --
FAIL rss(3,4) == 5: 0.000000
FAIL required_system_bw(1ns, 3%) ~ 1.418 GHz: TypeError: unsupported operand type(s) for /: 'NoneType' and 'float'
...
FAIL probe C loading slows the edge: spring, tip C 1/12/100 pF -> tr 1.00 / 1.00 / 1.00 ns (2.2*Rs*C = 0.07 / 0.87 / 7.26 ns)
...
    TODO     SPI0_SCLK | lead15cm peak=5.27V OS=59.8% ring=none | spring peak=3.48V OS=5.3% tr=1.12ns ring=none | limit 3.60V -> TODO
...
PASS SPI0_SCLK spring tr shows probe loading  [spring tr 1.118 ns (true 1.000 ns; 12pF loading + 1GHz)]
3/22 passed
exit=1
```

> 눈여겨볼 것: TODO 상태에서도 캡처의 peak(5.27V)와 overshoot(59.8%)는 이미 나온다 — 측정 도구는 완성돼 있으니까. **숫자를 읽는 것과 그 숫자를 믿을지 판정하는 것은 다른 일**이고, 이 레슨의 TODO는 전부 후자에 있다.

## 4. 힌트

<details>
<summary>힌트 1 — 예산을 거꾸로 풀기</summary>

`(1+err)² = 1 + (tr_sys/tr_sig)²` → `tr_sys = tr_sig·√((1+err)²−1)`. err=3% 이면 √0.0609 = 0.2468, 즉 시스템 rise time은 신호의 약 1/4 이어야 한다. 프로브가 있으면 `tr_scope = √(tr_sys² − tr_probe²)`, 근호 안이 음수면 어떤 스코프도 못 맞춘다.

</details>

<details>
<summary>힌트 2 — 단일 극점의 정확한 이산화</summary>

`τ dy/dt = x − y` 를 한 샘플 동안 x가 상수라고 보고 풀면 `y[n] = y[n−1] + (1 − e^(−dt/τ))(x[n] − y[n−1])`, `τ = 1/(2π·BW)`. 오일러(`dt/τ`)보다 dt가 클 때도 안정하다.

</details>

<details>
<summary>힌트 3 — RLC 상태방정식</summary>

KVL: `L di/dt = src(t) − Rs·i − v`, 커패시터: `C dv/dt = i − v/Rp`, 스코프: `τ dy/dt = v − y`. 상태 3개를 튜플로 들고 RK4 네 번 평가. dt=5ps면 459MHz(주기 2.18ns)도 주기당 435스텝이라 충분하다. `every` 스텝마다 저장하면 20GS/s 캡처처럼 보인다.

</details>

<details>
<summary>힌트 4 — 잡음 속에서 링잉 주파수 재기</summary>

극값의 *시각* 은 기울기가 0인 곳이라 잡음에 약하다 (6mV 잡음이 꼭대기 위치를 수십 ps 흔든다). 극값은 "어디서 반주기가 바뀌는가"를 찾는 데만 쓰고, 주파수는 극값 사이의 **0 교차**(기울기 최대 → 시간 오차 = σ_v / slope)를 선형 보간해 잰다. 극값 확정은 히스테리시스: 최대 탐색 중 `d < max − hyst` 가 되면 확정.

</details>

<details>
<summary>힌트 5 — ζ 와 L 역산</summary>

연속한 반대 부호 극값의 비 = `exp(−πζ/√(1−ζ²))` (반주기당). 로그를 취해 평균한 `dl` 로 `ζ = dl/√(π²+dl²)`. 측정 주파수는 감쇠 주파수 fd이므로 `f0 = fd/√(1−ζ²)` 로 고친 뒤 `L = 1/((2πf0)²·C)`. 첫 극값(overshoot)은 에지 모양·스코프 필터의 영향을 받으니 ζ 계산에서 뺀다.

</details>

## 5. 풀이

### Part A — rise-time 예산: 계산 한 줄, 가정 세 개

**왜 0.35인가.** 1차 RC의 계단 응답 10%→90% 시간은 `τ·ln 9 = 2.197τ`, −3dB 대역폭은 `1/(2πτ)` 이므로 `tr·BW = 2.197/(2π) = 0.350`. 가우시안 응답(`H(f) = exp(−2π²σ²f²)`)이면 −3dB 가 `f = 0.1325/σ`, 10–90% 폭이 `2.563σ` 이라 `tr·BW = 0.3396`. 1GHz 이상 고급 스코프는 대역 끝까지 평탄하고 급격히 떨어지는(maximally-flat, brick-wall에 가까운) 응답이라 `tr·BW ≈ 0.4–0.45` 를 쓴다 (벤더 앱노트 표기 범위, 기종마다 데이터시트 rise time을 직접 확인).

**왜 RSS인가.** 시스템의 계단 응답을 "임펄스 응답의 누적분포"로 보면, 직렬 연결은 임펄스 응답의 **합성곱** 이고 합성곱은 분산을 더한다: `σ_tot² = σ_1² + σ_2² + …`. 10–90% 폭이 σ에 비례하는 모양(가우시안)끼리는 RSS가 **정확**하다. 모양이 다르면 근사다.

| 질문 | 계산 | 결과 (실행값) |
|---|---|---|
| 1ns @ 3% 시스템 BW | `tr_sys = 1ns·√(1.03²−1) = 246.8ps` → `0.35/tr_sys` | **1.4183 GHz** |
| 1GHz 시스템으로 보면 | `√(1+0.35²) − 1` | 5.95% |
| 500MHz로 보면 | `√(1+0.7²) − 1` | 22.07% |
| 2.5GHz 액티브 프로브 + ? | `√(246.8² − 140²) = 203ps` → `0.35/203ps` | **1.722 GHz** |
| 500MHz 패시브 프로브 + ? | 프로브 tr 700ps > 예산 247ps | **None (불가능)** |

경험칙 "스코프 BW는 신호 BW(0.35/tr)의 3~5배"는 이 식의 다른 표현이다: 3배 → 5.41%, 4배 → 3.08%, 5배 → 1.98% (연습 1). 3% 요구는 정확히 "약 4배"다.

**가정 확인 — 시뮬레이션.** 같은 1.418GHz에서:

```svg
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">1ns 가우시안 에지의 측정 tr 오차 vs 시스템 BW</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 30 L60 230 L700 230"/>
    <path d="M60 206 L700 206" stroke-dasharray="4 4" opacity=".6"/>
    <path d="M295 30 L295 230" stroke-dasharray="2 4" opacity=".6"/>
    <path d="M347 30 L347 230" stroke-dasharray="2 4" opacity=".6"/>
  </g>
  <polyline class="f-ok" fill="none" stroke="currentColor" stroke-width="2" points="60,53 86,104 111,136 137,157 162,172 188,182 214,190 239,197 265,202 290,205 316,209 342,211 367,213 393,215 418,217 444,218 470,219 495,220 521,221 546,222 572,222 598,223 623,223 649,224 674,224 700,225"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3" points="60,32 86,85 111,119 137,142 162,159 188,171 214,180 239,188 265,194 290,198 316,202 342,205 367,208 393,210 418,212 444,214 470,215 495,217 521,218 546,219 572,220 598,220 623,221 649,222 674,222 700,223"/>
  <g fill="currentColor" font-size="12">
    <text x="52" y="234" text-anchor="end">0%</text><text x="52" y="210" text-anchor="end">3%</text>
    <text x="52" y="34" text-anchor="end">25%</text>
    <text x="60" y="248" text-anchor="middle">0.5</text><text x="188" y="248" text-anchor="middle">1.0</text>
    <text x="444" y="248" text-anchor="middle">2.0</text><text x="700" y="248" text-anchor="middle">3.0 GHz</text>
    <text x="290" y="46" text-anchor="end">1.418</text><text x="352" y="46">1.622</text>
    <text x="430" y="120">실선: RSS (가우시안 체인과 정확히 일치)</text>
    <text x="430" y="140">점선: 단일 극점 스코프, B04-lite로 측정</text>
  </g>
  <text x="10" y="278" fill="currentColor" font-size="12">두 곡선 모두 실행값. 단일 극점은 같은 BW에서 오차가 더 크다 → 3%를 지키려면 1.622GHz.</text>
</svg>
```

- 가우시안 스코프(`tr·BW = 0.3396`) → 측정 오차 **3.00%**. RSS와 일치.
- 단일 극점 스코프 → 측정 오차 **3.87%**, 3%를 맞추려면 이분법으로 **1.622GHz**.

왜 단일 극점이 더 나쁜가: 분산이 더해진다는 원리로 보면, 단일 극점의 표준편차는 τ인데 10–90% 폭은 2.197τ 뿐이다(가우시안은 2.563σ). 즉 단일 극점은 "10–90% 숫자로 보이는 것보다 분산을 많이 보탠다". 등가 가우시안 tr로 환산하면 `2.563τ = 1.167·tr_scope`, `√(1 + (1.167·0.2468)²) − 1 ≈ 4.1%` — 측정한 3.87%와 같은 쪽으로 같은 크기만큼 틀린다(합성 결과가 완전한 가우시안이 아니라 정확히 같지는 않다). 면접 답: **"RSS는 각 단이 가우시안에 가까울 때 정확하고, 단일 극점이면 오차를 과소평가, 평탄 응답 스코프면 k=0.45로 바꿔 계산한다."** 실무에서는 스코프 데이터시트의 rise time 스펙(10–90%)을 직접 RSS에 넣는 게 k를 추측하는 것보다 낫다.

### Part B — 프로브는 회로의 일부다

**1) 부하(loading) — DC와 AC.** 10x 패시브 프로브의 입력은 tip 기준 10MΩ‖(모델에 따라 약 4–15pF; 이 레슨은 과제대로 12pF). DC에서 10MΩ은 33Ω 드라이버에 3ppm이라 무의미하지만, 1MΩ 같은 고임피던스 노드(RC 타이밍, 분압 sense, 크리스털 발진기)에서는 −9.1%(1x 프로브의 1MΩ이면 −50%) — 연습 4. ns 영역에서는 C가 지배한다: RC 근사로 `2.2·Rs·C = 2.2·33Ω·12pF = 0.87ns` 가 에지에 RSS로 더해진다.

시뮬레이션 실측(스프링 10nH, 1GHz 스코프):

| tip C | 예 | ζ (Rs=33Ω, 10nH) | RC 근사 2.2RsC | 측정 tr (진짜 1.000ns) |
|---|---|---|---|---|
| 1pF | 액티브(FET) 프로브 | 0.165 | 0.07ns | 1.01ns |
| 12pF | 10x 패시브 | 0.572 | 0.87ns | 1.12ns |
| 100pF | 1x 패시브 + 케이블 수준 | 1.65 | 7.26ns | 6.67ns |

RC 근사가 12pF에서 약 1.37ns(=√(1² + 0.87² + 0.35²))를 예측하는데 실제 1.12ns인 이유는 10nH가 있어 순수 RC가 아니라 **약하게 덜 감쇠된 RLC(ζ=0.57)** 이기 때문이다 — 응답이 빨라지는 대신 5% overshoot가 생긴다. 1pF에서는 1.59GHz 공진(ζ=0.165)이 1GHz 스코프의 고역 롤오프를 일부 메워서, 스코프만일 때(1.074ns)보다 오히려 빠른 1.01ns가 나온다; 스코프 필터를 빼면 0.93ns로 **진짜보다 빠르게** 측정된다. "측정 tr < 실제 tr"은 불가능하지 않다 — 공진(peaking)이 있으면 가능하다. `demo_peaking.py` 실행 결과:

```text
scope 1GHz only (ideal probe)      tr=1.074 ns  OS=0.00%
tip  1pF + 10nH, no scope filter   tr=0.934 ns  OS=0.00%   (f0=1.59GHz zeta=0.165)
tip  1pF + 10nH, scope 1GHz        tr=1.012 ns  OS=0.00%   (f0=1.59GHz zeta=0.165)
tip 12pF + 10nH, no scope filter   tr=1.064 ns  OS=5.93%   (f0=0.46GHz zeta=0.572)
tip 12pF + 10nH, scope 1GHz        tr=1.116 ns  OS=5.15%   (f0=0.46GHz zeta=0.572)
```

1pF 경우 overshoot가 0인 이유: 가우시안 1ns 에지는 1.59GHz 근처에 에너지가 거의 없어 공진을 "울리지" 못하고, 공진 아래 대역의 이득 상승(peaking)만 받아 에지가 날카로워진다 (원시 시뮬레이션의 max − final도 0.0035%였다).

**2) 그라운드 리드 = 인덕터.** 리드 루프 인덕턴스는 대략 1nH/mm(루프 모양에 따라 다름) → 15cm ≈ 150nH. 프로브 C와 직렬 공진:

| 그라운드 | L | f0 = 1/(2π√LC) | Z0 = √(L/C) | ζ = Rs/(2Z0) | 이상적 계단 OS | 1ns 에지 측정 OS |
|---|---|---|---|---|---|---|
| 15cm 클립 | 150nH | **118.6MHz** | 111.8Ω | 0.148 | 62.6% | **59.6%** (peak 5.27V) |
| 스프링 | 10nH | **459.4MHz** | 28.9Ω | 0.572 | 11.2% | **5.1%** (peak 3.47V) |

이상적 계단 OS(공식)와 1ns 에지 OS(시뮬레이션)의 차이가 핵심이다. **에지가 공진 주기보다 충분히 느리면 공진을 못 친다.** 150nH의 주기는 8.4ns라 1ns 에지는 거의 계단처럼 보여 59.6%가 거의 다 나오고, 스프링의 주기 2.18ns에 대해서는 1ns 에지가 상대적으로 느려 11.2%가 5.1%로 줄어든다. Part C의 FAN_PWM(20ns 에지)은 15cm 리드로도 OS 0.6%다 — "느린 신호는 악어 클립으로 재도 된다"의 근거.

```svg
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 1ns 에지, 같은 12pF 프로브 — 그라운드만 다르다 (simulate_probe 실행값)</text>
  <g stroke="currentColor" fill="none" opacity=".6">
    <path d="M60 232 L720 232"/>
    <path d="M60 111 L720 111" stroke-dasharray="4 4"/>
    <path d="M60 100 L720 100" stroke-dasharray="1 3"/>
  </g>
  <polyline class="f-warn" fill="none" stroke="currentColor" stroke-width="2" points="64,232 85,232 93,231 101,230 105,229 109,225 114,221 118,214 122,206 126,196 130,185 134,173 138,159 142,146 147,132 151,118 155,105 159,92 163,80 167,70 171,61 175,53 180,47 184,43 188,40 192,39 196,39 200,41 204,45 208,49 213,55 217,62 221,69 225,78 229,86 233,95 237,103 241,112 246,120 250,127 254,134 258,140 262,145 266,149 270,153 274,155 279,156 283,156 287,155 291,153 295,150 299,147 303,143 308,138 312,133 316,128 320,122 324,117 328,112 332,106 336,102 340,97 345,93 349,90 353,87 357,85 361,84 365,83 369,82 373,83 378,84 382,85 386,87 390,90 394,93 398,96 402,99 406,102 411,106 415,109 419,112 423,115 427,118 431,121 435,123 439,125 444,126 448,127 452,128 460,128 468,127 472,126 477,124 481,122 485,121 489,119 493,117 497,114 501,112 505,110 510,108 514,106 518,105 522,103 526,102 530,101 534,100 555,100 559,101 563,102 567,103 572,104 576,105 580,107 584,108 588,109 592,111 596,112 600,113 604,114 609,115 613,116 621,117 629,118 637,117 646,117 654,116 658,115 662,114 670,113 675,112 679,111 683,110 687,109 695,108 704,107 712,107 720,106"/>
  <polyline class="f-ok" fill="none" stroke="currentColor" stroke-width="2" points="64,232 85,232 89,231 93,230 97,227 101,221 105,209 109,193 114,173 118,152 122,133 126,119 130,110 134,105 138,104 142,105 147,107 151,109 155,110 159,111 175,111 720,111"/>
  <g fill="currentColor" font-size="12">
    <text x="55" y="236" text-anchor="end">0V</text><text x="55" y="115" text-anchor="end">3.3V</text>
    <text x="725" y="96">3.6V</text>
    <text x="200" y="34">15cm 리드: peak 5.27V (OS 59.6%), 링잉 117.3MHz</text>
    <text x="250" y="160">스프링: peak 3.47V (OS 5.1%)</text>
    <text x="60" y="254" text-anchor="middle">8</text><text x="225" y="254" text-anchor="middle">16</text>
    <text x="390" y="254" text-anchor="middle">24</text><text x="555" y="254" text-anchor="middle">32</text>
    <text x="720" y="254" text-anchor="middle">40 ns</text>
  </g>
  <text x="10" y="280" fill="currentColor" font-size="12">실제 DUT 핀은 어느 쪽도 아니다: 리드가 물린 상태의 노드도 14.3% 출렁인다 (프로브가 회로를 바꾼다).</text>
</svg>
```

**3) 링잉에서 숫자 뽑기.** `ring_analysis` 가 150nH 시뮬레이션에서 찾은 값: 극값 8개, f_ring = **117.33MHz** (이론 fd = f0·√(1−ζ²) = 117.33MHz), ζ_est = **0.1476** (이론 0.1476), 역산 L = **150.0nH**. 측정되는 것은 감쇠 주파수 fd라는 점을 기억하자: ζ=0.148이면 f0보다 1.1% 낮다. ζ=0.5면 13%(√0.75 = 0.866) 차이 나고 L로는 그 제곱(25%)이 되므로 역산 전에 반드시 보정한다.

| 원인 | 증상 | 그것을 드러내는 측정 |
|---|---|---|
| 긴 그라운드 리드 (L) | 모든 넷에서 **같은 주파수**의 링잉, 에지가 빠를수록 큼 | f_ring → `L = 1/((2πf)²C)` 가 리드 길이와 맞는가, 리드를 스프링으로 바꾸면 사라지는가 |
| 프로브 tip C (부하) | 에지가 느려짐, 고임피던스 노드 동작 변화 | 액티브 프로브(<1pF)와 비교, 2.2·Rs·C 추정 |
| 스코프/프로브 BW 부족 | tr 이 BW 한계(0.35/BW) 근처에서 안 줄어듦, 링잉 진폭 감소 | 더 높은 BW 체인과 RSS 비교 |
| 실제 SI 문제 (반사, 종단 없음) | 스프링에서도 남는 링잉, 넷마다 다른 주파수 | 스프링/액티브 재측정, 주파수 ↔ 트레이스 길이(왕복 지연) 비교 (HWLAB-M01) |
| 그라운드 리드가 노드를 흔듦 | 프로브를 대면 기능이 바뀜 (드물게 오류가 생기거나 사라짐) | 프로브 없이 기능 테스트, 액티브/차동 프로브로 재확인 |

### Part C — 두 장의 캡처로 판정하기

판정 로직은 단순하다 — 짧은 그라운드가 기준이다:

```text
spring peak > vmax              -> REAL      (짧은 그라운드에서도 남는다 → 보드 SI 문제)
lead peak > vmax, spring OK      -> ARTIFACT  (리드가 만든 링잉, 판정은 spring 기준 PASS)
둘 다 OK                         -> PASS
+ lead 캡처의 f_ring 과 프로브 C 로 L_est 를 역산해 리포트에 적는다 (50–300nH 면 '그라운드 리드 루프')
```

solution의 실제 리포트:

```text
ARTIFACT SPI0_SCLK | lead15cm peak=5.27V OS=59.8% ring=118.0MHz -> L_est=149nH (ground-lead loop) | spring peak=3.48V OS=5.3% tr=1.12ns ring=none | limit 3.60V -> PASS on spring tip; lead ringing is a measurement artifact
REAL     QSPI_CLK  | lead15cm peak=5.97V OS=81.0% ring=117.6MHz -> L_est=150nH (ground-lead loop) | spring peak=4.71V OS=42.8% tr=1.04ns ring=242.6MHz | limit 3.60V -> FAIL: overshoot persists with spring tip (board SI, not the probe)
PASS     FAN_PWM   | lead15cm peak=3.32V OS=0.6% ring=none | spring peak=3.32V OS=0.6% tr=19.88ns ring=none | limit 3.60V -> PASS (edge too slow to excite the lead resonance)
```

읽는 법:

- **lead 캡처만 보면 SPI0_SCLK과 QSPI_CLK이 구별되지 않는다.** 둘 다 ~118MHz, L_est ≈ 150nH — 리드의 지문이 보드의 신호를 덮어버린다. 그래서 "링잉 주파수가 리드로 설명된다"는 것만으로 ARTIFACT라고 결론 내리면 안 되고, 반드시 짧은 그라운드로 다시 잰다. 이게 이 Part의 함정이다.
- QSPI_CLK 스프링 캡처에는 **242.6MHz** 링잉이 남는다. 생성기가 넣은 보드 공진은 250MHz인데 3% 낮다 — 12pF+10nH 프로브가 노드에 짐을 얹어 공진을 끌어내린 것. 숫자가 "정확히 250"이 아니라는 게 오히려 현실적이다. 조치: 직렬 종단 저항(예: 22–33Ω) 실장 후 재측정, 주파수를 트레이스 길이의 왕복 지연과 비교 (HWLAB-M01).
- SPI0_SCLK 스프링 tr 1.12ns는 진짜 1.00ns가 아니다 — 12pF 부하 + 1GHz 체인. 타이밍 마진 리포트(HWLAB-M05)에 이 값을 그대로 쓰면 안 되고, "측정 체인 포함" 이라고 적거나 액티브 프로브로 다시 잰다.
- FAN_PWM은 20ns 에지라 리드 공진(8.4ns 주기)을 거의 못 친다 → 둘 다 OS 0.6%.

**장비가 있을 때 (실제 벤치).** 같은 판정을 손으로 하는 순서: ① 15cm 클립을 떼고 그라운드 스프링(또는 tip-and-barrel, 가까운 GND 비아) 사용 ② 스코프의 BW limit이 꺼져 있는지 확인(리플 측정과 반대 — HWLAB-B08) ③ 링잉 주파수를 커서/자동 측정으로 읽고 `1/(2π√(L·C_probe))` 와 비교 ④ 리드 루프를 넓혔다 좁혔다 해서 주파수·진폭이 따라 변하면 프로브 문제 ⑤ 여전히 의심되면 액티브 프로브(<1pF, 수 GHz)로 확인. 스코프를 PyVISA/SCPI로 자동화해 두 캡처를 CSV로 받으면 이 레슨의 `diagnose` 를 그대로 돌릴 수 있다 (HWLAB-A07). 이 레슨에서는 실장비를 쓰지 않았고 모든 캡처는 합성이다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"     # 22/22 passed, exit=0  (실행 시간 ~0.45s)
python3 -I gen_captures.py                  # captures/*.csv 재생성 → sha 동일 확인함
```

실제 출력:

```text
-- B04-lite tool sanity (given) --
PASS edge_measure on an ideal 1ns Gaussian edge  [ideal 1ns edge -> tr 1.0000 ns, top 3.3000 V]
PASS 6 captures load (1601 samples each)  [samples per capture [1601, 1601, 1601, 1601, 1601, 1601]]
-- Part A: rise-time budget --
PASS rss(3,4) == 5  [5.000000]
PASS required_system_bw(1ns, 3%) ~ 1.418 GHz  [1ns @3% -> system BW 1.4183 GHz (tr_sys 246.8 ps)]
PASS rise_error 1ns via 1GHz / 500MHz  [1GHz 5.95%, 500MHz 22.07%]
PASS rise_error at required BW == 3.00%  [3.0000%]
PASS required_scope_bw with 2.5GHz active probe ~ 1.72 GHz  [1.722 GHz]
PASS 500MHz passive probe alone blows the 3% budget -> None  [probe tr 700 ps > budget 247 ps]
PASS simulated Gaussian chain matches RSS (3%)  [Gaussian scope (k=0.3396) measured error 3.00%]
PASS single-pole scope: RSS underestimates the error  [single-pole scope @1.418GHz measured error 3.87% (RSS says 3.00%)]
PASS bisection: single-pole needs more BW for 3%  [single-pole BW for 3% = 1.622 GHz]
-- Part B: probe loading + ground-lead RLC --
PASS 150nH+12pF: f0 118.6MHz, Z0 111.8, zeta 0.148  [f0=118.6MHz Z0=111.8ohm zeta=0.1476 OS_step=62.6%]
PASS 10nH spring: f0 459.4MHz, zeta 0.572, OS_step 11.2%  [f0=459.4MHz Z0=28.9ohm zeta=0.5716 OS_step=11.2%]
PASS simulated overshoot: 15cm lead vs spring tip  [1ns edge, Rs=33: OS 150nH 59.6% (peak 5.27V) vs spring 5.1% (peak 3.47V)]
PASS ring_analysis recovers fd and zeta (150nH)  [f_ring 117.33MHz vs fd 117.33MHz, zeta_est 0.1476 vs 0.1476, 8 extrema]
PASS L back-calculated from ringing ~ 150nH  [L_est 150.0 nH from f_ring + C=12pF]
PASS probe C loading slows the edge  [spring, tip C 1/12/100 pF -> tr 1.01 / 1.12 / 6.67 ns (2.2*Rs*C = 0.07 / 0.87 / 7.26 ns)]
PASS long lead also disturbs the DUT node  [DUT node itself (not the scope) overshoots 14.3% with 15cm lead attached]
-- Part C: BRD-A rev0 captures --
    ARTIFACT SPI0_SCLK | lead15cm peak=5.27V OS=59.8% ring=118.0MHz -> L_est=149nH (ground-lead loop) | spring peak=3.48V OS=5.3% tr=1.12ns ring=none | limit 3.60V -> PASS on spring tip; lead ringing is a measurement artifact
PASS SPI0_SCLK -> ARTIFACT (ground lead)  [L_est 149nH]
    REAL     QSPI_CLK  | lead15cm peak=5.97V OS=81.0% ring=117.6MHz -> L_est=150nH (ground-lead loop) | spring peak=4.71V OS=42.8% tr=1.04ns ring=242.6MHz | limit 3.60V -> FAIL: overshoot persists with spring tip (board SI, not the probe)
PASS QSPI_CLK -> REAL (planted: series R DNP)  [spring ring 242.6 MHz (board resonance)]
    PASS     FAN_PWM   | lead15cm peak=3.32V OS=0.6% ring=none | spring peak=3.32V OS=0.6% tr=19.88ns ring=none | limit 3.60V -> PASS (edge too slow to excite the lead resonance)
PASS FAN_PWM -> PASS (slow edge, lead harmless)  [20ns edge, lead OS 0.58%]
PASS SPI0_SCLK spring tr shows probe loading  [spring tr 1.118 ns (true 1.000 ns; 12pF loading + 1GHz)]
22/22 passed
exit=0
```

모델 가정 (리포트에 같이 적어야 하는 것): Thevenin Rs=33Ω (드라이버 + 직렬 저항 가정), 가우시안 소스 에지, 스코프+프로브 시스템 = 1GHz 단일 극점, 프로브 tip = 10MΩ‖12pF 집중 소자, 그라운드 리드 = 집중 인덕터(전송선로 효과·tip 자체 인덕턴스 무시), 보드 공진 = Thevenin 소스 앞의 2차 공진기(전송선로를 극단적으로 단순화). 잡음 6mV rms, 0.1mV 반올림, 양자화(8비트 ADC)는 넣지 않았다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/행동 |
|---|---|---|
| "1ns 에지니까 350MHz 스코프면 된다" | 0.35/tr는 *신호* BW. 시스템도 350MHz면 tr 오차 41% | `required_system_bw(tr, err)` — 3%면 약 4배 |
| 프로브 BW를 예산에서 빼먹음 | 500MHz 패시브 프로브 하나로 이미 22% | `required_scope_bw(..., probe_bw)` 가 None을 낼 수 있게 |
| 모든 스코프에 k=0.35 | 평탄 응답 고급 스코프는 0.4–0.45 → 같은 BW라도 tr이 길다 | 데이터시트의 rise time 스펙을 직접 RSS |
| overshoot를 `max − 마지막 값` 으로 계산 | 캡처 끝까지 링잉이 안 끝나면 top이 틀림 | 히스토그램 mode 상태 레벨 (B04-lite, HWLAB-B04) |
| 링잉 주파수를 극값 시각으로 잼 | 기울기 0 근처라 잡음에 취약 | 극값 사이 0 교차를 선형 보간 |
| 측정 주파수를 바로 f0로 써서 L 역산 | 측정되는 것은 fd = f0√(1−ζ²) | `f0 = fd/√(1−ζ²)` 보정 |
| 단순 sign-change로 극값/교차 검출 | 잡음이 0 근처에서 수십 개의 가짜 교차를 만든다 | 히스테리시스(6σ_noise 또는 2% 진폭 중 큰 값) |
| "링잉이 리드로 설명되니 ARTIFACT" | QSPI_CLK처럼 리드 지문이 실제 문제를 덮는다 | 짧은 그라운드 재측정이 판정 기준 |
| 스프링 tr을 신호 tr로 보고 | 12pF 부하 + 체인 BW 포함 (1.12ns vs 1.00ns) | "측정 체인 포함" 명시 또는 액티브 프로브 |
| RK4 대신 오일러, dt=50ps | 459MHz 공진에서 수치 감쇠/발산 → 가짜 ζ | dt ≤ 주기/100, RK4 |

## 8. 면접 Q&A

<details>
<summary>Q1. 1ns 에지를 보려면 몇 MHz 스코프가 필요한가요? 그 근사의 가정은?</summary>

신호 자체의 등가 BW는 0.35/1ns = 350MHz지만, 측정 tr 오차를 3%로 묶으려면 시스템(스코프+프로브) tr이 신호의 1/4 (246.8ps)이어야 하므로 약 1.42GHz. 가정: ① RSS — 각 단이 가우시안 응답일 때 정확 (분산이 더해지므로) ② k=0.35 — 단일 극점/가우시안 근사. 단일 극점 시스템이면 시뮬레이션상 1.62GHz가 필요하고, 평탄 응답 스코프(k≈0.45)면 1.82GHz. 프로브 BW도 예산에 들어간다.
꼬리질문: "5배 규칙의 근거는?" → `√(1+1/5²) − 1 ≈ 2%`. 3배면 5.4%. "그럼 2GHz 스코프면 무조건 OK?" → 500MHz 패시브 프로브를 꽂으면 그 프로브가 22%를 만든다.

</details>

<details>
<summary>Q2. 15cm 그라운드 클립으로 SPI 클럭을 쟀더니 overshoot 60%입니다. 진짜인가요?</summary>

먼저 링잉 주파수를 읽는다. 10x 패시브 프로브 ~12pF와 15cm 리드 ~150nH면 `1/(2π√LC) ≈ 119MHz`. 측정 주파수가 이 근처이고, 다른 넷에서도 같은 주파수로 링잉하면 프로브 공진을 의심한다. 그 다음 그라운드 스프링이나 가까운 GND 비아로 다시 잰다 — 이 레슨 시뮬레이션에서는 59.6% → 5.1%. 판정은 짧은 그라운드 측정 기준.
꼬리질문: "스프링으로도 40% 남으면?" → 실제 SI 문제. 링잉 주파수를 트레이스 왕복 지연과 비교하고(반사, HWLAB-M01), 소스 직렬 종단·드라이브 세기·슬루율 설정을 확인한다. "링잉 주파수가 리드와 일치하면 안심해도 되나?" → 아니다. 리드 공진이 실제 문제를 덮을 수 있다 (Part C의 QSPI_CLK: 리드 캡처만 보면 SPI0_SCLK과 똑같다).

</details>

<details>
<summary>Q3. 1x와 10x 프로브는 언제 무엇을 쓰나요?</summary>

10x: 입력 10MΩ‖수~십수 pF, BW 수백 MHz급 — 일반 디지털·클럭 측정의 기본. 1x: 감쇠가 없어 mV급 신호(잡음, 작은 리플)에 유리하지만 입력 용량이 수십~100pF 이상(케이블 포함)이고 BW가 수 MHz~수십 MHz 수준이라 빠른 에지에는 못 쓴다. 1x로 33Ω 노드를 보면 RC만 2.2·33·100pF ≈ 7ns.
꼬리질문: "10x 프로브 보상(compensation)이 안 맞으면 어떤 모양인가?" → 스코프의 1kHz 교정 사각파에서 모서리가 뾰족하게 튀거나(과보상) 둥글게 처진다(부족보상). 이것은 수백 µs 시간 스케일의 저주파 문제이고, 그라운드 리드 링잉(ns 스케일, 100MHz대)과는 완전히 다른 현상이다 — 섞어 말하면 감점.

</details>

<details>
<summary>Q4. 프로브가 회로 동작 자체를 바꾸는 예를 들어 보세요. 어떻게 피하나요?</summary>

① 크리스털 발진기 핀(특히 출력 쪽)에 10x 프로브를 대면 부하 용량이 늘어 주파수가 당겨지거나 발진이 멈춘다 ② 고임피던스 노드(1MΩ 분압, RC 리셋 타이머)는 10MΩ에도 −9% ③ 33Ω 노드의 1ns 에지는 12pF로 눈에 띄게 느려진다 ④ 긴 그라운드 리드는 DUT 노드 자체를 흔든다 (이 레슨 모델에서 노드 overshoot 14.3%). 대책: 액티브 FET 프로브(<1pF), 버퍼 출력/테스트 포인트 사용, 짧은 그라운드, 측정 전후로 기능이 바뀌는지 확인.
꼬리질문: "액티브 프로브의 단점은?" → 입력 전압 범위·동적 범위가 좁고(모델별 데이터시트 확인), ESD/과전압에 약하며 비싸고 전원·오프셋 설정이 필요하다.

</details>

<details>
<summary>Q5. 하이사이드 스위치 노드나 차동 신호를 보려고 그라운드 클립을 다른 노드에 물리면?</summary>

일반 벤치 스코프의 프로브 그라운드는 스코프 섀시를 통해 접지(earth)에 연결돼 있다. 그라운드 클립을 GND가 아닌 노드에 물리면 그 노드를 접지에 단락시킨다 — 보드나 프로브가 탈 수 있다. 두 채널을 빼서(A−B) 보는 방법은 공통 모드 제거비가 나쁘다. 차동 프로브(고전압용/고속용)를 쓰는 게 정석이고, 스코프 접지를 끊는 것("float")은 안전 문제로 금지.
꼬리질문: "두 프로브의 그라운드를 서로 다른 GND 지점에 물리면?" → 그라운드 루프가 생겨 GND 사이의 전압 강하·자기 결합이 측정에 섞인다. 같은 기준점을 쓰고 루프 면적을 줄인다.

</details>

<details>
<summary>Q6. 측정한 rise time이 실제보다 빠를 수도 있나요?</summary>

있다. 공진(peaking)이 있는 체인은 고역 이득이 1보다 커져 에지를 날카롭게 만든다. 이 레슨의 1pF+10nH(ζ=0.165, 1.59GHz 공진) 경우 스코프 필터를 빼면 1ns 에지가 0.93ns로 측정됐다. 일부 스코프의 DSP 응답 보정(equalization)도 비슷한 효과를 낸다. 그래서 overshoot와 tr을 함께 보고, tr이 BW 한계보다 비정상적으로 짧으면 체인을 의심한다.
꼬리질문: "그럼 tr은 언제 믿나?" → 체인 tr이 신호 tr의 1/4 이하이고, 체인 응답이 단조(링잉 없음)이며, 프로브를 바꿔도 값이 안정적일 때.

</details>

<details>
<summary>Q7. 캡처에서 링잉 주파수와 감쇠비를 어떻게 숫자로 뽑나요? 그걸로 무엇을 알 수 있나요?</summary>

top 상태 레벨 기준 편차의 교대 극값을 히스테리시스로 찾고, 극값 사이 0 교차 간격(반주기)으로 주파수를, 연속 극값의 진폭비(반주기당 `exp(−πζ/√(1−ζ²))`)로 ζ를 구한다. 프로브 C를 알면 `L = 1/((2πf0)²C)` 로 루프 인덕턴스, `R = 2ζ√(L/C)` 로 직렬 저항(드라이버 + 직렬 R)까지 추정된다 — 이 레슨 캡처에서 L ≈ 149nH, R ≈ 32Ω (생성기 값 150nH, 33Ω, 연습 2).
꼬리질문: "스프링 캡처에서는 왜 ring=none?" → ζ=0.57이라 반주기마다 진폭이 약 11%로 줄어 두 번째 극값이 히스테리시스(66mV) 아래로 떨어진다. 주파수를 재려면 최소 2개의 0 교차가 필요하다.

</details>

## 9. 추가 연습

### 연습 1 — 스코프 응답 모양과 3·4·5배 규칙

`required_bw(tr, err, k)` 로 k=0.35와 0.45를 비교하고, "스코프 BW = m × (0.35/tr_sig)"일 때 RSS 오차를 m=3,4,5에 대해 구하라.

<details>
<summary>답 보기</summary>

1ns @3%: k=0.35 → 1.418GHz, k=0.45 → 1.823GHz. 규칙 오차는 `√(1+1/m²) − 1` : 3배 5.41%, 4배 3.08%, 5배 1.98%. "3% 이내"는 사실상 4배 규칙이다. 평탄 응답 스코프라면 같은 m에서도 오차가 더 크니(k가 더 큼) 데이터시트 rise time을 쓴다.

</details>

### 연습 2 — 링잉 캡처에서 직렬 저항 역산

`SPI0_SCLK_lead15cm` 캡처에서 f_ring, ζ를 재고 프로브 C=12pF로 L과 직렬 R을 역산하라. 생성기 값은 33Ω.

<details>
<summary>답 보기</summary>

`f0 = fd/√(1−ζ²)`, `L = 1/((2πf0)²C)`, `R = 2ζ√(L/C)`. 실행값: f_ring 118.0MHz, ζ 0.143, L 149nH → R 31.8Ω (3.6% 낮음 — 잡음과 1GHz 필터의 영향). 실무 의미: ζ가 너무 작으면 "드라이버가 생각보다 세다 / 직렬 저항이 빠졌다"는 단서가 된다.

</details>

### 연습 3 — overshoot ≤ 10%를 위한 최대 그라운드 리드

Rs=33Ω, C=12pF, 1ns 에지에서 시뮬레이션 overshoot가 10% 이하인 최대 L을 이분법으로 찾고, 10nH/cm로 길이를 환산하라. 이상적 계단 기준(ζ에서 역산)과 비교하라.

<details>
<summary>답 보기</summary>

이상적 계단: OS=10% → ζ ≥ 0.591 → `Z0 ≤ 33/(2·0.591) = 27.9Ω` → L ≤ 9.3nH. 1ns 에지 시뮬레이션: L_max 13.8nH ≈ 1.4cm. 에지가 공진을 덜 쳐서 조금 더 허용되지만, **1ns 에지에서는 사실상 스프링만 허용된다.** 15cm 클립은 10배 이상 길다.

</details>

### 연습 4 — 프로브 부하 허용치 (AC와 DC)

① 1ns 에지, Rs=33Ω 노드에서 tr 증가를 10% 이내로 하려면 tip C는 최대 몇 pF인가 (RC 근사 + RSS)? ② 1MΩ Thevenin 노드를 10x(10MΩ)와 1x(1MΩ)로 읽을 때 DC 오차는?

<details>
<summary>답 보기</summary>

① `2.2·Rs·C ≤ 1ns·√(1.1²−1) = 458ps` → C ≤ 6.31pF. 12pF 패시브 프로브는 불합격, ~1pF 액티브 프로브는 여유. (Part B에서 보듯 실제 L이 있으면 RC 근사보다 덜 느려지지만 overshoot를 대가로 낸다.) ② 10x: −9.1%, 1x: −50%. 고임피던스 노드는 DMM(보통 10MΩ)도 같은 문제를 갖는다.

</details>

### 연습 5 — 그라운드 리드에 감쇠 저항을 넣으면?

15cm 리드(150nH)를 꼭 써야 한다면 루프에 직렬 저항을 넣어 ζ=0.7로 만들 수 있다. 추가 저항과 그때의 overshoot, rise time을 시뮬레이션하라.

<details>
<summary>답 보기</summary>

`R_total = 2·0.7·√(150n/12p) = 156.5Ω` → 추가 123.5Ω. 실행값: OS 4.4%, 그러나 tr 3.01ns (진짜 1ns). 링잉을 감쇠시켜도 L과 C가 만든 대역폭 한계는 그대로라 에지가 3배 느려진다 — **루프 인덕턴스는 감쇠로 해결되지 않고 줄여야 한다.** (일부 프로브 액세서리의 저항성 tip이 같은 원리지만, 고속 에지 측정의 정답은 짧은 그라운드다.)

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 7/7 passed, exit=0
```

실제 출력:

```text
-- 연습 1 --
PASS k=0.35 vs 0.45 for 1ns @3%  [single-pole/Gaussian-ish 1.418 GHz, brick-wall-ish 1.823 GHz]
PASS 3x / 4x / 5x rule errors  [3x 5.41%, 4x 3.08%, 5x 1.98%]
-- 연습 2 --
PASS series R from SPI0_SCLK lead capture ~ 33 ohm  [f_ring 118.0 MHz, zeta 0.143, L 149 nH -> R_series 31.8 ohm (gen used 33)]
-- 연습 3 --
PASS max ground lead for OS <= 10%  [1ns edge: L_max 13.8 nH (~1.4 cm @10nH/cm); ideal-step bound zeta>=0.591 -> L<=9.3 nH]
-- 연습 4 --
PASS max tip C for <=10% tr growth (Rs=33, 1ns)  [C_max 6.31 pF -> 12pF passive fails, ~1pF active OK]
PASS DC loading of a 1Mohm node: 10M (10x) vs 1M (1x)  [10x -9.1%, 1x -50.0%]
-- 연습 5 --
PASS damping resistor in 15cm lead: OS down, tr up  [add 123.5 ohm -> OS 4.4% but tr 3.01 ns (true 1 ns)]
7/7 passed
exit=0
```

## 10. 핵심 정리

- `tr·BW = 0.35`(단일 극점) / 0.34(가우시안) / 0.4–0.45(평탄 응답 스코프). 신호 BW와 측정 시스템 BW는 다른 숫자다.
- `tr_meas = √(tr_sig² + tr_scope² + tr_probe²)` 는 가우시안끼리 정확, 단일 극점이면 오차를 과소평가 (1.418GHz에서 3.00% 예측 vs 3.87% 측정).
- 1ns 에지 @3% → 시스템 1.42GHz (≈4배 규칙). 500MHz 패시브 프로브 하나로 이미 22%.
- 그라운드 리드 ≈ 1nH/mm. 프로브 C와 직렬 공진 `f0 = 1/(2π√LC)`: 150nH+12pF → 118.6MHz, 10nH → 459MHz. `ζ = Rs/(2√(L/C))`.
- 같은 1ns 에지: 15cm 리드 OS 59.6% vs 스프링 5.1%. 에지가 공진 주기보다 느리면 리드도 무해(20ns 에지 0.6%).
- 링잉 주파수(0 교차) + 감쇠(로그 감쇠율) → L, R 역산. 측정되는 건 fd, f0로 보정.
- 판정은 짧은 그라운드 기준: 리드 지문(같은 ~118MHz)이 실제 SI 문제를 덮을 수 있다.
- tip C는 에지를 늦추고 노드를 바꾼다 — 숫자를 보고할 때 측정 체인을 같이 적는다.

## 참고자료

- Howard Johnson, Martin Graham, *High-Speed Digital Design: A Handbook of Black Magic* — 오실로스코프 프로브, 그라운드 루프 인덕턴스, rise time 합성
- Eric Bogatin, *Signal and Power Integrity — Simplified* — 인덕턴스·링잉·rise time과 대역폭
- Tektronix, *ABCs of Probes* (primer) — 프로브 부하, 보상, 그라운드 리드 영향
- Keysight Application Note, *Understanding Oscilloscope Frequency Response and Its Effect on Rise-Time Accuracy* — 가우시안 vs 평탄 응답, k=0.35/0.45
- IEEE Std 181 — Transitions, Pulses, and Related Waveforms (상태 레벨·10–90% 정의, HWLAB-B04)
- Horowitz, Hill, *The Art of Electronics*, 3rd ed. — RC/RLC 응답, 프로브 실무
- [PyVISA documentation](https://pyvisa.readthedocs.io/) — 스코프 파형 자동 획득 (HWLAB-A07)
- 관련 레슨: HWLAB-B04 (측정 알고리즘), HWLAB-B08 (BW limit과 리플), HWLAB-M01 (전송선로 반사), HWLAB-M05 (타이밍 컴플라이언스)
