---
id: HWLAB-M07
time: 80분
updated: 2026-11-08
---
# HWLAB-M07 클럭 지터 측정: Period·Cycle-to-Cycle·TIE

## 학습 목표

- 스코프 파형에서 **보간 임계 교차 + 히스테리시스**로 상승 에지 타임스탬프를 뽑고, 그 하나의 목록에서 세 지표(TIE, period jitter, cycle-to-cycle jitter)를 계산한다. TIE의 기준이 되는 이상 클럭은 **최소제곱(least squares) 직선**으로 추정한다.
- 세 지표의 관계를 식과 숫자로 설명한다: `c2c = Δ(period)`, `period − T = Δ(TIE)` (항등식), 백색 TIE σ에서 period = √2·σ, c2c = √6·σ, 사인 PJ에서 이득 2·sin(πν), 4·sin²(πν).
- **SSC가 TIE를 지배하는** 현상(−0.5 %, 31.5 kHz 삼각파 → TIE ≈ δ·Tm/8 ≈ 19.8 ns, RJ 2 ps의 약 10⁴배)과, 같은 클럭에서 c2c는 SSC를 거의 못 본다는 것을 보인다.
- 측정 체인의 함정(공칭 주파수 기준 TIE의 ppm 경사, 에지 위 샘플 부족에 의한 보간 오차, 전압 잡음 → 지터 바닥)을 수치로 재고 걷어낸다.
- 이더넷 PHY 25 MHz 기준 클럭 두 보드 캡처로 "SSC 스트랩이 잘못 켜졌다"를 잡아내는 리포트를 쓴다 — 그리고 c2c 하나만 봤으면 놓쳤다는 것을 보인다.

## 1. 문제

PCIe refclk·이더넷 PHY 클럭·DDR 클럭 스펙에 쓰인 "jitter"는 **서로 다른 세 숫자**다. 어느 것인지 모르고 재면 SSC가 정상적으로 켜진 클럭을 "TIE 20 ns"로 FAIL 시키거나, SSC가 켜지면 안 되는 클럭을 "c2c 38 ps, PASS"로 통과시킨다 — Rivian/NVIDIA/Apple 실리콘·보드 검증 면접의 단골 질문 "period, c2c, TIE의 차이와 관계를 설명하고, 어떤 상황에서 어떤 것을 보나?"에 숫자로 답하는 것이 이 레슨의 목표다.

모든 지표의 원재료는 **에지 타임스탬프** `e[0], e[1], …, e[N−1]` (상승 에지가 임계값을 지난 시각) 하나다.

| 지표 | 정의 | 무엇에 민감한가 | 흔한 스펙 표기 |
|---|---|---|---|
| TIE (Time Interval Error) | `TIE[n] = e[n] − (a + b·n)` — 이상 클럭 대비 각 에지의 시간 오차 | 모든 주파수 성분, 특히 **저주파** (wander, SSC) | 위상 지터, 누적 지터. PLL/CDR 필터를 거친 값으로 스펙화 (HWLAB-M08) |
| period jitter | `P[n] = e[n+1] − e[n]` 의 평균 대비 편차 (rms, pk-pk) | 중·고주파, 저주파는 약하게 | 데이터시트 "period jitter rms / pk-pk (10k cycles)", DDR `tJIT(per)` |
| cycle-to-cycle | `C[n] = P[n+1] − P[n]` | 고주파, 저주파는 거의 안 봄 | "c2c jitter (max over 1000 cycles)", DDR `tJIT(cc)` |

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">에지 하나의 목록 → TIE / period / c2c</text>
  <text x="10" y="46" fill="currentColor" font-size="12">이상 클럭 a + b·n (최소제곱)</text>
  <g stroke="currentColor" stroke-dasharray="4 3" opacity=".55">
    <line x1="90" y1="56" x2="90" y2="185"/><line x1="220" y1="56" x2="220" y2="185"/><line x1="350" y1="56" x2="350" y2="185"/>
    <line x1="480" y1="56" x2="480" y2="185"/><line x1="610" y1="56" x2="610" y2="185"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="40,150 96,150 96,90 158,90 158,150 212,150 212,90 274,90 274,150 362,150 362,90 424,90 424,150 480,150 480,90 542,90 542,150 604,150 604,90 666,90 666,150 730,150"/>
  <g class="f-acc" stroke="currentColor"><circle cx="96" cy="120" r="4"/><circle cx="212" cy="120" r="4"/><circle cx="362" cy="120" r="4"/>
    <circle cx="480" cy="120" r="4"/><circle cx="604" cy="120" r="4"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="96" y="80">e0</text><text x="212" y="80">e1</text><text x="362" y="80">e2</text><text x="480" y="80">e3</text><text x="604" y="80">e4</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="90" y1="178" x2="96" y2="178"/><line x1="212" y1="178" x2="220" y2="178"/><line x1="350" y1="178" x2="362" y2="178"/>
    <line x1="604" y1="178" x2="610" y2="178"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="93" y="198">TIE0 +</text><text x="216" y="198">TIE1 −</text><text x="356" y="198">TIE2 +</text><text x="480" y="198">TIE3 ≈ 0</text><text x="607" y="198">TIE4 −</text>
  </g>
  <g stroke="currentColor"><line x1="96" y1="222" x2="212" y2="222"/><line x1="212" y1="236" x2="362" y2="236"/>
    <line x1="362" y1="222" x2="480" y2="222"/><line x1="480" y1="236" x2="604" y2="236"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="154" y="216">P0 (짧음)</text><text x="287" y="252">P1 (김)</text><text x="421" y="216">P2</text><text x="542" y="252">P3</text>
  </g>
  <text x="10" y="276" fill="currentColor" font-size="12">period: P[n] = e[n+1] − e[n] = T + (TIE[n+1] − TIE[n])</text>
  <text x="10" y="294" fill="currentColor" font-size="12">c2c: C[n] = P[n+1] − P[n] = TIE[n+2] − 2·TIE[n+1] + TIE[n]</text>
  <text x="10" y="316" fill="currentColor" font-size="12">TIE = 어디에 있나 · period = 얼마나 걸렸나 · c2c = 직전보다 얼마나 바뀌었나 (차분할수록 저주파가 사라진다)</text>
</svg>
```

### Part A — 에지 타임스탬프와 세 지표

- `edge_times(v, dt, thr, hyst)` — 균일 샘플 `v[i]`(시각 `i·dt`)에서 상승 에지의 `thr` 교차 시각. `thr − hyst` 아래로 내려가야 무장(arm), `thr + hyst` 이상이면 에지 확정, 그 사이 `thr`를 **마지막으로** 넘은 샘플 쌍에서 선형 보간 (HWLAB-B04의 히스테리시스 상태기계를 에지 시각 전용으로 줄인 것).
- `c2c_direct(edges)` — 에지에서 직접 2차 차분 `e[n+2] − 2e[n+1] + e[n]`.
- `tie_ls(edges)` — `e[n] ≈ a + b·n` 최소제곱, `TIE = e − (a + b·n)`, `T_fit = b`.
- 정답이 알려진 합성 캡처로 확인: 100 MHz HCSL refclk(차동 ±0.75 V, 10–90 % 0.8 ns, 임계 0 V)를 20 GS/s로 2048 에지. (1) 지터 없는 +25 ppm 클럭 → 추출 에지 == 참 에지, LS 주파수 == +25 ppm. (2) 백색 RJ 2 ps → 항등식 두 개가 부동소수 정밀도로 성립, TIE/period/c2c rms = 2 / 2.83 / 4.90 ps.

### Part B — 측정이 거짓말하는 곳, 그리고 SSC

- 공칭 100 MHz를 기준으로 TIE를 재면 +25 ppm이 2048 사이클 동안 **512 ps 경사**로 남는다 → LS 기준이 필요한 이유.
- 에지 위 샘플이 적으면(5 GS/s, 10–90 % 위 4 샘플) 선형 보간 오차가 깨끗한 클럭에서도 1.3 ps rms "가짜 지터"가 된다.
- 전압 잡음 σ_v는 지터 σ_v/slew로 바뀐다 — 깨끗한 클럭으로 바닥(floor)을 재고 **제곱합으로 빼서** 주입한 RJ 1.5 ps를 복원한다.
- 사인 PJ 2 MHz·25 MHz를 넣고 `sine_amp()`(최소제곱 사인 피팅)로 TIE/period/c2c 진폭을 재서 차분의 주파수 응답 2·sin(πν), 4·sin²(πν)을 확인한다.
- SSC −0.5 % down-spread, 31.5 kHz 삼각파: TIE pk-pk ≈ δ·Tm/8 = 19.84 ns, period rms ≈ √(2σ² + (δT0)²/12) = 14.71 ps, c2c rms ≈ √6·σ = 4.90 ps (SSC 무관).

### Part C — NETBRD-C: 이더넷 PHY 25 MHz 클럭에 SSC가 켜진 보드

가상의 보드 **NETBRD-C rev A**. 클럭 생성기 U12의 25 MHz LVCMOS 출력 `CLK25_PHY`가 이더넷 PHY U20의 XI 핀으로 간다. U12의 `SS_EN` 스트랩은 PHY 출력 뱅크에 SSC를 켜는 핀인데, 이 보드에서는 **풀업 R231이 DNP여야 한다**. SN0108은 조립 오류로 R231이 실장됐다(심어 둔 결함). TP44(U20 XI 옆)에서 액티브 프로브로 64 µs를 5 GS/s, 8-bit로 찍은 캡처 두 장(`SN0107_tp44_clk25.bin`, `SN0108_tp44_clk25.bin`)을 `spec_clk25_phy.json`(평균 ±50 ppm, 순간 ±100 ppm, SSC 불허, period rms ≤ 20 ps, |c2c| ≤ 100 ps, TIE ≤ 500 ps pk-pk — **대표값**, 데이터시트 확인 필요)으로 판정하고, `ssc_profile()`로 spread와 변조 주파수를 추정해 원인을 지목한다.

## 2. Problem (English)

> **Measure clock jitter from a scope capture: period, cycle-to-cycle, and TIE.**
>
> Using only the Python 3 standard library:
>
> 1. `edge_times(v, dt, thr, hyst, t0=0) -> list[float]`: rising-edge threshold-crossing times from uniformly sampled
>    volts, with hysteresis (arm below `thr-hyst`, fire at `>= thr+hyst`) and linear interpolation between the last
>    sample below `thr` and the next one.
> 2. `tie_ls(edges) -> (tie, T_fit, a)`: fit `e[n] = a + b*n` by least squares; `tie[n] = e[n] - (a + b*n)`.
>    `c2c_direct(edges)`: `e[n+2] - 2e[n+1] + e[n]`. Show numerically that `c2c == diff(period)` and
>    `period - T_fit == diff(tie)`.
> 3. `sine_amp(x, nu)`: least-squares amplitude of a sinusoid at normalized frequency `nu` (cycles/sample). Inject
>    periodic jitter of amplitude A at `f_pj` and verify the period and c2c amplitudes are `2 sin(pi nu) A` and
>    `4 sin^2(pi nu) A` with `nu = f_pj / f_clk`.
> 4. Show that triangular down-spread SSC (spread `delta`, rate `fm`) produces TIE peak-to-peak ≈ `delta / (8 fm)`
>    while barely changing c2c.
> 5. `ssc_profile(edges, W)` (moving-average instantaneous frequency → spread and modulation rate) and `judge()` for
>    an Ethernet PHY reference clock where SSC is not allowed; report PASS/FAIL per item.
>
> Example: 100 MHz, white TIE jitter σ = 2 ps → TIE rms ≈ 2.0 ps, period rms ≈ 2.83 ps, c2c rms ≈ 4.90 ps.
> With −0.5 % SSC at 31.5 kHz: TIE pk-pk ≈ 19.8 ns, period rms ≈ 14.7 ps, c2c rms still ≈ 4.9 ps.

## 3. 시작 코드

<!-- code: starter.py -->

Part C 캡처 생성기 (결정적 — 다시 돌려도 `.bin`이 바이트 단위로 같다)와 스펙 파일:

<!-- code: gen_capture.py -->

<!-- code: spec_clk25_phy.json -->

```bash
cd lessons/HWLAB/HWLAB-M07
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 2/19 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: edges -> period / c2c / TIE --
FAIL  edge_times: 2048 edges, interpolated == injected truth (clean): n=2048 mean(e-truth)=24.667ps rms=14.567ps
FAIL  tie_ls: least-squares frequency == 100MHz +25ppm (+-0.01ppm): f_fit=100.002443MHz -> +24.4266ppm (truth +25)
...
FAIL  white RJ: tie_rms == 2ps (+-5%): 14.976ps
...
PASS  SSC dominates TIE: pkpk ~= delta*Tm/8 = 19.84ns (+-3%), > 1000x RJ   [19.946ns vs 19.841ns]
FAIL  SSC in period jitter: rms ~= sqrt(2 RJ^2 + (delta T0)^2/12) = 14.71ps (+-5%): 25.267ps vs 14.708ps
...
2/19 passed
exit=1
```

> 눈여겨볼 것: starter의 `edge_times`는 보간 없이 "넘어선 첫 샘플"(snap)을 돌려준다. 20 GS/s(50 ps 간격)인데도 RJ 2 ps 클럭의 TIE가 **15 ps rms**로 나온다 — 샘플 격자 양자화(dt/√12 ≈ 14.4 ps)가 지터처럼 보이는 것이다. 그런데 SSC 테스트는 starter로도 PASS다. SSC의 20 ns는 어떤 엉터리 측정으로도 보일 만큼 크다. **"SSC가 TIE를 지배한다"는 말 그대로다.**

## 4. 힌트

<details>
<summary>힌트 1 — 히스테리시스와 '마지막' 교차</summary>

`last_below`는 `x < thr`일 때마다 갱신한다(무장 여부와 무관). `x <= thr − hyst`면 `armed = True`, `armed`이고 `x >= thr + hyst`면 `(last_below, last_below + 1)` 사이에서 보간하고 `armed = False`. 잡음이 임계 근처에서 오르내려도 에지는 하나, 시각은 임계를 마지막으로 넘은 구간에서 나온다. `v[j+1] >= thr > v[j]`가 보장되므로 0으로 나눌 일이 없다.

</details>

<details>
<summary>힌트 2 — 최소제곱 직선을 정밀하게</summary>

에지 시각은 1e-5 s 크기인데 보고 싶은 것은 1e-13 s다(비율 1e-8). `e − e[0]`를 쓰고, n도 평균 `n̄ = (N−1)/2`를 빼면 정규방정식이 대각이 된다: `b = Σ(k−n̄)(e_k−ē)/Σ(k−n̄)²`, `Σ(k−n̄)² = N(N²−1)/12`, `a = ē − b·n̄`. 이렇게 하면 항등식 오차가 1e-20 s 수준이다.

</details>

<details>
<summary>힌트 3 — 차분의 주파수 응답</summary>

`TIE[n] = A·sin(2πνn)`이면 `TIE[n+1] − TIE[n] = 2A·sin(πν)·cos(2πν(n + ½))`. 진폭이 `2·sin(πν)`배. 한 번 더 차분하면 또 `2·sin(πν)`배 → c2c는 `4·sin²(πν)`. ν = f_pj/f_clk 이고 에지가 사이클마다 한 번이므로 ν ≤ 0.5까지만 의미가 있다.

</details>

<details>
<summary>힌트 4 — 사인 진폭은 록인(lock-in)처럼</summary>

FFT 빈에 정확히 안 맞는 주파수도 `x ≈ p·cos + q·sin` 최소제곱이면 정확하다. 평균을 빼고 `Scc, Scs, Sss, Sxc, Sxs`를 누적해 2×2를 풀면 끝(크래머 공식). 진폭 = `hypot(p, q)`.

</details>

<details>
<summary>힌트 5 — SSC 프로파일은 W 주기 평균 주파수</summary>

주기 W개의 합은 `e[k+W] − e[k]` 한 번의 뺄셈이다. 분수 편차 `T_fit·W/(e[k+W] − e[k]) − 1`의 max−min이 spread. 변조 주파수는 이 곡선이 가운데 선을 ±spread/4 히스테리시스로 몇 번 건너는지 세서: 교차 c개가 `span` 초에 걸쳐 있으면 반 주기가 `span/(c−1)` → `fm = (c−1)/(2·span)`.

</details>

## 5. 풀이

### Part A — 에지 하나의 목록, 세 개의 창

**측정 체인.** 스코프의 지터 소프트웨어(Keysight EZJIT, Tektronix DPOJET 류)가 하는 일은 결국 아래와 같다. 이 레슨의 `capture()`가 정확히 이 경로를 탄다(합성 파형 → `edge_times` → 지표).

```svg
<svg viewBox="0 0 760 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm07-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">측정 체인과 두 개의 항등식</text>
  <rect x="10" y="40" width="120" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <rect x="170" y="40" width="150" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="360" y="40" width="170" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <rect x="570" y="40" width="180" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <rect x="360" y="130" width="170" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <rect x="570" y="130" width="180" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="70" y="60">파형 v[i]</text><text x="70" y="76">(20 GS/s, 8-bit 등)</text>
    <text x="245" y="60">edge_times()</text><text x="245" y="76">히스테리시스 + 보간</text>
    <text x="445" y="60">LS 직선 a + b·n</text><text x="445" y="76">T_fit = b</text>
    <text x="660" y="60">TIE[n] = e[n] − (a+b·n)</text><text x="660" y="76">rms · pk-pk</text>
    <text x="445" y="150">P[n] = e[n+1] − e[n]</text><text x="445" y="166">period jitter</text>
    <text x="660" y="150">C[n] = P[n+1] − P[n]</text><text x="660" y="166">cycle-to-cycle</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M130 62 L168 62" marker-end="url(#hwm07-arr)"/><path d="M320 62 L358 62" marker-end="url(#hwm07-arr)"/>
    <path d="M530 62 L568 62" marker-end="url(#hwm07-arr)"/><path d="M245 84 L245 152 L358 152" marker-end="url(#hwm07-arr)"/>
    <path d="M530 152 L568 152" marker-end="url(#hwm07-arr)"/><path d="M660 84 L660 128" stroke-dasharray="4 3" marker-end="url(#hwm07-arr)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="666" y="110">Δ: P − T_fit = ΔTIE</text><text x="536" y="192">Δ: C = ΔP = Δ²TIE</text>
  </g>
</svg>
```

**왜 최소제곱 기준인가.** TIE는 "이상 클럭"과의 차이인데, 이상 클럭의 주파수를 무엇으로 잡느냐가 곧 측정 정의다. 공칭값(100.000000 MHz)으로 잡으면 오실레이터의 ppm 오차가 TIE에 **직선 경사**로 남는다(Part B). 스코프의 "constant frequency" 클럭 복원 옵션이 바로 이 LS 직선이다. "PLL" 옵션은 기준 클럭이 저주파 지터를 따라가게 해서 TIE를 고역 통과시킨다 — 그 모델과 PCIe의 필터 함수는 DSP-A06·HWLAB-M08 몫이다.

**두 항등식.** `P[n] = e[n+1] − e[n]`이고 `TIE[n] = e[n] − a − b·n`이므로 `TIE[n+1] − TIE[n] = P[n] − b`. 한 번 더 빼면 `C[n] = P[n+1] − P[n] = TIE[n+2] − 2TIE[n+1] + TIE[n]`. 기준 직선이 무엇이든(LS든 공칭이든) 성립한다 — 차분이 상수와 기울기를 지우기 때문이다. 그래서 **period와 c2c는 기준 클럭 선택에 무관하고, TIE만 기준에 의존한다.** 실제 출력:

```text
PASS  edge_times: 2048 edges, interpolated == injected truth (clean)   [n=2048 mean(e-truth)=-0.001ps rms=0.022ps]
PASS  tie_ls: least-squares frequency == 100MHz +25ppm (+-0.01ppm)   [f_fit=100.002500MHz -> +24.9999ppm (truth +25)]
PASS  identities: c2c == diff(period), period - T_fit == diff(TIE)   [max|c2c-dP|=4.1e-25s max|(P-T)-dTIE|=5.1e-21s]
      RJ 2ps: tie_rms=2.006ps period_rms=2.796ps c2c_rms=4.769ps  (ratios 1.394, 2.377)
```

**백색 TIE에서 √2와 √6.** RJ가 에지마다 독립인 가우스 σ(PLL이 잠근 클럭의 고주파 위상 잡음을 단순화한 모델)라면:

| 지표 | 식 | 분산 | rms (σ = 2 ps) | 실측 |
|---|---|---|---|---|
| TIE | `x[n]` | σ² | 2.000 ps | 2.006 ps |
| period | `x[n+1] − x[n]` | (1+1)σ² | √2·σ = 2.828 ps | 2.796 ps |
| c2c | `x[n+2] − 2x[n+1] + x[n]` | (1+4+1)σ² | √6·σ = 4.899 ps | 4.769 ps |

c2c/period = √3 ≈ 1.73 (실측 1.705, 연습 2). 반대로 **자유 발진**(링 오실레이터 등 주기마다 독립 잡음 σ_p)이면 period = σ_p, c2c = √2·σ_p, 비율 √2 — TIE는 랜덤워크라 레코드가 길수록 커진다. 그래서 c2c/period 비율만 봐도 지터의 "색"을 가늠할 수 있다. 2048 샘플의 rms 추정 상대 오차는 약 1/√(2N) ≈ 1.6 %라서 테스트 허용 오차를 ±5 %(≈3σ)로 잡았다.

**pk-pk는 rms와 다르다.** 가우스 RJ의 pk-pk는 샘플 수와 함께 계속 자란다(최댓값 통계). 그래서 데이터시트의 "period jitter pk-pk"는 반드시 "몇 사이클에 대해"가 붙는다. BER 목표에서 피크를 환산하는 dual-Dirac은 HWLAB-M10, 욕조 곡선은 HWLAB-M11.

### Part B — 측정이 거짓말하는 곳

실제 출력:

```text
PASS  TIE vs nominal 100MHz: +25ppm leaves a 512ps ramp; LS reference removes it   [nominal-ref TIE pkpk=515.318ps (ppm ramp 511.750ps) vs LS TIE pkpk=13.780ps]
PASS  too few samples per edge: 5GS/s interpolation error shows up as >1ps 'jitter'   [clean +25ppm: tie_rms @5GS/s=1.315ps  @20GS/s=0.022ps]
PASS  noise floor: 3mV/slew adds jitter; subtract in quadrature -> RJ 1.5ps (+-10%)   [floor=1.171ps (sigma_v/slew=1.456ps) measured=1.917ps -> sqrt(meas^2-floor^2)=1.518ps vs injected 1.5ps]
PASS  PJ 2MHz (nu=0.02): period = 2sin(pi nu)*TIE, c2c = 4sin^2(pi nu)*TIE   [TIE=10.011ps period=1.244ps (2sin=0.1256->1.256ps) c2c=0.155ps (4sin^2=0.0158->0.158ps)]
PASS  PJ 25MHz (nu=0.25): period = 2sin(pi nu)*TIE, c2c = 4sin^2(pi nu)*TIE   [TIE=10.007ps period=14.148ps (2sin=1.4142->14.142ps) c2c=20.010ps (4sin^2=2.0000->20.000ps)]
      SSC: tie_pkpk=19.902ns tie_rms=7.264ns period_rms=14.783ps c2c_rms=4.931ps
PASS  SSC dominates TIE: pkpk ~= delta*Tm/8 = 19.84ns (+-3%), > 1000x RJ   [19.902ns vs 19.841ns]
PASS  SSC in period jitter: rms ~= sqrt(2 RJ^2 + (delta T0)^2/12) = 14.71ps (+-5%)   [14.783ps vs 14.708ps]
PASS  SSC invisible in c2c: rms ~= sqrt6 * 2ps = 4.899ps (+-5%)   [4.931ps]
```

**① 공칭 기준 TIE의 ppm 경사.** +25 ppm이면 사이클마다 0.25 ps씩 앞서고, 2047 사이클 뒤엔 511.75 ps. 실측 515.3 ps = 경사 + RJ 꼭대기. LS 기준으론 13.8 ps pk-pk(RJ 2 ps의 약 ±3.4σ). 공칭 기준 TIE가 쓸모 있는 경우도 있다 — "이 클럭이 외부 기준(GPS 1PPS, 다른 보드)과 얼마나 벌어지나"를 볼 때. 하지만 그건 지터가 아니라 주파수 오차 + wander 측정이다.

**② 에지 위 샘플 수와 보간 오차.** 선형 보간은 에지가 곡선이면 샘플 위상에 따라 다른 편향을 낸다. 클럭과 샘플링이 정확히 정수비면 그 편향은 상수라 안 보이지만, ppm 오차만 있어도 샘플 위상이 천천히 돌면서 **느린 패턴의 가짜 TIE**가 된다. `demo_sweeps.py` 표 2 (실제 출력):

```text
table 2: clean 100MHz +25ppm clock, 10-90% = 0.8ns, linear-interpolated 0V crossing, 2048 edges
  fs        samples on 10-90   |e - truth| max   TIE rms (fake jitter)
   5GS/s     4.0                1.879ps           1.315ps
  10GS/s     8.0                0.240ps           0.170ps
  20GS/s    16.0                0.030ps           0.022ps
  40GS/s    32.0                0.004ps           0.003ps
```

샘플링률을 2배로 할 때 오차가 약 8배 준다(≈ dt³). tanh 에지의 50 % 지점은 변곡점이라 2차 곡률이 0이어서, 선형 보간의 첫 오차 항이 3차이기 때문이다. 임계값을 변곡점이 아닌 곳(예: 단일 종단 신호를 20 %에서)에 두면 2차 항이 살아나 더 나빠진다. 실제 스코프는 sin(x)/x 보간을 쓰고 지터 측정에 10–90 % 위 수 샘플 이상을 권한다(대역 제한된 신호를 충분히 샘플링했을 때만 보간이 정당하다는 점은 HWLAB-B04·B05 참고).

**③ 전압 잡음 → 지터 바닥.** 임계 근처 전압 오차 Δv는 시간 오차 Δv/slew가 된다. 여기서 slew = (A/2)/tc = 0.75 V / 0.364 ns = 2.06 V/ns, 3 mV → 1.456 ps. 실측 바닥 1.171 ps는 그 0.80배인데, 보간이 두 샘플의 잡음을 가중 평균하기 때문이다: 교차가 두 샘플 사이 비율 f에 있으면 잡음 분산이 `(1−f)² + f²`배(0.5~1), f가 고르게 분포하면 평균 2/3 → rms √(2/3) = 0.816배.

바닥과 신호는 독립이라 **분산이 더해진다**: σ_meas² = σ_RJ² + σ_floor². 그래서 깨끗한 기준 클럭(또는 스코프 자체 교정 신호)으로 같은 셋업의 바닥을 재고 빼면 √(1.917² − 1.171²) = 1.518 ps → 주입값 1.5 ps. 단, 이 뺄셈은 **조건이 나쁘다**. 2048 에지 rms 추정의 상대 오차는 약 1/√(2N) = 1.6 %이고, 제곱하면 3.1 %. 잡음 3 mV(측정 1.917, 바닥 1.171 ps)에선 σ_RJ² = 2.30 ± 0.12 ps² → σ_RJ ±2.7 %지만, 잡음 6 mV(같은 코드로 돌린 실제 값: 바닥 2.345, 측정 2.805 → 복원 1.540 ps)에선 ±0.30 ps² → ±6.3 %로 2.4배 나빠진다(이번 시드에선 운 좋게 맞았다). 바닥이 측정값에 다가갈수록 오차는 발산한다. 경험칙: 바닥이 측정값의 1/2 아래일 때만 빼서 보고하고, 아니면 "측정 한계 이하"라고 쓴다.

| 원인 | 증상 | 드러내는 측정 / 대책 |
|---|---|---|
| 공칭 주파수 기준 TIE | TIE에 직선 경사, 레코드 길수록 커짐 | LS(constant frequency) 기준, ppm은 따로 보고 |
| 에지 위 샘플 부족 | ppm에 따라 느린 패턴의 TIE, 깨끗한 클럭도 ps급 | 샘플링률 ↑, sin(x)/x 보간, 깨끗한 소스로 바닥 확인 |
| 보간 없음(snap) | 지터 ≈ dt/√12 (20 GS/s에서 14 ps) | 보간 필수 |
| 전압 잡음·8-bit 양자화 | σ_v/slew 만큼 지터 바닥 | V/div를 화면 가득, 차동/액티브 프로브, 바닥 측정 후 제곱합 뺄셈 |
| 느린 에지(작은 slew) | 같은 잡음이 더 큰 지터 | 임계를 가장 가파른 곳(50 %, 차동 0 V)에 |
| 스코프 타임베이스 지터 | 레코드 길이에 따라 커지는 바닥 | 데이터시트의 jitter measurement floor 확인 |

**④ 차분 = 고역 통과 필터.** TIE에 사인 PJ `A·sin(2πν n)`를 넣으면 period 진폭은 `2·sin(πν)·A`, c2c는 `4·sin²(πν)·A`. 이건 근사가 아니라 선형 연산의 정확한 주파수 응답이다. `demo_sweeps.py` 표 1 (실제 출력):

```text
table 1: PJ 10ps injected, RJ 0.3ps, 2048 edges @ 20GS/s
  f_pj      nu     TIE amp   period amp (gain / 2sin(pi nu))   c2c amp (gain / 4sin^2(pi nu))
     1MHz  0.010   10.016ps    0.630ps (0.0629 / 0.0628)            0.040ps (0.0039 / 0.0039)
     2MHz  0.020   10.025ps    1.259ps (0.1256 / 0.1256)            0.158ps (0.0158 / 0.0158)
     5MHz  0.050   10.032ps    3.139ps (0.3129 / 0.3129)            0.982ps (0.0979 / 0.0979)
    10MHz  0.100   10.020ps    6.193ps (0.6180 / 0.6180)            3.827ps (0.3820 / 0.3820)
    25MHz  0.250   10.021ps   14.172ps (1.4142 / 1.4142)           20.042ps (2.0000 / 2.0000)
    40MHz  0.400   10.047ps   19.110ps (1.9021 / 1.9021)           36.349ps (3.6180 / 3.6180)
    45MHz  0.450   10.026ps   19.805ps (1.9754 / 1.9754)           39.123ps (3.9021 / 3.9021)
```

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">TIE → period → c2c 의 이득 (ν = f_jitter / f_clk)</text>
  <line x1="80" y1="280" x2="690" y2="280" stroke="currentColor"/><line x1="80" y1="280" x2="80" y2="30" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".4">
    <line x1="80" y1="220" x2="680" y2="220"/><line x1="80" y1="160" x2="680" y2="160"/><line x1="80" y1="100" x2="680" y2="100"/><line x1="80" y1="40" x2="680" y2="40"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="72" y="284">0</text><text x="72" y="224">1</text><text x="72" y="164">2</text><text x="72" y="104">3</text><text x="72" y="44">4</text></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="80" y="298">0</text><text x="200" y="298">0.1</text><text x="320" y="298">0.2</text>
    <text x="440" y="298">0.3</text><text x="560" y="298">0.4</text><text x="680" y="298">0.5</text><text x="380" y="320">ν (0.5 = f_clk/2: 에지당 한 번 샘플링되는 지터의 나이퀴스트)</text></g>
  <line x1="80" y1="220" x2="680" y2="220" stroke="currentColor" stroke-width="2"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="8 4" points="80,280.0 92,276.2 104,272.5 116,268.7 128,265.0 140,261.2 152,257.5 164,253.8 176,250.2 188,246.5 200,242.9 212,239.4 224,235.8 236,232.3 248,228.9 260,225.5 272,222.2 284,218.9 296,215.7 308,212.5 320,209.5 332,206.5 344,203.5 356,200.6 368,197.9 380,195.1 392,192.5 404,190.0 416,187.5 428,185.2 440,182.9 452,180.8 464,178.7 476,176.7 488,174.8 500,173.1 512,171.4 524,169.9 536,168.4 548,167.1 560,165.9 572,164.8 584,163.8 596,162.9 608,162.1 620,161.5 632,160.9 644,160.5 656,160.2 668,160.1 680,160.0"/>
  <polyline fill="none" stroke="currentColor" stroke-width="3" points="80,280.0 92,279.8 104,279.1 116,277.9 128,276.2 140,274.1 152,271.6 164,268.6 176,265.2 188,261.3 200,257.1 212,252.5 224,247.5 236,242.1 248,236.5 260,230.5 272,224.3 284,217.8 296,211.1 308,204.2 320,197.1 332,189.8 344,182.5 356,175.0 368,167.5 380,160.0 392,152.5 404,145.0 416,137.5 428,130.2 440,122.9 452,115.8 464,108.9 476,102.2 488,95.7 500,89.5 512,83.5 524,77.9 536,72.5 548,67.5 560,62.9 572,58.7 584,54.8 596,51.4 608,48.4 620,45.9 632,43.8 644,42.1 656,40.9 668,40.2 680,40.0"/>
  <g class="f-acc" stroke="currentColor"><circle cx="104" cy="272.5" r="4"/><circle cx="380" cy="195.1" r="4"/><circle cx="104" cy="279.1" r="4"/><circle cx="380" cy="160.0" r="4"/></g>
  <g fill="currentColor" font-size="12">
    <text x="560" y="214">TIE (이득 1)</text><text x="520" y="186">period: 2·sin(πν)</text><text x="430" y="92">c2c: 4·sin²(πν)</text>
    <text x="112" y="262">ν=0.02 (2 MHz): 0.126 / 0.016</text><text x="388" y="152">ν=0.25: 1.414 / 2.0</text>
    <text x="96" y="60">저주파(SSC, wander, 벅 리플)는</text><text x="96" y="76">period 에서 약하게, c2c 에서 거의 0</text>
  </g>
</svg>
```

해석 — 실무에서 이게 뜻하는 것:

- **저주파 지터(ν ≪ 1)는 period에서 `2πν`배, c2c에서 `(2πν)²`배로 줄어든다.** 1 MHz 벅 컨버터 리플이 100 MHz 클럭에 10 ps PJ를 만들어도 period엔 0.63 ps, c2c엔 0.04 ps만 보인다. 그런데 그 클럭을 받는 SerDes PLL·CDR의 추적 대역 밖이면 그 10 ps가 그대로 아이(eye)를 갉아먹는다. "c2c 스펙 만족 = 지터 문제 없음"이 아니다.
- **ν = 0.5 근처(f_clk/2) 지터는 c2c에서 4배로 커진다.** 클럭을 2분주하는 회로의 짝·홀 사이클 비대칭, f_clk/2 크로스토크가 그렇다. c2c 스펙은 이런 "사이클마다 번갈아 흔들림"을 잡으려고 존재한다(DDR 등 동기 인터페이스의 setup/hold는 바로 앞 사이클 대비 변화에 민감).
- ν > 0.5인 지터는 에지당 한 번 샘플링되므로 **에일리어싱**된다 — 상승 에지만 쓰는 지터 분석은 f_clk/2 위를 구분 못 한다.

**⑤ SSC가 TIE를 지배한다.** SSC(spread-spectrum clocking)는 EMI 피크를 낮추려고 주파수를 일부러 천천히 흔든다. PCIe refclk의 대표 사양은 down-spread 최대 −0.5 %, 변조 30–33 kHz(대표값 — PCIe Base Spec 확인 필요). 분수 주파수 편차 y(t)가 0 ↔ −δ 삼각파이면:

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SSC 삼각파 주파수 → TIE 는 그 적분 (포물선 조각)</text>
  <line x1="80" y1="95" x2="700" y2="95" stroke="currentColor" stroke-dasharray="4 4" opacity=".5"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="80,95.0 96,102.0 111,109.0 126,116.0 142,123.0 158,130.0 173,123.0 188,116.0 204,109.0 220,102.0 235,95.0 250,88.0 266,81.0 282,74.0 297,67.0 312,60.0 328,67.0 344,74.0 359,81.0 374,88.0 390,95.0 406,102.0 421,109.0 436,116.0 452,123.0 468,130.0 483,123.0 498,116.0 514,109.0 530,102.0 545,95.0 560,88.0 576,81.0 592,74.0 607,67.0 622,60.0 638,67.0 654,74.0 669,81.0 684,88.0 700,95.0"/>
  <g fill="currentColor" font-size="12"><text x="8" y="64">y = 0</text><text x="8" y="99">평균 −δ/2</text><text x="8" y="134">y = −δ</text>
    <text x="360" y="150">주파수가 평균보다 낮으면 에지가 늦어진다 → TIE 증가</text></g>
  <g stroke="currentColor"><line x1="312" y1="40" x2="622" y2="40"/><line x1="312" y1="35" x2="312" y2="45"/><line x1="622" y1="35" x2="622" y2="45"/></g>
  <text x="440" y="36" fill="currentColor" font-size="12">Tm = 1/fm ≈ 31.7 µs</text>
  <line x1="80" y1="235" x2="700" y2="235" stroke="currentColor" stroke-dasharray="4 4" opacity=".5"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="80,270.0 96,268.6 111,264.4 126,257.4 142,247.6 158,235.0 173,222.4 188,212.6 204,205.6 220,201.4 235,200.0 250,201.4 266,205.6 282,212.6 297,222.4 312,235.0 328,247.6 344,257.4 359,264.4 374,268.6 390,270.0 406,268.6 421,264.4 436,257.4 452,247.6 468,235.0 483,222.4 498,212.6 514,205.6 530,201.4 545,200.0 560,201.4 576,205.6 592,212.6 607,222.4 622,235.0 638,247.6 654,257.4 669,264.4 684,268.6 700,270.0"/>
  <g stroke="currentColor"><line x1="720" y1="200" x2="720" y2="270"/><line x1="714" y1="200" x2="726" y2="200"/><line x1="714" y1="270" x2="726" y2="270"/></g>
  <g fill="currentColor" font-size="12"><text x="8" y="239">TIE</text><text x="600" y="192">pk-pk = δ·Tm/8</text>
    <text x="80" y="292">0.5 % · 31.75 µs / 8 = 19.84 ns ≈ 2 UI @100 MHz — RJ 2 ps 의 약 10⁴ 배</text></g>
</svg>
```

- **TIE**: x(t) = −∫(y − ȳ)dt. 삼각파의 반 주기 동안 (y − ȳ)의 넓이 = ½·(Tm/2)·(δ/2) = δ·Tm/8 → **pk-pk = δ/(8·fm)**. 반송파 주파수와 무관하다! 0.5 %, 31.5 kHz면 19.84 ns. 실측 19.90 ns.
- **period**: P = T0/(1+y) ≈ T0(1 − y)가 0 ~ δ·T0 사이 삼각파 → 값이 균등 분포 → rms = δ·T0/√12 = 14.43 ps. RJ와 제곱합: √(2·2² + 14.43²) = 14.71 ps. 실측 14.78 ps. **period jitter도 SSC에 오염된다.**
- **c2c**: 연속 두 주기의 차 = 삼각파 기울기 × 1 사이클 = δ·T0/(Tm·f0/2) = 50 ps/1587 ≈ 0.03 ps. RJ c2c 4.90 ps에 묻혀 **안 보인다**. 실측 4.93 ps.

한 가지 함정: SSC 클럭에 LS 기준을 쓰면 **TIE와 평균 주파수가 창(window)의 위치·길이에 따라 바뀐다**. 테스트는 변조 정확히 2주기, 대칭 위상(u0 = 0.25)으로 잡아 LS 기울기 보정이 0이 되게 했다. Part C 캡처처럼 임의 위상·2.02주기면 TIE pk-pk 22.4 ns, LS 평균 주파수 −2640 ppm(끝점 기준 −2470 ppm, 이론 평균 −2500 + 12 = −2488 ppm)으로 나온다. 그래서 SSC 클럭의 TIE 스펙은 constant-frequency 기준이 아니라 **수신기의 CDR/PLL 추적을 흉내 낸 필터**를 거쳐 정의된다(PCIe refclk 지터 = 필터 함수 적용 — HWLAB-M08, PLL 클럭 복원은 DSP-A06).

### Part C — NETBRD-C 두 보드 판정

`python3 -I solution.py`의 Part C 실제 출력:

```text
NETBRD-C rev A SN0107 CLK25_PHY@TP44 -> PASS  edges=1600 f=25.000300MHz
  ok   freq_ppm   = +12.0ppm                   |.| <= 50ppm
  ok   inst_ppm   = -6..+33ppm                 |.| <= 100ppm
  ok   ssc        = none                       not allowed
  ok   period_rms = 5.5ps                      <= 20ps
  ok   c2c_absmax = 31.1ps                     <= 100ps
  ok   tie_pkpk   = 30.7ps                     <= 500ps
  info: period_pkpk=39.1ps c2c_rms=9.32ps tie_rms=4.8ps
NETBRD-C rev A SN0108 CLK25_PHY@TP44 -> FAIL  edges=1596 f=24.933998MHz
  BAD  freq_ppm   = -2640.1ppm                 |.| <= 50ppm
  BAD  inst_ppm   = -4888..-81ppm              |.| <= 100ppm
  BAD  ssc        = spread 0.48% @ 31.5kHz     not allowed
  BAD  period_rms = 58.7ps                     <= 20ps
  ok   c2c_absmax = 38.5ps                     <= 100ps
  BAD  tie_pkpk   = 22399.5ps                  <= 500ps
  info: period_pkpk=216.0ps c2c_rms=10.26ps tie_rms=6663.5ps
PASS  SN0108 SSC detected: spread 0.5% (+-10%), fm 31.5kHz (+-5%)   [spread=0.482% fm=31.48kHz (injected 0.50%, 31.5kHz)]
```

**숫자를 생성기 파라미터로 맞춰 보기.** 생성기(`gen_capture.py`)에 넣은 참값: +12 ppm, RJ 3 ps, PJ 4 ps @ 1.2 MHz, 전압 잡음 4 mV + 8-bit(LSB 14.06 mV), 10–90 % 2 ns(중앙 slew 1.81 V/ns). 잡음·양자화만 넣고 따로 돌려 본 측정 오차는 에지당 약 2.6 ps rms였다.

| 항목 | 예상 | SN0107 실측 | SN0108 실측 |
|---|---|---|---|
| TIE rms | √(3² + (4/√2)² + 2.6²) ≈ 4.9 ps | 4.8 ps | 6.66 ns (SSC) |
| period rms | √2·√(3² + 2.6²) ≈ 5.6 ps (PJ 기여 ≈ 0.85 ps) | 5.5 ps | 58.7 ps ≈ √(57.7² + 5.6²) |
| c2c rms | √6·√(3² + 2.6²) ≈ 9.7 ps | 9.32 ps | 10.26 ps (SSC 기여 사이클당 0.5 ps뿐) |
| spread / fm | — / 0.50 %, 31.5 kHz | 0.0039 % | 0.48 %, 31.48 kHz |

spread가 0.48 %로 조금 작게 나오는 이유: W = 32 주기(1.28 µs) 이동평균이 삼각파 꼭짓점을 깎는다. 기울기 = 5000 ppm/397 사이클 ≈ 12.6 ppm/사이클, 꼭짓점에서 약 W/4·기울기 ≈ 100 ppm씩 → 순간 주파수 범위 −4888..−81 ppm(참값 −4988..+12).

**리포트 (하드웨어 엔지니어에게 넘길 한 줄):**

`FAIL NETBRD-C SN0108 net=CLK25_PHY@TP44 (U20 XI): SSC detected — down-spread 0.48 % @ 31.5 kHz, f_inst −4888..−81 ppm (spec ±100), f_mean −2640 ppm (±50), TIE 22.4 ns pk-pk, period 58.7 ps rms; c2c 38.5 ps max는 PASS(c2c는 SSC를 못 봄). 같은 셋업 SN0107 +12.0 ppm, TIE 30.7 ps → 측정 체인 아님. U12 SS_EN 스트랩: R231 실장 확인 → DNP.`

- **왜 문제인가**: 이더넷 PHY는 25 MHz 기준으로 송신 심볼 클럭을 만든다. IEEE 802.3의 송신 주파수 허용치는 PHY 종류에 따라 ±50~±100 ppm 수준(PHY 데이터시트는 보통 25 MHz ±50 ppm 요구 — 대표값, 확인 필요)인데 −0.5 %는 −5000 ppm. 링크 상대방의 수신 CDR·elastic buffer가 감당하도록 설계된 범위를 수십 배 넘는다 → 링크가 안 붙거나, 붙어도 패킷 손실·FIFO 오버/언더플로.
- **c2c만 봤으면 놓쳤다**: 38.5 ps는 SN0107의 31.1 ps와 구분이 안 된다. SSC는 ν ≈ 31.5 kHz/25 MHz = 0.00126 — c2c 이득 4·sin²(πν) ≈ 6×10⁻⁵. 반대로 **period trend**(주기를 시간축으로 그린 것)를 보면 삼각파가 바로 보인다 — 스코프에서 SSC를 확인하는 표준 방법이 이것이다.
- **원인 지목**: spread·fm이 생성기 SSC 사양(0.5 %, 30–33 kHz)과 일치 → 오실레이터 불량이 아니라 SSC가 **켜진** 것. 같은 BOM의 다른 보드가 정상이면 조립(스트랩 저항) 차이다. PCIe refclk 뱅크는 SSC가 정상일 수 있으므로 "U12 전체 SSC 끄기"가 아니라 PHY 뱅크 스트랩만 지목한다.

**장비가 있을 때 (이 레슨에선 실행하지 않음):** 스코프 지터 옵션(EZJIT/DPOJET 류)에서 TIE·period·c2c 측정을 켜고, 클럭 복원을 "constant frequency"로 하면 이 레슨의 `tie_ls`와 같은 정의가 된다. 측정 trend(시간축)와 jitter spectrum 뷰를 같이 본다. 파형을 PyVISA로 받아(프리앰블 + BYTE/WORD 데이터 — HWLAB-B04 참고) 이 코드에 넣으면 스코프 결과와 두 의견을 비교할 수 있다. 스코프 데이터시트의 jitter measurement floor를 리포트에 같이 적는다. ppm의 정밀 판정은 스코프 타임베이스(수 ppm급)가 아니라 주파수 카운터로 한다(SIV-A06).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"                   # 19/19 passed, exit=0 (약 1.1 s)
python3 -I solution.py --report SN0108_tp44_clk25.bin      # 캡처 하나만 리포트
python3 -I -W error -X dev solution.py                     # 경고 없음 (exit 0)
python3 -I gen_capture.py                                  # .bin 재생성 → sha256 동일 (결정적)
python3 -I demo_sweeps.py                                  # §5 Part B 의 표 1·표 2
```

<!-- code: demo_sweeps.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 공칭 주파수로 TIE 기준을 잡음 | ppm 오차가 TIE에 경사로 남아 레코드 길이에 비례해 커짐 (+25 ppm → 512 ps) | `tie_ls` 최소제곱 직선, ppm은 따로 보고 |
| 기준 직선을 첫·마지막 에지 두 점으로 | 끝 에지 두 개의 지터가 기울기를 흔듦 (starter: +24.43 ppm) | 모든 에지로 LS |
| `e[n]`를 그대로 넣어 LS | 1e-5 s 값에서 1e-13 s를 보려다 정밀도 손실 | `e − e[0]`, n 평균 제거 → 대각 정규방정식 |
| 보간 없이 샘플 시각 | 지터 ≈ dt/√12 (20 GS/s에서 14.4 ps) | `(j + (thr − v[j])/(v[j+1] − v[j]))·dt` |
| 히스테리시스 없이 임계 하나 | 잡음이 임계 근처에서 오르내리면 에지 2배 | arm(thr−h) / fire(thr+h) |
| 10–90 % 위 샘플 4개로 ps 지터 판정 | 보간 편향이 샘플 위상 따라 변해 1.3 ps 가짜 지터 | 샘플링률 ↑, 깨끗한 소스로 바닥 확인 |
| c2c 스펙만 보고 "지터 OK" | c2c는 저주파(SSC, 벅 리플)를 (2πν)²배로 지움 | TIE·period trend·ppm도 함께 |
| SSC 클럭의 constant-frequency TIE를 스펙과 비교 | SSC는 의도된 변조, TIE는 창 위치·길이에 의존 | 스펙이 정한 필터/CDR 모델 적용 (M08, DSP-A06) |
| period pk-pk를 샘플 수 없이 보고 | 가우스 성분의 pk-pk는 샘플 수와 함께 증가 | "N cycles" 명시, rms와 함께 |
| 바닥(floor)을 측정값에서 그냥 빼기 | rms는 제곱합 | √(σ_meas² − σ_floor²), 바닥이 측정의 1/2 이상이면 빼지 말 것 |
| 지터 측정에 BW limit/평균 모드 | 평균(averaging)은 지터를 뭉개고, BW limit은 slew를 줄여 바닥을 키움 | 단일 획 + 충분한 BW, 통계는 에지 단위로 |

## 8. 면접 Q&A

<details>
<summary>Q1. Period jitter, cycle-to-cycle jitter, TIE의 차이와 관계를 설명해 주세요.</summary>

셋 다 같은 에지 타임스탬프에서 나온다. TIE[n] = 에지 − 이상 클럭(보통 LS 직선), period P[n] = e[n+1] − e[n], c2c = P[n+1] − P[n]. 정확한 항등식: P − T = ΔTIE, c2c = ΔP = Δ²TIE. 차분은 주파수 응답 2·sin(πν)인 고역 통과 필터라서 TIE → period → c2c로 갈수록 저주파가 사라지고 f_clk/2 근처는 커진다(c2c 최대 4배). 백색 TIE σ에서 period = √2σ, c2c = √6σ.
꼬리질문: "자유 발진 링 오실레이터라면?" → 주기 잡음이 독립(σ_p)이라 period = σ_p, c2c = √2·σ_p, TIE는 랜덤워크로 레코드 길이와 함께 √N으로 커진다(이 레슨 연습 3: tERR(n) = 2, 4, 8 ps @ n = 1, 4, 16). c2c/period 비율 √3 vs √2로 둘을 구분할 수 있다.

</details>

<details>
<summary>Q2. PCIe refclk의 TIE가 20 ns pk-pk로 나왔습니다. 고장인가요?</summary>

SSC가 켜져 있다면 정상일 수 있다. −0.5 % down-spread, 31.5 kHz 삼각파의 TIE pk-pk = δ/(8·fm) = 19.8 ns이고 반송파 주파수와 무관하다. 확인: period trend에 삼각파가 보이는지, spread와 fm이 사양 범위(대표값 0~−0.5 %, 30–33 kHz)인지. PCIe 수신 측 CDR/PLL은 이 느린 변화를 추적하므로, 스펙 판정은 constant-frequency TIE가 아니라 스펙의 필터 함수(송·수신 PLL 전달 함수 차이, CDR 추적)를 적용한 지터로 한다(HWLAB-M08).
꼬리질문: "SSC를 쓰는데 왜 30 kHz대인가?" → 가청 대역(약 20 kHz) 위에 두면서, 수신 PLL/CDR이 충분히 추적할 수 있을 만큼 느리게 둔 타협이다. 변조가 빨라지면 같은 spread에서 TIE는 줄지만(δ/8fm) 추적 오차가 커진다. "SSC가 있으면 period jitter는?" → δ·T0/√12만큼 오염된다(100 MHz에서 14.4 ps rms) — period jitter 스펙도 SSC 끄고 재거나 스펙 정의를 확인해야 한다.

</details>

<details>
<summary>Q3. 1 ps 이하 RJ를 스코프로 잴 수 있나요? 측정 바닥은 무엇이 정하나요?</summary>

바닥 = 스코프 타임베이스 지터 + 전압 잡음/slew + 보간 오차. 전압 잡음 σ_v는 σ_v/slew의 지터가 되므로(이 레슨: 3 mV / 2.06 V/ns ≈ 1.46 ps, 보간 가중치로 실측 1.17 ps), V/div를 화면 가득 채우고, 차동/액티브 프로브와 가장 가파른 지점(차동 0 V)을 쓴다. 깨끗한 소스로 같은 셋업의 바닥을 재고 √(σ_meas² − σ_floor²)로 뺄 수 있지만, 바닥이 측정값에 가까우면 뺄셈의 오차가 커진다. sub-ps RJ는 보통 위상 잡음 분석기로 L(f)를 적분해 구한다(HWLAB-M08).
꼬리질문: "샘플링률은 얼마나 필요한가?" → 이 레슨의 표 2: 10–90 % 위 4 샘플이면 깨끗한 클럭에서도 1.3 ps rms 가짜 지터, 16 샘플이면 0.02 ps. 정수비 샘플링이면 편향이 상수라 숨어 있다가 주파수가 조금만 달라도 패턴으로 나타난다.

</details>

<details>
<summary>Q4. 1 MHz 벅 컨버터 리플이 클럭에 10 ps PJ를 만들고 있습니다. period jitter 스펙으로 잡을 수 있나요?</summary>

거의 못 잡는다. 100 MHz 클럭에서 ν = 0.01 → period 이득 2·sin(0.01π) = 0.063(0.63 ps), c2c 이득 0.0039(0.04 ps). TIE에는 10 ps가 그대로 있다. 이 클럭이 SerDes 기준 클럭이면 1 MHz는 대개 송신 PLL 대역 안이라 출력으로 전달되고, 수신 CDR이 추적하는지가 관건이다 → 지터 전달 함수로 판정(HWLAB-M08, DSP-A06). 디버깅은 TIE 스펙트럼에서 1 MHz 피크를 찾고, 벅 스위칭 주파수를 바꾸거나 PLL 전원 필터(페라이트 + 커패시터, LDO)를 보강해 피크가 움직이는지 본다.
꼬리질문: "그럼 c2c 스펙은 왜 있나?" → 동기 인터페이스의 다음 사이클 타이밍 마진은 '직전 대비 변화'에 민감하고, f_clk/2 근처 성분(분주기 비대칭, 크로스토크)은 c2c에서 최대 4배로 드러나기 때문이다.

</details>

<details>
<summary>Q5. DDR 스펙의 tJIT(per), tJIT(cc), tERR(nper)를 이 레슨의 용어로 바꾸면?</summary>

DDR4(JESD79-4) 계열은 tCK(avg)를 연속 200 사이클 창의 평균 주기로 정의하고, tJIT(per) = tCK(i) − tCK(avg) → period jitter, tJIT(cc) = tCK(i+1) − tCK(i) → c2c, tERR(nper) = 연속 n 주기의 합 − n·tCK(avg) → TIE[k+n] − TIE[k] (n 사이클 누적 오차). 200 사이클 이동 평균이 기준이라 그보다 느린 wander·SSC 성분은 tCK(avg) 자체가 따라간다 — 일종의 '고역 통과된 TIE'다.
꼬리질문: "tERR(n)은 n에 따라 어떻게 변하나?" → 백색 TIE면 n ≥ 1에서 √2σ로 평평, 랜덤워크면 σ_p·√n으로 커진다(연습 3에서 2.83 ps 평평 vs 2→4→8 ps). 그래서 tERR 한계가 n별 표로 주어진다.

</details>

<details>
<summary>Q6. 지터를 rms로 보고하나요, pk-pk로 보고하나요?</summary>

둘 다, 그리고 조건을 붙인다. rms는 샘플 수에 안정적이지만 결정적 성분(PJ, DCD, SSC)의 경계를 표현 못 한다. pk-pk는 가우스 성분 때문에 샘플 수에 따라 커지므로 "10k cycles" 같은 조건이 필수다. 링크 마진에는 BER 목표의 총 지터 TJ(BER) = DJ(δδ) + 2·Q(BER)·RJ (Q(1e-12) ≈ 7.03)를 쓴다(HWLAB-M10). 이 레슨의 SN0107 TIE: rms 4.8 ps, 1600 에지 pk-pk 30.7 ps ≈ 6.4·rms.
꼬리질문: "pk-pk/rms 비율로 무엇을 알 수 있나?" → 순수 가우스면 N 샘플에서 약 2·√(2 ln N)(N=1600 → 7.7, 실측은 그보다 작은 6.4 — 기대값 근사이고 단일 실현의 산포가 크다). 사인 PJ만 있으면 2√2 ≈ 2.83에서 멈춘다. 비율이 작으면 결정적 성분 우세, N과 함께 크면 랜덤 성분 우세 — 분해는 HWLAB-M10.

</details>

<details>
<summary>Q7. 스코프로 잰 평균 주파수 −2640 ppm과 끝점 기준 −2470 ppm, 어느 쪽이 맞나요?</summary>

둘 다 "그 창에서의 정의"일 뿐이다. SSC처럼 주파수가 주기적으로 변하면 LS 기울기는 창 위치·길이에 따라 변조 성분 일부를 흡수한다(Part C 캡처는 2.02 주기, 임의 위상). 끝점 기준은 정수 주기에 가까우면 참 평균(−2500 + 12 = −2488 ppm)에 가깝다. 평균 주파수를 제대로 보려면 변조 주기의 정수배 게이트나 훨씬 긴 게이트의 주파수 카운터를 쓴다. 어쨌든 PHY 판정에는 평균이 아니라 순간 주파수 범위(−4888..−81 ppm)가 결정적이다.
꼬리질문: "스코프 타임베이스로 ±50 ppm 판정이 되나?" → 스코프 타임베이스 정확도는 모델에 따라 수 ppm 수준에 에이징이 더해진다. ±50 ppm 경계 근처라면 교정된 카운터나 외부 10 MHz 기준을 쓴다(SIV-A06).

</details>

## 9. 추가 연습

### 연습 1 — c2c로 RJ 추정하기 (SSC 무관)

백색 TIE 가정에서 σ_RJ ≈ c2c_rms/√6 인 `rj_from_c2c(edges)`. Part B의 SSC 캡처에서 2 ps를 ±5 %로 복원하고, SSC 없는 캡처에선 tie_rms와 ±5 %로 일치하는지 확인한다.

<details>
<summary>답 보기</summary>

c2c는 TIE의 2차 차분이라 분산 (1+4+1)σ². SSC의 기여는 이득 4·sin²(πν) ≈ (2πν)²로 사라진다. 실제 출력: SSC 캡처 c2c/√6 = 2.013 ps (TIE rms는 7.264 ns), SSC 없음 1.947 ps vs tie_rms 2.006 ps. 단, 이 추정은 TIE가 백색이라는 가정에 기댄다 — f_clk/2 근처 PJ가 있으면 c2c가 그걸 4배로 키워 RJ를 과대평가한다. 성분 분해의 정공법은 HWLAB-M10.

</details>

### 연습 2 — 지터의 '색' 판별

자유 발진 클럭 생성기 `free_running_edges(n, f0, σ_p)`(주기마다 독립 잡음, TIE는 랜덤워크)를 만들고 `jitter_color = c2c_rms/period_rms`가 PLL식(백색 TIE) √3, 자유 발진 √2인지 확인한다.

<details>
<summary>답 보기</summary>

백색 TIE: period √2σ, c2c √6σ → √3. 백색 주기: period σ_p, c2c = δ_{n+1} − δ_n → √2σ_p → √2. 실제 출력: 1.705, 1.413 (period_rms = 1.996 ps vs σ_p 2 ps). 실무 해석: PLL이 잠근 클럭의 고주파 위상 잡음은 백색 TIE에 가깝고, PLL 대역보다 느린 영역에선 기준 클럭을 따라가며, 링 오실레이터·RC 오실레이터처럼 잠기지 않은 클럭은 랜덤워크 TIE라 "TIE rms"가 레코드 길이에 의존해 의미가 약하다 — 그런 클럭은 period jitter와 tERR(n)로 스펙화한다.

</details>

### 연습 3 — DDR식 누적 오차 tERR(n)

`terr_rms(edges, n)` = rms(e[k+n] − e[k] − n·T_fit). 백색 TIE와 랜덤워크 캡처에서 n = 1, 4, 16, 64를 비교한다.

<details>
<summary>답 보기</summary>

tERR(n)[k] = TIE[k+n] − TIE[k]. 백색 TIE면 두 독립 값의 차 → √2σ (n ≥ 1에서 평평), 랜덤워크면 n개 독립 주기 잡음의 합 → σ_p√n. 실제 출력:

```text
      n=1   white-TIE tERR rms=2.796ps (sqrt2*2ps=2.828)   random-walk tERR rms=1.996ps (2ps*sqrt n=2.000ps)
      n=4   white-TIE tERR rms=2.843ps (sqrt2*2ps=2.828)   random-walk tERR rms=4.016ps (2ps*sqrt n=4.000ps)
      n=16  white-TIE tERR rms=2.839ps (sqrt2*2ps=2.828)   random-walk tERR rms=8.019ps (2ps*sqrt n=8.000ps)
      n=64  white-TIE tERR rms=2.824ps (sqrt2*2ps=2.828)   random-walk tERR rms=16.232ps (2ps*sqrt n=16.000ps)
```

랜덤워크의 큰 n은 겹치는 창의 수가 적어지고 서로 상관돼 추정이 흔들리므로 테스트는 n ≤ 16만 ±10 %로 본다. JEDEC 정의는 T_fit 대신 200 사이클 창의 tCK(avg)를 쓴다는 점이 다르다.

</details>

### 연습 4 — SSC를 걷어내고 RJ 보기 (블록 2차 detrend)

SSC 캡처의 TIE를 W 에지 블록마다 2차 다항식으로 맞춰 빼고, 자유도 보정 √(W/(W−3))을 곱한 블록 rms의 **중앙값**으로 RJ를 복원하는 `detrend_blocks_rms(tie, W)`. W = 64에서 2 ps ±10 %, W = 1024에서는 실패(> 3·RJ)하는 것을 보인다.

<details>
<summary>답 보기</summary>

SSC의 TIE는 포물선 조각이라 짧은 블록 안에선 2차식으로 거의 완벽히 맞는다. 삼각파 꼭짓점(곡률 부호가 바뀌는 곳)이 든 블록만 잔차가 크고, 중앙값이 그 몇 개를 무시한다. 실제 출력: W = 64 → 2.037 ps, W = 1024 → 131.0 ps(블록마다 꼭짓점·곡률 변화가 들어가 SSC가 샌다). 이건 "느린 성분을 따라가는 기준 클럭" — 즉 PLL 클럭 복원 — 의 조잡한 대용품이다. 블록 길이 W는 PLL 대역폭과 비슷한 역할(1/(W·T))을 한다. 제대로 된 PLL 모델(1·2차 루프, 대역폭 = bitrate/1667 같은 골든 PLL)은 DSP-A06·HWLAB-M09.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 8/8 passed, exit=0 (약 0.9 s)
```

## 10. 핵심 정리

- 원재료는 에지 타임스탬프 하나: 히스테리시스 + 선형 보간 교차, 임계는 가장 가파른 곳(차동 0 V / 50 %).
- TIE = e − (LS 직선), period P = Δe, c2c = ΔP. 항등식: **P − T = ΔTIE, c2c = Δ²TIE** — period·c2c는 기준 클럭 선택에 무관, TIE만 의존.
- 백색 TIE σ → period √2σ, c2c √6σ (비율 √3). 자유 발진(백색 주기) → c2c/period = √2, TIE는 랜덤워크.
- 차분 = 고역 통과: period 이득 2·sin(πν), c2c 이득 4·sin²(πν) (ν = f/f_clk ≤ 0.5). 저주파(SSC·벅 리플)는 c2c에서 사라지고, f_clk/2 근처는 4배.
- 삼각파 SSC: **TIE pk-pk = δ/(8·fm)** (0.5 %, 31.5 kHz → 19.84 ns, 반송파 무관), period rms += δ·T0/√12, c2c ≈ 0.
- 측정 바닥: σ_v/slew(보간이 약 0.8배로 줄임), 에지 위 샘플 수(오차 ∝ dt³), 공칭 기준 TIE의 ppm 경사. 바닥은 제곱합으로 빼되 조건이 나쁘다.
- 판정은 지표 하나로 하지 않는다: SN0108은 c2c 38.5 ps PASS인데 SSC 0.48 % @ 31.5 kHz, 순간 −4888 ppm → R231 DNP.

## 참고자료

- Tektronix, *Understanding and Characterizing Timing Jitter* (primer) — TIE, period, c2c 정의와 측정, RJ/DJ 개요
- SiTime, *AN10007: Clock Jitter Definitions and Measurement Methods* — period/c2c/TIE(phase jitter) 정의와 스펙 표기
- Mike Peng Li, *Jitter, Noise, and Signal Integrity at High-Speed*, Prentice Hall, 2007 — 지터 성분, 전달 함수, 클럭 복원
- JEDEC JESD79-4 (DDR4 SDRAM) — tCK(avg), tJIT(per), tJIT(cc), tERR(nper) 정의
- PCI-SIG, *PCI Express Base Specification* — refclk 주파수 허용치, SSC(down-spread, 30–33 kHz), refclk 지터 필터 함수
- IEEE Std 802.3 — PHY 송신 주파수 허용치 (PHY 데이터시트의 XI 요구사항과 같이 확인)
- Keysight Infiniium EZJIT/EZJIT+ 및 Tektronix DPOJET 사용자 설명서 — 클럭 복원 옵션(constant frequency / PLL), 측정 trend, jitter spectrum
- 관련 레슨: HWLAB-B04(교차 보간·히스테리시스), HWLAB-B05(스코프 BW·프로브), HWLAB-M08(위상 잡음 → RMS 지터, refclk 필터), HWLAB-M09(eye·골든 PLL), HWLAB-M10(RJ/DJ 분해·dual-Dirac), HWLAB-M11(BER 욕조), DSP-A06(PLL 추적), SIV-A06(ppm 판정)
