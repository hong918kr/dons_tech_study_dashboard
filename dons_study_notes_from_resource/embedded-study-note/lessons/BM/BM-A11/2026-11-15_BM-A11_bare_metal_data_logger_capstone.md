---
id: BM-A11
time: 130분
updated: 2026-11-15
---
# BM-A11 종합: 베어메탈 데이터 로거

## 학습 목표

- 직접 만든 부품(링커 스크립트·startup·타이머 ISR·링 버퍼·인터럽트 UART·워치독·fault 덤프·이미지 헤더)을 **한 펌웨어**로 묶고, 각 부품 사이의 "접착부(glue)" — ISR↔main 인계, 송신기 깨우기(prime), 워치독 킥 정책 — 가 어디서 틀리는지 실측으로 말할 수 있다.
- QEMU `mps2-an385` 의 CMSDK timer/UART/watchdog 동작(IRQ 번호, TX 인터럽트가 언제 서는지, 워치독 2 단계 NMI→리셋)을 **소스와 탐침(probe)으로 확인**하고 그 사실 위에 설계한다.
- 한 번의 QEMU 실행(5 부팅) 안에서 hang → 워치독 리셋 → 원인 보고 → fault → 덤프 보고를 재현하고, "같은 워치독 리셋인데 원인이 다른" 두 경우를 빵부스러기(breadcrumb)로 구별한다.
- 이미지 헤더를 빌드 뒤 도장 찍고(`mkimg.py`), 부팅 때 자기 검사해서 비트 하나 뒤집힌 이미지를 거부한다.
- `arm-none-eabi-size`/맵 파일로 메모리 예산 표를, icount 가상 시간으로 CPU 부하 추정을 만든다 — 그리고 그 숫자가 **무엇이 아닌지**(사이클이 아니다)도 말한다.

## 1. 문제

시니어 펌웨어 면접의 마지막 질문은 대개 "부품을 다 아는 건 알겠다, **그걸 엮은 제품**은 어떻게 죽고 어떻게 살아나나?" 다. Don 이 SSD 컨트롤러에서 했던 일 — 호스트 명령 경로, 타이머, 로그, 워치독, assert 덤프를 한 펌웨어에 넣고 현장 반품에서 원인을 회수하던 일 — 을 Cortex-M3 한 장에서 처음부터 끝까지 다시 해 본다. 부품은 이미 다 있다: startup·링커 스크립트(BM-B08), 타이머(BM-B14), 인터럽트 UART(BM-M04), 워치독(BM-M10), fault 정책·`.noinit`(BM-M13), 이미지 헤더(BM-A04), 크래시 덤프(BM-A09). 이 레슨의 주인공은 **부품 사이의 접착부**와 **장애 시나리오 대본**이다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">모듈 구조와 데이터 흐름 (화살표 = 데이터, 점선 = 감시/기록)</text>
  <rect x="10" y="32" width="160" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="50" fill="currentColor" font-weight="bold">startup + a11.ld</text>
  <text x="20" y="66" fill="currentColor">.data 복사 / .bss 0</text>
  <text x="20" y="82" fill="currentColor">.noinit 는 그대로</text>
  <rect x="190" y="32" width="170" height="58" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="200" y="50" fill="currentColor" font-weight="bold">img_verify (헤더)</text>
  <text x="200" y="66" fill="currentColor">CRC-32 [0, _img_hdr)</text>
  <text x="200" y="82" fill="currentColor">실패 → SAFE MODE, exit 3</text>
  <rect x="380" y="32" width="170" height="58" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="390" y="50" fill="currentColor" font-weight="bold">reset cause</text>
  <text x="390" y="66" fill="currentColor">dump? → FAULT</text>
  <text x="390" y="82" fill="currentColor">crumb? → WATCHDOG</text>
  <rect x="570" y="32" width="160" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="580" y="50" fill="currentColor" font-weight="bold">.noinit 0x2000FC00</text>
  <text x="580" y="66" fill="currentColor">g_scen 532 B</text>
  <text x="580" y="82" fill="currentColor">g_crumb · g_dump</text>
  <path d="M170 61 L190 61 M360 61 L380 61" stroke="currentColor"/>
  <path d="M550 61 L570 61" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="10" y="130" width="170" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="148" fill="currentColor" font-weight="bold">timer0 ISR (IRQ 8)</text>
  <text x="20" y="164" fill="currentColor">100 Hz, prio 0x40</text>
  <text x="20" y="180" fill="currentColor">TODO 1: tick++ → 샘플</text>
  <text x="20" y="194" fill="currentColor">sensor_value(tick)</text>
  <rect x="210" y="138" width="110" height="54" rx="6" fill="none" stroke="currentColor"/>
  <text x="220" y="158" fill="currentColor">샘플 링 8</text>
  <text x="220" y="176" fill="currentColor">SPSC, drop+1</text>
  <rect x="350" y="130" width="170" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="360" y="148" fill="currentColor" font-weight="bold">main 루프 (Thread)</text>
  <text x="360" y="164" fill="currentColor">pop → "S b nn t= v=" 줄</text>
  <text x="360" y="180" fill="currentColor">비면 cpsid; WFI; cpsie</text>
  <text x="360" y="194" fill="currentColor">TODO 4: wdog_service()</text>
  <rect x="550" y="138" width="110" height="54" rx="6" fill="none" stroke="currentColor"/>
  <text x="560" y="158" fill="currentColor">로그 링 512</text>
  <text x="560" y="176" fill="currentColor">TODO 3: kick</text>
  <path d="M180 165 L210 165 M320 165 L350 165 M520 165 L550 165" stroke="currentColor"/>
  <path d="M210 165 l-8 -4 l0 8 z M350 165 l-8 -4 l0 8 z M550 165 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="550" y="232" width="180" height="58" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="560" y="250" fill="currentColor" font-weight="bold">UART0 TX ISR (IRQ 1)</text>
  <text x="560" y="266" fill="currentColor">prio 0x80, TODO 2</text>
  <text x="560" y="282" fill="currentColor">→ logtool.py 비교</text>
  <path d="M605 192 L605 232" stroke="currentColor"/><path d="M605 232 l-4 -8 l8 0 z" fill="currentColor"/>
  <rect x="210" y="232" width="310" height="58" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="220" y="250" fill="currentColor" font-weight="bold">CMSDK watchdog 0x40008000 (25 MHz)</text>
  <text x="220" y="266" fill="currentColor">LOAD 2,500,000 = 100 ms: 1차 → NMI (crumb)</text>
  <text x="220" y="282" fill="currentColor">2차 100 ms → 리셋 (RESEN). 킥 = INTCLR</text>
  <path d="M435 200 L435 232" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="10" y="232" width="170" height="58" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="250" fill="currentColor" font-weight="bold">HardFault → 덤프</text>
  <text x="20" y="266" fill="currentColor">frame+CFSR/HFSR/BFAR</text>
  <text x="20" y="282" fill="currentColor">CRC → magic → SYSRESETREQ</text>
  <path d="M95 290 L95 330 L650 330 L650 90" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <path d="M365 290 L365 330" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="10" y="350" fill="currentColor">NMI·HardFault 는 UART 를 안 쓴다: .noinit 에 봉인만, 보고는 다음 부팅의 main 이.</text>
  <text x="10" y="368" fill="currentColor">세미호스팅 = 시험 하네스 채널 (진행·CHECK·종료 코드). 제품 로그 = UART0.</text>
</svg>
```

**보드 사실** (기억이 아니라 QEMU v10.2.0 소스 + `run_qemu.sh probe` 실측; 실제 CMSDK 하드웨어는 Arm CMSDK TRM 확인 권장):

| 부품 | 주소 / IRQ | 이 레슨이 기대는 동작 | 근거 |
|---|---|---|---|
| timer0 / timer1 | 0x40000000 IRQ 8 / 0x40001000 IRQ 9 | CTRL EN bit0, IRQEN bit3, INTSTATUS 0xC W1C, 25 MHz | `hw/arm/mps2.c` (irqno = 8 + i), probe PID0 = 0x22 |
| UART0 | 0x40004000, **TX = IRQ 1**, RX = IRQ 0 | TX 인터럽트는 **TXFULL 1→0 일 때만** — 빈 버퍼에서 TX_INTEN 을 켜도 안 선다 | `cmsdk-apb-uart.c:225` "Going from TXFULL set to clear triggers the tx interrupt", probe P2 |
| watchdog | 0x40008000 → **NMI** | 1차 만료 = 인터럽트, 2차 = 리셋; INTCLR 쓰기 = 지우기 + 재장전; LOCK(0x1ACCE551) | `cmsdk-apb-watchdog.c:299/302/154`, `mps2.c` "NMI", probe P4 |
| NMI 입력 | — | 상승 에지에서만 pending (레벨을 다시 보지 않음) | `armv7m_nvic.c:971` `nvic_nmi_trigger` |
| 워치독 리셋 | — | `qemu_system_reset_request` = SYSRESETREQ 와 같은 머신 리셋, RAM 유지 | `hw/watchdog/watchdog.c:50`, probe P4 boot 2 |

### Part A — 로거 한 바퀴: 접착부 네 개

- 주어진 것: `a11.ld`(FLASH 256 KB / RAM 63 KB / NOINIT 1 KB, `.img_hdr` 64 B 를 payload 바로 뒤에), startup, 레지스터 정의, 링 두 개, 로그 포매터, NMI 빵부스러기, HardFault 덤프, 시나리오 대본, 마지막 부팅의 자체 검사 52 개.
- **TODO 1** `tick_isr()` — timer0 ISR 의 일: `g_tick++`, 샘플 `{tick, sensor_value(tick)}` 을 샘플 링에 (가득 차면 버리고 `g_drops++`; ISR 은 기다리지 않는다).
- **TODO 2** `uart_tx_isr()` — TX 인터럽트의 일: 로그 링에서 한 바이트 → `DATA`, 비었으면 `tx_busy = 0`.
- **TODO 3** `log_kick()` — 쉬는 송신기 깨우기: 임계 구역 안에서 `!tx_busy` 면 첫 바이트를 main 이 직접 쓴다 (CMSDK TX IRQ 는 "다 보냈다" 에지라서).
- **TODO 4** `wdog_service()` — 킥 정책: **main 루프에서만**, 그리고 (a) 틱이 마지막 킥 뒤로 늘었고 (b) 샘플 링이 비었을 때만.

### Part B — 대본대로 망가뜨리기 (모든 주입은 결과까지 단언)

한 번의 QEMU 실행 = 5 부팅. 부팅마다 12 샘플을 로그한 뒤 다음 장애를 주입한다:

| 부팅 | 주입 | 기대 결과 (다음 부팅이 보고) |
|---|---|---|
| 1 | `main-hang` — main 이 `b .` 에 갇힘, 인터럽트는 계속 | NMI 빵부스러기: `MAIN_STUCK`, since_kick ≈ 10 틱, pc = `pc_hang` → 워치독 리셋 |
| 2 | `tick-stall` — 누군가 timer0 IRQ 를 NVIC 에서 꺼 버림, main 은 멀쩡 | `TICK_DEAD`, since_kick = 0 → 워치독 리셋 (킥 정책 (a) 덕분) |
| 3 | `bus-fault` — main 이 0x30000000 읽기 | 덤프: HardFault(FORCED), CFSR 0x8200, BFAR 0x30000000, pc = `pc_bus`, EXC_RETURN 0xFFFFFFF9 |
| 4 | `isr-fault` — timer0 ISR 안의 UDF | 덤프: CFSR 0x10000, EXC_RETURN 0xFFFFFFF1, 쌓인 xPSR 의 IPSR = 24 (IRQ 8) |
| 5 | 없음 | `DONE` → 52 검사 → `SYS_EXIT 0` |

추가 결함 빌드: `BUG_KICK_IN_ISR`(ISR 에서 킥), `BUG_UNCOND_KICK`(조건 없는 킥), 이미지 손상 3 종(payload 비트, 헤더 비트, 크기 거짓말), 그리고 `log_kick` 의 순서 버그(§5 Part B). 콜드 부팅 쓰레기·찢어진 기록은 QEMU RAM 이 0 으로 시작하므로 호스트에서(연습 2).

### Part C — 제품 문서: 메모리 표, 부하, 부팅 로그 읽기

- `run_qemu.sh mem` 의 실제 `size`/맵 출력으로 Flash/RAM 예산 표.
- `-icount` 가상 시간으로 로그 단계 부하 측정 + Cortex-M3 사이클 모델 (둘을 섞지 않기).
- `logtool.py decode` 로 WDOG/DUMP 줄을 사람 말로 (CFSR 비트, EXC_RETURN, PC 심볼화).

## 2. Problem (English)

> **Bare-metal data logger on a Cortex-M3 (QEMU `mps2-an385`).**
>
> Peripherals: CMSDK APB timer0 (`0x40000000`, IRQ 8), UART0 (`0x40004000`, TX IRQ 1 — the TX interrupt is raised only when the 1-byte TX buffer goes from full to empty), CMSDK watchdog (`0x40008000`, interrupt wired to NMI; first expiry → interrupt, second → system reset; writing `WDOGINTCLR` clears the interrupt and reloads). PCLK = 25 MHz.
>
> 1. Sample a sensor at 100 Hz from the timer ISR into an 8-entry SPSC ring (drop and count when full). The main loop formats each sample as `S <boot> <nn> t=<tick> v=<value>` into a 512-byte log ring drained by the UART TX interrupt. Implement `tick_isr()`, `uart_tx_isr()`, `log_kick()` (start an idle transmitter safely) and `wdog_service()`.
> 2. Kick policy: only from the main loop, only if the tick advanced since the last kick **and** the sample ring is drained.
> 3. Persist across resets in a `.noinit` section: a watchdog breadcrumb written in the NMI handler (tick, last-kick tick, interrupted PC, EXC_RETURN, CRC) and a HardFault dump (stacked frame, CFSR/HFSR/BFAR, EXC_RETURN, CRC; magic last). The next boot reports the reset cause and the record over UART, then invalidates it.
> 4. At boot, verify the image: a 64-byte header placed right after the last payload byte (magic, version, size, load address, CRC-32 of payload, CRC-32 of header), stamped post-link by a host tool. Refuse to start the logger on any mismatch.
>
> Expected UART after a watchdog reset caused by a stuck main loop: `WDOG diag=MAIN_STUCK boot=1 since_kick=10 pc=<pc_hang> excret=0xfffffff9`. After a fault inside the timer ISR: `DUMP ... exc=3 excret=0xfffffff1 ... cfsr=0x00010000 hfsr=0x40000000`, stacked IPSR = 24.
>
> Follow-ups: Why not kick the watchdog from the timer ISR? Why does the first byte have to be written by the main loop? What do you lose if the NMI handler kicks the dog? How would you tell "main stuck" from "tick ISR dead" after the fact? Why is the header *after* the payload here, and where would it go with a bootloader?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `a11.ld` (링커 스크립트), `mkimg.py` (헤더 도장, stdlib), `logtool.py` (기대 로그 생성·비교·해독), `expected_uart.txt` (`python3 -I logtool.py expected` 출력, 84 줄), `run_qemu.sh` (VM 빌드·실행), `demo_probe.c` (QEMU 사실 확인), `practice.c` (호스트 C).

<!-- code: a11.ld -->

실행은 Lima VM `elinux` 안에서 (Mac 홈은 VM 에서 읽기 전용 → `/tmp/BM-A11_work` 로 복사해 빌드). QEMU 프로세스는 하나, 리셋 5 번, 마지막 부팅의 `SYS_EXIT` 로 끝난다. Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A11
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 20/52, UART 0 줄, exit 1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 52/52 + UART 84 줄 일치, exit 0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C) 47/47
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh probe"      # 보드 사실 P1~P4
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh bugs"       # 킥 정책 결함 빌드 2 개
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh corrupt"    # 손상 이미지 3 종
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh mem"        # size / 맵 / 큰 심볼
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh syms"       # 라벨 주소, 세그먼트, objcopy vs mkimg
```

시작 코드 실행 결과 (실제 출력, FAIL 34 줄 중 앞 4 줄만):

```text
mkimg: payload [0x00000000, 0x00001f24) 7972 B  crc32=0x540d6579  hdr_crc32=0xa95d4be3  ver=1.1.0  hdr@file+0x11f24
== boot 1 cause=POWER_ON img=OK ver=0x01010000 payload=7972 crc=0x540d6579
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
== boot 2 cause=WATCHDOG img=OK ver=0x01010000 payload=7972 crc=0x540d6579
  … (부팅 3~8 도 똑같이: 아무것도 로그 못 하고 워치독 리셋) …
== boot 9 cause=WATCHDOG img=OK ver=0x01010000 payload=7972 crc=0x540d6579
boot-loop guard: too many resets
FAIL starter.c:500  g_scen.boots == N_BOOTS
FAIL starter.c:505  r->logged == N_SAMPLES
FAIL starter.c:507  r->kicks >= N_SAMPLES - 1u
FAIL starter.c:508  r->busy * 2u < r->total
20/52 passed
qemu exit=3
UART CHECK FAIL at line 1
  expected: BOOT 1 cause=POWER_ON
  actual:   <end of file>
starter: FAIL (qemu exit=3, uart check=1)
```

읽는 법: 이미지 검사·리셋 원인 판정·NMI 빵부스러기·부팅 루프 가드 같은 **틀은 이미 돈다**. 그런데 틱이 샘플을 안 만들고(TODO 1), 송신기가 한 번도 깨어나지 않아 UART 는 0 바이트(TODO 3), 아무도 킥하지 않아(TODO 4) 매 부팅 200 ms 만에 워치독이 문다. 가드(최대 8 부팅)가 없었다면 이 리셋 루프는 `timeout 20` 까지 돌았다.

## 4. 힌트

<details>
<summary>힌트 1 — ISR 쪽은 짧게, 기다리지 않게</summary>

`tick_isr` 는 세 줄이다: 틱 올리기, 샘플 만들기, `sring_push` 가 0 이면 `g_drops++`. 링이 가득 찼을 때 ISR 이 빈자리를 기다리면 main 을 영원히 못 돌게 한다 (main 이 비워야 자리가 나니까). `uart_tx_isr` 는 "있으면 하나 보내고, 없으면 `tx_busy = 0`" — 플래그 W1C 는 껍데기(`Uart0Tx_Handler`)가 이미 했다.

</details>

<details>
<summary>힌트 2 — 왜 main 이 첫 바이트를 써야 하나</summary>

probe P2: `TX_EN|TX_INTEN` 을 켠 직후 `INTSTATUS = 0`, NVIC pending 도 0. 바이트 하나를 쓰면 그제야 `INTSTATUS = 1`. 즉 이 UART 의 TX 인터럽트는 "버퍼가 비어 있음(레벨)" 이 아니라 "방금 다 보냈음(에지)" 이다 (STM32 의 TXE 는 레벨 — BM-M04). 송신기가 쉬고 있으면 아무도 다음 인터럽트를 만들어 주지 않는다 → `log_kick()` 이 첫 바이트를 쓴다.

</details>

<details>
<summary>힌트 3 — 꼬리(tail)를 언제 옮기나</summary>

QEMU 에선 `DATA` 쓰기가 끝나는 순간 TX 가 완료되고 IRQ 1 이 pending 이 된다. 마스크가 없으면 **다음 명령 경계에서 바로** TX ISR 이 돈다. 그 ISR 은 `tx_tail` 을 본다 — 아직 안 옮겼다면? 임계 구역(`irq_save`/`irq_restore`) 안에서 `tx_busy = 1; tx_tail = t + 1;` 을 **DATA 쓰기 전에** 해 두면 순서 걱정이 없다.

</details>

<details>
<summary>힌트 4 — 킥 조건 두 개가 각각 잡는 장애</summary>

"main 루프가 돈다" 만 확인하면 틱이 죽어도(부팅 2) 개는 배부르다. "틱이 돈다" 만 확인하면(ISR 킥) main 이 죽어도(부팅 1) 배부르다. 그래서 (a) `g_tick != g_kick_tick` (틱이 살아 있다) **그리고** main 루프 안에서 부른다(main 이 살아 있다), (b) `s_head == s_tail` (main 이 따라잡고 있다). 킥한 뒤 `g_kick_tick = t` — NMI 빵부스러기가 since_kick 을 계산하는 데도 쓴다.

</details>

## 5. 풀이

### Part A — 보드부터 재고, 그 위에 접착부

**1) 탐침 (`run_qemu.sh probe`, 실제 출력 발췌).** 로거를 짜기 전에 기대는 동작을 하나씩 확인했다:

```text
P1 PID0 timer0   = 0x00000022
P1 PID0 UART0    = 0x00000021
P1 PID0 watchdog = 0x00000024
P2 after TX_INTEN on empty buffer: INTSTATUS=0x00000000
P2                                  NVIC ISPR0=0x00000000
P2 after 1 byte written: STATE=0x00000000
P2                       INTSTATUS=0x00000001
P2                       NVIC ISPR0=0x00000002
P2 after INTSTATUS W1C:  INTSTATUS=0x00000000
P2                       NVIC ISPR0 (latched?)=0x00000002
P2 5 bytes with IRQ1 enabled -> TX ISR count=0x00000005
P3 50 timer0 ISRs took timer1 counts = 0x001312d4
P4 locked, tried CONTROL=0 -> CONTROL = 0x00000003
P4 25 ms with kicks every 2 ms: NMI count = 0x00000000
== boot 2 (after watchdog) ==
P4 .noinit survived: nmi_count=0x00000001
P4 timer1 down-count from last kick to NMI  = 0x0003d092
P4 NMI stacked pc (hang loop?) = 0x00000406
P4 &probe_hang                 = 0x00000406
P4 WDOGCONTROL after reset = 0x00000000
```

읽을 것: TX 인터럽트는 ISPR0 **bit 1** (= IRQ 1) 에 서고, 주변장치 플래그를 W1C 해도 **NVIC pending 은 래치된 채** 남는다 → `uart_init()` 이 `NVIC_ICPR0` 로 따로 지운다. 바이트 5 개 = ISR 5 번 (QEMU 에선 송신이 즉시 끝나므로 "바이트당 인터럽트 1 번"). 50 틱 = 0x1312d4 = 1,250,004 카운트 ≈ 50 × 25,000. 워치독은 잠그면 끌 수 없고, 마지막 킥에서 NMI 까지 0x3d092 = 250,002 카운트 = LOAD 250,000 (10 ms), 그 뒤 리셋, `.noinit` 살아남음, 주변장치는 리셋값.

**2) 이미지 헤더는 왜 payload "뒤" 에 있나.** Cortex-M 은 리셋 때 주소 0 에서 `[SP, Reset_Handler]` 를 읽는다. BM-A04 형식처럼 헤더를 슬롯 맨 앞에 두면 그 자리를 부트로더가 차지해야 하고(MCUboot 방식 — 앱은 슬롯+헤더 크기에 링크, 점프는 BM-A03), 이 레슨엔 부트로더가 없다. 그래서 같은 64 B 구조를 **payload 의 마지막 바이트 바로 뒤**에 둔다: payload = `[0, _img_hdr)` = 벡터 테이블 + `.text/.rodata` + `.data` 초기값(LMA). 헤더는 payload 에 포함되지 않으니 "자기 CRC 를 자기 안에" 문제가 없다. 링커 `ASSERT(_img_hdr == LOADADDR(.data) + SIZEOF(.data))` 가 이 배치를 강제한다.

빌드 순서: 링크(헤더 자리는 0) → `mkimg.py` 가 ELF 의 PT_LOAD 세그먼트를 **물리 주소(LMA)** 로 펼쳐 payload 를 만들고 zlib CRC-32 → 헤더 64 B 를 ELF 파일 안 그 자리에 덮어씀. `run_qemu.sh syms` 가 이 "평평한 이미지" 가 `objcopy -O binary` 와 바이트 단위로 같음을 확인한다 (실제 출력):

```text
  LOAD           0x010000 0x00000000 0x00000000 0x0220c 0x0220c R E 0x10000
  LOAD           0x00fc00 0x2000fc00 0x2000fc00 0x00000 0x00284 RW  0x10000
  [ 4] .img_hdr          PROGBITS        000021cc 0121cc 000040 00   A  0   0  4
  [ 6] .noinit           NOBITS          2000fc00 02fc00 000284 00  WA  0   0  4
8716 mkimg.bin
8716 objcopy.bin
identical
--- header bytes (last 64 B) ---
 474d4942 00400001 01010000 000021cc
 00000000 00000000 a5e1b7f9 00000000
 00000000 00000000 00000000 00000000
 00000000 00000000 00000000 31bca35e
```

`magic=0x474D4942`, `hdr_version=1 / hdr_size=0x40`, `img_version=0x01010000` (1.1.0), `payload_size=0x21cc` (8652), `load_addr=0`, `flags=0` (SHA 없음), `payload_crc32=0xa5e1b7f9`, … `hdr_crc32=0x31bca35e`. QEMU `-kernel` 은 리셋마다 FileSiz > 0 세그먼트를 다시 올리므로(BM-M13 실측) 도장 찍힌 헤더도 매 부팅 그대로다. `img_verify` 의 순서 — magic → 버전 → **헤더 CRC** → 크기 → 주소 → payload CRC — 는 "헤더를 믿을 수 있게 된 뒤에야 그 안의 크기로 메모리를 읽는다" 는 원칙이다 (BM-A04).

**3) 한 틱의 일생.** TODO 네 개가 어디서 만나는지:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">한 틱 (10 ms) 안의 사건 순서 — QEMU: UART 송신은 즉시 끝남</text>
  <line x1="10" y1="60" x2="730" y2="60" stroke="currentColor"/>
  <text x="10" y="52" fill="currentColor">Handler</text>
  <line x1="10" y1="150" x2="730" y2="150" stroke="currentColor"/>
  <text x="10" y="142" fill="currentColor">Thread (main)</text>
  <rect x="70" y="66" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <text x="76" y="82" fill="currentColor">timer0 ISR</text>
  <text x="76" y="96" fill="currentColor">W1C, TODO 1</text>
  <rect x="160" y="156" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="166" y="172" fill="currentColor">WFI 깸 → pop 샘플</text>
  <text x="166" y="186" fill="currentColor">→ 줄 22 B 를 로그 링에</text>
  <rect x="320" y="156" width="100" height="34" class="f-warn" stroke="currentColor"/>
  <text x="326" y="172" fill="currentColor">TODO 3 kick:</text>
  <text x="326" y="186" fill="currentColor">cpsid, DATA='S'</text>
  <rect x="430" y="66" width="150" height="34" class="f-acc" stroke="currentColor"/>
  <text x="436" y="82" fill="currentColor">TX ISR × 21 (꼬리물기)</text>
  <text x="436" y="96" fill="currentColor">TODO 2, 마지막 busy=0</text>
  <path d="M420 173 L430 173 L430 100" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="330" y="124" fill="currentColor">cpsie → pending IRQ 1</text>
  <rect x="590" y="156" width="70" height="34" class="f-warn" stroke="currentColor"/>
  <text x="596" y="172" fill="currentColor">TODO 4</text>
  <text x="596" y="186" fill="currentColor">kick?</text>
  <rect x="668" y="156" width="62" height="34" class="f-mut" stroke="currentColor"/>
  <text x="674" y="177" fill="currentColor">WFI …</text>
  <text x="20" y="226" fill="currentColor">kick 조건: 틱 증가 (ISR 생존) ∧ 샘플 링 비었음 (따라잡음) ∧ main 루프 안 (main 생존)</text>
  <text x="20" y="246" fill="currentColor">잠들기: cpsid i → 링 비었나? → WFI → (pending 이면 PRIMASK=1 이어도 깸) → cpsie i → 그 순간 ISR 실행</text>
  <text x="20" y="266" fill="currentColor">실측 (icount): 로그 단계 12 틱 동안 busy 5,974 / 3,000,489 timer1 카운트 → 틱당 약 1,244 명령 (§Part C)</text>
  <text x="20" y="286" fill="currentColor">실제 115200 baud 라면 21 B 줄 = 약 1.8 ms 동안 TX ISR 이 21 번 흩어져 온다 — 순서 규칙은 같다</text>
</svg>
```

`log_kick` 이 쓰는 첫 바이트는 **main 이 소비자 역할**을 잠깐 맡는 순간이다. SPSC 링의 "소비자는 하나" 규칙은 `tx_busy == 0` (= TX ISR 이 돌 일이 없음) **그리고** PRIMASK=1 (= 지금 끼어들 수 없음) 두 조건으로 지켜진다. 잠들기 직전의 `cpsid i → 확인 → WFI → cpsie i` 는 "확인과 잠들기 사이에 온 인터럽트를 놓치는" 고전 경쟁을 막는다 (WFI 는 PRIMASK 와 상관없이 pending 이면 깬다 — ARMv7-M ARM 규칙, 확인 권장; QEMU 에서 그대로 동작함을 이 실행이 보여 준다).

**4) 정답 실행** (`run_qemu.sh solution`, 세미호스팅 채널 실제 출력):

```text
== boot 1 cause=POWER_ON img=OK ver=0x01010000 payload=8652 crc=0xa5e1b7f9
  logged=12 sum=24677 busy/total=5974/3000489 tx_bytes=337 -> stage main-hang
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
== boot 2 cause=WATCHDOG img=OK ver=0x01010000 payload=8652 crc=0xa5e1b7f9
  logged=12 sum=24677 busy/total=5974/3000489 tx_bytes=412 -> stage tick-stall
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
== boot 3 cause=WATCHDOG img=OK ver=0x01010000 payload=8652 crc=0xa5e1b7f9
  logged=12 sum=24677 busy/total=5974/3000489 tx_bytes=409 -> stage bus-fault
  [hardfault] dump sealed in .noinit, SYSRESETREQ
== boot 4 cause=FAULT img=OK ver=0x01010000 payload=8652 crc=0xa5e1b7f9
  logged=12 sum=24677 busy/total=5974/3000489 tx_bytes=481 -> stage isr-fault
  [hardfault] dump sealed in .noinit, SYSRESETREQ
== boot 5 cause=FAULT img=OK ver=0x01010000 payload=8652 crc=0xa5e1b7f9
  logged=12 sum=24677 busy/total=5974/3000489 tx_bytes=477 -> stage DONE
== BM-A11 summary ==
boot cause     img logged sum   drops kicks tx_bytes busy/total(timer1 counts)
1    POWER_ON OK 12 24677 0 12 337 5974/3000489
2    WATCHDOG OK 12 24677 0 12 412 5974/3000489
3    WATCHDOG OK 12 24677 0 12 409 5974/3000489
4    FAULT OK 12 24677 0 12 481 5974/3000489
5    FAULT OK 12 24677 0 12 477 5974/3000489
52/52 passed
qemu exit=0
UART CHECK PASS: 84 lines match expected_uart.txt
solution: ALL PASS
```

UART0 캡처 (84 줄 중 부팅 1 끝 ~ 부팅 2 앞부분, 그리고 부팅 4 머리):

```text
S 1 12 t=0012 v=2103
SUM 1 n=12 sum=24677 drops=0
INJECT main-hang
BOOT 2 cause=WATCHDOG
IMG OK ver=1.1.0
WDOG diag=MAIN_STUCK boot=1 since_kick=10 pc=0x0000160c excret=0xfffffff9
S 2 01 t=0001 v=2011
…
BOOT 4 cause=FAULT
IMG OK ver=1.1.0
DUMP v=1 boot=3 exc=3 excret=0xfffffff9 pc=0x000001ec lr=0x000015f5 xpsr=0x81000000 cfsr=0x00008200 hfsr=0x40000000 bfar=0x30000000 crc=0xad93d5f6
```

기대 로그 `expected_uart.txt` 는 펌웨어 코드를 쓰지 않고 `logtool.py` 가 센서 식을 **따로** 다시 구현해 만든다 (첫 12 틱 합 24677 은 펌웨어·파이썬·`practice.c` 세 곳이 독립적으로 같은 값). 주소처럼 빌드마다 바뀌는 값은 `{*}` 이고, 그 정확한 값은 펌웨어의 CHECK 가 링크 라벨(`pc_hang`, `pc_bus`, `pc_isr`)과 직접 비교한다.

### Part B — 장애 대본, 그리고 두 가지 "같은 리셋, 다른 원인"

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">워치독 2 단계 (LOAD 100 ms) — 장애별로 누가 킥하나</text>
  <text x="10" y="44" fill="currentColor">시간 →  킥(INTCLR) 은 매 틱 (10 ms)</text>
  <line x1="120" y1="70" x2="720" y2="70" stroke="currentColor"/>
  <path d="M140 70 l0 -10 M160 70 l0 -10 M180 70 l0 -10 M200 70 l0 -10" stroke="currentColor"/>
  <rect x="210" y="56" width="10" height="28" class="f-warn" stroke="currentColor"/>
  <text x="200" y="100" fill="currentColor">장애 시작</text>
  <rect x="420" y="56" width="12" height="28" class="f-acc" stroke="currentColor"/>
  <text x="380" y="100" fill="currentColor">+100 ms: NMI (crumb)</text>
  <rect x="630" y="56" width="12" height="28" class="f-acc" stroke="currentColor"/>
  <text x="590" y="100" fill="currentColor">+200 ms: 리셋</text>
  <text x="10" y="74" fill="currentColor">카운터</text>
  <rect x="10" y="120" width="720" height="194" rx="6" fill="none" stroke="currentColor"/>
  <text x="20" y="140" fill="currentColor" font-weight="bold">정책 \ 장애</text>
  <text x="250" y="140" fill="currentColor" font-weight="bold">main 멈춤 (부팅 1)</text>
  <text x="420" y="140" fill="currentColor" font-weight="bold">틱 ISR 죽음 (부팅 2)</text>
  <text x="590" y="140" fill="currentColor" font-weight="bold">main 느림 (연습 4)</text>
  <text x="20" y="168" fill="currentColor">ISR 에서 킥 (BUG_KICK_IN_ISR)</text>
  <rect x="245" y="154" width="150" height="20" class="f-warn"/><text x="250" y="168" fill="currentColor">못 잡음 (실측 FAIL)</text>
  <rect x="415" y="154" width="150" height="20" class="f-ok"/><text x="420" y="168" fill="currentColor">리셋</text>
  <rect x="585" y="154" width="135" height="20" class="f-warn"/><text x="590" y="168" fill="currentColor">못 잡음</text>
  <text x="20" y="200" fill="currentColor">main 에서 무조건 (BUG_UNCOND)</text>
  <rect x="245" y="186" width="150" height="20" class="f-ok"/><text x="250" y="200" fill="currentColor">리셋</text>
  <rect x="415" y="186" width="150" height="20" class="f-warn"/><text x="420" y="200" fill="currentColor">못 잡음 (NMI 만 3 번)</text>
  <rect x="585" y="186" width="135" height="20" class="f-warn"/><text x="590" y="200" fill="currentColor">못 잡음</text>
  <text x="20" y="232" fill="currentColor">main + 틱 증가 + 링 비움 (정답)</text>
  <rect x="245" y="218" width="150" height="20" class="f-ok"/><text x="250" y="232" fill="currentColor">리셋, since_kick=10</text>
  <rect x="415" y="218" width="150" height="20" class="f-ok"/><text x="420" y="232" fill="currentColor">리셋, since_kick=0</text>
  <rect x="585" y="218" width="135" height="20" class="f-ok"/><text x="590" y="232" fill="currentColor">리셋</text>
  <text x="20" y="264" fill="currentColor">빵부스러기로 구별: since_kick = tick − kick_tick. 틱은 늘었는데 킥이 없다 → main 멈춤 (pc = 멈춘 곳).</text>
  <text x="20" y="282" fill="currentColor">틱이 하나도 안 늘었다 (0) → 틱 ISR/타이머가 죽음; 이때 NMI 의 pc 는 main 의 WFI 바로 뒤 (run_loop+0x1e8).</text>
  <text x="20" y="300" fill="currentColor">NMI 핸들러는 절대 킥하지 않는다 — 킥하면 "경고" 가 "무한 연기" 가 된다 (CMSDK: INTCLR = 재장전).</text>
</svg>
```

**결함 빌드 실측** (`run_qemu.sh bugs`):

```text
=== solution.c -DBUG_KICK_IN_ISR
== boot 1 cause=POWER_ON img=OK ver=0x01010000 payload=8692 crc=0x7483ec1b
FAIL: injected failure was not caught within the deadline (stage 1)
qemu exit=1
UART lines: 16, last: INJECT main-hang
=== solution.c -DBUG_UNCOND_KICK
== boot 1 cause=POWER_ON img=OK ver=0x01010000 payload=8640 crc=0xefdd2dd5
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
== boot 2 cause=WATCHDOG img=OK ver=0x01010000 payload=8640 crc=0xefdd2dd5
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
  [nmi] watchdog early warning: breadcrumb sealed, not kicking
FAIL: 3 watchdog NMIs without a reset (dog kept being fed while sick)
qemu exit=1
UART lines: 33, last: INJECT tick-stall
```

첫 번째: ISR 이 킥하니 main 이 `b .` 에 갇혀도 개는 배부르다 → 펌웨어의 600 ms 마감(HANG_DEADLINE_TICKS)이 대신 FAIL 을 낸다. 두 번째: main 멈춤(부팅 1)은 잡지만, 틱이 죽은 부팅 2 에선 **NMI 가 main 을 WFI 에서 깨우고 → main 이 무조건 킥 → 경고가 꺼지고 재장전** — 100 ms 마다 NMI 만 반복되고 리셋은 영영 안 온다. 현장에선 "장비는 살아 있는데 로그가 멈춘" 가장 비싼 고장이다.

**빵부스러기 해독** (`logtool.py decode`, 실제 출력):

```text
WDOG diag=MAIN_STUCK boot=1 since_kick=10 pc=0x0000160c excret=0xfffffff9
  -> watchdog reset after boot 1: main loop stuck (ticks still running); ticks since last kick = 10; NMI interrupted Thread mode, MSP at pc_hang+0x0
WDOG diag=TICK_DEAD boot=2 since_kick=0 pc=0x00000740 excret=0xfffffff9
  -> watchdog reset after boot 2: tick ISR dead (main alive); ticks since last kick = 0; NMI interrupted Thread mode, MSP at run_loop+0x1e8
```

`objdump` 로 확인: `73e: bf30 wfi` / `740: 6849 ldr r1, [r1, #4]` — NMI 가 WFI 를 깨웠으니 쌓인 PC 는 WFI **다음** 명령이다. `160c: e7fe b.n 160c <pc_hang>` — 멈춘 그 명령.

**fault 덤프 해독** (실제 출력):

```text
DUMP v=1 boot=3 exc=3 excret=0xfffffff9 pc=0x000001ec lr=0x000015f5 xpsr=0x81000000 cfsr=0x00008200 hfsr=0x40000000 bfar=0x30000000 crc=0xad93d5f6
  -> exc 3, CFSR[PRECISERR BFARVALID] HFSR[FORCED] BFAR=0x30000000
     Thread mode, MSP; faulting context Thread (IPSR=0)
     pc pc_bus+0x0   lr main+0x449
DUMP v=1 boot=4 exc=3 excret=0xfffffff1 pc=0x00000270 lr=0xfffffff9 xpsr=0x01000018 cfsr=0x00010000 hfsr=0x40000000 bfar=0x00000000 crc=0x680c2fc0
  -> exc 3, CFSR[UNDEFINSTR] HFSR[FORCED] BFAR=0x00000000
     Handler mode, MSP (fault inside an ISR); faulting context IRQ 8 (IPSR=24)
     pc pc_isr+0x0   lr EXC_RETURN(Thread mode, MSP)
```

이 로거는 BusFault/UsageFault 핸들러를 켜지 않았으므로(SHCSR 0) 둘 다 HardFault 로 **승격(FORCED)** 되고, 원래 원인은 CFSR 에 남는다 (BM-M13). 두 번째 덤프의 `lr=0xfffffff9` 는 timer0 ISR 이 받은 EXC_RETURN — ISR 이 main(Thread, MSP) 을 선점한 상태에서 fault 가 났다는 두 겹의 증거다 (`excret=0xfffffff1` = fault 는 Handler 모드에서). 전체 CFSR 해독·스택 역추적은 ARM-M12, 레지스터 r4–r11 까지 담는 덤프 형식과 fault 스택 전환은 BM-A09 의 몫이다.

**이미지 손상** (`run_qemu.sh corrupt`, 헤더 도장을 찍은 **뒤** 비트를 뒤집는다 = 전송 오류·비트 썩음):

```text
=== corrupt: payload bit 0x1000 (= byte 0x200, in .text)
== boot 1 cause=POWER_ON img=BAD_CRC ver=0x01010000 payload=8652 crc=0xa5e1b7f9
qemu exit=3
UART: BOOT 1 cause=POWER_ON|IMG BAD code=BAD_CRC -> SAFE MODE, logger not started|
=== corrupt: header bit 0x60 (= byte 12 = payload_size LSB, no CRC fix-up)
== boot 1 cause=POWER_ON img=BAD_HDR_CRC ver=0x01010000 payload=8653 crc=0xa5e1b7f9
qemu exit=3
UART: BOOT 1 cause=POWER_ON|IMG BAD code=BAD_HDR_CRC -> SAFE MODE, logger not started|
=== corrupt: payload_size lies (+4, header CRC recomputed)
== boot 1 cause=POWER_ON img=BAD_SIZE ver=0x01010000 payload=8656 crc=0xa5e1b7f9
qemu exit=3
UART: BOOT 1 cause=POWER_ON|IMG BAD code=BAD_SIZE -> SAFE MODE, logger not started|
```

세 번째가 중요하다: 헤더 CRC 까지 다시 맞춘 "정직하게 만든 거짓 헤더" 는 CRC 로는 안 잡히고 **크기 규칙**(payload 끝 == 헤더 위치)이 잡는다. 의도적인 위조(정상 CRC 를 다시 계산한 악성 이미지)는 CRC 로 막을 수 없다 — 그건 서명(SEC 트랙)의 일이다. 그리고 "자기 자신을 검사하는 펌웨어" 의 한계: 손상된 바이트가 검사 코드 자체에 있으면 검사가 돌지도 못한다 → 제품에선 별도 부트로더(BM-A03)가 검사한다.

**`log_kick` 순서 버그 — QEMU 에서 결정적으로 재현.** 비공개 변종(임계 구역 제거 + `DATA` 먼저, `tx_tail` 나중)을 돌린 실제 UART 첫 줄:

```text
BBOOT OOOT 1OOT 1 cause=TT 1 cause=POWER_ON  1 cause=POWER_ON
UART CHECK FAIL at line 1
UART lines: expected 84, actual 14253
```

`DATA` 쓰기 → QEMU 에서 즉시 TX 완료 → IRQ 1 → TX ISR 이 아직 안 옮겨진 `tx_tail` 의 같은 바이트('B')를 또 보내고 링 끝까지 진행 → main 이 돌아와 `tx_tail = t + 1` 로 **ISR 의 진행을 되감는다** → 다음 kick 마다 "OOT" 가 다시 나온다. 연습 3 의 호스트 모델이 똑같이 `BBOOTOOOT1` 을 낸다. 같은 변종이라도 임계 구역을 그대로 두면(꼬리 순서만 바꿈) QEMU 에서 통과했다 — PRIMASK 가 그 창을 닫기 때문이다. 실제 115200 baud 하드웨어에선 완료가 ~87 µs 뒤라 이 버그가 **드물게만** 터진다; QEMU 의 "즉시 완료" 가 오히려 좋은 스트레스 테스트가 된 경우다.

### Part C — 제품 문서

**메모리 예산** (`run_qemu.sh mem`, 실제 출력):

```text
   text	   data	    bss	    dec	    hex	filename
   8716	      0	   1300	  10016	   2720	solution.elf
.isr_vector     0x00000000       0xc0
.text           0x000000c0     0x210c
.data           0x20000000        0x0 load address 0x000021cc
.img_hdr        0x000021cc       0x40
.bss            0x20000000      0x290
.noinit         0x2000fc00      0x284
00000798 000007f4 t final_report
000011ac 00000680 T main
00000558 00000240 t run_loop
2000fc00 00000214 B g_scen
20000008 00000200 b tx_ring
00001044 00000104 T fault_capture
000001f0 000000e4 T Timer0_Handler
```

| 영역 | 내용 | 바이트 | 예산 대비 |
|---|---|---|---|
| Flash | 벡터 192 + .text/.rodata 8,460 + .data LMA 0 + 헤더 64 | 8,716 | 256 KB 의 3.3 % |
| RAM `.bss` | tx_ring 512 + 샘플 링 64 + 상태 | 656 | 63 KB 의 1.0 % |
| RAM 스택 | `_ebss` 0x20000290 ~ `_estack` 0x2000FC00 (링크 최소 2 KB, ASSERT) | 64,368 가용 | 사용량 측정 = BM-A07 |
| `.noinit` | g_scen 532 + g_crumb 40 + g_dump 72 | 644 | 1 KB 의 63 % |

`size` 의 `bss 1300` 은 `.bss` 656 + `.noinit` 644 를 합친 것이다 (둘 다 NOLOAD 할당 섹션). 큰 심볼 1 위 `final_report` 2,036 B 는 **시험 하네스**다 — 제품 빌드에선 빠진다. `.data` 가 0 인 것은 초기값 있는 전역을 두지 않은 결과(모든 상태를 0 에서 시작) — Flash·RAM 이중 비용이 없다 (BM-B05).

**CPU 부하.** `-icount shift=4,sleep=off` 에서 QEMU 가상 시간은 "실행한 명령 수 × 16 ns" 이고, WFI 동안엔 다음 타이머 마감으로 바로 건너뛴다. timer1 1 카운트 = 40 ns 이므로 1 카운트 = 2.5 명령. 로그 단계 12 틱: busy 5,974 / total 3,000,489 카운트 = **1,991 ppm (0.20 %)**, 약 14,935 명령 = **틱당 ~1,244 명령** (연습 5 가 이 산수를 검사). 이것은 **명령 수**지 Cortex-M3 사이클이 아니다. 사이클 모델로 옮기면 (가정, 확인 권장): CPI ≈ 1.3 → ~1,620 사이클 + 예외 진입·복귀 ~24 사이클 × 틱당 인터럽트 22 번(timer 1 + TX 21) ≈ 530 → ~2,200 사이클 / 틱; QEMU SYSCLK 25 MHz (실제 MPS2 AN385 도 25 MHz 로 알려짐 — 확인 권장) 에서 틱 = 250,000 사이클 → **≈ 0.9 %**. 대조로 icount 없이 돌리면 같은 펌웨어가 busy `24155/3052472`, `25745/3003347` 처럼 실행마다 다르고 1.04 s 가 걸린다 (icount: 0.059 s, 매번 같은 숫자). 진짜 사이클은 보드에서 DWT CYCCNT 로 재야 한다 (BM-A10; QEMU 의 CYCCNT 는 0).

**병목은 CPU 가 아니라 링크다.** 115200 8N1 = 11,520 B/s, 100 Hz × 22 B = 2,200 B/s = 링크의 19 %. 이 줄 형식이면 ~523 Hz 가 상한 — 그 전에 로그 링(512 B)이 버스트를 흡수하는지가 설계 질문이다 (연습 5).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

헤더 도장 도구 (호스트, stdlib 만):

<!-- code: mkimg.py -->

## 7. 자주 하는 실수

**① ISR 에서 킥.** "타이머가 주기적이니 거기서 킥하면 편하다" → main 이 죽어도 개는 배부르다 (`BUG_KICK_IN_ISR` 실측: 600 ms 마감 FAIL). 고침: 킥은 main 루프에서, 틱이 살아 있다는 증거와 함께.

**② 조건 없는 main 킥.** 틱 ISR/타이머가 죽어도 main 루프는 돌기 때문에 개가 배부르다. 이 로거에선 더 교활하게, **NMI 자체가 main 을 WFI 에서 깨워** 킥을 유발했다 (`BUG_UNCOND_KICK`: NMI 3 번, 리셋 없음). 고침: `g_tick != g_kick_tick` 조건.

**③ NMI(워치독 경고)에서 킥하거나 INTCLR 쓰기.** CMSDK 에선 INTCLR = 인터럽트 지우기 + **재장전**이다. 경고 핸들러가 "지운다" 고 쓰는 순간 리셋을 연기한다. 고침: 빵부스러기만 봉인하고 그냥 복귀.

**④ TX_INTEN 만 켜고 기다리기.** CMSDK TX 인터럽트는 "다 보냄" 에지 → 쉬는 송신기는 아무도 안 깨운다 (시작 코드: UART 0 바이트). 고침: `log_kick()` 이 첫 바이트를 쓴다.

```c
uint32_t pm = irq_save();
if (!tx_busy && tx_tail != tx_head) { uint32_t t = tx_tail; tx_busy = 1u; tx_tail = t + 1u;
                                      U0_DATA = tx_ring[t & (TXRING_N - 1u)]; }
irq_restore(pm);
```

**⑤ `DATA` 먼저, 꼬리 나중 (임계 구역 없이).** QEMU 실측: `BBOOT OOOT 1…`, 84 줄 대신 14,253 줄. 고침: 위 코드처럼 임계 구역 + 꼬리 먼저.

**⑥ 주변장치 플래그만 지우고 NVIC pending 은 안 지움.** probe P2: W1C 뒤에도 ISPR0 bit1 이 남는다 → `NVIC_ISER0` 을 켜는 순간 가짜 인터럽트 1 번. 고침: enable 전에 `NVIC_ICPR0`.

**⑦ 구조체 대입을 `-nostdlib` 펌웨어에.** `g_scen.dumps[n] = g_dump;` → GCC 가 `memcpy` 호출을 만들어 링크 에러 (`undefined reference to 'memcpy'` — 이 레슨 작성 중 실제로 났다). 고침: 워드 복사 함수, 또는 `-fno-tree-loop-distribute-patterns` 만으론 부족하다는 것을 기억.

## 8. 면접 Q&A

<details>
<summary>Q1. 워치독을 타이머 ISR 에서 킥하면 왜 안 되나? 그럼 어디서, 어떤 조건으로?</summary>

ISR 은 main 이 무한 루프·데드락에 빠져도 계속 돈다 → 워치독이 감시해야 할 대상(제품의 일을 하는 루프)이 아니라 가장 살아남기 쉬운 부분을 감시하게 된다. 킥은 "시스템이 일을 했다" 는 증거가 모였을 때 한 곳에서: 이 로거는 main 루프 안에서 (a) 틱 증가(생산자 생존) (b) 샘플 링 비움(소비자가 따라잡음). 멀티태스크면 각 태스크가 체크인 비트를 세우고 감독 태스크가 전부 모였을 때만 킥 (TEST-B09 다음 단계).

</details>

<details>
<summary>Q2. 같은 "워치독 리셋" 두 번 — 하나는 main 멈춤, 하나는 틱 정지. 현장 로그만 보고 어떻게 구별했나?</summary>

워치독 1 차 만료 인터럽트(NMI)에서 빵부스러기를 봉인한다: 현재 틱, 마지막 킥 때의 틱, 쌓인 PC, EXC_RETURN, CRC. since_kick = tick − kick_tick 이 10 이면 틱은 돌았는데 킥이 없었다 → main 이 멈춤, PC 가 멈춘 곳(`pc_hang`). 0 이면 틱 자체가 멈춤, PC 는 main 의 WFI 다음(`run_loop+0x1e8`) — main 은 살아 있었다. 2 단계 워치독(경고 → 리셋)이 없는 IWDG 류라면, 주기적으로 갱신하는 "마지막 상태" 기록을 .noinit 에 두고 리셋 원인 레지스터(STM32 RCC_CSR IWDGRSTF, BM-M10)와 조합한다.

</details>

<details>
<summary>Q3. UART TX 인터럽트로 로그를 보낼 때 "첫 바이트" 문제는 무엇이고, 칩마다 왜 다른가?</summary>

STM32 USART 의 TXE 는 레벨이라 TXEIE 를 켜면 즉시 인터럽트가 오고, 링이 비면 TXEIE 를 꺼야 한다(안 끄면 인터럽트 폭풍, BM-M04). CMSDK UART 는 TXFULL 1→0 에지에서만 INTSTATUS.TX 를 세운다 (QEMU `cmsdk-apb-uart.c:225`, probe P2 확인) → 쉬는 송신기는 main 이 첫 바이트를 써서 깨워야 한다. 그래서 `tx_busy` 같은 "송신 중" 상태가 필요하고, 그 상태 전이(ISR 의 busy=0 ↔ main 의 busy=1)가 경쟁 지점이다. 데이터시트에서 "인터럽트가 레벨인가 에지인가, 언제 서는가" 를 제일 먼저 확인하는 이유.

</details>

<details>
<summary>Q4. HardFault 핸들러에서 UART 로 바로 덤프를 찍지 않고 .noinit 에 쓰고 리셋하는 이유는?</summary>

fault 시점에 UART 드라이버·로그 링·인터럽트 상태가 멀쩡하다는 보장이 없다 (링 인덱스가 깨졌을 수도, TX ISR 이 fault 원인일 수도, 우선순위상 TX ISR 이 못 돈다). 최소한의 코드로 RAM 에 봉인(내용 → CRC → DSB → magic)하고 리셋하면, 다음 부팅에서 **검증된 깨끗한 드라이버**로 보고한다. 리셋해도 RAM 은 남고(QEMU·대부분 MCU 의 시스템 리셋), `.noinit` 은 startup 이 건드리지 않는다. 전원 차단·BOR 이면 RAM 이 날아가므로 그런 기록은 플래시 로그(BM-A02 모델)로.

</details>

<details>
<summary>Q5. 이미지 헤더를 앱 안에 넣고 자기 자신을 검사하는 설계의 한계는? 제품이라면 어떻게 바꾸겠나? (staff)</summary>

한계 셋: (1) 손상이 검사 코드나 벡터 테이블에 있으면 검사가 돌기도 전에 죽는다, (2) 실패해도 갈 곳이 없다 (SAFE MODE 루프뿐), (3) CRC 는 우연한 손상만 잡고 의도적 위조는 못 막는다. 바꾸면: 작고 변하지 않는 부트로더(ROM 이나 쓰기 보호된 첫 섹터)가 헤더를 **슬롯 맨 앞**에서 읽고(BM-A04 형식, 앱은 슬롯+0x200 에 링크), SHA-256 + 서명 검증(SEC), 버전 하한(anti-rollback, OTP/eFuse), A/B 슬롯과 test/confirm(BM-A06, PLAT-M07) 으로 "새 이미지가 스스로 확인하지 않으면 되돌림", 그리고 점프 전 주변장치 정리(BM-A03). 이 레슨의 헤더 뒤 배치는 부트로더가 없는 단계의 타협이고, 같은 64 B 구조라 부트로더 단계로 옮겨 가기 쉽다.

</details>

<details>
<summary>Q6. 이 로거의 CPU 부하가 0.2 % 라고 했는데, 그 숫자를 믿어도 되나?</summary>

절반만. QEMU icount 에선 가상 시간 = 명령 수 × 16 ns 라서 "로그 단계에서 실행한 명령 수" 는 정확히 결정적이다 (틱당 ~1,244). 하지만 Cortex-M3 의 CPI, 플래시 대기 상태, 예외 진입 12 사이클·복귀, 실제 UART 가 바이트마다 87 µs 걸리는 것은 없다. 그래서 명령 수 × CPI 가정 + 예외 오버헤드 모델로 ~0.9 % 를 추정하고, 보드에서 DWT CYCCNT(BM-A10) 나 GPIO 토글 + 로직 분석기로 확인한다. 면접 답은 "측정값과 모델을 구분하고, 어느 쪽이 몇 배 틀릴 수 있는지" 까지.

</details>

<details>
<summary>Q7. 샘플링을 1 kHz 로 올리고 줄 형식도 늘리라면 어디가 먼저 깨지나? 어떻게 바꾸겠나? (staff)</summary>

CPU 는 여유(틱당 수천 사이클)가 있지만 **UART 링크**가 먼저 깨진다: 1 kHz × 22 B = 22 KB/s > 115200 의 11.5 KB/s. 순서대로: (1) 텍스트 대신 바이너리 프레임(틱 델타 + 12 비트 값 = 3~4 B, COBS/CRC) 로 5~7 배 절약, (2) 배치(N 샘플을 한 프레임), (3) 링 크기를 "최악 버스트 × 지연" 으로 재계산하고 오버플로 정책(오래된 것 버림 vs 새것 버림)과 드롭 카운터를 로그에 노출, (4) 더 빠른 링크(921600, USB CDC) 또는 로컬 저장(플래시 로그 — BM-A02 의 append 구조), (5) 샘플링은 타이머 트리거 ADC + DMA(BM-M09) 로 ISR 을 블록당 한 번으로. 킥 정책의 "링 비움" 조건도 "백로그 < 임계값" 으로 바꿔야 버스트에 오탐이 없다.

</details>

<details>
<summary>Q8. 이 설계가 틀리는 경우는? — 리셋 원인 판정 로직의 구멍</summary>

(1) 덤프·빵부스러기가 둘 다 없는 리셋(브라운아웃, 외부 리셋 핀, NMI 전에 HardFault 안에서 lockup)은 `UNKNOWN` 으로만 보인다 — mps2 엔 리셋 원인 레지스터가 없어 소프트웨어 기록만 쓴다; 실칩은 RCC_CSR 같은 하드웨어 플래그와 교차 확인해야 한다. (2) NMI 빵부스러기 뒤 2 차 만료 전에 다른 경로로 리셋되면 원인을 워치독으로 오판 — 빵부스러기에 "리셋 직전 단계" 를 쓰고 소비 시 하드웨어 플래그와 맞춘다. (3) 콜드 부팅 쓰레기가 우연히 magic+CRC 를 통과할 확률 ≈ 2⁻³² (연습 2: 200,000 개 0 건). (4) `.noinit` 형식이 버전업으로 바뀌면 옛 기록을 잘못 읽는다 → version 필드(덤프 v=1)를 검사에 포함.

</details>

<details>
<summary>Q9. 왜 -icount 로 돌렸나? 그게 실제 펌웨어 시험과 무슨 관계인가?</summary>

icount 없이도 52/52 는 통과하지만, since_kick 이 9 또는 10 으로 실행마다 달라지고(실측), busy 숫자도 흔들리며, 가상 시간이 벽시계를 따라가 1.04 s 가 걸린다. icount + `sleep=off` 는 가상 시간을 명령 수에 묶어 매번 같은 결과를 0.06 s 에 낸다 — CI 에서 flaky 하지 않은 HIL 대용 시험. 단, 타이밍 의존 버그(실제 baud 지연에서만 나는 경쟁)는 이 결정적 세계에선 안 보일 수도 있으니 "즉시 완료" 같은 극단 조건으로 일부러 쥐어짜고(이번 `log_kick` 버그처럼), 진짜 타이밍은 보드에서 본다.

</details>

## 9. 추가 연습

모두 `practice.c` (호스트 C) 하나에서 검증한다.

### 연습 1 — 헤더 검증의 모든 단일 비트 플립

448 B payload + 64 B 헤더를 C 판 mkimg 로 만들고 4,096 개 비트 플립 각각이 **맞는 오류 코드**로 거부되는지 (payload → BAD_CRC, 헤더 0–3 → BAD_MAGIC, 4–7 → BAD_HDR_VERSION, 나머지 → BAD_HDR_CRC). 헤더 CRC 까지 맞춘 거짓 크기 6 가지(0, ±4, 0x7FFFFFFF, 0xFFFFFFC0, 0xFFFFFFFF)와 64 B 보다 작은 슬롯도.

<details>
<summary>답 / 결과</summary>

`ex1: 4096 single-bit flips -> accepted 0, wrong error code 0`. 핵심은 `slot_len - 64` 뺄셈을 `slot_len >= 64` 확인 **뒤에만** 하는 것 (언더플로하면 거대한 슬롯이 되어 아무 크기나 통과). 크기 비교는 `payload_size > slot_len - 64` 형태로 — `payload_size + 64 > slot_len` 은 0xFFFFFFC0 에서 오버플로한다.

</details>

### 연습 2 — 콜드 부팅 쓰레기와 찢어진 기록

`dump_t` 를 xorshift 쓰레기로 200,000 번 채워 `dump_valid` 오수락을 세고, magic·version 을 강제로 맞춘 최악 경우도 센다. 그리고 이전 유효 덤프 위에 새 덤프를 쓰다가 k 번째 쓰기에서 끊기는 모든 경우를 "magic 먼저 지우기" 순서와 "내용만 덮어쓰기" 순서로 비교 — CRC 검사 / magic 만 보는 검사 각각.

<details>
<summary>답 / 결과</summary>

```text
ex2: 200000 garbage fills -> false accept 0; with magic+version forced -> 0
ex2: torn writes accepted as valid (mixed old/new): good order crc=0 magic-only=0 | no-clear order crc=0 magic-only=15
```

magic 을 먼저 지우면 CRC 없이도 섞인 기록이 안 보이고, 순서를 어겨도 CRC 가 잡는다 — 둘은 서로의 보험이다. 둘 다 없으면 15 곳의 끊김 지점에서 "옛 PC + 새 CFSR" 같은 거짓 덤프를 믿는다. Don 의 SSD 경험으로 말하면 FTL 메타데이터의 "commit 레코드는 마지막, 그리고 CRC" 와 같은 규칙.

</details>

### 연습 3 — `log_kick` 경쟁을 호스트 모델로

QEMU 처럼 "DATA 쓰기 = 즉시 완료 = 마스크가 없으면 즉시 ISR" 모델에서 세 순서(PRIMASK+꼬리 먼저 / 꼬리 먼저 / DATA 먼저)로 4 개 메시지 × 인덱스 랩어라운드 32 지점을 보낸다.

<details>
<summary>답 / 결과</summary>

```text
ex3: 128 cases: correct output  primask+tail-first 128, tail-first 128, data-first 0
ex3: data-first sends "BBOOT" for "BOOT" and leaves tail=1 head=4 busy=0
ex3: next kick after pushing "1" -> total output "BBOOTOOOT1" (QEMU 실측 첫 줄: "BBOOT OOOT 1OOT 1 ...")
```

꼬리 먼저면 이 단일 생산자 경로에선 PRIMASK 없이도 맞다. 그래도 PRIMASK 를 두는 이유: `tx_busy` 확인과 설정 사이, 그리고 나중에 두 번째 생산자(예: 다른 ISR 의 긴급 로그)가 생기면 그 창이 열린다. "지금 맞다" 보다 "규칙이 단순해서 바꿔도 맞다" 가 펌웨어 리뷰의 기준.

</details>

### 연습 4 — 킥 정책 × 장애 표

1 ms 단위 시뮬레이션: 정책 3 개(ISR 킥 / main 무조건 / 하트비트+비움) × 장애 4 개(없음 / main 멈춤 / 틱 죽음 / main 느림 — 20 ms 에 한 샘플). 1 초 안에 리셋이 나나?

<details>
<summary>답 / 결과</summary>

```text
ex4: reset within 1 s?   none main_stuck tick_dead main_slow
     kick in tick ISR        -          -          RESET      -
     kick every main loop    -          RESET      -          -
     heartbeat + drained     -          RESET      RESET      RESET
```

정답 정책만 세 장애를 다 잡고 정상에선 리셋하지 않는다. "main 느림" 을 리셋으로 처리하는 게 맞는지는 제품 결정이다 (데이터를 영영 따라잡지 못하는 로거는 고장이다 — 하지만 일시적 버스트라면 임계값을 둔다, Q7).

</details>

### 연습 5 — 예산 계산

워치독 LOAD(25 MHz, 100 ms → 2,500,000; 32 비트로 한 단계 최대 ≈ 171.8 s), UART 대역(115200 8N1 = 11,520 B/s, 22 B 줄 → 상한 523 Hz), 그리고 §Part C 의 icount 실측 5,974 / 3,000,489 로 부하 ppm 과 틱당 명령 수.

<details>
<summary>답 / 결과</summary>

```text
ex5: UART 115200 8N1 = 11520 B/s; 100 Hz x 22 B = 2200 B/s (19 %); max rate for this line ~ 523 Hz
ex5: logging-phase load 1991 ppm (0.19 %), ~14935 instructions over 12 ticks = 1244 per tick
```

워치독 시간 선택 규칙: 경고(NMI)까지 = 정상 최악 루프 시간 × 여유(여기선 틱 10 ms vs 100 ms = 10 배), 리셋까지 = 경고 + 빵부스러기 봉인에 충분한 시간. 너무 길면 장애 중 출력이 오래 위험한 상태로 남는다 (BM-M13 안전 상태).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A11
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A11_pr && /tmp/BM-A11_pr          # 47/47 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-A11_as && /tmp/BM-A11_as
limactl shell elinux -- bash -lc "bash $PWD/run_qemu.sh practice"                             # VM 에서도 47/47
```

## 10. 핵심 정리

- 통합의 버그는 부품이 아니라 **접착부**에 산다: ISR↔main 링 인계, 송신기 깨우기, 킥 정책.
- 킥은 main 루프에서만, "틱이 살아 있다 ∧ main 이 따라잡았다" 의 증거와 함께. ISR 킥·무조건 킥은 각각 한 종류의 고장을 못 본다 (둘 다 QEMU 실측).
- 2 단계 워치독의 경고(NMI)는 빵부스러기를 봉인하는 시간이다 — 거기서 킥(INTCLR)하면 리셋이 무한 연기된다.
- 빵부스러기 since_kick 하나로 "main 멈춤(10)" 과 "틱 죽음(0)" 을 사후에 구별한다; PC 는 멈춘 곳 / WFI 다음.
- CMSDK UART TX 인터럽트는 에지 → main 이 첫 바이트를 쓴다. 임계 구역 + 꼬리 먼저; 거꾸로 하면 QEMU 에서 `BBOOT OOOT…`.
- fault 는 `.noinit` 에 봉인(내용 → CRC → magic) 후 리셋, 보고는 다음 부팅의 깨끗한 드라이버가. magic-last 와 CRC 는 서로의 보험.
- 헤더 검사 순서: magic → 버전 → 헤더 CRC → 크기 → 주소 → payload CRC. CRC 는 우연만 막고 위조는 서명(SEC)이.
- 숫자는 출처를 붙여 말한다: Flash 8,716 B / .noinit 644 B (size 실측), 부하 0.2 % (icount 명령 수) ≠ 사이클 (~0.9 % 모델).

## 참고자료

- QEMU v10.2.0 소스 (raw.githubusercontent.com/qemu/qemu/v10.2.0/…): `hw/arm/mps2.c` (UART0 TX = uartirq+1 = IRQ 1, timer irqno = 8 + i, watchdog 0x40008000 → "NMI", SYSCLK 25 MHz), `hw/char/cmsdk-apb-uart.c:225` (TX 인터럽트 = TXFULL 1→0), `hw/timer/cmsdk-apb-timer.c` (INTSTATUS W1C, PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD), `hw/watchdog/cmsdk-apb-watchdog.c:154/299/302` (INTCLR = 재장전, 1 차 인터럽트·2 차 리셋, LOCK), `hw/intc/armv7m_nvic.c:971` (NMI 상승 에지), `hw/watchdog/watchdog.c:50` (기본 동작 = 시스템 리셋). https://gitlab.com/qemu-project/qemu
- QEMU 문서: `-icount shift=N,sleep=off` (TCG Instruction Counting) — https://www.qemu.org/docs/master/devel/tcg-icount.html
- Arm CMSDK Technical Reference Manual (DDI 0479) — APB timer / UART / watchdog 레지스터 (확인 권장).
- ARMv7-M Architecture Reference Manual (DDI 0403) — 예외 진입·EXC_RETURN·WFI 깨움 조건·HFSR.FORCED (확인 권장). Cortex-M3 Devices Generic User Guide (DUI 0552).
- 이 트랙의 앞 레슨: BM-B05 (맵·size), BM-B08 (startup), BM-B14 (타이머), BM-M04 (인터럽트 UART), BM-M10 (워치독), BM-M13 (fault 정책·.noinit·QEMU 리셋 사실), BM-A02/A03/A04/A06/A07/A09/A10. 다음 단계: ARM-M12 (CFSR 해독·역추적), TEST-B09 (태스크 체크인 워치독), SEC (서명), PLAT-M07 (A/B 슬롯).
- MCUboot 설계 문서 (헤더 앞 배치, test/confirm) — https://docs.mcuboot.com/design.html (확인 권장).
