---
id: HWLAB-B10
time: 70분
updated: 2026-11-08
---
# HWLAB-B10 열 측정 기초: 열전대 냉접점 보상과 Tj 추정

## 학습 목표

- 열전대(thermocouple)가 재는 것은 **두 끝의 온도 차가 만든 전압** `V = E(Thot) − E(Tcj)` 임을 설명하고, NIST ITS-90 K형 기준 함수(°C→mV)와 근사 역함수(mV→°C)를 구현해 NIST 기준표 1643점 전체로 검증할 수 있다.
- 냉접점 보상(CJC, cold-junction compensation)을 **전압 영역에서** 하고, CJC 생략·온도 영역 덧셈·41.276 µV/°C 선형 근사가 각각 몇 °C 틀리는지, 냉접점 센서 오차가 몇 배로 증폭되는지(Seebeck 계수 비) 숫자로 보인다.
- 패키지 열 지표 θJA, θJC, ΨJT의 정의(JEDEC JESD51)를 구분하고, 케이스 윗면 온도와 전력으로 `Tj = Tc + P·ΨJT` 를 계산한다.
- 같은 로그에서 `Tj = Ta + P·θJA` 가 실제 보드에서 **양쪽 방향으로** 틀리는 것(벤치에서는 +31 °C 비관, 밀폐 박스에서는 −16 °C 낙관 → false PASS)을 수치로 설명하고, CJC 누락이라는 심어 둔 결함을 영점 확인·온다이 센서 교차 확인으로 잡는다.

## 1. 문제

열 검증(thermal validation)은 보드 브링업 막바지에 꼭 나오고, 면접에서는 "열전대로 케이스 온도를 쟀다 → Tj가 얼마냐 → 그 숫자를 왜 믿느냐"까지 한 줄로 이어서 묻는다. Rivian·NVIDIA·Apple 검증 면접에서 θJA로 Tj를 계산하거나 CJC를 모르면 바로 걸린다.

열전대는 접점(junction) 하나의 온도를 재는 센서가 **아니다**. 서로 다른 두 금속선(K형: 크로멜 Chromel(+, KP) / 알루멜 Alumel(−, KN))을 따라 온도 기울기가 있으면 Seebeck 효과로 전압이 생기고, 선이 균질하다면 그 전압은 **양 끝 온도에만** 의존한다. 측정 접점(hot junction)은 칩 위에, 다른 끝(냉접점, cold junction = DAQ 단자대)은 보드 온도에 있다. 그래서 단자에서 잰 전압은 두 온도의 *차이* 에 해당하고, 냉접점 온도를 따로 재서 보상해야 한다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb10-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">K형 열전대 측정 체인: 단자 전압 = E(Thot) − E(Tcj)</text>
  <rect x="20" y="70" width="120" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="80" y="95" fill="currentColor" text-anchor="middle">SOC0 패키지</text>
  <text x="80" y="113" fill="currentColor" text-anchor="middle" font-size="12">윗면 Tc (측정 접점)</text>
  <circle cx="140" cy="105" r="7" class="f-warn" stroke="currentColor"/>
  <path d="M147 100 C 230 70, 300 70, 380 92" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M147 110 C 230 140, 300 140, 380 118" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="6 3"/>
  <text x="250" y="66" fill="currentColor" font-size="12">KP 크로멜 (+, ANSI 노랑)</text>
  <text x="250" y="156" fill="currentColor" font-size="12">KN 알루멜 (−, ANSI 빨강)</text>
  <rect x="380" y="60" width="150" height="110" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="455" y="82" fill="currentColor" text-anchor="middle" font-weight="bold">등온 단자대</text>
  <text x="455" y="100" fill="currentColor" text-anchor="middle" font-size="12">K→Cu 접점 2개 = Tcj</text>
  <rect x="420" y="112" width="70" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="455" y="130" fill="currentColor" text-anchor="middle" font-size="12">서미스터</text>
  <text x="455" y="160" fill="currentColor" text-anchor="middle" font-size="12">cj_C 로 기록</text>
  <path d="M530 92 L600 92" stroke="currentColor" marker-end="url(#hwb10-a)"/>
  <path d="M530 118 L600 118" stroke="currentColor" marker-end="url(#hwb10-a)"/>
  <text x="565" y="84" fill="currentColor" text-anchor="middle" font-size="12">Cu</text>
  <rect x="602" y="70" width="120" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="662" y="96" fill="currentColor" text-anchor="middle">차동 ADC</text>
  <text x="662" y="114" fill="currentColor" text-anchor="middle" font-size="12">µV 분해능</text>
  <text x="662" y="130" fill="currentColor" text-anchor="middle" font-size="12">tc_uV 로 기록</text>
  <g fill="currentColor" font-size="13">
    <text x="20" y="205">CJC(전압 영역):  Thot = T⁻¹( V + E(Tcj) )      ← 이 레슨의 정답</text>
    <text x="20" y="228">CJC 생략:          Thot ≈ T⁻¹( V )               ← 대략 Tcj 만큼 낮게 읽음</text>
    <text x="20" y="251">온도 영역 덧셈:   Thot ≈ T⁻¹( V ) + Tcj        ← 비선형이라 수 °C 틀림</text>
    <text x="20" y="272" font-size="12">K→Cu 두 접점이 같은 온도면 구리선은 전압을 더하지 않는다(중간 금속의 법칙) → 단자대는 등온이어야 한다.</text>
  </g>
</svg>
```

그다음 문제는 그 케이스 온도로 **접합 온도(Tj)** 를 추정하는 것이다. 데이터시트에는 θJA, θJC, ΨJT, ΨJB가 나란히 적혀 있고, 가장 눈에 띄는 θJA로 `Ta + P·θJA` 를 계산하는 실수가 가장 흔하다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb10-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">PBGA 패키지의 열 흐름과 열 지표 (JESD51)</text>
  <rect x="40" y="200" width="420" height="22" class="f-mut" stroke="currentColor"/>
  <text x="250" y="216" fill="currentColor" text-anchor="middle" font-size="12">PCB (구리 평면이 열을 퍼뜨린다) — 보드 온도 Tb</text>
  <g fill="currentColor"><circle cx="150" cy="194" r="5"/><circle cx="180" cy="194" r="5"/><circle cx="210" cy="194" r="5"/><circle cx="240" cy="194" r="5"/><circle cx="270" cy="194" r="5"/><circle cx="300" cy="194" r="5"/><circle cx="330" cy="194" r="5"/></g>
  <rect x="130" y="120" width="220" height="68" rx="4" class="f-acc" stroke="currentColor"/>
  <rect x="205" y="160" width="70" height="16" class="f-warn" stroke="currentColor"/>
  <text x="240" y="172" fill="currentColor" text-anchor="middle" font-size="12">die Tj</text>
  <text x="240" y="140" fill="currentColor" text-anchor="middle" font-size="12">몰드 (윗면 Tc = Tt)</text>
  <circle cx="240" cy="116" r="6" class="f-ok" stroke="currentColor"/>
  <text x="252" y="108" fill="currentColor" font-size="12">열전대 비드</text>
  <path d="M240 158 L240 125" stroke="currentColor" marker-end="url(#hwb10-b)" stroke-width="1"/>
  <text x="140" y="96" fill="currentColor" font-size="12">윗면 경로: 작은 몫</text>
  <path d="M240 178 L240 235" stroke="currentColor" marker-end="url(#hwb10-b)" stroke-width="3"/>
  <text x="250" y="252" fill="currentColor" font-size="12">아래 경로(볼 → 보드 → 공기): 대부분</text>
  <path d="M60 196 C 30 150, 40 90, 90 60" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#hwb10-b)"/>
  <text x="20" y="52" fill="currentColor" font-size="12">공기 Ta</text>
  <g fill="currentColor" font-size="12.5">
    <text x="490" y="70" font-weight="bold">정의 (P = 패키지 총 전력)</text>
    <text x="490" y="94">θJA = (Tj − Ta)/P</text>
    <text x="500" y="110" font-size="12">JEDEC 2s2p 보드·정지 공기에서만</text>
    <text x="490" y="134">θJC = (Tj − Tc)/P_case</text>
    <text x="500" y="150" font-size="12">콜드 플레이트: 열을 케이스로만</text>
    <text x="490" y="174">ΨJT = (Tj − Tt)/P</text>
    <text x="500" y="190" font-size="12">저항 아님: 윗면으로 가는 건 P의 일부</text>
    <text x="490" y="214">ΨJB = (Tj − Tb)/P</text>
    <text x="490" y="246" font-weight="bold">보드 위에서 Tj 추정:</text>
    <text x="490" y="266">Tj ≈ Tt + P·ΨJT  (P·Ψ 항이 작다)</text>
  </g>
  <text x="10" y="290" fill="currentColor" font-size="12">ΨJT·P 는 수 °C 이하라 Ψ 값이 조금 틀려도 Tj 오차가 작다.</text>
  <text x="10" y="310" fill="currentColor" font-size="12">θJA·P 는 수십 °C라 보드·공기 조건이 다르면 그대로 틀린다.</text>
</svg>
```

이 레슨의 데이터 (모두 이 디렉터리에 커밋, Python 3 표준 라이브러리만 사용):

| 파일 | 내용 |
|---|---|
| `nist_type_k_table.csv` | NIST SRD 60 `type_k.tab` 의 K형 기준표 −270..1372 °C, 1 °C 간격 1643점 (mV, 소수 3자리). `extract_nist_table.py` 로 추출 |
| `soc0_thermal.json` | 가상 SoC "SOC0" (PBGA-324 17×17 mm) 열 데이터: θJA 20, θJC(top) 7, ΨJT 0.5, ΨJB 10 °C/W, Tj_max 105 °C, 온다이 센서 정확도 ±3 °C. 전형적인 크기의 값이지 특정 부품 값이 아니다 |
| `soak_log.csv` | 두 환경(`bench_fan`, `enclosure`)에서 1200 s soak, 2 s 간격. 열: `tc_uV`(열전대 단자 전압), `cj_C`(단자대 서미스터), `ta_C`(실험실 공기), `tlocal_C`(보드 근처 공기), 레일 V·I 2개, `tsens_C`(온다이 센서). `gen_soak_log.py` 로 결정적으로 재생성 |

### Part A — ITS-90 K형 변환과 전압 영역 CJC

- `emf_mV(t)` — NIST 기준 함수. −270..0 °C는 10차 다항식, 0..1372 °C는 9차 다항식 + 지수항 `a0·exp(a1·(t − a2)²)`. 범위 밖은 `ValueError`.
- `temp_C(e)` — NIST 근사 역함수 3구간(−200..0 / 0..500 / 500..1372 °C). 범위 밖은 `ValueError`.
- `tc_temperature(v, tcj)` — `temp_C(v + emf_mV(tcj))`.
- 검증: 기준표 1643점 전체와 표 반올림(±0.0005 mV) 안에서 일치, 왕복 오차가 NIST 역함수 오차 범위(±0.06 °C) 안.

### Part B — CJC를 틀리면 몇 °C 틀리나

- `seebeck_uV_per_C(t)` — 기준 함수의 해석적 미분 (지수항 포함), `cj_error_gain(th, tcj) = S(tcj)/S(th)`.
- `method_errors(th, tcj)` — 물리대로 만든 단자 전압을 네 방법(CJC 생략 / 온도 영역 덧셈 / 41.276 µV/°C 선형 / NIST 전압 영역)으로 읽은 오차.
- 극저온에서는 CJC 없는 역변환이 정의역(−200 °C)을 벗어나 **값 자체가 안 나온다**는 것도 확인한다.

### Part C — soak 로그 → Tj, θJA는 왜 틀리나

- `package_power_W(row)` (모든 레일 합), `is_steady(ts, ys)` (최소제곱 기울기), `estimate_tj(tc, ta, p, pkg)` (ΨJT / θJA / θJC 세 방식).
- `analyze(rows, pkg, use_cjc)` — 마지막 120 s 평균으로 Tj 추정, 플래그(`NOT_STEADY`, `TC_BELOW_AMBIENT`, `TSENS_MISMATCH`), 판정(`PASS`/`FAIL`/`INVALID`).
- 심어 둔 결함: 레거시 스크립트가 CJC 없이 `temp_C(tc_uV/1000)` 를 썼다. 밀폐 박스에서 Tc를 49 °C 낮게 읽고 "PASS(여유 44 °C)"가 나오는 것을 플래그로 잡아 `INVALID` 로 만든다.

## 2. Problem (English)

> **Thermocouple CJC and junction-temperature estimation.**
>
> A type-K thermocouple is bonded to the top of an SoC package. A DAQ logs the thermocouple terminal voltage (µV), the
> terminal-block (cold-junction) temperature, ambient temperatures, the rail voltages/currents and the on-die sensor.
> Using only the Python standard library:
>
> 1. Implement the NIST ITS-90 type-K reference function `emf_mV(t)` (two ranges, exponential term above 0 °C) and the
>    approximate inverse `temp_C(e)` (three ranges). Validate against the NIST reference table (e.g. 100 °C → 4.096 mV,
>    1000 °C → 41.276 mV). Implement voltage-domain cold-junction compensation `tc_temperature(v, t_cj)`.
> 2. Quantify the error of (a) no CJC, (b) adding `t_cj` in the temperature domain, (c) a single 41.276 µV/°C slope,
>    and the gain from a cold-junction sensor error to the reading (`S(t_cj)/S(t_hot)`).
> 3. From the steady-state part of the log, compute package power, case temperature and
>    `Tj = Tc + P·ΨJT`; compare with `Ta + P·θJA` and the on-die sensor and explain why θJA is wrong on a real board.
>    Flag runs whose thermocouple data is not trustworthy.
>
> Example: hot junction 85 °C, cold junction 35 °C → terminal voltage 2.067 mV; no CJC reads 51.1 °C (−33.9 °C),
> temperature-domain addition reads 86.1 °C (+1.1 °C), voltage-domain CJC reads 85.0 °C.

## 3. 시작 코드

<!-- code: starter.py -->

부품 열 데이터와 로그 생성기 (`python3 -I gen_soak_log.py` 로 `soak_log.csv` 를 바이트 단위로 같게 다시 만든다):

<!-- code: soc0_thermal.json -->

<!-- code: gen_soak_log.py -->

NIST 기준표 추출기 (원본 `type_k.tab` 은 커밋하지 않았다. 받는 방법은 파일 머리 주석과 §참고자료):

<!-- code: extract_nist_table.py -->

```bash
cd lessons/HWLAB/HWLAB-B10
python3 -I starter.py ; echo "exit=$?"     # 2/63 passed, exit=1
```

실제 출력 (일부):

```text
-- data --
PASS  NIST table: 1643 points -270..1372 C
PASS  soak log: 2 scenarios x 601 rows
-- Part A: ITS-90 type K + CJC --
      E(0) measured=None expected=0.0 tol=0.000500000001
FAIL  emf_mV(0) == NIST 0.000 mV (+-0.0005)
...
-- Part B: what CJC mistakes cost --
FAIL  seebeck(0) == 39.451 uV/C  (NotImplementedError: seebeck_uV_per_C)
...
-- Part C: soak log -> Tj --
      P measured=None expected=3.58 tol=0.1
FAIL  package_power_W(row@600s) == 3.58 W (+-0.1)
FAIL  estimate_tj(60, 25, 3.6 W): psi 61.8 / thetaJA 97.0 / thetaJC 85.2  (TypeError: 'NoneType' object is not subscriptable)
...
2/63 passed
```

## 4. 힌트

<details>
<summary>힌트 1 — 다항식은 Horner, 구간은 t의 부호로</summary>

`horner(coefs, x)` 가 이미 있다 (`coefs[0]` 이 상수항). `t < 0` 이면 `K_NEG`, 아니면 `K_POS` + 지수항. 지수항은 0 °C 이상 구간에만 있고, `K_POS[0] = −0.0176` 과 지수항 `a0·exp(a1·a2²) = +0.0176` 이 맞물려 `E(0) = 0` 이 된다 (직접 계산해 보면 확인된다).

</details>

<details>
<summary>힌트 2 — 역함수 구간은 '전압'으로 고른다</summary>

입력이 전압이므로 `e < 0`, `e < 20.644` (= 500 °C), 그 외로 나눈다. 정의역은 `emf_mV(-200)`..`emf_mV(1372)` (= −5.8914..54.8864 mV). 표에 적힌 −5.891 mV로 경계를 잡으면 −200 °C 바로 위 값이 거부된다.

</details>

<details>
<summary>힌트 3 — CJC는 전압을 더한다</summary>

`V = E(Th) − E(Tcj)` 이므로 `E(Th) = V + E(Tcj)`, `Th = T⁻¹(V + E(Tcj))`. 냉접점 온도를 먼저 전압으로 바꾼다. `T⁻¹(V) + Tcj` 는 E가 직선일 때만 같다.

</details>

<details>
<summary>힌트 4 — Seebeck 계수와 오차 이득</summary>

`d/dt Σ c_i t^i = Σ i·c_i·t^(i−1)` (Horner로 계수 `i·c_i` 를 돌리면 된다), 지수항 미분은 `a0·exp(a1(t−a2)²)·2·a1·(t−a2)`. mV/°C → ×1000 µV/°C. Tcj를 δ만큼 틀리면 `E(Tcj)` 가 `S(Tcj)·δ` 틀리고, 그 전압 오차를 Th 쪽에서 온도로 환산하면 `/S(Th)`.

</details>

<details>
<summary>힌트 5 — Part C 순서</summary>

`tc_uV` 는 µV다 (`/1000`). 마지막 120 s 행만 골라 Tc·P·Ta·Tlocal·TSENS를 평균 → `estimate_tj` 를 lab Ta와 local air로 두 번. 영점 확인은 처음 30 s(부하 전): 0.5 W를 쓰는 칩 윗면이 주변 공기보다 차가우면 측정 체인이 틀린 것이다. 기울기는 `Σ(t−t̄)(y−ȳ)/Σ(t−t̄)²`.

</details>

## 5. 풀이

### Part A — NIST 기준 함수, 역함수, 전압 영역 CJC

**왜 `E(Th) − E(Tcj)` 인가.** Seebeck 전압은 접점에서 생기는 것이 아니라 온도 기울기가 있는 **선 전체를 따라** 생긴다: `V = ∫_{Tcj}^{Th} S_AB(T) dT`. 선이 균질하면 적분은 끝 온도에만 의존하므로, 기준 접점을 0 °C로 한 표 `E(t)` 하나로 `V = E(Th) − E(Tcj)` (중간 온도의 법칙). 단자대에서 K선이 구리로 바뀌는 두 접점이 **같은 온도** 면 구리 쪽은 전압을 더하지 않는다 (중간 금속의 법칙). 그래서 단자대는 등온 블록이고, 냉접점 서미스터는 그 블록에 붙어 있어야 한다.

**계수 출처.** NIST SRD 60 *ITS-90 Thermocouple Database* 의 `type_k.tab` (NIST Monograph 175, 1993). 지금의 `srdata.nist.gov/its90` 은 JavaScript(Blazor) 앱이라 `curl` 로 원시 파일이 받아지지 않아서, Internet Archive 사본 `web.archive.org/web/2019id_/https://srdata.nist.gov/its90/download/type_k.tab` (18106 바이트, sha256 `a238fd1a…f26`)에서 받았다. 계수는 그 파일의 계수 절에서 옮겼고, 같은 파일의 기준표 전체(1643점)를 CSV로 뽑아 테스트 기대값으로 쓴다.

| 함수 | 구간 | 형태 | NIST가 밝힌 오차 |
|---|---|---|---|
| 기준 함수 E(t) | −270..0 °C | `Σ_{i=0}^{10} c_i t^i` | 정의 그 자체 (표는 이 식의 반올림) |
| 기준 함수 E(t) | 0..1372 °C | `Σ_{i=0}^{9} c_i t^i + a0·exp(a1(t−a2)²)`, a0=0.1185976, a1=−1.183432e−4, a2=126.9686 | 〃 |
| 역함수 t(E) | −5.891..0 mV (−200..0 °C) | `Σ d_i E^i`, 8차 | −0.02..0.04 °C |
| 역함수 t(E) | 0..20.644 mV (0..500 °C) | 9차 | −0.05..0.04 °C |
| 역함수 t(E) | 20.644..54.886 mV (500..1372 °C) | 6차 | −0.05..0.06 °C |

지수항은 K형 곡선의 100~200 °C 부근 굴곡을 표현하는 항이다 (알루멜의 자기 변태와 연관된다고 알려져 있다). 이 항을 빼면 0 °C에서 E가 −0.0176 mV가 되어 바로 티가 난다.

역함수는 **근사** 다. 기준 함수의 정확한 역이 아니라 별도로 맞춘 다항식이라, 왕복 `T(E(t))` 도 ±0.06 °C 안에서만 맞는다. 테스트 결과 (solution.py 실제 출력):

```text
      max |E(t) - table| over 1643 points = 0.000499 mV
PASS  emf_mV matches all 1643 NIST table points to table rounding
      max |T(E(t)) - t| over -200..1372 C step 0.5 = 0.0535 C
PASS  roundtrip T(E(t)) within NIST inverse error band (0.06 C)
      max |T(E_table) - t| = 0.0574 C
PASS  temp_C(table E) == t within error band + table rounding
```

정방향 최대 차이 0.000499 mV는 표가 소수 3자리로 반올림된 결과(±0.0005)와 정확히 맞는다. 역함수를 표 값에 적용할 때는 표 반올림 0.0005 mV를 그 온도의 기울기로 나눈 만큼(−200 °C 부근에서는 S ≈ 15 µV/°C라 0.03 °C)을 허용 오차에 더했다. 숫자 몇 개만 맞추는 테스트(100 °C → 4.096 mV)는 계수 하나를 잘못 옮겨도 통과할 수 있으므로 **표 전체** 로 확인하는 것이 핵심이다.

### Part B — CJC를 틀리면

`demo_cjc_sweep.py` 실제 출력 (Tcj = 25 °C, 읽은 값 − 참값):

```text
Tcj = 25 C : reading - truth [C]
    Thot     V[mV]      none     add_T    linear     nist     gain
    -200    -6.892    range!    range!   +58.035    +0.041    2.655
    -100    -4.554   -35.486   -10.486   +14.673    +0.008    1.329
     -40    -2.527   -28.258    -3.258    +3.773    +0.007    1.105
       0    -1.000   -25.864    -0.864    +0.767    +0.000    1.027
      50     1.023   -24.451    +0.549    -0.220    +0.033    0.982
      85     2.474   -24.072    +0.928    -0.060    -0.025    0.976
     125     4.124   -24.355    +0.645    -0.082    +0.000    0.992
     300    11.208   -24.206    +0.794    -3.454    +0.014    0.978
     600    23.905   -23.490    +1.510    +4.156    +0.005    0.953
     771    31.082   -24.169    +0.831    +7.031    +0.007    0.982
    1000    40.275   -25.589    -0.589    +0.757    -0.023    1.039
    1300    51.410   -28.461    -3.461   -29.481    -0.033    1.160
```

열별 해석:

- **none (CJC 생략)**: 오차 ≈ `−Tcj·S(Tcj)/S(Th)` — 대략 냉접점 온도만큼 낮게 읽는다. 실험실 단자대가 25 °C면 −24 °C 안팎, 밀폐 박스 안 단자대가 50 °C면 −50 °C. −200 °C에서는 `V = −6.892 mV` 가 역함수 정의역(−5.891 mV) 밖이라 **값이 안 나온다** (`range!`). 액체질소(−196 °C) 측정에서 CJC 없는 펌웨어가 에러를 내거나 쓰레기 값을 내는 이유다.
- **add_T (온도 영역 덧셈)**: E(t)가 직선이 아니라서 생기는 오차. 실온 근처 Tcj, 0~300 °C에서는 ±1 °C 안팎이라 "잘 맞는 것처럼" 보이지만, −40 °C(차량 저온 시험)에서 −3.3 °C, −100 °C에서 −10.5 °C. 수 °C 차이로 판정이 바뀌는 열 시험에서는 그대로 실패 원인이 된다.
- **linear (41.276 µV/°C 단일 기울기)**: 41.276 µV/°C는 `E(1000)/1000` 이고, 비선형 보정을 하지 않는 변환 IC의 방식이다 (MAX31855 데이터시트는 K형을 이 기울기의 선형 관계로 가정한다고 적고 있다 — 사용하는 IC의 데이터시트를 꼭 확인할 것). 0 °C와 1000 °C 근처에서만 맞고 771 °C에서 +7.0 °C, 1300 °C에서 −29.5 °C. 펌웨어에서 NIST 다항식(또는 표 + 보간)으로 다시 선형화해야 한다. MAX31856처럼 선형화를 내장한 IC도 있다.
- **nist (전압 영역 CJC)**: 남은 오차는 역함수 근사 오차(±0.06 °C) 뿐.
- **gain = S(Tcj)/S(Th)**: 냉접점 센서가 1 °C 틀리면 결과는 실온~1000 °C에서 거의 1:1로 틀리고, −200 °C에서는 2.66배. Seebeck 계수가 −200 °C에서 15.3 µV/°C로 작기 때문이다 (테스트: `seebeck(-150) == 23.622`, `seebeck(25) == 40.518`). 극저온 측정에서는 냉접점 센서 정확도가 결과 정확도를 결정한다.

실제 보드에서 CJC가 틀어지는 방식:

| 원인 | 증상 | 드러내는 측정 |
|---|---|---|
| CJC 생략 (레거시 스크립트, 설정 누락) | 실온의 칩이 0 °C 근처로 읽힘, 박스 안에서는 수십 °C 낮음 | 전원 끈 상태에서 열전대가 실온을 읽는지 (영점 확인) |
| 냉접점 서미스터가 단자대가 아닌 MCU·LDO 옆에 있음 | 보드가 데워지면서 읽기가 서서히 떠내려감 | 열전대 입력을 단락(V=0)하면 단자대 온도를 읽어야 함. 서미스터 값과 비교 |
| K형 대신 구리선/일반 커넥터로 연장 | 커넥터 온도가 바뀔 때마다 오차 | K형 전용 미니 커넥터·보상 도선 사용, 커넥터를 손으로 데워 반응 확인 |
| 극성 반대 (ANSI 규격은 **빨강이 −**) | 데우면 읽기가 내려감 | 손가락으로 비드를 잡아 올라가는지 확인 |
| 온도 영역 덧셈 / 선형 근사 | 실온 부근은 맞고 저온·고온에서 수 °C | 이 레슨의 표, 교정기로 두세 점 확인 |
| 접지된 비드가 전원이 걸린 금속에 닿음 | 오프셋, 50/60 Hz 잡음 | 절연형 프로브, 차동 입력, 델타-시그마 ADC의 50/60 Hz 제거 필터 |
| 단선 | ADC가 레일로 붙음 | 변환 IC의 open-circuit 플래그(MAX31855 OC 비트 등)를 펌웨어가 확인 |

그리고 열전대 자체의 허용 오차: K형 표준 등급은 ASTM E230 기준 ±2.2 °C 또는 ±0.75 % 중 큰 값(특수 등급 ±1.1 °C 또는 ±0.4 %). 소프트웨어를 완벽하게 해도 이만큼은 남는다는 점을 면접에서 함께 말하면 좋다.

### Part C — Tj 추정: ΨJT는 맞고 θJA는 틀린 이유

**정의 (JESD51-2A, JESD51-12).**

- `θJA = (Tj − Ta)/P` — JESD51-2A 정지 공기(1 ft³ 상자), JESD51-7 2s2p 시험 보드에서 잰 값. 보드의 구리 양, 주변 부품, 공기 흐름, 히트싱크가 바뀌면 값이 바뀐다. 용도는 **패키지끼리 비교** 하는 것이다. JESD51-12와 TI SPRA953 모두 θJA로 실제 시스템의 Tj를 예측하지 말라고 한다.
- `θJC = (Tj − Tc)/P` — 열이 거의 전부 케이스 한 면으로 빠지도록(콜드 플레이트에 눌러) 만든 조건의 값. 히트싱크를 붙일 때 직렬 저항 사슬 `θJC + θCS + θSA` 에 쓴다.
- `ΨJT = (Tj − Tt)/P` — 열 **저항이 아니다**. 분모는 총 전력인데 윗면으로 실제로 가는 열은 그 일부뿐이라 값이 작다 (여기서 0.5 °C/W). θJA를 잴 때 같이 재는 특성 파라미터(characterization parameter)이고, 환경이 바뀌어도 `P·ΨJT` 항 자체가 작아서 Tj 추정 오차가 작다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb10-c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">analyze(): 로그 한 시나리오 → 판정</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="120" height="56" rx="6" class="f-mut"/>
    <rect x="160" y="50" width="130" height="56" rx="6" class="f-acc"/>
    <rect x="320" y="50" width="130" height="56" rx="6" class="f-acc"/>
    <rect x="480" y="50" width="110" height="56" rx="6" class="f-warn"/>
    <rect x="620" y="50" width="110" height="56" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="70" y="74">마지막 120 s</text><text x="70" y="92">+ 처음 30 s(idle)</text>
    <text x="225" y="74">Tc = T⁻¹(V+E(Tcj))</text><text x="225" y="92">P = Σ V·I (모든 레일)</text>
    <text x="385" y="74">Tj = Tc + P·ΨJT</text><text x="385" y="92">(θJA·θJC는 비교용)</text>
    <text x="535" y="70">플래그</text><text x="535" y="86">NOT_STEADY</text><text x="535" y="100">BELOW_AMB · TSENS</text>
    <text x="675" y="74">PASS / FAIL</text><text x="675" y="92">플래그 → INVALID</text>
  </g>
  <g stroke="currentColor" marker-end="url(#hwb10-c)">
    <path d="M130 78 L158 78"/><path d="M290 78 L318 78"/><path d="M450 78 L478 78"/><path d="M590 78 L618 78"/>
  </g>
  <text x="10" y="140" fill="currentColor" font-size="12">TSENS_MISMATCH: |Tj(ΨJT) − 온다이 센서| &gt; 센서 정확도(±3 °C)</text>
  <text x="10" y="162" fill="currentColor" font-size="12">TC_BELOW_AMBIENT: 전력을 쓰는 칩 윗면 &lt; 주변 공기 (정상 상태 창 또는 처음 30 s idle)</text>
  <text x="10" y="184" fill="currentColor" font-size="12">NOT_STEADY: 창 안의 Tc 기울기 &gt; 0.005 °C/s — θJA·ΨJT 모두 정상 상태(steady state) 지표다</text>
  <text x="10" y="206" fill="currentColor" font-size="12">플래그가 하나라도 서면 숫자가 PASS여도 판정은 INVALID — 측정 체인을 먼저 고친다.</text>
</svg>
```

**결과** (solution.py 실제 리포트 줄, Tj_max 105 °C):

```text
PASS SOC0 scenario=bench_fan cjc=on Tj(psiJT)=63.7C <= Tjmax=105.0C | Tc=61.9C P=3.59W TSENS=64.8C | thetaJA(Ta=24.0C)=95.7C says PASS (off by +30.9C) | flags=-
FAIL SOC0 scenario=enclosure cjc=on Tj(psiJT)=110.8C > Tjmax=105.0C | Tc=109.0C P=3.58W TSENS=111.6C | thetaJA(Ta=24.0C)=95.7C says PASS (off by -15.9C) | flags=-
INVALID SOC0 scenario=bench_fan cjc=OFF Tj(psiJT)=37.7C <= Tjmax=105.0C | Tc=35.9C P=3.59W TSENS=64.8C | thetaJA(Ta=24.0C)=95.7C says PASS (off by +30.9C) | flags=TC_BELOW_AMBIENT,TSENS_MISMATCH
INVALID SOC0 scenario=enclosure cjc=OFF Tj(psiJT)=61.4C <= Tjmax=105.0C | Tc=59.6C P=3.58W TSENS=111.6C | thetaJA(Ta=24.0C)=95.7C says PASS (off by -15.9C) | flags=TC_BELOW_AMBIENT,TSENS_MISMATCH
```

같은 결과를 표로 (`analyze()` 반환값, °C):

| 시나리오 | Tc | P [W] | TSENS | Tj = Tc+P·ΨJT | Ta(lab)+P·θJA | Tlocal+P·θJA | Tc+P·θJC |
|---|---|---|---|---|---|---|---|
| bench_fan (큰 보드 + 팬) | 61.9 | 3.59 | 64.8 | **63.7** | 95.7 (+30.9) | 96.5 (+31.7) | 87.0 (+22.2) |
| enclosure (밀폐 박스) | 109.0 | 3.58 | 111.6 | **110.8** | 95.7 (−15.9) | 125.3 (+13.7) | 134.1 (+22.5) |

괄호 = TSENS 대비 차이. 해석:

1. **벤치: θJA가 31 °C 비관적.** 이 보드는 JEDEC 2s2p 보드보다 구리가 많고 팬 바람까지 있어서 실제 유효 열저항이 `(63.7 − 24.8)/3.59 ≈ 10.9 °C/W` (추가 연습 3)로 데이터시트 20 °C/W의 절반이다. θJA로 판단하면 "여유 9 °C, 아슬아슬"이지만 실제 여유는 41 °C. 그 결과 냉각을 과설계하거나 전력 한도를 쓸데없이 낮춘다.
2. **밀폐 박스: θJA가 16 °C 낙관적 → false PASS.** 실험실 공기(24 °C)를 Ta로 넣었는데 칩이 실제로 보는 공기는 박스 안 53.7 °C다. θJA 계산은 95.7 °C로 PASS, 실제 Tj는 110.8 °C로 FAIL. 박스 안 공기를 Ta로 넣어도 125.3 °C로 이번에는 14 °C 비관적이다 — Ta를 고쳐도 θJA는 **이 보드** 의 값이 아니라서 맞지 않는다. 양쪽 방향으로 틀리므로 "보수적인 추정"이라고 변명할 수도 없다.
3. **θJC 오용: +22 °C.** `Tc + P·θJC` 는 열 3.6 W가 전부 윗면으로 나간다고 가정한다. 히트싱크 없는 PBGA에서는 대부분이 볼을 통해 보드로 가므로 윗면 온도 차이는 `P·ΨJT ≈ 1.8 °C` 뿐이다.
4. **ΨJT: TSENS와 약 1 °C 차이.** 로그를 만든 실제 ΨJT는 벤치 0.65, 박스 0.55 °C/W로 데이터시트 0.5와 다르다(팬 바람이 윗면 열 흐름을 늘린다). 그래도 `(0.65−0.5)·3.59 = 0.5 °C` 밖에 차이가 안 난다. 나머지 차이는 온다이 센서의 오프셋(+0.6 °C로 심어 둠)이다.

**정상 상태가 먼저다.** `demo_cjc_sweep.py` 의 박스 시나리오 시간축 (실제 출력):

```text
enclosure, CJC on: t[s]  P[W]   Tc[C]  Tj_psi  TSENS  Tlocal
                     0  0.53    36.4    36.7   37.4    28.0
                    58  0.54    36.4    36.7   37.2    28.2
                    62  3.60    36.6    38.4   39.4    28.2
                   120  3.57    61.3    63.1   63.7    33.8
                   240  3.57    87.4    89.2   89.6    41.4
                   480  3.61   103.5   105.3  105.8    49.2
                   720  3.57   107.3   109.1  109.7    52.1
                   960  3.60   108.7   110.5  111.2    53.3
                  1200  3.60   109.1   110.9  111.5    53.6
```

부하 후 8분(480 s)에 Tj 105.3 °C로 "딱 한계"처럼 보이지만 박스 공기가 계속 데워지며 6 °C 더 오른다. 칩 자체의 시정수는 수십~100 s지만 **박스 공기** 의 시정수(여기서 250 s)가 지배하므로, soak 시간은 가장 느린 열 질량 기준으로 잡고 `is_steady` 로 확인해야 한다 (테스트: 60..180 s 창은 `False`, 1080..1200 s 창은 `True`).

**심어 둔 결함 — CJC 없는 레거시 스크립트.** 박스 시나리오에서는 DAQ 단자대도 박스 안이라 냉접점이 약 50 °C다. CJC를 빼면 Tc가 109.0 → 59.6 °C로 49 °C 낮게 읽히고, Tj 61.4 °C로 "여유 44 °C PASS"가 나온다. 이것을 잡는 두 겹:

- `TSENS_MISMATCH`: 61.4 vs 111.6 °C — 온다이 센서 정확도(±3 °C)를 한참 넘는다. 둘 중 하나가 틀렸으면 판정하지 않는다.
- `TC_BELOW_AMBIENT`: 부하 전 idle(0.5 W) 30 s 동안 CJC 없는 Tc가 주변 공기보다 낮다. 전력을 쓰는 칩 윗면은 주변 공기보다 차가울 수 없다. 벤치 시나리오는 정상 상태에서는 35.9 > 24.8 °C라 이 검사를 통과하지만 idle 구간(약 30 → 4 °C로 읽힘)에서 걸린다. **부하를 걸기 전 영점 확인** 이 실무 습관이어야 하는 이유다.

**벤치에서 실제로 할 일.**

- 열전대 부착: 가는 선(36~40 AWG 흔히 권장)으로 비드를 패키지 윗면 **중앙** 에 열전도 에폭시로 붙이고, 선을 패키지 면을 따라 몇 cm 눕혀서 선을 타고 열이 빠지는 핀 효과(fin effect)를 줄인다. 캡톤 테이프만 붙이면 비드가 떠서 낮게 읽기 쉽다.
- 전력: 패키지에 들어가는 **모든 레일** 의 V·I 합 (전류 센스 저항, PMIC 텔레메트리, 전류 프로브). 코어 레일만 쓰면 이 예에서 0.6 W가 빠진다 — ΨJT 경로에서는 0.3 °C 오차지만 θJA 경로에서는 12 °C.
- IR 카메라로 대체할 때: 반짝이는 몰드·금속 리드는 방사율(emissivity)이 낮아 낮게 읽는다. 방사율을 아는 검은 테이프/페인트를 붙이고 열전대 한 점으로 교차 확인한다.
- 온다이 센서(TSENS)는 검증 **대상** 이다. 열전대 + ΨJT로 독립 추정한 Tj와 여러 전력 단계에서 비교하는 것이 센서 교정 확인이고, 그 센서로 도는 throttling/thermal trip 검증은 SIV-A04에서 다룬다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"          # 63/63 passed, exit=0
python3 -I -W error -X dev solution.py            # 경고 없이 같은 결과
python3 -I gen_soak_log.py && python3 -I solution.py | tail -1   # 로그 재생성 후에도 63/63
```

CJC 방법별 오차 표와 시간축 데모 (§5에 붙인 출력):

<!-- code: demo_cjc_sweep.py -->

```bash
python3 -I demo_cjc_sweep.py
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `temp_C(v) + t_cj` | E(t)가 비선형. 85 °C/35 °C에서 +1.1 °C, −40 °C/25 °C에서 −3.3 °C | `temp_C(v + emf_mV(t_cj))` |
| `v / 0.041 + t_cj` 로 끝 | 단일 기울기: 771 °C에서 +7.0 °C, 1300 °C에서 −29.5 °C | NIST 다항식 (또는 표 + 보간) |
| `tc_uV` 를 mV로 착각 | 1000배 → 역함수 범위 밖 `ValueError`, 혹은 엉뚱한 값 | 단위를 열 이름에 넣고 `/1000.0` |
| 0 °C 이상 구간에서 지수항 생략 | E(0) = −0.0176 mV → 약 −0.45 °C 오프셋, 100~200 °C에서 더 큼 | `+ K_A0*math.exp(K_A1*(t-K_A2)**2)` |
| 역함수 정의역을 표의 −5.891 mV로 하드코딩 | E(−200) = −5.8914 mV가 거부됨 | `emf_mV(T_INV_MIN)` 로 계산 |
| 숫자 몇 개만 테스트 | 계수 하나 오타도 100 °C 하나는 맞을 수 있다 | 기준표 1643점 전체와 비교 |
| `Tj = Ta + P·θJA` | JEDEC 보드·정지 공기 값. 벤치 +31 °C, 박스 −16 °C | `Tj = Tc + P·ΨJT` (Tc는 열전대) |
| `Tj = Tc + P·θJA` | 두 가지가 섞인 엉터리 — 아무 의미 없는 큰 값 | 위와 같음 |
| `Tj = Tc + P·θJC` (히트싱크 없음) | 열이 전부 윗면으로 간다는 가정. +22 °C | ΨJT. θJC는 히트싱크 직렬 사슬에서만 |
| 코어 레일 전력만 사용 | ΨJT·θJA의 P는 패키지 **총** 전력 | 모든 레일 V·I 합 |
| 부하 걸고 5분 뒤 한 점 읽기 | 박스 공기 시정수가 길어 아직 오르는 중 | 마지막 창 평균 + `is_steady` |
| 플래그가 있어도 PASS/FAIL 출력 | 측정 체인이 틀린 숫자로 판정 | 플래그 → `INVALID` |

## 8. 면접 Q&A

<details>
<summary>Q1. 열전대는 정확히 무엇을 재나요? 냉접점 보상은 왜 필요한가요?</summary>

두 금속선을 따라 생기는 Seebeck 전압 `∫S dT` 을 잰다. 균질한 선이면 양 끝 온도에만 의존해 `V = E(Th) − E(Tcj)` 이므로, 단자대 온도 Tcj를 따로 재서(서미스터, RTD, IC 센서) `E(Tcj)` 를 더해야 Th를 알 수 있다.
꼬리질문: "단자대에서 구리선으로 바뀌는 접점도 열전대 아닌가요?" → K→Cu 접점 두 개가 생기지만 같은 온도면 기여가 상쇄된다(중간 금속의 법칙). 그래서 단자대는 등온 블록으로 만들고 서미스터를 그 블록에 붙인다. 두 접점 온도가 다르면(블록 한쪽에 LDO 열) 바로 오차가 된다.

</details>

<details>
<summary>Q2. CJC를 온도 영역에서 더하면(T(V) + Tcj) 왜 안 되나요? 얼마나 틀리나요?</summary>

E(t)가 직선이 아니기 때문이다. 같은 ΔT라도 Tcj 근처의 기울기와 Th 근처의 기울기가 다르다. 실제 숫자: Th 85 °C / Tcj 35 °C에서 +1.1 °C, Th −40 °C / Tcj 25 °C에서 −3.3 °C, −100 °C에서 −10.5 °C. 정답은 전압 영역 `T⁻¹(V + E(Tcj))`.
꼬리질문: "MCU 메모리가 작아서 다항식이 부담되면?" → 1 °C 간격 표 대신 10~20 °C 간격 표 + 선형 보간(정수 µV 연산)으로 충분하다. 표 간격과 보간 오차를 계산해 사양(예: ±0.1 °C)과 비교한다. 단일 기울기 41.276 µV/°C는 771 °C에서 +7 °C 틀린다.

</details>

<details>
<summary>Q3. 냉접점 센서가 1 °C 틀리면 결과는 몇 °C 틀리나요?</summary>

`S(Tcj)/S(Th)` 배. 실온 냉접점, 0~1000 °C 측정이면 거의 1:1 (0.95~1.04). −150 °C 측정이면 1.7배, −200 °C면 2.7배 — 저온에서 Seebeck 계수가 작아(−200 °C에서 15 µV/°C) 같은 전압 오차가 더 큰 온도 오차가 된다.
꼬리질문: "그럼 단자대 온도 센서는 어디에 둬야 하나요?" → 단자 바로 옆 등온 블록(구리 패드)에. 보드 가운데 MCU 옆에 두면 보드가 데워지는 동안 단자대와 수 °C 벌어지고 그만큼 결과가 떠내려간다. 확인법: 입력 단락(V=0) 시 읽기 = 단자대 실제 온도.

</details>

<details>
<summary>Q4. θJA로 Tj를 계산하면 왜 틀리나요? 그럼 θJA는 어디에 쓰나요?</summary>

θJA는 JESD51-2A 정지 공기 + JESD51-7 2s2p 시험 보드라는 특정 환경의 값이다. 실제 보드의 구리 양, 이웃 부품 열, 공기 흐름, 박스가 다르면 값 자체가 바뀐다. 이 레슨에서는 벤치에서 +31 °C(실제 유효 10.9 °C/W vs 20), 밀폐 박스에서 −16 °C(Ta를 실험실 공기로 넣음)로 양쪽 방향으로 틀렸다. 용도는 같은 조건에서 패키지끼리 비교하는 것, 그리고 초기 설계의 대략적 감 잡기.
꼬리질문: "Ta로 어느 공기 온도를 써야 하나요?" → JEDEC 정의는 부품에서 떨어진 상자 안 정지 공기. 실제 시스템에서는 '칩이 보는 국소 공기'가 실험실 공기와 수십 °C 다를 수 있다. 그래도 국소 공기로 넣은 θJA가 14 °C 비관적이었던 것처럼, Ta를 고쳐도 θJA가 그 보드 값이 아니라는 문제는 남는다.

</details>

<details>
<summary>Q5. ΨJT와 θJC는 뭐가 다른가요? 히트싱크 설계에는 무엇을 쓰나요?</summary>

θJC는 열이 거의 전부 케이스 한 면으로 빠지게 만든 조건(콜드 플레이트)의 **열저항** 이라 직렬 사슬에 쓸 수 있다: 히트싱크 선택 시 `θSA ≤ (Tj,target − Ta)/P − θJC − θCS`. ΨJT는 저항이 아니라 `(Tj − Tt)/P` 라는 특성 파라미터다. P는 총 전력인데 윗면으로 가는 열은 일부라 작다. 히트싱크 없는 보드에서 케이스 온도로 Tj를 역산할 때 쓴다.
꼬리질문: "히트싱크 없는 보드에서 Tc + P·θJC로 계산하면?" → 열이 전부 윗면으로 간다고 가정한 것이라 과대평가. 이 레슨에서 +22 °C. 반대로 히트싱크 설계에 ΨJT를 θJC 자리에 넣으면 θSA 허용치를 3.95 대신 10.45 °C/W로 계산해 히트싱크가 턱없이 작아진다 (추가 연습 4).

</details>

<details>
<summary>Q6. 케이스 윗면 온도를 정확히 재려면 어떻게 하나요? 오차 원인은?</summary>

36~40 AWG 가는 열전대, 비드를 패키지 윗면 중앙에 열전도 에폭시로, 선을 패키지 면을 따라 눕혀 핀 효과를 줄인다. 오차 원인: 선을 타고 열이 빠짐(낮게 읽음), 비드가 떠 있음(테이프만), 비드 위치가 핫스폿에서 벗어남, CJC·극성·연장선 문제, K형 자체 허용 오차(표준 등급 ±2.2 °C 또는 ±0.75 %).
꼬리질문: "IR 카메라로 재면 안 되나요?" → 가능하지만 방사율이 문제다. 반짝이는 표면·금속은 낮게 읽힌다. 방사율을 아는 검은 테이프/페인트를 붙이고 열전대 한 점으로 교차 확인한다. 장점은 보드 전체의 핫스폿(PMIC, 인덕터)을 한 번에 본다는 것.

</details>

<details>
<summary>Q7. 열전대로 추정한 Tj와 온다이 센서가 6 °C 다르면 무엇부터 보나요?</summary>

측정 체인 순서로: ① 정상 상태였나(기울기) ② 열전대 영점 — 전원 끈 상태/idle에서 실온을 읽나, CJC가 켜져 있나 ③ 전력이 모든 레일 합인가(ΨJT·P 항 확인) ④ ΨJT는 작으니 Ψ 값 오차로는 6 °C가 나오기 어렵다 ⑤ 센서 쪽: 센서 위치와 핫스폿의 차이(부하 분포에 따라 다이 안 온도차), 센서 교정(fuse trim) 상태, 읽기 단위·오프셋 변환식.
꼬리질문: "그 차이가 전력에 비례해 커지면?" → 여러 전력 단계에서 (Tj_tc − TSENS) vs P를 그려 본다. 상수 오프셋이면 센서 오프셋/교정 문제, P에 비례하면 핫스폿-센서 거리(다이 안 열저항)나 전력 계산 누락. 이 기울기가 바로 throttling 임계 설정의 여유(SIV-A04)가 된다.

</details>

## 9. 추가 연습

### 연습 1 — 열전대 교정기(simulator) 출력

교정기로 "측정 접점 100 °C" 를 흉내 내려 한다. DAQ 단자대는 28 °C다. 교정기가 내야 할 전압은? 교정기를 고정 mV 모드로 놓고 표 값 4.096 mV를 그대로 넣으면 CJC가 켜진 DAQ는 몇 °C로 읽나?

<details>
<summary>답 보기</summary>

`E(100) − E(28) = 2.974 mV`. 4.096 mV를 그대로 넣으면 DAQ는 `T⁻¹(4.096 + E(28))` = **127.3 °C** 로 읽는다 — CJC가 두 번 들어간 셈. 실제 교정기는 출력 단자 온도를 재서 자동 보상하는 모드(K형 °C 모드)와 고정 mV 모드가 있고, 고정 mV 모드에서는 이 계산을 사람이 해야 한다. 그리고 교정기와 DAQ 사이는 **K형 도선** 이어야 한다 (구리선이면 교정기 단자 온도가 기준이 된다).

</details>

### 연습 2 — 정밀 역변환 (Newton)

NIST 역함수는 ±0.06 °C 근사이고 −200 °C 아래는 정의되지 않는다. 기준 함수 `E(t) = e` 를 직접 풀어 −270..1372 °C 전체에서 왕복 오차 < 1e−6 °C를 만들어라.

<details>
<summary>답 보기</summary>

초기값은 NIST 역함수(−200 °C 아래는 −235 °C), `t ← t − (E(t) − e)/S(t)` 로 반복. −270 °C 부근은 S가 0.7 µV/°C로 거의 0이라 Newton이 튈 수 있으므로 `[lo, hi]` 구간을 유지하며 벗어나면 이분법으로 대체한다. 실제 결과 최대 왕복 오차 2.2e−10 °C. 덤: `temp_C_exact(4.096) = 99.994 °C` — 표 값 4.096은 E(100) = 4.0962의 반올림이라 남는 0.006 °C는 역함수 오차가 아니라 **표 반올림** 이다. 실무에서는 열전대 허용 오차(±1.1~2.2 °C)가 압도적이라 NIST 역함수로 충분하다. 정밀 역변환은 교정 소프트웨어나 표준 비교용이다.

</details>

### 연습 3 — 이 보드의 유효 열저항과 허용 전력

로그 정상 상태 창에서 `θeff = (Tj − Tlocal)/P` 와, 국소 공기가 그대로라 가정할 때 Tj_max에 닿는 전력 `P_max = (Tj_max − Tlocal)/θeff` 를 구하라.

<details>
<summary>답 보기</summary>

bench_fan θeff ≈ 10.85 °C/W (P_max 7.4 W), enclosure θeff ≈ 15.95 °C/W, **P_max ≈ 3.22 W** — 현재 3.58 W에서 약 10 % 줄여야 한다 (throttling 정책과 그 검증은 SIV-A04). 단 박스 안에서는 전력을 줄이면 국소 공기도 같이 내려가므로 실제 허용 전력은 조금 더 크다. 정확히는 공기 상승분도 P의 함수(`Tlocal = Ta + P·θ_box`)로 모델링해야 한다. 데이터시트 θJA 20 °C/W와 비교하면 두 보드 모두 다르다는 것이 요점이다.

</details>

### 연습 4 — 히트싱크 θSA 선택

박스 시나리오에 히트싱크를 붙인다. 목표 Tj 95 °C(여유 10 °C), 국소 공기 54 °C, P 3.58 W, θJC 7 °C/W, TIM θCS 0.5 °C/W일 때 필요한 θSA는? ΨJT를 θJC 자리에 잘못 넣으면?

<details>
<summary>답 보기</summary>

`θSA ≤ (95 − 54)/3.58 − 7 − 0.5 = 3.953 °C/W`. ΨJT(0.5)를 넣으면 10.453 °C/W가 나와 2.6배 큰(나쁜) 히트싱크를 고르게 된다. 히트싱크를 붙이면 열의 대부분이 윗면으로 가므로 그때는 θJC가 맞는 직렬 저항이다 (단, 볼→보드 병렬 경로가 일부 열을 나눠 가지므로 이 계산은 약간 보수적이다).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 10/10 passed, exit=0
```

## 10. 핵심 정리

- 열전대 전압 = `E(Th) − E(Tcj)`. CJC는 **전압 영역**: `Th = T⁻¹(V + E(Tcj))`. 온도끼리 더하면 −40 °C에서 −3.3 °C 틀린다.
- K형 NIST 기준 함수: t<0은 10차, t≥0은 9차 + 지수항 `a0·exp(a1(t−a2)²)`. 역함수는 3구간 근사(±0.06 °C), −200 °C 아래는 없다. 검증은 **기준표 전체** 로 (100 °C → 4.096 mV, 1000 °C → 41.276 mV).
- CJC 생략 ≈ Tcj만큼 낮게 읽음. 41.276 µV/°C 선형은 771 °C에서 +7 °C. 냉접점 센서 오차는 `S(Tcj)/S(Th)` 배 (−200 °C에서 2.7배).
- θJA = JEDEC 보드·정지 공기 비교용. 실제 보드 Tj 예측에 쓰면 양쪽으로 틀린다 (+31 °C 비관 / −16 °C false PASS).
- 보드 위 Tj는 `Tc + P·ΨJT`. Ψ는 저항이 아니지만 `P·Ψ` 가 작아서 견고하다. θJC는 히트싱크 직렬 사슬 전용.
- P = 패키지 총 전력(모든 레일). 정상 상태 확인(가장 느린 열 질량: 박스 공기).
- 판정 전에 측정 체인 확인: idle 영점(칩 윗면 ≥ 주변 공기), 온다이 센서와 교차 확인. 플래그가 서면 `INVALID`.

## 참고자료

- [NIST SRD 60 — ITS-90 Thermocouple Database](https://srdata.nist.gov/its90/) (계수·기준표 `type_k.tab`. 이 레슨은 Internet Archive 사본 `https://web.archive.org/web/2019id_/https://srdata.nist.gov/its90/download/type_k.tab` 에서 받음)
- G. W. Burns et al., *Temperature-Electromotive Force Reference Functions and Tables for the Letter-Designated Thermocouple Types Based on the ITS-90*, NIST Monograph 175 (1993)
- IEC 60584-1 (열전대 EMF 규격과 허용 오차), ASTM E230 (열전대 표준 사양·허용 오차, 색 규정)
- JEDEC JESD51-1 (전기적 시험법), JESD51-2A (정지 공기 환경, ΨJT 정의), JESD51-7 (2s2p 시험 보드), JESD51-12 (열 정보 보고·사용 지침), JESD51-14 (θJC 과도 이중 계면법)
- [TI SPRA953 — Semiconductor and IC Package Thermal Metrics](https://www.ti.com/lit/an/spra953d/spra953d.pdf) (θJA를 Tj 예측에 쓰면 안 되는 이유, ΨJT/ΨJB 사용법)
- Analog Devices(Maxim) MAX31855 / MAX31856 데이터시트 — 열전대-디지털 변환기의 CJC, 선형 근사 vs 내장 선형화, open-circuit 검출
- 같이 볼 레슨: HWLAB-B09 (smoke test·inrush — 첫 전원 인가), HWLAB-B04 (파형 측정 방법론), SIV-A04 (열 특성화와 throttling 검증)
