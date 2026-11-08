---
id: HWLAB-M03
time: 80분
updated: 2026-11-08
---
# HWLAB-M03 PDN 타겟 임피던스와 디커플링 커패시터 선정

## 학습 목표

- 레일 스펙(전압, 허용 리플 %, 부하 전류 스텝 ΔI)에서 **타겟 임피던스 Ztarget = V·ripple%/ΔI** 를 계산하고, 그 숫자가 무엇을 가정하는지(평탄 타겟, 선형 중첩, ΔI의 주파수 성분) 설명할 수 있다.
- 커패시터를 **직렬 RLC (Z = ESR + j(ωL − 1/ωC))** 로 모델링하고, VRM(R + jωL)과 여러 커패시터의 **병렬망 |Z(f)|** 를 10 kHz–1 GHz에서 직접 계산한다. SRF, 실장 인덕턴스(mounting inductance), 병렬 개수의 효과를 수치로 확인한다.
- **반공진(anti-resonance) 피크**의 주파수 `f = 1/(2π√((L1+L2)·C1C2/(C1+C2)))` 와 높이 `≈ X²/(R1+R2)` 를 유도하고, **ESR이 낮을수록 피크가 높아지는** 이유를 설명할 수 있다.
- "decade 점·SRF 지점만 확인" 이나 거친 주파수 그리드가 **피크를 놓쳐 PASS라고 거짓말하는** 것을 보이고, 국소 최대 + 황금분할 정밀화로 고친다.
- 1/10/100 µF 개수를 전수 탐색해 **목표 대역에서 Ztarget 이하가 되는 최소 BOM** 을 고르고, 조립된 보드의 **VNA 2-port shunt-thru 측정(.s2p)** 에서 Z를 복원해 "10 µF 3개 미실장" 을 찾아내는 리포트를 만든다.

## 1. 문제

SoC 코어 레일이 부하 스텝 때 몇 mV 출렁이는지는 결국 **PDN 임피던스 |Z(f)|** 로 정해진다 — NVIDIA/Apple/Rivian 보드·전원 무결성(PI) 면접의 단골 질문 "타겟 임피던스는 어떻게 정하고, 디커플링 커패시터 몇 개를 어떻게 골랐으며, 그걸 보드에서 어떻게 확인했나?" 에 숫자로 답하려면 직접 계산해 봐야 한다.

전류가 ΔI 만큼 갑자기 변하면 레일 전압은 대략 ΔV ≈ ΔI·|Z(f)| 만큼 움직인다(선형 근사). 허용 리플이 V·ripple% 라면, **관심 대역 전체에서 |Z(f)| ≤ V·ripple%/ΔI** 이면 어떤 주파수 성분의 전류 변화에도 리플이 허용치 안에 든다 — 이게 타겟 임피던스(target impedance) 방법이다. 저주파는 VRM(레귤레이터)의 제어 루프가, 그 위는 보드 커패시터가, 수십 MHz 위는 패키지·온다이 커패시턴스가 책임진다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">PDN 등가회로 (이 레슨의 모델): VRM ∥ 커패시터 군 → 부하 전류 ΔI</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <line x1="40" y1="70" x2="720" y2="70"/>
    <line x1="40" y1="250" x2="720" y2="250"/>
    <line x1="60" y1="70" x2="60" y2="95"/><rect x="48" y="95" width="24" height="34" class="f-mut"/>
    <path d="M60 129 q10 6 0 12 q10 6 0 12 q10 6 0 12 q10 6 0 12"/><line x1="60" y1="177" x2="60" y2="250"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="78" y="116">R_vrm 1.5 mΩ</text><text x="78" y="160">L_vrm 8 nH</text>
    <text x="22" y="62">VRM (폐루프 출력 Z 근사)</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <line x1="250" y1="70" x2="250" y2="92"/><rect x="240" y="92" width="20" height="26" class="f-mut"/>
    <path d="M250 118 q9 5 0 10 q9 5 0 10 q9 5 0 10"/><line x1="250" y1="148" x2="250" y2="190"/>
    <line x1="236" y1="190" x2="264" y2="190"/><line x1="236" y1="198" x2="264" y2="198"/><line x1="250" y1="198" x2="250" y2="250"/>
    <line x1="420" y1="70" x2="420" y2="92"/><rect x="410" y="92" width="20" height="26" class="f-mut"/>
    <path d="M420 118 q9 5 0 10 q9 5 0 10 q9 5 0 10"/><line x1="420" y1="148" x2="420" y2="190"/>
    <line x1="406" y1="190" x2="434" y2="190"/><line x1="406" y1="198" x2="434" y2="198"/><line x1="420" y1="198" x2="420" y2="250"/>
    <line x1="560" y1="70" x2="560" y2="92"/><rect x="550" y="92" width="20" height="26" class="f-mut"/>
    <path d="M560 118 q9 5 0 10 q9 5 0 10 q9 5 0 10"/><line x1="560" y1="148" x2="560" y2="190"/>
    <line x1="546" y1="190" x2="574" y2="190"/><line x1="546" y1="198" x2="574" y2="198"/><line x1="560" y1="198" x2="560" y2="250"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="268" y="110">ESR</text><text x="268" y="138">ESL+L_mount</text><text x="268" y="200">C</text>
    <text x="200" y="270">100 µF 1206 × n₁</text><text x="380" y="270">10 µF 0603 × n₂</text><text x="520" y="270">1 µF 0402 × n₃</text>
  </g>
  <circle cx="680" cy="160" r="22" class="f-warn" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="680" y1="70" x2="680" y2="138"/><line x1="680" y1="182" x2="680" y2="250"/>
    <line x1="680" y1="172" x2="680" y2="148"/><line x1="674" y1="154" x2="680" y2="148"/><line x1="686" y1="154" x2="680" y2="148"/></g>
  <g fill="currentColor" font-size="12"><text x="620" y="62">SoC 부하 (ΔI 스텝)</text>
    <text x="40" y="292">병렬 = 어드미턴스 합: Y = 1/Z_vrm + Σ nₖ/Zₖ ,  Z_PDN = 1/Y  ·  같은 부품 n 개 = (C·n, ESR/n, ESL/n)</text></g>
</svg>
```

> 데이터는 **합성(synthetic)** 이다. 부품 값·VRM 모델·가격은 `pdn_spec.json` 의 **대표값**(데이터시트/제조사 모델 확인 필요)이고, 테스트는 이 JSON과 자기 일관적이다. 측정 파일(.s2p)은 `gen_vna.py`(고정 seed)가 만든다.

### Part A — Ztarget·커패시터 Z·병렬망·피크 찾기

- `z_target(v_nom, ripple_pct, delta_i)` — VDD_CORE_0V9: 0.9 V × 3 % / 2 A = **13.5 mΩ**.
- `cap_z(f, c, esr, esl)`, `srf(c, esl)` — 직렬 RLC 모델과 자기 공진 주파수. 테스트: |Z(SRF)| = ESR, SRF/100 에선 1/ωC, 100·SRF 에선 ωL (0.1 % 이내).
- `pdn_z(f, bom, parts, vrm)` — VRM ∥ (부품 k × nₖ) 병렬망. 같은 부품 n 개는 정확히 Z/n.
- `find_peaks(zmag)` — 로그 그리드(10 kHz–1 GHz, 40점/decade = 201점) 위의 국소 최대 = 반공진 후보. rev C 망에서 3개.

### Part B — 반공진: 해석식, ESR의 역설, 피크를 놓치는 검사

- `anti_resonance(c1, l1, c2, l2)` — 두 직렬 LC 가지의 무손실 병렬 공진. 100 µF/1.4 nH ∥ 1 µF/0.8 nH = 3.411 MHz.
- `refine_peak(zfun, fa, fb)` — log f 축 황금분할 탐색으로 피크를 정밀화. `max_z_in_band(zfun, f1, f2, ppd, refine)` — 그리드 + 국소 최대 정밀화 + 대역 끝.
- ESR을 1/4로 줄이면 피크가 3.7배 높아지는 것, 10점/decade 그리드는 피크를 65 %로 읽는 것, "SRF와 decade 점에서만 확인" 하는 검사가 28.8 mΩ 피크를 놓치고 PASS를 주는 것을 테스트로 보인다.

### Part C — VALBRD-A rev C, VDD_CORE_0V9: 최소 BOM과 두 보드의 VNA 측정

- `min_bom(spec, objective)` — 100 µF(0–6) × 10 µF(0–12) × 1 µF(0–12) 1183 조합을 전수 탐색. 10점/decade 그리드로 거르고, 목적함수(비용 또는 개수) 순으로 정밀화 재검증 → **첫 통과가 최소 BOM**.
- 조립된 두 보드를 VNA 2-port shunt-thru로 쟀다(`SN0106_…s2p`, `SN0107_…s2p`). `s21_to_z` 로 Z를 복원하고 `fit_population` 으로 **실제로 실장된 개수**를 추정한다. SN0107에는 결함이 심어져 있다 — 리포트만 보고 찾아야 한다.

## 2. Problem (English)

> **Build a PDN impedance tool and pick the minimum decoupling BOM.**
>
> Using only the Python 3 standard library:
>
> 1. `z_target(v, ripple_pct, delta_i)` → V·(ripple%/100)/ΔI. Capacitor model `Z = ESR + j(ωL − 1/(ωC))`,
>    `srf = 1/(2π√(LC))`. VRM model `Z = R + jωL`. `pdn_z(f, bom, parts, vrm)` returns the impedance of the VRM in parallel
>    with `n_k` copies of each part `k` (sum admittances).
> 2. Evaluate |Z(f)| on a log grid 10 kHz–1 GHz, find local maxima (anti-resonance peaks), and refine each peak with a
>    golden-section search on log f. Show that a coarse grid (10 pts/decade) under-reads a sharp anti-resonance and that
>    checking only at self-resonant frequencies and decade points can pass a design whose true peak is 2× the target.
> 3. For a rail with V = 0.9 V, 3 % ripple, ΔI = 2 A and a band of 10 kHz–20 MHz, exhaustively search the number of
>    100 µF / 10 µF / 1 µF capacitors (part library in `pdn_spec.json`) and return the cheapest BOM whose refined in-band
>    maximum |Z| ≤ Ztarget.
> 4. Read two 2-port shunt-through VNA measurements (Touchstone `.s2p`, `# Hz S DB R 50`), convert `Z = 25·S21/(1 − S21)`,
>    estimate how many of each capacitor are actually populated (least squares on log|Z|), and print a PASS/FAIL report.
>
> Example: `Ztarget = 13.5 mΩ`; a single 100 µF part with 1.4 nH mounted ESL resonates at 425.4 kHz where |Z| = ESR;
> 100 µF ∥ 1 µF (1.4 nH / 0.8 nH) anti-resonates at 3.41 MHz.

## 3. 시작 코드

<!-- code: starter.py -->

레일 스펙과 부품 라이브러리 (대표값):

<!-- code: pdn_spec.json -->

Part C 측정 파일 생성기 (결정적 — 다시 돌려도 .s2p가 바이트 단위로 같다):

<!-- code: gen_vna.py -->

```bash
cd lessons/HWLAB/HWLAB-M03
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 1/27 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: Ztarget / capacitor Z / parallel network / peaks --
FAIL  Ztarget(0.9V, 3%, 2A) = 13.5 mOhm: TypeError: unsupported operand type(s) for *: 'NoneType' and 'float'
FAIL  Ztarget(1.8V, 5%, 0.5A) = 180 mOhm: returned False
FAIL  SRF(100uF, 1.4nH) = 425.4 kHz: TypeError: unsupported operand type(s) for /: 'NoneType' and 'float'
...
PASS  scan grid 10 kHz..1 GHz @ 40/dec = 201 points, exact ends
      (setup failed: TypeError: bad operand type for abs(): 'NoneType')
...
FAIL  SN0107 fit finds injected truth: 10uF populated 2 of 5 (3 missing): TypeError: 'NoneType' object is not subscriptable
FAIL  SN0107 FAIL: measured in-band |Z| above Ztarget in 2 separate bands (VRM-bulk peak + HF end): TypeError: 'NoneType' object is not subscriptable
1/27 passed
exit=1
```

## 4. 힌트

<details>
<summary>힌트 1 — 병렬은 어드미턴스로 더한다</summary>

`Z1 ∥ Z2 = 1/(1/Z1 + 1/Z2)`. 부품 k가 nₖ 개면 `y += n / cap_z(...)`. 파이썬 `complex` 는 그대로 나눗셈이 된다. `abs(z)` 가 |Z|. 같은 부품 n 개 = C·n, ESR/n, ESL/n 인 부품 하나와 정확히 같다 — SRF는 그대로다.

</details>

<details>
<summary>힌트 2 — 반공진 = 두 가지의 리액턴스가 상쇄</summary>

병렬 Z = Z1·Z2/(Z1 + Z2). 무손실이면 Z1 + Z2 = 0 일 때 무한대. `jω(L1+L2) + (1/jω)(1/C1 + 1/C2) = 0` → `ω² = (1/C1 + 1/C2)/(L1 + L2)`. 이 주파수에서 큰 커패시터는 이미 **인덕터**처럼(SRF 위), 작은 커패시터는 아직 **커패시터**처럼(SRF 아래) 동작한다 — LC 탱크.

</details>

<details>
<summary>힌트 3 — 황금분할 탐색 (log f 축)</summary>

국소 최대 i 는 (fs[i−1], fs[i+1]) 안에 진짜 피크를 하나 품는다(그 구간에서 단봉 가정). `x = log10 f` 로 놓고 c = b − 0.618(b−a), d = a + 0.618(b−a). g(c) > g(d) 이면 피크는 [a, d] 에, 아니면 [c, b] 에 있다. 한 번에 구간이 0.618배 — 50번이면 0.05 decade 구간이 약 2e−12 decade로 줄어든다. 줄이면서 c 또는 d 하나는 재사용한다.

</details>

<details>
<summary>힌트 4 — 전수 탐색을 빠르게</summary>

조합이 1183개, 그리드가 34점이면 4만 번의 복소 연산이다. 부품별 어드미턴스 `1/cap_z` 를 그리드에서 **한 번만** 계산해 두고, 조합마다 `Y = Y_vrm + Σ n·Y_k` 만 더한다. |Z| 가 Ztarget을 넘는 순간 그 조합은 `break`. 그리드 통과 후보만 정렬해 앞에서부터 정밀화 — 정밀화는 비싸니 후보 몇 개에만 쓴다.

</details>

<details>
<summary>힌트 5 — shunt-thru 공식</summary>

두 50 Ω 포트 사이에 DUT Z 가 접지로 걸려 있다. 포트 2 쪽은 Z ∥ 50 Ω 이고, S21 = 2Z/(2Z + Z0), S11 = −Z0/(2Z + Z0) = S21 − 1. Z 에 대해 풀면 Z = (Z0/2)·S21/(1 − S21). 10 mΩ 이면 S21 ≈ 4e−4 (−68 dB).

</details>

<details>
<summary>힌트 6 — 실장 개수 추정</summary>

모델 |Z| 와 측정 |Z| 를 **log 축**에서 비교한다(1 mΩ 와 100 mΩ 를 같은 비율 오차로). 모든 개수 조합에 대해 RMS(log10 |Zmeas| − log10 |Zmodel|)를 구하고 최소를 고른다. 2등과의 차이가 추정의 확신도다. 부품 공차(C ±10 %)가 있으니 RMS는 0이 되지 않는다.

</details>

## 5. 풀이

### Part A — Ztarget에서 |Z(f)| 곡선까지

**① Ztarget.** VDD_CORE_0V9: V = 0.9 V, 허용 동적 리플 3 % = 27 mV, 부하 스텝 ΔI = 2 A → Ztarget = 27 mV / 2 A = **13.5 mΩ**. 가정을 같이 말해야 한다:

| 가정 | 의미 | 깨지면 |
|---|---|---|
| 선형·시불변 | ΔV(f) = I(f)·Z(f) | 큰 스텝에서 VRM이 포화/모드 전환(PFM↔PWM) |
| 평탄(flat) 타겟 | 모든 주파수에서 같은 한도 | ΔI 스펙트럼이 좁으면 과설계, 피크가 높은 Q면 시간 영역 링잉(아래) |
| ΔI = 스텝 크기 | 보통 최대 전류의 일부(예: 50 %) | 실제 di/dt·반복 주파수가 피크와 겹치면 더 나쁨 (rogue wave) |
| 리플 % = 동적 몫 | DC 정확도(VRM 설정, 부하 레귤레이션)와 나눔 | 전체 ±3 %를 다 쓰면 DC 오차 몫이 없음 (HWLAB-B08 참고) |

**② 커패시터 한 개.** Z = ESR + j(ωL − 1/ωC). 저주파에선 −1/ωC (−20 dB/dec), SRF = 1/(2π√(LC)) 에서 리액턴스가 상쇄되어 |Z| = ESR, 그 위에선 +ωL (+20 dB/dec) — 로그-로그 그래프의 **V자**. 100 µF / 1.4 nH → SRF 425.4 kHz, 10 µF / 1.0 nH → 1.59 MHz, 1 µF / 0.8 nH → 5.63 MHz.

L은 부품 자체 ESL(0402 ≈ 0.35 nH 수준) **+ 실장 인덕턴스**(패드 → 비아 → 평면까지의 루프, 0.3–0.6 nH 수준)다. 이 레슨의 라이브러리는 둘을 나눠 적었다(`esl_part_h`, `l_mount_h`). 실장 L이 부품 ESL보다 크거나 비슷하다는 게 핵심이다 — **SRF 위에서 커패시터는 "값"이 아니라 "실장 루프 인덕턴스"** 이고, 값을 바꿔도 고주파 성능은 거의 안 변한다.

**③ 병렬망.** 같은 부품 n 개는 (nC, ESR/n, ESL/n) — V자가 SRF는 그대로 둔 채 아래로 1/n 내려간다. 서로 다른 부품과 VRM은 어드미턴스로 더한다. `python3 -I solution.py --profile` 실제 출력(일부, mΩ):

```text
    f [Hz]   |Z| rev C [mOhm]   VRM only   100u only x2   10u only x5   1u only x3
      1e+04          1.59          1.58         79.54        318.30       5305.15
   6.31e+04          4.91          3.51         12.40         50.38        840.72
      1e+05         12.57          5.25          7.62         31.72        530.36
   1.58e+05          5.90          8.11          4.50         19.91        334.49
   3.98e+05          1.21         20.07          1.27          7.56        132.65
      1e+06          2.94         50.29          3.81          2.17         51.53
   1.58e+06          0.96         79.68          6.59          1.00         31.08
   3.98e+06          4.62        200.12         17.35          4.32          7.77
      1e+07          5.35        502.66         43.92         12.29         12.13
   2.51e+07         15.04       1262.61        110.45         31.45         40.17
      1e+08         60.91       5026.55        439.82        125.64        167.07
      1e+09        609.80      50265.48       4398.23       1256.63       1675.47
```

100 kHz 행을 보면 이상한 점이 있다: VRM 혼자 5.25 mΩ, 100 µF×2 혼자 7.62 mΩ 인데 **둘을 합친 rev C는 12.57 mΩ — 각각보다 크다.** 병렬로 붙였는데 임피던스가 커지는 것, 이게 반공진이다. VRM의 등가 인덕턴스(8 nH)와 벌크 커패시턴스가 LC 탱크를 이룬다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">|Z(f)| (실제 모델 값, 로그-로그): rev C = VRM ∥ 100µ×2 ∥ 10µ×5 ∥ 1µ×3</text>
  <rect x="80" y="30" width="429" height="220" class="f-mut" opacity=".35"/>
  <g stroke="currentColor" opacity=".35"><line x1="80" y1="40" x2="730" y2="40"/><line x1="80" y1="92.5" x2="730" y2="92.5"/>
    <line x1="80" y1="145" x2="730" y2="145"/><line x1="80" y1="197.5" x2="730" y2="197.5"/><line x1="80" y1="250" x2="730" y2="250"/></g>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="74" y="44">1 Ω</text><text x="74" y="97">100 mΩ</text>
    <text x="74" y="149">10 mΩ</text><text x="74" y="202">1 mΩ</text><text x="74" y="254">0.1 mΩ</text></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="80" y="268">10k</text><text x="210" y="268">100k</text>
    <text x="340" y="268">1M</text><text x="470" y="268">10M</text><text x="600" y="268">100M</text><text x="730" y="268">1G</text></g>
  <line x1="80" y1="138" x2="730" y2="138" stroke="currentColor" stroke-dasharray="7 4" stroke-width="1.5"/>
  <text x="600" y="132" fill="currentColor" font-size="12">Ztarget 13.5 mΩ</text>
  <text x="300" y="47" fill="currentColor" font-size="12">목표 대역 10 kHz – 20 MHz</text>
  <g fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2 3">
    <polyline points="80,187 102,186 123,183 145,180 167,174 188,167 210,160 232,151 253,143 275,134 297,126 318,117 340,108 362,99 383,91 405,82 427,73 448,64 470,56 492,47 513,38 531,31"/>
    <polyline points="80,98 102,106 123,115 145,124 167,133 188,142 210,151 232,161 253,172 266,180 279,188 292,192 305,188 318,179 340,167 362,156 383,147 405,138 427,129 448,120 470,111 492,102 513,94 535,85 557,76 578,67 600,59 622,50 643,41 665,32"/>
    <polyline points="80,66 102,75 123,84 145,92 167,101 188,110 210,119 232,128 253,136 275,146 297,155 318,166 336,177 353,190 366,197 375,194 388,183 405,171 427,160 448,150 470,140 492,131 513,122 535,114 557,105 578,96 600,87 622,79 643,70 665,61 687,52 708,44 730,35"/>
    <polyline points="154,32 175,40 197,49 219,58 240,67 262,75 284,84 305,93 327,102 349,111 370,121 392,132 409,144 422,155 435,166 440,166 448,159 466,144 483,132 505,121 526,111 548,102 570,93 591,84 613,76 635,67 656,58 678,49 700,40 721,32"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="80,187 89,186 97,186 106,185 115,184 123,183 132,181 141,179 149,177 158,174 167,170 175,166 184,161 193,155 201,147 206,143 210,140 214,138 219,140 223,143 227,148 236,157 245,165 253,172 262,178 271,184 279,190 288,193 292,193 297,192 305,186 314,178 323,170 327,167 331,167 336,169 344,178 353,188 362,196 370,198 379,193 388,186 396,179 405,172 414,166 422,161 427,162 431,166 435,171 440,174 448,172 457,167 466,162 474,157 483,153 492,149 500,145 509,141 518,138 526,134 535,130 544,127 552,123 561,120 570,116 578,113 587,109 596,106 604,102 613,99 622,95 630,92 639,88 648,85 656,81 665,78 674,74 682,71 691,67 700,64 708,60 717,57 726,53 730,51"/>
  <g class="f-warn" stroke="currentColor"><circle cx="214" cy="138" r="5"/><circle cx="330" cy="167" r="5"/><circle cx="423" cy="161" r="5"/></g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="128">① VRM L ∥ 벌크 C</text><text x="290" y="216">② 100µ ∥ 10µ</text><text x="420" y="196">③ 10µ ∥ 1µ</text>
    <text x="520" y="200">↗ 병렬 ESL 98 pH (+20 dB/dec)</text>
    <text x="10" y="290">점선: VRM 혼자(대각선) · 100µ×2 · 10µ×5 · 1µ×3 각각의 V자 · 굵은 선: 병렬 결과. 피크 = 반공진</text>
    <text x="10" y="310">①108 kHz 13.49 mΩ (Ztarget에 0.07 % 여유) ②836 kHz 3.89 mΩ ③4.36 MHz 5.00 mΩ — 정밀화 값</text>
  </g>
</svg>
```

**④ 피크 찾기.** 그리드 |Z| 의 국소 최대(왼쪽보다 크고 오른쪽보다 크거나 같음)가 반공진 후보다. 40점/decade 그리드에서 rev C의 실제 출력: `1.06e+05 Hz 13.41 mOhm, 8.41e+05 Hz 3.88 mOhm, 4.47e+06 Hz 4.96 mOhm` — 세 피크의 그리드 값은 정밀화 값(13.49 / 3.89 / 5.00 mΩ)보다 조금씩 낮다. 이 "조금" 이 Part B의 주제다.

### Part B — 반공진은 어디에, 얼마나 높이

**주파수.** 두 직렬 RLC 가지의 병렬 Z = Z1Z2/(Z1+Z2). 손실을 무시하면 분모 Z1+Z2 = j[ω(L1+L2) − (1/ω)(1/C1+1/C2)] 가 0이 되는 ω 에서 Z → ∞:

  ω_ar² = (1/C1 + 1/C2)/(L1 + L2) = 1/((L1+L2)·Cs),  Cs = C1C2/(C1+C2)

C1 ≫ C2 면 Cs ≈ C2 → f_ar ≈ 1/(2π√((L1+L2)C2)): **큰 커패시터의 ESL과 작은 커패시터의 C가 만드는 탱크**다. rev C 피크 ②는 (200 µF, 0.7 nH) ∥ (50 µF, 0.2 nH) → 838.8 kHz (정밀화 835.6 kHz), 피크 ③은 (50 µF, 0.2 nH) ∥ (3 µF, 0.267 nH) → 4.379 MHz (정밀화 4.357 MHz). 다른 가지들이 조금 끌어당겨 해석식과 1 % 이내로 다르다. 피크 ①은 VRM(8 nH) ∥ 전체 C 253 µF → 1/(2π√(8 nH·253 µF)) = 111.9 kHz (정밀화 108.3 kHz).

**높이.** 반공진에서 Z1 ≈ R1 + jX, Z2 ≈ R2 − jX 이므로 Z = Z1Z2/(R1+R2) ≈ **X²/(R1+R2)** (X ≫ R 일 때). X 는 그 주파수에서 한 가지의 리액턴스 ≈ 특성 임피던스 √((L1+L2)/Cs). 결론이 반직관적이다:

- **ESR이 작을수록 피크가 높다.** 손실이 없는 LC 탱크는 Q가 무한대다. 실제 출력 (100 µF ∥ 1 µF 한 개씩, VRM 없음):

```text
      100uF||1uF: f_ar(lossless)=3.4101 MHz  refined peak 3.3459 MHz 65.66 mOhm  X^2/(R1+R2)=60.14 mOhm
PASS  refined |Z| peak frequency within 3% of lossless f_ar (Q~2 shifts it a little)   [-1.88%]
PASS  peak height within 10% of X^2/(R1+R2)   [+9.2%]
PASS  ESR x0.25 -> anti-resonance peak x>3 (lower ESR = higher Q = taller peak)   [65.7 -> 242.1 mOhm (x3.69)]
```

  Q = X/(R1+R2) ≈ 29.5/14.5 ≈ 2 로 낮아서 근사가 9 % 틀리고 피크 위치도 1.9 % 어긋난다. ESR을 1/4로 줄이면(Q ≈ 8) 근사가 4 × 60.14 = 240.6 mΩ, 실제 242.1 mΩ — Q가 클수록 근사가 맞는다. 테스트 허용치(주파수 3 %, 높이 10 %)는 이 Q≈2 경우의 근사 오차를 덮도록 잡은 것이다.
- 그래서 "초저 ESR MLCC만 쓰면 좋다" 는 틀렸다. 대책: (a) 값 간격을 좁히거나 같은 값 여러 개로 반공진 자체를 줄이기, (b) 병렬 개수를 늘려 탱크의 특성 임피던스 √(L/C)를 낮추기, (c) **의도적으로 ESR이 있는 부품**(폴리머/탄탈 벌크, controlled-ESR MLCC)으로 감쇠 — 연습 3에서 최적 ESR ≈ √(L/C) 를 확인한다.

**그리드가 거짓말하는 방식.** 피크의 −3 dB 폭은 대략 f_ar/Q. Q ≈ 8 이면 폭이 f_ar의 1/8 = 0.05 decade 정도인데 10점/decade 그리드 간격은 0.1 decade다. 실제 출력 (low-ESR 쌍):

```text
      grid density vs worst |Z| found (low-ESR pair, 10k..20M):
           5/dec  grid max  220.34 mOhm  ( 91.0% of true peak)
          10/dec  grid max  157.49 mOhm  ( 65.1% of true peak)
          20/dec  grid max  157.49 mOhm  ( 65.1% of true peak)
          40/dec  grid max  227.04 mOhm  ( 93.8% of true peak)
         100/dec  grid max  241.38 mOhm  ( 99.7% of true peak)
          10/dec + refine   242.07 mOhm @ 3.4046 MHz
```

눈여겨볼 것: **5점/decade가 10·20점보다 정확하다.** 그리드가 피크에 얼마나 가까이 떨어지느냐는 운이다 — 그리드 밀도와 오차가 단조 관계가 아니다. 20점 그리드는 10점 그리드 점을 모두 포함하고, 추가된 점도 피크를 비켜 갔다. 그래서 "그리드를 촘촘히" 가 아니라 **"국소 최대 주변을 정밀화"** 가 맞는 해법이다. 국소 최대 i 의 (fs[i−1], fs[i+1]) 구간에는 진짜 피크가 있다(그 구간에서 단봉이라면). 황금분할로 줄이면 10점 그리드 + 정밀화가 50,000점/decade 무차별 탐색과 0.01 % 이내로 같다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">날카로운 반공진과 거친 그리드: 점은 피크 어깨에만 떨어진다</text>
  <line x1="60" y1="210" x2="720" y2="210" stroke="currentColor"/>
  <path d="M60 200 C 200 190, 300 170, 340 120 C 360 80, 372 42, 382 40 C 392 42, 404 80, 424 120 C 464 170, 560 190, 720 200" fill="none" stroke="currentColor" stroke-width="2"/>
  <line x1="60" y1="96" x2="720" y2="96" stroke="currentColor" stroke-dasharray="7 4"/>
  <text x="600" y="90" fill="currentColor" font-size="12">Ztarget</text>
  <g class="f-ok" stroke="currentColor"><circle cx="120" cy="197" r="5"/><circle cx="230" cy="186" r="5"/><circle cx="340" cy="120" r="5"/>
    <circle cx="450" cy="148" r="5"/><circle cx="560" cy="186" r="5"/><circle cx="670" cy="198" r="5"/></g>
  <g class="f-warn" stroke="currentColor"><circle cx="382" cy="40" r="6"/></g>
  <g stroke="currentColor"><line x1="340" y1="214" x2="340" y2="228"/><line x1="450" y1="214" x2="450" y2="228"/><line x1="340" y1="222" x2="450" y2="222"/></g>
  <g fill="currentColor" font-size="12">
    <text x="392" y="44">진짜 피크 (Ztarget 초과)</text>
    <text x="130" y="140">그리드 최대 = 국소 최대 i (Ztarget 아래 → "PASS")</text>
    <text x="350" y="244">정밀화 구간 (fs[i−1], fs[i+1]) — 여기서 황금분할</text>
  </g>
</svg>
```

**"SRF와 decade 점만 확인" 하는 검사.** 커패시터마다 SRF에서 가장 낮으니 "각 값의 SRF가 대역을 덮고, 그 점들이 Ztarget 아래면 된다" 는 사고방식이 흔하다. 문제는 위험한 곳이 SRF가 아니라 **SRF 사이**라는 것. 실제 출력 (100 µF×2 + 1 µF×6, 10 µF 없음):

```text
      naive BOM 100u x2 + 1u x6: SRF/decade check worst 13.06 mOhm -> PASS | true max 28.81 mOhm @ 2.347 MHz
PASS  'check only at SRFs + decades' says PASS ...
PASS  ... but the true in-band max (anti-resonance between them) exceeds Ztarget   [28.81 > 13.50 mOhm @ 2.347 MHz]
```

SRF 425 kHz(1.24 mΩ), 5.63 MHz(1.98 mΩ), decade 점 10k/100k/1M/10M과 20 MHz(13.06 mΩ)는 전부 통과. 그런데 (200 µF, 0.7 nH) ∥ (6 µF, 0.133 nH) 반공진 = 2.284 MHz 해석값 근처에서 **28.8 mΩ = Ztarget의 2.1배**. 값 사이가 두 decade 벌어지면(100 µF → 1 µF) 탱크의 특성 임피던스가 커진다 — 중간 값(10 µF)이 필요한 이유다.

### Part C — 최소 BOM을 고르고, 보드에서 확인한다

**탐색.** 목적함수를 먼저 정해야 "최소" 가 정의된다. 이 레슨은 기본을 **BOM 비용**(동률이면 개수, 그다음 max|Z|)으로, 비교용으로 **부품 개수**(배치 면적·조립 시간 대리)를 같이 돌린다. 실제 출력:

```text
      min-cost : 100u×2 10u×5 1u×3  $0.58  10 parts  max|Z|=13.49 mOhm @ 0.108 MHz  (grid-pass 720, rejected after refine 22)
        rejected 100u×2 10u×1 1u×6: grid OK but refined 13.97 mOhm @ 0.116 MHz
        rejected 100u×2 10u×1 1u×7: grid OK but refined 13.97 mOhm @ 0.116 MHz
        rejected 100u×2 10u×2 1u×5: grid OK but refined 13.87 mOhm @ 0.114 MHz
      min-count: 100u×3 10u×1 1u×5  $0.68  9 parts  max|Z|=12.58 mOhm @ 20.000 MHz  (grid-pass 720, rejected after refine 4)
PASS  min-cost BOM == bom_rev_c in pdn_spec.json   [100u×2 10u×5 1u×3]
PASS  min-cost BOM really meets Ztarget over 10k..20M (dense 2000/dec)
PASS  at least one grid-passing candidate was rejected by peak refinement   [22 rejected]
```

읽는 법:

| 관찰 | 의미 |
|---|---|
| 그리드 통과 720개 중 **22개가 정밀화에서 탈락** | 10점/decade 그리드만 믿었다면 100 µF×2 + 10 µF×1 + 1 µF×6 ($0.49)을 "최소 BOM" 이라고 냈을 것이다 — 실제 피크 13.97 mΩ로 FAIL. 탈락 후보의 피크가 전부 110 kHz 근처 = VRM–벌크 반공진 |
| 최소 비용 BOM의 최악점이 **108 kHz, 13.49 mΩ** | 벌크 개수는 VRM 반공진이 정한다. 10 µF×5 는 고주파용이 아니라 이 피크를 누르는 C(+50 µF)를 싸게 보태는 역할 |
| 최소 개수 BOM은 100 µF를 3개로 늘리고 10 µF를 1개로 | 벌크로 저주파를 누르면 10 µF가 덜 필요 — 대신 개당 $0.20 |
| 두 BOM 모두 마진이 거의 0 | **최적화기는 항상 제약의 경계에 붙는다.** 공차·온도·DC bias 코너에서 다시 돌려야 한다 (연습 1: 코너에서 rev C는 21.5 mΩ로 FAIL) |
| 고주파 끝 20 MHz는 병렬 ESL이 정한다 | rev C의 Σn/ESL = 2/1.4 + 5/1.0 + 3/0.8 = 10.18 /nH → L_eff 98 pH → 20 MHz에서 ωL_eff = 12.3 mΩ (실제 |Z| 11.8 mΩ — 남은 용량성 성분이 조금 상쇄). 필요조건 Σn/ESL ≥ 2π·20 MHz/13.5 mΩ = 9.31 /nH (연습 4) |

20 MHz 위를 보드 커패시터로 책임지지 않는 이유: SoC 다이에서 보면 보드 커패시터 앞에 **패키지·BGA 볼·비아의 인덕턴스**가 직렬로 있다. 그 위 주파수는 패키지 커패시터와 온다이 커패시턴스(수백 nF급)의 몫이고, 보드 캡을 더 붙여도 다이에서 보이는 Z는 거의 안 줄어든다. 대역 상한은 실제 설계에선 패키지 모델과 함께 정한다(JSON의 `band_hz` 는 이 레슨의 가정).

**측정: 2-port shunt-thru.** mΩ 대 임피던스는 1-port 반사(S11)로 못 잰다 — S11 ≈ −0.9996 이라 0.01 dB 오차가 Z 오차 288 %가 된다(연습 2). 그래서 DUT를 두 포트 사이에 **션트로** 걸고 S21을 잰다:

```svg
<svg viewBox="0 0 760 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">2-port shunt-thru: S21 = 2Z/(2Z+Z0)  →  Z = 25·S21/(1−S21)</text>
  <rect x="20" y="60" width="120" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="620" y="60" width="120" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="300" y="40" width="160" height="110" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle"><text x="80" y="90" font-weight="bold">VNA port 1</text><text x="80" y="110" font-size="12">50 Ω 소스</text>
    <text x="680" y="90" font-weight="bold">VNA port 2</text><text x="680" y="110" font-size="12">50 Ω 수신</text>
    <text x="380" y="64" font-size="12">보드 레일 (VRM ON)</text><text x="380" y="140" font-size="12">Z_PDN ≈ mΩ</text></g>
  <g stroke="currentColor" fill="none"><line x1="140" y1="95" x2="200" y2="95"/><rect x="200" y="85" width="40" height="20" class="f-warn"/>
    <line x1="240" y1="95" x2="340" y2="95"/><line x1="420" y1="95" x2="520" y2="95"/><rect x="520" y="85" width="40" height="20" class="f-warn"/>
    <line x1="560" y1="95" x2="620" y2="95"/><line x1="380" y1="95" x2="380" y2="105"/><rect x="368" y="105" width="24" height="22"/>
    <line x1="380" y1="127" x2="380" y2="134"/><line x1="368" y1="134" x2="392" y2="134"/></g>
  <circle cx="340" cy="95" r="3" fill="currentColor"/><circle cx="420" cy="95" r="3" fill="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="186" y="80">DC block</text><text x="508" y="80">DC block</text>
    <text x="20" y="175">두 포트가 각자 레일 노드에 닿는다 → 접촉 저항이 측정에서 빠진다 (4단자 원리)</text>
    <text x="20" y="195">저주파에선 케이블 실드 그라운드 루프가 오차 → 공통모드 트랜스포머로 끊는다</text></g>
</svg>
```

S21 = 2Z/(2Z+Z0) 에서 Z가 mΩ면 S21은 −60~−90 dB, 측정이 "작은 숫자를 직접" 재는 구조라 상대 오차가 그대로 유지된다(0.01 dB → 0.115 %). 포트 1의 소스 전류가 Z를 지나며 만든 전압을 포트 2가 **따로** 읽으니 프로브 접촉 저항이 거의 상쇄된다 — 켈빈(4단자) 측정의 RF 버전이다. VRM을 켠 채(in-situ) 재려면 DC 블록이 필요하다.

**판정과 실장 추정.** `board_report` 는 .s2p → Z → 대역 내 최악 |Z| 판정 + `fit_population` 으로 개수 추정. 실제 출력:

```text
PASS VALBRD-A rev C SN0106 VDD_CORE_0V9: |Z|max=12.2mΩ @ 0.11MHz <= Ztarget 13.5mΩ | fit 100u×2 10u×5 1u×3 (rms 0.021 dec, next 0.036) vs BOM rev C 100u×2 10u×5 1u×3 -> population OK
FAIL VALBRD-A rev C SN0107 VDD_CORE_0V9: |Z|max=16.6mΩ @ 19.95MHz > Ztarget 13.5mΩ | fit 100u×2 10u×2 1u×3 (rms 0.015 dec, next 0.048) vs BOM rev C 100u×2 10u×5 1u×3 -> C10u_0603 3 missing
    over Ztarget: 0.112–0.119 MHz (peak 13.9mΩ); 16.8–20 MHz (peak 16.6mΩ)
PASS  SN0106 population fit == BOM rev C and PASS   [100u×2 10u×5 1u×3]
PASS  SN0107 fit finds injected truth: 10uF populated 2 of 5 (3 missing)   [100u×2 10u×2 1u×3]
PASS  SN0107 FAIL: measured in-band |Z| above Ztarget in 2 separate bands (VRM-bulk peak + HF end)   [16.64 mOhm @ 19.95 MHz, 2 bands]
```

SN0107 은 **두 군데**에서 넘는다. 그게 10 µF 미실장의 지문이다:

| 넘는 곳 | 왜 | 10 µF 3개(30 µF, 병렬 ESL 경로 3개)가 빠진 효과 |
|---|---|---|
| 0.112–0.119 MHz, 13.9 mΩ | VRM–벌크 반공진 | 전체 C 253 → 223 µF: 공진이 위로(114 kHz), 특성 임피던스 √(L/C) ↑ → 피크 ↑ |
| 16.8–20 MHz, 16.6 mΩ | 병렬 ESL | Σn/ESL 10.18 → 7.18 /nH, L_eff 98 → 139 pH → 20 MHz에서 ωL = 17.5 mΩ |

하나의 결함이 저주파와 고주파 양쪽에 흔적을 남기므로, 개수 조합을 log|Z| 전 대역(≤ 100 MHz)에 맞추면 구분이 된다. 1등 rms 0.015 decade(≈ 3.5 %, 부품 공차 수준) vs 2등 0.048 — 3배 차이라 확신할 만하다. 단, **임피던스로는 "몇 개" 만 알 수 있고 "어느 것(C41…C45 중 무엇)" 은 모른다** — 다음 행동은 AOI/육안/X-ray와 해당 릴(feeder) 기록 확인이다.

하드웨어 엔지니어에게 넘길 한 줄:

`FAIL VALBRD-A SN0107 VDD_CORE_0V9: |Z| 16.6 mΩ @ 20 MHz, 13.9 mΩ @ 114 kHz > Ztarget 13.5 mΩ (VNA shunt-thru, VRM on). Model fit = 10 µF 0603 ×2 of 5 (3 missing, rms 0.015 vs next 0.048 dec); SN0106 same setup 12.2 mΩ PASS → board defect, not fixture. Check C41–C45 placement (AOI / feeder log).`

SN0106 이 12.2 mΩ로 모델(13.49 mΩ)보다 낮게 나온 것도 해석해야 한다: `gen_vna.py` 의 참값 모델(공차 적용)로 확인하면 이 보드의 진짜 피크는 12.23 mΩ @ 105.5 kHz — **부품 공차 추첨이 유리했던 것**이다. 또 일반적으로는 측정 그리드(40점/decade)가 피크 꼭대기를 밟는다는 보장도 없다. **골든 보드 한 장의 PASS는 설계 마진의 증거가 아니다** — 마진은 코너 분석(연습 1)과 여러 보드의 분포로 말한다(HWLAB-B09 참고).

**장비가 있을 때 (이 레슨에선 실행하지 않음):** 저주파 게인-위상 포트가 있는 VNA(예: Keysight E5061B 계열, OMICRON Lab Bode 100)로 shunt-thru를 구성하고, 측정 전 THRU 정규화(또는 SOLT 교정), DC 블록, 저주파 그라운드 루프 대책을 둔다. 피크 근처는 **세그먼트 스윕**으로 점을 몰아 준다 — 측정 데이터는 사후 정밀화가 불가능하므로(모델과 달리 사이 값을 계산할 수 없다), 그리드 문제는 측정 설정에서 해결해야 한다. Touchstone 파일을 받아 이 레슨의 `load_s2p` → `s21_to_z` → `board_report` 에 그대로 넣는다. 시간 영역 확인은 부하 스텝으로 droop을 재는 것(HWLAB-M02 참고).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"         # 27/27 passed, exit=0  (실측 0.25–0.3 s)
python3 -I solution.py --profile                 # rev C 와 각 부품군의 |Z(f)| 표
python3 -I -W error -X dev solution.py           # 경고 없음
python3 -I gen_vna.py                            # .s2p 재생성 → shasum 동일 (결정적)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/행동 |
|---|---|---|
| 커패시터를 이상적 C(1/ωC)로만 계산 | SRF 위에서 인덕터가 된다. 1 µF/0.8 nH 는 5.6 MHz 위에선 0.8 nH 인덕터 | `Z = ESR + j(ωL − 1/ωC)` |
| 데이터시트 ESL만 쓰고 실장 L 무시 | 패드·비아 루프가 부품 ESL과 같거나 큼 → 고주파 개수를 절반으로 오판 | `esl = esl_part + l_mount` (레이아웃 추출 값) |
| 직렬 저항·리액턴스를 각각 병렬로 더함 | 병렬은 **복소 어드미턴스** 합 | `y += n / cap_z(...)`, `Z = 1/y` |
| SRF·decade 점에서만 확인 | 위험한 곳은 SRF **사이** (반공진) — 28.8 mΩ를 13.06으로 봄 | 대역 전체 그리드 + 국소 최대 정밀화 |
| 그리드를 촘촘히 하면 된다고 생각 | 오차가 밀도에 단조가 아님 (5점 91 %, 20점 65 %) | 국소 최대마다 황금분할 |
| "ESR 낮을수록 좋다" | 반공진 피크 ≈ X²/(R1+R2) — ESR 1/4 → 피크 3.7배 | 감쇠용 ESR, 값 간격 축소, 개수 증가 |
| VRM을 빼고 커패시터만 계산 | VRM 등가 L ∥ 벌크 C 의 반공진(108 kHz)이 이 레일의 최악점 | VRM 출력 Z 모델 포함 (루프 안정성은 HWLAB-M02) |
| 최적화 결과를 마진 없이 BOM에 | 최적해는 경계(0.07 % 마진)에 붙는다. 코너에서 21.5 mΩ | 공차·온도·DC bias 코너로 탐색 (연습 1) |
| 1-port S11로 mΩ 측정 | S11 ≈ −1 근처라 0.01 dB가 Z 288 % 오차 | 2-port shunt-thru, Z = 25·S21/(1−S21) |
| 측정 |Z|를 선형 축에서 모델과 비교 | 큰 |Z|(고주파) 점이 적합을 지배 | log10|Z| 차이의 RMS |
| 골든 보드 PASS = 설계 OK | 공차 추첨과 측정 그리드 운 | 코너 분석 + 여러 보드 분포 |

## 8. 면접 Q&A

<details>
<summary>Q1. 타겟 임피던스는 어떻게 정하고, 어떤 가정이 깔려 있나요?</summary>

Ztarget = V·(허용 동적 리플)/ΔI. 0.9 V, 3 %, 2 A 스텝이면 13.5 mΩ. 가정: PDN이 선형이라 ΔV(f) = I(f)·Z(f), 그리고 |Z| 가 대역 전체에서 평탄하게 한도 아래면 어떤 스펙트럼의 전류에도 리플이 한도 안이다. ΔI는 최대 전류 전체가 아니라 실제 스텝 크기(워크로드 특성, 흔히 최대의 절반 수준을 가정)이고, 리플 %는 DC 정확도 몫을 뺀 동적 몫이다.
꼬리질문: "평탄 타겟이 너무 보수적이거나 부족한 경우는?" → ΔI 스펙트럼이 좁으면 과설계(비싼 BOM). 반대로 피크가 Ztarget 아래라도 Q가 높으면 부하 전류가 그 주파수로 반복될 때(예: 루프 주기적 워크로드) 응답이 공진으로 쌓이고, 스텝 응답에도 링잉이 남는다 — 그래서 "평탄하고 감쇠된(flat & damped)" 임피던스를 목표로 하는 설계 철학이 있다. 시간 영역 최악 패턴은 부하 스텝 측정으로 확인한다(HWLAB-M02).

</details>

<details>
<summary>Q2. 반공진(anti-resonance)이 뭔가요? 주파수와 높이를 어떻게 추정하나요?</summary>

두 커패시터(또는 VRM과 벌크)가 병렬일 때, 큰 쪽은 SRF를 지나 인덕터가 되고 작은 쪽은 아직 커패시터인 주파수에서 둘이 LC 탱크를 이뤄 병렬 Z가 솟는다. 무손실이면 Z1+Z2 = 0 → ω² = (1/C1+1/C2)/(L1+L2). 높이는 X²/(R1+R2) (X ≫ R), X ≈ √((L1+L2)/Cs). 이 레슨의 100 µF∥1 µF는 3.41 MHz, 65.7 mΩ.
꼬리질문: "어떻게 줄이나?" → ① 값 간격을 좁힌다(100 µF→1 µF 두 decade 점프 대신 10 µF를 끼움 — Part B에서 28.8 mΩ 피크가 사라짐), ② 개수를 늘려 탱크의 특성 임피던스를 낮춘다, ③ ESR로 감쇠한다(폴리머 벌크, controlled-ESR MLCC — 최적 ESR ≈ √(L/C)), ④ 같은 값·같은 패키지를 여러 개 쓰면 그 커패시터들 사이에는 반공진이 없다(SRF가 같으니) — VRM·패키지와의 반공진은 남는다.

</details>

<details>
<summary>Q3. "핀마다 0.1 µF 하나" 규칙은 왜 이제 잘 안 맞나요?</summary>

그 규칙은 수 MHz 클럭, 수백 mΩ 타겟 시절의 것이다. SRF 위에서 커패시터는 **실장 루프 인덕턴스**로 동작하고, 같은 0402 패키지면 0.1 µF든 1 µF든 고주파 Z는 거의 같다 — 그러면 더 큰 값이 저주파까지 덮어 주니 손해가 없다. mΩ 타겟에서는 값보다 **병렬 개수(Σn/ESL)와 비아 설계**가 고주파를 정하고, 필요한 개수는 n ≥ 2π·f_hi·L_mount/Ztarget 로 바로 나온다(연습 4). 그리고 서로 다른 값을 마구 섞으면 반공진이 늘어난다.
꼬리질문: "실장 인덕턴스는 어떻게 줄이나?" → 비아를 패드 바로 옆(via-in-pad 또는 짧은 트레이스)에, 패드당 비아 2개 이상, 전원/접지 평면을 표면 가까운 층에(루프 면적 ↓), 역방향 기하(reverse-geometry, 예: 0204) 또는 다단자(IDC/X2Y 계열) 커패시터.

</details>

<details>
<summary>Q4. 보드 디커플링은 몇 MHz까지 책임지나요? 그 위는요?</summary>

다이에서 보면 보드 커패시터 앞에 패키지·BGA 볼·비아·평면 확산(spreading) 인덕턴스가 직렬로 있다. 그 인덕턴스와 온다이 커패시턴스가 또 하나의 반공진(흔히 수십~수백 MHz, "패키지–다이 공진")을 만들고, 그 위는 온다이 C만 유효하다. 보드 캡은 대략 수~수십 MHz까지 의미가 있고, 정확한 상한은 패키지 모델(칩 벤더의 PDN 가이드/모델)과 함께 정한다. 이 레슨은 20 MHz로 가정했다.
꼬리질문: "그럼 1 GHz까지 계산하는 의미는?" → 반공진이 대역 경계 바로 위에 있는지, 보드 캡 군의 L_eff가 패키지 공진 주파수를 어디로 옮기는지 보려고. 대역 밖이라도 높은 피크는 다른 레일과의 결합·EMI 문제로 이어질 수 있다.

</details>

<details>
<summary>Q5. 밀리옴 임피던스를 실제로 어떻게 재나요? 1-port로 안 되는 이유는?</summary>

1-port는 Z = Z0(1+S11)/(1−S11). Z = 10 mΩ면 S11 = −0.9996 — 반사계수가 거의 −1이라 작은 크기 오차가 1+S11 에서 증폭된다. 0.01 dB 오차로 Z가 −18.8 mΩ(288 % 오차)이 된다(연습 2). 2-port shunt-thru는 Z = 25·S21/(1−S21), S21 ≈ 4e−4 를 직접 재므로 0.01 dB가 0.115 % 오차다. 두 포트가 각각 레일에 닿으니 접촉 저항이 측정 경로에서 빠진다(켈빈 측정).
꼬리질문: "저주파에서 측정값이 이상하게 평탄하게 높게 나온다?" → 두 케이블의 실드 저항이 만드는 그라운드 루프가 DUT와 병렬/직렬로 끼어든다(대략 100 kHz 이하에서 두드러짐). 공통모드 트랜스포머나 아이솔레이터로 루프를 끊는다. VRM을 켠 채 재려면 DC 블록, 측정 하한(잡음 바닥)은 IFBW를 낮추고 평균으로 내린다.

</details>

<details>
<summary>Q6. VNA로 잰 피크가 시뮬레이션보다 높고 주파수가 다릅니다. 무엇부터 의심하나요?</summary>

주파수가 **올라갔으면** C가 줄었다: DC bias derating(작은 패키지·낮은 정격 전압일수록 심함), X5R/X7R 온도 특성, 미실장. 주파수가 **내려갔으면** L이 늘었다: 실장 L이 모델보다 큼(비아 위치, 평면 거리), 측정 픽스처 인덕턴스. 피크가 **높아졌으면** 감쇠가 줄었다: ESR이 모델보다 낮거나(초저 ESR 부품으로 대체), 손실 성분이 빠짐. 이 레슨처럼 저주파 반공진과 고주파 L_eff를 같이 보면 "C도 줄고 병렬 경로도 줄었다" = 미실장이 가장 그럴듯하고, 개수 조합 적합으로 확인한다(SN0107: 10 µF 2/5).
꼬리질문: "대체 부품(alternate part)으로 바뀐 것과 미실장을 어떻게 구분하나?" → 미실장은 C와 1/L이 **같은 비율**로 줄고, 다른 값으로의 대체는 SRF 자체가 이동한다. 적합 잔차(rms)가 공차 수준으로 떨어지는지가 판단 기준이다. 결국 AOI/X-ray와 BOM·feeder 기록으로 닫는다.

</details>

<details>
<summary>Q7. VRM과 벌크 커패시터 사이 반공진은 VRM 루프 설계와 어떤 관계인가요?</summary>

VRM 폐루프 출력 임피던스는 루프 대역폭 아래에선 낮고, 그 위로는 출력 인덕터/루프 이득 감소로 인덕터처럼 상승한다 — 이 레슨의 R + jωL 은 그 1차 근사다. 그 등가 L이 벌크 C와 탱크를 이뤄 108 kHz 피크(13.49 mΩ)를 만든다. 실제 VRM 출력 Z는 위상 마진이 낮으면 크로스오버 근처에 자체 피크가 생기고(감쇠비 ζ ↓ → 피크 ↑), 이 피크가 벌크 반공진과 겹치면 더 나빠진다. 부하 스텝 링잉에서 ζ·위상 마진을 추정하는 법은 HWLAB-M02 참고.
꼬리질문: "출력 캡을 늘리면 VRM이 불안정해질 수 있나?" → 그렇다. 많은 벅 컨트롤러는 출력 LC 공진과 ESR 영점을 보상기 설계에 쓴다. 벌크를 크게 바꾸면 LC 극점이 내려가 크로스오버 근처 위상이 바뀐다 — 디커플링 변경 후 VRM 루프(보드 phase/gain 측정 또는 부하 스텝)를 다시 확인한다. 특히 ESR 영점에 기대는 보상(타입 II)은 초저 ESR MLCC로 바꾸면 위상 마진을 잃는다.

</details>

## 9. 추가 연습

### 연습 1 — 공차·온도·DC bias 코너

`derate_parts(parts)` 로 C에 공차 하한(0.90) × X5R 온도 하한(0.85) × DC bias 계수(0.9 V에서 100 µF 0.80, 10 µF 0.85, 1 µF 0.92 — 대표값)를 곱한 코너 라이브러리를 만들고, rev C가 아직 Ztarget을 지키는지 보라. 못 지키면 코너 라이브러리로 `min_bom` 을 다시 돌려라.

<details>
<summary>답 보기</summary>

실제 출력: `rev C nominal 13.49 mOhm @ 0.108 MHz | corner 21.50 mOhm @ 0.138 MHz` → FAIL. 벌크 C가 61 %로 줄면 VRM–벌크 공진이 위로(138 kHz) 가고 √(L/C)가 커져 피크가 오른다. 코너에서 다시 탐색: `100u×5 10u×1 1u×4 $1.07 10 parts, max 13.07 mOhm @ 0.098 MHz` — 비용이 거의 두 배. 이 BOM은 공칭 C에서도 통과한다(테스트로 확인 — 코너에서 통과했다고 공칭 통과가 자동으로 보장되는 건 아니니 꼭 같이 돌린다). 교훈: 최적화는 **코너 라이브러리로** 돌리고 공칭은 확인용으로. 그리고 VRM–벌크 피크가 비용을 지배하면, 벌크 개수를 늘리는 것보다 감쇠(연습 3)나 VRM 보상 조정이 싼 해법일 수 있다. DC bias 계수는 반드시 제조사 곡선(SimSurfing/K-SIM 등)으로 바꿔 넣을 것.

</details>

### 연습 2 — 1-port S11 vs 2-port shunt-thru 감도

10 mΩ DUT에서 S11과 S21을 계산하고, 둘 다 크기에 +0.01 dB 오차가 있을 때 `z_from_s11` 과 `s21_to_z` 가 돌려주는 Z를 비교하라.

<details>
<summary>답 보기</summary>

실제 출력: `S11=-0.999600 (-0.00347 dB)  S21=3.998e-04 (-67.96 dB)`, `+0.01 dB magnitude error -> 1-port Z = -18.78 mOhm, 2-port Z = 10.0115 mOhm`. 1-port는 |S11|이 1을 넘어 저항이 **음수**가 된다(288 % 오차). Z = Z0(1+S11)/(1−S11) 에서 1+S11 ≈ 4e−4 라는 작은 차이를 큰 두 수의 뺄셈으로 얻기 때문이다(감도 dZ/dS11 = 2Z0/(1−S11)² ≈ 25 Ω). 2-port는 S21 자체가 작은 수라 상대 오차가 그대로 0.115 %. 1-port 반사 측정이 잘 되는 범위는 Z가 Z0(50 Ω) 근처일 때다.

</details>

### 연습 3 — 벌크 ESR로 VRM–벌크 반공진 감쇠

rev C에서 100 µF 자리 부품의 ESR만 1–100 mΩ로 스윕하며 10 kHz–1 MHz 최악 |Z|를 구하라. 최적 ESR을 √(L_vrm/C_total)과 비교하라.

<details>
<summary>답 보기</summary>

실제 출력:

```text
      bulk ESR    1.0 mOhm/part -> worst |Z| (10k..1M)  16.77 mOhm @ 108 kHz
      bulk ESR    2.5 mOhm/part -> worst |Z| (10k..1M)  13.47 mOhm @ 108 kHz
      bulk ESR    6.3 mOhm/part -> worst |Z| (10k..1M)   9.59 mOhm @ 110 kHz
      bulk ESR   15.8 mOhm/part -> worst |Z| (10k..1M)   8.10 mOhm @ 155 kHz
      bulk ESR   39.8 mOhm/part -> worst |Z| (10k..1M)  15.18 mOhm @ 236 kHz
      bulk ESR  100.0 mOhm/part -> worst |Z| (10k..1M)  27.70 mOhm @ 242 kHz
      bulk ESR   12.6 mOhm/part -> worst |Z| (10k..1M)   7.90 mOhm @ 129 kHz
      sqrt(L_vrm/C_total) = 5.62 mOhm  (C_total = 253 uF);  best ESR/part / 2 parts = 6.29 mOhm
```

ESR이 너무 작으면 탱크 Q가 높아 피크가 크고(16.8 mΩ), 너무 크면 벌크가 "저항" 이 되어 VRM L을 못 받쳐 준다(27.7 mΩ, 피크가 위로 이동). 최적은 병렬 ESR ≈ 특성 임피던스 √(L/C) = 5.62 mΩ 근처(실제 6.29 mΩ, 비율 1.12) — RLC를 임계 감쇠 근처로 만드는 값이다. 피크가 13.5 → 7.9 mΩ로 내려가므로, 같은 Ztarget이면 더 적은 부품으로 갈 수 있다. 실무에선 폴리머 커패시터(ESR 수~수십 mΩ)나 controlled-ESR MLCC를 이 자리에 쓴다. 단, VRM 보상이 출력 캡 ESR에 의존하면 루프도 같이 바뀐다(Q7).

</details>

### 연습 4 — 봉투 뒷면 하한: 필요조건 vs 충분조건

저주파: VRM이 Ztarget을 넘는 주파수 f_x = Ztarget/(2πL_vrm)에서 커패시터가 받으려면 C ≥ 1/(2πf_x·Ztarget) = L_vrm/Ztarget². 고주파: f_hi에서 병렬 ESL이 Ztarget 아래 → Σn/ESL ≥ 2πf_hi/Ztarget. 두 하한을 계산하고 rev C가 만족하는지, 그리고 **두 하한을 모두 만족하는데도 실패하는 BOM**을 하나 보여라.

<details>
<summary>답 보기</summary>

실제 출력: `C_bulk >= L_vrm/Zt^2 = 43.9 uF (rev C has 253 uF);  sum(n/ESL) >= 2*pi*f_hi/Zt = 9.31 /nH (rev C 10.18 /nH)`. rev C는 둘 다 만족. 함정 BOM `100u×1 10u×0 1u×9`: C = 109 µF, Σn/ESL = 11.96 /nH — 두 하한 모두 통과인데 실제 최악 36.58 mΩ @ 1.528 MHz. 두 하한은 "각 끝단만" 본 것이고, 그 사이의 **반공진**은 개수·값 간격·ESR로만 정해진다. 면접에서 하한식은 "몇 개쯤?" 에 30초 안에 답하는 도구, 검증은 반드시 전 대역 계산(또는 측정)으로. 저주파 하한이 L/Zt² 인 것은 우연이 아니다: √(L/C) ≤ Zt, 즉 VRM–벌크 탱크의 특성 임피던스가 타겟 이하라는 조건과 같다. 그리고 Q가 1보다 크면 피크는 그보다 높으니 실제론 C가 더 필요하다(rev C는 하한의 5.8배).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 9/9 passed, exit=0  (실측 0.13 s)
```

## 10. 핵심 정리

- **Ztarget = V·ripple%/ΔI** (0.9 V·3 %/2 A = 13.5 mΩ). 가정: 선형, 평탄 타겟, ΔI = 실제 스텝, 리플은 동적 몫.
- 커패시터 = **ESR + j(ωL − 1/ωC)**, L = 부품 ESL + **실장 인덕턴스**. SRF 위에선 값이 아니라 루프 인덕턴스. 같은 부품 n개 = (nC, ESR/n, ESL/n).
- 병렬은 어드미턴스 합. **반공진** f = 1/(2π√((L1+L2)·C1C2/(C1+C2))), 높이 ≈ X²/(R1+R2) → **ESR이 낮을수록 피크가 높다**.
- 위험한 곳은 SRF가 아니라 **SRF 사이**. 그리드는 피크를 놓치고(10점/decade → 65 %), 밀도와 오차가 단조가 아니다 → 국소 최대 + 황금분할 정밀화.
- 이 레일의 최악점은 **VRM L ∥ 벌크 C** (108 kHz). 벌크 개수는 VRM 반공진이, 고주파 끝은 **Σn/ESL ≥ 2πf_hi/Ztarget** 가 정한다.
- 최소 BOM = 목적함수(비용/개수) + 전수 탐색 + 정밀화 재검증. 최적해는 경계에 붙으니 **공차·온도·DC bias 코너**로 설계한다(코너에서 rev C 21.5 mΩ).
- mΩ 측정은 **2-port shunt-thru**: Z = 25·S21/(1−S21). 1-port는 0.01 dB가 288 % 오차.
- 미실장의 지문: 저주파 반공진 ↑ + 고주파 L_eff ↑ 동시에. log|Z| 적합으로 개수(SN0107: 10 µF 2/5)를 찾고, 위치는 AOI/X-ray로.

## 참고자료

- L. D. Smith, R. E. Anderson, D. W. Forehand, T. J. Pelc, T. Roy, "Power distribution system design methodology and capacitor selection for modern CMOS technology," *IEEE Trans. Advanced Packaging*, 1999 — 타겟 임피던스 방법의 원전
- Larry D. Smith, Eric Bogatin, *Principles of Power Integrity for PDN Design — Simplified*, Prentice Hall, 2017 — 반공진, 실장 인덕턴스, 평탄 임피던스, rogue wave
- Eric Bogatin, *Signal and Power Integrity — Simplified*, 3rd ed. — PDN 장(커패시터 모델, 병렬 반공진, 비아 루프 인덕턴스)
- Istvan Novak, "Measuring milliohms and picohenrys in power distribution networks," DesignCon 2000 — 2-port shunt-thru 측정과 오차 요인
- Steven M. Sandler, *Power Integrity: Measuring, Optimizing, and Troubleshooting Power Related Parameters in Electronics Systems*, McGraw-Hill, 2014 — VRM 출력 임피던스, 측정 셋업, DC 블록/그라운드 루프
- Keysight / OMICRON Lab 응용 노트: 2-port shunt-through 저임피던스 측정, 그라운드 루프 오차와 공통모드 트랜스포머
- 제조사 커패시터 모델 도구: Murata SimSurfing, TDK SEAT, KEMET K-SIM — 주파수 의존 ESR/ESL, DC bias·온도 특성(이 레슨 대표값을 이것으로 교체)
- Touchstone File Format Specification (IBIS Open Forum) — .s2p 옵션 줄(`# Hz S DB R 50`)과 데이터 순서
- 관련 레슨: HWLAB-M02(부하 트랜지언트·ζ→위상 마진), HWLAB-B08(레일 리플·BW limit), HWLAB-B09(골든 보드·smoke test), HWLAB-B05(프로브·그라운드 루프), HWLAB-M01(전송선·반사 — 같은 S 파라미터 언어)
