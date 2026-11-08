---
id: HWLAB-B09
time: 70분
updated: 2026-11-08
---
# HWLAB-B09 첫 전원 인가: 단락 검사·Golden 보드 비교·Inrush

## 학습 목표

- 무전원 상태의 레일-GND 저항표(DUT 1장 + golden 5장)를 **golden 중앙값 대비 비율 + robust z-score(median/MAD, sigma 바닥값)** 로 판정해 SHORT / OPEN / LOW / HIGH 를 표시하고, 전원 투입 여부(BLOCK / WARN / GO)를 결정할 수 있다.
- mean/stdev z-score 가 속는 두 경우(산포가 너무 작은 golden → 거짓 경보, 오염된 golden 1장 → 단락 놓침)를 숫자로 보이고, golden 자체를 leave-one-out 으로 검사할 수 있다.
- 전류 제한(CC) 벤치 전원 투입 캡처에서 정상 전류 I_ss, inrush 피크·지속시간·초과 전하 Q 를 재고, **1-샘플 글리치**(morphological opening 으로 제거)와 **CC 에 잘린 피크**를 측정값으로 착각하지 않는다.
- 정전력(constant-power) DC/DC 부하 때문에 "정상 전류 × 1.2" 제한값으로는 보드가 hiccup 하며 못 켜지는 이유를 부하선(load line)으로 설명하고, 근거 있는 전류 제한값과 그때 고장점에 들어갈 수 있는 최대 전력을 추천할 수 있다.

## 1. 문제

새 보드의 첫 전원 인가(smoke test)는 bring-up 의 첫 관문이고, "처음 보드에 전원을 어떻게 넣나요? 전류 제한은 얼마로?" 는 Rivian/NVIDIA/Apple 보드·실리콘 검증 면접의 단골 질문이다 — 감(感)이 아니라 **데이터로** 답하는 법을 이 레슨에서 만든다.

첫 보드에서 실제로 나는 사고는 대부분 조립 문제다: 0402 디커플링 캡 사이 솔더 브리지, 거꾸로 붙은 탄탈 캡, DNP 로 빠진 페라이트 비드, 값이 틀린 저항. 전원을 넣기 **전에** 레일마다 GND 까지 저항을 재서 golden(정상 동작이 확인된) 보드들과 비교하면 대부분을 잡는다. 그다음 전류 제한을 건 벤치 전원으로 켜면서 전류 파형을 캡처해 inrush 와 정상 전류를 확인한다. 이후의 레일 전압·리플(HWLAB-B08), 펌웨어 bring-up 순서(PLAT-B09 참고)는 이 관문을 통과한 보드에서만 의미가 있다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb09-a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">smoke test 흐름 (이 레슨의 스크립트가 맡는 부분 = 색칠한 상자)</text>
  <g stroke="currentColor">
    <rect x="10" y="45" width="130" height="56" rx="8" class="f-mut"/>
    <rect x="160" y="45" width="150" height="56" rx="8" class="f-acc"/>
    <rect x="330" y="45" width="160" height="56" rx="8" class="f-acc"/>
    <rect x="530" y="40" width="200" height="66" rx="8" class="f-warn"/>
    <rect x="160" y="160" width="150" height="56" rx="8" class="f-acc"/>
    <rect x="330" y="160" width="160" height="56" rx="8" class="f-acc"/>
    <rect x="530" y="160" width="200" height="56" rx="8" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="69">① 육안·극성</text><text x="75" y="88" font-size="12">현미경, 방향 표시</text>
    <text x="235" y="69">② 레일-GND 저항</text><text x="235" y="88" font-size="12">무전원 DMM (Part A)</text>
    <text x="410" y="69">③ golden 비교</text><text x="410" y="88" font-size="12">비율 + robust z (A/B)</text>
    <text x="630" y="64">BLOCK → 재작업·단락 찾기</text><text x="630" y="82" font-size="12">WARN → 낮은 제한으로 관찰</text>
    <text x="630" y="98" font-size="12">GO → 다음 단계</text>
    <text x="235" y="184">④ CC 전원 투입</text><text x="235" y="203" font-size="12">전류 프로브 캡처 (C)</text>
    <text x="410" y="184">⑤ inrush·I_ss·CC</text><text x="410" y="203" font-size="12">측정 → 제한값 추천</text>
    <text x="630" y="184">⑥ 레일 전압 (HWLAB-B08)</text><text x="630" y="203" font-size="12">→ 펌웨어 (PLAT-B09)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb09-a1)">
    <path d="M140 73 L158 73"/><path d="M310 73 L328 73"/><path d="M490 73 L528 73"/>
    <path d="M630 106 L630 130 L235 130 L235 158"/><path d="M310 188 L328 188"/><path d="M490 188 L528 188"/>
    <path d="M560 106 L560 118 L270 118 L270 103"/>
  </g>
  <text x="290" y="113" fill="currentColor" font-size="12">BLOCK: 재작업 후 다시 측정</text>
  <text x="10" y="242" fill="currentColor" font-size="12">원칙: 전원을 넣기 전에 잴 수 있는 것은 전부 잰다. 전원은 낮은 전류 제한 + 캡처 장비가 준비된 뒤에만.</text>
</svg>
```

데이터 (이 레슨 디렉터리):

| 파일 | 내용 |
|---|---|
| `rail_res.csv` | 가상의 보드 **ORCA-EVB rev B** 8개 레일의 GND 저항. golden G1–G5 + DUT(SN0007). DMM 표시 그대로 (`48.7k`, `410`, `OL`) |
| `rail_res_rework.csv` | 재작업 후 다시 잰 표 (U7 브리지 제거, FB12 장착, G4 입력 오타 수정) |
| `cap_golden_G1_3A0.csv` | golden G1 을 12 V / 제한 3.0 A 로 켠 V·I 캡처 (50 kS/s, 41 ms) |
| `cap_dut_0A50.csv` / `cap_dut_0A90.csv` | SN0007 을 제한 0.50 A / 0.90 A 로 켠 캡처 |
| `gen_captures.py` | 캡처 생성기 (CV/CC 전원 + 입력 C + UVLO·soft-start 가 있는 정전력 부하 모델, 시드 고정) |

### Part A — 레일-GND 저항 스크리닝과 inrush 기본 측정

1. `parse_ohms(s)`: `"2.20k"`, `"1.2M"`, `"0.95Ω"`, `"OL"` → 옴(float, OL 은 `inf`). 이상한 문자열은 `ValueError`.
2. `golden_stats(golden)`: 유한한 golden 값의 median, MAD, `sigma = max(1.4826·MAD, 5%·median, 0.05 Ω)`.
3. `classify_rail(golden, dut)`: `ratio = DUT/median`, `z = (DUT − median)/sigma` → SHORT(ratio ≤ 0.2 이고 z ≤ −4) / LOW(z ≤ −4) / OPEN(OL 또는 ratio ≥ 5 이고 z ≥ 4) / HIGH(z ≥ 4) / OK / NOGOLD(golden 유한값 < 3).
4. `measure_inrush(cap)`: 마지막 10 ms 의 정상 전류 I_ss(median, p99)와 전압, 피크, 문턱 `i_th = I_ss + 10%·(peak − I_ss)` 이상인 구간의 지속시간, 초과 전하 `Q = Σ max(0, i − I_ss)·dt`.

### Part B — 측정이 거짓말하는 지점들

- mean/stdev z-score(주어진 `naive_z`)가 VDD_1V8 단락을 놓치고(G4 의 입력 오타 `41.0`), VDD_0V9_PCIE 의 1.6 % 차이에 경보를 울리는 것을 숫자로 확인하고, robust 판정이 둘 다 맞히는지 본다.
- `golden_suspects(rows)`: golden 각 값을 "나머지 golden 의 중앙값" 과 비교해 ×0.5 ~ ×2 밖이면 golden 자체를 의심한다.
- `opening(x, k)`: 1-샘플 글리치(12.34 ms, +4.6 A)가 피크·지속시간·CC 판정을 망치지 않게 제거한다.
- CC 에 잘린 캡처(0.50 A)에서는 "피크 = 설정값" 이라 inrush 측정이 아님을 표시하고, 보드가 hiccup 하며 못 켜졌음을 판정한다.

### Part C — ORCA-EVB rev B SN0007 smoke test 리포트

- 첫 스크리닝 → `BLOCK` (VDD_1V8 SHORT) + golden G4 의심 → 재작업 후 → `WARN` (VDD_1V2_DDR LOW).
- golden 캡처에서 전류 제한 추천: `rec = ceil₅₀ₘₐ(max(1.3·I_ss,p99, I_ss + Q/5 ms))`, 고장점 최대 전력 `V_set·rec`.
- SN0007 의 0.50 A 캡처 → FAIL(기동 실패), 0.90 A 캡처 → PASS (CC 는 inrush 동안만, I_ss 가 golden 대비 ±15 % 안).

## 2. Problem (English)

> **Data-driven smoke test for a new board.**
>
> You get (1) a CSV of rail-to-GND resistance readings taken unpowered with a DMM on five golden boards and one DUT
> (values as displayed: `"48.7k"`, `"410"`, `"OL"`), and (2) V/I captures (CSV, `t_us,v,i`, header `# vset_V=…`, `# ilim_A=…`)
> of the board being powered from a current-limited bench supply. Using only the Python 3 standard library:
>
> 1. `parse_ohms(s) -> float` (`OL` → `inf`), `golden_stats(golden) -> {n, med, mad, sigma}` with
>    `sigma = max(1.4826*MAD, 0.05*med, 0.05)`, and `classify_rail(golden, dut) -> {verdict, ratio, z, med, sigma}`
>    with verdicts `SHORT/LOW/OPEN/HIGH/OK/NOGOLD` (thresholds: |z| ≥ 4, ratio ≤ 0.2 for SHORT, ≥ 5 for OPEN).
> 2. `golden_suspects(rows)` (leave-one-out median, flag ratio outside [0.5, 2]) and `opening(x, k)` (min filter then max filter).
> 3. `measure_inrush(cap) -> dict` with steady-state current (median and p99 of the last 10 ms), peak after opening,
>    duration above `I_ss + 0.1*(peak - I_ss)`, excess charge `Q`, time in constant-current mode, `clipped`, `started`.
> 4. `recommend_limit(golden_meas) -> {rec, by_ss, by_inrush, fault_w}`: refuse a clipped golden capture;
>    `rec = ceil_to_50mA(max(1.3*I_ss_p99, I_ss + Q/5ms))`; `poweron_verdict(dut_meas, golden_meas)`; `decide([(rail, verdict)])`
>    returning `BLOCK` if any SHORT, `WARN` if any other non-OK, else `GO`.
>
> Example: golden VDD_1V8 = `410, 405, 415, 41.0, 408`, DUT = `2.1` → median 408, MAD 3, sigma 20.4, z = −19.9 → `SHORT`,
> while mean/stdev z is only −2.0. Golden capture at 3 A: peak 2.77 A (raw max 5.03 A is a 1-sample glitch), 1.00 ms,
> Q = 2.26 mC, I_ss = 0.424 A → recommended limit 0.90 A; at 0.50 A the board never starts (hiccup).

## 3. 시작 코드

<!-- code: starter.py -->

저항표와 캡처 생성기:

<!-- code: rail_res.csv -->

<!-- code: gen_captures.py -->

```bash
cd lessons/HWLAB/HWLAB-B09
python3 -I starter.py ; echo "exit=$?"     # TODO 상태: 4/40 passed, exit 1
python3 -I gen_captures.py                  # (선택) 캡처 CSV 3개를 다시 만든다 — 커밋된 파일과 바이트 단위로 같다
```

실제 출력 (PASS 줄과 요약만):

```text
-- Part A: R-to-GND screening --
-- Part A: inrush capture --
PASS  golden I_ss ~= P/V + V/R_bleed = 5/11.98 + 0.006 = 0.4234 A
PASS  golden started (V_ss >= 0.95*12 V, flat)
PASS  golden not clipped at 3 A limit (glitch must not count as CC)
-- Part B: traps --
PASS  0.50 A > golden I_ss 0.42 A, yet too low: constant-power load needs P/V = 5W/4.5V = 1.1 A near UVLO
-- Part C: SN0007 smoke-test scenario --
4/40 passed
```

> 눈여겨볼 것: "golden not clipped" 는 stub(`n_cc = 0`)도 통과하는 **공짜 PASS** 다. 진짜 검사는 같은 측정 코드가 0.50 A 캡처에서 `clipped=True` 를 내는지(Part B)다 — 판정 로직은 항상 양성·음성 데이터 둘 다로 검증한다.

## 4. 힌트

<details>
<summary>힌트 1 — 왜 mean/stdev 가 아니라 median/MAD 인가</summary>

golden 이 5장뿐이면 한 장의 이상값(입력 오타, 덜 마른 플럭스, 다른 레인지)이 평균과 표준편차를 통째로 끌고 간다. median 은 5개 중 2개까지 망가져도 버티고(breakdown point 50 %), MAD = median(|x − med|) 도 같다. 정규분포라면 `sigma ≈ 1.4826·MAD` (1.4826 = 1/Φ⁻¹(0.75)).

</details>

<details>
<summary>힌트 2 — sigma 에 바닥값이 필요한 이유</summary>

golden 다섯 장이 우연히 `52.1, 52.2, 52.1, 52.3, 52.2` 처럼 붙어 있으면 sigma 가 0.1 Ω 수준이 되어, DMM 확도(±0.x % + 몇 카운트)보다 작은 차이에도 z 가 폭발한다. `max(1.4826·MAD, 5 %·med, 0.05 Ω)` 처럼 **측정·부품 공차로 정당화되는 하한** 을 둔다. 그리고 판정에는 z(통계적으로 이상한가)와 ratio(물리적으로 큰 차이인가)를 **둘 다** 쓴다.

</details>

<details>
<summary>힌트 3 — opening 의 창 정렬</summary>

erosion `e[j] = min(x[j−h .. j−h+k−1])` (h = k//2), dilation `d[j] = max(e[j−(k−1−h) .. j−(k−1−h)+k−1])`. 두 창이 서로 거울 관계여야 k 이상 지속된 평탄부가 제자리·같은 폭으로 돌아온다. 테스트: `[0,0,5,0,0,2,2,2,0,0]` → `[0,0,0,0,0,2,2,2,0,0]`.

</details>

<details>
<summary>힌트 4 — inrush 구간과 Q</summary>

문턱 이상인 샘플은 **opening 신호** 로 찾고 정상 구간(마지막 10 ms) 이전으로 제한한다. 원시 신호로 찾으면 12.34 ms 의 글리치가 "마지막 문턱 이상 샘플" 이 되어 지속시간이 12 ms 로 늘어난다. Q 는 그 구간에서 원시 전류로 `Σ max(0, i − I_ss)·dt` (노이즈는 적분에서 평균되어 사라진다).

</details>

<details>
<summary>힌트 5 — 올림과 부동소수점</summary>

계산으로 나온 값은 경계 위에 살짝 올라앉을 수 있다: `0.16 + 0.14 == 0.30000000000000004` 이고 `0.30000000000000004/0.05 == 6.000000000000001` 이라 `math.ceil(...)*0.05` 가 0.35 가 된다(실제로 확인). mA 정수로 바꿔 `ceil(round(x*1000, 6) / 50) * 50 / 1000`.

</details>

## 5. 풀이

### Part A — 레일-GND 저항: golden 과 "비율 + robust z" 로 비교

무전원 보드에서 레일-GND 저항은 그 레일에 걸린 모든 것의 병렬 합성이다: 블리더·분압 저항, 풀업, 레귤레이터 출력단, IC 의 전원 핀 내부(ESD 다이오드·클램프·누설 경로), 그리고 처음 몇 초 동안은 충전 중인 커패시터. 그래서 **절대 기준이 없다** — 대형 SoC 코어 레일은 정상인데 2 Ω, 12 V 입력은 50 kΩ 이다. "10 Ω 미만이면 단락" 같은 규칙은 이 표의 `VDD_0V8_CORE`(golden 1.82–1.97 Ω)를 단락으로 판정한다. 비교 기준은 같은 설계의 golden 보드뿐이다.

| 통계 | VDD_1V8 (golden 410, 405, 415, **41.0**, 408) | 의미 |
|---|---|---|
| median | 408 | 오타 하나에 흔들리지 않음 |
| MAD | median(2, 3, 7, 367, 0) = 3 | 산포의 robust 척도 |
| 1.4826·MAD | 4.45 Ω | 정규분포 가정 sigma |
| 5 %·median | 20.4 Ω ← **채택** | 측정·부품 공차 바닥값 |
| DUT 2.1 Ω | ratio 0.005, z = (2.1 − 408)/20.4 = −19.9 | **SHORT** |

판정 규칙과 그 이유:

| 판정 | 조건 | 왜 이 조건인가 | 다음 행동 |
|---|---|---|---|
| SHORT | ratio ≤ 0.2 **그리고** z ≤ −4 | 크기(5배 이상 낮음)와 유의성(산포 대비) 둘 다 | 전원 금지(BLOCK), 단락 위치 찾기 |
| LOW | z ≤ −4 (ratio > 0.2) | 부분 단락·값 틀린 부품·추가 부하 | 낮은 제한으로 켜고 그 레일 전류·온도 관찰 |
| OPEN | DUT = OL, 또는 ratio ≥ 5 이고 z ≥ 4 | 비드/0 Ω 점퍼 DNP, 레귤레이터 출력 끊김 | 기능 시험 전 수리, back-power 주의(HWLAB-B02) |
| HIGH | z ≥ 4 (ratio < 5) | 부하 부품 하나 빠짐 | BOM·조립 확인 |
| NOGOLD | golden 유한값 < 3 | 비교 근거 부족 | golden 추가 측정 |

`k = 4`, `rel_floor = 5 %`, `short_ratio = 0.2` 는 **팀 정책 값** 이다(물리 상수가 아님). 다섯 장 golden 의 산포, DMM 확도, 반도체가 달린 레일에서 측정 전류에 따라 값이 바뀌는 정도를 보고 레일별로 조정한다. 중요한 것은 정책을 코드와 리포트에 남겨 누가 재도 같은 판정이 나오게 하는 것이다.

**측정 프로토콜이 데이터의 일부다** — `rail_res.csv` 헤더에 적은 것처럼 golden 과 DUT 는 같은 미터·같은 레인지·같은 극성(빨강 = 레일, 검정 = GND)·같은 대기 시간(10 s)으로 잰다. 이유는 Part B 표 참고.

inrush 기본 측정 (golden G1, 12 V, 제한 3 A — 제한에 닿지 않는 넉넉한 값):

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">전류 캡처: golden @3 A (실선) vs SN0007 @0.9 A (점선)</text>
  <g stroke="currentColor" fill="none" opacity=".5">
    <path d="M60 250 L660 250"/><path d="M60 250 L60 50"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="54" y="254">0</text><text x="54" y="194">1 A</text><text x="54" y="134">2 A</text><text x="54" y="74">3 A</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="92" y="268">0</text><text x="192" y="268">1 ms</text><text x="292" y="268">2</text>
    <text x="392" y="268">3</text><text x="492" y="268">4</text><text x="592" y="268">5</text>
  </g>
  <polygon points="92,225 92,91 192,84 192,225" class="f-acc" opacity=".45"/>
  <path d="M62 250 L92 250 L92 91 L192 84 L194 239 L242 230 L342 225 L652 225" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M62 250 L92 250 L92 196 L502 196 L504 224 L652 224" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="6 4"/>
  <path d="M92 210 L330 210" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="336" y="214" fill="currentColor" font-size="12">i_th 0.66 A</text>
  <path d="M92 64 L192 64" stroke="currentColor"/><path d="M92 58 L92 70 M192 58 L192 70" stroke="currentColor"/>
  <text x="200" y="68" fill="currentColor" font-size="12">dur 1.00 ms ≈ 전원 출력 상승 시간</text>
  <text x="120" y="160" fill="currentColor" font-size="12">Q = 2.26 mC</text>
  <text x="200" y="100" fill="currentColor" font-size="12">peak 2.77 A ≈ C·dV/dt (220 µF × 12 V/ms = 2.64 A) + 부하</text>
  <text x="300" y="190" fill="currentColor" font-size="12">CC 0.90 A 로 4.1 ms 동안 충전 (잘린 inrush)</text>
  <text x="560" y="242" fill="currentColor" font-size="12">I_ss 0.42 A</text>
  <path d="M672 250 L680 240 M680 250 L688 240" stroke="currentColor"/>
  <path d="M705 250 L705 52" stroke="currentColor" class="f-warn" stroke-width="3"/>
  <text x="700" y="44" fill="currentColor" font-size="12" text-anchor="end">12.34 ms 글리치 5.03 A (1 샘플)</text>
  <text x="60" y="290" fill="currentColor" font-size="12">CC 는 입력 캡 충전 전하(≈ C·V = 2.6 mC)를 없애지 않고 더 긴 시간에 펴 바를 뿐이다.</text>
</svg>
```

- **I_ss** = 마지막 10 ms 의 median = 0.424 A. 모델로 검산: 5 W 정전력 / 11.98 V + 블리더 12 V/2 kΩ = 0.417 + 0.006 = 0.423 A. median 을 쓰는 이유는 부팅 활동 스파이크에 덜 민감해서이고, 여유 계산에는 p99 (0.433 A)를 따로 쓴다.
- **피크** = 2.77 A. 전원이 1 ms 동안 0 → 12 V 로 올리면 입력 커패시터는 `C·dV/dt = 220 µF × 12 kV/s = 2.64 A` 를 먹고, 4.5 V(UVLO)에서 켜진 DC/DC 의 soft-start 전류가 더해진다.
- **지속시간** 1.00 ms = 전원 상승 시간. 정의(문턱 10 %)를 리포트에 적어야 다른 사람의 숫자와 비교된다 — 파형 측정에서 "어느 기준 레벨로 쟀나" 를 밝히는 것은 IEEE 181 의 기본 정신이다(HWLAB-B04 참고).
- **Q** = 2.26 mC. 이상적으로는 `C·V = 2.64 mC` 인데 `−I_ss × 1 ms (0.42 mC) + soft-start 부하 몫` 이 빠지고 더해져 2.26 mC. 이 Q 가 Part C 추천의 핵심 입력이다.

### Part B — 측정이 거짓말하는 지점들

**① mean/stdev z 의 두 실패** (실제 테스트 출력):

```text
      measured -2.0244068640699284  expected -2.02 ± 0.01
PASS  naive mean/stdev z on VDD_1V8 = -2.0 -> |z|<3 would MISS the short (contaminated golden)
      measured 9.80087459654219  expected 9.8 ± 0.05
PASS  naive z on VDD_0V9_PCIE = +9.8 -> false alarm on a 1.6% difference
      measured 0.3065134099616847  expected 0.30651340996168586 ± 1e-06
PASS  robust VDD_0V9_PCIE -> OK (sigma floor 5% = 2.61 ohm, z +0.31)
```

| 레일 | mean / stdev | naive z | robust (med / sigma) | robust z | 판정 |
|---|---|---|---|---|---|
| VDD_1V8 | 335.8 / 164.8 (G4 오타가 stdev 를 부풀림) | −2.0 → 놓침 | 408 / 20.4 | −19.9 | SHORT |
| VDD_0V9_PCIE | 52.18 / 0.084 (우연히 빽빽) | +9.8 → 거짓 경보 | 52.2 / 2.61 | +0.31 | OK |

**② golden 도 검사 대상이다** — `golden_suspects` 는 G4 의 VDD_1V8 = 41.0 을 "나머지 넷의 중앙값 409 의 ×0.10" 으로 표시한다. 410 을 41.0 으로 옮겨 적은 소수점 실수일 가능성이 크지만, 진짜로 G4 에 문제가 있을 수도 있으니 **다시 재서** 확인한다(재작업 표에서는 410 으로 수정됨). golden 보드도 시간이 지나며 재작업·ESD·커넥터 마모로 변한다 — golden 세트는 주기적으로 서로 비교해 관리한다.

**③ DMM 저항 측정 자체의 함정** — 숫자가 같은 조건에서 나왔는지가 판정보다 먼저다:

| 원인 | 증상 | 드러내는 측정 / 대책 |
|---|---|---|
| 벌크 커패시터 충전 | 값이 수 초간 계속 올라감 (12 V 입력 수백 µF) | 고정 대기 시간 후 기록(여기선 10 s), golden 도 같은 시간 |
| 반도체 접합(ESD 다이오드, 바디 다이오드) | 레인지·극성에 따라 값이 다름 — 미터의 측정 전류·개방 전압이 레인지마다 다르기 때문 | 같은 미터·레인지·극성 고정, 극성 반대로도 한 번 재 보기, 다이오드 모드 병행 |
| 2-wire 리드·접촉 저항 (보통 0.1–0.3 Ω) | 저옴 레일(코어 1.9 Ω)에서 수 % ~ 수십 % 오차, 부분 단락을 가림 (추가 연습 3) | REL(영점) 기능, 4-wire(Kelvin) 밀리옴 미터 |
| 측정 지점 | 테스트 포인트 vs 레귤레이터 출력 캡 — 페라이트 비드 앞뒤가 다른 레일 | 측정 지점을 레일 이름과 함께 기록 (HWLAB-B01 의 테스트 포인트 감사) |
| 보드 상태 | 점퍼·DIP 스위치·배터리·디버거 연결 여부 | 측정 전 상태를 체크리스트로 고정 |

**④ 1-샘플 글리치** — 전류 프로브는 근처의 스위칭 노드나 정전기에서 픽업을 받는다. 원시 최대값을 피크로 쓰면 5.03 A, 그 글리치가 "마지막 문턱 이상 샘플" 이 되어 지속시간은 12 ms 로 늘어나고, CC 판정(`i ≥ 0.97·I_lim`)까지 오염된다. opening(폭 k = 3 샘플 = 60 µs)은 3 샘플보다 좁은 위쪽 돌기만 지우고 1 ms 짜리 inrush 평탄부는 그대로 둔다. 대가: 진짜 피크가 k 샘플보다 좁으면(ESR 이 작은 MLCC 의 수 µs 스파이크) 함께 깎인다 — k 는 "이보다 좁으면 물리가 아니라 노이즈" 라는 판단이고, 그 판단은 샘플링률·프로브 대역폭과 같이 적는다.

**⑤ CC 에 잘린 피크** — 제한 0.50 A 캡처의 최대값 0.507 A 는 보드의 inrush 가 아니라 **전원 설정값** 이다. 그래서 `clipped=True` 이면 피크를 리포트에 측정값으로 쓰지 않고, `recommend_limit` 은 잘린 golden 캡처를 거부한다. 진짜 inrush 를 알려면 제한을 넉넉히 올린 golden 캡처가 필요하다(golden 은 이미 정상 동작이 확인된 보드라서 가능한 일).

**⑥ "정상 전류보다 높게 걸었는데 왜 안 켜지나"** — 0.50 A 는 I_ss 0.42 A 보다 높지만 SN0007 은 V 가 4.0–5.8 V 사이를 오가며 끝내 켜지지 않았다(CC 100 %, hiccup). 이유는 DC/DC 컨버터가 **정전력 부하** 라서다: 출력 전력이 일정하면 입력 전류 `I = P/V` 는 입력 전압이 낮을수록 커진다(음의 증분 저항).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">부하선: CV/CC 전원 vs 5 W 정전력 부하 (I = P/V)</text>
  <g stroke="currentColor" fill="none" opacity=".5"><path d="M60 260 L660 260"/><path d="M60 260 L60 40"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="240" y="276">4 V</text><text x="330" y="276">6</text><text x="420" y="276">8</text>
    <text x="510" y="276">10</text><text x="600" y="276">12 V</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="54" y="189">0.5 A</text><text x="54" y="129">0.9 A</text><text x="54" y="74">1.25 A</text>
  </g>
  <rect x="240" y="40" width="22" height="220" class="f-mut" opacity=".5"/>
  <text x="246" y="56" fill="currentColor" font-size="12">UVLO 4.0/4.5 V</text>
  <path d="M222 52 L240 72 L262 93 L285 110 L330 135 L375 153 L420 166 L465 177 L510 185 L555 192 L600 197 L645 202" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="440" y="152" fill="currentColor" font-size="12">P = 5 W</text>
  <path d="M60 185 L600 185 L600 260" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="6 4"/>
  <path d="M60 125 L600 125 L600 185" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="2 3"/>
  <circle cx="510" cy="185" r="6" fill="none" stroke="currentColor" stroke-width="2"/>
  <circle cx="600" cy="197" r="6" class="f-ok" stroke="currentColor"/>
  <circle cx="311" cy="125" r="6" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="430" y="206" fill="currentColor" font-size="12">0.5 A 불안정점 10 V</text>
  <text x="608" y="190" fill="currentColor" font-size="12">동작점</text>
  <text x="320" y="117" fill="currentColor" font-size="12">0.9 A 불안정점 5.56 V</text>
  <text x="70" y="178" fill="currentColor" font-size="12">CC 0.5 A</text><text x="70" y="118" fill="currentColor" font-size="12">CC 0.9 A</text>
  <text x="70" y="236" fill="currentColor" font-size="12">불안정점 왼쪽: P/V &gt; I_lim → C 방전 → UVLO → 재시작(hiccup)</text>
  <text x="70" y="252" fill="currentColor" font-size="12">오른쪽: P/V &lt; I_lim → C 충전 → CV 12 V 로</text>
  <text x="10" y="294" fill="currentColor" font-size="12">기동 조건: soft-start 가 전력을 다 올렸을 때 입력 전압이 P/I_lim 보다 높아야 한다 (충전과 soft-start 의 경주).</text>
</svg>
```

입력 커패시터 방정식 `C·dV/dt = I_lim − P(t)/V` 에서 `V = P/I_lim` 은 평형점이지만 **불안정** 하다 — 그보다 낮으면 전류가 모자라 V 가 더 떨어진다. 0.50 A 면 그 점이 10 V 인데, soft-start(2 ms)가 전력을 5 W 까지 올리는 동안 커패시터는 4.5 V 에서 최대 5.75 V 까지밖에 못 올라가서 무너진다 → UVLO 4.0 V 에서 꺼짐 → 0.5 A 로 다시 4.5 V 까지 충전 → 재시작. 추가 연습 4 에서 이 주기를 재면 약 2.5 ms (≈ 400 Hz) — 벤치에서는 "전원 CC 램프 고정 + 보드에서 찌르르 소리(MLCC 압전 효과, 인덕터)" 로 나타난다. 0.90 A 면 불안정점이 5.56 V 이고 soft-start 가 끝날 무렵 V 는 이미 9 V 이상이라 기동한다.

### Part C — SN0007 smoke test 리포트와 전류 제한 추천

실제 출력 (`python3 -I solution.py` 의 Part C 부분, 앞의 공백 6칸 제거):

```text
=== SMOKE TEST ORCA-EVB rev B  SN0007  (golden G1..G5) ===
[R2GND] rail               DUT golden_med   ratio       z  verdict
[R2GND] VIN_12V          49.1k      49.6k    0.99    -0.2  OK
[R2GND] VDD_5V0          1.19k      1.21k   0.983    -0.3  OK
[R2GND] VDD_3V3            615        620   0.992    -0.2  OK
[R2GND] VDD_3V3_AUD         OL       2.2k     inf     inf  OPEN
[R2GND] VDD_1V8            2.1        408 0.00515   -19.9  SHORT
[R2GND] VDD_1V2_DDR       72.4       95.5   0.758    -4.8  LOW
[R2GND] VDD_0V9_PCIE        53       52.2    1.02     0.3  OK
[R2GND] VDD_0V8_CORE      1.88       1.91   0.984    -0.3  OK
[GOLDEN] G4 VDD_1V8 = 41 vs others 409 (x0.10) -> re-measure / check entry
[DECISION] SN0007 BLOCK: do not apply power: SHORT VDD_1V8; OPEN VDD_3V3_AUD; LOW VDD_1V2_DDR
--- after rework (U7 bridge removed, FB12 installed) ---
[R2GND] rail               DUT golden_med   ratio       z  verdict
[R2GND] VIN_12V          49.3k      49.6k   0.994    -0.1  OK
[R2GND] VDD_5V0           1.2k      1.21k   0.992    -0.2  OK
[R2GND] VDD_3V3            617        620   0.995    -0.1  OK
[R2GND] VDD_3V3_AUD      2.22k       2.2k    1.01     0.2  OK
[R2GND] VDD_1V8            404        410   0.985    -0.3  OK
[R2GND] VDD_1V2_DDR       72.6       95.5    0.76    -4.8  LOW
[R2GND] VDD_0V9_PCIE      52.9       52.2    1.01     0.3  OK
[R2GND] VDD_0V8_CORE      1.89       1.91    0.99    -0.2  OK
[DECISION] SN0007 WARN: power with low limit and watch: LOW VDD_1V2_DDR
[INRUSH] golden G1 @3.000A: peak 2.77A (raw max 5.03A, glitch rejected), 1.00 ms above 0.66A, Q 2.26 mC, I_ss 0.424A
[LIMIT] recommend 0.90A = max(1.3 x p99 0.433A = 0.563A, I_ss + Q/5ms = 0.876A); worst-case fault power 12V x 0.90A = 10.8 W
[POWER] SN0007 @0.500A: FAIL did not start: CC-limited 100% of capture, V_ss=5.33V pp=1.75V (hiccup) -> limit below startup need
[POWER] SN0007 @0.900A: PASS started, CC 4.1 ms (<= 5 ms), I_ss 0.431A vs golden 0.424A (+1.8%)
```

읽는 법 — 하드웨어 엔지니어가 바로 움직일 수 있는 줄이어야 한다:

1. **BLOCK**: VDD_1V8 이 2.1 Ω. 1.8 V 레일 전체가 거의 단락이다. 여기서 12 V 를 넣으면 1.8 V 레귤레이터가 전류 제한·열 차단을 반복하거나, 브리지가 탄다. 단락 찾기: 레일을 CC 전원으로 **정격 이하 전압**(예: ≤ 1 V, 1 A 제한)으로 몰고 열화상 카메라로 뜨거운 점을 보거나, 플레인 위 mV 전압 강하의 기울기를 따라간다(전류는 단락점으로 흐르므로 그쪽으로 갈수록 전압이 낮다). 이 시나리오에서는 U7 핀 사이 브리지.
2. **OPEN** VDD_3V3_AUD: 페라이트 FB12 미장착. 이대로 켜면 오디오 코덱 전원은 없는데 I2S/I2C 핀은 3.3 V 도메인에서 구동되어 ESD 다이오드로 back-power 될 수 있다(HWLAB-B02 참고) — 그래서 SHORT 만큼은 아니어도 수리 후 진행.
3. **LOW** VDD_1V2_DDR (×0.76, z −4.8): 단락은 아니지만 부하가 하나 더 붙은 모양. 전원을 넣되 그 레일 전류·온도를 따로 본다(HWLAB-B10 의 열 측정, HWLAB-B08 의 레일 측정).
4. **LIMIT**: 정상 동작 여유 `1.3 × p99 = 0.563 A` 와, inrush 를 CC 로 잘라도 입력 캡 충전이 `t_allow = 5 ms` 안에 끝나는 값 `I_ss + Q/t_allow = 0.876 A` 중 큰 것을 50 mA 단위로 올려 **0.90 A**. 실제 0.90 A 캡처의 CC 시간은 4.1 ms 로, 근사 `Q/(I_lim − I_ss) = 2.26 mC / 0.476 A ≈ 4.7 ms` 보다 조금 짧다 — 근사는 충전 중에도 부하가 I_ss 를 다 먹는다고 보는 보수적인 쪽이다(SN0007 의 C_in 도 215 µF 로 약간 작다, 추가 연습 1). `t_allow` 는 보드의 시퀀서·PG 타임아웃보다 충분히 짧게 고르는 정책 값이다.
5. **고장점 최대 전력** `V_set × I_lim = 10.8 W`: CV/CC 전원이 저항성 고장점에 넣을 수 있는 최대 전력은 고장 저항이 `V_set/I_lim`(여기선 13 Ω)일 때 `V·I` 다(더 작은 저항이면 CC 로 `I²R`, 더 크면 CV 로 `V²/R`). 0.05 Ω 의 완전 단락은 0.9² × 0.05 ≈ 40 mW 로 오히려 안전하고, 위험한 것은 수 Ω 의 **부분 단락** 이다. 그래서 전류 제한은 "낮을수록 안전" 이지만 Part B ⑥ 때문에 무한정 낮출 수 없다 — 이 트레이드오프를 숫자로 보여 주는 것이 이 리포트의 목적이다.

**장비/도구가 있을 때** (이 레슨에서는 실행하지 않음 — 형태만): 전원 설정과 캡처를 스크립트로 남기면 리포트의 메타데이터(`vset_V`, `ilim_A`)가 사람 기억이 아니라 장비에서 온다. PyVISA 로 SCPI 를 보내는 모양은 다음과 같다(명령 문법은 전원 기종 매뉴얼로 확인).

```python
import pyvisa                                   # 실행하지 않은 예시
rm = pyvisa.ResourceManager()
psu = rm.open_resource("TCPIP0::192.168.1.50::INSTR")
psu.write("VOLT 12.0"); psu.write("CURR 0.90")  # CV 설정, CC 제한
print(psu.query("CURR?"))                        # 설정값을 읽어 캡처 헤더에 기록
psu.write("OUTP ON")                             # 스코프는 미리 single 트리거(전류 상승 에지)로 대기
```

- 전원의 전류 표시(readback)는 수십~수백 ms 평균이라 1 ms inrush 를 볼 수 없다. **DC 결합 전류 프로브**(홀 효과 클램프 — 사용 전 degauss·영점, 화살표 방향 확인) 또는 **션트 + 차동 프로브** 로 스코프에서 본다.
- 벤치 전원의 CC 는 순간적이지 않다: 전원 출력단 커패시터가 먼저 방전되고 제어 루프가 수십 µs 이상 늦게 반응하는 기종이 많다. 이 모델은 CC 를 이상적으로 다뤘다.
- 이 모델의 전원 출력 상승은 1 ms 다(기종마다 다름). 이미 켜진 12 V 어댑터를 배럴잭에 꽂는 **hot-plug** 는 훨씬 빨라서 피크가 수십 A 가 될 수 있다 — 그래서 실제 제품은 hot-swap 컨트롤러·eFuse·입력 soft-start 를 둔다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"              # 40/40 passed, exit 0 (0.1 s)
python3 -I -W error -X dev solution.py > /dev/null   # 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 절차 |
|---|---|---|
| "10 Ω 미만이면 단락" 절대 기준 | 코어 레일은 정상이 2 Ω, 12 V 입력은 50 kΩ | golden median 대비 `ratio` + `z` |
| mean/stdev z-score | golden 1장 오염 → 단락 놓침, 빽빽한 golden → 거짓 경보 | `med`, `MAD`, `sigma = max(1.4826·MAD, 5%·med, 0.05)` |
| z 하나로만 판정 | 산포가 작으면 의미 없는 차이도 z 가 큼 | `ratio <= 0.2 and z <= -4` 처럼 크기와 유의성 둘 다 |
| golden 을 무조건 신뢰 | 입력 오타·재작업된 golden 이 기준을 망침 | `golden_suspects` leave-one-out, 의심 값은 재측정 |
| golden 과 DUT 를 다른 레인지·극성·대기 시간으로 측정 | 반도체·커패시터 때문에 같은 보드도 값이 달라짐 | 측정 프로토콜을 CSV 헤더에 남기고 고정 |
| `max(i)` 를 inrush 피크로 | 1-샘플 글리치 5.03 A | `max(opening(i, 3))` |
| 문턱 구간을 원시 신호로 찾음 | 글리치가 지속시간을 12 ms 로 늘림 | `io[j] >= i_th` (opening 신호) |
| CC 에 잘린 캡처의 피크를 inrush 로 보고 | 그것은 전원 설정값 | `clipped` 이면 피크 대신 "CC n ms" 로 보고, 추천에 쓰지 않음 |
| 제한 = 정상 전류 × 1.2 | 정전력 부하는 낮은 전압에서 더 많은 전류를 원함 → hiccup | `max(1.3·p99, I_ss + Q/t_allow)` 후 실제로 켜서 확인 |
| 전원 앞면 전류계로 inrush 측정 | readback 은 느린 평균값 | DC 전류 프로브 / 션트 + 스코프 single 트리거 |
| `math.ceil(x/0.05)*0.05` | `0.16+0.14` → 0.35 처럼 부동소수점 오차로 한 칸 더 올라감 | mA 정수로 `ceil(round(x*1000,6)/50)*50/1000` |

## 8. 면접 Q&A

<details>
<summary>Q1. 새 보드에 처음 전원을 넣는 순서를 설명해 보세요.</summary>

육안·현미경 검사(극성 부품 방향, 브리지) → 무전원으로 레일마다 GND 저항을 재서 golden 과 비교 → 단락이 없으면 벤치 전원에 근거 있는 전류 제한을 걸고, 전류 프로브로 inrush·정상 전류를 캡처하며 켠다 → 손/열화상으로 뜨거운 부품 확인 → 레일 전압·리플·시퀀싱 측정(HWLAB-B08) → 그다음에 클럭·리셋·콘솔·메모리(PLAT-B09). 가능하면 레귤레이터를 하나씩 enable 하며(점퍼·0 Ω 저항·EN 핀) 단계별로 켠다.
꼬리질문: "저항을 왜 먼저 재나요? 그냥 낮은 제한으로 켜면 되지 않나요?" → 제한을 걸어도 부분 단락에는 최대 `V_set·I_lim` 의 전력이 들어간다. 무전원 측정은 비용이 0 이고 **어느 레일** 인지까지 알려 주지만, 전원 투입 후의 전류 증가는 어느 레일인지 알려 주지 않는다.

</details>

<details>
<summary>Q2. 한 레일의 GND 저항이 golden 보다 낮습니다. 단락인가요?</summary>

얼마나 낮은지(비율)와 golden 산포 대비 얼마나 이상한지(z)를 같이 본다. 비율 0.005 처럼 몇 배 이상 낮으면 단락, 20–30 % 낮으면 부분 단락·값 틀린 부품·추가 부하 후보다. 그전에 측정 조건(같은 미터·레인지·극성, 커패시터 충전 대기, 저옴이면 리드 보정)이 golden 과 같은지 확인한다.
꼬리질문: "단락 위치는 어떻게 찾나요?" → 레일을 CC 전원으로 정격 이하 전압·적당한 전류로 몰고 열화상 카메라(또는 냉각 스프레이·IPA)로 뜨거운 점을 찾거나, 민감한 DMM 으로 플레인 위 mV 강하의 기울기를 따라간다. 의심 부품을 하나씩 들어내며 저항 변화를 보는 것은 마지막 수단이다.

</details>

<details>
<summary>Q3. 전류 제한을 얼마로 거나요? 정상 전류의 1.2배로 걸었더니 보드가 안 켜집니다. 왜죠?</summary>

DC/DC 는 정전력 부하라 입력 전압이 낮을 때 `P/V` 만큼 더 많은 전류를 원한다. CC 상태에서 `V = P/I_lim` 은 불안정 평형점이고, soft-start 가 끝날 때 입력 전압이 이 점보다 낮으면 무너져 UVLO 에서 꺼졌다 켜지기를 반복한다(hiccup). 그래서 golden 을 넉넉한 제한으로 캡처해 inrush 전하 Q 를 재고, `max(정상 전류 p99 × 여유, I_ss + Q/t_allow)` 처럼 계산한 뒤 **그 값으로 실제로 켜서** 확인한다.
꼬리질문: "그럼 아예 5 A 로 걸면?" → 부분 단락에 최대 60 W 가 들어간다. 제한은 보드가 확실히 켜지는 최소값 근처여야 보호 기능이 있다.

</details>

<details>
<summary>Q4. inrush 는 어떤 장비로 어떻게 재나요? 전원 앞면 전류계로는 안 되나요?</summary>

전원 readback 은 느린 평균이라 ms 이하 현상을 못 본다. DC 결합 전류 프로브(홀 효과 + 변류기 조합, degauss·영점·방향 확인, 대역폭은 µs~ms 현상에 충분) 또는 입력에 션트를 넣고 차동 프로브로 본다. 스코프는 전류 상승 에지 single 트리거, 전압 채널을 같이 잡아 dV/dt 와 CC 진입(전류 평탄 + 전압 처짐)을 구분한다. 측정 정의(문턱, 기준 레벨)를 리포트에 적는다.
꼬리질문: "캡처에서 5 A 스파이크 한 샘플이 보이면?" → 프로브 픽업 의심. 물리적인 inrush 라면 `C·dV/dt` 로 설명돼야 하고 폭이 있다. 같은 위치에서 프로브를 개방 루프로(도체 없이) 두고 다시 찍어 보면 픽업인지 바로 안다.

</details>

<details>
<summary>Q5. golden 보드 5장으로 통계를 내도 되나요? 왜 MAD 를 쓰나요?</summary>

5장으로 표준편차를 믿을 수는 없다. median/MAD 는 5개 중 2개까지 이상값이 있어도 버티지만, 그래도 우연히 빽빽하면 sigma 가 비현실적으로 작아지므로 측정·부품 공차에서 나온 바닥값(여기선 5 %)을 둔다. 그리고 z 와 비율을 같이 써서 "통계적으로 이상" 과 "물리적으로 의미 있음" 을 모두 요구한다.
꼬리질문: "golden 이 틀렸다면?" → leave-one-out 으로 golden 각각을 나머지와 비교해 의심 값을 찾고 재측정한다. golden 세트도 버전 관리하는 데이터다.

</details>

<details>
<summary>Q6. inrush 가 크면 무엇이 문제인가요? 보드 설계에서는 어떻게 줄이나요?</summary>

입력 퓨즈 열화·용단(I²t, 추가 연습 2), 커넥터 접점 아크·마모, 상위 전원(어댑터, 차량 12 V 버스)의 전압 강하로 다른 장치 리셋, 핫플러그 시 수십 A 피크. 설계에서는 hot-swap 컨트롤러나 eFuse 의 dV/dt 제어, 입력 MOSFET 의 게이트 RC 로 출력 상승을 느리게, 레귤레이터 soft-start 시간 조정, 순차 enable 로 줄인다.
꼬리질문: "dV/dt 를 늦추면 inrush 가 얼마나 줄어요?" → 커패시터 몫은 `C·dV/dt` 이므로 상승 시간에 반비례한다. 단, 정전력 부하가 그동안 켜지면 위 Q3 문제가 생기므로 부하 enable 을 PG 이후로 미룬다.

</details>

<details>
<summary>Q7. 전류 제한을 걸어 두면 단락이 있어도 보드가 안전한가요?</summary>

아니다. CV/CC 전원이 저항성 고장점에 넣을 수 있는 최대 전력은 `V_set·I_lim` (고장 저항 = `V_set/I_lim` 일 때)이다. 12 V·0.9 A 면 10.8 W — 0402 저항이나 얇은 트레이스를 태우기에 충분하다. 또 전원 출력 커패시터가 CC 루프보다 먼저 방전된다. 그래서 무전원 저항 스크리닝을 먼저 하고, 의심 보드는 낮은 전압부터 올리며 열화상으로 본다.

</details>

## 9. 추가 연습

### 연습 1 — inrush 로 입력 커패시턴스 추정

DC/DC 가 UVLO 로 아직 꺼져 있는 첫 상승 구간(0.5 V < V < 4.0 V)에서는 전류가 거의 전부 커패시터 충전이다: `C = I / (dV/dt)`. golden 과 SN0007 의 C_in 을 추정해 BOM(220 µF ±20 %)과 비교하라. CC 에 잘린 0.50 A 캡처에서도 구할 수 있는가?

<details>
<summary>답 보기</summary>

중앙차분 `dV/dt = (v[j+1] − v[j−1]) / 2dt` 로 샘플마다 C 를 구하고 중앙값을 쓴다(노이즈에 강함). 결과: golden 220.0 µF(15 샘플), SN0007 215.3 µF(0.50 A 캡처, 76 샘플) — 0.90 A 캡처와 3 % 이내로 일치. **CC 에 잘린 캡처에서도 된다**: 전류는 일정(0.5 A)하고 dV/dt 만 작아질 뿐 비율은 같다. 실무 용도: 벌크 캡 누락·용량 부족(전해 캡 극성 반대로 붙어 용량이 이상한 경우 등)을 전원 한 번 켜는 것으로 확인. 세라믹 캡은 DC 바이어스에서 용량이 크게 줄어드므로 "BOM 공칭값의 합" 과 다를 수 있다 — 그래서 golden 측정값과 비교하는 것이 맞다.

</details>

### 연습 2 — 입력 퓨즈 I²t 여유

golden inrush 구간의 `I²t = Σ i²·dt` 를 구하고, 입력 퓨즈 F1 의 공칭 용단 I²t 와 펄스 내구 derating 으로 판정하라. 이 연습에서는 F1 = 0.10 A²s, derating 22 % 를 **가정** 한다.

<details>
<summary>답 보기</summary>

`I²t ≈ 7.22e-3 A²s` (≈ 2.7² A² × 1 ms). 허용치 `0.22 × 0.10 = 0.022 A²s` 대비 33 % 사용 → OK. 공칭 0.02 A²s 퓨즈면 FAIL. 22 % 는 퓨즈 제조사 선정 가이드에 나오는 "펄스 반복 횟수별 허용 비율" 표의 예(10만 회 수준)로 쓴 값이다 — 실제 설계에서는 그 퓨즈의 데이터시트와 제조사 표, 주위 온도 derating 을 확인해야 하고, 공칭 용단 I²t 가 어떤 시험 조건에서 정의됐는지도 본다.

</details>

### 연습 3 — 2-wire 리드 저항이 단락을 가리는 경우

REL 없이 잰 SN0007 코어 레일 값이 0.42 Ω 이다(리드+접촉 0.25 Ω 포함). golden(1.82–1.97 Ω) 대비 보정 전·후 판정은?

<details>
<summary>답 보기</summary>

보정 전 0.42 Ω → ratio 0.220 → **LOW** (ratio > 0.2 라서 SHORT 가 아님). 보정 후 0.17 Ω → ratio 0.089 → **SHORT**. 리드 저항은 저옴 레일에서 판정을 바꾼다. 측정 전에 리드를 맞대고 REL(영점), 또는 4-wire 밀리옴 미터. 반대로 golden 을 REL 없이 쟀다면 golden median 이 올라가 DUT 가 낮아 보인다 — 다시 말하지만 **같은 프로토콜** 이 핵심이다.

</details>

### 연습 4 — hiccup 주기

0.50 A 캡처에서 V 가 (min+max)/2 를 히스테리시스 ±0.1 V 로 위로 지나는 시각들의 간격 중앙값을 구하라. 0.90 A 캡처에는 hiccup 이 없음을 확인하라.

<details>
<summary>답 보기</summary>

16번 재시작, 주기 중앙값 2.46 ms(≈ 407 Hz). 한 주기 = 부하가 꺼진 상태에서 0.5 A 로 4.0 → 4.5 V 재충전(`0.5 V × 215 µF / 0.5 A ≈ 0.2 ms`) + soft-start 가 다시 전력을 올리다 `P(t)/V` 가 0.5 A 를 넘어 4.0 V 까지 무너지는 시간. 히스테리시스가 없으면 노이즈(σ 4 mV)로 한 번 지날 때 여러 번 세어진다. 벤치 증상: CC 램프가 계속 켜져 있고 표시 전압이 5 V 근처에서 흔들리며 보드에서 가는 소리가 난다. 0.90 A 캡처는 10 ms 이후 V 의 pk-pk 가 0.1 V 미만.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 13/13 passed, exit 0
```

실제 출력 (일부):

```text
-- 연습 2: fuse I2t --
      measured 7.22148912380002  expected 7.3 ± 0.4
PASS  golden inrush I2t ~= 2.7^2 A^2 * 1 ms = 7.3e-3 A^2s
      F1: pulse 7.22e-03 A^2s vs 22% x 0.10 = 0.022 A^2s -> OK (33% used)
...
-- 연습 3: lead resistance (REL) on a low-ohm rail --
      raw 0.42 -> LOW ratio 0.220 ; REL 0.17 -> SHORT ratio 0.089
...
-- 연습 4: hiccup period --
      16 restarts, median period 2.46 ms (407 Hz)
...
13/13 passed
```

## 10. 핵심 정리

- smoke test = 육안 → **무전원 레일-GND 저항 vs golden** → 전류 제한 + 전류 프로브 캡처로 투입 → 레일 측정(B08) → 펌웨어(PLAT-B09).
- 레일 저항에 절대 기준은 없다. golden **median 대비 비율** 과 **robust z**(`sigma = max(1.4826·MAD, 5%·med, floor)`)를 둘 다 요구한다.
- mean/stdev z 는 오염된 golden 에서 단락을 놓치고(−2.0), 빽빽한 golden 에서 거짓 경보(+9.8)를 낸다. golden 도 leave-one-out 으로 검사한다.
- 측정 프로토콜(미터·레인지·극성·대기 시간·REL)이 같아야 비교가 성립한다. 저옴 레일은 리드 저항이 단락을 가린다.
- inrush 피크 ≈ `C·dV/dt` + 부하, 지속시간 ≈ 전원 상승 시간, `Q ≈ C·V`. 글리치는 opening 으로 지우고, CC 에 잘린 피크는 측정값이 아니다.
- DC/DC 는 정전력 부하: `V = P/I_lim` 은 불안정점 → 제한이 낮으면 hiccup. 제한 = `max(1.3·p99, I_ss + Q/t_allow)` 후 실제로 켜서 확인.
- CV/CC 전원이 고장점에 넣을 수 있는 최대 전력은 `V_set·I_lim` — 위험한 것은 완전 단락보다 수 Ω 의 부분 단락이다.

## 참고자료

- P. Horowitz, W. Hill, *The Art of Electronics*, 3rd ed. — 전원·레귤레이터·측정 장(CV/CC 전원, 커패시터 충전, 접지)
- E. Bogatin, *Signal and Power Integrity — Simplified* — PDN 과 디커플링 커패시터
- L. D. Smith, E. Bogatin, *Principles of Power Integrity for PDN Design* — 레일 임피던스, 벌크·세라믹 캡
- Texas Instruments, *Managing Inrush Current* (Application Report SLVA670) — 부하 스위치·eFuse 로 dV/dt 제어
- Littelfuse, Fuseology / fuse selection guide — I²t, 펄스 내구(pulse cycle withstand) derating 표
- [NIST/SEMATECH e-Handbook of Statistical Methods](https://www.itl.nist.gov/div898/handbook/) — robust 산포 척도(MAD), 이상값 검출
- P. J. Rousseeuw, C. Croux, "Alternatives to the Median Absolute Deviation", *JASA* 88(424), 1993
- IEEE Std 181 — Transitions, Pulses, and Related Waveforms (측정 정의를 밝히는 습관, HWLAB-B04)
- [PyVISA documentation](https://pyvisa.readthedocs.io/) — SCPI 로 벤치 전원·스코프 자동화
- IPC-A-610 — 전자 조립품 허용 기준 (솔더 브리지 등 육안 검사 기준)
- 관련 레슨: HWLAB-B01(넷리스트·테스트 포인트), HWLAB-B02(back-powering), HWLAB-B04(측정 정의), HWLAB-B05(프로브), HWLAB-B08(레일 측정), HWLAB-B10(열), PLAT-B09(bring-up 순서)
