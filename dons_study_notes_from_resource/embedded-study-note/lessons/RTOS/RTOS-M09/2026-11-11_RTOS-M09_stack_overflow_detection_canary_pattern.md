---
id: RTOS-M09
time: 90분
updated: 2026-11-11
---
# RTOS-M09 스택 오버플로 탐지: 카나리와 패턴 검사

## 학습 목표

- FreeRTOS `configCHECK_FOR_STACK_OVERFLOW` **방법 1**(저장된 SP 범위)과 **방법 2**(+ 스택 맨 아래 16 바이트 `0xa5` 패턴)를 미니 커널에 그대로 구현하고, V11.3.2 `stack_macros.h` 의 비교식(`<=`, `pulStack[0..3]`)까지 맞춘다.
- 같은 오버플로라도 **언제 전환이 일어나느냐**, **프레임의 어디를 쓰느냐** 에 따라 방법 1·2 가 잡기도 놓치기도 한다는 것을 시나리오 7개 + 무작위 300세트로 보인다 — 특히 "큰 배열을 바닥만 쓰고 전환 전에 반환" 하면 **둘 다 놓친다**.
- 탐지는 **전환 시점** 에만 일어나므로 훅이 울릴 때 이웃 태스크의 TCB·문맥은 **이미 깨져 있다** 는 것을 arena hexdump 로 보이고, 깨진 TCB 의 주인(피해자)과 범인이 다르다는 것을 설명한다.
- 진짜 해결책(ARMv8-M **PSPLIM**, MPU 가드 영역, 호스트의 `mprotect` 가드 페이지)을 모델과 실제 SIGBUS 데모로 비교하고, 가드가 있어도 **큰 프레임은 가드를 건너뛸 수 있어서** 컴파일러 프로브(`___chkstk_darwin`)가 필요하다는 것을 실제 디스어셈블리로 확인한다.
- FreeRTOS POSIX 포트에서는 오버플로 훅이 **절대 안 울리는** 이유(태스크 코드는 pthread 스택에서 돈다)를 실행 결과로 보인다.

## 1. 문제

"FreeRTOS 에서 스택 오버플로는 어떻게 잡나요? 그 방법으로 못 잡는 경우는요? 그럼 진짜로는 어떻게 막죠?" — 펌웨어 면접에서 RTOS 경험을 확인하는 단골 3연타다. 현장에서 가장 흔한 "가끔 리셋되는" 버그의 1순위가 태스크 스택 부족인데, `configCHECK_FOR_STACK_OVERFLOW 2` 를 켰다고 안심하는 사람과 그것이 **사후 탐지 + 확률적** 이라는 것을 아는 사람은 디버깅 속도가 완전히 다르다.

RTOS-B03 Part C 에서 ucontext 스택에 `0xA5` 를 칠하고 switch-out 때 카나리를 보는 것을 이미 해 봤다. 이번에는 질문을 뒤집는다: **언제 못 잡는가, 못 잡으면 무엇이 깨지는가, 진짜 방어는 무엇인가.** 그래서 진짜 스택 대신 "SRAM 한 덩어리(arena)" 안에 모든 태스크 스택과 TCB 를 heap_4 처럼 붙여 놓고, SP 를 정수로 흉내 내는 결정적 모델을 쓴다. 오버플로가 이웃을 덮어도 전부 우리 배열 안이라 실제 UB 가 없고 ASan 도 깨끗하다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">arena (SRAM 흉내) — 위가 높은 주소. heap_4 처럼 "스택 다음에 그 TCB" 순서</text>
  <rect x="230" y="34" width="230" height="28" class="f-acc" stroke="currentColor"/><text x="242" y="53" fill="currentColor">O 의 TCB (32 B)</text>
  <rect x="230" y="62" width="230" height="60" class="f-ok" stroke="currentColor"/><text x="242" y="86" fill="currentColor">O 의 프레임들 (main → f → g)</text>
  <text x="242" y="106" fill="currentColor" font-size="12">문맥 68 B 는 전환 때 SP 아래에 push</text>
  <rect x="230" y="122" width="230" height="90" class="f-mut" stroke="currentColor"/><text x="242" y="160" fill="currentColor">아직 안 쓴 칸 = a5 a5 a5 …</text>
  <text x="242" y="180" fill="currentColor" font-size="12">(생성 때 memset, tasks.c:1835)</text>
  <rect x="230" y="212" width="230" height="20" class="f-warn" stroke="currentColor"/><text x="242" y="227" fill="currentColor" font-size="12">맨 아래 16 B = 방법 2 의 가드</text>
  <rect x="230" y="232" width="230" height="40" class="f-acc" stroke="currentColor"/><text x="242" y="250" fill="currentColor" font-size="12">V 의 TCB: saved_sp | prio | lo | hi</text>
  <text x="242" y="265" fill="currentColor" font-size="12">| name | magic</text>
  <rect x="230" y="272" width="230" height="70" class="f-ok" stroke="currentColor"/><text x="242" y="296" fill="currentColor">V 의 스택 (위쪽 = V 의 프레임,</text><text x="242" y="314" fill="currentColor">그 아래 V 의 저장된 문맥)</text>
  <rect x="230" y="342" width="230" height="24" class="f-mut" stroke="currentColor"/><text x="242" y="359" fill="currentColor" font-size="12">.bss 64 B (남의 전역 변수)</text>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="120" y="38">0x20000460</text><text x="120" y="66">0x2000045f</text><text x="120" y="230">0x20000260</text><text x="120" y="250">0x20000240</text><text x="120" y="290">0x2000023f</text><text x="120" y="356">0x20000000</text>
  </g>
  <text x="20" y="130" fill="currentColor">O 의 스택 hi</text><text x="20" y="226" fill="currentColor">O 의 스택 lo</text>
  <path d="M470 100 L520 100" stroke="currentColor" fill="none"/><text x="526" y="96" fill="currentColor">방법 1: 전환 때 저장된 SP</text><text x="526" y="114" fill="currentColor">가 lo 보다 위인가 (TCB 의 값)</text>
  <path d="M470 222 L520 222" stroke="currentColor" fill="none"/><text x="526" y="218" fill="currentColor">방법 2: 방법 1 + 이 16 B 가</text><text x="526" y="236" fill="currentColor">아직 전부 a5 인가</text>
  <path d="M215 80 C 170 120, 170 220, 222 250" stroke="currentColor" fill="none" stroke-dasharray="5 4"/>
  <path d="M222 250 L212 246 L216 238 Z" fill="currentColor"/>
  <text x="20" y="286" fill="currentColor" font-size="12">O 가 넘치면 아래쪽</text><text x="20" y="302" fill="currentColor" font-size="12">= 이웃 V 의 TCB 부터 덮는다</text>
  <text x="526" y="300" fill="currentColor" font-size="12">검사는 "나가는 태스크" 만,</text><text x="526" y="318" fill="currentColor" font-size="12">전환 순간에만 (tasks.c:5259)</text>
</svg>
```

**미니 커널 규칙** (solution.c 머리 주석과 같다 — 레슨마다 커널을 따로 둔다, 공유 라이브러리 없음):

| 항목 | 규칙 |
|---|---|
| 메모리 | `uint8_t mem[4096]` 하나. `[.bss 64][stack0][TCB0][stack1][TCB1]…` — TCB 필드는 리틀 엔디안으로 arena 안에 산다 (그래서 덮어쓰면 커널이 그 값을 그대로 읽는다) |
| SP | arena 오프셋 정수. full-descending (SP = 마지막으로 넣은 바이트). 출력은 `0x20000000 + 오프셋` |
| op | `CALL(n)` 프레임 n B: 맨 위 8 B 프레임 레코드(r7/lr) + 지역 배열. `CALL(n)` 은 전부, `CALL_W(n,w)` 는 바닥부터 w B, `CALL_S(n,"…")` 는 바닥부터 문자열(snprintf). `RET`, `WORK`, `YIELD`, `END`. op 1개 = step 1 |
| 전환 | `YIELD`/`END` 또는 `slice` step 마다(tick 선점). ① 문맥 68 B push ② TCB.saved_sp 갱신 ③ 나가는 태스크 검사 → 훅 ④ 라운드 로빈으로 다음, TCB 에서 saved_sp 를 읽어 복원 |
| 훅 | `vApplicationStackOverflowHook` 자리. 기본 = 기록하고 멈춤(실제로는 로그 후 리셋). 교체 가능 |
| 고장 | 덮어써진 문맥/깨진 SP 로 복귀 = `HardFault-ctx`, 덮어써진 lr 로 RET = `HardFault-ret`, PSPLIM 위반 = `STKOF` |
| 진실(shadow) | 태스크별 `min_sp` 와 남의 메모리에 쓴 바이트 수 `oob`. 실제 커널은 모르는 값 — 탐지기 채점용 |

### Part A — 방법 1·2 구현 (TODO 1, 2)

- `bool check_method1(const kernel_t *k, int t)` — TCB 에서 `saved_sp`, `lo`, `hi` 를 읽어 `saved_sp <= lo + LIMIT_PADDING` 이면 오버플로 (FreeRTOS 와 같은 `<=`). 덤으로 `saved_sp > hi` 도.
- `bool check_method2(const kernel_t *k, int t)` — 방법 1 이거나, `lo` 부터 16 바이트 중 하나라도 `0xA5` 가 아니면 오버플로.
- 시나리오(V = 먼저 만든 이웃, O = 범인, 둘 다 512 B): S1 정상(오탐 없음), S2 깊은 재귀 7단을 쥔 채 YIELD(방법 1 이 잡음), S3 560 B 배열을 다 쓰고 **반환한 뒤** YIELD(방법 1 놓침, 방법 2 잡음), S5 코드는 안 넘었는데 **문맥 68 B push 가** 넘김.

### Part B — 둘 다 놓치는 경우, 타이밍, PSPLIM (TODO 3)

- S4: 560 B 배열의 **바닥 7 바이트만** 쓰고(`snprintf`) 반환 → 가드 16 B 를 건너뛴다 → 방법 1·2 모두 놓치고, 다음에 V 가 깨진 문맥으로 복귀하다 HardFault.
- S6: 64 B 재귀 8단(프레임 레코드만 기록). tick 선점이 깊은 곳에서 일어나면(`slice=1`) 방법 1 이 잡고, 반환 뒤에만 전환하면(`slice=0`) 둘 다 놓친다 — **같은 코드, 같은 버그, 탐지는 타이밍 운**.
- `bool cpu_sp_sub(kernel_t *k, int32_t n)` 에 **PSPLIM 모델**: SP 를 내리는 그 명령에서 한계 아래면 `STKOF` — 쓰기 전에 멈추므로 남의 메모리 0 바이트.
- 무작위 300세트로 성질 검사: 방법 2 ⊇ 방법 1, 오탐 없음, PSPLIM 은 진실과 정확히 일치하고 남의 메모리를 한 바이트도 안 쓴다.

### Part C — STM32 펌웨어: comms 가 ctrl 의 TCB 를 덮는다 (TODO 4)

모터 제어 `ctrl`(512 B) → 통신 `comms`(512 B) → IMU `imu`(384 B) 순서로 생성. comms 가 "GET_STATUS" 명령을 받으면 `format_status()` 가 `char reply[376]` 에 `snprintf(reply, …, "ST T=23.5 V=11.9 I=0.42")` 를 한다(프레임 384 B). 프레임 바닥이 comms 스택 아래 32 B, 즉 **ctrl 의 TCB 위** 에 떨어진다.

- `bool tcb_valid(const kernel_t *k, int t)` — switch-in 전에 TCB 자체를 검증(magic, `lo < hi`, `lo < saved_sp ≤ hi − 68`, 이름에 NUL). 진짜 위치는 모른다고 가정하고 **자기 일관성** 만 본다.
- 4가지 구성 비교: 방법 2 / 방법 2 + `tcb_valid` / PSPLIM / comms 스택 1 KB 로 고침.

## 2. Problem (English)

> **Stack overflow detection in a mini RTOS — and its blind spots.**
>
> All task stacks and TCBs live in one 4 KB arena, allocated like FreeRTOS heap_4 (stack, then its TCB). The CPU is simulated: SP is an arena offset, `CALL(n)` pushes an n-byte frame (8-byte frame record at the top, local array from the bottom), and a context switch pushes a 68-byte Cortex-M4 context below SP and stores SP into the TCB (`pxTopOfStack`, first TCB field). Stacks are pre-filled with `0xA5`. Implement:
>
> 1. `check_method1(k, t)` — overflow iff the saved SP `<= stack_base + padding` (FreeRTOS `configCHECK_FOR_STACK_OVERFLOW 1`), or above the stack top.
> 2. `check_method2(k, t)` — method 1, or any of the 16 lowest stack bytes is not `0xA5` (`configCHECK_FOR_STACK_OVERFLOW 2`).
> 3. `cpu_sp_sub(k, n)` — model the ARMv8-M PSPLIM register: if the new SP would go below the current task's limit, raise `STKOF` **before** any write.
> 4. `tcb_valid(k, t)` — sanity-check a TCB before restoring it (magic, base < top, saved SP leaves room for a full context, NUL-terminated name).
>
> The kernel calls the check on the **outgoing** task at every switch and calls the overflow hook on failure. Show: (a) no false positives on normal code, (b) an overflow method 1 misses but method 2 catches, (c) one both miss (large frame written only at its bottom, released before the switch), (d) the neighbour's TCB/context is already corrupted when the hook fires, (e) PSPLIM catches every case at the faulting instruction.
>
> Follow-ups: Why does FreeRTOS allocate the stack before the TCB? Why can't method 2 ever be complete? What does the hook do, and in which context does it run? How do MPU stack guards and PSPLIM differ? Why does a guard need compiler stack probing? Why does the hook never fire on the FreeRTOS POSIX port?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 27/45 passed, exit 1
./starter -v                                                                 # PASS 줄 + Part C arena hexdump + 시나리오별 이벤트 로그
```

처음 상태(stub)는 "검사를 아무것도 안 하는 커널" 이다. 그래서 오탐 없음·정상 종료 같은 테스트만 통과하고, 표를 보면 모든 열이 똑같다 — 넘친 시나리오는 전부 **이웃 V 가 HardFault** 로 끝나거나(S2, S3, S4, S6) 아무 일 없다는 듯 끝난다(S5: `finished, SILENT`). 탐지기가 없을 때의 현실이 바로 이것이다: 고장 나는 쪽은 범인이 아니라 이웃이다. pthread 를 쓰지 않는 단일 스레드 모델이라 `-pthread`/TSan 은 필요 없다.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 무엇을 어디서 읽나</summary>

FreeRTOS 는 `pxCurrentTCB->pxTopOfStack` 과 `pxCurrentTCB->pxStack` 을 비교한다 — **TCB 안의 값** 이다. 모델도 `tcb_get(k, t, T_SP)`, `tcb_get(k, t, T_LO)`, `tcb_get(k, t, T_HI)` 를 쓴다(그래서 TCB 가 깨지면 검사도 같이 속는다). 비교는 `uint32_t` 로: 깨진 TCB 에 `"ST T"` 같은 값이 있어도 정의된 동작이다. 등호를 넣을지 빼지 말지는 `stack_macros.h:74` 를 그대로 따른다.

</details>

<details>
<summary>힌트 2 — TODO 2: 방법 2 는 방법 1 을 포함한다</summary>

`stack_macros.h:109-113` 은 `pxTopOfStack <= pxStack + padding` **또는** `pulStack[0..3] != 0xa5a5a5a5` 다. 방법 1 을 먼저 부르고, 아니면 `lo .. lo+15` 를 본다. `lo` 가 깨져 arena 밖을 가리킬 수 있으니 `peek()` 를 쓰자(밖이면 -1 → "패턴 아님").

</details>

<details>
<summary>힌트 3 — TODO 3: 쓰기 "전에" 멈춰야 한다</summary>

새 SP 를 먼저 계산하고, `k->psplim` 이면서 새 SP 가 `k->splim` 보다 작으면 `kfault(k, F_STKOF, k->cur)` 후 `false`. SP 를 바꾸지 않고 돌아가야 호출자(`exec`, `task_switch`)가 아무것도 쓰지 않는다. `splim` 은 `switch_in()` 이 TCB 의 `lo` 로 이미 넣어 준다(ARMv8-M 포트가 문맥과 함께 PSPLIM 을 복원하듯). 음수 SP 도 위반이다.

</details>

<details>
<summary>힌트 4 — TODO 4: magic 만으로는 부족하다</summary>

Part C 의 문자열은 24 바이트라 TCB 끝(offset 28)의 magic 까지 오지 않는다. `saved_sp` 가 `[lo+1, hi−68]` 안인지, `lo < hi` 인지가 진짜 판정을 한다. `hi - CTX_BYTES` 는 `hi < 68` 이면 언더플로하니 `lo < hi` 를 먼저 확인하거나 `sp + CTX_BYTES > hi` 로 쓰자(이 모델에선 hi ≥ 64 + 스택이라 괜찮다).

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 미니 커널 | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 / RTX5 (문서 기준, 미검증) |
|---|---|---|---|
| `task_create` 의 `0xA5` 칠하기 | `tskSET_NEW_STACKS_TO_KNOWN_VALUE` → `memset(pxStack, 0xa5, …)` (tasks.c:129-135, 1832-1837) | `CONFIG_INIT_STACKS` (0xaa) | 스레드 생성 시 워터마크 패턴 (`OS_STACK_WATERMARK`) |
| `check_method1` | `configCHECK_FOR_STACK_OVERFLOW 1` (stack_macros.h:67-79) | — | — |
| `check_method2` | `configCHECK_FOR_STACK_OVERFLOW 2` (stack_macros.h:101-118) | `CONFIG_STACK_SENTINEL` (스택 맨 아래 sentinel 값을 전환/ISR 종료 때 검사) | `OS_STACK_CHECK` (스택 바닥 magic word) |
| 검사 위치 = 나가는 태스크 | `vTaskSwitchContext` 안 `taskCHECK_FOR_STACK_OVERFLOW()` (tasks.c:5259) | 전환 시 | 전환 시 |
| `default_hook` | `vApplicationStackOverflowHook(xTask, pcTaskName)` (task.h:2019) | 치명 오류 `K_ERR_STACK_CHK_FAIL` → `k_sys_fatal_error_handler` | `osRtxErrorNotify(osRtxErrorStackOverflow, …)` |
| `cpu_sp_sub` 의 PSPLIM | ARM_CM33 등 v8-M 포트: `portHAS_STACK_OVERFLOW_CHECKING 1` (portmacrocommon.h:117), PSPLIM 을 문맥에 저장·복원 | `CONFIG_HW_STACK_PROTECTION` (+ v8-M 스택 리밋) | 하드웨어 몫 |
| 연습 2 의 MPU 가드 | MPU 포트 (stack_macros.h:56-66 주석) | `CONFIG_MPU_STACK_GUARD` | — |
| 연습 3 `stack_hwm_bytes` | `uxTaskGetStackHighWaterMark()` — **워드** 단위 (tasks.c:6467-6482) | `k_thread_stack_space_get()` | `osThreadGetStackSpace()` |
| `tcb_valid` | 없음 (TCB 에 magic/체크섬 필드 없음, tasks.c:377-) | — | — |

### Part A — 방법 1·2: 몇 줄짜리 비교

```c
static bool check_method1(const kernel_t *k, int t) {
    uint32_t top = tcb_get(k, t, T_SP), base = tcb_get(k, t, T_LO), end = tcb_get(k, t, T_HI);
    if (top <= base + LIMIT_PADDING) return true;      /* stack_macros.h:74 와 같은 <= */
    if (top > end) return true;                        /* 덤: FreeRTOS 는 이쪽은 안 본다 */
    return false;
}
static bool check_method2(const kernel_t *k, int t) {
    if (check_method1(k, t)) return true;
    uint32_t base = tcb_get(k, t, T_LO);
    for (uint32_t i = 0; i < GUARD_BYTES; i++)
        if (peek(k, base + i) != (int)FILL_BYTE) return true;
    return false;
}
```

FreeRTOS 원문(아래로 자라는 스택, MPU 래퍼 아님)과 1:1 이다:

```c
/* include/stack_macros.h:101-118 (V11.3.2) */
const uint32_t * const pulStack = ( uint32_t * ) pxCurrentTCB->pxStack;
const uint32_t ulCheckValue = ( uint32_t ) 0xa5a5a5a5U;
if( ( pxCurrentTCB->pxTopOfStack <= pxCurrentTCB->pxStack + portSTACK_LIMIT_PADDING ) ||
    ( pulStack[ 0 ] != ulCheckValue ) || ( pulStack[ 1 ] != ulCheckValue ) ||
    ( pulStack[ 2 ] != ulCheckValue ) || ( pulStack[ 3 ] != ulCheckValue ) )
{
    char * pcOverflowTaskName = pxCurrentTCB->pcTaskName;
    vApplicationStackOverflowHook( ( TaskHandle_t ) pxCurrentTCB, pcOverflowTaskName );
}
```

숫자 정리:

| 항목 | 값 | 근거 |
|---|---|---|
| 채우는 값 | `0xa5` (바이트) | `tskSTACK_FILL_BYTE` tasks.c:121 |
| 언제 채우나 | 방법 2, trace facility, HWM API 중 하나라도 켜면 생성 때 스택 전체 | tasks.c:129-135, 1832-1837 |
| 방법 2 가 보는 양 | **16 바이트** = `uint32_t` 4 개, 스택 맨 아래 (`pxStack[0]` 쪽) | stack_macros.h:106-113 |
| 위로 자라는 스택이면 | **20 바이트**, `pxEndOfStack` 바로 아래 (연습 1) | stack_macros.h:129-138 |
| 방법 1 의 등호 | `pxTopOfStack <= pxStack + padding` — 마지막 칸까지 꽉 찬 것도 오버플로로 본다 | stack_macros.h:74 |
| padding | 기본 0, 포트가 정할 수 있음 (예: RP2040 포트 4) | stack_macros.h:52-54, ThirdParty/GCC/RP2040 portmacro.h:91 |
| 검사 시점 | `vTaskSwitchContext` 에서 **나가는** 태스크(`pxCurrentTCB`) | tasks.c:5259 |

문맥 크기 68 B 는 Cortex-M4F 포트의 PendSV 에서 온다: 예외 진입 때 하드웨어가 r0-r3, r12, lr, pc, xPSR 8 워드(32 B)를 PSP 에 쌓고, `xPortPendSVHandler` 가 `stmdb r0!, {r4-r11, r14}` 9 워드(36 B)를 더 쌓은 뒤(port.c:520) `str r0, [r2]` 로 **TCB 의 첫 필드** `pxTopOfStack` 에 저장한다(port.c:521, tasks.c:379 "THIS MUST BE THE FIRST MEMBER"). FPU 를 쓴 태스크면 `vstmdbeq r0!, {s16-s31}`(port.c:518)와 하드웨어 FP 프레임이 더해진다 — 이 모델은 FPU 미사용으로 고정했다(최악 스택 계산은 RTOS-M08).

`./solution -v` 의 실제 이벤트 로그 (방법 2):

```text
S2 deep recursion (method 2)
  step   2  switch-in  O
  step   4  O CALL 64 -> SP=20000240, 32 B below its stack
  step   5  switch-out O saved_sp=200001fc
  step   5  vApplicationStackOverflowHook(O)  saved_sp=200001fc
S3 big array, full (method 2)
  step   4  O CALL 560 -> SP=200001d0, 144 B below its stack
  step   6  switch-out O saved_sp=200003bc
  step   6  vApplicationStackOverflowHook(O)  saved_sp=200003bc
S5 ctx frame tips over (method 2)
  step   6  switch-out O saved_sp=20000254
  step   6  vApplicationStackOverflowHook(O)  saved_sp=20000254
```

- **S2** — 재귀 7단(96 + 7×64 = 544 B > 512)을 쥔 채 YIELD. 저장된 SP `0x200001fc` 는 O 의 lo `0x20000260` 보다 100 B 아래 → 방법 1 이 잡는다. 하지만 step 4 에 이미 V 의 TCB 32 B 를 덮었고(hexdump 가 전부 `d1`), step 5 의 문맥 push 68 B 는 V 의 스택 꼭대기를 덮었다. 테스트: `oob == 100`, `first_oob_step == 4` < `hook_step == 5`.
- **S3** — 560 B 배열을 다 쓰고 **반환한 뒤** YIELD. 전환 순간 SP 는 `0x200003bc` 로 멀쩡 → 방법 1 은 통과(`HardFault-ctx(V)@6` — V 가 덮인 문맥으로 복귀하다 죽는다). 방법 2 는 가드 16 B 가 `d1` 로 바뀐 것을 본다.
- **S5** — 코드 자체는 lo 위 56 B 에서 멈췄지만(SP `0x20000298`), 전환 때 push 하는 문맥 68 B 가 lo 아래 12 B 까지 내려간다. 방법 1 이 잡는다(`saved_sp = 0x20000254`). 검사가 없으면 `finished, SILENT`: V 의 TCB 꼬리(이름 끝 + magic)가 `c1` 로 덮인 채 끝까지 돈다. **ISR 문맥도 같은 원리** — 인터럽트 진입 프레임이 태스크 스택(PSP)에 쌓이는 순간이 최악점이다(첫 프레임만 PSP, 중첩 ISR 은 MSP — RTOS-M08).

### Part B — 왜 "방법 2 = 안전" 이 아닌가

`./solution` 첫 표 (실제 출력):

```text
Part A/B  scenario                min_sp-lo  oob | method 1              method 2              PSPLIM
          S1 normal                   156      0 | finished              finished              finished
          S2 deep recursion          -100    100 | hook(O)@5             hook(O)@5             STKOF(O)@4
          S3 big array, full         -144    144 | HardFault-ctx(V)@6    hook(O)@6             STKOF(O)@4
          S4 big array, sparse       -144      7 | HardFault-ctx(V)@6    HardFault-ctx(V)@6    STKOF(O)@4
          S5 ctx frame tips over      -12     12 | hook(O)@6             hook(O)@6             STKOF(O)@6
          S6 recursion, slice=0       -96      8 | HardFault-ret(V)@15   HardFault-ret(V)@15   STKOF(O)@4
          S6 recursion, slice=1      -164     76 | hook(O)@4             hook(O)@4             STKOF(O)@4
random 300 sets: method1 hooks=219, method2 hooks=229, overflowed but both missed=19, hooks without overflow=1, violations=0
```

(`min_sp-lo` = 그림자의 최저 SP − lo, 음수면 넘친 바이트 수. `oob` = 검사 없이 돌렸을 때 남의 메모리에 쓴 바이트. `@n` = step.)

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 144 B 오버플로, 세 가지 모양 — 전환 순간에 무엇이 남아 있나</text>
  <g>
    <text x="40" y="46" fill="currentColor" font-weight="bold">S2 깊은 재귀 (쥔 채 전환)</text>
    <rect x="60" y="60" width="140" height="160" class="f-ok" stroke="currentColor"/>
    <rect x="60" y="220" width="140" height="22" class="f-warn" stroke="currentColor"/><text x="70" y="236" fill="currentColor" font-size="12">문맥 68 B</text>
    <line x1="40" y1="210" x2="220" y2="210" stroke="currentColor" stroke-dasharray="4 3"/><text x="20" y="206" fill="currentColor" font-size="12">lo</text>
    <path d="M225 242 L205 242" stroke="currentColor"/><text x="228" y="246" fill="currentColor" font-size="12">SP</text>
    <text x="40" y="268" fill="currentColor">방법 1: 잡음</text><text x="40" y="286" fill="currentColor">방법 2: 잡음</text><text x="40" y="304" fill="currentColor" font-size="12">(이미 V 의 TCB 를 덮은 뒤)</text>
  </g>
  <g>
    <text x="280" y="46" fill="currentColor" font-weight="bold">S3 큰 배열 전부 기록 → 반환</text>
    <rect x="300" y="60" width="140" height="40" class="f-ok" stroke="currentColor"/><text x="310" y="84" fill="currentColor" font-size="12">살아 있는 프레임</text>
    <rect x="300" y="100" width="140" height="142" class="f-warn" stroke="currentColor" stroke-dasharray="5 3"/><text x="310" y="150" fill="currentColor" font-size="12">반환된 560 B 프레임</text><text x="310" y="168" fill="currentColor" font-size="12">(전부 d1 로 기록됨)</text>
    <line x1="280" y1="210" x2="460" y2="210" stroke="currentColor" stroke-dasharray="4 3"/>
    <text x="310" y="204" fill="currentColor" font-size="12">가드 16 B 깨짐</text>
    <path d="M465 100 L445 100" stroke="currentColor"/><text x="468" y="104" fill="currentColor" font-size="12">SP</text>
    <text x="280" y="268" fill="currentColor">방법 1: 놓침 (SP 정상)</text><text x="280" y="286" fill="currentColor">방법 2: 잡음 (패턴)</text>
  </g>
  <g>
    <text x="520" y="46" fill="currentColor" font-weight="bold">S4 큰 배열 바닥만 기록</text>
    <rect x="540" y="60" width="140" height="40" class="f-ok" stroke="currentColor"/><text x="550" y="84" fill="currentColor" font-size="12">살아 있는 프레임</text>
    <rect x="540" y="100" width="140" height="142" class="f-mut" stroke="currentColor" stroke-dasharray="5 3"/><text x="550" y="150" fill="currentColor" font-size="12">반환된 프레임</text><text x="550" y="168" fill="currentColor" font-size="12">대부분 a5 그대로</text>
    <rect x="540" y="232" width="140" height="10" class="f-warn" stroke="currentColor"/>
    <line x1="520" y1="210" x2="700" y2="210" stroke="currentColor" stroke-dasharray="4 3"/>
    <text x="550" y="204" fill="currentColor" font-size="12">가드 16 B 무사</text>
    <text x="550" y="258" fill="currentColor" font-size="12">"T=23.5" → V 의 문맥 위</text>
    <path d="M705 100 L685 100" stroke="currentColor"/><text x="708" y="104" fill="currentColor" font-size="12">SP</text>
    <text x="520" y="286" fill="currentColor">방법 1·2: 둘 다 놓침</text><text x="520" y="304" fill="currentColor" font-size="12">→ V 복귀 때 HardFault</text>
  </g>
</svg>
```

**S4 — 둘 다 놓친다.** 실제 hexdump (`./solution -v`, V 의 저장된 문맥 근처):

```text
    200001c0: c0 c0 c0 c0 c0 c0 c0 c0 c0 c0 c0 c0 c0 c0 c0 c0  |................|
    200001d0: 54 3d 32 33 2e 35 00 c0 c0 c0 c0 c0 c0 c0 c0 c0  |T=23.5..........|
    200001e0: d0 d0 d0 d0 d0 d0 d0 d0 d0 d0 d0 d0 d0 d0 d0 d0  |................|
    ...
    20000240: 9c 01 00 00 01 00 00 00 40 00 00 00 40 02 00 00  |........@...@...|   ← V 의 TCB (무사)
    20000250: 56 00 00 00 00 00 00 00 00 00 00 00 de c0 b0 7c  |V..............||
    20000260: a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5  |................|   ← O 의 가드 (무사)
```

`c0` = V 의 저장된 문맥(0xC0|0), `d0` = V 의 지역 변수, `f0` = V 의 프레임 레코드. O 의 560 B 프레임 바닥(`0x200001d0`)이 V 의 문맥 한가운데에 떨어졌고, O 는 그 7 바이트만 썼다. 가드는 `a5` 그대로, 전환 순간 SP 도 정상 — 커널은 볼 방법이 없다. 그리고 죽는 것은 **V** 다(step 6, V 로 복귀하며 깨진 PC/xPSR 를 pop). 크래시 덤프는 V 를 가리키는데 범인은 O — 이것이 "가끔 리셋, 크래시 위치는 매번 다름" 버그의 정체다.

이 모양은 지어낸 것이 아니다. 같은 모양의 C 코드를 Cortex-M4 로 컴파일하면(`demo_bigframe_cm4.c`, 실행하지 않고 `-S` 로 어셈블리만 본다, 실제 출력):

<!-- code: demo_bigframe_cm4.c -->

```text
$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -O2 -S demo_bigframe_cm4.c -o -
format_status:
	push	{r7, lr}          ← 프레임 맨 위 8 B = 모델의 FRAME_REC
	sub	sp, #384          ← SP 를 한 번에 384 B 내림 (검사·프로브 없음)
	...
	strb.w	r0, [sp]          ← 첫 쓰기 = reply[0] = 프레임의 가장 낮은 주소
	strb.w	r0, [sp, #1]
format_status_big:
	push	{r7, lr}
	sub.w	sp, sp, #8192     ← 8 KB 프레임도 한 명령
	strb.w	r0, [sp]
```

모델의 `CALL_S(384, "…")` 가 정확히 이 모양이다: 맨 위 8 B 와 맨 아래 몇 B 만 쓰고 가운데는 손대지 않는다. 같은 clang 에 `-fstack-clash-protection` 을 주면 `warning: argument unused during compilation: '-fstack-clash-protection'` 이 나오고 프로브가 생기지 않았다 — 이 타깃에서는 지원되지 않는다.

**S6 — 타이밍 운.** 같은 스크립트를 `slice=0`(YIELD 때만 전환)으로 돌리면 깊은 곳에서는 전환이 없고, 반환 뒤 SP 는 정상, 레코드 8 B 만 쓴 프레임들은 가드를 비켜 간다 → 둘 다 놓치고 V 가 `RET` 에서 덮인 lr 로 죽는다(`HardFault-ret(V)@15`). `slice=1`(매 step tick 선점)이면 재귀 바닥에서 선점되어 방법 1 이 잡는다. 실제 MCU 에서 이 버그는 "tick 이 마침 그 순간에 들어왔을 때만" 훅이 울린다 — 재현이 안 되는 이유.

**무작위 300세트** (`main` 의 루프, 고정 시드 xorshift32) 가 확인한 성질:

| 성질 | 결과 |
|---|---|
| 방법 1 이 잡으면 방법 2 도 잡고, 더 늦지 않다 | 위반 0 (방법 1 훅 219 / 방법 2 훅 229) |
| 오탐 없음: 훅이 울렸다면 최저 SP 가 `lo + 16` 아래까지 갔다 | 위반 0. 단 1건은 **넘치진 않았는데** 울렸다 — 저장된 SP 가 정확히 `lo` (꽉 찬 상태, `<=` 의 보수성) |
| 넘쳤는데 둘 다 놓침 | **19 / 300** |
| PSPLIM: 진실(넘쳤나)과 정확히 일치 + 남의 메모리 0 바이트 | 위반 0 |

**PSPLIM — 넘치는 그 명령에서.** TODO 3 은 두 줄이다:

```c
int32_t nsp = k->sp - n;
if (k->psplim && (nsp < 0 || (uint32_t)nsp < k->splim)) { kfault(k, F_STKOF, k->cur); return false; }
```

표의 PSPLIM 열은 전부 범인 O 에서, 넘치는 바로 그 step 에서(S2·S3·S4·S6 은 step 4 의 `CALL`, S5 는 step 6 의 문맥 push) 멈추고 `oob = 0` 이다. 실제 하드웨어(Arm v8-M 아키텍처 문서 기준, 실보드 미검증): Main Extension 이 있는 Cortex-M33/M55/M85 등은 `PSPLIM`/`MSPLIM` 레지스터가 있어 SP 가 한계 아래로 내려가는 명령이 UsageFault(UFSR.STKOF)를 낸다. FreeRTOS V11.3.2 의 ARM_CM33 포트는 이것을 쓴다:

| 위치 | 코드 |
|---|---|
| portmacrocommon.h:117 | `#define portHAS_STACK_OVERFLOW_CHECKING 1` |
| tasks.c:1982-1986 | 그러면 `pxPortInitialiseStack(pxTopOfStack, pxNewTCB->pxStack, …)` — 스택 바닥을 포트에 넘긴다 |
| port.c:1699 | 첫 문맥에 `*pxTopOfStack = ( StackType_t ) pxEndOfStack; /* Slot used to hold this task's PSPLIM value. */` |
| portasm.c:475-476 | PendSV 저장: `mrs r3, psplim` → `stmdb r2!, {r0, r3, lr}` |
| portasm.c:511-512 | 복원: `ldmia r2!, {r0, r3, lr}` → `msr psplim, r3` |
| port.c:74-81 | Cortex-M23(Main Extension 없음)은 Non-secure PSPLIM 이 없어 Secure 쪽에서만 쓴다 |

Cortex-M4/M7(ARMv7-M)에는 스택 리밋 레지스터가 없다. 그쪽의 하드웨어 방어는 MPU 로 각 태스크 스택 맨 아래에 "접근 금지" 영역을 두고 전환 때마다 다시 설정하는 것이다(연습 2). FreeRTOS 도 MPU 포트에서는 소프트웨어 검사를 아예 빼 버린다 — `stack_macros.h:56-66`: "It is still okay because an MPU region is used to protect task stack which means task stack overflow will trigger an MPU fault for unprivileged tasks. Additionally, architectures with hardware stack overflow checking support (such as Armv8-M) will trigger a fault when a task's stack overflows."

FreeRTOS 자신도 방법 2 의 한계를 머리 주석에 적어 두었다(stack_macros.h:42-43): "Note this second test does not guarantee that an overflowed stack will always be recognised."

### Part C — 피해자와 범인은 다르다

`./solution` 의 Part C 표와 `-v` 의 arena 덤프 (실제 출력):

```text
Part C  config                      outcome
        method 2                    HardFault-ctx(ctrl)@15   (comms oob=24 B)
        method 2 + tcb_valid        TCB-corrupt(ctrl)@15   (comms oob=24 B)
        PSPLIM                      STKOF(comms)@10   (comms oob=0 B)
        method 2, comms 1 KB        finished   (comms oob=0 B)
Part C, method 2 — arena map
    ctrl  stack 20000040..2000023f  TCB 20000240..2000025f
    comms stack 20000260..2000045f  TCB 20000460..2000047f
    imu   stack 20000480..200005ff  TCB 20000600..2000061f
  ctrl TCB + comms guard after the run:
    20000240: 53 54 20 54 3d 32 33 2e 35 20 56 3d 31 31 2e 39  |ST T=23.5 V=11.9|
    20000250: 20 49 3d 30 2e 34 32 00 00 00 00 00 de c0 b0 7c  | I=0.42........||
    20000260: a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5  |................|
  event log:
  step   8  switch-in  comms
  step  10  comms CALL 384 -> SP=20000240, 32 B below its stack
  step  13  switch-out comms saved_sp=200003bc
  step  13  switch-in  imu
  step  15  switch-out imu saved_sp=2000056c
  step  15  FAULT HardFault-ctx in ctrl
```

한 줄씩:

1. comms 의 스택 사용: 태스크 프레임 96 + `handle_cmd` 64 + `format_status` 384 = 544 B > 512. 프레임 바닥 `0x20000240` = **ctrl 의 TCB 첫 바이트**.
2. `snprintf` 가 바닥부터 24 B 를 쓴다: ctrl 의 `saved_sp` 가 `"ST T"` = `0x54205453`, prio·lo·hi·이름이 문자열 조각이 된다. magic(`de c0 b0 7c`)과 comms 의 가드(`a5` × 16)는 무사.
3. step 13 comms 가 반환 후 전환: 방법 2 는 SP·가드 모두 정상이라 **놓친다**.
4. step 15 ctrl 차례: TCB 의 `saved_sp` 로 문맥을 pop → HardFault. 크래시 덤프의 "현재 태스크" 는 **ctrl**. 범인 comms 는 이미 다른 데서 멀쩡히 돌았다.

`tcb_valid()`(TODO 4)를 switch-in 전에 부르면 같은 손상을 `TCB-corrupt(ctrl)` 로 **진단** 할 수 있다 — 막지는 못한다(손상은 step 10). 그래도 "어디서 터졌는지 모르는 HardFault" 가 "ctrl 의 TCB 가 깨졌다" 로 바뀌고, 연습 4 처럼 heap 배치를 알면 **그 TCB 바로 위에 스택이 있는 comms** 를 용의자로 바로 짚는다. FreeRTOS 가 아래로 자라는 스택에서 "스택 먼저, TCB 나중" 으로 할당하는 이유도 이것이다 — tasks.c:1640-1642 "If the stack grows down then allocate the stack then the TCB so the stack does not grow into the TCB." **자기** TCB 는 지키지만, 바로 아래 블록(이웃의 TCB)은 지키지 못한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C 타임라인 (step) — 손상과 탐지 사이의 거리</text>
  <line x1="60" y1="120" x2="720" y2="120" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="56" y="140">8</text><text x="166" y="140">10</text><text x="331" y="140">13</text><text x="441" y="140">15</text></g>
  <rect x="60" y="90" width="110" height="24" class="f-ok" stroke="currentColor"/><text x="66" y="107" fill="currentColor" font-size="12">comms 실행</text>
  <rect x="170" y="90" width="165" height="24" class="f-warn" stroke="currentColor"/><text x="176" y="107" fill="currentColor" font-size="12">ctrl TCB 이미 깨짐</text>
  <rect x="335" y="90" width="110" height="24" class="f-ok" stroke="currentColor"/><text x="341" y="107" fill="currentColor" font-size="12">imu 실행</text>
  <path d="M170 86 L170 60" stroke="currentColor"/><text x="120" y="54" fill="currentColor">CALL 384 + snprintf (손상)</text>
  <path d="M335 86 L335 70" stroke="currentColor"/><text x="300" y="66" fill="currentColor">방법 2 검사: 놓침</text>
  <path d="M445 86 L445 60" stroke="currentColor"/><text x="430" y="54" fill="currentColor">ctrl 복귀 → HardFault</text>
  <rect x="160" y="160" width="20" height="20" class="f-acc" stroke="currentColor"/><text x="190" y="175" fill="currentColor">PSPLIM: step 10 의 sub sp 에서 STKOF — 쓰기 0 B, 범인 comms 를 정확히 가리킴</text>
  <rect x="435" y="190" width="20" height="20" class="f-mut" stroke="currentColor"/><text x="465" y="205" fill="currentColor">tcb_valid: step 15, 진단만</text>
  <text x="60" y="236" fill="currentColor">소프트웨어 검사는 "다음 전환" 에서야 본다. 그 사이 몇 ms 동안 시스템은 깨진 메모리로 돈다.</text>
</svg>
```

**고치는 순서** (현장 기준):

| 대책 | 효과 | 비용 |
|---|---|---|
| comms 스택 1 KB (위 표 4번째 줄) | 이 버그는 사라짐, 훅 0 | RAM 512 B. 크기는 정적 분석(`-fstack-usage`, RTOS-M08) + HWM 실측으로 |
| `reply` 를 스택 대신 `static` / 태스크 소유 버퍼 / 메모리 풀 | 큰 지역 배열 자체를 없앰 | 재진입성 주의 (RTOS-M14) |
| `configCHECK_FOR_STACK_OVERFLOW 2` 유지 | 흔한 경우(깊은 재귀, 다 쓰는 배열)를 싸게 잡음 | 전환마다 비교 5번 |
| MPU 가드 (v7-M) / PSPLIM (v8-M) | 넘치는 순간 fault, 범인 정확 | MPU 영역 1개 + 전환 비용 / v8-M 칩 필요 |
| 훅에서 태스크 이름·PSP·`pxTopOfStack` 을 noinit RAM 에 남기고 리셋 | 다음 부팅에서 원인 분석 | 거의 없음 |

### 호스트 데모 1 — 진짜 가드 페이지 (`demo_guard_page.c`)

MPU 가드의 호스트판: `mmap` 한 덩어리를 `[neighbor 128 KB][guard 1 페이지][task stack 64 KB]` 로 나누고 ucontext 태스크를 그 스택에서 돌린다(ucontext 사용법은 RTOS-B03). (B)(C)(D)는 `fork()` 한 자식에서 돌려 부모가 `waitpid` 로 결과를 본다(death test).

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_guard_page.c -o demo_guard_page && ./demo_guard_page
cc -std=c11 -Wall -Wextra -Werror -O2 -fno-stack-check demo_guard_page.c -o demo_noprobe && ./demo_noprobe
```

<!-- code: demo_guard_page.c -->

```text
$ ./demo_guard_page
page size = 16384 B, stack = 64 KB, neighbor below guard = 128 KB
(A) no guard   : task returned normally; neighbor bytes changed = 3632 (guard page itself changed: yes)
    (B) guard page : child killed by signal 10 (Bus error: 10)
    handler (on sigaltstack): sig=SIGBUS fault addr = guard+16376 B -> inside guard page
    (C) + handler  : child exit code 42
    handler (on sigaltstack): sig=SIGBUS fault addr = guard+16352 B -> inside guard page
    (D) 96 KB frame: child exit code 42
$ ./demo_noprobe
...(A)~(C) 동일...
    (D) child: big_frame returned, neighbor bytes changed = 1
    (D) 96 KB frame: child exit code 0
```

- (A) 가드 없음: 재귀가 64 KB 스택과 그 아래 16 KB 를 지나 neighbor 3632 B 를 덮었는데 태스크는 **정상 반환**. 소프트웨어 검사가 없는 MCU 와 같다.
- (B) 가드 페이지 `PROT_NONE`: 건드리는 순간 SIGBUS (Apple Silicon macOS 는 이 경우 SIGSEGV 가 아니라 SIGBUS 였다, 신호 10).
- (C) 핸들러는 **넘친 스택 위에서는 돌 수 없다** — `sigaltstack` 으로 대체 스택을 주고 `SA_ONSTACK`. 폴트 주소가 가드 페이지의 맨 위(guard+16376)다. Cortex-M 에서 MemManage/HardFault 핸들러가 MSP 에서 도는 것과 같은 이유다.
- (D) 96 KB 프레임(> 스택 64 KB + 가드 16 KB)의 맨 아래 1 바이트만 쓴다. 기본 빌드는 그래도 가드에서 멈췄다 — 함수 앞에 컴파일러가 넣은 **스택 프로브** 때문이다. `-fno-stack-check` 로 프로브를 빼면 1 바이트가 **가드를 건너뛰어** neighbor 에 떨어지고 함수는 멀쩡히 반환한다. 이것이 S4 / Part C 의 호스트판이고, 리눅스 유저공간에서 2017년 "Stack Clash" (Qualys) 로 알려진 문제다.

프로브의 실제 모습 (`xcrun llvm-objdump -d`, lldb `disassemble -n ___chkstk_darwin`):

```text
_big_frame:
    stp  x28, x27, [sp, #-0x20]!
    stp  x29, x30, [sp, #0x10]
    add  x29, sp, #0x10
    mov  w9, #0x8010
    movk w9, #0x1, lsl #16          ; x9 = 0x18010 = 프레임 크기
    ldr  x16, [x16]                 ; ___chkstk_darwin (GOT)
    blr  x16
    sub  sp, sp, #0x18, lsl #12     ; 그 다음에야 SP 를 내린다
libsystem_pthread.dylib`___chkstk_darwin:
    mrs  x10, TPIDRRO_EL0           ; 스레드 정보에서 이 pthread 스택의 위·아래 경계를 읽어
    ...                             ; SP 가 그 범위 안이고 프레임이 바닥 위에서 끝나면 그냥 ret
    ldur x11, [x11, #-0x8]          ; 범위 안인데 바닥을 넘으면: 바닥 바로 아래(가드)를 읽어 일부러 fault
    sub  x10, x10, #0x1, lsl #12    ; 범위 밖이면: 4 KB 씩 내려가며
    ldr  x11, [x10]                 ;   한 번씩 "읽는다"
```

(디스어셈블리를 읽은 해석이다. 바이너리의 GOT 가 이 libsystem_pthread 판으로 연결되는 것으로 보이며, dyld 안에도 `ldrb wzr, [x10]` 로 4 KB 마다 읽는 `__chkstk_darwin_probe` 가 따로 있다.) ucontext 스택은 pthread 스택 범위 밖이라 프로브 루프를 탄다. 프로브가 **읽기** 라는 점이 중요하다: 접근 자체가 fault 인 가드 페이지/MPU 영역에는 효과가 있지만, `0xA5` 패턴 검사(방법 2)는 읽기로 바뀌지 않으므로 여전히 놓친다(연습 2 에서 확인).

### 호스트 데모 2 — FreeRTOS POSIX 포트에선 훅이 안 울린다 (`demo_freertos_overflow.c`)

<!-- code: demo_freertos_overflow.c -->

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
CF=(-O2 -g -std=gnu11 -pthread -I. -I$K/include -I$P -I$P/utils)
cc "${CF[@]}" -Wall -Wextra -c $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
cc "${CF[@]}" -Wall -Wextra -Werror -c demo_freertos_overflow.c hooks.c
cc -pthread demo_freertos_overflow.o hooks.o tasks.o list.o queue.o timers.o event_groups.o heap_3.o port.o wait_for_event.o -o demo_freertos_overflow && ./demo_freertos_overflow
```

```text
hog stack (kernel) : base 0x1054d56f0 .. end 0x1054d5ee8  (2048 B), pxTopOfStack 0x1054d5ec0
hog pthread stack  : 536576 B, a local lives at 0x16af0ef28
[1] 16 KB local live across vTaskDelay: hook calls = 0
    big[] at 0x16af0af28..0x16af0ef27  inside kernel stack? NO
    pxTopOfStack before 0x1054d5ec0 after 0x1054d5ec0 (unchanged)
    lowest 16 B of kernel stack: a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
    high-water mark = 251 words of 256
[3] after clobbering pxStack word 1: hook calls = 1, name = hog
```

(주소는 실행마다 다르다. 나머지 값은 두 번 실행에서 같았다.) 2 KB 스택 태스크가 16 KB 배열을 쥔 채 전환했는데 훅 0 회. 이유가 숫자에 다 있다:

- 커널이 준 스택은 `0x1054d56f0..` 인데 배열은 `0x16af0a…` — 태스크 코드는 커널 스택이 아니라 **pthread 의 512 KB 스택** 에서 돈다 (RTOS-B07 에서 본 536576 B).
- POSIX 포트의 `pxPortInitialiseStack` 은 커널 스택 꼭대기에 `Thread_t` 를 놓고 `pxTopOfStack` 을 그 바로 아래로 정한 뒤(port.c:226-227) 다시는 바꾸지 않는다 → 방법 1 이 볼 SP 가 늘 같은 값. `end − pxTopOfStack = 0x28 = 40 B = Thread_t 5 워드`, 그래서 HWM 도 256 − 5 = 251 (RTOS-B07 과 같은 결과).
- 커널 스택 맨 아래 16 B 는 아무도 안 쓰니 영원히 `a5`.
- [3] 검사 코드 자체는 살아 있다: `pxStack[1]` 을 직접 0 으로 덮고 `vTaskDelay(1)` → `vTaskSwitchContext` 의 방법 2 가 즉시 `hog` 로 훅을 부른다.

결론: POSIX 포트·시뮬레이터에서 "훅이 안 울린다 / HWM 이 넉넉하다" 는 아무 의미가 없다. 스택 검증은 타깃 보드(또는 이 레슨 같은 모델)에서 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 45/45 passed (5회 반복, 출력 바이트 단위 동일)
./solution -v                                                                 # Part C hexdump + 이벤트 로그 + 시나리오별 덤프
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 45/45, 보고 없음
```

오버플로를 "흉내" 만 내므로 ASan/UBSan 이 조용한 것이 정상이다 — 이웃을 덮는 쓰기도 전부 `mem[4096]` 안이고, 깨진 TCB 값(`"ST T"`)은 `uint32_t` 비교와 `peek()` 범위 검사로만 다룬다. 처음 버전은 arena 가 2 KB 라 고친 구성(comms 1 KB)이 배열 끝을 넘었고 UBSan 이 `index 2052 out of bounds` 로 잡았다 → 4 KB 로 키우고 `task_create` 가 넘치는 배치를 거부하게 했다(모델이 진짜로 넘치면 안 된다).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 대응 |
|---|---|---|
| "방법 2 켰으니 오버플로는 잡힌다" | 바닥만 쓰는 큰 프레임(S4, Part C)과 전환 전 반환(S3 의 방법 1, S6)은 놓친다. 무작위 300세트 중 19세트 | 방법 2 = 싼 1차 방어. 진짜 방어는 MPU 가드 / PSPLIM |
| 훅이 울렸으니 범인 태스크만 고치면 된다 | 훅이 울릴 땐 이웃 TCB·문맥이 **이미** 깨졌다 (S2: 탐지 step 5, 첫 손상 step 4) | 훅에서 계속 돌지 말고 기록 후 리셋 |
| HardFault 덤프의 현재 태스크 = 범인 | Part C: 죽은 건 ctrl, 범인은 comms | 깨진 TCB 바로 위 블록의 스택을 의심 (연습 4) |
| 훅에서 `printf`, 큐 send, `vTaskDelay` | 훅은 `vTaskSwitchContext` 안(PendSV, 스케줄러 임계 구역)에서 불린다 | 전역/noinit 변수에 이름·PSP 기록, `taskDISABLE_INTERRUPTS()` 후 리셋 |
| `check_method1` 을 `<` 로 | FreeRTOS 는 `<=` — 꽉 찬(`pxTopOfStack == pxStack`) 것도 위험으로 본다 | `top <= base + padding` |
| `configMINIMAL_STACK_SIZE` 를 바이트로 계산 | 단위는 **워드**(`StackType_t`). Cortex-M 은 ×4, POSIX 포트는 ×8 | `xTaskCreate(…, 256 /* words = 1 KB */, …)` |
| POSIX 포트/시뮬레이터에서 훅이 안 울리고 HWM 도 넉넉하니 OK | 태스크는 pthread 스택에서 돈다 (데모 2: 16 KB 배열에도 훅 0, HWM 251/256) | 타깃 보드에서 측정 |
| HWM(0xA5 로 잰 여유)을 그대로 믿음 | 건너뛴 영역은 `a5` 로 남아 HWM 이 넉넉하게 나온다 (연습 3: 348 B "여유" 인데 실제론 144 B 넘침) | 정적 분석(RTOS-M08) + 최악 경로 테스트 + 여유 |
| 큰 지역 버퍼(`char buf[512]`)를 태스크 함수에 | 스택 예산의 대부분을 한 프레임이 먹고, 바닥만 쓰여 가드를 건너뛴다 | `static`/풀/태스크 소유 버퍼, 또는 스택을 그만큼 키움 |
| MPU 가드만 두면 끝 | 가드보다 큰 프레임은 가드를 건너뛴다 (연습 2, 데모 1 의 `-fno-stack-check`) | 가드 ≥ 최대 프레임, 또는 프로브, 또는 PSPLIM |

## 8. 면접 Q&A

<details>
<summary>Q1. FreeRTOS 의 스택 오버플로 검사 방법 1 과 2 를 설명해 보세요. 비용은?</summary>

둘 다 `vTaskSwitchContext` 에서 **나가는 태스크** 에 대해 돈다(tasks.c:5259). 방법 1 은 문맥을 저장한 뒤의 `pxTopOfStack` 이 `pxStack + portSTACK_LIMIT_PADDING` 이하인지 비교 한 번(stack_macros.h:74). 방법 2 는 거기에 더해 스택 맨 아래 16 바이트(`uint32_t` 4개)가 생성 때 칠한 `0xa5a5a5a5` 그대로인지 본다(101-118). 비용은 전환마다 비교 1번 / 5번 + 생성 때 스택 전체 `memset`. 방법 1 은 "지금 넘쳐 있는" 순간만, 방법 2 는 "예전에 바닥까지 쓴 흔적" 도 잡는다. 꼬리질문 "왜 방법 2 가 방법 1 을 포함하나?" — 매크로가 OR 로 둘 다 본다. 그래서 방법 2 를 켜면 방법 1 은 따로 필요 없다.

</details>

<details>
<summary>Q2. 방법 2 로도 못 잡는 오버플로를 예로 들어 보세요.</summary>

① 큰 지역 배열의 **바닥만** 쓰는 함수: `char buf[512]; snprintf(buf, …, "short")` 는 프레임의 가장 낮은 주소부터 몇 바이트만 쓴다. 프레임이 스택보다 크면 그 몇 바이트는 가드 16 B **아래**(이웃 메모리)에 떨어지고 가드는 `a5` 그대로다. 함수가 전환 전에 반환하면 SP 도 정상 → 둘 다 놓친다(S4, Part C). Cortex-M4 asm 이 정확히 `sub sp, #384` + `strb.w r0, [sp]` 모양이다. ② 타이밍: 깊은 재귀를 하는 동안 전환이 없으면 방법 1 은 볼 기회가 없다(S6). ③ 검사는 나가는 태스크만 — 최고 우선순위로 계속 도는 태스크는 전환이 없어 검사되지 않는다. ④ 무엇보다, 잡아도 **사후** 다. FreeRTOS 소스 주석도 "does not guarantee that an overflowed stack will always be recognised" (stack_macros.h:42-43).

</details>

<details>
<summary>Q3. 훅(vApplicationStackOverflowHook) 안에서는 무엇을 해야 하나요? 어떤 문맥에서 불리나요?</summary>

PendSV 안의 `vTaskSwitchContext` — 즉 스케줄러 임계 구역, 인터럽트 우선순위 마스크(BASEPRI) 상태에서 불린다. 막히는 API, 큐, `printf`(UART 드라이버가 세마포어를 쓰면) 금지. 메모리가 이미 깨졌으니 **돌아가서 계속 돌면 안 된다**: 태스크 이름(`pcTaskName` — 범인 자신의 TCB 는 스택 위에 있어 보통 무사), PSP, `pxTopOfStack` 을 `.noinit` RAM 이나 백업 레지스터에 남기고 `taskDISABLE_INTERRUPTS(); NVIC_SystemReset();` (또는 워치독이 리셋하게 무한 루프). 다음 부팅 때 그 기록을 로그로 올린다. 꼬리질문 "훅이 넘긴 이름이 깨져 있을 수 있나?" — 범인의 TCB 는 자기 스택 위쪽이라 보통 무사하지만, 다른 태스크가 범인의 TCB 를 덮었다면 이름도 쓰레기일 수 있다. 그래서 이름보다 TCB 주소(`xTask`)를 기록한다.

</details>

<details>
<summary>Q4. 진짜로 막으려면 어떻게 하나요? MPU 가드와 PSPLIM 의 차이는?</summary>

**PSPLIM**(ARMv8-M Main Extension, Cortex-M33/M55/M85 — Arm 문서 기준): SP 를 한계 아래로 내리는 **그 명령** 에서 UsageFault(STKOF). SP 값을 비교하므로 프레임 크기와 무관하게 정확하고 쓰기가 일어나기 전이다(표의 PSPLIM 열: 전부 `oob = 0`). FreeRTOS CM33 포트는 태스크마다 PSPLIM 을 문맥에 넣어 전환 때 복원한다(portasm.c:475-476, 511-512). **MPU 가드**(v7-M Cortex-M4/M7): 각 태스크 스택 바닥에 접근 금지 영역(문서 기준 최소 32 B, 2의 거듭제곱 크기·정렬)을 두고 전환 때 다시 설정 → 가드를 **건드려야** MemManage. 그래서 가드보다 큰 프레임이 가드를 건너뛰면 못 잡고(연습 2), 컴파일러 프로브나 "가드 ≥ 최대 프레임" 규칙이 필요하다. 둘 다 없는 칩이면 방법 2 + 넉넉한 스택 + 정적 분석.

</details>

<details>
<summary>Q5. 왜 FreeRTOS 는 스택을 TCB 보다 먼저 할당하나요?</summary>

아래로 자라는 스택이면 "스택 → TCB" 순서로 할당해 TCB 가 스택보다 높은 주소에 오게 한다 — 넘친 스택이 **자기** TCB 를 덮지 않게 (tasks.c:1640-1642, 1681-1689). 위로 자라면 반대 순서. 덕분에 훅이 받는 `pcTaskName` 과 `pxStack` 은 보통 멀쩡하다. 하지만 heap 은 블록을 차례로 놓으므로 넘친 스택 바로 아래는 **먼저 만든 태스크의 TCB** 다(Part C: comms → ctrl). 그래서 깨진 TCB 의 주인은 피해자이고, 범인은 heap 에서 그 TCB 바로 위 블록의 스택이다(heap_4 라면 사이에 블록 헤더가 있다).

</details>

<details>
<summary>Q6. ISR 이 쓰는 스택은요? configCHECK_FOR_STACK_OVERFLOW 가 그것도 봐 주나요?</summary>

아니다. 그 검사는 **태스크 스택** 만 본다. Cortex-M 에서 스레드 모드 태스크는 PSP, 예외/ISR 은 MSP 를 쓴다: 인터럽트가 들어오면 첫 예외 프레임(8 워드, FPU 면 더)만 현재 태스크의 PSP 에 쌓이고 핸들러 본체와 중첩 ISR 은 MSP 에서 돈다(RTOS-M08). 그래서 각 태스크 스택 예산에는 "예외 프레임 1개" 를 더하고(S5 의 문맥 push 가 이 경우), MSP 크기는 따로 최악 ISR 중첩으로 잡는다. MSP 오버플로 방어는 v8-M 의 MSPLIM, v7-M 이면 MPU 영역이나 링커 스크립트에서 MSP 를 RAM 맨 아래에 둬 넘치면 존재하지 않는 주소에 닿게 하는 배치(흔히 쓰는 방법 — 칩별 메모리 맵 확인 필요).

</details>

<details>
<summary>Q7. 컴파일러의 -fstack-protector 카나리와 RTOS 스택 카나리는 같은 건가요?</summary>

다르다. 컴파일러 카나리는 **한 함수 프레임 안에서** 지역 배열과 저장된 lr 사이에 랜덤 값을 두고 함수 **반환 직전** 에 비교한다 — 배열을 **위로** 넘쳐 쓰는 버퍼 오버런(보안)을 잡는다. thumbv7em 에 `-fstack-protector-strong` 을 주면 `__stack_chk_guard` 를 읽어 `str r0, [sp, #388]` — 384 B 배열 바로 위에 저장하는 것을 실제로 봤다(이 Mac 의 arm64 기본 빌드는 이미 `___stack_chk_guard` 를 넣는다). RTOS 패턴은 **스택 영역 전체의 바닥** 에 고정 값을 두고 **전환 때** 비교 — 스택이 **아래로** 고갈되는 것을 잡는다. 하나는 프레임 내부의 오버런, 하나는 스택 전체의 고갈. 둘 다 켜도 서로를 대신하지 못한다.

</details>

<details>
<summary>Q8. 스택 크기는 그럼 어떻게 정하나요? HWM 만 보면 되나요?</summary>

세 가지를 같이 쓴다. ① 정적 분석: 함수별 프레임(`-fstack-usage`) + 호출 그래프 최악 경로 + 예외 프레임 1개(+FPU) (RTOS-M08). 재귀·함수 포인터가 있으면 분석이 끊긴다. ② 타깃 보드에서 최악 시나리오를 돌린 뒤 `uxTaskGetStackHighWaterMark`(**워드** 단위). 단 패턴으로 재므로 건너뛴 영역은 "안 쓴 것" 으로 보인다(연습 3: 348 B 여유로 보고, 실제론 144 B 넘침) — 테스트가 그 경로를 안 탔어도 마찬가지. ③ 여유(보통 20~30 % 또는 수백 B)와 방법 2/MPU/PSPLIM 런타임 검사. 시뮬레이터·POSIX 포트의 HWM 은 의미 없다(데모 2).

</details>

## 9. 추가 연습

### 연습 1 — 위로 자라는 스택의 방법 2

`portSTACK_GROWTH > 0` 인 포트에서는 방법 2 가 어떻게 다른가? `stack_macros.h:123-145` 를 읽고 `check_method2_up(mem, end, top)` 을 작성하라(`end` = `pxEndOfStack` = 마지막 워드의 주소, tasks.c:1869).

<details>
<summary>답 보기</summary>

```c
static bool check_method2_up(const uint8_t *mem, uint32_t end, uint32_t top) {
    if (top >= end - LIMIT_PADDING) return true;                       /* stack_macros.h:137 */
    for (uint32_t i = end - 20; i < end; i++) if (mem[i] != FILL_BYTE) return true;   /* 135-138 */
    return false;
}
```

차이 세 가지: 보는 양이 **20 바이트**(`ucExpectedStackBytes[]` 가 `tskSTACK_FILL_BYTE` 20 개, 129-133), 위치가 `pxEndOfStack − 20 .. pxEndOfStack − 1`, 비교가 `memcmp` 다. 코드대로라면 `pxEndOfStack` 이 가리키는 **마지막 워드 자체는 비교 범위 밖** 이다 — 테스트가 이것을 확인한다(`up[253] = 0` 은 안 잡힘, `up[232]` 는 잡힘, `up[231]` 은 안 잡힘). 왜 16 이 아니라 20 인지는 소스에 설명이 없다(커밋 이력은 확인 안 함).

</details>

### 연습 2 — MPU 가드 ± 프로브 vs 방법 2 vs PSPLIM

practice.c 의 커널에 두 옵션을 더했다: `mpu_guard`(현재 태스크 스택 바닥 32 B 접근 금지, 전환 때 재설정) 와 `probe`(16 B 보다 큰 프레임은 SP 를 내리기 전에 위에서 아래로 16 B 마다 1 바이트 **읽기**). S4 를 누가 잡나?

<details>
<summary>답 보기</summary>

```text
ex2 S4 method 2                 fault=HardFault-ctx hooks=0 oob=7
ex2 S4 method 2 + probe(read)   fault=HardFault-ctx hooks=0 oob=7
ex2 S4 MPU guard 32 B           fault=HardFault-ctx hooks=0 oob=7
ex2 S4 MPU guard + probe        fault=MemManage     hooks=0 oob=0
ex2 S4 PSPLIM                   fault=STKOF         hooks=0 oob=0
```

- 읽기 프로브는 `a5` 패턴을 바꾸지 않으니 방법 2 는 그대로 놓친다.
- MPU 가드만으로도 놓친다: 쓰기가 가드 **아래** (V 의 메모리, 접근 가능)에 떨어진다 — 가드 건너뛰기.
- MPU 가드 + 프로브: 프로브 읽기가 가드에 닿아 MemManage, 쓰기 0 B. 데모 1 의 `___chkstk_darwin` + 가드 페이지 조합과 같다.
- PSPLIM 은 프로브 없이도 잡는다 (SP 값 비교).

함정 둘: S3(전부 쓰는 배열)은 MPU 가드만으로도 잡히지만 `oob = 144` — `buf[0]` 부터 채우니 가드에 닿기 전에 이웃 144 B 를 먼저 썼다. S5(문맥 push)는 MemManage 이지만 `oob = 12` — 모델이 push 를 낮은 주소부터 쓰기 때문이고, 실제 예외 진입 스태킹 순서·동작은 아키텍처 문서로 확인해야 한다(미검증).

</details>

### 연습 3 — HWM 도 속는다

`stack_hwm_bytes(k, t)` 를 작성하라: `lo` 부터 위로 `0xA5` 가 몇 바이트 이어지나 (`prvTaskCheckFreeStackSpace`, tasks.c:6467-6482 — FreeRTOS 는 이것을 `sizeof(StackType_t)` 로 나눠 **워드** 로 돌려준다). S1, S3, S4 에서 그림자의 `min_sp − lo` 와 비교하라.

<details>
<summary>답 보기</summary>

| 시나리오 | HWM (패턴) | 진실 `min_sp − lo` |
|---|---|---|
| S1 정상 | 156 B | 156 B — 정확 |
| S3 전부 기록 | 0 B | −144 B — "다 썼다" 는 맞게 보임 |
| S4 바닥만 기록 | **348 B 여유** | **−144 B (넘침)** |

S4 에서 O 의 스택 안 가장 낮은 비-`a5` 바이트는 반환 뒤 전환 때 push 한 문맥(`0x200003bc`)이라 HWM 은 348 B 여유를 보고한다. 건너뛴 프레임의 가운데는 끝까지 `a5` 다. HWM 은 "쓴 바닥" 을 재지 "SP 가 내려간 바닥" 을 재지 않는다.

</details>

### 연습 4 — 깨진 TCB 의 범인 찾기

Part C 에서 `tcb_valid` 가 ctrl 의 TCB 손상을 보고했다. heap 배치만 보고 용의자를 돌려주는 `suspect_for_tcb(k, victim, heap_hdr)` 를 작성하라(`heap_hdr` = 블록 사이 헤더 바이트; heap_4 는 `BlockLink_t`).

<details>
<summary>답 보기</summary>

```c
static int suspect_for_tcb(const kernel_t *k, int victim, int32_t heap_hdr) {
    int32_t next = k->t[victim].tcb + TCB_BYTES + heap_hdr;      /* 그 TCB 바로 위 블록 */
    for (int i = 0; i < k->nt; i++) if (k->t[i].lo == next) return i;
    return -1;
}
```

ctrl → comms (정답), imu(맨 위 블록) → 없음, 헤더 8 B 를 가정하면 이 모델 배치와 안 맞아 → 없음. 실제 보드에서는 `pxStack` 과 TCB 주소를 `uxTaskGetSystemState`(`pxStackBase`)나 디버거로 뽑아 "깨진 주소 바로 위에 시작하는 스택" 을 찾는다. 블록 사이 헤더(heap_4 `BlockLink_t`)도 같이 깨져 있으면 다음 `vPortFree` 에서 assert/HardFault 가 날 수 있다 — 또 다른 "엉뚱한 곳에서 터지는" 증상(RTOS-M10).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 23/23 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 23/23, 보고 없음
```

## 10. 핵심 정리

- 방법 1 = 전환 때 저장된 SP ≤ `pxStack` (+padding)? 방법 2 = 방법 1 + 스택 맨 아래 **16 B** 가 `0xa5` 인가 (위로 자라면 20 B). 검사는 `vTaskSwitchContext` 에서 **나가는 태스크** 만.
- 둘 다 **사후·전환 시점** 탐지: 훅이 울릴 땐 이웃 TCB/문맥이 이미 깨졌다. 훅에서는 기록하고 리셋 — 계속 돌지 않는다.
- 놓치는 모양: 큰 프레임을 바닥만 쓰고 전환 전 반환(가드 건너뛰기), 깊은 곳에서 전환이 없음(타이밍), 전환이 없는 태스크. 무작위 300세트 중 19세트를 둘 다 놓쳤다.
- 고장 나는 쪽은 **이웃**(피해자). heap 은 "스택→TCB" 순서라 범인은 깨진 TCB 바로 위 블록의 스택.
- 진짜 방어: PSPLIM(v8-M, 넘치는 명령에서 STKOF, 쓰기 0) > MPU 가드(v7-M, 가드를 건드려야 함 → 큰 프레임엔 프로브 필요) > 패턴 검사. 호스트 가드 페이지 + `___chkstk_darwin` 이 같은 구조다.
- MSP(ISR 스택)는 이 검사 대상이 아니다. 태스크 예산엔 예외 프레임 1개를 더한다 (RTOS-M08).
- POSIX 포트에선 훅이 절대 안 울린다(코드는 pthread 스택, `pxTopOfStack` 고정, HWM = 깊이 − 5). 스택 검증은 타깃에서.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 — include/stack_macros.h (머리 주석 32-44, padding 52-54, MPU 주석 56-66, 방법 1 67-79, 방법 2 101-118, 위로 자라는 스택 123-145); tasks.c (`tskSTACK_FILL_BYTE` 121, 칠하기 조건 129-135, TCB 379·392, 할당 순서 1640-1705, `memset` 1832-1837, `pxEndOfStack` 1845-1870, PSPLIM 경로 1982-1986, 검사 호출 5259, HWM 6467-6482); include/task.h (훅 2006-2020); portable/GCC/ARM_CM4F/port.c (PendSV 504-521); portable/GCC/ARM_CM33/non_secure/ port.c (74-81, 1699), portasm.c (475-476, 511-512), portmacrocommon.h:117; portable/ThirdParty/GCC/Posix/port.c (226-227) — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 — Stack Usage and Stack Overflow Checking — <https://www.freertos.org/Stacks-and-stack-overflow-checking.html>
- Arm, *Armv8-M Architecture Reference Manual* — stack limit registers (MSPLIM/PSPLIM), UsageFault STKOF; *Armv7-M Architecture Reference Manual* — MPU (PMSAv7) — <https://developer.arm.com/documentation/ddi0553/latest>
- Qualys, "The Stack Clash" (2017) — 가드 페이지를 건너뛰는 큰 스택 할당과 컴파일러 프로브 — <https://www.qualys.com/2017/06/19/stack-clash/stack-clash.txt>
- Zephyr 문서 — Thread stack protection (`CONFIG_HW_STACK_PROTECTION`, `CONFIG_MPU_STACK_GUARD`, `CONFIG_STACK_SENTINEL`, `CONFIG_INIT_STACKS`) — <https://docs.zephyrproject.org/latest/kernel/services/threads/index.html> (본문의 Zephyr 내용은 문서 기준, 미검증)
- CMSIS-RTOS2 / RTX5 문서 — `osThreadGetStackSpace`, RTX 설정 `OS_STACK_CHECK`, `osRtxErrorNotify` — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/index.html> (문서 기준, 미검증)
- 이 Mac 에서 본 것: Apple clang 21.0.0, `xcrun llvm-objdump -d`, lldb `disassemble -n ___chkstk_darwin` / `__chkstk_darwin_probe`, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -S`
- 관련 레슨: RTOS-B03 (ucontext, 칠하기·HWM·카나리 첫 구현), RTOS-B07 (POSIX 포트 HWM = 깊이 − 5), RTOS-M08 (스택 예산, 예외 프레임, ISR 은 MSP), RTOS-M10 (heap_4 블록 헤더), RTOS-M14 (재진입 — 큰 버퍼를 static 으로 옮길 때), C-M14 (ISR 공유 데이터)
