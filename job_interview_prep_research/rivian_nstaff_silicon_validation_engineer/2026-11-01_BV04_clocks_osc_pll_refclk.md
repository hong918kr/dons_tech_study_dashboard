# BV04 · 클럭: crystal/oscillator · PLL · refclk · jitter
<!-- theme: pastel -->

> **묶음**: B 전원·클럭·리셋 · **우선**: P1 · **선행 노트**: [BV03 전원 트리](2026-11-01_BV03_power_tree_sequencing_pmic.html) · **다음 노트**: [BV05 리셋·strap·boot mode](2026-11-01_BV05_reset_strap_boot_mode.html) · 관련: [LP03 링크 트레이닝 (REFCLK·PERST#)](2026-10-31_LP03_link_training_ltssm.html) · [LP00A primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · **코드**: [bv04/pll_calc.c](onsitePrep/bv04/pll_calc.c) · [bv04/uart_baud.c](onsitePrep/bv04/uart_baud.c) · [PLL 출력](onsitePrep/bv04/pll_calc_output.txt) · [UART 출력](onsitePrep/bv04/uart_baud_output.txt)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"새 보드에서 UART가 깨진 글자를 찍는다" · "crystal 핀에 프로브 대면 왜 멈추나" · "PLL이 lock 안 된다, 다음은?" · "PCIe 100 MHz refclk·SSC·common clock vs SRIS" · "블록 레지스터가 0만 읽힌다/버스가 멈춘다"(clock gate·reset)**.
- **외울 것 1 — 클럭 경로**: crystal(수동, 칩 안 발진 회로 + load cap 필요) 또는 oscillator(능동, 전원 넣으면 바로 출력) → SoC **PLL**(ref/M → PFD → VCO = PFD×N → /P) → mux → divider → **clock gate** → 블록. PCIe·SerDes는 보드 **clock generator/buffer**에서 **100 MHz HCSL 차동 refclk**를 따로 받는다.
- **외울 것 2 — PLL bring-up 순서**: **bypass**(osc 그대로 사용)로 먼저 살리기 → PLL power-down/reset 상태에서 M/N/P 설정 → reset 해제 → **lock 비트를 timeout과 함께 polling** → (DVFS면 전압 먼저 올리고) **glitch-free mux로 PLL 출력 전환**. 제약: **PFD 입력 범위, VCO 범위, 분주기 범위** — §6 계산기가 이걸 검사.
- **외울 것 3 — 측정·증상**: crystal 핀을 10× 프로브(10–15 pF)로 찍으면 부하 변화로 발진이 멈추거나 주파수가 바뀐다 → **buffered clock-out(debug clock out) 핀**으로 PLL/n을 내보내 잰다. **UART 글자 깨짐 = baud 오차 = 클럭 오차**(16× oversampling RX는 양쪽 합쳐 약 ±3–5% 예산, 한쪽 약 ±2.5%). 블록 쓰려면 **clock enable + reset deassert 둘 다**.
- PCIe refclk: **SSC는 30–33 kHz로 0 ~ −0.5% down-spread**(EMI 감소), **SRIS면 최대 −0.3%**, 기본 주파수 허용치는 Gen1–4 ±300 ppm, Gen5–6 ±100 ppm [확인됨][4][5].
- 코드: **C PLL 계산기**(ref·target·VCO/PFD/분주기 범위 → 최적 M/N/P + ppm 오차, integer-N vs fractional-N, 불가능한 경우 판정) + **UART baud 오차 계산기**(16550 정수 분주 vs PL011 분수 분주, "FW가 가정한 클럭 ≠ 실제 클럭" 시나리오, scope bit 폭 → baud) — ASan/UBSan 빌드 실제 출력 §6.
- **30초 영어 답 ("UART prints garbage on a new board")**: "Garbage that's consistent, not random, usually means a baud-rate mismatch, and on a new board that's almost always a clock problem: the UART's input clock isn't what the firmware assumes. I'd put a scope on TX and measure one bit width — at 115200 it should be 8.68 microseconds. If it's off by a ratio, like 20% or exactly 2×, I know the clock is wrong rather than the UART config. Then I go up the clock tree: is the crystal running and the right part, did the PLL lock or is the chip still on the bypass clock, is the UART's clock mux and divider what the firmware expects. A debug clock-out pin lets me measure the PLL output directly without loading the crystal."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "오케스트라의 메트로놈과 기어 박스"

- 칩 안의 모든 디지털 회로는 **박자(클럭)** 에 맞춰 한 걸음씩 움직인다. 박자가 없으면 아무도 안 움직이고(=칩이 죽은 것처럼 보임), 박자가 틀리면 다 같이 틀린 속도로 움직이고(=UART baud·타이머 틀림), 박자가 흔들리면(=**jitter**) 빠른 대화(PCIe·DDR)에서 상대방이 말을 놓친다.
- **Crystal(수정 진동자)** = **소리굽쇠**. 아주 정확한 음(예: 24 MHz)을 내지만 **스스로는 울리지 않는다** — 칩 안의 증폭 회로가 계속 툭툭 쳐 줘야 울린다(그래서 칩의 osc 회로 + 양옆 **load capacitor**가 필요). 소리굽쇠 옆에 손가락을 대면(=프로브) 음이 바뀌거나 멈춘다.
- **Oscillator(발진기 모듈, XO)** = **건전지 든 전자 메트로놈**. 안에 crystal + 증폭기가 다 있어서 **전원만 주면 바로 깨끗한 사각파**를 낸다. 대신 전원이 필요하고 비싸다.
- **Clock generator / buffer** = **방송실**. 메트로놈 하나의 박자를 여러 방(PCIe 슬롯·SoC·카메라 serializer)에 같은 박자로 나눠 보내고, 필요하면 다른 박자로 바꿔 만든다.
- **PLL(Phase-Locked Loop)** = **자전거 기어 박스**. 페달(24 MHz)을 한 번 밟을 때 바퀴(1.2 GHz)가 50바퀴 돌게 만드는 장치. 단, 기어가 맞물리는 데(=**lock**) 시간이 걸리고, 기어 범위 밖(=**VCO 범위** 밖)으로는 못 만든다.
- **Clock gate** = **방마다 있는 스피커 스위치**. 안 쓰는 방은 박자를 꺼서 전력을 아낀다. 이 스위치를 안 켜고 그 방(블록)에 말을 걸면 대답이 없다(레지스터가 0이거나 버스가 멈춤).

### 1.2 보드 → 칩 안까지 클럭 경로

```text
  보드                                    SoC 내부
 ┌──────────────┐  XTAL_IN/OUT  ┌───────────────────────────────────────────────────────────────┐
 │ 24 MHz crystal├──────────────▶│ OSC pad ─▶ [PLL_CPU] ─▶ mux ─▶ /div ─▶ gate ─▶ CPU/NPU         │
 │ + C1, C2      │               │   │       [PLL_PERI] ─▶ mux ─▶ /div ─▶ gate ─▶ UART, SPI, I2C  │
 └──────────────┘               │   │       [PLL_DDR]  ─▶ ─────────────▶ gate ─▶ DDR PHY        │
                                 │   └──(bypass)──────────▲  (PLL 없이 osc 그대로)                │
 ┌──────────────┐  100 MHz HCSL  │                                                               │
 │ clock gen /  ├─────(차동)────▶│ PCIe REFCLK ─▶ SerDes PLL (PHY 안) ─▶ 8/16/32 GT/s 비트 클럭   │
 │ buffer (SSC) ├──▶ PCIe 장치·NVMe·스위치 (같은 refclk = common clock)                          │
 └──────────────┘                │ CLK_OUT 핀 ◀─ mux(PLL/n, osc, …) ← 디버그로 내보내 측정       │
 ┌──────────────┐                └───────────────────────────────────────────────────────────────┘
 │ 32.768 kHz   ├─▶ RTC / always-on (느리지만 저전력)
 └──────────────┘
```

### 1.3 PLL 안을 들여다보면

```text
  f_ref ─▶ [÷M] ─▶ f_pfd ─▶ [PFD: 위상 비교] ─▶ [charge pump + loop filter] ─▶ [VCO] ─┬─▶ [÷P] ─▶ f_out
                               ▲                                                     │
                               └──────────────── [÷N] ◀─────────────────────────────┘
   lock 조건: f_vco / N == f_pfd  →  f_vco = f_ref × N / M,   f_out = f_vco / P
   lock detect: PFD 위상 오차가 일정 시간 작게 유지되면 LOCK 비트 = 1

  예) f_ref 24 MHz, M=1, N=125 → VCO 3000 MHz, P=3 → 1000 MHz
```

- **왜 VCO를 높게 돌리고 P로 나누나**: VCO는 정해진 범위(예: 1.5–3 GHz)에서만 안정적으로 돈다. 원하는 출력이 낮으면 VCO는 범위 안에서 돌리고 뒤에서 나눈다.
- **왜 PFD(위상 비교 주파수) 범위가 있나**: 너무 낮으면 루프가 느리고 노이즈가 N배로 커지고, 너무 높으면 비교기가 못 따라간다 → 데이터시트에 범위(예: STM32F4는 VCO 입력 1–2 MHz, jitter 줄이려면 2 MHz 권장) [확인됨][13].

### 1.4 Jitter를 그림으로

```text
 이상적인 클럭:  |‾‾|__|‾‾|__|‾‾|__|‾‾|__|      edge가 정확히 T 간격
 jitter 있는 클럭: |‾‾|_|‾‾‾|__|‾|__|‾‾|___|     edge가 앞뒤로 조금씩 흔들림
                   ←T+δ1→←T−δ2→ ...
  period jitter       = 한 주기 길이의 흔들림
  cycle-to-cycle      = 이웃한 두 주기 길이 차이
  TIE (time interval error) = 이상적 위치 대비 edge 누적 위치 오차
  SSC                 = 일부러 주파수를 천천히(30–33 kHz) 0 ~ −0.5% 흔들어 EMI 피크를 퍼뜨림 (jitter와 다름: 느려서 PLL이 따라감)
```

### 1.5 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| crystal (XTAL) | quartz crystal resonator | 정확한 주파수로 진동하는 수정 조각, 혼자는 못 울림 | 칩 osc 회로 + load cap 필요, 프로브에 민감 |
| oscillator (XO) | crystal oscillator module | crystal + 증폭기 내장, 전원 넣으면 사각파 출력 | 전원·enable 핀 확인이 먼저 |
| TCXO | Temperature-Compensated XO | 온도 보상으로 더 정확(ppm 작음) | GNSS·통신·센서 동기 |
| MEMS oscillator | — | 실리콘 MEMS 공진기 발진기 | 진동·충격 강함(자동차) [추정] |
| clock generator | — | 하나의 입력으로 여러 주파수·여러 출력 생성(내부 PLL) | PCIe refclk, SSC 제공 |
| clock buffer / fan-out | — | 한 클럭을 여러 장치로 복제(주파수 그대로) | common clock 분배 |
| load capacitance (CL) | — | crystal이 "보고 싶어 하는" 부하 용량(데이터시트 값, pF) | 틀리면 주파수가 ppm 단위로 빗나감 |
| ESR | Equivalent Series Resistance | crystal의 손실 저항 | 크면 발진 시작 못함(gain margin) |
| gain margin / negative resistance | — | 발진 회로가 crystal 손실을 이길 여유 | 부족하면 "가끔 안 켜짐"(특히 저온) |
| ppm | parts per million | 100만분의 1. 24 MHz ±50 ppm = ±1.2 kHz | 주파수 정확도 단위 |
| PLL | Phase-Locked Loop | 낮은 기준 클럭을 높은 클럭으로 곱하는 피드백 회로 | 거의 모든 칩 클럭의 출처 |
| PFD | Phase-Frequency Detector | 기준과 피드백의 위상/주파수 차이를 비교 | f_pfd = f_ref/M, 허용 범위 있음 |
| VCO | Voltage-Controlled Oscillator | 전압으로 주파수가 변하는 발진기(PLL 심장) | VCO 범위 밖이면 lock 불가 |
| M / N / P | pre-divider / feedback divider / post-divider | 기준 분주 / 곱하기 / 출력 분주 | 설정 계산 = §6 |
| lock | PLL lock | 피드백이 기준과 맞물려 안정된 상태 | LOCK 비트 확인 전 사용 금지 |
| bypass | PLL bypass | PLL을 건너뛰고 기준 클럭을 그대로 출력 | bring-up 첫 단계, 원인 분리 |
| integer-N / fractional-N | — | N이 정수만 / 소수도 가능(ΣΔ 변조) | 원하는 주파수 정확도 vs spur |
| glitch-free mux | — | 클럭 소스를 바꿀 때 짧은 펄스(glitch) 없이 전환 | 전환 중 CPU 크래시 방지 |
| clock gating | — | 블록 클럭을 끄는 스위치 | 블록 사용 전 enable 필수 |
| CCF | Common Clock Framework | Linux의 클럭 트리 관리 프레임워크 | `clk_summary`로 트리·rate 확인 |
| REFCLK | reference clock | PCIe 등 SerDes의 기준 클럭(100 MHz) | 없으면 링크 Detect도 못 감 (LP03) |
| HCSL | High-speed Current Steering Logic | PCIe refclk의 차동 신호 규격 | 차동 프로브로 측정 |
| SSC | Spread Spectrum Clocking | 주파수를 일부러 천천히 흔들어 EMI 분산 | 양쪽이 같은 refclk인지가 중요 |
| common clock (CC) | — | 링크 양끝이 같은 refclk 사용 | 가장 단순, 케이블·보드 경계에선 어려움 |
| SRNS / SRIS | Separate Refclk No SSC / Separate Refclk Independent SSC | 양끝이 각자 refclk(SSC 없음 / 각자 SSC) | SRIS는 PHY·elastic buffer 지원 필요 |
| jitter | — | edge 시점의 흔들림(시간 영역) | eye를 닫음 → BER |
| phase noise | — | 주파수 영역에서 본 jitter (dBc/Hz) | 적분하면 RMS jitter |
| MCO / CLK_OUT | Microcontroller Clock Output / clock out | 내부 클럭을 핀으로 내보내는 디버그 기능 | crystal을 안 건드리고 측정 |
| baud | — | UART 초당 심볼(비트) 수 | 클럭 오차가 그대로 baud 오차 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Crystal vs oscillator vs clock generator

| 항목 | Crystal | Oscillator (XO/TCXO/MEMS) | Clock generator / buffer |
|---|---|---|---|
| 능동/수동 | 수동 (칩 osc 회로가 구동) | 능동 (전원 필요) | 능동, 보통 I2C/strap 설정 |
| 출력 | 칩 XTAL 핀 사이 사인파 | CMOS 사각파 (또는 차동) | 여러 출력, HCSL/LVDS/CMOS, SSC 옵션 |
| 정확도 | 초기 tolerance + 온도 stability + aging (각각 ±10–50 ppm 흔함) | 비슷 또는 TCXO는 훨씬 작음 | 입력 기준에 좌우 + 자체 jitter |
| 보드 설계 포인트 | CL·C1/C2, ESR, drive level, 배선 짧게 | 전원 디커플링, enable 핀 | 출력 termination, 배선, 출력 enable·SSC 설정 |
| 실패 모드 | 안 켜짐(gain margin), 주파수 빗나감(CL), 저온 시작 실패 | 전원·enable 없음 | 출력 disable 상태, 설정 OTP/strap, termination 누락 |
| bring-up 확인 | clock-out 핀, 저용량 프로브로 XOUT | 출력 핀 직접 측정 가능 | 각 출력 차동 프로브, I2C 레지스터 |

- **Load capacitance 공식**: crystal이 보는 부하 CL = (C1 × C2)/(C1 + C2) + C_stray. STM32 데이터시트 예: C_stray 2–7 pF, CL 6 pF crystal + stray 2 pF → C1 = C2 = 8 pF [확인됨][2]. 흔한 실수: **CL 값을 C1/C2에 그대로 넣는 것**(예: CL 12 pF crystal에 12 pF ×2 → 실제 6 pF + stray → 주파수가 위로 빗나감).
- **틀린 CL의 증상**: 발진은 되지만 주파수가 수십 ppm 빗나감 → UART는 괜찮은데(UART 예산은 % 단위) **Ethernet(±100 ppm 요구 [추정 — IEEE 802.3 원문 미확인])·USB·RTC 시간·센서 timestamp 동기**가 틀어짐. 자동차 카메라 다중 센서 동기·timestamp에 직접 영향 [추정].
- **Gain margin**: 발진 회로의 증폭이 crystal 손실(ESR)을 충분히 이겨야 확실히 켜진다. 부족하면 "대부분 켜지는데 가끔·저온에서 안 켜짐" — PVT에서 잡아야 하는 종류의 버그. ST AN2867이 gain margin 계산 방법을 설명 [확인됨][1].

### 2.2 PLL — 공식, 제약, lock, bring-up 순서

- **공식**: f_pfd = f_ref / M, f_vco = f_pfd × N, f_out = f_vco / P. 제약 3종: **f_pfd ∈ [PFD_min, PFD_max]**, **f_vco ∈ [VCO_min, VCO_max]**, **M/N/P ∈ 허용 범위**(P는 짝수만 같은 제한이 흔함).
- **공개 예시 1 — STM32F4 메인 PLL**: VCO 입력(f_ref/PLLM)은 1–2 MHz, jitter를 줄이려면 2 MHz 권장 [확인됨][13]. PLLP는 2/4/6/8. VCO 출력 범위 100–432 MHz는 RM0090 기준 값으로 알려져 있으나 이번에 원문 확인은 못 함 [추정][13]. → 8 MHz HSE → 168 MHz: M=4, N=168, P=2 (§6 실행 결과 #1).
- **공개 예시 2 — Zynq UltraScale+ PS PLL**: VCO 1500–3000 MHz (DS925) [확인됨][11], Linux `zynqmp` PLL 드라이버의 FBDIV 25–125, VCO 1.5–3.0 GHz, 16비트 fractional 지원 [확인됨][10]. VPLL_CTRL의 FBDIV는 bits[14:8], DIV2(bit 16)는 VCO가 아니라 출력만 반으로 [확인됨 — 검색 요약 기준][12]. → 33.333 MHz PS_REF_CLK에서 1200 MHz: FBDIV=72, DIV2 → **1199.988 MHz(−10 ppm)** — 정확한 1.2 GHz는 정수 FBDIV로 안 나온다(§6 #2). 이런 "몇 ppm 빗나감"은 CPU엔 무의미하지만 **타이머·timestamp 계산에 공칭값(1.2 GHz)을 하드코딩하면 누적 오차**가 된다.
- **Integer-N vs fractional-N**: 25 MHz 기준에서 148.5 MHz(1080p60 pixel clock)는 integer-N으로 최선이 −421 ppm, 16비트 fractional-N이면 +0.04 ppm (§6 #4·#5). 대가: fractional-N은 ΣΔ 변조로 **spur(특정 주파수의 잡음 봉우리)** 가 생길 수 있음 → 고속 SerDes PLL은 정수 모드 선호 [추정].
- **Lock**: lock detect가 "PFD 위상 오차가 일정 시간 작음"을 보고 LOCK=1. lock 시간은 루프 대역폭에 반비례(수 µs–수백 µs가 흔함 [추정]). **lock 비트가 1이 될 때까지 출력 사용 금지**, 그리고 **timeout 필수**(bring-up FW가 lock 대기에서 무한 루프 → "칩이 그냥 멈춘 것처럼" 보이는 대표 원인).
- **Bring-up 순서 (일반형)**:

```text
 1. PLL을 bypass (출력 = f_ref)  ← 리셋 직후 기본값인 칩이 많음
 2. PLL power-down / reset assert
 3. M, N, P (+ frac, loop filter 설정) 쓰기 — 계산기로 PFD·VCO 범위 검증한 값
 4. PLL reset deassert / power-up
 5. LOCK 비트 polling (timeout: 예 1 ms) → 실패하면 bypass 유지 + 에러 코드 + 상태 덤프
 6. (주파수 올리는 경우) 해당 rail 전압 먼저 올리기 (BV03 DVFS 순서)
 7. 버스 분주기를 먼저 안전한 값으로 → glitch-free mux로 PLL 출력 선택
 8. 하위 분주기·게이트 설정 → clock-out 핀으로 측정 확인
```

- **Bypass가 bring-up에 중요한 이유**: PLL 없이도 CPU·UART가 osc 클럭(예: 24 MHz)으로 돈다 → "PLL 문제 vs 그 외 문제"를 가른다. **PLL이 lock 안 되면 많은 칩이 bypass로 남아 '느리게 동작'** → "코어는 도는데 너무 느리다" 증상 [추정 — 칩/FW 설계에 따라 다름].

### 2.3 클럭 트리 · 게이팅 · enable + reset

- 블록 하나를 쓰려면 보통 4가지가 맞아야 한다: ① 소스 PLL lock ② mux 선택·분주기 ③ **clock gate enable** ④ **reset deassert**(+ 전원 도메인 on, BV03). 하나라도 빠지면 레지스터가 0/0xFFFFFFFF로 읽히거나, **버스 응답이 없어 CPU가 멈추거나**(bus hang), bus error/abort가 난다 [추정 — 인터커넥트 설계에 따라 다름].
- **순서 함정**: 일부 블록은 **리셋이 클럭이 도는 동안 assert되어야** 내부 동기 플립플롭이 제대로 초기화된다 → "clock enable → (몇 사이클) → reset deassert" 순서. 반대로 하면 첫 부팅은 되는데 warm reset 후 이상한 등 재현이 까다로운 버그.
- **Linux CCF**: `/sys/kernel/debug/clk/clk_summary`에 트리·enable count·rate가 나온다. 커널은 부팅 후반에 **사용되지 않는 클럭을 끈다**("clk: Disabling unused clocks") → bootloader가 켜 둔 블록이 갑자기 죽는 bring-up 단골. 임시 확인용 부트 인자 `clk_ignore_unused` [추정 — 커널 파라미터명은 널리 쓰이나 이번에 문서 미확인].

### 2.4 PCIe 100 MHz refclk · common clock vs SRIS · SSC

- PCIe refclk는 모든 세대에서 **100 MHz HCSL 차동**이 기본 [확인됨][4]. 링크 bit rate(2.5–64 GT/s)는 PHY 안 PLL이 이 100 MHz에서 만든다. refclk가 없거나 PERST# 해제 전 안정되지 않으면 LTSSM이 Detect부터 못 간다 → [LP03](2026-10-31_LP03_link_training_ltssm.html).
- **아키텍처 3가지**:

```text
 Common Clock (CC)                 SRNS                               SRIS
 [clk gen]──┬──▶ Root Port         [clk A]──▶ RP    [clk B]──▶ EP      [clk A + SSC]──▶ RP  [clk B + SSC]──▶ EP
            └──▶ Endpoint           두 클럭 각자, SSC 없음             두 클럭 각자, 각자 SSC (서로 비동기)
 양쪽이 같은 SSC를 함께 흔듦         ppm 차이만 흡수                   ppm + SSC 차이까지 흡수해야 함
 → 차이 거의 0, 가장 쉬움           → elastic buffer + SKP로 흡수      → 더 자주 SKP, PHY가 SRIS 지원해야
```

- **SSC 수치**: 변조 주파수 30–33 kHz, CC는 0 ~ −0.5% down-spread, separate reference는 0 ~ −0.3% (SRIS 최대 −0.3%) [확인됨][4]. 기본 주파수 허용치: Gen1–4 ±300 ppm, Gen5–6 ±100 ppm (congatec AN45) [확인됨][5]. SSC 포함 총 편차는 TI가 −0.5% 경우 대략 −100 ~ +2600 ppm, SRIS −100 ~ +1600 ppm으로 정리 [확인됨][4] (부호 규약은 문서 기준).
- **왜 SSC?** 100 MHz와 그 배수에 몰린 EMI 에너지를 넓은 대역으로 퍼뜨려 피크를 낮춤 — 자동차 EMC(CISPR 25, BV15)에 유리. **대가**: 양끝이 다른 클럭이면 SSC 차이까지 흡수해야 → SRIS 필요. 케이블·보드 사이(자동차 ECU 간 PCIe·외부 장치)에서 refclk를 같이 보내기 어려우면 SRIS 고려 [추정].
- **Jitter 한계는 세대가 오를수록 엄격**: Gen5 CC refclk jitter 한계가 약 0.15 ps RMS라는 2차 자료가 있으나 원문 확인 못 함 [추정][6]. 측정은 PCIe 규격의 필터 함수를 적용한 jitter 분석 SW로 해야 비교 가능.
- **자동차 등급 PCIe clock generator** 존재: 예) Diodes PI6CG334Q — PCIe 6.0/5.0/4.0 clock generator, 4출력, AEC-Q 등급 [확인됨 — 제품 페이지 제목 기준][7].

### 2.5 Jitter · phase noise 기초

- **종류**: random jitter(RJ, 가우시안, 열잡음 — RMS로 표현, 피크는 BER에 따라 커짐) / deterministic jitter(DJ: 전원 노이즈 결합에 따른 periodic jitter, 데이터 패턴 의존 jitter(DDJ), crosstalk). 링크 BER 기준 total jitter TJ ≈ DJ + 2·Q(BER)·RJ_rms, BER 10^-12에서 2·Q ≈ 14 [추정 — 표준 dual-Dirac 근사].
- **Phase noise**: 주파수 영역에서 본 jitter. L(f) [dBc/Hz]를 offset 주파수 대역에 걸쳐 적분하면 RMS phase jitter. PLL은 대역폭 안쪽에서 **기준 클럭 phase noise를 20·log10(N) dB만큼 키운다**(N=50이면 +34 dB) → 좋은 기준 클럭 + 높은 PFD가 유리 [추정 — PLL 일반 이론].
- **전원과의 연결**: PLL/VCO 전원 rail의 노이즈가 periodic jitter로 나타남 → BV03의 "PLL rail은 LDO + 필터" 이유. **buck 스위칭 주파수가 jitter 스펙트럼에 spur로 보이면** 전원 결합의 증거 [추정].
- **어디에 영향**: DDR(타이밍 마진·training 결과 폭, BV09), SerDes(eye 닫힘·BER, BV12), ADC/카메라(SNR), UART·I2C·SPI 같은 저속은 거의 영향 없음(그 쪽은 **주파수 오차**가 문제).

### 2.6 증상 → 클럭 가설 표

| 증상 | 클럭 가설 | 확인 방법 |
|---|---|---|
| UART가 일관된 깨진 글자 | UART 입력 클럭 ≠ FW 가정 (crystal 값, PLL bypass, mux, 분주기) | scope로 bit 폭 → 실제 baud (§6), clock-out 핀 |
| UART가 가끔만 깨짐 | baud 오차가 예산 경계(±2–3%) + 상대편 오차 | 양쪽 오차 계산, 다른 baud로 시험 |
| 타이머/sleep이 일정 비율로 틀림 | 타이머 클럭 소스 오설정, 32 kHz 대신 다른 클럭 | 1 s 토글 GPIO를 scope/카운터로 |
| 코어가 돌지만 너무 느림 | PLL lock 실패로 bypass 상태, 분주기 오설정 | PLL 상태 레지스터, clock-out, 성능 카운터 |
| 칩이 전혀 안 보임 (JTAG도) | osc 없음(crystal 미발진, XO 전원/enable), 리셋이 클럭 대기 | XO 출력/XOUT 저용량 프로브, osc 전원 (→ BV07) |
| 저온에서만 가끔 부팅 안 됨 | crystal 시작 실패(gain margin), 시작 시간 > 리셋 지연 | 냉각 스프레이·chamber, 시작 시간 측정 |
| 특정 블록 레지스터가 0/버스 hang | clock gate off 또는 reset 미해제 | clock/reset 레지스터 덤프, `clk_summary` |
| 부팅 후반에 장치가 죽음 (Linux) | CCF가 unused clock 끔 | dmesg, `clk_ignore_unused`로 비교 |
| PCIe 링크 Detect/Polling 반복 | refclk 없음·불안정, CC/SRIS 불일치 | 차동 프로브, refclk 출력 enable, SSC 설정 |
| Ethernet link는 되는데 에러·flap | 클럭 ppm 초과·jitter | 25/125 MHz 측정, PHY 에러 카운터 |

## 3. 보드에서 볼 것 · 잴 것

### 3.1 클럭 bring-up 체크리스트

| 순서 | 무엇 | 어디서 | 장비·설정 | 통과 기준 |
|---|---|---|---|---|
| 1 | osc 전원·enable | XO VDD, OE 핀 | DMM | 정격 전압, OE active |
| 2 | 기준 클럭 존재·주파수 | XO 출력 / crystal은 XOUT(저용량 프로브) | scope(active probe <1 pF) / frequency counter | 공칭 ± ppm 스펙 |
| 3 | 리셋 해제 전 클럭 안정 | REF + RESET_N 동시 | scope 2ch | 클럭 안정 후 리셋 해제 (BV05) |
| 4 | PLL lock | PLL 상태 레지스터 | JTAG(BV07) / UART 로그 | LOCK=1, timeout 이내 |
| 5 | PLL 출력 주파수 | **CLK_OUT 핀** (PLL/n 선택) | counter 또는 scope frequency 측정 | 계산값 ± 오차 |
| 6 | 블록 클럭 | 블록 동작 + 레지스터 | GPIO 토글 주기, UART bit 폭 | 계산값과 일치 |
| 7 | PCIe refclk | 커넥터/소켓 근처 test point | 차동 프로브, jitter SW | 100 MHz ± ppm, SSC 설정대로, jitter 스펙 |
| 8 | 32.768 kHz RTC | RTC 출력/clock-out | counter (장시간 gate) | ppm, 1일 오차 환산 |

### 3.2 Crystal을 재면 안 되는 이유와 대안

- 일반 10× passive 프로브는 **약 10–15 pF**의 부하 — crystal 회로에서는 load cap을 바꾸는 것과 같아서 **발진이 멈추거나 시작되거나 주파수가 바뀐다** → crystal 핀 직접 측정은 피하고, **버퍼된 출력 핀은 10× 프로브로 재도 괜찮다** [확인됨][3].
- 대안 순서: ① **CLK_OUT/MCO 핀**(칩이 내부 클럭을 버퍼해 내보냄 — 분주해서 측정 가능한 주파수로) ② UART TX bit 폭·GPIO 토글로 간접 측정 ③ 꼭 봐야 하면 **저용량 active 프로브(<1 pF)** 로 **XOUT 쪽**(드라이브 쪽이 덜 민감) [추정 — 포럼·일반 실무].
- "프로브를 대니까 보드가 켜졌다"도 같은 현상의 반대편 — **gain margin 부족 신호**. 이건 HW 팀에 보낼 버그.

### 3.3 Debug clock-out 핀 활용

- 많은 SoC/MCU가 내부 클럭(osc, PLL 출력, 시스템 클럭 등)을 선택·분주해서 핀으로 내보낸다(STM32는 MCO 핀) [추정 — 칩마다 이름·옵션 다름]. bring-up FW의 표준 기능으로 넣어 두면:
  - PLL을 하나씩 선택해 **counter로 주파수 확인** (예: 1.2 GHz PLL을 /100 → 12 MHz 측정).
  - 온도·전압을 바꾸며 jitter·주파수 변화를 관찰(특성화).
  - 보드 farm 자동화에서 "클럭 체크" 단계를 장비로 자동화(BV11/BV14).
- 주의: clock-out 경로 자체의 IO 버퍼 jitter가 섞이므로 **주파수 확인용**이지 정밀 jitter 측정용은 아니다 [추정].

### 3.4 PLL lock bring-up 로그 예 **[예시 출력]** (보드 없음 — 형식·해석 연습용)

```text
[bringup] clk: REF=24.000 MHz (strap OSC_SEL=0)
[bringup] pll_cpu: M=1 N=125 P=3 -> VCO 3000.0 MHz, OUT 1000.0 MHz
[bringup] pll_cpu: reset deassert, waiting lock (timeout 1000 us)
[bringup] pll_cpu: LOCK after 42 us
[bringup] pll_ddr: M=1 N=133 P=2 -> VCO 3192.0 MHz, OUT 1596.0 MHz
[bringup] pll_ddr: LOCK TIMEOUT (status=0x00000004) -> staying in BYPASS
[bringup] clkout: sel=PLL_CPU div=100 -> expect 10.000 MHz on CLK_OUT
```

- 줄별 해석: 기준 24 MHz는 strap으로 선택됨(BV05 연결) → CPU PLL 설정·VCO 3000 MHz(범위 상한 근처) → 42 µs에 lock 정상 → **DDR PLL이 lock timeout**: VCO 3192 MHz는 범위(예: 1000–3200 MHz) 상한에 매우 가까움 → 온도·칩 편차에 따라 lock 실패 가능 → **VCO를 범위 중간으로 옮기는 다른 M/N/P 조합**을 계산기로 찾는다. status 값의 비트 의미는 칩 TRM에서(여기선 지어낸 값). FW가 **timeout 후 bypass 유지 + 로그**를 한 것이 좋은 설계 — 무한 대기였다면 "부팅 멈춤"으로만 보였을 것.

### 3.5 PCIe refclk 측정

- **차동 프로브**로 REFCLK+/− 를 동시에(또는 두 single-ended 채널 math), 수신단 근처에서. termination이 맞아야 진폭·edge가 정상.
- 확인 항목: 주파수(100 MHz, SSC 끄면 ± ppm), **SSC on/off** — scope의 frequency trend/track 기능으로 보면 30–33 kHz 삼각형 모양으로 0 ~ −0.5% 흔들리는 게 보인다 [추정 — 계측기 기능], 진폭·crossing 전압, jitter(규격 필터 적용 SW).
- CC 보드에서 **endpoint 쪽에 refclk가 실제로 도착하나**(clock buffer 출력 enable 핀·OE 레지스터·커넥터 핀)가 bring-up 단골 — "카드 하나만 Detect 실패" 증상.

### 3.6 UART 깨짐 진단 흐름

```text
 UART 깨진 글자
   │
   ├─ scope로 TX의 start bit 폭 측정 (115200 → 8.68 µs)
   │     ├─ 정상 폭 → 클럭은 OK: 터미널 설정(baud·8N1·흐름제어), TX/RX 바뀜, 전압 레벨(1.8 V vs 3.3 V), RS-232 레벨 변환
   │     └─ 폭이 틀림 → 비율 계산 (§6 Part 3)
   │            ├─ 정확히 2×/0.5× → PLL bypass, 분주기 하나 차이, 오버샘플링(16× vs 8×) 설정
   │            ├─ 24/19.2, 24/26 같은 비율 → 기준 클럭 부품이 다름 (BOM alternate, strap)
   │            └─ 2–5% 애매 → 분주기 정수 반올림 오차 (높은 baud에서 정수 분주) → 분수 분주기 사용
   └─ clock-out 핀으로 UART 소스 클럭 직접 측정 → FW의 가정값과 비교
```

### 3.7 Linux에서 보는 클럭

- Lima VM(Ubuntu aarch64, QEMU virt — 실제 SoC 클럭 트리 없음)에서 **실제 실행한 출력** — 클럭이 `apb_pclk` 하나뿐인 것이 "가상 보드엔 PLL 트리가 없다"는 증거:

```text
$ sudo cat /sys/kernel/debug/clk/clk_summary
                                 enable  prepare  protect                                duty  hardware                            connection
   clock                          count    count    count        rate   accuracy phase  cycle    enable   consumer                         id
---------------------------------------------------------------------------------------------------------------------------------------------
 apb_pclk                            1       1        0        0           0          0     50000      Y   ARMH0061:00                     apb_pclk
                                                                                                           deviceless                      no_connection_id
$ sudo ls /sys/kernel/debug/clk/
apb_pclk  clk_dump  clk_orphan_dump  clk_orphan_summary  clk_summary
```

- 실제 SoC 보드에서는 osc → PLL → 분주기 → 게이트가 들여쓰기 트리로 나오고, 각 줄의 `rate`가 **커널이 계산한 값**(레지스터 설정 기준)이다 → 실측(clock-out·counter)과 비교해야 진짜 검증. `enable count 0`인 블록 클럭 = 그 블록 접근 시 hang 후보. `clk_orphan_summary`에 뭔가 있으면 부모 클럭 드라이버가 안 붙은 것.

## 4. 꼬리 질문 체인

### 체인 1 — "UART prints garbage on a new board. What do you suspect?"

Q → 꼬리1 "How do you prove it's the clock?" → 꼬리2 "The crystal is fine but the PLL doesn't lock. Next?" → 꼬리3 "What if it only fails at cold?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Consistent garbage means a baud mismatch, and the UART divisor is computed from an input clock the firmware assumes. On a new board the usual causes are a different crystal or oscillator than the firmware expects, the PLL not locked so the chip is still on the bypass clock, a wrong clock mux or divider for the UART, or an integer divisor that's too coarse at a high baud rate. Non-clock causes I'd rule out too: terminal settings, swapped TX and RX, or the wrong voltage level."
  - 한국어 핵심: 일관된 깨짐 = baud 불일치 = UART 입력 클럭 ≠ FW 가정. crystal 값·PLL bypass·mux/분주기·정수 분주 오차. 비클럭 원인도 배제.
- **꼬리1. Prove it's the clock?** — "Measure it. On a scope I trigger on TX and measure the start-bit width: at 115200 it should be 8.68 microseconds. The ratio tells me a lot — 10.85 microseconds is 20% slow, which matches a 19.2 MHz clock where the firmware assumed 24; exactly double means a bypassed PLL or a divider off by one stage. Then I confirm at the source by routing the UART clock or the PLL to the debug clock-out pin and measuring it with a counter."
  - 한국어 핵심: start bit 폭 → 실제 baud → 비율로 원인 분류(20% = 19.2 vs 24 MHz, 2× = bypass). clock-out 핀 + counter로 확정.
- **꼬리2. Crystal fine, PLL doesn't lock.** — "First check the configuration against the datasheet limits: is the phase-detector input frequency, ref divided by M, inside its range, and is the VCO frequency inside its range — I keep a small calculator for that. Then check the PLL's own supply, because PLLs have a separate analog rail that may be missing or noisy, and that the PLL was actually taken out of reset or power-down. If the configuration and power are right, try a VCO setting in the middle of the range and compare with another board; a lock failure on one board only points at the analog supply or the part."
  - 한국어 핵심: PFD·VCO 범위 검증(계산기), PLL 아날로그 전원, reset/power-down 해제 여부, VCO를 범위 중간으로, 보드 간 비교.
- **꼬리3. Only at cold?** — "That smells like crystal start-up margin: at cold the crystal's resistance can go up and the oscillator's gain down, so it starts slowly or not at all, and if the reset releases before the clock is stable the chip doesn't come up. I'd measure start-up time in the chamber, check the load capacitors and drive settings against the crystal's datasheet and do a gain-margin calculation, and check the reset delay. A PLL that's near the edge of its VCO range is the other suspect at temperature extremes."
  - 한국어 핵심: 저온 crystal 시작 마진(ESR↑, gain↓) + 리셋이 너무 일찍 해제 → chamber에서 시작 시간, CL·drive·gain margin, 리셋 지연. VCO 범위 끝도 의심.

</details>

### 체인 2 — "Why does PCIe need a 100 MHz refclk, and what's SSC?"

Q → 꼬리1 "What's the difference between common clock and SRIS?" → 꼬리2 "A card works in one slot but not another on the same board." → 꼬리3 "How would you validate the refclk?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "The serial link has no clock wire, so each side's PHY PLL multiplies a reference up to the bit rate, and the receiver recovers the clock from the data. The 100 MHz HCSL reference gives both sides a known, low-jitter base; with a common clock the two ends also share the same drift. SSC is spread-spectrum clocking: the reference is deliberately swept down by up to half a percent at about 30 to 33 kilohertz, which spreads the EMI energy instead of having a sharp peak at 100 MHz and its harmonics — useful for automotive EMC."
  - 한국어 핵심: 클럭선 없음 → 양쪽 PHY PLL의 기준. CC면 drift도 공유. SSC = 30–33 kHz로 0 ~ −0.5% down-spread해 EMI 피크 분산.
- **꼬리1. CC vs SRIS?** — "In common clock, one generator feeds both ends, so any SSC is identical on both sides and they only differ by trace delay. In separate-reference modes each side has its own clock: SRNS without spread, where the elastic buffers absorb a few hundred ppm, and SRIS with independent spread on each side, where the difference can be thousands of ppm at any instant, so the transmitter inserts SKP ordered sets more often and both PHYs must support SRIS. The spread is also limited to minus 0.3 percent there. You use separate references when it's hard to route a shared clock — across cables or between boards."
  - 한국어 핵심: CC = 한 소스, SSC 동일. SRNS = 각자, SSC 없음(수백 ppm). SRIS = 각자 SSC → 순간 차이 큼 → SKP 자주, PHY 지원 필요, 최대 −0.3%. 케이블·보드 간에 사용.
- **꼬리2. Works in one slot, not another.** — "Since the link is per slot, I'd first check that slot's refclk actually arrives: clock buffer output enabled — some buffers have per-output enable pins or registers — correct termination, and the trace to that connector. Then PERST# and power for that slot. If the clock is there, I'd compare link training on the two slots — fails at Detect versus downgrades speed — which separates a missing clock or reset from a signal-integrity issue on that channel."
  - 한국어 핵심: 그 슬롯의 refclk 도착 여부(버퍼 출력 enable·termination·배선) → PERST#·전원 → LTSSM 실패 단계로 클럭/리셋 vs SI 구분.
- **꼬리3. Validate the refclk?** — "With a differential probe at the receiver end: frequency and ppm offset with SSC off, the SSC profile with SSC on — the modulation rate and depth — amplitude and crossing levels, and phase jitter using analysis software with the PCIe-defined filters so the number is comparable with the spec for that generation. Across temperature and several boards, and repeated after any clock-buffer or layout change."
  - 한국어 핵심: 수신단 차동 측정, SSC off 주파수·ppm, SSC on 프로파일, 진폭·crossing, PCIe 필터 적용 jitter, 온도·보드 여러 장.

</details>

### 체인 3 — "Why can probing a crystal pin stop the oscillator?"

Q → 꼬리1 "So how do you check that the crystal is running?" → 꼬리2 "Touching it with the probe makes the board boot. What does that tell you?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "A crystal oscillator is a tuned circuit: the crystal plus the load capacitors on each pin set the frequency, and the chip's amplifier has only so much gain to overcome the crystal's losses. A standard 10× probe adds around 10 to 15 picofarads and some resistance, which detunes the circuit and loads it, so the frequency shifts or the oscillation stops altogether. So what you see on the probe isn't what the circuit does without it."
  - 한국어 핵심: crystal + load cap = 공진 회로, 증폭 여유 제한. 10× 프로브 10–15 pF가 detune·부하 → 주파수 이동/정지.
- **꼬리1. How to check?** — "Indirectly first: route an internal clock to the debug clock-out pin, which is buffered, and measure it, or measure a UART bit width or a GPIO toggle from a known timer. If I must look at the oscillator itself, I use a low-capacitance active probe on the output side of the oscillator, not the input. And JTAG working at all tells me some clock is running, though not necessarily the right one."
  - 한국어 핵심: clock-out 핀(버퍼됨), UART bit 폭·GPIO 토글로 간접 확인. 직접은 저용량 active 프로브로 출력 쪽.
- **꼬리2. Probe makes it boot?** — "That means the oscillator is marginal — the probe's capacitance happens to move it into a region where it starts. It's a real hardware bug: wrong load capacitors, a crystal with higher ESR than the design assumed, or insufficient drive. I'd record it, check the components against the crystal datasheet, compute gain margin, and test start-up across temperature and multiple boards, because in the field this becomes 'some cars don't start their ECU at cold'."
  - 한국어 핵심: 발진 마진 부족(CL·ESR·drive) = 실제 HW 버그. 부품 확인·gain margin 계산·온도/보드 여러 장 시작 테스트.

</details>

### 체인 4 (추가) — "A peripheral's registers read back zero, or the CPU hangs when you touch them."

Q → 꼬리1 "Clock is enabled and it still doesn't work." → 꼬리2 "It works after cold boot but not after a warm reset."

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Classic sign that the block isn't clocked or is held in reset, or its power domain is off. Depending on the interconnect, an access to an unclocked block returns zeros, returns an error, or never completes and hangs the bus. I'd check the clock-gate enable bit, the block's reset bit in the reset controller, the power domain status, and that the parent PLL is locked. With JTAG I can read those control registers even while the CPU is stuck, and see the PC sitting on the load instruction."
  - 한국어 핵심: clock gate·reset·전원 도메인·부모 PLL 확인. 미클럭 블록 접근 = 0/에러/버스 hang. JTAG로 제어 레지스터와 멈춘 PC 확인.
- **꼬리1. Clock enabled, still nothing.** — "Then it's likely still in reset, or the order was wrong: some blocks need their clock running while reset is asserted so the synchronous logic actually resets, and only then reset is released. Also check the mux — the gate can be on but selecting a source that's off — and in Linux, whether the clock framework disabled it later as unused, which shows up as an enable count of zero in clk_summary."
  - 한국어 핵심: 아직 reset 중, 또는 순서(클럭 켠 상태에서 reset assert → deassert). mux 소스 off, Linux CCF가 unused로 끔(enable count 0).
- **꼬리2. Cold boot OK, warm reset not.** — "Warm reset doesn't reset everything — some clock and PLL registers, the PMIC, or the block itself may keep their state, so the init code finds the PLL already configured or the block in a half-state it doesn't expect. I'd dump clock, PLL and reset-controller registers after each reset type and diff them, and make the init code put clocks into a known state — bypass, reprogram, relock — regardless of what it finds."
  - 한국어 핵심: warm reset은 일부 클럭/PLL 레지스터·PMIC를 유지 → init이 예상 못 한 상태. 리셋 종류별 레지스터 덤프 diff, init은 항상 알려진 상태로(bypass → 재설정 → relock).

</details>

### 체인 5 (추가) — "How would you validate the clock tree of a new SoC?"

Q → 꼬리1 "How do you check a 1.6 GHz PLL you can't probe?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "From the clock spec I'd list every PLL and every clock output with its expected frequency, and test each one: PLL lock and lock time, the measured frequency through the clock-out pin or an on-chip frequency counter if there is one, every mux setting and divider value, gating on and off, and the transitions — switching sources glitch-free while traffic is running. Then the analog side: jitter of the PLLs that feed DDR and SerDes, and all of it across voltage and temperature, since lock failures and jitter show up at corners."
  - 한국어 핵심: 모든 PLL/출력 목록 → lock·lock time·주파수(clock-out/내부 카운터)·mux·분주·gating·전환 → DDR/SerDes PLL jitter → 전압·온도 코너.
- **꼬리1. 1.6 GHz you can't probe?** — "Divide it down to the clock-out pin — 1.6 GHz over 100 is 16 MHz, easy for a counter — and since the divider is digital, the measured frequency times 100 is the PLL frequency. Many SoCs also have an internal frequency meter that counts the target clock against the reference over a window; I'd cross-check one against the other. For jitter I'd look at what that clock drives — DDR training margins or SerDes eye and BER — because the divided output adds its own IO jitter."
  - 한국어 핵심: clock-out으로 /100 분주 후 counter, 내부 주파수 측정기와 교차 확인. jitter는 그 클럭이 먹이는 DDR margin·SerDes eye/BER로 간접.

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up Cortex-R8, R82 and M0+ subsystems on FPGA before RTL freeze, and the clock setup is where the FPGA and silicon differ most — on FPGA the clock ratios are scaled down and there's no real PLL lock behavior, so I parameterized the clock and timeout values in the test firmware so the same binary could run on both `[숫자 채우기: FPGA vs 실리콘 주파수 비]`."
- "On PCIe Gen5 and Gen6 SSDs, the reference clock and SSC/SRIS configuration was part of host compatibility — I understand why a link that works with one host fails with another if the clocking assumptions differ `[숫자 채우기: 실제 겪은 사례가 있다면]`."
- "At Apple, debugging RF and interface issues taught me that clock and power noise show up as spurs — I'd look at the spectrum of a jittery clock for the buck regulator's switching frequency before blaming the PLL `[숫자 채우기]`."
- 정직하게: crystal 회로 설계는 HW 엔지니어 몫 → "My part is to prove every clock is what the spec says on every board and corner, and to have the firmware fail loudly — timeout, bypass, log — instead of hanging."

## 6. 실습 / 코드

### 6.1 PLL 설정 계산기 — [pll_calc.c](onsitePrep/bv04/pll_calc.c)

- 입력(코드 안 preset): f_ref, 목표 f_out, M/N 범위, P 허용 목록, PFD 범위, VCO 범위, fractional 비트 수. 모든 (M, P)에 대해 N = target × P × M / f_ref 를 양자화(정수 또는 1/2^frac) → 범위 검사 → **|오차 ppm|** 순 정렬(1 ppm 미만은 동률로 보고 **PFD가 높은 쪽** 우선 — 노이즈 배율이 작고 루프가 빠름). 해가 없으면 도달 가능 범위를 출력.
- 빌드·실행 (spec 명령 그대로, ASan+UBSan):

```bash
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined onsitePrep/bv04/pll_calc.c -o /tmp/pll_calc && /tmp/pll_calc
```

- **실제 출력** (경고·sanitizer 보고 없음, exit 0):

```text
--- stm32f4-like : f_ref 8.000000 MHz -> target 168.000000 MHz (integer-N) ---
    limits: M 2..63  N 50..432  PFD 1.000..2.000 MHz  VCO 100..432 MHz  P {2,4,6,8}
    tried 248 (M,P) pairs: rejected PFD 228, N-range 13, VCO 2
    rank   M  N            P   f_pfd(MHz)  f_vco(MHz)  f_out(MHz)      err(ppm)
    #1    4  168.00000    2      2.0000     336.000    168.000000       +0.000
    #2    5  210.00000    2      1.6000     336.000    168.000000       +0.000
    #3    6  252.00000    2      1.3333     336.000    168.000000       +0.000
    #4    7  294.00000    2      1.1429     336.000    168.000000       +0.000

--- zynqmp-ps-pll-like : f_ref 33.333000 MHz -> target 1200.000000 MHz (integer-N) ---
    limits: M 1..1  N 25..125  PFD 27.000..60.000 MHz  VCO 1500..3000 MHz  P {1,2}
    tried 2 (M,P) pairs: rejected PFD 0, N-range 0, VCO 1
    rank   M  N            P   f_pfd(MHz)  f_vco(MHz)  f_out(MHz)      err(ppm)
    #1    1  72.00000     2     33.3330    2399.976   1199.988000      -10.000

--- generic-soc-int : f_ref 24.000000 MHz -> target 1000.000000 MHz (integer-N) ---
    limits: M 1..8  N 16..320  PFD 5.000..50.000 MHz  VCO 1000..3200 MHz  P {1,2,3,4,5,6,7,8}
    tried 64 (M,P) pairs: rejected PFD 32, N-range 19, VCO 4
    rank   M  N            P   f_pfd(MHz)  f_vco(MHz)  f_out(MHz)      err(ppm)
    #1    1  125.00000    3     24.0000    3000.000   1000.000000       +0.000
    #2    2  250.00000    3     12.0000    3000.000   1000.000000       +0.000
    #3    3  125.00000    1      8.0000    1000.000   1000.000000       +0.000
    #4    3  250.00000    2      8.0000    2000.000   1000.000000       +0.000

--- generic-soc-int : f_ref 25.000000 MHz -> target 148.500000 MHz (integer-N) ---
    limits: M 1..8  N 16..320  PFD 5.000..50.000 MHz  VCO 1000..3200 MHz  P {1,2,3,4,5,6,7,8}
    tried 64 (M,P) pairs: rejected PFD 24, N-range 3, VCO 27
    rank   M  N            P   f_pfd(MHz)  f_vco(MHz)  f_out(MHz)      err(ppm)
    #1    2  95.00000     8     12.5000    1187.500    148.437500     -420.875
    #2    4  190.00000    8      6.2500    1187.500    148.437500     -420.875
    #3    5  208.00000    7      5.0000    1040.000    148.571429     +481.000
    #4    5  238.00000    8      5.0000    1190.000    148.750000    +1683.502

--- generic-soc-frac : f_ref 25.000000 MHz -> target 148.500000 MHz (fractional-N) ---
    limits: M 1..8  N 16..320  PFD 5.000..50.000 MHz  VCO 1000..3200 MHz  P {1,2,3,4,5,6,7,8}
    tried 64 (M,P) pairs: rejected PFD 24, N-range 3, VCO 27
    rank   M  N            P   f_pfd(MHz)  f_vco(MHz)  f_out(MHz)      err(ppm)
    #1    1  41.58000     7     25.0000    1039.500    148.500007       +0.044
    #2    1  47.52000     8     25.0000    1188.000    148.500013       +0.090
    #3    2  83.16000     7     12.5000    1039.500    148.500007       +0.044
    #4    2  95.03999     8     12.5000    1188.000    148.499990       -0.071
    best encodes as N_int=41, N_frac=38011/65536

--- generic-soc-int : f_ref 24.000000 MHz -> target 5.000000 MHz (integer-N) ---
    limits: M 1..8  N 16..320  PFD 5.000..50.000 MHz  VCO 1000..3200 MHz  P {1,2,3,4,5,6,7,8}
    tried 64 (M,P) pairs: rejected PFD 32, N-range 32, VCO 0
    NO SOLUTION: reachable f_out = VCO/P = [125.000, 3200.000] MHz
```

- 읽는 법:
  - **STM32F4-like**: 정확한 해가 4개 이상 — 그중 **PFD 2 MHz(M=4)** 가 1위 = ST가 "jitter 줄이려면 2 MHz 권장"한 것과 같은 판단. 대부분의 (M,P) 조합은 PFD 범위(1–2 MHz)에서 탈락(228/248).
  - **Zynq-like**: P=1이면 VCO가 1200 MHz로 범위(1500–3000) 아래라 탈락 → **DIV2가 필요한 이유**가 숫자로 보임. 33.333 MHz가 정확히 100/3 MHz가 아니라서 −10 ppm.
  - **148.5 MHz**: integer-N 최선 −421 ppm(디스플레이 pixel clock엔 보통 허용 안 됨 [추정]) → fractional-N이면 0.1 ppm 이하. N_frac 38011/65536 같은 값이 실제 레지스터에 들어갈 형태.
  - **5 MHz**: VCO 최소 1000 MHz ÷ 최대 P 8 = 125 MHz가 하한 → PLL로는 불가 → **PLL 뒤의 분주기**(또는 bypass + 분주)로 만들어야 한다는 결론.
  - 확장 `[미실행]`: VCO가 범위 끝에서 X% 이내면 경고(§3.4 lock timeout 사례), 다단 분주기(PLL → 버스 분주 → 블록 분주) 체인 탐색.

### 6.2 UART baud 오차 계산기 — [uart_baud.c](onsitePrep/bv04/uart_baud.c)

- (a) 16550식 **정수 분주**: div = round(clk/(16·baud)) (b) **Arm PL011 분수 분주**: BAUDDIV = UARTCLK/(16·baud) = IBRD + FBRD/64, FBRD = int(frac×64 + 0.5), IBRD 16비트·FBRD 6비트 [확인됨 — 드라이버·QEMU 이슈 기준][14]. 판정 기준: 한쪽 ±2.5% 이내(16× oversampling RX, 총 예산 약 5% 가정) [확인됨][9], 1% 이하 OK, 그 사이 MARGINAL. Maxim AN2141은 보통 링크 ±3.3%, 열악한 링크 ±2%를 **양쪽 합계 예산**으로 계산 [확인됨][8].
- 빌드·실행: `cc -isysroot ... -fsanitize=address,undefined onsitePrep/bv04/uart_baud.c -o /tmp/uart_baud && /tmp/uart_baud` → **실제 출력** (exit 0):

```text
Part 1: divisor error (16x oversampling)
  UARTCLK     baud     | int div  actual      err%    verdict        | PL011 IBRD+FBRD/64  actual      err%    verdict
   1.8432 MHz     9600 |    12     9600.0   +0.000  OK             |    12 +  0/64     9600.0   +0.000  OK
   1.8432 MHz   115200 |     1   115200.0   +0.000  OK             |     1 +  0/64   115200.0   +0.000  OK
   1.8432 MHz   921600 | clock too slow for 16x oversampling
   1.8432 MHz  3000000 | clock too slow for 16x oversampling
  19.2000 MHz     9600 |   125     9600.0   +0.000  OK             |   125 +  0/64     9600.0   +0.000  OK
  19.2000 MHz   115200 |    10   120000.0   +4.167  FAIL (garbage) |    10 + 27/64   115142.4   -0.050  OK
  19.2000 MHz   921600 |     1  1200000.0  +30.208  FAIL (garbage) |     1 + 19/64   925301.2   +0.402  OK
  19.2000 MHz  3000000 | clock too slow for 16x oversampling
  24.0000 MHz     9600 |   156     9615.4   +0.160  OK             |   156 + 16/64     9600.0   +0.000  OK
  24.0000 MHz   115200 |    13   115384.6   +0.160  OK             |    13 +  1/64   115246.1   +0.040  OK
  24.0000 MHz   921600 |     2   750000.0  -18.620  FAIL (garbage) |     1 + 40/64   923076.9   +0.160  OK
  24.0000 MHz  3000000 | clock too slow for 16x oversampling
  48.0000 MHz     9600 |   313     9584.7   -0.160  OK             |   312 + 32/64     9600.0   +0.000  OK
  48.0000 MHz   115200 |    26   115384.6   +0.160  OK             |    26 +  3/64   115177.0   -0.020  OK
  48.0000 MHz   921600 |     3  1000000.0   +8.507  FAIL (garbage) |     3 + 16/64   923076.9   +0.160  OK
  48.0000 MHz  3000000 |     1  3000000.0   +0.000  OK             |     1 +  0/64  3000000.0   +0.000  OK
  100.0000 MHz     9600 |   651     9600.6   +0.006  OK             |   651 +  3/64     9599.9   -0.001  OK
  100.0000 MHz   115200 |    54   115740.7   +0.469  OK             |    54 + 16/64   115207.4   +0.006  OK
  100.0000 MHz   921600 |     7   892857.1   -3.119  FAIL (garbage) |     6 + 50/64   921659.0   +0.006  OK
  100.0000 MHz  3000000 |     2  3125000.0   +4.167  FAIL (garbage) |     2 +  5/64  3007518.8   +0.251  OK

Part 2: FW assumed one UART clock, hardware delivers another (115200 target)
  correct clock                                                  IBRD=13  FBRD=1  ->  115246.1 baud    +0.040%  OK
  26 MHz crystal fitted instead of 24 MHz                        IBRD=13  FBRD=1  ->  124849.9 baud    +8.377%  FAIL (garbage)
  19.2 MHz osc option (BOM alternate)                            IBRD=13  FBRD=1  ->   92196.9 baud   -19.968%  FAIL (garbage)
  PLL bypassed: UART gets 24 MHz osc, FW assumed 48 MHz PLL out  IBRD=26  FBRD=3  ->   57588.5 baud   -50.010%  FAIL (garbage)
  crystal +100 ppm (aging/temp)                                  IBRD=13  FBRD=1  ->  115257.6 baud    +0.050%  OK

Part 3: baud from a bit width measured on the scope (start bit edge-to-edge)
  bit =   8.68 us ->  115207.4 baud  (vs 115200:   +0.01%  OK)
  bit =   8.70 us ->  114942.5 baud  (vs 115200:   -0.22%  OK)
  bit =  10.85 us ->   92165.9 baud  (vs 115200:  -19.99%  FAIL (garbage))
  bit =  17.36 us ->   57603.7 baud  (vs 115200:  -50.00%  FAIL (garbage))
```

- 읽는 법:
  - **Part 1**: 정수 분주는 **높은 baud에서 반올림 오차가 폭발**(24 MHz·921600 → −18.6%, 100 MHz·921600 → −3.1%) — 낮은 baud(115200)에선 괜찮다가 **baud만 올렸더니 깨진다**의 원인. PL011 분수 분주(1/64)는 같은 조건에서 0.5% 이내. 1.8432 MHz는 115200의 정확한 배수라 오차 0(옛날 UART crystal이 이 값인 이유).
  - **Part 2**: 클럭 ppm 오차(+100 ppm = 0.01%)는 UART엔 무의미 → **UART가 깨진다면 ppm이 아니라 '다른 클럭'** 이다(20%, 2× 같은 큰 비율). 반대로 UART가 멀쩡해도 Ethernet·timestamp에 필요한 ppm 정확도는 증명 못 한다.
  - **Part 3**: scope에서 잰 bit 폭 하나로 실제 baud·비율을 바로 계산 → 10.85 µs = 19.2/24, 17.36 µs = 정확히 2× = PLL bypass 가설.

## 7. 자주 틀리는 것

1. **crystal 핀에 10× 프로브** — 측정이 회로를 바꾼다. clock-out 핀·간접 측정 먼저.
2. **CL 값을 C1/C2 값으로 그대로** — CL = C1·C2/(C1+C2) + C_stray. 직렬이라 각 커패시터는 대략 2×(CL − C_stray).
3. **PLL lock을 timeout 없이 무한 대기** — "부팅이 멈췄다"로만 보이고 원인이 안 보인다. timeout + bypass 유지 + 로그.
4. **VCO 범위 끝에 걸친 설정** — 상온 한 보드에선 lock, 고온·다른 칩에선 실패. 범위 중간을 선택.
5. **"UART가 되니 클럭 OK"** — UART 예산은 % 단위, Ethernet·USB·timestamp는 ppm 단위. 따로 측정.
6. **clock enable만 하고 reset 해제를 잊음(또는 순서 반대)** — 블록이 0만 읽힘/버스 hang/warm reset 후만 실패.
7. **Linux가 unused clock을 끄는 것을 모름** — bootloader에선 되던 장치가 커널 부팅 후반에 죽음.
8. **SSC를 jitter와 혼동** — SSC는 30–33 kHz의 의도된 느린 변조, PLL이 따라간다. 문제는 **양끝이 다른 SSC**일 때(CC/SRIS 설정 불일치).
9. **PCIe refclk를 "100 MHz 나오나"만 확인** — 수신단 도착·termination·SSC 설정·규격 필터 jitter까지.
10. **공칭 주파수 하드코딩**(1.2 GHz) — 실제는 1199.988 MHz(−10 ppm). 장시간 timestamp·타이머 계산에서 누적.

## 8. 출처

1. ST AN2867 — Oscillator design guide for STM32 MCUs (load capacitance, gain margin): https://www.st.com/resource/en/application_note/an2867-oscillator-design-guide-for-stm32-microcontrollers-stmicroelectronics.pdf
2. STM32F100 datasheet excerpt — CL = CL1·CL2/(CL1+CL2) + Cstray, stray 2–7 pF, 6 pF + 2 pF → 8 pF 예: https://www.alldatasheet.net/html-pdf/933921/STMICROELECTRONICS/STM32F100RC/103556/53/STM32F100RC.html
3. Microchip online docs — oscillator measurement (10× probe 10–15 pF loading, probe buffered outputs instead): https://onlinedocs.microchip.com/oxy/GUID-E1A9010A-4FFF-4CCD-912A-926ED28D22D6-en-US-8/GUID-A218AC70-8FDC-4008-A164-8BB9BF9C2278.html
4. TI SNAA386 — PCIe clocking (100 MHz HCSL, CC/SRNS/SRIS, SSC 30–33 kHz, −0.5% / −0.3%, ppm budgets): https://www.ti.com/document-viewer/lit/html/SNAA386/GUID-FACD087F-2C1E-460A-8C4F-76351435EEBC
5. congatec AN45 — PCI Express Reference Clock Design Considerations (±300 ppm Gen1–4, ±100 ppm Gen5–6): https://www.congatec.com/fileadmin/user_upload/Documents/Application_Notes/AN45_PCI_Express_Reference_Clock_Design_Considerations.pdf
6. Secondary (unverified) PCIe Gen5 refclk jitter-limit discussion: https://gitcode.csdn.net/6aa0d5651a35784c84205223.html
7. Diodes PI6CG334Q — PCIe 6.0/5.0/4.0 clock generator, 4-output, AEC-Q grade: https://diodes.com/part/view/PI6CG334Q
8. Maxim/ADI AN2141 — Determining Clock Accuracy Requirements for UART Communications (±3.3% normal, ±2% nasty, shared between ends): https://www.analog.com/en/resources/technical-articles/determining-clock-accuracy-requirements-for-uart-communications.html
9. OpenTitan UART README (16× oversampling, both sides within ±2.5%): https://opensecura.googlesource.com/3p/lowrisc/opentitan/+show/82af7b49c3b7e9b6e06ff907e2c08efe17d327a4/hw/ip/uart/README.md
10. Linux kernel — drivers/clk/zynqmp/pll.c (FBDIV 25–125, VCO 1.5–3.0 GHz, 16-bit frac): https://raw.githubusercontent.com/torvalds/linux/master/drivers/clk/zynqmp/pll.c
11. AMD DS925 — Zynq UltraScale+ PS clocks / PLL VCO min/max: https://docs.amd.com/r/en-US/ds925-zynq-ultrascale-plus/PS-Clocks
12. AMD UG1087 — VPLL_CTRL (CRF_APB) register (FBDIV [14:8], DIV2 bit 16): https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/VPLL_CTRL-CRF_APB-Register
13. ST community — STM32F4 PLL settings (RM0090 caution: VCO input 1–2 MHz, 2 MHz recommended for jitter; PLLN/PLLP ranges): https://community.st.com/t5/stm32-mcus-products/pll-settings/td-p/432841 · HAL RCC_PLLInitTypeDef: https://docs.ros.org/en/api/picovoice_driver/html/structRCC__PLLInitTypeDef.html
14. QEMU issue — PL011 IBRD 16-bit / FBRD 6-bit masks (refs Arm DDI0183): https://gitlab.com/qemu-project/qemu/-/issues/2610 · Genode PL011 driver (BAUDDIV, FBRD rounding): https://gitea.c3d2.de/ehmry/genode/commit/2b0c6133365547d52cae15109b9aa81503af866c
