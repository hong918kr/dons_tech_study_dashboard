---
id: BM-M02
time: 80분
updated: 2026-11-11
---
# BM-M02 시스템 IRQ 우선순위 설계와 선점 검증

## 학습 목표

- Cortex-M 우선순위 3규칙을 말로, 레지스터로 설명할 수 있다: **(1) 그룹(선점) 우선순위가 더 급해야만 선점한다, (2) 동시에 pending 이면 그룹 → 서브 → 예외 번호 순서로 고른다, (3) 숫자가 작을수록 급하다.**
- 모터 PWM · ADC · UART RX/TX · SysTick · PendSV 가 있는 시스템의 우선순위 표와 PRIGROUP 을 **근거(데드라인, 공유 자원)** 와 함께 정하고, `AIRCR`(VECTKEY) · `NVIC_IPR` · `SHPR3` 에 올바른 바이트를 쓴다.
- 진짜 Cortex-M3 (QEMU `mps2-an385`) 에서 `NVIC_STIR` / `NVIC_ISPR` / `ICSR.PENDSVSET·PENDSTSET` 로 IRQ 를 겹쳐 걸고, RAM 트레이스로 선점·비선점·꼬리 물기(tail-chaining) 순서를 **검증**한다.
- 흔한 사고(VECTKEY 없는 AIRCR, `|=` 로 쓴 AIRCR, 시프트 안 한 우선순위, PRIGROUP 0, 전부 0 으로 둔 우선순위)를 일부러 일으키고 증상을 기억한다.
- QEMU 가 말해 주지 **않는** 것(사이클 타이밍, 구현 비트 수 차이)을 알고, 응답 시간 예산을 호스트 계산으로 따로 확인한다.

## 1. 문제

펌웨어 면접의 단골: **"모터 PWM 과 UART 중 누가 더 높은 우선순위인가요? 왜요?"**, **"서브 우선순위가 더 높으면 선점하나요?"**, **"PendSV 는 왜 제일 낮아요?"**. 보드 브링업에서는 우선순위 실수가 "가끔 생기는 UART overrun", "1 만 번에 한 번 튀는 모터 전류" 처럼 재현하기 어려운 버그로 나온다. Don 이 SSD 컨트롤러에서 NVMe 커맨드 완료 인터럽트와 NAND 채널 인터럽트의 순서를 정했던 것과 같은 일을, 이번엔 Cortex-M NVIC 레지스터 수준에서 직접 설계하고 **진짜 코어(QEMU)** 로 증명한다.

BM-M01 (NVIC 레지스터 비트 계산, 호스트 mock) 이 "어느 비트를 쓰나" 였다면, 이 레슨은 "어떤 값을 왜 쓰나 + 정말 그렇게 도나" 다. 크리티컬 섹션(PRIMASK/BASEPRI)은 ARM-M10, 인터럽트 지연 사이클은 ARM-A08 에서 다룬다.

우선순위 바이트 한 개의 구조. STM32F4 는 상위 4비트만 구현(`__NVIC_PRIO_BITS = 4`), QEMU 는 8비트를 다 구현한다(Part A 에서 실측). PRIGROUP 이 그 바이트를 그룹(선점)과 서브로 자른다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">우선순위 바이트 (IPR[n] / SHPR) — PRIGROUP = 4 일 때</text>
  <text x="30" y="52" fill="currentColor">비트</text>
  <g font-family="monospace">
    <rect x="90" y="36" width="60" height="26" class="f-acc" stroke="currentColor"/><text x="115" y="54" fill="currentColor">7</text>
    <rect x="150" y="36" width="60" height="26" class="f-acc" stroke="currentColor"/><text x="175" y="54" fill="currentColor">6</text>
    <rect x="210" y="36" width="60" height="26" class="f-acc" stroke="currentColor"/><text x="235" y="54" fill="currentColor">5</text>
    <rect x="270" y="36" width="60" height="26" class="f-ok" stroke="currentColor"/><text x="295" y="54" fill="currentColor">4</text>
    <rect x="330" y="36" width="60" height="26" class="f-mut" stroke="currentColor"/><text x="355" y="54" fill="currentColor">3</text>
    <rect x="390" y="36" width="60" height="26" class="f-mut" stroke="currentColor"/><text x="415" y="54" fill="currentColor">2</text>
    <rect x="450" y="36" width="60" height="26" class="f-mut" stroke="currentColor"/><text x="475" y="54" fill="currentColor">1</text>
    <rect x="510" y="36" width="60" height="26" class="f-mut" stroke="currentColor"/><text x="535" y="54" fill="currentColor">0</text>
  </g>
  <text x="120" y="82" fill="currentColor">그룹(선점) [7:5] = 8 단계</text>
  <text x="262" y="100" fill="currentColor">서브 [4]</text>
  <text x="340" y="82" fill="currentColor">STM32F4: 미구현, 0 으로 읽힘</text>
  <text x="340" y="100" fill="currentColor">QEMU mps2-an385: 구현됨 (8비트)</text>
  <line x1="270" y1="30" x2="270" y2="70" stroke="currentColor" stroke-width="3"/>
  <text x="580" y="54" fill="currentColor">← PRIGROUP 경계</text>
  <text x="30" y="135" fill="currentColor" font-weight="bold">설계 값 (encode → &lt;&lt; 4)</text>
  <g font-family="monospace" font-size="12">
    <text x="30" y="160" fill="currentColor">MOTOR_PWM  IRQ14  0x00 = 000 0 0000   group 0 sub 0</text>
    <text x="30" y="180" fill="currentColor">ADC        IRQ15  0x20 = 001 0 0000   group 1 sub 0</text>
    <text x="30" y="200" fill="currentColor">UART_RX    IRQ16  0x40 = 010 0 0000   group 2 sub 0</text>
    <text x="30" y="220" fill="currentColor">UART_TX    IRQ17  0x40 = 010 0 0000   group 2 sub 0  (RX 와 동률 → 번호 16 &lt; 17)</text>
    <text x="30" y="240" fill="currentColor">SysTick    -1     0x50 = 010 1 0000   group 2 sub 1  (SHPR3 [31:24])</text>
    <text x="30" y="260" fill="currentColor">PendSV     -2     0xF0 = 111 1 0000   group 7 sub 1  (SHPR3 [23:16])</text>
  </g>
  <text x="30" y="288" fill="currentColor">선점 조건: 새 예외의 그룹 &lt; 지금 실행 중인 가장 급한 그룹 (같으면 절대 선점 안 함)</text>
</svg>
```

### Part A — QEMU 가 무엇을 구현했나 + 우선순위 표 올리기

1. **먼저 진짜를 확인**: QEMU 10.2.1 `hw/arm/mps2.c` 를 읽어 외부 IRQ 수(`num-irq` 32)와 장치가 안 붙은 줄을 찾고, 펌웨어로 탐침(probe)한다 — `ICTR.INTLINESNUM`, `ISER0/1` 에 전부 1 을 써서 읽기, 빈 줄의 `IPR` 바이트에 0xFF 를 써서 몇 비트가 남는지.
2. 빈 줄 14·15·16·17 에 가짜 MOTOR_PWM · ADC · UART_RX · UART_TX 를 놓고, `aircr_set_prigroup()` (VECTKEY), `prio_encode()` (CMSIS `NVIC_EncodePriority`), `nvic_set_prio()` (IRQ → IPR 바이트, 음수 → SHPR 바이트) 를 구현한다.
3. 설계 표(`k_design`)와 `DESIGN_PRIGROUP` 을 채우고, 레지스터를 다시 읽어 바이트·AIRCR·SHPR3 를 확인한다. gdb 로도 같은 값을 읽는다.

### Part B — 선점 트레이스와 결함 주입

핸들러는 진입 때 대문자, 복귀 때 소문자를 RAM 트레이스에 남기고(스레드로 돌아오면 `.`), 진입하자마자 "plan" 에 적힌 다른 소스들을 소프트웨어로 pending 시킨다. 다음 7 개 순서를 설계에서 손으로 유도한 기대값과 비교한다: 더 급한 그룹의 중첩(S1), 덜 급한 그룹의 대기(S2), **같은 그룹·다른 서브는 어느 방향으로도 선점 안 함**(S3, S3b), 4 개 동시 pending 의 순서(S4: 서브 → IRQ 번호 → PendSV 마지막), PendSV 꼬리 물기(S5), 자기 자신 pend(S6). 그리고 사고 5 개를 주입한다: VECTKEY 없는 AIRCR(F1), `AIRCR |=`(F1b), PRIGROUP 0(F2), 시프트 안 한 우선순위(F3), 꺼진 IRQ 의 pend(F4).

### Part C — 20 kHz 모터 제어 루프 버스트

400 주기의 결정적 이벤트 스크립트: 매 주기 PWM 업데이트 → ADC 변환 → (3 주기마다) UART 바이트 도착 → (8 바이트마다) PendSV 로 명령 해석 미루기 → 응답 1 바이트 TX, (20 주기마다) SysTick, (8 주기에 한 번) ADC ISR 이 길어져 다음 PWM 에지가 ADC 안으로 끼어든다. 불변식(PWM 은 한 번도 줄 서지 않는다, PendSV 는 항상 베이스 레벨, 최대 중첩 깊이 2, 개수)과 주기별 로그를 검사한다. 사이클 수준 예산은 QEMU 가 못 재므로 연습 4 에서 호스트 계산(RTA + 시뮬레이션)으로 한다.

## 2. Problem (English)

You are bringing up a motor-controller MCU (Cortex-M, 4 implemented priority bits). Interrupt sources:
MOTOR_PWM update (20 kHz, must write the new duty within 10 µs), ADC end-of-conversion (current loop, 12 µs of
work, must finish before the next PWM update), USART RX (115200 8N1, no FIFO: read DR within one character time ≈
86.8 µs or ORE), USART TX, SysTick (1 kHz) and PendSV (deferred work / RTOS context switch).

1. Choose a PRIGROUP and a (group, sub) priority for every source; justify each choice.
2. Implement on bare metal (no CMSIS):
   ```c
   void     aircr_set_prigroup(uint32_t prigroup);                         /* needs VECTKEY 0x05FA */
   uint32_t prio_encode(uint32_t prigroup, uint32_t group, uint32_t sub);  /* = NVIC_EncodePriority */
   void     nvic_set_prio(int irqn, uint32_t prio);  /* irqn >= 0 -> NVIC_IPR byte, < 0 -> SHPR byte */
   ```
   Example: PRIGROUP 4, (group 2, sub 1) → encode = 5 → byte 0x50; `nvic_set_prio(-1, 5)` writes SHPR3[31:24].
3. On QEMU `mps2-an385` (Cortex-M3), place fake devices on unused IRQ lines, trigger them with `NVIC_STIR` /
   `NVIC_ISPR` / `ICSR`, and prove with an enter/exit trace that: a higher group preempts (nesting), the same
   group with a better sub-priority does *not* preempt but is served first when both are pending, equal priority
   is broken by the lower IRQ number, and PendSV runs last via tail-chaining.
   Example: RX handler pends ADC then (inside ADC) PWM → trace `RAPpar.`; PWM handler pends PendSV, SysTick,
   UART_TX, UART_RX → `PpRrTtSsVv.`.
4. Show what goes wrong with: AIRCR written without the key, priority not shifted into the top bits, PRIGROUP = 0,
   all priorities left at reset value 0.

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `bm_m02.ld` (BM-B08 링커 스크립트 축약판), `run_qemu.sh` (VM 빌드·실행), `demo_nvic.gdb` (gdb 배치 스크립트). 펌웨어는 한 파일 안에 startup (BM-B08 축약판: 벡터 테이블, `.data` 복사, `.bss` 0) 과 세미호스팅 출력을 담는다.

<!-- code: bm_m02.ld -->

실행은 Lima VM `elinux` 안 (Mac 홈은 VM 에서 읽기 전용 → 스크립트가 `/tmp/BM-M02_work` 로 복사해 빌드, QEMU 는 `SYS_EXIT_EXTENDED` 로 스스로 끝나고 `timeout 10` 이 안전망). Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-M02
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 18/34 passed, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 34/34 passed, qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # design_applied() 에서 NVIC/SCB 읽기 + 중첩 bt
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh tailchain"  # S5 만, QEMU -d int 예외 로그
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C) 를 VM 네이티브로
```

시작 코드 실행 결과 (실제 출력, 발췌):

```text
probe: ICTR.INTLINESNUM=0 ISER0 after 0xFFFFFFFF=0xffffffff ISER1=0x00000000
probe: IPR[25] write 0xFF reads 0x000000ff -> implemented priority bits = 8
encode/decode round trip: 8/128
AIRCR=0xfa050000 SHPR3=0x00000000 IPR[12..15]=0x00000000 IPR[16..19]=0x00000000 ISER0=0x0003c000
S1  higher group preempts (nesting)          got RrAaPp.       want RAPpar.       FAIL
S4  4 pending: sub, then IRQ number          got PpVvSsRrTt.   want PpRrTtSsVv.   FAIL
    S4 after pends: ICSR=0x1440e81e VECTPENDING=14 ISPR0=0x00030000
S5  PendSV last, tail-chained                got RrVvAa.       want RAarVv.       FAIL
F2  PRIGROUP=0: sub becomes preemption       got SsRr.         want SRrs.         FAIL
 k=18  PpSsAaRr.         FAIL want PpAaRrSs.
18/34 passed
qemu exit=1
```

탐침(probe)과 일부 시나리오(S2, S3, S6, F4)는 우선순위와 무관하게 통과한다. 전부 0 이면 **아무것도 선점하지 않고**(S1), 동시 pending 은 **예외 번호 순서**로 처리된다 — PendSV(14) 와 SysTick(15) 가 외부 IRQ(32, 33) 보다 먼저다(S4 `PpVvSsRrTt`, `VECTPENDING=14`). RTOS 라면 컨텍스트 스위치가 UART RX 보다 먼저 도는 셈이다.

## 4. 힌트

<details><summary>힌트 1 — 탐침은 "써 보고 읽기"</summary>

구현 안 된 비트는 0 으로 읽힌다(RAZ/WI). `IPR` 바이트에 0xFF 를 쓰고 읽어 위에서부터 1 의 개수를 세면 구현 비트 수다. `ISER1` 에 1 을 다 써도 0 이면 IRQ 32 이상은 없다. `ICTR`(0xE000E004) 의 INTLINESNUM 은 32 줄 단위 개수 − 1.

</details>

<details><summary>힌트 2 — AIRCR 은 열쇠가 필요하다</summary>

쓰기 값의 [31:16] 이 0x05FA 가 아니면 쓰기 전체가 무시된다. 읽으면 그 자리에 0xFA05(VECTKEYSTAT)가 나온다. 그래서 `|=` 는 안 된다: 읽은 0xFA05 와 0x05FA 를 OR 하면 0xFFFF. 키 필드와 PRIGROUP 필드를 지운 뒤 넣는다.

</details>

<details><summary>힌트 3 — encode: 비트 개수부터</summary>

그룹 비트 수 = min(7 − PRIGROUP, PRIO_BITS), 서브 비트 수 = PRIO_BITS − 그룹 비트 수 (음수면 0). 값 = (group &lt;&lt; 서브비트) | sub. 바이트로는 `<< (8 − PRIO_BITS)`. PRIGROUP 4, PRIO_BITS 4 → 그룹 3비트 · 서브 1비트.

</details>

<details><summary>힌트 4 — 음수 IRQn 은 SHPR</summary>

예외 번호 = 16 + IRQn (SysTick −1 → 15, PendSV −2 → 14). SHPR1 은 예외 4 부터 시작: 바이트 주소 = 0xE000ED18 + (예외 번호 − 4). CMSIS 는 `SHP[(IRQn & 0xF) − 4]`.

</details>

<details><summary>힌트 5 — 표는 "놓치면 무슨 일이 생기나" 순서</summary>

데드라인이 짧고 놓치면 물리적 결과가 나오는 것(PWM 듀티)이 위, 하드웨어 버퍼 하나 시간 안에 읽어야 하는 것(ADC DR, UART DR)이 다음, 늦어도 누적만 되는 것(SysTick)이 그 아래, "모든 ISR 이 끝난 뒤" 가 의미인 PendSV 가 맨 아래. 서로 공유 상태를 만지는 ISR 은 같은 그룹에 두면 서로 선점하지 않으니 락이 필요 없다.

</details>

## 5. 풀이

### Part A — QEMU 가 무엇을 구현했나 + 우선순위 표 올리기

**1) 먼저 확인한 사실들 (전부 실제로 확인)**

| 질문 | 답 | 어떻게 확인 |
|---|---|---|
| 외부 IRQ 몇 개? | 32 | `mps2.c`: AN385 → `num-irq` 32 / 탐침: ICTR.INTLINESNUM=0, ISER0 = 0xFFFFFFFF, ISER1 = 0 |
| 우선순위 비트 몇 개? | **8** | `armv7m_nvic.c`: `num-prio-bits` 기본 0 → v7 코어면 8 / 탐침: IPR[25] ← 0xFF → 0xFF |
| PRIGROUP 리셋값 | 0 | starter 출력 `AIRCR=0xfa050000` |
| 키 없는 AIRCR | 무시 | `armv7m_nvic.c`: `(value >> 16) == 0x05fa` 일 때만 처리 / F1, F1b |
| 동시 pending 순서 | 그룹 → 서브 → 예외 번호 | `armv7m_nvic.c` 주석 "lowest group priority … lowest subpriority … lowest exception number" / S4 |
| 꼬리 물기 | 모델링됨 | `m_helper.c`: "…tailchaining to pending exception" / `run_qemu.sh tailchain` 로그 |

진짜 칩과 다른 점: STM32F4 는 4비트, AN385 FPGA 의 실제 Cortex-M3 는 3비트로 알려져 있다(CMSIS `CMSDK_CM3.h` 의 `__NVIC_PRIO_BITS 3` — 확인 권장). 그래서 설계는 `PRIO_BITS 4` (STM32F4) 로 하고 상위 4비트만 쓴다. QEMU 는 하위 비트도 기억하므로, **시프트를 잊은 버그(F3)가 QEMU 와 실칩에서 다른 값으로 읽힌다** — 그래도 둘 다 그룹 0 이라 증상은 같다.

`mps2.c` (v10.2.1) 의 AN385 IRQ 배선에서 빈 줄 찾기:

| IRQ | 장치 | IRQ | 장치 |
|---|---|---|---|
| 0, 1 | UART0 RX, TX | 12 | UART0–2 오버플로 OR |
| 2, 3 / 4, 5 | UART1 / UART2 RX, TX | 13 | LAN9118 이더넷 |
| 8, 9 | CMSDK 타이머 0, 1 | 18, 19 / 20, 21 | UART3 / UART4 RX, TX |
| 10 | 듀얼 타이머 | 22 | PL022 (외부 ADC SPI) |
| 11, 24 | SPI (PL022 ×2 OR) | **6, 7, 14–17, 23, 25–31** | **아무것도 없음** |

워치독은 NMI 로 간다. 장치가 없는 줄은 하드웨어가 절대 올리지 않으므로 소프트웨어 pend 만으로 결정적인 실험이 된다. 14–17 을 쓴다.

**2) 우선순위 3규칙 (ARMv7-M ARM B1.5.4 — 절 번호 확인 권장)**

1. 숫자가 작을수록 급하다. Reset(−3), NMI(−2), HardFault(−1) 는 고정.
2. **선점**: pending 예외의 *그룹* 우선순위가 현재 실행 우선순위(active 중 가장 급한 그룹, 스레드면 "없음")보다 **엄격히** 작을 때만. 서브는 선점에 전혀 관여하지 않는다.
3. **선택**: 여러 개가 pending 이면 그룹 → 서브 → 예외 번호가 작은 것. 즉 구현된 비트의 raw 바이트가 작은 것, 같으면 번호가 작은 것.

**3) 설계 표와 근거**

| 소스 | (group, sub) | 바이트 | 놓치면 | 근거 |
|---|---|---|---|---|
| MOTOR_PWM | (0, 0) | 0x00 | 다음 주기에 옛 듀티 → 전류 리플, 제어 루프 불안정 | 주기 50 µs 안의 10 µs 예산. 짧은 ISR (C≈2 µs) 이라 남을 오래 막지 않는다 |
| ADC | (1, 0) | 0x20 | 다음 변환이 DR 을 덮음 (OVR) → 전류 샘플 하나 손실 | PWM 에 동기된 샘플. 계산이 길어(12 µs) PWM 보다 아래여야 PWM 예산이 안 깨진다 (연습 4) |
| UART_RX | (2, 0) | 0x40 | F4 USART 는 FIFO 없음 → 1 문자 시간(86.8 µs @115200) 넘기면 ORE | 데드라인은 길지만 하드. ADC·PWM 이 짧아서 아래 둬도 16.5 µs 안에 끝난다 |
| UART_TX | (2, 0) | 0x40 | 줄 사이 공백만 생김 | RX 와 같은 값 → 동시면 IRQ 번호(16 &lt; 17)로 RX 먼저. 같은 그룹이라 TX/RX 가 공유하는 드라이버 상태에 락 불필요 |
| SysTick | (2, 1) | 0x50 | 틱 하나가 늦어짐 (pending 이 남아 누적은 됨) | UART 와 같은 그룹 → 서로 선점 안 함 (RX 타임아웃 카운터를 공유해도 락 불필요). 서브 1 이라 UART 와 겹치면 UART 먼저 |
| PendSV | (7, 1) | 0xF0 | 지연 처리일 뿐 | "다른 ISR 이 하나도 없을 때" 가 의미. RTOS 컨텍스트 스위치도 여기 |

**PRIGROUP = 4** (ST HAL 이름으로 `NVIC_PRIORITYGROUP_3`: 선점 3비트 · 서브 1비트). 이유: 선점 단계는 8 개면 충분하고, "같은 그룹 = 서로 배타" 를 이용해 UART 와 SysTick 을 묶되 그 안의 순서는 서브로 정하고 싶어서다. 단, RTOS 를 올릴 거면 이야기가 다르다(Q&A 5).

**4) 레지스터에 쓰기**

`aircr_set_prigroup(4)`: 읽기 0xFA050000 → 키·PRIGROUP 필드 지움 → `0x05FA0000 | 4<<8` = 0x05FA0400 쓰기 → 읽기 0xFA050400.
`nvic_set_prio(15, prio_encode(4, 1, 0)=2)`: 바이트 0x20 을 0xE000E40F 에. 워드로 보면 IPR3 (0xE000E40C) = IRQ12..15 의 바이트 3 → `0x20000000`. `nvic_set_prio(-1, 5)`: 0xE000ED18 + (15 − 4) = 0xE000ED23 ← 0x50 = SHPR3[31:24].

`apply_design()` 은 우선순위를 **먼저** 다 쓰고, 남아 있을지 모르는 pending 을 `ICPR` 로 지운 뒤에 `ISER` 로 켠다. 순서를 거꾸로 하면 리셋값 0(=가장 급함)으로 한 번 돌 수 있다.

정답 실행 결과 (실제 출력, Part A):

```text
   text	   data	    bss	    dec	    hex	filename
  10108	      0	    388	  10496	   2900	solution.elf
BM-M02 priority design on qemu mps2-an385 (Cortex-M3)
== Part A: probe + design ==
probe: ICTR.INTLINESNUM=0 ISER0 after 0xFFFFFFFF=0xffffffff ISER1=0x00000000
probe: IPR[25] write 0xFF reads 0x000000ff -> implemented priority bits = 8
encode/decode round trip: 128/128
AIRCR=0xfa050400 SHPR3=0x50f00000 IPR[12..15]=0x20000000 IPR[16..19]=0x00004040 ISER0=0x0003c000
source     IRQn  byte  group sub
MOTOR_PWM  14   0x00000000  0     0
ADC        15   0x00000020  1     0
UART_RX    16   0x00000040  2     0
UART_TX    17   0x00000040  2     0
SysTick    -1   0x00000050  2     1
PendSV     -2   0x000000f0  7     1
```

같은 값을 디버거로 (`run_qemu.sh gdb`, `demo_nvic.gdb`, 실제 출력):

```text
Breakpoint 1, design_applied () at solution.c:131
AIRCR  = 0xfa050400  (VECTKEYSTAT 0xFA05, PRIGROUP [10:8])
SHPR3  = 0x50f00000  (SysTick [31:24], PendSV [23:16])
ISER0  = 0x0003c000
0xe000e40e:	0x00	0x20	0x40	0x40
```

`x/4xb 0xE000E40E` 가 IRQ14..17 의 바이트 4 개를 바로 보여 준다 — NVIC 는 바이트 접근이 된다(BM-M01 의 mock 은 32비트 전용이라 워드 RMW 를 했다).

### Part B — 선점 트레이스와 결함 주입

`pend()` 는 외부 IRQ 면 `NVIC_STIR ← IRQ 번호`, SysTick/PendSV 면 `ICSR ← PENDSTSET/PENDSVSET` 을 쓰고 `DSB; ISB` 를 한다. 배리어가 있어야 "pend 한 다음 명령 전에" 선점이 인식된다는 것이 보장된다(Arm 배리어 가이드 — 확인 권장). 그래서 선점은 **항상 `pend()` 안의 배리어 지점**에서만 일어나고, 트레이스 기록(`g_tn` RMW)이 끊길 일이 없다. 일반적인 ISR 공유 데이터는 ARM-M10 / C-M14 의 문제다.

S1 (중첩) 과 S5 (꼬리 물기) 를 실행 우선순위 축으로 그리면:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">S1 "RAPpar." — 더 급한 그룹이 중첩 선점</text>
  <text x="10" y="52" fill="currentColor">PWM g0</text>
  <text x="10" y="82" fill="currentColor">ADC g1</text>
  <text x="10" y="112" fill="currentColor">RX g2</text>
  <text x="10" y="142" fill="currentColor">thread</text>
  <line x1="70" y1="150" x2="720" y2="150" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="70" y="132" width="40" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="102" width="70" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="180" y="72" width="70" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="42" width="110" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="360" y="72" width="60" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="420" y="102" width="60" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="480" y="132" width="60" height="16" class="f-mut" stroke="currentColor"/>
  <text x="74" y="128" fill="currentColor">STIR 16</text>
  <text x="114" y="98" fill="currentColor">STIR 15</text>
  <text x="184" y="68" fill="currentColor">STIR 14</text>
  <text x="256" y="38" fill="currentColor">IABR0 = 0x0001c000</text>
  <text x="365" y="68" fill="currentColor">a</text>
  <text x="425" y="98" fill="currentColor">r</text>
  <text x="490" y="128" fill="currentColor">"."</text>
  <text x="560" y="60" fill="currentColor">깊이 3: RX → ADC → PWM</text>
  <text x="560" y="78" fill="currentColor">각 pend 직후 바로 선점</text>
  <text x="10" y="188" fill="currentColor" font-weight="bold" font-size="13">S5 "RAarVv." — PendSV 는 모든 ISR 뒤, 스레드로 안 돌아가고 바로 (tail-chain)</text>
  <text x="10" y="222" fill="currentColor">ADC g1</text>
  <text x="10" y="252" fill="currentColor">RX g2</text>
  <text x="10" y="282" fill="currentColor">PendSV g7</text>
  <text x="10" y="312" fill="currentColor">thread</text>
  <line x1="70" y1="320" x2="720" y2="320" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="70" y="302" width="40" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="242" width="110" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="220" y="212" width="90" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="310" y="242" width="60" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="370" y="272" width="110" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="480" y="302" width="60" height="16" class="f-mut" stroke="currentColor"/>
  <text x="114" y="238" fill="currentColor">pend V, pend A</text>
  <text x="114" y="275" fill="currentColor">V: pending 대기 (g7)</text>
  <path d="M370 250 L 372 270" stroke="currentColor" fill="none"/>
  <text x="376" y="268" fill="currentColor">tail-chain (fffffff9)</text>
  <text x="380" y="300" fill="currentColor">RETTOBASE=1</text>
  <text x="490" y="298" fill="currentColor">"."</text>
</svg>
```

정답 실행 결과 (실제 출력, Part B):

```text
== Part B: preemption traces (designed table) ==
S1  higher group preempts (nesting)          got RAPpar.       want RAPpar.       PASS
    S1 IABR0 at PWM entry=0x0001c000 max depth=3
S2  lower group waits                        got PpAa.         want PpAa.         PASS
S3  same group, better sub: no preempt       got SsRr.         want SsRr.         PASS
S3b same group, worse sub: no preempt        got RrSs.         want RrSs.         PASS
S4  4 pending: sub, then IRQ number          got PpRrTtSsVv.   want PpRrTtSsVv.   PASS
    S4 after pends: ICSR=0x1442081e VECTPENDING=32 ISPR0=0x00030000
S5  PendSV last, tail-chained                got RAarVv.       want RAarVv.       PASS
    S5 ICSR at PendSV entry=0x0000080e (VECTACTIVE=14)
S6  self-pend: no re-entry, runs again       got TtTt.         want TtTt.         PASS
== Part B: injected faults ==
F1  AIRCR write without VECTKEY -> PRIGROUP still 4
F1b AIRCR |= (0x05FA<<16)|(5<<8) -> PRIGROUP still 4
F2  PRIGROUP=0: sub becomes preemption       got SRrs.         want SRrs.         PASS
F3' RX prio byte 0x40 (correct): waits       got AaRr.         want AaRr.         PASS
    F3 IPR[16] reads 0x00000004 (QEMU keeps low bits; a 4-bit part would read 0x00 — both = group 0)
F3  RX prio byte 0x04 (no <<4): preempts ADC got ARra.         want ARra.         PASS
F4  pend while disabled: ISPR0=0x00010000 trace .Rr. (want .Rr.)
```

한 줄씩 읽기:

- **S1**: PWM 진입 순간 `IABR0 = 0x0001C000` = 비트 14·15·16 → RX, ADC, PWM 셋 다 active. gdb `bt` 로 보면 프레임 사이에 `<signal handler called>` 가 세 번, 매번 `barrier()` (pend 직후 DSB/ISB) 에서 끊겼다 — 선점 지점이 정확히 보인다:
  ```text
  #0  MotorPwm_IRQHandler () at solution.c:201
  #1  <signal handler called>
  #2  barrier () at solution.c:60
  #3  pend (s=<optimized out>) at solution.c:176
  #4  isr_body (s=1) at solution.c:193
  #5  <signal handler called>
  ...
  #12 run_scn (sc=0x25c4 <k_scn>) at solution.c:264
  ```
- **S3 / S3b**: UART_RX(0x40) 와 SysTick(0x50) 은 그룹 2 로 같다. 어느 쪽이 먼저 돌든 다른 쪽은 기다린다. "서브가 더 급하면 선점" 은 **틀린 말**이다.
- **S4**: PWM 안에서 V, S, T, R 순서로 pend 한 직후 `ICSR = 0x1442081E`: PENDSVSET(28)·PENDSTSET(26)·ISRPENDING(22)·RETTOBASE(11), **VECTPENDING = 32** (= IRQ16 UART_RX), VECTACTIVE = 0x1E = 30 (= IRQ14 PWM). NVIC 가 다음에 고를 것을 레지스터로 미리 알려 준다. 실제 순서 R(서브 0, 번호 32) → T(서브 0, 번호 33) → S(서브 1) → V(그룹 7). pend 한 시간 순서는 무관하다.
- **S5**: PendSV 진입 시 `ICSR = 0x0000080E` → RETTOBASE=1 (다른 active 없음), VECTACTIVE = 14. 그리고 트레이스에서 `.` 이 `v` 뒤에 있다 = RX 복귀와 PendSV 진입 사이에 스레드 코드가 한 줄도 안 돌았다. QEMU 내부 로그로도 확인(`run_qemu.sh tailchain`, S5 만 도는 빌드, 실제 출력):
  ```text
  Taking exception 5 [IRQ] on CPU 0
  ...taking pending nonsecure exception 32
  Taking exception 5 [IRQ] on CPU 0
  ...taking pending nonsecure exception 31
  Taking exception 8 [QEMU v7M exception exit] on CPU 0
  Exception return: magic PC fffffff1 previous exception 31
  ...successful exception return
  Taking exception 8 [QEMU v7M exception exit] on CPU 0
  Exception return: magic PC fffffff9 previous exception 32
  ...tailchaining to pending exception
  ...taking pending nonsecure exception 14
  Taking exception 8 [QEMU v7M exception exit] on CPU 0
  Exception return: magic PC fffffff9 previous exception 14
  ...successful exception return
  ```
  ADC 복귀의 EXC_RETURN `0xFFFFFFF1` = 핸들러 모드로 복귀(RX 로), RX 복귀의 `0xFFFFFFF9` = 스레드·MSP 로 복귀하려는 순간 "tailchaining" — 언스택·리스택 없이 PendSV 로 바로 들어간다. 실칩에서는 이게 사이클 이득(대표적으로 12 → 6 사이클, ARM-A08 — 확인 권장)이고, QEMU 는 순서만 재현한다.
- **S6**: 자기 자신을 pend 해도 같은 우선순위라 재진입하지 않고, 복귀 직후 한 번 더 돈다. BM-M01 의 "늦은 플래그 클리어 → 재진입" 이 정확히 이 모양이다.
- **F1 / F1b**: AIRCR 은 키가 틀리면 *전체* 쓰기가 무시된다 — SYSRESETREQ 도 마찬가지(BM-M13).
- **F2**: PRIGROUP 0 이면 그룹 = 비트 [7:1] (QEMU 8비트 기준; 4비트 파트면 [7:4] 전부). 0x40 과 0x50 이 다른 그룹이 되어 S3 이 `SRrs.` — RX 가 SysTick 을 선점한다. 같은 그룹이라 락 없이 공유하던 상태가 이제 깨진다.
- **F3**: encode 값 4 를 시프트 없이 바이트에 쓰면 0x04. 그룹 비트 [7:5] = 0 → **PWM 과 같은 그룹 0**. ADC(그룹 1)를 선점(`ARra.`). 4비트 실칩에서는 0x00 으로 읽혀 더 노골적이지만 결과는 같다.
- **F4**: 꺼진 IRQ 를 `ISPR` 로 pend 하면 안 돈다(`.` 먼저), `ISPR0` 비트 16 이 남아 있다가 `ISER` 쓰는 순간 실행(`.Rr.`). 초기화 때 `ICPR` 로 묵은 pending 을 지우는 이유다.

### Part C — 20 kHz 모터 제어 루프 버스트

`burst_hook()` 이 매 진입마다 하는 일: PWM → ADC pend (k%20==18 이면 SysTick 도) / ADC → k%3==0 이면 RX pend, k%8==5 이면 PWM pend ("ADC ISR 이 길어져 다음 PWM 에지가 그 안에 옴") / RX → 바이트 셈, 8 바이트째면 PendSV pend / PendSV → RETTOBASE·깊이 확인, TX pend. 가장 붐비는 주기 k=21:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">k=21: "PpAPpaAaRrVTtv." (QEMU 실측 트레이스)</text>
  <text x="10" y="50" fill="currentColor">PWM g0</text>
  <text x="10" y="80" fill="currentColor">ADC g1</text>
  <text x="10" y="110" fill="currentColor">RX/TX g2</text>
  <text x="10" y="140" fill="currentColor">PendSV g7</text>
  <text x="10" y="170" fill="currentColor">thread</text>
  <line x1="80" y1="178" x2="730" y2="178" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="80" y="160" width="30" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="40" width="50" height="16" class="f-acc" stroke="currentColor"/><text x="120" y="35" fill="currentColor">P</text>
  <rect x="160" y="70" width="60" height="16" class="f-warn" stroke="currentColor"/><text x="164" y="65" fill="currentColor">A: pend R, P</text>
  <rect x="220" y="40" width="50" height="16" class="f-acc" stroke="currentColor"/><text x="226" y="35" fill="currentColor">P (중첩)</text>
  <rect x="270" y="70" width="40" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="310" y="70" width="60" height="16" class="f-warn" stroke="currentColor"/><text x="314" y="65" fill="currentColor">A 다시</text>
  <rect x="370" y="100" width="60" height="16" class="f-ok" stroke="currentColor"/><text x="374" y="95" fill="currentColor">R: 8번째</text>
  <rect x="430" y="130" width="40" height="16" class="f-mut" stroke="currentColor"/><text x="430" y="125" fill="currentColor">V</text>
  <rect x="470" y="100" width="50" height="16" class="f-ok" stroke="currentColor"/><text x="474" y="95" fill="currentColor">T 선점</text>
  <rect x="520" y="130" width="50" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="570" y="160" width="40" height="16" class="f-mut" stroke="currentColor"/><text x="578" y="155" fill="currentColor">"."</text>
  <text x="80" y="205" fill="currentColor">R 은 ADC 가 pend 했지만 P, A(두 번째) 다음에야 진입 → wait = 2 (아래 "max wait UART_RX=2")</text>
  <text x="80" y="222" fill="currentColor">중첩 PWM 이 pend 한 A 는 A 가 active 중이라 pending 으로 남았다가 꼬리 물기로 한 번 더 돈다</text>
</svg>
```

정답 실행 결과 (실제 출력, Part C):

```text
== Part C: 20 kHz control-loop burst, 400 periods ==
  k=0  PpAaRr.           (as designed)
  k=1  PpAa.             (as designed)
  k=5  PpAPpaAa.         (as designed)
 k=18  PpAaRrSs.         (as designed)
 k=21  PpAPpaAaRrVTtv.   (as designed)
  counts: MOTOR_PWM=450 ADC=450 UART_RX=134 UART_TX=16 SysTick=20 PendSV=16
  max wait (handler entries between pend and entry): MOTOR_PWM=0 ADC=0 UART_RX=2 UART_TX=0 SysTick=2 PendSV=0
  max nesting depth=2  PWM edge during ADC at k=5,13,21,...  PendSV not at base=0
34/34 passed
qemu exit=0
```

브링업 엔지니어처럼 읽기: PWM 450 = 400 주기 + 50 번의 "ADC 안으로 끼어든 에지"(k%8==5), ADC 도 450 (PWM 마다 하나). RX 134 = 0..399 중 3 의 배수, PendSV·TX 16 = 134/8. **PWM 의 max wait 0** 이 핵심 불변식이다 — 어느 주기에서도 PWM 은 다른 핸들러 뒤에 줄 서지 않았다. RX 는 최대 2 개(중첩 PWM 과 두 번째 ADC) 뒤에 섰고, SysTick 은 k=18 에서 A, R 뒤에 섰다(2). PendSV 는 16 번 모두 RETTOBASE=1 · 깊이 1 에서 시작했다. starter 설정(전부 0)이면 k=18 이 `PpSsAaRr.` 로 바뀐다 — SysTick(번호 15)이 ADC 보다 먼저 뽑힌다.

"handler entries" 는 시간이 아니다. QEMU 는 사이클 정확(cycle-accurate)하지 않아서 µs 예산을 여기서 재면 안 된다. µs 는 연습 4 (호스트 RTA + 이벤트 시뮬레이션) 에서, 실칩 측정은 DWT CYCCNT / GPIO 토글 + 로직 분석기(HWLAB-M05, ARM-A08)에서 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

바뀐 곳은 다섯 군데: `DESIGN_PRIGROUP`, `k_design` 여섯 줄, `aircr_set_prigroup`, `prio_encode`, `nvic_set_prio`. 하네스·시나리오·startup 은 starter 와 같다.

## 7. 자주 하는 실수

**1. 우선순위를 상위 비트로 안 올림 (F3).** `NVIC_IPR[16] = 4;` → 4비트 칩에선 0x00 (가장 급함), QEMU 에선 0x04 (역시 그룹 0). UART 가 모터 PWM 과 같은 급이 된다. CMSIS `NVIC_SetPriority` 는 `prio << (8 − __NVIC_PRIO_BITS)` 를 해 준다; 직접 쓰면 `REG8(IPR + n) = (uint8_t)(prio << (8 - PRIO_BITS));`.

**2. `SCB->AIRCR |= (0x5FA << 16) | (4 << 8);` (F1b).** 읽으면 [31:16] = 0xFA05 → OR 결과 0xFFFF → 키 불일치로 **조용히 무시**. 고친 코드:
```c
uint32_t v = SCB_AIRCR & ~((0xFFFFu << 16) | (7u << 8));
SCB_AIRCR = v | (0x05FAu << 16) | (4u << 8);
```

**3. "서브 우선순위가 더 높으면 선점한다" 고 믿기 (S3).** 서브는 *둘 다 pending 일 때의 순서* 만 정한다. 선점이 필요하면 그룹을 나눠야 한다.

**4. 전부 기본값 0 으로 두기 (starter).** 아무것도 선점하지 않고, 동시 pending 은 예외 번호 순 → PendSV·SysTick 이 모든 외부 IRQ 보다 먼저(`PpVvSsRrTt`). 연습 4 에서 PWM 응답이 45.5 µs 로 10 µs 예산을 깬다.

**5. HAL 상수와 "비트 수" 를 헷갈리기.** ST HAL `NVIC_PRIORITYGROUP_4` 는 값이 3 (= PRIGROUP 3, 선점 4비트)이다(확인 권장). `HAL_NVIC_SetPriorityGrouping(4)` 처럼 "4비트 선점" 의도로 숫자 4 를 넘기면 PRIGROUP 4 = 선점 3비트가 되고, 선점 우선순위 8..15 를 주면 잘려서 다른 그룹이 된다.

**6. 우선순위 쓰기 전에 enable, 묵은 pending 방치 (F4).** enable 순간 리셋값 우선순위 0 으로, 혹은 부팅 전에 쌓인 pending 으로 한 번 돈다. 순서: 우선순위 → `ICPR` → `ISER`.

**7. 3비트 파트로 이식하면서 서브 비트가 사라지는 것 모르기 (연습 3).** 0x50 이 0x40 으로 잘려 SysTick 이 UART 와 동률이 되고, 번호가 작은 SysTick(15)이 UART RX(32) 보다 먼저 뽑힌다. 선점 관계는 그대로라 테스트 대부분이 통과해서 더 늦게 발견된다.

## 8. 면접 Q&A

<details><summary>Q1. STM32F4 에서 NVIC_SetPriority(37, 5) 는 어느 레지스터 어느 비트를 바꾸나요?</summary>

IPR 은 IRQ 당 1 바이트: 바이트 주소 0xE000E400 + 37 = 0xE000E425. 워드로는 IPR9 (0xE000E424, 37/4 = 9) 의 바이트 레인 1 = 비트 [15:8]. 값은 5 << 4 = 0x50 → 비트 [15:12] = 0101, [11:8] = 0 (미구현). CMSIS 는 바이트 배열 `NVIC->IP[37]` 로 써서 다른 IRQ 를 건드리지 않는다. 워드 RMW 로 쓰면 그 사이에 다른 문맥이 같은 워드를 바꿀 수 있다는 점도 말하면 좋다. 꼬리질문 "SysTick 이면?" → IRQn −1 → 예외 15 → SHPR3[31:24], 0xE000ED23.

</details>

<details><summary>Q2. 모터 PWM 업데이트와 UART RX, 누가 더 높아야 하나요?</summary>

PWM. 근거는 "데드라인 길이 × 놓쳤을 때의 결과". PWM 듀티는 다음 업데이트 이벤트 전에 써야 하고(예산 10 µs / 주기 50 µs), 놓치면 옛 듀티로 한 주기가 더 나가 전류 리플이 생긴다. UART RX 는 FIFO 없는 F4 기준 1 문자 시간(115200 에서 86.8 µs)까지 버티면 된다. PWM ISR 이 짧으면(2 µs) RX 가 그 아래에 있어도 응답 16.5 µs 로 여유가 크다(연습 4). 꼬리질문 "UART 가 DMA 로 받으면?" → RX ISR 은 IDLE/HT/TC 처리뿐이라 더 내려도 된다(COMM-M11). "PWM 이 하드웨어 preload 를 쓰면?" → 데드라인이 주기 전체로 늘지만 제어 지연(샘플→적용)이 커지는 대가.

</details>

<details><summary>Q3. 서브 우선순위는 왜 있나요? 언제 쓰나요?</summary>

같은 그룹끼리는 서로 선점하지 않는다 → **그룹 = 상호 배타 영역**. 두 ISR 이 같은 상태(예: UART 드라이버의 링 버퍼 인덱스와 SysTick 의 RX 타임아웃 카운터)를 만지면 같은 그룹에 넣어 락 없이 안전하게 만들 수 있다. 그 안에서 "둘 다 pending 이면 누구 먼저" 는 서브로 정한다. S3/S3b 에서 확인했듯 서브가 더 급해도 선점은 없다. 단, 같은 그룹이면 긴 ISR 이 다른 쪽을 *막는다*(blocking) — 연습 4 의 B 항.

</details>

<details><summary>Q4. PendSV 는 왜 가장 낮게 두나요? 0 으로 두면?</summary>

PendSV 의 의미는 "모든 ISR 이 끝난 뒤 한 번" 이다. RTOS 는 여기서 컨텍스트 스위치를 해서, ISR 이 중첩된 상태에서 태스크 스택을 바꾸는 일을 피한다. 가장 낮으면 S5 처럼 마지막 ISR 복귀에서 꼬리 물기로 바로 들어오고(RETTOBASE=1), 스레드로 한 번 나갔다 오는 비용도 없다. 기본값 0 으로 두면 우선순위가 같은 외부 IRQ 와 동률이고 예외 번호 14 가 가장 작아 **제일 먼저** 뽑힌다(starter S4 `PpVvSsRrTt`, VECTPENDING=14). FreeRTOS Cortex-M 포트는 스케줄러 시작 시 PendSV·SysTick 을 가장 낮은 우선순위로 SHPR3 에 쓴다(port.c — 확인 권장).

</details>

<details><summary>Q5. FreeRTOS 를 쓰면 PRIGROUP 을 어떻게 하나요?</summary>

FreeRTOS 는 STM32 에서 모든 비트를 선점으로(`NVIC_PRIORITYGROUP_4`, 서브 0비트) 쓰라고 권하고, `configASSERT` 가 켜져 있으면 `vPortValidateInterruptPriority()` 가 PRIGROUP 과 "FromISR API 를 부르는 ISR 의 우선순위가 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 급하지 않은지" 를 검사한다(확인 권장). 이유는 커널이 BASEPRI 로 임계 구역을 만드는데, 서브 비트가 섞이면 "BASEPRI 아래" 판단이 그룹 기준이라 헷갈리기 때문. 그때는 이 레슨의 서브 트릭(UART·SysTick 같은 그룹) 대신 "같은 선점 값" 을 주면 된다 — 같은 값이면 역시 서로 선점 안 함. BASEPRI 자체는 ARM-M10.

</details>

<details><summary>Q6. 꺼져 있는 IRQ 를 pend 하면? 반대로 pending 인 채로 disable 하면?</summary>

pend 는 ISPR 비트로 남고 실행되지 않는다. enable 하는 순간 (우선순위가 허락하면) 바로 실행된다(F4: `.Rr.`). disable 해도 pending 은 지워지지 않는다 — 지우려면 ICPR. 그래서 드라이버 초기화 순서는 "주변장치 플래그 클리어 → NVIC ICPR → 우선순위 → ISER". 꼬리질문 "레벨 인터럽트면?" → 주변장치 플래그가 1 인 동안 줄이 계속 올라가 있어서 ICPR 로 지워도 다시 pending 된다; 원인(플래그)을 먼저 지워야 한다(BM-M01).

</details>

<details><summary>Q7. QEMU 에서 선점을 어떻게 증명했고, QEMU 가 못 알려 주는 건 뭔가요?</summary>

증명: (1) 핸들러 진입/복귀 트레이스를 기대 문자열과 비교, (2) 진입 시 `IABR0` 스냅샷 (S1 0x0001C000), (3) pend 직후 `ICSR.VECTPENDING` (S4 = 32), (4) gdb `bt` 의 `<signal handler called>` 중첩 프레임, (5) `-d int` 로그의 "tailchaining". 못 알려 주는 것: 사이클 정확도가 없어 지연·지터 µs 는 의미 없음, 구현 비트 수가 실칩과 다름(QEMU 8 vs F4 4 vs AN385 실 M3 3), late-arrival(스태킹 중 더 급한 IRQ 가 와서 벡터만 바꾸는 최적화) 같은 타이밍 현상. 실칩에선 GPIO 토글 + 로직 분석기나 DWT CYCCNT 로 잰다.

</details>

<details><summary>Q8. STIR 쓰기 뒤에 왜 DSB, ISB 를 넣었나요?</summary>

STIR/ISPR 쓰기는 버스 쓰기라 쓰기 버퍼에 머물 수 있고, 코어가 새 pending 을 인식하기 전에 다음 명령 몇 개가 실행될 수 있다. DSB 로 쓰기 완료를 기다리고 ISB 로 파이프라인을 비우면 "pend 다음 명령 전에 선점" 이 보장된다(Arm 의 메모리 배리어 응용 노트 권고 — 확인 권장). 테스트에서는 이 덕분에 선점 지점이 `pend()` 안으로 고정되어 결정적이 된다(gdb bt 의 `barrier ()` 프레임). 실무 예: 인터럽트를 끈 직후(ICER) "이제 ISR 이 안 돈다" 를 가정하는 코드도 DSB+ISB 가 필요하다.

</details>

<details><summary>Q9. 같은 그룹 안의 긴 ISR 이 다른 ISR 을 얼마나 늦출 수 있나요? 계산해 보세요.</summary>

같은 그룹의 순서가 뒤인 ISR 하나가 막 시작했다면 그 C 전체만큼 막힌다(blocking, 비선점 구간). 설계에서 UART_RX 는 SysTick(1.5 µs)에 막힐 수 있고, 위의 PWM(2)·ADC(12) 간섭까지 더해 시작 시각 15.5 µs, 끝 16.5 µs. 연습 4 의 상한식이 정확히 16500 ns, 3000 회 시뮬레이션 최악이 16499 ns 였다. ADC 를 PWM 그룹(서브 1)에 넣으면 PWM 이 ADC 12 µs 에 막혀 14 µs 로 10 µs 예산을 깬다.

</details>

## 9. 추가 연습

practice.c 는 **호스트 C** 다 (Mac 또는 VM 네이티브 `cc`). 펌웨어가 QEMU 에서 실측한 트레이스를 정답으로 삼아, 모델이 그것을 재현하는지 검사한다.

### 연습 1 — `prio_reg_locate(irqn, prio_bits, prio, *out)`

IRQn 과 우선순위 값에서 바이트 주소, 워드 주소, 바이트 레인, 쓸 바이트를 낸다. 음수는 설정 가능한 시스템 예외(MemManage −12, BusFault −11, UsageFault −10, SVCall −5, DebugMon −4, PendSV −2, SysTick −1)만 허용하고 HardFault/NMI/예약 번호는 거부. IRQ 0..239 × prio 0..15 를 다른 공식(워드 IRQ/4, 레인 IRQ%4)과 전수 비교.

<details><summary>답</summary>

`irqn ≥ 0 → 0xE000E400 + irqn`, `irqn < 0 → 0xE000ED18 + (16 + irqn − 4)`, 바이트 = `prio << (8 − bits)`. 3840/3840 일치. SysTick → 0xE000ED23 바이트 0x50, PendSV → 0xE000ED22, SVCall → 0xE000ED1F (SHPR2[31:24]), MemManage → 0xE000ED18, IRQ15 → 워드 0xE000E40C 레인 3.

</details>

### 연습 2 — `nvic_model_run()`: NVIC 중재 모델이 QEMU 를 재현하나

(raw 바이트, 그룹 마스크 `0xFF << (PRIGROUP+1)`, 예외 번호) 만으로 "선점 = 그룹이 실행 우선순위보다 엄격히 작음, 선택 = (raw, 번호) 최소, 복귀 시 다시 중재(꼬리 물기)" 를 재귀로 구현하고, 펌웨어와 같은 plan 으로 S1..S6, F2, F3, starter(전부 0)의 S1/S4/S5 를 돌려 QEMU 실측 문자열과 비교한다.

<details><summary>답</summary>

12 개 모두 같다. 특히 starter 의 `PpVvSsRrTt.` (동률이면 예외 번호 14 < 15 < 32 < 33) 와 F2 의 `SRrs.` (PRIGROUP 0 이면 8비트 QEMU 에서 그룹 = [7:1]) 가 같은 규칙에서 나온다. 모델 = 규칙 3 줄이면 충분하다는 것이 요점.

</details>

### 연습 3 — 이식성: PRIGROUP × 구현 비트 표, 3비트 파트로 옮기기

PRIGROUP 0..7 과 구현 비트 3·4·8 에서 (선점 단계 수 × 서브 단계 수) 표를 만들고, 설계 바이트를 3비트로 자른 뒤 연습 2 모델로 S4 를 다시 돌린다.

<details><summary>답</summary>

```text
  PRIGROUP  HAL group      bits=3     bits=4     bits=8
  0         -                8x1       16x1      128x2  
  3         PRIORITYGROUP_4   8x1       16x1       16x16 
  4         PRIORITYGROUP_3   8x1        8x2        8x32 
  7         PRIORITYGROUP_0   1x8        1x16       1x256
  design bytes on a 3-bit part: P=0x00 A=0x20 R=0x40 T=0x40 S=0x40 V=0xE0
  S4/3b model PpSsRrTtVv.      QEMU PpSsRrTtVv.      same
```
(표 일부, 실제 출력.) 3비트에선 서브가 사라져 SysTick 0x50 → 0x40, UART 와 동률 → 번호 15 가 먼저: `PpSsRrTtVv.` 모든 쌍의 선점 관계는 그대로(검사 통과). 마지막 줄의 "QEMU" 열은 모델 예측값이다(3비트 QEMU 머신은 돌리지 않음).

</details>

### 연습 4 — 응답 시간 예산: 상한식 vs 이벤트 시뮬레이션

C(진입·복귀 포함), T, D 를 ns 로 주고 각 소스의 최악 응답 R 을 계산한다. 선점 임계값(preemption threshold) 식: 시작 S = B + Σ(순서가 앞선 j)(⌊S/T_j⌋+1)·C_j, B = 같은 그룹·순서가 뒤인 것 중 최대 C; 끝 F = S + C + Σ(그룹이 더 급한 j)(⌈F/T_j⌉ − ⌊S/T_j⌋ − 1)·C_j. 같은 규칙의 이벤트 시뮬레이터(소스당 pending 1비트)로 동시 방출 · 차단자 1 ns 먼저 · 무작위 위상 3000 회를 돌려 최악 관측값 ≤ 상한인지 검사. 나쁜 설계 두 개(전부 0, ADC 를 PWM 그룹에)도.

<details><summary>답 (실제 출력)</summary>

```text
  -- design (PRIGROUP 4)
  source      byte    C(ns)    T(ns)    D(ns)  R_bound  R_sim_max  verdict
  MOTOR_PWM   0x00     2000    50000    10000     2000       2000  OK
  ADC         0x20    12000    50000    40000    14000      14000  OK
  UART_RX     0x40     1000    86806    86806    16500      16499  OK
  UART_TX     0x40      800    86806    86806    17300      17299  OK
  SysTick     0x50     1500  1000000  1000000    17300      17300  OK
  PendSV      0xF0    30000   694448   694448    47300      47300  OK
  -- starter: all 0 (PRIGROUP 0)
  MOTOR_PWM   0x00     2000    50000    10000      > D      45499  MISS (sim too)
  ADC         0x00    12000    50000    40000      > D      46499  MISS (sim too)
  -- ADC in PWM group (PRIGROUP 4)
  MOTOR_PWM   0x00     2000    50000    10000      > D      13999  MISS (sim too)
```
(발췌.) 설계는 상한과 시뮬레이션이 거의 일치(1 ns 차이는 차단자를 1 ns 먼저 넣은 위상). 전부 0 이면 PWM 이 PendSV(30 µs)·SysTick 에 순서로 밀리고 ADC 에 막혀 45.5 µs. ADC 를 PWM 그룹에 넣으면 ADC 12 µs 에 막혀 14 µs. 숫자 C/T/D 는 설계 가정이다(실측 아님).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M02_pr && /tmp/BM-M02_pr    # 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-M02_as && /tmp/BM-M02_as
# VM 에서: limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice exit=0
```

실행 스크립트와 gdb 스크립트:

<!-- code: run_qemu.sh -->

<!-- code: demo_nvic.gdb -->

## 10. 핵심 정리

- 선점은 **그룹** 우선순위가 엄격히 더 급할 때만. 동시 pending 선택은 그룹 → 서브 → 예외 번호. 숫자가 작을수록 급하다.
- 값은 바이트 **상위** `__NVIC_PRIO_BITS` 비트에 산다: `prio << (8 − bits)`. IRQ → IPR 바이트(0xE000E400+n), SysTick/PendSV → SHPR3[31:24]/[23:16].
- AIRCR 은 `0x05FA` 키와 함께, 필드를 지운 뒤 쓴다. `|=` 는 읽은 0xFA05 때문에 무시된다.
- 표는 "놓치면 무슨 일이 나나 + 데드라인 + ISR 길이": PWM(0) > ADC(1) > UART RX=TX, SysTick 같은 그룹(2) > PendSV(최하).
- 같은 그룹 = 서로 선점 안 함 = 공짜 상호 배타. 대신 같은 그룹의 긴 ISR 은 blocking 이 된다.
- 전부 0 이면 PendSV/SysTick 이 외부 IRQ 보다 먼저 뽑힌다. 우선순위 → ICPR → ISER 순서로 초기화.
- QEMU mps2-an385: IRQ 32 개, 우선순위 8비트, 꼬리 물기 모델링, 사이클 타이밍은 없음 → 예산은 RTA/실측으로.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5.4 Exception priorities and preemption, B3.2.4 ICSR, B3.2.6 AIRCR, B3.2.10–12 SHPR1–3, B3.4 NVIC (절 번호 확인 권장).
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552A) — 4.2 NVIC (ISER/ICER/ISPR/ICPR/IABR/IPR/STIR), 4.3 SCB.
- Arm, *Application Note 321: ARM Cortex-M Programming Guide to Memory Barrier Instructions* — NVIC 쓰기 뒤 DSB/ISB.
- Arm CMSIS 5 `CMSIS/Core/Include/core_cm3.h` — `__NVIC_SetPriorityGrouping`, `NVIC_EncodePriority`, `NVIC_DecodePriority`, `__NVIC_SetPriority`; `Device/ARM/CMSDK_CM3` (`__NVIC_PRIO_BITS`).
- QEMU v10.2.1 소스 (실제로 읽음): `hw/arm/mps2.c` (AN385 `num-irq` 32, IRQ 배선), `hw/intc/armv7m_nvic.c` (`num-prio-bits` 기본 8, 그룹 마스크 `~0U << (prigroup + 1)`, AIRCR 키 검사, STIR, 우선순위 선택 순서), `target/arm/tcg/m_helper.c` ("tailchaining to pending exception") — https://gitlab.com/qemu-project/qemu/-/tree/v10.2.1
- STMicroelectronics, STM32CubeF4 HAL `stm32f4xx_hal_cortex.h` (`NVIC_PRIORITYGROUP_0..4`), RM0090 (USART 에 FIFO 없음, ORE).
- FreeRTOS, "Running the RTOS on a ARM Cortex-M Core" (RTOS-Cortex-M3-M4.html) — PRIGROUP, `configMAX_SYSCALL_INTERRUPT_PRIORITY`; `portable/GCC/ARM_CM3/port.c` (`vPortValidateInterruptPriority`).
- Y. Wang, M. Saksena, "Scheduling Fixed-Priority Tasks with Preemption Threshold", RTCSA 1999 — 연습 4 의 상한식 모양 (확인 권장).
- 관련 레슨: BM-M01 (NVIC 레지스터 mock, 늦은 플래그 클리어), BM-B08 (QEMU 첫 부팅, startup), BM-B09 (세미호스팅), BM-M13 (fault 정책, AIRCR SYSRESETREQ), C-M14 (ISR↔main 공유 데이터), ARM-M10 (PRIMASK/BASEPRI 크리티컬 섹션), ARM-A08 (인터럽트 지연), COMM-M11 (UART DMA + IDLE), HWLAB-M05 (로직 분석기 타이밍).
