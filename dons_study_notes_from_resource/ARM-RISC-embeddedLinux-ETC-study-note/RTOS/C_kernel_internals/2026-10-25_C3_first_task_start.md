# C3. 첫 태스크 시작 — 한 번도 멈춘 적 없는 태스크를 "복원"하는 법: 위조 프레임, SVC, 버려지는 main 스택

> **이 노트를 다 읽으면**: 새 태스크의 초기 스택 프레임(fake frame)을 gdb 덤프에서 한 워드씩 읽고 각 값이 왜 그 값인지 말할 수 있다 · `vTaskStartScheduler()` → `xPortStartScheduler()` → `prvPortStartFirstTask()` → `svc 0` → `vPortSVCHandler`를 명령어 한 줄 단위로, 레지스터(MSP·PSP·BASEPRI·CONTROL·LR) 값까지 따라갈 수 있다 · 스케줄러 시작이 실패하는 경우, 시작 전 인터럽트·FromISR, main 스택이 덮이는 현상을 실제로 재현하고 설명할 수 있다 · Zephyr가 같은 일을 SVC 없이 하는 방법(`arch_switch_to_main_thread`)과 reset → 첫 태스크까지의 시간 분해를 숫자로 비교할 수 있다
> **왜 필요한가**: "How does the first task start? What happens to main's stack? What if a task function returns?"는 RTOS 포팅·bring-up 면접의 단골이고, 새 보드에서 "스케줄러를 켰더니 바로 HardFault" 같은 버그는 거의 다 이 경로(벡터 테이블, 우선순위 비트, 스택 정렬, xPSR T 비트)에서 난다.
> **Don 기준 난이도**: 예외 진입·복귀 자동 스태킹, MSP/PSP, 벡터 테이블은 이미 안다 / "복원할 게 없는데 복원하는" 트릭, FreeRTOS가 시작 직전에 NVIC 우선순위 비트 수를 직접 재는 방법, MSP를 벡터 테이블 값으로 되돌려 main 스택을 버리는 이유, Zephyr의 예외 없는 시작 방식을 새로 배운다
> **선행 노트**: A4(태스크 모델 — 위조 프레임 첫 소개, 태스크 함수 return), A7(부팅 시간 측정), B2(예외 진입·복귀, EXC_RETURN), B4(SVC·PendSV·SysTick), B5(reset → main), C2(PendSV 컨텍스트 스위치 — 평소의 저장·복원). 이어지는 노트: C5(tick), C7(크리티컬 섹션·BASEPRI), C8(커널 진입 경로), E1(ISR 규칙), F1(정적 할당), M1(HardFault 해독)

---

## 0. 큰 그림 — 닭이 먼저냐 달걀이 먼저냐

RTOS의 컨텍스트 스위치는 "**저장했던 것을 복원**"하는 일이다. PendSV 핸들러(C2)는 지금 태스크의 R4–R11을 그 태스크 스택에 밀어 넣고, 스택 포인터를 TCB에 적고, 다음 태스크 TCB에서 스택 포인터를 꺼내 R4–R11을 꺼내고, 예외 복귀로 나머지(R0–R3, R12, LR, PC, xPSR)를 하드웨어가 꺼내게 한다.

그런데 **방금 만든 태스크는 한 번도 실행된 적이 없다.** 저장된 적이 없으니 복원할 것도 없다. 그렇다고 첫 태스크만 특별한 코드로 부르면 "평소 경로"와 "첫 경로"가 달라져 버그가 숨는다. 그래서 커널은 **"이 태스크가 예전에 실행되다가 막 멈춘 것처럼" 스택을 꾸며 둔다** — 이것이 초기 스택 프레임 위조(fake frame)다. 그러면 첫 실행도 그냥 "복원"이 된다.

남은 문제는 맨 처음 한 번이다. 아직 아무 태스크도 돌고 있지 않으니 "현재 태스크를 저장"할 대상이 없다. 그래서 FreeRTOS의 Cortex-M3 포트는 첫 진입만은 PendSV 대신 **SVC 예외 하나**를 쓰고, 그 핸들러는 저장 단계 없이 **복원 절반만** 한다. 그리고 그 직전에 MSP를 리셋 값으로 되돌린다 — `main()`이 쓰던 스택은 이 순간 버려진다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="22" font-size="14" text-anchor="middle" font-weight="bold">평소의 스위치 (C2, PendSV)</text>
<text x="510" y="22" font-size="14" text-anchor="middle" font-weight="bold">처음 실행되는 태스크 (C3)</text>
<rect x="20" y="40" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="63" font-size="13" text-anchor="middle">태스크 A 실행 중 (PSP = A 스택)</text>
<line x1="170" y1="76" x2="170" y2="96" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="98" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="121" font-size="13" text-anchor="middle">저장: HW 8워드 + R4–R11 → A 스택</text>
<line x1="170" y1="134" x2="170" y2="154" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="156" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="179" font-size="13" text-anchor="middle">TCB 교체: A의 SP 저장, B의 SP 읽기</text>
<line x1="170" y1="192" x2="170" y2="212" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="214" width="300" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="170" y="237" font-size="13" text-anchor="middle">복원: R4–R11 + 예외 복귀 → B 실행</text>
<rect x="360" y="40" width="300" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510" y="63" font-size="13" text-anchor="middle">xTaskCreate: 새 스택은 0xa5뿐</text>
<line x1="510" y1="76" x2="510" y2="96" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="360" y="98" width="300" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510" y="121" font-size="13" text-anchor="middle">저장한 적 없음 → 복원할 게 없다</text>
<line x1="510" y1="134" x2="510" y2="154" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="360" y="156" width="300" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="179" font-size="13" text-anchor="middle">pxPortInitialiseStack: 16워드 위조</text>
<line x1="510" y1="192" x2="510" y2="212" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="360" y="214" width="300" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510" y="237" font-size="13" text-anchor="middle">SVC 0: 복원 절반만 → 첫 실행</text>
<line x1="320" y1="232" x2="358" y2="232" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="340" y="280" font-size="13" text-anchor="middle">두 경로의 끝(초록)은 같은 코드 모양이다 — 첫 실행도 "복원"으로 만든다</text>
</svg>
```

그림 1 — 왼쪽은 평소의 컨텍스트 스위치(C2), 오른쪽은 첫 실행. 새 태스크에는 저장된 문맥이 없으니 커널이 "멈췄던 것처럼" 프레임을 꾸며 두고, 첫 진입 때는 저장 단계 없이 복원만 한다.

**Don의 경험과 연결**: SSD 펌웨어에서 코어마다 `main` 루프 하나씩 돌리던 실행부(executive)는 "돌아갈 문맥"이 처음부터 하나뿐이라 이 문제가 없었다. 코루틴이나 백그라운드 작업을 "나중에 이어서 실행"하는 구조를 만들 때 Don이 아마 직접 해 봤을 일 — "처음 시작할 때도 이어서 실행하는 것처럼 상태 구조체를 채워 두기" — 가 정확히 위조 프레임이다. RTOS는 그걸 **CPU 레지스터 수준**에서 한다.

이 노트의 지도:

| 절 | 질문 | 실행 예제 |
|---|---|---|
| §1 | 위조 프레임에는 정확히 무엇이 들어 있나 | 예제 1 (gdb로 태스크 3개 프레임 덤프) |
| §2 | `vTaskStartScheduler()`는 무엇을 하나, 누가 먼저 도나 | 예제 6 (먼저 도는 태스크) |
| §3 | `xPortStartScheduler()`는 NVIC를 어떻게 확인·설정하나 | 예제 2 (우선순위 비트 탐지 · 4가지 assert) |
| §4 | `prvPortStartFirstTask` + `vPortSVCHandler` 한 줄씩 | 예제 3 (gdb stepi 21단계), 예제 4 (main 스택 덮임) |
| §5 | 무엇이 잘못될 수 있나 | 예제 5 (heap 부족 → 시작 실패), 예제 7 (시작 전 ISR · FromISR) |
| §6 | 직접 만들어 보기 | 예제 8 (bare-metal 50줄 런처) |
| §7 | Zephyr는 어떻게 하나 | 예제 9 (main · idle · 새 스레드 프레임), 예제 10 (gdb로 main 진입 21단계) |
| §8 | reset → 첫 태스크까지 몇 µs인가 | 예제 11 (FreeRTOS 분해), 예제 12 (Zephyr 분해) |

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩 `/var/rtoslab`: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위의 **FreeRTOS V11.3.2**(`portable/GCC/ARM_CM3`, heap_4, 템플릿 `FreeRTOSConfig.h` 기본값: 1 kHz tick, 우선순위 8단계, `configMAX_SYSCALL_INTERRUPT_PRIORITY 0x20`, 정적 할당 지원 + `configKERNEL_PROVIDED_STATIC_MEMORY 1`)와 **Zephyr v4.4.2**(같은 보드 `mps2/an385`). 시간 숫자는 전부 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자)로 잰 것이고, **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스·예외 스태킹 사이클을 모델링하지 않는다). 모든 예제는 `-Wall -Wextra` 경고 0개다.

```sh
# FreeRTOS 예제 빌드 + 실행 (~/.rtos_scratch/C3/run.sh)
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/$N.c OUT=/var/tmp/rtos/C3/$N "$@" && \
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/$N/app.elf; echo "exit=$?"
```

gdb 예제는 QEMU를 `-S -gdb tcp::31303`(다른 작성자와 겹치지 않는 포트)으로 멈춘 채 띄우고 `gdb -batch -x <스크립트>`로 붙었다(`~/.rtos_scratch/C3/gdbrun.sh`). 여기서 **함정 하나를 실제로 밟았다**: `-icount shift=5,align=off,sleep=off`(랩의 ICOUNT=1 설정) 상태에서 gdb 브레이크포인트에 멈출 때마다 가상 시계가 **다음 타이머 마감까지 1 ms씩 건너뛰었다**(FPGAIO 카운터가 브레이크 사이에 정확히 25000씩 늘었다). 그 결과 SVC 핸들러가 끝나자마자 SysTick이 끼어드는, 실제로는 일어나지 않는 장면이 보였다. 그래서 **gdb로 한 줄씩 따라가는 예제는 `sleep=on`으로**, **시간을 재는 예제는 gdb 없이 프로그램 안에서 카운터를 읽는 방식으로** 했다. "디버거를 붙이면 타이밍이 바뀐다"는 실칩에서도 똑같이 조심할 점이다(브레이크에 멈춘 동안 주변장치 타이머는 계속 돈다 — DBGMCU freeze 비트가 있는 이유).

---

## 1. 위조 프레임 — `pxPortInitialiseStack()` 한 줄씩

### 1.1 직관 — "예외 복귀 직전 사진"을 미리 찍어 둔다

Cortex-M에서 예외 복귀(`bx lr`, LR = `0xFFFFFFFx`)는 하드웨어가 스택에서 8워드(R0, R1, R2, R3, R12, LR, PC, xPSR)를 꺼내고 PC로 점프하는 동작이다(B2). 그러니 **스택에 8워드를 원하는 값으로 써 두고 그 주소를 PSP에 넣은 다음 예외에서 복귀하면, CPU는 그 PC에서 그 레지스터 값으로 실행을 "재개"한다.** 실제로는 처음 시작하는 것인데 CPU는 구분하지 못한다. 거기에 PendSV가 소프트웨어로 저장·복원하는 R4–R11 8워드를 더 얹으면, 평소의 스위치 경로(C2)가 그대로 첫 태스크를 꺼낼 수 있는 16워드 프레임이 된다.

### 1.2 소스 — V11.3.2 `portable/GCC/ARM_CM3/port.c`

```c
/* port.c:182 — 실제 소스 그대로 (주석만 줄임) */
StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters )
{
    pxTopOfStack--;                                                      /* 정렬 여유 한 칸 */
    *pxTopOfStack = portINITIAL_XPSR;                                    /* xPSR = 0x01000000 */
    pxTopOfStack--;
    *pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK; /* PC  = 진입점 & ~1 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) portTASK_RETURN_ADDRESS;             /* LR  = prvTaskExitError */
    pxTopOfStack -= 5;                                                   /* R12, R3, R2, R1: 값 안 씀 */
    *pxTopOfStack = ( StackType_t ) pvParameters;                        /* R0  = 인자 */
    pxTopOfStack -= 8;                                                   /* R11..R4: 값 안 씀 */
    return pxTopOfStack;                                                 /* → TCB.pxTopOfStack */
}
```

호출하는 쪽은 `tasks.c`의 `prvInitialiseNewTask()`다. 스택 끝 주소를 `portBYTE_ALIGNMENT`(Cortex-M은 8)로 내려 정렬한 뒤 이 함수를 부르고, 돌려받은 값을 TCB의 첫 멤버 `pxTopOfStack`에 적는다(A4 §2.4 — 첫 멤버라서 어셈블리가 `ldr r0, [r1]` 한 번으로 꺼낸다).

| 워드 | 값 | 왜 이 값인가 | 틀리면 |
|---|---|---|---|
| xPSR | `0x01000000` | bit 24 = T(Thumb). Cortex-M은 Thumb 상태만 있다. 나머지(플래그, IPSR, ICI/IT)는 0 | T=0이면 복귀 직후 INVSTATE UsageFault → HardFault (예제 8에서 재현) |
| PC | `pxCode & 0xfffffffe` | 예외 복귀가 꺼내는 PC는 bit 0이 0이어야 한다는 규격(포트 주석: "For strict compliance with the Cortex-M spec") | bit 0이 1이면 동작이 예측 불가(UNPREDICTABLE) — 미리 지운다 |
| LR | `prvTaskExitError` (+1 Thumb 비트) | 태스크 함수가 `return`하면 `bx lr`로 여기 온다 | NULL이면 return 순간 0번지로 점프 → 원인을 모르는 HardFault |
| R12, R3, R2, R1 | 안 씀 (스택 채움값 `0xa5a5a5a5`) | AAPCS에서 인자 레지스터지만 FreeRTOS 태스크 인자는 하나뿐 | — |
| R0 | `pvParameters` | AAPCS: 첫 인자 = R0. 그래서 `void task(void *pv)`의 `pv`가 된다 | — |
| R11..R4 | 안 씀 (`0xa5a5a5a5`) | callee-saved 레지스터. 태스크가 처음 시작할 때 값은 무의미 | — |

`configTASK_RETURN_ADDRESS`를 정의하면 LR 값을 바꿀 수 있다(포트 소스 104행 주석: "in case it messes up unwinding of the stack in the debugger"). A4 §4.4에서 본 엉터리 backtrace가 바로 그 이유다.

### 1.3 예제 1 — gdb로 스케줄러 시작 직전 모든 태스크의 프레임 덤프

**무엇을 확인하나**: A4는 `main()`에서 C로 사용자 태스크 하나의 프레임을 찍었다. 여기서는 한 걸음 더 가서 **`xPortStartScheduler()` 입구에 브레이크를 걸고**, 커널이 직접 만든 idle 태스크와 timer 태스크까지 세 태스크의 프레임을 gdb로 읽는다. 그리고 그 순간의 MSP·PSP·BASEPRI·CONTROL, `uxCriticalNesting`도 본다.

```c
/* ex1: forged initial frames of every task, inspected by gdb at xPortStartScheduler */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static uint32_t param = 0xC3C3C3C3;

static void ctl_task(void *pv)
{
    uint32_t *p = pv;                      /* r0 of the forged frame */
    board_printf_raw("ctl_task: pv=0x%x *pv=0x%x\n", (unsigned)p, (unsigned)*p);
    board_exit(0);
}

int main(void)
{
    TaskHandle_t h;
    xTaskCreate(ctl_task, "ctl", 128, &param, 3, &h);
    board_printf_raw("created ctl: TCB=0x%x\n", (unsigned)h);
    vTaskStartScheduler();
    return 1;
}
```

gdb 스크립트(`ex1.gdb`). TCB 워드 오프셋은 A4 예제 1에서 확인한 것(word 0 = `pxTopOfStack`, 11 = 우선순위, 12 = `pxStack`, 13~ = 이름)을 쓴다. gdb에서 `$sp`는 진짜 SP 레지스터라서 변수 이름으로 쓰면 안 된다(처음에 그 실수를 했다) — `$f`로 바꿨다.

```text
break xPortStartScheduler
continue
define dumpframe
  set $tcb = (uint32_t *)$arg0
  set $f = (uint32_t *)$tcb[0]
  printf "TCB %s @0x%08x  pxTopOfStack=0x%08x  pxStack=0x%08x  prio=%u\n", (char *)&$tcb[13], $tcb, $f, $tcb[12], $tcb[11]
  printf "  r4..r11 : %08x %08x %08x %08x %08x %08x %08x %08x\n", $f[0],$f[1],$f[2],$f[3],$f[4],$f[5],$f[6],$f[7]
  printf "  r0      : %08x\n", $f[8]
  printf "  r1 r2 r3 r12 : %08x %08x %08x %08x\n", $f[9],$f[10],$f[11],$f[12]
  printf "  lr      : %08x  ", $f[13]
  info symbol $f[13]
  printf "  pc      : %08x  ", $f[14]
  info symbol $f[14]
  printf "  xpsr    : %08x\n", $f[15]
  printf "  above   : %08x %08x\n", $f[16], $f[17]
end
dumpframe pxCurrentTCB
set $h = ((ListItem_t *)pxReadyTasksLists[3].xListEnd.pxNext)->pvOwner
dumpframe $h
set $i = xIdleTaskHandles[0]
dumpframe $i
print/x &param
print xSchedulerRunning
print/x uxCriticalNesting
info registers msp psp basepri primask control
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/C3 && make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex1.c OUT=/var/tmp/rtos/C3/ex1 && ./gdbrun.sh /var/tmp/rtos/C3/ex1/app.elf ex1.gdb'
```

```text
   text	   data	    bss	    dec	    hex	filename
  11644	    104	 135400	 147148	  23ecc	/var/tmp/rtos/C3/ex1/app.elf
Breakpoint 1, xPortStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:284
284	        const portISR_t * const pxVectorTable = portSCB_VTOR_REG;
TCB Tmr Svc @0x20000868  pxTopOfStack=0x20000820  pxStack=0x20000068  prio=7
  r4..r11 : a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
  r0      : 00000000
  r1 r2 r3 r12 : a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
  lr      : 00001d1d  prvTaskExitError + 1 in section .text
  pc      : 00001734  prvTimerTask in section .text
  xpsr    : 01000000
  above   : a5a5a5a5 a5a5a5a5
TCB ctl @0x20001210  pxTopOfStack=0x200011c0  pxStack=0x20001008  prio=3
  r4..r11 : a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
  r0      : 20000000
  r1 r2 r3 r12 : a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
  lr      : 00001d1d  prvTaskExitError + 1 in section .text
  pc      : 000000c0  ctl_task in section .text
  xpsr    : 01000000
  above   : a5a5a5a5 a5a5a5a5
TCB IDLE @0x20000cc8  pxTopOfStack=0x20000c80  pxStack=0x200008c8  prio=0
  r4..r11 : a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
  r0      : 00000000
  r1 r2 r3 r12 : a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
  lr      : 00001d1d  prvTaskExitError + 1 in section .text
  pc      : 00000750  prvIdleTask in section .text
  xpsr    : 01000000
  above   : a5a5a5a5 a5a5a5a5
$1 = 0x20000000
$2 = 1
$3 = 0xaaaaaaaa
msp            0x203fffb8          541065144
psp            0x0                 0
basepri        0x20                32
primask        0x0                 0
control        0x0                 0
```

(gdb 연결 메시지 몇 줄은 생략했다. 같은 ELF의 `nm` 결과: `ctl_task` 0xc0, `prvIdleTask` 0x750, `prvTimerTask` 0x1734, `prvTaskExitError` 0x1d1c, `param` 0x20000000, `ucHeap` 0x20000ffc.)

**출력에서 볼 것**:

- **`pxCurrentTCB`가 이미 `Tmr Svc`(timer 데몬 태스크, 우선순위 7)를 가리킨다.** 내가 만든 `ctl`(우선순위 3)이 아니다. 스케줄러가 처음 꺼낼 태스크는 이 순간 이미 정해져 있다 — §2.3에서 규칙을 본다.
- 세 프레임의 모양이 똑같다: xPSR `0x01000000`, PC = 각자의 함수(Thumb 비트 없는 짝수 주소), LR = `prvTaskExitError + 1`, R0 = 인자(`ctl`만 `&param` = 0x20000000, idle·timer는 NULL), 나머지는 스택 채움 값 `0xa5a5a5a5`.
- 위치가 다르다: `ctl`의 TCB(0x20001210)와 스택(0x20001008~)은 `ucHeap`(0x20000ffc~) 안 = **heap에서 할당**. 반면 `Tmr Svc`(스택 0x20000068~)와 `IDLE`(0x200008c8~)은 heap **밖** `.bss`에 있다. 템플릿이 `configKERNEL_PROVIDED_STATIC_MEMORY 1`이라 커널이 자기 정적 배열(`uxTimerTaskStack` 등)을 쓴다. 이 차이가 §5.2의 "heap이 모자라 시작 실패"를 막는 방법이 된다.
- 각 프레임 위에 `0xa5a5a5a5` 두 워드("above")가 남는다. 스택 끝을 8바이트로 내려 정렬한 뒤 `pxTopOfStack--`로 한 칸 더 내리고 시작하기 때문이다(A4 §2.3에서 계산했다).
- **스케줄러가 "돌기로 결정된" 상태인데 CPU는 아직 main의 문맥이다**: `xSchedulerRunning = 1`인데 PSP = 0, CONTROL = 0(MSP 사용, 특권), MSP = 0x203fffb8(RAM 끝 0x20400000에서 72바이트 내려온 곳 = Reset_Handler와 main의 스택).
- **BASEPRI = 0x20, `uxCriticalNesting` = 0xaaaaaaaa**: 첫 `xTaskCreate()`가 크리티컬 섹션에 들어갔다 나왔는데도 인터럽트가 다시 열리지 않았다. 이유와 결과는 §5.4에서 실제 인터럽트로 확인한다.

### 1.4 그림으로 — 실제 덤프에서 그린 `ctl` 태스크 스택

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="20" font-size="13" font-weight="bold">태스크 ctl 스택 (예제 1 덤프, 높은 주소가 위)</text>
<text x="96" y="46" font-size="12" text-anchor="end">0x2000120c</text>
<rect x="104" y="32" width="190" height="20" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="199" y="47" font-size="12" text-anchor="middle">a5a5a5a5 (정렬 여유)</text>
<text x="96" y="66" font-size="12" text-anchor="end">0x20001208</text>
<rect x="104" y="52" width="190" height="20" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="199" y="67" font-size="12" text-anchor="middle">a5a5a5a5 (정렬 여유)</text>
<text x="96" y="86" font-size="12" text-anchor="end">0x20001204</text>
<rect x="104" y="72" width="190" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="199" y="87" font-size="12" text-anchor="middle">xPSR = 01000000</text>
<text x="96" y="106" font-size="12" text-anchor="end">0x20001200</text>
<rect x="104" y="92" width="190" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="199" y="107" font-size="12" text-anchor="middle">PC = 000000c0</text>
<text x="96" y="126" font-size="12" text-anchor="end">0x200011fc</text>
<rect x="104" y="112" width="190" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="199" y="127" font-size="12" text-anchor="middle">LR = 00001d1d</text>
<text x="96" y="146" font-size="12" text-anchor="end">0x200011f8</text>
<rect x="104" y="132" width="190" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="199" y="147" font-size="12" text-anchor="middle">R12 = a5a5a5a5</text>
<text x="96" y="166" font-size="12" text-anchor="end">0x200011ec</text>
<rect x="104" y="152" width="190" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="199" y="167" font-size="12" text-anchor="middle">R3 R2 R1 (3워드) = a5…</text>
<text x="96" y="186" font-size="12" text-anchor="end">0x200011e0</text>
<rect x="104" y="172" width="190" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="199" y="187" font-size="12" text-anchor="middle">R0 = 20000000</text>
<text x="96" y="206" font-size="12" text-anchor="end">0x200011c0</text>
<rect x="104" y="192" width="190" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="199" y="218" font-size="12" text-anchor="middle">R4 … R11 (8워드)</text><text x="199" y="236" font-size="12" text-anchor="middle">전부 a5a5a5a5</text>
<line x1="20" y1="202" x2="102" y2="202" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="20" y="270" font-size="12">TCB.pxTopOfStack</text>
<text x="20" y="286" font-size="12">= 0x200011c0</text>
<rect x="104" y="252" width="190" height="40" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/><text x="199" y="276" font-size="12" text-anchor="middle">빈 스택 (a5 채움)</text>
<text x="96" y="306" font-size="12" text-anchor="end">0x20001008</text>
<text x="199" y="306" font-size="12" text-anchor="middle">pxStack (스택 바닥)</text>
<line x1="300" y1="72" x2="300" y2="192" stroke="#4a7bd0" stroke-width="2"/>
<text x="310" y="122" font-size="13">하드웨어 프레임 8워드</text>
<text x="310" y="140" font-size="12">예외 복귀(bx 0xFFFFFFFD) 때 CPU가 자동으로 꺼낸다</text>
<line x1="300" y1="192" x2="300" y2="252" stroke="#e08a3c" stroke-width="2"/>
<text x="310" y="218" font-size="13">소프트웨어 몫 8워드</text>
<text x="310" y="236" font-size="12">SVC/PendSV가 ldmia r0!, {r4-r11}로 꺼낸다</text>
<rect x="310" y="330" width="350" height="124" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="322" y="352" font-size="12">PC 0xc0 = ctl_task (심볼 0xc1에서 Thumb 비트 제거)</text>
<text x="322" y="372" font-size="12">LR 0x1d1d = prvTaskExitError+1 (return 시)</text>
<text x="322" y="392" font-size="12">R0 0x20000000 = &amp;param (pvParameters)</text>
<text x="322" y="412" font-size="12">xPSR 0x01000000 = T 비트만 1</text>
<text x="322" y="432" font-size="12">72 B 사용 = 16워드 프레임 + 정렬 여유 2워드</text>
<text x="20" y="350" font-size="12">TCB 0x20001210 (스택 바로 위,</text>
<text x="20" y="366" font-size="12">heap_4에서 연속 할당)</text>
</svg>
```

그림 2 — 예제 1이 덤프한 `ctl` 태스크의 초기 스택(실제 주소와 값). 파랑 8워드는 하드웨어가 예외 복귀 때 꺼내는 몫, 주황 8워드는 커널 어셈블리가 꺼내는 몫이다. `pxTopOfStack`은 주황 블록의 맨 아래를 가리킨다.

### 1.5 흔한 함정

- **스택을 8바이트로 정렬하지 않은 정적 버퍼**: `xTaskCreateStatic()`에 넘긴 `StackType_t` 배열은 4바이트 정렬일 수 있다. FreeRTOS는 끝 주소를 내려 정렬하니 프레임 자체는 맞지만, Zephyr·자작 커널에서는 정렬을 잊기 쉽다. AAPCS는 함수 경계에서 SP 8바이트 정렬을 요구하고, `printf("%f")`·`double` 인자가 있는 코드가 정렬 안 된 스택에서 이상한 값을 낸다.
- **"R4–R11에도 의미 있는 값을 넣자"**: 필요 없다. 태스크 함수는 C 함수이므로 callee-saved 레지스터의 처음 값에 의존하지 않는다. FreeRTOS도 칸만 비워 둔다(디버깅용 표식을 넣는 포트도 있지만 CM3 포트는 아니다).
- **포인터 크기 인자를 R0 말고 다른 곳으로 기대**: FreeRTOS 태스크 인자는 하나(R0). 여러 값을 넘기려면 구조체 포인터 하나로. Zephyr는 p1·p2·p3 셋을 R1·R2·R3로 넘긴다(§7.3).

---

## 2. `vTaskStartScheduler()` — 커널이 "시작해도 된다"고 결정하는 곳

### 2.1 직관 — 공장 가동 전 점검표

`vTaskStartScheduler()`는 "이제부터 CPU는 내가 나눠 준다"는 선언이다. 선언하기 전에 커널은 자기에게 꼭 필요한 일꾼 둘(**idle 태스크**: 아무도 할 일이 없을 때 돌 태스크, **timer 데몬 태스크**: 소프트웨어 타이머 콜백을 돌릴 태스크)을 만들고, 인터럽트를 막고, 상태 변수를 "가동 중"으로 바꾼 뒤, CPU 코어 쪽 준비(포트 계층, §3)로 넘어간다. 포트 함수는 정상이면 **돌아오지 않는다.**

### 2.2 소스 — `tasks.c:3742` (V11.3.2, 단일 코어 경로만 남김)

```c
void vTaskStartScheduler( void )
{
    BaseType_t xReturn;

    xReturn = prvCreateIdleTasks();                 /* IDLE, 우선순위 0 (tskIDLE_PRIORITY) */
    #if ( configUSE_TIMERS == 1 )
        if( xReturn == pdPASS )
            xReturn = xTimerCreateTimerTask();      /* "Tmr Svc", configTIMER_TASK_PRIORITY */
    #endif

    if( xReturn == pdPASS )
    {
        portDISABLE_INTERRUPTS();                   /* BASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY */
        xNextTaskUnblockTime = portMAX_DELAY;
        xSchedulerRunning = pdTRUE;
        xTickCount = ( TickType_t ) configINITIAL_TICK_COUNT;
        portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();
        traceTASK_SWITCHED_IN();
        traceSTARTING_SCHEDULER( xIdleTaskHandles );
        ( void ) xPortStartScheduler();             /* 정상이면 여기서 끝: 돌아오지 않는다 */
    }
    else
    {
        /* idle 또는 timer 태스크를 만들 heap이 없었다 */
        configASSERT( xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY );
    }
}
```

`prvCreateIdleTasks()`(`tasks.c` 같은 파일, 바로 위)는 `configSUPPORT_STATIC_ALLOCATION == 1`이면 `vApplicationGetIdleTaskMemory()`로 받은 버퍼에 `xTaskCreateStatic()`, 아니면 `xTaskCreate(..., configMINIMAL_STACK_SIZE, ...)`로 heap에서 만든다. 우선순위 인자는 `portPRIVILEGE_BIT`(= `tskIDLE_PRIORITY | portPRIVILEGE_BIT`, 우선순위 0)다. timer 태스크도 같은 구조(`timers.c`의 `xTimerCreateTimerTask()` → `vApplicationGetTimerTaskMemory()` 또는 `xTaskCreate()`), 그리고 그 전에 `prvCheckForValidListAndQueue()`가 명령 큐를 만든다(정적 할당이면 `xQueueCreateStatic()`).

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="10" width="300" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170" y="30" font-size="12" text-anchor="middle">main(): xTaskCreate(...) 몇 번</text>
<line x1="170" y1="40" x2="170" y2="52" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="54" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="74" font-size="12" text-anchor="middle">vTaskStartScheduler()</text>
<line x1="170" y1="84" x2="170" y2="96" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="98" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="118" font-size="12" text-anchor="middle">prvCreateIdleTasks(): IDLE (prio 0)</text>
<line x1="170" y1="128" x2="170" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="142" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="162" font-size="12" text-anchor="middle">xTimerCreateTimerTask(): Tmr Svc</text>
<line x1="170" y1="172" x2="170" y2="184" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="186" width="300" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="206" font-size="12" text-anchor="middle">둘 다 pdPASS?</text>
<line x1="320" y1="201" x2="398" y2="201" stroke="#d0564a" marker-end="url(#ah3)"/><text x="358" y="195" font-size="12" text-anchor="middle">아니오</text>
<rect x="400" y="180" width="260" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="530" y="198" font-size="12" text-anchor="middle">configASSERT(tasks.c:3832)</text><text x="530" y="215" font-size="12" text-anchor="middle">→ main으로 돌아온다 (예제 5)</text>
<line x1="170" y1="216" x2="170" y2="228" stroke="currentColor" marker-end="url(#ah3)"/><text x="180" y="226" font-size="12">예</text>
<rect x="20" y="230" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="250" font-size="12" text-anchor="middle">BASEPRI=0x20, xSchedulerRunning=1</text>
<line x1="170" y1="260" x2="170" y2="272" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="274" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="294" font-size="12" text-anchor="middle">xPortStartScheduler(): 점검 (§3)</text>
<line x1="170" y1="304" x2="170" y2="316" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="318" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="338" font-size="12" text-anchor="middle">SHPR3·SHPR2, SysTick, nesting=0</text>
<line x1="170" y1="348" x2="170" y2="360" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="362" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="382" font-size="12" text-anchor="middle">prvPortStartFirstTask(): MSP 리셋, svc 0</text>
<line x1="170" y1="392" x2="170" y2="404" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="406" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="426" font-size="12" text-anchor="middle">vPortSVCHandler: 위조 프레임 복원</text>
<line x1="170" y1="436" x2="170" y2="448" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="450" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="170" y="470" font-size="12" text-anchor="middle">첫 태스크 첫 명령 (Thread, PSP)</text>
<text x="350" y="30" font-size="12">첫 API 호출부터 BASEPRI=0x20 (§5.4)</text>
<text x="350" y="118" font-size="12">tasks.c — 커널 객체 준비 (portable 아님)</text>
<text x="350" y="162" font-size="12">timers.c — 큐 + 데몬 태스크</text>
<text x="350" y="294" font-size="12">port.c — 여기부터 CPU 전용 코드</text>
<text x="350" y="338" font-size="12">PendSV·SysTick 최저, SVC 최고</text>
<text x="350" y="382" font-size="12">main의 스택은 이 순간 버려진다</text>
<text x="350" y="426" font-size="12">ldmia r4-r11, msr psp, bx 0xFFFFFFFD</text>
<text x="350" y="470" font-size="12">정상 경로에서는 다시 main으로 안 온다</text>
</svg>
```

그림 3 — 스케줄러 시작 경로. 파랑은 `tasks.c`·`timers.c`(이식 가능한 커널 코드), 초록은 `portable/GCC/ARM_CM3/port.c`(Cortex-M3 전용)다. 실패 경로(빨강)만 `main()`으로 돌아온다.

### 2.3 예제 6 — 누가 먼저 도나, 그리고 시작 뒤에 만든 태스크

**무엇을 확인하나**: (1) 스케줄러 시작 전 `pxCurrentTCB`가 무엇을 가리키는지, (2) 실제로 첫 번째로 도는 태스크가 무엇인지, (3) 스케줄러가 돌고 있을 때 더 높은/낮은 우선순위 태스크를 만들면 무엇이 바로 일어나는지. `traceTASK_SWITCHED_IN()` 훅으로 스위치 순서를 기록한다.

```c
/* ex6/FreeRTOSConfig.h — template + log of every task switched in */
#ifndef C3_EX6_CONFIG_H
#define C3_EX6_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifdef C3_NOTIMER                        /* variant: no timer daemon task */
#undef  configUSE_TIMERS
#define configUSE_TIMERS 0
#undef  INCLUDE_xTimerPendFunctionCall
#define INCLUDE_xTimerPendFunctionCall 0
#endif
void c3_log_in(const char *name);
#define traceTASK_SWITCHED_IN() c3_log_in(pxCurrentTCB->pcTaskName)
#endif
```

```c
/* ex6: which task runs first, and what happens when a task is created after the start */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static const char *seq[16];
static volatile unsigned nseq;
void c3_log_in(const char *name)
{
    if (nseq < 16 && (nseq == 0 || seq[nseq - 1] != name)) seq[nseq++] = name;
}

static void say(void *pv)                    /* D (prio 4) and E (prio 1) */
{
    board_printf_raw("    %s runs\n", pcTaskGetName(NULL));
    (void)pv;
    vTaskDelete(NULL);
}
static void worker(void *pv)
{
    board_printf_raw("%s runs (prio %u)\n", pcTaskGetName(NULL), (unsigned)uxTaskPriorityGet(NULL));
    if (pv) {                                /* only A does this */
        board_printf_raw("  A: create D (prio 4)...\n");
        xTaskCreate(say, "D", 256, NULL, 4, NULL);
        board_printf_raw("  A: back from xTaskCreate(D); create E (prio 1)...\n");
        xTaskCreate(say, "E", 256, NULL, 1, NULL);
        board_printf_raw("  A: back from xTaskCreate(E)\n");
        vTaskDelay(5);
        board_printf_raw("switch-in order:");
        for (unsigned i = 0; i < nseq; i++) board_printf_raw(" %s", seq[i]);
        board_printf_raw("\n");
        board_exit(0);
    }
    for (;;) vTaskDelay(100);
}

int main(void)
{
    xTaskCreate(worker, "A", 256, (void *)1, 2, NULL);
    xTaskCreate(worker, "B", 256, NULL, 2, NULL);
    xTaskCreate(worker, "C", 256, NULL, 1, NULL);
    board_printf_raw("before start: pxCurrentTCB -> %s\n", pcTaskGetName(NULL));
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex6/main.c OUT=/var/tmp/rtos/C3/ex6 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/ex6/app.elf; echo ==; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex6/main.c OUT=/var/tmp/rtos/C3/ex6nt EXTRA_CFLAGS=-DC3_NOTIMER && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/ex6nt/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12732	    100	 135472	 148304	  24350	/var/tmp/rtos/C3/ex6/app.elf
before start: pxCurrentTCB -> B
A runs (prio 2)
  A: create D (prio 4)...
    D runs
B runs (prio 2)
  A: back from xTaskCreate(D); create E (prio 1)...
  A: back from xTaskCreate(E)
C runs (prio 1)
    E runs
switch-in order: Tmr Svc A D B A C E IDLE A
[board_exit] code=0
==
   text	   data	    bss	    dec	    hex	filename
   9500	    100	 132956	 142556	  22cdc	/var/tmp/rtos/C3/ex6nt/app.elf
before start: pxCurrentTCB -> B
B runs (prio 2)
A runs (prio 2)
  A: create D (prio 4)...
    D runs
  A: back from xTaskCreate(D); create E (prio 1)...
  A: back from xTaskCreate(E)
C runs (prio 1)
    E runs
switch-in order: B A D A C E IDLE A
[board_exit] code=0
```

**출력에서 볼 것**:

- **스케줄러 시작 전 `pxCurrentTCB`는 "지금까지 만든 것 중 우선순위가 가장 높은 것, 같으면 나중에 만든 것"이다.** A와 B가 둘 다 2인데 B다. `prvAddNewTaskToReadyList()`(`tasks.c` 2073행 근처)가 스케줄러가 안 돌 때 `if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority ) pxCurrentTCB = pxNewTCB;` — `<=`라서 같은 우선순위면 새것으로 바꾼다.
- **그래서 timer 태스크가 있으면(기본값) 진짜 첫 태스크는 `Tmr Svc`다**(우선순위 7 = `configMAX_PRIORITIES - 1`, 가장 나중에 만들어짐). 예제 1의 gdb에서 본 `pxCurrentTCB = Tmr Svc`와 같다. 사용자 태스크 중 처음 도는 것은 B가 아니라 **A**다: `Tmr Svc`가 명령 큐에서 막히면 `vTaskSwitchContext()`가 우선순위 2 리스트에서 `listGET_OWNER_OF_NEXT_ENTRY()`로 "다음 항목"을 꺼내는데, 리스트의 인덱스가 끝 표지에 있으니 맨 앞(먼저 넣은 A)이 나온다.
- timer 태스크를 끈 빌드(`C3_NOTIMER`)에서는 `pxCurrentTCB = B`가 그대로 첫 태스크가 된다. **"어느 태스크가 먼저 도나"는 설정에 따라 바뀐다** — 초기화 순서를 "첫 태스크가 먼저 돈다"에 기대면 안 된다는 뜻이다(§9 실무).
- **시작 뒤에 더 높은 우선순위 태스크를 만들면 `xTaskCreate()` 안에서 바로 선점된다**: "A: create D" 다음에 "D runs"가 먼저 찍히고, A는 그 뒤에야 `xTaskCreate()`에서 돌아온다. `prvAddNewTaskToReadyList()`가 스케줄러가 돌고 있으면 `taskYIELD_ANY_CORE_IF_USING_PREEMPTION()`으로 PendSV를 건다. 그 사이 B(같은 우선순위 2)가 time slicing으로 끼어든 것도 보인다.
- 낮은 우선순위 E는 만들어져도 A가 막힐 때까지 기다린다(C 다음).

---

## 3. `xPortStartScheduler()` — CPU 코어를 점검하고 맞추는 곳

> 이 절의 SVC·PendSV·SysTick 우선순위 이야기는 B4가 주인이다. 여기서는 "시작 직전에 포트가 실제로 하는 일"만 본다.

### 3.1 한 줄씩 — `port.c:268`

| 순서 | 코드 | 하는 일 | 왜 |
|---|---|---|---|
| 1 | `configASSERT( pxVectorTable[ 11 ] == vPortSVCHandler )` · `[14] == xPortPendSVHandler` | VTOR(0xE000ED08)이 가리키는 벡터 테이블의 SVC·PendSV 칸이 FreeRTOS 핸들러인지 확인 (`configCHECK_HANDLER_INSTALLATION`, 기본 1) | 벤더 startup 파일의 `SVC_Handler` 이름과 연결을 잊는 것이 포팅 실수 1위. 연결 안 되면 `svc 0`이 기본 핸들러(무한 루프)로 간다 |
| 2 | `*pucFirstUserPriorityRegister = 0xff;` 다시 읽기 | IRQ0의 우선순위 바이트(0xE000E400)에 0xff를 쓰고 읽어 **구현된 비트 수**를 잰다 | 칩마다 우선순위 비트가 3~8개라서. 구현 안 된 하위 비트는 0으로 읽힌다 |
| 3 | `ucMaxSysCallPriority = configMAX_SYSCALL_INTERRUPT_PRIORITY & 읽은값` + assert 3개 | 설정값이 0이 아닌지, 구현 안 된 비트를 쓰지 않았는지, 8비트 칩이면 bit 0(서브 우선순위 비트)이 0인지 | 나중에 `vPortValidateInterruptPriority()`가 ISR 우선순위를 검사할 기준값 |
| 4 | 비트 수 → `ulMaxPRIGROUPValue` | AIRCR PRIGROUP의 허용 최대값을 계산(**쓰지는 않는다**) | 앱이 PRIGROUP을 바꿔 서브 우선순위를 만들면 BASEPRI 마스킹이 어긋나므로 나중에 검사용 |
| 5 | 원래 우선순위 복원 | IRQ0 우선순위 바이트를 되돌린다 | 탐지가 부작용을 남기지 않게 |
| 6 | `SHPR3 = PendSV 0xff, SysTick 0xff` · `SHPR2 = 0` | PendSV·SysTick을 최저, SVCall을 최고(0)로 | PendSV 최저 이유는 B4. SVC가 0이어야 BASEPRI=0x20 상태에서도 `svc 0`이 막히지 않는다(§4.4에서 일부러 깨 본다) |
| 7 | `vPortSetupTimerInterrupt()` | SysTick: CTRL=0, VAL=0, LOAD=`25000000/1000 - 1` = 24999, CTRL=CLK·INT·EN(=7) | tick 시작 (C5). BASEPRI가 막고 있어 아직 안 들어온다 |
| 8 | `uxCriticalNesting = 0` | 0xaaaaaaaa였던 중첩 카운트를 0으로 | 첫 태스크는 "크리티컬 섹션 밖"에서 시작해야 하므로 |
| 9 | `prvPortStartFirstTask()` | §4 | 돌아오지 않는다 |
| 10 | `vTaskSwitchContext(); prvTaskExitError(); return 0;` | 도달하면 안 되는 코드 | 주석: 컴파일러 경고와 LTO가 심볼을 지우는 것을 막기 위해 부른다 |

### 3.2 예제 2 — 우선순위 비트 탐지를 QEMU에서 보고, 4가지 설정 실수를 터뜨리기

**무엇을 확인하나**: 2~4단계가 QEMU에서 무엇을 찾는지 gdb로 보고, 시작한 태스크 안에서 SHPR2·SHPR3·SysTick·BASEPRI·CONTROL 값을 읽는다. 그다음 `configMAX_SYSCALL_INTERRUPT_PRIORITY`를 바꿔 각 assert를 실제로 터뜨린다. QEMU `mps2-an385`는 우선순위 8비트를 다 구현하지만(실칩은 보통 3~4비트), QEMU 10.2의 `-global armv7m.num-prio-bits=N`으로 **STM32처럼 4비트, 3비트 칩을 흉내 낼 수 있다**는 것을 이번에 확인했다(아래 probe 결과).

```c
/* ex2/FreeRTOSConfig.h — template + overridable configMAX_SYSCALL_INTERRUPT_PRIORITY */
#ifndef C3_EX2_CONFIG_H
#define C3_EX2_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifndef C3_MAXSYS
#define C3_MAXSYS 0x20
#endif
#undef configMAX_SYSCALL_INTERRUPT_PRIORITY
#define configMAX_SYSCALL_INTERRUPT_PRIORITY C3_MAXSYS
#endif
```

```c
/* ex2: what xPortStartScheduler programs into the core before the first task */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void first(void *pv)
{
    (void)pv;
    uint32_t basepri, control;
    __asm volatile("mrs %0, basepri" : "=r"(basepri));
    __asm volatile("mrs %0, control" : "=r"(control));
    board_printf_raw("SHPR2=0x%x SHPR3=0x%x SYST_RVR=%u SYST_CSR=0x%x\n",
                     REG32(0xE000ED1Cu), REG32(0xE000ED20u), REG32(0xE000E014u), REG32(0xE000E010u));
    board_printf_raw("in task: BASEPRI=0x%x CONTROL=0x%x AIRCR.PRIGROUP=%u\n",
                     basepri, control, (REG32(0xE000ED0Cu) >> 8) & 7u);
    board_exit(0);
}

int main(void)
{
    board_printf_raw("configMAX_SYSCALL_INTERRUPT_PRIORITY=0x%x\n", configMAX_SYSCALL_INTERRUPT_PRIORITY);
    xTaskCreate(first, "first", 256, NULL, configMAX_PRIORITIES - 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

먼저 RTOS 없이 비트 수만 재는 6줄짜리 probe(`probe.c`: IRQ0 우선순위 바이트에 0xff를 쓰고 읽기)를 세 가지 QEMU 설정으로 돌렸다:

```text
== qemu 
wrote 0xff, read back 0xff -> 8 priority bits, 256 levels
== qemu -global armv7m.num-prio-bits=4
wrote 0xff, read back 0xf0 -> 4 priority bits, 16 levels
== qemu -global armv7m.num-prio-bits=3
wrote 0xff, read back 0xe0 -> 3 priority bits, 8 levels
```

(`[board_exit]` 줄은 생략.) 이제 FreeRTOS가 같은 것을 어떻게 읽는지 gdb로 본다(`port.c` 327행 = 읽은 직후, 384행 = 계산 끝).

```text
break port.c:327
break port.c:384
continue
printf "read-back ucMaxPriorityValue = 0x%02x\n", ucMaxPriorityValue
continue
printf "ulImplementedPrioBits=%u  ulMaxPRIGROUPValue=0x%x  ucMaxSysCallPriority=0x%02x\n", ulImplementedPrioBits, ulMaxPRIGROUPValue, ucMaxSysCallPriority
```

```text
=== qemu 
read-back ucMaxPriorityValue = 0xff
ulImplementedPrioBits=8  ulMaxPRIGROUPValue=0x0  ucMaxSysCallPriority=0x20
=== qemu -global armv7m.num-prio-bits=4
read-back ucMaxPriorityValue = 0xf0
ulImplementedPrioBits=4  ulMaxPRIGROUPValue=0x300  ucMaxSysCallPriority=0x20
```

그리고 gdb 없이 6가지 조합을 돌렸다(ICOUNT, `C3_MAXSYS`를 바꿔 빌드):

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C3; for v in "0x20:" "0x20:4" "0x21:" "0x28:4" "0x00:" "0x10:3"; do m=${v%%:*}; b=${v#*:}; G=""; [ -n "$b" ] && G="-global armv7m.num-prio-bits=$b"; echo "=== MAXSYS=$m prio-bits=${b:-8}"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex2/main.c OUT=/var/tmp/rtos/C3/ex2_$m EXTRA_CFLAGS=-DC3_MAXSYS=$m >/dev/null && timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off $G -kernel /var/tmp/rtos/C3/ex2_$m/app.elf; echo "exit=$?"; done'
```

```text
=== MAXSYS=0x20 prio-bits=8
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x20
SHPR2=0x0 SHPR3=0xffff0000 SYST_RVR=24999 SYST_CSR=0x7
in task: BASEPRI=0x0 CONTROL=0x2 AIRCR.PRIGROUP=0
[board_exit] code=0
exit=0
=== MAXSYS=0x20 prio-bits=4
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x20
SHPR2=0x0 SHPR3=0xf0f00000 SYST_RVR=24999 SYST_CSR=0x7
in task: BASEPRI=0x0 CONTROL=0x2 AIRCR.PRIGROUP=0
[board_exit] code=0
exit=0
=== MAXSYS=0x21 prio-bits=8
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x21

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:369
[board_exit] code=253
exit=1
=== MAXSYS=0x28 prio-bits=4
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x28

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:339
[board_exit] code=253
exit=1
=== MAXSYS=0x00 prio-bits=8
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x0

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:335
[board_exit] code=253
exit=1
=== MAXSYS=0x10 prio-bits=3
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x10

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:335
[board_exit] code=253
exit=1
```

**출력에서 볼 것**:

- **QEMU 기본(8비트)에서 FreeRTOS가 찾은 것**: 읽은 값 0xff → 8비트, `ulMaxPRIGROUPValue = 0`, `ucMaxSysCallPriority = 0x20`. 4비트 칩 흉내에서는 0xf0 → 4비트, PRIGROUP 허용 최대 3(0x300 = 3 << 8). 계산만 하고 **AIRCR에 쓰지는 않는다**(태스크 안에서 읽은 PRIGROUP은 두 경우 다 0).
- 정상 시작 후의 코어 상태: SHPR3 = 0xffff0000(8비트) / **0xf0f00000(4비트 — 쓴 0xff 중 하위 4비트가 사라짐)**, SHPR2 = 0(SVC 최고), SysTick LOAD = 24999(25 MHz / 1 kHz − 1), CSR = 7, 태스크 안 BASEPRI = 0, **CONTROL = 2**(SPSEL = 1: Thread 모드가 PSP를 쓴다, nPRIV = 0: 특권).
- **assert 4개가 각자 다른 실수를 잡는다**:
- `0x21`(8비트 칩): 369행 — 8비트를 다 구현한 칩은 PRIGROUP을 0으로 둬도 bit 0이 서브 우선순위라, 0x21로 설정하면 0x20 인터럽트까지 같은 선점 그룹으로 묶여 같이 막힌다(소스 주석의 "confusion"). 짝수로 쓰라는 뜻이다.
- `0x28`(4비트 칩): 339행 — 구현 안 된 하위 4비트(0x08)를 썼다. STM32에서 "우선순위 5"를 shift 없이 `5`라고 쓰는 실수가 이 assert에 걸린다. 올바른 값은 `5 << (8 - 4)` = 0x50.
- `0x00`: 335행 — BASEPRI = 0은 "아무것도 안 막음"이라 크리티컬 섹션이 성립하지 않는다.
- `0x10`(3비트 칩): 역시 335행 — 0x10 & 0xe0 = 0. **8비트 QEMU에서는 멀쩡하던 설정이 3비트 실칩에서 죽는다.** QEMU만 믿으면 놓치는 버그라서, 실칩의 `__NVIC_PRIO_BITS`로 계산하는 습관(`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - __NVIC_PRIO_BITS)`)이 필요하다.

**Don의 경험과 연결**: Apple에서 새 실리콘을 bring-up할 때 "스펙 문서의 레지스터 폭"과 "실제 실리콘이 구현한 폭"이 다른 일을 겪어 봤을 것이다. FreeRTOS는 문서를 믿지 않고 **써 보고 읽어서** 확인한다 — bring-up 엔지니어가 하는 방식 그대로다.

---

## 4. `prvPortStartFirstTask()` + `vPortSVCHandler` — 21개 명령으로 Thread 모드에 들어가기

### 4.1 소스 — 두 함수 모두 `naked`(컴파일러가 프롤로그·에필로그를 붙이지 않는다)

```asm
prvPortStartFirstTask:              @ port.c:247
    ldr r0, =0xE000ED08             @ SCB->VTOR 주소
    ldr r0, [r0]                    @ r0 = 벡터 테이블 주소
    ldr r0, [r0]                    @ r0 = vector[0] = 리셋 때의 초기 MSP
    msr msp, r0                     @ MSP를 맨 처음 값으로 되돌린다 (main 스택 폐기)
    cpsie i                         @ PRIMASK = 0
    cpsie f                         @ FAULTMASK = 0
    dsb
    isb
    svc 0                           @ SVCall 예외 → vPortSVCHandler
    nop

vPortSVCHandler:                    @ port.c:228
    ldr r3, =pxCurrentTCB           @ r3 = &pxCurrentTCB
    ldr r1, [r3]                    @ r1 = pxCurrentTCB (TCB 주소)
    ldr r0, [r1]                    @ r0 = TCB->pxTopOfStack (첫 멤버)
    ldmia r0!, {r4-r11}             @ 소프트웨어 몫 8워드 꺼내기, r0 += 32
    msr psp, r0                     @ PSP = 하드웨어 프레임 시작
    isb
    mov r0, #0
    msr basepri, r0                 @ BASEPRI = 0: 모든 인터럽트 허용
    orr r14, #0xd                   @ LR: 0xFFFFFFF9 → 0xFFFFFFFD
    bx r14                          @ 예외 복귀: PSP에서 8워드 꺼내고 Thread 모드로
```

명령마다 "왜"를 붙이면:

- **`ldr r0, [r0]` 두 번**: 한 번은 VTOR 레지스터 값(테이블 주소), 한 번은 테이블 0번 칸(초기 SP). 링커 심볼 `_estack`을 직접 쓰지 않고 VTOR를 따라가는 이유는 **부트로더 뒤에 앱이 있어 테이블이 0번지가 아닐 때도** 맞는 값을 얻기 위해서다.
- **`msr msp, r0`**: 이제부터 MSP는 "인터럽트 전용 스택"이 된다(태스크는 PSP를 쓴다). main 함수·`vTaskStartScheduler()`·`xPortStartScheduler()`가 쌓은 프레임은 어차피 다시 돌아갈 일이 없으니 그 RAM을 ISR에게 돌려준다. 결과는 예제 4에서 본다.
- **`cpsie i` / `cpsie f`**: PRIMASK와 FAULTMASK를 푼다. 그런데 아직 **BASEPRI = 0x20**이라 우선순위 0x20 이하(숫자가 큰) 인터럽트 — SysTick, PendSV, 커널을 부르는 ISR — 는 여전히 막혀 있다. 열리는 것은 0x00~0x1F(커널을 부르지 않는 zero-latency ISR)와 SVC(우선순위 0)뿐이다. `cpsie`는 혹시 앞선 코드(부트로더 등)가 PRIMASK를 닫아 두었을 때 SVC가 막히지 않게 하는 보험이기도 하다.
- **`dsb` / `isb`**: 앞의 MSP 변경과 마스크 해제가 확실히 끝난 뒤 `svc`를 실행하게 한다(ARMv7-M에서 `msr`·`cpsie` 뒤 즉시 효과를 보장하는 권장 장벽).
- **`svc 0`**: 동기 예외. 하드웨어가 **현재 스택(MSP)**에 8워드를 쌓고, LR = `0xFFFFFFF9`(Thread 모드 + MSP에서 왔다), Handler 모드로 들어간다. 번호 0은 이 포트에서 의미가 없다(핸들러가 번호를 보지 않는다). 다른 SVC 번호를 쓰는 포트(ARM_CM3_MPU, CM33 등)는 번호로 분기한다(C8).
- **`ldr r1, [r3]` → `ldr r0, [r1]`**: `pxCurrentTCB` → TCB → 첫 멤버 `pxTopOfStack`. TCB 첫 멤버가 SP인 이유가 이 두 명령이다(A4 §2.4).
- **`ldmia r0!, {r4-r11}`**: 위조 프레임 아래쪽 8워드(전부 0xa5a5a5a5)를 R4–R11에 넣고 r0를 32 올린다. PendSV의 복원 절반과 똑같은 명령이다(C2) — 그래서 프레임 모양을 맞춰 둔 것이다.
- **`msr psp, r0`**: PSP = 하드웨어 프레임의 시작. 예외 복귀가 여기서 8워드를 꺼낼 것이다.
- **`msr basepri, #0`**: 이제야 모든 인터럽트를 연다. 첫 태스크는 인터럽트가 열린 상태로 시작해야 한다(프레임에 BASEPRI를 저장하지 않는 포트라서 여기서 직접 0으로).
- **`orr r14, #0xd`**: 0xFFFFFFF9 | 0xD = **0xFFFFFFFD**. EXC_RETURN의 bit 2(SPSEL) = 1 → "PSP로 복귀", bit 3 = 1 → "Thread 모드로", bit 0 = 1. 이 한 비트(bit 2)가 MSP 세계에서 PSP 세계로 넘어가는 스위치다. 복귀 때 CPU가 CONTROL.SPSEL도 1로 맞춘다.
- **`bx r14`**: 예외 복귀. PSP에서 R0, R1, R2, R3, R12, LR, PC, xPSR을 꺼내 PC로 점프한다 — 그 PC가 첫 태스크 함수다.

### 4.2 예제 3 — gdb `stepi`로 21개 명령과 레지스터를 전부 보기

**무엇을 확인하나**: 위 설명이 실제 레지스터 값으로 맞는지. 예제 1과 같은 ELF에서 `prvPortStartFirstTask`에 브레이크를 걸고 한 명령씩 실행하며 PC·R0·R1·R3·R4·LR·MSP·PSP·BASEPRI·PRIMASK·CONTROL·IPSR을 찍는다. §0에서 말한 이유로 이 예제만 `sleep=on` icount로 돌렸다.

```text
break prvPortStartFirstTask
continue
define sh
  printf "pc=%08x  r0=%08x r1=%08x r3=%08x r4=%08x lr=%08x msp=%08x psp=%08x basepri=%02x primask=%x control=%x ipsr=%u\n", $pc, $r0, $r1, $r3, $r4, $lr, $msp, $psp, $basepri, $primask, $control, $xpsr & 0x1ff
end
sh
set $n = 0
while $n < 21
  x/i $pc
  if $pc == 0x1cb2
    printf "hw frame at psp: "
    x/8wx $psp
  end
  stepi
  sh
  set $n = $n + 1
end
info symbol $pc
x/8wx $msp
x/2wx 0x20400000-8
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/C3 && IC="-icount shift=5,align=off,sleep=on" ./gdbrun.sh /var/tmp/rtos/C3/ex1/app.elf ex3.gdb'
```

gdb가 매 단계 찍는 소스 줄 표시와 브레이크 안내는 지웠고, 나머지는 그대로다:

```text
pc=00001cb8 r0=000061a7 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=203fffa8 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cb8 <prvPortStartFirstTask>:	ldr	r0, [pc, #24]	@ (0x1cd4 <prvPortStartFirstTask+28>)
pc=00001cba r0=e000ed08 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=203fffa8 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cba <prvPortStartFirstTask+2>:	ldr	r0, [r0, #0]
pc=00001cbc r0=00000000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=203fffa8 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cbc <prvPortStartFirstTask+4>:	ldr	r0, [r0, #0]
pc=00001cbe r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=203fffa8 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cbe <prvPortStartFirstTask+6>:	msr	MSP, r0
pc=00001cc2 r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=20400000 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cc2 <prvPortStartFirstTask+10>:	cpsie	i
pc=00001cc4 r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=20400000 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cc4 <prvPortStartFirstTask+12>:	cpsie	f
pc=00001cc6 r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=20400000 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cc6 <prvPortStartFirstTask+14>:	dsb	sy
pc=00001cca r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=20400000 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cca <prvPortStartFirstTask+18>:	isb	sy
pc=00001cce r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=00001f0f msp=20400000 psp=00000000 basepri=20 primask=0 control=0 ipsr=0
=> 0x1cce <prvPortStartFirstTask+22>:	svc	0
pc=00001c94 r0=20400000 r1=00000007 r3=20000008 r4=00000000 lr=fffffff9 msp=203fffe0 psp=00000000 basepri=20 primask=0 control=0 ipsr=11
=> 0x1c94 <SVC_Handler>:	ldr	r3, [pc, #28]	@ (0x1cb4 <SVC_Handler+32>)
pc=00001c96 r0=20400000 r1=00000007 r3=20000e6c r4=00000000 lr=fffffff9 msp=203fffe0 psp=00000000 basepri=20 primask=0 control=0 ipsr=11
=> 0x1c96 <SVC_Handler+2>:	ldr	r1, [r3, #0]
pc=00001c98 r0=20400000 r1=20000868 r3=20000e6c r4=00000000 lr=fffffff9 msp=203fffe0 psp=00000000 basepri=20 primask=0 control=0 ipsr=11
=> 0x1c98 <SVC_Handler+4>:	ldr	r0, [r1, #0]
pc=00001c9a r0=20000820 r1=20000868 r3=20000e6c r4=00000000 lr=fffffff9 msp=203fffe0 psp=00000000 basepri=20 primask=0 control=0 ipsr=11
=> 0x1c9a <SVC_Handler+6>:	ldmia.w	r0!, {r4, r5, r6, r7, r8, r9, r10, r11}
pc=00001c9e r0=20000840 r1=20000868 r3=20000e6c r4=a5a5a5a5 lr=fffffff9 msp=203fffe0 psp=00000000 basepri=20 primask=0 control=0 ipsr=11
=> 0x1c9e <SVC_Handler+10>:	msr	PSP, r0
pc=00001ca2 r0=20000840 r1=20000868 r3=20000e6c r4=a5a5a5a5 lr=fffffff9 msp=203fffe0 psp=20000840 basepri=20 primask=0 control=0 ipsr=11
=> 0x1ca2 <SVC_Handler+14>:	isb	sy
pc=00001ca6 r0=20000840 r1=20000868 r3=20000e6c r4=a5a5a5a5 lr=fffffff9 msp=203fffe0 psp=20000840 basepri=20 primask=0 control=0 ipsr=11
=> 0x1ca6 <SVC_Handler+18>:	mov.w	r0, #0
pc=00001caa r0=00000000 r1=20000868 r3=20000e6c r4=a5a5a5a5 lr=fffffff9 msp=203fffe0 psp=20000840 basepri=20 primask=0 control=0 ipsr=11
=> 0x1caa <SVC_Handler+22>:	msr	BASEPRI, r0
pc=00001cae r0=00000000 r1=20000868 r3=20000e6c r4=a5a5a5a5 lr=fffffff9 msp=203fffe0 psp=20000840 basepri=00 primask=0 control=0 ipsr=11
=> 0x1cae <SVC_Handler+26>:	orr.w	lr, lr, #13
pc=00001cb2 r0=00000000 r1=20000868 r3=20000e6c r4=a5a5a5a5 lr=fffffffd msp=203fffe0 psp=20000840 basepri=00 primask=0 control=0 ipsr=11
=> 0x1cb2 <SVC_Handler+30>:	bx	lr
hw frame at psp: 0x20000840 <uxTimerTaskStack.0+2008>:	0x00000000	0xa5a5a5a5	0xa5a5a5a5	0xa5a5a5a5
0x20000850 <uxTimerTaskStack.0+2024>:	0xa5a5a5a5	0x00001d1d	0x00001734	0x01000000
pc=00001734 r0=00000000 r1=a5a5a5a5 r3=a5a5a5a5 r4=a5a5a5a5 lr=00001d1d msp=203fffe0 psp=20000860 basepri=00 primask=0 control=2 ipsr=0
=> 0x1734 <prvTimerTask>:	stmdb	sp!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
pc=00001738 r0=00000000 r1=a5a5a5a5 r3=a5a5a5a5 r4=a5a5a5a5 lr=00001d1d msp=203fffe0 psp=2000083c basepri=00 primask=0 control=2 ipsr=0
=> 0x1738 <prvTimerTask+4>:	mov.w	r10, #0
pc=0000173c r0=00000000 r1=a5a5a5a5 r3=a5a5a5a5 r4=a5a5a5a5 lr=00001d1d msp=203fffe0 psp=2000083c basepri=00 primask=0 control=2 ipsr=0
0x203fffe0:	0x20400000	0x00000007	0x00000000	0x20000008
0x203ffff0:	0xffffffff	0x00001f0f	0x00001cd0	0x21000000
0x203ffff8:	0x00001cd0	0x21000000
```

**출력에서 볼 것** (단계 번호는 위에서부터):

| 단계 | 명령 | 바뀐 값 | 의미 |
|---|---|---|---|
| 1–3 | `ldr` ×3 | r0: `e000ed08` → `00000000`(VTOR) → `20400000`(vector[0]) | 이 보드의 벡터 테이블은 0번지, 초기 MSP = RAM 끝 |
| 4 | `msr MSP` | msp: `203fffa8` → **`20400000`** | main 쪽 88바이트 스택이 그 자리에서 버려진다 |
| 5–8 | `cpsie i/f`, `dsb`, `isb` | (변화 없음: PRIMASK는 이미 0) | **basepri는 여전히 20** — tick은 아직 못 들어온다 |
| 9 | `svc 0` | ipsr: 0 → **11**(SVCall), lr = **`fffffff9`**, msp: `20400000` → **`203fffe0`** | 하드웨어가 MSP에 32바이트 프레임을 쌓았다 |
| 10–12 | `ldr` ×3 | r3 = `&pxCurrentTCB` (0x20000e6c), r1 = TCB `Tmr Svc` (0x20000868), r0 = `0x20000820` | 예제 1의 `pxTopOfStack`과 같은 값 |
| 13 | `ldmia` | r4 = `a5a5a5a5`, r0 = `20000840` (+32) | 소프트웨어 몫 8워드 복원 |
| 14 | `msr PSP` | psp: 0 → **`20000840`** | 하드웨어 프레임(R0=0, LR=0x1d1d, PC=0x1734, xPSR=0x01000000)을 가리킨다 |
| 16–17 | `mov`, `msr BASEPRI` | basepri: 20 → **00** | 이제 모든 인터럽트가 열린다 |
| 18 | `orr` | lr: `fffffff9` → **`fffffffd`** | "Thread 모드, PSP로 복귀" |
| 19 | `bx lr` | pc = **`0x1734` prvTimerTask**, ipsr = 0, **control = 2**, psp = **`20000860`** | 프레임 8워드가 꺼내졌고(R0=0, LR=prvTaskExitError+1) Thread 모드 + PSP로 왔다 |
| 20 | `stmdb sp!, ...` | psp: `20000860` → `2000083c` | 태스크 함수 프롤로그가 **PSP**에 push한다. MSP는 그대로 |

그리고 마지막 덤프: **MSP는 `0x203fffe0`에 머물러 있다.** SVC가 쌓은 프레임(R0=0x20400000, R1=7, R2=0, R3=0x20000008, R12=0xffffffff, LR=0x1f0f, PC=0x1cd0 = `svc` 다음 `nop`, xPSR=0x21000000)은 PSP로 복귀했기 때문에 **아무도 꺼내지 않는다.** 예외 복귀는 EXC_RETURN이 가리킨 스택(PSP)에서만 꺼낸다. 이후 모든 ISR은 MSP = 0x203fffe0에서 시작하니, **인터럽트 스택의 맨 위 32바이트는 영원히 쓰이지 않는 자리**가 된다. 그 위 `0x203ffff8`의 두 워드도 SVC 프레임의 PC·xPSR이다(아래쪽은 x/2wx로 겹쳐 본 것). MSP 크기를 아슬아슬하게 잡을 때 이 32바이트를 기억해 둔다.

### 4.3 그림 — MSP와 PSP, 전과 후

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="20" font-size="13" text-anchor="middle" font-weight="bold">MSP 영역 (RAM 끝, 0x20400000 아래)</text>
<text x="510" y="20" font-size="13" text-anchor="middle" font-weight="bold">PSP: Tmr Svc 태스크 스택</text>
<text x="96" y="46" font-size="12" text-anchor="end">0x20400000</text>
<line x1="100" y1="42" x2="300" y2="42" stroke="currentColor"/>
<rect x="100" y="42" width="200" height="70" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="200" y="66" font-size="12" text-anchor="middle">SVC 하드웨어 프레임 32 B</text>
<text x="200" y="84" font-size="12" text-anchor="middle">PC=0x1cd0 xPSR=0x21000000</text>
<text x="200" y="102" font-size="12" text-anchor="middle">아무도 꺼내지 않음</text>
<text x="96" y="116" font-size="12" text-anchor="end">0x203fffe0</text>
<rect x="100" y="112" width="200" height="80" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="200" y="138" font-size="12" text-anchor="middle">Reset_Handler · main ·</text>
<text x="200" y="156" font-size="12" text-anchor="middle">vTaskStartScheduler 프레임</text>
<text x="200" y="174" font-size="12" text-anchor="middle">(버려짐 → ISR이 덮어씀)</text>
<text x="96" y="196" font-size="12" text-anchor="end">0x203fffa8</text>
<rect x="100" y="192" width="200" height="60" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-dasharray="4 3"/>
<text x="200" y="226" font-size="12" text-anchor="middle">ISR이 쓸 인터럽트 스택</text>
<line x1="310" y1="196" x2="330" y2="196" stroke="#888"/><text x="316" y="190" font-size="12">①</text>
<line x1="310" y1="42" x2="330" y2="42" stroke="#888"/><text x="316" y="36" font-size="12">②</text>
<line x1="310" y1="112" x2="330" y2="112" stroke="#d0564a"/><text x="316" y="106" font-size="12">③</text>
<text x="20" y="280" font-size="12">① svc 직전까지 MSP = 0x203fffa8 (main 쪽 88 B 사용)</text>
<text x="20" y="298" font-size="12">② msr msp: 0x20400000 (vector[0])</text>
<text x="20" y="316" font-size="12">③ svc 0 진입 뒤 MSP = 0x203fffe0, 이후 계속 이 값</text>
<text x="436" y="56" font-size="12" text-anchor="end">0x20000868</text>
<rect x="440" y="42" width="200" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="540" y="62" font-size="12" text-anchor="middle">TCB Tmr Svc (위쪽)</text>
<text x="436" y="86" font-size="12" text-anchor="end">0x20000860</text>
<rect x="440" y="72" width="200" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="540" y="98" font-size="12" text-anchor="middle">HW 8워드: R0=0 … PC=0x1734</text>
<text x="540" y="116" font-size="12" text-anchor="middle">bx 0xFFFFFFFD가 꺼냄</text>
<text x="436" y="146" font-size="12" text-anchor="end">0x20000840</text>
<rect x="440" y="132" width="200" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="540" y="158" font-size="12" text-anchor="middle">R4–R11 = a5a5a5a5</text>
<text x="540" y="176" font-size="12" text-anchor="middle">ldmia r0!가 꺼냄</text>
<text x="436" y="206" font-size="12" text-anchor="end">0x20000820</text>
<rect x="440" y="192" width="200" height="60" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="540" y="226" font-size="12" text-anchor="middle">태스크가 쓸 빈 스택</text>
<line x1="650" y1="206" x2="650" y2="146" stroke="#e08a3c" marker-end="url(#ah4)"/>
<line x1="664" y1="146" x2="664" y2="86" stroke="#4a7bd0" marker-end="url(#ah4)"/>
<text x="360" y="280" font-size="12">TCB.pxTopOfStack = 0x20000820</text>
<text x="360" y="298" font-size="12">msr psp 직후 PSP = 0x20000840</text>
<text x="360" y="316" font-size="12">예외 복귀 뒤 PSP = 0x20000860, CONTROL = 2</text>
<text x="360" y="334" font-size="12">첫 push(stmdb) 뒤 PSP = 0x2000083c</text>
<rect x="20" y="352" width="640" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="374" font-size="12" text-anchor="middle">시작 뒤: 태스크 = Thread 모드 + PSP(각자), ISR·핸들러 = Handler 모드 + MSP(공용)</text>
<text x="340" y="394" font-size="12" text-anchor="middle">main()이 MSP 위에 남긴 지역 변수는 더 이상 누구의 것도 아니다 (예제 4)</text>
</svg>
```

그림 4 — 예제 3의 실제 값으로 그린 MSP·PSP 전후. 왼쪽 MSP는 리셋 값으로 돌아갔다가 SVC 프레임 32바이트만큼 내려간 채로 남고, 오른쪽 PSP는 위조 프레임 16워드를 다 꺼낸 자리(0x20000860)에서 태스크가 시작한다.

### 4.4 예제 3b — SVC 우선순위가 BASEPRI에 막히면?

**무엇을 확인하나**: §3의 `SHPR2 = 0`이 왜 필요한지. gdb로 `prvPortStartFirstTask` 입구에서 SVCall 우선순위를 0xff로 바꿔 놓고(BASEPRI = 0x20보다 낮은 우선순위) 계속 실행한다.

```text
break prvPortStartFirstTask
continue
set *(uint32_t *)0xE000ED1C = 0xff000000
printf "SHPR2 forced to 0x%08x (SVCall priority 0xff), BASEPRI=0x%02x\n", *(uint32_t *)0xE000ED1C, $basepri
break board_hardfault
continue
printf "HFSR=0x%08x CFSR=0x%08x  stacked PC=0x%08x\n", *(uint32_t *)0xE000ED2C, *(uint32_t *)0xE000ED28, ((uint32_t *)$r0)[6]
info symbol ((uint32_t *)$r0)[6]
```

```text
Breakpoint 1, prvPortStartFirstTask () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:249
SHPR2 forced to 0xff000000 (SVCall priority 0xff), BASEPRI=0x20
Breakpoint 2, board_hardfault (frame=0x203fffe0) at /var/rtoslab/templates/common/board.c:90
HFSR=0x40000000 CFSR=0x00000000  stacked PC=0x00001cd0
prvPortStartFirstTask + 24 in section .text
```

**출력에서 볼 것**: `svc`는 동기 예외라서 "나중에 처리"가 없다. 마스크에 막히면 그 자리에서 **HardFault로 승격(escalation)**된다 — HFSR = 0x40000000(FORCED), CFSR = 0(다른 원인 없음), 쌓인 PC = 0x1cd0(`svc` 바로 다음). 현장에서 "스케줄러 시작하자마자 HardFault, HFSR.FORCED, PC = `prvPortStartFirstTask` 근처"면 SVC 우선순위를 건드린 코드(벤더 HAL의 `NVIC_SetPriority(SVCall_IRQn, ...)`, 부트로더가 남긴 SHPR2)를 의심한다. HardFault 해독법은 M1.

### 4.5 예제 4 — main 스택 위의 변수는 어떻게 되나

**무엇을 확인하나**: `main()`의 지역 배열 주소를 태스크에 넘기고, 스케줄러가 시작된 뒤 그 내용을 다시 읽는다. "`main`은 끝나지 않았으니 지역 변수는 살아 있겠지"라는 직관이 틀렸다는 것을 보인다.

```c
/* ex4: a variable on main()'s stack after vTaskStartScheduler() — who owns that RAM now? */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void dump(const char *when, volatile uint32_t *b)
{
    board_printf_raw("%s:", when);
    for (int i = 0; i < 12; i++) board_printf_raw(" %x", b[i]);
    board_printf_raw("\n");
}

static void user(void *pv)
{
    volatile uint32_t *b = pv;               /* points into main()'s old stack frame */
    uint32_t msp;
    __asm volatile("mrs %0, msp" : "=r"(msp));
    board_printf_raw("task: MSP now 0x%x, buffer at 0x%x..0x%x\n", msp, (unsigned)b, (unsigned)&b[12]);
    dump("task start  ", b);
    vTaskDelay(pdMS_TO_TICKS(3));            /* let a few SysTick ISRs run on MSP */
    dump("after 3 ticks", b);
    board_exit(0);
}

int main(void)
{
    volatile uint32_t buf[12];               /* lives on MSP, near the top of RAM */
    for (int i = 0; i < 12; i++) buf[i] = 0x1111u * (i + 1);
    uint32_t msp;
    __asm volatile("mrs %0, msp" : "=r"(msp));
    board_printf_raw("main: MSP 0x%x, &buf 0x%x, vector[0] (initial MSP) 0x%x\n",
                     msp, (unsigned)buf, REG32(REG32(0xE000ED08u)));  /* VTOR -> vector[0] */
    dump("main        ", buf);
    xTaskCreate(user, "user", 256, (void *)buf, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/C3 && ./run.sh ex4'
```

```text
   text	   data	    bss	    dec	    hex	filename
  11984	    100	 135400	 147484	  2401c	/var/tmp/rtos/C3/ex4/app.elf
main: MSP 0x203fffb0, &buf 0x203fffb8, vector[0] (initial MSP) 0x20400000
main        : 1111 2222 3333 4444 5555 6666 7777 8888 9999 aaaa bbbb cccc
task: MSP now 0x203fffe0, buffer at 0x203fffb8..0x203fffe8
task start  : 1111 2222 3333 4444 0 0 20000fb4 1dd9 20000e6c fffffffd 20400000 7
after 3 ticks: 20000d70 e000e000 a5a5a5a5 a5a5a5a5 20000dcc 20000d70 e000e000 1dd9 20000e6c fffffffd 20400000 7
[board_exit] code=0
exit=0
```

**출력에서 볼 것**:

- `buf`는 0x203fffb8~0x203fffe8, 즉 초기 MSP 0x20400000 바로 아래에 있다.
- **태스크가 처음 읽었을 때 이미 뒤쪽 8워드가 바뀌었다.** `buf[10..11]` = `20400000 7`은 예제 3에서 본 **SVC 프레임의 R0·R1**(0x203fffe0부터)이다. 그 아래 `20000e6c fffffffd`는 이 태스크보다 먼저 돈 `Tmr Svc`가 막힐 때 PendSV 핸들러가 MSP에 저장한 R3(`&pxCurrentTCB`)·LR(EXC_RETURN) 등이다.
- 3 tick 뒤에는 앞쪽까지 다 덮였다: SysTick 핸들러 → `xTaskIncrementTick()` 호출 체인이 MSP를 더 깊이 쓴다(`e000e000`은 SCS 베이스 주소 같은 핸들러의 지역 값, `a5a5a5a5`는 어떤 핸들러가 레지스터를 저장한 것).
- 그리고 태스크 안에서 읽은 MSP = 0x203fffe0 — 예제 3에서 본 "SVC 프레임 32바이트만큼 내려간 MSP"가 그대로다.

**교훈**: `vTaskStartScheduler()` 전에 `main()`에서 만든 지역 변수(버퍼, 큐 저장소, `StaticTask_t`, 설정 구조체)를 태스크에게 넘기면 안 된다. **`static`이나 전역으로** 만든다. 이 버그는 디버그 빌드에서는 우연히 살아남고(스택 위치가 달라서) 최적화 빌드나 인터럽트가 많아진 버전에서 갑자기 터진다. Zephyr는 사정이 다르다 — main이 처음부터 스레드라 main의 지역 변수는 main 스레드 스택에 있고, main이 돌아가기 전까지 살아 있다(§7).

---

## 5. 무엇이 잘못될 수 있나 — 네 가지 "만약"

### 5.1 태스크 함수가 return하면 — A4에서 봤다

위조 프레임의 LR = `prvTaskExitError`이므로 return은 그 함수로의 점프가 된다. `configASSERT`가 있으면 즉시 assert, 없으면 `portDISABLE_INTERRUPTS()`(BASEPRI = 0x20) 뒤 무한 루프라 tick·스위치가 다 멈춘다. 실행 결과와 엉터리 backtrace는 **A4 §4.3–4.4**에 있으니 반복하지 않는다. 이 노트에서 덧붙일 것은 두 가지다.

- 예제 1에서 본 것처럼 **idle·timer 태스크도 같은 LR을 갖는다**. 커널 태스크는 return하지 않으니 실제로 쓰이지 않을 뿐이다.
- Zephyr는 위조 프레임의 PC를 사용자 함수가 아니라 래퍼 `z_thread_entry`로 두고 사용자 함수를 R0에 넣는다. 래퍼가 사용자 함수를 부르고, 돌아오면 `k_thread_abort(k_current_get())`를 부른다. 그래서 return이 정상 종료다(§7, 예제 9의 "worker state after return: dead").

### 5.2 예제 5 — heap이 모자라 스케줄러 시작이 실패하면

**무엇을 확인하나**: idle·timer 태스크를 heap에서 만드는 설정(`configSUPPORT_STATIC_ALLOCATION 0`)에서 `configTOTAL_HEAP_SIZE`를 작게 하면 `vTaskStartScheduler()`가 **돌아온다**는 것, 그리고 어느 태스크에서 실패했는지. 템플릿의 malloc-failed 훅과 `configASSERT`는 QEMU를 끝내 버리므로, 이 예제에서는 둘 다 "찍고 계속"으로 바꿨다. 마지막에는 템플릿 기본(정적 할당)으로 같은 작은 heap을 돌려 비교한다.

```c
/* ex5/FreeRTOSConfig.h — idle/timer from heap (C3_DYN=1) or kernel static memory (C3_DYN=0); non-fatal assert */
#ifndef C3_EX5_CONFIG_H
#define C3_EX5_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#if C3_DYN
#undef  configSUPPORT_STATIC_ALLOCATION
#define configSUPPORT_STATIC_ALLOCATION     0
#undef  configKERNEL_PROVIDED_STATIC_MEMORY
#define configKERNEL_PROVIDED_STATIC_MEMORY 0
#endif
#define traceTASK_CREATE(t)      board_printf_raw("  created %s (prio %u)\n", (t)->pcTaskName, (unsigned)(t)->uxPriority)
#undef  configASSERT                       /* print and keep going, so we can see the return */
#define configASSERT(x) do { if (!(x)) board_printf_raw("[configASSERT] %s:%d\n", __FILE__, __LINE__); } while (0)
#endif
```

```c
/* ex5: configTOTAL_HEAP_SIZE too small -> vTaskStartScheduler() returns */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

void vApplicationMallocFailedHook(void)     /* overrides the template's fatal hook */
{
    board_printf_raw("  [MallocFailedHook] free=%u\n", (unsigned)xPortGetFreeHeapSize());
}

static void app(void *pv) { (void)pv; board_printf_raw("app task running\n"); board_exit(0); }

int main(void)
{
    BaseType_t r = xTaskCreate(app, "app", 128, NULL, 2, NULL);
    board_printf_raw("heap=%u B, idle/timer %s; xTaskCreate(app)=%d, free after=%u\n",
                     (unsigned)configTOTAL_HEAP_SIZE, C3_DYN ? "from heap" : "static",
                     (int)r, (unsigned)xPortGetFreeHeapSize());
    vTaskStartScheduler();
    board_printf_raw("vTaskStartScheduler() RETURNED, xTaskGetSchedulerState()=%d free=%u\n",
                     (int)xTaskGetSchedulerState(), (unsigned)xPortGetFreeHeapSize());
    board_exit(2);
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "1:1500" "1:3000" "1:5000" "0:1500"; do d=${v%%:*}; h=${v#*:}; echo "=== C3_DYN=$d heap=$h"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex5/main.c OUT=/var/tmp/rtos/C3/ex5_${d}_$h EXTRA_CFLAGS="-DC3_DYN=$d -DconfigTOTAL_HEAP_SIZE=$h" | grep -v filename; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/ex5_${d}_$h/app.elf; echo "exit=$?"; done 2>&1' | grep -v "^  1[0-9]*\s"
```

```text
=== C3_DYN=1 heap=1500
  created app (prio 2)
heap=1500 B, idle/timer from heap; xTaskCreate(app)=1, free after=864
  [MallocFailedHook] free=864
[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3832
vTaskStartScheduler() RETURNED, xTaskGetSchedulerState()=1 free=864
[board_exit] code=2
exit=1
=== C3_DYN=1 heap=3000
  created app (prio 2)
heap=3000 B, idle/timer from heap; xTaskCreate(app)=1, free after=2368
  created IDLE (prio 0)
  [MallocFailedHook] free=976
[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3832
vTaskStartScheduler() RETURNED, xTaskGetSchedulerState()=1 free=976
[board_exit] code=2
exit=1
=== C3_DYN=1 heap=5000
  created app (prio 2)
heap=5000 B, idle/timer from heap; xTaskCreate(app)=1, free after=4368
  created IDLE (prio 0)
  created Tmr Svc (prio 7)
app task running
[board_exit] code=0
exit=0
=== C3_DYN=0 heap=1500
  created app (prio 2)
heap=1500 B, idle/timer static; xTaskCreate(app)=1, free after=864
  created IDLE (prio 0)
  created Tmr Svc (prio 7)
app task running
[board_exit] code=0
exit=0
```

(size 출력 줄은 grep으로 뺐다.)

**출력에서 볼 것**:

- **1500 B**: `app`(TCB + 128워드 스택 = 636 B 사용, 남은 864 B)은 만들어졌지만 IDLE(스택 256워드 = 1 KB)을 못 만든다. malloc-failed 훅 → `tasks.c:3832`의 `configASSERT( xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY )` → **`vTaskStartScheduler()`가 main으로 돌아왔다.** 상태는 1 = `taskSCHEDULER_NOT_STARTED`.
- **3000 B**: IDLE은 만들어졌고, timer 데몬(스택 `configTIMER_TASK_STACK_DEPTH` = 512워드 = 2 KB)에서 실패한다. 남은 976 B — idle 1 KB + TCB + timer 명령 큐가 그 사이에 쓰였다.
- **5000 B**: 다 만들어지고 정상 시작.
- **정적 할당(`C3_DYN=0`)이면 1500 B로도 시작된다.** idle·timer가 heap을 쓰지 않으니까. 양산 펌웨어가 `configSUPPORT_STATIC_ALLOCATION 1` + `configKERNEL_PROVIDED_STATIC_MEMORY`(또는 앱이 `vApplicationGetIdleTaskMemory()`를 직접 구현)를 선호하는 이유 중 하나다 — "부팅이 heap 상태에 달려 있지 않다"(F1).
- **`configASSERT`가 정의되어 있으면 이 경로에서 이미 멈추고, 없으면 조용히 돌아온다.** 그래서 `vTaskStartScheduler()` 다음 줄에 반드시 "여기 오면 안 됨" 처리를 둔다(로그 + 안전 상태 + 리셋). `return 1;`만 두면 템플릿에서는 `board_exit(1)`이지만 실칩 startup 코드에서는 `main`이 돌아간 뒤 무엇을 하는지 아무도 모른다(B5).

### 5.3 시작 뒤에 태스크를 만들면

예제 6이 보였다: 스케줄러가 돌고 있으면 `xTaskCreate()`가 새 태스크를 Ready 리스트에 넣고, 새 것이 지금 것보다 높으면 **그 자리에서 PendSV로 양보**한다. 그래서 "만들고 나서 핸들을 전역 변수에 저장"하는 코드는 순서가 위험하다 — 새 태스크가 먼저 돌아 아직 NULL인 핸들을 쓸 수 있다. 핸들은 `xTaskCreate()`의 마지막 인자로 받는데, 그 대입은 `prvInitialiseNewTask()` 끝(`*pxCreatedTask = pxNewTCB`)에서 **Ready 리스트에 넣기 전에** 일어나니 핸들 자체는 안전하다. 위험한 것은 핸들 말고 "생성 뒤에 초기화하려던 다른 공유 데이터"다. 높은 우선순위 태스크가 쓸 데이터는 생성 **전에** 준비한다.

### 5.4 예제 7 — 스케줄러 시작 전에 인터럽트가 FromISR을 부르면

**무엇을 확인하나**: 예제 1에서 본 "첫 API 호출부터 BASEPRI = 0x20, `uxCriticalNesting` = 0xaaaaaaaa"가 실제로 인터럽트에 무엇을 하는지. TIMER0을 100 µs 주기로 켜고, ISR에서 `vTaskNotifyGiveFromISR()`을 부른다. 우선순위 0x80(커널 관리 대상, 정상)과 0x10(`configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높음, 규칙 위반), 그리고 "핸들이 NULL인지 확인하지 않는 ISR"을 비교한다.

```c
/* ex7: an interrupt that fires before/while the scheduler is being started and calls FromISR */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#ifndef C3_PRIO
#define C3_PRIO 0x80                         /* NVIC priority of TIMER0 (0x20 = MAX_SYSCALL) */
#endif
static TaskHandle_t volatile h;              /* NULL until xTaskCreate() returns */
static volatile unsigned n_irq, n_given;

void TIMER0_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    n_irq++;
#ifndef C3_NOCHECK
    if (h != NULL)
#endif
    { vTaskNotifyGiveFromISR(h, &woken); n_given++; }
    portYIELD_FROM_ISR(woken);
}
static void spin_us(unsigned us) { uint32_t t = board_now_us(); while (board_now_us() - t < us) { } }
static uint32_t basepri(void) { uint32_t v; __asm volatile("mrs %0, basepri" : "=r"(v)); return v; }

static void task(void *pv)
{
    (void)pv;
    board_printf_raw("task first line: n_irq=%u n_given=%u BASEPRI=0x%x\n", n_irq, n_given, basepri());
    uint32_t got = ulTaskNotifyTake(pdTRUE, 0);
    board_printf_raw("task: pending notifications taken = %u\n", (unsigned)got);
    vTaskDelay(2);
    board_printf_raw("task after 2 ms: n_irq=%u n_given=%u\n", n_irq, n_given);
    board_exit(0);
}

int main(void)
{
    NVIC_IPR(8) = C3_PRIO;
    TIMER_RELOAD(TIMER0_BASE) = 2500 - 1;    /* 100 us */
    TIMER_VALUE(TIMER0_BASE) = 2500 - 1;
    NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
    spin_us(500);
    board_printf_raw("prio 0x%x | before any API: n_irq=%u BASEPRI=0x%x\n", C3_PRIO, n_irq, basepri());
    TaskHandle_t t;
    xTaskCreate(task, "task", 256, NULL, 2, &t);
    h = t;
    unsigned before = n_irq;
    spin_us(500);
    board_printf_raw("after xTaskCreate: BASEPRI=0x%x, irqs in next 500 us = %u, n_given=%u\n",
                     basepri(), n_irq - before, n_given);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "0x80:" "0x10:" "0x80:-DC3_NOCHECK"; do p=${v%%:*}; x=${v#*:}; n=ex7_${p}${x:+_nc}; echo "=== $p $x"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex7.c OUT=/var/tmp/rtos/C3/$n EXTRA_CFLAGS="-DC3_PRIO=$p $x" | grep -v "filename\|^  1"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/$n/app.elf 5; echo "exit=$?"; done'
```

```text
=== 0x80 
prio 0x80 | before any API: n_irq=5 BASEPRI=0x0
after xTaskCreate: BASEPRI=0x20, irqs in next 500 us = 0, n_given=0
task first line: n_irq=6 n_given=1 BASEPRI=0x0
task: pending notifications taken = 1
task after 2 ms: n_irq=26 n_given=21
[board_exit] code=0
exit=0
=== 0x10 
prio 0x10 | before any API: n_irq=5 BASEPRI=0x0
after xTaskCreate: BASEPRI=0x20, irqs in next 500 us = 5, n_given=5
task first line: n_irq=11 n_given=6 BASEPRI=0x0
task: pending notifications taken = 6
[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:803
[board_exit] code=253
exit=1
=== 0x80 -DC3_NOCHECK

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:8325
[board_exit] code=253
exit=1
```

**출력에서 볼 것**:

- **우선순위 0x80 (정상 설정)**: 첫 API 호출 전에는 인터럽트가 정상으로 5번 들어왔다(BASEPRI 0). `xTaskCreate()` 뒤로는 BASEPRI가 0x20에 **머물러서** 그 뒤 500 µs + 스케줄러 시작 동안 **한 번도** 안 들어왔다. 그사이 TIMER0은 5번 넘게 만료됐지만 NVIC의 pending 비트는 하나뿐이라, 첫 태스크가 BASEPRI를 0으로 여는 순간 **딱 1번** 들어왔다(n_irq 5 → 6, 통지 1개). **나머지 만료는 사라졌다.** 시작 전에 켠 주기 인터럽트는 "시작할 때 한 번에 몰려 들어온다"가 아니라 "합쳐져서 일부가 없어진다"이다.
- 왜 BASEPRI가 안 풀리나: `taskENTER_CRITICAL()` → `vPortEnterCritical()`은 BASEPRI를 올리고 `uxCriticalNesting++`, `taskEXIT_CRITICAL()` → `vPortExitCritical()`은 `--` 뒤 **0이 되어야만** BASEPRI를 내린다(`port.c:443`). 시작 전 값이 0xaaaaaaaa라 0이 될 리 없다. 그래서 첫 API 호출부터 `xPortStartScheduler()`가 0을 넣을 때까지 커널 관리 인터럽트는 사실상 막힌다. 결과적으로 "스케줄러가 준비되기 전에 커널을 건드리는 ISR"을 막는 효과가 있다(포트 소스에 이 값의 의도를 밝힌 주석은 없다 — 동작으로 확인한 사실만 쓴다).
- **우선순위 0x10 (규칙 위반)**: BASEPRI 0x20보다 높아서 막히지 않고, 커널이 리스트를 만지는 중에도 들어와 `vTaskNotifyGiveFromISR()`을 5번 더 불렀다. **assert는 시작 전에는 안 걸렸다** — `vPortValidateInterruptPriority()`가 비교하는 `ucMaxSysCallPriority`가 아직 0이기 때문이다(§3의 2~3단계가 시작할 때 채운다). 시작 후 첫 인터럽트에서 비로소 `port.c:803` `configASSERT( ucCurrentPriority >= ucMaxSysCallPriority )`가 걸렸다. **시작 전에는 이 안전망이 없다**는 것이 핵심이다. 크리티컬 섹션 중 리스트 조작과 겹치면 Ready 리스트가 깨질 수 있고, 그 증상은 한참 뒤 엉뚱한 곳에서 나타난다.
- **NULL 확인 없는 ISR**: 핸들이 생기기 전 첫 인터럽트에서 `tasks.c:8325` `configASSERT( xTaskToNotify )`. assert를 끈 양산 빌드라면 NULL TCB에 쓰기 → HardFault 또는 0번지 근처 메모리 손상이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<rect x="120" y="30" width="200" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="220" y="48" font-size="12" text-anchor="middle">main: BASEPRI = 0 (500 µs)</text>
<rect x="320" y="30" width="226" height="26" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="433" y="48" font-size="12" text-anchor="middle">xTaskCreate 뒤 ~ 시작: 0x20</text>
<rect x="546" y="30" width="120" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="606" y="48" font-size="12" text-anchor="middle">첫 태스크: 0</text>
<text x="10" y="104" font-size="12">IRQ 0x80</text>
<line x1="120" y1="100" x2="666" y2="100" stroke="currentColor"/>
<circle cx="160" cy="100" r="6" fill="#4a7bd0"/><circle cx="200" cy="100" r="6" fill="#4a7bd0"/><circle cx="240" cy="100" r="6" fill="#4a7bd0"/><circle cx="280" cy="100" r="6" fill="#4a7bd0"/><circle cx="320" cy="100" r="6" fill="#4a7bd0"/>
<circle cx="360" cy="100" r="6" fill="none" stroke="#888"/><circle cx="400" cy="100" r="6" fill="none" stroke="#888"/><circle cx="440" cy="100" r="6" fill="none" stroke="#888"/><circle cx="480" cy="100" r="6" fill="none" stroke="#888"/><circle cx="520" cy="100" r="6" fill="none" stroke="#888"/>
<circle cx="552" cy="100" r="7" fill="#e08a3c"/>
<circle cx="590" cy="100" r="6" fill="#4a7bd0"/><circle cx="630" cy="100" r="6" fill="#4a7bd0"/>
<text x="220" y="128" font-size="12" text-anchor="middle">5번 실행</text>
<text x="440" y="128" font-size="12" text-anchor="middle">만료 ≥5번, 실행 0번 (pending 1개)</text>
<text x="600" y="128" font-size="12" text-anchor="middle">대기 1번만</text>
<text x="10" y="194" font-size="12">IRQ 0x10</text>
<line x1="120" y1="190" x2="666" y2="190" stroke="currentColor"/>
<circle cx="160" cy="190" r="6" fill="#4a7bd0"/><circle cx="200" cy="190" r="6" fill="#4a7bd0"/><circle cx="240" cy="190" r="6" fill="#4a7bd0"/><circle cx="280" cy="190" r="6" fill="#4a7bd0"/><circle cx="320" cy="190" r="6" fill="#4a7bd0"/>
<circle cx="360" cy="190" r="6" fill="#d0564a"/><circle cx="400" cy="190" r="6" fill="#d0564a"/><circle cx="440" cy="190" r="6" fill="#d0564a"/><circle cx="480" cy="190" r="6" fill="#d0564a"/><circle cx="520" cy="190" r="6" fill="#d0564a"/>
<path d="M584,182 l16,16 M600,182 l-16,16" stroke="#d0564a" stroke-width="2.5"/>
<text x="440" y="218" font-size="12" text-anchor="middle">막히지 않음: FromISR 5번, 검사 기준 0</text>
<text x="600" y="218" font-size="12" text-anchor="middle">assert 803행</text>
<text x="20" y="262" font-size="12">채운 원 = ISR 실행, 빈 원 = 만료됐지만 BASEPRI에 막힘, 주황 = 합쳐진 pending 1개, X = configASSERT</text>
<text x="20" y="282" font-size="12">TIMER0 100 µs 주기, ICOUNT. 원의 개수는 예제 7의 실제 카운트, 가로 위치는 개략</text>
</svg>
```

그림 5 — 예제 7 결과를 시간 축에 놓은 것. 첫 API 호출부터 시작까지 BASEPRI = 0x20 창이 열려 있다. 커널 관리 인터럽트(위)는 그동안 합쳐져 하나만 남고, 규칙을 어긴 인터럽트(아래)는 그 창을 뚫고 들어가는데 시작 전에는 아무도 잡지 않는다.

**실무 규칙** (여기서 바로 나오는 것):

1. 주변장치 인터럽트는 **스케줄러 시작 뒤, 그 인터럽트를 받을 태스크가 준비된 뒤**에 켠다. 가장 흔한 패턴은 "드라이버 태스크가 자기 첫 줄에서 인터럽트를 켠다"이다.
2. 시작 전에 꼭 켜야 하면(예: 워치독, 전원 감시) **커널 API를 부르지 않는** ISR로 만들고 우선순위를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높게(숫자 작게) 둔다.
3. ISR이 핸들·큐를 쓴다면 NULL 확인을 두거나, 생성이 끝난 뒤에 NVIC enable을 한다.

---

## 6. 직접 만들어 보기 — bare-metal 첫 태스크 런처 (55줄)

### 6.1 예제 8 — 위조 프레임 + SVC, 그리고 예외 없이 CONTROL로

**무엇을 확인하나**: 커널 없이 같은 원리만으로 "태스크"를 시작할 수 있는지. 두 방법을 다 쓴다.

- 방법 A (FreeRTOS 방식): 8워드 하드웨어 프레임만 위조 → `svc 0` → 핸들러가 PSP를 설정하고 LR = 0xFFFFFFFD로 복귀.
- 방법 B (Zephyr 방식, §7): 예외 없이 Thread 모드에서 `msr psp` → `msr control, #2`(SPSEL = 1) → `isb` → `bx 함수`.
- 덤으로 xPSR의 T 비트를 0으로 위조했을 때 무엇이 나는지.

```c
/* ex8: a 50-line first-task launcher on bare metal — forge a frame, enter it with SVC or CONTROL */
#include "board.h"
#ifndef XPSR0
#define XPSR0 0x01000000u                    /* T bit. Build with -DXPSR0=0 to see the fault */
#endif
static uint32_t stack_a[128] __attribute__((aligned(8)));
static uint32_t stack_b[128] __attribute__((aligned(8)));
uint32_t *volatile g_first_sp;               /* the SVC handler "restores" from here */

static void show(const char *who)
{
    uint32_t c, p, m;
    __asm volatile("mrs %0, control\n mrs %1, psp\n mrs %2, msp" : "=r"(c), "=r"(p), "=r"(m));
    board_printf_raw("%s: CONTROL=%u PSP=0x%x MSP=0x%x\n", who, c, p, m);
}
void launch_direct(uint32_t *top, void (*fn)(void));   /* no exception: what Zephyr does for main */
__asm(".text\n .syntax unified\n .thumb\n .thumb_func\n launch_direct:\n"
      " msr psp, r0\n movs r2, #2\n msr control, r2\n isb\n bx r1\n");  /* SPSEL=1, jump */
static void task_b(void) { show("task_b (direct)"); board_exit(0); }
static void task_exit(void)                  /* planted in LR, like prvTaskExitError */
{
    show("task_exit trap ");
    launch_direct(&stack_b[128], task_b);
}
static void task_a(void *arg)
{
    show("task_a (SVC)   ");
    board_printf_raw("task_a: arg (r0) = 0x%x, returning...\n", (unsigned)arg);
}
static uint32_t *forge(uint32_t *top, void (*fn)(void *), void *arg)
{
    uint32_t *sp = top;
    *--sp = XPSR0;                           /* xPSR */
    *--sp = (uint32_t)fn & ~1u;              /* PC: entry, Thumb bit cleared */
    *--sp = (uint32_t)task_exit;             /* LR: where a return lands */
    *--sp = 0x12121212; *--sp = 3; *--sp = 2; *--sp = 1;   /* r12 r3 r2 r1 */
    *--sp = (uint32_t)arg;                   /* r0: first argument */
    return sp;                               /* 8 words = hardware frame only */
}
__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile("ldr r0, =g_first_sp\n ldr r0, [r0]\n msr psp, r0\n isb\n"
                   "mvn lr, #2\n"            /* lr = 0xFFFFFFFD: thread mode, PSP */
                   "bx lr\n .ltorg\n");
}
int main(void)
{
    show("main           ");
    g_first_sp = forge(&stack_a[128], task_a, (void *)0xC3C3);
    board_printf_raw("forged frame at 0x%x: r0=%x pc=%x lr=%x xpsr=%x\n", (unsigned)g_first_sp,
                     g_first_sp[0], g_first_sp[6], g_first_sp[5], g_first_sp[7]);
    __asm volatile("svc 0");
    board_printf_raw("never here\n");
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for x in "" "-DXPSR0=0"; do echo "=== $x"; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/ex8.c OUT=/var/tmp/rtos/C3/ex8$x EXTRA_CFLAGS="$x" && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/ex8$x/app.elf 5; echo "exit=$?"; done; arm-none-eabi-nm /var/tmp/rtos/C3/ex8/app.elf | grep -E "task_|stack_|launch"'
```

```text
=== 
   text	   data	    bss	    dec	    hex	filename
   4624	     96	   1360	   6080	   17c0	/var/tmp/rtos/C3/ex8/app.elf
main           : CONTROL=0 PSP=0x0 MSP=0x203fffd0
forged frame at 0x20000448: r0=c3c3 pc=108 lr=141 xpsr=1000000
task_a (SVC)   : CONTROL=2 PSP=0x20000458 MSP=0x203fffc0
task_a: arg (r0) = 0xc3c3, returning...
task_exit trap : CONTROL=2 PSP=0x20000458 MSP=0x203fffc0
task_b (direct): CONTROL=2 PSP=0x20000250 MSP=0x203fffc0
[board_exit] code=0
exit=0
=== -DXPSR0=0
   text	   data	    bss	    dec	    hex	filename
   4624	     96	   1360	   6080	   17c0	/var/tmp/rtos/C3/ex8-DXPSR0=0/app.elf
main           : CONTROL=0 PSP=0x0 MSP=0x203fffd0
forged frame at 0x20000448: r0=c3c3 pc=108 lr=141 xpsr=0

[HardFault] PC=0x108 LR=0x141 xPSR=0x0 CFSR=0x20000 HFSR=0x40000000
[board_exit] code=255
exit=1
000000c0 t launch_direct
20000268 b stack_a
20000068 b stack_b
00000108 t task_a
000000f4 t task_b
00000140 t task_exit
```

**출력에서 볼 것**:

- **방법 A가 동작했다**: `main`(CONTROL 0, PSP 0)에서 `svc 0` 한 번으로 `task_a`가 CONTROL = 2(PSP 사용)로 시작했고, 위조 R0 = 0xc3c3이 인자로 들어왔다. 프레임은 `stack_a` 끝(0x20000468)에서 32바이트 아래 0x20000448.
- **LR 위조도 동작했다**: `task_a`가 return하자 `task_exit`(0x140, LR = 0x141)로 왔다 — `prvTaskExitError`와 같은 원리.
- **방법 B도 동작했다**: `launch_direct`가 예외 없이 PSP를 `stack_b`로 바꾸고 점프해서 `task_b`가 새 스택(0x20000250 근처)에서 돈다. 예외 복귀가 없으니 하드웨어 프레임도 필요 없다.
- **MSP 0x203fffd0 → 0x203fffc0**: 여기서도 SVC 프레임은 꺼내지지 않고 MSP에 남았다(main이 `svc`를 실행할 때 SP는 `show()` 안보다 위인 0x203fffe0이었고 거기서 32바이트). FreeRTOS와 같은 현상이다. 이 런처는 MSP를 리셋하지 않으니 main의 프레임도 그대로 남아 있다.
- **xPSR = 0으로 위조하면**: 예외 복귀가 T = 0 상태로 PC 0x108을 실행하려다 **CFSR = 0x20000 = UFSR.INVSTATE**(잘못된 상태) UsageFault, UsageFault가 활성화되어 있지 않으니 HardFault로 승격(HFSR.FORCED = 0x40000000). 쌓인 PC가 태스크 진입점 그 자체이고 xPSR = 0인 HardFault라면 "프레임 위조에서 T 비트를 빠뜨렸다"는 뜻이다(M1).

### 6.2 방법 A와 B, 무엇이 다른가

| 항목 | A: SVC + 예외 복귀 | B: CONTROL 직접 변경 |
|---|---|---|
| 위조 프레임 | 필요 (HW 8워드, 커널이면 +R4–R11) | 필요 없음 (레지스터로 인자를 직접 넘김) |
| 시작 시점의 모드 | Handler → 복귀하면서 Thread | 처음부터 Thread |
| 인터럽트 마스크 해제 | 핸들러 안에서 BASEPRI 0 → 원자적으로 복귀 | `bx` 전에 해제 (사이에 인터럽트가 들어와도 PSP는 이미 새 스택) |
| 비특권(unprivileged) 시작 | 복귀 전에 CONTROL.nPRIV 설정 가능 | `msr control, #3`로 가능하지만 되돌리려면 SVC가 필요 |
| 평소 스위치 경로와 같은가 | 같다 (PendSV도 같은 프레임을 꺼낸다) | 첫 스레드만 다른 경로 |
| 쓰는 곳 | FreeRTOS ARM_CM3·CM4F 포트 | Zephyr Cortex-M의 main 스레드 진입 |

---

## 7. Zephyr는 어떻게 하나 — `z_cstart()` → `arch_switch_to_main_thread()`

### 7.1 구조 — main이 처음부터 스레드다

Zephyr에서 `main()`은 "커널을 시작하는 함수"가 아니라 **커널이 시작한 첫 스레드(main thread)가 부르는 함수**다(A7 §4에서 본 차이). 경로(v4.4.2):

1. `z_arm_reset`(`arch/arm/core/cortex_m/reset.S`): MSP = `z_main_stack` 꼭대기로 한 번 맞추고, `BASEPRI = _EXC_IRQ_DEFAULT_PRIO`로 인터럽트를 막고, **PSP = `z_interrupt_stacks` 꼭대기, CONTROL.SPSEL = 1** — 부팅 C 코드를 PSP(인터럽트 스택 위)에서 돌린다. 그리고 `z_prep_c`.
2. `z_prep_c`(`prep_c.c`): 벡터 테이블 재배치, `arch_bss_zero()`, `arch_data_copy()`, `z_arm_interrupt_init()`(모든 IRQ 우선순위 기본값) → `z_cstart()`.
3. `z_cstart`(`kernel/init.c:538`): `EARLY` init → `arch_kernel_init()`(**`z_arm_interrupt_stack_setup()`: MSP = 인터럽트 스택 꼭대기**, 예외 우선순위 설정 `z_arm_exc_setup`, MPU 초기화) → 가짜 스레드 `_thread_dummy` 설정 → `PRE_KERNEL_1`, `PRE_KERNEL_2` init(드라이버) → `switch_to_main_thread(prepare_multithreading())`.
4. `prepare_multithreading()`: `z_setup_new_thread(&z_main_thread, z_main_stack, ..., bg_thread_main, ...)`로 main 스레드를, `z_init_cpu(0)` → `init_idle_thread()`로 idle 스레드를 만든다. 두 스레드 모두 `arch_new_thread()`가 프레임을 위조한다.
5. `switch_to_main_thread()`: `CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN`(Cortex-M은 `!USE_SWITCH`일 때 `arch/arm/core/Kconfig`에서 select — 이 빌드의 `.config`에서 `=y`, `CONFIG_USE_SWITCH`는 not set 확인)이면 **`arch_switch_to_main_thread()`**(`arch/arm/core/cortex_m/thread.c:576`) — 이름을 v4.4.2 소스에서 확인했다. 아니면 일반 `z_swap_unlocked()`.
6. `arch_switch_to_main_thread()`: 현재 스레드 = main, (MPU면) main 스택 영역 설정, `msr PSP, stack_ptr` → `arch_irq_unlock_outlined(0)`(`cpsie f`, `cpsie i`, `BASEPRI = 0`) → `z_thread_entry(bg_thread_main, NULL, NULL, NULL)`로 **그냥 `bx`**. 예외도, 위조 프레임 복원도 없다.
7. `bg_thread_main()`(main 스레드 안): `POST_KERNEL` init, 부트 배너, `APPLICATION` init, 정적 스레드(`K_THREAD_DEFINE`) 시작, 그리고 `main()`.

`arch_new_thread()`(같은 `thread.c`)의 위조 프레임 — `struct __basic_sf`(a1..a4, ip, lr, pc, xpsr):

```c
/* arch/arm/core/cortex_m/thread.c, arch_new_thread() 중 (CONFIG_USE_SWITCH 아닌 경로, 실제 소스) */
	struct __basic_sf *iframe = Z_STACK_PTR_TO_FRAME(struct __basic_sf, stack_ptr);
	iframe->pc = (uint32_t)entry_wrapper;      /* = z_thread_entry */
	/* force ARM mode by clearing LSB of address */
	iframe->pc &= 0xfffffffe;
	iframe->a1 = (uint32_t)entry;              /* 사용자 함수 → R0 */
	iframe->a2 = (uint32_t)p1;
	iframe->a3 = (uint32_t)p2;
	iframe->a4 = (uint32_t)p3;
	iframe->xpsr = 0x01000000UL; /* clear all, thumb bit is 1, even if RO */
	thread->callee_saved.psp = (uint32_t)iframe;
	thread->arch.basepri = 0;
```

FreeRTOS와 다른 점: R4–R11을 스택에 두지 않고 `struct k_thread`의 `callee_saved`에 저장한다(그래서 프레임은 8워드뿐이고 `callee_saved.psp`가 그 프레임을 가리킨다). LR은 설정하지 않는다 — `z_thread_entry`가 돌아오지 않으니(`k_thread_abort` 후 `CODE_UNREACHABLE`).

### 7.2 예제 9 — Zephyr 앱 안에서 main · idle · 새 스레드의 프레임 보기

**무엇을 확인하나**: main 스레드 안의 CONTROL·PSP·MSP, main 스택 범위, 인터럽트 스택 꼭대기, idle 스레드와 아직 시작 안 한(`K_FOREVER`) 새 스레드의 위조 프레임, 그리고 return한 스레드의 상태.

```text
# z1/prj.conf
CONFIG_BOOT_BANNER=n
CONFIG_THREAD_NAME=y
```

```c
/* z1: how Zephyr v4.4.2 (Cortex-M) set up main, idle and a new thread's initial frame */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
extern struct k_thread z_main_thread, z_idle_threads[];
extern void z_thread_entry(k_thread_entry_t entry, void *p1, void *p2, void *p3);
extern void idle(void *a, void *b, void *c);

static void dump_frame(const char *who, struct k_thread *t)
{
    uint32_t *f = (uint32_t *)t->callee_saved.psp;     /* struct __basic_sf: a1..a4 ip lr pc xpsr */
    printk("%-6s psp=0x%08x  a1=%08x a2=%08x a3=%08x a4=%08x ip=%08x lr=%08x pc=%08x xpsr=%08x\n",
           who, (unsigned)f, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
}
static void worker(void *p1, void *p2, void *p3)
{
    printk("worker: p1=%p p2=%p p3=%p -> returning (Zephyr allows it)\n", p1, p2, p3);
}
K_THREAD_STACK_DEFINE(wstack, 1024);
static struct k_thread wthread;

int main(void)
{
    uint32_t control, psp, msp, basepri;
    __asm volatile("mrs %0, control\n mrs %1, psp\n mrs %2, msp\n mrs %3, basepri"
                   : "=r"(control), "=r"(psp), "=r"(msp), "=r"(basepri));
    printk("main thread: CONTROL=%u PSP=0x%08x MSP=0x%08x BASEPRI=0x%x\n", control, psp, msp, basepri);
    printk("main stack  [0x%08x .. 0x%08x)  prio %d\n", (unsigned)z_main_thread.stack_info.start,
           (unsigned)(z_main_thread.stack_info.start + z_main_thread.stack_info.size),
           k_thread_priority_get(k_current_get()));
    printk("irq stack top (MSP after arch_kernel_init) = 0x%08x\n", (unsigned)_kernel.cpus[0].irq_stack);
    printk("z_thread_entry=%p idle=%p worker=%p\n", z_thread_entry, idle, worker);
    dump_frame("idle", &z_idle_threads[0]);
    k_tid_t t = k_thread_create(&wthread, wstack, K_THREAD_STACK_SIZEOF(wstack), worker,
                                (void *)0x11, (void *)0x22, (void *)0x33, 5, 0, K_FOREVER);
    dump_frame("worker", &wthread);
    k_thread_start(t);
    k_thread_join(t, K_FOREVER);
    printk("worker state after return: %s\n", k_thread_state_str(t, (char[32]){0}, 32));
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");   /* semihosting exit */
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C3/z1 /var/tmp/rtos/C3/z1 2>&1 | grep -i "warning\|error\|FLASH\|RAM"; grep -E "CONFIG_USE_SWITCH|CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN|CONFIG_MAIN_STACK_SIZE|CONFIG_ISR_STACK_SIZE|CONFIG_IDLE_STACK_SIZE|CONFIG_MAIN_THREAD_PRIORITY|CONFIG_INIT_STACKS|CONFIG_MPU_STACK_GUARD|CONFIG_ARM_MPU=" /var/tmp/rtos/C3/z1/zephyr/.config; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/z1/zephyr/zephyr.elf 5'
```

```text
           FLASH:       13900 B         4 MB      0.33%
             RAM:        7392 B         4 MB      0.18%
CONFIG_MAIN_STACK_SIZE=1024
CONFIG_ISR_STACK_SIZE=4096
CONFIG_IDLE_STACK_SIZE=256
# CONFIG_INIT_STACKS is not set
# CONFIG_MPU_STACK_GUARD is not set
CONFIG_ARM_MPU=y
CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN=y
CONFIG_MAIN_THREAD_PRIORITY=0
# CONFIG_USE_SWITCH is not set
CONFIG_USE_SWITCH_SUPPORTED=y
main thread: CONTROL=2 PSP=0x20001c58 MSP=0x200017e0 BASEPRI=0x0
main stack  [0x200018e0 .. 0x20001ce0)  prio 0
irq stack top (MSP after arch_kernel_init) = 0x200017e0
z_thread_entry=0x2157 idle=0x27b1 worker=0xc1
idle   psp=0x200018c0  a1=000027b1 a2=200003b0 a3=00000000 a4=00000000 ip=00000000 lr=00000000 pc=00002156 xpsr=01000000
worker psp=0x200007c0  a1=000000c1 a2=00000011 a3=00000022 a4=00000033 ip=00000000 lr=00000000 pc=00002156 xpsr=01000000
worker state after return: dead
```

(`worker: p1=0x11 p2=0x22 p3=0x33 -> returning (Zephyr allows it)` 줄이 `worker` 덤프와 `dead` 사이에 찍혔다.)

**출력에서 볼 것**:

- **main은 스레드다**: CONTROL = 2(PSP), PSP는 main 스택 [0x200018e0, 0x20001ce0) 안, 우선순위 0(`CONFIG_MAIN_THREAD_PRIORITY`). MSP = 0x200017e0 = 인터럽트 스택 꼭대기 — FreeRTOS의 "벡터 테이블의 초기 MSP"와 달리 Zephyr는 **전용 배열 `z_interrupt_stacks`(4 KB)**를 인터럽트 스택으로 쓴다.
- **위조 프레임 모양이 FreeRTOS와 같은 원리, 다른 배치**: `pc = 0x2156` = `z_thread_entry`(0x2157에서 Thumb 비트 제거) — **두 스레드 모두 같은 PC**다. 사용자 함수는 `a1`(R0): idle은 `idle`(0x27b1), worker는 `worker`(0xc1). p1~p3은 a2~a4(idle의 p1 = `&_kernel.cpus[0]` = 0x200003b0, worker는 0x11/0x22/0x33). xPSR = 0x01000000. LR·IP = 0(설정하지 않아 `.bss`/`.noinit`의 0이 보인다).
- worker는 return했고 상태가 **dead** — 래퍼가 `k_thread_abort()`를 불렀다. FreeRTOS라면 `prvTaskExitError`다.
- 이 앱에서는 `printk` 줄 순서상 worker 출력이 dump 다음에 나왔다: `K_FOREVER`라 `k_thread_start()` 전에는 안 돌았고, 우선순위 5라 main(0)이 `k_thread_join()`으로 막힐 때 돌았다.

### 7.3 예제 10 — gdb로 Zephyr의 main 진입 21개 명령 보기

**무엇을 확인하나**: 부팅 중 어느 스택을 쓰는지, 그리고 `arch_switch_to_main_thread()`가 정말 예외 없이(IPSR = 0 그대로) main 스레드로 넘어가는지.

```text
define sh
  printf "pc=%08x r0=%08x r4=%08x lr=%08x msp=%08x psp=%08x control=%x basepri=%02x primask=%x ipsr=%u\n", $pc, $r0, $r4, $lr, $msp, $psp, $control, $basepri, $primask, $xpsr & 0x1ff
end
break z_prep_c
break z_cstart
break arch_switch_to_main_thread
continue
printf "[z_prep_c]  "
sh
continue
printf "[z_cstart]  "
sh
continue
printf "[arch_switch_to_main_thread] "
sh
info symbol $r2
set $n = 0
while $n < 40 && $pc != 0x2156
  x/i $pc
  stepi
  sh
  set $n = $n + 1
end
printf "steps=%d  ", $n
info symbol $pc
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/C3 && PORT=31304 IC="-icount shift=5,align=off,sleep=on" ./gdbrun.sh /var/tmp/rtos/C3/z1/zephyr/zephyr.elf z1.gdb 2>&1 | grep -E "^pc=|^=>|^\[|^steps|^bg_thread|^z_thread_entry in"'
```

```text
[z_prep_c]  pc=00002184 r0=00000002 r4=00000000 lr=00000719 msp=20001ce0 psp=200017e0 control=2 basepri=20 primask=0 ipsr=0
[z_cstart]  pc=00000fc4 r0=e000e100 r4=00000000 lr=0000219b msp=20001ce0 psp=200017d8 control=2 basepri=20 primask=0 ipsr=0
[arch_switch_to_main_thread] pc=000007e4 r0=200001e8 r4=200001e8 lr=00001067 msp=200017e0 psp=200017b8 control=2 basepri=20 primask=0 ipsr=0
bg_thread_main + 1 in section text
=> 0x7e4 <arch_switch_to_main_thread>:	ldr	r3, [pc, #40]	@ (0x810 <arch_switch_to_main_thread+44>)
pc=000007e6 r0=200001e8 r4=200001e8 lr=00001067 msp=200017e0 psp=200017b8 control=2 basepri=20 primask=0 ipsr=0
=> 0x7e6 <arch_switch_to_main_thread+2>:	push	{lr}
pc=000007e8 r0=200001e8 r4=200001e8 lr=00001067 msp=200017e0 psp=200017b4 control=2 basepri=20 primask=0 ipsr=0
=> 0x7e8 <arch_switch_to_main_thread+4>:	mov	r6, r1
...
=> 0x7ee <arch_switch_to_main_thread+10>:	mov	r4, r5
pc=000007f0 r0=200001e8 r4=00000e95 lr=00001067 msp=200017e0 psp=200017b4 control=2 basepri=20 primask=0 ipsr=0
=> 0x7f0 <arch_switch_to_main_thread+12>:	msr	PSP, r6
pc=000007f4 r0=200001e8 r4=00000e95 lr=00001067 msp=200017e0 psp=20001ce0 control=2 basepri=20 primask=0 ipsr=0
=> 0x7f4 <arch_switch_to_main_thread+16>:	movs	r0, #0
...
=> 0x7f8 <arch_switch_to_main_thread+20>:	blx	r3
pc=000021a0 r0=00000000 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=20 primask=0 ipsr=0
=> 0x21a0 <arch_irq_unlock_outlined>:	cpsie	f
pc=000021a2 r0=00000000 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=20 primask=0 ipsr=0
=> 0x21a2 <arch_irq_unlock_outlined+2>:	cpsie	i
pc=000021a4 r0=00000000 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=20 primask=0 ipsr=0
=> 0x21a4 <arch_irq_unlock_outlined+4>:	msr	BASEPRI, r0
pc=000021a8 r0=00000000 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=00 primask=0 ipsr=0
=> 0x21a8 <arch_irq_unlock_outlined+8>:	isb	sy
pc=000021ac r0=00000000 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=00 primask=0 ipsr=0
=> 0x21ac <arch_irq_unlock_outlined+12>:	bx	lr
pc=000007fa r0=00000000 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=00 primask=0 ipsr=0
=> 0x7fa <arch_switch_to_main_thread+22>:	mov	r0, r4
pc=000007fc r0=00000e95 r4=00000e95 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=00 primask=0 ipsr=0
...
=> 0x802 <arch_switch_to_main_thread+30>:	ldr	r4, [pc, #8]	@ (0x80c <arch_switch_to_main_thread+40>)
pc=00000804 r0=00000e95 r4=00002157 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=00 primask=0 ipsr=0
=> 0x804 <arch_switch_to_main_thread+32>:	bx	r4
pc=00002156 r0=00000e95 r4=00002157 lr=000007fb msp=200017e0 psp=20001ce0 control=2 basepri=00 primask=0 ipsr=0
steps=21  z_thread_entry in section text
```

(`...`는 레지스터 이동만 있는 단계 6줄을 줄인 것이다.)

**출력에서 볼 것**:

- **부팅 C 코드는 PSP 위에서 돈다**: `z_prep_c`·`z_cstart`에서 CONTROL = 2, PSP = 0x200017e0 근처(인터럽트 스택 꼭대기), MSP = 0x20001ce0(main 스택 꼭대기, 실제로는 안 쓰임). `arch_kernel_init()` 뒤 `arch_switch_to_main_thread` 입구에서는 **MSP = 0x200017e0**(인터럽트 스택 꼭대기)이 되어 있고 PSP는 그 아래 0x200017b8 — 즉 부팅 코드가 쓴 스택 자리와 앞으로 ISR이 쓸 자리가 **같은 4 KB 배열**이다. 부팅 코드의 프레임은 FreeRTOS의 main 프레임처럼 버려지고 ISR이 덮어쓴다. 대신 Zephyr의 `main()`은 아직 시작도 안 했으니 잃을 지역 변수가 없다.
- **`msr PSP, r6` 한 줄**(0x7f0)로 PSP가 0x200017b4 → **0x20001ce0**(main 스택 꼭대기). 위조 프레임이 필요 없으니 꼭대기 그대로다.
- `arch_irq_unlock_outlined`: `cpsie f`, `cpsie i`, `BASEPRI = 0` — FreeRTOS SVC 핸들러의 `msr basepri, #0`에 해당한다.
- `mov r0, r4`(r0 = `bg_thread_main` 0xe95), r1~r3 = 0, `bx r4` → **`z_thread_entry(bg_thread_main, NULL, NULL, NULL)`**. 21단계 내내 **IPSR = 0** — 예외는 한 번도 없었다. MSP도 계속 0x200017e0이라 **SVC 프레임 32바이트 손실도 없다.**

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="20" font-size="14" text-anchor="middle" font-weight="bold">FreeRTOS V11.3.2 (ARM_CM3)</text>
<text x="510" y="20" font-size="14" text-anchor="middle" font-weight="bold">Zephyr v4.4.2 (Cortex-M)</text>
<rect x="20" y="34" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170" y="51" font-size="12" text-anchor="middle">reset: MSP = vector[0], Thread·MSP</text><text x="170" y="67" font-size="12" text-anchor="middle">Reset_Handler → main() (MSP 위)</text>
<rect x="360" y="34" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="510" y="51" font-size="12" text-anchor="middle">z_arm_reset: PSP = 인터럽트 스택</text><text x="510" y="67" font-size="12" text-anchor="middle">CONTROL=2, z_prep_c → z_cstart</text>
<line x1="170" y1="74" x2="170" y2="92" stroke="currentColor" marker-end="url(#ah6)"/><line x1="510" y1="74" x2="510" y2="92" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="94" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="111" font-size="12" text-anchor="middle">main: xTaskCreate → 위조 16워드</text><text x="170" y="127" font-size="12" text-anchor="middle">vTaskStartScheduler: IDLE·Tmr Svc</text>
<rect x="360" y="94" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510" y="111" font-size="12" text-anchor="middle">arch_kernel_init: MSP = 인터럽트 스택</text><text x="510" y="127" font-size="12" text-anchor="middle">PRE_KERNEL_1·2 (드라이버)</text>
<line x1="170" y1="134" x2="170" y2="152" stroke="currentColor" marker-end="url(#ah6)"/><line x1="510" y1="134" x2="510" y2="152" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="154" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="171" font-size="12" text-anchor="middle">xPortStartScheduler: 우선순위 탐지</text><text x="170" y="187" font-size="12" text-anchor="middle">SHPR, SysTick, nesting = 0</text>
<rect x="360" y="154" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510" y="171" font-size="12" text-anchor="middle">prepare_multithreading: main·idle</text><text x="510" y="187" font-size="12" text-anchor="middle">스레드 생성 (위조 8워드 + callee_saved)</text>
<line x1="170" y1="194" x2="170" y2="212" stroke="currentColor" marker-end="url(#ah6)"/><line x1="510" y1="194" x2="510" y2="212" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="214" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="231" font-size="12" text-anchor="middle">MSP ← vector[0] (main 스택 폐기)</text><text x="170" y="247" font-size="12" text-anchor="middle">svc 0 → Handler 모드 (IPSR 11)</text>
<rect x="360" y="214" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="231" font-size="12" text-anchor="middle">arch_switch_to_main_thread:</text><text x="510" y="247" font-size="12" text-anchor="middle">msr PSP = main 스택 꼭대기</text>
<line x1="170" y1="254" x2="170" y2="272" stroke="currentColor" marker-end="url(#ah6)"/><line x1="510" y1="254" x2="510" y2="272" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="274" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="291" font-size="12" text-anchor="middle">ldmia r4-r11, msr psp, BASEPRI 0</text><text x="170" y="307" font-size="12" text-anchor="middle">bx 0xFFFFFFFD (예외 복귀)</text>
<rect x="360" y="274" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="291" font-size="12" text-anchor="middle">cpsie f/i, BASEPRI 0</text><text x="510" y="307" font-size="12" text-anchor="middle">bx z_thread_entry (예외 없음, IPSR 0)</text>
<line x1="170" y1="314" x2="170" y2="332" stroke="currentColor" marker-end="url(#ah6)"/><line x1="510" y1="314" x2="510" y2="332" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="20" y="334" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="351" font-size="12" text-anchor="middle">첫 태스크 (= 가장 높은 우선순위,</text><text x="170" y="367" font-size="12" text-anchor="middle">기본 설정이면 Tmr Svc)</text>
<rect x="360" y="334" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="510" y="351" font-size="12" text-anchor="middle">bg_thread_main: POST_KERNEL·APP init</text><text x="510" y="367" font-size="12" text-anchor="middle">→ main() (main 스레드 안)</text>
<text x="340" y="394" font-size="12" text-anchor="middle">주황 = 첫 진입 순간. 두 예제 모두 21개 명령이지만 왼쪽은 예외 하나를 거친다</text>
</svg>
```

그림 6 — 두 커널의 첫 진입 비교. FreeRTOS는 main(MSP) → SVC 예외 → 위조 프레임 복원으로 첫 태스크에 들어가고, Zephyr는 부팅을 처음부터 PSP로 돌리다가 main 스레드 스택으로 PSP만 바꿔 `bx`로 들어간다. Zephyr의 다른 스레드(idle, 앱 스레드)는 나중에 PendSV(`z_arm_pendsv`)가 위조 프레임을 꺼내며 처음 실행된다.

### 7.4 비교 표

| 항목 | FreeRTOS V11.3.2 ARM_CM3 | Zephyr v4.4.2 Cortex-M (`USE_SWITCH` 아님) |
|---|---|---|
| `main()`의 정체 | 리셋 코드가 부르는 평범한 함수 (MSP 위) | main 스레드가 부르는 함수 (main 스레드 스택, PSP) |
| 시작 함수 | `vTaskStartScheduler()` (앱이 부른다) | `z_cstart()` (리셋 코드가 부른다, 앱은 안 부름) |
| 첫 진입 방법 | `svc 0` → `vPortSVCHandler` → EXC_RETURN 0xFFFFFFFD | `arch_switch_to_main_thread()`: `msr PSP` + `bx` |
| 위조 프레임 | 16워드 (HW 8 + R4–R11 8), PC = 태스크 함수, LR = `prvTaskExitError` | HW 8워드 (`__basic_sf`), PC = `z_thread_entry`, a1 = 함수, a2–a4 = p1–p3, R4–R11은 `callee_saved` |
| 인터럽트 스택 | 벡터 테이블의 초기 MSP 아래 (링커 `_Main_Stack_Size` 4 KB) | 전용 배열 `z_interrupt_stacks` (`CONFIG_ISR_STACK_SIZE` 4096) |
| 버려지는 스택 | main의 MSP 프레임 + SVC 프레임 32 B | 부팅 코드가 쓴 인터럽트 스택 일부 (main 지역 변수 손실 없음) |
| 시작 전 인터럽트 | 첫 API 호출부터 BASEPRI 0x20 (`uxCriticalNesting` 0xaaaaaaaa) | 리셋에서 BASEPRI = `_EXC_IRQ_DEFAULT_PRIO`, main 스레드 진입 때 해제 |
| 함수가 return하면 | `prvTaskExitError` (assert 또는 정지) | `k_thread_abort()` → dead (정상 종료) |
| 시작 실패 | idle·timer 생성 실패 시 `vTaskStartScheduler()`가 돌아옴 | 정적 객체라 heap 실패 경로 없음 (스택은 컴파일 때 배치) |

---

## 8. 측정 — reset부터 첫 태스크 첫 줄까지 몇 µs인가

### 8.1 예제 11 — FreeRTOS 분해 (trace 훅 + 링커 `--wrap`)

**무엇을 확인하나**: reset → `main()` → 사용자 태스크 생성 → idle·timer 생성 → 포트 시작 → 첫 태스크 첫 줄을 25 MHz FPGAIO 카운터(reset부터 센다, A7 예제 1)로 나눠 잰다. 커널 소스를 고치지 않으려고 `traceSTARTING_SCHEDULER()` 훅과 `-Wl,--wrap=xPortStartScheduler`(링커가 `tasks.c`의 호출을 내 `__wrap_` 함수로 돌린다)를 썼다. 사용자 태스크가 진짜 첫 태스크가 되도록 timer 우선순위를 6으로 내렸다(§2.3). heap 128 KB(템플릿 기본)와 16 KB 두 가지.

```c
/* mfrt/FreeRTOSConfig.h — template + timestamp hooks for the boot breakdown */
#ifndef C3_MFRT_CONFIG_H
#define C3_MFRT_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef  configTIMER_TASK_PRIORITY
#define configTIMER_TASK_PRIORITY ( configMAX_PRIORITIES - 2 )   /* so OUR task is the first to run */
extern volatile uint32_t c3_t_sched;
#define traceSTARTING_SCHEDULER(h) do { c3_t_sched = REG32(0x40028018u); (void)(h); } while (0)
#endif
```

```c
/* mfrt: reset -> main -> first task instruction, broken down (FPGAIO 25 MHz counter, ICOUNT) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define CYC() REG32(0x40028018u)                 /* counts from reset, 1 cyc = 40 ns */
volatile uint32_t c3_t_sched;
static uint32_t t_main, t_create, t_port;
extern BaseType_t __real_xPortStartScheduler(void);
BaseType_t __wrap_xPortStartScheduler(void) { t_port = CYC(); return __real_xPortStartScheduler(); }
extern uint32_t _sbss, _ebss;

static void first(void *pv)
{
    uint32_t t_first = CYC();                    /* first line of the first task */
    (void)pv;
    board_printf_raw("bss=%u B heap=%u B\n", (unsigned)((char *)&_ebss - (char *)&_sbss),
                     (unsigned)configTOTAL_HEAP_SIZE);
    board_printf_raw("reset -> main()                 %u ns\n", t_main * 40);
    board_printf_raw("main -> xTaskCreate done        %u ns\n", (t_create - t_main) * 40);
    board_printf_raw("idle+timer create (StartSched)  %u ns\n", (c3_t_sched - t_create) * 40);
    board_printf_raw("-> xPortStartScheduler entry    %u ns\n", (t_port - c3_t_sched) * 40);
    board_printf_raw("port start -> first task line   %u ns\n", (t_first - t_port) * 40);
    board_printf_raw("TOTAL reset -> first task       %u ns\n", t_first * 40);
    board_exit(0);
}

int main(void)
{
    t_main = CYC();
    xTaskCreate(first, "first", 256, NULL, configMAX_PRIORITIES - 1, NULL);
    t_create = CYC();
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for h in 131072 16384; do echo "=== heap $h"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C3/mfrt/main.c OUT=/var/tmp/rtos/C3/mfrt_$h EXTRA_CFLAGS=-DconfigTOTAL_HEAP_SIZE=$h EXTRA_LDFLAGS=-Wl,--wrap=xPortStartScheduler | grep -v filename; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/mfrt_$h/app.elf; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/mfrt_$h/app.elf | grep TOTAL; done'
```

```text
=== heap 131072
  12072	    100	 135416	 147588	  24084	/var/tmp/rtos/C3/mfrt_131072/app.elf
bss=135412 B heap=131072 B
reset -> main()                 1365760 ns
main -> xTaskCreate done        32880 ns
idle+timer create (StartSched)  58760 ns
-> xPortStartScheduler entry    160 ns
port start -> first task line   5800 ns
TOTAL reset -> first task       1463360 ns
[board_exit] code=0
TOTAL reset -> first task       1463360 ns
=== heap 16384
  12072	    100	  20728	  32900	   8084	/var/tmp/rtos/C3/mfrt_16384/app.elf
bss=20724 B heap=16384 B
reset -> main()                 218880 ns
main -> xTaskCreate done        32880 ns
idle+timer create (StartSched)  58760 ns
-> xPortStartScheduler entry    160 ns
port start -> first task line   5800 ns
TOTAL reset -> first task       316480 ns
[board_exit] code=0
TOTAL reset -> first task       316480 ns
```

**출력에서 볼 것**: 두 번 돌려도 같은 숫자(icount의 결정성). heap 크기는 reset → main만 바꾼다(1365.8 vs 218.9 µs — `.bss` 0 채우기, A7 §4와 같은 결론). **main 뒤 커널 비용은 97.6 µs로 heap과 무관**하고, 그중 "포트 시작 → 첫 태스크 첫 줄"(§3·§4 전체: 우선순위 탐지, SHPR, SysTick, `prvPortStartFirstTask`, SVC, 복원)은 **5.8 µs**뿐이다. 대부분은 태스크 생성(32.9 + 58.8 µs)이다.

### 8.2 예제 12 — Zephyr 분해 (`SYS_INIT` 단계마다 타임스탬프)

**무엇을 확인하나**: Zephyr는 앱이 부르는 시작 함수가 없으니 `SYS_INIT(fn, LEVEL, 0)`으로 각 init 단계 입구에 표시를 박는다. `POST_KERNEL` 훅은 이미 **main 스레드 안**에서 돈다(§7.1의 7번) — 그래서 "→ main thread" 구간이 Zephyr의 "첫 스레드 진입"이다. 같은 우선순위 0의 다른 init 항목보다 먼저 돈다는 보장은 없으니 경계는 "그 단계 즈음"이다.

```c
/* z2: Zephyr v4.4.2 reset -> main thread, broken down with SYS_INIT hooks (FPGAIO 25 MHz) */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)
static uint32_t t[6];
static int mark_early(void) { t[0] = CYC(); return 0; }   /* first thing in z_cstart */
static int mark_pk1(void)   { t[1] = CYC(); return 0; }   /* after arch_kernel_init */
static int mark_pk2(void)   { t[2] = CYC(); return 0; }   /* drivers (UART...) done */
static int mark_post(void)  { t[3] = CYC(); return 0; }   /* already IN the main thread */
static int mark_app(void)   { t[4] = CYC(); return 0; }
SYS_INIT(mark_early, EARLY, 0);
SYS_INIT(mark_pk1, PRE_KERNEL_1, 0);
SYS_INIT(mark_pk2, PRE_KERNEL_2, 0);
SYS_INIT(mark_post, POST_KERNEL, 0);
SYS_INIT(mark_app, APPLICATION, 0);

int main(void)
{
    t[5] = CYC();
    static const char *name[6] = { "reset -> z_cstart (EARLY)", "-> PRE_KERNEL_1",
        "-> PRE_KERNEL_2", "-> main thread (POST_KERNEL)", "-> APPLICATION", "-> main()" };
    uint32_t prev = 0;
    for (int i = 0; i < 6; i++) { printk("%-30s %u ns\n", name[i], (t[i] - prev) * 40); prev = t[i]; }
    printk("TOTAL reset -> main()          %u ns\n", t[5] * 40);
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C3/z2 /var/tmp/rtos/C3/z2 2>&1 | grep -i "warning\|error\|FLASH:\|RAM:"; for i in 1 2; do ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/z2/zephyr/zephyr.elf 5; done; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C3/z2 /var/tmp/rtos/C3/z2_nompu -- -DCONFIG_ARM_MPU=n 2>&1 | grep -i "warning\|error"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C3/z2_nompu/zephyr/zephyr.elf 5'
```

```text
           FLASH:       12868 B         4 MB      0.31%
             RAM:        6136 B         4 MB      0.15%
reset -> z_cstart (EARLY)      104640 ns
-> PRE_KERNEL_1                7640 ns
-> PRE_KERNEL_2                23560 ns
-> main thread (POST_KERNEL)   18000 ns
-> APPLICATION                 19680 ns
-> main()                      2480 ns
TOTAL reset -> main()          176000 ns
reset -> z_cstart (EARLY)      104640 ns
-> PRE_KERNEL_1                7640 ns
-> PRE_KERNEL_2                23560 ns
-> main thread (POST_KERNEL)   18000 ns
-> APPLICATION                 19680 ns
-> main()                      2480 ns
TOTAL reset -> main()          176000 ns
reset -> z_cstart (EARLY)      98520 ns
-> PRE_KERNEL_1                2160 ns
-> PRE_KERNEL_2                23560 ns
-> main thread (POST_KERNEL)   17920 ns
-> APPLICATION                 19680 ns
-> main()                      2480 ns
TOTAL reset -> main()          164320 ns
```

(세 번째 묶음이 MPU off 빌드.) **출력에서 볼 것**: main 스레드 첫 줄까지 104.64 + 7.64 + 23.56 + 18.00 = **153.8 µs**, `main()`까지 **176.0 µs**. MPU를 끄면 `arch_kernel_init`의 MPU 설정이 빠져 PRE_KERNEL_1 구간이 7.64 → 2.16 µs, 리셋 구간도 6 µs 줄어 164.3 µs. **스레드를 만들고 main 스레드로 넘어가는 구간(→ POST_KERNEL)은 18.0 µs**로, FreeRTOS의 "태스크 3개 생성 + 포트 시작" 97.6 µs보다 훨씬 짧다.

### 8.3 예제 13 — 어디에 명령을 썼나: QEMU 명령 단위 프로파일

**무엇을 확인하나**: 위 구간이 왜 그 길이인지. QEMU에 `-accel tcg,one-insn-per-tb=on -d exec,nochain`을 주면 실행한 **명령마다** 로그 한 줄이 나온다. icount에서 명령 1개 = 32 ns이니 줄 수가 곧 시간이다. 로그의 PC를 `nm -S` 심볼 표로 함수에 매핑하는 Python 스크립트를 썼다.

```python
# insn_profile.py <exec.log> <nm.txt> <stop_symbol> [start_symbol] : count executed instructions per function until stop_symbol
import sys, re, bisect, collections
log, nmf, stop = sys.argv[1:4]
start = sys.argv[4] if len(sys.argv) > 4 else None   # optional: start counting at this symbol
syms = []
for l in open(nmf):
    p = l.split()
    if len(p) >= 4 and p[2] in "tTwW":
        syms.append((int(p[0], 16) & ~1, int(p[1], 16), p[3]))
syms.sort(); addrs = [s[0] for s in syms]
def name(a):
    i = bisect.bisect_right(addrs, a) - 1
    return syms[i][2] if i >= 0 and a < syms[i][0] + max(syms[i][1], 2) else "?"
cnt = collections.Counter(); n = 0; on = start is None
for l in open(log):
    m = re.search(r"Trace \d+: \S+ \[[0-9a-f]+/([0-9a-f]+)/", l)
    if not m: continue
    f = name(int(m.group(1), 16))
    if not on:
        if f != start: continue
        on = True
    if f == stop: break
    cnt[f] += 1; n += 1
print(f"instructions {start or 'reset'} -> {stop}: {n}  (= {n*32} ns at icount shift=5)")
for f, c in cnt.most_common(8): print(f"  {c:6d}  {f}")
```

```sh
# Zephyr (z2 기본 빌드)
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C3 && timeout 20 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial null -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -accel tcg,one-insn-per-tb=on -d exec,nochain -D /var/tmp/rtos/C3/zexec.log -kernel z2/zephyr/zephyr.elf; arm-none-eabi-nm -S z2/zephyr/zephyr.elf > z2nm.txt; for s in z_cstart bg_thread_main main; do python3 -I /Users/donh/.rtos_scratch/C3/insn_profile.py zexec.log z2nm.txt $s; done'
# FreeRTOS (mfrt heap 16 KB)
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C3 && timeout 20 qemu-system-arm ... -d exec,nochain -D /var/tmp/rtos/C3/fexec.log -kernel mfrt_16384/app.elf; arm-none-eabi-nm -S mfrt_16384/app.elf > fnm.txt; python3 -I /Users/donh/.rtos_scratch/C3/insn_profile.py fexec.log fnm.txt main; python3 -I /Users/donh/.rtos_scratch/C3/insn_profile.py fexec.log fnm.txt first main'
```

```text
instructions reset -> z_cstart: 3254  (= 104128 ns at icount shift=5)
    2373  memset
     667  memcpy
     164  z_arm_interrupt_init
      14  ?
      12  arch_data_copy
       8  relocate_vector_table
       7  arch_bss_zero
       6  z_prep_c
instructions reset -> bg_thread_main: 4788  (= 153216 ns at icount shift=5)
    2373  memset
     667  memcpy
     164  z_arm_interrupt_init
     161  z_sys_init_run_level
     140  do_device_init
     140  arm_irq_priority_set
     100  arm_irq_enable
      99  z_arm_mpu_init
instructions reset -> main: 5497  (= 175904 ns at icount shift=5)
    2682  memset
     667  memcpy
     213  z_sys_init_run_level
     164  z_arm_interrupt_init
     140  do_device_init
     140  arm_irq_priority_set
     100  arm_irq_enable
      99  z_arm_mpu_init
instructions reset -> main: 6835  (= 218720 ns at icount shift=5)
    6601  memset
      78  memcpy
      53  global_stdio_init.part.0
      46  setvbuf
      25  Reset_Handler
      19  board_init
       9  __sinit
       4  _fflush_r
instructions main -> first: 3047  (= 97504 ns at icount shift=5)
    1514  memset
     235  prvInitialiseNewTask.constprop.0
     210  prvAddNewTaskToReadyList
     184  pvPortMalloc
     143  xPortStartScheduler
     119  vListInitialise
      84  vPortEnterCritical
      80  xTaskCreateStatic
```

(두 번째 FreeRTOS 명령의 QEMU 옵션은 첫 줄과 같아서 `...`로 줄였다. 앞 세 묶음이 Zephyr, 뒤 두 묶음이 FreeRTOS.)

**출력에서 볼 것**:

- **명령 수 × 32 ns가 카운터 측정과 맞는다**: Zephyr reset → z_cstart 3254개 = 104.1 µs(카운터 104.64), main 스레드 진입 4788개 = 153.2 µs(153.84), main 5497개 = 175.9 µs(176.0). FreeRTOS reset → main 6835개 = 218.7 µs(218.88), main → first 3047개 = 97.5 µs(97.6). 약 0.2~0.6 µs 차이는 카운터 읽기 위치와 리셋 직후 몇 명령 차이다.
- **Zephyr 리셋 구간의 73%는 `memset`이다** — 그런데 `.bss`는 591 B뿐이다. `arch_bss_zero()`가 picolibc `memset`을 부르는데, 이 빌드(-Os)의 `memset`이 591 B에 2373개 명령(바이트당 약 4개)을 썼다. `.data` 복사의 `memcpy`도 같다. 바이트 단위 루프로 보이지만 picolibc 빌드 옵션, 그리고 `arch_bss_zero()`가 `size -A`의 `bss` 섹션(591 B) 말고 다른 영역도 지우는지는 확인하지 않았다. 즉 Zephyr의 104 µs는 "커널이 무거워서"가 아니라 "작은 배열을 느린 루틴으로 채워서"다.
- **FreeRTOS 리셋 구간은 newlib-nano `memset`이 20 KB `.bss`를 6601개 명령(명령당 약 3 B — 워드 단위 저장으로 보인다)으로 채운 것**이다. 템플릿 `Reset_Handler`의 C 루프를 gcc -O2가 `memset` 호출로 바꿨다(`Reset_Handler` 자체는 25개 명령).
- **FreeRTOS main → 첫 태스크에서도 `memset`이 절반(1514개)** — 태스크 스택 3개(1 KB + 1 KB + 2 KB)를 0xa5로 채우는 것(`tskSET_NEW_STACKS_TO_KNOWN_VALUE`, stack overflow 검사·high water mark용, A4 §5.3). 포트 시작(`xPortStartScheduler` 143개 + 첫 진입)은 작은 몫이다.

### 8.4 차트 — 측정한 구간을 한 축에

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<line x1="200.0" y1="42" x2="200.0" y2="226" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="200.0" y="36" font-size="12" text-anchor="middle">0 µs</text>
<line x1="290.0" y1="42" x2="290.0" y2="226" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="290.0" y="36" font-size="12" text-anchor="middle">100 µs</text>
<line x1="380.0" y1="42" x2="380.0" y2="226" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="380.0" y="36" font-size="12" text-anchor="middle">200 µs</text>
<line x1="470.0" y1="42" x2="470.0" y2="226" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="470.0" y="36" font-size="12" text-anchor="middle">300 µs</text>
<line x1="560.0" y1="42" x2="560.0" y2="226" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="560.0" y="36" font-size="12" text-anchor="middle">400 µs</text>
<text x="192" y="73" font-size="12" text-anchor="end">FreeRTOS heap 128 KB</text>
<rect x="200.0" y="58" width="342.0" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<path d="M543.8,56 l6,12 l-6,12" fill="none" stroke="currentColor"/>
<text x="558.2" y="73" font-size="12">1463.4 µs (축 밖)</text>
<text x="192" y="117" font-size="12" text-anchor="end">FreeRTOS heap 16 KB</text>
<rect x="200.0" y="102" width="197.0" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="397.0" y="102" width="82.5" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="479.5" y="102" width="5.4" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="490.8" y="117" font-size="12">316.5 µs → 첫 태스크</text>
<text x="192" y="161" font-size="12" text-anchor="end">Zephyr 기본</text>
<rect x="200.0" y="146" width="94.2" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="294.2" y="146" width="44.3" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="338.5" y="146" width="19.9" height="20" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="364.4" y="161" font-size="12">153.8 / 176.0 µs</text>
<text x="192" y="205" font-size="12" text-anchor="end">Zephyr MPU off</text>
<rect x="200.0" y="190" width="88.7" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="288.7" y="190" width="39.3" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="327.9" y="190" width="19.9" height="20" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="353.9" y="205" font-size="12">142.2 / 164.3 µs</text>
<rect x="20" y="245" width="14" height="14" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="40" y="256" font-size="12">시작 코드 (.data 복사, .bss 0 채우기)</text>
<rect x="350" y="245" width="14" height="14" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="370" y="256" font-size="12">커널 준비: 태스크·스레드 생성, 드라이버 init</text>
<rect x="20" y="267" width="14" height="14" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="40" y="278" font-size="12">포트 시작 → 첫 태스크 첫 줄 (FreeRTOS)</text>
<rect x="350" y="267" width="14" height="14" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="370" y="278" font-size="12">main 스레드 안 init (Zephyr)</text>
<text x="20" y="308" font-size="12">QEMU icount, FPGAIO 25 MHz 카운터. Zephyr 라벨 = 첫 스레드 / main()</text>
</svg>
```

그림 7 — 예제 11·12에서 잰 reset → 첫 태스크(FreeRTOS) / 첫 스레드·`main()`(Zephyr). 1 px = 1 µs, heap 128 KB 막대는 축 밖으로 잘랐다. 파랑(시작 코드)이 가장 크고 설정(heap 크기, libc `memset`)에 좌우되며, 커널이 "첫 태스크로 들어가는" 순간 자체(FreeRTOS 초록 5.8 µs)는 작다.

### 8.5 A7 숫자와 맞춰 보기

| 항목 | A7 (다른 앱) | 이 노트 | 차이의 이유 |
|---|---|---|---|
| FreeRTOS heap 128 KB, reset → main | 1366.56 µs | 1365.76 µs | `.bss`가 몇십 바이트 다르다 (앱 전역 변수) |
| FreeRTOS heap 16 KB, reset → main | 219.68 µs | 218.88 µs | 같은 이유 |
| FreeRTOS main → 첫 태스크 | 148.4 µs (368.12 − 219.68) | 97.6 µs | A7은 사용자 태스크 2개 + TIMER0 설정, 이 노트는 1개. 태스크 하나 ≈ 스택 채우기 + 생성 약 30 µs |
| Zephyr reset → main (기본) | 205.0 µs | 176.0 µs | A7 앱은 `K_THREAD_DEFINE` 스레드 + `IRQ_CONNECT`가 있고, `.bss`·`.data`가 더 크다(바이트 단위 `memset`·`memcpy`라 그대로 시간이 된다) |
| Zephyr MPU off | 191.2 µs | 164.3 µs | 같은 방향: MPU 초기화 몫 약 12~14 µs |

결론은 A7과 같다: **"RTOS 부팅이 느리다"의 대부분은 커널이 아니라 startup의 메모리 채우기**이고, 첫 태스크로 들어가는 메커니즘(이 노트의 주제) 자체는 수 µs다. 다만 Zephyr의 바이트 단위 `memset`은 이 노트에서 새로 찾은 것이다 — 실칩에서 큰 `.bss`를 가진 Zephyr 앱이라면 picolibc 최적화 옵션이나 `.noinit` 배치를 확인할 가치가 있다(확인 못 한 추정이므로 B11에서 검증할 것).

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나 — 소스 지도

이 노트 전체가 커널 안 이야기였으니, 여기서는 **어느 파일의 어느 함수를 열면 되는지**만 지도로 모은다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<text x="170" y="20" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS-Kernel V11.3.2</text>
<text x="510" y="20" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2</text>
<rect x="20" y="32" width="300" height="76" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="50" font-size="12">tasks.c</text>
<text x="30" y="66" font-size="12">prvInitialiseNewTask</text>
<text x="30" y="82" font-size="12">prvAddNewTaskToReadyList</text>
<text x="30" y="98" font-size="12">vTaskStartScheduler · prvCreateIdleTasks</text>
<rect x="360" y="32" width="300" height="76" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="50" font-size="12">kernel/init.c</text>
<text x="370" y="66" font-size="12">z_cstart · prepare_multithreading</text>
<text x="370" y="82" font-size="12">switch_to_main_thread</text>
<text x="370" y="98" font-size="12">bg_thread_main</text>
<rect x="20" y="116" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="134" font-size="12">timers.c</text>
<text x="30" y="150" font-size="12">xTimerCreateTimerTask · prvTimerTask</text>
<rect x="360" y="116" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="134" font-size="12">kernel/thread.c · lib/os/thread_entry.c</text>
<text x="370" y="150" font-size="12">z_setup_new_thread · z_thread_entry</text>
<rect x="20" y="168" width="300" height="124" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="186" font-size="12">portable/GCC/ARM_CM3/port.c</text>
<text x="30" y="202" font-size="12">pxPortInitialiseStack (182행)</text>
<text x="30" y="218" font-size="12">xPortStartScheduler (268행)</text>
<text x="30" y="234" font-size="12">prvPortStartFirstTask (247행)</text>
<text x="30" y="250" font-size="12">vPortSVCHandler (228행)</text>
<text x="30" y="266" font-size="12">prvTaskExitError</text>
<text x="30" y="282" font-size="12">vPortValidateInterruptPriority</text>
<rect x="360" y="168" width="300" height="124" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="370" y="186" font-size="12">arch/arm/core/cortex_m/</text>
<text x="370" y="202" font-size="12">reset.S: z_arm_reset (PSP 부팅)</text>
<text x="370" y="218" font-size="12">prep_c.c: z_prep_c</text>
<text x="370" y="234" font-size="12">thread.c: arch_new_thread (위조)</text>
<text x="370" y="250" font-size="12">thread.c: arch_switch_to_main_thread</text>
<text x="370" y="266" font-size="12">swap_helper.S: z_arm_pendsv</text>
<rect x="20" y="300" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="318" font-size="12">앱 FreeRTOSConfig.h: 핸들러 이름 매핑</text>
<text x="30" y="334" font-size="12">#define vPortSVCHandler SVC_Handler</text>
<rect x="360" y="300" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="370" y="318" font-size="12">arch/arm/include/cortex_m/</text>
<text x="370" y="334" font-size="12">stack.h: z_arm_interrupt_stack_setup</text>
<text x="340" y="368" font-size="12" text-anchor="middle">파랑 = 이식 가능한 커널 코어, 초록 = Cortex-M 전용 (포팅할 때 새로 쓰는 부분)</text>
</svg>
```

그림 8 — 첫 태스크 시작에 관련된 소스 위치. 새 CPU로 포팅할 때 다시 쓰는 것은 초록 상자뿐이다: "프레임을 어떤 모양으로 위조하나"와 "첫 프레임을 어떻게 꺼내나".

- **위조 프레임의 모양은 포트가 정하고, 그 모양을 쓰는 쪽도 포트다.** `tasks.c`는 `pxPortInitialiseStack()`이 돌려준 포인터를 `pxTopOfStack`에 저장할 뿐 내용을 모른다. 그래서 FPU가 있는 ARM_CM4F 포트는 프레임에 EXC_RETURN 값을 한 워드 더 넣고(lazy stacking 상태를 태스크마다 기억하려고), MPU 포트는 CONTROL 값을 더 넣는다 — 같은 원리, 다른 모양(B6, F5).
- **첫 진입 방법도 포트마다 다르다.** V11.3.2의 ARM_CM3·CM4F·CM0 포트는 모두 SVC를 쓴다(CM0은 `portasm.c`의 `vStartFirstTask`가 `svc`). 반면 RISC-V 포트(`portable/GCC/RISC-V/portASM.S`의 `xPortStartFirstTask`)는 위조 프레임에서 레지스터를 꺼낸 뒤 예외 복귀(`mret`)가 아니라 **`ret`로 들어가고 MIE 비트를 직접 켠다** — 소스 주석 그대로 "required as returns with ret not eret". 예제 8의 방법 B(예외 없는 진입)와 같은 계열이다. 공통점은 "위조 프레임을 평소 복원 코드와 같은 순서로 꺼낸다"이다.
- **ID로 이어지는 곳**: 평소의 저장·복원은 C2, 스케줄러가 `pxCurrentTCB`를 고르는 법은 C1, tick은 C5, BASEPRI 크리티컬 섹션은 C7, SVC를 시스템 콜로 쓰는 MPU 포트는 C8, idle 태스크가 하는 일은 C9.

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 — "모터가 안전값을 잡기 전"의 시간

예를 들어 드론 FC(STM32H7)라면 리셋 직후 ESC 출력(PWM/DShot)은 **스케줄러 시작 전에** 안전값(0 스로틀 또는 출력 끔)으로 잡아야 한다. 이 노트에서 본 대로 `main()` → 첫 태스크 사이에는 수십~수백 µs가 있고, 그 사이 인터럽트는 BASEPRI에 막혀 있으니, 그 일은 **태스크나 ISR이 아니라 `main()`의 첫 줄(또는 `SystemInit`)**에서 해야 한다. 반대로 IMU 데이터 준비(DRDY) 인터럽트는 **IMU 태스크가 돌기 시작한 뒤** 켠다 — 예제 7처럼 미리 켜 두면 첫 몇 샘플이 합쳐져 사라지고, 핸들이 NULL일 때 FromISR을 부를 수 있다.

```c
/* 드라이버 태스크가 자기 인터럽트를 켜는 패턴 (FreeRTOS) */
static TaskHandle_t s_imu_task;
static void imu_task(void *pv)
{
    (void)pv;
    s_imu_task = xTaskGetCurrentTaskHandle();   /* ISR이 쓸 핸들을 먼저 준비 */
    imu_configure_drdy();                        /* 센서 쪽 설정 */
    NVIC_SetPriority(EXTI_IMU_IRQn, 6);          /* 4비트 칩: 6 << 4 = 0x60 >= 0x50 (MAX_SYSCALL) */
    NVIC_EnableIRQ(EXTI_IMU_IRQn);               /* 이제야 켠다 */
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        imu_read_and_publish();
    }
}
```

(실칩 HAL 함수 이름이 들어간 스케치라 이 랩에서 실행하지 않았다. 원리는 예제 7과 같다.)

### 10.2 로봇 관절 모터 컨트롤러 — 20 kHz FOC는 커널 밖에

FOC 전류 루프 ISR(20 kHz, 50 µs 주기)은 보통 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 zero-latency 우선순위에 두고 커널 API를 부르지 않는다(A7의 하이브리드). 이 ISR은 BASEPRI에 막히지 않으니 **스케줄러 시작 전 창에도 돌 수 있다** — 그래서 시작 전에 PWM을 켜도 되는 유일한 인터럽트다. 단, 그 ISR이 태스크와 공유하는 데이터(설정값 setpoint)는 `main()`의 지역 변수가 아니라 정적 변수여야 한다(예제 4). 그리고 §3.2의 함정: 개발 보드 QEMU(8비트)에서 0x10으로 잘 돌던 설정이 실칩(4비트)에서 `0x10`은 우선순위 1로 해석되고, 0x08처럼 하위 비트만 쓴 값은 assert에 걸린다.

### 10.3 센서 허브 MCU — 부트로더 뒤의 앱

Linux SoC 옆 센서 허브는 흔히 MCUboot 같은 부트로더 뒤에서 앱이 0x08008000 같은 곳에 있다. 이때 **VTOR**가 앱의 벡터 테이블을 가리키도록 부트로더나 앱 `SystemInit`이 설정해야 한다. `prvPortStartFirstTask()`는 VTOR를 따라가 초기 MSP를 읽고, `xPortStartScheduler()`의 핸들러 설치 검사도 VTOR 테이블을 본다(포트 소스 주석: "Systems with a configurable address for the interrupt vector table can also encounter assertion failures or even system faults here if VTOR is not set correctly"). VTOR가 부트로더 테이블에 남아 있으면 MSP가 부트로더의 스택 값으로 바뀌고 SVC가 부트로더 핸들러로 간다 — "스케줄러 시작 순간 죽음"의 대표 원인이다. 부트로더가 PRIMASK·BASEPRI를 닫은 채 점프하는 경우도 있는데, `prvPortStartFirstTask`의 `cpsie i`가 PRIMASK는 풀어 준다(BASEPRI는 SVC 핸들러가 0으로).

### 10.4 SSD 컨트롤러 펌웨어 — Don의 실행부와 비교

Don이 다룬 SSD 펌웨어는 코어마다 슈퍼 루프나 협력형 실행부가 있었고 "첫 태스크 시작"이라는 단계가 없었다. RTOS를 도입하는 컨트롤러라면 체크할 것은 이 노트 그대로다: (1) 코어별 인터럽트 스택(MSP)의 크기 — SVC 프레임 32 B와 중첩 ISR 깊이를 포함, (2) 부팅 로그·크래시 덤프 버퍼를 `.noinit`에 두어 리셋 시간과 데이터를 둘 다 지키기(§8의 `memset` 비용), (3) 멀티코어 부팅에서 코어마다 `vTaskStartScheduler`(또는 SMP 커널의 코어별 시작 경로, C10)를 언제 부르나.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| startup의 `SVC_Handler`·`PendSV_Handler` 이름을 FreeRTOS에 연결 안 함 | 시작하자마자 `configASSERT`(port.c 299·300행) 또는 기본 핸들러 무한 루프 | 벡터 테이블에 FreeRTOS 핸들러가 없다 | `FreeRTOSConfig.h`에 `#define vPortSVCHandler SVC_Handler` 등 (이 랩 템플릿처럼) |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY`를 shift 없이 씀 (예: 5) | port.c 339행 assert, 또는 assert를 끈 빌드에서 크리티컬 섹션이 안 막음 | 4비트 칩에서 하위 비트만 썼다 | `5 << (8 - __NVIC_PRIO_BITS)` (예제 2) |
| 값이 0이거나 칩 비트 수보다 작은 비트만 씀 | port.c 335행 assert (예제 2의 0x10 @ 3비트) | BASEPRI 0 = 마스킹 없음 | 구현 비트 안의 0이 아닌 값 |
| `main()` 지역 변수를 태스크에 넘김 | 처음엔 되다가 tick이 몇 번 지나면 값이 깨짐 | MSP가 리셋되어 ISR이 그 자리를 쓴다 (예제 4) | `static`·전역, 또는 정적 할당 |
| 스케줄러 시작 전에 주변장치 IRQ를 켬 | 첫 이벤트 몇 개가 사라짐, NULL 핸들로 FromISR → assert/HardFault | BASEPRI 0x20 창 + pending 비트 하나 (예제 7) | 받는 태스크 첫 줄에서 IRQ enable |
| heap에서 idle·timer를 만들면서 `configTOTAL_HEAP_SIZE`를 빠듯하게 | `vTaskStartScheduler()`가 돌아옴, main 끝으로 떨어짐 | idle 1 KB / timer 2 KB 할당 실패 (예제 5) | 정적 할당 + `configKERNEL_PROVIDED_STATIC_MEMORY`, 그리고 `vTaskStartScheduler()` 다음 줄에 실패 처리 |
| 부트로더 뒤에서 VTOR 미설정 | 시작 순간 HardFault 또는 엉뚱한 SVC 핸들러 | 초기 MSP·핸들러를 부트로더 테이블에서 읽음 | 앱 `SystemInit`에서 `SCB->VTOR` 설정 |
| SVCall 우선순위를 HAL이 낮춤 | HardFault, HFSR.FORCED, PC = `svc` 다음 (예제 3b) | BASEPRI 0x20에 SVC가 막혀 승격 | SVC 우선순위는 건드리지 않는다 (포트가 0으로) |
| 자작 커널에서 xPSR T 비트 누락 | HardFault, CFSR = 0x20000 (INVSTATE), PC = 태스크 진입점 (예제 8) | Thumb 상태가 아닌 채로 복귀 | xPSR = 0x01000000 |
| gdb로 브레이크 걸며 타이밍을 봄 | 브레이크 직후 엉뚱한 tick·ISR | 멈춘 동안 시계가 흐름 (§0 icount 함정, 실칩은 주변장치 타이머) | 타이밍은 카운터로 재고, 실칩은 DBGMCU freeze |

---

## 12. 면접에서 이렇게 말한다

**Q.** How does an RTOS start the very first task on a Cortex-M? Nothing has been saved yet.

**A.** 새 태스크마다 커널이 "예외에서 막 멈춘 것 같은" 스택 프레임을 위조해 둔다. FreeRTOS CM3 포트는 16워드 — 하드웨어 8워드(xPSR = Thumb 비트, PC = 태스크 함수, LR = 종료 트랩, R0 = 인자)와 R4–R11 8워드. 그다음 MSP를 벡터 테이블의 초기값으로 되돌리고 `svc 0`을 실행한다. SVC 핸들러는 저장 없이 복원 절반만 한다: 현재 TCB에서 SP를 읽어 R4–R11을 꺼내고, PSP에 넣고, BASEPRI를 0으로, LR을 0xFFFFFFFD로 바꿔 예외 복귀한다. 그러면 하드웨어가 나머지 8워드를 꺼내 태스크 함수에서 Thread 모드 + PSP로 시작한다.

> Every new task gets a forged stack frame that looks exactly like a task that was preempted inside an exception: the hardware part — xPSR with only the Thumb bit, PC set to the task function, LR set to an exit trap, R0 holding the parameter — plus R4 to R11 for the software-saved part. To start, the FreeRTOS Cortex-M3 port resets MSP to the initial value from the vector table and executes `svc 0`. The SVC handler does only the restore half of a context switch: it loads the stack pointer from the current TCB, pops R4–R11, writes PSP, clears BASEPRI and returns with EXC_RETURN 0xFFFFFFFD. The core unstacks the remaining eight words and lands in the task function in thread mode on the process stack. I've stepped through all 21 instructions in gdb on QEMU.

**꼬리 질문**: "Why SVC and not PendSV?" — 첫 진입에는 저장할 현재 태스크가 없고, 동기적으로 지금 당장 들어가야 하기 때문이다. PendSV는 최저 우선순위로 미뤄지는 비동기 예외라 "언제 들어갈지"가 BASEPRI·다른 ISR에 달려 있고, PendSV 핸들러는 저장 절반부터 시작한다.

**Q.** What happens to main()'s stack after the scheduler starts?

**A.** 버려진다. `prvPortStartFirstTask`가 MSP를 리셋 값으로 되돌리니 main과 `vTaskStartScheduler`의 프레임 자리는 이제 인터럽트 스택이다. QEMU에서 main의 지역 배열을 태스크에 넘겨 봤더니 태스크 첫 줄에서 이미 SVC 프레임과 PendSV 저장값으로 절반이 덮였고, tick 3번 뒤에는 전부 덮였다. 그래서 태스크와 공유할 것은 static으로 둔다. 덤으로 SVC 진입 프레임 32바이트는 PSP로 복귀하므로 아무도 꺼내지 않아 MSP 꼭대기에 영구히 남는다.

> It's discarded. The start routine reloads MSP from vector-table entry zero, so the frames of main and vTaskStartScheduler become the interrupt stack. I demonstrated it on QEMU: an array local to main, passed to a task, was half overwritten by the SVC frame and the PendSV prologue by the time the task ran, and fully overwritten after three ticks. Anything shared with tasks must be static or global. A side effect is that the 32-byte frame pushed by the SVC is never popped, because the exception returns to PSP, so the top 32 bytes of the interrupt stack are permanently lost.

**Q.** What if a task function returns?

**A.** FreeRTOS에서는 위조 프레임의 LR이 `prvTaskExitError`라 거기로 점프한다 — assert를 걸거나, assert가 없으면 BASEPRI를 올리고 무한 루프라 tick까지 멈춘다. 끝내야 하는 태스크는 `vTaskDelete(NULL)`. Zephyr는 PC를 래퍼 `z_thread_entry`로 두고 사용자 함수를 R0로 넘겨서, return하면 래퍼가 `k_thread_abort`를 불러 정상 종료한다.

> In FreeRTOS the forged LR is prvTaskExitError, so a return jumps there: it asserts, and without configASSERT it masks interrupts via BASEPRI and spins, which also stops the tick — one bad task freezes the system. A task that wants to finish calls vTaskDelete(NULL). Zephyr instead puts a wrapper, z_thread_entry, in the PC and passes the user function in R0, so returning is a clean exit through k_thread_abort.

**Q.** Can vTaskStartScheduler() return? What would you do about it?

**A.** 돌아올 수 있다. idle이나 timer 데몬 태스크를 heap에서 만들지 못하면 configASSERT를 거쳐 main으로 돌아온다. QEMU에서 heap 1500 B면 idle에서, 3000 B면 timer에서 실패하는 것을 재현했다. 대책은 둘: idle·timer를 정적 할당(`configSUPPORT_STATIC_ALLOCATION` + kernel-provided memory)으로 만들어 heap과 무관하게 하고, 그 다음 줄에 로그·안전 상태·리셋을 둔다.

> Yes — if the idle or timer daemon task can't be allocated, it asserts and returns to main. I reproduced it with a 1.5 KB heap failing on the idle task and a 3 KB heap failing on the timer task. I'd allocate both statically so start-up doesn't depend on heap state, and still put a fail-safe right after the call: log, drive outputs to a safe state, and reset.

**Q.** Why does FreeRTOS probe the NVIC priority bits before starting? What can go wrong?

**A.** 칩마다 구현된 우선순위 비트 수가 다르다(3~8비트). 포트가 우선순위 레지스터에 0xff를 쓰고 읽어 비트 수를 재고, `configMAX_SYSCALL_INTERRUPT_PRIORITY`가 0이 아닌지, 구현 안 된 비트를 쓰지 않았는지, 8비트 칩이면 서브 우선순위 비트가 0인지 검사한다. 이 결과가 나중에 "ISR이 허용 우선순위에서 FromISR을 불렀나" 검사의 기준이 된다. QEMU에서 `num-prio-bits`를 4·3으로 바꿔 4개의 assert를 다 터뜨려 봤다. 8비트 QEMU에서 멀쩡하던 0x10이 3비트 칩에서는 0이 되어 죽는다.

> Cortex-M parts implement anywhere from three to eight priority bits, and BASEPRI masking only works if the configured threshold uses implemented bits. The port writes 0xFF to an IRQ priority register, reads back how many bits stuck, and asserts that the max-syscall priority is non-zero, uses no unimplemented bits, and on 8-bit parts leaves the sub-priority bit clear. It also stores that threshold for vPortValidateInterruptPriority. On QEMU I emulated 4- and 3-bit NVICs and hit each assert; a value of 0x10 that works on the 8-bit model collapses to zero on a 3-bit chip.

**Q.** What happens if an interrupt fires before the scheduler starts and calls a FromISR API?

**A.** FreeRTOS에서는 첫 API 호출부터 스케줄러 시작까지 BASEPRI가 configMAX_SYSCALL에 올라간 채로 남는다(크리티컬 중첩 카운트가 0xaaaaaaaa로 시작해 0이 안 되므로). 그래서 커널 관리 인터럽트는 그동안 막히고, 여러 번 만료돼도 pending 하나로 합쳐져 시작 순간 한 번만 들어온다 — 이벤트를 잃는다. 더 위험한 건 규칙을 어긴 높은 우선순위 ISR이다: 막히지 않고, 우선순위 검사 기준값이 아직 0이라 assert도 안 걸린다. 시작 후에야 assert가 났다. 해결은 받는 태스크가 준비된 뒤 IRQ를 켜는 것.

> From the first API call until the scheduler starts, FreeRTOS leaves BASEPRI raised, because the critical-nesting counter starts at 0xAAAAAAAA and never returns to zero. Kernel-aware interrupts are held off, and since the NVIC keeps one pending bit, several expirations collapse into one interrupt when the first task opens the mask — events are lost. Worse, an interrupt wrongly placed above the syscall threshold isn't masked and isn't caught either, because the validation threshold is only computed inside xPortStartScheduler; in my test the assert fired only after the start. So I enable a peripheral's IRQ from the task that consumes it, once its handle exists.

**Q.** How does Zephyr enter main on Cortex-M, and how is that different?

**A.** Zephyr는 리셋 코드가 처음부터 PSP(인터럽트 스택 위)로 C 코드를 돌리고, `z_cstart`가 main 스레드와 idle 스레드를 만든 뒤 `arch_switch_to_main_thread()`에서 PSP를 main 스택 꼭대기로 바꾸고 인터럽트를 연 다음 `z_thread_entry(bg_thread_main)`로 그냥 분기한다. 예외가 없다(gdb에서 21개 명령 내내 IPSR 0). main은 처음부터 스레드라 main의 지역 변수는 안전하다. 다른 스레드는 나중에 PendSV가 위조 프레임(PC = z_thread_entry, R0 = 함수, R1–R3 = p1–p3)을 꺼내며 시작한다.

> Zephyr runs its boot C code on PSP from reset, using the interrupt stack. z_cstart creates the main and idle threads, then arch_switch_to_main_thread sets PSP to the top of the main stack, unmasks interrupts and simply branches to z_thread_entry with bg_thread_main — no exception at all; I saw IPSR stay zero for all 21 instructions. Because main is already a thread, its locals are safe. Other threads start later when PendSV restores their forged frame, whose PC is z_thread_entry with the entry function in R0 and the three parameters in R1–R3.

**Q.** How long does it take from reset to the first task, and where does the time go?

**A.** QEMU icount(25 MHz급)에서 FreeRTOS는 heap 16 KB일 때 316 µs, 128 KB면 1463 µs — 차이 전부가 `.bss` 0 채우기다. main 뒤 커널 몫은 97.6 µs, 그중 절반이 스택을 0xa5로 채우는 memset이고, 포트 시작부터 첫 태스크 첫 줄까지는 5.8 µs다. Zephyr는 첫 스레드까지 154 µs, main()까지 176 µs인데 리셋 구간의 대부분이 작은 .bss에 대한 느린 바이트 단위 memset이었다. 명령 단위 QEMU 트레이스로 함수별로 나눠 확인했다.

> On QEMU with instruction counting, at 25 MHz scale: FreeRTOS reaches the first task in 316 µs with a 16 KB heap and 1.46 ms with 128 KB — the entire difference is zeroing .bss. After main the kernel spends about 98 µs, half of it filling task stacks with the 0xA5 pattern, and only 5.8 µs from xPortStartScheduler to the first task instruction. Zephyr reaches its first thread in 154 µs and main in 176 µs, and most of the reset phase is a slow byte-wise memset on a tiny .bss. I attributed every instruction to a function with a QEMU exec trace, so I'd look at startup memory initialization before blaming the kernel.

---

## 13. 직접 해보기

1. **(개념)** 예제 1의 `Tmr Svc` 프레임에서 R0 = 0, PC = 0x1734다. 이 태스크가 첫 실행에서 `prvTimerTask(NULL)`로 시작한다는 것을 프레임만 보고 설명하라. 그리고 같은 태스크가 한 번 막혔다가(C2의 PendSV 저장 뒤) 다시 꺼내질 때 프레임의 PC는 무엇일지 예측하라. 정답/힌트: AAPCS에서 R0 = 첫 인자. 두 번째부터는 PC = 막혔던 지점(예: `vTaskDelay` 안 `portYIELD` 다음) — 더 이상 함수 시작 주소가 아니다. gdb로 `vTaskSwitchContext` 브레이크 뒤 `pxCurrentTCB->pxTopOfStack[14]`를 보면 확인된다.
2. **(코드)** 예제 8의 런처를 확장해 R4–R11까지 16워드 프레임을 위조하고, SVC 핸들러에서 `ldmia r0!, {r4-r11}`를 추가하라. R4에 0x44444444를 위조해 두고 태스크 안에서 `mov`로 읽어 보라(컴파일러가 R4를 덮기 전에 naked 함수나 인라인 asm으로). 정답/힌트: 프레임 순서는 낮은 주소부터 R4…R11, R0…xPSR. 태스크 진입 첫 명령에서 gdb로 `info registers r4`.
3. **(코드)** 예제 4를 고쳐 `buf`를 `static`으로 바꾸고 같은 출력을 비교하라. 그리고 MSP 크기(링커 `_Main_Stack_Size` 4 KB)를 줄였을 때 어디서부터 깨지는지 생각하라. 정답/힌트: static이면 `.bss`라서 끝까지 1111…cccc 그대로. MSP는 이제 ISR 전용이므로 필요 크기 = 가장 깊은 ISR 중첩 + SVC 잔여 32 B.
4. **(측정)** 예제 11에서 사용자 태스크 스택을 256 → 1024워드로 바꾸고 "main → xTaskCreate done"이 몇 µs 늘어나는지 재라. `configCHECK_FOR_STACK_OVERFLOW 0` + `configUSE_TRACE_FACILITY 0`이어도 0xa5 채우기가 남는지 `tasks.c`의 `tskSET_NEW_STACKS_TO_KNOWN_VALUE` 정의 조건을 읽고 예측한 뒤 확인하라. 정답/힌트: 예제 13에서 newlib-nano memset ≈ 명령당 3 B → 3 KB 더 = 약 1000개 명령 = 약 32 µs. 정의 조건은 `tasks.c` 130행(`configCHECK_FOR_STACK_OVERFLOW > 1`, trace facility, `INCLUDE_uxTaskGetStackHighWaterMark`(2) 중 하나라도 켜져 있으면 1) — 템플릿은 high water mark API를 켜 두었으니 두 옵션을 꺼도 여전히 채운다.
5. **(측정)** 예제 7을 `num-prio-bits=4`로 돌리고 TIMER0 우선순위를 `0x50`·`0x40`으로 바꿔 configMAX_SYSCALL `0x20` 기준 결과가 어떻게 달라지는지 예측·확인하라. 4비트 칩에서 `0x48`을 쓰면 실제 우선순위는 무엇인가? 정답/힌트: 0x48 → 하위 4비트 버림 → 0x40. 둘 다 0x20보다 숫자가 크니 커널 관리 대상(정상).
6. **(Zephyr)** 예제 10의 gdb 스크립트에 `break z_arm_pendsv`를 더해, idle 스레드가 처음 실행될 때(PendSV가 위조 프레임을 꺼낼 때) PSP가 예제 9의 `idle psp=0x200018c0`과 같은지 확인하라. 정답/힌트: main이 처음 막히는 순간(예: `k_sleep`) PendSV가 idle의 `callee_saved.psp`를 PSP로 쓴다. 예외 복귀 뒤 PC = 0x2156(`z_thread_entry`), R0 = `idle`.
7. **(설계)** 부트로더(0x0) + 앱(0x10000) 구조에서 앱의 FreeRTOS가 시작 순간 HardFault가 난다. 이 노트의 경로를 따라 확인할 레지스터 5개를 순서대로 적어라. 정답/힌트: VTOR(0xE000ED08) → vector[0](초기 MSP) → vector[11](SVC 핸들러 주소) → SHPR2(SVC 우선순위) → BASEPRI/PRIMASK(부트로더가 닫았나), 그리고 HFSR/CFSR(M1).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| fake frame (initial stack frame) | 위조 초기 스택 프레임 | 한 번도 안 돈 태스크를 "예외에서 멈춘 태스크"처럼 보이게 꾸민 스택 꼭대기 16워드 |
| `pxPortInitialiseStack` | 포트의 프레임 위조 함수 | xPSR·PC·LR·R0 등을 쓰고 새 스택 꼭대기를 돌려준다 |
| `prvTaskExitError` | 태스크 종료 트랩 | 위조 LR. 태스크 함수가 return하면 여기로 온다 |
| `xPortStartScheduler` | 포트의 시작 함수 | 핸들러 확인, 우선순위 비트 탐지, SHPR·SysTick 설정, 첫 태스크 진입 |
| `prvPortStartFirstTask` | 첫 진입 어셈블리 | MSP를 vector[0]로 리셋, 마스크 해제, `svc 0` |
| `vPortSVCHandler` | SVC 핸들러 | 위조 프레임의 복원 절반만 수행하고 PSP로 예외 복귀 |
| MSP / PSP | Main / Process Stack Pointer | 시작 뒤 MSP = ISR·핸들러 공용 스택, PSP = 태스크별 스택 |
| CONTROL.SPSEL | 스택 선택 비트 | 1이면 Thread 모드가 PSP를 쓴다. 첫 태스크 안에서 CONTROL = 2 |
| EXC_RETURN | 예외 복귀 값 | LR에 든 0xFFFFFFFx. 0xFFFFFFF9 = Thread·MSP, 0xFFFFFFFD = Thread·PSP |
| VTOR | Vector Table Offset Register | 0xE000ED08. 벡터 테이블 주소, 0번 칸이 초기 MSP |
| SHPR2 / SHPR3 | 시스템 핸들러 우선순위 레지스터 | SHPR2 bit 31:24 = SVCall, SHPR3 = PendSV(23:16)·SysTick(31:24) |
| BASEPRI | 우선순위 기준 마스크 | 이 값 이하 우선순위(숫자 큰 쪽) 예외를 막는다. 0 = 막지 않음 |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 커널 API 허용 상한 | 크리티컬 섹션의 BASEPRI 값. 이보다 높은 ISR은 FromISR 금지 |
| `ucMaxSysCallPriority` | 검사 기준값 | 시작 때 계산되어 `vPortValidateInterruptPriority`가 쓴다. 시작 전에는 0 |
| `uxCriticalNesting` | 크리티컬 섹션 중첩 수 | 초기값 0xaaaaaaaa → 시작 전 BASEPRI가 안 풀린다. 시작 때 0 |
| num-prio-bits | 구현된 우선순위 비트 수 | 칩마다 3~8. QEMU `-global armv7m.num-prio-bits=N`으로 흉내 |
| idle task | 유휴 태스크 | 우선순위 0. `vTaskStartScheduler`가 만든다. 삭제된 태스크 메모리 회수(C9) |
| timer daemon (`Tmr Svc`) | 타이머 데몬 태스크 | 소프트웨어 타이머 콜백 실행. 기본 우선순위 = 최고 → 첫 태스크가 된다 |
| `arch_switch_to_main_thread` | Zephyr Cortex-M main 진입 | PSP를 main 스택으로 바꾸고 `z_thread_entry`로 `bx`. 예외 없음 |
| `z_thread_entry` | Zephyr 스레드 래퍼 | 위조 PC. 사용자 함수를 부르고 return하면 `k_thread_abort` |
| `z_interrupt_stacks` | Zephyr 인터럽트 스택 배열 | `CONFIG_ISR_STACK_SIZE`. 부팅 중에는 PSP로, 시작 뒤에는 MSP로 쓴다 |
| escalation | 우선순위 승격 | 막힌 동기 예외(SVC 등)나 비활성 fault가 HardFault로 바뀌는 것 (HFSR.FORCED) |
| INVSTATE | UsageFault 원인 비트 | CFSR bit 17. T 비트 없이 실행하려 할 때 |

---

## 15. 요약 & 체크리스트

한 번도 실행된 적 없는 태스크를 시작하는 문제를 RTOS는 "**복원으로 바꿔서**" 푼다. 커널은 새 태스크마다 예외에서 멈춘 것처럼 프레임을 위조하고(FreeRTOS CM3: 16워드, xPSR = 0x01000000, PC = 함수 & ~1, LR = `prvTaskExitError`, R0 = 인자), `vTaskStartScheduler()`가 idle·timer 태스크를 만든 뒤 `xPortStartScheduler()`가 벡터 테이블의 핸들러, NVIC 우선순위 비트 수(QEMU 8비트 / 흉내 낸 4·3비트), PendSV·SysTick 최저·SVC 최고, SysTick, 중첩 카운트를 맞추고, `prvPortStartFirstTask()`가 **MSP를 벡터 테이블 값으로 되돌려 main 스택을 버린 다음** `svc 0`을 실행한다. SVC 핸들러는 복원 절반만 하고 EXC_RETURN 0xFFFFFFFD로 Thread 모드 + PSP에 들어간다 — gdb로 21개 명령의 모든 레지스터 값을 확인했다. 시작 전에는 BASEPRI가 0x20으로 남아 커널 관리 인터럽트가 합쳐져 사라지고, 규칙을 어긴 ISR은 검사 기준이 없어 잡히지 않는다. heap이 부족하면 `vTaskStartScheduler()`가 돌아오며, 정적 할당이 그 경로를 없앤다. Zephyr는 부팅을 처음부터 PSP로 돌리고 `arch_switch_to_main_thread()`에서 예외 없이 main 스레드로 분기하며, 위조 프레임의 PC는 래퍼 `z_thread_entry`다. 시간으로 보면 첫 진입 메커니즘은 5.8 µs이고, reset → 첫 태스크의 대부분은 startup의 메모리 채우기(`.bss`, 스택 0xa5)다.

- [ ] 위조 프레임 16워드를 순서대로 말하고, xPSR·PC·LR·R0 값의 이유를 댈 수 있다
- [ ] gdb에서 TCB → `pxTopOfStack` → 프레임을 덤프하고 `info symbol`로 PC·LR을 해석할 수 있다
- [ ] 스케줄러 시작 전 `pxCurrentTCB` 선택 규칙(최고 우선순위, 같으면 나중 것)과 기본 설정에서 timer 태스크가 먼저 도는 이유를 안다
- [ ] `xPortStartScheduler()`의 10단계를 순서대로 말할 수 있다
- [ ] 우선순위 비트 탐지와 3개의 configASSERT가 각각 어떤 설정 실수를 잡는지 안다 (339·335·369행)
- [ ] `prvPortStartFirstTask` + `vPortSVCHandler`의 모든 명령을 설명하고, LR 0xFFFFFFF9 → 0xFFFFFFFD의 의미를 안다
- [ ] main 스택이 왜 버려지는지, 그리고 SVC 프레임 32바이트가 MSP에 남는다는 것을 안다
- [ ] SVC 우선순위가 BASEPRI에 막히면 HardFault(FORCED)로 승격된다는 것을 안다
- [ ] 첫 API 호출부터 시작까지의 BASEPRI 창과 그 결과(인터럽트 합쳐짐, 검사 기준 0)를 설명할 수 있다
- [ ] `vTaskStartScheduler()`가 돌아오는 조건과 대책을 안다
- [ ] Zephyr의 `z_cstart` → `prepare_multithreading` → `arch_switch_to_main_thread` → `bg_thread_main` 경로와 FreeRTOS와의 차이를 표로 말할 수 있다
- [ ] reset → 첫 태스크 시간을 분해해 커널 몫과 startup 몫을 구분할 수 있다

---

## 참고 자료

- FreeRTOS-Kernel V11.3.2 소스 (랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `portable/GCC/ARM_CM3/port.c`(`pxPortInitialiseStack`, `prvTaskExitError`, `vPortSVCHandler`, `prvPortStartFirstTask`, `xPortStartScheduler`, `vPortSetupTimerInterrupt`, `vPortEnterCritical`·`vPortExitCritical`, `vPortValidateInterruptPriority`), `tasks.c`(`prvInitialiseNewTask`, `prvAddNewTaskToReadyList`, `prvCreateIdleTasks`, `vTaskStartScheduler`), `timers.c`(`xTimerCreateTimerTask`, `prvCheckForValidListAndQueue`). 저장소: https://github.com/FreeRTOS/FreeRTOS-Kernel
- FreeRTOS 문서 "Running the RTOS on a ARM Cortex-M Core" (인터럽트 우선순위, `configMAX_SYSCALL_INTERRUPT_PRIORITY` 설명 — port.c 주석이 가리키는 페이지): https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html
- FreeRTOS FAQ (핸들러 설치 assert가 가리키는 페이지): https://www.freertos.org/Why-FreeRTOS/FAQs
- Zephyr v4.4.2 소스 (랩 `/var/rtoslab/zephyrproject/zephyr`): `kernel/init.c`(`z_cstart`, `prepare_multithreading`, `switch_to_main_thread`, `bg_thread_main`, `init_idle_thread`), `arch/arm/core/cortex_m/reset.S`(`z_arm_reset`), `prep_c.c`(`z_prep_c`), `thread.c`(`arch_new_thread`, `arch_switch_to_main_thread`, `arch_irq_unlock_outlined`), `swap_helper.S`(`z_arm_pendsv`), `arch/arm/include/cortex_m/stack.h`(`z_arm_interrupt_stack_setup`), `lib/os/thread_entry.c`(`z_thread_entry`). 저장소: https://github.com/zephyrproject-rtos/zephyr
- Arm, "ARMv7-M Architecture Reference Manual" (DDI 0403) — 예외 진입·복귀, EXC_RETURN, CONTROL, BASEPRI, SHPR, UFSR.INVSTATE, HFSR.FORCED: https://developer.arm.com/documentation/ddi0403/latest/
- Arm, "Cortex-M3 Devices Generic User Guide" (DUI 0552) — SCB 레지스터(VTOR, SHPR2/3, ICSR, AIRCR), SysTick: https://developer.arm.com/documentation/dui0552/latest/
- QEMU 문서 — icount(`-icount shift=…,align=…,sleep=…`), `-d exec`, `-accel tcg,one-insn-per-tb=on`: https://www.qemu.org/docs/master/system/qemu-manpage.html (`armv7m.num-prio-bits` 속성은 QEMU 10.2의 `-device armv7m,help` 출력에서 확인)
- 이 노트 세트: A4(태스크 모델 — 위조 프레임 첫 소개, return), A7(부팅 시간 측정), B2·B4·B5(예외·SVC/PendSV/SysTick·reset → main), C2(PendSV 컨텍스트 스위치), M1(HardFault 해독)
