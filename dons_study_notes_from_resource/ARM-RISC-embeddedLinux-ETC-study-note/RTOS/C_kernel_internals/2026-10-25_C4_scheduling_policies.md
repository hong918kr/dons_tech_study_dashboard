# C4. 스케줄링 정책 — "다음에 누가 CPU를 쥐나"를 정하는 규칙: 고정 우선순위 선점 · time slicing · 협력형 · meta-IRQ · preemption-threshold

> **이 노트를 다 읽으면**: "ready 중 최고 우선순위가 항상 돈다"는 규칙과 그 규칙이 실제로 적용되는 순간(선점 지점: tick · ISR 종료 · API 호출)을 FreeRTOS `tasks.c`와 Zephyr `kernel/sched.c` 함수 이름으로 설명할 수 있다 · 같은 4-태스크 일감을 선점+slicing / slicing 끔 / 협력형 / Zephyr 협력형·meta-IRQ로 돌린 실제 context switch trace와 간트 차트를 읽고, 응답 시간·기아(starvation)·switch 수·CPU 점유가 정책마다 어떻게 바뀌는지 숫자로 말할 수 있다 · ThreadX preemption-threshold, Zephyr meta-IRQ, 스케줄러 잠금이 "선점을 일부만 끄는" 서로 다른 도구라는 것을 실행 결과로 구분할 수 있다
> **왜 필요한가**: 면접 단골 질문("FreeRTOS에서 같은 우선순위 태스크 두 개는 어떻게 도나?", "cooperative와 preemptive의 차이는?", "high-priority task가 늦게 깨는 이유를 어떻게 찾나?")이고, 입사 후 "비행 제어 루프가 가끔 1 ms 늦는다"는 버그의 절반은 우선순위 배치와 정책 설정에서 나온다
> **Don 기준 난이도**: SSD 펌웨어 executive(run-to-completion 루프 + ISR)는 협력형 스케줄링 그 자체라 직관은 이미 있다 / 새로 배울 것은 선점이 걸리는 정확한 지점, 같은 우선순위끼리의 순환 규칙(FreeRTOS와 Zephyr가 다르다), Zephyr 음수 우선순위·meta-IRQ, ThreadX preemption-threshold
> **선행 노트**: A1(실시간 정의, RM 배치 결과), A2(실행 모델 M1~M5), A4(태스크 상태), A5(RTOS별 우선순위 숫자 방향). 이어지는 노트: C1(ready list 자료구조), C2(PendSV 해부), C5(tick), C7(크리티컬 섹션 구현), C11(EDF·cyclic), D3(우선순위 역전), N2·N3(RM·RTA 이론). Linux 스케줄링 클래스(CFS/EEVDF, SCHED_FIFO/RR)는 EL F1

---

## 0. 큰 그림 — 스케줄링 정책은 세 가지 질문에 대한 답이다

Don이 만든 SSD 펌웨어의 executive를 떠올려 보자. 메인 루프는 "할 일 목록" 중에서 가장 급한 것(호스트 명령 > GC > wear leveling)을 골라 **끝까지** 실행하고, 끝나면 다시 고른다. 인터럽트는 할 일을 목록에 올려 놓기만 한다. 이 구조에는 이미 스케줄링 정책이 들어 있다.

1. **누가 후보인가** — 지금 실행할 수 있는 일(RTOS 말로 ready 상태의 태스크).
2. **누구를 고르나** — 가장 급한 것(우선순위). 같은 급이면? (순서대로 돌아가며? 먼저 온 것?)
3. **언제 다시 고르나** — SSD executive는 "일 하나가 끝났을 때"만 다시 고른다. 이게 **협력형(cooperative)** 이다. RTOS의 **선점형(preemptive)** 은 "더 급한 일이 생긴 바로 그 순간"에도 다시 고른다.

RTOS의 스케줄링 정책 설정(`configUSE_PREEMPTION`, `configUSE_TIME_SLICING`, Zephyr의 음수 우선순위, `CONFIG_TIMESLICING` …)은 전부 2번과 3번의 답을 바꾸는 손잡이다. 1번(ready 판정)은 커널 자료구조의 몫이라 C1에서 다룬다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="250" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="31" font-size="13">(1) tick 인터럽트 (SysTick)</text>
<text x="20" y="51" font-size="12">time slice 만료 · delay 만료로 깨움</text>
<rect x="10" y="72" width="250" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="93" font-size="13">(2) 주변장치 ISR 종료</text>
<text x="20" y="113" font-size="12">FromISR + portYIELD_FROM_ISR</text>
<rect x="10" y="134" width="250" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="155" font-size="13">(3) 태스크의 API 호출</text>
<text x="20" y="175" font-size="12">xQueueSend · xTaskCreate 등</text>
<rect x="10" y="196" width="250" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="217" font-size="13">(4) 자발적 양보</text>
<text x="20" y="237" font-size="12">block(대기 진입) · taskYIELD()</text>
<rect x="300" y="100" width="160" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="380" y="124" font-size="13" text-anchor="middle">PendSV 요청</text>
<text x="380" y="143" font-size="12" text-anchor="middle">ICSR.PENDSVSET</text>
<text x="380" y="161" font-size="12" text-anchor="middle">가장 낮은 우선순위 예외</text>
<rect x="490" y="90" width="180" height="90" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="580" y="113" font-size="13" text-anchor="middle">vTaskSwitchContext</text>
<text x="580" y="133" font-size="12" text-anchor="middle">ready 중 최고 우선순위</text>
<text x="580" y="151" font-size="12" text-anchor="middle">같은 우선순위면 다음 차례</text>
<text x="580" y="169" font-size="12" text-anchor="middle">→ 그 태스크 문맥 복원</text>
<line x1="260" y1="36" x2="300" y2="112" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="260" y1="98" x2="300" y2="126" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="260" y1="160" x2="300" y2="146" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="260" y1="222" x2="300" y2="160" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="460" y1="135" x2="488" y2="135" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="10" y="278" font-size="12">파랑 (1)~(3): 커널이 "더 높은 우선순위가 ready가 됐다"고 판단해 스스로 거는 선점</text>
<text x="10" y="298" font-size="12">초록 (4): 실행 중인 태스크가 스스로 CPU를 내놓는 경우 — 협력형에서는 이것만 남는다</text>
<text x="10" y="318" font-size="12">(예외: FreeRTOS 협력형에서도 ISR이 portYIELD_FROM_ISR을 부르면 바뀐다, §5.3)</text>
</svg>
```
그림 1 — 스케줄러가 "다시 고르는" 순간 네 가지. Cortex-M FreeRTOS에서는 모두 PendSV 요청으로 모이고, 실제 선택은 PendSV 핸들러가 부르는 `vTaskSwitchContext()`에서 일어난다(C2에서 어셈블리 해부).

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| 1 | 고정 우선순위 선점 규칙, 선점 지점 | 예제 1 (API 호출이 선점 지점인가) |
| 2 | 공통 실험대: 4-태스크 일감 + switch trace | 예제 2 (trace 훅·일감 코드) |
| 3 | (a) 선점 + time slicing, "slice는 공평하지 않다" | 예제 3 |
| 4 | (b) 선점, slicing 끔 | 예제 4 |
| 5 | (c) 협력형, FreeRTOS 협력형의 ISR 예외 | 예제 5 |
| 6 | (d) Zephyr: 선점·slicing, 협력형(음수), meta-IRQ | 예제 6, 7 |
| 7 | ThreadX preemption-threshold | 예제 8 |
| 8 | 우선순위 배정 입문과 흔한 실수(기아) | 예제 9 |
| 9 | 스케줄러 잠금 vs 크리티컬 섹션 | 예제 10 |
| 10 | 측정 모음 차트 | 예제 11 (분석 스크립트) |
| 11 | 커널 안: `tasks.c` · `sched.c`에서 선점 결정이 나는 곳 | 예제 12 (소스 확인) |

### 실습 환경 한 줄

Lima VM `elinux` 안의 **QEMU `mps2-an385`(Cortex-M3 @ 25 MHz)** 에서 **FreeRTOS V11.3.2(ARM_CM3 포트, 1 kHz tick)** 와 **Zephyr v4.4.2(같은 보드, 1 kHz tick으로 맞춤)**, 그리고 A5에서 같은 보드로 포팅한 **Eclipse ThreadX 6.5.2**를 돌린다. 모든 타이밍은 **`-icount shift=5`(명령 1개 = 32 ns 가상 시간, 매번 같은 숫자)** 기준이다. **QEMU icount 기준이므로 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스·예외 스태킹 사이클을 모델링하지 않는다). 시각은 MPS2 CMSDK TIMER1을 25 MHz 자유 카운터로 읽는다(1 cycle = 40 ns). 소스는 Mac `~/.rtos_scratch/C4/`, 빌드 산출물은 VM `/var/tmp/rtos/C4/`.

---

## 1. 고정 우선순위 선점 — 규칙은 딱 하나

### 1.1 규칙

> **불변식(invariant)**: 커널이 스케줄링 결정을 할 수 있는 모든 순간에, CPU를 쥔 태스크는 **ready 상태인 태스크 중 우선순위가 가장 높은 것**이다.

- **고정 우선순위(fixed priority)**: 우선순위는 애플리케이션이 정하고, 커널은 스스로 바꾸지 않는다. 예외는 뮤텍스 우선순위 상속(D3)과 `vTaskPrioritySet()` 같은 명시적 호출뿐이다. Linux CFS처럼 "많이 쓴 태스크의 몫을 줄이는" 동적 조정이 없다 (EL F1과 비교).
- **선점(preemptive)**: 더 높은 우선순위 태스크가 ready가 되면, 지금 도는 태스크가 일을 다 끝내지 않았어도 **바로** 빼앗는다.
- 이 규칙 하나 덕분에 "가장 높은 우선순위 태스크의 응답 시간 = 자기 일 + 커널 오버헤드 + (인터럽트·크리티컬 섹션)"으로 아래 태스크와 **무관하게** 위에서 잘린다. 이것이 RTOS가 super-loop보다 나은 핵심 이유다(A2 §5에서 측정).

무엇이 "높은" 숫자인지는 RTOS마다 반대다(A5 §1): FreeRTOS는 **큰 숫자가 높고**(0 = idle), Zephyr와 ThreadX는 **작은 숫자가 높다**. 이 노트에서도 FreeRTOS 예제는 H=4 > M=3 > A=B=1, Zephyr 예제는 H=1 > M=2 > A=B=5로 쓴다.

### 1.2 "바로"는 언제인가 — 선점 지점

선점 규칙은 커널 코드가 실행될 때만 검사된다. 태스크가 그냥 `for` 루프를 돌고 있으면 아무도 검사하지 않는다. 검사가 일어나는 지점은 그림 1의 네 가지다.

| 선점 지점 | 언제 | FreeRTOS에서 누가 PendSV를 거나 | 협력형에서도? |
|---|---|---|---|
| tick 인터럽트 | 1 ms마다 | `xPortSysTickHandler()`가 `xTaskIncrementTick()`의 반환값이 pdTRUE면 | 아니오 (`#if configUSE_PREEMPTION == 1`로 막힘) |
| 주변장치 ISR 종료 | ISR이 FromISR API로 높은 태스크를 깨웠을 때 | 애플리케이션 ISR의 `portYIELD_FROM_ISR(woken)` | **예** — 커널이 막지 않는다 (§5.3) |
| 태스크의 API 호출 | `xQueueSend`, `xSemaphoreGive`, `xTaskCreate`, `vTaskPrioritySet`, `vTaskResume` … 가 더 높은 태스크를 ready로 만들 때 | API 안의 `queueYIELD_IF_USING_PREEMPTION()` / `taskYIELD_ANY_CORE_IF_USING_PREEMPTION()` | 아니오 (매크로가 빈 정의) |
| 자발적 양보 | 태스크가 block하거나 `taskYIELD()` | API 안의 `portYIELD_WITHIN_API()` | 예 (이것이 협력형의 유일한 전환) |

Don에게 익숙한 말로 바꾸면: **PendSV = "가장 낮은 우선순위로 미뤄 둔 소프트웨어 인터럽트"** 다. 위 네 경로는 모두 "PendSV pending 비트를 세우는 것"으로 끝나고, 실제 문맥 교체는 다른 모든 ISR이 끝난 뒤 PendSV 핸들러에서 한 번에 일어난다(C2). 그래서 ISR 안에서 태스크를 깨워도 ISR이 중간에 끊기지 않는다.

### 1.3 예제 1 — API 호출 한가운데서 선점이 일어나는가

무엇을 확인하는 코드인지: 낮은 우선순위 L(prio 1)이 (1) 큐에 보내기 (2) 더 높은 태스크 만들기 (3) 다른 태스크 우선순위 올리기 (4) `taskYIELD()`를 차례로 할 때, 깨어난 높은 태스크가 **API가 반환되기 전에** 도는지를 출력 순서로 본다. `configUSE_PREEMPTION=1`과 `0` 두 번 빌드한다. (trace 훅 `c4_tn[]`은 §2.2의 `FreeRTOSConfig.h`가 만든다.)

```c
/* C4 ex: API 호출이 선점 지점이 되는가 — L(prio 1)이 하는 세 가지 일이 H(prio 3)를 깨운다 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
volatile uint32_t c4_tt[C4_TRACE_N]; volatile char c4_tn[C4_TRACE_N]; volatile uint32_t c4_ti;
static QueueHandle_t q;
static TaskHandle_t hW;
static void say(const char *s) { board_printf_raw("  %s\n", s); }

static void taskH(void *p) {                     /* 큐에서 기다리는 높은 우선순위 */
    (void)p; int v;
    for (;;) { xQueueReceive(q, &v, portMAX_DELAY); say("H: got item from queue"); }
}
static void taskNew(void *p) { (void)p; say("N: new prio-2 task runs"); vTaskDelete(NULL); }
static void taskW(void *p) {                     /* prio 0에서 시작, L이 우선순위를 올려 준다 */
    (void)p; say("W: raised to prio 2, runs"); vTaskSuspend(NULL);
}
static void taskL(void *p) {
    (void)p; int v = 1;
    say("L: xQueueSend ->");      xQueueSend(q, &v, 0);                    say("L: <- xQueueSend returned");
    say("L: xTaskCreate(prio 2) ->"); xTaskCreate(taskNew, "N", 256, NULL, 2, NULL); say("L: <- xTaskCreate returned");
    say("L: vTaskPrioritySet(W, 2) ->"); vTaskPrioritySet(hW, 2);          say("L: <- vTaskPrioritySet returned");
    say("L: taskYIELD() ->");     taskYIELD();                             say("L: <- taskYIELD returned");
    board_printf_raw("switch-in order: ");
    for (uint32_t i = 0; i < c4_ti; i++) if (c4_tn[i] != '^') board_printf_raw("%c", c4_tn[i]);
    board_exit(0);
}
int main(void) {
    q = xQueueCreate(4, sizeof(int));
    board_printf_raw("configUSE_PREEMPTION=%d\n", configUSE_PREEMPTION);
    xTaskCreate(taskH, "H", 256, NULL, 3, NULL);
    xTaskCreate(taskL, "L", 256, NULL, 1, NULL);
    xTaskCreate(taskW, "W", 256, NULL, 0, &hW);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/C4/runf.sh p1 "" points.c; cat /var/tmp/rtos/C4/p1/out.txt
  /Users/donh/.rtos_scratch/C4/runf.sh p0 "-DconfigUSE_PREEMPTION=0" points.c; cat /var/tmp/rtos/C4/p0/out.txt'
```

`runf.sh`는 템플릿 Makefile로 빌드(`-B`, 경고는 grep으로 표시)하고 `ICOUNT=1`로 실행해 출력을 `/var/tmp/rtos/C4/<이름>/out.txt`에 저장하는 3줄짜리 스크립트다(§2.4). 경고는 0개였다.

```text
p1 exit=0 lines=13
configUSE_PREEMPTION=1
  L: xQueueSend ->
  H: got item from queue
  L: <- xQueueSend returned
  L: xTaskCreate(prio 2) ->
  N: new prio-2 task runs
  L: <- xTaskCreate returned
  L: vTaskPrioritySet(W, 2) ->
  W: raised to prio 2, runs
  L: <- vTaskPrioritySet returned
  L: taskYIELD() ->
  L: <- taskYIELD returned
switch-in order: THLHLNLWLL[board_exit] code=0
p0 exit=0 lines=13
configUSE_PREEMPTION=0
  L: xQueueSend ->
  L: <- xQueueSend returned
  L: xTaskCreate(prio 2) ->
  L: <- xTaskCreate returned
  L: vTaskPrioritySet(W, 2) ->
  L: <- vTaskPrioritySet returned
  L: taskYIELD() ->
  H: got item from queue
  N: new prio-2 task runs
  W: raised to prio 2, runs
  L: <- taskYIELD returned
switch-in order: THLHNWL[board_exit] code=0
```

출력에서 볼 것:

- 선점형(p1)에서는 세 API 모두 **호출과 반환 사이에** 높은 태스크가 끼어들었다. `xQueueSend()`는 L의 코드 안에서 호출됐지만, 그 안에서 H가 ready가 되는 순간 PendSV가 걸리고, PendSV는 `xQueueSend()`가 크리티컬 섹션을 빠져나오자마자 실행된다. L 입장에서는 함수 하나를 불렀는데 그 사이에 다른 태스크가 한 바퀴 돈 것이다.
- 협력형(p0)에서는 세 API가 전부 그냥 반환됐고, 깨어난 H·N·W는 L이 `taskYIELD()`로 스스로 내놓은 뒤에야 우선순위 순서(H 3 → N 2 → W 2)로 돌았다.
- switch-in 순서의 첫 글자 `T`는 timer service 태스크(우선순위 7, 시작하자마자 block)다. p1 마지막 `LL`은 `taskYIELD()`가 PendSV를 걸었지만 L보다 높거나 같은 ready 태스크가 없어서 L이 다시 선택된 것이다 — `traceTASK_SWITCHED_IN()`은 같은 태스크가 다시 뽑혀도 불린다(§11의 trace 해석에서 중요).

### 1.4 Don의 경험과 연결, 그리고 함정

- SSD 펌웨어에서 "호스트 명령 ISR이 큐에 넣고, 메인 루프가 다음 바퀴에 꺼낸다"는 구조는 p0과 같다. 다음 바퀴까지의 지연 = 지금 돌고 있는 일의 남은 길이. RTOS 선점형은 그 지연을 "PendSV까지의 시간"으로 줄인다.
- **함정 1 — "선점은 즉시"가 아니다**: PendSV는 가장 낮은 예외 우선순위다. 크리티컬 섹션(BASEPRI 마스크) 안이거나 다른 ISR이 돌고 있으면 그것이 끝날 때까지 미뤄진다. §9에서 2 ms 크리티컬 섹션이 응답 시간을 2 ms 늘리는 것을 잰다.
- **함정 2 — API 호출이 "짧은 함수 호출"이라고 믿는 것**: 선점형에서 `xQueueSend()`가 반환될 때까지 걸리는 시간에는 깨어난 높은 태스크의 실행 시간 전체가 들어갈 수 있다. 낮은 태스크 안에서 "send 직전과 직후의 시각 차"로 큐 성능을 재면 엉뚱한 숫자가 나온다.

---

## 2. 공통 실험대 — 같은 일감, 정책만 바꾼다

### 2.1 일감 설계

드론 비행 제어기를 아주 작게 줄인 모양이다. 일의 양은 모든 실험에서 같고, 바꾸는 것은 **커널 설정과 우선순위**뿐이다.

| 태스크 | FreeRTOS 우선순위 | 깨우는 것 | 한 번의 일 | 드론 FC로 치면 |
|---|---|---|---|---|
| H | 4 (최고) | TIMER0 ISR, 4.87 ms마다 → `vTaskNotifyGiveFromISR` | 200 µs | IMU data-ready → 자세 rate loop |
| M | 3 | `xTaskDelayUntil`, 10 ms 주기 (tick에 정렬) | 1500 µs | 항법·텔레메트리 묶음 |
| A | 1 | 없음 — 쉬지 않는 CPU hog | 500 µs 조각 반복 | 로그 압축, 지도 갱신 같은 배경 계산 |
| B | 1 | 없음 — 쉬지 않는 CPU hog | 500 µs 조각 반복 | 두 번째 배경 계산 |

- H의 주기를 5 ms가 아니라 **4.87 ms**로 한 이유: 5 ms면 H가 매번 tick과 같은 위상에서 깨어나 응답 시간이 "한 가지 숫자"만 나온다. 실제 IMU 타이머는 RTOS tick과 동기화돼 있지 않으므로, 어긋난 주기로 위상을 골고루 훑는다.
- **응답 시간(response time)** = 릴리스(깨어날 이유가 생긴 순간)부터 그 job의 일이 끝난 순간까지. H의 릴리스 시각은 ISR 진입 시각이 아니라 **타이머가 0을 지난 순간**으로 잰다: ISR 안에서 `RELOAD - VALUE`(타이머가 0을 지나 다시 장전된 뒤 흐른 cycle)를 빼면 ISR이 늦게 들어와도(§9의 크리티컬 섹션) 진짜 릴리스 시각이 나온다. M의 릴리스는 해당 tick 인터럽트 시각이다.
- 관측 구간: H 릴리스 50번(약 240 ms). 51번째 인터럽트에서 ISR이 기록을 멈추고 전부 출력한다(어떤 정책에서도 ISR은 돌기 때문에 태스크가 굶어도 실험이 끝난다).

### 2.2 예제 2-1 — context switch trace 훅

FreeRTOS는 커널 안 곳곳에 `traceXXX()` 매크로 자리를 비워 두었다(`include/FreeRTOS.h`에서 기본값이 빈 매크로). `FreeRTOSConfig.h`에서 정의하면 커널 소스를 고치지 않고 계측할 수 있다. `traceTASK_SWITCHED_IN()`은 `vTaskSwitchContext()`가 새 태스크를 고른 직후(`tasks.c` 5274행, 단일 코어 판) `pxCurrentTCB`가 이미 새 태스크를 가리키는 상태에서 불린다.

무엇을 확인하는 코드인지: 랩 템플릿 설정을 그대로 쓰되 trace 매크로 두 개만 더하는 앱 쪽 `FreeRTOSConfig.h`. switch-in마다 (시각, 태스크 이름 첫 글자), tick마다 (시각, `'^'`)를 RAM 배열에 넣는다.

```c
/* C4: 템플릿 config + trace 훅 (context switch · tick 시각을 RAM 버퍼에) */
#ifndef C4_CONFIG_H
#define C4_CONFIG_H
#include <stdint.h>
#define C4_CYC()    (0xFFFFFFFFu - *(volatile uint32_t *)0x40001004u) /* TIMER1, 25 MHz */
#define C4_TRACE_N  8192
extern volatile uint32_t c4_tt[C4_TRACE_N];   /* 시각(cycle) */
extern volatile char     c4_tn[C4_TRACE_N];   /* 태스크 이름 첫 글자, '^' = tick */
extern volatile uint32_t c4_ti;
#define C4_REC(ch) do { uint32_t i_ = c4_ti;                              \
        if (i_ < C4_TRACE_N) { c4_tt[i_] = C4_CYC(); c4_tn[i_] = (ch); c4_ti = i_ + 1; } } while (0)
#define traceTASK_SWITCHED_IN()        C4_REC(pxCurrentTCB->pcTaskName[0])
#define traceTASK_INCREMENT_TICK(x)    C4_REC('^')
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#endif
```

- 두 매크로 모두 커널 안(PendSV 핸들러, SysTick 핸들러)에서 BASEPRI가 올라간 상태로 불리므로, 태스크 코드가 이 배열을 동시에 건드리지 않는 한 잠금이 필요 없다.
- 비용: 기록 한 번은 명령 10여 개다(icount에서 1 µs 미만). 모든 시나리오에 똑같이 들어간다.
- 템플릿 설정 파일은 `#ifndef`로 감싸져 있어 `EXTRA_CFLAGS="-DconfigUSE_TIME_SLICING=0"`처럼 정책만 바꿔 빌드할 수 있다.

### 2.3 예제 2-2 — 일감 코드

무엇을 확인하는 코드인지: 모든 FreeRTOS 실험이 공유하는 헤더. 보정된 바쁜 일(`work_us`), TIMER0 설정, 릴리스 시각 계산, 결과 덤프.

```c
/* C4 공통: 보정된 바쁜 일(work_us), TIMER0 = "IMU data-ready" 4.87 ms 인터럽트, 결과 덤프 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define NREL 50                                  /* H 릴리스 50번 ≈ 240 ms 관측 */
volatile uint32_t c4_tt[C4_TRACE_N]; volatile char c4_tn[C4_TRACE_N]; volatile uint32_t c4_ti;
static uint32_t rel[NREL], lagc[NREL], hdone[NREL], mtick[64], mdone[64], nm;
static volatile uint32_t nrel, hogcnt[2];
static uint32_t lp100;                           /* 100 us 동안 도는 루프 수 */
static TaskHandle_t hH;

static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }
static void work_us(uint32_t us) { spin(us * lp100 / 100u); }
static void calibrate(void) {
    uint32_t t0 = C4_CYC(); spin(100000); uint32_t c = C4_CYC() - t0;
    lp100 = (uint32_t)(100000ull * 2500u / c);   /* 2500 cycle = 100 us */
}
static void timer0_start(uint32_t period_us) {
    TIMER_RELOAD(TIMER0_BASE) = period_us * 25u - 1u;
    TIMER_VALUE(TIMER0_BASE)  = period_us * 25u - 1u;
    TIMER_CTRL(TIMER0_BASE)   = 1u | 8u;         /* EN | IRQ EN */
    NVIC_IPR(8) = 0x40;                          /* configMAX_SYSCALL(0x20)보다 낮게 → FromISR API 허용 */
    NVIC_ISER0  = 1u << 8;
}
static void dump(void) __attribute__((noreturn));
static void dump(void) {
    TIMER_CTRL(TIMER0_BASE) = 0;
    uint32_t n = c4_ti; c4_ti = C4_TRACE_N;      /* 기록 정지 */
    board_printf_raw("CFG %s\n", C4_TAG);
    for (uint32_t i = 0; i < n; i++) board_printf_raw("T %u %c\n", c4_tt[i], c4_tn[i]);
    for (uint32_t k = 0; k < NREL; k++) board_printf_raw("H %u %u %u\n", rel[k], lagc[k], hdone[k]);
    for (uint32_t k = 0; k < nm; k++) board_printf_raw("M %u %u\n", mtick[k], mdone[k]);
    board_printf_raw("HOG %u %u\nEND\n", hogcnt[0], hogcnt[1]);
    board_exit(0);
}
void TIMER0_IRQHandler(void) {                   /* 릴리스 시각 = 지금 - (타이머가 0을 지난 뒤 흐른 cycle) */
    TIMER_INTCLR(TIMER0_BASE) = 1;
    uint32_t now = C4_CYC(), lag = TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE);
    if (nrel < NREL) { rel[nrel] = now - lag; lagc[nrel] = lag; }
    if (++nrel > NREL) dump();
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(hH, &woken);
#if ISR_YIELD
    portYIELD_FROM_ISR(woken);
#else
    (void)woken;
#endif
}
```

무엇을 확인하는 코드인지: 정책은 `-D` 플래그로만 바꾸는 본체. `HOG_YIELD`(hog가 조각마다 `taskYIELD()`), `ISR_YIELD`(ISR이 `portYIELD_FROM_ISR`을 부르나), `CHUNK_US`(hog 조각 길이), `PRIO_*`(우선순위 실수 실험)가 손잡이다.

```c
/* C4 ex1~: 같은 4-태스크 일감, 정책은 -D 플래그로만 바꾼다
 *   H(prio 4): TIMER0 ISR이 4.87 ms마다 깨움, 200 us 일   M(prio 3): 10 ms 주기, 1500 us 일
 *   A, B(prio 1): 쉬지 않는 CPU hog, CHUNK_US 단위로 일하고 HOG_YIELD면 taskYIELD() */
#ifndef ISR_YIELD
#define ISR_YIELD 1
#endif
#ifndef HOG_YIELD
#define HOG_YIELD 0
#endif
#ifndef CHUNK_US
#define CHUNK_US 500
#endif
#ifndef PRIO_H
#define PRIO_H 4
#endif
#ifndef PRIO_M
#define PRIO_M 3
#endif
#ifndef PRIO_A
#define PRIO_A 1
#endif
#define STR2(x) #x
#define STR(x) STR2(x)
#define C4_TAG "preempt=" STR(configUSE_PREEMPTION) " slice=" STR(configUSE_TIME_SLICING) \
    " isr_yield=" STR(ISR_YIELD) " hog_yield=" STR(HOG_YIELD) " chunk=" STR(CHUNK_US) \
    " prio H/M/A/B=" STR(PRIO_H) "/" STR(PRIO_M) "/" STR(PRIO_A) "/1"
#include "wl.h"

static void taskH(void *p) {
    (void)p;
    for (uint32_t k = 0;; k++) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);   /* 릴리스 하나씩 소비 */
        work_us(200);
        if (k < NREL) hdone[k] = C4_CYC();
    }
}
static void taskM(void *p) {
    (void)p;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(10));
        work_us(1500);
        if (nm < 64) { mtick[nm] = last; mdone[nm++] = C4_CYC(); }
    }
}
static void hog(void *p) {
    uint32_t id = (uint32_t)p;
    for (;;) {
        work_us(CHUNK_US);
        hogcnt[id]++;
        if (HOG_YIELD) taskYIELD();
    }
}
int main(void) {
    calibrate();
    xTaskCreate(taskH, "H", 256, NULL, PRIO_H, &hH);
    xTaskCreate(taskM, "M", 256, NULL, PRIO_M, NULL);
    xTaskCreate(hog, "A", 256, (void *)0, PRIO_A, NULL);
    xTaskCreate(hog, "B", 256, (void *)1, 1, NULL);
    timer0_start(4870);           /* tick(1 ms)과 맞물리지 않는 주기 */
    vTaskStartScheduler();
    for (;;) { }
}
```

- `ulTaskNotifyTake(pdFALSE, …)`: notification 값을 1씩만 줄인다. H가 한참 늦어서 릴리스가 두 번 쌓여도 job을 하나씩 처리하므로 k번째 완료가 k번째 릴리스와 짝이 맞는다(협력형 실험에서 실제로 쌓인다).
- `work_us()`는 시계를 보며 기다리는 것이 아니라 **루프 횟수**로 일한다. 중간에 선점당해도 "일의 양"이 줄지 않는다(시계 기반 바쁜 대기는 선점 시간까지 일한 것으로 쳐서 실험을 망친다).

### 2.4 빌드·실행·분석 흐름

```sh
# ~/.rtos_scratch/C4/runf.sh  <이름> "<EXTRA_CFLAGS>" [소스=main.c]
N=$1; F=$2; S=${3:-main.c}; O=/var/tmp/rtos/C4/$N
make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="/Users/donh/.rtos_scratch/C4/frt/$S" OUT=$O EXTRA_CFLAGS="$F" >$O.build 2>&1 || { cat $O.build; exit 1; }
grep -i warn $O.build
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 30 > $O/out.txt; echo "$N exit=$? lines=$(wc -l < $O/out.txt)"
```

출력 파일은 이런 모양이다(시나리오 a의 앞부분과 끝부분, 가운데 약 640줄 생략).

```text
CFG preempt=1 slice=1 isr_yield=1 hog_yield=0 chunk=500 prio H/M/A/B=4/3/1/1
T 484543 T
T 485014 H
T 485259 M
T 485503 A
T 509682 ^
T 509795 B
T 534682 ^
T 534795 A
...
H 604820 7 610104
H 726569 7 731806
...
M 10 772366
M 20 1022366
...
HOG 196 203
END
[board_exit] code=0
```

읽는 법: `T 509682 ^`는 tick 인터럽트, 바로 다음 `T 509795 B`는 그 tick 뒤 113 cycle(4.5 µs)에 B가 CPU를 받았다는 뜻이다(time slice 교대). `H 604820 7 610104`는 릴리스 시각, ISR 진입 지연 7 cycle(0.3 µs), 완료 시각 — 응답 시간 = (610104 − 604820) / 25 = 211 µs. `M 10 772366`은 tick 10에 릴리스된 M job이 772366에 끝났다는 뜻이다.

분석은 VM의 Python(`python3 -I analyze.py out.txt…`)이 한다. 핵심은 switch 기록을 "누가 언제부터 언제까지 CPU를 쥐었나" 구간으로 바꾸는 함수다(전체 스크립트는 간트 SVG 생성까지 약 100줄, 여기서는 계산 부분만).

```python
def intervals(d, a, b):
    """[a,b) 구간에서 (start, end, task) 실행 구간 목록 (같은 태스크 재선택은 합침)"""
    out, cur, cs = [], None, None
    for t, n in d['sw']:                      # d['sw'] = tick('^')을 뺀 switch-in 기록
        if t >= b: break
        if n == cur: continue                 # LL처럼 같은 태스크가 다시 뽑힌 것은 switch가 아니다
        if cur is not None and t > a: out.append((max(cs, a), t, cur))
        cur, cs = n, t
    if cur is not None: out.append((max(cs, a), b, cur))
    return [x for x in out if x[1] > x[0]]

def stats(path):
    d = load(path)
    H = [(dn - r) / 25.0 for r, lag, dn in d['H'] if dn]              # us
    M = [(dn - d['ticks'][k - 1]) / 25.0 for k, dn in d['M']]         # tick k 시각부터
    a, b = d['H'][0][0], d['H'][-1][0]                                # 첫~마지막 H 릴리스 (49주기)
    iv = intervals(d, a, b)
    share = {}
    for s, e, n in iv: share[n] = share.get(n, 0) + (e - s)           # CPU 점유
    nsw = len(iv) - 1                                                 # 실제 switch 수
    ...                                                               # hog별 "가장 길게 굶은 시간"
```

- CPU 점유는 "switch-in부터 다음 switch-in까지"를 그 태스크 몫으로 센다. 그 사이에 끼어든 ISR(tick, TIMER0) 시간도 끼어들림을 당한 태스크 몫으로 들어간다. tick ISR이 수 µs라 1% 아래의 오차다.
- "max gap"은 hog가 연속으로 CPU를 못 받은 가장 긴 시간이다. 기아(starvation)를 숫자 하나로 보여 준다.

---

## 3. 정책 (a) 선점 + time slicing — FreeRTOS 기본값

### 3.1 정의

- **time slicing(시분할)**: 같은 우선순위의 ready 태스크가 둘 이상이면 일정 시간(quantum, time slice)마다 돌아가며 CPU를 준다. **round-robin**이라고도 부른다.
- FreeRTOS에서는 `configUSE_PREEMPTION == 1`이고 `configUSE_TIME_SLICING == 1`(`include/FreeRTOS.h` 2771행에서 기본값 1)일 때 켜진다.
- **quantum = 1 tick**. 별도의 slice 타이머가 없다. `xTaskIncrementTick()`이 매 tick마다 "지금 도는 태스크와 같은 우선순위의 ready 리스트에 태스크가 2개 이상인가"만 보고, 그렇다면 switch를 요청한다(`tasks.c` 4967~4998행).

```c
/* tasks.c (V11.3.2) xTaskIncrementTick() 안, 단일 코어 부분 — 발췌 */
#if ( ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 ) )
    if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > 1U )
    {
        xSwitchRequired = pdTRUE;
    }
#endif
```

여기서 중요한 것은 **"slice가 끝났는가"를 묻지 않는다**는 점이다. 태스크가 언제 CPU를 받았든, 다음 tick이 오면 그 태스크의 slice는 끝난다. 이 단순함이 §3.4의 "불공평"을 만든다.

### 3.2 예제 3 — 실행

무엇을 확인하는 코드인지: §2의 일감을 기본 설정(선점 + slicing) 그대로 돌리고, 응답 시간·CPU 점유·switch 수를 낸다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C4; /Users/donh/.rtos_scratch/C4/runf.sh a ""
  python3 -I /Users/donh/.rtos_scratch/C4/analyze.py a/out.txt'
```

```text
a exit=0 lines=668
[a] preempt=1 slice=1 isr_yield=1 hog_yield=0 chunk=500 prio H/M/A/B=4/3/1/1
  H resp us min/avg/max    209/   210/   213  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max   1507/  1566/  1726  (n=24)
  CPU% A=39.3  B=41.2  H=4.3  M=15.2 | switches=327 | hog chunks A=196 B=203 | max gap A=2.7ms B=2.7ms
```

출력에서 볼 것: H는 일 200 µs에 커널 오버헤드 약 10 µs를 더한 209~213 µs로 **흔들림이 4 µs** 뿐이다. 아래에서 hog 둘이 CPU를 100% 쓰려고 해도 H와 무관하다 — 고정 우선순위 선점의 약속 그대로다. M은 H에게 가끔 끼어들림을 당해 1507~1726 µs. A와 B는 남은 80%를 거의 반씩 나눴고, 238.6 ms 동안 switch가 327번(초당 약 1370번) 일어났다.

```svg
<svg viewBox="0 0 680 168" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="29" font-size="13">H</text>
<line x1="70" y1="38" x2="660" y2="38" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="55" font-size="13">M</text>
<line x1="70" y1="64" x2="660" y2="64" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="81" font-size="13">A</text>
<line x1="70" y1="90" x2="660" y2="90" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="107" font-size="13">B</text>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor" stroke-opacity="0.2"/>
<rect x="70.0" y="68" width="24.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="94.8" y="94" width="33.4" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="128.2" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="138.5" y="68" width="5.5" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="144.1" y="42" width="74.2" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="218.3" y="94" width="24.0" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="242.3" y="68" width="49.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="291.5" y="94" width="49.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="340.6" y="68" width="27.0" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="367.6" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="378.0" y="94" width="11.8" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="389.8" y="68" width="49.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="439.0" y="94" width="49.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="488.1" y="68" width="49.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="537.3" y="94" width="49.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="586.5" y="68" width="20.6" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="607.0" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="617.4" y="94" width="18.3" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="635.7" y="42" width="24.3" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="94.6" y1="12" x2="94.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="143.8" y1="12" x2="143.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="192.9" y1="12" x2="192.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="242.1" y1="12" x2="242.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="291.2" y1="12" x2="291.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="340.4" y1="12" x2="340.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="389.6" y1="12" x2="389.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="438.8" y1="12" x2="438.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="487.9" y1="12" x2="487.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="537.1" y1="12" x2="537.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="586.2" y1="12" x2="586.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="635.4" y1="12" x2="635.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<path d="M127.8 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M367.2 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M606.7 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M143.8 42 l-4 -7 h8 z" fill="#e08a3c"/>
<path d="M635.4 42 l-4 -7 h8 z" fill="#e08a3c"/>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor"/>
<line x1="94.6" y1="116" x2="94.6" y2="120" stroke="currentColor"/>
<text x="94.6" y="133" font-size="12" text-anchor="middle">9</text>
<line x1="143.8" y1="116" x2="143.8" y2="120" stroke="currentColor"/>
<text x="143.8" y="133" font-size="12" text-anchor="middle">10</text>
<line x1="192.9" y1="116" x2="192.9" y2="120" stroke="currentColor"/>
<text x="192.9" y="133" font-size="12" text-anchor="middle">11</text>
<line x1="242.1" y1="116" x2="242.1" y2="120" stroke="currentColor"/>
<text x="242.1" y="133" font-size="12" text-anchor="middle">12</text>
<line x1="291.2" y1="116" x2="291.2" y2="120" stroke="currentColor"/>
<text x="291.2" y="133" font-size="12" text-anchor="middle">13</text>
<line x1="340.4" y1="116" x2="340.4" y2="120" stroke="currentColor"/>
<text x="340.4" y="133" font-size="12" text-anchor="middle">14</text>
<line x1="389.6" y1="116" x2="389.6" y2="120" stroke="currentColor"/>
<text x="389.6" y="133" font-size="12" text-anchor="middle">15</text>
<line x1="438.8" y1="116" x2="438.8" y2="120" stroke="currentColor"/>
<text x="438.8" y="133" font-size="12" text-anchor="middle">16</text>
<line x1="487.9" y1="116" x2="487.9" y2="120" stroke="currentColor"/>
<text x="487.9" y="133" font-size="12" text-anchor="middle">17</text>
<line x1="537.1" y1="116" x2="537.1" y2="120" stroke="currentColor"/>
<text x="537.1" y="133" font-size="12" text-anchor="middle">18</text>
<line x1="586.2" y1="116" x2="586.2" y2="120" stroke="currentColor"/>
<text x="586.2" y="133" font-size="12" text-anchor="middle">19</text>
<line x1="635.4" y1="116" x2="635.4" y2="120" stroke="currentColor"/>
<text x="635.4" y="133" font-size="12" text-anchor="middle">20</text>
<text x="660" y="152" font-size="12" text-anchor="end">ms (tick 0 기준)</text>
<path d="M74 152 l-4 -7 h8 z" fill="#d0564a"/><text x="82" y="152" font-size="12">H 릴리스(ISR)</text>
<path d="M194 152 l-4 -7 h8 z" fill="#e08a3c"/><text x="202" y="152" font-size="12">M 릴리스(tick)  점선 = tick</text>
</svg>
```
그림 2 — 시나리오 (a) 선점 + time slicing의 실제 trace(8.5~20.5 ms 구간). 빨간 삼각형은 H 릴리스(TIMER0 인터럽트), 주황 삼각형은 M 릴리스(tick 10, 20), 점선은 tick. A와 B가 tick마다 교대하고, H·M이 오면 즉시 빼앗긴다.

### 3.3 그림 읽기

- 9.0 ms tick에서 B가 slice를 받았다. 9.7 ms에 H가 B를 선점하고, H가 끝난 9.9 ms에 CPU를 받은 것은 **B가 아니라 A**다. A는 0.1 ms만 돌고 10 ms tick에서 M에게 밀린다.
- 11.5 ms에 M이 끝나자 B가 받는다. 그런데 12 ms tick에 slice가 끝나 A로 넘어간다. B의 이번 slice는 0.5 ms짜리였다.
- 14.55 ms에 H가 A를 선점, H가 끝나자 B가 0.25 ms만 받고 15 ms tick에서 A로.

### 3.4 "slice는 선점 뒤에 공평하지 않다" — 소스와 숫자로 확인

두 가지가 겹친다.

1. **선점당한 태스크는 남은 slice를 돌려받지 못한다.** FreeRTOS는 같은 우선순위 ready 리스트를 `pxIndex`라는 "다음 차례" 포인터로 돈다. `vTaskSwitchContext()` → `taskSELECT_HIGHEST_PRIORITY_TASK()` → `listGET_OWNER_OF_NEXT_ENTRY()`는 **호출될 때마다** `pxIndex`를 한 칸 전진시킨다(`include/list.h` 286~297행). H가 block해서 스케줄러가 다시 고를 때 이 전진이 일어나므로, 선점당했던 A가 아니라 그 다음 차례인 B가 뽑힌다.
2. **slice의 끝은 tick 경계로 고정이다.** tick 사이 어중간한 시점에 CPU를 받은 태스크도 다음 tick에서 끝난다.

이게 실제로 얼마나 자주 일어나는지 trace에서 셌다(분석 스크립트 `fair.py`, 예제 11의 일부).

```python
# hog 공정성 지표: 선점당한 hog가 다시 CPU를 받았나? hog 실행 조각 길이 분포
for p in sys.argv[1:]:
    d = load(p); a, b = d['H'][0][0], d['H'][-1][0]
    iv = intervals(d, a, b)
    same = diff = 0
    for i in range(1, len(iv) - 1):
        if iv[i][2] in 'HM' and iv[i - 1][2] in 'AB':          # hog가 H/M에게 선점당했고
            j = i
            while j < len(iv) and iv[j][2] in 'HM': j += 1
            if j < len(iv) and iv[j][2] in 'AB':                # H/M이 끝난 뒤 받은 hog가
                same += iv[j][2] == iv[i - 1][2]; diff += iv[j][2] != iv[i - 1][2]
    seg = [(e - s) / 25.0 for s, e, n in iv if n in 'AB']
    short = sum(1 for x in seg if x < 900)
    print("%-4s preempted hog resumed: same=%d other=%d | hog segments=%d, <0.9ms=%d, avg=%.0f us, max=%.0f us"
          % (p.split('/')[-2], same, diff, len(seg), short, sum(seg) / len(seg), max(seg)))
```

```text
a    preempted hog resumed: same=0 other=66 | hog segments=249, <0.9ms=101, avg=771 us, max=1002 us
b    preempted hog resumed: same=0 other=66 | hog segments=67, <0.9ms=11, avg=2866 us, max=4659 us
z1   preempted hog resumed: same=64 other=1 | hog segments=245, <0.9ms=97, avg=774 us, max=1029 us
z1n  preempted hog resumed: same=64 other=0 | hog segments=65, <0.9ms=11, avg=2954 us, max=4662 us
```

출력에서 볼 것: FreeRTOS(a)는 선점당한 hog가 **66번 중 0번** 자기 차례를 돌려받았다. 언제나 "다른" hog가 받았다. hog 실행 조각 249개 중 101개(41%)가 0.9 ms보다 짧은 "잘린 slice"였다. 아래 두 줄(z1, z1n)은 §6의 Zephyr 결과로, 같은 상황에서 **64번 중 64번 선점당한 스레드가 다시 받는다**. 같은 "round-robin"이라는 이름 아래 동작이 다르다.

그래도 장기 점유는 39.3% 대 41.2%로 거의 같다. 잘린 slice가 A와 B에 번갈아 걸리기 때문이다. 즉 이 불공평은 **장기 처리량 문제가 아니라 단기 지터 문제**다. 같은 우선순위에 "주기적으로 일해야 하는" 태스크 두 개를 두면, 한쪽이 언제 CPU를 받을지 tick 단위로도 예측할 수 없다는 뜻이다. 답은 간단하다 — **시간 제약이 있는 태스크는 같은 우선순위에 두지 않는다.** slicing은 배경 계산끼리 나눠 쓰는 용도다.

### 3.5 `configIDLE_SHOULD_YIELD` — idle 우선순위의 같은 문제

애플리케이션 태스크를 우선순위 0(idle과 같은 `tskIDLE_PRIORITY`)에 두면 idle 태스크와 time slice를 나눠 쓴다. `configIDLE_SHOULD_YIELD == 1`(기본값, `FreeRTOS.h` 376행)이면 idle 태스크가 루프마다 "우선순위 0 ready 리스트에 나 말고 누가 있나"를 보고 있으면 즉시 `taskYIELD()`한다(`tasks.c` 5946~5967행, `prvIdleTask`). 그래서 우선순위 0 태스크가 idle에게 slice의 대부분을 뺏기지 않는다. 다만 idle이 yield한 직후 받은 태스크도 **그 tick의 남은 부분만** 쓰고 다음 tick에 다시 slice 판정을 받는다. 그래서 "우선순위 0에서 돌 일은 태스크 대신 idle hook으로"가 흔한 권장이다(FreeRTOS 설정 문서의 `configIDLE_SHOULD_YIELD` 항목이 이 현상을 설명한다). `configUSE_PREEMPTION == 0`일 때는 idle이 무조건 매 바퀴 `taskYIELD()`한다(5936~5944행) — 협력형에서 idle이 CPU를 쥐고 놓지 않으면 아무 일도 안 일어나기 때문이다.

이 절은 소스 확인만 했고 별도 측정은 하지 않았다(§15 연습 3).

---

## 4. 정책 (b) 선점, time slicing 끔

### 4.1 무엇이 바뀌나

`configUSE_TIME_SLICING = 0`이면 §3.1의 tick 검사 블록이 컴파일에서 빠진다. tick은 더 이상 "같은 우선순위끼리 바꿔라"라고 하지 않는다. 직관적으로는 "A가 한 번 CPU를 받으면 더 높은 태스크가 올 때까지 계속 A"이고, 많은 문서가 "B는 굶을 수 있다"고 쓴다. 실제로 재 보자.

### 4.2 예제 4 — 실행

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C4; /Users/donh/.rtos_scratch/C4/runf.sh b "-DconfigUSE_TIME_SLICING=0"
  python3 -I /Users/donh/.rtos_scratch/C4/analyze.py b/out.txt'
```

```text
b exit=0 lines=477
[b] preempt=1 slice=0 isr_yield=1 hog_yield=0 chunk=500 prio H/M/A/B=4/3/1/1
  H resp us min/avg/max    209/   210/   213  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max   1507/  1565/  1726  (n=24)
  CPU% A=40.2  B=40.2  H=4.3  M=15.2 | switches=145 | hog chunks A=200 B=200 | max gap A=6.5ms B=6.6ms
```

```svg
<svg viewBox="0 0 680 168" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="29" font-size="13">H</text>
<line x1="70" y1="38" x2="660" y2="38" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="55" font-size="13">M</text>
<line x1="70" y1="64" x2="660" y2="64" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="81" font-size="13">A</text>
<line x1="70" y1="90" x2="660" y2="90" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="107" font-size="13">B</text>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor" stroke-opacity="0.2"/>
<rect x="70.0" y="94" width="58.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="128.2" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="138.5" y="68" width="5.5" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="144.1" y="42" width="74.2" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="218.3" y="94" width="149.3" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="367.6" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="378.0" y="68" width="229.1" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="607.0" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="617.4" y="94" width="18.3" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="635.7" y="42" width="24.3" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="94.6" y1="12" x2="94.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="143.8" y1="12" x2="143.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="192.9" y1="12" x2="192.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="242.1" y1="12" x2="242.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="291.2" y1="12" x2="291.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="340.4" y1="12" x2="340.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="389.6" y1="12" x2="389.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="438.8" y1="12" x2="438.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="487.9" y1="12" x2="487.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="537.1" y1="12" x2="537.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="586.2" y1="12" x2="586.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="635.4" y1="12" x2="635.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<path d="M127.8 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M367.2 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M606.7 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M143.8 42 l-4 -7 h8 z" fill="#e08a3c"/>
<path d="M635.4 42 l-4 -7 h8 z" fill="#e08a3c"/>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor"/>
<line x1="94.6" y1="116" x2="94.6" y2="120" stroke="currentColor"/>
<text x="94.6" y="133" font-size="12" text-anchor="middle">9</text>
<line x1="143.8" y1="116" x2="143.8" y2="120" stroke="currentColor"/>
<text x="143.8" y="133" font-size="12" text-anchor="middle">10</text>
<line x1="192.9" y1="116" x2="192.9" y2="120" stroke="currentColor"/>
<text x="192.9" y="133" font-size="12" text-anchor="middle">11</text>
<line x1="242.1" y1="116" x2="242.1" y2="120" stroke="currentColor"/>
<text x="242.1" y="133" font-size="12" text-anchor="middle">12</text>
<line x1="291.2" y1="116" x2="291.2" y2="120" stroke="currentColor"/>
<text x="291.2" y="133" font-size="12" text-anchor="middle">13</text>
<line x1="340.4" y1="116" x2="340.4" y2="120" stroke="currentColor"/>
<text x="340.4" y="133" font-size="12" text-anchor="middle">14</text>
<line x1="389.6" y1="116" x2="389.6" y2="120" stroke="currentColor"/>
<text x="389.6" y="133" font-size="12" text-anchor="middle">15</text>
<line x1="438.8" y1="116" x2="438.8" y2="120" stroke="currentColor"/>
<text x="438.8" y="133" font-size="12" text-anchor="middle">16</text>
<line x1="487.9" y1="116" x2="487.9" y2="120" stroke="currentColor"/>
<text x="487.9" y="133" font-size="12" text-anchor="middle">17</text>
<line x1="537.1" y1="116" x2="537.1" y2="120" stroke="currentColor"/>
<text x="537.1" y="133" font-size="12" text-anchor="middle">18</text>
<line x1="586.2" y1="116" x2="586.2" y2="120" stroke="currentColor"/>
<text x="586.2" y="133" font-size="12" text-anchor="middle">19</text>
<line x1="635.4" y1="116" x2="635.4" y2="120" stroke="currentColor"/>
<text x="635.4" y="133" font-size="12" text-anchor="middle">20</text>
<text x="660" y="152" font-size="12" text-anchor="end">ms (tick 0 기준)</text>
<path d="M74 152 l-4 -7 h8 z" fill="#d0564a"/><text x="82" y="152" font-size="12">H 릴리스(ISR)</text>
<path d="M194 152 l-4 -7 h8 z" fill="#e08a3c"/><text x="202" y="152" font-size="12">M 릴리스(tick)  점선 = tick</text>
</svg>
```
그림 3 — 시나리오 (b) slicing 끔. tick(점선)에서는 아무 일도 안 일어난다. 그런데 H나 M이 끝날 때마다 hog가 바뀐다(9.9 ms에 A, 11.5 ms에 B, 14.75 ms에 A).

출력에서 볼 것:

- H·M의 응답 시간은 (a)와 **완전히 같다**. 같은 우선순위끼리의 정책은 더 높은 태스크에 영향이 없다.
- switch 수가 327 → 145로 절반 이하. tick마다 일어나던 hog 교대가 없어졌다.
- 그런데 B가 굶지 않았다. 40.2% 대 40.2%, 조각 수 200 대 200. 대신 한 번 받으면 평균 2.9 ms, 최대 4.7 ms를 연속으로 돌고(§3.4 표의 b 줄), 가장 길게 굶은 시간이 2.7 ms → 6.6 ms로 늘었다.

### 4.3 왜 굶지 않았나 — 같은 함수가 또 등장한다

§3.4의 `listGET_OWNER_OF_NEXT_ENTRY()`다. slicing을 꺼도 H·M이 block할 때마다 `vTaskSwitchContext()`가 불리고, 그때마다 우선순위 1 리스트의 `pxIndex`가 한 칸 전진한다. 그래서 FreeRTOS에서 "slicing 끔"의 실제 의미는 **"tick에서는 안 바꾼다. 하지만 스케줄러가 다시 고를 일이 생기면 같은 우선순위 안에서 다음 차례로 넘어간다"** 이다. 높은 우선순위 활동이 전혀 없는 시스템이라면 정말로 한 태스크가 영원히 돈다(§5의 c0 실험이 그 극단).

이것은 FreeRTOS의 구현 세부이지 문서화된 보장이 아니다. 이 행동에 기대서 설계하면 안 되고, "같은 우선순위 태스크는 slicing을 꺼도 돌아가며 돈다"를 면접에서 일반론으로 말하면 틀린다 — Zephyr는 §6.3에서 정반대로 동작한다.

### 4.4 언제 slicing을 끄나

- 같은 우선순위 태스크들이 전부 이벤트 구동(대부분 block 상태)이면 slicing은 아무 이득 없이 tick ISR 일만 늘린다. 끄면 switch가 줄고 trace가 읽기 쉬워진다.
- 배경 계산 두 개가 "공평하게" 진행돼야 하면 켠다. 다만 그 공평함은 위에서 본 대로 tick 단위이고 선점 뒤에는 흔들린다.
- 실무에서 흔한 선택: **시간 제약 태스크는 전부 서로 다른 우선순위**, slicing은 기본값(켬)으로 두고 배경 태스크만 같은 우선순위에 둔다.

---

## 5. 정책 (c) 협력형 — `configUSE_PREEMPTION = 0`

### 5.1 정의

**협력형(cooperative, non-preemptive) 스케줄링**: 실행 중인 태스크가 스스로 block하거나 `taskYIELD()`를 부를 때만 다시 고른다. 다시 고를 때는 여전히 "ready 중 최고 우선순위"를 고른다 — 우선순위가 없어지는 게 아니라 **"언제 고르나"만 바뀐다**. A2의 M3(table-driven run-to-completion)과 Don의 SSD executive가 이 모델이다.

FreeRTOS에서 `configUSE_PREEMPTION = 0`이면 소스에서 이런 일이 일어난다.

- `tasks.c` 68~73행: `taskYIELD_ANY_CORE_IF_USING_PREEMPTION()`이 **빈 매크로**가 된다. `xTaskCreate`, `vTaskPrioritySet`, `vTaskResume` 같은 API가 더 높은 태스크를 만들어도 yield하지 않는다(예제 1의 p0).
- `queue.c` 89행: `queueYIELD_IF_USING_PREEMPTION()`도 빈 매크로. `xQueueSend`·`xSemaphoreGive`가 yield하지 않는다.
- `xTaskIncrementTick()`의 "깨어난 태스크가 더 높으면 switch"(4932행)와 slicing(4967행)과 pending yield 검사(5015행)가 모두 `#if ( configUSE_PREEMPTION == 1 )` 안이라 tick도 switch를 요청하지 않는다.

### 5.2 예제 5 — 협력형 네 가지

무엇을 확인하는 코드인지: 같은 `main.c`로 (c0) hog가 양보하지 않음, (c1) hog가 0.5 ms마다 `taskYIELD()` + ISR은 `portYIELD_FROM_ISR` 사용, (c2) c1에서 ISR yield 제거, (c3) c2에서 hog 조각을 3 ms로.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C4; R=/Users/donh/.rtos_scratch/C4/runf.sh
  $R c0 "-DconfigUSE_PREEMPTION=0 -DISR_YIELD=0"
  $R c1 "-DconfigUSE_PREEMPTION=0 -DHOG_YIELD=1"
  $R c2 "-DconfigUSE_PREEMPTION=0 -DHOG_YIELD=1 -DISR_YIELD=0"
  $R c3 "-DconfigUSE_PREEMPTION=0 -DHOG_YIELD=1 -DISR_YIELD=0 -DCHUNK_US=3000"
  python3 -I /Users/donh/.rtos_scratch/C4/analyze.py c0/out.txt c1/out.txt c2/out.txt c3/out.txt'
```

```text
c0 exit=0 lines=306
c1 exit=0 lines=852
c2 exit=0 lines=803
c3 exit=0 lines=472
[c0] preempt=0 slice=1 isr_yield=0 hog_yield=0 chunk=500 prio H/M/A/B=4/3/1/1
  H resp us min/avg/max    (none)  (n=0)   ISR lag max 0.3 us
  M resp us min/avg/max    (none)  (n=0)
  CPU% A=100.0 | switches=0 | hog chunks A=496 B=0 | max gap A=0.0ms B=238.6ms
[c1] preempt=0 slice=1 isr_yield=1 hog_yield=1 chunk=500 prio H/M/A/B=4/3/1/1
  H resp us min/avg/max    209/   210/   213  (n=50)   ISR lag max 3.2 us
  M resp us min/avg/max   1531/  1719/  1970  (n=24)
  CPU% A=40.4  B=40.1  H=4.3  M=15.2 | switches=502 | hog chunks A=199 B=199 | max gap A=2.2ms B=2.2ms
[c2] preempt=0 slice=1 isr_yield=0 hog_yield=1 chunk=500 prio H/M/A/B=4/3/1/1
  H resp us min/avg/max    219/   520/  1716  (n=50)   ISR lag max 1.7 us
  M resp us min/avg/max   1540/  1654/  1873  (n=24)
  CPU% A=40.2  B=40.3  H=4.3  M=15.2 | switches=454 | hog chunks A=200 B=199 | max gap A=2.2ms B=2.2ms
[c3] preempt=0 slice=1 isr_yield=0 hog_yield=1 chunk=3000 prio H/M/A/B=4/3/1/1
  H resp us min/avg/max    281/  1641/  3146  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max   1780/  3939/  5704  (n=24)
  CPU% A=40.3  B=40.1  H=4.3  M=15.2 | switches=137 | hog chunks A=34 B=33 | max gap A=4.9ms B=4.9ms
```

출력에서 볼 것:

- **c0**: 양보하지 않는 태스크 하나가 시스템 전체를 가졌다. 238.6 ms 동안 switch 0번, H와 M은 **한 번도** 돌지 못했다(n=0). TIMER0 ISR은 계속 돌아서(ISR은 스케줄러 밖) 실험은 끝났다. 협력형에서 "무한 루프 하나 = 시스템 정지"다.
- **c2**: hog가 0.5 ms마다 양보하면 H는 평균 520 µs, 최악 **1716 µs**. 최악이 hog 조각(500 µs)이 아니라 M의 일(1500 µs) + 자기 일(200 µs)이다. 협력형에서는 **H보다 낮은 태스크 중 가장 긴 "양보 없는 구간"** 이 H의 대기 시간이 된다. M은 H보다 낮은데도 H를 막았다.
- **c3**: 조각을 3 ms로 늘리자 H 최악 3146 µs, M 최악 5704 µs. switch는 137번으로 가장 적다 — 협력형의 장점(오버헤드·switch 수)과 단점(응답 시간)이 같은 숫자에서 나온다.
- **c1**: 협력형인데 H 응답이 선점형(a)과 **똑같이** 209~213 µs. 다음 절의 주제다.

```svg
<svg viewBox="0 0 680 168" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="29" font-size="13">H</text>
<line x1="70" y1="38" x2="660" y2="38" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="55" font-size="13">M</text>
<line x1="70" y1="64" x2="660" y2="64" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="81" font-size="13">A</text>
<line x1="70" y1="90" x2="660" y2="90" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="107" font-size="13">B</text>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor" stroke-opacity="0.2"/>
<rect x="70.0" y="68" width="37.3" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="107.3" y="94" width="147.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="255.2" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="265.6" y="42" width="74.2" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="339.8" y="68" width="147.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="487.6" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="498.1" y="94" width="147.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="646.0" y="16" width="10.4" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="656.4" y="42" width="3.6" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="94.6" y1="12" x2="94.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="143.8" y1="12" x2="143.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="192.9" y1="12" x2="192.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="242.1" y1="12" x2="242.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="291.2" y1="12" x2="291.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="340.4" y1="12" x2="340.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="389.6" y1="12" x2="389.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="438.8" y1="12" x2="438.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="487.9" y1="12" x2="487.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="537.1" y1="12" x2="537.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="586.2" y1="12" x2="586.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="635.4" y1="12" x2="635.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<path d="M127.8 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M367.2 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M606.7 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M143.8 42 l-4 -7 h8 z" fill="#e08a3c"/>
<path d="M635.4 42 l-4 -7 h8 z" fill="#e08a3c"/>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor"/>
<line x1="94.6" y1="116" x2="94.6" y2="120" stroke="currentColor"/>
<text x="94.6" y="133" font-size="12" text-anchor="middle">9</text>
<line x1="143.8" y1="116" x2="143.8" y2="120" stroke="currentColor"/>
<text x="143.8" y="133" font-size="12" text-anchor="middle">10</text>
<line x1="192.9" y1="116" x2="192.9" y2="120" stroke="currentColor"/>
<text x="192.9" y="133" font-size="12" text-anchor="middle">11</text>
<line x1="242.1" y1="116" x2="242.1" y2="120" stroke="currentColor"/>
<text x="242.1" y="133" font-size="12" text-anchor="middle">12</text>
<line x1="291.2" y1="116" x2="291.2" y2="120" stroke="currentColor"/>
<text x="291.2" y="133" font-size="12" text-anchor="middle">13</text>
<line x1="340.4" y1="116" x2="340.4" y2="120" stroke="currentColor"/>
<text x="340.4" y="133" font-size="12" text-anchor="middle">14</text>
<line x1="389.6" y1="116" x2="389.6" y2="120" stroke="currentColor"/>
<text x="389.6" y="133" font-size="12" text-anchor="middle">15</text>
<line x1="438.8" y1="116" x2="438.8" y2="120" stroke="currentColor"/>
<text x="438.8" y="133" font-size="12" text-anchor="middle">16</text>
<line x1="487.9" y1="116" x2="487.9" y2="120" stroke="currentColor"/>
<text x="487.9" y="133" font-size="12" text-anchor="middle">17</text>
<line x1="537.1" y1="116" x2="537.1" y2="120" stroke="currentColor"/>
<text x="537.1" y="133" font-size="12" text-anchor="middle">18</text>
<line x1="586.2" y1="116" x2="586.2" y2="120" stroke="currentColor"/>
<text x="586.2" y="133" font-size="12" text-anchor="middle">19</text>
<line x1="635.4" y1="116" x2="635.4" y2="120" stroke="currentColor"/>
<text x="635.4" y="133" font-size="12" text-anchor="middle">20</text>
<text x="660" y="152" font-size="12" text-anchor="end">ms (tick 0 기준)</text>
<path d="M74 152 l-4 -7 h8 z" fill="#d0564a"/><text x="82" y="152" font-size="12">H 릴리스(ISR)</text>
<path d="M194 152 l-4 -7 h8 z" fill="#e08a3c"/><text x="202" y="152" font-size="12">M 릴리스(tick)  점선 = tick</text>
</svg>
```
그림 4 — 시나리오 (c3) 협력형, hog 조각 3 ms. H 릴리스(9.7 ms)부터 H가 실제로 도는 12.3 ms까지 B의 3 ms 조각이 끝나기를 기다렸다. M은 10 ms에 릴리스됐지만 B가 양보하고 H가 끝난 뒤인 12.5 ms에야 돌았다.

### 5.3 FreeRTOS 협력형의 구멍 — ISR의 `portYIELD_FROM_ISR`은 여전히 전환한다

c1과 c2의 차이는 ISR 한 줄(`portYIELD_FROM_ISR(woken)`)뿐이다. 소스를 보면 이유가 보인다. 단일 코어 `vTaskGenericNotifyGiveFromISR()`(`tasks.c` 8315행에서 시작)는 깨운 태스크가 현재 태스크보다 높으면 `configUSE_PREEMPTION` 값과 **상관없이** `*pxHigherPriorityTaskWoken = pdTRUE`를 쓴다(8397~8416행의 단일 코어 분기에 `#if`가 없다. 같은 함수의 SMP 분기(8418행~)에만 `#if ( configUSE_PREEMPTION == 1 )`이 있다. `xTaskGenericNotifyFromISR()`도 같은 모양이다). 그리고 `portYIELD_FROM_ISR(x)`는 ARM_CM3 포트에서 `x`가 참이면 PendSV를 거는 매크로일 뿐이다(`portmacro.h` 98~111행). 커널의 "협력형 스위치"는 커널 **자신의** yield 매크로들만 비워 둔 것이고, 애플리케이션 ISR이 직접 거는 PendSV는 막지 않는다.

결과적으로 FreeRTOS 협력형 + ISR yield는 "ISR이 깨운 태스크만 선점할 수 있는 하이브리드"가 된다(c1에서 M은 tick으로 깨므로 여전히 hog의 양보를 기다려 최악 1970 µs).

- 의도한 것이라면 쓸모 있는 패턴이다: "태스크끼리는 협력형(공유 데이터 잠금이 거의 필요 없음), 인터럽트 구동 긴급 처리만 선점".
- 의도하지 않았다면 버그다: 협력형이라 믿고 공유 변수를 잠금 없이 쓰던 태스크가 ISR이 깨운 태스크에게 한가운데서 선점당한다. 협력형으로 쓸 거면 ISR에서 `portYIELD_FROM_ISR`을 부르지 않는 규칙을 코드 리뷰로 지킨다.

### 5.4 협력형 응답 시간 규칙

협력형에서 태스크 i의 최악 대기는 대략 **"자기보다 낮은 태스크들의 가장 긴 비양보 구간 하나" + "자기보다 높은 태스크들의 일"** 이다. 앞의 항을 **blocking term**이라 부르며, N3(응답 시간 분석)에서 비선점 구간을 가진 RTA 식으로 정식화한다. c2에서 H의 blocking term은 M의 1500 µs였다. 그래서 협력형 시스템을 설계할 때는 "모든 태스크의 가장 긴 비양보 구간"을 목록으로 관리해야 한다 — A2 §4.4의 "긴 일을 상태 머신으로 조각내기"가 바로 이 숫자를 줄이는 작업이다.

---

## 6. (d) Zephyr — 같은 실험, 다른 규칙

### 6.1 Zephyr 우선순위 지도

Zephyr의 스레드 우선순위는 **부호가 의미를 갖는다**(A5 §1에서 숫자 방향만 봤다. 여기서는 정책을 본다).

- **협력형(cooperative) 우선순위: 음수** `-CONFIG_NUM_COOP_PRIORITIES` ~ `-1`(기본 16개, `kernel/Kconfig` 27행). 이 스레드는 **다른 스레드에게 선점당하지 않는다**. ISR은 끼어들지만, ISR이 더 높은 스레드를 깨워도 ISR 종료 후 원래 협력형 스레드로 돌아온다. 스레드가 block하거나 `k_yield()`할 때만 바뀐다.
- **선점형(preemptible) 우선순위: 0 이상** `0` ~ `CONFIG_NUM_PREEMPT_PRIORITIES - 1`(기본 15개, 52행). FreeRTOS 태스크와 같은 규칙.
- 협력형은 숫자가 작으므로 **모든 선점형보다 높다** ("Cooperative threads always preempt preemptible threads" — Kconfig 도움말).
- **meta-IRQ 우선순위**: `CONFIG_NUM_METAIRQ_PRIORITIES`(기본 0, 95행)를 N으로 주면 가장 높은 N개 우선순위(가장 작은 숫자부터)가 meta-IRQ가 된다. 이 스레드는 **협력형 스레드와 스케줄러 잠금(`k_sched_lock`)마저 선점한다.**
- idle 스레드는 가장 낮은 우선순위 하나를 혼자 쓴다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="30" y="20" font-size="12">높은 우선순위</text>
<text x="650" y="20" font-size="12" text-anchor="end">낮은 우선순위</text>
<line x1="120" y1="16" x2="560" y2="16" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="30" y="34" width="310" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="185" y="56" font-size="13" text-anchor="middle">협력형 (음수): -16 ~ -1</text>
<text x="185" y="76" font-size="12" text-anchor="middle">다른 스레드에게 선점당하지 않는다</text>
<text x="185" y="91" font-size="12" text-anchor="middle">(meta-IRQ는 예외)</text>
<rect x="340" y="34" width="290" height="62" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="485" y="56" font-size="13" text-anchor="middle">선점형: 0 ~ 14</text>
<text x="485" y="76" font-size="12" text-anchor="middle">더 높은 우선순위가 오면 선점된다</text>
<text x="485" y="91" font-size="12" text-anchor="middle">TIMESLICE_PRIORITY 이하만 slicing</text>
<rect x="630" y="34" width="20" height="62" fill="#888" fill-opacity="0.25" stroke="#888"/>
<rect x="30" y="34" width="20" height="62" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<line x1="30" y1="104" x2="650" y2="104" stroke="currentColor"/>
<text x="40" y="120" font-size="12" text-anchor="middle">-16</text>
<text x="195" y="120" font-size="12" text-anchor="middle">-8</text>
<text x="330" y="120" font-size="12" text-anchor="middle">-1</text>
<text x="350" y="120" font-size="12" text-anchor="middle">0</text>
<text x="485" y="120" font-size="12" text-anchor="middle">7</text>
<text x="620" y="120" font-size="12" text-anchor="middle">14</text>
<text x="645" y="134" font-size="12" text-anchor="end">idle = 15</text>
<text x="30" y="134" font-size="12">빨강 칸 = meta-IRQ (NUM_METAIRQ_PRIORITIES=1이면 -16)</text>
<text x="30" y="162" font-size="12">이 노트의 배치</text>
<circle cx="369" cy="182" r="5" fill="#d0564a"/><circle cx="388" cy="182" r="5" fill="#e08a3c"/><circle cx="446" cy="182" r="5" fill="#4a7bd0"/>
<text x="460" y="186" font-size="12">z1/z1n/z4: H=1 M=2 A,B=5</text>
<circle cx="292" cy="206" r="5" fill="#d0564a"/><circle cx="311" cy="206" r="5" fill="#e08a3c"/><circle cx="330" cy="206" r="5" fill="#4a7bd0"/>
<text x="350" y="210" font-size="12">z2: H=-3 M=-2 A,B=-1 (전부 협력형)</text>
<circle cx="40" cy="230" r="5" fill="#d0564a"/><circle cx="311" cy="230" r="5" fill="#e08a3c"/><circle cx="330" cy="230" r="5" fill="#4a7bd0"/>
<text x="350" y="234" font-size="12">z3: H=-16 (meta-IRQ), M=-2, A,B=-1</text>
<circle cx="40" cy="254" r="5" fill="#d0564a"/><circle cx="388" cy="254" r="5" fill="#e08a3c"/><circle cx="446" cy="254" r="5" fill="#4a7bd0"/>
<text x="460" y="258" font-size="12">z5: H=-16, M=2, A,B=5</text>
</svg>
```
그림 5 — Zephyr 기본 설정(`NUM_COOP_PRIORITIES=16`, `NUM_PREEMPT_PRIORITIES=15`)의 우선순위 지도와 이 노트 실험의 배치. 점 색은 빨강 H, 주황 M, 파랑 A·B.

### 6.2 "협력형"과 "스케줄러 잠금"은 Zephyr 안에서 같은 비트다

Zephyr 소스의 작은 묘수다. `struct _thread_base`(`include/zephyr/kernel/thread.h` 75~86행)에 `int8_t prio`와 `uint8_t sched_locked`를 나란히 두고, 둘을 합친 `uint16_t preempt`를 union으로 겹쳐 놓았다(리틀 엔디언에서 prio가 하위 바이트).

- 협력형 스레드: prio가 음수 → 하위 바이트가 0x80~0xFF → `preempt ≥ 0x0080`.
- `k_sched_lock()`: `sched_locked`를 0에서 0xFF, 0xFE …로 감소 → 상위 바이트가 0이 아님 → `preempt ≥ 0x0100`.
- `thread_is_preemptible()`(`kernel/include/kthread.h` 65행)은 `preempt <= _PREEMPT_THRESHOLD(0x7F)` 비교 **한 번**으로 둘을 동시에 판정한다.

그래서 Zephyr에서 **"협력형 스레드 = 영원히 스케줄러를 잠근 스레드"** 와 같고, `k_sched_lock()`은 "선점형 스레드를 잠시 협력형으로 만드는 것"이다. 결정은 `should_preempt()`(kthread.h 224~254행)에서 난다.

```c
/* kernel/include/kthread.h (v4.4.2) should_preempt() — 주석 줄임 */
static ALWAYS_INLINE bool should_preempt(const struct k_thread *thread, int preempt_ok)
{
	if (preempt_ok != 0) return true;                        /* k_yield() 등 스스로 양보 */
	if (z_is_thread_prevented_from_running(_current)) return true;   /* 지금 스레드가 block됨 */
	if (thread_is_preemptible(_current) || thread_is_metairq(thread)) return true;
	if (IS_ENABLED(CONFIG_SWAP_NONATOMIC) && z_is_thread_timeout_active(thread)) return true;
	return false;                                            /* 협력형·잠금 중이면 바꾸지 않는다 */
}
```

세 번째 줄의 `|| thread_is_metairq(thread)`가 meta-IRQ의 정의 전부다: 새로 고른 스레드가 meta-IRQ면 현재 스레드가 협력형이든 잠금 중이든 선점한다.

### 6.3 Zephyr time slicing 설정

| Kconfig | 기본값 (v4.4.2 `kernel/Kconfig`) | 뜻 |
|---|---|---|
| `CONFIG_TIMESLICING` | y (선점 우선순위가 있고 시스템 시계가 있을 때) | 같은 우선순위 선점형 스레드끼리 시분할 |
| `CONFIG_TIMESLICE_SIZE` | 20 (ms) | slice 길이. 0이면 무한(= 사실상 끔) |
| `CONFIG_TIMESLICE_PRIORITY` | 0 | 이 값**보다 높은** 우선순위(작은 숫자)는 slicing 제외. 0이면 선점형 전부가 대상 |
| `CONFIG_TIMESLICE_PER_THREAD` | n | `k_thread_time_slice_set()`으로 스레드마다 slice와 만료 콜백 |

- 기본 slice가 **20 ms**라는 점에 주의한다. FreeRTOS(1 tick)와 비교하려면 이 노트처럼 `CONFIG_TIMESLICE_SIZE=1`로 맞춰야 한다. 실행 중에 `k_sched_time_slice_set(slice_ms, prio)`로 바꿀 수도 있다.
- 협력형 스레드는 slicing 대상이 아니다(`z_time_slice_size()`가 `thread_is_preemptible()`을 먼저 본다, `kernel/timeslicing.c`).
- 구현: Zephyr는 FreeRTOS처럼 "매 tick 검사"가 아니라 **slice 전용 timeout**을 건다. 스레드가 switch-in될 때 `update_cache()`가 `z_reset_time_slice()`를 불러 `slice_timeouts[cpu]`를 `K_TICKS(slice_size - 1)`로 다시 건다. timeout이 만료되면 다음 timer 인터럽트의 `z_time_slice()`가 `move_current_to_end_of_prio_q()`로 현재 스레드를 같은 우선순위 큐의 맨 뒤로 보낸다. Zephyr timeout은 tick 경계에서 만료되므로, slice 1 tick이면 결국 FreeRTOS처럼 "다음 tick 경계까지"가 된다(그림 6에서 확인).

### 6.4 예제 6 — Zephyr 일감과 선점·slicing 실험

무엇을 확인하는 코드인지: FreeRTOS `main.c`와 같은 일감을 Zephyr API로. `SCEN` 값(CMake 변수 → 컴파일 정의)으로 우선순위와 hog 동작을 바꾸고, 추가 conf 파일로 Kconfig를 바꾼다. trace는 Zephyr의 `CONFIG_TRACING_USER` 훅 `sys_trace_thread_switched_in_user()`로 FreeRTOS와 같은 형식을 남긴다. 시각은 `k_cycle_get_32()`(SysTick 기반, 25 MHz)이고, 이 보드에서 cycle 0이 tick 0이라 M 릴리스를 `tick × 25000`으로 바로 쓴다.

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
CONFIG_TIMESLICING=y
CONFIG_TIMESLICE_SIZE=1
CONFIG_TIMESLICE_PRIORITY=0
# noslice.conf: CONFIG_TIMESLICING=n      metairq.conf: CONFIG_NUM_METAIRQ_PRIORITIES=1
```

```c
/* C4 Zephyr: FreeRTOS 예제와 같은 일감. SCEN: 1=선점(H1 M2 A/B5)  2=전부 협력형(H-3 M-2 A/B-1, k_yield)
 *   3=2 + H를 meta-IRQ(K_HIGHEST_THREAD_PRIO)  4=1 + A가 k_sched_lock 2 ms  5=4 + H meta-IRQ */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>                    /* IRQ_CONNECT */
#include <zephyr/sys/printk.h>
#define REG32(a) (*(volatile uint32_t *)(a))
#define T0 0x40000000u                     /* CMSDK TIMER0 */
enum { NREL = 50, TN = 8192 };
static volatile uint32_t tt[TN], ti; static volatile char tn[TN];
static uint32_t rel[NREL], hdone[NREL], mrel[64], mdone[64], nm, lp100;
static volatile uint32_t nrel, hogcnt[2];
static struct k_thread th[4]; static K_THREAD_STACK_ARRAY_DEFINE(stk, 4, 1024); K_SEM_DEFINE(hsem, 0, NREL + 1);
#if SCEN == 1 || SCEN == 4
enum { PH = 1, PM = 2, PA = 5 };
#elif SCEN == 2
enum { PH = -3, PM = -2, PA = -1 };
#elif SCEN == 3 || SCEN == 5
enum { PH = K_HIGHEST_THREAD_PRIO, PM = (SCEN == 3) ? -2 : 2, PA = (SCEN == 3) ? -1 : 5 };
#endif
void sys_trace_thread_switched_in_user(void) {
	struct k_thread *c = k_current_get(); uint32_t i = ti;
	if (i < TN) { tt[i] = k_cycle_get_32(); tn[i] = c == &th[0] ? 'H' : c == &th[1] ? 'M' :
		c == &th[2] ? 'A' : c == &th[3] ? 'B' : 'I'; ti = i + 1; }
}
static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }
static void work_us(uint32_t us) { spin(us * lp100 / 100u); }
static void dump(void) {
	REG32(T0) = 0; uint32_t n = ti; ti = TN;
	printk("CFG zephyr SCEN=%d prio H/M/A/B=%d/%d/%d/%d\n", SCEN, PH, PM, PA, PA);
	for (uint32_t i = 0; i < n; i++) printk("T %u %c\n", tt[i], tn[i]);
	for (uint32_t k = 0; k < NREL; k++) printk("H %u 0 %u\n", rel[k], hdone[k]);
	for (uint32_t k = 0; k < nm; k++) printk("Z %u %u\n", mrel[k], mdone[k]);
	printk("HOG %u %u\nEND\n", hogcnt[0], hogcnt[1]);
	register unsigned r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;   /* semihosting exit */
	__asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
}
static void t0_isr(const void *a) {
	ARG_UNUSED(a); REG32(T0 + 0xC) = 1;
	uint32_t now = k_cycle_get_32(), lag = REG32(T0 + 8) - REG32(T0 + 4);
	if (nrel < NREL) rel[nrel] = now - lag;
	if (++nrel > NREL) dump();
	k_sem_give(&hsem);
}
static void fH(void *a, void *b, void *c) {
	for (uint32_t k = 0;; k++) { k_sem_take(&hsem, K_FOREVER); work_us(200); if (k < NREL) hdone[k] = k_cycle_get_32(); }
}
static void fM(void *a, void *b, void *c) {
	int64_t next = k_uptime_ticks();
	for (;;) { next += 10; k_sleep(K_TIMEOUT_ABS_TICKS(next));
		uint32_t r = (uint32_t)(next * 25000); work_us(1500);
		if (nm < 64) { mrel[nm] = r; mdone[nm++] = k_cycle_get_32(); } }
}
static void hog(void *a, void *b, void *c) {
	uint32_t id = (uint32_t)a;
	for (uint32_t n = 0;; n++) {
		if (SCEN >= 4 && id == 0 && n % 8 == 0) { k_sched_lock(); work_us(2000); k_sched_unlock(); }
		work_us(500); hogcnt[id]++;
		if (SCEN == 2 || SCEN == 3) k_yield();
	}
}
int main(void) {
	k_thread_priority_set(k_current_get(), K_HIGHEST_THREAD_PRIO);   /* 만드는 동안 안 뺏기게 */
	uint32_t t0 = k_cycle_get_32(); spin(100000); lp100 = (uint32_t)(100000ull * 2500u / (k_cycle_get_32() - t0));
	k_thread_entry_t f[4] = { fH, fM, hog, hog }; int p[4] = { PH, PM, PA, PA };
	for (int i = 0; i < 4; i++)
		k_thread_create(&th[i], stk[i], 1024, f[i], (void *)(i - 2), NULL, NULL, p[i], 0, K_NO_WAIT);
	IRQ_CONNECT(8, 1, t0_isr, NULL, 0); irq_enable(8);
	REG32(T0 + 8) = 4870 * 25 - 1; REG32(T0 + 4) = 4870 * 25 - 1; REG32(T0) = 1 | 8;
	return 0;
}
```

- `main()`이 첫 줄에서 자기 우선순위를 가장 높은 협력형으로 올리는 이유: SCEN 2에서 협력형 hog를 만드는 순간 그 hog가 (우선순위 0인) main을 선점하고 영원히 돌려주지 않아, 나머지 스레드를 못 만든다. 실제로 처음 판에서 겪을 뻔한 함정이다 — Zephyr 협력형 스레드는 **만들어지는 순간부터** 협력형이다.
- Zephyr 판은 ISR 진입 지연(lag)을 출력하지 않는다(0으로 찍음). 분석 출력의 "ISR lag max 0.0"은 측정값이 아니다.
- 실행은 랩의 `rtos-zbuild`로 빌드한 뒤 FreeRTOS와 같은 icount 옵션으로 직접 QEMU를 띄운다.

```sh
# ~/.rtos_scratch/C4/runz.sh <이름> <SCEN> [추가.conf] 의 핵심 두 줄
/var/rtoslab/bin/rtos-zbuild mps2/an385 $Z $O -- -DSCEN=$S $X
timeout 30 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -kernel $O/zephyr/zephyr.elf > $O/out.txt
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/C4/runz.sh; $R z1 1; $R z1n 1 noslice.conf
  cd /var/tmp/rtos/C4; python3 -I /Users/donh/.rtos_scratch/C4/analyze.py z1/out.txt z1n/out.txt'
```

```text
           FLASH:       15188 B         4 MB      0.36%
             RAM:       52976 B         4 MB      1.26%
z1 exit=0 lines=417
warning: TIMESLICE_SIZE (defined at soc/sensry/ganymed/sy1xx/Kconfig.defconfig:70,
warning: TIMESLICE_PRIORITY (defined at kernel/Kconfig:698) was assigned the value '0' but got the
           FLASH:       14704 B         4 MB      0.35%
             RAM:       52944 B         4 MB      1.26%
z1n exit=0 lines=228
[z1] zephyr SCEN=1 prio H/M/A/B=1/2/5/5
  H resp us min/avg/max    217/   228/   243  (n=50)   ISR lag max 0.0 us
  M resp us min/avg/max   1540/  1611/  1782  (n=24)
  CPU% A=40.9  B=38.9  H=4.6  M=15.6 | switches=323 | hog chunks A=195 B=187 | max gap A=3.0ms B=4.0ms
[z1n] zephyr SCEN=1 prio H/M/A/B=1/2/5/5
  H resp us min/avg/max    211/   211/   221  (n=50)   ISR lag max 0.0 us
  M resp us min/avg/max   1513/  1591/  1731  (n=24)
  CPU% A=80.5  H=4.3  M=15.2 | switches=144 | hog chunks A=401 B=0 | max gap A=1.7ms B=238.6ms
```

(z1n의 Kconfig 경고 두 줄은 `prj.conf`가 `TIMESLICE_SIZE`·`TIMESLICE_PRIORITY`를 정했는데 `noslice.conf`가 그 의존 대상인 `TIMESLICING`을 껐기 때문에 나온 "값이 무시됨" 경고다. C 컴파일 경고는 0개.)

출력에서 볼 것:

- **z1(선점 + slice 1 ms)** 은 FreeRTOS (a)와 거의 같은 그림이다. switch 323번(FreeRTOS 327), A·B 40.9%·38.9%. H 응답이 217~243 µs로 FreeRTOS보다 10~30 µs 길고 더 흔들리는 것은 Zephyr의 ISR → `k_sem_give` → swap 경로와, trace 훅 안에서 부르는 `k_cycle_get_32()`(spinlock을 잡는다)의 비용이 섞인 것이라 커널 성능 비교로 읽으면 안 된다.
- **z1n(slicing 끔)에서 B는 238.6 ms 동안 한 번도 돌지 못했다** (조각 0개). FreeRTOS (b)의 40%/40%와 정반대다. §3.4 표의 z1n 줄이 이유를 보여 준다: H·M이 끝나면 **선점당했던 바로 그 스레드(A)** 가 돌아온다(64번 중 64번).

이유는 `next_up()`(`kernel/sched.c` 185행~)에 있다. 단일 코어에서 Zephyr는 실행 중인 스레드를 ready 큐에 그대로 둔 채(큐에서 빼지 않음) 선점당하게 하므로, 선점당한 스레드는 같은 우선순위 큐의 **맨 앞 자리를 유지**한다. 같은 우선순위 큐의 맨 뒤로 가는 것은 `k_yield()`(`runq_yield()`)와 slice 만료(`move_current_to_end_of_prio_q()`)뿐이다. FreeRTOS는 "고를 때마다 다음 차례"(`listGET_OWNER_OF_NEXT_ENTRY`), Zephyr는 "양보하거나 slice가 끝날 때만 뒤로". Zephyr 쪽이 POSIX `SCHED_FIFO`(선점당한 스레드는 자기 우선순위 큐의 맨 앞에 남는다, EL F1)와 같은 의미다.

```svg
<svg viewBox="0 0 680 168" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="29" font-size="13">H</text>
<line x1="70" y1="38" x2="660" y2="38" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="55" font-size="13">M</text>
<line x1="70" y1="64" x2="660" y2="64" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="81" font-size="13">A</text>
<line x1="70" y1="90" x2="660" y2="90" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="107" font-size="13">B</text>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor" stroke-opacity="0.2"/>
<rect x="70.0" y="68" width="26.3" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="96.3" y="94" width="18.7" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="115.0" y="16" width="10.9" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="125.9" y="94" width="19.6" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="145.5" y="68" width="49.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="194.6" y="94" width="49.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="243.8" y="68" width="48.7" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="292.5" y="94" width="48.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="341.4" y="42" width="12.2" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="353.6" y="16" width="10.8" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="364.5" y="42" width="64.6" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="429.0" y="94" width="11.4" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="440.5" y="68" width="49.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="489.6" y="94" width="49.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="538.8" y="68" width="49.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="588.0" y="94" width="4.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="592.1" y="16" width="10.9" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="603.0" y="94" width="34.1" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="637.1" y="68" width="22.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="94.6" y1="12" x2="94.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="143.8" y1="12" x2="143.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="192.9" y1="12" x2="192.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="242.1" y1="12" x2="242.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="291.2" y1="12" x2="291.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="340.4" y1="12" x2="340.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="389.6" y1="12" x2="389.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="438.8" y1="12" x2="438.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="487.9" y1="12" x2="487.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="537.1" y1="12" x2="537.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="586.2" y1="12" x2="586.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="635.4" y1="12" x2="635.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<path d="M113.9 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M352.6 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M591.0 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M340.4 42 l-4 -7 h8 z" fill="#e08a3c"/>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor"/>
<line x1="94.6" y1="116" x2="94.6" y2="120" stroke="currentColor"/>
<text x="94.6" y="133" font-size="12" text-anchor="middle">27</text>
<line x1="143.8" y1="116" x2="143.8" y2="120" stroke="currentColor"/>
<text x="143.8" y="133" font-size="12" text-anchor="middle">28</text>
<line x1="192.9" y1="116" x2="192.9" y2="120" stroke="currentColor"/>
<text x="192.9" y="133" font-size="12" text-anchor="middle">29</text>
<line x1="242.1" y1="116" x2="242.1" y2="120" stroke="currentColor"/>
<text x="242.1" y="133" font-size="12" text-anchor="middle">30</text>
<line x1="291.2" y1="116" x2="291.2" y2="120" stroke="currentColor"/>
<text x="291.2" y="133" font-size="12" text-anchor="middle">31</text>
<line x1="340.4" y1="116" x2="340.4" y2="120" stroke="currentColor"/>
<text x="340.4" y="133" font-size="12" text-anchor="middle">32</text>
<line x1="389.6" y1="116" x2="389.6" y2="120" stroke="currentColor"/>
<text x="389.6" y="133" font-size="12" text-anchor="middle">33</text>
<line x1="438.8" y1="116" x2="438.8" y2="120" stroke="currentColor"/>
<text x="438.8" y="133" font-size="12" text-anchor="middle">34</text>
<line x1="487.9" y1="116" x2="487.9" y2="120" stroke="currentColor"/>
<text x="487.9" y="133" font-size="12" text-anchor="middle">35</text>
<line x1="537.1" y1="116" x2="537.1" y2="120" stroke="currentColor"/>
<text x="537.1" y="133" font-size="12" text-anchor="middle">36</text>
<line x1="586.2" y1="116" x2="586.2" y2="120" stroke="currentColor"/>
<text x="586.2" y="133" font-size="12" text-anchor="middle">37</text>
<line x1="635.4" y1="116" x2="635.4" y2="120" stroke="currentColor"/>
<text x="635.4" y="133" font-size="12" text-anchor="middle">38</text>
<text x="660" y="152" font-size="12" text-anchor="end">ms (tick 0 기준)</text>
<path d="M74 152 l-4 -7 h8 z" fill="#d0564a"/><text x="82" y="152" font-size="12">H 릴리스(ISR)</text>
<path d="M194 152 l-4 -7 h8 z" fill="#e08a3c"/><text x="202" y="152" font-size="12">M 릴리스(tick)  점선 = tick</text>
</svg>
```
그림 6 — Zephyr z1(선점 + slice 1 ms)의 실제 trace(26.5~38.5 ms). 27.4 ms에 H가 B를 선점했다가 끝나자 **B가 돌아온다**(FreeRTOS 그림 2와 비교). 32.2 ms에는 H(1)가 M(2)을 선점한다. B가 돌아와 받는 slice는 다음 tick 경계까지라서, slice 1 tick에서는 FreeRTOS처럼 잘린 조각이 생긴다(조각 245개 중 97개가 0.9 ms 미만).

### 6.5 예제 7 — Zephyr 협력형과 meta-IRQ

무엇을 확인하는 코드인지: (z2) 전부 협력형 — H=-3, M=-2, A·B=-1, hog는 0.5 ms 조각마다 `k_yield()`. (z3) z2에서 H만 meta-IRQ 우선순위(`K_HIGHEST_THREAD_PRIO` = -16, `CONFIG_NUM_METAIRQ_PRIORITIES=1`).

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/C4/runz.sh; $R z2 2; $R z3 3 metairq.conf
  cd /var/tmp/rtos/C4; python3 -I /Users/donh/.rtos_scratch/C4/analyze.py z2/out.txt z3/out.txt; grep METAIRQ z3/zephyr/.config'
```

```text
z2 exit=0 lines=552
z3 exit=0 lines=600
[z2] zephyr SCEN=2 prio H/M/A/B=-3/-2/-1/-1
  H resp us min/avg/max    224/   525/  1595  (n=50)   ISR lag max 0.0 us
  M resp us min/avg/max   1532/  1836/  2231  (n=24)
  CPU% A=40.3  B=40.1  H=4.3  M=15.3 | switches=450 | hog chunks A=199 B=197 | max gap A=2.2ms B=2.8ms
[z3] zephyr SCEN=3 prio H/M/A/B=-16/-2/-1/-1
  H resp us min/avg/max    213/   214/   226  (n=50)   ISR lag max 0.0 us
  M resp us min/avg/max   1592/  1890/  2247  (n=24)
  CPU% A=40.0  B=40.4  H=4.3  M=15.3 | switches=497 | hog chunks A=197 B=198 | max gap A=2.2ms B=2.8ms
CONFIG_NUM_METAIRQ_PRIORITIES=1
```

출력에서 볼 것:

- z2는 FreeRTOS 협력형 c2(평균 520, 최악 1716 µs)와 거의 같은 숫자(525, 1595 µs)다. 협력형은 커널이 달라도 "가장 긴 비양보 구간"이 지배한다. 여기서 최악도 M의 1500 µs 구간이다.
- z3에서는 다른 스레드가 전부 협력형인데도 H가 213~226 µs로 선점형 수준이 됐다. **ISR이 meta-IRQ 스레드를 깨우면, ISR 종료 시 협력형 스레드를 선점한다.**
- M은 z3에서 오히려 약간 늦어졌다(평균 1836 → 1890). H가 끼어든 시간만큼 앞 hog의 조각이 늦게 끝나기 때문이다.

```svg
<svg viewBox="0 0 680 168" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="29" font-size="13">H</text>
<line x1="70" y1="38" x2="660" y2="38" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="55" font-size="13">M</text>
<line x1="70" y1="64" x2="660" y2="64" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="81" font-size="13">A</text>
<line x1="70" y1="90" x2="660" y2="90" stroke="currentColor" stroke-opacity="0.2"/>
<text x="10" y="107" font-size="13">B</text>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor" stroke-opacity="0.2"/>
<rect x="70.0" y="94" width="6.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="76.9" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="101.9" y="94" width="13.7" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="115.6" y="16" width="10.3" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="125.9" y="94" width="11.8" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="137.7" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="162.7" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="187.6" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="212.5" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="237.5" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="262.4" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="287.4" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="312.3" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="337.2" y="68" width="17.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="355.0" y="16" width="10.3" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="365.4" y="68" width="8.3" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="373.6" y="42" width="74.5" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="448.1" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="473.1" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="498.0" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="522.9" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="547.9" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="572.8" y="68" width="21.6" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="594.4" y="16" width="10.3" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="604.8" y="68" width="3.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="608.7" y="94" width="24.9" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<rect x="633.6" y="68" width="24.9" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="658.6" y="94" width="1.4" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<line x1="94.6" y1="12" x2="94.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="143.8" y1="12" x2="143.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="192.9" y1="12" x2="192.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="242.1" y1="12" x2="242.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="291.2" y1="12" x2="291.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="340.4" y1="12" x2="340.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="389.6" y1="12" x2="389.6" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="438.8" y1="12" x2="438.8" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="487.9" y1="12" x2="487.9" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="537.1" y1="12" x2="537.1" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="586.2" y1="12" x2="586.2" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<line x1="635.4" y1="12" x2="635.4" y2="116" stroke="currentColor" stroke-opacity="0.25" stroke-dasharray="2,3"/>
<path d="M115.1 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M354.5 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M593.9 16 l-4 -7 h8 z" fill="#d0564a"/>
<path d="M340.4 42 l-4 -7 h8 z" fill="#e08a3c"/>
<line x1="70" y1="116" x2="660" y2="116" stroke="currentColor"/>
<line x1="94.6" y1="116" x2="94.6" y2="120" stroke="currentColor"/>
<text x="94.6" y="133" font-size="12" text-anchor="middle">27</text>
<line x1="143.8" y1="116" x2="143.8" y2="120" stroke="currentColor"/>
<text x="143.8" y="133" font-size="12" text-anchor="middle">28</text>
<line x1="192.9" y1="116" x2="192.9" y2="120" stroke="currentColor"/>
<text x="192.9" y="133" font-size="12" text-anchor="middle">29</text>
<line x1="242.1" y1="116" x2="242.1" y2="120" stroke="currentColor"/>
<text x="242.1" y="133" font-size="12" text-anchor="middle">30</text>
<line x1="291.2" y1="116" x2="291.2" y2="120" stroke="currentColor"/>
<text x="291.2" y="133" font-size="12" text-anchor="middle">31</text>
<line x1="340.4" y1="116" x2="340.4" y2="120" stroke="currentColor"/>
<text x="340.4" y="133" font-size="12" text-anchor="middle">32</text>
<line x1="389.6" y1="116" x2="389.6" y2="120" stroke="currentColor"/>
<text x="389.6" y="133" font-size="12" text-anchor="middle">33</text>
<line x1="438.8" y1="116" x2="438.8" y2="120" stroke="currentColor"/>
<text x="438.8" y="133" font-size="12" text-anchor="middle">34</text>
<line x1="487.9" y1="116" x2="487.9" y2="120" stroke="currentColor"/>
<text x="487.9" y="133" font-size="12" text-anchor="middle">35</text>
<line x1="537.1" y1="116" x2="537.1" y2="120" stroke="currentColor"/>
<text x="537.1" y="133" font-size="12" text-anchor="middle">36</text>
<line x1="586.2" y1="116" x2="586.2" y2="120" stroke="currentColor"/>
<text x="586.2" y="133" font-size="12" text-anchor="middle">37</text>
<line x1="635.4" y1="116" x2="635.4" y2="120" stroke="currentColor"/>
<text x="635.4" y="133" font-size="12" text-anchor="middle">38</text>
<text x="660" y="152" font-size="12" text-anchor="end">ms (tick 0 기준)</text>
<path d="M74 152 l-4 -7 h8 z" fill="#d0564a"/><text x="82" y="152" font-size="12">H 릴리스(ISR)</text>
<path d="M194 152 l-4 -7 h8 z" fill="#e08a3c"/><text x="202" y="152" font-size="12">M 릴리스(tick)  점선 = tick</text>
</svg>
```
그림 7 — Zephyr z3(협력형 hog + meta-IRQ H)의 실제 trace. 32.3 ms에 H가 협력형 A의 조각 한가운데를 선점하고, 끝나자 **A로 돌아가** 조각을 마친다(32.5~32.65 ms). A가 `k_yield()`한 뒤에야 M(-2)이 돈다. meta-IRQ가 끼어든 것 말고는 협력형 규칙이 그대로 지켜졌다.

### 6.6 meta-IRQ가 보장하는 것과 보장하지 않는 것

- **보장**: ready가 된 meta-IRQ 스레드는 협력형 스레드와 `k_sched_lock()` 구간을 무시하고 다음 스케줄링 지점(ISR 종료 포함)에서 바로 돈다(`should_preempt()`의 `thread_is_metairq(thread)`).
- **돌아갈 곳 보장**: meta-IRQ가 협력형 스레드를 선점하면 `update_metairq_preempt()`가 그 스레드를 `_current_cpu->metairq_preempted`에 기록하고, `next_up()`이 meta-IRQ가 끝난 뒤 **우선순위가 더 높은 다른 협력형 스레드가 있어도** 그 스레드로 돌아간다(sched.c 166~210행). 협력형 스레드에게 한 "다른 스레드에게는 선점당하지 않는다"는 약속을 지키기 위해서다. 그림 7의 32.5 ms가 그 장면이다.
- **보장 안 함**: meta-IRQ는 ISR이 아니다. 인터럽트 마스크(`irq_lock`) 구간은 뚫지 못하고, 깨우는 ISR이 끝나야 돈다. 그리고 meta-IRQ 스레드끼리는 일반 우선순위 규칙을 따른다.
- **용도**: Kconfig 도움말의 표현 그대로 "interrupt bottom half" 또는 "tasklet" — 드라이버가 ISR에서 일을 빨리 넘기고, 그 나머지가 애플리케이션 코드보다 먼저 끝나도록 보장하는 장치다. 같은 도움말이 "협력형 스레드에 대한 약속을 깨므로 애플리케이션 코드에서는 아마 쓰지 않는 게 좋다(probably shouldn't be used from application code)"고 적고 있다. 실무에서는 드라이버·통신 스택의 하반부, 또는 협력형으로 짠 레거시 시스템에 긴급 경로 하나를 뚫을 때 쓴다.

### 6.7 FreeRTOS와 Zephyr 정책 대응표

| 하고 싶은 것 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| 고정 우선순위 선점 | `configUSE_PREEMPTION 1` (필수 정의) | 우선순위 0 이상 스레드 |
| 같은 우선순위 시분할 | `configUSE_TIME_SLICING 1`(기본), quantum = 1 tick 고정 | `CONFIG_TIMESLICING=y`(기본), `CONFIG_TIMESLICE_SIZE`(기본 20 ms), `CONFIG_TIMESLICE_PRIORITY` |
| 선점당한 같은 우선순위 태스크의 다음 차례 | 다음 태스크로 넘어감 (`pxIndex` 전진) | 선점당한 스레드가 맨 앞 유지 |
| 시스템 전체 협력형 | `configUSE_PREEMPTION 0` | 전 스레드를 음수 우선순위로 (`NUM_PREEMPT_PRIORITIES=0`도 가능) |
| 일부 스레드만 협력형 | 없음 (스케줄러 잠금으로 흉내) | 음수 우선순위 |
| 협력형도 뚫는 긴급 스레드 | 없음 (ISR yield가 사실상 그 역할, §5.3) | meta-IRQ 우선순위 |
| 잠깐 선점 금지 | `vTaskSuspendAll()` / `xTaskResumeAll()` | `k_sched_lock()` / `k_sched_unlock()` |
| 태스크별 선점 금지 | `vTaskPreemptionDisable()` — **SMP 전용** (`FreeRTOS.h` 2909행: 단일 코어에서 쓰면 `#error`) | 협력형 우선순위로 바꾸기 |

---

## 7. ThreadX preemption-threshold — "이 우선순위보다 높은 것만 나를 선점하라"

### 7.1 개념

Eclipse ThreadX(옛 Azure RTOS ThreadX)의 `tx_thread_create()`에는 우선순위 옆에 **preemption threshold** 인자가 하나 더 있다. 스레드가 실행 중일 때는 **우선순위가 threshold보다 높은(숫자가 작은) 스레드만** 그 스레드를 선점할 수 있다.

- threshold = 자기 우선순위 → 보통의 선점형.
- threshold = 0 → 아무도(우선순위 0 스레드도) 선점 못 함 → 스케줄러 잠금과 같다.
- 그 사이 값 → "자기와 threshold 사이 우선순위의 스레드들에게는 비선점, 그보다 높은 것에는 선점". 비선점 그룹을 만들어 **switch 수를 줄이고, 그 그룹 안의 공유 데이터에 잠금이 필요 없게** 한다. 그러면서도 최상위 긴급 스레드의 응답은 지킨다.
- threshold는 **실행 중일 때의** 보호막이다. ready 큐 안에서 누가 먼저 고려되는지는 여전히 우선순위가 정한다.

소스에서는 `tx_thread_system_resume.c`가 스레드를 ready로 만들 때 이렇게 비교한다(238행, 759행): `if (priority < execute_ptr -> tx_thread_preempt_threshold)` → 그때만 선점. `tx_thread_create.c` 191~209행은 `TX_DISABLE_PREEMPTION_THRESHOLD`로 이 기능을 빼고 빌드하면 "threshold ≠ priority"인 스레드는 **선점 완전 금지(threshold 0)** 로 바꿔 버린다는 것도 보여 준다(기능을 빼도 의미가 "더 보수적"인 쪽으로 남도록).

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="40" y="20" font-size="12">ThreadX 우선순위 (0 = 가장 높음)</text>
<rect x="40" y="40" width="190" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="135" y="60" font-size="12" text-anchor="middle">0 ~ 9: L을 선점 가능</text>
<text x="135" y="76" font-size="12" text-anchor="middle">(priority &lt; threshold)</text>
<rect x="230" y="40" width="209" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="334" y="60" font-size="12" text-anchor="middle">10 ~ 20: L이 도는 동안 대기</text>
<text x="334" y="76" font-size="12" text-anchor="middle">(L과 같은 비선점 그룹)</text>
<rect x="439" y="40" width="190" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="534" y="66" font-size="12" text-anchor="middle">21 ~ 31: 원래 L보다 낮음</text>
<line x1="40" y1="96" x2="629" y2="96" stroke="currentColor"/>
<text x="40" y="112" font-size="12" text-anchor="middle">0</text>
<text x="135" y="112" font-size="12" text-anchor="middle">5</text>
<text x="230" y="112" font-size="12" text-anchor="middle">10</text>
<text x="325" y="112" font-size="12" text-anchor="middle">15</text>
<text x="420" y="112" font-size="12" text-anchor="middle">20</text>
<text x="620" y="112" font-size="12" text-anchor="middle">31</text>
<circle cx="135" cy="134" r="6" fill="#d0564a"/><text x="147" y="138" font-size="12">H (5)</text>
<circle cx="325" cy="134" r="6" fill="#e08a3c"/><text x="337" y="138" font-size="12">M (15)</text>
<circle cx="420" cy="134" r="6" fill="#4a7bd0"/><text x="432" y="138" font-size="12">L (20)</text>
<line x1="420" y1="150" x2="234" y2="150" stroke="#4a7bd0" marker-end="url(#ah3)"/>
<text x="327" y="168" font-size="12" text-anchor="middle">L의 threshold = 10 (보호막을 위로 올린다)</text>
<text x="40" y="196" font-size="12">L이 돌고 있을 때: H(5) → 선점한다 · M(15) → L이 끝나거나 block할 때까지 기다린다</text>
<text x="40" y="216" font-size="12">L이 ready 큐에서 기다릴 때는 threshold가 아무 의미 없다 (M이 L보다 먼저 뽑힌다)</text>
</svg>
```
그림 8 — preemption-threshold. 우선순위 20인 L에 threshold 10을 주면, L이 실행 중인 동안 10~20 사이의 스레드는 L을 선점하지 못하고, 0~9만 선점한다.

### 7.2 예제 8 — 세 가지 threshold로 같은 장면

A5 예제 3에서 같은 보드에 올린 ThreadX(커밋 `56c8695`, 6.5.2)를 `/var/tmp/rtos/C4/tx/threadx`로 **복사**해서 쓴다(다른 작성자 폴더를 직접 빌드하지 않는다). 접착 코드(`tx_glue.c`)와 빌드 스크립트도 A5에서 경로만 바꿔 가져왔다.

무엇을 확인하는 코드인지: L(우선순위 20)이 3 ms 일을 시작하고, 1 tick 뒤 M(15)이, 2 tick 뒤 H(5)가 깨어난다. L의 threshold를 20(끔) / 10 / 0으로 바꿔 누가 언제 도는지 µs 단위로 찍는다.

```c
/* C4 ex: ThreadX preemption-threshold — L(prio 20)이 3 ms 일을 하는 동안 M(15), H(5)가 깨어난다
 *   THR = L의 preemption threshold: 20(=끔) / 10 / 0(=선점 완전 금지) */
#include "tx_api.h"
#include "board.h"
#ifndef THR
#define THR 20
#endif
static TX_THREAD tl, tm, th; static ULONG sl[512], sm[512], sh[512];
static uint32_t t0, lp100;
static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }
static void work_us(uint32_t us) { spin(us * lp100 / 100u); }
static void stamp(const char *what) { board_printf_raw("%u us: %s\n", board_now_us() - t0, what); }

static void fL(ULONG a) {
    (void)a; t0 = board_now_us(); stamp("L start (3 ms of work)");
    work_us(1000); stamp("L 1/3");
    work_us(1000); stamp("L 2/3");
    work_us(1000); stamp("L done");
    tx_thread_sleep(5); board_exit(0);
}
static void fM(ULONG a) { (void)a; tx_thread_sleep(1); stamp("M runs (prio 15)"); }
static void fH(ULONG a) { (void)a; tx_thread_sleep(2); stamp("H runs (prio 5)"); }

void tx_application_define(void *first_unused) {
    (void)first_unused;
    uint32_t c = board_now_us(); spin(100000); lp100 = 100000u * 100u / (board_now_us() - c);
    board_printf_raw("ThreadX %u.%u.%u  L: priority 20, preemption threshold %u\n",
                     THREADX_MAJOR_VERSION, THREADX_MINOR_VERSION, THREADX_PATCH_VERSION, THR);
    tx_thread_create(&th, "H", fH, 0, sh, sizeof sh, 5, 5, TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_create(&tm, "M", fM, 0, sm, sizeof sm, 15, 15, TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_thread_create(&tl, "L", fL, 0, sl, sizeof sl, 20, THR, TX_NO_TIME_SLICE, TX_AUTO_START);
}
int main(void) { tx_kernel_enter(); }
```

```sh
limactl shell elinux -- bash -lc 'for t in 20 10 0; do O=/var/tmp/rtos/C4/tx/t$t; mkdir -p $O && cd $O &&
  /Users/donh/.rtos_scratch/C4/tx/build.sh /Users/donh/.rtos_scratch/C4/tx/pt.c $O -DTHR=$t 2>&1 | grep -i "warn\|error"
  echo "== THR=$t"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 10; done'
```

```text
== THR=20
ThreadX 6.5.2  L: priority 20, preemption threshold 20
0 us: L start (3 ms of work)
648 us: M runs (prio 15)
1035 us: L 1/3
1648 us: H runs (prio 5)
2068 us: L 2/3
3075 us: L done
[board_exit] code=0
== THR=10
ThreadX 6.5.2  L: priority 20, preemption threshold 10
0 us: L start (3 ms of work)
1022 us: L 1/3
1649 us: H runs (prio 5)
2056 us: L 2/3
3064 us: L done
3077 us: M runs (prio 15)
[board_exit] code=0
== THR=0
ThreadX 6.5.2  L: priority 20, preemption threshold 0
1 us: L start (3 ms of work)
1013 us: L 1/3
2022 us: L 2/3
3031 us: L done
3052 us: M runs (prio 15)
3649 us: H runs (prio 5)
[board_exit] code=0
```

출력에서 볼 것:

- **THR=20(보통 선점)**: M이 648 µs, H가 1648 µs에 각각 L을 선점했다(첫 tick이 L 시작 0.65 ms 뒤에 왔다).
- **THR=10**: H(5 < 10)는 그대로 1649 µs에 선점했지만, M(15 ≥ 10)은 L이 끝난 뒤인 3077 µs에야 돌았다. L의 "1/3" 표시가 1035 → 1022 µs로 당겨진 것은 M에게 뺏긴 시간이 사라졌기 때문이다.
- **THR=0**: 아무도 선점하지 못했다. H가 M보다도 **늦게**(3649 µs) 돈 것은 ThreadX의 timer 처리 때문으로 보인다: 이 포트에서 sleep 만료는 우선순위 0의 시스템 timer 스레드가 처리하는데(`tx_port.h` 134행 `TX_TIMER_THREAD_PRIORITY 0`), threshold 0이면 그 timer 스레드도 L을 선점하지 못한다. 만료 처리가 L이 끝날 때까지 밀렸다가 tick마다 나뉘어 처리된 것으로 추정하지만, timer 리스트 처리 순서까지는 소스로 따라가 확인하지 못했다. 교훈은 분명하다 — **threshold 0(전면 비선점)은 커널 자신의 timer 서비스까지 멈춘다.**

### 7.3 다른 RTOS에서는

FreeRTOS와 Zephyr에는 같은 이름의 기능이 없다. 비슷한 효과를 내는 방법:

- 비선점 그룹 전체를 **같은 우선순위**로 두고 slicing을 끄기 — 그룹 안에서는 선점이 없지만, FreeRTOS에서는 §4.3처럼 다른 이유로 순서가 바뀐다.
- 구간 동안 자기 우선순위를 threshold 값으로 올렸다가 내리기(`vTaskPrioritySet` / `k_thread_priority_set`) — 이것이 preemption-threshold의 "손으로 구현한" 판이다. 우선순위 상한 프로토콜(priority ceiling, D3)과 같은 모양이다.
- Zephyr에서 threshold를 "협력형 최상단"으로 주고 싶다면 협력형 우선순위가 바로 그것이고, threshold 0은 `k_sched_lock()`이다.

---

## 8. 우선순위 배정 입문 — 그리고 실수 두 가지를 실측

### 8.1 기본 휴리스틱

- **Rate Monotonic(RM)**: 주기가 짧을수록 높은 우선순위. A1 §6에서 같은 부하에 우선순위만 RM 순서로 바꾸자 최악 응답이 1136 µs(miss 75번) → 542 µs(miss 0)로 줄어드는 것을 이미 봤다. 이 노트의 일감도 H(4.87 ms) > M(10 ms) > 배경(주기 없음)으로 RM 순서다. 왜 RM이 고정 우선순위 중 최적인지, 이용률 한계(Liu & Layland)와 응답 시간 분석은 N2·N3가 맡는다.
- **Deadline Monotonic(DM)**: deadline이 주기보다 짧으면 주기 대신 deadline 순서.
- **우선순위 ≠ 중요도**: "가장 중요한" 태스크가 아니라 "가장 급한(deadline이 짧은)" 태스크가 위로 간다. 비행 안전에 중요한 failsafe 판단이 100 ms 주기라면 1 kHz rate loop보다 낮은 우선순위가 맞다.
- **ISR은 모든 태스크 위**: 태스크 우선순위로는 ISR을 못 막는다. 긴 ISR은 모든 태스크의 응답 시간에 그대로 더해진다(A1 §6, E 모듈).

### 8.2 예제 9 — 실수 1: 전부 같은 우선순위, 실수 2: 배경 태스크 하나만 조금 높게

무엇을 확인하는 코드인지: 같은 `main.c`를 (m1) H·M·A·B 전부 우선순위 1(slicing 켬) — "우선순위 고민하기 싫어서 다 같게 둔" 설계, (m2) A만 2로 — "이 배경 일이 좀 더 중요해 보여서" 한 칸 올린 설계로 빌드한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C4; R=/Users/donh/.rtos_scratch/C4/runf.sh
  $R m1 "-DPRIO_H=1 -DPRIO_M=1"; $R m2 "-DPRIO_A=2"
  python3 -I /Users/donh/.rtos_scratch/C4/analyze.py m1/out.txt m2/out.txt'
```

```text
m1 exit=0 lines=652
m2 exit=0 lines=477
[m1] preempt=1 slice=1 isr_yield=1 hog_yield=0 chunk=500 prio H/M/A/B=1/1/1/1
  H resp us min/avg/max   1211/  1887/  3081  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max   4509/  4607/  4724  (n=24)
  CPU% A=40.4  B=40.2  H=4.3  M=15.0 | switches=311 | hog chunks A=200 B=199 | max gap A=2.2ms B=2.2ms
[m2] preempt=1 slice=1 isr_yield=1 hog_yield=0 chunk=500 prio H/M/A/B=4/3/2/1
  H resp us min/avg/max    209/   210/   213  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max   1507/  1566/  1726  (n=24)
  CPU% A=80.5  H=4.3  M=15.2 | switches=145 | hog chunks A=401 B=0 | max gap A=1.7ms B=238.6ms
```

출력에서 볼 것:

- **m1**: CPU 점유(H 4.3%, M 15%)는 (a)와 똑같은데 H 응답이 210 → 평균 **1887**, 최악 **3081 µs**로 9~15배가 됐다. H는 round-robin 줄 맨 뒤에 서서 A·B·M의 1 ms slice를 기다린다. M은 1.5 ms 일을 slice 1 ms 단위로 쪼개 받아 4.5~4.7 ms. **"평균 CPU 사용률이 낮으니 괜찮다"는 판단이 틀리는 전형**이다. 4.87 ms 주기 H의 최악 3.1 ms는 이미 주기의 63%다 — 부하가 조금만 늘면 deadline miss.
- **m2**: H·M은 멀쩡하다. 대신 B는 238.6 ms 동안 **0번** 돌았다(조각 0개, CPU 0%). 쉬지 않는 태스크(CPU hog)가 한 칸이라도 위에 있으면 아래는 영원히 굶는다. 이것이 **기아(starvation)** 다. 고정 우선순위 스케줄러는 기아를 막아 주지 않는다 — 막는 것은 설계(높은 태스크는 반드시 block한다)와 감시(태스크별 watchdog 체크인, run-time stats)다.

```svg
<svg viewBox="0 0 680 248" xmlns="http://www.w3.org/2000/svg">
<text x="164" y="29" font-size="12" text-anchor="end">FR 선점 + slicing</text>
<rect x="170.0" y="17" width="20.9" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="190.9" y="17" width="73.0" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="227.3" y="30" font-size="12" text-anchor="middle">15%</text>
<rect x="263.8" y="17" width="188.6" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="358.1" y="30" font-size="12" text-anchor="middle">39%</text>
<rect x="452.4" y="17" width="197.6" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="551.2" y="30" font-size="12" text-anchor="middle">41%</text>
<text x="164" y="55" font-size="12" text-anchor="end">FR slicing 끔</text>
<rect x="170.0" y="43" width="20.9" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="190.9" y="43" width="73.0" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="227.3" y="56" font-size="12" text-anchor="middle">15%</text>
<rect x="263.8" y="43" width="193.0" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="360.3" y="56" font-size="12" text-anchor="middle">40%</text>
<rect x="456.8" y="43" width="193.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="553.4" y="56" font-size="12" text-anchor="middle">40%</text>
<text x="164" y="81" font-size="12" text-anchor="end">FR 협력형, yield 없음</text>
<rect x="170.0" y="69" width="480.0" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="410.0" y="82" font-size="12" text-anchor="middle">100%</text>
<text x="164" y="107" font-size="12" text-anchor="end">FR A를 B보다 높게</text>
<rect x="170.0" y="95" width="20.9" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="190.9" y="95" width="73.0" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="227.3" y="108" font-size="12" text-anchor="middle">15%</text>
<rect x="263.8" y="95" width="386.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="456.9" y="108" font-size="12" text-anchor="middle">80%</text>
<text x="164" y="133" font-size="12" text-anchor="end">Z 선점 + slice</text>
<rect x="170.0" y="121" width="22.2" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="192.2" y="121" width="74.8" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="229.6" y="134" font-size="12" text-anchor="middle">16%</text>
<rect x="267.0" y="121" width="196.2" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="365.1" y="134" font-size="12" text-anchor="middle">41%</text>
<rect x="463.2" y="121" width="186.8" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="556.6" y="134" font-size="12" text-anchor="middle">39%</text>
<text x="164" y="159" font-size="12" text-anchor="end">Z slicing 끔</text>
<rect x="170.0" y="147" width="20.5" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="190.5" y="147" width="73.2" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="227.1" y="160" font-size="12" text-anchor="middle">15%</text>
<rect x="263.7" y="147" width="386.3" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="456.9" y="160" font-size="12" text-anchor="middle">80%</text>
<text x="164" y="185" font-size="12" text-anchor="end">Z A가 sched_lock</text>
<rect x="170.0" y="173" width="22.2" height="18" fill="#d0564a" fill-opacity="0.75"/>
<rect x="192.2" y="173" width="74.7" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="229.6" y="186" font-size="12" text-anchor="middle">16%</text>
<rect x="267.0" y="173" width="250.1" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="392.0" y="186" font-size="12" text-anchor="middle">52%</text>
<rect x="517.0" y="173" width="133.0" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="583.5" y="186" font-size="12" text-anchor="middle">28%</text>
<rect x="170" y="208" width="14" height="10" fill="#d0564a" fill-opacity="0.75"/><text x="188" y="217" font-size="12">H</text>
<rect x="240" y="208" width="14" height="10" fill="#e08a3c" fill-opacity="0.75"/><text x="258" y="217" font-size="12">M</text>
<rect x="310" y="208" width="14" height="10" fill="#4a7bd0" fill-opacity="0.75"/><text x="328" y="217" font-size="12">A</text>
<rect x="380" y="208" width="14" height="10" fill="#3f9a6b" fill-opacity="0.75"/><text x="398" y="217" font-size="12">B</text>
<text x="650" y="238" font-size="12" text-anchor="end">CPU 점유 (H 릴리스 49주기 ≈ 238.6 ms 구간)</text>
</svg>
```
그림 9 — 정책·배치별 CPU 점유(측정, 238.6 ms 구간). 위에서 셋째 줄(FreeRTOS 협력형, hog가 양보 안 함)은 A가 100%를 가져 H·M까지 0%다. 넷째 줄(A 한 칸 위)과 여섯째 줄(Zephyr slicing 끔)은 B가 0%로 굶었다. 마지막 줄은 A의 `k_sched_lock()` 구간이 slicing을 막아 A 52% : B 28%로 기울었다.

### 8.3 실수 3 — 로깅 태스크를 높은 우선순위에

이 노트에서 따로 돌리지는 않았지만 m1·m2와 같은 원리다. "로그가 밀리면 안 되니까" 로깅 태스크를 제어 루프보다 위에 두면, 로그 한 줄 포맷(`snprintf`)과 UART 출력 시간이 제어 루프의 응답 시간에 통째로 더해진다. 115200 baud에서 80자 한 줄은 약 7 ms다(80 × 10 bit / 115200). 로깅은 **가장 낮은 축**에 두고, 큐가 넘치면 버리는(drop + 카운터) 정책을 쓴다. 제어 루프 쪽에서는 `printf`를 직접 부르지 않고 고정 크기 레코드를 큐에 넣기만 한다(M 모듈 trace·로깅).

---

## 9. 스케줄러 잠금 vs 크리티컬 섹션 — 응답 시간에 주는 영향

### 9.1 두 도구

| | 스케줄러 잠금 | 크리티컬 섹션 |
|---|---|---|
| FreeRTOS | `vTaskSuspendAll()` / `xTaskResumeAll()` | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` (Cortex-M3: BASEPRI = `configMAX_SYSCALL_INTERRUPT_PRIORITY`) |
| Zephyr | `k_sched_lock()` / `k_sched_unlock()` | `irq_lock()` / `irq_unlock()`, `k_spin_lock()` |
| 막는 것 | 태스크 전환만 | 태스크 전환 + (일정 우선순위 이하의) 인터럽트 |
| ISR은 | 그대로 돈다. 깨운 태스크는 잠금이 풀릴 때 돈다 | 미뤄진다 (마스크된 우선순위) |
| 안에서 block API | 금지 | 금지 |

구현(BASEPRI 방식, 중첩 카운트, `xPendingReadyList`, pended tick)은 C7이 맡는다. 여기서는 응답 시간만 잰다.

### 9.2 예제 10 — 같은 2 ms 구간을 세 가지로 감싸기

무엇을 확인하는 코드인지: B(우선순위 2)가 7 tick마다 2 ms짜리 "공유 자료 정리" 구간을 돈다. 그 구간을 (k0) 아무것도 안 감쌈 / (k1) `vTaskSuspendAll` / (k2) `taskENTER_CRITICAL`로 감싸고, H 응답 시간과 **TIMER0 ISR 진입 지연**(`RELOAD - VALUE`로 잰 lag)을 본다. `wl.h`는 §2.3 그대로다.

```c
/* C4 ex: 스케줄러 잠금 vs 크리티컬 섹션 — B(prio 2)가 7 ms마다 2 ms 동안 LOCK 구간을 돈다
 *   LOCK=0 아무것도 안 함, 1 vTaskSuspendAll/xTaskResumeAll, 2 taskENTER_CRITICAL/EXIT */
#define ISR_YIELD 1
#ifndef LOCK
#define LOCK 0
#endif
#define STR2(x) #x
#define STR(x) STR2(x)
#define C4_TAG "lock=" STR(LOCK) " (0 none, 1 suspendAll, 2 critical)"
#include "wl.h"

static void taskH(void *p) {
    (void)p;
    for (uint32_t k = 0;; k++) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        work_us(200);
        if (k < NREL) hdone[k] = C4_CYC();
    }
}
static void taskB(void *p) {                 /* "플래시 로그 블록 정리" 같은 2 ms짜리 구간 */
    (void)p;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(7));
#if LOCK == 1
        vTaskSuspendAll();
#elif LOCK == 2
        taskENTER_CRITICAL();
#endif
        work_us(2000);
#if LOCK == 1
        (void)xTaskResumeAll();
#elif LOCK == 2
        taskEXIT_CRITICAL();
#endif
        hogcnt[1]++;
    }
}
static void hog(void *p) { (void)p; for (;;) { work_us(500); hogcnt[0]++; } }
int main(void) {
    calibrate();
    xTaskCreate(taskH, "H", 256, NULL, 4, &hH);
    xTaskCreate(taskB, "B", 256, NULL, 2, NULL);
    xTaskCreate(hog, "A", 256, NULL, 1, NULL);
    timer0_start(4870);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C4; R=/Users/donh/.rtos_scratch/C4/runf.sh
  for l in 0 1 2; do $R k$l "-DLOCK=$l" lock.c; done
  python3 -I /Users/donh/.rtos_scratch/C4/analyze.py k0/out.txt k1/out.txt k2/out.txt
  for f in k0 k1 k2; do awk "/^H /{l=\$3/25; s+=l; if(l>m)m=l; n++} END{printf \"$f ISR lag avg %.1f max %.1f us\n\", s/n, m}" $f/out.txt; done
  for f in k0 k1 k2; do echo "$f tick records=$(grep -c "\^" $f/out.txt)"; done'
```

```text
k0 exit=0 lines=459
k1 exit=0 lines=513
k2 exit=0 lines=432
[k0] lock=0 (0 none, 1 suspendAll, 2 critical)
  H resp us min/avg/max    209/   210/   213  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max    (none)  (n=0)
  CPU% A=72.9  B=22.8  H=4.3 | switches=151 | hog chunks A=365 B=27 | max gap A=2.2ms B=7.1ms
[k1] lock=1 (0 none, 1 suspendAll, 2 critical)
  H resp us min/avg/max    209/   445/  2199  (n=50)   ISR lag max 0.3 us
  M resp us min/avg/max    (none)  (n=0)
  CPU% A=72.8  B=22.8  H=4.3 | switches=151 | hog chunks A=365 B=27 | max gap A=2.2ms B=7.1ms
[k2] lock=2 (0 none, 1 suspendAll, 2 critical)
  H resp us min/avg/max    209/   443/  2191  (n=50)   ISR lag max 1980.0 us
  M resp us min/avg/max    (none)  (n=0)
  CPU% A=72.9  B=22.8  H=4.4 | switches=151 | hog chunks A=365 B=27 | max gap A=2.2ms B=7.1ms
k0 ISR lag avg 0.3 max 0.3 us
k1 ISR lag avg 0.3 max 0.3 us
k2 ISR lag avg 233.0 max 1980.0 us
k0 tick records=248
k1 tick records=302
k2 tick records=221
```

출력에서 볼 것:

- **H 응답**: 잠금 없음 213 µs → 스케줄러 잠금 2199 µs, 크리티컬 섹션 2191 µs. 높은 태스크 입장에서는 **둘 다 "2 ms 동안 못 돈다"로 똑같다**. 스케줄러 잠금이 "가벼운 대안"이라는 말은 인터럽트 쪽 이야기지, 태스크 응답 시간 이야기가 아니다.
- **ISR 진입 지연**: 스케줄러 잠금에서는 0.3 µs 그대로, 크리티컬 섹션에서는 최대 **1980 µs**(평균 233 µs). 크리티컬 섹션은 BASEPRI로 TIMER0(우선순위 0x40, 마스크 대상)까지 막았다. ISR에서 하던 시간 민감한 일(타임스탬프 찍기, DMA 재장전)이 2 ms 늦는다.
- **tick 기록 수**(같은 248 ms 구간): k0 248개가 기준이다. k2는 **221개 — tick 27개가 사라졌다.** 2 ms 크리티컬 섹션 동안 SysTick이 두 번 만료되지만 예외 pending 비트는 하나뿐이라, 구간이 끝나면 tick 하나만 처리된다. 27번의 구간마다 1 tick씩, 커널 시계가 약 11% 느려졌다. 이것은 QEMU의 착시가 아니라 Cortex-M 예외 모델 그대로다(pending은 "횟수"가 아니라 "있다/없다"). k1의 302개는 반대로 많아 보이는데, `traceTASK_INCREMENT_TICK`이 잠금 중에도 불리고(그때는 `xPendedTicks`만 늘린다), `xTaskResumeAll()`이 밀린 tick을 다시 `xTaskIncrementTick()`으로 처리할 때 또 불리기 때문이다(27구간 × 2 tick = 54 → 248 + 54 = 302). 스케줄러 잠금은 tick을 **잃지 않고 미뤘다가 몰아서** 처리한다.

### 9.3 Zephyr — `k_sched_lock()`과 그것을 뚫는 meta-IRQ

예제 6의 SCEN 4는 A가 8조각마다 `k_sched_lock()`을 잡고 2 ms 일한다. SCEN 5는 같은 상황에서 H만 meta-IRQ로 둔다.

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/C4/runz.sh; $R z4 4; $R z5 5 metairq.conf
  cd /var/tmp/rtos/C4; python3 -I /Users/donh/.rtos_scratch/C4/analyze.py z4/out.txt z5/out.txt'
```

```text
z4 exit=0 lines=359
z5 exit=0 lines=339
[z4] zephyr SCEN=4 prio H/M/A/B=1/2/5/5
  H resp us min/avg/max    217/   319/  1704  (n=50)   ISR lag max 0.0 us
  M resp us min/avg/max   1537/  1726/  3300  (n=24)
  CPU% A=52.1  B=27.7  H=4.6  M=15.6 | switches=273 | hog chunks A=169 B=128 | max gap A=4.0ms B=9.0ms
[z5] zephyr SCEN=5 prio H/M/A/B=-16/2/5/5
  H resp us min/avg/max    213/   216/   226  (n=50)   ISR lag max 0.0 us
  M resp us min/avg/max   1541/  2010/  3576  (n=24)
  CPU% A=56.3  B=23.7  H=4.4  M=15.6 | switches=250 | hog chunks A=184 B=112 | max gap A=4.0ms B=11.0ms
```

출력에서 볼 것: `k_sched_lock()` 2 ms가 H 최악을 243 → 1704 µs로 늘렸다(구간과 H 릴리스가 겹친 위상에 따라 2 ms 이하). H를 meta-IRQ로 바꾸면 같은 잠금 구간을 뚫고 226 µs로 돌아왔다. M(일반 선점형)은 여전히 잠금에 막혀 최악 3.3~3.6 ms. 잠금 구간 동안 slicing도 멈춰 A가 52~56%로 B보다 많이 가져갔다.

(Zephyr 실행 출력에서 `FLASH:`/`RAM:` 빌드 크기 줄은 생략했다.)

### 9.4 규칙

- 잠금 구간 길이는 **모든 더 높은 태스크의 응답 시간에 그대로 더해진다**(blocking term, §5.4와 같은 항). 크리티컬 섹션은 거기에 더해 ISR 지연과 tick 손실까지 만든다.
- 그래서 순서는: 잠금이 필요 없는 설계(소유 태스크 하나 + 메시지) > 뮤텍스(관련 태스크만 막음, 우선순위 상속 D3) > 스케줄러 잠금(모든 태스크를 막음) > 크리티컬 섹션(인터럽트까지 막음, 수 µs 이하로만).
- 정말 긴급한 경로 하나만 지키면 되는 Zephyr 시스템이라면 meta-IRQ가 마지막 수단이다.

---

## 10. 측정 모음 — 한 장으로 비교

### 10.1 예제 11 — 차트 만들기

무엇을 확인하는 코드인지: 모든 시나리오의 `out.txt`를 같은 `stats()`로 읽어 H 응답 시간 막대와 CPU 점유 막대를 SVG로 그린다(좌표는 Python이 계산). 아래는 응답 시간 차트 부분이다(점유 차트도 같은 구조).

```python
# chart.py (일부) — python3 -I chart.py /var/tmp/rtos/C4 svg
from analyze import stats
ROWS = [("a", "FR 선점 + slicing"), ("b", "FR 선점, slicing 끔"), ("c1", "FR 협력형 + ISR yield"),
        ("c2", "FR 협력형, 조각 0.5 ms"), ("c3", "FR 협력형, 조각 3 ms"), ("m1", "FR 전부 같은 우선순위"),
        ("k1", "FR suspendAll 2 ms"), ("k2", "FR critical 2 ms"),
        ("z1", "Z 선점 + slice 1 ms"), ("z1n", "Z slicing 끔"), ("z2", "Z 협력형(음수)"),
        ("z3", "Z 협력형 + H meta-IRQ"), ("z4", "Z k_sched_lock 2 ms"), ("z5", "Z sched_lock + meta-IRQ")]
def resp_chart():
    data = []
    for k, lab in ROWS:
        _, H, *_ = stats(f"{D}/{k}/out.txt"); data.append((lab, sum(H) / len(H), max(H)))
        print("%-26s avg %6.0f  max %6.0f" % (lab, data[-1][1], data[-1][2]))
    X0, X1, RH, Y0, VMAX = 205, 650, 22, 14, 3500.0
    X = lambda v: X0 + v * (X1 - X0) / VMAX            # us → 픽셀
    for i, (lab, av, mx) in enumerate(data):
        y = Y0 + i * RH                                  # 한 줄 22 px: 최악은 넓은 주황, 평균은 얇은 파랑
        ...
```

```text
FR 선점 + slicing            avg    210  max    213
FR 선점, slicing 끔           avg    210  max    213
FR 협력형 + ISR yield         avg    210  max    213
FR 협력형, 조각 0.5 ms          avg    520  max   1716
FR 협력형, 조각 3 ms            avg   1641  max   3146
FR 전부 같은 우선순위              avg   1887  max   3081
FR suspendAll 2 ms         avg    445  max   2199
FR critical 2 ms           avg    443  max   2191
Z 선점 + slice 1 ms          avg    228  max    243
Z slicing 끔                avg    211  max    221
Z 협력형(음수)                  avg    525  max   1595
Z 협력형 + H meta-IRQ         avg    214  max    226
Z k_sched_lock 2 ms        avg    319  max   1704
Z sched_lock + meta-IRQ    avg    216  max    226
FR 선점 + slicing        H=4.3 M=15.2 A=39.3 B=41.2 I=0.0 sw=327
FR slicing 끔           H=4.3 M=15.2 A=40.2 B=40.2 I=0.0 sw=145
FR 협력형, yield 없음       H=0.0 M=0.0 A=100.0 B=0.0 I=0.0 sw=0
FR A를 B보다 높게           H=4.3 M=15.2 A=80.5 B=0.0 I=0.0 sw=145
Z 선점 + slice           H=4.6 M=15.6 A=40.9 B=38.9 I=0.0 sw=323
Z slicing 끔            H=4.3 M=15.2 A=80.5 B=0.0 I=0.0 sw=144
Z A가 sched_lock        H=4.6 M=15.6 A=52.1 B=27.7 I=0.0 sw=273
```

(한글 폭 때문에 텍스트 열이 어긋나 보이는 것은 터미널 출력 그대로다.)

```svg
<svg viewBox="0 0 680 372" xmlns="http://www.w3.org/2000/svg">
<line x1="205.0" y1="10" x2="205.0" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="205.0" y="338" font-size="12" text-anchor="middle">0</text>
<line x1="268.6" y1="10" x2="268.6" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="268.6" y="338" font-size="12" text-anchor="middle">500</text>
<line x1="332.1" y1="10" x2="332.1" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="332.1" y="338" font-size="12" text-anchor="middle">1000</text>
<line x1="395.7" y1="10" x2="395.7" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="395.7" y="338" font-size="12" text-anchor="middle">1500</text>
<line x1="459.3" y1="10" x2="459.3" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="459.3" y="338" font-size="12" text-anchor="middle">2000</text>
<line x1="522.9" y1="10" x2="522.9" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="522.9" y="338" font-size="12" text-anchor="middle">2500</text>
<line x1="586.4" y1="10" x2="586.4" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="586.4" y="338" font-size="12" text-anchor="middle">3000</text>
<line x1="650.0" y1="10" x2="650.0" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
<text x="650.0" y="338" font-size="12" text-anchor="middle">3500</text>
<text x="199" y="28" font-size="12" text-anchor="end">FR 선점 + slicing</text>
<rect x="205" y="17" width="27.1" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="21" width="26.7" height="6" fill="#4a7bd0"/>
<text x="236.1" y="28" font-size="12">213</text>
<text x="199" y="50" font-size="12" text-anchor="end">FR 선점, slicing 끔</text>
<rect x="205" y="39" width="27.1" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="43" width="26.7" height="6" fill="#4a7bd0"/>
<text x="236.1" y="50" font-size="12">213</text>
<text x="199" y="72" font-size="12" text-anchor="end">FR 협력형 + ISR yield</text>
<rect x="205" y="61" width="27.0" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="65" width="26.7" height="6" fill="#4a7bd0"/>
<text x="236.0" y="72" font-size="12">213</text>
<text x="199" y="94" font-size="12" text-anchor="end">FR 협력형, 조각 0.5 ms</text>
<rect x="205" y="83" width="218.2" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="87" width="66.1" height="6" fill="#4a7bd0"/>
<text x="427.2" y="94" font-size="12">1716</text>
<text x="199" y="116" font-size="12" text-anchor="end">FR 협력형, 조각 3 ms</text>
<rect x="205" y="105" width="400.0" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="109" width="208.7" height="6" fill="#4a7bd0"/>
<text x="609.0" y="116" font-size="12">3146</text>
<text x="199" y="138" font-size="12" text-anchor="end">FR 전부 같은 우선순위</text>
<rect x="205" y="127" width="391.7" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="131" width="240.0" height="6" fill="#4a7bd0"/>
<text x="600.7" y="138" font-size="12">3081</text>
<text x="199" y="160" font-size="12" text-anchor="end">FR suspendAll 2 ms</text>
<rect x="205" y="149" width="279.6" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="153" width="56.5" height="6" fill="#4a7bd0"/>
<text x="488.6" y="160" font-size="12">2199</text>
<text x="199" y="182" font-size="12" text-anchor="end">FR critical 2 ms</text>
<rect x="205" y="171" width="278.6" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="175" width="56.3" height="6" fill="#4a7bd0"/>
<text x="487.6" y="182" font-size="12">2191</text>
<text x="199" y="204" font-size="12" text-anchor="end">Z 선점 + slice 1 ms</text>
<rect x="205" y="193" width="30.9" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="197" width="28.9" height="6" fill="#4a7bd0"/>
<text x="239.9" y="204" font-size="12">243</text>
<text x="199" y="226" font-size="12" text-anchor="end">Z slicing 끔</text>
<rect x="205" y="215" width="28.0" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="219" width="26.9" height="6" fill="#4a7bd0"/>
<text x="237.0" y="226" font-size="12">221</text>
<text x="199" y="248" font-size="12" text-anchor="end">Z 협력형(음수)</text>
<rect x="205" y="237" width="202.8" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="241" width="66.7" height="6" fill="#4a7bd0"/>
<text x="411.8" y="248" font-size="12">1595</text>
<text x="199" y="270" font-size="12" text-anchor="end">Z 협력형 + H meta-IRQ</text>
<rect x="205" y="259" width="28.7" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="263" width="27.2" height="6" fill="#4a7bd0"/>
<text x="237.7" y="270" font-size="12">226</text>
<text x="199" y="292" font-size="12" text-anchor="end">Z k_sched_lock 2 ms</text>
<rect x="205" y="281" width="216.6" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="285" width="40.5" height="6" fill="#4a7bd0"/>
<text x="425.6" y="292" font-size="12">1704</text>
<text x="199" y="314" font-size="12" text-anchor="end">Z sched_lock + meta-IRQ</text>
<rect x="205" y="303" width="28.7" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="205" y="307" width="27.5" height="6" fill="#4a7bd0"/>
<text x="237.7" y="314" font-size="12">226</text>
<line x1="205" y1="322" x2="650" y2="322" stroke="currentColor"/>
<text x="650" y="356" font-size="12" text-anchor="end">H 응답 시간 (us, 릴리스 → 일 끝)</text>
<rect x="15" y="347" width="14" height="10" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="33" y="356" font-size="12">최악(max)</text>
<rect x="105" y="349" width="14" height="6" fill="#4a7bd0"/><text x="123" y="356" font-size="12">평균</text>
</svg>
```
그림 10 — H(최고 우선순위, 일 200 µs) 응답 시간의 평균(파랑)과 최악(주황 막대), 50 릴리스, QEMU icount. FR = FreeRTOS V11.3.2, Z = Zephyr v4.4.2. 선점형 계열은 모두 210~245 µs에 모이고, 협력형·잠금·잘못된 배치가 최악을 1.6~3.1 ms로 늘린다. meta-IRQ와 FreeRTOS 협력형의 ISR yield는 그 긴 막대를 다시 짧게 만든다.

### 10.2 결과 표

| 시나리오 | H 최악 (µs) | M 최악 (µs) | switch 수 | A / B 점유 (%) | 무엇을 보여 주나 |
|---|---|---|---|---|---|
| FR (a) 선점 + slicing | 213 | 1726 | 327 | 39.3 / 41.2 | 기준. 위는 아래와 무관 |
| FR (b) slicing 끔 | 213 | 1726 | 145 | 40.2 / 40.2 | switch 절반. 그래도 교대(pxIndex) |
| FR (c0) 협력형, 양보 없음 | 못 돎 | 못 돎 | 0 | 100 / 0 | 무한 루프 = 시스템 정지 |
| FR (c1) 협력형 + ISR yield | 213 | 1970 | 502 | 40.4 / 40.1 | ISR이 협력형을 뚫는다 |
| FR (c2) 협력형, 조각 0.5 ms | 1716 | 1873 | 454 | 40.2 / 40.3 | 최악 = 가장 긴 비양보 구간(M) |
| FR (c3) 협력형, 조각 3 ms | 3146 | 5704 | 137 | 40.3 / 40.1 | switch는 최소, 응답은 최악 |
| FR (m1) 전부 같은 우선순위 | 3081 | 4724 | 311 | 40.4 / 40.2 | 평균 부하 같아도 응답 15배 |
| FR (m2) A가 한 칸 위 | 213 | 1726 | 145 | 80.5 / 0 | 기아 |
| FR (k1) suspendAll 2 ms | 2199 | — | 151 | — | 태스크 응답은 크리티컬과 같다 |
| FR (k2) critical 2 ms | 2191 | — | 151 | — | + ISR 지연 1980 µs, tick 27개 손실 |
| Z (z1) 선점 + slice 1 ms | 243 | 1782 | 323 | 40.9 / 38.9 | 선점당한 스레드가 돌아온다 |
| Z (z1n) slicing 끔 | 221 | 1731 | 144 | 80.5 / 0 | FreeRTOS와 달리 B 기아 |
| Z (z2) 전부 협력형 | 1595 | 2231 | 450 | 40.3 / 40.1 | FR c2와 같은 모양 |
| Z (z3) 협력형 + H meta-IRQ | 226 | 2247 | 497 | 40.0 / 40.4 | 협력형을 뚫는 긴급 스레드 |
| Z (z4) k_sched_lock 2 ms | 1704 | 3300 | 273 | 52.1 / 27.7 | 잠금 = 잠깐 협력형 |
| Z (z5) sched_lock + meta-IRQ | 226 | 3576 | 250 | 56.3 / 23.7 | meta-IRQ는 잠금도 뚫는다 |

---

## 11. RTOS 커널 안에서는 무슨 일이 일어나나

### 11.1 예제 12 — 선점 결정이 나는 곳을 소스에서 찾기

무엇을 확인하는 코드인지: 이 노트에서 이름을 부른 함수·매크로가 V11.3.2 / v4.4.2 소스의 몇 행에 있는지 grep으로 확인한다(행 번호는 이 버전 기준).

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel
grep -n "^BaseType_t xTaskIncrementTick\|^    void vTaskSwitchContext( void )\|^#define prvAddTaskToReadyList\|#define taskYIELD_ANY_CORE_IF_USING_PREEMPTION\|traceTASK_SWITCHED_IN();" tasks.c | head -8
grep -n "define queueYIELD_IF_USING_PREEMPTION" queue.c
grep -n "portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT\|^void xPortPendSVHandler\|bl vTaskSwitchContext" portable/GCC/ARM_CM3/port.c
cd /var/rtoslab/zephyrproject/zephyr
grep -n "^static ALWAYS_INLINE bool should_preempt" kernel/include/kthread.h
grep -n "^static ALWAYS_INLINE struct k_thread \*next_up\|^static ALWAYS_INLINE void update_cache\|^void z_reschedule(\|^void k_sched_lock\|^void z_impl_k_yield" kernel/sched.c
grep -n "^void z_reset_time_slice\|^void z_time_slice" kernel/timeslicing.c
grep -n "SCB_ICSR_PENDSVSET_Msk" arch/arm/core/cortex_m/exc_exit.c'
```

```text
73:    #define taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB )
88:        #define taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB ) \
106:        #define taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB )     prvYieldForTask( pxTCB )
287:#define prvAddTaskToReadyList( pxTCB )                                                                     \
3809:        traceTASK_SWITCHED_IN();
4831:BaseType_t xTaskIncrementTick( void )
5215:    void vTaskSwitchContext( void )
5274:            traceTASK_SWITCHED_IN();
89:    #define queueYIELD_IF_USING_PREEMPTION()
92:        #define queueYIELD_IF_USING_PREEMPTION()    portYIELD_WITHIN_API()
94:        #define queueYIELD_IF_USING_PREEMPTION()    vTaskYieldWithinAPI()
123:void xPortPendSVHandler( void ) __attribute__( ( naked ) );
455:void xPortPendSVHandler( void )
473:        "   bl vTaskSwitchContext               \n"
507:            portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
224:static ALWAYS_INLINE bool should_preempt(const struct k_thread *thread,
38:static ALWAYS_INLINE void update_cache(int preempt_ok);
185:static ALWAYS_INLINE struct k_thread *next_up(void)
294:static ALWAYS_INLINE void update_cache(int preempt_ok)
799:void z_reschedule(struct k_spinlock *lock, k_spinlock_key_t key)
819:void k_sched_lock(void)
1150:void z_impl_k_yield(void)
75:void z_reset_time_slice(struct k_thread *thread)
131:void z_time_slice(void)
59:		SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
```

출력에서 볼 것: `taskYIELD_ANY_CORE_IF_USING_PREEMPTION`과 `queueYIELD_IF_USING_PREEMPTION`이 각각 **세 번** 정의된다 — 협력형(빈 매크로), 단일 코어 선점형, SMP 선점형. 협력형 스위치는 결국 "이 매크로를 비워 두기"다. `traceTASK_SWITCHED_IN()`이 두 곳(3809행은 스케줄러 시작 시 첫 태스크, 5274행은 단일 코어 `vTaskSwitchContext`)에 있다. port.c 507행이 tick 경로의 PendSV, 473행이 PendSV 핸들러 안에서 `vTaskSwitchContext`를 부르는 곳이다.

### 11.2 FreeRTOS — 세 경로가 한 함수로 모인다

- **tick 경로**: `xPortSysTickHandler()`(port.c 491행~) → `xTaskIncrementTick()`(tasks.c 4831행). 이 함수가 pdTRUE를 돌려주는 경우는 세 가지다: delay 만료로 깨운 태스크가 현재보다 높을 때(4932행 블록), slicing(4967행 블록), ISR이 남긴 `xYieldPendings[0]`(5015행 블록). 셋 다 `configUSE_PREEMPTION == 1`일 때만 컴파일된다. pdTRUE면 port.c 507행이 PendSV를 건다.
- **API 경로**: `xQueueGenericSend()`가 기다리던 태스크를 `xTaskRemoveFromEventList()`로 깨우고, 그 반환값(깨운 태스크가 더 높은가)이 참이면 `queueYIELD_IF_USING_PREEMPTION()` → `portYIELD_WITHIN_API()` = `portYIELD()` = PendSV 세트(portmacro.h 85행). `xTaskCreate`·`vTaskPrioritySet`·`vTaskResume`은 `prvAddTaskToReadyList()`(287행, 리스트의 **끝**에 넣는다) 뒤에 `taskYIELD_ANY_CORE_IF_USING_PREEMPTION()`(88행: "현재 우선순위 < 새 태스크 우선순위면 yield")을 부른다.
- **ISR 경로**: FromISR API는 PendSV를 직접 걸지 않는다. `*pxHigherPriorityTaskWoken`에 표시만 하고(그리고 `xYieldPendings[0]`), 실제 PendSV는 애플리케이션이 ISR 끝에서 `portYIELD_FROM_ISR()`로 건다. 안 걸면 다음 tick에서 `xYieldPendings` 검사로 늦게라도 바뀐다(선점형일 때).
- **선택**: PendSV 핸들러(`xPortPendSVHandler`, port.c 455행)가 현재 문맥을 저장하고 `vTaskSwitchContext()`(5215행)를 부른다. 스케줄러가 잠겨 있으면(`uxSchedulerSuspended != 0`) `xYieldPendings[0] = pdTRUE`만 남기고 그냥 나온다 — §9의 k1에서 H가 잠금 해제까지 기다린 이유다. 아니면 `taskSELECT_HIGHEST_PRIORITY_TASK()`가 CLZ로 최고 우선순위를 찾고(C1) `listGET_OWNER_OF_NEXT_ENTRY()`로 그 리스트의 다음 차례를 고른다(§3.4·§4.3의 주인공).

### 11.3 Zephyr — "캐시"에 미리 정해 두고, 나갈 때 비교한다

- 스레드가 ready가 되면 `ready_thread()`(sched.c 348행) → `queue_thread()` → `update_cache(0)`(294행). `update_cache`가 `next_up()`(185행)으로 최선 후보를 구하고 `should_preempt()`가 허락하면 `_kernel.ready_q.cache`에 적는다. 즉 Zephyr는 **"다음에 돌 스레드"를 상태가 바뀔 때마다 미리 계산해 둔다**.
- ISR 경로: Cortex-M에서 ISR 끝의 `z_arm_int_exit()`(exc_exit.c 57~60행)는 `ready_q.cache != current`면 PendSV를 건다 — 비교 한 번이다.
- 스레드 경로: `k_sem_give()` 같은 API는 끝에서 `z_reschedule()`(799행) → `need_swap()`(cache와 current 비교) → `z_swap()`.
- slicing: tick(정확히는 timeout 처리, `kernel/timeout.c`)에서 `z_time_slice()`(timeslicing.c 131행)가 만료를 보고 `move_current_to_end_of_prio_q()`.
- 협력형·잠금·meta-IRQ의 차이는 전부 `should_preempt()` 한 함수 안의 조건이다(§6.2).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13">FreeRTOS V11.3.2</text>
<rect x="10" y="30" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="55" font-size="12" text-anchor="middle">xTaskIncrementTick()</text>
<rect x="10" y="80" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="105" font-size="12" text-anchor="middle">xQueueSend → queueYIELD</text>
<rect x="10" y="130" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="155" font-size="12" text-anchor="middle">portYIELD_FROM_ISR(woken)</text>
<rect x="250" y="80" width="150" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="325" y="105" font-size="12" text-anchor="middle">ICSR.PENDSVSET</text>
<rect x="440" y="30" width="230" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="55" font-size="12" text-anchor="middle">xPortPendSVHandler</text>
<rect x="440" y="80" width="230" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="105" font-size="12" text-anchor="middle">vTaskSwitchContext</text>
<rect x="440" y="130" width="230" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="148" font-size="12" text-anchor="middle">SELECT_HIGHEST_PRIORITY</text>
<text x="555" y="164" font-size="12" text-anchor="middle">+ GET_OWNER_OF_NEXT_ENTRY</text>
<line x1="210" y1="50" x2="250" y2="92" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="210" y1="100" x2="248" y2="100" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="210" y1="150" x2="250" y2="110" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="400" y1="95" x2="440" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="555" y1="70" x2="555" y2="80" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="555" y1="120" x2="555" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="10" y1="186" x2="670" y2="186" stroke="currentColor" stroke-opacity="0.3" stroke-dasharray="4,4"/>
<text x="10" y="208" font-size="13">Zephyr v4.4.2</text>
<rect x="10" y="218" width="200" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="110" y="243" font-size="12" text-anchor="middle">ready_thread / k_yield</text>
<rect x="10" y="268" width="200" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="110" y="293" font-size="12" text-anchor="middle">z_time_slice (timeout)</text>
<rect x="250" y="218" width="190" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="345" y="236" font-size="12" text-anchor="middle">update_cache</text>
<text x="345" y="252" font-size="12" text-anchor="middle">next_up + should_preempt</text>
<rect x="250" y="268" width="190" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="345" y="293" font-size="12" text-anchor="middle">ready_q.cache</text>
<rect x="460" y="218" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="236" font-size="12" text-anchor="middle">스레드: z_reschedule</text>
<text x="565" y="252" font-size="12" text-anchor="middle">cache != current → z_swap</text>
<rect x="460" y="268" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="286" font-size="12" text-anchor="middle">ISR: z_arm_int_exit</text>
<text x="565" y="302" font-size="12" text-anchor="middle">cache != current → PendSV</text>
<line x1="210" y1="238" x2="248" y2="238" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="210" y1="288" x2="250" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="345" y1="258" x2="345" y2="268" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="440" y1="288" x2="460" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="440" y1="290" x2="458" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="10" y="336" font-size="12">FreeRTOS는 PendSV 안에서 고르고, Zephyr는 상태가 바뀔 때 미리 골라 둔 뒤 나갈 때 비교만 한다</text>
</svg>
```
그림 11 — 선점 결정의 경로. FreeRTOS(위)는 여러 입구가 PendSV 하나로 모이고 선택은 `vTaskSwitchContext()`에서, Zephyr(아래)는 `update_cache()`가 `ready_q.cache`에 다음 스레드를 미리 적어 두고 스레드·ISR 출구에서 비교한다.

더 깊은 내용은 C1(ready list·비트맵·CLZ), C2(PendSV 어셈블리), C5(tick·timeout), C7(크리티컬 섹션·잠금 구현), C12(소스 투어)로 이어진다.

---

## 12. 실무에서 다시 보기

### 12.1 드론 비행 제어기 — 우선순위 표가 곧 설계 문서

예를 들어 STM32H7급 FC에 IMU 8 kHz, rate loop 1~2 kHz, attitude 250 Hz, 항법 50 Hz, 텔레메트리, 로깅이 있다면:

| 우선순위 (FreeRTOS, 큰 수 = 높음) | 무엇 | 깨우는 것 | 정책 메모 |
|---|---|---|---|
| ISR | IMU DMA 완료, ESC DShot 타이밍 | 하드웨어 | 태스크 우선순위 위. 짧게 |
| 6 | rate loop | IMU ISR → task notify + `portYIELD_FROM_ISR` | 혼자 쓰는 우선순위, 절대 block API를 길게 쓰지 않음 |
| 5 | attitude / mixer | rate loop가 notify | |
| 4 | 수신기·failsafe | UART idle ISR | |
| 3 | 항법·GNSS 파싱 | 큐 | |
| 2 | 텔레메트리 | 주기 | |
| 1 | 로깅 · 파라미터 저장 | 큐 | 같은 우선순위 slicing 허용. 플래시 지우기 같은 긴 일은 조각내기 |

시간 제약 있는 태스크는 전부 다른 우선순위에 두고(§3.4), 같은 우선순위 공유는 맨 아래 배경 축에서만 한다. "가끔 rate loop가 1 ms 늦는다"면 이 노트의 trace 훅을 그대로 붙여 간트 차트를 그리고, rate loop 릴리스 직전에 누가 CPU를 쥐고 있었는지(잠금? 같은 우선순위? ISR?)부터 본다.

### 12.2 로봇 관절 모터 컨트롤러

FOC 전류 루프(20 kHz)는 태스크가 아니라 ADC/PWM ISR이다 — 스케줄러 정책 밖이다. 그 위의 속도·위치 루프(1 kHz)와 CAN 명령 처리를 태스크로 둔다. 여기서 흔한 함정은 CAN 수신 처리 태스크를 "명령이 늦으면 안 된다"며 속도 루프보다 높게 두는 것이다. 버스에 명령이 몰리면 속도 루프가 굶는다(§8.2 m2와 같은 기아). CAN은 ISR에서 링버퍼에 넣고 속도 루프보다 낮은 태스크가 처리하게 한다.

### 12.3 Zephyr 센서 허브 — 협력형과 meta-IRQ의 자리

Zephyr의 시스템 workqueue는 기본 우선순위가 -1(협력형, `kernel/Kconfig` 609행 `SYSTEM_WORKQUEUE_PRIORITY`)이고, 여러 드라이버·서브시스템이 일을 그 workqueue로 넘긴다. 애플리케이션의 선점형 스레드는 그 아래다. 타임스탬프를 찍어 Linux SoC로 보내는 경로가 workqueue 일 때문에 밀린다면, (1) 그 경로를 ISR에서 끝내거나, (2) 전용 협력형 스레드에 높은 우선순위를 주거나, (3) 드라이버 하반부라면 meta-IRQ를 검토한다. Zephyr 기본 `TIMESLICE_SIZE`가 20 ms라는 것도 기억한다 — 같은 우선순위 선점형 스레드 둘이면 20 ms씩 번갈아 돈다.

### 12.4 Don의 SSD 펌웨어 — 협력형에서 선점형으로 옮길 때

SSD executive는 협력형이라 "공유 자료구조는 잠금 없이 써도 된다"는 암묵적 가정이 코드 곳곳에 있다. 이 코드를 선점형 RTOS 태스크 여러 개로 나누면 그 가정이 전부 깨진다. 안전한 이전 순서: (1) 처음에는 모든 executive 일을 **한 태스크**에 넣는다(=협력형 그대로), (2) 시간 제약이 분명한 일(호스트 명령 응답)만 높은 우선순위 태스크로 떼어 내고 그 경계의 데이터만 큐로 바꾼다, (3) FreeRTOS를 협력형(`configUSE_PREEMPTION 0`)으로 두고 시작한다면 ISR에서 `portYIELD_FROM_ISR`을 쓰는 순간 §5.3의 하이브리드가 된다는 것을 팀 규칙으로 적어 둔다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 시간 제약 태스크 여러 개를 같은 우선순위에 | 평균 부하 낮은데 가끔 수 ms 늦음 (m1: 210 → 3081 µs) | round-robin 줄에서 다른 태스크 slice를 기다림 | 서로 다른 우선순위, RM/DM 순서 |
| 쉬지 않는(block 없는) 태스크를 위쪽에 | 아래 태스크가 영원히 안 돎 (m2: B 0%) | 고정 우선순위는 기아를 막지 않음 | 높은 태스크는 반드시 block, 배경 계산은 맨 아래 |
| slicing을 끄면 같은 우선순위가 돌아가며 돈다고 가정 | Zephyr로 옮기자 한 스레드가 굶음 (z1n) | FreeRTOS `pxIndex` 전진 vs Zephyr 선점 스레드 맨 앞 유지 | 같은 우선순위끼리 순서에 기대지 않기, 필요하면 명시적 yield |
| FreeRTOS 협력형에서 ISR이 `portYIELD_FROM_ISR` 호출 | 잠금 없이 쓰던 공유 변수가 가끔 깨짐 | 협력형도 ISR yield는 전환함 (§5.3) | 협력형이면 ISR yield 금지, 또는 하이브리드를 명시적으로 설계 |
| 협력형에서 긴 일을 조각내지 않음 | 높은 태스크 응답 = 가장 긴 비양보 구간 (c3: 3146 µs) | blocking term | 상태 머신으로 조각, 조각마다 yield |
| 응답 시간 개선하려고 크리티컬 섹션 대신 스케줄러 잠금 | 태스크 응답은 그대로 2 ms 늦음 (k1) | 두 도구 모두 태스크 전환을 막음 | 잠금 구간 자체를 줄이기, 뮤텍스 |
| 수 ms 크리티컬 섹션 | ISR 2 ms 지연, 커널 시계가 느려짐 (k2: tick 27개 손실) | BASEPRI 마스크 + SysTick pending은 1개 | 크리티컬 섹션은 µs 단위로만 |
| 로깅 태스크를 제어 루프 위에 | 로그 많을 때 제어 지터 | UART 출력 시간이 그대로 선점 | 로깅은 최하위, 큐 넘치면 drop |
| Zephyr에서 main()이 협력형 스레드를 만들다 멈춤 | 일부 스레드가 안 생김 | 협력형은 만들어지는 즉시 main(0)을 선점 | main을 먼저 높게, 또는 `K_FOREVER`로 만들고 나중에 `k_thread_start` |
| Zephyr 기본 slice(20 ms)를 FreeRTOS(1 tick)로 생각 | 같은 우선순위 스레드 지연이 20 ms | 기본값 차이 | `CONFIG_TIMESLICE_SIZE` 확인 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "What does fixed-priority preemptive scheduling guarantee, and when exactly does preemption happen in FreeRTOS?"

**A.** 규칙은 "ready 중 최고 우선순위가 항상 돈다". 검사 시점은 tick, ISR 종료(애플리케이션이 `portYIELD_FROM_ISR`), 더 높은 태스크를 ready로 만드는 API 호출, 그리고 block·yield. 모두 Cortex-M에서는 PendSV 요청으로 끝나고 실제 선택은 `vTaskSwitchContext`에서 한다. 실험에서 최고 우선순위 태스크 응답이 아래 hog와 무관하게 209~213 µs였다.

> It guarantees that whenever the kernel makes a scheduling decision, the highest-priority ready task gets the CPU, so a high-priority task's response time is bounded independently of lower-priority work, apart from interrupts and critical sections. In FreeRTOS on Cortex-M the decision points are the tick, the end of an ISR that called portYIELD_FROM_ISR, any API call that readies a higher-priority task — xQueueSend, xTaskCreate, vTaskPrioritySet — and the running task blocking or yielding. All of them just pend PendSV; the actual selection happens in vTaskSwitchContext. In a QEMU experiment the top task's response stayed at 209 to 213 microseconds while two CPU hogs ran below it.

**꼬리 질문**: "Does xQueueSend return before the woken task runs?" → 선점형에서는 아니다. 깨운 태스크가 더 높으면 send 안에서 PendSV가 걸려 그 태스크가 먼저 돈다(예제 1).

**Q.** "How do two tasks at the same priority share the CPU in FreeRTOS? Is it fair?"

**A.** time slicing이 켜져 있으면(기본) 매 tick마다 같은 우선순위의 다음 태스크로 바뀐다. quantum은 1 tick 고정. 하지만 선점 뒤에는 공평하지 않다: 선점당한 태스크는 남은 slice를 돌려받지 못하고 다음 차례로 넘어가며, slice는 tick 경계에서 끝나므로 중간에 받은 태스크는 짧은 조각만 받는다. 측정에서 66번 중 0번 돌려받았고 조각의 41%가 0.9 ms 미만. 장기 점유는 거의 반반.

> With configUSE_TIME_SLICING on, which is the default, the tick handler requests a switch whenever another task of the running task's priority is ready, so the quantum is exactly one tick. Long-term it's roughly fair, but short-term it isn't: the ready list is walked by an index that advances every time the scheduler selects, so after a higher-priority task preempts and blocks, the following equal-priority task runs, not the one that was interrupted, and slices always end on a tick boundary. In my trace the preempted task got the CPU back zero times out of 66, and about 40 percent of slices were partial. So I never put tasks with timing requirements on a shared priority.

**Q.** "What's the difference between cooperative and preemptive scheduling, and when would you choose cooperative?"

**A.** 협력형은 태스크가 block·yield할 때만 다시 고른다. 장점은 switch가 적고 공유 자료에 잠금이 거의 필요 없다는 것, 단점은 높은 태스크의 응답 시간 = 아래 태스크의 가장 긴 비양보 구간. 측정: 0.5 ms 조각이면 최악 1.7 ms(실제로는 M의 1.5 ms 구간이 지배), 3 ms 조각이면 3.1 ms, 양보 안 하는 태스크 하나면 시스템 정지. 작은 시스템이나 레거시 run-to-completion 코드에 쓴다. FreeRTOS 협력형에서도 ISR의 portYIELD_FROM_ISR은 전환한다는 함정.

> Cooperative means the scheduler only switches when the running task blocks or yields; preemptive also switches the moment a higher-priority task becomes ready. Cooperative gives fewer context switches and almost no locking between tasks, but every task's worst-case latency includes the longest non-yielding section of any lower-priority task — I measured 1.7 milliseconds with 0.5 millisecond chunks, because a 1.5 millisecond job dominated, and 3.1 with 3 millisecond chunks; one task that never yields stops the whole system. I'd use it for small systems or when porting run-to-completion code like an SSD executive. One FreeRTOS gotcha: even with configUSE_PREEMPTION set to 0, an ISR that calls portYIELD_FROM_ISR still switches, because the kernel only disables its own yield macros.

**Q.** "Zephyr has negative priorities. What are they, and what is a meta-IRQ thread?"

**A.** 음수는 협력형 — 다른 스레드에게 선점당하지 않고 모든 선점형보다 높다. 내부적으로 prio와 sched_locked를 한 16비트로 겹쳐 협력형 = 스케줄러 잠금 상태와 같은 비교로 처리. meta-IRQ는 가장 높은 N개 우선순위로, 협력형과 `k_sched_lock`까지 선점하고 끝나면 선점당한 협력형 스레드로 돌아간다. 드라이버 하반부 용도. 측정: 협력형 hog들 사이에서 H 최악 1595 → meta-IRQ로 226 µs.

> Negative priorities are cooperative: such a thread is never preempted by another thread, only by interrupts, and it ranks above every preemptible thread. Internally Zephyr overlays the priority and the scheduler-lock count in one 16-bit field, so "cooperative" and "scheduler locked" are the same comparison. Meta-IRQ priorities are the top N priorities, enabled with CONFIG_NUM_METAIRQ_PRIORITIES; a ready meta-IRQ thread preempts even cooperative threads and k_sched_lock sections, and the kernel then returns to the exact cooperative thread it interrupted. It's meant for interrupt bottom halves. In my test, with cooperative background threads, the urgent thread's worst case dropped from about 1.6 milliseconds to 226 microseconds once it was made meta-IRQ.

**Q.** "What is preemption-threshold in ThreadX and why would you use it?"

**A.** 실행 중일 때만 적용되는 두 번째 우선순위. threshold보다 높은 스레드만 선점 가능. 관련 스레드들을 서로 비선점 그룹으로 묶어 switch와 잠금을 줄이면서 최상위 긴급 스레드 응답은 지킨다. 실험: L(20, threshold 10) 실행 중 M(15)은 기다리고 H(5)는 선점. threshold 0은 커널 timer 스레드까지 막는다.

> It's a second number per thread that only applies while the thread is running: only threads with a priority higher than the threshold can preempt it. That lets you make a group of related threads non-preemptive among themselves — fewer context switches, less locking — while the truly urgent threads above the threshold still preempt. In a run on Cortex-M3, a priority-20 thread with threshold 10 was preempted by a priority-5 thread but not by a priority-15 one. FreeRTOS and Zephyr don't have it; the closest equivalents are temporarily raising the priority, which is essentially a priority ceiling, or a scheduler lock, which is a threshold of zero.

**Q.** "Scheduler lock versus critical section — which one hurts real-time behaviour less?"

**A.** 인터럽트 쪽에서는 스케줄러 잠금이 낫다(ISR 지연 0.3 µs vs 최대 1980 µs, 크리티컬은 tick까지 잃었다). 하지만 높은 태스크 응답은 둘 다 똑같이 2.2 ms로 늘었다. 그래서 "더 가벼운 잠금"이 아니라 "구간을 줄여라"가 답.

> For interrupts, the scheduler lock is clearly better: in my measurement ISR entry latency stayed at 0.3 microseconds with vTaskSuspendAll, but went up to almost 2 milliseconds with a 2 millisecond critical section, and the kernel even lost one tick per section because SysTick can only be pending once. But for the highest-priority task both were identical — about 2.2 milliseconds worst case instead of 213 microseconds — because both prevent the context switch. So the real fix is shortening the section or using a mutex that only blocks the tasks that share the data.

**Q.** "A high-priority control task occasionally runs a millisecond late. How do you debug it?"

**A.** 추측 말고 trace. traceTASK_SWITCHED_IN 훅으로 switch 시각을 RAM에 남기고, 릴리스 시각(타이머 하드웨어에서 역산)과 같이 간트 차트로 본다. 늦은 job 직전에 누가 CPU를 쥐었는지에 따라: 같은 우선순위 → 우선순위 분리, 낮은 태스크 → 잠금 구간 찾기, ISR → ISR 길이, 아무도 아님 → 깨우는 경로(ISR yield 누락).

> I'd instrument before guessing: a traceTASK_SWITCHED_IN hook that writes a timestamp and task ID into a RAM buffer, plus the true release time, which I back-calculate from the timer's reload and current value so a delayed ISR doesn't hide the lateness. Then I draw a Gantt chart around the late jobs. If an equal-priority task was running, I separate priorities; if a lower-priority task held the CPU, I look for scheduler locks, critical sections or a cooperative section; if an ISR was running, I measure the ISR; and if nothing was running, the wake-up path is wrong — typically a missing portYIELD_FROM_ISR, so the switch waits for the next tick.

---

## 15. 직접 해보기

1. **개념** — FreeRTOS에서 우선순위 2의 태스크 X·Y와 우선순위 3의 Z가 있다. X가 돌다가 Z가 깨어나 1 ms 돌고 block했다. slicing 켬/끔 각각에서 다음에 누가 도나? Zephyr라면?
   정답/힌트: FreeRTOS는 둘 다 Y(`listGET_OWNER_OF_NEXT_ENTRY`가 `pxIndex`를 전진). Zephyr는 둘 다 X(선점당한 스레드가 큐 맨 앞 유지). §3.4 표.
2. **코드** — 예제 3의 일감에 `configUSE_TIME_SLICING=1`인 채로 hog 조각마다 `taskYIELD()`를 넣으면 switch 수와 max gap이 어떻게 변할지 예측하고 `-DHOG_YIELD=1`로 돌려 확인하라.
   정답/힌트: 조각(0.5 ms)마다 교대하므로 switch가 늘고(협력형 c1·c2의 450~500 근처를 예상), slice 끝 tick 교대와 섞인다. 예측 후 trace로 확인.
3. **측정** — `configIDLE_SHOULD_YIELD`를 0과 1로 두고 우선순위 0(idle과 같은)에 CPU hog 하나를 둬서 hog의 CPU 점유를 재라. trace에 idle(`I`)이 얼마나 끼어드나?
   정답/힌트: 1이면 idle이 매 바퀴 즉시 양보해 hog가 대부분을 가진다. 0이면 tick마다 idle과 hog가 slice를 나눈다(대략 반씩 예상). §3.5의 `prvIdleTask` 5946행.
4. **코드** — Zephyr 예제에서 `CONFIG_TIMESLICE_SIZE`를 1, 5, 20으로 바꿔 hog의 평균 조각 길이와 switch 수를 그래프로 그려라. `CONFIG_TIMESLICE_PRIORITY=6`이면 우선순위 5인 hog는 어떻게 되나?
   정답/힌트: slice가 커질수록 조각이 길고 switch가 준다. PRIORITY=6이면 5는 "6보다 높은" 우선순위라 slicing에서 제외 → z1n처럼 B 기아.
5. **개념** — 협력형 시스템에서 태스크 셋(우선순위 순) H: C=0.2 ms, M: C=1.5 ms(비양보), A·B: 3 ms 조각. H의 최악 응답을 계산하고 c3 측정(3146 µs)과 비교하라.
   정답/힌트: blocking ≈ max(1.5, 3.0) = 3.0 ms + 자기 0.2 ms + 커널 오버헤드 → 약 3.2 ms. 측정 3.15 ms. N3의 비선점 RTA로 정식화.
6. **측정** — 예제 10에서 크리티컬 섹션 길이를 0.5, 1, 1.5, 2, 3 ms로 바꿔 "잃은 tick 수"를 표로 만들어라. 1 ms 미만에서는 왜 0인가?
   정답/힌트: 구간 동안 SysTick 만료가 2번 이상이어야 하나를 잃는다. 1 ms 미만이면 만료는 최대 1번이라 pending으로 보존된다.
7. **ThreadX** — 예제 8에서 L이 일하는 도중 `tx_thread_preemption_change(&tl, 20, &old)`로 threshold를 풀면 M이 언제 도는지 확인하라.
   정답/힌트: threshold를 낮추는 순간 ThreadX가 선점 검사를 해서 대기 중인 M이 즉시 돈다(예상). `tx_thread_preemption_change.c` 확인.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| fixed-priority preemptive | 고정 우선순위 선점 | ready 중 최고 우선순위가 항상 돌고, 우선순위는 앱이 정한다 |
| preemption point | 선점 지점 | 커널이 다시 고르는 순간: tick, ISR 종료, API 호출, block/yield |
| time slicing / round-robin | 시분할 | 같은 우선순위끼리 quantum마다 교대 |
| quantum (time slice) | 한 번에 받는 시간 | FreeRTOS는 1 tick 고정, Zephyr는 `CONFIG_TIMESLICE_SIZE`(기본 20 ms) |
| cooperative scheduling | 협력형 | block·yield할 때만 다시 고름 |
| `configUSE_PREEMPTION` | FreeRTOS 선점 스위치 | 0이면 커널 자신의 yield 매크로가 빈 매크로가 됨 |
| `configIDLE_SHOULD_YIELD` | idle 양보 | 우선순위 0에 다른 태스크가 있으면 idle이 즉시 yield |
| `pxIndex` | ready 리스트의 "다음 차례" | 선택할 때마다 전진 — FreeRTOS 같은 우선순위 순환의 정체 |
| cooperative priority (Zephyr) | 음수 우선순위 | 다른 스레드에게 선점당하지 않음, 모든 선점형보다 높음 |
| meta-IRQ | 협력형도 선점하는 최상위 우선순위 | 드라이버 하반부용, 끝나면 선점당한 협력형 스레드로 복귀 |
| `k_sched_lock` | Zephyr 스케줄러 잠금 | 현재 스레드를 잠시 협력형으로 만듦 |
| `vTaskSuspendAll` | FreeRTOS 스케줄러 잠금 | 전환만 막고 ISR은 허용, tick은 미뤘다가 몰아서 처리 |
| preemption-threshold | ThreadX 선점 문턱 | 실행 중에는 threshold보다 높은 스레드만 선점 가능 |
| starvation | 기아 | 높은 쪽이 쉬지 않아 낮은 태스크가 영원히 못 도는 것 |
| blocking term | 블로킹 항 | 낮은 태스크의 비선점 구간 때문에 높은 태스크가 기다리는 시간 |
| response time | 응답 시간 | 릴리스부터 job 완료까지 |
| Rate Monotonic (RM) | 주기 단조 배치 | 주기가 짧을수록 높은 우선순위 |
| PendSV | 미뤄 둔 문맥 교체 예외 | 가장 낮은 우선순위 예외, 모든 선점 경로의 종착점 |

---

## 17. 요약 & 체크리스트

고정 우선순위 선점은 "ready 중 최고 우선순위가 돈다"는 규칙 하나이고, 정책 설정은 "같은 우선순위끼리 어떻게"와 "언제 다시 고르나"를 바꾼다. 같은 4-태스크 일감을 QEMU icount 위에서 돌린 결과, 선점형 계열에서 최고 우선순위 태스크 응답은 정책과 무관하게 209~245 µs였고, 협력형·잠금·잘못된 배치는 최악을 1.6~3.1 ms로 늘렸다. 같은 이름의 "round-robin"도 FreeRTOS(고를 때마다 다음 차례, 선점당한 태스크는 차례를 잃음)와 Zephyr(선점당한 스레드가 맨 앞 유지, slicing을 끄면 기아)가 반대로 동작했다. FreeRTOS 협력형은 ISR의 `portYIELD_FROM_ISR`을 막지 않고, Zephyr meta-IRQ는 협력형과 `k_sched_lock`을 뚫으며, ThreadX preemption-threshold는 "누가 나를 선점할 수 있나"를 스레드마다 정한다. 스케줄러 잠금과 크리티컬 섹션은 높은 태스크 응답을 똑같이 늘리고, 크리티컬 섹션은 ISR 지연과 tick 손실까지 만든다.

- [ ] "ready 중 최고 우선순위가 항상 돈다"를 말하고, 선점 지점 네 가지를 FreeRTOS 함수·매크로 이름과 함께 댈 수 있다
- [ ] `xQueueSend()` 안에서 높은 태스크가 먼저 도는 이유를 `queueYIELD_IF_USING_PREEMPTION` → PendSV로 설명할 수 있다
- [ ] FreeRTOS time slice가 1 tick 고정이고, 선점 뒤 차례를 잃는 이유(`listGET_OWNER_OF_NEXT_ENTRY`)를 안다
- [ ] slicing을 꺼도 FreeRTOS는 교대하지만 Zephyr는 기아가 나는 것을 측정으로 설명할 수 있다
- [ ] `configIDLE_SHOULD_YIELD`가 무엇을 하는지 안다
- [ ] 협력형의 최악 응답 = 가장 긴 비양보 구간 + 자기 일, 을 계산할 수 있다
- [ ] FreeRTOS 협력형에서 ISR yield가 전환을 일으키는 소스 근거를 댈 수 있다
- [ ] Zephyr 음수 우선순위, `preempt` union, meta-IRQ 보장과 용도를 설명할 수 있다
- [ ] Zephyr `CONFIG_TIMESLICING` / `TIMESLICE_SIZE`(기본 20 ms) / `TIMESLICE_PRIORITY`를 설정할 수 있다
- [ ] ThreadX preemption-threshold를 정의하고 threshold 0의 부작용을 안다
- [ ] 스케줄러 잠금 vs 크리티컬 섹션의 차이를 응답 시간·ISR 지연·tick 손실 숫자로 말할 수 있다
- [ ] `traceTASK_SWITCHED_IN` 훅으로 trace를 남기고 간트 차트로 늦은 job의 원인을 찾을 수 있다

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (`/var/rtoslab/src/FreeRTOS-Kernel`, https://github.com/FreeRTOS/FreeRTOS-Kernel): `tasks.c` — `taskYIELD_ANY_CORE_IF_USING_PREEMPTION`(68~110행), `taskSELECT_HIGHEST_PRIORITY_TASK`(197·238행), `prvAddTaskToReadyList`(287행), `xTaskIncrementTick`(4831행), `vTaskSwitchContext`(5215행), `prvIdleTask`(5908행), `vTaskGenericNotifyGiveFromISR`(8315행) · `queue.c` `queueYIELD_IF_USING_PREEMPTION`(89~94행) · `include/list.h` `listGET_OWNER_OF_NEXT_ENTRY`(286행) · `include/FreeRTOS.h`(기본값과 `configUSE_TASK_PREEMPTION_DISABLE` 제약 2909행) · `portable/GCC/ARM_CM3/port.c`·`portmacro.h` · `History.txt`(V11.3.2, 2026-10-16)
- FreeRTOS 문서(https://www.freertos.org): "Customization"(`FreeRTOSConfig.h`) 페이지의 `configUSE_PREEMPTION`, `configUSE_TIME_SLICING`, `configIDLE_SHOULD_YIELD` 항목, "Trace Hook Macros" 페이지 (정확한 하위 URL은 사이트 개편이 잦아 적지 않는다)
- Zephyr v4.4.2 소스 (`/var/rtoslab/zephyrproject/zephyr`): `kernel/Kconfig`(`NUM_COOP_PRIORITIES`, `NUM_PREEMPT_PRIORITIES`, `NUM_METAIRQ_PRIORITIES`, `TIMESLICING`·`TIMESLICE_SIZE`·`TIMESLICE_PRIORITY`) · `kernel/sched.c`(`update_metairq_preempt`, `next_up`, `update_cache`, `ready_thread`, `z_reschedule`, `k_sched_lock`, `z_impl_k_yield`) · `kernel/include/kthread.h`(`thread_is_preemptible`, `thread_is_metairq`, `should_preempt`) · `kernel/timeslicing.c` · `include/zephyr/kernel/thread.h`(`preempt` union) · `arch/arm/core/cortex_m/exc_exit.c` · `subsys/tracing/user/tracing_user.c`
- Zephyr 문서: Scheduling — https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html · Threads(우선순위, 협력형/선점형, meta-IRQ) — https://docs.zephyrproject.org/latest/kernel/services/threads/index.html
- Eclipse ThreadX 6.5.2 (커밋 `56c8695`, https://github.com/eclipse-threadx/threadx): `common/src/tx_thread_create.c`, `tx_thread_system_resume.c`, `tx_thread_preemption_change.c`, `ports/cortex_m3/gnu/inc/tx_port.h`
- 이 모듈·다른 모듈 노트: A1(RM 배치 실측), A2(실행 모델), A5(우선순위 방향, ThreadX 포팅), C1·C2·C5·C7·C11·C12, D3, N2·N3, EL F1(Linux 스케줄링 클래스)
