# C01. ARM Cortex 아키텍처 · 부트 · 툴체인 — 전원이 들어온 순간부터 main()까지, 그리고 그걸 만드는 도구

> **이 노트를 다 읽으면**: Cortex-M 레지스터·예외 모델·NVIC를 화이트보드에 그릴 수 있다 · reset부터 `main()`까지 startup 코드와 링커 스크립트를 직접 쓸 수 있다 · MPU/캐시와 Cortex-A(MMU, EL, GIC, TF-A/U-Boot)의 차이를 설명할 수 있다 · map 파일·objdump·size로 이미지를 분석할 수 있다
> **JD 연결**: "Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers" · "Experience with ARM Cortex-M or Cortex-A architectures and associated toolchains"
> **Don 기준 난이도**: Cortex-R/M0+ bare-metal bring-up, Trace32는 이미 익숙함 / 새로 다질 부분은 ARMv7-M·ARMv8-M 예외 세부(EXC_RETURN, BASEPRI, lazy FP stacking), MPU 규칙, Cortex-A 부트 체인(TF-A, U-Boot, Qualcomm 체인)

---

## 0. 큰 그림

Hark 같은 기기라면 칩이 두 종류 들어갈 가능성이 크다(추정). 하나는 Android를 돌리는 Cortex-A 기반 SoC, 다른 하나는 늘 켜져 있는 저전력 Cortex-M MCU다. 두 세계는 "ARM"이라는 이름은 같지만 프로그래머 모델, 부트 방식, 메모리 보호, 인터럽트 컨트롤러가 전부 다르다.

```
                    예를 들어 Hark 같은 기기 (추정 구조)
 +-----------------------------------------+     +-------------------------------+
 |  Application SoC (Cortex-A, AArch64)    |     |  Always-on MCU (Cortex-M)     |
 |                                         |     |                               |
 |  EL3  Secure monitor (TF-A BL31 등)     |     |  Thread mode  : RTOS tasks    |
 |  EL2  Hypervisor (선택)                 | IPC |  Handler mode : ISR           |
 |  EL1  Linux/Android kernel + BSP        |<--->|  NVIC, SysTick, MPU           |
 |  EL0  Apps (Hark agent)                 | SPI |  Flash(XIP) + SRAM            |
 |  MMU + GIC + L1/L2/L3 cache             | GPIO|  I2S mic, IMU, PMIC(I2C)      |
 |  Boot: ROM -> BL -> U-Boot/ABL -> Linux |     |  Boot: vector table -> main   |
 +-----------------------------------------+     +-------------------------------+
```

이 노트의 순서는 다음과 같다.

1. Cortex 제품군과 아키텍처 버전(ARMv6-M, ARMv7-M, ARMv8-M, ARMv8-A)
2. Cortex-M 프로그래머 모델(레지스터, 모드, 스택)
3. 예외 모델과 NVIC
4. 메모리 맵
5. 부트: reset → startup → `main()`
6. 링커 스크립트
7. MPU와 캐시
8. Cortex-A는 무엇이 다른가
9. 툴체인(arm-none-eabi-gcc, 옵션, map, objdump, size)
10. CMSIS

---

## 1. Cortex 제품군과 아키텍처 버전

### 1.1 아키텍처 vs 코어

ARM은 **아키텍처**(명령어 집합, 예외 모델, 메모리 모델을 정의한 스펙)와 **코어**(그 스펙을 구현한 IP)를 구분한다. 예를 들어 Cortex-M4는 ARMv7E-M 아키텍처를 구현한 코어다. 면접에서 "M4는 ARMv7-M이다"라고 말할 수 있으면 기본을 아는 사람으로 보인다.

| 코어 | 아키텍처 | 특징 | 흔한 사용처 |
|---|---|---|---|
| Cortex-M0 / M0+ | ARMv6-M | Thumb 부분집합, BASEPRI 없음, LDREX/STREX 없음, 최대 32 IRQ | 초저가, 보조 MCU (Don의 SSD M0+) |
| Cortex-M3 | ARMv7-M | 하드웨어 나눗셈, BASEPRI, 비트밴드(선택) | 범용 MCU |
| Cortex-M4 | ARMv7E-M | DSP 명령(SIMD, saturating), 단정밀도 FPU(선택) | 센서 허브, 오디오 전처리 |
| Cortex-M7 | ARMv7E-M | 6단 superscalar, I/D 캐시, TCM, 배정밀도 FPU(선택) | 고성능 MCU |
| Cortex-M23 | ARMv8-M Baseline | TrustZone-M, M0+ 후속 | 보안 저가 MCU |
| Cortex-M33 | ARMv8-M Mainline | TrustZone-M, DSP, FPU, stack limit 레지스터 | nRF5340, 최신 MCU |
| Cortex-M55 | ARMv8.1-M | Helium(MVE) 벡터 확장 | ML/DSP MCU |
| Cortex-M85 | ARMv8.1-M | Helium + 고성능 | 고성능 ML MCU |
| Cortex-R5/R8/R82 | ARMv7-R / ARMv8-R | 실시간, TCM, MPU (R82는 64비트, 선택적 MMU) | SSD 컨트롤러, 모뎀 |
| Cortex-A53/A55/A78 등 | ARMv8-A / ARMv8.2-A | MMU, EL0~EL3, 가상화 | 스마트폰 AP, Linux |

> Ambiq Apollo 계열은 세대마다 코어가 다르다(예: Apollo4는 Cortex-M4F, Apollo5는 Cortex-M55). Hark가 어떤 칩을 쓰는지는 알 수 없으니 "Ambiq-class MCU"라는 JD 표현 정도로만 말한다.

### 1.2 Don 경험과 연결

SSD 컨트롤러의 Cortex-R8은 ARMv7-R로, 예외 모델이 Cortex-A와 비슷한 "모드 기반"(SVC, IRQ, FIQ, ABT, UND 모드와 모드별 banked 레지스터)이다. Cortex-M은 이것과 완전히 다르다. **Cortex-M은 예외 진입 때 하드웨어가 레지스터를 스택에 자동으로 저장**하고, 벡터 테이블에는 명령어가 아니라 **핸들러 주소**가 들어간다. R8에서 IRQ 모드 진입 후 어셈블리로 컨텍스트를 저장하던 코드가 M에서는 필요 없다는 점이 가장 큰 차이다.

---

## 2. Cortex-M 프로그래머 모델

### 2.1 레지스터

```
 범용 레지스터                         특수 레지스터 (MRS/MSR로 접근)
 +------+                              +-----------+------------------------------------+
 | R0   |  인자/반환값 (AAPCS)          | xPSR      | APSR(N,Z,C,V,Q,GE) + IPSR(예외번호) |
 | R1   |                              |           | + EPSR(T비트, IT/ICI)               |
 | R2   |                              | PRIMASK   | 1이면 NMI/HardFault 외 전부 마스크  |
 | R3   |                              | FAULTMASK | 1이면 NMI 외 전부 마스크 (v7-M+)    |
 | R4   |  callee-saved                | BASEPRI   | 이 값 이상(숫자) 우선순위 마스크     |
 | ...  |                              | CONTROL   | nPRIV, SPSEL, FPCA (v8-M: +SFPA)    |
 | R11  |                              +-----------+------------------------------------+
 | R12  |  IP (scratch)
 | R13  |  SP  -> MSP 또는 PSP (banked)
 | R14  |  LR  (예외 중엔 EXC_RETURN 값)
 | R15  |  PC
 +------+
```

- **R0~R3, R12**: caller-saved. 함수 호출 시 망가져도 된다. 예외 진입 때 하드웨어가 자동 저장하는 이유가 바로 이것이다. 하드웨어가 caller-saved를 저장해 주면 ISR를 평범한 C 함수로 쓸 수 있다.
- **R4~R11**: callee-saved. C 함수가 쓰면 스스로 저장·복원한다. RTOS 컨텍스트 스위치는 이것을 PendSV 안에서 수동으로 저장한다(C04 참고).
- **SP는 두 개**: MSP(Main Stack Pointer)와 PSP(Process Stack Pointer). 리셋 후엔 MSP를 쓴다. RTOS는 task에 PSP, 커널과 ISR에 MSP를 쓴다.
- **ARMv8-M Mainline**은 `MSPLIM`, `PSPLIM` stack limit 레지스터가 있다. SP가 이 값 아래로 내려가면 UsageFault(STKOF)가 난다. 스택 오버플로를 하드웨어로 잡을 수 있는 중요한 기능이다.

### 2.2 모드와 권한

| 상태 | 설명 |
|---|---|
| Thread mode | 일반 코드 실행. privileged 또는 unprivileged (`CONTROL.nPRIV`) |
| Handler mode | 예외/인터럽트 처리 중. 항상 privileged, 항상 MSP 사용 |
| `CONTROL.SPSEL` | Thread mode에서 0이면 MSP, 1이면 PSP |
| `CONTROL.FPCA` | 현재 컨텍스트가 FP 레지스터를 사용 중인지 (FPU 있을 때) |

Thread mode를 unprivileged로 돌리면 SCS(System Control Space) 레지스터나 MPU가 막은 영역에 접근할 수 없다. 보안이 필요한 기기에서 RTOS를 "user task는 unprivileged"로 구성하는 이유다(FreeRTOS-MPU, Zephyr userspace).

### 2.3 Thumb 전용

Cortex-M은 **Thumb/Thumb-2 명령어만 실행**한다. ARM(A32) 상태가 없다. 그래서 함수 포인터와 벡터 테이블 항목의 **최하위 비트(bit 0)가 1**이어야 한다. 이 비트가 0인 주소로 분기하면 EPSR.T가 0이 되어 INVSTATE UsageFault(또는 HardFault로 escalation)가 난다. 링커는 Thumb 함수 심볼에 자동으로 bit 0을 붙여 주므로 C로 벡터 테이블을 만들면 신경 쓸 일이 없지만, 어셈블리에서 `.word` 로 주소를 직접 쓸 때는 `.thumb_func` 지시어가 필요하다.

---

## 3. 예외 모델과 NVIC

### 3.1 예외 번호

| 번호 | 예외 | 우선순위 | 비고 |
|---|---|---|---|
| 1 | Reset | -3 (고정) | |
| 2 | NMI | -2 (고정) | |
| 3 | HardFault | -1 (고정) | 다른 fault가 escalation되는 곳 |
| 4 | MemManage | 설정 가능 | MPU 위반. ARMv6-M에는 없음 |
| 5 | BusFault | 설정 가능 | 버스 에러. ARMv6-M에는 없음 |
| 6 | UsageFault | 설정 가능 | 미정의 명령, 0 나누기(트랩 켰을 때), unaligned(트랩 켰을 때) |
| 7 | SecureFault | 설정 가능 | ARMv8-M Mainline + TrustZone |
| 11 | SVCall | 설정 가능 | `SVC` 명령 |
| 12 | DebugMonitor | 설정 가능 | ARMv7-M 이상 |
| 14 | PendSV | 설정 가능 | RTOS 컨텍스트 스위치 |
| 15 | SysTick | 설정 가능 | OS tick |
| 16 + n | IRQn | 설정 가능 | 벤더 주변장치 인터럽트 |

CMSIS에서는 `IRQn_Type` enum으로 외부 인터럽트를 0부터, 시스템 예외는 음수(`SysTick_IRQn = -1`, `PendSV_IRQn = -2` 등)로 표현한다. 그래서 "IRQ 번호"와 "예외 번호"는 16 차이가 난다. `IPSR` 값은 예외 번호다.

MemManage, BusFault, UsageFault는 기본으로 **꺼져 있다**. `SCB->SHCSR`의 `MEMFAULTENA`, `BUSFAULTENA`, `USGFAULTENA` 비트를 켜지 않으면 전부 HardFault로 escalation된다. 디버깅을 편하게 하려면 초기화 때 켜 두는 것이 좋다.

### 3.2 예외 진입: 하드웨어가 하는 일

```
  인터럽트 발생 (IRQ 5 pending, 현재 우선순위보다 높음)
  |
  v
 [1] Stacking: 현재 SP(MSP 또는 PSP)에 8워드 push
       높은 주소
       +--------+
       | xPSR   |  SP+0x1C
       | PC     |  SP+0x18  <- 복귀 주소 (HardFault 분석 때 보는 "stacked PC")
       | LR     |  SP+0x14
       | R12    |  SP+0x10
       | R3     |  SP+0x0C
       | R2     |  SP+0x08
       | R1     |  SP+0x04
       | R0     |  SP+0x00  <- 새 SP
       +--------+
       낮은 주소       (FPU 사용 중이면 S0-S15, FPSCR 등 18워드가 더 붙음)
 [2] Vector fetch: VTOR + 4 x 예외번호 에서 핸들러 주소 읽기 (stacking과 병렬)
 [3] LR <- EXC_RETURN,  IPSR <- 예외번호,  Handler mode 진입, MSP 사용
 [4] 핸들러 첫 명령 실행
```

- Cortex-M3/M4는 zero wait-state 메모리 기준으로 인터럽트 요청부터 핸들러 첫 명령까지 **12 사이클**이다(M0+는 15 사이클). flash wait state나 버스 상태에 따라 더 늘어난다.
- 스택 프레임은 8바이트 정렬이 기본이다(`SCB->CCR.STKALIGN`). 정렬을 맞추려고 한 워드를 끼워 넣었다면 stacked xPSR의 bit 9에 기록된다.

### 3.3 EXC_RETURN

예외 핸들러 안에서 LR에는 복귀 주소가 아니라 특별한 값이 들어간다. 핸들러가 `BX LR` 로 이 값을 PC에 넣으면 하드웨어가 "예외 복귀"로 인식하고 unstacking을 한다.

| EXC_RETURN (ARMv7-M) | 복귀할 곳 | 사용할 스택 | FP 프레임 |
|---|---|---|---|
| `0xFFFFFFF1` | Handler mode (중첩 예외) | MSP | 없음 |
| `0xFFFFFFF9` | Thread mode | MSP | 없음 |
| `0xFFFFFFFD` | Thread mode | PSP | 없음 |
| `0xFFFFFFE1` | Handler mode | MSP | 확장 프레임 |
| `0xFFFFFFE9` | Thread mode | MSP | 확장 프레임 |
| `0xFFFFFFED` | Thread mode | PSP | 확장 프레임 |

ARMv8-M은 TrustZone 때문에 비트가 더 있다(S, DCRS, ES 비트 등). 값이 다르므로 HardFault 핸들러에서 "bit 2가 1이면 PSP"처럼 **비트 단위로 판단**하는 코드가 이식성이 좋다.

```c
/* HardFault에서 어느 스택에 프레임이 쌓였는지 찾아 C 함수로 넘긴다 (ARMv7-M/v8-M Mainline). */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"  /* EXC_RETURN bit 2: 0 = MSP, 1 = PSP */
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b hardfault_c     \n"  /* r0 = stacked frame 주소, 첫 번째 인자 */
    );
}
```

- `naked`: 컴파일러가 prologue/epilogue를 만들지 않게 한다. 스택을 건드리기 전에 SP를 읽어야 하기 때문이다.
- `tst lr, #4`: LR(=EXC_RETURN)의 bit 2를 검사한다.
- `ite eq`: Thumb-2 IT 블록. 다음 두 명령을 조건부로 실행한다.
- `b hardfault_c`: `void hardfault_c(uint32_t *frame)` 로 점프한다. `frame[6]`이 stacked PC, `frame[5]`가 stacked LR이다. 자세한 fault 해석은 C10에서 다룬다.

### 3.4 Tail-chaining, late arrival, lazy stacking

```
 Tail-chaining: ISR A 끝날 때 B가 pending이면 unstack/restack 생략
   Thread |==A 실행==|--B 진입(짧음)--|==B 실행==|unstack| Thread
          ^stack                                  ^한 번만 복귀

 Late arrival: A 진입 stacking 중에 더 높은 B가 도착하면 B 벡터를 먼저 fetch
   Thread |stacking...| ==B 실행== |tail-chain| ==A 실행== | unstack

 Lazy FP stacking (FPCCR.LSPEN=1): FP 레지스터 공간만 예약하고 실제 저장은
   ISR가 처음 FP 명령을 쓸 때 수행 -> FP 안 쓰는 ISR의 지연시간 보호
```

### 3.5 우선순위와 NVIC

- **숫자가 작을수록 높은 우선순위**다. 0이 가장 높다(음수 고정 우선순위 제외).
- 우선순위 필드는 8비트지만 칩마다 **상위 몇 비트만 구현**한다. CMSIS 매크로 `__NVIC_PRIO_BITS` 가 그 수다. 예: Cortex-M0+는 2비트(4단계), nRF52는 3비트(8단계), 많은 STM32는 4비트(16단계). 구현되지 않은 하위 비트는 0으로 읽힌다.
- 그래서 레지스터에 쓰는 값은 `prio << (8 - __NVIC_PRIO_BITS)` 이다. CMSIS `NVIC_SetPriority()`가 이 시프트를 대신 해 준다. FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY` 설정에서 이 시프트를 헷갈려서 assert가 터지는 일이 아주 흔하다.
- `SCB->AIRCR.PRIGROUP`으로 우선순위를 **group priority(선점 결정)**와 **subpriority(동시 pending일 때 순서)**로 나눌 수 있다. 선점은 group priority로만 결정된다.

| NVIC 레지스터 | CMSIS 이름 | 역할 |
|---|---|---|
| Interrupt Set-Enable | `NVIC->ISER[n]` | 1을 쓰면 enable (0 쓰기는 무시) |
| Interrupt Clear-Enable | `NVIC->ICER[n]` | 1을 쓰면 disable |
| Interrupt Set-Pending | `NVIC->ISPR[n]` | 소프트웨어로 pending 만들기 |
| Interrupt Clear-Pending | `NVIC->ICPR[n]` | pending 지우기 |
| Interrupt Active Bit | `NVIC->IABR[n]` | 현재 실행 중(active)인지 (ARMv7-M 이상) |
| Interrupt Priority | `NVIC->IP[n]` / `NVIC->IPR[n]` | 우선순위 (CMSIS 버전에 따라 이름이 다름) |
| System Handler Priority | `SCB->SHP[]` / `SCB->SHPR[]` | 시스템 예외 우선순위 |

Set/Clear 레지스터가 분리된 이유는 **read-modify-write 없이 한 비트만 원자적으로 바꾸기 위해서**다. `ISER`에 `1u << 5`를 쓰면 다른 인터럽트의 enable 상태를 건드리지 않는다. 인터럽트 사이 경쟁 조건이 원천적으로 없다.

```c
#include "device.h"   /* 벤더 디바이스 헤더: IRQn_Type, __NVIC_PRIO_BITS 정의 후 core_cm4.h include */

void uart0_irq_setup(void)
{
    NVIC_DisableIRQ(UART0_IRQn);          /* ICER에 쓰기 */
    NVIC_ClearPendingIRQ(UART0_IRQn);     /* 설정 전 쌓여 있던 pending 제거 */
    NVIC_SetPriority(UART0_IRQn, 5);      /* 내부에서 5 << (8 - __NVIC_PRIO_BITS) */
    NVIC_EnableIRQ(UART0_IRQn);           /* ISER에 쓰기 */
}
```

- `UART0_IRQn`은 예시 이름이다. 실제 이름은 벤더 디바이스 헤더를 따른다(벤더마다 다름).
- 주변장치 쪽 인터럽트 플래그를 먼저 지우지 않고 NVIC pending만 지우면, 레벨 인터럽트는 바로 다시 pending된다. **주변장치 플래그 → NVIC 순서**로 정리한다.

### 3.6 인터럽트 마스킹

| 방법 | 효과 | 아키텍처 |
|---|---|---|
| `__disable_irq()` (`CPSID i`) | PRIMASK=1, 설정 가능한 우선순위 전부 차단 | 전부 |
| `__set_BASEPRI(x)` | 우선순위 숫자가 x 이상인 것만 차단, 더 긴급한 것은 통과 | ARMv7-M, ARMv8-M Mainline |
| `__disable_fault_irq()` (`CPSID f`) | FAULTMASK=1, HardFault까지 차단 | ARMv7-M 이상 |
| `NVIC_DisableIRQ(n)` | 특정 IRQ 하나만 차단 | 전부 |

FreeRTOS의 Cortex-M3/M4 포트는 critical section에 PRIMASK가 아니라 BASEPRI를 쓴다. 그래서 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 긴급한 인터럽트(예: 모터 제어, 오디오 DMA)는 커널 critical section 중에도 지연 없이 들어온다. 대신 그런 ISR에서는 FreeRTOS API를 부르면 안 된다. M0+(ARMv6-M) 포트는 BASEPRI가 없어서 PRIMASK를 쓴다.

---

## 4. 메모리 맵

Cortex-M은 4GB 주소 공간을 **아키텍처가 미리 나눠 둔다**. 영역마다 기본 메모리 속성(실행 가능 여부, cacheable, Device/Normal)이 정해져 있다.

```
 0xFFFFFFFF +-----------------------------+
            | Vendor specific             |
 0xE0100000 +-----------------------------+
            | PPB (Private Peripheral Bus)|  SCS: NVIC 0xE000E100, SCB 0xE000ED00,
            |                             |  SysTick 0xE000E010, MPU 0xE000ED90,
 0xE0000000 +-----------------------------+  DWT 0xE0001000, ITM 0xE0000000
            | External Device   (1GB)     |  XN (실행 불가)
 0xA0000000 +-----------------------------+
            | External RAM      (1GB)     |  QSPI PSRAM, SDRAM 등
 0x60000000 +-----------------------------+
            | Peripheral        (0.5GB)   |  UART, SPI, I2C, GPIO ... (Device, XN)
 0x40000000 +-----------------------------+
            | SRAM              (0.5GB)   |  .data .bss heap stack
 0x20000000 +-----------------------------+
            | Code              (0.5GB)   |  Flash, ROM, vector table (리셋 시)
 0x00000000 +-----------------------------+
```

- 주요 SCS 레지스터 주소: `SCB->VTOR` = `0xE000ED08`, `SCB->CPUID` = `0xE000ED00`, `SCB->AIRCR` = `0xE000ED0C`, `SCB->CFSR` = `0xE000ED28`, `SCB->HFSR` = `0xE000ED2C`.
- Flash가 실제로 어디 매핑되는지는 벤더마다 다르다. STM32는 `0x08000000`에 flash를 두고 `0x00000000`에 alias한다. nRF52는 `0x00000000`부터 flash다.
- Cortex-M3/M4는 선택적으로 **bit-band** 영역(SRAM `0x22000000`, Peripheral `0x42000000`)을 제공해 한 비트를 한 워드처럼 원자적으로 쓸 수 있다. M7과 ARMv8-M에는 없다.
- **XN(Execute Never)**: Peripheral, Device 영역에서는 코드를 실행할 수 없다. 함수 포인터가 망가져 `0x4xxxxxxx`로 점프하면 MemManage/HardFault(IACCVIOL)가 난다.

> Don 경험과 연결: SSD 컨트롤러에서 ITCM/DTCM, SRAM, DRAM 영역별로 코드·데이터를 나눠 배치하던 것과 같은 일이다. Cortex-M7도 ITCM(`0x00000000` 부근)과 DTCM(`0x20000000` 부근)을 가진다(칩마다 다름). ISR와 hot loop를 ITCM에 두는 튜닝은 R8에서 하던 것과 동일하다.

---

## 5. 부트: reset에서 main()까지

### 5.1 하드웨어 리셋 시퀀스

```
 POR/리셋 해제
   |
   v
 [HW] VTOR 초기값(보통 0x00000000, 구현에 따라 다름)에서
      word[0] 읽기 -> MSP 초기값
      word[1] 읽기 -> PC (Reset_Handler 주소, bit0=1)
   |
   v
 [SW] Reset_Handler
      1) (선택) 클럭/flash wait state 설정 = SystemInit()
      2) .data를 flash(LMA)에서 SRAM(VMA)으로 복사
      3) .bss를 0으로 채움
      4) (FPU 있으면) CPACR로 CP10/CP11 활성화 - 보통 SystemInit에서
      5) C++ 정적 생성자 실행 (__libc_init_array)
      6) main() 호출
      7) main이 반환하면 무한 루프
```

벡터 테이블의 첫 워드가 명령어가 아니라 **스택 포인터 초기값**이라는 점이 Cortex-M의 특징이다. 그래서 Reset_Handler를 C로 작성할 수 있다. 하드웨어가 이미 SP를 세팅해 주었기 때문이다. Cortex-A/R은 리셋 벡터에 **명령어**가 있고 SP도 어셈블리로 직접 설정해야 한다.

실제 제품에는 이 앞에 벤더 **ROM bootloader**나 **MCUboot 같은 2nd stage bootloader**가 있는 경우가 많다. 그 경우 bootloader가 서명을 검증한 뒤 애플리케이션 벡터 테이블 주소로 VTOR를 바꾸고 MSP를 다시 설정한 뒤 앱 Reset_Handler로 점프한다(C07에서 자세히).

### 5.2 C로 쓴 startup 코드

아래는 GCC용 최소 startup이다. 링커 스크립트(6절)가 정의하는 심볼을 사용한다.

```c
/* startup.c - 최소 Cortex-M startup (GCC). */
#include <stdint.h>

/* 링커 스크립트가 정의하는 심볼. 값이 아니라 "주소"만 의미가 있다. */
extern uint32_t _sidata;   /* .data 초기값의 flash 위치 (LMA) */
extern uint32_t _sdata;    /* .data 시작 (SRAM, VMA) */
extern uint32_t _edata;    /* .data 끝 */
extern uint32_t _sbss;     /* .bss 시작 */
extern uint32_t _ebss;     /* .bss 끝 */
extern uint32_t _estack;   /* 스택 top (SRAM 끝) */

extern int main(void);
extern void __libc_init_array(void);   /* newlib: C++ 생성자, init_array 실행 */
void _init(void) { }                   /* -nostartfiles면 crti.o가 빠지므로 __libc_init_array가 부르는 _init을 직접 제공 */
void SystemInit(void) __attribute__((weak));
void SystemInit(void) { }

void Reset_Handler(void);
void Default_Handler(void);

/* 핸들러 이름을 weak alias로 선언 -> 사용자가 같은 이름으로 정의하면 그게 우선 */
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void UART0_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));

typedef void (*vector_t)(void);

/* .isr_vector 섹션에 배치. 링커 스크립트가 flash 맨 앞에 KEEP 한다. */
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[] = {
    (vector_t)&_estack,     /* 0: 초기 MSP */
    Reset_Handler,          /* 1: Reset */
    NMI_Handler,            /* 2 */
    HardFault_Handler,      /* 3 */
    MemManage_Handler,      /* 4 */
    BusFault_Handler,       /* 5 */
    UsageFault_Handler,     /* 6 */
    0, 0, 0, 0,             /* 7-10: reserved (v8-M은 7 = SecureFault) */
    SVC_Handler,            /* 11 */
    DebugMon_Handler,       /* 12 */
    0,                      /* 13: reserved */
    PendSV_Handler,         /* 14 */
    SysTick_Handler,        /* 15 */
    UART0_IRQHandler,       /* 16: IRQ0 (예시, 실제 순서는 벤더 데이터시트) */
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    SystemInit();                       /* 클럭, flash wait state, FPU 활성화 */

    while (dst < &_edata) {             /* .data 복사: flash -> RAM */
        *dst++ = *src++;
    }
    for (dst = &_sbss; dst < &_ebss; ) {/* .bss 0 초기화 */
        *dst++ = 0u;
    }

    __libc_init_array();                /* C++ 전역 생성자, __attribute__((constructor)) */
    (void)main();

    for (;;) {                          /* main이 반환하면 여기서 멈춤 */
    }
}

void Default_Handler(void)
{
    for (;;) {                          /* 디버거로 IPSR를 보면 어떤 예외인지 알 수 있다 */
    }
}
```

줄 단위 설명:

- `extern uint32_t _sdata;` : 링커 심볼은 메모리를 차지하는 변수가 아니다. `&_sdata` 로 **주소만** 쓴다. `_sdata` 값을 읽으면 엉뚱한 메모리를 읽는다.
- `__attribute__((weak, alias("Default_Handler")))` : 사용자가 `void UART0_IRQHandler(void)`를 정의하지 않으면 Default_Handler로 연결된다. 정의하면 strong 심볼이 이긴다. **ISR 이름 오타**(`UART0_IRQHandle`)는 컴파일 에러 없이 Default_Handler로 빠진다. 흔한 버그다.
- `section(".isr_vector"), used` : `used`는 참조가 없어도 컴파일러가 지우지 않게 한다. 링커 쪽은 `KEEP()`이 필요하다(`--gc-sections` 때문).
- `(vector_t)&_estack` : 함수 포인터 배열에 스택 주소를 넣으려고 캐스팅한다. ISO C에서 엄밀히는 객체 포인터를 함수 포인터로 바꾸는 것이 정의되지 않지만 GCC/Arm 툴체인에서는 관례적으로 쓴다. 싫다면 `uint32_t` 배열로 만든다.
- `.data` 복사 전에 `SystemInit()` 을 부르는 이유: 클럭을 먼저 올려야 복사가 빠르다. 대신 `SystemInit` 안에서는 **전역 변수를 쓰면 안 된다**(아직 초기화 전). CMSIS의 `SystemCoreClock` 변수를 SystemInit에서 쓰는 템플릿이 있는데, 이 경우 벤더 startup은 순서를 다르게 하기도 한다. 벤더 템플릿의 순서를 꼭 확인한다.
- 워드 단위 복사는 링커 스크립트에서 `ALIGN(4)`로 경계를 맞췄다는 전제다.
- `__libc_init_array()` : newlib이 `.preinit_array`, `_init`, `.init_array`를 실행한다. `-nostartfiles`로 빌드하면서 C++ 전역 객체를 쓴다면 이 호출이 없으면 생성자가 안 돈다.

### 5.3 흔한 함정

- **.data 복사 누락**: 초기값이 있는 전역 변수가 0이나 쓰레기로 보인다. 디버거로 flash에서 바로 실행했더니 되고, 전원 리셋 후엔 안 되는 식으로 나타나기도 한다.
- **스택이 .bss와 겹침**: 링커 스크립트에서 스택 크기를 확보하지 않으면 깊은 호출에서 전역 변수가 조용히 망가진다.
- **워치독이 startup 중에 리셋**: 큰 `.bss`를 0으로 채우는 동안 ROM이 켜 둔 워치독이 만료된다. 벤더에 따라 부트 시 워치독이 켜져 있는 칩이 있다(벤더마다 다름).
- **FPU 활성화 전 FP 명령**: `-mfloat-abi=hard`로 빌드했는데 `SCB->CPACR`로 CP10/CP11을 켜기 전에 컴파일러가 FP 레지스터를 쓰면 NOCP UsageFault가 난다.

---

## 6. 링커 스크립트

### 6.1 LMA vs VMA

- **VMA(Virtual Memory Address)**: 실행 중 그 섹션이 있을 주소.
- **LMA(Load Memory Address)**: 이미지 안에 저장되는 주소(= flash에 구워지는 위치).
- `.text`, `.rodata`는 flash에서 바로 실행(XIP)하므로 LMA = VMA.
- `.data`는 VMA는 SRAM, LMA는 flash. startup이 LMA → VMA로 복사한다.
- `.bss`는 LMA가 필요 없다(이미지에 공간을 차지하지 않음, `NOLOAD`).

```
 Flash (0x00000000)                      SRAM (0x20000000)
 +------------------+                    +------------------+
 | .isr_vector      |                    | .data  (VMA)     | <-+ startup이 복사
 | .text            |                    +------------------+   |
 | .rodata          |                    | .bss   (0으로)   |   |
 | .ARM.exidx       |                    +------------------+   |
 +------------------+                    | heap  -> 위로    |   |
 | .data 초기값(LMA)| -------------------+------------------+---+
 +------------------+                    |       ...        |
 |   (빈 공간)       |                    | stack <- 아래로  |
 +------------------+                    +------------------+ _estack
```

### 6.2 최소 링커 스크립트

QEMU `lm3s6965evb`(Cortex-M3, flash 256KB, SRAM 64KB)에 맞춘 예시다.

```ld
/* app.ld - 최소 Cortex-M 링커 스크립트 (GNU ld) */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_stack_size = 0x1000;                 /* 4KB, 빌드 시 -Wl,--defsym으로 바꿀 수 있음 */
_estack     = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : {
    . = ALIGN(4);
    KEEP(*(.isr_vector))              /* gc-sections에도 지우지 말 것 */
  } > FLASH

  .text : {
    . = ALIGN(4);
    *(.text .text.*)                  /* -ffunction-sections 결과 포함 */
    *(.rodata .rodata.*)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
    __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
    __init_array_start = .;    KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
    __fini_array_start = .;    KEEP(*(.fini_array)) __fini_array_end = .;
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx : {                      /* 스택 unwind 테이블 (C++ 예외 끄면 거의 비어 있음) */
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH

  _sidata = LOADADDR(.data);          /* .data의 LMA */

  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    *(.ramfunc .ramfunc.*)            /* RAM에서 실행할 함수 (flash 쓰기 루틴 등) */
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH                  /* VMA는 RAM, LMA는 FLASH */

  .bss (NOLOAD) : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  .noinit (NOLOAD) : {                /* 리셋에도 값이 유지되는 영역 (crash 로그 등) */
    . = ALIGN(4);
    *(.noinit .noinit.*)
  } > RAM

  end = .;                            /* newlib _sbrk가 heap 시작으로 사용 */
  _end = .;

  ._stack_check : {                   /* 스택 공간이 남아 있는지 링크 타임 검사 */
    . = ALIGN(8);
    . = . + _stack_size;
  } > RAM
}
```

줄 단위 설명:

- `ENTRY(Reset_Handler)`: ELF 헤더의 entry point. 디버거가 로드 후 PC를 여기 둔다. 하드웨어 부팅에는 영향이 없다(하드웨어는 벡터 테이블을 읽는다).
- `MEMORY { ... (rx) ... }`: 영역 이름, 속성, 시작, 크기. 섹션이 넘치면 링커가 `region 'FLASH' overflowed by N bytes` 에러를 낸다. 이 에러가 "이미지가 안 들어간다"는 첫 신호다.
- `KEEP(*(.isr_vector))`: `--gc-sections`는 참조되지 않는 섹션을 지운다. 벡터 테이블은 코드에서 아무도 참조하지 않으므로 KEEP 없이는 사라진다. 그러면 flash 0번지에 `.text`가 들어가서 부팅이 안 된다.
- `*(.text .text.*)`: `-ffunction-sections`는 함수마다 `.text.func_name` 섹션을 만든다. 와일드카드로 모두 모은다.
- `.init_array`: C++ 전역 생성자 포인터 목록. `__libc_init_array()`가 이 범위를 돈다.
- `> RAM AT > FLASH`: VMA와 LMA를 나누는 핵심 문법이다.
- `(NOLOAD)`: 이미지에 내용을 넣지 않는다. `.noinit`은 startup이 0으로 채우지 않으므로 소프트 리셋 후에도 값이 남는다. 리셋 원인, crash 정보를 여기 남겼다가 다음 부팅에 텔레메트리로 보낸다.
- `._stack_check`: 실제 스택은 RAM 끝(`_estack`)에서 자라지만, 이 더미 섹션으로 "`.bss` 이후에 최소 스택 크기만큼 남아 있는가"를 링크 타임에 검사한다. 넘치면 RAM overflow 에러가 난다.

### 6.3 특정 섹션에 배치하기

```c
#include <stdint.h>

/* 리셋에도 유지되는 crash 정보 */
__attribute__((section(".noinit"))) volatile uint32_t g_reset_magic;

/* flash 지우기/쓰기 동안 flash에서 실행할 수 없으므로 RAM에 둔다 */
__attribute__((section(".ramfunc"), noinline, long_call))
void flash_program_word(volatile uint32_t *addr, uint32_t val)
{
    *addr = val;   /* 실제로는 벤더 flash 컨트롤러 시퀀스 (벤더마다 다름) */
}
```

- `long_call`: RAM(`0x2000xxxx`)과 flash(`0x0000xxxx`) 사이 거리가 Thumb `BL` 명령 범위(약 ±16MB)를 넘을 수 있어서 레지스터 경유 호출(`BLX`)을 쓰게 한다. 링커가 veneer를 자동 삽입하기도 하지만 명시가 안전하다.
- `.ramfunc`가 `.data` 안에 있으므로 startup의 `.data` 복사가 코드를 RAM으로 옮겨 준다.

---

## 7. MPU와 캐시

### 7.1 MPU란

MPU(Memory Protection Unit)는 **주소 변환 없이** 영역별 권한과 메모리 속성만 정한다. MMU처럼 가상 주소를 만들지 않는다. 용도는 다음과 같다.

- 스택 오버플로 감지: 각 task 스택 아래에 접근 금지 guard 영역
- NULL 포인터 역참조 감지: `0x00000000` 근처 첫 영역을 접근 금지(단, 벡터 테이블이 거기 있으면 VTOR를 옮긴 뒤에)
- 코드 영역 쓰기 금지, 데이터 영역 실행 금지(XN)
- unprivileged task 격리
- 캐시 속성 지정(M7: DMA 버퍼를 non-cacheable로)

### 7.2 ARMv7-M MPU vs ARMv8-M MPU

| 항목 | ARMv7-M (M3/M4/M7) | ARMv8-M (M23/M33/M55) |
|---|---|---|
| 영역 지정 | `MPU->RBAR`(base) + `MPU->RASR`(size, 속성, enable) | `MPU->RBAR`(base, AP, XN) + `MPU->RLAR`(limit, attr index, enable) |
| 크기 규칙 | 2의 거듭제곱, 최소 32B, **base가 size에 정렬** | 32바이트 단위 base/limit, 정렬 제약 거의 없음 |
| subregion | 8개 subregion disable 가능 (`SRD`) | 없음 |
| 메모리 속성 | RASR의 TEX/C/B/S 비트 | `MPU->MAIR0/1` 속성 테이블 + index |
| 영역 수 | 보통 8 (M7은 8 또는 16) | 구현마다 다름 (보통 8~16) |
| 겹침 | 번호 큰 영역이 우선 | 영역 겹침 금지 (겹치면 fault) |

ARMv7-M의 "2의 거듭제곱 + 정렬" 규칙 때문에 RTOS task 스택을 MPU로 보호하려면 스택 크기와 정렬을 2의 거듭제곱으로 맞춰야 한다. 메모리 낭비가 크다. ARMv8-M이 이 문제를 고쳤다.

```c
#include "device.h"   /* core_cm4.h 포함 (ARMv7-M) */

/* 예: 스택 바로 아래 32바이트를 no-access guard로 만든다 (스택 오버플로 감지) */
void mpu_guard_region(uint32_t guard_base /* 32B 정렬 */)
{
    __DMB();
    MPU->CTRL = 0;                                  /* 설정 중엔 MPU 끔 */

    MPU->RNR  = 0;                                  /* region 0 선택 */
    MPU->RBAR = guard_base & MPU_RBAR_ADDR_Msk;
    MPU->RASR = (0u << MPU_RASR_AP_Pos)             /* AP=000: 권한 무관 no access */
              | (1u << MPU_RASR_XN_Pos)             /* 실행 금지 */
              | (4u << MPU_RASR_SIZE_Pos)           /* SIZE=4 -> 2^(4+1) = 32 bytes */
              | MPU_RASR_ENABLE_Msk;

    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk             /* 나머지 영역은 기본 메모리 맵 사용 */
              | MPU_CTRL_ENABLE_Msk;
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;        /* MemManage fault 활성화 */
    __DSB();                                        /* MPU 설정 완료 보장 */
    __ISB();                                        /* 이후 명령이 새 설정으로 fetch */
}
```

- `SIZE` 필드는 "영역 크기 = 2^(SIZE+1)"이다. 32바이트는 SIZE=4.
- `PRIVDEFENA`: 켜지 않으면 MPU를 켜는 순간 설정하지 않은 모든 영역이 접근 금지가 되어 바로 fault가 난다.
- `__DSB(); __ISB();`: 시스템 제어 레지스터를 바꾼 뒤엔 barrier로 적용을 보장한다. CMSIS에는 `ARM_MPU_SetRegion()` 같은 헬퍼(`mpu_armv7.h`, `mpu_armv8.h`)가 있다.

### 7.3 캐시 (Cortex-M7, M55, M85)

Cortex-M3/M4에는 코어 캐시가 없다(벤더가 flash 앞에 prefetch/가속기를 두기도 한다. 예: STM32의 ART accelerator). M7부터 L1 I-cache/D-cache가 있고, 여기서 **DMA coherency** 문제가 생긴다.

```
 CPU --> D-cache --> AXI bus --> SRAM <-- DMA (캐시를 모름)

 문제 1 (TX): CPU가 버퍼에 썼지만 write-back 캐시에만 있음 -> DMA는 옛날 데이터 전송
   해결: DMA 시작 전 SCB_CleanDCache_by_Addr(buf, len)
 문제 2 (RX): DMA가 SRAM에 썼지만 CPU는 캐시의 옛날 줄을 읽음
   해결: DMA 완료 후 SCB_InvalidateDCache_by_Addr(buf, len)
       (버퍼는 32바이트 캐시 라인 정렬 + 크기도 32의 배수: 인접 변수까지 invalidate되는 것 방지)
 대안: MPU로 DMA 버퍼 영역을 non-cacheable 지정, 또는 DTCM에 배치(DMA 접근 가능 여부는 칩마다 다름)
```

CMSIS 함수: `SCB_EnableICache()`, `SCB_EnableDCache()`, `SCB_CleanDCache_by_Addr()`, `SCB_InvalidateDCache_by_Addr()`, `SCB_CleanInvalidateDCache()`.

> Don 경험과 연결: SSD에서 호스트 DMA 버퍼와 CPU 캐시 사이 coherency를 다뤘다면 똑같은 문제다. 오디오 I2S DMA ping-pong 버퍼(C03, C08)에서 M7/M55급 MCU를 쓴다면 이 invalidate를 빼먹어 "가끔 이전 프레임 소리가 섞이는" 버그가 전형적이다.

---

## 8. Cortex-A는 무엇이 다른가

### 8.1 비교표

| 항목 | Cortex-M | Cortex-A (ARMv8-A) |
|---|---|---|
| 권한 모델 | Thread/Handler, privileged/unprivileged | Exception Level EL0(앱) / EL1(커널) / EL2(하이퍼바이저) / EL3(secure monitor) |
| 보안 | TrustZone-M (v8-M, 선택) | TrustZone: Secure/Non-secure world, EL3가 전환 |
| 메모리 보호 | MPU (변환 없음) | MMU: 다단계 page table, VA→PA 변환, TLB |
| 인터럽트 | NVIC (코어에 통합, 자동 stacking, 벡터 = 핸들러 주소) | GIC (Distributor + Redistributor/CPU interface, SPI/PPI/SGI/LPI). 벡터 테이블(`VBAR_ELx`)엔 **명령어**, 소프트웨어가 컨텍스트 저장 |
| 부팅 | 벡터 테이블 word0=SP, word1=PC | ROM → 다단계 bootloader → OS |
| 캐시 | 없거나 L1만 | L1/L2/(L3), 캐시 유지 명령, coherency interconnect |
| 인터럽트 지연 | 수십 사이클, 결정적 | 가변(캐시/TLB miss, OS) |
| OS | bare-metal / RTOS | Linux/Android, 드물게 RTOS |
| 드라이버 모델 | 레지스터 직접, HAL | Linux 커널 드라이버 + devicetree |

### 8.2 ARMv8-A Exception Level

```
          Non-secure world            Secure world
 EL0   |  Android apps          |  |  Trusted apps (TA)     |
 EL1   |  Linux kernel          |  |  Secure OS (OP-TEE 등) |
 EL2   |  Hypervisor (선택)     |  |  (v8.4+ Secure EL2)    |
       +------------------------+--+------------------------+
 EL3   |        Secure monitor (TF-A BL31) - world 전환(SMC)  |
       +------------------------------------------------------+
```

- 낮은 EL은 `SVC`(EL0→EL1), `HVC`(→EL2), `SMC`(→EL3) 명령으로 높은 EL을 호출한다.
- 예외가 나면 `VBAR_ELx`가 가리키는 벡터 테이블로 가고, 복귀 주소는 `ELR_ELx`, 상태는 `SPSR_ELx`에 저장된다. 범용 레지스터 저장은 소프트웨어(커널 entry 코드) 몫이다.
- Don이 아는 Cortex-R8의 모드 기반 구조(ARMv7)와 개념이 비슷하다. 차이는 AArch64에서 "모드" 대신 "EL"이라는 수직 계층으로 정리되었다는 점이다.

### 8.3 GIC

```
 주변장치 --SPI--> +-------------+      +-----------------+
                  | Distributor | ---> | Redistributor   | ---> CPU0 (IRQ/FIQ)
 타이머 등 --PPI-> | (GICD_*)    |      | + CPU interface | 
 코어간  --SGI-->  +-------------+      | (GICR_*, ICC_*) | ---> CPU1
                                        +-----------------+
 인터럽트 처리: ICC_IAR1_EL1 읽기(acknowledge, INTID 얻기) -> 처리
               -> ICC_EOIR1_EL1 쓰기(end of interrupt)
```

- SGI(Software Generated, INTID 0~15), PPI(Private Peripheral, 16~31), SPI(Shared Peripheral, 32~1019), LPI(GICv3의 메시지 기반, 8192 이상).
- GICv2는 CPU interface를 메모리 매핑 레지스터(`GICC_*`)로, GICv3부터는 시스템 레지스터(`ICC_*`)로 접근한다.
- NVIC와 가장 큰 차이: NVIC는 핸들러 주소로 바로 벡터링하지만, GIC는 코어에 "IRQ가 왔다"만 알리고 소프트웨어가 IAR을 읽어 번호를 얻고 분기한다.

### 8.4 Cortex-A 부트 체인

```
 [Boot ROM] (칩 내장, 변경 불가, root of trust)
     |  서명 검증 후 SRAM으로 로드
     v
 [BL1/BL2]  TF-A: DRAM 초기화(DDR training), 다음 이미지 로드·검증
     |
     +--> [BL31] EL3 runtime (PSCI 전원관리, SMC 처리) - 계속 상주
     +--> [BL32] Secure OS (OP-TEE 등, 선택)
     +--> [BL33] Non-secure bootloader = U-Boot 또는 UEFI (EL2/EL1)
               |  커널 Image + DTB(devicetree blob) + initramfs 로드
               v
          [Linux kernel] -> init -> Android
```

- **TF-A**(Trusted Firmware-A): ARM이 관리하는 EL3 레퍼런스 펌웨어. BL1/BL2/BL31/BL32/BL33 단계 이름을 쓴다.
- **U-Boot**: 가장 널리 쓰는 오픈소스 bootloader. 스토리지 드라이버, 네트워크, 커널 로드, 부트 스크립트.
- **Qualcomm SoC**는 자체 체인을 쓴다. 일반적으로 PBL(Primary Boot Loader, ROM) → XBL(eXtensible Boot Loader, DDR 초기화 등) → ABL(Android Boot Loader) → Linux 순서로 알려져 있고, 세대마다 세부가 다르다(벤더 문서 확인 필요). Hark가 Qualcomm 칩을 쓴다면 이 체인은 벤더 BSP로 받고, 이 역할은 주로 커스터마이징과 디버깅을 하게 될 가능성이 크다(추정).
- Cortex-A BSP에서 FW 엔지니어가 만지는 것: devicetree(`.dts`)에서 I2C/SPI 노드·GPIO·인터럽트 기술, 커널 드라이버(probe/remove), 부트로더 설정, 전원 시퀀스, PSCI 기반 CPU idle/suspend.

> Don 경험과 연결: Apple RF 통합에서 AP 쪽 커널 드라이버가 라디오 칩과 PCIe로 통신하던 구조가 바로 "Cortex-A + OS 드라이버" 쪽이다. MCU 쪽은 SSD 펌웨어와 같은 bare-metal/RTOS 쪽이다. Hark 면접에서는 "양쪽 경계를 모두 봤다"고 말할 수 있다.

---

## 9. 툴체인

### 9.1 빌드 흐름

```
 main.c --arm-none-eabi-gcc -c--> main.o \
 startup.c ----------------------> startup.o  >--arm-none-eabi-gcc(ld)--> app.elf --objcopy--> app.bin / app.hex
 drivers.c ----------------------> drivers.o /        ^  app.ld                  |
                                                      |  libc_nano.a, libgcc.a   +--> app.map (-Wl,-Map)
                                                                                 +--> size/objdump/nm/addr2line
```

- `arm-none-eabi-`: 타깃 ARM, OS 없음(none), EABI. Linux 타깃은 `aarch64-linux-gnu-` 같은 다른 툴체인이다.
- 배포: Arm GNU Toolchain(developer.arm.com). LLVM 계열로는 Arm Toolchain for Embedded(구 LLVM Embedded Toolchain for Arm)도 있다. 상용은 Arm Compiler 6(armclang), IAR.

### 9.2 핵심 컴파일 옵션

| 옵션 | 의미 | 왜 중요한가 |
|---|---|---|
| `-mcpu=cortex-m4` | 코어 지정 (명령어 선택, 스케줄링) | 잘못 지정하면 M0+에서 M4 명령 실행 → UsageFault/HardFault |
| `-mthumb` | Thumb 명령어 생성 | Cortex-M 필수 (M 타깃은 기본값이지만 명시) |
| `-mfpu=fpv4-sp-d16` | M4F의 FPU 종류 | M7은 `fpv5-d16` 또는 `fpv5-sp-d16` |
| `-mfloat-abi=hard` | FP 인자를 FP 레지스터로 전달 | `soft`/`softfp`/`hard` 섞으면 링크 에러. 라이브러리와 일치 필요 |
| `-Os` / `-O2` / `-Og` | 크기 / 속도 / 디버그 친화 최적화 | MCU 양산은 보통 `-Os`, hot path만 `-O2` 속성 |
| `-g3` | 디버그 정보 (매크로 포함) | ELF에만 들어가고 bin 크기는 늘지 않음 |
| `-ffunction-sections -fdata-sections` | 함수·변수마다 섹션 분리 | `--gc-sections`와 함께 미사용 코드 제거 |
| `-Wl,--gc-sections` | 참조 없는 섹션 제거 | 이미지 크기 대폭 감소 |
| `-fno-common` | 초기화 안 된 전역을 COMMON 대신 `.bss`로 | 중복 정의를 링크 에러로 잡음 (GCC 10부터 기본) |
| `-fstack-usage` | 함수별 스택 사용량 `.su` 파일 생성 | 최악 스택 추정 |
| `-Wall -Wextra -Wconversion` | 경고 | 정수 변환 버그 조기 발견 (C02) |
| `--specs=nano.specs` | newlib-nano 사용 | printf 등 크기 절감 |
| `--specs=nosys.specs` | 시스템 콜 stub (`_write`, `_sbrk` 등) | 링크만 되게 함 |
| `-nostartfiles` | crt0 등 기본 startup 제외 | 자체 startup 사용 시 |
| `-T app.ld` | 링커 스크립트 지정 | |
| `-Wl,-Map=app.map` | map 파일 생성 | 크기·배치 분석 |
| `-Wl,--print-memory-usage` | 영역별 사용률 출력 | CI에서 크기 추적 |
| `-flto` | 링크 타임 최적화 | 크기↓, 대신 디버깅 어려움, `used`/`KEEP` 주의 |

### 9.3 빌드 명령 예

```sh
CFLAGS="-mcpu=cortex-m3 -mthumb -Os -g3 -Wall -Wextra \
        -ffunction-sections -fdata-sections -fno-common -fstack-usage"
LDFLAGS="-T app.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
         -Wl,--gc-sections -Wl,-Map=app.map -Wl,--print-memory-usage"

arm-none-eabi-gcc $CFLAGS -c startup.c -o startup.o
arm-none-eabi-gcc $CFLAGS -c main.c    -o main.o
arm-none-eabi-gcc $CFLAGS startup.o main.o $LDFLAGS -o app.elf

arm-none-eabi-objcopy -O binary app.elf app.bin     # 플래시에 굽는 raw 이미지
arm-none-eabi-objcopy -O ihex   app.elf app.hex     # 주소 정보 포함 hex
```

### 9.4 이미지 분석 도구

```sh
arm-none-eabi-size app.elf
#    text    data     bss     dec     hex filename
#    4212     120    2080    6412    190c app.elf
```

- `text` = `.text` + `.rodata` + 벡터 등 flash에 있는 읽기 전용.
- `data` = `.data` 초기값. **flash와 RAM 둘 다 차지**한다.
- `bss` = RAM만 차지.
- 따라서 flash 사용량 ≈ text + data, RAM 정적 사용량 ≈ data + bss (+ 스택 + 힙).

```sh
arm-none-eabi-nm --size-sort -S -C app.elf | tail -20   # 가장 큰 심볼 20개 (-C: C++ demangle)
arm-none-eabi-objdump -d -S app.elf | less              # 소스 섞인 디스어셈블리
arm-none-eabi-objdump -h app.elf                        # 섹션 헤더 (VMA/LMA 확인)
arm-none-eabi-readelf -S app.elf                        # 섹션 목록
arm-none-eabi-readelf -s app.elf | grep Handler         # 심볼 테이블에서 핸들러 확인
arm-none-eabi-addr2line -e app.elf -f -C 0x000012a4     # crash PC -> 함수:파일:줄
```

- **objdump -h**에서 `.data`의 VMA(`0x20000000`)와 LMA(flash 주소)가 다르게 나오는지 확인하는 것이 링커 스크립트 검증의 기본이다.
- **addr2line**은 필드 crash 로그의 stacked PC를 소스 줄로 바꾸는 도구다. 그래서 **출하한 모든 빌드의 ELF를 보관**해야 한다(C07, C10).

### 9.5 map 파일 읽기

```
 Memory Configuration
 Name             Origin             Length             Attributes
 FLASH            0x00000000         0x00040000         xr
 RAM              0x20000000         0x00010000         xrw

 .text           0x00000040      0xf80
  .text.main     0x00000040       0x3c main.o
                 0x00000040                main
  .text.uart_init
                 0x0000007c       0x58 drivers.o
 ...
 Discarded input sections
  .text.unused_func   0x00000000   0x20 drivers.o
```

- 각 입력 섹션이 어느 object 파일에서 왔는지, 크기가 얼마인지 나온다. "이미지가 왜 커졌나"는 map을 빌드 전후로 diff하면 바로 보인다.
- **Discarded input sections**: `--gc-sections`로 제거된 것. 여기 ISR가 들어 있으면 KEEP/`used`가 빠진 것이다.
- libc에서 끌려온 큰 함수(`_vfprintf_r`, `__aeabi_ddiv` 등 soft-float 루틴)를 찾는 데도 유용하다. `printf("%f")` 하나로 수십 KB가 늘어나는 일이 흔하다.

### 9.6 흔한 함정

- **float-abi 불일치**: `hard`로 빌드한 object와 `soft` 라이브러리를 섞으면 "uses VFP register arguments, ... does not" 링크 에러.
- **`-O0` 디버그 빌드만 테스트**: 최적화 빌드에서 `volatile` 누락, 초기화 안 된 변수, UB가 드러난다(C02). 릴리스 설정으로 테스트해야 한다.
- **`-flto`와 ISR**: LTO가 weak 심볼·섹션 속성과 얽혀 핸들러를 잘못 해석하는 경우가 있다. ISR에 `used`를 붙이고 map으로 확인한다.
- **디버그 정보를 bin에서 찾기**: `-g`는 ELF에만 영향이 있다. bin 크기와 무관하다.

---

## 10. CMSIS

CMSIS(Common Microcontroller Software Interface Standard)는 Arm이 정의한 Cortex-M 공통 소프트웨어 인터페이스다. 현재 CMSIS 6 버전이 GitHub `ARM-software/CMSIS_6`에서 관리된다.

| 구성 요소 | 내용 |
|---|---|
| CMSIS-Core(M) | `core_cm4.h` 등 코어 레지스터 정의(`SCB`, `NVIC`, `SysTick`, `MPU`), intrinsic(`__DSB`, `__WFI`...), 벤더 디바이스 헤더와 `system_<device>.c` 규약 |
| CMSIS-DSP | 필터, FFT, 행렬, fixed-point(`q15_t`, `q31_t`) 함수 (별도 repo) |
| CMSIS-NN | INT8/INT16 신경망 커널, TFLite Micro가 백엔드로 사용 (C08) |
| CMSIS-RTOS2 | RTOS 공통 API (`osThreadNew` 등). Keil RTX5, FreeRTOS 래퍼 존재 |
| CMSIS-Driver | 주변장치 드라이버 공통 인터페이스 (USART, SPI, I2C...) |
| CMSIS-DAP | 디버그 프로브 펌웨어/프로토콜 (오픈 디버그 프로브) |
| CMSIS-Pack | 디바이스 지원 패키지 배포 형식 |
| CMSIS-View / Compiler | 이벤트 기록, 컴파일러 추상화 |

### 10.1 디바이스 헤더 구조

```
 device.h (벤더 작성, 예: stm32f4xx.h, nrf52840.h)
   - IRQn_Type enum (Reset/NMI/... 음수, 주변장치 0~)
   - #define __NVIC_PRIO_BITS 3,  __MPU_PRESENT 1,  __FPU_PRESENT 1
   - #include "core_cm4.h"   <- Arm 제공
   - 주변장치 구조체 typedef + 베이스 주소 (UART0, SPI1 ...)
 system_device.c
   - SystemInit(): 클럭 설정
   - SystemCoreClock 변수, SystemCoreClockUpdate()
```

### 10.2 자주 쓰는 CMSIS-Core 함수와 intrinsic

| 함수 | 역할 |
|---|---|
| `SysTick_Config(ticks)` | SysTick을 ticks 주기로 설정, 인터럽트 enable (24비트 제한) |
| `NVIC_EnableIRQ/DisableIRQ/SetPriority` | NVIC 제어 |
| `NVIC_SystemReset()` | `AIRCR.SYSRESETREQ`로 소프트 리셋 |
| `__disable_irq()` / `__enable_irq()` | PRIMASK 설정/해제 |
| `__get_PRIMASK()` / `__set_BASEPRI()` | 특수 레지스터 접근 |
| `__DMB()` / `__DSB()` / `__ISB()` | 메모리/데이터 동기화/명령 동기화 barrier |
| `__WFI()` / `__WFE()` / `__SEV()` | sleep, 이벤트 (C05) |
| `__REV()` / `__REV16()` | 바이트 순서 뒤집기 (C02) |
| `__CLZ()` | leading zero count |
| `__LDREXW()` / `__STREXW()` / `__CLREX()` | exclusive access (C02) |

```c
#include "device.h"

volatile uint32_t g_ms;

void SysTick_Handler(void) { g_ms++; }      /* 1ms마다 */

int main(void)
{
    SystemCoreClockUpdate();                 /* 실제 클럭으로 SystemCoreClock 갱신 */
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        for (;;) { }                         /* reload 값이 24비트 초과 -> 실패 */
    }
    for (;;) {
        __WFI();                             /* 인터럽트까지 sleep */
    }
}
```

- `SysTick_Config()`는 `SysTick->LOAD = ticks - 1`, `VAL = 0`, `CTRL`에 CLKSOURCE/TICKINT/ENABLE를 설정한다. SysTick 우선순위는 최저로 설정한다.
- SysTick 레지스터: `CTRL`(0xE000E010), `LOAD`, `VAL`, `CALIB`. 카운터는 24비트 down-counter다.
- `g_ms`에 `volatile`이 필요한 이유(ISR에서 바뀜)와 32비트 읽기가 원자적인 이유는 C02에서 다룬다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 벡터 테이블 KEEP 누락 | 전원 켜면 아무 반응 없음, 디버거로 PC가 이상한 곳 | `--gc-sections`가 `.isr_vector` 제거 | 링커 스크립트 `KEEP()` + `used`, map의 Discarded 확인 |
| ISR 이름 오타 | 인터럽트 오면 Default_Handler 무한 루프 | weak alias로 연결됨 | 디버거로 IPSR 확인, `readelf -s`로 심볼 확인 |
| `.data` 복사 누락/순서 오류 | 초기값 있는 전역이 0/쓰레기 | startup 버그, 링커 심볼 오용 | `objdump -h`로 LMA/VMA 확인, 복사 루프 점검 |
| NVIC 우선순위 시프트 착각 | FreeRTOS `configASSERT` 실패, 커널 데이터 깨짐 | `NVIC->IP`에 직접 쓰면서 `__NVIC_PRIO_BITS` 무시 | `NVIC_SetPriority()` 사용, 구현 비트 확인 |
| 주변장치 플래그 안 지움 | ISR가 끝나자마자 재진입, 메인 코드 멈춤(interrupt storm) | 레벨 인터럽트 소스 유지 | ISR에서 원인 플래그 clear 후 `__DSB()` (버스 write buffer 때문) |
| M7 DMA 버퍼 캐시 무시 | 간헐적 옛 데이터, 오디오 글리치 | D-cache coherency | clean/invalidate, 정렬, MPU non-cacheable |
| FPU 활성화 누락 | 첫 float 연산에서 UsageFault(NOCP) | `CPACR` 미설정 | `SystemInit`에서 `SCB->CPACR`의 CP10/CP11 필드(bit 20~23)를 full access로 설정 후 `__DSB()`/`__ISB()` |
| float-abi 혼용 | 링크 에러 "VFP register arguments" | object/라이브러리 ABI 불일치 | 전체 빌드 옵션 통일 |
| MPU 켤 때 PRIVDEFENA 누락 | MPU enable 직후 fault | 기본 맵 비활성 | `MPU_CTRL_PRIVDEFENA_Msk` 설정 |
| 부트로더→앱 점프 시 VTOR 미변경 | 앱에서 인터럽트 나면 부트로더 핸들러로 감 | VTOR가 부트로더 테이블 | 점프 전 `SCB->VTOR = app_base`, MSP 재설정, 인터럽트/주변장치 정리 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Walk me through what happens from power-on reset to `main()` on a Cortex-M.

**A.** 리셋이 풀리면 코어가 VTOR(보통 0번지)에서 첫 워드를 MSP로, 두 번째 워드를 PC로 읽는다. Reset_Handler는 클럭과 flash wait state를 설정하고, `.data` 초기값을 flash에서 SRAM으로 복사하고, `.bss`를 0으로 채우고, C++ 생성자를 실행한 뒤 `main`을 부른다. 실제 제품엔 그 앞에 ROM과 서명 검증하는 2단계 bootloader가 있다.

English: "On reset the core loads the initial MSP from the first word of the vector table and the reset handler address from the second word. The reset handler sets up clocks and flash wait states, copies initialized data from flash to RAM, zeroes BSS, runs static constructors through `__libc_init_array`, and calls `main`. In a product there's usually a ROM stage and a signed second-stage bootloader like MCUboot in front of that, which validates the image, relocates VTOR and jumps to the app."

**Q.** What does the hardware do automatically on exception entry on a Cortex-M, and why does it matter?

**A.** 현재 스택에 R0-R3, R12, LR, PC, xPSR 8워드를 push하고(FPU 사용 중이면 FP 레지스터 공간도), 벡터를 fetch하고, LR에 EXC_RETURN을 넣는다. caller-saved 레지스터를 하드웨어가 저장하니 ISR를 일반 C 함수로 쓸 수 있고, tail-chaining과 late arrival로 지연이 짧고 결정적이다.

English: "The core stacks eight words, R0 to R3, R12, LR, the return PC and xPSR, which are exactly the AAPCS caller-saved registers, so an ISR can be a plain C function. In parallel it fetches the handler address from the vector table, and LR gets an EXC_RETURN value that tells the hardware which stack and mode to return to. With tail-chaining and late arrival, latency is around twelve cycles on an M3 or M4 with zero wait-state memory."

**Q.** Explain interrupt priorities on the NVIC. What's BASEPRI for?

**A.** 숫자가 작을수록 높고, 칩은 상위 몇 비트만 구현한다(`__NVIC_PRIO_BITS`). PRIGROUP으로 선점용 group과 subpriority를 나눈다. BASEPRI는 특정 숫자 이상의 인터럽트만 막아서, RTOS critical section 동안에도 긴급한 인터럽트는 지연 없이 받게 해 준다. M0+에는 없다.

English: "Lower numbers are more urgent, and only the top few bits of the eight-bit field are implemented, so I always use `NVIC_SetPriority` rather than writing the raw register. BASEPRI masks only interrupts at or below a given urgency, which is how FreeRTOS on M3 and M4 implements critical sections: anything above `configMAX_SYSCALL_INTERRUPT_PRIORITY` still runs with zero added latency, but it must not call RTOS APIs."

**Q.** What is the difference between LMA and VMA in a linker script?

**A.** VMA는 실행 중 주소, LMA는 이미지에 저장되는 주소다. `.data`는 VMA가 RAM, LMA가 flash라 startup이 복사해야 한다. `> RAM AT > FLASH`로 표현하고 `LOADADDR()`로 LMA를 얻는다.

English: "VMA is where the section lives at run time, LMA is where it's stored in the image. For `.data` the VMA is RAM and the LMA is flash, so the startup code copies it using symbols from the linker script. I verify it with `objdump -h`, where the two addresses should differ."

**Q.** MPU vs MMU?

**A.** MPU는 주소 변환 없이 영역별 권한·속성만 준다. 영역 수가 적고(8~16) 결정적이다. MMU는 page table로 가상 주소를 물리 주소로 바꾸고 프로세스 격리, demand paging을 한다. TLB miss 때문에 지연이 가변이다. ARMv7-M MPU는 2의 거듭제곱 크기·정렬 제약이 있고 ARMv8-M은 base/limit 방식이라 유연하다.

English: "An MPU only assigns permissions and memory attributes to a small number of regions, with no address translation, so timing stays deterministic. An MMU translates virtual to physical addresses through page tables, which gives process isolation and virtual memory at the cost of TLB misses. On MCUs I use the MPU for stack guards, null-pointer traps, execute-never on RAM and non-cacheable DMA buffers."

**Q.** How is booting a Cortex-A application processor different?

**A.** ROM이 첫 단계 bootloader를 검증·로드하고, DDR 초기화, TF-A BL31(EL3 상주), U-Boot나 UEFI, 그다음 커널과 devicetree 순서다. 각 단계가 다음 단계를 검증하는 chain of trust다. Qualcomm은 PBL, XBL, ABL 같은 자체 단계를 쓴다.

English: "On an A-class SoC the boot ROM authenticates and loads the first-stage loader into on-chip SRAM, which trains DDR and loads the rest: TF-A's BL31 stays resident at EL3 for PSCI and secure monitor calls, then a non-secure loader like U-Boot or UEFI loads the kernel, the devicetree and the ramdisk. Each stage verifies the next, and Qualcomm has its own equivalents like PBL, XBL and ABL."

**Q.** Your image doesn't fit in flash anymore. What do you do?

**A.** map 파일과 `nm --size-sort`로 큰 심볼을 찾고, 빌드 전후 map을 diff한다. `-Os`, `--gc-sections`, newlib-nano, `printf` float 제거, LTO, 큰 테이블을 압축하거나 외부 flash로 옮기기, 사용하지 않는 기능 Kconfig로 끄기 순으로 본다.

English: "First I measure: `size` for the totals, the map file and `nm --size-sort` for the biggest symbols, and a diff of the map against the last build that fit. Typical wins are `-Os` with function sections and gc-sections, newlib-nano without float printf, LTO, removing accidental soft-float or C++ library pulls, and moving large constant tables like model weights to external flash."

---

## 13. 직접 해보기

### 실습 1. QEMU에서 bare-metal Cortex-M3 부팅

위 `startup.c`와 `app.ld`, 아래 `main.c`로 QEMU `lm3s6965evb` 보드에서 부팅한다. QEMU의 이 보드는 UART0를 `0x4000C000`에 두고 있어서 데이터 레지스터에 쓰면 콘솔에 출력된다.

```c
/* main.c */
#include <stdint.h>
#define UART0_DR (*(volatile uint32_t *)0x4000C000u)   /* lm3s6965 UART0 data register */

static const char msg[] = "hello from Cortex-M3\n";   /* .rodata (flash) */
static uint32_t counter = 42u;                        /* .data (flash->RAM 복사 확인) */
static uint32_t zeroed;                               /* .bss */

int main(void)
{
    for (const char *p = msg; *p != '\0'; ++p) {
        UART0_DR = (uint32_t)*p;
    }
    UART0_DR = (counter == 42u && zeroed == 0u) ? 'O' : 'X';   /* startup 검증 */
    UART0_DR = '\n';
    for (;;) { }
}
```

```sh
# 빌드 (9.3절 옵션, startup.c에서 UART0_IRQHandler는 weak라 그대로 둬도 됨)
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -g3 -ffunction-sections -fdata-sections \
  startup.c main.c -T app.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
  -Wl,--gc-sections -Wl,-Map=app.map -o app.elf
qemu-system-arm -M lm3s6965evb -nographic -kernel app.elf
# 종료: Ctrl-A 다음 X
```

`O`가 출력되면 `.data` 복사와 `.bss` 초기화가 성공한 것이다. 링커 스크립트에서 `AT > FLASH`를 지우고 다시 해 보면서 무엇이 깨지는지 관찰한다.

### 실습 2. GDB로 예외 진입 관찰

```sh
qemu-system-arm -M lm3s6965evb -nographic -kernel app.elf -S -s   # -S: 멈춘 채 시작, -s: gdb :1234
# 다른 터미널
arm-none-eabi-gdb app.elf        # 또는 gdb-multiarch
(gdb) target remote :1234
(gdb) info registers msp pc      # 리셋 직후 MSP = _estack, PC = Reset_Handler
(gdb) break main
(gdb) continue
(gdb) x/16wx 0x00000000          # 벡터 테이블 덤프: 첫 워드 = 0x20010000
```

`main.c`에서 `__asm volatile("udf #0");`(정의되지 않은 명령)을 넣어 UsageFault를 일으키면 SHCSR에서 켜지 않은 상태이므로 HardFault로 escalation된다. 그 뒤 `x/8wx $sp`로 stacked 프레임(R0..xPSR)을 읽고, `frame[6]`(stacked PC)를 `addr2line`으로 소스 줄에 매핑해 본다. (QEMU의 fault 모델링은 실제 실리콘과 다를 수 있으니 레지스터 값은 참고용이다.)

### 실습 3. 이미지 분석

```sh
arm-none-eabi-size app.elf
arm-none-eabi-objdump -h app.elf | grep -E "data|bss|text"
arm-none-eabi-nm --size-sort -S app.elf | tail
# main.c에 #include <stdio.h> 후 printf("%f", 1.5) 추가 -> size와 map 비교
```

float `printf` 한 줄이 text를 얼마나 키우는지, map에서 어떤 libc 객체가 끌려왔는지 확인한다.

### 실습 4. 실제 보드 (nRF52840 DK + Zephyr)

```sh
west build -b nrf52840dk/nrf52840 zephyr/samples/hello_world
ls build/zephyr/ | grep -E "zephyr.(elf|map|lst)"
arm-none-eabi-size build/zephyr/zephyr.elf
west build -t rom_report     # Zephyr의 flash 사용 트리 리포트
west build -t ram_report
west flash && west debug     # J-Link/GDB
```

Zephyr가 생성한 링커 스크립트(`build/zephyr/linker.cmd`)와 벡터 테이블(`_vector_table`)을 이 노트의 최소 버전과 비교한다. 보드 이름 형식은 Zephyr 버전에 따라 `nrf52840dk_nrf52840`(구) 또는 `nrf52840dk/nrf52840`(신)이다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ARMv7-M / ARMv8-M | M 프로파일 아키텍처 버전 | M3/M4/M7 vs M23/M33/M55 |
| MSP / PSP | Main / Process Stack Pointer | 핸들러·커널은 MSP, RTOS task는 PSP |
| xPSR | Program Status Register | APSR+IPSR+EPSR 합친 뷰 |
| PRIMASK / BASEPRI / FAULTMASK | 인터럽트 마스크 레지스터 | 전부 / 우선순위 이하 / HardFault까지 |
| EXC_RETURN | 예외 복귀 매직 값 | LR에 들어가며 복귀 모드·스택·FP 프레임 결정 |
| NVIC | Nested Vectored Interrupt Controller | Cortex-M 내장 인터럽트 컨트롤러 |
| VTOR | Vector Table Offset Register | 벡터 테이블 위치 (`0xE000ED08`) |
| SCB | System Control Block | CPUID, AIRCR, SHCSR, CFSR 등 |
| Tail-chaining | 연속 ISR 사이 unstack/restack 생략 | 인터럽트 처리량 향상 |
| Lazy stacking | FP 레지스터 저장 지연 | FP 안 쓰는 ISR 지연 보호 |
| LMA / VMA | Load / Virtual Memory Address | 저장 위치 / 실행 위치 |
| `.data` / `.bss` | 초기화된 / 0 초기화 전역 | 복사 대상 / 0 채움 대상 |
| XIP | Execute In Place | flash에서 바로 코드 실행 |
| MPU | Memory Protection Unit | 변환 없는 영역별 권한·속성 |
| MMU | Memory Management Unit | 가상→물리 주소 변환 + 보호 |
| EL0~EL3 | Exception Level | ARMv8-A 권한 계층 |
| GIC | Generic Interrupt Controller | Cortex-A 인터럽트 컨트롤러 |
| TF-A | Trusted Firmware-A | EL3 레퍼런스 펌웨어 (BL1/BL2/BL31) |
| U-Boot | Universal Boot Loader | Linux 부팅용 오픈소스 부트로더 |
| DTB | Devicetree Blob | 하드웨어 기술 데이터, 커널/Zephyr가 사용 |
| EABI | Embedded ABI | 호출 규약·데이터 레이아웃 규약 (AAPCS 포함) |
| AAPCS | Procedure Call Standard for the Arm Architecture | R0-R3 인자, R4-R11 callee-saved 등 |
| map 파일 | 링커 출력 배치 리포트 | 심볼별 주소·크기·출처 |
| CMSIS | Arm 공통 MCU SW 인터페이스 | Core, DSP, NN, RTOS2 등 |

---

## 15. 요약 & 체크리스트

Cortex-M은 벡터 테이블 첫 워드가 스택 포인터이고, 예외 진입 시 하드웨어가 caller-saved 레지스터 8워드를 자동 저장하기 때문에 startup과 ISR를 모두 C로 쓸 수 있다. NVIC는 숫자가 작을수록 높은 우선순위이며 구현 비트 수가 칩마다 다르고, BASEPRI로 부분 마스킹이 가능하다(M0+ 제외). 부트는 벡터 fetch → Reset_Handler → `.data` 복사, `.bss` 0 채우기 → 생성자 → `main` 순이며, 링커 스크립트의 VMA/LMA와 KEEP이 핵심이다. MPU는 변환 없는 보호, 캐시가 있는 M7급은 DMA coherency를 챙겨야 한다. Cortex-A는 EL, MMU, GIC, 다단계 부트 체인(ROM → TF-A → U-Boot → Linux)으로 완전히 다른 세계다. 툴체인은 `-mcpu/-mfloat-abi`를 일관되게, `-ffunction-sections`+`--gc-sections`로 크기를 줄이고, map·size·objdump·addr2line으로 이미지를 분석한다.

- [ ] Cortex-M 레지스터 세트(R0-R15, MSP/PSP, xPSR, PRIMASK/BASEPRI/FAULTMASK/CONTROL)를 화이트보드에 그릴 수 있다
- [ ] 예외 진입 시 스택 프레임 8워드의 순서와 EXC_RETURN 비트 의미를 설명할 수 있다
- [ ] NVIC 우선순위 숫자·구현 비트·PRIGROUP·BASEPRI 관계를 FreeRTOS 설정과 연결해 말할 수 있다
- [ ] Cortex-M 메모리 맵(Code/SRAM/Peripheral/PPB)과 주요 SCS 주소를 그릴 수 있다
- [ ] startup 코드(벡터 테이블 + Reset_Handler)를 보지 않고 C로 쓸 수 있다
- [ ] 링커 스크립트의 MEMORY, SECTIONS, `> RAM AT > FLASH`, KEEP, NOLOAD를 설명할 수 있다
- [ ] ARMv7-M vs ARMv8-M MPU 차이와 M7 DMA 캐시 문제 해결법을 말할 수 있다
- [ ] Cortex-A 부트 체인(ROM → BL2 → BL31 → U-Boot → Linux)과 EL/GIC를 Cortex-M과 비교할 수 있다
- [ ] `size` 출력의 text/data/bss가 flash/RAM에 각각 어떻게 대응하는지 계산할 수 있다
- [ ] crash PC를 `addr2line`으로 소스 줄에 매핑하는 흐름을 시연할 수 있다

---

## 참고 자료

- [ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest/)
- [ARMv8-M Architecture Reference Manual (DDI 0553)](https://developer.arm.com/documentation/ddi0553/latest/)
- [ARMv6-M Architecture Reference Manual (DDI 0419)](https://developer.arm.com/documentation/ddi0419/latest/)
- [Cortex-M4 Devices Generic User Guide (DUI 0553)](https://developer.arm.com/documentation/dui0553/latest/)
- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest/)
- [Arm GIC Architecture Specification (IHI 0069)](https://developer.arm.com/documentation/ihi0069/latest/)
- [Procedure Call Standard for the Arm Architecture (AAPCS32)](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [CMSIS 6 (GitHub)](https://github.com/ARM-software/CMSIS_6)
- [CMSIS documentation](https://arm-software.github.io/CMSIS_6/latest/General/index.html)
- [Arm GNU Toolchain downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
- [GNU ld manual — Linker Scripts](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [GCC ARM options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)
- [Trusted Firmware-A documentation](https://trustedfirmware-a.readthedocs.io/)
- [U-Boot documentation](https://docs.u-boot.org/)
- [QEMU Arm system emulator (Stellaris boards)](https://www.qemu.org/docs/master/system/arm/stellaris.html)
- [Zephyr: Optimizing for footprint (rom_report/ram_report)](https://docs.zephyrproject.org/latest/develop/optimizations/footprint.html)
- [Joseph Yiu, The Definitive Guide to Arm Cortex-M3 and Cortex-M4 Processors (책)](https://www.sciencedirect.com/book/9780124080829/the-definitive-guide-to-arm-cortex-m3-and-cortex-m4-processors)
