---
id: HWLAB-M02
time: 80분
updated: 2026-11-08
---
# HWLAB-M02 Load Transient 응답: Droop·Recovery·위상 마진 추정

## 학습 목표

- 부하 스텝 캡처(time, vout, iload)에서 **스텝 시작·slew(전류 채널) → droop → 허용 밴드 복귀 시간(recovery) → 링잉 주기 Td → 로그 감쇠율 λ → 감쇠비 ζ → 위상 마진(PM)** 까지를 직접 계산한다.
- 레귤레이터 폐루프 출력 임피던스를 **2차 모델(병렬 RLC: L_eq ∥ C ∥ R)** 로 설명하고, droop ≈ ΔI·Z0 = ΔI/(2π·fn·C), 램프 중 오프셋 L_eq·di/dt, Zout 피크 Q = 1/(2ζ) 를 수치로 연결한다.
- **PM ≈ 100·ζ 근사의 유효 범위**(ζ ≲ 0.6 에서 3.3° 이내, ζ = 0.8 이면 10° 과대평가)와 정확식 PM = atan(2ζ/√(√(1+4ζ⁴) − 2ζ²)) 를 쓸 수 있다.
- 측정이 거짓말하는 지점을 수치로 보인다: 스위칭 리플(1 fsw 박스카로 제거), load-line 기준선, 느린 e-load(droop 27 %만 보임 + 강제응답 로브로 ζ 0.20 → 0.28), ζ 가 커서 로브가 하나뿐일 때(하한만 가능).
- 보상 변경 전/후 두 캡처로 `FAIL ... PM≈17° recovery 29.7µs > 25µs` / `PASS ... PM≈55°` 같은, 하드웨어·파워 엔지니어가 바로 움직일 수 있는 리포트를 만든다.

## 1. 문제

SoC가 idle → full load로 넘어가는 순간 코어 레일이 얼마나 꺼지고(droop) 얼마나 빨리, 얼마나 얌전하게 돌아오는지가 보드 bring-up의 전원 무결성(power integrity) 첫 관문이다 — Rivian/NVIDIA/Apple 보드·실리콘 검증 면접의 단골 질문 "load step 캡처 하나로 루프가 안정한지 어떻게 판단하나? 그 숫자의 가정은?" 에 답하려면 측정 알고리즘을 직접 짜 봐야 한다.

부하 스텝 시험은 이렇게 한다: 보드 위 load-step FET(또는 e-load)로 부하 전류를 0.5 A → 3 A로 정해진 slew(여기선 1 A/µs)로 바꾸고, 전류 프로브 채널로 트리거해 레일 전압을 스프링 팁으로 잰다. 루프가 응답하기 전까지 전류 차이는 출력 커패시터가 메꾸므로 전압이 떨어지고(droop), 루프가 따라잡으면서 되돌아오는데, **위상 마진이 부족하면 링잉하며 천천히 가라앉는다.** 아래 그림의 실선(보상 변경 전)과 점선(후)은 이 레슨의 Part C 보드 모델에서 계산한 참값이다.

```svg
<svg viewBox="0 0 760 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">부하 스텝 0.5→3 A (1 A/µs) 에 대한 VDD_CORE_0V8 편차 — 보상 변경 전(실선)/후(점선), 모델 참값</text>
  <rect x="70" y="97.1" width="670" height="49.5" class="f-ok" opacity=".25"/>
  <g stroke="currentColor" stroke-dasharray="4 4" opacity=".5">
    <line x1="70" y1="121.8" x2="740" y2="121.8"/><line x1="120" y1="40" x2="120" y2="318"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="8" y="125.8">0.800 V</text><text x="8" y="101.1">+1 % 밴드</text><text x="8" y="150.5">−1 % 밴드</text>
    <text x="8" y="218.5">−30 mV</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="70,121.8 75,121.8 80,121.8 85,121.8 91,121.8 96,121.8 101,121.8 106,121.8 111,121.8 116,121.8 122,122.3 127,132.3 132,153.8 137,180.3 142,199.8 147,210.9 152,213.3 158,207.6 163,195.1 168,177.2 173,156.2 178,134.0 183,112.7 189,94.0 194,79.3 199,69.5 204,65.2 209,66.1 214,71.7 219,81.1 225,93.3 230,106.9 235,120.6 240,133.2 245,143.7 250,151.4 256,155.9 261,157.1 266,155.2 271,150.5 276,143.7 281,135.7 286,127.1 292,118.8 297,111.5 302,105.8 307,101.9 312,100.0 317,100.2 323,102.3 328,105.8 333,110.5 338,115.7 343,121.0 348,125.9 353,130.0 359,133.1 364,134.9 369,135.4 374,134.8 379,133.0 384,130.5 390,127.4 395,124.1 400,120.9 405,118.0 410,115.8 415,114.2 420,113.4 426,113.5 431,114.2 436,115.5 441,117.3 446,119.3 451,121.3 457,123.3 462,124.9 467,126.1 472,126.8 477,127.1 482,126.8 487,126.2 493,125.2 498,124.1 503,122.8 508,121.5 513,120.4 518,119.5 524,118.9 529,118.6 534,118.6 539,118.9 544,119.4 549,120.0 554,120.8 560,121.6 565,122.3 570,123.0 575,123.4 580,123.7 585,123.8 591,123.8 596,123.5 601,123.2 606,122.7 611,122.2 616,121.7 621,121.3 627,121.0 632,120.7 637,120.6 642,120.6 647,120.7 652,120.9 658,121.1 663,121.4 668,121.7 673,122.0 678,122.2 683,122.4 688,122.6 694,122.6 699,122.6 704,122.5 709,122.3 714,122.2 719,122.0 725,121.8 730,121.6 735,121.5 740,121.4"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" stroke-dasharray="6 4" opacity=".75" points="70,121.8 80,121.8 91,121.8 101,121.8 111,121.8 122,122.2 132,151.0 142,188.9 152,201.6 163,196.6 173,181.8 183,163.2 194,145.4 204,130.8 214,120.5 225,114.5 235,112.0 245,112.0 256,113.6 266,115.8 276,118.2 286,120.2 297,121.6 307,122.6 317,123.0 328,123.1 338,122.9 348,122.7 359,122.4 369,122.1 379,121.9 390,121.8 400,121.7 410,121.7 420,121.7 431,121.7 441,121.7 451,121.8 462,121.8 472,121.8 482,121.8 493,121.8 503,121.8 513,121.8 524,121.8 534,121.8 544,121.8 554,121.8 565,121.8 575,121.8 585,121.8 596,121.8 606,121.8 616,121.8 627,121.8 637,121.8 647,121.8 658,121.8 668,121.8 678,121.8 688,121.8 699,121.8 709,121.8 719,121.8 730,121.8 740,121.8"/>
  <g stroke="currentColor"><line x1="151" y1="121.8" x2="151" y2="213.5"/></g>
  <g fill="currentColor" font-size="12">
    <text x="115" y="229.5">a0 = droop 29.7 mV</text>
    <text x="196" y="58">a1</text><text x="252" y="174">a2</text><text x="306" y="94">a3</text>
  </g>
  <g stroke="currentColor"><line x1="207" y1="46" x2="315" y2="46"/><line x1="207" y1="41" x2="207" y2="51"/><line x1="315" y1="41" x2="315" y2="51"/></g>
  <text x="222" y="40" fill="currentColor" font-size="12">Td = 21.07 µs (fn 48 kHz)</text>
  <text x="370" y="88" fill="currentColor" font-size="12">|a(k+1)/a(k)| = e^(−πζ/√(1−ζ²)) = 0.621 (ζ=0.15)</text>
  <g stroke="currentColor" stroke-width="1.5"><line x1="193" y1="97.1" x2="193" y2="250"/>
    <line x1="274" y1="97.1" x2="274" y2="250"/></g>
  <g fill="currentColor" font-size="12">
    <text x="131" y="246">후 14.1 µs</text>
    <text x="279" y="246">전 recovery 29.9 µs (±1 % 밴드에 머물기 시작)</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="70,312 120,312 133,270 740,270"/>
  <g fill="currentColor" font-size="12"><text x="8" y="274">i = 3 A</text><text x="8" y="316">i = 0.5 A</text>
    <text x="138" y="291">램프 2.5 µs (1 A/µs)</text></g>
  <g fill="currentColor" font-size="12">
    <text x="118" y="336">0</text><text x="217" y="336">20</text><text x="320" y="336">40</text>
    <text x="423" y="336">60</text><text x="526" y="336">80</text><text x="625" y="336">100</text>
    <text x="700" y="336">120 µs</text></g>
</svg>
```

왜 링잉에서 위상 마진이 보이나: 폐루프 출력 임피던스 Zout(f)는 낮은 주파수에선 루프 이득이 커서 작고 주파수에 비례해 커지며(유도성, sL_eq), 교차 주파수 위에선 출력 커패시터 임피던스 1/(sC)로 떨어진다. 둘이 만나는 곳(≈ 루프 교차 주파수)이 **병렬 공진**이고, 그 피크의 Q가 곧 폐루프 극점의 감쇠비 ζ 다(Q = 1/(2ζ)). 스텝 전류는 이 공진을 때려서 감쇠 진동을 만든다. 그래서 링잉의 감쇠율 → ζ → (2차 루프 가정 하에서) PM으로 환산할 수 있다. 이 레슨은 Zout을 다음 2차 모델로 둔다 (gen_capture.py).

`Zout(s) = R_LL + (1/C) · s / (s² + 2ζωn·s + ωn²)` — R_LL은 load-line(AVP) 저항(일반 POL은 0), C는 유효 출력 용량, ωn = 1/√(L_eq·C), Z0 = √(L_eq/C) = 1/(ωn·C).

```svg
<svg viewBox="0 0 760 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">폐루프 출력 임피던스 |Zout(f)| (log-log) — 낮은 위상 마진 = 높은 Q 피크</text>
  <g stroke="currentColor" opacity=".3">
    <line x1="80" y1="270" x2="720" y2="270"/><line x1="80" y1="197" x2="720" y2="197"/><line x1="80" y1="123" x2="720" y2="123"/><line x1="80" y1="50" x2="720" y2="50"/>
    <line x1="80" y1="50" x2="80" y2="270"/><line x1="293" y1="50" x2="293" y2="270"/><line x1="507" y1="50" x2="507" y2="270"/><line x1="720" y1="50" x2="720" y2="270"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 5" opacity=".7">
    <line x1="80" y1="233.6" x2="614" y2="50.0"/>
    <line x1="263" y1="50.0" x2="720" y2="207.0"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.2" points="80,233.5 85,231.7 91,229.9 96,228.0 101,226.2 107,224.4 112,222.5 117,220.7 123,218.9 128,217.0 133,215.2 139,213.3 144,211.5 149,209.7 155,207.8 160,206.0 165,204.1 171,202.3 176,200.5 181,198.6 187,196.8 192,194.9 197,193.1 203,191.2 208,189.3 213,187.5 219,185.6 224,183.8 229,181.9 235,180.0 240,178.1 245,176.3 251,174.4 256,172.5 261,170.6 267,168.6 272,166.7 277,164.8 283,162.8 288,160.9 293,158.9 299,156.9 304,154.9 309,152.8 315,150.7 320,148.6 325,146.5 331,144.3 336,142.1 341,139.8 347,137.5 352,135.0 357,132.5 363,130.0 368,127.2 373,124.4 379,121.4 384,118.3 389,114.9 395,111.2 400,107.3 405,102.9 411,98.1 416,92.8 421,87.0 427,80.8 432,75.2 437,72.1 443,73.2 448,77.9 453,83.9 459,90.0 464,95.5 469,100.6 475,105.1 480,109.3 485,113.1 491,116.6 496,119.9 501,122.9 507,125.9 512,128.6 517,131.3 523,133.8 528,136.3 533,138.6 539,140.9 544,143.2 549,145.4 555,147.6 560,149.7 565,151.8 571,153.8 576,155.9 581,157.9 587,159.9 592,161.8 597,163.8 603,165.8 608,167.7 613,169.6 619,171.5 624,173.4 629,175.3 635,177.2 640,179.1 645,181.0 651,182.8 656,184.7 661,186.6 667,188.4 672,190.3 677,192.1 683,194.0 688,195.8 693,197.7 699,199.5 704,201.4 709,203.2 715,205.1 720,206.9"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" stroke-dasharray="6 4" points="80,215.2 85,213.4 91,211.6 96,209.7 101,207.9 107,206.1 112,204.2 117,202.4 123,200.5 128,198.7 133,196.9 139,195.0 144,193.2 149,191.4 155,189.5 160,187.7 165,185.8 171,184.0 176,182.2 181,180.3 187,178.5 192,176.6 197,174.8 203,172.9 208,171.1 213,169.2 219,167.4 224,165.5 229,163.7 235,161.8 240,159.9 245,158.1 251,156.2 256,154.3 261,152.4 267,150.5 272,148.6 277,146.7 283,144.8 288,142.9 293,141.0 299,139.1 304,137.1 309,135.2 315,133.2 320,131.2 325,129.3 331,127.3 336,125.3 341,123.3 347,121.3 352,119.3 357,117.3 363,115.4 368,113.5 373,111.7 379,110.0 384,108.5 389,107.1 395,105.9 400,105.0 405,104.4 411,104.2 416,104.2 421,104.7 427,105.4 432,106.5 437,107.7 443,109.2 448,110.9 453,112.6 459,114.5 464,116.4 469,118.3 475,120.3 480,122.3 485,124.3 491,126.3 496,128.3 501,130.2 507,132.2 512,134.2 517,136.1 523,138.1 528,140.0 533,142.0 539,143.9 544,145.8 549,147.7 555,149.6 560,151.5 565,153.4 571,155.2 576,157.1 581,159.0 587,160.9 592,162.7 597,164.6 603,166.4 608,168.3 613,170.1 619,172.0 624,173.8 629,175.7 635,177.5 640,179.4 645,181.2 651,183.1 656,184.9 661,186.8 667,188.6 672,190.4 677,192.3 683,194.1 688,195.9 693,197.8 699,199.6 704,201.5 709,203.3 715,205.1 720,207.0"/>
  <circle cx="439" cy="110.3" r="4" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="90" y="199.6">sL_eq: 루프 이득이 큰 구간</text>
    <text x="90" y="213.6">L_eq = 1/(C·ωn²) = 50 nH</text>
    <text x="470" y="232">1/(sC): 출력 커패시터 (220 µF)</text>
    <text x="447" y="65.9">전 ζ=0.15: 피크 50 mΩ, Q = 3.3</text>
    <text x="449" y="132.3">Z0 = 1/(ωn·C) = 15.1 mΩ</text>
    <text x="470" y="252">후 ζ=0.55 (점선): 피크 18.3 mΩ, Q = 0.91</text>
    <text x="22" y="274">0.1m</text><text x="40" y="201">1m</text><text x="34" y="127">10m</text><text x="28" y="54">100m</text>
    <text x="68" y="290">1k</text><text x="279" y="290">10k</text><text x="489" y="290">100k</text><text x="680" y="290">1 MHz</text>
    <text x="10" y="312">fn 에서 반복되는 사인파 부하: 전압 진폭 = |Z|피크 × 전류 진폭.  1회성 스텝: droop ≈ ΔI·Z0 (= ΔI/(2π·fn·C))</text>
  </g>
</svg>
```

### Part A — 스텝·droop·recovery·링잉·ζ·PM (측정 라이브러리 핵심)

- `step_edges(t, i)` — 전류 채널의 10/90 % 교차(HWLAB-B04의 "10 %를 마지막으로 떠난/90 %에 처음 닿은" 규칙)로 slew = 0.8·|ΔI|/(t90 − t10), 램프 시작/끝 t_start/t_end를 1/8 외삽으로. 부하 해제(i1 < i0)도 같은 함수.
- `lobes(t, e, j0, h)` — 기준선 대비 편차 e(t)를 **히스테리시스 ±h** 로 반주기 덩어리(로브)로 자르고, 로브마다 꼭짓점(2차 최소제곱)과 로브를 시작한 0 교차 시각 t_zc를 구한다.
- `fit_decay(sel)` — ln|a_k| 를 반주기 번호 k 에 대해 **가중 최소제곱**(가중치 a_k²) → λ → `zeta_from_lambda` 로 ζ = λ/√(π² + λ²). Td는 0 교차들의 직선 기울기 × 2(교차가 2개 미만이면 꼭짓점 시각으로).
- `phase_margin(ζ)` — 2차 루프 정확식과 100·ζ 를 같이. `recovery_time(...)` — 최종값 ± band 안에 **계속 머무르기 시작한** 시각 − t_start.
- 정답이 알려진 깨끗한 모델 캡처(ζ = 0.10/0.25/0.40/0.55, fn = 48 kHz, C = 220 µF, 0.5 → 3 A, 1 A/µs)에서 주입한 ζ ±0.003, Td ±0.5 %, droop ±0.15 mV, recovery ±0.1 µs 로 맞춘다. PM 정확식은 |L(jω)| = 1 을 이분법으로 푼 독립 검산과 0.01° 이내.

### Part B — 측정이 거짓말하는 지점 (면접관이 파고드는 곳)

- `boxcar(t, v, n)` — 길이 n = 1/(fsw·dt) 이동평균(가운데 시각 정렬): 8 mVpp 1 MHz 삼각파 리플이 정확히 0. n = 19면 0.211 mV 남는다.
- `select_free_decay(lbs, t_end, thr)` — **램프가 끝난 뒤의 자유감쇠 로브만**, 잡음 문턱 thr = max(6σ, 10⁻³·droop) 이상인 것만 연속으로.
- `analyze(..., filt, baseline, free_decay, hyst)` 스위치로 각 함정을 하나씩 재현해 주입한 참값과 비교: 리플 + 잡음(순진한 방법은 로브 544개, ζ ≈ 0), load-line 2 mΩ(스텝 전 값을 기준선으로 쓰면 ζ 0.25 → 0.204), 느린 e-load 0.1 A/µs(참 droop 7.63 mV vs 1 A/µs에서 27.82 mV, 강제응답 로브를 섞으면 ζ 0.20 → 0.282, 같은 잡음에서 쓸 수 있는 로브 6 → 2), ζ = 0.8(로브 1개 → ζ ≥ 0.696 하한만), 100·ζ 근사 표.

### Part C — VALBRD-A U12 VDD_CORE_0V8: 보상 변경 전/후 판정

가상의 보드 **VALBRD-A rev B**, SoC 코어 레일 `VDD_CORE_0V8`(벅 U12, fsw 1 MHz, 유효 Cout ≈ 220 µF). 첫 보드에서 링잉이 보여 파워 엔지니어가 보상(Rcomp 10 k → 6.8 k, Ccomp 조정)을 바꿨다. 같은 셋업(TP40 스프링 팁, Q9 load-step FET 0.5 → 3 A 1 A/µs, 20 MS/s)으로 찍은 두 캡처 `U12_before_comp.csv`, `U12_after_comp.csv` 를 스펙 `spec_vdd_core_0v8.json`(원시 캡처 window ±5 %, ±1 % 밴드 복귀 ≤ 25 µs, PM ≥ 45°, 시험 slew 1 A/µs ±10 %)으로 판정하고 **어느 쪽이 안정한지** 한 줄로 결론낸다. 시험 조건(slew)이 틀리면 PASS/FAIL이 아니라 `INVALID`.

## 2. Problem (English)

> **Estimate regulator stability from a load-step capture.**
>
> You get a scope capture with three columns — time, output voltage, load current — of a buck regulator stepped from
> 0.5 A to 3 A at 1 A/µs, plus a switching frequency `fsw`. Using only the Python 3 standard library:
>
> 1. `step_edges(t, i)`: find the 10 %/90 % load-current crossings, the slew rate (A/µs) and the ramp start/end.
> 2. Remove switching ripple with a moving average of exactly one switching period, take the post-step settled value as
>    the baseline, and split the deviation into half-cycle lobes with hysteresis (`±k·σ`, σ from the pre-step record).
> 3. Using only free-decay lobes (after the current ramp ends, above a noise threshold), fit `ln|a_k| = c − λ·k`
>    (weighted least squares) and return `ζ = λ / sqrt(π² + λ²)`, the ringing period `Td` (from zero crossings), the droop,
>    and the recovery time into a `±band` around the final value.
> 4. Convert ζ to phase margin with the exact second-order relation
>    `PM = atan(2ζ / sqrt(sqrt(1 + 4ζ⁴) − 2ζ²))` and with the `PM ≈ 100·ζ` rule; state where the rule breaks.
> 5. Given two captures (before/after a compensation change) and a JSON spec (±5 % window, ±1 % recovery ≤ 25 µs,
>    PM ≥ 45°, test slew 1 A/µs ±10 %), print PASS/FAIL/INVALID per capture and pick the stable one.
>
> Example: a noise-free second-order capture with injected ζ = 0.25, fn = 48 kHz, C = 220 µF must return ζ = 0.250,
> `Td = 21.517 µs`, droop 26.18 mV and recovery (±8 mV) 19.46 µs; `phase_margin(0.5)` = 51.83° (rule: 50°).

## 3. 시작 코드

<!-- code: starter.py -->

2차 Zout 모델 · 합성기 · 정답 계산 · Part C 캡처 생성기 (결정적 — 다시 돌려도 CSV가 바이트 단위로 같다):

<!-- code: gen_capture.py -->

Part C 스펙:

<!-- code: spec_vdd_core_0v8.json -->

```bash
cd lessons/HWLAB/HWLAB-M02
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 2/28 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: clean 2nd-order captures, measured vs injected truth --
FAIL  step_edges: slew 1A/us +-0.5%, ramp start/end +-10ns: NotImplementedError: TODO step_edges
FAIL  zeta_from_lambda inverts lambda = pi*z/sqrt(1-z^2) (z=0.05..0.9): NotImplementedError: TODO zeta_from_lambda
      (setup failed: NotImplementedError: TODO step_edges)
FAIL  zeta=0.10: zeta+-0.003, Td+-0.5%, droop+-0.15mV, recovery+-0.1us: KeyError: 'zeta'
...
-- Part B: ripple/noise, load-line baseline, slow e-load, high zeta, 100*zeta rule --
FAIL  boxcar(n=20 = 1/(fsw*dt)) nulls 8mVpp triangle ripple; n=19 leaves residual: n=20 max|res|=4.0e-03 V, n=19 4.000mV
...
PASS  slow e-load 0.1A/us: true droop < 35% of the 1A/us droop (slow load hides the problem)   [7.63mV vs 27.82mV]
...
-- Part C: VALBRD-A U12 VDD_CORE_0V8 load-step report (before/after compensation) --
PASS  captures loaded: 5000 samples @ 50ns, 3 columns
FAIL  before -> FAIL (recovery, PM) ; after -> PASS (window OK on both): before=TODO BAD=[] after=TODO
      DECISION: adopt none (re-tune compensation)
2/28 passed
exit=1
```

> 순서 추천: `step_edges` → `zeta_from_lambda`/`phase_margin`(한 줄짜리) → `lobes` → `fit_decay` → `recovery_time` 으로 Part A를 통과시킨 뒤, `boxcar` · `select_free_decay` 로 Part B, `judge` 로 Part C. starter의 `boxcar` 는 아무것도 안 하고 `select_free_decay` 는 모든 로브를 돌려준다 — 즉 **starter의 기본 동작이 곧 "순진한 방법"** 이다.

## 4. 힌트

<details>
<summary>힌트 1 — 전류 채널부터: 스텝 시작은 전압이 아니라 전류로</summary>

전압 파형에서 "droop가 시작된 시점"을 찾으면 잡음·리플 때문에 들쭉날쭉하다. 전류 채널은 단조 램프라 10/90 % 교차가 깨끗하다. 사다리꼴이면 0→10 %는 램프의 1/8 (10 %/80 %)이므로 `t_start = t10 − (t90 − t10)/8`, `t_end = t90 + (t90 − t10)/8`. 앞 5 %(= 12.5 µs, 스텝 전)를 i0, 뒤 25 %를 i1로 쓴다 — 앞 10 %로 하면 레코드가 −20 µs에서 시작하므로 +5 µs까지, 즉 스텝 후 구간이 섞인다.

</details>

<details>
<summary>힌트 2 — 감쇠 진동의 극값 간격은 정확히 π/ωd</summary>

자유감쇠 e(t) = A·e^(−αt)·sin(ωd·t + φ) 를 미분하면 A·e^(−αt)·[ωd·cos(·) − α·sin(·)] = A'·e^(−αt)·cos(ωd·t + φ + ψ) 꼴이다. 0이 되는 시각은 위상과 상관없이 π/ωd 간격이고, 이웃 극값의 크기 비는 정확히 e^(−α·π/ωd) = e^(−λ), λ = πζ/√(1−ζ²). **단 램프가 끝난 뒤(자유감쇠 구간)에서만** 성립한다.

</details>

<details>
<summary>힌트 3 — 로브 = 히스테리시스 상태기계 (HWLAB-B04의 에지 검출과 같은 구조)</summary>

상태 +1이면 `e < −h` 가 될 때까지 같은 로브다. 그동안 `s·e` 가 가장 큰 샘플 jp를 기억한다. `e < −h` 가 되는 순간 로브를 닫고, 뒤로 걸어가며 `s·e[m] > 0` 인 마지막 샘플 m을 찾아 m, m+1 사이의 0 교차를 보간한다 — 이게 다음 로브의 t_zc. h = 0이면 잡음이 0 근처에서 로브를 수백 개 만든다.

</details>

<details>
<summary>힌트 4 — 왜 ln|a| 에 가중치 a² 인가</summary>

진폭 잡음이 σ 로 일정하면 var(ln a) ≈ (σ/a)². 작은 로브의 로그 값은 잡음이 크다 → 가중 최소제곱의 최적 가중치는 1/var = a²/σ². 가중치 없이 맞추면 문턱 근처 로브 하나가 기울기를 흔든다. `lsq_line(k, ln|a|, ws=[a²])` 의 기울기가 −λ.

</details>

<details>
<summary>힌트 5 — 주기는 0 교차로, 진폭은 꼭짓점으로</summary>

시간 측정 오차 ≈ 전압 잡음 / 기울기. 0 교차는 기울기가 최대(Aωd)라 시간이 정밀하고, 꼭짓점은 기울기가 0이라 시간은 흐릿하지만 값(진폭)은 1차 오차가 없다. 그래서 Td는 t_zc의 직선 기울기, λ는 꼭짓점 진폭으로. 교차가 2개 미만(ζ 큼)이면 꼭짓점 시각으로 후퇴하고 정밀도가 떨어짐을 리포트에 남긴다(`Td_src`).

</details>

<details>
<summary>힌트 6 — PM 정확식 유도</summary>

L(jω) = ωn² / (jω(jω + 2ζωn)). |L| = 1 → ω⁴ + 4ζ²ωn²ω² − ωn⁴ = 0 → ωc² = ωn²(√(1+4ζ⁴) − 2ζ²). ∠L(jωc) = −90° − atan(ωc/(2ζωn)) 이므로 PM = 180° + ∠L = 90° − atan(ωc/(2ζωn)) = atan(2ζωn/ωc).

</details>

## 5. 풀이

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m02-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">analyze() 파이프라인 — 각 단계를 끄면 '순진한 방법'이 된다 (Part B ablation)</text>
  <g stroke="currentColor">
    <rect x="10" y="38" width="170" height="70" rx="8" class="f-mut"/>
    <rect x="200" y="38" width="170" height="70" rx="8" class="f-acc"/>
    <rect x="390" y="38" width="170" height="70" rx="8" class="f-acc"/>
    <rect x="580" y="38" width="170" height="70" rx="8" class="f-acc"/>
    <rect x="10" y="150" width="170" height="80" rx="8" class="f-acc"/>
    <rect x="200" y="150" width="170" height="80" rx="8" class="f-acc"/>
    <rect x="390" y="150" width="170" height="80" rx="8" class="f-acc"/>
    <rect x="580" y="150" width="170" height="80" rx="8" class="f-ok"/>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m02-arr)">
    <line x1="180" y1="73" x2="198" y2="73"/><line x1="370" y1="73" x2="388" y2="73"/><line x1="560" y1="73" x2="578" y2="73"/>
    <path d="M665 108 L665 128 L95 128 L95 148"/>
    <line x1="180" y1="190" x2="198" y2="190"/><line x1="370" y1="190" x2="388" y2="190"/><line x1="560" y1="190" x2="578" y2="190"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="60">CSV: time, vout, iload</text><text x="20" y="78">20 MS/s, −20…230 µs</text><text x="20" y="96">스펙 JSON</text>
    <text x="210" y="60">step_edges(iload)</text><text x="210" y="78">10/90 % → slew</text><text x="210" y="96">t_start, t_end (램프 끝)</text>
    <text x="400" y="60">boxcar, n = 1/(fsw·dt)</text><text x="400" y="78">fsw 와 모든 고조파 = 0</text><text x="400" y="96">[filt]</text>
    <text x="590" y="60">기준선 v_final</text><text x="590" y="78">= 기록 뒤 25 % 평균</text><text x="590" y="96">[baseline] e = vf − v_final</text>
    <text x="20" y="172">lobes(e): 히스테리시스</text><text x="20" y="190">±h = ±3σ, 꼭짓점 2차 LS,</text><text x="20" y="208">로브 시작 0 교차 t_zc</text><text x="20" y="224">[hyst]</text>
    <text x="210" y="172">select_free_decay</text><text x="210" y="190">t_pk &gt; t_end (자유감쇠)</text><text x="210" y="208">|a| ≥ 6σ, 처음 미달에서 멈춤</text><text x="210" y="224">[free_decay]</text>
    <text x="400" y="172">fit_decay</text><text x="400" y="190">WLS ln|a_k| → λ → ζ</text><text x="400" y="208">LS t_zc(또는 t_pk) → Td</text><text x="400" y="224">로브 1개면 ζ 하한</text>
    <text x="590" y="172">phase_margin(ζ)</text><text x="590" y="190">recovery_time(±1 %)</text><text x="590" y="208">judge(spec) → PASS /</text><text x="590" y="224">FAIL / INVALID + 1줄</text>
  </g>
</svg>
```

### Part A — 모델에서 측정까지

**1) 모델이 무엇을 대표하나.** 레귤레이터 + 루프 + 출력 커패시터를 출력 단자에서 본 임피던스는 다음 병렬 RLC와 같은 모양이다.

| 모델 소자 | 회로/루프에서의 의미 | 이 레슨 값 (보상 전) |
|---|---|---|
| L_eq = 1/(C·ωn²) | 루프 이득이 큰 저주파에서 Zout ≈ jω·L_eq (적분기가 있는 루프는 DC에서 Zout → 0) | 50 nH |
| C | 유효 출력 용량 (MLCC DC-bias 감소 후, 폴리머 포함) | 220 µF |
| R = Z0/(2ζ) | 감쇠 — 위상 마진이 클수록 작다 | 50.2 mΩ (= 피크 높이) |
| fn = 1/(2π√(L_eq·C)) | 공진 ≈ 루프 교차 주파수 | 48 kHz (교차 46.9 kHz) |
| Z0 = 1/(ωn·C) | 특성 임피던스, "droop ≈ ΔI·Z0" 의 Z0 | 15.07 mΩ |

전류 계단 ΔI 에 대한 응답은 V(s) = Zout(s)·ΔI/s = (ΔI/C)/(s² + 2ζωn·s + ωn²) → **h(t) = ΔI·e^(−αt)·sin(ωd·t)/(C·ωd)** (α = ζωn, ωd = ωn√(1−ζ²)). 기울기 S 의 램프는 그 적분 H(t) = [ωd − e^(−αt)(α·sin ωd·t + ωd·cos ωd·t)]/(ωn²·C·ωd) 에 S를 곱한 것이고, 사다리꼴은 v = −S·[H(t) − H(t − tr)]. H(∞) = 1/(C·ωn²) = L_eq 라서 두 항이 상쇄돼 최종 편차 0 (적분기 → DC 오차 0). gen_capture.py가 이 해석식으로 캡처를 만든다.

이 모델에서 바로 나오는 손계산 세 개:

| 손계산 | 식 | 값 (보상 전) | 측정/모델 |
|---|---|---|---|
| droop 1차 근사 | ΔI·Z0 = ΔI/(2π·fn·C) | 2.5 × 15.07 mΩ = 37.7 mV | 이상적 계단 30.4 mV (ζ 보정 ×0.806), 1 A/µs 29.66 mV |
| 램프 중 오프셋 | L_eq·di/dt | 50 nH × 0.1 A/µs = 5 mV | 0.1 A/µs droop 8.10 mV = 5·(1 + e^(−λ)) = 5 × 1.621 |
| Zout 피크 | Z0·Q = Z0/(2ζ) | 15.07/0.3 = 50.2 mΩ | 연습 3: 50.24 mΩ @ 48.0 kHz |

"droop ≈ ΔI/(2π·fc·Cout)" 는 파워 쪽 앱노트에서 흔히 쓰는 근사인데, ζ 에 따른 보정 계수(이상 계단에서 exp(−ζ/√(1−ζ²)·atan(√(1−ζ²)/ζ)))와 slew 효과를 빼먹은 상한 쪽 값이다.

**2) 로그 감쇠율 → ζ.** 힌트 2대로 자유감쇠 로브의 이웃 비는 정확히 e^(−λ), λ = πζ/√(1−ζ²) (반주기 감쇠율; 한 주기 δ = 2λ 를 쓰면 ζ = δ/√(4π² + δ²) — 같은 식). 역으로 **ζ = λ/√(π² + λ²)**. 로브가 여러 개면 두 개만 쓰지 말고 ln|a_k| 를 k 에 직선으로 맞춘다 (힌트 4의 가중치). ζ = 0.15 면 이웃 비 0.621, ζ = 0.55 면 0.126 — ζ가 크면 두 번째 로브가 금방 잡음에 묻힌다.

**3) 꼭짓점 2차 최소제곱 (`quad_vertex`).** 로브 최대 샘플 주변에서 |e| ≥ 50 % 인 연속 샘플들로 y = c0 + c1·x + c2·x² 를 정규방정식(3×3, 크라메르)으로 풀고 꼭짓점 (−c1/2c2, c0 − c1²/4c2) 을 쓴다. 감쇠 사인파를 포물선으로 맞추는 건 모양 오차가 있지만, **창을 "최댓값의 50 %"처럼 상대적으로 정하면 모든 로브가 같은 모양(서로 스케일·시간 이동한 것)이라 비율 오차가 상쇄** 된다 — 깨끗한 모델에서 ζ 0.10 → 0.1000, 0.55 → 0.5499 가 그 확인이다. 1 fsw 박스카(1 µs)는 48 kHz 링잉을 sin(π·0.048)/(π·0.048) = 0.9962 배로 줄이지만 모든 로브에 같은 배율이라 λ에는 영향이 없다.

**4) 0 교차와 꼭짓점의 역할 분담 (힌트 5).**

| 양 | 쓰는 특징점 | 이유 | 실패 조건 |
|---|---|---|---|
| Td (주기) | 0 교차 t_zc의 직선 기울기 × 2 | 기울기 최대 → 시간 오차 σ/(A·ωd) 최소 | 교차 < 2개 (ζ ≥ ~0.5) → 꼭짓점 시각, 정밀도 ↓ |
| λ, ζ | 꼭짓점 진폭 a_k | 기울기 0 → 진폭에 1차 시간 오차 없음 | 로브 1개 → 하한만 |
| droop | 필터 출력 최소값 − 스텝 전 값 | 정의 그대로 (스펙 window는 원시 캡처로 따로) | 램프 중 최솟값이어도 그대로 droop |
| recovery | ±band 밖이었던 마지막 샘플 → 경계 보간 | HWLAB-B04 연습 1의 settling과 같은 정의 | 리플 > band 면 원시 파형으로는 영원히 미복귀 |

**5) ζ → PM.** 힌트 6의 유도: **PM = atan(2ζ/√(√(1+4ζ⁴) − 2ζ²))**. 역함수는 ζ = tan(PM)·√(cos PM)/2 (연습 1) — Erickson & Maksimović의 2차 근사 Q = √(cos φm)/sin φm 과 Q = 1/(2ζ) 로 같은 식이다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">감쇠비 ζ → 위상 마진: 2차 루프 정확식(실선) vs PM ≈ 100·ζ (점선)</text>
  <rect x="80" y="50" width="360" height="230" class="f-ok" opacity=".18"/>
  <rect x="440" y="50" width="240" height="230" class="f-warn" opacity=".18"/>
  <g stroke="currentColor" opacity=".35">
    <line x1="80" y1="280" x2="680" y2="280"/><line x1="80" y1="165" x2="680" y2="165"/><line x1="80" y1="50" x2="680" y2="50"/>
    <line x1="80" y1="50" x2="80" y2="280"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.2" points="86.0,277.1 92.0,274.1 98.0,271.2 104.0,268.3 110.0,265.4 116.0,262.5 122.0,259.5 128.0,256.6 134.0,253.7 140.0,250.8 146.0,247.9 152.0,245.0 158.0,242.1 164.0,239.3 170.0,236.4 176.0,233.6 182.0,230.7 188.0,227.9 194.0,225.0 200.0,222.2 206.0,219.4 212.0,216.7 218.0,213.9 224.0,211.1 230.0,208.4 236.0,205.7 242.0,203.0 248.0,200.3 254.0,197.6 260.0,195.0 266.0,192.3 272.0,189.7 278.0,187.2 284.0,184.6 290.0,182.1 296.0,179.6 302.0,177.1 308.0,174.6 314.0,172.2 320.0,169.8 326.0,167.4 332.0,165.1 338.0,162.8 344.0,160.5 350.0,158.3 356.0,156.1 362.0,153.9 368.0,151.7 374.0,149.6 380.0,147.6 386.0,145.5 392.0,143.5 398.0,141.5 404.0,139.6 410.0,137.7 416.0,135.8 422.0,134.0 428.0,132.2 434.0,130.5 440.0,128.7 446.0,127.1 452.0,125.4 458.0,123.8 464.0,122.2 470.0,120.7 476.0,119.2 482.0,117.7 488.0,116.3 494.0,114.9 500.0,113.5 506.0,112.1 512.0,110.8 518.0,109.6 524.0,108.3 530.0,107.1 536.0,105.9 542.0,104.8 548.0,103.6 554.0,102.5 560.0,101.5 566.0,100.4 572.0,99.4 578.0,98.4 584.0,97.4 590.0,96.5 596.0,95.6 602.0,94.7 608.0,93.8 614.0,93.0 620.0,92.1 626.0,91.3 632.0,90.5 638.0,89.8 644.0,89.0 650.0,88.3 656.0,87.6 662.0,86.9 668.0,86.2 674.0,85.5 680.0,84.9"/>
  <line x1="80" y1="280" x2="620" y2="50" stroke="currentColor" stroke-dasharray="6 4"/>
  <g stroke="currentColor"><circle cx="170.0" cy="236.3" r="4" class="f-warn"/><circle cx="410.0" cy="137.7" r="4" class="f-ok"/>
    <circle cx="332.2" cy="165.0" r="4" class="f-acc"/></g>
  <g fill="currentColor" font-size="12">
    <text x="178.0" y="252.3">보상 전 ζ=0.15 → 17°</text>
    <text x="418.0" y="153.7">보상 후 ζ=0.55 → 56°</text>
    <text x="182.2" y="157.0">PM 45° ⇔ ζ = 0.420 (100ζ 라면 0.45)</text>
    <text x="452.0" y="70">ζ &gt; 0.6: 100ζ 가 과대평가</text><text x="452.0" y="86">(ζ=0.8 → 80 vs 69.9°)</text>
    <text x="92.0" y="70">ζ ≤ 0.6: |100ζ − PM| ≤ 3.3°</text>
    <text x="50" y="284">0°</text><text x="44" y="169">45°</text><text x="44" y="54">90°</text>
    <text x="76" y="300">0</text><text x="192" y="300">0.2</text><text x="312" y="300">0.4</text><text x="432" y="300">0.6</text><text x="552" y="300">0.8</text><text x="666" y="300">1.0 ζ</text>
    <text x="10" y="322">정확식: L(s) = ωn²/(s(s+2ζωn)),  PM = atan(2ζ / √(√(1+4ζ⁴) − 2ζ²))</text>
  </g>
</svg>
```

가정을 꼭 말해야 한다: (1) 폐루프가 **우세 극점 한 쌍**으로 근사되고, (2) 개루프가 L(s) = ωn²/(s(s + 2ζωn)) 꼴(적분기 + 극점 하나)이며, (3) Zout 피크의 극점이 그 폐루프 극점과 같다는 것. 실제 벅 + type-II/III 보상은 영점·극점이 더 있고 ESR 영점, 전류 모드의 샘플링 효과(fsw/2 근처)도 있다 → 로드 트랜지언트 PM 추정은 **±10° 급 스크리닝**이고, 판정이 경계면 루프 이득을 직접 잰다(§5 Part C 장비).

`python3 -I solution.py` 의 Part A 실제 출력:

```text
PASS  step_edges: slew 1A/us +-0.5%, ramp start/end +-10ns   [slew=1.0000A/us t_start=-0.250us t_end=2.250us (truth 1.0, -0.250, 2.250us)]
PASS  zeta=0.10: zeta+-0.003, Td+-0.5%, droop+-0.15mV, recovery+-0.1us   [zeta 0.1000/0.10 | Td 20.928us/20.938us (zc) | droop 31.74/31.74mV | rec 49.620us/49.629us | lobes used 22]
PASS  zeta=0.25: zeta+-0.003, Td+-0.5%, droop+-0.15mV, recovery+-0.1us   [zeta 0.2500/0.25 | Td 21.517us/21.517us (zc) | droop 26.18/26.18mV | rec 19.462us/19.462us | lobes used 9]
PASS  zeta=0.40: zeta+-0.003, Td+-0.5%, droop+-0.15mV, recovery+-0.1us   [zeta 0.4000/0.40 | Td 22.731us/22.731us (zc) | droop 22.18/22.19mV | rec 10.375us/10.375us | lobes used 6]
PASS  zeta=0.55: zeta+-0.003, Td+-0.5%, droop+-0.15mV, recovery+-0.1us   [zeta 0.5499/0.55 | Td 24.945us/24.945us (zc) | droop 19.19/19.19mV | rec 10.197us/10.197us | lobes used 4]
PASS  phase_margin exact == numeric |L(jw)|=1 bisection (+-0.01deg)   [z0.1:11.42/11.42 z0.3:33.27/33.27 z0.5:51.83/51.83 z0.7:65.16/65.16 z0.9:73.51/73.51]
```

눈여겨볼 것: ζ = 0.40 → 0.55 에서 recovery가 10.375 → 10.197 µs 로 거의 안 줄었다. ζ 를 키우면 링잉은 사라지지만 ±8 mV 밴드 기준으로는 "첫 droop에서 올라오는 시간"이 지배하고, ζ > 1(과감쇠)로 가면 오히려 느리게 기어 올라온다. **PM을 무작정 키우는 게 정답이 아니다** — 보통 45~70°를 목표로 한다.

### Part B — 측정이 거짓말하는 지점

| 원인 | 증상 | 드러내는 측정 / 고침 |
|---|---|---|
| 스위칭 리플 8 mVpp가 링잉 위에 | 1 µs마다 로브, 순진한 방법 로브 544개, ζ ≈ 0.001 | 1 fsw 박스카(정확히 n = 1/(fsw·dt)) — fsw와 모든 고조파의 영점 |
| 20 MHz BW limit로 리플 제거 시도 | 1 MHz 리플은 그대로 (BW limit은 스파이크용, HWLAB-B08 참고) | 박스카 또는 fsw 노치 |
| 기준선 = 스텝 전 값 (load-line 레일) | 편차가 5 mV 비대칭 → 양(+) 로브가 줄거나 사라짐, ζ 0.25 → 0.204 | 기준선 = 스텝 후 정착값 (기록 뒤 25 % 평균) |
| 느린 e-load (0.1 A/µs) | droop 7.63 mV (1 A/µs의 27 %), 링잉 여기 ↓, 쓸 수 있는 로브 6 → 2 | 전류 채널로 slew를 재서 시험 조건 검증 → 아니면 `INVALID` |
| 램프 중 로브를 감쇠 피팅에 포함 | 강제응답(L_eq·di/dt 오프셋) 위의 로브 → ζ 0.20 → 0.282 | t_pk > t_end 인 자유감쇠 로브만 |
| 히스테리시스 없음 (h = 0) | 0 근처 잡음이 작은 로브를 만들어 선택이 일찍 멈춤 → 보상 후 캡처에서 로브 1개, 하한이 **거짓 하한** | h = 3σ, thr = 6σ |
| ζ 가 큼 (≥ 0.7) | 두 번째 로브가 잡음 아래 → ζ 측정 불가 | λ ≥ ln(a0/thr) 로 ζ 하한만 보고, 필요하면 Bode 측정 |

**리플 제거는 왜 박스카인가.** 길이 T = n·dt 의 이동평균 전달함수는 |sin(πfT)/(πfT)|, T = 1/fsw 면 fsw, 2fsw, 3fsw… 에서 정확히 0이다. 삼각파·톱니파 리플처럼 고조파가 많은 파형도 한 주기 평균이 상수라 깨끗이 사라진다(테스트: n = 20 → 8.5e−18 V, n = 19 → 0.211 mV). 대가는 링잉 진폭 0.4 % 감소(위 3번 — 비율에는 무영향)와 1 µs보다 빠른 구조(ESL 스파이크)를 못 보는 것. 그래서 **window(Vmin/Vmax) 판정은 원시 캡처로, 루프 특성(ζ, Td, recovery)은 필터 출력으로** 나눠서 본다.

**느린 slew의 물리.** 램프 중에는 Zout의 저주파 부분이 인덕터처럼 보여 **v ≈ −L_eq·di/dt** 의 일정 오프셋 + 감쇠 진동이 생긴다. 램프가 첫 극값(≈ Td/2)보다 길면 droop ≈ L_eq·(di/dt)·(1 + e^(−λ)), 짧으면 이상적 계단의 ΔI·Z0·(ζ 보정) 쪽으로 간다. `python3 -I demo_sweep.py` 표 2 (실제 출력):

```text
  U12_before_comp (injected zeta=0.15 fn=48kHz)
    slew[A/us]  ramp[us]  droop[mV]  t_droop[us]  lobes  zeta_meas
       0.1      25.00       8.10      10.54        2    0.194
       0.2      12.50      16.20      10.54        5    0.152
       0.5       5.00      27.58       7.36        7    0.150
       1.0       2.50      29.66       6.03        8    0.150
       3.0       0.83      30.30       5.18        8    0.149
      10.0       0.25      30.37       4.89        8    0.149
  U12_after_comp (injected zeta=0.55 fn=36kHz)
    slew[A/us]  ramp[us]  droop[mV]  t_droop[us]  lobes  zeta_meas
       0.1      25.00      10.01      16.63        1    >=0.06
       0.2      12.50      18.93      13.18        2    0.557
       0.5       5.00      24.85       7.99        2    0.532
       1.0       2.50      25.86       6.54        2    0.534
       3.0       0.83      26.16       5.65        2    0.535
      10.0       0.25      26.20       5.36        2    0.535
```

0.1/0.2 A/µs 행이 손계산과 맞는다: 50 nH × 0.1 A/µs × 1.621 = 8.1 mV, × 0.2 = 16.2 mV (보상 후: 88.8 nH × 0.1 A/µs × 1.126 = 10.0 mV). 그리고 slew를 아무리 키워도(3, 10 A/µs) droop는 30.4 mV로 포화 — 그 위는 루프가 아니라 커패시터(와 ESR/ESL, 이 모델엔 없음) 영역이다. 실제 SoC의 di/dt는 수십~수백 A/µs급이라 **벤치 e-load의 느린 slew로 PASS 한 결과는 그 레일을 보증하지 못한다.**

**허용오차의 근거 — 잡음 시드 20개 몬테카를로** (demo 표 3, 실제 출력):

```text
  U12_before_comp truth zeta=0.15 Td=21.07us rec=29.86us
    zeta  mean 0.1500  sd 0.0007  min 0.1489  max 0.1512
    Td    err mean +0.21%  sd 0.41%  worst +1.32%
    rec   err mean -0.012us  sd 0.059us  worst -0.119us
    lobes used  min 7  max 8
  U12_after_comp truth zeta=0.55 Td=33.26us rec=14.11us
    zeta  mean 0.5502  sd 0.0061  min 0.5382  max 0.5623
    Td    err mean -0.55%  sd 1.14%  worst -2.73%
    rec   err mean -0.009us  sd 0.074us  worst -0.132us
    lobes used  min 2  max 2
```

그래서 테스트 허용오차를 ζ ±0.005(보상 전, ≈ 7σ) / ±0.03(보상 후, ≈ 5σ), Td ±2 % / ±6 %, recovery ±0.5 µs 로 잡았다. 보상 후는 로브가 2개뿐이라(교차 1개 → `Td_src=pk`) 주기 정밀도가 3배 나쁘다 — 리포트에 그 사실을 남기는 게 정직한 측정이다.

**Ablation — 스위치를 하나씩 끄면** (demo 표 4, Part C 두 캡처, 실제 출력):

```text
  U12_before_comp
    variant            lobes  used  zeta     PM[deg]  Td[us]   rec[us]  verdict
    robust (all on)       10     8  0.150    17.1     21.06    29.7     FAIL
    filt=False             6     2  0.149    16.9     20.88    50.9     FAIL
    hyst=False           284     8  0.150    17.1     21.06    29.7     FAIL
    free_decay=False      10    10  0.148    16.9     21.23    29.7     FAIL
    baseline=pre          11     7  0.151    17.1     21.02    29.8     FAIL
    naive (all off)      543   543  -0.012   -1.4     0.72     50.9     FAIL
  U12_after_comp
    variant            lobes  used  zeta     PM[deg]  Td[us]   rec[us]  verdict
    robust (all on)        4     2  0.536    54.7     33.12    14.1     PASS
    filt=False             2     1  None     None     None     26.6     FAIL
    hyst=False           368     1  None     None     None     14.1     PASS
    free_decay=False       4     4  0.502    52.0     36.11    14.1     PASS
    baseline=pre           3     2  0.543    55.1     33.07    14.1     PASS
    naive (all off)      599   599  -0.020   -2.3     0.72     26.6     FAIL
```

읽는 법: 보상 전 캡처(링잉 30 mV급, 로브 많음)는 웬만한 실수에도 ζ가 버틴다 — 깨끗한 데이터는 나쁜 알고리즘도 통과시킨다(HWLAB-B04의 교훈과 같다). 망가지는 건 **보상 후처럼 신호가 잡음에 가까울 때**다. `filt=False` 는 σ 가 리플(8 mVpp 삼각파 = 2.3 mV rms, 잡음 포함 실측 2.44 mV)로 커져 문턱이 14.6 mV가 되고, 원시 파형의 리플 때문에 recovery가 26.6 µs → 스펙 FAIL로 **멀쩡한 보상을 반려**한다. `hyst=False` 는 PASS지만 이유가 틀렸다 — 잡음 로브 때문에 로브 1개만 남아 ζ 하한(거짓 하한)으로 통과했다. `naive` 는 Td = 0.72 µs, 즉 1 µs 스위칭 리플·잡음 교차를 링잉으로 보고 PM을 음수로 낸다.

### Part C — 판정과 행동 가능한 리포트

`python3 -I solution.py` 의 Part C 실제 출력:

```text
VDD_CORE_0V8 U12_before_comp -> FAIL  droop=29.8mV ring=47.5kHz Td_src=zc lobes=8 sigma=0.18mV
  ok   slew      = 1.008A/us              1.00+-10%
  ok   Vmin      = 0.7653V                >=0.760V
  ok   Vmax      = 0.8235V                <=0.840V
  BAD  recovery  = 29.7us                 <=25us (+-1%)
  BAD  PM        = 17.1deg (zeta=0.150)   >=45deg
VDD_CORE_0V8 U12_after_comp -> PASS  droop=25.8mV ring=30.2kHz Td_src=pk lobes=2 sigma=0.21mV
  ok   slew      = 0.980A/us              1.00+-10%
  ok   Vmin      = 0.7699V                >=0.760V
  ok   Vmax      = 0.8083V                <=0.840V
  ok   recovery  = 14.1us                 <=25us (+-1%)
  ok   PM        = 54.7deg (zeta=0.536)   >=45deg
...
      FAIL VDD_CORE_0V8 U12_before_comp: zeta=0.150 PM=17deg(100z:15) droop=29.8mV rec=29.7us BAD=recovery,PM
      PASS VDD_CORE_0V8 U12_after_comp: zeta=0.536 PM=55deg(100z:54) droop=25.8mV rec=14.1us
      DECISION: adopt U12_after_comp
```

주입한 참값과 비교: 보상 전 ζ 0.1504 (참 0.15), Td 21.065 µs (참 21.072), recovery 29.748 µs (참 29.862); 보상 후 ζ 0.5364 (참 0.55 → PM 55.7°), Td 33.12 µs (참 33.26), recovery 14.083 µs (참 14.113).

**리포트를 읽는 법 (파워 엔지니어에게 넘길 문장):**

- `FAIL VDD_CORE_0V8 U12 before_comp: ζ=0.150 → PM≈17° (2차 근사; 100ζ=15°) < 45°, ring 47.5 kHz (≈ fc), ±1% recovery 29.7 µs > 25 µs; window OK (Vmin 765.3 mV ≥ 760). after_comp: ζ=0.536 → PM≈55°, recovery 14.1 µs, droop 25.8 mV → PASS. 보상 변경 채택, 단 Bode 측정으로 PM 확인 + Vin/온도/부하 코너 재시험 요청.`
- **window는 둘 다 통과했다는 게 핵심이다.** 보상 전 캡처는 "droop 29.8 mV, ±5 % 안" 이라 window만 보면 PASS다. 하지만 PM 17°는 부품 편차·온도·DC-bias로 Cout이 줄거나 fc가 움직이면 발진 직전까지 갈 수 있는 마진이고, Zout 피크 50 mΩ(Q 3.3)는 48 kHz 근처로 반복되는 부하(예: SoC 워크로드가 그 주기로 on/off)에서 스텝보다 훨씬 큰 리플을 만든다 (0.5↔3 A 사각파면 기본파 진폭 (4/π)·1.25 A ≈ 1.59 A → 50 mΩ × 1.59 A ≈ 80 mV 진폭, 손계산).
- droop가 작아진 것도 우연이 아니다: Zout 피크(공진)가 낮아지면 스텝 응답의 첫 극값도 작아진다 (ζ 보정 계수 0.806 → 0.52). "PM을 올리면 droop가 커진다" 는 교차 주파수를 같이 낮출 때의 이야기이고 (여기도 fc 46.9 → 27 kHz로 낮췄다 — Z0가 15.1 → 20.1 mΩ로 커졌지만 감쇠가 그보다 더 이득), 두 효과를 분리해서 말할 수 있어야 한다.
- 시험 조건 검증이 먼저다: 리포트 첫 줄이 slew다. e-load가 0.1 A/µs로 설정돼 있었다면 같은 보드도 droop 8 mV로 "아주 좋아" 보인다 — 그래서 slew가 ±10 % 밖이면 `INVALID` (테스트 마지막 항목).
- 측정 체인도 적는다: 스프링 팁(15 cm 그라운드 리드는 수백 MHz 링잉을 만든다, HWLAB-B05 참고), 측정점(SoC 볼 근처 TP40 — 레귤레이터 출력 단자에서 재면 PCB/패키지 임피던스가 빠진다), 1 fsw 박스카, 20 MS/s.

**장비가 있을 때 (이 레슨에선 실행하지 않음):**

- 부하 스텝: 보드 위 MOSFET + 저항(함수발생기로 게이트 구동, 게이트 저항으로 slew 조절)이나 전용 load-step 주입기. 벤치 e-load는 slew와 케이블 인덕턴스 때문에 µs급 이하 에지가 어렵다. 전류는 전류 프로브나 FET 소스 저항 전압으로 같은 스코프에서 잡는다 — 그래야 `step_edges` 가 가능하다.
- 전압: 수 mV를 보려면 1x 프로브나 전원 레일 전용 프로브(예: Keysight N7020A, Tektronix TPR 시리즈 — 큰 DC 오프셋 + 낮은 잡음), 스프링 팁. 레일 리플 측정 일반은 HWLAB-B08 참고.
- 루프 이득 직접 측정(확인용): 피드백 분압기 위쪽에 수 Ω~수십 Ω 주입 저항을 넣고 주입 트랜스포머(예: OMICRON Lab B-WIT 100, Picotest J2100A/J2101A)로 신호를 넣어 주파수 응답 분석기(예: OMICRON Lab Bode 100)로 T(f) = V_return/V_inject 를 잰다 → PM, GM, fc. 피드백 루프를 끊을 수 없는 보드(모듈, PMIC 내부 분압)에서는 Zout 피크의 Q로 PM을 추정하는 비침습 방식(Picotest NISM 계열)이나 이 레슨의 로드 트랜지언트 방식이 대안이다.
- 디지털 PWM 컨트롤러(PMBus)는 보상 계수와 응답 설정을 레지스터로 바꾸는 제품이 많다 — 펌웨어/검증 엔지니어가 "보상 전/후" 를 스크립트로 스윕하고 이 분석기를 돌려 표로 만드는 게 실제 bring-up 워크플로다. 단, PMBus `READ_VOUT` 텔레메트리는 평균된 느린 값이라 µs 과도응답은 못 본다 — 스코프가 필요하다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"                     # 28/28 passed, exit=0  (약 1.1 s)
python3 -I solution.py --report U12_before_comp.csv         # 캡처 하나만 리포트
python3 -I -W error -X dev solution.py                       # 경고 없음 (exit 0)
python3 -I gen_capture.py                                    # CSV 재생성 → shasum 동일 (결정적)
python3 -I demo_sweep.py                                     # §5 의 표 1–4 (약 2 s)
```

<!-- code: demo_sweep.py -->

demo 표 1 (ζ → PM, 실제 출력):

```text
  zeta  ratio   lambda   Q      PM_exact  100z   diff
  0.05  0.854   0.157  10.00      5.7      5   -0.7
  0.10  0.729   0.316   5.00     11.4     10   -1.4
  0.15  0.621   0.477   3.33     17.1     15   -2.1
  0.20  0.527   0.641   2.50     22.6     20   -2.6
  0.30  0.372   0.988   1.67     33.3     30   -3.3
  0.40  0.254   1.371   1.25     43.1     40   -3.1
  0.50  0.163   1.814   1.00     51.8     50   -1.8
  0.55  0.126   2.069   0.91     55.7     55   -0.7
  0.60  0.095   2.356   0.83     59.2     60   +0.8
  0.70  0.046   3.079   0.71     65.2     70   +4.8
  0.80  0.015   4.189   0.62     69.9     80  +10.1
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 스텝 시작을 전압 파형에서 찾음 | 잡음·리플 때문에 들쭉날쭉, recovery 기준점이 흔들림 | 전류 채널 10/90 % → `t_start = t10 − dur/8` |
| i0를 레코드 앞 10 %로 | 프리트리거가 8 %뿐이면 스텝 후 구간이 섞여 t_start가 0.4 µs 밀림 (실제로 겪은 버그) | 프리트리거 안쪽(앞 5 %)만 |
| 원시 파형으로 로브·recovery | 8 mVpp 리플이 1 µs마다 로브, ±8 mV 밴드를 리플이 들락날락 → recovery 50.9 µs | `boxcar(n = 1/(fsw·dt))` |
| 박스카 길이를 대충 (n = 19) | fsw의 영점에서 벗어나 0.211 mV 잔류 | n = round(1/(fsw·dt)), 샘플레이트를 fsw 정수배로 |
| 기준선 = 스텝 전 값 | load-line·DC 오차가 있으면 편차가 비대칭 → ζ 0.25 → 0.204 | 스텝 후 정착값 |
| 램프 중 로브까지 피팅 | 강제응답 오프셋 위의 로브 → ζ 0.20 → 0.282 | `t_pk > t_end` |
| 두 로브만으로 ζ (첫 droop/첫 overshoot) | 문턱 근처 로브 하나가 결과를 좌우, 램프 중 droop면 편향 | 여러 로브 가중 LS, 개수 리포트 |
| 히스테리시스 없이 0 교차 | 잡음 로브가 선택을 끊어 거짓 하한 / 주기 오류 | h = 3σ, thr = 6σ |
| ζ 측정 불가인데 "링잉 없음 = 안정" | 문턱 아래일 뿐 — 하한만 안다 | `zeta_min`, 필요하면 Bode |
| 모든 ζ에 PM = 100·ζ | ζ = 0.8에서 80° vs 69.9°, 경계(45°) 근처 ζ 0.42 vs 0.45 | 정확식 + 근사를 함께 표기 |
| e-load slew 미확인 | 0.1 A/µs면 droop 27 %만 보이고 로브 6 → 2 | slew를 재서 `INVALID` 판정 |
| window를 필터 출력으로 판정 | SoC가 보는 건 리플·잡음 포함 순시값 | window는 원시 캡처 Vmin/Vmax |

## 8. 면접 Q&A

<details>
<summary>Q1. load step 캡처 하나로 루프 안정성을 어떻게 판단하나요? 그 방법의 가정은?</summary>

링잉의 이웃 극값 비로 반주기 감쇠율 λ 를 구해 ζ = λ/√(π² + λ²), 2차 루프로 가정하고 PM = atan(2ζ/√(√(1+4ζ⁴) − 2ζ²)) (대략 100ζ). 링잉 주파수는 대략 교차 주파수다. 가정: 폐루프가 우세 극점 한 쌍으로 근사되고, 그 극점이 Zout에서 보이며, 스텝이 선형 영역(포화·전류 제한·모드 전환 없음)이고, 자유감쇠 구간에서 쟀다는 것.
꼬리질문: "그럼 Bode 측정은 왜 하나?" → 이 방법은 ±10° 급 스크리닝이다. type-III 보상의 영점·극점, 전류 모드의 fsw/2 효과, 이득 마진(GM)은 안 보인다. 판정이 경계이거나 양산 사인오프면 주입 트랜스포머로 루프 이득을 직접 재서 PM·GM·fc 를 확인한다.

</details>

<details>
<summary>Q2. PM ≈ 100·ζ 는 언제 맞고 언제 틀리나요?</summary>

2차 루프 정확식과 비교하면 ζ ≤ 0.6 에서 3.3° 이내(그 구간에선 100ζ 가 약간 작게 나와 보수적), ζ = 0.7 에서 +4.8°, 0.8 에서 +10° 과대평가다. 스펙 경계 45°는 정확식으로 ζ = 0.420, 근사로는 0.45 — 0.43짜리 측정은 근사로 FAIL, 정확식으로 PASS다. 리포트엔 두 값과 쓴 식을 함께 적는다.
꼬리질문: "Q로 말하면?" → Zout 피크 Q = 1/(2ζ). Erickson의 근사 Q = √(cos φm)/sin φm 와 같다. PM 45° ↔ Q ≈ 1.19, PM 60° ↔ Q ≈ 0.82.

</details>

<details>
<summary>Q3. droop 크기는 무엇이 결정하나요? 줄이려면?</summary>

1차 근사 ΔV ≈ ΔI/(2π·fc·Cout) = ΔI·Z0. 여기에 ζ 보정(이상 계단에서 ζ = 0.15면 ×0.81, 0.55면 ×0.52), 에지 순간의 ESR·ESL 스텝(ΔI·ESR + ESL·di/dt — 이 레슨 모델엔 없음, HWLAB-M03의 PDN 범위), slew가 붙는다. 줄이는 법: fc를 올린다(PM·fsw/fc 비율 한계), Cout을 늘린다(같은 fc면 Z0 ↓), 부하 디커플링·패키지 커패시턴스로 고주파를 맡긴다, 전류 모드/피드포워드/비선형 제어(일부 컨트롤러의 transient 부스트), 또는 AVP로 window를 대칭으로 쓴다.
꼬리질문: "PM을 올렸더니 droop가 줄었다, 왜?" → Zout 공진 피크가 낮아져서다(Q ↓). 반대로 PM을 올리려고 fc를 낮추면 Z0 가 커져 droop가 는다. 이 레슨의 보상 후 캡처는 두 효과 중 감쇠 쪽이 이겨서 29.8 → 25.8 mV.

</details>

<details>
<summary>Q4. 벤치 e-load로 0.1 A/µs 스텝을 했더니 droop 8 mV로 아주 좋습니다. 사인오프해도 되나요?</summary>

안 된다. 램프 중엔 Zout이 L_eq 처럼 보여 오프셋이 L_eq·di/dt 뿐이다 — 이 보드(L_eq 50 nH)면 0.1 A/µs에서 5 mV × 1.62 = 8.1 mV, 1 A/µs에서 29.7 mV, 실제 SoC의 di/dt(수십 A/µs 이상)에선 계단 응답 쪽 30.4 mV + ESR/ESL 스파이크다. 게다가 느린 스텝은 링잉을 덜 때려서 같은 잡음에서 쓸 수 있는 로브가 6 → 2로 줄어 안정성 판정도 약해진다. 시험 조건(slew)을 전류 채널로 재서 스펙과 다르면 결과를 무효로 한다.
꼬리질문: "그럼 e-load는 쓸모없나?" → DC 레귤레이션·load line·효율·열 시험엔 좋다. 빠른 스텝은 보드 위 FET 스텝이나 전용 주입기로.

</details>

<details>
<summary>Q5. 1 MHz 스위칭 리플이 링잉 위에 얹혀 있습니다. 스코프 20 MHz BW limit을 켜면 되나요?</summary>

아니다. 20 MHz BW limit은 1 MHz 리플을 거의 그대로 통과시킨다 — 수백 MHz 스위칭 스파이크와 프로브 잡음을 깎는 용도다(HWLAB-B08). 링잉(수십 kHz)과 리플(1 MHz)을 가르려면 정확히 한 스위칭 주기 길이의 이동평균(박스카): 전달함수 sinc(f·Tsw)가 fsw의 모든 고조파에서 0이고 48 kHz에선 0.996이라 링잉은 거의 그대로다. 스코프에선 샘플레이트를 fsw 정수배로 맞추고 후처리하거나, 일부 스코프의 smoothing/boxcar 수학 함수를 쓴다.
꼬리질문: "fsw가 스프레드 스펙트럼으로 흔들리면?" → 고정 길이 박스카의 영점이 비껴 잔류가 생긴다. 창을 2~3 주기로 늘리거나(링잉 감쇠도 같이 커지니 보정), 전압을 fsw에 동기된 샘플(스위칭 주기마다 같은 위상)로 다운샘플한다.

</details>

<details>
<summary>Q6. 링잉이 전혀 안 보입니다(로브 1개). 안정하다고 해도 되나요?</summary>

"두 번째 로브가 잡음 문턱 아래"라는 뜻이라 하한만 말할 수 있다: λ ≥ ln(a0/thr) → 이 레슨의 ζ = 0.8 캡처는 ζ ≥ 0.696, PM ≥ 64.9°. 이 하한은 로브 선택이 옳을 때만 유효하다 — 히스테리시스 없이 잡음 로브가 선택을 끊으면 거짓 하한이 나온다(ablation `hyst=False`). 하한이 스펙보다 높으면 PASS로 쓰되 "bound" 를 명시하고, 아니면 더 큰 스텝(신호 ↑), 평균(반복 스텝 + 트리거 평균으로 잡음 ↓), 또는 Bode로 간다.
꼬리질문: "너무 큰 PM은 문제 없나?" → 과감쇠(ζ > 1 근처)면 링잉은 없지만 천천히 기어 올라와 recovery가 길어진다. 이 레슨의 ζ 0.40 → 0.55에서 recovery가 10.38 → 10.20 µs로 거의 안 준 것이 그 시작이다.

</details>

<details>
<summary>Q7. CPU 코어 레일처럼 load-line(AVP)이 있는 레일에선 무엇이 달라지나요?</summary>

의도적으로 Vout = Vnom − R_LL·I 로 정착하므로 스텝 후 최종값이 스텝 전보다 R_LL·ΔI 낮다(2 mΩ × 2.5 A = 5 mV). 기준선을 스텝 전 값으로 잡으면 편차가 비대칭이 돼 양(+) 로브가 줄거나 사라지고 ζ가 틀린다(0.25 → 0.204). 정착값을 기준선으로 쓰면 0.2465. window 판정은 load-line 스펙(전류별 허용 밴드)으로 해야 하고, AVP의 목적 자체가 같은 window에서 스텝 편차를 양방향으로 쓰는 것이다.
꼬리질문: "load-line 레일의 이상적 Zout은?" → 넓은 대역에서 R_LL 로 평탄한 저항성 Zout. 그러면 스텝 응답이 링잉 없이 계단(ΔI·R_LL) 모양이 된다 — 공진 피크가 있으면 그 위에 링잉이 얹힌다.

</details>

<details>
<summary>Q8. 펌웨어/검증 엔지니어로서 이 측정을 어떻게 자동화하나요?</summary>

PyVISA/SCPI로 스코프를 전류 채널 상승 에지에 트리거, 세 채널을 받아(프리앰블로 스케일 변환, HWLAB-B04 참고) `analyze()` 에 넣는다. PMBus 디지털 컨트롤러면 보상 설정·Vout·fsw를 레지스터로 스윕하고, 부하 FET는 GPIO/함수발생기로 구동, Vin/온도 코너(챔버)까지 매트릭스로 돌려 ζ/PM/droop/recovery 표를 만든다. 결과 리포트에 측정 조건(slew, 측정점, 프로브, 필터, 문턱)과 `Td_src`, ζ 하한 여부를 남긴다. 텔레메트리 `READ_VOUT` 는 평균값이라 과도응답 판정에 쓸 수 없다.

</details>

## 9. 추가 연습

### 연습 1 — PM → ζ 역변환

`zeta_from_pm(pm_deg)` 를 정확식의 역함수로 작성하고 `phase_margin` 과 왕복(ζ 0.05~0.95)이 1e−9 이내인지, PM 45°/60°가 각각 ζ 몇인지 구한다.

<details>
<summary>답 보기</summary>

tan PM = 2ζ/√(√(1+4ζ⁴) − 2ζ²) 를 정리하면 ζ = tan(PM)·√(cos PM)/2 = sin PM/(2√(cos PM)). 실제 출력: `45deg->0.4204 60deg->0.6124`. 스펙 "PM ≥ 45°" 는 측정 ζ ≥ 0.420 과 같다(100ζ 근사로 환산하면 0.45 — 0.03만큼 엄격해진다). 판정 코드는 ζ 를 PM으로 바꾸는 것보다 스펙 PM을 ζ 문턱으로 바꿔 두는 편이 단순하다.

</details>

### 연습 2 — 부하 해제(release) 스텝

같은 보상 전 보드에서 3 → 0.5 A (1 A/µs) 해제 스텝을 합성해 `analyze` 가 하강 전류 에지·overshoot(위쪽 편차)·ζ 를 그대로 처리하는지 확인한다. overshoot는 모델 참값 ±0.5 mV, ζ ±0.005.

<details>
<summary>답 보기</summary>

`step_edges` 는 부호 s 로 하강 에지도 같은 규칙(10 %를 마지막으로 떠남 → 90 %에 처음 닿음)으로 잡고, `analyze` 는 `sgn = −1` 이면 max − v_pre 를 "droop"(여기선 overshoot)로 보고한다. 실제 출력: `i0=3.00 i1=0.50 slew=0.986A/us`, `overshoot 29.68/29.66mV zeta=0.1498 vmax_raw=0.8332V`. 선형 모델이라 크기가 droop와 같지만, 실제 벅은 해제 때 다르다: 하이사이드가 꺼진 채 인덕터 전류는 −Vout/L 기울기로만 줄어든다(부하 증가 때는 (Vin − Vout)/L 로 늘린다). Vout 이 Vin − Vout 보다 훨씬 작으면(0.8 V ← 12 V) **해제 overshoot가 droop보다 큰 경우가 흔하다**(특히 낮은 Vout). 그래서 스펙은 두 방향을 따로 잰다.

</details>

### 연습 3 — Zout 피크 Q ↔ 과도응답 ζ 교차 검증

`zout_mag(f, ζ, fn, C)`, `zout_peak(...)`(로그 스윕 + 황금분할)로 |Zout| 피크를 구해 Q = |Z|pk/Z0 와 과도응답에서 잰 1/(2ζ), fn 을 비교하고, 교차 주파수 fc = fn·√(√(1+4ζ⁴) − 2ζ²) 와 fsw/fc 를 구한다.

<details>
<summary>답 보기</summary>

병렬 RLC는 정확히 ωn에서 피크, 높이 R = Z0/(2ζ). 실제 출력: `f_pk=48000.0Hz |Z|pk=50.24mOhm Z0=15.07mOhm Q=3.33`, 과도응답 쪽 `Q_transient=3.33 Q_Zout=3.33 fn_meas=48.02kHz`, `fc=46.95kHz fsw/fc=21.3`. 이게 비침습 안정성 측정의 원리다: VNA로 레일 임피던스를 재서 피크 Q를 보거나(PDN 측정, HWLAB-M03 참고), 스텝 링잉으로 ζ를 보거나 같은 극점을 다른 창으로 보는 것. fsw/fc ≥ 5~10은 흔한 설계 가이드(샘플링·리플 영향 회피)라 21은 여유 있다.

</details>

### 연습 4 — 판정기: UNSTABLE / MARGINAL / LOW_PM / OK

`classify(m)`: ζ ≤ 0 → `UNSTABLE`(감쇠 없음·성장), PM < 30° → `MARGINAL`, < 45° → `LOW_PM`, 그 외 `OK`, ζ 없이 하한만 있으면 하한 ≥ 45°일 때 `OK`. ζ = −0.02(성장하는 링잉) 합성 캡처, Part C 두 캡처, ζ = 0.8 캡처로 확인.

<details>
<summary>답 보기</summary>

음의 감쇠는 로브가 커지므로 가중 LS 기울기가 양수 → λ < 0 → ζ < 0. 실제 출력: `zeta=-0.0198 rec=None lobes=22` → UNSTABLE, 그리고 `MARGINAL / OK / OK (PM_min=64.9)`. 주의: 실제 회로의 불안정은 무한히 커지지 않고 전류 제한·듀티 포화로 **리밋 사이클(일정 진폭 발진)** 이 된다 → 측정 ζ ≈ 0, recovery None, 스펙트럼에 날카로운 선. 링잉 주파수가 fsw/2 근처면 전류 모드의 subharmonic 발진을 의심한다(루프 이득 2차 모델 밖의 현상).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 9/9 passed, exit=0
```

## 10. 핵심 정리

- Zout = sL_eq(루프 구간) ∥ 1/(sC) → 교차 주파수 근처 병렬 공진. 피크 = Z0·Q, Z0 = 1/(ωn·C), **Q = 1/(2ζ)**. 스텝 링잉 = 이 공진의 감쇠 진동.
- droop ≈ ΔI·Z0 = ΔI/(2π·fc·C) (× ζ 보정, + ESR/ESL), 램프 중엔 L_eq·di/dt — **느린 e-load는 droop와 링잉을 숨긴다**, slew부터 검증(`INVALID`).
- 자유감쇠 극값 비 = e^(−λ), λ = πζ/√(1−ζ²) → **ζ = λ/√(π² + λ²)**. 램프가 끝난 뒤 로브만, ln|a| 가중 LS(가중치 a²).
- 주기는 0 교차로(기울기 최대), 진폭은 꼭짓점으로(기울기 0). 교차가 부족하면 정밀도가 떨어진다고 적는다.
- **PM = atan(2ζ/√(√(1+4ζ⁴) − 2ζ²))**; 100ζ 는 ζ ≤ 0.6에서 3.3° 이내, 0.8에서 +10°. PM 45° ⇔ ζ 0.420. 2차·우세극점 가정 → ±10° 스크리닝, 경계면 Bode.
- 리플은 정확히 1 fsw 박스카(BW limit 아님), 기준선은 정착값(load-line), 히스테리시스 ±3σ / 문턱 6σ, 로브 1개면 하한만.
- window는 원시 캡처, 루프 특성은 필터 출력. 리포트 = 시험 조건 + 항목별 PASS/FAIL + ζ/PM(식 명시) + 다음 행동.
- 이 보드: 보상 전 ζ 0.150 → PM 17°, recovery 29.7 µs → FAIL / 보상 후 ζ 0.536 → PM 55°, 14.1 µs → PASS (window는 둘 다 통과).

## 참고자료

- Robert W. Erickson, Dragan Maksimović, *Fundamentals of Power Electronics*, 3rd ed. — 폐루프 출력 임피던스, 루프 이득, 위상 마진과 Q의 관계(2차 근사 Q = √cos φm / sin φm)
- Katsuhiko Ogata, *Modern Control Engineering* — 2차 시스템 계단 응답, 로그 감쇠율, 감쇠비와 위상 마진
- Steven M. Sandler, *Power Integrity: Measuring, Optimizing, and Troubleshooting Power Related Parameters in Electronics Systems* (McGraw-Hill) — 출력 임피던스 기반 안정성 평가, 부하 스텝 측정 실무
- Eric Bogatin, *Signal and Power Integrity — Simplified* — PDN 임피던스, 타겟 임피던스 개념 (HWLAB-M03와 연결)
- OMICRON Lab Bode 100 / B-WIT 100 응용 노트 — 주입 트랜스포머를 이용한 루프 이득(안정성) 측정 절차
- Picotest 응용 자료 — 비침습 안정성 측정(NISM): 출력 임피던스 피크로 위상 마진 추정
- [PMBus specification](https://pmbus.org/) — 디지털 전원 컨트롤러 명령(READ_VOUT 등) 일반
- 관련 레슨: HWLAB-B04(10/90 % 교차·settling), HWLAB-B05(프로브·그라운드 리드), HWLAB-B08(리플·BW limit), HWLAB-B09(smoke test·inrush), HWLAB-M03(PDN 타겟 임피던스·디커플링), see: SIV-A06(전원 시퀀싱)
