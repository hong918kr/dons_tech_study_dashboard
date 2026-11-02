# D6. 소프트웨어 타이머 — 알람 목록과 그 알람을 울려 주는 비서(daemon task)

> **이 노트를 다 읽으면**: FreeRTOS 소프트웨어 타이머가 "명령 큐 + 타이머 서비스 태스크 + 정렬된 알람 목록 두 개"로 돌아가는 구조를 `timers.c` 함수 이름으로 설명할 수 있다 · 콜백에서 블록하면 왜 모든 타이머가 밀리는지, 데몬 우선순위를 낮추면 무엇이 늦어지는지를 측정 숫자로 말할 수 있다 · auto-reload가 drift 없이 돌고 밀리면 "몰아서" 호출된다는 것을 소스와 실측으로 보여 줄 수 있다 · Zephyr `k_timer`의 expiry 함수가 ISR에서 돈다는 것과 그 결과(블록 금지, +1 tick이 없는 duration, IRQ 잠금 시 시간 손실)를 설명하고 소프트웨어 타이머 / 주기 태스크 / 하드웨어 타이머 중 하나를 근거와 함께 고를 수 있다
> **왜 필요한가**: "타이머 콜백에서 블록하면?", "소프트웨어 타이머와 하드웨어 타이머는 언제 쓰나?", "RC 링크가 끊긴 걸 어떻게 감지하나?"는 드론·로봇 펌웨어 면접의 단골이다. 실무에서는 "LED가 가끔 멈칫한다", "failsafe가 100 ms가 아니라 113 ms 뒤에 걸렸다", "워치독이 멈춘 태스크를 못 잡았다"로 나타난다.
> **Don 기준 난이도**: SSD 펌웨어에서 1 ms 타이머 ISR로 명령 타임아웃 테이블을 훑던 일은 이미 했다 / 그 일을 RTOS가 "데몬 태스크 + 명령 큐"로 일반화한 방식, 명령의 기준 시각, auto-reload의 몰아치기, 데몬 우선순위의 효과, Zephyr의 ISR 문맥 expiry와 SysTick 드라이버의 전제 조건은 새로 배운다
> **선행 노트**: C3(Tmr Svc가 첫 태스크인 이유), C5(tick, delayed list 두 개와 wrap, `xTaskDelayUntil`), C6(tickless), C9(daemon startup hook, stack overflow 검사), EL F8(timer wheel, hrtimer). 함께 읽을 노트: D7(`xTimerPendFunctionCallFromISR`, Zephyr workqueue · `k_work_delayable`), E5(HW 타이머 직접 사용)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 쓰던 명령 타임아웃을 떠올려 보자. 호스트가 NVMe 명령을 수백 개 동시에 보내면, 펌웨어는 명령마다 "언제까지 끝나야 하나"를 기록해 두고, 1 ms 타이머 ISR(또는 메인 루프의 주기 작업)이 그 테이블을 훑어 마감이 지난 명령을 abort 경로로 보낸다. 하드웨어 타이머는 몇 개뿐인데 마감은 수백 개다. 그래서 **하드웨어 타이머 하나를 시계로 삼고, 소프트웨어로 "알람 목록"을 관리**했다.

RTOS의 **소프트웨어 타이머(software timer)**가 정확히 이것이다. 하드웨어 타이머가 아니라 커널의 tick(C5)을 시계로 쓰는, 개수 제한 없는(RAM이 허락하는 만큼) 알람이다. 알람마다 "언제 울릴지, 울리면 무엇을 부를지(callback), 한 번만인지 반복인지"를 적어 둔다. 그리고 **누가 그 콜백을 부르나**가 RTOS마다 다르다.

- **FreeRTOS**: 콜백을 부르는 전담 태스크가 있다. 이름은 **타이머 서비스 태스크(timer service task)**, 별명은 **daemon task**, 태스크 이름은 `"Tmr Svc"`다. 비서 한 명이 알람 목록을 들고 있다가 시각이 되면 콜백을 **차례로** 부른다고 생각하면 된다.
- **Zephyr**: 비서가 없다. 시스템 시계 인터럽트(SysTick ISR)가 알람 목록을 훑다가 **ISR 안에서 직접** expiry 함수를 부른다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 330">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="165.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">소프트웨어 타이머 = 알람 목록 + 비서</text>
<text x="500.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">하드웨어 타이머 = 주변장치 + ISR</text>
<rect x="20" y="40" width="290" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="165.0" y="61.5" font-size="13" text-anchor="middle">SysTick 1 kHz: 커널의 시계 (tick)</text>
<rect x="20" y="100" width="290" height="96" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165.0" y="128.2" font-size="12" text-anchor="middle" font-weight="bold">알람 목록 (active timer list)</text>
<text x="165.0" y="144.2" font-size="12" text-anchor="middle">hb 만료 tick 120 / 주기 5</text>
<text x="165.0" y="160.2" font-size="12" text-anchor="middle">rc_fs 만료 tick 140 / one-shot</text>
<text x="165.0" y="176.2" font-size="12" text-anchor="middle">deb 만료 tick 205 / one-shot</text>
<rect x="20" y="224" width="290" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165.0" y="250.1" font-size="13" text-anchor="middle">비서 = 타이머 서비스 태스크</text>
<text x="165.0" y="267.1" font-size="13" text-anchor="middle">(Tmr Svc, 보통 우선순위 높게)</text>
<line x1="165" y1="74" x2="165" y2="98" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="165" y1="196" x2="165" y2="222" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<text x="165.0" y="304.0" font-size="12" text-anchor="middle">시각이 되면 비서가 콜백을 "차례로" 부른다</text>
<text x="165.0" y="322.0" font-size="12" text-anchor="middle">정밀도 = 1 tick, 콜백 하나가 늦으면 뒤도 늦다</text>
<rect x="350" y="40" width="290" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="495.0" y="61.2" font-size="12" text-anchor="middle">TIMER0 / TIM1 비교 레지스터</text>
<text x="495.0" y="77.2" font-size="12" text-anchor="middle">(카운터 = 25 MHz cycle 단위)</text>
<rect x="350" y="120" width="290" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="495.0" y="141.2" font-size="12" text-anchor="middle">IRQ → ISR 진입 수 µs 안</text>
<text x="495.0" y="157.2" font-size="12" text-anchor="middle">(NVIC 우선순위로 선점)</text>
<rect x="350" y="200" width="290" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="495.0" y="221.2" font-size="12" text-anchor="middle">ISR: ADC 트리거, PWM 갱신</text>
<text x="495.0" y="237.2" font-size="12" text-anchor="middle">태스크에 notify</text>
<line x1="495" y1="90" x2="495" y2="118" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="495" y1="170" x2="495" y2="198" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<text x="495.0" y="284.0" font-size="12" text-anchor="middle">정밀도 = 하드웨어 클럭, 개수 = 주변장치 수</text>
<text x="495.0" y="302.0" font-size="12" text-anchor="middle">하나하나가 진짜 인터럽트</text>
</svg>
```
그림 1 — 소프트웨어 타이머(왼쪽)는 tick 하나를 시계로 삼아 알람을 몇 개든 만들고, 비서(FreeRTOS의 Tmr Svc)가 콜백을 차례로 부른다. 하드웨어 타이머(오른쪽)는 주변장치 하나가 알람 하나이고 만료가 곧 인터럽트다.

이 비유 하나에서 이 노트의 모든 주제가 나온다.

1. **비서는 한 명이다** → 콜백 하나가 오래 걸리거나 잠들면(블록하면) 뒤에 줄 선 모든 알람이 늦는다 (§4).
2. **비서도 태스크다** → 비서보다 우선순위 높은 태스크가 CPU를 쥐고 있으면 알람이 늦는다 (§5).
3. **비서에게 일을 시키려면 쪽지(명령)를 큐에 넣는다** → `xTimerStart()`는 타이머를 "시작"하는 게 아니라 "시작해 달라는 명령"을 보낸다. ISR도 마찬가지다 (§2, §3).
4. **알람 목록의 정밀도는 tick이다** → 1 kHz tick이면 1 ms보다 촘촘한 일은 못 한다. 8 kHz rate loop, 20 kHz FOC는 하드웨어 타이머의 일이다 (§8).

드론 비행 제어기라면 소프트웨어 타이머가 맡는 일은 이런 것들이다: 상태 LED 깜빡임(heartbeat), RC 링크 끊김 감지(패킷마다 타이머 리셋, 만료 = failsafe), 버튼·스위치 debounce, 배터리 전압 저속 샘플링, 텔레메트리 주기 송신 트리거, 통신 재시도 backoff. 공통점은 **짧고, 블록하지 않고, ms 단위 정밀도면 충분한 일**이라는 것이다.

### 실습 환경 한 줄

FreeRTOS 예제는 QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) + FreeRTOS V11.3.2, `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자)로 돌렸다. Zephyr 예제는 같은 가상 보드 + Zephyr v4.4.2, `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`을 명시하고 역시 `ICOUNT=1`로 돌렸다. 시간은 MPS2 FPGAIO 카운터(0x40028018, 25 MHz = 40 ns 단위)로 쟀다. **숫자는 QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·예외 스태킹 사이클 미모델링). 하지만 "몇 ms 늦는가", "몇 번 몰아서 불리는가" 같은 구조적인 결과는 실칩과 같다.

> 참고: 이 노트를 쓰는 동안 VM의 루트 파일시스템이 읽기 전용으로 바뀌어 빌드 산출물을 `/var/tmp/rtos/D6` 대신 tmpfs인 `/tmp/rtos_D6`에 두었다. Zephyr 빌드는 툴체인 캐시도 써야 해서 `-DUSER_CACHE_DIR=/tmp/rtos_D6/zcache`를 붙였다. 결과에는 영향이 없다.

모든 FreeRTOS 예제가 같이 쓰는 파일 세 개다. 템플릿 `FreeRTOSConfig.h`를 그대로 가져온 뒤 타이머 데몬 설정만 `-D`로 바꿀 수 있게 했다.

```c
/* D6 FreeRTOSConfig.h — 템플릿 설정 + 타이머 데몬 설정만 바꿀 수 있게 */
#ifndef D6_FREERTOS_CONFIG_H
#define D6_FREERTOS_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef configTIMER_TASK_PRIORITY
#undef configTIMER_QUEUE_LENGTH
#undef configTIMER_TASK_STACK_DEPTH
#ifndef D6_TMR_PRIO
#define D6_TMR_PRIO   ( configMAX_PRIORITIES - 1 )
#endif
#ifndef D6_TMR_QLEN
#define D6_TMR_QLEN   10
#endif
#ifndef D6_TMR_STACK
#define D6_TMR_STACK  512
#endif
#define configTIMER_TASK_PRIORITY      D6_TMR_PRIO
#define configTIMER_QUEUE_LENGTH       D6_TMR_QLEN
#define configTIMER_TASK_STACK_DEPTH   D6_TMR_STACK
#ifdef D6_NOTIMERS     /* 타이머 서비스 자체를 빼고 크기 비교 */
#undef configUSE_TIMERS
#undef INCLUDE_xTimerPendFunctionCall
#define configUSE_TIMERS 0
#define INCLUDE_xTimerPendFunctionCall 0
#endif
#ifdef D6_TRACE        /* 데몬이 명령을 꺼내는 순간을 앱 함수로 알린다 */
void d6_cmd_rx( void * t, long id );
#define traceTIMER_COMMAND_RECEIVED( t, id, v )    d6_cmd_rx( ( void * ) ( t ), ( long ) ( id ) )
#endif
#endif
```

```c
/* d6.h — D6 측정 도우미 (QEMU mps2-an385, FPGAIO 25 MHz 카운터) */
#ifndef D6_H
#define D6_H
#include "board.h"
#define CYC()  REG32(0x40028018u)                 /* 25 MHz = 40 ns */
#define US(c)  ((c) / 25u)
static inline void spin_us(uint32_t us)
{ uint32_t t0 = CYC(); while (CYC() - t0 < us * 25u) { } }
#endif
```

```sh
#!/bin/bash
# run.sh <src> <outname> [extra cflags...] — D6 FreeRTOS 빌드+실행 (ICOUNT=1)
S=/Users/donh/.rtos_scratch/D6; src=$1; out=$2; shift 2
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="$S/$src" OUT=${D6OUT:-/tmp/rtos_D6}/$out EXTRA_CFLAGS="$*" 2>&1 | grep -iE "warn|error"
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 ${D6OUT:-/tmp/rtos_D6}/$out/app.elf ${TMO:-60}
```

`run.sh ex2_cmdlat.c ex2_7 -DD6_TRACE -DD6_TMR_PRIO=7`처럼 부른다. `D6_TRACE`를 켜면 FreeRTOS의 trace 매크로 `traceTIMER_COMMAND_RECEIVED`(데몬이 명령을 큐에서 꺼낸 순간)가 앱 함수 `d6_cmd_rx()`를 부른다. trace 매크로 자체는 C9 §7에서 다뤘다.

---

## 1. 소프트웨어 타이머의 정의와 첫 예제

### 1.1 직관 — 부엌 타이머 여러 개

부엌 타이머를 생각하자. 하나는 "30분 뒤 한 번 울림"(오븐), 하나는 "20분마다 계속 울림"(물 주기 알림)이다. 앞의 것이 **one-shot**, 뒤의 것이 **auto-reload**다. 타이머에는 이름표(ID)를 붙일 수 있어서, 똑같은 "울리면 할 일"(콜백)을 여러 타이머가 같이 쓰면서도 누가 울렸는지 구분할 수 있다.

### 1.2 정의

FreeRTOS 소프트웨어 타이머 하나는 커널 안에서 `Timer_t` 구조체 하나다(§9에서 필드를 본다). 사용자가 정하는 것은 다섯 가지다.

| 항목 | 뜻 | API |
|---|---|---|
| 이름 | 디버깅용 문자열. 커널은 안 쓴다 | `pcTimerGetName()` |
| 주기(period) | tick 단위. 0이면 `configASSERT` | `xTimerChangePeriod()`, `xTimerGetPeriod()` |
| reload 모드 | `pdFALSE` = one-shot, `pdTRUE` = auto-reload | `vTimerSetReloadMode()`, `xTimerGetReloadMode()` |
| ID | `void *` 하나. 콜백이 "누가 울렸나"를 구분하거나 작은 상태를 담는 데 쓴다 | `pvTimerGetTimerID()`, `vTimerSetTimerID()` |
| 콜백 | `void cb(TimerHandle_t xTimer)` — 만료 시 **데몬 태스크가** 부른다 | `xTimerCreate()`의 마지막 인자 |

타이머의 상태는 둘뿐이다. **dormant**(만들어졌지만 돌지 않음 — 만든 직후, one-shot이 한 번 울린 뒤, Stop 뒤)와 **active**(알람 목록에 걸려 있음). `xTimerIsTimerActive()`가 이 상태를 돌려준다.

주요 API와 **"만료 시각을 무엇 기준으로 계산하나"**를 같이 정리한다. 이 열이 이 노트에서 가장 중요하다.

| API | 하는 일 | 만료 시각의 기준 |
|---|---|---|
| `xTimerCreate(name, period, autoReload, id, cb)` | heap에서 `Timer_t` 할당, dormant로 만든다 | — |
| `xTimerCreateStatic(..., &StaticTimer_t)` | 사용자가 준 메모리로 만든다 | — |
| `xTimerStart(t, xTicksToWait)` | dormant → active | **명령을 보낸 순간의 tick** + period |
| `xTimerReset(t, xTicksToWait)` | active면 만료를 뒤로 미룬다, dormant면 Start와 같다 | 명령을 보낸 순간 + period |
| `xTimerStop(t, xTicksToWait)` | active → dormant | — |
| `xTimerChangePeriod(t, newPeriod, xTicksToWait)` | 주기를 바꾸고 active로 만든다 | **데몬이 명령을 처리한 순간** + 새 period |
| `xTimerDelete(t, xTicksToWait)` | 동적 할당이면 free | — |
| `...FromISR(t, &woken)` | 위 명령들의 ISR 판 (Start/Stop/Reset/ChangePeriod) | ISR 시점의 `xTaskGetTickCountFromISR()` |
| `xTimerIsTimerActive(t)` | `ucStatus`의 active 비트를 읽는다 | — |
| `xTimerGetExpiryTime(t)` | 리스트 항목 값 = 다음 만료 tick | — |

`xTicksToWait`는 **타이머 시간이 아니다**. 명령 큐가 가득 찼을 때 "큐에 자리가 날 때까지 몇 tick 기다릴까"다. 처음 보는 사람이 가장 많이 헷갈리는 인자다.

### 1.3 예제 1 — one-shot / auto-reload / ID / 콜백이 도는 곳

무엇을 확인하나: 두 타이머(one-shot 30 ms, auto-reload 20 ms)를 만들고 Start · ChangePeriod · SetReloadMode · Reset을 차례로 부르며, 콜백이 **어느 태스크에서, 어떤 우선순위로** 도는지와 각 API 뒤의 상태·만료 시각을 찍는다. ID는 호출 횟수 카운터로 쓴다.

```c
/* D6 ex1: one-shot vs auto-reload, timer ID, 콜백이 도는 곳(Tmr Svc), 상태 질의 API */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define T() ((unsigned)xTaskGetTickCount())
static TimerHandle_t t_one, t_auto;
static void cb(TimerHandle_t t)
{
    uint32_t n = (uint32_t)pvTimerGetTimerID(t) + 1;     /* ID를 호출 횟수 카운터로 쓴다 */
    vTimerSetTimerID(t, (void *)n);
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    board_printf_raw("  tick %u: cb %s #%u  task='%s' prio=%u reload=%u\n", T(), pcTimerGetName(t),
                     (unsigned)n, pcTaskGetName(me), (unsigned)uxTaskPriorityGet(me),
                     (unsigned)xTimerGetReloadMode(t));
}
static void app(void *p)
{
    (void)p;
    t_one  = xTimerCreate("one",  pdMS_TO_TICKS(30), pdFALSE, (void *)0, cb);
    t_auto = xTimerCreate("auto", pdMS_TO_TICKS(20), pdTRUE,  (void *)0, cb);
    board_printf_raw("tick %u: created   active one=%u auto=%u\n", T(),
                     (unsigned)xTimerIsTimerActive(t_one), (unsigned)xTimerIsTimerActive(t_auto));
    xTimerStart(t_one, 0);
    xTimerStart(t_auto, 0);
    board_printf_raw("tick %u: started   active one=%u auto=%u  expiry one=%u auto=%u\n", T(),
                     (unsigned)xTimerIsTimerActive(t_one), (unsigned)xTimerIsTimerActive(t_auto),
                     (unsigned)xTimerGetExpiryTime(t_one), (unsigned)xTimerGetExpiryTime(t_auto));
    vTaskDelay(pdMS_TO_TICKS(65));
    board_printf_raw("tick %u: one active=%u (one-shot은 한 번 울리고 dormant)\n", T(),
                     (unsigned)xTimerIsTimerActive(t_one));
    xTimerChangePeriod(t_auto, pdMS_TO_TICKS(5), 0);    /* 새 주기는 '지금'부터 센다 */
    board_printf_raw("tick %u: ChangePeriod(5) -> auto expiry=%u period=%u\n", T(),
                     (unsigned)xTimerGetExpiryTime(t_auto), (unsigned)xTimerGetPeriod(t_auto));
    vTaskDelay(pdMS_TO_TICKS(12));
    vTimerSetReloadMode(t_auto, pdFALSE);               /* 다음 한 번만 울리고 멈춘다 */
    board_printf_raw("tick %u: SetReloadMode(pdFALSE)\n", T());
    vTaskDelay(pdMS_TO_TICKS(12));
    board_printf_raw("tick %u: auto active=%u\n", T(), (unsigned)xTimerIsTimerActive(t_auto));
    xTimerReset(t_one, 0);                              /* dormant 타이머에 Reset = Start */
    board_printf_raw("tick %u: Reset(one) -> expiry=%u\n", T(), (unsigned)xTimerGetExpiryTime(t_one));
    vTaskDelay(pdMS_TO_TICKS(10));
    xTimerReset(t_one, 0);                              /* 달리는 중 Reset = 만료 시각을 뒤로 */
    board_printf_raw("tick %u: Reset(one) again -> expiry=%u\n", T(), (unsigned)xTimerGetExpiryTime(t_one));
    vTaskDelay(pdMS_TO_TICKS(40));
    xTimerStop(t_auto, 0);
    board_printf_raw("tick %u: done\n", T());
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex1_basic.c ex1'
```

```text
tick 0: created   active one=0 auto=0
tick 0: started   active one=1 auto=1  expiry one=30 auto=20
  tick 20: cb auto #1  task='Tmr Svc' prio=7 reload=1
  tick 30: cb one #1  task='Tmr Svc' prio=7 reload=0
  tick 40: cb auto #2  task='Tmr Svc' prio=7 reload=1
  tick 60: cb auto #3  task='Tmr Svc' prio=7 reload=1
tick 65: one active=0 (one-shot은 한 번 울리고 dormant)
tick 65: ChangePeriod(5) -> auto expiry=70 period=5
  tick 70: cb auto #4  task='Tmr Svc' prio=7 reload=1
  tick 75: cb auto #5  task='Tmr Svc' prio=7 reload=1
tick 77: SetReloadMode(pdFALSE)
  tick 80: cb auto #6  task='Tmr Svc' prio=7 reload=0
tick 89: auto active=0
tick 89: Reset(one) -> expiry=119
tick 99: Reset(one) again -> expiry=129
  tick 129: cb one #2  task='Tmr Svc' prio=7 reload=0
tick 139: done
[board_exit] code=0
```

출력에서 볼 것:

- **콜백은 전부 `Tmr Svc` 태스크, 우선순위 7에서 돌았다.** 내 `app` 태스크(prio 2)가 아니다. 이 템플릿은 `configTIMER_TASK_PRIORITY = configMAX_PRIORITIES - 1 = 7`로 데몬을 가장 높게 둔다.
- `xTimerStart()`가 돌아온 **바로 다음 줄에서 이미 active=1**이다. app(prio 2)이 명령을 큐에 넣는 순간 높은 데몬(prio 7)이 깨어 선점하고, 명령을 처리한 뒤 다시 막히고 나서야 app이 돌아왔기 때문이다. 데몬이 app보다 낮으면 이 줄은 0이 된다(예제 2).
- tick 65에 `ChangePeriod(5)`를 부르자 다음 만료가 **70**이 됐다. 원래 격자(80)와 상관없이 "지금 + 5"다.
- tick 77에 reload 모드를 끄자 **이미 걸려 있던 80의 만료는 그대로 울리고**(reload=0으로 찍힘) 그 뒤로는 다시 걸리지 않았다(tick 89에 active=0).
- 달리는 one-shot에 Reset을 두 번 부르면 만료가 119 → 129로 밀린다. **"계속 리셋해서 안 울리게 한다"**가 바로 link-loss 감지와 debounce의 원리다(§10).

### 1.4 함정

- `xTimerStart(t, 0)`의 0은 "타이머 0 ms"가 아니라 "큐가 차 있으면 기다리지 말고 실패하라"다. 반환값 `pdFAIL`을 무시하면 타이머가 조용히 안 돈다.
- 스케줄러 시작 전(`main()` 안)에 `xTimerStart()`를 부르면 명령이 큐에 쌓이기만 하고 데몬이 시작될 때 처리된다. 이때 `xTicksToWait`는 무시되고 0으로 보낸다(`xTimerGenericCommandFromTask`의 `xTaskGetSchedulerState()` 분기). 큐 길이보다 많은 타이머를 `main()`에서 시작하면 넘친다.
- 콜백 시그니처의 인자는 **만료된 타이머의 핸들**이다. 같은 콜백을 여러 타이머가 쓰면 `pvTimerGetTimerID(xTimer)`로 구분한다.
- period 0은 `configASSERT( ( xTimerPeriodInTicks > 0 ) )`에 걸린다. `pdMS_TO_TICKS(0.5)` 같은 값이 0으로 내림되는 경우를 조심한다(C5 §1.4, §8의 예제 13).

---

## 2. 타이머 서비스 태스크와 명령 큐

### 2.1 왜 ISR이 아니라 태스크인가

FreeRTOS는 콜백을 tick ISR에서 부르지 않고 일부러 태스크 하나를 세웠다. 이유는 셋이다.

1. **tick ISR을 짧고 일정하게** 유지한다. tick ISR이 알람 목록을 훑고 콜백까지 부르면, tick 시간이 "그 순간 만료된 타이머 수 × 콜백 길이"에 비례해 들쭉날쭉해진다. 데몬 방식에서 tick ISR은 "데몬이 기다리던 시각이 됐다"만 알고 데몬을 ready로 옮긴다 — 지연 태스크 하나 깨우는 것과 같은 일이다(C5 §3).
2. **콜백이 태스크 문맥이다**. 일반 API(`xQueueSend`, `xTaskNotify`, `printf`류)를 `FromISR` 없이 부를 수 있다. 단, **블록만은 하면 안 된다**(§4).
3. **알람 목록을 데몬 혼자만 만진다**. `timers.c`의 주석이 "Only the timer service task is allowed to access these lists."라고 못 박는다. 그래서 목록을 고칠 때 인터럽트를 끌 필요가 없다. 다른 태스크와 ISR은 목록을 직접 못 만지고 **명령을 큐에 넣어** 부탁한다.

Don의 SSD 경험으로 말하면, ISR에서 플래그만 세우고 실제 처리는 메인 루프의 "타이머 처리 함수"가 하던 구조를 커널이 표준화한 것이다. 그 처리 함수가 독립된 스택과 우선순위를 가진 태스크가 됐을 뿐이다.

### 2.2 설정 손잡이

| 설정 | 템플릿 값 | 뜻 |
|---|---|---|
| `configUSE_TIMERS` | 1 | 0이면 `timers.c` 전체가 빠지고 데몬도 안 만든다 |
| `configTIMER_TASK_PRIORITY` | 7 (`configMAX_PRIORITIES - 1`) | 데몬 우선순위. §5에서 측정 |
| `configTIMER_QUEUE_LENGTH` | 10 | 명령 큐 칸 수. §3에서 넘치게 해 본다 |
| `configTIMER_TASK_STACK_DEPTH` | 512 words (2 KB) | 데몬 스택 = **모든 콜백이 같이 쓰는 스택**. §4.4 |
| `configTIMER_SERVICE_TASK_NAME` | 기본 `"Tmr Svc"` | 태스크 이름 (`timers.c` 65행의 기본값) |
| `configUSE_DAEMON_TASK_STARTUP_HOOK` | 0 | 1이면 데몬이 처음 돌 때 `vApplicationDaemonTaskStartupHook()`을 부른다 (C9 §6) |

데몬은 `vTaskStartScheduler()` 안의 `xTimerCreateTimerTask()`가 만든다(`tasks.c` 3762행). 템플릿은 `configKERNEL_PROVIDED_STATIC_MEMORY 1`이라 데몬 스택(`uxTimerTaskStack`)과 TCB가 heap이 아니라 `.bss`에 있다. C3에서 본 "첫 태스크가 `Tmr Svc`"가 바로 이 태스크다(가장 높은 우선순위이고 가장 나중에 만들어지므로 C3 §2.3의 규칙대로 먼저 돈다).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 360">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="15" y="30" width="150" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="90.0" y="49.2" font-size="12" text-anchor="middle">app 태스크</text>
<text x="90.0" y="65.2" font-size="12" text-anchor="middle">xTimerStart/Reset</text>
<rect x="15" y="100" width="150" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90.0" y="119.2" font-size="12" text-anchor="middle">ISR</text>
<text x="90.0" y="135.2" font-size="12" text-anchor="middle">xTimerResetFromISR</text>
<rect x="215" y="60" width="170" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="300.0" y="76.2" font-size="12" text-anchor="middle">xTimerQueue</text>
<text x="300.0" y="92.2" font-size="12" text-anchor="middle">길이 configTIMER_</text>
<text x="300.0" y="108.2" font-size="12" text-anchor="middle">QUEUE_LENGTH, 16 B/칸</text>
<line x1="165" y1="53" x2="213" y2="80" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<line x1="165" y1="123" x2="213" y2="98" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="190.0" y="162.0" font-size="12" text-anchor="middle">명령 = (ID, 시각, Timer_t*)</text>
<rect x="430" y="30" width="215" height="120" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="537.5" y="62.2" font-size="12" text-anchor="middle" font-weight="bold">prvTimerTask (Tmr Svc)</text>
<text x="537.5" y="78.2" font-size="12" text-anchor="middle">1) 다음 만료 시각 확인</text>
<text x="537.5" y="94.2" font-size="12" text-anchor="middle">2) 만료면 콜백 호출</text>
<text x="537.5" y="110.2" font-size="12" text-anchor="middle">   아니면 큐에서 블록</text>
<text x="537.5" y="126.2" font-size="12" text-anchor="middle">3) 큐의 명령 모두 처리</text>
<line x1="385" y1="88" x2="428" y2="88" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<rect x="430" y="190" width="215" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="537.5" y="211.2" font-size="12" text-anchor="middle">pxCurrentTimerList</text>
<text x="537.5" y="227.2" font-size="12" text-anchor="middle">만료 tick 순 정렬</text>
<rect x="430" y="260" width="215" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="537.5" y="281.2" font-size="12" text-anchor="middle">pxOverflowTimerList</text>
<text x="537.5" y="297.2" font-size="12" text-anchor="middle">tick wrap 뒤 만료분</text>
<line x1="537" y1="150" x2="537" y2="188" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<rect x="215" y="200" width="170" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="300.0" y="221.2" font-size="12" text-anchor="middle">콜백(Timer_t*)</text>
<text x="300.0" y="237.2" font-size="12" text-anchor="middle">Tmr Svc의 스택 위에서</text>
<text x="300.0" y="253.2" font-size="12" text-anchor="middle">블록 금지</text>
<line x1="430" y1="215" x2="387" y2="230" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<rect x="15" y="200" width="150" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="90.0" y="221.2" font-size="12" text-anchor="middle">SysTick ISR</text>
<text x="90.0" y="237.2" font-size="12" text-anchor="middle">xTickCount++</text>
<text x="90.0" y="253.2" font-size="12" text-anchor="middle">데몬 깨우기</text>
<line x1="165" y1="233" x2="213" y2="233" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)" stroke-dasharray="4 3"/>
<text x="330.0" y="300.0" font-size="12" text-anchor="middle">wrap 순간 prvSwitchTimerLists()가 두 리스트를 맞바꾼다</text>
<text x="330.0" y="322.0" font-size="12" text-anchor="middle">명령의 기준 시각 = 보낸 순간의 tick (처리 순간 아님)</text>
<text x="330.0" y="344.0" font-size="12" text-anchor="middle">예외: ChangePeriod는 처리 순간 기준</text>
</svg>
```
그림 2 — FreeRTOS 소프트웨어 타이머의 구조. 태스크와 ISR은 명령을 `xTimerQueue`에 넣기만 하고, 데몬(`prvTimerTask`)만 알람 목록 두 개를 만지고 콜백을 부른다. SysTick은 데몬을 깨울 뿐이다.

### 2.3 데몬의 한 바퀴

`prvTimerTask()`(`timers.c` 746행)의 몸체는 세 줄이다.

```c
for( ; configCONTROL_INFINITE_LOOP(); )
{
    xNextExpireTime = prvGetNextExpireTime( &xListWasEmpty );       /* 목록 맨 앞의 만료 tick */
    prvProcessTimerOrBlockTask( xNextExpireTime, xListWasEmpty );   /* 만료면 콜백, 아니면 블록 */
    prvProcessReceivedCommands();                                   /* 큐에 쌓인 명령 전부 처리 */
}
```

`prvProcessTimerOrBlockTask()`는 지금 tick이 맨 앞 타이머의 만료 tick 이상이면 `prvProcessExpiredTimer()`로 콜백을 부르고, 아니면 `vQueueWaitForMessageRestricted(xTimerQueue, xNextExpireTime - xTimeNow, ...)`로 **"명령이 오거나 다음 만료 시각이 되거나, 먼저 오는 쪽까지"** 블록한다. 블록 시간이 곧 다음 만료까지의 거리이므로, 타이머가 많아도 데몬은 **만료가 없으면 한 번도 깨지 않는다**. tick ISR 입장에서 데몬은 "N tick 뒤에 깨울 태스크 하나"일 뿐이다. 그래서 tickless(C6)와도 자연스럽게 맞물린다 — 다음 타이머 만료가 곧 idle이 잘 수 있는 한계가 된다.

### 2.4 예제 2 — 명령은 언제 처리되나, 만료는 무엇 기준인가

무엇을 확인하나: app 태스크(prio 3)가 10 ms one-shot에 `xTimerStart()`를 보낸 뒤 0 / 5 / 15 ms 동안 CPU를 쥔다(바쁜 대기). 데몬 우선순위가 7(호출자보다 높음)일 때와 1(낮음)일 때, 명령이 데몬에게 **언제 도착하나**(trace 매크로로 측정)와 콜백이 **몇 tick에 울리나**를 본다.

```c
/* D6 ex2: 명령 큐 지연 — 데몬 우선순위가 호출자보다 높을 때/낮을 때, 만료 기준 시각은 언제인가 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define T() ((unsigned)xTaskGetTickCount())
static TimerHandle_t tm;
static volatile uint32_t c_send, c_rx, rx_tick, cb_tick;
void d6_cmd_rx(void *t, long id) { (void)t; (void)id; c_rx = CYC(); rx_tick = T(); }
static void cb(TimerHandle_t t) { (void)t; cb_tick = T(); }
static void trial(uint32_t busy_ms)
{
    vTaskDelay(1);                                       /* tick 경계에서 시작 */
    uint32_t t0 = T();
    c_rx = 0; cb_tick = 0;
    c_send = CYC();
    xTimerStart(tm, 0);                                  /* 기간 10 ms one-shot */
    unsigned act = (unsigned)xTimerIsTimerActive(tm);
    spin_us(busy_ms * 1000);                             /* 호출자가 CPU를 계속 쥔다 */
    vTaskDelay(pdMS_TO_TICKS(30));                       /* 이제 블록 → 낮은 데몬도 돈다 */
    unsigned rx_rel = rx_tick - t0;
    board_printf_raw("  busy=%u ms: active-after-send=%u  cmd rx +%u us (tick +%u)  cb at tick +%u\n",
                     (unsigned)busy_ms, act, (unsigned)US(c_rx - c_send),
                     rx_rel, (unsigned)(cb_tick - t0));
}
static void app(void *p)
{
    (void)p;
    tm = xTimerCreate("t10", pdMS_TO_TICKS(10), pdFALSE, NULL, cb);
    board_printf_raw("daemon prio=%u, caller prio=%u, period=10 ticks\n",
                     (unsigned)configTIMER_TASK_PRIORITY, (unsigned)uxTaskPriorityGet(NULL));
    trial(0); trial(5); trial(15);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; for p in 7 1; do ./run.sh ex2_cmdlat.c ex2_$p -DD6_TRACE -DD6_TMR_PRIO=$p; done'
```

```text
daemon prio=7, caller prio=3, period=10 ticks
  busy=0 ms: active-after-send=1  cmd rx +14 us (tick +0)  cb at tick +10
  busy=5 ms: active-after-send=1  cmd rx +14 us (tick +0)  cb at tick +10
  busy=15 ms: active-after-send=1  cmd rx +14 us (tick +0)  cb at tick +10
[board_exit] code=0
daemon prio=1, caller prio=3, period=10 ticks
  busy=0 ms: active-after-send=0  cmd rx +20 us (tick +0)  cb at tick +10
  busy=5 ms: active-after-send=0  cmd rx +5020 us (tick +5)  cb at tick +10
  busy=15 ms: active-after-send=0  cmd rx +15020 us (tick +15)  cb at tick +15
[board_exit] code=0
```

출력에서 볼 것:

- **데몬 prio 7**: 명령은 보낸 지 **14 µs** 만에 처리됐다(큐 send → 데몬 선점 → 큐 receive → 리스트 삽입). `xTimerStart()`가 돌아왔을 때 이미 active=1이다. busy=15일 때 콜백이 tick +10에 울렸는데, 그 순간 app은 아직 바쁜 대기 중이었다 — 높은 데몬이 app을 **선점해서** 콜백을 불렀다.
- **데몬 prio 1**: 명령은 app이 CPU를 놓을 때까지 큐에서 기다린다(5020 µs, 15020 µs). 그런데 busy=5에서 콜백은 여전히 **tick +10**이다. 명령이 5 ms 늦게 처리됐는데도 만료는 "명령을 보낸 순간 + 10"으로 계산됐다. `xTimerStart()` 매크로가 보낼 때의 tick을 같이 싣기 때문이다.
- busy=15에서는 처리 시점(+15)에 이미 만료 시각(+10)이 지났다. 이때 데몬은 리스트에 넣지 않고 **처리하자마자 콜백을 부른다**(tick +15). 늦게는 울려도 빼먹지는 않는다.

이 동작이 소스 어디에 있나. `include/timers.h`의 매크로가 tick을 싣는다.

```c
#define xTimerStart( xTimer, xTicksToWait ) \
    xTimerGenericCommand( ( xTimer ), tmrCOMMAND_START, ( xTaskGetTickCount() ), NULL, ( xTicksToWait ) )
```

데몬은 명령을 꺼낸 뒤 `prvInsertTimerInActiveList(pxTimer, 명령시각 + period, xTimeNow, 명령시각)`을 부른다(`timers.c` 892행). 만료 tick이 이미 지났고 `xTimeNow - 명령시각 >= period`면 `xProcessTimerNow = pdTRUE`를 돌려주고, 호출자(`prvProcessReceivedCommands`)가 그 자리에서 콜백을 부른다. `ChangePeriod`만 예외로 `xTimeNow + 새 period`를 쓴다(소스 주석: "The new period does not really have a reference").

### 2.5 함정

- 데몬이 낮은 우선순위일 때 `xTimerStart()` 직후 `xTimerIsTimerActive()`는 **0**이다. "시작했는데 active가 아니다"를 버그로 착각하기 쉽다. 상태는 데몬이 명령을 처리해야 바뀐다.
- `xTimerStop()`도 명령이다. 데몬이 호출자보다 낮아서 Stop이 큐에서 기다리는 동안 만료 시각이 지나면, 데몬은 깨어나서 **만료(콜백)를 먼저 처리하고 Stop을 나중에 처리한다**(루프 순서가 "만료 처리 → 명령 처리"). 즉 "멈췄는데 한 번 더 울린" 콜백이 나올 수 있다. 콜백 안에서 "지금 이 일을 해도 되는 상태인가"를 한 번 더 확인하는 습관이 안전하다.
- `xTimerDelete()` 직후 핸들을 다시 쓰면 안 된다. free는 데몬이 명령을 처리할 때 일어나므로 "지금은 되는 것처럼 보이는" use-after-free가 생긴다.

---

## 3. ISR에서 타이머 다루기 — `FromISR`와 명령 지연

### 3.1 왜 ISR도 큐를 거치나

ISR이 알람 목록을 직접 고칠 수 있다면 빠르겠지만, 그러면 데몬이 목록을 훑는 동안 인터럽트를 꺼야 한다. FreeRTOS는 반대를 택했다. ISR은 `xTimerStartFromISR()`, `xTimerResetFromISR()`, `xTimerStopFromISR()`, `xTimerChangePeriodFromISR()`로 **16바이트짜리 명령 하나를 큐에 넣고**(`xQueueSendToBackFromISR`) 끝난다. 명령의 기준 시각은 ISR이 부른 순간의 `xTaskGetTickCountFromISR()`다(`timers.h` 1116행의 `xTimerResetFromISR` 매크로).

대가는 **명령 지연**이다. 데몬이 명령을 꺼내기 전까지 타이머 상태는 그대로다. 그리고 큐가 가득 차면 `FromISR`는 기다릴 수 없으니 즉시 `pdFAIL`을 돌려준다.

`FromISR` 계열은 `pxHigherPriorityTaskWoken`을 받는다. 데몬이 지금 도는 태스크보다 높으면 `pdTRUE`가 되고, ISR 끝에서 `portYIELD_FROM_ISR(woken)`를 불러야 ISR이 끝나자마자 데몬으로 전환된다. 부르지 않으면? 예제 7에서 잰다.

### 3.2 예제 7 — ISR → 데몬 지연, yield 유무, 큐 넘침

무엇을 확인하나: TIMER0 인터럽트(tick 경계에서 0.37 ms 뒤 한 번)가 `xTimerResetFromISR()`를 1 / 10 / 12번 보낸다. 그동안 app(prio 2)은 3 ms 바쁜 대기 중이다. ISR 진입부터 데몬이 첫 명령을 꺼낼 때까지의 시간과, 큐 길이 10에서 실패 수를 센다.

```c
/* D6 ex7: ISR에서 xTimerResetFromISR — 명령은 큐로 간다: 지연(yield 유무)과 큐가 넘칠 때 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define NT 12
static TimerHandle_t tm[NT];
static volatile int yield, burst; static volatile uint32_t c_isr, c_rx, nrx, nfail;
void d6_cmd_rx(void *t, long id) { (void)t; (void)id; if (nrx++ == 0) c_rx = CYC(); }
static void cb(TimerHandle_t t) { (void)t; }
void TIMER0_IRQHandler(void)                             /* "RC 패킷 도착" 같은 이벤트 */
{
    BaseType_t woken = pdFALSE;
    TIMER_CTRL(TIMER0_BASE) = 0; TIMER_INTCLR(TIMER0_BASE) = 1;
    c_isr = CYC();
    for (int i = 0; i < burst; i++)
        if (xTimerResetFromISR(tm[i], &woken) != pdPASS) nfail++;
    if (yield) portYIELD_FROM_ISR(woken);
}
static void trial(int y, int b)
{
    vTaskDelay(1);
    yield = y; burst = b; nrx = 0; nfail = 0;
    TIMER_RELOAD(TIMER0_BASE) = 25u * 370u;              /* 0.37 ms 뒤 한 번 */
    TIMER_VALUE(TIMER0_BASE) = 25u * 370u;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;                   /* EN + IRQ EN */
    spin_us(3000);                                       /* 이 태스크(prio 2)가 CPU를 쥐고 있다 */
    vTaskDelay(pdMS_TO_TICKS(5));
    board_printf_raw("  yield=%d burst=%d: ISR->daemon rx %u us, received %u, FromISR fails %u\n",
                     y, b, (unsigned)US(c_rx - c_isr), (unsigned)nrx, (unsigned)nfail);
}
static void app(void *p)
{
    (void)p;
    for (int i = 0; i < NT; i++) tm[i] = xTimerCreate("rc", pdMS_TO_TICKS(100), pdFALSE, NULL, cb);
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;            /* 0x40 >= MAX_SYSCALL(0x20): API 허용 */
    board_printf_raw("daemon prio %u, queue len %u\n", (unsigned)configTIMER_TASK_PRIORITY,
                     (unsigned)configTIMER_QUEUE_LENGTH);
    trial(1, 1); trial(0, 1); trial(1, 10); trial(1, 12);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex7_isr.c ex7 -DD6_TRACE; ./run.sh ex7_isr.c ex7b -DD6_TRACE -DD6_TMR_PRIO=1'
```

```text
daemon prio 7, queue len 10
  yield=1 burst=1: ISR->daemon rx 15 us, received 1, FromISR fails 0
  yield=0 burst=1: ISR->daemon rx 631 us, received 1, FromISR fails 0
  yield=1 burst=10: ISR->daemon rx 62 us, received 10, FromISR fails 0
  yield=1 burst=12: ISR->daemon rx 69 us, received 10, FromISR fails 2
[board_exit] code=0
daemon prio 1, queue len 10
  yield=1 burst=1: ISR->daemon rx 2642 us, received 1, FromISR fails 0
  yield=0 burst=1: ISR->daemon rx 2642 us, received 1, FromISR fails 0
  yield=1 burst=10: ISR->daemon rx 2642 us, received 10, FromISR fails 0
  yield=1 burst=12: ISR->daemon rx 2642 us, received 10, FromISR fails 2
[board_exit] code=0
```

출력에서 볼 것:

- **yield=1, 데몬 prio 7**: ISR에서 데몬까지 **15 µs**. ISR이 끝나자마자 PendSV로 데몬에 전환됐다.
- **yield=0**: **631 µs**. 인터럽트가 tick 시작 뒤 0.37 ms에 왔으니 다음 tick까지 남은 0.63 ms를 기다렸다. 큐 send가 `xYieldPendings`를 세워 두었기 때문에 다음 tick ISR이 전환을 일으켰다. 즉 `portYIELD_FROM_ISR`를 빼먹으면 최악 **1 tick**이 더 붙는다. link-loss 같은 100 ms 타임아웃에서는 티가 안 나지만, 데몬을 "ISR의 뒷일 처리기"로 쓰면(D7) 이 1 tick이 그대로 응답 지연이 된다.
- **burst=12**: 큐 길이 10이라 **2개가 실패**했다(`pdFAIL`). ISR 안에서 보냈으니 데몬이 중간에 꺼낼 기회가 없다. 10개를 보내는 데 ISR이 약 47 µs(62 − 15) 걸렸다 — 명령 하나에 약 5 µs다.
- **데몬 prio 1**: yield를 하든 말든 **2642 µs**. app(prio 2)이 3 ms 바쁜 대기를 끝내고 블록할 때까지 데몬이 못 돈다. `portYIELD_FROM_ISR`는 "더 높은 태스크가 깨어났으면 전환"이지, 낮은 데몬을 앞으로 당겨 주지 않는다.

### 3.3 큐 길이 정하기

`configTIMER_QUEUE_LENGTH`는 "데몬이 한 번 돌기 전까지 쌓일 수 있는 명령의 최대 수"다. 계산하는 순서는 이렇다.

1. 한 ISR이 한 번에 보내는 명령 수의 최대(예제 7의 burst)를 센다.
2. 데몬이 못 도는 최장 구간(더 높은 태스크의 최장 실행, 콜백 하나의 최장 실행) 동안 들어올 수 있는 인터럽트 수를 곱한다.
3. `xTimerPendFunctionCallFromISR()`(D7)도 **같은 큐**를 쓴다(음수 명령 ID). 그 몫도 더한다.

메시지 한 칸은 16바이트다. 예제 12의 `nm` 출력에서 `ucStaticTimerQueueStorage`가 `0xa0` = 160바이트 = 10 × 16으로 확인된다. 큐를 늘리는 RAM 비용은 작으니 아끼지 말고, **`FromISR` 반환값은 반드시 확인해서** 실패 카운터라도 올려 둔다.

### 3.4 함정

- `FromISR` 계열은 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은(숫자가 작은) 인터럽트에서 부르면 안 된다(E1). 예제는 TIMER0을 0x40으로 두었다(경계 0x20보다 낮은 우선순위).
- ISR에서 일반 판(`xTimerReset`)을 부르면 `xTimerGenericCommand` 매크로가 명령 ID(태스크용 1~5)를 보고 `xTimerGenericCommandFromTask`로 보내고, 그 안의 `xQueueSendToBack`이 블록 경로를 탄다. ARM_CM3 포트에서는 그 앞의 `vPortEnterCritical()`이 `configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 )`(`port.c` 438행)로 "ISR에서 불렸다"를 잡아 준다 — **`configASSERT`를 켜 두었을 때만**이다.
- "ISR에서 Reset했으니 이제 100 ms 뒤에 울린다"는 **데몬이 명령을 처리한 뒤**에야 참이다. 데몬이 처리하기 전에 원래 만료 시각이 와 버리면, 데몬 루프는 만료를 먼저 처리하므로 **이미 리셋했어야 할 타이머가 한 번 울릴 수 있다**. link-loss 타이머처럼 "울리면 안 되는" 타이머는 만료 직전의 리셋이 이 경쟁에 걸리지 않을 만큼 데몬을 높게 둔다.

---
## 4. 콜백 문맥의 규칙 — 블록 금지, 짧게, 스택은 공용

### 4.1 직관 — 비서는 한 명이다

비서가 알람 A의 할 일(콜백)을 하다가 "10분만 기다렸다가 다시 하자"며 의자에 앉아 버리면, 그동안 울려야 할 알람 B, C, D는 아무도 안 울린다. FreeRTOS 데몬이 정확히 이렇다. 콜백은 **데몬 태스크의 함수 호출**이므로, 콜백이 `vTaskDelay()`를 부르면 **데몬 태스크 자체가 잠든다**. 데몬이 잠든 동안 알람 목록은 아무도 보지 않고 명령 큐도 아무도 비우지 않는다.

FreeRTOS 공식 문서(software timer 페이지)도 콜백에서 `vTaskDelay()`, `vTaskDelayUntil()`, 0이 아닌 block time의 큐·세마포어 API를 부르지 말라고 명시하고, `timers.h`의 예제 주석도 "This is called from a timer callback so must not block."라고 적는다(323행). 커널이 막지는 않는다 — 데몬도 태스크이니 블록 API가 "작동"은 한다. 그래서 더 위험하다.

### 4.2 예제 3 — 콜백 하나가 블록하면 다른 타이머가 얼마나 늦나

무엇을 확인하나: 착한 타이머 `hb`(auto-reload 5 ms, 시각만 기록)와 나쁜 타이머 `bad`(auto-reload 50 ms, 콜백에서 `vTaskDelay(blk_ms)`)를 같이 돌린다. `blk_ms`를 0 / 1 / 2 / 5 / 10 / 20 / 40 ms로 바꾸며 500 ms 동안 `hb`의 지연(첫 호출 기준 이상적 시각과의 차이)과 "100 µs 안에 연달아 불린 최대 횟수"를 잰다.

```c
/* D6 ex3: 콜백 하나가 블록하면(vTaskDelay) 다른 모든 타이머가 늦는다 — 5 ms heartbeat의 지연 측정 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define HB_MS 5u
static volatile uint32_t blk_ms, n, c_first, late_max, late_sum, burst, burst_max, c_prev;
static void hb_cb(TimerHandle_t t)                       /* 착한 콜백: 시각만 기록 */
{
    (void)t;
    uint32_t c = CYC();
    if (n == 0) c_first = c;
    uint32_t ideal = c_first + n * HB_MS * 25000u;       /* 첫 호출 기준 이상적 시각 */
    uint32_t late = (c - ideal) / 25u;
    if ((int32_t)(c - ideal) < 0) late = 0;
    late_sum += late; if (late > late_max) late_max = late;
    burst = (n && c - c_prev < 25u * 100u) ? burst + 1 : 1; /* 100 us 안에 연달아 = 몰아치기 */
    if (burst > burst_max) burst_max = burst;
    c_prev = c; n++;
}
static void bad_cb(TimerHandle_t t)                      /* 나쁜 콜백: 데몬 태스크 안에서 블록 */
{
    (void)t;
    if (blk_ms) vTaskDelay(pdMS_TO_TICKS(blk_ms));
}
static void app(void *p)
{
    (void)p;
    static const uint32_t B[] = { 0, 1, 2, 5, 10, 20, 40 };
    TimerHandle_t hb = xTimerCreate("hb", pdMS_TO_TICKS(HB_MS), pdTRUE, NULL, hb_cb);
    TimerHandle_t bad = xTimerCreate("bad", pdMS_TO_TICKS(50), pdTRUE, NULL, bad_cb);
    board_printf_raw("block_ms  hb_calls  late_avg_us  late_max_us  max_burst\n");
    for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) {
        blk_ms = B[i]; n = 0; late_max = late_sum = 0; burst_max = 0;
        xTimerReset(hb, 0); xTimerReset(bad, 0);
        vTaskDelay(pdMS_TO_TICKS(502));                  /* 약 100번의 heartbeat */
        xTimerStop(hb, 0); xTimerStop(bad, 0);
        vTaskDelay(pdMS_TO_TICKS(60));                   /* 블록 중인 bad_cb가 끝나길 기다림 */
        board_printf_raw("%u\t  %u\t    %u\t\t %u\t      %u\n", (unsigned)blk_ms, (unsigned)n,
                         (unsigned)(late_sum / n), (unsigned)late_max, (unsigned)burst_max);
    }
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex3_block.c ex3'
```

```text
block_ms  hb_calls  late_avg_us  late_max_us  max_burst
0	  100	    0		 7	      1
1	  100	    99		 999	      1
2	  100	    199		 2001	      1
5	  99	    454		 4998	      2
10	  99	    1363		 9998	      3
20	  99	    4546		 19998	      5
40	  99	    16369		 39998	      9
[board_exit] code=0
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 330">
<line x1="70" y1="270" x2="620" y2="270" stroke="currentColor"/>
<line x1="70" y1="40" x2="70" y2="270" stroke="currentColor"/>
<line x1="66" y1="270.0" x2="620" y2="270.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="212.5" x2="620" y2="212.5" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="216.5" font-size="12" text-anchor="end">10</text>
<line x1="66" y1="155.0" x2="620" y2="155.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="159.0" font-size="12" text-anchor="end">20</text>
<line x1="66" y1="97.5" x2="620" y2="97.5" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="101.5" font-size="12" text-anchor="end">30</text>
<line x1="66" y1="40.0" x2="620" y2="40.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="44.0" font-size="12" text-anchor="end">40</text>
<text x="30.0" y="155.0" font-size="12" text-anchor="middle" transform="rotate(-90 30 155.0)">heartbeat 지연 (ms)</text>
<text x="345.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">예제 3 — 콜백 하나가 블록한 시간 vs 5 ms heartbeat의 지연</text>
<rect x="87.3" y="270.0" width="22" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="109.3" y="270.0" width="22" height="0.0" fill="#d0564a" fill-opacity="0.75"/>
<text x="109.3" y="264.0" font-size="12" text-anchor="middle">×1</text>
<text x="109.3" y="288.0" font-size="12" text-anchor="middle">0</text>
<rect x="165.9" y="269.4" width="22" height="0.6" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="187.9" y="264.3" width="22" height="5.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="187.9" y="258.3" font-size="12" text-anchor="middle">×1</text>
<text x="187.9" y="288.0" font-size="12" text-anchor="middle">1</text>
<rect x="244.4" y="268.9" width="22" height="1.1" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="266.4" y="258.5" width="22" height="11.5" fill="#d0564a" fill-opacity="0.75"/>
<text x="266.4" y="252.5" font-size="12" text-anchor="middle">×1</text>
<text x="266.4" y="288.0" font-size="12" text-anchor="middle">2</text>
<rect x="323.0" y="267.4" width="22" height="2.6" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="345.0" y="241.3" width="22" height="28.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="345.0" y="235.3" font-size="12" text-anchor="middle">×2</text>
<text x="345.0" y="288.0" font-size="12" text-anchor="middle">5</text>
<rect x="401.6" y="262.2" width="22" height="7.8" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="423.6" y="212.5" width="22" height="57.5" fill="#d0564a" fill-opacity="0.75"/>
<text x="423.6" y="206.5" font-size="12" text-anchor="middle">×3</text>
<text x="423.6" y="288.0" font-size="12" text-anchor="middle">10</text>
<rect x="480.1" y="243.9" width="22" height="26.1" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="502.1" y="155.0" width="22" height="115.0" fill="#d0564a" fill-opacity="0.75"/>
<text x="502.1" y="149.0" font-size="12" text-anchor="middle">×5</text>
<text x="502.1" y="288.0" font-size="12" text-anchor="middle">20</text>
<rect x="558.7" y="175.9" width="22" height="94.1" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="580.7" y="40.0" width="22" height="230.0" fill="#d0564a" fill-opacity="0.75"/>
<text x="580.7" y="34.0" font-size="12" text-anchor="middle">×9</text>
<text x="580.7" y="288.0" font-size="12" text-anchor="middle">40</text>
<text x="345.0" y="308.0" font-size="12" text-anchor="middle">bad_cb 안의 vTaskDelay (ms) — 막대 위 ×N = 연달아 호출된 최대 횟수</text>
<rect x="90" y="52" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="108.0" y="62.0" font-size="12" text-anchor="start">평균 지연</text>
<rect x="180" y="52" width="12" height="12" fill="#d0564a" fill-opacity="0.75"/>
<text x="198.0" y="62.0" font-size="12" text-anchor="start">최대 지연</text>
</svg>
```
그림 3 — 예제 3 실측. 블록하는 콜백이 잠든 시간만큼 **다른 타이머의 최대 지연이 정확히 늘어난다**(5 ms → 4998 µs, 40 ms → 39998 µs). 막대 위 ×N은 밀린 heartbeat가 깨어난 뒤 연달아 불린 횟수다.

출력에서 볼 것:

- 최대 지연이 블록 시간과 **µs 단위까지 같다**(999, 2001, 4998, 9998 …). 데몬이 잠든 동안 `hb`는 완전히 멈춘다. 착한 타이머가 나쁜 이웃 하나 때문에 망가진다.
- 호출 횟수는 거의 그대로 100이다(99는 마지막 주기가 Stop과 겹친 것). **빼먹지는 않는다.** 대신 블록이 끝나면 밀린 만큼 **연달아 몰아서** 불린다(40 ms 블록 → 9번 연속). 5 ms 주기 LED라면 40 ms 멈췄다가 9번을 수 µs 안에 깜빡이는 셈이다 — 눈에는 "멈칫"으로 보인다. 이 몰아치기의 정체는 §6.3에서 소스로 본다.
- 블록 1 ms만으로도 평균 지연이 99 µs다. "잠깐인데 괜찮겠지"가 다른 모든 타이머에 세금을 매긴다.

### 4.3 무엇이 "블록"인가 — 콜백에서 되는 것과 안 되는 것

| 콜백 안에서 | 괜찮나 | 이유 / 대안 |
|---|---|---|
| 변수 갱신, GPIO 토글, 레지스터 쓰기 | 된다 | 짧고 블록 없음 |
| `xTaskNotify()`, `xTaskNotifyGive()` | 된다 | 블록하지 않는 API. 무거운 일은 태스크에 알리고 넘긴다 |
| `xQueueSend(q, &v, 0)`, `xSemaphoreGive()` | 된다 | block time 0. 실패를 처리할 것 |
| `xTimerStart(other, 0)`, `xTimerReset(self, 0)` | 된다 | 데몬이 자기 큐에 넣는다. **0 아닌 대기는 금지** — 큐가 차 있으면 데몬이 자기 자신을 기다리는 교착 |
| `vTaskDelay()`, `xTaskDelayUntil()` | 안 된다 | 예제 3 그대로 |
| `xQueueReceive(q, &v, 10)`, `xSemaphoreTake(m, portMAX_DELAY)` | 안 된다 | 데몬이 잠든다. 뮤텍스라면 보유한 낮은 태스크가 끝날 때까지 모든 타이머가 그 태스크의 속도로 묶인다 |
| 블로킹 UART `printf`, I2C 동기 전송, 플래시 erase | 안 된다 | 내부에서 세마포어·폴링으로 수 ms 잡아먹는다 |
| 큰 지역 배열 | 조심 | 데몬 스택을 쓴다 (§4.4) |

Don의 SSD 경험으로 바꿔 말하면, 타이머 콜백은 "타이머 ISR 안에서 하던 일"보다는 여유가 있지만 "메인 루프의 한 단계"처럼 **빨리 돌아와야 하는 코드**다. 무거운 일은 `xTaskNotifyGive()`로 전용 태스크에 넘긴다. "콜백에서 함수 하나를 태스크 문맥으로 미루기"는 D7의 `xTimerPendFunctionCall()`이 정확히 그 일을 한다.

### 4.4 예제 11 — 콜백은 데몬의 스택을 쓴다

무엇을 확인하나: 콜백 안의 지역 버퍼 크기(64 / 1024 / 1900 / 2000 / 2400 B)를 바꾸며 `Tmr Svc`의 스택 high-water mark를 본다. 데몬 스택은 `configTIMER_TASK_STACK_DEPTH = 512` words = 2048 B이고, 템플릿은 `configCHECK_FOR_STACK_OVERFLOW = 2`다.

```c
/* D6 ex11: 콜백은 데몬 태스크의 스택을 쓴다 — 지역 버퍼 크기별 Tmr Svc high-water mark */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#ifndef BUF
#define BUF 64
#endif
static volatile uint32_t sink;
static void cb(TimerHandle_t t)
{
    (void)t;
    volatile uint8_t buf[BUF];                           /* 예: 텔레메트리 프레임을 지역 변수로 조립 */
    for (unsigned i = 0; i < BUF; i++) buf[i] = (uint8_t)i;
    sink = buf[BUF - 1];
}
static void app(void *p)
{
    (void)p;
    TaskHandle_t d = xTimerGetTimerDaemonTaskHandle();
    UBaseType_t before = uxTaskGetStackHighWaterMark(d);
    xTimerStart(xTimerCreate("t", 1, pdFALSE, NULL, cb), 0);
    vTaskDelay(5);
    UBaseType_t after = uxTaskGetStackHighWaterMark(d);
    board_printf_raw("BUF=%u B: Tmr Svc stack %u words, free-min before=%u after=%u words (used %u B)\n",
                     (unsigned)BUF, (unsigned)configTIMER_TASK_STACK_DEPTH, (unsigned)before,
                     (unsigned)after, (unsigned)((configTIMER_TASK_STACK_DEPTH - after) * 4));
    if (BUF == 64) board_printf_raw("sizeof(StaticTimer_t)=%u B (= Timer_t 1개)\n", (unsigned)sizeof(StaticTimer_t));
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; for b in 64 1024 1900 2000 2400; do TMO=5 ./run.sh ex11_stack.c ex11_$b -DBUF=$b; echo "[exit=$?]"; done'
```

```text
BUF=64 B: Tmr Svc stack 512 words, free-min before=481 after=481 words (used 124 B)
sizeof(StaticTimer_t)=44 B (= Timer_t 1개)
[board_exit] code=0
[exit=0]
BUF=1024 B: Tmr Svc stack 512 words, free-min before=481 after=241 words (used 1084 B)
[board_exit] code=0
[exit=0]
BUF=1900 B: Tmr Svc stack 512 words, free-min before=481 after=22 words (used 1960 B)
[board_exit] code=0
[exit=0]

[StackOverflowHook] task=Tmr Svc
[board_exit] code=251
[exit=1]
qemu-system-arm: terminating on signal 15 from pid 584459 (timeout)
[exit=124]
```

출력에서 볼 것:

- 데몬 자체는 스택을 **124 B**(31 words)만 쓴다. 나머지는 전부 콜백 몫이다. 버퍼 1024 B면 1084 B, 1900 B면 1960 B — 거의 "데몬 기본 + 콜백 지역 변수"다.
- 2000 B에서는 넘쳤고 **방식 2 검사(스택 끝 16바이트 패턴)**가 다음 문맥 전환 때 잡아 `vApplicationStackOverflowHook(..., "Tmr Svc")`가 불렸다.
- 2400 B에서는 **아무 메시지 없이 멈췄다**(5초 타임아웃). `nm -n`으로 보면 템플릿에서 `uxTimerTaskStack`(0x2000006c부터 0x800 B) 바로 아래가 `.data`의 `uxCriticalNesting`·`_impure_data`다. 넘친 쓰기가 패턴 영역을 지나 커널 변수까지 덮어 검사 코드가 돌기 전에 시스템이 망가졌다. "방식 1 · 2 둘 다 놓치는 넘침"은 C9 §5와 같은 현상이다.
- 결론: **데몬 스택 = 가장 스택을 많이 쓰는 콜백 + 124 B + 여유**. 콜백 하나를 추가할 때마다 이 숫자를 다시 봐야 한다. 실제 제품에서는 `uxTaskGetStackHighWaterMark(xTimerGetTimerDaemonTaskHandle())`를 텔레메트리로 내보내는 것이 싸고 확실하다(C9 §9의 top 태스크).

`sizeof(StaticTimer_t) = 44 B`가 타이머 하나의 RAM 비용이다(heap에서 만들면 heap_4 헤더가 더 붙는다). 타이머 수십 개는 부담이 아니다. 부담은 **데몬 스택 하나**다.

### 4.5 예제 12 — 타이머 서비스를 켜는 비용 (footprint)

무엇을 확인하나: 타이머 API를 하나도 안 쓰는 최소 앱을 `configUSE_TIMERS` 1과 0으로 빌드해 크기를 비교하고, `nm`으로 타이머 관련 심볼의 크기를 본다.

```c
/* D6 ex12: 크기 비교용 최소 앱 — 태스크 하나, 타이머 API는 부르지 않는다 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
static void app(void *p) { (void)p; vTaskDelay(1); board_exit(0); }
int main(void)
{
    xTaskCreate(app, "app", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; M="make -s -f /var/rtoslab/templates/freertos-mps2/Makefile"
  $M SRCS=$PWD/ex12_min.c OUT=/tmp/rtos_D6/ex12_on; $M SRCS=$PWD/ex12_min.c OUT=/tmp/rtos_D6/ex12_off EXTRA_CFLAGS=-DD6_NOTIMERS
  arm-none-eabi-nm -S --size-sort /tmp/rtos_D6/ex1/app.elf | grep -iE "timer|tmr"'
```

```text
   text	   data	    bss	    dec	    hex	filename
  11700	    100	 135400	 147200	  23f00	/tmp/rtos_D6/ex12_on/app.elf
   text	   data	    bss	    dec	    hex	filename
   8468	    100	 132884	 141452	  2288c	/tmp/rtos_D6/ex12_off/app.elf
...
20000fd4 00000014 b xActiveTimerList1
20000fc0 00000014 b xActiveTimerList2
...
00001ed8 0000004c T xTimerCreate
20000eb8 00000054 b xStaticTimerQueue.0
20000870 00000060 b xTimerTaskTCB.1
00001f24 00000064 T xTimerGenericCommandFromTask
00001e6c 0000006c T xTimerCreateTimerTask
00001b8c 00000090 t prvProcessExpiredTimer
20000f0c 000000a0 b ucStaticTimerQueueStorage.1
00001c1c 00000250 t prvTimerTask
20000070 00000800 b uxTimerTaskStack.0
```

(`nm` 출력은 타이머 관련 줄 중 큰 것 위주로 남기고 `...`로 줄였다.)

출력에서 볼 것: 타이머를 켜면 **코드 +3232 B, RAM +2516 B**다. RAM의 대부분은 데몬 스택 2048 B(`uxTimerTaskStack`)이고, TCB 96 B, 큐 제어 블록 84 B, 큐 저장소 160 B(10칸 × 16 B), 리스트 둘 40 B가 뒤따른다. `prvTimerTask`가 0x250 = 592 B로 가장 큰 함수인데, GCC(-O2)가 `prvProcessTimerOrBlockTask`, `prvProcessReceivedCommands`, `prvSwitchTimerLists`, `prvInsertTimerInActiveList`, `prvReloadTimer`, `prvSampleTimeNow`를 모두 인라인했기 때문이다. 같은 ELF의 `nm`에서 `prv` 접두 타이머 함수는 `prvProcessExpiredTimer`(0x90)와 `prvCheckForValidListAndQueue`(0x6c)만 남아 있고, 데몬이 블록하는 `vQueueWaitForMessageRestricted`(0x50)는 `queue.c` 쪽 심볼이다.

---

## 5. 데몬 우선순위 고르기

### 5.1 직관

비서도 회사 서열이 있다. 비서보다 높은 사람이 일하는 동안 비서는 알람이 울려도 못 움직인다. 반대로 비서가 가장 높으면 알람이 울리는 순간 누구의 일이든 끊고 콜백을 부른다. **데몬 우선순위 = 모든 콜백의 우선순위**다.

### 5.2 예제 4 — 데몬 우선순위 vs 앱 부하

무엇을 확인하나: 제어 루프를 흉내 낸 `load` 태스크(prio 3, 10 ms마다 6 ms 계산 = CPU 60%)가 도는 동안 7 ms auto-reload 타이머 200번의 지연을 잰다. 지연은 tick hook이 기록한 "그 만료 tick이 시작된 순간"부터 콜백 진입까지다. 데몬 우선순위를 7 / 4 / 3 / 2 / 1로 바꾼다.

```c
/* D6 ex4: 데몬 우선순위 vs 앱 부하 — 7 ms 주기 타이머의 지연 분포 (앱 태스크 prio 3이 10 ms마다 6 ms 계산) */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define P_MS 7u
#define NB 8                                             /* 히스토그램 칸: 1 ms 단위, 마지막은 7 ms 이상 */
static volatile uint32_t n, t_start, late_max, late_sum, hist[NB], c_tick[64];
void vApplicationTickHook(void) { c_tick[xTaskGetTickCountFromISR() % 64] = CYC(); }
static void cb(TimerHandle_t t)                          /* 지연 = 지금 - 만료 tick이 시작된 순간 */
{
    (void)t;
    uint32_t e = t_start + (n + 1) * P_MS;               /* 이번 호출이 맡은 만료 tick */
    uint32_t late = (CYC() - c_tick[e % 64]) / 25u;
    late_sum += late; if (late > late_max) late_max = late;
    uint32_t b = late / 1000u; hist[b < NB ? b : NB - 1]++;
    n++;
}
static void load(void *p)                                /* 제어 루프 흉내: 10 ms 주기, 6 ms 계산 */
{
    (void)p;
    TickType_t w = xTaskGetTickCount();
    for (;;) { spin_us(6000); xTaskDelayUntil(&w, pdMS_TO_TICKS(10)); }
}
static void boss(void *p)
{
    (void)p;
    TimerHandle_t tm = xTimerCreate("t7", pdMS_TO_TICKS(P_MS), pdTRUE, NULL, cb);
    vTaskDelay(3);                                       /* load와 위상을 어긋나게 */
    t_start = xTaskGetTickCount();
    xTimerStart(tm, 0);
    vTaskDelay(pdMS_TO_TICKS(1400));                     /* 약 200번 */
    xTimerStop(tm, 0);
    board_printf_raw("daemon prio %u (load prio 3): n=%u avg=%u us max=%u us  hist[ms]:",
                     (unsigned)configTIMER_TASK_PRIORITY, (unsigned)n, (unsigned)(late_sum / n),
                     (unsigned)late_max);
    for (int i = 0; i < NB; i++) board_printf_raw(" %u", (unsigned)hist[i]);
    board_printf_raw("\n");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(load, "load", 256, NULL, 3, NULL);
    xTaskCreate(boss, "boss", 512, NULL, 6, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; for p in 7 4 3 2 1; do ./run.sh ex4_prio.c ex4_$p -DD6_TMR_PRIO=$p -DconfigUSE_TICK_HOOK=1; done'
```

```text
daemon prio 7 (load prio 3): n=200 avg=11 us max=11 us  hist[ms]: 200 0 0 0 0 0 0 0
[board_exit] code=0
daemon prio 4 (load prio 3): n=199 avg=11 us max=11 us  hist[ms]: 199 0 0 0 0 0 0 0
[board_exit] code=0
daemon prio 3 (load prio 3): n=199 avg=11 us max=11 us  hist[ms]: 199 0 0 0 0 0 0 0
[board_exit] code=0
daemon prio 2 (load prio 3): n=199 avg=2112 us max=6020 us  hist[ms]: 80 20 20 19 20 20 20 0
[board_exit] code=0
daemon prio 1 (load prio 3): n=199 avg=2112 us max=6020 us  hist[ms]: 80 20 20 19 20 20 20 0
[board_exit] code=0
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 320">
<line x1="70" y1="260" x2="620" y2="260" stroke="currentColor"/>
<line x1="70" y1="45" x2="70" y2="260" stroke="currentColor"/>
<line x1="66" y1="260.0" x2="620" y2="260.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="206.2" x2="620" y2="206.2" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="210.2" font-size="12" text-anchor="end">50</text>
<line x1="66" y1="152.5" x2="620" y2="152.5" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="156.5" font-size="12" text-anchor="end">100</text>
<line x1="66" y1="98.8" x2="620" y2="98.8" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="102.8" font-size="12" text-anchor="end">150</text>
<line x1="66" y1="45.0" x2="620" y2="45.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="49.0" font-size="12" text-anchor="end">200</text>
<text x="30.0" y="152.5" font-size="12" text-anchor="middle" transform="rotate(-90 30 152.5)">호출 횟수</text>
<text x="345.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">예제 4 — 7 ms 타이머 지연 분포: 데몬 prio 7 vs 2 (부하 태스크 prio 3)</text>
<rect x="80.4" y="45.0" width="24" height="215.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="92.4" y="40.0" font-size="12" text-anchor="middle">200</text>
<rect x="104.4" y="174.0" width="24" height="86.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="116.4" y="169.0" font-size="12" text-anchor="middle">80</text>
<text x="104.4" y="278.0" font-size="12" text-anchor="middle">0-1</text>
<rect x="149.1" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="173.1" y="238.5" width="24" height="21.5" fill="#e08a3c" fill-opacity="0.75"/>
<text x="185.1" y="233.5" font-size="12" text-anchor="middle">20</text>
<text x="173.1" y="278.0" font-size="12" text-anchor="middle">1-2</text>
<rect x="217.9" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="241.9" y="238.5" width="24" height="21.5" fill="#e08a3c" fill-opacity="0.75"/>
<text x="253.9" y="233.5" font-size="12" text-anchor="middle">20</text>
<text x="241.9" y="278.0" font-size="12" text-anchor="middle">2-3</text>
<rect x="286.6" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="310.6" y="239.6" width="24" height="20.4" fill="#e08a3c" fill-opacity="0.75"/>
<text x="322.6" y="234.6" font-size="12" text-anchor="middle">19</text>
<text x="310.6" y="278.0" font-size="12" text-anchor="middle">3-4</text>
<rect x="355.4" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="379.4" y="238.5" width="24" height="21.5" fill="#e08a3c" fill-opacity="0.75"/>
<text x="391.4" y="233.5" font-size="12" text-anchor="middle">20</text>
<text x="379.4" y="278.0" font-size="12" text-anchor="middle">4-5</text>
<rect x="424.1" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="448.1" y="238.5" width="24" height="21.5" fill="#e08a3c" fill-opacity="0.75"/>
<text x="460.1" y="233.5" font-size="12" text-anchor="middle">20</text>
<text x="448.1" y="278.0" font-size="12" text-anchor="middle">5-6</text>
<rect x="492.9" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="516.9" y="238.5" width="24" height="21.5" fill="#e08a3c" fill-opacity="0.75"/>
<text x="528.9" y="233.5" font-size="12" text-anchor="middle">20</text>
<text x="516.9" y="278.0" font-size="12" text-anchor="middle">6-7</text>
<rect x="561.6" y="260.0" width="24" height="0.0" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="585.6" y="260.0" width="24" height="0.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="585.6" y="278.0" font-size="12" text-anchor="middle">7+</text>
<text x="345.0" y="300.0" font-size="12" text-anchor="middle">지연 구간 (ms) — prio 7·4·3은 전부 11 µs, prio 2·1은 최대 6.02 ms</text>
<rect x="420" y="60" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="438.0" y="70.0" font-size="12" text-anchor="start">데몬 prio 7</text>
<rect x="420" y="80" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/>
<text x="438.0" y="90.0" font-size="12" text-anchor="start">데몬 prio 2</text>
</svg>
```
그림 4 — 예제 4 실측. 데몬이 부하 태스크보다 높거나 같으면 모든 콜백이 tick 뒤 11 µs에 불렸다. 낮으면 지연이 0~6 ms에 고르게 퍼진다(부하가 6 ms 계산 중 어디쯤에서 만료가 왔느냐에 따라).

출력에서 볼 것:

- **prio 7 / 4 / 3**: 지연 11 µs, 흔들림 0. 11 µs가 "SysTick ISR → 데몬 ready → PendSV → 데몬이 리스트를 보고 콜백 진입"의 고정 비용이다(QEMU icount 기준).
- **prio 3(같음)도 11 µs**인 것이 흥미롭다. `configUSE_TIME_SLICING = 1`이라 같은 우선순위의 ready 태스크가 있으면 tick마다 차례가 넘어간다. 데몬은 만료 tick에 ready가 되고 바로 그 tick의 time slicing으로 차례를 받는다. 다만 이것은 "같은 우선순위 태스크가 tick마다 양보한다"에 기대는 것이므로 설계 근거로 쓰기엔 약하다. time slicing을 끄면 load가 블록할 때까지 기다린다.
- **prio 2 / 1**: 평균 2.1 ms, 최대 6.02 ms = load의 계산 시간 그대로. 200번 중 80번만 1 ms 안이었다. 7과 10의 최소공배수 70 ms마다 같은 위상이 반복되므로 히스토그램이 고르게 나왔다.

### 5.3 그럼 몇으로 하나

| 데몬 우선순위 | 장점 | 단점 | 이럴 때 |
|---|---|---|---|
| 가장 높게 (`configMAX_PRIORITIES - 1`) | 타이머가 정확(tick + 11 µs). `FromISR` 명령이 즉시 처리 | **모든 콜백이 사실상 최고 우선순위 코드**가 된다. 콜백 하나가 1 ms 걸리면 제어 루프가 1 ms 밀린다 | 콜백이 전부 짧은 "알리고 끝" 형태일 때. 많은 프로젝트의 기본 |
| 제어 루프 바로 아래 | 제어 루프는 타이머 때문에 안 밀린다 | 제어 루프가 바쁘면 타이머가 그만큼 늦는다 (예제 4의 prio 2) | 타이머 일이 LED·로그·텔레메트리처럼 늦어도 되는 것일 때 |
| 낮게 (1 근처) | 시스템에 거의 영향 없음 | 지연이 커지고 예측 불가. link-loss 같은 안전 타이머에는 위험 | 진단·통계용 타이머만 있을 때 |

실무 규칙은 이렇다. **데몬은 높게 두고, 콜백은 "기록 + notify"만 하게 만든다.** 그러면 타이머의 정확도와 제어 루프의 지연을 둘 다 지킬 수 있다. 콜백이 무거워지는 순간 그 일을 태스크로 빼낸다. 데몬을 낮춰서 문제를 숨기면 예제 8(§10.1)처럼 failsafe가 늦어진다.

스케줄 가능성 분석(N1·N2, RTA)으로 말하면, 데몬은 "가장 짧은 주기의 타이머 주기"와 "가장 긴 콜백 실행 시간"을 가진 태스크 하나로 모델링해서 스케줄 가능성 분석에 넣는다. 콜백들을 모두 합한 최악 실행 시간이 그 태스크의 C다.

---

## 6. auto-reload의 정확도 — drift는 없고, 밀리면 몰아서 부른다

### 6.1 소스 — 다음 만료 = 이전 만료 + period

`prvProcessExpiredTimer()`(`timers.c` 716행)는 만료된 타이머를 리스트에서 빼고, auto-reload면 `prvReloadTimer( pxTimer, xNextExpireTime, xTimeNow )`를 부른 뒤 콜백을 부른다. `prvReloadTimer()`(697행)가 핵심이다.

```c
static void prvReloadTimer( Timer_t * const pxTimer, TickType_t xExpiredTime, const TickType_t xTimeNow )
{
    /* Insert the timer into the appropriate list for the next expiry time.
     * If the next expiry time has already passed, advance the expiry time,
     * call the callback function, and try again. */
    while( prvInsertTimerInActiveList( pxTimer, ( xExpiredTime + pxTimer->xTimerPeriodInTicks ), xTimeNow, xExpiredTime ) != pdFALSE )
    {
        xExpiredTime += pxTimer->xTimerPeriodInTicks;   /* Advance the expiry time. */
        traceTIMER_EXPIRED( pxTimer );
        pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
    }
}
```

두 가지를 읽을 수 있다.

1. 다음 만료는 `xExpiredTime + period` — **원래 울렸어야 할 시각 + 주기**다. 콜백이 몇 µs 늦게 불렸든, 데몬이 몇 ms 늦게 깨어났든 격자(grid)는 그대로다. `xTaskDelayUntil()`(C5)과 같은 성질이다. **누적 drift가 없다.**
2. 그 다음 만료조차 이미 지났으면(`prvInsertTimerInActiveList`가 `pdTRUE`) **콜백을 그 자리에서 부르고 한 주기 더 전진**한다. 지금보다 미래가 될 때까지 반복한다. 즉 밀린 주기 수만큼 **연달아 호출**한다. 마지막으로 `prvProcessExpiredTimer`가 원래 만료분의 콜백을 한 번 더 부른다.

### 6.2 예제 5 — 10 ms 주기를 네 가지로 만들어 100주기 뒤 비교

무엇을 확인하나: 매 주기 1 ms 일을 하는 10 ms 주기를 (1) auto-reload 타이머, (2) one-shot을 콜백 끝에서 다시 `xTimerStart`, (3) 태스크의 `vTaskDelay(10)`, (4) 태스크의 `xTaskDelayUntil(10)`으로 만든다. 17 ms마다 3 ms CPU를 뺏는 높은 태스크(prio 4)가 방해한다. 데몬과 주기 태스크는 prio 2. 25주기마다 경과 tick을 찍는다.

```c
/* D6 ex5: 10 ms 주기를 네 가지로 만들고 100주기 뒤 누적 오차 비교 (매 주기 1 ms 일, prio 4 방해꾼) */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
enum { AUTO, REARM, DELAY, UNTIL };
static const char *NAME[] = { "auto-reload timer", "one-shot re-armed in cb", "vTaskDelay(10)", "xTaskDelayUntil" };
static volatile int mode; static volatile uint32_t k, t0, mark[4];
static TaskHandle_t boss_h;
static void hit(void)                                    /* 주기 1회분: 일 1 ms + 기록 */
{
    spin_us(1000);
    k++;
    if (k % 25 == 0) mark[k / 25 - 1] = xTaskGetTickCount() - t0;
    if (k == 100) xTaskNotifyGive(boss_h);
}
static void cb(TimerHandle_t t)
{
    if (k >= 100) return;
    hit();
    if (mode == REARM && k < 100) xTimerStart(t, 0);     /* '지금'부터 다시 10 ms */
}
static void per_task(void *p)
{
    (void)p;
    TickType_t w = xTaskGetTickCount();
    while (k < 100) {
        if (mode == DELAY) vTaskDelay(pdMS_TO_TICKS(10)); else xTaskDelayUntil(&w, pdMS_TO_TICKS(10));
        hit();
    }
    vTaskDelete(NULL);
}
static void noise(void *p)                               /* 17 ms마다 3 ms CPU를 뺏는 높은 태스크 */
{
    (void)p;
    for (;;) { vTaskDelay(pdMS_TO_TICKS(17)); spin_us(3000); }
}
static void boss(void *p)
{
    (void)p;
    TimerHandle_t ta = xTimerCreate("ta", pdMS_TO_TICKS(10), pdTRUE, NULL, cb);
    TimerHandle_t tr = xTimerCreate("tr", pdMS_TO_TICKS(10), pdFALSE, NULL, cb);
    board_printf_raw("mode                      elapsed ticks at k=25 50 75 100 (ideal 250 500 750 1000)\n");
    for (mode = AUTO; mode <= UNTIL; mode++) {
        k = 0; t0 = xTaskGetTickCount();
        if (mode == AUTO) xTimerStart(ta, 0);
        else if (mode == REARM) xTimerStart(tr, 0);
        else xTaskCreate(per_task, "per", 256, NULL, 2, NULL);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTimerStop(ta, 0);
        board_printf_raw("%s:\t%u %u %u %u\n", NAME[mode], (unsigned)mark[0], (unsigned)mark[1],
                         (unsigned)mark[2], (unsigned)mark[3]);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    board_exit(0);
}
int main(void)
{
    xTaskCreate(boss, "boss", 512, NULL, 5, &boss_h);
    xTaskCreate(noise, "noise", 256, NULL, 4, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex5_drift.c ex5 -DD6_TMR_PRIO=2'
```

```text
mode                      elapsed ticks at k=25 50 75 100 (ideal 250 500 750 1000)
auto-reload timer:	251 501 751 1001
one-shot re-armed in cb:	284 571 855 1142
vTaskDelay(10):	284 571 858 1142
xTaskDelayUntil:	251 501 751 1001
[board_exit] code=0
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 320">
<line x1="70" y1="260" x2="600" y2="260" stroke="currentColor"/>
<line x1="70" y1="45" x2="70" y2="260" stroke="currentColor"/>
<line x1="66" y1="260.0" x2="600" y2="260.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="188.3" x2="600" y2="188.3" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="192.3" font-size="12" text-anchor="end">50</text>
<line x1="66" y1="116.7" x2="600" y2="116.7" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="120.7" font-size="12" text-anchor="end">100</text>
<line x1="66" y1="45.0" x2="600" y2="45.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="49.0" font-size="12" text-anchor="end">150</text>
<text x="30.0" y="152.5" font-size="12" text-anchor="middle" transform="rotate(-90 30 152.5)">누적 오차 (ms)</text>
<text x="345.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">예제 5 — 10 ms 주기 100번: 이상적 시각과의 누적 차이</text>
<text x="70.0" y="278.0" font-size="12" text-anchor="middle">0</text>
<text x="202.5" y="278.0" font-size="12" text-anchor="middle">25</text>
<text x="335.0" y="278.0" font-size="12" text-anchor="middle">50</text>
<text x="467.5" y="278.0" font-size="12" text-anchor="middle">75</text>
<text x="600.0" y="278.0" font-size="12" text-anchor="middle">100</text>
<text x="335.0" y="300.0" font-size="12" text-anchor="middle">주기 번호 k</text>
<polyline points="70.0,260.0 202.5,258.6 335.0,258.6 467.5,258.6 600.0,258.6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="70.0" cy="260.0" r="3" fill="#3f9a6b"/>
<circle cx="202.5" cy="258.6" r="3" fill="#3f9a6b"/>
<circle cx="335.0" cy="258.6" r="3" fill="#3f9a6b"/>
<circle cx="467.5" cy="258.6" r="3" fill="#3f9a6b"/>
<circle cx="600.0" cy="258.6" r="3" fill="#3f9a6b"/>
<line x1="90" y1="62" x2="110" y2="62" stroke="#3f9a6b" stroke-width="2"/>
<text x="116.0" y="66.0" font-size="12" text-anchor="start">auto-reload / DelayUntil</text>
<polyline points="70.0,260.0 202.5,211.3 335.0,158.2 467.5,109.5 600.0,56.5" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="70.0" cy="260.0" r="3" fill="#d0564a"/>
<circle cx="202.5" cy="211.3" r="3" fill="#d0564a"/>
<circle cx="335.0" cy="158.2" r="3" fill="#d0564a"/>
<circle cx="467.5" cy="109.5" r="3" fill="#d0564a"/>
<circle cx="600.0" cy="56.5" r="3" fill="#d0564a"/>
<line x1="90" y1="80" x2="110" y2="80" stroke="#d0564a" stroke-width="2"/>
<text x="116.0" y="84.0" font-size="12" text-anchor="start">one-shot 재무장</text>
<polyline points="70.0,260.0 202.5,211.3 335.0,158.2 467.5,105.2 600.0,56.5" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
<circle cx="70.0" cy="260.0" r="3" fill="#e08a3c"/>
<circle cx="202.5" cy="211.3" r="3" fill="#e08a3c"/>
<circle cx="335.0" cy="158.2" r="3" fill="#e08a3c"/>
<circle cx="467.5" cy="105.2" r="3" fill="#e08a3c"/>
<circle cx="600.0" cy="56.5" r="3" fill="#e08a3c"/>
<line x1="90" y1="98" x2="110" y2="98" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
<text x="116.0" y="102.0" font-size="12" text-anchor="start">vTaskDelay(10)</text>
<text x="596.0" y="48.5" font-size="12" text-anchor="end">+142 ms</text>
<text x="596.0" y="250.6" font-size="12" text-anchor="end">+1 ms</text>
</svg>
```
그림 5 — 예제 5 실측. auto-reload와 `xTaskDelayUntil`은 100주기 뒤에도 +1 ms(기록을 1 ms 일 뒤에 하기 때문의 고정 오프셋)다. 콜백에서 다시 Start하는 one-shot과 `vTaskDelay`는 주기마다 "일 1 ms + 방해 지연"이 쌓여 +142 ms(14%)가 됐다.

출력에서 볼 것:

- **auto-reload = `xTaskDelayUntil`**: 1001 tick. 남는 1은 `hit()`가 1 ms 일을 한 **뒤에** tick을 읽기 때문의 고정 오프셋이지 누적이 아니다(25주기 때도 251).
- **one-shot 재무장 = `vTaskDelay`**: 1142 tick, 주기당 평균 1.42 ms씩 밀렸다. 콜백 끝에서 부른 `xTimerStart(t, 0)`의 기준 시각은 **콜백이 끝난 지금**이다. 일 1 ms와 방해 지연이 매번 주기에 더해진다. `vTaskDelay`가 "지금부터 10"인 것과 똑같은 구조다(C5 §5).
- 그러니 "주기적으로 무언가를 한다"는 소프트웨어 타이머는 **auto-reload로 만든다.** one-shot 재무장은 "이번 일이 끝난 뒤 최소 N ms 쉬고 다시"(재시도 backoff, 폴링 간격)처럼 **간격**이 의미일 때만 쓴다.

### 6.3 예제 6 — 콜백이 여러 주기를 넘겨 버리면 (catch-up)

무엇을 확인하나: 10 ms auto-reload 타이머의 3번째 콜백만 35 ms 바쁜 대기를 한다. 각 호출의 tick, µs, 직전 호출과의 간격, 그 순간 `xTimerGetExpiryTime()`이 보여 주는 값을 기록한다.

```c
/* D6 ex6: auto-reload 콜백이 여러 주기를 넘겨 버리면? — prvReloadTimer의 '밀린 만큼 연달아 호출' */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define N 10
static volatile uint32_t n, c0, tk[N], us[N], nx[N];
static void cb(TimerHandle_t t)
{
    if (n >= N) return;
    tk[n] = xTaskGetTickCount();
    us[n] = US(CYC() - c0);
    nx[n] = xTimerGetExpiryTime(t);                      /* 이 순간 리스트에 걸린 다음 만료 tick */
    n++;
    if (n == 3) spin_us(35000);                          /* 3번째 호출만 35 ms 걸린다 (주기 10 ms) */
}
static void app(void *p)
{
    (void)p;
    TimerHandle_t tm = xTimerCreate("t10", pdMS_TO_TICKS(10), pdTRUE, NULL, cb);
    vTaskDelay(1);
    uint32_t t0 = xTaskGetTickCount(); c0 = CYC();
    xTimerStart(tm, 0);
    vTaskDelay(pdMS_TO_TICKS(105));
    xTimerStop(tm, 0);
    board_printf_raw("call  tick(+%u)  t_us   gap_us  expiry_seen\n", (unsigned)t0);
    for (uint32_t i = 0; i < n; i++)
        board_printf_raw("#%u    %u\t     %u\t   %u\t  %u\n", (unsigned)(i + 1), (unsigned)(tk[i] - t0),
                         (unsigned)us[i], (unsigned)(i ? us[i] - us[i - 1] : us[i]), (unsigned)(nx[i] - t0));
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex6_catchup.c ex6'
```

```text
call  tick(+1)  t_us   gap_us  expiry_seen
#1    10	     10008	   10008	  20
#2    20	     20008	   10000	  30
#3    30	     30008	   10000	  40
#4    65	     65015	   35007	  50
#5    65	     65017	   2	  60
#6    65	     65020	   3	  70
#7    70	     70008	   4988	  80
#8    80	     80008	   10000	  90
#9    90	     90008	   10000	  100
#10    100	     100008	   10000	  110
[board_exit] code=0
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 230">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="350.0" y="18.0" font-size="13" text-anchor="middle" font-weight="bold">예제 6 — auto-reload 10 ms, 3번째 콜백이 35 ms 걸릴 때 (실측)</text>
<line x1="60" y1="190" x2="640" y2="190" stroke="currentColor"/>
<line x1="60.0" y1="190" x2="60.0" y2="195" stroke="currentColor"/>
<text x="60.0" y="210.0" font-size="12" text-anchor="middle">0</text>
<line x1="112.7" y1="190" x2="112.7" y2="195" stroke="currentColor"/>
<text x="112.7" y="210.0" font-size="12" text-anchor="middle">10</text>
<line x1="165.5" y1="190" x2="165.5" y2="195" stroke="currentColor"/>
<text x="165.5" y="210.0" font-size="12" text-anchor="middle">20</text>
<line x1="218.2" y1="190" x2="218.2" y2="195" stroke="currentColor"/>
<text x="218.2" y="210.0" font-size="12" text-anchor="middle">30</text>
<line x1="270.9" y1="190" x2="270.9" y2="195" stroke="currentColor"/>
<text x="270.9" y="210.0" font-size="12" text-anchor="middle">40</text>
<line x1="323.6" y1="190" x2="323.6" y2="195" stroke="currentColor"/>
<text x="323.6" y="210.0" font-size="12" text-anchor="middle">50</text>
<line x1="376.4" y1="190" x2="376.4" y2="195" stroke="currentColor"/>
<text x="376.4" y="210.0" font-size="12" text-anchor="middle">60</text>
<line x1="429.1" y1="190" x2="429.1" y2="195" stroke="currentColor"/>
<text x="429.1" y="210.0" font-size="12" text-anchor="middle">70</text>
<line x1="481.8" y1="190" x2="481.8" y2="195" stroke="currentColor"/>
<text x="481.8" y="210.0" font-size="12" text-anchor="middle">80</text>
<line x1="534.5" y1="190" x2="534.5" y2="195" stroke="currentColor"/>
<text x="534.5" y="210.0" font-size="12" text-anchor="middle">90</text>
<line x1="587.3" y1="190" x2="587.3" y2="195" stroke="currentColor"/>
<text x="587.3" y="210.0" font-size="12" text-anchor="middle">100</text>
<line x1="640.0" y1="190" x2="640.0" y2="195" stroke="currentColor"/>
<text x="640.0" y="210.0" font-size="12" text-anchor="middle">110</text>
<text x="638.0" y="226.0" font-size="12" text-anchor="end">ms</text>
<text x="54.0" y="74.0" font-size="12" text-anchor="end">만료</text>
<text x="54.0" y="140.0" font-size="12" text-anchor="end">콜백</text>
<line x1="112.7" y1="60" x2="112.7" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<line x1="165.5" y1="60" x2="165.5" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<line x1="218.2" y1="60" x2="218.2" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<line x1="270.9" y1="60" x2="270.9" y2="84" stroke="#d0564a" stroke-width="2"/>
<line x1="323.6" y1="60" x2="323.6" y2="84" stroke="#d0564a" stroke-width="2"/>
<line x1="376.4" y1="60" x2="376.4" y2="84" stroke="#d0564a" stroke-width="2"/>
<line x1="429.1" y1="60" x2="429.1" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<line x1="481.8" y1="60" x2="481.8" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<line x1="534.5" y1="60" x2="534.5" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<line x1="587.3" y1="60" x2="587.3" y2="84" stroke="#4a7bd0" stroke-width="2"/>
<rect x="218.2" y="124" width="184.5" height="28" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="310.5" y="143.0" font-size="12" text-anchor="middle">#3 콜백 35 ms (spin)</text>
<rect x="110.7" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="163.5" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="400.7" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="400.8" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="401.0" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="427.1" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="479.8" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="532.5" y="124" width="4" height="28" fill="#3f9a6b"/>
<rect x="585.3" y="124" width="4" height="28" fill="#3f9a6b"/>
<text x="413.3" y="112.0" font-size="12" text-anchor="start">#4 #5 #6 연달아 (2~3 µs 간격)</text>
<line x1="270.9090909090909" y1="86" x2="400.09090909090907" y2="120" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah6)" stroke-dasharray="3 3"/>
<line x1="323.6363636363636" y1="86" x2="400.09090909090907" y2="120" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah6)" stroke-dasharray="3 3"/>
<line x1="376.3636363636364" y1="86" x2="400.09090909090907" y2="120" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah6)" stroke-dasharray="3 3"/>
<text x="323.6" y="52.0" font-size="12" text-anchor="middle">놓친 만료 40·50·60</text>
<text x="508.2" y="170.0" font-size="12" text-anchor="middle">#7부터 원래 격자(70, 80, 90…)로 복귀</text>
</svg>
```
그림 6 — 예제 6 실측 타임라인. 3번째 콜백이 35 ms 동안 데몬을 붙잡자 40·50·60의 만료가 밀렸고, 콜백이 끝난 65 ms에 #4·#5·#6이 2~3 µs 간격으로 연달아 불렸다. #7은 원래 격자인 70에 울렸다.

출력에서 볼 것:

- 밀린 세 번(40, 50, 60)이 **사라지지 않고** 65 ms에 2~3 µs 간격으로 몰아서 불렸다. 그 뒤 #7은 70 — 격자가 그대로다(drift 없음).
- `expiry_seen` 열이 소스를 그대로 보여 준다. #4와 #5는 `prvReloadTimer`의 while 루프 안에서 불린 것이라 그 순간 리스트 항목 값이 "이번 반복의 만료 시각"(50, 60)이다. #6은 루프가 70을 리스트에 넣는 데 성공한 뒤 `prvProcessExpiredTimer`가 부른 마지막 호출이라 **다음 만료(70)**가 보인다. 즉 몰아치기 중 콜백이 `xTimerGetExpiryTime()`으로 "지금이 몇 번째 만료분인가"를 알아내려 하면 값이 일관되지 않다. 필요하면 콜백이 자기 카운터를 직접 세야 한다.
- 이 동작은 V10.4.4에서 생겼다. History.txt의 V10.4.4 항목: timers.c 리팩터로 데몬이 자기 큐에 명령을 다시 넣던 방식을 없앴고, 그 결과 "auto-reload timers that miss their intended next execution time will execute again immediately rather than executing again the next time the command queue is processed"가 됐다. 오래된 FreeRTOS 책·블로그의 설명과 다를 수 있는 부분이다.

### 6.4 몰아치기가 문제가 될 때

몰아치기는 "횟수를 세는 일"(펄스 카운트, 평균 계산의 샘플 수)에는 맞고, "시간 간격이 의미인 일"에는 틀린다. 예를 들어 드론 FC에서 10 ms마다 배터리 ADC를 읽어 저역 통과 필터에 넣는 콜백이 몰아서 세 번 불리면, 같은 순간의 값 세 개가 "10 ms 간격 샘플"로 필터에 들어간다. 대처는 셋 중 하나다.

1. 콜백 안에서 `xTaskGetTickCount()`로 직전 호출과의 간격을 보고, 간격이 period보다 많이 작으면 "밀린 호출"로 보고 건너뛴다.
2. 몰아치기 자체를 막으려면 데몬을 붙잡는 원인(긴 콜백, 높은 태스크)을 없앤다 — 근본 대책.
3. "놓치면 버린다"가 맞는 일이면 auto-reload 대신 one-shot 재무장을 쓰고 drift를 받아들인다.

Zephyr `k_timer`는 이 상황에서 FreeRTOS와 다르게 행동한다(§7.6).

---
## 7. Zephyr `k_timer` — expiry 함수는 ISR에서 돈다

### 7.1 API 한 장

Zephyr에는 데몬 태스크가 없다. `k_timer`의 expiry 함수는 **시스템 시계 인터럽트 안에서** 불린다. 공식 문서(`doc/kernel/services/timing/timers.rst`)도 "The function is executed by the system clock interrupt handler."라고 적고, `struct k_timer` 선언(`include/zephyr/kernel.h` 1777행)의 주석도 `expiry_fn` 옆에 "runs in ISR context", `stop_fn` 옆에 "runs in the context of the thread that calls k_timer_stop()"이라고 적는다.

| API | 하는 일 | FreeRTOS 대응 |
|---|---|---|
| `K_TIMER_DEFINE(name, expiry_fn, stop_fn)` | 정적으로 정의 + 초기화 | `xTimerCreateStatic` |
| `k_timer_init(&t, expiry_fn, stop_fn)` | 런타임 초기화 | `xTimerCreate` |
| `k_timer_start(&t, duration, period)` | 첫 만료 = duration 뒤, 그 뒤 period마다. period가 `K_NO_WAIT`나 `K_FOREVER`면 one-shot | Start + ChangePeriod + reload 모드를 한 번에 |
| `k_timer_stop(&t)` | 멈춤. 달리던 중이었으면 **호출자 문맥에서** `stop_fn` 호출 | `xTimerStop` (단, 큐 없이 즉시) |
| `k_timer_status_get(&t)` | 지난 읽기 뒤 만료 횟수를 읽고 0으로 | 없음 (콜백에서 직접 세야 함) |
| `k_timer_status_sync(&t)` | 만료 횟수가 0이면 **다음 만료나 stop까지 블록**, 읽고 0으로 | 없음. 주기 태스크의 `xTaskDelayUntil` 역할 |
| `k_timer_remaining_get(&t)`, `k_timer_expires_ticks(&t)` | 남은 시간 / 만료 tick | `xTimerGetExpiryTime` |
| `k_timer_user_data_set/get(&t, p)` | 포인터 하나 | 타이머 ID |

명령 큐가 없으니 `FromISR` 판도 없다. `k_timer_start()`·`k_timer_stop()`은 spinlock을 잡고 timeout 리스트를 **바로** 고친다. `k_timer_stop()`은 헤더 문서에 `@isr_ok`로 표시돼 있고(그 경우 `stop_fn`도 ISR에서 불릴 수 있어야 한다는 note가 붙어 있다), `k_timer_start()`도 블록하지 않는다. `k_timer_status_sync()`만 ISR 금지다 — 블록하므로. 대신 expiry 함수가 ISR이라 **그 안에서는 아무것도 기다릴 수 없다.**

### 7.2 tick rate부터 명시하자

`kernel/Kconfig` 826행의 기본값은 이렇다.

```text
config SYS_CLOCK_TICKS_PER_SEC
	int "System tick frequency (in ticks/second)"
	default 100 if QEMU_TARGET || SOC_POSIX
	default 10000 if TICKLESS_KERNEL
	default 100
```

`mps2/an385` 같은 QEMU 보드는 **100 Hz**(10 ms 해상도)가 기본이고, 실보드(tickless)는 10 kHz가 기본이다. QEMU에서 잰 숫자를 실보드에 그대로 옮기면 해상도가 100배 다르다. 이 노트의 Zephyr 앱은 `prj.conf`에 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`을 명시해서 FreeRTOS 예제(1 kHz)와 맞췄다. 예제 z1만 100 Hz로도 한 번 더 돌려 차이를 본다.

### 7.3 예제 z1 — ISR 문맥, duration의 tick 수, status_sync / get, stop fn

무엇을 확인하나: (1) one-shot `K_MSEC(10)`이 몇 tick 뒤 만료되는지와 expiry 함수 안의 `k_is_in_isr()`, 같은 10 ms의 `k_sleep()`과 비교, (2) duration 5 ms · period 20 ms 타이머를 `k_timer_status_sync()`로 기다리는 루프, (3) 70 ms 자고 나서 `k_timer_status_get()`, (4) `k_timer_stop()`이 `stop_fn`을 어느 문맥에서 부르나.

`prj.conf`와 `CMakeLists.txt`(세 Zephyr 예제 공통, 프로젝트 이름만 다르다):

```text
CONFIG_BOOT_BANNER=n
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
```

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(d6_z1)
target_sources(app PRIVATE src/main.c)
```

```c
/* D6 z1: Zephyr k_timer — expiry fn은 ISR에서, duration의 tick 수, status_sync/get, stop fn, user data */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)            /* FPGAIO 25 MHz */
#define US(c) ((uint32_t)(c) / 25u)
static volatile uint32_t c_exp, exp_isr = 9, n_stop, stop_isr = 9;
static void exp_fn(struct k_timer *t)
{
	c_exp = CYC();
	exp_isr = k_is_in_isr();
	uint32_t *cnt = k_timer_user_data_get(t);
	if (cnt != NULL) (*cnt)++;
}
static void stop_fn(struct k_timer *t) { ARG_UNUSED(t); n_stop++; stop_isr = k_is_in_isr(); }
K_TIMER_DEFINE(tm, exp_fn, stop_fn);
static uint32_t user_cnt;
int main(void)
{
	printk("tick=%d Hz\n", CONFIG_SYS_CLOCK_TICKS_PER_SEC);
	k_sleep(K_TICKS(1));                                   /* tick 경계에 맞춘다 */
	int64_t t0 = k_uptime_ticks(); uint32_t c0 = CYC();
	k_timer_start(&tm, K_MSEC(10), K_NO_WAIT);             /* one-shot 10 ms */
	k_timer_status_sync(&tm);
	printk("one-shot K_MSEC(10): %lld ticks, %u us, expiry in ISR=%u\n",
	       k_uptime_ticks() - t0, US(c_exp - c0), exp_isr);
	k_sleep(K_TICKS(1));
	t0 = k_uptime_ticks(); c0 = CYC();
	k_sleep(K_MSEC(10));
	printk("k_sleep(K_MSEC(10)):  %lld ticks, %u us\n", k_uptime_ticks() - t0, US(CYC() - c0));

	k_timer_user_data_set(&tm, &user_cnt);
	c0 = CYC();
	k_timer_start(&tm, K_MSEC(5), K_MSEC(20));             /* 첫 만료 5 ms, 이후 20 ms 주기 */
	for (int i = 0; i < 4; i++) {
		uint32_t s = k_timer_status_sync(&tm);
		printk("  status_sync #%d -> %u at %u us\n", i, s, US(CYC() - c0));
	}
	k_msleep(70);                                           /* 그 사이 3번 만료 */
	printk("  after k_msleep(70): status_get=%u remaining=%u ms\n",
	       k_timer_status_get(&tm), k_timer_remaining_get(&tm));
	k_timer_stop(&tm);
	printk("stop: stop_fn calls=%u in ISR=%u, user_cnt=%u\n", n_stop, stop_isr, user_cnt);
	k_timer_stop(&tm);
	printk("stop again (already stopped): stop_fn calls=%u, status_sync=%u (no block)\n",
	       n_stop, k_timer_status_sync(&tm));
	register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
	__asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/var/rtoslab/bin; for hz in 1000 100; do
  XDG_CACHE_HOME=/tmp/rtos_D6/zcache $R/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D6/z1 /tmp/rtos_D6/z1_$hz -- -DCONFIG_SYS_CLOCK_TICKS_PER_SEC=$hz -DUSER_CACHE_DIR=/tmp/rtos_D6/zcache
  ICOUNT=1 $R/rtos-run-mps2 /tmp/rtos_D6/z1_$hz/zephyr/zephyr.elf 60; done'
```

```text
           FLASH:       14660 B         4 MB      0.35%
             RAM:        6192 B         4 MB      0.15%
tick=1000 Hz
one-shot K_MSEC(10): 10 ticks, 9987 us, expiry in ISR=1
k_sleep(K_MSEC(10)):  11 ticks, 11002 us
  status_sync #0 -> 1 at 4937 us
  status_sync #1 -> 1 at 24938 us
  status_sync #2 -> 1 at 44939 us
  status_sync #3 -> 1 at 64940 us
  after k_msleep(70): status_get=3 remaining=9 ms
stop: stop_fn calls=1 in ISR=0, user_cnt=7
stop again (already stopped): stop_fn calls=1, status_sync=0 (no block)
           FLASH:       14680 B         4 MB      0.35%
             RAM:        6192 B         4 MB      0.15%
tick=100 Hz
one-shot K_MSEC(10): 1 ticks, 9987 us, expiry in ISR=1
k_sleep(K_MSEC(10)):  2 ticks, 20002 us
  status_sync #0 -> 1 at 9940 us
  status_sync #1 -> 1 at 29940 us
  status_sync #2 -> 1 at 49941 us
  status_sync #3 -> 1 at 69942 us
  after k_msleep(70): status_get=4 remaining=20 ms
stop: stop_fn calls=1 in ISR=0, user_cnt=8
stop again (already stopped): stop_fn calls=1, status_sync=0 (no block)
```

출력에서 볼 것:

- **expiry in ISR=1**: expiry 함수는 인터럽트 문맥이다. **stop_fn은 ISR=0** — `k_timer_stop()`을 부른 main 스레드에서 돌았다. 이미 멈춘 타이머를 다시 stop하면 `stop_fn`은 안 불리고, `status_sync`는 블록하지 않고 0을 돌려준다(소스: `z_abort_timeout()`이 실패하면 바로 return).
- **one-shot `K_MSEC(10)` = 정확히 10 tick**인데 **`k_sleep(K_MSEC(10))` = 11 tick**이다. C5 §8에서 본 Zephyr의 "+1 tick 규칙"(`z_add_timeout`이 상대 타임아웃에 1을 더한다)이 `k_sleep`에는 적용되고 `k_timer`의 duration에는 **상쇄**됐다. 이유는 §7.4.
- **100 Hz에서** `K_MSEC(5)`는 올림으로 1 tick = 10 ms가 됐다(첫 sync가 9940 µs). 해상도가 10 ms면 5 ms 타이머는 없다. `k_sleep(K_MSEC(10))`은 2 tick = 20 ms다. tick rate를 명시하지 않고 QEMU에서 잰 숫자가 엉뚱한 이유다.
- `status_sync` 루프는 20 ms 격자(4.9, 24.9, 44.9, 64.9 ms)에서 정확히 깨어났다. **스레드가 k_timer에 맞춰 주기적으로 도는 표준 패턴**이다. 70 ms 동안 안 보고 있었으면 `status_get`이 그 사이 만료 횟수(3)를 알려 준다 — "몇 주기를 놓쳤나"를 공짜로 안다. FreeRTOS에는 이런 카운터가 없다.
- 100 Hz에서는 `status_get=4`다. `k_msleep(70)`이 7 tick + 1 tick = 80 ms를 자서 20 ms 주기 만료가 하나 더 지나갔다. 같은 코드가 tick rate에 따라 "놓친 주기 수"까지 달라진다. `user_cnt`(expiry 함수가 user data로 센 횟수)도 7 대 8로 다르다.

### 7.4 duration에는 +1 tick이 없다 — 오히려 1 tick까지 일찍 울린다

`z_impl_k_timer_start()`(`kernel/timer.c` 182행)의 주석과 코드다.

```c
	/* z_add_timeout() always adds one to the incoming tick count
	 * to round up to the next tick (by convention it waits for
	 * "at least as long as the specified timeout"), but the
	 * period interval is always guaranteed to be reset from
	 * within the timer ISR, so no round up is desired and 1 is
	 * subtracted in there.
	 *
	 * Note that the duration (!) value gets the same treatment
	 * for backwards compatibility.  This is unfortunate
	 * (i.e. k_timer_start() doesn't treat its initial sleep
	 * argument the same way k_sleep() does), but historical.  The
	 * timer_api test relies on this behavior.
	 */
	if (Z_IS_TIMEOUT_RELATIVE(duration)) {
		duration.ticks = max(1, duration.ticks);
		duration.ticks = duration.ticks - 1;
	}
```

`z_add_timeout()`(`kernel/timeout.c` 111행)이 `dticks = timeout.ticks + 1 + ticks_elapsed`로 1을 더하므로, 여기서 1을 빼 두면 duration N tick은 "**지금 tick이 끝난 뒤부터 센 N-1개 + 지금 tick의 남은 부분**"이 된다. 즉 tick 경계에서 정확히 시작하면 N tick, tick 중간에서 시작하면 **N tick보다 짧다.** 커널 주석이 스스로 "unfortunate … historical"이라고 부른다.

예제 z2의 마지막 줄이 이것을 측정한다(아래 §7.6의 코드). tick 안 임의 위상 200번에서 one-shot `K_MSEC(10)`을 시작했다.

```text
one-shot K_MSEC(10) from random phase: min=9017 avg=9520 max=10010 us
```

**최소 9.017 ms** — 요청보다 약 1 ms(1 tick) 짧다. C5에서 잰 `k_sleep(K_MSEC(1))`이 1.03~2.03 ms였던 것(항상 요청 이상)과 반대 방향이다. 실무 결론: `k_timer`의 duration으로 "최소 N ms는 기다린다"를 보장하려면 1 tick을 더해 준다. 반대로 period는 ISR이 만료 tick 기준으로 다시 걸기 때문에 이 문제가 없다(§9.2).

### 7.5 예제 z3(B) — expiry 함수에서 블록하면

무엇을 확인하나: expiry 함수 안에서 `k_sem_take(&never, K_FOREVER)`를 부른다. `CONFIG_ASSERT=y`로 빌드한다. 같은 소스(z3)를 `-DEXTRA_CFLAGS=-DBLOCK`으로 빌드한 것이다(소스는 §7.7).

```sh
limactl shell elinux -- bash -lc 'R=/var/rtoslab/bin; export XDG_CACHE_HOME=/tmp/rtos_D6/zcache
  $R/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D6/z3 /tmp/rtos_D6/z3b -- -DUSER_CACHE_DIR=/tmp/rtos_D6/zcache -DEXTRA_CFLAGS=-DBLOCK -DCONFIG_ASSERT=y
  ICOUNT=1 $R/rtos-run-mps2 /tmp/rtos_D6/z3b/zephyr/zephyr.elf 10 | head -20'
```

```text
           FLASH:       22700 B         4 MB      0.54%
             RAM:        6320 B         4 MB      0.15%
qemu-system-arm: terminating on signal 15 from pid 532612 (timeout)
expiry fn: in ISR=1, calling k_sem_take(K_FOREVER)...
ASSERTION FAIL [((arch_is_in_isr() == 0) || ((timeout).ticks == (((k_timeout_t){0})).ticks))] @ WEST_TOPDIR/zephyr/kernel/sem.c:136
	
r0/a1:  0x00000004  r1/a2:  0x00000088  r2/a3:  0x00000000
r3/a4:  0x00000004 r12/ip:  0x00000000 r14/lr:  0x0000171f
 xpsr:  0x2100000f
Faulting instruction address (r15/pc): 0x000038c0
```

(QEMU 종료 메시지가 파이프 버퍼링 때문에 먼저 찍혔다. 실행 순서는 아래 줄들이 먼저다.)

출력에서 볼 것: `kernel/sem.c` 136행의 assert "ISR이 아니거나, 타임아웃이 `K_NO_WAIT`이어야 한다"에 걸려 커널이 fatal error로 멈췄다. `xpsr`의 하위 비트 `0x0f` = 예외 번호 15 = **SysTick**이다. expiry 함수가 SysTick ISR 안에서 돌고 있었다는 직접 증거다. FreeRTOS에서는 같은 실수가 "다른 타이머가 밀린다"(예제 3)로 조용히 나타나지만, Zephyr에서는 **assert를 켜 두면** 즉시 잡힌다. 꺼 두면 ISR에서 스레드 스케줄 경로를 타는 미정의 동작이다. 개발 빌드에서는 `CONFIG_ASSERT=y`를 기본으로 둔다.

expiry 함수에서 할 수 있는 것은 ISR 규칙(E1)과 같다: `k_sem_give()`, `k_event_post()`, `K_NO_WAIT`의 `k_msgq_put()`, `k_work_submit()`. 무거운 일은 workqueue로 넘기는 것이 Zephyr의 정석이고 그 패턴과 `k_work_delayable`(데몬 태스크 방식에 가까운 "스레드 문맥 타이머")은 D7이 다룬다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="165.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS: 태스크가 콜백을 부른다</text>
<text x="495.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">Zephyr: 시계 ISR이 expiry fn을 부른다</text>
<rect x="20" y="40" width="290" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="165.0" y="64.2" font-size="12" text-anchor="middle">SysTick_Handler</text>
<rect x="20" y="102" width="290" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="165.0" y="126.2" font-size="12" text-anchor="middle">xTaskIncrementTick: 데몬 ready</text>
<line x1="165" y1="80" x2="165" y2="100" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah9)"/>
<rect x="20" y="164" width="290" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="165.0" y="188.2" font-size="12" text-anchor="middle">PendSV → Tmr Svc로 전환</text>
<line x1="165" y1="142" x2="165" y2="162" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah9)"/>
<rect x="20" y="226" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165.0" y="250.2" font-size="12" text-anchor="middle">prvProcessExpiredTimer → 콜백</text>
<line x1="165" y1="204" x2="165" y2="224" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah9)"/>
<rect x="350" y="40" width="290" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="495.0" y="64.2" font-size="12" text-anchor="middle">sys_clock_isr (SysTick)</text>
<rect x="350" y="102" width="290" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="495.0" y="126.2" font-size="12" text-anchor="middle">sys_clock_announce: timeout list</text>
<line x1="495" y1="80" x2="495" y2="100" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah9)"/>
<rect x="350" y="164" width="290" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="495.0" y="188.2" font-size="12" text-anchor="middle">z_timer_expiration_handler</text>
<line x1="495" y1="142" x2="495" y2="162" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah9)"/>
<rect x="350" y="226" width="290" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="495.0" y="250.2" font-size="12" text-anchor="middle">expiry_fn (ISR, 블록 금지)</text>
<line x1="495" y1="204" x2="495" y2="224" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah9)"/>
<text x="165.0" y="290.0" font-size="12" text-anchor="middle">스레드 문맥: 블록하면 다른 타이머가 밀린다</text>
<text x="495.0" y="290.0" font-size="12" text-anchor="middle">인터럽트 문맥: 블록 API는 assert</text>
</svg>
```
그림 7 — 같은 "타이머 만료"가 콜백까지 가는 길. FreeRTOS는 SysTick이 데몬을 깨우고 PendSV로 전환한 뒤 **태스크 문맥**에서 콜백을 부른다. Zephyr는 SysTick ISR 안의 `sys_clock_announce`가 timeout 리스트를 훑어 **ISR 문맥**에서 expiry 함수를 부른다.

### 7.6 예제 z2 — k_timer 1 ms 주기 지터

무엇을 확인하나: 1 kHz tick에서 1 ms 주기 `k_timer` 2001주기의 실제 주기 분포를 expiry 함수 안에서 잰다(ISR이므로 출력하지 않고 히스토그램에만 넣는다). 세 상황: 한가할 때 / 낮은 스레드(prio 5)가 CPU를 다 먹을 때 / 그 스레드가 0~300 µs씩 `irq_lock()`을 걸 때. 끝에 §7.4의 random-phase one-shot 측정을 붙였다.

```c
/* D6 z2: k_timer 1 ms 주기 지터 — 한가할 때 / 높은 스레드가 CPU를 쥘 때 / irq_lock 구간이 있을 때 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)
#define NB 13                                                /* 700..1300 us, 50 us 칸 */
static volatile uint32_t prev, n, hist[NB], mn, mx; static volatile int mode;
static uint32_t seed = 1u;
static uint32_t rnd(uint32_t m) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % m; }
static void spin_us(uint32_t us) { uint32_t t = CYC(); while (CYC() - t < us * 25u) { } }
static void exp_fn(struct k_timer *t)                        /* ISR: 주기만 재서 히스토그램에 */
{
	ARG_UNUSED(t);
	uint32_t c = CYC();
	if (n++ > 0) {
		uint32_t p = (c - prev) / 25u;
		mn = MIN(mn, p); mx = MAX(mx, p);
		int b = ((int)p - 700) / 50;
		hist[CLAMP(b, 0, NB - 1)]++;
	}
	prev = c;
}
K_TIMER_DEFINE(tm, exp_fn, NULL);
static void load_fn(void *a, void *b, void *c)              /* prio 5: main(0)이 잘 때 CPU를 다 먹는다 */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (;;) {
		if (mode == 0) { k_msleep(5); continue; }
		uint32_t len = rnd(300);
		unsigned key = (mode == 2) ? irq_lock() : 0;
		spin_us(len);                                        /* 0~300 us 계산 (mode 2: 인터럽트 잠금) */
		if (mode == 2) irq_unlock(key);
		spin_us(rnd(200));
	}
}
K_THREAD_DEFINE(load, 1024, load_fn, NULL, NULL, NULL, 5, 0, 0);
static const char *NAME[] = { "idle", "busy thread", "irq_lock 0-300 us" };
int main(void)
{
	for (mode = 0; mode < 3; mode++) {
		n = 0; mn = ~0u; mx = 0;
		for (int i = 0; i < NB; i++) hist[i] = 0;
		k_timer_start(&tm, K_MSEC(1), K_MSEC(1));
		k_msleep(2001);
		k_timer_stop(&tm);
		printk("%-20s n=%u min=%u max=%u us  hist(700..1300/50us):", NAME[mode], n - 1, mn, mx);
		for (int i = 0; i < NB; i++) printk(" %u", hist[i]);
		printk("\n");
	}
	/* one-shot K_MSEC(10)을 tick 안 임의 위상에서 시작하면 실제 길이는? */
	mode = 0;
	uint32_t smn = ~0u, smx = 0, sum = 0;
	for (int i = 0; i < 200; i++) {
		k_sleep(K_TICKS(1)); spin_us(rnd(1000));
		uint32_t c0 = CYC();
		k_timer_start(&tm, K_MSEC(10), K_NO_WAIT);
		k_timer_status_sync(&tm);
		uint32_t d = (prev - c0) / 25u;                      /* prev = expiry 시각 */
		smn = MIN(smn, d); smx = MAX(smx, d); sum += d;
	}
	printk("one-shot K_MSEC(10) from random phase: min=%u avg=%u max=%u us\n", smn, sum / 200, smx);
	register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
	__asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/var/rtoslab/bin; XDG_CACHE_HOME=/tmp/rtos_D6/zcache $R/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D6/z2 /tmp/rtos_D6/z2 -- -DUSER_CACHE_DIR=/tmp/rtos_D6/zcache; ICOUNT=1 $R/rtos-run-mps2 /tmp/rtos_D6/z2/zephyr/zephyr.elf 90'
```

```text
           FLASH:       14588 B         4 MB      0.35%
             RAM:        7392 B         4 MB      0.18%
idle                 n=2001 min=985 max=1017 us  hist(700..1300/50us): 0 0 0 0 0 333 1668 0 0 0 0 0 0
busy thread          n=2001 min=985 max=1021 us  hist(700..1300/50us): 0 0 0 0 0 100 1901 0 0 0 0 0 0
irq_lock 0-300 us    n=2001 min=717 max=1286 us  hist(700..1300/50us): 12 59 105 132 236 307 585 240 163 95 56 11 0
one-shot K_MSEC(10) from random phase: min=9017 avg=9520 max=10010 us
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 320">
<line x1="70" y1="255" x2="640" y2="255" stroke="currentColor"/>
<line x1="70" y1="45" x2="70" y2="255" stroke="currentColor"/>
<line x1="66" y1="255.0" x2="640" y2="255.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="259.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="185.0" x2="640" y2="185.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="189.0" font-size="12" text-anchor="end">600</text>
<line x1="66" y1="115.0" x2="640" y2="115.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="119.0" font-size="12" text-anchor="end">1200</text>
<line x1="66" y1="45.0" x2="640" y2="45.0" stroke="#888" stroke-opacity="0.35"/>
<text x="63.0" y="49.0" font-size="12" text-anchor="end">1800</text>
<text x="30.0" y="150.0" font-size="12" text-anchor="middle" transform="rotate(-90 30 150.0)">주기 횟수</text>
<text x="355.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">z2 — Zephyr k_timer 1 ms 주기 2001번의 실제 주기 분포</text>
<rect x="74.9" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="91.9" y="253.6" width="17" height="1.4" fill="#d0564a" fill-opacity="0.75"/>
<text x="70.0" y="273.0" font-size="12" text-anchor="middle">700</text>
<rect x="118.8" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="135.8" y="248.1" width="17" height="6.9" fill="#d0564a" fill-opacity="0.75"/>
<rect x="162.6" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="179.6" y="242.8" width="17" height="12.2" fill="#d0564a" fill-opacity="0.75"/>
<text x="157.7" y="273.0" font-size="12" text-anchor="middle">800</text>
<rect x="206.5" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="223.5" y="239.6" width="17" height="15.4" fill="#d0564a" fill-opacity="0.75"/>
<rect x="250.3" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="267.3" y="227.5" width="17" height="27.5" fill="#d0564a" fill-opacity="0.75"/>
<text x="245.4" y="273.0" font-size="12" text-anchor="middle">900</text>
<rect x="294.2" y="216.2" width="17" height="38.9" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="311.2" y="219.2" width="17" height="35.8" fill="#d0564a" fill-opacity="0.75"/>
<rect x="338.0" y="60.4" width="17" height="194.6" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="355.0" y="186.8" width="17" height="68.2" fill="#d0564a" fill-opacity="0.75"/>
<text x="333.1" y="273.0" font-size="12" text-anchor="middle">1000</text>
<rect x="381.8" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="398.8" y="227.0" width="17" height="28.0" fill="#d0564a" fill-opacity="0.75"/>
<rect x="425.7" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="442.7" y="236.0" width="17" height="19.0" fill="#d0564a" fill-opacity="0.75"/>
<text x="420.8" y="273.0" font-size="12" text-anchor="middle">1100</text>
<rect x="469.5" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="486.5" y="243.9" width="17" height="11.1" fill="#d0564a" fill-opacity="0.75"/>
<rect x="513.4" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="530.4" y="248.5" width="17" height="6.5" fill="#d0564a" fill-opacity="0.75"/>
<text x="508.5" y="273.0" font-size="12" text-anchor="middle">1200</text>
<rect x="557.2" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="574.2" y="253.7" width="17" height="1.3" fill="#d0564a" fill-opacity="0.75"/>
<rect x="601.1" y="255.0" width="17" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="618.1" y="255.0" width="17" height="0.0" fill="#d0564a" fill-opacity="0.75"/>
<text x="596.2" y="273.0" font-size="12" text-anchor="middle">1300</text>
<text x="640.0" y="273.0" font-size="12" text-anchor="middle">1350</text>
<text x="355.0" y="295.0" font-size="12" text-anchor="middle">주기 (µs, 50 µs 칸) — 한가: 985~1017 / irq_lock 0~300 µs: 717~1286</text>
<rect x="90" y="58" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="108.0" y="68.0" font-size="12" text-anchor="start">한가할 때</text>
<rect x="90" y="78" width="12" height="12" fill="#d0564a" fill-opacity="0.75"/>
<text x="108.0" y="88.0" font-size="12" text-anchor="start">다른 스레드가 irq_lock</text>
</svg>
```
그림 8 — 예제 z2 실측. 한가할 때 1 ms 주기는 985~1017 µs에 모인다. 다른 스레드가 `irq_lock`을 0~300 µs씩 걸면 717~1286 µs로 퍼진다 — 잠금 길이만큼 늦고, 다음 주기는 그만큼 짧아진다(격자는 유지).

출력에서 볼 것:

- **busy thread는 지터를 거의 안 바꿨다**(985~1021 µs). expiry 함수가 ISR이라 스레드 우선순위와 무관하다. FreeRTOS 예제 4에서 데몬보다 높은 태스크가 콜백을 6 ms 밀었던 것과 대조적이다. Zephyr 쪽이 "타이머 정확도"에서는 유리하다. 대가는 그 정확도가 **ISR이라는 제약**과 함께 온다는 것이다.
- **irq_lock은 그대로 지터가 된다**(±283 µs). 시계 인터럽트도 막히기 때문이다. 늦게 온 만료 다음 주기는 짧다(717 µs) — 커널이 다음 만료를 "실제로 늦게 불린 시각"이 아니라 "원래 만료 tick" 기준으로 다시 걸기 때문이다. 2001주기 전부 불렸다(빼먹음 없음).
- 한가할 때도 ±15 µs가 있다. tickless 모드에서 매 만료마다 SysTick LOAD를 다시 계산하고 ISR 진입 위치가 매번 조금씩 다른 것이 원인으로 보인다(QEMU icount 기준의 값이라 실칩 지터 크기로 옮기지 말 것).

### 7.7 예제 z3(A) — 인터럽트를 35 ms 잠그면 10 ms k_timer는?

FreeRTOS 예제 6에서는 밀린 만료가 몰아서 불렸다. Zephyr는 어떨까. 무엇을 확인하나: 10 ms 주기 `k_timer`가 도는 중 main이 25 ms 시점부터 35 ms 동안 `irq_lock()`을 걸고 바쁜 대기를 한다. 각 expiry의 실제 시각과 그 순간 `k_uptime_ticks()`가 보여 주는 값, 끝에서 실제 경과 시간과 커널 uptime을 비교한다.

```c
/* D6 z3: (A) 인터럽트가 35 ms 잠기면 10 ms 주기 k_timer는? (B) expiry fn에서 블록하면? */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)
#define N 9
static volatile uint32_t n, c0, t_us[N]; static volatile int64_t up[N];
K_SEM_DEFINE(never, 0, 1);
static void spin_us(uint32_t us) { uint32_t t = CYC(); while (CYC() - t < us * 25u) { } }
static void exp_fn(struct k_timer *t)
{
	ARG_UNUSED(t);
	if (n < N) { t_us[n] = (CYC() - c0) / 25u; up[n] = k_uptime_ticks(); }
	n++;
#ifdef BLOCK
	printk("expiry fn: in ISR=%d, calling k_sem_take(K_FOREVER)...\n", k_is_in_isr());
	int r = k_sem_take(&never, K_FOREVER);               /* ISR에서 블록 시도 */
	printk("expiry fn: k_sem_take returned %d\n", r);
#endif
}
K_TIMER_DEFINE(tm, exp_fn, NULL);
int main(void)
{
	k_sleep(K_TICKS(1));
	c0 = CYC();
	int64_t u0 = k_uptime_ticks();
	k_timer_start(&tm, K_MSEC(10), K_MSEC(10));
	k_msleep(25);
	unsigned key = irq_lock();                           /* 25 ms ~ 60 ms: 인터럽트 잠금 */
	spin_us(35000);
	irq_unlock(key);
	k_msleep(30);
	k_timer_stop(&tm);
	printk("end: real elapsed %u us, kernel uptime %lld ticks\n", (CYC() - c0) / 25u, k_uptime_ticks() - u0);
	printk("call  t_us    uptime_tick_seen  gap_us\n");
	for (uint32_t i = 0; i < n && i < N; i++)
		printk("#%u    %6u  %3lld              %u\n", i + 1, t_us[i], up[i] - u0,
		       i ? t_us[i] - t_us[i - 1] : t_us[i]);
	register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
	__asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/var/rtoslab/bin; export XDG_CACHE_HOME=/tmp/rtos_D6/zcache
  $R/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D6/z3 /tmp/rtos_D6/z3 -- -DUSER_CACHE_DIR=/tmp/rtos_D6/zcache
  ICOUNT=1 $R/rtos-run-mps2 /tmp/rtos_D6/z3/zephyr/zephyr.elf 30'
```

```text
           FLASH:       13964 B         4 MB      0.33%
             RAM:        6288 B         4 MB      0.15%
end: real elapsed 91926 us, kernel uptime 64 ticks
call  t_us    uptime_tick_seen  gap_us
#1      9995   10              9995
#2     19996   20              10001
#3     61013   30              41017
#4     70910   40              9897
#5     77911   50              7001
#6     87911   60              10000
```

출력에서 볼 것:

- **몰아치기가 없다.** 30·40·50 ms의 만료가 밀렸는데 잠금이 풀린 61 ms에 **한 번만** 불렸고, 그 순간 커널은 지금이 tick **30**이라고 믿고 있었다.
- 끝에서 실제 92 ms가 지났는데 커널 uptime은 **64 tick**이다. **커널 시계가 약 28 ms를 잃었다.** 이후 만료(#4 이후)는 커널 시계 기준으로는 10 ms 격자(40, 50, 60)지만 실제 시각은 그만큼 뒤로 밀려 있다.
- 원인은 SysTick 드라이버(`drivers/timer/cortex_m_systick.c`)의 전제 조건이다. 24비트 SysTick은 tickless 모드에서 LOAD를 "다음 타임아웃까지"(여기서는 최대 10 ms)로 잡고, 지나간 cycle을 `elapsed()`가 COUNTFLAG로 계산한다. 그 함수 주석의 전제(291행): "no more than one counter-wrap has occurred between the timer reset or the last time the function was called and until the current call". 인터럽트를 35 ms 막아 SysTick이 여러 번 wrap하면 한 번분만 셀 수 있고 **나머지 wrap의 시간은 사라진다.**
- FreeRTOS 포트에서도 SysTick을 tick 주기보다 오래 막으면 같은 일이 생긴다. 놓친 SysTick들은 pending 비트 하나로 합쳐지므로 `xTickCount`가 그만큼 덜 올라간다 — tick 인터럽트로 시간을 세는 RTOS 공통의 성질이다(이 노트에서는 Zephyr 쪽만 실측했다). 다만 FreeRTOS 데몬은 "지금 tick"에서 밀린 auto-reload를 몰아서 부르고(예제 6), Zephyr는 시계 자체가 늦게 가니 몰아칠 것이 없다.

결론: **긴 `irq_lock`은 Zephyr에서 타이머 지연이 아니라 시간 손실을 만든다.** 인터럽트 잠금은 1 tick보다(가능하면 훨씬) 짧게 유지한다. 몰아치기 자체는 Zephyr 소스에도 있다 — `z_timer_expiration_handler()`가 다음 만료를 `K_TIMEOUT_ABS_TICKS(k_uptime_ticks() + 1 + next.ticks)`로 걸고, `sys_clock_announce` 안에서 `k_uptime_ticks()`는 "지금 처리 중인 타임아웃의 원래 tick"을 돌려주므로, 한 번의 announce로 여러 주기가 지나갔다면 같은 루프 안에서 여러 번 불릴 수 있다. 하지만 SysTick 드라이버는 LOAD를 다음 만료까지로 잡기 때문에 시간을 잃지 않고 여러 주기가 한꺼번에 announce되는 상황을 이 보드에서는 만들지 못했다(소스로 읽은 동작, 실측 재현 못 함).

---

## 8. 소프트웨어 타이머 vs 주기 태스크 vs 하드웨어 타이머

> 🔄 **2026-11-01 보충 (E7)**: HW 타이머 ISR → 태스크 notify 방식도 태스크 단계에서는 tick 구동과 같은 지터를 받는다(최악 139 µs 대 132 µs, 같은 부하). µs 단위가 필요하면 제어 법칙을 ISR 안에서 돌린다 — E7 참고.

### 8.1 결정 표

| 기준 | 소프트웨어 타이머 (FreeRTOS) | `k_timer` (Zephyr) | 주기 태스크 (`xTaskDelayUntil`, `k_timer_status_sync`) | 하드웨어 타이머 + ISR |
|---|---|---|---|---|
| 해상도 | 1 tick | 1 tick (duration은 최대 1 tick 일찍) | 1 tick | 타이머 클럭 (예: 25 MHz = 40 ns) |
| 지연 (이 노트 실측) | tick + 11 µs (데몬 최고), 데몬보다 높은 태스크가 있으면 그 실행 시간만큼 | tick ISR 안, 스레드와 무관 | 태스크 우선순위에 따름 | 인터럽트 진입 수 µs |
| 문맥 | 데몬 태스크. 블록 금지 | ISR. 블록 금지 | 자기 태스크. 블록 가능 | ISR |
| 스택 | 데몬 스택 공유 | ISR 스택 공유 | 자기 스택 | ISR 스택 |
| 개수 비용 | 타이머당 44 B, 데몬 2 KB 한 번 | 타이머당 수십 B | 태스크당 TCB + 스택 | 주변장치 수만큼 |
| 맞는 일 | LED, 타임아웃, debounce, 재시도 | 같은 일 + 정확한 주기 신호 | 블록하는 일, 무거운 주기 처리 | 1 kHz 넘는 제어 루프, PWM/ADC 동기 |

### 8.2 예제 13 — 제어 루프를 소프트웨어 타이머로 돌리면 (계산)

무엇을 확인하나: 이 노트에서 잰 "tick → 콜백 진입 11 µs"(예제 4)를 고정 비용으로 놓고, 루프 주파수별로 필요한 tick rate와 CPU 비용, 그리고 tick 해상도 때문에 생기는 주기 양자화를 계산한다.

```python
# D6 ex13: 제어 루프를 소프트웨어 타이머로 돌리면? — 이 노트의 실측값으로 계산
TICK_TO_CB_US = 11     # 예제 4: tick 시작 → Tmr Svc 콜백 진입 (데몬 prio 7, QEMU icount)
print(f"{'loop':>8} {'period_us':>9} {'tick_hz_needed':>14} {'overhead_us':>11} {'CPU_%':>6} {'of_period_%':>11}")
for hz in (100, 1000, 8000, 20000):
    period = 1e6 / hz
    over = TICK_TO_CB_US                      # 주기마다 최소: tick ISR + 데몬 전환
    print(f"{hz:>8} {period:>9.0f} {hz:>14} {over:>11} {over * hz / 1e4:>6.1f} {100 * over / period:>11.1f}")
# 1 tick 해상도의 양자화: 요청 주기 → 실제 주기 (pdMS_TO_TICKS는 내림)
for tick_hz in (1000, 10000):
    for req_us in (125, 50, 2500):
        ticks = req_us * tick_hz // 1_000_000
        real = ticks * 1e6 / tick_hz
        print(f"tick {tick_hz:>5} Hz: request {req_us:>5} us -> {ticks} ticks = {real:>6.0f} us"
              + ("  (0 tick = 사용 불가)" if ticks == 0 else ""))
```

```sh
python3 ~/.rtos_scratch/D6/ex13_budget.py
```

```text
    loop period_us tick_hz_needed overhead_us  CPU_% of_period_%
     100     10000            100          11    0.1         0.1
    1000      1000           1000          11    1.1         1.1
    8000       125           8000          11    8.8         8.8
   20000        50          20000          11   22.0        22.0
tick  1000 Hz: request   125 us -> 0 ticks =      0 us  (0 tick = 사용 불가)
tick  1000 Hz: request    50 us -> 0 ticks =      0 us  (0 tick = 사용 불가)
tick  1000 Hz: request  2500 us -> 2 ticks =   2000 us
tick 10000 Hz: request   125 us -> 1 ticks =    100 us
tick 10000 Hz: request    50 us -> 0 ticks =      0 us  (0 tick = 사용 불가)
tick 10000 Hz: request  2500 us -> 25 ticks =   2500 us
```

출력에서 볼 것:

- 드론 FC의 8 kHz rate loop(125 µs)를 소프트웨어 타이머로 돌리려면 tick부터 8 kHz여야 하고, 타이머 경로만으로 **CPU 8.8%**를 쓴다. 게다가 이건 tick ISR의 다른 일(C5에서 잰 tick 세금)과 콜백 본체를 빼고 센 것이다. 20 kHz FOC(50 µs)는 고정 비용만 주기의 22%다.
- 1 kHz tick에서 125 µs 주기는 **0 tick**이 되어 만들 수조차 없다(`configASSERT`). 10 kHz tick에서도 125 µs는 100 µs로 잘린다. 2.5 ms는 1 kHz tick에서 2 ms가 된다(`pdMS_TO_TICKS`의 내림, C5 §1.4).
- 그래서 고속 루프는 **하드웨어 타이머(또는 IMU data-ready 인터럽트)가 직접 ISR을 일으키고**, ISR이 계산하거나 최고 우선순위 태스크에 notify한다(E5, E7). 소프트웨어 타이머는 그 루프를 "감시"(타임아웃)하는 쪽에 선다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 380">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="200" y="15" width="260" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330.0" y="41.5" font-size="13" text-anchor="middle">주기적/지연된 일이 하나 생겼다</text>
<rect x="200" y="85" width="260" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330.0" y="106.2" font-size="12" text-anchor="middle">필요한 정밀도가 1 tick보다</text>
<text x="330.0" y="122.2" font-size="12" text-anchor="middle">작은가? (또는 > 1 kHz)</text>
<line x1="330" y1="59" x2="330" y2="83" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<rect x="480" y="85" width="165" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="562.5" y="106.2" font-size="12" text-anchor="middle">하드웨어 타이머</text>
<text x="562.5" y="122.2" font-size="12" text-anchor="middle">+ ISR (+ DMA)</text>
<line x1="460" y1="110" x2="478" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<text x="469.0" y="102.0" font-size="12" text-anchor="middle">예</text>
<rect x="200" y="165" width="260" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330.0" y="186.2" font-size="12" text-anchor="middle">일이 블록하거나 길거나</text>
<text x="330.0" y="202.2" font-size="12" text-anchor="middle">스택을 많이 쓰는가?</text>
<line x1="330" y1="135" x2="330" y2="163" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<text x="342.0" y="153.0" font-size="12" text-anchor="start">아니오</text>
<rect x="480" y="165" width="165" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="562.5" y="186.2" font-size="12" text-anchor="middle">전용 주기 태스크</text>
<text x="562.5" y="202.2" font-size="12" text-anchor="middle">xTaskDelayUntil</text>
<line x1="460" y1="190" x2="478" y2="190" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<text x="469.0" y="182.0" font-size="12" text-anchor="middle">예</text>
<rect x="200" y="245" width="260" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330.0" y="274.2" font-size="12" text-anchor="middle">FreeRTOS인가, Zephyr인가?</text>
<line x1="330" y1="215" x2="330" y2="243" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<text x="342.0" y="233.0" font-size="12" text-anchor="start">아니오 (짧고 논블로킹)</text>
<rect x="15" y="320" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165.0" y="341.2" font-size="12" text-anchor="middle">FreeRTOS 소프트웨어 타이머</text>
<text x="165.0" y="357.2" font-size="12" text-anchor="middle">(데몬 태스크 문맥)</text>
<rect x="345" y="320" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="495.0" y="341.2" font-size="12" text-anchor="middle">Zephyr k_timer (ISR 문맥)</text>
<text x="495.0" y="357.2" font-size="12" text-anchor="middle">무거우면 k_work로 넘김 (D7)</text>
<line x1="280" y1="295" x2="200" y2="318" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<line x1="380" y1="295" x2="460" y2="318" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah8)"/>
<text x="20.0" y="104.0" font-size="12" text-anchor="start">HW 타이머 예:</text>
<text x="20.0" y="122.0" font-size="12" text-anchor="start">20 kHz FOC,</text>
<text x="20.0" y="140.0" font-size="12" text-anchor="start">8 kHz rate loop</text>
<text x="20.0" y="258.0" font-size="12" text-anchor="start">SW 타이머 예:</text>
<text x="20.0" y="276.0" font-size="12" text-anchor="start">heartbeat LED,</text>
<text x="20.0" y="294.0" font-size="12" text-anchor="start">link-loss 타임아웃</text>
</svg>
```
그림 9 — 주기적·지연된 일 하나를 어디에 맡길지 고르는 흐름. 정밀도가 tick보다 촘촘하면 하드웨어 타이머, 블록하거나 무거우면 전용 주기 태스크, 짧고 논블로킹이면 소프트웨어 타이머다.

### 8.3 드론 FC 한 대의 배치 예

예를 들어 STM32H7급 드론 비행 제어기라면 이렇게 나눈다.

- **IMU data-ready EXTI 인터럽트(8 kHz)** → 자이로 읽기 DMA 시작 → 완료 ISR이 rate-loop 태스크(최고 우선순위)에 notify. 하드웨어가 박자를 정한다.
- **ESC 출력(DShot)**: 타이머 + DMA. 소프트웨어 개입 없음.
- **attitude loop(1 kHz), 센서 퓨전**: 주기 태스크(`xTaskDelayUntil`) 또는 rate-loop의 8번에 한 번.
- **소프트웨어 타이머**: RC link-loss 100 ms, GNSS 타임아웃 1 s, 상태 LED, 배터리 저속 샘플링 트리거 50 ms, arm 스위치 debounce 20 ms, 텔레메트리 송신 트리거. 데몬은 높게, 콜백은 전부 "기록 + notify".

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나

### 9.1 FreeRTOS — `timers.c` (V11.3.2, 1345줄)

타이머 하나 = `Timer_t`(`timers.c` 83행).

```c
typedef struct tmrTimerControl
{
    const char * pcTimerName;
    ListItem_t xTimerListItem;          /* 알람 목록의 항목. 항목 값 = 만료 tick */
    TickType_t xTimerPeriodInTicks;
    void * pvTimerID;
    portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t pxCallbackFunction;
    #if ( configUSE_TRACE_FACILITY == 1 )
        UBaseType_t uxTimerNumber;
    #endif
    uint8_t ucStatus;                   /* tmrSTATUS_IS_ACTIVE 0x01, _IS_STATICALLY_ALLOCATED 0x02, _IS_AUTORELOAD 0x04 */
} xTIMER;
typedef xTIMER Timer_t;
```

필드 합이 4 + 20 + 4 + 4 + 4 + 4 + 1(+패딩 3) = 44 B로, 예제 11의 `sizeof(StaticTimer_t) = 44`와 맞는다. `ListItem_t`(C1)를 그대로 쓰기 때문에 알람 목록은 지연 태스크 목록(C5)과 **같은 정렬 삽입 리스트**(`vListInsert`, 만료 tick 오름차순)다. active 여부를 "리스트에 들어 있나"가 아니라 `ucStatus` 비트로 따로 두는 이유는 History.txt에 있다: 명령 처리 중 잠깐 리스트에서 빠졌다 다시 들어가는 순간 `xTimerIsTimerActive()`가 거짓을 말하지 않게 하려고(History.txt의 V10.2.0 항목, 2019년).

전역 상태(모두 `static`, 데몬만 만진다):

| 이름 | 뜻 |
|---|---|
| `xActiveTimerList1`, `xActiveTimerList2` | 알람 목록 두 개 (각 20 B) |
| `pxCurrentTimerList` | 지금 tick 주기의 만료들 |
| `pxOverflowTimerList` | 만료 tick이 wrap 너머인 것들 |
| `xTimerQueue` | 명령 큐 (`DaemonTaskMessage_t` 16 B × `configTIMER_QUEUE_LENGTH`), 레지스트리 이름 `"TmrQ"` |
| `xTimerTaskHandle` | 데몬 TCB. `xTimerGetTimerDaemonTaskHandle()` |

명령 메시지는 `{ xMessageID; union { {xMessageValue, pxTimer}, {pxCallbackFunction, pvParameter1, ulParameter2} } }`다. 음수 ID(`tmrCOMMAND_EXECUTE_CALLBACK` −1, `_FROM_ISR` −2)는 D7의 pended function call이고, 0~5는 태스크 명령, 6~9는 ISR 명령(`tmrFIRST_FROM_ISR_COMMAND = 6`)이다. `xTimerGenericCommand` 매크로가 ID로 `...FromTask`/`...FromISR`를 고른다.

함수 지도(행 번호는 V11.3.2 `timers.c`):

| 함수 | 행 | 하는 일 |
|---|---|---|
| `xTimerCreateTimerTask` | 237 | 큐·리스트 초기화(`prvCheckForValidListAndQueue` 1116) 후 데몬 생성. `vTaskStartScheduler`가 부름 |
| `xTimerGenericCommandFromTask` | 448 | 명령을 `xQueueSendToBack`. 스케줄러가 안 돌면 대기 0 |
| `xTimerGenericCommandFromISR` | 499 | `xQueueSendToBackFromISR` |
| `prvTimerTask` | 746 | 데몬 무한 루프 (§2.3) |
| `prvProcessTimerOrBlockTask` | 780 | 스케줄러 잠금 안에서 시각 확인 → 만료 처리 또는 `vQueueWaitForMessageRestricted`로 블록 |
| `prvGetNextExpireTime` | 842 | 현재 리스트 맨 앞 값. 비었으면 0을 돌려줘 wrap 때 깨어나게 |
| `prvSampleTimeNow` | 869 | tick이 지난번보다 작아졌으면 wrap → `prvSwitchTimerLists` |
| `prvInsertTimerInActiveList` | 892 | 현재/overflow 리스트에 넣거나, 이미 지났으면 "지금 처리" 반환 |
| `prvProcessReceivedCommands` | 936 | 큐가 빌 때까지 명령 처리 (Start/Reset/Stop/ChangePeriod/Delete) |
| `prvReloadTimer` | 697 | auto-reload 재삽입 + 밀린 만큼 콜백 (§6.1) |
| `prvProcessExpiredTimer` | 716 | 리스트에서 빼고 reload 또는 비활성화, 콜백 |
| `prvSwitchTimerLists` | 1091 | wrap: 현재 리스트에 남은 것(전부 만료된 것)을 처리하고 두 포인터를 맞바꿈 |

tick wrap을 두 리스트로 견디는 방식은 태스크 지연 리스트(C5 §4.2의 `taskSWITCH_DELAYED_LISTS`)와 같은 발상이다. 차이는 타이머 쪽은 **ISR이 아니라 데몬이** wrap을 감지한다는 것이다(`prvSampleTimeNow`가 "지금 tick < 지난번 tick"을 본다). 그래서 데몬이 wrap을 지나서야 깨어나도 처리할 수 있도록, 목록이 비었을 때는 만료 시각을 0으로 잡아 wrap 순간에 깨어난다(`prvGetNextExpireTime`의 주석).

`vQueueWaitForMessageRestricted()`는 `queue.c`에 있는 커널 내부 전용 함수로, 스케줄러가 잠긴 상태에서 "이 큐에 메시지가 오거나 이 시간이 지나면 깨워 줘"를 걸어 둔다(`xListWasEmpty`가 참이면 무한 대기 = C1에서 본 "Tmr Svc가 suspended 리스트에 있다"의 정체). 데몬이 블록하는 유일한 곳이 여기다.

### 9.2 Zephyr — `kernel/timer.c`, `kernel/timeout.c` (v4.4.2)

`struct k_timer`(`include/zephyr/kernel.h` 1777행)의 핵심 필드는 `struct _timeout timeout`, `_wait_q_t wait_q`(status_sync로 기다리는 스레드 하나), `expiry_fn`, `stop_fn`, `k_timeout_t period`, `uint32_t status`, `void *user_data`다. `struct _timeout`(`kernel_structs.h` 299행)은 `sys_dnode_t node`, `_timeout_func_t fn`, `int64_t dticks`(`CONFIG_TIMEOUT_64BIT=y`, 기본) — 스레드의 `k_sleep` 타임아웃과 **같은 구조체, 같은 리스트**다.

흐름은 이렇다.

1. `k_timer_start()` → `z_add_timeout(&timer->timeout, z_timer_expiration_handler, duration-1)`. timeout 리스트는 **delta list**다(`timeout.c` 120행의 루프: 앞 항목들의 `dticks`를 빼 가며 자리를 찾는다). C5 §8에서 본 그 리스트다.
2. SysTick 인터럽트 → `sys_clock_isr()`(`drivers/timer/cortex_m_systick.c` 349행)가 지난 tick 수를 계산해 `sys_clock_announce(dticks)`.
3. `sys_clock_announce_locked()`(`timeout.c` 223행)가 맨 앞 타임아웃의 `dticks`가 남은 tick 이하인 동안 반복: `curr_tick += dt` → 리스트에서 빼고 → **lock을 풀고 `t->fn(t)` 호출**. 이 `fn`이 `z_timer_expiration_handler`다. 아직 ISR 안이다.
4. `z_timer_expiration_handler()`(`timer.c` 71행): period가 있으면 다음 만료를 `K_TIMEOUT_ABS_TICKS(k_uptime_ticks() + 1 + (period - 1))`로 **절대 tick**에 다시 건다. announce 중에는 `elapsed()`가 0이고 `curr_tick`이 "이번 타임아웃의 원래 tick"이므로, 늦게 불렸어도 격자 기준이다(소스 주석: "we still end up calculating the next expiration as a regular stride from where we 'should' have run"). 그다음 `status += 1`, `expiry_fn(timer)` 호출(lock 해제 상태), 마지막으로 `wait_q`에 스레드가 있으면 깨운다(`z_ready_thread`).
5. `k_timer_status_sync()`는 `status`가 0이면 `z_pend_curr(&lock, key, &timer->wait_q, K_FOREVER)`로 블록한다. 그래서 ISR에서 부르면 `__ASSERT(!arch_is_in_isr(), "")`에 걸린다.

`k_timer_stop()`이 `stop_fn`을 부르는 조건은 `z_abort_timeout()`이 성공했을 때, 즉 **리스트에 걸려 있던(달리던) 타이머**일 때뿐이다(예제 z1의 "stop again" 줄). 그리고 `wait_q`의 스레드를 깨워 `status_sync`가 0을 돌려주게 한다.

### 9.3 다른 노트와의 연결

- C3 · C9: `Tmr Svc`가 첫 태스크가 되는 이유, `vApplicationDaemonTaskStartupHook`.
- C5: `ListItem_t` 정렬 삽입, tick wrap과 리스트 두 개, `xTaskDelayUntil`과 auto-reload의 같은 격자 성질, Zephyr delta list와 +1 tick.
- C6: 데몬의 블록 시간 = 다음 만료까지 → tickless idle이 잘 수 있는 시간의 상한. 타이머가 많고 짧으면 tickless의 이득이 줄어든다.
- D7: 같은 큐를 쓰는 `xTimerPendFunctionCall(FromISR)`, Zephyr의 `k_work_delayable`(스레드 문맥 타이머).
- EL F8 §6.5: Linux의 timer wheel은 "만료를 정확히 정렬하지 않고 버킷에 대충 넣는" 대신 O(1) 삽입을 얻었다. FreeRTOS 타이머 리스트는 반대로 **정확히 정렬**(삽입 O(n))하고 데몬이 맨 앞만 본다. 타이머 수가 수십 개인 MCU에서는 이쪽이 맞다.

---
## 10. 실무에서 다시 보기

### 10.1 드론 RC 링크 끊김 failsafe — 예제 8

드론은 RC 수신기에서 50~500 Hz로 조종 패킷을 받는다. 패킷이 일정 시간(보통 100 ms~1 s) 안 오면 **failsafe**(호버링, RTL, 착륙)로 들어가야 한다. 소프트웨어 타이머로 가장 깔끔하게 만드는 방법은 "**패킷이 올 때마다 one-shot 타이머를 리셋**하고, 만료되면 failsafe"다. 정상일 때 타이머는 영원히 울리지 않는다. 워치독과 같은 발상이다.

무엇을 확인하나: TIMER0 인터럽트를 RC 프레임 수신 ISR로 삼아 18~22 ms 간격으로 15개의 패킷을 보낸 뒤 끊는다. ISR은 `xTimerResetFromISR()`로 100 ms one-shot을 미룬다. 만료 콜백은 시각을 기록하고 비행 태스크에 `xTaskNotify`만 한다. 350 ms부터 60 ms 동안 무거운 일(로그 flush 같은)을 하는 prio 3 태스크가 있다. 데몬 우선순위 7과 1로 돌린다.

```c
/* D6 ex8: 드론 RC 링크 끊김 failsafe — 패킷마다 one-shot 100 ms를 ResetFromISR, 만료 = 링크 끊김 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define FS_MS 100u
static TimerHandle_t fs; static TaskHandle_t flight_h;
static volatile uint32_t npkt, c0, last_pkt_us, fs_us;
static uint32_t seed = 7u;
void TIMER0_IRQHandler(void)                             /* RC 수신기 UART 프레임 완료 인터럽트라고 치자 */
{
    BaseType_t woken = pdFALSE;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    last_pkt_us = US(CYC() - c0);
    xTimerResetFromISR(fs, &woken);                      /* "아직 살아 있다" → 마감을 100 ms 뒤로 */
    seed = seed * 1103515245u + 12345u;
    TIMER_VALUE(TIMER0_BASE) = TIMER_RELOAD(TIMER0_BASE) = 25000u * (18u + (seed >> 16) % 5u);   /* 다음 패킷 18~22 ms */
    if (++npkt == 15) TIMER_CTRL(TIMER0_BASE) = 0;       /* 15번째 뒤 링크 끊김(송신기 꺼짐) */
    portYIELD_FROM_ISR(woken);
}
static void fs_cb(TimerHandle_t t)                       /* 짧게: 기록 + 비행 태스크에 알리기만 */
{
    (void)t;
    fs_us = US(CYC() - c0);
    xTaskNotify(flight_h, 1u, eSetBits);
}
static void hog(void *p)                                 /* 350 ms에 60 ms짜리 무거운 일(로그 flush 등) */
{
    (void)p;
    vTaskDelay(pdMS_TO_TICKS(350)); spin_us(60000);
    vTaskDelete(NULL);
}
static void flight(void *p)
{
    (void)p;
    uint32_t bits;
    fs = xTimerCreate("rc_fs", pdMS_TO_TICKS(FS_MS), pdFALSE, NULL, fs_cb);
    xTimerStart(fs, 0);
    c0 = CYC();
    TIMER_RELOAD(TIMER0_BASE) = 25000u * 20u; TIMER_VALUE(TIMER0_BASE) = 25000u * 20u;
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
    xTaskNotifyWait(0, ~0u, &bits, portMAX_DELAY);       /* 실제 FC라면 여기서 제어 루프가 돈다 */
    board_printf_raw("daemon prio %u: packets=%u last at %u us, FAILSAFE at %u us, gap %u us (limit %u ms)\n",
                     (unsigned)configTIMER_TASK_PRIORITY, (unsigned)npkt, (unsigned)last_pkt_us,
                     (unsigned)fs_us, (unsigned)(fs_us - last_pkt_us), FS_MS);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(flight, "flight", 512, NULL, 5, &flight_h);
    xTaskCreate(hog, "hog", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex8_failsafe.c ex8; ./run.sh ex8_failsafe.c ex8b -DD6_TMR_PRIO=1'
```

```text
daemon prio 7: packets=15 last at 297120 us, FAILSAFE at 396960 us, gap 99840 us (limit 100 ms)
[board_exit] code=0
daemon prio 1: packets=15 last at 297120 us, FAILSAFE at 410007 us, gap 112887 us (limit 100 ms)
[board_exit] code=0
```

출력에서 볼 것:

- **데몬 prio 7**: 마지막 패킷 뒤 **99.84 ms**에 failsafe. 100 ms보다 조금 짧은 것은 tick 양자화다. 리셋 명령의 기준 시각이 ISR 시점의 tick 값(내림)이라, 마지막 패킷이 tick 경계 뒤 x ms에 왔다면 만료는 그 패킷 뒤 (100 − x) ms + 콜백 경로 시간이 된다. 여기서는 x가 약 0.17 ms였다. **소프트웨어 타이머의 정밀도는 ±1 tick**이라는 것을 그대로 보여 준다.
- **데몬 prio 1**: **112.9 ms**. 만료 시각(397 ms)에 prio 3 태스크가 무거운 일을 하고 있어 데몬이 410 ms까지 못 돌았다. 100 ms를 요구사항으로 적었다면 **요구사항 위반**이다. 안전 기능을 맡는 타이머가 있으면 데몬 우선순위는 "그 기능의 마감을 지킬 수 있는가"로 정한다.
- 콜백은 `xTaskNotify` 한 줄이다. failsafe 진입 자체(모드 전환, 모터 명령 변경)는 비행 태스크가 자기 루프에서 한다. 콜백에서 직접 모터 출력을 바꾸면 비행 태스크와 경쟁(race)이 생긴다.

실무 보강: 실제 FC는 이 타이머 하나에 기대지 않고, 비행 태스크도 매 루프에서 "마지막 패킷 시각"을 직접 비교하는 이중 감시를 둔다. 데몬이 어떤 이유로든 멈추면(예: 다른 콜백의 버그) 타이머 기반 failsafe도 같이 멈추기 때문이다. §10.3의 워치독 이야기와 같은 이유다.

### 10.2 버튼 · 스위치 debounce — 예제 9

기계식 스위치는 누르는 순간 수 ms 동안 0과 1을 수십 번 오간다(bounce). 엣지 인터럽트마다 이벤트를 내면 한 번 누름이 여러 번으로 보인다. 소프트웨어 타이머 debounce는 "**엣지마다 10 ms one-shot을 리셋하고, 조용해진 뒤 만료되면 그때 레벨을 한 번 읽는다**"다.

무엇을 확인하나: TIMER0으로 엣지 10개(누름 바운스 5개 → 40 ms 유지 → 뗌 바운스 5개)를 흉내 내고, 엣지 ISR마다 `xTimerResetFromISR()`. 콜백이 몇 번, 어떤 레벨로 불리는지 본다.

```c
/* D6 ex9: 버튼 debounce — 엣지 ISR마다 one-shot 10 ms Reset, 조용해진 뒤 한 번만 레벨을 읽는다 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
/* 엣지 간격(us): 누름 바운스 5엣지 → 40 ms 유지 → 뗌 바운스 5엣지 */
static const uint32_t EDGE_US[] = { 1000, 300, 400, 900, 800, 40000, 250, 500, 600, 300 };
#define NE (sizeof EDGE_US / sizeof EDGE_US[0])
static TimerHandle_t db;
static volatile uint32_t ei, level, c0, nevt;
void TIMER0_IRQHandler(void)                             /* GPIO 엣지 인터럽트 흉내 */
{
    BaseType_t woken = pdFALSE;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    level ^= 1u;                                         /* 핀 레벨이 뒤집혔다 */
    xTimerResetFromISR(db, &woken);                      /* 엣지마다 '조용해질 때까지' 마감 연장 */
    if (++ei < NE) TIMER_VALUE(TIMER0_BASE) = TIMER_RELOAD(TIMER0_BASE) = 25u * EDGE_US[ei];
    else TIMER_CTRL(TIMER0_BASE) = 0;
    portYIELD_FROM_ISR(woken);
}
static void db_cb(TimerHandle_t t)                       /* 10 ms 동안 엣지가 없었다 = 안정 */
{
    (void)t;
    nevt++;
    board_printf_raw("  t=%u us  edges so far=%u  stable level=%u -> %s\n", (unsigned)US(CYC() - c0),
                     (unsigned)ei, (unsigned)level, level ? "PRESSED" : "RELEASED");
}
static void app(void *p)
{
    (void)p;
    db = xTimerCreate("deb", pdMS_TO_TICKS(10), pdFALSE, NULL, db_cb);
    c0 = CYC();
    TIMER_RELOAD(TIMER0_BASE) = 25u * EDGE_US[0]; TIMER_VALUE(TIMER0_BASE) = 25u * EDGE_US[0];
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
    vTaskDelay(pdMS_TO_TICKS(80));
    board_printf_raw("raw edges=%u, debounced events=%u\n", (unsigned)ei, (unsigned)nevt);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex9_debounce.c ex9'
```

```text
  t=12992 us  edges so far=5  stable level=1 -> PRESSED
  t=54992 us  edges so far=10  stable level=0 -> RELEASED
raw edges=10, debounced events=2
[board_exit] code=0
```

출력에서 볼 것: 엣지 10개가 이벤트 **2개**(PRESSED, RELEASED)가 됐다. 마지막 누름 엣지가 3.4 ms에 왔으니 이론상 13.4 ms에 확정돼야 하는데 실제는 12.99 ms다 — 예제 8과 같은 tick 내림 때문이다. debounce 시간은 "최소 9 ms, 최대 10 ms"로 이해하고 바운스 최대 길이보다 넉넉히 잡는다. 엣지 인터럽트가 매우 잦은 스위치라면 ISR마다 큐에 명령을 넣는 비용(예제 7: 약 5 µs/명령)과 큐 넘침을 계산에 넣는다. 그 경우는 ISR에서 엣지 시각만 기록하고 주기 타이머(예: 5 ms auto-reload)가 샘플링하는 방식이 더 싸다.

### 10.3 워치독 — 타이머 콜백에서 "무조건" 차면 안 된다 — 예제 10

흔한 초보 설계: "20 ms auto-reload 타이머를 만들고 콜백에서 IWDG를 kick한다." 이 설계는 **데몬 태스크가 살아 있다는 것**만 증명한다. 제어 태스크가 무한 루프나 교착에 빠져도, 데몬이 그보다 높은 우선순위로 돌고 있으면 kick은 계속된다.

무엇을 확인하나: tick hook으로 하드웨어 워치독(50 ms 안에 kick 없으면 리셋)을 흉내 낸다. 제어 태스크(prio 3)가 200 ms에 무한 루프로 멈춘다. (1) 콜백이 무조건 kick, (2) 콜백이 "모든 감시 대상 태스크가 체크인 비트를 세웠을 때만" kick하는 두 정책을 비교한다.

```c
/* D6 ex10: 워치독을 타이머 콜백에서 '무조건' 차면 멈춘 태스크를 못 잡는다 — 체크인 비트로 고친다 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d6.h"
#define WDT_MS 50u                                       /* 이 시간 안에 kick이 없으면 리셋 */
#define ALL    0x3u                                      /* 감시할 태스크 2개: ctrl(bit0), comm(bit1) */
static volatile uint32_t wd_deadline = WDT_MS, alive;
void vApplicationTickHook(void)                          /* 하드웨어 워치독 흉내: tick마다 마감 검사 */
{
    uint32_t now = xTaskGetTickCountFromISR();
    if ((int32_t)(now - wd_deadline) > 0) {
        board_printf_raw("  WATCHDOG RESET at tick %u (last kick %u)\n", (unsigned)now,
                         (unsigned)(wd_deadline - WDT_MS));
        board_exit(0);
    }
}
static void kick_cb(TimerHandle_t t)                     /* 20 ms마다 */
{
    (void)t;
#ifdef CHECKED
    if ((alive & ALL) != ALL) return;                    /* 누가 체크인 안 했으면 차지 않는다 */
    alive = 0;
#endif
    wd_deadline = xTaskGetTickCount() + WDT_MS;
}
static void ctrl(void *p)                                /* 10 ms 제어 루프, 200 ms에 멈춘다 */
{
    (void)p;
    TickType_t w = xTaskGetTickCount();
    for (;;) {
        alive |= 1u;
        if (xTaskGetTickCount() >= 200) { board_printf_raw("  ctrl hangs at tick %u\n",
                                          (unsigned)xTaskGetTickCount()); for (;;) { } }
        xTaskDelayUntil(&w, pdMS_TO_TICKS(10));
    }
}
static void comm(void *p)
{
    (void)p;
    for (;;) { alive |= 2u; vTaskDelay(pdMS_TO_TICKS(15)); }
}
static void boss(void *p)
{
    (void)p;
#ifdef CHECKED
    board_printf_raw("policy: kick only if all tasks checked in\n");
#else
    board_printf_raw("policy: kick unconditionally from timer callback\n");
#endif
    xTimerStart(xTimerCreate("kick", pdMS_TO_TICKS(20), pdTRUE, NULL, kick_cb), 0);
    vTaskDelay(pdMS_TO_TICKS(600));
    board_printf_raw("  tick 600: no watchdog reset — ctrl has been dead for 400 ms\n");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(ctrl, "ctrl", 256, NULL, 3, NULL);
    xTaskCreate(comm, "comm", 256, NULL, 2, NULL);
    xTaskCreate(boss, "boss", 512, NULL, 6, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/D6; ./run.sh ex10_wdt.c ex10 -DconfigUSE_TICK_HOOK=1; ./run.sh ex10_wdt.c ex10b -DconfigUSE_TICK_HOOK=1 -DCHECKED'
```

```text
policy: kick unconditionally from timer callback
  ctrl hangs at tick 200
  tick 600: no watchdog reset — ctrl has been dead for 400 ms
[board_exit] code=0
policy: kick only if all tasks checked in
  ctrl hangs at tick 200
  WATCHDOG RESET at tick 251 (last kick 200)
[board_exit] code=0
```

출력에서 볼 것: 무조건 kick은 제어 태스크가 **400 ms 동안 죽어 있는데도** 워치독을 계속 먹였다. 체크인 방식은 마지막 kick(200) 뒤 51 tick에 리셋했다. 체크인 비트는 "각 태스크가 자기 루프 한 바퀴를 돌았다"는 증거이고, 콜백은 그 증거가 다 모였을 때만 kick한다. 더 엄격하게 하려면 kick을 소프트웨어 타이머가 아니라 **가장 낮은 우선순위 태스크**(또는 idle hook)에서 하고, 체크인 검사도 거기서 한다 — 그러면 "CPU가 굶어서 낮은 태스크가 못 도는 상황"까지 잡힌다. 어느 쪽이든 핵심은 같다: **워치독을 먹이는 코드는 감시하려는 대상이 살아 있다는 것을 확인한 다음에만 먹인다.**

### 10.4 SSD 펌웨어와 비교 — 명령 타임아웃

Don이 SSD에서 다룬 NVMe 명령 타임아웃을 소프트웨어 타이머로 옮긴다면 선택지는 둘이다.

1. **명령마다 타이머 하나**: 동시에 수백 개가 active가 된다. FreeRTOS 타이머 리스트는 정렬 삽입(O(n))이라 명령 하나를 넣을 때마다 수백 항목을 훑는다. 게다가 시작·정지가 전부 명령 큐를 거친다. 명령 처리 경로(수 µs 단위)에 넣기엔 비싸다.
2. **주기 타이머 하나 + 명령 테이블**: 10 ms auto-reload 콜백이 "마감이 지난 명령이 있나"를 표시만 하고 처리 태스크에 notify한다. 명령 테이블에는 시작 tick만 기록한다(unsigned 뺄셈으로 wrap 안전, C5 §4.4). SSD 펌웨어가 실제로 하던 방식이고, 정밀도가 10 ms면 충분한 타임아웃(보통 수 초)에 맞는다.

Linux 커널이 수만 개의 네트워크·블록 타임아웃을 timer wheel(EL F8 §6.5)로 처리하는 이유도 같다 — 대부분의 타임아웃은 **취소되는 것이 정상**이고 정확한 만료 시각은 중요하지 않다. RTOS에서 개수가 많아지면 2번 패턴을 쓴다.

### 10.5 heartbeat LED — 가장 흔한 첫 타이머

```c
static void hb_cb(TimerHandle_t t) { (void)t; GPIO_TOGGLE(LED_PIN); }   /* 레지스터 쓰기 하나 */
/* main(): */
xTimerStart(xTimerCreate("hb", pdMS_TO_TICKS(500), pdTRUE, NULL, hb_cb), 0);
```

(실행하지 않은 스케치다 — `GPIO_TOGGLE`·`LED_PIN`은 보드별 매크로라 이 랩 보드에는 없다.) LED가 깜빡이면 "스케줄러가 돌고, tick이 가고, 데몬이 산다"는 세 가지가 동시에 증명된다. LED가 가끔 멈칫한다면 §4(블록하는 콜백)나 §5(데몬보다 높은 태스크의 긴 실행)를 의심한다. 반대로 LED는 멀쩡한데 제품이 죽어 있다면 §10.3의 함정이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 콜백에서 `vTaskDelay`·블로킹 큐/세마포어 | 다른 타이머가 블록 시간만큼 늦고 몰아서 울림 (예제 3) | 콜백 = 데몬 태스크. 데몬이 잠든다 | 콜백은 기록 + `xTaskNotify`. 무거운 일은 태스크로 (D7) |
| 데몬 우선순위를 너무 낮게 | 부하 때 타이머가 수 ms 늦음, failsafe 113 ms (예제 4, 8) | 데몬보다 높은 태스크가 CPU를 쥠 | 데몬을 높게 두고 콜백을 짧게 |
| 데몬 스택을 기본값 그대로 두고 콜백에 큰 지역 변수 | StackOverflowHook "Tmr Svc", 또는 아무 메시지 없이 멈춤 (예제 11) | 모든 콜백이 데몬 스택 공유 | 가장 큰 콜백 + 124 B + 여유. high-water mark 감시 |
| `xTicksToWait`를 타이머 시간으로 착각 | 타이머가 엉뚱한 주기 또는 시작 안 됨 | 그 인자는 큐 대기 시간 | period는 `xTimerCreate`/`ChangePeriod`로 |
| `FromISR` 반환값 무시 | 가끔 리셋이 안 먹어 오탐 failsafe | 명령 큐가 가득 참 (예제 7: 12개 중 2개 실패) | 큐 길이 계산, 실패 카운터 |
| `portYIELD_FROM_ISR` 누락 | ISR → 타이머 동작이 최대 1 tick 늦음 (631 µs) | 다음 tick까지 데몬 전환이 미뤄짐 | ISR 끝에서 `portYIELD_FROM_ISR(woken)` |
| 주기 작업을 one-shot 재무장으로 | 주기가 조금씩 길어짐 (100주기에 +142 ms, 예제 5) | 재무장 기준 시각 = 콜백 끝난 지금 | auto-reload 사용 |
| auto-reload 몰아치기를 모름 | 필터·적분이 같은 순간 샘플을 여러 번 받음 (예제 6) | `prvReloadTimer`가 밀린 주기만큼 연달아 호출 | 간격 검사, 데몬 붙잡는 원인 제거 |
| 워치독을 타이머 콜백에서 무조건 kick | 태스크가 죽어도 리셋 안 됨 (예제 10) | 데몬만 살아 있으면 kick | 체크인 비트, 또는 최저 우선순위에서 kick |
| Zephyr expiry fn에서 블록 / 긴 처리 | `ASSERTION FAIL ... sem.c:136` 또는 다른 인터럽트 지연 | expiry fn은 SysTick ISR (예제 z3) | `k_sem_give`·`k_work_submit`로 넘김 |
| Zephyr에서 긴 `irq_lock` | 타이머가 늦을 뿐 아니라 uptime이 실제보다 느려짐 (28 ms 손실, 예제 z3) | SysTick 드라이버는 wrap 1회까지만 셈 | 잠금을 1 tick보다 훨씬 짧게 |
| Zephyr `k_timer` duration을 "최소 시간"으로 믿음 | 10 ms 타임아웃이 9.0 ms에 만료 (예제 z2) | duration에서 1 tick을 뺌 (`timer.c` 주석) | 최소 보장이 필요하면 1 tick 더하기 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "What happens if a FreeRTOS software timer callback blocks?"

**A.** 콜백은 타이머 서비스 태스크(데몬) 안에서 차례로 불리는 함수다. 콜백이 블록하면 데몬 자체가 잠들어 그동안 다른 모든 타이머의 만료와 명령 큐 처리가 멈춘다. 측정해 보면 다른 타이머의 최대 지연이 블록 시간과 같아지고, 깨어난 뒤 auto-reload 타이머는 밀린 만큼 연달아 불린다. 그래서 콜백은 기록하고 태스크에 notify만 한다.

> A software timer callback runs inside the timer service task, one callback after another. If it blocks, the daemon itself sleeps, so every other timer and every pending timer command stalls for that long. When I measured it, a 40 ms vTaskDelay in one callback delayed a 5 ms heartbeat by exactly 40 ms, and then the heartbeat fired nine times back to back. So callbacks must be short and non-blocking — record state, notify a task, return.

**꼬리 질문**: "How do you size the timer task stack and priority?" → 가장 스택을 많이 쓰는 콜백 + 데몬 기본(약 124 B) + 여유, high-water mark 감시. 우선순위는 높게 두되 콜백을 짧게.

**Q.** "Why does xTimerStartFromISR go through a queue? What is the cost?"

**A.** 타이머 리스트는 데몬만 만지게 설계해서 리스트를 고칠 때 인터럽트를 끌 필요가 없다. ISR은 16바이트 명령을 큐에 넣고 끝난다. 대가는 명령 지연이다 — yield를 하면 15 µs, 빼먹으면 다음 tick까지(최대 1 tick), 데몬이 낮으면 높은 태스크가 끝날 때까지. 큐가 차면 FromISR는 즉시 실패하므로 반환값을 확인하고 큐 길이를 계산한다.

> Only the daemon task touches the active timer lists, so the kernel never walks them with interrupts disabled. An ISR just posts a 16-byte command to the timer queue. The cost is command latency: about 15 µs with portYIELD_FROM_ISR in my QEMU measurement, up to one tick without it, and arbitrarily long if the daemon priority is below a busy task. If the queue is full the FromISR call fails immediately, so I check the return value and size configTIMER_QUEUE_LENGTH for the worst burst.

**Q.** "Do auto-reload timers drift?"

**A.** 안 한다. `prvReloadTimer`가 다음 만료를 "원래 만료 시각 + 주기"로 계산한다. 100주기 실험에서 auto-reload는 이상값과 같았고, 콜백 끝에서 one-shot을 다시 시작하는 방식은 +142 ms가 쌓였다. 단 여러 주기를 놓치면 밀린 횟수만큼 연달아 부른다 — V10.4.4부터의 동작이다.

> No. The next expiry is computed from the previous expiry time, not from when the callback ran, so it behaves like vTaskDelayUntil. Over 100 periods it stayed on the grid, while re-arming a one-shot from the callback drifted by 142 ms. The catch is that if the daemon misses several periods, prvReloadTimer calls the callback once per missed period, back to back — I saw three calls within 5 µs after a 35 ms overrun.

**Q.** "How is Zephyr's k_timer different from a FreeRTOS software timer?"

**A.** Zephyr에는 데몬이 없다. expiry 함수는 시스템 시계 ISR 안에서 돈다. 그래서 스레드 부하와 무관하게 정확하지만(1 ms 주기가 985~1021 µs) 블록하면 assert에 걸린다. 대신 `k_timer_status_sync`로 스레드가 타이머 주기에 맞춰 돌 수 있고, status 카운터로 놓친 주기 수를 안다. 무거운 일은 workqueue로 넘긴다. duration은 +1 tick이 아니라 오히려 최대 1 tick 일찍 울린다.

> Zephyr has no daemon: the expiry function runs in the system clock ISR, so it is unaffected by thread load but cannot block — calling k_sem_take with K_FOREVER there trips an assertion. Threads synchronize with k_timer_status_sync, and the status counter tells you how many periods you missed. Heavy work goes to a workqueue. One subtlety: k_timer_start subtracts a tick from the duration, so a 10 ms one-shot can fire after about 9 ms, unlike k_sleep which always waits at least the requested time.

**Q.** "Would you run an 8 kHz rate loop from a software timer?"

**A.** 아니다. 해상도가 tick이라 tick을 8 kHz로 올려야 하고, tick → 콜백 경로만 주기마다 약 11 µs(제 측정)라 CPU의 9%가 거기 쓰인다. 지터도 데몬보다 높은 태스크에 좌우된다. IMU data-ready 인터럽트나 하드웨어 타이머가 박자를 정하고, ISR이 최고 우선순위 태스크에 notify한다. 소프트웨어 타이머는 그 루프의 타임아웃 감시 같은 ms급 일에 쓴다.

> No. Software timers have tick resolution, so I'd need an 8 kHz tick, and the tick-to-callback path alone cost about 11 µs per period in my measurement — almost 9% CPU before doing any work — with jitter that depends on higher-priority tasks. The loop should be paced by hardware: the IMU data-ready interrupt or a hardware timer, with the ISR notifying the highest-priority task. Software timers are for millisecond-scale housekeeping like link-loss timeouts.

**Q.** "How do you detect RC link loss?"

**A.** one-shot 타이머를 failsafe 시간(예: 100 ms)으로 두고 패킷 수신 ISR에서 `xTimerResetFromISR`로 계속 미룬다. 만료되면 콜백이 비행 태스크에 notify하고 비행 태스크가 failsafe로 전환한다. 측정해 보니 데몬이 높으면 99.8 ms, 낮으면 무거운 태스크 때문에 112.9 ms였다 — 안전 타이머는 데몬 우선순위가 마감을 좌우한다. 그리고 비행 루프에서도 마지막 패킷 시각을 직접 확인해 이중으로 감시한다.

> A one-shot timer set to the failsafe timeout, reset from the packet-received ISR with xTimerResetFromISR. If packets stop, the timer expires and the callback notifies the flight task, which switches to failsafe mode. In my test it triggered 99.8 ms after the last packet with a high-priority daemon, but 112.9 ms with a low-priority daemon because a logging task held the CPU. So the daemon priority is part of the safety requirement, and I also check the last-packet timestamp inside the flight loop as a second line of defense.

**Q.** "Is it OK to kick the watchdog from a timer callback?"

**A.** 무조건 kick하면 안 된다. 그건 데몬이 살아 있다는 것만 증명한다. 실험에서 제어 태스크가 400 ms 동안 멈췄는데 워치독은 계속 먹었다. 감시 대상 태스크들이 루프마다 체크인 비트를 세우고, 다 모였을 때만 kick한다. 또는 최저 우선순위 태스크에서 kick해서 CPU 굶주림까지 잡는다.

> Not unconditionally — that only proves the timer daemon is alive. In my test a control task hung for 400 ms while the timer kept feeding the watchdog. Each monitored task sets a check-in bit every loop, and the kicker feeds the watchdog only when all bits are set, then clears them. Kicking from the lowest-priority task additionally catches CPU starvation.

---

## 13. 직접 해보기

1. **(개념)** 데몬 우선순위가 7이고 app이 prio 2일 때, app이 `xTimerStop(t, 0)`을 부르고 바로 `xTimerIsTimerActive(t)`를 읽으면 몇이 나오나? 데몬이 1이라면?
   정답/힌트: 7이면 0(데몬이 즉시 선점해 처리), 1이면 1(명령이 아직 큐에 있음). 예제 1·2의 active-after-send와 같은 이치.
2. **(코드)** 예제 3의 `bad_cb`를 "블록하지 않는" 형태로 고쳐라: 콜백은 `xTaskNotifyGive()`만 하고, prio 1 워커 태스크가 `ulTaskNotifyTake()` 후 `vTaskDelay(blk_ms)`를 한다. heartbeat 최대 지연이 몇 µs가 되나?
   정답/힌트: 블록 시간과 무관하게 0~수십 µs로 돌아온다. 데몬은 notify 한 줄 뒤 바로 다음 만료를 처리한다.
3. **(측정)** 예제 6에서 `spin_us(35000)`을 `spin_us(95000)`으로 바꾸고 `N`을 16으로 늘려라. 몇 번이 연달아 불리나? 그 사이 `xTimerGetExpiryTime()` 값은 어떤 순서로 보이나?
   정답/힌트: 밀린 만료 수 + 1번이 연달아. while 루프 안의 호출은 그 반복의 만료 tick, 마지막 호출은 다음 만료 tick을 보인다.
4. **(측정)** 예제 4를 `-DconfigUSE_TIME_SLICING=0`으로 다시 빌드해 데몬 prio 3(부하와 같음)일 때의 지연 분포를 보라.
   정답/힌트: time slicing이 없으면 같은 우선순위의 데몬은 load가 블록할 때까지 기다린다. 작성자가 돌려 본 결과는 prio 2와 똑같이 avg 2112 µs, max 6020 µs였다 — §5.2의 "같은 우선순위도 11 µs"는 time slicing 덕분이었다.
5. **(코드, Zephyr)** 예제 z2를 고쳐 expiry 함수는 `k_sem_give()`만 하고, 스레드가 `k_sem_take()` 후 주기를 재게 하라. busy thread(prio 5) 대신 prio −1 스레드가 500 µs씩 바쁘면 히스토그램이 어떻게 바뀌나?
   정답/힌트: 측정 지점이 스레드로 옮겨 가므로 스레드 스케줄링 지연이 더해진다. expiry가 ISR이라는 장점은 "ISR 안에서 쓴 시각"에만 있다.
6. **(설계)** 드론에서 GNSS 타임아웃(1 s), RC 타임아웃(100 ms), 배터리 샘플 트리거(50 ms), LED(500 ms) 네 타이머를 쓴다. 콜백 최악 실행 시간이 각각 20, 15, 30, 2 µs이고 데몬이 최고 우선순위라면, 데몬이 rate loop에 주는 최악의 방해 시간은?
   정답/힌트: 네 만료가 같은 tick에 겹치는 경우 합 67 µs + 데몬 전환 고정 비용(이 노트 기준 약 11 µs). rate loop 태스크의 응답 시간 분석에 블로킹이 아니라 "더 높은 우선순위의 간섭"으로 넣는다.
7. **(조사)** Zephyr `k_work_delayable`(D7)과 `k_timer`의 차이를 "어느 문맥에서 도나 · 블록할 수 있나 · 정밀도"로 표로 정리하라.
   정답/힌트: `k_work_delayable`은 내부적으로 timeout을 쓰고 만료 시 workqueue 스레드에서 핸들러를 실행한다 — FreeRTOS 데몬 방식에 가깝다. 자세한 내용은 D7.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| software timer | 소프트웨어 타이머 | 커널 tick을 시계로 쓰는 알람. 개수 제한이 RAM뿐 |
| timer service task / daemon task | 타이머 서비스 태스크 | FreeRTOS에서 콜백을 부르고 타이머 명령을 처리하는 전담 태스크, 이름 `Tmr Svc` |
| timer command queue | 타이머 명령 큐 | `xTimerQueue`. Start/Stop/Reset 등을 데몬에 전달, 칸당 16 B |
| one-shot | 한 번 | 만료 후 dormant |
| auto-reload | 자동 재장전 | 만료마다 "원래 만료 + 주기"로 다시 걸림 |
| dormant / active | 휴면 / 활성 | 알람 목록에 걸려 있는지 여부 (`ucStatus` 비트 0) |
| timer ID | 타이머 ID | `void *` 하나. 콜백 공유 시 구분, 작은 상태 저장 |
| command time | 명령 기준 시각 | Start/Reset이 보낸 순간의 tick. 만료 = 기준 + 주기 |
| catch-up | 몰아치기 | 밀린 auto-reload 주기만큼 콜백을 연달아 호출 (`prvReloadTimer`) |
| overflow timer list | overflow 리스트 | 만료 tick이 wrap 너머인 타이머들, wrap 때 현재 리스트와 교체 |
| `k_timer` | Zephyr 타이머 | expiry fn은 시스템 시계 ISR에서, stop fn은 stop 호출자 문맥에서 |
| duration / period | 첫 만료 / 주기 | `k_timer_start`의 두 인자. period가 `K_NO_WAIT`·`K_FOREVER`면 one-shot |
| timer status | 만료 카운터 | `k_timer_status_get/sync`가 읽고 0으로 리셋 |
| `sys_clock_announce` | tick 공지 | 시계 드라이버 ISR이 경과 tick을 알리고 timeout 리스트를 처리 |
| failsafe | 안전 모드 | 링크 끊김 등에서 기체를 안전 상태로 보내는 동작 |
| debounce | 바운스 제거 | 엣지가 조용해진 뒤 한 번만 레벨을 읽기 |
| check-in bits | 체크인 비트 | 감시 대상 태스크가 살아 있음을 알리는 플래그, 워치독 kick 조건 |

---

## 15. 요약 & 체크리스트

소프트웨어 타이머는 tick 하나를 시계로 삼은 "알람 목록"이다. FreeRTOS에서는 타이머 서비스 태스크(`Tmr Svc`)가 정렬된 리스트 두 개(현재 / overflow)를 혼자 관리하고, 다른 태스크와 ISR은 16바이트 명령을 큐에 넣어 부탁한다. 명령의 기준 시각은 보낸 순간이라 늦게 처리돼도 만료는 정확하지만, 상태 변화와 FromISR 처리에는 명령 지연(yield 15 µs, 누락 시 최대 1 tick)이 있다. 콜백은 데몬 안에서 차례로 돌므로 블록하면 모든 타이머가 그만큼 늦고(예제 3), 데몬보다 높은 태스크가 돌면 그 실행 시간만큼 늦다(예제 4). 데몬 스택은 모든 콜백의 공용 스택이다(예제 11). auto-reload는 이전 만료 + 주기로 다시 걸려 drift가 없고, 밀리면 밀린 횟수만큼 몰아서 부른다(예제 5·6, V10.4.4 이후). Zephyr `k_timer`의 expiry 함수는 SysTick ISR에서 돌아 스레드 부하와 무관하게 정확하지만 블록할 수 없고(assert), duration은 최대 1 tick 일찍 울리며, 긴 IRQ 잠금은 지연이 아니라 시간 손실을 만든다(예제 z1~z3). 1 kHz를 넘는 제어 루프는 하드웨어 타이머가 박자를 정하고, 소프트웨어 타이머는 failsafe·debounce·LED·타임아웃처럼 짧고 논블로킹이며 ms 정밀도면 되는 일을 맡는다.

- [ ] 소프트웨어 타이머와 하드웨어 타이머의 차이를 "시계 · 정밀도 · 개수 · 콜백 문맥"으로 말할 수 있다
- [ ] `prvTimerTask`의 한 바퀴(다음 만료 확인 → 만료 처리 또는 블록 → 명령 처리)를 설명할 수 있다
- [ ] `xTimerStart`의 `xTicksToWait`가 무엇인지, 명령의 기준 시각이 언제인지 안다 (ChangePeriod는 예외)
- [ ] 콜백에서 블록하면 무슨 일이 생기는지 측정 숫자(40 ms → 40 ms 지연, 9연속)로 말할 수 있다
- [ ] 데몬 우선순위의 장단점과 "데몬은 높게, 콜백은 짧게" 원칙을 설명할 수 있다
- [ ] 데몬 스택 크기를 콜백 기준으로 정하고 high-water mark로 확인할 수 있다
- [ ] `portYIELD_FROM_ISR` 누락의 비용(최대 1 tick)과 명령 큐 넘침을 안다
- [ ] auto-reload가 drift하지 않는 이유와 몰아치기(`prvReloadTimer` while 루프)를 소스로 설명할 수 있다
- [ ] Zephyr `k_timer`의 expiry fn(ISR) / stop fn(호출자) 문맥과 `status_sync`/`status_get` 사용법을 안다
- [ ] `k_timer` duration의 −1 tick과 `k_sleep`의 +1 tick 차이를 설명할 수 있다
- [ ] 8 kHz·20 kHz 루프에 소프트웨어 타이머를 쓰지 않는 이유를 숫자로 말할 수 있다
- [ ] link-loss failsafe, debounce, 워치독 체크인 패턴을 코드로 쓸 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `timers.c`(`Timer_t` 83, `xTimerCreateTimerTask` 237, `xTimerGenericCommandFromTask` 448, `xTimerGenericCommandFromISR` 499, `prvReloadTimer` 697, `prvProcessExpiredTimer` 716, `prvTimerTask` 746, `prvProcessTimerOrBlockTask` 780, `prvSampleTimeNow` 869, `prvInsertTimerInActiveList` 892, `prvProcessReceivedCommands` 936, `prvSwitchTimerLists` 1091), `include/timers.h`(명령 ID 55~68, `xTimerStart` 매크로 508, `xTimerResetFromISR` 1116, `xTimerGenericCommand` 매크로 1380), `tasks.c`(`xTimerCreateTimerTask` 호출 3762), `portable/GCC/ARM_CM3/port.c`(`vPortEnterCritical`의 ISR assert 438), `History.txt`(V10.2.0 active 상태 비트, V10.4.4 auto-reload 즉시 재실행, V11.3.1 명령 ID 하한 검사)
- FreeRTOS 공식 문서 — Software Timers: https://www.freertos.org/RTOS-software-timer.html
- Richard Barry, "Mastering the FreeRTOS Real Time Kernel" — Software Timer Management 장
- Zephyr v4.4.2 소스 (랩 `/var/rtoslab/zephyrproject/zephyr`): `kernel/timer.c`(`z_timer_expiration_handler` 71, `k_timer_init` 154, `z_impl_k_timer_start` 182, `z_impl_k_timer_stop` 244, `z_impl_k_timer_status_get` 295, `z_impl_k_timer_status_sync` 315), `kernel/timeout.c`(`z_add_timeout` 88, +1 tick 111, `sys_clock_announce_locked` 223), `include/zephyr/kernel.h`(`struct k_timer` 1777, `K_TIMER_DEFINE` 1899), `include/zephyr/kernel_structs.h`(`struct _timeout` 299), `drivers/timer/cortex_m_systick.c`(`elapsed()` 전제 조건 291, `sys_clock_isr` 349), `kernel/Kconfig`(`SYS_CLOCK_TICKS_PER_SEC` 826), `kernel/sem.c`(ISR 블록 assert 136), `doc/kernel/services/timing/timers.rst`
- Zephyr 문서 — Timers: https://docs.zephyrproject.org/latest/kernel/services/timing/timers.html
- 관련 노트: RTOS C3, C5, C6, C9, D7, E1, E5, E7 · Embedded Linux F8
