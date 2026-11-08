---
id: HWLAB-B03
time: 70분
updated: 2026-11-08
---
# HWLAB-B03 I2C 풀업 저항 계산: 버스 커패시턴스·Rise Time

## 학습 목표

- UM10204 스펙 표(tr max, VOL@IOL, Cb max)에서 **허용 풀업 창** `Rp(min) = (VDD−VOL)/IOL`, `Rp(max) = tr/(0.8473·Cb)` 를 유도하고, 창이 닫히는 Cb(`Fm 3.3 V → 366 pF`)를 계산할 수 있다.
- 0.8473 = ln(0.7/0.3) 이 어디서 오는지, 왜 I2C rise time은 **0.3·VDD → 0.7·VDD (절대 레벨)** 인지, 스코프 기본값 10–90 % 와 무엇이 다른지(2.593배) 설명할 수 있다.
- 캡처 파형에서 rise time을 **보간 + 히스테리시스 무장 + 이동평균** 으로 재고, 샘플 간격·잡음·잘린 캡처가 만드는 거짓 숫자를 테스트로 보여 줄 수 있다.
- 측정된 tr과 회로도 Rp로 **버스 C를 역산** 하고, 예산과 비교해 DNP 풀업·이중 풀업 같은 조립 결함을 짚어 내는 bring-up 리포트를 쓸 수 있다.

## 1. 문제

첫 보드가 들어온 날 I2C가 "가끔" 실패하면 회로도 검토, 스코프 측정, 저항 교체까지 이어지는 디버그의 출발점이 바로 풀업 계산이다. Rivian/NVIDIA/Apple 검증 면접의 단골 질문 "풀업 몇 옴 쓰세요? 근거는요? 스코프에서 무엇을 봤나요?"의 답이 이 레슨이다.

I2C의 SDA/SCL은 **오픈드레인(open-drain)** 이다. 디바이스는 라인을 LOW로 끌어내리기만 하고, HIGH는 풀업 저항 Rp가 버스 커패시턴스 Cb를 충전해서 만든다. 그래서 두 개의 제약이 서로 반대 방향으로 Rp를 누른다.

- **Rp가 너무 작으면**: 가장 약한 디바이스가 `VDD/Rp` 전류를 sink 하면서 VOL 이하로 못 내린다 → LOW 노이즈 마진 상실 (HWLAB-B02 참고).
- **Rp가 너무 크면**: RC 충전이 느려 rise time이 스펙(Fm 300 ns)을 넘는다 → SCL HIGH 시간 잠식, 잡음에 따른 이중 클럭.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">오픈드레인 I2C 라인 한 가닥 = 풀업 Rp + 모든 커패시턴스의 합 Cb</text>
  <line x1="40" y1="45" x2="700" y2="45" stroke="currentColor" stroke-width="2"/>
  <text x="44" y="38" fill="currentColor">VDD (3.3 V / 1.8 V)</text>
  <rect x="78" y="60" width="24" height="56" class="f-acc" stroke="currentColor"/>
  <line x1="90" y1="45" x2="90" y2="60" stroke="currentColor"/>
  <line x1="90" y1="116" x2="90" y2="140" stroke="currentColor"/>
  <text x="108" y="92" fill="currentColor">Rp</text>
  <line x1="40" y1="140" x2="700" y2="140" stroke="currentColor" stroke-width="3"/>
  <text x="600" y="132" fill="currentColor" font-weight="bold">SCL / SDA</text>
  <g stroke="currentColor" fill="none">
    <line x1="180" y1="140" x2="180" y2="170"/><rect x="160" y="170" width="40" height="34" class="f-mut"/>
    <line x1="300" y1="140" x2="300" y2="170"/><rect x="280" y="170" width="40" height="34" class="f-mut"/>
    <line x1="420" y1="140" x2="420" y2="170"/><rect x="400" y="170" width="40" height="34" class="f-mut"/>
    <line x1="530" y1="140" x2="530" y2="175"/>
    <line x1="515" y1="175" x2="545" y2="175"/><line x1="515" y1="183" x2="545" y2="183"/>
    <line x1="530" y1="183" x2="530" y2="220"/>
    <line x1="640" y1="140" x2="640" y2="175"/>
    <line x1="625" y1="175" x2="655" y2="175"/><line x1="625" y1="183" x2="655" y2="183"/>
    <line x1="640" y1="183" x2="640" y2="220"/>
    <line x1="140" y1="220" x2="680" y2="220"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="180" y="192">MCU</text><text x="300" y="192">PMIC</text><text x="420" y="192">센서</text>
    <text x="180" y="236">오픈드레인 FET</text><text x="300" y="236">Ci ≤ 10 pF/핀</text><text x="420" y="236">Ron (VOL@IOL)</text>
    <text x="530" y="236">배선·커넥터·FPC</text><text x="640" y="236">스코프 프로브</text>
    <text x="530" y="252">~1 pF/cm + α</text><text x="640" y="252">10x 패시브 ≈ 10~15 pF</text>
  </g>
  <text x="10" y="274" fill="currentColor" font-size="12">HIGH: Rp 가 Cb 를 충전 → v(t) = VDD − (VDD − V0)·e^(−t/(Rp·Cb))</text>
  <text x="10" y="292" fill="currentColor" font-size="12">LOW: 가장 약한 드라이버가 (VDD − VOL)/Rp 를 sink 해야 한다</text>
</svg>
```

UM10204(NXP I2C-bus specification)의 SDA/SCL 버스 라인 특성 표에서 이 레슨이 쓰는 값:

| 모드 | 클럭 | tr max (0.3→0.7·VDD) | tf max | Cb max | VOL1 (VDD > 2 V) | IOL @ VOL1 |
|---|---|---|---|---|---|---|
| Standard (Sm) | 100 kHz | 1000 ns | 300 ns | 400 pF | 0.4 V | 3 mA |
| Fast (Fm) | 400 kHz | 300 ns | 300 ns | 400 pF | 0.4 V | 3 mA |
| Fast-mode Plus (Fm+) | 1 MHz | 120 ns | 120 ns | 550 pF | 0.4 V | 20 mA |

VDD ≤ 2 V에서는 VOL2 = 0.2·VDD @ 2 mA 행을 쓴다 (표에서 Fm/Fm+ 열에 값이 있고 Sm 열은 비어 있다 — 이 레슨은 Sm에도 같은 값을 **가정** 한다). VIL = 0.3·VDD, VIH = 0.7·VDD 이므로 tr도 그 두 레벨 사이로 정의된다.

### Part A — 스펙 표 → 풀업 창, 창이 닫히는 Cb, RC 에지의 30–70 % rise time

- `vol_iol(mode, vdd)` — VDD > 2 V면 표의 (0.4 V, 3/20 mA), VDD ≤ 2 V면 (0.2·VDD, 2 mA).
- `rp_min(mode, vdd) = (VDD − VOL)/IOL`, `rp_max(mode, cb) = tr_max/(0.8473·Cb)`, `rp_window`.
- `cb_close(mode, vdd)` — Rp_min = Rp_max 가 되는 Cb (해석해). `sweep_close(mode, vdd, cbs)` — **Cb 50~400 pF를 10 pF 간격으로 훑어 창이 처음 닫히는 지점** (Fm 3.3 V → 370 pF, Fm 5 V → 240 pF, Sm 3.3 V / Fm+ 3.3 V → 열린 채로 끝).
- `cross_time`, `moving_average`, `measure_rise_times(t, v, vdd, lo_frac=0.3, hi_frac=0.7, smooth=1)` — 합성 RC 에지(2.2 kΩ × 100 pF)에서 `0.8473·RC = 186.4 ns` 를 재야 한다.

### Part B — 측정이 거짓말하는 경우

같은 `measure_rise_times` 가 아래를 모두 견뎌야 한다 (각각 체크 테스트가 있다).

1. **10–90 % 기본값**: 30–70 %로 250 ns(PASS)인 에지가 스코프 기본 10–90 %로는 648 ns → 300 ns 스펙에 대해 **가짜 FAIL**. 비율 ln 9 / ln(7/3) = 2.593.
2. **시작 레벨**: LOW 레벨이 0 V든 0.6 V든 30–70 % 간격은 같다 (지수 함수의 성질 — 풀이에서 증명).
3. **샘플 간격**: 50 MS/s(20 ns) 캡처에서 "임계값 넘은 첫 샘플" 방식은 오차가 샘플 간격 수준, 선형 보간은 1 ns 미만.
4. **잡음**: σ = 40 mV, 느린 Sm 에지(τ = 1 µs)에서 0.7·VDD를 13번 오르내린다. 에지는 1개로 세야 하고, 잡음 편향을 이동평균으로 없애야 한다.
5. **잘린 캡처**: 0.7·VDD에 도달하기 전에 끝난 에지는 숫자를 지어내지 말고 빈 목록.
6. **1.8 V 버스**: 0.4 V@3 mA를 그대로 쓰면 Rp_min = 467 Ω, 스펙 VOL2(0.36 V@2 mA)로는 720 Ω.

### Part C — 가상 보드 SNS-EVT1의 I2C 4개 버스 bring-up 리포트

`buses.json` 은 회로도·BOM이 **주장하는** 값(VDD, 모드, Rp와 참조 번호, Cb 예산 = 디바이스 수 × Ci + 배선 cm × pF/cm + 커넥터/FPC)이다. `captures/*.csv` 는 10x 패시브 프로브(12 pF)로 각 버스를 찍은 8비트 스코프 캡처다 (`gen_captures.py` 가 결정적으로 생성, 숨은 정답은 생성기 안에만 있다).

`analyze_bus(ch, t, v, probe_f)` 가 버스마다 계산한다:

| 값 | 식 | 의미 |
|---|---|---|
| `tr_meas` | 30–70 % 측정 (중앙값) | 프로브가 달린 상태의 rise time |
| `tr_bus` | `tr_meas − 0.8473·Rp·C_probe` | 프로브를 떼면 될 버스 자체 rise time |
| `cb_impl` | `tr_meas/(0.8473·Rp_sch) − C_probe` | 회로도 Rp가 맞다고 믿을 때 버스 C |
| `rp_impl` | `tr_meas/(0.8473·(Cb_budget + C_probe))` | Cb 예산이 맞다고 믿을 때 실효 Rp |

판정: `FAIL_TR`(tr_bus > tr max), `FAIL_VOL`(LOW 레벨 > VOL max), `SUSPECT_RP_HIGH/LOW`(cb_impl이 예산의 1.6배 초과 / 1/1.6 미만 → Rp 자체가 회로도와 다르다), `FAIL_TR` 이면서 의심이 없으면 `choose_e24` 로 **E24 ±1 %, Cb +20 % 마진** 을 지키는 가장 큰 저항을 추천한다. 4개 중 1개만 PASS이고 나머지 3개는 원인이 각각 다르다.

## 2. Problem (English)

> **I2C pull-up sizing and rise-time verification.**
>
> Using only the Python 3 standard library and the UM10204 bus-line limits (Sm/Fm/Fm+: tr max 1000/300/120 ns,
> VOL 0.4 V at IOL 3/3/20 mA, Cb max 400/400/550 pF; for VDD ≤ 2 V use VOL = 0.2·VDD at 2 mA):
>
> 1. `rp_min(mode, vdd) = (VDD − VOL)/IOL`, `rp_max(mode, cb) = tr_max / (ln(0.7/0.3)·Cb)`, and the bus capacitance
>    at which `rp_min > rp_max` (window closed). Sweep Cb = 50..400 pF in 10 pF steps and report the first closed point.
> 2. `measure_rise_times(t, v, vdd, lo_frac=0.3, hi_frac=0.7, smooth=1) -> list[float]`: one value per rising edge,
>    from the last upward crossing of `lo` to the first upward crossing of `hi`, with linear interpolation between samples,
>    re-arming only after the signal drops below `lo`, optional centered moving average, and no result for an edge that
>    never reaches `hi`.
> 3. Given per-bus schematic data (VDD, mode, Rp, capacitance budget) and a scope capture taken with a 12 pF probe,
>    report measured tr, tr without the probe, low level, implied Cb, implied Rp, and flags
>    `FAIL_TR / FAIL_VOL / SUSPECT_RP_HIGH / SUSPECT_RP_LOW`; recommend the largest E24 (±1 %) resistor that meets tr
>    with Cb + 20 % margin when the only problem is a too-large Rp.
>
> Example: Fm at 3.3 V → Rp_min = 966.7 Ω; at Cb = 100 pF, Rp_max = 3541 Ω; the window closes at Cb ≈ 366 pF.
> A 2.2 kΩ × 100 pF edge has a 30–70 % rise time of 186.4 ns.

## 3. 시작 코드

<!-- code: starter.py -->

회로도 쪽 값 (`buses.json`) 과 캡처 생성기:

<!-- code: buses.json -->

<!-- code: gen_captures.py -->

```bash
cd lessons/HWLAB/HWLAB-B03
python3 -I gen_captures.py     # captures/*.csv 재생성 (이미 커밋돼 있음, 바이트 단위로 같게 나온다)
python3 -I starter.py          # TODO 상태: 2/29 passed, exit 1
```

실제 출력 (일부):

```text
-- Part A: spec window --
PASS  K = ln(0.7/0.3) = 0.8473
FAIL  Rp_min(Fm, 3.3V) = None (expect 966.7ohm)
...
FAIL  RC edge 2.2k*100pF: 30-70 tr = None (expect 186.4ns)
-- Part B: why the naive number lies --
FAIL  30-70 tr of Fm edge = None (expect 249.9ns) <= 300ns  (TypeError: 'NoneType' object is not subscriptable)
...
FAIL  noise 40mV: 13 raw up-crossings of 0.7*VDD, but exactly 1 edge (arm below 0.3*VDD)
-- Part C: SNS-EVT1 bring-up captures --
FAIL  I2C0 tr_meas = None (expect 167.8ns)
...
2/29 passed
```

> 눈여겨볼 것: stub `sweep_close` 가 `None` 을 돌려주므로 "Fm+ 3.3 V는 550 pF까지 열려 있다(None)" 테스트는 **공짜로 PASS** 한다. 그래서 같은 함수에 "Fm 3.3 V는 370 pF에서 닫힌다" 라는 양성 테스트를 같이 둔다. "닫히지 않음"만 확인하는 테스트는 아무것도 증명하지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — Rp_max의 0.8473은 어디서?</summary>

v(t) = VDD(1 − e^(−t/RC)) 에서 v = a·VDD 가 되는 시각은 t = RC·ln(1/(1−a)). 0.7과 0.3의 차를 구하면 RC·[ln(1/0.3) − ln(1/0.7)] = RC·ln(0.7/0.3). 이 값이 tr_max 이하여야 하므로 R ≤ tr_max/(0.8473·C).

</details>

<details>
<summary>힌트 2 — 창이 닫히는 Cb</summary>

(VDD−VOL)/IOL = tr/(K·Cb) 를 Cb에 대해 풀면 Cb = tr·IOL / (K·(VDD−VOL)). Fm 3.3 V: 300 ns × 3 mA / (0.8473 × 2.9 V) = 366 pF. **VDD가 높을수록 일찍 닫힌다** (Rp_min이 커지므로).

</details>

<details>
<summary>힌트 3 — 교차 시각과 무장(arm)</summary>

인접 샘플 (a, b) 에서 `a < lo <= b` 면 lo를 위로 지난 것이다. 시각은 `t0 + (lo − a)·dt/(b − a)`. 잡음이 lo 근처에서 여러 번 오르내리면 **마지막** 교차를 쓰고(새 교차가 나오면 덮어씀), hi를 처음 넘는 순간 하나의 에지로 확정한다. 다음 에지는 신호가 lo 아래로 다시 내려가야 무장된다.

</details>

<details>
<summary>힌트 4 — 이동평균이 rise time을 바꾸지 않는 이유</summary>

중심 boxcar(폭 W)로 e^(−t/τ) 를 평균하면 e^(−t/τ)·sinh(W/2τ)/(W/2τ) — 상수 배다. 지수에서 상수 배는 **시간 이동** 이므로 두 교차 시각이 같이 밀리고 간격은 그대로다. 단, 창이 에지 시작점(꺾이는 곳)을 걸치지 않도록 W ≪ τ.

</details>

<details>
<summary>힌트 5 — Part C의 역산</summary>

tr = K·Rp·(Cb + C_probe) 하나의 식에 미지수가 둘이다. 회로도 Rp를 믿으면 Cb가 나오고, Cb 예산을 믿으면 Rp가 나온다. 둘 중 하나가 **말이 안 되는 값** (예산 48 pF인 버스가 1.1 nF?) 이면 반대쪽이 틀린 것이다. 추천 저항은 E24 값 r에 대해 `r(1−tol) ≥ Rp_min` 이고 `r(1+tol) ≤ Rp_max(Cb·1.2)` 인 최댓값.

</details>

## 5. 풀이

### Part A — 두 제약, 하나의 창

**Rp(min) — DC 제약.** LOW일 때 Rp에 흐르는 전류 (VDD − VOL)/Rp를 버스의 **가장 약한** 디바이스가 sink 하면서 VOL 이하를 유지해야 한다. 스펙은 "VOL = 0.4 V일 때 최소 3 mA sink"(Fm+는 20 mA)를 보장하므로 Rp ≥ (VDD − 0.4)/IOL. 3.3 V Fm이면 967 Ω, Fm+이면 145 Ω. Fm+ 디바이스를 같은 버스에 둔다고 Rp_min이 내려가는 게 아니다 — **버스에 있는 모든 디바이스가** 20 mA를 sink 할 수 있어야 145 Ω을 쓴다.

**Rp(max) — AC 제약.** 오픈드레인이 놓으면 라인은 V0(≈VOL)에서 VDD로 RC 충전된다. 0.3·VDD와 0.7·VDD를 지나는 시각의 차는 V0와 무관하게 RC·ln(0.7/0.3) (Part B에서 증명). 그래서 Rp ≤ tr_max/(0.8473·Cb).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Fm 풀업 창: Rp_max(Cb) 곡선 아래, Rp_min 선 위 (Y는 로그 눈금)</text>
  <line x1="80" y1="40" x2="80" y2="260" stroke="currentColor"/>
  <line x1="80" y1="260" x2="690" y2="260" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="74" y="44">10k</text><text x="74" y="95">5k</text><text x="74" y="162">2k</text>
    <text x="74" y="213">1k</text><text x="74" y="264">500</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="276">0</text><text x="230" y="276">100</text><text x="380" y="276">200</text>
    <text x="530" y="276">300</text><text x="680" y="276">400 pF</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 4" opacity=".35">
    <line x1="80" y1="91" x2="690" y2="91"/><line x1="80" y1="158" x2="690" y2="158"/><line x1="80" y1="209" x2="690" y2="209"/>
    <line x1="230" y1="40" x2="230" y2="260"/><line x1="380" y1="40" x2="380" y2="260"/><line x1="530" y1="40" x2="530" y2="260"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="140,49 155,65 170,79 185,90 200,100 215,109 230,116 245,123 260,130 275,136 290,141 305,146 320,151 335,155 350,159 365,163 380,167 395,171 410,174 425,177 440,181 455,184 470,186 485,189 500,192 515,194 530,197 545,199 560,202 575,204 590,206 605,208 620,210 635,212 650,214 665,216 680,218"/>
  <text x="205" y="62" fill="currentColor">Rp_max = 300 ns / (0.8473·Cb)</text>
  <g stroke-width="1.5">
    <line x1="80" y1="178" x2="690" y2="178" stroke="currentColor" stroke-dasharray="6 3"/>
    <line x1="80" y1="212" x2="690" y2="212" stroke="currentColor"/>
    <line x1="80" y1="233" x2="690" y2="233" stroke="currentColor" stroke-dasharray="1 3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="90" y="174">Rp_min 5.0 V = 1533</text>
    <text x="90" y="208">Rp_min 3.3 V = 967</text>
    <text x="90" y="250">Rp_min 1.8 V = 720 (0.2·VDD@2mA)</text>
  </g>
  <circle cx="426" cy="178" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="629" cy="212" r="6" class="f-warn" stroke="currentColor"/>
  <text x="440" y="168" fill="currentColor" font-size="12">5 V: 231 pF에서 닫힘</text>
  <text x="560" y="230" fill="currentColor" font-size="12">3.3 V: 366 pF</text>
  <text x="329" y="100" fill="currentColor" font-weight="bold" text-anchor="middle">✕</text>
  <text x="345" y="100" fill="currentColor" font-size="12">I2C1 실제: 4.7k @ 166 pF (곡선 위 = tr FAIL)</text>
  <circle cx="329" cy="175" r="5" class="f-ok" stroke="currentColor"/>
  <text x="340" y="198" fill="currentColor" font-size="12">1.6k 수정안 (창 안)</text>
</svg>
```

실제 계산값 (테스트로 확인):

| 모드 / VDD | Rp_min | Rp_max @100 pF | 창이 닫히는 Cb (해석해) | 50~400 pF 스윕 결과 |
|---|---|---|---|---|
| Fm / 3.3 V | 966.7 Ω | 3541 Ω | 366.3 pF | **370 pF에서 닫힘** |
| Fm / 5.0 V | 1533 Ω | 3541 Ω | 230.9 pF | **240 pF에서 닫힘** |
| Fm / 1.8 V | 720 Ω | 3541 Ω | 491.8 pF | 열림 (None) |
| Sm / 3.3 V | 966.7 Ω | 11802 Ω | 1221 pF | 열림 (None) |
| Fm+ / 3.3 V | 145 Ω | 1416 Ω | 976.7 pF | 열림 (550 pF까지 None) |

읽는 법:

- **Fm 3.3 V는 스펙 Cb max(400 pF)에 닿기 전, 366 pF에서 이미 저항 풀업으로는 불가능** 하다. "400 pF까지 허용"은 디바이스가 그 부하를 견딘다는 뜻이지, 그 부하에서 아무 저항으로나 스펙이 맞는다는 뜻이 아니다. 대안은 버퍼로 버스 분할, 정전류(액티브) 풀업, 모든 디바이스가 Fm+ 드라이버(20 mA)면 Fm+ 규칙 적용, 또는 속도를 Sm으로 낮추기 (추가 연습 2·3).
- **VDD가 높을수록 창이 좁다** — 5 V 레거시 버스가 가장 먼저 막힌다.
- Fm+는 tr이 2.5배 빡빡하지만 IOL이 6.7배라 창이 오히려 넓다. 이것이 Fm+가 550 pF를 허용할 수 있는 이유다.

**30–70 % 측정.** `measure_rise_times` 는 상태 기계다 (HWLAB-B06의 트리거 상태 기계와 같은 구조).

```svg
<svg viewBox="0 0 740 205" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb03-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">rise time 측정 상태 기계 (lo = 0.3·VDD, hi = 0.7·VDD)</text>
  <rect x="20" y="60" width="150" height="60" rx="10" class="f-mut" stroke="currentColor"/>
  <rect x="290" y="60" width="160" height="60" rx="10" class="f-acc" stroke="currentColor"/>
  <rect x="570" y="60" width="150" height="60" rx="10" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="95" y="86" font-weight="bold">IDLE</text><text x="95" y="106" font-size="12">무장 안 됨</text>
    <text x="370" y="86" font-weight="bold">ARMED</text><text x="370" y="106" font-size="12">t_lo = 마지막 lo↑ 교차</text>
    <text x="645" y="86" font-weight="bold">EMIT</text><text x="645" y="106" font-size="12">tr = t_hi − t_lo</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb03-arr)">
    <path d="M170 80 L288 80"/>
    <path d="M450 80 L568 80"/>
    <path d="M645 120 C645 175, 95 175, 95 122"/>
    <path d="M330 60 C330 30, 410 30, 410 58"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="190" y="72">v &lt; lo</text>
    <text x="470" y="72">a &lt; hi ≤ b (보간)</text>
    <text x="425" y="42">a &lt; lo ≤ b → t_lo 갱신 · v &lt; lo → t_lo 초기화</text>
    <text x="150" y="194">다음 에지는 다시 lo 아래로 내려가야 무장 (잡음이 hi 근처를 오르내려도 에지 1개)</text>
  </g>
</svg>
```

### Part B — 측정이 거짓말하는 여섯 가지 방식

실제 테스트 출력:

```text
PASS  30-70 tr of Fm edge = 250ns (expect 249.9ns) <= 300ns
PASS  scope default 10-90 of SAME edge = 648.2ns (expect 648.2ns) -> false FAIL
PASS  ratio 10-90/30-70 = 2.593 (expect 2.593)
PASS  start level 0.0V vs 0.6V -> same 30-70 tr (exponential)
PASS  dt=20ns: naive max err 10.5ns > 5ns, interpolated 0.24ns < 1ns
PASS  noise 40mV: naive err -5.6..-0.2%, raw last-cross bias -9.2%, boxcar 0.2tau max|err| 0.91% < 1.2%
PASS  noise 40mV: 13 raw up-crossings of 0.7*VDD, but exactly 1 edge (arm below 0.3*VDD)
PASS  truncated capture (ends at 1.04V < 0.7*VDD) -> [] not a fake number
PASS  3 pulses -> 3 rise times
PASS  Rp_min(Fm, 1.8V) uses VOL2=0.2*VDD@2mA = 720ohm (expect 720ohm); naive 0.4V@3mA = 466.7
```

**① 임계값 정의.** 스코프의 자동 "Rise Time" 측정은 보통 파형의 top/base 기준 10–90 %가 기본이다 (20–80 %나 사용자 지정으로 바꿀 수 있다). 단일 극점 RC에서

| 정의 | 간격 | 300 ns 스펙과 비교하면 |
|---|---|---|
| 30–70 % (I2C) | ln(7/3)·τ = 0.8473 τ | 기준 |
| 20–80 % | ln 4·τ = 1.386 τ | 1.64배 과대 |
| 10–90 % (스코프 기본) | ln 9·τ = 2.197 τ | **2.593배 과대** |

그래서 "스코프에서 650 ns가 나와서 Fm FAIL" 이라는 리포트는 측정 기준부터 확인해야 한다. 반대로 tr 0.35/BW 같은 스코프 대역폭 공식은 10–90 % 기준이다 (HWLAB-B05 참고) — 두 세계의 정의를 섞지 않는다.

```svg
<svg viewBox="0 0 740 275" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb03-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 RC 에지(V0 = 0.05·VDD), 다른 임계값: I2C 30–70 % vs 스코프 기본 10–90 %</text>
  <line x1="60" y1="225" x2="720" y2="225" stroke="currentColor"/>
  <line x1="60" y1="35" x2="60" y2="225" stroke="currentColor"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="60,216 120,216 124,210 128,204 132,198 136,192 140,187 144,182 148,177 152,172 156,167 160,163 170,152 180,142 190,134 200,125 210,118 220,111 230,105 240,100 250,95 260,90 270,86 280,82 290,78 300,75 310,72 320,69 330,67 340,65 350,63 360,61 370,59 380,57 390,56 400,55 410,54 420,52 430,51 440,51 450,50 460,49 470,48 480,48 490,47 500,47 510,46 520,46 530,45 540,45 550,45 560,44 570,44 580,44 590,43 600,43 610,43 620,43 630,43 640,43 650,42 660,42 670,42 680,42 690,42 700,42 710,42 720,42"/>
  <g stroke="currentColor" stroke-dasharray="5 4">
    <line x1="60" y1="41" x2="720" y2="41" opacity=".5"/>
    <line x1="60" y1="207" x2="720" y2="207" opacity=".6"/>
    <line x1="60" y1="170" x2="720" y2="170"/>
    <line x1="60" y1="96" x2="720" y2="96"/>
    <line x1="60" y1="59" x2="720" y2="59" opacity=".6"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="64" y="36">VDD</text><text x="64" y="200">V0 = VOL</text>
    <text x="600" y="203">0.1·VDD</text><text x="600" y="166">0.3·VDD = VIL</text>
    <text x="600" y="92">0.7·VDD = VIH</text><text x="600" y="55">0.9·VDD</text>
  </g>
  <rect x="154" y="96" width="93" height="74" class="f-acc" stroke="currentColor" opacity=".7"/>
  <text x="256" y="140" fill="currentColor">tr(30–70) = 0.8473·RpCb</text>
  <line x1="126" y1="242" x2="368" y2="242" stroke="currentColor" marker-start="url(#hwb03-arr2)" marker-end="url(#hwb03-arr2)"/>
  <line x1="126" y1="207" x2="126" y2="248" stroke="currentColor" stroke-dasharray="2 2"/>
  <line x1="368" y1="59" x2="368" y2="248" stroke="currentColor" stroke-dasharray="2 2"/>
  <text x="380" y="247" fill="currentColor" font-size="12">tr(10–90) = 2.197·RpCb — 같은 에지가 2.593배 길게 보인다</text>
  <text x="10" y="268" fill="currentColor" font-size="12">곡선은 v = VDD − (VDD − V0)·e^(−t/τ) 를 정확히 그린 것 (τ = 110 px).</text>
</svg>
```

**② 시작 레벨 무관성 (증명).** v(t) = VDD − (VDD − V0)e^(−t/τ). 레벨 x·VDD를 지나는 시각은 t_x = τ·ln((VDD − V0)/((1 − x)·VDD)). 따라서 t_0.7 − t_0.3 = τ·ln(0.7/0.3) — **(VDD − V0) 항이 소거** 된다 (V0 < 0.3·VDD인 한). 이중 풀업으로 LOW가 0.6 V로 떠도 tr 숫자는 같고, 차이는 지연(t_0.3 자체)과 VOL 마진에서 나타난다. 하강 에지는 목표값이 VOL이라 이 성질이 **없다** (추가 연습 1).

**③ 샘플 간격.** "임계값을 넘은 첫 샘플" 은 교차 시각을 [0, dt) 만큼 늦게 잡는다. 두 교차의 오차가 독립이라 차이는 ±dt까지 흔들린다 (20 ns 샘플링에서 최대 10.5 ns, 위상 20가지 시험). 선형 보간은 곡률 오차만 남아 0.24 ns. 실무 규칙: 에지에 샘플 수십 개 이상이 걸리도록 타임베이스를 잡고, 그래도 보간한다. Part C 캡처는 에지당 약 100~200 샘플이다.

**④ 잡음 편향.** 느린 에지는 기울기가 작아 잡음 σ가 시간 지터 σ/(dv/dt)로 크게 바뀐다. 0.7·VDD 근처 기울기는 0.3·VDD 근처의 3/7이라 더 나쁘다. 무장 없이 상향 교차를 세면 에지 하나에 13개가 나온다. 무장 + 마지막 lo 교차 + 첫 hi 교차는 개수는 맞지만 **lo는 늦게, hi는 일찍** 잡혀 체계적으로 짧아진다 (−9.2 %). 첫 샘플 방식은 두 편향이 우연히 일부 상쇄돼 −5.6..−0.2 %로 흩어진다 — 재현성이 없다. 해법은 측정 **전에** 잡음을 줄이는 것: 중심 이동평균 W = 0.2τ (힌트 4의 증명대로 간격은 보존) → 최대 오차 0.91 %. 스코프에서는 평균 모드(반복 파형), high-res/boxcar 모드, 또는 여러 에지의 통계(평균·최대)를 쓴다.

**⑤ 잘린 캡처.** 0.7·VDD에 도달하지 않은 에지에서 "마지막 샘플까지의 시간" 을 돌려주면 그럴듯한 가짜 숫자가 된다. 측정 불가는 **빈 결과** 여야 하고 리포트는 `NO_EDGE` 로 실패한다.

**⑥ 1.8 V 버스의 VOL.** 0.4 V@3 mA를 1.8 V에 그대로 쓰면 (1.8 − 0.4)/3 mA = 467 Ω. 스펙의 VOL2 행(0.2·VDD = 0.36 V @ 2 mA)으로는 720 Ω. 467 Ω을 달면 2 mA만 보장하는 1.8 V 디바이스가 3.1 mA를 sink 해야 해 VOL이 0.36 V를 넘을 수 있다. 1.8 V 센서 + 3.3 V SoC 사이 레벨 시프터·back-powering은 HWLAB-B02.

### Part C — SNS-EVT1 bring-up 리포트

실제 출력 (`python3 -I solution.py` 마지막 부분):

```text
I2C0_SCL  PASS 3.30V Fm  Rp(sch)=2.2k R101: tr=167.6ns (bus 145.3ns, max 300ns) VOL=0.062V (max 0.40V) Cb~78pF (budget 62pF) Rp_eff~2.7k window[967, 4.5k]@Cb~
I2C1_SCL  FAIL 1.80V Fm  Rp(sch)=4.7k R140: tr=709.2ns (bus 661.4ns, max 300ns) VOL=0.016V (max 0.36V) Cb~166pF (budget 161pF) Rp_eff~4.8k window[720, 2.1k]@Cb~
    FAIL_TR  bus tr 661ns > 300ns -> R140: 4.7k -> 1.6k (E24 1%, Cb+20%): tr~225ns, worst 273ns
I2C2_SCL  FAIL 3.30V Sm  Rp(sch)=2.2k R230: tr=2111.7ns (bus 2089.3ns, max 1000ns) VOL=0.016V (max 0.40V) Cb~1121pF (budget 48pF) Rp_eff~41.5k window[967, 24.6k]@budget
    FAIL_TR  bus tr 2089ns > 1000ns
    SUSPECT_RP_HIGH implied Cb 1121pF >> budget 48pF; Rp_eff~41.5k vs 2.2k -> R230 populated? (DNP, internal pull-up only)
I2C3_SDA  FAIL 3.30V Fm  Rp(sch)=1k R310: tr=39.2ns (bus 29.1ns, max 300ns) VOL=0.594V (max 0.40V) Cb~34pF (budget 77pF) Rp_eff~520 window[967, 4.6k]@budget
    FAIL_VOL low level 0.594V > 0.40V (noise margin, HWLAB-B02)
    SUSPECT_RP_LOW implied Cb 34pF << budget 77pF; Rp_eff~520 < Rp(sch) 1k -> second pull-up in parallel?
```

생성기의 숨은 정답과 비교하면: I2C0 Cb 78 pF(정답 78), I2C1 166 pF(정답 168), I2C2 실효 Rp 41.5 kΩ(정답 40 kΩ), I2C3 실효 Rp 520 Ω(정답 500 Ω = 1 k‖1 k). 예산 오차(I2C0 62 vs 78 pF)는 1.6배 문턱 안이라 의심으로 이어지지 않는다 — 예산은 원래 그 정도로만 맞는다.

| 버스 | 원인 (보드에서 실제로 일어나는 일) | 증상 | 이것을 드러낸 측정 | 조치 |
|---|---|---|---|---|
| I2C1 카메라 | 3.3 V Sm 기본값 4.7 k를 1.8 V + 20 cm FPC 버스에 복사 | 400 kHz에서 간헐적 NACK, 카메라 설정 레지스터 쓰기 실패 | tr 709 ns(> 300), Cb 역산 166 pF ≈ 예산 | R140 → 1.6 k (tr ≈ 225 ns, 최악 273 ns), 또는 100 kHz로 낮추기 |
| I2C2 센서 | R230 DNP(BOM 실수), MCU 내부 풀업(수십 kΩ)만으로 동작 | 상온 벤치에선 "된다", 노이즈 많은 환경·온도에서 비트 에러 | tr 2.1 µs, Cb 역산 1.1 nF ≫ 48 pF → Rp가 틀렸다 | R230 장착 확인 (2.2 k는 예산 기준 창 [967, 24.6 k] 안) |
| I2C3 확장 | 메인 보드와 캐리어 보드에 모두 1 k → 500 Ω | 약한 슬레이브의 ACK LOW가 0.59 V, LOW 노이즈 마진 반토막 | LOW 레벨 0.594 V > 0.4 V, 실효 Rp 520 Ω < Rp_min | 한쪽 DNP, "풀업은 호스트 쪽 한 곳" 규칙 |
| I2C0 PMIC | — | — | tr 168 ns, VOL 0.06 V, 창 안 | 없음 |

추천 저항 규칙: 창 안에서 **가장 큰** 값 = LOW일 때 정적 전류(VDD/Rp)가 가장 작고 VOL이 가장 낮은 값이면서, 저항 +1 %와 Cb +20 % (온도·다른 조립 변형·프로브)에도 tr을 지키는 값. 속도 마진을 최우선으로 하면 반대로 Rp_min 쪽을 고른다 — 면접에서는 어느 쪽을 왜 골랐는지 말하는 것이 핵심이다.

**프로브 보정.** 10x 패시브 프로브 12 pF가 I2C0 버스 C(78 pF)의 15 %다. tr_bus = tr_meas − 0.8473·Rp·C_probe = 167.6 − 22.4 = 145.3 ns. 마진이 작은 버스에서 프로브 때문에 FAIL로 보이는 일이 실제로 있다 — 그래서 리포트에 두 숫자를 다 쓴다.

**장비/도구가 있을 때** (이 레슨의 테스트는 커밋된 합성 캡처로만 돈다):

- 프로브: 1x 패시브는 수십~100 pF 이상이라 측정 대상 자체를 바꾼다 — 쓰지 않는다. 10x 패시브(약 10~15 pF, 그라운드 스프링으로 루프 최소화, HWLAB-B05)나 1 pF 미만 액티브 프로브. 버스에서 **가장 먼 수신기 핀** 근처를 잰다 (분포 정수 효과가 있는 긴 케이블일수록 중요).
- 스코프 측정 기준: 측정 레벨을 **절대값** 0.3·VDD / 0.7·VDD (3.3 V → 0.99 / 2.31 V, 1.8 V → 0.54 / 1.26 V)로 바꾼다. 기본 % 모드는 측정된 top/base 기준이라 VOL이 뜬 버스에서는 기준점까지 바뀐다.
- 통계: 트리거를 SCL 상승 에지(또는 I2C 프로토콜 트리거)로 두고 수백 에지의 **최대값** 을 본다. 평균만 보면 가장 느린 에지(가장 약한 디바이스가 놓는 ACK 직후 등)를 놓친다.
- 자동화: PyVISA로 SCPI 명령을 보내 측정 레벨 설정·파형 전송(waveform data query)을 하고 같은 `analyze_bus` 에 넣는다. 명령 문법은 벤더·모델마다 다르므로 해당 프로그래밍 매뉴얼을 따른다.
- Cb를 LCR 미터로 직접 재는 방법도 있지만, 무전원 보드에서는 디바이스 입력 구조(ESD 다이오드 등)가 동작 상태와 달라 값이 다를 수 있다. 알려진 Rp를 달고 tr로 역산하는 쪽이 동작 조건 그대로다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                     # 29/29 passed, exit 0 (리포트 포함, 약 0.15 s)
python3 -I -W error -X dev solution.py     # 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 습관 |
|---|---|---|
| "I2C는 4.7 k" 를 모든 버스에 | 1.8 V·큰 Cb·400 kHz에서 창 밖 (I2C1: 709 ns) | `rp_window(mode, vdd, cb)` 로 버스마다 계산 |
| Rp_max에 `tr/(Cb)` 또는 `tr/(2.2·Cb)` | 2.2는 10–90 % 상수. I2C는 30–70 % | `tr_max / (math.log(0.7/0.3) * cb)` |
| 스코프 기본 Rise Time(10–90 %)을 300 ns와 비교 | 2.593배 과대 → 좋은 보드를 불량 판정 | 측정 레벨을 절대값 0.3/0.7·VDD로 |
| 1.8 V에서 0.4 V@3 mA로 Rp_min | VOL2 = 0.2·VDD@2 mA. 467 Ω vs 720 Ω | `vol_iol()` 에서 VDD ≤ 2 V 분기 |
| Fm+ 디바이스 하나 있다고 Rp_min 145 Ω | 가장 약한 디바이스가 Rp_min을 정한다 | 버스 전체 중 최소 IOL로 계산 |
| 첫 샘플 ≥ 임계값으로 교차 판정 | ±dt 오차, 잡음에 흩어짐 | `cross_time` 선형 보간 |
| 잡음 있는 파형에서 상향 교차를 그냥 셈 | 에지 하나가 13개로 | lo 아래에서만 무장 |
| 잡음을 무시하고 바로 교차 측정 | lo 늦게·hi 일찍 → −9 % 편향 | W ≪ τ 중심 이동평균 후 측정, 여러 에지 통계 |
| 잘린 캡처에서 마지막 샘플까지 시간을 반환 | 그럴듯한 가짜 숫자 | 빈 목록 → `NO_EDGE` |
| 프로브 C 무시 | 마진 작은 버스에서 프로브 때문에 FAIL | `tr_bus = tr_meas − K·Rp·C_probe` |
| 측정 tr로 바로 "Rp 줄이자" | 원인이 DNP·이중 풀업이면 엉뚱한 수정 | Cb 역산을 예산과 비교, SUSPECT면 조립부터 확인 |
| 상태 레벨을 VDD/2 위 샘플의 중앙값으로 | 느린 에지는 꼬리 샘플이 많아 HIGH가 31 mV 낮게 (추가 연습 4) | 히스토그램 최빈값 (IEEE 181, HWLAB-B04) |

## 8. 면접 Q&A

<details>
<summary>Q1. I2C 풀업 저항은 어떻게 고르나요?</summary>

두 경계를 계산한다. 하한은 DC: 가장 약한 디바이스가 VOL(0.4 V)에서 보장하는 sink 전류 IOL(3 mA, Fm+ 20 mA)로 Rp ≥ (VDD − VOL)/IOL. 상한은 AC: Rp·Cb 충전으로 0.3→0.7·VDD를 tr_max 안에 → Rp ≤ tr_max/(0.8473·Cb). Cb는 디바이스 핀 Ci(≤ 10 pF) 합 + 배선(대략 1 pF/cm) + 커넥터·케이블. 그 창 안에서 표준값을 공차와 Cb 마진을 두고 고르고, 보드가 나오면 tr과 VOL을 측정해 확인한다.
꼬리질문: "창 안이면 아무 값이나?" → 큰 쪽은 정적 전류·VOL이 작고 EMI가 적지만 tr 마진이 작다. 작은 쪽은 빠르고 잡음에 강하지만 sink 전류가 크다. 배터리 기기와 긴 케이블 버스는 선택이 다르다.

</details>

<details>
<summary>Q2. 0.8473은 어디서 나오나요? 왜 30–70 %인가요?</summary>

I2C 입력 문턱이 VIL = 0.3·VDD, VIH = 0.7·VDD라서 rise time도 그 두 레벨 사이로 정의된다. RC 충전에서 그 간격은 RC·ln(0.7/0.3) = 0.8473·RC.
꼬리질문: "10–90 %로 재면?" → 2.197·RC, 즉 2.593배 길게 나온다. 스코프 기본값이 10–90 %라 측정 레벨을 바꾸지 않으면 좋은 버스를 FAIL로 판정한다. 꼬리질문 2: "LOW가 0.6 V로 떠 있으면 tr이 바뀌나?" → 상승 에지 30–70 % 간격은 시작 레벨과 무관(지수 함수의 성질). 바뀌는 건 지연과 VOL 마진이다.

</details>

<details>
<summary>Q3. 3.3 V Fm 버스에 Cb가 400 pF이면 풀업으로 맞출 수 있나요?</summary>

없다. Rp_min = 967 Ω, Rp_max(400 pF) = 885 Ω — 창은 Cb ≈ 366 pF에서 이미 닫힌다. 대안: ① 버퍼/리피터(PCA9517 계열 리피터, P82B96 같은 버스 버퍼)로 세그먼트 분할 — 세그먼트마다 Cb와 Rp가 따로 (추가 연습 3), ② 정전류형 rise-time accelerator(LTC4311 같은) — 같은 전류 한도로 약 1.86배 큰 Cb 허용 (추가 연습 2), ③ 버스의 **모든** 디바이스가 20 mA sink 가능하면 Rp_min을 145 Ω까지 내릴 수 있다, ④ 100 kHz로 낮추기.
꼬리질문: "리피터를 쓰면 공짜인가?" → 아니다. 일부 리피터는 한쪽 포트에 static offset(LOW가 0.5 V 근처로 뜸)이 있어 직렬 연결이나 다른 버퍼와 조합에 제약이 있고, 전파 지연이 더해진다. 데이터시트 조건을 확인한다.

</details>

<details>
<summary>Q4. 스코프로 tr을 쟀더니 650 ns가 나왔습니다. Fm FAIL인가요?</summary>

세 가지를 먼저 확인한다. ① 측정 레벨: 10–90 %면 30–70 %로 약 250 ns, PASS일 수 있다. ② 프로브 부하: 1x 프로브는 수십 pF 이상을 더한다. 10x 패시브도 10~15 pF — 2.2 k 풀업이면 0.8473 × 2.2 k × 12 pF ≈ 22 ns가 프로브 몫이다. ③ 측정 위치와 통계: 어느 디바이스 핀에서, 몇 개 에지의 평균인지 최대인지.
꼬리질문: "프로브 영향을 측정으로 확인하려면?" → 같은 지점에 두 번째 프로브를 추가로 대서 tr 변화량을 본다. 1차 RC면 Δtr = 0.8473·Rp·ΔC이므로 Rp를 알면 프로브 C를, C를 알면 Rp를 확인할 수 있다.

</details>

<details>
<summary>Q5. 버스 커패시턴스는 어떻게 알 수 있나요?</summary>

설계 단계에서는 예산: 디바이스 수 × Ci(스펙 최대 10 pF) + 배선 길이 × 단위 길이당 C + 커넥터·케이블(데이터시트). 보드에서는 알려진 Rp로 tr을 재서 Cb = tr/(0.8473·Rp) − C_probe. 이 레슨의 I2C0은 예산 62 pF, 역산 78 pF였다 — 예산은 ±30 % 정도로 생각한다.
꼬리질문: "역산한 Cb가 1.1 nF인데 디바이스는 3개뿐이라면?" → Cb가 아니라 Rp가 틀린 것이다. 실효 Rp = tr/(0.8473·Cb_예산) ≈ 41 kΩ → 외부 풀업 DNP, MCU 내부 풀업(수십 kΩ)만으로 동작 중. 내부 풀업은 I2C용으로 설계된 값이 아니다.

</details>

<details>
<summary>Q6. rise time이 스펙을 넘으면 실제로 무엇이 고장 나나요? 벤치에서는 잘 되는데요.</summary>

① SCL HIGH 시간 잠식: 고정 타이머로 SCL을 놓고 당기는 마스터(GPIO bit-bang, 단순 IP)는 라인을 놓은 순간부터 HIGH 시간을 세지만 수신기는 VIH를 넘은 뒤부터 HIGH로 본다 → 수신기 입장에서 tHIGH, SDA 쪽 tSU;DAT 같은 최소값이 깨진다. 스펙의 클럭 동기화처럼 SCL을 다시 읽어 HIGH 카운트를 시작하는 마스터는 대신 실측 SCL 주파수가 설정값보다 낮아진다. ② 느린 에지 + 잡음 = 이중 클럭: 기울기가 작으면 문턱 근처에서 잡음으로 여러 번 교차, 수신기가 클럭을 두 번 세서 비트가 밀린다 (Fm/Fm+ 입력의 50 ns 스파이크 필터 tSP가 일부만 막아 준다). ③ 데이터 쪽: SDA 상승이 느리면 다음 SCL 상승 전에 VIH에 못 닿아 1이 0으로 읽힌다 (주소 바이트 오독 → 엉뚱한 NACK).
벤치에서 되는 이유: 마진이 0이 아니라 음수일 뿐, 실제 문턱은 보통 스펙(0.3/0.7·VDD)보다 가운데에 있고 잡음이 작다. 온도, 다른 로트의 디바이스, 모터·DC-DC 잡음이 들어오면 깨진다 — 그래서 bring-up에서 **스펙 대비** 로 판정한다. 디코드 쪽 증상은 SIV-A07, 프로토콜 비트 수준은 COMM-B06.

</details>

<details>
<summary>Q7. 메인 보드와 모듈에 둘 다 풀업이 있으면 어떻게 되나요?</summary>

병렬이 되어 실효 Rp가 작아진다 (1 k‖1 k = 500 Ω < Rp_min 967 Ω). 가장 약한 디바이스가 VOL을 못 지켜 LOW가 뜬다 — 이 레슨의 I2C3에서 0.594 V. rise time은 오히려 빨라 보여서 tr만 보면 "좋아 보인다". 그래서 LOW 레벨과 실효 Rp를 같이 본다. 설계 규칙: 풀업은 호스트 쪽 한 곳, 모듈 쪽은 DNP 옵션으로. 보드 간 전원 도메인이 다르면 꺼진 쪽 디바이스를 통한 back-powering도 확인한다 (HWLAB-B02).
꼬리질문: "측정 없이 회로도에서 잡으려면?" → 넷리스트 감사로 넷마다 풀업 개수와 연결 레일을 센다 (HWLAB-B01).

</details>

<details>
<summary>Q8. 1.8 V 버스에서 Rp_min 계산의 함정은?</summary>

VDD ≤ 2 V에서는 스펙이 VOL = 0.2·VDD @ 2 mA로 바뀐다. 0.4 V@3 mA를 그대로 쓰면 467 Ω, 올바르게는 720 Ω. 또 1.8 V에서는 VIL 자체가 0.54 V라 0.36 V VOL과의 마진이 0.18 V뿐이다 — 그라운드 바운스 수십 mV도 의미가 있다.
꼬리질문: "1.8 V 센서와 3.3 V SoC를 연결하면?" → 레벨 시프터(패스 FET형 또는 전용 IC)를 두고 **양쪽 각각** 의 VDD로 창을 계산한다. 시프터의 FET 온저항이 VOL에 더해진다.

</details>

## 9. 추가 연습

### 연습 1 — fall time과 VOL의 함정

`measure_fall_times(t, v, vdd)` 로 0.7·VDD → 0.3·VDD 하강 시간을 잰다. 목표값이 0 V면 0.8473·τ지만, VOL이 0.6 V인 하강 에지(τ = 10 ns)는? I2C3 캡처의 fall time은?

<details>
<summary>답 보기</summary>

하강은 v = VOL + (VDD − VOL)e^(−t/τ) 로 **VOL을 향해** 간다. tf = τ·ln((0.7·VDD − VOL)/(0.3·VDD − VOL)). VOL = 0이면 ln(7/3) = 0.8473, VOL = 0.6 V면 ln(1.71/0.39) → 14.78 ns(τ = 10 ns). 상승 에지와 달리 끝점이 문턱에 가까워질수록 tf가 길어지고, VOL이 0.3·VDD를 넘으면 tf는 무한대(LOW 인식 불가)가 된다. I2C3 캡처: 측정 12.07 ns, 모델 12.16 ns (τf = (Ron‖Rp)·C). tf max 300 ns보다 훨씬 작다 — 문제는 속도가 아니라 VOL이다. Fm/Fm+는 EMI 때문에 tf **최솟값** 도 있으니 (값은 사용하는 UM10204 개정판의 표에서 확인) 강한 드라이버 + 작은 Cb면 그쪽도 본다.

</details>

### 연습 2 — 정전류 풀업이 버티는 Cb

저항 대신 정전류 I로 충전하면 에지는 직선이다: tr = Cb·0.4·VDD / I. sink 쪽 제약 때문에 I ≤ IOL. Fm 3.3 V에서 3 mA 정전류가 버티는 Cb는? 저항의 366 pF와 비교하면?

<details>
<summary>답 보기</summary>

300 ns × 3 mA / 1.32 V = 681.8 pF, 저항 대비 1.861배 = 0.8473 × (VDD − VOL)/(0.4·VDD). 저항은 전압이 오를수록 전류가 줄어(0.7·VDD에서 0.3·VDD/Rp) 위쪽이 느리지만, 정전류는 끝까지 같은 전류를 쓴다. 실제 rise-time accelerator는 상승 에지를 감지했을 때만 강한 전류를 켜는 방식이라 LOW 상태 정적 전류는 저항 풀업이 결정한다 — 데이터시트의 동작 조건을 확인한다. 400 pF Fm 3.3 V: 저항으로는 불가, 정전류로는 가능 (테스트로 확인).

</details>

### 연습 3 — 버퍼로 버스 나누기

`split_plan(mode, vdd, segments)` 가 세그먼트마다 E24 풀업(±1 %, Cb +20 %)을 고르고, 하나라도 불가하면 `None`. 520 pF 한 덩어리, 260 + 260 pF, 360 + 160 pF는?

<details>
<summary>답 보기</summary>

520 pF: Cb max 400 pF 초과이자 창도 닫힘 → None. 260 + 260 pF: 각 Rp_max(312 pF) = 1135 Ω, Rp_min = 967 Ω → 1.1 k (1111 ≤ 1135, 1089 ≥ 967) → `[1100, 1100]`. 360 + 160 pF: 360 pF 쪽은 마진 20 %를 넣으면 432 pF라 Rp_max 819 Ω < 967 → None. 분할은 **균형** 이 중요하다. 커넥터 너머 긴 케이블 쪽에 버퍼를 둬서 무거운 쪽을 따로 떼는 것이 보통이다.

</details>

### 연습 4 — ln(VHIGH − v) 직선 맞춤으로 τ 교차 검증

상승 에지 20–80 % 구간에서 ln(VHIGH − v) = c − t/τ 를 최소제곱으로 맞춰 τ를 구하고 0.8473·τ를 임계값 방식 tr과 비교한다. VHIGH를 "VDD/2 위 샘플의 중앙값"으로 잡으면 I2C2(느린 에지)에서 무슨 일이 생기나?

<details>
<summary>답 보기</summary>

I2C2는 캡처의 대부분이 아직 올라가는 꼬리라 중앙값 HIGH가 31 mV 낮다 (3.2656 V). ln(VHIGH − v) 는 VHIGH 근처에서 오차를 크게 키우므로 τ = 2417 ns, 0.8473·τ = 2048 ns로 3 % 짧게 나온다. 히스토그램 최빈값(3.2969 V, 평평한 구간의 레벨 — IEEE 181 방식, HWLAB-B04)을 쓰면 τ = 2478 ns → 2099 ns로 임계값 방식 2112 ns와 맞는다 (정답 0.8473 × 40 k × 62 pF = 2101 ns). 두 독립된 추정이 맞으면 단일 극점 RC 모델이 맞다는 증거이고, 크게 어긋나면 에지가 RC가 아니다 (액티브 풀업, 링잉, 분포 정수 케이블, 버퍼 offset).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py      # 12/12 passed, exit 0
```

실제 출력 (일부):

```text
PASS  VOL=0.6V: tf = 14.781ns = tau*ln((2.31-0.6)/(0.99-0.6)) = 14.781ns, NOT 8.47
PASS  I2C3 capture tf = 12.07ns, model 12.16ns (VOL 0.594V), << tf_max 300ns
PASS  Fm 3.3V: resistor closes at 366.3pF, 3mA current source reaches 681.8pF (x1.861)
PASS  260pF + 260pF -> [1100.0, 1100.0]
PASS  I2C2 slow edge: upper-half MEDIAN 3.2656V is 31mV low (tail samples) -> fit tau 2417ns, 0.8473*tau 2048ns (biased)
PASS  I2C2_SCL: Vhigh(mode)=3.2969V fit tau=2477.6ns -> 0.8473*tau=2099.2ns vs threshold tr=2111.7ns
12/12 passed
```

## 10. 핵심 정리

- 풀업 창: **Rp_min = (VDD − VOL)/IOL** (DC, 가장 약한 디바이스), **Rp_max = tr/(0.8473·Cb)** (AC). VDD ≤ 2 V면 VOL = 0.2·VDD @ 2 mA.
- 0.8473 = ln(0.7/0.3): I2C tr은 절대 레벨 0.3·VDD → 0.7·VDD. 스코프 기본 10–90 %는 2.593배 길게 나온다.
- 창이 닫히는 Cb = tr·IOL/(0.8473·(VDD − VOL)): Fm 3.3 V **366 pF**, 5 V 231 pF — 스펙 400 pF보다 먼저 막힌다. 대안은 버퍼 분할, 정전류 풀업, 전부 Fm+ 드라이버, 저속.
- 측정: 선형 보간, lo 아래에서만 무장, W ≪ τ 이동평균(지수에서는 간격 보존), 잘린 에지는 결과 없음.
- 프로브도 Cb다: tr_bus = tr_meas − 0.8473·Rp·C_probe. 1x 프로브 금지.
- 역산 Cb = tr/(0.8473·Rp) − C_probe를 예산과 비교: ≫ 예산이면 DNP/내부 풀업, ≪ 예산이면 이중 풀업. tr만 보지 말고 LOW 레벨도 본다.
- 추천은 창 안 표준값 + 저항 공차 + Cb 마진, 그리고 "왜 큰 쪽/작은 쪽인지" 를 말할 수 있어야 한다.

## 참고자료

- [NXP UM10204 I2C-bus specification and user manual](https://www.nxp.com/docs/en/user-guide/UM10204.pdf) — SDA/SCL 버스 라인 특성 표(tr, tf, Cb, VOL, IOL, Ci, tSP), §7.1 Pull-up resistor sizing (식 1·2)
- [TI SLVA689 — I2C Bus Pull-Up Resistor Calculation](https://www.ti.com/lit/an/slva689/slva689.pdf)
- NXP AN10216 — I2C manual (버스 버퍼·리피터·멀티플렉서 사용 예)
- NXP PCA9517 / P82B96 데이터시트 — 레벨 변환 리피터, 버스 버퍼 (static offset, 세그먼트 분할)
- Analog Devices LTC4311 데이터시트 — I2C/SMBus rise-time accelerator (정전류형 액티브 풀업)
- IEEE Std 181 — Transitions, Pulses, and Related Waveforms (상태 레벨·참조 레벨, HWLAB-B04)
- 스코프 제조사의 프로브 기초 문서 (예: Tektronix "ABCs of Probes") — 프로브 입력 커패시턴스와 부하 효과
- 관련 레슨: HWLAB-B01(넷리스트 풀업 감사) · HWLAB-B02(노이즈 마진·back-powering) · HWLAB-B04(IEEE 181 측정) · HWLAB-B05(프로브·그라운드 리드) · HWLAB-B06(트리거 상태 기계) · SIV-A07(디코드 측 증상) · COMM-B06(I2C 비트 수준)
