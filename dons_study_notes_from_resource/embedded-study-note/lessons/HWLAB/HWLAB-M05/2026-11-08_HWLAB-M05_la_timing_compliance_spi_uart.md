---
id: HWLAB-M05
time: 80분
updated: 2026-11-08
---
# HWLAB-M05 LA 캡처 타이밍 컴플라이언스: SPI·UART vs 데이터시트

## 학습 목표

- 디코드가 끝난 LA 캡처에서 **데이터시트 타이밍 파라미터**를 직접 잰다: SPI tCSS / tCSH / tCSD / tSU / tH / tCH / tCL, SCLK 평균 주파수와 duty, UART 비트 폭 히스토그램 → 실제 baud, inter-byte gap.
- 각 측정값에 **샘플링 불확도 U** 를 붙인다: 에지 하나는 [0, Ts) 늦게 보이므로 간격은 ±Ts, 채널이 다르면 + skew, 긴 간격은 + 타임베이스 ppm, span 평균은 ±Ts/N.
- 데이터시트 min/max 표(JSON)와 비교해 **PASS / FAIL / UNRESOLVED** 가드밴드 판정을 내리고, 순진한(naive) 비교가 거짓 PASS 를 내는 비율을 실험으로 보인다.
- "평균을 내면 정밀해진다"가 언제 참이고(span 기반 주파수, 비동기 샘플링) 언제 거짓인지(정수비 샘플링의 duty, min 스펙) 설명한다.
- 보드 두 장의 캡처로 하드웨어/펌웨어 엔지니어가 바로 움직일 수 있는 **한 줄 위반 리포트**와 원인 힌트(공통 클럭 비율 → 크리스털)를 만든다.

## 1. 문제

LA 로 "디코드가 잘 된다"는 것은 타이밍이 스펙 안이라는 증거가 아니다 — Rivian/NVIDIA/Apple 보드 검증 면접은 "그 setup time 을 LA 로 몇 ns 정밀도로 쟀고, 그래서 PASS 라고 말할 수 있나?"를 묻는다.

상황: EVT2 센서 허브 보드. MCU U1 이 SPI ADC U7(가상 부품, mode 0)을 읽고, UART TX 로 BLE 모듈 U9 에 AT 명령을 보낸다 (115200 8N1). 펌웨어 클럭 설계는 HSE 24 MHz → PCLK 76.8 MHz → SPI ÷8 = **9.6 MHz**, UART BRR=667 → **115142 baud** (−0.05%). 보드 A 는 "BLE 가 가끔 응답 없음", "ADC 값이 가끔 튐" 이 보고됐고, 보드 B 는 수정본이다. LA 캡처(`.sr`)와 데이터시트 표(`evt2_limits.json`)가 주어진다.

데이터시트 숫자는 이 계열 부품의 **대표값 — 데이터시트/스펙 확인 필요**. 테스트는 이 레슨의 JSON 과 자기 일관적이다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm05-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">SPI mode 0 타이밍 파라미터 (U7 입력 기준)</text>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".35">
    <line x1="130" y1="30" x2="130" y2="262"/><line x1="190" y1="30" x2="190" y2="262"/><line x1="240" y1="100" x2="240" y2="262"/>
    <line x1="290" y1="100" x2="290" y2="262"/><line x1="490" y1="30" x2="490" y2="262"/><line x1="560" y1="30" x2="560" y2="262"/>
  </g>
  <text x="10" y="70" fill="currentColor">CS_N</text>
  <path d="M90 50 L130 50 L130 80 L560 80 L560 50 L700 50 L700 80 L740 80" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="140" fill="currentColor">SCLK</text>
  <path d="M90 150 L190 150 L190 120 L240 120 L240 150 L290 150 L290 120 L340 120 L340 150 L390 150 L390 120 L440 120 L440 150 L490 150 L490 120 L540 120 L540 150 L740 150" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="400" y="138" fill="currentColor" font-size="12">…</text>
  <text x="10" y="210" fill="currentColor">MOSI</text>
  <path d="M90 205 L136 205 L144 190 L246 190 L254 220 L346 220 L354 190 L446 190 L454 220 L546 220 L554 205 L740 205" fill="none" stroke="currentColor" stroke-width="2"/>
  <path d="M136 205 L144 220 L246 220 L254 190 L346 190 L354 220 L446 220 L454 190 L546 190 L554 205" fill="none" stroke="currentColor" stroke-width="2"/>
  <g stroke="currentColor" marker-start="url(#hwm05-a)" marker-end="url(#hwm05-a)">
    <line x1="132" y1="100" x2="188" y2="100"/><line x1="492" y1="100" x2="558" y2="100"/><line x1="562" y1="38" x2="698" y2="38"/>
    <line x1="192" y1="165" x2="238" y2="165"/><line x1="242" y1="165" x2="288" y2="165"/>
    <line x1="252" y1="240" x2="288" y2="240"/><line x1="292" y1="254" x2="350" y2="254"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="140" y="96">tCSS</text><text x="502" y="96">tCSH</text><text x="606" y="34">tCSD</text>
    <text x="200" y="180">tCH</text><text x="252" y="180">tCL</text>
    <text x="210" y="244">tSU</text><text x="356" y="258">tH</text>
  </g>
  <g class="f-warn" stroke="currentColor"><circle cx="290" cy="120" r="4"/><circle cx="190" cy="120" r="4"/><circle cx="490" cy="120" r="4"/></g>
  <text x="300" y="112" fill="currentColor" font-size="12">U7 은 rising 에서 MOSI 를 샘플</text>
  <text x="10" y="284" fill="currentColor" font-size="12">tCSH 기준 에지(마지막 rising? falling?)는 부품마다 다르다 → JSON 의 "ref" 에 적고 그 정의대로 잰다.</text>
</svg>
```

### Part A — 디코더 위에 타이밍 측정

`.sr` 로더(HWLAB-B07 축약)와 SPI mode 0 / UART 8N1 디코더(COMM-A10 식 디코더를 축약)는 **주어진다**. 작성할 것:

- `spi_measure(cap)` — CS_N 창마다 tCSS, tCSH, tCH, tCL, MOSI 변화마다 tSU, tH, 프레임 사이 tCSD (ns 목록), span 평균 fSCLK (MHz), duty (%).
- `uart_measure(cap)` — run 폭 히스토그램 → T0 → 프레임 디코드 → 프레임 안 에지 span 으로 Tbit → baud, baud_err (%), gap (공칭 문자 시간 단위).

정답 기준은 **샘플링하지 않은 같은 파형**(`G.ideal(wave)`)이다: 100 MS/s 캡처의 모든 인스턴스가 진짜 값과 Ts 이내여야 한다.

### Part B — 불확도와 가드밴드 판정

- `meas_uncertainty(name, m, ts_ps, la)` — 파라미터 종류별 U (같은 채널 / 다른 채널 / span 평균 / duty / baud / gap).
- `guard_verdict(vmin, vmax, u, lim)` — PASS/FAIL/UNRESOLVED. u=0 이면 순진한 비교.
- `compliance(meas, params, ts_ps, la)` — 데이터시트 표 순서로 행을 만든다. 인스턴스가 없으면 `NO DATA`.
- 실험: (1) 진짜 tCSS 17 / 23 ns vs min 20 ns 를 100~1000 MS/s 무작위 위상 100회 — 순진한 비교의 오판률 vs 가드밴드 (2) span 평균 주파수 vs 한 주기 (3) 정수비(coherent) 샘플링의 duty 편향 (4) UART +1.5% 를 1 MS/s 와 10 MS/s 로.

### Part C — EVT2 보드 위반 리포트

`board_report(label, spi_cap, uart_cap, spec)` → (overall, lines, oneline). 보드 A(100 MS/s SPI + 10 MS/s UART)는 FAIL 과 원인 힌트, 보드 B 100 MS/s 는 UNRESOLVED + 재캡처 제안, 보드 B 500 MS/s 는 PASS 여야 한다. MOSI 가 한 번도 안 바뀌는 캡처는 tSU/tH 가 `NO DATA` → PASS 가 아니다.

## 2. Problem (English)

> **Timing compliance from logic-analyzer captures.**
>
> A capture is `{"names", "tr": {ch: [(t_ps, level), ...]}, "ts_ps"}` (first entry = initial level). A datasheet table gives
> min/max limits (`evt2_limits.json`). Using only the Python 3 standard library:
>
> 1. `spi_measure(cap)` (mode 0): per CS_N-low window measure tCSS (CS_N fall → first SCLK rise), tCSH (last SCLK rise →
>    CS_N rise), tCH/tCL, and for every in-window MOSI change tSU (→ next rise) and tH (previous rise →); tCSD between frames;
>    mean SCLK frequency from `Σ(n_i−1)/Σ(last_rise−first_rise)` and duty `mean(tCH)/T`.
> 2. `uart_measure(cap)`: estimate the bit time from a run-width histogram, decode 8N1 frames, refine
>    `Tbit = Σ span / Σ n_bits` over in-frame edges, report baud error vs 115200 and inter-byte idle gaps in character times.
> 3. Each sampled edge is late by `[0, Ts)`. Attach an uncertainty `U` (±Ts per interval, + channel skew if two channels,
>    + timebase ppm, `F·Ts/Σcycles` for span averages) and return `PASS` only if the whole interval `[v−U, v+U]` meets the
>    limit, `FAIL` only if it is entirely outside, else `UNRESOLVED`.
> 4. Produce a one-line actionable report per board.
>
> Example: `EVT2-A FAIL: fSCLK=10.407+-0.048MHz, tCSH=0.000+-11.001ns, baud_err=8.281+-0.155%, gap=2.074+-0.003char | ...`

## 3. 시작 코드

<!-- code: starter.py -->

캡처 생성기 (결정적: 시드 고정, zip 타임스탬프 고정). 커밋된 `.sr` 5개(합계 약 4 KB)와 같은 파형을 Part B 실험에서 메모리로 다시 만든다:

<!-- code: gen_capture.py -->

데이터시트 표 일부 (`evt2_limits.json`):

```json
"la": {"skew_ns": 1.0, "timebase_ppm": 50, "rates_msps": [100, 200, 250, 500, 1000]},
"design": {"hse_mhz": 24.0, "sclk_hz": 9600000, "baud_hz": 115142.42878560719},
"spi_U7": {"params": {
  "fSCLK": {"max": 10.0, "unit": "MHz"}, "tCH": {"min": 40.0}, "tCL": {"min": 40.0},
  "duty": {"min": 40.0, "max": 60.0}, "tCSS": {"min": 20.0}, "tCSH": {"min": 30.0},
  "tCSD": {"min": 500.0}, "tSU": {"min": 10.0}, "tH": {"min": 10.0}}},
"uart_U9": {"baud_nom": 115200, "params": {
  "baud_err": {"min": -2.0, "max": 2.0, "unit": "%"}, "gap": {"max": 1.5, "unit": "char"}}}
```

```bash
cd lessons/HWLAB/HWLAB-M05
python3 -I starter.py ; echo "exit=$?"      # TODO 상태
```

실제 출력 (일부):

```text
-- Part A: SPI timing on top of the decoder --
PASS  given: load .sr (4 ch, 100 MS/s) + decode_spi 4 frames == SPI_FRAMES
FAIL  ideal EVT2-B: tCSS == 26.000 ns x4, tCSD ~ 600 ns x3  (TypeError: 'NoneType' object is not subscriptable)
...
PASS  given: same EVT2-A capture decoded at NOMINAL 115200 (what U9 does) -> garbage
...
FAIL  guard_verdict table (min / max / both / u=0 naive), 10 cases  (NotImplementedError: guard_verdict)
...
FAIL  coverage: MOSI never toggles (all 0x00) -> tSU/tH NO DATA -> overall not PASS  (NotImplementedError: board_report)
2/31 passed  (0.0 s)
exit=1
```

(`TypeError: 'NoneType'…` 는 `spi_measure` 가 NotImplementedError 를 내서 측정 결과가 None 으로 남았다는 뜻이다.)

## 4. 힌트

<details>
<summary>힌트 1 — CS_N 창과 bisect</summary>

`cs_windows(cap)` 가 `[(fall, rise)]` 를 준다. SCLK rising 시각 리스트 `rises` 에서 `rises[bisect_left(rises, lo):bisect_right(rises, hi)]` 가 창 안의 rising 이다. `hi` 와 **같은 샘플**의 rising 도 넣는다 — 순서를 모르니 tCSH = 0 ± U 가 정직한 값이다. MOSI 변화 t 마다 `j = bisect_left(r, t)`: `r[j] - t` 가 tSU, `t - r[j-1]` 이 tH.

</details>

<details>
<summary>힌트 2 — 주파수는 주기 하나가 아니라 span 으로</summary>

한 주기 측정은 ±Ts (100 MS/s 에서 104 ns 주기는 100 또는 110 ns 로만 보인다). 프레임 안 첫 rising ~ 마지막 rising 의 span 도 ±Ts 지만 23주기로 나누면 ±0.43 ns. 프레임 F 개면 `T = Σspan / Σ(n−1)`, 오차 한계 `F·Ts / Σ(n−1)`.

</details>

<details>
<summary>힌트 3 — UART 히스토그램은 출발점, 정밀값은 span</summary>

1비트 클러스터 = 가장 짧은 run 근처(`w < 1.5·min`)의 중앙값 → `T_init`. 모든 run 을 `n = round(w/T_init)` 로 묶어 히스토그램. stop+idle 이 섞인 run 은 비정수 폭이라 T0 를 조금 끌고 간다. 그래서 디코드 후 **프레임 안(start falling ~ stop 중앙 전)** 에지의 span 만 써서 `Tbit = Σspan/Σn` 으로 다시 맞춘다 (두 번 반복).

</details>

<details>
<summary>힌트 4 — 가드밴드 판정의 비대칭</summary>

min 한계: `vmin + u < min` 이면 FAIL (진짜도 반드시 미달), `vmin − u < min` 이면 UNRESOLVED. max 한계는 거울상. duty 처럼 둘 다 있으면 두 쪽 모두 보고 FAIL 이 UNRESOLVED 보다 우선. `u = 0` 을 넣으면 그대로 순진한 비교가 된다 — 같은 함수로 비교할 수 있다.

</details>

<details>
<summary>힌트 5 — 원인 힌트는 비율 두 개로</summary>

`r_spi = fSCLK/설계값`, `r_uart = baud/설계값`. 둘 다 1% 넘게 틀렸고 서로 0.3% 안이면 같은 클럭 트리가 틀린 것이다. `HSE_VALUE × r` 을 표준 크리스털 목록과 비교한다.

</details>

## 5. 풀이

### Part A — 무엇을 어느 에지 사이로 재나

| 파라미터 | 시작 에지 | 끝 에지 | 인스턴스 (EVT2-B) | 누구의 스펙인가 |
|---|---|---|---|---|
| tCSS | CS_N ↓ | 첫 SCLK ↑ | 4 (프레임당 1) | U7 입력 요구 → MCU 가 지켜야 |
| tCSH | 마지막 SCLK ↑ | CS_N ↑ | 4 | 〃 (펌웨어가 CS 를 언제 올리나) |
| tCSD | CS_N ↑ | 다음 CS_N ↓ | 3 | 〃 (변환 시간·리셋) |
| tCH / tCL | SCLK ↑ / ↓ | 다음 ↓ / ↑ | 96 / 92 | 〃 (클럭 품질, LA 임계값 영향 큼) |
| tSU / tH | MOSI 변화 / SCLK ↑ | 다음 SCLK ↑ / MOSI 변화 | 20 / 17 | 〃 — **MOSI 가 바뀐 비트에서만** 잴 수 있다 |
| fSCLK | 첫 SCLK ↑ | 마지막 SCLK ↑ (span) | 1 (평균) | 〃 |
| tV (연습 1) | SCLK ↓ | MISO 변화 | 18 | U7 출력 → MCU 입력 setup 에 쓰임 |

ideal(샘플링 없음) 측정과 캡처 측정 (solution 출력):

```text
      tCSS  ideal 26.00..26.00           100M 30.00..30.00           500M 26.00..26.00
      tCSH  ideal 58.00..58.00           100M 60.00..60.00           500M 58.00..58.00
      tCSD  ideal 600.00..600.00         100M 600.00..600.00         500M 600.00..600.00
      tSU   ideal 21.00..51.76           100M 20.00..50.00           500M 20.00..52.00
      tH    ideal 52.77..53.48           100M 50.00..60.00           500M 52.00..54.00
      tCH   ideal 49.52..50.56           100M 50.00..60.00           500M 48.00..52.00
      tCL   ideal 53.71..54.76           100M 50.00..60.00           500M 52.00..56.00
      fSCLK ideal 9.60..9.60             100M 9.62..9.62             500M 9.60..9.60
      duty  ideal 48.04..48.04           100M 48.32..48.32           500M 47.98..47.98
```

읽는 법:

- **tSU 최악값 21 ns 는 첫 비트**다. 나머지 비트는 MOSI 가 falling 뒤 3 ns 에 바뀌어 setup 이 T·0.52 − 3 ≈ 51 ns 인데, 첫 비트는 CS_N ↓ + 5 ns 에 세팅되고 첫 rising 은 tCSS = 26 ns 뒤 → 21 ns. 실제 보드에서도 "첫 비트 setup 은 tCSS 가 결정한다"가 흔하다.
- 100 MS/s 의 tCH 는 50 또는 60 ns 로만 나온다 (진짜 49.5~50.6). 각 인스턴스 오차는 테스트로 확인한 대로 모두 < Ts.
- tSU/tH 는 **커버리지**가 있다: 0x00 같은 바이트는 MOSI 가 안 바뀌어 측정 인스턴스가 없다. 그래서 검증용 펌웨어는 0x55/0xAA·walking-1 패턴을 보낸다 (Part C 의 `NO DATA` 테스트).

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm05-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">UART 8N1 프레임: 히스토그램은 run 폭, 정밀 Tbit 는 프레임 안 span</text>
  <path d="M30 60 L60 60 L60 100 L100 100 L100 60 L140 60 L140 100 L340 100 L340 60 L380 60 L380 100 L420 100 L420 60 L470 60 L470 100 L510 100 L510 60 L560 60" fill="none" stroke="currentColor" stroke-width="2"/>
  <g fill="currentColor" font-size="12">
    <text x="64" y="118">start</text><text x="104" y="118">b0</text><text x="224" y="118">b1–b5 = 0</text><text x="344" y="118">b6</text>
    <text x="384" y="118">b7</text><text x="424" y="118">stop</text><text x="474" y="118">start</text>
  </g>
  <g class="f-ok" stroke="currentColor"><circle cx="60" cy="80" r="4"/><circle cx="100" cy="80" r="4"/><circle cx="140" cy="80" r="4"/><circle cx="340" cy="80" r="4"/><circle cx="380" cy="80" r="4"/><circle cx="420" cy="80" r="4"/></g>
  <g stroke="currentColor" marker-start="url(#hwm05-b)" marker-end="url(#hwm05-b)"><line x1="62" y1="140" x2="418" y2="140"/><line x1="422" y1="40" x2="468" y2="40"/></g>
  <text x="150" y="156" fill="currentColor" font-size="12">span = 마지막 프레임 안 에지 − start ↓ = 9 비트 (오차 ±Ts 하나)</text>
  <text x="430" y="34" fill="currentColor" font-size="12">stop 1비트 + gap</text>
  <line x1="440" y1="50" x2="440" y2="104" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="444" y="132" fill="currentColor" font-size="12">stop 중앙 (9.5 T)</text>
  <text x="590" y="36" fill="currentColor" font-weight="bold" font-size="12">EVT2-A run 히스토그램</text>
  <g class="f-acc" stroke="currentColor">
    <rect x="600" y="60" width="22" height="90"/><rect x="630" y="93" width="22" height="57"/><rect x="660" y="137" width="22" height="13"/>
    <rect x="690" y="145" width="22" height="5"/><rect x="720" y="148" width="22" height="2"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="605" y="166">1</text><text x="635" y="166">2</text><text x="665" y="166">3</text><text x="695" y="166">4</text><text x="725" y="166">5</text>
    <text x="600" y="54">57</text><text x="630" y="87">36</text><text x="600" y="182">n 비트 (Ts=100 ns)</text>
  </g>
  <text x="10" y="200" fill="currentColor" font-size="12">1비트 run = 80/81 샘플 (Tbit 8016.8 ns, Ts 100 ns). 2비트 run 의 171 = stop + 9 µs gap (비정수 폭).</text>
  <text x="10" y="222" fill="currentColor" font-size="12">→ 히스토그램 T0 = 8091.8 ns (+0.9 % 끌려감), 프레임 안 span 정밀화 Tbit = 8016.67 ns (진짜 8016.83 ns).</text>
</svg>
```

**UART baud 를 왜 이렇게 재나.** run 폭 하나의 오차는 ±Ts — 10 MS/s 에서 1비트(8 µs)의 ±1.25%. 그러나 한 프레임 안의 에지는 모두 같은 비트 격자 위에 있으므로 start ↓ 부터 마지막 프레임 안 에지까지의 span 은 오차 ±Ts **하나**로 n 비트를 덮는다. 프레임 사이 idle 은 격자를 끊으므로(gap 은 임의 길이) span 은 프레임 안에서만 잇는다. 실제 출력:

```text
      EVT2-A hist {n: [count, min_smp, max_smp]} {1: [57, 80, 81], 5: [1, 401, 401], 2: [36, 160, 171], 3: [8, 240, 241], 4: [3, 320, 321]}  T0=8091.8ns Tbit=8016.67ns baud=124740.1
      EVT2-A nominal-rate decode: b'\xa1\xb4\x8b\xb3\xa5\xae\xa4\x9d\xb4\x8c\x92\x93\x8e\x95\x8d\x8a'
```

보드 A 의 실제 baud 는 124 740 (+8.28%). 같은 캡처를 공칭 115200 으로 디코드하면 쓰레기 — **U9 이 받는 것이 바로 이것**이다. 8N1 수신기는 start ↓ 에서 맞춘 뒤 stop 중앙(9.5 비트)까지 동기 없이 가므로 누적 어긋남 9.5·e 가 반 비트를 넘으면 깨진다: 양쪽 오차의 합 |e| < 0.5/9.5 ≈ 5.3% 가 이론 한계이고, 수신기 16× 오버샘플링의 샘플 불확도와 느린 에지를 빼면 실제 여유는 그보다 작다. 그래서 흔한 설계 규칙이 "각 측 ±2%" 다 (이 레슨 JSON 의 한계, 대표값). gap 은 `다음 start − (start + 10·Tbit)` 를 공칭 문자 시간(86.8 µs)으로 나눈 값.

### Part B — 불확도 U 는 어디서 오나

샘플러 하나가 모든 채널을 같은 시각에 찍는 timing-mode LA 에서 에지 t 는 `t_obs = t + δ`, δ ∈ [0, Ts) (HWLAB-B07). 그러면:

| 측정 | 오차의 원천 | U (이 레슨 구현) | 100 MS/s 값 |
|---|---|---|---|
| 같은 채널 간격 (tCH, tCL, tCSD) | δ₂ − δ₁ ∈ (−Ts, Ts) | Ts + ppm·v | 10.0 ns |
| 다른 채널 간격 (tCSS, tCSH, tSU, tH, tV) | + 채널 간 skew (LA 입력·프로브 리드 길이) | Ts + skew + ppm·v | 11.0 ns |
| span 평균 주기 | span 마다 ±Ts, F 개 | U_T = F·Ts/Σ(n−1) + ppm·T | 0.43 ns (4 프레임 × 23) |
| fSCLK | U_T 전파 | f·U_T/T | 0.041~0.048 MHz |
| duty | high 폭 평균의 한계 오차 | 100·Ts/T + duty·U_T/T | ≈ 10 % |
| baud_err | 프레임 span | U_Tb = F·Ts/Σn → 100·(Tnom/Tb)·U_Tb/Tb | 0.13~0.16 % (10 MS/s) |
| gap | start 간격 ±Ts + 10·U_Tb | (Ts + 10·U_Tb)/Tchar | 0.003 char |

이 표에 **없는** 항목도 말할 수 있어야 한다: ① LA 임계값 vs 데이터시트 기준 레벨 — 데이터시트 타이밍은 보통 VIH/VIL 또는 50% 교차 기준인데 LA 는 비교기 하나의 임계값에서 잰다. 에지가 느리면 (예: tr 5 ns) 기준 레벨 차이만큼 각 에지가 수 ns 이동한다 → 스코프로 그 에지의 crossing 을 IEEE-181 방식으로 재야 한다 (HWLAB-B04). ② 프로브 위치 — 입력 타이밍은 **수신 부품(U7) 핀에서** 잰다. ③ 합성 LA 는 skew 0 이지만 예산에는 사양값 1 ns 를 넣었다 (보수적).

**최악값에도 U 를 그대로 쓸 수 있는 이유.** 인스턴스마다 |m_i − x_i| ≤ U 이면 |min m_i − min x_i| ≤ U (min 은 각 좌표 오차의 최대를 넘지 않는다). 그래서 "측정 최악값 ± U 안에 진짜 최악값" — 테스트 `truth inside [worst +- U]` 가 모든 파라미터에서 확인한다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">가드밴드 판정 (min 한계 20 ns, 측정 m, 진짜 값은 [m − U, m + U] 안; 10 px = 1 ns)</text>
  <rect x="80" y="32" width="300" height="206" class="f-warn" opacity=".25"/>
  <line x1="380" y1="32" x2="380" y2="238" stroke="currentColor" stroke-width="2"/>
  <text x="330" y="256" fill="currentColor">limit min = 20 ns</text>
  <text x="90" y="50" fill="currentColor" font-size="12">위반 영역 (&lt; 20 ns)</text>
  <g stroke="currentColor" stroke-width="2">
    <line x1="470" y1="80" x2="690" y2="80"/><line x1="470" y1="72" x2="470" y2="88"/><line x1="690" y1="72" x2="690" y2="88"/>
    <line x1="370" y1="130" x2="590" y2="130"/><line x1="370" y1="122" x2="370" y2="138"/><line x1="590" y1="122" x2="590" y2="138"/>
    <line x1="120" y1="180" x2="340" y2="180"/><line x1="120" y1="172" x2="120" y2="188"/><line x1="340" y1="172" x2="340" y2="188"/>
    <line x1="410" y1="224" x2="470" y2="224"/><line x1="410" y1="216" x2="410" y2="232"/><line x1="470" y1="216" x2="470" y2="232"/>
  </g>
  <g stroke="currentColor"><circle cx="580" cy="80" r="5" class="f-ok"/><circle cx="480" cy="130" r="5" class="f-acc"/><circle cx="230" cy="180" r="5" class="f-warn"/><circle cx="440" cy="224" r="5" class="f-ok"/></g>
  <g fill="currentColor" font-size="12">
    <text x="700" y="84">PASS</text><text x="600" y="134">UNRESOLVED</text><text x="130" y="204">FAIL: m + U &lt; 20</text><text x="480" y="228">PASS (500 MS/s, U=3)</text>
    <text x="470" y="66">m=40, U=11 → [29, 51]</text><text x="400" y="116">m=30, U=11 → [19, 41]</text><text x="150" y="166">m=5 → [−6, 16]</text>
    <text x="10" y="84">100 MS/s</text><text x="10" y="134">100 MS/s</text><text x="10" y="184">100 MS/s</text><text x="10" y="228">500 MS/s</text>
    <text x="400" y="208">26 ± 3</text>
  </g>
</svg>
```

**실험 1 — 순진한 비교는 얼마나 자주 틀리나** (진짜 tCSS 17 ns = 위반, 23 ns = 통과, 한계 ≥ 20 ns, 위상 무작위 100회, 실제 출력):

```text
      tCSS truth  fs[MS/s]  naive wrong  guard wrong  UNRESOLVED   (100 random phases, limit >= 20 ns)
         17 ns      100        50%           0%         100%
         17 ns      200         0%           0%         100%
         17 ns      500         0%           0%           0%
         17 ns     1000         0%           0%           0%
         23 ns      100         0%           0%         100%
         23 ns      200         0%           0%         100%
         23 ns      500         0%           0%          98%
         23 ns     1000         0%           0%           0%
```

- 100 MS/s 에서 진짜 17 ns(위반)를 순진한 비교는 **절반을 PASS** 라고 했다 (측정값이 10 또는 20 ns 로 양자화, 20 ≥ 20). 가드밴드는 한 번도 틀리지 않았다 — 대신 "모른다"고 했다.
- 23 ns 는 마진이 3 ns 라 U = Ts + 1 ns 가 3 ns 보다 작아지는 1 GS/s 에서야 전부 결정된다. 500 MS/s(U=3)는 98% 가 경계에 걸렸다. **필요한 샘플링률은 마진이 정한다** (연습 2: 반드시 결정하려면 Ts < M/2 − skew).

**실험 2 — 주파수는 span 으로.** 보드 A 100 MS/s:

```text
      EVT2-A 100M: fSCLK span = 10.4072 +- 0.0476 MHz ; 1/(min tCH + min tCL) = 11.111 MHz
```

한 주기로 보면 11.1 MHz (+6.8%) 같은 엉뚱한 값이 나오지만 span 평균은 10.407 ± 0.048 MHz (진짜 10.400) — 10 MHz 한계를 **확정적으로** 넘는다. 단, 평균이 스펙 안이어도 개별 사이클이 짧을 수 있다(지터, 클럭 게이팅, spread spectrum). 그 경우는 tCH/tCL (사이클별, ±Ts)이 잡는다.

**실험 3 — 평균이 거짓말할 때 (coherent sampling).** duty 48%, 지터 0, 한 프레임 96 클럭, 위상 8개:

```text
      f=10.0 MHz @100MS/s duty over 8 phases: 50.00 50.00 50.00 50.00 40.00 50.00 50.00 50.00  (truth 48.00)
      f= 9.6 MHz @100MS/s duty over 8 phases: 47.98 47.98 48.03 48.03 48.03 47.98 47.98 47.98  (truth 48.00)
```

10 MHz 와 100 MS/s 는 정수비라 모든 에지가 샘플 격자에 대해 **같은 위상**에 놓인다 → 96번 평균해도 같은 오차를 96번 더할 뿐(+2 % 또는 −8 %). 9.6 MHz 는 주기가 10.417 샘플이라 위상이 12 사이클마다 격자를 한 바퀴 돌며 오차가 상쇄(dither)된다. 그래서 U 는 평균 개수로 나누지 않는다 — 비동기라는 보장이 없으면 최악 한계 Ts 를 그대로 쓴다. 통계적 정밀도(σ = Ts/√6 per 간격, /√N)는 "샘플 클럭과 신호가 무관하다"는 가정에서만 성립한다.

**실험 4 — UART 는 샘플링률로만 좋아진다.**

```text
      U9' +1.500% @ 1 MS/s: baud_err=+1.461 +- 1.323 %  hist={1: [72, 8, 9], 5: [1, 42, 42], 3: [8, 25, 26], 2: [23, 17, 18], 4: [3, 34, 34]}  -> UNRESOLVED
      U9' +1.500% @10 MS/s: baud_err=+1.502 +- 0.137 %  hist=...  -> PASS
```

U_Tb = F·Ts/Σn ≈ Ts/(프레임당 평균 비트 수) — 프레임을 더 많이 잡아도 **최악 한계**는 그대로다(프레임마다 ±Ts 가 더해지고 비트 수도 같은 비율로 늘어난다). 1 MS/s 의 ±1.3% 는 ±2% 판정에 쓸 수 없다. 10 MS/s 면 ±0.14%.

### Part C — EVT2 리포트

실제 리포트 (solution 출력, 보드 A):

```text
      EVT2-A SPI U7  fs=100MS/s Ts=10ns frames=4
        fSCLK    n=  1  worst=   10.407 +-  0.048 MHz   limit <=10        FAIL       (naive FAIL)
        tCH      n= 96  worst=   40.000 +- 10.002 ns    limit >=40        UNRESOLVED (naive PASS)
        tCL      n= 92  worst=   50.000 +- 10.002 ns    limit >=40        UNRESOLVED (naive PASS)
        duty     n=  1  worst=   48.133 +- 10.627 %     limit >=40 <=60   UNRESOLVED (naive PASS)
        tCSS     n=  4  worst=   20.000 +- 11.002 ns    limit >=20        UNRESOLVED (naive PASS)
        tCSH     n=  4  worst=    0.000 +- 11.001 ns    limit >=30        FAIL       (naive FAIL)
        tCSD     n=  3  worst=  600.000 +- 10.030 ns    limit >=500       PASS       (naive PASS)
        tSU      n= 20  worst=   20.000 +- 11.002 ns    limit >=10        UNRESOLVED (naive PASS)
        tH       n= 17  worst=   40.000 +- 11.002 ns    limit >=10        PASS       (naive PASS)
      EVT2-A UART U9 fs=10MS/s baud=124740.1 Tbit=8016.67ns bytes=b'AT+SEND=T,23.5\r\n' stop_err=0
        baud_err n=  1  worst=    8.281 +-  0.155 %     limit >=-2 <=2    FAIL       (naive FAIL)
        gap      n= 15  worst=    2.074 +-  0.003 char  limit <=1.5       FAIL       (naive FAIL)
      EVT2-A FAIL: fSCLK=10.407+-0.048MHz, tCSH=0.000+-11.001ns, baud_err=8.281+-0.155%, gap=2.074+-0.003char | unresolved tCH,tCL,duty,tCSS,tSU -> re-capture SPI at >= 500 MS/s (U ~ Ts) or scope | SCLK x1.0841 & UART x1.0834 = common clock: HSE 24 MHz x 1.0837 = 26.01 MHz -> 26 MHz xtal fitted vs HSE_VALUE=24? | CS_N rises before last bit done -> wait BSY/EOT (not TXE) before CS_N high | TX gaps > limit -> send packet by DMA (ISR preempted)
```

심어 둔 결함과 리포트의 대응:

| 심은 결함 (gen_capture) | 증상 | 리포트가 본 것 | 행동 |
|---|---|---|---|
| 26 MHz 크리스털 + 펌웨어 HSE_VALUE=24 → 모든 클럭 ×1.0833 | ADC 가끔 튐, BLE 무응답 | fSCLK 10.407 MHz > 10, baud +8.28%, **두 비율이 같음** | BOM 확인 (RF 쪽 26 MHz 공용?) 또는 HSE_VALUE/PLL 수정 |
| CS_N 을 TXE 만 보고 올림 (마지막 rising + 4 ns) | 마지막 비트/변환 깨짐 | tCSH 0 ± 11 ns < 30 → FAIL (증명됨) | BSY(또는 EOT) 대기 후 CS_N ↑ |
| UART TX 를 ISR 로, 상위 ISR 에 밀림 (180 µs × 2) | U9 가 패킷을 끊어 읽음 | gap 2.074 char > 1.5 | DMA TX, 또는 우선순위 조정 |

두 비율이 같다는 점이 핵심 진단이다: SCLK 와 UART 가 **같은 비율 1.083** 으로 틀렸다면 SPI 설정 실수와 UART BRR 실수가 따로 일어났을 가능성보다 공통 클럭 트리(HSE → PLL → PCLK)가 틀렸을 가능성이 훨씬 크다. 26.01 MHz → 표준 크리스털 26 MHz. (RF 설계에서는 26 MHz 가 셀룰러/GNSS 기준 클럭으로 흔해 BOM 대체로 섞이기 쉽다.) 측정 정밀도가 이 추론을 가능하게 한다: span 평균의 ±0.46% 와 baud ±0.16% 이 1.0841 과 1.0834 를 "같다"고 말할 근거다.

보드 B (수정본):

```text
      EVT2-B UNRESOLVED: unresolved tCH,tCL,duty,tCSS,tSU -> re-capture SPI at >= 500 MS/s (U ~ Ts) or scope
      EVT2-B SPI U7  fs=500MS/s Ts=2ns frames=4
        fSCLK    n=  1  worst=    9.599 +-  0.008 MHz   limit <=10        PASS       (naive PASS)
        tCH      n= 96  worst=   48.000 +-  2.003 ns    limit >=40        PASS       (naive PASS)
        tCL      n= 92  worst=   52.000 +-  2.003 ns    limit >=40        PASS       (naive PASS)
        duty     n=  1  worst=   47.977 +-  1.962 %     limit >=40 <=60   PASS       (naive PASS)
        tCSS     n=  4  worst=   26.000 +-  3.001 ns    limit >=20        PASS       (naive PASS)
        tCSH     n=  4  worst=   58.000 +-  3.003 ns    limit >=30        PASS       (naive PASS)
        tCSD     n=  3  worst=  600.000 +-  2.030 ns    limit >=500       PASS       (naive PASS)
        tSU      n= 20  worst=   20.000 +-  3.003 ns    limit >=10        PASS       (naive PASS)
        tH       n= 17  worst=   52.000 +-  3.003 ns    limit >=10        PASS       (naive PASS)
      EVT2-B UART U9 fs=10MS/s baud=115117.1 Tbit=8686.81ns bytes=b'AT+SEND=T,23.5\r\n' stop_err=0
        baud_err n=  1  worst=   -0.072 +-  0.133 %     limit >=-2 <=2    PASS       (naive PASS)
        gap      n= 15  worst=    0.000 +-  0.002 char  limit <=1.5       PASS       (naive PASS)
      EVT2-B PASS: all 11 limits met with guard band
```

100 MS/s 에서는 순진한 비교가 전부 PASS 였지만 근거가 없었다 (tCSS 30 ± 11 ns → 진짜는 19~41 ns 어디든). 500 MS/s 재캡처로 26 ± 3 ns → PASS. 리포트 순서가 실무 순서다: **캡처가 판정을 지지할 수 있나 → 위반 → 미결정 + 다음 측정 → 원인 힌트**.

**장비/도구가 있을 때** (sigrok-cli 는 이 맥에 설치돼 있지 않아 실행하지 않았다 — 출력 없음):

```bash
# 보드에서 SPI 4채널을 500 MS/s 급 LA 로 (FX2 계열은 24 MS/s 가 상한 -> 이 레슨 기준 판정 불가)
sigrok-cli -d <driver> --config samplerate=500m --channels D0,D1,D2,D3 --samples 20k -o evt2b_spi.sr
# 디코드 교차 확인 (타이밍 측정은 이 레슨 코드로)
sigrok-cli -i evt2b_spi_500M.sr -P spi:clk=SCLK:mosi=MOSI:miso=MISO:cs=CS_N -A spi
sigrok-cli -i evt2a_uart_10M.sr -P uart:rx=TX:baudrate=115200 -A uart
```

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"
python3 -I -W error -X dev solution.py > /dev/null ; echo "exit=$?"   # 경고 없이 exit=0
```

실제 출력 (Part A/B 검사 줄, 측정값과 리포트는 §5 에 그대로 실었다):

```text
-- Part A: SPI timing on top of the decoder --
PASS  given: load .sr (4 ch, 100 MS/s) + decode_spi 4 frames == SPI_FRAMES
PASS  ideal EVT2-B: tCSS == 26.000 ns x4, tCSD ~ 600 ns x3
PASS  ideal EVT2-B: counts tCH 96, tCL 92 (23/frame), tSU 20 / tH 17 (one per in-frame MOSI edge)
PASS  ideal EVT2-B: tSU = 21 (1st bit: tCSS-5) or T*0.52-3ns, tH = 0.48T+3ns (+-1 ns jitter)
PASS  ideal EVT2-B: fSCLK (span) = 9.6 MHz +-0.02%, duty = 48% +-0.3
PASS  100 MS/s capture: every instance |meas - true| < Ts = 10 ns (all 7 timing params)
PASS  500 MS/s capture: every instance |meas - true| < Ts = 2 ns
-- Part A: UART baud from bit-width histogram, inter-byte gap --
PASS  EVT2-B UART: bytes == 'AT+SEND=T,23.5\r\n', no stop errors
PASS  EVT2-B UART: Tbit within 0.05% of truth (8684.9 ns), histogram has 1-bit cluster
PASS  EVT2-A UART: decodes the same text with measured Tbit, baud_err = +8.28% +-0.05
PASS  EVT2-A UART: gaps -> 2 of 15 are 180 us = 2.07 char, rest 0.10 char (+-0.01)
PASS  given: same EVT2-A capture decoded at NOMINAL 115200 (what U9 does) -> garbage
-- Part B: uncertainty and guard-band verdicts --
PASS  guard_verdict table (min / max / both / u=0 naive), 10 cases
PASS  U(tCSS @100M) = Ts + skew + ppm*v = 11.0013 ns; U(tCH @100M) = 10.0025 ns
PASS  truth inside [worst +- U] for every SPI param, EVT2-B 100M and 500M
PASS  truth inside [worst +- U] for every SPI param, EVT2-A 100M
PASS  sweep: guard band is NEVER wrong (0 false PASS / false FAIL) at any fs
PASS  sweep: naive is wrong > 30% at 100 MS/s for truth 17 ns (false PASS)
PASS  sweep: 17 ns decided (FAIL) from 500 MS/s, 23 ns only at 1 GS/s (margin 3 ns vs U = Ts + 1)
PASS  EVT2-A fSCLK: span mean 10.400 MHz +-U (U < 0.5%) vs single shortest cycle off by > 3%
PASS  coherent 10.0 MHz @100 MS/s: averaged duty biased (|err| >= 2% for every phase)
PASS  incoherent 9.6 MHz @100 MS/s: averaging works (|err| < 1% for every phase)
PASS  both: truth 48% inside duty +- U (U = 100*Ts/T ~ 10%)
PASS  UART +1.5%: 1 MS/s -> UNRESOLVED, 10 MS/s -> PASS, truth inside +-U both
PASS  truth inside +-U: EVT2-A baud_err and every gap (10 MS/s)
-- Part C: EVT2 violation reports vs datasheet table (evt2_limits.json) --
...
PASS  EVT2-A: FAIL on fSCLK, tCSH, baud_err, gap
PASS  EVT2-A one-line: common clock hint 26 MHz xtal + BSY hint + DMA hint
PASS  EVT2-B @100M: no FAIL, tCSS UNRESOLVED -> overall UNRESOLVED with re-capture advice
PASS  EVT2-B @500M: overall PASS (all 11 limits with guard band)
PASS  EVT2-B @100M: naive comparison already 'decided' a row the guard band calls UNRESOLVED
PASS  coverage: MOSI never toggles (all 0x00) -> tSU/tH NO DATA -> overall not PASS
31/31 passed  (0.1 s)
exit=0
```

실행 시간: `time python3 -I solution.py` → 0.33 s total (0.29 s user, 스윕 800회 포함), practice 0.19 s. 데이터 재생성 결정성: `python3 -I gen_capture.py` 전후 `shasum -a 256 *.sr` 비교 → `IDENTICAL`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 습관 |
|---|---|---|
| 측정값을 한계와 그대로 비교 | ±Ts 양자화 → 100 MS/s 에서 진짜 17 ns 를 50% 확률로 PASS | `guard_verdict(vmin, vmax, U, lim)` — UNRESOLVED 를 허용 |
| UNRESOLVED 를 PASS 로 반올림 | "증거 없음"을 "통과"로 보고 | 재캡처(fs↑) 또는 스코프, 리포트에 남김 |
| 주파수를 한 주기로 | ±Ts/T = ±10% (100 MS/s, 10 MHz) | span: `Σ(n−1)/Σ(last−first)`, U = F·Ts/Σ(n−1) |
| 평균하면 duty·간격 오차가 1/N 로 준다고 가정 | 정수비 샘플링은 위상이 고정 → 편향 (실험 3) | 최악 U 는 Ts 그대로; 통계 주장은 비동기 보장 시만 |
| 다른 채널 간격에 skew 를 빼먹음 | LA 입력/프로브 리드 차이 수백 ps~ns | `U = Ts + skew + ppm·v` |
| tSU 를 "모든 비트"로 셈 | MOSI 가 안 바뀐 비트는 측정 불가 → 커버리지 착시 | 인스턴스 수 출력, 0이면 NO DATA, 토글 패턴 사용 |
| 같은 샘플의 SCLK↑·CS_N↑ 를 창 밖으로 버림 | 마지막 비트를 놓치고 tCSH 를 T 만큼 크게 잼 | `bisect_right(rises, hi)` — 0 ± U 로 남긴다 |
| tCSH 기준 에지를 임의로 고름 | 부품마다 마지막 rising/falling 기준이 다름 | JSON `ref` 에 정의를 적고 그대로 구현 |
| UART baud 를 히스토그램 최빈값으로만 | stop+gap 오염 run 이 끌고 감 (+0.9%) | 프레임 안 span 으로 정밀화 |
| baud 오차를 "디코드 됐으니 OK" | 우리 디코더는 측정 Tbit 로 맞춰 읽는다 — 수신기는 공칭으로 읽는다 | 공칭 디코드도 같이 보여 준다 (쓰레기) |
| LA 임계값으로 잰 시간을 데이터시트 기준 레벨 시간과 동일시 | 느린 에지면 기준 레벨 차이만큼 이동 | 에지가 느리면 스코프 + IEEE-181 crossing (HWLAB-B04) |

## 8. 면접 Q&A

<details>
<summary>Q1. 100 MS/s LA 로 잰 tSU 가 20 ns 이고 스펙이 min 10 ns 입니다. PASS 인가요?</summary>

각 에지가 [0, Ts) 늦게 보이므로 간격은 ±10 ns, 다른 채널이니 skew 1 ns 를 더해 U = 11 ns. 진짜는 [9, 31] ns → 한계 10 ns 에 걸쳐 **UNRESOLVED**. 이 레슨의 보드 B 가 정확히 그렇고, 500 MS/s 재캡처에서 20 ± 3 → PASS.
꼬리질문: "그럼 100 MS/s 로 증명할 수 있는 최소 마진은?" → 반드시 결정하려면 Ts < M/2 − skew → M > 22 ns. "U 에 Ts 가 두 번 들어가는 이유는?" → 측정이 진짜에서 최대 U 만큼 벗어나 있고, 판정도 측정에서 U 만큼 떨어져야 하므로 (연습 2).

</details>

<details>
<summary>Q2. SCLK 주파수를 LA 로 0.5% 이내로 재는 방법은? 100 MS/s 에서 10 MHz 는 10 샘플뿐인데요.</summary>

한 주기가 아니라 프레임 안 첫 rising ~ 마지막 rising 의 span 을 사이클 수로 나눈다. span 오차도 ±Ts 하나라 23 사이클이면 ±0.43 ns/주기 ≈ ±0.44% (100 ns 주기). 여러 프레임은 Σspan/Σ사이클 (최악 F·Ts/Σ사이클). 주파수 카운터의 "게이트 시간"을 늘리는 것과 같은 원리.
꼬리질문: "평균 주파수가 스펙 안이면 끝인가?" → 아니다. fSCLK max 는 사실상 사이클별 최소 주기·tCH/tCL 요구다. 지터·spread spectrum·클럭 스트레칭은 평균으로 안 보인다 → 사이클별 tCH/tCL(±Ts)을 같이 판정한다.

</details>

<details>
<summary>Q3. 측정을 1000번 반복해 평균내면 ±Ts 가 ±Ts/√1000 이 되지 않나요?</summary>

샘플 클럭과 신호가 **비동기**이고 위상이 고르게 퍼질 때만이다. 그때 한 간격의 양자화 오차는 (−Ts, Ts) 삼각 분포(σ = Ts/√6)라 평균은 √N 으로 줄어든다. 하지만 10 MHz 를 100 MS/s 로 잡으면 위상이 고정되어 같은 오차를 N 번 더한다 — 실험 3 의 duty 가 50.00 / 40.00 으로 박혔다. 게다가 min/max 스펙은 평균이 아니라 **최악 인스턴스**에 대한 것이라 평균 자체가 답이 아니다.
꼬리질문: "그럼 일부러 dither 를 만들 수 있나?" → LA 샘플링률을 신호와 정수비가 아니게 고르거나(예: 97 MS/s), 여러 캡처를 비동기로 반복. 그래도 컴플라이언스 보고에는 최악 한계를 쓰고 통계값은 참고로.

</details>

<details>
<summary>Q4. UART 실제 baud 를 LA 로 어떻게 재고, 왜 프레임을 많이 잡아도 정밀도가 안 좋아지나요?</summary>

run 폭 히스토그램으로 1비트 클러스터를 찾고(거친 T0), 디코드 후 각 프레임 안 에지 span 을 비트 수로 나눠 `Tbit = Σspan/Σn`. 프레임 사이 idle 은 임의 길이라 비트 격자가 끊기므로 span 은 프레임 안(최대 ~9비트)에서만 가능하다. 최악 한계는 프레임마다 ±Ts, 비트 수도 프레임 수에 비례 → `F·Ts/Σn ≈ Ts/9` 로 F 와 무관. 정밀도를 올리려면 fs 를 올린다 (1 MS/s ±1.3% → 10 MS/s ±0.14%).
꼬리질문: "0x55 를 연속으로 보내면?" → 모든 비트에 에지가 있어 히스토그램이 깨끗하고, gap 없이 연속(DMA)이면 프레임 경계에서도 격자가 이어져 span 을 여러 프레임에 걸칠 수 있다 — LIN sync byte 와 오토보드(autobaud)에 0x55 를 쓰는 이유와 같다.

</details>

<details>
<summary>Q5. SPI 클럭과 UART 가 둘 다 약 8% 빠릅니다. 무엇을 의심하나요?</summary>

두 주변장치가 같은 비율로 틀렸으면 공통 원인 — 클럭 트리. 이 레슨에서는 ×1.0841 / ×1.0834 (측정 불확도 안에서 같음) → HSE 24 MHz × 1.0837 = 26.01 MHz → 26 MHz 크리스털이 실장됐는데 펌웨어는 HSE_VALUE=24 MHz. 확인: MCO 핀으로 HSE/SYSCLK 를 내보내 카운터로 재거나, 크리스털 마킹/BOM 확인. RF 보드에서 26 MHz TCXO 가 흔해 대체 부품으로 섞이기 쉽다.
꼬리질문: "+8% UART 인데 왜 '가끔' 응답했나?" → 이 데이터에서는 공칭 디코드가 전부 깨진다. 실제 현장에서 '가끔'이라면 오차가 수신 한계(이론 ±5.3%, 실제 그 이하) 근처라는 신호이고, 온도로 RC 발진기(HSI)가 움직이는 경우를 먼저 의심한다.

</details>

<details>
<summary>Q6. tCSH 위반을 펌웨어 엔지니어에게 어떻게 설명하나요?</summary>

"마지막 SCLK rising 뒤 CS_N 이 0 ± 11 ns 에 올라간다, U7 은 30 ns 이상을 요구 — 측정 불확도를 넣어도 위반이 증명된다." 원인 후보: TXE(송신 버퍼 비었음)만 보고 CS 를 올림 — TXE 는 마지막 바이트가 시프트 레지스터로 옮겨진 순간이지 전송 완료가 아니다. 수정: BSY=0(또는 STM32H7 의 EOT, RXNE 로 마지막 수신 바이트 확인) 후 CS_N ↑, 하드웨어 NSS 의 경우 NSS 지연 설정.
꼬리질문: "100 MS/s 로 증명이 되는 이유는?" → 마진이 커서다: 진짜 4 ns vs 한계 30 ns, 측정 0 + 11 = 11 < 30. 위반이 크면 저가 LA 로도 FAIL 은 증명할 수 있다. PASS 를 증명하는 것이 더 비싸다.

</details>

<details>
<summary>Q7. LA 로 잰 타이밍과 데이터시트 타이밍이 정의상 다를 수 있는 지점은?</summary>

① 기준 레벨: 데이터시트는 VIH/VIL 또는 50% VDD 교차, LA 는 비교기 임계값 하나. 느린 에지(수 ns)에서는 수 ns 차이 → 에지가 느리면 스코프로 (HWLAB-B04 crossing). ② 측정 위치: 수신 핀 기준 — MCU 쪽에서 재면 트레이스 지연(FR-4 에서 대략 6~7 ps/mm)과 반사가 빠진다. ③ 기준 에지 정의(tCSH 가 rising 기준인지 falling 기준인지). ④ 부하 조건(데이터시트 CL 30 pF 등) vs 실제 보드 + 프로브 부하.
꼬리질문: "LA 프로브가 신호를 바꾸나?" → 입력 용량 수 pF 와 리드 인덕턴스. 고속 에지에서는 링잉을 만들어 LA 에 이중 에지로 보일 수 있다 (HWLAB-B05).

</details>

## 9. 추가 연습

### 연습 1 — MISO 출력 유효 시간 tV (max 스펙)

`miso_tv(cap)`: CS_N 창 안 SCLK falling 마다 다음 rising 전 첫 MISO 변화까지. 가상 U7 tV max 20 ns (대표값). 100 MS/s 와 500 MS/s 판정은?

<details>
<summary>답 보기</summary>

실제 출력: `100 MS/s tV n=18 max=20.0 +- 11.00 ns (truth max 12.00) -> UNRESOLVED (naive PASS)`, `500 MS/s ... max=12.0 +- 3.00 ns -> PASS`. max 스펙은 측정 최대 + U ≤ 한계여야 PASS. tV 는 U7 의 출력 스펙이지만 MCU 쪽에서 의미가 있다: mode 0 에서 MCU 는 다음 rising 에서 MISO 를 샘플하므로 `tCL − tV − 왕복 지연 ≥ MCU 입력 tSU` 가 진짜 요구다. SCLK 를 올리면 tCL 이 줄어 이 식이 먼저 깨진다 — 고속 SPI 에서 MCU 의 "RX sample delay" 설정이 있는 이유.

</details>

### 연습 2 — 반드시 판정하려면 몇 MS/s?

`min_fs_for(margin_ns, skew_ns, rates)`. 마진 3 / 6 / 26 / 40 ns 에 대해.

<details>
<summary>답 보기</summary>

측정 m 은 진짜 x 에서 최대 Ts + skew 떨어지고, 판정은 |m − L| > U = Ts + skew 를 요구 → 최악 `M − (Ts+skew) > Ts + skew` → **Ts < M/2 − skew**. 실제 출력: `3 ns -> None, 6 ns -> 1000, 26 ns -> 100, 40 ns -> 100`. 검증: 진짜 tCSS 26 ns 를 무작위 위상 60회 → `{100: 0, 500: 60, 1000: 60}` 결정. 500 MS/s 도 이번엔 다 결정했지만 보장은 아니다(충분조건). 마진 3 ns 는 표준 LA 로는 보장 불가 → 스코프.

</details>

### 연습 3 — 글리치가 히스토그램을 망칠 때

EVT2-B UART 에 200 ns 크로스토크 글리치 12개를 심는다. `uart_measure` 는? `deglitch(cap, ch, min_ps)` 후에는?

<details>
<summary>답 보기</summary>

순진한 버전은 가장 짧은 run(글리치 2샘플)을 1비트로 잡아 `T0=100.0 ns Tbit=100.0 ns` — 바이트가 전부 0x00/0xFE 쓰레기. 500 ns 미만 펄스를 에지째 지우면 `glitches=12 Tbit=8686.11 ns baud_err=-0.064 +- 0.133 %`, 메시지 복원. 실무 LA 의 glitch filter 와 같은 일이지만 **지운 개수를 리포트에 남긴다** — 글리치 자체가 SI/크로스토크 문제의 증거다. 필터 폭은 최소 정상 펄스(1비트 = 8.7 µs)보다 충분히 작게.

</details>

### 연습 4 — Modbus RTU t1.5 / t3.5

`modbus_timers_us(baud)`, `modbus_classify(gaps_us, baud)`. 보드 A 의 gap 을 Modbus 규칙으로 보면?

<details>
<summary>답 보기</summary>

Modbus over Serial Line v1.02: RTU 문자 = 11 비트, 문자 사이 침묵이 t1.5 를 넘으면 프레임 불완전(수신측 폐기), t3.5 이상이면 프레임 경계. baud > 19200 은 고정 750 µs / 1750 µs. 19200 → t1.5 = 859.4 µs, t3.5 = 2005.2 µs. 보드 A gap(9 µs, 180 µs)은 Modbus @115200 기준 전부 `OK` — 같은 측정이라도 **한계는 프로토콜 스펙이 정한다**. 이 레슨의 U9 한계(1.5 문자 = 130 µs)는 가상 호스트 프로토콜 값이다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 9/9 passed, exit=0
```

## 10. 핵심 정리

- 샘플된 에지는 [0, Ts) 늦다 → 간격 ±Ts, 다른 채널 + skew, 긴 간격 + ppm. 최악값에도 같은 U.
- 판정은 3값: `[v−U, v+U]` 가 한계 안 = PASS, 밖 = FAIL, 걸침 = UNRESOLVED. 순진한 비교는 100 MS/s 에서 진짜 위반(17 vs 20 ns)을 50% PASS 로 냈다.
- 필요 샘플링률은 마진이 정한다: 반드시 결정하려면 Ts < M/2 − skew. FAIL 증명은 싸고 PASS 증명은 비싸다.
- 주파수·baud 는 span 으로 (±Ts/N). duty·간격의 평균은 비동기일 때만 줄고, 정수비 샘플링은 편향을 그대로 남긴다.
- UART: 히스토그램 → T0 → 프레임 안 span → Tbit. 정밀도는 fs 로만 오른다 (Ts/9). 공칭 디코드 = 수신기가 보는 것.
- tSU/tH 는 데이터가 바뀐 비트에서만 잴 수 있다 → 인스턴스 수와 NO DATA 를 보고, 토글 패턴으로 커버리지 확보.
- 여러 주변장치가 같은 비율로 틀리면 클럭 트리: HSE_VALUE × 비율 → 실장 크리스털.
- LA 임계값 ≠ 데이터시트 기준 레벨. 마진이 Ts 급이거나 에지가 느리면 스코프(HWLAB-B04).

## 참고자료

- [sigrok — File format:Sigrok/v2](https://sigrok.org/wiki/File_format:Sigrok/v2) · [SPI](https://sigrok.org/wiki/Protocol_decoder:Spi) / [UART](https://sigrok.org/wiki/Protocol_decoder:Uart) 프로토콜 디코더
- Modbus Organization, *MODBUS over Serial Line Specification and Implementation Guide V1.02* — RTU 문자 11비트, t1.5/t3.5, 19200 초과 시 750 µs / 1.75 ms
- Keysight, *Logic Analyzer Fundamentals* / Tektronix, *XYZs of Logic Analyzers* — timing 모드 샘플링 불확도, channel-to-channel skew, glitch 캡처
- Maxim (ADI) Tutorial 2141, *Determining Clock Accuracy Requirements for UART Communications* — 수신기 허용 오차 유도
- ST RM (STM32F4/H7) SPI 장 — TXE / BSY / EOT 의미와 NSS 관리, USART 장 — 수신기 허용 오차 표 (값은 RM 확인)
- IEEE Std 181 — 전이 시간·기준 레벨 정의 (HWLAB-B04)
- 관련 레슨: HWLAB-B07(LA 포맷·샘플링률), HWLAB-B04(IEEE-181 crossing), HWLAB-B05(프로브·그라운드), HWLAB-B02(임계값), HWLAB-M06(커스텀 sigrok 디코더), COMM-A10(SPI/UART 디코더 — 작성 예정, 이 레슨은 축약판 내장)
