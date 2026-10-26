# C9. idle 태스크와 훅 — 커널이 비워 둔 콘센트로 CPU·스택·heap을 들여다보기

> **이 노트를 다 읽으면**: idle 태스크가 매 바퀴 하는 일(삭제된 태스크 회수, 우선순위 0 양보, idle hook, tickless 진입)을 `prvIdleTask()` 소스로 설명하고 "idle은 왜 절대 블록하면 안 되나"를 실제 고장 모습으로 보여 줄 수 있다 · 여섯 가지 훅(idle, tick, malloc-failed, stack-overflow, daemon startup, configASSERT)과 trace 매크로가 **어느 문맥에서** 불리고 그 안에서 무엇을 하면 안 되는지 안다 · run-time stats의 동작 원리와 카운터 해상도가 만드는 오차를 측정으로 보여 주고, 태스크별 CPU%·스택 HWM·heap을 찍는 "top" 모니터를 FreeRTOS와 Zephyr에서 만든다 · `vTaskDelete`가 회수하지 **않는** 것(malloc 버퍼, 쥔 mutex)을 누수·use-after-free 데모로 설명한다
> **왜 필요한가**: 양산 펌웨어의 "운영 품질"은 거의 다 이 노트의 장치로 만든다 — 현장 장비의 CPU 사용률 보고, 스택 넘침·heap 고갈을 잡는 훅, 크래시 로그를 남기는 assert. 면접에서는 "How do you measure CPU load on an RTOS?", "What does the idle task do?", "How does FreeRTOS detect stack overflow and when does it miss?"가 단골이다.
> **Don 기준 난이도**: SSD 펌웨어의 "할 일 없을 때 도는 루프", assert 매크로, 성능 카운터는 이미 안다 / FreeRTOS idle의 숨은 책임(메모리 회수), 훅마다 다른 실행 문맥(ISR vs 태스크 vs PendSV), run-time 카운터의 샘플링 오차, 스택 검사 방식 1·2가 각각 놓치는 경우, 삭제가 자원을 정리해 주지 않는다는 사실을 새로 배운다
> **선행 노트**: A4(태스크 모델 — 자기 삭제 태스크가 idle을 기다리는 장면, overflow 훅 기본 데모, `vTaskListTasks` 출력), C1(ready list · CLZ), C2(PendSV 컨텍스트 스위치), C5(tick). 이어지는 노트: C6(tickless — idle이 tick을 끄는 경로), F3(스택 크기 산정 · MPU 가드 · PSPLIM), F2(heap 구현 비교), D3(우선순위 상속), M(디버깅 · 크래시 로그)

---

## 0. 큰 그림 — 훅은 "커널이 비워 둔 콘센트"

Don이 SSD 컨트롤러 펌웨어에서 쓰던 main 루프를 떠올려 보자. job 테이블을 다 돌았는데 할 일이 없으면 보통 이런 분기가 있었다: "idle 카운터 하나 올리고, 필요하면 WFI로 코어를 재운다." 그리고 펌웨어 곳곳에 `ASSERT()`가 있어서, 말이 안 되는 상태가 되면 레지스터와 위치를 로그 영역에 남기고 멈췄다. heap이 따로 없어도 "버퍼 풀이 비면 어디로 가나"를 정해 둔 코드가 있었다.

RTOS 커널도 똑같은 자리가 필요하다. 다만 이 자리는 **커널 코드 안**에 있어서 사용자가 직접 고칠 수 없으니, 커널이 미리 "여기서 당신 함수를 불러 주겠다"는 콘센트를 뚫어 두었다. 그게 **훅(hook)** 이다. 정의하면: **훅 = 커널의 정해진 지점에서, 정해진 문맥으로 불러 주는 사용자 함수(또는 매크로)**. FreeRTOS에서는 `FreeRTOSConfig.h`의 `configUSE_xxx_HOOK = 1`로 콘센트를 열고, 정해진 이름의 함수(`vApplicationIdleHook` 등)를 사용자가 정의한다. 매크로 형태의 훅(`traceTASK_SWITCHED_IN()`, `configASSERT()`)은 정의만 하면 그 자리에 코드가 붙는다.

훅을 쓸 때 가장 중요한 질문은 **"이 함수는 누구의 문맥에서 불리나"** 다. 어떤 훅은 idle 태스크 안에서, 어떤 훅은 SysTick ISR 안에서, 어떤 훅은 인터럽트가 막힌 PendSV 안에서 불린다. 문맥에 따라 할 수 있는 일(블로킹 API, FromISR API, printf)이 완전히 달라진다.

```svg
<svg viewBox="0 0 680 384" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle">훅은 어느 문맥에서 불리나 — FreeRTOS V11.3.2</text>
<rect x="10" y="38" width="140" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
<text x="80" y="65" font-size="13" text-anchor="middle" font-weight="bold">태스크 문맥</text>
<text x="80" y="84" font-size="12" text-anchor="middle">(호출한 태스크)</text>
<rect x="162" y="38" width="250" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="287" y="64" font-size="12" text-anchor="middle">vApplicationMallocFailedHook</text>
<text x="287" y="84" font-size="12" text-anchor="middle">pvPortMalloc 실패 직후</text>
<rect x="420" y="38" width="250" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="545" y="64" font-size="12" text-anchor="middle">vApplicationDaemonTaskStartupHook</text>
<text x="545" y="84" font-size="12" text-anchor="middle">timer 태스크 첫 실행 때 1번</text>
<rect x="10" y="112" width="140" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
<text x="80" y="139" font-size="13" text-anchor="middle" font-weight="bold">idle 태스크</text>
<text x="80" y="158" font-size="12" text-anchor="middle">(우선순위 0)</text>
<rect x="162" y="112" width="250" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="287" y="138" font-size="12" text-anchor="middle">vApplicationIdleHook</text>
<text x="287" y="158" font-size="12" text-anchor="middle">idle 루프 한 바퀴마다</text>
<rect x="420" y="112" width="250" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="545" y="138" font-size="12" text-anchor="middle">portSUPPRESS_TICKS_AND_SLEEP</text>
<text x="545" y="158" font-size="12" text-anchor="middle">tickless일 때 (C6)</text>
<rect x="10" y="186" width="140" height="62" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="80" y="213" font-size="13" text-anchor="middle" font-weight="bold">SysTick ISR</text>
<text x="80" y="232" font-size="12" text-anchor="middle">(tick 인터럽트)</text>
<rect x="162" y="186" width="250" height="62" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="287" y="212" font-size="12" text-anchor="middle">vApplicationTickHook</text>
<text x="287" y="232" font-size="12" text-anchor="middle">tick마다, 스케줄러 정지 중에도</text>
<rect x="420" y="186" width="250" height="62" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="545" y="212" font-size="12" text-anchor="middle">traceTASK_INCREMENT_TICK</text>
<text x="545" y="232" font-size="12" text-anchor="middle">trace 매크로</text>
<rect x="10" y="260" width="140" height="62" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/>
<text x="80" y="287" font-size="13" text-anchor="middle" font-weight="bold">PendSV</text>
<text x="80" y="306" font-size="12" text-anchor="middle">(컨텍스트 스위치)</text>
<rect x="162" y="260" width="250" height="62" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="287" y="286" font-size="12" text-anchor="middle">vApplicationStackOverflowHook</text>
<text x="287" y="306" font-size="12" text-anchor="middle">나가는 태스크를 검사</text>
<rect x="420" y="260" width="250" height="62" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="545" y="286" font-size="12" text-anchor="middle">traceTASK_SWITCHED_OUT / IN</text>
<text x="545" y="306" font-size="12" text-anchor="middle">run-time 카운터 누적과 같은 자리</text>
<rect x="10" y="338" width="660" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="360" font-size="13" text-anchor="middle">configASSERT(x) — 커널 어디서나(태스크 · ISR · PendSV) 불릴 수 있는 마지막 훅</text>
</svg>
```

그림 1 — FreeRTOS 훅 지도. 같은 "사용자 함수"라도 불리는 문맥이 넷으로 갈린다: 태스크(malloc-failed, daemon startup), idle 태스크(idle hook), SysTick ISR(tick hook), PendSV(stack-overflow hook과 switch trace). `configASSERT`는 어디서나 불린다.

그리고 이 모든 훅의 바닥에 **idle 태스크**가 있다. idle은 "할 일 없을 때 도는 루프"이면서, 동시에 FreeRTOS에서는 **자기 삭제한 태스크의 메모리를 치우는 청소부**이고, idle hook과 tickless(저전력) 진입의 출발점이다. idle이 굶으면 이 셋이 다 멈춘다. A4 §3에서 우선순위 1 무한 루프 때문에 idle이 굶어 Deleted 태스크 메모리가 회수되지 않는 장면을 봤다. 이 노트는 거기서 한 걸음 더 들어가 idle 루프 자체, 훅 하나하나, 그리고 그 훅으로 만드는 운영 도구(CPU 미터, top 모니터, 누수 추적기)를 다룬다.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | idle 태스크의 일: `prvIdleTask` 한 바퀴, `configIDLE_SHOULD_YIELD`, idle이 블록하면 생기는 일 | 예제 1, 2 |
| §2 | idle hook으로 CPU 사용률 재기 — 보정(calibration) 방식, WFI와의 충돌, ISR 시간 | 예제 3, 4 |
| §3 | tick hook — ISR 문맥의 규칙, 스케줄러 정지 중 호출, FromISR로 깨우기 | 예제 5 |
| §4 | malloc-failed hook — 단편화로 "남았는데 실패" | 예제 6 |
| §5 | stack-overflow hook — 방식 1 vs 2, 둘 다 놓치는 넘침 | 예제 7 |
| §6 | daemon startup hook과 configASSERT 훅 | 예제 8 |
| §7 | trace 매크로를 훅으로 — 스위치 횟수 · 최장 실행 구간 | 예제 9 |
| §8 | run-time stats 해부 — 카운터 해상도와 오차 | 예제 10 |
| §9 | "top" 모니터 태스크 — CPU% · HWM · heap을 주기적으로 | 예제 11 |
| §10 | 태스크 삭제와 자원 회수 — 누수와 use-after-free | 예제 12 |
| §11 | Zephyr: idle 스레드, runtime stats, top, stack sentinel vs MPU guard | 예제 13, 14 |
| §12 | RTOS 커널 안: 훅이 끼어드는 소스 위치 | — |

소스 파일 이름: 예제 1 = `ex1.c`, 2 = `ex1b.c`, 3 = `ex2.c`, 4 = `ex2c.c`, 5 = `ex3.c`, 6 = `ex4.c`, 7 = `ex5.c`, 8 = `ex6.c`, 9 = `ex7.c`, 10 = `ex9.c`, 11 = `ex10.c`, 12 = `ex8.c`, 13 = `ztop/`, 14 = `zstk2/`, 차트 = `py/charts.py` (모두 `~/.rtos_scratch/C9/` 아래, 빌드 산출물은 VM `/var/tmp/rtos/C9/`).

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩 `/var/rtoslab`: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위의 **FreeRTOS V11.3.2**(ARM_CM3 포트, heap_4, 템플릿 기본값: 1 kHz tick, 우선순위 8단계, `configCHECK_FOR_STACK_OVERFLOW 2`, `configUSE_MALLOC_FAILED_HOOK 1`, run-time stats = `board_now_us()`)와 **Zephyr v4.4.2**(같은 보드 `mps2/an385`, 이 노트의 Zephyr 앱은 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`으로 맞췄다). 시간이 나오는 예제는 전부 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자)로 돌렸고, Zephyr도 같은 `-icount shift=5`로 직접 QEMU를 띄웠다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스를 모델링하지 않는다). 모든 예제는 `-Wall -Wextra` 경고 0개다.

훅을 켜고 끄는 스위치를 예제마다 바꿔야 해서, 템플릿 설정을 include하고 `-D` 플래그로 몇 가지만 덮어쓰는 설정 파일 하나를 `~/.rtos_scratch/C9/`에 두었다(앱 폴더가 include 경로 맨 앞이라 이게 먼저 잡힌다). 템플릿은 `configUSE_IDLE_HOOK`·`configUSE_TICK_HOOK`만 `#ifndef`로 감싸 두었고, 나머지(`configIDLE_SHOULD_YIELD`, `configCHECK_FOR_STACK_OVERFLOW`, run-time 카운터)는 무조건 정의하므로 `#undef` 후 다시 정의한다.

```c
/* C9 공용 설정: 템플릿 + 예제별 스위치(-D로 켠다) */
#ifndef C9_CONFIG_H
#define C9_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifdef C9_IDLE_YIELD                      /* configIDLE_SHOULD_YIELD 0/1 비교 */
#undef  configIDLE_SHOULD_YIELD
#define configIDLE_SHOULD_YIELD C9_IDLE_YIELD
#endif
#ifdef C9_STACK_METHOD                    /* 스택 검사 방식 1/2 비교 */
#undef  configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW C9_STACK_METHOD
#endif
#ifdef C9_RT_DIV                          /* run-time 카운터 해상도를 일부러 거칠게 */
#undef  portGET_RUN_TIME_COUNTER_VALUE
#define portGET_RUN_TIME_COUNTER_VALUE() (board_now_us() / C9_RT_DIV)
#endif
#ifdef C9_RT_TICK                         /* tick 카운트를 run-time 카운터로 (흔한 실수) */
#undef  portGET_RUN_TIME_COUNTER_VALUE
#define portGET_RUN_TIME_COUNTER_VALUE() xTaskGetTickCount()
#endif
#ifdef C9_DAEMON_HOOK
#define configUSE_DAEMON_TASK_STARTUP_HOOK 1
#endif
#ifdef C9_TRACE                           /* trace 매크로를 훅으로 */
#ifndef __ASSEMBLER__
void c9_sw_in(void *tcb);
void c9_sw_out(void *tcb);
void c9_malloc(void *p, unsigned n);
void c9_free(void *p, unsigned n);
#endif
#define traceTASK_SWITCHED_IN()  c9_sw_in((void *)pxCurrentTCB)
#define traceTASK_SWITCHED_OUT() c9_sw_out((void *)pxCurrentTCB)
#define traceMALLOC(p, n)        c9_malloc((p), (unsigned)(n))
#define traceFREE(p, n)          c9_free((p), (unsigned)(n))
#endif
#ifdef C9_ASSERT_HOOK                     /* configASSERT를 내 훅으로 */
#undef configASSERT
#ifndef __ASSEMBLER__
void c9_assert(const char *file, int line);
#endif
#define configASSERT(x) do { if (!(x)) c9_assert(__FILE__, __LINE__); } while (0)
#endif
#endif
```

```sh
#!/bin/bash
# usage: run.sh <name> [make vars...]   예: run.sh ex1 EXTRA_CFLAGS="-DC9_IDLE_YIELD=0"
N=$1; shift; TAG=${TAG:-$N}
make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C9/$N.c OUT=/var/tmp/rtos/C9/$TAG "$@" && \
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C9/$TAG/app.elf ${TMO:-20}; echo "exit=$?"
```

같은 소스를 플래그만 바꿔 여러 번 빌드하므로 `make -B`(무조건 다시 빌드)와 변형마다 다른 `TAG`(출력 폴더)를 쓴다. 예: `TAG=ex1_y0 ./run.sh ex1 EXTRA_CFLAGS='-DC9_IDLE_YIELD=0 -DconfigUSE_IDLE_HOOK=1'`.

---

## 1. idle 태스크가 하는 일

### 1.1 직관 — "아무도 안 돌 때 도는 태스크"가 꼭 있어야 하는 이유

스케줄러의 규칙은 "Ready 중 가장 높은 우선순위를 Running으로"다(C1, C4). 그런데 모든 태스크가 Blocked(센서 데이터 기다림, `vTaskDelay`)면? CPU는 무언가를 실행해야 한다 — PC가 가리킬 곳이 있어야 하니까. 그래서 커널은 `vTaskStartScheduler()` 안에서 **우선순위 0(`tskIDLE_PRIORITY`)의 태스크 하나를 반드시 만든다**. 이게 idle 태스크다. idle은 절대 블록하지 않으므로 "Ready 태스크가 최소 1개"라는 불변식(invariant)이 항상 성립하고, 스케줄러는 "고를 게 없는" 경우를 처리할 필요가 없다.

bare-metal 비유로 말하면, idle은 super-loop의 `else { idle_count++; __WFI(); }` 분기를 **별도 태스크로 떼어 낸 것**이다. 차이점은 셋이다.

- 자기 스택과 TCB가 있다(이 랩: `configMINIMAL_STACK_SIZE` 256 words = 1 KB, `uxIdleTaskStack`로 .bss에 정적 — A4 §6).
- 다른 태스크가 Ready가 되면 그 즉시 선점당한다(preemption). 그러니 idle 안의 코드는 언제든 끊길 수 있다.
- FreeRTOS에서는 **청소 업무**가 있다: 스스로를 지운 태스크(`vTaskDelete(NULL)`)는 자기 스택 위에서 자기 스택을 free할 수 없으니, idle이 대신 치운다.

### 1.2 `prvIdleTask()` 한 바퀴 — 소스로 보기

`tasks.c`의 `portTASK_FUNCTION( prvIdleTask, pvParameters )`(V11.3.2 기준 5908행 근처)를 줄여서 옮기면 이렇다.

```c
for( ; configCONTROL_INFINITE_LOOP(); )
{
    prvCheckTasksWaitingTermination();          /* ① 자기 삭제한 태스크의 TCB·스택 free */

    #if ( configUSE_PREEMPTION == 0 )
        taskYIELD();                            /* ② 협력형이면 계속 양보 */
    #endif

    #if ( ( configUSE_PREEMPTION == 1 ) && ( configIDLE_SHOULD_YIELD == 1 ) )
        if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ) ) > ( UBaseType_t ) configNUMBER_OF_CORES )
            taskYIELD();                        /* ③ 우선순위 0에 idle 말고 누가 있으면 양보 */
    #endif

    #if ( configUSE_IDLE_HOOK == 1 )
        vApplicationIdleHook();                 /* ④ 사용자 idle hook */
    #endif

    #if ( configUSE_TICKLESS_IDLE != 0 )
        /* ⑤ 예상 idle 시간 ≥ configEXPECTED_IDLE_TIME_BEFORE_SLEEP 이면
              vTaskSuspendAll() → portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime ) → xTaskResumeAll() (C6) */
    #endif
}
```

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">prvIdleTask() 한 바퀴 (tasks.c)</text>
<rect x="50" y="40" width="400" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="250" y="62" font-size="12" text-anchor="middle">prvCheckTasksWaitingTermination()</text>
<text x="250" y="81" font-size="12" text-anchor="middle">자기 삭제한 태스크의 TCB · 스택 free</text>
<line x1="250" y1="92" x2="250" y2="106" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<rect x="50" y="108" width="400" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="250" y="130" font-size="12" text-anchor="middle">configUSE_PREEMPTION == 0 이면 taskYIELD()</text>
<text x="250" y="149" font-size="12" text-anchor="middle">협력형: 다른 태스크에게 양보</text>
<line x1="250" y1="160" x2="250" y2="174" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<rect x="50" y="176" width="400" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="198" font-size="12" text-anchor="middle">configIDLE_SHOULD_YIELD == 1 이고</text>
<text x="250" y="217" font-size="12" text-anchor="middle">우선순위 0 Ready가 idle 말고 더 있으면 taskYIELD()</text>
<line x1="250" y1="228" x2="250" y2="242" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<rect x="50" y="244" width="400" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="250" y="266" font-size="12" text-anchor="middle">configUSE_IDLE_HOOK == 1 이면</text>
<text x="250" y="285" font-size="12" text-anchor="middle">vApplicationIdleHook() — 사용자 코드</text>
<line x1="250" y1="296" x2="250" y2="310" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<rect x="50" y="312" width="400" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="250" y="334" font-size="12" text-anchor="middle">configUSE_TICKLESS_IDLE != 0 이면</text>
<text x="250" y="353" font-size="12" text-anchor="middle">예상 idle ≥ 기준 → portSUPPRESS_TICKS_AND_SLEEP (C6)</text>
<path d="M50,338 L28,338 L28,66 L48,66" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="36" y="230" font-size="12" text-anchor="middle">for (;;)</text>
<rect x="470" y="50" width="200" height="52" rx="6" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/>
<text x="570" y="72" font-size="12" text-anchor="middle" font-weight="bold">블록 금지</text>
<text x="570" y="91" font-size="12" text-anchor="middle">Blocked면 Ready 태스크 0개</text>
<rect x="470" y="186" width="200" height="52" rx="6" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/>
<text x="570" y="208" font-size="12" text-anchor="middle" font-weight="bold">WFI 없음 (기본)</text>
<text x="570" y="227" font-size="12" text-anchor="middle">훅이나 tickless가 넣어야 한다</text>
<rect x="470" y="254" width="200" height="52" rx="6" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/>
<text x="570" y="276" font-size="12" text-anchor="middle" font-weight="bold">짧게</text>
<text x="570" y="295" font-size="12" text-anchor="middle">길면 0번 동료 · 회수가 밀림</text>
<text x="340" y="388" font-size="12" text-anchor="middle">configUSE_PREEMPTION=1 · IDLE_SHOULD_YIELD=1 · TICKLESS=0인 이 랩에서는 1 → 3 → 4만 돈다</text>
</svg>
```

그림 2 — idle 루프 한 바퀴. 이 랩 설정(선점형, `configIDLE_SHOULD_YIELD 1`, tickless 끔)에서는 ①→③→④만 실제로 컴파일된다. 오른쪽은 idle 코드 전체에 걸리는 세 가지 규칙.

하나씩 짚어 보자.

- **① `prvCheckTasksWaitingTermination()`**: 전역 카운터 `uxDeletedTasksWaitingCleanUp`가 0보다 크면, 크리티컬 섹션 안에서 `xTasksWaitingTermination` 리스트 머리의 TCB를 떼어 내고, 크리티컬 섹션 밖에서 `prvDeleteTCB()`로 스택과 TCB를 free한다. 카운터를 먼저 보는 이유는 소스 주석 그대로 "idle이 매 바퀴 `taskENTER_CRITICAL()`을 부르지 않게" 하기 위해서다 — 크리티컬 섹션은 인터럽트 지연을 만든다(C7).
- **② 협력형(`configUSE_PREEMPTION 0`)**: 선점이 없으면 다른 태스크가 Ready가 돼도 idle이 CPU를 놓지 않는다. 그래서 매 바퀴 `taskYIELD()`로 "누구 있나?"를 묻는다.
- **③ `configIDLE_SHOULD_YIELD`**: 사용자 태스크를 우선순위 0(idle과 같은 급)에 만들 수 있다. time slicing이 켜져 있으면 둘은 tick마다 번갈아 돈다(C4). 이 옵션이 1이면 idle은 자기 차례가 와도 할 일만 하고 **바로 양보**한다. ready list 길이가 코어 수보다 크면 "idle 말고 누가 있다"는 뜻이다. 이 읽기에는 크리티컬 섹션이 없다 — 주석 그대로 "가끔 틀린 값을 읽어도 문제없다".
- **④ idle hook**: 사용자 코드. §2에서 자세히.
- **⑤ tickless**: idle이 "다음에 깨어날 태스크까지 몇 tick 남았나"(`prvGetExpectedIdleTime()`)를 보고 길면 tick 인터럽트를 끄고 잔다. C6의 주제다. 여기서 기억할 것은 **WFI는 기본 idle 루프에 없다**는 점이다. tickless를 끄면 idle은 그냥 ①③④를 무한히 돈다 — 코어가 쉬지 않고 전력을 먹는다. 저전력을 원하면 idle hook에 `__WFI()`를 넣거나(§2.3) tickless를 켠다.

### 1.3 예제 1 — 우선순위 0 사용자 태스크와 `configIDLE_SHOULD_YIELD`

**무엇을 확인하나**: 우선순위 0에서 블로킹 없이 도는 사용자 태스크 `bg`가 있을 때, `configIDLE_SHOULD_YIELD`가 0이냐 1이냐에 따라 idle과 `bg`가 CPU를 얼마나 나눠 갖는지, idle 루프가 몇 바퀴 도는지, 그리고 같은 우선순위 0의 `oneshot`이 자기 삭제했을 때 idle이 몇 us 뒤에 메모리를 회수하는지.

```c
/* ex1: 우선순위 0 사용자 태스크 vs idle — configIDLE_SHOULD_YIELD 0/1, idle 루프 횟수, 자기 삭제 회수 시점 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static volatile uint32_t idle_iter, bg_loops;
static uint32_t t_del, t_freed;
static size_t heap_before;

void vApplicationIdleHook(void)                  /* idle 루프 한 바퀴마다 1번 불린다 */
{
    idle_iter++;
    if (t_del && !t_freed && xPortGetFreeHeapSize() > heap_before) t_freed = board_now_us();
}

static void bg(void *pv) { (void)pv; for (;;) bg_loops++; }      /* 우선순위 0 바쁜 루프 */

static void oneshot(void *pv)                    /* 일 끝내고 스스로 삭제 */
{
    (void)pv;
    heap_before = xPortGetFreeHeapSize();
    t_del = board_now_us();
    vTaskDelete(NULL);
}

static void mon(void *pv)
{
    (void)pv;
    vTaskDelay(pdMS_TO_TICKS(100));
    TaskHandle_t hi = xTaskGetIdleTaskHandle();
    TaskHandle_t hb = xTaskGetHandle("bg");
    uint32_t ti = ulTaskGetRunTimeCounter(hi), tb = ulTaskGetRunTimeCounter(hb);
    board_printf_raw("configIDLE_SHOULD_YIELD=%d\n", configIDLE_SHOULD_YIELD);
    board_printf_raw("  IDLE run=%u us (%u%%)  bg run=%u us (%u%%)\n",
                     ti, (unsigned)ulTaskGetRunTimePercent(hi), tb, (unsigned)ulTaskGetRunTimePercent(hb));
    board_printf_raw("  idle loop iterations=%u  bg loops=%u\n", idle_iter, bg_loops);
    board_printf_raw("  oneshot deleted at %u us, TCB+stack freed by idle at %u us (+%u us)\n",
                     t_del, t_freed, t_freed - t_del);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(bg, "bg", 128, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(oneshot, "oneshot", 256, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(mon, "mon", 256, NULL, 3, NULL);
    vTaskStartScheduler();
}
```

```sh
TAG=ex1_y1 ./run.sh ex1 EXTRA_CFLAGS='-DC9_IDLE_YIELD=1 -DconfigUSE_IDLE_HOOK=1'
TAG=ex1_y0 ./run.sh ex1 EXTRA_CFLAGS='-DC9_IDLE_YIELD=0 -DconfigUSE_IDLE_HOOK=1'
```

```text
configIDLE_SHOULD_YIELD=1
  IDLE run=405 us (0%)  bg run=99573 us (99%)
  idle loop iterations=98  bg loops=774386
  oneshot deleted at 1158 us, TCB+stack freed by idle at 2159 us (+1001 us)
[board_exit] code=0
exit=0
configIDLE_SHOULD_YIELD=0
  IDLE run=49995 us (49%)  bg run=49983 us (49%)
  idle loop iterations=77738  bg loops=388689
  oneshot deleted at 1158 us, TCB+stack freed by idle at 1176 us (+18 us)
[board_exit] code=0
exit=0
```

(크기 표시 줄은 생략했다.)

**출력에서 볼 것**:

- **YIELD=1**: idle은 100 ms 동안 98바퀴, 합계 405 us만 돌았다 — tick마다 time slice 차례가 와서 한 바퀴(약 4 us) 돌고 곧바로 양보한다. 나머지 99%는 `bg` 몫이다. "우선순위 0 사용자 태스크가 idle 시간을 거의 다 가져간다"가 이 옵션의 뜻이다.
- **YIELD=0**: idle과 `bg`가 정확히 반반(49%/49%). idle은 자기 1 ms slice를 꽉 채워 77738바퀴 돌았다(1바퀴 ≈ 0.64 us). `bg`가 돌 수 있었던 시간의 절반을 idle이 "할 일 없는 루프"로 날린 것이다. 대신 `bg`의 루프 횟수가 정확히 절반(388689 대 774386)이 됐다.
- **메모리 회수 시점**: YIELD=1에서는 `oneshot`이 1158 us에 자기 삭제했는데 회수는 2159 us — **다음 tick에 idle 차례가 올 때까지 1 ms 밀렸다**. YIELD=0에서는 18 us 만에 회수됐다(삭제 직후 round-robin 순서상 idle이 바로 이어 받았다). 어느 쪽이든 회수는 일어난다. 우선순위 0 태스크는 time slicing 때문에 idle을 **굶기지는 않는다**. idle을 완전히 굶기는 것은 우선순위 ≥ 1의 바쁜 루프다(A4 §3, §7).
- 그래서 실무 규칙: 바쁜 배경 작업(체크섬 스캔, 로그 압축)을 꼭 바쁜 루프로 짜야 한다면 **우선순위 0 + `configIDLE_SHOULD_YIELD 1`** 에 둔다. 우선순위 1에 두면 idle hook·메모리 회수·저전력이 전부 멈춘다.

> 주의: YIELD=1에는 부작용이 있다(FreeRTOS 문서의 `configIDLE_SHOULD_YIELD` 설명에도 나온다). idle이 slice 중간에 양보하면 다음 태스크는 새 slice를 받는 게 아니라 **idle이 쓰다 남긴 slice를 이어 쓴다**. 위 출력에서 `bg`가 tick마다 약 996 us를 받는 것이 그 모습이다. 우선순위 0 사용자 태스크가 여럿이면 idle 바로 뒤에 오는 태스크의 몫이 조금씩 줄어 slice가 고르지 않게 된다.

### 1.4 idle이 하지 않는 일

- **WFI**: 위에서 말한 대로 기본 루프에는 없다. `portSUPPRESS_TICKS_AND_SLEEP()`(tickless)만 WFI를 한다. Zephyr는 다르다: idle 스레드가 매 바퀴 `k_cpu_idle()` → Cortex-M에서 `arch_cpu_idle()`(`arch/arm/core/cortex_m/cpu_idle.c`)이 `__WFI`를 실행한다(§11).
- **정적으로 만든 태스크의 메모리 회수**: `prvDeleteTCB()`는 `ucStaticallyAllocated` 표시를 보고, `xTaskCreateStatic`으로 만든 태스크의 스택·TCB는 free하지 않는다(사용자 메모리이므로). 그 메모리를 언제 재사용해도 되는지는 사용자가 판단해야 한다 — `eTaskGetState()`가 `eDeleted`를 지나 더 이상 리스트에 없을 때.
- **남이 지운 태스크의 회수**: 다른 태스크를 지우면(`vTaskDelete(h)`, h ≠ 자기) `vTaskDelete()` 안에서 크리티컬 섹션을 나온 뒤 **그 자리에서** `prvDeleteTCB()`를 부른다(`tasks.c`의 "If the task is not deleting itself, call prvDeleteTCB from outside of critical section"). idle을 거치지 않는다. idle이 필요한 것은 자기 삭제뿐이다.

### 1.5 예제 2 — idle이 블록하면? ("절대 하면 안 되는 일"을 직접 해 보기)

FreeRTOS 문서는 "idle hook은 블로킹 API를 부르면 안 된다"고만 말한다. 실제로 부르면 무슨 일이 나는지 보자.

**무엇을 확인하나**: idle hook에서 `vTaskDelay(5)`를 부르고, 2 ms씩 자는 `worker`의 실제 지연을 tick 카운트로 확인한다.

```c
/* ex1b: idle hook 안에서 블로킹 API를 부르면? (절대 하면 안 되는 일) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

void vApplicationIdleHook(void)
{
    static int once;
    if (!once) { once = 1; board_printf_raw("[%u us] idle hook: calling vTaskDelay(5)...\n", board_now_us()); }
    vTaskDelay(5);                               /* idle이 Blocked가 되면 Ready 태스크가 0개 */
}

static void worker(void *pv)
{
    (void)pv;
    for (int i = 0; i < 3; i++) {
        board_printf_raw("[%u us] worker alive %d (tick=%u)\n", board_now_us(), i, (unsigned)xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(2));            /* worker가 자는 동안 돌 태스크는 idle뿐 */
    }
    board_printf_raw("[%u us] worker done\n", board_now_us());
    board_exit(0);
}

int main(void)
{
    xTaskCreate(worker, "worker", 256, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

```text
[117 us] worker alive 0 (tick=0)
[142 us] idle hook: calling vTaskDelay(5)...
[168 us] worker alive 1 (tick=0)
[193 us] worker alive 2 (tick=0)
[217 us] worker done
[board_exit] code=0
exit=0
```

**출력에서 볼 것**: `worker`는 `vTaskDelay(pdMS_TO_TICKS(2))`를 불렀는데 **tick이 0인 채로 25 us 만에 깨어났다**. 2 ms 지연이 통째로 무시됐다. 크래시도, assert도 없다 — 가장 나쁜 종류의 고장이다.

왜 그런지 생성 코드를 보면 바로 알 수 있다. idle이 Blocked가 되면 Ready 태스크가 0개다. `vTaskSwitchContext()`의 `taskSELECT_HIGHEST_PRIORITY_TASK()`(포트 최적화판, C1)는 ready 비트맵 `uxTopReadyPriority`에 CLZ를 한다.

```sh
arm-none-eabi-objdump -d /var/tmp/rtos/C9/ex1b/app.elf | awk '/<vTaskSwitchContext>:/,/^$/'
arm-none-eabi-nm -n /var/tmp/rtos/C9/ex1b/app.elf | grep -B3 -A2 pxReadyTasksLists
```

```text
     d8a:	fab3 f383 	clz	r3, r3                 ; r3 = uxTopReadyPriority = 0 → clz = 32
     d8e:	b2db      	uxtb	r3, r3
     d90:	f1c3 031f 	rsb	r3, r3, #31            ; 31 - 32 = -1
     d94:	481e      	ldr	r0, [pc, #120]	; r0 = &pxReadyTasksLists = 0x20000dd0
     d96:	eb03 0383 	add.w	r3, r3, r3, lsl #2     ; × 5 words (List_t = 20 B)
     d9a:	009a      	lsls	r2, r3, #2
     d9c:	5881      	ldr	r1, [r0, r2]          ; pxReadyTasksLists[-1].uxNumberOfItems
...
20000da4 b pxDelayedTaskList
20000da8 b xDelayedTaskList2
20000dbc b xDelayedTaskList1
20000dd0 b pxReadyTasksLists
```

(디스어셈블 오른쪽 `;` 주석은 내가 붙였다.) 비트맵이 0이면 `31 - clz(0) = -1`이고, `pxReadyTasksLists[-1]`은 0x20000dd0 − 20 = **0x20000dbc = `xDelayedTaskList1`** 이다. 스케줄러가 **delayed list를 ready list로 착각**하고, 거기 줄 서 있던(자고 있어야 할) `worker`를 골라 실행했다. `configASSERT( listCURRENT_LIST_LENGTH(...) > 0 )`도 그 delayed list에 항목이 있으니 통과한다. 결국 "블록된 태스크가 시간 전에 깨어나는" 조용한 고장이 됐다. 링커가 변수를 다르게 배치했다면 엉뚱한 메모리를 TCB로 읽고 HardFault가 났을 것이다 — 즉 결과는 **정의되지 않는다**.

이것이 "idle은 절대 블록하면 안 된다"의 진짜 이유다. 불변식 "Ready ≥ 1"이 깨지면 스케줄러의 O(1) 선택 코드는 방어하지 않는다(방어하면 매 스위치마다 비용이 든다). idle hook에서 금지되는 것은 그래서 `vTaskDelay`, 무한 대기 `xQueueReceive`, `xSemaphoreTake(…, portMAX_DELAY)`, 그리고 블록할 수 있는 모든 것(블로킹 드라이버 호출, 블로킹 printf 포함)이다. 타임아웃 0으로 부르는 API(`xQueueReceive(q, &v, 0)`)는 블록하지 않으므로 써도 된다.

### 1.6 idle 문맥 정리

| 질문 | FreeRTOS | Zephyr (§11) |
|---|---|---|
| 우선순위 | 0 (`tskIDLE_PRIORITY`, 가장 낮음) | `K_IDLE_PRIO` (가장 낮음, 이 보드에서 15) |
| 누가 만드나 | `vTaskStartScheduler()` → `prvCreateIdleTasks()` | 커널 초기화 (`kernel/init.c`), `idle()` 함수는 `kernel/idle.c` |
| 스택 | `configMINIMAL_STACK_SIZE` words, 정적이면 `vApplicationGetIdleTaskMemory` 또는 커널 제공 | `CONFIG_IDLE_STACK_SIZE` bytes (A4 출력: 256 B) |
| 메모리 회수 | 자기 삭제한 태스크의 TCB·스택 free | 하지 않음 — 스레드 메모리는 기본이 정적 |
| 사용자 훅 | `vApplicationIdleHook()` | 사용자 idle 훅 API 없음 — 전원 관리(`pm_system_suspend`) 경로가 대신 |
| 저전력 | tickless 켤 때만 `portSUPPRESS_TICKS_AND_SLEEP` | 매 바퀴 `k_cpu_idle()` = WFI (기본이 tickless) |
| 같은 우선순위 양보 | `configIDLE_SHOULD_YIELD` | 해당 없음 — 응용 스레드 최저는 `K_LOWEST_APPLICATION_THREAD_PRIO` = idle − 1 (`CONFIG_ASSERT`면 `Z_VALID_PRIO`로 검사) |

---
## 2. idle hook — 할 일 없을 때 부르는 사용자 코드, 그리고 CPU 사용률 미터

### 2.1 규칙 네 가지

`configUSE_IDLE_HOOK 1`로 켜고 `void vApplicationIdleHook(void)`를 정의한다. idle 루프 **한 바퀴마다 한 번** 불린다(그림 2의 ④). 규칙은 idle 자체의 규칙과 같다.

1. **블록 금지** — §1.5에서 본 대로 스케줄러가 망가진다.
2. **짧게** — 훅이 오래 걸리면 ①(메모리 회수)과 ③(우선순위 0 양보)이 그만큼 늦어진다. 선점은 언제든 되므로 높은 우선순위 태스크의 지연에는 영향이 없다.
3. **선점될 수 있다** — 훅 코드 중간에 어떤 태스크든 끼어든다. 다른 태스크와 공유하는 변수는 원자적으로 다룬다.
4. **흔한 용도**: idle 카운터(CPU 미터), `__WFI()`(저전력), 워치독 "idle 도달" 표시(낮은 우선순위까지 CPU가 돌아간다는 증거), 배경 self-test(RAM 패턴 검사를 조금씩). 무거운 배경 일은 훅보다 우선순위 0 태스크 + `configIDLE_SHOULD_YIELD 1`이 낫다(§1.3).

### 2.2 idle 카운터로 CPU 사용률 재기 — 원리

오래된(그러나 여전히 쓰이는) 방법이다. 직관은 이렇다: "아무것도 안 할 때 idle 루프가 1초에 N바퀴 돈다면, 지금 1초에 n바퀴만 돌았으니 CPU는 (1 − n/N)만큼 바빴다."

- **보정(calibration)**: 부팅 직후, 다른 태스크를 띄우기 전에 일정 시간(예: 200 ms) 동안 idle 카운터 증가량 N을 잰다.
- **측정**: 같은 길이의 창마다 증가량 n을 재서 `CPU% = 100 × (1 − n/N)`.
- 장점: 하드웨어 타이머가 필요 없다, 커널 설정을 안 바꾼다, **ISR이 먹은 시간도 자동으로 "바쁨"에 들어간다**(ISR이 idle 루프를 끊으니까).
- 단점: 보정 순간의 조건(캐시·플래시 wait state·인터럽트)이 측정 때와 달라지면 틀린다. 훅에 WFI를 넣으면 깨진다(아래 예제). 태스크별 분해가 안 된다.

### 2.3 예제 3 — 보정 기반 CPU 미터 vs run-time stats

**무엇을 확인하나**: 10 ms 주기로 duty%만큼 바쁘게 도는 `load` 태스크의 부하를 0~90%로 바꿔 가며, (a) idle hook 카운터 방식과 (b) run-time stats의 idle 시간(`ulTaskGetIdleRunTimeCounter()`)으로 계산한 CPU%를 비교한다. 같은 소스를 `-DC9_WFI`로 다시 빌드해서 idle hook에 `wfi`를 넣은 경우도 본다.

```c
/* ex2: idle hook 카운터로 CPU 사용률 재기 — 보정(calibration) 후 부하를 바꿔 가며 run-time stats와 비교 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static volatile uint32_t idle_cnt;
static volatile uint32_t duty_pct;               /* load 태스크의 목표 부하(%) */

void vApplicationIdleHook(void)
{
    idle_cnt++;
#ifdef C9_WFI
    __asm volatile("wfi");                        /* 다음 인터럽트까지 코어 정지 (저전력) */
#endif
}

static void load(void *pv)                       /* 10 ms 주기, 그중 duty% 동안 바쁘게 */
{
    (void)pv;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t t0 = board_now_us(), busy = duty_pct * 100;   /* 10 ms × duty/100 = duty × 100 us */
        while (board_now_us() - t0 < busy) { }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    }
}

static void mon(void *pv)
{
    (void)pv;
    TaskHandle_t hLoad;
    uint32_t c0 = idle_cnt;
    vTaskDelay(pdMS_TO_TICKS(200));              /* 보정: 이 200 ms 동안은 idle만 돈다 */
    uint32_t base = idle_cnt - c0;               /* 부하 0%일 때 200 ms당 idle 루프 수 */
    board_printf_raw("calibration: %u idle-hook calls / 200 ms\n", base);
    xTaskCreate(load, "load", 256, NULL, 3, &hLoad);
    board_printf_raw("duty  idle_cnt  cpu%%(idle-hook)  cpu%%(run-time stats)\n");
    for (uint32_t d = 0; d <= 90; d += 15) {
        duty_pct = d;
        vTaskDelay(pdMS_TO_TICKS(20));            /* 부하 바뀐 뒤 안정화 */
        uint32_t i0 = idle_cnt, r0 = ulTaskGetIdleRunTimeCounter(), t0 = board_now_us();
        vTaskDelay(pdMS_TO_TICKS(200));
        uint32_t di = idle_cnt - i0, dr = ulTaskGetIdleRunTimeCounter() - r0, dt = board_now_us() - t0;
        uint32_t est1 = di >= base ? 0 : 1000 - (uint32_t)((uint64_t)di * 1000 / base);  /* 0.1% 단위 */
        uint32_t est2 = 1000 - (uint32_t)((uint64_t)dr * 1000 / dt);
        board_printf_raw("%u%%  %u  %u.%u  %u.%u\n", d, di, est1 / 10, est1 % 10, est2 / 10, est2 % 10);
    }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(mon, "mon", 512, NULL, 6, NULL);
    vTaskStartScheduler();
}
```

```sh
TAG=ex2  ./run.sh ex2 EXTRA_CFLAGS='-DconfigUSE_IDLE_HOOK=1'
TAG=ex2w ./run.sh ex2 EXTRA_CFLAGS='-DconfigUSE_IDLE_HOOK=1 -DC9_WFI'
```

```text
calibration: 390009 idle-hook calls / 200 ms
duty  idle_cnt  cpu%(idle-hook)  cpu%(run-time stats)
0%  389441  0.2  0.2
15%  330922  15.2  15.1
30%  272443  30.2  30.1
45%  213895  45.2  45.1
60%  155417  60.2  60.1
75%  96883  75.2  75.1
90%  38405  90.2  90.1
[board_exit] code=0
exit=0
```

```text
calibration: 200 idle-hook calls / 200 ms
duty  idle_cnt  cpu%(idle-hook)  cpu%(run-time stats)
0%  200  0.0  0.2
15%  180  10.0  15.1
30%  140  30.0  30.1
45%  120  40.0  45.1
60%  80  60.0  60.1
75%  60  70.0  75.1
90%  20  90.0  90.1
[board_exit] code=0
exit=0
```

(첫 블록이 WFI 없음, 둘째 블록이 `-DC9_WFI`. 크기 표시 줄은 생략.)

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="20" text-anchor="middle" font-size="14">측정 CPU% − 목표 부하 (오차, %p) — ex2</text>
<line x1="70" y1="240.0" x2="620" y2="240.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="244.0" text-anchor="end" font-size="12">-6</text>
<line x1="70" y1="212.9" x2="620" y2="212.9" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="216.9" text-anchor="end" font-size="12">-5</text>
<line x1="70" y1="185.7" x2="620" y2="185.7" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="189.7" text-anchor="end" font-size="12">-4</text>
<line x1="70" y1="158.6" x2="620" y2="158.6" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="162.6" text-anchor="end" font-size="12">-3</text>
<line x1="70" y1="131.4" x2="620" y2="131.4" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="135.4" text-anchor="end" font-size="12">-2</text>
<line x1="70" y1="104.3" x2="620" y2="104.3" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="108.3" text-anchor="end" font-size="12">-1</text>
<line x1="70" y1="77.1" x2="620" y2="77.1" stroke="currentColor" stroke-opacity="0.5"/>
<text x="62" y="81.1" text-anchor="end" font-size="12">+0</text>
<line x1="70" y1="50.0" x2="620" y2="50.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="62" y="54.0" text-anchor="end" font-size="12">+1</text>
<rect x="83.3" y="71.7" width="16" height="5.4" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="101.3" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="119.3" y="77.1" width="16" height="1.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="109.3" y="258" text-anchor="middle" font-size="12">0%</text>
<rect x="161.9" y="74.4" width="16" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="179.9" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="197.9" y="77.1" width="16" height="135.7" fill="#e08a3c" fill-opacity="0.75"/>
<text x="187.9" y="258" text-anchor="middle" font-size="12">15%</text>
<rect x="240.4" y="74.4" width="16" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="258.4" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="276.4" y="77.1" width="16" height="1.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="266.4" y="258" text-anchor="middle" font-size="12">30%</text>
<rect x="319.0" y="74.4" width="16" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="337.0" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="355.0" y="77.1" width="16" height="135.7" fill="#e08a3c" fill-opacity="0.75"/>
<text x="345.0" y="258" text-anchor="middle" font-size="12">45%</text>
<rect x="397.6" y="74.4" width="16" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="415.6" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="433.6" y="77.1" width="16" height="1.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="423.6" y="258" text-anchor="middle" font-size="12">60%</text>
<rect x="476.1" y="74.4" width="16" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="494.1" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="512.1" y="77.1" width="16" height="135.7" fill="#e08a3c" fill-opacity="0.75"/>
<text x="502.1" y="258" text-anchor="middle" font-size="12">75%</text>
<rect x="554.7" y="74.4" width="16" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="572.7" y="71.7" width="16" height="5.4" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="590.7" y="77.1" width="16" height="1.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="580.7" y="258" text-anchor="middle" font-size="12">90%</text>
<text x="345" y="276" text-anchor="middle" font-size="12">load 태스크 목표 부하</text>
<rect x="80" y="276" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="98" y="286" font-size="12">run-time stats</text>
<rect x="250" y="276" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="268" y="286" font-size="12">idle hook 카운터</text>
<rect x="420" y="276" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/>
<text x="438" y="286" font-size="12">idle hook + WFI</text>
</svg>
```

그림 3 — 예제 3의 측정 오차(측정 CPU% − 목표 duty). WFI가 없으면 두 방법 모두 +0.1~0.2%p(모니터 태스크 자신의 몫)로 정확하다. idle hook에 WFI를 넣으면 카운터 방식은 tick 단위로 양자화되어 최대 −5%p까지 틀린다. run-time stats는 WFI와 무관하게 그대로다.

**출력에서 볼 것**:

- WFI가 없을 때 idle 루프는 200 ms에 39만 바퀴(1바퀴 ≈ 0.51 us) 돌았고, 두 방법 모두 목표 부하를 0.2%p 이내로 맞췄다. 0.1~0.2%p 차이는 `mon` 자신과 `load`의 스케줄링 오버헤드다. QEMU icount는 결정적이라 보정이 완벽하게 들어맞는다 — 실제 칩에서는 캐시 상태에 따라 1바퀴 시간이 흔들리므로 이만큼 정확하지 않다.
- **WFI를 넣으면 보정값이 200(=200 tick)으로 무너진다**. idle은 한 바퀴 돌고 `wfi`로 잠들었다가 다음 인터럽트(1 kHz tick)에 깨어 한 바퀴 더 돈다. 카운터는 "idle이 돈 시간"이 아니라 "idle이 깨어난 횟수"를 센다. 그래서 15% 부하가 10%로, 45%가 40%로 보인다(tick 1개 = 0.5% 단위로 양자화, 10 ms 주기와 맞물려 5%p 계단). 실무에서 **저전력(WFI·tickless)과 idle 카운터 CPU 미터는 같이 못 쓴다**. 저전력 장비는 run-time stats(또는 Zephyr `idle_cycles`)로 재야 한다.
- run-time stats는 `IDLE` 태스크가 Running이었던 **시간**을 타이머로 재므로, WFI로 자는 시간도 "idle 시간"으로 정확히 잡힌다.

### 2.4 예제 4 — ISR이 먹은 시간은 누구에게 붙나

두 방법의 결정적인 차이는 ISR 시간에서 나온다. FreeRTOS run-time stats는 **컨텍스트 스위치 때만** 시각을 읽어 "나가는 태스크"에 더한다(§8). ISR은 태스크가 아니므로, ISR이 실행된 시간은 **그때 끊긴 태스크**의 시간으로 잡힌다.

**무엇을 확인하나**: 태스크는 아무 일도 안 하고(idle만 돈다) TIMER0 인터럽트가 2 kHz로 매번 100 us씩 일할 때(≈ 20% 부하), 두 방법이 각각 몇 %로 보는지.

```c
/* ex2c: ISR이 쓰는 CPU 시간은 run-time stats에서 누구에게 붙나? (TIMER0 2 kHz, 매번 100 us = 20%) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static volatile uint32_t idle_cnt, isr_us;

void vApplicationIdleHook(void) { idle_cnt++; }

void TIMER0_IRQHandler(void)
{
    uint32_t t0 = board_now_us();
    TIMER_INTCLR(TIMER0_BASE) = 1;
    while (board_now_us() - t0 < 100) { }        /* ISR 안에서 100 us 일 */
    isr_us += board_now_us() - t0;
}

static void mon(void *pv)
{
    (void)pv;
    uint32_t c0 = idle_cnt;
    vTaskDelay(pdMS_TO_TICKS(200));
    uint32_t base = idle_cnt - c0;               /* ISR 없이 보정 */
    TIMER_RELOAD(TIMER0_BASE) = BOARD_CPU_HZ / 2000;
    TIMER_VALUE(TIMER0_BASE) = BOARD_CPU_HZ / 2000;
    NVIC_IPR(8) = 0x80;                          /* API를 안 부르는 ISR */
    NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1 | 8;             /* EN + IRQ EN */
    vTaskDelay(pdMS_TO_TICKS(20));
    uint32_t i0 = idle_cnt, r0 = ulTaskGetIdleRunTimeCounter(), t0 = board_now_us(), s0 = isr_us;
    vTaskDelay(pdMS_TO_TICKS(200));
    uint32_t di = idle_cnt - i0, dr = ulTaskGetIdleRunTimeCounter() - r0, dt = board_now_us() - t0;
    board_printf_raw("true ISR load       : %u%%\n", (isr_us - s0) * 100 / dt);
    board_printf_raw("cpu%% by idle-hook   : %u%%\n", 100 - di * 100 / base);
    board_printf_raw("cpu%% by run-time st.: %u%%  (IDLE was charged %u of %u us)\n",
                     100 - (uint32_t)((uint64_t)dr * 100 / dt), dr, dt);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(mon, "mon", 512, NULL, 6, NULL);
    vTaskStartScheduler();
}
```

```text
true ISR load       : 19%
cpu% by idle-hook   : 21%
cpu% by run-time st.: 1%  (IDLE was charged 199890 of 200000 us)
[board_exit] code=0
exit=0
```

**출력에서 볼 것**: ISR이 실제로 쓴 시간은 19%(정수 버림, 실제는 약 20%)인데 run-time stats는 **1%** 로 본다 — 200 ms 중 199890 us가 `IDLE`에 붙었다. ISR이 idle을 끊고 돌았으니 그 시간이 전부 idle 몫으로 계산된 것이다. idle hook 카운터는 21%로 맞게 본다(ISR 진입·복귀 오버헤드가 조금 더해졌다).

실무 결론:

- run-time stats의 "IDLE %"는 **"태스크들이 쓰지 않은 시간"** 이지 "CPU가 쉰 시간"이 아니다. ISR이 무거운 시스템(드론 FC의 IMU 8 kHz DMA 완료 ISR, 모터 제어 20 kHz 전류 루프 ISR)에서는 이 차이가 크다.
- ISR 시간을 따로 보고 싶으면 ISR 입구·출구에서 직접 타임스탬프를 재거나(이 예제의 `isr_us`), trace 도구(SEGGER SystemView, Percepio Tracealyzer)를 쓴다. FreeRTOS에는 `traceISR_ENTER()`/`traceISR_EXIT()` 매크로(`FreeRTOS.h`에 빈 기본값)가 있어서, 일부 포트의 SysTick 핸들러(예: `portable/ARMv8M/non_secure/port.c`)는 이미 부르고 사용자 ISR에는 직접 넣는다 — 그러면 ISR 구간을 따로 기록할 수 있다. Zephyr thread analyzer는 ISR 스택 사용량은 보여 주지만(`ISR0` 줄) ISR CPU 시간은 0으로 찍는다(A4 예제 11 출력, §11).

| | idle hook 카운터 | run-time stats (`configGENERATE_RUN_TIME_STATS`) |
|---|---|---|
| 필요한 것 | idle hook 하나, 보정 | 자유 실행 타이머 + 설정 3줄 |
| 태스크별 분해 | 안 됨 (전체만) | 됨 |
| ISR 시간 | 바쁨에 포함 (정확) | 끊긴 태스크에 붙음 (idle% 과대) |
| WFI · tickless | 깨짐 (깨어난 횟수를 셈) | 정상 |
| 실칩 정확도 | 보정 조건에 민감 (캐시, wait state) | 타이머 해상도에 민감 (§8) |
| 오버헤드 | idle 루프 1바퀴당 증가 1번 | 컨텍스트 스위치마다 타이머 읽기 1번 |

---

## 3. tick hook — SysTick ISR 안의 사용자 코드

### 3.1 어디서 불리나

`configUSE_TICK_HOOK 1` + `void vApplicationTickHook(void)`. `xTaskIncrementTick()`(SysTick 핸들러 `xPortSysTickHandler`가 부른다) 안에서 불린다. 소스를 보면 두 군데다.

- 스케줄러가 돌 때: tick 카운트를 올리고 깨울 태스크를 옮긴 뒤, `if( xPendedTicks == ( TickType_t ) 0 ) vApplicationTickHook();` — 스케줄러 재개 때 밀린 tick을 한꺼번에 처리하는 중이면 부르지 않는다(주석: "Guard against the tick hook being called when the pended tick count is being unwound").
- 스케줄러가 정지됐을 때(`vTaskSuspendAll()` 중): tick 카운트는 올리지 않고 `xPendedTicks += 1U`만 한 뒤, **그래도 훅은 부른다**(주석: "The tick hook gets called at regular intervals, even if the scheduler is locked").

즉 tick hook은 **"정확히 tick마다 한 번"** 불리는 유일한 사용자 코드다. 그리고 **ISR 문맥**이다. 규칙:

1. FromISR로 끝나는 API만 쓴다. 일반 API는 `vPortEnterCritical()`의 assert에 걸린다(아래 예제).
2. 아주 짧게 — tick ISR 길이가 그대로 모든 태스크의 지터가 된다. 1 kHz tick에서 훅이 20 us면 CPU의 2%다.
3. `pxHigherPriorityTaskWoken`은 NULL을 넘겨도 된다. FromISR API가 높은 우선순위 태스크를 깨우면 `xYieldPendings[0]`이 서고, `xTaskIncrementTick()`은 훅을 부른 **뒤에** 이 값을 보고 `xSwitchRequired = pdTRUE`를 돌려준다(V11.3.2 `tasks.c`, 훅 호출 바로 아래 `if( xYieldPendings[ 0 ] != pdFALSE )`). 그래서 tick ISR이 끝날 때 PendSV가 걸린다.

### 3.2 예제 5 — IPSR, 정지 중 호출, FromISR로 깨우기, 그리고 금지 API

**무엇을 확인하나**: 훅 안에서 읽은 IPSR(현재 예외 번호), `vTaskSuspendAll()` 5 ms 동안의 훅 호출 수와 tick 카운트, 훅이 10 tick마다 `vTaskNotifyGiveFromISR`로 깨우는 태스크의 지연(우선순위 1 바쁜 태스크가 CPU를 쥔 상태에서). 마지막으로 `-DC9_BAD`로 FromISR 아닌 `xTaskNotifyGive`를 부르게 바꿔 빌드한다.

```c
/* ex3: tick hook = SysTick ISR 안에서 도는 코드 — IPSR, 스케줄러 정지 중 호출, FromISR로 태스크 깨우기 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static volatile uint32_t hook_calls, hook_ipsr, t_give, lat_max, lat_sum, n_wake;
static TaskHandle_t hWork;

void vApplicationTickHook(void)
{
    uint32_t ipsr; __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    hook_ipsr = ipsr;
    hook_calls++;
    if (hWork && (hook_calls % 10) == 0) {       /* 10 tick마다 태스크를 깨운다 */
        t_give = board_now_us();
#ifdef C9_BAD
        xTaskNotifyGive(hWork);                  /* ISR에서 FromISR 아닌 API — 금지 */
#else
        vTaskNotifyGiveFromISR(hWork, NULL);     /* NULL이어도 tick ISR이 끝에서 switch 판단 */
#endif
    }
}

static void work(void *pv)                       /* prio 4: 깨우면 바로 돌아야 한다 */
{
    (void)pv;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t l = board_now_us() - t_give;
        lat_sum += l; n_wake++; if (l > lat_max) lat_max = l;
    }
}

static void spin(void *pv) { (void)pv; for (;;) { } }  /* prio 1: CPU를 계속 쥔 배경 부하 */

static void mon(void *pv)
{
    (void)pv;
    vTaskDelay(5);
    uint32_t c0 = hook_calls; TickType_t k0 = xTaskGetTickCount();
    vTaskSuspendAll();                           /* 스케줄러 정지: tick count는 멈추고 pended */
    uint32_t t0 = board_now_us(); while (board_now_us() - t0 < 5000) { }
    uint32_t c1 = hook_calls; TickType_t k1 = xTaskGetTickCount();
    xTaskResumeAll();
    board_printf_raw("hook IPSR=%u (15 = SysTick exception)\n", hook_ipsr);
    board_printf_raw("during 5 ms vTaskSuspendAll: hook calls +%u, tick count +%u, after resume tick=%u\n",
                     c1 - c0, (unsigned)(k1 - k0), (unsigned)(xTaskGetTickCount() - k0));
    vTaskDelay(pdMS_TO_TICKS(200));
    board_printf_raw("hook -> task wake: n=%u avg=%u us max=%u us\n", n_wake, lat_sum / n_wake, lat_max);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(work, "work", 256, NULL, 4, &hWork);
    xTaskCreate(spin, "spin", 128, NULL, 1, NULL);
    xTaskCreate(mon, "mon", 512, NULL, 5, NULL);
    vTaskStartScheduler();
}
```

```sh
TAG=ex3    ./run.sh ex3 EXTRA_CFLAGS='-DconfigUSE_TICK_HOOK=1'
TAG=ex3bad ./run.sh ex3 EXTRA_CFLAGS='-DconfigUSE_TICK_HOOK=1 -DC9_BAD'
```

```text
hook IPSR=15 (15 = SysTick exception)
during 5 ms vTaskSuspendAll: hook calls +5, tick count +0, after resume tick=5
hook -> task wake: n=20 avg=11 us max=75 us
[board_exit] code=0
exit=0

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253
exit=1
```

**출력에서 볼 것**:

- **IPSR=15**: 훅은 SysTick 예외(번호 15) 안에서 돈다. 태스크가 아니다 — `pcTaskGetName(NULL)`을 부르면 "끊긴 태스크"의 이름이 나온다.
- **정지 중에도 5번**: 스케줄러를 5 ms 정지한 동안 훅은 5번 불렸지만 tick 카운트는 그대로(+0)였고, 재개(`xTaskResumeAll`)하자 밀린 5 tick이 한꺼번에 반영됐다(tick=5). 훅으로 "정확한 1 ms 주기 일"(소프트웨어 워치독 카운트다운, 간단한 LED 깜빡임)을 할 수 있는 이유다.
- **깨우기 지연 평균 11 us**: `pxHigherPriorityTaskWoken`에 NULL을 넘겼는데도 우선순위 4 `work`가 tick ISR이 끝나자마자 우선순위 1 `spin`을 선점했다(위 3번 규칙). max 75 us는 측정 초반 `mon`(우선순위 5)이 스케줄러를 정지하고 printf하던 구간과 겹친 깨움으로 보인다(개별 지연값은 기록하지 않아 확인하지는 못했다).
- **`-DC9_BAD`**: ISR에서 `xTaskNotifyGive()`(일반 API)를 부르자 `port.c:438`의 `configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 )`에 걸렸다. `vPortEnterCritical()`이 "크리티컬 섹션 중첩 1단계에서 진입했는데 지금 예외 처리 중이면" 잡는 검사다. configASSERT를 꺼 둔 빌드라면 이 검사가 사라지고 호출이 그냥 진행된다. 일반 API는 "지금 도는 태스크(`pxCurrentTCB`) = 호출자"라고 가정하는데 ISR 안에서 `pxCurrentTCB`는 **끊긴 태스크**다. 그래서 큐가 꽉 찬 `xQueueSend(q, &v, 10)` 같은 호출은 엉뚱하게 끊긴 태스크를 Blocked 리스트로 옮겨 버린다. 결과는 "가끔 어떤 태스크가 이유 없이 10 ms 멈춘다" 같은 재현 안 되는 버그다(E1).

> tick hook과 비슷한 자리로 **software timer**(C5, D7)가 있다. timer 콜백은 ISR이 아니라 timer(daemon) 태스크 문맥이라 printf나 큐 API를 쓸 수 있지만, 주기의 정확도는 daemon 태스크의 우선순위에 달려 있다. "1 tick마다 반드시, 아주 짧게"면 tick hook, "주기적으로, 조금 길게"면 software timer다.

---

## 4. malloc-failed hook — heap이 비었을 때

### 4.1 어디서 불리나

`configUSE_MALLOC_FAILED_HOOK 1`(이 템플릿 기본) + `void vApplicationMallocFailedHook(void)`. heap_1~5의 `pvPortMalloc()`이 NULL을 돌려주기 **직전**에 부른다. heap_4 소스(`portable/MemMang/heap_4.c`)를 보면 순서가 이렇다: `vTaskSuspendAll()` → 블록 탐색 → `traceMALLOC( pvReturn, xAllocatedBlockSize )` → `( void ) xTaskResumeAll()` → `if( pvReturn == NULL ) vApplicationMallocFailedHook();`. 즉 훅은 **스케줄러가 재개된 뒤, `pvPortMalloc`을 부른 태스크의 문맥**에서 돈다. 커널 내부 할당(`xTaskCreate`의 스택·TCB, `xQueueCreate`의 저장 공간)이 실패해도 똑같이 불린다.

훅에 인자가 없다는 점이 아쉽다 — 누가 몇 바이트를 요청했는지 모른다. 실무에서는 `pcTaskGetName(NULL)`로 호출자 이름을 남기고, 요청 크기는 `traceMALLOC` 훅(§7)에서 실패(`pvReturn == NULL`)를 잡아 기록한다.

### 4.2 예제 6 — "남은 heap은 64 KB인데 2 KB를 못 받는다"

**무엇을 확인하나**: 1000 B 블록을 heap이 꽉 찰 때까지 받고, 하나 걸러 반납해서 free 합계는 큰데 구멍은 작은 상태(단편화, fragmentation)를 만든 뒤, 2000 B 요청·2 KB 스택 태스크 생성·512 B 스택 태스크 생성이 각각 어떻게 되는지. `vPortGetHeapStats()`로 가장 큰 빈 블록도 본다.

```c
/* ex4: malloc-failed hook — 단편화로 "남은 heap은 많은데" 실패하는 장면, 훅이 불리는 문맥 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static uint32_t n_fail;
void vApplicationMallocFailedHook(void)          /* pvPortMalloc이 NULL을 돌려주기 직전, 호출한 태스크 문맥 */
{
    n_fail++;
    board_printf_raw("  [MallocFailedHook #%u] caller=%s free=%u sched=%d\n", n_fail,
                     pcTaskGetName(NULL), (unsigned)xPortGetFreeHeapSize(), (int)xTaskGetSchedulerState());
}

static void show(const char *tag)
{
    HeapStats_t s; vPortGetHeapStats(&s);
    board_printf_raw("%s: free=%u largest=%u blocks=%u min_ever=%u\n", tag, (unsigned)s.xAvailableHeapSpaceInBytes,
                     (unsigned)s.xSizeOfLargestFreeBlockInBytes, (unsigned)s.xNumberOfFreeBlocks,
                     (unsigned)s.xMinimumEverFreeBytesRemaining);
}

static void dummy(void *pv) { (void)pv; for (;;) vTaskDelay(100); }

static void app(void *pv)
{
    (void)pv;
    static void *p[160];
    int n = 0;
    show("start");
    while (n < 160 && (p[n] = pvPortMalloc(1000)) != NULL) n++;   /* 꽉 찰 때까지 */
    board_printf_raw("allocated %d x 1000 B\n", n);
    for (int i = 0; i < n; i += 2) vPortFree(p[i]);                 /* 하나 걸러 반납 */
    show("after freeing every other block");
    void *big = pvPortMalloc(2000);
    board_printf_raw("pvPortMalloc(2000) -> %s\n", big ? "OK" : "NULL");
    BaseType_t r = xTaskCreate(dummy, "dummy", 512, NULL, 1, NULL);  /* 2 KB 스택 */
    board_printf_raw("xTaskCreate(2 KB stack) -> %d (errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY=%d)\n",
                     (int)r, (int)errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY);
    r = xTaskCreate(dummy, "small", 128, NULL, 1, NULL);              /* 512 B 스택은 구멍에 들어간다 */
    board_printf_raw("xTaskCreate(512 B stack) -> %d\n", (int)r);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

```text
start: free=128896 largest=128896 blocks=1 min_ever=128896
  [MallocFailedHook #1] caller=app free=880 sched=2
allocated 127 x 1000 B
after freeing every other block: free=65392 largest=1888 blocks=64 min_ever=880
  [MallocFailedHook #2] caller=app free=65392 sched=2
pvPortMalloc(2000) -> NULL
  [MallocFailedHook #3] caller=app free=65392 sched=2
xTaskCreate(2 KB stack) -> -1 (errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY=-1)
xTaskCreate(512 B stack) -> 1
[board_exit] code=0
exit=0
```

```svg
<svg viewBox="0 0 660 210" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="20" font-size="14" text-anchor="middle">ex4의 heap — 1000 B 블록 127개 중 짝수 번째만 반납한 뒤</text>
<rect x="30" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="55" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="80" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="105" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="130" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="155" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="180" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="205" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="230" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="255" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="280" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="305" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="330" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="355" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="380" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="405" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="430" y="40" width="23" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<rect x="455" y="40" width="23" height="44" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="480" y="40" width="150" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="555.0" y="67" font-size="12" text-anchor="middle">… 끝 1888 B</text>
<rect x="30" y="100" width="14" height="14" fill="#4a7bd0" fill-opacity="0.55"/>
<text x="52" y="112" font-size="12" text-anchor="start">사용 중 1008 B (헤더 8 B 포함)</text>
<rect x="300" y="100" width="14" height="14" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="322" y="112" font-size="12" text-anchor="start">빈 구멍 1008 B — 이웃이 사용 중이라 합쳐지지 않음</text>
<text x="330" y="145" font-size="13" text-anchor="middle">free 합계 65392 B · 가장 큰 빈 블록 1888 B · 빈 블록 64개</text>
<text x="330" y="168" font-size="12" text-anchor="middle">pvPortMalloc(2000) → NULL + MallocFailedHook  (남은 양이 아니라 가장 큰 구멍이 기준)</text>
<text x="330" y="190" font-size="12" text-anchor="middle">xTaskCreate(스택 2 KB) 실패 · xTaskCreate(스택 512 B) 성공</text>
</svg>
```

그림 4 — 예제 6의 heap 모양. 사용 중 블록과 빈 블록이 번갈아 있어서 heap_4가 빈 블록끼리 합치지(coalesce) 못한다. 끝의 1888 B는 마지막으로 반납한 블록(1008 B)이 원래 남아 있던 꼬리 880 B와 붙은 것이다.

**출력에서 볼 것**:

- 훅 #1은 "꽉 찰 때까지" 루프의 마지막 실패에서 불렸다. 그때 free는 880 B(1000 + 헤더 8 B가 안 들어간다). `sched=2`는 `taskSCHEDULER_RUNNING` — 스케줄러가 재개된 뒤 태스크 문맥이라는 증거다.
- 하나 걸러 반납한 뒤 **free 합계 65392 B, 가장 큰 블록 1888 B**. `pvPortMalloc(2000)`은 실패한다. 판단 기준은 남은 총량이 아니라 **가장 큰 연속 구멍**이다. `xPortGetFreeHeapSize()`만 모니터링하면 이 상태를 "여유 64 KB"로 오판한다. 모니터에는 `xSizeOfLargestFreeBlockInBytes`와 `xMinimumEverFreeBytesRemaining`(최저 수위)도 같이 찍어야 한다.
- `xTaskCreate`는 실패를 반환값 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`(-1)로도 알려 준다. 반환값을 안 보는 코드가 흔하므로 훅이 마지막 그물이다.
- 실무 정책: (1) 양산 빌드에서는 부팅 후 동적 할당을 금지하고(F1 정적 할당), (2) 훅에서는 호출자·free·largest를 noinit RAM에 남기고 리셋, (3) 개발 빌드에서는 `configASSERT(0)`으로 바로 멈춰 디버거로 호출 스택을 본다. heap 구현별 단편화 특성(heap_4 coalesce, heap_5 여러 영역)은 F2에서 다룬다.

---
## 5. stack-overflow hook — 방식 1 vs 방식 2, 그리고 둘 다 놓치는 넘침

A4 §5.5에서 방식 2가 "넘친 순간이 아니라 다음 컨텍스트 스위치 때" 잡는다는 것을 봤다. 여기서는 두 방식의 검사 조건을 소스로 정확히 보고, **각 방식이 놓치는 장면**을 직접 만든다. 스택 크기를 정하는 방법과 하드웨어 보호(MPU 가드, PSPLIM)는 F3·F4의 몫이다.

### 5.1 검사 조건 — `include/stack_macros.h`

두 방식 모두 `vTaskSwitchContext()` 안의 `taskCHECK_FOR_STACK_OVERFLOW()`에서, **나가는 태스크**(`pxCurrentTCB`가 아직 바뀌기 전)에 대해 검사한다(그림 9). 스택이 아래로 자라는 포트(Cortex-M, `portSTACK_GROWTH < 0`)의 매크로는 이렇다.

```c
/* configCHECK_FOR_STACK_OVERFLOW == 1 : 저장된 SP만 본다 */
if( pxCurrentTCB->pxTopOfStack <= pxCurrentTCB->pxStack + portSTACK_LIMIT_PADDING )
    vApplicationStackOverflowHook( ( TaskHandle_t ) pxCurrentTCB, pcOverflowTaskName );

/* configCHECK_FOR_STACK_OVERFLOW > 1 : SP + 스택 바닥 16바이트(0xa5a5a5a5 × 4) */
if( ( pxCurrentTCB->pxTopOfStack <= pxCurrentTCB->pxStack + portSTACK_LIMIT_PADDING ) ||
    ( pulStack[ 0 ] != ulCheckValue ) || ( pulStack[ 1 ] != ulCheckValue ) ||
    ( pulStack[ 2 ] != ulCheckValue ) || ( pulStack[ 3 ] != ulCheckValue ) )
    vApplicationStackOverflowHook( ( TaskHandle_t ) pxCurrentTCB, pcOverflowTaskName );
```

- **방식 1**은 "스위치 순간 SP가 스택 밖에 있나"만 본다. `pxTopOfStack`은 PendSV가 R4–R11을 저장한 직후의 PSP다(C2). 깊은 함수에서 넘쳤다가 **돌아온 뒤에** 스위치가 일어나면 SP는 이미 정상이라 못 잡는다.
- **방식 2**는 그에 더해 스택 바닥 16바이트가 생성 때 채운 `0xa5`(`tskSTACK_FILL_BYTE`) 그대로인지 본다. 지나간 넘침의 **흔적**을 본다. 하지만 바닥 16바이트를 **건너뛰고** 그 아래만 쓴 넘침은 흔적이 안 남는다.
- 두 방식 모두 MPU 포트(`portUSING_MPU_WRAPPERS == 1`)에서는 비활성이다(소스 주석: MPU 영역이 스택을 보호하니까).
- 값 3(`configCHECK_FOR_STACK_OVERFLOW 3`)은 일부 포트(예: RISC-V `port.c`의 `configISR_STACK_SIZE_WORDS`와 함께)에서 **ISR 스택**도 검사하는 확장이다. ARM_CM3 포트에는 없다.

### 5.2 예제 7 — 세 장면 × 두 방식

**무엇을 확인하나**: 512 B(128 words) 스택의 `victim`이 600 B 지역 배열을 쓰는 세 장면을 방식 1·2로 각각 빌드한다. ① 배열 전체를 쓰고 **돌아온 뒤** 블록, ② 배열을 쓴 채 **그 함수 안에서** 블록, ③ 배열의 가장 낮은 주소 64 B만 써서 **카나리를 건너뛴다**. 넘친 바이트가 떨어질 곳으로 victim 스택 바로 아래에 0x11로 채운 512 B 이웃 블록을 먼저 잡아 둔다(heap_4는 낮은 주소부터 주고, 스택이 아래로 자라는 포트에서 `xTaskCreate`는 스택을 먼저, TCB를 나중에 할당하므로 이웃 블록 → victim 스택 → victim TCB 순서가 된다). `pxStack`은 A4에서 확인한 TCB word[12]에서 읽는다.

```c
/* ex5: 스택 검사 방식 1 vs 2 — 시나리오 3개 (SCEN=1 깊이 갔다 돌아옴, 2 깊은 곳에서 블록, 3 카나리 건너뜀) */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static uint8_t *nb;                               /* victim 스택 바로 아래 이웃 블록 */
static volatile uint32_t *pxStack;

void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
    (void)t;
    board_printf_raw("  HOOK: task=%s  pxStack[0]=%x\n", name, (unsigned)pxStack[0]);
    board_exit(251);
}

static __attribute__((noinline)) void deep(void)
{
    volatile uint8_t b[600];                      /* 512 B 스택에 600 B 배열: b[0]이 가장 낮은 주소 */
#if SCEN == 3
    for (int i = 0; i < 64; i++) b[i] = 0;        /* 낮은 주소 64 B만 씀 = 스택 바닥보다 훨씬 아래 */
    board_printf_raw("  b[0..63] at %x..%x, pxStack=%x (canary %x..%x untouched)\n", (unsigned)&b[0],
                     (unsigned)&b[63], (unsigned)pxStack, (unsigned)pxStack, (unsigned)pxStack + 15);
#else
    for (unsigned i = 0; i < sizeof b; i++) b[i] = 0;
#endif
#if SCEN == 2
    vTaskDelay(1);                                /* SP가 스택 밖에 있는 채로 문맥 저장 */
    b[1] = b[0];                                  /* 배열을 살려 둬야 tail call로 프레임이 먼저 풀리지 않는다 */
#endif
}

static void victim(void *pv)
{
    (void)pv;
    pxStack = (volatile uint32_t *)((uint32_t *)xTaskGetCurrentTaskHandle())[12];   /* TCB word[12] */
    deep();
    for (;;) vTaskDelay(1);                       /* 매 tick 컨텍스트 스위치 = 검사 기회 */
}

static void judge(void *pv)
{
    (void)pv;
    vTaskDelay(20);
    int bad = 0;
    for (int i = 0; i < 512; i++) bad += nb[i] != 0x11;
    board_printf_raw("  no hook after 20 ticks; neighbor block corrupted bytes=%d, pxStack[0]=%x\n",
                     bad, (unsigned)pxStack[0]);
    board_exit(0);
}

int main(void)
{
    board_printf_raw("method=%d scenario=%d\n", configCHECK_FOR_STACK_OVERFLOW, SCEN);
    nb = pvPortMalloc(512); memset(nb, 0x11, 512);
    xTaskCreate(victim, "victim", 128, NULL, 2, NULL);
    xTaskCreate(judge, "judge", 256, NULL, 3, NULL);
    vTaskStartScheduler();
}
```

```sh
for m in 1 2; do for s in 1 2 3; do
  TAG=ex5_m${m}s$s ./run.sh ex5 EXTRA_CFLAGS="-DC9_STACK_METHOD=$m -DSCEN=$s"
done; done
```

```text
method=1 scenario=1
  no hook after 20 ticks; neighbor block corrupted bytes=96, pxStack[0]=0
method=1 scenario=2
  HOOK: task=victim  pxStack[0]=0
method=1 scenario=3
  b[0..63] at 200011a8..200011e7, pxStack=20001218 (canary 20001218..20001227 untouched)
  no hook after 20 ticks; neighbor block corrupted bytes=125, pxStack[0]=a5a5a5a5
method=2 scenario=1
  HOOK: task=victim  pxStack[0]=0
method=2 scenario=2
  HOOK: task=victim  pxStack[0]=0
method=2 scenario=3
  b[0..63] at 200011a8..200011e7, pxStack=20001218 (canary 20001218..20001227 untouched)
  no hook after 20 ticks; neighbor block corrupted bytes=125, pxStack[0]=a5a5a5a5
```

(각 실행의 크기 표시, `[board_exit]`, `exit=` 줄은 생략했다. 훅이 불린 실행은 code=251, 나머지는 code=0.)

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">스택 넘침 세 장면 — 무엇을 보고 잡나 (ex5, 스택은 아래로 자람)</text>
<text x="115" y="48" font-size="13" text-anchor="middle" font-weight="bold">① 깊이 갔다 돌아옴</text>
<rect x="40" y="60" width="110" height="150" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="95" y="120" font-size="12" text-anchor="middle">스택 512 B</text>
<text x="95" y="138" font-size="12" text-anchor="middle">(pxStack 위)</text>
<rect x="40" y="210" width="110" height="18" rx="0" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="95" y="223" font-size="12" text-anchor="middle">카나리 16 B</text>
<rect x="40" y="228" width="110" height="10" rx="0" fill="#888" fill-opacity="0.3" stroke="#888"/>
<rect x="40" y="238" width="110" height="72" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="95" y="270" font-size="12" text-anchor="middle">이웃 블록</text>
<text x="95" y="288" font-size="12" text-anchor="middle">(다른 데이터)</text>
<rect x="44" y="110" width="102" height="165" fill="#d0564a" fill-opacity="0.28"/>
<line x1="200" y1="95" x2="154" y2="95" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="204" y="99" font-size="12" text-anchor="start">SP</text>
<text x="95" y="335" font-size="12" text-anchor="middle">방식 1: 놓침</text>
<text x="95" y="355" font-size="12" text-anchor="middle">방식 2: 잡음</text>
<text x="340" y="48" font-size="13" text-anchor="middle" font-weight="bold">② 깊은 곳에서 블록</text>
<rect x="265" y="60" width="110" height="150" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="320" y="120" font-size="12" text-anchor="middle">스택 512 B</text>
<text x="320" y="138" font-size="12" text-anchor="middle">(pxStack 위)</text>
<rect x="265" y="210" width="110" height="18" rx="0" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="320" y="223" font-size="12" text-anchor="middle">카나리 16 B</text>
<rect x="265" y="228" width="110" height="10" rx="0" fill="#888" fill-opacity="0.3" stroke="#888"/>
<rect x="265" y="238" width="110" height="72" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="320" y="270" font-size="12" text-anchor="middle">이웃 블록</text>
<text x="320" y="288" font-size="12" text-anchor="middle">(다른 데이터)</text>
<rect x="269" y="110" width="102" height="165" fill="#d0564a" fill-opacity="0.28"/>
<line x1="425" y1="285" x2="379" y2="285" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="429" y="289" font-size="12" text-anchor="start">SP</text>
<text x="320" y="335" font-size="12" text-anchor="middle">방식 1: 잡음</text>
<text x="320" y="355" font-size="12" text-anchor="middle">방식 2: 잡음</text>
<text x="565" y="48" font-size="13" text-anchor="middle" font-weight="bold">③ 카나리를 건너뜀</text>
<rect x="490" y="60" width="110" height="150" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="545" y="120" font-size="12" text-anchor="middle">스택 512 B</text>
<text x="545" y="138" font-size="12" text-anchor="middle">(pxStack 위)</text>
<rect x="490" y="210" width="110" height="18" rx="0" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="545" y="223" font-size="12" text-anchor="middle">카나리 16 B</text>
<rect x="490" y="228" width="110" height="10" rx="0" fill="#888" fill-opacity="0.3" stroke="#888"/>
<rect x="490" y="238" width="110" height="72" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="545" y="270" font-size="12" text-anchor="middle">이웃 블록</text>
<text x="545" y="288" font-size="12" text-anchor="middle">(다른 데이터)</text>
<rect x="494" y="240" width="102" height="50" fill="#d0564a" fill-opacity="0.28"/>
<line x1="650" y1="95" x2="604" y2="95" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="654" y="99" font-size="12" text-anchor="start">SP</text>
<text x="545" y="335" font-size="12" text-anchor="middle">방식 1: 놓침</text>
<text x="545" y="355" font-size="12" text-anchor="middle">방식 2: 놓침</text>
<rect x="20" y="364" width="12" height="10" fill="#d0564a" fill-opacity="0.28"/>
<text x="38" y="373" font-size="12" text-anchor="start">배열이 실제로 쓴 범위</text>
<text x="250" y="373" font-size="12" text-anchor="start">SP = 컨텍스트 스위치 때 TCB에 저장된 pxTopOfStack</text>
</svg>
```

그림 5 — 예제 7의 세 장면. 빨간 띠가 배열이 실제로 쓴 범위, SP 화살표가 컨텍스트 스위치 때 TCB에 저장된 값이다. ①은 흔적(카나리)만 남고 SP는 정상, ②는 둘 다 비정상, ③은 둘 다 정상이지만 이웃 블록이 망가졌다.

**출력에서 볼 것**:

| 장면 | 방식 1 | 방식 2 | 이웃 블록 피해 |
|---|---|---|---|
| ① 깊이 갔다 돌아온 뒤 블록 | **놓침** | 잡음 (카나리 0) | 96 B |
| ② 깊은 곳에서 블록 | 잡음 (SP < pxStack) | 잡음 | 96 B + 문맥 |
| ③ 카나리를 건너뛴 넘침 | **놓침** | **놓침** | 125 B |

- ①이 방식 1과 2의 차이다. 실제 버그는 대부분 ① 모양이다 — 가끔 불리는 깊은 경로(에러 처리, 로그 포맷팅)가 넘쳤다가 돌아온다. 방식 1은 "넘친 채로 블록하는" 경우만 잡는다. 오버헤드 차이는 스위치당 워드 4개 비교뿐이니 **개발·양산 모두 방식 2**가 기본이다.
- ③에서 b[0..63]은 0x200011a8~0x200011e7, 스택 바닥(pxStack)은 0x20001218 — 쓴 범위가 카나리보다 48 B 이상 아래다. 두 방식 모두 아무 말도 안 했는데 이웃 블록 125 B가 망가졌다(64 B 배열 + `deep()`이 부른 `board_printf_raw`의 호출 프레임도 그 아래로 내려갔다). 큰 지역 버퍼를 앞부분만 쓰는 코드(`char buf[600]; snprintf(buf, 64, …)`)가 정확히 이 모양이다 — **배열의 낮은 인덱스가 가장 낮은 주소**이고, 스택에서는 그게 스택 밖이다.
- 피해자는 "이웃"이다. 여기서는 일부러 놓은 블록이지만 실전에서는 다른 태스크의 TCB(첫 워드가 `pxTopOfStack`!)나 큐 저장 공간이다. 증상은 **다른 태스크**가 엉뚱한 곳에서 HardFault — 넘친 태스크는 멀쩡하다. 그래서 진짜 방어선은 하드웨어다: MPU 가드 리전(Armv7-M, F4), PSPLIM(Armv8-M, F3). Zephyr도 같은 구도다(§11.4).
- 디버깅 일화: 처음 빌드에서 ②가 방식 1로 안 잡혔다. `objdump`로 보니 `deep()`이 `add.w sp, sp, #600`로 배열을 먼저 풀고 `b.w vTaskDelay`로 **tail call** 하고 있었다 — 블록 순간의 SP가 이미 정상이었다. `vTaskDelay` 뒤에 배열을 한 번 더 읽게 해서(`b[1] = b[0]`) 프레임을 살렸다. A4의 "memset이 사라진" 일화와 같은 교훈: 스택 실험은 생성 코드를 확인해야 한다.

### 5.3 훅 안에서 해도 되는 일

`vApplicationStackOverflowHook(TaskHandle_t xTask, char * pcTaskName)`은 **PendSV 안, 인터럽트가 `configMAX_SYSCALL_INTERRUPT_PRIORITY`까지 막힌 상태**에서 불린다. 그리고 이미 메모리가 망가졌을 수 있다(`pcTaskName`은 TCB 안에 있으니 TCB가 덮였다면 이름도 쓰레기다). 그래서:

- 커널 API를 부르지 않는다. 최소한의 정보(태스크 이름, 핸들, `pxStack`, 지금 PSP)를 noinit RAM에 복사하고 리셋하는 것이 정석이다. printf는 개발 빌드에서만.
- "잡고 계속 돌기"는 안 된다. 망가진 이웃이 누군지 모르기 때문이다.
- `pcTaskName` 대신 핸들 값(주소)도 같이 남겨라. 이름이 덮였어도 주소로 map 파일과 맞춰 볼 수 있다.

---

## 6. daemon task startup hook과 configASSERT 훅

### 6.1 `vApplicationDaemonTaskStartupHook` — 스케줄러가 시작된 뒤의 초기화

`configUSE_DAEMON_TASK_STARTUP_HOOK 1`(+ `configUSE_TIMERS 1`)이면, timer 서비스 태스크(daemon task, 이름 `Tmr Svc`)가 처음 실행될 때 `timers.c`의 `prvTimerTask()` 첫머리에서 **딱 한 번** 부른다. 소스 주석 그대로 "스케줄러가 시작된 뒤 실행되면 좋은 초기화 코드"를 넣는 자리다. 이 랩에서 timer 태스크는 우선순위 7(`configMAX_PRIORITIES - 1`)이라 스케줄러 시작 직후 가장 먼저 돈다.

main()에서 하기 곤란한 초기화가 있다: 블로킹 API를 써야 하는 드라이버 초기화(I2C 센서 설정을 세마포어로 기다리기), 스케줄러가 돌아야 의미 있는 시간 측정, "모든 태스크가 생성된 뒤" 걸어야 하는 워치독. 이걸 별도 init 태스크로 만들었다가 자기 삭제하는 패턴도 흔하지만, 그러면 1회용 태스크의 스택·TCB가 생기고 idle이 회수해야 한다. daemon startup hook은 이미 있는 태스크의 스택을 빌려 쓴다.

주의: 이 훅은 **timer 태스크의 스택**(이 랩 `configTIMER_TASK_STACK_DEPTH` 512 words)에서 돈다. 훅 안에서 무거운 일을 하면 timer 태스크 스택 크기를 그만큼 키워야 하고, 훅이 끝날 때까지 모든 software timer 콜백이 밀린다.

### 6.2 configASSERT를 훅으로 바꿔 끼우기

`configASSERT(x)`는 FreeRTOS 소스 수백 곳에 박혀 있는 매크로다. 정의하지 않으면 빈 매크로가 되어 검사 자체가 사라진다. 템플릿은 `board_printf_raw`로 파일·줄을 찍고 `board_exit(253)`한다. 실무에서는 이 매크로를 **크래시 로그 훅**으로 쓴다: 파일·줄·현재 태스크·호출 주소를 noinit RAM에 남기고 리셋, 다음 부팅에서 업로드.

### 6.3 예제 8 — daemon startup hook에서 초기화, assert 훅으로 버그 위치 잡기

**무엇을 확인하나**: main()은 아무 객체도 만들지 않고 스케줄러만 시작한다. daemon startup hook이 큐·5 ms software timer·소비자 태스크를 만든다. 소비자는 세 번 받은 뒤 **스케줄러를 정지한 채 `vTaskDelay`** 를 부르는 버그를 낸다. `-DC9_ASSERT_HOOK`으로 configASSERT를 `c9_assert()`로 바꿔, 파일·줄·태스크·호출 주소(LR)를 남긴다.

```c
/* ex6: daemon task startup hook(스케줄러 시작 뒤 초기화) + configASSERT를 "훅"으로 바꿔 끼우기 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "board.h"

static QueueHandle_t q;
static struct { const char *file; int line; const char *task; uint32_t lr; } crash;   /* 양산이면 noinit RAM */

void c9_assert(const char *file, int line)        /* FreeRTOSConfig.h: configASSERT(x) → c9_assert() */
{
    __asm volatile("cpsid i");                    /* 더 망가지기 전에 멈춘다 */
    crash.file = file; crash.line = line; crash.lr = (uint32_t)__builtin_return_address(0);
    crash.task = xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED ? "(no sched)" : pcTaskGetName(NULL);
    board_printf_raw("[ASSERT] %s:%d task=%s caller_lr=0x%x\n", crash.file, crash.line, crash.task, crash.lr);
    board_exit(253);
}

static void tmr_cb(TimerHandle_t t) { (void)t; uint32_t v = board_now_us(); xQueueSend(q, &v, 0); }

static void consumer(void *pv)
{
    (void)pv;
    uint32_t v;
    for (int i = 0; i < 3; i++) {
        xQueueReceive(q, &v, portMAX_DELAY);
        board_printf_raw("[%u us] %s: got timer stamp %u\n", board_now_us(), pcTaskGetName(NULL), v);
    }
    vTaskSuspendAll();
    board_printf_raw("[%u us] consumer: vTaskDelay() while scheduler suspended (bug)\n", board_now_us());
    vTaskDelay(1);
    for (;;) { }
}

void vApplicationDaemonTaskStartupHook(void)     /* timer(daemon) 태스크가 처음 돌 때 1번 */
{
    board_printf_raw("[%u us] DaemonTaskStartupHook in task '%s' (prio %u)\n", board_now_us(),
                     pcTaskGetName(NULL), (unsigned)uxTaskPriorityGet(NULL));
    q = xQueueCreate(4, sizeof(uint32_t));
    xTimerStart(xTimerCreate("t5ms", pdMS_TO_TICKS(5), pdTRUE, NULL, tmr_cb), 0);
    xTaskCreate(consumer, "consumer", 256, NULL, 2, NULL);
    board_printf_raw("[%u us] hook done: queue, timer, consumer created\n", board_now_us());
}

int main(void)
{
    board_printf_raw("[%u us] main: starting scheduler (no app objects yet)\n", board_now_us());
    vTaskStartScheduler();
}
```

```sh
TAG=ex6 ./run.sh ex6 EXTRA_CFLAGS='-DC9_DAEMON_HOOK -DC9_ASSERT_HOOK'
arm-none-eabi-addr2line -f -i -e /var/tmp/rtos/C9/ex6/app.elf 0xe1e
```

```text
[6 us] main: starting scheduler (no app objects yet)
[96 us] DaemonTaskStartupHook in task 'Tmr Svc' (prio 7)
[170 us] hook done: queue, timer, consumer created
[5133 us] consumer: got timer stamp 5109
[10133 us] consumer: got timer stamp 10109
[15133 us] consumer: got timer stamp 15109
[15153 us] consumer: vTaskDelay() while scheduler suspended (bug)
[ASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:2517 task=consumer caller_lr=0xe1f
[board_exit] code=253
exit=1
vTaskDelay
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:2528
```

**출력에서 볼 것**:

- 스케줄러 시작 90 us 뒤 훅이 `Tmr Svc`(우선순위 7) 안에서 돌았다. 훅이 만든 timer가 5 ms마다 큐에 타임스탬프를 넣고, 소비자가 24 us 뒤에 받는다.
- assert는 `tasks.c:2517` — `vTaskDelay()` 안의 `configASSERT( uxSchedulerSuspended == 1U )`다. `vTaskDelay`는 스스로 `vTaskSuspendAll()`을 한 뒤 "정지 카운트가 정확히 1(=내가 건 것)"인지 확인한다. 이미 정지돼 있었으니 2가 되어 걸렸다. 정지 상태에서 블록하면 스위치를 못 해서 시스템이 그 자리에 멈추므로 이 검사가 있다.
- `caller_lr` 0xe1f(Thumb 비트 1을 뗀 0xe1e)를 addr2line에 넣으면 `vTaskDelay`가 나온다. 줄 번호가 2528로 조금 어긋난 것은 `-O2` 최적화로 assert 호출 코드가 함수 끝 쪽에 합쳐졌기 때문이다. 그래서 assert 로그에는 `__LINE__`과 LR을 **둘 다** 남긴다.
- 훅에서 `cpsid i`로 인터럽트를 먼저 막았다. assert 뒤에 다른 ISR이 돌면서 상태를 더 망가뜨리거나, 로그를 찍는 도중 선점되는 것을 막는다.
- 양산 빌드에서 configASSERT를 끄는 팀이 많다(코드 크기·속도). 그러면 §3의 "ISR에서 일반 API" 검사, 이 예제의 "정지 중 블록" 검사, ready list 검사가 모두 사라진다. 적어도 `vPortValidateInterruptPriority()`(인터럽트 우선순위 오설정 검사, E2)와 같은 싼 검사는 남기는 편을 권한다 — A4 §4.4에서 assert를 끈 빌드가 얼마나 엉뚱하게 죽는지 봤다.

---

## 7. trace 매크로 — 가장 강력한 훅

### 7.1 무엇인가

`FreeRTOS.h`에는 `#ifndef traceXXX / #define traceXXX() / #endif` 형태의 빈 매크로가 사건용으로만 100개 가까이 있다(V11부터 생긴 API 진입·복귀용 `traceENTER_xxx`/`traceRETURN_xxx`까지 세면 `#ifndef trace`가 504개). 커널은 중요한 사건마다 이걸 부른다: `traceTASK_SWITCHED_IN`/`OUT`, `traceTASK_CREATE`/`DELETE`, `traceTASK_DELAY`, `traceBLOCKING_ON_QUEUE_RECEIVE`, `traceQUEUE_SEND`, `traceMALLOC`/`traceFREE`, `traceTASK_INCREMENT_TICK`, `traceTASK_PRIORITY_INHERIT`, `traceLOW_POWER_IDLE_BEGIN` … `FreeRTOSConfig.h`에서 같은 이름을 먼저 정의하면 그 코드가 커널 소스의 그 자리에 **인라인으로** 박힌다. SEGGER SystemView나 Percepio Tracealyzer 같은 상용 trace 도구도 이 매크로를 정의하는 헤더를 제공하는 방식으로 붙는다.

함수 훅과 다른 점:

- 매크로가 커널 함수 **안**(예: `tasks.c` 안)에서 펼쳐지므로 `pxCurrentTCB` 같은 커널 내부 변수를 직접 쓸 수 있다(A4 예제 3이 `traceTASK_SWITCHED_IN()`에서 `pxCurrentTCB`를 넘긴 이유).
- 문맥이 사건마다 다르다. `traceTASK_SWITCHED_IN/OUT`은 PendSV 안(인터럽트 막힘), `traceMALLOC`은 스케줄러 정지 상태의 태스크 문맥, `traceTASK_INCREMENT_TICK`은 SysTick ISR. 기본 원칙: **커널 API 금지, 짧게, 기록만**(링버퍼에 쓰고 나중에 태스크가 내보낸다 — Don이 SSD 펌웨어에서 하던 event log와 같다).
- 이 노트의 설정 파일은 `C9_TRACE`가 정의되면 `traceTASK_SWITCHED_IN/OUT`·`traceMALLOC`·`traceFREE`를 내 함수로 연결한다. 함수 선언은 `#ifndef __ASSEMBLER__`로 감쌌다 — `FreeRTOSConfig.h`가 포트 어셈블리 파일에서 include될 수도 있어서다(이 ARM_CM3 포트는 C 파일 안의 인라인 어셈블리라 필수는 아니다).

### 7.2 예제 9 — 태스크별 스위치 횟수와 최장 연속 실행 구간

run-time stats는 "총합"만 준다. 실시간 분석에서는 **한 번 CPU를 잡았을 때 최대 얼마나 오래 놓지 않았나**(최장 slice)가 더 중요할 때가 많다 — 낮은 우선순위 태스크의 최악 대기, 같은 우선순위 태스크의 지터를 결정하니까.

**무엇을 확인하나**: 우선순위 1 바쁜 `hog`, 5 ms마다 1 ms 일하는 우선순위 3 `ctl`, 100 ms 뒤 보고하는 우선순위 5 `mon`. switch-in 때 시각을 적고 switch-out 때 그 구간 길이를 태스크별로 누적·최대값 갱신. 같은 소스를 trace 없이 빌드해서 `hog` 루프 횟수로 trace 오버헤드도 본다.

```c
/* ex7: trace 매크로를 훅으로 — traceTASK_SWITCHED_IN/OUT으로 태스크별 스위치 횟수·최장 연속 실행 시간 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define NT 8
static struct { void *tcb; uint32_t n_in, total, max; } st[NT];
static volatile uint32_t hog_loops, n_malloc;
#ifdef C9_TRACE
static void *cur; static uint32_t t_in;
static int slot(void *tcb)
{
    for (int i = 0; i < NT; i++) { if (st[i].tcb == tcb) return i; if (!st[i].tcb) { st[i].tcb = tcb; return i; } }
    return NT - 1;
}
void c9_sw_in(void *tcb)  { cur = tcb; t_in = board_now_us(); st[slot(tcb)].n_in++; }   /* PendSV 안 */
void c9_sw_out(void *tcb)
{
    if (tcb != cur) return;
    uint32_t d = board_now_us() - t_in; int i = slot(tcb);
    st[i].total += d; if (d > st[i].max) st[i].max = d;
}
void c9_malloc(void *p, unsigned n) { (void)p; (void)n; n_malloc++; }
void c9_free(void *p, unsigned n) { (void)p; (void)n; }
#endif

static void hog(void *pv) { (void)pv; for (;;) hog_loops++; }

static void ctl(void *pv)                         /* 5 ms마다 1 ms 일 */
{
    (void)pv;
    TickType_t last = xTaskGetTickCount();
    for (;;) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < 1000) { } xTaskDelayUntil(&last, 5); }
}

static void mon(void *pv)
{
    (void)pv;
    vTaskDelay(pdMS_TO_TICKS(100));
    vTaskSuspendAll();                            /* 표를 찍는 동안 통계가 바뀌지 않게 */
    board_printf_raw("hog loops in 100 ms = %u, mallocs seen = %u\n", hog_loops, n_malloc);
    for (int i = 0; i < NT && st[i].tcb; i++)
        board_printf_raw("%s\tswitch-ins=%u\ttotal=%u us\tmax slice=%u us\n",
                         pcTaskGetName((TaskHandle_t)st[i].tcb), st[i].n_in, st[i].total, st[i].max);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(hog, "hog", 128, NULL, 1, NULL);
    xTaskCreate(ctl, "ctl", 256, NULL, 3, NULL);
    xTaskCreate(mon, "mon", 512, NULL, 5, NULL);
    vTaskStartScheduler();
}
```

```sh
TAG=ex7    ./run.sh ex7 EXTRA_CFLAGS='-DC9_TRACE'
TAG=ex7off ./run.sh ex7
```

```text
   text	   data	    bss	    dec	    hex	filename
  12372	    100	 135544	 148016	  24230	/var/tmp/rtos/C9/ex7/app.elf
hog loops in 100 ms = 620675, mallocs seen = 6
Tmr Svc	switch-ins=1	total=17 us	max slice=17 us
mon	switch-ins=2	total=7 us	max slice=7 us
ctl	switch-ins=20	total=20180 us	max slice=1009 us
hog	switch-ins=20	total=79665 us	max slice=3986 us
[board_exit] code=0
exit=0
   text	   data	    bss	    dec	    hex	filename
  11992	    100	 135408	 147500	  2402c	/var/tmp/rtos/C9/ex7off/app.elf
hog loops in 100 ms = 621770, mallocs seen = 0
[board_exit] code=0
exit=0
```

**출력에서 볼 것**:

- `ctl`은 20번 들어와 매번 최대 1009 us — 1 ms 일 + 스위치 오버헤드. `hog`는 20번 들어와 최장 3986 us 연속 실행 — `ctl`이 자는 4 ms 동안이다. **IDLE이 표에 없다** — 100 ms 동안 한 번도 switch-in되지 않았다(우선순위 1 바쁜 루프 → idle 굶음, A4 §7과 같은 결론을 다른 도구로 확인).
- `mallocs seen = 6`: 태스크 3개 × (스택 + TCB). `traceMALLOC`이 커널 내부 할당도 다 본다는 뜻이다 — §10의 누수 추적기가 이걸 쓴다.
- **오버헤드**: trace를 켜면 `hog` 루프가 621770 → 620675(−0.18%). 스위치가 100 ms에 약 43번뿐이라 작다. 스위치가 초당 수만 번인 시스템에서는 훅 하나의 수 us가 바로 몇 %가 된다. 코드도 380 B 늘었다(text 11992 → 12372).
- 스위치 순서상 `traceTASK_SWITCHED_OUT()`은 run-time 카운터 누적 **전**, `traceTASK_SWITCHED_IN()`은 새 태스크 선택 **후**에 불린다(§12, 그림 9). 그래서 OUT 훅의 `pxCurrentTCB`는 나가는 태스크, IN 훅의 `pxCurrentTCB`는 들어오는 태스크다.

---
## 8. run-time stats 해부 — 무엇을, 언제, 얼마나 정확히 재나

### 8.1 설정 세 줄과 커널 안의 회계

```c
#define configGENERATE_RUN_TIME_STATS           1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() /* 타이머 켜기 (이 랩은 board_init이 이미 함) */
#define portGET_RUN_TIME_COUNTER_VALUE()        board_now_us()   /* 자유 실행 카운터 읽기 */
```

`portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()`는 `vTaskStartScheduler()`가 한 번 부르고, `portGET_RUN_TIME_COUNTER_VALUE()`는 **컨텍스트 스위치마다** 부른다. 카운터 폭은 `configRUN_TIME_COUNTER_TYPE`(기본 `uint32_t`)이다. 64비트 카운터를 쓰려면 이 타입을 `uint64_t`로 바꾸고, 레지스터 두 개를 읽어야 하는 카운터면 `portALT_GET_RUN_TIME_COUNTER_VALUE(x)` 형태를 쓴다.

회계는 `vTaskSwitchContext()`에서 일어난다(V11.3.2 단일 코어 경로).

```c
ulTotalRunTime[ 0 ] = portGET_RUN_TIME_COUNTER_VALUE();
if( ulTotalRunTime[ 0 ] > ulTaskSwitchedInTime[ 0 ] )
    pxCurrentTCB->ulRunTimeCounter += ( ulTotalRunTime[ 0 ] - ulTaskSwitchedInTime[ 0 ] );   /* 나가는 태스크에 Δ */
ulTaskSwitchedInTime[ 0 ] = ulTotalRunTime[ 0 ];                                             /* 들어오는 태스크의 시작 시각 */
```

여기서 세 가지가 나온다.

1. **샘플링 지점은 스위치뿐이다.** 그래서 ISR 시간은 끊긴 태스크에 붙는다(§2.4).
2. **`>` 비교**: 카운터가 같은 값이면(해상도보다 짧게 달렸으면) 0이 더해진다. 소스 주석은 이 비교를 "의심스러운 카운터 구현에 대한 방어"라고 하고, **"카운터가 넘치기 전까지만 값이 유효하다"** 고 분명히 적어 둔다. 32비트 us 카운터는 2^32 us ≈ 71.6분에 한 바퀴 돈다. 이 랩의 `board_now_us()`는 25 MHz 다운카운터를 25로 나눈 값이라 **약 171.8초**(2^32 / 25 MHz)에 wrap한다(`board.h` 주석). wrap 순간의 Δ는 버려지고, 그 뒤로는 누적값 자체가 "총 시간"과 맞지 않게 된다 — 장시간 운영 장비는 64비트 카운터를 쓰거나, 모니터가 **창(window) 단위 델타**만 보게 만든다(§9).
3. **조회 함수**: `uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime)`가 기본이고, `vTaskGetRunTimeStatistics(buf, len)`(V11에서 `vTaskGetRunTimeStats`의 버퍼 길이 버전)는 그 결과를 사람이 읽는 표로 만든다. 단일 태스크용 `ulTaskGetRunTimeCounter(h)`/`ulTaskGetRunTimePercent(h)`, idle 전용 `ulTaskGetIdleRunTimeCounter()`/`ulTaskGetIdleRunTimePercent()`가 있다(뒤 둘은 `INCLUDE_xTaskGetIdleTaskHandle`도 필요). 이 함수들은 지금 Running인 태스크의 "아직 회계 안 된 시간"(마지막 switch-in부터 지금까지)도 더해 준다. 퍼센트 함수는 **부팅 이후 누적**이므로 "지금 부하"가 아니다.

`vTaskGetRunTimeStatistics()`와 `vTaskListTasks()`는 소스 주석이 "양산에서는 `uxTaskGetSystemState()`를 직접 불러 원시 데이터를 쓰라"고 권한다 — `snprintf()`에 의존해서 코드가 커지고 스택을 많이 먹기 때문이다. 그리고 `uxTaskGetSystemState()` 자신도 모든 태스크를 훑는 동안 `vTaskSuspendAll()`로 스케줄러를 정지한다(태스크 수에 비례하는 시간, 그 사이 tick은 pended).

### 8.2 예제 10 — 카운터 해상도를 바꾸면

**무엇을 확인하나**: 같은 작업 세트를 run-time 카운터만 바꿔 다섯 번 돌린다: 1 us(`board_now_us()`), 10 us, 100 us, 1 ms(`board_now_us()/1000`), tick 카운트(`xTaskGetTickCount()` — 인터넷 예제에 흔한 "타이머 없이 run-time stats 켜기"). 작업은 두 개다: `burst`(우선순위 3)는 **tick 직후 깨어나 300 us** 일하고 다시 1 tick 잔다. `async`(우선순위 4)는 tick과 박자가 안 맞는 1370 Hz TIMER0 인터럽트로 깨어나 150 us 일한다.

```c
/* ex9: run-time stats 정확도 vs 카운터 해상도 — 1 us / 10 us / 100 us / 1 ms / tick 카운터 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static TaskHandle_t hAsync;

void TIMER0_IRQHandler(void)                       /* 1370 Hz: tick과 박자가 안 맞는 사건 */
{
    BaseType_t w = pdFALSE;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    vTaskNotifyGiveFromISR(hAsync, &w);
    portYIELD_FROM_ISR(w);
}

static void spin_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) { } }
static void async(void *pv) { (void)pv; for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); spin_us(150); } }
static void burst(void *pv) { (void)pv; for (;;) { vTaskDelay(1); spin_us(300); } }   /* tick 직후 300 us */

static void mon(void *pv)
{
    (void)pv;
    static TaskStatus_t ts[8];
    TIMER_RELOAD(TIMER0_BASE) = BOARD_CPU_HZ / 1370; TIMER_VALUE(TIMER0_BASE) = BOARD_CPU_HZ / 1370;
    NVIC_IPR(8) = 0x80; NVIC_ISER0 = 1u << 8; TIMER_CTRL(TIMER0_BASE) = 1 | 8;
    vTaskDelay(pdMS_TO_TICKS(500));
    configRUN_TIME_COUNTER_TYPE total;
    UBaseType_t n = uxTaskGetSystemState(ts, 8, &total);
    board_printf_raw("counter units in 500 ms: %u\n", (unsigned)total);
    for (UBaseType_t i = 0; i < n; i++) {
        uint32_t pm = total ? (uint32_t)((uint64_t)ts[i].ulRunTimeCounter * 1000 / total) : 0;  /* 0.1% */
        if (pm || ts[i].ulRunTimeCounter) board_printf_raw("  %s %u.%u%%\n", ts[i].pcTaskName, pm / 10, pm % 10);
        else board_printf_raw("  %s 0.0%%\n", ts[i].pcTaskName);
    }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(async, "async", 256, NULL, 4, &hAsync);
    xTaskCreate(burst, "burst", 256, NULL, 3, NULL);
    xTaskCreate(mon, "mon", 512, NULL, 5, NULL);
    vTaskStartScheduler();
}
```

```sh
for v in "DIV=1" "DIV=10" "DIV=100" "DIV=1000" "TICK=1"; do
  k=${v%%=*}; val=${v#*=}; TAG=ex9_$k$val ./run.sh ex9 EXTRA_CFLAGS="-DC9_RT_$k=$val"
done
```

```text
== DIV=1
counter units in 500 ms: 501043
  mon 0.0%
  burst 26.0%
  IDLE 51.8%
  Tmr Svc 0.0%
  async 21.9%
== DIV=10
counter units in 500 ms: 50104
  mon 0.0%
  burst 26.1%
  IDLE 51.6%
  Tmr Svc 0.0%
  async 21.9%
== DIV=100
counter units in 500 ms: 5010
  mon 0.0%
  burst 26.3%
  IDLE 51.5%
  Tmr Svc 0.0%
  async 21.9%
== DIV=1000
counter units in 500 ms: 501
  mon 0.0%
  burst 0.0%
  IDLE 78.0%
  Tmr Svc 0.0%
  async 21.7%
== TICK=1
counter units in 500 ms: 500
  mon 0.0%
  burst 0.0%
  IDLE 77.4%
  Tmr Svc 0.0%
  async 22.6%
```

(`== …` 줄은 루프가 찍은 구분선, 크기 표시와 `[board_exit]` 줄은 생략.)

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="20" text-anchor="middle" font-size="14">run-time 카운터 해상도별 CPU% — ex9 (500 ms)</text>
<line x1="60" y1="245.0" x2="620" y2="245.0" stroke="currentColor" stroke-opacity="0.5"/>
<text x="52" y="249.0" text-anchor="end" font-size="12">0</text>
<line x1="60" y1="195.0" x2="620" y2="195.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="199.0" text-anchor="end" font-size="12">20</text>
<line x1="60" y1="145.0" x2="620" y2="145.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="149.0" text-anchor="end" font-size="12">40</text>
<line x1="60" y1="95.0" x2="620" y2="95.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="99.0" text-anchor="end" font-size="12">60</text>
<line x1="60" y1="45.0" x2="620" y2="45.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="49.0" text-anchor="end" font-size="12">80</text>
<rect x="78.0" y="180.0" width="24" height="65.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="90.0" y="176.0" text-anchor="middle" font-size="12">26</text>
<rect x="104.0" y="190.2" width="24" height="54.8" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="116.0" y="186.2" text-anchor="middle" font-size="12">22</text>
<rect x="130.0" y="115.5" width="24" height="129.5" fill="#888" fill-opacity="0.75"/>
<text x="142.0" y="111.5" text-anchor="middle" font-size="12">52</text>
<text x="116.0" y="263" text-anchor="middle" font-size="12">1 us</text>
<rect x="190.0" y="179.8" width="24" height="65.2" fill="#e08a3c" fill-opacity="0.75"/>
<text x="202.0" y="175.8" text-anchor="middle" font-size="12">26</text>
<rect x="216.0" y="190.2" width="24" height="54.8" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="228.0" y="186.2" text-anchor="middle" font-size="12">22</text>
<rect x="242.0" y="116.0" width="24" height="129.0" fill="#888" fill-opacity="0.75"/>
<text x="254.0" y="112.0" text-anchor="middle" font-size="12">52</text>
<text x="228.0" y="263" text-anchor="middle" font-size="12">10 us</text>
<rect x="302.0" y="179.2" width="24" height="65.8" fill="#e08a3c" fill-opacity="0.75"/>
<text x="314.0" y="175.2" text-anchor="middle" font-size="12">26</text>
<rect x="328.0" y="190.2" width="24" height="54.8" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="340.0" y="186.2" text-anchor="middle" font-size="12">22</text>
<rect x="354.0" y="116.2" width="24" height="128.8" fill="#888" fill-opacity="0.75"/>
<text x="366.0" y="112.2" text-anchor="middle" font-size="12">52</text>
<text x="340.0" y="263" text-anchor="middle" font-size="12">100 us</text>
<rect x="414.0" y="245.0" width="24" height="1.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="426.0" y="241.0" text-anchor="middle" font-size="12">0</text>
<rect x="440.0" y="190.8" width="24" height="54.2" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="452.0" y="186.8" text-anchor="middle" font-size="12">22</text>
<rect x="466.0" y="50.0" width="24" height="195.0" fill="#888" fill-opacity="0.75"/>
<text x="478.0" y="46.0" text-anchor="middle" font-size="12">78</text>
<text x="452.0" y="263" text-anchor="middle" font-size="12">1 ms</text>
<rect x="526.0" y="245.0" width="24" height="1.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="538.0" y="241.0" text-anchor="middle" font-size="12">0</text>
<rect x="552.0" y="188.5" width="24" height="56.5" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="564.0" y="184.5" text-anchor="middle" font-size="12">23</text>
<rect x="578.0" y="51.5" width="24" height="193.5" fill="#888" fill-opacity="0.75"/>
<text x="590.0" y="47.5" text-anchor="middle" font-size="12">77</text>
<text x="564.0" y="263" text-anchor="middle" font-size="12">tick</text>
<rect x="110" y="284" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/>
<text x="128" y="294" font-size="12">burst (tick 직후 300 us)</text>
<rect x="310" y="284" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="328" y="294" font-size="12">async (1370 Hz)</text>
<rect x="450" y="284" width="12" height="12" fill="#888" fill-opacity="0.75"/>
<text x="468" y="294" font-size="12">IDLE</text>
</svg>
```

그림 6 — 예제 10의 결과. 해상도가 일의 길이(300 us)보다 거칠어지는 순간 tick에 맞춰 도는 `burst`는 **0%** 가 되고 그 시간이 IDLE로 간다. 박자가 안 맞는 `async`는 거친 카운터에서도 평균적으로 맞는다.

**출력에서 볼 것**:

- **1 us ~ 100 us**: 세 결과가 0.3%p 안에서 같다. 100 us 해상도면 300 us 일을 3칸으로 재는 셈이라 약간 흔들리지만 쓸 만하다. (`burst`가 30%가 아니라 26%인 것은 `async`(우선순위 4)가 `burst`의 300 us 바쁜 구간 안에 끼어들기 때문이다. `spin_us`는 벽시계로 재므로 `burst`의 실제 CPU 몫은 300 us보다 작다.)
- **1 ms와 tick**: `burst`가 **0.0%**. `burst`는 tick ISR 직후 switch-in되어 같은 1 ms 칸 안에서 switch-out된다. 들어올 때와 나갈 때 카운터 값이 같으니 `>` 비교에서 Δ = 0. 26%의 CPU가 통째로 IDLE(78%)로 넘어갔다. "IDLE 78%라서 여유 많네"라고 판단하면 틀린다. 이것을 **aliasing(박자 맞물림) 오차** 라고 부를 수 있다 — 측정 시계와 사건이 같은 tick에 동기돼 있어서 평균내도 오차가 사라지지 않는다.
- `async`는 1 ms 카운터에서도 21.7~22.6%로 맞는다. 사건이 카운터 경계와 무관한 위치에서 일어나므로 "가끔 1칸, 가끔 0칸"이 평균적으로 실제 길이에 수렴한다(통계적으로 unbiased, 대신 짧은 창에서는 흔들린다).
- 규칙: **run-time 카운터는 tick보다 10~100배 빠른, tick과 독립된 자유 실행 타이머**여야 한다(흔히 쓰는 경험칙이 "tick보다 10~100배 빠르게"다 — 이 예제에서 1 ms(=tick)는 실패, 100 us(tick의 10배)는 0.3%p 오차였다). Cortex-M3/M4/M7 실칩이라면 DWT CYCCNT(코어 클럭, QEMU에는 없음), 범용 타이머를 1 MHz로 돌린 값이 흔한 선택이다. 32비트 CYCCNT는 168 MHz에서 약 25.6초에 wrap하므로 장시간이면 64비트 확장이 필요하다.
- `async`가 20.55%(150 us × 1370 Hz)가 아니라 21.9%인 것은 ISR·통지·스위치 비용이 `async`에 붙어서다(ISR 시간은 끊긴 태스크에 붙는다 — 여기서는 대부분 IDLE이나 `burst`가 끊긴다. 그래도 `async` 자신의 깨어남·블록 오버헤드가 더해진다).

---

## 9. "top" 모니터 태스크 — 운영 중에 CPU·스택·heap 보기

### 9.1 설계

Linux의 `top`처럼, 양산 펌웨어에도 주기적으로 "누가 CPU를 얼마나, 스택은 얼마나 남았나, heap은 새고 있나"를 보고하는 장치가 있으면 현장 문제의 절반은 로그만 보고 풀린다. 설계 요점:

1. **델타로 계산한다.** `ulRunTimeCounter`는 누적이므로 창마다 이전 값을 기억해 빼야 "지금 부하"가 나온다. 누적 퍼센트(`ulTaskGetRunTimePercent`)는 부팅 직후 부하가 몇 시간 뒤까지 섞여 나온다. 델타 방식은 카운터 wrap에도 강하다(부호 없는 뺄셈은 한 번 wrap까지 맞다).
2. **태스크를 핸들로 짝짓는다.** `uxTaskGetSystemState()`가 돌려주는 순서는 리스트 순서라 매번 바뀐다(아래 출력에서 `IDLE`·`ctl` 위치가 줄마다 다르다).
3. **우선순위는 높게, 일은 짧게.** 모니터가 낮으면 바쁠 때(가장 보고 싶을 때) 못 돈다. 대신 포맷팅·출력은 짧게 하거나, 숫자만 링버퍼에 넣고 출력은 낮은 우선순위 태스크가 한다.
4. **스택 HWM과 heap 최저 수위를 같이.** `usStackHighWaterMark`(words, `uxTaskGetSystemState`가 채운다 — 스택 전체를 훑으므로 태스크가 많으면 비용이 크다), `xPortGetFreeHeapSize()`, `xPortGetMinimumEverFreeHeapSize()`.

### 9.2 예제 11 — 3초 동안의 top 출력

**무엇을 확인하나**: 2 ms마다 400 us 일하는 `ctl`(우선순위 4, 20%), 10 ms마다 duty만큼 일하는 `log`(우선순위 2, 5% → 1 s에 40% → 2 s에 10%), 100 ms마다 256 B씩 새는 `leak`(우선순위 1, 2초쯤 깊은 재귀 한 번). 우선순위 6 `top`이 250 ms마다 태스크별 `CPU%/HWM(words)`와 heap을 한 줄로 찍는다.

```c
/* ex10: "top" 모니터 태스크 — 250 ms 창마다 태스크별 CPU%(델타), 스택 HWM, heap free/min-ever */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static volatile uint32_t log_duty = 5;            /* logger 부하(%) — 시간에 따라 바꾼다 */
static void spin_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) { } }

static void ctl(void *pv) { (void)pv; TickType_t l = xTaskGetTickCount(); for (;;) { spin_us(400); xTaskDelayUntil(&l, 2); } }
static void logger(void *pv) { (void)pv; TickType_t l = xTaskGetTickCount(); for (;;) { spin_us(log_duty * 100); xTaskDelayUntil(&l, 10); } }

static __attribute__((noinline)) uint32_t burn(volatile uint8_t *p, int n) { volatile uint8_t b[256]; b[0] = p ? p[0] : 0; return n ? burn(b, n - 1) + b[0] : 0; }
static void leaky(void *pv)                       /* 100 ms마다 256 B 누수, 2 s 뒤 깊은 재귀 한 번 */
{
    (void)pv;
    for (int k = 0;; k++) { (void)pvPortMalloc(256); if (k == 20) burn(NULL, 3); vTaskDelay(100); }
}

#define NT 8
static TaskStatus_t ts[NT];
static struct { TaskHandle_t h; uint32_t last; } prev[NT];
static uint32_t prev_of(TaskHandle_t h, uint32_t now)
{
    for (int i = 0; i < NT; i++) if (prev[i].h == h || !prev[i].h) { uint32_t o = prev[i].h ? prev[i].last : 0; prev[i].h = h; prev[i].last = now; return o; }
    return 0;
}

static void top(void *pv)
{
    (void)pv;
    uint32_t tprev = 0;
    for (int w = 1; w <= 12; w++) {
        vTaskDelay(pdMS_TO_TICKS(250));
        log_duty = w >= 8 ? 10 : w >= 4 ? 40 : 5;     /* 부하 시나리오 */
        configRUN_TIME_COUNTER_TYPE tot;
        UBaseType_t n = uxTaskGetSystemState(ts, NT, &tot);
        uint32_t dt = tot - tprev; tprev = tot;
        board_printf_raw("t=%u", w * 250);
        for (UBaseType_t i = 0; i < n; i++) {
            uint32_t d = ts[i].ulRunTimeCounter - prev_of(ts[i].xHandle, ts[i].ulRunTimeCounter);
            uint32_t pm = (uint32_t)((uint64_t)d * 1000 / dt);
            board_printf_raw(" %s=%u.%u/%u", ts[i].pcTaskName, pm / 10, pm % 10, (unsigned)ts[i].usStackHighWaterMark);
        }
        board_printf_raw(" heap=%u min=%u\n", (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());
    }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(ctl, "ctl", 256, NULL, 4, NULL);
    xTaskCreate(logger, "log", 256, NULL, 2, NULL);
    xTaskCreate(leaky, "leak", 512, NULL, 1, NULL);
    xTaskCreate(top, "top", 512, NULL, 6, NULL);
    vTaskStartScheduler();
}
```

```text
t=250 top=0.0/468 ctl=20.3/232 log=5.0/232 IDLE=74.0/230 leak=0.0/488 Tmr Svc=0.0/480 heap=123672 min=123672
t=500 top=0.4/460 IDLE=73.7/230 ctl=20.5/232 log=5.2/232 leak=0.0/488 Tmr Svc=0.0/480 heap=122880 min=122880
t=750 top=0.4/460 ctl=20.3/232 IDLE=74.0/230 log=5.0/232 leak=0.0/488 Tmr Svc=0.0/480 heap=122352 min=122352
t=1000 top=0.4/460 IDLE=73.9/230 ctl=20.5/232 log=5.0/232 leak=0.0/488 Tmr Svc=0.0/480 heap=121560 min=121560
t=1250 top=0.4/460 ctl=20.3/232 log=31.7/232 IDLE=47.3/230 leak=0.0/488 Tmr Svc=0.0/480 heap=121032 min=121032
t=1500 top=0.4/460 IDLE=47.1/230 ctl=20.5/232 log=31.7/232 leak=0.0/488 Tmr Svc=0.0/480 heap=120240 min=120240
t=1750 top=0.4/460 ctl=20.3/232 IDLE=47.3/230 log=31.7/232 leak=0.0/488 Tmr Svc=0.0/480 heap=119712 min=119712
t=2000 top=0.4/460 IDLE=47.2/230 ctl=20.5/232 log=31.8/232 leak=0.0/244 Tmr Svc=0.0/480 heap=118920 min=118920
t=2250 top=0.4/460 ctl=20.3/232 IDLE=69.1/230 log=10.0/232 leak=0.0/244 Tmr Svc=0.0/480 heap=118392 min=118392
t=2500 top=0.4/460 IDLE=68.9/230 log=10.0/232 ctl=20.5/232 leak=0.0/244 Tmr Svc=0.0/480 heap=117600 min=117600
t=2750 top=0.4/460 ctl=20.3/232 log=10.0/232 IDLE=69.1/230 leak=0.0/244 Tmr Svc=0.0/480 heap=117072 min=117072
t=3000 top=0.4/460 log=10.1/232 IDLE=68.9/230 ctl=20.5/232 leak=0.0/244 Tmr Svc=0.0/480 heap=116280 min=116280
[board_exit] code=0
exit=0
```

(크기 표시 줄은 생략.)

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="20" text-anchor="middle" font-size="14">top 모니터 출력 — 250 ms 창별 CPU% (막대) · heap free (선)</text>
<line x1="60" y1="255.0" x2="590" y2="255.0" stroke="currentColor" stroke-opacity="0.5"/>
<text x="52" y="259.0" text-anchor="end" font-size="12">0%</text>
<line x1="60" y1="202.5" x2="590" y2="202.5" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="206.5" text-anchor="end" font-size="12">25%</text>
<line x1="60" y1="150.0" x2="590" y2="150.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="154.0" text-anchor="end" font-size="12">50%</text>
<line x1="60" y1="97.5" x2="590" y2="97.5" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="101.5" text-anchor="end" font-size="12">75%</text>
<line x1="60" y1="45.0" x2="590" y2="45.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="49.0" text-anchor="end" font-size="12">100%</text>
<text x="596" y="259.0" font-size="12">115.0K</text>
<text x="596" y="206.5" font-size="12">117.5K</text>
<text x="596" y="154.0" font-size="12">120.0K</text>
<text x="596" y="101.5" font-size="12">122.5K</text>
<text x="596" y="49.0" font-size="12">125.0K</text>
<rect x="64.0" y="212.4" width="36.2" height="42.6" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="64.0" y="201.9" width="36.2" height="10.5" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="64.0" y="46.5" width="36.2" height="155.4" fill="#888" fill-opacity="0.25"/>
<rect x="108.2" y="211.9" width="36.2" height="43.1" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="108.2" y="201.0" width="36.2" height="10.9" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="108.2" y="46.3" width="36.2" height="154.8" fill="#888" fill-opacity="0.25"/>
<text x="126.2" y="273" text-anchor="middle" font-size="12">0.5s</text>
<rect x="152.3" y="212.4" width="36.2" height="42.6" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="152.3" y="201.9" width="36.2" height="10.5" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="152.3" y="46.5" width="36.2" height="155.4" fill="#888" fill-opacity="0.25"/>
<rect x="196.5" y="211.9" width="36.2" height="43.1" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="196.5" y="201.4" width="36.2" height="10.5" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="196.5" y="46.3" width="36.2" height="155.2" fill="#888" fill-opacity="0.25"/>
<text x="214.6" y="273" text-anchor="middle" font-size="12">1s</text>
<rect x="240.7" y="212.4" width="36.2" height="42.6" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="240.7" y="145.8" width="36.2" height="66.6" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="240.7" y="46.5" width="36.2" height="99.3" fill="#888" fill-opacity="0.25"/>
<rect x="284.8" y="211.9" width="36.2" height="43.1" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="284.8" y="145.4" width="36.2" height="66.6" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="284.8" y="46.5" width="36.2" height="98.9" fill="#888" fill-opacity="0.25"/>
<text x="302.9" y="273" text-anchor="middle" font-size="12">1.5s</text>
<rect x="329.0" y="212.4" width="36.2" height="42.6" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="329.0" y="145.8" width="36.2" height="66.6" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="329.0" y="46.5" width="36.2" height="99.3" fill="#888" fill-opacity="0.25"/>
<rect x="373.2" y="211.9" width="36.2" height="43.1" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="373.2" y="145.2" width="36.2" height="66.8" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="373.2" y="46.1" width="36.2" height="99.1" fill="#888" fill-opacity="0.25"/>
<text x="391.2" y="273" text-anchor="middle" font-size="12">2s</text>
<rect x="417.3" y="212.4" width="36.2" height="42.6" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="417.3" y="191.4" width="36.2" height="21.0" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="417.3" y="46.3" width="36.2" height="145.1" fill="#888" fill-opacity="0.25"/>
<rect x="461.5" y="211.9" width="36.2" height="43.1" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="461.5" y="190.9" width="36.2" height="21.0" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="461.5" y="46.3" width="36.2" height="144.7" fill="#888" fill-opacity="0.25"/>
<text x="479.6" y="273" text-anchor="middle" font-size="12">2.5s</text>
<rect x="505.7" y="212.4" width="36.2" height="42.6" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="505.7" y="191.4" width="36.2" height="21.0" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="505.7" y="46.3" width="36.2" height="145.1" fill="#888" fill-opacity="0.25"/>
<rect x="549.8" y="211.9" width="36.2" height="43.1" fill="#4a7bd0" fill-opacity="0.7"/>
<rect x="549.8" y="190.7" width="36.2" height="21.2" fill="#e08a3c" fill-opacity="0.7"/>
<rect x="549.8" y="46.1" width="36.2" height="144.7" fill="#888" fill-opacity="0.25"/>
<text x="567.9" y="273" text-anchor="middle" font-size="12">3s</text>
<polyline points="82.1,72.9 126.2,89.5 170.4,100.6 214.6,117.2 258.8,128.3 302.9,145.0 347.1,156.0 391.2,172.7 435.4,183.8 479.6,200.4 523.8,211.5 567.9,228.1" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="82.1" cy="199.8" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="126.2" cy="199.1" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="170.4" cy="199.6" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="214.6" cy="199.6" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="258.8" cy="143.9" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="302.9" cy="143.9" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="347.1" cy="142.6" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="391.2" cy="143.5" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="435.4" cy="188.8" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="479.6" cy="188.8" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="523.8" cy="188.8" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<circle cx="567.9" cy="188.0" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<rect x="60" y="304" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7"/>
<text x="78" y="314" font-size="12">ctl</text>
<rect x="120" y="304" width="12" height="12" fill="#e08a3c" fill-opacity="0.7"/>
<text x="138" y="314" font-size="12">log</text>
<rect x="180" y="304" width="12" height="12" fill="#888" fill-opacity="0.25"/>
<text x="198" y="314" font-size="12">IDLE</text>
<line x1="250" y1="310" x2="264" y2="310" stroke="#d0564a" stroke-width="2"/>
<text x="268" y="314" font-size="12">heap free (오른쪽 축)</text>
<circle cx="436" cy="310" r="3.5" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<text x="448" y="314" font-size="12">Zephyr 100−idle</text>
</svg>
```

그림 7 — 예제 11의 출력을 그대로 그렸다(막대: 창별 CPU% 누적, 선: heap free). 초록 원은 같은 작업을 Zephyr에서 돌린 예제 13의 "100 − idle%"다. 두 RTOS가 1%p 안에서(최대 차이 0.8%p) 같은 그림을 그린다.

**출력에서 볼 것**:

- 부하 변화가 창 단위로 정확히 보인다: 1 s에 `log`가 5 → 31.7%, 2 s에 10%. 40%로 설정했는데 31.7%인 이유는 `log`(우선순위 2)의 4 ms 바쁜 구간에 `ctl`(우선순위 4)이 20%를 끼어들어 가져가기 때문이다(4 ms × 0.8 = 3.2 ms → 32%). 모니터는 "실제로 CPU를 쓴 시간"을 보여 준다.
- **heap이 계단처럼 꾸준히 준다** — 250 ms마다 528 B 또는 792 B(256 + 헤더 8 = 264 B 블록 2~3개). `min`이 `heap`과 같이 계속 내려가면 누수다. 일시적 피크라면 `min`만 내려가고 `heap`은 돌아온다. 이 두 숫자를 같이 찍는 이유다.
- **HWM이 488 → 244 words**로 2 s 창에서 떨어졌다 — 깊은 재귀 한 번(256 B 배열 × 4단)의 흔적이다. HWM은 "지금까지의 최저"라서 한 번 내려가면 안 돌아온다. 현장에서 HWM이 갑자기 떨어진 시점을 로그로 찾으면 원인 경로를 좁힐 수 있다.
- `top` 자신은 0.4%. 태스크 6개 `uxTaskGetSystemState` + 한 줄 포맷이 250 ms마다 약 1 ms다(그중 상당 부분이 스택 HWM 계산과 UART 출력).

---

## 10. 태스크 삭제와 자원 회수 — `vTaskDelete`가 정리해 주지 않는 것

### 10.1 자기 삭제 vs 남 삭제 — 다시 정리

| | 자기 삭제 `vTaskDelete(NULL)` | 남 삭제 `vTaskDelete(h)` |
|---|---|---|
| 리스트에서 빼기 | 즉시 (ready/delayed/event list) | 즉시 |
| 어디로 가나 | `xTasksWaitingTermination`, `uxDeletedTasksWaitingCleanUp++` | 아무 리스트에도 없음 |
| 스택·TCB free | **idle이 나중에** (`prvCheckTasksWaitingTermination`) | `vTaskDelete` 안에서 **즉시** (`prvDeleteTCB`) |
| 위험 | idle이 굶으면 영원히 안 돌아옴 (A4 §3) | 지운 태스크가 쥔 자원 (아래) |

두 경우 모두 커널이 돌려주는 것은 **커널이 그 태스크를 위해 할당한 것** — TCB와 스택(동적 생성일 때만) — 뿐이다. 태스크가 실행 중에 얻은 자원은 하나도 정리하지 않는다. FreeRTOS에는 Linux의 "프로세스가 죽으면 열린 파일·메모리를 커널이 회수"하는 개념이 없다. 태스크는 같은 주소 공간을 공유하는 함수일 뿐이고, 커널은 그 태스크가 무엇을 가졌는지 추적하지 않는다(유일한 예외는 mutex의 holder 필드인데, 그것도 아래처럼 정리되지 않는다).

### 10.2 예제 12 — 누수, 영원히 잠긴 mutex, 해제된 TCB에 쓰는 커널

**무엇을 확인하나**: `owner`(우선순위 2)가 버퍼 2048 B·300 B를 malloc하고 mutex를 쥔 채 대기한다. `boss`(우선순위 3)가 `owner`를 삭제한 뒤 (1) heap이 얼마 돌아왔나, (2) `traceMALLOC`/`traceFREE`로 만든 "살아 있는 블록 표"에 죽은 태스크 소유 블록이 남았나, (3) mutex holder가 누구로 보이나, (4) mutex를 10 ms 기다리면 어떻게 되나를 본다. 그 기다리는 동안 `spy`(우선순위 4)가 **해제된 TCB 메모리**의 11번째 word(`uxPriority`)을 읽는다.

```c
/* ex8: vTaskDelete가 회수하지 않는 것 — 태스크가 malloc한 버퍼, 쥔 mutex. traceMALLOC으로 소유자 추적 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"

#define NB 32
static struct { void *p; unsigned n; TaskHandle_t owner; } blk[NB];      /* 살아 있는 블록 표 */
void c9_malloc(void *p, unsigned n)
{
    if (!p) return;
    for (int i = 0; i < NB; i++) if (!blk[i].p) { blk[i].p = p; blk[i].n = n; blk[i].owner = xTaskGetCurrentTaskHandle(); return; }
}
void c9_free(void *p, unsigned n) { (void)n; for (int i = 0; i < NB; i++) if (blk[i].p == p) { blk[i].p = NULL; return; } }
void c9_sw_in(void *t) { (void)t; }
void c9_sw_out(void *t) { (void)t; }

static SemaphoreHandle_t m;
static TaskHandle_t hOwner;

static void owner(void *pv)
{
    (void)pv;
    char *buf = pvPortMalloc(2048);               /* 태스크 전용 작업 버퍼 */
    char *cfg = pvPortMalloc(300);
    xSemaphoreTake(m, portMAX_DELAY);             /* 자원을 쥔 채로... */
    board_printf_raw("owner: buf=%x cfg=%x, holds mutex, now waits forever\n", (unsigned)buf, (unsigned)cfg);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* ...여기서 남에게 삭제된다 */
}

static void spy(void *pv)                         /* prio 4: boss가 mutex를 기다리는 동안 죽은 TCB를 엿본다 */
{
    (void)pv;
    vTaskDelay(7);
    board_printf_raw("spy: freed TCB word[11] (uxPriority) = %u  <- kernel wrote into freed memory\n",
                     (unsigned)((uint32_t *)hOwner)[11]);
    vTaskDelete(NULL);
}

static void boss(void *pv)
{
    (void)pv;
    m = xSemaphoreCreateMutex();
    xTaskCreate(owner, "owner", 256, NULL, 2, &hOwner);
    vTaskDelay(2);
    size_t f0 = xPortGetFreeHeapSize();
    vTaskDelete(hOwner);                          /* 남을 삭제: TCB와 스택은 즉시 반납 */
    size_t f1 = xPortGetFreeHeapSize();
    board_printf_raw("boss: deleted owner, heap free %u -> %u (+%u)\n", (unsigned)f0, (unsigned)f1, (unsigned)(f1 - f0));
    for (int i = 0; i < NB; i++)
        if (blk[i].p && blk[i].owner == hOwner)
            board_printf_raw("  LEAK: block %x size %u allocated by deleted task\n", (unsigned)blk[i].p, blk[i].n);
    board_printf_raw("boss: mutex holder = %x (deleted owner was %x)\n",
                     (unsigned)xSemaphoreGetMutexHolder(m), (unsigned)hOwner);
    board_printf_raw("boss: freed TCB word[11] (uxPriority) before take = %u\n", (unsigned)((uint32_t *)hOwner)[11]);
    BaseType_t ok = xSemaphoreTake(m, pdMS_TO_TICKS(10));
    board_printf_raw("boss: xSemaphoreTake(m, 10 ms) -> %s\n", ok ? "pdTRUE" : "pdFALSE (mutex is gone forever)");
    board_exit(0);
}

int main(void)
{
    xTaskCreate(boss, "boss", 512, NULL, 3, NULL);
    xTaskCreate(spy, "spy", 256, NULL, 4, NULL);
    vTaskStartScheduler();
}
```

```sh
TAG=ex8 ./run.sh ex8 EXTRA_CFLAGS='-DC9_TRACE -DINCLUDE_xSemaphoreGetMutexHolder=1'
```

```text
owner: buf=20002340 cfg=20002b48, holds mutex, now waits forever
boss: deleted owner, heap free 124160 -> 125296 (+1136)
  LEAK: block 20002340 size 2056 allocated by deleted task
  LEAK: block 20002b48 size 312 allocated by deleted task
boss: mutex holder = 200022d8 (deleted owner was 200022d8)
boss: freed TCB word[11] (uxPriority) before take = 2
spy: freed TCB word[11] (uxPriority) = 3  <- kernel wrote into freed memory
boss: xSemaphoreTake(m, 10 ms) -> pdFALSE (mutex is gone forever)
[board_exit] code=0
exit=0
```

(크기 표시 줄은 생략. 템플릿은 `INCLUDE_xSemaphoreGetMutexHolder`를 켜지 않아서 `-D`로 켰다.)

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">vTaskDelete(owner)가 돌려주는 것과 안 돌려주는 것 (ex8)</text>
<rect x="20" y="120" width="150" height="60" rx="6" fill="#888" fill-opacity="0.2" stroke="#888"/>
<text x="95" y="146" font-size="13" text-anchor="middle" font-weight="bold">vTaskDelete</text>
<text x="95" y="166" font-size="12" text-anchor="middle">(남이 삭제)</text>
<line x1="170" y1="140" x2="228" y2="80" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="170" y1="160" x2="228" y2="215" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="230" y="40" width="430" height="86" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="445" y="62" font-size="13" text-anchor="middle" font-weight="bold">회수됨 — 커널이 만든 것만</text>
<text x="445" y="84" font-size="12" text-anchor="middle">TCB 96 B + 스택 1024 B + heap 헤더 16 B = +1136 B</text>
<text x="445" y="104" font-size="12" text-anchor="middle">(자기 삭제면 idle이 돌 때까지 미뤄짐 — §1)</text>
<rect x="230" y="140" width="430" height="170" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="445" y="162" font-size="13" text-anchor="middle" font-weight="bold">회수 안 됨 — 태스크가 얻은 자원</text>
<text x="445" y="186" font-size="12" text-anchor="middle">pvPortMalloc 버퍼 2056 B + 312 B → 영구 누수</text>
<text x="445" y="208" font-size="12" text-anchor="middle">쥐고 있던 mutex → holder = 죽은 TCB, 아무도 못 얻음</text>
<text x="445" y="230" font-size="12" text-anchor="middle">상속: 커널이 해제된 TCB에 우선순위를 씀 (2 → 3)</text>
<text x="445" y="252" font-size="12" text-anchor="middle">보낸 큐 항목 · 등록한 콜백 · ISR이 쓰는 핸들</text>
<text x="445" y="274" font-size="12" text-anchor="middle">드라이버 상태 (DMA 진행 중, 주변장치 전원)</text>
<text x="445" y="296" font-size="12" text-anchor="middle">→ 해결: 스스로 정리하고 끝나게 신호를 보낸다</text>
</svg>
```

그림 8 — 예제 12에서 확인한 "돌아오는 것"과 "안 돌아오는 것". 초록은 커널이 만든 것(TCB + 스택), 빨강은 태스크가 실행 중에 얻은 것.

**출력에서 볼 것**:

- 삭제로 돌아온 것은 **+1136 B = 스택 1024 + TCB 96 + heap 헤더 2 × 8**. 남 삭제라서 즉시 돌아왔다. `owner`가 malloc한 2056 B + 312 B(요청 + 헤더 8 B)는 소유자가 사라진 채 영원히 남는다. `traceMALLOC` 훅에서 `xTaskGetCurrentTaskHandle()`로 "누가 할당했나"를 적어 두면 이런 **고아 블록**을 찾을 수 있다 — 개발 빌드용 누수 추적기로 쓸 만하다.
- **mutex holder가 죽은 TCB 주소(0x200022d8)를 그대로 가리킨다**. 아무도 이 mutex를 다시 얻지 못한다. `xSemaphoreTake(m, 10 ms)`는 타임아웃(pdFALSE). `portMAX_DELAY`로 기다렸다면 영원히 멈춘다.
- **가장 위험한 것**: `boss`(우선순위 3)가 mutex를 기다리기 시작하자 커널이 **우선순위 상속**(priority inheritance, D3)을 하려고 holder TCB의 `uxPriority`를 2 → 3으로 바꿨다. 그 TCB는 이미 free된 메모리다. `spy`가 그 자리에서 3을 읽었다. 그 블록이 그사이 다른 용도(큐, 다른 태스크의 스택)로 재할당됐다면 커널이 남의 데이터를 덮어쓴 것이 된다 — use-after-free. 타임아웃 뒤 `vTaskPriorityDisinheritAfterTimeout()`이 다시 2로 되돌리며 한 번 더 쓴다. 증상은 "mutex를 쓰는 태스크가 가끔 다른 곳의 메모리를 망가뜨린다"로 나타나 원인을 찾기 매우 어렵다.
- 큐에 넣은 항목은 삭제와 무관하게 큐에 남는다(받는 쪽이 처리하면 문제없다). 문제는 그 항목이 "죽은 태스크의 스택에 있는 버퍼를 가리키는 포인터"일 때다 — 스택이 free되는 순간 dangling이 된다.

### 10.3 그래서 태스크는 이렇게 끝낸다

1. **남을 지우지 않는다.** 끝내야 하면 "끝나라"는 신호(task notification, 이벤트 비트)를 보내고, 그 태스크가 스스로 버퍼 free → mutex give → 드라이버 정지 → `vTaskDelete(NULL)` 순서로 정리하게 한다. POSIX의 `pthread_cancel`보다 "협조적 종료"를 쓰는 이유와 같다.
2. **가능하면 아예 지우지 않는다.** 양산 펌웨어 대부분은 부팅 때 모든 태스크를 정적으로 만들고 영원히 돌린다(F1). 일회성 초기화는 daemon startup hook(§6)이나 main()에서.
3. 지워야 한다면 idle이 돌 수 있게(우선순위 ≥ 1 바쁜 루프 금지) 하고, 정적 생성 태스크의 메모리는 `eTaskGetState(h) == eDeleted`를 지나 "리스트에서 완전히 빠진 뒤" 재사용한다.
4. 개발 빌드에서는 `traceTASK_DELETE(pxTCB)` 훅에서 "이 태스크가 쥔 mutex가 있나"(`uxMutexesHeld`, TCB 필드 — `configUSE_MUTEXES`일 때 있다)를 검사해 assert를 걸 수 있다. TCB 구조체는 `tasks.c` 안에 숨어 있지만 trace 매크로는 `tasks.c` 안에서 펼쳐지므로 `pxTCB->uxMutexesHeld`를 직접 읽을 수 있다.

---
## 11. Zephyr에서는 — idle 스레드, runtime stats, top, 스택 보호

### 11.1 idle 스레드 (`kernel/idle.c`)

Zephyr의 `idle()`은 FreeRTOS보다 단순하다. 메모리 회수도, 사용자 훅 호출도 없다. 한 바퀴는 이렇다(v4.4.2, SMP·레거시 분기 생략).

```c
while (true) {
    (void) arch_irq_lock();                     /* 인터럽트를 막고 */
#ifdef CONFIG_PM
    _kernel.idle = z_get_next_timeout_expiry(); /* 다음 timeout까지 남은 tick */
    if (k_is_pre_kernel() || !pm_system_suspend(_kernel.idle))
        k_cpu_idle();                           /* 깊은 절전에 못 들어가면 그냥 WFI */
#else
    k_cpu_idle();                               /* Cortex-M: arch_cpu_idle() → WFI, 리턴하며 인터럽트 허용 */
#endif
}
```

- **WFI가 기본이다.** `k_cpu_idle()`은 "인터럽트가 막힌 채로 불려서, 인터럽트를 허용하며 돌아오는" 특이한 API다(소스 주석: "Note weird API"). 막은 상태에서 잠들어야 "잠들기 직전에 인터럽트가 와서 놓치는" 경쟁을 피한다. Cortex-M 구현은 `arch/arm/core/cortex_m/cpu_idle.c`의 `arch_cpu_idle()`(`SLEEP_IF_ALLOWED(__WFI)`).
- **tickless가 기본이다**(이 보드의 빌드 `.config`에 `CONFIG_TICKLESS_KERNEL=y`). idle에 오래 머물면 tick 인터럽트 자체가 안 온다(C6).
- **사용자 idle 훅이 없다.** 저전력 정책은 PM 서브시스템(`pm_system_suspend()` → SoC의 `pm_state_set()`)에 꽂는다. "idle 카운터로 CPU 재기"는 Zephyr에서 애초에 불가능하고, 대신 커널이 `idle_cycles`를 세 준다(아래).
- **메모리 회수가 없다.** Zephyr 스레드는 기본이 정적(`K_THREAD_DEFINE`, `K_THREAD_STACK_DEFINE`)이다. 스레드가 entry에서 return하거나 `k_thread_abort()`되면 dead가 되고, 스택·`struct k_thread`는 사용자 소유 그대로다. 동적 스택(`CONFIG_DYNAMIC_THREAD`의 `k_thread_stack_alloc()`)은 사용자가 `k_thread_join()` 뒤 `k_thread_stack_free()`로 직접 돌려준다.

### 11.2 runtime stats — `CONFIG_THREAD_RUNTIME_STATS`

| FreeRTOS | Zephyr v4.4.2 | 비고 |
|---|---|---|
| `configGENERATE_RUN_TIME_STATS` | `CONFIG_THREAD_RUNTIME_STATS` (+ `CONFIG_SCHED_THREAD_USAGE` 기본 y) | 스레드별 cycle 누적 |
| `portGET_RUN_TIME_COUNTER_VALUE()` | `k_cycle_get_32()` (기본) 또는 timing functions (`CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS`) | `kernel/usage.c`의 `usage_now()` |
| `ulTaskGetRunTimeCounter(h)` | `k_thread_runtime_stats_get(tid, &s)` → `s.execution_cycles` | 64비트 |
| `ulTaskGetIdleRunTimeCounter()` | `k_thread_runtime_stats_all_get(&s)` → `s.idle_cycles` (`CONFIG_SCHED_THREAD_USAGE_ALL`, 기본 y) | 전체 = `execution_cycles` |
| (없음) | `CONFIG_SCHED_THREAD_USAGE_ANALYSIS` → `current_cycles`/`peak_cycles`/`average_cycles` | 예제 9의 "최장 slice"를 커널이 해 준다 |
| `traceTASK_SWITCHED_IN/OUT` | `z_thread_mark_switched_in/out()` (`CONFIG_INSTRUMENT_THREAD_SWITCHING`) → `z_sched_usage_start/stop()` + `CONFIG_TRACING`의 `SYS_PORT_TRACING_*` | 같은 "스위치 지점 샘플링" |
| `uxTaskGetStackHighWaterMark` | `k_thread_stack_space_get(t, &unused)` (`CONFIG_INIT_STACKS` + `CONFIG_THREAD_STACK_INFO`) | 바이트 단위, 0xaa 채움 |
| `xPortGetFreeHeapSize` | `sys_heap_runtime_stats_get(&heap.heap, &stats)` (`CONFIG_SYS_HEAP_RUNTIME_STATS`) | `free_bytes` · `allocated_bytes` · `max_allocated_bytes` |
| `vTaskListTasks` | thread analyzer `thread_analyzer_print()` (`CONFIG_THREAD_ANALYZER`, 주기 실행은 `_AUTO`) | 스택 + CPU% |

Zephyr의 회계도 FreeRTOS처럼 **스레드 전환 지점**(`z_thread_mark_switched_in/out`)에서 시각을 읽는다. 그래서 ISR 시간이 어떻게 나뉘는지는 FreeRTOS와 같은 질문이 남는다 — 이 노트에서는 Zephyr 쪽 ISR 귀속을 따로 실험하지 않았다(확인 못 함).

### 11.3 예제 13 — Zephyr판 top

**무엇을 확인하나**: 예제 11과 같은 작업 세트(ctl 2 ms/400 us, logger 10 ms/duty 5→40→10%, 100 ms마다 256 B 누수)를 Zephyr 스레드로 만들고, `main`이 250 ms마다 `k_thread_foreach()`로 스레드별 cycle 델타·미사용 스택을, `k_thread_runtime_stats_all_get()`으로 idle%를, `sys_heap_runtime_stats_get()`으로 heap을 찍는다. 끝에 thread analyzer도 부른다.

```text
CONFIG_PRINTK=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
CONFIG_THREAD_NAME=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_STACK_INFO=y
CONFIG_INIT_STACKS=y
CONFIG_THREAD_RUNTIME_STATS=y
CONFIG_SYS_HEAP_RUNTIME_STATS=y
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_PRINTK=y
```

```c
/* ztop: Zephyr판 "top" — k_thread_runtime_stats_get 델타, k_thread_stack_space_get, sys_heap 통계 */
#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>

K_HEAP_DEFINE(app_heap, 16384);
static volatile uint32_t log_duty = 5;

static void ctl(void *a, void *b, void *c)
{
	int64_t next = k_uptime_get();
	for (;;) { k_busy_wait(400); next += 2; k_sleep(K_TIMEOUT_ABS_MS(next)); }
}
static void logger(void *a, void *b, void *c)
{
	int64_t next = k_uptime_get();
	for (;;) { k_busy_wait(log_duty * 100); next += 10; k_sleep(K_TIMEOUT_ABS_MS(next)); }
}
static void leaky(void *a, void *b, void *c)
{
	for (;;) { (void)k_heap_alloc(&app_heap, 256, K_NO_WAIT); k_msleep(100); }
}
K_THREAD_DEFINE(ctl_tid, 1024, ctl, NULL, NULL, NULL, 4, 0, 0);
K_THREAD_DEFINE(log_tid, 1024, logger, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(leak_tid, 1024, leaky, NULL, NULL, NULL, 8, 0, 0);

static struct { const struct k_thread *t; uint64_t last; } prev[8];
static uint64_t win_total;

static void row(const struct k_thread *t, void *u)
{
	k_thread_runtime_stats_t s; size_t unused = 0; int i;
	k_thread_runtime_stats_get((k_tid_t)t, &s);
	k_thread_stack_space_get(t, &unused);
	for (i = 0; i < 8 && prev[i].t && prev[i].t != t; i++) { }
	uint64_t d = s.execution_cycles - prev[i].last;
	prev[i].t = t; prev[i].last = s.execution_cycles;
	uint32_t pm = win_total ? (uint32_t)(d * 1000 / win_total) : 0;
	printk(" %s=%u.%u/%u", k_thread_name_get((k_tid_t)t), pm / 10, pm % 10, (unsigned)unused);
}

int main(void)
{
	k_thread_runtime_stats_t all, all0 = {0};
	struct sys_memory_stats hs;
	for (int w = 1; w <= 12; w++) {
		k_msleep(250);
		log_duty = w >= 8 ? 10 : w >= 4 ? 40 : 5;
		k_thread_runtime_stats_all_get(&all);       /* execution_cycles = 전체, idle_cycles = idle */
		win_total = all.execution_cycles - all0.execution_cycles;
		uint32_t idle_pm = (uint32_t)((all.idle_cycles - all0.idle_cycles) * 1000 / win_total);
		all0 = all;
		printk("t=%d idle=%u.%u", w * 250, idle_pm / 10, idle_pm % 10);
		k_thread_foreach(row, NULL);
		sys_heap_runtime_stats_get(&app_heap.heap, &hs);
		printk(" heap_free=%u\n", (unsigned)hs.free_bytes);
	}
	thread_analyzer_print(0);
	k_sleep(K_FOREVER);
	return 0;
}
```

```sh
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C9/ztop /var/tmp/rtos/C9/ztop && \
timeout 8 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/C9/ztop/zephyr/zephyr.elf
```

```text
           FLASH:       17296 B         4 MB      0.41%
             RAM:       26528 B         4 MB      0.63%
*** Booting Zephyr OS build v4.4.2 ***
t=250 idle=73.7 log_tid=5.1/880 leak_tid=0.0/824 ctl_tid=20.9/880 idle=73.7/208 main=0.5/580 heap_free=15508
t=500 idle=73.4 log_tid=5.3/880 leak_tid=0.0/824 ctl_tid=20.8/880 idle=73.4/208 main=0.4/580 heap_free=14980
t=750 idle=73.6 log_tid=5.1/880 leak_tid=0.0/824 ctl_tid=20.8/880 idle=73.6/208 main=0.4/580 heap_free=14188
t=1000 idle=73.6 log_tid=5.1/880 leak_tid=0.0/824 ctl_tid=20.8/880 idle=73.6/208 main=0.4/580 heap_free=13660
t=1250 idle=47.1 log_tid=31.3/880 leak_tid=0.0/824 ctl_tid=20.9/880 idle=47.1/208 main=0.4/580 heap_free=12868
t=1500 idle=47.1 log_tid=31.3/880 leak_tid=0.0/824 ctl_tid=20.9/880 idle=47.1/208 main=0.4/580 heap_free=12340
t=1750 idle=46.5 log_tid=32.0/880 leak_tid=0.0/824 ctl_tid=20.9/880 idle=46.5/208 main=0.4/580 heap_free=11548
t=2000 idle=46.9 log_tid=31.5/880 leak_tid=0.0/824 ctl_tid=20.9/880 idle=46.9/208 main=0.4/580 heap_free=11020
t=2250 idle=68.5 log_tid=10.0/880 leak_tid=0.0/824 ctl_tid=20.8/880 idle=68.5/208 main=0.4/580 heap_free=10228
t=2500 idle=68.5 log_tid=10.0/880 leak_tid=0.0/824 ctl_tid=20.8/880 idle=68.5/208 main=0.4/580 heap_free=9700
t=2750 idle=68.5 log_tid=10.0/880 leak_tid=0.0/824 ctl_tid=20.8/880 idle=68.5/208 main=0.4/580 heap_free=8908
t=3000 idle=68.1 log_tid=10.4/880 leak_tid=0.0/824 ctl_tid=20.9/880 idle=68.1/208 main=0.4/580 heap_free=8380
Thread analyze:
 log_tid             : STACK: unused 880 usage 144 / 1024 (14 %); CPU: 15 %
                     : Total CPU cycles used: 11774792
 leak_tid            : STACK: unused 824 usage 200 / 1024 (19 %); CPU: 0 %
                     : Total CPU cycles used: 18850
 ctl_tid             : STACK: unused 880 usage 144 / 1024 (14 %); CPU: 20 %
                     : Total CPU cycles used: 15743796
 idle                : STACK: unused 208 usage 48 / 256 (18 %); CPU: 62 %
                     : Total CPU cycles used: 47447927
 main                : STACK: unused 444 usage 580 / 1024 (56 %); CPU: 0 %
                     : Total CPU cycles used: 388671
 ISR0                : STACK: unused 3944 usage 152 / 4096 (3 %); CPU: 0 %
                     : Total CPU cycles used: 0
qemu-system-arm: terminating on signal 15 from pid 1513320 (timeout)
```

**출력에서 볼 것**:

- 같은 작업에 같은 숫자: ctl 20.8~20.9%(FreeRTOS 20.3~20.5%), log 5.1 → 31.3 → 10.0%, idle 73.6 → 47.1 → 68.5%. 그림 7의 초록 원이 이 idle 값이다. Zephyr 쪽 ctl이 0.5%p 큰 것은 `k_busy_wait()`와 `k_sleep(K_TIMEOUT_ABS_MS())` 경로의 비용 차이로 보인다(정확한 분해는 안 했다).
- 첫 줄의 `idle=73.7`(전체 통계의 `idle_cycles`)과 목록의 `idle=73.7/208`(idle 스레드 자신의 `execution_cycles`)이 같다 — 두 경로가 같은 회계에서 나온다.
- 미사용 스택이 **바이트**(880 of 1024)다. FreeRTOS HWM은 **words**였다(232 words = 928 B). 같은 표에 섞으면 4배 틀린다.
- heap은 250 ms마다 528 또는 792 B씩 준다 — sys_heap도 256 B 요청에 264 B를 쓴다(청크 헤더 8 B, 우연히 heap_4와 같다).
- thread analyzer의 CPU%(log 15%, idle 62%)는 **부팅 이후 누적**이라 창 단위 값과 다르다. FreeRTOS의 `ulTaskGetRunTimePercent`와 같은 함정이다. `ISR0` 줄의 CPU 0%는 ISR 시간을 따로 회계하지 않는다는 뜻이다.

### 11.4 예제 14 — `CONFIG_STACK_SENTINEL` vs `CONFIG_HW_STACK_PROTECTION`, 그리고 `k_sys_fatal_error_handler`

Zephyr의 스택 넘침 대책은 세 층이다.

- **`CONFIG_INIT_STACKS`**: 스택을 0xaa로 채워 미사용량을 잰다(FreeRTOS 0xa5와 같은 발상). 검출 기능은 아니다.
- **`CONFIG_STACK_SENTINEL`**(`subsys/debug/Kconfig`): 스택 가장 낮은 4바이트에 `STACK_SENTINEL`(0xF0F0F0F0, `kernel_structs.h`)을 쓰고, 네 곳에서 `z_check_stack_sentinel()`(`kernel/thread.c`)로 확인한다(Kconfig 도움말): 컨텍스트 스위치 때 나가는 스레드, 스위치 없이 끝난 하드웨어 인터럽트 뒤 끊겼던 스레드, 스레드가 entry에서 return할 때, 스위치 없이 끝난 `k_yield()`. 깨졌으면 `z_except_reason(K_ERR_STACK_CHK_FAIL)`. FreeRTOS 방식 2와 같은 "흔적 검사"이고, `MPU_STACK_GUARD`·`USERSPACE`와는 같이 못 켠다(Kconfig `depends on !MPU_STACK_GUARD`).
- **`CONFIG_HW_STACK_PROTECTION`**: Cortex-M3(Armv7-M)에서는 `CONFIG_MPU_STACK_GUARD`로, 현재 스레드 스택 아래에 MPU 금지 영역(가드)을 둔다. 가드 크기는 `MPU_GUARD_ALIGN_AND_SIZE` — 최소 MPU 영역이 32 B 이하면 0x40(64 B, `include/zephyr/arch/arm/arch.h`). 넘치는 **그 명령**에서 MemManage fault가 난다. Armv8-M이면 PSPLIM을 쓴다(F3).

치명적 오류는 전부 `z_fatal_error()`(`kernel/fatal.c`) → 약한(weak) 심볼 `k_sys_fatal_error_handler(reason, esf)`로 모인다. 기본 구현은 `arch_system_halt()`로 시스템을 멈춘다. 사용자가 이 함수를 정의하고 **return하면** 커널은 그 스레드만 abort하고 나머지는 계속 돌린다(ISR 안 오류·kernel panic은 예외). FreeRTOS의 stack-overflow hook·configASSERT·HardFault 처리를 한곳에 모은 셈이다.

**무엇을 확인하나**: `K_THREAD_STACK_ARRAY_DEFINE(stk, 2, 512)`로 붙어 있는 스택 두 개를 만들고 `stk[1]`에서 victim을 돌린다(`stk[0]`은 0x11로 채운 이웃). 600 B 배열을 ① 전부 쓰기, ③ 가장 낮은 64 B만 쓰기, ④ 스택 객체(`stk[1]`, 가드 포함) **바로 아래까지만** 쓰기. 센티널 빌드와 HW 가드 빌드로 각각 돌린다.

```text
CONFIG_PRINTK=y
CONFIG_THREAD_NAME=y
CONFIG_LOG=n
# sentinel.conf
CONFIG_STACK_SENTINEL=y
# hw.conf
CONFIG_HW_STACK_PROTECTION=y
```

```c
/* zstk2: Zephyr 스택 넘침 — STACK_SENTINEL vs HW_STACK_PROTECTION(MPU guard), k_sys_fatal_error_handler */
#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <string.h>

K_THREAD_STACK_ARRAY_DEFINE(stk, 2, 512);         /* stk[0] = 이웃(pad), stk[1] = victim 스택 */
static struct k_thread vth;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	printk("[fatal handler] reason=%u (K_ERR_STACK_CHK_FAIL=%u) thread=%s\n", reason,
	       K_ERR_STACK_CHK_FAIL, k_thread_name_get(k_current_get()));
	/* return하면 커널은 이 스레드만 abort하고 나머지는 계속 돈다 */
}

static __noinline void deep(void)
{
	volatile uint8_t b[600];                  /* 512 B 스택에 600 B 배열, b[0]이 가장 낮은 주소 */
#if SCEN == 3
	for (int i = 0; i < 64; i++) b[i] = 0;    /* 가장 낮은 64 B만 */
#elif SCEN == 4
	for (int i = 0; &b[i] < (uint8_t *)stk[1]; i++) b[i] = 0;   /* 스택 객체(가드 포함) 바로 아래까지만 */
#else
	for (int i = 0; i < (int)sizeof(b); i++) b[i] = 0;
#endif
	printk("deep: b=%p..%p stack_info.start=%p\n", (void *)b, (void *)&b[63], (void *)vth.stack_info.start);
}

static void victim(void *a, void *b, void *c)
{
	deep();
	k_msleep(1);                              /* 컨텍스트 스위치 = 센티널 검사 시점 */
	printk("victim: still alive after sleep\n");
}

int main(void)
{
	printk("scenario=%d sentinel=%d hw_guard=%d\n", SCEN, IS_ENABLED(CONFIG_STACK_SENTINEL),
	       IS_ENABLED(CONFIG_HW_STACK_PROTECTION));
	memset(stk[0], 0x11, K_THREAD_STACK_SIZEOF(stk[0]));
	k_thread_create(&vth, stk[1], K_THREAD_STACK_SIZEOF(stk[1]), victim, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&vth, "victim");
	k_msleep(50);
	int bad = 0;
	for (size_t i = 0; i < K_THREAD_STACK_SIZEOF(stk[0]); i++) bad += ((uint8_t *)stk[0])[i] != 0x11;
	printk("main: alive. victim=%s, neighbor stk[0] corrupted bytes=%d\n",
	       k_thread_state_str(&vth, (char[32]){0}, 32), bad);
	return 0;
}
```

```sh
#!/bin/bash
# zrun.sh <app> <conf> <scen>: Zephyr 앱 빌드 + icount QEMU 실행
A=$1; C=$2; S=$3; D=/var/tmp/rtos/C9/${A}_${C}$S
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C9/$A $D -- -DEXTRA_CONF_FILE=$C.conf -DSCEN=$S && \
timeout 4 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -icount shift=5,align=off,sleep=off -kernel $D/zephyr/zephyr.elf 2>&1 | grep -v terminating
```

```text
=== sentinel scen 1
*** Booting Zephyr OS build v4.4.2 ***
scenario=1 sentinel=1 hw_guard=0
deep: b=0x20000570..0x200005af stack_info.start=0x200005e0
r0/a1:  0x00000002  r1/a2:  0xf0f0f0f0  r2/a3:  0x200005e0
r3/a4:  0x00000002 r12/ip:  0x0016b91f r14/lr:  0x000012ed
 xpsr:  0x01000000
Faulting instruction address (r15/pc): 0x00001110
[fatal handler] reason=2 (K_ERR_STACK_CHK_FAIL=2) thread=victim
main: alive. victim=sleeping+dead, neighbor stk[0] corrupted bytes=260
=== sentinel scen 3
*** Booting Zephyr OS build v4.4.2 ***
scenario=3 sentinel=1 hw_guard=0
deep: b=0x20000570..0x200005af stack_info.start=0x200005e0
victim: still alive after sleep
main: alive. victim=dead, neighbor stk[0] corrupted bytes=212
=== sentinel scen 4
*** Booting Zephyr OS build v4.4.2 ***
scenario=4 sentinel=1 hw_guard=0
deep: b=0x20000570..0x200005af stack_info.start=0x200005e0
victim: still alive after sleep
main: alive. victim=dead, neighbor stk[0] corrupted bytes=260
=== hw scen 1
*** Booting Zephyr OS build v4.4.2 ***
scenario=1 sentinel=0 hw_guard=1
***** MPU FAULT *****
  Data Access Violation
  MMFAR Address: 0x20000640
r0/a1:  0x00000000  r1/a2:  0x00000000  r2/a3:  0x00000000
r3/a4:  0x00000030 r12/ip:  0x00000000 r14/lr:  0x000000ff
 xpsr:  0x81000000
Faulting instruction address (r15/pc): 0x000000ca
[fatal handler] reason=2 (K_ERR_STACK_CHK_FAIL=2) thread=victim
main: alive. victim=dead, neighbor stk[0] corrupted bytes=16
=== hw scen 3
*** Booting Zephyr OS build v4.4.2 ***
scenario=3 sentinel=0 hw_guard=1
***** MPU FAULT *****
  Data Access Violation
  MMFAR Address: 0x20000640
r0/a1:  0x00000000  r1/a2:  0x00000000  r2/a3:  0x00000000
r3/a4:  0x00000030 r12/ip:  0x00000000 r14/lr:  0x000000ff
 xpsr:  0x81000000
Faulting instruction address (r15/pc): 0x000000ca
[fatal handler] reason=2 (K_ERR_STACK_CHK_FAIL=2) thread=victim
main: alive. victim=dead, neighbor stk[0] corrupted bytes=16
=== hw scen 4
*** Booting Zephyr OS build v4.4.2 ***
scenario=4 sentinel=0 hw_guard=1
deep: b=0x20000610..0x2000064f stack_info.start=0x20000680
victim: still alive after sleep
main: alive. victim=dead, neighbor stk[0] corrupted bytes=132
```

(`=== …` 줄은 루프가 찍은 구분선, 크기 표시 줄은 생략. 센티널 ① 출력의 `r0/a1 …` 레지스터 덤프는 커널의 fatal 경로가 `CONFIG_LOG=n`이라 머리말 없이 찍은 것이다. QEMU는 `timeout 4`로 끝냈다.)

**출력에서 볼 것**:

| 장면 | STACK_SENTINEL | HW_STACK_PROTECTION (MPU 가드 64 B) | 이웃 stk[0] 피해 |
|---|---|---|---|
| ① 600 B 전부 쓰기 | 잡음 — 다음 스위치(`k_msleep`) 때, reason=2 | 잡음 — **쓰는 그 명령**에서 MPU fault, reason=2 | 센티널 260 B / 가드 16 B |
| ③ 낮은 64 B만 | **놓침** (센티널 4 B를 안 건드림) | 잡음 — 64 B 중 위쪽 48 B가 가드에 닿음 | 센티널 212 B / 가드 16 B |
| ④ 가드·센티널 바로 아래까지만 | **놓침** | **놓침** | 센티널 260 B / 가드 132 B |

- **센티널은 FreeRTOS 방식 2와 같은 "흔적 검사"** 다. ①에서 victim은 `deep()`을 끝내고 `k_msleep(1)`에서 스위치될 때 잡혔고, 그 전에 이웃 260 B가 이미 망가졌다. 센티널은 4바이트뿐이라 ③처럼 그 위를 건너뛴 넘침은 못 본다.
- **MPU 가드는 즉시 잡는다.** HW 빌드에서 `stk[1]` 객체는 0x20000640에서 시작하고 그 첫 64 B(0x640~0x67f)가 가드, 실제 스택은 0x20000680부터다(`stack_info.start`). MMFAR 0x20000640 = 가드의 첫 바이트에 쓰려는 순간 fault가 났다. b[0..15]는 가드보다 더 아래(stk[0])라 이미 써졌다 — 피해 16 B. 즉 **가드도 "가드에 닿는" 넘침만 잡는다**. ④처럼 가드를 통째로 건너뛰어 그 아래만 쓰면 MPU도 아무 말이 없고 132 B가 망가졌다. 큰 지역 배열은 가드 크기(64 B)보다 훨씬 멀리 뛸 수 있다 — 그래서 F3의 정적 스택 산정(`-fstack-usage`)과 Armv8-M PSPLIM(SP 자체를 한계와 비교하므로 "건너뛰기"가 없다)이 필요하다.
- 검출된 경우(센티널 ①, 가드 ①③) 모두 `k_sys_fatal_error_handler`가 return하자 커널이 victim만 abort했고 `main`은 계속 돌았다(`victim=dead`, 센티널 ①은 `sleeping+dead`). 놓친 경우의 `dead`는 victim이 entry에서 정상 return한 결과다. FreeRTOS에서는 이런 "스레드만 죽이고 계속"을 커널이 해 주지 않는다 — 훅 안에서 직접 정책을 정해야 한다. 다만 이 회복은 위험하다. 이웃이 이미 망가졌을 수 있으므로 양산에서는 보통 로그를 남기고 리셋한다(`k_sys_fatal_error_handler`에서 `sys_reboot()` 등).
- ①의 센티널 빌드 피해(260 B)가 600 − 512보다 큰 것은 `deep()`이 부른 `printk`의 호출 프레임이 배열보다 더 아래로 내려갔기 때문이다. 가드 빌드에서는 그 printk 전에 fault가 나서 16 B로 끝났다 — **즉시 검출이 피해를 줄이는** 모습이다.

---

## 12. RTOS 커널 안에서는 무슨 일이 일어나나

훅은 전부 "커널 함수 안의 정해진 줄"이다. 소스 위치를 한 장으로 모으면 이렇다(FreeRTOS V11.3.2).

| 훅 | 파일 · 함수 | 문맥 | 켜는 설정 |
|---|---|---|---|
| idle 루프 | `tasks.c` `prvIdleTask()` (`prvCreateIdleTasks()`가 생성) | idle 태스크 | 항상 |
| 메모리 회수 | `tasks.c` `prvCheckTasksWaitingTermination()` → `prvDeleteTCB()` | idle 태스크 | `INCLUDE_vTaskDelete` |
| `vApplicationIdleHook` | `tasks.c` `prvIdleTask()` | idle 태스크 | `configUSE_IDLE_HOOK` |
| `vApplicationTickHook` | `tasks.c` `xTaskIncrementTick()` (두 곳) | SysTick ISR | `configUSE_TICK_HOOK` |
| `vApplicationMallocFailedHook` | `heap_N.c` `pvPortMalloc()` 끝 | 호출 태스크 (스케줄러 재개 후) | `configUSE_MALLOC_FAILED_HOOK` |
| `vApplicationStackOverflowHook` | `include/stack_macros.h` `taskCHECK_FOR_STACK_OVERFLOW()` ← `vTaskSwitchContext()` | PendSV (인터럽트 막힘) | `configCHECK_FOR_STACK_OVERFLOW` 1/2 |
| `vApplicationDaemonTaskStartupHook` | `timers.c` `prvTimerTask()` 첫머리 | timer 태스크 | `configUSE_DAEMON_TASK_STARTUP_HOOK` |
| run-time 회계 | `tasks.c` `vTaskSwitchContext()` | PendSV | `configGENERATE_RUN_TIME_STATS` |
| `traceTASK_SWITCHED_OUT/IN` | `tasks.c` `vTaskSwitchContext()` | PendSV | 매크로 정의 |
| `traceMALLOC/FREE` | `heap_N.c` | 호출 태스크 (스케줄러 정지 중) | 매크로 정의 |
| `configASSERT` | 커널 전역 | 어디서나 | 매크로 정의 |
| `vApplicationGetIdleTaskMemory` | `tasks.c` `prvCreateIdleTasks()` | 스케줄러 시작 전 | 정적 할당 + `configKERNEL_PROVIDED_STATIC_MEMORY 0` |

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">컨텍스트 스위치 한 번에 훅이 끼어드는 순서 (ARM_CM3 + tasks.c)</text>
<rect x="30" y="38" width="380" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="220" y="65" font-size="12" text-anchor="middle">PendSV_Handler: R4–R11 저장, pxTopOfStack = PSP</text>
<rect x="430" y="38" width="230" height="44" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="545" y="65" font-size="12" text-anchor="middle">port.c (C2)</text>
<line x1="220" y1="82" x2="220" y2="93" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="30" y="93" width="380" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="120" font-size="12" text-anchor="middle">traceTASK_SWITCHED_OUT()</text>
<rect x="430" y="93" width="230" height="44" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
<text x="545" y="120" font-size="12" text-anchor="middle">나가는 태스크 — 사용자 trace 훅</text>
<line x1="220" y1="137" x2="220" y2="148" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="30" y="148" width="380" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="220" y="175" font-size="12" text-anchor="middle">ulRunTimeCounter += now − ulTaskSwitchedInTime</text>
<rect x="430" y="148" width="230" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="545" y="175" font-size="12" text-anchor="middle">configGENERATE_RUN_TIME_STATS</text>
<line x1="220" y1="192" x2="220" y2="203" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="30" y="203" width="380" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="220" y="230" font-size="12" text-anchor="middle">taskCHECK_FOR_STACK_OVERFLOW()</text>
<rect x="430" y="203" width="230" height="44" rx="6" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
<text x="545" y="230" font-size="12" text-anchor="middle">방식 1: SP / 방식 2: +카나리</text>
<line x1="220" y1="247" x2="220" y2="258" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="30" y="258" width="380" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="220" y="285" font-size="12" text-anchor="middle">taskSELECT_HIGHEST_PRIORITY_TASK()</text>
<rect x="430" y="258" width="230" height="44" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="545" y="285" font-size="12" text-anchor="middle">CLZ로 최고 우선순위 리스트 (C1)</text>
<line x1="220" y1="302" x2="220" y2="313" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="30" y="313" width="380" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="340" font-size="12" text-anchor="middle">traceTASK_SWITCHED_IN()</text>
<rect x="430" y="313" width="230" height="44" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
<text x="545" y="340" font-size="12" text-anchor="middle">들어오는 태스크 — 사용자 trace 훅</text>
<line x1="220" y1="357" x2="220" y2="368" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="30" y="368" width="380" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="220" y="395" font-size="12" text-anchor="middle">PendSV_Handler: 새 PSP에서 R4–R11 복원, BX LR</text>
<rect x="430" y="368" width="230" height="44" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="545" y="395" font-size="12" text-anchor="middle">port.c (C2)</text>
<text x="340" y="432" font-size="12" text-anchor="middle">가운데 다섯 줄이 vTaskSwitchContext() — 인터럽트가 막힌 PendSV 안에서 돈다</text>
</svg>
```

그림 9 — 한 번의 컨텍스트 스위치 안에서 훅들이 끼어드는 순서. PendSV 어셈블리(C2)가 나가는 태스크의 R4–R11을 저장하고 `pxTopOfStack`을 갱신한 **뒤에** `vTaskSwitchContext()`를 부르므로, 스택 검사 방식 1이 보는 SP는 "문맥까지 다 쌓은" 값이다. run-time 회계와 스택 검사는 둘 다 `pxCurrentTCB`가 아직 나가는 태스크일 때 일어나고, `taskSELECT_HIGHEST_PRIORITY_TASK()`가 `pxCurrentTCB`를 바꾼 뒤에야 `traceTASK_SWITCHED_IN()`이 불린다.

이 순서에서 실무적으로 중요한 사실 세 가지:

- **모든 PendSV 훅 비용은 컨텍스트 스위치 지연에 그대로 더해진다.** 방식 2 스택 검사(워드 4개 비교), run-time 카운터 읽기(`board_now_us()` 함수 호출과 나눗셈), trace 훅. 스위치가 잦은 시스템(드론 FC: IMU 8 kHz → 태스크 깨움 → 초당 수만 번)에서는 카운터 읽기를 레지스터 하나 읽기로 줄여야 한다(DWT CYCCNT나 1 MHz 타이머 값을 나눗셈 없이).
- **훅이 보는 TCB 필드**: 스택 검사는 `pxTopOfStack`(word 0)과 `pxStack`(word 12), 회계는 `ulRunTimeCounter`. TCB가 메모리 손상으로 덮이면 훅이 오히려 엉뚱하게 터진다 — §5.3에서 훅 안에서 커널 API를 부르지 말라고 한 이유다.
- 깊이 이어지는 노트: 스케줄러가 다음 태스크를 고르는 CLZ 경로는 C1, PendSV 어셈블리는 C2, tickless 진입(그림 2의 ⑤)은 C6, 스택 산정·하드웨어 가드는 F3·F4, heap 구현은 F2, 우선순위 상속(§10의 use-after-free 경로 `xTaskPriorityInherit`)은 D3, 크래시 로그(훅에서 noinit RAM에 남기기)는 M 모듈.

---

## 13. 실무에서 다시 보기

### 13.1 드론 비행 제어기 — CPU 예산 보고와 "idle 도달" 워치독

비행 제어기(STM32H7급, IMU 8 kHz, rate loop 8 kHz, attitude 1 kHz, 텔레메트리, 로깅)에서 이 노트의 장치는 이렇게 쓰인다.

- **CPU 예산**: rate loop 태스크의 실행 시간과 전체 부하를 텔레메트리로 지상국에 보낸다(PX4의 `top`, Betaflight의 CLI `tasks` 명령이 같은 구실을 한다 — 각각 자체 구현이다). FreeRTOS 기반이면 §9의 델타 방식 `uxTaskGetSystemState()` 모니터를 1 Hz로. 카운터는 **DWT CYCCNT**(실칩, 나눗셈 없이)로 — §8의 aliasing 때문에 tick 기반은 금지. 주의: IMU DMA 완료 ISR이 무거우면 run-time stats의 IDLE%가 과대평가된다(§2.4). ISR은 별도로 입구·출구 타임스탬프를 잰다.
- **"idle 도달" 워치독**: 하드웨어 워치독을 rate loop에서 차면, rate loop는 살아 있는데 낮은 우선순위 태스크(로깅, 파라미터 저장)가 굶는 상황을 못 잡는다. 패턴: idle hook이 `idle_seen = 1`을 세우고, 워치독을 차는 감시 태스크가 "모든 태스크 체크인 + idle_seen"을 확인한 뒤에만 찬다. idle이 100 ms 동안 한 번도 못 돌면 "CPU 포화"로 판단해 리셋 대신 로깅을 끄는 등의 열화 모드로 간다.

```c
/* idle 도달 + 태스크 체크인 워치독 — 개념 코드 (빌드만 고려, 실행 안 함) */
static volatile uint32_t idle_seen, checkin;           /* 비트마다 태스크 하나 */
void vApplicationIdleHook(void) { idle_seen = 1; }
void task_checkin(uint32_t bit) { taskENTER_CRITICAL(); checkin |= bit; taskEXIT_CRITICAL(); }
static void wdog_task(void *pv)                         /* 가장 높은 우선순위, 50 ms 주기 */
{
    (void)pv; TickType_t last = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(50));
        if (checkin == ALL_TASKS && idle_seen) { iwdg_kick(); checkin = 0; idle_seen = 0; }
        else if (checkin == ALL_TASKS) cpu_saturated_count++;   /* 다 돌지만 idle이 굶음 */
    }
}
```

(위 코드는 `ALL_TASKS`, `iwdg_kick()`, `cpu_saturated_count`가 보드별 정의라 이 랩에서 실행하지 않았다.)

### 13.2 로봇 관절 모터 컨트롤러 — tick hook을 쓰지 않는 이유

FOC 전류 루프(20 kHz)는 PWM 타이머 ISR에서 돈다. 1 kHz tick hook에 "속도 루프"를 넣고 싶은 유혹이 있지만, tick hook은 SysTick ISR 안이라 그 길이만큼 모든 태스크와 낮은 우선순위 인터럽트의 지터가 된다. 그리고 tick은 소프트웨어 시간 기준이라 tickless(C6)를 켜는 순간 사라진다. 속도 루프는 별도 하드웨어 타이머 ISR이나 높은 우선순위 태스크 + `vTaskDelayUntil`로 둔다. tick hook에는 "tick마다 카운트다운하는 소프트웨어 워치독" 정도만.

### 13.3 센서 허브 MCU — heap을 없애고 훅으로 증명하기

Linux SoC 옆에서 IMU 타임스탬프를 찍는 센서 허브는 몇 달씩 꺼지지 않는다. 장기 운영의 적은 누수와 단편화(§4, §9의 계단형 heap)다. 정책: `configSUPPORT_DYNAMIC_ALLOCATION 0`(F1)로 아예 heap을 없애거나, heap_1(할당만, free 없음)을 쓰고 부팅 끝에 `xPortGetFreeHeapSize()`를 기록해 둔 뒤 이후 값이 바뀌면 assert. malloc-failed hook은 "절대 불리면 안 되는 훅"으로 두고 불리면 크래시 로그 + 리셋.

### 13.4 Don의 SSD 펌웨어와 나란히 놓기

| SSD 펌웨어 (bare-metal executive) | RTOS |
|---|---|
| main 루프의 "할 일 없음" 분기 + idle 카운터 | idle 태스크 + idle hook (§1, §2) |
| 코어별 busy/idle 사이클 카운터로 성능 리포트 | run-time stats + top 모니터 (§8, §9) |
| `ASSERT()` → 레지스터 덤프를 로그 영역에 → 정지 | `configASSERT` 훅, `vApplicationStackOverflowHook` → noinit RAM (§5, §6) |
| 버퍼 풀 고갈 처리 경로 | malloc-failed hook (§4) |
| 이벤트 로그 링버퍼 (명령 도착·NAND 완료 시각) | trace 매크로 (§7) |
| job은 상태를 전역에 두니 "작업 취소" = 상태 리셋 | 태스크 삭제는 자원을 정리하지 않는다 — 협조적 종료 (§10) |

---
## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| idle hook에서 `vTaskDelay`·무한 대기 API 호출 | 태스크가 지연보다 일찍 깨어남, 또는 랜덤 HardFault | Ready 태스크 0개 → CLZ(0)로 `pxReadyTasksLists[-1]`을 읽음 (§1.5) | idle hook은 비블로킹만. 블록이 필요하면 우선순위 0 태스크로 |
| 우선순위 ≥ 1에 바쁜 루프 배경 태스크 | 자기 삭제 태스크 메모리가 안 돌아옴, idle hook·저전력이 멈춤, run-time stats IDLE 0% | idle이 영원히 Ready 대기 (A4 §7, §7.2) | 배경 바쁜 루프는 우선순위 0 + `configIDLE_SHOULD_YIELD 1` |
| idle hook에 WFI를 넣고 idle 카운터로 CPU% 계산 | 부하가 tick 단위로 양자화, 최대 수 %p 틀림 | 카운터가 "깨어난 횟수"를 셈 (§2.3) | 저전력 장비는 run-time stats(idle 시간)로 |
| run-time stats IDLE%를 "CPU 여유"로 보고 | ISR이 무거운데 여유가 많아 보임 | ISR 시간이 끊긴 태스크(대개 IDLE)에 붙음 (§2.4) | ISR 시간을 따로 재거나 idle 카운터 방식과 교차 확인 |
| run-time 카운터로 tick 카운트 사용 | tick 직후 짧게 도는 태스크가 0%, IDLE 과대 | 해상도 < 실행 길이 + tick 동기 aliasing (§8.2) | tick보다 10~100배 빠른 독립 타이머 (CYCCNT, 1 MHz 타이머) |
| 누적 퍼센트(`ulTaskGetRunTimePercent`, analyzer CPU%)를 현재 부하로 보고 | 부하가 바뀌어도 숫자가 천천히 움직임 | 부팅 이후 누적 | 창 단위 델타 (§9) |
| tick hook에서 일반 API(`xQueueSend`, `xTaskNotifyGive`) | `port.c:438` configASSERT, assert 끈 빌드에선 끊긴 태스크가 엉뚱하게 Blocked | ISR 문맥 (§3.2) | FromISR API만, 짧게 |
| 스택 검사 방식 1만 켬 | 넘쳤다 돌아오는 경로를 못 잡음, 다른 태스크가 랜덤 크래시 | 스위치 순간 SP만 봄 (§5.2 ①) | 방식 2 + MPU 가드 / PSPLIM (F3, F4) |
| 큰 지역 배열을 앞부분만 사용 | 훅 없이 이웃 메모리 손상 | 카나리를 건너뛰는 넘침 (§5.2 ③) | 하드웨어 가드, `-fstack-usage`로 정적 산정 (F3) |
| 남의 태스크를 `vTaskDelete` | heap 누수, mutex 영구 잠김, 상속 때 해제된 TCB에 쓰기 | 커널은 태스크가 얻은 자원을 추적 안 함 (§10) | 협조적 종료: 신호 → 스스로 정리 → `vTaskDelete(NULL)` |
| `xPortGetFreeHeapSize()`만 모니터링 | "여유 64 KB"인데 2 KB 할당 실패 | 단편화 (§4.2) | `xSizeOfLargestFreeBlockInBytes`, `xMinimumEverFreeBytesRemaining`도 같이 |
| HWM 단위를 섞음 | FreeRTOS와 Zephyr 표에서 스택 여유가 4배 차이 | FreeRTOS words, Zephyr bytes (§11.3) | 표에 단위를 같이 찍는다 |

---

## 15. 면접에서 이렇게 말한다

**Q.** What does the idle task do in FreeRTOS, and why must it never block?

**A.** idle은 우선순위 0에서 "Ready 태스크가 최소 하나"를 보장하는 태스크다. 매 바퀴 자기 삭제한 태스크의 TCB·스택을 free하고, 같은 우선순위 0 태스크가 있으면 양보하고, idle hook을 부르고, tickless면 저전력에 들어간다. 블록하면 Ready가 0개가 되는데, 스케줄러의 O(1) 선택 코드는 그 경우를 방어하지 않아서 정의되지 않은 동작이 된다 — 실제로 CLZ(0)로 ready list 배열의 −1번째, 즉 delayed list를 읽어서 자고 있어야 할 태스크가 깨어나는 것을 봤다.

> The idle task runs at priority zero and guarantees there's always at least one ready task. Each loop it frees the TCB and stack of tasks that deleted themselves, yields to other priority-zero tasks if configIDLE_SHOULD_YIELD is set, calls the idle hook, and enters tickless sleep if enabled. It must never block, because then the ready set is empty and the scheduler's O(1) selection doesn't guard against that — I actually tried it on a Cortex-M3 and the CLZ of an empty ready bitmap indexed one list before the ready array, which happened to be the delayed list, so a sleeping task was woken early. Undefined behavior, no assert.

**꼬리 질문**: "So what can you put in the idle hook?" → 비블로킹·짧은 코드: idle 카운터, WFI, 워치독 "idle 도달" 플래그, 조금씩 하는 RAM self-test.

**Q.** How would you measure CPU load on an RTOS-based MCU?

**A.** 두 방법이 있다. idle hook 카운터를 부하 없을 때 보정해 두고 비율로 계산하는 방법은 ISR 시간까지 바쁨으로 잡지만, WFI나 tickless를 쓰면 깨진다. run-time stats는 자유 실행 타이머로 컨텍스트 스위치마다 태스크별 시간을 누적해서 태스크별 분해가 되지만, ISR 시간이 끊긴 태스크에 붙고 타이머 해상도가 tick보다 충분히 빨라야 한다. 실무에서는 run-time stats를 CYCCNT 같은 빠른 카운터로 켜고, 창 단위 델타로 보고하고, 무거운 ISR은 따로 잰다.

> Two classic ways. One is an idle-loop counter calibrated at boot with no load; load is one minus the ratio. It naturally counts ISR time as busy, but it breaks as soon as you put WFI in the idle hook or use tickless — it then counts wakeups, not idle time. The other is FreeRTOS run-time stats: a free-running counter sampled at every context switch and added to the outgoing task. That gives a per-task breakdown and works with low power, but ISR time is charged to whatever task was interrupted, and the counter must be 10 to 100 times faster than the tick — with a 1 ms counter I measured a tick-aligned 26% task showing as zero. In production I use a cycle counter, report per-window deltas, and timestamp heavy ISRs separately.

**Q.** How does FreeRTOS detect stack overflow? When does it miss?

**A.** `configCHECK_FOR_STACK_OVERFLOW` 1은 컨텍스트 스위치 때 나가는 태스크의 저장된 SP가 스택 경계 밖인지만 본다. 2는 거기에 스택 바닥 16바이트의 0xa5 패턴이 살아 있는지도 본다. 1은 깊이 넘쳤다가 돌아온 뒤 스위치되면 못 잡고, 2는 그건 잡지만 바닥 16바이트를 건너뛰고 그 아래만 쓴 넘침(큰 지역 배열의 앞부분만 쓰는 경우)은 못 잡는다. 둘 다 사후 검사라 넘친 순간과 검출 사이에 이웃 메모리가 이미 망가진다. 그래서 MPU 가드나 Armv8-M PSPLIM 같은 하드웨어 검사를 같이 쓴다.

> Method 1 checks, at each context switch, whether the outgoing task's saved stack pointer is beyond its stack limit. Method 2 additionally checks that the last 16 bytes of the stack still hold the 0xA5 fill pattern. Method 1 misses an overflow that happens deep in a call chain and returns before the next switch; method 2 catches that through the overwritten canary. Both miss an overflow that skips the canary — for example a large local array where only the low indices are written, which land below the stack. And both are after-the-fact, so a neighbor may already be corrupted. I reproduced all three cases; the real fix is a hardware guard — an MPU guard region or PSPLIM on v8-M.

**꼬리 질문**: "What do you do inside the hook?" → 인터럽트가 막힌 PendSV 안이고 TCB가 이미 깨졌을 수 있으니 API 없이 핸들·이름·PSP를 noinit RAM에 복사하고 리셋.

**Q.** What happens to resources when you delete a task?

**A.** 커널이 그 태스크를 위해 만든 TCB와 스택만 돌아온다(남을 지우면 즉시, 자기 삭제면 idle이 돌 때). 태스크가 malloc한 메모리, 쥐고 있던 mutex, 드라이버 상태는 그대로 남는다. mutex는 holder가 죽은 TCB를 가리켜 영원히 잠기고, 더 높은 우선순위가 기다리면 상속 때문에 커널이 해제된 TCB에 우선순위를 써 넣는 use-after-free까지 생긴다. 그래서 태스크는 신호를 받아 스스로 정리하고 끝나게 하거나, 아예 지우지 않는다.

> Only what the kernel allocated for it — the TCB and the stack — and only if it was created dynamically. If another task deletes it, that memory comes back immediately; if it deletes itself, the idle task frees it later. Anything the task acquired is not tracked: its malloc'd buffers leak, a mutex it held stays owned by a dead TCB forever, and if a higher-priority task then waits on that mutex, priority inheritance writes into the freed TCB — I saw the priority field in freed memory change from 2 to 3. So the rule is cooperative shutdown: signal the task, let it release everything, and have it call vTaskDelete(NULL). Most production firmware never deletes tasks at all.

**Q.** What can and can't you do in the tick hook?

**A.** tick hook은 SysTick ISR 안에서 tick마다, 스케줄러가 정지돼 있어도 불린다. 그러니 FromISR API만 쓰고, 아주 짧아야 한다(그 길이가 시스템 전체 지터가 된다). FromISR API로 높은 우선순위 태스크를 깨우면 `xTaskIncrementTick`이 훅 뒤에 yield pending을 보고 스위치를 요청하므로 woken 인자는 NULL이어도 된다. 일반 API를 부르면 Cortex-M 포트에서는 `vPortEnterCritical`의 assert에 걸린다.

> It runs inside the SysTick interrupt, once per tick, even while the scheduler is suspended — the tick count is pended but the hook still fires. So only FromISR APIs, and keep it very short since its duration adds to every task's jitter. If a FromISR call unblocks a higher-priority task, xTaskIncrementTick checks the yield-pending flag after the hook and requests a context switch, so you can pass NULL for the woken flag. Calling a normal API there trips the configASSERT in the Cortex-M port's vPortEnterCritical, which checks that you're not in an exception.

**Q.** How do run-time stats work internally, and what limits their accuracy?

**A.** `vTaskSwitchContext`에서 카운터를 읽어 지난 switch-in 이후의 차이를 나가는 태스크의 `ulRunTimeCounter`에 더한다. 샘플링이 스위치 때만이라 ISR은 끊긴 태스크 몫이 되고, 해상도보다 짧은 실행은 0이 될 수 있고, 32비트 카운터는 wrap하면 누적값이 틀어진다(1 us면 약 71분). 그래서 빠른 카운터, 64비트 또는 창 단위 델타, ISR 별도 측정이 답이다.

> At every context switch the kernel reads the run-time counter and adds the delta since the last switch-in to the outgoing task's ulRunTimeCounter. Accuracy is limited by three things: sampling only at switches, so ISR time goes to the interrupted task; counter resolution, since a run shorter than one count can add zero — especially when it's synchronized to the tick; and wraparound of a 32-bit counter, about 71 minutes at 1 MHz, after which cumulative values are meaningless. Use a fast counter, compute per-window deltas or a 64-bit counter, and measure ISRs separately.

**Q.** Compare FreeRTOS and Zephyr on idle and diagnostics.

**A.** Zephyr idle 스레드는 메모리 회수도 사용자 훅도 없이 매 바퀴 `k_cpu_idle()`(WFI)을 하고, tickless와 PM 서브시스템이 저전력을 맡는다. 진단은 Kconfig로 켠다: `CONFIG_THREAD_RUNTIME_STATS`와 `k_thread_runtime_stats_get`, idle은 `idle_cycles`, 스택은 `CONFIG_INIT_STACKS` + `k_thread_stack_space_get`(바이트), thread analyzer, `CONFIG_STACK_SENTINEL` 또는 MPU 가드. 치명 오류는 `k_sys_fatal_error_handler` 하나로 모이고, return하면 그 스레드만 abort된다.

> Zephyr's idle thread doesn't reclaim memory or call a user hook; it just calls k_cpu_idle, which is WFI on Cortex-M, and tickless plus the PM subsystem handle low power. Diagnostics are Kconfig options: THREAD_RUNTIME_STATS with k_thread_runtime_stats_get, idle_cycles for the idle share, INIT_STACKS with k_thread_stack_space_get in bytes, the thread analyzer, and either a stack sentinel or an MPU stack guard. All fatal errors funnel into k_sys_fatal_error_handler; if you return from it, only the faulting thread is aborted. I ran the same top-style monitor on both kernels on the same QEMU board and the numbers matched within a percent.

---

## 16. 직접 해보기

1. **개념** — `configIDLE_SHOULD_YIELD 1`, 우선순위 0 사용자 태스크 A·B 둘, time slicing 켬. tick 하나 동안 idle → A → B 순서일 때 A가 받는 시간은 B보다 많은가 적은가?
   정답/힌트: 적다. idle이 slice 앞부분을 쓰고 양보하면 A는 그 slice의 나머지만 쓴다(§1.3 주의). idle 한 바퀴가 짧으면 차이는 작다.
2. **코드** — 예제 1을 고쳐 `bg`를 우선순위 1로 올리면 `oneshot`의 메모리는 언제 회수되나? 직접 돌려 `t_freed`를 확인하라.
   정답/힌트: 영원히 안 된다(`t_freed = 0`). idle이 Ready인 채로 굶는다. A4 §3와 같은 결론.
3. **측정** — 예제 3의 보정 창을 200 ms에서 20 ms로 줄이면 CPU 미터 오차가 어떻게 변하나? 그리고 보정 중에 예제 4의 TIMER0 ISR이 이미 돌고 있다면?
   정답/힌트: icount에서는 거의 그대로, 실칩에서는 짧을수록 흔들린다. 보정 중 ISR이 돌면 N이 작게 잡혀 이후 부하가 전부 과소평가된다 — 보정은 인터럽트를 켜기 전에.
4. **코드** — `traceMALLOC`에서 `pvReturn == NULL`이면 요청 크기와 `pcTaskGetName(NULL)`을 링버퍼에 남기고, malloc-failed hook이 그 기록을 찍게 바꿔라(예제 6 기반). `traceMALLOC`은 스케줄러 정지 중이라는 점에 주의.
   정답/힌트: heap_4는 실패해도 `traceMALLOC( pvReturn, xAllocatedBlockSize )`를 부르는데 이때 크기는 0이다(블록을 못 찾았으니). 원래 요청 크기를 알려면 `pvPortMalloc`을 감싸는 래퍼에서 크기를 전역에 적어 두고 부른다(heap_4.c에는 API 진입용 `traceENTER_` 매크로가 없다 — `grep trace heap_4.c`로 확인하면 `traceMALLOC`·`traceFREE` 둘뿐).
5. **측정** — 예제 10에 카운터 `board_now_us()/300`(300 us 해상도)을 추가해 `burst`가 몇 %로 보이는지 예측하고 돌려 보라.
   정답/힌트: 300 us 일이 카운터 1칸과 같아서 들어올 때의 위상에 따라 0칸 또는 1칸 — tick과 카운터 경계의 위상 관계가 고정이므로 평균이 실제(26%)에 수렴하지 않을 수 있다. 직접 확인하는 것이 목적.
6. **코드** — 예제 7을 확장해 태스크별 "Ready가 된 순간부터 Running이 될 때까지"(스케줄링 지연)의 최대값을 재라. 필요한 trace 매크로는?
   정답/힌트: `traceMOVED_TASK_TO_READY_STATE(pxTCB)`에서 시각을 TCB별로 적고 `traceTASK_SWITCHED_IN()`에서 빼면 된다. 이미 Ready인 채 선점당한 경우를 구분하려면 `traceTASK_SWITCHED_OUT()`에서 상태도 본다.
7. **Zephyr** — 예제 13에 `CONFIG_SCHED_THREAD_USAGE_ANALYSIS=y`를 켜고 `ctl` 스레드 `peak_cycles`를 찍어 예제 9의 `max slice`(1009 us)와 비교하라.
   정답/힌트: 25 MHz cycle 단위라 약 25000 cycles 근처가 나와야 한다. 다르면 무엇이 "한 번의 실행 창"으로 세어지는지 `kernel/usage.c`의 `current`·`longest` 갱신 조건을 읽어 보라.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| idle task | 유휴 태스크 | 우선순위 0, 항상 Ready인 커널 태스크. FreeRTOS에서는 메모리 회수 담당 |
| hook | 훅 | 커널이 정해진 지점·문맥에서 불러 주는 사용자 함수나 매크로 |
| idle hook | idle 훅 | `vApplicationIdleHook`, idle 루프 한 바퀴마다 호출, 블록 금지 |
| tick hook | tick 훅 | `vApplicationTickHook`, SysTick ISR 안에서 tick마다 호출 |
| malloc-failed hook | 할당 실패 훅 | `pvPortMalloc`이 NULL을 돌려주기 직전 호출 |
| stack-overflow hook | 스택 넘침 훅 | 컨텍스트 스위치 때 나가는 태스크의 SP·카나리 검사 실패 시 호출 |
| daemon task | 데몬 태스크 | software timer 서비스 태스크 `Tmr Svc`, startup hook이 여기서 1번 돈다 |
| `configASSERT` | 커널 assert | 커널 전역의 불변식 검사 매크로, 정의 안 하면 사라진다 |
| trace macro | trace 매크로 | `traceTASK_SWITCHED_IN` 등, 커널 소스 안에서 펼쳐지는 빈 매크로 |
| run-time stats | 실행 시간 통계 | 스위치마다 자유 실행 카운터 차이를 태스크에 누적 |
| aliasing | 박자 맞물림 오차 | 측정 시계와 사건이 동기돼 평균내도 사라지지 않는 오차 (§8) |
| calibration | 보정 | 부하 0일 때 idle 카운터 증가량을 재 두는 것 |
| high water mark (HWM) | 최고 수위 | 지금까지 가장 깊이 쓴 스택 깊이의 여유분 (FreeRTOS words, Zephyr bytes) |
| canary | 카나리 | 스택 바닥에 둔 알려진 패턴(0xa5, Zephyr 0xF0F0F0F0), 깨지면 넘침 |
| MPU guard | MPU 가드 | 스택 아래 접근 금지 영역, 넘치는 명령에서 즉시 fault |
| fragmentation | 단편화 | free 총량은 큰데 연속 구멍이 작아 할당이 실패하는 상태 |
| cooperative shutdown | 협조적 종료 | 신호를 받은 태스크가 스스로 자원을 정리하고 끝나는 방식 |
| use-after-free | 해제 후 사용 | 해제된 메모리를 읽거나 씀 — §10에서 커널이 죽은 TCB에 씀 |
| `k_sys_fatal_error_handler` | Zephyr 치명 오류 훅 | 모든 fatal 경로가 모이는 weak 함수, return하면 스레드만 abort |
| `idle_cycles` | Zephyr idle 사이클 | `k_thread_runtime_stats_all_get`이 주는 idle 누적 cycle |

---

## 18. 요약 & 체크리스트

idle 태스크는 "할 일 없을 때 도는 루프" 이상이다. FreeRTOS에서는 자기 삭제 태스크의 청소부이고, idle hook과 tickless의 출발점이며, "Ready ≥ 1"이라는 스케줄러 불변식 자체다 — 블록하면 CLZ(0)이 delayed list를 ready list로 읽는 조용한 고장이 난다. 훅은 커널이 비워 둔 콘센트지만 문맥이 제각각이다: idle hook은 idle 태스크, tick hook은 SysTick ISR(정지 중에도), malloc-failed는 호출 태스크, stack-overflow와 switch trace는 인터럽트가 막힌 PendSV. CPU 사용률은 idle 카운터(ISR 포함, WFI와 상극)나 run-time stats(태스크별, ISR은 끊긴 태스크 몫, 해상도가 tick보다 훨씬 빨라야 함 — 1 ms 카운터에서 26% 태스크가 0%)로 재고, 운영에서는 창 단위 델타 + HWM + heap 최저 수위를 함께 찍는 top 모니터를 둔다. 스택 검사 방식 1은 넘쳤다 돌아온 경우를, 방식 2는 카나리를 건너뛴 경우를 놓친다. `vTaskDelete`는 TCB와 스택만 돌려주고, 태스크가 얻은 heap·mutex는 남기며, mutex 상속이 해제된 TCB에 쓰는 use-after-free까지 만든다. Zephyr는 idle이 WFI만 하고, 진단은 Kconfig(runtime stats, analyzer, sentinel/MPU guard)와 `k_sys_fatal_error_handler` 하나로 모은다.

- [ ] `prvIdleTask()`의 한 바퀴(회수 → 양보 → 훅 → tickless)를 소스 이름으로 말할 수 있다
- [ ] idle이 블록하면 왜 정의되지 않은 동작인지 CLZ와 ready list 배열로 설명한다
- [ ] `configIDLE_SHOULD_YIELD`와 우선순위 0 vs 1 배경 태스크의 차이를 측정 숫자로 말한다
- [ ] idle 카운터 CPU 미터를 만들고, WFI·ISR에서 어떻게 틀리는지 안다
- [ ] tick hook이 ISR 문맥이고 스케줄러 정지 중에도 불린다는 것, FromISR만 써야 하는 이유를 안다
- [ ] malloc-failed hook이 불리는 문맥과 단편화로 "남았는데 실패"하는 상태를 heap stats로 진단한다
- [ ] 스택 검사 방식 1·2의 정확한 조건과 각각 놓치는 장면을 그림으로 그릴 수 있다
- [ ] daemon startup hook과 configASSERT 훅(파일·줄·LR → addr2line)을 쓸 수 있다
- [ ] trace 매크로로 스위치 횟수·최장 slice·할당 소유자를 기록할 수 있다
- [ ] run-time 카운터 해상도·aliasing·wrap 문제와 해결책(빠른 독립 카운터, 델타)을 설명한다
- [ ] `vTaskDelete`가 회수하지 않는 것과 협조적 종료 패턴을 안다
- [ ] Zephyr의 idle·runtime stats·thread analyzer·sentinel·MPU guard·fatal handler를 FreeRTOS와 짝지을 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (`/var/rtoslab/src/FreeRTOS-Kernel`): `tasks.c`(`prvIdleTask`, `prvCheckTasksWaitingTermination`, `prvDeleteTCB`, `vTaskDelete`, `xTaskIncrementTick`, `vTaskSwitchContext`, `uxTaskGetSystemState`, `ulTaskGetIdleRunTimeCounter`), `include/stack_macros.h`, `timers.c`(`prvTimerTask`), `portable/MemMang/heap_4.c`(`pvPortMalloc`, `vPortGetHeapStats`), `portable/GCC/ARM_CM3/port.c`(`vPortEnterCritical`), `include/FreeRTOS.h`(trace 매크로 기본값)
- FreeRTOS 문서: "Hook Functions" — https://www.freertos.org/a00016.html
- FreeRTOS 문서: "Run Time Statistics" — https://www.freertos.org/rtos-run-time-stats.html
- FreeRTOS 문서: "Stack Usage and Stack Overflow Checking" — https://www.freertos.org/Stacks-and-stack-overflow-checking.html
- FreeRTOS 문서: "Customisation (FreeRTOSConfig.h)" — `configIDLE_SHOULD_YIELD`, `configUSE_DAEMON_TASK_STARTUP_HOOK` 항목 — https://www.freertos.org/a00110.html
- Zephyr v4.4.2 소스 (`/var/rtoslab/zephyrproject/zephyr`): `kernel/idle.c`, `kernel/usage.c`, `kernel/thread.c`(`z_check_stack_sentinel`), `kernel/fatal.c`(`k_sys_fatal_error_handler`), `kernel/init.c`(idle 스레드 생성), `arch/arm/core/cortex_m/cpu_idle.c`, `include/zephyr/arch/arm/arch.h`(`MPU_GUARD_ALIGN_AND_SIZE`), `subsys/debug/Kconfig`(`STACK_SENTINEL`), `subsys/debug/thread_analyzer/`
- Zephyr 문서: "Thread Analyzer" — https://docs.zephyrproject.org/latest/services/debugging/thread-analyzer.html
- Zephyr 문서: "Threads" (runtime statistics, stack protection 항목 포함) — https://docs.zephyrproject.org/latest/kernel/services/threads/index.html
- Zephyr 문서: "Fatal Errors" — https://docs.zephyrproject.org/latest/kernel/services/other/fatal.html
- 이 노트의 모든 실행 예제: `~/.rtos_scratch/C9/` (소스), VM `/var/tmp/rtos/C9/` (빌드 산출물)
