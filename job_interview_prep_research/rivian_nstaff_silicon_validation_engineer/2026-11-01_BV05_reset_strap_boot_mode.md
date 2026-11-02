# BV05 · 리셋 · strap 핀 · boot mode · reset reason · watchdog
<!-- theme: pastel -->

> **묶음**: B 전원·클럭·리셋 · **우선**: P0 ("Board keeps rebooting" 류 질문의 본진) · **선행 노트**: [BV03 전원 트리·sequencing](2026-11-01_BV03_power_tree_sequencing_pmic.html), [BV04 클럭·PLL](2026-11-01_BV04_clocks_osc_pll_refclk.html) · **다음 노트**: [BV06 첫 전원 인가 체크리스트](2026-11-01_BV06_first_power_on_checklist.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), 관련: [BV08 부트 흐름·테스트 FW](2026-11-01_BV08_boot_flow_bringup_firmware.html), [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html), [LP11 PCIe 리셋 종류](2026-10-31_LP11_reset_types.html), [LP16 Zynq bare-metal](2026-10-31_LP16_baremetal_vs_linux_zynq.html), [EL P1 보드 bring-up 순서](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html) · **코드**: [onsitePrep/bv05/reset_reason_bootcount.c](onsitePrep/bv05/reset_reason_bootcount.c) (macOS ASan/UBSan 빌드·실행, 출력 [reset_reason_bootcount.out](onsitePrep/bv05/reset_reason_bootcount.out))

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"The board keeps rebooting — debug it"**, "strap 핀이 뭐고 뭐가 잘못될 수 있나", "전원·클럭·리셋 순서는? 보드에서 누가 보장하나", "warm vs cold reset", "watchdog을 bring-up FW에서 어떻게 다루나", "reset 자체를 어떻게 검증하나".
- **외울 것 ① 순서**: **전원(모든 rail 안정 + PGOOD) → 클럭(osc 안정, 필요하면 PLL lock) → 리셋 해제** — 보드에서는 **reset supervisor / PMIC의 PGOOD→RESET 지연**이 보장하고, 칩 안에서는 **reset controller**가 블록별로 다시 순서를 매긴다 (clock enable → reset deassert).
- **외울 것 ② strap = 리셋이 풀리는 순간 한 번 찍는 사진**: 그 순간의 전압이 VIH/VIL 안에 있어야 한다. 실패 원인 3종 = **(a) 풀 저항이 너무 약함 + 누설/다른 칩이 그 선을 잡아당김, (b) 다른 칩이 리셋 중 그 핀을 driving, (c) IO rail이 아직 안 올라온 상태에서 샘플**. 확인 = 회로도가 아니라 **샘플된 값 레지스터**(ZynqMP `BOOT_MODE_POR`/`BOOT_MODE_USER`) [2][3].
- **외울 것 ③ reset reason 레지스터 = 재부팅 루프 디버그 1순위**. 대부분 **sticky + W1C(1을 써서 지움)**, POR만 지움 → **읽고, 로그 남기고, 지워라**. 안 지우면 비트가 쌓여 "POR|SW|WDT"처럼 보이고 순진한 디코더는 엉뚱한 원인을 보고한다 (이 노트 C 실습에서 재현). ZynqMP = `CRL_APB.RESET_REASON` @ `0xFF5E0220`, bit0 external POR … bit6 debug [1][3].
- **Watchdog**: 재부팅 루프의 1번 용의자. 독립 WDT는 시작하면 못 끈다 (ST IWDG) [12], window WDT는 **너무 일찍 kick해도 리셋** [13], 디버거로 halt하면 WDT도 멈추게(freeze) 설정해야 디버그 중 리셋이 안 난다 [12].
- **부트 카운터 패턴**: "noinit" RAM(또는 ZynqMP `PERS_GLOB_GEN_STORAGE`, POR에만 리셋 [6][7]) 에 {magic, ~magic, count, checksum} → **연속 WDT N회면 safe mode / golden image** — U-Boot `bootcount`/`bootlimit`/`altbootcmd`와 같은 아이디어 [20].
- **30초 영어 ("The board keeps rebooting")**: "First I find out *who* is resetting the chip. I read the reset-reason register on the next boot — before anything clears it — and log the last UART lines. If it says watchdog, the software is stuck or the watchdog isn't being serviced; I check with JTAG where the PC is just before the timeout, and whether the watchdog is frozen on debug halt. If it says brown-out or power-on, it's a power problem, so I put a scope on the core rail, PGOOD and the reset pin, triggered on reset assertion, and look for a droop under load. If the reason is the external reset pin, something on the board — a supervisor, a CPLD, a debugger — is pulling it. Reason register first, then scope; never guess."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 육상 경기 출발선

- **전원** = 선수들이 트랙에 입장해서 자리를 잡는 것. 다 입장하기 전에 출발 신호를 쏘면 사고 난다.
- **클럭** = 선수들의 심장 박동/발걸음 박자. 박자가 안정돼야 달릴 수 있다.
- **리셋** = 출발 심판. "제자리에(리셋 assert) … 준비 … **땅!**(리셋 deassert)". 총성이 울리기 전까지 아무도 움직이면 안 된다. 총성 = **리셋 해제**. 칩은 이 순간부터 BootROM 첫 명령을 실행한다.
- **Strap 핀** = 출발하는 **바로 그 순간** 선수가 보는 표지판 ("오늘은 A 코스 = QSPI flash로 부팅", "B 코스 = SD 카드"). 출발한 뒤에 표지판을 바꿔도 소용없다 (latched). 표지판이 흐릿하면(전압이 애매하면) 선수마다 다르게 읽는다 → **보드마다 다른 동작**.
- **Watchdog** = 감독관. "10초마다 손 흔들어(kick) — 안 흔들면 넘어진 걸로 보고 출발선으로 돌려보낸다(리셋)."
- **Reset reason 레지스터** = 블랙박스. "왜 다시 출발선에 섰나? 정전(POR)? 심판이 버튼 누름(PIN)? 감독관(WDT)? 선수가 스스로(SW)? 전압이 순간 떨어짐(brown-out)?"
- **Boot counter** = 출발선에 몇 번 섰는지 세는 칠판. 정전되면 지워지지만, 감독관이 돌려보낸 건 기억한다. "세 번 연속 넘어졌으면 오늘은 연습 코스(safe mode)로."

### 1.2 시간 순서 그림 — 전원 → 클럭 → 리셋 → strap 샘플 → BootROM

```text
 시간 ─────────────────────────────────────────────────────────────────────────────▶

 VIN (12V/5V)  ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 VDD_CORE      _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 VDD_IO (1.8V) ____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 PGOOD (all)   __________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 OSC (24MHz)   ______________~~~~~~∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿
                             (켜지는 중: 진폭·주파수 불안정)  (안정)
 POR_B (active low) _______________________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
                   ◀────── 리셋 유지 (supervisor 지연 tD) ──▶│
                                                            │ ① strap 핀 샘플 (latch)
                                                            │ ② 내부 reset controller가 블록별 해제
                                                            │ ③ BootROM 첫 명령 실행
 STRAP pin     ~~~~~(정의 안 됨)~~~~~~~~~/‾‾‾‾‾‾‾‾(풀업으로 1.8V 안정)‾‾‾‾‾‾‾‾\__ (부팅 후 GPIO/LED로 재사용 가능)
                                                            ▲ 이 순간 값만 중요
```

- 핵심: **POR_B가 올라가는 엣지**가 모든 것의 기준점. 그 전에 rail·클럭·strap 전압이 다 안정돼 있어야 한다.
- 그 엣지 직전에 rail이 잠깐 떨어지면(brown-out) supervisor가 다시 리셋을 잡는다 → 재부팅.

### 1.3 보드의 리셋 트리 — "누가 칩을 리셋할 수 있나"

```text
                 ┌──────────────────┐
  VDD rails ───▶ │ Reset supervisor │──┐  (전압 감시 + 지연 tD, open-drain)
                 │ / PMIC PGOOD→RST │  │
                 └──────────────────┘  │ wired-AND (open-drain끼리 묶음: 누구든 Low로 당기면 리셋)
  RESET 버튼 ─────────────────────────┤
  CPLD / BMC / 보드 관리 MCU ──────────┤
  JTAG 커넥터 nSRST / nTRST ───────────┤
  외부 watchdog IC ───────────────────┘
                                       ▼
                    ┌─────────────────────────────────────────────────────────┐
                    │ SoC                                                     │
                    │  POR_B ─▶ [ 칩 리셋 컨트롤러 ] ◀─ 내부 WDT 타임아웃         │
                    │  SRST_B ─▶      │      ▲  ◀─ SW reset (레지스터 쓰기,       │
                    │                 │      │       Cortex-M AIRCR.SYSRESETREQ) │
                    │                 │      └─── 디버거 reset 요청             │
                    │                 ▼                                       │
                    │   RESET_REASON 레지스터 (원인 기록, sticky)               │
                    │   블록별 reset 라인: CPU0 CPU1 DDRC PCIe USB ETH I2C …     │
                    │   (각 블록 = clock enable + reset deassert 둘 다 필요)    │
                    └─────────────────────────────────────────────────────────┘
```

### 1.4 strap 핀 회로 — 저항 하나가 부팅 장치를 정한다

```text
   VDD_IO (1.8V)                         규칙: 리셋 해제 순간 핀 전압 V가
       │                                       V ≥ VIH (예 0.65×1.8 = 1.17V) → '1'
      [R_pu 4.7kΩ]  ← 설계 의도 '1'            V ≤ VIL (예 0.35×1.8 = 0.63V) → '0'
       │                                       그 사이 → 정의 안 됨 (칩마다·온도마다 다름)
  ─────●────────────────▶ SoC MODE1 입력
       │
       └──────────▶ 다른 칩의 핀 (LED, CPLD 출력, PHY 입력…)
                      · 입력이면 누설전류 I_leak (μA)  →  V = 1.8 − I_leak × R_pu
                      · 리셋 중 출력이면 R_pu와 싸움   →  V = 분압
```

- 4.7 kΩ × 10 μA = 0.047 V 떨어짐 → 안전. **100 kΩ** × 10 μA = **1.0 V** 떨어짐 → 0.8 V = VIL과 VIH 사이 → **보드마다 다르게 부팅** (이 노트 C 실습 [C] 케이스에서 실제 계산).

### 1.5 BootROM의 판단 흐름 (일반형)

```text
  리셋 해제
     │
     ▼
  strap/OTP 읽기 ──▶ boot mode = ? ──┬─ QSPI/eMMC/SD/NAND ─▶ 이미지 헤더 확인 ─┬─ OK ─▶ (인증/복호화) ─▶ SRAM에 FSBL 로드 ─▶ 점프
     │                              │                                       └─ 실패 ─▶ 다음 오프셋/다음 이미지 (fallback, multiboot)
     │                              │                                                    └─ 전부 실패 ─▶ 에러 코드 남기고 멈춤 / 다운로드 모드
     │                              ├─ USB/UART 다운로드 ─▶ PC에서 이미지 받기 (벽돌 복구용)
     │                              └─ JTAG ─▶ 아무것도 안 하고 디버거 기다림 (bring-up 첫날 모드)
     ▼
  보안 퓨즈(OTP)에 따라 JTAG 잠금 / secure boot 강제 여부 결정
```

### 1.6 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| Reset | (리셋) | 칩 상태를 정해진 초기값으로 되돌리고 처음부터 시작시키는 신호 | 모든 bring-up의 출발점 |
| Assert / Deassert | (활성 / 해제) | 리셋 신호를 "건다" / "푼다". active-low(`_B`, `n` 접두)면 Low가 assert | `POR_B` = Low일 때 리셋 중 |
| POR | Power-On Reset | 전원이 처음 들어올 때(또는 다시 들어올 때) 걸리는 가장 강한 리셋 | 모든 sticky 레지스터·SRAM이 초기화 |
| BOR | Brown-Out Reset | 전압이 순간 기준 아래로 떨어져서 걸린 리셋 | "가끔 재부팅" = 전원 droop 의심 |
| Cold reset | (콜드 리셋) | 전원을 끊었다 켜는 수준의 리셋 — 거의 모든 것이 초기화 | SRAM 내용·persistent reg 사라짐 |
| Warm reset | (웜 리셋) | 전원은 유지한 채 로직만 리셋 — 일부 상태가 살아남음 | 부트 카운터·크래시 로그를 남길 수 있음 |
| System reset vs core reset | (시스템 / 코어) | 칩 전체 vs CPU 코어 하나만 | 코어만 리셋하면 주변 블록은 이전 상태 그대로 |
| SRST | System ReSeT (pin) | 외부에서 시스템 리셋을 거는 핀 (ZynqMP `PS_SRST_B`) | 버튼·CPLD·디버거가 이걸 당김 |
| Reset supervisor | (리셋 감시 IC) | rail 전압을 보고 "기준 이상 + tD 지남"일 때만 리셋을 풀어 주는 작은 칩 | 순서를 **보드에서** 보장하는 주체 |
| tD | reset time delay | 전압이 정상 된 후 리셋을 풀기까지 기다리는 시간 (캡 하나로 설정하는 부품도 있음) | 클럭 안정 시간 확보 |
| Hysteresis | (히스테리시스) | 켜질 때·꺼질 때 기준 전압을 살짝 다르게 해서 경계에서 덜덜 떨지 않게 | 리셋 채터링 방지 |
| Open-drain | (오픈 드레인) | Low로만 당길 수 있고 High는 풀업 저항이 만드는 출력 | 여러 리셋원을 한 선에 안전하게 묶음 |
| Reset controller | (리셋 컨트롤러) | 칩 안에서 블록별 리셋 라인을 레지스터로 켜고 끄는 블록 | "레지스터가 0만 읽힘" = 블록이 아직 리셋 중 |
| Reset synchronizer | (리셋 동기화기) | 리셋을 비동기로 걸고 **클럭에 맞춰** 푸는 회로 (async assert, sync deassert) | 클럭 없으면 리셋이 안 풀리는 블록이 있음 |
| Strap pin | (부트스트랩 핀) | 리셋 해제 순간의 High/Low로 칩 설정을 정하는 핀 | boot mode·PHY 주소·IO 전압 결정 |
| Latch | (래치) | 그 순간 값을 붙잡아 저장 | 리셋 후 핀을 바꿔도 설정은 안 바뀜 |
| VIH / VIL | Input High / Low voltage | 입력이 '1'/'0'으로 **보장**되는 전압 경계 | 그 사이 값은 칩이 아무렇게나 읽음 |
| Leakage | (누설전류) | 꺼져 있어도 핀으로 새는 작은 전류 (μA) | 약한 풀 저항을 무력화 |
| Boot mode | (부트 모드) | BootROM이 어떤 장치에서 첫 이미지를 찾을지 | 틀리면 엉뚱한 곳에서 부팅 / 멈춤 |
| BootROM | (부트 롬) | 칩 안 마스크 ROM에 박힌 첫 코드 — 수정 불가 | 첫 이미지 로드·인증·fallback 담당 |
| Fallback / multiboot | (대체 부팅) | 이미지가 깨졌으면 다음 위치의 이미지(golden)로 | 업데이트 실패해도 벽돌이 안 됨 |
| Reset reason / reset cause | (리셋 원인 레지스터) | 마지막 리셋이 왜 일어났는지 비트로 기록 | 재부팅 루프 디버그 1순위 |
| W1C | Write-1-to-Clear | 그 비트에 1을 쓰면 0이 되는 비트 (0을 쓰면 무시) | 실수로 `reg = reg`로 전부 지우기도 |
| Sticky | (끈적이 비트) | 원인이 사라져도 SW가 지우거나 POR가 올 때까지 유지 | 지우지 않으면 쌓임 |
| WDT | WatchDog Timer | 일정 시간 안에 SW가 "살아 있다"를 안 보내면 리셋 | 행(hang) 자동 복구 |
| Kick / feed / refresh | (워치독 주기) | WDT 카운터를 다시 채우는 쓰기 | 어디서 kick하느냐가 설계의 핵심 |
| Window WDT | (윈도우 워치독) | 너무 늦어도, **너무 일찍** kick해도 리셋 | 폭주 루프가 kick만 반복하는 것도 잡음 |
| Debug freeze | (디버그 정지) | CPU를 디버거로 멈추면 WDT도 같이 멈추게 하는 설정 | 안 하면 breakpoint마다 리셋 |
| noinit section | (초기화 안 하는 영역) | startup 코드가 0으로 지우지 않는 RAM 구역 | warm reset 넘어 정보 전달 |
| Golden image | (골든 이미지) | 절대 안 지우는 안전한 예비 이미지 | 복구 경로 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 리셋 종류 — 무엇이 걸고, 무엇이 지워지고, 무엇이 남나

| 종류 | 누가 거나 | 범위 (일반적) | 살아남는 것 (일반적) | ZynqMP RESET_REASON 비트 [1] |
|---|---|---|---|---|
| POR (외부) | 전원 투입, supervisor가 POR_B 당김 | 칩 전체 | 없음 (SRAM 내용 쓰레기) | bit0 `external_por` (PS_POR_B 핀) |
| 내부 POR | 칩 내부 치명적 에러 → POR로 승격 | 칩 전체 | 거의 없음 | bit1 `internal_por` |
| 시스템 리셋 (핀) | 버튼, CPLD, 디버거 nSRST | 시스템 (always-on 일부 제외) | persistent reg, SRAM 내용 [추정: 칩별] | bit4 `srst` (PS_SRST_B) |
| SW 시스템 리셋 | FW가 리셋 레지스터 쓰기 | 시스템 | 위와 같음 | bit5 `soft` (RESET_CTRL 쓰기) |
| PMU/관리 블록 리셋 | 에러 처리 블록(PMU)이 시스템 에러로 리셋 | 시스템 | 위와 같음 | bit2 `pmu_sys_reset` |
| 부분 리셋 | PS-only (FPGA 영역 유지) | PS만 | PL 상태 | bit3 `psonly_reset_req` |
| 디버거 리셋 | Trace32/xsdb의 system reset | 시스템 (debug 로직 제외 흔함) | debug 설정 | bit6 `debug_sys` |
| WDT 리셋 | WDT 타임아웃 | 설정에 따라 코어/시스템/POR | 설정 따라 | ZynqMP는 PMU 에러 처리 경로로 들어감 [추정: ERROR_SRST/POR 설정에 따름] |
| BOR | rail이 기준 아래로 | 칩 전체 (POR와 동급인 칩 많음) | 없음 | (ZynqMP는 supervisor가 POR_B로 표현) |
| 코어 리셋 | 디버거/SW가 코어 하나만 | 코어 | 주변 블록 상태 전부 | — |

- **Cold vs warm** 은 칩 용어가 제각각이다. 안전한 정의: **cold = 전원이 실제로 빠짐(모든 것 초기화)**, **warm = 전원은 유지하고 로직만 리셋**. 면접에서는 "which state survives which reset — I'd build a table per chip from the TRM and then *verify it* on silicon"이라고 답하는 게 안전.
- **Cortex-M SW 리셋 (AIRCR)**: SCB AIRCR = `0xE000ED0C` (SCB base `0xE000ED00` + 0x0C). 쓰기 시 `VECTKEY` 필드 [31:16]에 `0x05FA`를 넣어야 하고, `SYSRESETREQ` = bit 2 [11]. CMSIS `NVIC_SystemReset()`은 `DSB` → `AIRCR = (0x5FA<<16) | (PRIGROUP 유지) | SYSRESETREQ` → `DSB` → 무한 루프 [11]. 주의: SYSRESETREQ는 코어가 **"리셋해 주세요" 요청**을 칩 리셋 컨트롤러에 보내는 것뿐이고, **실제로 무엇이 리셋되는지는 SoC 통합 설계가 정한다** (디버그 로직은 보통 제외). `VECTRESET`(bit 0)은 코어만 리셋하는 디버거용 비트 [11] — 실 FW에서는 쓰지 않는 게 원칙 [추정: v7-M 권고, v8-M에선 없음].

```c
/* Cortex-M: CMSIS NVIC_SystemReset()과 동등 (core_cm3.h 기준) [11] */
#define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0Cu)
static inline __attribute__((noreturn)) void sys_reset(void)
{
    __asm volatile("dsb" ::: "memory");          /* 남은 쓰기(로그 등) 먼저 끝내기 */
    SCB_AIRCR = (0x5FAu << 16)                   /* VECTKEY: 없으면 쓰기 무시 */
              | (SCB_AIRCR & (7u << 8))          /* PRIGROUP 유지 */
              | (1u << 2);                       /* SYSRESETREQ */
    __asm volatile("dsb" ::: "memory");
    for (;;) { }                                 /* 리셋될 때까지 대기 */
}
```

### 2.2 Reset supervisor · RC 지연 — 보드에서 "순서"를 보장하는 부품

- **Supervisor(voltage supervisor, reset IC)** 동작 [16][17]:
  - 입력 rail(VDD 또는 SENSE 핀)을 기준 `VIT-`와 비교 → 아래면 RESET assert.
  - 위로 올라가면(**VIT- + VHYS** 이상) **tD 동안 더 기다렸다가** RESET 해제. TPS3840 같은 부품은 **CT 핀 캡**으로 tD를 정한다 [17].
  - **MR(manual reset)** 입력: 버튼·CPLD를 연결하는 입력. MR이 풀려 있고 전압이 기준 이상이고 tD가 지나야 리셋 해제 [17].
  - 출력: **open-drain**(여러 리셋원 wired-AND 가능, 풀업 필요) 또는 push-pull.
  - Detector vs supervisor: detector는 지연이 거의 없고 hysteresis만, supervisor는 지연 tD까지 [16].
- **RC 지연(저항+캡)**만으로 리셋을 만드는 보드: 싸지만 (a) 전원이 천천히 오르면 리셋이 너무 일찍 풀림, (b) **brown-out 감지 못 함**(전압이 살짝 떨어져도 캡이 리셋을 다시 걸지 않음), (c) 느린 엣지 → 입력에 **Schmitt trigger** 필요. → 검증 보드에서 "가끔 이상하게 부팅" 원인.
- **PMIC 내장 시퀀서**: 마지막 rail PGOOD 후 **nRESET/PORn 출력을 지연 해제**하는 기능이 흔함 → BV03의 sequencing 표 마지막 줄이 "reset release".
- **브라운아웃 판단 기준**: supervisor 임계값이 rail 최소 스펙보다 **위**에 있어야 의미가 있다 (칩이 오동작하기 전에 리셋). 임계값 + 정확도(±%) + 히스테리시스 + **glitch immunity**(짧은 스파이크 무시 시간)를 같이 본다 [16].

### 2.3 전원 → 클럭 → 리셋 순서의 정확한 의미

```text
  [rail 전부 in-spec] ──(t_a)──▶ [osc 안정 / PLL ref OK] ──(t_b)──▶ POR_B deassert ──▶ strap latch ──▶ BootROM
        PGOOD                       osc start-up time                    supervisor tD 또는 PMIC 지연
```

- **칩 데이터시트가 요구하는 것** (일반형): ① 모든 필수 rail이 허용 범위 안 ② ref clock이 안정 ③ 그 후 최소 **t_POR_hold** 동안 리셋 유지. 수치는 칩마다 → 데이터시트의 "power-on sequence / reset timing" 표에서 가져와 **측정 체크리스트의 통과 기준**으로 만든다.
- **칩 안에서 다시 한 번 순서**: 리셋이 풀린 뒤 칩 리셋 컨트롤러가 PLL lock을 기다렸다가 블록별 리셋을 해제한다. FW가 블록을 켤 때 순서 = **(1) 전원 도메인 on → (2) clock enable → (3) 잠깐 대기 → (4) reset deassert → (5) 레지스터 접근**. 많은 IP가 **리셋 중에도 클럭이 돌아야** 리셋이 내부까지 전파된다(reset synchronizer: async assert, sync deassert) — 클럭 없이 리셋만 풀면 "반쯤 리셋된" 상태가 된다 [추정: IP별, 일반 설계 관행].
- ZynqMP 예: 블록 리셋은 `CRL_APB`(저전력 도메인, LPD)와 `CRF_APB`(full-power 도메인, FPD)의 `RST_*` 레지스터(예: `RST_LPD_TOP`, CRL_APB+0x23C)로 제어된다 [3]. 보통 FSBL의 `psu_init`이 대부분 해제해 둔다 → bare-metal 테스트에서 "레지스터가 0만 읽힘/버스 hang"이면 해당 블록의 **clock + reset 레지스터부터** 본다.

### 2.4 Strap 핀 — 샘플링 시점, 풀 저항 값, 읽어 보는 레지스터

- **언제 샘플되나**: 대부분 **리셋 해제 엣지**(또는 그 직전 몇 클럭). 일부는 **POR에서만** 샘플하고 하드웨어 리셋에선 다시 안 읽는다 — 예: TI DP83826 PHY의 LED1/CLKOUT strap은 POR에서만 latch, HW reset에선 re-latch 안 됨 [19]. → "버튼 리셋 후에도 strap이 반영 안 됨"의 이유.
- **흔한 strap 기능**: boot mode, boot 장치 폭/전압, PLL bypass, JTAG 체인 선택/디버그 enable, Ethernet **PHY 주소**·모드(RGMII/SGMII), IO bank 전압, DDR 타입, 보안 모드(보통은 OTP 퓨즈로).
- **풀 저항 값 고르기** (정량):

| 요인 | 너무 약한 풀 (예 100 kΩ) | 너무 강한 풀 (예 330 Ω) |
|---|---|---|
| 누설 전류 | `I_leak × R` 전압 강하 → VIL/VIH 사이로 | 문제 없음 |
| 다른 칩이 같은 선을 리셋 중 driving | 다른 칩이 이김 → 값 뒤집힘 | strap이 이김, 그러나 다른 칩 출력에 과전류 |
| 핀이 부팅 후 출력/LED로 쓰임 | OK | 출력 드라이버 부하·전력 낭비, LED 밝기 영향 |
| 캡 부하(긴 선·프로브)와 RC 시간 | τ = R·C 커서 샘플 전에 안 차오를 수 있음 [18] | 빨리 안정 |
| 칩 내부 풀업/풀다운 | 내부 풀(수십 kΩ)과 분압 → 값 애매 [18] | 외부가 압도 |

- **멀티레벨 strap**: 핀 하나로 2비트 이상 — 저항 분압으로 여러 전압 레벨을 만들고 칩 안 ADC/비교기로 읽음 (TI PHY 4-level strap). 저항 공차·내부 풀까지 넣어 **최악 조건에서 레벨 구간 안**인지 계산해야 한다 [18].
- **읽어 보는 레지스터 = 진실**: 회로도 ≠ 실제. 반드시 **칩이 샘플한 값**을 읽는다.
  - ZynqMP `CRL_APB.BOOT_MODE_POR` — POR 때 캡처된 boot mode 핀 값 [2].
  - ZynqMP `CRL_APB.BOOT_MODE_USER` @ `0xFF5E0200`: `boot_mode`[3:0] (RO, 실제 사용될 모드), `use_alt` bit 8, `alt_boot_mode` [15:12] — SW/JTAG로 다음 리셋의 boot mode를 바꿀 수 있음, POR에서만 리셋 [2][3].
  - PHY는 MDIO로 strap 상태 레지스터를 제공하는 경우 많음 (→ BV10).

### 2.5 Boot mode · BootROM · fallback (ZynqMP를 구체 예로)

- **ZynqMP boot mode 값** (`PS_MODE[3:0]` 핀; U-Boot `arch/arm/mach-zynqmp/include/mach/hardware.h` 정의, 원 출처는 UG1085 Table 11-1 [3][23]):

| MODE[3:0] | 부트 장치 | 비고 |
|---|---|---|
| 0x0 | JTAG | 디버거 대기 — **bring-up 첫날 모드**, non-secure only [8] |
| 0x1 | QSPI24 | 24-bit 주소 QSPI flash |
| 0x2 | QSPI32 | 32-bit 주소 QSPI flash (16 MB 초과) |
| 0x3 | SD0 | SD 컨트롤러 0 |
| 0x4 | NAND | |
| 0x5 | SD1 | SD 컨트롤러 1 |
| 0x6 | eMMC | (eMMC18) |
| 0x7 | USB0 | USB 다운로드 모드 (PMU FW는 FSBL이 로드해야 함) |
| 0xE | SD1-LS | SD1 + level shifter |
| 0x8 / 0x9 | PJTAG0 / PJTAG1 | MIO 핀을 통한 JTAG [23] |

- **ZynqMP 부트 순서** [8]: POR → **PMU**가 자기 ROM 실행(pre-boot: 전원·리셋 정리) → **CSU**(Configuration Security Unit) 리셋 해제 → **CSU BootROM**이 boot mode 레지스터 읽음, OCM(on-chip memory) 초기화, **boot header** 읽음(보안 모드, FSBL을 어느 CPU — A53 or R5 — 가 돌릴지) → 인증/복호화 후 **FSBL을 OCM에 로드** → 점프. PMU FW는 BootROM 또는 FSBL이 로드 [8]. (FSBL 이후 흐름 → [BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html))
- **Fallback / MultiBoot** [9]: BootROM은 offset 0의 이미지를 먼저 보고, 헤더 식별값이 없거나 인증/복호화 실패면 **MultiBoot 오프셋을 1 증가 → 다음 32 KB 경계**를 보는 식으로 search limit까지 반복("golden image search"). FW가 일부러 다른 이미지로 부팅하려면 **CSU_MULTI_BOOT** 값을 바꾸고 soft reset [9].
- **함정**: `BOOT_MODE_USER.use_alt`는 **POR에서만 지워지고 SW reset에서는 유지** → SW reset 후 BootROM이 다른 장치를 볼 수 있다 (U-Boot 커밋 메시지) [24]. "버튼 리셋은 되는데 reboot 명령 후엔 이상" 류 증상.
- **복구용 다운로드 모드**(일반): USB/UART 다운로드 모드가 BootROM에 있는 칩이 많다 — flash가 깨져도 PC에서 이미지를 밀어 넣어 살림. 검증 보드라면 **boot mode 딥스위치/점퍼**로 언제든 JTAG/USB 모드로 바꿀 수 있게 회로도 리뷰에서 요구(→ BV02).

### 2.6 Reset reason 레지스터 — 정확한 레이아웃과 읽는 법

**ZynqMP `CRL_APB.RESET_REASON`** @ `0xFF5E0220`, POR에서만 리셋, reset 값 0x1, 상태 비트는 **W1C** [1][3]:

```text
  bit:  15      7   6          5      4      3                2               1              0
       ┌────────┬──────────┬──────┬──────┬────────────────┬───────────────┬──────────────┬──────────────┐
       │reserved│debug_sys │ soft │ srst │psonly_reset_req│ pmu_sys_reset │ internal_por │ external_por │
       └────────┴──────────┴──────┴──────┴────────────────┴───────────────┴──────────────┴──────────────┘
                 디버거      SW      PS_SRST_B  PS-only 리셋     PMU가 시스템     에러→내부 POR    PS_POR_B 핀
                 시스템리셋  리셋    핀                          에러로 리셋
```

- U-Boot(ZynqMP)는 부팅 때 이 레지스터를 읽어 `Reset reason:\t<NAME>` 을 출력하고 env `reset_reason`에 넣는다. 검사 순서 = DEBUG → SOFT → SRST → PS-ONLY → PMU → INTERNAL → EXTERNAL, **첫 번째로 매칭된 하나만** 출력 [4].
- U-Boot는 예전엔 이 레지스터를 지웠지만, PMU FW가 보호하고 있어 APU에서의 쓰기가 거부되므로 **지우지 않도록 바뀜** [5]. → APU의 Linux/U-Boot에서 보면 **POR 이후 누적된 비트**일 수 있다는 뜻. 해석 시 주의.
- **MCU 예 (STM32L4 `RCC_CSR`)** [14][15]: bit 31 `LPWRRSTF`, 30 `WWDGRSTF`, 29 `IWDGRSTF`, 28 `SFTRSTF`, 27 `BORRSTF`, 26 `PINRSTF`, 25 `OBLRSTF`, 24 `FWRSTF`; `RMVF`(bit 23)에 1을 써서 전부 지움. 다른 패밀리는 비트 위치가 다르다(F1은 RMVF bit 24). STM32는 내부 리셋원이 **NRST 핀을 같이 Low로 당기므로 PIN 플래그가 거의 항상 같이 선다** → **PIN은 마지막에 검사** [15].
- **올바른 처리 순서 (FW 첫 몇 줄)**:
  1. 리셋 원인 레지스터를 **가장 먼저** 읽는다 (다른 코드가 지우기 전에, 가능하면 startup 어셈블리 직후).
  2. noinit/persistent 영역에 저장 + UART에 출력 (`RST cause=WDT raw=0x08 boot#=4`).
  3. **W1C로 지운다** (읽은 값 그대로 써서 해당 비트만 지움).
  4. 여러 비트면 "우선순위 해석" (예: BOR/POR > WDT > SW > PIN), 그리고 raw 값도 같이 기록.

### 2.7 리셋을 넘어 정보를 남기는 곳 — noinit · persistent register · boot counter

| 저장소 | 살아남는 리셋 | 지워지는 리셋 | 예 |
|---|---|---|---|
| noinit SRAM 영역 | warm(시스템/SW/WDT) [추정: 칩별, BootROM이 덮어쓰는 영역 제외] | POR/BOR (전원 빠짐) | 링커 `.noinit (NOLOAD)` 섹션 [21] |
| 칩 persistent 레지스터 | 시스템 리셋 | POR | ZynqMP `PERS_GLOB_GEN_STORAGE0..7` (PMU_GLOBAL `0xFFD80050`–`0x6C`; 4–7은 FSBL 등 Xilinx SW가 사용) [6][7] |
| 비영속 general storage | 없음 (시스템 리셋에도 지워짐) | 시스템/POR | ZynqMP `GLOBAL_GEN_STORAGE0..6` [7] |
| RTC backup 레지스터 | POR까지 (배터리 있으면 그 이상) | 배터리 제거 | MCU RTC 도메인 |
| flash/eMMC 파일 | 전부 | — (쓰기 수명 주의) | U-Boot `CONFIG_BOOTCOUNT_FS` [20] |

- Linux에서 ZynqMP persistent 레지스터: `/sys/devices/platform/firmware:zynqmp-firmware/pggs0`..`pggs3` (읽기/쓰기), 비영속은 `ggs0`..`ggs3` [7].
- **Boot counter + 루프 탈출 패턴** (U-Boot `bootcount`: POR 후 1, 리부팅마다 +1, `bootlimit` 초과 시 `bootcmd` 대신 `altbootcmd`; **부팅 성공 후 앱이 0으로 리셋**해야 함 [20]):

```text
  boot ──▶ read cause ──▶ noinit rec valid? (magic, ~magic, checksum)
                │                 │ no / POR ─▶ cold init (count=0, streak=0)
                │                 ▼ yes
                └────▶ count++ ; streak = (cause==WDT) ? streak+1 : 0
                                  │
                         streak ≥ N ? ──yes──▶ SAFE MODE: golden image / 앱 시작 안 함 / 진단 덤프
                                  │ no
                                  ▼
                         정상 부팅 ──(60 s 정상 or self-test PASS)──▶ "healthy" → streak = 0
```

- 왜 **magic + ~magic + checksum**? POR 후 SRAM은 무작위 값이라 우연히 magic 하나와 같을 수 있다 → 3중 확인. 그리고 POR/BOR 원인이면 레코드가 유효해 보여도 cold로 취급.

### 2.8 Watchdog — 종류, 설계, bring-up 때 다루는 법

| 종류 | 특징 | 예 / 출처 |
|---|---|---|
| 독립(independent) WDT | 자체 저속 클럭(LSI)으로 돌아 메인 클럭이 죽어도 동작, **시작하면 SW로 못 끔**, HW 옵션(옵션 바이트)으로 리셋 직후 자동 시작 가능 | STM32 IWDG: kick = `IWDG_KR`에 `0xAAAA`; timeout ~125 μs–32.7 s @32 kHz [12] |
| Window WDT | 카운터가 window 값보다 클 때(너무 일찍) refresh해도 리셋, 0x40→0x3F로 내려가면 리셋, 0x40에서 **EWI**(early wakeup interrupt)로 마지막 로그 기회 | STM32 WWDG [13] |
| 외부 WDT IC | supervisor에 WDI 입력 — 일정 주기 토글 안 하면 RESET | 칩 전체가 멈춰도 동작, 기능 안전 설계에서 선호 [추정] |
| SoC 시스템 WDT | 타임아웃 → 인터럽트 → 다음 타임아웃에 리셋 (2단계) 흔함 | ZynqMP SWDT = Cadence WDT, Linux `cdns,wdt-r1p2` 드라이버 |

- **Debug freeze**: STM32는 `DBG_IWDG_STOP` 비트로 코어 halt 시 IWDG 정지 여부 결정 [12]. 다른 칩도 "debug halt 시 WDT pause" 비트가 있는 경우가 많다 → **bring-up 첫날 디버거 붙이기 전에 확인**. 안 하면 breakpoint에서 멈출 때마다 리셋.
- **Kick 위치**: 타이머 ISR에서 kick하면 메인 루프가 죽어도 ISR은 살아서 kick → WDT가 무의미. **메인 루프에서 모든 태스크의 "살아 있음" 플래그를 확인한 뒤에만** kick (태스크별 체크인).
- **Bring-up FW 정책**: (a) 초기 bring-up: WDT를 끄거나(가능하면) 아주 길게, debug freeze ON; (b) 테스트 러너: **테스트별 타임아웃**은 SW 타이머로, **최후의 보루**로 WDT → 리셋 후 reason=WDT + noinit에 "어느 테스트 중이었나" 기록 → 다음 부팅에서 그 테스트를 FAIL로 보고하고 다음 테스트부터 재개 (→ [BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)).
- Linux: `/sys/class/watchdog/watchdog0/bootstatus` = `WDIOC_GETBOOTSTATUS`와 같음 [22]; 플래그 중 `WDIOF_CARDRESET`(0x0020) = "이 WDT가 이전에 CPU를 리셋했음" [22] (드라이버가 지원할 때만 의미 있음).

### 2.9 리셋 자체를 검증하기 (silicon validation 관점)

- **리셋 매트릭스**: (리셋 종류 × 블록) 표 — 각 리셋 후 (1) 각 블록 레지스터가 **reset 값**으로 돌아왔나 (→ BV08 reset-value 테스트), (2) "살아남아야 할 것"(persistent reg, retention SRAM, debug 설정)은 살아남았나, (3) reset reason 비트가 정확한가.
- **트래픽 중 리셋**: DMA·PCIe·DDR 트래픽 도중 warm reset → 버스 hang 없이 복귀하나, 블록이 반쯤 리셋된 상태로 남지 않나.
- **반복 스트레스**: 전원 사이클 1만 회(프로그래머블 PSU + 릴레이), warm reset 10만 회 → 매회 boot 시간·reason·strap 값 기록 → 분포의 꼬리(가끔 실패) 찾기.
- **전원 코너**: 느린 ramp, 빠른 ramp, ramp 중 멈칫(plateau), 짧은 dropout(수 ms) → 리셋/strap이 항상 같은 결과인가 (→ BV12 PVT).

## 3. 보드에서 볼 것 · 잴 것

### 3.1 Scope 셋업 — "리셋이 언제, 어떤 조건에서 풀렸나"

| 채널 | 프로브 위치 | 보는 것 |
|---|---|---|
| CH1 | 마지막에 올라오는 rail (예 VDD_IO 1.8 V) 테스트 포인트 | 90 % 도달 시점, 이후 droop |
| CH2 | PGOOD (PMIC/레귤레이터) | rail in-spec 신호 시점 |
| CH3 | POR_B (SoC 핀 근처 TP) | 해제 시점, 펄스 폭, glitch |
| CH4 | 기준 osc 출력(버퍼된 TP 또는 clock-out 핀) 또는 strap 핀 | 클럭 시작 / strap 전압 |

- **트리거**: CH3 rising edge(리셋 해제). 재부팅 루프 추적이면 **CH3 falling edge + normal(반복) 모드** 또는 single + 긴 타임베이스.
- **측정**: `t(CH1 90% → CH3 rise)` ≥ 데이터시트 최소 hold, `t(osc 안정 → CH3 rise)` 확인, CH3 rise 순간 CH4(strap) 전압이 VIH/VIL 구간 밖인지.
- **Glitch 찾기**: pulse-width 트리거("POR_B Low 폭 < 1 μs") 또는 runt 트리거 — 리셋 라인 노이즈로 가끔 재부팅.
- **정지 상태 측정**: 리셋 버튼을 누른 채(또는 supervisor MR을 Low로 잡은 채) **DMM으로 strap 핀 전압**을 잰다 → 리셋 중 실제 값. 부팅 후 재면 핀이 이미 GPIO/LED로 바뀌어 의미 없다.

### 3.2 JTAG / 레지스터로 읽을 것 (ZynqMP 예)

```bash
# xsdb (Vivado/Vitis) — [예시 출력]은 보드가 없으므로 형태만
xsdb% connect
xsdb% targets -set -filter {name =~ "PSU"}
xsdb% mrd 0xFF5E0220          ;# CRL_APB.RESET_REASON
FF5E0220:   00000001          ;# [예시 출력] bit0 external_por = 정상 전원 투입
xsdb% mrd 0xFF5E0200          ;# CRL_APB.BOOT_MODE_USER
FF5E0200:   00000002          ;# [예시 출력] boot_mode[3:0]=0x2 → QSPI32 로 샘플됨
# 다음 부팅을 SD0(0x3)로 강제 (kplabs 가이드의 rwr 순서) [10]
xsdb% rwr crl_apb boot_mode_user alt_boot_mode 0x3
xsdb% rwr crl_apb boot_mode_user use_alt 1
xsdb% rst -system
```

- 해석: `0xFF5E0200`의 `boot_mode`가 회로도 의도와 다르면 → **strap 하드웨어 문제** (3.1로 전압 측정). 같으면 → boot 장치/이미지 문제로 넘어간다.

### 3.3 부트 로그로 보는 것

```text
[예시 출력]  (U-Boot ZynqMP; "Reset reason:" 줄 형식은 U-Boot 소스 기준 [4])
U-Boot 2024.01 (...)
Model: ZynqMP ZCU102 RevB
Board: Xilinx ZynqMP
DRAM:  4 GiB
...
Bootmode: QSPI_MODE
Reset reason:	SOFT
```

- 1행~4행: U-Boot까지 왔다 = BootROM → FSBL → (PMU FW) → TF-A → U-Boot 경로 OK.
- `Bootmode:` = 레지스터에서 읽은 모드 → strap 확인과 같은 의미.
- `Reset reason: SOFT` = 지난번 리셋은 SW 리셋 (`reboot`). U-Boot는 **첫 매칭 하나만** 출력하고 레지스터를 안 지우므로 [4][5], 정확히 보려면 raw 값(`md 0xff5e0220 1`)도 본다.

```bash
# Linux 쪽 [예시 출력]
$ cat /sys/class/watchdog/watchdog0/bootstatus
32            # = 0x20 = WDIOF_CARDRESET: 이전 리셋을 WDT가 일으킴 (드라이버 지원 시) [22]
$ cat /sys/devices/platform/firmware:zynqmp-firmware/pggs0
0x00000004    # 우리 FW가 남긴 boot counter (POR에만 지워짐) [7]
$ dmesg | grep -i -E "reset|watchdog"
```

### 3.4 "계속 재부팅" 디버그 흐름표

| 단계 | 할 일 | 결과 → 다음 |
|---|---|---|
| 1 | 재부팅 주기 측정 (UART 타임스탬프, scope POR_B/SRST_B) | 일정 주기 (예 정확히 8 s) → **WDT 타임아웃** 의심 / 불규칙 → 전원·노이즈 |
| 2 | reset reason raw 값 읽기 (다음 부팅 초기 or JTAG) | WDT / SW / PIN / POR·BOR 로 분기 |
| 3a | **WDT**: 마지막 UART 로그, JTAG로 halt 후 PC·콜스택, WDT kick 위치·주기, debug freeze 설정 | 무한 루프 / 예외 핸들러에서 멈춤 / kick 주기 > timeout / BootROM·FSBL이 WDT 켜 둠 |
| 3b | **SW**: 누가 리셋 레지스터를 썼나 — 예외 핸들러가 "fatal → reset" 하나? (HardFault → 리셋 정책) | 예외 원인(BV08 디버그) |
| 3c | **PIN/SRST**: 리셋 라인을 누가 당기나 — 각 리셋원 분리(0 Ω 제거, CPLD 로그, 디버거 분리) | supervisor 임계값 근처 rail / 외부 WDT IC / 디버거 nSRST |
| 3d | **POR/BOR**: rail scope, 부하 걸리는 순간(DDR training, CPU 풀로드, PCIe 링크업) droop | → [BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html) 전원: 레귤레이터 한계·OCP·PMIC fault 레지스터 |
| 4 | 같은 증상이 다른 보드에서도? (golden board 비교) | 1장만 → 실장/부품 / 전부 → 설계·FW |
| 5 | 고친 뒤: 1000회 전원 사이클 + 24 h soak 로 재발 없음 확인 | 회귀 테스트에 추가 |

### 3.5 첫 전원 인가 날 리셋/strap 체크리스트

| # | 항목 | 방법 | 통과 기준 |
|---|---|---|---|
| 1 | 리셋 해제가 마지막 rail 이후 | scope CH1/CH3 | ≥ 데이터시트 t_hold, glitch 없음 |
| 2 | 리셋 해제가 osc 안정 이후 | scope CH3/CH4 | osc 진폭·주파수 안정 후 |
| 3 | 리셋 Low 레벨·High 레벨 | scope / DMM | VIL 이하 / VIH 이상, 노이즈 마진 |
| 4 | 모든 strap 핀 전압 (리셋 유지 상태) | DMM | VIH 이상 또는 VIL 이하, 여유 ≥ 10 % VCCIO [추정: 사내 기준으로] |
| 5 | 샘플된 boot mode 레지스터 = 의도 | JTAG `mrd` | 일치 |
| 6 | reset reason = POR (첫 전원) | JTAG / UART | external POR 비트만 |
| 7 | 버튼 리셋·SW 리셋·WDT 리셋 각각 reason 정확 | 각각 유발 | 해당 비트 |
| 8 | debug halt 시 WDT 정지 | halt 후 timeout 이상 대기 | 리셋 안 됨 |
| 9 | boot mode 바꿔서(JTAG/SD/QSPI) 각각 부팅 | 딥스위치 | 전부 OK |
| 10 | 전원 사이클 100회 | 자동 PSU 스크립트 | 100/100 같은 모드·같은 reason |

## 4. 꼬리 질문 체인

### 4.1 BV00 체인 — "The board keeps rebooting every few seconds. How do you debug it?"

<details><summary>답 펼치기</summary>

- **Q. The board keeps rebooting every few seconds. How do you debug it?** — "I want to know who is resetting the chip before I form any theory. On the next boot I read the reset-cause register first thing, and I capture the UART log right up to the reset. In parallel I put a scope on the reset pin and the main rails, triggered on reset assertion, and I measure the reboot period — a very regular period smells like a watchdog, an irregular one like power or noise." · 한국어: 누가 리셋했나 먼저 — reason 레지스터 + UART 마지막 줄 + scope(주기).
- **꼬리1. Where do you look first?** — "The reset-reason register, because it splits the problem into software, power, or an external pin in one read. I make sure it's read before any code clears it and that I look at the raw value, since sticky bits from earlier resets can accumulate. Then the last UART lines tell me how far boot got." · 한국어: reason raw 값 → UART 마지막 줄.
- **꼬리2. Reset reason says watchdog, but the code kicks it.** — "Then either the kick isn't happening as often as we think, or something else stops it. I'd halt with JTAG near the timeout to see where the PC is — often it's stuck in an exception handler or a polling loop with no timeout. I check whether the kick is in an interrupt that's masked, whether the timeout was configured shorter than intended — for instance, the clock feeding the watchdog is different on this board — and whether the boot ROM or FSBL already started a watchdog with a default timeout before my code runs. Also if the watchdog is a window type, kicking too early also resets." · 한국어: JTAG로 PC 확인, kick 위치/주기, WDT 클럭, 앞단 부트로더가 켠 WDT, window WDT 조기 kick.
- **꼬리3. Reset reason says brown-out.** — "That's a power problem until proven otherwise. I scope the core and IO rails with the trigger on the reset assertion and look backward in time for a droop — typically when the load steps up, like DDR training or all cores starting. Then I check the regulator's current limit and the PMIC fault registers, the supervisor's threshold versus the rail's minimum, and the decoupling. A quick experiment is to raise the rail setpoint slightly or to limit the load and see if the resets stop." · 한국어: rail droop 찾기(리셋 트리거로 과거 보기) → 레귤레이터 한계·PMIC fault·supervisor 임계값 → BV03.
- **꼬리4 (추가). It only happens on 2 boards out of 10.** — "Then I compare those two against a good one: the rail ripple and droop, the supervisor part and threshold, the reset line with a scope for glitches, strap voltages, and the rework history. Swapping the SoC or the supervisor between a good and a bad board tells me whether the problem follows the part or the board." · 한국어: 좋은 보드와 비교 + 부품 swap (따라가는 쪽이 원인).

</details>

### 4.2 BV00 체인 — "What's a strap pin and what can go wrong with it?"

<details><summary>답 펼치기</summary>

- **Q. What's a strap pin and what can go wrong with it?** — "A strap is a pin whose level is sampled once, when reset is released, to configure the chip — boot device, PHY address, PLL bypass, debug options. After that the pin is often reused as a GPIO or an LED. What goes wrong is always about the voltage at that exact instant: a pull resistor that's too weak against leakage or another driver, another chip actively driving the net during reset, or the IO rail for that pin not being up yet when reset releases." · 한국어: 리셋 해제 순간 한 번 샘플 — 실패는 전부 "그 순간의 전압" 문제.
- **꼬리1. The strap is right on the schematic but the chip boots in the wrong mode.** — "First I read what the chip actually latched — on Zynq UltraScale+ that's the boot-mode register in CRL_APB — so I know it's a sampling problem and not an image problem. Then I hold the chip in reset and measure the strap pin with a DMM: if it's between VIL and VIH or on the wrong side, I look for who else is on that net. Typical culprits are a CPLD or FPGA output that drives the net during reset, an LED or a PHY input with leakage against a 100k pull, a wrong resistor value in the BOM, or the IO bank powered late." · 한국어: 샘플된 값 레지스터 → 리셋 중 DMM → 같은 net의 다른 장치·BOM·IO bank 전원 순서.
- **꼬리2. How would you choose the pull resistor value?** — "It has to be strong enough that the worst-case leakage times R keeps me well beyond VIH or VIL, and strong enough to win against any internal pull or other driver, with the RC time constant settling before the sample point. But weak enough not to load the pin when it later becomes an output or drives an LED. Something like 4.7k to 10k is typical, but I'd do the worst-case math with the datasheet leakage and thresholds." · 한국어: 누설×R 여유, 내부 풀/다른 드라이버 대비, RC 정착 vs 이후 출력 부하 — 최악 조건 계산.
- **꼬리3. How do you make strap problems visible in a board test?** — "Every boot, the test firmware prints the latched strap and boot-mode register values, and the automation compares them to the expected values for that board revision. Plus a power-cycle loop across temperature, because a marginal strap shows up as an occasional wrong mode, not a constant one." · 한국어: 매 부팅 strap 레지스터 출력 + 기대값 비교 + 온도·전원 사이클 반복.

</details>

### 4.3 BV00 체인 — "What's the right order of power, clock and reset? Who guarantees it?"

<details><summary>답 펼치기</summary>

- **Q. What's the right order of power, clock and reset?** — "Power first — every required rail within tolerance, in the order the datasheet asks for. Then the reference clock has to be running and stable. Only then is reset released, with a minimum hold time after the last rail and the clock are good. Inside the chip the same rule repeats per block: power domain on, clock enabled, then reset deasserted, then software touches registers." · 한국어: 전원 → 클럭 → 리셋 해제 (+ hold 시간), 칩 안에서도 블록마다 같은 순서.
- **꼬리1. Who guarantees it on the board?** — "Usually the PMIC sequencer or a chain of regulators with enable and power-good signals, and a reset supervisor or the PMIC's reset output that holds the SoC in reset until the monitored rails are good plus a programmed delay. That delay must cover the oscillator start-up. A plain RC delay is cheap but doesn't catch brown-outs and is sensitive to slow ramps." · 한국어: PMIC 시퀀서/PGOOD 체인 + supervisor 지연(tD ≥ osc 시작 시간). RC는 brown-out 못 잡음.
- **꼬리2. What happens if reset releases before the clock is stable?** — "The boot ROM can execute on a garbage clock, or blocks with synchronous reset de-assertion may come out of reset in an undefined state. Symptoms are random — sometimes boots, sometimes hangs — which is the worst kind. I'd measure it with the scope: reset-pin rising edge relative to the oscillator settling." · 한국어: 랜덤 부팅 실패 — scope로 리셋 엣지 vs osc 정착 비교.
- **꼬리3. How do you verify it on every board automatically?** — "On the bench a multi-channel scope or a cheap logic analyzer with analog channels captures the rails, power-good and reset on each power-up, and a script extracts the 10/90 percent times and the deltas and compares them to the spec — the same checker for every board. In production you can't scope everything, so you rely on the PMIC's own sequencing logs or power-good fault registers plus the boot-time and reset-reason logs." · 한국어: 다채널 캡처 → 스크립트로 시점 추출·스펙 비교 (BV03 체커), 양산은 PMIC 로그 + 부팅 로그.

</details>

### 4.4 추가 체인 — "How would you handle the watchdog in bring-up test firmware?"

<details><summary>답 펼치기</summary>

- **Q. How would you handle the watchdog in bring-up test firmware?** — "In the very first bring-up I want the watchdog either off or very long and frozen on debug halt, so a breakpoint doesn't reset the chip under me. In the test runner, each test has its own software timeout, and the hardware watchdog is the last line of defense for real hangs. Before each test I write the test ID into a no-init record; if the watchdog fires, the next boot reads reset-reason equals watchdog, reports that test as HANG, and continues with the next one." · 한국어: 초기엔 끄거나 길게+debug freeze, 러너는 테스트별 SW 타임아웃 + 최후 WDT, noinit에 현재 테스트 ID.
- **꼬리1. Why not kick the watchdog from a timer interrupt?** — "Because the timer interrupt keeps running even when the main loop is dead, so the watchdog never fires and you lose the protection. Kick from the main loop only after every task has checked in." · 한국어: ISR kick은 메인 루프 죽어도 계속 kick → 무의미.
- **꼬리2. What's a window watchdog for?** — "It also resets if you kick too early. That catches a runaway loop that happens to call the kick function repeatedly, and it enforces that the scheduler timing is roughly right. Some have an early-warning interrupt so you can log state just before the reset." · 한국어: 너무 이른 kick도 리셋 → 폭주 루프 감지, EWI로 마지막 로그.
- **꼬리3. How would you validate the watchdog itself on new silicon?** — "Program a set of timeouts and measure the actual reset time against the watchdog clock across temperature, verify the reset-reason bit, verify early-kick behavior for window mode, verify the debug-freeze bit, and verify which domains it resets and what survives. That's a classic safety-mechanism validation: inject the fault and prove the mechanism reacts." · 한국어: 타임아웃 정확도(온도), reason 비트, window, debug freeze, 리셋 범위 — fault injection.

</details>

### 4.5 추가 체인 — "Warm vs cold reset — why does it matter for validation?"

<details><summary>답 펼치기</summary>

- **Q. What's the difference between a warm and a cold reset, and why does it matter?** — "A cold reset removes power or is equivalent to it, so everything starts from scratch. A warm reset keeps power and resets logic, so some state survives — SRAM contents, persistent registers, sometimes PHY or debug configuration. It matters because a lot of bugs only show up on warm reset: a block that isn't fully reset and starts in a stale state, a PHY that keeps its old link state, a DMA that was mid-transfer." · 한국어: 콜드 = 전부 초기화 / 웜 = 일부 생존 → 웜 리셋에서만 나는 버그가 많다.
- **꼬리1. How do you test reset behavior systematically?** — "A reset matrix: every reset type against every block, checking reset values, what must survive, and the reason bits. And I reset during activity — DMA, PCIe traffic, DDR stress — then loop it thousands of times and look for the rare failure." · 한국어: 리셋 종류×블록 매트릭스 + 트래픽 중 리셋 + 수천 회 반복.
- **꼬리2. What's SYSRESETREQ on Cortex-M?** — "A bit in the AIRCR register at 0xE000ED0C; you write it with the key 0x05FA in the upper half. It asks the system's reset controller for a system reset; what actually gets reset is up to the SoC integration, and debug logic usually isn't. CMSIS wraps it in NVIC_SystemReset with barriers around it." · 한국어: AIRCR(0xE000ED0C) + VECTKEY 0x05FA + bit2, 범위는 SoC가 정함.

</details>

### 4.6 추가 체인 — "The board bricked after a firmware update. How do you recover, and how should the design prevent it?"

<details><summary>답 펼치기</summary>

- **Q. A firmware update bricked the board. What now?** — "Short term, change the boot mode to JTAG or the boot ROM's USB/UART download mode, load a known-good image into SRAM, and reflash. Long term, the design needs an A/B or golden-image scheme: the boot ROM or first-stage loader falls back if the image fails its header or signature check — Zynq UltraScale+ does a golden-image search across 32 KB offsets — and a boot counter falls back if the new image boots but keeps crashing." · 한국어: 단기 = JTAG/다운로드 모드로 재기록, 장기 = A/B·golden + 헤더 검사 fallback + boot counter.
- **꼬리1. The image header is valid but the app crashes every boot. Fallback doesn't help — why?** — "Because header or signature fallback only catches images that are invalid, not images that are valid but broken. That's what the boot counter covers: count consecutive failed boots, and the app clears the counter only after it reached a healthy state; above the limit, boot the alternate image." · 한국어: 헤더 fallback은 "깨진 이미지"만, "정상 이미지인데 죽는 경우"는 boot counter.

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up Cortex-R8, R82 and M0+ cores on the pre-silicon FPGA platform, so the first thing I always establish is that reset is released cleanly and that I can halt the core right after reset with Trace32 — vector catch — before trusting anything else. [숫자 채우기: 몇 개 코어/몇 번의 FPGA 이미지]"
- "In SSD firmware, unexpected resets and power loss are a core concern — we logged the reset cause and a boot counter in retained memory so a reboot loop could be diagnosed from the field log, and we had fallback firmware slots. [숫자 채우기: 실제 쓴 메커니즘 이름 — 예: FW slot / boot counter]"
- "At SK hynix the chamber reliability platform power-cycled devices for long runs across temperature; the automation recorded every boot, and marginal behavior showed up as rare outliers in thousands of cycles — that's exactly how I'd hunt a marginal strap or a reset-timing issue. [숫자 채우기: 사이클 수 / 온도 범위]"
- "At Apple, root-causing interface issues with a scope and protocol analyzer taught me to trigger on the event — here the reset edge — and look backwards in time for the cause."

## 6. 실습 / 코드

### 6.1 `onsitePrep/bv05/reset_reason_bootcount.c` — 검증됨

- 무엇: (A) **ZynqMP RESET_REASON 디코더** (UG1087 / U-Boot 정의 비트 레이아웃 [1][3]), (B) 가짜 SoC를 여러 번 리셋시키며 **noinit 부트 카운터 + WDT 루프 탈출** 3개 시나리오 (정상 FW / W1C 안 지우는 FW / noinit 레코드를 BootROM 스크래치 영역에 둔 FW), (C) **strap 전기 모델**(풀 저항·누설·다른 드라이버·IO rail 미안정) → 논리값 → ZynqMP boot mode.
- 빌드·실행 (macOS) [확인됨]:

```bash
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined onsitePrep/bv05/reset_reason_bootcount.c -o /tmp/bv05_rr && /tmp/bv05_rr
# 전체 출력: onsitePrep/bv05/reset_reason_bootcount.out
```

- **실제 출력 (Part A 발췌)**:

```text
=== Part A: Zynq UltraScale+ RESET_REASON decode ===
  RESET_REASON=0x0001 -> EXTERNAL
      EXTERNAL : PS_POR_B: real power-on / power cycle
  RESET_REASON=0x0010 -> SRST
      SRST     : PS_SRST_B pin: button / supervisor / BMC
  RESET_REASON=0x0021 -> SOFT EXTERNAL
      SOFT     : software wrote RESET_CTRL (reboot cmd)
      EXTERNAL : PS_POR_B: real power-on / power cycle
      note     : >1 bit set = sticky bits accumulated since last POR (nobody cleared them)
```

- 해석: `0x0021` = 전원 투입(bit0) 후 아무도 안 지운 상태에서 SW 리셋(bit5)이 더해짐. U-Boot처럼 "첫 매칭만" 출력하면 `SOFT`, 우선순위가 반대인 디코더면 `EXTERNAL` → **raw 값을 같이 남겨야** 하는 이유.

- **실제 출력 (Part B, 정상 FW)**:

```text
[B] good FW (rec @SRAM+128, clear_w1c=1, wdt_limit=3)
     (bench PSU on)
  #1  raw=0x01 POR            naive=POR  rec=cold(invalid rec) boot_count=1 wdt_streak=0 -> normal
     (FW update -> reboot; new FW hangs)
  #2  raw=0x04 SW             naive=SW   rec=warm              boot_count=2 wdt_streak=0 -> normal
     (watchdog fires)
  #3  raw=0x08 WDT            naive=WDT  rec=warm              boot_count=3 wdt_streak=1 -> normal
     (watchdog fires)
  #4  raw=0x08 WDT            naive=WDT  rec=warm              boot_count=4 wdt_streak=2 -> normal
     (watchdog fires)
  #5  raw=0x08 WDT            naive=WDT  rec=warm              boot_count=5 wdt_streak=3 -> SAFE MODE (golden image, no app)
     (engineer presses reset button)
  #6  raw=0x02 PIN            naive=PIN  rec=warm              boot_count=6 wdt_streak=0 -> normal
     (load step -> rail droop -> brown-out)
  #7  raw=0x10 BOR            naive=BOR  rec=cold(invalid rec) boot_count=1 wdt_streak=0 -> normal
     (power cycle)
  #8  raw=0x01 POR            naive=POR  rec=cold(invalid rec) boot_count=1 wdt_streak=0 -> normal
```

- 해석 줄별: #1 POR → SRAM 쓰레기 → 레코드 무효 → cold init. #2–#5 warm 리셋이라 레코드 생존, 연속 WDT 3회째(#5)에 **SAFE MODE**. #6 버튼 리셋은 WDT가 아니므로 streak 0. #7 brown-out은 전원 손실 → SRAM 날아감 → boot_count 1로 리셋.

- **실제 출력 (Part B, W1C 안 지우는 FW — 함정 재현)**:

```text
[B] FW forgets to clear W1C cause bits (rec @SRAM+128, clear_w1c=0, wdt_limit=3)
  #1  raw=0x01 POR            naive=POR  rec=cold(invalid rec) boot_count=1 wdt_streak=0 -> normal
  #2  raw=0x05 POR|SW         naive=POR  rec=cold(POR)         boot_count=1 wdt_streak=0 -> normal
  #3  raw=0x0d POR|SW|WDT     naive=POR  rec=cold(POR)         boot_count=1 wdt_streak=1 -> normal
  #4  raw=0x0d POR|SW|WDT     naive=POR  rec=cold(POR)         boot_count=1 wdt_streak=1 -> normal
  #5  raw=0x0d POR|SW|WDT     naive=POR  rec=cold(POR)         boot_count=1 wdt_streak=1 -> normal
  #6  raw=0x0f POR|PIN|SW|WDT naive=POR  rec=cold(POR)         boot_count=1 wdt_streak=1 -> normal
```

- 해석: POR 비트가 계속 남아 있어 매 부팅이 "cold"로 판정 → 카운터가 매번 0으로 → **SAFE MODE가 영원히 안 걸림 = 무한 재부팅 루프**. 순진한 디코더는 계속 "POR"(전원 문제)라고 보고 → 엉뚱하게 전원팀을 부르게 된다.

- **실제 출력 (Part B, noinit 레코드가 BootROM 스크래치 영역 안 — 함정 재현)**:

```text
[B] noinit record inside BootROM scratch (rec @SRAM+16, clear_w1c=1, wdt_limit=3)
  #2  raw=0x04 SW             naive=SW   rec=cold(invalid rec) boot_count=1 wdt_streak=0 -> normal
  #3  raw=0x08 WDT            naive=WDT  rec=cold(invalid rec) boot_count=1 wdt_streak=1 -> normal
  #4  raw=0x08 WDT            naive=WDT  rec=cold(invalid rec) boot_count=1 wdt_streak=1 -> normal
  #5  raw=0x08 WDT            naive=WDT  rec=cold(invalid rec) boot_count=1 wdt_streak=1 -> normal
```

- 해석: reason은 정확하지만, BootROM이 매 부팅 SRAM 앞부분을 스택으로 쓰며 덮어씀 → 레코드가 매번 무효 → 카운터 안 쌓임. 실제 칩에서는 **BootROM/FSBL이 쓰는 OCM 영역**(TRM의 메모리 맵)을 피해서 `.noinit`을 배치하거나 persistent 레지스터를 써야 한다.

- **실제 출력 (Part C, strap)**:

```text
[C] MODE1 pull-up 100k + 10uA leakage (VCCIO at sample = 1.80 V)
    MODE1   PU 100.0k  leak= 10.0uA  other=none     V=0.800 V -> UNDEFINED (between VIL/VIH)
    MODE[3:0]=0x0 (JTAG)  expected 0x2 (QSPI32)  => UNRELIABLE (board-dependent!)

[C] MODE1 shared with a CPLD output driving low (VCCIO at sample = 1.80 V)
    MODE1   PU   4.7k  leak=  0.0uA  other=DRIVING  V=0.537 V -> 0
    MODE[3:0]=0x0 (JTAG)  expected 0x2 (QSPI32)  => WRONG BOOT MODE

[C] IO rail still ramping when reset released (VCCIO at sample = 0.90 V)
    MODE1   PU   4.7k  leak=  0.0uA  other=none     V=0.900 V -> 0
    MODE[3:0]=0x0 (JTAG)  expected 0x2 (QSPI32)  => WRONG BOOT MODE
```

- 해석: 4.7 kΩ + 아무것도 없음 → OK (전체 출력 참조). 100 kΩ × 10 μA = 1 V 강하 → 0.8 V는 VIL(0.63)과 VIH(1.17) 사이 → **보드마다/온도마다 다름**. CPLD가 2 kΩ 출력 저항으로 Low를 잡으면 분압 0.537 V → '0'. IO rail이 0.9 V일 때 샘플 → '0'. 세 경우 모두 결과는 "JTAG 모드(0x0)" — **증상은 같고 원인은 셋** → 측정으로 가른다 (3.1).
- 한계: VIH/VIL = 0.65/0.35 × VCCIO는 일반적 LVCMOS 근사 [추정] — 실제는 SoC 데이터시트 값 사용. "IO rail 미안정" 판정은 단순화.

### 6.2 랩 레시피 `[미실행]`

- Cortex-M 보드(예: Nucleo)가 있다면: (1) 부팅 직후 `RCC->CSR` 출력 → (2) 버튼 리셋 / `NVIC_SystemReset()` / IWDG 타임아웃 / 전원 분리를 각각 유발 → 비트 변화 기록 → (3) `RMVF` 안 쓰면 누적되는 것 확인 → (4) `DBG_IWDG_STOP` on/off 상태에서 breakpoint 걸고 리셋 여부 확인 [12][14][15].
- ZCU102가 있다면: SW6 boot mode 스위치를 바꿔 가며 `mrd 0xFF5E0200` 값 비교, `reboot` 후 `md 0xff5e0220 1` 로 SOFT 비트 확인, `pggs0`에 값을 쓰고 `reboot` → 유지, 전원 사이클 → 0 확인 [1][2][7].

## 7. 자주 틀리는 것

1. **회로도의 strap을 믿고 레지스터를 안 읽음** — 칩이 실제로 latch한 값(`BOOT_MODE_POR`/`BOOT_MODE_USER`)이 진실 [2].
2. **부팅 후 strap 핀 전압을 잼** — 부팅 후엔 GPIO/LED로 바뀌었을 수 있다. 리셋을 잡은 상태에서 잰다.
3. **Reset reason을 안 지우거나 늦게 읽음** — sticky W1C는 쌓인다; 다른 코드(부트로더)가 먼저 지울 수도 있다. raw 값을 기록하라 (6.1 재현). ZynqMP에선 APU가 못 지우는 경우도 있음 [5].
4. **STM32 PIN 리셋 플래그를 원인으로 오진** — 모든 내부 리셋이 NRST를 당기므로 PIN은 거의 항상 선다 → 마지막에 검사 [15].
5. **SYSRESETREQ = 칩 전체 리셋이라고 단정** — 범위는 SoC 통합이 정한다; 디버그 로직·일부 always-on 블록은 보통 제외 [11].
6. **WDT를 타이머 ISR에서 kick** — 메인 루프가 죽어도 리셋이 안 난다.
7. **Debug freeze 미설정** — breakpoint에서 멈추면 WDT가 리셋 → "디버거 붙이면 재부팅" [12].
8. **IWDG/WWDG를 SW로 끌 수 있다고 가정** — 한번 시작하면 리셋 전까지 못 끈다 [12][13]; 앞단 부트로더가 이미 켰을 수 있다.
9. **`use_alt` 같은 boot override가 SW reset을 넘어 남는 것을 모름** — POR만 지움 → reboot 후 다른 장치에서 부팅 [24].
10. **RC 리셋 회로로 brown-out이 잡힌다고 생각** — 캡이 이미 충전돼 있으면 짧은 droop엔 리셋이 다시 안 걸린다 → supervisor 필요 [16].

## 8. 출처

1. AMD UG1087 Zynq UltraScale+ Devices Register Reference, "RESET_REASON (CRL_APB) Register" (0xFF5E0220, bit 정의, W1C, POR only) — https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/RESET_REASON-CRL_APB-Register
2. AMD UG1087, "BOOT_MODE_USER (CRL_APB) Register" / "BOOT_MODE_POR (CRL_APB) Register" (boot_mode[3:0], use_alt bit 8, alt_boot_mode[15:12]) — https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/BOOT_MODE_USER-CRL_APB-Register , https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/BOOT_MODE_POR-CRL_APB-Register
3. U-Boot, `arch/arm/mach-zynqmp/include/mach/hardware.h` (CRL_APB 0xFF5E0000, boot_mode 0x200, reset_reason 0x220, RESET_REASON_* 비트, boot mode 값, PMU_GLOBAL 0xFFD80000) — https://github.com/u-boot/u-boot/blob/master/arch/arm/mach-zynqmp/include/mach/hardware.h
4. U-Boot, `board/xilinx/zynqmp/zynqmp.c` (`reset_reasons[]` 표와 `reset_reason()` — "Reset reason:" 출력, 첫 매칭만) — https://github.com/u-boot/u-boot/blob/master/board/xilinx/zynqmp/zynqmp.c
5. U-Boot commit "arm64: zynqmp: Do not clear reset reason" (PMUFW 보호로 APU 쓰기 거부) — https://git.uninsane.org/colin/u-boot/commit/3aba25bc382beeb8a92b46d23fd1db47dfcb1121
6. AMD UG1087, "PERS_GLOB_GEN_STORAGE0 (PMU_GLOBAL) Register" (0xFFD80050, POR에서만 리셋) — https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/PERS_GLOB_GEN_STORAGE0-PMU_GLOBAL-Register
7. Linux kernel, `Documentation/ABI/stable/sysfs-driver-firmware-zynqmp` (ggs*/pggs* sysfs, 4–7은 Xilinx SW 사용) — https://github.com/torvalds/linux/blob/master/Documentation/ABI/stable/sysfs-driver-firmware-zynqmp
8. AMD UG1137 Zynq UltraScale+ MPSoC Software Developer Guide, "Boot Modes", "Boot Flow", "Pre-Boot Sequence", "PMU Firmware Loading Options" — https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/Boot-Modes , https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/Pre-Boot-Sequence , https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/PMU-Firmware-Loading-Options
9. AMD UG1137, "Fallback and MultiBoot Flow" (32 KB 오프셋 golden image search, CSU_MULTI_BOOT + soft reset) — https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/Fallback-and-MultiBoot-Flow ; Foundries.io "Boot Software Updates on Zynq UltraScale+ MPSoC" — https://docs.foundries.io/94/reference-manual/security/boot-software-updates-zynqmp.html
10. KP Labs, "Override boot mode using JTAG" (xsdb `rwr crl_apb boot_mode_user ...`, `rst -system`) — https://docs.sml.kplabs.space/how_to/zynqmp/jtag_set_boot_mode.html
11. Arm CMSIS_5, `CMSIS/Core/Include/core_cm3.h` (SCB_AIRCR_VECTKEY_Pos 16, SYSRESETREQ bit 2, VECTRESET bit 0, `__NVIC_SystemReset`, SCB base 0xE000ED00) — https://github.com/ARM-software/CMSIS_5/blob/develop/CMSIS/Core/Include/core_cm3.h
12. STMicroelectronics, STM32L4 HAL `stm32l4xx_hal_iwdg.c` 헤더 주석 (LSI 클럭, 시작 후 비활성화 불가, 0xAAAA reload, DBG_IWDG_STOP, timeout 범위) — https://github.com/STMicroelectronics/stm32l4xx-hal-driver/blob/master/Src/stm32l4xx_hal_iwdg.c
13. STMicroelectronics, STM32L4 HAL `stm32l4xx_hal_wwdg.c` 헤더 주석 (window, 0x40→0x3F 리셋, EWI, 비활성화 불가) — https://github.com/STMicroelectronics/stm32l4xx-hal-driver/blob/master/Src/stm32l4xx_hal_wwdg.c
14. STMicroelectronics, `cmsis_device_l4/Include/stm32l476xx.h` (RCC_CSR 리셋 플래그 비트 위치, RMVF bit 23) — https://github.com/STMicroelectronics/cmsis_device_l4/blob/master/Include/stm32l476xx.h
15. Micromouse Online, "STM32 reset source" (내부 리셋이 NRST를 당겨 PIN 플래그가 항상 섬, 검사 순서, RMVF) — https://micromouseonline.com/2012/03/29/stm32-reset-source/
16. TI SLVAE47 "Voltage Supervisors FAQ" (정확도·최악 검출·hysteresis·리셋 지연) / SSZTAR7 detector vs supervisor — https://ti.com/lit/SLVAE47 , https://ti.com/lit/pdf/ssztar7
17. TI TPS3840 datasheet (VIT- + VHYS, CT 캡으로 tD, MR 입력) — https://www.ti.com/lit/gpn/TPS3840
18. TI SNLA258 "Configuring 4-level strap pins" (분압·내부 풀·샘플 전 충전 미완료 위험) — https://edgeworker.ti.com/lit/pdf/snla258
19. TI SNLA344C DP83826 bootstrap 설정 (LED1/CLKOUT strap은 POR에서만 latch) — https://ti.com/document-viewer/lit/html/SNLA344C/GUID-EB9FDE89-AACC-4B87-B763-6A247F40056B
20. U-Boot docs, "Boot Count Limit" (bootcount, bootlimit, altbootcmd, upgrade_available, 앱이 0으로 리셋) — https://docs.u-boot.org/en/latest/api/bootcount.html
21. Memfault docs, "MCU Reboot Reasons" (noinit RAM 영역에 reboot 정보, 링커 NOLOAD) — https://docs.memfault.com/docs/mcu/reboot-reason-tracking
22. Linux kernel, `Documentation/ABI/testing/sysfs-class-watchdog` (bootstatus) + `include/uapi/linux/watchdog.h` (WDIOF_CARDRESET 0x0020) — https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-class-watchdog , https://github.com/torvalds/linux/blob/master/include/uapi/linux/watchdog.h
23. barebox patch "arm: zynqmp: add boot source support" (UG1085 Table 11-1 기반 boot mode 값, PJTAG0/1 = 0x8/0x9) — https://lore.barebox.org/barebox/20210913121350.9307-4-michael.riesch@wolfvision.net/
24. U-Boot commit "ARM64: zynqmp: Fix secondary bootmode enabling" (use_alt은 POR에만 지워지고 SW reset 후 유지) — https://git01.mediatek.com/plugins/gitiles/filogic/uboot/+/833e0c49b9954ee055daaea07e155c616603ff41
