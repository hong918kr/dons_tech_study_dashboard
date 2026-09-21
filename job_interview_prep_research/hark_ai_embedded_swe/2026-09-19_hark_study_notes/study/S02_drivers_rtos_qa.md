# S02. 드라이버·BSP·RTOS·Cortex-M 면접 문항 — 29문항 모범 답안과 꼬리질문

> **목표**: Cortex-M 부트·예외, 주변장치 드라이버(UART/SPI/I2C/I2S/DMA), RTOS(FreeRTOS/Zephyr), 임베디드 C/C++ 질문에 30초 한국어 요지 + 그대로 말할 수 있는 영어 답으로 막힘 없이 답한다
> **선행**: C01(ARM Cortex·부트·툴체인), C02(제약 환경 C/C++), C03(BSP·주변장치 드라이버), C04(RTOS FreeRTOS/Zephyr)
> **사용법**: 질문을 먼저 소리 내어 답해 보고, 그다음 모범 답안을 읽는다.

---

## 0. 이 노트 쓰는 법

- 각 문항은 `왜 묻나 → 30초 답변 → English answer → 꼬리질문 → Don 스토리 연결` 순서다.
- English answer는 3~6문장이다. 외우기보다 **구조(정의 → 이유 → 실무 방법 → 함정)**를 기억한다.
- Don의 약점인 상용 RTOS 문항(Q15~Q24)은 "SSD FW 자체 스케줄러에서 같은 문제를 이렇게 풀었다"로 연결해서 경험 공백을 메운다.
- 숫자·API 이름은 실제 이름이다. 벤더마다 다른 것은 "(벤더마다 다름)"으로 표시했다.

| 절 | 문항 | 핵심 키워드 |
|---|---|---|
| 1. Cortex-M 코어 | Q01~Q06 | reset, vector table, VTOR, NVIC, BASEPRI, HardFault, PendSV |
| 2. BSP·드라이버 | Q07~Q14 | bring-up, UART DMA, SPI mode, I2C recovery, I2S/PDM, cache coherency, devicetree |
| 3. RTOS | Q15~Q24 | scheduling, priority inversion, FromISR, stack, watchdog, tickless, heap, Zephyr |
| 4. 임베디드 C/C++ | Q25~Q29 | volatile, barrier, const 배치, atomicity, C++ 서브셋 |

---

## 1. Cortex-M 코어

### Q01. Walk me through what happens from power-on reset until main() runs.

**왜 묻나**: JD "Develop BSPs". BSP의 첫 줄이 startup 코드다. 링커 스크립트와 C 런타임 초기화를 실제로 이해하는지 본다.

**30초 답변**: 리셋이 풀리면 코어가 vector table의 0번 word를 MSP 초기값으로, 1번 word를 PC(Reset_Handler)로 읽는다. Reset_Handler는 `SystemInit`(클럭, FPU 활성화 등)을 부르고, `.data`를 flash(LMA)에서 RAM(VMA)으로 복사하고 `.bss`를 0으로 채운 뒤, C++이면 `__libc_init_array`로 정적 생성자를 돌리고 `main`을 부른다. 위치 정보는 전부 링커 스크립트가 export한 심볼(`_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss` 등, 이름은 툴체인 템플릿마다 다름)이다.

**English answer**: On reset the core fetches two words from the vector table: word zero is the initial main stack pointer and word one is the address of the reset handler. The reset handler does the C runtime setup — it calls SystemInit for clocks and the FPU, copies the initialized data section from its load address in flash to its run address in RAM, and zero-fills bss. If there is C++ it runs the static constructors through `__libc_init_array`, and then it branches to main. All the addresses come from symbols the linker script exports, so when bring-up fails before main, I check the map file and the vector table first.

**꼬리질문**:
- Q: `.data`와 `.bss`는 왜 다르게 처리하나? → A: `.data`는 초기값이 있어 flash에 사본(LMA)이 있고 RAM(VMA)으로 복사해야 한다. `.bss`는 0이므로 flash 공간을 쓰지 않고 크기만 기록했다가 0으로 채운다.
- Q: FPU 코드를 `main` 전에 쓰면 왜 UsageFault(NOCP)가 나나? → A: `SCB->CPACR`에서 CP10/CP11 접근 권한을 켜기 전에 FP 명령을 실행했기 때문이다. `SystemInit`에서 `SCB->CPACR |= (0xFUL << 20)` 후 `__DSB(); __ISB();`를 한다.
- Q: `main` 전에 멈추면 무엇부터 보나? → A: 디버거로 reset 직후 PC/SP 값, vector table 0/1번 word, 클럭 설정(PLL lock 대기 무한 루프), `.data` 복사 루프를 본다.

**Don 스토리 연결**: Solidigm FPGA pre-silicon bring-up에서 Cortex-R/M0+ 첫 부팅을 Trace32로 따라간 경험. "첫 부팅이 안 될 때 제일 먼저 reset vector와 SP 값, 그리고 map 파일을 봤다"고 말한다.

### Q02. What is in the vector table, and how would you relocate it — for example when a bootloader jumps to an application?

**왜 묻나**: OTA/bootloader(JD의 OTA 인프라)와 BSP가 만나는 지점이다.

**30초 답변**: vector table은 32비트 word 배열이다. 0번이 초기 MSP, 1번이 Reset, 이후 NMI, HardFault, MemManage, BusFault, UsageFault, SVCall, PendSV, SysTick 같은 시스템 예외, 16번부터가 외부 IRQ다. ARMv7-M/ARMv8-M에서는 `SCB->VTOR`에 새 주소를 쓰면 이동하고, 정렬은 테이블 크기 이상의 2의 거듭제곱(최소 128바이트)이어야 한다. 부트로더는 인터럽트를 끄고 주변장치를 원상태로 돌린 뒤, VTOR 설정, 앱 테이블의 MSP 로드, 앱 Reset_Handler로 점프한다.

**English answer**: The vector table is an array of words: the initial stack pointer, the reset handler, the system exceptions like HardFault, SVCall, PendSV and SysTick, and from entry sixteen onward the device interrupts. On Cortex-M3 and later you relocate it by writing SCB->VTOR, and the table has to be aligned to a power of two at least as large as the table. When a bootloader hands off to an application, it disables interrupts, de-initializes the peripherals it used, sets VTOR to the application's table, loads MSP from the first word, and jumps to the application's reset handler. The common bug is leaving a peripheral interrupt pending, so the application takes an interrupt before it has set up the handler state.

**꼬리질문**:
- Q: Cortex-M0에는 VTOR가 없는데? → A: M0에는 없고 M0+에서는 옵션이다. 없으면 벤더의 remap 기능을 쓰거나 부트로더가 RAM에 vector table을 복사하고 SYSCFG remap을 하는 식이다(벤더마다 다름).
- Q: 점프 직전 SysTick은? → A: SysTick도 끄고 pending bit를 지운다. 안 그러면 앱이 SysTick 설정 전에 SysTick 예외를 받는다.
- Q: 왜 `__DSB(); __ISB();`가 필요한가? → A: VTOR 쓰기가 완료되고 파이프라인이 새 설정을 보도록 보장하기 위해서다.

```c
#include <stdint.h>
#include "stm32f4xx.h"   /* 예시: CMSIS device header (벤더마다 다름) */

#define APP_BASE 0x08020000UL   /* 가상의 앱 시작 주소 */

typedef void (*reset_fn_t)(void);

void jump_to_app(void)
{
    uint32_t app_sp    = ((volatile uint32_t *)APP_BASE)[0];
    uint32_t app_reset = ((volatile uint32_t *)APP_BASE)[1];

    __disable_irq();
    SysTick->CTRL = 0;                       /* SysTick 정지 */
    for (int i = 0; i < 8; i++) {            /* 모든 NVIC IRQ disable + pending clear */
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    SCB->VTOR = APP_BASE;
    __DSB();
    __ISB();
    __set_MSP(app_sp);
    __enable_irq();                          /* 앱이 다시 끄고 켤 수도 있음 */
    ((reset_fn_t)app_reset)();               /* Thumb 비트는 테이블 값에 이미 1로 들어 있음 */
}
```

**Don 스토리 연결**: SSD FW의 firmware download/activate 흐름(새 이미지를 검증 후 부트 경로로 넘김)과 같은 구조라고 연결한다.

### Q03. Explain NVIC priorities on Cortex-M. What does a lower number mean, and what is priority grouping?

**왜 묻나**: RTOS 인터럽트 설정 실수(특히 FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`)의 근본이다.

**30초 답변**: Cortex-M은 **숫자가 낮을수록 우선순위가 높다**. 구현된 비트 수는 칩마다 다르다(`__NVIC_PRIO_BITS`, 예: nRF52는 3비트, STM32는 보통 4비트). 레지스터의 상위 비트만 쓰이며 CMSIS `NVIC_SetPriority`가 시프트를 해 준다. ARMv7-M 이상에서는 PRIGROUP으로 preemption priority와 sub-priority를 나눈다. preemption priority가 높은 것만 선점하고, sub-priority는 동시에 pending일 때 순서만 정한다. Reset(-3), NMI(-2), HardFault(-1)는 고정 우선순위다.

**English answer**: On Cortex-M a numerically lower priority value means higher urgency, and only the top bits of each priority byte are implemented — how many is chip-specific, given by `__NVIC_PRIO_BITS`. On ARMv7-M and ARMv8-M Mainline you can split those bits with PRIGROUP into a preemption group and a sub-priority; only the group priority decides preemption, the sub-priority just orders pending interrupts. Reset, NMI and HardFault have fixed negative priorities above everything configurable. In practice with an RTOS I usually put all bits into preemption priority so the RTOS mask logic is simple, and I document a priority map for the whole system.

**꼬리질문**:
- Q: tail-chaining과 late arrival은? → A: tail-chaining은 한 ISR이 끝날 때 pending ISR로 unstacking/stacking 없이 바로 넘어가는 것, late arrival은 stacking 중 더 높은 IRQ가 오면 그 핸들러를 먼저 실행하는 것이다.
- Q: 같은 preemption priority 두 개가 동시에 pending이면? → A: sub-priority, 그다음 exception number가 작은 쪽이 먼저다.
- Q: ARMv6-M(M0/M0+)에서는? → A: 2비트(4단계) 우선순위만 있고 PRIGROUP이 없다.

**Don 스토리 연결**: SSD FW에서 NVMe doorbell, DMA 완료, 타이머 인터럽트의 우선순위 맵을 설계·디버깅한 경험이 있으면 그 예를 쓴다.

### Q04. How do you implement a critical section on Cortex-M? PRIMASK versus BASEPRI.

**왜 묻나**: ISR과 공유 데이터 보호, RTOS 커널 구현 이해.

**30초 답변**: PRIMASK=1(`__disable_irq()`, `cpsid i`)은 NMI와 HardFault를 뺀 모든 인터럽트를 막는다. BASEPRI는 ARMv7-M 이상에만 있고 설정값 **이상의 숫자**(= 같거나 낮은 우선순위) 인터럽트만 막는다. 그래서 FreeRTOS Cortex-M3/M4 포트는 BASEPRI로 critical section을 만들어, 커널을 호출하지 않는 최상위 인터럽트(예: 모터 제어, 오디오 DMA)는 지연 없이 돌게 한다. 중첩을 고려해 이전 상태를 저장·복원해야 한다.

**English answer**: The blunt tool is PRIMASK — `__disable_irq` masks every configurable interrupt. On ARMv7-M and later, BASEPRI masks only interrupts whose priority value is equal to or greater than the threshold, so truly time-critical interrupts above that level still run. FreeRTOS on Cortex-M3 and M4 uses exactly that: kernel critical sections raise BASEPRI to `configMAX_SYSCALL_INTERRUPT_PRIORITY`. Whatever I use, I save and restore the previous mask so critical sections nest correctly, and I keep them to a few dozen cycles because they add latency to everything below them.

**꼬리질문**:
- Q: FreeRTOS에서 `FromISR` API를 부르는 ISR의 우선순위 규칙은? → A: 숫자가 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이상(= 논리적으로 같거나 낮음)이어야 한다. 더 높은 우선순위 ISR에서 부르면 커널 자료구조가 깨진다. `configASSERT`를 켜면 포트가 검사해 준다.
- Q: 왜 `configMAX_SYSCALL_INTERRUPT_PRIORITY`는 시프트된 값인가? → A: BASEPRI 레지스터에 그대로 쓰는 값이라 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS)`로 정의한다.
- Q: FAULTMASK는? → A: HardFault까지 막는다(NMI 제외). 일반 코드에서는 거의 안 쓴다.

```c
#include <stdint.h>
#include "cmsis_compiler.h"   /* __get_PRIMASK 등 CMSIS 인트린식 */

static inline uint32_t crit_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;              /* 이전 상태 저장 → 중첩 가능 */
}

static inline void crit_exit(uint32_t primask)
{
    __set_PRIMASK(primask);      /* 원래 켜져 있었을 때만 다시 켜짐 */
}
```

### Q05. You get a HardFault in the field. How do you find the root cause?

**왜 묻나**: JD "Debug complex hardware-software interactions". 필드 크래시 분석은 스타트업 1세대 기기의 일상이다.

**30초 답변**: HardFault 핸들러에서 EXC_RETURN(LR) 비트 2로 MSP/PSP 중 어느 스택에 frame이 쌓였는지 판단하고, stacked frame(R0~R3, R12, LR, PC, xPSR)에서 **PC와 LR**을 꺼낸다. 동시에 CFSR(0xE000ED28), HFSR(0xE000ED2C), MMFAR, BFAR를 읽는다. 이 값들을 `.noinit` RAM이나 flash에 저장하고 리셋, 다음 부팅에 텔레메트리로 올린다. PC를 map 파일/`addr2line`으로 함수·줄로 바꾸면 대부분 원인이 보인다.

**English answer**: First I make sure the fault handler captures state: it checks bit two of EXC_RETURN to know whether the frame is on MSP or PSP, then pulls the stacked PC, LR and xPSR, plus the fault status registers — CFSR, HFSR, and MMFAR or BFAR when their valid bits are set. That record goes into a no-init RAM region or flash, the device resets, and on the next boot it is uploaded as a crash report. Offline I run `addr2line` on the PC and LR against the exact ELF of that build. The CFSR bits tell me the class: IMPRECISERR usually means a buffered write to a bad address, so the PC is after the culprit; INVSTATE often means a function pointer without the Thumb bit; STKERR points to stack overflow.

**꼬리질문**:
- Q: HFSR.FORCED가 1이면? → A: MemManage/BusFault/UsageFault가 disable 상태라 HardFault로 escalate된 것이다. `SCB->SHCSR`로 개별 fault를 켜면 더 구체적인 핸들러에서 잡힌다.
- Q: imprecise bus fault를 precise로 만들려면? → A: 디버깅 중에는 `SCnSCB->ACTLR`의 DISDEFWBUF로 write buffer를 끄면 된다(M3/M4). 성능이 떨어지므로 디버깅용이다.
- Q: M0+에서는? → A: ARMv6-M은 CFSR가 없고 HardFault 하나뿐이라 stacked PC와 코드 리뷰에 더 의존한다.

```c
#include <stdint.h>

typedef struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr, cfsr, hfsr, mmfar, bfar;
} crash_t;

__attribute__((section(".noinit"))) crash_t g_crash;   /* 리셋 후에도 유지 */

void hardfault_c(uint32_t *frame)
{
    g_crash.r0 = frame[0]; g_crash.r1 = frame[1];
    g_crash.r2 = frame[2]; g_crash.r3 = frame[3];
    g_crash.r12 = frame[4]; g_crash.lr = frame[5];
    g_crash.pc = frame[6]; g_crash.xpsr = frame[7];
    g_crash.cfsr  = *(volatile uint32_t *)0xE000ED28UL;
    g_crash.hfsr  = *(volatile uint32_t *)0xE000ED2CUL;
    g_crash.mmfar = *(volatile uint32_t *)0xE000ED34UL;
    g_crash.bfar  = *(volatile uint32_t *)0xE000ED38UL;
    for (;;) { }   /* 실제로는 NVIC_SystemReset() */
}

__attribute__((naked)) void HardFault_Handler(void)   /* ARMv7-M 이상 (ITE 사용) */
{
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b hardfault_c   \n");
}
```

**Don 스토리 연결**: SSD FW의 error reporting/handling과 NVMe telemetry(크래시 덤프를 고객이 로그 페이지로 가져가는 구조)가 거의 같은 설계다. "필드 기기에서 재현 안 되는 fault는 덤프 설계가 먼저"라고 말한다.

### Q06. How does an RTOS context switch work on Cortex-M? Why PendSV?

**왜 묻나**: RTOS를 "API 사용자"가 아니라 동작 원리로 아는지.

**30초 답변**: SysTick(또는 low-power timer)이 tick을 주고, 커널이 더 높은 우선순위 task가 ready라고 판단하면 PendSV를 pending한다. PendSV는 **가장 낮은 우선순위**라 다른 ISR이 다 끝난 뒤에 실행된다. 하드웨어가 이미 R0~R3, R12, LR, PC, xPSR을 PSP에 쌓았으므로 PendSV는 R4~R11(FPU를 쓰면 S16~S31)만 저장하고, TCB에 PSP를 보관한 뒤 다음 task의 PSP를 복원하고 EXC_RETURN으로 복귀한다. task는 PSP, 커널과 ISR은 MSP를 쓴다.

**English answer**: The tick interrupt or any API that readies a higher-priority task sets PendSV pending. PendSV runs at the lowest priority, so the switch happens only after all nested interrupts have finished, which keeps interrupt latency low and avoids switching in the middle of an ISR. On entry the hardware has already pushed the caller-saved registers onto the task's PSP, so the handler saves R4 to R11, and the high FPU registers if the task used the FPU, stores PSP in the task control block, picks the next task, restores its registers and returns with an EXC_RETURN that selects thread mode on PSP. SVC is typically used once to start the first task.

**꼬리질문**:
- Q: lazy FP stacking은? → A: FPU 컨텍스트 공간만 예약하고 실제 저장은 ISR이 FP 명령을 쓸 때 한다(FPCCR.LSPEN). 인터럽트 지연을 줄인다.
- Q: 왜 ISR 스택이 따로 있나? → A: ISR이 MSP 하나를 공유하므로 각 task 스택에 ISR 최악 깊이를 더할 필요가 없다. 대신 MSP 크기를 ISR 중첩 최악 경우로 잡는다.

**Don 스토리 연결**: SSD FW 자체 스케줄러(멀티코어 task 전환) 경험이 있으면 "우리 스케줄러는 cooperative였고, 선점형의 차이는 PendSV로 인한 비동기 전환"이라고 비교한다.

---

## 2. BSP·주변장치 드라이버

### Q07. You receive a brand-new EVT board. How do you bring up the firmware?

**왜 묻나**: JD "hardware bring-up", "read schematics". 순서와 판단력을 본다.

**30초 답변**: 코드 전에 회로도로 전원 트리, 리셋, 부트 핀, 디버그 헤더, 클럭 소스를 확인한다. 전원 레일 전압과 시퀀스를 스코프로 보고, SWD로 붙어 IDCODE를 읽는다. 그다음 내부 RC 클럭으로 최소 이미지(LED 토글) → UART 로그 → 외부 크리스털/PLL → SysTick → 버스별 드라이버(I2C 스캔, SPI ID 레지스터 읽기) 순서로 하나씩 늘린다. 한 번에 하나만 바꾸고 결과를 로그로 남긴다.

**English answer**: Before flashing anything I read the schematic for the power tree, reset and boot-mode pins, the debug connector and the clock sources, and I measure the rails and their sequencing with a scope. Then I attach over SWD and confirm the core responds. I start from the internal oscillator with a GPIO toggle, then get a UART console, then switch to the external crystal and PLL, then timers, and then each bus — an I2C scan, reading a WHO_AM_I register over SPI. I change one thing at a time, and when something fails I split hardware from software by probing the bus with a logic analyzer before touching code.

**꼬리질문**:
- Q: SWD 연결이 안 되면? → A: 전원, 리셋 라인 상태, SWDIO/SWCLK 핀 충돌(핀 재사용 펌웨어), 저전력 모드로 디버그 포트가 꺼진 경우(connect under reset), 읽기 보호 설정을 본다.
- Q: I2C 스캔에 센서가 안 보이면? → A: 센서 전원 레일·enable 핀, pull-up 존재와 값, 주소 핀 strap, 레벨 시프터 방향을 LA/스코프로 확인한다.

**Don 스토리 연결**: 가장 강한 영역이다. Apple에서 새 무선 칩을 플랫폼에 올릴 때 bring-up → NPI → MP를 겪은 순서, Solidigm FPGA bring-up 순서를 그대로 말한다.

### Q08. Design a UART receive driver that never loses bytes at 1 Mbaud while the CPU is busy.

**왜 묻나**: 인터럽트 vs DMA 판단, 링버퍼, 경계 조건.

**30초 답변**: 1 Mbaud, 8N1이면 바이트당 10 µs다. 바이트마다 인터럽트면 초당 10만 번이라 부담이 크고, 다른 ISR이 10 µs 이상 막으면 FIFO가 작을 때 overrun이 난다. 그래서 **원형 DMA + idle line 검출(또는 half/full 완료 인터럽트)**로 받는다. DMA가 링버퍼에 계속 쓰고, 소프트웨어는 DMA의 남은 카운트로 write 위치를 계산해 새 바이트를 task로 넘긴다. overrun·framing error 카운터는 텔레메트리로 남긴다.

**English answer**: At one megabaud with 8N1 a byte arrives every ten microseconds, so a per-byte interrupt is a hundred thousand interrupts a second and any ISR that blocks longer than the hardware FIFO depth causes an overrun. I would run the receiver into a circular DMA buffer and take an interrupt on half-transfer, transfer-complete and line-idle. In the handler I read the DMA's remaining count to compute the write index and hand the new span to a task through a stream buffer or a notification. I also count overrun, framing and noise errors, because those counters are what tell you in the field whether it is a baud mismatch or a latency problem.

**꼬리질문**:
- Q: idle line 인터럽트가 없는 MCU라면? → A: 타이머로 주기적으로 DMA 카운트를 폴링하거나, 수신 타임아웃 기능(Zephyr async UART `uart_rx_enable()`의 timeout 인자처럼)을 쓴다.
- Q: flow control은? → A: RTS/CTS 하드웨어 flow control을 켜면 수신 측이 바쁠 때 송신을 멈출 수 있다. 무선 콤보칩 HCI UART에서는 거의 필수다.

**Don 스토리 연결**: SSD FW에서 DMA descriptor와 링 구조를 다룬 경험, Apple에서 무선 칩 호스트 인터페이스 장애를 본 경험.

### Q09. Explain SPI modes. When do you choose SPI versus I2C?

**왜 묻나**: JD의 SPI/I2C 드라이버. 기본 개념을 정확히 말하는지.

**30초 답변**: CPOL은 idle 클럭 레벨, CPHA는 첫 번째(0) 또는 두 번째(1) 에지에서 샘플링하는지다. Mode 0(0,0)과 Mode 3(1,1)은 상승 에지에서 샘플링, Mode 1(0,1)과 Mode 2(1,0)는 하강 에지에서 샘플링한다. SPI는 push-pull, full-duplex, 수십 MHz까지 가능하고 CS 핀이 기기마다 필요하다. I2C는 2선 open-drain, 주소 기반 멀티 드롭, 속도는 100 kHz/400 kHz/1 MHz(Fm+)로 느리다. 고속 데이터(플래시, 디스플레이, IMU FIFO 대량 읽기)는 SPI, 저속 제어·설정(PMIC, fuel gauge, 온도 센서)은 I2C를 쓴다.

**English answer**: CPOL sets the idle level of the clock and CPHA sets whether data is sampled on the first or the second edge, which gives four modes; modes zero and three sample on the rising edge, modes one and two on the falling edge. SPI is push-pull and full-duplex, runs at tens of megahertz, and needs a chip select per device. I2C is two open-drain wires with addressing and acknowledgements, typically 100 or 400 kilohertz, up to 1 megahertz in Fast-mode Plus. So I use SPI for bandwidth — flash, displays, draining a sensor FIFO — and I2C for low-rate control devices like the PMIC, fuel gauge and temperature sensors where pin count matters more than speed.

**꼬리질문**:
- Q: SPI에서 데이터가 1비트씩 밀려 읽히면? → A: mode 불일치 가능성이 가장 크다. LA로 에지와 데이터 전이 시점을 보고 데이터시트 타이밍도와 비교한다.
- Q: I2C 속도를 결정하는 물리 요소는? → A: pull-up 저항과 버스 정전용량이 만드는 상승 시간(RC)이다. 스펙은 모드별 최대 rise time과 최대 버스 정전용량(Standard/Fast 400 pF)을 정한다.

**Don 스토리 연결**: Apple에서 I2C·SPMI·RFFE 장애를 다뤘으므로 "SPMI와 RFFE도 MIPI의 2선 제어 버스이고, I2C와 비교하면 ..."으로 폭넓게 답할 수 있다.

### Q10. An I2C sensor stops responding and SDA is stuck low. What happened and how does the driver recover?

**왜 묻나**: 실제 양산에서 자주 나는 문제. 버스 복구를 아는지가 실무 경험의 신호다.

**30초 답변**: 흔한 원인은 마스터가 트랜잭션 중간에 리셋되거나 글리치로 클럭을 놓쳐서, 슬레이브가 읽기 데이터의 0 비트를 출력하는 중에 멈춘 것이다. 슬레이브는 SCL을 기다리며 SDA를 계속 low로 잡는다. 복구는 I2C 스펙(UM10204)의 bus clear 방법대로 SCL을 GPIO로 전환해 **최대 9번 토글**하면서 SDA가 풀리는지 보고, 풀리면 STOP 조건을 만든 뒤 I2C 주변장치를 재초기화한다. 안 풀리면 센서 전원 사이클이나 리셋 핀을 쓴다.

**English answer**: The classic cause is that the master reset or glitched in the middle of a read, while the slave was driving a zero bit, so the slave is still waiting for clocks and holds SDA low. Per the bus-clear procedure in the I2C specification, the driver switches SCL to a GPIO and clocks it up to nine times until the slave releases SDA, then generates a STOP condition and re-initializes the controller. If SDA stays low after that, the recovery path is a hardware reset or a power cycle of that device, so I make sure the board has a reset line or a switchable rail for critical sensors. I also add timeouts on every transfer, so a stuck bus becomes an error code and a counter, not a hung task.

**꼬리질문**:
- Q: clock stretching은? → A: 슬레이브가 준비가 안 됐을 때 SCL을 low로 잡아 마스터를 기다리게 하는 것이다. 마스터가 지원하지 않거나 타임아웃이 짧으면 오류가 난다.
- Q: NACK의 의미 두 가지는? → A: 주소 NACK는 기기가 없거나 바쁨, 데이터 NACK는 레지스터/명령 거부. 마스터 읽기의 마지막 바이트 NACK는 정상(읽기 종료 신호)이다.
- Q: Zephyr에서는? → A: `i2c_recover_bus()` API가 있고 드라이버가 지원하면 쓸 수 있다(드라이버마다 지원 여부 다름).

```c
#include <stdbool.h>
#include <stdint.h>

/* 보드별 GPIO 헬퍼 (가상의 BSP 함수) */
extern void scl_set(bool high);
extern bool sda_read(void);
extern void sda_set(bool high);
extern void delay_us(uint32_t us);

bool i2c_bus_clear(void)
{
    for (int i = 0; i < 9 && !sda_read(); i++) {
        scl_set(false); delay_us(5);     /* 100 kHz 반주기 정도 */
        scl_set(true);  delay_us(5);
    }
    if (!sda_read()) {
        return false;                    /* 전원/리셋으로 복구해야 함 */
    }
    sda_set(false); delay_us(5);         /* STOP: SCL high 상태에서 SDA low → high */
    scl_set(true);  delay_us(5);
    sda_set(true);  delay_us(5);
    return true;
}
```

**Don 스토리 연결**: Apple I2C 장애 root cause 사례 1건을 STAR로(증상 → 프로토콜 분석기 캡처 → 원인 → 수정 → 재발 방지). EVT 보드 일부만 실패하는 경우 pull-up, 전원 시퀀스, 온도까지 좁혀 간 방법을 강조한다.

### Q11. Describe the I2S frame. How would you capture audio from two microphones for a wake-word pipeline?

**왜 묻나**: JD에 I2S가 명시돼 있고 Hark는 음성 AI 기기다. Don의 갭 영역이다.

**30초 답변**: I2S는 BCLK(bit clock), LRCLK/WS(word select, 좌/우 채널), SD(데이터) 3선이다. Philips I2S 형식은 WS가 바뀐 뒤 **한 BCLK 늦게 MSB**가 나온다. BCLK = 샘플레이트 × 슬롯 비트 수 × 채널 수라서, 16 kHz, 32비트 슬롯, 스테레오면 1.024 MHz다. 디지털 MEMS 마이크는 I2S 출력형과 PDM 출력형이 있다. PDM은 1비트 스트림을 1~3 MHz대 클럭으로 보내고, 두 마이크가 한 데이터선을 클럭의 상승/하강 에지로 나눠 쓴다. MCU의 PDM 주변장치가 decimation 필터로 PCM을 만들고, DMA ping-pong 버퍼로 task에 넘긴다.

**English answer**: I2S has a bit clock, a word-select or LR clock that marks left and right, and a serial data line; in the Philips format the MSB appears one bit clock after the word-select edge. The bit clock is sample rate times slot width times channels, so sixteen kilohertz, thirty-two-bit slots, stereo is 1.024 megahertz. For two digital MEMS microphones I'd likely use PDM mics sharing one data line, one on each clock edge, and let the MCU's PDM peripheral decimate to 16 kHz PCM. The peripheral writes into a ping-pong DMA buffer — say 10 millisecond halves — and each half-complete interrupt notifies the audio task, which runs the feature extraction while DMA fills the other half.

**꼬리질문**:
- Q: 누가 마스터 클럭을 만드나? → A: 코덱/MCU 중 하나가 BCLK/LRCLK master. 클럭 도메인이 둘이면 샘플레이트 드리프트로 버퍼 underrun/overrun이 생기므로 한쪽을 master로 정한다.
- Q: TDM은? → A: 한 프레임에 2개 이상의 슬롯을 넣어 여러 채널(마이크 어레이)을 한 데이터선으로 보내는 확장이다.
- Q: 오디오 드롭의 원인은? → A: 처리 시간이 버퍼 반쪽 시간(예: 10 ms)을 넘거나, 더 높은 우선순위 task/ISR이 오래 막는 경우. half/full 콜백에서 이전 버퍼가 아직 처리 중이면 overrun 카운터를 올린다.

**Don 스토리 연결**: 오디오는 경험이 없다고 솔직히 말하되, "SSD 데이터 경로의 DMA descriptor·링버퍼와 같은 스트리밍 문제"로 연결한다.

### Q12. Polling, interrupts, or DMA — how do you decide for a given peripheral?

**왜 묻나**: 설계 판단력과 전력 감각.

**30초 답변**: 기준은 데이터율, 지연 요구, CPU·전력 비용이다. 한 번 읽고 끝나는 설정 레지스터나 수 µs 안에 끝나는 짧은 대기는 polling이 가장 단순하다. 드물고 불규칙한 이벤트(버튼, 센서 data-ready)는 인터럽트, 연속 스트림이나 큰 블록(오디오, UART 고속, SPI 플래시)은 DMA다. 저전력 기기에서는 DMA가 CPU를 sleep에 둘 수 있어서 전력상 유리하지만, 작은 전송에서는 DMA 설정 오버헤드가 더 클 수 있다.

**English answer**: I decide by data rate, latency requirement and what the CPU and power cost is. Polling is fine for one-off configuration and waits of a few microseconds. Interrupts fit sporadic events like a data-ready line or a button. DMA is for streams and blocks — audio, high-baud UART, SPI flash, draining a sensor FIFO — because the CPU can sleep while bytes move. On a battery device DMA is usually the power win, but for a two-byte register read the setup overhead of DMA is higher than just doing it, so a real driver often uses both paths depending on the length.

**꼬리질문**:
- Q: 인터럽트 폭주(interrupt storm)가 나면? → A: 레벨 트리거 소스의 원인을 안 지워서 반복 진입하는 경우가 흔하다. ISR 끝에서 플래그를 지우고, 필요하면 `__DSB()`로 쓰기 완료를 보장한다.
- Q: polling이 전력에 나쁜 이유는? → A: 기다리는 동안 CPU가 active 전류를 계속 쓰기 때문이다. `WFI`로 sleep하고 인터럽트로 깨는 게 보통 낫다.

### Q13. How do you design double-buffered DMA for audio, and what changes on a Cortex-M7 with a data cache?

**왜 묻나**: on-device AI 파이프라인과 DMA/캐시 일관성.

**30초 답변**: 버퍼 하나를 반으로 나누고 DMA를 circular 모드로 돌려 half-transfer, transfer-complete 인터럽트마다 방금 찬 절반을 task에 넘긴다. 처리 데드라인은 반쪽이 다시 채워지기 전까지다. D-cache가 있는 M7에서는 DMA가 캐시를 거치지 않으므로, RX 버퍼는 CPU가 읽기 전에 `SCB_InvalidateDCache_by_Addr`, TX 버퍼는 DMA 시작 전에 `SCB_CleanDCache_by_Addr`를 해야 한다. 버퍼는 캐시 라인(32바이트)에 정렬하고 크기도 32의 배수로 한다. 또는 MPU로 해당 영역을 non-cacheable로 둔다.

**English answer**: I use one circular DMA buffer split in two halves, with interrupts at half and full, and each interrupt hands the just-filled half to the processing task; the deadline is simply the time to fill the other half. On a Cortex-M7 with data cache, DMA bypasses the cache, so before the CPU reads a received half I invalidate that range, and before DMA transmits a buffer I clean it. The buffers must be aligned to the 32-byte cache line and sized in multiples of it, otherwise an invalidate can throw away neighbouring data. The simpler alternative is to put DMA buffers in a region the MPU marks non-cacheable, or in a tightly coupled memory that DMA can reach, depending on the chip.

**꼬리질문**:
- Q: 정렬 안 된 버퍼를 invalidate하면? → A: 같은 캐시 라인에 있는 다른 변수의 dirty 데이터가 사라질 수 있다.
- Q: Cortex-M4에도 필요한가? → A: M4 코어 자체에는 D-cache가 없다. 다만 벤더가 붙인 시스템 캐시/flash accelerator가 있으면 확인해야 한다(벤더마다 다름).

**Don 스토리 연결**: SSD FW에서 DMA와 캐시/SRAM 버퍼 일관성 문제(Cortex-R은 캐시 있음)를 다룬 경험이 있으면 가장 좋은 연결 고리다.

### Q14. How do you structure a driver so application code is portable across board revisions? How does Zephyr approach this?

**왜 묻나**: BSP 설계와 Zephyr 이해(RTOS 요건).

**30초 답변**: 계층을 나눈다. 레지스터 접근(LL/HAL) → 버스 드라이버(I2C, SPI) → 디바이스 드라이버(센서) → 앱이 쓰는 추상 API. 보드마다 다른 것(핀, 버스 번호, 주소, 인터럽트 선)은 코드가 아니라 설정 데이터로 뺀다. Zephyr는 이것을 **devicetree**(하드웨어 기술)와 **Kconfig**(소프트웨어 기능 선택)로 하고, 드라이버는 `DEVICE_DT_GET(DT_NODELABEL(...))`로 인스턴스를 얻고 `device_is_ready()`로 확인한 뒤 `sensor_sample_fetch()` 같은 서브시스템 API를 쓴다. board overlay 파일만 바꾸면 EVT→DVT 핀 변경을 흡수한다.

**English answer**: I layer it: register access at the bottom, then bus drivers, then device drivers, then a small application-facing API, and everything board-specific — pins, bus instance, addresses, interrupt lines — lives in configuration, not in code. Zephyr formalizes that: the devicetree describes the hardware, Kconfig selects the software features, and a driver gets its instance with `DEVICE_DT_GET` and checks `device_is_ready` before use. Application code calls generic subsystem APIs such as the sensor or I2C API, so moving from an EVT to a DVT board with different pins is an overlay change. That matters a lot in a first-generation product where the board spins several times.

**꼬리질문**:
- Q: devicetree는 런타임에 파싱하나? → A: Zephyr에서는 빌드 타임에 C 매크로로 생성된다(Linux는 런타임에 DTB를 파싱).
- Q: I2C 센서를 Zephyr에서 읽는 코드는? → A: `static const struct i2c_dt_spec dev = I2C_DT_SPEC_GET(DT_NODELABEL(imu));` 다음 `i2c_write_read_dt(&dev, &reg, 1, buf, len)`.

---

## 3. RTOS

### Q15. How does the FreeRTOS scheduler decide which task runs?

**왜 묻나**: JD "RTOS task scheduling". 기본기.

**30초 답변**: 고정 우선순위 선점형이다. ready 상태 중 **가장 높은 우선순위** task가 실행되고, FreeRTOS에서는 숫자가 클수록 우선순위가 높다(0이 idle). 같은 우선순위끼리는 `configUSE_TIME_SLICING`이 켜져 있으면 tick마다 round-robin한다. task가 block(큐 대기, delay)되면 다음 ready task로 넘어간다. 그래서 설계의 핵심은 우선순위 배정과 "높은 우선순위 task는 짧게 일하고 block된다"는 규칙이다.

**English answer**: FreeRTOS uses fixed-priority preemptive scheduling: the highest-priority task in the Ready state always runs, and a task becoming ready at higher priority preempts immediately, not at the next tick. In FreeRTOS a larger number means higher priority, with the idle task at zero. Tasks of equal priority time-slice on each tick if `configUSE_TIME_SLICING` is enabled. So the real design work is assigning priorities by deadline — the audio capture task above the sensor task above logging — and making sure high-priority tasks do short work and then block on a queue or notification.

**꼬리질문**:
- Q: Zephyr 우선순위는? → A: 반대로 **숫자가 작을수록 높다**. 음수는 cooperative thread(선점 안 됨), 0 이상은 preemptible thread다. 두 RTOS를 오가면 가장 흔한 실수다.
- Q: starvation은 어떻게 막나? → A: 높은 우선순위 task가 busy loop를 돌지 않게 하고, idle task가 돌아야 하는 일(heap 해제, idle hook)이 있다면 CPU 사용률을 모니터링한다.
- Q: 우선순위를 어떻게 정하나? → A: 주기적 task는 주기가 짧을수록 높게(Rate Monotonic). Q24 참고.

### Q16. What is priority inversion? How does priority inheritance fix it, and what doesn't it fix?

**왜 묻나**: 가장 많이 나오는 RTOS 질문 중 하나.

**30초 답변**: 낮은 우선순위 L이 mutex를 잡고 있는데 높은 H가 그 mutex를 기다리면, 중간 우선순위 M이 L을 선점해서 H가 **M 때문에 무기한** 기다리게 된다. 1997년 Mars Pathfinder(VxWorks)에서 watchdog 리셋을 일으킨 사례가 유명하다. priority inheritance는 H가 기다리는 동안 L의 우선순위를 H로 올려 M이 끼어들지 못하게 한다. FreeRTOS mutex(`xSemaphoreCreateMutex`)와 Zephyr `k_mutex`는 inheritance를 지원한다. 하지만 체인 blocking이나 deadlock은 해결하지 못하므로 lock 보유 시간을 짧게 하고 lock 순서를 정한다.

**English answer**: Priority inversion is when a high-priority task waits on a mutex held by a low-priority task, and a medium-priority task that doesn't need the mutex preempts the low one, so the high task is effectively blocked by the medium one for an unbounded time — the Mars Pathfinder reset problem. Priority inheritance temporarily raises the holder to the priority of the highest waiter so it can finish the critical section and release the lock. FreeRTOS mutexes and Zephyr's `k_mutex` do this; binary semaphores do not, which is why you use a mutex for mutual exclusion. Inheritance bounds the inversion but doesn't prevent deadlock or long chains, so I still keep critical sections short and acquire locks in a fixed order.

**꼬리질문**:
- Q: priority ceiling protocol은? → A: mutex에 미리 ceiling 우선순위를 정해 두고 잡는 순간 그 우선순위로 올린다. deadlock도 막지만 설정이 필요하다.
- Q: ISR이 mutex를 쓸 수 없는 이유는? → A: ISR에는 inherit할 task 우선순위가 없고 block할 수도 없다.

```
우선순위: H > M > L
t0  L 실행, mutex 획득
t1  H ready → L 선점 → H가 mutex 요청 → block
t2  (inheritance 없음) M ready → L을 선점 → M이 오래 실행 → H 대기 지속  ← inversion
t2' (inheritance 있음) L이 H 우선순위로 승격 → M은 못 끼어듦 → L이 unlock → H 실행
```

**Don 스토리 연결**: SSD FW에서 공유 자원(NAND 채널, 버퍼 풀) lock 때문에 레이턴시 스파이크를 본 경험이 있으면 연결한다.

### Q17. Mutex versus binary semaphore versus counting semaphore — when do you use each?

**왜 묻나**: 동기화 프리미티브를 목적에 맞게 쓰는지.

**30초 답변**: mutex는 **소유권**이 있는 상호배제다. 잡은 task만 풀 수 있고 priority inheritance가 있다. binary semaphore는 **신호**다. ISR이 give하고 task가 take하는 이벤트 통지에 쓰고 소유권이 없다. counting semaphore는 자원 개수(버퍼 N개)나 누적 이벤트 수를 센다. FreeRTOS에서는 ISR→task 1:1 통지라면 semaphore보다 direct-to-task notification이 더 빠르고 가볍다.

**English answer**: A mutex is for mutual exclusion and has ownership — only the holder releases it, and it carries priority inheritance, so it's what protects a shared bus or data structure between tasks. A binary semaphore is a signal with no owner, typically given from an ISR and taken by a task. A counting semaphore counts resources, like free DMA descriptors, or events that may pile up. In FreeRTOS, if one ISR signals one task, a direct-to-task notification does the same job as a binary semaphore with less RAM and faster.

**꼬리질문**:
- Q: 같은 task가 같은 mutex를 두 번 잡으면? → A: 일반 mutex는 deadlock이다. 필요하면 recursive mutex(`xSemaphoreCreateRecursiveMutex`, `xSemaphoreTakeRecursive`)를 쓴다. Zephyr `k_mutex`는 기본적으로 재귀 잠금을 허용한다.
- Q: event group은 언제? → A: 여러 조건 비트 중 모두/일부를 기다릴 때(예: Wi-Fi 연결됨 + 시간 동기화됨).

### Q18. How do you pass data from an ISR to a task in FreeRTOS? Why is portYIELD_FROM_ISR needed?

**왜 묻나**: 드라이버와 RTOS가 만나는 핵심 패턴(deferred interrupt processing).

**30초 답변**: ISR은 최소한만 한다. 하드웨어 플래그 지우기, 데이터를 큐/stream buffer에 넣거나 task에 notification, 그리고 끝. ISR에서는 반드시 `FromISR` 버전(`xQueueSendFromISR`, `vTaskNotifyGiveFromISR`, `xSemaphoreGiveFromISR`)을 쓴다. 이 함수들은 block하지 않고, 더 높은 우선순위 task를 깨웠는지를 `xHigherPriorityTaskWoken`으로 알려 준다. ISR 끝에서 `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)`를 호출해야 ISR 직후 그 task로 바로 전환된다. 안 하면 다음 tick까지 기다린다.

**English answer**: The ISR does the minimum — acknowledge the hardware, capture the data or index, and signal a task — and the real processing happens in task context. From an interrupt you must use the FromISR variants, such as `xQueueSendFromISR` or `vTaskNotifyGiveFromISR`, because they never block and they report through `xHigherPriorityTaskWoken` whether a higher-priority task became ready. At the end of the ISR you pass that flag to `portYIELD_FROM_ISR`, which pends a context switch so the woken task runs as soon as the interrupt returns, instead of waiting up to a full tick. In Zephyr the kernel handles the reschedule at interrupt exit, so you just call `k_sem_give` or `k_msgq_put` with `K_NO_WAIT`.

**꼬리질문**:
- Q: ISR에서 `printf`나 `malloc`을 부르면? → A: 재진입 불가, 블로킹, 긴 실행 시간 때문에 금지다. 로그는 링버퍼에 넣고 task가 출력한다.
- Q: 큐와 notification 중 무엇을? → A: 데이터를 여러 개 버퍼링해야 하면 큐/stream buffer, "일이 생겼다"는 신호뿐이면 notification.

```c
#include "FreeRTOS.h"
#include "task.h"

static TaskHandle_t s_rx_task;               /* 초기화 때 xTaskGetCurrentTaskHandle()로 저장 */

void DMA1_Stream5_IRQHandler(void)           /* IRQ 이름은 벤더마다 다름 */
{
    BaseType_t woken = pdFALSE;
    /* 1) 하드웨어 인터럽트 플래그 clear (벤더 레지스터, 생략) */
    vTaskNotifyGiveFromISR(s_rx_task, &woken);   /* 2) task 깨우기 */
    portYIELD_FROM_ISR(woken);                   /* 3) 필요하면 즉시 전환 */
}

void rx_task(void *arg)
{
    (void)arg;
    s_rx_task = xTaskGetCurrentTaskHandle();
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        if (n == 0) {
            continue;                        /* 타임아웃: 헬스 체크 등 */
        }
        /* DMA 버퍼 처리 */
    }
}
```

**Don 스토리 연결**: SSD FW의 "ISR은 완료 큐에 넣고 task가 처리"하는 command completion 흐름과 1:1 대응한다. "SSD FW의 completion queue ↔ RTOS queue"라고 말한다.

### Q19. How do you size task stacks and detect stack overflow?

**왜 묻나**: RAM이 수백 KB인 MCU에서 가장 흔한 필드 크래시 원인.

**30초 답변**: 세 가지를 같이 쓴다. ① 정적 분석: GCC `-fstack-usage`로 함수별 `.su` 파일을 만들고 호출 그래프 최악 깊이를 계산한다(함수 포인터·재귀는 수동). ② 런타임 측정: 스택을 패턴으로 칠하고(FreeRTOS는 0xA5) 스트레스 테스트 후 `uxTaskGetStackHighWaterMark()`로 남은 최소량을 본다. ③ 검출: `configCHECK_FOR_STACK_OVERFLOW`(1은 전환 시 SP 검사, 2는 스택 끝 16바이트 패턴 검사)와 `vApplicationStackOverflowHook`, 가능하면 MPU/PSPLIM 스택 가드로 즉시 fault를 낸다. 여유는 측정값의 20~30% 이상 둔다.

**English answer**: I combine analysis, measurement and detection. For analysis, GCC's `-fstack-usage` gives per-function frame sizes and I walk the worst call path, handling function pointers by hand. For measurement, stacks are filled with a known pattern and after a stress run I read `uxTaskGetStackHighWaterMark` for each task, or the thread analyzer in Zephyr. For detection, FreeRTOS's `configCHECK_FOR_STACK_OVERFLOW` checks at context switch and calls the overflow hook, and on parts with an MPU or the ARMv8-M stack limit registers I add a hardware guard so the overflow faults immediately instead of silently corrupting a neighbour. Then I keep a margin of roughly a quarter over the measured peak and report high-water marks in telemetry.

**꼬리질문**:
- Q: 체크 방식 2도 못 잡는 경우는? → A: 전환 사이에 한 번에 스택 끝을 크게 건너뛰어 다른 메모리를 덮고 패턴 영역은 안 건드린 경우, 또는 전환 전에 이미 크래시한 경우. 그래서 하드웨어 가드가 낫다.
- Q: Zephyr에서는? → A: `CONFIG_HW_STACK_PROTECTION`(MPU 가드 또는 ARMv8-M stack limit), `CONFIG_STACK_SENTINEL`, `CONFIG_THREAD_ANALYZER`, `k_thread_stack_space_get()`.
- Q: 스택을 많이 먹는 흔한 코드는? → A: 큰 로컬 배열, `printf` 계열(수백 바이트~1KB 이상), 깊은 재귀, 인라인으로 합쳐진 큰 함수.

**Don 스토리 연결**: SSD FW에서 SRAM 예산을 엄격하게 관리한 경험 → "RAM map을 항상 스프레드시트로 관리했다" 같은 구체적 습관이 있으면 말한다.

### Q20. Design a watchdog strategy for a system with eight RTOS tasks.

**왜 묻나**: 필드 신뢰성. "watchdog을 idle에서 feed하면 안 되는 이유"를 아는지.

**30초 답변**: 하드웨어 watchdog을 한 곳에서만 feed하는 **supervisor(모니터) 패턴**을 쓴다. 각 중요 task는 자기 루프에서 check-in 비트를 세우고, 모니터 task가 주기마다 모든 비트가 모였을 때만 watchdog을 feed하고 비트를 지운다. 어느 task가 멈췄는지 알 수 있도록 feed 실패 직전에 누락된 task ID를 `.noinit` RAM에 기록한다. watchdog은 독립 클럭(예: LSI 기반 IWDG)을 쓰는 것을 고르고, 디버거 halt 중에는 멈추게 설정한다.

**English answer**: I don't kick the hardware watchdog from a timer interrupt or the idle task, because that only proves interrupts are alive. Instead each critical task checks in once per loop by setting its bit, and a monitor task kicks the hardware watchdog only when every expected bit has arrived within the window, then clears them. Before a missed deadline turns into a reset, the monitor records which task failed to check in into a no-init RAM area, so the next boot can report it together with the reset reason register. I pick a watchdog on an independent clock, set a timeout well above the longest legitimate blocking operation such as a flash erase, and configure it to pause while the debugger has halted the core.

**꼬리질문**:
- Q: Zephyr에는? → A: 하드웨어는 `wdt_install_timeout()`, `wdt_setup()`, `wdt_feed()`로, 여러 thread 감시는 task watchdog 서브시스템(`task_wdt_add()`, `task_wdt_feed()`)이 있다.
- Q: window watchdog은? → A: 너무 일찍 feed해도 리셋한다. 폭주 루프가 feed를 반복 호출하는 경우까지 잡는다.
- Q: 긴 flash erase 중에는? → A: timeout을 그보다 길게 잡거나 erase를 섹터 단위로 쪼개서 사이에 check-in한다.

**Don 스토리 연결**: SSD FW의 hang 검출·복구와 Apple factory test-node에서 스트레스 중 hang을 잡은 경험.

### Q21. What is tickless idle and why does it matter for a battery device?

**왜 묻나**: RTOS + 저전력 결합 질문(S03과 연결).

**30초 답변**: 주기적 tick(예: 1 kHz)은 할 일이 없어도 1 ms마다 CPU를 깨워서 deep sleep을 방해한다. tickless idle은 모든 task가 block돼 있으면 다음 타임아웃까지 tick을 멈추고, low-power 타이머를 그 시점에 맞춘 뒤 sleep한다. 깨어나면 잠든 시간만큼 tick count를 보정한다. FreeRTOS는 `configUSE_TICKLESS_IDLE`(1은 SysTick 기반 기본 구현, 2는 포트/앱이 `portSUPPRESS_TICKS_AND_SLEEP()`를 직접 구현)이고, 보정은 `vTaskStepTick()`으로 한다. Zephyr는 `CONFIG_TICKLESS_KERNEL`이 대부분 기본으로 켜져 있다.

**English answer**: A periodic tick wakes the CPU every millisecond even when nothing is due, which kills deep sleep. With tickless idle, when the idle task sees that all tasks are blocked, the kernel computes the time to the next timeout, stops the tick, programs a low-power timer such as an RTC for that moment and enters sleep; on wake it steps the tick count forward by the time slept. In FreeRTOS that's `configUSE_TICKLESS_IDLE`, with option two letting you supply your own `portSUPPRESS_TICKS_AND_SLEEP` built on an RTC or LPTIM, since SysTick stops in deep sleep and has a short maximum period. Zephyr is tickless by default on most boards, and its power management subsystem picks the sleep state from the expected idle time.

**꼬리질문**:
- Q: 기본 SysTick 구현의 한계는? → A: SysTick은 24비트라 최대 sleep 시간이 짧고, 많은 MCU에서 deep sleep 중 코어 클럭이 멈춰 SysTick도 멈춘다. 그래서 32.768 kHz RTC 기반 구현을 쓴다.
- Q: tickless에서 흔한 버그는? → A: 소프트웨어 타이머가 늦게 불리거나, tick 보정 누락으로 시간이 밀리는 것, 깨어난 원인이 타이머가 아닌 인터럽트일 때 경과 시간 계산 오류.
- Q: `configEXPECTED_IDLE_TIME_BEFORE_SLEEP`은? → A: 이보다 짧은 idle에서는 tickless로 들어가지 않는다(진입/복귀 오버헤드가 더 크므로).

### Q22. Static versus dynamic allocation in an RTOS. Which FreeRTOS heap would you use?

**왜 묻나**: "resource-constrained" 요건과 신뢰성.

**30초 답변**: 양산 펌웨어는 가능하면 **정적 할당**이다. `configSUPPORT_STATIC_ALLOCATION`을 켜고 `xTaskCreateStatic`, `xQueueCreateStatic`으로 메모리를 컴파일 타임에 확정하면 링크 시점에 RAM 초과를 알 수 있고 단편화가 없다. 동적이 필요하면 FreeRTOS heap: heap_1(해제 없음), heap_2(해제 가능, 병합 없음), heap_3(libc malloc 래핑), heap_4(인접 블록 병합), heap_5(heap_4 + 여러 비연속 영역, `vPortDefineHeapRegions`). 보통 초기화 때만 할당하는 heap_4 또는 여러 SRAM 뱅크가 있으면 heap_5를 쓴다.

**English answer**: For production firmware I prefer static allocation: with `configSUPPORT_STATIC_ALLOCATION` and the `...CreateStatic` APIs, every task stack and queue is sized at link time, so running out of RAM is a build error rather than a field failure, and there's no fragmentation. When I need a heap I use heap_4, which coalesces adjacent free blocks, or heap_5 when RAM is split across several banks. I still restrict allocation to initialization, use fixed-size pools for runtime buffers, and track `xPortGetMinimumEverFreeHeapSize` in telemetry.

**꼬리질문**:
- Q: static allocation을 켜면 추가로 구현해야 하는 것은? → A: `vApplicationGetIdleTaskMemory()`, software timer를 쓰면 `vApplicationGetTimerTaskMemory()`.
- Q: 런타임에 가변 크기 메모리가 필요하면? → A: 크기 클래스별 memory pool(블록 고정 크기)로 대체한다. S01의 memory pool 문제 참고.

### Q23. FreeRTOS or Zephyr for this device — how would you choose?

**왜 묻나**: JD가 두 이름을 모두 언급. 판단 근거와 트레이드오프.

**30초 답변**: FreeRTOS는 **커널만** 있는 작고 단순한 RTOS(MIT 라이선스)라 벤더 SDK(Ambiq, NXP, ST 등)와 잘 붙고 학습 비용이 낮다. Zephyr는 커널 + 드라이버 모델 + devicetree/Kconfig + BLE 호스트·컨트롤러 + 네트워킹(Thread는 OpenThread 통합) + MCUboot 연동까지 있는 **OS 플랫폼**(Apache 2.0)이다. BLE/Thread 같은 무선 스택과 OTA를 한 생태계에서 쓰려면 Zephyr(예: Nordic nRF Connect SDK), 벤더 SDK가 FreeRTOS 기반이고 코드 크기를 최소로 해야 하면 FreeRTOS다. 결정 기준은 칩 벤더의 지원, 무선 스택, 팀 경험, 인증 요구다.

**English answer**: FreeRTOS is a small, MIT-licensed kernel and not much else, which makes it easy to drop into a vendor SDK and easy to reason about. Zephyr is a whole platform under Apache 2.0: kernel, a device-driver model with devicetree and Kconfig, a Bluetooth LE host and controller, networking with OpenThread for Thread, and first-class MCUboot integration. So I'd let the silicon and the wireless stack drive the choice — if the always-on MCU's vendor SDK and radio stack are FreeRTOS-based, fighting that is expensive; if we need BLE, Thread and signed OTA on a Nordic-class part, Zephyr gives most of it out of the box. I'd also weigh team familiarity and how much we value upstream maintenance versus a smaller, fully understood code base.

**꼬리질문**:
- Q: Zephyr 빌드 도구는? → A: `west`(메타 툴, `west build -b <board>`, `west flash`), CMake, Kconfig(`prj.conf`), devicetree overlay.
- Q: Zephyr에서 ISR 이후 처리 방법은? → A: `k_work_submit()`으로 system workqueue에 넘기거나 전용 workqueue/thread를 쓴다.

| 항목 | FreeRTOS | Zephyr |
|---|---|---|
| 범위 | 커널(+ FreeRTOS-Plus 라이브러리) | 커널 + 드라이버 + 스택 + 빌드 시스템 |
| 라이선스 | MIT | Apache 2.0 |
| 우선순위 숫자 | 클수록 높음 | 작을수록 높음(음수 = cooperative) |
| 하드웨어 기술 | 벤더 HAL/코드 | devicetree + Kconfig |
| 무선 | 벤더 스택 연동 | 자체 BLE 스택, OpenThread 통합 |
| OTA | 직접 또는 벤더 | MCUboot 통합 |

**Don 스토리 연결**: 상용 RTOS 경험 갭을 솔직히 인정하고 "nRF52840 DK로 Zephyr 샘플(BLE peripheral, devicetree overlay)을 돌려 봤다"는 사이드 프로젝트를 준비해 둔다(C04 직접 해보기 참고).

### Q24. How do you convince yourself that all tasks meet their deadlines?

**왜 묻나**: 실시간성 분석 기초. 오디오 파이프라인에서 중요.

**30초 답변**: 주기적 task는 주기가 짧을수록 우선순위를 높게(Rate Monotonic) 주고, CPU 사용률 U = 합(Ci/Ti)을 계산한다. Liu & Layland 한계 U ≤ n(2^(1/n) − 1)(n이 크면 약 69%) 이하면 RMS로 스케줄 가능이 보장된다. 넘으면 response time analysis를 한다. 실제로는 최악 실행 시간을 DWT CYCCNT나 GPIO 토글 + LA로 측정하고, 트레이스(SEGGER SystemView, Percepio Tracealyzer)로 선점과 blocking을 확인한다.

**English answer**: I start with rate-monotonic priorities — shorter period, higher priority — and compute utilization as the sum of worst-case execution time over period. Below the Liu and Layland bound, about 69 percent for many tasks, the set is guaranteed schedulable; above it I do a response-time analysis including blocking from mutexes and interrupt load. The numbers come from measurement: cycle counts from the DWT cycle counter or a GPIO toggle on a logic analyzer, under worst-case conditions. And I verify on target with a trace tool like SystemView or Tracealyzer, and keep deadline-miss counters in the firmware, for example audio buffer overruns.

**꼬리질문**:
- Q: DWT CYCCNT 켜는 법은? → A: `CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;` (M3 이상, M0+에는 없음).
- Q: 인터럽트 부하는 어떻게 반영하나? → A: ISR을 가장 높은 우선순위 "task"로 보고 최악 빈도 × 실행 시간을 더한다.

---

## 4. 임베디드 C/C++

### Q25. What does volatile guarantee, and what does it not?

**왜 묻나**: 임베디드 C 기본 질문 1순위.

**30초 답변**: `volatile`은 컴파일러에게 "이 객체는 프로그램 밖에서 바뀌거나 접근 자체가 부작용이 있다"고 알려서 **모든 읽기/쓰기를 코드에 쓴 횟수와 순서대로** 수행하게 한다(레지스터 캐싱, 삭제, volatile 접근 간 재배치 금지). 쓰는 곳은 MMIO 레지스터, ISR과 공유하는 플래그, `setjmp` 이후 지역변수다. 보장하지 않는 것: **원자성**(32비트 증가는 read-modify-write 3단계), volatile이 아닌 접근과의 순서, CPU/버스 수준 재배치(barrier 필요), 멀티코어 동기화.

**English answer**: Volatile tells the compiler that every access to that object is observable, so it must perform each read and write exactly as written, in program order relative to other volatile accesses, without caching the value in a register. That's what you need for memory-mapped registers and for a flag shared with an ISR. What it doesn't give you is atomicity — `count++` on a volatile is still a load, add and store that an interrupt can split — and it doesn't order non-volatile accesses around it or stop the hardware from reordering. For those I use critical sections, C11 atomics, or explicit barriers.

**꼬리질문**:
- Q: `volatile`이 없으면 생기는 버그 예는? → A: `while (!flag) {}`에서 flag를 한 번만 읽고 무한 루프로 최적화된다. `-O0`에서는 되고 `-O2`에서 안 되는 버그의 전형이다.
- Q: 레지스터 구조체는? → A: CMSIS처럼 `volatile uint32_t` 멤버(`__IO`, `__I`, `__O` 매크로)로 선언한다.

### Q26. When do you need memory barriers on Cortex-M? DMB, DSB, ISB.

**왜 묻나**: volatile 다음 단계. 실제 버그 경험이 있는지.

**30초 답변**: DMB는 barrier 앞뒤 메모리 접근의 **순서**를 보장, DSB는 앞의 메모리 접근이 **완료**될 때까지 다음 명령을 멈춤, ISB는 파이프라인을 비워 이후 명령이 새 시스템 설정을 보게 한다. 쓰는 곳: VTOR/MPU/CONTROL 변경 후 DSB+ISB, WFI 직전 DSB, ISR 끝에서 주변장치 인터럽트 플래그를 지운 직후 DSB(쓰기 버퍼 때문에 ISR을 다시 진입하는 문제 방지), DMA나 다른 코어와 공유하는 버퍼에서 데이터 쓰기와 인덱스/도어벨 쓰기 사이 DMB. 컴파일러 재배치만 막으려면 `__asm volatile("" ::: "memory")`, CMSIS `__COMPILER_BARRIER()`.

**English answer**: DMB guarantees ordering of memory accesses across it, DSB waits until outstanding accesses complete, and ISB flushes the pipeline so later instructions see new system state. On a single Cortex-M core the common cases are: DSB and ISB after changing VTOR, the MPU or the CONTROL register; a DSB before WFI; a DSB after clearing a peripheral's interrupt flag at the end of an ISR, because with a write buffer the flag may still read as set when the exception returns and you get a spurious re-entry; and a DMB between writing a descriptor and ringing the DMA or mailbox doorbell. Note that the CMSIS intrinsics also act as compiler barriers, whereas volatile alone doesn't order ordinary memory.

**꼬리질문**:
- Q: 싱글코어 M4에서 SPSC 링버퍼에 DMB가 필요한가? → A: M4는 실제로 일반 메모리 접근을 재배치하지 않지만, 아키텍처는 허용하고 컴파일러 재배치는 여전히 가능하다. 이식성을 위해 C11 `atomic_store_explicit(..., memory_order_release)`나 `__DMB()`를 쓴다. M7이나 멀티코어면 반드시 필요하다.

**Don 스토리 연결**: SSD FW 멀티코어(Cortex-R) 사이 메일박스/공유 메모리 동기화 경험이 있으면 가장 강한 답이 된다. Cortex-R은 barrier가 실제로 필요한 코어다.

### Q27. Where does each of these end up in memory: a const global table, a static local, a string literal? And what's the difference between `const int *p` and `int *const p`?

**왜 묻나**: 링커/메모리 맵 감각. flash/RAM 예산에 직결.

**30초 답변**: 초기값 있는 전역/static 변수는 `.data`(RAM, flash에 초기값 사본), 0 또는 초기값 없는 것은 `.bss`(RAM), `const` 전역 테이블과 문자열 리터럴은 `.rodata`(flash, bare-metal GCC 기준), 함수 안의 non-static 지역 `const` 배열은 스택에 매번 복사될 수 있으니 `static const`로 선언한다. `const int *p`는 가리키는 값이 읽기 전용, `int *const p`는 포인터 자체가 고정이다. 읽기 전용 상태 레지스터는 `const volatile`이다.

**English answer**: Initialized globals and statics go to .data, which costs both RAM and a copy of the initial values in flash; zero-initialized ones go to .bss, RAM only. Const globals and string literals go to .rodata, which the linker script places in flash on a bare-metal target. A non-static const array inside a function may be rebuilt on the stack every call, so lookup tables should be `static const`. For pointers, `const int *p` means the pointed-to data is read-only, while `int *const p` means the pointer itself can't change; a read-only hardware status register is `const volatile`. I verify placement with the map file or `arm-none-eabi-size` and `nm`, rather than trusting it.

**꼬리질문**:
- Q: 특정 함수를 RAM에서 실행하려면? → A: `__attribute__((section(".ramfunc")))`처럼 섹션을 지정하고 링커 스크립트에서 그 섹션을 RAM VMA/flash LMA로 배치한 뒤 startup이 복사하게 한다(섹션 이름은 프로젝트마다 다름). flash 쓰기 중 코드 실행이나 XIP 레이턴시 회피에 쓴다.
- Q: `const`와 `#define`의 차이는? → A: `const`는 타입과 스코프가 있고 디버거에서 보인다. C에서 `const int`는 배열 크기 같은 상수식에 못 쓰므로 `enum`이나 `#define`을 쓴다(C++ `constexpr`는 가능).

### Q28. Is `count++` on a shared variable safe on Cortex-M? How do you make it atomic?

**왜 묻나**: ISR 공유 데이터 버그의 근원.

**30초 답변**: 안전하지 않다. LDR → ADD → STR 사이에 인터럽트가 같은 변수를 바꾸면 업데이트가 사라진다. 32비트 정렬된 단일 읽기/쓰기만 원자적이다. 해결: ① 짧은 critical section(PRIMASK/BASEPRI), ② ARMv7-M 이상에서는 LDREX/STREX 배타 접근(C11 `atomic_fetch_add`가 이것으로 컴파일됨), ③ 설계로 회피(단일 writer로 만들기, SPSC 링버퍼). ARMv6-M(M0/M0+)에는 LDREX/STREX가 없으므로 critical section만 가능하다.

**English answer**: No — it compiles to a load, an add and a store, and an interrupt between them that also updates the variable loses one of the updates. Only single aligned word loads and stores are atomic. On ARMv7-M and later I use C11 atomics such as `atomic_fetch_add`, which compile to an LDREX/STREX retry loop, or a very short critical section. On Cortex-M0 and M0+ there are no exclusive instructions, so it's a PRIMASK critical section. Better still is designing so each variable has a single writer — for example the ISR only advances the head and the task only advances the tail.

**꼬리질문**:
- Q: 64비트 타임스탬프 읽기는? → A: 두 번의 32비트 읽기라 중간에 상위 word가 바뀔 수 있다. 상위 → 하위 → 상위를 읽어 같을 때까지 반복하거나 critical section.
- Q: 비트필드 레지스터 수정은? → A: read-modify-write이므로 같은 레지스터를 ISR도 건드리면 보호가 필요하다. 가능하면 SET/CLR 전용 레지스터나 bit-band(M3/M4)를 쓴다.

**Don 스토리 연결**: Don의 코어 경험(M0+, R8)이 둘 다 해당된다. "M0+에는 LDREX가 없어서 critical section을 썼다"는 식의 구체적 사실을 넣는다.

### Q29. Would you use C++ in firmware? Which features do you use and which do you avoid?

**왜 묻나**: JD "C and/or C++". 코드베이스 스타일 판단.

**30초 답변**: 쓴다. 단 서브셋으로. 쓰는 것: 클래스와 RAII(lock guard, CS guard), `constexpr`, `enum class`, 템플릿(타입 안전한 레지스터/링버퍼, 크기 컴파일 타임 고정), `std::array`, 네임스페이스, `static_assert`. 끄거나 피하는 것: 예외(`-fno-exceptions`), RTTI(`-fno-rtti`), 런타임 힙 할당(`new`는 초기화 때만, placement new와 pool), `iostream`, 무분별한 템플릿 인스턴스화(코드 크기). virtual은 vtable이 flash에 있고 간접 호출 하나 비용이라 필요한 곳에만 쓴다. 전역 객체의 정적 초기화 순서 문제에 주의한다.

**English answer**: Yes, as a disciplined subset. I use classes with RAII for things like critical-section and mutex guards, `constexpr` and `static_assert` for compile-time checks, `enum class`, and templates for type-safe fixed-size containers where the size is known at compile time. I turn off exceptions and RTTI, avoid heap allocation after init — using placement new into static storage or pools — and avoid iostream. Virtual functions are fine where polymorphism is real, since the cost is a vtable in flash and one indirect call. The two things I watch are code size from template instantiation, which I check in the map file, and the static initialization order between translation units, which I avoid by constructing global objects explicitly in a known order.

**꼬리질문**:
- Q: 함수 내 `static` 객체의 숨은 비용은? → A: 스레드 안전한 초기화를 위해 `__cxa_guard_acquire` 호출이 생긴다. RTOS와 연동하지 않으면 `-fno-threadsafe-statics`로 끄고 초기화 시점을 통제한다.
- Q: 순수 가상 함수 링크 에러(`__cxa_pure_virtual`)는? → A: 라이브러리 없이 빌드할 때 이 심볼을 직접 정의한다(보통 무한 루프/fault).

```cpp
#include <cstdint>
#include "cmsis_compiler.h"

class CriticalSection {               // RAII: 스코프를 벗어나면 자동 복원
public:
    CriticalSection() : primask_(__get_PRIMASK()) { __disable_irq(); }
    ~CriticalSection() { __set_PRIMASK(primask_); }
    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;
private:
    uint32_t primask_;
};

volatile uint32_t g_events;

void post_event(uint32_t bit)
{
    CriticalSection cs;               // 예외 없이도 early return에서 안전
    g_events |= bit;
}
```

**Don 스토리 연결**: SSD FW를 C와 C++로 개발했으므로 "양산 코드에서 쓴 C++ 규칙"을 구체적으로 말한다(예: 예외/RTTI off, 정적 객체).

---

## 5. 최종 점검

- [ ] reset → `main` 과정을 vector table, `.data` 복사, `.bss` 초기화, 생성자 순서로 화이트보드에 그릴 수 있다
- [ ] NVIC 우선순위 숫자 방향(Cortex-M과 FreeRTOS task, Zephyr thread 세 가지)을 헷갈리지 않고 말할 수 있다
- [ ] `configMAX_SYSCALL_INTERRUPT_PRIORITY` 규칙과 BASEPRI의 관계를 설명할 수 있다
- [ ] HardFault 핸들러가 수집할 레지스터와 stacked frame 순서를 쓸 수 있다
- [ ] I2C bus clear(9 clock + STOP) 절차를 코드로 쓸 수 있다
- [ ] SPI 4개 mode와 I2S BCLK 계산을 즉석에서 할 수 있다
- [ ] M7 캐시 + DMA에서 clean/invalidate를 언제 하는지 말할 수 있다
- [ ] priority inversion 타임라인과 inheritance를 그릴 수 있다
- [ ] ISR → task 패턴을 `vTaskNotifyGiveFromISR` + `portYIELD_FROM_ISR`로 코드로 쓸 수 있다
- [ ] 스택 사이징 3단계(분석·측정·검출)와 watchdog supervisor 패턴을 설명할 수 있다
- [ ] tickless idle 동작과 SysTick 기반 구현의 한계를 말할 수 있다
- [ ] FreeRTOS vs Zephyr 선택 기준을 1분 안에 영어로 말할 수 있다

| 자주 틀리는 포인트 | 올바른 내용 |
|---|---|
| "Cortex-M 우선순위는 높은 숫자가 높다" | 낮은 숫자가 높다. FreeRTOS task는 반대(큰 숫자가 높음), Zephyr thread는 Cortex-M과 같은 방향 |
| "volatile이면 원자적이다" | 아니다. 컴파일러 최적화만 막는다 |
| "binary semaphore로 상호배제" | priority inheritance가 없다. mutex를 쓴다 |
| "watchdog은 idle hook에서 feed" | 인터럽트 생존만 증명한다. task check-in 기반 모니터 |
| "ISR에서 xQueueSend 사용" | `xQueueSendFromISR` + `portYIELD_FROM_ISR` |
| "M0+에서 LDREX로 atomic" | ARMv6-M에는 없다. critical section |
| "DMA 버퍼 invalidate는 아무 주소나" | 32바이트 캐시 라인 정렬·크기가 아니면 이웃 데이터 손상 |
| "I2C 마지막 바이트 NACK는 에러" | 마스터 읽기 종료 신호로 정상 |
| "configCHECK_FOR_STACK_OVERFLOW면 다 잡힌다" | 소프트웨어 검사는 놓칠 수 있다. MPU/PSPLIM 하드웨어 가드 추가 |
