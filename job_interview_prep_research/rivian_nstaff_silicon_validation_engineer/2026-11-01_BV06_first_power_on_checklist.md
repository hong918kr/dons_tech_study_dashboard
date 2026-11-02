# BV06 · 첫 전원 인가 날 체크리스트 — "보드가 방금 도착했다, Day 1을 말해 봐"
<!-- theme: pastel -->

> **묶음**: C 첫 전원 인가·JTAG·부트 · **우선**: P0 ★ 면접 1순위 ("The board just arrived. Walk me through Day 1.") · **선행 노트**: [BV02 회로도 읽기](2026-11-01_BV02_schematic_layout_reading.html), [BV03 전원 트리](2026-11-01_BV03_power_tree_sequencing_pmic.html), [BV04 클럭](2026-11-01_BV04_clocks_osc_pll_refclk.html), [BV05 리셋·strap](2026-11-01_BV05_reset_strap_boot_mode.html) · **다음 노트**: [BV07 JTAG·SWD·boundary scan](2026-11-01_BV07_jtag_swd_boundary_scan.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), [BV01 큰 그림](2026-11-01_BV01_si_board_validation_big_picture.html), [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html), [BV14 검증 계획](2026-11-01_BV14_board_validation_plan_automation.html), [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html), [EL P1 bring-up 순서 (Linux 쪽)](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html) · **코드**: [bv06/bringup_log.py](onsitePrep/bv06/bringup_log.py) (Day-1 체크리스트 평가 + bring-up log 생성, mock 데이터, `python3 -I` 실행 PASS, 출력 [bv06/bringup_log.out](onsitePrep/bv06/bringup_log.out))

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"The board just arrived — walk me through Day 1"**, "전원 넣기 전에 뭘 재나?", "전류 제한은 얼마로?", "전원은 OK인데 JTAG이 안 보인다", "JTAG은 되는데 SRAM 코드가 아무것도 안 찍는다", "보드 5장 중 2장이 다르게 동작한다", "하나뿐인 A0 칩을 어떻게 안 태우나?"
- **외울 것 ① — 사다리 (아래가 증명되기 전엔 위를 디버그하지 않는다)**: Day 0 준비 → **S1 육안·현미경 → S2 rail-GND 저항 → S3 전류 제한 첫 전원(단계적으로 올림) → S4 rail 전압 → S5 순서·PGOOD → S6 클럭 → S7 리셋 해제 → S8 JTAG(IDCODE·halt·SRAM R/W) → S9 SRAM 코드(GPIO 토글·UART "hello") → S10 boot 장치 → S11 DDR → S12 인터페이스**.
- **외울 것 ② — 각 단계엔 "통과 기준"과 "실패 시 다음 행동"이 미리 적혀 있다.** 측정값은 **예상값(회로도·전력 예산에서 미리 계산)** 과 **golden 보드** 둘 다와 비교한다. 기록은 **보드 SN·리비전·rework·측정값·시간**을 bring-up log에.
- **외울 것 ③ — A0 실리콘 보호 규칙**: 전류 제한은 낮게 시작, 처음엔 **가장 상태 좋아 보이는 보드 한 장**만, 한 번에 **하나만 바꾸기**, 프로브 미끄러짐 금지(납땜한 wire·hook), ESD, 열화상, PMIC/OTP 설정 먼저 확인, 첫 부팅은 **안전한 boot mode(JTAG/다운로드 모드)** 로.
- **Day 1의 현실적 목표**: 보통 **S1–S9 ("전원 → JTAG → SRAM에서 hello")** 까지. DDR(S11)은 며칠–몇 주 걸리는 별도 프로젝트 ([BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)).
- **30초 영어 답**: "Before the board even arrives I turn the schematic into a checklist — every rail with its expected voltage, sequence, resistance to ground and current — and I have test firmware that already ran on the FPGA prototype. On Day 1 I go bottom-up and don't move to the next layer until the current one is proven: visual inspection, rail-to-ground resistance with power off, then power on from a current-limited supply with a low limit that I raise in steps while watching current and a thermal camera. Then I check every rail voltage, the sequence and power-good on a scope, the oscillator, and that reset releases after the rails are stable. Then JTAG: read the IDCODE, halt the core, write and read SRAM. Then I load a tiny program into SRAM that toggles a GPIO and prints hello on the UART. Only after that do I go to the boot device, DDR and the interfaces. Every measurement goes into a log per board serial number, compared with the expected value and the golden board."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "응급실에 처음 들어온 환자를 검사하는 순서"

새 보드는 **처음 보는 환자**다. 이 환자에 대해 아는 것은 설계도(회로도)뿐이고, 실제로 그 설계대로 만들어졌는지는 아무도 모른다.

1. **겉모습 보기 (육안·현미경 검사)**: 팔다리가 다 붙어 있나, 뒤집혀 붙은 부품은 없나, 땜납이 옆 핀과 붙어(솔더 브리지) 있지 않나.
2. **피 뽑기 전에 혈관 확인 (rail-GND 저항 측정)**: 전원을 넣기 전에, 전원선과 땅(GND) 사이가 **합선**돼 있지 않은지 멀티미터로 잰다. 합선된 채 전원을 넣으면 부품이 탄다.
3. **수액을 아주 천천히 (전류 제한 첫 전원)**: 전원 공급기에 "최대 전류 이만큼까지만"이라는 제한을 걸고 켠다. 이상하면 공급기가 알아서 전류를 막는다. 괜찮으면 제한을 조금씩 올린다.
4. **혈압·맥박 (rail 전압·클럭)**: 각 전압선(rail)이 목표 전압에 들어왔나, 심장 박동(클럭)이 뛰나.
5. **의식 확인 (리셋 해제 → JTAG)**: 칩이 깨어났나? 디버거로 "너 누구니?"(IDCODE)를 묻고, CPU를 잠깐 멈추고(halt) 메모리에 써 보고 읽어 본다.
6. **"제 말 들리면 손 들어 보세요" (GPIO 토글·UART hello)**: 아주 작은 프로그램을 칩 내부 SRAM에 넣고 돌려서, LED를 깜빡이고 시리얼 포트로 "hello"를 찍게 한다. 이게 되면 CPU·클럭·핀 설정·UART가 다 산 것.
7. **정밀 검사 (boot 장치·DDR·인터페이스)**: 그다음에야 플래시 부팅, 큰 메모리(DDR), 카메라·네트워크·PCIe 같은 장기들을 하나씩.

핵심 규칙: **혈압도 안 쟀는데 뇌 MRI부터 찍지 않는다.** 아래 계층(전원)이 증명되지 않았는데 위 계층(소프트웨어) 증상을 디버그하면 사흘을 날린다.

### 1.2 전체 그림 — Day 0 (준비)와 Day 1 (첫 전원)

```text
 ┌──────────────────────── Day 0 : 보드 도착 전 (1~2주) ────────────────────────┐
 │ 회로도 → 체크리스트 · rail 표(전압/허용오차/순서/예상 저항/예상 전류)        │
 │ FPGA에서 검증된 테스트 FW (SRAM 전용 이미지, GPIO, UART, mem test, JTAG 스크립트)│
 │ 장비 준비 (PSU, DMM, scope, 열화상, 디버거, USB-UART, ESD)                   │
 │ bring-up log 양식 · 보드 SN/리비전 · rework 기록 · golden 보드 정하기          │
 └─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼  (전원 OFF 상태)
 S1 육안·현미경 ──▶ S2 rail-GND 저항 ──┐
                                      ▼  (전원 ON, 전류 제한)
 S3 첫 전원(제한 낮게→단계적) ──▶ S4 rail 전압 ──▶ S5 순서·PGOOD ──▶ S6 클럭 ──▶ S7 리셋 해제
                                                                                   │
                                      ┌────────────────────────────────────────────┘
                                      ▼  (칩이 깨어남)
 S8 JTAG: IDCODE · halt · SRAM R/W ──▶ S9 SRAM 코드: GPIO 토글 · UART "hello"
                                      │        ★ 여기까지가 보통 "Day 1"
                                      ▼
 S10 boot 장치(flash) ──▶ S11 DDR training·memtest ──▶ S12 인터페이스 (I2C·SPI·ETH·PCIe·MIPI)
                                      │
                                      ▼
            Linux bring-up (EL P1 노트) · 특성화/PVT (BV12) · 자동화 (BV14)

 규칙: 한 단계 FAIL → 그 단계에서 멈춤. 다음 단계로 넘어가지 않는다 (gate).
```

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Bring-up | — | 새 보드/새 칩을 처음 살려서 "기본 동작함"까지 만드는 과정 | 이 포지션의 핵심 업무 |
| Smoke test | — | 처음 전원 넣고 "연기 안 나나" 보는 첫 시험 (비유적 표현) | Day 1의 첫 관문 |
| Rail | power rail | 보드 위 특정 전압을 나르는 전원선 (예: 0.75 V 코어, 1.8 V IO) | SoC는 rail 여러 개를 먹는다 |
| GND | ground | 0 V 기준 (모든 전압의 기준점) | rail-GND 사이가 합선이면 위험 |
| DMM | Digital MultiMeter | 전압·저항·전류를 재는 손 측정기 | 저항 측정·rail 전압 |
| Bench PSU | bench Power Supply Unit | 책상용 전원 공급기, 전압과 **전류 제한**을 설정 가능 | 보드 보호의 1차 방어선 |
| Current limit / CC mode | Constant Current mode | 설정 전류에 도달하면 PSU가 전압을 낮춰서 전류를 그 값으로 묶음 | 합선 시 부품이 타는 것을 막음 |
| Inrush current | — | 켜는 순간 커패시터가 충전되며 잠깐 흐르는 큰 전류 | 제한을 너무 낮게 걸면 정상 보드도 CC로 빠짐 |
| PMIC | Power Management IC | 여러 rail을 만들고 켜는 순서를 지켜 주는 전원 관리 칩 | 순서·전압 설정(OTP)이 틀리면 보드가 안 산다 |
| OTP | One-Time Programmable | 칩 안에 한 번만 굽는 설정 (PMIC 기본 전압·순서 등) | 샘플 PMIC의 OTP 버전이 다를 수 있다 |
| PGOOD | Power Good | 레귤레이터가 "내 출력 전압이 목표 범위에 들어왔다"고 알리는 신호선 | 다음 rail·리셋 해제의 조건 |
| Sequencing | power sequencing | rail들이 켜지는 **순서와 간격** | 틀리면 칩 오동작·손상 ([BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html)) |
| Test point (TP) | — | 측정용으로 일부러 만든 보드 위 작은 패드 | 어디서 잴지 미리 정해 둔다 |
| Shunt resistor | current-sense resistor | 전류를 재려고 rail에 직렬로 넣은 아주 작은 저항 (예: 10 mΩ) | rail별 전류 측정 |
| 0 Ω resistor / jumper | — | 저항값 0인 부품, 떼면 회로가 끊긴다 | rail을 분리해서 합선 위치 찾기 |
| DNP | Do Not Populate | 회로도엔 있지만 실장하지 않는 부품 | 옵션 회로, BOM 확인 |
| Rework | — | 보드 부품을 손으로 떼거나 붙이거나 선을 덧대는 수정 작업 | 반드시 기록 (보드 간 차이의 원인) |
| SN / Rev | Serial Number / Revision | 보드 일련번호 / 설계 버전 (Rev A, B…) | 측정값을 보드별로 추적 |
| Golden board | — | "정상"으로 확인된 기준 보드 | 이상한 보드와 측정값 비교 |
| A0 silicon | — | 첫 번째로 제조된 칩 버전 (stepping A0) | 수량이 매우 적고 귀하다 |
| Thermal camera | 열화상 카메라 | 온도를 색으로 보여 주는 카메라 | 합선·과열 부품을 손대지 않고 찾음 |
| Oscilloscope (scope) | — | 전압을 시간에 따라 그려 주는 장비 | rail 순서·리셋 타이밍·클럭 ([BV11](2026-11-01_BV11_lab_instruments_measurement.html)) |
| JTAG | Joint Test Action Group (IEEE 1149.1) | 칩에 달린 디버그·테스트용 직렬 포트 | 칩이 부팅 못 해도 들어갈 수 있는 뒷문 ([BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)) |
| IDCODE | identification code | JTAG으로 읽는 칩의 32비트 신분증 | "칩이 살아 있고 JTAG 배선이 맞다"의 첫 증거 |
| Halt | — | 디버거가 CPU를 멈추는 것 | 레지스터·메모리 관찰 가능 |
| SRAM (on-chip) | Static RAM | 칩 **내부** 작은 메모리, 초기화 없이 바로 쓸 수 있음 | DDR 전에 코드를 돌릴 수 있는 유일한 장소 |
| UART | Universal Asynchronous Receiver/Transmitter | 가장 단순한 시리얼 통신 (TX/RX 두 선) | "hello" 첫 글자 = 살아 있다는 신호 |
| BootROM | — | 칩 안에 구워진 최초 부트 코드 | strap 핀을 보고 boot 장치를 고른다 ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)) |
| Strap pin | — | 리셋 해제 순간 값이 읽혀 boot mode 등을 정하는 핀 | 첫날엔 안전한 boot mode로 설정 |
| ESD | ElectroStatic Discharge | 정전기 방전 | 칩이 소리 없이 죽는다 → 손목 스트랩 |
| EVT/DVT/PVT | Engineering/Design/Production Validation Test | 보드 개발 단계 이름 | Day 1은 보통 EVT 첫 보드 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Day 0 — 보드가 오기 전에 준비할 것

> 면접 포인트: "Day 1"을 물으면 **Day 0부터 말해야** Staff 레벨 답이 된다. 준비된 사람만 Day 1에 하루에 S1–S9를 끝낸다.

| 산출물 | 내용 | 어디서 나오나 | 왜 |
|---|---|---|---|
| Bring-up 체크리스트 | S1–S12 단계, 단계별 측정 위치(TP)·장비·통과 기준·실패 시 행동 | 회로도·SoC 데이터시트·HW 엔지니어 리뷰 ([BV02](2026-11-01_BV02_schematic_layout_reading.html)) | 당일엔 생각하지 말고 실행만 |
| Rail 표 | rail 이름·전압·허용 오차·순서·ramp·PGOOD·측정 TP·shunt 위치 | power tree 페이지, SoC power sequence 표 ([BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html)) | S4·S5의 기준값 |
| 예상 rail-GND 저항 | rail별 "이 정도면 정상" 범위 (코어 rail은 낮게 나와도 정상일 수 있음) | 첫 보드라 모르면 → **여러 보드 비교 + 시간에 따른 변화**로 판단 | S2의 기준값 |
| 예상 전류 표 | 상태별 입력 전류: 리셋 유지 중 / 리셋 해제 후 idle / CPU 루프 / 부하 | 전력 예산(power budget), SoC 데이터시트, FPGA·이전 칩 경험 | S3 전류 제한 단계 설정 |
| 테스트 FW | SRAM 전용 이미지(벡터·스택·UART·GPIO), mem test, 레지스터 dump, JTAG 스크립트 | **FPGA/에뮬레이터에서 이미 돌려 본 것** ([BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)) | 실리콘 날 새 코드 디버그 금지 |
| 디버거 설정 | Trace32 `.cmm` / OpenOCD `.cfg` (IR 길이·expected IDCODE·TCK 속도) | BSDL·SoC 디버그 문서 | S8을 바로 시작 |
| 장비 | 전류 제한 PSU(가능하면 2–4채널), DMM, 4채널+ scope(짧은 ground spring), 열화상, 디버거, USB-UART(1.8 V/3.3 V 레벨 확인), 납땜기·현미경, ESD 매트·스트랩 | 랩 | 당일 장비 찾느라 시간 낭비 금지 |
| 기록 체계 | bring-up log 양식, 보드별 상태표, 이슈 트래커, rework 기록, 사진 폴더 | 팀 표준 ([BV14](2026-11-01_BV14_board_validation_plan_automation.html)) | 보드 간 차이 추적, 다음 리비전 피드백 |
| 보드 준비 | 보드 SN 스티커, 리비전·BOM 버전·칩 lot/샘플 ID 기록, 점퍼 기본 위치 표 | 조립 업체 정보 | "어떤 보드에서 무엇을 했나" 추적 |

**예상 전류 표 예시** (가상의 자동차 AI SoC validation 보드, 12 V 입력 — 전부 `[예시]` 값):

| 상태 | 예상 VIN 전류 (12 V) | 근거 | S3에서 쓰는 법 |
|---|---|---|---|
| PMIC만 동작, SoC rail off | 10–30 mA | PMIC 대기 전류 + LED | 첫 제한 0.1 A에서 여기서 멈춰야 정상 |
| 모든 rail on, SoC 리셋 유지 | 0.2–0.3 A | 누설 전류(leakage) + IO + DDR 대기 | 두 번째 제한 0.3–0.5 A |
| 리셋 해제, BootROM idle | 0.3–0.6 A | 코어 클럭 동작 시작 | 리셋 해제 후 증가량 확인 |
| CPU 루프 + DDR training | 1–2 A | 동적 전력 | DDR 단계에서 제한 상향 |
| 최대 부하 (NPU·카메라) | 5 A+ | 전력 예산 | 특성화 단계, 첫날 아님 |

- 숫자를 모르면: **낮게 시작해서 단계적으로 올린다**. 정상 보드도 켜는 순간 inrush 때문에 낮은 제한에 걸릴 수 있으니, "제한에 닿았다" 자체보다 **제한에 붙어서 계속 머무르는지(CC 모드 유지)** 를 본다 [4].

### 2.2 Day 1 체크리스트 — 단계별 통과 기준과 실패 시 행동

| 단계 | 무엇을 | 어떻게 (장비·위치) | 통과 기준 | 실패 시 다음 행동 |
|---|---|---|---|---|
| S1 육안 | 부품 누락·방향·솔더 브리지·이물질 | 현미경, 조립도(assembly drawing) 출력해서 대조 [1] | 체크리스트 전 항목 OK, 사진 기록 | rework 후 다시 S1 (rework 기록) |
| S2 rail-GND 저항 | 전원 OFF에서 각 rail과 GND 사이 저항 | DMM 저항 모드, rail TP ↔ GND TP, 같은 범위·같은 극성 | 0 Ω 근처 아님, 예상 범위·다른 보드와 비슷함 [1][4] | **전원 금지**. 0 Ω/0 Ω 근처면 합선 → 0 Ω 저항·ferrite 떼서 rail 분할, 열화상(저전압·전류 제한 주입) |
| S3 첫 전원 | 입력에 전류 제한 걸고 켜기 | bench PSU (전압 = 보드 정격, 제한 낮게), 열화상, 냄새·소리 | 전류가 제한 아래로 안정, 예상 전류의 ~1.5배 이내, 핫스팟 없음 | CC 모드 유지·전압 붕괴·과열 → 즉시 OFF → S2로 돌아감 [4] |
| S4 rail 전압 | 각 rail 전압 | DMM, **부하 쪽** TP (레귤레이터 출력이 아니라 SoC 근처) | 허용 오차(예: ±3%) 이내 [1] | 낮음/없음: EN 핀·PGOOD·피드백 저항·인덕터 솔더 확인 [4] / **높음: 즉시 OFF** (아래 칩 보호) |
| S5 순서·PGOOD | rail이 켜지는 순서·간격·ramp, PGOOD 체인 | scope 4–8채널, 트리거 = 첫 rail 상승, 각 rail 10%/90% 시점 | SoC 데이터시트 순서·간격·ramp 시간 만족, 모든 PGOOD high | PMIC fault/status 레지스터(I2C) 읽기, OTP 버전·sequence 설정 확인 ([BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html)) |
| S6 클럭 | 메인 오실레이터(예: 24 MHz) 동작 | scope/주파수 카운터, **오실레이터 출력** 또는 SoC 클럭 출력 핀 (crystal 핀 직접 찍지 않기) | 주파수·진폭 정상, ppm 오차 예산 이내 [1] | osc 전원·enable, crystal load cap, 실장 확인 ([BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html)) |
| S7 리셋 해제 | 리셋 핀이 rail·클럭 안정 **후** 풀리나 | scope: 마지막 PGOOD ↔ reset 핀, strap 핀 레벨 | 데이터시트 최소 지연 이상, strap 값이 의도한 boot mode | 누가 리셋을 잡고 있나(supervisor, 디버거 SRST, watchdog), strap 저항 ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)) |
| S8 JTAG | IDCODE 읽기 → core halt → 레지스터 읽기 → SRAM write/read | Trace32/OpenOCD/J-Link, **TCK 낮게 시작 (예: 100 kHz–1 MHz)** | IDCODE = 기대값, halt OK, PC가 BootROM 영역, SRAM 패턴 일치 | VTref·TCK·TDO 파형·chain 설정·리셋 상태·debug 전원 도메인 ([BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)) |
| S9 SRAM 코드 | JTAG으로 SRAM에 작은 이미지 로드 → PC 설정 → 실행: GPIO 토글, UART "hello" | 디버거 load, scope로 GPIO, USB-UART 터미널 | GPIO 주기 = 계산값, UART 문자열 정확 | UART: pinmux·클럭(baud 오차)·TX/RX 뒤바뀜·레벨(1.8 vs 3.3 V); 코드: JTAG로 PC·fault 레지스터 |
| S10 boot 장치 | flash(QSPI/eMMC/UFS)에서 부팅 | JTAG/SRAM 로더로 flash 프로그래밍, strap을 flash boot로 | BootROM이 이미지 로드, FSBL 첫 로그 | BootROM 에러 코드·boot 로그, 이미지 헤더, flash ID 읽기 ([BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)) |
| S11 DDR | 초기화·training → memtest | 벤더 training 도구/FSBL, 낮은 주파수부터 | training PASS, 전 영역 memtest 0 error | training 로그·파라미터·VREF·온도 ([BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)) |
| S12 인터페이스 | I2C 스캔, SPI flash ID, Ethernet PHY ID·링크, PCIe 링크, MIPI 카메라 | 테스트 FW 메뉴, protocol analyzer | 장치 ID·링크 상태·loopback·PRBS 통과 | [BV10](2026-11-01_BV10_board_interface_tests.html), PCIe는 [LP15](2026-10-31_LP15_debug_scenarios.html) |

### 2.3 단계별 세부 — 면접에서 "왜?"를 물을 때

#### S1 육안·현미경 검사

- 조립도(어떤 부품이 어디에 어떤 방향으로)와 BOM을 출력해서 **한 부품씩 대조** — 특히 극성 있는 부품(다이오드, 탄탈 커패시터, 전해 커패시터), IC의 1번 핀 표시, 커넥터 방향 [1].
- BGA(공 모양 납땜이 칩 밑에 있는 패키지, SoC·DDR)는 눈으로 안 보인다 → 조립 업체의 **X-ray 검사 결과**를 받아 둔다 (첫 빌드는 요청하는 게 일반적 `[추정]`).
- 보드 SN 스티커, 사진 촬영(앞/뒤) — 나중에 "이 보드만 이상해"일 때 사진이 증거다.
- 점퍼·DIP 스위치 기본 위치 확인: **boot mode strap은 첫날 "JTAG/다운로드 모드"** 로 두는 게 안전하다 — BootROM이 빈 flash나 쓰레기 이미지를 실행하지 않게.

#### S2 rail-GND 저항 (전원 OFF)

- 각 rail TP와 GND 사이를 DMM으로 잰다. **0 Ω 근처면 합선** — 절대 전원을 넣지 않는다 [1][4].
- 함정: 큰 SoC의 **코어 rail은 정상이어도 수 Ω–수십 Ω로 낮게 나올 수 있다** (거대한 칩의 내부 회로 + 수많은 decoupling 커패시터) [1]. 그래서 **절대값보다 비교**가 중요: 같은 rail을 여러 보드에서, golden 보드와, 같은 DMM 범위로.
- 함정 2: 커패시터 때문에 **읽는 값이 천천히 올라간다** — 값이 계속 올라가면 정상 충전, 0에 붙어 있으면 합선 [4]. 측정 극성(빨강/검정 방향)에 따라 ESD 다이오드 때문에 값이 다를 수 있으니 **항상 같은 방향**으로 잰다.
- 합선을 찾으면: (1) 0 Ω 저항·ferrite bead를 떼서 rail을 구간으로 나누고 어느 쪽에 합선이 있는지 좁힌다 (2) 합선된 rail에 **아주 낮은 전압·전류 제한**(예: 수백 mV, 1 A 제한 `[예시]`)을 걸고 열화상으로 뜨거워지는 부품을 찾는다 — 흔히 쓰는 short-hunting 기법 `[추정: 일반 관행]` (3) mΩ 단위 측정기로 plane을 따라 전압 강하가 가장 큰 쪽을 찾는다.

#### S3 전류 제한 첫 전원 — 단계적으로

```text
 전류 제한 단계 (예: 12 V 입력, 예상 "리셋 유지 중" 전류 0.25 A) [예시]

 단계   제한     관찰                                         판단
 ─────  ───────  ─────────────────────────────────────────  ─────────────────────────────
  1     0.10 A   켜는 순간 0.10 A에 닿았다가?                 inrush면 곧 내려온다 → 정상
                 계속 0.10 A + 전압 하강(CC 모드 유지)          → 예상 전류가 0.1 A보다 크면 다음 단계로
  2     0.30 A   0.23 A에서 안정, 열화상 이상 없음              ✅ 예상(0.25 A) 근처 → S4로
                 0.30 A에 붙음(CC) + 핫스팟                     ❌ 즉시 OFF → S2로, 핫스팟 부품 조사
  3     1.0 A    (리셋 해제·DDR 단계에서 필요할 때만)            예상치의 1.5–2배를 넘지 않게
```

- **왜 전류 제한인가**: 합선·뒤집힌 부품이 있으면 전류가 폭주한다. 제한이 없으면 PSU가 수 A–수십 A를 밀어 넣고 **A0 칩·PMIC·기판 패턴이 탄다.** 제한이 있으면 PSU가 전압을 낮춰서(CC 모드) 손상을 막는다.
- **너무 낮으면?** 정상 보드도 CC로 빠져서 전압이 덜 올라가고, PMIC가 UV(저전압) fault로 꺼지거나 rail이 반쯤 켜진 이상한 상태가 된다 → "보드 고장"으로 오판. 그래서 **예상 전류 표가 필요**하다.
- **가능하면 rail별로 따로**: validation 보드는 PMIC를 끄고(점퍼) **각 rail을 외부 PSU 채널로 직접 공급**하는 옵션이 있는 경우가 많다 `[추정: 보드 설계에 따라]` — rail 하나씩 켜면서 rail별 전류를 볼 수 있다. 순서는 반드시 SoC 데이터시트 순서를 지킨다.
- 관찰: PSU 전류 표시, 열화상(첫 30초–1분), 냄새·소리. **부품을 손으로 만져서 온도를 보지 않는다** (화상·ESD·쇼트) [4].

#### S4–S5 rail 전압·순서·PGOOD

- DMM으로 **SoC 근처(부하 쪽)** TP에서 잰다. 레귤레이터 출력에서만 재면 배선 저항에 의한 강하(IR drop)를 놓친다.
- scope로 순서: 채널마다 rail 하나, 트리거 = 첫 rail 상승 에지, 시간축 = 전체 시퀀스가 들어가게(보통 수 ms–수십 ms `[예시]`). 볼 것: 순서, 간격, ramp(상승 시간), overshoot, **중간에 멈칫(plateau)하거나 다시 떨어지는 rail**(부하가 켜질 때 droop → PMIC가 다시 시작하는 반복).
- PMIC가 I2C/PMBus로 접근 가능하면: **ID·OTP 버전·rail 설정·fault 레지스터(OV/UV/OC/OT)** 를 읽어 로그에 남긴다. 리눅스가 아직 없으니 I2C 어댑터(USB-I2C) 또는 나중에 JTAG로.
- 자세한 측정법(ripple 프로빙, ground spring)은 [BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html), [BV11](2026-11-01_BV11_lab_instruments_measurement.html).

#### S6 클럭 · S7 리셋

- 오실레이터 출력 핀이나 **SoC의 clock-out(debug clock) 핀**을 잰다. crystal 핀(XTAL_IN/OUT)을 scope 프로브로 직접 찍으면 프로브의 커패시턴스가 발진을 멈추게 할 수 있다 ([BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html)).
- 리셋: scope 2채널 = 마지막 rail PGOOD + SoC reset 핀. 리셋이 **너무 일찍** 풀리면(rail이 덜 안정) 칩이 이상한 상태로 시작하고, **아예 안 풀리면** (supervisor가 잡고 있음, 디버거가 SRST를 잡고 있음) 아무것도 안 된다.
- strap 핀: 리셋 해제 순간의 레벨을 scope로 같이 찍는다 — 다른 칩이 그 순간 그 핀을 driving하고 있을 수 있다 ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)).

#### S8 JTAG — "칩이 살아 있다"의 첫 증거

순서 (각각이 앞 단계를 증명):

1. **VTref** (디버거가 보는 타깃 IO 전압) 확인 → 디버거가 맞는 레벨로 구동.
2. **IDCODE 읽기** → JTAG 배선·TAP·칩의 debug 전원이 산 것. 기대값과 비교 (part·version으로 **칩 stepping**도 확인).
3. **DAP 접속 → core halt** → 코어 전원 도메인·클럭·debug 권한이 OK.
4. **레지스터 읽기**: PC가 BootROM 주소 범위면 정상 (BootROM이 boot 장치를 못 찾아 루프 중일 수도 — 그것도 정보).
5. **SoC 정보 레지스터**: chip ID/revision, fuse 값, **reset reason**, PLL lock 상태, strap 샘플 값 레지스터 (있다면).
6. **SRAM write/read**: 여러 패턴(0x55555555, 0xAAAAAAAA, 주소=값)으로 써서 읽기 → 버스·SRAM OK.

막히면 [BV07의 debug tree](2026-11-01_BV07_jtag_swd_boundary_scan.html).

#### S9 SRAM에 코드 로드 → GPIO 토글 → UART hello

- 이미지: **SRAM에서만 도는** 작은 ELF (벡터 테이블, 스택 = SRAM 끝, .bss 클리어, UART 초기화, GPIO 토글 루프). DDR·캐시·MMU 안 씀. FPGA에서 이미 돌린 바로 그 코드가 이상적.
- 순서: halt → `load` ELF → PC = entry (Cortex-M이면 벡터 테이블의 SP/Reset 값) → run.
- **GPIO 토글이 먼저인 이유**: UART는 클럭 설정·baud 분주·pinmux·레벨 시프터·터미널 설정까지 맞아야 하지만, GPIO는 "CPU가 코드를 실행한다"만 증명하면 된다. scope로 토글 주기를 재면 **실제 코어 클럭도 역산**할 수 있다 (루프 사이클 수를 알면).
- UART "hello"가 깨지면 → baud 오차 = 클럭 오차 의심 (기준 클럭이 24 MHz가 아니라 다른 값, PLL 설정) [1]. 아무것도 안 나오면 → TX 핀을 scope로: 파형이 있으면 터미널/케이블 쪽(TX↔RX 교차, 레벨), 없으면 pinmux·UART 클럭 enable.
- JTAG이 있으니 출력이 없어도 **halt해서 PC·fault 레지스터를 본다** — "어디서 죽었나"를 바로 안다. UART가 없으면 semihosting이나 SRAM 링 버퍼 + JTAG 읽기로 대체.

#### S10–S12 boot 장치 · DDR · 인터페이스 (Day 1 이후)

- S10: SRAM 로더(또는 디버거의 flash 알고리즘)로 flash에 이미지를 굽고, strap을 flash boot로 바꿔 BootROM 경로 검증. BootROM이 실패하면 보통 에러 코드/로그를 남기거나 다운로드 모드로 떨어진다 (칩마다 다름).
- S11: DDR은 **training**(신호 타이밍 맞추기)이 필요한 가장 어려운 단계 — 낮은 주파수부터, training 로그를 남기고, March 계열 memtest ([BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)).
- S12: 저속(I2C·SPI·UART·GPIO) → 고속(Ethernet·PCIe·MIPI) 순 ([BV10](2026-11-01_BV10_board_interface_tests.html)).
- 이후 부트로더·커널·DT를 단계적으로 늘리는 Linux 쪽은 **[EL P1 노트](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html)** (QEMU로 단계적 DT bring-up 실습 포함) — 여기서는 반복하지 않는다.

### 2.4 원칙 — 면접에서 "how do you work"를 보여 주는 부분

| 원칙 | 무슨 뜻 | 왜 |
|---|---|---|
| Bottom-up gate | 아래 계층 PASS 전엔 위 계층을 디버그하지 않는다 | 전원이 5% 낮은데 드라이버를 사흘 의심하는 일 방지 |
| Change one thing at a time | rework·설정·FW 중 **하나만** 바꾸고 다시 잰다 | 두 개를 바꾸면 무엇이 고쳤는지/망쳤는지 모른다 |
| Expected value first | 재기 전에 "얼마가 나와야 하나"를 적어 둔다 | 측정값이 맞는지 판단할 기준 |
| Golden board 비교 | 정상 보드와 같은 지점·같은 조건으로 비교 | 첫 보드는 "정상"을 모르므로 비교가 유일한 기준 |
| A-B-A 확인 | 고쳤다고 생각하면 원래대로 되돌려 증상이 다시 나오는지 | 우연의 일치 배제 (재현 = 원인 증명) |
| 기록 | 보드 SN별 log: 날짜·시각·측정값·rework·FW 버전·장비 | 보드 간 차이, 다음 리비전 ECO 근거 |
| 데이터시트 > 기억 | 순서·전압·타이밍은 항상 문서에서 | A0 실리콘은 errata가 있을 수 있음 |

### 2.5 하나뿐인 A0 실리콘 보호하기

- **보드 한 장부터**: 가장 솔더 상태가 좋아 보이는 보드로 시작하고 [1], 그 보드가 S1–S9를 통과한 뒤 나머지에 같은 절차를 반복.
- **보드 먼저, 칩은 나중에** `[추정: 팀 사정에 따라]`: 소켓 보드라면 첫 전원은 **칩 없이**(또는 dummy/이전 칩) rail·순서만 먼저 검증할 수 있다. 솔더 보드라면 칩 없는 bare 보드(일부 실장)로 전원부만 먼저 검증하는 방법도 있다.
- **전류 제한·rail별 감시**, 과전압 rail 발견 시 즉시 OFF (낮은 전압 칩은 과전압에 가장 약하다).
- **PMIC 설정 확인 먼저**: OTP/기본 전압이 이 SoC 용으로 맞는지 (샘플 PMIC는 OTP가 다를 수 있다) — 가능하면 SoC rail 연결 전(0 Ω 떼고) PMIC 출력만 먼저 재 본다.
- **프로빙 안전**: scope 프로브가 미끄러져 인접 핀을 쇼트시키는 게 실제로 흔한 사고 → 측정점에 **wire를 납땜**하거나 hook/스프링 팁, 프로빙 중 전원 OFF로 연결.
- **ESD**: 손목 스트랩, 매트, 보드는 금속 책상 위에 직접 놓지 않고 스탠드오프 위에.
- **열**: 첫 부팅에 heatsink/팬 준비 — DDR training·CPU 루프에서 전력이 급증 [1].
- **소프트웨어로 칩을 망가뜨리는 경로 차단**: fuse/OTP 쓰기, secure boot 키 굽기, 전압 설정 레지스터(AVS) 쓰기 같은 **되돌릴 수 없는 동작은 테스트 FW에서 막아 두기**.
- **"이 보드는 위험" 표시**: 합선 이력·rework 많은 보드는 태그, 다른 사람이 모르고 쓰지 않게.

### 2.6 보드 여러 장이 서로 다를 때

```text
 증상이 몇 장에서?
   ├─ 전부 같은 실패        → 설계(회로도·레이아웃) 또는 실리콘 또는 FW 문제일 가능성 ↑
   │                          (조립 불량이 모든 보드에 똑같이 생길 확률은 낮음)
   ├─ 일부만 실패 (예: 5장 중 2장)
   │     → 차이를 표로: 보드 SN · rework 이력 · BOM 대체 부품 · 칩 lot/샘플 ID · fuse/stepping
   │                    · PMIC OTP 버전 · 측정값(rail 저항·전압·전류) · 장비/케이블/디버거
   │     → swap test: (1) 셋업(PSU·케이블·디버거·FW)을 바꿔도 보드를 따라가나?  → 보드 문제
   │                  (2) 소켓 보드면 칩을 맞바꿔서 증상이 칩을 따라가나?       → 칩 문제
   │                  (3) DDR·PMIC 등 의심 부품을 맞바꿈 (rework)              → 부품 문제
   └─ 한 장에서 가끔 (intermittent)
         → 온도·시간·전원 사이클 상관관계 기록, 냉각 스프레이/열풍으로 자극, 장기 루프 자동화 (BV13·BV14)
```

- 결론은 **A-B-A**로 확인: 증상이 칩을 따라가면, 다시 되돌렸을 때 증상도 돌아와야 한다.
- 결과는 HW 팀에 **증거와 함께** 전달: "SN003, SN005만 VDD_IO_1V8이 2.9% 낮음, 둘 다 같은 대체 LDO(BOM alternate) 실장" 같은 식.

## 3. 보드에서 볼 것 · 잴 것

### 3.1 Bring-up log 양식 (보드마다 하나)

```text
=== BRING-UP LOG ===  board: SN003   rev: A   BOM: v1.2 (alt LDO U17)   SoC: A0 sample #07 (lot ____ )
PMIC OTP: ____   FW: bu_sram_v0.3 (git 1a2b3c4, FPGA-proven 10-28)   engineer: Don   date: 2026-11-__
setup: PSU ch1 12.0 V / lim 0.10→0.30 A, DMM ____, scope ____, debugger Trace32 ____, UART 115200 8N1 @1.8 V

time   step  action / measurement                         expected            result     note
-----  ----  -------------------------------------------  ------------------  ---------  -----------------------
09:10  S1    visual + microscope, photos front/back       no defects          PASS       photo IMG_0412-0413
09:25  S2    VDD_CORE_0V75 -> GND (DMM 200 ohm, red=rail)  > 0.5 ohm, ~golden  1.9 ohm   golden SN001 1.8 ohm
09:40  S3    12 V on, lim 0.10 A                          inrush then CC      CC 0.10 A  expected, raise
09:41  S3    lim 0.30 A                                   ~0.25 A             0.231 A    thermal OK (img)
09:50  S4    VDD_IO_1V8 @ TP31 (near SoC)                 1.80 V +/-5%        1.797 V    PASS
10:05  S5    scope seq, trig VDD_AON rise                 AON>CORE>DDR>IO     FAIL       IO_1V8 before CORE!
10:06  ---   STOP. PMIC reg dump saved pmic_SN003.txt; compare SN001 -> OTP rev differs (__ vs __)
...
REWORK: ____ (who/when/what)        OPEN ISSUES: JIRA BRD-____        STATUS: blocked at S5
```

- 규칙: **값 + 단위 + 측정 위치 + 조건(전압·온도·FW)** 을 같이 적는다. "OK"만 적으면 나중에 쓸모없다.
- 사진·scope 캡처 파일명을 log에 연결. 자동화 단계에선 이 log가 DB 레코드가 된다 ([BV14](2026-11-01_BV14_board_validation_plan_automation.html)).

### 3.2 보드별 상태표 (팀 공유)

| 보드 SN | Rev | 칩 | 현재 단계 | 상태 | 마지막 이슈 | rework |
|---|---|---|---|---|---|---|
| SN001 | A | A0 #03 | S11 DDR | ✅ golden | — | 없음 |
| SN002 | A | A0 #05 | S2 | ❌ 1.8 V rail 합선 | U21 아래 솔더 브리지 의심 | X-ray 요청 |
| SN003 | A | A0 #07 | S5 | 🟡 순서 이상 | PMIC OTP 버전 다름 | PMIC 교체 예정 |
| SN004 | A | A0 #08 | S8 | 🟡 IDCODE 0xFFFFFFFF | TDO 저항 DNP? | 조사 중 |

(`[예시]` — 값은 가상.)

### 3.3 예시 측정값과 해석

**rail 전압 측정** `[예시 출력]`

```text
rail            TP    nominal   tol    measured   err      verdict
VDD_AON_0V8     TP10  0.800 V   ±3%    0.801 V    +0.1%    PASS
VDD_CORE_0V75   TP12  0.750 V   ±3%    0.748 V    -0.3%    PASS
VDD_DDR_1V05    TP20  1.050 V   ±3%    1.052 V    +0.2%    PASS
VDD_IO_1V8      TP31  1.800 V   ±5%    1.797 V    -0.2%    PASS
VDD_IO_3V3      TP33  3.300 V   ±5%    3.310 V    +0.3%    PASS
```

- 줄마다: 어떤 TP에서(부하 쪽), 기준과 허용 오차를 같이 적었다. 오차가 허용 범위 안이어도 **보드 간 경향**(예: 모든 보드에서 코어가 -2.5%)이 있으면 HW 팀에 알린다 — 부하가 걸리면 더 떨어질 수 있다(droop).

**전원 순서 scope 캡처** `[예시 파형]`

```text
 time(ms)   0    1    2    3    4    5    6        26
            │    │    │    │    │    │    │   ...   │
 VDD_AON   _/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   90% @ 0.8 ms
 VDD_CORE  ______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   90% @ 2.1 ms
 VDD_DDR   ___________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   90% @ 3.4 ms
 VDD_IO    ________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   90% @ 4.6 ms (1V8), 5.9 ms (3V3)
 PGOOD_ALL _____________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 SOC_RST_N ____________________________________/‾‾‾‾‾‾   released @ 26 ms (≥ 10 ms after last rail)
```

- 해석: 순서 AON → CORE → DDR → IO, PGOOD 이후 약 20 ms 뒤 리셋 해제 → S5·S7 PASS. 실제 기준(순서·최소 간격·ramp)은 **해당 SoC 데이터시트의 power sequence 표**가 정한다.

**JTAG 첫 접속** `[예시 출력]` (OpenOCD 형식 흉내, 실제 메시지는 버전마다 다름)

```text
Info : clock speed 100 kHz
Info : JTAG tap: soc.dap tap/device found: 0x4ba00477 (mfg: 0x23b (ARM Ltd), part: 0xba00, ver: 0x4)
Info : soc.cpu0: hardware has 6 breakpoints, 4 watchpoints
Info : soc.cpu0: halted, PC 0x00000148 (BootROM region)
> mww 0x10000000 0x55555555 ; mdw 0x10000000
0x10000000: 55555555
```

- 1줄: TCK를 100 kHz로 **낮게** 시작했다. 2줄: IDCODE가 기대값과 일치 → 배선·TAP·debug 전원 OK (IDCODE 필드 해석은 [BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)). 3줄: 디버거가 코어의 HW breakpoint 개수를 읽었다 → DAP를 통해 코어 debug 레지스터까지 접근됨. 4줄: halt 성공, PC가 BootROM 영역 → BootROM이 돌다가 (boot 장치를 못 찾아) 대기 중인 것으로 해석 가능. 5–6줄: SRAM 주소(가상)에 패턴 write/read 일치.

**UART 첫 출력** `[예시 출력]`

```text
hello from SRAM
bu_sram v0.3 (1a2b3c4)  cpu0 @ 24 MHz (PLL bypass)
chip_id=0x____  rev=A0  reset_reason=POR  strap=0x3 (JTAG boot)
```

- 첫 버전은 **PLL bypass(오실레이터 24 MHz 그대로)** 로 돌리는 게 안전 — PLL 설정이 틀려도 UART는 나온다. 그다음 PLL을 켜고 같은 문자열이 다시 나오는지 확인 ([BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html)).
- reset reason·strap 값을 첫 줄에 찍어 두면 이후 재부팅 디버그가 쉬워진다 ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)).

### 3.4 Linux가 올라온 뒤 (참고 — 자세한 건 EL P1)

- 커널까지 오면 같은 체크를 소프트웨어로 반복·자동화: `dmesg` 부트 로그(에러·deferred probe), `/sys/class/hwmon/*/in*_input`·`curr*_input` (전압·전류 센서가 있으면), `i2cdetect -y <bus>` (PMIC·센서 주소), `/sys/class/regulator/` (regulator 상태).
- 단계별 DT 확장·earlycon·memory 노드 실수 재현은 **[EL P1](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html)** 참고.

## 4. 꼬리 질문 체인

### 4.0 ★ 2분 영어 답 — "The board just arrived. Walk me through Day 1."

<details><summary>2분 답 펼치기 (소리 내서 연습)</summary>

"Day 1 really starts a week or two earlier. Before the boards arrive I go through the schematic with the hardware engineer and turn it into a bring-up checklist: every rail with its nominal voltage, tolerance, sequence position, the test point I'll measure it at, the expected resistance to ground, and an expected-current table for each state — PMIC only, all rails on with the SoC in reset, reset released. I also make sure the test firmware already ran on the FPGA prototype: a small SRAM-only image that toggles a GPIO, prints on the UART and dumps the chip ID and reset reason, plus debugger scripts with the expected IDCODE. Then every board gets a serial-number log.

When the boards arrive I start with the best-looking one. Visual and microscope inspection against the assembly drawing. Then, with power off, rail-to-ground resistance on every rail and a comparison across boards — near zero ohms means a short and I don't power it.

Then first power from a bench supply with a low current limit, which I raise in steps while I watch the current and a thermal camera. If it sits in constant-current mode or something gets hot, power goes off and I go back to resistance checks. Then I measure every rail at the load side against tolerance, capture the sequence and power-good on a scope, check the oscillator, and confirm reset releases after the rails are stable with the boot straps set to a safe JTAG or download mode.

Next is JTAG at a slow TCK: read the IDCODE and check it matches the part and stepping, halt the core, read the PC and status registers, and do a write–read pattern test on SRAM. Then I load the SRAM image, see the GPIO toggle — that proves the CPU executes — and get hello on the UART, first with the PLL bypassed.

That's usually Day 1. After that come the boot device, DDR training and memory test, and the interfaces one by one. Throughout, the rule is bottom-up: I don't debug a layer until the layer below is proven, I change one thing at a time, everything goes in the log, and every measurement is compared with an expected value and a golden board."

- 한국어 핵심: **Day 0 준비 → 육안 → 저항 → 전류 제한 단계 전원 → rail·순서·클럭·리셋 → JTAG(IDCODE·halt·SRAM) → SRAM 코드(GPIO·UART) → 이후 boot·DDR·인터페이스**, 원칙 = bottom-up gate · 하나씩 · 기록 · 예상값·golden 비교.

</details>

### 4.1 BV00 체인 — "Walk me through your first day" → 전원 전 측정 → JTAG 안 보임 → SRAM 코드 무출력

<details><summary>답 펼치기</summary>

- **Q. The board just arrived. Walk me through your first day.** — (위 4.0의 2분 답, 또는 30초 버전) "Bottom-up: inspect, check for shorts with power off, current-limited first power, rails and sequence, clock, reset, then JTAG, then a small program in SRAM that toggles a GPIO and prints on the UART. Each step has a pass criterion written down before I start, and I don't move on until it passes." · 한국어: 아래부터, 단계마다 통과 기준, 통과 전엔 다음 단계 금지.
- **꼬리1. What do you measure before applying power?** — "Resistance from every rail to ground with a multimeter, same range and polarity each time, compared with what I expect and across boards. Near zero is a short and the board doesn't get powered. Big core rails can legitimately read low, a few ohms, because of the die and all the decoupling, so the comparison with other boards matters more than the absolute number. I also check the jumpers and boot straps are in the safe position and the supply polarity." · 한국어: rail-GND 저항, 비교가 핵심, 코어 rail은 원래 낮을 수 있음, strap·극성 확인.
- **꼬리2. Power is fine but JTAG sees nothing.** — "I go through the debug path from the bottom. Is VTref present so the probe drives the right level? Is the SoC actually out of reset — or is the debugger itself holding SRST? I drop TCK to 100 kHz or lower and put a scope on TCK, TMS and TDO to see whether TDO ever toggles. If TDO is stuck high the chain is open or the TAP isn't powered; stuck low suggests a short or a level-shifter problem. Then the chain configuration — IR lengths, other TAPs in the chain, a JTAG router that must be enabled. Boundary-scan of a neighbouring device can also tell me if the JTAG nets are connected." · 한국어: VTref → 리셋 상태 → TCK 낮추기 → scope로 TDO → chain 설정 (BV07 debug tree).
- **꼬리3. JTAG works, but the code you load into SRAM doesn't print anything.** — "First I halt and look at the PC: is it in my code, in a fault handler, or back in ROM? If it's running my loop, the CPU is fine and the problem is the UART path. I'd check the GPIO toggle first, then scope the UART TX pin: if there's a waveform, it's the terminal side — TX and RX swapped, wrong voltage level, wrong baud. If the bit width is wrong, my clock assumption is wrong, for example the reference isn't 24 MHz or the PLL isn't what I think. If there's no waveform, it's pinmux, the UART clock gate or its reset not released." · 한국어: halt해서 PC → GPIO → TX 파형 유무로 터미널 쪽 vs SoC 쪽 분리, bit 폭으로 클럭 오차.

</details>

### 4.2 "You have 5 boards and 2 of them behave differently. What do you do?"

<details><summary>답 펼치기</summary>

- **Q.** — "First I make sure it's the board and not the setup: move the failing board to the good setup and vice versa — same supply, cables, debugger and firmware. If the symptom follows the board, I build a difference table: rework history, BOM alternates, silicon sample and lot, fuse or stepping, PMIC OTP version, and the measurements we logged — rail resistances, voltages, currents. Usually something lines up. Then I prove it with a swap — on a socketed board I swap the SoC, otherwise I swap the suspect part — and confirm with A-B-A: the symptom must move and come back." · 한국어: 셋업 vs 보드 분리 → 차이 표 → swap → A-B-A.
- **꼬리1. Everything in the table is identical. Now what?** — "Then I look for margin rather than a defect: the two boards may be the weak tail of a distribution. I'd sweep voltage and temperature on good and bad boards — if the bad ones fail at a slightly higher voltage, it's marginal timing in silicon or the board's power integrity. That moves it into characterization with more samples." · 한국어: 결함이 아니라 마진 → 전압·온도 스윕으로 분포 비교 (BV12).
- **꼬리2. How do you decide whether it's a silicon or a board problem?** — "Swap tests and correlation. If it follows the chip across boards, silicon. If it follows the board position or a specific component, board. If all boards fail the same way, compare with the FPGA and with the design intent — it's either design or silicon, and the register-level behaviour versus the spec decides." · 한국어: 따라가는 쪽이 원인, 전부 실패면 설계/실리콘 → spec 대비.
- **꼬리3. How do you communicate this to the hardware team?** — "With data, not opinions: the board serials, the measurement, the expected value, the scope captures, and the swap result. And a clear ask — rework, X-ray, or an ECO for the next revision." · 한국어: 증거 + 명확한 요청.

</details>

### 4.3 "How do you avoid destroying the only A0 silicon you have?"

<details><summary>답 펼치기</summary>

- **Q.** — "Validate the board before the chip: on a socketed board I bring up the power tree with no SoC or a dummy, and on a soldered one I can check the PMIC outputs with the SoC rails isolated by zero-ohm links. Then current limits, staged; first power on the best-looking board only; overvoltage means immediate power-off because the low-voltage core is the most fragile. Probe safely — soldered wires instead of probe tips that can slip and short pins. ESD discipline. And software can kill chips too, so test firmware must not touch fuses, OTP, secure-boot keys or voltage-scaling registers until we mean to." · 한국어: 칩 없이 전원부 먼저, 단계 전류 제한, 한 장부터, 과전압 즉시 OFF, 프로브 안전, ESD, 되돌릴 수 없는 SW 동작 차단.
- **꼬리1. Why not just set the current limit very low?** — "Too low and a healthy board sits in constant-current mode: the input voltage sags, the PMIC sees undervoltage and either shuts down or comes up half-way, and I'd misdiagnose a good board. That's why I make an expected-current table per state and raise the limit in steps." · 한국어: 너무 낮으면 정상 보드도 CC → UV → 오판.
- **꼬리2. One rail is overvoltage by 15%. What do you do?** — "Power off immediately, log it, and check before re-powering: the feedback divider values and the PMIC setting or OTP for that rail. I'd measure the regulator output with the load disconnected if possible, and check whether the chip on that rail got stressed — compare its leakage current with other boards afterwards." · 한국어: 즉시 OFF → 피드백 저항·OTP → 칩 스트레스 여부 확인.

</details>

### 4.4 (추가) "What do you have ready before the silicon and boards arrive?"

<details><summary>답 펼치기</summary>

- **Q.** — "Three things. A checklist derived from the schematic and the SoC's power and reset requirements, with expected values. Test firmware and debugger scripts that already ran on the FPGA or emulator, so on silicon day I'm not debugging new code. And infrastructure: instruments, the log template, the board tracking sheet, and automation hooks so the same steps can later run unattended on a board farm." · 한국어: 체크리스트·FPGA 검증된 FW·인프라(장비·로그·자동화).
- **꼬리1. What's different between FPGA and silicon that can break your proven firmware?** — "Clock ratios and absolute speeds — timeouts and delay loops; real analog — PLLs, PHYs and DDR PHY training that the FPGA didn't have or modelled; power domains and resets that were always-on in the FPGA; and memory map or fuse differences. I parametrize clocks and timeouts and keep a platform layer so only that changes." · 한국어: 클럭·타임아웃, 아날로그(PLL·PHY·DDR), 전원 도메인·리셋, fuse — 플랫폼 계층 분리.
- **꼬리2. How would you automate Day 1 for the next 50 boards?** — "The same checklist becomes a script: programmable supplies with current readback, a DMM or DAQ for rail voltages, a scope over SCPI for sequence timing, the debugger's scripting interface for IDCODE and SRAM tests, and UART capture with pattern matching. Each step has a pass criterion, a failure stops the run, and results go to a database keyed by board serial." · 한국어: SCPI 장비 + 디버거 스크립트 + UART 파싱, gate 유지, SN별 DB (→ bv06 코드, BV14).

</details>

### 4.5 (추가) "Nothing on UART, and JTAG shows the PC stuck in BootROM. What's happening?"

<details><summary>답 펼치기</summary>

- **Q.** — "The chip is alive and the BootROM is running but didn't find a bootable image, or it's waiting in a download mode. I'd read the strap-sample or boot-mode register to see which device it thinks it should boot from, then check that device: is the flash powered, does it answer its ID command, is the image header correct for this BootROM. Many BootROMs leave an error code or a log in SRAM or a register — I'd look that up in the TRM." · 한국어: BootROM은 살아 있음 → boot mode 레지스터 → boot 장치 ID·이미지 헤더 → BootROM 에러 코드.
- **꼬리1. The strap register says the wrong boot mode, but the schematic is right.** — "Then the pin's level at the moment of reset release is wrong: another device driving it during reset, a pull resistor too weak against leakage, or the IO rail not yet up when it's sampled. I'd scope the strap pin against reset." · 한국어: 리셋 순간 핀 레벨 → 다른 장치 driving·약한 풀·IO rail 미안정 (BV05).

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up our PCIe Gen6 SSD controller's Cortex-R8, R82 and M0+ cores on the pre-silicon FPGA platform before RTL freeze, so I know the value of having firmware and debugger scripts proven before silicon day — the first bring-up on new hardware should only debug the hardware. `[숫자 채우기: FPGA 플랫폼 이름, 첫 부팅까지 걸린 기간]`"
- "My bring-up order is the same bottom-up ladder I used there — power and clocks, reset, Trace32 attach and halt, SRAM tests, then the UART and the subsystems — with a log per build and per board. `[숫자 채우기: 처음 막혔던 것 1개 — 예: 클럭 비율·리셋 순서]`"
- "At Apple, on new wireless-chipset boards, I root-caused PCIe, I2C, SPMI and RFFE issues with a DSO and protocol analyzers, which is exactly the measure-first discipline Day 1 needs. `[숫자 채우기: 대표 사례 1건]`"
- "At SK hynix I automated chamber-based reliability testing, so I tend to design the Day-1 checklist so it can be scripted for the next fifty boards. `[숫자 채우기: 자동화로 줄인 시간]`"
- 주의: 실리콘 A0 bring-up을 **직접** 했는지, 옆에서 봤는지 과장 없이 구분 (BV00 §4 체크리스트).

## 6. 실습 / 코드

### 6.1 `onsitePrep/bv06/bringup_log.py` — Day-1 체크리스트 평가 + log 생성 (실행 검증됨)

- 무엇: 가상의 보드 5장(SN001 golden + 4장에 서로 다른 결함 주입)의 Day-1 측정값(mock)을 S1–S9 체크리스트로 평가. **한 단계 FAIL → 이후 단계 전부 SKIP (gate 규칙)**, 실패 단계엔 "NEXT" 다음 행동 힌트, 마지막에 golden 대비 차이표와 요약.
- 주입한 결함: SN002 = 1.8 V rail-GND 0.3 Ω(합선), SN003 = IO rail이 core보다 먼저(순서 위반), SN004 = IDCODE 0xFFFFFFFF(JTAG), SN005 = UART 깨짐.
- 실행: `python3 -I onsitePrep/bv06/bringup_log.py` (stdlib만, Python 3.9.6에서 exit 0). 전체 출력: [bringup_log.out](onsitePrep/bv06/bringup_log.out).
- **실제 출력 발췌** (SN001 일부, SN002, SN004, 요약):

```text
## Day-1 bring-up log (mock data, [example] values)

### Board SN001  rev A  SoC A0  (golden: first board, best-looking solder)
  [PASS                ] S1 visual       | no missing/rotated parts, no bridges
                                         | measured: clean
  [PASS                ] S2 rail-GND     | each rail >= min ohm and within 50% of golden
                                         | measured: all ok
  [PASS                ] S3 first power  | settles below limit, <= 1.5x expected 0.25A
                                         | measured: 0.10A lim -> 0.100A CC!, 0.30A lim -> 0.231A
  [PASS                ] S4 rail V       | every rail within tolerance (DMM at load-side test point)
                                         | measured: all within tol
  [PASS                ] S5 sequence     | rail 90% points in spec order, all PGOOD high
                                         | measured: AON_0V8 > CORE_0V75 > DDR_1V05 > IO_1V8 > IO_3V3, PGOOD=all
  ...
  => status: READY for boot-device/DDR

### Board SN002  rev A  SoC A0  (IO 1.8 V rail reads 0.3 ohm)
  [PASS                ] S1 visual       | no missing/rotated parts, no bridges
                                         | measured: clean
  [FAIL                ] S2 rail-GND     | each rail >= min ohm and within 50% of golden
                                         | measured: VDD_IO_1V8=0.3ohm(min 50.0, golden 180.0)
                                         | NEXT: short suspected: thermal camera at low current, lift 0-ohm/ferrite to split the rail, compare golden
  [SKIP (blocked by S2)] S3 first power  | settles below limit, <= 1.5x expected 0.25A
  [SKIP (blocked by S2)] S4 rail V       | every rail within tolerance (DMM at load-side test point)
  ...
  => status: STOPPED at S2 rail-GND

### Board SN004  rev A  SoC A0  (JTAG reads all ones)
  ...
  [FAIL                ] S8 JTAG         | IDCODE == 0x4BA00477, halt ok, SRAM R/W ok
                                         | measured: 0xFFFFFFFF, halt=False, sram_rw=False
                                         | NEXT: VTref present? TCK lower (100 kHz)? TDO on scope; chain/irlen; held in reset? debug power domain
  [SKIP (blocked by S8)] S9 SRAM+UART    | GPIO toggles, UART prints 'hello from SRAM'
  => status: STOPPED at S8 JTAG

### Cross-board diff vs golden (SN001) -- only values that differ by >10%
  SN002: rail_ohm[VDD_IO_1V8] 180.0ohm->0.3ohm
  SN003: no rail-level differences >10%
  SN004: no rail-level differences >10%
  SN005: no rail-level differences >10%

### Summary (first failing step per board)
  SN001: all PASS
  SN002: S2 rail-GND
  SN003: S5 sequence
  SN004: S8 JTAG
  SN005: S9 SRAM+UART
```

- 해석: (1) S3에서 0.10 A 제한에 "CC!"가 찍혀도 FAIL이 아니다 — 예상 전류(0.25 A)가 첫 제한보다 크니 **다음 단계로 올리는 게 정상 절차**이고, 0.30 A에서 0.231 A로 안정되었으므로 PASS. (2) SN003·SN004·SN005는 rail 수준 차이표에 아무것도 안 나온다 → **차이표만으로는 순서·JTAG·UART 문제를 못 찾는다**, 단계별 측정이 필요하다는 것을 보여 준다. (3) 실제 자동화에서는 `meas` dict 대신 PSU(전류 readback)·DMM·scope(SCPI)·디버거 스크립트·UART 캡처 결과를 채우면 된다 ([BV14](2026-11-01_BV14_board_validation_plan_automation.html)).
- 한계: 모든 기준값(전압·허용 오차·저항 하한·전류)은 `[예시]`. 실제 보드에서는 회로도·데이터시트·golden 측정으로 채운다.

### 6.2 랩 레시피 `[미실행]` — 실제 보드가 있다면

1. 공개 EVB 회로도(예: Raspberry Pi CM4 IO board, AMD ZCU102) 한 장으로 rail 표(이름·전압·순서·TP)를 만들어 `RAILS` 리스트를 교체해 본다.
2. 저렴한 MCU 보드(Cortex-M)로 S6–S9 축소판: 디버거로 IDCODE 읽기 → halt → SRAM에 GPIO 토글 코드 로드 → scope로 주기 측정 → UART hello.

## 7. 자주 틀리는 것

1. **Day 0를 빼먹고 Day 1부터 말한다** — 예상값 없는 측정은 판단이 안 된다. "Before the board arrives…"로 시작할 것.
2. **rail-GND 저항의 절대값만 본다** — 코어 rail은 원래 낮을 수 있다. 보드 간·golden 비교와 "값이 올라가나(충전) vs 0에 붙어 있나(합선)"를 본다.
3. **전류 제한을 아무 값이나** — 너무 높으면 보호가 안 되고, 너무 낮으면 정상 보드를 고장으로 오판한다. 예상 전류 표 + 단계적 상향.
4. **레귤레이터 출력에서만 전압을 잰다** — SoC 근처(부하 쪽) TP에서 재야 배선 강하가 보인다.
5. **crystal 핀을 scope로 직접 찍는다** — 발진이 멈출 수 있다. 오실레이터 출력·clock-out 핀으로.
6. **JTAG TCK를 처음부터 빠르게** — 리셋 직후·PLL 전에는 느린 TCK만 되는 칩이 많다. 낮게 시작 (OpenOCD 문서도 autoprobe는 느린 클럭으로 권장 [6]).
7. **UART 먼저, GPIO 나중** — UART는 의존성이 많다. GPIO 토글로 "CPU가 코드를 실행한다"를 먼저 증명.
8. **두 가지를 동시에 바꾼다** — rework + FW 업데이트를 같이 하면 원인을 모른다. 그리고 A-B-A로 확인.
9. **첫날 boot strap을 flash boot로 둔다** — 빈/잘못된 flash 이미지 실행, 리셋 루프. 첫날은 JTAG/다운로드 모드.
10. **프로브로 핀을 쇼트시킨다** — 미세 피치에서 프로브 팁 미끄러짐이 흔한 사고. wire 납땜·hook, 전원 OFF 상태에서 연결.

## 8. 출처

1. Fedevel, "8 Steps to Bring up your Board to Life" (i.MX6: 육안·조립도 대조, rail-GND 저항, 코어/메모리 rail은 수십 Ω도 가능, 전류 제한 전원, rail 측정, 클럭, TX 파형·콘솔, 보드 번호 스티커·가장 잘 납땜된 보드부터) — https://fedevel.com/blog/9-steps-to-bring-up-a-freescale-i-mx6-board-to-life
2. Analog Devices EngineerZone, "RF transceiver bring-up: a systematic first power-on checklist" (검색 요약으로 확인, 본문 fetch 타임아웃) — https://ez.analog.com/ez-blogs/b/engineering-mind/posts/rf-transceiver-bring-up-a-systematic-first-power-on-checklist
3. Cadence PCB Resources, "The Definitive Guide to PCB Bring-Up" (검색 결과로 확인, 본문 미추출) — https://resources.pcb.cadence.com/blog/the-definitive-guide-to-pcb-bring-up
4. JLCPCB Help, "PCBA First Power-On Troubleshooting" (VIN·rail·IC 전원핀 GND 저항, 양품 비교, 낮은 전류 제한·점진 상향, CC 모드 진입 시 즉시 차단, 열화상, 부품 만지지 말 것, 출력 과전압 시 차단, FB 저항·EN·인덕터 확인) — https://jlcpcb.com/help/article/pcba-first-power-on-troubleshooting
5. Don의 EmbeddedLinux 노트 P1 "새 보드 bring-up 순서" (Linux 쪽 단계·QEMU 실습) — ../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html
6. OpenOCD User's Guide, "TAP Declaration" (autoprobe는 느린 클럭으로, expected-id 권장) — https://openocd.org/doc/html/TAP-Declaration.html
7. OpenOCD User's Guide, "Reset Configuration" (srst_only, srst_nogate, connect_assert_srst — 디버거가 리셋을 잡는 경우) — https://openocd.org/doc/html/Reset-Configuration.html
8. BV00 트랙 목록 §0.5·§2 (사다리·체인 원본) — 2026-11-01_BV00_board_validation_track.html
