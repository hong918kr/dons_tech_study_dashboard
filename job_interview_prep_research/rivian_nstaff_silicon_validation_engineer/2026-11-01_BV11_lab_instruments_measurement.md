# BV11 · 랩 장비와 측정 — DMM · PSU · e-load · 오실로스코프 · LA · 프로토콜 애널라이저 · 파워 · 열 · BERT · SCPI 자동화
<!-- theme: pastel -->

> **묶음**: E 측정 · 마진 · **우선**: P1 · **선행 노트**: [BV03 전원 트리](2026-11-01_BV03_power_tree_sequencing_pmic.html) · [BV10 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) · **다음 노트**: [BV12 SI/PI · shmoo · PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · [BV06 첫 전원 인가](2026-11-01_BV06_first_power_on_checklist.html) · [BV14 검증 계획·자동화](2026-11-01_BV14_board_validation_plan_automation.html) · [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) · [LP00A I/O primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **코드**: [rail_sweep.py](onsitePrep/bv11/rail_sweep.py) (mock SCPI 장비, `python3 -I` 실행 검증) · [결과 CSV](onsitePrep/bv11/out/rail_sweep_results.csv)

## 0. TL;DR

- 이 노트로 답할 수 있는 질문: **"How would you measure the power-up sequence of 6 rails?" → "Your scope has 4 channels."**, "Logic analyzer vs scope vs protocol analyzer?", "Your rise-time measurement doesn't match the spec — what's wrong with your setup?", "How would you automate measurements across 50 boards?", "Why do you see 200 mV of ripple when the HW engineer sees 20 mV?"
- **외울 것 ① 대역폭·상승 시간**: `tr ≈ 0.35 / BW` (Gaussian 응답 scope), 측정되는 상승 시간 ≈ `√(tr_signal² + tr_scope² + tr_probe²)` → 장비(scope+probe) 상승 시간이 신호보다 **3배 빠르면 오차 ~5%, 5배면 ~2%** [1][2]. 1 ns 엣지를 350 MHz scope로 재면 약 1.41 ns로 보인다.
- **외울 것 ② ground lead**: 프로브 접지선은 인덕터 — 11 pF 프로브 + 6인치 ground lead는 약 **140 MHz에서 공진(ringing)** [1]. 빠른 엣지·ripple은 **spring tip(짧은 접지)** + ripple은 **20 MHz BW limit + AC coupling** 으로 잰다.
- **외울 것 ③ 장비 선택**: 아날로그 모양(레벨·엣지·ringing·ripple) = **scope**, 많은 디지털 선의 0/1 순서·타이밍 = **logic analyzer**, 고속 프로토콜 내용(PCIe TLP·LTSSM·USB 패킷) = **protocol analyzer**, 비트 에러율·지터 = **BERT**, 전류·전력·에너지 = **power analyzer/shunt**.
- **4채널로 6 rail 시퀀스**: 기준 rail(또는 PGOOD/EN)을 **CH1에 고정 + 같은 트리거**로 두 번 캡처하면 두 캡처가 같은 시간축을 공유 → 이 노트의 [rail_sweep.py](onsitePrep/bv11/rail_sweep.py) 가 mock scope + relay mux로 그대로 구현, 6장 mock 보드에서 전압 오차·시퀀스 위반·ripple 초과·입력 단락을 자동 판정 (§6 실제 출력).
- **30초 영어 답 (headline)**: "I pick the instrument by the question. A DMM for DC levels and shorts, a current-limited bench supply so the first power-on can't burn the board, a scope for analog shape — ramps, overshoot, ripple — with bandwidth three to five times faster than the edge and the shortest possible ground, a logic analyzer when I need many digital lines over a long time, and a protocol analyzer when the question is what packets were on PCIe or USB. Everything that repeats goes through SCPI with PyVISA, so a sweep across fifty boards produces one CSV and a pass-fail table instead of screenshots."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 병원 진찰 도구 세트

- 보드 검증 = **환자(보드) 진찰**. 증상을 말로 듣는 것(로그)만으로는 부족하고, 도구로 **재야** 한다.
  - **DMM(멀티미터) = 체온계·혈압계**: 한 시점의 숫자 하나(전압, 저항). 빠르고 정확하지만 "시간에 따른 변화"는 못 본다.
  - **Bench PSU(전원 공급기) = 링거(수액) 조절기**: 정해진 전압을 주되, **"이 이상 전류는 절대 주지 마"(current limit)** 를 걸 수 있다. 첫 전원 인가 때 합선이 있어도 보드가 타지 않게.
  - **E-load(전자 부하) = 러닝머신 부하 테스트**: 레귤레이터에 일부러 전류를 끌어다 써서 "부하가 걸리면 전압이 버티나" 확인.
  - **Oscilloscope(오실로스코프) = 심전도(ECG)**: 전압이 **시간에 따라 어떻게 변하나**를 그림으로. 엣지가 얼마나 빠른지, 튀는지(overshoot), 떨리는지(ripple).
  - **Logic analyzer = 여러 환자의 맥박을 동시에 기록하는 모니터**: 모양은 안 보고 "0이냐 1이냐"만, 대신 **수십 개 선을 오래** 기록.
  - **Protocol analyzer = 통역사**: 고속 링크 위를 지나가는 **대화 내용(패킷)** 을 해석해서 보여 줌.
  - **Power analyzer = 칼로리 측정기**: 순간 전류·평균 전력·에너지를 정밀하게.
  - **열화상 카메라 = 체열 사진**: 어디가 뜨거운지(합선 부품) 한 장으로. **Thermal chamber = 사우나/냉동실**: 보드 전체를 −40 °C ~ +125 °C로.
  - **BERT = 청력 검사**: 아주 작은 소리(에러)까지 몇 개 틀렸나 통계로.
- 핵심 메시지: **도구가 측정을 바꾼다** — 체온계를 입에 반만 물면 틀린 값이 나오듯, 프로브를 길게 접지하면 없는 noise가 보인다. "무엇을 쟀나"만큼 "어떻게 쟀나"를 말할 수 있어야 한다.

### 1.2 측정 사슬 그림 — 신호가 화면에 오기까지

```text
  보드(DUT)                프로브                         스코프 front-end                   디지털
 ┌─────────┐  tip ─┐   ┌──────────────┐  케이블   ┌──────────────────────────────┐   ┌──────────────┐
 │ 신호 점  │───────┼──▶│ R‖C (10 MΩ‖  │──────────▶│ 감쇠·증폭 → [BW limit 필터] │──▶│ ADC (8–12 bit)│──▶ 메모리 ──▶ 화면/측정/SCPI
 │ GND 점   │──┐    │   │  ~10 pF)     │           │  AC/DC coupling, offset      │   │ 샘플링 GS/s   │    (record length)
 └─────────┘  │    │   └──────────────┘           └──────────────────────────────┘   └──────────────┘
              └────┴── ground lead (인덕턴스 L!) ── L과 프로브 C가 직렬 공진 → ringing · 안테나처럼 noise 수집

  각 단계가 측정을 망칠 수 있는 지점:
   ① 프로브 부하: 프로브 C가 빠른 엣지를 느리게 만듦   ② ground lead: ringing/noise 추가
   ③ 대역폭: 빠른 엣지를 둥글게 → 상승 시간 과대 측정  ④ 샘플링/메모리: 긴 시간축에서 샘플 간격↑ → 글리치 놓침
   ⑤ 수직 분해능·offset: 1 V rail 위 10 mV ripple을 1 V/div로 보면 ADC 몇 칸 → 양자화 noise
```

### 1.3 "어떤 질문엔 어떤 장비" 한 장

```text
  질문                                          장비
  ────────────────────────────────────────────  ─────────────────────────────────────────────
  전압이 맞나? 단락인가? (DC, 한 점)              DMM (전원 끄고 저항, 켜고 전압)
  전원을 안전하게 넣을 수 있나?                   Bench PSU (current limit, OVP)
  부하가 걸리면 rail이 버티나?                    E-load (dynamic) + scope
  엣지·ramp·순서·ripple·overshoot (아날로그 모양)  Oscilloscope (+ 적절한 프로브)
  20개 GPIO/버스 선의 순서·타이밍 (디지털)        Logic analyzer
  I2C/SPI/UART 내용                             Scope serial decode 또는 LA decode
  PCIe/USB/Ethernet 패킷·링크 상태 기계            Protocol analyzer (interposer/tap)
  비트 에러율·지터 허용치                          BERT (pattern generator + error detector)
  전류 파형·평균 전력·sleep µA                     Power analyzer / current probe / shunt + DMM
  어디가 뜨겁나 / 온도 코너                        Thermal camera / chamber / thermal forcing
```

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| DMM | Digital Multimeter | 전압·전류·저항·도통을 숫자로 재는 장비 | 첫 전원 인가 전 rail–GND 저항(단락 찾기) |
| 4-wire (Kelvin) | — | 전류 넣는 선과 전압 재는 선을 분리 | 작은 저항·shunt 측정에서 선 저항 오차 제거 |
| NPLC | Number of Power Line Cycles | DMM이 몇 전원 주기 동안 적분하나 | 길수록 정확·느림, 50/60 Hz noise 제거 |
| burden voltage | — | 전류계 내부 저항에 걸리는 전압 강하 | sleep 전류 잴 때 DUT 전압을 떨어뜨림 |
| CV / CC | Constant Voltage / Constant Current | PSU가 전압 유지 모드 / 전류 한계에 걸린 모드 | **CC로 넘어갔다 = 한계까지 먹고 있다**(단락 신호) |
| OVP / OCP | Over-Voltage / Over-Current Protection | 넘으면 출력 차단 | 실수로 12 V를 1.8 V rail에 넣는 사고 방지 |
| remote sense | — | PSU가 보드 끝에서 전압을 재서 선 전압 강하를 보상 | 큰 전류에서 보드 입력 전압 정확히 유지 |
| SCPI | Standard Commands for Programmable Instruments | 장비에 보내는 텍스트 명령 규약 (`*IDN?`, `MEAS:VOLT?`) | 측정 자동화의 공용어 |
| VISA / PyVISA | Virtual Instrument Software Architecture | USB/LAN/GPIB 장비에 SCPI를 보내는 공통 라이브러리 / 그 Python 래퍼 | 장비 종류와 무관한 코드 |
| e-load | Electronic Load | 정해진 전류/저항/전력만큼 끌어가는 장비 | 레귤레이터 부하 응답(droop) 테스트 |
| bandwidth (BW) | −3 dB bandwidth | 진폭이 70.7%로 떨어지는 주파수 | 엣지를 얼마나 빠르게 볼 수 있나 결정 |
| rise time (tr) | 10–90% (또는 20–80%, 30–70%) | 신호가 낮은 값에서 높은 값으로 가는 시간 | 기준 % 를 맞춰야 스펙과 비교 가능 |
| sample rate / record length | — | 초당 샘플 수 / 한 캡처에 저장하는 샘플 수 | 긴 시간축에서 샘플 간격이 벌어져 글리치 놓침 |
| passive 10× probe | — | 10 MΩ, ~10 pF, 10:1 감쇠, 가장 흔한 프로브 | 범용, 고속 엣지·작은 ripple엔 한계 |
| active probe | — | 프로브 머리에 증폭기, ~1 pF 이하 | 고속 신호 부하 최소화 |
| differential probe | — | 두 점 사이 전압차를 재는 프로브 | 접지 안 된 신호, shunt 양단, 차동 신호(D-PHY 등) |
| current probe | — | 전선을 집어 전류 파형을 봄 (Hall: DC 가능, Rogowski: AC만) | 돌입 전류(inrush), 부하 계단 |
| ground lead / spring tip | — | 프로브 접지선 (긴 악어 클립 / 짧은 스프링) | 긴 선 = 인덕턴스 → ringing·noise |
| BW limit (20 MHz) | Bandwidth limit filter | 스코프 내부 저역 통과 필터 | ripple 측정 시 무관한 고주파 noise 제거 |
| AC coupling | — | DC 성분을 막고 변동만 봄 | 1 V rail 위 10 mV ripple을 크게 확대 |
| trigger | — | 어느 순간을 기준(t=0)으로 캡처할지 조건 | 드문 이벤트를 잡는 핵심 기술 |
| serial decode | — | 스코프가 I2C/SPI/UART 파형을 바이트로 해석 | 아날로그 + 프로토콜을 한 화면에 |
| logic analyzer (LA) | — | 많은 디지털 채널을 임계값으로 0/1 기록 | 버스 전체 순서, 긴 시간 |
| protocol analyzer | — | 고속 링크(PCIe 등) 패킷을 캡처·디코드 | TLP/DLLP/LTSSM 이벤트 확인 |
| BERT | Bit Error Ratio Tester | 패턴 송신 + 수신 비교로 BER·지터 허용치 측정 | 고속 SerDes 마진 (BV12) |
| thermal chamber / forcing | — | 보드 전체 온도 / 칩 하나에 뜨겁거나 찬 공기 | PVT 코너 테스트 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 DMM

- **전원 끈 상태 측정** (BV06 첫날): rail–GND 저항. 수 Ω 이하 = 단락 의심 (단, 큰 디커플링 커패시터가 충전되며 값이 천천히 올라가는 건 정상). 다이오드 모드: IO 핀의 ESD 다이오드 확인.
- **전원 켠 상태**: DC 전압. 입력 임피던스 보통 10 MΩ(일부 장비는 >10 GΩ 선택 가능) — 고임피던스 노드(예: 분압 저항 중간)를 재면 DMM이 부하가 된다 [추정: 기종별].
- **정밀도 요인**: NPLC(적분 시간), 자릿수(6½ digit), 4-wire 저항, 온도 계수. 1.8 V rail ±3% 판정이면 4½ digit로도 충분.
- **전류 측정**: DMM 직렬 삽입은 burden voltage 때문에 DUT 전압을 떨어뜨림 → sleep µA는 shunt + DMM 전압 측정이나 전용 장비(§2.7).

### 2.2 Bench PSU

- **CV/CC 교차**: 설정 V, 한계 I. 부하가 한계보다 많이 먹으면 CC 모드로 넘어가며 전압이 떨어진다. **첫 전원 인가 = 예상 전류의 1.2–1.5배 정도로 한계를 걸고 시작** [추정: 팀 관행마다], CC로 들어가면 즉시 OFF (BV03/BV06).
- **remote sense**: PSU 출력 단자가 아니라 보드 입력 단자의 전압을 sense 선으로 읽어 보상. 큰 전류 + 긴 선에서 수백 mV 강하를 없앰. sense 선이 빠지면 PSU가 과보상 → OVP 설정으로 보호.
- **다채널·시퀀싱**: 여러 rail을 PSU로 직접 줄 때(PMIC 없이 검증) 채널 간 지연·순서 설정, tracking.
- **SCPI 예** (Keysight E36300 programming guide 목차 기준 형태 [3]): `APPLy CH1, <V>, <I>`, `MEASure:CURRent? (@1)`, `OUTPut ON`. 정확한 문법은 기종별 가이드 확인 — 이 노트 코드는 **일반화한 명령**(`VOLT`, `CURR`, `OUTP ON`, `MEAS:VOLT?`, `MEAS:CURR?`)을 씀 [추정].
- **IEEE 488.2 공통 명령**: `*IDN?`(신원), `*RST`(초기화), `*CLS`(상태 지움), `*OPC?`(이전 명령 완료까지 대기 후 1) + SCPI `SYST:ERR?`(에러 큐, `+0,"No error"`까지 읽기) — 장비 무관하게 거의 공통 [추정: 표준 공통 명령으로 알고 있음].

### 2.3 E-load

- 모드: CC(정전류), CR(정저항), CV, CP(정전력). **Dynamic(transient) 모드**: 예) 0.2 A ↔ 2 A를 1 kHz로 전환, slew rate 설정 → 레귤레이터의 **load-step droop/overshoot**과 회복 시간을 scope로 (BV03 꼬리 "rail 10% low under load").
- 보드 위 SoC 대신 e-load를 레귤레이터 출력에 붙여 **SoC 없이 전원부만 먼저 검증**하는 것도 흔한 단계.

### 2.4 Oscilloscope

#### 2.4.1 대역폭·상승 시간

- Gaussian 응답 scope: `tr(10–90%) ≈ 0.35 / BW` [1][2]. maximally-flat 응답 scope는 상수가 더 큼(약 0.4–0.45) [2].
- 규칙: 장비 상승 시간을 신호의 **1/3–1/5** 로 → 오차 약 5% / 2% [1]. 주파수 관점: 관심 최고 주파수의 **5배 BW** ("five-times rule") [1][2].
- 계산 예 (Python 계산값): 1 ns 엣지를 —

| 측정 시스템 BW | 시스템 tr | 측정되는 tr | 오차 |
|---|---|---|---|
| 350 MHz | 1.00 ns | 1.41 ns | +41% ❌ |
| 500 MHz | 0.70 ns | 1.22 ns | +22% ❌ |
| 1 GHz | 0.35 ns | 1.06 ns | +6% 🟡 |
| 2 GHz | 0.175 ns | 1.015 ns | +1.5% ✅ |

- **프로브도 시스템의 일부**: scope 1 GHz에 500 MHz 프로브면 시스템 BW는 더 낮다. 제조사는 "프로브 팁 기준" 시스템 BW를 명시한다 [1].

#### 2.4.2 샘플링과 메모리

- 실시간 샘플링에서 BW 대비 샘플 속도는 **최소 2.5배, 보통 4–5배 이상** 권장 [추정: sin(x)/x 보간 기준 관행].
- `record length = sample rate × 캡처 시간`. 10 ms 전원 시퀀스를 5 GS/s로 보려면 50 Mpts. 메모리가 작으면 scope가 **샘플 속도를 자동으로 낮춤** → 짧은 글리치를 놓친다 → **peak-detect 모드** 또는 glitch/runt 트리거.
- 수직 분해능: 8-bit ADC는 화면 전체를 256 단계로. 1 V/div × 8 div = 8 V 범위면 1 단계 ≈ 31 mV → 10 mV ripple은 안 보임 → **AC coupling + 작은 V/div** 또는 offset.

#### 2.4.3 프로브 종류

| 프로브 | 전형 특성 | 언제 | 주의 |
|---|---|---|---|
| Passive 10× | 10 MΩ ‖ ~10–15 pF, 수백 MHz | 범용 디지털·전원 ramp | 보상(compensation) 조정, 긴 ground lead |
| Passive 1× | 낮은 BW(수–수십 MHz), 감쇠 없음 | 작은 ripple(scope noise 덜 증폭) | 큰 C 부하 [1] |
| Active single-ended | ~1 pF 이하, GHz | 고속 엣지, 클럭 | 입력 전압 범위 작음, 정전기 민감 |
| Differential | 두 점의 차이, 높은 CMRR | 접지 안 된 신호, shunt 양단, 차동 클럭(PCIe REFCLK) | CMRR은 주파수 따라 떨어짐 [1] |
| Current (Hall / Rogowski / clamp) | DC–수십 MHz(Hall) | inrush, 부하 계단 | degauss·zero, 전선을 루프에 통과시켜야 |
| Power-rail probe | 큰 DC offset + 낮은 noise, 50 Ω 계열 | DC rail 위 mV ripple/PI | 예: Tek TPR1000/4000 — 20 MHz BWL 시 시스템 noise <300 µVpp 스펙 [4] |

#### 2.4.4 Ground lead — 왜 짧아야 하나

- 접지선은 인덕터 L. 프로브 입력 C와 **직렬 공진** → 빠른 엣지가 오면 ringing, 그리고 루프가 **안테나**가 되어 주변 스위칭 noise를 주워 온다 [1].
- Tek 수치: **11 pF 프로브 + 6인치 ground lead ≈ 140 MHz에서 ringing** [1] (역산하면 L ≈ 117 nH). Tek P6137 매뉴얼: 표준 ground lead ≈ 140 nH vs low-inductance lead ≈ 32 nH [5].
- `f = 1 / (2π √(L·C))` 로 보면 (C = 11 pF 가정, Python 계산값):

```text
  ground 방식              L (대략)     공진 주파수     의미
  6인치 악어 클립          ~117 nH      ~140 MHz      200 MHz 이상 scope면 화면에 ringing으로 나타남
  저인덕턴스 lead          ~32 nH       ~268 MHz
  spring tip (수 mm)       ~5 nH [추정]  ~680 MHz      대부분 측정에서 대역 밖
```

- **자가 진단법**: 프로브 케이블을 움직이거나 손을 대 보았을 때 파형의 aberration이 변하면 = 접지 시스템 문제 [1].
- ground loop: scope 접지(전원 코드 접지)와 DUT 접지가 다른 경로로 연결되면 저주파 hum. 접지 안 된 DUT(배터리)·고전압 측은 differential probe.

#### 2.4.5 트리거

- **Edge**(가장 기본, 레벨·기울기), **pulse width**(N ns보다 짧은/긴 펄스 = 글리치), **runt**(한 임계값은 넘었지만 다른 임계값은 못 넘은 불완전 스윙), **timeout**(N µs 동안 변화 없음 = 버스 멈춤), **setup/hold**(클럭 대비 데이터 타이밍 위반), **serial**(I2C 주소 0x48 + NACK, UART 특정 바이트), **zone/mask**.
- 모드: auto(트리거 없으면 그냥 그림) / normal(트리거 시에만) / **single**(한 번 잡고 멈춤 — 전원 인가 캡처의 표준). holdoff로 반복 신호의 안정 트리거.
- 리셋·부트 디버그: **reset 핀 rising 트리거 + pre-trigger 구간**으로 그 직전 rail 상태를 같이 본다.

#### 2.4.6 대표 측정 3가지

- **상승 시간/overshoot**: 기준 %를 신호 스펙과 맞추기(I2C 30–70%, 일반 10–90%, 고속 20–80%), 엣지 하나를 화면 2–3 div에 확대, 평균 모드는 반복 신호에서만.
- **ripple/noise**: AC coupling, **20 MHz BW limit**(관행 — 레귤레이터 스위칭 주파수 수백 kHz–수 MHz 리플은 통과, 수백 MHz 픽업 noise 제거), spring tip을 **출력 커패시터 양단에 직접**, 5–10 mV/div, Vpp와 RMS 둘 다. PI 엔지니어가 전 대역 noise를 원하면 BWL 없이 power-rail probe로 — "어떤 BW로 쟀나"를 결과에 항상 기록.
- **다채널 타이밍(시퀀스)**: 채널 간 **deskew**(프로브마다 지연 다름), 같은 트리거, 1–2 ms/div, 10%/50%/90% 시점 측정 → 순서·간격.

### 2.5 Logic analyzer vs scope vs protocol analyzer

| 항목 | Oscilloscope | Logic analyzer | Protocol analyzer |
|---|---|---|---|
| 보는 것 | 아날로그 전압 모양 | 임계값 기준 0/1 | 링크 계층 패킷·상태 |
| 채널 수 | 2–8 | 16–136+ | 링크 1–2개(레인 다수) |
| 시간 | 짧음(메모리 한계) | 길게(깊은 메모리, 압축) | 길게(트래픽 필터링) |
| 잡는 문제 | ringing, slow edge, 레벨 오류, 글리치, ripple | 버스 순서, 상태 머신, 핸드셰이크 타이밍, 많은 GPIO | TLP 내용, completion timeout, LTSSM 전이, 재전송, 링크 협상 |
| 못 잡는 것 | 수십 선의 순서, 프로토콜 내용(저속 decode 제외) | 아날로그 마진(전압이 임계 근처면 오판) | 아날로그 신호 품질(eye는 BERT/scope) |
| 예 | 1 GHz MSO, serial decode | Saleae류 USB LA, 벤치 LA | Teledyne LeCroy PCIe 분석기(interposer), USB 분석기 [추정: 제품군 예시] |

- **MSO(Mixed Signal Oscilloscope)** = scope + 16채널 디지털 — 6 rail 문제에서 PGOOD/EN 같은 **디지털 신호는 디지털 채널로** 돌리면 아날로그 채널을 아낀다.
- **Protocol analyzer를 쓰는 때**: "link up은 되는데 드라이버가 timeout" → 실제로 Completion이 오나? 재전송(NAK/replay)이 많나? LTSSM이 Recovery를 반복하나? — 소프트웨어 로그·AER만으로 가설이 안 갈릴 때 [LP15](2026-10-31_LP15_debug_scenarios.html). PCIe는 interposer(슬롯 사이에 끼우는 카드)나 mid-bus probe가 필요하고, interposer 자체가 링크 마진을 바꿀 수 있다 [추정].
- **Ethernet**: 대개 미러 포트/탭 + Wireshark로 충분, 자동차 T1은 **T1 ↔ 표준 Ethernet 변환 미디어 컨버터/탭**을 거쳐 캡처 [추정].

### 2.6 BERT와 지터 (자세히는 BV12)

- 구성: **pattern generator**(PRBS7/15/31, 스트레스: 지터·ISI 주입) + **error detector**(수신 비트 비교). 측정: BER, **jitter tolerance**(얼마나 흔들어도 견디나), bathtub curve.
- 통계: 에러 0개로 BER < 1e-12를 95% 신뢰로 → 약 3e12 bit (BV10 §2.5.4). 그래서 BERT 측정은 **외삽(bathtub 외삽)** 과 함께 쓴다.
- 실리콘 validation에서는 SoC 내장 PRBS/loopback(BIST)을 쓰고, 외부 BERT는 컴플라이언스·특성화에 [추정].

### 2.7 전류·전력 측정

- **shunt 저항**: `I = V_shunt / R`. 작은 R(mΩ)이면 4-wire(Kelvin) 패드로 shunt 양단을 직접, high-side면 **differential** 측정. 검증 보드에 rail별 shunt를 미리 넣어 달라고 하는 게 BV02의 "debuggability 요청" 항목.
- **DC power analyzer / SMU**: PSU + 정밀 전류 측정 + 파형 로깅 하나로. 동적 범위가 핵심 — sleep 수 µA ↔ active 수 A (6자릿수) → 자동 레인지 전환 시 glitch 주의.
- **current probe**: inrush(돌입 전류) 파형, 부하 계단. Hall 타입은 DC 측정 가능하지만 사용 전 degauss/zero [1].
- 전력 계산: rail별 `V × I`를 동시에 샘플해야 의미 있음(시간 정렬).

### 2.8 열 장비

- **열화상 카메라**: 전원 켜고 수 초 내 **뜨거운 점 = 단락 부품** (BV03 "전류 3배" 꼬리). 반사가 강한 금속 표면은 방사율 때문에 실제보다 낮게 보임 → 테이프/도료 [추정].
- **Thermocouple**: 칩 표면·보드 특정 지점의 절대 온도 로깅, SoC 내부 온도 센서와 비교.
- **Thermal chamber**: 보드 전체를 −40 ~ +125 °C 등으로, **soak(온도 안정화) 시간** 필요, 저온→상온 전환 시 **결로** 주의.
- **Thermal forcing system (예: ThermoStream류)**: 칩 하나에 노즐로 뜨겁거나 찬 공기 — 빠른 온도 전환, 보드 다른 부품은 상온 유지 → 칩 vs 보드 원인 분리에 유용 [추정: 제품명은 예시].

### 2.9 자동화 — SCPI / VISA / PyVISA

```text
  Python 테스트 코드 ── driver(Scope/PSU/Mux 클래스: SCPI 문자열만 앎)
          │                      │ .write(cmd) / .query(cmd)
          ▼                      ▼
  backend 선택 ── PyVISA ResourceManager("@ivi" NI-VISA/Keysight IO, 또는 "@py" pyvisa-py) ── LAN(VXI-11/HiSLIP/socket) · USB-TMC · GPIB
              └─ Mock backend (이 노트 코드) ── 같은 .write/.query 인터페이스, 보드 모델로 응답 생성
```

- 자원 문자열 예: `TCPIP0::192.168.0.50::inst0::INSTR`, `USB0::0x2A8D::...::INSTR`, `GPIB0::5::INSTR` [추정: 일반 VISA 형식].
- PyVISA: `rm = pyvisa.ResourceManager(); rm.list_resources()` [6]. 기본은 NI-VISA 래퍼, 순수 Python 백엔드 **pyvisa-py**는 일부 프로토콜만 지원 [6].
- 자동화 품질 포인트:
  1. **동기화**: 설정 후 `*OPC?` 또는 상태 폴링 — 측정 전에 설정이 끝났는지.
  2. **에러 큐**: 각 단계 후 `SYST:ERR?`를 `+0`까지 읽어 로그 (오타 명령이 조용히 무시되는 것 방지).
  3. **timeout·재연결**, 장비 `*IDN?`·펌웨어 버전·교정일을 결과 메타데이터에.
  4. **가능하면 원시 파형을 받아 호스트에서 계산**(재현성, 알고리즘 버전 관리) vs scope 내장 측정(빠름) — 둘 다 쓰되 판정 로직은 코드에.
  5. **mock 백엔드**로 장비 없이 CI에서 테스트 로직 검증.

## 3. 보드에서 볼 것 · 잴 것

### 3.1 측정 레시피 표

| 무엇을 | 장비·프로브 | 설정 | 통과 기준 예 |
|---|---|---|---|
| rail–GND 단락 (무전원) | DMM 저항/다이오드 | 큰 cap 충전 기다리기 | 회로도 예상치(수십 Ω–kΩ), 0 Ω 근처 = 단락 |
| 첫 전원 인가 입력 전류 | PSU (current limit) | 한계 = 예상 × 1.2–1.5 [추정] | CV 유지, 예상 전류 ±30% 내 |
| rail DC 전압 | DMM (또는 PMIC telemetry) | test point–근처 GND | 스펙 ±3% 등 |
| power-up 시퀀스 | scope 4ch + MSO 디지털 | single, 기준 rail rising 트리거, 1 ms/div, pre-trigger 1 div | 순서·간격·단조 증가·overshoot |
| ripple | scope, spring tip, AC, 20 MHz BWL | 2 µs/div, 5–10 mV/div | Vpp < rail 스펙 |
| load-step droop | e-load dynamic + scope | 부하 계단 트리거 | droop < X mV, 회복 < Y µs |
| 클럭 주파수·jitter | scope (active/diff 프로브) | 주기 측정 통계, histogram | ppm 오차·주기 jitter 스펙 (BV04) |
| reset 타이밍 | scope 2–3ch | reset rising 트리거 | 마지막 PGOOD 후 ≥ N ms (BV05) |
| I2C 상승 시간 | scope 10× spring | 30–70% 기준 | 모드별 최대 tr (BV10) |
| sleep 전류 | power analyzer / shunt + DMM | 긴 적분, 자동 레인지 끔 | µA 스펙 |
| 핫스팟 | 열화상 카메라 | 전원 직후 수 초 | 국부 고온 없음 |

### 3.2 "6 rail을 4채널 scope로" — 레시피

```text
 방법 1 (이 노트 코드): 기준 고정 + 두 번 캡처
   캡처 A: CH1=VDD_CORE(트리거, 50% rising)  CH2=PHY  CH3=DDR VDD2  CH4=VDDQ
   캡처 B: CH1=VDD_CORE(같은 트리거)         CH2=IO   CH3=3V3       CH4=PGOOD
   → t=0이 두 캡처 모두 "CORE 50% 통과" → 6 rail의 t50·t90을 한 시간축에 합침
   전제: 시퀀스가 매 전원 인가마다 같다(반복성) → 여러 번 반복해 jitter도 같이 기록

        t=0 (trigger)
          │
 CORE ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  (A,B 공통)
 PHY  ________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  (A)
 VDD2 ______________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  (A)
 VDDQ ____________________/‾‾‾‾‾‾‾‾‾‾  (A)
 IO   __________________________/‾‾‾‾  (B)
 3V3  ________________________________/‾‾ (B)
 PGOOD _________________________________|‾ (B, 디지털)

 방법 2: MSO 디지털 채널에 PGOOD·EN 신호(디지털) → 아날로그 4ch는 rail에만
 방법 3: 외부 트리거 입력(PSU 출력 ON의 trigger out, 또는 EN 신호)을 t=0으로 → CH1까지 rail에 사용
 방법 4: PMIC 시퀀서 로그/telemetry, 또는 다채널 DAQ(느린 ramp면 충분) → scope는 의심 rail만
 방법 5: 8채널 scope 빌리기 — 가장 단순하지만 "장비 없이도 푼다"를 보여 주는 게 면접 포인트
```

### 3.3 예시 파형 해석 [예시 파형]

```text
[예시 파형] VDD_CORE 0.75 V ripple, AC coupling, 10 mV/div, 2 µs/div
  (a) 6인치 ground lead, BW full     (b) spring tip, BW 20 MHz
  ┌──────────────────────────┐       ┌──────────────────────────┐
  │ ╷  ╷  ╷  ╷  ╷  ╷  ╷  ╷   │       │                          │
  │~┼~~┼~~┼~~┼~~┼~~┼~~┼~~┼~~ │       │ ~~~~~~~~~~~~~~~~~~~~~~~~ │  ← 1 MHz 스위칭 리플만
  │ ╵  ╵  ╵  ╵  ╵  ╵  ╵  ╵   │       │                          │
  └──────────────────────────┘       └──────────────────────────┘
   Vpp ≈ 수십 mV: 스위칭 엣지마다       Vpp ≈ 10 mV 수준: 실제 리플
   ~140 MHz ringing 스파이크 (측정 artifact)
```

- (a)의 스파이크는 **레귤레이터 스위칭 엣지 순간**에만 나타나고, 케이블을 움직이면 모양이 변함 → 접지 루프 artifact [1]. 이 패턴을 보고 "rail이 noisy하다"고 HW 팀에 버그를 열면 안 된다.
- 이 차이를 코드로 재현한 숫자는 §6 (`ground=long BWL20M=OFF → 78.1 mVpp` vs `spring ON → 8.9 mVpp`, mock 모델 결과).

### 3.4 Linux에서 보이는 측정값 (장비 대신 보드 자체 센서)

```bash
ls /sys/class/hwmon/                     # 보드 전압/전류/온도 센서(PMIC, INA226 같은 전력 모니터)
cat /sys/class/hwmon/hwmon*/name
cat /sys/class/hwmon/hwmon2/in1_input    # mV
cat /sys/class/hwmon/hwmon2/curr1_input  # mA
cat /sys/class/thermal/thermal_zone*/temp   # m°C
```

```text
[예시 출력]
$ cat /sys/class/hwmon/hwmon*/name
cpu_thermal
ina226_vdd_core
$ cat /sys/class/hwmon/hwmon2/in1_input
752
$ cat /sys/class/hwmon/hwmon2/curr1_input
3184
```

- `ina226_vdd_core`: 보드에 실장된 전력 모니터 IC(shunt + ADC, I2C) [추정: 보드마다]. **장비 측정과 on-board 측정을 교차 검증**해 두면, 이후 보드 팜 자동화에서 scope 없이 hwmon만으로 회귀 가능 (BV14).
- `752` mV = 0.752 V (VDD_CORE 0.75 V 스펙 내), `3184` mA. hwmon 단위: 전압 mV, 전류 mA, 온도 m°C (Linux hwmon sysfs 관례 [추정: hwmon sysfs-interface 문서 기준으로 알고 있음]).

## 4. 꼬리 질문 체인

### 체인 A — "How would you measure the power-up sequence of 6 rails?"

Q → 꼬리1 "Your scope has 4 channels." → 꼬리2 "How do you know the two captures line up?" → 꼬리3 "What exactly do you measure on each rail?"

<details>
<summary>A-Q. How would you measure the power-up sequence of 6 rails?</summary>

- **EN**: "Single-shot capture triggered on the first rail or the enable signal, timebase so the whole sequence fits — usually a few milliseconds — with pre-trigger so I see the start. Short grounds at each rail's output capacitor, channels deskewed. On each rail I measure the 10, 50 and 90 percent times, monotonic ramp, overshoot and final value, then check order and gaps against the PMIC or SoC datasheet sequence, and PGOOD and reset release relative to the last rail."
- 한국어 핵심: single + 첫 rail/EN 트리거 + pre-trigger, rail별 10/50/90%·단조·overshoot·최종값 → 순서·간격·PGOOD·reset.
</details>

<details>
<summary>A-꼬리1. Your scope has 4 channels.</summary>

- **EN**: "Keep one reference rail on channel 1 as the trigger in every capture and rotate the other five through channels 2 to 4 — two captures, since the sequence is repeatable power cycle to power cycle. Digital signals like PGOOD and enables go on the MSO's digital channels. Or use the external trigger input from the supply's output-on so all four analog channels are free. That's exactly what my mock automation does with a relay mux: same trigger, two banks, merged timeline."
- 한국어 핵심: CH1 기준 고정 + 같은 트리거로 2회 캡처, 디지털은 MSO 채널, 외부 트리거, (자동화는 relay mux).
</details>

<details>
<summary>A-꼬리2. How do you know the two captures line up?</summary>

- **EN**: "Because both are referenced to the same physical event — the reference rail crossing 50 percent — so time zero means the same thing. I still repeat each capture several times and record the spread of every rail's timing relative to the reference; if the spread is small compared to the required gaps, merging is valid. If the sequence isn't repeatable, that itself is a finding."
- 한국어 핵심: 같은 물리 이벤트 기준 t=0 + 반복 측정으로 산포 확인, 반복성 없음 자체가 버그.
</details>

<details>
<summary>A-꼬리3. What exactly do you measure on each rail?</summary>

- **EN**: "Start time and 50 percent time for ordering, 10-to-90 rise time against the slew-rate requirement, monotonicity — no dips or plateaus during the ramp, which can confuse power-on-reset circuits — overshoot, final DC value, and later ripple in steady state with a separate AC-coupled, bandwidth-limited capture. Then power-down too, since many parts specify the reverse order."
- 한국어 핵심: t_start·t50·rise(slew)·단조성·overshoot·최종값·(별도) ripple, power-down 순서도.
</details>

### 체인 B — "When do you use a logic analyzer vs a scope vs a protocol analyzer?"

<details>
<summary>B-Q. Logic analyzer vs scope vs protocol analyzer?</summary>

- **EN**: "Scope when the question is analog — is the edge clean, is the level right, is there ringing or ripple. Logic analyzer when I need many digital lines over a long time and only care about ones and zeros and their order — a bus handshake, a state machine, twenty GPIOs. Protocol analyzer when the question is what was said on a high-speed link — PCIe TLPs, completions, replays, LTSSM transitions — which no scope can decode in real time. I usually start with the scope, because a digital tool will happily decode a signal that's marginal analog."
- 한국어 핵심: 아날로그=scope, 다채널·장시간 디지털=LA, 고속 패킷=PA. 아날로그 확인 먼저.
</details>

<details>
<summary>B-꼬리1. Give me a case where the logic analyzer lied.</summary>

- **EN**: "A slow or ringing edge sitting near the logic analyzer's threshold — it can show a clean extra edge, a double clock, or miss one entirely, depending on where the threshold is. The decode looks like a protocol error, but the root cause is analog. A scope on the same line shows the ringing or the slow rise; fixing the pull-up or termination fixes the 'protocol' problem."
- 한국어 핵심: 임계값 근처 ringing/느린 엣지 → LA가 가짜 엣지. scope로 아날로그 확인.
</details>

<details>
<summary>B-꼬리2. When is the protocol analyzer worth the setup cost?</summary>

- **EN**: "When software-visible state can't separate the hypotheses — for example the driver times out but AER is clean, and I need to know whether the request ever went out, whether the completion came back, or whether the link went through recovery in between. The cost is the interposer, which can change the link's margin, so I confirm the failure still reproduces with it in place."
- 한국어 핵심: SW 상태로 가설이 안 갈릴 때. interposer가 마진을 바꾸니 재현 확인.
</details>

### 체인 C — "Your rise-time measurement doesn't match the spec. What could be wrong with your setup?"

<details>
<summary>C-Q. Rise time doesn't match the spec — what's wrong with the setup?</summary>

- **EN**: "Several things before I blame the board. Bandwidth: the measured rise time is roughly the root-sum-square of the signal, scope and probe rise times, so a 350 MHz system makes a 1 ns edge look like 1.4. Probe loading: a 10 to 15 picofarad passive probe slows a weak driver. Ground lead: a six-inch lead rings around 140 MHz and distorts the edge. Thresholds: the spec may use 20–80 or 30–70 while the scope defaults to 10–90. And sample rate on a long timebase."
- 한국어 핵심: BW(RSS), 프로브 C 부하, ground lead ringing, 기준 %, 샘플 속도.
</details>

<details>
<summary>C-꼬리1. How do you tell probe loading from a real slow edge?</summary>

- **EN**: "Change the probe and see if the answer changes. An active probe with sub-picofarad input, or probing at a different point, should not change a real rise time. If the edge gets faster with the lower-capacitance probe, my original probe was part of the circuit. The same trick works for the ground: move the cable, and if the aberrations move, they're from my grounding."
- 한국어 핵심: 프로브를 바꿔도 같으면 진짜, 빨라지면 프로브 부하. 케이블 움직여 aberration 변하면 접지.
</details>

### 체인 D — "How would you automate measurements across 50 boards?"

<details>
<summary>D-Q. How would you automate measurements across 50 boards?</summary>

- **EN**: "Instrument drivers that speak SCPI through PyVISA, wrapped in small classes — supply, scope, mux — with a mock backend so the test logic runs in CI without hardware. Each board goes through the same flow: configure, check the error queue, power on with a current limit, capture, pull raw waveforms, compute measurements in code, judge against a spec file, and write one row per board and rail into a CSV or database with serial number, firmware, temperature and instrument IDs. Then the dashboard shows distributions across boards, not one screenshot."
- 한국어 핵심: SCPI/PyVISA 드라이버 + mock, 설정→에러 큐→캡처→원시 파형→코드 계산→스펙 판정→DB(메타데이터).
</details>

<details>
<summary>D-꼬리1. Why pull raw waveforms instead of using the scope's built-in measurements?</summary>

- **EN**: "Reproducibility and control. My code defines exactly which thresholds and windows are used and is version-controlled, I can re-run new analysis on old captures, and it's the same algorithm across different scope models. The built-in measurements are fine for quick checks and faster to transfer, so I often log both and alarm if they disagree."
- 한국어 핵심: 기준·윈도우를 코드로 고정·버전 관리, 과거 데이터 재분석, 장비 간 동일. 내장 측정과 교차 확인.
</details>

<details>
<summary>D-꼬리2. One board in 50 fails. What do you do with the data?</summary>

- **EN**: "Look at where it sits in the distribution — a clear outlier on one rail points at that board's assembly; if it's at the edge of a broad distribution, the design margin is the problem and more boards will fail. Re-test the same board to rule out the fixture, swap fixture position, and if it follows the board, it becomes a hardware ticket with the waveform and the measured numbers attached."
- 한국어 핵심: 분포에서의 위치(이상치 vs 분포 끝) → 재측정·픽스처 교체 → 보드 따라가면 HW 티켓.
</details>

### 체인 E (추가) — "Why do you see 200 mV of ripple when the HW engineer sees 20 mV?"

<details>
<summary>E-Q. You see 200 mV of ripple, the HW engineer sees 20 mV. Why?</summary>

- **EN**: "Almost always my measurement. A long ground clip forms a loop that picks up switching noise and rings, and full bandwidth shows hundreds of megahertz of energy that isn't on the rail. The HW engineer is probably using a spring tip right at the output capacitor, AC coupling and a 20 MHz bandwidth limit. I'd repeat with that setup and state the bandwidth in the result. My mock demo shows the same effect: about 78 mV peak-to-peak with a long lead and full bandwidth versus about 9 with a spring tip and the limit."
- 한국어 핵심: 긴 ground lead + full BW = 픽업·ringing. spring tip + 출력 cap + AC + 20 MHz BWL, BW를 결과에 기록.
</details>

### 체인 F (추가) — "How would you measure the SoC's sleep current?"

<details>
<summary>F-Q. How would you measure sleep current accurately?</summary>

- **EN**: "Not with a DMM in series on its default range — the burden voltage drops the rail and the range switch glitches the board on wake-up. I'd use a shunt with a differential measurement or a DC power analyzer with seamless ranging, long integration to average out periodic wake-ups, and log the waveform so I can see the wake events. And I'd cross-check against the on-board power monitor if there is one."
- 한국어 핵심: DMM 직렬 X(burden·레인지 글리치) → shunt+차동 또는 seamless ranging 분석기, 긴 적분, 파형 로깅.
</details>

## 5. Don 경험 연결

- "At Apple I root-caused PCIe, I2C, SPMI and RFFE issues on wireless chipset integration with a digital scope and a protocol analyzer — `[숫자 채우기: 한 사례 — 증상, 어떤 장비·트리거, 원인]`. That's where I learned to confirm the analog picture first before trusting a decode."
- "My resume lists JTAG, oscilloscope, logic analyzer and power analyzer because I used them for bring-up and debug — `[숫자 채우기: 써 본 scope/LA/분석기 모델]`."
- "At SK hynix I automated the chamber reliability platform for NAND and PCIe interfaces, so instrument control, temperature soak and result databases are familiar — `[숫자 채우기: 온도 범위, 장비 수, 줄인 시간]`. The rail-sweep script in my notes is the same pattern with SCPI drivers and a mock backend."
- 정직 포인트: 보드 전원부 설계·PI 측정 전문가는 아님 → "I'm not the PI engineer, but I know how to set up the measurement so we argue about the board, not about my probe."

## 6. 실습 / 코드

### 6.1 Mock SCPI 장비 + rail sweep (실행 검증됨)

- 파일: [onsitePrep/bv11/rail_sweep.py](onsitePrep/bv11/rail_sweep.py) — stdlib만 사용, `python3 -I`로 실행.
- 구조:
  - **Driver 계층**: `PSU`, `Mux`, `Scope` 클래스는 `.write()/.query()`로 SCPI 문자열만 보냄 (PyVISA resource와 같은 모양).
  - **Backend**: `open_resource(addr, ...)` — 주소가 `MOCK::`이면 mock, 아니면 `import pyvisa` 시도 → 실패하면 mock으로 대체하고 로그 출력. (이 Mac에는 pyvisa 미설치 → scope 주소 `TCPIP0::192.168.0.50::inst0::INSTR`가 mock으로 떨어지는 경로가 실제로 실행됨.)
  - **Mock 장비**: SCPI 에러 큐(`SYST:ERR?`, 미지원 명령 → `-113,"Undefined header"`), PSU(CV/CC: 단락 보드에서 전압 붕괴), 4ch scope(single·트리거·`:TER?`·`:WAV:DATA?`, AC coupling, 20 MHz 1-pole BW limit), relay mux(bank 1/2 → CH2–CH4).
  - **보드 모델**: rail 6개 + PGOOD, 램프(raised-cosine), 1 MHz 스위칭 리플, scope noise, "long ground lead" 시 150 MHz 감쇠 ringing.
  - **판정**: rail 전압 ±tol, ripple ≤ 한계, 시퀀스 순서·간격(t50), PGOOD > 마지막 rail t90, 입력 CC 검출 → 보드별 PASS/FAIL + CSV.
  - 주입된 결함: B03 VDD2 −6%, B04 VDD_IO가 CORE보다 먼저, B05 CORE 리플 과다, B06 입력 단락.
- 실행:

```bash
cd onsitePrep/bv11
python3 -I rail_sweep.py --scpi-log      # 기본 6 보드
python3 -I rail_sweep.py --boards 8      # B07, B08은 결함 없는 보드
```

- **실제 출력** (2026-11-01, macOS Python 3.9.6, exit 0):

```text
BV11 rail sweep -- backend selection
  [backend] TCPIP0::192.168.0.50::inst0::INSTR -> pyvisa unavailable (ModuleNotFoundError), using mock
  psu   *IDN? -> MOCKLAB,PSU-3CH,SN0001,1.0
  mux   *IDN? -> MOCKLAB,RELAY-MUX,SN0002,1.0
  scope *IDN? -> MOCKLAB,DSO-4CH-1GHZ,SN0003,1.0

Probe/ground-lead demo on B01 VDD_CORE ripple (AC, 2 us/div):
  ground=long   BWL20M=OFF ->   78.1 mVpp
  ground=long   BWL20M=ON  ->   13.9 mVpp
  ground=spring BWL20M=OFF ->   15.0 mVpp
  ground=spring BWL20M=ON  ->    8.9 mVpp

SCPI log, scope, board B01 (first 18 lines):
    >> :TIM:SCAL 1E-3
    >> :CHAN1:COUP DC
    >> :CHAN1:BWL OFF
    >> :CHAN2:COUP DC
    >> :CHAN2:BWL OFF
    >> :CHAN3:COUP DC
    >> :CHAN3:BWL OFF
    >> :CHAN4:COUP DC
    >> :CHAN4:BWL OFF
    >> :TRIG:SOUR CHAN1
    >> :TRIG:LEV 0.375
    >> :TRIG:SLOP POS
    >> :SING
    >> :TER?
    << 1
    >> :WAV:SOUR CHAN1
    >> :WAV:XINC?
    << 5.000000e-06

Per-rail results (board B01):
  rail             nom V   meas V   err %  t50 ms  rise us  rip mVpp
  VDD_CORE_0V75    0.750   0.7525    0.33   0.000    287.4       8.8
  VDD_PHY_0V8      0.800   0.7996   -0.05   0.684    262.0      15.8
  VDD2_DDR_1V1     1.100   1.1022    0.20   1.295    222.4      11.6
  VDDQ_DDR_0V6     0.600   0.6011    0.19   1.929    195.5      11.5
  VDD_IO_1V8       1.800   1.8102    0.57   2.586    249.1      11.2
  VDD_3V3          3.300   3.2967   -0.10   3.469    330.9       8.3
  PGOOD after last rail t90: 0.922 ms

Board summary:
  board verdict Iin A  notes
  B01   PASS    1.040  -
  B02   PASS    0.906  -
  B03   FAIL    1.074  VDD2_DDR_1V1 1.034 V (-6.0%) out of +/-3%
  B04   FAIL    0.972  sequence violation: order=VDD_IO_1V8 > VDD_CORE_0V75 > VDD_PHY_0V8 > VDD2_DDR_1V1 > VDDQ_DDR_0V6 > VDD_3V3 (first wrong: VDD_IO_1V8)
  B05   FAIL    0.936  VDD_CORE_0V75 ripple 41.5 mVpp > 25
  B06   FAIL    2.000  input in CC: 0.10 V / 2.00 A -> abort (short?)

wrote out/rail_sweep_results.csv (30 rows); 2/6 boards PASS
```

- CSV 첫 줄들 (실제 파일 [out/rail_sweep_results.csv](onsitePrep/bv11/out/rail_sweep_results.csv)):

```text
board,rail,nominal_v,meas_v,err_pct,t50_ms,rise_us,ripple_mvpp,ripple_lim,v_ok,ripple_ok
B01,VDD_CORE_0V75,0.75,0.7525,0.33,0.0,287.4,8.8,25.0,True,True
B01,VDD_PHY_0V8,0.8,0.7996,-0.05,0.684,262.0,15.8,25.0,True,True
B01,VDD2_DDR_1V1,1.1,1.1022,0.2,1.295,222.4,11.6,40.0,True,True
```

### 6.2 출력 줄별 해석

- **`[backend] ... pyvisa unavailable (ModuleNotFoundError), using mock`**: 실제 VISA 주소를 줬지만 pyvisa가 없어 mock으로 대체 — 같은 드라이버 코드가 실장비/mock 양쪽에서 돈다는 설계의 증거. 실장비라면 `pip install pyvisa pyvisa-py` 후 같은 주소로 연결 [6].
- **`*IDN?`**: 결과 메타데이터에 남길 장비 신원 (실제 랩에서는 시리얼·펌웨어·교정일까지).
- **ground-lead demo**: 같은 rail, 같은 리플(mock 모델의 1 MHz 성분)인데 `long + full BW = 78.1 mVpp` vs `spring + 20 MHz = 8.9 mVpp` — 체인 E의 "200 mV vs 20 mV" 현상을 숫자로. `long + BWL ON = 13.9`: BW limit이 ringing 대부분을 지우지만 완전히는 아님 → **접지를 고치는 게 1순위, 필터는 보조**. (숫자는 mock 모델이 만든 값이며 실제 보드 측정값이 아님.)
- **SCPI log**: 전원 인가 캡처 설정 순서 — 시간축 1 ms/div, 4채널 DC coupling·BWL OFF(ramp 모양은 넓은 대역으로), 트리거 CH1 0.375 V(= 0.75 V의 50%) rising, `:SING`(single) → PSU `OUTP ON` → `:TER?` = 1(트리거됨) → 파형 전송(`:WAV:XINC?` = 5 µs/pt = 10 ms / 2000 pt).
- **Per-rail B01**: `t50` CORE = 0.000 (트리거 기준이니 정의상 0), 나머지는 0.6–0.9 ms 간격으로 증가 = 순서 정상. 두 번의 캡처(bank 1: PHY/VDD2/VDDQ, bank 2: IO/3V3/PGOOD)가 **같은 t=0**을 공유해 한 표로 합쳐졌다. `rise us` 195–331 µs = 램프 slew, `PGOOD after last rail t90: 0.922 ms` = PGOOD가 모든 rail 안정 후 약 0.9 ms 뒤 → 리셋 해제 조건 충족(BV05).
- **B03**: VDD2 1.034 V (−6.0%) → 전압 판정만 FAIL. 실제라면 피드백 저항·PMIC OTP 설정·부하 IR drop 순으로 의심 (BV03).
- **B04**: `first wrong: VDD_IO_1V8` — IO가 CORE보다 먼저(t50 음수, pre-trigger 구간에서 잡힘). **pre-trigger를 두지 않았다면 IO 램프를 화면 밖에서 놓쳤을 것** — 레시피에 pre-trigger를 넣는 이유.
- **B05**: CORE 리플 41.5 mVpp > 25 — spring tip + BWL로 잰 값이라 artifact가 아니라 실제 문제로 판단(측정 조건이 결과 행에 고정돼 있어야 이런 주장이 가능).
- **B06**: 입력 2.00 A = 한계, 전압 0.10 V로 붕괴 → CC 모드 → **즉시 OFF 후 abort** (scope 캡처·rail 판정 자체를 건너뜀). 첫 전원 인가 current limit의 자동화 버전.
- **`2/6 boards PASS`**, CSV 30행(5 보드 × 6 rail; B06은 측정 전 중단이라 행 없음) — 대시보드는 이 CSV로 보드 간 분포를 그린다.

### 6.3 확장 [미실행]

- 실장비 연결 시: `pyvisa.ResourceManager("@py")`로 pyvisa-py 백엔드, scope 벤더 SCPI 문법으로 `Scope` 클래스만 교체(드라이버 계층 분리의 이유).
- 각 보드 N회 반복 → rail 타이밍 산포(체인 A-꼬리2), 온도 챔버 SCPI 추가 → 온도별 표 (BV12 PVT).

## 7. 자주 틀리는 것

1. **scope BW = 신호 주파수면 충분하다** — 디지털 엣지는 기본 주파수보다 훨씬 높은 성분. 상승 시간 기준 3–5배 [1].
2. **프로브 대역폭을 잊음** — 시스템 BW는 scope + 프로브 [1].
3. **긴 ground clip으로 ripple 측정** — ringing·픽업 artifact를 rail noise로 오판 [1].
4. **BW limit을 고속 엣지 측정에서도 켜 둠** — ripple엔 좋지만 엣지·클럭 측정에선 상승 시간을 망친다. 측정마다 설정을 기록.
5. **긴 시간축에서 글리치를 못 봤으니 없다** — 샘플 속도가 떨어졌을 수 있음 → peak detect / glitch 트리거.
6. **logic analyzer decode를 그대로 믿음** — 임계값 근처 아날로그 문제는 LA가 가짜 엣지로 보여 줌.
7. **PSU current limit 없이 첫 전원 인가** — 단락 시 부품·트레이스 손상. 한계 + CC 감시 + 즉시 OFF.
8. **DMM 직렬로 sleep 전류** — burden voltage, 레인지 전환 글리치.
9. **rise time 기준 %를 안 맞춤** — I2C 30–70%, 일반 10–90%, 일부 고속 20–80%.
10. **SCPI 에러 큐를 안 읽음** — 오타 명령이 조용히 무시되고 이전 설정으로 측정 → 잘못된 데이터가 DB에.

## 8. 출처

1. Tektronix, "ABCs of Probes" primer (five-times rule, Tr = 0.35/BW, 3:1 → 5%·5:1 → 2% 오차, 11 pF + 6인치 ground lead ≈ 140 MHz ringing, ground lead 자가 진단, 1× probe) — PDF 텍스트에서 확인 · https://docs.ampnuts.ru/eevblog.docs/Tektronix/pub/ABCs%20of%20Probes.pdf
2. Keysight, "Evaluating Oscilloscope Bandwidths for Your Application" (5989-5733EN: Gaussian 0.35/BW, 상수 차이) · https://helpfiles.keysight.com/sp/PD1000A/Content/PD1500A%20DPT%20Tests/5989-5733EN.pdf · Keysight glossary "What is oscilloscope bandwidth" (5배 권장) · https://www.keysight.com/used/ph/en/knowledge/glossary/oscilloscopes/what-is-oscilloscope-bandwith
3. Keysight E36300 Series Programming Guide (APPLy, MEASure:CURRent? 형태 — 목차 발췌로 확인, current limit 명령 문법은 미확인) · https://xdevs.com/doc/HP_Agilent_Keysight/E36300A/E36311-90008_programming_guide.pdf
4. Tektronix TPR1000/TPR4000 Power Rail Probe datasheet (20 MHz BWL 시 <300 µVpp 시스템 noise — 검색 결과 발췌로 확인, full-BW 수치는 사본마다 1 / 1.3 mVpp로 다름) · https://www.farnell.com/datasheets/2831711.pdf
5. Tektronix P6137 probe instruction manual (ground lead ≈ 140 nH vs low-inductance lead ≈ 32 nH — 검색 결과 발췌로 확인) · https://docs.ampnuts.ru/eevblog.docs/Tektronix/TEK%20P6137%20INST.pdf
6. PyVISA documentation — Installation / backends (NI-VISA 래퍼, pyvisa-py) · https://pyvisa.readthedocs.io/en/latest/introduction/getting.html
7. TI SLVA689 (I2C 상승 시간 30–70% 기준 — BV10과 공유) · https://www.ti.com/lit/SLVA689
8. BV10 (BER 통계, 인터페이스별 측정 포인트) · [2026-11-01_BV10_board_interface_tests.html](2026-11-01_BV10_board_interface_tests.html)
