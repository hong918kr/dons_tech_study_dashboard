# BV13 · 보드 디버그 시나리오 12선 — 증상 → 잴 것 → 가설 → HW/SW 가르기 → 재발 방지
<!-- theme: pastel -->

> **묶음**: F 디버그 · 계획 · 실전 · **우선**: P0 ★ 도메인 라운드 단골 ("It doesn't boot — debug it") · **선행 노트**: [BV03 전원](2026-11-01_BV03_power_tree_sequencing_pmic.html), [BV05 리셋/strap](2026-11-01_BV05_reset_strap_boot_mode.html), [BV06 첫 전원 인가](2026-11-01_BV06_first_power_on_checklist.html), [BV07 JTAG](2026-11-01_BV07_jtag_swd_boundary_scan.html), [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html) · **이전 노트**: [BV12 SI/PI·shmoo](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · **다음 노트**: [BV14 검증 계획·자동화](2026-11-01_BV14_board_validation_plan_automation.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), PCIe 쪽 짝 노트 [LP15 디버그 시나리오](2026-10-31_LP15_debug_scenarios.html), [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) · **코드**: 시나리오 7·8·12·hang·boot 실패를 재현하는 mock 보드 팜 [farm_orchestrator.py](onsitePrep/bv14/farm_orchestrator.py) (BV14와 공유, 실행 로그 [run_log.txt](onsitePrep/bv14/outputs/run_log.txt))

## 0. TL;DR

- 이 노트로 답할 것: "전원 넣자마자 전류 제한 / JTAG가 안 붙음 / UART 무출력 / 재부팅 루프 / 엉뚱한 boot mode / DDR training 실패 / 고온에서만 실패 / 10장 중 2장만 / warm reset만 실패 / 부하 시 랜덤 hang / I2C NACK / rev B에서 새로 생긴 실패" — 12개 시나리오를 **같은 6단 틀**(증상 → 잴 것·볼 것 → 가설 우선순위 → HW/SW 가르는 실험 → 결론·재발 방지 → 영어 2분 답)로.
- **외울 것 ① 아래 계층부터 증명 (bottom-up)**: 전원 → 클럭 → 리셋/strap → JTAG → SRAM 코드·UART → DDR → 인터페이스 → OS/드라이버 → 마진. **각 계층마다 "OK라는 증거 1개"** (예: 리셋 = scope에서 PORn이 마지막 PGOOD 뒤 X ms에 올라감). 아래가 증명되기 전엔 위 증상을 믿지 않는다.
- **외울 것 ② 실패가 "무엇을 따라가나"를 실험으로 가른다**: **swap test**(칩·보드·케이블·팜 슬롯 맞바꾸기, A→B→A로 재확인), **다른 FW**(이전 빌드, 최소 bare-metal, 벤더 레퍼런스 코드), **다른 보드**(golden board), **조건 스윕**(온도·전압·주파수). 실패가 칩을 따라가면 silicon, 보드를 따라가면 board, 빌드를 따라가면 SW.
- **외울 것 ③ 끝은 항상 "재발 방지"**: 원인 → **자동 회귀 테스트**(팜에서 매일) 또는 **체크리스트 항목**(새 rev마다 scope 측정) 중 하나로 고정. 버그 리포트는 **증상 · 조건 · 재현률 · 증거(파형·레지스터·로그) · 가설 · 요청** 6칸.
- **30초 영어 (헤드라인 "It doesn't boot — debug it")**: "I don't start at the symptom, I start at the bottom and collect one piece of evidence per layer. Current draw and rail voltages against the expected table, then sequencing and PGOOD on the scope, the oscillator running, reset released after the last rail is good, and the strap levels at the moment reset is released. Then JTAG: can I read IDCODE and halt the core, and where is the PC — still in BootROM, in an exception handler, or in my code? That tells me which layer broke. Then I split hardware from software by changing one thing at a time: the same binary on a golden board, a known-good older build on this board, and a swap of the suspect part. Once I know the cause, I turn it into a regression test or a bring-up checklist item so it can't come back silently."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "응급실 의사의 진료 순서"

보드 디버그 = **응급실에 실려 온 환자 보기**. 좋은 의사는 환자가 "머리가 아파요"(증상)라고 해도 바로 뇌 MRI부터 찍지 않는다.

| 의사가 하는 일 | 보드 디버그에서 | 이 노트의 칸 |
|---|---|---|
| ① 활력징후 먼저: 숨 쉬나, 맥박 있나, 혈압은 | **전원(rail 전압·전류)·클럭·리셋** 먼저 | 잴 것·볼 것 |
| ② 문진: "언제부터? 어떤 상황에서? 매번?" | **조건·재현률**: 매번인가, 고온만인가, 이 보드만인가 | 증상 |
| ③ 감별진단: 가능성 높은 병부터 목록 | **가설 우선순위**: 흔하고 싸게 확인되는 것부터 | 가설 표 |
| ④ 검사로 가르기: 피검사·X-ray | **HW/SW 가르는 실험**: swap·다른 FW·조건 스윕·측정 | 실험 |
| ⑤ 처방 + 재발 방지 교육 | **수정 + 회귀 테스트·체크리스트** | 결론·재발 방지 |
| ⑥ 차트 기록: 다음 의사가 읽을 수 있게 | **증거 기반 버그 리포트** | §2.5 |

- **핵심 직관 1 — 증상은 위층, 원인은 아래층인 경우가 많다**: "Linux가 가끔 멈춘다"(위층 증상) ← 실제 원인은 NPU가 돌 때 코어 전압이 순간 떨어지는 것(맨 아래 전원층). 그래서 **아래부터** 본다.
- **핵심 직관 2 — 실험은 "한 번에 하나만" 바꾼다**: 보드도 바꾸고 FW도 바꾸고 온도도 바꿨는데 고쳐졌다 → 무엇이 고쳤는지 모른다 → 다음에 또 난다.
- **핵심 직관 3 — "따라가는 것"을 찾는다**: 고장 난 전구를 찾을 때 전구를 다른 소켓에 끼워 보는 것과 같다. **전구를 따라 고장이 움직이면 전구 문제, 소켓에 남으면 소켓 문제** — 이게 swap test. 칩·보드·케이블·FW 빌드 모두 "전구"가 될 수 있다.

### 1.2 계층 그림 — 각 층이 OK라는 증거는 어디서 보나

```text
 층 (위로 갈수록 아래층에 의존)        "이 층 OK" 증거 1개                         주로 쓰는 장비
 ─────────────────────────────────────────────────────────────────────────────────────────────────────
 ⑨ 마진·성능 (PVT, 부하)          shmoo에서 목표 조건 주변 여유, 부하 soak N시간 무오류     챔버, 전압 margining, 스트레스
 ⑧ OS·드라이버                    dmesg probe 성공, 장치 노드 존재                           UART 콘솔, Linux 도구
 ⑦ 인터페이스 (I2C/SPI/ETH/PCIe)  I2C 스캔 기대 주소, flash JEDEC ID, PHY ID, LnkSta         LA/protocol analyzer, scope
 ⑥ DDR                            training PASS + 바이트 레인별 eye, memtest 무오류          training 로그, memtest
 ⑤ 부트·SRAM 코드·UART            BootROM 로그/에러 코드, "hello", LED 토글                   UART, scope(TX 핀)
 ④ JTAG / debug                   IDCODE 읽힘, halt 됨, PC 값, SRAM read/write               Trace32 / OpenOCD / J-Link
 ③ 리셋 · strap                   PORn이 마지막 PGOOD 뒤에 해제, strap 래치 레지스터 = 설계값  scope 2ch, 레지스터
 ② 클럭                           osc 출력 주파수·진폭, PLL lock 비트, clock-out 핀           scope, frequency counter
 ① 전원                           rail 전압·순서·ramp·ripple, 전류 = 예상치                   DMM, PSU, scope, 열화상
 ─────────────────────────────────────────────────────────────────────────────────────────────────────
           ▲ 아래부터 하나씩 "OK 증거"를 확보하며 올라간다.  ▲ 위층 증상을 보면 → 아래층 증거부터 다시 확인
```

### 1.3 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| rail | Power rail | 보드 위 전압선 하나 (예: VDD_CORE 0.75 V, VDDQ 1.1 V) | 대부분의 첫 디버그 대상 |
| current limit | (PSU) Current limit | 전원 공급기가 "이 이상 전류는 안 준다"고 거는 상한 | 합선 시 보드를 태우지 않게 함 (S1) |
| PGOOD | Power Good | 레귤레이터가 "내 출력이 목표 범위에 들어왔다"고 알려 주는 신호선 | 리셋 해제 조건의 체인 |
| PMIC | Power Management IC | 여러 rail을 만들고 순서를 지키는 전원 관리 칩 | fault 레지스터가 증거 창고 |
| BOR / brown-out | Brown-Out Reset | 전압이 잠깐 기준 아래로 떨어지면 칩이 스스로 리셋 | 재부팅 루프 원인 (S4) |
| WDT | Watchdog Timer | SW가 주기적으로 "살아 있음"을 알리지 않으면 강제 리셋하는 감시견 | 재부팅 루프 원인 (S4) |
| reset reason | Reset cause register | "왜 리셋됐나"(POR·BOR·WDT·SW·핀)를 기록하는 레지스터 | 재부팅 디버그 1순위 증거 |
| strap | Strap pin | 리셋 해제 **순간** 칩이 읽는 핀의 High/Low (부팅 장치·모드 결정) | 엉뚱한 부팅 (S5) |
| IDCODE | JTAG Identification Code | JTAG로 읽는 칩의 32비트 신분증 (bit0=1, 제조사·부품·버전) | "칩이 살아 있나"의 첫 증거 (S2) |
| VTref | Target Voltage Reference | 디버거가 보드 IO 전압을 알려고 재는 핀 | 0 V면 디버거가 아예 말을 안 건다 |
| PC | Program Counter | CPU가 지금 실행 중인 명령 주소 | JTAG halt 후 "어디 멈췄나" (S3·S10) |
| pin mux | Pin multiplexing | 칩 핀 하나를 UART/GPIO/I2C 중 무엇으로 쓸지 고르는 설정 | UART 무출력 1순위 (S3) |
| DDR training | (Memory) Training | 컨트롤러가 DRAM과 신호 타이밍을 스스로 맞추는 보정 절차 | DDR bring-up 핵심 (S6) |
| VREF | Reference Voltage | 수신기가 0/1을 가르는 기준 전압 | 틀리면 데이터가 깨짐 |
| margin | Margin | 실패 경계까지 남은 여유 (시간·전압·온도) | 고온/일부 보드 실패의 본질 (S7·S8) |
| droop | Voltage droop | 부하가 갑자기 늘 때 rail 전압이 순간 꺼지는 것 | 랜덤 hang 원인 (S10) |
| DVFS | Dynamic Voltage and Frequency Scaling | 부하에 따라 전압·주파수를 바꾸는 기능 | 전환 순서 버그 (S10) |
| NACK | Not Acknowledge | I2C에서 9번째 클럭에 장치가 "받았다"(SDA low)를 안 한 것 | I2C 디버그 (S11) |
| pull-up | Pull-up resistor | open-drain 선을 기본 High로 당겨 주는 저항 | I2C 상승 시간·NACK (S11) |
| golden board | (기준 양품 보드) | 모든 테스트를 통과한, 비교 기준으로 보관하는 보드 | "정상은 어떻게 생겼나"의 기준 |
| swap test | (교차 교환 실험) | 의심 부품을 정상 쪽과 맞바꿔 실패가 따라가는지 보기 | HW/SW·칩/보드 가르기 핵심 |
| ABA | A→B→A swap | 바꿨다가 **되돌려서** 실패가 다시 나오는지 확인 | 우연·접촉 불량 배제 |
| bisect | (이분 탐색) | 좋은 것과 나쁜 것 사이를 반씩 좁히기 (FW 빌드·조건·부품) | 원인 위치를 빠르게 좁힘 |
| ECO | Engineering Change Order | 보드 설계 변경 지시 (rev A → rev B) | 새 rev 실패 (S12) |
| BOM | Bill of Materials | 보드 부품 목록 (대체 부품 포함) | 같은 rev도 BOM이 다를 수 있음 |
| rework | (수작업 수정) | 조립 후 납땜으로 부품을 바꾸거나 선을 잇는 것 | 보드마다 이력이 달라짐 (S8) |
| errata | Errata | 칩 제조사가 공개하는 "알려진 silicon 버그 + 우회법" 목록 | silicon 버그 결론의 근거 |
| signature | Failure signature | 실패를 구분하는 안정적인 문자열 (테스트명 + 에러 코드) | 중복 제거·상관관계 분석 |

## 2. 핵심 개념 (정확한 버전) — 일반 디버그 방법

### 2.1 원칙 6개

| 원칙 | 무엇 | 왜 | 면접 문장 |
|---|---|---|---|
| ① Bottom-up | 전원 → 클럭 → 리셋 → debug → 부트 → 메모리 → IO → SW 순서로 층별 증거 | 위층 증상의 원인이 아래층인 경우가 흔함 | "One piece of evidence per layer, from the bottom." |
| ② 한 번에 하나만 | 변수 하나 바꾸고 같은 횟수로 재측정 | 여러 개 바꾸면 인과를 모름 | "I change one variable and rerun the same number of cycles." |
| ③ Golden board 비교 | 같은 측정을 양품 보드에서도 → 차이(delta)만 본다 | "정상값"을 데이터시트가 아니라 실물로 앎 | "I measure the same point on a known-good board first." |
| ④ Swap (ABA) | 칩·DRAM·케이블·프로브·팜 슬롯을 맞바꾸고 되돌림 | 실패가 무엇을 따라가는지 = 원인 위치 | "Does the failure follow the chip or stay with the board?" |
| ⑤ Bisect | FW 빌드(`git bisect run`), 조건(온도 계단), 부품 그룹을 반씩 좁힘 | 100개 후보도 7번이면 끝 | "Good and bad build, then binary search with an automated check." |
| ⑥ 증거 기반 리포트 | 파형·레지스터 덤프·로그·조건·재현률 + 최소 재현 | 다른 팀이 의견이 아니라 데이터로 판단 | "I hand over a minimal repro with the waveform, not an opinion." |

- **간헐 실패의 추가 규칙** (LP14 §2.5와 같은 통계): 실패율을 먼저 숫자로 (예: 200회 중 3회). 고쳤다고 말하려면 **rule of three** — 실패 0회로 "p < 1/500"을 95% 신뢰로 말하려면 ≈ 1,500회 무실패. 실험 한 번에 그 횟수를 못 돌리면 **스트레스로 실패율을 올린다**(고온·저전압·고속·짧은 리셋 간격) — 단, 실패 시그니처가 같은지 확인.

### 2.2 증상 → 계층 결정 흐름도

```text
 전원 ON (전류 제한 걸린 bench PSU)
   │
   ├─ 전류가 제한값에 붙음 / 부품이 뜨거움 ─────────────────────────────▶ S1  합선·부품·back-power
   │
   ├─ rail 전압·순서·PGOOD 틀림 ────────────────────────────────────────▶ BV03 (PMIC 설정·순서)
   ├─ osc 안 뜀 / PLL lock 안 됨 ───────────────────────────────────────▶ BV04
   ├─ PORn/RESET 안 풀림 ───────────────────────────────────────────────▶ BV05 (supervisor·PGOOD 체인)
   │
   ▼ (①②③ OK 증거 확보)
 JTAG IDCODE 읽히나? ──no──────────────────────────────────────────────▶ S2
   │ yes
 halt 해서 PC 확인 → UART에 글자 나오나? ──no─────────────────────────▶ S3
   │ yes (또는 중간에 리셋)
   ├─ 몇 초마다 다시 처음부터 ──────────────────────────────────────────▶ S4  reset reason 먼저
   ├─ BootROM이 엉뚱한 장치에서 부팅 / 다운로드 모드 ───────────────────▶ S5  strap 래치값
   ▼
 DDR training PASS? ──no────────────────────────────────────────────────▶ S6  어느 단계·어느 바이트 레인
   │ yes
 인터페이스 OK? ── I2C NACK ───────────────────────────────────────────▶ S11
   │              └─ PCIe 안 보임/느림 ────────────────────────────────▶ LP15
   ▼ 기능은 다 됨 → "조건부" 실패인가?
   ├─ 고온(또는 저온)에서만 ────────────────────────────────────────────▶ S7  온도 스윕·마진
   ├─ 부하 걸 때 랜덤 hang ──────────────────────────────────────────────▶ S10 droop·DVFS
   ├─ cold boot OK, warm reset 후만 ─────────────────────────────────────▶ S9  리셋 도메인
   ├─ 일부 보드만 (2/10) ───────────────────────────────────────────────▶ S8  속성 표·swap
   └─ 새 보드 rev에서만 ─────────────────────────────────────────────────▶ S12 ECO diff·2×2
```

### 2.3 HW vs SW (vs silicon) 가르는 실험 매트릭스

| 실험 | 결과가 이렇다면 → HW(보드) | → silicon(칩) | → SW/FW |
|---|---|---|---|
| **칩 swap** (소켓 보드 또는 rework) | 실패가 보드에 남음 | 실패가 칩을 따라감 | 어느 쪽이든 같은 빌드에서 동일 |
| **보드 swap** (같은 FW·같은 칩 lot, golden board) | 특정 보드만 실패 | 같은 corner 칩 보드들만 실패 | 모든 보드 동일 실패 |
| **다른 FW** (이전 빌드, 벤더 레퍼런스, 최소 bare-metal) | 어떤 FW로도 실패 | 어떤 FW로도 실패 + 칩 의존 | 특정 빌드부터 실패 → `git bisect` |
| **조건 스윕** (V·T·F) | 강한 상관 (보드 하나만 벽이 낮음) | 강한 상관 (corner 칩에서 벽이 낮음) | 상관 없음 (타이밍 race는 예외 — 온도로 속도가 바뀌면 SW race도 드러남) |
| **측정** (scope·LA) | 신호가 spec 위반 (ripple·rise time·타이밍) | 신호는 spec 이내인데 칩 응답이 틀림 | 와이어에 요청 자체가 안 나감 / 틀린 값이 나감 |
| **레지스터 덤프 비교** (good vs bad) | 같은 설정, 다른 결과 | 같은 설정, 다른 결과 + errata 일치 | 설정값 자체가 다름 |

- 실무 문장: **"Prove it with the smallest software stack possible."** — Linux 재현 → bare-metal 몇 줄로 재현 → JTAG로 레지스터 직접 써서 재현. SW를 빼도 남으면 HW/silicon, 빼면 사라지면 SW. (PCIe 판은 [LP15 §2.3](2026-10-31_LP15_debug_scenarios.html))
- silicon 결론은 가장 마지막: "보드·SW를 다 배제했고, 여러 보드·여러 칩에서 같은 조건으로 재현, 칩을 바꾸면 따라간다 + 설계팀이 RTL/시뮬레이션으로 확인" → errata + workaround.

### 2.4 Bisect 손잡이 (보드 버전)

| 손잡이 | 방법 | 무엇을 가르나 |
|---|---|---|
| FW 빌드 | good 빌드·bad 빌드 → `git bisect run <자동 판정 스크립트>` [5] | SW 회귀 위치 |
| 클럭/속도 | PLL bypass(osc 직결), DDR 최저 speed bin, 인터페이스 저속 | 타이밍·SI 문제 vs 기능 문제 |
| 전압 | PMIC로 rail ±3–5% margining (BV12) | PI·마진 문제 |
| 온도 | 챔버 계단 (25 → 45 → 65 → 85 → 105 °C), 국부 가열/냉각 스프레이 | 온도 민감 부품 위치 |
| 블록 on/off | NPU·PCIe·카메라 블록 하나씩 끔 | 어떤 부하/블록이 트리거하나 |
| 부품 | 칩·DRAM·PMIC·osc swap, 0 Ω 제거로 rail 분리 | 부품 vs 보드 |
| 팜 인프라 | 다른 슬롯·케이블·PSU·프로브 | **"보드가 아니라 랙이 문제"** 배제 |

### 2.5 증거 기반 버그 리포트 템플릿

```text
 제목: [B08][rev B][85C] ddr_march_c FAIL code=0x0003 bit 15 of byte lane 1 — 50/50 at 85C, 0/50 at 25C
 ─────────────────────────────────────────────────────────────────────────────────────────────────
 1. 증상      : March C- 데이터 비교 실패, 항상 DQ15 (byte lane 1), 주소 랜덤
 2. 조건      : board B08 (rev B, SoC lot LOT18-W02 SS corner), FW bringup-fw 0.9.3-g1a2b3c4, 85C chamber,
                VDDQ 1.10 V nominal, DDR 3200 MT/s [예시]
 3. 재현률    : 85C 50/50, 65C 3/50, 25C 0/50 (95% 상한 6%) ; 다른 보드 B01-B06 @85C 0/50 each
 4. 증거      : training log(byte lane 1 read eye 폭 25C 대비 40% 감소), VDDQ scope 캡처(ripple 18 mVpp, spec 이내),
                farm repro package outputs/repro/B08_85C_ddr_march_c/ (uart.log + meta.json)
 5. 가설      : SS corner 칩 + 고온에서 read eye 마진 부족 (보드 고유 아님: 칩 swap 대기)
                배제한 것: FW(다른 빌드 동일), 팜 슬롯(슬롯 교체 동일), VDDQ(측정 spec 이내)
 6. 요청/다음 : ① SoC를 golden board로 이동해 따라가는지 (socket) ② V×T shmoo로 벽 위치 ③ DDR PHY 팀에 read
                DQS 지연 tracking 설정 확인 요청
```

- 이 6칸이 채워지지 않으면 아직 남에게 넘길 때가 아니다. 특히 **"배제한 것"** 줄이 리포트의 신뢰도를 만든다.

### 2.6 재발 방지 — 회귀 테스트 vs 체크리스트

| 원인 종류 | 재발 방지 형태 | 예 |
|---|---|---|
| SW/FW 버그 | **자동 회귀 테스트** (팜 nightly) | 리셋 매트릭스 1,000회, I2C 기대 주소 스캔 |
| 보드 설계 (매 rev 다시 확인해야 하는 것) | **bring-up 체크리스트 항목** (측정 + 기준값) | 리셋 해제 시점 strap 레벨 scope, rail-GND 저항표 |
| 조립·부품 편차 | **입고 스크린 테스트** (모든 새 보드에 1회) | 전류 baseline, training eye 최소값 |
| 마진 | **PVT 특성화 항목** + 양산 스크린 조건 | V×T shmoo, 고온 memtest |
| 인프라 (팜) | **팜 헬스 체크** | 매일 IDCODE·콘솔·PDU 확인 |

## 3. 보드에서 볼 것 · 잴 것 — 시나리오 12선

> 모든 예시 출력·파형은 실제 보드에서 캡처한 것이 아니다 → `[예시 출력]` / `[예시 파형]`. 수치는 전형적인 크기를 보여 주기 위한 것.

### S1 · 전원 넣자마자 전류 제한에 걸림

**증상** — bench PSU를 켜자마자 CC(constant current) 표시등, 출력 전압이 설정값(12 V)보다 훨씬 낮음. 어떤 부품이 손으로 만지기 뜨겁다.

```text
[예시 출력]  bench PSU 화면
  SET  12.000 V  1.000 A      ← 예상 정지 전류 0.35 A, 제한 1.0 A 로 걸어 둠
  OUT   2.140 V  1.000 A  CC  ← 전압이 무너지고 전류가 제한값에 붙음 = 어딘가 저항이 매우 낮다(합선)
```

**🔰 배경** — 합선(short) = 전류가 부하를 거치지 않고 GND로 바로 가는 길. 전류 제한을 걸었기 때문에 보드가 타지 않고, 대신 전압이 무너진다. 합선 지점은 **열**이 나므로 열화상 카메라로 찾는다 [7].

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| rail별 GND 저항 (전원 **끈** 상태) | DMM 저항 모드 | 각 rail 테스트 포인트 ↔ GND | golden board 값과 비교. 코어 rail은 원래 수 Ω 이하일 수 있다 → **절대값이 아니라 delta**. 0.1 Ω 미만 + golden은 수 Ω → 합선 |
| 어느 rail이 무너지나 | DMM / scope | 각 레귤레이터 출력 | 입력 12 V가 무너지면 상류(입력 보호·첫 buck), 특정 rail만 0 V면 그 rail 하류 |
| 뜨거운 곳 | 열화상 카메라 | 보드 전체, 낮은 전류(예: 0.5 A)로 몇 초 | 합선 부품(MLCC, 탄탈, LDO)에 열점이 핀다 [7] |
| 전압 강하 위치 | mΩ 미터 / DMM mV | 같은 plane 위 여러 점 | 강하가 가장 큰 쪽으로 합선 위치가 있다 |
| 외부 연결 | 눈 | USB-UART, JTAG 프로브, 다른 보드 | 다른 전원이 IO 핀으로 보드를 **역급전(back-power)** 하고 있지 않나 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 솔더 브리지 / 부품 방향 틀림 (탄탈·다이오드 극성) | 현미경·육안, 열화상 | 특정 부품 열점, 보드 1장만 |
| 2 | 파손된 decoupling MLCC (ESD·기계 스트레스) | 열화상, 의심 cap 제거 | cap 제거 후 저항 정상 |
| 3 | 잘못된 부품 (BOM 오류: 저항값·레귤레이터 버전) | BOM 대조, 마킹 확인 | 해당 rail 출력 전압 과다 → 하류 부품 손상 |
| 4 | 설계 오류 (net 단락, footprint 핀 순서) | 회로도·레이아웃 리뷰, **모든 보드 동일**인가 | 전 보드 동일 증상 |
| 5 | 역급전 / latch-up (시퀀스 위반으로 IO가 먼저 켜짐) | 외부 케이블 제거, rail 순서 scope | 프로브 빼면 정상, 또는 특정 순서에서만 |
| 6 | PMIC OTP 설정 (rail을 너무 일찍/잘못된 전압으로 켬) | PMIC 레지스터 읽기 (I2C), 단독 rail 인가 | 설정 전압 ≠ 설계값 |

**HW/SW 가르는 실험**
1. **rail 분리**: 레귤레이터 출력의 0 Ω 저항·인덕터를 빼서 rail을 하나씩 따로 살림 → 어느 rail 하류에 합선이 있는지.
2. **다른 보드**: 2장 이상이 똑같으면 설계/BOM, 1장만이면 조립.
3. **외부 연결 전부 제거** 후 재시도 → 역급전 배제.
4. SW 쪽 가능성은 거의 없지만 예외 둘: **PMIC OTP/NVM 설정**(사실상 FW), 그리고 부팅 **이후** 전류가 튀는 경우(FW가 모든 블록을 한꺼번에 켬) — 이건 "전원 넣자마자"가 아니라 "부팅 중 X초에"로 증상이 다르다.

**결론 · 재발 방지** — Day 0 체크리스트에 **rail-GND 저항표(golden 값 포함)** 와 **전류 단계적 인가**(낮은 제한 → 점진적)를 고정 ([BV06](2026-11-01_BV06_first_power_on_checklist.html)). 팜에서는 PSU의 SCPI 전류 readback을 **매 전원 인가마다** 기록하고 baseline ±X%를 벗어나면 보드 격리 (BV14 §2.10).

<details><summary>🎤 English 2-minute answer</summary>

"If the supply goes into current limit as soon as I turn it on, the first thing I'm glad about is that I set a current limit — that's why the board is still alive. The voltage collapsing tells me there's a very low-resistance path somewhere. I power off and measure each rail's resistance to ground, but I compare against a known-good board rather than against infinity, because a big SoC core rail can legitimately read only an ohm or so. That tells me which rail. Then I find where on that rail: I power it at a low current, around half an amp, and look with a thermal camera — a shorted capacitor or a reversed tantalum lights up in a few seconds. If I don't have a camera, I measure millivolt drops along the plane and follow the gradient. If the short is downstream of one regulator, I isolate rails by lifting the zero-ohm or inductor at the regulator output. To separate design from assembly, I check a second board: if every board does it, it's the schematic or the BOM — wrong part, wrong footprint; if only one, it's assembly. I also unplug every external cable, because a USB-UART adapter can back-power the board through an IO pin, and I check the PMIC's programmed output voltages over I2C if the rail comes up at the wrong level. To keep it from coming back, the rail-to-ground resistance table with golden values goes into the Day-0 checklist, and on the farm every power-on logs the supply current through SCPI and quarantines a board whose current drifts from its baseline."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. The core rail reads 0.8 Ω to ground. Is that a short?** — "Not necessarily — compare to a golden board. A large SoC core rail with hundreds of decoupling caps and the die itself can read very low. A short is a big delta from golden, or a rail that should be kilo-ohms reading near zero." · 한국어: 절대값 말고 golden 대비 delta.
- **Q. No thermal camera. Now what?** — "Inject a limited current and measure millivolt drops along the plane with a sensitive meter; the drop points toward the short. Or isolate sections by removing zero-ohm links." · 한국어: mV 강하 추적, 0 Ω로 구간 분리.
- **Q. Why not just raise the current limit to see what burns?** — "Because on A0 boards the SoC may be irreplaceable; a controlled low current makes the short warm, not vaporize — and vaporizing it destroys the evidence." · 한국어: A0 칩 보호 + 증거 보존.

</details>

### S2 · rail은 정상인데 JTAG가 IDCODE를 못 읽음

**증상** — 전원·클럭은 측정상 정상. 디버거(Trace32/OpenOCD/J-Link)가 "no device found" 또는 IDCODE가 `0xFFFFFFFF` / `0x00000000`.

```text
[예시 출력]  OpenOCD 스타일
Info : clock speed 10000 kHz
Error: JTAG scan chain interrogation failed: all ones
Error: Check JTAG interface, timings, target power, etc.
Info : VTarget = 0.000 V                   ← VTref가 0 V: 디버거가 보드 IO 전압을 못 봄
```

**🔰 배경** — IDCODE는 32비트: bit0 = 1(항상), bit 11:1 = 제조사(JEDEC JEP106 코드), bit 27:12 = 부품 번호, bit 31:28 = 버전. **TAP(JTAG 상태 기계)는 TCK와 debug 전원만 있으면 동작**하므로, IDCODE가 안 읽히면 "신호 경로 / TAP 리셋 / 칩 전체가 리셋에 갇힘 / JTAG 비활성"이 주 의심 ([BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)).

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| VTref | 디버거 소프트웨어 + DMM | JTAG 헤더 pin 1 | 0 V → 헤더 배선/전원 미연결, 디버거는 안전을 위해 신호를 안 냄 [8] |
| TCK·TMS·TDI 도착 | scope | **SoC 쪽** 핀 근처 (헤더가 아니라) | 헤더에선 보이는데 SoC 쪽에서 안 보임 → 직렬 저항 DNP, 레벨 시프터, 버퍼 enable |
| TDO 응답 | scope (TCK와 같이) | SoC TDO | 항상 High → 칩이 TDO를 drive 안 함 (`0xFFFFFFFF`), 항상 Low → TDO 단락/버퍼 (`0x0`) |
| 신호 레벨 | scope | 각 JTAG 핀 | 1.8 V IO인데 3.3 V로 구동 / High 레벨이 VTref보다 한참 낮음 → 프로브 부하·직렬 저항 [8] |
| 리셋 상태 | scope | PORn, nRST, nTRST | nTRST가 low에 고정 → TAP이 리셋에 갇힘, PORn low → 칩 전체 리셋 |
| JTAG 관련 strap | 회로도 + scope | JTAG enable / chain 선택 strap | 보안 모드·다른 체인(FPGA, boundary-scan 전용) 선택 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 헤더/케이블/VTref 문제 | VTref 측정, 다른 케이블·프로브, golden board에 같은 프로브 | VTref 0 V, 다른 프로브로도 동일 |
| 2 | TCK 너무 빠름 / SI | TCK 10 MHz → 100 kHz로 낮춤 | 낮추면 읽힘 → 신호 품질·긴 배선 |
| 3 | 칩이 리셋에 갇힘 (PORn 미해제, supervisor가 어떤 PGOOD를 기다림) | PORn scope, PGOOD 체인 | PORn low 고정 → BV05로 |
| 4 | nTRST low 고정 / TAP 리셋 | nTRST 풀업 확인 | nTRST 0 V |
| 5 | JTAG 비활성 (secure debug, eFuse, strap, chain mux) | strap 레벨, 칩 문서의 debug enable 조건 | 다른 체인으로 선택돼 있음 |
| 6 | scan chain 구성 오류 (여러 칩 직렬, IR 길이 설정 틀림) | 디버거 auto-detect / 체인 하나씩 bypass | IDCODE가 shift된 쓰레기 값 |
| 7 | debug 전원 도메인 off | 해당 rail 측정 | debug/AON rail이 0 V |

**HW/SW 가르는 실험**
1. **프로브 swap + golden board**: 같은 프로브가 golden board에선 되나 → 프로브/설정 vs 이 보드.
2. **TCK를 10배씩 낮추기**: 되면 SI, 안 되면 연결/리셋.
3. **디버거 설정 = SW**: target config(IR 길이, DAP index, 체인 순서)가 틀려도 읽기 실패 → `jtag autoprobe`류 자동 감지로 설정 없이 체인만 스캔. 체인 스캔이 IDCODE를 찾으면 HW는 OK, 설정 문제.
4. **리셋 붙잡고/풀고**: PORn을 강제로 해제한 상태에서 재시도 → 리셋 문제 분리.

**결론 · 재발 방지** — Day 0 체크리스트에 "VTref·TCK/TDO 파형·IDCODE 값"을 기록. 팜 nightly 헬스 체크에서 **보드마다 IDCODE를 읽어** 프로브·케이블 노후를 조기에 잡는다 (BV14 §2.10). 보드팀에 디버거블리티 요청: JTAG 신호 테스트 포인트, nTRST 풀업, 직렬 저항 실장 확인.

<details><summary>🎤 English 2-minute answer</summary>

"IDCODE is the cheapest possible proof of life: the TAP only needs TCK and its own power, so if I can't read it, I'm looking at the signal path, the reset state, or JTAG being disabled. First the probe's own view: does it see target voltage on VTref? If VTref reads zero, a careful debugger won't drive the pins at all, so that's wiring or the header's supply. Then I scope TCK, TMS and TDI at the SoC side, not at the header, because a series resistor that's not populated or a level shifter without its enable will stop them in between. The TDO pattern is very informative: all ones usually means nobody is driving TDO, all zeros means it's shorted or a buffer is stuck, and a shifted value means the chain length or IR length in my configuration is wrong. I drop TCK by ten times — if that fixes it, it's signal integrity on long or loaded lines. Then reset: if PORn is still low because the supervisor is waiting for one PGOOD that never comes, the chip is in reset and the TAP may be too — and nTRST being held low does the same. Finally the straps or fuses that enable debug or select a different chain. To split hardware from software, I use the same probe on a golden board, and I do an auto-probe scan with no target configuration; if the scan finds the IDCODE, the hardware is fine and my debugger script is wrong. On the farm, reading every board's IDCODE nightly is part of the health check, which catches dying probes and cables before they look like silicon bugs."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. IDCODE reads, but you can't halt the core.** — "The TAP and DAP are alive but the core's debug logic isn't reachable: core power domain off, core clock not running, core held in reset by the reset controller, or debug authentication disabled. I check the DAP's power-up request acknowledge bits and the core's reset and power status." · 한국어: 코어 전원·클럭·리셋·debug 권한. (→ BV07)
- **Q. How do you debug the very first instruction after reset?** — "Connect under reset: hold reset, attach, set a vector catch or a hardware breakpoint at the reset vector, then release." · 한국어: connect under reset + vector catch.
- **Q. IDCODE manufacturer field is wrong.** — "Then I'm talking to a different device in the chain — maybe the FPGA or a PMIC with JTAG — so I fix the chain order or select the right chain strap." · 한국어: 체인의 다른 장치를 보고 있음.

</details>

### S3 · JTAG는 되는데 UART 출력이 없음

**증상** — JTAG로 halt·메모리 읽기 OK, 테스트 FW를 SRAM에 올려 실행했는데 터미널에 아무것도 안 나옴 (또는 깨진 글자).

```text
[예시 출력]  Trace32 스타일로 halt 후
  PC = 0x0000_1A2C   (BootROM 영역)   → 우리 코드로 아예 안 넘어감 → 이미지 로드/점프 문제
  PC = 0x2000_0410   (SRAM, uart_init 내부 polling 루프) → UART 상태 비트를 기다리며 멈춤 → UART 클럭/리셋
  PC = 0x2000_0008   (vector table, HardFault) → 초기화 중 예외
```

**🔰 배경** — UART 글자 하나가 나오려면: 핀 mux가 UART 기능 + UART 블록 **클럭 enable** + **리셋 해제** + baud divisor(입력 클럭 기준) + TX 핀이 보드 커넥터까지 이어짐(레벨 시프터·저항) + 터미널 설정(baud·TX/RX 교차·GND)이 전부 맞아야 한다. 하나라도 틀리면 "아무것도 없음".

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| PC 위치 | JTAG halt | — | BootROM / 우리 코드 / 예외 핸들러 → 어느 층인지 |
| TX 핀 토글 | scope | **SoC 볼 근처**와 커넥터 두 곳 | SoC에서도 없음 → FW·pinmux·클럭. SoC엔 있고 커넥터엔 없음 → 보드 경로 |
| 비트 폭 | scope (start bit 폭 측정) | TX | 115200 baud = 8.68 µs. 9.4 µs면 클럭이 약 8% 느림 → baud 오차 → 깨진 글자 (BV04) |
| 전압 레벨 | scope | TX, USB-UART 어댑터 쪽 | 1.8 V TX를 3.3 V 어댑터가 못 읽음 / 어댑터가 RX로 역급전 |
| UART·클럭·pinmux 레지스터 | JTAG 메모리 읽기 | 클럭 컨트롤러, pad 설정, UART 상태 | clock gate off, 블록 리셋 assert, pad function ≠ UART |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 핀 mux / 콘솔 UART 인스턴스 틀림 | 회로도의 콘솔 UART 번호 vs FW, pad 레지스터 | 다른 UART 핀에서 토글 |
| 2 | UART 클럭 미enable 또는 블록 리셋 미해제 | JTAG로 clock/reset 레지스터 | 상태 레지스터 polling에서 멈춤 |
| 3 | baud 계산의 기준 클럭 가정 틀림 | 비트 폭 측정 | 토글은 되는데 글자 깨짐 |
| 4 | TX/RX 교차·GND 미연결·레벨 | 어댑터 쪽 측정, 루프백 | SoC 토글 OK, 어댑터 무반응 |
| 5 | 이미지 로드/링커 주소/점프 오류 | PC가 BootROM, 로드 주소 확인 | 우리 코드 미실행 |
| 6 | 보드 경로 (직렬 저항 DNP, 레벨 시프터 enable) | 경로를 따라 scope | 중간에서 신호 사라짐 |

**HW/SW 가르는 실험**
1. **FW를 우회해서 HW 경로 증명**: JTAG로 UART 데이터 레지스터에 직접 `0x55`('U')를 써 본다 → 커넥터에 나오면 HW 경로 OK, **FW 초기화 순서가 문제**. 또는 TX 핀을 GPIO로 설정하고 JTAG로 토글 → scope로 핀·경로 확인.
2. **같은 바이너리를 golden board / FPGA에서** → 거기서 되면 이 보드 HW, 거기서도 안 되면 FW.
3. 루프백: 커넥터에서 TX-RX 단락 → 어댑터 쪽 문제 배제.

**결론 · 재발 방지** — bring-up FW의 **첫 명령은 LED/GPIO 토글**(UART 이전의 생존 신호), UART 초기화 직후 고정 문자열, 실패 시 JTAG/semihosting 대체 출력 ([BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)). 체크리스트: "콘솔 UART 번호·핀·레벨 = 회로도" 교차 확인.

<details><summary>🎤 English 2-minute answer</summary>

"Since JTAG works, the chip, power, clock and reset are alive, so I use the debugger to find out exactly where the code is. I halt and read the PC. If it's still in the BootROM range, my image never ran — that's a load address, linker or jump problem. If it's in my UART init, spinning on a status bit, the UART block probably has no clock or is still held in reset by the reset controller, which I can confirm by reading the clock-gate and reset registers over JTAG. If it's in the fault handler, initialization crashed. In parallel I scope the TX pin right at the SoC and at the connector. No activity at the SoC means firmware, pin mux or clock; activity at the SoC but not at the connector means the board path — an unpopulated series resistor or a level shifter that isn't enabled. If it toggles but prints garbage, I measure the start-bit width: at 115200 it should be about 8.7 microseconds, and if it's 8 percent longer my baud divisor assumes the wrong reference clock — that's a clock problem, not a UART problem. The cleanest hardware-versus-software split is to bypass my firmware entirely: write a character straight into the UART data register from the debugger, or drive the TX pad as a GPIO. If that shows up at the terminal, the hardware path is good and my init sequence is wrong. And to make this faster next time, my bring-up firmware toggles an LED as its very first action, before the UART exists, so I always have a heartbeat."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. How much clock error can UART tolerate?** — "Roughly a few percent total between both ends, because the receiver resamples at the start bit and the error accumulates over ten bits; beyond that the last bits are sampled in the wrong place." · 한국어: 대략 수 % [추정: 흔히 ±2–3% 수준], 10비트 동안 누적.
- **Q. The PC is in the HardFault handler. Next?** — "Read the fault status and fault address registers, and the stacked PC to find the faulting instruction — often an access to a peripheral whose clock isn't enabled yet." · 한국어: fault 레지스터 + stacked PC → 클럭 안 켠 블록 접근.
- **Q. What would you ask the board team for next spin?** — "A test point on console TX near the SoC, an LED on a GPIO, and the debug UART on a header with a documented voltage." · 한국어: TX 테스트 포인트, LED, 전압 표기된 헤더.

</details>

### S4 · 계속 재부팅 (몇 초마다 처음부터)

**증상** — UART에 부트 로그가 반복된다. 어느 지점에서 끊기고 다시 BootROM부터.

```text
[예시 출력]  UART
BOOTROM v2.1 mode=QSPI
FSBL: PMIC init ok
FSBL: PLL lock ok
FSBL: DDR training ...                 ← 여기서 끊김 (매번 같은 위치?)
BOOTROM v2.1 mode=QSPI                 ← 다시 처음 (간격 ≈ 2.1 s, 일정)
FSBL: RESET_REASON=BOR boot_count=17   ← FW가 reset reason과 부팅 횟수를 찍게 해 둔 경우
```

**🔰 배경** — 재부팅은 "누군가 리셋을 걸었다"는 뜻이고, **누가 걸었는지**는 칩의 reset reason 레지스터가 기억한다 (POR=전원 첫 인가, BOR=전압 저하, WDT=감시견, SW=소프트웨어 요청, PIN=외부 리셋 핀, thermal=과열). Linux라면 watchdog sysfs `bootstatus` 같은 곳에도 흔적이 남는다 [추정: 드라이버 지원 여부에 따라] ([BV05](2026-11-01_BV05_reset_strap_boot_mode.html)).

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| reset reason | FW 출력 / JTAG | reset cause 레지스터 (sticky, 읽고 지우기) | WDT·BOR·SW·PIN·thermal 중 무엇 |
| 마지막 로그 줄 | UART | — | 매번 같은 위치 → 그 단계가 트리거 (예: DDR init에서 전류 증가) |
| 재부팅 주기 | scope (긴 time base) 또는 로그 타임스탬프 | reset 핀 | 일정 주기 = WDT 타임아웃과 일치하나 |
| rail 파형 | scope, **reset 하강 엣지 트리거 + pre-trigger** | 코어·DDR rail | 리셋 직전에 rail이 꺼지면 BOR/PMIC fault |
| PMIC fault 레지스터 | I2C 읽기 (재부팅 후에도 sticky) | PMIC | UV/OV/OC/OT 비트 → PMIC가 전원을 끊었다 |
| 리셋 핀 노이즈 | scope | nRST / 버튼 / supervisor 출력 | 짧은 글리치 → supervisor 임계값·풀업 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | WDT: BootROM/eFuse가 WDT를 켰는데 FW가 모름, 또는 긴 루프(DDR training·flash erase) 중 kick 없음 | reset reason=WDT, 주기 = WDT 타임아웃 | WDT 끄거나 kick 추가 시 해결 |
| 2 | Brown-out: 특정 단계에서 부하 증가 → rail droop | reason=BOR, scope에서 리셋 직전 droop | PSU 직결·전압 +3% 시 해결 |
| 3 | PMIC fault로 전원 재시작 (OC·UV·OT) | PMIC fault 레지스터 | fault 비트 set, rail 전체가 꺼졌다 켜짐 |
| 4 | 예외 → fault 핸들러가 SW 리셋 | reason=SW, JTAG로 fault 핸들러에 breakpoint | 핸들러 진입 |
| 5 | 외부 감시 칩 (자동차 SBC/safety MCU 워치독) 미서비스 [추정: 보드 구성에 따라] | 외부 리셋 핀 scope, reason=PIN | 외부 칩이 리셋 핀을 당김 |
| 6 | 리셋 핀 글리치 / supervisor 임계값 | reason=PIN, scope | 짧은 low 펄스 |

**HW/SW 가르는 실험**
1. **WDT 배제**: 디버거 연결 시 WDT 정지 설정 또는 FW에서 WDT 비활성 빌드 → 루프가 멈추면 WDT 원인(그다음 "왜 kick을 못 했나"는 SW 문제일 수도, 클럭이 멈춘 HW 문제일 수도).
2. **전원 배제**: 해당 rail을 lab PSU로 직접(remote sense) 공급, 또는 PMIC로 +3% → 사라지면 PI/전원.
3. **트리거 지점 bisect**: FW에서 단계별로 멈춤(DDR init 전 `while(1)`) → 어느 단계부터 리셋되는지.
4. 주의: BOR이 원인이라도 **해결은 SW일 수 있다** — 블록을 한꺼번에 켜서 돌입 전류가 커진 것이면 순차 enable로 고친다. "HW 증상 = HW 수정"이 아니다.

**결론 · 재발 방지** — FW가 **매 부팅 reset reason + boot count + PMIC fault 레지스터**를 찍게 하고, 팜 파서가 예상치 못한 `RESET_REASON≠POR`를 실패로 처리 (BV14 코드의 `RESET_REASON=` 파서가 그 예). BOR이었다면 해당 단계의 rail droop 측정을 체크리스트에.

<details><summary>🎤 English 2-minute answer</summary>

"A reboot loop means something is pulling reset, and the chip usually remembers who: so the first thing I read is the reset-cause register — power-on, brown-out, watchdog, software, external pin or thermal. If firmware doesn't print it yet, I read it over JTAG right after a reset, and I make the bring-up firmware print it, plus a boot counter kept in a no-init RAM section, on every boot. Second, the last log line: if it dies at the same step every time — say DDR training — that step is the trigger. Third, the period: if it's constant and matches the watchdog timeout, I suspect a watchdog that the BootROM or a fuse enabled and my firmware doesn't kick during a long loop. To see power, I put the scope on the core and DDR rails, trigger on the falling edge of reset with lots of pre-trigger, and look for a droop just before it. I also read the PMIC's fault register, which is sticky across the reboot — an undervoltage or overcurrent bit means the PMIC itself power-cycled the board. Then I split causes with one change at a time: disable or pause the watchdog — if the loop stops, it's the watchdog; feed the rail from a lab supply with remote sense or margin it up three percent — if it stops, it's power. One subtlety: a brown-out isn't automatically a hardware fix — if firmware enables every block at once, the inrush is a software sequencing problem. Afterwards, the farm's log parser treats any unexpected reset reason as a failure, so a new reboot can't hide inside a passing run."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. Reset reason says watchdog, but the code kicks it.** — "Then the kick isn't happening when I think: a long blocking loop, an interrupt-driven kick with interrupts masked, a clock change that makes the watchdog faster, or the CPU actually hung — JTAG after the reset with a breakpoint just before the timeout tells me where." · 한국어: 블로킹 루프·인터럽트 마스크·클럭 변화·진짜 hang.
- **Q. Reset reason says brown-out.** — "Scope the rail with a trigger below the BOR threshold, find what load step precedes it, and check the PMIC's current limit and the regulator's transient response — then it's BV03 territory." · 한국어: BOR 임계 아래 트리거 → 부하 단계 → PMIC/레귤레이터.
- **Q. Reset reason is always POR, even though it reboots.** — "Then the whole supply is dropping — the PMIC or the input supply is cycling. Its fault register or the input current limit is next." · 한국어: POR = 전원 자체가 꺼졌다 켜짐.

</details>

### S5 · 엉뚱한 boot mode로 부팅

**증상** — QSPI에서 부팅해야 하는데 BootROM이 SD/USB 다운로드 모드로 감, 또는 "no boot device".

```text
[예시 출력]  UART
BOOTROM v2.1 mode=USB_DFU        ← 설계 의도: QSPI (strap 0b0010) [예시 값]
BOOTROM: waiting for host...

[예시 출력]  Zynq UltraScale+에서 래치된 모드 읽기 (JTAG 메모리 읽기)
  CRL_APB.BOOT_MODE_USER @ 0xFF5E0200 = 0x0000_0002   ← bit[3:0] = 리셋 해제 시 샘플된 PS_MODE 핀 값 [9]
```

**🔰 배경** — strap 핀은 **리셋이 풀리는 그 순간**에만 읽힌다(Zynq US+의 PS_MODE[3:0]은 POR 해제 시 샘플 [9]). 부팅 후 DMM으로 재면 그 핀이 이미 다른 기능(GPIO 등)으로 쓰이고 있을 수 있어서 의미가 없다. 그래서 **래치된 값을 레지스터로 읽고**, 그 순간의 전압을 **scope로 리셋과 함께** 본다.

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 래치된 boot mode | JTAG / BootROM 로그 | boot mode 레지스터 | 설계값과 비교 — 다르면 strap 레벨 문제, 같으면 이미지/fallback 문제 |
| 리셋 해제 순간 strap 전압 | scope 2ch: ch1 PORn, ch2 strap 핀, PORn 상승 엣지 트리거 | strap 핀 | 해제 시점에 레벨이 VIH/VIL 경계 근처·천천히 올라가는 중 → 타이밍/풀 강도 |
| 풀 저항 실장 | 육안·DMM·BOM | strap 저항 | DNP/값 틀림 |
| 다른 장치가 drive 중? | scope로 strap 핀 파형 모양 | 공유 핀 | 리셋 중에 다른 칩 출력이 핀을 당김 (깨끗한 풀업이 아니라 구동 파형) |
| IO bank 전원 타이밍 | scope | strap이 속한 VCCIO | 샘플 시점에 VCCIO가 아직 ramp 중 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | strap 저항 값/실장 오류 (BOM·DNP) | BOM 대조, 저항 측정 | 저항이 없거나 반대 방향 |
| 2 | 다른 장치가 리셋 중 그 핀을 drive (공유 핀) | 그 장치를 리셋에 묶거나 분리 | 분리하면 정상 |
| 3 | 외부 풀이 내부 풀/누설보다 약함 | 더 강한 풀(예 1 kΩ)로 임시 rework | 강하게 하면 정상 |
| 4 | 샘플 시점에 IO 전원 미안정 / 리셋이 너무 이름 | PORn vs VCCIO scope | 해제 시 VCCIO < 정상 |
| 5 | 프로브·어댑터가 핀을 로딩 | 외부 연결 제거 | 제거 시 정상 |
| 6 | (가짜 증상) mode는 맞는데 이미지 헤더 불량 → BootROM fallback | 래치 값 = 설계값 + BootROM 에러 코드 | fallback 경로로 감 = SW/이미지 문제 |

**HW/SW 가르는 실험**
1. **래치 값 vs 설계값**이 첫 분기: 다르면 HW(strap 회로/타이밍), 같으면 이미지·BootROM fallback(SW 쪽).
2. **boot mode override**: 칩이 지원하면 JTAG로 대체 boot mode를 설정하고 SW 리셋(Zynq US+ BOOT_MODE_USER의 alternate mode 비트 [9]) → 이미지가 정상 부팅하면 이미지 문제 배제.
3. 충돌 장치를 리셋에 묶거나 lift → 따라가는지.

**결론 · 재발 방지** — 팜의 **boot-mode 릴레이**로 모드를 설정하고 테스트 FW가 **래치 값을 읽어 기대값과 비교** (매 부팅 자동). 새 rev마다 "리셋 해제 시점 strap 레벨 scope" 체크리스트 항목. 회로도 리뷰 규칙: strap 핀을 리셋 중 drive할 수 있는 장치와 공유 금지 또는 그 장치를 같은 리셋에 묶기.

<details><summary>🎤 English 2-minute answer</summary>

"Strap pins are only sampled at the instant reset is released, so the first fork is: what did the chip actually latch? I read the boot-mode register — on a Zynq UltraScale+ that's BOOT_MODE_USER, whose low four bits show the PS_MODE pins sampled at POR release — or the BootROM's log line. If the latched value differs from the schematic intent, it's the strap circuit or its timing. If it matches but the chip still ends up in USB download mode, the BootROM probably rejected the image and fell back — that's an image or header problem, not a strap problem. For the strap itself, a multimeter after boot is useless because the pin may already be repurposed as a GPIO; I put one scope channel on the reset release and one on the strap pin and look at the level at that edge. Typical findings: a missing or wrong resistor from the BOM, an external pull too weak against the pad's internal pull or leakage, the IO bank's supply still ramping when the strap is sampled, or another device that drives that shared pin during reset — the waveform shows a driven edge instead of a clean pull. I confirm by temporarily strengthening the pull or holding the other device in reset, and I can use a boot-mode override from JTAG to prove the image itself is fine. To prevent recurrence, the farm sets boot mode through a relay and the test firmware reads back the latched value on every boot, and every new board revision gets a scope check of strap levels at reset release."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. The strap is right on the schematic but the chip boots in the wrong mode.** — "Then I look at what happens at the sampling instant: another device driving the pin, a pull too weak against leakage, or IO power not up yet. The schematic shows intent; the scope at reset release shows reality." · 한국어: 샘플 순간의 현실을 scope로.
- **Q. How do you recover a board that boots into nothing?** — "Use the BootROM's fallback or download mode — USB or UART boot, or JTAG boot mode — and reprogram the flash from there; that's why I want boot-mode control on every farm board." · 한국어: fallback/JTAG 모드로 복구, 팜 boot-mode 제어.

</details>

### S6 · DDR training 실패

**증상** — FSBL/SPL이 DDR training에서 실패, 또는 training PASS 후 memtest에서 바로 에러.

```text
[예시 출력]  training 로그 (벤더마다 형식 다름)
DDR: freq 3200 MT/s, LPDDR4x 2ch x16, 2 ranks
DDR: ZQ cal ........ PASS
DDR: CA training ... PASS
DDR: write leveling  PASS
DDR: read gate ..... FAIL  ch0 byte1        ← 특정 채널·바이트 레인만 실패
DDR: read eye ...... skipped
DDR: training FAILED (err=0x0004)
```

**🔰 배경** — training 단계마다 맞추는 것이 다르다 [10][11]: **write leveling** = 메모리에 도착하는 DQS(데이터 스트로브)와 CK(클럭) 엣지를 맞춤 (fly-by 배선이라 칩마다 클럭 도착 시간이 다름), **read gate** = 읽기 때 DQS가 언제 오는지 창을 여는 시점, **read/write eye** = DQ 하나하나를 DQS 가운데에 맞춤, **VREF** = 0/1 기준 전압. **어느 단계·어느 바이트 레인**에서 실패했는지가 원인 위치를 알려 준다 ([BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)).

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 실패 단계 + 바이트 레인 | training 로그 / PHY 레지스터 덤프 | — | 한 레인만 → 그 레인 배선·솔더. 전 레인 → 클럭·CA·전원·파라미터 |
| DRAM rail (VDD1/VDD2/VDDQ) | DMM + scope (ripple) | DRAM 근처 decoupling | 전압 낮음·ripple 큼 → 전원 |
| VREF (해당 시) | DMM | VREF 핀 | 설계값(보통 VDDQ 비율)과 다름 |
| CK 주파수·모양 | differential probe | DRAM 쪽 CK | 클럭 없음/틀린 주파수 |
| ZQ 저항 | DMM | ZQ 핀 | DDR4/LPDDR4 계열은 보통 240 Ω ±1% → 없거나 틀리면 임피던스 보정 실패 |
| 같은 설정의 레퍼런스 보드 결과 | 벤더 EVB | — | 거기서도 실패 → 파라미터. 거기선 PASS → 우리 보드 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 설정 파라미터 틀림 (BOM상 DRAM 부품 변경: 용량·rank·x16/x8, 타이밍, MR 값) | 실장된 DRAM 마킹 vs 설정, 벤더 툴 재생성 | 전 레인·전 보드 실패 |
| 2 | 전원/VREF/ZQ | 측정 | rail·VREF·ZQ 이상 |
| 3 | 솔더 불량 (BGA void·open) — 특정 바이트 레인 | 여러 보드 비교, X-ray, boundary scan 연결 테스트 | 한 보드의 한 레인만 |
| 4 | 레이아웃 (길이 미스매치·SI) | 저속 training 시 통과하나, 레이아웃 리뷰 [10] | 저속 PASS, 고속 FAIL |
| 5 | PHY 설정 (ODT·drive strength·VREF 초기값) | 벤더 권장값, ODT/VREF 스윕 [11] | 특정 값에서 PASS |

**HW/SW 가르는 실험**
1. **주파수 낮추기** (최저 speed bin): PASS → SI/타이밍 마진. 저속도 FAIL → 연결·파라미터·전원.
2. **같은 설정을 레퍼런스 보드(같은 DRAM 부품)** 에서 → 설정 vs 보드 분리.
3. **보드 여러 장**: 같은 레인이 모든 보드에서 실패 → 설계(배선). 한 보드만 → 조립. 보드마다 다른 레인 → 마진.
4. **boundary scan으로 DDR 핀 연결 확인** (training 없이 open/short) ([BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html)).

**결론 · 재발 방지** — 팜에서 **매 부팅 training 결과(바이트 레인별 eye 폭·delay)를 기록**해 보드·온도별로 추세를 보고, 최소값 임계 미만이면 알람. BOM 대체 부품이 생기면 DDR 설정 재검토가 ECO 회귀 범위에 자동 포함 (BV14 §2.13).

<details><summary>🎤 English 2-minute answer</summary>

"The training log is the first piece of evidence, and the two things I want from it are which step failed and on which byte lane. Each step tunes something different: write leveling aligns the strobe with the clock at each DRAM because of fly-by routing, read gate finds when the read strobe arrives, the read and write eye steps center each data bit in its strobe window, and VREF sets the decision threshold. If a single byte lane fails, I suspect that lane's routing or solder joints; if every lane fails, it's something common — the clock, the command bus, power, or simply the wrong configuration. And wrong configuration is the most common: the BOM got an alternate DRAM with a different density or rank count, so I check the marking on the actual part against the settings. Then I measure the DRAM rails and VREF, check the ZQ resistor, and look at the clock at the DRAM with a differential probe. My bisect knobs are frequency and platform: drop to the lowest speed bin — if training passes, it's timing margin or signal integrity; if it still fails, it's connectivity, parameters or power. Running the same configuration on the vendor's reference board with the same DRAM separates parameters from our board. Comparing several of our boards tells me design versus assembly: same lane failing everywhere is routing, one board is solder — which X-ray or a boundary-scan connectivity test confirms without needing training at all. Going forward, the farm logs per-lane training results on every boot, so a shrinking eye shows up as a trend long before it becomes a failure."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. Training passes, but memtest fails.** — "Training found a working point, but margin is thin or a specific line is bad. The memtest pattern tells me which: the same bit everywhere is a data line, aliasing is an address line, random bits are margin — so I run data-bus, address-bus and March tests separately." · 한국어: 패턴으로 데이터선/주소선/마진 구분. (→ BV09)
- **Q. Why does write leveling exist at all?** — "Because DDR3 and later route clock and command in fly-by, the clock reaches each DRAM at a different time, while each byte's strobe is point-to-point — so the controller delays each byte's strobe to line up with the clock at its DRAM." · 한국어: fly-by → DRAM마다 클럭 도착 시간 차이.

</details>

### S7 · 상온 OK, 고온에서만 실패

**증상** — 25 °C 모든 테스트 PASS, 챔버 85 °C에서 memtest(또는 링크·카메라) 실패.

```text
[예시 출력]  온도 스윕 결과 (보드 B08, ddr_march_c, 각 온도 50회)
  temp(C)   25    45    65    75    85    95
  fail/50    0     0     3    17    50    50      ← 65C 근처부터 시작하는 "벽(wall)"
  failing bit: DQ15 (byte lane 1) 89%, others 11%  ← 특정 비트 편중 = 특정 선의 마진
```

**🔰 배경** — 온도가 오르면 트랜지스터가 느려지고, 선의 저항·지연이 바뀌고, 레귤레이터·크리스털 값이 drift하고, DRAM은 데이터가 더 빨리 새어 나가 **refresh를 더 자주** 해야 한다 (DDR4는 85 °C 초과 확장 온도 구간에서 refresh 간격을 절반으로 — tREFI 7.8 µs → 3.9 µs; LPDDR4는 MR4 온도 플래그로 refresh 배율 조정) [추정: JEDEC 문서 미열람, 널리 알려진 값]. 상온에서 겨우 통과하던 **마진**이 고온에서 0이 되는 것.

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 실패 시작 온도 (벽) | 챔버 계단 스윕, 온도당 N회 | — | 급격한 벽 = 마진 경계, 완만 = 여러 원인 섞임 |
| 실패 시그니처 | memtest 로그 | 주소·비트 | 특정 비트 = 특정 선, 랜덤 = 타이밍, 시간 지나야 남 = refresh |
| 칩·보드 실제 온도 | 칩 온도 센서 + 열전대 | SoC·DRAM·PMIC | 챔버 85 °C ≠ 칩 접합 온도 (자기 발열로 더 높음) |
| rail 전압 at hot | DMM/scope | 코어·VDDQ | 레귤레이터 drift로 전압 하락 |
| training 결과 at hot | training 로그 | — | **고온에서 재부팅하면 PASS** + **상온 부팅 후 가열하면 FAIL** → 온도 drift 추적(재training/주기적 보정) 문제 |
| 클럭 at hot | frequency counter | osc 출력 | ppm drift (UART·링크) |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 타이밍/전압 마진 부족 (칩 corner + 보드) | V×T shmoo (BV12), 여러 보드 | 벽이 목표 온도보다 아래 |
| 2 | 상온 training 값이 고온에서 drift (주기적 재보정·DQS 추적 비활성) | 고온 부팅 vs 상온 부팅 후 가열 | 고온 부팅은 PASS |
| 3 | 고온 refresh 배율 미적용 | refresh 설정 확인, refresh 2배 강제 | 2배로 하면 PASS, 시간 지난 데이터만 깨짐 |
| 4 | 레귤레이터·VREF drift | 고온 전압 측정 | 전압이 spec 하한 근처 |
| 5 | 열팽창으로 솔더 crack 열림 (특정 보드) | 국부 가열/냉각 스프레이, 보드 비교 | 한 보드, 특정 부품 가열 시만 |
| 6 | thermal throttling/DVFS 버그 (SW) | throttling 로그, 고정 주파수 | 고정 주파수로 하면 PASS |

**HW/SW 가르는 실험**
1. **국부 가열** (heat gun / thermal forcer로 DRAM만, SoC만, PMIC만) 또는 냉각 스프레이 → 어느 부품이 민감한지.
2. **고온 부팅 vs 상온 부팅 후 가열** → training drift vs 순수 마진.
3. **보드·칩 swap**: 모든 보드가 같은 온도에서 벽 → 설계/칩, 한 보드만 → 그 보드(솔더·부품), SS corner 칩 보드만 → silicon 마진.
4. **FW 손잡이**: refresh 2배, 주기적 ZQ/재training on, 고정 주파수 → 사라지면 SW 설정으로 해결 가능.

**결론 · 재발 방지** — 검증 계획의 PVT 매트릭스에 **고온 memtest·링크 테스트** 고정, 팜 일부 보드를 챔버에 두고 nightly hot 회귀, 보드·온도별 training eye 추세 기록. mock 팜에서 B08이 바로 이 패턴: 25 °C 0/50, 85 °C 50/50 ([run_log.txt](onsitePrep/bv14/outputs/run_log.txt), BV14 §6).

<details><summary>🎤 English 2-minute answer</summary>

"Temperature-only failures are almost always margin, so my goal is to turn 'fails at hot' into a map. First I characterize: step the chamber from room temperature up in ten- or twenty-degree steps and run the test a fixed number of times at each point, so I find where the wall is and how sharp it is. I also look at the failure signature — if it's mostly one data bit, that's one line's timing margin; if data only goes bad after it has sat for a while, that points to refresh, since DRAM needs a higher refresh rate above about 85 degrees. And I measure the real chip and DRAM temperatures, because the chamber setpoint isn't the junction temperature. Then a few discriminating experiments. Boot hot versus boot at room temperature and then heat: if booting hot passes, the training is fine but the values drift and the firmware isn't re-calibrating, which is a configuration fix. Local heating with a thermal forcer or freeze spray on the DRAM, the SoC or the PMIC tells me which part is sensitive. Swapping boards and chips tells me whether it's one board — a solder joint opening — or every board, which is design margin, or only slow-corner chips, which is silicon margin. Then a voltage-versus-temperature shmoo quantifies how much margin is left. The fix might be firmware — refresh rate, periodic retraining — or hardware, or a spec adjustment. Either way, hot memory and link tests stay in the PVT matrix, and a few farm boards run them nightly in a chamber."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. It also fails at cold (−40 °C).** — "Different physics: cold makes transistors faster but can shift thresholds, crystals start slowly, and electrolytic caps lose capacitance. I'd sweep the cold side separately and check oscillator startup and regulator stability." · 한국어: 저온은 다른 메커니즘 — osc 기동, cap 용량 감소.
- **Q. How many boards and runs per temperature?** — "Enough to separate a rate from noise: at least a few boards across chip corners, and per point enough cycles that zero failures means something — by the rule of three, 100 clean runs only bound it below 3 percent." · 한국어: 보드 여러 장 + rule of three.

</details>

### S8 · 10장 중 2장만 실패

**증상** — 같은 rev, 같은 FW인데 B05·B08만 특정 테스트 실패.

```text
[예시 출력]  속성 표 (팜 인벤토리 DB에서)
 SN   rev  BOM-alt  rework            SoC lot      corner  DRAM lot  farm slot  i2c_scan  ddr@85C
 B01  A    -        -                 LOT17-W03    TT      D22       pdu1:1     PASS      PASS
 ...
 B05  B    C112->X  -                 LOT17-W03    TT      D22       pdu2:1     FAIL      PASS
 B08  B    C112->X  -                 LOT18-W02    SS      D24       pdu2:4     FAIL      FAIL
       ▲ i2c 실패는 rev B 두 장과 정확히 일치 → rev 따라감      ▲ ddr@85C는 B08만 (SS corner) → 칩 corner?
```

**🔰 배경** — "일부 보드만"은 **보드마다 다른 무언가**가 원인이라는 뜻이다. 그 무언가를 찾는 방법은 **속성 표를 만들고 실패와 정확히 일치하는 열을 찾는 것**, 그리고 swap으로 확인하는 것. 같은 rev라도 BOM 대체 부품·rework·칩 lot·조립 날짜·팜 슬롯이 다를 수 있다.

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 속성 표 | 인벤토리 DB (SN·rev·BOM·rework·SoC lot/corner·DRAM lot·조립일·슬롯) | — | 실패와 일치하는 열 = 1순위 가설 |
| good vs bad 측정 delta | DMM/scope, 같은 측정점 | rail·클럭·리셋 타이밍 | 차이가 나는 측정값 |
| training/eye 결과 비교 | 로그 | — | bad 보드만 eye 좁음 → 마진 |
| X-ray / 현미경 | — | 의심 BGA·부품 | 조립 결함 |
| board ID | 레지스터 / ADC | board-ID 저항 | **FW가 보드 ID로 다른 설정 경로를 탐** → 보드 차이처럼 보이는 SW 차이 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 팜 인프라 (슬롯·케이블·PSU·프로브) | bad 보드를 good 슬롯으로, good을 bad 슬롯으로 | 실패가 슬롯에 남음 → 보드 문제 아님 |
| 2 | BOM 대체 부품 / rework 이력 | 속성 표 | 해당 열과 정확히 일치 |
| 3 | 조립 결함 (솔더·방향) | X-ray, 현미경, 재납땜 | 한 장만, 재작업 후 해결 |
| 4 | 칩 corner (SS/FF) 또는 lot | 칩 swap (소켓/리볼), corner 정보 | 실패가 칩을 따라감 |
| 5 | board ID 의존 FW 경로 | board ID 값, FW 분기 | 보드 ID 다른 보드만 실패 |

**HW/SW 가르는 실험**
1. **ABA swap**: 칩(소켓 보드면 쉬움) bad→good 보드로 이동 → 실패가 따라가면 silicon, 남으면 board. **되돌려서(A로) 다시 실패하는지** 확인해 접촉·우연 배제.
2. 팜 슬롯 swap을 **가장 먼저** (가장 싸고, 의외로 흔함).
3. 같은 FW라도 board ID 분기를 확인 — "같은 바이너리 ≠ 같은 코드 경로".

**결론 · 재발 방지** — 인벤토리 DB를 모든 결과에 조인해 **자동 상관 분석** (mock 팜의 triage hint가 정확히 이것: "every rev B board, no other rev → follows board revision", "single board B08 (rev B, SS corner …)" — BV14 §6 실제 출력). 신규 보드 입고 스크린 테스트 추가.

<details><summary>🎤 English 2-minute answer</summary>

"When only some boards fail with the same firmware, something differs per board, and I find it with a table and then confirm it with a swap. The table comes straight from the farm inventory: serial number, revision, BOM alternates, rework history, SoC lot and process corner, DRAM lot, assembly date, and which farm slot the board sits in — joined against the pass/fail results. I look for a column that matches the failures exactly. Before anything clever, I swap the farm slot, because a bad cable, power outlet or debug probe is surprisingly common and looks exactly like a board problem. Then I measure the same points on a good and a bad board and compare deltas — rails, clocks, reset timing, training eye widths. The decisive experiment is an A-B-A swap of the suspect part: move the SoC — easy on a socketed board — or the DRAM from the bad board to a good one; if the failure follows the chip it's silicon, maybe a slow-corner part with less margin; if it stays with the board, it's assembly or BOM; and I move it back to make sure the failure returns, so I'm not fooled by reseating. One software trap: the same binary isn't always the same code path — if firmware reads a board-ID resistor and picks a different configuration, a 'board difference' can be a software difference. Afterwards, the correlation becomes automatic: every result is joined with inventory, so the dashboard says 'this signature only appears on rev B' or 'only on slow-corner chips' without anyone building a spreadsheet."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. The failure follows the chip. Is it a silicon bug?** — "Not yet — it's chip-dependent. It could be a process-corner margin issue rather than a design bug. I check the chip's corner and lot, run a shmoo on it versus typical parts, and only call it a silicon bug if it fails within spec conditions." · 한국어: 칩 의존 ≠ silicon 버그. corner 마진인지 spec 내 실패인지.
- **Q. You can't swap the SoC — it's soldered.** — "Then I swap everything else around it — DRAM via rework, PMIC, oscillator — and compare chip-level measurements like on-die sensors and training results across many boards to see whether the chip's own parameters correlate." · 한국어: 주변 부품 swap + 칩 고유 값 상관.

</details>

### S9 · cold boot OK, warm reset 후만 실패

**증상** — 전원을 껐다 켜면 항상 정상, SW 리셋/워치독 리셋 후에는 PCIe 장치 사라짐·카메라 안 됨·DDR init hang.

```text
[예시 출력]  리셋 종류별 1000회 루프 결과
  reset type          cycles  fail  first-fail signature
  power cycle (PDU)    1000     0   -
  ext reset pin        1000     0   -
  WDT reset            1000    37   pcie_link:HANG(after_START)
  SW reset (sysreset)  1000    41   pcie_link:HANG(after_START)
```

**🔰 배경** — 리셋에는 깊이가 있다. **cold(전원 사이클)** 는 모든 것을 처음으로, **warm(SW·WDT)** 은 보통 rail은 켜진 채 칩 일부만 리셋. 그래서 warm reset 후에는 ① PMIC 설정이 그대로 ② strap이 다시 샘플되지 않을 수 있음 ③ always-on 블록·sticky 레지스터는 이전 값 ④ PLL이 이미 돌고 있는 상태에서 FSBL이 재설정 ⑤ **보드의 외부 장치(PCIe endpoint·PHY·센서)는 리셋을 못 받았을 수 있음** — 그 장치들의 리셋 선이 POR에만 묶여 있으면.

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 리셋 종류별 실패율 | 팜 루프 | — | 어떤 리셋만 실패하나 (cold/pin/WDT/SW) |
| 외부 장치 리셋 선 | scope (warm reset 이벤트 트리거) | PERST#, PHY reset, 센서 enable | warm reset 때 외부 리셋이 안 내려감 |
| 레지스터 diff | JTAG, FSBL 진입 시 halt | 문제 블록·클럭 컨트롤러 | cold vs warm 진입 시 값이 다름 → FW가 리셋 기본값을 가정 |
| I2C 버스 상태 | scope | SDA/SCL | 리셋 순간 트랜잭션 중이던 slave가 SDA를 잡고 있음 → S11 |
| reset reason | 레지스터 | — | FW가 리셋 종류를 구분해 처리하나 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 외부 장치가 warm reset을 못 받음 (보드 리셋 배선) | 외부 리셋 선 scope | 그 선이 POR에만 연결 |
| 2 | FW가 POR 기본값을 가정 (이전 상태 미정리) | 레지스터 diff, FW에서 명시적 리셋/재초기화 | 재초기화 추가 시 해결 |
| 3 | PLL/클럭이 돌고 있는 채로 재설정 (글리치) | 클럭 재설정 순서 확인 | bypass로 먼저 전환 후 재설정 시 해결 |
| 4 | DDR 상태(self-refresh 등) 미처리 | DDR init 경로 로그 | warm 경로에서만 training 실패 |
| 5 | I2C slave stuck (리셋 중 트랜잭션 끊김) | SDA low 지속 | 버스 복구(9 clock) 시 해결 |
| 6 | PMIC가 warm reset에서 rail을 재시퀀싱하지 않음 | PMIC 동작 문서·scope | rail 그대로, 장치는 이상 상태 |

**HW/SW 가르는 실험**
1. **외부 장치를 수동으로 리셋**(GPIO 토글/점퍼) 후 warm reset → 해결되면 보드 리셋 배선 또는 FW가 그 GPIO를 토글해야 함 (HW 수정 vs SW workaround 둘 다 가능).
2. **FW에 명시적 재초기화** 추가 (블록 리셋 assert→deassert) → 해결되면 SW 가정 문제.
3. 리셋 종류 매트릭스로 "어느 깊이부터" 실패하는지.

**결론 · 재발 방지** — 회귀에 **리셋 매트릭스** (cold·pin·WDT·SW × N회 × 체크 항목) — PCIe 판은 [LP14 §2.4 트리거 표](2026-10-31_LP14_validation_methodology.html). 회로도 리뷰 체크리스트: "모든 외부 장치 리셋 선은 SoC가 제어 가능한가, warm reset 때 누가 내리나".

<details><summary>🎤 English 2-minute answer</summary>

"Cold boot working and warm reset failing tells me the difference between the two reset depths is the bug. A power cycle resets everything; a software or watchdog reset usually keeps the rails up and resets only part of the SoC. So after a warm reset the PMIC keeps its state, straps may not be resampled, always-on and sticky registers keep old values, PLLs are already running, and — most often on a board — external devices like a PCIe endpoint, an Ethernet PHY or a camera sensor never got reset, because their reset lines are tied to power-on reset only. First I quantify by reset type in the farm: a thousand cycles each of power cycle, reset pin, watchdog and software reset — that shows exactly which depth fails. Then I scope the external devices' reset lines triggered on the warm reset: if PERST# or the PHY reset never goes low, that's the answer. In parallel I halt at the first-stage bootloader's entry with JTAG on a cold and on a warm boot and diff the registers of the failing block and the clock controller — if they differ, the firmware is assuming power-on defaults. Experiments: manually reset the external device before the warm reset, or add an explicit assert-and-release of the block reset in firmware. Either can be the fix — a board change to route the reset, or firmware that toggles a GPIO — and I'd present both with their cost. The permanent guard is a reset matrix in nightly regression, and a schematic review rule that every external device's reset is controllable by the SoC."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. Why might the BootROM behave differently on a warm reset?** — "Some BootROMs skip steps or take a different path when the reset reason isn't power-on, for example keeping a previous boot mode or skipping clock setup — so the reset reason and the boot path need to be tested per reset type." · 한국어: BootROM이 reset 원인에 따라 경로가 다를 수 있음.
- **Q. HW fix or SW fix?** — "I lay out both: firmware toggling the device reset is cheap and immediate; routing the reset in hardware is robust against firmware bugs and needed for safety cases. Usually the workaround ships now and the board fix goes into the next revision." · 한국어: SW workaround 지금, HW 수정 다음 rev.

</details>

### S10 · 부하 걸 때 랜덤 hang

**증상** — 아이들 상태에선 며칠 멀쩡, NPU+DDR+카메라 풀 부하 테스트에서 수 분~수 시간 사이 랜덤 hang (UART 무응답, WDT 리셋 또는 그냥 멈춤).

```text
[예시 파형]  코어 rail, AC 결합, 20 mV/div, 트리거 = 0.75 V − 4% 아래로 떨어짐
   0.75 V ───────╮      ╭───────────────
                  ╲    ╱                 ← NPU 시작 순간(GPIO 마커 ch2 상승) 직후 38 mV 언더슛, 6 µs
                   ╲__╱
   ch2 (NPU start marker) ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾
```

**🔰 배경** — 큰 블록(NPU)이 갑자기 일을 시작하면 전류가 수 A 단위로 순간 증가하고, 전원망(PDN)과 레귤레이터가 따라잡기 전까지 전압이 **순간 꺼진다(droop)**. 그 순간 가장 마진이 작은 경로의 타이밍이 깨지면 → 랜덤 hang. **DVFS**(전압·주파수 조절)에서 주파수를 먼저 올리고 전압을 나중에 올리는 순서 버그도 같은 증상 ([BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html), [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)).

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 재현률 | 팜 스트레스 루프 | — | "시간당 X회" — 이게 있어야 실험 비교 가능 |
| rail droop | scope, AC 결합, 짧은 ground spring, **언더슛 트리거** | SoC 근처 sense 점 / decoupling | droop 크기·폭이 spec(예 −3~5%)을 넘나 |
| 이벤트 마커 | FW가 GPIO 토글 (NPU 시작·DVFS 전환) | scope 다른 채널 | droop/hang이 어떤 SW 이벤트와 동시인가 |
| 전류 | current probe / PMIC telemetry | rail | 부하 계단 크기 |
| hang 직후 상태 | JTAG attach (halt) | 모든 코어 | PC가 제각각 정상 = SW deadlock 의심, 모두 bus 접근에서 멈춤 = 인터커넥트/HW, 코어 응답 없음 = 전원/클럭 |
| 온도 | 칩 센서 | — | 온도 상승 후에만 → 열·마진 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | PI droop (부하 계단 > PDN·레귤레이터 응답) | 언더슛 트리거 scope, Vcore +3% 실험 | 전압 올리면 사라짐, droop이 hang과 동시 |
| 2 | DVFS 전환 순서/타이밍 버그 | DVFS 비활성(고정 OPP), 전환 마커 | 고정하면 사라짐 |
| 3 | 열 (throttling 미동작·마진) | 냉각/온도 기록 | 온도 상관 |
| 4 | SW race/deadlock (드라이버·인터럽트) | JTAG 상태, SW만 변경 (부하 종류 분리) | 코어들이 lock 대기 |
| 5 | 클럭 전환 글리치 | 클럭 소스 전환 고정 | 전환 없애면 사라짐 |
| 6 | silicon errata (인터커넥트·캐시) | errata 확인, 최소 재현 | 특정 접근 패턴에서만 |

**HW/SW 가르는 실험**
1. **전압 +3~5% margining**: SW 동작은 그대로인데 hang이 사라지면 → 전압 마진(PI). SW race는 전압으로 고쳐지지 않는다(온도·주파수는 타이밍을 바꿔 SW race도 바꿀 수 있으니 이 실험이 가장 깨끗).
2. **주파수 고정(DVFS off)** → DVFS 경로 분리. **주파수 낮춤** → 타이밍 마진.
3. **부하 분해**: 메모리만 / NPU만 / 카메라만 → 어떤 블록이 트리거.
4. **다른 보드**에서 재현률 비교 → 보드 PDN 편차 vs 설계.
5. 각 실험마다 **같은 시간·같은 횟수** (예: 각 4시간), 재현률이 낮으면 rule of three로 판단.

**결론 · 재발 방지** — 검증 계획의 **stress/soak** 항목(부하 프로파일 × 시간 × 보드 수)과 **DVFS 전환 스트레스**(전환 10^5–10^6회) 추가, PI 특성화(부하 계단 droop 측정)를 체크리스트에. 팜 hang 감지 → 자동 JTAG 덤프(PC·코어 상태) → 재현 패키지.

<details><summary>🎤 English 2-minute answer</summary>

"A random hang under load is the classic case where the symptom is at the top and the cause can be at the very bottom, so I start by making it measurable: a fixed stress profile on the farm — NPU, DDR and camera all running — and a hang rate per hour, because without a rate I can't compare experiments. Then I instrument the moment of failure. The scope goes on the core rail at the SoC's sense point, AC-coupled with a short ground spring, triggered on an undershoot below, say, four percent of nominal, and firmware toggles a GPIO when the NPU starts a job or when DVFS changes the operating point, so I can see whether a droop lines up with a software event. When it hangs, the farm attaches JTAG before power-cycling: if every core is still executing but waiting on locks, that smells like a software deadlock; if they're all stuck on a bus access, it's the interconnect; if the debugger can't reach the cores, power or clock. The cleanest discriminator is voltage margining: raise the core rail three to five percent — software doesn't care about voltage, so if the hang rate drops to zero, it's power integrity or timing margin. Fixing the frequency with DVFS off isolates the DVFS sequence, and splitting the load tells me which block's current step triggers it. Each experiment runs the same duration so the rates are comparable. The outcome feeds the plan: a soak test with this load profile, a DVFS transition stress test, and a droop measurement in characterization — and the hang detector on the farm automatically captures a JTAG snapshot as part of the repro package."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. Your scope shows 200 mV of noise but the HW engineer sees 20 mV.** — "Probably my long ground lead picking up switching noise and full bandwidth; I'd redo it with a ground spring right at the capacitor and the 20 MHz bandwidth limit for ripple, but full bandwidth with the short ground for droop." · 한국어: 긴 GND 리드 → spring, ripple은 20 MHz BW limit. (→ BV11)
- **Q. Raising voltage fixes it. Are you done?** — "No — that tells me it's margin, not where. Next is whether it's the PDN, the regulator's transient response or a slow-corner chip, so I measure droop across boards and chips and talk to the PI team with data; a voltage bump may be the workaround, not the fix." · 한국어: 전압 올림은 원인 위치가 아니라 종류만 알려 줌.

</details>

### S11 · I2C 장치가 NACK

**증상** — 센서/PMIC/EEPROM이 응답 안 함. `i2cdetect`에 주소가 안 보이거나 드라이버가 `-ENXIO`/`-EREMOTEIO`.

```text
[예시 출력]  Linux
$ i2cdetect -y -r 3
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --      ← 0x50 EEPROM 기대, 없음
$ dmesg | grep i2c
at24 3-0050: probe with driver at24 failed with error -6   [예시]

[예시 파형]  scope ch1 SCL, ch2 SDA, I2C decode
  START | 0xA0 (addr 0x50 W) | 9th clock: SDA HIGH = NACK | STOP
  SDA rise time 1.6 us  (100 kHz 표준 모드 한계 1000 ns 초과)      ← 풀업 너무 약함/버스 용량 큼
```

**🔰 배경** — I2C는 선 2개(SCL 클럭, SDA 데이터)를 여러 장치가 **open-drain**으로 공유한다: 아무도 안 당기면 **풀업 저항**이 High로 올림. 주소 바이트 뒤 **9번째 클럭에 장치가 SDA를 Low로 당기면 ACK**, 안 당기면 NACK. 상승 시간 한계: 표준 모드(100 kHz) 1000 ns, fast mode(400 kHz) 300 ns [추정: UM10204 표 값, 원문 미열람]. SDA가 계속 Low면 **slave가 트랜잭션 중간에 멈춘 것** → SCL 9클럭 + STOP으로 복구 (UM10204 §3.1.16 bus clear, Linux `i2c_bus_recovery_info` [1][2]).

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| 와이어 위 주소 | scope/LA + I2C decode | SCL/SDA, 장치 근처 | 기대 주소인가 (7비트 0x50 = 쓰기 바이트 0xA0) — 다르면 SW |
| ACK 비트 | 같은 캡처 | 9번째 클럭 | NACK + 주소 맞음 → 장치 쪽 |
| 상승 시간·High 레벨 | scope | SDA/SCL | 너무 느림 → 풀업·용량, High가 낮음 → 전압 도메인 |
| idle 레벨 | DMM/scope | SDA/SCL | SDA 계속 Low → stuck bus, 둘 다 0 V → 풀업 전원 없음 |
| 장치 전원·리셋·enable | DMM | 장치 VDD, RESET/EN 핀 | 장치가 꺼져 있거나 리셋 중 |
| 주소 strap | DMM | A0–A2 핀 | 실제 주소가 FW 기대와 다름 |
| 레벨 시프터 | scope 양쪽 | 시프터 A/B | 한쪽에서만 신호, enable 미설정 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 주소 오류 (7비트/8비트 혼동, strap 다름, 대체 부품의 다른 주소) | 와이어 decode, `i2cdetect`로 전체 스캔 | 다른 주소에서 응답 |
| 2 | 장치 전원 off / 리셋 중 / enable 미설정 | 장치 VDD·RESET 측정 | 0 V 또는 RESET low |
| 3 | 풀업 문제 (없음·너무 약함·전원 도메인 틀림) | 상승 시간·High 레벨 | rise time 초과, 낮은 High |
| 4 | 레벨 시프터·전압 도메인 (1.8 V ↔ 3.3 V) | 양쪽 파형 | 한쪽만 신호 |
| 5 | stuck bus (slave가 SDA 잡음) | idle SDA low | 9클럭 복구 후 정상 |
| 6 | 속도 초과·clock stretching 미지원 | 100 kHz로 낮춤 | 저속에서 ACK |
| 7 | 장치 불량·솔더 | 다른 보드, 재납땜 | 한 보드만 |

**HW/SW 가르는 실험**
1. **와이어 위 주소가 맞나?** — 틀리면 SW(주소·버스 번호·드라이버 DT). 맞는데 NACK이면 장치·보드.
2. **최소 SW**: 드라이버 없이 `i2cdetect -y -r N`(주의: 일부 장치는 probe 방식에 민감 — `-r` read 방식 사용), 또는 GPIO bit-bang → SW 스택 배제.
3. **다른 보드 / 다른 버스의 같은 장치** → 장치 vs 보드.
4. 속도 100 kHz, 다른 slave 분리, 외부 풀업 임시 추가 → 각각 따라가는지.

**결론 · 재발 방지** — 테스트 FW/팜에 **보드 rev별 "기대 I2C 장치 목록" 스캔 테스트** (mock 팜의 `i2c_scan`이 이 역할: rev B의 0x50 NACK을 잡음), 드라이버에 bus recovery, 회로도 리뷰에 "풀업 값 vs 버스 용량·속도" 계산. 대체 부품 BOM 변경 시 주소 확인을 ECO 체크리스트에.

<details><summary>🎤 English 2-minute answer</summary>

"An I2C NACK has a very clean first split: is the right address actually on the wire? I put a scope or logic analyzer with I2C decode on SCL and SDA near the device and look at the address byte and the ninth clock — the device acknowledges by pulling SDA low. If the address on the wire is wrong — the classic is a seven-bit versus eight-bit mix-up, 0x50 versus 0xA0, or the wrong bus number — it's software. If the address is right and nobody acknowledges, it's the device or the board, and I go down the list: is the device powered and out of reset, are its address-strap pins at the levels firmware assumes — an alternate BOM part can even have a different default address — and is the bus electrically healthy. On the scope I check rise time against the mode — about 300 nanoseconds for 400 kHz fast mode — and the high level, which tells me if the pull-ups are too weak, missing, or tied to the wrong voltage domain, and I check both sides of any level shifter. If SDA sits low all the time, a slave got stuck mid-transfer, and the standard bus clear is up to nine clocks on SCL and a STOP — the Linux I2C core has a recovery hook for exactly that. To remove software from the picture, I scan with i2cdetect or bit-bang the GPIOs, then compare the same device on another board. Lowering the speed to 100 kHz and temporarily adding a stronger pull-up are cheap one-variable tests. For prevention, the bring-up suite has an expected-device scan per board revision, so a missing or moved address fails immediately."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. Why 9 clocks?** — "A stuck slave is somewhere inside a byte waiting for clocks — at most eight data bits plus the ACK slot — so nine clocks guarantee it finishes and releases SDA, then a STOP resets the bus state." · 한국어: 최대 8비트 + ACK = 9. (→ BV10)
- **Q. How do you size the pull-up?** — "From bus capacitance and the rise-time limit for the speed mode: the RC time constant must give a rise within the limit, and the resistor must be large enough that devices can still pull the line low within their sink current." · 한국어: 용량 × 저항으로 rise time, 하한은 sink 전류.

</details>

### S12 · 보드 rev B에서 새로 생긴 실패

**증상** — rev A에서 전부 PASS였던 테스트가 rev B 보드에서만 실패. 마침 FW도 새 빌드.

```text
[예시 출력]  2x2 매트릭스 (FW 버전 × 보드 rev), i2c_scan, 각 칸 20회
                   FW 0.9.2 (old)    FW 0.9.3 (new)
   rev A (B01)        20/20 PASS       20/20 PASS
   rev B (B05)        0/20 PASS        0/20 PASS      ← rev를 따라감, FW와 무관 → ECO/BOM 쪽
```

**🔰 배경** — 새 rev = ECO 목록만큼 바뀐 보드 + (종종) 같은 시기에 바뀐 FW + (가끔) 다른 BOM 대체 부품·조립 라인. 원인을 찾으려면 **변수를 분리**해야 하고, 가장 강력한 도구는 **2×2 매트릭스(old/new FW × rev A/B)**.

**잴 것 · 볼 것**

| 무엇 | 장비 | 어디서 | 읽은 값이 뜻하는 것 |
|---|---|---|---|
| ECO 목록 + 회로도/레이아웃/BOM diff | 문서 | — | 실패 블록과 겹치는 변경 = 1순위 |
| 2×2 매트릭스 | 팜 | — | rev 따라감 / FW 따라감 / 둘의 조합 |
| 실패 블록의 측정 비교 | scope/DMM, rev A vs rev B 같은 측정점 | 바뀐 net | 풀업·전압·타이밍 차이 |
| board ID / rev 인식 | 레지스터 | — | FW가 rev B를 rev B로 인식하나, 그 설정 테이블이 맞나 |
| 대체 부품의 데이터시트 차이 | 문서 | — | 기본 주소·전원 투입 기본값·타이밍 |

**가설 우선순위**

| 순위 | 가설 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | ECO의 직접 영향 (값 변경·재배선) | ECO 목록 ↔ 실패 블록 | 겹침 |
| 2 | ECO 부작용 (다른 부품 기본값·주소·타이밍) | 대체 부품 데이터시트 | 기본값 차이 |
| 3 | FW의 rev 처리 (board ID 테이블·핀맵 미반영) | board ID 값, 설정 경로 | rev B 테이블 틀림 |
| 4 | 같은 시기 FW 변경 | 2×2 매트릭스 | FW를 따라감 |
| 5 | 조립 배치·라인 차이 | rev B 여러 장 비교 | 일부 rev B만 |

**HW/SW 가르는 실험**
1. **2×2 매트릭스**: 한 칸만 바꿔 가며 비교 — 가장 먼저.
2. **ECO 되돌리기/적용하기**: rev B에서 그 변경을 rework로 되돌림, 또는 rev A에 적용 → 실패가 따라오면 그 ECO 확정.
3. board ID를 강제로 다른 값으로 → FW 경로 차이 확인.

**결론 · 재발 방지** — ECO마다 **영향 테스트 목록**을 추적성 표(net → 인터페이스 → 테스트)로 뽑아 **새 rev 승인 전에 팜에서 전체 + 영향 테스트 집중 실행** (BV14 §2.13). 보드 rev별 설정 테스트(rev 인식·기대 장치 목록). mock 팜에서 "every rev B board, no other rev → follows board revision"이 자동으로 나온 것이 이 시나리오.

<details><summary>🎤 English 2-minute answer</summary>

"A failure that appears with a new board revision almost always comes with more than one change — the ECOs, maybe a BOM alternate, and often new firmware in the same week — so my first job is to separate the variables. The fastest tool is a two-by-two matrix: old and new firmware on a rev A and a rev B board, same test, same number of runs per cell. If the failure follows the board revision regardless of firmware, it's hardware or how firmware handles that revision; if it follows the firmware, I bisect builds. Then I get the ECO list and the schematic, layout and BOM diffs, and I look for changes that touch the failing block — a pull-up value, a re-routed net, a different regulator — but also side effects: an alternate part with a different default I2C address or a different power-up state. I also check that firmware recognizes the new revision — the board-ID resistor and the per-revision configuration table are a common miss. To confirm, I revert the suspect ECO on a rev B board with a rework, or apply it to a rev A board; if the failure moves with the change, that's proof. Measurements on the changed net on both revisions give the data for the board team. For prevention, every ECO comes with an impact list generated from traceability — which nets, which interfaces, which tests — and a new revision isn't accepted until the farm has run the full suite plus a focused pass on those impacted tests on several boards."

</details>

<details><summary>🔁 꼬리 질문</summary>

- **Q. The board team says nothing in the ECO touches I2C.** — "Then I show them the 2x2 matrix and the rise-time capture on both revisions; if rev B's waveform differs, something in the electrical path changed — a BOM alternate, a shared supply, or layout — and the data narrows where to look." · 한국어: 매트릭스 + 양 rev 파형으로 데이터 대화.
- **Q. How do you scope regression for the next ECO?** — "From traceability: map each changed net to the interfaces and requirements it touches and run those tests on several boards at the relevant corners, plus the full nightly suite — impact-focused, not everything at every corner." · 한국어: 변경 net → 요구사항 → 테스트 (→ BV14).

</details>

## 4. 꼬리 질문 체인 (시나리오를 넘는 공통 패턴)

### 4.1 BV00 패턴 — "What's first?" → "That's fine." → "Board or silicon or software?" → "Never again?"

<details><summary>답 펼치기</summary>

- **Q. A board doesn't do X. What's the first thing you check?** — "The layer below X. Before I trust any symptom, I want evidence that power, clock and reset are right on this board: rail voltages and current against the table, the oscillator, reset released after the last PGOOD. Most 'interface bugs' on a new board are one of those." · 한국어: 증상 층 아래부터 증거.
- **꼬리1. That's fine. What next?** — "Then I go one layer up and get the next piece of evidence — JTAG IDCODE and halt, PC location, UART, training results — until the first layer that doesn't have its evidence. That's where I spend my time." · 한국어: 증거가 끊기는 첫 층.
- **꼬리2. How do you prove it's the board and not the silicon or the software?** — "By what the failure follows. Same binary on a golden board; a known-good older build and a minimal bare-metal sequence on this board; an A-B-A swap of the chip or the suspect part; and a condition sweep. Board-only, chip-following, or build-following each point to a different owner — and I always run the swap back to confirm." · 한국어: 무엇을 따라가나 + ABA.
- **꼬리3. How do you make sure it never comes back?** — "Root cause becomes either an automated regression on the farm — if it's something software or a test can catch — or a measured item on the bring-up checklist for every new revision, if it's a board property. And the bug report records what I ruled out, so the next person doesn't repeat it." · 한국어: 자동 회귀 또는 체크리스트 + 배제 기록.

</details>

### 4.2 헤드라인 — "It doesn't boot. Debug it."

<details><summary>답 펼치기</summary>

- **Q. The board doesn't boot. Debug it.** — (§0의 30초 답) + "The first question I ask back is: what does 'doesn't boot' mean — no current, no UART, a reboot loop, or a hang at a specific log line? Each one starts me at a different layer." · 한국어: "안 된다"를 먼저 쪼갬.
- **꼬리1. Nothing on UART, current looks normal.** — "Then JTAG: IDCODE, halt, PC. The PC location tells me whether the BootROM ran, whether it loaded my image, or where it crashed." · 한국어: JTAG halt → PC.
- **꼬리2. PC is stuck in the BootROM.** — "The BootROM didn't find a valid image: I read the latched boot mode and any BootROM error code, verify the flash contents and header over JTAG, and check the boot device's clock and pins. Sometimes I bypass it entirely by loading my test firmware into SRAM through JTAG." · 한국어: boot mode 래치값·BootROM 에러·이미지 헤더, JTAG로 SRAM 로드.
- **꼬리3. You only have one A0 board. What do you do differently?** — "I'm conservative: current-limited supplies, rails brought up one at a time, no probing with long leads on fragile nodes, rework only after a second opinion, and I log every measurement so I don't have to repeat risky steps." · 한국어: 단 하나의 A0 보호.

</details>

### 4.3 "1-in-200 failure on the farm" — 간헐 실패

<details><summary>답 펼치기</summary>

- **Q. A test fails once in 200 runs, only on some nights. What do you do?** — "First make it self-documenting: every failure must capture the UART log, a JTAG snapshot, rail telemetry and conditions automatically, because I can't wait by the bench. Then quantify with a dedicated loop — a few thousand runs — so I have a rate with a confidence bound, and slice the failures by board, slot, temperature, time since boot, and firmware build." · 한국어: 실패가 증거를 남기게 + 루프로 율 측정 + 속성별 분해.
- **꼬리1. It only shows on some nights — why?** — "Because a nightly run is a few samples of a low-probability event; in my mock farm, a board with a true 20 percent failure rate on one test looked completely green in four out of six seeded nightly runs. A green night doesn't mean fixed." · 한국어: mock 팜 실제 결과 — 6번 중 4번 green (BV14 §6).
- **꼬리2. How do you speed it up?** — "Stress the suspected axis to raise the rate — temperature, voltage margin, a tighter reset cadence — and confirm the signature is the same, then bisect at the higher rate." · 한국어: 스트레스로 율을 올리고 시그니처 동일 확인.
- **꼬리3. When is it fixed?** — "When the loop at the original condition shows zero failures in enough runs: for a 1-in-200 rate, about 600 clean runs gives 95% confidence it's below the old rate — rule of three." · 한국어: 3/p ≈ 600회 무실패.

</details>

### 4.4 "The HW team says it's software" (behavioral 브릿지)

<details><summary>답 펼치기</summary>

- **Q. You believe it's a board issue; the HW team says it's firmware. What do you do?** — "I move the discussion from opinions to an experiment we agree on in advance. I propose: if the failure reproduces with a minimal bare-metal sequence, or follows the board in an A-B-A swap with identical firmware, it's the board; if it follows the firmware build, it's ours. Then I run it and share the raw data — waveforms, register dumps, the matrix — and I'm equally ready to own it if the data points to us." · 한국어: 판정 기준을 먼저 합의 → 실험 → 원본 데이터 공유, 우리 것이면 인정.
- **꼬리1. They still disagree.** — "Then I ask what evidence would change their mind and go get that — often a specific measurement on their side of the interface. If it's blocking, I escalate with a clear summary of what's known and the cost of the delay, not as a blame." · 한국어: 무엇이면 납득하나 → 그 데이터 확보, 비용 중심 에스컬레이션.
- **꼬리2. It turned out to be your firmware.** — "Good — the experiment did its job. I fix it, add the regression that would have caught it, and say so in the same channel where I raised the issue." · 한국어: 같은 채널에서 인정 + 회귀 추가.

</details>

### 4.5 "Tell me about a hard board bug you debugged." — Don STAR 틀

<details><summary>답 펼치기</summary>

- **Q. Hardest board-level bug?** — Don 버전 (채우기): "At Apple, a new wireless chipset on a new board [증상 채우기: e.g. intermittently failed PCIe enumeration / I2C/SPMI transactions] about [숫자 채우기] in [숫자 채우기] boots. I made each failure capture [증거 채우기], measured [DSO/protocol analyzer 관측 채우기], and found it followed [board / chip / build — 채우기]. Root cause was [채우기]; the fix was [HW ECO / FW change — 채우기]; failure rate went from [숫자 채우기] to zero in [숫자 채우기] runs, and I added [회귀 테스트 채우기]." · 한국어: §2.5 리포트 6칸 순서로 말하기.
- **꼬리1. What hypothesis did you drop and why?** — 준비: 처음 가설 1개 + 그걸 버린 측정 [채우기].
- **꼬리2. What would you do differently?** — 준비: "automatic evidence capture on the first day instead of after a week of manual repro" 류 [채우기].

</details>

## 5. Don 경험 연결

- "At Apple my daily work is root-causing failures when a new wireless chip meets the full system — PCIe, I2C, SPMI, RFFE — with a scope and a protocol analyzer, and the core skill is exactly this: find what the failure follows — board, chip or software build — and hand over evidence, not opinions." `[숫자 채우기: 사례 1개 — §4.5 틀로]`
- "At Solidigm I brought up Cortex-R8/R82/M0+ cores and the PCIe/NVMe subsystem on FPGA prototypes before RTL freeze, so I'm used to the bottom-up order — clocks, resets, debug access, then SRAM code, then memory and interfaces — and to splitting a failure between our firmware, the IP configuration and the design." (→ S2, S3, S6, S9)
- "At SK hynix I automated chamber reliability testing for NAND and PCIe Gen3–5 interfaces, so for temperature-only failures my reflex is to turn them into a rate map over voltage and temperature with enough samples per point." (→ S7, S10, §4.3) `[숫자 채우기: 챔버 온도 범위, 동시 DUT 수]`
- Trace32 멀티코어 디버그 경험 → S2·S3·S10에서 "JTAG로 PC·코어 상태" 말할 때 구체 예시 `[채우기: 기억나는 halt 디버그 1건]`.

## 6. 실습 / 코드

### 6.1 검증된 것 — mock 보드 팜이 재현하는 시나리오

BV14의 [farm_orchestrator.py](onsitePrep/bv14/farm_orchestrator.py) (Python stdlib, `python3 -I`로 실행, 출력 전문 [run_log.txt](onsitePrep/bv14/outputs/run_log.txt))의 mock 인벤토리 8장 중 고장 모델이 이 노트의 시나리오에 대응한다 [확인됨: 실행 출력].

| mock 보드 | 고장 모델 | 시나리오 | 오케스트레이터가 실제로 낸 판정 / 힌트 (발췌) |
|---|---|---|---|
| B04 | ddr_march_c 간헐 실패 p=0.2 | S8·§4.3 | `K/P` (FLAKY, 25 °C) · "seen 1 time(s) -> too few events to correlate with temperature; loop it first" · 루프 500회 → `86 fail / 500 = 17.20%`, 95% 상한 0.2022 |
| B05, B08 | rev B ECO로 I2C 0x50 NACK | S11·S12 | `i2c_scan:0x0102` FAIL · "every rev B board, no other rev -> follows board revision: diff ECO/BOM A/B" |
| B06 | pcie_link에서 무응답 (rework: PERST# strap 저항 DNP) | S2·S9 계열 hang | `H/H` · "single board B06 (… rework=['R214 0R->DNP (PERST# strap)']) -> board-specific" + "hang -> JTAG halt at timeout …" |
| B07 | PMIC fault로 부팅 불가 (VDD_DDR UV) | S4 계열 | 헬스 체크 2회 실패 → `QUARANTINED`, 테스트 실패로 집계 안 함 (INFRA) |
| B08 | 85 °C에서만 DDR 실패 (SS corner) | S7·S8 | `P/F` · "single board B08 (rev B, SS corner, LOT18-W02 …)" + "only at 85 C -> temperature-correlated" · 루프 85 °C 50/50, 25 °C 0/50 |

- 이 표의 교훈 3개: ① 간헐 실패는 nightly 한 번으로 안 보인다 (seed 6개 중 4개에서 B04 전부 PASS) ② 같은 에러 코드(`0x0003`)라도 FLAKY(B04)와 결정적 실패(B08)는 다른 버그로 분리 ③ 부팅 불가 보드는 "테스트 실패"가 아니라 "인프라/보드 상태"로 격리해야 통계가 오염되지 않는다.

### 6.2 랩 레시피 `[미실행]` — 실보드가 생기면

```bash
# S4 재부팅 루프: reset reason 이력 (Linux, watchdog 드라이버가 지원할 때)
cat /sys/class/watchdog/watchdog0/bootstatus          # [미실행] 값 의미는 드라이버별
dmesg | grep -iE "reset|watchdog|brown|pmic"
# S11 I2C: 버스 스캔(read 방식) → 특정 레지스터 읽기
i2cdetect -l ; i2cdetect -y -r 3 ; i2cget -y 3 0x50 0x00
# S6/S7 DDR: 사용자 공간 memtest (부팅 후), U-Boot에선 mtest
memtester 256M 3
# 팜: 한 보드·한 테스트를 N회 루프해서 율 + 95% 상한 (BV14 코드, 이건 실제 실행됨)
python3 -I onsitePrep/bv14/farm_orchestrator.py --loop B04 ddr_march_c 500
```

## 7. 자주 틀리는 것

1. **증상 층에서 시작** — "Linux가 멈춘다"에서 커널 디버그부터 → 실제는 코어 rail droop. 항상 아래층 증거부터.
2. **rail-GND 저항을 절대값으로 판단** — 큰 SoC 코어 rail은 원래 낮다. golden board 대비 delta로.
3. **strap을 부팅 후 DMM으로 측정** — 샘플은 리셋 해제 순간뿐. 래치 레지스터 + 리셋 엣지 트리거 scope.
4. **여러 변수를 동시에 바꿈** — 보드·FW·온도를 한꺼번에 바꾸고 "고쳐졌다". 2×2 매트릭스, 하나씩.
5. **swap 후 되돌리지 않음** — 재장착(reseat) 자체가 고친 것일 수 있다. A→B→A.
6. **팜 인프라를 의심 목록에서 빼먹음** — 슬롯·케이블·프로브가 "보드 2장만 실패"의 흔한 원인. 가장 먼저, 가장 싸게 배제.
7. **"BOR이니까 HW 문제"** — 원인이 HW 증상이어도 수정은 SW(순차 enable, DVFS 순서)일 수 있다.
8. **"칩을 따라간다 = silicon 버그"** — corner 마진일 수 있다. spec 조건 내 실패인지, 여러 칩에서 재현되는지가 먼저.
9. **간헐 실패를 한 번의 green으로 종결** — rule of three: 1/200 문제는 ~600회 무실패가 필요 (LP14 §2.5).
10. **증거 없이 넘기기** — "보드 문제 같아요" 대신 §2.5 6칸 리포트 + 최소 재현 + "배제한 것".

## 8. 출처

1. NXP Community — S32K IIC Communication Error Recovery (UM10204 §3.1.16 "Bus clear" 안내: SDA stuck 시 9 클럭, SCL stuck 시 HW 리셋/전원 사이클) · https://community.nxp.com/t5/S32K/S32K-IIC-Communication-Error-Recovery/m-p/1619032
2. Linux kernel `struct i2c_bus_recovery_info` (man page, generic SCL/GPIO recovery) · https://manpages.debian.org/testing/linux-manual-4.8/struct_i2c_bus_recovery_info.9.en.html · 배경: Marcus Folkesson, "I2C Bus Recovery" · https://www.marcusfolkesson.se/blog/i2c-bus-recovery/
3. Embedded Artistry field manual — DVT / PVT 정의 · https://embeddedartistry.com/fieldmanual-terms/design-validation-test/
4. EmbeddedRelated — "Embedded Linux board farms 101: the requirements that actually matter" (콘솔·전원·복구 모드) · https://www.embeddedrelated.com/showarticle/1782.php
5. git-bisect 문서 (`git bisect run`) · https://github.com/gitster/git/blob/maint/Documentation/git-bisect.txt
6. Google Testing Blog — Flaky Tests at Google and How We Mitigate Them (2016) · https://testing.googleblog.com/2016/05/flaky-tests-at-google-and-how-we.html
7. ACDI — Troubleshooting electronic board power rail shorts with a thermal camera (전류 제한 ~0.5 A로 열점 찾기) · https://www.acdi.com/troubleshooting-electronic-board-power-rail-shorts-with-a-thermal-camera/ · Espressif — ESP DevKit short circuit analysis (MLCC ESD 파손·LDO) · https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/hardware-troubleshooting/esp-devkit-short-circuit-analysis.html
8. SEGGER Forum — J-Link not communicating with target (VTref 미연결 시 연결 안 함, 지원 1.2–5 V) · https://forum.segger.com/index.php/Thread/8401-SOLVED-J-link-not-communicating-with-target-device · Infineon Community — JTAG failure unexpected voltage (직렬 저항·부하로 레벨 저하) · https://community.infineon.com/t5/Wi-Fi-Combo/CYW43907-jtag-failure-unexpected-voltage/m-p/340929
9. LKML — dt-bindings zynqmp modepin 스레드 (UG1085 인용: PS_MODE "sampled on POR deassertion") · https://lkml.iu.edu/hypermail/linux/kernel/2108.2/03192.html · barebox 패치 (CRL_APB BOOT_MODE_USER = 0xFF5E0200, 하위 4비트) · https://lore.barebox.org/barebox/20210913121350.9307-4-michael.riesch@wolfvision.net/
10. NXP Community — Vybrid DDR3 write leveling (write leveling·read leveling·gate training 정의, 배선 길이 원인 사례) · https://community.nxp.com/t5/Vybrid-Processors/Vybrid-DDR3-write-leveling/m-p/589694/highlight/true
11. Microchip — Write Leveling Training Fail / DDR Controller Options Impacting Training (Vref CA, ODT, drive 조정) · https://onlinedocs.microchip.com/oxy/GUID-7F276F66-9418-456E-9FA3-8E7EE40C9E25-en-US-7/GUID-23B9898C-146F-47D5-94B3-55770A8F5AF0.html
12. TI E2E — K2H DDR3 leveling/gate training errors (작은 위반은 100% 실패를 만들지 않음, 레퍼런스 설정으로 재현 비교) · https://e2e.ti.com/support/dsp/c6000_multi-core_dsps/f/639/t/394547
13. 내부: [LP15 디버그 시나리오](2026-10-31_LP15_debug_scenarios.html), [LP14 검증 방법론 §2.4–§2.5](2026-10-31_LP14_validation_methodology.html), [BV00 트랙 §2](2026-11-01_BV00_board_validation_track.html)
