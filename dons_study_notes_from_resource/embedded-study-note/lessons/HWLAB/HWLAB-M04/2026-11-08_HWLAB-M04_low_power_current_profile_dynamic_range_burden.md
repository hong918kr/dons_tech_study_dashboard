---
id: HWLAB-M04
time: 80분
updated: 2026-11-08
---
# HWLAB-M04 저전력 전류 프로파일 측정: 다이내믹 레인지·Burden 전압

## 학습 목표

- 저전력 MCU 전류(sleep 2 µA ↔ active 15 mA)의 **진짜 평균·배터리 수명**을 정의하고, 계측기 사양 하나하나(샘플링률·샘플 방식·ADC 분해능·자동 레인지 전환 지연·션트 burden)가 그 숫자를 얼마나 바꾸는지 **원인별로 떼어 내** 계산한다.
- 점 샘플(point)과 적분형(integrating, boxcar) 샘플의 차이를 수치로 보인다: 1 kS/s 점 샘플은 위상 평균으로는 −0.75 %지만 **1 s 캡처 한 번은 +327 %**, 적분형은 어떤 fs에서든 평균이 정확하지만 피크를 잃는다(1 kS/s에서 15 mA → 6.6 mA).
- 다이내믹 레인지 750 000 : 1(117.5 dB)이 왜 단일 레인지 20비트를 요구하는지, 왜 실제 장비가 **자동 레인지(여러 션트)** 를 쓰는지 설명하고, 그 대가인 **전환 지연 → 디캡 sag → brown-out** 과 **재충전 스파이크** 를 RC 시뮬레이션으로 재현한다.
- 후보 계측기 6종을 같은 모델로 돌려 `PASS / MARGINAL / FAIL` 을 내고, 이 DUT에 **필요한 장비 사양**(DR, fs, burden, 전환 지연 상한)을 식으로 도출한다.

## 1. 문제

배터리 제품의 bring-up에서 "sleep 전류 몇 µA, 배터리 몇 년" 은 반드시 나오는 숫자인데, 그 숫자를 낸 **계측기가 DUT를 brown-out 시키거나 sleep을 0으로 읽고 있으면** 수명 예측이 수십 % 틀린다 — Rivian/Apple/NVIDIA 보드·실리콘 검증 면접에서 "µA와 mA가 섞인 전류를 어떻게 재고, 그 장비의 어떤 사양이 결과를 망치는가" 는 단골이다.

가상의 보드 **SENSNODE-L rev A** (nRF52/STM32L4급 MCU + 센서, CR2032). `VDD_MCU` 전류는 1 s 프레임이 반복된다: 250 ms마다 RTC 틱 ISR(15 mA × 63 µs, 센서 FIFO 확인), 프레임마다 한 번 측정 + BLE 송신(15 mA × 817 µs), 나머지는 sleep 2 µA. 디캡 `C_dut` = 2.2 µF, 공급 3.0 V, MCU brown-out(BOR) 1.8 V (대표값 — MCU 데이터시트 확인 필요). 이 데이터는 `profile_sensnode.json` 에 있고, 진짜 값(truth)은 해석적으로 정확히 안다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">측정 체인: 계측기 션트(레인지)는 DUT와 직렬 — 디캡은 DUT 쪽에 있다</text>
  <rect x="20" y="50" width="90" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="65" y="76" text-anchor="middle" fill="currentColor">V_src</text><text x="65" y="94" text-anchor="middle" fill="currentColor" font-size="12">3.0 V</text>
  <line x1="110" y1="80" x2="160" y2="80" stroke="currentColor"/>
  <rect x="160" y="40" width="200" height="80" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="260" y="62" text-anchor="middle" fill="currentColor">계측기: R_shunt(레인지 r)</text>
  <text x="260" y="82" text-anchor="middle" fill="currentColor" font-size="12">1 kΩ / 100 / 10 / 1 / 0.05 Ω</text>
  <text x="260" y="102" text-anchor="middle" fill="currentColor" font-size="12">ADC → quantize → 샘플러(fs, mode)</text>
  <line x1="360" y1="80" x2="470" y2="80" stroke="currentColor"/>
  <text x="415" y="72" text-anchor="middle" fill="currentColor" font-size="12">i_shunt</text>
  <circle cx="470" cy="80" r="3" fill="currentColor"/>
  <text x="470" y="64" text-anchor="middle" fill="currentColor" font-size="12">V_dut</text>
  <line x1="470" y1="80" x2="470" y2="105" stroke="currentColor"/>
  <line x1="455" y1="105" x2="485" y2="105" stroke="currentColor" stroke-width="2"/><line x1="455" y1="113" x2="485" y2="113" stroke="currentColor" stroke-width="2"/>
  <line x1="470" y1="113" x2="470" y2="135" stroke="currentColor"/>
  <text x="492" y="114" fill="currentColor" font-size="12">C_dut 2.2 µF</text>
  <line x1="470" y1="80" x2="590" y2="80" stroke="currentColor"/>
  <rect x="590" y="50" width="150" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="665" y="76" text-anchor="middle" fill="currentColor">DUT (MCU)</text><text x="665" y="94" text-anchor="middle" fill="currentColor" font-size="12">I_load, BOR 1.8 V</text>
  <text x="380" y="150" text-anchor="middle" fill="currentColor" font-size="12">V_dut = V_src − i_shunt·R (정상 상태) — 이 전압 강하가 burden. 계측기는 i_shunt 를 보고 I_load 는 직접 못 본다</text>
  <text x="10" y="186" fill="currentColor" font-weight="bold">1 s 프레임 (전류 축은 로그)</text>
  <line x1="60" y1="300" x2="740" y2="300" stroke="currentColor"/>
  <line x1="60" y1="200" x2="60" y2="300" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="55" y="214">15 mA</text><text x="55" y="294">2 µA</text></g>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".4"><line x1="60" y1="210" x2="740" y2="210"/></g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="60,290 145,290 145,210 148,210 148,290 315,290 315,210 318,210 318,290 400,290 400,210 410,210 410,290 485,290 485,210 488,210 488,290 655,290 655,210 658,210 658,290 740,290"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="146" y="200">tick 63 µs</text><text x="316" y="200">tick</text><text x="405" y="200">meas+TX 817 µs</text>
    <text x="486" y="200">tick</text><text x="657" y="200">tick (250 ms 간격)</text>
    <text x="400" y="320">I_avg = 2 µA + (15 mA − 2 µA)·(4·63 + 817 µs)/1 s = 18.033 µA → CR2032 220 mAh 로 1.39 년. sleep 몫 11.1 %</text>
  </g>
</svg>
```

### Part A — 진짜 값과 샘플러: 1k / 10k / 100k S/s

- `average_current(segs)` (∫I dt / T), `battery_life_h(I, mAh)` (가장 단순한 용량/전류 모델 — 용량 감소·펄스 부하·온도 보정은 see:RF-A04).
- `sample_point(segs, fs, phase)` — 점 샘플(sample-and-hold). `sample_avg(segs, fs, phase)` — 샘플 주기 동안의 적분 평균(integrate-and-dump). 둘 다 **세그먼트 단위로 한꺼번에** 만들어 1 MS/s도 빠르게.
- 각 fs·방식에서 샘플 위상을 한 주기 안에서 100개(점)/10개(적분)로 바꿔 가며 "캡처 한 번" 의 평균 오차와 피크를 잰다: **위상 평균 오차 / 최악 1회 캡처 오차 / 최악 피크**.

### Part B — ADC 분해능, burden, 자동 레인지 전환 지연

- `quantize(i, lsb, FS)` — 포화 + 반올림. 단일 레인지 FS 20 mA를 8/12/16/20비트로 바꿔 sleep 판독값과 평균 오차.
- `simulate_event(...)` — 버스트 하나를 1 µs씩 **정확한 RC 해** 로 적분: 션트 R(레인지), 디캡 C_dut, 정전류 부하. 자동 레인지 상태기계(과전류 감지 → `t_switch` 지연 뒤 최고 레인지로, `t_hold` 동안 낮으면 한 단계씩 하강).
- DMM µA 레인지(100 Ω)는 해석해와 1 µV 이내로 같아야 하고(검증), 전환 지연 1/20/100/200 µs의 Vmin·전하 오차를 비교한다.

### Part C — SENSNODE-L: 어떤 계측기로 잴 것인가

`instruments.json` 의 6종(DMM µA, DMM mA, 스코프 + 10 Ω, 느린 자작 자동 레인지, PPK2급 100 kS/s 점 샘플, Joulescope급 1 MS/s 적분)을 같은 모델로 돌려 목표(1 s 캡처 평균 오차 ≤ 1 %, sleep ≤ 1 %, 피크 ≥ 95 %, Vmin ≥ BOR + 0.2 V)로 판정하고, `required_spec()` 으로 필요한 사양을 식으로 도출해 한 줄 결론(PICK)을 낸다. 수치는 모두 **대표값**(특정 제품 데이터시트 아님)이다.

## 2. Problem (English)

> **Quantify how a current meter distorts a low-power MCU current profile, and derive the meter spec you need.**
>
> A sensor node draws 2 µA in sleep and 15 mA bursts (4 × 63 µs ticks + one 817 µs burst per 1 s frame) from 3.0 V through
> the meter's shunt; the DUT has 2.2 µF of decoupling and browns out below 1.8 V. Using only the Python 3 standard library:
>
> 1. Represent current as piecewise-constant segments `(t0_us, t1_us, amps)`. Implement `average_current`, a point sampler and
>    an integrating (boxcar) sampler at `fs` with a phase offset. Sweep the phase and report, for 1k/10k/100k S/s, the
>    worst single-capture error of the average current, and the worst-case peak reading.
> 2. Implement `quantize(i, lsb, fs)` and show the sleep-current error of a single 20 mA range at 8/12/16/20 bits.
> 3. Simulate one burst through shunt `R` and `C_dut` with the exact first-order solution per 1 µs step
>    (`V' = V∞ + (V − V∞)·e^{−dt/RC}`, shunt current `= I_load + C·ΔV/dt`), including an auto-ranging state machine with a
>    switch delay. Report minimum DUT voltage (brown-out?) and measured-vs-true charge.
> 4. Evaluate six candidate meters and print PASS/MARGINAL/FAIL with reasons, plus the required spec:
>    dynamic range, minimum sample rate, maximum fixed shunt, maximum range-switch delay.
>
> Example: the true average is 18.033 µA; a 12-bit single 20 mA range reads sleep as 0 → −11.08 %;
> a 100 Ω µA-range DMM drops V_DUT to 1.537 V during the 817 µs burst (brown-out).

## 3. 시작 코드

<!-- code: starter.py -->

DUT 프로파일과 후보 계측기 표 (Part C의 입력 — 시험 기준도 이 파일과 맞물려 있다):

<!-- code: profile_sensnode.json -->

<!-- code: instruments.json -->

```bash
cd lessons/HWLAB/HWLAB-M04
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 1/24 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: truth, point vs integrating samplers @ 1k/10k/100k S/s --
      (setup failed: NotImplementedError: average_current)
FAIL  average_current == sleep + dI*duty = 18.0329uA (1e-12 A): TypeError: unsupported operand type(s) for *: 'NoneType' and 'float'
FAIL  sample_point: 1 S/s on [(0,1e6,5)] -> [5.0]; 4 S/s phase 0 on a 2-step -> [1,1,3,3]: NotImplementedError: sample_point
...
FAIL  DMM uA range (100 ohm) on 0.817ms burst: Vmin == analytic RC 1.5366V (1uV) -> BROWNOUT < 1.8V: Vmin=2.9998V
      auto-range t_switch   20us: Vmin 2.9980V  over-range  817us  charge err  +0.000%  switches []
FAIL  t_switch=20us sag ~= 50mV + I*(t_sw+1us)/C within 10%: sim 2.0mV, linear 193.2mV
PASS  fast switch (1us/20us): charge conserved within 0.1% (decap redistributes, nothing clipped for long)   [+0.0000% / +0.0000%]
...
1/24 passed
exit=1
```

> 눈여겨볼 것: starter의 `simulate_event` 는 burden이 0인 **이상적인 계측기** 다(V가 안 움직이고, `quantize` 도 무한 분해능). 그래서 유일하게 PASS하는 테스트가 "전하 보존" 이다 — 이상적인 계측기는 당연히 전하를 보존한다. 나머지 23개가 전부 FAIL인 이유가 곧 이 레슨의 내용이다: **현실 계측기는 이상적이지 않다.**

## 4. 힌트

<details>
<summary>힌트 1 — 점 샘플을 샘플 수만큼 돌지 말고 세그먼트 수만큼</summary>

샘플 시각은 `k·Ts` (rotate로 위상을 0으로 옮긴 뒤). 세그먼트 `[a, b)` 에 떨어지는 샘플 수는 `ceil(b/Ts) − ceil(a/Ts)`. `_idx()` 가 부동소수 오차를 흡수한다. 모든 세그먼트의 개수를 더하면 telescoping으로 정확히 N이 된다. `out.extend([I] * n)` 는 C 속도라 1 MS/s × 1 s도 순식간이다.

</details>

<details>
<summary>힌트 2 — 적분형 샘플은 "통째로 든 칸" 과 "걸친 칸" 으로 나눈다</summary>

세그먼트 안에 통째로 든 칸의 평균은 그 세그먼트 값 I 그대로다. 경계에 걸친 칸만 `Q(끝) − Q(시작)` 으로 적분한다(`charge_fn` 이 누적 전하 Q(t) 를 준다). 칸 번호 k를 앞에서부터 하나씩만 전진시키면 각 칸이 정확히 한 번씩 나온다.

</details>

<details>
<summary>힌트 3 — RC 한 스텝은 오일러가 아니라 정확해로</summary>

스텝 동안 R과 I_load가 일정하면 V는 `V∞ = V_src − I_load·R` 를 향해 시정수 `RC` 로 지수 접근한다. 최고 레인지(0.05 Ω × 2.2 µF = 0.11 µs)는 dt = 1 µs보다 시정수가 훨씬 짧아서 오일러(`V += dt·dV/dt`)를 쓰면 발산한다. `exp(−dt/RC)` 를 쓰면 어떤 R에서도 안정하다.

</details>

<details>
<summary>힌트 4 — 션트 전류는 I_load가 아니다</summary>

계측기가 보는 것은 션트를 지난 전하다. 한 스텝 동안 디캡이 `C·(V − V')` 만큼 내놓으면 션트는 그만큼 덜 흐른다: `i_sh = I_load + C·(V' − V)/dt`. 이 식이 **전하 보존** 을 보장한다 — 버스트 창 전체에서 V가 처음 값으로 돌아오면 Σ i_sh = Σ I_load.

</details>

<details>
<summary>힌트 5 — 필요 fs 를 "±1 샘플" 로 잡는 이유</summary>

점 샘플은 폭 w인 버스트 안에 ⌊w·fs⌋ 또는 ⌈w·fs⌉ 개의 샘플을 떨어뜨린다. 그래서 버스트마다 전하 오차가 최대 `ΔI·Ts`. 같은 RTC 주기(250 ms = 8192 틱)로 깨는 틱들은 계측기 클럭에 대해 **위상이 고정** 이라 오차가 같은 방향으로 쌓인다 → 최악은 `Σ ΔI·Ts ≤ e·Q_frame`.

</details>

## 5. 풀이

### Part A — 평균은 전하다: 점 샘플은 위상 복권, 적분형은 전하 계수기

**진짜 값.** 두 레벨 프로파일이면 `I_avg = I_sleep + (I_burst − I_sleep)·D`, D = Σw/T = 1069 µs / 1 s. → **18.033 µA**, 220 mAh로 12 200 h = **1.39 년**. 여기서 sleep 몫은 2 µA·(1 − D)/18.033 µA = **11.08 %** — 이 숫자가 뒤에서 "sleep을 0으로 읽는 계측기" 의 오차로 그대로 나온다. 버스트가 짧고 드물수록 sleep 몫이 커진다(웨어러블·태그는 50 % 이상도 흔하다). 즉 **배터리 수명은 15 mA가 아니라 2 µA를 얼마나 정확히 재느냐로 결정되는 경우가 많다.**

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">10 kS/s (Ts = 100 µs) 로 63 µs 틱을 볼 때: 점 샘플 vs 적분형</text>
  <line x1="40" y1="120" x2="740" y2="120" stroke="currentColor"/>
  <g stroke="currentColor" opacity=".35" stroke-dasharray="3 3">
    <line x1="100" y1="40" x2="100" y2="250"/><line x1="250" y1="40" x2="250" y2="250"/><line x1="400" y1="40" x2="400" y2="250"/>
    <line x1="550" y1="40" x2="550" y2="250"/><line x1="700" y1="40" x2="700" y2="250"/></g>
  <rect x="280" y="50" width="95" height="70" class="f-acc" stroke="currentColor"/>
  <text x="327" y="44" text-anchor="middle" fill="currentColor" font-size="12">15 mA × 63 µs</text>
  <g fill="currentColor" font-size="12"><text x="44" y="138">점 샘플 (샘플 시각 = 점선)</text></g>
  <g class="f-warn" stroke="currentColor"><circle cx="100" cy="120" r="5"/><circle cx="250" cy="120" r="5"/><circle cx="400" cy="120" r="5"/><circle cx="550" cy="120" r="5"/><circle cx="700" cy="120" r="5"/></g>
  <text x="400" y="102" text-anchor="middle" fill="currentColor" font-size="12">이 위상에선 틱을 0번 맞힘 → 전하 0 (진짜 0.945 µC)</text>
  <text x="400" y="160" text-anchor="middle" fill="currentColor" font-size="12">위상을 40 µs 옮기면 1번 맞힘 → 15 mA × 100 µs = 1.5 µC (+59 %) · 확률 63 % vs 37 %</text>
  <line x1="40" y1="240" x2="740" y2="240" stroke="currentColor"/>
  <rect x="250" y="203" width="150" height="37" class="f-ok" stroke="currentColor"/>
  <text x="325" y="196" text-anchor="middle" fill="currentColor" font-size="12">칸 평균 = 0.945 µC / 100 µs = 9.45 mA</text>
  <text x="44" y="232" fill="currentColor" font-size="12">적분형</text>
  <text x="560" y="214" fill="currentColor" font-size="12">전하는 정확 (어느 위상이든)</text>
  <text x="560" y="232" fill="currentColor" font-size="12">피크는 15 → 9.45 mA 로 뭉개짐</text>
</svg>
```

`python3 -I solution.py` Part A 실제 출력과 `demo_sweep.py` 표 1 (이상적 ADC, 진짜 파형):

| fs | 점: 위상 평균 오차 | 점: 최악 1회 캡처 | 점: 최악 피크 | 적분: 최악 1회 캡처 | 적분: 최악 피크 |
|---|---|---|---|---|---|
| 1 kS/s | −0.75 % | **+326.94 %** | 0.002 mA (전부 놓침) | −0.00 % | 6.586 mA |
| 2 kS/s | +0.91 % | +160.60 % | 15.000 mA | — | 12.420 mA |
| 5 kS/s | +0.42 % | +60.80 % | 15.000 mA | — | 15.000 mA |
| 10 kS/s | +0.00 % | −22.37 % | 15.000 mA | +0.00 % | 15.000 mA |
| 31.75 kS/s | −0.00 % | −2.46 % | 15.000 mA | — | 15.000 mA |
| 100 kS/s | +0.00 % | +2.58 % | 15.000 mA | +0.00 % | 15.000 mA |
| 416 kS/s | +0.00 % | +0.66 % | 15.000 mA | — | 15.000 mA |
| 1 MS/s | −0.00 % | −0.00 % | 15.000 mA | — | 15.000 mA |

(— 는 Part A 테스트에서 돌리지 않은 칸; demo 표 1은 적분형 피크만 찍는다.)

읽는 법:

1. **점 샘플은 위상에 대해 불편(unbiased)이다.** 위상 φ를 [0, Ts)에서 균일하게 평균하면 `E_φ[I(φ + kTs)]` 가 정확히 `(1/Ts)∫I dt` 가 되기 때문이다(1 kS/s의 −0.75 %는 위상을 100개로만 나눈 이산화 잔차). 하지만 **캡처는 한 번에 한 위상** 이다. 1 kS/s에서 틱 4개가 같은 위상(250 ms = 8192 RTC 틱이 1 ms의 정수배)이라 "다 맞히거나 다 놓친다": 5샘플 × 15 mA → 76.99 µA(+327 %), 또는 버스트 전부 놓쳐 피크가 sleep 2 µA.
2. **적분형(boxcar)은 전하 계수기다.** 칸들의 합 = 프레임 전하이므로 어떤 fs·위상에서도 평균이 정확하다. 대가는 **피크**: w < Ts면 최악 피크 ≈ ΔI·w/(2Ts) (817 µs 버스트가 1 ms 칸 두 개에 반씩 걸칠 때 ~6.1 mA; 이산 위상 10개 중 최악은 6.586 mA). brown-out·LDO·배터리 펄스 부하를 판단하려면 피크가 필요하다.
3. **±1 샘플 한계.** 점 샘플의 버스트당 오차는 최대 ΔI·Ts. 위상 고정 최악은 버스트 5개가 같은 방향으로 쌓일 때 `ΣΔI·Ts`. 1 % 목표면 `Ts ≤ 0.01·18.033 A·µs / (5·14.998 mA) = 2.40 µs` → **fs ≥ 415.9 kS/s** (`required_spec`). 실제로 416 kS/s 최악 +0.66 %, 100 kS/s +2.58 % — 상한식이 보수적임을 확인. 31.75 kS/s(= 2/w_min, 적분형 피크 조건)에서도 점 샘플 최악은 −2.46 %.

**"Nyquist로 따지면?"** 사각 버스트 열의 스펙트럼은 sinc 꼴로 끝없이 이어진다 — 대역 제한 신호가 아니므로 Nyquist 조건을 만족하는 fs는 없다. 그래서 질문을 바꿔야 한다: "평균 전류만 필요한가?"(→ 적분형/안티에일리어싱 필터 + 아무 fs), "버스트 모양·피크가 필요한가?"(→ 가장 짧은 사건에 샘플이 충분히 들어가는 fs). 측정 대상 정의가 먼저다.

### Part B — 다이내믹 레인지, burden, 그리고 자동 레인지의 대가

**ADC 분해능 (단일 레인지 FS 20 mA, 실제 출력).**

| 비트 | LSB | sleep 판독 | 평균 오차 |
|---|---|---|---|
| 8 | 78 125 nA | 0 µA (−100 %) | −11.079 % |
| 12 | 4 882.8 nA | 0 µA (−100 %) | −11.079 % |
| 16 | 305.2 nA | 2.1362 µA (+6.81 %) | +0.755 % |
| 20 | 19.07 nA | 2.0027 µA (+0.14 %) | +0.015 % |

필요 다이내믹 레인지는 `I_peak / (e·I_sleep) = 15 mA / (1 % · 2 µA) = 750 000 : 1 = 117.5 dB → 20비트` (LSB ≤ 1 %·I_sleep 기준). 그런데 실제 장비의 20비트 단일 레인지는 **비트 수보다 오프셋·잡음이 먼저 막는다**: 오프셋을 "FS의 0.05 %" 로 잡으면 20 mA 레인지에서 10 µA = sleep의 5배다. 그래서 sleep은 작은 FS(큰 션트) 레인지로, 버스트는 큰 FS(작은 션트) 레인지로 재는 **자동 레인지** 가 표준이 된다. 12비트 ADC로 1 % 분해능을 2 µA–15 mA 전 구간에서 유지하려면 레인지 3개면 된다(연습 4: 한 레인지가 덮는 폭 = e·2^bits = 40.96배). 이 레슨의 AR 장비는 FS 50 µA / 500 µA / 5 mA / 50 mA / 1 A의 10배 간격 5개(모두 FS에서 burden 50 mV)를 쓴다.

```svg
<svg viewBox="0 0 760 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">전류 축 (로그): 12-bit 자동 레인지 5개가 2 µA – 15 mA 를 1 % 분해능으로 덮는다</text>
  <line x1="40" y1="160" x2="740" y2="160" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="40" y="180">10 nA</text><text x="140" y="180">100 nA</text><text x="240" y="180">1 µA</text><text x="340" y="180">10 µA</text>
    <text x="440" y="180">100 µA</text><text x="540" y="180">1 mA</text><text x="640" y="180">10 mA</text><text x="740" y="180">100 mA</text></g>
  <g stroke="currentColor"><line x1="40" y1="155" x2="40" y2="165"/><line x1="140" y1="155" x2="140" y2="165"/><line x1="240" y1="155" x2="240" y2="165"/>
    <line x1="340" y1="155" x2="340" y2="165"/><line x1="440" y1="155" x2="440" y2="165"/><line x1="540" y1="155" x2="540" y2="165"/>
    <line x1="640" y1="155" x2="640" y2="165"/></g>
  <g class="f-acc" stroke="currentColor" opacity=".8">
    <rect x="249" y="40" width="161" height="16"/><rect x="349" y="62" width="161" height="16"/><rect x="449" y="84" width="161" height="16"/>
    <rect x="549" y="106" width="161" height="16"/></g>
  <g fill="currentColor" font-size="12"><text x="415" y="53">r0: 1 kΩ, FS 50 µA, LSB 12 nA</text><text x="515" y="75">r1: 100 Ω, 500 µA</text>
    <text x="615" y="97">r2: 10 Ω, 5 mA</text><text x="300" y="119">r3: 1 Ω, 50 mA (r4 1 A 는 축 밖) →</text></g>
  <g class="f-warn" stroke="currentColor"><rect x="268" y="128" width="4" height="32"/><rect x="656" y="128" width="4" height="32"/></g>
  <g fill="currentColor" font-size="12"><text x="200" y="140">sleep 2 µA</text><text x="600" y="140">15 mA</text></g>
  <text x="400" y="202" text-anchor="middle" fill="currentColor" font-size="12">막대 = LSB ≤ 1 %·I 인 구간 (FS/40.96 … FS) · 단일 20 mA 12-bit 레인지는 488 µA 미만에서 1 % 불가</text>
</svg>
```

**Burden 전압.** 직렬 전류계는 션트에서 `V_burden = I·R` 만큼 DUT 전압을 빼앗는다. 핸드헬드 DMM µA 레인지(대표값 100 µV/µA = 100 Ω)에 15 mA면 정상 상태 1.5 V 강하 → V_dut 1.5 V. 디캡이 짧은 사건은 가려 준다: RC 해 `V(t) = V∞ + (V0 − V∞)·e^{−t/RC}` 에서 RC = 220 µs, 63 µs 틱은 0.374 V만 처지지만 817 µs 버스트는 **1.5366 V (BOR 1.8 V 미만 → brown-out)**. 시뮬레이터가 해석해와 1 µV 이내로 일치한다(테스트). 게다가 션트 전류가 FS 6 mA를 넘어 901 µs 동안 포화 → 그 버스트 전하의 57.9 %만 읽힌다. `demo_sweep.py` 표 3 — DMM µA 레인지가 버티려면 디캡이 얼마나 필요한가:

| C_dut | 0.1 µF | 1 µF | 2.2 µF | 4.7 µF | 7.44 µF | 10 µF | 22 µF |
|---|---|---|---|---|---|---|---|
| Vmin | 1.5000 V | 1.5004 V | 1.5366 V | 1.7637 V | 2.0002 V | 2.1625 V | 2.5346 V |
| 판정 | BROWNOUT | BROWNOUT | BROWNOUT | BROWNOUT | ok | ok | ok |

해석 경계: `1.5·(1 − e^{−817 µs/(100 Ω·C)}) ≤ 1.0 V` → C ≥ 7.44 µF. 교훈: **"DMM으로 잘 쟀다" 는 그 보드의 디캡이 우연히 컸다는 뜻일 수 있다.** 같은 펌웨어라도 디캡이 작은 rev에서는 측정 셋업이 DUT를 리셋시킨다 — 측정 중 리셋 루프는 "펌웨어 버그" 처럼 보인다.

**자동 레인지 전환 지연.** 긴 sleep 뒤 계측기는 r0(1 kΩ)에 있다. 버스트가 시작되면:

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">자동 레인지 이벤트 (t_switch = 20 µs, C_dut 2.2 µF) — 시뮬레이터가 하는 일</text>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="88" y="64">I_load</text><text x="88" y="134">i_shunt</text><text x="88" y="204">V_dut</text><text x="88" y="268">레인지</text></g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="95,75 140,75 140,48 740,48"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="95,148 140,148 175,140 260,128 262,82 266,118 300,118 740,118"/>
  <text x="270" y="78" fill="currentColor" font-size="12">재충전 스파이크 C·ΔV/1µs ≈ 435 mA (모델)</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="95,190 140,190 175,200 260,226 264,192 740,192"/>
  <g stroke="currentColor" stroke-dasharray="4 3" opacity=".6"><line x1="95" y1="236" x2="740" y2="236"/></g>
  <text x="520" y="232" fill="currentColor" font-size="12">2.0 V = BOR + margin (축척 임의)</text>
  <text x="270" y="232" fill="currentColor" font-size="12">sag 191.7 mV → Vmin 2.808 V</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="95,278 262,278 262,250 300,250 300,258 560,258 560,264 600,264 600,270 640,270 640,278 740,278"/>
  <g fill="currentColor" font-size="12"><text x="100" y="292">r0 1 kΩ</text><text x="268" y="246">r4 (top)</text><text x="380" y="254">r3 1 Ω (15 mA 동안)</text><text x="590" y="292">r2→r1→r0 (t_hold 마다)</text></g>
  <g stroke="currentColor"><line x1="140" y1="30" x2="140" y2="296"/><line x1="175" y1="30" x2="175" y2="296" stroke-dasharray="2 3"/><line x1="262" y1="30" x2="262" y2="296"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="140" y="38">버스트 시작</text><text x="190" y="38">감지(~7 µs)</text><text x="262" y="38">+t_switch</text></g>
  <text x="300" y="100" fill="currentColor" font-size="12">r0 에선 i_shunt = ΔV/1 kΩ — 디캡이 15 mA 를 대신 낸다</text>
</svg>
```

1. r0에서는 션트 전류가 `ΔV/1 kΩ` 뿐이라 15 mA는 **디캡이 낸다** → V_dut이 `I/C = 6.8 mV/µs` 로 처진다.
2. 션트 전류가 FS(50 µA)를 넘는 순간 = ΔV가 `FS·R0 = 50 mV` 에 닿는 순간(≈ 7.3 µs) 과전류 감지.
3. `t_switch` 동안 r0 유지 → 추가로 `I·t_sw/C` 만큼 처짐. 선형 근사 `sag ≈ 50 mV + I·(t_sw + 1 µs)/C` (RC0 = 2.2 ms ≫ t_sw일 때 유효; 1 µs는 감지 스텝).
4. 최고 레인지(0.05 Ω)로 바뀌면 디캡이 거의 한 스텝 만에 다시 차며 **재충전 스파이크** `C·ΔV/dt` 가 흐른다. 그 뒤 t_hold마다 한 단계씩 내려온다.

`demo_sweep.py` 표 2 (실제 출력):

| t_switch | Vmin | sag (sim) | sag (선형식) | 과전류 시간 | 스파이크 | 전하 오차 |
|---|---|---|---|---|---|---|
| 1 µs | 2.9368 V | 63.2 mV | 63.6 mV | 2 µs | 152.3 mA | −0.074 % |
| 5 µs | 2.9096 V | 90.4 mV | 90.9 mV | 6 µs | 212.2 mA | −0.074 % |
| 20 µs | 2.8083 V | 191.7 mV | 193.2 mV | 21 µs | 435.1 mA | −0.085 % |
| 50 µs | 2.6078 V | 392.2 mV | 397.7 mV | 51 µs | 876.2 mA | −0.146 % |
| 100 µs | 2.2795 V | 720.5 mV | 738.6 mV | 102 µs | 1000.0 mA (포화) | −5.222 % |
| 139 µs | 2.0286 V | 971.4 mV | 1004.5 mV | 141 µs | 1000.0 mA (포화) | −9.967 % |
| 200 µs | **1.6450 V** | 1355.0 mV | 1420.5 mV | 202 µs | 1000.0 mA (포화) | −17.388 % |
| 300 µs | 1.0386 V | 1961.4 mV | 2102.3 mV | 302 µs | 1000.0 mA (포화) | −29.555 % |

핵심 두 가지:

- **빠른 전환에선 평균 전류가 거의 안 틀린다(−0.07 %).** 디캡이 빌려준 전하를 재충전 스파이크로 돌려받고, 계측기는 그 스파이크를 (포화 없이) 센다 — 전하 보존. 남는 −0.07 %는 r0 포화 몇 µs와 hold 동안 굵은 LSB 반올림이다. 그러니 전환 지연의 1차 피해는 평균이 아니라 **burden sag → brown-out** 이다.
- **느린 전환은 두 번 망가뜨린다.** sag가 커져 brown-out(200 µs → 1.645 V), 그리고 재충전 스파이크가 최고 레인지 FS(1 A)마저 넘어 **포화로 전하를 잃는다**(100 µs부터 −5 %, 200 µs −17 %). 상한식 `t_sw ≤ C·(headroom − 50 mV)/I = 139.3 µs` 에서 실제 Vmin 2.0286 V — 마진 경계와 맞는다(선형식은 RC 곡률을 무시해 약간 보수적).

> 모델의 한계 (정직하게): 스파이크 높이·폭은 "이상 전원 + 이상 스위치 + 1 µs 스텝" 가정에 좌우된다. 실제로는 배선 인덕턴스·소스 출력 임피던스·측정 앰프 대역폭이 스파이크를 수 µs로 퍼뜨리고, 상용 장비는 전환 순간에 소스 쪽을 보조하거나 전환 직후 샘플을 처리하는 기능을 둔다. 하지만 "**전하 C·ΔV가 짧은 시간에 몰려 흐른다**" 는 구조는 모델과 무관하고, 그게 Part C의 점 샘플러를 흔든다.

### Part C — 판정과 필요 사양

`python3 -I solution.py --report` 실제 출력:

```text
SENSNODE-L rev A VDD_MCU: I_true=18.033uA life=1.39y (220mAh) | REQ DR 750000:1 = 117.5dB (single range 20 bit), fs_point>=416kS/s or integrating fs>=31.7kS/s, R_fixed<=66.7ohm, t_switch<=139us
  DMM_uA    FAIL     avg  12.868uA (mean -28.64% worst -28.64%) sleep  2.000uA peak   0.07mA Vmin 1.537V life  1.95y  BROWNOUT Vmin=1.537V<1.8V; avg err -28.6%>1.0%; peak 0.07mA<14.25mA
  DMM_mA    FAIL     avg  16.035uA (mean -11.08% worst -11.08%) sleep  0.000uA peak   0.12mA Vmin 2.973V life  1.57y  sleep 0.000uA err -100.0%; avg err -11.1%>1.0%; peak 0.12mA<14.25mA
  SCOPE_10R FAIL     avg  16.034uA (mean -11.08% worst -11.20%) sleep  0.000uA peak  15.00mA Vmin 2.850V life  1.57y  sleep 0.000uA err -100.0%; avg err -11.1%>1.0%
  AR_SLOW   FAIL     avg  15.574uA (mean -13.64% worst -13.64%) sleep  2.002uA peak  14.89mA Vmin 1.645V life  1.61y  BROWNOUT Vmin=1.645V<1.8V; avg err -13.6%>1.0%
  AR_100K   MARGINAL avg  18.004uA (mean  -0.16% worst +83.03%) sleep  2.002uA peak  15.00mA Vmin 2.808V life  1.39y  1-capture worst avg err +83.03%>1.0%
  AR_1M     PASS     avg  18.033uA (mean  -0.00% worst  -0.00%) sleep  1.999uA peak  83.88mA Vmin 2.937V life  1.39y  -
PICK meter=AR_1M (Joulescope-class auto-range, 1 MS/s integrated samples): avg 18.033uA err -0.00% (worst -0.00%), Vmin 2.937V, life 1.39y
```

사양 대 요구 (요구는 `required_spec()`):

| 후보 | 결정적 사양 | 요구 | 결과 |
|---|---|---|---|
| DMM_uA | 고정 션트 100 Ω | R ≤ 66.7 Ω (정상상태 burden) | BROWNOUT 1.537 V, 포화로 −28.6 %, **수명 1.95 년으로 과대** |
| DMM_mA | LSB 10 µA, 10 S/s | LSB ≤ 20 nA, 피크 보려면 fs ≥ 31.7 kS/s | sleep 0 → −11.08 % (= sleep 몫 그대로), 피크 0.12 mA |
| SCOPE_10R | 8-bit, LSB 78 µA | LSB ≤ 20 nA | sleep 0 → −11.08 %. 버스트 파형은 잘 보인다 |
| AR_SLOW | t_switch 200 µs | ≤ 139 µs | BROWNOUT 1.645 V, 스파이크 포화 −13.6 % |
| AR_100K | 점 샘플 100 kS/s | 점이면 ≥ 416 kS/s (또는 적분형) | 위상 평균 −0.16 %, **1회 캡처 −13 … +83 %** |
| AR_1M | 적분형 1 MS/s, t_sw 1 µs | 적분형 ≥ 31.7 kS/s, ≤ 139 µs | PASS |

**AR_100K가 왜 Part A(+2.58 %)보다 훨씬 나쁜가?** `demo_sweep.py` 표 4 실제 출력이 답이다: 버스트 5개 모두 전환 직후 **435.1 mA × 1 µs** 재충전 스파이크가 있고(전하 C·ΔV = 2.2 µF × 0.192 V = 0.42 µC, 다섯 개면 프레임 전하의 11.7 %), 10 µs 간격 점 샘플이 그것을 맞히느냐에 따라:

```text
   phase  0.5us: avg   15.614uA  err  -13.41%  samples > 50mA: 0
   phase  5.5us: avg   33.005uA  err  +83.03%  samples > 50mA: 4
   phase  6.5us: avg   16.211uA  err  -10.11%  samples > 50mA: 0
   phase  9.5us: avg   20.482uA  err  +13.58%  samples > 50mA: 1
```

놓치면 디캡이 낸 전하(sag 동안 r0가 못 본 몫)가 통째로 빠져 −10 ~ −13 %, 맞히면 1 µs짜리를 10 µs로 늘려 세어 +13 ~ +83 %. 위상 평균은 −0.16 %로 맞다 — 그래서 MARGINAL: **DUT와 계측기 클럭이 드리프트해 위상이 섞이는 긴 캡처** 에서는 쓸 수 있다(연습 2: 37 ppm이면 100 프레임에 −22.37 % → +0.000 %, 0 ppm이면 100 프레임도 그대로 −22.37 %). 적분형 1 MS/s는 스파이크를 정확히 한 번 센다. 같은 이유로 AR_1M의 "피크 83.88 mA" 는 DUT 전류가 아니라 **전원이 실제로 낸 재충전 전류** 다 — 계측기는 디캡 너머의 I_load를 못 본다.

**리포트를 읽는 법 (하드웨어/펌웨어 팀에 넘길 문장):**

- `PICK AR_1M(적분형 1 MS/s, 전환 1 µs): VDD_MCU avg 18.03 µA (sleep 2.00 µA = 11 %), Vmin 2.94 V ≥ 2.0 V → 220 mAh 1.39 y (단순 모델, 보정은 RF-A04). DMM µA 레인지는 817 µs 버스트에서 V_DUT 1.54 V (BOR 1.8 V) — 그 셋업의 리셋은 펌웨어 버그 아님. 스코프+10 Ω/DMM mA 결과(16.0 µA, 1.57 y)는 sleep을 0으로 읽은 값이므로 폐기.`
- 측정 전 체크리스트(bring-up 현실): 디버거(SWD/JTAG) 분리 후 전원 재인가 — 디버그 도메인이 켜져 있으면 sleep이 데이터시트 값보다 훨씬 크게 나온다(MCU마다 다름). 플로팅 GPIO, 풀업 걸린 LED/버튼, 센서 인터럽트 핀 누설, 레벨 시프터 back-power(HWLAB-B02 참고)를 먼저 의심. 측정점은 레일 하나만(`VDD_MCU` 를 0 Ω/점퍼로 분리) — 보드 전체 전류는 PMIC 효율이 섞인다.

**장비가 있을 때 (이 레슨에선 실행하지 않음, 사양은 데이터시트 확인 필요):** PPK2(Nordic Power Profiler Kit II)는 nRF Connect for Desktop의 Power Profiler 앱으로 캡처하고, 소스 미터 모드(PPK2가 전원 공급)와 앰미터 모드(외부 전원과 직렬)가 있다 — 앰미터 모드에서는 burden이 이 레슨 모델 그대로 적용된다. Joulescope는 자체 UI와 Python 패키지로 스트림을 받는다. 고급 대안으로 끊김 없는 레인지 전환(seamless ranging)을 내세우는 SMU급 장비(예: Keysight N6781A류)와 Otii류가 있다. 어떤 장비든 확인할 것은 이 레슨의 다섯 가지다: **최저 레인지 분해능·오프셋, 샘플 방식(점/적분)과 fs, 레인지 전환 시간과 전환 시 동작, 각 레인지 burden, 공급 모드에서의 전압 보상 응답.** 스트림을 CSV로 받으면 `(t, i)` 를 1 µs 세그먼트로 바꿔 `sample_avg` 와 `average_current` 에 그대로 넣을 수 있다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"     # 24/24 passed, exit=0  (실행 0.75 s)
python3 -I solution.py --report             # Part C 판정 리포트만
python3 -I -W error -X dev solution.py      # 경고 없음 (exit 0)
python3 -I demo_sweep.py                    # §5 의 표 1–4 (실행 2.5 s)
```

<!-- code: demo_sweep.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/행동 |
|---|---|---|
| 1 kS/s 로그를 그냥 평균 | 점 샘플은 1회 캡처가 −100 … +327 % 복권 (틱이 RTC 위상 고정) | 적분형(평균) 다운샘플 모드, 또는 fs ≥ ΣΔI/(e·Q) |
| "Nyquist 2배면 충분" | 사각 버스트는 대역 제한이 아니다 | 측정 대상(평균/피크/모양)을 먼저 정하고 그에 맞는 방식 |
| 적분형 1 kS/s 의 max를 피크로 보고 | 817 µs × 15 mA가 6.6 mA로 뭉개짐 | 피크는 fs ≥ 2/w_min (여기 31.7 kS/s) |
| 단일 20 mA 레인지 12-bit로 sleep | LSB 4.88 µA > 2 µA → 0, 평균 −11.08 % | 자동 레인지 또는 sleep 전용 µA 레인지 |
| ADC 비트만 보고 장비 선택 | 오프셋(%FS)·잡음이 20비트를 무의미하게 만든다 | 최저 레인지의 오프셋·잡음 사양을 nA로 환산 |
| DMM µA 레인지를 직렬로 꽂고 리셋을 "펌웨어 버그" 로 | 100 Ω × 15 mA burden, 817 µs 버스트에 1.54 V | burden ≤ headroom/I_peak (66.7 Ω), 아니면 바이패스 스위치 |
| 디캡을 빼고 brown-out 판단 | 디캡이 짧은 사건을 가린다(63 µs 틱은 0.37 V만) | RC 해 `I·R(1 − e^{−t/RC})` 로 가장 긴 버스트까지 |
| 오일러로 RC 적분 | 0.05 Ω × 2.2 µF = 0.11 µs ≪ dt → 발산 | `V∞ + (V − V∞)·exp(−dt/RC)` |
| 션트 전류 = I_load 로 가정 | 디캡이 낸 전하만큼 션트는 덜/나중에 흐른다 | `i_sh = I_load + C·ΔV/dt` (전하 보존) |
| 자동 레인지 계측기면 다 괜찮다 | 전환 지연 → sag, 재충전 스파이크 포화, 점 샘플 aliasing | t_sw ≤ C·(headroom − V_det)/I, 적분형 샘플 |
| 짧은 캡처 한 번으로 보고 | 위상 고정이면 같은 오차가 반복된다 | 여러 캡처의 분산 확인, 드리프트 있으면 긴 캡처 |
| 측정 중 디버거 연결 | 디버그 도메인 전류로 sleep 과대 | 디버거 분리 후 전원 재인가하고 측정 |

## 8. 면접 Q&A

<details>
<summary>Q1. sleep 2 µA, active 15 mA인 보드의 평균 전류를 DMM으로 재면 어떻게 되나요?</summary>

µA 레인지는 분해능이 충분하지만 션트가 크다(대표값 100 µV/µA ≈ 100 Ω). 15 mA 버스트에서 정상 상태 1.5 V burden, 디캡 2.2 µF로도 817 µs 버스트 끝에 V_DUT 1.54 V → BOR 1.8 V 아래, 리셋. 측정 자체도 FS를 넘어 포화(−28.6 %). mA 레인지는 burden이 작지만(1.8 Ω) 분해능 10 µA라 sleep을 0으로 읽어 −11 % — 그 값은 정확히 "sleep이 예산에서 차지하는 몫" 이다. 둘 다 피크는 못 본다.
꼬리질문: "DMM은 적분형이니 평균은 맞지 않나?" → 적분형은 레인지 안에 있을 때만 맞는다. µA 레인지는 포화되고 DUT를 리셋시키며, mA 레인지는 적분된 평균 18 µA 자체가 LSB 10 µA 몇 개라 분해능이 안 된다. 실전 우회책은 µA 레인지 + 바이패스 스위치(부팅·활성 구간엔 단락, sleep 정착 후 개방) — 그러면 sleep만 정확히 재고 활성 전하는 따로 잰다.

</details>

<details>
<summary>Q2. 이 전류를 재려면 샘플링률이 얼마나 필요하죠?</summary>

질문을 둘로 나눈다. 평균(=배터리 수명)만이면 **적분형** 샘플(각 샘플이 그 구간 전하)은 fs와 무관하게 정확하다. 점 샘플이면 버스트마다 ±1 샘플(±ΔI·Ts) 오차가 나고, RTC로 깨는 사건들은 계측기 클럭에 위상 고정이라 최악이 쌓인다: `fs ≥ ΣΔI/(e·Q_frame)` = 1 %에 415.9 kS/s. 피크·버스트 모양이 필요하면 가장 짧은 사건(63 µs)에 샘플이 충분히 들어가야 한다(적분형 피크 보존 ≥ 2/w = 31.7 kS/s).
꼬리질문: "Nyquist로는?" → 사각 펄스는 대역 제한 신호가 아니다. 그래서 안티에일리어싱(=적분/평균)이 먼저고 fs는 그다음이다. 점 샘플 1 kS/s는 위상 평균으로는 −0.75 %지만 1회 캡처는 +327 %까지 간다.

</details>

<details>
<summary>Q3. Burden voltage가 뭐고 왜 문제가 되나요? DUT에 DC/DC가 있으면 달라지나요?</summary>

직렬 전류계 션트(와 배선·퓨즈)에서 생기는 전압 강하 `I·R` 이다. DUT 공급 전압을 깎아 (1) BOR/UVLO 아래로 떨어뜨리고(리셋, 무선 송신 실패), (2) 측정 대상 자체를 바꾼다. 고정 레인지 허용치는 `R ≤ (V_src − V_BOR − margin)/I_peak` = 66.7 Ω, 디캡이 있으면 짧은 사건은 `I·R·(1 − e^{−t/RC})` 로 완화된다.
꼬리질문: "DC/DC면?" → DC/DC 뒤 부하는 대략 **정전력** 이라 입력 전압이 떨어지면 입력 전류가 **늘어난다**: `I = (V − √(V² − 4RP))/(2R)`, 45 mW·10 Ω에서 15 mA → 15.84 mA(+5.6 %), 100 Ω이면 해가 없어 전압 붕괴(연습 3). LDO/선형 부하는 전류가 거의 일정하다. 그래서 burden 큰 측정은 DC/DC 제품의 전류를 과대평가한다.

</details>

<details>
<summary>Q4. 자동 레인지 계측기의 전환 지연은 측정을 어떻게 망치나요? 디캡이 전하를 보존해 준다면서요?</summary>

평균 전하는 빠른 전환이면 거의 보존된다(t_sw 20 µs에서 −0.085 %): r0에서 못 본 전류를 디캡이 대신 내고, 전환 직후 재충전 스파이크로 계측기를 지나 돌아오기 때문이다. 문제는 세 가지다. ① 감지(FS·R0 = 50 mV) + 전환 지연 동안 `I·t_sw/C` 만큼 처져 brown-out — 상한 `t_sw ≤ C·(headroom − 50 mV)/I` = 139 µs, 200 µs면 1.645 V. ② 재충전 스파이크 `C·ΔV/짧은 시간` 이 최고 레인지 FS마저 넘으면 포화로 전하 손실(100 µs부터 −5 %). ③ 점 샘플러가 그 짧은 스파이크를 맞히느냐에 따라 1회 캡처가 −13 … +83 % (AR_100K).
꼬리질문: "실제 장비는 어떻게 피하나?" → 매우 빠른 전환(µs 이하), 전환 순간 소스 측 보조, 전환 직후 샘플 처리 옵션, 적분형 다운샘플. 장비마다 방식이 다르니 데이터시트·매뉴얼에서 "range switching" 동작을 확인하고, 이 레슨처럼 알려진 부하(저항 + 펄스 부하 지그)로 직접 검증한다.

</details>

<details>
<summary>Q5. 12-bit ADC로 LSB보다 작은 2 µA를 평균으로 읽을 수 있나요?</summary>

잡음이 없으면 절대 못 읽는다 — 2 µA < LSB/2(2.44 µA)는 매번 0으로 양자화되고 0의 평균은 0이다. 잡음이 0.5 LSB rms 이상 있으면(dither) 양자화 오차가 신호와 무관해져 평균이 수렴한다: 양방향 ADC + 200k 샘플에서 1.9954 µA(−0.23 %). 단, **단방향 ADC는 0 아래 잡음을 잘라** 평균을 위로 민다: 2.1603 µA(+8 %, 이론 E[Q(max(X,0))] = 2.162 µA와 일치, 연습 1).
꼬리질문: "그래서 장비는 어떻게?" → 작은 전류는 작은 FS 레인지로 LSB 자체를 줄이는 것이 정공법이고, 오프셋 보정(무부하 zero)·양방향 측정으로 0 근처 편향을 없앤다.

</details>

<details>
<summary>Q6. 1 s 캡처를 할 때마다 평균이 다르게 나옵니다. 길게 찍으면 해결되나요?</summary>

점 샘플 위상 오차라면 **DUT 클럭과 계측기 클럭이 드리프트할 때만** 길게 찍는 게 도움이 된다. 37 ppm이면 프레임마다 위상이 37 µs씩 밀려 10 kS/s(100 µs 격자)를 100 프레임에 고르게 훑고 −22.37 % → +0.000 %. 둘이 같은 기준 클럭을 공유하거나 우연히 정수비면 100 프레임도 1 프레임과 같은 오차(연습 2). 적분형으로 바꾸면 위상 문제 자체가 사라진다.
꼬리질문: "드리프트가 있는지 어떻게 아나?" → 캡처 사이 버스트 시각의 미세 이동(샘플 위치 대비)을 보거나, 같은 조건 반복 캡처의 분산을 본다. 분산이 크면 샘플 방식부터 의심한다.

</details>

<details>
<summary>Q7. 측정 평균 18.03 µA, 220 mAh → 1.39년. 이 수명 숫자를 그대로 보고해도 되나요?</summary>

측정 오차는 이제 1 % 이하지만 모델 오차가 더 크다. 코인셀은 15 mA 펄스에서 내부저항(수~수십 Ω, 온도·잔량에 따라 증가)으로 전압이 처지고 유효 용량이 공칭보다 줄어든다. 저온, 자가 방전, 수명 말기 전압에서의 BOR 마진(이 레슨의 brown-out 계산을 배터리 내부저항으로 다시 해야 한다)도 있다. 그래서 "측정 평균 + 펄스 피크 + 최저 전압" 을 함께 넘기고 용량 보정은 배터리 모델에서 한다(see:RF-A04).
꼬리질문: "피크 전류 값은 왜 같이 보고하나?" → 배터리 내부저항 × 피크 = 전압 강하라서, 평균이 같아도 피크가 크면 수명 말기에 BOR을 먼저 맞는다. 그래서 적분형만 쓰더라도 피크가 보이는 fs(≥ 2/w_min)가 필요하다.

</details>

<details>
<summary>Q8. 계측기가 보여 준 피크가 84 mA입니다. MCU가 84 mA를 먹나요?</summary>

아니다. 계측기는 **션트 전류**(=전원이 낸 전류)를 보고 디캡 너머의 I_load는 못 본다. AR_1M의 83.88 mA는 레인지 전환 직후 디캡 재충전 전류다. 반대로 r0에 있던 첫 수 µs 동안은 MCU가 15 mA를 먹는데 계측기는 수십 µA만 본다. 버스트 모양을 해석할 때는 "디캡이 시간축에서 전하를 옮긴다" 를 늘 염두에 둔다.
꼬리질문: "그럼 디캡을 빼고 재면?" → 실제 제품 조건이 아니게 되고 DUT가 불안정해질 수 있다. 대신 디캡을 포함한 상태에서 sag·스파이크를 이렇게 모델로 설명하거나, 디캡 이후 노드의 전류를 보고 싶으면 그 경로에 별도 저-burden 센스(예: 작은 션트 + 차동 앰프)를 둔다.

</details>

## 9. 추가 연습

### 연습 1 — dither와 단방향 ADC

12-bit / 20 mA(LSB 4.883 µA)에서 sleep 2 µA에 가우스 잡음 σ를 더해 양자화한 평균 `dithered_mean`. σ = 0 → 0, σ = 0.5 LSB + 양방향 → 2 % 이내, σ = 0.5 LSB + 단방향(`quantize` 그대로) → 위로 편향, 이론값 `Σ k·LSB·P(X ∈ [(k−½)LSB, (k+½)LSB))` 과 3 % 이내.

<details>
<summary>답 보기</summary>

실제 출력: no noise 0.0000 µA | 0.5 LSB rms bipolar 1.9954 µA | unipolar 2.1603 µA (이론 2.162 µA, +8 %). dither의 조건은 "잡음이 양자화 격자보다 넓고, 양자화기가 잡음의 양쪽을 다 본다" 이다. 단방향 측정은 0 근처에서 그 조건을 깨므로, nA급 측정에서는 오프셋을 줘서 양방향으로 보거나 작은 레인지로 LSB를 줄인다. 처음엔 E[max(X,0)] = 2.284 µA로 비교했다가 틀렸다 — 절단 뒤 다시 양자화되므로 기대값도 양자화된 분포로 계산해야 한다.

</details>

### 연습 2 — 캡처 길이와 클럭 드리프트

10 kS/s 점 샘플, 최악 위상에서 시작해 DUT 클럭이 37 ppm 빠를 때 프레임마다 위상이 1 s × 37 ppm = 37 µs씩 밀린다. 1/10/100 프레임 평균 오차, 그리고 0 ppm 100 프레임.

<details>
<summary>답 보기</summary>

실제 출력: worst phase 0.5 µs: 1 frame −22.37 % | 10 frames −0.75 % | 100 frames +0.000 % (37 ppm) | 100 frames, 0 ppm −22.37 %. 37과 100이 서로소라 100 프레임이면 1 µs 격자 위상 100개를 한 번씩 다 지나 정확히 위상 평균이 된다. 긴 캡처가 "자동으로" 좋아지는 게 아니라 위상이 섞일 때만 좋아진다 — 같은 기준 클럭을 공유하는 테스트 장비(예: 10 MHz 레퍼런스 공유)에서는 오히려 고정 오차가 된다.

</details>

### 연습 3 — 정전력(DC/DC) 부하와 burden

`cp_current(P, V_src, R)`: P = 45 mW(3 V × 15 mA)가 R 뒤에 있을 때 `R·I² − V·I + P = 0` 의 작은 근, 판별식 < 0이면 None. R = 0.05/1/10/100 Ω.

<details>
<summary>답 보기</summary>

실제 출력: 0.05 Ω 15.0038 mA(+0.03 %), 1 Ω 15.0758 mA(+0.51 %), 10 Ω 15.8359 mA(+5.57 %), 100 Ω 해 없음(붕괴). 작은 근을 고르는 이유: 큰 근은 V_DUT < V_src/2인 불안정 동작점이다. 실제 DC/DC는 UVLO에서 꺼지므로 "해 없음" 은 brown-out/리셋 루프로 나타난다. 측정 셋업이 측정량을 바꾼다는 가장 직접적인 예다.

</details>

### 연습 4 — ADC 비트 수와 레인지 개수

`ranges_needed(I_min, I_max, bits, e)` = ceil(log(I_max/I_min)/log(e·2^bits)), 그리고 레인지 표가 [2 µA, 15 mA]를 "FS ≥ I, LSB ≤ e·I" 로 빈틈없이 덮는지 `covers()`.

<details>
<summary>답 보기</summary>

실제 출력: {8: 10, 12: 3, 16: 2, 20: 1}. AR_100K(10배 간격 5개, 12-bit) covers = True, SCOPE_10R(단일, 8-bit) False(첫 빈틈 2 µA), 같은 10배 간격 5개를 8-bit로 바꾸면 False(한 레인지 폭 2.56배 < 10배 간격). 레인지 간격은 "e·2^bits" 보다 좁아야 하고, 히스테리시스(down_frac 0.9)를 둘 여유도 필요하다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 9/9 passed, exit=0 (0.3 s)
```

## 10. 핵심 정리

- 배터리 수명은 **평균 전하** 로 정해지고, 드문 버스트 제품에선 sleep 몫(여기 11 %)이 크다 — sleep을 0으로 읽는 장비는 그만큼 틀린다.
- 평균이 목적이면 **적분형** 샘플(어떤 fs든 정확), 점 샘플은 위상 평균만 맞고 1회 캡처는 `ΣΔI·Ts` 까지 틀린다 → fs ≥ ΣΔI/(e·Q). 피크는 fs ≥ 2/w_min.
- 다이내믹 레인지 = I_peak/(e·I_sleep) = 750 000 : 1 = 117.5 dB → 단일 레인지 20비트(오프셋 때문에 비현실적) → 자동 레인지(12-bit면 3개).
- Burden = I·R. 고정 션트 허용치 `(V_src − V_BOR − margin)/I_peak` = 66.7 Ω. 디캡은 짧은 사건만 가린다: `I·R·(1 − e^{−t/RC})`.
- 자동 레인지 전환 지연의 1차 피해는 **sag → brown-out**: `sag ≈ FS0·R0 + I·t_sw/C`, t_sw ≤ 139 µs. 느리면 재충전 스파이크가 포화돼 전하도 잃는다.
- 계측기는 **션트 전류** 를 본다. 디캡이 전하를 시간축에서 옮겨 스파이크(84 mA)와 빈 구간을 만든다 — 점 샘플은 그걸 aliasing한다.
- 리포트에는 장비·모드·fs·레인지·캡처 길이·Vmin을 함께: "AR_1M 적분 1 MS/s, avg 18.03 µA, Vmin 2.94 V, 1.39 y(단순 모델)".

## 참고자료

- Keithley (Tektronix), *Low Level Measurements Handbook* — 전류계 burden voltage, 션트 vs 피드백 전류계, 오프셋·잡음
- Nordic Semiconductor, *Power Profiler Kit II* 사용자 가이드 / nRF Connect for Desktop Power Profiler 문서 — 소스 미터·앰미터 모드, 자동 레인지 (사양은 해당 문서 확인)
- Joulescope 사용자 가이드 / 사양 문서 — 자동 레인지 전환, 샘플률, 다운샘플링 방식 (사양은 해당 문서 확인)
- Keysight, N6781A SMU / Battery drain analysis 응용 노트 — seamless measurement ranging 개념
- Nordic Online Power Profiler, TI EnergyTrace 문서 — 저전력 MCU 전류 프로파일과 예산 계산의 실무 예
- W. Kester (ed.), *The Data Conversion Handbook* (Analog Devices) — 양자화 잡음, dither, ENOB
- Energizer/Panasonic CR2032 데이터시트 — 공칭 용량, 펄스 부하 시 전압 강하(배터리 모델은 see:RF-A04)
- 관련 레슨: HWLAB-B04(측정 알고리즘 일반), HWLAB-B08(레일 측정·BW limit), HWLAB-B09(전류 제한·inrush), HWLAB-B02(back-powering), HWLAB-M02(load transient droop)
