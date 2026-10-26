# B1. Cortex-M 프로그래머 모델 — 레지스터 16개 + 특수 레지스터, 두 모드 · 두 특권 · 두 스택

> **이 노트를 다 읽으면**: R0–R15 · xPSR(APSR/IPSR/EPSR) · CONTROL의 각 비트가 무엇을 뜻하는지 말하고 코드로 직접 읽을 수 있다 · Thread/Handler 모드와 특권/비특권의 2×2 조합, 그리고 "비특권 → 특권은 예외(SVC)로만"이라는 규칙을 QEMU에서 fault까지 내 보며 설명할 수 있다 · RTOS가 태스크는 PSP, ISR은 MSP에 두는 이유를 **직접 잰 스택 바이트 수**로 설명할 수 있다 · FreeRTOS · Zephyr가 CONTROL과 PSP를 만지는 정확한 함수 이름을 대고 C2 · C3로 넘어갈 수 있다
> **왜 필요한가**: "Cortex-M의 레지스터와 모드를 설명해 보라", "왜 스택 포인터가 두 개인가", "HardFault 덤프에서 어느 스택이었는지 어떻게 아나"는 펌웨어 면접의 1단계 질문이고, PendSV 컨텍스트 스위치 코드(C2)를 한 줄씩 읽으려면 이 노트의 모든 용어가 전제다
> **Don 기준 난이도**: 레지스터 · 스택 · 인터럽트 · AAPCS는 Cortex-R SSD 펌웨어에서 이미 매일 쓴 것 / 새로 배울 부분은 Cortex-M 고유의 "모드 2개 + 스택 2개 + CONTROL 한 장" 구조, 비특권 실행과 SVC로 특권 되찾기, 그리고 RTOS가 이것을 어떻게 쓰는지
> **선행 노트**: A4(태스크 = 함수 + 자기 스택 + TCB), EL B1(AAPCS · EABI 기본), EL C5(Cortex-A의 Exception Level과 비교용). 이어지는 노트: B2(예외 진입 · 8워드 스태킹 · EXC_RETURN), B3(NVIC · PRIMASK · BASEPRI), B4(SysTick · PendSV · SVC), C2(컨텍스트 스위치 코드), C3(첫 태스크 시작)
> 🔁 **Bare-Metal 겹침**: BM C1(프로그래머 모델) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 MSP 하나로 사는 펌웨어에서 CONTROL을 언제 건드리나(부트로더 → 앱 점프, 비특권 테스트 하네스)"만 더한다

---

## 0. 큰 그림 — RTOS가 기대는 CPU의 "작업대"

Don이 SSD 컨트롤러의 Cortex-R 펌웨어에서 매일 본 것은 이런 것들이다: 레지스터 덤프의 R0~R15, CPSR, 그리고 IRQ 모드 · SVC 모드마다 따로 있는 스택. Cortex-M도 뼈대는 같다. 다른 점은 **모드가 딱 두 개(Thread, Handler)이고, 스택 포인터도 딱 두 개(MSP, PSP)이며, 그 선택을 CONTROL 레지스터 한 장이 정한다**는 것이다. 그리고 예외가 들어오면 하드웨어가 레지스터 8개를 스스로 저장해 준다(B2).

RTOS는 이 구조 위에 지어진다. 한 문장으로 요약하면 이렇다.

- **태스크** = Thread 모드에서 **PSP**를 쓰는 코드. 태스크마다 PSP 값이 다르다(= 태스크마다 자기 스택).
- **ISR과 커널의 예외 핸들러**(SysTick, PendSV, SVC) = Handler 모드, 항상 **MSP**. 시스템 전체에 하나뿐인 인터럽트 스택이다.
- **컨텍스트 스위치** = PendSV 핸들러가 "지금 PSP 값"을 TCB에 저장하고 "다음 태스크의 PSP 값"을 꺼내 넣는 일(C2).
- **보호** = 태스크를 비특권으로 돌리면 NVIC · SysTick · CONTROL을 못 건드린다. 특권이 필요하면 SVC로 커널에 부탁한다(C8, F4).

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20.0" y="20.0" font-size="13" font-weight="bold">범용 레지스터 (AAPCS 역할)</text>
<text x="370.0" y="20.0" font-size="13" font-weight="bold">특수 레지스터 (MRS / MSR로만 접근)</text>
<rect x="20.0" y="34.0" width="56.0" height="26.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="48.0" y="51.5" font-size="13" text-anchor="middle">R0</text>
<rect x="82.0" y="34.0" width="56.0" height="26.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110.0" y="51.5" font-size="13" text-anchor="middle">R1</text>
<rect x="144.0" y="34.0" width="56.0" height="26.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="172.0" y="51.5" font-size="13" text-anchor="middle">R2</text>
<rect x="206.0" y="34.0" width="56.0" height="26.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="234.0" y="51.5" font-size="13" text-anchor="middle">R3</text>
<text x="272.0" y="52.0" font-size="12">인자 / 리턴</text>
<rect x="20.0" y="66.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="48.0" y="83.5" font-size="13" text-anchor="middle">R4</text>
<rect x="82.0" y="66.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="110.0" y="83.5" font-size="13" text-anchor="middle">R5</text>
<rect x="144.0" y="66.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="172.0" y="83.5" font-size="13" text-anchor="middle">R6</text>
<rect x="206.0" y="66.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="234.0" y="83.5" font-size="13" text-anchor="middle">R7</text>
<text x="272.0" y="84.0" font-size="12">보존 필수</text>
<rect x="20.0" y="98.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="48.0" y="115.5" font-size="13" text-anchor="middle">R8</text>
<rect x="82.0" y="98.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="110.0" y="115.5" font-size="13" text-anchor="middle">R9</text>
<rect x="144.0" y="98.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="172.0" y="115.5" font-size="13" text-anchor="middle">R10</text>
<rect x="206.0" y="98.0" width="56.0" height="26.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="234.0" y="115.5" font-size="13" text-anchor="middle">R11</text>
<text x="272.0" y="116.0" font-size="12">보존 필수</text>
<rect x="20.0" y="130.0" width="56.0" height="26.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="48.0" y="147.6" font-size="13" text-anchor="middle">R12</text>
<text x="272.0" y="148.0" font-size="12">스크래치</text>
<rect x="20.0" y="168.0" width="70.0" height="56.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="55.0" y="191.6" font-size="13" text-anchor="middle" font-weight="bold">R13</text>
<text x="55.0" y="209.6" font-size="12" text-anchor="middle">SP</text>
<rect x="110.0" y="168.0" width="150.0" height="26.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="185.0" y="185.6" font-size="13" text-anchor="middle">MSP (Main)</text>
<rect x="110.0" y="198.0" width="150.0" height="26.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="185.0" y="215.6" font-size="13" text-anchor="middle">PSP (Process)</text>
<line x1="90.0" y1="188.0" x2="108.0" y2="181.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="90.0" y1="204.0" x2="108.0" y2="211.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="268.0" y="186.0" font-size="12">둘 중 하나가</text>
<text x="268.0" y="216.0" font-size="12">지금의 SP</text>
<rect x="20.0" y="236.0" width="70.0" height="26.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="55.0" y="253.6" font-size="13" text-anchor="middle">R14 LR</text>
<text x="100.0" y="254.0" font-size="12">복귀 주소 · 예외 안에서는 EXC_RETURN</text>
<rect x="20.0" y="270.0" width="70.0" height="26.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="55.0" y="287.6" font-size="13" text-anchor="middle">R15 PC</text>
<text x="100.0" y="288.0" font-size="12">다음 명령 주소 (bit0 = Thumb 표시)</text>
<rect x="20.0" y="312.0" width="14.0" height="14.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="40.0" y="324.0" font-size="12">caller-saved: 예외 때 하드웨어가 저장</text>
<rect x="330.0" y="312.0" width="14.0" height="14.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="350.0" y="324.0" font-size="12">callee-saved: 컨텍스트 스위치 때 소프트웨어가 저장</text>
<rect x="370.0" y="34.0" width="300.0" height="92.0" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="380.0" y="52.0" font-size="12" font-weight="bold">xPSR — 세 레지스터를 한 워드로 겹쳐 읽음</text>
<rect x="378.0" y="62.0" width="92.0" height="54.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="424.0" y="84.5" font-size="13" text-anchor="middle" font-weight="bold">APSR</text>
<text x="424.0" y="102.5" font-size="12" text-anchor="middle">N Z C V Q</text>
<rect x="476.0" y="62.0" width="92.0" height="54.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="522.0" y="84.5" font-size="13" text-anchor="middle" font-weight="bold">IPSR</text>
<text x="522.0" y="102.5" font-size="12" text-anchor="middle">예외 번호</text>
<rect x="574.0" y="62.0" width="90.0" height="54.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="619.0" y="84.5" font-size="13" text-anchor="middle" font-weight="bold">EPSR</text>
<text x="619.0" y="102.5" font-size="12" text-anchor="middle">T 비트 · IT</text>
<rect x="370.0" y="138.0" width="300.0" height="60.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="520.0" y="163.6" font-size="13" text-anchor="middle" font-weight="bold">PRIMASK · FAULTMASK · BASEPRI</text>
<text x="520.0" y="181.6" font-size="12" text-anchor="middle">인터럽트 마스킹 (자세히는 B3)</text>
<rect x="370.0" y="210.0" width="300.0" height="96.0" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="380.0" y="228.0" font-size="12" font-weight="bold">CONTROL — Thread 모드의 성격을 정함</text>
<rect x="378.0" y="238.0" width="92.0" height="58.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="424.0" y="262.6" font-size="13" text-anchor="middle" font-weight="bold">nPRIV</text>
<text x="424.0" y="280.6" font-size="12" text-anchor="middle">bit0 특권?</text>
<rect x="476.0" y="238.0" width="92.0" height="58.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="522.0" y="262.6" font-size="13" text-anchor="middle" font-weight="bold">SPSEL</text>
<text x="522.0" y="280.6" font-size="12" text-anchor="middle">bit1 MSP/PSP</text>
<rect x="574.0" y="238.0" width="90.0" height="58.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="619.0" y="262.6" font-size="13" text-anchor="middle" font-weight="bold">FPCA</text>
<text x="619.0" y="280.6" font-size="12" text-anchor="middle">bit2 FP 문맥</text>
</svg>
```

그림 1 — Cortex-M 프로그래머 모델 전체. 왼쪽은 R0–R15(색은 AAPCS에서의 역할), 오른쪽은 MRS/MSR 명령으로만 접근하는 특수 레지스터. SP는 실제로 MSP와 PSP 두 개이고, 지금 어느 쪽이 "SP"인지는 모드와 CONTROL.SPSEL이 정한다.

이 노트는 그림 1의 모든 칸을 **QEMU에서 코드로 직접 읽어 보고, 일부러 규칙을 어겨 fault를 내 보면서** 익힌다. 마지막에는 FreeRTOS와 Zephyr가 이 칸들을 정확히 어디서 만지는지 소스 위치로 연결한다.

### 0.1 Don의 Cortex-R 경험과 나란히 놓기

| 항목 | Cortex-R5 (Armv7-R, Don의 SSD 컨트롤러) | Cortex-M3/M4 (Armv7-M, 이 노트) |
|---|---|---|
| 실행 모드 | USR · SYS · SVC · IRQ · FIQ · ABT · UND (7개) | Thread · Handler (2개) |
| 스택 포인터 | 모드마다 banked SP (FIQ는 R8–R12도 banked) | MSP · PSP 두 개만 banked |
| 상태 레지스터 | CPSR (모드 비트 M[4:0], T 비트는 bit5) | xPSR = APSR + IPSR + EPSR (T 비트는 bit24) |
| 예외 진입 저장 | 하드웨어는 LR · SPSR만, 나머지는 핸들러가 `push` | 하드웨어가 8워드(R0–R3, R12, LR, PC, xPSR)를 자동 저장 |
| 인터럽트 컨트롤러 | 외부 VIC/GIC | NVIC가 코어에 붙어 있음 (B3) |
| ISR 작성 | 어셈블리 진입부(irq 래퍼)가 필요 | 평범한 C 함수 그대로 (예제 2에서 확인) |
| 특권 구분 | USR만 비특권 | Thread 모드에서만 CONTROL.nPRIV로 선택 |

Cortex-R의 "IRQ 모드 스택 + SVC/SYS 모드 스택"이 Cortex-M에서는 "MSP + PSP"로 줄어든 것이라고 보면 머리에 잘 들어온다. Cortex-R 계열 자체는 B9에서 다룬다.

### 실습 환경 한 줄

- QEMU `mps2-an385` = **Cortex-M3 @ 25 MHz** 가상 보드, lab의 `templates/baremetal-mps2`(RTOS 없음)와 `templates/freertos-mps2`(FreeRTOS V11.3.2, ARM_CM3 포트), Zephyr v4.4.2 `mps2/an385`. 다른 코어 비교는 QEMU `mps2-an386`(Cortex-M4F). gdb는 VM의 Ubuntu gdb 17.1, QEMU 10.2.1, arm-none-eabi-gcc 14.2.1.
- 이 노트는 레지스터 **값**과 스택 **바이트 수**를 본다. 시간은 재지 않으므로 ICOUNT 여부는 결과에 영향이 없다(그래도 모두 `ICOUNT=1`로 돌렸다). 스택 바이트 수는 명령 흐름이 결정하므로 실제 칩과 같다. 단 QEMU는 예외 스태킹 **사이클**을 모델링하지 않는다.
- 모든 bare-metal 예제는 아래 스크립트로 빌드하고 실행했다. `-B`는 매번 새로 빌드하라는 뜻이고, 두 번째 인자부터는 `EXTRA_CFLAGS`(예: `-DCASE=2`)로 넘어간다.

```sh
#!/bin/bash
# ~/.rtos_scratch/B1/run.sh — usage: run.sh exN [EXTRA_CFLAGS]  — bare-metal build + ICOUNT run
N=$1; shift
mkdir -p /var/tmp/rtos/B1
make -s -B -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/B1/$N.c OUT=/var/tmp/rtos/B1/$N EXTRA_CFLAGS="$*" && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B1/$N/app.elf 10
echo "exit=$?"
```

```sh
# Mac에서: VM 안에서 실행
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/B1/run.sh ex1'
```

모든 예제가 같이 쓰는 작은 헤더 `regs.h`다. `MRS(reg)`는 특수 레지스터 하나를 읽는 매크로, `run_task()`는 "PSP를 새 스택으로 바꾸고, CONTROL을 원하는 값으로 바꾼 뒤 함수를 부르는" 어셈블리 트램펄린(trampoline, 점프만 해 주는 짧은 코드)이다. RTOS 없이 "태스크 하나"를 흉내 내는 최소 장치라고 보면 된다. `run_task`의 각 줄은 §4와 §5에서 하나씩 설명한다.

```c
/* B1 공용 헤더: 특수 레지스터 읽기/쓰기 + "PSP 위에서 함수 실행" 트램펄린 */
#ifndef B1_REGS_H
#define B1_REGS_H
#include <stdint.h>
#define MRS(reg)    ({ uint32_t v_; __asm volatile("mrs %0, " #reg : "=r"(v_)); v_; })
#define MSR(reg, v) __asm volatile("msr " #reg ", %0" :: "r"((uint32_t)(v)) : "memory")
static inline uint32_t get_sp(void) { uint32_t v; __asm volatile("mov %0, sp" : "=r"(v)); return v; }

/* run_task(fn, top, ctrl): PSP=top, CONTROL=ctrl(2=특권+PSP, 3=비특권+PSP)로 fn() 실행.
 * fn이 돌아오면 svc #255로 특권을 되찾고(비특권은 CONTROL을 못 고치므로) MSP로 복귀한다. */
void run_task(void (*fn)(void), uint32_t *top, uint32_t ctrl);
__asm__(
    "  .global run_task         \n  .type run_task, %function\n  .thumb_func\n"
    "run_task:                  \n"
    "  push {r4, lr}            \n"   /* MSP에 저장 */
    "  mrs  r4, control         \n"
    "  msr  psp, r1             \n"
    "  msr  control, r2         \n"
    "  isb                      \n"   /* 이후 명령은 새 SP · 새 특권으로 */
    "  blx  r0                  \n"
    "  svc  #255                \n"   /* 특권 회복 요청 */
    "  msr  control, r4         \n"   /* 원래 CONTROL(MSP) */
    "  isb                      \n"
    "  pop  {r4, pc}            \n");

#ifdef B1_WANT_SVC  /* run_task를 쓰는 예제는 B1_WANT_SVC를 정의하고 svc_c()를 만든다 */
/* SVC 진입: 어느 스택에 프레임이 있나(EXC_RETURN bit2) → 쌓인 PC 앞 2바이트에서 SVC 번호 */
void svc_c(uint32_t *frame, uint32_t num);   /* 예제마다 정의 */
__asm__(
    "  .global SVC_Handler      \n  .type SVC_Handler, %function\n  .thumb_func\n"
    "SVC_Handler:               \n"
    "  tst   lr, #4             \n"
    "  ite   eq                 \n"
    "  mrseq r0, msp            \n"
    "  mrsne r0, psp            \n"
    "  ldr   r1, [r0, #24]      \n"   /* 쌓인 PC = svc 다음 명령 */
    "  ldrb  r1, [r1, #-2]      \n"   /* svc #imm8의 imm8 */
    "  b     svc_c              \n");
#endif
static inline void svc_back_to_priv(void) { MSR(control, MRS(control) & ~1u); __asm volatile("isb"); }
#endif
```

lab 템플릿이 이미 준비해 둔 것 두 가지도 알아 두자. `startup.c`의 `HardFault_Handler`는 `tst lr, #4`로 어느 스택(MSP/PSP)에 예외 프레임이 쌓였는지 보고 `board_hardfault()`로 넘긴다. 그 함수는 쌓인 PC · LR · xPSR과 CFSR · HFSR을 찍고 QEMU를 끝낸다. 이 노트의 fault 예제는 모두 이 출력으로 결과를 확인한다.

---

## 1. 범용 레지스터 R0–R15

> 🔁 이 절은 Bare-Metal BM C1과 겹친다 (주인: 이 노트).

### 1.1 직관 — 16칸짜리 작업대

CPU가 계산하는 곳은 메모리가 아니라 레지스터다. Cortex-M에는 이름 붙은 32비트 레지스터가 16개 있다. 이 중 13개(R0–R12)는 "아무 용도로나 쓰는 칸"이고, 3개는 특별한 역할이 정해져 있다.

| 레지스터 | 별명 | 하는 일 | 함정 |
|---|---|---|---|
| R0–R3 | a1–a4 | 함수 인자 1~4번째, 반환값(R0, 64비트면 R0:R1) | 함수를 부르면 망가져도 된다 (caller-saved) |
| R4–R11 | v1–v8 | 지역 변수 보관 | 함수가 쓰려면 저장했다가 되돌려야 한다 (callee-saved) |
| R12 | IP | 링커 veneer · 임시값 | caller-saved |
| R13 | SP | 스택 포인터. 실제로는 **MSP와 PSP 두 개** 중 하나 | 하위 2비트는 항상 0 (4바이트 정렬) |
| R14 | LR | 함수 복귀 주소. 예외 핸들러 안에서는 **EXC_RETURN**이라는 특수 값 | `bx lr`이 함수 복귀도, 예외 복귀도 된다 |
| R15 | PC | 다음에 실행할 명령 주소 | 함수 포인터의 bit0은 "Thumb" 표시 (§2.4) |

Armv7-M의 Thumb-2 명령 대부분은 R0–R7(low register)에서 16비트 인코딩을 쓰고 R8–R12(high register)는 32비트 인코딩이 필요한 경우가 많다. 그래서 컴파일러는 R0–R7을 먼저 쓴다. 코드 크기에 민감한 MCU에서 체감되는 차이다(어셈블리 읽기는 BM C2).

### 1.2 예제 1 — reset 직후 main()에서 레지스터를 전부 읽기

무엇을 확인하나: reset 직후 CPU가 어떤 상태로 `main()`에 도착하는지 — 어느 스택을 쓰나, 특권인가, 인터럽트가 막혀 있나 — 를 특수 레지스터를 모두 읽어 확인한다. 그리고 APSR 플래그가 연산 결과에 따라 어떻게 바뀌는지 본다.

```c
/* ex1: reset 직후 main()에서 특수 레지스터를 전부 읽는다 */
#include "board.h"
#include "regs.h"

extern uint32_t _estack;

static void show_flags(const char *what, uint32_t apsr)
{
    board_printf_raw("  %s APSR=0x%x  N=%u Z=%u C=%u V=%u Q=%u\n", what, apsr,
                     apsr >> 31, (apsr >> 30) & 1, (apsr >> 29) & 1,
                     (apsr >> 28) & 1, (apsr >> 27) & 1);
}

int main(void)
{
    board_printf_raw("_estack(vector[0]) = 0x%x\n", (unsigned)&_estack);
    board_printf_raw("SP=0x%x MSP=0x%x PSP=0x%x\n", get_sp(), MRS(msp), MRS(psp));
    board_printf_raw("CONTROL=0x%x PRIMASK=%u BASEPRI=0x%x FAULTMASK=%u\n",
                     MRS(control), MRS(primask), MRS(basepri), MRS(faultmask));
    board_printf_raw("xPSR=0x%x IPSR=%u EPSR=0x%x (MRS로 읽으면 EPSR은 0)\n",
                     MRS(xpsr), MRS(ipsr), MRS(epsr));

    /* APSR 플래그를 일부러 만든다 */
    uint32_t a;
    __asm volatile("movs r0, #1\n cmp r0, #2\n mrs %0, apsr" : "=r"(a) :: "r0", "cc");
    show_flags("1-2 :", a);                       /* 음수, borrow → C=0 */
    __asm volatile("movs r0, #5\n cmp r0, #5\n mrs %0, apsr" : "=r"(a) :: "r0", "cc");
    show_flags("5-5 :", a);                       /* 0 → Z=1, C=1 (borrow 없음) */
    __asm volatile("ldr r0, =0x7fffffff\n adds r0, r0, #1\n mrs %0, apsr" : "=r"(a) :: "r0", "cc");
    show_flags("MAX+1:", a);                      /* 부호 넘침 → V=1, N=1 */
    __asm volatile("movw r0, #1000\n ssat r0, #8, r0\n mrs %0, apsr" : "=r"(a) :: "r0", "cc");
    show_flags("SSAT:", a);                       /* 1000을 8비트로 포화 → Q=1 (sticky) */
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4544	     96	    328	   4968	   1368	/var/tmp/rtos/B1/ex1/app.elf
_estack(vector[0]) = 0x20400000
SP=0x203fffe8 MSP=0x203fffe8 PSP=0x0
CONTROL=0x0 PRIMASK=0 BASEPRI=0x0 FAULTMASK=0
xPSR=0x60000000 IPSR=0 EPSR=0x0 (MRS로 읽으면 EPSR은 0)
  1-2 : APSR=0x80000000  N=1 Z=0 C=0 V=0 Q=0
  5-5 : APSR=0x60000000  N=0 Z=1 C=1 V=0 Q=0
  MAX+1: APSR=0x90000000  N=1 Z=0 C=0 V=1 Q=0
  SSAT: APSR=0x68000000  N=0 Z=1 C=1 V=0 Q=1
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- **SP = MSP = 0x203fffe8**: reset 때 하드웨어가 벡터 테이블 0번 워드(`_estack` = 0x20400000)를 MSP에 넣고 시작했다. `main()`까지 오면서 Reset_Handler와 main의 프레임 24바이트만큼 내려왔다. 지금 SP는 MSP다.
- **PSP = 0**: 아무도 PSP를 설정하지 않았다. 아키텍처는 reset 때 PSP 값을 "UNKNOWN"으로 정의한다. QEMU는 0을 주지만 실제 칩에서는 쓰레기 값일 수 있다. 쓰기 전에 반드시 초기화한다.
- **CONTROL = 0**: 특권(nPRIV=0), MSP 사용(SPSEL=0). reset 직후 CPU는 **Thread 모드 + 특권 + MSP**다. bare-metal 펌웨어는 대부분 평생 이 상태로 산다.
- **PRIMASK · BASEPRI · FAULTMASK = 0**: 아무 인터럽트도 막혀 있지 않다(B3).
- **EPSR = 0**: 이상하다. CPU는 지금 분명 Thumb 상태(T=1)로 돌고 있는데 0이 나온다. 아키텍처 규칙상 **MRS로 읽으면 EPSR 비트는 항상 0으로 읽힌다**. T 비트를 진짜로 보려면 디버거(§6) 또는 예외 프레임에 쌓인 xPSR(§2.4)을 봐야 한다.
- **플래그**: `1-2`는 음수라 N=1, 빌림(borrow)이 생겨 C=0(Arm에서 뺄셈의 C는 "빌림 없음"이 1). `5-5`는 Z=1, C=1. `0x7fffffff+1`은 부호 있는 넘침이라 V=1, N=1. `SSAT`(signed saturate)는 1000을 8비트 범위(−128~127)로 자르면서 **Q=1**(포화가 일어났다는 끈끈한(sticky) 표시)을 세웠다. SSAT는 NZCV를 바꾸지 않으므로 Z=1 C=1은 그 사이 `board_printf_raw` 코드가 남긴 값이다.

> M3에는 DSP 확장이 없어서 `QADD` 같은 포화 산술은 어셈블러가 거부한다(직접 해 보면 "selected processor does not support `qadd'"). `SSAT/USAT`는 Armv7-M 기본 명령이라 M3에도 있다. GE 플래그(bit19:16)와 QADD는 M4 이상의 DSP 확장 몫이다(BM C10).

### 1.3 LR과 PC — 복귀 주소와 Thumb 표시

함수를 `BL`로 부르면 LR에 "돌아올 주소 + 1"이 들어간다. +1은 실수로 넣은 게 아니라 **bit0 = 1 = Thumb 상태로 돌아가라**는 표시다. Cortex-M은 Thumb 명령만 실행하므로 이 비트는 항상 1이어야 한다. 이 규칙이 어기면 어떻게 되는지는 §2.4에서 실험한다.

LR은 예외 핸들러 안에서 완전히 다른 의미를 가진다. 예외에 들어가면 하드웨어가 LR에 `0xFFFFFFF9`, `0xFFFFFFFD` 같은 **EXC_RETURN** 값을 넣는다. 핸들러가 이 값으로 `bx lr`(또는 `pop {..., pc}`)하면 하드웨어는 "아, 함수 복귀가 아니라 예외 복귀구나"를 알아채고 쌓아 둔 8워드를 꺼낸다. 어느 값이 무엇을 뜻하는지는 예제 3 · 8에서 실제로 보고, 자세한 규칙은 B2가 다룬다.

### 1.4 AAPCS — 누가 어느 레지스터를 지키나 (딱 필요한 만큼)

Don은 Cortex-R에서 "R0–R3 인자, R0 반환, R4–R11은 callee-saved"를 이미 지켜 왔다(EL B1에서도 정리). 여기서는 **Cortex-M 예외와 RTOS 컨텍스트 스위치를 이해하는 데 필요한 부분만** 본다. AAPCS 전체(구조체 반환, 가변 인자, 8바이트 스택 정렬의 세부)는 Bare-Metal BM C3이 주인이다.

- **caller-saved (R0–R3, R12, 그리고 LR)**: 호출당한 함수가 마음대로 망가뜨려도 된다. 그래서 호출하는 쪽이 필요하면 미리 저장한다.
- **callee-saved (R4–R11)**: 호출당한 함수가 쓰려면 프롤로그에서 `push`하고 에필로그에서 `pop`해야 한다.

이 구분이 Cortex-M의 예외 설계를 결정했다. 인터럽트는 "아무 데서나 끼어드는 함수 호출"이다. 그렇다면 끼어든 쪽(ISR)이 망가뜨릴 수 있는 레지스터 — 즉 caller-saved 집합 R0–R3, R12, LR — 에 복귀용 PC와 xPSR을 더한 **8워드만 하드웨어가 저장하면**, ISR은 평범한 C 함수로 써도 된다. R4–R11은 ISR이 C 함수인 이상 컴파일러가 알아서 보존해 준다.

### 1.5 예제 2 — 컴파일러가 실제로 무엇을 저장하나 (objdump)

무엇을 확인하나: AAPCS 규칙이 생성 코드에서 어떻게 보이는지, 그리고 ISR이 아무 attribute 없는 C 함수로 컴파일되어도 되는 이유를 디스어셈블리로 본다. 링크하지 않고 컴파일만 했다.

```c
/* ex8: AAPCS — 누가 어떤 레지스터를 보존하나 (컴파일만 하고 objdump로 본다) */
#include <stdint.h>
int ext(int v);                       /* 다른 파일에 있는 함수: 컴파일러는 속을 모른다 */
volatile int counter;

int leaf(int a, int b, int c, int d, int e)   /* 인자 1~4: R0-R3, 5번째: 스택 */
{
    return a + b + c + d + e;         /* 결과: R0 */
}

int keep(int x)                       /* x를 ext() 호출 너머까지 살려야 한다 */
{
    int y = ext(x);                   /* ext는 R0-R3, R12를 마음대로 망가뜨려도 된다 */
    return x + y;                     /* → x는 callee-saved R4에 둔다 */
}

void TIMER0_IRQHandler(void)          /* ISR이 그냥 C 함수: 특별한 attribute 없음 */
{
    counter = counter + ext(counter);
}

int64_t wide(int64_t a, int64_t b) { return a * b; }   /* 64비트: R0:R1, R2:R3 쌍 */
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/rtos/B1/ex8 && cd /var/tmp/rtos/B1/ex8 && arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -Wall -Wextra -c /Users/donh/.rtos_scratch/B1/ex8.c -o ex8.o && arm-none-eabi-objdump -d --no-show-raw-insn ex8.o | sed -n "/<leaf>/,\$p"'
```

```text
00000000 <leaf>:
   0:	add	r0, r1
   2:	add	r0, r2
   4:	add	r0, r3
   6:	ldr	r3, [sp, #0]
   8:	add	r0, r3
   a:	bx	lr

0000000c <keep>:
   c:	push	{r4, lr}
   e:	mov	r4, r0
  10:	bl	0 <ext>
  14:	add	r0, r4
  16:	pop	{r4, pc}

00000018 <TIMER0_IRQHandler>:
  18:	push	{r4, lr}
  1a:	ldr	r4, [pc, #16]	@ (2c <TIMER0_IRQHandler+0x14>)
  1c:	ldr	r0, [r4, #0]
  1e:	bl	0 <ext>
  22:	ldr	r3, [r4, #0]
  24:	add	r0, r3
  26:	str	r0, [r4, #0]
  28:	pop	{r4, pc}
  2a:	nop
  2c:	.word	0x00000000

00000030 <wide>:
  30:	mul.w	r3, r0, r3
  34:	umull	r0, ip, r0, r2
  38:	mla	r1, r2, r1, r3
  3c:	add	r1, ip
  3e:	bx	lr
```

출력에서 볼 것:

- `leaf`: 인자 4개는 R0–R3로 왔고 5번째(`e`)는 스택(`[sp, #0]`)에서 읽었다. R0–R3만 썼으므로 아무것도 저장하지 않는다. R3를 덮어쓴 것도 문제없다(caller-saved).
- `keep`: `x`를 `ext()` 호출 너머까지 살려야 하므로 callee-saved **R4**로 옮기고, R4를 `push`/`pop`으로 보존했다. LR도 같이 저장한 이유는 `bl`이 LR을 덮어쓰기 때문이다.
- `TIMER0_IRQHandler`: `keep`과 **똑같은 모양**이다. `__attribute__((interrupt))`도 없고 특별한 진입 코드도 없다. 끝의 `pop {r4, pc}`는 LR 자리에 있던 값을 PC로 꺼내는데, 그 값이 EXC_RETURN(예: 0xFFFFFFF9)이므로 하드웨어가 예외 복귀를 수행한다. R0–R3, R12는 하드웨어가 이미 저장해 두었으니 ISR이 망가뜨려도 된다.
- `wide`: 64비트 인자는 R0:R1, R2:R3 쌍으로, 결과는 R0:R1로 간다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20.0" y="20.0" font-size="13" font-weight="bold">예외 · 컨텍스트 스위치에서 누가 무엇을 저장하나</text>
<rect x="20.0" y="34.0" width="300.0" height="170.0" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="30.0" y="54.0" font-size="12" font-weight="bold">하드웨어 자동 스태킹 8워드 (B2)</text>
<rect x="30.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="62.0" y="85.5" font-size="13" text-anchor="middle">R0</text>
<rect x="102.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="134.0" y="85.5" font-size="13" text-anchor="middle">R1</text>
<rect x="174.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="206.0" y="85.5" font-size="13" text-anchor="middle">R2</text>
<rect x="246.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="278.0" y="85.5" font-size="13" text-anchor="middle">R3</text>
<rect x="30.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="62.0" y="125.5" font-size="13" text-anchor="middle">R12</text>
<rect x="102.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="134.0" y="125.5" font-size="13" text-anchor="middle">LR</text>
<rect x="174.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="206.0" y="125.5" font-size="13" text-anchor="middle">PC</text>
<rect x="246.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="278.0" y="125.5" font-size="13" text-anchor="middle">xPSR</text>
<text x="30.0" y="168.0" font-size="12">caller-saved(R0–R3, R12) + 복귀 정보</text>
<text x="30.0" y="188.0" font-size="12">→ ISR을 평범한 C 함수로 쓸 수 있다</text>
<rect x="360.0" y="34.0" width="300.0" height="170.0" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
<text x="370.0" y="54.0" font-size="12" font-weight="bold">소프트웨어(PendSV)가 저장 8워드 (C2)</text>
<rect x="370.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="402.0" y="85.5" font-size="13" text-anchor="middle">R4</text>
<rect x="442.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="474.0" y="85.5" font-size="13" text-anchor="middle">R5</text>
<rect x="514.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="546.0" y="85.5" font-size="13" text-anchor="middle">R6</text>
<rect x="586.0" y="66.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="618.0" y="85.5" font-size="13" text-anchor="middle">R7</text>
<rect x="370.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="402.0" y="125.5" font-size="13" text-anchor="middle">R8</text>
<rect x="442.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="474.0" y="125.5" font-size="13" text-anchor="middle">R9</text>
<rect x="514.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="546.0" y="125.5" font-size="13" text-anchor="middle">R10</text>
<rect x="586.0" y="106.0" width="64.0" height="30.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="618.0" y="125.5" font-size="13" text-anchor="middle">R11</text>
<text x="370.0" y="168.0" font-size="12">callee-saved — C 함수는 보존해 주지만</text>
<text x="370.0" y="188.0" font-size="12">태스크를 바꿀 때는 커널이 직접 저장</text>
<text x="20.0" y="232.0" font-size="12">합계 16워드 = 64 B가 태스크 하나의 최소 문맥 (FPU 없을 때)</text>
</svg>
```

그림 2 — 예외와 컨텍스트 스위치에서 저장 책임이 나뉜다. 하드웨어는 caller-saved 집합과 복귀 정보 8워드만 저장한다(B2). 태스크를 바꿀 때는 나머지 callee-saved 8워드를 RTOS의 PendSV 핸들러가 직접 `stmdb`/`ldmia`로 저장 · 복원한다(C2). 그래서 FreeRTOS 태스크의 최소 문맥은 16워드다(A4의 위조 초기 프레임 16워드와 같은 숫자).

---

## 2. xPSR — 세 레지스터를 한 워드로 겹친 상태 레지스터

### 2.1 직관

Cortex-R의 CPSR 하나에 들어 있던 정보가 Cortex-M에서는 세 조각으로 나뉘어 있다. 셋은 서로 비트가 겹치지 않아서 `MRS r0, xpsr`로 한 번에 읽으면 OR한 값이 나온다.

- **APSR** (Application PSR): 연산 결과 플래그 N · Z · C · V · Q (+ M4 이상은 GE). 비특권 코드도 읽고 쓸 수 있다.
- **IPSR** (Interrupt PSR): **지금 처리 중인 예외 번호**. 0이면 Thread 모드, 0이 아니면 Handler 모드다. 읽기 전용.
- **EPSR** (Execution PSR): 실행 상태 — T(Thumb) 비트와 IT 블록 · 중단된 LDM/STM 재개 정보(ICI). MRS로 읽으면 0, MSR로 쓸 수 없다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20.0" y="20.0" font-size="13" font-weight="bold">xPSR (32비트)</text>
<rect x="20.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30.0" y="52.0" font-size="12" text-anchor="middle">N</text>
<rect x="40.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="50.0" y="52.0" font-size="12" text-anchor="middle">Z</text>
<rect x="60.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="70.0" y="52.0" font-size="12" text-anchor="middle">C</text>
<rect x="80.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="90.0" y="52.0" font-size="12" text-anchor="middle">V</text>
<rect x="100.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110.0" y="52.0" font-size="12" text-anchor="middle">Q</text>
<rect x="120.0" y="30.0" width="40.0" height="34.0" rx="2" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="140.0" y="52.0" font-size="12" text-anchor="middle">IT</text>
<rect x="160.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170.0" y="52.0" font-size="12" text-anchor="middle">T</text>
<rect x="180.0" y="30.0" width="80.0" height="34.0" rx="2" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="220.0" y="52.0" font-size="12" text-anchor="middle">-</text>
<rect x="260.0" y="30.0" width="80.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="5,3"/>
<text x="300.0" y="52.0" font-size="12" text-anchor="middle">GE</text>
<rect x="340.0" y="30.0" width="120.0" height="34.0" rx="2" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="400.0" y="52.0" font-size="12" text-anchor="middle">IT / ICI</text>
<rect x="460.0" y="30.0" width="20.0" height="34.0" rx="2" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="470.0" y="52.0" font-size="12" text-anchor="middle">-</text>
<rect x="480.0" y="30.0" width="180.0" height="34.0" rx="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570.0" y="52.0" font-size="12" text-anchor="middle">예외 번호</text>
<text x="30.0" y="80.0" font-size="12" text-anchor="middle">31</text>
<text x="110.0" y="80.0" font-size="12" text-anchor="middle">27</text>
<text x="170.0" y="80.0" font-size="12" text-anchor="middle">24</text>
<text x="270.0" y="80.0" font-size="12" text-anchor="middle">19</text>
<text x="330.0" y="80.0" font-size="12" text-anchor="middle">16</text>
<text x="350.0" y="80.0" font-size="12" text-anchor="middle">15</text>
<text x="450.0" y="80.0" font-size="12" text-anchor="middle">10</text>
<text x="490.0" y="80.0" font-size="12" text-anchor="middle">8</text>
<text x="650.0" y="80.0" font-size="12" text-anchor="middle">0</text>
<rect x="20.0" y="92.0" width="14.0" height="14.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="40.0" y="104.0" font-size="12">APSR: 플래그 (GE는 M4 이상 DSP)</text>
<rect x="290.0" y="92.0" width="14.0" height="14.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="310.0" y="104.0" font-size="12">EPSR: 실행 상태</text>
<rect x="450.0" y="92.0" width="14.0" height="14.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470.0" y="104.0" font-size="12">IPSR: 0 = Thread</text>
<text x="20.0" y="134.0" font-size="12">T(bit24)는 항상 1이어야 한다 — 0이면 다음 명령에서 INVSTATE UsageFault</text>
<text x="20.0" y="168.0" font-size="13" font-weight="bold">CONTROL</text>
<rect x="20.0" y="178.0" width="280.0" height="34.0" rx="2" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="160.0" y="200.0" font-size="12" text-anchor="middle">31:3 예약 (v8-M에서 비트 추가)</text>
<rect x="300.0" y="178.0" width="120.0" height="34.0" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="360.0" y="200.0" font-size="12" text-anchor="middle">FPCA (2)</text>
<rect x="420.0" y="178.0" width="120.0" height="34.0" rx="2" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="480.0" y="200.0" font-size="12" text-anchor="middle">SPSEL (1)</text>
<rect x="540.0" y="178.0" width="120.0" height="34.0" rx="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="600.0" y="200.0" font-size="12" text-anchor="middle">nPRIV (0)</text>
<text x="360.0" y="232.0" font-size="12" text-anchor="middle">1 = FP 문맥 있음</text>
<text x="360.0" y="250.0" font-size="12" text-anchor="middle">(FPU 칩만)</text>
<text x="480.0" y="232.0" font-size="12" text-anchor="middle">1 = PSP 사용</text>
<text x="480.0" y="250.0" font-size="12" text-anchor="middle">(Thread만)</text>
<text x="600.0" y="232.0" font-size="12" text-anchor="middle">1 = 비특권</text>
<text x="600.0" y="250.0" font-size="12" text-anchor="middle">(Thread만)</text>
</svg>
```

그림 3 — xPSR과 CONTROL의 비트 배치. xPSR의 bit9는 Armv7-M 레지스터에서는 예약이고, 예외 프레임에 쌓인 xPSR에서만 "8바이트 정렬을 위해 4바이트를 더 쌓았다"는 표시로 쓰인다(B2). CONTROL의 FPCA는 FPU가 있는 코어(M4F/M7 등)에만 있다.

### 2.2 IPSR — 예외 번호 표

IPSR 값은 벡터 테이블의 인덱스와 같다. 외부 인터럽트 IRQn은 `16 + n`이다.

| 번호 | 예외 | 우선순위 | 비고 |
|---|---|---|---|
| 0 | (Thread 모드) | — | 예외 처리 중이 아님 |
| 1 | Reset | −3 (고정) | |
| 2 | NMI | −2 (고정) | PRIMASK로도 못 막는다 |
| 3 | HardFault | −1 (고정) | 다른 fault가 꺼져 있으면 여기로 승격(escalation) |
| 4 | MemManage | 설정 가능 | MPU 위반 (F4) |
| 5 | BusFault | 설정 가능 | 버스 오류, **비특권의 SCS 접근** (예제 6) |
| 6 | UsageFault | 설정 가능 | 정의 안 된 명령, **T=0 (INVSTATE)**, 0 나눗셈(켰을 때) |
| 7–10 | 예약 | | Armv8-M에서 7 = SecureFault |
| 11 | SVCall | 설정 가능 | `svc #n` 명령 — 시스템 콜 · 첫 태스크 시작 (B4, C3) |
| 12 | DebugMonitor | 설정 가능 | |
| 14 | PendSV | 설정 가능 | 미뤄 둔 컨텍스트 스위치 (B4, C2) |
| 15 | SysTick | 설정 가능 | RTOS tick (B4, C5) |
| 16 + n | IRQn | 설정 가능 | 이 보드: UART0 = 16, TIMER0 = 24, DUALTIMER = 26 |

### 2.3 예제 3 — 각 예외 안에서 IPSR · LR · SP 찍기

무엇을 확인하나: NMI, SVC, PendSV, SysTick, 외부 IRQ 두 개(중첩)를 소프트웨어로 일으켜 핸들러 안에서 IPSR이 정말 예외 번호인지, LR에 무엇이 들어 있는지, 어느 스택을 쓰는지 본다. 이 예제는 `regs.h`의 SVC 핸들러를 쓰지 않고(`B1_WANT_SVC`를 정의하지 않음) 자기 `SVC_Handler`를 둔다.

```c
/* ex2: 예외마다 IPSR(예외 번호) · CONTROL · LR · SP를 찍는다 */
#include "board.h"
#include "regs.h"
#define ICSR      REG32(0xE000ED04u)
#define SYST_CSR  REG32(0xE000E010u)
#define SYST_RVR  REG32(0xE000E014u)
#define SYST_CVR  REG32(0xE000E018u)
#define NVIC_ISPR0 REG32(0xE000E200u)

#define WHERE(name) do { uint32_t lr_; __asm volatile("mov %0, lr" : "=r"(lr_)); \
    board_puts(name); board_printf_raw(" IPSR=%u CONTROL=0x%x LR=0x%x SP=0x%x MSP=0x%x\n", \
    MRS(ipsr), MRS(control), lr_, get_sp(), MRS(msp)); } while (0)

void NMI_Handler(void)          { WHERE("NMI      "); }
void SVC_Handler(void)          { WHERE("SVC      "); }
void PendSV_Handler(void)       { WHERE("PendSV   "); }
void SysTick_Handler(void)      { SYST_CSR = 0; WHERE("SysTick  "); }
void DUALTIMER_IRQHandler(void) { WHERE("  IRQ10  "); }
void TIMER0_IRQHandler(void)
{
    WHERE("IRQ8     ");
    NVIC_ISPR0 = 1u << 10;           /* 더 높은 우선순위 IRQ10을 pend → 중첩 */
    __asm volatile("dsb\n isb");
    board_printf_raw("IRQ8 back, IPSR=%u\n", MRS(ipsr));
}

int main(void)
{
    WHERE("main     ");
    ICSR = 1u << 31; __asm volatile("isb");   /* NMIPENDSET */
    __asm volatile("svc #0");
    ICSR = 1u << 28; __asm volatile("isb");   /* PENDSVSET */
    SYST_RVR = 1000; SYST_CVR = 0; SYST_CSR = 7;   /* 프로세서 클럭, 인터럽트, enable */
    while (SYST_CSR & 1) { }
    NVIC_IPR(8) = 0xC0; NVIC_IPR(10) = 0x40;  /* 숫자가 작을수록 높다 */
    NVIC_ISER0 = (1u << 8) | (1u << 10);
    NVIC_ISPR0 = 1u << 8; __asm volatile("dsb\n isb");
    WHERE("main     ");
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4784	     96	    328	   5208	   1458	/var/tmp/rtos/B1/ex2/app.elf
main      IPSR=0 CONTROL=0x0 LR=0x327 SP=0x203fffe0 MSP=0x203fffe0
NMI       IPSR=2 CONTROL=0x0 LR=0xfffffff9 SP=0x203fffa8 MSP=0x203fffa8
SVC       IPSR=11 CONTROL=0x0 LR=0xfffffff9 SP=0x203fffa8 MSP=0x203fffa8
PendSV    IPSR=14 CONTROL=0x0 LR=0xfffffff9 SP=0x203fffa8 MSP=0x203fffa8
SysTick   IPSR=15 CONTROL=0x0 LR=0xfffffff9 SP=0x203fffa8 MSP=0x203fffa8
IRQ8      IPSR=24 CONTROL=0x0 LR=0xfffffff9 SP=0x203fffa8 MSP=0x203fffa8
  IRQ10   IPSR=26 CONTROL=0x0 LR=0xfffffff1 SP=0x203fff70 MSP=0x203fff70
IRQ8 back, IPSR=24
main      IPSR=0 CONTROL=0x0 LR=0x24b SP=0x203fffe0 MSP=0x203fffe0
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- IPSR이 표 그대로다: NMI 2, SVC 11, PendSV 14, SysTick 15, TIMER0(IRQ8) 24, DUALTIMER(IRQ10) 26. `main`에서는 0(Thread 모드).
- 첫 단계 예외의 LR은 모두 **0xFFFFFFF9** = "Thread 모드로, MSP를 써서 돌아가라". 이 프로그램은 PSP를 쓰지 않으므로 당연하다.
- IRQ8 안에서 더 높은 우선순위의 IRQ10을 pend하자 IRQ10이 **즉시 끼어들었고**(중첩), 그 LR은 **0xFFFFFFF1** = "Handler 모드로 돌아가라(MSP)". 복귀 후 IRQ8은 IPSR=24로 이어서 실행됐다.
- 모든 핸들러의 SP = MSP다. 핸들러는 **항상 MSP**를 쓴다. `main`의 SP 0x203fffe0에서 핸들러 SP 0x203fffa8까지 56바이트 = 하드웨어 프레임 32바이트 + 핸들러 자신의 `push` 24바이트. 중첩된 IRQ10은 거기서 다시 56바이트 아래(0x203fff70)다.
- 핸들러 안의 CONTROL은 0이다. CONTROL을 읽어서는 "지금 Handler인가"를 알 수 없다 — 그건 IPSR이 말한다.

### 2.4 EPSR의 T 비트 — 왜 반드시 1이어야 하나

Arm 아키텍처의 큰 코어(Cortex-A, Cortex-R)는 32비트 Arm 명령과 16/32비트 Thumb 명령을 둘 다 실행하고, 둘 사이를 오갈 때 **분기 주소의 bit0**을 표시로 쓴다(`BX`/`BLX`: bit0 = 1이면 Thumb). Cortex-M은 **Thumb만** 실행한다. 그런데 이 bit0 규칙은 그대로 남겼다. 그래서:

- 함수 포인터 값은 항상 홀수다(`&hello` = 0xf9, 실제 명령은 0xf8에 있다).
- 벡터 테이블의 핸들러 주소도 모두 홀수여야 한다. reset 벡터의 bit0이 EPSR.T의 초기값이 된다.
- `BX`/`BLX`/`POP {pc}`/`LDR pc`로 bit0 = 0인 주소에 가면 T가 0이 된다. Cortex-M은 Arm 상태가 없으므로 **다음 명령을 실행하려는 순간 UsageFault(INVSTATE)**가 난다.
- 예외 복귀 때 하드웨어는 스택에 쌓인 xPSR을 그대로 복원한다. 쌓인 xPSR의 bit24가 0이면 복귀하자마자 같은 fault가 난다. 그래서 RTOS가 새 태스크의 위조 프레임을 만들 때 xPSR 칸에 **0x01000000**을 넣는다(A4에서 본 FreeRTOS `portINITIAL_XPSR`, C3).

### 2.5 예제 4 — T 비트를 일부러 0으로 만들기

무엇을 확인하나: 세 경우를 비교한다. (1) 짝수 주소로 함수 포인터 호출, (2) 위조 예외 프레임에 xPSR = 0x01000000을 넣고 예외 복귀, (3) 같은 프레임에 xPSR = 0. (2)와 (3)은 SVC 핸들러에서 PSP를 위조 프레임으로 맞추고 EXC_RETURN = 0xFFFFFFFD로 복귀한다. 하드웨어는 PSP에서 8워드를 꺼내 PC와 xPSR을 복원한다. 이것이 RTOS가 첫 태스크를 시작하는 방법의 축소판이다(자세히는 C3).

```c
/* ex6: Thumb 비트(EPSR.T)가 0이 되면? Cortex-M은 Thumb만 실행하므로 INVSTATE UsageFault
 * -DCASE=1: 짝수 주소로 BLX   -DCASE=2: 가짜 예외 프레임 xPSR=0x01000000   -DCASE=3: xPSR=0 */
#include "board.h"
#include "regs.h"

uint32_t *g_fake_sp;

__attribute__((noinline)) static void hello(void) { board_printf_raw("hello() ran, xPSR=0x%x\n", MRS(xpsr)); }

#if CASE != 1
static uint32_t task_stack[128] __attribute__((aligned(8)));
static void task_entry(void)
{
    board_printf_raw("task_entry on PSP: SP=0x%x CONTROL=0x%x\n", get_sp(), MRS(control));
    board_exit(0);
}
#endif

/* SVC: PSP를 가짜 프레임으로 맞추고 EXC_RETURN=0xFFFFFFFD(Thread, PSP)로 복귀 → 하드웨어가 8워드를 꺼낸다 */
__asm__("  .global SVC_Handler\n  .type SVC_Handler, %function\n  .thumb_func\nSVC_Handler:\n"
        "  ldr r0, =g_fake_sp\n  ldr r0, [r0]\n  msr psp, r0\n  isb\n"
        "  ldr lr, =0xFFFFFFFD\n  bx lr\n  .ltorg\n");

int main(void)
{
    void (*fp)(void) = hello;
    board_printf_raw("&hello = 0x%x (bit0=1 → Thumb)\n", (unsigned)(uintptr_t)fp);
#if CASE == 1
    fp = (void (*)(void))((uintptr_t)fp & ~1u);       /* bit0을 지운다 */
    board_printf_raw("calling 0x%x ...\n", (unsigned)(uintptr_t)fp);
    fp();
#else
    uint32_t *sp = &task_stack[128] - 8;              /* 하드웨어 프레임 8워드 */
    for (int i = 0; i < 5; i++) sp[i] = 0;            /* R0-R3, R12 */
    sp[5] = 0xFFFFFFFF;                               /* LR: 돌아오면 안 됨 */
    sp[6] = (uint32_t)(uintptr_t)task_entry & ~1u;    /* PC: bit0 없이 (반환 주소 규칙) */
    sp[7] = XPSR;                                     /* xPSR: T 비트 = bit24 */
    g_fake_sp = sp;
    board_printf_raw("fake frame PC=0x%x xPSR=0x%x, svc...\n", sp[6], sp[7]);
    __asm volatile("svc #0");
#endif
    board_printf_raw("not reached\n");
    return 1;
}
```

```sh
R=/Users/donh/.rtos_scratch/B1/run.sh
$R ex6 -DCASE=1
$R ex6 -DCASE=2 -DXPSR=0x01000000u
$R ex6 -DCASE=3 -DXPSR=0u
arm-none-eabi-nm /var/tmp/rtos/B1/ex6/app.elf | grep -i " t hello\| t task_entry"
```

```text
== CASE=1
&hello = 0xf9 (bit0=1 → Thumb)
calling 0xf8 ...

[HardFault] PC=0xf8 LR=0x123 xPSR=0x60000000 CFSR=0x20000 HFSR=0x40000000
[board_exit] code=255
exit=1
== CASE=2
&hello = 0xf9 (bit0=1 → Thumb)
fake frame PC=0x108 xPSR=0x1000000, svc...
task_entry on PSP: SP=0x20000258 CONTROL=0x2
[board_exit] code=0
exit=0
== CASE=3
&hello = 0xf9 (bit0=1 → Thumb)
fake frame PC=0x108 xPSR=0x0, svc...

[HardFault] PC=0x108 LR=0xffffffff xPSR=0x0 CFSR=0x20000 HFSR=0x40000000
[board_exit] code=255
exit=1
000000f8 t hello
00000108 t task_entry
```

(빌드 줄 `text data bss`는 생략했다.)

출력에서 볼 것:

- `nm`은 `hello`를 0xf8로 보여 주지만 C의 `&hello`는 **0xf9**다. 심볼 주소는 명령의 실제 위치, 함수 포인터는 "Thumb로 가라"는 bit0을 단 값이다.
- **CASE 1**: 0xf8로 `blx`하자 fault. 쌓인 PC = 0xf8(실행하려던 바로 그 명령), 쌓인 xPSR = 0x60000000 → **bit24(T)가 0**이다. CFSR = 0x20000 = UsageFault의 **INVSTATE** 비트(bit17). UsageFault를 따로 켜지 않았으므로 HardFault로 승격(HFSR = 0x40000000, FORCED)됐다.
- **CASE 2**: 위조 프레임으로 예외 복귀가 성공했다. `task_entry`는 Thread 모드, **PSP 위에서**, CONTROL = 0x2(특권 + PSP)로 돈다. 코드 어디에도 `msr control`이 없는데 SPSEL이 1이 됐다 — **EXC_RETURN 0xFFFFFFFD로 복귀하면 하드웨어가 CONTROL.SPSEL을 1로 만든다**. FreeRTOS가 바로 이 방법을 쓴다(§8).
- **CASE 3**: 같은 프레임인데 xPSR만 0이다. 복귀는 됐지만 첫 명령(PC = 0x108)에서 INVSTATE. 쌓인 LR = 0xffffffff는 위조 프레임에 넣은 값 그대로다.
- 위조 프레임의 PC 칸에는 bit0을 **지운** 주소를 넣었다. 예외 프레임의 PC는 "돌아갈 명령의 주소"이고, Thumb 여부는 xPSR.T가 따로 말해 주기 때문이다. bit0을 남겨 두면 Armv7-M에서는 예측할 수 없는(UNPREDICTABLE) 동작이다. FreeRTOS `pxPortInitialiseStack()`도 PC 칸에 `portSTART_ADDRESS_MASK`(bit0 지우기)를 적용한다.

> **Don 경험과 연결**: SSD 부트로더에서 "다음 이미지로 점프" 코드를 짤 때 Cortex-R은 Arm/Thumb 둘 다라서 bit0이 의미 있는 선택이었다. Cortex-M 부트로더에서 앱으로 점프할 때는 앱 벡터 테이블의 reset 항목(이미 홀수)을 그대로 함수 포인터로 쓰면 된다. 링커 스크립트 심볼로 직접 주소를 만들어 `| 1`을 잊으면 CASE 1과 똑같이 죽는다(BM L2).

---

## 3. 특수 레지스터 — 마스크 셋과 CONTROL

### 3.1 접근 방법: MRS / MSR / CPS

특수 레지스터는 메모리 주소가 없다. 전용 명령으로만 읽고 쓴다.

| 명령 | 뜻 | 예 |
|---|---|---|
| `MRS Rd, spec` | 특수 레지스터 → 범용 레지스터 | `mrs r0, control` |
| `MSR spec, Rn` | 범용 레지스터 → 특수 레지스터 | `msr psp, r0` |
| `CPSID i` / `CPSIE i` | PRIMASK = 1 / 0 (인터럽트 끄기/켜기) | 크리티컬 섹션 (C7) |
| `CPSID f` / `CPSIE f` | FAULTMASK = 1 / 0 | |
| `ISB` | 명령 파이프라인 비우기 | CONTROL을 바꾼 뒤 필수 |

CMSIS를 쓰면 `__get_CONTROL()`, `__set_PSP()`, `__get_IPSR()` 같은 인라인 함수가 같은 명령을 낸다(예제 14의 Zephyr 코드가 이것을 쓴다).

### 3.2 마스크 레지스터 셋 — 여기서는 소개만 (주인: B3)

| 레지스터 | 비트 | 1로 하면 | NMI · HardFault |
|---|---|---|---|
| PRIMASK | 1비트 | 우선순위 설정 가능한 모든 예외를 막는다 (실행 우선순위 = 0) | 통과 |
| FAULTMASK | 1비트 | HardFault까지 포함해 NMI 빼고 다 막는다 (우선순위 −1). 예외 복귀 때 자동으로 0 | NMI만 통과 |
| BASEPRI | 8비트(구현 비트만) | 이 값 **이상**(숫자가 같거나 큰 = 우선순위 같거나 낮은) 인터럽트만 막는다. 0이면 마스킹 없음 | 통과 |

FreeRTOS의 `taskENTER_CRITICAL()`이 Cortex-M3/M4에서 PRIMASK가 아니라 BASEPRI를 쓰는 이유, 즉 "커널보다 높은 인터럽트는 막지 않는다"는 설계는 B3과 C7이 다룬다. 이 노트에서 기억할 것은 하나다: **이 셋은 특권 코드에서만 바뀐다**(예제 5).

### 3.3 CONTROL — Thread 모드의 성격을 정하는 3비트

| 비트 | 이름 | 0 | 1 | 언제 의미가 있나 |
|---|---|---|---|---|
| 0 | nPRIV | Thread 모드 특권 | Thread 모드 **비특권** | Thread 모드에서만. Handler는 항상 특권 |
| 1 | SPSEL | Thread 모드가 MSP 사용 | Thread 모드가 **PSP** 사용 | Thread 모드에서만. Handler는 항상 MSP |
| 2 | FPCA | 현재 FP 문맥 없음 | FP 명령을 썼다 → 예외 때 FP 레지스터도 쌓아야 함 | FPU 있는 코어만 (§7, B6) |

두 가지를 꼭 기억하자.

1. **CONTROL은 Thread 모드를 위한 레지스터다.** Handler 모드는 언제나 특권 + MSP다. 예제 8에서 확인하겠지만, 핸들러 안에서 SPSEL을 1로 써도 무시되고 0으로 읽힌다.
2. **CONTROL을 바꾼 직후에는 `ISB`를 넣는다.** 파이프라인에 이미 들어온 다음 명령이 옛 SP · 옛 특권으로 실행되는 것을 막는다. Arm이 권고하는 규칙이고, FreeRTOS · Zephyr 소스 모두 `msr control` 뒤에 `isb`가 있다.

Armv8-M(M23/M33/M55/M85)에서는 CONTROL에 비트가 더 생긴다(Secure 쪽 FP 상태를 나타내는 SFPA 등). 그리고 스택 넘침을 하드웨어로 잡는 **MSPLIM / PSPLIM** 레지스터가 생긴다. 세대별 차이는 B7, TrustZone은 F6이 다룬다.

---
## 4. Thread / Handler 모드 × 특권 / 비특권

> 🔁 이 절은 Bare-Metal BM C1과 겹친다 (주인: 이 노트).

### 4.1 직관 — "누가 지금 돌고 있나"와 "무엇을 해도 되나"는 다른 질문

- **모드**는 "지금 예외를 처리하는 중인가"다. 예외(인터럽트 포함)를 처리하는 중이면 **Handler 모드**, 아니면 **Thread 모드**. 소프트웨어가 고르는 게 아니라 하드웨어가 예외 진입 · 복귀 때 바꾼다. IPSR ≠ 0이면 Handler다.
- **특권(privilege)**은 "시스템 레지스터와 보호된 메모리를 건드려도 되나"다. 특권이면 NVIC · SCB · SysTick · MPU 설정, 마스크 레지스터, CONTROL을 바꿀 수 있다. 비특권이면 못 한다. MPU를 켜면 메모리 영역별로도 막을 수 있다(F4).

Linux에 비유하면(EL A1) 비특권 Thread = 유저 공간 프로세스, 특권 Handler = 커널(시스템 콜 · 인터럽트 처리)이다. Cortex-A의 EL0/EL1(EL C5)과 같은 생각을 MCU 크기로 줄인 것이다. 차이는 Cortex-M에는 MMU가 없고, **Thread 모드도 특권일 수 있다**는 점이다. 그리고 bare-metal 펌웨어와 FreeRTOS 기본 포트는 실제로 모든 Thread 코드를 특권으로 돌린다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="245.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">특권 (privileged)</text>
<text x="555.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">비특권 (unprivileged)</text>
<text x="60.0" y="112.0" font-size="13" text-anchor="middle" font-weight="bold">Thread</text>
<text x="60.0" y="130.0" font-size="12" text-anchor="middle">모드</text>
<text x="60.0" y="262.0" font-size="13" text-anchor="middle" font-weight="bold">Handler</text>
<text x="60.0" y="280.0" font-size="12" text-anchor="middle">모드</text>
<rect x="130.0" y="60.0" width="230.0" height="100.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="245.0" y="96.5" font-size="13" text-anchor="middle" font-weight="bold">Thread + 특권</text>
<text x="245.0" y="114.5" font-size="12" text-anchor="middle">리셋 직후 · bare-metal main</text>
<text x="245.0" y="132.6" font-size="12" text-anchor="middle">FreeRTOS 기본 태스크</text>
<rect x="440.0" y="60.0" width="230.0" height="100.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="555.0" y="96.5" font-size="13" text-anchor="middle" font-weight="bold">Thread + 비특권</text>
<text x="555.0" y="114.5" font-size="12" text-anchor="middle">MPU로 묶인 사용자 태스크</text>
<text x="555.0" y="132.6" font-size="12" text-anchor="middle">Zephyr user thread</text>
<rect x="130.0" y="210.0" width="230.0" height="100.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="245.0" y="246.6" font-size="13" text-anchor="middle" font-weight="bold">Handler (항상 특권)</text>
<text x="245.0" y="264.6" font-size="12" text-anchor="middle">모든 예외 · ISR</text>
<text x="245.0" y="282.6" font-size="12" text-anchor="middle">SVC · PendSV · SysTick</text>
<rect x="440.0" y="210.0" width="230.0" height="100.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,3"/>
<text x="555.0" y="255.6" font-size="13" text-anchor="middle" font-weight="bold">Handler + 비특권</text>
<text x="555.0" y="273.6" font-size="12" text-anchor="middle">존재하지 않는다</text>
<line x1="362.0" y1="90.0" x2="438.0" y2="90.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<circle cx="400" cy="76" r="10" fill="none" stroke="currentColor"/>
<text x="400.0" y="80.5" font-size="12" text-anchor="middle">1</text>
<line x1="438.0" y1="130.0" x2="362.0" y2="130.0" stroke="#d0564a" stroke-width="1.2" stroke-dasharray="5,3" marker-end="url(#ah3)"/>
<circle cx="400" cy="146" r="10" fill="none" stroke="currentColor"/>
<text x="400.0" y="150.5" font-size="12" text-anchor="middle">2</text>
<text x="400.0" y="124.0" font-size="14" text-anchor="middle" font-weight="bold">X</text>
<line x1="200.0" y1="162.0" x2="200.0" y2="208.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<circle cx="184" cy="186" r="10" fill="none" stroke="currentColor"/>
<text x="184.0" y="190.5" font-size="12" text-anchor="middle">3</text>
<line x1="290.0" y1="208.0" x2="290.0" y2="162.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<circle cx="306" cy="186" r="10" fill="none" stroke="currentColor"/>
<text x="306.0" y="190.5" font-size="12" text-anchor="middle">5</text>
<line x1="450.0" y1="162.0" x2="362.0" y2="212.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<circle cx="394" cy="174" r="10" fill="none" stroke="currentColor"/>
<text x="394.0" y="178.5" font-size="12" text-anchor="middle">4</text>
<line x1="362.0" y1="252.0" x2="520.0" y2="162.0" stroke="currentColor" stroke-width="1.2" stroke-dasharray="5,3" marker-end="url(#ah3)"/>
<circle cx="416" cy="240" r="10" fill="none" stroke="currentColor"/>
<text x="416.0" y="244.5" font-size="12" text-anchor="middle">6</text>
</svg>
```

그림 4 — 모드와 특권의 2×2 조합. Handler 모드는 항상 특권이라 오른쪽 아래 칸은 존재하지 않는다. 번호는 아래 표의 전이.

| 번호 | 전이 | 어떻게 | 확인한 예제 |
|---|---|---|---|
| 1 | Thread 특권 → Thread 비특권 | 특권 코드가 `MSR CONTROL`로 nPRIV = 1, 그리고 `ISB` | 예제 5 · 6 · 7 (`run_task(..., 3)`) |
| 2 | Thread 비특권 → Thread 특권 | **직접은 불가능**. `MSR CONTROL`은 조용히 무시된다 | 예제 5 |
| 3 | Thread 특권 → Handler | 예외 발생 (IRQ, SVC, fault) | 예제 3 |
| 4 | Thread 비특권 → Handler | 예외 발생. 비특권 코드가 스스로 일으킬 수 있는 것은 `SVC`(그리고 fault) | 예제 7 |
| 5 | Handler → Thread 특권 | 예외 복귀. 복귀 전에 핸들러가 nPRIV = 0으로 써 두면 특권으로 돌아간다 | 예제 7 (`svc #0`) |
| 6 | Handler → Thread 비특권 | 예외 복귀. nPRIV = 1이면 비특권으로 돌아간다 (RTOS가 사용자 태스크로 전환할 때) | 예제 7 |

핵심 규칙은 하나다: **특권은 내려놓기는 쉽고(1), 되찾는 길은 예외를 거치는 것(4 → 5)뿐이다.** 그래서 비특권 코드가 아무리 망가져도, 특권으로 돌아가려면 반드시 커널(예외 핸들러)의 검사를 통과해야 한다. 이것이 MPU 기반 RTOS 보호 모델(FreeRTOS-MPU, Zephyr userspace)의 하드웨어 근거다.

### 4.2 예제 5 — 비특권에서 특수 레지스터 쓰기는 "조용히 무시"

무엇을 확인하나: 비특권 Thread 모드(CONTROL = 3)에서 CONTROL, BASEPRI, PRIMASK, MSP를 바꾸려 하면 어떻게 되나. fault가 나나, 무시되나? 그리고 같은 일을 특권에서 하면?

```c
/* ex4: 비특권 Thread 모드에서 특수 레지스터 쓰기는 "조용히 무시"된다 (fault 아님) */
#define B1_WANT_SVC
#include "board.h"
#include "regs.h"

static uint32_t task_stack[256] __attribute__((aligned(8)));

void svc_c(uint32_t *frame, uint32_t num) { (void)frame; if (num == 255) svc_back_to_priv(); }

static void user_task(void)
{
    board_printf_raw("user: CONTROL=0x%x MSP=0x%x PSP=0x%x\n", MRS(control), MRS(msp), MRS(psp));
    MSR(control, 0);  __asm volatile("isb");
    board_printf_raw("user: msr control,0   -> CONTROL=0x%x (무시됨)\n", MRS(control));
    MSR(basepri, 0x40);
    board_printf_raw("user: msr basepri,0x40 -> BASEPRI=0x%x\n", MRS(basepri));
    __asm volatile("cpsid i" ::: "memory");
    board_printf_raw("user: cpsid i          -> PRIMASK=%u\n", MRS(primask));
    MSR(msp, 0x20001000);
    board_printf_raw("user: msr msp          -> (특권에서 확인)\n");
    MSR(apsr_nzcvq, 0xF8000000);
    board_printf_raw("user: msr apsr_nzcvq   -> APSR=0x%x (APSR은 누구나 쓴다)\n", MRS(apsr));
}

static void priv_task(void)
{
    MSR(basepri, 0x40);
    __asm volatile("cpsid i" ::: "memory");
    board_printf_raw("priv: same writes      -> BASEPRI=0x%x PRIMASK=%u\n", MRS(basepri), MRS(primask));
    MSR(basepri, 0); __asm volatile("cpsie i" ::: "memory");
}

int main(void)
{
    uint32_t msp0 = MRS(msp);
    run_task(user_task, &task_stack[256], 3);     /* 3 = 비특권 + PSP */
    board_printf_raw("main: back, CONTROL=0x%x, MSP before=0x%x now=0x%x\n", MRS(control), msp0, MRS(msp));
    run_task(priv_task, &task_stack[256], 2);     /* 2 = 특권 + PSP */
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4780	     96	   1352	   6228	   1854	/var/tmp/rtos/B1/ex4/app.elf
user: CONTROL=0x3 MSP=0x0 PSP=0x0
user: msr control,0   -> CONTROL=0x3 (무시됨)
user: msr basepri,0x40 -> BASEPRI=0x0
user: cpsid i          -> PRIMASK=0
user: msr msp          -> (특권에서 확인)
user: msr apsr_nzcvq   -> APSR=0xf8000000 (APSR은 누구나 쓴다)
main: back, CONTROL=0x0, MSP before=0x203ffff0 now=0x203ffff0
priv: same writes      -> BASEPRI=0x40 PRIMASK=1
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- **fault는 하나도 나지 않았다.** `msr control`, `msr basepri`, `cpsid i`, `msr msp` 모두 비특권에서는 **아무 일도 하지 않는 명령(NOP)처럼** 지나갔다. CONTROL은 그대로 3, BASEPRI · PRIMASK는 0, MSP도 그대로다(main에서 확인한 before = now).
- **비특권에서는 MSP와 PSP를 읽어도 0이다.** 지금 쓰고 있는 PSP조차 0으로 읽힌다. Armv7-M의 MRS 정의가 "SP 읽기는 특권일 때만"이기 때문이다. 비특권 코드에서 스택 검사를 `MRS psp`로 짜면 엉터리 값이 나온다.
- APSR 플래그는 비특권도 쓸 수 있다(`apsr_nzcvq`).
- 같은 쓰기를 특권 태스크(CONTROL = 2)에서 하면 BASEPRI = 0x40, PRIMASK = 1로 정상 반영된다.

> **함정**: "조용히 무시"가 fault보다 위험하다. 비특권 태스크 안에서 `taskENTER_CRITICAL()` 같은 크리티컬 섹션을 직접 `cpsid i`로 구현하면, 컴파일도 되고 실행도 되는데 **인터럽트는 전혀 막히지 않는다**. 경쟁 조건이 생기고 아무 증상 없이 가끔 데이터가 깨진다. FreeRTOS-MPU가 비특권 태스크의 커널 API 호출을 SVC 래퍼로 감싸는 이유다(F4, C8).

### 4.3 예제 6 — 비특권 코드가 NVIC/SCB를 건드리면 BusFault

무엇을 확인하나: 시스템 제어 공간(SCS, System Control Space: 0xE000E000~0xE000EFFF, NVIC · SCB · SysTick · MPU 레지스터가 있는 곳)은 비특권 접근이 금지다. 레지스터 명령(MSR)과 달리 **메모리 접근은 무시되지 않고 fault가 난다**. 네 경우를 본다: (1) SCB ICSR 읽기, (2) NVIC ISER 쓰기, (3) (1)과 같지만 BusFault 예외를 따로 켠 경우, (4) 예외적으로 허용되는 STIR 쓰기.

```c
/* ex5: 비특권 코드가 SCS(SCB/NVIC/SysTick, 0xE000E000~)를 건드리면 BusFault → HardFault
 * 빌드: -DCASE=1 (SCB ICSR 읽기) / 2 (NVIC ISER 쓰기) / 3 (1 + BusFault 따로 켬) / 4 (STIR + USERSETMPEND) */
#define B1_WANT_SVC
#include "board.h"
#include "regs.h"
#define SCB_ICSR   0xE000ED04u
#define SCB_CCR    REG32(0xE000ED14u)
#define SCB_SHCSR  REG32(0xE000ED24u)
#define SCB_CFSR   REG32(0xE000ED28u)
#define SCB_BFAR   REG32(0xE000ED38u)
#define NVIC_STIR  0xE000EF00u

static uint32_t task_stack[256] __attribute__((aligned(8)));
void svc_c(uint32_t *frame, uint32_t num) { (void)frame; if (num == 255) svc_back_to_priv(); }

void BusFault_Handler(void)     /* CASE 3에서만 불린다 (SHCSR.BUSFAULTENA=1) */
{
    board_printf_raw("[BusFault] IPSR=%u CFSR=0x%x BFAR=0x%x\n", MRS(ipsr), SCB_CFSR, SCB_BFAR);
    board_exit(3);
}
void TIMER0_IRQHandler(void) { board_printf_raw("IRQ8 ran (IPSR=%u) - pended from user via STIR\n", MRS(ipsr)); }

static void user_task(void)
{
    board_printf_raw("user: CONTROL=0x%x, touching SCS...\n", MRS(control));
#if CASE == 1 || CASE == 3
    uint32_t v = REG32(SCB_ICSR);
    board_printf_raw("user: ICSR=0x%x (여기 오면 안 됨)\n", v);
#elif CASE == 2
    REG32(0xE000E100u) = 1u << 8;
    board_printf_raw("user: wrote ISER (여기 오면 안 됨)\n");
#elif CASE == 4
    REG32(NVIC_STIR) = 8;              /* IRQ8을 소프트웨어로 pend */
    __asm volatile("dsb\n isb");
    board_printf_raw("user: STIR write OK, no fault\n");
#endif
}

int main(void)
{
#if CASE == 3
    SCB_SHCSR |= 1u << 17;             /* BUSFAULTENA: HardFault로 승격하지 않고 BusFault로 */
#endif
#if CASE == 4
    SCB_CCR |= 1u << 1;                /* USERSETMPEND: 비특권도 STIR 쓰기 허용 */
    NVIC_ISER0 = 1u << 8;
#endif
    run_task(user_task, &task_stack[256], 3);
    board_printf_raw("main: survived\n");
    return 0;
}
```

```sh
for c in 1 2 3 4; do echo "== CASE=$c"; /Users/donh/.rtos_scratch/B1/run.sh ex5 -DCASE=$c | grep -v "text\|app.elf"; done
```

```text
== CASE=1
user: CONTROL=0x3, touching SCS...

[HardFault] PC=0x10a LR=0x105 xPSR=0x61000000 CFSR=0x8200 HFSR=0x40000000
[board_exit] code=255
exit=1
== CASE=2
user: CONTROL=0x3, touching SCS...

[HardFault] PC=0x10e LR=0x105 xPSR=0x61000000 CFSR=0x8200 HFSR=0x40000000
[board_exit] code=255
exit=1
== CASE=3
user: CONTROL=0x3, touching SCS...
[BusFault] IPSR=5 CFSR=0x8200 BFAR=0xe000ed04
[board_exit] code=3
exit=1
== CASE=4
user: CONTROL=0x3, touching SCS...
IRQ8 ran (IPSR=24) - pended from user via STIR
user: STIR write OK, no fault
main: survived
[board_exit] code=0
exit=0
```

CASE 1의 PC가 어디인지 확인했다.

```sh
arm-none-eabi-addr2line -f -e /var/tmp/rtos/B1/ex5/app.elf 0x10a
arm-none-eabi-objdump -d /var/tmp/rtos/B1/ex5/app.elf | grep -B4 -A2 "^ *10a:"
```

```text
user_task
/Users/donh/.rtos_scratch/B1/ex5.c:27
     104:	f04f 23e0 	mov.w	r3, #3758153728	@ 0xe000e000
     108:	4804      	ldr	r0, [pc, #16]	@ (11c <user_task+0x24>)
     10a:	f8d3 1d04 	ldr.w	r1, [r3, #3332]	@ 0xd04
     10e:	e8bd 4008 	ldmia.w	sp!, {r3, lr}
```

(objdump 출력은 앞의 두 줄을 생략했다.)

출력에서 볼 것:

- CASE 1 · 2: **CFSR = 0x8200** = BusFault 영역의 PRECISERR(bit9, "정확히 이 명령이 일으킨 버스 오류") + BFARVALID(bit15, "BFAR에 주소가 있다"). BusFault를 켜지 않았으니 HardFault로 승격(HFSR.FORCED). 쌓인 PC 0x10a는 정확히 `ldr.w r1, [r3, #0xd04]` = ICSR(0xE000ED04) 읽기다. precise fault라서 PC가 범인을 가리킨다.
- 쌓인 xPSR = 0x61000000: T = 1(정상), IPSR = 0 → **Thread 모드에서** 난 fault라는 증거.
- CASE 3: `SHCSR.BUSFAULTENA`를 켜면 HardFault까지 가지 않고 BusFault 핸들러(**IPSR = 5**)가 받는다. BFAR = 0xe000ed04가 접근한 주소를 정확히 알려 준다. 실무에서는 MemManage · BusFault · UsageFault를 모두 켜 두는 편이 진단에 유리하다(M1).
- CASE 4: SCS 안에서도 **STIR**(Software Trigger Interrupt Register, 0xE000EF00)만은 `CCR.USERSETMPEND = 1`이면 비특권이 쓸 수 있다. 사용자 태스크가 IRQ8을 직접 pend했고 fault 없이 핸들러가 돌았다.
- UART(0x40004000) 출력은 비특권에서도 잘 됐다. MPU가 꺼져 있으면 SCS 밖의 메모리는 비특권도 다 접근할 수 있다. 사용자 태스크를 정말 가두려면 MPU가 필요하다(F4).

Fault 레지스터(CFSR · HFSR · BFAR) 읽는 법 전체와 escalation 규칙은 Bare-Metal BM C8과 RTOS M1이 다룬다.

### 4.4 예제 7 — 특권을 되찾는 유일한 문: SVC

무엇을 확인하나: 비특권 태스크가 `svc #n`으로 "커널"(SVC 핸들러)에 일을 부탁하는 시스템 콜 구조를 만든다. SVC 번호는 명령 안에 들어 있으므로 핸들러가 쌓인 PC 바로 앞 2바이트에서 꺼낸다(`regs.h`의 `SVC_Handler`). 인자와 결과는 쌓인 프레임의 R0 칸으로 오간다. 그리고 `svc #0`으로 특권 자체를 되찾아 본다.

```c
/* ex7: 비특권 태스크가 SVC로 "커널"에게 일을 맡기고, 특권을 되찾는 유일한 문도 SVC다 */
#define B1_WANT_SVC
#include "board.h"
#include "regs.h"
#define SCB_CPUID  REG32(0xE000ED00u)
#define NVIC_ISPR0 REG32(0xE000E200u)

static uint32_t task_stack[256] __attribute__((aligned(8)));

/* r0에 인자, svc #n, r0로 결과 — 인자/결과는 하드웨어가 쌓은 프레임의 R0 칸으로 오간다 */
#define SVC1(n, a) ({ register uint32_t r0_ __asm__("r0") = (a); \
    __asm volatile("svc %1" : "+r"(r0_) : "i"(n) : "memory"); r0_; })

void svc_c(uint32_t *frame, uint32_t num)
{
    board_printf_raw("  [svc #%u] IPSR=%u CONTROL=0x%x r0=0x%x stackedPC=0x%x\n",
                     num, MRS(ipsr), MRS(control), frame[0], frame[6]);
    switch (num) {
    case 0:   svc_back_to_priv(); break;          /* 특권 상승: 실제 커널은 호출 위치를 검사한다 */
    case 1:   if (frame[0] < 32) { NVIC_ISER0 = 1u << frame[0]; NVIC_ISPR0 = 1u << frame[0]; frame[0] = 0; }
              else frame[0] = (uint32_t)-1;       /* 인자 검증 = 커널의 의무 */
              break;
    case 2:   frame[0] = SCB_CPUID; break;        /* 특권 레지스터를 대신 읽어 준다 */
    case 255: svc_back_to_priv(); break;
    }
}
void TIMER0_IRQHandler(void) { board_printf_raw("  IRQ8 handler, IPSR=%u\n", MRS(ipsr)); }

static void user_task(void)
{
    board_printf_raw("user: CONTROL=0x%x\n", MRS(control));
    board_printf_raw("user: CPUID via svc #2 = 0x%x\n", SVC1(2, 0));
    board_printf_raw("user: svc #1(irq=8) -> %d\n", (int)SVC1(1, 8));
    board_printf_raw("user: svc #1(irq=99) -> %d\n", (int)SVC1(1, 99));
    SVC1(0, 0);
    board_printf_raw("user: after svc #0 CONTROL=0x%x -> CPUID direct = 0x%x\n", MRS(control), SCB_CPUID);
}

int main(void)
{
    run_task(user_task, &task_stack[256], 3);
    board_printf_raw("main: CONTROL=0x%x\n", MRS(control));
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4680	     96	   1352	   6128	   17f0	/var/tmp/rtos/B1/ex7/app.elf
user: CONTROL=0x3
  [svc #2] IPSR=11 CONTROL=0x1 r0=0x0 stackedPC=0x108
user: CPUID via svc #2 = 0x410fc231
  [svc #1] IPSR=11 CONTROL=0x1 r0=0x8 stackedPC=0x114
  IRQ8 handler, IPSR=24
user: svc #1(irq=8) -> 0
  [svc #1] IPSR=11 CONTROL=0x1 r0=0x63 stackedPC=0x120
user: svc #1(irq=99) -> -1
  [svc #0] IPSR=11 CONTROL=0x1 r0=0x0 stackedPC=0x12c
user: after svc #0 CONTROL=0x2 -> CPUID direct = 0x410fc231
  [svc #255] IPSR=11 CONTROL=0x1 r0=0x0 stackedPC=0xd6
main: CONTROL=0x0
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- SVC 핸들러 안의 **CONTROL = 0x1**이다. nPRIV = 1이 그대로 남아 있지만 핸들러는 특권으로 CPUID를 읽고 NVIC를 썼다. **nPRIV는 Thread 모드에만 적용**된다는 증거다. SPSEL은 Handler에서 0으로 읽힌다.
- `svc #2`: 비특권이 못 읽는 CPUID(0x410FC231: Arm, 파트 번호 0xC23 = Cortex-M3)를 커널이 대신 읽어 R0 칸에 넣어 줬다. 시스템 콜의 기본 모양이다.
- `svc #1(irq=99)`: 커널이 인자를 검사해서 −1을 돌려줬다. 사용자가 넘긴 값을 믿지 않는 것이 커널의 의무다.
- `svc #0` 뒤 CONTROL = 0x2: 핸들러가 nPRIV를 0으로 쓰고 복귀하자 태스크가 **특권으로 돌아왔다**(그림 4의 4 → 5). 이제 CPUID를 직접 읽을 수 있다.
- 마지막 `svc #255`의 stackedPC 0xd6은 `run_task` 안이다. 트램펄린이 태스크 함수가 돌아온 뒤 특권을 되찾고 MSP로 돌아가는 단계다(이때는 이미 특권이었지만 같은 경로를 탄다).

> **함정 — "아무나 부를 수 있는 특권 상승 SVC"는 보안 구멍이다.** 이 예제의 `svc #0`은 누가 불러도 특권을 준다. 실제 커널은 이렇게 하지 않는다. FreeRTOS-MPU 포트(`portable/GCC/ARM_CM3_MPU/port.c`)의 `portSVC_RAISE_PRIVILEGE` 처리는 "시스템 콜 구현 코드 영역(`__syscalls_flash_start__` ~ `__syscalls_flash_end__`) 안에서 부른 SVC일 때만" 특권을 올린다. Zephyr는 시스템 콜마다 검증 함수(`z_vrfy_...`)가 인자를 검사한 뒤 구현을 부른다(C8).

---

## 5. MSP와 PSP — 왜 RTOS는 태스크를 PSP에, ISR을 MSP에 두나

> 🔁 이 절은 Bare-Metal BM C1과 겹친다 (주인: 이 노트).

### 5.1 직관 — 스택이 두 개인 이유

bare-metal 펌웨어에는 스택이 하나다. `main()`도 ISR도 같은 스택을 쓴다. ISR이 끼어들면 그 ISR의 프레임은 **지금 스택의 맨 아래에** 쌓인다.

RTOS에서는 태스크마다 자기 스택이 있다(A4). 스택 포인터가 하나뿐이라면 ISR은 **그 순간 돌고 있던 태스크의 스택** 위에 쌓인다. 어느 태스크가 돌고 있을 때 인터럽트가 올지 모르므로, **모든 태스크 스택에 "최악의 ISR 중첩 깊이"만큼 여유를 더해야** 한다. 태스크가 10개면 같은 여유를 10번 낸다.

Cortex-M의 답은 스택 포인터를 둘로 나누는 것이다.

- **PSP (Process Stack Pointer)**: Thread 모드(CONTROL.SPSEL = 1일 때)가 쓴다. RTOS는 태스크를 바꿀 때마다 PSP 값을 바꾼다.
- **MSP (Main Stack Pointer)**: Handler 모드가 **항상** 쓴다. reset 때 벡터 테이블 0번 워드로 초기화된다. 시스템에 하나.

인터럽트가 오면 하드웨어는 8워드 프레임을 **지금 쓰던 스택(PSP)**에 쌓은 뒤 Handler 모드로 들어가 **MSP로 전환**한다. ISR의 지역 변수, 더 높은 우선순위 ISR의 중첩 프레임은 전부 MSP에 쌓인다. 결국 태스크 스택에는 "첫 프레임 32바이트"만 더하면 되고, ISR 몫은 MSP 한 곳에서만 낸다.

### 5.2 예제 8 — Thread 모드를 PSP로 옮기고 ISR이 어느 스택을 쓰는지 보기

무엇을 확인하나: `run_task(task, top, 2)`로 Thread 모드를 PSP(1 KB 배열)로 옮긴 뒤 IRQ8을 일으킨다. ISR 안에서 SP · MSP · PSP · CONTROL · LR을 보고, 핸들러 안에서 SPSEL을 1로 써 보면 어떻게 되는지 본다.

```c
/* ex3: Thread 모드를 PSP로 옮기고, ISR이 어느 스택을 쓰는지 본다 */
#include "board.h"
#define B1_WANT_SVC
#include "regs.h"
#define NVIC_ISPR0 REG32(0xE000E200u)

static uint32_t task_stack[256] __attribute__((aligned(8)));   /* 1 KB "태스크 스택" */
static volatile uint32_t isr_psp;

void svc_c(uint32_t *frame, uint32_t num) { (void)frame; if (num == 255) svc_back_to_priv(); }

void TIMER0_IRQHandler(void)
{
    uint32_t lr; __asm volatile("mov %0, lr" : "=r"(lr));
    isr_psp = MRS(psp);
    board_printf_raw("  ISR   : SP=0x%x MSP=0x%x PSP=0x%x CONTROL=0x%x LR=0x%x\n",
                     get_sp(), MRS(msp), isr_psp, MRS(control), lr);
    MSR(control, 2); __asm volatile("isb");     /* Handler 모드에서 SPSEL=1 시도 */
    board_printf_raw("  ISR   : after msr control,2 -> CONTROL=0x%x SP=0x%x\n",
                     MRS(control), get_sp());
}

static void task(void)
{
    uint32_t psp0 = MRS(psp);
    board_printf_raw("  task  : SP=0x%x MSP=0x%x PSP=0x%x CONTROL=0x%x\n",
                     get_sp(), MRS(msp), psp0, MRS(control));
    NVIC_ISPR0 = 1u << 8; __asm volatile("dsb\n isb");     /* IRQ8 발생 */
    board_printf_raw("  task  : PSP in ISR was 0x%x (task SP - %u bytes)\n",
                     isr_psp, psp0 - isr_psp);
}

int main(void)
{
    NVIC_ISER0 = 1u << 8;
    board_printf_raw("main    : SP=0x%x MSP=0x%x CONTROL=0x%x  task_stack top=0x%x\n",
                     get_sp(), MRS(msp), MRS(control), (unsigned)&task_stack[256]);
    run_task(task, &task_stack[256], 2);          /* 2 = 특권 + PSP */
    board_printf_raw("main    : SP=0x%x CONTROL=0x%x (back on MSP)\n", get_sp(), MRS(control));
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4720	     96	   1360	   6176	   1820	/var/tmp/rtos/B1/ex3/app.elf
main    : SP=0x203fffe8 MSP=0x203fffe8 CONTROL=0x0  task_stack top=0x20000468
  task  : SP=0x20000450 MSP=0x203fffe0 PSP=0x20000450 CONTROL=0x2
  ISR   : SP=0x203fffd0 MSP=0x203fffd0 PSP=0x20000430 CONTROL=0x0 LR=0xfffffffd
  ISR   : after msr control,2 -> CONTROL=0x0 SP=0x203fffd0
  task  : PSP in ISR was 0x20000430 (task SP - 32 bytes)
main    : SP=0x203fffe8 CONTROL=0x0 (back on MSP)
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- `task` 안: SP = PSP = 0x20000450(배열 꼭대기 0x20000468에서 함수 프레임만큼 아래), CONTROL = 0x2. MSP는 0x203fffe0에 **멈춰 있다**(`run_task`가 MSP에 8바이트 push한 자리).
- ISR 안: SP = MSP, LR = **0xFFFFFFFD** = "Thread 모드, PSP로 돌아가라". PSP는 정확히 **32바이트**(8워드 프레임) 내려갔다. ISR 자신의 지역 변수와 push는 MSP 쪽(0x203fffe0 → 0x203fffd0, 16바이트)에 갔다.
- ISR 안에서 `msr control, 2`를 해도 CONTROL은 0, SP도 그대로다. **Handler 모드에서 SPSEL은 0으로 읽히고 쓰기는 무시된다.** 핸들러가 어느 스택에서 왔는지 알고 싶으면 CONTROL이 아니라 **EXC_RETURN(LR)의 bit2**를 봐야 한다. lab의 `HardFault_Handler`와 이 노트의 `SVC_Handler`가 `tst lr, #4`를 하는 이유다.
- `run_task`의 `msr control, r2` 바로 뒤의 `isb`가 없다면 다음 `blx r0`이 파이프라인에서 옛 SP 기준으로 처리될 수 있다. 지금 QEMU에서는 차이가 안 보이지만, 실칩에서는 규칙대로 넣는다.

### 5.3 예제 9 — 스택 사용량 측정: (A) 모두 MSP vs (B) 태스크 PSP + ISR MSP

무엇을 확인하나: 같은 "태스크"(200바이트 지역 배열을 쓰는 함수)가 가장 깊은 지점에 있을 때 ISR이 0~3단계 중첩해서 들어오게 하고, 두 구성에서 태스크 스택과 MSP가 각각 몇 바이트 쓰였는지 0xA5A5A5A5 패턴(high water mark, A4 §5.3)으로 잰다.

- (A) 스택 포인터를 하나만 쓰는 RTOS를 흉내: `run_on_msp()`가 **MSP 자체를** 태스크 스택 꼭대기로 바꾼 뒤 태스크를 부른다. ISR도 같은 스택에 쌓인다.
- (B) Cortex-M RTOS 방식: `run_task(..., 2)`로 태스크는 PSP, ISR은 원래 MSP.
- ISR 세 개(IRQ8 · 9 · 10)는 각자 96바이트 지역 배열을 쓰고 다음 단계(더 높은 우선순위)를 pend한다.

```c
/* ex9: 스택 사용량 측정 — (A) 모두 MSP 위  vs  (B) 태스크=PSP, ISR=MSP.  ISR 중첩 깊이 0~3 */
#define B1_WANT_SVC
#include "board.h"
#include "regs.h"
#define NVIC_ISPR0 REG32(0xE000E200u)
#define PAT 0xA5A5A5A5u
#define TSTK_WORDS 512                          /* 2 KB 태스크 스택 */
static uint32_t tstk[TSTK_WORDS] __attribute__((aligned(8)));
static volatile int depth;                       /* 이번 실행의 ISR 중첩 깊이 */
static volatile uint32_t sink;

void svc_c(uint32_t *frame, uint32_t num) { (void)frame; if (num == 255) svc_back_to_priv(); }

/* (A)용: 현재 SP(=MSP)를 top으로 바꿔 fn을 부르고 되돌린다 — ISR도 이 스택에 쌓인다 */
void run_on_msp(void (*fn)(void), uint32_t *top);
__asm__("  .global run_on_msp\n  .type run_on_msp, %function\n  .thumb_func\nrun_on_msp:\n"
        "  push {r4, lr}\n  mov r4, sp\n  mov sp, r1\n  blx r0\n  mov sp, r4\n  pop {r4, pc}\n");

#define ISR_BODY(next) do { volatile uint8_t buf[96]; for (int i = 0; i < 96; i++) buf[i] = (uint8_t)i; \
    if (depth > (next)) { NVIC_ISPR0 = 1u << (8 + (next)); __asm volatile("dsb\n isb"); } \
    sink += buf[17]; } while (0)
void TIMER0_IRQHandler(void)    { ISR_BODY(1); }   /* 우선순위 0xC0 (가장 낮음) */
void TIMER1_IRQHandler(void)    { ISR_BODY(2); }   /* 0x80 */
void DUALTIMER_IRQHandler(void) { ISR_BODY(3); }   /* 0x40 (가장 높음) */

__attribute__((noinline)) static void deep_work(void)  /* 태스크의 가장 깊은 지점 */
{
    volatile uint8_t buf[200];
    for (int i = 0; i < 200; i++) buf[i] = (uint8_t)i;
    if (depth > 0) { NVIC_ISPR0 = 1u << 8; __asm volatile("dsb\n isb"); }   /* 여기서 IRQ가 온다 */
    sink += buf[3];
}
static void task(void) { deep_work(); }

static uint32_t used(uint32_t *lo, uint32_t *hi)          /* hi에서 아래로, 패턴이 깨진 가장 낮은 곳 */
{
    uint32_t *p = lo; while (p < hi && *p == PAT) p++;
    return (uint32_t)((hi - p) * 4);
}

int main(void)
{
    NVIC_IPR(8) = 0xC0; NVIC_IPR(9) = 0x80; NVIC_IPR(10) = 0x40;
    NVIC_ISER0 = 7u << 8;
    uint32_t *msp_hi = (uint32_t *)(MRS(msp) - 8), *msp_lo = msp_hi - 512;  /* -8: run_task의 push 자리 */
    board_printf_raw("depth  A:task(MSP-only)  B:task(PSP)  B:MSP(ISR)\n");
    for (int d = 0; d <= 3; d++) {
        uint32_t ua, ub, um;
        depth = d;
        for (int i = 0; i < TSTK_WORDS; i++) tstk[i] = PAT;
        run_on_msp(task, &tstk[TSTK_WORDS]);
        ua = used(tstk, &tstk[TSTK_WORDS]);
        for (int i = 0; i < TSTK_WORDS; i++) tstk[i] = PAT;
        for (volatile uint32_t *p = msp_lo; p < msp_hi; p++) *p = PAT;  /* volatile: memset 호출(=MSP 사용) 금지 */
        run_task(task, &tstk[TSTK_WORDS], 2);
        ub = used(tstk, &tstk[TSTK_WORDS]);
        um = used(msp_lo, msp_hi);          /* ISR이 MSP에 쓴 양 */
        board_printf_raw("  %d      %u                 %u           %u\n", d, ua, ub, um);
    }
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4828	     96	   2384	   7308	   1c8c	/var/tmp/rtos/B1/ex9/app.elf
depth  A:task(MSP-only)  B:task(PSP)  B:MSP(ISR)
  0      200                 200           0
  1      328                 232           96
  2      456                 232           224
  3      584                 232           352
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- (A)에서는 ISR 한 단계마다 태스크 스택이 정확히 **128바이트**(하드웨어 프레임 32 + ISR 지역 배열 96)씩 늘었다. 중첩 3단계면 584바이트.
- (B)의 태스크 스택은 ISR이 몇 단계든 **232바이트 = 200 + 32**로 고정이다. 첫 프레임 32바이트만 PSP에 쌓였다.
- (B)의 MSP는 96(첫 ISR은 지역 배열만, 프레임은 PSP에 있으므로) → 224 → 352로 늘었다. ISR 몫이 **한 곳에** 모였다.

> **이 측정을 하다가 실제로 낸 버그**: 처음에는 MSP 측정 영역을 `for` 루프로 0xA5 패턴을 채웠는데, `-O2`가 그 루프를 `memset()` 호출로 바꿨다. `memset`은 **지금 쓰고 있는 MSP 바로 아래**에 자기 레지스터를 push하는데, 바로 그 영역을 채우고 있었다. 자기 저장 레지스터를 덮어쓰고 돌아가다가 HardFault(CFSR = 0x8200)가 났다. `volatile` 포인터로 바꿔 해결했다. "지금 쓰는 스택 아래를 건드리는 코드는 그 자신도 스택을 쓴다"는 교훈 — RTOS의 스택 채우기는 그래서 태스크가 돌기 **전에** 한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="70.0" y1="270.0" x2="650.0" y2="270.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="70.0" y1="270.0" x2="70.0" y2="50.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="66.0" y1="270.0" x2="70.0" y2="270.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="66.0" y1="233.3" x2="70.0" y2="233.3" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="237.3" font-size="12" text-anchor="end">100</text>
<line x1="70.0" y1="233.3" x2="650.0" y2="233.3" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="196.7" x2="70.0" y2="196.7" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="200.7" font-size="12" text-anchor="end">200</text>
<line x1="70.0" y1="196.7" x2="650.0" y2="196.7" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="160.0" x2="70.0" y2="160.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="164.0" font-size="12" text-anchor="end">300</text>
<line x1="70.0" y1="160.0" x2="650.0" y2="160.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="123.3" x2="70.0" y2="123.3" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="127.3" font-size="12" text-anchor="end">400</text>
<line x1="70.0" y1="123.3" x2="650.0" y2="123.3" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="86.7" x2="70.0" y2="86.7" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="90.7" font-size="12" text-anchor="end">500</text>
<line x1="70.0" y1="86.7" x2="650.0" y2="86.7" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="50.0" x2="70.0" y2="50.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="54.0" font-size="12" text-anchor="end">600</text>
<line x1="70.0" y1="50.0" x2="650.0" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<text x="20.0" y="24.0" font-size="12">스택 사용량 (바이트)</text>
<rect x="87.5" y="196.7" width="34" height="73.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="104.5" y="191.7" font-size="12" text-anchor="middle">200</text>
<rect x="125.5" y="196.7" width="34" height="73.3" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="142.5" y="191.7" font-size="12" text-anchor="middle">200</text>
<rect x="163.5" y="270.0" width="34" height="0.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="180.5" y="265.0" font-size="12" text-anchor="middle">0</text>
<text x="142.5" y="288.0" font-size="12" text-anchor="middle">ISR 중첩 0</text>
<rect x="232.5" y="149.7" width="34" height="120.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="249.5" y="144.7" font-size="12" text-anchor="middle">328</text>
<rect x="270.5" y="184.9" width="34" height="85.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="287.5" y="179.9" font-size="12" text-anchor="middle">232</text>
<rect x="308.5" y="234.8" width="34" height="35.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="325.5" y="229.8" font-size="12" text-anchor="middle">96</text>
<text x="287.5" y="288.0" font-size="12" text-anchor="middle">ISR 중첩 1</text>
<rect x="377.5" y="102.8" width="34" height="167.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="394.5" y="97.8" font-size="12" text-anchor="middle">456</text>
<rect x="415.5" y="184.9" width="34" height="85.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="432.5" y="179.9" font-size="12" text-anchor="middle">232</text>
<rect x="453.5" y="187.9" width="34" height="82.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="470.5" y="182.9" font-size="12" text-anchor="middle">224</text>
<text x="432.5" y="288.0" font-size="12" text-anchor="middle">ISR 중첩 2</text>
<rect x="522.5" y="55.9" width="34" height="214.1" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="539.5" y="50.9" font-size="12" text-anchor="middle">584</text>
<rect x="560.5" y="184.9" width="34" height="85.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="577.5" y="179.9" font-size="12" text-anchor="middle">232</text>
<rect x="598.5" y="140.9" width="34" height="129.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="615.5" y="135.9" font-size="12" text-anchor="middle">352</text>
<text x="577.5" y="288.0" font-size="12" text-anchor="middle">ISR 중첩 3</text>
<rect x="90" y="302" width="14" height="14" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="110.0" y="314.0" font-size="12">(A) 태스크 스택, 모두 MSP</text>
<rect x="285" y="302" width="14" height="14" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="305.0" y="314.0" font-size="12">(B) 태스크 스택 (PSP)</text>
<rect x="480" y="302" width="14" height="14" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="500.0" y="314.0" font-size="12">(B) ISR 스택 (MSP)</text>
</svg>
```

그림 5 — 예제 9의 실측. 주황 (A)는 ISR 중첩 깊이에 비례해 태스크 스택이 커지고, 파랑 (B)는 232바이트에서 멈춘다. 대신 초록 (B)의 MSP가 ISR 몫을 혼자 진다.

### 5.4 예제 10 — 태스크가 N개면? (Python 계산)

무엇을 확인하나: 예제 9의 숫자로 "모든 태스크 스택을 같은 크기로 잡는다"고 할 때 스택 RAM 총량을 계산한다. 태스크 스택 크기는 "어느 태스크가 돌 때 인터럽트가 와도 견디는" 최악값으로 잡아야 하므로 (A)는 N × 584, (B)는 N × 232 + 352다.

```python
# 예제 10: 예제 9의 실측값으로 "태스크 N개일 때 스택 RAM 총량"을 계산한다
depth  = [0, 1, 2, 3]
a_task = [200, 328, 456, 584]          # (A) 모두 MSP: 태스크 스택에 ISR까지
b_task = [200, 232, 232, 232]          # (B) 태스크=PSP
b_msp  = [0, 96, 224, 352]             # (B) ISR=MSP
per_level = [a_task[i+1] - a_task[i] for i in range(3)]
print('ISR 한 단계가 (A) 태스크 스택에 더하는 양:', per_level, 'B')
print('(B) 태스크 스택 증가: depth0→1', b_task[1] - b_task[0], 'B, 그 뒤 0')
d = 3
print(f'\nISR 중첩 {d}단계 기준 스택 RAM 총량 (태스크 스택은 모두 같은 크기로 잡는다고 가정)')
print(' N   (A) N×a      (B) N×b + msp   절약')
rows = []
for n in range(1, 11):
    A = n * a_task[d]; Bt = n * b_task[d] + b_msp[d]
    rows.append((n, A, Bt)); print(f'{n:2d}   {A:6d}       {Bt:6d}         {A - Bt:5d} ({100*(A-Bt)/A:4.1f}%)')
# 실무 규모 예: ISR 중첩 4단계, 단계당 FPU 확장 프레임 104 B + 지역 변수 200 B, 태스크 본문 1 KB, 태스크 12개
lvl = 104 + 200; body = 1024; n = 12; levels = 4
A = n * (body + levels * lvl); Bt = n * (body + 104) + levels * lvl
print(f'\n드론 FC 규모 가정(태스크 {n}, 중첩 {levels}, 단계 {lvl} B, 본문 {body} B): (A) {A} B  (B) {Bt} B  절약 {A-Bt} B')
```

```text
ISR 한 단계가 (A) 태스크 스택에 더하는 양: [128, 128, 128] B
(B) 태스크 스택 증가: depth0→1 32 B, 그 뒤 0

ISR 중첩 3단계 기준 스택 RAM 총량 (태스크 스택은 모두 같은 크기로 잡는다고 가정)
 N   (A) N×a      (B) N×b + msp   절약
 1      584          584             0 ( 0.0%)
 2     1168          816           352 (30.1%)
 3     1752         1048           704 (40.2%)
 4     2336         1280          1056 (45.2%)
 5     2920         1512          1408 (48.2%)
 6     3504         1744          1760 (50.2%)
 7     4088         1976          2112 (51.7%)
 8     4672         2208          2464 (52.7%)
 9     5256         2440          2816 (53.6%)
10     5840         2672          3168 (54.2%)

드론 FC 규모 가정(태스크 12, 중첩 4, 단계 304 B, 본문 1024 B): (A) 26880 B  (B) 14752 B  절약 12128 B
```

(차트 좌표를 만드는 나머지 코드는 생략했다. 같은 스크립트가 그림 5 · 6의 SVG를 만들었다.)

출력에서 볼 것: 태스크가 하나면 차이가 없다(그 태스크 스택이 곧 ISR 스택). 태스크가 늘수록 (B)의 절약이 (N − 1) × 352바이트로 커져서 10개면 54%다. 마지막 줄은 실무 규모 가정이다: FPU가 있는 코어(M4F/M7)에서 ISR이 FP를 쓰면 예외 프레임이 104바이트로 커지고(§7에서 측정), ISR 지역 변수 200바이트, 중첩 4단계, 태스크 12개라면 **약 12 KB**가 갈린다. 128~512 KB SRAM MCU에서 무시할 수 없는 숫자다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="70.0" y1="260.0" x2="630.0" y2="260.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="70.0" y1="260.0" x2="70.0" y2="50.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="66.0" y1="260.0" x2="70.0" y2="260.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="66.0" y1="225.0" x2="70.0" y2="225.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="229.0" font-size="12" text-anchor="end">1000</text>
<line x1="70.0" y1="225.0" x2="630.0" y2="225.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="190.0" x2="70.0" y2="190.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="194.0" font-size="12" text-anchor="end">2000</text>
<line x1="70.0" y1="190.0" x2="630.0" y2="190.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="155.0" x2="70.0" y2="155.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="159.0" font-size="12" text-anchor="end">3000</text>
<line x1="70.0" y1="155.0" x2="630.0" y2="155.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="120.0" x2="70.0" y2="120.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="124.0" font-size="12" text-anchor="end">4000</text>
<line x1="70.0" y1="120.0" x2="630.0" y2="120.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="85.0" x2="70.0" y2="85.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="89.0" font-size="12" text-anchor="end">5000</text>
<line x1="70.0" y1="85.0" x2="630.0" y2="85.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="66.0" y1="50.0" x2="70.0" y2="50.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="54.0" font-size="12" text-anchor="end">6000</text>
<line x1="70.0" y1="50.0" x2="630.0" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="5,3"/>
<line x1="70.0" y1="260.0" x2="70.0" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="70.0" y="278.0" font-size="12" text-anchor="middle">1</text>
<line x1="132.2" y1="260.0" x2="132.2" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="132.2" y="278.0" font-size="12" text-anchor="middle">2</text>
<line x1="194.4" y1="260.0" x2="194.4" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="194.4" y="278.0" font-size="12" text-anchor="middle">3</text>
<line x1="256.7" y1="260.0" x2="256.7" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="256.7" y="278.0" font-size="12" text-anchor="middle">4</text>
<line x1="318.9" y1="260.0" x2="318.9" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="318.9" y="278.0" font-size="12" text-anchor="middle">5</text>
<line x1="381.1" y1="260.0" x2="381.1" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="381.1" y="278.0" font-size="12" text-anchor="middle">6</text>
<line x1="443.3" y1="260.0" x2="443.3" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="443.3" y="278.0" font-size="12" text-anchor="middle">7</text>
<line x1="505.6" y1="260.0" x2="505.6" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="505.6" y="278.0" font-size="12" text-anchor="middle">8</text>
<line x1="567.8" y1="260.0" x2="567.8" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="567.8" y="278.0" font-size="12" text-anchor="middle">9</text>
<line x1="630.0" y1="260.0" x2="630.0" y2="264.0" stroke="currentColor" stroke-width="1.2"/>
<text x="630.0" y="278.0" font-size="12" text-anchor="middle">10</text>
<text x="350.0" y="296.0" font-size="12" text-anchor="middle">태스크 수 N</text>
<text x="20.0" y="24.0" font-size="12">스택 RAM 총량 (바이트), ISR 중첩 3단계</text>
<polyline points="70.0,239.6 132.2,219.1 194.4,198.7 256.7,178.2 318.9,157.8 381.1,137.4 443.3,116.9 505.6,96.5 567.8,76.0 630.0,55.6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="239.6" r="3" fill="#e08a3c"/>
<circle cx="132.2" cy="219.1" r="3" fill="#e08a3c"/>
<circle cx="194.4" cy="198.7" r="3" fill="#e08a3c"/>
<circle cx="256.7" cy="178.2" r="3" fill="#e08a3c"/>
<circle cx="318.9" cy="157.8" r="3" fill="#e08a3c"/>
<circle cx="381.1" cy="137.4" r="3" fill="#e08a3c"/>
<circle cx="443.3" cy="116.9" r="3" fill="#e08a3c"/>
<circle cx="505.6" cy="96.5" r="3" fill="#e08a3c"/>
<circle cx="567.8" cy="76.0" r="3" fill="#e08a3c"/>
<circle cx="630.0" cy="55.6" r="3" fill="#e08a3c"/>
<polyline points="70.0,239.6 132.2,231.4 194.4,223.3 256.7,215.2 318.9,207.1 381.1,199.0 443.3,190.8 505.6,182.7 567.8,174.6 630.0,166.5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="239.6" r="3" fill="#4a7bd0"/>
<circle cx="132.2" cy="231.4" r="3" fill="#4a7bd0"/>
<circle cx="194.4" cy="223.3" r="3" fill="#4a7bd0"/>
<circle cx="256.7" cy="215.2" r="3" fill="#4a7bd0"/>
<circle cx="318.9" cy="207.1" r="3" fill="#4a7bd0"/>
<circle cx="381.1" cy="199.0" r="3" fill="#4a7bd0"/>
<circle cx="443.3" cy="190.8" r="3" fill="#4a7bd0"/>
<circle cx="505.6" cy="182.7" r="3" fill="#4a7bd0"/>
<circle cx="567.8" cy="174.6" r="3" fill="#4a7bd0"/>
<circle cx="630.0" cy="166.5" r="3" fill="#4a7bd0"/>
<text x="626.0" y="45.6" font-size="12" text-anchor="end">(A) 5840 B</text>
<text x="626.0" y="156.5" font-size="12" text-anchor="end">(B) 2672 B</text>
<text x="256.7" y="60.0" font-size="12">(A) 모두 MSP: N × 584</text>
<text x="256.7" y="78.0" font-size="12">(B) PSP + MSP: N × 232 + 352</text>
</svg>
```

그림 6 — 태스크 수에 따른 스택 RAM 총량(예제 9 실측, ISR 중첩 3단계). 두 직선의 기울기 차이(584 − 232 = 352바이트/태스크)가 "ISR 몫을 태스크마다 내느냐, 한 번만 내느냐"다.

### 5.5 이 결과가 RAM 배치에서 어떻게 보이나

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20.0" y="20.0" font-size="13" font-weight="bold">RAM 0x20000000 – 0x20400000 (FreeRTOS 예제 12 기준)</text>
<rect x="40.0" y="36.0" width="220.0" height="70.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="150.0" y="57.5" font-size="13" text-anchor="middle" font-weight="bold">MSP 영역 4 KB</text>
<text x="150.0" y="75.5" font-size="12" text-anchor="middle">ISR · 예외 핸들러 스택</text>
<text x="150.0" y="93.5" font-size="12" text-anchor="middle">(스케줄러 전에는 main)</text>
<text x="268.0" y="44.0" font-size="12">0x20400000 = _estack = vector[0]</text>
<rect x="40.0" y="116.0" width="220.0" height="40.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,3"/>
<text x="150.0" y="140.6" font-size="13" text-anchor="middle">(빈 RAM)</text>
<rect x="40.0" y="166.0" width="220.0" height="120.0" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="48.0" y="184.0" font-size="12" font-weight="bold">ucHeap 128 KB (.bss)</text>
<rect x="50.0" y="194.0" width="200.0" height="38.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="208.6" font-size="13" text-anchor="middle" font-weight="bold">t2 스택 (PSP)</text>
<text x="150.0" y="226.6" font-size="12" text-anchor="middle">SP 0x20001860</text>
<rect x="50.0" y="240.0" width="200.0" height="38.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="254.6" font-size="13" text-anchor="middle" font-weight="bold">t1 스택 (PSP)</text>
<text x="150.0" y="272.6" font-size="12" text-anchor="middle">SP 0x200013f0</text>
<rect x="40.0" y="296.0" width="220.0" height="40.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="150.0" y="320.6" font-size="13" text-anchor="middle">.data · .bss · 커널 변수</text>
<text x="268.0" y="334.0" font-size="12">0x20000000</text>
<text x="300.0" y="100.0" font-size="13" font-weight="bold">ISR이 오면:</text>
<text x="300.0" y="122.0" font-size="12">1. 하드웨어가 8워드(32 B)를 지금 태스크의 PSP에</text>
<text x="300.0" y="140.0" font-size="12">2. 핸들러는 MSP로 실행 — 지역 변수 · 중첩 프레임은 MSP에</text>
<text x="300.0" y="158.0" font-size="12">3. 그래서 태스크 스택마다 ISR 몫을 더할 필요가 없다</text>
<line x1="298.0" y1="230.0" x2="262.0" y2="230.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="300.0" y="226.0" font-size="12">첫 프레임 32 B만 여기</text>
<line x1="298.0" y1="70.0" x2="262.0" y2="70.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="300.0" y="66.0" font-size="12">중첩 ISR 프레임 · 지역 변수</text>
</svg>
```

그림 7 — FreeRTOS 예제 13의 실제 주소로 그린 RAM 배치. 태스크 스택은 heap(ucHeap) 안에, MSP는 RAM 꼭대기 4 KB(lab 링커 스크립트의 `_Main_Stack_Size`)에 있다. ISR이 오면 첫 프레임만 태스크 스택에, 나머지는 MSP에 쌓인다.

실무 규칙으로 정리하면:

- **태스크 스택 크기** = 그 태스크 자신의 최악 깊이 + 32바이트(FPU 프레임이 가능하면 104바이트) + 여유. ISR 몫은 넣지 않는다.
- **MSP 크기** = 가장 깊은 ISR 중첩 경로의 합(프레임 + 지역 변수) + 부팅 코드(`main` 전까지) + 여유. FreeRTOS에서는 링커 스크립트의 메인 스택 크기, Zephyr에서는 `CONFIG_ISR_STACK_SIZE`가 이 값이다(예제 14에서 4096으로 확인).
- MSP가 넘치면 **모든 태스크가 아니라 커널과 ISR 전체**가 망가진다. MSP 끝을 MPU 가드나 Armv8-M의 MSPLIM으로 지키는 이유다(F3).

---

## 6. 예제 11 — gdb로 살아 있는 레지스터 보기

무엇을 확인하나: 디버거는 MRS가 못 보여 주는 것을 보여 준다 — xPSR의 T 비트, 비특권 상태에서의 실제 MSP · PSP 값. 예제 7의 ELF를 QEMU gdbstub(`-s -S`: 1234 포트로 gdb 연결을 받고 첫 명령 전에 멈춤)으로 띄우고 gdb batch 모드로 두 지점에서 레지스터를 본다.

```sh
#!/bin/bash
# ex10: ex7 ELF를 QEMU gdbstub(-s -S)으로 띄우고 gdb batch로 레지스터를 본다
ELF=/var/tmp/rtos/B1/ex7/app.elf
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial file:/var/tmp/rtos/B1/ex7/uart.txt \
  -semihosting-config enable=on,target=native -kernel $ELF -s -S &
QPID=$!
sleep 0.5
gdb -q -batch $ELF \
  -ex "target remote :1234" \
  -ex "break user_task" -ex "break svc_c" \
  -ex "continue" \
  -ex "printf \"--- user_task (Thread, unprivileged, PSP)\\n\"" \
  -ex "info registers sp lr pc xpsr msp psp control primask basepri faultmask" \
  -ex "continue" \
  -ex "printf \"--- svc_c (Handler, SVC)\\n\"" \
  -ex "info registers sp lr pc xpsr msp psp control" \
  -ex "p/x \$xpsr & 0x1ff" -ex "p/x (\$xpsr >> 24) & 1" \
  -ex "x/8wx \$psp" \
  -ex "kill" 2>&1 | grep -v "^\[Inferior\|^Remote debugging\|warning: No executable"
kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
```

```text
Reset_Handler () at /var/rtoslab/templates/common/startup.c:42
42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
Breakpoint 1 at 0xf8: file /Users/donh/.rtos_scratch/B1/ex7.c, line 31.
Breakpoint 2 at 0x158: file /Users/donh/.rtos_scratch/B1/ex7.c, line 17.

Breakpoint 1, user_task () at /Users/donh/.rtos_scratch/B1/ex7.c:31
31	    board_printf_raw("user: CONTROL=0x%x\n", MRS(control));
--- user_task (Thread, unprivileged, PSP)
sp             0x20000460          0x20000460 <__sf>
lr             0xd5                213
pc             0xf8                0xf8 <user_task>
xpsr           0x21000000          553648128
msp            0x203fffe8          541065192
psp            0x20000460          536872032
control        0x3                 3
primask        0x0                 0
basepri        0x0                 0
faultmask      0x0                 0

Breakpoint 2, svc_c (frame=0x20000438 <task_stack+984>, num=2) at /Users/donh/.rtos_scratch/B1/ex7.c:17
17	                     num, MRS(ipsr), MRS(control), frame[0], frame[6]);
--- svc_c (Handler, SVC)
sp             0x203fffe8          0x203fffe8
lr             0xfffffffd          -3
pc             0x158               0x158 <svc_c>
xpsr           0x2100000b          553648139
msp            0x203fffe8          541065192
psp            0x20000438          536871992
control        0x1                 1
$1 = 0xb
$2 = 0x1
0x20000438 <task_stack+984>:	0x00000000	0x20000414	0x00000000	0x000010ae
0x20000448 <task_stack+1000>:	0x20000415	0x00000105	0x00000108	0x61000000
Kill the program being debugged? (y or n) [answered Y; input not from terminal]
```

(QEMU 종료 메시지 한 줄을 지웠다.)

출력에서 볼 것:

- gdb의 `xpsr` = 0x21000000: **T 비트(bit24)가 1**로 보인다. MRS로는 0으로 읽혔던 EPSR을 디버거는 그대로 보여 준다.
- `user_task`에서 `control` = 3(비특권 + PSP), sp = psp = 0x20000460. 비특권 코드가 MRS로 읽으면 0이었던 MSP · PSP 값도 디버거는 다 보여 준다. QEMU gdbstub이 M-profile 시스템 레지스터(msp, psp, primask, basepri, faultmask, control)를 gdb에 내보내기 때문이다. 실칩에서는 J-Link · OpenOCD도 같은 레지스터를 보여 준다.
- `svc_c` 안: xpsr = 0x2100000b → 하위 9비트(IPSR) = **0xb = 11 = SVCall**. sp = msp(Handler 모드), lr = 0xfffffffd, control = 1(SPSEL은 Handler에서 0으로 읽힘, nPRIV는 남아 있음).
- `x/8wx $psp`는 하드웨어가 PSP에 쌓은 **8워드 프레임 그대로**다: R0 = 0(인자), R1, R2, R3, R12, LR = 0x105, PC = **0x108**(SVC 다음 명령 = 예제 7 출력의 stackedPC와 같다), xPSR = 0x61000000(T = 1, IPSR = 0 = Thread에서 왔음). 이 순서(R0 → xPSR)가 B2에서 다룰 예외 프레임 레이아웃이다.

> **실무 팁**: HardFault 덤프를 읽을 때 순서는 이렇다. (1) LR(EXC_RETURN)의 bit2로 MSP/PSP 중 어느 쪽에 프레임이 있는지 고른다. (2) 그 스택에서 8워드를 읽어 PC · LR · xPSR을 얻는다. (3) 쌓인 xPSR의 IPSR이 0이면 Thread(어느 태스크인지는 RTOS의 현재 TCB로), 0이 아니면 그 번호의 ISR 안에서 났다. (4) bit24가 0이면 INVSTATE를 의심한다. 자세한 HardFault 분석 루틴은 M1.

---

## 7. 다른 코어에서는 — Cortex-M4F의 FPCA, 그리고 세대별 차이

### 7.1 예제 12 — mps2-an386(Cortex-M4F)에서 CONTROL.FPCA 보기

무엇을 확인하나: FPU가 있는 코어에서 CONTROL의 세 번째 비트 FPCA가 언제 1이 되는지, 그리고 그때 예외 프레임 크기와 EXC_RETURN이 어떻게 바뀌는지 본다. lab 템플릿은 M3 전용이므로 `templates/common`을 내 폴더(`/var/tmp/rtos/B1/m4`)로 복사하고 `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`로 직접 빌드했다. AN386은 AN385와 메모리 맵 · UART · 타이머 주소가 같아서 startup · board · 링커 스크립트는 고칠 필요가 없었다.

```c
/* ex11 (mps2-an386, Cortex-M4F): CONTROL.FPCA — FP 명령을 쓰는 순간 1이 되고, 예외 프레임이 커진다 */
#include "board.h"
#include "regs.h"
#define CPACR      REG32(0xE000ED88u)
#define FPCCR      REG32(0xE000EF34u)
#define NVIC_ISPR0 REG32(0xE000E200u)
static volatile float f = 1.5f;
static volatile uint32_t isr_lr, isr_sp, isr_ctrl;

void TIMER0_IRQHandler(void)
{
    __asm volatile("mov %0, lr" : "=r"(isr_lr));
    isr_sp = get_sp(); isr_ctrl = MRS(control);
}
static uint32_t irq_frame(void)            /* IRQ를 한 번 걸고 ISR 안의 SP 변화를 잰다 */
{
    uint32_t sp0 = get_sp();
    NVIC_ISPR0 = 1u << 8; __asm volatile("dsb\n isb");
    return sp0 - isr_sp;
}

int main(void)
{
    NVIC_ISER0 = 1u << 8;
    board_printf_raw("CPUID=0x%x FPCCR=0x%x CONTROL=0x%x\n", REG32(0xE000ED00u), FPCCR, MRS(control));
    uint32_t d0 = irq_frame();
    board_printf_raw("IRQ (no FP yet): LR=0x%x CONTROL in ISR=0x%x SP drop=%u\n", isr_lr, isr_ctrl, d0);
    CPACR |= 0xFu << 20; __asm volatile("dsb\n isb");   /* CP10/CP11 full access = FPU on */
    board_printf_raw("FPU on, CONTROL=0x%x\n", MRS(control));
    f = f * 2.0f;                                       /* 첫 FP 명령 */
    board_printf_raw("after vmul: CONTROL=0x%x (FPCA=bit2)\n", MRS(control));
    uint32_t d1 = irq_frame();
    board_printf_raw("IRQ (FP active): LR=0x%x CONTROL in ISR=0x%x SP drop=%u\n", isr_lr, isr_ctrl, d1);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'D=/var/tmp/rtos/B1/m4; cd $D && arm-none-eabi-gcc -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -g -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections -I/Users/donh/.rtos_scratch/B1 -I$D /Users/donh/.rtos_scratch/B1/ex11.c $D/startup.c $D/board.c -T $D/mps2_m3.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -o ex11.elf && arm-none-eabi-size ex11.elf && timeout 10 qemu-system-arm -machine mps2-an386 -cpu cortex-m4 -nographic -monitor none -serial stdio -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -kernel ex11.elf; echo exit=$?'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4616	    100	    340	   5056	   13c0	ex11.elf
CPUID=0x410fc240 FPCCR=0xc0000000 CONTROL=0x0
IRQ (no FP yet): LR=0xfffffff9 CONTROL in ISR=0x0 SP drop=32
FPU on, CONTROL=0x0
after vmul: CONTROL=0x4 (FPCA=bit2)
IRQ (FP active): LR=0xffffffe9 CONTROL in ISR=0x0 SP drop=104
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- CPUID 0x410FC240 = 파트 번호 0xC24 = **Cortex-M4**. FPCCR = 0xC0000000 = ASPEN(FP 명령을 쓰면 FPCA를 자동으로 세움) + LSPEN(lazy stacking) 기본값.
- FPU를 켜는 것(CPACR)만으로는 FPCA가 바뀌지 않는다. **첫 FP 명령(`vmul`)을 실행한 순간 CONTROL = 0x4**(FPCA = 1)가 됐다.
- FPCA = 1인 상태에서 IRQ가 오자 EXC_RETURN이 **0xFFFFFFE9**(bit4 = 0: "확장 프레임")로 바뀌고 스택이 **104바이트**(26워드 = 기본 8 + S0–S15 16 + FPSCR 1 + 예약 1) 내려갔다. FP를 쓰기 전에는 32바이트였다. 즉 **FP를 한 번이라도 쓴 태스크는 인터럽트마다 PSP에 104바이트를 예약**당한다. §5의 "태스크 스택 = 자기 깊이 + 32"가 FPU 칩에서는 "+ 104"가 되는 이유다.
- ISR 안의 CONTROL은 0이다. 핸들러는 FP 문맥이 없는 상태로 시작한다(QEMU 실행 결과).
- QEMU는 lazy stacking의 **공간 예약**은 모델링하지만 실제 저장 시점의 사이클 차이는 보여 줄 수 없다. lazy stacking과 FPU 문맥 비용은 B6이 주인이다.

### 7.2 세대별 차이 — 프로그래머 모델에서 달라지는 것만

이 표는 Arm 아키텍처 문서 기준으로 정리했다. M3/M4F 행만 이 노트에서 실행으로 확인했고 나머지는 실행하지 않았다(세대 지도 전체는 B7).

| 코어 (아키텍처) | 비특권 Thread | BASEPRI · FAULTMASK | CONTROL 비트 | 스택 한계 레지스터 | 비고 |
|---|---|---|---|---|---|
| M0 (Armv6-M) | 없음 (항상 특권) | 없음 (PRIMASK만) | SPSEL만 | 없음 | IT 블록 · 많은 32비트 명령 없음 |
| M0+ (Armv6-M) | 선택 구현 | 없음 | nPRIV(구현 시) · SPSEL | 없음 | MPU 선택 |
| M3 (Armv7-M) | 있음 | 있음 | nPRIV · SPSEL | 없음 | 이 노트의 기준 (실행 확인) |
| M4F / M7 (Armv7E-M) | 있음 | 있음 | nPRIV · SPSEL · FPCA | 없음 | DSP · FPU, 26워드 프레임 (예제 12) |
| M23 (Armv8-M Baseline) | 있음 | 없음 | nPRIV · SPSEL | MSPLIM · PSPLIM (Secure 쪽) | TrustZone 선택 |
| M33 / M55 / M85 (Armv8-M Mainline, v8.1-M) | 있음 | 있음 | nPRIV · SPSEL · FPCA · SFPA 등 | MSPLIM · PSPLIM | Secure/Non-secure마다 MSP · PSP가 따로 (4개) |

RTOS 포팅에서 이 표가 바로 드러난다. FreeRTOS `ARM_CM0` 포트는 BASEPRI가 없어서 크리티컬 섹션을 PRIMASK(`cpsid i`)로 구현한다. `ARM_CM33` 포트는 태스크마다 PSPLIM을 저장 · 복원해서 하드웨어 스택 넘침 검사를 한다. Zephyr도 `CONFIG_BUILTIN_STACK_GUARD`로 PSPLIM을 쓴다(소스 `arch/arm/core/cortex_m/thread.c`의 `__set_PSPLIM`).

---
## 8. RTOS 커널 안에서는 무슨 일이 일어나나

지금까지 손으로 만든 장치(`run_task`, 위조 프레임 + EXC_RETURN 0xFFFFFFFD, SVC 시스템 콜)는 RTOS 포트 코드가 하는 일의 축소판이다. 실제 커널에서 확인한다.

### 8.1 FreeRTOS ARM_CM3 포트 — CONTROL을 한 번도 쓰지 않는다

`portable/GCC/ARM_CM3/port.c`에서 CONTROL을 검색하면 주석 한 줄만 나온다. 그런데 태스크는 PSP 위에서 돈다. 어떻게?

1. `vTaskStartScheduler()` → `xPortStartScheduler()`: PendSV와 SysTick을 최저 우선순위로 설정(`portNVIC_SHPR3_REG`)하고 SysTick을 켠 뒤 `prvPortStartFirstTask()`를 부른다.
2. `prvPortStartFirstTask()`: VTOR(0xE000ED08)에서 벡터 테이블 주소를 읽고, 그 0번 워드(초기 스택 꼭대기)를 **`msr msp`로 다시 넣는다**. `main()`이 쓰던 MSP 스택은 이 순간 버려지고 ISR 전용 스택이 된다. 그다음 `cpsie i`, `cpsie f`, `dsb`, `isb`, `svc 0`.
3. `vPortSVCHandler()`(Handler 모드, MSP): `pxCurrentTCB`의 첫 워드(저장된 SP)를 읽어 R4–R11을 `ldmia`로 꺼내고 **`msr psp, r0`**, `isb`, BASEPRI = 0, 그리고 **`orr r14, #0xd`**로 LR을 0xFFFFFFF9에서 **0xFFFFFFFD**로 바꿔 `bx r14`.
4. 하드웨어가 PSP에서 위조 8워드 프레임을 꺼내며 Thread 모드로 복귀하고, EXC_RETURN의 bit2 때문에 **CONTROL.SPSEL = 1**이 된다. 첫 태스크는 Thread · 특권 · PSP로 시작한다.

예제 4 CASE 2에서 한 것과 같은 원리다. 이후 컨텍스트 스위치는 `xPortPendSVHandler()`가 `mrs r0, psp` → R4–R11 저장 → `vTaskSwitchContext` → 다음 TCB에서 복원 → `msr psp, r0`로 한다(C2). 비특권 태스크가 필요하면 `ARM_CM3_MPU` 포트를 쓴다. 그 포트는 태스크마다 CONTROL 값을 문맥에 같이 저장하고(`portINITIAL_CONTROL_IF_UNPRIVILEGED` = 0x03, `..._IF_PRIVILEGED` = 0x02), `vResetPrivilege()` · `xIsPrivileged()` · SVC 기반 시스템 콜 진입(`vSystemCallEnter`)으로 nPRIV를 바꾼다(F4).

### 8.2 예제 13 — FreeRTOS 태스크는 정말 Thread · 특권 · PSP인가

무엇을 확인하나: lab의 FreeRTOS 템플릿에서 태스크 두 개가 각자 CONTROL · SP · PSP · MSP를 찍고, 각 태스크 안에서 IRQ8을 일으켜 ISR이 어느 스택을 쓰는지 본다.

```c
/* ex12: FreeRTOS(ARM_CM3 포트)에서 태스크는 어떤 모드 · 스택에서 도나 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "regs.h"
#define NVIC_ISPR0 REG32(0xE000E200u)
extern uint32_t _estack;
static volatile uint32_t isr_lr, isr_sp, isr_msp, isr_ipsr;

void TIMER0_IRQHandler(void)
{
    __asm volatile("mov %0, lr" : "=r"(isr_lr));
    isr_sp = get_sp(); isr_msp = MRS(msp); isr_ipsr = MRS(ipsr);
}

static void task(void *arg)
{
    board_printf_raw("[%s] CONTROL=0x%x IPSR=%u SP=0x%x PSP=0x%x MSP=0x%x\n", (const char *)arg,
                     MRS(control), MRS(ipsr), get_sp(), MRS(psp), MRS(msp));
    NVIC_ISPR0 = 1u << 8; __asm volatile("dsb\n isb");
    board_printf_raw("[%s] ISR: IPSR=%u LR=0x%x SP=0x%x (=MSP 0x%x)\n", (const char *)arg,
                     isr_ipsr, isr_lr, isr_sp, isr_msp);
    if (arg && ((const char *)arg)[1] == '2') board_exit(0);
    vTaskDelay(1);
    for (;;) vTaskDelay(1000);
}

int main(void)
{
    NVIC_IPR(8) = 0xFF; NVIC_ISER0 = 1u << 8;
    board_printf_raw("main: CONTROL=0x%x SP=0x%x _estack=0x%x\n", MRS(control), get_sp(), (unsigned)&_estack);
    xTaskCreate(task, "t1", 256, "t1", 2, NULL);
    xTaskCreate(task, "t2", 256, "t2", 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/B1/ex12.c OUT=/var/tmp/rtos/B1/ex12 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B1/ex12/app.elf 10; echo exit=$?'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12040	    100	 135416	 147556	  24064	/var/tmp/rtos/B1/ex12/app.elf
main: CONTROL=0x0 SP=0x203fffe0 _estack=0x20400000
[t1] CONTROL=0x2 IPSR=0 SP=0x200013f0 PSP=0x200013f0 MSP=0x203fffe0
[t1] ISR: IPSR=24 LR=0xfffffffd SP=0x203fffe0 (=MSP 0x203fffe0)
[t2] CONTROL=0x2 IPSR=0 SP=0x20001860 PSP=0x20001860 MSP=0x203fffe0
[t2] ISR: IPSR=24 LR=0xfffffffd SP=0x203fffe0 (=MSP 0x203fffe0)
[board_exit] code=0
exit=0
```

출력에서 볼 것:

- 두 태스크 모두 **CONTROL = 0x2**(특권 + PSP), SP = PSP, 그리고 PSP 값이 서로 다르다(t1 0x200013f0, t2 0x20001860 — 둘 다 heap 안). "태스크 = 자기 스택"이 PSP 값 하나로 구현된 것이다.
- MSP = **0x203fffe0**: `prvPortStartFirstTask`가 MSP를 `_estack`(0x20400000)으로 되돌린 뒤 `svc 0`이 그 위에 8워드(32바이트)를 쌓았고, `vPortSVCHandler`가 PSP 쪽으로 복귀했으므로 MSP는 그 자리에 남았다. `main`에서 찍은 SP도 우연히 같은 값이지만 의미가 다르다 — `main`의 프레임은 이미 버려졌다(그래서 `main`의 지역 변수를 태스크에 포인터로 넘기면 안 된다, C3).
- ISR은 LR = 0xFFFFFFFD(태스크 = PSP에서 왔다), SP = MSP. 어느 태스크가 돌던 중이든 ISR은 같은 MSP 위에서 돈다.

### 8.3 Zephyr v4.4.2 — 부팅부터 PSP, ISR 스택은 따로 할당

Zephyr의 Cortex-M 코드(`arch/arm/core/cortex_m/`)는 FreeRTOS와 방향이 같지만 몇 가지가 다르다.

- `reset.S`: 부팅 초기에 **PSP를 인터럽트 스택(`z_interrupt_stacks`) 꼭대기로 맞추고 CONTROL.SPSEL = 1**로 바꾼 뒤 C 초기화를 PSP 위에서 한다(소스 주석: "Set PSP and use it to boot without using MSP"). `msr CONTROL` 바로 뒤에 ISB 주석도 있다.
- `arch_kernel_init()`(`arch/arm/include/cortex_m/kernel_arch_func.h`) → `z_arm_interrupt_stack_setup()`(`arch/arm/include/cortex_m/stack.h`): **MSP = `z_interrupt_stacks` 꼭대기**. 크기는 `CONFIG_ISR_STACK_SIZE`(커널 기본 2048, mps2 보드 defconfig는 4096). Armv8-M이면 MSPLIM도 설정한다.
- `arch_switch_to_main_thread()`(`thread.c`): main 스레드의 스택으로 PSP를 맞추고 main으로 들어간다.
- `z_arm_pendsv`(`swap_helper.S`): `mrs ip, PSP`로 현재 스레드 문맥 저장 → 다음 스레드 복원. `CONFIG_USERSPACE`면 스레드별 `mode` 값으로 **CONTROL.nPRIV를 복원**한다. (v4.4에는 새 `CONFIG_USE_SWITCH` 경로 `arm-m-switch.c`도 있지만 Cortex-M 기본은 이 PendSV 경로다.)
- 사용자 모드: `k_thread_user_mode_enter()` → `arch_user_mode_enter()` → `z_arm_userspace_enter`가 nPRIV = 1로 내려간다. 시스템 콜은 `svc.S`의 `z_arm_svc` → `z_arm_do_syscall`. 현재 모드 확인은 `z_arm_thread_is_in_user_mode()`가 `__get_CONTROL() & CONTROL_nPRIV_Msk`로 한다.

### 8.4 예제 14 — Zephyr에서 같은 것을 확인

무엇을 확인하나: Zephyr `mps2/an385` 빌드에서 main 스레드, 일반 스레드, 타이머 콜백(SysTick ISR 안에서 실행)의 CONTROL · IPSR · SP · PSP · MSP를 본다. `prj.conf`는 `CONFIG_PRINTK=y` 한 줄이다.

```c
/* ex13: Zephyr(mps2/an385)에서 스레드와 타이머 ISR이 어느 모드 · 스택에서 도나 */
#include <zephyr/kernel.h>
#include <cmsis_core.h>

static void show(const char *who)
{
    uint32_t sp; __asm volatile("mov %0, sp" : "=r"(sp));
    printk("%-8s CONTROL=0x%x IPSR=%u SP=0x%08x PSP=0x%08x MSP=0x%08x\n", who,
           __get_CONTROL(), __get_IPSR(), sp, __get_PSP(), __get_MSP());
}
static void tmr_fn(struct k_timer *t) { ARG_UNUSED(t); show("timerISR"); }
K_TIMER_DEFINE(tmr, tmr_fn, NULL);

static void worker(void *a, void *b, void *c) { ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c); show("worker"); }
K_THREAD_DEFINE(wk, 1024, worker, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    extern char z_interrupt_stacks[];
    printk("z_interrupt_stacks=%p ISR_STACK_SIZE=%d\n", (void *)z_interrupt_stacks, CONFIG_ISR_STACK_SIZE);
    show("main");
    k_timer_start(&tmr, K_MSEC(1), K_NO_WAIT);
    k_msleep(5);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B1/zapp /var/tmp/rtos/B1/zbuild && /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/B1/zbuild 5'
```

```text
           FLASH:       13720 B         4 MB      0.33%
             RAM:        7320 B         4 MB      0.17%
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
z_interrupt_stacks=0x20000798 ISR_STACK_SIZE=4096
main     CONTROL=0x2 IPSR=0 SP=0x20001c38 PSP=0x20001c38 MSP=0x20001798
worker   CONTROL=0x2 IPSR=0 SP=0x20000770 PSP=0x20000770 MSP=0x20001798
timerISR CONTROL=0x0 IPSR=15 SP=0x20001738 PSP=0x20001868 MSP=0x20001738
```

(빌드 로그는 마지막 두 줄만 남겼다. 빌드 경고 0개.)

출력에서 볼 것:

- main 스레드와 worker 모두 CONTROL = 0x2(특권 + PSP). FreeRTOS와 같다.
- MSP = 0x20001798 = `z_interrupt_stacks`(0x20000798) + 4096. `z_arm_interrupt_stack_setup()`이 맞춘 값 그대로다. FreeRTOS처럼 "RAM 꼭대기의 링커 스택"이 아니라 **커널이 크기를 정해 할당한 배열**이 ISR 스택이다.
- 타이머 콜백은 IPSR = **15**(SysTick 핸들러 안), SP = MSP, CONTROL = 0. Zephyr의 `k_timer` 만료 함수는 ISR 문맥에서 돈다는 사실도 여기서 보인다(E 모듈).

```svg
<svg viewBox="0 0 680 334" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20.0" y="20.0" font-size="13" font-weight="bold">FreeRTOS ARM_CM3 포트 (port.c)</text>
<rect x="20.0" y="34.0" width="116.0" height="52.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="78.0" y="55.5" font-size="13" text-anchor="middle" font-weight="bold">main()</text>
<text x="78.0" y="73.5" font-size="12" text-anchor="middle">Thread 특권 MSP</text>
<rect x="150.0" y="34.0" width="116.0" height="52.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="208.0" y="55.5" font-size="13" text-anchor="middle" font-weight="bold">xPortStart</text>
<text x="208.0" y="73.5" font-size="12" text-anchor="middle">Scheduler()</text>
<rect x="280.0" y="34.0" width="116.0" height="52.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="338.0" y="55.5" font-size="13" text-anchor="middle" font-weight="bold">prvPortStart</text>
<text x="338.0" y="73.5" font-size="12" text-anchor="middle">FirstTask()</text>
<rect x="410.0" y="34.0" width="116.0" height="52.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="468.0" y="55.5" font-size="13" text-anchor="middle" font-weight="bold">vPortSVC</text>
<text x="468.0" y="73.5" font-size="12" text-anchor="middle">Handler()</text>
<rect x="540.0" y="34.0" width="116.0" height="52.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="598.0" y="55.5" font-size="13" text-anchor="middle" font-weight="bold">첫 태스크</text>
<text x="598.0" y="73.5" font-size="12" text-anchor="middle">Thread 특권 PSP</text>
<line x1="137.0" y1="60.0" x2="149.0" y2="60.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<line x1="267.0" y1="60.0" x2="279.0" y2="60.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<line x1="397.0" y1="60.0" x2="409.0" y2="60.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<line x1="527.0" y1="60.0" x2="539.0" y2="60.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<text x="280.0" y="106.0" font-size="12">MSP ← vector[0], cpsie, svc 0</text>
<text x="410.0" y="124.0" font-size="12">PSP ← TCB, LR = 0xFFFFFFFD</text>
<text x="20.0" y="150.0" font-size="12">CONTROL은 한 번도 쓰지 않는다 — EXC_RETURN이 SPSEL=1을 만든다</text>
<text x="20.0" y="196.0" font-size="13" font-weight="bold">Zephyr v4.4.2 (arch/arm/core/cortex_m)</text>
<rect x="20.0" y="210.0" width="116.0" height="52.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="78.0" y="231.6" font-size="13" text-anchor="middle" font-weight="bold">reset.S</text>
<text x="78.0" y="249.6" font-size="12" text-anchor="middle">PSP 부팅</text>
<rect x="150.0" y="210.0" width="116.0" height="52.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="208.0" y="231.6" font-size="13" text-anchor="middle" font-weight="bold">arch_kernel</text>
<text x="208.0" y="249.6" font-size="12" text-anchor="middle">_init()</text>
<rect x="280.0" y="210.0" width="116.0" height="52.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="338.0" y="231.6" font-size="13" text-anchor="middle" font-weight="bold">arch_switch_to</text>
<text x="338.0" y="249.6" font-size="12" text-anchor="middle">_main_thread()</text>
<rect x="410.0" y="210.0" width="116.0" height="52.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="468.0" y="231.6" font-size="13" text-anchor="middle" font-weight="bold">z_arm_pendsv</text>
<text x="468.0" y="249.6" font-size="12" text-anchor="middle">(swap_helper.S)</text>
<rect x="540.0" y="210.0" width="116.0" height="52.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="598.0" y="231.6" font-size="13" text-anchor="middle" font-weight="bold">user thread</text>
<text x="598.0" y="249.6" font-size="12" text-anchor="middle">nPRIV = 1</text>
<line x1="137.0" y1="236.0" x2="149.0" y2="236.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<line x1="267.0" y1="236.0" x2="279.0" y2="236.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<line x1="397.0" y1="236.0" x2="409.0" y2="236.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<line x1="527.0" y1="236.0" x2="539.0" y2="236.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<text x="20.0" y="284.0" font-size="12">reset.S: PSP = 인터럽트 스택 꼭대기, SPSEL=1 (부팅 코드가 PSP 위에서 돈다)</text>
<text x="20.0" y="302.0" font-size="12">arch_kernel_init → z_arm_interrupt_stack_setup: MSP = z_interrupt_stacks 꼭대기</text>
<text x="20.0" y="320.0" font-size="12">z_arm_pendsv: PSP 교체, USERSPACE면 CONTROL.nPRIV 복원 · z_arm_svc: 시스템 콜</text>
</svg>
```

그림 8 — FreeRTOS와 Zephyr가 CONTROL · MSP · PSP를 만지는 지점. FreeRTOS는 EXC_RETURN으로 SPSEL을 켜고, Zephyr는 reset.S에서 직접 SPSEL을 켠다. 둘 다 결국 "스레드 = PSP, ISR = MSP"로 수렴한다. 어셈블리 한 줄씩은 C2(PendSV), C3(첫 태스크 시작), C8(시스템 콜 진입).

---

## 9. 실무에서 다시 보기

### 9.1 드론 비행 제어기 — MSP 크기를 재서 정한다

예를 들어 드론 FC(STM32H7급, Cortex-M7 + FPU)라면 IMU SPI DMA 완료 ISR, ESC DShot 타이머 ISR, UART 텔레메트리 ISR, CAN ISR이 서로 다른 우선순위로 중첩될 수 있다. 각 ISR이 FP를 쓰면 단계마다 104바이트 프레임이 MSP에 쌓인다(예제 12). 태스크 스택에는 이 몫을 넣지 않는 대신, **MSP를 최악 경로로 재서** 잡아야 한다. 방법은 예제 9 그대로다: 부팅 때 MSP 영역을 패턴으로 채우고(스케줄러 시작 전, 이미 쓰는 영역은 피해서), 비행 테스트 · HIL 스트레스 뒤 high water mark를 텔레메트리로 보낸다. Zephyr라면 `CONFIG_ISR_STACK_SIZE`를, FreeRTOS라면 링커 스크립트의 메인 스택 크기를 그 값 + 여유로 정한다.

### 9.2 로봇 관절 모터 컨트롤러 — 20 kHz FOC ISR과 스택

FOC 전류 루프 ISR(20 kHz)은 FP 연산(Park/Clarke 변환, PI)을 한다. FPCA = 1인 태스크 위에서 이 ISR이 들어오면 PSP에 104바이트가 예약되고, ISR 자신의 FP 레지스터 사용은 lazy stacking으로 처리된다(B6). 이 ISR은 RTOS API를 부르지 않는 "zero-latency" 인터럽트로 두는 경우가 많은데(E2), 그래도 **스택은 RTOS가 정한 MSP를 같이 쓴다**는 점은 같다. CAN 진단 태스크를 비특권으로 돌리면 실수로 NVIC를 건드려도 예제 6처럼 BusFault로 바로 잡힌다 — 현장에서 "모터가 가끔 멈춘다"보다 훨씬 나은 증상이다.

### 9.3 센서 허브 MCU — 서드파티 코드를 비특권으로 가두기

Linux SoC 옆에서 IMU · 타임스탬프를 담당하는 센서 허브에 벤더가 준 센서 퓨전 바이너리 라이브러리를 넣는다고 하자. 그 라이브러리를 비특권 태스크(Zephyr user thread 또는 FreeRTOS-MPU의 restricted task)로 돌리면, CONTROL · BASEPRI · NVIC를 못 건드리고(예제 5 · 6), MPU로 허락한 메모리만 쓸 수 있으며, 특권 작업은 정해진 시스템 콜(SVC)로만 한다(예제 7). 그림 4의 전이 규칙이 이 보호를 하드웨어로 보장한다.

### 9.4 Don의 SSD 컨트롤러 펌웨어와 나란히

SSD 컨트롤러의 Cortex-R 펌웨어에서는 IRQ 모드 스택 크기를 정하고, SYS/SVC 모드에서 실행부(executive)를 돌렸을 것이다. IRQ 모드 스택이 넘쳐서 바로 아래 변수가 깨지는 버그를 한 번쯤 봤다면, 그게 Cortex-M의 MSP 넘침과 똑같은 문제다. 차이는 Cortex-M에서는 (1) 하드웨어가 8워드를 자동 저장하니 IRQ 래퍼 어셈블리가 없고, (2) 태스크(실행부 대신)가 여러 개면 PSP를 바꿔 끼워 각자 스택을 갖게 할 수 있으며, (3) 관리 MCU 같은 작은 코어에서 서드파티 코드를 비특권으로 돌릴 수 있다는 점이다. 면접에서 "Cortex-R에서는 모드별 banked SP로, Cortex-M에서는 MSP/PSP로 같은 문제를 풀었다"고 말하면 두 아키텍처를 다 아는 사람으로 들린다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `msr control` 뒤 `isb` 생략 | 실칩에서만 가끔 이상한 스택 사용, 디버거로는 재현 안 됨 | 파이프라인의 다음 명령이 옛 SP · 옛 특권으로 실행 | CONTROL · MSP를 바꾼 직후 항상 `isb` (CMSIS `__set_CONTROL` 뒤 `__ISB()`) |
| C 함수 중간에 SP(MSP)를 바꿈 | 돌아갈 때 HardFault, 지역 변수 쓰레기 | 컴파일러는 SP가 함수 안에서 안 바뀐다고 가정 | 예제의 `run_task`처럼 naked 어셈블리 트램펄린에서만 바꾼다 |
| 지금 쓰는 스택 아래를 `memset`으로 채움 | HardFault CFSR = 0x8200 (예제 9 작성 중 실제로 겪음) | `memset` 자신이 그 영역에 레지스터를 push | `volatile` 루프, 또는 스택을 쓰기 전에 채운다 |
| bit0이 0인 함수 주소로 점프 | HardFault, CFSR = 0x20000 (INVSTATE), 쌓인 xPSR의 bit24 = 0 | Cortex-M은 Thumb만, T = 0이면 실행 불가 | 함수 포인터는 컴파일러가 만든 값 사용, 링커 심볼이면 `+ 1` (예제 4) |
| 위조 프레임 xPSR을 0으로 둠 | 첫 태스크 시작 즉시 INVSTATE | 예외 복귀가 T = 0을 복원 | xPSR = 0x01000000 (`portINITIAL_XPSR`) |
| 비특권 태스크가 NVIC · SysTick · SCB 접근 | BusFault → HardFault, CFSR = 0x8200, BFAR = 0xE000Exxx | SCS는 특권 전용 (STIR 예외) | 커널 API · SVC 시스템 콜로 대신 하게 한다 (예제 6 · 7) |
| 비특권에서 `cpsid i` · BASEPRI로 크리티컬 섹션 | 증상 없이 가끔 데이터 경쟁 | 비특권의 MSR · CPS는 조용히 무시 | 비특권 태스크는 커널 동기화 객체 사용 (예제 5) |
| 비특권 코드에서 `MRS psp`로 스택 검사 | 남은 스택이 엉터리로 계산됨 | 비특권 MRS SP 읽기는 0 | 스택 검사는 커널(특권) 쪽에서 |
| ISR 안에서 CONTROL.SPSEL로 "어느 스택에서 왔나" 판단 | fault 핸들러가 항상 MSP를 덤프해 엉뚱한 PC | Handler에서 SPSEL은 0으로 읽힘 | EXC_RETURN(LR)의 bit2를 본다 (`tst lr, #4`) |
| 태스크 스택에 ISR 몫까지 넣거나, MSP를 너무 작게 | RAM 낭비, 또는 ISR 중첩 시 커널 변수 손상 | PSP/MSP 분리를 스택 예산에 반영 안 함 | 태스크 = 자기 깊이 + 32 B(FPU면 104 B), MSP = 최악 ISR 경로 (예제 9 · 10) |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Walk me through the Cortex-M programmer's model."

**A.** 범용 레지스터 R0–R12, SP(R13, 실제로는 MSP와 PSP 두 개), LR(R14), PC(R15)가 있다. 특수 레지스터로 xPSR(APSR 플래그, IPSR 예외 번호, EPSR T 비트), 마스크 레지스터 PRIMASK · FAULTMASK · BASEPRI, 그리고 CONTROL(nPRIV, SPSEL, FPU 있으면 FPCA)이 있다. 모드는 Thread와 Handler 두 개, 특권은 Thread에서만 고를 수 있다. reset 직후는 Thread · 특권 · MSP이고, 나는 QEMU에서 이걸 MRS로 다 읽어 확인했다.

> There are thirteen general-purpose registers, R0 to R12, then SP, LR and PC. SP is banked: there's a main stack pointer and a process stack pointer, and which one is "SP" depends on the mode and CONTROL.SPSEL. The special registers are xPSR — which overlays the APSR flags, the IPSR exception number and the EPSR Thumb bit — the three mask registers PRIMASK, FAULTMASK and BASEPRI, and CONTROL, which holds nPRIV, SPSEL and, on FPU parts, FPCA. There are only two modes, Thread and Handler, and Handler is always privileged. Out of reset you're in Thread mode, privileged, on MSP — I verified that by reading every register with MRS on a QEMU Cortex-M3.

**꼬리 질문**: "EPSR를 MRS로 읽으면?" → 항상 0이다. T 비트를 보려면 디버거나 예외 프레임의 쌓인 xPSR을 본다.

**Q.** "What's the difference between Thread and Handler mode, and between privileged and unprivileged? How does unprivileged code get privilege back?"

**A.** 모드는 "예외 처리 중인가"(IPSR ≠ 0이면 Handler), 특권은 "시스템 레지스터를 건드릴 수 있나"다. Handler는 항상 특권이다. 특권 Thread는 CONTROL.nPRIV = 1로 스스로 내려갈 수 있지만, 비특권에서 CONTROL을 쓰면 조용히 무시된다. 돌아가는 길은 예외뿐이고, 의도적으로는 SVC다. SVC 핸들러가 검사한 뒤 nPRIV를 0으로 쓰고 복귀하면 특권으로 돌아온다. 그래서 커널이 검문소가 된다.

> Mode tells you whether you're servicing an exception — IPSR nonzero means Handler mode; privilege tells you whether you can touch system registers like the NVIC, SysTick, the mask registers and CONTROL. Handler mode is always privileged. Privileged thread code can drop privilege by setting CONTROL.nPRIV, but an unprivileged MSR to CONTROL is silently ignored — I tested that, along with BASEPRI and CPSID, and none of them fault, they just do nothing. The only way back up is through an exception, normally an SVC: the handler validates the request, clears nPRIV, and the exception return lands you back in privileged thread mode. That's the hardware basis for FreeRTOS-MPU and Zephyr userspace.

**꼬리 질문**: "그럼 아무 코드나 SVC로 특권을 얻을 수 있지 않나?" → 그래서 FreeRTOS-MPU는 시스템 콜 코드 영역에서 부른 SVC만 특권 상승을 허락하고, Zephyr는 시스템 콜마다 `z_vrfy_` 검증 함수를 둔다.

**Q.** "Why does Cortex-M have two stack pointers, and why does an RTOS put tasks on PSP and interrupts on MSP?"

**A.** 스택이 하나면 ISR이 그때 돌던 태스크의 스택에 쌓이므로 모든 태스크 스택에 최악 ISR 중첩 깊이를 더해야 한다. PSP/MSP로 나누면 하드웨어가 첫 8워드만 태스크 PSP에 쌓고 나머지 ISR 사용량은 MSP 한 곳에 모인다. 내가 잰 숫자로는 ISR 3단계 중첩에서 태스크 스택이 584바이트 → 232바이트로 줄었고, 태스크 10개면 스택 RAM이 54% 줄었다.

> If you only had one stack pointer, an interrupt would push onto whichever task happened to be running, so every task stack would need headroom for the worst-case nested interrupt path. With two, the hardware pushes only the first eight-word frame onto the task's PSP, then switches to MSP for the handler, so all the nested ISR usage lands on one shared interrupt stack. I measured this on QEMU: with three nested ISRs, the per-task stack went from 584 bytes to a constant 232, and the ISR cost moved to MSP once — for ten tasks that's about 54 percent less stack RAM. On an FPU part the per-task reserve becomes 104 bytes instead of 32, but the principle is the same.

**꼬리 질문**: "그럼 MSP 크기는 어떻게 정하나?" → 최악 ISR 중첩 경로(프레임 + 지역 변수) + 부팅 코드 사용량. 패턴 채우기로 high water mark를 재서 확인한다. Zephyr는 `CONFIG_ISR_STACK_SIZE`.

**Q.** "Why is the LSB of a function pointer set on Cortex-M? What happens if it's clear?"

**A.** Arm 아키텍처에서 분기 주소의 bit0은 Thumb 상태 표시인데, Cortex-M은 Thumb만 실행하므로 항상 1이어야 한다. 0인 주소로 BX/BLX하면 EPSR.T가 0이 되고 다음 명령에서 INVSTATE UsageFault가 난다. 예외 복귀로 T = 0인 xPSR을 복원해도 똑같다. 그래서 RTOS 초기 스택 프레임의 xPSR은 0x01000000이다.

> On Arm, bit zero of a branch target selects the Thumb instruction set. Cortex-M only executes Thumb, so that bit must always be one — a function pointer is the instruction address plus one. If you branch to an even address, EPSR.T becomes zero and the very next instruction raises a UsageFault with INVSTATE; CFSR reads 0x20000 and the stacked xPSR shows bit 24 clear. The same happens if an exception return restores an xPSR with T clear, which is why every RTOS seeds a new task's fake frame with xPSR equal to 0x01000000.

**Q.** "Which registers does the hardware save on exception entry, and why only those?"

**A.** R0–R3, R12, LR, PC, xPSR 8워드. AAPCS의 caller-saved 집합에 복귀 정보를 더한 것이다. R4–R11은 callee-saved라서 ISR이 C 함수이면 컴파일러가 보존한다. 그래서 ISR을 평범한 C 함수로 쓸 수 있다. 대신 태스크를 바꿀 때는 R4–R11을 커널(PendSV)이 직접 저장해야 하므로 태스크 문맥은 16워드다.

> Eight words: R0 to R3, R12, LR, PC and xPSR. That's exactly the AAPCS caller-saved set plus the return state. Because R4 to R11 are callee-saved, any handler written as a normal C function will preserve them itself, so no assembly wrapper is needed — the objdump of an IRQ handler looks identical to an ordinary function, ending in pop {r4, pc} where the popped value is EXC_RETURN. The flip side is that a context switch has to save R4 to R11 in software, which is what PendSV does, so a minimal task context is sixteen words.

**Q.** "How does FreeRTOS get the first task running on PSP?"

**A.** CM3 포트는 CONTROL을 직접 쓰지 않는다. `prvPortStartFirstTask`가 MSP를 벡터 테이블 0번 값으로 되돌리고 `svc 0`을 한다. `vPortSVCHandler`가 TCB에서 저장된 SP를 읽어 R4–R11을 꺼내고 PSP에 넣은 뒤, LR을 0xFFFFFFFD로 만들어 복귀한다. 하드웨어가 PSP에서 위조 프레임을 꺼내며 SPSEL = 1이 된다. 태스크 안에서 CONTROL을 읽으면 0x2다.

> The CM3 port never writes CONTROL. prvPortStartFirstTask resets MSP from the first entry of the vector table — discarding main's stack — enables interrupts and executes SVC 0. vPortSVCHandler loads the first task's saved stack pointer from the TCB, pops R4 to R11, writes PSP, and ORs 0xD into LR so that EXC_RETURN becomes 0xFFFFFFFD — return to Thread mode using PSP. The hardware unstacks the fake frame from PSP and sets SPSEL as part of the exception return. I confirmed on QEMU that tasks then read CONTROL as 0x2, privileged on PSP, while ISRs run on MSP with LR equal to 0xFFFFFFFD.

**Q.** "Given a HardFault, how do you tell which stack and which context it came from?"

**A.** 핸들러 진입 시 LR(EXC_RETURN)의 bit2로 MSP/PSP를 고르고(`tst lr, #4`), 그 스택의 8워드에서 PC · LR · xPSR을 읽는다. 쌓인 xPSR의 IPSR이 0이면 Thread(RTOS의 현재 TCB로 태스크를 알아낸다), 아니면 그 번호의 ISR이다. CFSR로 원인(PRECISERR + BFAR, INVSTATE 등)을 본다. CONTROL.SPSEL은 핸들러 안에서 0으로 읽히니 쓰면 안 된다.

> I use EXC_RETURN, not CONTROL — in Handler mode SPSEL always reads zero. Bit 2 of LR on entry says whether the frame is on MSP or PSP, so the handler does tst lr, #4 and picks the right one. From that frame I take the stacked PC, LR and xPSR. The IPSR field of the stacked xPSR tells me whether the fault happened in thread code — then I look at the RTOS's current TCB to name the task — or inside a specific ISR. Then CFSR gives the cause: for example 0x8200 is a precise bus fault with a valid BFAR, which is what you see when an unprivileged task touches the NVIC.

---

## 12. 직접 해보기

1. **개념** — 비특권 Thread 모드 코드가 할 수 있는 일과 없는 일을 각각 세 가지씩 들어 보라. "할 수 없는 일" 중 fault가 나는 것과 조용히 무시되는 것을 구분하라.
   정답/힌트: 할 수 있음 — 일반 메모리 접근(MPU 허용 범위), APSR 쓰기, `svc`. 할 수 없음 — SCS 접근(BusFault, 예제 6), `msr control/basepri`(무시), `cpsid i`(무시), MSP · PSP 읽기(0으로 읽힘, 예제 5).
2. **코드** — 예제 8에서 `run_task(task, ..., 2)`를 `3`으로 바꾸면 ISR의 LR · CONTROL은 어떻게 나오나? 예상하고 돌려서 확인하라.
   정답/힌트: LR은 여전히 0xFFFFFFFD(EXC_RETURN에는 특권 정보가 없다), ISR 안의 CONTROL은 0x1(nPRIV는 남고 SPSEL은 0으로 읽힘). `msr control,2`는 Handler 특권이라 nPRIV를 0으로 바꿀 수 있다는 점도 확인해 보라 — 복귀 후 태스크가 특권이 된다.
3. **측정** — 예제 9의 ISR 지역 배열을 96에서 256바이트로 바꾸고 표를 다시 만들라. (A)의 단계당 증가량과 (B)의 MSP 값이 어떻게 변하는지 식으로 예측한 뒤 확인하라.
   정답/힌트: 단계당 (A) 증가 = 32 + 256 = 288바이트로 예측(ISR 프롤로그 push가 생기면 몇 바이트 더). (B) 태스크는 232 그대로, MSP는 256, 256 + 288, 256 + 2 × 288 근처. 숫자는 직접 재야 한다.
4. **코드** — 예제 7의 `svc #0`(특권 상승)을 "`run_task` 코드 범위 안에서 부른 SVC만 허락"하도록 고쳐라. 쌓인 PC(`frame[6]`)를 무엇과 비교하면 되나?
   정답/힌트: 링커 심볼이나 함수 주소 두 개(시작 · 끝)를 두고 `frame[6]`이 그 사이인지 검사한다. FreeRTOS-MPU의 `__syscalls_flash_start__`/`__syscalls_flash_end__` 검사와 같은 발상이다. 바깥에서 부른 `svc #0`은 무시하거나 태스크를 종료한다.
5. **디버깅** — 현장 덤프: "HardFault, LR = 0xFFFFFFFD, 쌓인 PC = 0x08004a10, 쌓인 xPSR = 0x00000000, CFSR = 0x00020000". 무엇이 일어났나? 어디를 먼저 보나?
   정답/힌트: PSP에서 온 Thread 문맥(태스크)에서 T = 0 → INVSTATE. 쌓인 xPSR 전체가 0이라는 건 정상 실행 중 T를 잃었다기보다 **예외 복귀로 0인 xPSR을 복원한 것** — 새 태스크의 초기 프레임이 깨졌거나(스택 오버플로로 다른 태스크가 덮어씀), 포트의 프레임 생성 코드 문제를 의심한다(예제 4 CASE 3, C3).
6. **측정(다른 코어)** — 예제 12를 고쳐 FP를 쓴 뒤 PSP 위에서 도는 태스크(`run_task`)에 IRQ를 걸었을 때 PSP가 몇 바이트 내려가는지 재 보라. ISR이 아니라 **태스크** 스택 예산에 무엇을 더해야 하는지 결론을 내라.
   정답/힌트: 104바이트(26워드)가 예상된다(예제 12의 MSP 측정과 같은 프레임). 따라서 FP를 쓰는 태스크는 "자기 깊이 + 104 B"로 잡는다. 세부는 B6.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| programmer's model | 프로그래머 모델 | 소프트웨어가 보는 레지스터 · 모드 · 스택의 전체 구조 |
| R0–R12 | 범용 레지스터 | R0–R3 · R12는 caller-saved, R4–R11은 callee-saved (AAPCS) |
| SP (R13) | 스택 포인터 | 실제로는 MSP · PSP 중 지금 선택된 것 |
| MSP | Main Stack Pointer | reset 기본 스택, Handler 모드가 항상 사용 = RTOS의 ISR 스택 |
| PSP | Process Stack Pointer | CONTROL.SPSEL = 1일 때 Thread 모드가 사용 = 태스크 스택 |
| LR (R14) | Link Register | 함수 복귀 주소, 예외 안에서는 EXC_RETURN |
| PC (R15) | Program Counter | 다음 명령 주소, 분기 주소의 bit0은 Thumb 표시 |
| xPSR | 프로그램 상태 레지스터 | APSR · IPSR · EPSR을 한 워드로 겹친 것 |
| APSR | Application PSR | N · Z · C · V · Q (+ GE) 플래그, 비특권도 쓰기 가능 |
| IPSR | Interrupt PSR | 지금 처리 중인 예외 번호, 0 = Thread 모드 |
| EPSR | Execution PSR | T 비트와 IT/ICI. MRS로 읽으면 0 |
| T bit | Thumb 비트 | EPSR bit24. 0이 되면 INVSTATE UsageFault |
| CONTROL | 제어 레지스터 | nPRIV(bit0) · SPSEL(bit1) · FPCA(bit2) |
| nPRIV | 비특권 비트 | 1이면 Thread 모드가 비특권. Handler에는 영향 없음 |
| SPSEL | 스택 선택 비트 | 1이면 Thread 모드가 PSP 사용. Handler에서는 0으로 읽힘 |
| FPCA | FP 문맥 활성 비트 | FP 명령 실행 시 1, 예외 프레임이 26워드로 커짐 |
| PRIMASK · FAULTMASK · BASEPRI | 마스크 레지스터 | 인터럽트 우선순위 마스킹, 특권 전용 (B3) |
| Thread mode | 스레드 모드 | 예외 처리 중이 아닌 실행 상태 (main, 태스크) |
| Handler mode | 핸들러 모드 | 예외 처리 중, 항상 특권 · MSP |
| privileged / unprivileged | 특권 / 비특권 | 시스템 레지스터 · 보호 메모리 접근 가능 여부 |
| SVC | Supervisor Call | 비특권 → 커널 진입 명령, 예외 번호 11 |
| EXC_RETURN | 예외 복귀 값 | 예외 진입 시 LR에 들어가는 0xFFFFFFxx 값. bit2 = PSP 여부 |
| SCS | System Control Space | 0xE000E000~0xE000EFFF, NVIC · SCB · SysTick · MPU. 비특권 접근 시 BusFault |
| STIR | 소프트웨어 인터럽트 트리거 레지스터 | CCR.USERSETMPEND = 1이면 비특권도 쓸 수 있는 SCS 레지스터 |
| INVSTATE | 잘못된 상태 UsageFault | T = 0으로 실행하려 할 때, CFSR bit17 |
| MRS / MSR / ISB | 특수 레지스터 읽기 / 쓰기 / 명령 동기화 배리어 | CONTROL을 바꾼 뒤 ISB 필수 |
| AAPCS | Arm 프로시저 호출 표준 | 인자 · 반환 · 레지스터 보존 규칙 (자세히는 BM C3, EL B1) |

---

## 14. 요약 & 체크리스트

Cortex-M의 프로그래머 모델은 범용 레지스터 13개, SP · LR · PC, 그리고 MRS/MSR로만 다루는 xPSR · 마스크 셋 · CONTROL로 이루어진다. 모드는 Thread와 Handler 둘뿐이고(IPSR이 예외 번호, 0이면 Thread), Handler는 항상 특권 · MSP다. CONTROL은 Thread 모드의 특권(nPRIV), 스택(SPSEL), FP 문맥(FPCA)을 정한다. 특권은 스스로 내려놓을 수 있지만 되찾는 길은 예외(SVC)뿐이고, 비특권의 MSR · CPS는 조용히 무시되며 SCS 접근은 BusFault가 된다. T 비트는 항상 1이어야 하고, 0이 되면 — 짝수 주소 분기든 xPSR = 0인 위조 프레임이든 — INVSTATE fault다. 하드웨어가 예외 때 AAPCS caller-saved 8워드만 저장하므로 ISR은 평범한 C 함수이고, 대신 컨텍스트 스위치는 R4–R11을 소프트웨어로 저장한다. RTOS는 태스크를 PSP, ISR을 MSP에 둔다: 실측에서 ISR 3단계 중첩 시 태스크 스택이 584 → 232바이트로 줄었고 ISR 몫은 MSP에 한 번만 냈다(태스크 10개면 54% 절약). FreeRTOS CM3 포트는 CONTROL을 직접 쓰지 않고 EXC_RETURN 0xFFFFFFFD로 SPSEL을 켜며, Zephyr는 reset.S에서 SPSEL을 켜고 `z_interrupt_stacks`를 MSP로 쓴다.

- [ ] R0–R15의 역할과 AAPCS caller/callee-saved 구분을 말할 수 있다
- [ ] xPSR의 세 조각(APSR · IPSR · EPSR)과 주요 비트 위치(N Z C V Q, T = bit24, 예외 번호 = bit8:0)를 그릴 수 있다
- [ ] 예외 번호 표(2 NMI, 3 HardFault, 11 SVC, 14 PendSV, 15 SysTick, 16 + n IRQ)를 외운다
- [ ] CONTROL의 nPRIV · SPSEL · FPCA가 각각 무엇이고, 왜 Thread 모드에만 의미가 있는지 설명할 수 있다
- [ ] 모드 × 특권 2×2 표와 여섯 전이, "특권 회복은 SVC로만"을 설명할 수 있다
- [ ] 비특권에서 "무시되는 것"(MSR · CPS · MRS SP)과 "fault 나는 것"(SCS 접근)을 구분한다
- [ ] T 비트가 0이 되는 두 경로와 증상(CFSR 0x20000, 쌓인 xPSR bit24 = 0)을 안다
- [ ] PSP/MSP 분리가 스택 RAM을 얼마나 줄이는지 숫자로 설명하고, 태스크 스택과 MSP 크기를 정하는 규칙을 말할 수 있다
- [ ] EXC_RETURN 0xFFFFFFF9 / 0xFFFFFFF1 / 0xFFFFFFFD / 0xFFFFFFE9의 의미를 말하고, fault 핸들러가 `tst lr, #4`를 하는 이유를 안다
- [ ] CONTROL을 바꾼 뒤 ISB가 필요한 이유를 안다
- [ ] FreeRTOS(`prvPortStartFirstTask`, `vPortSVCHandler`, `orr r14, #0xd`)와 Zephyr(`reset.S`, `z_arm_interrupt_stack_setup`, `z_arm_pendsv`)가 CONTROL · MSP · PSP를 만지는 지점을 말할 수 있다
- [ ] M0(Armv6-M)과 M33(Armv8-M)에서 프로그래머 모델이 무엇이 다른지 두 가지 이상 말할 수 있다

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM C1 | 프로그래머 모델 | §0–§7 전체 (주인) | 이 노트 링크 + RTOS 없는 펌웨어에서 CONTROL을 건드리는 장면: 부트로더 → 앱 점프 때 MSP · VTOR 재설정, 비특권 테스트 하네스 |
| BM C3 | AAPCS 호출 규약 | §1.4 · §1.5 (예외 · 컨텍스트 스위치에 필요한 부분만) | AAPCS 전체: 8바이트 스택 정렬, 구조체 반환, 가변 인자, C ↔ asm 연결 |
| BM C4 | 예외 진입 · 복귀 | §2.3 · §5.2 · §6 (EXC_RETURN 값만 관찰) | 주인은 RTOS B2. Bare-Metal은 링크 |
| BM C8 | Fault 예외 | §4.3 · §2.5 (CFSR 0x8200 · 0x20000만) | fault 레지스터 전체 해석, escalation, lockup (RTOS M1과 공유) |
| BM C10 | FPU · DSP · Helium | §7.1 (FPCA · 26워드 프레임 관찰만) | 주인은 RTOS B6. Bare-Metal은 FPU 켜는 순서 · DSP 명령 |

## 참고 자료

- Arm, "Armv7-M Architecture Reference Manual" (DDI 0403) — B1.4 레지스터(xPSR, 특수 레지스터, CONTROL), B1.5 예외 모델(EXC_RETURN, 스택 프레임), B5 MRS/MSR 의사코드(비특권 SP 읽기, Handler 모드 SPSEL). https://developer.arm.com/documentation/ddi0403/latest
- Arm, "Cortex-M3 Devices Generic User Guide" (DUI 0552) — 2.1 프로그래머 모델(모드 · 특권 · 스택), 2.1.3 코어 레지스터, 4.3 System Control Block(CFSR · SHCSR · CCR.USERSETMPEND). https://developer.arm.com/documentation/dui0552/latest
- Arm, "Cortex-M4 Devices Generic User Guide" (DUI 0553) — CONTROL.FPCA, FPCCR, 확장 스택 프레임
- Arm, "Procedure Call Standard for the Arm Architecture" (AAPCS32) — https://github.com/ARM-software/abi-aa
- FreeRTOS Kernel V11.3.2 소스(랩 `/var/rtoslab/src/FreeRTOS-Kernel`) — `portable/GCC/ARM_CM3/port.c`(`pxPortInitialiseStack`, `portINITIAL_XPSR`, `portSTART_ADDRESS_MASK`, `vPortSVCHandler`, `prvPortStartFirstTask`, `xPortStartScheduler`, `xPortPendSVHandler`), `portable/GCC/ARM_CM3_MPU/port.c`(`portINITIAL_CONTROL_IF_UNPRIVILEGED`, `vResetPrivilege`, `xIsPrivileged`, `vSystemCallEnter`, `portSVC_RAISE_PRIVILEGE`), `portable/GCC/ARM_CM0/portmacro.h`(`cpsid i`), `portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c`(PSPLIM). GitHub: https://github.com/FreeRTOS/FreeRTOS-Kernel
- Zephyr v4.4.2 소스(랩 `/var/rtoslab/zephyrproject/zephyr`) — `arch/arm/core/cortex_m/reset.S`, `arch/arm/include/cortex_m/stack.h`(`z_arm_interrupt_stack_setup`), `arch/arm/include/cortex_m/kernel_arch_func.h`(`arch_kernel_init`), `arch/arm/core/cortex_m/thread.c`(`arch_switch_to_main_thread`, `arch_user_mode_enter`, `z_arm_thread_is_in_user_mode`), `swap_helper.S`(`z_arm_pendsv`), `svc.S`(`z_arm_svc`), `arm-m-switch.c`(`CONFIG_USE_SWITCH`), `kernel/Kconfig`(`ISR_STACK_SIZE`), `boards/arm/mps2/Kconfig.defconfig`
- Zephyr Project Documentation — User Mode: https://docs.zephyrproject.org/latest/kernel/usermode/index.html
- Joseph Yiu, "The Definitive Guide to Arm Cortex-M3 and Cortex-M4 Processors" (3rd ed., Newnes) — 프로그래머 모델 · 예외 · OS 지원 기능 장
- QEMU 문서 — Arm MPS2 보드(`mps2-an385`, `mps2-an386`): https://www.qemu.org/docs/master/system/arm/mps2.html
- 관련 Don 노트 — EL B1(AAPCS · EABI), EL C5(Cortex-A Exception Level), EL A1(커널/유저 경계), 이 모듈 A4(태스크 · 위조 초기 프레임). 이어서: B2 · B3 · B4 · B6 · B7, C2 · C3 · C8, F4, M1
