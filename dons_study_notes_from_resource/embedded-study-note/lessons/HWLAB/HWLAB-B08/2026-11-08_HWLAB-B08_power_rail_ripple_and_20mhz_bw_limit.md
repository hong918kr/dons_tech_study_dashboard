---
id: HWLAB-B08
time: 70분
updated: 2026-11-08
---
# HWLAB-B08 전원 레일 측정: DC 정확도·리플·20MHz BW Limit

## 학습 목표

- 스코프 캡처에서 **Vavg · Vmin/Vmax · Vpp · AC RMS** 를 계산하고, 삼각파/정현파 리플의 RMS–Vpp 관계(`Vpp/√12`, `Vpp/(2√2)`)를 확인할 수 있다.
- 벅(buck) 출력 리플을 **ΔI = (Vin−Vout)·D/(L·fsw)**, **ΔV_C = ΔI/(8·fsw·C)**, **ΔV_ESR = ΔI·ESR** 로 예측하고, 실측 Vpp에서 거꾸로 **유효 출력 용량 C_eff** 를 추정해 "커패시터 미실장"을 짚어낼 수 있다.
- 스코프의 **20 MHz BW limit** 을 2차 Butterworth 바이쿼드(bilinear transform + 프리워핑)로 직접 구현하고, 스위칭 스파이크가 Vpp를 얼마나 부풀리는지(이 레슨 데이터에서 8.2 mV → 40.9 mV) 수치로 보여 줄 수 있다.
- 디지털 필터의 **시작 과도응답**, **잡음 등가 대역폭(ENBW)**, **Vpp가 기록 길이에 따라 커지는 성질**, 8비트 ADC **수직 분해능·클리핑** 같은 측정 함정을 설명할 수 있다.
- 레일 스펙(±3 %, 30 mVpp @20 MHz)으로 하드웨어 엔지니어가 바로 움직일 수 있는 PASS/FAIL 리포트를 만든다.

## 1. 문제

첫 보드가 랩에 오면 펌웨어가 한 줄도 돌기 전에 **모든 전원 레일의 DC 값과 리플** 부터 잰다 — Rivian/NVIDIA/Apple 보드·포스트실리콘 검증 면접에서 "리플을 어떻게 쟀고, 그 숫자를 믿을 수 있나?"는 거의 반드시 나오는 질문이다.

리플 측정이 까다로운 이유는 **같은 레일을 재도 측정 조건에 따라 숫자가 5배씩 달라지기 때문**이다. 벅 컨버터 출력에는 (1) 스위칭 주파수(fsw)의 느린 리플, (2) 스위치 노드가 수 ns 만에 0 V ↔ Vin 으로 뛸 때 생기는 수백 MHz 링잉 스파이크, (3) 프로브·스코프 잡음이 겹쳐 있다. 부하(SoC)가 실제로 느끼는 것은 대부분 (1)이고, (2)는 SoC 패키지 디커플링이 대부분 걸러 내며 일부는 아예 **프로브 그라운드 루프가 주워 온 가짜**다. 그래서 많은 레일 스펙이 "Vpp, 20 MHz 대역 제한" 처럼 **측정 대역폭을 같이 적는다**.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb08-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">측정 체인: 숫자는 레일만이 아니라 체인 전체의 결과다</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="140" height="80" rx="8" class="f-acc"/>
    <rect x="180" y="50" width="120" height="80" rx="8" class="f-mut"/>
    <rect x="330" y="50" width="200" height="80" rx="8" class="f-mut"/>
    <rect x="560" y="50" width="170" height="80" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="72" font-weight="bold">벅 출력</text>
    <text x="80" y="92" font-size="12">SW → L → C_out</text>
    <text x="80" y="110" font-size="12">(ESR · ESL)</text>
    <text x="240" y="72" font-weight="bold">프로브</text>
    <text x="240" y="92" font-size="12">10x 패시브</text>
    <text x="240" y="110" font-size="12">spring-tip GND</text>
    <text x="430" y="72" font-weight="bold">스코프 프런트엔드</text>
    <text x="430" y="92" font-size="12">1 MΩ · BW limit 20 MHz?</text>
    <text x="430" y="110" font-size="12">8-bit ADC · 2.5 GS/s</text>
    <text x="645" y="72" font-weight="bold">이 레슨 코드</text>
    <text x="645" y="92" font-size="12">CSV → stats</text>
    <text x="645" y="110" font-size="12">→ Butterworth → 리포트</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb08-arr)">
    <path d="M150 90 L178 90"/><path d="M300 90 L328 90"/><path d="M530 90 L558 90"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="160">진짜 성분: fsw 리플 (ΔV_C + ΔV_ESR + ESL 계단)</text>
    <text x="10" y="180">진짜일 수도 가짜일 수도: 스위치 에지 링잉 스파이크 (100–300 MHz)</text>
    <text x="10" y="200">측정 체인이 더하는 것: 그라운드 리드 루프 픽업(HWLAB-B05), 10x 프로브로 10배 커진 스코프 잡음,</text>
    <text x="10" y="220">V/div 가 커서 생기는 양자화(LSB), 너무 작아서 생기는 클리핑, BW limit 유무 · 필터 차수</text>
    <text x="10" y="242">→ 리포트에는 반드시 "BW, 프로브, 그라운드 방식, 기록 길이" 를 같이 적는다.</text>
  </g>
</svg>
```

보드 **EVT1-07** 의 세 레일을 재 왔다. 캡처 조건은 모두 같다: 10x 패시브 프로브 + 스프링 팁 그라운드, 전대역(full BW), 2.5 GS/s, 4 µs(10,000 샘플). 레일 스펙 시트(`SPECS`)에는 정격·허용오차·리플 한도와 설계값(Vin, fsw, L, C)이 있다.

| 레일 | 정격 | 스펙 | 설계 (Vin, fsw, L, C_out) | 데이터 |
|---|---|---|---|---|
| VDD_1V8 | 1.8 V | ±3 %, 30 mVpp @20 MHz | 5 V, 2 MHz, 1.0 µH, 4.7 µF | `cap_VDD_1V8.csv` (커밋된 파일) |
| VDD_0V8 | 0.8 V | ±3 %, 30 mVpp @20 MHz | 3.3 V, 1 MHz, 0.47 µH, 30 µF | `gen_capture.synth("VDD_0V8")` (메모리) |
| VDD_3V3 | 3.3 V | ±3 %, 30 mVpp @20 MHz | 5 V, 2 MHz, 1.5 µH, 10 µF | `gen_capture.synth("VDD_3V3")` (메모리) |

> 데이터는 **합성(synthetic)** 이다 (`gen_capture.py`, 고정 seed). 실제 계측기 출력이 아니고, 물리 모델은 풀이 Part A에 적었다. 보드에는 결함 두 개가 심어져 있다 — 분석하는 사람은 스펙 시트만 보고 찾아내야 한다.

한 스위칭 주기를 확대하면 이렇게 생겼다 (VDD_1V8, 개념도):

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">VDD_1V8 한 주기 (T = 500 ns): 리플 + 에지 스파이크</text>
  <g stroke="currentColor" stroke-dasharray="4 4" opacity=".6">
    <line x1="80" y1="60" x2="700" y2="60"/><line x1="80" y1="210" x2="700" y2="210"/>
    <line x1="80" y1="118" x2="700" y2="118"/><line x1="80" y1="152" x2="700" y2="152"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="704" y="64">max</text><text x="704" y="214">min</text>
    <text x="704" y="122">max</text><text x="704" y="156">min</text>
  </g>
  <path d="M80 150 Q170 160 260 140 L262 128 Q440 100 590 150 L592 148 Q650 160 700 150" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <path d="M260 140 l3 -80 l3 120 l3 -90 l3 60 l3 -30 l3 20 l3 -8" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <path d="M590 150 l3 60 l3 -95 l3 70 l3 -45 l3 25 l3 -14 l3 6" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <g fill="currentColor" font-size="12">
    <text x="210" y="50">ON 에지 (SW ↑) 링잉 ~180 MHz</text>
    <text x="520" y="240">OFF 에지 (SW ↓) 링잉</text>
    <text x="300" y="98">포물선 = ∫i_ac dt / C, 기울기 변화 = ESR·i_ac, 계단 = ESL·di/dt</text>
  </g>
  <rect x="14" y="60" width="54" height="150" class="f-warn" stroke="currentColor" opacity=".55"/>
  <rect x="14" y="118" width="54" height="34" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="41" y="84">full</text><text x="41" y="100">40.9</text><text x="41" y="196">mVpp</text>
    <text x="41" y="140">8.2</text>
  </g>
  <text x="10" y="262" fill="currentColor" font-size="12">전대역 Vpp = 스파이크 꼭대기–바닥 (40.9 mV), 20 MHz Vpp = 리플 자체 (8.2 mV, 정답 8.21 mV)</text>
</svg>
```

### Part A — 기본 통계와 벅 리플 이론

- `rail_stats(v)` → `n, vavg, vmin, vmax, vpp, rms_ac` (`rms_ac` = 평균을 뺀 RMS = AC 커플링 RMS, N으로 나눔).
- `buck_ripple_theory(vin, vout, fsw, L, C, esr)` → `D, dI, dv_c, dv_esr`.
- 검증: 삼각파 RMS = Vpp/√12, 정현파 RMS = Vpp/(2√2), 커패시터 성분만 남긴 합성 파형의 Vpp == ΔI/(8·fsw·C) (0.5 % 이내), CSV의 Vavg = 1.8030 V ±0.3 mV.

### Part B — 20 MHz BW limit 과 그 함정

- `butter_biquad(fc, fs, q)` — 아날로그 2차 저역통과를 **프리워핑한 bilinear transform** 으로 이산화한 계수 `(b0, b1, b2, a1, a2)`. `butter_qs(order)`(주어진 코드)로 4차도 만든다.
- `biquad_filter(x, c, init)` — Transposed Direct Form II. `init="dc"` 는 첫 샘플의 DC 정상 상태로 시작, `init=None` 은 0 상태 → **1.8 V 레일이 0 V 에서 출발하는 가짜 1.88 V "리플"** 이 생기는 것을 테스트로 보인다.
- 필터 검증: |H(0)| = 1, |H(20 MHz)| = 0.7071, |H(2 MHz)| > 0.9999, |H(180 MHz)| < 0.0125, 백색 잡음 σ 비 = √(ENBW/(fs/2)).
- `find_spikes(t, v_full, v_bwl, thr, gap)` — 잔차 `v_full − v_bwl` 에서 스파이크 이벤트를 묶어 개수·부호·시각을 낸다. 16개(ON 8 + OFF 8), ON 에지 간격 = 500 ns = 1/fsw 여야 한다.
- 비교: 전대역 Vpp(스파이크 포함)가 정답 리플보다 25 mV 이상 크고, 20 MHz Vpp 는 정답의 ±3 mV 안.

### Part C — 보드 EVT1-07 레일 리포트

- `rail_report(name, t, v)` — DC(±3 %)는 전대역 Vavg로, 리플은 20 MHz Vpp로 판정. 스파이크는 따로 보고하고, 리플 FAIL이면 **C_eff = ΔI_design/(8·fsw·Vpp)** 로 "커패시터 얼마가 빠진 것 같은지"까지 적는다.
- 기대 결과: VDD_1V8 PASS(전대역만 FAIL → 스파이크 힌트), VDD_0V8 리플 FAIL(C_eff ≈ 4 µF vs 설계 30 µF), VDD_3V3 DC FAIL(−3.27 %). 보드 요약 `pass=1 fail=2`.

## 2. Problem (English)

> **Power-rail ripple measurement with a bandwidth limit.**
>
> You are given oscilloscope captures of buck-converter outputs (time in ns, volts; 2.5 GS/s, 4 µs) and a rail spec sheet
> (nominal, ±3 % DC tolerance, 30 mVpp ripple limit measured with a 20 MHz bandwidth limit, plus design Vin/fsw/L/C).
> Using only the Python 3 standard library:
>
> 1. `rail_stats(v)` returning mean, min, max, peak-to-peak and AC RMS; `buck_ripple_theory(vin, vout, fsw, L, C, esr)`
>    returning duty cycle, inductor ripple current ΔI, capacitive ripple ΔI/(8·fsw·C) and ESR ripple ΔI·ESR.
> 2. Emulate the scope's 20 MHz bandwidth limit with a 2nd-order Butterworth low-pass (bilinear transform with pre-warping,
>    transposed direct form II). Initialise the filter state so that a DC input produces no start-up transient.
>    Show that switching spikes inflate full-bandwidth Vpp far above the true ripple while the band-limited Vpp tracks it.
>    Detect spike events from the residual (full − band-limited) and verify they occur at the switching edges.
> 3. `rail_report(name, t, v)` → PASS/FAIL for DC (mean within ±3 %) and ripple (band-limited Vpp ≤ 30 mV), spikes
>    reported separately, and an actionable hint (estimated effective output capacitance when ripple fails).
>
> Example: `FAIL rail=VDD_0V8 Vavg=0.8008V (+0.10%, DC PASS) Vpp=48.4mV@full 38.8mV@20MHz (limit 30mV, ripple FAIL) ...`
> followed by `-> ripple 38.8mV@20MHz → C_eff≈4.2uF (설계 30.0uF): C_out 실장/DC-bias derating 확인`.

## 3. 시작 코드

<!-- code: starter.py -->

합성 캡처 생성기 (결정적, `python3 -I gen_capture.py` 로 `cap_VDD_1V8.csv` 를 다시 쓴다):

<!-- code: gen_capture.py -->

`cap_VDD_1V8.csv` 앞부분 (147 KB, 10,003줄):

```text
# SYNTHETIC capture generated by gen_capture.py (HWLAB-B08) -- not a real instrument export
# rail=VDD_1V8  fs=2.5e+09 Sa/s  record=4e-06 s  bw=full  probe=10x passive, spring-tip ground
time_ns,volt_V
0.0,1.80538
0.4,1.80306
```

```bash
cd lessons/HWLAB/HWLAB-B08
python3 -I starter.py ; echo "exit=$?"     # TODO 상태: 3/29 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: stats & buck ripple theory --
FAIL  rail_stats([1,2,3,4]) vavg/vpp/rms_ac: NotImplementedError: rail_stats
...
PASS  load_csv: 10000 samples, dt=0.4ns  [n=10000]
PASS  committed cap_VDD_1V8.csv == gen_capture output (stale check)
...
PASS  butter_qs(4) = [1.3066, 0.5412]  [[1.3065629648763766, 0.541196100146197]]
FAIL  |H(0)| == 1 (DC passes untouched): TypeError: 'NoneType' object is not iterable
...
FAIL  report deterministic: NotImplementedError: rail_report
3/29 passed
exit=1
```

> `python3 -I` 는 스크립트 디렉터리를 `sys.path` 에 넣지 않는다 (Python 3.9 isolated mode). 그래서 코드 첫머리에서 `sys.path.insert(0, HERE)` 로 `gen_capture` 를 import 한다.

## 4. 힌트

<details>
<summary>힌트 1 — RMS 리플은 "평균을 뺀" RMS</summary>

1.8 V 레일의 그냥 RMS는 1.8 V 근처다. 리플 RMS는 `sqrt(Σ(x−mean)²/N)` — 스코프의 "AC RMS" 측정과 같다. 합은 `math.fsum` 으로 (10,000개 1.8 V를 더하면 일반 `sum` 의 반올림 오차가 µV 수준에서 보이기 시작한다).

</details>

<details>
<summary>힌트 2 — ΔV = ΔI/(8·f·C) 는 삼각형 넓이</summary>

인덕터 전류의 AC 성분은 ±ΔI/2 삼각파. 그 중 양(+)인 반 주기 동안 커패시터로 들어가는 전하는 밑변 T/2, 높이 ΔI/2인 삼각형 넓이 = ΔI·T/8. ΔV = Q/C = ΔI/(8·fsw·C). (듀티와 무관하다는 점이 재미있다.)

</details>

<details>
<summary>힌트 3 — bilinear transform 계수</summary>

H(s) = 1/(s² + s/Q + 1) (ωc로 정규화)에 `s = (1/K)·(1−z⁻¹)/(1+z⁻¹)`, `K = tan(π·fc/fs)` 를 넣고 분모·분자에 K²(1+z⁻¹)² 를 곱해 정리하면 `b = K²·[1, 2, 1]/(1+K/Q+K²)`, `a1 = 2(K²−1)/(…)`, `a2 = (1−K/Q+K²)/(…)`.

</details>

<details>
<summary>힌트 4 — DC 정상 상태로 초기화</summary>

TDF-II에서 입력 u가 영원히 이어졌다면 출력은 g·u (g = Σb/(1+a1+a2) = 1). 상태 갱신식에 x=u, y=g·u 를 넣으면 `z2 = (b2 − a2·g)·u`, `z1 = (b1 − a1·g)·u + z2`. 이게 SciPy `lfilter_zi` 가 하는 일과 같은 아이디어다.

</details>

<details>
<summary>힌트 5 — 스파이크 이벤트 묶기</summary>

잔차 `r = v_full − v_bwl` 은 리플을 거의 다 지우고 스파이크와 고주파 잡음만 남긴다. 잡음 σ ≈ 1.5 mV이므로 thr = 8 mV(5σ 이상)면 거짓 이벤트가 거의 없다. 링잉 한 번은 여러 샘플에서 thr을 넘었다 내려왔다 하므로 "직전 hit와 20 ns 미만이면 같은 이벤트"로 묶는다.

</details>

## 5. 풀이

### Part A — 통계, 그리고 리플이 어디서 오는가

**통계 정의.** 이 레슨에서 쓰는 네 숫자는 서로 다른 질문에 답한다.

| 측정값 | 식 | 답하는 질문 | 함정 |
|---|---|---|---|
| Vavg | Σv/N | DC 정확도 (±3 %) | 정수 주기가 아니면 리플 일부가 섞인다 (여기선 4 µs = 8주기라 정확) |
| Vpp | max − min | 순간 최악값, 스펙 한도 | 스파이크 1개·잡음 꼬리 1개에 좌우, **기록 길이에 따라 커진다** (연습 4) |
| AC RMS | √(Σ(v−avg)²/N) | 잡음·리플의 에너지 | 피크를 숨긴다. 삼각파는 Vpp/√12 = 0.289·Vpp |
| Vmin/Vmax | 그대로 | DC+AC 합산 윈도우 | BW 설정에 따라 값이 달라진다 (연습 5) |

테스트 결과: 20 mVpp 삼각파 RMS 5.7735 mV = 20/√12, 정현파 7.0711 mV = 20/(2√2).

**벅 리플 모델.** 이상적 CCM 벅에서 D = Vout/Vin, ON 구간 동안 인덕터 전압 Vin−Vout 이 걸려 전류가 ΔI 만큼 오른다:

```text
ΔI   = (Vin − Vout)·D·T / L = (Vin − Vout)·D / (L·fsw)
VDD_1V8 설계: (5 − 1.8)·0.36 / (1 µH · 2 MHz) = 0.576 A
ΔV_C   = ΔI / (8·fsw·C)   = 0.576 / (8 · 2e6 · 4.7e-6) = 7.66 mV
ΔV_ESR = ΔI · ESR         = 0.576 · 10 mΩ               = 5.76 mV
ESL 계단 = ESL·di/dt       ON: 0.3 nH · 0.576 A/180 ns ≈ 1.0 mV
```

`gen_capture.synth` 는 이 세 성분을 그대로 더한다. 커패시터 전압은 삼각파 전류의 적분이라 **포물선 조각** 이고(ON 구간에서 아래로 볼록, OFF 구간에서 위로 볼록), 한 주기 평균을 빼야 DC가 Vout에 맞는다. 평균은 해석적으로 `ΔI·T·(1−2D)/12` (∫q dt 를 두 구간에서 계산, gen 코드 주석). 커패시터 성분만 남긴 합성 파형의 Vpp가 7.6596 mV로 ΔI/(8fC)와 0.5 % 안에서 일치하는 것이 Part A의 핵심 테스트다.

**pk-pk 는 더해지지 않는다.** ESR 리플(삼각파)은 전류와 위상이 같고, 커패시터 리플(포물선)은 전류의 적분이라 1/4 주기 늦다. 실제 계산값(스파이크·잡음 없음):

| 레일 | ΔV_C | ΔV_ESR | 단순 합 | 실제 Vpp (ESL 제외) | ESL 포함 정답 | 20 MHz 필터 후 |
|---|---|---|---|---|---|---|
| VDD_1V8 | 7.665 mV | 5.764 mV | 13.43 mV | 8.84 mV | 8.21 mV | 7.50 mV |
| VDD_0V8 | 40.32 mV | 3.87 mV | 44.19 mV | 40.45 mV | 38.34 mV | 38.34 mV |
| VDD_3V3 | 2.405 mV | 1.924 mV | 4.33 mV | 2.82 mV | 2.92 mV | 2.40 mV |

설계 단계에서 단순 합을 쓰는 것은 보수적(안전한) 추정이지만, 실측과 비교할 때 "실측이 이론보다 작다 = 이상하다" 고 판단하면 안 된다. 그리고 마지막 열이 보여 주듯 **20 MHz 필터는 ESL 계단의 날카로운 모서리(진짜 성분)도 조금 깎는다** (8.21 → 7.50 mV). BW limit은 공짜가 아니다.

**DC 정확도.** CSV의 Vavg = 1.80303 V (+0.17 %). 수학적으로는 10,000 샘플 평균이라 잡음 σ/√N = 15 µV 수준까지 정확하지만, **스코프 자체의 DC 정확도가 그보다 훨씬 나쁘다**. 8비트 스코프의 DC 게인 정확도는 대개 풀스케일의 1–2 % 대(모델별 데이터시트 확인), 여기에 오프셋 정확도가 더해진다. ±3 % 판정에 그 오차 예산은 너무 크다. 그래서 실무에서는:

| 측정 | 도구 | 이유 |
|---|---|---|
| DC (±3 %) | 6½자리 DMM, 레일의 **sense 지점**(부하 쪽 커패시터 양단)에서 | 0.01 % 대 정확도. 레귤레이터 출력 핀에서 재면 PCB IR drop을 놓친다 |
| 리플 Vpp | 스코프 + 1:1 레일 프로브 또는 10x + DC offset, BW limit 명시 | 시간 영역 피크가 필요 |
| 부하 과도 | 스코프 + 전류 프로브/전자 부하 | 스텝 응답, 언더슈트 |

이 레슨의 리포트는 Vavg를 스코프 데이터로 계산하지만, 결과 힌트에 "DMM으로 sense 지점 재측정"을 넣은 이유가 이것이다.

### Part B — BW limit을 직접 만든다

**왜 Butterworth 2차인가.** 스코프의 20 MHz BW limit 구현(아날로그 필터인지 DSP인지, 몇 차인지)은 제조사·모델마다 다르다. 이 레슨은 "통과대역이 평탄하고 −3 dB가 정확히 20 MHz"인 2차 Butterworth를 가정한다. 연습 2에서 1차/4차와 비교해 **필터 모양에 따라 Vpp(20 MHz)가 0.5 mV 정도 달라진다** 는 것도 확인한다. 그래서 리포트에는 "20 MHz BW limit, 스코프 모델"까지 적는 게 맞다.

**Bilinear transform + 프리워핑.** 아날로그 원형 H(s) = 1/(s² + s/Q + 1) (ωc = 1로 정규화). bilinear transform `s ← (2/Ts)·(1−z⁻¹)/(1+z⁻¹)` 는 아날로그 주파수 축 전체를 디지털 0…fs/2 로 압축하면서 주파수를 휘게(warp) 한다: Ω_analog = (2/Ts)·tan(ω/2). 그래서 원하는 디지털 fc에서 정확히 −3 dB가 나오도록 **아날로그 쪽 차단 주파수를 tan으로 미리 휘어 놓는다** (prewarp). ωc로 정규화하면 두 효과가 합쳐져 `s = (1/K)·(1−z⁻¹)/(1+z⁻¹)`, `K = tan(π·fc/fs)` 가 된다.

```text
분자·분모에 K²(1+z⁻¹)² 를 곱해 정리:
  H(z) = K²(1 + 2z⁻¹ + z⁻²) / [(1 + K/Q + K²) + 2(K² − 1)z⁻¹ + (1 − K/Q + K²)z⁻²]
  norm = 1/(1 + K/Q + K²)
  b0 = b2 = K²·norm, b1 = 2·b0, a1 = 2(K² − 1)·norm, a2 = (1 − K/Q + K²)·norm
fc = 20 MHz, fs = 2.5 GHz → K = tan(0.02513) = 0.025138
z = 1 (DC) 대입: Σb / (1 + a1 + a2) = 4K² / 4K² = 1  → DC 이득 정확히 1
```

4차 Butterworth는 2차 섹션 두 개를 직렬로: `Q_k = 1/(2·sin((2k−1)π/(2n)))` → 1.3066, 0.5412. fc에서 두 섹션의 |H| 곱이 Q1·Q2 = 0.7071이 되어 역시 −3 dB.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">BW limit 필터 |H(f)| (dB, 로그 주파수) — 1차 / 2차 / 4차, fc = 20 MHz</text>
  <g stroke="currentColor" opacity=".35">
    <line x1="60" y1="50" x2="660" y2="50"/><line x1="60" y1="100" x2="660" y2="100"/>
    <line x1="60" y1="150" x2="660" y2="150"/><line x1="60" y1="200" x2="660" y2="200"/>
    <line x1="60" y1="250" x2="660" y2="250"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="55" y="54">0</text><text x="55" y="104">−20</text><text x="55" y="154">−40</text>
    <text x="55" y="204">−60</text><text x="55" y="254">−80</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="60" y="270">1M</text><text x="260" y="270">10M</text><text x="460" y="270">100M</text><text x="660" y="270">1G Hz</text>
  </g>
  <g fill="none" stroke="currentColor">
    <polyline stroke-dasharray="2 3" points="60,50 180,50 240,52 280,54 300,55 320,58 340,60 360,64 380,67 400,72 420,76 440,81 460,85 480,90 500,95 520,100 540,105 560,110 580,115 600,120 620,125 640,130 660,135"/>
    <polyline stroke-width="2.5" points="60,50 250,50 270,51 290,52 300,54 310,55 320,57 330,60 340,64 350,67 360,72 370,76 380,81 390,85 400,90 420,100 440,110 460,120 480,130 500,140 520,150 540,160 560,170 580,180 600,190 620,200 640,210 660,220"/>
    <polyline stroke-dasharray="8 4" points="60,50 280,50 290,51 300,52 310,54 320,57 330,63 340,71 350,80 360,90 380,110 400,130 420,150 440,170 460,190 480,210 500,230 520,250 660,250"/>
  </g>
  <g stroke="currentColor">
    <line x1="120" y1="40" x2="120" y2="258" class="f-ok" stroke-dasharray="3 3"/>
    <line x1="320" y1="40" x2="320" y2="258" stroke-dasharray="3 3"/>
    <line x1="511" y1="40" x2="511" y2="258" stroke-dasharray="3 3"/>
  </g>
  <rect x="505" y="141" width="12" height="12" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="124" y="44">fsw 2 MHz: 손실 0.0004 dB</text>
    <text x="324" y="44">20 MHz: −3.01 dB</text>
    <text x="515" y="44">링잉 180 MHz</text>
    <text x="522" y="140">2차 −38.5 dB (×0.0119)</text>
    <text x="522" y="96">1차 −19 dB</text>
    <text x="470" y="244">4차 −77 dB</text>
    <text x="70" y="292">점선 1차 · 굵은 선 2차 Butterworth (기본) · 긴 점선 4차 (−80 dB 아래 생략)</text>
  </g>
</svg>
```

실제 코드로 잰 값 (`freq_response`):

| f | 2차 \|H\| | 4차 \|H\| | 의미 |
|---|---|---|---|
| 2 MHz (fsw) | 0.99995 | 1.000 | 리플 기본파는 그대로 통과 |
| 20 MHz | 0.70711 | 0.7071 | 프리워핑 덕에 정확히 −3 dB |
| 150 MHz | 0.01736 | 3.0e-4 | VDD_3V3 링잉 |
| 180 MHz | 0.01193 | 1.4e-4 | VDD_1V8 링잉 |
| 220 MHz | 0.00785 | 6.2e-5 | VDD_0V8 링잉 |

**TDF-II와 DC 초기화.** Transposed Direct Form II는 상태 두 개(z1, z2)로 `y = b0·x + z1; z1 = b1·x − a1·y + z2; z2 = b2·x − a2·y`. 상태를 0으로 시작하면 필터는 "0 V가 영원히 들어오다가 t=0에 갑자기 1.8 V"를 본 것으로 해석해 **스텝 응답** 을 낸다. 2차 Butterworth 스텝 응답은 약 4 % 오버슈트하고 수십 ns 동안 안정되므로, 레코드 전체의 Vpp는 1.881 V가 된다 (테스트 출력 `Vpp=1.881V`). 진짜 스코프의 아날로그 BW limit은 늘 켜져 있어 이런 과도가 없다. 해결책은 둘: (1) 이 레슨처럼 첫 샘플의 DC 정상 상태로 초기화, (2) 앞부분(수 × 1/fc, 여기선 100 ns 정도)을 버리기. 실전에서는 둘 다 한다 — 첫 샘플 자체가 스파이크 위에 있을 수도 있기 때문.

**잡음 등가 대역폭 (ENBW).** 백색 잡음은 대역폭에 비례하는 전력을 가진다. 샘플링된 백색 잡음의 대역은 0…fs/2 = 1.25 GHz. n차 Butterworth의 ENBW = fc·(π/2n)/sin(π/2n) → 2차 1.1107·fc = 22.2 MHz. 따라서 σ_out/σ_in = √(22.2/1250) = 0.1333 — 테스트 실측 0.1320 (±6 % 안). 즉 **BW limit은 잡음 σ를 7.5배 줄인다.** 이 레슨의 1.5 mV 잡음이 20 MHz Vpp에 거의 안 보이는 이유다. (1차 RC는 ENBW = (π/2)·fc = 31.4 MHz → 0.1585, 연습 2에서 실측.)

**스파이크가 Vpp를 얼마나 부풀리나 (VDD_1V8 실측).**

| 측정 | Vpp | 스펙 30 mV |
|---|---|---|
| 전대역 (스파이크+잡음 포함) | 40.9 mV | FAIL |
| 20 MHz BW limit (2차) | 8.18 mV | PASS |
| 정답: 스파이크·잡음 없는 합성 리플 | 8.21 mV | — |

전대역 수치는 진짜 리플의 **5배**다. 스파이크의 꼭대기(ON 에지 +26 mV 부근)와 바닥(OFF 에지)이 Vpp를 정한다. 스파이크를 따로 보고하려면 잔차 `v_full − v_bwl` 을 쓴다: 리플은 필터를 그대로 통과하므로 잔차에서 사라지고, 수백 MHz 링잉과 고주파 잡음만 남는다. `find_spikes` 는 16개 이벤트(+8, −8)를 찾았고 ON 에지 간격은 정확히 500.0 ns — **스파이크가 스위칭 에지에 동기돼 있다는 것** 이 "이건 컨버터에서 온 것"이라는 증거다 (무작위 시각의 스파이크라면 외부 간섭이나 ESD, 다른 레일 커플링을 의심). 연습 1에서는 같은 이벤트 시각으로 fsw, 듀티, Vin까지 역산한다.

**그 스파이크는 진짜인가?** 측정 체인이 만든 가짜일 가능성을 먼저 지운다:

| 원인 | 증상 | 드러내는 측정 |
|---|---|---|
| 프로브 그라운드 리드 루프가 인덕터·스위치 노드 자기장을 주움 | 에지에 동기된 큰 링잉, 리드를 움직이면 모양이 바뀜 | 프로브 팁을 **같은 지점 GND에 단락**해서 측정 (루프 픽업만 보임), spring-tip으로 교체 (HWLAB-B05) |
| 10x 프로브로 스코프 잡음이 10배 | 잡음 바닥이 mV 수준 | 1:1 레일 프로브, 또는 같은 설정에서 프로브를 GND에 대고 잡음만 측정 |
| 진짜 스위치 노드 링잉이 출력으로 샘 | 프로빙을 고쳐도 남음, C_out 근처 고주파 커패시터 추가 시 감소 | 다른 위치(부하 쪽)에서 비교, 스너버/부트스트랩 저항 변경 효과 |
| 부하(SoC)의 di/dt | 스위칭과 무관한 시각, 펌웨어 활동과 상관 | 트리거를 GPIO 이벤트에 걸기 (HWLAB-B06) |

### Part C — 리포트: 숫자에서 행동으로

`python3 -I solution.py --report` 실제 출력:

```text
board=EVT1-07 rails=3 pass=1 fail=2
FAIL rail=VDD_0V8 Vavg=0.8008V (+0.10%, DC PASS) Vpp=48.4mV@full 38.8mV@20MHz (limit 30mV, ripple FAIL) rms=13.38mV spikes=8 pk=17mV
    -> ripple 38.8mV@20MHz → C_eff≈4.2uF (설계 30.0uF): C_out 실장/DC-bias derating 확인
PASS rail=VDD_1V8 Vavg=1.8030V (+0.17%, DC PASS) Vpp=40.9mV@full 8.2mV@20MHz (limit 30mV, ripple PASS) rms=2.66mV spikes=16 pk=26mV
    -> full-BW 초과는 스위칭 스파이크(n=16, pk=26mV): 스펙은 20MHz 기준. spring-tip으로 재측정해 진짜인지 확인
FAIL rail=VDD_3V3 Vavg=3.1920V (-3.27%, DC FAIL) Vpp=32.9mV@full 3.3mV@20MHz (limit 30mV, ripple PASS) rms=0.73mV spikes=16 pk=18mV
    -> DC -3.27% out of ±3%: DMM으로 sense 지점 재측정, FB 분압 저항·부하 IR drop 확인
    -> full-BW 초과는 스위칭 스파이크(n=16, pk=18mV): 스펙은 20MHz 기준. spring-tip으로 재측정해 진짜인지 확인
```

세 레일이 세 가지 다른 이야기를 한다:

- **VDD_1V8 — BW limit을 안 켰으면 멀쩡한 레일을 불량으로 보고했을 경우.** 전대역 40.9 mV는 FAIL, 20 MHz 8.2 mV는 PASS. 스펙이 20 MHz 기준이므로 PASS가 맞다. 단, 스파이크 26 mV pk는 별도로 남긴다 — 1.8 V I/O 레일이면 문제없지만, 같은 크기가 PLL·ADC 레퍼런스 레일에 있으면 지터/SNR 문제가 된다.
- **VDD_0V8 — BW limit을 켜도 FAIL인 경우 = 진짜 문제.** 20 MHz Vpp 38.8 mV. 설계값으로 ΔI = (3.3−0.8)·0.2424/(0.47 µH·1 MHz) = 1.29 A, 설계 C 30 µF이면 리플은 5.4 mV여야 한다. 거꾸로 `C_eff = ΔI/(8·fsw·Vpp) = 1.29/(8·1e6·38.8e-3) ≈ 4.2 µF` (실제 심은 값 4.0 µF — ESR·ESL 성분과 위상이 섞여 pk-pk가 순수 커패시터 리플보다 약간 작게 나와 4 % 정도 크게 추정된다. 1차 근사로 충분하다). 설계의 1/7. 원인 후보 순서: **C_out 미실장(DNP 오기, 조립 누락)** → MLCC DC-bias derating을 과소평가 (작은 패키지·낮은 정격전압 MLCC는 정격전압 근처에서 용량이 크게 줄어든다) → 커패시터가 레귤레이터에서 먼 곳에 배치. 이번 결함은 첫 번째(22 µF ×2 미실장)였다. X-ray나 육안 검사로 바로 확인 가능한 형태의 리포트다.
- **VDD_3V3 — 리플은 깨끗한데 DC가 −3.27 %.** 3.192 V. 벅 출력은 Vout = Vref·(1 + R_top/R_bot). Vref 0.6 V, R_bot 10 kΩ, 설계 R_top 45.3 kΩ → 3.318 V. 43.2 kΩ(E96 이웃 값)이 실장되면 0.6·(1+4.32) = 3.192 V. **고정된 % 오차 + 리플 정상 + 부하와 무관** 이면 FB 분압 BOM 오류, **부하에 비례해 커지는 오차** 면 IR drop(sense 위치)이나 전류 한계를 의심한다.

| 리포트 패턴 | 가장 흔한 원인 | 다음 측정 |
|---|---|---|
| full FAIL, 20 MHz PASS | 스위칭 스파이크 (진짜 or 프로브 픽업) | GND 단락 테스트, spring-tip, 부하 지점 측정 |
| 20 MHz FAIL, Vpp ≈ ΔI/(8fC_small) | C_out 누락/derating | BOM·조립 확인, LCR로 C 측정 |
| 20 MHz FAIL, 리플 주파수 ≠ fsw | 불안정(루프 발진), 경부하 PFM/burst 모드 | 주파수 측정, 부하 바꿔 보기, 루프 응답(Bode) |
| DC 고정 오프셋 | FB 저항, Vref 설정(PMIC 레지스터/OTP) | DMM, PMIC 레지스터 덤프 |
| DC 부하 의존 | IR drop, 전류 한계 | 부하 스텝, 원격 sense 확인 |

> 시퀀싱(어느 레일이 먼저 올라오는지, ramp time, PG 신호)은 이 레슨 범위 밖이다 — SIV-A06 참고. 첫 전원 인가 시 돌입 전류·스모크 테스트는 HWLAB-B09.

**장비/도구가 있을 때** (이 맥에는 계측기가 없으므로 아래는 실행하지 않은 참고용 명령이다. 정확한 SCPI 문법은 사용하는 모델의 프로그래밍 매뉴얼을 확인할 것):

```python
# PyVISA 로 Keysight InfiniiVision 계열 예시 (참고용, 실행하지 않음)
import pyvisa
scope = pyvisa.ResourceManager().open_resource("TCPIP0::192.168.1.50::INSTR")
scope.write(":CHANnel1:PROBe 10")          # 10x 프로브
scope.write(":CHANnel1:BWLimit 1")         # 20 MHz BW limit ON (Rigol 계열은 ':CHANnel1:BWLimit 20M')
scope.write(":CHANnel1:SCALe 0.01")        # 10 mV/div
scope.write(":CHANnel1:OFFSet 1.8")        # 1.8 V 를 화면 중앙에 (AC 커플링 대신 DC offset)
print(scope.query(":MEASure:VPP? CHANnel1"))
```

자동 측정값을 그대로 믿기보다 파형을 받아(`:WAVeform:DATA?`) 이 레슨 코드로 다시 계산하면, 측정 조건(BW, 기록 길이, 필터)을 리포트에 그대로 박을 수 있다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"            # 29/29 passed, exit=0
python3 -I solution.py --report                    # 테스트 + 보드 리포트 (§5 Part C 출력)
python3 -I gen_capture.py && python3 -I solution.py  # CSV 재생성 → sha 동일, stale check PASS
python3 -I -W error -X dev solution.py             # 경고 없음
```

실제 출력 (발췌):

```text
PASS  cap-only synth: measured Vpp == dI/(8fC) within 0.5%  [got 7.6596mV exp 7.6596mV ±0.0383mV]
PASS  VDD_1V8 capture Vavg = 1.8030V ±0.3mV (spikes/noise average out)  [got 1803.0302mV exp 1803.0000mV ±0.3000mV]
PASS  |H(20MHz)| = 0.7071 (-3dB) order 2  [got 0.7071 exp 0.7071 ±0.0001]
PASS  |H(180MHz)| < 0.0125 (ring ~ -38dB, order 2)  [0.01193]
PASS  zero-state init: start-up transient makes Vpp > 1V (the artifact)  [Vpp=1.881V]
PASS  white noise sigma ratio = sqrt(ENBW/(fs/2)) = 0.1333 ±6%  [got 0.1320 exp 0.1333 ±0.0080]
PASS  full-BW Vpp inflated by spikes: > 30mV and > truth + 25mV  [full=40.9mV truth=8.2mV]
PASS  20MHz Vpp within 3mV of spike/noise-free truth  [got 8.1788mV exp 8.2069mV ±3.0000mV]
PASS  find_spikes: 16 events (8 ON + 8 OFF edges in 4us @2MHz)  [n=16 pos=8]
PASS  ON-edge spikes every 500ns (= 1/fsw) ±2ns  [dt=[500.0, 500.0, 500.0] ns]
PASS  VDD_0V8: C_eff estimate < 25% of design 30uF → C_out hint  [c_est=4.15uF]
PASS  VDD_3V3: DC -3.27% → DC FAIL, ripple PASS  [-3.27%]
29/29 passed
```

`cap_VDD_1V8.csv` 재생성 전후 sha256 은 둘 다 `1fd694132e665266a437d5852d0fe8d2f07288cd553e7635c698bb05ce02d32a` 였다. 전체 테스트 실행 시간은 이 맥에서 약 0.2 s.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 습관 |
|---|---|---|
| 전대역 Vpp를 그대로 스펙과 비교 | 스펙이 20 MHz 기준이면 스파이크·잡음으로 5배 부풀린 숫자 | `vpp = rail_stats(bw_limit(v))["vpp"]`, 리포트에 BW 명시 |
| 반대로 **모든** 측정에 BW limit | 진짜 고주파 문제(abs-max 근접 스파이크, PLL 레일 잡음)를 숨김 | 스파이크를 `find_spikes` 로 **따로** 보고 |
| 리플 RMS = `sqrt(mean(v²))` | DC 1.8 V가 들어가 RMS ≈ 1.8 V | 평균을 뺀 `rms_ac` |
| 필터 상태 0으로 시작 | 1.8 V 스텝 응답 → Vpp 1.881 V | `init="dc"` (lfilter_zi 개념) 또는 앞부분 버리기 |
| 프리워핑 없이 `K = π·fc/fs` | fc가 fs에 가까우면 −3 dB 점이 밀림 (여기선 작지만 습관 문제) | `K = math.tan(math.pi * fc / fs)` |
| AC 커플링으로 리플 측정 후 Vavg 도 보고 | AC 커플링은 DC를 지움 (Vavg ≈ 0) | DC offset 기능 또는 레일 프로브, DC는 DMM |
| 500 mV/div로 1.8 V와 리플을 한 화면에 | 8비트 LSB = 15.6 mV > 리플 → 수치가 의미 없음 (연습 3) | 10 mV/div + 1.8 V offset |
| ΔV_C + ΔV_ESR 를 실측과 비교 | pk-pk는 위상이 달라 단순 합이 아님 (13.4 vs 8.8 mV) | 합성/시뮬레이션으로 같은 조건 비교 |
| 레귤레이터 출력 핀에서 DC 측정 | 부하까지의 IR drop을 놓침 | 부하 쪽 sense 지점에서 4-wire/Kelvin |
| 15 cm 그라운드 리드로 리플 측정 | 루프가 자기장을 주워 가짜 스파이크 (HWLAB-B05) | spring-tip, 팁–GND 단락 테스트 |

## 8. 면접 Q&A

<details>
<summary>Q1. 리플 측정에 20 MHz BW limit을 켜는 게 정당한 경우와 아닌 경우는?</summary>

정당한 경우: (1) 스펙이 그렇게 정의돼 있을 때 (예: ATX12V 설계 가이드는 리플/잡음을 20 MHz 대역 제한, 출력 지점에 10 µF 전해 + 0.1 µF 세라믹을 단 조건으로 측정하게 한다), (2) 부하가 그 이상 주파수를 자체 디커플링으로 걸러서 실제로 보지 못할 때, (3) 리플 기본파·주요 고조파가 20 MHz보다 충분히 낮을 때(fsw 2 MHz → 10차 고조파까지 통과).
정당하지 않은 경우: fsw가 높은 컨버터(수 MHz 이상, GaN 등)라 리플 자체가 잘릴 때, 스파이크가 부하의 abs-max나 PLL/ADC/RF 레일 잡음 한도에 영향을 줄 때, 그리고 **프로빙이 나빠서 생긴 가짜 스파이크를 숨기려고** 쓸 때(프로빙을 고쳐야 한다).
꼬리질문: "그럼 스파이크는 어떻게 보고하나?" → 전대역에서 별도 항목(pk, 발생 시각·주기성)으로. 이 레슨의 `find_spikes` 처럼 잔차로 분리하면 에지 동기 여부까지 알 수 있다.

</details>

<details>
<summary>Q2. 10x 패시브 프로브로 mV 리플을 재면 무엇이 문제고, 대안은?</summary>

10x 프로브는 신호를 1/10로 줄이므로 스코프 입력 잡음이 **팁 기준으로 10배** 가 된다. 가장 민감한 V/div 설정도 팁 기준 10배 거칠어진다. 대안: 1x 패시브(잡음은 좋지만 케이블 용량 때문에 대역폭이 보통 수~수십 MHz로 낮고 부하 용량이 크다), 50 Ω 동축 직결 + DC 블록(고대역, 하지만 DC를 못 보고 레일에 50 Ω 부하/DC 블록 고려 필요), **1:1 파워 레일 프로브** (예: Keysight N7020A, Tektronix TPR 계열 — 프로브 안에서 큰 DC 오프셋을 빼 주고 높은 대역폭 유지).
꼬리질문: "그라운드는?" → 15 cm 리드는 ~150 nH, 루프 면적이 자기장을 주워 수십 mV 가짜 스파이크를 만든다 (HWLAB-B05). spring-tip이나 PCB에 미리 놓은 프로브 포인트(동축 커넥터/패드 쌍)를 쓴다. 검증: 팁을 같은 점 GND에 대고 찍었을 때 보이는 것은 전부 측정 체인 픽업이다.

</details>

<details>
<summary>Q3. ΔV = ΔI/(8·f·C)를 유도해 보세요. 실측 리플이 이 식의 7배라면?</summary>

인덕터 전류 AC 성분은 ±ΔI/2 삼각파, 그 양(+)인 T/2 동안 커패시터로 들어가는 전하 = ½·(T/2)·(ΔI/2) = ΔI·T/8 → ΔV = ΔI/(8fC). ESR 항 ΔI·ESR, ESL 항 ESL·di/dt가 더해지며 위상이 달라 pk-pk는 단순 합보다 작다.
7배면: 유효 C가 1/7 (미실장, 깨진 MLCC, DC-bias derating), ESR이 큰 커패시터로 대체(폴리머 → 전해), L이 잘못 실장돼 ΔI가 커짐(L 1/7), 혹은 측정이 틀림(스파이크 포함 전대역 Vpp). 이 레슨 VDD_0V8은 `C_eff ≈ ΔI/(8·f·Vpp)` = 4.2 µF로 첫 번째를 지목했다.
꼬리질문: "어떻게 확정하나?" → 리플 파형 모양 (포물선이면 C 지배, 삼각파면 ESR 지배, 사각 계단이면 ESL), 보드에서 LCR 미터로 C 측정, BOM/X-ray 확인. L이 의심되면 인덕터 전류를 전류 프로브로 직접 보고 ΔI를 잰다.

</details>

<details>
<summary>Q4. 레일 DC ±3 %를 스코프 Vavg로 판정해도 되나요?</summary>

권하지 않는다. 8비트 스코프의 DC 게인 정확도는 대개 풀스케일의 1–2 % 대이고 오프셋 오차가 더해진다(모델별 데이터시트). ±3 % 판정에 오차 예산 대부분을 측정기가 먹는다. DC는 6½자리 DMM으로, 부하 쪽 sense 지점에서, 가능하면 부하 조건(무부하/최대 부하)별로 잰다. 스코프 Vavg는 "대략 맞는지" 교차 확인과 리플·과도용.
꼬리질문: "DMM으로 쟀는데 3.192 V다. 다음은?" → 부하를 바꿔도 같은 % 오차면 FB 분압 저항(실장 값 측정)이나 PMIC의 출력 전압 설정 레지스터/OTP를 확인한다. 부하에 따라 커지면 IR drop·원격 sense 배선 확인.

</details>

<details>
<summary>Q5. Vpp와 RMS 중 어느 쪽으로 스펙을 쓰나요? Vpp의 약점은?</summary>

Vpp는 부하가 실제로 보는 순간 최악값(전압 윈도우 위반)과 직결돼서 레일 스펙에 많이 쓴다. 약점은 통계적으로 불안정하다는 것: 가우시안 잡음의 최댓값은 샘플 수에 따라 커진다 (연습 4 실측: σ = 1 mV 잡음의 Vpp가 1k/10k/100k 샘플에서 6.24/7.24/8.60 mV). 그래서 Vpp 스펙에는 BW와 기록 길이(또는 시간 창, 평균/누적 방식)가 같이 있어야 재현된다. RMS는 안정적이지만 드문 큰 스파이크를 숨긴다. 실무에서는 Vpp(BW 명시) + RMS를 같이 보고하고, 잡음 성분이 큰 경우 백분위 pk-pk(예: 0.5–99.5 %)를 쓰기도 한다.

</details>

<details>
<summary>Q6. 디지털로 BW limit을 흉내낼 때 조심할 점은? 스코프 안의 BW limit과 무엇이 다른가?</summary>

(1) 프리워핑해서 −3 dB를 정확히 fc에, (2) 시작 과도응답(상태 0 → 레일 전압 스텝) 제거, (3) 원본 샘플링이 스파이크를 제대로 잡을 만큼 빨라야 한다 — 이미 에일리어싱된 데이터는 디지털 필터로 되돌릴 수 없다. 스코프의 아날로그 BW limit은 ADC **앞** 에 있어 고주파 잡음이 에일리어싱되기 전에 걸러지고, 8비트 ADC의 동적 범위도 리플에 더 쓸 수 있다. 일부 스코프는 BW limit을 DSP로 구현하기도 하므로, 그때는 이 레슨의 방식과 개념이 같다. 필터 차수에 따라 결과가 달라지므로 (연습 2: 1차 8.56 / 2차 8.18 / 4차 8.00 mV) 같은 스코프·같은 설정으로 비교한다.
꼬리질문: "필터 지연은?" → 2차 Butterworth의 DC 군지연은 √2/(2π·fc) ≈ 11 ns. 리플 Vpp에는 영향이 없지만 잔차(full − filtered)로 스파이크를 분리할 때 빠른 리플 경사에서 작은 잔차를 만든다. 여기서는 임계값 8 mV에 비해 무시할 만했다.

</details>

<details>
<summary>Q7. 리플이 fsw가 아닌 주파수로 보인다면 무엇을 의심하나요?</summary>

경부하에서 PFM/burst 모드로 들어가 낮은 주파수의 큰 리플이 생기는 경우(데이터시트의 모드 설정, 강제 PWM 옵션 확인), 제어 루프 불안정(보상 부품 오류, 출력 C가 바뀌어 위상 여유가 줄어듦 — C_out 미실장이 리플과 함께 루프 안정성도 망칠 수 있다), 다른 컨버터와의 비트(beat) 주파수, 부하 쪽 주기적 전류(펌웨어 루프). 리플 주파수 측정은 이 레슨 연습 1처럼 스파이크 간격이나 BW-limited 신호의 평균 교차로 한다.

</details>

## 9. 추가 연습

### 연습 1 — 스파이크 타이밍으로 fsw · 듀티 · Vin 역산

`find_spikes` 이벤트에서 양(+) 스파이크 = ON 에지, 음(−) = OFF 에지. fsw, D, 그리고 이상적 벅 관계 Vin ≈ Vout/D를 구하라.

<details>
<summary>답 보기</summary>

T = (마지막 ON − 첫 ON)/(개수−1), D = 평균(OFF − 직전 ON)/T. 두 이벤트 모두 "첫 임계값 초과 시각"이라 검출 지연이 비슷해 차이에서 상쇄된다. 실측: fsw = 2.0000 MHz, D = 0.3608, Vin ≈ 4.997 V. 실제 컨버터는 손실 때문에 D가 Vout/Vin보다 조금 크다 → Vin 추정이 조금 낮게 나온다. 입력 레일 전압을 따로 프로빙하지 않고도 "이 벅이 어느 레일에서 먹고 있는지"를 확인하는 요령이다.

</details>

### 연습 2 — BW limit 필터 모양: 1차 / 2차 / 4차

1차 RC(`a = exp(−2π·fc/fs)`)로 BW limit을 구현해 2차·4차 Butterworth와 Vpp(20 MHz), 스파이크 누설, 잡음 σ 비를 비교하라.

<details>
<summary>답 보기</summary>

실측 Vpp: 1차 8.56 / 2차 8.18 / 4차 8.00 mV (정답 8.21). 스파이크만 남긴 신호의 누설 pk: 1차 4.26 / 2차 1.56 / 4차 1.28 mV. 잡음 σ 비 1차 0.1614 (이론 √((π/2)·fc/(fs/2)) = 0.1585). 1차 필터는 180 MHz에서 −19 dB밖에 안 깎아 스파이크가 더 샌다. 결론: "20 MHz BW limit"이라는 말만으로는 숫자가 0.5 mV 단위까지 재현되지 않는다. 이 데이터처럼 마진이 크면 상관없지만, 한도 근처에서는 같은 스코프·같은 설정으로 비교해야 한다.

</details>

### 연습 3 — 수직 분해능과 클리핑 (8비트 ADC)

`scope_quantize(v, vdiv, offset)`: 화면 = offset ± 4 div, 256 코드. 500 mV/div(offset 0), 10 mV/div(offset 1.8 V), 2 mV/div(offset 1.8 V) 세 설정에서 20 MHz Vpp와 클리핑 샘플 수를 구하라.

<details>
<summary>답 보기</summary>

500 mV/div: LSB = 4 V/256 = 15.625 mV — 리플(8 mV)이 1 LSB보다 작아 코드가 두어 개 사이에서만 튄다. 필터 후 Vpp 4.37 mV (기준 8.18 mV) — 틀린데 심지어 작게 틀려 **좋아 보인다**. 10 mV/div + 1.8 V offset: LSB 0.312 mV, 클리핑 0, Vpp 8.19 mV. 2 mV/div: 화면 ±8 mV를 스파이크가 넘어 457 샘플 클리핑 — 클리핑된 Vpp는 화면 높이로 잘린다. 스코프의 과입력(overdrive) 회복 시간 문제까지 생길 수 있다. 습관: 파형이 화면 세로의 대부분을 쓰되 넘치지 않게, DC는 offset으로 빼기.

</details>

### 연습 4 — Vpp는 기록 길이에 따라 커진다

σ = 1 mV 백색 잡음 100k 샘플에서 앞 1k/10k/100k의 Vpp, RMS, 0.5–99.5 % 백분위 pk-pk를 비교하라.

<details>
<summary>답 보기</summary>

Vpp 6.24 → 7.24 → 8.60 mV로 계속 커진다 (가우시안의 최댓값은 대략 √(2 ln N)·σ 속도로 증가). RMS는 0.9977, 0.9990 mV로 σ에 수렴. 0.5–99.5 % pk-pk는 5.093, 5.120 mV ≈ 2·2.576σ = 5.15σ로 안정적. 그래서 "Vpp = 30 mV" 스펙은 측정 창(시간/주기 수, 반복 누적 여부)을 같이 정해야 재현된다. 스코프 무한 지속(infinite persistence)으로 몇 분 누적한 Vpp는 한 번 캡처한 Vpp보다 반드시 크다.

</details>

### 연습 5 — DC + AC 합산 윈도우

BW-limited 순간값의 min/max가 nominal ±3 % 안인지 검사하라. VDD_0V8은 DC PASS(+0.10 %)지만 리플 FAIL — 윈도우는?

<details>
<summary>답 보기</summary>

VDD_1V8: 1.7986…1.8067 V (윈도우 1.746…1.854) 여유 충분. VDD_0V8: 0.7785…0.8173 V vs 0.776…0.824 → 윈도우는 **간신히 PASS, 여유 2.47 mV**. 즉 SoC가 "총 허용오차 ±3 %(DC+AC)"만 요구한다면 이 레일은 지금 당장은 통과하지만, 부하 과도(언더슈트)가 조금만 더해지면 넘는다. 그리고 C_out이 빠진 상태라 부하 과도 언더슈트는 훨씬 커진다. 리플 스펙과 윈도우 스펙은 다른 질문이므로 둘 다 본다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 16/16 passed, exit=0
```

## 10. 핵심 정리

- 리플 숫자는 **레일 + 측정 체인**의 결과다. 리포트에 BW, 프로브(10x/1:1), 그라운드 방식, 기록 길이를 적는다.
- 벅 리플: ΔI = (Vin−Vout)·D/(L·fsw), ΔV_C = ΔI/(8·fsw·C), ΔV_ESR = ΔI·ESR, ESL 계단. pk-pk는 단순 합이 아니다. 실측에서 C_eff = ΔI/(8·fsw·Vpp)로 역산하면 미실장 커패시터를 짚는다.
- 20 MHz BW limit = 저역통과. 2차 Butterworth: K = tan(π·fc/fs), b = K²[1,2,1]·norm, a1 = 2(K²−1)·norm, a2 = (1−K/Q+K²)·norm. DC 이득 1, fc에서 −3 dB.
- 디지털 필터는 DC 정상 상태로 초기화 (안 하면 1.8 V 스텝 → 가짜 1.88 V Vpp). ENBW 2차 = 1.11·fc → 잡음 σ × √(ENBW/(fs/2)).
- 스파이크가 전대역 Vpp를 5배 부풀렸다 (40.9 vs 8.2 mV). 스파이크는 잔차(full − BW-limited)로 분리해 **따로** 보고하고, 에지 동기 여부로 출처를 가린다. BW limit은 진짜 문제(VDD_0V8 38.8 mV)를 숨기지 못한다.
- DC ±3 %는 DMM으로 sense 지점에서. 스코프 DC 정확도는 1–2 % 대라 예산을 다 먹는다. 고정 % 오차 → FB 저항/설정, 부하 의존 → IR drop.
- Vpp는 기록 길이·필터 차수·V/div(양자화/클리핑)에 따라 변한다. 같은 조건으로만 비교한다.

## 참고자료

- R. W. Erickson, D. Maksimović, *Fundamentals of Power Electronics* (3rd ed., Springer 2020) — 벅 CCM 리플, 커패시터 리플 유도
- TI SLVA477, *Basic Calculation of a Buck Converter's Power Stage* · TI SLVA057, *Understanding Buck Power Stages in Switchmode Power Supplies*
- S. M. Sandler, *Power Integrity: Measuring, Optimizing, and Troubleshooting Power Related Parameters in Electronics Systems* (McGraw-Hill 2014) — 레일 프로브, 1:1 측정, 그라운드 루프
- E. Bogatin, *Signal and Power Integrity — Simplified* (3rd ed.) — PDN, 프로브 루프 인덕턴스
- Intel ATX12V Power Supply Design Guide — 리플/잡음 측정 조건(20 MHz BW, 측정 지점 커패시터)
- A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing* — bilinear transform, 프리워핑, Butterworth
- R. Bristow-Johnson, *Audio EQ Cookbook* (W3C 노트로 공개) — 바이쿼드 계수 형식
- Keysight InfiniiVision / Rigol / Tektronix 오실로스코프 프로그래밍 매뉴얼 — `BWLimit`, `MEASure:VPP` 등 SCPI (모델별 확인)
- [PyVISA 문서](https://pyvisa.readthedocs.io/)
- 관련 레슨: HWLAB-B04 (IEEE 181 측정 알고리즘), HWLAB-B05 (스코프 BW·프로브·그라운드 리드 링잉), HWLAB-B06 (트리거), HWLAB-B09 (스모크 테스트·돌입 전류), SIV-A06 (전원 시퀀싱)
