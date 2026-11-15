---
id: BM-M07
time: 90분
updated: 2026-11-11
---
# BM-M07 ADC 드라이버: 스캔 변환, 샘플링 시간, 보정

## 학습 목표

- 다채널 스캔 시퀀스를 `SQR1/2/3`(랭크 → 채널, `L = n−1`)와 `SMPR1/2`(채널 → 샘플링 코드)로 패킹하고, "샘플링 시간은 **랭크가 아니라 채널** 속성" 이라는 함정을 설명할 수 있다.
- `T_conv = T_sample + 12 ADCCLK` 과 샘플 커패시터의 RC 충전을 연결해, **소스 임피던스가 크면 짧은 샘플링에서 값이 이전 채널 쪽으로 끌려간다** 를 숫자 표로 말하고, 데이터시트식으로 SMP 를 고를 수 있다.
- EOC / EOCS / OVR 의 관계를 알고, 폴링 스캔이 OVR 없이 버티는 **처리 예산을 사이클 단위로** 계산할 수 있다 (`w ≤ P − 2`, DR 한 칸의 여유).
- VREFINT 공장 보정값으로 실제 VDDA 를 구하고, raw → mV 를 **64비트 정수 · 한 번의 반올림** 으로 구현한다 (32비트 오버플로 · 이중 반올림을 테스트로 잡는다).
- 4^n 오버샘플링이 +n 비트를 주는 조건(노이즈 = 디더)을 실험 표로 설명하고, 그 시간 비용을 계산할 수 있다.

## 1. 문제

ADC 는 펌웨어가 "바깥 세상의 숫자" 를 얻는 첫 관문이라, 면접에서 **"배터리 전압을 읽는데 10% 높게 나옵니다 / 채널 두 개를 스캔하면 서로 값이 끌려갑니다 / 가끔 OVR 이 납니다 — 왜죠?"** 가 단골이다. 답은 거의 항상 세 가지 중 하나다: VDDA 를 3.3 V 로 가정했다, 소스 임피던스에 비해 샘플링 시간이 짧다, DR 을 다음 결과 전에 못 읽었다. Don 이 RF 보드에서 본 "전력 검출기 출력이 채널 순서에 따라 다르게 읽힌다" 같은 현상이 바로 두 번째다.

이 레슨은 STM32F4 의 ADC1 을 흉내 내는 **mock ADC 모델** 을 만든다: 채널마다 입력 전압과 소스 임피던스, SQR/SMPR 레지스터, `SWSTART` → 시퀀스를 한 채널씩 ADCCLK 단위로 샘플링 + 변환, 변환마다(EOCS=1) 또는 시퀀스 끝에만(EOCS=0) EOC, DR 을 안 읽고 다음 결과가 나오면 OVR. 샘플링은 커패시터 RC 충전으로 계산한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mock ADC1: 핀 → MUX → 샘플·홀드(RC) → SAR 12 cycle → DR</text>
  <rect x="10" y="40" width="150" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="62" fill="currentColor" font-size="12">ch1 VBAT/2  R=50k</text>
  <rect x="10" y="82" width="150" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="104" fill="currentColor" font-size="12">ch4 I_SENSE R=100</text>
  <rect x="10" y="124" width="150" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="146" fill="currentColor" font-size="12">ch8 NTC     R=5k</text>
  <rect x="10" y="166" width="150" height="34" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="18" y="188" fill="currentColor" font-size="12">ch17 VREFINT R=200k*</text>
  <path d="M160 57 L210 110 M160 99 L210 115 M160 141 L210 120 M160 183 L210 125" stroke="currentColor" fill="none"/>
  <rect x="210" y="90" width="50" height="50" rx="4" fill="none" stroke="currentColor"/>
  <text x="217" y="120" fill="currentColor" font-size="12">MUX</text>
  <path d="M260 115 L290 115" stroke="currentColor"/>
  <rect x="290" y="103" width="60" height="24" rx="3" fill="none" stroke="currentColor"/>
  <text x="296" y="120" fill="currentColor" font-size="12">R_ADC</text>
  <path d="M350 115 L390 115 L390 140" stroke="currentColor" fill="none"/>
  <path d="M372 140 L408 140 M372 148 L408 148" stroke="currentColor" stroke-width="2"/>
  <path d="M390 148 L390 170 M380 170 L400 170" stroke="currentColor"/>
  <text x="412" y="149" fill="currentColor" font-size="12">C_ADC</text>
  <text x="300" y="96" fill="currentColor" font-size="12">6 kΩ, 4 pF (대표값)</text>
  <path d="M390 115 L470 115" stroke="currentColor"/>
  <rect x="470" y="95" width="90" height="40" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="480" y="113" fill="currentColor" font-size="12">SAR 12 cyc</text>
  <text x="480" y="128" fill="currentColor" font-size="12">round(V·4096/VDDA)</text>
  <path d="M560 115 L600 115" stroke="currentColor"/>
  <rect x="600" y="95" width="130" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="610" y="113" fill="currentColor" font-size="12">DR 0x4C → EOC</text>
  <text x="610" y="128" fill="currentColor" font-size="12">안 읽고 또 → OVR</text>
  <text x="200" y="215" fill="currentColor" font-size="12">샘플링(t_s = SMP/f_ADC) 동안 커패시터가 R_src + R_ADC 를 통해 충전:</text>
  <text x="200" y="235" fill="currentColor" font-family="monospace" font-size="12">V_hold = V_in + (V_prev − V_in)·exp(−t_s / τ),  τ = (R_src+R_ADC)·C_ADC</text>
  <text x="200" y="255" fill="currentColor" font-size="12">V_prev = 직전 변환의 잔류 전압 → 짧은 t_s + 큰 R_src 면 이전 채널 쪽으로 끌려간다</text>
  <rect x="10" y="275" width="720" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="20" y="293" fill="currentColor" font-size="12">SQR1/2/3: 랭크 1..16 → 채널 번호 (5비트) · L = n−1     SMPR1/2: 채널 0..18 → SMP 코드 (3비트)</text>
  <text x="20" y="311" fill="currentColor" font-size="12">CR2: ADON · EOCS · SWSTART     SR (rc_w0): EOC bit1 · STRT bit4 · OVR bit5     * VREFINT R 은 모델값</text>
</svg>
```

모델 값과 정의 (주소·비트는 **RM0090 기준 대표값 — RM 확인 권장**, 아날로그 값은 **대표값, 데이터시트 확인 권장**. 테스트의 정답은 이 모델의 정의):

| 항목 | 이 레슨의 값 | 비고 |
|---|---|---|
| ADC1 | 0x40012000: SR 0x00, CR1 0x04, CR2 0x08, SMPR1 0x0C, SMPR2 0x10, SQR1 0x2C, SQR2 0x30, SQR3 0x34, DR 0x4C | RM0090 |
| SMP 코드 0..7 | 3 / 15 / 28 / 56 / 84 / 112 / 144 / 480 cycle | T_conv = SMP + 12 (12비트) |
| ADCCLK | 21 MHz (PCLK2 84 MHz / 4) | F4 최대 36 MHz (VDDA ≥ 2.4 V) — 확인 권장 |
| C_ADC, R_ADC | 4 pF, 6 kΩ | F4 데이터시트 ADC 표의 값으로 기억 — 확인 권장 |
| 전달 함수 | `code = clamp(round(V·4096/VDDA), 0, 4095)` | 이상적 ADC, 오프셋/이득 오차 없음 |
| VREFINT | 1.21 V, R_src 200 kΩ (모델값) | 200 kΩ 은 데이터시트의 "VREFINT 샘플링 ≥ 약 10 µs" 요구에 맞춰 거꾸로 정한 값 |
| VREFINT_CAL | 0x1FFF7A2A (하프워드), VDDA = 3.3 V 에서의 raw | F4, 30 °C 측정 — 확인 권장. 모델에서는 1502 |
| tSTAB | 63 cycle (3 µs) — ADON 후 이보다 빨리 온 SWSTART 는 무시 | F4 에는 ADRDY 플래그가 없다 |
| OVR | EOCS=1 (또는 DMA=1) 일 때, EOC 가 아직 1 인데 새 결과가 나오면 OVR=1, 새 결과 버림, 시퀀스 중단. OVR=1 동안 SWSTART 무시 | RM0090 의 overrun 절차를 모델이 강제 — 확인 권장 |
| 시간 | 1 tick = 1 ADCCLK, **버스 접근 1회 = 1 tick** | BM-B02 mock 규칙 |

### Part A — 레지스터 메커닉: 시퀀스 설정 · 폴링 스캔 · raw → mV

- `adc_config_scan(chs[], n, smp[])`: 전부 **검증한 다음에** 쓴다 (n 1..16, ch 0..18, smp 0..7, 같은 채널에 다른 smp 금지). SQR 은 통째로 다시 쓰고(안 쓰는 랭크 = 0), SMPR 은 쓰는 채널의 3비트만 RMW.
- `adc_read_scan_polled(out, n, cb, ctx)`: `adc_enable()` → SCAN → `EOCS=1` → SR 클리어 → `SWSTART` → 랭크마다 EOC 대기(상한 있음) → DR. `cb` 는 샘플마다 "다른 일" (로그 출력 등).
- `adc_vdda_mv(cal, data, extra_bits)`, `adc_raw_to_mv(raw, bits, vdda)`, `adc_raw_to_mv_cal(raw, cal, data)`: 64비트 정수, round-half-up. 전수 테스트(raw 0..4095 × DATA 1300..1800 = 2,052,096 케이스)로 독립 기준과 비교.

### Part B — 면접관이 찌르는 고장 (전부 주입하고 결과를 assert)

- 잘못된 설정 5종 → `ADC_EINVAL` 이고 **레지스터 쓰기 0회**. `n` 이 `L+1` 과 다르면 읽기도 거부.
- `EOCS=0` 으로 폴링 스캔 → 시퀀스 끝에만 EOC → 첫 대기에서 **마지막 채널 값**을 받고 나머지는 타임아웃.
- 샘플마다 `w` 사이클의 일을 하는 느린 루프 → OVR. OVR 없는 최대 `w` 를 n = 3..16 × SMP 3종에 대해 찾아 공식과 일치하는지.
- OVR 을 안 지우고 SWSTART → 무시됨. `ADON|SWSTART` 한 번에 → tSTAB 전이라 무시 → 타임아웃.
- 50 kΩ 소스를 0 V 채널 다음에 짧은 SMP 로 → 낮게 읽힘 (오차 표). `adc_pick_smp(R_src, f_ADC)` 로 고른 SMP 는 400개 무작위 소스에서 오차 0.
- 32비트 곱 오버플로 · 두 단계 반올림 → 기준과 불일치 개수를 센다.

### Part C — 펌웨어 현실: 인터럽트 스캔 · 보드 모니터 · 오버샘플링

- EOCIE + OVRIE 인터럽트 스캔: `adc_start_scan_it()` + `ADC_IRQHandler()` — NVIC-lite 의 `sim_step()` 이 레벨 라인을 보고 핸들러를 부른다 (BM-M 공통 규약).
- 보드 모니터: VDDA 가 실제로 **3.0 V** 인 보드에서 VREFINT · 배터리 분압 · 전류 감지 · NTC 를 스캔 → 보정된 mV 로그. 3.3 V 가정과 비교.
- 다른 고우선 ISR 이 60 cycle 동안 ADC IRQ 를 막음(지연 주입) → OVR → 핸들러가 시퀀스를 처음부터 재시작 → 최종 값은 정상, `ovr=1`.
- `adc_oversample(ch, smp, n, &out)`: 4^n 합 → `(sum + 2^(n−1)) >> n`. 노이즈 0 이면 결과는 정확히 `4·code` (정보 증가 없음), σ = 0.5 LSB 면 RMS 오차가 줄어드는지.

## 2. Problem (English)

> **Write a polled + interrupt-driven multi-channel ADC driver for an STM32F4-style ADC, with VREFINT calibration and oversampling.**
>
> Registers (offsets from 0x40012000): SR 0x00 (EOC bit1, STRT bit4, OVR bit5; rc_w0), CR1 0x04 (EOCIE 5, SCAN 8, OVRIE 26), CR2 0x08 (ADON 0, EOCS 10, SWSTART 30), SMPR1 0x0C (channels 10–18, 3 bits each), SMPR2 0x10 (channels 0–9), SQR1 0x2C (SQ13–16, L[23:20] = n−1), SQR2 0x30 (SQ7–12), SQR3 0x34 (SQ1–6), DR 0x4C (reading clears EOC). Sample-time codes 0..7 = 3/15/28/56/84/112/144/480 ADCCLK; one 12-bit conversion = sample + 12 cycles.
>
> 1. `int adc_config_scan(const uint8_t *chs, unsigned n, const uint8_t *smp);` — rank i converts `chs[i]` with sample code `smp[i]`. Validate everything first (n 1..16, ch 0..18, smp 0..7, a channel listed twice must have the same smp) and touch no register on error. Only the SMPR fields of the listed channels may change.
> 2. `int adc_read_scan_polled(uint16_t *out, unsigned n, adc_sample_cb cb, void *ctx);` — EOC per conversion, bounded polling, returns n, `ADC_ETIMEOUT` or `ADC_EOVR`.
> 3. `uint32_t adc_vdda_mv(uint16_t cal, uint32_t vref_data, unsigned extra_bits);` and `uint32_t adc_raw_to_mv_cal(uint32_t raw, uint16_t cal, uint32_t vref_data);` — integer only, round half up, no intermediate rounding, no overflow.
> 4. `int adc_pick_smp(uint32_t r_src_ohm, uint32_t f_adc_hz);` — shortest code with R_AIN,max(k) = (k−0.5)/(f·C_ADC·ln 2^14) − R_ADC ≥ r_src.
> 5. `int adc_oversample(uint8_t ch, uint8_t smp, unsigned n, uint32_t *out);` — (12+n)-bit result from 4^n conversions.
> 6. `void ADC_IRQHandler(void);` — collects a scan into a buffer; on OVR restarts the sequence.
>
> Example: `adc_config_scan({17,1,4,8}, 4, {7,3,1,1})` on reset registers → `SQR1=0x00300000 SQR3=0x00041031 SMPR1=0x00E00000 SMPR2=0x01001018`. With CAL = 1502 and a VREFINT reading of 1652, `adc_vdda_mv` = 3000.
>
> Follow-ups: Why is the sample time per channel, not per rank? How much work per sample can the polling loop do before OVR? Why does oversampling a perfectly clean DC signal not add resolution? `raw*3300/4095` — what's wrong with it?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter     # 7/39 passed, exit 1
./solution -v     # (정답 빌드) 3랭크 스캔의 사이클 타임라인: work = 0 / 26 / 27
```

TODO 8곳: `adc_config_scan`, `adc_read_scan_polled` (Part A), `adc_vdda_mv`, `adc_raw_to_mv`, `adc_raw_to_mv_cal` (Part A), `adc_pick_smp` (Part B), `adc_oversample`, `ADC_IRQHandler` (Part C). mock 코어(BM-B02 축약판), NVIC-lite, ADC 모델, `adc_enable`, `adc_start_scan_it` 는 주어진다. 스텁은 즉시 돌아오고 모든 시뮬레이션 루프에 상한이 있어서 시작 코드는 멈추지 않는다 (처음부터 PASS 인 7개는 주어진 `adc_conv_cycles` · 스텁이 늘 `ADC_EINVAL` 이라 우연히 맞는 설정 거부 4개 · 모델 자체가 트리거를 무시하는 2개).

## 4. 힌트

<details>
<summary>힌트 1 — 랭크 r 은 어느 레지스터의 몇 번째 비트?</summary>

랭크 0..5 → SQR3 의 `5·r`, 6..11 → SQR2 의 `5·(r−6)`, 12..15 → SQR1 의 `5·(r−12)`. 길이는 SQR1 `[23:20]` 에 **n−1**. 채널 c 의 샘플링 코드는 c < 10 이면 SMPR2 의 `3·c`, 아니면 SMPR1 의 `3·(c−10)`. 랭크 번호와 SMPR 위치는 아무 상관이 없다.

</details>

<details>
<summary>힌트 2 — 검증은 루프 하나, 쓰기는 그 뒤</summary>

중간에 잘못된 값을 만나 `return` 하면 앞 랭크까지만 써진 "반쯤 설정된 ADC" 가 남는다. 첫 루프에서 모든 인자를 확인하고 (중복 채널은 앞 원소와 비교), 둘째 루프에서 레지스터 값을 **지역 변수에** 만든 다음 마지막에 다섯 번 쓴다.

</details>

<details>
<summary>힌트 3 — 폴링 루프는 EOC 와 OVR 을 같이 본다</summary>

`while (!((sr = REG_READ(SR)) & (EOC | OVR)))` + 상한. OVR 이 먼저 나면 EOC 를 기다리는 루프는 (EOC 가 이미 1 이라) 그냥 빠져나와 **이전** 결과를 읽게 된다. OVR 을 먼저 검사해서 `ADC_EOVR`.

</details>

<details>
<summary>힌트 4 — mV 는 곱을 다 하고 한 번만 나눈다</summary>

`3300 · CAL · raw` 는 최대 3300·4095·4095 ≈ 5.5·10^10 → `uint64_t`. 분모 `DATA · 4096` 도 64비트. 반올림은 `(num + den/2) / den`. VDDA 를 먼저 mV 정수로 만들고 다시 곱하면 반올림이 두 번 일어난다.

</details>

<details>
<summary>힌트 5 — ISR 의 OVR 경로</summary>

SR 은 rc_w0: `REG_WRITE(SR, ~(OVR|EOC|STRT))` 는 그 세 비트만 0 으로 만들고 나머지(1 을 쓴 비트)는 그대로 둔다. OVR 을 먼저 지워야 모델이 다음 `SWSTART` 를 받아준다. `idx = 0` 으로 처음부터.

</details>

## 5. 풀이

### Part A — 시퀀스 패킹: 예제를 비트로

`chs = {17, 1, 4, 8}`, `smp = {7, 3, 1, 1}` (VREFINT 는 길게, 배터리 분압은 중간, 저임피던스 둘은 짧게):

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">SQR3 = 0x00041031 (SQ1..SQ4 사용, 5비트씩) · SQR1 L[23:20] = 3</text>
  <rect x="10" y="30" width="120" height="30" class="f-mut" stroke="currentColor"/>
  <text x="20" y="50" fill="currentColor">[31:20] 0</text>
  <rect x="130" y="30" width="150" height="30" class="f-ok" stroke="currentColor"/>
  <text x="138" y="50" fill="currentColor">SQ4 [19:15] = 8</text>
  <rect x="280" y="30" width="150" height="30" class="f-ok" stroke="currentColor"/>
  <text x="288" y="50" fill="currentColor">SQ3 [14:10] = 4</text>
  <rect x="430" y="30" width="150" height="30" class="f-ok" stroke="currentColor"/>
  <text x="438" y="50" fill="currentColor">SQ2 [9:5] = 1</text>
  <rect x="580" y="30" width="150" height="30" class="f-acc" stroke="currentColor"/>
  <text x="588" y="50" fill="currentColor">SQ1 [4:0] = 17</text>
  <text x="10" y="80" fill="currentColor" font-family="monospace">8&lt;&lt;15 | 4&lt;&lt;10 | 1&lt;&lt;5 | 17 = 0x40000 + 0x1000 + 0x20 + 0x11 = 0x41031</text>
  <text x="10" y="112" fill="currentColor" font-weight="bold" font-size="13">SMPR2 = 0x01001018 (ch0..9, 3비트씩) — 랭크가 아니라 채널 번호로 위치가 정해진다</text>
  <rect x="10" y="124" width="80" height="30" class="f-mut" stroke="currentColor"/>
  <text x="16" y="144" fill="currentColor">ch9 [29:27]</text>
  <rect x="90" y="124" width="100" height="30" class="f-ok" stroke="currentColor"/>
  <text x="96" y="144" fill="currentColor">ch8 [26:24]=1</text>
  <rect x="190" y="124" width="180" height="30" class="f-mut" stroke="currentColor"/>
  <text x="200" y="144" fill="currentColor">ch7..ch5 (안 건드림)</text>
  <rect x="370" y="124" width="100" height="30" class="f-ok" stroke="currentColor"/>
  <text x="376" y="144" fill="currentColor">ch4 [14:12]=1</text>
  <rect x="470" y="124" width="120" height="30" class="f-mut" stroke="currentColor"/>
  <text x="480" y="144" fill="currentColor">ch3, ch2</text>
  <rect x="590" y="124" width="80" height="30" class="f-ok" stroke="currentColor"/>
  <text x="594" y="144" fill="currentColor">ch1 [5:3]=3</text>
  <rect x="670" y="124" width="60" height="30" class="f-mut" stroke="currentColor"/>
  <text x="676" y="144" fill="currentColor">ch0</text>
  <text x="10" y="176" fill="currentColor" font-family="monospace">1&lt;&lt;24 | 1&lt;&lt;12 | 3&lt;&lt;3 = 0x01000000 + 0x1000 + 0x18 = 0x01001018</text>
  <text x="10" y="206" fill="currentColor" font-weight="bold" font-size="13">SMPR1 = 0x00E00000 : ch17 → [23:21] = 7  (3·(17−10) = 21)</text>
  <text x="10" y="232" fill="currentColor">같은 채널을 랭크 두 곳에 넣는 것은 허용 (오버샘플링 트릭), 단 SMP 는 하나뿐 → 다르면 EINVAL</text>
</svg>
```

실제 출력 (`./solution`):

```text
PASS  adc_config_scan(ch, 4, sm) == ADC_OK
      SQR1=0x00300000 SQR2=0x00000000 SQR3=0x00041031 SMPR1=0x00E00000 SMPR2=0x01001018
      single-channel exhaustive: 456 cases, 0 bad
```

테스트 설계: (1) 채널 0..18 × SMP 0..7 × 무작위 prefill 3개 = 456 케이스에서 **목표 3비트만** 바뀌었는지(나머지 SMPR 비트는 prefill 그대로), (2) 무작위 시퀀스 3000개를 **독립 디코더**(`dec_len/dec_rank/dec_smp`)로 되읽어 입력과 같은지 + 안 쓴 채널의 SMP 필드가 prefill 그대로인지. mock 은 구현된 비트만 저장한다 (SQR1 `0x00FFFFFF`, SQR2/3 `0x3FFFFFFF`, SMPR1 `0x07FFFFFF`, SMPR2 `0x3FFFFFFF`) — 진짜 칩의 reserved 비트처럼.

설정을 "검증 → 지역 변수에 계산 → 마지막에 쓰기" 로 나눈 이유: 잘못된 4번째 채널 때문에 리턴할 때 SQR3 의 앞 3랭크만 바뀐 상태로 남으면, 다음 `SWSTART` 가 엉뚱한 채널을 변환한다. 테스트는 거부 케이스들 후에 `M.writes` 가 그대로인지 본다.

### Part A — 폴링 스캔의 사이클 타임라인

SMP=3 cycle 이면 한 랭크 주기 `P = 3 + 12 = 15` ADCCLK. `./solution -v` 의 진짜 출력 (S = 샘플링 시작, H = 홀드, E = 결과·EOC, D = DR 읽기, O = OVR; @ 뒤는 첫 샘플링 시작 기준 tick):

```text
-- 3-rank scan, smp=3cyc (P=15), work=0 -> rc=3
  S0@0 H0@3 E0@15 S1@15 D0@16 H1@18 E1@30 S2@30 D1@31 H2@33 E2@45 D2@46 
-- 3-rank scan, smp=3cyc (P=15), work=26 -> rc=3
  S0@0 H0@3 E0@15 S1@15 D0@16 H1@18 E1@30 S2@30 H2@33 D1@44 E2@45 D2@72 
-- 3-rank scan, smp=3cyc (P=15), work=27 -> rc=-3
  S0@0 H0@3 E0@15 S1@15 D0@16 H1@18 E1@30 S2@30 H2@33 O2@45 D1@45 
```

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">결과 k 의 DR 읽기는 E(k+1) 이전이어야 한다 (P = 15, 1 tick = 8 px, 0..75)</text>
  <path d="M60 50 L700 50" stroke="currentColor"/>
  <text x="10" y="54" fill="currentColor">ADC</text>
  <rect x="60" y="40" width="24" height="20" class="f-mut" stroke="currentColor"/><rect x="84" y="40" width="96" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="180" y="40" width="24" height="20" class="f-mut" stroke="currentColor"/><rect x="204" y="40" width="96" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="300" y="40" width="24" height="20" class="f-mut" stroke="currentColor"/><rect x="324" y="40" width="96" height="20" class="f-acc" stroke="currentColor"/>
  <text x="66" y="76" fill="currentColor">샘플 3 + SAR 12</text>
  <path d="M180 34 L180 66 M300 34 L300 66 M420 34 L420 66" stroke="currentColor" stroke-width="2"/>
  <text x="170" y="30" fill="currentColor">E0@15</text><text x="290" y="30" fill="currentColor">E1@30</text><text x="410" y="30" fill="currentColor">E2@45</text>
  <text x="10" y="114" fill="currentColor">w=0</text>
  <path d="M60 110 L700 110" stroke="currentColor" stroke-dasharray="3 3"/>
  <circle cx="188" cy="110" r="5" class="f-ok" stroke="currentColor"/><circle cx="308" cy="110" r="5" class="f-ok" stroke="currentColor"/><circle cx="428" cy="110" r="5" class="f-ok" stroke="currentColor"/>
  <text x="180" y="128" fill="currentColor">D0@16</text><text x="300" y="128" fill="currentColor">D1@31</text><text x="420" y="128" fill="currentColor">D2@46</text>
  <text x="10" y="164" fill="currentColor">w=26</text>
  <path d="M60 160 L700 160" stroke="currentColor" stroke-dasharray="3 3"/>
  <circle cx="188" cy="160" r="5" class="f-ok" stroke="currentColor"/><rect x="196" y="154" width="208" height="12" class="f-warn" stroke="currentColor"/>
  <circle cx="412" cy="160" r="5" class="f-ok" stroke="currentColor"/><text x="396" y="178" fill="currentColor">D1@44 (E2 직전 마지막 기회)</text>
  <text x="10" y="214" fill="currentColor">w=27</text>
  <path d="M60 210 L700 210" stroke="currentColor" stroke-dasharray="3 3"/>
  <circle cx="188" cy="210" r="5" class="f-ok" stroke="currentColor"/><rect x="196" y="204" width="216" height="12" class="f-warn" stroke="currentColor"/>
  <text x="430" y="214" fill="currentColor">poll@44 → tick 45 = E2 인데 EOC(결과1) 아직 1 → OVR</text>
</svg>
```

읽는 법: E0 이 tick 15 에 서면, 그 tick 을 만든 SR 폴링이 다음 접근에서 EOC 를 보고, DR 읽기는 16. 드라이버가 따라잡고 있으면 결과마다 **2 tick (SR 폴링 1 + DR 읽기 1)** 만 쓴다. 결과 k 의 DR 읽기는 늦어도 `E(k+1) − 1` 에 일어나야 한다 (`E(k+1)` tick 에 EOC 가 아직 1 이면 OVR).

**예산 공식.** 처리 시간 `w` 가 `P − 2` 보다 크면 드라이버는 결과마다 `d = w + 2 − P` 씩 뒤처진다. 결과 0 은 E0+1 에 읽으므로, 결과 k 의 읽기 시각은 `E0 + 1 + k(w+2)`. 조건 `E0 + 1 + k(w+2) ≤ E0 + (k+1)P − 1` 을 OVR 이 가능한 마지막 결과 `k = n−2` 에서 풀면

`w_max = (P − 2) + ⌊(P − 2) / (n − 2)⌋`

— 정상 상태 예산은 **P − 2**, 거기에 DR 한 칸이 주는 **일회성 여유 P − 2** 가 시퀀스 길이에 나눠진다. 테스트는 n = 3..16 × SMP {3, 15, 56} 의 42 조합 모두에서 모델로 찾은 최대 w 가 공식과 같음을 확인한다:

```text
      max work w/o OVR (smp 3/15/56 cyc): n=3 26/50/132  n=4 19/37/99  n=16 13/26/70
PASS  bad == 0
```

면접용 한 줄: "ADC 가 1.4 MSPS (21 MHz, 15 cycle) 면 샘플당 15 ADCCLK = 714 ns — CPU 168 MHz 기준 약 120 사이클. printf 한 번이면 끝. 그래서 폴링 스캔은 짧은 시퀀스나 느린 SMP 에만 쓰고, 연속 샘플링은 DMA(BM-M08/M09)."

### Part A — raw → mV 와 VREFINT 보정

`VDDA` 를 모르면 `raw` 는 "VDDA 의 몇 분의 몇" 일 뿐이다. 공장이 VDDA = 3.3 V 에서 VREFINT 를 읽어 둔 값 `CAL` 과 지금 읽은 `DATA` 의 비율이 VDDA 를 알려준다 (VREFINT 자체는 VDDA 와 무관한 밴드갭 전압):

| 식 | 이 레슨의 구현 |
|---|---|
| `VDDA = 3300 · CAL / DATA` [mV] | `adc_vdda_mv(cal, data, 0)` — 1502 / 1652 → **3000** |
| `V_x = VDDA · raw / 4096` | `adc_raw_to_mv(raw, 12, vdda)` |
| 합치면 `V_x = 3300 · CAL · raw / (DATA · 4096)` | `adc_raw_to_mv_cal` — 64비트, 나눗셈 1회 |

```text
      raw_to_mv_cal exhaustive: 2052096 cases, 0 bad
      VREFINT_CAL=1502 DATA=1652 -> VDDA=3000 mV (truth 3000)
      raw 0..4095 @CAL=1502 DATA=1652: 32-bit naive wrong 3661, two-step wrong 738, one-shot wrong 0
```

- 기준(reference)은 다른 방식으로 쓴 `q + (2r ≥ den)` — `(num + den/2)/den` 과 같은 round-half-up 이지만 독립 구현.
- **32비트 곱** `3300u * 1502u * raw` 는 raw ≥ 867 부터 2^32 를 넘는다: 4096 개 중 3661 개가 틀리고, 그중 3229 개가 오버플로, 432 개는 넘치기 전 구간에서 반올림 대신 버림을 한 탓 (파이썬으로 분해해 확인).
- **두 단계** (VDDA 를 3000 mV 정수로 반올림한 뒤 다시 곱함) 는 738 개가 ±1 mV 틀린다. 1 mV 는 작아 보이지만, 같은 값을 여러 단계 필터·스케일에 넣으면 누적된다 — "반올림은 마지막에 한 번".
- `CAL` 은 `0x1FFF7A2A` 의 **하프워드** — 4바이트 정렬이 아니다. 이 레슨의 mock 은 32비트 레지스터 표라서 `mock_read16` 이 `0x1FFF7A28` 워드의 상위 하프워드를 잘라 준다. 타깃에서는 `*(const uint16_t *)0x1FFF7A2A` (정렬된 하프워드 읽기, 문제 없음).

4095 냐 4096 이냐: ST 의 LL 매크로 등은 `raw · VREF / 4095` 를 쓴다(full-scale 코드로 나눔, 확인 권장). 이 레슨의 모델은 `round(V·4096/VDDA)` 라서 `/4096` 이 정확한 역함수다. 차이는 최대 1 LSB (≈ 0.8 mV @3.3 V, 풀스케일 근처). 어느 쪽이든 **팀의 전달 함수 정의와 맞추는 것** 이 핵심이고, VDDA 보정 오차(아래 Part C, ~2 mV)가 이보다 크다.

### Part B — 고장 표 (실제 출력)

| 주입 | 기대 | 결과 |
|---|---|---|
| n=0, n=17, ch 19, smp 8, ch1 이 smp 3 과 4 둘 다 | `ADC_EINVAL`, 쓰기 0회 | PASS (5 케이스) |
| n=4 로 읽기, L+1=3 | `ADC_EINVAL` | PASS |
| `EOCS=0` 폴링 (ch 0.5/1.5/2.5 V) | 첫 대기에서 ch3 값, 이후 타임아웃 | `got[0]=3103 (ch3 ideal 3103, ch1 ideal 621), rc=-2` |
| 콜백 40 cycle, P=27, n=4 (w_max 37) | `ADC_EOVR`, ovr_events 1 | PASS |
| OVR 상태에서 SWSTART 만 | 무시 (ignored 1), busy=0 | PASS → 드라이버 재호출(SR 클리어 후)은 4/4 |
| ADON · EOCS · SWSTART 를 한 번의 CR2 쓰기로 | 무시, 2000 폴링 타임아웃 | PASS → `adc_enable()` 경로는 1/1 |

`EOCS=0` 이 왜 이렇게 되나: EOCS=0 은 "시퀀스 끝에만 EOC" 이고 DR 은 변환마다 덮어써진다. DMA 가 매 결과를 가져가는 구성(BM-M08/M09)에서는 맞는 설정이지만, CPU 폴링에서는 마지막 값만 남는다. 그리고 EOCS=0 + DMA=0 에서는 OVR 감시도 꺼져 있어서 **아무 에러 없이 조용히** 틀린다 — 테스트는 `ovr_events == 0` 까지 확인한다.

### Part B — 샘플링 시간 × 소스 임피던스

`demo_smp_table.c` (모델과 같은 RC 식) 의 진짜 출력 — 0 V 채널 바로 다음에 3.000 V 를 읽을 때의 오차 [LSB]:

<!-- code: demo_smp_table.c -->

```text
[1] error [LSB] reading 3.000 V right after a 0 V channel, f_ADC = 21 MHz
  R_src   |      3     15     28     56     84    112    144    480   (SMP cycles)
  --------+--------------------------------------------------------
       0k |    -10      0      0      0      0      0      0      0
       1k |    -23      0      0      0      0      0      0      0
       5k |   -145      0      0      0      0      0      0      0
      10k |   -400      0      0      0      0      0      0      0
      50k |  -1968   -154    -10      0      0      0      0      0
     100k |  -2659   -691   -161     -7     -1      0      0      0
     200k |  -3131  -1565   -739   -147    -29     -6     -1      0

[2] datasheet-style R_AIN max [kOhm] (negative = not even with 0 ohm)
  SMP cyc |  21 MHz   36 MHz
        3 |     -2.9     -4.2
       15 |     11.8      4.4
       28 |     27.7     13.7
       56 |     62.1     33.7
       84 |     96.4     53.8
      112 |    130.8     73.8
      144 |    170.0     96.7
      480 |    582.2    337.1

[3] crosstalk the other way: read 0.300 V right after a 3.0 V channel, 50k source, 21 MHz
  3:+1771  15:+139  28:+9  56:+0  84:+0  112:+0  144:+0  480:+0  
```

- 오차의 부호가 **이전 채널 쪽** 이다: 0 V 다음이면 낮게, 3 V 다음이면 높게. "채널 순서를 바꾸면 값이 바뀐다" 가 이 현상의 지문(fingerprint)이다.
- 3 cycle 은 R_src = 0 이어도 풀스케일 계단에서 10 LSB 모자란다 — 데이터시트식([2])도 음수(R_ADC 6 kΩ 을 최댓값으로 보면 3 cycle 은 12비트 ¼ LSB 를 보장 못 함). 모델과 공식이 같은 결론. 36 MHz 표는 같은 사이클 수가 더 짧은 시간이라 더 나쁘다.
- 공식은 `(k − 0.5)` 와 ¼ LSB 를 쓰므로 모델(`k` 사이클, 반올림 양자화)보다 보수적이다. 그래서 `adc_pick_smp` 로 고른 코드는 모델에서 최악 계단(0 V → 3.2 V)에도 오차 0:

```text
      50k source, 3.0 V after 0 V: err[LSB] by smp = -1968 -154 -10 0 0 0 0 0
      pick_smp property: 400 sources, 0 bad
```

  `adc_pick_smp`: 0 Ω → 코드 1 (15 cyc), 50 kΩ → 3 (56), 200 kΩ → 7 (480), 600 kΩ → 불가(EINVAL — 버퍼 앰프나 외부 커패시터, 연습 4). ADCCLK 를 7 MHz 로 낮추면 0 Ω 은 3 cycle 로 충분 → "느린 ADCCLK + 짧은 SMP" 와 "빠른 ADCCLK + 긴 SMP" 는 같은 샘플링 시간을 다른 방식으로 사는 것.

### Part C — 인터럽트 스캔과 보드 모니터

`adc_start_scan_it` 는 `EOCIE | OVRIE | SCAN`, `EOCS=1` 을 켜고 `SWSTART`. 모델은 매 tick 끝에 `irq_line = (EOC && EOCIE) || (OVR && OVRIE)` 를 NVIC-lite 에 올리고, `sim_step()` 이 라인이 서 있으면 핸들러를 부른다 (레벨 감지 — DR 을 읽어 EOC 가 내려가야 라인이 내려간다).

```c
void ADC_IRQHandler(void) {
    uint32_t sr = REG_READ(ADC_SR);
    if (sr & ADC_SR_OVR) { g_adc.ovr_count++; g_adc.idx = 0;
        REG_WRITE(ADC_SR, ~(ADC_SR_OVR | ADC_SR_EOC | ADC_SR_STRT));   /* rc_w0: 이 셋만 0 */
        REG_WRITE(ADC_CR2, ADC_CR2_ADON | ADC_CR2_EOCS | ADC_CR2_SWSTART); return; }
    if (sr & ADC_SR_EOC) { /* DR → buf[idx++], idx == n → done */ }
}
```

보드: VDDA 가 실제로 3.0 V (LDO 를 바꿨거나 배터리 직결). 채널별 SMP 는 `adc_pick_smp` 가 고른다. 진짜 출력:

```text
PASS  ok
      [adc] smp=7/3/1/1 seq=614 cyc=29.24 us | VDDA=3000 mV (VREFINT raw=1652 CAL=1502)
      [adc] VBAT=3700 mV (naive@3300: 4070) I_SENSE=420 mV NTC=1400 mV  isr=4 ovr=0
PASS  bat >= 3697 && bat <= 3703 && cur >= 419 && cur <= 421 && ntc >= 1399 && ntc <= 1401
PASS  bat_naive > 4000
```

- 시퀀스 한 바퀴 614 cycle = 29.24 µs 중 492 cycle 이 VREFINT 다. VDDA 는 천천히 변하므로 실제 펌웨어는 VREFINT 를 매 스캔이 아니라 가끔(예: 1 초마다) 읽어 캐시하는 경우가 많다.
- 3.3 V 를 가정한 `raw·3300/4096` 은 배터리를 **4070 mV** 로 읽는다 (+10 %). 배터리 보호 로직이 "4.0 V 이상 = 충전 완료" 라면 3.7 V 에서 충전을 멈춘다. 면접의 "10 % 높게 나와요" 의 정답.
- 허용 오차 ±3 mV 의 근거: CAL 과 DATA 가 각각 ±½ LSB 양자화 → VDDA 상대오차 ≲ 0.5/1502 + 0.5/1652 ≈ 6.4·10⁻⁴ (≈ 2 mV @3 V) + 최종 반올림 ½ mV. VREFINT 를 오버샘플링하면 이 오차가 줄어든다 (`adc_vdda_mv` 의 `extra_bits` 인자).

ISR 지연 주입: 랭크 0 을 읽은 직후 다른 고우선 ISR 이 60 cycle 동안 ADC IRQ 를 막는다 (`NV.mask_ticks = 60`, P = 27):

```text
      ISR latency 60 cyc: ovr=1, isr calls=5, conversions=6
```

변환 6번 = 랭크0 · 랭크1(읽히지 못함) · 랭크2(→ OVR, 버려짐) + 재시작 3번. ISR 5번 = EOC 1 + OVR 1 + EOC 3. 최종 버퍼는 이상값과 일치. 교훈: **인터럽트 기반 스캔의 실제 예산은 "다른 ISR 의 최악 실행 시간 + 내 ISR 진입 지연 < 한 랭크 주기"** — 이걸 못 지키면 DMA(BM-M08/M09) 로 간다. 우선순위 설계는 BM-M02, 지연 측정은 ARM-A08 (다음 단계).

### Part C — 오버샘플링: +n 비트는 노이즈가 있어야 생긴다

4^n 개를 더하면 신호는 4^n 배, 서로 독립인 노이즈는 √(4^n) = 2^n 배 → SNR 이 2^n 배 = n 비트. 그래서 n 비트를 얻는 데 2^n 이 아니라 **4^n** 개. 결과는 `(sum + 2^(n−1)) >> n` 로 (12+n) 비트 — 그냥 `>> n` 하면 −½ LSB(12+n) 편향.

조건은 "양자화 오차가 샘플마다 달라야 한다" 이다. 노이즈 0 이면 같은 DC 를 몇 번 읽어도 같은 코드 → 평균도 그 코드. 테스트가 이것을 정확히 확인한다 (`r14 == 4·code`, 200 개 레벨). `demo_oversample.c` 의 진짜 출력:

<!-- code: demo_oversample.c -->

```text
RMS error [LSB12] of the (12+n)-bit result, 2000 DC levels
  sigma |    n=0    n=1    n=2    n=3    n=4   (samples 1/4/16/64/256)
    0.0 |  0.286  0.286  0.286  0.286  0.286
    0.1 |  0.302  0.246  0.215  0.201  0.196
    0.3 |  0.421  0.262  0.131  0.075  0.049
    0.5 |  0.564  0.341  0.159  0.081  0.040
    1.0 |  1.067  0.564  0.267  0.138  0.068
    2.0 |  2.014  1.040  0.526  0.255  0.125

ideal (12+n)-bit quantizer alone: n=0 0.289  n=1 0.144  n=2 0.072  n=3 0.036  n=4 0.018  

fixed input 1.0004 V (code boundary not crossed), sigma=0: 16 samples -> 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242 1242

time per (12+n)-bit result @21 MHz ADCCLK, SMP=15 cyc (27 cyc/conv):
  n=0    1 conv      1.29 us  -> max   777778 results/s
  n=1    4 conv      5.14 us  -> max   194444 results/s
  n=2   16 conv     20.57 us  -> max    48611 results/s
  n=3   64 conv     82.29 us  -> max    12153 results/s
  n=4  256 conv    329.14 us  -> max     3038 results/s
```

- σ = 0: n 을 아무리 키워도 0.286 (= 1/√12, 12비트 양자화 오차) 그대로. **이득 0.**
- σ = 0.1 LSB: 디더가 부족해 0.2 근처에서 바닥.
- σ = 0.3–0.5 LSB: n 마다 거의 절반 — 이상적인 (12+n) 비트 양자화기에 근접 (n=4 에서 0.04–0.05 vs 이상 0.018 + 노이즈 σ/16).
- σ 가 크면(2 LSB) 같은 정밀도에 n 이 하나 더 필요. 노이즈는 "적당히" 가 최적이다. 실제 보드에서는 보통 열잡음·전원 리플이 이 역할을 하고, 부족하면 의도적으로 디더를 넣는다.
- 공짜가 아니다: 16비트(n=4) 결과는 초당 3038 개. 그리고 평균은 저역통과 필터라서 신호가 그 사이에 변하면 안 된다 — 느린 센서(온도, 배터리)에만.
- solution 의 체크 (σ = 0.5): `RMS err n=0 0.562, n=2 0.165, n=4 0.036 LSB12` → `rms[2] < 0.6·rms[0]`, `rms[4] < 0.5·rms[2]`.

신형 STM32(L4/G4/H7 등)에는 하드웨어 오버샘플러(비율·시프트 레지스터)가 있다 — F4 에는 없어서 이 레슨처럼 소프트웨어로 하거나 DMA 버퍼를 평균한다 (확인 권장).

### 호스트 모델 ↔ 실제 칩

| 이 레슨의 모델 | 실제 F4 (확인 권장) |
|---|---|
| ch17 은 항상 VREFINT 에 연결 | `ADC_CCR.TSVREFE` (0x40012304 bit 23) 를 켜야 연결된다 |
| ADCCLK 21 MHz 고정 | `ADC_CCR.ADCPRE` 로 PCLK2 /2,/4,/6,/8 |
| SWSTART 순간 SQR/SMPR 래치 | 변환 중 설정 변경은 RM 이 금지/주의 — 어차피 하지 말 것 |
| 오프셋·이득 오차 · INL/DNL 없음 | 데이터시트 TUE ±2–5 LSB 수준 (보정은 2점 보정이 필요) |
| OVR 후 SWSTART 무시 | RM0090 overrun 절차: OVR 클리어 후 재트리거 (DMA 면 DMA 재설정까지) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 39/39 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o so_asan && ./so_asan
```

## 7. 자주 하는 실수

1. **SMP 를 랭크 순서로 넣는다** — `SMPR2 |= smp[i] << 3*i`. SMPR 은 채널 번호로 인덱싱된다. 고친 코드: `ch < 10 ? 3*ch : 3*(ch-10)`, 그리고 같은 채널이 두 랭크에 있으면 SMP 도 같아야 한다 (다르면 EINVAL — 마지막 쓴 값이 조용히 이기는 것보다 낫다).
2. **`L = n`** — SQR1 의 L 은 "길이 − 1". `L = n` 이면 랭크 하나가 더 돌고, 그 랭크의 SQ 필드(보통 0 = ch0)가 변환된다. 폴링 루프는 n 개만 읽고 나오니, 그 여분 변환이 아직 진행 중이면 다음 호출의 SWSTART 가 무시되고(이 모델: busy 중 트리거 무시) 그 여분 결과를 첫 랭크 값으로 읽게 된다. 검사를 뺀 드라이버 복사본으로 실험(ch1/2/3 = 1/2/3 V, `L = 3`): 첫 호출 `1241 2482 3724` 정상 → 둘째 호출 `rc=-2`, 첫 값 **11** (여분 랭크의 ch0, 설정 안 한 SMP 코드 0 = 3 cycle 이라 3 V 잔류 전하까지 섞임) → 셋째 호출은 다시 정상. 간헐적으로 틀리는 최악의 버그 모양이다. 드라이버가 `L+1 == n` 을 확인하는 이유.
3. **CPU 폴링인데 EOCS=0** — 마지막 채널만, OVR 감시도 없음 (Part B 표). DMA 를 쓸 때만 EOCS=0 이 자연스럽다.
4. **ADON 직후 SWSTART** — F4 에는 ADRDY 가 없어서 tSTAB 를 소프트웨어로 기다려야 한다. 한 번에 쓰면 무시되고, 상한 없는 `while (!(SR & EOC));` 이면 **영원히 멈춘다**. 고친 코드: `adc_enable()` (켜져 있으면 바로 리턴, 아니면 켜고 지연).
5. **`raw * 3300 / 4095` (uint32_t)** — VDDA 가정 + 곱셈 범위. 보정식을 32비트로 하면 raw ≥ 867 에서 넘친다 (4096 중 3661 오답). 고친 코드: `uint64_t num = (uint64_t)3300 * cal * raw; den = (uint64_t)data << 12; (num + den/2) / den`.
6. **VREFINT 를 짧은 SMP 로** — VREFINT 는 내부 고임피던스 소스라 긴 샘플링이 필요하다 (데이터시트 최소 샘플링 시간 ~10 µs, 확인 권장). 짧으면 직전 채널 쪽으로 끌린 DATA → VDDA 가 틀리고 → **모든 채널의 mV 가 같은 비율로 틀린다**. 고친 코드: VREFINT 는 SMP 7 (480 cycle @21 MHz = 22.9 µs). TSVREFE 도 켜야 한다.
7. **오버샘플링 `sum >> n` (반올림 없음)** — 결과가 평균 ½ LSB(12+n) 낮다. `(sum + (1u << (n-1))) >> n`.
8. **OVR 을 안 지우고 재시작** — 이 모델(과 RM0090 절차)에서는 재트리거가 먹지 않는다. SR 은 rc_w0 이니 `REG_WRITE(SR, ~OVR)` 로 OVR 만 지울 수 있다 (`SR &= ~OVR` 은 RMW 사이에 HW 가 세운 다른 플래그를 지울 수 있다 — BM-B04).

## 8. 면접 Q&A

<details>
<summary>Q1. 샘플링 시간은 왜 필요하고, 어떻게 고르나요?</summary>

SAR ADC 는 변환 전에 내부 샘플 커패시터(F4 대표값 4 pF)를 입력 전압까지 충전한다. 경로 저항은 소스 임피던스 + 내부 스위치 저항(대표값 6 kΩ)이라 τ = (R_src + R_ADC)·C_ADC, 12비트 ¼ LSB 까지 가려면 ln(2^14) ≈ 9.7 τ 가 필요하다. 데이터시트식 `R_AIN,max = (k − 0.5)/(f_ADC·C_ADC·ln 2^(N+2)) − R_ADC` 를 만족하는 가장 짧은 k 를 고른다. 21 MHz 에서 50 kΩ 이면 56 cycle, 200 kΩ 이면 480. 꼬리질문 "그래도 안 되면?" → 버퍼 op-amp, 핀에 외부 커패시터(연습 4, 대신 샘플률 제한), 분압 저항을 낮추기(대신 대기 전류 증가), ADCCLK 낮추기.

</details>

<details>
<summary>Q2. 스캔할 때 한 채널 값이 옆 채널 값에 끌려갑니다. 원인과 확인 방법은?</summary>

샘플 커패시터에 이전 변환의 전하가 남아 있고, 샘플링 시간이 소스 임피던스에 비해 짧아 다 못 따라간 것. 지문: **채널 순서를 바꾸거나 사이에 GND/VREFINT 채널을 끼우면 값이 바뀐다**, 오차 부호가 이전 채널 쪽. 이 레슨 표: 50 kΩ, 15 cycle 에서 0 V 다음 3 V 를 읽으면 −154 LSB, 3 V 다음 0.3 V 는 +139 LSB. 대책은 Q1 과 같고, 마지막 수단은 같은 채널을 두 번 연속 변환해 첫 값을 버리는 것 (SQR 에 같은 채널을 두 번 — 허용된다, SMP 는 같아야).

</details>

<details>
<summary>Q3. EOC, EOCS, OVR 의 관계를 설명해 보세요. 폴링으로 스캔하면 샘플당 얼마나 일할 수 있나요?</summary>

EOCS=1 이면 변환마다 EOC 가 서고 OVR 감시가 켜진다 (DMA=1 일 때도). EOC 는 DR 을 읽으면 지워진다. 새 결과가 나올 때 EOC 가 아직 1 이면 OVR — 이전 결과를 못 가져간 것. 이 모델에서 버스 접근 1회 = 1 ADCCLK 이라 결과마다 SR 1 + DR 1 = 2 사이클, 정상 상태 예산은 샘플당 `P − 2` (P = SMP + 12), DR 한 칸이 일회성으로 `P − 2` 를 더 버텨 준다: `w_max = (P−2) + ⌊(P−2)/(n−2)⌋` — n = 3..16 × SMP 3종 42 조합에서 모델과 일치. 실제 칩에서는 APB 접근 비용·플래시 대기·ISR 진입이 섞이니 숫자는 다르지만 구조는 같다: "읽는 쪽 평균 속도 ≥ ADC 속도, 버퍼 깊이 = 1".

</details>

<details>
<summary>Q4. 배터리 전압이 10% 높게 읽힙니다. 무엇부터 보나요?</summary>

VDDA 가정. raw 는 VDDA 의 분수라서 VDDA 가 3.0 V 인데 3.3 V 로 계산하면 +10 %. VREFINT 와 공장 보정값으로 `VDDA = 3300·CAL/DATA`. 이 레슨: CAL 1502, DATA 1652 → 3000 mV, VBAT 3700 mV (3.3 V 가정은 4070 mV). 그다음 확인: 분압 저항 공차(1 % 두 개면 비율 최대 ~1 %), 분압의 Thevenin 저항 vs SMP (Q1), VREFINT 채널 활성화(TSVREFE), VREFINT 샘플링 시간.

</details>

<details>
<summary>Q5. `raw * 3300 / 4095` 에서 잘못될 수 있는 것을 다 말해 보세요.</summary>

(1) 3300 은 가정 — VDDA 는 보드·부하·배터리에 따라 다르다 (Q4). (2) 보정식을 붙이면 `3300·CAL·raw` 가 5.5·10^10 까지 → 32비트 오버플로 (4096 중 3661 오답). (3) 정수 나눗셈은 버림 — 평균 −½ mV 편향, 반올림은 `(num + den/2)/den`. (4) 두 단계(VDDA 를 먼저 정수 mV 로) 는 반올림이 두 번 → 4096 중 738 개 ±1 mV. (5) 4095 vs 4096 은 전달 함수 정의의 문제 — 최대 1 LSB. 팀 정의와 맞추고 문서화.

</details>

<details>
<summary>Q6. 오버샘플링으로 12비트 ADC 에서 16비트를 얻을 수 있나요?</summary>

조건부로. 4^n 개를 더해 n 비트 오른쪽 시프트 → +n 비트 **분해능**, 단 (a) 입력에 ~0.3–1 LSB 정도의 노이즈(디더)가 있어 양자화 오차가 샘플마다 달라야 하고, (b) 그동안 신호가 변하지 않아야 한다. 노이즈 0 이면 평균해도 같은 코드 (데모: 1.0004 V 를 16번 → 전부 1242, RMS 0.286 LSB 그대로). 16비트(n=4)는 256 변환 = 21 MHz/15 cycle SMP 기준 329 µs → 초당 3038 개. 그리고 **정확도**(오프셋·이득·INL)는 안 좋아진다 — 분해능과 정확도는 다르다.

</details>

<details>
<summary>Q7. 왜 4^n 이고 2^n 이 아닌가요?</summary>

독립 노이즈의 합은 표준편차가 √N 배, 신호는 N 배라 SNR 이 √N 배. 1 비트 = SNR 2 배 = N 4 배. 데모 표의 σ=1.0 행이 그대로 보여 준다: 1.067 → 0.564 → 0.267 → 0.138 → 0.068, n 이 1 늘 때마다 거의 절반.

</details>

<details>
<summary>Q8. F1, F4, L4/G4 ADC 를 다 써 봤다면 드라이버에서 무엇이 달라지나요?</summary>

(확인 권장, 시리즈별 RM 기준) F1: ADON 을 두 번 쓰면 변환 시작, 자체 보정 `CAL` 비트, ADCCLK 최대 14 MHz. F4: `SWSTART`, 자체 보정 없음(공장 VREFINT_CAL·TS_CAL 만), ADRDY 없음 → tSTAB 지연, OVR 은 EOCS/DMA 때. L4/G4: `ADCAL` 자체 보정(ADC 꺼진 상태에서), `ADRDY` 플래그, 딥파워다운·전압 레귤레이터 활성화 절차, `OVRMOD`(덮어쓸지 보존할지), 하드웨어 오버샘플러, 플래그가 W1C (ISR 레지스터). 그래서 드라이버는 "시작 · 준비 대기 · 플래그 클리어 · 보정" 을 칩별 함수 뒤에 숨긴다.

</details>

<details>
<summary>Q9. 인터럽트로 스캔하는데 가끔 OVR 이 납니다. 디버깅 순서는?</summary>

(1) OVR 카운터와 그 시점의 다른 ISR 활동을 로그 (GPIO 토글 + 로직 분석기로 ISR 구간 측정). (2) 예산: 한 랭크 주기(예: 27 cycle @21 MHz = 1.3 µs) 안에 ADC ISR 이 DR 을 읽어야 한다 → 더 높은 우선순위 ISR 의 최악 실행 시간 + 진입 지연이 이보다 길면 OVR. 이 레슨에서 60 cycle 마스크를 주입하면 정확히 OVR 1회, ISR 이 재시작해 결과는 정상. (3) 해결: ADC IRQ 우선순위 올리기(BM-M02), SMP 를 늘려 주기를 늘리기, 근본적으로는 DMA (BM-M08/M09) — CPU 는 반 버퍼마다 한 번만 깨어난다.

</details>

## 9. 추가 연습

### 연습 1 — 분해능(RES)별 변환 시간과 샘플률

F4 의 RES 필드(CR1 [25:24])는 12/10/8/6 비트 — SAR 단계가 줄어 T_conv = SMP + 12/10/8/6 cycle (확인 권장). `conv_cycles_res`, `max_sps`, 스캔 한 바퀴 시간 `seq_time_ns`, 4^n 오버샘플 결과율 `os_results_per_s` 를 쓰고, "F4 ADC 2.4 MSPS" 가 어디서 나오는지 계산으로 보여라.

<details>
<summary>답</summary>

36 MHz / (3 + 12) = 2.4 MSPS (12비트), 6비트면 36 / 9 = 4 MSPS. 21 MHz 에서는 1.4 MSPS. Part C 보드 스캔 614 cycle @21 MHz = 29238 ns. SMP 15 cycle 로 n=4 오버샘플하면 21e6 / (27·256) = 3038 결과/s — 데모 표와 같은 값. 정수 나눗셈 순서: `f / (cyc << 2n)`.

</details>

### 연습 2 — 온도 센서 2점 보정 (0.01 °C, 정수)

F4 의 TS_CAL1 (30 °C), TS_CAL2 (110 °C) 는 VDDA = 3.3 V 에서 측정한 raw 다 (주소 0x1FFF7A2C / 0x1FFF7A2E — 확인 권장). VDDA 가 다른 보드에서 `T = 30 + 80·(raw·VDDA/3300 − CAL1)/(CAL2 − CAL1)` 를 0.01 °C 정수로 계산하라. 30 °C 아래(음수 분자)에서 반올림을 조심.

<details>
<summary>답</summary>

`num = 8000·(raw·VDDA − CAL1·3300)`, `den = (CAL2−CAL1)·3300`, 결과 `3000 + round(num/den)`. C 의 정수 나눗셈은 0 쪽으로 자르므로 `(num + den/2)/den` 은 음수 num 에서 틀린다 → 부호별 대칭 반올림. 전수 비교(raw 0..4095 × VDDA 2800/3000/3300 = 12288): 내 구현 0 오답, 단순 구현 3093 오답. 보정점에서는 정확히 3000, 11000. VDDA 환산을 빼먹으면 3.0 V 보드에서 수십 °C 가 틀린다 — 내부 온도 센서는 원래 정확도가 낮아(±수 °C, 확인 권장) "상대 변화" 용이다.

</details>

### 연습 3 — 아날로그 워치독 임계값

AWD 는 `code > HTR` 또는 `code < LTR` 이면 플래그를 세운다. 배터리 1/2 분압이 1500..1800 mV 를 벗어나면 깨우고 싶다. 코드 c 의 전압을 `c·VDDA/4096` 로 보고, `(c > HTR) ⇔ c·VDDA > hi·4096`, `(c < LTR) ⇔ c·VDDA < lo·4096` 이 **모든 코드에서** 성립하도록 `awd_thresholds()` 를 쓰고 brute force 로 증명하라.

<details>
<summary>답</summary>

`HTR = floor(hi·4096/VDDA)`, `LTR = ceil(lo·4096/VDDA)`, 4095 로 포화. 1500..1800 mV @3300 → LTR 1862, HTR 2234. 전수 3,981,312 (mV, 코드) 쌍에서 0 불일치. 반올림(`round`)을 쓰면 경계 코드 하나가 반대쪽으로 간다. VDDA 가 바뀌면 임계값도 다시 계산해야 한다 — VREFINT 측정 후 갱신.

</details>

### 연습 4 — 외부 커패시터로 고임피던스 소스 받치기

2 MΩ/2 MΩ 분압(3.7 V 에서 대기 전류 0.93 µA, Thevenin 1 MΩ)은 480 cycle 로도 안 된다 (21 MHz 에서 R_AIN,max 582 kΩ). 핀에 C_ext 를 달면 샘플링 순간 C_ADC 와 전하를 나눠 갖는다: ΔV = step·C_ADC/(C_ADC + C_ext). 풀스케일 계단에서 ¼ LSB 이하가 되는 최소 C_ext 를 정수 pF 로 구하고, 그 대가를 계산하라.

<details>
<summary>답</summary>

`C_ext ≥ C_ADC·(step·4096·4/VDDA − 1)` → 4 pF·16383 = **65532 pF** (≈ 66 nF, 실무에선 100 nF). 이웃 채널과 차이가 100 mV 뿐이면 1982 pF. 대가: C_ext 가 R_src 로 다시 차는 시간 ≈ R·C·ln 2^14 → 50 kΩ + 100 nF 이면 약 20.6 Hz 보다 자주 읽으면 처진다. 배터리 · 온도처럼 느린 신호엔 정답, 빠른 신호엔 버퍼 앰프.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
```

## 10. 핵심 정리

- SQR: 랭크 → 채널 (5비트, SQ1 은 SQR3 [4:0], L = n−1). SMPR: **채널** → SMP (3비트, ch<10 은 SMPR2). 검증 먼저, 쓰기는 마지막.
- T_conv = SMP + 12 ADCCLK. 샘플 커패시터는 이전 채널 전압에서 출발해 τ = (R_src + R_ADC)·C_ADC 로 충전 → 짧은 SMP + 큰 R 이면 이전 채널 쪽으로 끌린다. `R_AIN,max = (k−0.5)/(f·C·ln 2^14) − R_ADC`.
- CPU 폴링 스캔은 EOCS=1. 새 결과 때 EOC 가 남아 있으면 OVR. 폴링 예산 `w ≤ P − 2` (+ DR 한 칸 여유). OVR 은 지우고 재트리거.
- ADON 후 tSTAB (F4 는 ADRDY 없음). 폴링 루프엔 항상 상한.
- VDDA = 3300·CAL/DATA. mV = 3300·CAL·raw/(DATA·4096) — 64비트, 반올림 한 번. 3.3 V 가정은 3.0 V 보드에서 +10 %.
- 4^n 평균 → +n 비트, **노이즈(디더)가 있을 때만**. 결과 = (sum + 2^(n−1)) >> n. 시간은 4^n 배.
- 연속 샘플링·짧은 ISR 예산이면 DMA → BM-M08 (DMA 드라이버), BM-M09 (ADC + DMA 더블 버퍼링).

## 참고자료

- STMicroelectronics, **RM0090** Reference manual STM32F405/415, 407/417, 427/437, 429/439 — ADC 장(ADC_SR/CR1/CR2/SMPRx/SQRx/DR, EOCS, overrun, 스캔 모드). 주소·비트는 여기서 확인.
- STMicroelectronics, **STM32F407xx 데이터시트** — ADC characteristics (C_ADC, R_ADC, f_ADC, tSTAB), R_AIN 식 (Equation 1, ¼ LSB 기준), 내부 기준전압 VREFINT 와 VREFINT_CAL 주소, 온도 센서 TS_CAL1/2.
- STMicroelectronics, **AN2834** "How to get the best ADC accuracy in STM32 microcontrollers" — 소스 임피던스, 외부 커패시터, 오버샘플링·평균.
- 데이터시트 R_AIN 식 확인에 쓴 검색 결과(공개 데이터시트 미러): https://www.manualslib.com/manual/1573810/St-Stm32.html?page=28 (AN2834 의 소스 저항 계산 페이지)
- Silicon Labs **AN118** "Improving ADC Resolution by Oversampling and Averaging" — 4^n 규칙과 디더 조건.
- 이 트랙: BM-B02 (mock 프레임워크), BM-B04 (rc_w0 · RMW 경쟁), BM-B10 (PCLK2 → ADCCLK 클럭 트리), BM-B14 (타이머 — TRGO 로 ADC 트리거는 다음 단계), BM-M02 (IRQ 우선순위), BM-M08 / BM-M09 (DMA, 더블 버퍼링), C-M14 (ISR ↔ main 공유 데이터), ARCH-M12 (DMA 캐시 일관성).
