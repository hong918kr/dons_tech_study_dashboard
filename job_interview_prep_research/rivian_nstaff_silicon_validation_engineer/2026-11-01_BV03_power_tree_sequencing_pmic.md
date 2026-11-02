# BV03 · 전원 트리 · power sequencing · PMIC · rail 측정
<!-- theme: pastel -->

> **묶음**: B 전원·클럭·리셋 · **우선**: P0 (보드 디버그 질문의 1차 가설이 거의 항상 "전원") · **선행 노트**: [BV02 회로도 읽기](2026-11-01_BV02_schematic_layout_reading.html) · [BV01 큰 그림](2026-11-01_BV01_si_board_validation_big_picture.html) · **다음 노트**: [BV04 클럭](2026-11-01_BV04_clocks_osc_pll_refclk.html) · [BV05 리셋·strap](2026-11-01_BV05_reset_strap_boot_mode.html) · [BV06 첫 전원 인가 체크리스트](2026-11-01_BV06_first_power_on_checklist.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · **코드**: [bv03/power_seq_check.py](onsitePrep/bv03/power_seq_check.py) · [bv03/pmbus_decode.py](onsitePrep/bv03/pmbus_decode.py) · [실행 로그](onsitePrep/bv03/run_output.txt)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"새 보드 첫 전원, 뭘 확인하나?" · "왜 power sequencing이 중요한가, 순서 틀리면 무슨 일?" · "ripple을 제대로 재는 법?" · "부하 걸 때만 rail이 10% 떨어진다" · "PMIC가 꺼졌다 — fault 레지스터로 뭘 보나" · "AVS/DVFS 검증은?"**
- **외울 것 1 — 전원 트리**: 입력(차량 12 V / bench) → **buck**(스위칭, 효율 85–95%, ripple 있음, 큰 전류) / **LDO**(선형, 깨끗, 효율 ≈ Vout/Vin, 작은 전류·노이즈 민감한 PLL/아날로그용) → 각 rail. AI SoC의 core/NPU rail은 **수십 A짜리 multiphase buck** [추정].
- **외울 것 2 — sequencing 4요소**: **순서**(데이터시트 지정, 흔히 always-on → core → analog/PHY → IO, 끌 때는 역순) · **간격**(앞 rail PGOOD 후 다음 EN) · **ramp**(soft-start 시간, **monotonic** 이어야) · **리셋은 마지막 rail PGOOD + 지연 뒤 해제**. 틀리면 **back-powering, latch-up, IO glitch, strap 오독, inrush 과부하**.
- **외울 것 3 — 측정 규칙**: 첫 인가 전 **rail-GND 저항**(합선) → **전류 제한 bench PSU**로 낮게 시작 → DMM으로 DC 값 → scope 다채널로 **10%/90% 순서·ramp·overshoot** → ripple은 **20 MHz BW limit + 출력 커패시터 바로 옆 + ground spring(긴 ground lead 금지)** → 부하 계단으로 **droop**. 디지털 증거는 **PMIC fault 레지스터(OV/UV/OC/OT, PGOOD)** 와 telemetry.
- 코드: mock scope CSV로 **power-up sequence checker**(Python) 실행 — 정상 보드 PASS, 고장 보드(3V3이 2번째로 켜짐·DDR rail 13% overshoot·PHY rail mid-ramp sag·reset 너무 이름)에서 **5개 위반 FAIL** 실측 출력 §6.
- **30초 영어 답 (헤드라인 "First power-on — what do you check?")**: "Before power, I check the schematic power tree so I know every rail, its voltage, order and expected current, and I measure each rail's resistance to ground to catch shorts. Then I power the board from a bench supply with a low current limit and watch the input current — if it hits the limit immediately, I stop and look for a short with a thermal camera. If the current is sane, I measure every rail with a DMM against its tolerance, then capture the power-up sequence on a multi-channel scope: order, ramp time, monotonic ramps, overshoot, PGOOD, and that reset releases only after the last rail is good. Then I read the PMIC fault and status registers over I2C. Only when power is proven do I move on to clocks, reset and JTAG."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "새 아파트 단지에 물을 넣는 순서"

새 아파트 단지(= 보드)에 상수도(= 전원)를 처음 넣는다고 하자.

1. **큰 수도관 → 동별 감압 밸브 → 세대별 수도꼭지**: 시(차량 배터리 12 V)에서 오는 높은 압력의 물을 그대로 세대에 넣으면 터진다. 그래서 단지 입구의 **감압 밸브**(= 레귤레이터: buck/LDO)가 동마다 필요한 압력(= 전압: 0.75 V, 1.8 V, 3.3 V …)으로 낮춰 준다. 이 "어디서 어디로 몇 V가 흐르나" 지도가 **power tree(전원 트리)**.
2. **물 넣는 순서가 있다**: 기초 배관(= core)에 물이 차기 전에 베란다 수도(= IO)부터 틀면 거꾸로 흘러 엉뚱한 배관이 반쯤 찬다(= **back-powering**). 관리소가 "1번 밸브 압력 정상" 램프(= **PGOOD**)가 켜지면 그다음 밸브를 연다(= **enable chain**). 이게 **power sequencing**.
3. **천천히 열어야 한다**: 밸브를 확 열면 순간 물이 몰려 수도관이 흔들리고 계량기가 튄다(= **inrush 전류**, overshoot). 그래서 밸브를 정해진 속도로 연다(= **soft-start ramp**). 중간에 압력이 다시 떨어졌다 오르면(= **non-monotonic**) 센서가 "물 끊겼다 → 다시 들어왔다"로 착각한다(= POR 회로 오동작).
4. **관리소 중앙 제어실(= PMIC)**: 밸브 여러 개를 한 칩에 모아 놓고, 순서·속도·압력 기준을 **미리 공장에서 써 둔 설정(OTP/NVM)** 대로 실행한다. 문제가 생기면 "몇 동 과압/저압/과전류/과열" 기록(= **fault 레지스터**)을 남기고 밸브를 잠근다.
5. **입주 전 점검**: 물 넣기 전에 배관이 바닥(= GND)과 붙어 있지 않은지 저항을 재고(= **rail-GND 저항**), 처음엔 단지 입구 밸브를 조금만 열어(= **전류 제한 PSU**) 새는 곳이 없는지 본다.

### 1.2 왜 전압이 하나가 아니라 여러 개인가

- **코어 로직(CPU·NPU·GPU)**: 트랜지스터가 작을수록 낮은 전압에서 돈다(예: 0.75 V 근처 [추정]). 전력 ∝ C·V²·f 라서 **전압을 조금만 낮춰도 전력이 크게 준다** → AVS/DVFS로 전압을 동적으로 바꾸기도 한다.
- **IO(핀으로 밖과 대화하는 회로)**: 바깥 칩과 약속한 전압 레벨(1.8 V, 3.3 V)을 써야 한다.
- **메모리(LPDDR)**: JEDEC 표준이 정한 여러 전압(LPDDR5: VDD1 1.8 V, VDD2H 1.05 V, VDD2L 0.9 V, VDDQ 0.5 V [추정 — JESD209-5 공개 요약 기준, 부품 데이터시트로 확인]).
- **아날로그(PLL, SerDes PHY, ADC)**: 노이즈에 민감 → **깨끗한 전원(LDO, 별도 필터)** 을 따로 준다.
- **Always-on(AO) / safety island**: 차가 꺼져도 깨어 있는 작은 MCU·RTC·wake 로직 → 제일 먼저 켜지고 제일 늦게 꺼진다.

### 1.3 Buck vs LDO — 쉬운 그림

```text
 Buck (스위칭 레귤레이터) = "수도꼭지를 아주 빨리 열었다 닫았다 해서 평균 압력을 맞춤"
   Vin 12V ──[SW 스위치 on/off ~MHz]──┬──[L 인덕터]──┬──▶ Vout 0.75V
                                      │              │
                                   [다이오드/       [C 출력 커패시터]  ← 출렁임을 평평하게
                                    하단 스위치]     │
                                      GND           GND
   + 효율 85–95% (열 적음), 큰 전류(수십 A, multiphase)
   - 스위칭 주파수의 ripple + 스위칭 순간 spike(노이즈), 부하 급변 시 droop

 LDO (Low-Dropout 선형 레귤레이터) = "밸브를 반쯤 잠가 남는 압력을 열로 버림"
   Vin 1.8V ──[패스 트랜지스터 (가변 저항처럼)]──▶ Vout 0.9V
                         ▲
                    [오차 증폭기: Vout을 기준과 비교해서 계속 조절]
   + 출력이 깨끗(ripple 거의 없음, PSRR로 입력 노이즈도 걸러냄), 부품 적음
   - 효율 ≈ Vout/Vin (0.9/1.8 = 50%, 나머지는 열), 큰 전류엔 부적합, dropout 전압 필요
```

- 그래서 흔한 구성: **buck으로 크게 내리고 → 민감한 아날로그 rail만 LDO로 한 번 더 다듬기** ("buck + LDO post-regulation").

### 1.4 전원 트리 그림 — 자동차 AI SoC 보드 예 [추정]

> 아래는 공개 정보로 만든 **일반적인 모양**이다. Rivian 보드의 실제 rail 이름·값은 모름 [추정]. 실제 값은 SoC 데이터시트의 "Recommended operating conditions"와 보드 회로도 power page에서 온다.

```text
 차량 12 V 배터리 (cold crank 시 ~6 V까지 떨어질 수 있음 → BV15)
   │
   ├─[역전압·과전압 보호, 필터]─▶ 12V_PROT
   │                                │
   │                      [Pre-regulator buck 12V→5V 또는 3.3V]  (자동차 등급, 넓은 입력 범위)
   │                                │
   │                          VSYS_3V3/5V ──────────────────────────────────────────┐
   │                                │                                                │
   │                 ┌──────────────┴──────────── PMIC (예: 다상 buck 4–5개 + LDO 몇 개) ┐
   │                 │  BUCK1+2 (2-phase) ─▶ VDD_CPU_NPU 0.75 V  수십 A, AVS 대상     │
   │                 │  BUCK3 ────────────▶ VDD_CORE/SOC 0.8 V                       │
   │                 │  BUCK4 ────────────▶ VDD_DDR (VDD2H) 1.05 V                   │
   │                 │  BUCK5 ────────────▶ VDD_IO_1V8 (+ LPDDR VDD1 1.8 V)           │
   │                 │  LDO1 ─────────────▶ VDDA_PLL 0.8/1.8 V (저노이즈)            │
   │                 │  LDO2 ─────────────▶ VDDA_PHY 0.85 V (SerDes/PCIe/MIPI PHY)   │
   │                 │  LDO3 ─────────────▶ VDD_AO 0.8 V (always-on / safety MCU)    │
   │                 │  nRSTOUT, PGOOD, nINT, I2C/SPI, watchdog ─▶ SoC              │
   │                 └─────────────────────────────────────────────────────────────┘
   │                                                                                 │
   └── 별도 load switch / 작은 buck ─▶ VDD_IO_3V3, 카메라 serializer, Ethernet PHY, eMMC/UFS ◀┘
```

- 실제 예: TI Jacinto TDA4VM(자동차 ADAS SoC)은 **TPS6594-Q1 PMIC** 와 짝을 이룬다 — 다상 buck + LDO, **NVM에 저장된 기본 power-up/down 시퀀스와 전압**, 전 출력 전압 감시, ASIL-D까지의 기능 안전 지원(MCU safety island와 함께) [확인됨][11]. 부품 번호(orderable)마다 NVM 설정이 다르다 = "같은 PMIC라도 OTP가 다르면 다른 보드" [확인됨][11].

### 1.5 Sequencing을 시간축으로 보면

```text
 time ──▶      t0       t1        t2        t3        t4            t5
 VSYS     ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 VDD_AO   _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 PG_AO    _________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  ← PGOOD = "나 정상"
 VDD_CORE ____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  ← EN = PG_AO
 VDDA_PHY _____________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 VDD_IO   ______________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 VDD_DDR  _______________________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 RESET_N  ______________________________________________________|‾‾‾‾‾‾‾‾‾‾  ← 마지막 PGOOD + 지연
                 │← ramp →│← gap →│
   각 rail: 10%→90% 상승 시간(ramp), 앞 rail 90% → 다음 rail 10% 간격(gap), overshoot, monotonic
   끌 때(power-down): 보통 역순. RESET_N을 먼저 내리고 → IO → … → AO 마지막
```

### 1.6 PMIC가 하는 일 (그림)

```text
           ┌────────────────────────── PMIC ───────────────────────────┐
  VSYS ──▶ │  [NVM/OTP: 기본 전압·순서·지연·PGOOD 매핑·fault 동작]       │
           │        │                                                   │
           │  [시퀀서 상태기계] ─EN─▶ BUCK1..5, LDO1..4 ─▶ rail 출력      │
           │        ▲                    │                              │
           │        │             [전압 감시 OV/UV, 전류 제한 OC, 온도 OT] │
           │        │                    ▼                              │
           │  [레지스터 맵: VSET, EN, STATUS, FAULT(sticky), INT mask]    │
           │        ▲ I2C/SPI/PMBus          ▼ nINT, PGOOD, nRSTOUT       │
           └────────┼─────────────────────────┼───────────────────────────┘
                    │                         ▼
             SoC/BMC/디버그 PC         SoC reset 핀 · 인터럽트
```

### 1.7 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| rail | power rail | 보드 위 한 종류 전압이 흐르는 전원선(예: VDD_CORE 0.75 V) | 측정·순서·fault가 rail 단위 |
| power tree | — | 입력 전원에서 각 rail까지 누가 누구를 먹이는지 그린 지도 | bring-up 체크리스트의 뼈대 |
| buck | buck (step-down) converter | 스위치를 빠르게 켜고 꺼서 전압을 내리는 스위칭 레귤레이터 | 효율 좋음, ripple·droop 측정 대상 |
| LDO | Low-Dropout regulator | 남는 전압을 열로 버리는 선형 레귤레이터, 출력이 깨끗 | PLL·아날로그 rail용 |
| dropout | dropout voltage | LDO가 동작하려면 Vin이 Vout보다 최소 이만큼 높아야 함 | 입력이 낮아지면 LDO 출력이 같이 떨어짐 |
| PSRR | Power Supply Rejection Ratio | 입력 노이즈를 출력에서 얼마나 걸러내나(dB) | buck ripple을 LDO가 막아 주는 정도 |
| PMIC | Power Management IC | 레귤레이터 여러 개 + 시퀀서 + 감시 + 통신을 한 칩에 | 전원 bring-up의 "관리소" |
| OTP / NVM | One-Time Programmable / Non-Volatile Memory | PMIC 기본 설정(전압·순서)이 공장에서 써진 비휘발 메모리 | 잘못된 OTP = 잘못된 보드. 부품 번호로 확인 |
| EN | enable | 레귤레이터를 켜라는 입력 핀 | sequencing의 "다음 밸브 열어" |
| PGOOD / PG | Power Good | 레귤레이터가 "내 출력이 목표 범위 안에 들어왔다"고 알려주는 신호선(보통 open-drain) | 다음 rail EN·reset 해제의 근거 |
| soft-start | — | 출력 전압을 정해진 시간에 걸쳐 천천히 올리는 기능 | inrush·overshoot 제한 |
| ramp | ramp / rise time | rail이 10%→90%까지 오르는 시간 | 데이터시트에 min/max가 있음 |
| monotonic | — | 올라가는 도중 한 번도 내려가지 않음 | 아니면 POR·PGOOD가 오동작 |
| inrush | inrush current | 켜는 순간 커패시터를 채우느라 몰리는 큰 전류(I = C·dV/dt) | PSU 전류 제한·퓨즈·상위 rail sag |
| overshoot | — | 목표 전압을 넘었다가 돌아오는 것 | 절대 최대 전압 넘으면 칩 손상 |
| ripple | output ripple | 스위칭 주파수로 반복되는 작은 출렁임 | 아날로그/PLL jitter, 스펙 mV p-p |
| droop / undershoot | load transient droop | 부하가 갑자기 늘 때 순간 전압이 꺼지는 것 | 랜덤 hang·brown-out의 주범 |
| IR drop | — | 전류 × 배선 저항만큼 칩 핀에서 전압이 낮아지는 것 | PMIC 출력은 정상인데 칩에선 낮음 |
| remote sense | — | 레귤레이터가 칩 바로 옆 전압을 별도 선으로 재서 보정 | IR drop 보상, 측정 위치 주의 |
| back-powering | back-feeding | 꺼진 rail이 IO 핀의 ESD 다이오드를 통해 다른 전원에서 반쯤 충전되는 것 | 이상 상태·latch-up·리셋 오동작 |
| latch-up | — | 칩 안 기생 사이리스터가 켜져 전원-GND가 붙어 버리는 현상 | 큰 전류·발열·영구 손상 |
| POR | Power-On Reset | 전원이 일정 전압 넘을 때 칩 내부 리셋을 풀어주는 회로 | non-monotonic ramp에 취약 |
| OV/UV/OC/OT | Over-Voltage / Under-Voltage / Over-Current / Over-Temperature | PMIC fault 종류 | fault 레지스터 해석 |
| hiccup / latch-off | — | fault 후 자동 재시도(hiccup) vs 꺼진 채 유지(latch-off) | "주기적으로 꺼졌다 켜짐" 증상 |
| telemetry | — | PMIC/모니터가 재는 V·I·온도 값(ADC) | 자동화된 전력 측정 |
| PMBus | Power Management Bus | SMBus(I2C 계열) 위의 전원 장치 표준 명령 세트 | 서버/고전류 VR 표준, Linux hwmon |
| power domain / island | — | 칩 안에서 따로 켜고 끌 수 있는 전원 구역 | power gating·retention 검증 |
| power gating | — | 안 쓰는 블록의 전원을 칩 안 스위치로 끊음 | 누설 전류 절감, 깨어날 때 상태 복구 |
| retention | — | 전원을 내려도 일부 플립플롭/SRAM 값은 유지 | suspend/resume 검증 |
| AVS | Adaptive Voltage Scaling | 칩마다·온도마다 필요한 최소 전압을 측정해 맞추는 폐루프 | 칩 편차·전력, 특성화 결과로 테이블 작성 |
| DVFS | Dynamic Voltage and Frequency Scaling | 부하에 따라 (전압, 주파수) 쌍(OPP)을 바꿈 | 전환 순간의 droop·hang |
| OPP | Operating Performance Point | 허용된 (주파수, 전압) 조합 하나 | DVFS 테이블의 행 |
| bench PSU | bench power supply | 전압·전류 제한을 설정할 수 있는 실험실 전원 | 첫 인가의 안전장치 |
| shunt | current-sense resistor | 아주 작은 저항, 양단 전압으로 전류 계산 | rail별 전류 측정 |
| BW limit | bandwidth limit | 스코프 입력을 20 MHz 등으로 저역통과 | ripple 측정 표준 조건 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Buck vs LDO — 숫자로

| 항목 | Buck (스위칭) | LDO (선형) |
|---|---|---|
| 원리 | 스위치 + 인덕터 + 커패시터, duty ≈ Vout/Vin | 패스 소자를 오차 증폭기가 조절, 차이 전압을 열로 |
| 효율 | 대략 85–95% (부하·전압비 의존) | ≈ Vout/Vin (예: 1.8→0.9 V = 50%) |
| 발열 | 낮음 | P_loss = (Vin − Vout) × I → 1 A면 0.9 W |
| 출력 노이즈 | 스위칭 주파수 ripple(수 mV–수십 mV) + 스위칭 edge spike | 매우 낮음, PSRR로 입력 노이즈 감쇠 |
| 전류 | 수 A–수십 A (multiphase로 확장) | 보통 수백 mA–수 A |
| 부하 transient | 제어 루프 대역폭·출력 C에 따라 droop | 빠르지만 Vin 여유(dropout) 필요 |
| 측정 포인트 | ripple, spike, droop, 스위칭 노드, 효율, 열 | DC 정확도, 노이즈, dropout 상황 |
| 보드에서 쓰는 곳 | core/NPU/DDR/IO 큰 rail | PLL, SerDes PHY, ADC, 작은 AO rail |

- **Multiphase buck**: 2–4개 위상을 시간차로 돌려 큰 전류를 나눠 담당 → ripple 상쇄·transient 개선. 위상 하나가 죽으면 "전압은 맞는데 큰 부하에서만 droop/과열" 증상 [추정 — 일반 원리].
- **자동차 PMIC의 스위칭 주파수**는 AM 라디오 대역(약 0.5–1.7 MHz)을 피하려고 2 MHz 이상으로 잡는 경우가 많다 [추정].

### 2.2 자동차 AI SoC rail 예시 표 [추정]

| rail (예시 이름) | 공칭 V | 공급원 | 전류 규모 | 특징 |
|---|---|---|---|---|
| VDD_AO | 0.8 V | PMIC LDO | 수십 mA | always-on, safety MCU, 제일 먼저 켜지고 제일 늦게 꺼짐 |
| VDD_CPU_NPU | 0.65–0.9 V (AVS) | multiphase buck | 수십 A | AVS/DVFS, droop 최대 위험, remote sense |
| VDD_CORE/SOC | 0.75–0.8 V | buck | 수 A | 인터커넥트·주변장치 |
| VDD_SRAM | 0.8–0.9 V | buck/LDO | 수 A | retention 동작 |
| VDDA_PLL | 0.8 / 1.8 V | LDO + 필터 | 수십 mA | 노이즈 → jitter (BV04) |
| VDDA_PHY | 0.85 V 등 | LDO | 수백 mA | PCIe/MIPI/Ethernet SerDes |
| VDD_DDR (LPDDR VDD2H/VDDQ) | 1.05 / 0.5 V | buck | 수 A | DDR training 실패 원인 후보 (BV09) |
| VDD_IO | 1.8 / 3.3 V | buck/load switch | 수백 mA | strap 샘플링·외부 칩 레벨 |

### 2.3 Sequencing — 왜, 무엇을, 어떻게

**왜 순서가 필요한가 (면접 답의 핵심 5개)**

1. **Back-powering / 기생 경로**: IO rail이 켜져 있고 core rail이 꺼져 있으면, IO 핀(또는 다른 칩이 driving하는 핀) → **ESD 보호 다이오드** → 꺼진 rail로 전류가 새서 rail이 0.5–1 V쯤 "반쯤" 올라간다. 칩 내부는 정의되지 않은 상태, POR이 애매하게 동작, 그 상태에서 rail이 켜지면 PMIC가 **pre-biased 출력**에 시동해야 한다.
2. **Latch-up**: IO 핀에 전원보다 높은 전압이 걸리면(꺼진 rail 기준) 기판으로 전류가 주입되어 기생 SCR(사이리스터)이 켜질 수 있다 → 전원-GND 단락처럼 큰 전류 → 손상.
3. **IO 상태 미정의 · glitch**: core가 아직 안 켜진 상태에서 IO만 켜지면 출력 핀이 아무 값이나 낼 수 있다 → 외부 flash에 쓰기 신호, PHY reset 핀 glitch, **strap 핀 오독**(BV05).
4. **Inrush 집중**: 모든 rail을 한꺼번에 켜면 커패시터 충전 전류가 겹쳐 입력 rail이 sag → PMIC UV 또는 PSU 전류 제한. FPGA 계열은 데이터시트가 "내부 core → aux → IO" 순서를 권장하는데 그 이유 중 하나가 inrush 제한이다 [확인됨][4].
5. **칩 내부 회로 가정**: 아날로그 PHY·PLL·SRAM은 특정 rail이 먼저 있어야 바이어스가 정상 → 데이터시트가 순서를 **요구**한다. 반대로 순서 요구가 없는 칩도 있다(예: TI 일부 DSP는 core/IO 간 순서 요구 없음, 다만 한쪽만 오래 켜 둔 상태는 피하라고 권고) [확인됨][6]. → **결론: 순서는 일반론이 아니라 그 칩 데이터시트가 정한다.**

**무엇을 정하나 (스펙 항목)**

| 항목 | 의미 | 흔한 형태 (값은 칩마다 다름) |
|---|---|---|
| order | rail 켜지는 순서 | "VDD_AO → VDD_CORE → … → VDD_IO" 또는 "같이 켜도 됨" |
| t_delay / gap | 앞 rail 기준점(90% 또는 PGOOD) → 다음 rail 시작 | ≥ 0, 수백 µs–수 ms |
| ramp time | 10%→90% 시간 | 데이터시트 min/max (너무 빨라도, 느려도 안 됨) |
| monotonic | 상승 중 감소 없음 | 대부분 필수 |
| max V difference | 두 rail 간 최대 차이 (예: IO가 core보다 X V 이상 높으면 안 됨) | 상승 중 순간값 포함 |
| PGOOD threshold | PGOOD가 켜지는 출력 비율 | 레귤레이터마다, 대략 90% 전후 [추정] |
| reset release | 마지막 rail 안정 → 리셋 해제까지 | ≥ 수 ms + 클럭 안정 (BV04/BV05) |
| power-down | 끌 때 순서·방전 | 보통 역순, active discharge |

**어떻게 구현하나 (보드 방식 4가지)**

```text
 (A) PGOOD daisy chain               (B) RC 지연 EN           (C) 전용 sequencer / supervisor   (D) PMIC 내부 시퀀서(NVM)
 REG1 ─PG─▶ EN REG2 ─PG─▶ EN REG3    VIN─R─┬─▶EN             LM3880 같은 칩이 EN1..3을          slot 0: BUCK3 on
                                          C                  정해진 지연으로 순서대로            slot 1: LDO1 on (+1 ms)
 + 단순, 앞이 안 뜨면 뒤도 안 뜸        GND                    올리고 역순으로 내림 [5]            slot 2: BUCK1/2 ...
 - 지연이 PG 타이밍에 좌우             + 싸다 - 온도·편차       + 정밀, 역순 power-down            + 유연, I2C로 변경, fault 시 정해진
                                                                                                   shutdown 시퀀스 [11]
```

- **(A)의 함정**: PGOOD는 open-drain이라 **풀업이 어느 rail에 달렸나**가 중요 — 풀업 전원이 아직 없으면 PGOOD가 "low(=not good)"로 보이고 체인이 멈춘다. 반대로 풀업이 빠지면 영원히 low.
- **Power-down**: 순서를 안 지키면 power-up과 같은 back-powering이 거꾸로 생긴다. 큰 출력 커패시터는 저절로 천천히 방전 → 다음 전원 인가 때 **pre-bias**(이미 0.3 V가 남아 있는 rail에 다시 시동) 문제 → PMIC의 **active discharge** 기능·"off time 최소값" 스펙. 빠른 off-on(차량 cold crank·리셋 버튼 연타)은 검증 항목.

### 2.4 Inrush 계산 — 면접에서 바로 쓰는 공식

- 커패시터 충전 전류: **I = C × dV/dt**.
- 예: VDD_CPU_NPU에 출력 커패시터 합 2,000 µF [추정], 0 → 0.75 V를 1 ms에 올리면 I = 2000e-6 × 0.75 / 1e-3 = **1.5 A** — 부하 전류와 별개로 커패시터만 채우는 데 필요.
- 같은 rail을 0.1 ms로 올리면 15 A → PMIC 전류 제한·상위 rail sag. 그래서 soft-start 시간이 있다.
- 입력 측(12 V)에서 보면 효율·전압비로 나눠서 줄어든다(P 보존: 0.75 V × 1.5 A ≈ 1.1 W → 12 V에서 0.1 A 수준 + 손실).

### 2.5 PMIC — 레지스터·OTP·fault·telemetry

- **OTP/NVM**: 기본 출력 전압, 켜는 순서·지연(slot), PGOOD에 어떤 rail을 묶을지, fault 시 동작(hiccup vs latch-off, 어느 rail까지 끄나), watchdog 기본값, I2C 주소. TPS6594-Q1처럼 **NVM CRC 검사로 오류면 power-up 시퀀스를 멈추는** 부품도 있다 [확인됨][11].
- **bring-up 첫 질문**: "이 보드의 PMIC 부품 번호·NVM ID/revision이 SoC + DDR 조합에 맞는 것인가?" (TPS6594-Q1은 orderable part별 NVM_ID/NVM_REV로 구분) [확인됨][11].
- **I2C/SPI 레지스터 종류 (일반형)**: 출력 전압 설정(VSET), enable, 모드(PWM 고정/자동 PFM), slew rate, 상태(현재 PGOOD), **fault/interrupt 레지스터(sticky — 읽거나 write-1-to-clear 하기 전까지 남음)**, mask, 온도, watchdog. 구체 주소·비트는 데이터시트마다 다르다 — 여기선 일반화.
- **fault 처리 해석 순서**: ① 어떤 rail에서 ② 무슨 fault가 ③ **먼저** 났나(첫 fault가 원인, 나머지는 결과일 수 있음 — 예: OC → 출력 꺼짐 → UV, PGOOD low) ④ PMIC가 그 뒤 무엇을 했나(전체 shutdown? 해당 rail만? 재시도?).
- **PMBus (서버·고전류 VR 표준, Linux hwmon `pmbus` 드라이버)** — 명령 코드는 커널 `pmbus.h` 기준 [확인됨][9]:

| 명령 | 코드 | 용도 |
|---|---|---|
| OPERATION | 0x01 | on/off, **margin high/low** 선택 (전압 margining → BV12) |
| CLEAR_FAULTS | 0x03 | sticky fault 지우기 |
| VOUT_MODE | 0x20 | 전압 데이터 형식·지수 (LINEAR16의 N) |
| VOUT_COMMAND | 0x21 | 출력 전압 설정 |
| VOUT_MARGIN_HIGH / LOW | 0x25 / 0x26 | margin 전압 값 |
| VOUT_OV_FAULT_LIMIT / VOUT_UV_FAULT_LIMIT | 0x40 / 0x44 | OV/UV fault 기준 |
| IOUT_OC_FAULT_LIMIT | 0x46 | 과전류 기준 |
| STATUS_WORD | 0x79 | 요약 상태 2바이트 |
| STATUS_VOUT / STATUS_IOUT / STATUS_TEMPERATURE | 0x7A / 0x7B / 0x7D | 세부 상태 |
| READ_VIN / READ_VOUT / READ_IOUT / READ_TEMPERATURE_1 | 0x88 / 0x8B / 0x8C / 0x8D | telemetry |

- STATUS_WORD 주요 비트 (커널 `pmbus.h`) [확인됨][9]: b1 CML(통신 오류), b2 TEMPERATURE, b3 VIN_UV, b4 IOUT_OC, b5 VOUT_OV, b6 OFF, b11 **POWER_GOOD#**(1 = PGOOD 아님), b13 INPUT, b14 IOUT/POUT, b15 VOUT. STATUS_VOUT: b4 UV_FAULT, b5 UV_WARNING, b6 OV_WARNING, b7 OV_FAULT.
- 데이터 형식: READ_VOUT은 **LINEAR16**(16비트 unsigned × 2^N, N은 VOUT_MODE의 5비트 2의 보수)으로 "측정값"(설정값 아님) [확인됨][10]. READ_IOUT·온도는 보통 **LINEAR11**(상위 5비트 지수 + 하위 11비트 가수). → §6 디코더.

### 2.6 SoC 쪽 전원 도메인 · AVS · DVFS

```text
  ┌──────────────────────────── SoC ────────────────────────────┐
  │ [AO domain: PMU/safety MCU, wake, RTC] ← 항상 켜짐           │
  │      │ 전원 스위치 제어 신호                                 │
  │ ┌────▼─────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │
  │ │ CPU cl0  │  │ NPU      │  │ ISP/VPU  │  │ PCIe/ETH │       │
  │ │ (island) │  │ (island) │  │ (island) │  │ PHY      │       │
  │ └──────────┘  └──────────┘  └──────────┘  └──────────┘       │
  │  각 island: power switch + isolation cell(꺼진 쪽 출력 고정)  │
  │             + retention flop/SRAM(선택) + 리셋·클럭 순서      │
  └──────────────────────────────────────────────────────────────┘
```

- **Power gating**: 칩 안 헤더/풋터 스위치로 블록 전원을 끊음. 켤 때 순서: 전원 스위치 on(조금씩 — 칩 내부 inrush) → 안정 → 클럭 → 리셋 해제 → isolation 해제. 이 순서가 틀리면 "블록 레지스터가 안 읽힘/버스 hang" (BV04의 clock+reset 함정과 같은 계열).
- **DVFS**: OPP(주파수, 전압) 테이블에서 고르기. **올릴 때는 전압 먼저 → 주파수, 내릴 때는 주파수 먼저 → 전압** (반대로 하면 잠깐 "높은 주파수 + 낮은 전압" = timing violation). PMIC slew rate·안정 대기 시간도 검증 항목.
- **AVS**: 같은 OPP라도 칩마다(공정 편차), 온도마다 필요한 최소 전압이 다르다. AVS는 칩 안 센서(링 오실레이터 등)로 측정해 전압을 조정하는 **폐루프**, DVS/DVFS의 고정 테이블은 개루프 [확인됨][13][12]. 자주 쓰는 변형: 공장에서 칩별로 측정해 eFuse에 "이 칩은 OPP_X에서 Y mV" 를 써 두는 **static AVS** [추정].
- **검증 관점**: ① 모든 OPP에서 기능·성능 테스트 ② OPP 전환을 반복(수만 회)하며 hang·droop 확인 ③ **전압 margining**: AVS가 정한 전압에서 −N mV까지 내려 fail 지점 찾기(shmoo, BV12) ④ 온도 코너 ⑤ PMIC slew·안정 시간 측정(스코프로 VDD_CPU와 주파수 변경 GPIO 마커 동시 캡처).

### 2.7 PDN · droop · IR drop (짧게)

- 부하 계단(예: NPU가 idle → full)에서 전압이 순간 내려가는 정도는 **PDN 임피던스(레귤레이터 루프 + 커패시터 + 기판 + 패키지)** 로 정해진다 → BV12 SI/PI.
- **IR drop**: 0.75 V rail에 30 A가 흐르고 경로 저항이 1 mΩ이면 30 mV(4%) 손실 → 그래서 **remote sense**(칩 근처 전압을 별도 선으로 레귤레이터에 피드백). 측정할 때도 **어디서 쟀나**(PMIC 출력 vs SoC 볼 근처 test point)를 같이 기록해야 한다.

## 3. 보드에서 볼 것 · 잴 것

### 3.1 Bring-up 전에 만들 power tree 표 (회로도 → 표)

| rail | 공칭 V | 허용 오차 | 순서 / EN 출처 | PGOOD 연결 | 측정 TP | rail-GND 저항 (golden) | 예상 전류 (idle / load) |
|---|---|---|---|---|---|---|---|
| VDD_AO | 0.80 | ±3% | 1 / VSYS | PG_AO → EN_CORE | TP101 | `[측정 채우기]` | `[숫자 채우기]` |
| VDD_CORE | 0.75 | ±3% | 2 / PG_AO | PG_CORE → EN_PHY | TP102 | | |
| VDDA_PHY | 0.85 | ±3% | 3 | | TP103 | | |
| VDD_IO_1V8 | 1.80 | ±5% | 4 | | TP104 | | |
| VDD_DDR | 1.05 | ±3% | 5 | | TP105 | | |
| VDD_IO_3V3 | 3.30 | ±5% | 6 | 모든 PG AND → RESET_N | TP106 | | |

> 이 표가 [§6 체커](onsitePrep/bv03/power_seq_check.py)의 `SPEC` 표와 같은 모양이다 — 보드 데이터시트·회로도에서 한 번 만들면 매 보드 자동 검사에 그대로 쓴다.

### 3.2 첫 전원 인가 절차 (전원 쪽만 — 전체는 BV06)

| 단계 | 할 일 | 장비 | 통과 기준 | 실패 시 다음 행동 |
|---|---|---|---|---|
| 0 | 육안·현미경: 레귤레이터/인덕터/PMIC 방향·누락·솔더 브리지 | 현미경 | 이상 없음 | rework |
| 1 | **전원 없이 rail-GND 저항** (양 극성으로 한 번씩) | DMM | golden 보드 값과 비슷, 0 Ω 근처 아님 | 0 Ω → 합선: 열화상(낮은 전류 주입), 0 Ω 저항/인덕터 빼서 구간 나누기 |
| 2 | 입력(VSYS)만, **낮은 전류 제한**(예상 idle의 1.5–2배쯤에서 시작) | bench PSU | CV 모드 유지, 전류 예상 범위 | CC(전류 제한) 모드 진입 → 즉시 끔 → 1단계로 |
| 3 | 각 rail DC 값 | DMM | 허용 오차 내 | 0 V: EN/PGOOD 체인·PMIC 상태 / 낮음: 과부하·피드백 저항 |
| 4 | **power-up 순서·ramp·overshoot** | scope 4–8ch | 스펙 표 PASS | §6 체커로 판정 |
| 5 | PGOOD·RESET_N 타이밍 | scope | 마지막 PG + 지연 후 reset 해제 | PG 풀업·EN 연결 확인 |
| 6 | PMIC fault/status 레지스터 덤프 | I2C 어댑터 / BMC | fault 0 | §3.7 해석 |
| 7 | 열화상 1분 | 열화상 카메라 | 핫스팟 없음 | 뜨거운 부품 = 단락·latch-up 후보 |
| 8 | ripple / droop | scope + 부하 | 스펙 mV p-p 이내 | §3.4/§3.5 |

- **rail-GND 저항의 함정**: 큰 SoC의 core rail은 die가 크고 누설이 커서 **정상 보드도 수 Ω 수준**일 수 있다 [추정] → "몇 Ω이면 정상"이란 절대값은 없고 **golden 보드·같은 리비전 보드와 비교**가 기준. DMM 극성을 바꾸면 ESD 다이오드 때문에 값이 달라지는 것도 정상.
- **전류 제한 설정**: 시작은 보수적으로(작은 보드 예: 50–100 mA) [확인됨][14], 보드가 정상이면 단계적으로 올림. **전압이 떨어지며 CC 모드로 바로 들어가면 즉시 차단** — 단락·부품 방향·비정상 부하 [확인됨][14]. 큰 AI SoC 보드는 idle도 수 A일 수 있으니 "예상 전류 표"가 먼저 필요하다 [추정].

### 3.3 Scope로 power-up sequence 잡는 법

```text
 채널 배치 예 (4ch scope로 6 rail + reset)                    트리거
  CH1: VDD_AO   (기준 rail, 첫 번째)                        → CH1 rising edge, 10% 레벨, single
  CH2: VDD_CORE                                              → 타임베이스: 전체 시퀀스가 화면 70%에 들어오게
  CH3: VDD_IO_3V3                                              (예: 2–5 ms/div, pre-trigger 10%)
  CH4: RESET_N
  두 번째 캡처: CH1 같은 기준 rail 유지 + CH2..4를 나머지 rail로 → 기준 rail로 두 캡처를 정렬
  대안: PGOOD들을 로직 애널라이저로 (디지털 순서만), 또는 8ch scope
```

- **측정 위치**: 레귤레이터 출력이 아니라 **부하(SoC) 쪽 test point** 를 기본으로, 차이가 있으면 둘 다.
- **프로브**: DC 시퀀스는 10× passive로 충분. 낮은 전압 rail은 V/div를 키워 분해능 확보(0.75 V rail을 1 V/div로 보면 10%/90%가 거의 안 보임).
- **판정 항목**: 순서(10% 기준), gap(앞 90% → 다음 10%), ramp(10→90%), monotonic, overshoot, 최종값, reset 해제 지연. → 손으로 커서 찍지 말고 **CSV로 export → §6 스크립트**로 자동 판정, 보드마다 같은 기준.
- **power-down도 한 번**: 입력을 끊고(또는 PMIC off 명령) 역순·방전 시간 확인. 재인가까지 최소 off 시간도 측정.

### 3.4 Ripple을 제대로 재는 법 ("200 mV vs 20 mV" 함정)

```text
 ❌ 잘못된 방법                                 ✅ 올바른 방법
  probe tip ─────── rail                       probe tip ──┐ (출력 커패시터 바로 위)
  15 cm ground lead (악어 클립) ── 먼 GND        ground spring ┘ (수 mm, 같은 커패시터의 GND 패드)
  → 리드가 만든 루프(안테나)가 스위칭             → 루프 면적 최소
    edge의 자기장을 주워 수백 mV spike로 보임     스코프 20 MHz BW limit, AC coupling, 작은 V/div
  스코프 full BW (500 MHz+)                       (가능하면 1× 프로브 또는 전용 PI 프로브로 노이즈 플로어↓)
```

- 긴 ground lead가 만드는 루프가 고주파 spike를 "주워" 보이게 하며, 이건 레귤레이터 출력이 아니라 측정 아티팩트다 [확인됨][1]. 20 MHz 대역 제한을 걸면 스코프가 저역통과 필터 역할을 해 spike가 크게 줄어든다 [확인됨][2]. 데이터시트의 ripple 측정 조건도 흔히 **AC coupling + 20 MHz BW limit** [확인됨][2]. ground lead ≤ 약 10 mm, 대역 제한은 스코프 설정으로 [확인됨][3].
- **무엇을 보고하나**: 스위칭 주파수 ripple(mV p-p), spike(별도로, full BW에서), 측정 조건(BW, 프로브, 위치, 부하 전류). 조건 없이 "ripple 50 mV"만 말하면 의미 없음.
- **10× 프로브 노이즈 플로어**: 10× 프로브는 신호를 1/10로 줄여 스코프 자체 노이즈가 상대적으로 커진다 → 수 mV ripple은 1× 프로브나 전용 power-rail 프로브가 유리 [추정 — 일반 계측 원리, BV11].

### 3.5 Droop / load transient

- **방법**: 전자 부하(electronic load)로 rail에 전류 계단(예: 10% → 90% 부하, 빠른 slew)을 주거나, SoC에서 **부하 생성 FW**(NPU/CPU 전 코어 busy loop ↔ WFI 반복, GPIO로 마커)를 돌린다. 스코프 트리거는 GPIO 마커.
- **판정**: 최저 전압이 칩 최소 동작 전압 + 마진 이상인가, 복구 시간, ringing. AVS가 낮춘 전압일수록 droop 여유가 적다.
- **"부하일 때만 10% 낮다" 가설 순서**: ① 레귤레이터 전류 한계/전류 제한 동작(PMIC OC warning) ② multiphase 위상 하나 죽음 ③ 피드백 저항 값/ remote sense 미연결 ④ IR drop(PMIC 출력은 정상, SoC 쪽만 낮음 → 두 지점 동시 측정) ⑤ 입력 rail sag(상위 레귤레이터·PSU 리드 저항) ⑥ 출력 커패시터 누락/잘못된 값.

### 3.6 전류 측정

| 방법 | 장점 | 단점 |
|---|---|---|
| bench PSU readback | 쉬움, 입력 총전류 | rail별 구분 불가, 느림 |
| shunt 저항 + DMM / 전력 분석기 | rail별, 정확 | 검증 보드에 shunt가 있어야 (BV02: "shunt 넣어 달라" 요청) |
| 전류 프로브 (hall) | 비접촉, 파형 | 작은 전류 정확도·DC offset |
| PMIC/PMBus telemetry | 자동화·전 rail·실사용 상태 | 정확도·업데이트 속도 확인 필요, 교정 |

### 3.7 PMIC fault 레지스터 덤프 해석 예

[§6 디코더](onsitePrep/bv03/pmbus_decode.py) 실제 실행 출력 (레지스터 값 자체는 **[예시 출력]** — 지어낸 값):

```text
== VDD_DDR_1V05 (tripped) ==
  STATUS_WORD 0x8850: b4:IOUT_OC, b6:OFF (output is off), b11:POWER_GOOD# (PGOOD is NOT good), b15:VOUT (see STATUS_VOUT)
  STATUS_VOUT 0x10  : b4:VOUT_UV_FAULT
  STATUS_IOUT 0x80  : b7:IOUT_OC_FAULT
  READ_VOUT 0x0000 -> 0.0000 V
  READ_TEMP 0xE9F0 -> 62.00 C
```

- 줄별 해석: **IOUT_OC_FAULT**(과전류)가 원인 후보 1번 → 레귤레이터가 출력을 끔(**OFF**) → 그래서 출력 전압이 떨어져 **UV_FAULT**·**POWER_GOOD#** 가 뒤따름. "UV가 났다"만 보고 레귤레이터 탓을 하면 틀린다 — **OC가 먼저**, 즉 DDR rail의 부하(단락·DRAM 실장 불량·training 중 과도 전류) 쪽을 의심. 온도 62 °C → OT는 아님.
- 다음 행동: fault 레지스터를 **CLEAR_FAULTS 전에 저장**(로그) → 전류 제한 걸고 해당 rail만 다시 켜며 전류 파형 측정 → DRAM 쪽 rail-GND 저항 → 보드 간 비교.
- 비-PMBus PMIC(I2C 레지스터형)도 개념은 같다: "어느 rail · 어떤 fault · 첫 번째는 무엇 · 그 후 PMIC의 동작". 많은 PMIC가 **마지막 shutdown 원인**을 저장하는 레지스터를 갖는다 → 재부팅 루프 디버그 때 SoC reset reason(BV05)과 같이 본다 [추정 — 부품마다 다름].

### 3.8 Linux에서 보는 전원 (짧게)

- **regulator framework**: consumer(드라이버가 `regulator_get/enable/set_voltage`), regulator driver(PMIC 드라이버가 등록), machine/constraints(DT에서 보드별 min/max — 버그 있는 드라이버가 과전압을 못 걸게) 3층 구조 [확인됨][7]. sysfs는 **모니터링용**(제어는 안 됨).
- `/sys/class/regulator/regulator.N/` 속성: `name`, `state`, `microvolts`(**설정값**, 실측 아님), `num_users`, `type`, `min_microvolts/max_microvolts`, fault 플래그 `under_voltage`, `over_current`, `over_temp`, `regulation_out`, `fail` 등 [확인됨][8]. 부품이 지원하는 속성만 생긴다.
- Lima VM(Ubuntu aarch64, 실제 PMIC 없음)에서 **실제로 실행한 출력** — dummy regulator 하나만 있는 것 자체가 "가상 머신엔 PMIC가 없다"는 증거:

```text
$ ls /sys/class/regulator/regulator.0
device  name  num_users  power  requested_microamps  subsystem
suspend_disk_state  suspend_mem_state  suspend_standby_state  type  uevent
$ cat name num_users type
regulator-dummy
1
voltage
$ sudo cat /sys/kernel/debug/regulator/regulator_summary
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV
```

- 실제 보드에서는 `regulator_summary`가 **트리 모양으로 PMIC rail → consumer 장치**를 보여 준다 → "이 rail을 누가 켜고 있나", "전압이 DT 제약 안인가" 확인. `state`/`microvolts`는 커널이 **설정한** 값이므로 실제 전압은 DMM/telemetry로 따로 확인.
- **hwmon** (`/sys/class/hwmon/hwmonN/`): PMBus/전원 모니터(INA2xx 등) 드라이버가 `in*_input`(mV), `curr*_input`(mA), `power*_input`(µW), `temp*_input`(m°C), `*_alarm` 을 노출 [확인됨][16]. 자동화 스크립트가 전 rail 전압·전류를 주기적으로 읽는 표준 경로.
- I2C로 직접 볼 때 **[예시 출력]** (보드 없음 — 형식만):

```text
$ i2cdetect -y 2                 # PMIC가 I2C 버스 2의 0x48에 있다고 가정
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
40: -- -- -- -- -- -- -- -- 48 -- -- -- -- -- -- --
$ i2cget -y 2 0x48 0x79 w         # PMBus 장치라면 STATUS_WORD (SMBus word, little-endian)
0x0000
```

- 주의: 커널 PMIC 드라이버가 이미 붙어 있는 주소는 i2c-dev 접근 시 `Device or resource busy` → `-f`는 드라이버 상태와 충돌하므로 bring-up 때만, 읽기 위주로.

## 4. 꼬리 질문 체인

### 체인 1 — "Walk me through powering on a new board for the first time."

Q → 꼬리1 "Why current-limited?" → 꼬리2 "Current is 3× higher than expected. What next?" → 꼬리3 "One rail is 10% low only under load — what is it?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Before applying power I'd already have a power-tree table from the schematic: every rail, nominal voltage, tolerance, sequence position, where to probe, and expected current. First a visual inspection, then I measure each rail's resistance to ground unpowered and compare with a known-good board, which catches most shorts and wrong-orientation parts. Then I power the input from a bench supply with a conservative current limit, check that it stays in constant-voltage mode, measure each rail with a DMM, capture the power-up sequence on a scope, read the PMIC status registers, and take a quick thermal image. Only after power is clean do I look at clocks and reset."
  - 한국어 핵심: 표 준비 → 육안 → rail-GND 저항(golden 비교) → 전류 제한 인가 → DMM → scope 시퀀스 → PMIC 레지스터 → 열화상 → 그다음 클럭.
- **꼬리1. Why current-limited?** — "Because a short or a reversed part on a new board can draw amps and burn a trace, the regulator, or the only A0 silicon we have. With the limit set just above the expected current, a fault makes the supply drop into constant-current mode and the voltage collapses instead of something melting. It also makes the problem visible: the supply's current reading is my first measurement. I raise the limit in steps as each stage proves out."
  - 한국어 핵심: 단락 시 손상 방지(특히 귀한 A0 칩), CC 모드 진입 = 경보, 단계적으로 올림.
- **꼬리2. Current is 3× higher than expected.** — "I'd stop and localize before guessing. A thermal camera with the limited current flowing usually points straight at the part that's dissipating — a shorted capacitor, a regulator, or the SoC itself if it's latch-up. If that's not conclusive, I split the power tree: disable regulators through their enable pins or remove the zero-ohm links or inductors, and bring rails up one at a time while watching the current. I'd also check whether it's the input stage itself, and compare with a second board to see if it's this board or the design."
  - 한국어 핵심: 열화상 → rail 하나씩 분리(EN, 0 Ω/인덕터 제거) → 보드 간 비교. latch-up이면 SoC 자체가 뜨거움.
- **꼬리3. One rail is 10% low only under load.** — "That's a load-dependent drop, so it's either the regulator not keeping up or resistance in the path. I'd measure at the regulator output and at the SoC side at the same time: if both drop, it's the regulator — current limit, a dead phase on a multiphase buck, a wrong feedback divider, or its input sagging. If only the SoC side drops, it's IR drop in the copper or a missing remote-sense connection. I'd check the PMIC's current telemetry and OC warning flags too, and with a scope triggered on a load-step marker I'd see whether it's a steady drop or a transient droop."
  - 한국어 핵심: 두 지점 동시 측정 → 둘 다 낮으면 레귤레이터(전류 한계·위상·피드백·입력), SoC 쪽만이면 IR drop/remote sense. 정적 vs transient 구분.

</details>

### 체인 2 — "Why does power sequencing matter?"

Q → 꼬리1 "What can happen if IO comes up before core?" → 꼬리2 "How would you verify sequencing on every board automatically?" → 꼬리3 "What about power-down?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Because the chip's internal circuits assume certain supplies exist before others. If the order is wrong you can get current flowing through ESD diodes into an unpowered rail, IO pins in undefined states driving other chips, latch-up in the worst case, and inrush stacking up so the input sags. The datasheet specifies the order, the delays and ramp limits, and the board guarantees it with PGOOD-to-enable chains, a sequencer, or the PMIC's programmed sequence — and reset is held until the last rail is good."
  - 한국어 핵심: 내부 회로 가정, ESD 다이오드 경로, IO 미정의, latch-up, inrush. 순서는 데이터시트가 정하고 보드(PGOOD 체인/PMIC)가 보장.
- **꼬리1. IO before core?** — "The IO ring is powered but the core logic that controls it isn't, so the output drivers can come up in a random state — glitching a flash write-enable or a PHY reset, for example. Any external chip driving into the SoC's pins can back-power the unpowered core rail through the protection diodes, so the core sits at some half voltage and the internal power-on reset may not behave. Strap pins may be sampled wrong. And injecting current into a chip whose supply is lower than the pin voltage is exactly the condition that can trigger latch-up."
  - 한국어 핵심: IO 출력 glitch, ESD 다이오드로 core back-power(반쯤 켜짐), POR 오동작, strap 오독, latch-up.
- **꼬리2. Verify on every board automatically?** — "I'd capture the rails on a multi-channel scope or a DAQ, triggered on the first rail, export the waveforms, and run a script that finds the 10% and 90% crossings, checks the order, the gaps, ramp times, monotonicity, overshoot, final value, and the reset delay against a spec table taken from the datasheet. It runs on every board in the farm with SCPI control of the scope, stores the numbers, not just pass or fail, so I can see the distribution across boards and temperature. I actually wrote that checker for this prep — it flags an IO rail coming up second, a 13% overshoot and a reset that's too early." (§6)
  - 한국어 핵심: scope/DAQ 캡처 → 스크립트로 10/90% 교차·순서·간격·ramp·monotonic·overshoot·reset 판정 → 숫자 저장, 보드·온도 분포.
- **꼬리3. Power-down?** — "Usually the reverse order, and it's easy to forget to test. Reset should assert first, then rails go down in reverse, and large output capacitors may discharge slowly, so a rail can still be partly charged when the next power-up starts — that's the pre-bias case. So I test fast off-on cycles, check active discharge, and measure the minimum off time. In a car that matters because of cranking dips and quick key cycles."
  - 한국어 핵심: 역순, reset 먼저, 느린 방전 → pre-bias, 빠른 off-on 반복 테스트(자동차 cranking).

</details>

### 체인 3 — "How do you measure ripple correctly?"

Q → 꼬리1 "Why does your measurement show 200 mV of noise but the HW engineer sees 20 mV?" → 꼬리2 "What bandwidth would you use and why?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "Right at the output capacitor or the load's test point, with the shortest possible ground — a ground spring on the capacitor's ground pad, not the long alligator lead. Scope on AC coupling, small volts per division, and the 20 MHz bandwidth limit, which is the usual datasheet condition. I'd report the ripple at the switching frequency separately from the switching spikes, and always state the conditions: bandwidth, probe, location, and load current."
  - 한국어 핵심: 출력 커패시터 바로 위, ground spring, AC coupling, 20 MHz BW limit, ripple과 spike 분리, 조건 같이 보고.
- **꼬리1. 200 mV vs 20 mV?** — "Almost certainly the measurement. A long ground lead forms a loop that picks up the magnetic field from the switching edges, and with full scope bandwidth you see that as big spikes that aren't really on the rail. The other usual suspects are probing far from the capacitor so you include trace inductance, and the 10× probe's noise floor. I'd redo it with a spring ground and bandwidth limit, and if we still disagree, compare at the same location and load with the same probe."
  - 한국어 핵심: 긴 ground lead 루프가 스위칭 자기장을 주움 + full BW. 위치·프로브 노이즈도. spring + BW limit으로 다시.
- **꼬리2. Which bandwidth?** — "20 MHz for the ripple number because that's how most regulator datasheets specify it, so it's comparable, and it removes pickup that the load wouldn't see anyway. But I'd also look at full bandwidth once, carefully probed, because fast spikes can matter for a sensitive analog or PLL rail. So two numbers: ripple at 20 MHz, and spikes at full bandwidth with a good probe."
  - 한국어 핵심: 20 MHz = 데이터시트와 비교 가능. full BW로 spike도 한 번 (PLL/아날로그 rail엔 중요).

</details>

### 체인 4 — "What's AVS/DVFS and how would you validate it?"

Q → 꼬리1 "Which order do you change voltage and frequency?" → 꼬리2 "How do you find how much margin the AVS voltage has?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "DVFS switches between operating points — frequency and voltage pairs — based on load. AVS goes further: because every die and temperature needs a different minimum voltage, it uses on-chip monitors or factory-fused values to set the voltage per chip, closed-loop. To validate, I run functional and performance tests at every operating point, stress the transitions themselves thousands of times, measure the PMIC's slew and settling on a scope against a GPIO marker, and do voltage margining below the AVS target to see how much margin there is across temperature and across chips from different process corners."
  - 한국어 핵심: DVFS = OPP 전환, AVS = 칩·온도별 최소 전압(폐루프/eFuse). 모든 OPP 기능·성능, 전환 반복 스트레스, slew 측정, margining × 온도 × corner.
- **꼬리1. Order?** — "Going up, raise the voltage first, wait for it to settle, then raise the frequency. Going down, lower the frequency first, then the voltage. Otherwise there's a window where the logic runs at a high frequency with too little voltage, which shows up as rare, random failures right at the transition — a classic hard-to-debug hang."
  - 한국어 핵심: 올릴 때 V → f, 내릴 때 f → V. 반대면 전환 순간 timing violation → 드문 랜덤 hang.
- **꼬리2. Margin?** — "Step the rail down from the AVS value with the PMIC or PMBus margin commands while running a stressful workload, until it fails — that's a one-dimensional shmoo. Do it at hot and cold and on several chips, ideally including slow-corner parts. The difference between the fail point and the operating voltage, minus droop measured under the worst load step, is the real margin. Then it goes into the characterization report."
  - 한국어 핵심: PMIC/PMBus margin으로 단계 하강 + 스트레스 → fail 지점, 온도·칩 corner별, droop 빼고 실제 마진 → BV12.

</details>

### 체인 5 (추가) — "The board shuts off under heavy load. How do you debug it?"

Q → 꼬리1 "The PMIC fault register says UV on the DDR rail. Is it the regulator?" → 꼬리2 "How do you catch it if it happens once a day?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "First I'd read and save the PMIC fault and last-shutdown registers before clearing anything, and the SoC's reset reason, so I know whether the PMIC cut power or the SoC reset itself. If the PMIC tripped, the register tells me which rail and which fault: over-current, under-voltage, over-temperature, or an input under-voltage. Then I reproduce with a scope on that rail, the input rail and a load marker GPIO, triggered on the PGOOD falling edge, so I see what happened just before."
  - 한국어 핵심: 지우기 전에 PMIC fault + SoC reset reason 저장 → 어느 rail·어떤 fault → PGOOD falling 트리거로 직전 파형.
- **꼬리1. UV on DDR — the regulator?** — "Not necessarily — UV is often a consequence. I'd look at what came first: if there's also an over-current fault, the regulator hit its limit and shut off, and the voltage dropped as a result, so the question becomes why the load drew that much. If it's UV without OC, then it's droop: the load step was faster than the loop can handle, or the input sagged. The order of the fault bits and the scope capture separate those."
  - 한국어 핵심: UV는 결과일 수 있음. OC 동반 → 부하가 원인. OC 없이 UV → transient droop/입력 sag.
- **꼬리2. Once a day?** — "Make it self-capturing: scope in normal-trigger mode on PGOOD falling, with long memory and the rails on the other channels, and a script that logs PMIC telemetry and fault registers periodically and right after every reboot. Then accelerate it — higher temperature, lower margin voltage, a load pattern that toggles the NPU — to turn once a day into once an hour."
  - 한국어 핵심: PGOOD falling 트리거 대기 + 주기적 telemetry/fault 로그 → 온도·마진·부하 패턴으로 가속.

</details>

### 체인 6 (추가) — "How do you validate the PMIC configuration itself?"

Q → 꼬리1 "What if the OTP on the PMIC is wrong for this board?"

<details><summary>모범 답 펼치기</summary>

- **Q.** — "I'd read back its identity and configuration first — part number, NVM or OTP revision — and diff every voltage, sequence slot, delay, PGOOD mapping and fault-response setting against the board's power spec. Then I measure: each rail's voltage at nominal and at the margin settings, the actual sequence timing versus what's programmed, and I trigger faults deliberately on a test board — overload with an electronic load, pull a rail low — to confirm the PMIC detects them and does the shutdown it's supposed to, and that the interrupt reaches the SoC."
  - 한국어 핵심: 부품·NVM revision 확인 → 설정 전부 스펙과 diff → 실측(전압·시퀀스) → 고의 fault 주입(과부하·rail 당김)으로 감지·shutdown·인터럽트 확인.
- **꼬리1. OTP wrong?** — "For bring-up I can often override the registers over I2C after power-up, or hold the regulator off and use an external supply for that rail, to keep working. But the real fix is the right orderable part or reprogramming the NVM if the part supports it — and adding a check to the board test that reads the NVM revision, so the wrong part never gets past incoming test again."
  - 한국어 핵심: 임시: I2C로 레지스터 override, 외부 전원 대체. 근본: 올바른 부품/NVM 재프로그램 + 보드 테스트에 NVM revision 검사 추가.

</details>

## 5. Don 경험 연결

- "At Solidigm, power-loss behavior was a core validation area for SSD controllers — we exercised power-loss protection by cutting power at controlled points and checking that the hold-up energy was enough to flush data, which is the same mindset as power-down sequencing and droop on a board. I measured `[숫자 채우기: 측정한 rail/전류/hold-up 시간]` with a power analyzer."
- "At SK hynix I automated chamber reliability tests, so I'm used to running the same measurement across many boards and temperatures and storing the numbers, not just pass/fail — that's how I'd treat power-sequence checks across a board farm."
- "At Apple I root-caused interface issues with a scope and protocol analyzer; one lesson that transfers directly is to distrust a noisy-looking measurement until the probing is right — short ground, right bandwidth, right location `[숫자 채우기: 구체 사례 1개]`."
- 정직하게: PMIC 회로 설계는 Don의 경력이 아님 → "I'm not the board designer, but I'm the person who proves the power tree works on every board and turns the datasheet's sequencing table into an automated check." 라고 포지셔닝.

## 6. 실습 / 코드

### 6.1 Power-up sequence checker — [power_seq_check.py](onsitePrep/bv03/power_seq_check.py)

- 하는 일: mock scope export CSV(100 kS/s, 25 ms, 6 rail + RESET_N, 0.2% rms 노이즈)를 만들고 → 다시 읽어서 각 rail의 **10%/90% 교차 시각(샘플 간 선형 보간)**, ramp, 앞 rail 90% → 다음 rail 10% gap, monotonic(상승 중 최대 dip), overshoot, 최종값(마지막 1 ms 평균), 순서, RESET_N 해제 지연을 `SPEC` 표와 비교 → PASS/FAIL. 실제 보드 캡처는 `python3 -I power_seq_check.py check board.csv`.
- 고장 보드 mock에 넣은 결함: ① 3V3 IO의 EN이 잘못된 PGOOD에 묶여 **2번째로** 켜짐 ② DDR rail **14% overshoot**(보상·출력 C 문제 가정) ③ PHY rail이 ramp 중 **9% sag**(상위 rail inrush 가정) ④ RESET_N이 마지막 rail 후 **0.6 ms** 만에 해제.
- 실행: `python3 -I onsitePrep/bv03/power_seq_check.py` → 종료 코드 1(하나라도 FAIL). **실제 출력 그대로**:

```text
=== mock_good.csv  (2500 samples, 0.010 ms/sample) ===
  rail            Vnom  t10ms  t90ms  ramp    gap  final  ovs%  dip%
  VDD_AO_0V8      0.80   1.10   1.90  0.80      -  0.800   0.9   0.0
  VDD_CORE_0V75   0.75   2.62   3.58  0.96   0.72  0.750   1.1   0.0
  VDDA_PHY_0V85   0.85   4.40   5.20  0.80   0.82  0.850   1.0   0.0
  VDD_IO_1V8      1.80   6.15   7.35  1.19   0.95  1.800   1.0   0.1
  VDD_DDR_1V05    1.05   8.30   9.10  0.80   0.95  1.050   1.1   0.0
  VDD_IO_3V3      3.30  10.10  11.70  1.61   1.00  3.299   3.0   0.4
  order seen : AO_0V8 > CORE_0V75 > PHY_0V85 > IO_1V8 > DDR_1V05 > IO_3V3
  RESET_N 50%:  14.53 ms  (delay after last rail 90% =   2.83 ms)
  VDD_AO_0V8     ..//==============================================
  VDD_CORE_0V75  .....//===========================================
  VDDA_PHY_0V85  ........./========================================
  VDD_IO_1V8     ............///===================================
  VDD_DDR_1V05   ................./================================
  VDD_IO_3V3     ....................///===========================
  RESET_N        .............................|^^^^^^^^^^^^^^^^^^^^
                 0 ms                                         25 ms
  RESULT: PASS

=== mock_bad.csv  (2500 samples, 0.010 ms/sample) ===
  rail            Vnom  t10ms  t90ms  ramp    gap  final  ovs%  dip%
  VDD_AO_0V8      0.80   1.10   1.90  0.80      -  0.800   1.0   0.0
  VDD_CORE_0V75   0.75   4.72   5.68  0.96   2.82  0.750   1.1   0.0
  VDDA_PHY_0V85   0.85   6.50   7.55  1.05   0.82  0.850   1.3   9.2
  VDD_IO_1V8      1.80   8.25   9.45  1.20   0.70  1.800   1.2   0.0
  VDD_DDR_1V05    1.05  10.36  10.84  0.48   0.91  1.049  13.0   0.0
  VDD_IO_3V3      3.30   2.60   4.20  1.59  -8.24  3.300   3.3   0.3
  order seen : AO_0V8 > IO_3V3 > CORE_0V75 > PHY_0V85 > IO_1V8 > DDR_1V05
  RESET_N 50%:  11.53 ms  (delay after last rail 90% =   0.69 ms)
  VDD_AO_0V8     ..//==============================================
  VDD_IO_3V3     .....///==========================================
  VDD_CORE_0V75  .........//=======================================
  VDDA_PHY_0V85  .............//===================================
  VDD_IO_1V8     .................//===============================
  VDD_DDR_1V05   ...................../============================
  RESET_N        .......................|^^^^^^^^^^^^^^^^^^^^^^^^^^
                 0 ms                                         25 ms
  FAIL  VDDA_PHY_0V85: non-monotonic ramp, dip 9.2% of Vnom
  FAIL  VDD_DDR_1V05: overshoot 13.0% > 5.0% (peak 1.186 V)
  FAIL  VDD_IO_3V3: gap from VDD_DDR_1V05 90% = -8.24 ms outside [0.1, 5.0] ms
  FAIL  ORDER: position 2 is VDD_IO_3V3, spec says VDD_CORE_0V75
  FAIL  RESET_N released 0.69 ms after last rail 90% (< 2.0 ms)
  RESULT: FAIL (5 violations)

summary: 1/2 captures PASS
```

- 출력 읽는 법:
  - `t10ms/t90ms` = 10%/90% 교차 시각, `ramp` = 그 차이, `gap` = **스펙상 앞 rail**의 90% → 이 rail 10% (3V3의 −8.24 ms = DDR보다 8 ms **먼저** 켜졌다는 뜻 → ORDER 위반과 같은 사건을 두 각도에서 잡음).
  - DDR overshoot은 mock에서 14%로 넣었는데 13.0%로 측정됨 → **샘플링(10 µs)이 ring 피크를 놓친 것**. 실제 측정에서도 overshoot은 샘플 속도·BW에 따라 과소평가된다 → overshoot 판정은 충분히 빠른 샘플로.
  - PHY의 `dip% 9.2` → monotonic 위반. 10%/90% 시각만 보면 정상처럼 보이는(ramp 1.05 ms, 스펙 안) 결함이라 **ramp 시간만 검사하면 놓친다**.
  - 타임라인: `/` = 10–90% 구간, `=` = 90% 이상, `|` = RESET_N 해제. 고장 보드는 3V3가 두 번째 줄로 올라옴.
- 확장 아이디어 `[미실행]`: power-down 캡처 검사(역순·방전 시간), 두 rail 간 순간 전압차(예: IO − core ≤ X V) 검사, PyVISA로 scope에서 직접 waveform 받기(BV11), 결과를 JSON으로 보드 DB에 저장(BV14).

### 6.2 PMBus status/telemetry 디코더 — [pmbus_decode.py](onsitePrep/bv03/pmbus_decode.py)

- 비트 이름은 커널 `pmbus.h`, LINEAR11/LINEAR16 변환 구현. 레지스터 값은 **[예시 출력]** 용 지어낸 값, 디코드 결과는 실제 실행 출력:

```text
== VDD_CORE_0V75 (healthy) ==
  STATUS_WORD 0x0000: (none)
  STATUS_VOUT 0x00  : (none)
  STATUS_IOUT 0x00  : (none)
  VOUT_MODE 0x14 -> LINEAR16 exponent N=-12
  READ_VOUT 0x0C00 -> 0.7500 V
  READ_IOUT 0xDA4C -> 18.375 A
  READ_TEMP 0xE9B8 -> 55.00 C
== VDD_DDR_1V05 (tripped) ==
  STATUS_WORD 0x8850: b4:IOUT_OC, b6:OFF (output is off), b11:POWER_GOOD# (PGOOD is NOT good), b15:VOUT (see STATUS_VOUT)
  STATUS_VOUT 0x10  : b4:VOUT_UV_FAULT
  STATUS_IOUT 0x80  : b7:IOUT_OC_FAULT
  VOUT_MODE 0x14 -> LINEAR16 exponent N=-12
  READ_VOUT 0x0000 -> 0.0000 V
  READ_IOUT 0x0000 -> 0.000 A
  READ_TEMP 0xE9F0 -> 62.00 C
```

- 계산 확인: VOUT_MODE 0x14 → 하위 5비트 0b10100 = −12 → 0x0C00 = 3072 × 2^−12 = 0.75 V. READ_IOUT 0xDA4C → 지수 0b11011 = −5, 가수 0x24C = 588 → 588/32 = 18.375 A.

## 7. 자주 틀리는 것

1. **"순서는 항상 core → IO"라고 단정** — 칩마다 다르고, 순서 요구가 없는 칩도 있다. 답은 "데이터시트가 정한 순서, 보드가 보장, 나는 측정으로 증명".
2. **PMIC 출력 핀에서 재고 "정상"** — 부하가 큰 rail은 SoC 쪽에서 IR drop이 있다. 측정 위치를 같이 말할 것.
3. **ripple을 긴 ground lead + full BW로 측정** — 수백 mV "노이즈"는 대부분 측정 아티팩트. spring ground + 20 MHz.
4. **ramp 시간만 보고 monotonic을 안 봄** — 10/90% 시각은 정상인데 중간에 sag가 있는 결함을 놓침(§6 PHY rail).
5. **UV fault만 보고 레귤레이터 탓** — OC가 먼저면 부하가 원인. 첫 fault와 결과 fault를 구분.
6. **fault 레지스터를 지우거나 전원을 다시 넣은 뒤 읽음** — sticky 정보가 날아감. 먼저 저장, 그다음 clear.
7. **rail-GND 저항의 절대값으로 판정** — 큰 SoC core rail은 정상도 낮다. golden 보드 비교·극성 두 번.
8. **`/sys/class/regulator/*/microvolts`를 실제 전압으로 믿음** — 커널이 설정한 값이다. 실측은 DMM/telemetry.
9. **DVFS 순서 반대**(주파수 먼저 올리고 전압 나중) — 전환 순간의 드문 hang.
10. **power-down·빠른 off-on을 테스트하지 않음** — pre-bias·잔류 전압 때문에 두 번째 부팅만 실패하는 보드가 있다(BV13 "cold boot OK, warm/quick power cycle 실패").

## 8. 출처

1. Analog Devices, Analog Dialogue — "Understanding Switching Regulator Output Artifacts Expedites Power Supply Design" (ground lead loop pickup, 20 MHz vs full BW): https://www.analog.com/en/resources/analog-dialogue/articles/understanding-switching-reg-output-artifacts.html (요약: https://www.allaboutcircuits.com/industry-articles/switching-regulator-output-artifacts-expedites-power-supply-design/)
2. TI SLVAF30 — ripple/spike measurement and bandwidth limit: https://ti.com/lit/pdf/slvaf30 · TI TPS53126 user guide SLVU435A (AC coupling + 20 MHz BW for ripple test): https://ti.com/document-viewer/lit/html/SLVU435A/GUID-14BA1164-260E-4E27-99B2-8D22EAB8E0C5
3. Traco Power — Ripple & noise measurement (ground lead ≤ ~10 mm, BW limit in scope): https://www.tracopower.com/info/ripple-noise.pdf
4. Analog Devices, Analog Dialogue — "Complex Power-Supply Sequencing Made Easy" (FPGA VCCINT → VCCAUX → VCCO, inrush, datasheet first): https://www.analog.com/en/resources/analog-dialogue/articles/complex-power-supply-sequencing
5. TI SSZT944 — "Sequencing Solutions: Simple, Reliable and Cost-Effective" (LM3880/LM3881 sequencers): https://ti.com/lit/pdf/sszt944
6. TI SLVA073 — "Power Supply Sequencing Solutions for Dual Supply Voltage DSPs": https://ti.com/lit/pdf/slva073
7. Linux kernel docs — Regulator framework overview: https://www.kernel.org/doc/html/latest/power/regulator/overview.html
8. Linux kernel ABI — sysfs-class-regulator: https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-class-regulator
9. Linux kernel — drivers/hwmon/pmbus/pmbus.h (command codes, STATUS bit definitions): https://raw.githubusercontent.com/torvalds/linux/master/drivers/hwmon/pmbus/pmbus.h
10. AMD AM006 Versal SysMon — PMBus transfer commands (READ_VOUT LINEAR16, STATUS_WORD/VOUT): https://docs.amd.com/r/en-US/am006-versal-sysmon/PMBus-Transfer-Commands
11. TI SLVUC32B — Dual TPS6594-Q1 PMIC user guide for TDA4VM/DRA829 (NVM sequences, NVM_ID, CRC, ASIL-D): https://www.ti.com/document-viewer/lit/html/SLVUC32B/GUID-43F9C691-0EFA-4208-856D-635BF7294065 · TPS6594-Q1 product page: https://ti.com/product/TPS6594-Q1
12. TI SLVA646 — DVFS/AVS, OPP application report: https://ti.com/lit/pdf/slva646
13. Wikipedia — Adaptive voltage scaling (closed-loop vs open-loop DVS): https://en.wikipedia.org/wiki/Adaptive_voltage_scaling
14. JLCPCB — PCBA first power-on troubleshooting (resistance to ground, 50–100 mA limit, CC mode → disconnect): https://jlcpcb.com/help/article/pcba-first-power-on-troubleshooting
15. Altium — Expert tips for safely powering your new PCB the first time: https://resources.altium.com/p/expert-tips-safely-powering-your-new-pcb-first-time
16. Linux kernel ABI — sysfs-class-hwmon (inY_input millivolt, currY_input milliampere, powerY_input microWatt, tempY_input millidegree Celsius): https://raw.githubusercontent.com/torvalds/linux/master/Documentation/ABI/testing/sysfs-class-hwmon · hwmon sysfs interface doc: https://raw.githubusercontent.com/torvalds/linux/master/Documentation/hwmon/sysfs-interface.rst
