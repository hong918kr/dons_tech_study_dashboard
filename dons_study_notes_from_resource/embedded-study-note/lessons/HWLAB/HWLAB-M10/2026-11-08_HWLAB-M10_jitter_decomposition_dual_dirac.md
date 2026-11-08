---
id: HWLAB-M10
time: 90분
updated: 2026-11-08
---
# HWLAB-M10 지터 분해: RJ/DJ(DDJ·PJ)와 Dual-Dirac TJ@BER

## 학습 목표

- 고속 직렬 캡처의 TIE(Time Interval Error)를 **DDJ(패턴 위치별 평균) → PJ(잔차 스펙트럼 피크 + 사인 최소제곱) → RJ(남은 RMS)** 순서로 직접 분해하고, 주입한 정답과 0.2 ps 안에서 맞춘다.
- TIE 히스토그램 꼬리를 **Q-scale에서 직선 피팅**해 RJδδ·DJδδ를 구하고 `TJ(1e-12) = DJδδ + 2·7.034·RJδδ` 를 계산한다. 7.034가 어디서 나오는지, BER을 에지당으로 볼지 비트당으로 볼지(전이 밀도 ρT)도 설명할 수 있다.
- 순진한 숫자가 왜 틀리는지 정답과 비교해 수치로 보인다: `std(TIE)` = RJ(4배 과대), 관측 pk-pk = TJ(27 % 과소), 직사각 창 FFT 피크 = PJ(36 % 과소), DDJ를 빼기 전 FFT(패턴 고조파를 PJ로 오인).
- **DJδδ ≠ DJpp, RJδδ ≠ RJrms** 인 이유(dual-Dirac은 분해가 아니라 꼬리 모델)와, 피팅 깊이에 따라 TJ가 얼마나 흔들리는지 안다.
- 레인 3개의 판정 리포트에서 "어떤 성분이 TJ를 깎는가"로 **무엇을 고칠지**(채널 이퀄라이제이션 vs 25 MHz 스퍼 추적)를 한 줄로 말한다.

## 1. 문제

"TJ가 0.39 UI라 스펙 0.28 UI를 넘습니다" 는 보고가 아니라 질문이다 — 고칠 대상이 채널(ISI)인지, 전원 스퍼(PJ)인지, 레퍼런스 클럭(RJ)인지 말하려면 지터를 **성분으로 쪼개야** 한다. Rivian/NVIDIA/Apple의 SerDes·보드 검증 면접에서 "RJ/DJ 분리를 스코프가 어떻게 하나, dual-Dirac은 왜 쓰나, 그 숫자를 믿을 수 있나" 가 단골인 이유다.

TIE(Time Interval Error)는 각 에지가 **이상 클럭(ideal clock)** 보다 얼마나 이르거나 늦은지다 (주기·c2c·TIE 정의는 HWLAB-M07 참고). 지터는 원인별로 나무처럼 나뉜다:

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">지터 나무 (jitter tree) — 성분마다 원인도, 분리 방법도, BER에 따른 크기 변화도 다르다</text>
  <rect x="290" y="30" width="180" height="36" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="380" y="53" text-anchor="middle" fill="currentColor" font-weight="bold">TJ @ BER (총 지터)</text>
  <g stroke="currentColor"><line x1="380" y1="66" x2="170" y2="100"/><line x1="380" y1="66" x2="560" y2="100"/></g>
  <rect x="40" y="100" width="260" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="170" y="120" text-anchor="middle" fill="currentColor" font-weight="bold">RJ — 랜덤 (무한·가우스)</text>
  <text x="170" y="140" text-anchor="middle" fill="currentColor" font-size="12">열·샷 잡음, PLL 위상잡음 → σ (rms)</text>
  <rect x="420" y="100" width="280" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="560" y="120" text-anchor="middle" fill="currentColor" font-weight="bold">DJ — 결정적 (유계, pk-pk)</text>
  <text x="560" y="140" text-anchor="middle" fill="currentColor" font-size="12">패턴·주기 신호·간섭이 원인</text>
  <g stroke="currentColor"><line x1="560" y1="150" x2="420" y2="185"/><line x1="560" y1="150" x2="570" y2="185"/><line x1="560" y1="150" x2="695" y2="185"/></g>
  <rect x="340" y="185" width="160" height="44" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="420" y="203" text-anchor="middle" fill="currentColor">DDJ (데이터 의존)</text>
  <text x="420" y="221" text-anchor="middle" fill="currentColor" font-size="12">패턴 위치별 평균</text>
  <rect x="515" y="185" width="110" height="44" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="570" y="203" text-anchor="middle" fill="currentColor">PJ (주기)</text>
  <text x="570" y="221" text-anchor="middle" fill="currentColor" font-size="12">TIE FFT 피크</text>
  <rect x="640" y="185" width="112" height="44" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="696" y="203" text-anchor="middle" fill="currentColor">BUJ (유계 비상관)</text>
  <text x="696" y="221" text-anchor="middle" fill="currentColor" font-size="12">크로스토크 등</text>
  <g stroke="currentColor"><line x1="420" y1="229" x2="367" y2="252"/><line x1="420" y1="229" x2="472" y2="252"/></g>
  <rect x="320" y="252" width="95" height="36" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="367" y="275" text-anchor="middle" fill="currentColor" font-size="12">ISI (채널 손실)</text>
  <rect x="425" y="252" width="95" height="36" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="472" y="275" text-anchor="middle" fill="currentColor" font-size="12">DCD (임계·듀티)</text>
  <g fill="currentColor" font-size="12">
    <text x="40" y="180">이 레슨의 RJ: DDJ·PJ 를 뺀 잔차 RMS (스펙트럼 방법)</text>
    <text x="40" y="200">또는 히스토그램 꼬리 피팅의 RJδδ</text>
    <text x="40" y="240">TJ(BER) = DJδδ + 2·Q(BER)·RJδδ</text>
    <text x="40" y="260">Q(1e-12) = 7.034 → RJ 1 ps 가 TJ 14.07 ps</text>
    <text x="560" y="250">DJ 는 BER 을 낮춰도 안 커지고</text>
    <text x="560" y="268">RJ 항은 Q(BER) 에 비례해 커진다</text>
  </g>
</svg>
```

> **캡처에 대하여.** 이 레슨은 HWLAB-M09(eye diagram)의 캡처를 입력으로 쓰도록 설계됐지만, M09가 지금 병렬로 작성 중이라 **M09식 캡처를 `gen_capture.py` 에 축약해 직접 만든다**: PRBS7 NRZ + 채널 ISI + 진폭 잡음 + 주입 RJ/PJ. 차이 두 가지 — ① 채널은 M09의 FIR 대신 **1극 RC** 다(비트 경계 전압이 정확히 재귀되어 "정답 DDJ"를 해석적으로 얻는다. 선형 채널의 ISI라는 점은 같다). ② 이상 클럭은 golden PLL 대신 **직선 피팅(상수 주파수)** 이다 — golden PLL과의 차이는 연습 2에서 잰다.

| 항목 | Part A 캡처 (DEMO) |
|---|---|
| 비트율 / UI | 10 Gb/s / 100 ps |
| 패턴 | PRBS7 (x⁷+x⁶+1, 127 비트, 전이 64개/주기) × 130 반복 = 16510 비트 |
| 샘플링 | 16 샘플/UI (6.25 ps, 160 GS/s 등가) → 264160 샘플, 메모리에서 생성 |
| 신호 / 채널 | NRZ ±0.4 V, RC 채널 τ = 0.55 UI (정답 DDJ pk-pk 9.74 ps) |
| 주입 지터 | RJ σ = 1.5 ps, PJ 6.0 ps(진폭) @ 50.97 MHz, 진폭 잡음 1 mV rms |

### Part A — TIE → DDJ → PJ → RJ (스펙트럼 분해)

- `tie_from_edges(edges, ui_nom)` — 에지마다 UI 번호 `n = round((t − t0)/UI)` 를 붙이고, `t = a + T·n` 을 최소제곱으로 맞춰 `TIE = t − (a + T·n)`.
- `ddj_by_pattern(n, tie)` — 패턴 위치 `p = n mod 127` 별 평균 = DDJ[p] (센터링), 잔차 `resid = TIE − DDJ[p]`, DDJ pk-pk.
- `find_pj(n, resid, ui)` — 에지 UI에서만 정의된 잔차를 UI마다 채운 균일 수열(선형 보간) → Hann 창 FFT 피크 → 피크 ±1 빈에서 사인 LS 진폭이 최대인 주파수 → 진폭·위상. 비교용으로 직사각 창 피크 진폭도 돌려준다.
- `rj_rms(...)` — PJ 사인을 뺀 나머지의 RMS × √(R/(R−1)).
- 검증: DDJ 위치별 rms 오차 < 0.30 ps, DDJ pk-pk ±0.6 ps, PJ ±0.05 MHz·±0.2 ps, RJ ±0.08 ps.

### Part B — 꼬리 피팅·dual-Dirac, 그리고 순진한 숫자의 함정

- `inv_phi(p)` (Φ⁻¹, 이분법), `tail_fit(tie, p_lo, p_hi)` (Q-scale 직선 `x = μ + σ·Q`), `dual_dirac(fit, ber)`.
- 정답 TJ = 주입 성분(DDJ ⊗ PJ ⊗ RJ)의 컨볼루션에서 꼬리 확률을 풀어 구한다(`gen_capture.tj_convolution`).
- 비교: ① `std(TIE)` vs RJ, ② 관측 pk-pk vs TJ, ③ 직사각 FFT 피크 vs PJ, ④ 캡처(8320 에지)와 긴 기록(256000 에지)의 dual-Dirac TJ, ⑤ DJpp + 2Q·RJ(성분 합), ⑥ 분해 성분 컨볼루션 TJ, ⑦ DDJ 큰 레인에서 DDJ 제거 **전에** FFT를 하면 무엇을 PJ로 잡는가.

### Part C — VALBRD-B rev A: SERDES TX 3레인 판정

가상의 보드 **VALBRD-B rev A**. SoC SERDES TX 3레인을 J5(SMA 브레이크아웃)에서 PRBS7 @ 10 Gb/s로 캡처했다. 심어 둔 결함 두 개: **TX1** 은 경로가 길어 채널 손실이 크고(τ = 0.8 UI), **TX2** 는 25 MHz 소스가 SERDES PLL 전원에 커플링되어 PJ 9 ps @ 25 MHz가 실렸다. 사내 TX 지터 예산(`valbrd_b_serdes.json`: TJ(1e-12) ≤ 0.28 UI, DJδδ ≤ 0.15 UI, RJδδ ≤ 2.0 ps — **대표값, 실제 표준/데이터시트 확인 필요**)으로 판정하고, `judge_lane()` 이 지배 성분과 조치를 한 줄로 낸다.

## 2. Problem (English)

> **Decompose the jitter of a serial-data capture and report TJ at BER = 1e-12.**
>
> You get a sampled NRZ waveform (PRBS7, 10 Gb/s, 16 samples/UI) as a Python list. Using only the standard library:
>
> 1. Extract 0 V crossings (given) and compute TIE against a least-squares constant-frequency ideal clock:
>    `tie_from_edges(edges, ui_nom) -> (n, tie, T)`.
> 2. `ddj_by_pattern(n, tie)`: DDJ[p] = mean TIE at pattern position `p = n mod 127` (centered), residual = TIE − DDJ[p].
> 3. `find_pj(n, resid, ui)`: locate the periodic-jitter tone in the residual spectrum (fill the non-transition UIs by
>    linear interpolation, Hann window, radix-2 FFT), refine the frequency by maximizing a least-squares sine fit within
>    ±1 bin, and return its frequency and amplitude.
> 4. `rj_rms(...)`: RMS of what is left after removing DDJ and PJ.
> 5. `tail_fit(tie, p_lo, p_hi)` + `dual_dirac(fit, ber)`: fit each histogram tail on a Q-scale, `x = μ + σ·Q` with
>    `Q = Φ⁻¹(2F)` (left) / `−Φ⁻¹(2F)` on the mirrored order statistics (right); return
>    `RJδδ = (σL+σR)/2`, `DJδδ = μR − μL`, `TJ = DJδδ + 2·Q(BER)·RJδδ`, `Q(1e-12) ≈ 7.034`.
> 6. Judge three lanes against a jitter budget and name the dominant component (DDJ / PJ / RJ) with a fix.
>
> Example: injected RJ = 1.5 ps, PJ = 6 ps @ 50.97 MHz on a channel with 9.74 ps DDJ must decompose to within
> 0.08 ps / 0.2 ps / 0.6 ps, and the dual-Dirac TJ(1e-12) must land within −3 %…+12 % of the exact 41.23 ps.

## 3. 시작 코드

<!-- code: starter.py -->

캡처 생성기(결정적) — PRBS7, RC 채널, time-warp 지터 주입, 정답 DDJ, 성분 컨볼루션 TJ:

<!-- code: gen_capture.py -->

```bash
cd lessons/HWLAB/HWLAB-M10
python3 -I gen_capture.py                 # valbrd_b_serdes.json(스펙+레인 설정), capture_preview_tx2.csv(앞 64 UI) 재생성
python3 -I starter.py ; echo "exit=$?"     # TODO 상태: 2/22 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: capture -> TIE -> DDJ (pattern avg) -> PJ (FFT + LS) -> RJ (residual rms) --
      capture DEMO: 16510 bits x 16 samples/UI = 264160 samples, UI=100ps, PRBS7 transitions/pattern=64
PASS  find_edges: one crossing per data transition (8320 = 64/127 x 16510 bits)   [edges=8320]
      (setup failed: NotImplementedError: TODO: tie_from_edges)
FAIL  TIE: fitted UI within 0.001ps of 100ps, mean TIE ~ 0: KeyError: 'T'
...
FAIL  inv_phi: Q(1e-12) = -inv_phi(1e-12) = 7.0345 +-0.0005, inv_phi(0.5) = 0: NotImplementedError: TODO: inv_phi
      truth (injected components, convolution): TJ(1e-12)=41.23ps  DJpp=21.74ps (DDJ 9.74 + PJ 2x6.0)  RJ=1.50ps
...
PASS  component sum DJpp + 2Q*RJ over-estimates TJ (conservative, 0..+8%)   [sum=42.84ps vs 41.23ps (+3.9%)]
...
FAIL  TX2 -> FAIL, dominant PJ at 25.00 +-0.05MHz: KeyError: 'pj'
2/22 passed  (2.0s)
exit=1
```

> starter에서도 통과하는 두 개는 "주어진 것" 만 쓰는 테스트다: 에지 추출(HWLAB-B04의 보간 교차와 같은 코드)과, 정답 성분만으로 계산하는 "DJpp + 2Q·RJ는 보수적" 관찰.

## 4. 힌트

<details>
<summary>힌트 1 — UI 번호와 이상 클럭</summary>

지터(수십 ps)가 UI/2(50 ps)보다 작으므로 `round((t − t0)/UI_nom)` 이 각 에지가 속한 비트 경계를 맞힌다. 그다음 `t = a + T·n` 직선 최소제곱: `T = (NΣnt − ΣnΣt)/(NΣn² − (Σn)²)`, `a = (Σt − TΣn)/N`. 잔차의 평균은 자동으로 0이다. 공칭 UI가 아니라 **맞춘 T** 를 쓰는 이유: 송신 클럭의 ppm 오프셋이 TIE에 램프로 쌓이면 그 자체가 거대한 "저주파 지터" 가 된다.

</details>

<details>
<summary>힌트 2 — 패턴 평균이 DDJ인 이유</summary>

PRBS7은 127비트마다 똑같이 반복되므로, 위치 p의 에지는 매번 **같은 비트 이력** 을 갖는다 → ISI로 인한 지연이 매번 같다. RJ는 평균 0, PJ는 패턴 주기와 무관한 주파수라 130번 평균하면 거의 지워진다. `p = n mod 127` 은 캡처 시작이 패턴의 어디든 상관없다 — 위치의 이름이 돌아갈 뿐 같은 위치끼리 묶인다.

</details>

<details>
<summary>힌트 3 — 에지가 없는 UI와 FFT</summary>

TIE는 전이가 있는 UI에만 있다(PRBS7은 64/127). FFT는 균일 간격이 필요하니 사이 UI를 선형 보간으로 채운다(스코프의 TIE 스펙트럼도 이렇게 한다). 피크 빈 k만 찾고, 진폭은 FFT 빈 크기로 읽지 말고 **실제 에지 위치에서 사인 최소제곱** `r ≈ a·cos(2πfn) + b·sin(2πfn)` 으로 구한다. 주파수는 `(k−1)/N … (k+1)/N` 에서 LS 진폭이 최대가 되는 곳(황금분할, `golden_max`).

</details>

<details>
<summary>힌트 4 — Q-scale 꼬리 좌표</summary>

정렬값 `s[i]`, 경험 CDF `F = (i+0.5)/N`. dual-Dirac은 두 델타에 1/2씩 무게가 있으므로 왼쪽 꼬리는 `F ≈ ½·Φ((x−μL)/σ)` → `Q = Φ⁻¹(2F)` 로 바꾸면 `x = μL + σ·Q` 직선. 오른쪽은 대칭: `s[N−1−i]` 와 `Q = −Φ⁻¹(2F)`. 피팅 점은 확률을 로그 간격으로 뽑아야 얕은 꼬리 점이 피팅을 독점하지 않는다.

</details>

<details>
<summary>힌트 5 — Q(1e-12)와 RJ 보정</summary>

`Q = −Φ⁻¹(BER)`. `Φ⁻¹(1 − 1e-12)` 로 쓰면 `1 − 1e-12` 가 double에서 꼬리 확률 유효숫자 4자리만 남는다. RJ는 위치별 평균에 그 에지 자신이 포함되어 잔차 분산이 `(1 − 1/R)` 배(R = 130)로 줄어든 것을 `√(R/(R−1))` 로 되돌린다.

</details>

## 5. 풀이

### Part A — 분해 파이프라인

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm10-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">TIE 분해 순서 (스펙트럼 방법) + 꼬리 피팅 (dual-Dirac)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="120" height="54" rx="8" class="f-mut"/>
    <rect x="160" y="40" width="120" height="54" rx="8" class="f-acc"/>
    <rect x="310" y="40" width="135" height="54" rx="8" class="f-ok"/>
    <rect x="475" y="40" width="140" height="54" rx="8" class="f-ok"/>
    <rect x="645" y="40" width="105" height="54" rx="8" class="f-ok"/>
    <rect x="160" y="150" width="200" height="54" rx="8" class="f-warn"/>
    <rect x="390" y="150" width="180" height="54" rx="8" class="f-warn"/>
    <rect x="600" y="150" width="150" height="54" rx="8" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="70" y="62">캡처 v[i]</text><text x="70" y="82" font-size="12">0 V 교차 보간</text>
    <text x="220" y="62">TIE[k]</text><text x="220" y="82" font-size="12">t − (a + T·n)</text>
    <text x="377" y="62">DDJ[p]</text><text x="377" y="82" font-size="12">p = n mod 127 평균</text>
    <text x="545" y="62">PJ (f, A)</text><text x="545" y="82" font-size="12">Hann FFT → LS 사인</text>
    <text x="697" y="62">RJ</text><text x="697" y="82" font-size="12">남은 RMS</text>
    <text x="260" y="172">정렬 → Q-scale 꼬리</text><text x="260" y="192" font-size="12">x = μ + σ·Q (양쪽)</text>
    <text x="480" y="172">RJδδ, DJδδ</text><text x="480" y="192" font-size="12">TJ = DJδδ + 2·7.034·RJδδ</text>
    <text x="675" y="172">검증</text><text x="675" y="192" font-size="12">성분 컨볼루션 TJ</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwm10-arr)">
    <path d="M130 67 L158 67"/><path d="M280 67 L308 67"/><path d="M445 67 L473 67"/><path d="M615 67 L643 67"/>
    <path d="M220 94 L220 148"/><path d="M360 177 L388 177"/><path d="M570 177 L598 177"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="290" y="122">잔차 = TIE − DDJ[p]</text><text x="470" y="122">잔차 − PJ 사인</text>
    <text x="10" y="232">순서가 중요: DDJ 를 먼저 빼지 않으면 패턴 고조파(k × 78.74 MHz)가 PJ 로 잡힌다 (Part B ⑦)</text>
  </g>
</svg>
```

**지터 주입 방식 — 왜 time-warp인가.** 지터를 송신기 에지 시각에 넣고 채널(RC)을 통과시키면, 이웃 에지의 지터가 ISI를 통해 현재 교차 시각에 섞인다 — 채널이 지터를 걸러내거나 증폭한다. 현실적이지만 "주입한 RJ = 1.5 ps" 가 더 이상 측정 대상의 정답이 아니게 된다. 그래서 `gen_capture.py` 는 깨끗한 파형 `v_clean(t)` 를 만든 뒤 관측 시간축을 비튼다: `v_obs(t) = v_clean(t − J(t))`. J(t)는 각 비트 경계의 교차 시각 ±0.4 UI에서 정확히 `j[n] = PJ(n) + RJ(n)` 으로 평평하고 그 사이 0.2 UI 구간에서만 다음 값으로 넘어간다. 교차는 항상 평평한 구간 안에서 일어나므로 **에지 n의 정답 TIE = DDJ[p] + PJ(n) + RJ(n)** 이 정확히 성립한다.

**TIE.** 결과 `T = 99.999998 ps` (공칭 100 ps 대비 2×10⁻⁶ ps — 비정수 주기의 PJ와 RJ가 직선 피팅에 아주 조금 섞인 몫). 이상 클럭 직선 피팅은 "무한 대역 CDR" 이 아니라 **"추적 대역폭 0인 CDR"** 이다. 스펙 측정은 보통 golden PLL(1차, BW = baud/1667 ≈ 6 MHz, HWLAB-M09)로 저주파 지터를 지운 TIE를 쓴다 — 우리 PJ는 25~51 MHz라 거의 다 남는다(연습 2: 25 MHz에서 97 %).

**DDJ = 패턴 위치별 평균.** RC 채널에서 교차 시각은 직전 런(run) 길이에 따라 정해진다. 짧은 런 뒤에는 신호가 아직 끝까지 안 갔으니 임계에서 가까운 곳에서 출발 → **이르게** 교차한다. 정답 DDJ (DEMO, τ = 0.55 UI):

| 직전 런 길이 | 위치 수 (PRBS7) | 정답 DDJ (ps) |
|---|---|---|
| 1 | 32 | −4.93 … −3.23 |
| 2 | 16 | +3.34 … +3.58 |
| 3 | 8 | +4.57 … +4.61 |
| 4 | 4 | +4.77 … +4.78 |
| 5–7 | 4 | +4.80 … +4.81 |

DDJ 분포는 연속이 아니라 **이산 값 몇 무더기** 다(런 1 대 런 ≥ 2의 두 무더기가 9.7 ps 떨어짐). 같은 표를 TX1(τ = 0.8 UI)로 만들면 −13.99 … +12.99 ps (pk-pk 27.0 ps) — 채널 손실이 크면 DDJ가 3배.

추정 오차는 위치별 평균의 잡음이다: 위치당 130개 평균이면 RJ 기여 σ/√130 = 0.13 ps, 실측 위치별 rms 오차 **0.141 ps**. pk-pk는 64개 노이즈 평균의 최대−최소라 위로 편향된다: 추정 **10.146 ps vs 정답 9.741 ps (+0.4 ps)**. 반복 수를 늘리면 줄어든다.

**PJ = 잔차 스펙트럼.** 세 단계가 다 이유가 있다:

| 단계 | 하는 일 | 안 하면 |
|---|---|---|
| 에지 없는 UI 보간 | 64/127 위치만 있는 TIE를 균일 수열로 | FFT 불가. 0으로 채우면 패턴 주기로 변조된 가짜 측대파가 생김 |
| Hann 창 | 누설(leakage)을 낮춰 피크 빈을 확실히 | 직사각 창은 강한 톤의 측엽이 수십 빈까지 퍼짐 |
| ±1 빈 LS 사인 | 빈 사이 실제 주파수·진폭을 실제 에지 위치에서 | 빈 크기로 진폭을 읽으면 scalloping 손실 (최악 0.5 빈에서 sinc(0.5) = 0.64) |

FFT 빈 = 10 GHz / 16384 = 0.610 MHz. 50.97 MHz는 83.51 빈 — 일부러 빈 사이 정중앙에 둔 값이다. 결과: **50.970 MHz, 5.987 ps (정답 6.000), SNR 311**. LS 진폭 추정의 이론 표준편차는 σ_RJ·√(2/N_edge) = 1.5·√(2/8320) ≈ 0.02 ps이므로 0.013 ps 오차는 그 범위다.

**RJ = 남은 RMS.** `1.5079 ps` (정답 1.5). 0.008 ps 초과분은 진폭 잡음 1 mV가 기울기 7.27 mV/ps 에서 만드는 √(2/3)·1/7.27 = 0.112 ps를 제곱합한 1.5042 ps와 같은 크기다(연습 1). 측정 시스템의 잡음은 **RJ로 보인다** — 스코프 지터 바닥이 RJ 측정의 하한이다.

### Part B — dual-Dirac은 "꼬리 모델" 이다

dual-Dirac 모델은 DJ를 두 개의 델타(±DJδδ/2, 무게 1/2씩)로, RJ를 그 각각에 씌운 가우스(σ = RJδδ)로 본다. 실제 DJ 분포(이산 DDJ 무더기 ⊗ 사인 PJ의 아크사인 분포)와 모양은 전혀 다르지만, **아주 깊은 꼬리(BER 1e-12 근처)에서는 맞는다** — 거기선 DJ의 최극단 근처만 의미가 있고, 그 위에 가우스가 얹힌 모양이 되기 때문이다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">TIE 히스토그램(실제) vs dual-Dirac 모델 — DJδδ 는 DJpp 보다 작다</text>
  <line x1="40" y1="200" x2="730" y2="200" stroke="currentColor"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="80,199 120,196 150,185 170,150 185,95 200,70 215,88 235,120 265,128 300,130 340,126 380,122 420,124 455,128 490,126 520,112 545,85 560,62 575,80 595,140 615,182 640,196 680,199"/>
  <g stroke="currentColor" stroke-dasharray="5 4" fill="none">
    <path d="M150,200 C185,200 195,66 230,66 C265,66 275,200 310,200"/>
    <path d="M465,200 C500,200 510,66 545,66 C580,66 590,200 625,200"/>
  </g>
  <g class="f-acc" stroke="currentColor"><rect x="228" y="66" width="4" height="134"/><rect x="543" y="66" width="4" height="134"/></g>
  <g stroke="currentColor"><line x1="230" y1="222" x2="545" y2="222"/><line x1="170" y1="244" x2="595" y2="244"/>
    <line x1="230" y1="216" x2="230" y2="228"/><line x1="545" y1="216" x2="545" y2="228"/><line x1="170" y1="238" x2="170" y2="250"/><line x1="595" y1="238" x2="595" y2="250"/></g>
  <g fill="currentColor" font-size="12">
    <text x="300" y="218">DJδδ = μR − μL (모델 델타 간격, 꼬리 피팅으로)</text>
    <text x="250" y="262">DJpp = DDJpp + 2·A_PJ (실제 결정적 지터의 끝–끝)</text>
    <text x="40" y="36">실선: 실제 TIE 분포 (DDJ 무더기 ⊗ 아크사인 PJ ⊗ 가우스 RJ)</text>
    <text x="40" y="52">점선: dual-Dirac 의 두 가우스 (σ = RJδδ &gt; 실제 RJ)</text>
    <text x="610" y="150">꼬리 모양만</text><text x="610" y="166">맞추는 모델</text>
  </g>
</svg>
```

**Q-scale.** 왼쪽 꼬리에서 `F(x) ≈ ½·Φ((x − μL)/σ)` 이면 `Q(x) = Φ⁻¹(2F)` 는 x의 1차 함수다. 꼬리를 직선으로 맞추면 기울기가 σ, Q = 0 절편이 μ. 오른쪽도 대칭. 직선을 Q(BER)까지 연장한 두 점 사이가 TJ다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">Q-scale 꼬리 피팅과 TJ(1e-12) 외삽</text>
  <line x1="60" y1="40" x2="60" y2="270" stroke="currentColor"/><line x1="60" y1="270" x2="740" y2="270" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="16" y="56">Q=0</text><text x="16" y="136">Q=3</text><text x="10" y="248">Q=7.03</text>
    <text x="680" y="288">TIE (ps)</text></g>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".5"><line x1="60" y1="52" x2="740" y2="52"/><line x1="60" y1="132" x2="740" y2="132"/><line x1="60" y1="244" x2="740" y2="244"/></g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="300,52 285,70 268,88 250,104 236,118 225,132"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="500,52 515,70 532,88 550,104 564,118 575,132"/>
  <g class="f-ok" stroke="currentColor"><circle cx="250" cy="104" r="4"/><circle cx="236" cy="118" r="4"/><circle cx="225" cy="132" r="4"/>
    <circle cx="550" cy="104" r="4"/><circle cx="564" cy="118" r="4"/><circle cx="575" cy="132" r="4"/></g>
  <g stroke="currentColor" stroke-width="1.5" stroke-dasharray="7 4"><line x1="290" y1="52" x2="150" y2="244"/><line x1="510" y1="52" x2="650" y2="244"/></g>
  <g class="f-warn" stroke="currentColor"><circle cx="150" cy="244" r="5"/><circle cx="650" cy="244" r="5"/></g>
  <line x1="150" y1="258" x2="650" y2="258" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="320" y="288">TJ(1e-12) = xR − xL (위 실선 구간)</text>
    <text x="200" y="46">μL (Q=0 절편)</text><text x="505" y="46">μR</text>
    <text x="320" y="110">피팅 창: 2F ∈ [p_lo, p_hi]</text><text x="320" y="128">(초록 점, 로그 간격)</text>
    <text x="320" y="170">기울기 = σ = RJδδ</text>
    <text x="320" y="190">얕은 곳은 DJ 모양 때문에 휘어 있다</text>
    <text x="320" y="210">→ 피팅 깊이가 바뀌면 σ·μ 가 바뀐다</text>
  </g>
</svg>
```

**7.034의 약속 (convention).** 이 레슨은 "에지 하나가 [xL, xR] 밖으로 나갈 확률 = BER" 로 정의한다(양쪽 꼬리 각각 BER/2). dual-Dirac에선 오른쪽 꼬리 확률이 `½·Φ(−Q)` 이므로 `Q(BER) = Φ⁻¹(1 − BER) = √2·erfc⁻¹(2·BER)`, Q(1e-12) = **7.0345** → 흔히 보는 "RJ × 14.069". BER을 **비트당** 으로 두면 전이가 있는 비트만 오류를 낼 수 있으므로 에지당 확률이 BER/ρT로 커지고, PRBS(ρT ≈ 0.5)에선 Q(1e-12) = 6.937 (×13.87)이 된다(연습 3). 1.4 % 차이지만 TJ 예산이 빠듯하면 판정이 갈린다 — 스펙이 어느 약속을 쓰는지 확인한다(bathtub 쪽 약속은 HWLAB-M11).

**정답 TJ.** 주입 성분이 다 알려져 있으니 TJ는 근사 없이 풀 수 있다: `P(TIE > x) = mean_p mean_θ Φ̄((x − DDJ[p] − A·sinθ)/σ)` 를 BER/2와 같게 하는 x를 이분법으로 (θ는 256점 중점 규칙 — 주기 함수라 오차가 지수적으로 작다). DEMO: **TJ(1e-12) = 41.23 ps**, DJpp = 9.74 + 2×6.0 = 21.74 ps.

**순진한 숫자들 vs 정답** (`python3 -I solution.py` Part B 실제 출력에서):

| 방법 | 값 | 정답 | 오차 | 왜 |
|---|---|---|---|---|
| RJ = std(TIE) | 6.13 ps | 1.50 ps | ×4.1 | DJ(유계)를 가우스 잡음으로 취급 |
| TJ = 관측 pk-pk (8320 에지) | 30.05 ps | 41.23 ps | −27 % | 8320개로는 1e-4 깊이까지만 봄. 기록이 길수록 커짐 |
| PJ = 직사각 FFT 피크 | 3.86 ps | 6.00 ps | −36 % | 83.51 빈: scalloping |
| DJpp + 2·Q·RJ (성분 합) | 42.84 ps | 41.23 ps | +3.9 % | DJ 극단과 RJ 극단이 동시에 올 확률을 과대 — 보수적 |
| dual-Dirac, 캡처 8320 에지 | 43.92 ps | 41.23 ps | +6.5 % | 얕은 피팅(2F ≥ 5e-4)에서 RJδδ = 2.01 ps로 과대 |
| dual-Dirac, 긴 기록 256000 에지 | 42.06 ps | 41.23 ps | +2.0 % | 깊은 피팅(2F 1e-5…1e-3): RJδδ 1.76, DJδδ 17.35 |
| 분해 성분 컨볼루션 | 41.47 ps | 41.23 ps | +0.57 % | 성분을 정확히 알면 모델 근사가 필요 없다 |

**피팅 깊이 vs 기록 길이** — `python3 -I demo_tailfit.py` 실제 출력:

```text
truth: TJ(1e-12)=41.23ps  DJpp=21.74ps  RJ=1.50ps   (DJpp+2Q*RJ=42.84ps)
   edges  fit 2F window      RJdd    DJdd     TJdd     err | DJ(sfix)      err |   pk-pk
    8320  [5e-04,2e-02]   1.972   15.99    43.72   +6.0% |    18.39    -4.2% |   29.96
   32000  [5e-04,2e-02]   1.992   15.69    43.71   +6.0% |    18.32    -4.4% |   31.60
  128000  [5e-04,2e-02]   2.033   15.58    44.18   +7.2% |    18.47    -4.0% |   31.96
  128000  [1e-04,1e-02]   1.966   15.95    43.61   +5.8% |    18.75    -3.3% |   31.96
  512000  [5e-04,2e-02]   1.991   15.76    43.77   +6.2% |    18.42    -4.1% |   32.67
  512000  [1e-04,1e-02]   1.927   16.12    43.24   +4.9% |    18.75    -3.4% |   32.67
  512000  [1e-05,1e-03]   1.714   17.58    41.70   +1.1% |    19.12    -2.4% |   32.67
(1.4s)
```

읽는 법:

1. **같은 창이면 기록을 늘려도 편향은 그대로다** (+6 % 근처). 늘어나는 건 정밀도뿐. 편향을 줄이는 건 **더 깊은 창** 이고, 깊은 창을 쓰려면 긴 기록이 필요하다 — 이게 "지터 측정에 왜 수백만 UI가 필요한가" 의 답이다.
2. 깊어질수록 RJδδ ↓ (1.97 → 1.71), DJδδ ↑ (16.0 → 17.6). 둘 다 정답 RJ 1.5 / DJpp 21.74 쪽으로 가지만 1e-5 깊이에서도 아직 멀다. 그래도 TJ는 +1.1 %까지 맞는다: **RJδδ·DJδδ는 개별로는 틀려도 그 조합(TJ)이 맞도록 만들어진 모델** 이다.
3. σ고정 열: RJ를 스펙트럼 RJ(정답 1.5 ps)로 고정하고 μ만 맞추면 TJ가 **−2…−4 % 낙관적** 이다. 얕은 깊이의 DJδδ가 1e-12에서 실제로 작용하는 DJ보다 작기 때문이다. 일부 도구가 이런 혼합 방법을 쓰는데, 결과가 스펙 경계에 걸리면 이 방향의 편향을 기억해 둔다.
4. pk-pk는 기록 길이에 따라 계속 커진다(29.96 → 32.67 ps). **pk-pk는 측정 조건(에지 수)을 같이 적지 않으면 의미 없는 숫자** 다.

**순서가 중요하다 — TX1.** 채널 손실이 큰 TX1(DDJ 27 ps)의 TIE에 DDJ를 빼지 않고 바로 `find_pj` 를 돌리면 **787.405 MHz = 10 × 78.740 MHz** 를 PJ로 잡는다. 78.74 MHz = 10 Gb/s / 127, 즉 패턴 반복 주파수다. DDJ는 패턴 주기로 반복되는 "주기 신호" 라 스펙트럼에서 패턴 주파수의 고조파 선들로 나타난다. 그래서 스코프 지터 패키지는 **패턴 길이를 알려 주면** DDJ를 먼저 빼고 PJ를 찾으며, 남은 스펙트럼에서 패턴 고조파 위치의 선은 PJ에서 제외한다. DDJ를 뺀 뒤에는 37.298 MHz(정답 37.3)를 정확히 잡는다.

### Part C — 3레인 판정

`python3 -I solution.py` 의 Part C 실제 출력:

```text
-- Part C: VALBRD-B rev A, SoC SERDES TX -> J5 (SMA breakout), PRBS7 @ 10 Gb/s --
      PASS lane=TX0 TJ(1e-12)=0.251UI(25.1ps) DJdd=8.2ps RJdd=1.20ps | DDJ=7.5ps PJ=2.0ps@37.29MHz RJ=1.02ps | dominant=RJ
        truth: DDJ=7.3ps PJ=2.0ps@37.30MHz RJ=1.00ps TJ=24.4ps | est err: DDJ +0.22 PJ -0.01 RJ +0.016 TJdd +3.1%
      FAIL lane=TX1 TJ(1e-12)=0.468UI(46.8ps) DJdd=25.3ps RJdd=1.53ps | DDJ=27.2ps PJ=2.0ps@37.30MHz RJ=1.00ps | dominant=DDJ -> ISI-dominated: check channel loss / stubs on this lane, tune TX FFE/de-emphasis or RX CTLE (HWLAB-M12) [TJ 46.8ps>28.0ps, DJdd 25.3ps>15.0ps]
        truth: DDJ=27.0ps PJ=2.0ps@37.30MHz RJ=1.00ps TJ=43.7ps | est err: DDJ +0.20 PJ -0.03 RJ +0.003 TJdd +7.1%
      FAIL lane=TX2 TJ(1e-12)=0.386UI(38.6ps) DJdd=21.4ps RJdd=1.22ps | DDJ=7.5ps PJ=9.0ps@25.00MHz RJ=1.01ps | dominant=PJ -> spur 25.00MHz: find the 25.00MHz aggressor (refclk/oscillator/switcher) coupling into SERDES PLL supply [TJ 38.6ps>28.0ps, DJdd 21.4ps>15.0ps]
        truth: DDJ=7.3ps PJ=9.0ps@25.00MHz RJ=1.00ps TJ=38.2ps | est err: DDJ +0.21 PJ -0.01 RJ +0.008 TJdd +1.0%
PASS  TX0 -> PASS
PASS  TX1 -> FAIL, dominant DDJ (long lossy lane)
PASS  TX2 -> FAIL, dominant PJ at 25.00 +-0.05MHz   [f=25.000MHz]
PASS  all lanes: decomposition error DDJpp<0.6ps, PJ<0.2ps, RJ<0.05ps vs injected   [worst |err| DDJpp=0.22 PJ=0.025 RJ=0.016 ps]
PASS  all lanes: dual-Dirac TJ within -3%..+12% of truth   [TX0 +3.1%, TX1 +7.1%, TX2 +1.0%]
22/22 passed  (3.3s)
```

**두 FAIL 레인은 TJ 숫자만 보면 비슷하지만 고칠 곳이 정반대다.**

| 레인 | TJ | 지배 성분 | 근거 | 조치 |
|---|---|---|---|---|
| TX0 | 0.251 UI | RJ 항 (2·7.03·1.02 = 14.4 ps) | DDJ 7.5, PJ 2.0 | 기준 레인. 마진 0.03 UI |
| TX1 | 0.468 UI | DDJ 27.2 ps | 다른 레인과 RJ·PJ 동일, DDJ만 3.6배 | 채널 손실(경로 길이·비아 스텁·커넥터) 확인, TX FFE/de-emphasis 또는 RX CTLE 튜닝 (HWLAB-M12) |
| TX2 | 0.386 UI | PJ 9.0 ps @ 25.00 MHz | DDJ는 TX0과 같음 | 25 MHz 어그레서 추적 (아래) |

- **TX2 → 하드웨어 엔지니어에게 넘길 문장:** `FAIL VALBRD-B TX2: TJ(1e-12) 0.386 UI > 0.28 UI. PJ 9.0 ps @ 25.000 MHz (TX0/TX1 2.0 ps @ 37.3 MHz) — 25 MHz 소스(이더넷 PHY refclk 25 MHz 오실레이터, 25 MHz로 도는 스위처 등)가 TX2 SERDES PLL 전원(VDDA_PLL) 또는 refclk 경로로 커플링 의심. 해당 소스 on/off 또는 주파수 변경 시 스퍼가 따라 움직이는지 확인, VDDA_PLL 리플을 20 MHz BW limit 없이 FFT (HWLAB-B08).` 스퍼 주파수가 **정확히** 25.000 MHz라는 것 자체가 단서다 — 주파수가 맞는 후보만 남는다.
- **TX1:** DDJ가 지배하면 "지터를 줄이는" 게 아니라 "채널을 보상하는" 문제다. 같은 채널에 프리엠퍼시스를 켜면 DDJ가 줄어드는지 재측정해 닫는다. 수신 쪽 eye가 열리는지는 HWLAB-M09/M12.
- **RJδδ 1.53 ps (TX1) vs RJ 1.00 ps.** TX1의 RJδδ가 TX0(1.20)보다 큰 건 RJ가 커서가 아니라 큰 DDJ 무더기가 얕은 꼬리를 휘게 해서다. 스펙의 "RJ" 항목이 dual-Dirac RJ인지 스펙트럼 RJ rms인지에 따라 같은 레인이 다른 숫자를 받는다.
- TJ 허용 오차를 −3 %…+12 %로 둔 근거: 위 demo 표의 얕은 창(8320 에지) 편향이 +6 %±1 %, TX1처럼 DDJ가 큰 경우 +7 %. 즉 이 판정은 **보수적인 쪽으로 몇 % 틀린다** — PASS 레인의 마진(TX0 0.03 UI)이 그 편향보다 커야 PASS를 믿는다.

**장비가 있을 때 (이 레슨에선 실행하지 않음):** 실시간 스코프의 지터 분석 옵션(Keysight EZJIT+/EZJIT Complete, Tektronix DPOJET 등)이 같은 일을 한다. 설정할 것: ① 클럭 복원 방식(golden PLL 차수·BW, 또는 상수 클럭), ② **패턴 길이**(PRBS7 → 127, 또는 arbitrary-data 모드), ③ RJ/DJ 분리 방법(스펙트럼 / 꼬리 피팅)과 기준 BER, ④ 레코드 길이(수백만 UI). 메뉴 이름·기본값은 벤더·버전마다 다르니 매뉴얼로 확인한다. PyVISA로 파형을 받아(HWLAB-B04의 프리앰블 변환) `decompose()` 에 넣으면 스코프의 분해와 **같은 파형으로 두 의견** 을 비교할 수 있다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"           # 22/22 passed (2.9s), exit=0
python3 -I -W error -X dev solution.py             # 경고 없음 (exit 0)
python3 -I gen_capture.py                          # JSON/CSV 재생성 → shasum 동일 (결정적)
python3 -I demo_tailfit.py                         # §5 Part B 의 기록 길이 × 피팅 깊이 표 (~1.4s)
```

<!-- code: demo_tailfit.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/방법 |
|---|---|---|
| `RJ = std(TIE)` | DJ까지 가우스로 취급 → 6.13 ps vs 1.5 ps | DDJ·PJ를 빼고 남은 RMS (`rj_rms`) |
| 관측 pk-pk를 TJ로 보고 | 에지 수에 따라 커지는 숫자(29.96 → 32.67 ps), 1e-12까지 못 봄 | `dual_dirac(tail_fit(...))` 로 외삽, 에지 수 함께 기록 |
| DDJ를 빼기 전에 PJ 탐색 | 패턴 고조파(10 × 78.74 MHz)를 PJ로 오인 | DDJ → PJ → RJ 순서 고정, 패턴 고조파 위치 제외 |
| FFT 빈 크기로 PJ 진폭 | 빈 사이 주파수면 최대 −36 %(직사각)·−15 %(Hann) | 피크 주변에서 사인 LS 진폭 최대화 |
| 에지 없는 UI를 0으로 채워 FFT | 패턴 주기로 변조된 가짜 측대파 | 선형 보간으로 채움 (`fill_uniform`) |
| 꼬리 피팅에 `Φ⁻¹(F)` | dual-Dirac 델타 무게 1/2를 무시 → Q-scale에서 직선이 아님, μ·σ 왜곡 | `Φ⁻¹(2F)` (무게 ρ = 1/2 정규화) |
| 피팅 점을 F 균일 간격으로 | 얕은 꼬리 점 수천 개가 피팅을 독점 | 확률 로그 간격 (decade당 40점) |
| DJδδ를 DJpp 스펙과 비교 | 서로 다른 정의(17.35 vs 21.74 ps) | 스펙이 정의한 양과 같은 방법으로 측정 |
| 패턴 길이를 틀리게 (PRBS31인데 127로 평균) | 위치별 평균이 섞여 DDJ가 0 근처로 뭉개지고, DDJ가 RJ/BUJ로 넘어감 | 실제 패턴 길이, 또는 비트 이력 기반 DDJ 추정 |
| 이상 클럭을 공칭 UI로 고정 | 송신 ppm 오프셋이 TIE 램프 → 거대한 가짜 저주파 지터 | 직선 피팅 또는 golden PLL |
| 스코프 잡음 바닥을 무시 | 진폭 잡음/slew가 RJ로 더해짐 (1 mV → 0.11 ps, 10 mV에선 선형식도 깨짐) | 바닥 측정 후 RJ만 제곱합으로 빼기 (DJ는 빼면 안 됨) |

## 8. 면접 Q&A

<details>
<summary>Q1. RJ와 DJ를 왜 굳이 나누나요? TJ를 rms로 더하면 안 되나요?</summary>

BER에 따른 거동이 다르기 때문이다. RJ는 가우스라 **유계가 아니다** — 기다리면 언제든 더 큰 값이 나온다. 그래서 RJ는 σ로 표현하고 BER을 정해야 크기(2·Q·σ)가 정해진다. DJ는 유계라 BER을 1e-12에서 1e-15로 낮춰도 그대로다. TJ는 두 분포의 컨볼루션의 꼬리이지 rms 합이 아니다: rms 합 √(DJ² + RJ²) 같은 식은 단위부터 섞인 것(pk-pk와 rms)이다.
꼬리질문: "BER을 1e-12 → 1e-15로 바꾸면 TJ는 얼마나 늘어나나?" → dual-Dirac로 2·(7.941 − 7.034)·RJδδ = 1.81·RJδδ. DEMO 긴 기록 피팅에서 TJ 42.06 → 45.24 ps, 정답 컨볼루션은 44.10 ps(연습 3). DJ는 그대로, RJ 항만 커진다.

</details>

<details>
<summary>Q2. 스코프는 DDJ를 어떻게 분리하나요? PRBS31이나 실제 트래픽이면요?</summary>

반복 패턴이면 패턴 위치별 TIE 평균이 DDJ다 — 같은 위치는 같은 비트 이력을 가지므로. 조건: 패턴 길이를 알 것, 위치마다 충분히 반복될 것(이 레슨 130회 → 위치당 잡음 0.13 ps), 저주파 지터가 평균을 흔들지 않게 클럭 복원이 적절할 것.
꼬리질문: "PRBS31은 2³¹−1 비트라 한 캡처에 반복이 없다." → 채널 메모리가 몇 UI뿐이라는 점을 이용한다: "직전 N비트 이력(예: 5~10비트)이 같은 에지끼리" 평균하면 DDJ를 근사할 수 있다(이력 길이 ≥ 채널 메모리여야 함). 이력이 짧으면 남은 ISI가 RJ/BUJ로 넘어가 RJ가 부푼다. 그래서 컴플라이언스 지터 측정은 보통 짧은 패턴(PRBS7, 컴플라이언스 패턴)으로 하고, PRBS31은 BER·스트레스 테스트에 쓴다.

</details>

<details>
<summary>Q3. dual-Dirac DJδδ가 DJpp보다 작고 RJδδ는 RJrms보다 큽니다. 어느 쪽이 맞는 값인가요?</summary>

둘 다 "맞다" — 다른 질문에 대한 답이다. DJpp·RJrms는 **분해** (원인별 크기)이고, DJδδ·RJδδ는 특정 깊이의 꼬리를 재현하는 **모델 파라미터** 다. 실제 DJ 분포의 끝 부분은 확률이 작아서(아크사인 분포의 끝, DDJ 극단 위치 몇 개) 얕은 꼬리에선 가우스 기울기에 섞여 보인다 → σ는 커지고 델타 간격은 줄어든다. 이 레슨 DEMO: DJδδ 17.35 < DJpp 21.74, RJδδ 1.76 > RJ 1.50, 그래도 TJ는 +2 %.
꼬리질문: "스펙에는 어느 걸 넣나?" → 스펙이 정의한 쪽. 많은 SerDes 규격이 DJ를 dual-Dirac DJ(δδ)로, RJ를 dual-Dirac 또는 스펙트럼 RJ로 정의한다 — 항목 이름·측정 조건(클럭 복원, 패턴, 기준 BER)은 해당 스펙에서 확인한다. 측정값을 다른 정의의 한계와 비교하는 게 가장 흔한 사고다.

</details>

<details>
<summary>Q4. Q(1e-12) = 7.034는 어디서 나오고, 전이 밀도는 왜 신경 써야 하나요?</summary>

Q(BER) = Φ⁻¹(1 − BER): 표준 가우스가 Q보다 클 확률이 BER인 점. 1e-12 → 7.034, 그래서 TJ = DJδδ + 14.069·RJδδ. 이건 "에지당 확률" 약속이다. BER을 비트당으로 정의하면 오류는 전이가 있는 비트에서만 생기므로 에지당 허용 확률이 BER/ρT로 커지고, PRBS(ρT ≈ 0.5)에선 Q = 6.937, 곱수 13.87이 된다. RJ 1.5 ps면 TJ 차이 0.29 ps.
꼬리질문: "bathtub에서 Q-scale을 쓰면?" → 같은 Φ⁻¹를 BER 축에 쓴다. 전이 밀도 약속을 피팅 정규화에 넣느냐에 따라 절편이 달라진다 — HWLAB-M11에서 다룬다.

</details>

<details>
<summary>Q5. TIE 스펙트럼에 25 MHz 스퍼가 보입니다. 원인을 어떻게 찾고, golden PLL이 걸러 주지 않나요?</summary>

주파수가 지문이다. 보드에서 25 MHz(또는 그 약수의 고조파)를 만드는 것을 나열한다: 이더넷 PHY 레퍼런스 오실레이터, 스위칭 레귤레이터(스위칭 주파수와 고조파), 다른 클럭 도메인. 하나씩 끄거나 주파수를 바꿔(스위처 sync 핀, 다른 오실레이터) 스퍼가 따라 움직이는지 본다. 동시에 SERDES PLL 전원(VDDA_PLL)의 리플 스펙트럼을 보고(HWLAB-B08), 근접장 프로브로 커플링 경로를 찾는다. 수정은 PLL 전원 필터(페라이트+커패시터) 확인, 어그레서 트레이스 이격 등.
꼬리질문: "golden PLL이 지우지 않나?" → 1차 golden PLL은 E/X = s/(s+ωc), BW = 10 Gb/s / 1667 ≈ 6 MHz. 25 MHz는 |H| = 0.972라 97 %가 남는다(연습 2). 2 MHz였다면 32 %만 남았을 것이다 — 스펙이 CDR 전달함수를 적용하는 이유는 **수신기 CDR이 따라가는 저주파 지터는 비트 오류를 만들지 않기** 때문이다.

</details>

<details>
<summary>Q6. 측정 장비 자체의 지터는 어떻게 처리하나요?</summary>

스코프의 RJ 바닥은 내부 타임베이스 지터와 진폭 잡음/기울기(σv/slew) 변환으로 생긴다. 이 레슨 연습 1: 16 샘플/UI 선형 보간에서 σt ≈ √(2/3)·σv/slew (샘플 잡음이 독립이라는 가정 — 실제 스코프는 잡음이 대역 제한되어 이웃 샘플이 상관되므로 대략 σv/slew). 1 mV면 0.11 ps, 측정 RJ 1.5079 ps에 그 몫이 들어 있다. 바닥을 알면 RJ는 **제곱합으로** 뺄 수 있다: RJ_dut = √(RJ_meas² − RJ_floor²). DJ는 선형으로 더해지는 유계량이라 이런 식으로 빼면 안 된다.
꼬리질문: "바닥은 어떻게 재나?" → 데이터시트의 jitter measurement floor를 참고하고, 지터가 매우 낮은 소스(저위상잡음 신호원)를 같은 설정(V/div, 샘플링률, 대역폭)으로 측정해 확인한다. V/div를 파형이 화면을 채우게 맞추면(클리핑 없이) σv/slew가 줄어든다.

</details>

<details>
<summary>Q7. 분해했더니 RJ가 예상보다 크고, Q-scale 꼬리가 깊은 곳에서도 휘어 있습니다. 무엇을 의심하나요?</summary>

BUJ(bounded uncorrelated jitter) — 대표적으로 이웃 레인 크로스토크. 피해 레인의 패턴과 무관하니 패턴 평균(DDJ)에 안 잡히고, 이웃 데이터가 랜덤이면 스펙트럼 선도 없어 PJ에도 안 잡혀 **잔차 = RJ** 로 넘어간다. 하지만 유계라 깊은 꼬리에선 가우스보다 빨리 떨어진다 → Q-scale 직선이 아님. 확인: 어그레서 레인을 끄거나 같은 패턴/다른 패턴으로 바꾸며 RJ가 변하는지 본다. 진짜 RJ(열잡음·PLL)는 이웃 레인과 무관하다.
꼬리질문: "RJ로 잘못 분류하면 TJ는?" → 유계 성분을 가우스로 외삽하므로 TJ(1e-12)를 **과대** 평가한다(보수적이지만 수율 손해).

</details>

## 9. 추가 연습

### 연습 1 — 진폭 잡음이 지터가 되는 양

정착된 RC 에지(0 V 교차 기울기 amp/τ = 7.27 mV/ps)를 6.25 ps 간격, 임의 샘플 위상으로 찍고 샘플마다 가우스 잡음 σv를 더한 뒤 선형 보간 교차의 표준편차를 몬테카를로로 잰다(`amp_noise_jitter`). 이론 √(2/3)·σv/slew와 비교하고, Part A 캡처의 RJ 1.5079 ps가 √(1.5² + 잡음지터²)와 맞는지 확인한다.

<details>
<summary>답 보기</summary>

보간 교차 `t = t_i + (L − v_i)/(v_{i+1} − v_i)·dt` 에서 잡음의 1차 효과는 `δt ≈ −[(1−f)·n_i + f·n_{i+1}]/slew` (f = 교차의 샘플 사이 위치). 분산을 f에 대해 평균하면 ∫(1−f)² + f² df = 2/3 → σt = √(2/3)·σv/slew. 실제 출력: 1 mV → 0.1124 ps (이론 0.1123), 2 mV → 0.2263 ps (0.2245). **10 mV → 3.265 ps (이론 1.123)**: 샘플 간 신호 변화가 45 mV인데 두 샘플 잡음 차이가 σ√2 = 14 mV라 분모 (v_{i+1} − v_i)가 크게 흔들리고 분포 꼬리가 두꺼워진다 — 선형화가 깨지는 영역. Part A: 예측 1.5042 ps, 측정 1.5079 ps.

</details>

### 연습 2 — golden PLL 기준 TIE

1차 PLL `φ[n+1] = φ[n] + α·(x[n] − φ[n])`, `α = 1 − e^(−2π·fc/fbaud)`, fc = 10 Gb/s / 1667 = 5.999 MHz로 TIE를 추적하고, 남은 TIE `e = x − φ` 의 PJ 진폭 비율을 2 / 25 / 50.97 MHz에서 |H| = f/√(f² + fc²)와 비교한다(`golden_pll_residual`).

<details>
<summary>답 보기</summary>

z 영역에서 E/X = (1 − z⁻¹)/(1 − (1−α)z⁻¹) — fbaud에 비해 낮은 주파수에선 s/(s + ωc)의 1차 고역통과와 같다. 실제 출력: 2 MHz 0.317 (|H| 0.3163), 25 MHz 0.974 (0.9724), 50.97 MHz 0.995 (0.9931). 즉 VALBRD-B TX2의 25 MHz PJ는 golden PLL로 재도 9.0 → 약 8.75 ps라 판정이 바뀌지 않는다. 반대로 1~2 MHz 스위처 스퍼는 golden PLL에서 대부분 사라진다 — "직선 피팅 TIE에선 FAIL, 스펙 측정(golden PLL)에선 PASS" 가 가능하므로 리포트에 클럭 복원 방식을 꼭 적는다. 2차 PLL(피킹 있음)의 전달함수는 HWLAB-A01(CDR/JTOL)에서.

</details>

### 연습 3 — Q(BER) 표, 전이 밀도, 다른 BER의 TJ

`q_ber(ber, rho_t)` 로 1e-6/1e-9/1e-12/1e-15의 Q와 ρT = 0.5일 때의 Q(1e-12)를 구하고, 긴 기록 dual-Dirac 피팅을 1e-9와 1e-15로 외삽해 정답 컨볼루션 TJ와 비교한다.

<details>
<summary>답 보기</summary>

실제 출력: Q = 4.7534 / 5.9978 / 7.0345 / 7.9413, ρT = 0.5 → 6.9372. 외삽: BER 1e-9 TJδδ 38.42 ps vs 정답 37.91 ps (+1.3 %), 1e-15 45.24 vs 44.10 ps (+2.6 %). 1e-5…1e-3 깊이에서 맞춘 직선을 1e-15까지 멀리 연장할수록 오차가 커진다(σ가 실제보다 커서 기울기 과대). 그래도 보수적인 쪽이다. 1e-15 같은 깊이를 직접 확인할 수는 없으므로, 외삽 결과에는 피팅 깊이를 함께 적는다.

</details>

### 연습 4 — DCD 분리 (수신 임계 오프셋)

같은 DEMO 캡처를 임계 0 V와 +40 mV로 에지 검출해, 위치별 DDJ를 에지 방향으로 나눠 `DCD = mean(DDJ 상승) − mean(DDJ 하강)` 과 방향별 평균을 뺀 ISI pk-pk를 구한다(`dcd_split`). 정답은 같은 임계에서의 잡음 없는 교차(`true_ddj(cfg, thr)`).

<details>
<summary>답 보기</summary>

임계가 +40 mV면 상승 에지는 늦게, 하강 에지는 이르게 교차한다. 정착된 에지의 공식: DCD = τ·ln((amp + thr)/(amp − thr)) = 55 ps·ln(0.44/0.36) = 11.04 ps. 실제 출력: thr = 0 → DCD −0.036 ps, thr = +40 mV → DCD 11.001 ps (정답 11.037 ps). DDJ pk-pk는 10.15 → 21.17 ps로 두 배가 되지만 ISI 부분은 10.18 → 10.17 ps로 그대로다. 실제 원인: 차동 오프셋, 비대칭 상승/하강 시간, 직렬화기의 반속(half-rate) 클럭 듀티 오차. 방향별로 나눠 보면 "채널(ISI)" 과 "드라이버/클럭 듀티(DCD)" 를 다른 문제로 보고할 수 있다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 14/14 passed, exit=0 (1.1s)
```

## 10. 핵심 정리

- TIE = 에지 시각 − 이상 클럭(직선 피팅 또는 golden PLL). 클럭 복원 방식이 바뀌면 저주파 지터의 양이 바뀐다.
- 분해 순서 고정: **DDJ(패턴 위치별 평균) → PJ(잔차 FFT 피크 + 사인 LS) → RJ(남은 RMS)**. 순서를 바꾸면 패턴 고조파가 PJ가 된다.
- PJ 진폭은 FFT 빈 크기가 아니라 피크 주변 LS로 (scalloping 최대 −36 %). 에지 없는 UI는 보간으로 채운다.
- dual-Dirac: 꼬리를 Q-scale `x = μ + σ·Q`, `Q = Φ⁻¹(2F)` 로 직선 피팅 → `TJ = DJδδ + 2·Q(BER)·RJδδ`, Q(1e-12) = 7.034 (에지당 약속, ρT = 0.5 비트당이면 6.937).
- DJδδ < DJpp, RJδδ > RJrms — 모델 파라미터이지 분해가 아니다. TJ만 맞게 나온다. 얕은 피팅은 TJ를 +6 % 과대, 깊게 피팅하려면 긴 기록이 필요하다.
- `std(TIE)` 는 RJ가 아니고(×4), pk-pk는 TJ가 아니다(에지 수에 따라 자람). 성분 합 DJpp + 2Q·RJ는 보수적(+4 %).
- 리포트는 "TJ 숫자" 가 아니라 "어떤 성분이 얼마이고 그래서 무엇을 고칠지": DDJ → 채널/이퀄라이제이션, PJ → 그 주파수의 어그레서, RJ → refclk·PLL 전원.
- 측정 바닥(σv/slew)은 RJ로 보인다 — RJ만 제곱합으로 빼고 DJ는 빼지 않는다.

## 참고자료

- Ransom Stephens, *Jitter Analysis: The dual-Dirac Model, RJ/DJ, and Q-Scale*, Agilent/Keysight 백서 — dual-Dirac 정의, DJδδ vs DJpp, Q-scale 꼬리 피팅
- INCITS T11 *Fibre Channel — Methodologies for Jitter and Signal Quality Specification (MJSQ)* 기술 보고서 — RJ/DJ/TJ 정의, golden PLL, BER과 전이 밀도 약속의 원전
- Mike Peng Li, *Jitter, Noise, and Signal Integrity at High-Speed*, Prentice Hall (2007) — 지터 나무, 스펙트럼 분해, 꼬리 피팅
- Tektronix, *Understanding and Characterizing Timing Jitter* (primer) — TIE, 지터 성분, 스펙트럼 방법
- Keysight Infiniium EZJIT+ / Tektronix DPOJET 사용자 설명서 — 실제 도구의 RJ/DJ 분리 설정(패턴 길이, 클럭 복원, 기준 BER)
- PCI-SIG *PCI Express Base Specification* — 송신 지터 항목과 CDR 전달함수 적용 방식 (항목·한계값은 해당 세대 스펙에서 확인)
- 관련 레슨: HWLAB-M07(주기/c2c/TIE), HWLAB-M08(위상잡음 → RMS 지터), HWLAB-M09(eye diagram·golden PLL), HWLAB-M11(bathtub·Q-scale 외삽), HWLAB-M12(ISI·FFE/CTLE/DFE), HWLAB-B04(교차 보간), HWLAB-B08(리플 측정), HWLAB-A01(CDR/JTOL)
