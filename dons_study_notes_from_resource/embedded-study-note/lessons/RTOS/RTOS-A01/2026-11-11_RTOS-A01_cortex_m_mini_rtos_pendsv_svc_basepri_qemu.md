---
id: RTOS-A01
time: 120분
updated: 2026-11-11
---
# RTOS-A01 Cortex-M 미니 RTOS 완성: 우선순위 스케줄러 + 동기화

## 학습 목표

- 태스크 **초기 스택 프레임** 16 워드(xPSR `0x01000000`, PC `& ~1`, LR = 종료 트랩, R0 = 인자, R4–R11 칸)를 손으로 쌓고, 각 칸이 왜 그 값이어야 하는지 HardFault 레지스터 값으로 설명할 수 있다.
- **SVC 로 첫 태스크를 시작**(MSP 리셋 → `svc 0` → EXC_RETURN `0xFFFFFFFD`)하고, FreeRTOS 가 왜 이 길을 택하는지(그리고 RP2040 포트의 대안)를 말할 수 있다.
- **PendSV 컨텍스트 스위치**(R4–R11 저장 → TCB 에 PSP → 다음 태스크 선택 → 복원)를 naked 함수 + 인라인 asm 으로 쓰고, PendSV·SysTick 이 왜 **최저 우선순위**여야 하는지 결함 주입으로 확인한다.
- 비트맵(CLZ) 스케줄러 · delay · 카운팅 세마포어 · **우선순위 상속 뮤텍스**를 진짜 Cortex-M3(QEMU)에서 돌리고, 역전 시나리오의 완료 tick(상속 OFF 11 vs ON 5)을 assert 한다.
- **BASEPRI 크리티컬 섹션**이 "시스콜 등급" IRQ 는 막고 문턱 위 IRQ 는 통과시킨다는 것, 그리고 길게 막으면 SysTick 과 IRQ 펜딩이 **합쳐져 사라진다**는 것을 측정한다.

## 1. 문제

"PendSV 핸들러를 화이트보드에 써 보세요. 첫 태스크는 어떻게 시작하죠? BASEPRI 와 PRIMASK 중 뭘 쓰고 왜죠?" — Cortex-M 펌웨어/RTOS 직무의 스태프급 면접에서 FreeRTOS 를 "써 봤다" 와 "포트를 이해한다" 를 가르는 질문이다. 지금까지 RTOS-B/M 에서는 호스트(ucontext, 시뮬레이터)에서 미니 커널을 만들었다. 이번엔 그 커널을 **진짜 Cortex-M3 포트**로 옮긴다: `qemu-system-arm -M mps2-an385` 위에서 벡터 테이블부터 PendSV 까지 전부 우리 코드다 (BM-B08 의 부팅 틀 위에 얹는다).

이 펌웨어의 예외/우선순위 배치 (NVIC 는 **숫자가 작을수록 급하다** — 태스크 우선순위와 반대 방향):

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">RTOS-A01 우선순위 사다리 (NVIC 값, QEMU 는 8 비트 구현 — 실측)</text>
  <line x1="120" y1="40" x2="120" y2="300" stroke="currentColor" stroke-width="2"/>
  <text x="20" y="48" fill="currentColor" font-family="monospace">0x00 급함</text>
  <text x="20" y="298" fill="currentColor" font-family="monospace">0xFF 느긋</text>
  <rect x="140" y="36" width="250" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="150" y="54" fill="currentColor">SVCall 0x00 — 첫 태스크 시작 (svc 0)</text>
  <rect x="140" y="76" width="250" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="150" y="94" fill="currentColor">IRQ31 0x20 "모터 과전류" — 커널 호출 금지</text>
  <line x1="110" y1="120" x2="720" y2="120" stroke="currentColor" stroke-dasharray="6 4" stroke-width="2"/>
  <text x="400" y="114" fill="currentColor" font-weight="bold">BASEPRI 문턱 0x40</text>
  <text x="400" y="134" fill="currentColor">= configMAX_SYSCALL_INTERRUPT_PRIORITY</text>
  <text x="400" y="152" fill="currentColor">crit_enter() 동안 선 아래(≥ 0x40)는 펜딩만</text>
  <rect x="140" y="160" width="250" height="26" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="150" y="178" fill="currentColor">IRQ30 0x80 "UART RX" — sem_give_from_isr</text>
  <rect x="140" y="240" width="250" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="150" y="258" fill="currentColor">SysTick 0xFF · PendSV 0xFF (최저)</text>
  <text x="410" y="250" fill="currentColor">SysTick: tick + 깨우기 → PendSV 펜딩</text>
  <text x="410" y="268" fill="currentColor">PendSV: 다른 ISR 이 다 끝난 뒤 스위치</text>
  <rect x="410" y="166" width="310" height="60" rx="4" fill="none" stroke="currentColor"/>
  <text x="420" y="184" fill="currentColor">Handler 모드 = MSP (0x2000FFE0 근처)</text>
  <text x="420" y="202" fill="currentColor">Thread 모드 태스크 = PSP (태스크마다 1 KB)</text>
  <text x="420" y="220" fill="currentColor">CONTROL = 0x2 (SPSEL=1, 특권)</text>
  <text x="140" y="318" fill="currentColor">태스크: CTRL 10 · W 12 · C 5 · H 3 · M 2 · L 1 · IDLE 0 (숫자 클수록 급함, FreeRTOS 방식)</text>
</svg>
```

### Part A — 포트 계층: 초기 프레임, SVC 첫 시작, PendSV 스위치

- **TODO 1** `task_stack_init(top, fn, arg)`: 스택 꼭대기에서 16 워드를 쌓아 "방금 PendSV 로 쫓겨난 태스크" 처럼 보이게 만든다. 반환값 = 새 sp (= `top - 16`).
- **TODO 3** `SVC_Handler` (naked): `g_cur->sp` 에서 R4–R11 꺼내기 → PSP 설정 → BASEPRI 0 → `lr |= 0xD` → `bx lr`. (`start_first_task` 가 MSP 를 벡터 [0] 으로 되돌리고 `cpsie i` → `svc 0`.)
- **TODO 2** `PendSV_Handler` (naked): PSP 아래에 R4–R11 저장 → `g_cur->sp` 갱신 → BASEPRI 올리고 `kernel_switch_context()` → 새 태스크 복원.

### Part B — 커널: 비트맵 스케줄러, delay, 세마포어, 상속 뮤텍스

- 주어짐: CLZ 비트맵 ready list(RTOS-B04), `task_delay`, 카운팅 세마포어(+ `sem_give_from_isr`), `kernel_tick`(SysTick), 워치독(400 tick).
- **TODO 5** `mutex_lock` / `mutex_unlock` 의 **우선순위 상속**: 막힐 때 소유자를 내 우선순위로, 풀 때 `base_prio` 로.
- 검증: 선점 순서 로그 `HMLhml`, ISR → 세마포어 → 높은 태스크가 ISR 끝에서 바로 (`aXb`), 카운팅 + 타임아웃, M03/M04 의 H/M/L 역전 시나리오(상속 OFF/ON 완료 tick), 상속 중 대기자 타임아웃 → 우선순위 복원.

### Part C — BASEPRI 크리티컬 섹션 + gdb 로 스위치아웃된 태스크 보기

- **TODO 4** `crit_enter/crit_exit` (중첩 카운트 + BASEPRI), `isr_mask/isr_unmask` (이전 값 저장/복원).
- 검증: 크리티컬 섹션 안에서 IRQ30 은 `NVIC->STIR` 로 3 번, IRQ31 은 `ISPR` 로 펜딩 → IRQ31 은 **즉시**(BASEPRI 0x40 을 본다), IRQ30 은 나갈 때 **1 번만**; SysTick 3 바퀴 동안 막으면 tick 은 1 만 오른다.
- `run_qemu.sh gdb`: M 이 L 을 선점한 순간 멈춰 PSP/MSP/CONTROL, L 과 H 의 스택 프레임 16 워드를 읽는다.

## 2. Problem (English)

> **Port a tiny priority-based RTOS kernel to a Cortex-M3 (QEMU `mps2-an385`).**
>
> You are given the startup code, semihosting I/O, a bitmap ready list, delay/semaphore/mutex logic and a self-test. Implement the port layer:
>
> 1. `uint32_t *task_stack_init(uint32_t *top, void (*fn)(void *), void *arg)` — build the initial frame so that the first exception return into the task starts `fn(arg)` in Thumb state, and a `return` from `fn` lands in `task_exit_trap`. `top` is 8-byte aligned. Return the new stack pointer (16 words below `top`). R4–R11 go below the hardware frame, R4 at the lowest address.
> 2. `SVC_Handler` / `start_first_task` — start the first task with `svc 0`: reset MSP to the vector-table value, enable interrupts, and in the SVC handler restore R4–R11 from `g_cur->sp`, load PSP, clear BASEPRI and return with `EXC_RETURN = 0xFFFFFFFD`.
> 3. `PendSV_Handler` — save R4–R11 below the hardware-stacked frame on PSP, store the new top in `g_cur->sp` (offset 0 of the TCB), call `kernel_switch_context()` with BASEPRI raised, restore the next task.
> 4. `crit_enter/crit_exit` — nestable critical section using BASEPRI = `0x40`; IRQs with priority value `< 0x40` must keep running inside it.
> 5. Priority inheritance in `mutex_lock/mutex_unlock`.
>
> Constraints: `-ffreestanding -nostdlib`, no CMSIS headers, PendSV and SysTick at the lowest priority, SVCall at the highest. The program must exit QEMU with code 0 only if all checks pass, and must never hang (HardFault handler and a SysTick watchdog exit with 1).
>
> Example: with tasks H(3) M(2) L(1) where L holds a mutex for 4 ticks, H asks for it 1 tick later and M (6 ticks of CPU) is released 2 ticks after L, H must finish at t0+5 with inheritance and at t0+11 without it.
>
> Follow-ups: Why SVC instead of jumping to the task? Why must PendSV be the lowest priority? What does a 3 ms critical section do to the tick count? How would you port this to a Cortex-M4F?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `rtos_a01.ld` (링커 스크립트, BM-B08 과 같은 모양), `run_qemu.sh` (VM 빌드·실행), `demo_switch.gdb` (5. 풀이 Part C).

<!-- code: rtos_a01.ld -->

실행은 Lima VM `elinux` 안에서 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch 17.1). Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/RTOS-A01_work` 로 복사해 빌드한다. QEMU 는 **`-icount shift=3,align=off,sleep=off`** 로 돈다: 가상 시간 = 명령어 수(1 명령 = 8 ns)라서 tick 경계가 매번 같은 명령에서 떨어지고, 결과(로그·완료 tick)가 **결정적**이다. `sleep=off` 는 idle 의 `wfi` 동안 가상 시계를 다음 타이머까지 건너뛰게 한다.

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/RTOS/RTOS-A01
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 시작 코드: FAIL, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 38/38, qemu exit=0 (0.37 s)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # -s -S + gdb-multiarch -batch -x demo_switch.gdb
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh faults"     # F1~F4 결함 주입 (sed 로 한 줄씩 망가뜨린 사본)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh size"       # 섹션 크기 + PendSV/SVC 역어셈블
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh noicount"   # -icount 없이 (가상 시간 = 호스트 실시간)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C) — Mac 에서도 cc 로 된다
```

시작 코드 실행 결과 (실제 출력):

```text
   text	   data	    bss	    dec	    hex	filename
  12512	      0	   9152	  21664	   54a0	starter.elf
RTOS-A01 mini RTOS on mps2-an385
FAIL starter.c:739  sp == top - 16
FAIL starter.c:740  top[-1] == 0x01000000u
FAIL starter.c:741  top[-2] == ((uint32_t)(uintptr_t)idle_fn & ~1u) && (top[-2] & 1u) == 0u
FAIL starter.c:742  top[-3] == (uint32_t)(uintptr_t)task_exit_trap && (top[-3] & 1u) == 1u
FAIL starter.c:743  top[-8] == 0x1234u
FAIL starter.c:744  sp == top - 16 && sp[0] == 0x04040404u && sp[7] == 0x0B0B0B0Bu
[A] in crit: high=1 sys=1 BASEPRI=0x00000000 | after exit: sys=1
FAIL starter.c:758  hi_in == 1u && sys_in == 0u && bp_in == KERNEL_BASEPRI
FAIL: svc 0 이 돌아왔다 — SVC_Handler 가 첫 태스크로 넘어가지 않았다
3/11 passed
qemu exit=1
```

시작 코드는 **매달리지 않는다**: 빈 `SVC_Handler`(`bx lr`) 는 `svc 0` 다음 줄 `b start_failed` 로 돌아와 집계를 찍고 끝난다. TODO 를 반쯤 채워 프레임이 깨지면 HardFault 핸들러가, 스케줄러가 엉키면 SysTick 워치독(400 tick)이 `exit 1` 로 끝낸다. (워치독이 SysTick 안에 있다는 것의 한계는 7. 실수 4 참고.)

## 4. 힌트

<details>
<summary>힌트 1 — 프레임은 "하드웨어 8 + 소프트웨어 8"</summary>

예외 진입 때 코어가 PSP 에 쌓는 것: 높은 주소부터 xPSR, PC, LR, R12, R3, R2, R1, R0 (8 워드). PendSV 가 그 아래에 `stmdb r0!, {r4-r11}` 로 8 워드 더. 초기 프레임은 이 16 워드를 **똑같은 순서로** 흉내 낸다. `*--sp = ...` 를 16 번 (R4–R11 은 루프).

</details>

<details>
<summary>힌트 2 — xPSR 과 PC 의 bit0</summary>

Cortex-M 은 Thumb 만 있다. Thumb 상태는 **xPSR 의 T 비트(bit 24)** 로 복원되므로 xPSR = `0x01000000`. 스택된 PC 의 bit0 은 0 이어야 한다 (FreeRTOS `portSTART_ADDRESS_MASK`). 반대로 LR 칸은 `bx lr` 로 돌아갈 주소이므로 함수 포인터 그대로(bit0 = 1).

</details>

<details>
<summary>힌트 3 — SVC 는 들어올 때 MSP, 나갈 때 PSP</summary>

`main` 에서 `svc 0` 하면 SVC 의 LR = `0xFFFFFFF9` (Thread, MSP 로 복귀). `orr lr, lr, #0xd` → `0xFFFFFFFD` (Thread, **PSP** 로 복귀). 복귀하면서 하드웨어가 PSP 에서 R0–R3, R12, LR, PC, xPSR 을 꺼내 준다 — 그래서 PSP 를 `g_cur->sp + 32` (R4–R11 다음) 로 맞춰 둬야 한다. `ldmia r0!, {r4-r11}` 이 그 +32 를 해 준다.

</details>

<details>
<summary>힌트 4 — PendSV 안에서 C 함수를 부를 때</summary>

`bl` 은 LR 을 덮어쓴다. LR 에는 EXC_RETURN 이 들어 있으니 호출 전에 MSP 에 `stmdb sp!, {r3, lr}` 로 보관 (2 개 = 8 바이트, AAPCS 정렬 유지). `kernel_switch_context` 동안은 BASEPRI 를 올려 IRQ30 이 ready list 를 건드리지 못하게 한다.

</details>

<details>
<summary>힌트 5 — 크리티컬 섹션은 중첩된다, ISR 도 중첩된다</summary>

태스크용은 카운트가 0 으로 돌아올 때만 BASEPRI = 0. ISR 용은 카운트 대신 **이전 BASEPRI 를 돌려받아 그대로 복원** — 이미 BASEPRI 가 올라간 문맥에서 불릴 수 있기 때문 (FreeRTOS `ulPortRaiseBASEPRI` / `vPortSetBASEPRI`).

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 이 레슨 | FreeRTOS V11.3.2 (GCC/ARM_CM3) | CMSIS-RTOS2 | Zephyr (문서 기준, 미검증) |
|---|---|---|---|
| `task_stack_init` | `pxPortInitialiseStack` port.c:182–199 | (내부) | `arch_new_thread` |
| `start_first_task` + `SVC_Handler` | `prvPortStartFirstTask` port.c:247–262 + `vPortSVCHandler` :228–244 | `osKernelStart` | `arch_switch_to_main_thread` |
| `PendSV_Handler` + `kernel_switch_context` | `xPortPendSVHandler` port.c:455–488 + `vTaskSwitchContext` tasks.c:5215 | — | `z_arm_pendsv` |
| `SysTick_Handler` / `kernel_tick` | `xPortSysTickHandler` port.c:491–515 + `xTaskIncrementTick` tasks.c:4831 | `osKernelGetTickCount` | `sys_clock_announce` |
| `crit_enter/exit` | `vPortEnterCritical/ExitCritical` port.c:426–452 | — (`osKernelLock` 은 스케줄러 잠금) | `irq_lock` (v7-M 은 BASEPRI) |
| `isr_mask/unmask` | `ulPortRaiseBASEPRI` portmacro.h:228, `vPortSetBASEPRI` :248 | — | `irq_lock` |
| `sched_top_prio` (CLZ) | `portGET_HIGHEST_PRIORITY` portmacro.h:168 + `ucPortCountLeadingZeros` :148 | — | — |
| `yield_request` | `portYIELD()` portmacro.h:85 (ICSR PENDSVSET) | `osThreadYield` | `k_yield` |
| `sem_take/give/give_from_isr` | `xSemaphoreTake/Give/GiveFromISR` | `osSemaphoreAcquire/Release` | `k_sem_take/give` |
| `mutex_lock/unlock` (상속) | `xTaskPriorityInherit` tasks.c:6745 / `xTaskPriorityDisinherit` :6848 / `vTaskPriorityDisinheritAfterTimeout` :6942 | `osMutexNew` + `osMutexPrioInherit` | `k_mutex_lock` |
| `task_exit_trap` | `prvTaskExitError` port.c:202 | `osThreadExit` 를 불러야 | 스레드 return = 정상 종료 |

### Part A — 초기 스택 프레임: 16 워드를 "쫓겨난 척"

FreeRTOS 원본 (port.c:189–196) 과 이 레슨의 `task_stack_init` 은 칸 하나하나가 같다:

```c
*pxTopOfStack = portINITIAL_XPSR;                                    /* xPSR */     /* port.c:189, 0x01000000 */
*pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK; /* PC */       /* :191, & 0xfffffffe */
*pxTopOfStack = ( StackType_t ) portTASK_RETURN_ADDRESS;             /* LR */       /* :193, prvTaskExitError */
pxTopOfStack -= 5;                                                   /* R12, R3, R2 and R1. */
*pxTopOfStack = ( StackType_t ) pvParameters;                        /* R0 */
pxTopOfStack -= 8;                                                   /* R11, R10, R9, R8, R7, R6, R5 and R4. */
```

FreeRTOS 는 코드 크기를 아끼려 R1–R3, R12, R4–R11 칸을 비워 두지만, 이 레슨은 `0x04040404` (R4) … `0x0B0B0B0B` (R11), `0x12121212` (R12) 로 채운다 — gdb 덤프에서 "이 레지스터를 태스크가 아직 안 건드렸다" 가 바로 보인다. 아래는 gdb 로 실제 읽은 **L 의 스위치아웃 프레임**(Part C) 이다:

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">L 의 스택 (gdb x/16wx t_l.sp, Phase E 상속 OFF, t0+2)</text>
  <text x="10" y="44" fill="currentColor" font-family="monospace">0x20001838</text>
  <text x="120" y="44" fill="currentColor">stack_hi (배열 끝, 8 정렬) — 초기화 때 top</text>
  <rect x="120" y="54" width="300" height="160" class="f-acc" stroke="currentColor"/>
  <text x="130" y="72" fill="currentColor" font-family="monospace">+60 xPSR 0xa1000000 (T=1)</text>
  <text x="130" y="90" fill="currentColor" font-family="monospace">+56 PC   0x00000aec (burn 루프)</text>
  <text x="130" y="108" fill="currentColor" font-family="monospace">+52 LR   0x00000adb</text>
  <text x="130" y="126" fill="currentColor" font-family="monospace">+48 R12  0x20000004</text>
  <text x="130" y="144" fill="currentColor" font-family="monospace">+44 R3   0xfffffffd</text>
  <text x="130" y="162" fill="currentColor" font-family="monospace">+40 R2   0x00000004 (target)</text>
  <text x="130" y="180" fill="currentColor" font-family="monospace">+36 R1   0x200002cc</text>
  <text x="130" y="198" fill="currentColor" font-family="monospace">+32 R0   0x00000001</text>
  <rect x="120" y="214" width="300" height="130" class="f-warn" stroke="currentColor"/>
  <text x="130" y="232" fill="currentColor" font-family="monospace">+28..+4 R11..R5 0x0b0b0b0b..0x05050505</text>
  <text x="130" y="250" fill="currentColor">(초기값 그대로 — L 은 R5–R11 을 안 썼다)</text>
  <text x="130" y="268" fill="currentColor" font-family="monospace">+0  R4   0x20000000</text>
  <text x="130" y="286" fill="currentColor">(L 이 전역 베이스로 쓰는 중)</text>
  <text x="10" y="340" fill="currentColor" font-family="monospace">0x200017f0</text>
  <path d="M 90 336 L 116 336" stroke="currentColor"/>
  <text x="10" y="358" fill="currentColor">= t_l.sp (TCB 첫 멤버)</text>
  <text x="440" y="96" fill="currentColor">하드웨어가 예외 진입 때 push,</text>
  <text x="440" y="114" fill="currentColor">EXC_RETURN 으로 복귀할 때 pop</text>
  <text x="440" y="132" fill="currentColor">(8 워드, 첫 시작 땐 SVC 복귀가 pop)</text>
  <text x="440" y="250" fill="currentColor">PendSV 의 stmdb r0!, {r4-r11}</text>
  <text x="440" y="268" fill="currentColor">복원은 ldmia r0!, {r4-r11}</text>
  <text x="440" y="300" fill="currentColor">초기 프레임: 같은 16 칸에</text>
  <text x="440" y="318" fill="currentColor">xPSR 0x01000000 · PC fn&amp;~1</text>
  <text x="440" y="336" fill="currentColor">LR exit_trap · R0 arg</text>
</svg>
```

칸마다 "틀리면 무슨 일이" 는 결함 주입으로 확인했다 (아래 F1–F4). 요약: **xPSR = 0** → 첫 복귀에서 INVSTATE UsageFault → HardFault (CFSR `0x00020000`), **PC bit0 = 1** → ARMv7-M 에서 UNPREDICTABLE (QEMU 는 경고만 찍고 진행), **LR** → 태스크가 return 할 때만 의미가 있다 (Phase G 가 확인: M 태스크가 return → `task_exit_trap` 1 회), **R0** → 첫 인자 (Phase B: CTRL 이 `arg=0xc0de0001` 을 받았다). 정렬: `top` 이 8 바이트 정렬이고 프레임이 8 워드라 하드웨어 프레임 시작(`top-8`)도 8 정렬 — xPSR bit 9 (정렬 패딩 표시) 를 0 으로 둬도 된다.

### Part A — SVC 로 첫 태스크 시작: 왜 굳이 예외를 거치나

```text
kernel_start: task_create(IDLE) → BASEPRI = 0x40 (SysTick·PendSV 막음) → SHPR3 PendSV/SysTick = 0xFF, SHPR2 SVC = 0
            → g_cur = sched_pick_next() (= CTRL, prio 10) → SysTick 켬 → start_first_task()
start_first_task: MSP = *(VTOR) (= 0x20010000, main 의 스택 반납) → cpsie i → dsb/isb → svc 0
SVC_Handler:  r0 = g_cur->sp → ldmia r0!, {r4-r11} → PSP = r0 → BASEPRI = 0 → lr |= 0xD → bx lr
예외 복귀:    PSP 에서 R0..xPSR 언스택 → PC = ctrl_fn, R0 = 0xC0DE0001, Thread 모드 + PSP
```

FreeRTOS 도 정확히 이 모양이다: `prvPortStartFirstTask` (port.c:247–262) 가 `msr msp, r0` (:253) → `cpsie i` → `svc 0` (:258), `vPortSVCHandler` (:228–244) 가 `ldmia r0!, {r4-r11}` → `msr psp` → `msr basepri, 0` → `orr r14, #0xd` (:239) → `bx r14`. 그리고 `vTaskStartScheduler` 는 그 전에 `portDISABLE_INTERRUPTS()` (tasks.c:3787, 주석: "a tick does not occur before or during the call to xPortStartScheduler") — 우리 `kernel_start` 의 첫 `basepri_set(0x40)` 와 같은 이유다. 처음에 이걸 빼먹었더니(정확히는 `task_create` 의 `crit_exit` 가 BASEPRI 를 0 으로 되돌려서) PSP 가 아직 0 인데 SysTick → PendSV 가 들어올 수 있는 구멍이 생겼다 — 그래서 IDLE 생성을 BASEPRI 올리기 **앞으로** 옮겼다.

**왜 SVC 인가** (대안: Thread 모드에서 PSP 설정 → `CONTROL = 2` → 프레임을 손으로 pop → `bx`):

| | SVC 경로 (FreeRTOS CM3/CM4/CM0, 이 레슨) | 수동 경로 (FreeRTOS RP2040 포트) |
|---|---|---|
| 첫 태스크 진입 | 이후 모든 재개와 **같은** 예외 복귀 — 초기 프레임이 진짜 프레임과 같은지 첫 순간에 검증된다 | `pop {r0-r5}; mov lr, r5; pop {r3}; pop {r2}; bx r3` (ThirdParty/GCC/RP2040/port.c:215–233) — xPSR 은 "pop and discard" |
| Thread/PSP 전환 | EXC_RETURN 이 원자적으로 | `msr CONTROL, r0` (:226) + `isb` 를 직접 |
| 인터럽트 켜기 | SVC 직전 `cpsie i`, BASEPRI 는 SVC 안에서 0 | 마지막에 `cpsie i` |
| MSP 정리 | VTOR 로 MSP 리셋 (VTOR 없는 M0 계열은 생략, ARM_CM0/portasm.c:224 주석) | `configRESET_STACK_POINTER` 옵션 |

수동 경로도 동작한다 — 핵심 차이는 "첫 시작이 일반 경로의 특수 사례냐, 별도 코드냐" 다. SVC 경로는 SVC 우선순위를 최고(0)로 두기 때문에 BASEPRI 가 0x40 인 상태에서도 바로 들어온다 (`portNVIC_SHPR2_REG = 0;` port.c:392).

**관찰 1: MSP 가 `_estack` 이 아니라 `_estack − 32`.** Phase B 의 첫 시도는 `CHECK(msp == _estack)` 이었고 FAIL 이었다 (`MSP=0x2000ffe0`). 이유: `svc 0` 은 Thread+MSP 에서 들어왔으니 하드웨어가 **MSP** 에 8 워드를 쌓았는데, 우리는 EXC_RETURN `0xFFFFFFFD` 로 **PSP** 에서 언스택하며 나간다 → MSP 의 32 바이트는 영영 pop 되지 않는다. FreeRTOS 도 같은 경로라 같은 32 바이트가 남는다 (무해 — MSP 는 그 아래부터 ISR 용으로 쓴다). 검사를 `_estack - 32u` 로 고치고 주석에 이유를 적었다.

### Part A — PendSV 스위치

`run_qemu.sh size` 로 본 실제 기계어 (arm-none-eabi-gcc 14.2.1, `-O2`, 인라인 asm 이라 컴파일러가 손대지 않는다):

```text
00000218 <PendSV_Handler>:
     218:	f3ef 8009 	mrs	r0, PSP
     21c:	f3bf 8f6f 	isb	sy
     220:	4b0f      	ldr	r3, [pc, #60]	@ (260 <PendSV_Handler+0x48>)
     222:	681a      	ldr	r2, [r3, #0]
     224:	e920 0ff0 	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
     228:	6010      	str	r0, [r2, #0]
     22a:	b508      	push	{r3, lr}
     22c:	f04f 0040 	mov.w	r0, #64	@ 0x40
     230:	f380 8811 	msr	BASEPRI, r0
     234:	f3bf 8f4f 	dsb	sy
     238:	f3bf 8f6f 	isb	sy
     23c:	f000 fe4e 	bl	edc <kernel_switch_context>
     240:	f04f 0000 	mov.w	r0, #0
     244:	f380 8811 	msr	BASEPRI, r0
     248:	e8bd 4008 	ldmia.w	sp!, {r3, lr}
     24c:	6819      	ldr	r1, [r3, #0]
     24e:	6808      	ldr	r0, [r1, #0]
     250:	e8b0 0ff0 	ldmia.w	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
     254:	f380 8809 	msr	PSP, r0
     258:	f3bf 8f6f 	isb	sy
     25c:	4770      	bx	lr
     25e:	0000      	.short	0x0000
     260:	20000118 	.word	0x20000118
```

`stmdb sp!, {r3, lr}` 를 어셈블러가 `push {r3, lr}` 로 찍었다 (Handler 모드의 sp = MSP). `0x20000118` 은 `g_cur` 의 주소 (`nm`: `20000118 B g_cur`). FreeRTOS `xPortPendSVHandler` (port.c:455–488) 와 비교하면 차이는 호출하는 C 함수 이름뿐이다 (`stmdb r0!, {r4-r11}` :467, `stmdb sp!, {r3, r14}` :470, `bl vTaskSwitchContext` :473). `kernel_switch_context` 는 스위치마다 **나가는 태스크의 sp 가 자기 스택 안인지 + 바닥 16 바이트가 칠한 값(0xA5A5A5A5) 그대로인지** 검사한다 (RTOS-M09 의 두 방법) → 전체 실행에서 `stack_errors=0`, `switches=63`.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">t0+2 의 스위치: L(1) → M(2) — SysTick 이 깨우고, PendSV 가 바꾼다</text>
  <rect x="10" y="40" width="110" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="64" fill="currentColor">L: burn() 루프</text>
  <rect x="150" y="40" width="150" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="158" y="58" fill="currentColor">SysTick (0xFF, MSP)</text>
  <text x="158" y="74" fill="currentColor">tick++, M 깨움 → PENDSVSET</text>
  <rect x="330" y="40" width="180" height="40" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="338" y="58" fill="currentColor">PendSV (tail-chain)</text>
  <text x="338" y="74" fill="currentColor">L 의 r4-r11 → L 스택, t_l.sp</text>
  <rect x="540" y="40" width="190" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="548" y="58" fill="currentColor">M: delay_to 다음 줄부터</text>
  <text x="548" y="74" fill="currentColor">PSP = M 스택</text>
  <path d="M120 60 L146 60" stroke="currentColor"/><path d="M146 60 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M300 60 L326 60" stroke="currentColor"/><path d="M326 60 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M510 60 L536 60" stroke="currentColor"/><path d="M536 60 l-7 -4 l0 8 z" fill="currentColor"/>
  <text x="10" y="110" fill="currentColor">① 하드웨어: SysTick 진입 때 L 의 r0-r3, r12, lr, pc, xPSR 를 PSP 에 push.</text>
  <text x="10" y="128" fill="currentColor">   SysTick 이 끝나면 PendSV 가 바로 이어진다 (tail-chaining). 그 사이 L 은 한 명령도 안 돈다.</text>
  <text x="10" y="152" fill="currentColor">② PendSV: mrs r0, psp → stmdb r0!, {r4-r11} → t_l.sp = r0</text>
  <text x="10" y="170" fill="currentColor">   → BASEPRI 0x40 → kernel_switch_context() → g_cur = M</text>
  <text x="10" y="194" fill="currentColor">③ r0 = t_m.sp → ldmia r0!, {r4-r11} → msr psp, r0 → bx lr (0xFFFFFFFD)</text>
  <text x="10" y="212" fill="currentColor">   → 하드웨어가 M 의 r0..xPSR 를 pop, M 은 delay_to() 다음 줄부터</text>
  <text x="10" y="240" fill="currentColor">왜 SysTick 안에서 바로 안 바꾸나: SysTick 이 다른 ISR 을 선점 중일 수 있다.</text>
  <text x="10" y="258" fill="currentColor">최저 PendSV 는 모든 ISR 이 끝난 뒤에만 돈다 → 여러 번 펜딩해도 1 번, Thread 문맥만 바꾼다.</text>
  <text x="10" y="282" fill="currentColor">PendSV 를 0x00 으로 올리면? → 결함 F3: 크리티컬 섹션 안에서 스위치, BASEPRI 0x40 고착, 정지.</text>
</svg>
```

### Part B — 비트맵 스케줄러 + delay + SysTick 선점 (Phase C)

`sched_top_prio()` = `31 - clz(g_ready_bits)` — Cortex-M3 에는 `CLZ` 명령이 있다 (FreeRTOS 도 `ucPortCountLeadingZeros` 의 `clz %0, %1`, portmacro.h:148–155; M0 는 없음 → RTOS-B04). 실행 중인 태스크도 ready list 에 남아 있는 FreeRTOS 방식이다.

세 태스크 H(3) M(2) L(1) 이 모두 `delay_to(t0)` 로 같은 tick 에 풀려난다. H: 로그 `H` → `task_delay(2)` → `h`. M: `M` → `task_delay(4)` → `m`. L: `L` → `burn(8)` → `l`. 실제 출력:

```text
[C] log=HMLhml h@+2 m@+4 l@+8
```

`burn(n)` 은 "SysTick 순간에 실행 중이던 태스크에게 1 tick 을 청구" 하는 `run_ticks` 로 잰다 (FreeRTOS 런타임 통계와 같은 근사). L 은 t0+2, t0+4 에 H, M 에게 잠깐 선점당하지만 그 구간 끝의 SysTick 때 다시 L 이 돌고 있었으므로 8 tick 을 다 청구받아 t0+8 에 끝난다.

### Part B — ISR → 세마포어 → ISR 끝에서 스위치 (Phase D)

W(12) 가 `g_isr_sem` 에서 막혀 있고, CTRL(10) 이 로그 `a` → `NVIC->STIR = 30` → `b`. IRQ30 은 `sem_give_from_isr` 가 돌려준 `woken = true` 로 PENDSVSET (= `portYIELD_FROM_ISR`, portmacro.h:111 → `portEND_SWITCHING_ISR` :98). IRQ30 이 끝나자마자 PendSV 가 tail-chain 되어 W 가 돈다 (RTOS-M02 의 "스위치는 ISR 출구에서"):

```text
[D] isr→task log=aXb
[D] counting: got=3 timeout_ok=0 dt=3 woke@+0
```

카운팅 세마포어(초기 3): C 가 3 개를 바로 받고, 4 번째 `sem_take(…, 3)` 은 정확히 3 tick 뒤 `false`, 그다음 무한 대기 중 CTRL 의 `sem_give` 로 같은 tick 에 깨어난다. 이 커널은 **깨울 때 토큰을 대기자에게 직접 넘긴다** (count 증가 없음). FreeRTOS 는 count 를 올리고 깨어난 태스크가 `xQueueSemaphoreTake` 의 `for( ; ; )` (queue.c:1686) 루프에서 다시 시도한다 — 그래서 더 높은 태스크가 먼저 가로챌 수 있다 (RTOS-M06 Part B).

### Part B — 우선순위 역전: 상속 OFF vs ON (Phase E)

RTOS-M03/M04 의 숫자 그대로: L 이 t0 에 뮤텍스를 잡고 4 tick 일하고 풀고 1 tick 더, H 는 t0+1 에 같은 뮤텍스 요청 후 1 tick, M 은 t0+2 에 풀려나 6 tick. **실제 QEMU 출력** (트레이스는 `kernel_tick` 이 tick 마다 기록):

```text
[E] 우선순위 역전 — 상속 OFF (binary semaphore 처럼)
  tick   : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
  H(3)   : . B B B B B B B B B R . . . . .
  M(2)   : . . R R R R R R . . . . . . . .
  L(1)   : R R r r r r r r R R r R . . . .
  L prio : 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
[E] 우선순위 역전 — 상속 ON (mutex)
  tick   : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
  H(3)   : . B B B R . . . . . . . . . . .
  M(2)   : . . r r r R R R R R R . . . . .
  L(1)   : R R R R r r r r r r r R . . . .
  L prio : 1 3 3 3 1 1 1 1 1 1 1 1 1 1 1 1
[E] done(H,M,L) off=(11,8,12) on=(5,11,12) inversion ticks off=6 on=0 Lmax off=1 on=3
```

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Phase E 실측 타임라인 (QEMU 출력 그대로, 칸 k = 구간 [t0+k, t0+k+1))</text>
  <text x="10" y="42" fill="currentColor" font-weight="bold">상속 OFF</text><text x="161" y="42" fill="currentColor">0</text><text x="191" y="42" fill="currentColor">1</text><text x="221" y="42" fill="currentColor">2</text>
  <text x="251" y="42" fill="currentColor">3</text><text x="281" y="42" fill="currentColor">4</text><text x="311" y="42" fill="currentColor">5</text><text x="341" y="42" fill="currentColor">6</text>
  <text x="371" y="42" fill="currentColor">7</text><text x="401" y="42" fill="currentColor">8</text><text x="431" y="42" fill="currentColor">9</text><text x="461" y="42" fill="currentColor">10</text>
  <text x="491" y="42" fill="currentColor">11</text><text x="521" y="42" fill="currentColor">12</text><text x="551" y="42" fill="currentColor">13</text><text x="581" y="42" fill="currentColor">14</text>
  <text x="611" y="42" fill="currentColor">15</text><text x="10" y="63" fill="currentColor">H (prio 3)</text><rect x="180" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="210" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/><rect x="240" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="270" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/><rect x="300" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="330" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/><rect x="360" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="390" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/><rect x="420" y="50" width="30" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="450" y="50" width="30" height="18" class="f-acc" stroke="currentColor"/><text x="638" y="63" fill="currentColor">H 끝 +11</text><text x="10" y="87" fill="currentColor">M (prio 2)</text>
  <rect x="210" y="74" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="240" y="74" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="270" y="74" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="300" y="74" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="330" y="74" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="360" y="74" width="30" height="18" class="f-acc" stroke="currentColor"/><text x="638" y="87" fill="currentColor">M 끝 +8</text>
  <text x="10" y="111" fill="currentColor">L (prio 1)</text><rect x="150" y="98" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="180" y="98" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="210" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="240" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="270" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="300" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="330" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="360" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="390" y="98" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="420" y="98" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="450" y="98" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="480" y="98" width="30" height="18" class="f-acc" stroke="currentColor"/><text x="638" y="111" fill="currentColor">L 끝 +12</text>
  <path d="M210 136 l0 6 l180 0 l0 -6" fill="none" stroke="currentColor"/><text x="230" y="156" fill="currentColor">H 가 막힌 채 M 이 6 tick 실행 = 역전</text><text x="10" y="180" fill="currentColor" font-weight="bold">상속 ON</text>
  <text x="161" y="180" fill="currentColor">0</text><text x="191" y="180" fill="currentColor">1</text><text x="221" y="180" fill="currentColor">2</text><text x="251" y="180" fill="currentColor">3</text>
  <text x="281" y="180" fill="currentColor">4</text><text x="311" y="180" fill="currentColor">5</text><text x="341" y="180" fill="currentColor">6</text><text x="371" y="180" fill="currentColor">7</text>
  <text x="401" y="180" fill="currentColor">8</text><text x="431" y="180" fill="currentColor">9</text><text x="461" y="180" fill="currentColor">10</text><text x="491" y="180" fill="currentColor">11</text>
  <text x="521" y="180" fill="currentColor">12</text><text x="551" y="180" fill="currentColor">13</text><text x="581" y="180" fill="currentColor">14</text><text x="611" y="180" fill="currentColor">15</text>
  <text x="10" y="201" fill="currentColor">H (prio 3)</text><rect x="180" y="188" width="30" height="18" class="f-warn" stroke="currentColor"/><rect x="210" y="188" width="30" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="240" y="188" width="30" height="18" class="f-warn" stroke="currentColor"/><rect x="270" y="188" width="30" height="18" class="f-acc" stroke="currentColor"/><text x="638" y="201" fill="currentColor">H 끝 +5</text>
  <text x="10" y="225" fill="currentColor">M (prio 2)</text><rect x="210" y="212" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="240" y="212" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="270" y="212" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="300" y="212" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="330" y="212" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="360" y="212" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="390" y="212" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="420" y="212" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="450" y="212" width="30" height="18" class="f-acc" stroke="currentColor"/><text x="638" y="225" fill="currentColor">M 끝 +11</text><text x="10" y="249" fill="currentColor">L (prio 1)</text>
  <rect x="150" y="236" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="180" y="236" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="210" y="236" width="30" height="18" class="f-acc" stroke="currentColor"/><rect x="240" y="236" width="30" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="270" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="300" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="330" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="360" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="390" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="420" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="450" y="236" width="30" height="18" class="f-mut" stroke="currentColor"/><rect x="480" y="236" width="30" height="18" class="f-acc" stroke="currentColor"/><text x="638" y="249" fill="currentColor">L 끝 +12</text>
  <text x="10" y="273" fill="currentColor">L 의 prio</text><text x="161" y="273" fill="currentColor" font-weight="normal">1</text><text x="191" y="273" fill="currentColor" font-weight="bold">3</text>
  <text x="221" y="273" fill="currentColor" font-weight="bold">3</text><text x="251" y="273" fill="currentColor" font-weight="bold">3</text><text x="281" y="273" fill="currentColor" font-weight="normal">1</text>
  <text x="311" y="273" fill="currentColor" font-weight="normal">1</text><text x="341" y="273" fill="currentColor" font-weight="normal">1</text><text x="371" y="273" fill="currentColor" font-weight="normal">1</text>
  <text x="401" y="273" fill="currentColor" font-weight="normal">1</text><text x="431" y="273" fill="currentColor" font-weight="normal">1</text><text x="461" y="273" fill="currentColor" font-weight="normal">1</text>
  <text x="491" y="273" fill="currentColor" font-weight="normal">1</text><text x="521" y="273" fill="currentColor" font-weight="normal">1</text><text x="551" y="273" fill="currentColor" font-weight="normal">1</text>
  <text x="581" y="273" fill="currentColor" font-weight="normal">1</text><text x="611" y="273" fill="currentColor" font-weight="normal">1</text>
  <rect x="10" y="300" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="30" y="310" fill="currentColor">R 실행</text>
  <rect x="100" y="300" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="120" y="310" fill="currentColor">r READY (선점당함)</text>
  <rect x="250" y="300" width="14" height="12" class="f-warn" stroke="currentColor"/><text x="270" y="310" fill="currentColor">B 뮤텍스 대기</text><text x="380" y="310" fill="currentColor">빈칸 = 출발 전 · 끝남</text>
</svg>
```

| | H 끝 | M 끝 | L 끝 | H 가 막힌 tick | 그중 M 이 돈 tick | L 최고 prio |
|---|---|---|---|---|---|---|
| 상속 OFF | +11 | +8 | +12 | 9 (= 3 + C_M) | 6 | 1 |
| 상속 ON | +5 | +11 | +12 | 3 (= L 의 남은 임계 구역) | 0 | 3 |

M04 의 공식(OFF: `3 + C_M`, ON: `3`)이 진짜 Cortex-M 스위치 위에서 tick 단위로 그대로 나온다. 상속은 공짜가 아니다: M 은 +8 → +11 로 늦어졌다 (L 이 prio 3 으로 M 을 막은 3 tick). L 의 전체 완료(+12)는 같다 — CPU 총량은 그대로이고 **순서만** 바뀐다.

**E' — 상속 중 대기자가 타임아웃되면?** H 가 `mutex_lock(&R, 2)` 로 기다리다 포기하면, L 에게 빌려준 prio 3 을 **회수해야** 한다. 안 그러면 L 이 쓸데없이 높은 우선순위로 계속 돈다 (FreeRTOS `vTaskPriorityDisinheritAfterTimeout`, tasks.c:6942). `kernel_tick` 의 타임아웃 처리에서 `mutex_recompute_owner()` = max(base, 남은 대기자 최고 prio):

```text
[E'] timeout: ok=0 at+3 L prio slots1..4=3311 L done+6
```

칸 1, 2 는 3 (H 대기 중), 칸 3 부터 1 (t0+3 에 H 타임아웃 → 복원). 이 구현은 소유자가 뮤텍스를 **하나만** 쥔 경우만 정확하다 (`held != 1` 이면 손대지 않음) — 여러 개면 RTOS-M04 Part B 의 정책이 필요하다.

### Part C — BASEPRI 크리티컬 섹션 (Phase A2, F)

```text
[A] in crit: high=1 sys=0 BASEPRI=0x00000040 | after exit: sys=1
[F] crit: hi ran inside (BASEPRI seen=0x00000040), sys ISR runs after exit=1 (3 pends merged), tick advance after 3 ms masked=1 log=X
```

| 관찰 (Phase F, 태스크 문맥, 스케줄러 동작 중) | 값 | 이유 |
|---|---|---|
| IRQ31 (0x20) 을 ISPR 로 펜딩 | 즉시 실행, 안에서 BASEPRI = 0x40 을 봄 | 0x20 < 0x40 → 마스크 대상 아님 = "zero-latency" IRQ |
| IRQ30 (0x80) 을 STIR 로 **3 번** 펜딩 | 안에서 0 번, 나간 뒤 **1 번** | 펜딩 비트는 1 개 — 3 번 써도 "펜딩됨" 하나 |
| SysTick 카운터 3 바퀴(3 ms) 동안 막음 | `g_tick` +1 만 (2 tick 손실), 안에서 PENDSTSET = 1 | SysTick 도 펜딩 비트 하나 |
| 중첩 `crit_enter`/`crit_exit` 한 쌍 뒤 | 아직 막혀 있음 | nesting 2 → 1, BASEPRI 유지 |
| 바깥 `crit_exit` | IRQ30 → SysTick → PendSV(W) 순서로 처리, 로그 `X` | 가장 급한 펜딩부터 |

`crit_enter` 의 asm 은 FreeRTOS `vPortRaiseBASEPRI` (portmacro.h:212) 와 같은 `msr basepri; isb; dsb`. **ISR 용**은 `ulPortRaiseBASEPRI` (:228) 처럼 이전 값을 돌려준다 — `SysTick_Handler` 가 이걸 써서 `kernel_tick` 동안 IRQ30 (같은 커널 자료구조를 만지는 ISR) 을 막는다. IRQ31 은 SysTick 안에서도 들어올 수 있으므로 **절대 커널 API 를 부르면 안 된다**. FreeRTOS 는 이것을 `vPortValidateInterruptPriority` 의 `configASSERT( ucCurrentPriority >= ucMaxSysCallPriority )` (port.c:803) 로 잡는다.

(1 tick = 1 ms 는 QEMU mps2 SYSCLK 25 MHz 가정의 **가상** 시간이다 — QEMU hw/arm/mps2.c 기준, 미검증.) SysTick 손실은 "tick 시간 = 벽시계" 라는 가정을 깨뜨린다: `task_delay(10)` 이 10 ms 보다 길어진다. 그래서 크리티컬 섹션은 **수 µs** 로 짧게 — 긴 작업은 스케줄러 잠금(뮤텍스, `vTaskSuspendAll`) 으로 바꾼다.

### Part C — gdb 로 스위치아웃된 태스크 보기

<!-- code: demo_switch.gdb -->

`hbreak gdb_probe` — Phase E (상속 OFF) 에서 M 이 t0+2 에 깨어나 L 을 선점한 직후. 실제 출력 (발췌):

```text
Breakpoint 1, gdb_probe () at solution.c:561
=== (1) 지금 CPU: 실행 중 태스크 M, Thread 모드, PSP 사용 ===
g_tick=24  g_cur=M  (t0=22)
SysTick CVR=1 (리로드 24999 에서 0 으로 내려간다. tick 직후라면 24990 근처여야)
sp             0x20001428          0x20001428 <stk_t+2032>
msp            0x2000ffe0          0x2000ffe0
psp            0x20001428          0x20001428
control        0x2                 0x2
basepri        0x0                 0x0
primask        0x0                 0x0
xpsr           0x61000000          0x61000000
M stack = [0x20001038, 0x20001438)
=== (2) 스위치아웃된 L: t_l.sp 에서 16 워드 = r4..r11 | r0 r1 r2 r3 r12 lr pc xPSR ===
t_l.sp=0x200017f0  state=0  prio=1  stack_hi=0x20001838  (frame top = sp + 64 → 18 words used)
0x200017f0 <stk_t+3000>:	0x20000000	0x05050505	0x06060606	0x07070707
0x20001800 <stk_t+3016>:	0x08080808	0x09090909	0x0a0a0a0a	0x0b0b0b0b
0x20001810 <stk_t+3032>:	0x00000001	0x200002cc	0x00000004	0xfffffffd
0x20001820 <stk_t+3048>:	0x20000004	0x00000adb	0x00000aec	0xa1000000
stacked pc  = 0x00000aec  phe_l + 48 in section .text
stacked lr  = 0x00000adb  phe_l + 31 in section .text
stacked xPSR= 0xa1000000  (T=1, IPSR=0)
=== (3) 막혀 있는 H: mutex_lock → crit_exit 에서 PendSV 로 나갔다 ===
t_h.sp=0x20000fc8  state=2  wait_obj=0x20000194 (&g_res=0x20000194)
0x20000ff8 <stk_t+960>:	0x2000000c	0x00000003	0x000008cc	0x61000000
stacked pc  = 0x000008cc  mutex_lock.constprop + 84 in section .text
stacked lr  = 0x00000003  g_vectors + 3 in section .isr_vector
```

읽는 법:
- **지금 CPU**: `sp == psp` (Thread 모드는 PSP), `control = 0x2`, MSP 는 `0x2000ffe0` (Part A 관찰 1). PSP 가 M 의 스택 `[0x20001038, 0x20001438)` 안.
- **L (READY, 선점됨)**: 스택된 PC `0xaec` = `phe_l` 에 인라인된 `burn()` 루프의 `ldr r3, [r1, #32]` (`run_ticks` 다시 읽기, objdump 로 확인). LR `0xadb` = `bl mutex_lock` 의 복귀 주소(0xada|1). R2 = 4 = burn 의 target, R4 = 0x20000000 (전역 베이스), **R5–R11 은 초기 패턴 그대로** — L 이 그 레지스터를 안 썼고, 여러 번의 스위치 동안 PendSV 가 정확히 보존했다는 증거다.
- **H (BLOCKED)**: 스택된 PC `0x8cc` = `mutex_lock` 안에 인라인된 `crit_exit` 의 `msr BASEPRI, r3` **바로 다음** 명령 — BASEPRI 가 0 이 되는 순간 펜딩돼 있던 PendSV 가 들어왔다. 스택된 LR = `3` 은 "그 순간 LR 레지스터에 있던 값" 일 뿐이다: 직전에 불린 `block_current` 가 LR 을 임시 레지스터로 쓰고 `pop {…, pc}` 로 돌아와서 남은 값이다. **스택된 LR ≠ 항상 리턴 주소.**

**프로브 효과 (실측)**: gdb 아래에서 돌린 같은 펌웨어는 Phase E 가 `off=(12,9,13)`, `inversion ticks off=7` 로 바뀌어 `36/38` 로 끝났다. 멈춘 지점의 `CVR=1` 이 단서다 — tick 직후 몇 명령 안 지났는데 카운터가 거의 0. `-icount … sleep=off` 에서 CPU 가 gdb 로 멈춰 있는 동안 QEMU 가 가상 시계를 다음 타이머 직전까지 당긴 것으로 보인다 (원인은 추정; 관찰은 사실). 실보드에서도 디버거 정지 중 SysTick·주변장치 타이머가 멈추는지는 칩/설정(STM32 `DBGMCU` freeze 비트 등)마다 다르다 (문서 기준). **교훈: 타이밍 assert 는 디버거 없이 돌린다.**

### 전체 실행 결과 (solution)

```text
   text	   data	    bss	    dec	    hex	filename
  12960	      0	   9272	  22232	   56d8	solution.elf
RTOS-A01 mini RTOS on mps2-an385
[A] in crit: high=1 sys=0 BASEPRI=0x00000040 | after exit: sys=1
[B] first task: CONTROL=0x00000002 PSP=0x200006a8 MSP=0x2000ffe0 BASEPRI=0x00000000 arg=0xc0de0001
[C] log=HMLhml h@+2 m@+4 l@+8
[D] isr→task log=aXb
[D] counting: got=3 timeout_ok=0 dt=3 woke@+0
   … (Phase E 타임라인은 위) …
[E] done(H,M,L) off=(11,8,12) on=(5,11,12) inversion ticks off=6 on=0 Lmax off=1 on=3
[E'] timeout: ok=0 at+3 L prio slots1..4=3311 L done+6
[F] crit: hi ran inside (BASEPRI seen=0x00000040), sys ISR runs after exit=1 (3 pends merged), tick advance after 3 ms masked=1 log=X
[G] exit trap hits=1 task=M arg=0x00005eed
[G] switches=63 stack_errors=0 api_errors=0 idle_ticks=30 tick=69
[G] stack used (bytes): CTRL=288 IDLE=64 L=96 M=80 H=112 W=72 C=80
38/38 passed
qemu exit=0
```

- 5 번 연속 실행: 출력이 바이트 단위로 같았다 (`diff -q`). 한 번에 0.37 s (VM 안 `time`).
- `noicount` (가상 시간 = 호스트 실시간) 3 번도 전부 38/38 — 하지만 보장은 아니다: 호스트가 QEMU 스레드를 재우면 tick 이 몰려 올 수 있다. 검사가 tick 단위 순서를 assert 하므로 `-icount` 가 기본.
- 스택 하이 워터 마크: IDLE = 64 B = **초기 프레임 16 워드 그 자체** (`wfi` 루프는 아무것도 push 하지 않는다). CTRL 288 B 가 최대 (세미호스팅 숫자 출력 버퍼 + 트레이스 줄 버퍼). ISR 은 MSP 를 쓰므로 이 숫자에 ISR 중첩이 안 들어간다 — 단, 예외 진입 프레임 32 B 는 들어간다 (RTOS-M08).

### 결함 주입 (`run_qemu.sh faults`, 실제 출력 발췌)

```text
##### F1: 초기 xPSR = 0 (T 비트 없음) #####
HardFault! CFSR=0x00020000 HFSR=0x40000000 EXC_RETURN=0xfffffffd
  stacked pc=0x00001320 lr=0x00000551 xpsr=0x00000000
##### F2: 초기 PC 칸에 bit0 = 1 을 그대로 (& ~1u 빠뜨림) #####
M profile return from interrupt with misaligned PC is UNPREDICTABLE on v7M     (← -d guest_errors, 16 번)
37/38 passed                                                                  (← 프레임 검사만 FAIL)
##### F3: PendSV 우선순위 = 0x00 (최고, BASEPRI 로도 안 막힘) #####
[B] first task: CONTROL=0x00000002 PSP=0x200006b0 MSP=0x2000ffe0 BASEPRI=0x00000000 arg=0xc0de0001
qemu-system-arm: terminating on signal 15 from pid 263265 (timeout)
qemu exit=124
##### F4: TCB 첫 멤버가 sp 가 아님 (name 과 순서 바뀜) #####
HardFault! CFSR=0x00020000 HFSR=0x40000000 EXC_RETURN=0xfffffffd
  stacked pc=0x00000000 lr=0x00000000 xpsr=0x00000200
```

- **F1**: CFSR `0x00020000` = UFSR **INVSTATE** (bit 17), HFSR `0x40000000` = FORCED (UsageFault 를 안 켜서 HardFault 로 승격). 스택된 PC = `ctrl_fn`(0x1320, `addr2line` 확인), LR = `task_exit_trap|1` — **첫 태스크의 첫 명령**에서 죽었다.
- **F2**: QEMU 는 v7-M 에서 경고만 찍고 진행한다 → 테스트는 다 통과. 실칩에서는 UNPREDICTABLE 이므로 "QEMU 에서 됐다" 가 증거가 못 된다 (ARMv8-M 은 INVPC UsageFault 로 정의 — 문서 기준).
- **F3**: gdb 로 붙어 본 상태: `g_tick=2 nesting=4 g_cur=L`, `basepri 0x40`, `ICSR=0x0400f800` (PENDSTSET + VECTPENDING=15 = SysTick). PendSV 가 크리티컬 섹션 **안에서** 스위치해 버려 nesting 이 태스크 사이로 새고, BASEPRI 가 0x40 에 고착 → SysTick 이 영원히 펜딩 → **SysTick 안의 워치독도 죽었다**. `timeout 15` 만이 끝냈다.
- **F4**: asm 의 `ldr r0, [r1]` 이 `name` 포인터를 sp 로 읽어 쓰레기 프레임 → INVSTATE.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

**실수 1 — 스택된 PC 에 Thumb 비트를 넣는다.** 함수 포인터는 bit0 = 1 이니 그대로 넣기 쉽다. 예외 복귀가 읽는 PC 는 bit0 = 0 이어야 하고 Thumb 은 xPSR.T 로 정해진다. QEMU v7-M 은 경고만 하고 넘어가 **테스트가 통과해 버린다** (F2). 고친 코드: `*--sp = (uint32_t)(uintptr_t)fn & ~1u;` + `-d guest_errors` 로 QEMU 경고를 켜 두기.

**실수 2 — xPSR 칸을 0 으로 (memset 한 스택 그대로).** → INVSTATE HardFault, 그것도 첫 태스크의 첫 명령에서 (F1). HardFault 핸들러가 `CFSR` 과 스택된 `xpsr` 을 찍게 해 두면 1 초 만에 원인이 보인다. 고친 코드: `*--sp = 0x01000000u;`.

**실수 3 — TCB 레이아웃과 asm 의 약속을 깨뜨린다.** asm 은 `[tcb + 0]` 을 sp 로 읽는다. 누가 디버깅용 필드를 맨 앞에 추가하면 바로 F4. 고친 코드: `uint32_t *sp;` 를 첫 멤버로 두고, 가능하면 `_Static_assert(offsetof(tcb_t, sp) == 0, "asm reads tcb+0")` (FreeRTOS 는 tasks.c:379 `pxTopOfStack` 주석 "THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT" 로 약속한다).

**실수 4 — PendSV/SysTick 우선순위를 안 정한다 (리셋값 0).** F3: 크리티컬 섹션 안에서 스위치 → nesting 누수 → BASEPRI 고착 → 시스템 정지. 게다가 **SysTick 안에 둔 소프트웨어 워치독도 같이 죽는다** — 진짜 제품은 하드웨어 워치독(IWDG)을 태스크 생존 비트마스크로 먹인다 (RTOS-A10). 고친 코드: `SCB_SHPR3 |= (0xFFu << 16) | (0xFFu << 24);` (FreeRTOS port.c:390–391).

**실수 5 — 문턱 위 IRQ 에서 `*_from_isr` 를 부른다.** IRQ31 은 크리티컬 섹션 안에서도 실행되므로, 거기서 ready list 를 만지면 `crit_enter` 가 지키던 자료구조가 반쯤 바뀐 상태로 노출된다. 리셋 기본 우선순위 0 인 IRQ 가 그대로 이 경우다. 고친 코드: 커널을 부르는 IRQ 는 `NVIC_IPR(n) = 0x80` 처럼 문턱 **이상**으로, FreeRTOS 에선 `configASSERT` 를 켜서 port.c:803 검사를 살린다.

**실수 6 — 4 비트 칩에서 우선순위를 하위 비트에 쓴다.** `NVIC_IPR(n) = 5;` 는 STM32(4 비트, 상위 니블만 구현 — 데이터시트 기준)에서 0 으로 읽혀 **가장 급한** IRQ 가 된다. QEMU mps2-an385 는 8 비트를 다 구현해서(0xFF 쓰고 0xFF 읽힘, 실측) 이 버그가 안 보인다. 고친 코드: `NVIC_IPR(n) = (uint8_t)(5u << (8 - __NVIC_PRIO_BITS));` (추가 연습 3).

## 8. 면접 Q&A

<details>
<summary>Q1. 첫 태스크를 왜 SVC 로 시작하나요? 그냥 태스크 함수로 점프하면 안 되나요?</summary>

그냥 `bl task_fn` 하면 MSP 위에서 Thread 모드로 돌게 되어 "태스크 = PSP" 모델이 깨진다. 제대로 하려면 PSP 설정 + `CONTROL = 2` + 프레임 수동 pop 이 필요하다 (RP2040 포트가 실제로 이렇게 한다, port.c:215–233). SVC 경로는 첫 시작을 **평범한 예외 복귀**로 만든다: 하드웨어 언스택이 초기 프레임을 검증하고, Thread+PSP 전환이 EXC_RETURN 으로 원자적이며, SVC 안에서 BASEPRI 를 0 으로 푸는 시점이 명확하다. 덤으로 MSP 를 벡터 [0] 으로 되돌려 main 의 스택을 ISR 용으로 회수한다 (VTOR 가 없는 M0 계열은 이 단계를 뺀다 — ARM_CM0/portasm.c:224 주석). 꼬리질문 "그럼 MSP 는 정확히 꼭대기인가?" → 아니다, SVC 진입 프레임 32 B 가 남는다 (실측 `0x2000ffe0`).

</details>

<details>
<summary>Q2. PendSV 는 왜 최저 우선순위여야 하나요? SysTick 안에서 바로 스위치하면?</summary>

SysTick 이 다른 ISR 을 선점한 채로 스위치하면, 복귀 경로에 그 ISR 의 프레임이 MSP 에 남은 채 Thread 문맥만 바뀌어 복잡해진다(그리고 SysTick 우선순위를 높게 둔 설계면 실제로 일어난다). 최저 PendSV 는 "모든 ISR 이 끝난 뒤, Thread 로 돌아가기 직전" 에만 돌기 때문에 스위치가 항상 단순하고, 여러 ISR 이 각각 펜딩해도 한 번만 돈다. 또 BASEPRI 로 막혀야 커널 크리티컬 섹션 안에서 스위치가 일어나지 않는다 — 0x00 으로 올리면 크리티컬 섹션 중첩 카운트가 태스크 사이로 새어 BASEPRI 가 고착되는 것을 F3 에서 봤다.

</details>

<details>
<summary>Q3. 초기 스택 프레임의 각 칸을 설명해 보세요. xPSR 은 왜 0x01000000 이죠?</summary>

위에서부터 xPSR, PC, LR, R12, R3–R1, R0 (하드웨어 프레임 8 워드) + R11–R4 (소프트웨어 8 워드). xPSR 의 bit 24 = T (Thumb) — Cortex-M 은 Thumb 만 실행하므로 0 이면 첫 명령에서 INVSTATE (CFSR 0x00020000 실측). IPSR 칸 = 0 (Thread), bit 9 = 0 (정렬 패딩 없음). PC 는 bit0 을 지운 태스크 진입점, LR 은 태스크가 return 했을 때 갈 트랩(`prvTaskExitError`), R0 는 인자. R4–R11 값은 아무거나 되지만 패턴을 넣어 두면 디버깅이 쉽다.

</details>

<details>
<summary>Q4. FreeRTOS Cortex-M3 포트는 왜 PRIMASK 가 아니라 BASEPRI 로 크리티컬 섹션을 만드나요? 잃는 건?</summary>

PRIMASK 는 NMI/HardFault 빼고 전부 막는다. BASEPRI 는 문턱 이상(덜 급한) 것만 막으므로 모터 과전류·고속 ADC 같은 IRQ 를 문턱 위에 두면 커널 크리티컬 섹션과 **무관하게 지연 0** 으로 돈다 (Phase F: IRQ31 이 크리티컬 섹션 안에서 실행, BASEPRI 0x40 을 봄). 대가: 문턱 위 IRQ 는 어떤 커널 API 도 못 부른다 — 태스크에 알리려면 문턱 아래의 다른 IRQ 를 소프트웨어 펜딩해 "중계" 해야 한다. 또 BASEPRI = 0 은 "아무것도 안 막음" 이라 문턱을 0 으로 설정하면 크리티컬 섹션이 사라진다 (V11.3.2 의 ARM_CM3/port.c·portmacro.h 에는 0 을 막는 검사가 따로 없었다 — grep 확인. FreeRTOS 문서는 0 금지라고 적는다, 문서 기준).

</details>

<details>
<summary>Q5. 크리티컬 섹션이 3 ms 길면 시스템에 무슨 일이 생기죠? 어떻게 알아내나요?</summary>

실측: SysTick 3 번이 1 번으로 합쳐져 tick 이 2 개 사라졌고(시간이 느려짐 → 모든 delay·타임아웃이 늘어남), 그동안 IRQ30 을 3 번 펜딩했는데 ISR 은 1 번만 돌았다 (FIFO 없는 1 바이트 데이터 레지스터 UART 라면 2 바이트 유실 — 오버런). 문턱 위 IRQ 는 영향 없음. 찾는 법: 크리티컬 섹션 진입/퇴장에 DWT CYCCNT 타임스탬프를 찍어 최대값 기록, 또는 `crit_exit` 에서 `SCB->ICSR.PENDSTSET` 가 이미 서 있었는지 카운트 (= tick 을 늦게 처리한 횟수), 트레이스(RTOS-A06). 고치는 법: 긴 일은 뮤텍스나 스케줄러 잠금으로 옮기고 크리티컬 섹션은 몇 µs 로.

</details>

<details>
<summary>Q6. MSP 와 PSP 를 나누면 무엇이 좋아요?</summary>

ISR 은 항상 MSP 에서 돈다 → 각 태스크 스택은 "자기 최대 깊이 + 예외 진입 프레임 하나(32 B, FPU 컨텍스트면 104 B)" 만 있으면 되고, ISR 중첩 깊이는 MSP 하나에만 더한다 (RTOS-M08). 태스크 10 개 × ISR 중첩 512 B 를 아낀다. 또 태스크 스택 오버플로가 ISR 스택을 덮지 않고, MPU/PSPLIM 으로 태스크 스택만 따로 지킬 수 있다. 이 레슨의 실측: IDLE 스택 사용량 64 B = 정확히 초기 프레임 하나.

</details>

<details>
<summary>Q7. 당신의 상속 뮤텍스가 틀리는 경우는? (꼬리질문)</summary>

① 소유자가 뮤텍스를 **여러 개** 쥐고 하나만 풀 때 — 이 구현은 `held == 0` 일 때만 base 로 내리므로 그 사이엔 너무 높게 남는다 (안전 쪽 오류). FreeRTOS 도 같은 선택 (`uxMutexesHeld == 0` 일 때만 복원, tasks.c:6870). ② **연쇄** (H → L1 → L2): 소유자가 다른 뮤텍스에 막혀 있으면 그 소유자까지 올려야 한다 — 이 구현과 FreeRTOS 는 한 단계만 (RTOS-M04 Part C). ③ 대기자 **타임아웃** 후 되돌리기는 했지만 `held == 1` 인 경우만. ④ 이미 BLOCKED 인 소유자의 우선순위를 올려도 그 wait list 의 정렬 위치는 안 바꾼다. 면접에선 "어디까지 보장하는지 문서로 적고, 나머지는 설계(뮤텍스 하나만, 짧은 임계 구역, 천장 프로토콜)로 피한다" 가 답이다.

</details>

<details>
<summary>Q8. 이 포트를 Cortex-M4F 로 옮긴다면 무엇을 바꾸겠나요?</summary>

FPU 레지스터: 태스크가 FPU 를 쓰면 하드웨어 프레임이 26 워드(S0–S15, FPSCR 포함, lazy stacking)로 커지고 EXC_RETURN bit 4 가 0 이 된다. PendSV 는 `tst r14, #0x10` → `vstmdbeq r0!, {s16-s31}` 후 `stmdb r0!, {r4-r11, r14}` 로 **EXC_RETURN 도 태스크마다 저장**해야 한다 (ARM_CM4F/port.c:516–520). 그래서 초기 프레임에 EXC_RETURN 칸 `0xFFFFFFFD` 가 하나 더 생겨 17 워드 (port.c:226, 추가 연습 5). 스케줄러 시작 때 `FPCCR` 에 ASPEN/LSPEN(lazy stacking) 을 켠다 (ARM_CM4F/port.c:448), 스택 예산에 +104 B (M08). M33 이면 PSPLIM 으로 스택 한계를 하드웨어가 검사하고 TrustZone 이면 EXC_RETURN 비트가 더 늘어난다 (문서 기준).

</details>

<details>
<summary>Q9. 이 RTOS 가 맞게 동작한다는 걸 어떻게 아나요? QEMU 검증이 틀리는 경우는?</summary>

검증: ① `-icount` 로 결정적 실행 → tick 단위 순서·완료 시각을 assert (5 회 출력 동일), ② 스위치마다 불변식 (sp 범위, 스택 바닥 패턴) → `stack_errors=0`, ③ gdb 로 스위치아웃 프레임을 직접 읽어 R5–R11 보존 확인, ④ 결함 주입으로 각 TODO 가 틀리면 **실패하는지** 확인 (F1–F4). QEMU 가 틀리게 만드는 경우: 우선순위 비트 8 개 (실칩 3–4 개 → 실수 6 이 숨음), PC bit0 을 봐줌 (F2), 캐시·버스 대기·인터럽트 지연 사이클 없음 (명령 수 = 시간), 디버거 정지가 가상 시계를 당김 (프로브 효과 실측). 그래서 실보드에서 DWT CYCCNT 로 스위치 시간과 IRQ 지연을 한 번은 재야 한다 (RTOS-A03).

</details>

## 9. 추가 연습

`practice.c` 는 **호스트 C** 다 (QEMU 불필요). 표의 숫자는 위의 QEMU/gdb 실측값이다.

### 연습 1 — 스택 프레임 린터 `frame_lint`

스위치아웃된 태스크의 sp 와 16 워드를 받아 문제를 비트마스크로: T 비트 없음, PC 홀수, PC 가 .text 밖, 스레드 프레임인데 IPSR ≠ 0, sp 가 [바닥 + 가드, 꼭대기 − 64] 밖. gdb 로 읽은 L·H 프레임은 통과해야 하고, F1·F2 처럼 망가뜨린 프레임은 정확한 플래그를 내야 한다.

<details>
<summary>답</summary>

xPSR bit 24, PC bit 0, `.text` 범위, `xpsr & 0x1FF`, `sp + 64 > stack_hi` 를 각각 검사. H 의 LR = 3 은 **문제가 아니다** — LR 은 그 순간의 레지스터 값이라 린트 대상이 아니다 (Part C). 실제 디버거 확장(예: 태스크 인식 gdb 스크립트)도 이런 검사로 "깨진 태스크" 를 표시한다.

</details>

### 연습 2 — EXC_RETURN 해독 `exc_return_decode`

`0xFFFFFFF1/F9/FD/E1/E9/ED` → (Thread?, PSP?, 프레임 8 or 26 워드). 그 밖의 값은 invalid.

<details>
<summary>답</summary>

bit 3 = Thread, bit 2 = PSP, bit 4 = 0 이면 확장(FP) 프레임 26 워드. SVC 진입 LR `0xFFFFFFF9` → `| 0xD` = `0xFFFFFFFD`. HardFault 핸들러의 `tst lr, #4` 가 bit 2 를 보는 이유가 이것이다.

</details>

### 연습 3 — BASEPRI 계산기 `prio_masked`, `can_call_from_isr`

구현 비트 수(8 = QEMU, 4 = STM32)로 우선순위를 자른 뒤, BASEPRI 가 그 IRQ 를 막는지, 그 IRQ 가 FromISR API 를 불러도 되는지.

<details>
<summary>답</summary>

`eff(p) = p & (0xFF << (8 − bits))`. 막힘 = `eff(basepri) != 0 && eff(prio) >= eff(basepri)`. FromISR 허용 = `eff(prio) >= eff(max_syscall)`. 4 비트 칩에서 0x3F 는 0x30 이 되어 문턱 0x40 **위**로 올라간다 — 숫자를 대충 쓰면 의도와 반대가 된다. 리셋 기본값 0 인 IRQ 는 항상 금지.

</details>

### 연습 4 — 잃어버린 tick 계산 `tick_loss`

SysTick 주기 P, 크리티컬 섹션 시작 위상 phase, 길이 L → 지나간 경계 수, 처리된 수(0 또는 1), 손실 수. Phase F 실측(3 바퀴 → 1 처리, 2 손실)과 맞춰 본다.

<details>
<summary>답</summary>

`crossed = (phase + L) / P`, `processed = crossed ? 1 : 0`, `lost = crossed − processed`. L < P 여도 경계를 넘으면 tick 이 **늦게** 처리될 뿐 손실은 없다 — 손실은 L ≥ P 쪽에서 시작한다 (위상에 따라 L 이 2P 근처면 1~2 개). IRQ 펜딩도 같은 규칙: n 번 펜딩 → 1 번 실행.

</details>

### 연습 5 — Cortex-M4F 초기 프레임 `frame_init_cm4f`

FreeRTOS ARM_CM4F/port.c:202–231 처럼 R0 다음에 EXC_RETURN 칸(`0xFFFFFFFD`)을 넣은 17 워드 프레임을 만든다.

<details>
<summary>답</summary>

xPSR, PC(& ~1), LR, R12–R1(5 칸), R0, **EXC_RETURN**, R11–R4 → `top − 17`. PendSV 가 `ldmia r0!, {r4-r11, r14}` 로 EXC_RETURN 을 태스크별로 복원하므로, FPU 를 쓰기 시작한 태스크는 이후 `0xFFFFFFED` 로 저장된다. 처음엔 "FPU 안 씀" (`0xFFFFFFFD`, 8 워드 프레임) 으로 시작.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/RTOS-A01_pr && /tmp/RTOS-A01_pr        # 27/27 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/RTOS-A01_prsan && /tmp/RTOS-A01_prsan
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"                                  # VM 에서도 같은 결과
```

## 10. 핵심 정리

- 초기 프레임 = PendSV 가 막 저장한 모양 16 워드: xPSR `0x01000000`, PC `fn & ~1`, LR 종료 트랩, R0 인자, 그 아래 R4–R11. sp 는 TCB 첫 멤버.
- 첫 시작: MSP 리셋 → `cpsie i` → `svc 0` → SVC 가 R4–R11·PSP 복원, BASEPRI 0, EXC_RETURN `0xFFFFFFFD`. 첫 태스크도 일반 예외 복귀로 들어간다. (SVC 프레임 32 B 는 MSP 에 남는다.)
- PendSV: `mrs psp` → `stmdb r0!, {r4-r11}` → `str r0, [tcb]` → BASEPRI 올리고 `bl` 스케줄러 → 복원 → `bx lr`. **PendSV·SysTick = 최저**, SVC = 최고.
- BASEPRI 크리티컬 섹션은 문턱 아래만 막는다: 문턱 위 IRQ 는 지연 0 이지만 커널 API 금지. ISR 은 이전 BASEPRI 를 저장/복원.
- 막힌 동안 SysTick·IRQ 펜딩은 1 비트로 합쳐진다: 3 ms 막음 → tick 2 개, UART IRQ 2 번 손실 (실측).
- 상속 OFF/ON: H 완료 +11 / +5, H 막힘 `3 + C_M` / `3` (QEMU 실측, M04 공식 그대로). 대기자 타임아웃 시 소유자 우선순위 회수.
- 검증은 `-icount` 결정적 실행 + 스위치 불변식 + gdb 프레임 확인 + 결함 주입. 타이밍 assert 는 디버거 없이 (프로브 효과).

## 참고자료

- FreeRTOS-Kernel V11.3.2 `portable/GCC/ARM_CM3/port.c` (pxPortInitialiseStack :182, prvTaskExitError :202, vPortSVCHandler :228, prvPortStartFirstTask :247, xPortStartScheduler SHPR 설정 :390–392, vPortEnterCritical :426, xPortPendSVHandler :455, xPortSysTickHandler :491, vPortValidateInterruptPriority :803), `portmacro.h` (portYIELD :85, ucPortCountLeadingZeros :148, portGET_HIGHEST_PRIORITY :168, vPortRaiseBASEPRI :212, ulPortRaiseBASEPRI :228), `portable/GCC/ARM_CM4F/port.c` (:202–231, :516–520), `portable/ThirdParty/GCC/RP2040/port.c` (:215–233), `tasks.c` (vTaskStartScheduler :3787, xTaskIncrementTick :4831, vTaskSwitchContext :5215, xTaskPriorityInherit :6745, xTaskPriorityDisinherit :6848, vTaskPriorityDisinheritAfterTimeout :6942), `queue.c` xQueueSemaphoreTake :1659/:1686 — 로컬 `~/.esn_scratch/FreeRTOS-Kernel`.
- ARM, *ARMv7-M Architecture Reference Manual* (DDI 0403): B1.5 예외 모델 (스택 프레임, EXC_RETURN, 예외 복귀 시 PC bit0), B3.4 NVIC, BASEPRI. ARM, *Cortex-M3 Technical Reference Manual* (DDI 0337).
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors*, 3rd ed. (2014) — PendSV 컨텍스트 스위치 예제, SVC/PendSV 우선순위 장.
- QEMU `hw/arm/mps2.c`, `hw/intc/armv7m_nvic.c` (mps2-an385 보드·NVIC 모델), `-icount` 문서 (docs/devel/tcg-icount).
- 같은 시리즈: BM-B08 (부팅·세미호스팅·run_qemu.sh 틀), RTOS-B04 (CLZ 비트맵), RTOS-M02 (FromISR·ISR 출구 yield), RTOS-M03/M04 (역전·상속 숫자), RTOS-M06 (FreeRTOS 재시도 루프), RTOS-M08/M09 (스택 예산·오버플로 탐지), RTOS-M13 (vTaskSwitchContext 읽기), ARM-M11 (QEMU M3 의 LDREX/STREX). 다음 단계: ARM-A01 (PendSV 스위처 심화), RTOS-A02 (tickless), RTOS-A06 (트레이스).
