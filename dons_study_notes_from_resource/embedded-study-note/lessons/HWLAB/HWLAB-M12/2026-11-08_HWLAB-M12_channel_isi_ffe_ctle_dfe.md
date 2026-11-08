---
id: HWLAB-M12
time: 90분
updated: 2026-11-08
---
# HWLAB-M12 채널 손실·ISI와 등화: TX FFE·CTLE·DFE

## 학습 목표

- 손실 채널을 **FIR(샘플 간격 임펄스 응답)** 으로 모델링하고, 그 FIR에서 Nyquist 삽입손실(−15.00 dB)을 한 주파수짜리 DFT로 직접 확인한다.
- **pulse response** 에서 pre-cursor / main cursor / post-cursor를 뽑아 ISI를 숫자로 말한다: "h₋₁ = 22.9 mV, h₀ = 177.3 mV, h₁ = 87.2 mV — h₁/h₀ = 0.49".
- 3-tap TX FFE(zero-forcing, PCIe preset), 1-zero/2-pole CTLE(bilinear biquad), 1–3 tap DFE를 구현하고 **단계별 eye height/width** 개선을 표로 만든다. 각 등화기가 무엇을 지우고 무엇을 대가로 치르는지(스윙 손실·고주파 잡음 증가·오류 전파) 설명한다.
- PRBS7 eye와 최악 패턴(PDA) eye의 차이, CTLE 과등화, ZF ≠ eye 최적, DFE 오류 전파(×1.47, 평균 버스트 1.47)를 실측 숫자로 보인다.
- PCIe Gen3 보드 두 레인을 preset × CTLE × DFE로 전수 스윕해 판정하고, 실패 레인의 echo를 찾아 "8.8 mm 열린 스텁, 노치 4.27 GHz ≈ Nyquist → R_BYP2 실장 확인" 같은 행동 가능한 리포트를 낸다.

## 1. 문제

고속 링크 bring-up에서 "eye가 안 열린다"는 보고를 받았을 때 채널·등화기·보드 결함을 숫자로 가르는 능력은 Rivian/NVIDIA/Apple 실리콘·보드 검증 면접의 핵심 질문("pulse response의 h₁이 0.5면 무엇을 쓰겠나? CTLE와 DFE의 차이는?")이다.

손실 채널(PCB 트레이스, 커넥터, 패키지)은 저역통과 필터다. 1 UI(unit interval, 비트 하나의 시간)짜리 펄스를 보내면 받는 쪽에서는 낮고 넓게 퍼진 응답이 나온다. 이게 **pulse response** 이고, 그걸 UI 간격으로 샘플링한 값이 **cursor** 다. main cursor h₀는 현재 비트의 신호, 나머지(h₋₁ = pre-cursor, h₁, h₂, … = post-cursor)는 이웃 비트가 현재 샘플에 얹는 **ISI(inter-symbol interference, 심볼 간 간섭)** 다.

```svg
<svg viewBox="0 0 760 316" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pulse response와 cursor (8 GT/s, −15 dB @ 4 GHz, TX ±400 mV) — 실제 계산값</text>
  <line x1="40" y1="230" x2="740" y2="230" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".4">
    <line x1="60" y1="40" x2="60" y2="236"/><line x1="140" y1="40" x2="140" y2="236"/><line x1="220" y1="40" x2="220" y2="236"/>
    <line x1="300" y1="40" x2="300" y2="236"/><line x1="380" y1="40" x2="380" y2="236"/><line x1="460" y1="40" x2="460" y2="236"/>
    <line x1="540" y1="40" x2="540" y2="236"/><line x1="620" y1="40" x2="620" y2="236"/><line x1="700" y1="40" x2="700" y2="236"/>
  </g>
  <rect x="140" y="70" width="80" height="160" class="f-mut" stroke="currentColor" stroke-dasharray="5 3" opacity=".35"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.2" stroke-linejoin="round"
    points="60,230 100,230 120,228 140,209 160,190 180,140 200,92 220,70 240,80 260,110 280,135 300,151 330,180 380,202 420,211 460,217 540,222 620,225 700,226"/>
  <g class="f-warn" stroke="currentColor"><circle cx="140" cy="209" r="5"/></g>
  <g class="f-ok" stroke="currentColor"><circle cx="220" cy="70" r="6"/></g>
  <g class="f-acc" stroke="currentColor"><circle cx="300" cy="151" r="5"/><circle cx="380" cy="202" r="5"/><circle cx="460" cy="217" r="4"/>
    <circle cx="540" cy="222" r="4"/><circle cx="620" cy="225" r="4"/><circle cx="700" cy="226" r="4"/></g>
  <g fill="currentColor" font-size="12">
    <text x="92" y="196">h₋₁ = 22.9</text>
    <text x="230" y="62">h₀ = 177.3 mV (main)</text>
    <text x="306" y="146">h₁ = 87.2</text>
    <text x="386" y="196">h₂ = 31.4</text>
    <text x="466" y="208">h₃ = 14.2</text>
    <text x="560" y="210">h₄…h₄₀ 꼬리 (√f 손실)</text>
    <text x="146" y="86" opacity=".8">보낸 1 UI 펄스</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="60" y="252">−2</text><text x="140" y="252">−1</text><text x="220" y="252">0</text><text x="300" y="252">+1</text>
    <text x="380" y="252">+2</text><text x="460" y="252">+3</text><text x="540" y="252">+4</text><text x="620" y="252">+5</text><text x="700" y="252">+6 UI</text>
    <text x="390" y="278">샘플 y = h₀·b₀ + Σ hₖ·b₋ₖ — 400 mV를 보내 h₀ 177 mV만 남는다</text>
    <text x="390" y="298">이웃 비트 ISI 합 Σ|hₖ| 이 h₀보다 커서 최악 패턴에서 eye가 닫힌다</text>
  </g>
</svg>
```

등화기(equalizer)는 이 ISI를 지운다. 위치와 성질이 다른 세 가지를 이 레슨에서 직접 만든다:

```svg
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm12-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">링크 모델: TX FFE → 채널 FIR → (+잡음) → CTLE → Σ → slicer, DFE는 결정 비트를 되먹임</text>
  <rect x="10" y="60" width="110" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="160" y="60" width="120" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <circle cx="312" cy="88" r="13" fill="none" stroke="currentColor"/>
  <rect x="345" y="60" width="110" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <circle cx="490" cy="88" r="13" fill="none" stroke="currentColor"/>
  <rect x="525" y="60" width="90" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <rect x="520" y="150" width="110" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <path d="M120 88 L160 88" marker-end="url(#hwm12-arr)"/><path d="M280 88 L299 88" marker-end="url(#hwm12-arr)"/>
    <path d="M325 88 L345 88" marker-end="url(#hwm12-arr)"/><path d="M455 88 L477 88" marker-end="url(#hwm12-arr)"/>
    <path d="M503 88 L525 88" marker-end="url(#hwm12-arr)"/><path d="M615 88 L740 88" marker-end="url(#hwm12-arr)"/>
    <path d="M312 50 L312 75" marker-end="url(#hwm12-arr)"/>
    <path d="M680 88 L680 172 L630 172" marker-end="url(#hwm12-arr)"/><path d="M520 172 L490 172 L490 101" marker-end="url(#hwm12-arr)"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="65" y="84" font-weight="bold">TX FFE</text><text x="65" y="102" font-size="12">c₋₁ c₀ c₊₁</text>
    <text x="220" y="84" font-weight="bold">채널 FIR</text><text x="220" y="102" font-size="12">−15 dB @ 4 GHz</text>
    <text x="312" y="44" font-size="12">잡음 σ</text>
    <text x="400" y="84" font-weight="bold">CTLE</text><text x="400" y="102" font-size="12">1 zero / 2 pole</text>
    <text x="490" y="93">Σ</text><text x="476" y="122" font-size="12">−</text>
    <text x="570" y="84" font-weight="bold">slicer</text><text x="570" y="102" font-size="12">d = sign</text>
    <text x="575" y="168" font-weight="bold">DFE</text><text x="575" y="185" font-size="12">Σ wⱼ·d₋ⱼ</text>
    <text x="712" y="80" font-size="12">dₙ</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="222">FFE: pre+post 제거, 잡음 증폭 없음 · 대가 = TX 스윙(저주파 레벨) 감소</text>
    <text x="10" y="242">CTLE: 저주파를 깎아 기울기 보정(선형) · 고주파 잡음·크로스토크 비중 ↑</text>
    <text x="10" y="262">DFE: post-cursor만, 잡음 증폭 없음(비선형) · 대가 = 오류 전파, tap-1 타이밍(1 UI)</text>
  </g>
</svg>
```

| 항목 | 이 레슨의 값 (가정은 명시) |
|---|---|
| 링크 | 8 GT/s NRZ (PCIe Gen3 급), UI = 125 ps, Nyquist 4 GHz, 32 샘플/UI (dt = 3.90625 ps) |
| 채널 | `exp(−k√s)` 표피효과(skin effect) 선로 × 2개 극(pole) — Nyquist 손실의 40 %를 √f 항, 60 %를 극이 담당. 48 UI에서 자른 FIR |
| TX | ±400 mV (800 mVppd 차동), FFE 계수는 \|c₋₁\| + c₀ + \|c₊₁\| = 1 (peak 전력 제약) |
| 잡음 | RX 입력 환산 2.0 mV rms, 0–12 GHz 평탄 (열잡음+크로스토크 묶음 — 가정) |
| eye | **HWLAB-M09의 eye 도구를 축약해 직접 넣었다** — M09는 골든 PLL 폴딩·2D 히스토그램·마스크를 하고, 여기선 잡음·지터 없는 선형 채널이라 이상 클럭 + pulse response 중첩으로 위상별 inner eye를 계산한다. BER eye = PDA eye − 2·Q(1e-12)·σ_out, Q = 7.034 |

### Part A — 채널 FIR · pulse response · 축약 eye 도구

- `fir_from_step(s)` — step 응답 → 샘플 간격 FIR `h[n] = s[n] − s[n−1]`. `il_db(h, f)` — 한 주파수 DFT로 이득[dB]. **FIR에서 잰 4 GHz 손실이 −15.0 ± 0.3 dB** 인지 확인.
- `pulse_from_step(s)` — 1 UI 펄스 응답 `p[n] = 400·(S[n] − S[n−32])` (FIR 끝 이후 step은 그대로, 꼬리 31 샘플까지 포함). `cursors(p, npre, npost)` — peak 기준 UI 간격 샘플.
- `eye_curve(p, bits, taps)` — 주기 PRBS7 비트열을 중첩(superposition)으로 보내 위상별 inner eye 높이 EH(φ). `pda_curve(p, taps)` — 최악 패턴 eye `2·(h₀ − Σ|hₖ|)`. `eye_metrics(eh, thr)` — eye height, 보간한 eye width[UI].
- 검증: 중첩 eye 샘플 == NRZ 파형을 FIR로 직접 컨볼루션한 값(오차 < 1e-6 mV), UI 간격 cursor 전체 합 == 400·step 최종값(망원 합 항등식).

### Part B — 등화기 단계별 (TX FFE · CTLE · DFE) + 오류 전파

- `ffe_zf(cur)` 3-tap zero-forcing, `ffe_apply(p, c)`. `ctle_biquad(adc_db)` — H(s) = ωp2(s + A·ωp1)/((s+ωp1)(s+ωp2)), fp1 = 2 GHz, fp2 = 8 GHz를 bilinear 변환한 biquad (해석해와 0.05 dB 이내). `dfe_taps(p, n, limits)`.
- 단계표: no EQ → TX FFE(ZF) → CTLE(ADC 0…−12 dB 스윕 최적) → +DFE1/2/3, 그리고 "DFE3 only", "ZF+CTLE+DFE3". CTLE 과등화, σ_out/h₀ 증가(상대 잡음 증가)도 테스트.
- `dfe_sim(cur, ntaps, σ, …, genie)` / `bursts(errs, gap)` — 같은 비트·잡음으로 **참 비트를 되먹인 DFE(genie)** 와 **자기 결정을 되먹인 실제 DFE** 의 BER·버스트 길이 비교.

### Part C — VALBRD-S rev A: PCIe Gen3 두 레인 판정

가상의 보드 **VALBRD-S rev A** — SoC PCIe Gen3 x4 root port → M.2 M-key(NVMe SSD) 슬롯. 같은 길이(−20 dB @ 4 GHz)로 라우팅된 RX 레인 L0, L2의 차동 TDT step 응답 CSV(`valbrd_s_tdt_steps.csv`)를 받았다. L2에는 결함을 심어 두었다: BOM상 DNP여야 할 바이패스 0 Ω(R_BYP2)이 실장되어 **열린 스텁(open stub)** 이 T자로 붙었다. 스펙 파일 `valbrd_s_pcie_gen3.json`(TX preset P0–P9, 기준 수신기 CTLE ADC −6…−12 dB + 1-tap DFE ±30 mV, eye ≥ 25 mV / 0.30 UI @1e-12 — 대표값)으로:

- `preset_taps(ps_db, de_db)` — preshoot/de-emphasis[dB] → (c₋₁, c₀, c₊₁). P7 = (−0.100, 0.700, −0.200).
- `sweep_eq()`(주어짐) 로 10 preset × 7 CTLE = 70 설정 전수 스윕 → 레인별 최선 설정과 PASS/FAIL.
- `find_echo(p_ref, p_dut)` — 골든 레인 대비 echo의 지연 τ·크기·분기점 Γ를 찾고, 스텁 길이 = v·τ/2, 노치 = 1/(2τ)로 바꿔 한 줄 리포트.

## 2. Problem (English)

> **Analyze ISI of a lossy serial channel and equalize it.**
>
> A lossy channel is given as a sampled step response `s[n]` (32 samples per UI, 8 GT/s NRZ, TX swing ±400 mV). Using only the
> Python 3 standard library:
>
> 1. Build the channel FIR `h = diff(s)`, verify its insertion loss at Nyquist (4 GHz) by a single-bin DFT (−15 dB ± 0.3), build the
>    1-UI pulse response, and report pre/main/post cursors (main = peak).
> 2. Write an eye tool for a linear channel: per-phase inner eye height for a periodic PRBS7 pattern (superposition), the
>    worst-case (peak-distortion) eye `2·(h0 − Σ|hk − wk|)`, and eye width by linear interpolation of EH(φ) crossings. A BER eye
>    subtracts `2·Q·σ` (Q(1e-12) = 7.034) with σ the noise rms after the CTLE.
> 3. Implement a 3-tap zero-forcing TX FFE (normalized `|c-1|+|c0|+|c+1| = 1`), a CTLE
>    `H(s) = ωp2(s + A·ωp1)/((s+ωp1)(s+ωp2))` as a bilinear-transform biquad, and an N-tap DFE (taps = post-cursors, optionally clipped).
>    Tabulate eye height/width per stage and show the CTLE over-equalization point.
> 4. Simulate a DFE at the symbol rate with Gaussian noise and compare genie feedback vs. decision feedback: BER ratio and
>    error-burst lengths. Show that a large `w1/h0` causes stronger error propagation.
> 5. For two PCIe Gen3 lanes, convert TX presets (preshoot/de-emphasis dB) to tap weights, sweep preset × CTLE × 1-tap DFE, judge
>    against EH ≥ 25 mV, EW ≥ 0.3 UI @1e-12, and for the failing lane estimate the echo delay/amplitude by least squares against
>    the good lane, converting it to stub length and notch frequency.
>
> Example: P7 (3.5 dB preshoot, −6.0 dB de-emphasis) → `(-0.100, 0.700, -0.200)`. The −15 dB channel has
> `h-1/h0 ≈ 0.13`, `h1/h0 ≈ 0.49`; its PRBS7 eye is barely open (≈ 23 mV) while the worst-case eye is closed.

## 3. 시작 코드

<!-- code: starter.py -->

Part C 스펙(대표값 표기 포함)과 데이터 생성기 (결정적 — 다시 돌려도 CSV가 바이트 단위로 같다):

<!-- code: valbrd_s_pcie_gen3.json -->

<!-- code: gen_lanes.py -->

```bash
cd lessons/HWLAB/HWLAB-M12
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 1/31 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: channel FIR / pulse response / eye (8 GT/s, -15 dB @ 4 GHz) --
      (setup failed: NotImplementedError: TODO: fir_from_step)
FAIL  FIR = diff(step): sum(h) == step[-1] (telescoping): sum(h)=0.000000 step_end=0.954731
      (setup failed: NotImplementedError: TODO: il_db)
FAIL  insertion loss @4GHz from FIR == -15.0 dB (+-0.3): TypeError: must be real number, not NoneType
...
FAIL  superposition eye sample == brute-force FIR convolution of NRZ waveform: max |conv - superposition| = 3.22e+02 mV
...
-- Part C: VALBRD-S rev A PCIe Gen3 lanes (TDT step -> pulse -> EQ sweep -> report) --
FAIL  preset P7 (3.5 dB PS, -6.0 dB DE) -> (-0.100, 0.700, -0.200) +-0.002: NotImplementedError: TODO: preset_taps
PASS  TDT CSV loaded: lanes L0,L2, 1536 samples @ 3.90625 ps
...
1/31 passed  (0.0 s)
exit=1
```

> 눈여겨볼 것: starter의 `pulse_from_step` 은 일부러 **꼬리가 잘린 버전**(길이 len(s))이다. 대부분 맞아 보이지만 FIR이 끝나는 지점 뒤 31 샘플의 펄스 꼬리가 없어서, `eye_curve` 를 다 구현해도 중첩 eye와 직접 컨볼루션이 0.12 mV 어긋난다(직접 해 본 값). "모델의 끝을 어떻게 닫았나"는 측정 pulse response(VNA → IFFT)에서도 똑같이 나오는 문제다.

## 4. 힌트

<details>
<summary>힌트 1 — 한 주파수 DFT와 FIR의 DC 이득</summary>

`H(f) = Σ h[n]·e^(−j2πf·n·dt)` 를 `cmath.exp` 로 그대로 더하면 된다(FFT 불필요 — 주파수 3개만 본다). f = 0이면 Σh = s[−1] — step이 48 UI 끝에서 0.955까지만 올라가 있다(√f 손실의 긴 꼬리를 잘랐다). 이 DC 0.4 dB 부족은 Nyquist 값에 거의 영향이 없다.

</details>

<details>
<summary>힌트 2 — 중첩으로 eye 만들기</summary>

선형 채널에서 받은 파형은 `y(t) = Σⱼ bⱼ·p(t − j·UI)`. 비트 m의 위상 φ 샘플은 `Σₖ b₍ₘ₋ₖ₎·p[o + φ + k·32]`. o = main − 16으로 잡으면 φ = 16이 peak. PRBS7을 **주기**로 보내므로 `bits[(m−k) % 127]` — 파형을 만들 필요가 없다. 위상마다 127비트 × ~50 cursor라 32위상 전체가 수 ms.

</details>

<details>
<summary>힌트 3 — eye width 보간</summary>

best φ에서 좌우로 `eh > thr` 인 동안 걷는다. 왼쪽 경계 칸 l에서 `lx = l − (eh[l] − thr)/(eh[l] − eh[l−1])`. 오른쪽도 대칭. EW = (rx − lx)/32 UI. 삼각형 `EH(φ) = 10 − 2|φ − 16|` 이면 0 교차가 11, 21 → EW = 10/32.

</details>

<details>
<summary>힌트 4 — ZF FFE 연립방정식</summary>

등화 후 cursor `g_m = c₋₁·h₍ₘ₊₁₎ + c₀·h_m + c₊₁·h₍ₘ₋₁₎`. c₀ = 1로 두고 g₋₁ = 0, g₊₁ = 0:
`c₋₁·h₀ + c₊₁·h₋₂ = −h₋₁`, `c₋₁·h₂ + c₊₁·h₀ = −h₁`. 2×2 크라머 공식 후 |c| 합으로 나눈다. 검증은 **설계한 위상**(원래 peak + 1 UI)에서 한다 — FFE 후 peak 위치가 바뀔 수 있다.

</details>

<details>
<summary>힌트 5 — bilinear 변환 계수</summary>

`H(s) = (B0·s² + B1·s + B2)/(A0·s² + A1·s + A2)`, `s = K(1 − z⁻¹)/(1 + z⁻¹)`, K = 2/dt. 분모·분자에 (1 + z⁻¹)² 를 곱하면 `b0 = B0K² + B1K + B2`, `b1 = 2B2 − 2B0K²`, `b2 = B0K² − B1K + B2` (a도 같은 꼴). fs = 256 GHz라 4 GHz에서 주파수 휨(warping)은 0.08 %뿐 — prewarp 없이 된다.

</details>

<details>
<summary>힌트 6 — echo를 선형 최소제곱으로</summary>

열린 스텁의 통과 응답은 반사가 무한히 오가므로 `H = (a + b·z)/(1 + c·z)`, z = e^(−sτ)(지연 τ). 양변에 (1 + c·z)를 곱하면 `y[n] + c·y[n−τ] = a·x[n] + b·x[n−τ]` — τ를 고정하면 a, b, c에 대해 **선형**. τ를 스캔하며 3×3 정규방정식(`solve3` 주어짐)을 풀고 잔차 최소를 고른다.

</details>

## 5. 풀이

### Part A — 채널을 FIR로, ISI를 cursor로

**채널 모델.** `H(s) = exp(−k√s) · 1/(1 + s/ωp)²`.
- `exp(−k√s)` 는 표피효과 지배 선로의 고전 모델이다. step 응답이 닫힌 꼴 `erfc(k/(2√t))` (라플라스 쌍 `(1/s)·e^(−k√s) ↔ erfc(k/(2√t))`)이라 인과적(causal)이고 계산이 정확하다. `√(jω) = √ω·(1+j)/√2` 이므로 `|H| = exp(−k√(πf))`, 손실 dB = 8.686·k·√(πf) — √f 비례.
- 2개 극은 패키지·패드 커패시턴스와 유전 손실(dielectric loss, tanδ — 실제로는 f에 비례하는 손실)을 **뭉뚱그린 근사**다. 실제 SI 도구는 인과적 유전체 모델(Djordjevic–Sarkar 등)과 측정 S-파라미터를 쓴다. Nyquist 손실 15 dB를 6 dB(√f) + 9 dB(극, fp = 2.97 GHz)로 나눴다.
- step-invariant 1차 IIR(`y += α(x − y)`, α = 1 − e^(−dt·ωp))로 극을 적용 — 입력이 dt 동안 일정하다는 가정(ZOH)에서 정확.

**FIR과 삽입손실.** `h[n] = s[n] − s[n−1]` 은 "dt 폭 단위 펄스"의 응답이다. 그 DFT가 채널 주파수 응답(dt 폭 sinc 보정 −0.02 dB 수준 포함). 실제 출력: **IL(1/2/4 GHz) = −3.94 / −7.50 / −15.00 dB** — 설계한 −15 dB를 FIR에서 되찾았다.

**pulse response와 cursor.** 1 UI 펄스 = step(t) − step(t − UI). 실제 출력:

```text
      cursors[mV]: h-2=0.0 h-1=22.9 h+0=177.3 h+1=87.2 h+2=31.4 h+3=14.2 h+4=8.3 h+5=5.7 h+6=4.3
```

| 지표 | 값 | 의미 |
|---|---|---|
| h₀/TX | 177.3/400 = 0.44 | 펄스 높이가 −7.1 dB로 줄었다 (Nyquist −15 dB보다 덜 — 펄스는 넓은 대역) |
| h₋₁/h₀ | 0.129 | pre-cursor — DFE로 못 지움, TX FFE c₋₁ 또는 CTLE 몫 |
| h₁/h₀ | 0.492 | 큰 첫 post-cursor — DFE tap-1이 가장 효과적인 자리 |
| Σ\|hₖ\| (k≠0) / h₀ | > 1 | 최악 패턴 eye가 닫힘 (PDA EH = −54.6 mV) |

**두 가지 항등식으로 구현을 검증.** ① `Σₖ p[i₀ + k·UI] = 400·s[끝]` — 펄스 = step 차분이라 UI 간격 합이 망원(telescoping) 합이 된다(어떤 위상이든). 실제: 381.8926 = 381.8926. ② 중첩 eye 샘플 == NRZ 파형을 FIR로 직접 컨볼루션한 값: 실제 최대 오차 8.53e−13 mV.

**eye 두 개 — PRBS7 vs 최악 패턴(PDA).** PDA(peak distortion analysis)는 모든 ISI가 동시에 나쁜 부호인 비트열의 eye: `EH = 2·(h₀ − Σ|hₖ|)`. 실제 출력:

```text
      no EQ: PRBS7 eye EH=22.9mV EW=0.305UI | worst-case(PDA) EH=-54.6mV EW=0.000UI
```

PRBS7은 127비트 주기라 "7비트 창 안의 패턴"만 다 나온다. 이 채널은 ISI가 40 UI 넘게 이어져 최악 패턴(약 40비트짜리 특정 열)이 PRBS7에 없다. 연습 1의 실측: no EQ에서 PRBS7 22.9 → PRBS9 −0.7 → PRBS11 −16.7 → PDA −54.6 mV. **랩에서 PRBS7로 본 eye는 낙관적이다.** 그 40비트 최악 패턴이 나올 확률은 2⁻⁴⁰ ≈ 9e−13 — BER 1e−12를 논할 때는 무시할 수 없는 크기라서, 컴플라이언스 도구는 cursor 분포를 컨볼루션한 통계적 eye(statistical eye)를 쓰고, PDA는 그 확률 0 경계(하한)다.

### Part B — 등화기 셋, 각자 무엇을 지우고 무엇을 내주나

**TX FFE (3-tap).** 송신 심볼을 `x_n = c₋₁·b₍ₙ₊₁₎ + c₀·b_n + c₊₁·b₍ₙ₋₁₎` 로 만든다. 송신기 드라이버의 최대 스윙이 고정이라 `|c₋₁| + c₀ + |c₊₁| = 1`(peak 제약) — 그래서 FFE는 고주파를 "올리는" 게 아니라 **저주파 레벨을 깎는다**(de-emphasis). ZF 실제 출력: `c₋₁=-0.081 c₀=0.626 c₊₁=-0.293`. 설계 위상에서 g₋₁, g₊₁ = 3.6e−15, 7.1e−15 mV(정확히 0). 대가: h₀가 177.3 → 98.7 mV. 잡음은 RX에서 더해지니 FFE가 키우지 않는다 — 신호만 작아진다. 그리고 ZF는 eye 최적이 아니다: 연습 2의 격자 탐색에서 (−0.050, 0.650, −0.300)이 @1e-12 EH 131.3 mV로 ZF(128.6 mV)보다 낫다 — ZF는 h±1만 0으로 만들고 꼬리·위상 민감도는 보지 않는다.

**CTLE (1 zero / 2 pole).** `H(s) = ωp2·(s + A·ωp1)/((s + ωp1)(s + ωp2))`, A = 10^(ADC/20). DC 이득 = A(예: −7 dB), 중간 대역(fp1 ≪ f ≪ fp2) 이득 ≈ 1, zero는 fz = A·fp1. 즉 **CTLE도 고주파를 올리는 게 아니라 저주파를 내린다** — "peaking"은 상대값이다. 이 꼴은 PCIe Gen3 기준 수신기(behavioral reference CTLE)의 형태다(fp1 = 2 GHz, fp2 = 8 GHz, ADC −6…−12 dB — 대표값, 스펙 확인 필요).

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">|H(f)| [dB]: 채널(−15 dB @ 4 GHz) · CTLE(ADC −7 dB) · 합성 — 코드로 계산한 점을 이은 것</text>
  <line x1="80" y1="50" x2="80" y2="295" stroke="currentColor"/><line x1="80" y1="295" x2="710" y2="295" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".35">
    <line x1="80" y1="50" x2="710" y2="50"/><line x1="80" y1="130" x2="710" y2="130"/><line x1="80" y1="210" x2="710" y2="210"/>
    <line x1="390" y1="50" x2="390" y2="295"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="74" y="54">0</text><text x="74" y="134">−10</text><text x="74" y="214">−20</text><text x="74" y="294">−30</text></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="80" y="312">0</text><text x="235" y="312">2</text><text x="390" y="312">4 GHz (Nyquist)</text><text x="545" y="312">6</text><text x="700" y="312">8</text></g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.2" points="80,50 99,62 119,69 158,82 196,95 235,110 312,141 390,170 468,197 545,222 622,244 700,265"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="7 4" points="80,106 99,104 119,99 158,86 196,76 235,70 312,64 390,64 468,66 545,68 622,72 700,76"/>
  <polyline fill="none" stroke="currentColor" stroke-width="3" opacity=".55" points="80,116 99,116 119,118 158,118 196,121 235,130 312,155 390,184 468,213 545,240 622,266 700,290"/>
  <g class="f-ok" stroke="currentColor"><circle cx="390" cy="170" r="5"/><circle cx="390" cy="184" r="5"/></g>
  <g class="f-acc" stroke="currentColor"><circle cx="390" cy="64" r="5"/><circle cx="80" cy="106" r="4"/></g>
  <g fill="currentColor" font-size="12">
    <text x="470" y="186">채널: −15.0 dB (기울기 15 dB)</text>
    <text x="420" y="58">CTLE: −1.7 dB, peak 3.66 GHz</text>
    <text x="90" y="98">CTLE DC = −7 dB (zero 0.89 GHz)</text>
    <text x="200" y="160">합성: −8.3 dB(저역) → −16.7 dB(4 GHz)</text>
    <text x="200" y="176">기울기 15 → 8.4 dB로 평탄화 (절대 이득은 더 낮다)</text>
  </g>
</svg>
```

bilinear biquad를 임펄스로 돌려 DFT한 결과는 해석해와 최대 0.0005 dB, DC −7.000 dB로 일치했다. ADC 스윕 (`demo_sweeps.py` 실제 출력, −15 dB 채널):

```text
ADC[dB]   h0[mV]  h1[mV]  h1/h0   sigma_out  sigma/h0   PDA EH   @1e-12 EH  @1e-12 EW
    0      171.3    88.2   0.515     1.62    0.0095     -78.8    -101.5    0.000
   -3      141.9    57.9   0.408     1.49    0.0105      26.7       5.8    0.164
   -6      122.7    37.0   0.302     1.42    0.0115     107.5      87.5    0.671
   -7      117.8    32.5   0.276     1.40    0.0119     113.6      93.9    0.716
   -8      113.7    26.4   0.232     1.39    0.0122     112.5      93.0    0.729
   -9      109.9    20.9   0.190     1.38    0.0125     110.1      90.7    0.721
  -12      101.4    10.0   0.098     1.36    0.0134     102.3      83.1    0.607
```

(전체 13줄 중 일부.) 읽는 법:

1. **최적점이 있다(−7 dB).** 그보다 깊게 가면 h₁은 계속 줄지만(−12 dB에서 10 mV) h₀도 같이 줄고 꼬리 cursor 부호가 뒤집혀(과등화, over-equalization) eye가 다시 작아진다. "peaking 많이 = 좋다"가 아니다.
2. **상대 잡음 증가.** σ_out 절댓값은 오히려 1.62 → 1.36 mV로 준다(CTLE가 저주파와 8 GHz 위를 깎으니까). 하지만 신호 h₀가 더 빨리 줄어 **σ/h₀가 0.0095 → 0.0134로 단조 증가** 한다. "CTLE가 고주파 잡음을 증폭한다"의 정확한 뜻은 이것 — 신호 대비 고주파 잡음·크로스토크의 비중이 커진다. 크로스토크(FEXT)는 주파수가 높을수록 커서 실제 보드에선 이 효과가 이 평탄 잡음 가정보다 크다.
3. eye width도 −8 dB 근처가 최대 — EH 최적과 EW 최적이 꼭 같지 않다. 판정 기준(EH 우선? 면적?)을 미리 정해야 한다.

**DFE.** 이미 내린 결정 d₍ₙ₋ⱼ₎에 tap wⱼ(= post-cursor hⱼ)를 곱해 빼서 현재 샘플의 post-cursor ISI를 지운다. 잡음을 증폭하지 않는다 — 빼는 값이 아날로그 신호가 아니라 깨끗한 디지털 결정이기 때문. 하지만:
- **pre-cursor는 못 지운다** (미래 비트는 아직 결정 전).
- tap-1은 결정 → 곱 → 빼기 → 다음 결정이 **1 UI(125 ps) 안에** 닫혀야 한다. 그래서 실리콘은 tap-1을 투기(speculation, loop unrolling: 두 비교기 + MUX)로 만든다 — 연습 4에서 직접 구현해 결정이 완전히 같음을 확인.
- tap 범위가 유한하다(이 레슨의 Part C 기준 수신기는 1 tap, ±30 mV).
- **오류 전파.**

`python3 -I solution.py` Part B 실제 출력 (단계표):

```text
      no EQ              h-1=  22.9 h0= 177.3 h1=  87.2 h2=  31.4 | PRBS7   22.9mV 0.31UI | PDA  -54.6mV 0.00UI | @1e-12  -82.8mV 0.00UI (σ=2.00mV)
      TX FFE (ZF)        h-1=  -7.1 h0=  98.7 h1=   5.8 h2=  -7.8 | PRBS7  168.8mV 0.81UI | PDA  156.7mV 0.75UI | @1e-12  128.6mV 0.64UI (σ=2.00mV)
      CTLE -7dB          h-1=   9.2 h0= 117.8 h1=  32.5 h2=  -2.2 | PRBS7  138.3mV 0.88UI | PDA  113.6mV 0.80UI | @1e-12   93.9mV 0.72UI (σ=1.40mV)
      CTLE+DFE1          h-1=   9.2 h0= 117.8 h1=  32.5 h2=  -2.2 | PRBS7  202.3mV 0.92UI | PDA  178.7mV 0.87UI | @1e-12  159.0mV 0.80UI (σ=1.40mV)
      CTLE+DFE2          h-1=   9.2 h0= 117.8 h1=  32.5 h2=  -2.2 | PRBS7  206.1mV 0.92UI | PDA  183.1mV 0.87UI | @1e-12  163.4mV 0.80UI (σ=1.40mV)
      CTLE+DFE3          h-1=   9.2 h0= 117.8 h1=  32.5 h2=  -2.2 | PRBS7  208.7mV 0.93UI | PDA  186.8mV 0.87UI | @1e-12  167.1mV 0.80UI (σ=1.40mV)
      DFE3 only          h-1=  22.9 h0= 177.3 h1=  87.2 h2=  31.4 | PRBS7  278.4mV 0.89UI | PDA  210.9mV 0.77UI | @1e-12  182.8mV 0.69UI (σ=2.00mV)
      ZF+CTLE+DFE3       h-1=  -5.1 h0=  68.6 h1= -12.4 h2= -11.3 | PRBS7  122.1mV 0.75UI | PDA  116.9mV 0.72UI | @1e-12   97.2mV 0.63UI (σ=1.40mV)
```

(표의 cursor는 각 단계 등화 **전** 펄스의 peak 기준 — DFE는 펄스를 바꾸지 않고 결정에서 뺀다. TX FFE 행의 h±1이 0이 아닌 것은 FFE 후 peak가 설계 위상에서 옮겨 갔기 때문이다.)

- 체인 @1e-12: −82.8 → 93.9 → 159.0 → 163.4 → 167.1 mV. CTLE가 h₁을 32.5 mV로 줄여 놓았으니 DFE1이 가장 크게(+65 mV) 벌고, DFE2/3은 몇 mV씩만 더한다 — **CTLE가 펄스를 짧게 만들어 적은 DFE tap으로 끝나게 하는 것** 이 실제 수신기 구조의 이유다.
- **"DFE3 only"가 182.8 mV로 숫자상 최고.** 이 모델은 tap 범위 무제한·결정 오류 없음을 가정하기 때문이다. 실제로는 w₁ = 87 mV(= 0.49·h₀) tap을 1 UI 안에 닫아야 하고, tap 범위 한계에 걸리며(Part C는 ±30 mV), 아래 오류 전파가 커진다.
- **ZF+CTLE+DFE3 = 97.2 mV로 오히려 나쁘다.** 각각 따로 최적화한 등화기를 쌓으면 같은 ISI를 두 번 지워 과등화된다(h₁ = −12.4 mV). 실제 링크 트레이닝이 TX preset과 RX CTLE/DFE를 **함께** 최적화하는 이유(PCIe Gen3 equalization phase 2/3).

**DFE 오류 전파 — 직접 재 보기.** 같은 seed(같은 비트·같은 잡음)로 genie DFE(참 비트를 되먹임)와 실제 DFE(자기 결정을 되먹임)를 돌린다. 실제 출력(`demo_sweeps.py`, 10만 비트):

```text
config       w1/h0  sigma/h0  genie BER   actual BER  ratio  burst(mean/max)
DFE3 only    0.49   1/3.6    5.90e-04    7.90e-04    x1.34   1.34 / 3
DFE3 only    0.49   1/3.2    1.98e-03    2.91e-03    x1.47   1.47 / 3
DFE3 only    0.49   1/2.8    4.90e-03    7.41e-03    x1.51   1.53 / 6
CTLE+DFE3    0.28   1/3.6    2.00e-04    2.00e-04    x1.00   1.00 / 1
CTLE+DFE3    0.28   1/3.2    9.60e-04    1.04e-03    x1.08   1.08 / 3
CTLE+DFE3    0.28   1/2.8    3.43e-03    3.73e-03    x1.09   1.09 / 3
```

왜 이런 숫자가 나오나: 비트 n−1을 틀리면 피드백이 2·w₁만큼 틀린다. 슬라이서 입력은 `b_n·h₀ + 2w₁·b₍ₙ₋₁₎ + …`. b_n = b₍ₙ₋₁₎이면 오히려 마진이 늘고, b_n ≠ b₍ₙ₋₁₎(확률 ½)이면 마진이 h₀ − 2w₁로 준다. w₁/h₀ = 0.49면 h₀ − 2w₁ ≈ 0.02·h₀ — 거의 동전 던지기 → 다음 비트 오류 확률 ≈ ½·½ ≈ 0.25~0.3 → 평균 버스트 ≈ 1/(1 − 0.3) ≈ 1.4. w₁/h₀ = 0.28면 h₀ − 2w₁ = 0.44·h₀ = 1.4σ(σ = h₀/3.2) → 다음 오류 확률 ≈ ½·Q(1.4) ≈ 0.04 → 1.04~1.08. 측정 1.47 / 1.08과 맞는다. 오류가 genie 대비 늘어난 비율(×1.47)이 평균 버스트 길이(1.47)와 같은 것도 이 때문이다 — 새로 생긴 오류는 전부 버스트의 꼬리다. 그래서 고속 링크(특히 PAM4)는 FEC가 버스트를 견디도록 interleave·precoding(1/(1+D))을 쓴다 (see:NETHW).

| 원인 | 증상 | 드러내는 측정 / 대책 |
|---|---|---|
| 손실 채널 (h₁/h₀ 큼) | PRBS7 eye는 겨우 열림, 긴 패턴에서 오류 | pulse response cursor 표, PRBS31/통계 eye로 재측정 |
| CTLE 과등화 | h₁ 음수, eye 다시 작아짐, 잡음 비중 ↑ | ADC 스윕 표에서 최적점 확인, σ/h₀ 추적 |
| 등화기 독립 최적화 후 중첩 | 각각은 좋은데 합치면 나쁨 | 합동 스윕(TX preset × RX 설정), 링크 트레이닝 로그 |
| DFE tap-1이 너무 큼 | 버스트 오류, FEC 보정 불가 오류 | genie 대비 BER 비, 버스트 길이 히스토그램 → CTLE를 먼저 |
| pre-cursor 큼 | DFE만으로 안 열림 | TX preshoot(c₋₁) — preset P5/P6/P9 |
| 반사(스텁·커넥터) | DFE 범위 밖 cursor, 노치 | pulse response의 두 번째 봉우리, TDR(HWLAB-M01) |

### Part C — VALBRD-S 두 레인: 등화로 될 일과 보드를 고칠 일

**preset → tap.** a = |c₋₁|, b = |c₊₁|, c₀ = 1 − a − b로 두면 비트 패턴별 레벨이: 고립 비트 Vd = 1, 정상 상태(같은 비트 연속) Vb = 1 − 2a − 2b, 전이 직후 Va = 1 − 2a, 전이 직전 Vc = 1 − 2b. de-emphasis = 20·log10(Vb/Va), preshoot = 20·log10(Vc/Vb). rd = 10^(DE/20), rp = 10^(PS/20)이면 a, b에 대한 일차식 두 개 → 크라머. 실제: P7 → (−0.0996, 0.7007, −0.1997), P1(−3.5 dB) → c₊₁ = −0.1658. (PCIe Gen3 preset 표 P0–P10의 dB 값은 대표값 — Base Spec 원문 확인 필요. P10은 FS/LF 경계라 표에서 뺐다.)

**스윕.** 레인마다 CTLE ADC −6…−12 dB(7) × preset P0–P9(10) = 70 설정, DFE 1 tap(±30 mV 클립), 판정 지표는 PDA eye − 2·7.034·σ_out. 실제 출력:

```text
   PASS VALBRD-S PCIE_RX0(L0): best P4+CTLE-9dB+DFE1(29.3mV) -> EH=82.7mV EW=0.70UI @1e-12 (spec >=25mV/0.3UI) | PRBS7 lab eye 103.2mV
   FAIL VALBRD-S PCIE_RX2(L2): best P3+CTLE-8dB+DFE1(24.2mV) -> EH=17.4mV EW=0.52UI @1e-12 (spec >=25mV/0.3UI) | PRBS7 lab eye 37.3mV
     echo +0.67x @ +117.2ps vs L0 (junction G=-0.33) -> open stub ~ 8.8 mm (v=15cm/ns), notch ~ 4.27 GHz (F_nyq 4 GHz)
     -> R_BYP2(0R, BOM상 DNP) 실장 여부 확인 -> 제거 후 재측정, TDR로 위치 확인(HWLAB-M01)
      L0 top3: P4/-9dB 82.7mV 0.70UI, P5/-9dB 82.4mV 0.65UI, P6/-9dB 79.6mV 0.63UI
      L2 top3: P3/-8dB 17.4mV 0.52UI, P1/-8dB 17.3mV 0.46UI, P3/-7dB 16.6mV 0.47UI
```

- L0: −20 dB인데도 CTLE −9 dB + DFE1로 82.7 mV — 마진 충분. 이 모델에선 TX FFE 없는 P4가 최선이었다(FFE의 스윙 손실 > 이득). 상위 3개가 0.3 mV 차이 — 실제 링크 트레이닝이 매번 다른 preset을 고를 수 있다는 뜻이고, 그게 정상이다.
- L2: 70개 설정 **전부** 25 mV 미만. 등화로 해결할 문제가 아니다.
- **"PRBS7 lab eye 37.3 mV"** — 랩에서 PRBS7 패턴으로 eye를 보면 L2도 스펙을 넘는다(Part A의 교훈 그대로). 판정은 최악 패턴/통계 eye 기준이어야 한다.

**echo를 스텁으로 번역.** 같은 임피던스 Z₀의 선로에 열린 스텁이 T로 붙으면, 분기점에서 본 부하는 Z₀ ∥ Z₀ = Z₀/2 → Γ = −1/3, 통과 1 + Γ = 2/3. 스텁으로 들어간 파(2/3)는 열린 끝(Γ = +1)에서 돌아와 분기점에서 다시 2/3이 통과 → 첫 echo = 4/9, 이후 매번 −1/3배. 등비급수로 접으면

```text
H(z) = (2/3)·(1 + z) / (1 + z/3),   z = e^(−s·τ),  τ = 스텁 왕복 지연
     = 2/3 + (4/9)z − (4/27)z² + (4/81)z³ − …        DC(z=1): (2/3)·2/(4/3) = 1
노치: 1 + z = 0 → z = −1 → f = 1/(2τ)   (스텁 = 1/4 파장)
```

```svg
<svg viewBox="0 0 760 266" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">L2 결함: 실장된 R_BYP2(0 Ω)로 생긴 열린 스텁 — echo와 1/4 파장 노치</text>
  <line x1="40" y1="90" x2="720" y2="90" stroke="currentColor" stroke-width="3"/>
  <line x1="380" y1="90" x2="380" y2="185" stroke="currentColor" stroke-width="3"/>
  <rect x="368" y="185" width="24" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="20" y="76" width="40" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="700" y="76" width="40" height="28" class="f-ok" stroke="currentColor"/>
  <g class="f-acc" stroke="currentColor"><circle cx="380" cy="90" r="6"/></g>
  <g fill="currentColor" font-size="12">
    <text x="22" y="66">TX (SoC)</text><text x="690" y="66">RX (M.2)</text>
    <text x="300" y="80">분기점 Γ = −1/3</text><text x="300" y="118">통과 2/3</text>
    <text x="398" y="140">스텁 8.8 mm</text><text x="398" y="156">(왕복 τ = 117.2 ps)</text>
    <text x="398" y="200">열린 끝 Γ = +1 (R_BYP2 너머 DNP 패드)</text>
    <text x="460" y="118">echo 4/9 @ +τ, −4/27 @ +2τ …</text>
    <text x="40" y="226">L ≈ v·τ/2 = 15 cm/ns × 117.2 ps / 2 = 8.8 mm · 노치 f = 1/(2τ) = 4.27 GHz ≈ Gen3 Nyquist 4 GHz</text>
    <text x="40" y="246">→ Gen3 eye가 닫힌다. Gen2(Nyquist 2.5 GHz)는 노치보다 아래라 링크가 붙는다</text>
  </g>
</svg>
```

`find_echo` 는 이 꼴을 일반화한 `(a + b·z)/(1 + c·z)` 를 τ마다 최소제곱으로 푼다. 실제: **τ = 30 샘플 = 117.19 ps(심어 둔 값 그대로), a = b = 0.6667, c = 0.3333, 잔차 9.4e−08** → 첫 echo/통과 = (b − a·c)/a = 0.667, 분기점 Γ = −c = −0.333. (처음엔 2항 모델 `a·x[n] + b·x[n−τ]` 로 풀었더니 고차 echo를 흡수하느라 τ = 23 샘플로 틀렸다 — 반사가 여러 번 오가는 구조에는 IIR 꼴이 맞다.)

**행동 가능한 한 줄 리포트:**

`FAIL VALBRD-S PCIE_RX2: 70개 EQ 설정 최선(P3+CTLE−8dB+DFE1) EH 17.4 mV < 25 mV @1e-12 (L0 동일 라우팅 82.7 mV PASS). 골든 레인 대비 echo +0.67× @ +117 ps, 분기 Γ −0.33 → 약 8.8 mm 열린 스텁, 노치 4.27 GHz ≈ Gen3 Nyquist. R_BYP2(0 Ω, BOM DNP) 실장 확인·제거 후 재측정. 등화 튜닝으로 해결 불가.`

- 링크 증상으로는: Gen3 equalization 단계에서 해당 레인이 목표 BER을 못 맞춰 LTSSM이 Recovery를 반복하거나, 레인 폭/속도를 낮춰(x4 → x2, Gen3 → Gen2) 붙는 형태로 보이는 것이 대표적이다(`lspci -vvv` 의 LnkSta Speed/Width "downgraded"). 이 레슨의 노치는 Gen2 Nyquist(2.5 GHz) 위에 있어 Gen2로는 동작한다 — "Gen2로 내리면 된다"는 관찰이 오히려 결함 위치(고주파 노치)의 힌트다.
- **장비/도구가 있을 때 (이 레슨에선 실행하지 않음):** 4-포트 VNA로 SDD21/SDD11을 재서 s4p로 저장 → IFFT로 step/pulse response(이 레슨 CSV 자리). TDR(차동)로 임피던스 프로파일을 보면 분기점이 dip으로 보인다(위치 계산은 HWLAB-M01). 칩 쪽에서는 SerDes 내장 eye scan(예: FPGA transceiver의 IBERT 2D eye)이나 PCIe Gen4+ Lane Margining at Receiver로 실제 eye 마진을 읽는다(HWLAB-A02 참고). PCIe Gen3 레인별 TX preset 요청값은 Secondary PCI Express Extended Capability의 Lane Equalization Control 레지스터에 있다(필드 배치는 스펙 확인).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"              # 31/31 passed (1.2 s), exit=0
python3 -I -W error -X dev solution.py                # 경고 없음 (31/31, exit 0)
python3 -I gen_lanes.py                               # CSV 재생성 → shasum 동일 (74307a99…)
python3 -I demo_sweeps.py                             # §5의 CTLE 스윕 표 + 오류 전파 표 (3.3 s)
```

<!-- code: demo_sweeps.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| pulse를 `s[n] − s[n−32]` 로 len(s)까지만 | FIR 끝 뒤 31 샘플 꼬리가 빠져 컨볼루션과 0.12 mV 어긋남 | `S(n) = s[min(n, len−1)]` 로 연장, 길이 len + 31 |
| eye를 PRBS7로만 보고 PASS | 긴 ISI의 최악 패턴이 PRBS7에 없음 (L2: 37.3 mV vs PDA 17.4 mV) | PDA/통계 eye로 판정, 랩 PRBS는 PRBS31 |
| CTLE peaking을 최대로 | 과등화: h₁ 음수, h₀ 감소, σ/h₀ 증가 | ADC 스윕 → eye 최대점 (−7 dB) |
| CTLE가 고주파를 "증폭"한다고 계산 | 수동 CTLE는 저주파를 깎는다; 절대 잡음은 줄 수도 있음 | SNR(σ/h₀)로 비교 |
| ZF FFE를 eye 최적으로 착각 | h±1만 0, 꼬리·위상 무시 (128.6 vs 격자 131.3 mV) | eye 지표를 직접 최대화 (연습 2) |
| ZF 결과를 FFE 후 peak에서 검증 | FFE가 peak 위상을 옮김 → h±1 ≠ 0으로 보임 | 설계 위상(원래 peak + 1 UI)에서 검사 |
| 등화기를 각각 최적화 후 쌓기 | 같은 ISI를 두 번 지움 (97.2 < 167.1 mV) | 합동 스윕 / 링크 트레이닝 |
| DFE로 pre-cursor를 지우려 함 | 미래 비트는 결정 전 | TX preshoot(c₋₁) 또는 CTLE |
| DFE eye를 genie로만 평가 | 오류 전파로 BER ×1.47 (w₁/h₀ = 0.49) | 실제 결정 피드백 시뮬레이션, 버스트 통계 |
| noise 2Qσ를 eye 한쪽에서만 뺌 | 위·아래 레벨 양쪽이 닫힘 | EH_ber = EH − 2·Q·σ |
| echo를 1회 반사(2항)로 피팅 | 다중 반사를 흡수하며 τ가 틀림 (23 vs 30 샘플) | `(a + bz)/(1 + cz)` IIR 꼴 최소제곱 |
| 노치 레인을 EQ 튜닝으로 끝까지 붙잡기 | 70개 설정 전부 FAIL — 보드 문제 | echo 위치 → 스텁/BOM 확인, TDR |

## 8. 면접 Q&A

<details>
<summary>Q1. pulse response에서 h₋₁ = 0.13·h₀, h₁ = 0.49·h₀입니다. 어떤 등화기를 어떻게 배치하겠습니까?</summary>

h₁이 크니 DFE tap-1이 가장 효율적이지만, w₁/h₀ ≈ 0.5면 tap-1 타이밍(1 UI 안에 닫힘)과 오류 전파(이 레슨 ×1.47)가 부담이다. 그래서 CTLE로 먼저 h₁을 줄이고(−7 dB에서 0.28·h₀) 남은 post-cursor를 1–3 tap DFE로, pre-cursor는 TX preshoot(c₋₁)로 처리한다. 실제 단계표: CTLE −7 dB 93.9 mV → +DFE1 159.0 mV @1e-12.
꼬리질문: "DFE만 쓰면 숫자는 182.8 mV로 더 좋던데요?" → 이상 조건(무제한 tap 범위, 결정 오류 없음)의 숫자다. tap 범위 한계(Gen3 기준 수신기는 1 tap ±30 mV 수준 — 대표값), tap-1 투기 회로 비용(비교기 2배), 오류 전파, 긴 꼬리에 필요한 tap 수 때문에 실제 수신기는 CTLE+짧은 DFE 구조다.

</details>

<details>
<summary>Q2. CTLE가 "노이즈를 증폭한다"는 말을 정확히 설명해 보세요.</summary>

수동 CTLE는 DC를 A(예: −7 dB)로 깎고 중간 대역은 ≈ 0 dB로 둔다 — 절대 이득을 올리지 않는다. 이 레슨에서 σ_out은 오히려 1.62 → 1.36 mV로 줄었다. 하지만 신호의 main cursor h₀도 저주파 성분이 깎여 171 → 101 mV로 더 빨리 줄어 **σ/h₀가 0.0095 → 0.0134로 증가** 한다. 즉 신호 대비 고주파 잡음·크로스토크 비중이 커진다. 능동 CTLE(이득 단 포함)면 고주파 절대 이득도 > 0 dB라 말 그대로 증폭한다.
꼬리질문: "그럼 DFE는 왜 잡음을 안 키우나?" → 빼는 값이 아날로그 신호를 필터링한 게 아니라 결정된 비트(디지털)에 상수 tap을 곱한 것이라 잡음이 실리지 않는다. 대신 결정이 틀리면 오류가 전파된다.

</details>

<details>
<summary>Q3. DFE 오류 전파는 왜 생기고 얼마나 나쁜가요? 줄이는 방법은?</summary>

틀린 결정이 피드백되면 다음 샘플에서 2·w₁만큼 잘못 빼서, 비트가 바뀌는 경우(확률 ½) 마진이 h₀ − 2w₁로 줄어든다. w₁/h₀ = 0.49면 마진이 거의 0이라 다음 비트도 약 절반 확률로 틀린다 → 측정: 평균 버스트 1.47, BER ×1.47. w₁/h₀ = 0.28이면 ×1.08. 줄이는 법: CTLE/TX FFE로 w₁/h₀를 낮춤, tap 범위 제한, FEC가 버스트를 견디게 interleave, PAM4에서는 1/(1+D) precoding으로 버스트를 2개 오류로 끊음(see:NETHW).
꼬리질문: "BER 측정에서 버스트는 왜 문제인가?" → 오류가 독립이라는 가정(BER 신뢰구간, Q-scale 외삽 — HWLAB-M11)이 깨진다. FEC 코드워드 하나에 오류가 몰리면 정정 한계를 넘는다.

</details>

<details>
<summary>Q4. 랩에서 PRBS7로 본 eye는 30 mV 열렸는데 컴플라이언스 도구는 FAIL이라고 합니다. 누가 맞나요?</summary>

채널 ISI가 7 UI보다 길면 PRBS7(127비트 주기)에는 최악 패턴이 없다. 이 레슨: no EQ에서 PRBS7 22.9 → PRBS9 −0.7 → PRBS11 −16.7 → PDA −54.6 mV, L2 레인은 PRBS7 37.3 mV(PASS처럼 보임) vs PDA 17.4 mV(FAIL). 40비트 최악 패턴이 나올 확률은 2⁻⁴⁰ ≈ 1e−12 수준이라 BER 1e−12 판정에서는 무시할 수 없다. 컴플라이언스 쪽(통계적 eye — cursor 분포의 컨볼루션)이 맞다. 랩 측정은 PRBS31 또는 실제 트래픽(스크램블된 128b/130b)으로 해야 한다.
꼬리질문: "PDA는 너무 비관적이지 않나?" → PDA는 확률 0 경계다. 꼬리 cursor가 아주 많으면 그 패턴의 확률이 1e−12보다 훨씬 작아 과도하게 비관적일 수 있다 — 그래서 실무 지표는 통계적 eye의 1e−12 등고선이다.

</details>

<details>
<summary>Q5. TX de-emphasis −6 dB, preshoot 3.5 dB(P7)를 tap 계수로 바꿔 보세요.</summary>

|c₋₁| + c₀ + |c₊₁| = 1, a = |c₋₁|, b = |c₊₁|. 정상 상태 Vb = 1 − 2a − 2b, 전이 직후 Va = 1 − 2a, 전이 직전 Vc = 1 − 2b. DE = 20log(Vb/Va) = −6 dB → Vb/Va = 0.5, PS = 20log(Vc/Vb) = 3.5 dB → Vc/Vb = 1.496. 풀면 a = 0.1, b = 0.2 → (−0.1, 0.7, −0.2). 검산: Vb = 0.4, Va = 0.8(−6.0 dB), Vc = 0.6(+3.5 dB).
꼬리질문: "de-emphasis를 하면 왜 eye가 열리는데 신호는 작아지나?" → 고주파(전이) 레벨은 그대로 두고 저주파(연속 비트) 레벨을 깎아 채널 기울기를 보상한다. 수신 eye의 절대 진폭은 줄지만 ISI가 더 많이 줄면 eye가 커진다. 이 레슨의 L0는 FFE 없는 P4가 최선이었다 — 손실이 CTLE+DFE로 충분히 처리되면 FFE의 스윙 손실이 더 크다.

</details>

<details>
<summary>Q6. 특정 레인만 모든 EQ 설정에서 실패합니다. pulse response에서 무엇을 보고 보드의 어디를 의심하나요?</summary>

같은 길이의 정상 레인과 pulse response를 겹쳐 본다. 두 번째 봉우리(echo)가 있으면 반사 — 지연 τ에서 거리 L = v·τ/2(v ≈ c/√εeff, FR-4 스트립라인 약 15 cm/ns). 이 레슨: τ = 117.2 ps → 8.8 mm, 분기점 Γ ≈ −1/3이면 T 분기(스텁), 1/4 파장 노치 f = 1/(2τ) = 4.27 GHz가 Nyquist 근처라 치명적. 의심 위치: DNP여야 할 0 Ω/바이패스 패드, 백드릴 안 된 via stub, 테스트 포인트 분기. TDR로 위치를 확인하고(HWLAB-M01) 고친 뒤 같은 스윕으로 닫는다.
꼬리질문: "via stub도 같은 원리인가?" → 같다. 두꺼운 보드에서 신호가 윗층으로 빠지면 아래쪽 via barrel이 열린 스텁이 되어 f = c/(4·L·√εeff)에서 노치 — 수 mm면 수~십수 GHz라 25G+ 링크에서 back-drill을 지정하는 이유다.

</details>

<details>
<summary>Q7. CTLE를 디지털로 시뮬레이션할 때 bilinear 변환의 함정은?</summary>

주파수 휨: 아날로그 ω가 `(2/T)·tan(ωT/2)` 로 매핑된다. 이 레슨은 fs = 256 GHz라 4 GHz에서 휨이 0.08 %, 측정 오차 0.0005 dB로 무시 가능. 샘플링이 거칠면(예: 2 샘플/UI) zero/pole을 prewarp해야 한다. 또 bilinear는 안정성·최소위상을 보존하지만 임펄스 응답 모양은 근사다(같은 시간 응답을 원하면 impulse/step-invariant).
꼬리질문: "IBIS-AMI 모델의 CTLE는?" → 벤더가 주는 Rx 알고리즘 모델(AMI)로 채널 시뮬레이터에서 돌리며, 보통 주파수 응답 표나 zero/pole 파라미터로 기술된다. 측정 eye와 시뮬레이션을 상관시킬 때 같은 CTLE 설정 코드를 맞추는 게 첫 단계다.

</details>

## 9. 추가 연습

### 연습 1 — 패턴 길이와 eye

PRBS7/9/11 LFSR(`x⁷+x⁶+1`, `x⁹+x⁵+1`, `x¹¹+x⁹+1`)을 만들어 no EQ와 CTLE −7 dB 펄스의 eye를 다시 잰다. EH(PRBS7) ≥ EH(PRBS9) ≥ EH(PRBS11) ≥ PDA를 확인한다.

<details>
<summary>답 보기</summary>

피보나치 LFSR: `bit = (r >> (a−1)) ^ (r >> (b−1)) & 1`, `r = (r << 1 | bit) & mask`. 한 주기에서 +1은 2^(n−1)개(균형). 실제 출력: no EQ 22.9 / −0.7 / −16.7 / PDA −54.6 mV, CTLE −7 dB 138.3 / 133.1 / 126.8 / 113.6 mV. 등화가 ISI를 줄일수록 패턴 의존성이 작아진다(PRBS11과 PDA 차이 13.2 mV). 주기 PRBS로 계산하므로 순환 컨볼루션이 정확하다 — 단 pulse 길이(49 UI)가 주기보다 짧아야 한다(PRBS7 127 UI > 49 ✓).

</details>

### 연습 2 — eye를 직접 최대화하는 TX FFE

c₋₁ ∈ [−0.2, 0], c₊₁ ∈ [−0.35, 0]을 0.025 간격으로 훑어 @1e-12 PDA eye가 최대인 조합을 찾고 ZF와 비교한다.

<details>
<summary>답 보기</summary>

135개 조합, 실제: ZF (−0.081, 0.626, −0.293) → 128.6 mV / 0.642 UI, 격자 최적 (−0.050, 0.650, −0.300) → 131.3 mV / 0.675 UI. ZF는 h±1 두 점만 0으로 만들고 h±2 이후와 위상 민감도(eye width)를 보지 않는다. 실제 PCIe 링크 트레이닝도 상대 수신기가 eye/BER 지표로 preset이나 계수를 평가해 요청하는 탐색이지 ZF 공식이 아니다. 같은 이유로 실리콘의 DFE/FFE 적응은 ZF보다 LMS(sign-sign LMS)처럼 오차를 줄이는 방향을 많이 쓴다.

</details>

### 연습 3 — CTLE zero·peak·peaking

`ctle_peaking(adc)` → (fz, f_peak, peaking dB = 20log(|H|max/|H(0)|)). ADC −6/−9/−12 dB에서 출력한다.

<details>
<summary>답 보기</summary>

실제: −6 dB → fz 1.002 GHz, peak 3.57 GHz, peaking 4.36 dB / −9 dB → 0.710 GHz, 3.79 GHz, 7.20 dB / −12 dB → 0.502 GHz, 3.90 GHz, 10.13 dB. peaking은 |ADC|보다 작다 — 두 번째 극(8 GHz)과 첫 극(2 GHz)이 고주파 이득을 1 아래로 묶기 때문이다(|H(4 GHz)| ≈ −1.7…−1.9 dB). 데이터시트의 "peaking 10 dB"가 DC 대비인지, 어느 주파수 기준인지 확인해야 하는 이유.

</details>

### 연습 4 — tap-1 투기(unrolled) DFE

tap-1 피드백 루프를 끊기 위해, 직전 결정이 +1인 경우와 −1인 경우의 두 비교 결과를 미리 내고 직전 결정으로 MUX한다. 같은 seed에서 `dfe_sim` 과 오류 위치가 완전히 같은지 확인한다.

<details>
<summary>답 보기</summary>

`if_prev_pos = sign(r − w₁)`, `if_prev_neg = sign(r + w₁)`, `d_n = if_prev_pos if d₍ₙ₋₁₎ > 0 else if_prev_neg`. 수학적으로 같은 결정이다 — 실제: 직접 151개 = 투기 151개, 위치 동일. 하드웨어 이득: 크리티컬 패스가 "곱·뺄셈·비교"에서 "MUX 하나"로 줄어 1 UI(8 GT/s에서 125 ps) 타이밍을 맞춘다. 비용: tap-1당 비교기 2배(N tap 투기면 2^N). 그래서 보통 tap-1(때로 tap-2)만 투기하고 나머지 tap은 일반 피드백으로 둔다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 9/9 passed (0.8 s), exit=0
```

## 10. 핵심 정리

- 손실 채널 = 저역통과 FIR. pulse response를 UI 간격으로 샘플한 cursor가 ISI다: h₀(신호), h₋₁(pre), h₁, h₂…(post). 이 레슨 −15 dB: h₋₁/h₀ 0.13, h₁/h₀ 0.49.
- 중첩: y = Σ bⱼ·p(t − jUI). PRBS eye는 패턴에 의존(PRBS7 낙관적), PDA `2(h₀ − Σ|hₖ|)` 는 최악 경계. BER eye = EH − 2·Q·σ, Q(1e-12) = 7.034.
- TX FFE: |c| 합 = 1 제약 → 저주파를 깎음(de-emphasis), 잡음 증폭 없음, 스윙 손실. ZF ≠ eye 최적.
- CTLE: ωp2(s + Aωp1)/((s+ωp1)(s+ωp2)) — DC를 A로 깎는 기울기 보정. 최적 ADC가 있고(−7 dB), 과등화하면 h₁ 음수·σ/h₀ 증가.
- DFE: post-cursor만, 잡음 증폭 없음. 대가 = 오류 전파(w₁/h₀ 0.49 → ×1.47, 버스트 1.47), tap-1 1 UI 타이밍(투기로 해결), tap 범위.
- 등화기는 합동 최적화: 따로 최적화해 쌓으면 과등화(97.2 < 167.1 mV).
- 모든 EQ 설정에서 실패하는 레인 = 보드 문제. echo 지연 τ → L = v·τ/2, 1/4 파장 노치 1/(2τ). 리포트는 "최선 설정의 수치 + 골든 대비 echo + 의심 부품 + 등화로는 해결 불가".

## 참고자료

- PCI-SIG, *PCI Express Base Specification* Rev 3.0 이상 — 8 GT/s TX preset 표(P0–P10), 계수 공간(FS/LF), 기준 수신기 CTLE·DFE, RX eye 요구사항, Lane Equalization Control 레지스터 (이 레슨의 수치는 대표값 — 원문 확인)
- Hall & Heck, *Advanced Signal Integrity for High-Speed Digital Designs* (Wiley, 2009) — 손실 선로 모델, pulse response, PDA·통계적 eye, 등화기
- Eric Bogatin, *Signal and Power Integrity — Simplified* — 반사, 스텁, 손실과 상승 시간
- Howard Johnson & Martin Graham, *High-Speed Signal Propagation: Advanced Black Magic* — 표피효과 선로의 erfc step 응답, 유전 손실
- Behzad Razavi, *Design of Integrated Circuits for Optical Communications* 2nd ed. 및 그의 SSC Magazine "Circuit Intuitions" 칼럼 — CTLE, DFE, loop unrolling
- B. Casper, F. O'Mahony, "Clocking Analysis, Implementation and Measurement Techniques for High-Speed Data Links — A Tutorial," *IEEE TCAS-I*, 2009 — 링크 모델·eye·BER
- Keysight / Tektronix / Teledyne LeCroy의 SerDes 등화·eye 분석 애플리케이션 노트 (CTLE/DFE 에뮬레이션, 통계적 eye)
- 관련 레슨: HWLAB-M01(TDR·반사·종단), HWLAB-M09(eye diagram·골든 PLL·마스크 — 여기 eye 도구의 원본), HWLAB-M10(RJ/DJ·dual-Dirac), HWLAB-M11(BER bathtub·Q-scale), HWLAB-A02(lane margining 2D eye), HWLAB-B04(IEEE-181 측정), see: NETHW(PAM4·FEC·precoding)
