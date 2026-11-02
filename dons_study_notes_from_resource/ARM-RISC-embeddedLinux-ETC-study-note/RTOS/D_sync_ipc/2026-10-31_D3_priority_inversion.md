# D3. 우선순위 역전 — 높은 태스크가 낮은 태스크를 기다리는 동안 중간 태스크가 끼어드는 문제: 재현 · PIP · OCPP/ICPP · Mars Pathfinder · FreeRTOS/Zephyr 구현의 한계

> **이 노트를 다 읽으면**: 유한(bounded) 역전과 무한(unbounded) 역전을 L/M/H 세 태스크로 설명하고, QEMU에서 직접 잰 간트 차트로 "M이 1 ms 더 돌면 H도 1 ms 더 늦는다"를 보여 줄 수 있다 · PIP · 전이적 상속 · OCPP · ICPP의 규칙과 성질(블로킹 1회, 데드락 방지, chained blocking)을 구분하고 B_i 상한을 계산할 수 있다 · FreeRTOS `xTaskPriorityInherit`/`xTaskPriorityDisinherit`와 Zephyr `adjust_owner_prio`가 실제로 무엇을 하고 무엇을 하지 않는지(중첩 mutex, 3단 사슬, 타임아웃, ceiling 상한)를 소스와 실측으로 말할 수 있다 · Mars Pathfinder 사건을 사실대로 이야기하고 "그럼 mutex 쓰면 끝인가?"에 답할 수 있다
> **왜 필요한가**: RTOS 면접 1순위 질문이다("Explain priority inversion and how to fix it", "PIP vs PCP", "Mars Pathfinder"). 실무에서는 "비행 제어 루프가 몇 시간에 한 번 수 ms 늦는다"는 버그의 대표 원인이고, trace 없이는 거의 못 찾는다
> **Don 기준 난이도**: 락 · 선점 · 우선순위는 SSD 펌웨어에서 이미 안다 / 새로 배울 것은 "중간 태스크가 왜 문제인가"를 시간축으로 보는 법, 상속 · ceiling 프로토콜의 정확한 규칙과 증명된 성질, 그리고 실제 RTOS 구현이 교과서보다 단순화된 지점(전이적 상속 없음, 중첩 락 처리)
> **선행 노트**: A1(실시간 정의), A4(태스크 상태), C4(고정 우선순위 선점, time slicing), C7(크리티컬 섹션), D1(세마포어), D2(뮤텍스 API — 이 노트는 API 기초를 다시 설명하지 않는다). Linux 쪽 `PTHREAD_PRIO_INHERIT` · `rt_mutex` 실측은 EL N1 §3.4 · §7, PREEMPT_RT의 rt_mutex 변환은 EL F4 · EL N1. 이어지는 노트: N3(응답 시간 분석), N4(블로킹 분석 증명), M3(이벤트 트레이싱), M7(버그 패턴), I9(RTIC의 SRP)

---

## 0. 큰 그림 — "중간 태스크"가 진짜 범인이다

Don의 SSD 펌웨어를 떠올려 보자. 호스트 명령 처리(급함)가 FTL 매핑 테이블을 고치려는데, 마침 GC(급하지 않음)가 그 테이블 락을 쥐고 있다. 협력형 executive라면 GC가 자기 단계를 끝내고 락을 놓을 때까지 기다리면 된다. 기다림의 길이는 "GC의 남은 단계 길이"로 **정해져 있다**. 이게 **유한 역전(bounded inversion)** 이다. 락을 쓰는 한 피할 수 없고, 설계로 짧게 만드는 대상이다.

RTOS의 선점이 들어오면 새 문제가 생긴다. GC가 락을 쥔 채로 **중간 우선순위의 태스크(예: SMART 로그 갱신)** 에게 밀려나면, 호스트 명령은 "GC의 남은 구간 + SMART 로그 작업 전부"를 기다린다. 중간 태스크는 락과 아무 관계가 없는데도 가장 급한 일을 늦춘다. 중간 태스크가 여러 개거나 오래 돌면 기다림에 **상한이 없다**. 이게 **무한 역전(unbounded inversion)** 이고, 이 노트가 다루는 "우선순위 역전 문제"의 본체다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerUnits="userSpaceOnUse" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="30" y="20" width="230" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="42" y="40" font-size="13">H: 자세 추정기 (prio 3)</text>
<text x="42" y="59" font-size="12">SPI 락 요청 → blocked</text>
<rect x="30" y="125" width="230" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="42" y="145" font-size="13">M: 텔레메트리 (prio 2)</text>
<text x="42" y="164" font-size="12">락과 무관, CPU만 쓴다</text>
<rect x="30" y="230" width="230" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="42" y="250" font-size="13">L: SD 로거 (prio 1)</text>
<text x="42" y="269" font-size="12">락을 쥔 채 ready로 밀려남</text>
<rect x="430" y="125" width="220" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="442" y="145" font-size="13">SPI 버스 mutex</text>
<text x="442" y="164" font-size="12">owner = L</text>
<line x1="260" y1="50" x2="428" y2="135" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="330" y="78" font-size="12">① 기다린다</text>
<line x1="430" y1="168" x2="262" y2="248" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="352" y="228" font-size="12">② L이 쥐고 있다</text>
<line x1="145" y1="176" x2="145" y2="228" stroke="#e08a3c" stroke-width="2" marker-end="url(#ah1)"/>
<text x="155" y="207" font-size="12">③ M이 L을 선점</text>
<text x="30" y="306" font-size="12">H가 기다리는 시간 = ② L의 남은 임계 구역(유한) + ③ M이 도는 시간 전부(상한 없음)</text>
</svg>
```
그림 1 — 우선순위 역전의 해부. ②만 있으면 유한 역전(락의 본질적 비용), ③이 끼면 무한 역전. 모든 해결책은 ③을 막거나(상속 · ceiling) ②를 줄이는(임계 구역 단축) 것이다.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| 1 | 정의: 유한 vs 무한 역전, 실험대 | 공통 하네스 |
| 2 | 무한 역전 재현 (binary semaphore를 락으로) | 예제 1 |
| 3 | FreeRTOS mutex의 priority inheritance | 예제 2 |
| 4 | Zephyr `k_mutex`, `CONFIG_PRIORITY_CEILING` | 예제 3 |
| 5 | Mars Pathfinder 1997 | — |
| 6 | 프로토콜 이론: PIP · 전이적 상속 · OCPP · ICPP · B_i | 예제 10 |
| 7 | ICPP 직접 구현과 비용 | 예제 4, 예제 9 |
| 8 | 구현의 한계 1: 중첩 mutex와 타임아웃 | 예제 5, 예제 6 |
| 9 | 구현의 한계 2: 3단 사슬(전이적 상속) | 예제 7 |
| 10 | 락이 아닌 역전: 큐 · ISR · 스케줄러 잠금 · DMA | 예제 8 |
| 11 | 커널 안: `tasks.c` · `queue.c` · `kernel/mutex.c` | 예제 11 |
| 12 | 실무: 드론 FC · 모터 · 센서 허브 · SSD | — |
| 13~ | 실수 표, 면접, 연습, 용어, 체크리스트 | 예제 12 (trace → 간트) |

### 실습 환경 한 줄

Lima VM `elinux` 안의 **QEMU `mps2-an385`(Cortex-M3 @ 25 MHz)** 에서 **FreeRTOS V11.3.2(ARM_CM3 포트, 1 kHz tick)** 와 **Zephyr v4.4.2(같은 보드, 1 kHz tick, `CONFIG_TIMESLICING=n`)** 를 돌린다. 타이밍은 전부 **`-icount shift=5`(명령 1개 = 32 ns 가상 시간, 실행할 때마다 같은 숫자)** 기준이다. **QEMU icount 기준이므로 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 모델링하지 않는다). 하지만 이 노트의 결론은 절대값이 아니라 **모양**(기울기 1인 직선 vs 평평한 선, 누가 언제 CPU를 쥐었나)이라서 그대로 실칩에 옮겨진다. 시각은 MPS2 CMSDK TIMER1을 25 MHz 자유 카운터로 읽는다(1 cycle = 40 ns). 소스는 Mac `~/.rtos_scratch/D3/`, 빌드 산출물은 VM `/tmp/rtos_d3/`(작성 당일 VM 루트 파일시스템이 ext4 오류로 읽기 전용이 되어 tmpfs를 썼다).

---

## 1. 정의 — 무엇이 "역전"이고 무엇이 "문제"인가

### 1.1 세 태스크 이야기

고정 우선순위 선점 스케줄러(C4)의 약속은 하나다: **ready 중 가장 높은 우선순위가 돈다**. 우선순위 역전은 이 약속이 겉으로는 지켜지는데 결과적으로 깨지는 상황이다.

1. L(낮음)이 락 S를 잡고 임계 구역에 들어간다.
2. H(높음)가 깨어나 L을 선점한다. H도 S가 필요해서 `take`를 부르지만 L이 쥐고 있으니 **blocked**가 된다. 여기까지는 정상이다. H는 L이 S를 놓을 때까지 기다려야 한다.
3. L이 다시 돈다(ready 중 최고). 그런데 M(중간)이 깨어난다. M > L이므로 M이 L을 선점한다. 스케줄러 입장에서는 완벽히 규칙대로다. H는 blocked라 후보가 아니다.
4. M이 도는 동안 L은 S를 놓지 못하고, H는 계속 기다린다. **H보다 낮은 M이 사실상 H보다 먼저 돈다.**

### 1.2 정의

- **우선순위 역전(priority inversion)**: 높은 우선순위 태스크가 낮은 우선순위 태스크 때문에 실행하지 못하는 상태 전체. 락을 공유하는 이상 **0으로 만들 수 없다**.
- **유한 역전(bounded)**: H가 기다리는 시간이 "L이 쥔 임계 구역의 남은 길이"로 묶인 경우. 설계자가 임계 구역을 짧게 해서 통제한다.
- **무한 역전(unbounded)**: H의 기다림에 **H와 자원을 공유하지 않는 태스크(M)의 실행 시간**이 더해지는 경우. M의 수와 길이에 상한이 없으니 H의 응답 시간에도 상한이 없다. 면접에서 "priority inversion problem"이라고 할 때는 보통 이것을 가리킨다.
- **블로킹 시간 B_i**: 태스크 i가 자기보다 낮은 태스크 때문에 기다리는 최악 시간. 응답 시간 분석(N3)의 식 `R_i = C_i + B_i + Σ ⌈R_i / T_j⌉ × C_j`에 들어가는 그 B다. 우선순위 역전 대책의 목표는 **B_i에 작은 상한을 만드는 것**이다.

> 한 줄로: 역전 자체는 락의 비용이고, **문제는 "상한이 사라지는 것"** 이다. 해결책이 "역전을 없앤다"가 아니라 "역전을 묶는다(bound)"인 이유다.

### 1.3 공통 실험대 — 이벤트를 RAM에 적고, 끝나고 출력한다

측정 중에 UART로 출력하면 그 자체가 수백 us를 먹어 타이밍을 망친다. 그래서 FreeRTOS의 trace 훅(C4에서 쓴 방식)으로 **context switch와 우선순위 변화**를 RAM 배열에 시각과 함께 적고, 실험이 끝난 뒤 한꺼번에 출력한다. Linux 쪽에서 같은 실험을 `ftrace`로 한 것이 EL N1 §7이고, 여기서는 MCU에서 직접 만든다.

무엇을 확인하는 코드인지: 앱 폴더의 `FreeRTOSConfig.h`가 템플릿 설정을 그대로 포함하면서 trace 매크로 네 개(switch-in, 상속, 상속 해제, `vTaskPrioritySet`)와 tick 시각 기록을 덧붙인다.

```c
/* D3: 템플릿 config + trace 훅 (switch-in · 상속 · 복귀를 RAM 버퍼에) */
#ifndef D3_CONFIG_H
#define D3_CONFIG_H
#include <stdint.h>
#define D3_CYC()  (0xFFFFFFFFu - *(volatile uint32_t *)0x40001004u)   /* TIMER1, 25 MHz */
#define D3_N      2048
extern volatile uint32_t d3_t[D3_N], d3_i;
extern volatile char     d3_e[D3_N], d3_n[D3_N];
extern volatile uint8_t  d3_p[D3_N];
extern volatile uint32_t d3_tk[256];                    /* tick k가 실제로 온 시각 */
#define D3_REC(ev, nm, pr) do { uint32_t i_ = d3_i; if (i_ < D3_N) { d3_t[i_] = D3_CYC(); \
        d3_e[i_] = (ev); d3_n[i_] = (nm); d3_p[i_] = (uint8_t)(pr); d3_i = i_ + 1; } } while (0)
#define traceTASK_SWITCHED_IN()             D3_REC('S', pxCurrentTCB->pcTaskName[0], pxCurrentTCB->uxPriority)
#define traceTASK_PRIORITY_INHERIT(t, p)    D3_REC('+', (t)->pcTaskName[0], (p))
#define traceTASK_PRIORITY_DISINHERIT(t, p) D3_REC('-', (t)->pcTaskName[0], (p))
#ifndef D3_NO_PRIOSET                     /* 예제 9는 이 훅 없이 잰다 */
#define traceTASK_PRIORITY_SET(t, p)       D3_REC('=', (t)->pcTaskName[0], (p))
#endif
#define traceTASK_INCREMENT_TICK(x) do { uint32_t k_ = (x) + 1u; if (k_ < 256u && d3_tk[k_] == 0u) d3_tk[k_] = D3_CYC(); } while (0)
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#endif
```

`traceTASK_PRIORITY_INHERIT`/`traceTASK_PRIORITY_DISINHERIT`는 `tasks.c`의 `xTaskPriorityInherit()`·`xTaskPriorityDisinherit()` 안에서 불리는 공식 훅이다(§11). 상속이 언제 일어났는지를 커널이 직접 알려 준다.

무엇을 확인하는 코드인지: 모든 FreeRTOS 예제가 쓰는 공통 헤더. `work_us()`는 **CPU를 쥐고 있는 동안만** 줄어드는 바쁜 루프라서, 선점당하면 그만큼 끝나는 시각이 밀린다. 실제 계산 부하와 같은 성질이다.

이후 예제의 출력 블록에는 결과 줄만 옮긴다. trace 덤프(`T`로 시작하는 수십~수백 줄), `END`/`[board_exit]` 줄, Zephyr 부팅 배너는 생략했다(trace는 필요한 구간만 따로 보여 준다).

```c
/* D3 공통: 보정된 CPU 일(work_us), 이벤트 기록, 덤프 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"
volatile uint32_t d3_t[D3_N], d3_i; volatile char d3_e[D3_N], d3_n[D3_N]; volatile uint8_t d3_p[D3_N]; volatile uint32_t d3_tk[256];
static uint32_t lp100;                                   /* 100 us(2500 cycle) 동안 도는 루프 수 */
static inline void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }
static inline void work_us(uint32_t us) { spin(us * lp100 / 100u); }   /* CPU를 쥔 시간만 센다 */
static inline void calibrate(void) {
    uint32_t t0 = D3_CYC(); spin(100000); uint32_t c = D3_CYC() - t0;
    lp100 = (uint32_t)(100000ull * 2500u / c);
}
#define MARK(ev) D3_REC((ev), pcTaskGetName(NULL)[0], uxTaskPriorityGet(NULL))
static inline void dump_trace(void) {
    uint32_t n = d3_i; d3_i = D3_N;                      /* 기록 정지 */
    for (uint32_t i = 0; i < n; i++)
        board_printf_raw("T %u %c %c %u\n", d3_t[i], d3_e[i], d3_n[i], d3_p[i]);
}
```

---

## 2. 무한 역전 재현 — binary semaphore를 락으로 쓰면

### 2.1 실험 설계

| 태스크 | 우선순위 | 라운드마다 하는 일 |
|---|---|---|
| L (로거) | 1 | t0에 깨어 락을 잡고 3 ms 일, 놓는다 |
| H (추정기) | 3 | t0 + 1 ms에 깨어 락을 요청, 잡으면 0.2 ms 일 |
| M (텔레메트리) | 2 | t0 + 2 ms에 깨어 W ms 일 (락 없음) |

W를 0, 1, 2, 4, 6, 8, 10, 12 ms로 바꿔 가며 8 라운드(라운드 간격 25 ms)를 돈다. H의 **응답 시간(릴리스 tick → 일 끝)** 을 잰다. 락은 컴파일 옵션 `LOCK`으로 바꾼다: 0 = binary semaphore(D1에서 "상호배제에 쓰지 말라"고 한 그것), 1 = mutex, 2 = ICPP(§7), 3 = `vTaskSuspendAll`(§10).

### 2.2 예제 1 — binary semaphore: H의 대기가 M의 일만큼 늘어난다

무엇을 확인하는 코드인지: 락 종류만 바꿔 가며 같은 L/M/H 일감을 돌리고, 라운드마다 H의 블로킹 · 응답 시간과 M의 응답 시간을 기록한다.

```c
/* 예제 1·2·4: L/M/H 역전 실험. LOCK = 0 binary sem · 1 mutex(PIP) · 2 ICPP(ceiling으로 올림) · 3 vTaskSuspendAll
 *   L(1): 라운드 t0에 락 잡고 3 ms 일 · H(3): t0+1 ms에 락 요청, 0.2 ms 일 · M(2): t0+2 ms에 W ms 일(락 없음) */
#include "d3.h"
enum { PL = 1, PM = 2, PH = 3, CEIL = PH, PERIOD = 25, START = 10 };
static const uint16_t Wms[] = { 0, 1, 2, 4, 6, 8, 10, 12 };
#define NR (sizeof Wms / sizeof Wms[0])
static SemaphoreHandle_t lk;
static uint32_t req[NR], acq[NR], hend[NR], mend[NR], hrel[NR], mrel[NR];
static UBaseType_t lock(void) {
    UBaseType_t saved = uxTaskPriorityGet(NULL);
    if (LOCK == 2) vTaskPrioritySet(NULL, CEIL);              /* ICPP: 잡기 "전에" ceiling으로 */
    if (LOCK == 3) vTaskSuspendAll(); else xSemaphoreTake(lk, portMAX_DELAY);
    return saved;
}
static void unlock(UBaseType_t saved) {
    if (LOCK == 3) (void)xTaskResumeAll(); else xSemaphoreGive(lk);
    if (LOCK == 2) vTaskPrioritySet(NULL, saved);             /* 놓은 "뒤에" 원래대로 */
}
static void tL(void *p) {
    (void)p; TickType_t last = 0;
    for (uint32_t r = 0; r < NR; r++) {
        xTaskDelayUntil(&last, r ? PERIOD : START);
        UBaseType_t s = lock(); MARK('L'); work_us(3000); MARK('U'); unlock(s);
    }
    vTaskDelay(PERIOD);
    board_printf_raw("CFG LOCK=%d\n", LOCK);
    for (uint32_t r = 0; r < NR; r++)
        board_printf_raw("R W=%u h_block_us=%u h_resp_us=%u m_resp_us=%u t0=%u\n", Wms[r], (acq[r] - req[r]) / 25u,
            (hend[r] - d3_tk[hrel[r]]) / 25u, (mend[r] - d3_tk[mrel[r]]) / 25u, d3_tk[hrel[r] - 1]);
    dump_trace(); board_printf_raw("END\n"); board_exit(0);
}
static void tH(void *p) {
    (void)p; TickType_t last = 0;
    for (uint32_t r = 0; r < NR; r++) {
        xTaskDelayUntil(&last, r ? PERIOD : START + 1); hrel[r] = last;
        req[r] = D3_CYC(); MARK('q');
        UBaseType_t s = lock(); acq[r] = D3_CYC(); MARK('a'); work_us(200); unlock(s);
        hend[r] = D3_CYC();
    }
    vTaskSuspend(NULL);
}
static void tM(void *p) {
    (void)p; TickType_t last = 0;
    for (uint32_t r = 0; r < NR; r++) {
        xTaskDelayUntil(&last, r ? PERIOD : START + 2); mrel[r] = last;
        MARK('m'); work_us(Wms[r] * 1000u); mend[r] = D3_CYC(); MARK('n');
    }
    vTaskSuspend(NULL);
}
int main(void) {
    calibrate();
    if (LOCK == 1) lk = xSemaphoreCreateMutex();
    else { lk = xSemaphoreCreateBinary(); xSemaphoreGive(lk); }  /* binary sem은 0으로 시작 → 한 번 give */
    xTaskCreate(tL, "L", 512, NULL, PL, NULL);
    xTaskCreate(tM, "M", 512, NULL, PM, NULL);
    xTaskCreate(tH, "H", 512, NULL, PH, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/D3/frt/inv.c OUT=/tmp/rtos_d3/inv0 EXTRA_CFLAGS=-DLOCK=0 \
  && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_d3/inv0/app.elf 30'
```

```text
CFG LOCK=0
R W=0 h_block_us=2067 h_resp_us=2280 m_resp_us=10 t0=735006
R W=1 h_block_us=3068 h_resp_us=3281 m_resp_us=1010 t0=1360006
R W=2 h_block_us=4068 h_resp_us=4281 m_resp_us=2010 t0=1985006
R W=4 h_block_us=6068 h_resp_us=6282 m_resp_us=4011 t0=2610006
R W=6 h_block_us=8069 h_resp_us=8282 m_resp_us=6011 t0=3235006
R W=8 h_block_us=10070 h_resp_us=10283 m_resp_us=8012 t0=3860006
R W=10 h_block_us=12070 h_resp_us=12283 m_resp_us=10013 t0=4485006
R W=12 h_block_us=14070 h_resp_us=14283 m_resp_us=12013 t0=5110006
```

출력에서 볼 것: `h_resp_us`가 **2280 → 3281 → 4281 → … → 14283**. W가 1 ms 늘 때마다 H의 응답이 정확히 약 1000 us 는다. H는 M과 아무 자원도 공유하지 않는데 M의 일을 전부 기다렸다. 기울기 1인 직선, 이것이 무한 역전의 정체다(m_resp_us = M 릴리스부터 M 끝까지).

### 2.3 trace로 한 라운드 들여다보기

W = 4 ms 라운드(t0 = 2610006 cycle)의 이벤트만 잘라 ms로 바꿨다. `S` = switch-in, `q`/`a` = H의 요청/획득, `m`/`n` = M 시작/끝, `L`/`U` = L의 lock/unlock 직후.

```text
   0.007 ms  S L prio=1
   0.011 ms  L L prio=1
   1.007 ms  S H prio=3
   1.008 ms  q H prio=3
   1.028 ms  S L prio=1
   2.007 ms  S M prio=2
   2.008 ms  m M prio=2
   6.012 ms  n M prio=2
   6.022 ms  S L prio=1
   7.064 ms  U L prio=1
   7.074 ms  S H prio=3
   7.077 ms  a H prio=3
   7.291 ms  S L prio=1
   7.301 ms  S I prio=0
```

1.008 ms에 H가 요청하고 바로 막힌다(1.028에 L로 돌아감). 2.007 ms에 M이 L을 선점해 6.012 ms까지 4 ms를 돈다. L은 prio 1 그대로라 저항할 방법이 없다. L이 7.064 ms에야 락을 놓고, H는 7.077 ms에 획득한다. 아래 그림 (a)가 이 기록을 그대로 그린 것이다.

---

## 3. Priority inheritance — FreeRTOS mutex로 바꾸면

### 3.1 아이디어

**L이 H를 막고 있는 동안에는 L을 H의 우선순위로 올려 준다.** 그러면 M(2)은 L(이제 3)을 선점할 수 없다. L은 임계 구역을 빨리 끝내고, 놓는 순간 원래 우선순위로 돌아간다. 이것이 **basic priority inheritance protocol(PIP)** 이다. FreeRTOS에서는 `xSemaphoreCreateMutex()`로 만든 mutex가 이 일을 한다(binary semaphore는 하지 않는다 — 이게 D1·D2의 "mutex vs semaphore" 답의 핵심 근거다).

### 3.2 예제 2 — 같은 코드, `LOCK=1`

무엇을 확인하는 코드인지: 예제 1과 같은 소스를 `-DLOCK=1`(mutex)로 빌드한다. 바뀌는 것은 `xSemaphoreCreateMutex()` 한 줄뿐이다.

```text
CFG LOCK=1
R W=0 h_block_us=2056 h_resp_us=2270 m_resp_us=1283 t0=735003
R W=1 h_block_us=2056 h_resp_us=2270 m_resp_us=2283 t0=1360003
R W=2 h_block_us=2056 h_resp_us=2270 m_resp_us=3283 t0=1985003
R W=4 h_block_us=2056 h_resp_us=2270 m_resp_us=5284 t0=2610003
R W=6 h_block_us=2056 h_resp_us=2270 m_resp_us=7284 t0=3235003
R W=8 h_block_us=2056 h_resp_us=2270 m_resp_us=9285 t0=3860003
R W=10 h_block_us=2056 h_resp_us=2270 m_resp_us=11285 t0=4485003
R W=12 h_block_us=2056 h_resp_us=2270 m_resp_us=13285 t0=5110003
```

출력에서 볼 것: `h_resp_us = 2270`이 **W와 무관하게 상수**다. 대신 `m_resp_us`가 W + 약 1283 us로 늘었다. M이 L의 남은 임계 구역과 H의 일을 기다려 준 것이다. 늦음이 "덜 급한 쪽"으로 옮겨 갔다. 우선순위 설계가 원래 의도한 그대로다.

같은 라운드의 trace다. `+`/`-`는 커널의 상속/해제 훅이 남긴 기록이다.

```text
   0.007 ms  S L prio=1
   0.011 ms  L L prio=1
   1.007 ms  S H prio=3
   1.008 ms  q H prio=3
   1.021 ms  + L prio=3
   1.032 ms  S L prio=3
   3.049 ms  U L prio=3
   3.055 ms  - L prio=1
   3.062 ms  S H prio=3
   3.065 ms  a H prio=3
   3.279 ms  S M prio=2
   3.280 ms  m M prio=2
   7.284 ms  n M prio=2
   7.295 ms  S L prio=1
   7.305 ms  S I prio=0
```

1.021 ms에 H가 막히면서 커널이 L을 3으로 올린다(`+ L prio=3`). 2 ms에 M이 깨어나지만 trace에 `S M`이 없다. L(3) > M(2)이라 선점이 일어나지 않았다. L은 3.049 ms에 락을 놓고, `xTaskPriorityDisinherit()`가 즉시 1로 내린다(`- L prio=1`). H가 3.065 ms에 획득한다.

```svg
<svg viewBox="0 0 680 348" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13" font-weight="bold">(a) binary semaphore를 락으로 — 상속 없음</text>
<text x="58" y="40" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="44" x2="650" y2="44" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="62" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="66" x2="650" y2="66" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="84" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="88" x2="650" y2="88" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.5" y="74" width="72.5" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="143.0" y="30" width="1.6" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="144.6" y="74" width="70.9" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="215.5" y="52" width="291.2" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="506.6" y="74" width="76.3" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="582.9" y="30" width="15.7" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="598.6" y="74" width="0.7" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="142.5" y1="26" x2="142.5" y2="46" stroke="currentColor" stroke-width="1.5"/>
<text x="145.5" y="27" font-size="12">릴리스</text>
<line x1="583.1" y1="26" x2="583.1" y2="46" stroke="currentColor" stroke-width="1.5"/>
<text x="586.1" y="27" font-size="12">획득</text>
<line x1="142.5" y1="37" x2="583.1" y2="37" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="362.8" y="34" font-size="12" text-anchor="middle">H 대기 6.08 ms</text>
<text x="10" y="114" font-size="13" font-weight="bold">(b) FreeRTOS mutex — priority inheritance</text>
<text x="58" y="136" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="140" x2="650" y2="140" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="158" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="162" x2="650" y2="162" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="180" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="184" x2="650" y2="184" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.5" y="170" width="72.5" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="143.0" y="126" width="1.9" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="144.8" y="170" width="146.6" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="291.5" y="170" width="0.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="292.0" y="126" width="15.8" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="307.7" y="148" width="291.2" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="598.9" y="170" width="0.7" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="142.5" y1="122" x2="142.5" y2="142" stroke="currentColor" stroke-width="1.5"/>
<text x="145.5" y="123" font-size="12">릴리스</text>
<line x1="292.2" y1="122" x2="292.2" y2="142" stroke="currentColor" stroke-width="1.5"/>
<text x="295.2" y="123" font-size="12">획득</text>
<line x1="142.5" y1="133" x2="292.2" y2="133" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="217.3" y="130" font-size="12" text-anchor="middle">H 대기 2.06 ms</text>
<text x="10" y="210" font-size="13" font-weight="bold">(c) ICPP: 락 잡기 전에 ceiling(3)으로 올림, time slicing 끔</text>
<text x="58" y="232" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="236" x2="650" y2="236" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="254" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="258" x2="650" y2="258" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="276" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="280" x2="650" y2="280" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.5" y="266" width="0.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="70.7" y="266" width="218.8" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="289.6" y="266" width="0.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="290.0" y="222" width="0.6" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="290.4" y="222" width="15.2" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="305.6" y="222" width="0.7" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="306.4" y="244" width="291.1" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="597.4" y="266" width="0.7" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="142.5" y1="218" x2="142.5" y2="238" stroke="currentColor" stroke-width="1.5"/>
<text x="145.5" y="219" font-size="12">릴리스</text>
<line x1="290.7" y1="218" x2="290.7" y2="238" stroke="currentColor" stroke-width="1.5"/>
<text x="293.7" y="219" font-size="12">획득</text>
<line x1="142.5" y1="229" x2="290.7" y2="229" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="216.6" y="226" font-size="12" text-anchor="middle">H 대기 2.04 ms</text>
<line x1="70" y1="294" x2="650" y2="294" stroke="currentColor"/>
<line x1="70.0" y1="294" x2="70.0" y2="298" stroke="currentColor"/>
<text x="70.0" y="311" font-size="12" text-anchor="middle">0</text>
<line x1="142.5" y1="294" x2="142.5" y2="298" stroke="currentColor"/>
<text x="142.5" y="311" font-size="12" text-anchor="middle">1</text>
<line x1="215.0" y1="294" x2="215.0" y2="298" stroke="currentColor"/>
<text x="215.0" y="311" font-size="12" text-anchor="middle">2</text>
<line x1="287.5" y1="294" x2="287.5" y2="298" stroke="currentColor"/>
<text x="287.5" y="311" font-size="12" text-anchor="middle">3</text>
<line x1="360.0" y1="294" x2="360.0" y2="298" stroke="currentColor"/>
<text x="360.0" y="311" font-size="12" text-anchor="middle">4</text>
<line x1="432.5" y1="294" x2="432.5" y2="298" stroke="currentColor"/>
<text x="432.5" y="311" font-size="12" text-anchor="middle">5</text>
<line x1="505.0" y1="294" x2="505.0" y2="298" stroke="currentColor"/>
<text x="505.0" y="311" font-size="12" text-anchor="middle">6</text>
<line x1="577.5" y1="294" x2="577.5" y2="298" stroke="currentColor"/>
<text x="577.5" y="311" font-size="12" text-anchor="middle">7</text>
<line x1="650.0" y1="294" x2="650.0" y2="298" stroke="currentColor"/>
<text x="650.0" y="311" font-size="12" text-anchor="middle">8</text>
<text x="650" y="326" font-size="12" text-anchor="end">시간 (ms, 라운드 시작 = L 릴리스)</text>
<rect x="70" y="332" width="14" height="10" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/><text x="90" y="341" font-size="12">L (prio 1)</text>
<rect x="170" y="332" width="14" height="10" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0"/><text x="190" y="341" font-size="12">L이 prio 3으로 올라간 구간</text>
<rect x="370" y="332" width="14" height="10" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/><text x="390" y="341" font-size="12">M</text>
<rect x="420" y="332" width="14" height="10" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/><text x="440" y="341" font-size="12">H</text>
</svg>
```
그림 2 — 예제 1·2·4의 W = 4 ms 라운드를 trace 타임스탬프 그대로 그린 간트 차트(QEMU icount 기준). (a) binary semaphore: M(주황)이 L을 밀어내 H 대기 6.08 ms. (b) mutex: H가 막히는 순간 L이 prio 3(빨강 칠)으로 올라가 M이 끼어들지 못한다, H 대기 2.06 ms. (c) ICPP(§7): L이 락을 잡기 전에 이미 3으로 올라가 있어, H는 1 ms에 ready가 되고도 L(같은 3)이 락을 놓을 때까지 CPU를 받지 못한다. H 대기 2.04 ms로 (b)와 같고, H가 "막혔다 깨는" 왕복이 없어 context switch가 가장 적다.

### 3.3 무엇이 남았나 — 유한 역전 2 ms

mutex를 써도 H는 약 2 ms를 기다렸다. L이 1 ms에 이미 임계 구역 3 ms 중 1 ms를 지났으니 남은 2 ms다. 이것은 **없앨 수 없는 유한 역전**이다. 줄이는 방법은 임계 구역을 짧게 만드는 것뿐이다(§12에서 DMA를 락 밖으로 빼는 예). 그래서 실무 규칙은 두 개가 짝이다.

1. 자원 보호에는 **반드시 상속이 있는 mutex**를 쓴다 (무한 → 유한).
2. 임계 구역은 **측정해서 짧게** 유지한다 (유한한 상한을 작게).

---

## 4. Zephyr에서 같은 실험 — `k_sem` vs `k_mutex`, 그리고 `CONFIG_PRIORITY_CEILING`

### 4.1 예제 3 — Zephyr 하네스와 결과

Zephyr에는 FreeRTOS의 trace 매크로 대신 `CONFIG_TRACING_USER`가 있다. `sys_trace_thread_switched_in_user()`를 앱이 정의하면 switch-in마다 불린다(C4에서 쓴 방식). Zephyr 우선순위는 **숫자가 작을수록 높다**(A5): H = 1, M = 3, L = 5.

무엇을 확인하는 코드인지: Zephyr 공통 헤더(switch-in trace, 보정된 일, semihosting 종료).

```c
/* D3 Zephyr 공통: switch-in trace(RAM), 보정된 CPU 일, semihosting 종료 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
enum { TN = 1024 };
static volatile uint32_t tt[TN], ti; static volatile char tn[TN]; static volatile int8_t tp[TN];
static uint32_t lp100;
static char name_of(struct k_thread *c);                /* 앱이 정의: 스레드 → 글자 */
void sys_trace_thread_switched_in_user(void) {
	struct k_thread *c = k_current_get(); uint32_t i = ti;
	if (i < TN) { tt[i] = k_cycle_get_32(); tn[i] = name_of(c); tp[i] = (int8_t)c->base.prio; ti = i + 1; }
}
static inline void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }
static inline void work_us(uint32_t us) { spin(us * lp100 / 100u); }
static inline void calibrate(void) {
	uint32_t t0 = k_cycle_get_32(); spin(100000); uint32_t c = k_cycle_get_32() - t0;
	lp100 = (uint32_t)(100000ull * 2500u / c);           /* 2500 cycle = 100 us @ 25 MHz */
}
static void dump_exit(void) {
	uint32_t n = ti; ti = TN;
	for (uint32_t i = 0; i < n; i++) printk("T %u S %c %d\n", tt[i], tn[i], tp[i]);
	printk("END\n");
	register unsigned r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;   /* semihosting exit */
	__asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
}
```

무엇을 확인하는 코드인지: FreeRTOS 예제 1·2와 같은 일감. `SCEN=1`은 `k_sem`, `SCEN=2`는 `k_mutex`. 여기에 `ceil4.conf`(`CONFIG_PRIORITY_CEILING=4`)를 더한 빌드도 돌린다.

```c
/* 예제 3: Zephyr L/M/H. SCEN 1 = k_sem을 락으로 · 2 = k_mutex(PI) · (2 + ceil4.conf = CONFIG_PRIORITY_CEILING=4)
 *   우선순위(작을수록 높다): H=1, M=3, L=5. L: t0에 락 3 ms · H: t0+1 ms 락 요청 · M: t0+2 ms에 W ms 일 */
#include "common.h"
static const uint16_t Wms[] = { 0, 1, 2, 4, 6, 8, 10, 12 };
#define NR ARRAY_SIZE(Wms)
enum { PERIOD = 25, START = 100 };
K_SEM_DEFINE(sem, 1, 1);
K_MUTEX_DEFINE(mtx);
static uint32_t rel[NR], acq[NR], hend[NR], pl[NR];
static struct k_thread th[3]; static K_THREAD_STACK_ARRAY_DEFINE(stk, 3, 1024);
static char name_of(struct k_thread *c) { return c == &th[0] ? 'L' : c == &th[1] ? 'M' : c == &th[2] ? 'H' : 'I'; }
static void lock(void)   { if (SCEN == 1) k_sem_take(&sem, K_FOREVER); else k_mutex_lock(&mtx, K_FOREVER); }
static void unlock(void) { if (SCEN == 1) k_sem_give(&sem); else k_mutex_unlock(&mtx); }
static void at(int64_t tick) { k_sleep(K_TIMEOUT_ABS_TICKS(tick)); }
static void fL(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (uint32_t r = 0; r < NR; r++) { at(START + r * PERIOD); lock(); work_us(3000); unlock(); }
	at(START + NR * PERIOD);
	printk("CFG SCEN=%d PRIORITY_CEILING=%d\n", SCEN, CONFIG_PRIORITY_CEILING);
	for (uint32_t r = 0; r < NR; r++)
		printk("R W=%u h_block_us=%u h_resp_us=%u prio(L)_when_M_ran=%u\n", Wms[r],
		       (acq[r] - rel[r]) / 25u, (hend[r] - rel[r]) / 25u, pl[r]);
	dump_exit();
}
static void fH(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (uint32_t r = 0; r < NR; r++) {
		at(START + r * PERIOD + 1); rel[r] = k_cycle_get_32();
		lock(); acq[r] = k_cycle_get_32(); work_us(200); unlock(); hend[r] = k_cycle_get_32();
	}
}
static void fM(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (uint32_t r = 0; r < NR; r++) {
		at(START + r * PERIOD + 2); pl[r] = k_thread_priority_get(&th[0]); work_us(Wms[r] * 1000u);
	}
}
int main(void) {
	calibrate();
	k_thread_create(&th[0], stk[0], 1024, fL, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&th[1], stk[1], 1024, fM, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
	k_thread_create(&th[2], stk[2], 1024, fH, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
	return 0;
}
```

```text
CONFIG_PRINTK=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
CONFIG_TIMESLICING=n
```

```sh
# runz.sh <이름> <APP> <SCEN> [추가 conf] — rtos-zbuild로 빌드 후 qemu-system-arm -icount shift=5로 직접 실행
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/D3/runz.sh; $R zsw1 sweep 1; $R zsw2 sweep 2; $R zsw2c sweep 2 ceil4.conf'
```

```text
CFG SCEN=1 PRIORITY_CEILING=-128
R W=0 h_block_us=2052 h_resp_us=2254 prio(L)_when_M_ran=5
R W=1 h_block_us=3051 h_resp_us=3253 prio(L)_when_M_ran=5
R W=2 h_block_us=4050 h_resp_us=4252 prio(L)_when_M_ran=5
R W=4 h_block_us=6048 h_resp_us=6250 prio(L)_when_M_ran=5
R W=6 h_block_us=8046 h_resp_us=8248 prio(L)_when_M_ran=5
R W=8 h_block_us=10044 h_resp_us=10246 prio(L)_when_M_ran=5
R W=10 h_block_us=12042 h_resp_us=12244 prio(L)_when_M_ran=5
R W=12 h_block_us=14036 h_resp_us=14238 prio(L)_when_M_ran=5
CFG SCEN=2 PRIORITY_CEILING=-128
R W=0 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=1 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=2 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=4 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=6 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=8 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=10 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
R W=12 h_block_us=2041 h_resp_us=2244 prio(L)_when_M_ran=5
CFG SCEN=2 PRIORITY_CEILING=4
R W=0 h_block_us=2057 h_resp_us=2260 prio(L)_when_M_ran=4
R W=1 h_block_us=3056 h_resp_us=3259 prio(L)_when_M_ran=4
R W=2 h_block_us=4055 h_resp_us=4258 prio(L)_when_M_ran=4
R W=4 h_block_us=6053 h_resp_us=6256 prio(L)_when_M_ran=4
R W=6 h_block_us=8051 h_resp_us=8254 prio(L)_when_M_ran=4
R W=8 h_block_us=10049 h_resp_us=10252 prio(L)_when_M_ran=4
R W=10 h_block_us=12047 h_resp_us=12250 prio(L)_when_M_ran=4
R W=12 h_block_us=14041 h_resp_us=14244 prio(L)_when_M_ran=4
```

출력에서 볼 것: `k_sem`은 FreeRTOS binary semaphore와 거의 같은 직선(2254 → 14238 us)이다. `k_mutex`는 2244 us 상수다. 그런데 `CONFIG_PRIORITY_CEILING=4`를 주면 `k_mutex`인데도 **다시 직선**이 됐고, M이 돌 때 L의 우선순위가 4였다(`prio(L)_when_M_ran=4`).

### 4.2 `CONFIG_PRIORITY_CEILING`은 "priority ceiling protocol"이 아니다

이름 때문에 가장 많이 오해하는 옵션이다. `kernel/Kconfig`의 도움말은 이렇다: "This defines the minimum priority value (i.e. the logically highest priority) that a thread will acquire as part of the k_mutex priority inheritance." 즉 **상속으로 올라갈 수 있는 최고 높이의 상한**이다. 기본값 -128은 사실상 제한 없음이고, 값을 idle 우선순위 이하로 두면 상속 코드 전체가 `#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)`로 컴파일에서 빠진다.

실험에서 L(5)은 H(1)를 상속해야 했지만 상한 4에 걸려 4까지만 올라갔다. M(3)이 4보다 높으니 L을 선점했고, 무한 역전이 그대로 돌아왔다. 이 옵션은 "상속이 cooperative 우선순위(음수)나 meta-IRQ 영역까지 올라가 시스템 전체를 멈추는 것"을 막으려는 안전장치로 쓰는 것이고, 값을 **락을 쓰는 가장 높은 스레드보다 낮게(숫자 크게) 잡으면 상속이 무력화된다.** ICPP(§6.4)와는 아무 관계가 없다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="290.0" x2="420" y2="290.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="294.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="238.0" x2="420" y2="238.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="242.0" font-size="12" text-anchor="end">3</text>
<line x1="70" y1="186.0" x2="420" y2="186.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="190.0" font-size="12" text-anchor="end">6</text>
<line x1="70" y1="134.0" x2="420" y2="134.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="138.0" font-size="12" text-anchor="end">9</text>
<line x1="70" y1="82.0" x2="420" y2="82.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="86.0" font-size="12" text-anchor="end">12</text>
<line x1="70" y1="30.0" x2="420" y2="30.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="34.0" font-size="12" text-anchor="end">15</text>
<text x="70.0" y="306" font-size="12" text-anchor="middle">0</text>
<text x="128.3" y="306" font-size="12" text-anchor="middle">2</text>
<text x="186.7" y="306" font-size="12" text-anchor="middle">4</text>
<text x="245.0" y="306" font-size="12" text-anchor="middle">6</text>
<text x="303.3" y="306" font-size="12" text-anchor="middle">8</text>
<text x="361.7" y="306" font-size="12" text-anchor="middle">10</text>
<text x="420.0" y="306" font-size="12" text-anchor="middle">12</text>
<line x1="70" y1="290" x2="420" y2="290" stroke="currentColor"/><line x1="70" y1="290" x2="70" y2="30" stroke="currentColor"/>
<text x="245.0" y="324" font-size="12" text-anchor="middle">M의 일 W (ms) — 락과 무관한 중간 우선순위 일</text>
<text x="14" y="18" font-size="12">H 응답 시간 (ms, 릴리스 → 완료)</text>
<polyline points="70.0,250.5 99.2,233.1 128.3,215.8 186.7,181.1 245.0,146.4 303.3,111.8 361.7,77.1 420.0,42.4" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="250.5" r="2.5" fill="#4a7bd0"/>
<circle cx="99.2" cy="233.1" r="2.5" fill="#4a7bd0"/>
<circle cx="128.3" cy="215.8" r="2.5" fill="#4a7bd0"/>
<circle cx="186.7" cy="181.1" r="2.5" fill="#4a7bd0"/>
<circle cx="245.0" cy="146.4" r="2.5" fill="#4a7bd0"/>
<circle cx="303.3" cy="111.8" r="2.5" fill="#4a7bd0"/>
<circle cx="361.7" cy="77.1" r="2.5" fill="#4a7bd0"/>
<circle cx="420.0" cy="42.4" r="2.5" fill="#4a7bd0"/>
<line x1="436" y1="50" x2="466" y2="50" stroke="#4a7bd0" stroke-width="2"/>
<text x="472" y="54" font-size="12">FreeRTOS binary sem</text>
<polyline points="70.0,250.9 99.2,233.6 128.3,216.3 186.7,181.7 245.0,147.0 303.3,112.4 361.7,77.8 420.0,43.2" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/>
<circle cx="70.0" cy="250.9" r="2.5" fill="#4a7bd0"/>
<circle cx="99.2" cy="233.6" r="2.5" fill="#4a7bd0"/>
<circle cx="128.3" cy="216.3" r="2.5" fill="#4a7bd0"/>
<circle cx="186.7" cy="181.7" r="2.5" fill="#4a7bd0"/>
<circle cx="245.0" cy="147.0" r="2.5" fill="#4a7bd0"/>
<circle cx="303.3" cy="112.4" r="2.5" fill="#4a7bd0"/>
<circle cx="361.7" cy="77.8" r="2.5" fill="#4a7bd0"/>
<circle cx="420.0" cy="43.2" r="2.5" fill="#4a7bd0"/>
<line x1="436" y1="72" x2="466" y2="72" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/>
<text x="472" y="76" font-size="12">Zephyr k_sem</text>
<polyline points="70.0,250.8 99.2,233.5 128.3,216.2 186.7,181.6 245.0,146.9 303.3,112.3 361.7,77.7 420.0,43.1" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="2 3"/>
<circle cx="70.0" cy="250.8" r="2.5" fill="#d0564a"/>
<circle cx="99.2" cy="233.5" r="2.5" fill="#d0564a"/>
<circle cx="128.3" cy="216.2" r="2.5" fill="#d0564a"/>
<circle cx="186.7" cy="181.6" r="2.5" fill="#d0564a"/>
<circle cx="245.0" cy="146.9" r="2.5" fill="#d0564a"/>
<circle cx="303.3" cy="112.3" r="2.5" fill="#d0564a"/>
<circle cx="361.7" cy="77.7" r="2.5" fill="#d0564a"/>
<circle cx="420.0" cy="43.1" r="2.5" fill="#d0564a"/>
<line x1="436" y1="94" x2="466" y2="94" stroke="#d0564a" stroke-width="2" stroke-dasharray="2 3"/>
<text x="472" y="98" font-size="12">Zephyr k_mutex, ceiling=4</text>
<polyline points="70.0,250.7 99.2,250.7 128.3,250.7 186.7,250.7 245.0,250.7 303.3,250.7 361.7,250.7 420.0,250.7" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="70.0" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="99.2" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="128.3" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="186.7" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="245.0" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="303.3" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="361.7" cy="250.7" r="2.5" fill="#3f9a6b"/>
<circle cx="420.0" cy="250.7" r="2.5" fill="#3f9a6b"/>
<line x1="436" y1="116" x2="466" y2="116" stroke="#3f9a6b" stroke-width="2"/>
<text x="472" y="120" font-size="12">FreeRTOS mutex (PIP)</text>
<polyline points="70.0,251.1 99.2,251.1 128.3,251.1 186.7,251.1 245.0,251.1 303.3,251.1 361.7,251.1 420.0,251.1" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6 4"/>
<circle cx="70.0" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="99.2" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="128.3" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="186.7" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="245.0" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="303.3" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="361.7" cy="251.1" r="2.5" fill="#3f9a6b"/>
<circle cx="420.0" cy="251.1" r="2.5" fill="#3f9a6b"/>
<line x1="436" y1="138" x2="466" y2="138" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6 4"/>
<text x="472" y="142" font-size="12">Zephyr k_mutex (PIP)</text>
<polyline points="70.0,251.0 99.2,251.0 128.3,251.0 186.7,251.0 245.0,251.0 303.3,251.0 361.7,251.0 420.0,251.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="99.2" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="128.3" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="186.7" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="245.0" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="303.3" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="361.7" cy="251.0" r="2.5" fill="#e08a3c"/>
<circle cx="420.0" cy="251.0" r="2.5" fill="#e08a3c"/>
<line x1="436" y1="160" x2="466" y2="160" stroke="#e08a3c" stroke-width="2"/>
<text x="472" y="164" font-size="12">FreeRTOS ICPP (prio set)</text>
<text x="436" y="196" font-size="12">기울기 1: M이 1 ms 더 돌면</text>
<text x="436" y="214" font-size="12">H도 1 ms 더 늦는다 (무한 역전)</text>
<text x="436" y="238" font-size="12">평평한 선: L의 남은 임계 구역</text>
<text x="436" y="256" font-size="12">(약 2 ms)에서 멈춘다 (유한 역전)</text>
</svg>
```
그림 3 — 예제 1·2·3·4의 측정값. 가로축은 락과 무관한 M의 일 W, 세로축은 H의 응답 시간. 상속이 없거나(binary sem, `k_sem`) 상한에 막힌(`k_mutex` + ceiling=4) 세 선은 기울기 1로 겹치고, 상속(PIP)과 ICPP 세 선은 약 2.25 ms에서 평평하다. QEMU icount 기준.

### 4.3 두 RTOS 비교 (지금까지의 측정)

| 항목 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| 상속이 있는 락 | `xSemaphoreCreateMutex()`, 재귀 mutex | `k_mutex` (항상) |
| 상속이 없는 락 | binary · counting semaphore | `k_sem` |
| H 응답, W = 12 ms, 상속 없음 | 14283 us | 14238 us |
| H 응답, 상속 있음 | 2270 us (W 무관) | 2244 us (W 무관) |
| 상속 상한 손잡이 | 없음 | `CONFIG_PRIORITY_CEILING` (기본 -128) |
| 상속 끄기 | binary semaphore를 쓰면 됨 | ceiling을 idle 이하로 (컴파일 제외) |
| ICPP 내장 | 없음 | 없음 (POSIX `PTHREAD_PRIO_PROTECT`도 미구현, §11) |

---

## 5. Mars Pathfinder 1997 — 면접관이 기대하는 그 이야기, 사실대로

### 5.1 무슨 일이 있었나

Mars Pathfinder는 1997년 7월 4일 화성에 착륙했다. 비행 소프트웨어는 Wind River의 **VxWorks** 위에서 돌았다. 지표 운용 중 컴퓨터가 **전체 리셋(total system reset)** 을 반복했고, 그때마다 그날 데이터 일부를 잃었다. 원인은 교과서 그대로의 우선순위 역전이었다.

이 이야기는 두 개의 1차 자료로 퍼졌다. 둘을 구분해서 말할 수 있으면 좋다.

- **Mike Jones의 요약 (1997-12-05)**: IEEE Real-Time Systems Symposium(1997년 12월)에서 Wind River CTO David Wilner가 한 기조연설을 정리한 이메일. "information bus"라는 공유 메모리 영역을 mutex로 보호했고, **버스 관리 태스크(높은 우선순위, 자주 실행)**, **기상 데이터 수집 태스크(낮은 우선순위, 가끔 실행, 버스에 데이터를 게시)**, **통신 태스크(중간 우선순위, 오래 실행)** 가 등장한다.
- **Glenn Reeves의 답장 (1997-12-15)**: JPL의 Pathfinder 비행 소프트웨어 책임 엔지니어(Flight Software Cognizant Engineer)가 쓴 "권위 있는 설명". 태스크 이름까지 나온다: 최고 우선순위의 `bc_sched`, 1553 버스 데이터를 분배하는 높은 우선순위의 `bc_dist`, 훨씬 낮은 우선순위의 `ASI/MET`(기상 과학 계측). 문제의 자원은 **VxWorks `select()` 메커니즘 내부의 mutual exclusion semaphore**였다.

### 5.2 역전의 사슬 (Reeves의 설명 기준)

1. `ASI/MET`(낮음)가 pipe를 통해 데이터를 보내는 과정에서 `select()` 내부 mutex를 쥔다.
2. `ASI/MET`가 그 mutex를 쥔 채로 선점된다.
3. `bc_dist`(높음)가 `pipeWrite()` 안에서 같은 mutex를 기다리며 막힌다.
4. 중간 우선순위 태스크들이 계속 돈다. `ASI/MET`는 CPU를 받지 못해 mutex를 놓지 못한다.
5. 1553 버스는 8 Hz 주기로 동작했고, 다음 주기에 `bc_sched`가 깨어나 **`bc_dist`가 자기 사이클을 끝내지 못했음**을 발견한다. 이것은 하드 마감 위반이라 오류를 선언하고 **시스템을 리셋**한다. Jones 요약은 이 검사를 "watchdog timer가 버스 태스크가 한동안 실행되지 않은 것을 감지했다"고 표현했다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerUnits="userSpaceOnUse" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="340" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="32" y="40" font-size="13">bc_sched — 최고 우선순위</text>
<text x="32" y="59" font-size="12">8 Hz 주기마다 bc_dist 완료를 확인</text>
<rect x="20" y="100" width="340" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="32" y="120" font-size="13">bc_dist — 높음 (1553 버스 데이터 분배)</text>
<text x="32" y="139" font-size="12">pipeWrite() 안에서 mutex 대기 → blocked</text>
<rect x="20" y="180" width="340" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="32" y="200" font-size="13">중간 우선순위 태스크들</text>
<text x="32" y="219" font-size="12">오래 실행 → ASI/MET를 계속 밀어냄</text>
<rect x="20" y="260" width="340" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="280" font-size="13">ASI/MET — 훨씬 낮음 (기상 계측)</text>
<text x="32" y="299" font-size="12">select() mutex를 쥔 채 선점됨</text>
<rect x="440" y="180" width="220" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="452" y="200" font-size="13">select() 내부 mutex</text>
<text x="452" y="219" font-size="12">priority inheritance 꺼짐</text>
<rect x="440" y="20" width="220" height="50" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/>
<text x="452" y="40" font-size="13">마감 위반 선언</text>
<text x="452" y="59" font-size="12">→ 전체 시스템 리셋</text>
<line x1="360" y1="45" x2="438" y2="45" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="360" y1="130" x2="438" y2="192" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="384" y="150" font-size="12">기다림</text>
<line x1="438" y1="220" x2="362" y2="280" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="404" y="270" font-size="12">쥠</text>
<line x1="190" y1="231" x2="190" y2="258" stroke="#e08a3c" stroke-width="2" marker-end="url(#ah2)"/>
<text x="200" y="250" font-size="12">선점</text>
<line x1="190" y1="72" x2="190" y2="98" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<text x="200" y="90" font-size="12">완료 검사</text>
<text x="20" y="342" font-size="12">해결: selectLib의 전역 변수를 바꿔 select() 세마포어에 priority inheritance를 켬</text>
<text x="20" y="362" font-size="12">(지상 복제기에서 재현 · 시험 후 패치로 업로드, Reeves 1997-12-15)</text>
<text x="20" y="386" font-size="12">교훈: 상속 옵션 한 개가 꺼져 있었을 뿐, 커널에는 이미 해결책이 들어 있었다</text>
</svg>
```
그림 4 — Mars Pathfinder 역전 사슬(Reeves의 설명 기준으로 그림). 그림 1과 같은 모양이다: 높음(bc_dist)이 낮음(ASI/MET)이 쥔 mutex를 기다리고, 중간 태스크들이 낮음을 밀어낸다. 다른 점은 마지막에 "마감 위반 → 리셋"이라는 안전장치가 걸려 있었다는 것.

### 5.3 어떻게 찾고 어떻게 고쳤나

- **발사 전**: Reeves는 착륙 전에 이 문제를 본 적이 있지만 추적하려 할 때 재현되지 않았고, 진입 · 착륙 소프트웨어에 집중하느라 우선순위를 낮게 뒀다고 썼다. 발사 전 시험은 "best case" 높은 데이터율 · 과학 활동 위주였다.
- **재현**: JPL은 우주선과 똑같은 지상 복제기(replica)에서 시스템을 돌렸다. VxWorks에는 context switch, 동기화 객체 사용, 인터럽트를 기록하는 trace 기능이 있었고, 이것을 켠 채로 실행해 리셋을 재현했다. Reeves는 "18시간이 안 되어 문제를 일으킬 수 있었다"고 썼다. trace를 분석해 우선순위 역전을 확인했다.
- **원인**: VxWorks mutex 세마포어는 우선순위 상속을 켜는 옵션을 받는다(VxWorks API로는 `semMCreate()`의 `SEM_INVERSION_SAFE`). 문제의 mutex는 이 옵션이 **꺼진 채** 초기화됐다. Jones 요약에 따르면 이 mutex와, 같은 문제를 일으킬 수 있는 다른 mutex 두 개의 초기화 파라미터가 전역 변수에 있었고, 그 주소가 발사된 소프트웨어의 심볼 테이블에 들어 있었다.
- **수정**: Reeves에 따르면 `selectLib`의 전역 변수를 바꿔 select 세마포어에 우선순위 상속을 켰다. 이 변경은 그 뒤에 만들어지는 모든 select 세마포어에 적용된다. Wind River가 성능 영향을 분석해 미미하다고 판단했고, 지상에서 시험한 뒤 **패치 절차로 업로드**했다. (Jones 요약은 "VxWorks의 C 인터프리터로 작은 C 프로그램을 올려 변수를 FALSE에서 TRUE로 바꿨다"고 썼지만, Reeves는 vxWorks 셸이 아니라 패치 과정이었다고 바로잡았다.) 이후 리셋은 다시 일어나지 않았다.

### 5.4 면접용 교훈 네 가지

1. **알고리즘은 이미 있었다.** 우선순위 상속은 1990년 논문(§6)으로 정리된 기법이고 VxWorks에 이미 구현돼 있었다. 문제는 **기본값/설정**이었다. FreeRTOS에서 binary semaphore를 락으로 쓰는 것, Zephyr에서 `k_sem`을 락으로 쓰는 것이 정확히 같은 실수다(예제 1, 3).
2. **trace가 결정적이었다.** 간헐적 리셋은 로그 printf로는 못 잡는다. "누가 언제 CPU를 쥐었나"의 시간축 기록이 있어야 한다(M3).
3. **안전장치(마감 검사 → 리셋)가 증상을 "리셋"으로 바꿔 줬다.** 덕분에 우주선은 살아 있었고 원격으로 고칠 수 있었다. 워치독 설계(E8)와 연결된다.
4. **현장 패치 가능성**: 심볼 테이블과 원격 패치 경로가 남아 있어서 고칠 수 있었다. 드론 · 차량 펌웨어의 OTA와 같은 이야기다.

> 확인한 범위: 위 내용은 Jones 요약과 Reeves 답장 두 원문에서 확인한 사실만 썼다. 리셋이 착륙 며칠 뒤 처음 났는지 같은 날짜 세부는 두 원문에서 확인하지 못해 쓰지 않았다.

---

## 6. 프로토콜 이론 — PIP · 전이적 상속 · OCPP · ICPP

이론의 출발점은 **Sha, Rajkumar, Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Transactions on Computers 39(9), 1990** 이다. 이 논문이 basic priority inheritance protocol과 priority ceiling protocol을 정의하고 성질을 증명했다. 증명은 N4에서 하고, 여기서는 규칙과 성질을 정확히 말할 수 있게 만든다.

용어: 각 태스크 i에는 **기본 우선순위(base)** 와 지금 스케줄러가 쓰는 **유효 우선순위(active/effective)** 가 있다. FreeRTOS TCB의 `uxBasePriority`/`uxPriority`, Zephyr는 `mutex->owner_orig_prio`/`thread->base.prio`가 각각 이것이다.

### 6.1 PIP (basic priority inheritance) 규칙

1. 태스크 J가 락 S를 요청했는데 다른 태스크 K가 쥐고 있으면 J는 blocked가 된다.
2. K의 유효 우선순위 = max(K의 base, K 때문에 막힌 모든 태스크의 유효 우선순위). 즉 K는 자기가 막고 있는 태스크 중 가장 높은 것을 **상속**한다.
3. K가 S를 놓으면, S 때문에 상속받은 부분을 내려놓는다. 다른 락 때문에 여전히 누군가를 막고 있으면 그 최고값까지만 내려간다.
4. **전이성(transitivity)**: K 자신이 다른 락 때문에 L에게 막혀 있으면, K가 상속받은 우선순위가 L에게도 전달된다(H → K → L 사슬).

규칙 2·3을 정확히 구현하려면 "이 태스크가 쥔 락마다의 최고 대기자"를 알아야 하고, 규칙 4는 사슬을 따라 걷는 루프가 필요하다. Linux `rt_mutex`는 이 사슬 걷기(`rt_mutex_adjust_prio_chain`)를 실제로 한다(EL N1 §7.3의 PI futex가 이 위에 있다). MCU RTOS는 대부분 이것을 **단순화**했고, 그 대가를 §8·§9에서 잰다.

### 6.2 PIP의 성질 — 좋은 점과 남는 문제

- **무한 역전을 막는다.** 중간 태스크는 상속받은 L을 선점하지 못한다(예제 2).
- **데드락은 막지 못한다.** H가 S2를 쥐고 S1을 기다리고, L이 S1을 쥐고 S2를 기다리면 상속과 상관없이 둘 다 영원히 기다린다(그림 5 위). 락 순서 규칙(D9)이 따로 필요하다.
- **chained blocking**: H가 락 n개를 순서대로 쓰고, 그 각각을 서로 다른 낮은 태스크가 쥐고 있으면 H는 **락마다 한 번씩** 막힐 수 있다. 상한은 "낮은 태스크 수"와 "H를 막을 수 있는 락 수" 중 작은 쪽 번의 임계 구역이다.
- **push-through blocking**: L이 H를 상속해 3이 되면, 그 락을 쓰지도 않는 M(2)도 L을 기다리게 된다. 예제 2의 `m_resp_us`가 W + 1.28 ms로 늘어난 것이 이것이다. M 입장의 블로킹이다.

### 6.3 OCPP (original priority ceiling protocol)

**자원마다 ceiling을 미리 정한다**: ceiling(S) = S를 쓸 수 있는 태스크 중 가장 높은 우선순위. 이것은 설계 시점(정적)에 알아야 한다.

1. **system ceiling** = 지금 **다른 태스크들이** 쥐고 있는 자원들의 ceiling 중 최댓값.
2. 태스크 J는 자원이 비어 있어도, **J의 우선순위가 system ceiling보다 엄격히 높을 때만** 잠글 수 있다. 아니면 blocked(ceiling blocking)가 된다.
3. J를 막은 자원(system ceiling을 만든 자원)의 소유자는 J의 우선순위를 상속한다(PIP와 같은 상속).

규칙 2가 핵심이다. "지금 잠긴 자원 중에 나보다 높은 태스크가 쓸 수 있는 게 있으면, 다른 빈 자원도 건드리지 마라." 이 한 줄 때문에 아래 성질이 증명된다.

- **데드락이 없다.** 순환 대기가 생길 수 없다(그림 5 아래).
- **블로킹은 최대 한 번**, 그 길이는 **낮은 태스크의 임계 구역 하나**(ceiling이 자기 이상인 것 중 가장 긴 것)를 넘지 않는다. chained blocking이 없다.

단점은 구현이다. 커널이 system ceiling을 매 lock마다 계산해야 하고 자원별 ceiling을 알아야 한다. 범용 RTOS(FreeRTOS · Zephyr)는 OCPP를 구현하지 않는다.

### 6.4 ICPP (immediate ceiling priority protocol)

OCPP를 단순하게 만든 변형이다. **락을 잡는 순간 바로 그 자원의 ceiling으로 우선순위를 올리고, 놓을 때 원래대로 내린다.** 이름이 여럿이다: highest locker protocol, priority protect protocol(POSIX `PTHREAD_PRIO_PROTECT` + `pthread_mutexattr_setprioceiling`), Ada의 `Ceiling_Locking` 정책, OSEK/AUTOSAR OS의 resource(`GetResource`/`ReleaseResource`, I5).

- 최악 블로킹 상한은 OCPP와 같다: 임계 구역 하나.
- 데드락이 없다(단일 코어).
- **블로킹이 실행 시작 전에만 일어난다**: 태스크가 일단 CPU를 받으면 자원 때문에 다시 막히지 않는다. ceiling이 자기 이상인 자원을 누가 쥐고 있으면 애초에 CPU를 못 받기 때문이다. 그래서 같은 우선순위 태스크들이 **스택 하나를 공유**할 수 있고(Baker의 Stack Resource Policy, I9 RTIC가 컴파일 시점에 이것을 쓴다), N4에서 다룬다.
- 대가: 경쟁이 없어도 **매번** 우선순위를 바꾼다(비용, §7.3). 그리고 L이 임계 구역 안에 있는 동안 L과 ceiling 사이의 **모든** 태스크가 기다린다(자원을 안 쓰는 태스크도). OCPP · PIP는 실제로 경쟁이 생길 때만 L을 올린다.

### 6.5 PIP와 PCP의 데드락 예 — 그림으로

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13" font-weight="bold">PIP: H는 S2 → S1, L은 S1 → S2 순서로 잠근다</text>
<text x="40" y="52" font-size="12">H</text>
<text x="40" y="88" font-size="12">L</text>
<line x1="70" y1="58" x2="630" y2="58" stroke="currentColor" stroke-opacity="0.25"/>
<line x1="70" y1="94" x2="630" y2="94" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70" y="78" width="80" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="150" y="42" width="80" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="230" y="78" width="80" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0"/>
<rect x="310" y="42" width="320" height="14" fill="#888" fill-opacity="0.25" stroke="#888" stroke-dasharray="4 3"/>
<rect x="310" y="78" width="320" height="14" fill="#888" fill-opacity="0.25" stroke="#888" stroke-dasharray="4 3"/>
<text x="74" y="74" font-size="12">S1 잠금</text>
<text x="154" y="38" font-size="12">S2 잠금, S1 요청</text>
<text x="234" y="112" font-size="12">L이 H 상속, S2 요청</text>
<text x="420" y="74" font-size="13">데드락 — 둘 다 영원히 대기</text>
<text x="10" y="150" font-size="13" font-weight="bold">PCP (OCPP 또는 ICPP): ceiling(S1) = ceiling(S2) = H</text>
<text x="40" y="184" font-size="12">H</text>
<text x="40" y="220" font-size="12">L</text>
<line x1="70" y1="190" x2="630" y2="190" stroke="currentColor" stroke-opacity="0.25"/>
<line x1="70" y1="226" x2="630" y2="226" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70" y="210" width="80" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="150" y="174" width="30" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="180" y="210" width="160" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0"/>
<rect x="340" y="174" width="160" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="500" y="210" width="60" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="74" y="206" font-size="12">S1 잠금</text>
<text x="154" y="170" font-size="12">S2 요청 → 거절 (prio가 system ceiling 이하)</text>
<text x="184" y="244" font-size="12">L이 H 상속: S2 잠금 → 해제 → S1 해제</text>
<text x="344" y="206" font-size="12">S2 · S1 잠금, 끝</text>
<line x1="70" y1="270" x2="630" y2="270" stroke="currentColor"/>
<text x="70" y="288" font-size="12">0</text>
<text x="620" y="288" font-size="12">t</text>
<text x="10" y="316" font-size="12">빨강 칠 = H의 우선순위로 도는 L. PCP는 H를 "빈 S2"에서 미리 막아서 순환 대기 자체를 만들지 않는다</text>
</svg>
```
그림 5 — 개념도(실측 아님). 위: PIP는 상속만 할 뿐 순환 대기를 막지 못한다. 아래: PCP는 H가 빈 S2를 잠그려는 순간 "지금 잠긴 S1의 ceiling이 H 이상"이라는 이유로 막는다. ICPP라면 L이 S1을 잡는 순간 이미 H의 우선순위라 H는 아예 CPU를 받지 못하고, 결과는 같다.

### 6.6 한 표로 비교

| 항목 | 상속 없음 | PIP | OCPP | ICPP |
|---|---|---|---|---|
| L의 우선순위가 오르는 때 | 없음 | 누가 L 때문에 막혔을 때 | 막혔을 때 (+ ceiling 규칙으로 막을 때) | 락을 잡는 즉시, 항상 |
| 미리 알아야 할 것 | 없음 | 없음 | 자원별 ceiling | 자원별 ceiling |
| 무한 역전 | 있음 | 없음 | 없음 | 없음 |
| H의 블로킹 횟수 | 무한 | 낮은 태스크 수와 락 수 중 작은 만큼 | 최대 1번 | 최대 1번 (시작 전) |
| 데드락 | 막지 못함 | 막지 못함 | 막음 | 막음 (단일 코어) |
| 경쟁 없을 때 비용 | 가장 쌈 | 쌈 (소유자 기록) | system ceiling 계산 | 매번 우선순위 변경 2회 |
| 대표 구현 | semaphore | FreeRTOS mutex, Zephyr `k_mutex`, VxWorks `SEM_INVERSION_SAFE`, ThreadX `TX_INHERIT`, Linux `rt_mutex` | 연구용 커널 위주 | POSIX `PTHREAD_PRIO_PROTECT`, OSEK/AUTOSAR resource, Ada `Ceiling_Locking`, RTIC(SRP) |

### 6.7 블로킹 상한 B_i 미리 보기와 예제 10

N3의 RTA 식 `R_i = C_i + B_i + Σ_{j ∈ hp(i)} ⌈R_i / T_j⌉ × C_j`에 넣을 B_i를 프로토콜마다 이렇게 잡는다(정확한 증명과 중첩 락 처리는 N4).

- **PCP(OCPP · ICPP)**: `B_i = max { cs(j, k) : j는 i보다 낮은 태스크, ceiling(k) ≥ prio(i) }`. 낮은 태스크의 "i를 막을 수 있는" 임계 구역 중 가장 긴 **하나**.
- **PIP**: 낮은 태스크마다 한 번씩 막힐 수 있고, 자원마다 한 번씩 막힐 수 있다. `B_i ≤ min( Σ_{j ∈ lp(i)} max_k cs(j, k), Σ_{k: ceiling(k) ≥ prio(i)} max_{j ∈ lp(i)} cs(j, k) )`.

무엇을 확인하는 코드인지: 드론 FC 태스크 다섯 개와 자원 세 개(SPI 버스, 상태 구조체, 로그 버퍼)로 두 상한을 계산하고, 로거의 SPI 임계 구역을 줄이면 무엇이 바뀌는지 본다.

```python
# 예제 10: 드론 FC 태스크 세트의 최악 블로킹 B_i — PIP vs PCP(OCPP/ICPP 공통 상한)
# 우선순위: 숫자가 클수록 높다. cs[태스크][자원] = 그 자원을 쥐는 가장 긴 임계 구역(us)
prio = {'rate': 5, 'estimator': 4, 'nav': 3, 'telemetry': 2, 'logger': 1}
cs = {'rate':      {},
      'estimator': {'spi': 40,  'state': 15},
      'nav':       {'state': 30},
      'telemetry': {'state': 25, 'logbuf': 60},
      'logger':    {'spi': 900, 'logbuf': 80}}    # SD 카드 블록 쓰기를 SPI mutex 안에서
res = {r for t in cs for r in cs[t]}
ceil = {r: max(prio[t] for t in cs if r in cs[t]) for r in res}   # 자원 ceiling = 쓰는 태스크 중 최고
def lower(i): return [j for j in prio if prio[j] < prio[i]]
def usable(i, j): return [r for r in cs[j] if ceil[r] >= prio[i]]  # i를 막을 수 있는 j의 구역
def b_pip(i):   # 낮은 태스크마다 한 번씩, 또는 자원마다 한 번씩 — 둘 중 작은 쪽
    by_task = sum(max([cs[j][r] for r in usable(i, j)], default=0) for j in lower(i))
    by_res = sum(max([cs[j][r] for j in lower(i) if r in cs[j]], default=0) for r in res if ceil[r] >= prio[i])
    return min(by_task, by_res)
def b_pcp(i):   # 낮은 태스크의 (ceiling >= P_i인) 임계 구역 중 가장 긴 것 하나
    return max([cs[j][r] for j in lower(i) for r in usable(i, j)], default=0)
def report(title):
    print(title)
    print('  ceiling:', ', '.join(f'{r}={ceil[r]}' for r in sorted(res)))
    print(f'  {"task":10} {"prio":>4} {"B_PIP(us)":>10} {"B_PCP(us)":>10}')
    for t in sorted(prio, key=prio.get, reverse=True):
        print(f'  {t:10} {prio[t]:>4} {b_pip(t):>10} {b_pcp(t):>10}')
report('[1] logger가 SD 블록 쓰기 전체를 SPI mutex 안에서 (900 us)')
cs['logger']['spi'] = 50   # 고친 설계: 락 안에서는 DMA 디스크립터만 걸고 완료는 세마포어로 기다린다
report('[2] logger의 SPI 임계 구역을 50 us로 줄임')
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/D3/py/bound.py
```

```text
[1] logger가 SD 블록 쓰기 전체를 SPI mutex 안에서 (900 us)
  ceiling: logbuf=2, spi=4, state=4
  task       prio  B_PIP(us)  B_PCP(us)
  rate          5          0          0
  estimator     4        930        900
  nav           3        925        900
  telemetry     2        900        900
  logger        1          0          0
[2] logger의 SPI 임계 구역을 50 us로 줄임
  ceiling: logbuf=2, spi=4, state=4
  task       prio  B_PIP(us)  B_PCP(us)
  rate          5          0          0
  estimator     4         80         50
  nav           3         75         50
  telemetry     2         80         80
  logger        1          0          0
```

출력에서 볼 것: 첫 설계에서 estimator의 B는 PIP 930 us, PCP 900 us로 **거의 같다**. 둘 다 logger의 900 us SPI 구간 하나가 지배하기 때문이다. **프로토콜을 바꾸는 것보다 그 임계 구역을 50 us로 줄이는 것**이 B를 열 배 넘게 줄였다(PIP 930 → 80, PCP 900 → 50). 또 nav는 SPI를 쓰지도 않는데 B = 900 us다. logger가 estimator의 우선순위(4)를 상속하거나(PIP의 push-through) ceiling 4로 올라가면(PCP) nav(3)도 기다리기 때문이다. 이것이 "임계 구역은 짧게"가 프로토콜 선택보다 먼저인 이유다.

---

## 7. ICPP를 FreeRTOS에서 직접 만들기

### 7.1 래퍼 하나면 된다

FreeRTOS에는 ICPP가 없지만 `vTaskPrioritySet()`으로 흉내 낼 수 있다. 예제 1의 `lock()`/`unlock()`이 그것이다. 규칙은 두 개다: **잡기 전에 올리고, 놓은 뒤에 내린다.** 순서를 바꾸면(잡은 뒤에 올리면) 잡는 순간과 올리는 순간 사이에 M이 끼어들 틈이 생긴다.

```c
static UBaseType_t lock(void) {
    UBaseType_t saved = uxTaskPriorityGet(NULL);
    if (LOCK == 2) vTaskPrioritySet(NULL, CEIL);              /* ICPP: 잡기 "전에" ceiling으로 */
    if (LOCK == 3) vTaskSuspendAll(); else xSemaphoreTake(lk, portMAX_DELAY);
    return saved;
}
```

ceiling은 그 자원을 쓰는 태스크 중 가장 높은 우선순위(여기서는 H = 3)다. 락 객체는 binary semaphore를 그대로 썼다. ICPP가 상호배제를 대신 보장하니 상속은 필요 없다.

### 7.2 예제 4 — 결과, 그리고 time slicing 함정

무엇을 확인하는 코드인지: 같은 `inv.c`를 `-DLOCK=2`로, 그리고 `-DLOCK=2 -DconfigUSE_TIME_SLICING=0`으로 빌드한다(템플릿 config는 `#ifndef`로 감싸 있어 덮어쓸 수 있다).

```text
CFG LOCK=2
R W=0 h_block_us=1064 h_resp_us=2279 m_resp_us=1291 t0=735006
R W=1 h_block_us=1064 h_resp_us=2279 m_resp_us=2292 t0=1360006
R W=2 h_block_us=1064 h_resp_us=2279 m_resp_us=3292 t0=1985006
R W=4 h_block_us=1064 h_resp_us=2279 m_resp_us=5292 t0=2610006
R W=6 h_block_us=1064 h_resp_us=2279 m_resp_us=7293 t0=3235006
R W=8 h_block_us=1064 h_resp_us=2279 m_resp_us=9294 t0=3860006
R W=10 h_block_us=1064 h_resp_us=2279 m_resp_us=11294 t0=4485006
R W=12 h_block_us=1064 h_resp_us=2279 m_resp_us=13294 t0=5110006
```

```text
CFG LOCK=2
R W=0 h_block_us=7 h_resp_us=2251 m_resp_us=1263 t0=735006
R W=4 h_block_us=7 h_resp_us=2251 m_resp_us=5263 t0=2610006
R W=12 h_block_us=7 h_resp_us=2251 m_resp_us=13261 t0=5110006
```

출력에서 볼 것: 두 경우 모두 H 응답이 W와 무관하다(2279 us / 2251 us). 그런데 time slicing이 켜진 쪽은 `h_block_us = 1064`, 꺼진 쪽은 `7`이다. trace를 보면 이유가 보인다.

```text
time slicing 켬 (W = 4 라운드)          time slicing 끔
   0.010 ms  = L prio=3                    0.010 ms  = L prio=3
   1.007 ms  S L prio=3                    3.022 ms  U L prio=3
   2.007 ms  S H prio=3                    3.028 ms  = L prio=1
   2.008 ms  q H prio=3                    3.034 ms  S H prio=3
   2.030 ms  S L prio=3                    3.036 ms  q H prio=3
   3.055 ms  U L prio=3                    3.044 ms  a H prio=3
   3.069 ms  S H prio=3
   3.072 ms  a H prio=3
```

(두 trace에서 핵심 줄만 골라 나란히 놓았다.) ceiling = H의 우선순위이므로 임계 구역 안의 L과 깨어난 H가 **같은 우선순위**가 된다. FreeRTOS는 `configUSE_TIME_SLICING=1`이면 같은 우선순위끼리 tick마다 돌아가며 실행하므로(C4), 2.007 ms tick에 H가 CPU를 받아 락을 요청하고 막혔다가 다시 L로 돌아간다. 정확성은 지켜진다(락 객체가 있어서 H가 막힌다). 하지만 쓸데없는 switch 두 번과 "ICPP인데 H가 락에서 막히는" 이상한 trace가 생긴다. 8 라운드 전체의 switch-in 수는 이렇다.

| 방식 | switch-in 수 (8 라운드) | H 응답 (us) |
|---|---|---|
| binary semaphore | 70 | 2280 ~ 14283 |
| mutex (PIP) | 62 | 2270 |
| ICPP, time slicing 켬 | 70 | 2279 |
| ICPP, time slicing 끔 | 46 | 2251 |
| `vTaskSuspendAll` (§10) | 46 | 2235 |

### 7.3 언제 락 객체를 아예 뺄 수 있나

교과서 ICPP는 "우선순위만 올리면 상호배제가 된다"고 말한다. 단일 코어에서 ceiling 이상인 태스크는 그 자원을 쓰지 않으니, L이 ceiling에서 도는 동안 다른 사용자는 CPU를 받을 수 없기 때문이다. 하지만 FreeRTOS에서 이 주장이 성립하려면 세 조건이 모두 필요하다.

1. **단일 코어** (SMP에서는 다른 코어가 같은 자원 사용자를 돌린다, C10).
2. **임계 구역 안에서 블록하지 않는다.** L이 구역 안에서 DMA 완료를 기다리며 block하면, ceiling보다 낮은 다른 사용자가 CPU를 받아 같은 자원에 들어온다.
3. **ceiling과 같은 우선순위 태스크와의 time slicing이 없다.** 위 실험처럼 같은 우선순위의 사용자가 tick에 끼어든다.

이 중 하나라도 깨질 수 있으면 락 객체를 남겨 둔다(예제 4처럼). 이것은 OSEK/AUTOSAR가 resource를 잡은 상태에서 대기를 금지하는 이유이기도 하다(I5).

### 7.4 예제 9 — 경쟁이 없을 때의 비용

무엇을 확인하는 코드인지: 경쟁 없는 lock/unlock 한 쌍을 1000번 돌려 평균 cycle을 잰다. `switches`는 그동안 `traceTASK_SWITCHED_IN`이 불린 횟수(FreeRTOS는 같은 태스크가 다시 선택돼도 불린다)다.

```c
/* 예제 8: 경쟁 없는 lock/unlock 한 쌍의 비용 (1000회 평균, TIMER1 25 MHz cycle, QEMU icount 기준) */
#include "d3.h"
enum { N = 1000 };
static SemaphoreHandle_t bs, mx;
static volatile uint32_t shared;
#define BENCH(name, LOCK, UNLOCK) do { d3_i = 0; uint32_t t_ = D3_CYC();           \
        for (uint32_t i = 0; i < N; i++) { LOCK; shared++; UNLOCK; }                \
        uint32_t c_ = D3_CYC() - t_;                                                 \
        board_printf_raw("%s: %u cycles/pair (%u ns), switches=%u\n", name, c_ / N, c_ / N * 40u, d3_i); } while (0)
static void tB(void *p) {
    (void)p; UBaseType_t s = 0;
    BENCH("empty loop (baseline)        ", (void)0, (void)0);
    BENCH("taskENTER/EXIT_CRITICAL      ", taskENTER_CRITICAL(), taskEXIT_CRITICAL());
    BENCH("vTaskSuspendAll/xTaskResumeAll", vTaskSuspendAll(), (void)xTaskResumeAll());
    BENCH("binary semaphore take/give   ", xSemaphoreTake(bs, portMAX_DELAY), xSemaphoreGive(bs));
    BENCH("mutex (PIP) take/give        ", xSemaphoreTake(mx, portMAX_DELAY), xSemaphoreGive(mx));
    BENCH("ICPP: prio set x2 only       ", (s = uxTaskPriorityGet(NULL), vTaskPrioritySet(NULL, 5)),
          vTaskPrioritySet(NULL, s));
    BENCH("ICPP: prio set x2 + bin sem  ",
          (s = uxTaskPriorityGet(NULL), vTaskPrioritySet(NULL, 5), xSemaphoreTake(bs, portMAX_DELAY)),
          (xSemaphoreGive(bs), vTaskPrioritySet(NULL, s)));
    board_exit(0);
}
int main(void) {
    bs = xSemaphoreCreateBinary(); xSemaphoreGive(bs); mx = xSemaphoreCreateMutex();
    xTaskCreate(tB, "B", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```text
empty loop (baseline)        : 4 cycles/pair (160 ns), switches=0
taskENTER/EXIT_CRITICAL      : 26 cycles/pair (1040 ns), switches=0
vTaskSuspendAll/xTaskResumeAll: 58 cycles/pair (2320 ns), switches=0
binary semaphore take/give   : 129 cycles/pair (5160 ns), switches=0
mutex (PIP) take/give        : 153 cycles/pair (6120 ns), switches=0
ICPP: prio set x2 only       : 330 cycles/pair (13200 ns), switches=1000
ICPP: prio set x2 + bin sem  : 455 cycles/pair (18200 ns), switches=1000
```

무엇을 확인하는 코드인지: Zephyr에서 같은 측정. ICPP 흉내는 `k_thread_priority_set()` 두 번이다(스레드는 prio 5, ceiling 1).

```c
/* 예제 8Z: Zephyr에서 경쟁 없는 lock/unlock 한 쌍의 비용 (1000회 평균, k_cycle_get_32 = 25 MHz) */
#include "common.h"
enum { N = 1000 };
K_SEM_DEFINE(sem, 1, 1);
K_MUTEX_DEFINE(mtx);
static volatile uint32_t shared;
static char name_of(struct k_thread *c) { ARG_UNUSED(c); return 'B'; }
#define BENCH(name, LOCK, UNLOCK) do { ti = 0; uint32_t t_ = k_cycle_get_32();             \
	for (uint32_t i = 0; i < N; i++) { LOCK; shared++; UNLOCK; }                        \
	uint32_t c_ = k_cycle_get_32() - t_;                                                \
	printk("%s: %u cycles/pair (%u ns), switches=%u\n", name, c_ / N, c_ / N * 40u, ti); } while (0)
int main(void) {
	k_thread_priority_set(k_current_get(), 5);          /* 보통 선점형 스레드처럼 */
	int s = k_thread_priority_get(k_current_get());
	BENCH("empty loop (baseline)      ", (void)0, (void)0);
	BENCH("irq_lock/irq_unlock        ", unsigned int k = irq_lock(), irq_unlock(k));
	BENCH("k_sched_lock/unlock        ", k_sched_lock(), k_sched_unlock());
	BENCH("k_sem take/give            ", k_sem_take(&sem, K_FOREVER), k_sem_give(&sem));
	BENCH("k_mutex lock/unlock (PIP)  ", k_mutex_lock(&mtx, K_FOREVER), k_mutex_unlock(&mtx));
	BENCH("ICPP: k_thread_priority_set x2",
	      k_thread_priority_set(k_current_get(), 1), k_thread_priority_set(k_current_get(), s));
	ti = 0; dump_exit();
	return 0;
}
```

```text
empty loop (baseline)      : 4 cycles/pair (160 ns), switches=0
irq_lock/irq_unlock        : 8 cycles/pair (320 ns), switches=0
k_sched_lock/unlock        : 44 cycles/pair (1760 ns), switches=0
k_sem take/give            : 32 cycles/pair (1280 ns), switches=0
k_mutex lock/unlock (PIP)  : 53 cycles/pair (2120 ns), switches=0
ICPP: k_thread_priority_set x2: 132 cycles/pair (5280 ns), switches=0
```

출력에서 볼 것: FreeRTOS mutex(153 cycle)는 binary semaphore(129)보다 약 24 cycle 비싸다. 소유자 기록(`pvTaskIncrementMutexHeldCount`)과 상속 확인 비용이다. 반면 `vTaskPrioritySet` 두 번은 **330 cycle에 switch 1000번**이다. `vTaskPrioritySet()`은 실행 중인 태스크의 우선순위를 **내릴 때** 무조건 `xYieldRequired = pdTRUE`로 yield하기 때문에(§11) 매번 PendSV → `vTaskSwitchContext()`를 한 번 거친다(같은 태스크가 다시 뽑힌다). Zephyr는 `k_mutex`(53)가 `k_sem`(32)보다 조금 비싸고, 우선순위 변경 두 번은 132 cycle이다.

```svg
<svg viewBox="0 0 680 302" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">FreeRTOS V11.3.2</text>
<text x="222" y="41" font-size="12" text-anchor="end">taskENTER/EXIT_CRITICAL</text>
<rect x="230" y="30" width="16.0" height="14" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="252.0" y="41" font-size="12">26 cyc (1.04 us)</text>
<text x="222" y="61" font-size="12" text-anchor="end">vTaskSuspendAll/ResumeAll</text>
<rect x="230" y="50" width="35.7" height="14" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="271.7" y="61" font-size="12">58 cyc (2.32 us)</text>
<text x="222" y="81" font-size="12" text-anchor="end">binary sem take/give</text>
<rect x="230" y="70" width="79.4" height="14" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="315.4" y="81" font-size="12">129 cyc (5.16 us)</text>
<text x="222" y="101" font-size="12" text-anchor="end">mutex take/give (PIP)</text>
<rect x="230" y="90" width="94.2" height="14" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="330.2" y="101" font-size="12">153 cyc (6.12 us)</text>
<text x="222" y="121" font-size="12" text-anchor="end">ICPP: vTaskPrioritySet x2</text>
<rect x="230" y="110" width="203.1" height="14" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="439.1" y="121" font-size="12">330 cyc (13.20 us)</text>
<text x="222" y="141" font-size="12" text-anchor="end">ICPP: PrioritySet x2 + sem</text>
<rect x="230" y="130" width="280.0" height="14" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="516.0" y="141" font-size="12">455 cyc (18.20 us)</text>
<text x="10" y="166" font-size="13" font-weight="bold">Zephyr v4.4.2</text>
<text x="222" y="187" font-size="12" text-anchor="end">irq_lock/unlock</text>
<rect x="230" y="176" width="4.9" height="14" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="240.9" y="187" font-size="12">8 cyc (0.32 us)</text>
<text x="222" y="207" font-size="12" text-anchor="end">k_sched_lock/unlock</text>
<rect x="230" y="196" width="27.1" height="14" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="263.1" y="207" font-size="12">44 cyc (1.76 us)</text>
<text x="222" y="227" font-size="12" text-anchor="end">k_sem take/give</text>
<rect x="230" y="216" width="19.7" height="14" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="255.7" y="227" font-size="12">32 cyc (1.28 us)</text>
<text x="222" y="247" font-size="12" text-anchor="end">k_mutex lock/unlock (PIP)</text>
<rect x="230" y="236" width="32.6" height="14" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="268.6" y="247" font-size="12">53 cyc (2.12 us)</text>
<text x="222" y="267" font-size="12" text-anchor="end">ICPP: priority_set x2</text>
<rect x="230" y="256" width="81.2" height="14" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="317.2" y="267" font-size="12">132 cyc (5.28 us)</text>
<text x="10" y="292" font-size="12">경쟁 없는 lock/unlock 한 쌍, 1000회 평균. 1 cycle = 25 MHz 타이머 40 ns, QEMU icount 기준</text>
</svg>
```
그림 6 — 예제 9의 측정값. 주황 막대가 ICPP 흉내. FreeRTOS에서 `vTaskPrioritySet`으로 만든 ICPP는 PIP mutex의 2~3배 비용이다. 경쟁이 드문 자원이라면 PIP가 싸고, ICPP의 장점(블로킹 1회, 데드락 없음, 예측 가능성)이 필요한 곳에만 쓴다. QEMU icount 기준이라 실칩 cycle과 절대값은 다르다.

> 실무 판단: FreeRTOS에서는 **기본은 mutex(PIP)**, 여러 락이 얽혀 데드락 · chained blocking 분석이 필요하거나 인증 문서에 "PCP 사용"을 써야 하는 자원에만 ICPP 래퍼를 쓴다. 아주 짧은(수 us) 공유 변수라면 둘 다 아니고 `taskENTER_CRITICAL`(26 cycle) 또는 `vTaskSuspendAll`이 맞다. 크리티컬 섹션은 "모두를 막는 ceiling = 최고 우선순위의 ICPP"로 볼 수 있다(C7).

---

## 8. 구현의 한계 1 — 두 개 이상의 mutex를 쥐면

§6.1의 규칙 3("놓은 락 때문에 받은 것만 내려놓는다")을 정확히 하려면 커널이 "쥔 락마다 최고 대기자가 누구인가"를 추적해야 한다. FreeRTOS와 Zephyr는 둘 다 이것을 하지 않고 **서로 다른 방향으로 단순화**했다. 그래서 같은 코드가 한쪽에서는 너무 오래 올라가 있고, 다른 쪽에서는 너무 일찍 내려온다.

### 8.1 FreeRTOS — "쥔 mutex가 0개가 될 때까지 내리지 않는다"

`tasks.c`의 `xTaskPriorityDisinherit()`는 TCB의 카운터 `uxMutexesHeld`를 하나 줄이고, 상속받은 상태라도 **카운터가 0일 때만** base로 내린다. 주석이 그대로 "Only disinherit if no other mutexes are held."다. 타임아웃 경로의 `vTaskPriorityDisinheritAfterTimeout()`는 "This is a simplification in the priority inheritance implementation"이라고 스스로 밝히며, 쥔 mutex가 **정확히 1개일 때만** 내린다.

### 8.2 예제 5 — FreeRTOS 중첩 mutex 네 가지 경우

무엇을 확인하는 코드인지: L(1)이 A(바깥) → B(안쪽) 순서로 잡고, H(3)가 둘 중 하나를 기다리게 만든 뒤, L이 하나씩 놓을 때마다 L의 우선순위와 M(2, 3 ms에 릴리스)이 언제 도는지 RAM 로그로 남긴다.

```c
/* 예제 5: 중첩 mutex 두 개 A(바깥)·B(안쪽) — FreeRTOS 상속이 언제 풀리나 (uxMutexesHeld)
 *  MODE 1: H가 안쪽 B를 기다림, L은 B를 먼저 돌려주고 A를 쥔 채 3 ms 더 일
 *  MODE 2: H가 바깥 A를 기다림, L은 B를 먼저 돌려줌      MODE 3·4: H가 A를 1 tick 타임아웃으로 기다림(4는 L이 B도 쥠) */
#include "d3.h"
#ifndef MODE
#define MODE 1
#endif
static SemaphoreHandle_t A, B;
static TaskHandle_t hL;
static volatile uint32_t t0;
static struct { uint32_t t; char who; const char *what; UBaseType_t pl; } lg[16]; static uint32_t nl;
static void say(const char *what) {          /* RAM에 기록만, 출력은 끝나고 */
    lg[nl].t = D3_CYC(); lg[nl].who = pcTaskGetName(NULL)[0]; lg[nl].what = what; lg[nl++].pl = uxTaskPriorityGet(hL);
}
static void show(void) {
    for (uint32_t i = 0; i < nl; i++)
        board_printf_raw("%u us  %c  %s  -> prio(L)=%u\n", (lg[i].t - t0) / 25u, lg[i].who, lg[i].what, lg[i].pl);
}
static void tL(void *p) {
    (void)p; vTaskDelay(10); t0 = D3_CYC();
    xSemaphoreTake(A, portMAX_DELAY);
    if (MODE != 3) xSemaphoreTake(B, portMAX_DELAY);
    say(MODE == 3 ? "took A" : "took A, B"); work_us(2000);
    if (MODE != 3) { xSemaphoreGive(B); say("gave B"); }
    work_us(3000);
    say("giving A"); xSemaphoreGive(A);
    vTaskDelay(10); show(); dump_trace(); board_printf_raw("END\n"); board_exit(0);
}
static void tH(void *p) {
    (void)p; vTaskDelay(11);
    SemaphoreHandle_t want = (MODE == 1) ? B : A;
    say(MODE == 1 ? "take B ..." : "take A ...");
    TickType_t to = (MODE >= 3) ? 1 : portMAX_DELAY;
    if (xSemaphoreTake(want, to) == pdTRUE) { say("got it"); work_us(200); xSemaphoreGive(want); }
    else say("timed out");
    vTaskSuspend(NULL);
}
static void tM(void *p) {
    (void)p; vTaskDelay(13); say("M running"); work_us(1000); say("M done"); vTaskSuspend(NULL);
}
int main(void) {
    calibrate(); A = xSemaphoreCreateMutex(); B = xSemaphoreCreateMutex();
    board_printf_raw("MODE %d (L=1 M=2 H=3)\n", MODE);
    xTaskCreate(tL, "L", 512, NULL, 1, &hL);
    xTaskCreate(tM, "M", 512, NULL, 2, NULL);
    xTaskCreate(tH, "H", 512, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
# runf.sh = §2.2의 make + ICOUNT=1 rtos-run-mps2 두 줄을 묶은 스크립트 (산출물 /tmp/rtos_d3/<이름>/)
for m in 1 2 3 4; do runf.sh nest$m nest.c "-DMODE=$m"; done
```

```text
MODE 1 (L=1 M=2 H=3)
5 us  L  took A, B  -> prio(L)=1
1000 us  H  take B ...  -> prio(L)=1
2044 us  L  gave B  -> prio(L)=3
3001 us  H  got it  -> prio(L)=3
5269 us  L  giving A  -> prio(L)=3
5281 us  M  M running  -> prio(L)=1
6284 us  M  M done  -> prio(L)=1

MODE 2 (L=1 M=2 H=3)
5 us  L  took A, B  -> prio(L)=1
1000 us  H  take A ...  -> prio(L)=1
2042 us  L  gave B  -> prio(L)=3
5047 us  L  giving A  -> prio(L)=3
5063 us  H  got it  -> prio(L)=1
5278 us  M  M running  -> prio(L)=1
6281 us  M  M done  -> prio(L)=1

MODE 3 (L=1 M=2 H=3)
2 us  L  took A  -> prio(L)=1
1000 us  H  take A ...  -> prio(L)=1
2014 us  H  timed out  -> prio(L)=1
2999 us  M  M running  -> prio(L)=1
4002 us  M  M done  -> prio(L)=1
6089 us  L  giving A  -> prio(L)=1

MODE 4 (L=1 M=2 H=3)
5 us  L  took A, B  -> prio(L)=1
1000 us  H  take A ...  -> prio(L)=1
2011 us  H  timed out  -> prio(L)=3
2071 us  L  gave B  -> prio(L)=3
5077 us  L  giving A  -> prio(L)=3
5089 us  M  M running  -> prio(L)=1
6092 us  M  M done  -> prio(L)=1
```

출력에서 볼 것 (로그의 prio(L)은 그 줄을 기록한 순간의 값이다).

- **MODE 1 (H가 안쪽 B를 기다림)**: L이 2044 us에 B를 돌려줬는데 prio(L)=3 그대로다. A를 아직 쥐고 있어 `uxMutexesHeld = 1`이기 때문이다. 결과가 두 가지로 나빠졌다. 첫째, B를 받은 H가 같은 우선순위 3이라 바로 선점하지 못하고 **다음 tick(3001 us, time slicing)** 에야 돈다. 둘째, M은 3000 us에 릴리스됐지만 L이 3으로 계속 버텨서 **5281 us**까지 기다렸다. 아무도 L을 기다리지 않는데 L이 H의 우선순위로 2.2 ms를 더 돈 것이다(과잉 상속).
- **MODE 2 (H가 바깥 A를 기다림)**: B를 돌려줘도 3 유지, A를 돌려줄 때 1로. 이 경우는 정확히 맞는 동작이다.
- **MODE 3 (A 하나만 쥠, H가 1 tick 타임아웃)**: H가 2014 us에 timed out 되자 L이 즉시 1로 돌아갔고 M이 2999 us에 제때 돌았다. `vTaskPriorityDisinheritAfterTimeout()`가 일한 것이다.
- **MODE 4 (A · B를 쥠, H가 타임아웃)**: H가 포기했는데도 L은 3에 머문다(쥔 mutex가 2개라 timeout 경로가 내리지 않음). B를 놓아도(1개 남음) 그대로다. M은 3 ms 릴리스에서 **5089 us**까지 밀렸다. 기다리는 사람이 아무도 없는 상태로 L이 2 ms 넘게 최고 우선순위에서 돌았다.

### 8.3 Zephyr — "mutex마다 잡을 때의 우선순위를 기억했다가 그 값으로 돌아간다"

Zephyr `struct k_mutex`에는 `owner_orig_prio` 필드가 있다. `z_impl_k_mutex_lock()`은 락이 비어 있을 때 잡으면서 **그 순간 내 `base.prio`** 를 이 필드에 적는다. `z_impl_k_mutex_unlock()`은 무조건 `adjust_owner_prio(mutex, mutex->owner_orig_prio)`를 부른다. 즉 "이 mutex를 잡던 순간의 우선순위"로 되돌린다. 다른 mutex에 걸린 대기자는 보지 않는다. `kernel/mutex.c` 맨 위 주석이 사용 규칙을 명시한다.

> Each mutex that contributes to priority inheritance must be released in the reverse order in which it was acquired. Furthermore each subsequent mutex that contributes to raising the owning thread's priority level must be acquired at a point after the most recent "bumping" of the priority level. ... Failure to follow this nested model may result in threads running at unexpected priority levels (too high, or too low).

### 8.4 예제 6 — Zephyr 중첩 `k_mutex`

무엇을 확인하는 코드인지: FreeRTOS 예제 5와 같은 구성. L(5)이 A → B를 잡을 때는 아직 아무도 기다리지 않는다(부스트 전에 B를 잡음 = 위 규칙의 둘째 문장 위반). SCEN 3은 H(1)가 바깥 A를, SCEN 4는 안쪽 B를 기다린다. SCEN 5는 3과 같되 M을 2 ms(L이 아직 부스트된 때)에 릴리스한다.

```c
/* 예제 5Z: Zephyr 중첩 k_mutex A(바깥)·B(안쪽). L은 A→B 순으로 잡고(아직 부스트 전) B를 먼저 푼다
 *   SCEN 3: H가 바깥 A를 기다림 · 4: H가 안쪽 B를 기다림 · 5: 3과 같되 M이 2 ms에 릴리스. H=1 M=3 L=5 */
#include "common.h"
K_MUTEX_DEFINE(A);
K_MUTEX_DEFINE(B);
static struct k_thread th[3]; static K_THREAD_STACK_ARRAY_DEFINE(stk, 3, 1024);
static char name_of(struct k_thread *c) { return c == &th[0] ? 'L' : c == &th[1] ? 'M' : c == &th[2] ? 'H' : 'I'; }
static struct { uint32_t t; char who; const char *what; int pl; } lg[16]; static uint32_t nl, t0;
static void say(const char *what) {
	lg[nl].t = k_cycle_get_32(); lg[nl].who = name_of(k_current_get());
	lg[nl].what = what; lg[nl++].pl = k_thread_priority_get(&th[0]);
}
static void at(int64_t tick) { k_sleep(K_TIMEOUT_ABS_TICKS(tick)); }
static void fL(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	at(100); t0 = k_cycle_get_32();
	k_mutex_lock(&A, K_FOREVER); k_mutex_lock(&B, K_FOREVER); say("took A, B"); work_us(2000);
	k_mutex_unlock(&B); say("gave B"); work_us(3000);
	say("giving A"); k_mutex_unlock(&A);
	at(120);
	printk("SCEN %d (H=1 M=3 L=5)\n", SCEN);
	for (uint32_t i = 0; i < nl; i++)
		printk("%u us  %c  %s  -> prio(L)=%d\n", (lg[i].t - t0) / 25u, lg[i].who, lg[i].what, lg[i].pl);
	dump_exit();
}
static void fH(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct k_mutex *want = (SCEN == 4) ? &B : &A;
	at(101); say(SCEN == 4 ? "lock B ..." : "lock A ...");
	k_mutex_lock(want, K_FOREVER); say("got it"); work_us(200); k_mutex_unlock(want);
}
static void fM(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c); at(SCEN == 5 ? 102 : 103); say("M running"); work_us(1000); say("M done"); }
int main(void) {
	calibrate();
	k_thread_create(&th[0], stk[0], 1024, fL, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&th[1], stk[1], 1024, fM, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
	k_thread_create(&th[2], stk[2], 1024, fH, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
	return 0;
}
```

```text
SCEN 3 (H=1 M=3 L=5)
3 us  L  took A, B  -> prio(L)=5
1000 us  H  lock A ...  -> prio(L)=5
2031 us  L  gave B  -> prio(L)=5
2999 us  M  M running  -> prio(L)=5
4001 us  M  M done  -> prio(L)=5
6054 us  L  giving A  -> prio(L)=5
6065 us  H  got it  -> prio(L)=5

SCEN 4 (H=1 M=3 L=5)
3 us  L  took A, B  -> prio(L)=5
1000 us  H  lock B ...  -> prio(L)=5
2038 us  H  got it  -> prio(L)=5
2249 us  L  gave B  -> prio(L)=5
2999 us  M  M running  -> prio(L)=5
4001 us  M  M done  -> prio(L)=5
6272 us  L  giving A  -> prio(L)=5

SCEN 5 (H=1 M=3 L=5)
3 us  L  took A, B  -> prio(L)=5
1000 us  H  lock A ...  -> prio(L)=5
2040 us  L  gave B  -> prio(L)=5
5040 us  L  giving A  -> prio(L)=5
5050 us  H  got it  -> prio(L)=5
5262 us  M  M running  -> prio(L)=5
6263 us  M  M done  -> prio(L)=5
```

출력에서 볼 것 (prio(L)은 로그를 남긴 스레드가 기록한 순간의 값이라 L이 부스트된 동안의 값은 trace로 본다).

- **SCEN 3**: trace에서 H가 막힌 직후 L이 prio 1로 switch-in 한다(`T 2525614 S L 1`). 그런데 L이 B를 놓는 2031 us에 `owner_orig_prio`(B를 잡을 때의 5)로 **떨어진다**. A에는 여전히 H가 기다리는데. 3 ms에 M(3)이 L(5)을 선점해 1 ms를 돌고, H는 **6065 us**에야 A를 받는다. M의 일만큼 H가 늦었다. 즉 **무한 역전이 다시 열렸다.** FreeRTOS MODE 2(같은 상황)는 정확했다.
- **SCEN 4**: H가 B를 기다리는 경우는 반대로 Zephyr가 이상적이다. B를 놓는 순간 새 owner H가 깨어나 2038 us에 바로 돌고, L은 5로 돌아가 M이 3 ms에 제때 돈다. FreeRTOS MODE 1(같은 상황)은 과잉 상속으로 M을 2.3 ms 늦췄다.
- **SCEN 5**: B를 놓아 L이 5로 떨어진 2040 us에 이미 M(3)이 ready였는데도 **M이 돌지 않았다**. M은 5262 us에야 돌았다. `z_impl_k_mutex_unlock()`은 대기자가 없으면 `adjust_owner_prio()`로 우선순위만 바꾸고 `z_reschedule()` 없이 `k_spin_unlock()`으로 끝나기 때문이다(§11). tickless 커널이라 다음 인터럽트도 없었다. 결과적으로 "ready 중 최고 우선순위가 돈다"는 약속이 약 3 ms 동안 깨졌다. 이 실험에서는 H가 손해 보지 않았지만, M이 마감이 있는 태스크였다면 그만큼 늦는다.

```svg
<svg viewBox="0 0 680 256" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13" font-weight="bold">(a) FreeRTOS: H가 안쪽 B를 기다림 → B를 돌려줘도 L은 3에 머문다</text>
<text x="58" y="40" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="44" x2="650" y2="44" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="62" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="66" x2="650" y2="66" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="84" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="88" x2="650" y2="88" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.0" y="74" width="82.9" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="152.9" y="30" width="2.0" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="154.8" y="74" width="163.7" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="318.6" y="30" width="17.9" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="336.4" y="74" width="170.8" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="507.2" y="74" width="0.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="507.6" y="52" width="84.0" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="591.6" y="74" width="0.7" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="152.9" y1="26" x2="152.9" y2="46" stroke="currentColor" stroke-width="1.5"/>
<text x="155.9" y="27" font-size="12">B 요청</text>
<line x1="239.0" y1="70" x2="239.0" y2="90" stroke="currentColor" stroke-width="1.5"/>
<text x="242.0" y="71" font-size="12">B 반환</text>
<line x1="318.6" y1="48" x2="318.6" y2="68" stroke="currentColor" stroke-width="1.5"/>
<text x="321.6" y="49" font-size="12">릴리스</text>
<line x1="318.6" y1="59" x2="507.5" y2="59" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="413.0" y="56" font-size="12" text-anchor="middle">M 대기 2.28 ms</text>
<text x="10" y="114" font-size="13" font-weight="bold">(b) Zephyr: H가 바깥 A를 기다림 → B를 돌려주는 순간 L이 5로 떨어진다</text>
<text x="58" y="136" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="140" x2="650" y2="140" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="158" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="162" x2="650" y2="162" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="180" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="184" x2="650" y2="184" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.0" y="170" width="82.9" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="152.9" y="126" width="1.0" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="153.9" y="170" width="84.4" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="238.3" y="170" width="80.3" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="318.5" y="148" width="83.8" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="402.4" y="170" width="170.2" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="572.6" y="126" width="17.5" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="590.0" y="170" width="1.0" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="152.9" y1="122" x2="152.9" y2="142" stroke="currentColor" stroke-width="1.5"/>
<text x="155.9" y="123" font-size="12">A 요청</text>
<line x1="238.2" y1="166" x2="238.2" y2="186" stroke="currentColor" stroke-width="1.5"/>
<text x="241.2" y="167" font-size="12">B 반환</text>
<line x1="572.1" y1="122" x2="572.1" y2="142" stroke="currentColor" stroke-width="1.5"/>
<text x="575.1" y="123" font-size="12">획득</text>
<line x1="152.9" y1="133" x2="572.1" y2="133" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="362.5" y="130" font-size="12" text-anchor="middle">H 대기 5.06 ms</text>
<line x1="70" y1="198" x2="650" y2="198" stroke="currentColor"/>
<line x1="70.0" y1="198" x2="70.0" y2="202" stroke="currentColor"/>
<text x="70.0" y="215" font-size="12" text-anchor="middle">0</text>
<line x1="152.9" y1="198" x2="152.9" y2="202" stroke="currentColor"/>
<text x="152.9" y="215" font-size="12" text-anchor="middle">1</text>
<line x1="235.7" y1="198" x2="235.7" y2="202" stroke="currentColor"/>
<text x="235.7" y="215" font-size="12" text-anchor="middle">2</text>
<line x1="318.6" y1="198" x2="318.6" y2="202" stroke="currentColor"/>
<text x="318.6" y="215" font-size="12" text-anchor="middle">3</text>
<line x1="401.4" y1="198" x2="401.4" y2="202" stroke="currentColor"/>
<text x="401.4" y="215" font-size="12" text-anchor="middle">4</text>
<line x1="484.3" y1="198" x2="484.3" y2="202" stroke="currentColor"/>
<text x="484.3" y="215" font-size="12" text-anchor="middle">5</text>
<line x1="567.1" y1="198" x2="567.1" y2="202" stroke="currentColor"/>
<text x="567.1" y="215" font-size="12" text-anchor="middle">6</text>
<line x1="650.0" y1="198" x2="650.0" y2="202" stroke="currentColor"/>
<text x="650.0" y="215" font-size="12" text-anchor="middle">7</text>
<text x="650" y="230" font-size="12" text-anchor="end">시간 (ms, 라운드 시작 = L 릴리스)</text>
<text x="70" y="244" font-size="12">빨강 칠 = 상속받은 우선순위. (a)는 너무 오래 올라가 있고, (b)는 너무 일찍 내려온다</text>
</svg>
```
그림 7 — 예제 5 MODE 1과 예제 6 SCEN 3의 trace(QEMU icount 기준). (a) FreeRTOS는 B를 돌려준 뒤에도 L을 3으로 유지해 M을 2.28 ms 늦춘다(과잉). (b) Zephyr는 B를 돌려주는 순간 L을 5로 내려 M이 L을 선점하고, A를 기다리던 H가 5.06 ms를 기다린다(과소). Zephyr 쪽 우선순위 하강 시점(2.03 ms)은 trace 훅이 없어서 앱 로그의 "gave B" 시각으로 표시했다.

### 8.5 정리 — 2 × 2 표

| 상황 (L이 A → B 순서로 잡음, B를 먼저 놓음) | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| H가 안쪽 B를 기다림 | 과잉: B를 놓아도 A를 놓을 때까지 H의 우선순위 유지, M 2.28 ms 지연 | 정확 |
| H가 바깥 A를 기다림 | 정확 | 과소: B를 놓는 순간 원래 우선순위로, M이 끼어들어 H 1 ms 추가 지연 |
| H가 타임아웃으로 포기 (L이 하나만 쥠) | 정확: 다음 대기자 기준으로 내림 | 정확: 다음 대기자 기준으로 내림 (`new_prio_for_inheritance`) |
| H가 타임아웃으로 포기 (L이 둘 쥠) | 과잉: 쥔 mutex가 0개가 될 때까지 유지 | 잡은 순서 규칙을 지키면 정확 |
| 우선순위가 내려간 뒤 재스케줄 | `xTaskPriorityDisinherit`가 pdTRUE → yield | 대기자 없으면 재스케줄 안 함 (SCEN 5) |

실무 규칙은 그래서 단순하다.

1. **mutex를 두 개 이상 동시에 쥐지 않도록 설계한다.** 꼭 필요하면 하나의 락으로 묶는다.
2. 그래도 중첩한다면 **항상 같은 순서로 잡고 역순(LIFO)으로 놓는다**(데드락 방지, D9). Zephyr에서는 이것이 상속 정확성의 전제 조건이기도 하다.
3. FreeRTOS에서 중첩 구간은 짧게 유지한다. 과잉 상속은 "가장 바깥 mutex를 놓을 때"까지 이어진다.
4. 제어 루프 같은 태스크에서 mutex 타임아웃을 정상 경로로 쓰지 않는다. 타임아웃 뒤 상속 해제가 구현마다 다르다.

---

## 9. 구현의 한계 2 — 전이적 상속(사슬)을 따라가나?

### 9.1 실험 — H →(B)→ M →(A)→ L

1. L(가장 낮음)이 A를 잡고 4 ms 일을 한다.
2. M이 B를 잡은 뒤 A를 요청 → 막힘. L이 M의 우선순위를 상속.
3. H가 B를 요청 → 막힘. M이 H의 우선순위를 상속. **교과서 PIP(전이적)라면 L도 H까지 올라가야 한다.**
4. 락을 쓰지 않는 X(M과 H 사이 우선순위)가 깨어나 W ms 일을 한다. L이 H까지 올라가 있었다면 X는 L을 선점하지 못한다.

### 9.2 예제 7 — FreeRTOS와 Zephyr에서 3단 사슬

무엇을 확인하는 코드인지: FreeRTOS(L 1, M 2, X 3, H 4). X가 시작하는 순간 L과 M의 우선순위를 읽고, H가 B를 요청해서 받기까지의 시간을 잰다.

```c
/* 예제 6: 3단 사슬 H →(B)→ M →(A)→ L. FreeRTOS 상속은 사슬을 따라가나?
 *  L(1)이 A를 쥠 → M(2)이 B를 쥐고 A를 기다림 → H(4)가 B를 기다림 → X(3)가 W ms 일(락 없음) */
#include "d3.h"
#ifndef W
#define W 5
#endif
static SemaphoreHandle_t A, B;
static TaskHandle_t hL, hM;
static volatile uint32_t t0, hreq, hacq;
static uint32_t pl_at_x, pm_at_x;
static void tL(void *p) {
    (void)p; vTaskDelay(10); t0 = D3_CYC();
    xSemaphoreTake(A, portMAX_DELAY); work_us(4000); xSemaphoreGive(A);
    vTaskDelay(30);
    board_printf_raw("W=%u ms: prio when X starts L=%u M=%u, H blocked %u us (released->got B)\n",
                     W, pl_at_x, pm_at_x, (hacq - hreq) / 25u);
    dump_trace(); board_printf_raw("END\n"); board_exit(0);
}
static void tM(void *p) {
    (void)p; vTaskDelay(11);
    xSemaphoreTake(B, portMAX_DELAY); xSemaphoreTake(A, portMAX_DELAY);   /* B 쥔 채 A 대기 */
    work_us(300); xSemaphoreGive(A); xSemaphoreGive(B);
    vTaskSuspend(NULL);
}
static void tH(void *p) {
    (void)p; vTaskDelay(12); hreq = D3_CYC();
    xSemaphoreTake(B, portMAX_DELAY); hacq = D3_CYC(); work_us(200); xSemaphoreGive(B);
    vTaskSuspend(NULL);
}
static void tX(void *p) {
    (void)p; vTaskDelay(13);
    pl_at_x = uxTaskPriorityGet(hL); pm_at_x = uxTaskPriorityGet(hM);
    work_us(W * 1000u); vTaskSuspend(NULL);
}
int main(void) {
    calibrate(); A = xSemaphoreCreateMutex(); B = xSemaphoreCreateMutex();
    xTaskCreate(tL, "L", 512, NULL, 1, &hL);
    xTaskCreate(tM, "M", 512, NULL, 2, &hM);
    xTaskCreate(tX, "X", 512, NULL, 3, NULL);
    xTaskCreate(tH, "H", 512, NULL, 4, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```text
W=0 ms: prio when X starts L=2 M=4, H blocked 2409 us (released->got B)
W=5 ms: prio when X starts L=2 M=4, H blocked 7411 us (released->got B)
W=10 ms: prio when X starts L=2 M=4, H blocked 12412 us (released->got B)
```

무엇을 확인하는 코드인지: 같은 사슬을 Zephyr(L 4, M 3, X 2, H 1 — 숫자가 작을수록 높다)에서.

```c
/* 예제 6Z: 3단 사슬 H →(B)→ M →(A)→ L 을 Zephyr에서. 우선순위 H=1 X=2 M=3 L=4, X는 락 없이 W ms 일 */
#include "common.h"
#ifndef XW
#define XW 5
#endif
K_MUTEX_DEFINE(A);
K_MUTEX_DEFINE(B);
static struct k_thread th[4]; static K_THREAD_STACK_ARRAY_DEFINE(stk, 4, 1024);
static char name_of(struct k_thread *c) {
	return c == &th[0] ? 'L' : c == &th[1] ? 'M' : c == &th[2] ? 'X' : c == &th[3] ? 'H' : 'I';
}
static uint32_t hreq, hacq; static int pl_at_x, pm_at_x;
static void at(int64_t tick) { k_sleep(K_TIMEOUT_ABS_TICKS(tick)); }
static void fL(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	at(100); k_mutex_lock(&A, K_FOREVER); work_us(4000); k_mutex_unlock(&A);
	at(140);
	printk("W=%d ms: prio when X starts L=%d M=%d, H blocked %u us (released->got B)\n",
	       XW, pl_at_x, pm_at_x, (hacq - hreq) / 25u);
	dump_exit();
}
static void fM(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	at(101); k_mutex_lock(&B, K_FOREVER); k_mutex_lock(&A, K_FOREVER);
	work_us(300); k_mutex_unlock(&A); k_mutex_unlock(&B);
}
static void fH(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	at(102); hreq = k_cycle_get_32();
	k_mutex_lock(&B, K_FOREVER); hacq = k_cycle_get_32(); work_us(200); k_mutex_unlock(&B);
}
static void fX(void *a, void *b, void *c) {
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	at(103); pl_at_x = k_thread_priority_get(&th[0]); pm_at_x = k_thread_priority_get(&th[1]);
	work_us(XW * 1000u);
}
int main(void) {
	calibrate();
	k_thread_create(&th[0], stk[0], 1024, fL, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	k_thread_create(&th[1], stk[1], 1024, fM, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
	k_thread_create(&th[2], stk[2], 1024, fX, NULL, NULL, NULL, 2, 0, K_NO_WAIT);
	k_thread_create(&th[3], stk[3], 1024, fH, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
	return 0;
}
```

```text
W=0 ms: prio when X starts L=3 M=1, H blocked 2386 us (released->got B)
W=5 ms: prio when X starts L=3 M=1, H blocked 7381 us (released->got B)
W=10 ms: prio when X starts L=3 M=1, H blocked 12377 us (released->got B)
```

출력에서 볼 것: **두 커널 모두 사슬을 따라가지 않는다.** FreeRTOS에서 X가 시작할 때 M은 4(H)로 올랐지만 L은 2(M)에 머물러 있다. Zephyr도 M은 1, L은 3이다. 그래서 X(3 / 2)가 L을 선점하고, H의 대기는 `2.4 ms + W`로 **W에 비례**한다. 상속을 쓰는데도 무한 역전이 그대로다. trace에도 드러난다(FreeRTOS W = 5).

```text
   1.000 ms  S M prio=2
   1.013 ms  + L prio=2
   1.024 ms  S L prio=2
   2.000 ms  S H prio=4
   2.009 ms  + M prio=4
   2.021 ms  S L prio=2
   3.000 ms  S X prio=3
   8.013 ms  S L prio=2
   9.085 ms  - L prio=1
   9.092 ms  S M prio=4
   9.402 ms  - M prio=2
   9.410 ms  S H prio=4
   9.624 ms  S M prio=2
   9.632 ms  S L prio=1
   9.641 ms  S I prio=0
```

1.013 ms에 L이 2로, 2.009 ms에 M이 4로 오르지만(`+ M 4`) L에게는 아무 일도 없다. 3.000 ms에 X가 L을 선점해 8.013 ms까지 돈다. L이 A를 놓는 9.085 ms에야 사슬이 풀린다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13" font-weight="bold">(a) FreeRTOS V11.3.2 — L은 M의 2까지만 올라간다</text>
<text x="58" y="40" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="44" x2="650" y2="44" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="62" font-size="12" text-anchor="end">X</text>
<line x1="70" y1="66" x2="650" y2="66" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="84" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="88" x2="650" y2="88" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="106" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="110" x2="650" y2="110" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.0" y="96" width="58.0" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="128.0" y="74" width="1.4" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="129.4" y="96" width="56.6" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="186.0" y="30" width="1.2" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="187.2" y="96" width="56.8" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="244.0" y="52" width="290.8" height="14" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/>
<rect x="534.7" y="96" width="62.2" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="596.9" y="96" width="0.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="597.4" y="74" width="18.0" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#e08a3c" stroke-width="1"/>
<rect x="615.3" y="74" width="0.6" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="615.8" y="30" width="12.4" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="628.2" y="74" width="0.6" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="628.7" y="96" width="0.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="128.0" y1="70" x2="128.0" y2="90" stroke="currentColor" stroke-width="1.5"/>
<text x="131.0" y="71" font-size="12">B 쥐고 A 요청</text>
<line x1="186.0" y1="26" x2="186.0" y2="46" stroke="currentColor" stroke-width="1.5"/>
<text x="189.0" y="27" font-size="12">릴리스: B 요청</text>
<line x1="615.8" y1="26" x2="615.8" y2="46" stroke="currentColor" stroke-width="1.5"/>
<text x="618.8" y="27" font-size="12">획득</text>
<line x1="186.0" y1="37" x2="615.8" y2="37" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="400.9" y="34" font-size="12" text-anchor="middle">H 대기 7.41 ms</text>
<text x="10" y="136" font-size="13" font-weight="bold">(b) Zephyr v4.4.2 — 똑같이 L은 M의 3까지만</text>
<text x="58" y="158" font-size="12" text-anchor="end">H</text>
<line x1="70" y1="162" x2="650" y2="162" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="180" font-size="12" text-anchor="end">X</text>
<line x1="70" y1="184" x2="650" y2="184" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="202" font-size="12" text-anchor="end">M</text>
<line x1="70" y1="206" x2="650" y2="206" stroke="currentColor" stroke-opacity="0.25"/>
<text x="58" y="224" font-size="12" text-anchor="end">L</text>
<line x1="70" y1="228" x2="650" y2="228" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="70.0" y="214" width="58.0" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<rect x="128.0" y="192" width="0.6" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="128.6" y="214" width="57.4" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="186.0" y="148" width="0.6" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="186.6" y="214" width="57.4" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="244.0" y="170" width="290.2" height="14" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/>
<rect x="534.2" y="214" width="61.9" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="1"/>
<rect x="596.1" y="192" width="18.1" height="14" fill="#d0564a" fill-opacity="0.45" stroke="#e08a3c" stroke-width="1"/>
<rect x="614.2" y="148" width="12.2" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/>
<rect x="626.4" y="192" width="0.6" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/>
<rect x="626.8" y="214" width="0.7" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/>
<line x1="128.0" y1="188" x2="128.0" y2="208" stroke="currentColor" stroke-width="1.5"/>
<text x="131.0" y="189" font-size="12">B 쥐고 A 요청</text>
<line x1="186.0" y1="144" x2="186.0" y2="164" stroke="currentColor" stroke-width="1.5"/>
<text x="189.0" y="145" font-size="12">릴리스: B 요청</text>
<line x1="614.0" y1="144" x2="614.0" y2="164" stroke="currentColor" stroke-width="1.5"/>
<text x="617.0" y="145" font-size="12">획득</text>
<line x1="186.0" y1="155" x2="614.0" y2="155" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="400.0" y="152" font-size="12" text-anchor="middle">H 대기 7.38 ms</text>
<line x1="70" y1="242" x2="650" y2="242" stroke="currentColor"/>
<line x1="70.0" y1="242" x2="70.0" y2="246" stroke="currentColor"/>
<text x="70.0" y="259" font-size="12" text-anchor="middle">0</text>
<line x1="128.0" y1="242" x2="128.0" y2="246" stroke="currentColor"/>
<text x="128.0" y="259" font-size="12" text-anchor="middle">1</text>
<line x1="186.0" y1="242" x2="186.0" y2="246" stroke="currentColor"/>
<text x="186.0" y="259" font-size="12" text-anchor="middle">2</text>
<line x1="244.0" y1="242" x2="244.0" y2="246" stroke="currentColor"/>
<text x="244.0" y="259" font-size="12" text-anchor="middle">3</text>
<line x1="302.0" y1="242" x2="302.0" y2="246" stroke="currentColor"/>
<text x="302.0" y="259" font-size="12" text-anchor="middle">4</text>
<line x1="360.0" y1="242" x2="360.0" y2="246" stroke="currentColor"/>
<text x="360.0" y="259" font-size="12" text-anchor="middle">5</text>
<line x1="418.0" y1="242" x2="418.0" y2="246" stroke="currentColor"/>
<text x="418.0" y="259" font-size="12" text-anchor="middle">6</text>
<line x1="476.0" y1="242" x2="476.0" y2="246" stroke="currentColor"/>
<text x="476.0" y="259" font-size="12" text-anchor="middle">7</text>
<line x1="534.0" y1="242" x2="534.0" y2="246" stroke="currentColor"/>
<text x="534.0" y="259" font-size="12" text-anchor="middle">8</text>
<line x1="592.0" y1="242" x2="592.0" y2="246" stroke="currentColor"/>
<text x="592.0" y="259" font-size="12" text-anchor="middle">9</text>
<line x1="650.0" y1="242" x2="650.0" y2="246" stroke="currentColor"/>
<text x="650.0" y="259" font-size="12" text-anchor="middle">10</text>
<text x="650" y="274" font-size="12" text-anchor="end">시간 (ms, 라운드 시작 = L 릴리스)</text>
<text x="70" y="288" font-size="12">빨강 칠 = 상속받은 우선순위로 도는 구간 · 두 커널 모두 사슬을 따라가지 않는다</text>
</svg>
```
그림 8 — 예제 7(W = 5 ms)의 trace. 두 커널 모두 L은 M의 우선순위까지만 올라가고(빨강 칠), 락과 무관한 X가 5 ms 동안 L을 밀어내 H가 7.4 ms를 기다린다. QEMU icount 기준.

### 9.3 왜 그런가 — 소스 한 줄씩

- **FreeRTOS** `xTaskPriorityInherit()`: holder가 ready list에 있으면 리스트를 옮기고, 아니면 주석 그대로 "Just inherit the priority."로 `uxPriority`만 바꾸고 끝난다. holder가 **다른 mutex에 막혀 있는지 확인하지도, 그 mutex의 holder를 찾아가지도 않는다.** 게다가 막혀 있는 holder의 event list item 값은 바꾸지만 그 대기 리스트 안의 위치를 다시 정렬하지도 않는다.
- **Zephyr** `adjust_owner_prio()` → `z_thread_prio_set()`: owner가 pending 상태면 "remove it from the waitq and reinsert it with the new priority"로 **자기가 기다리는 wait_q 안의 순서만** 고친다. 그 wait_q를 가진 mutex의 owner로 넘어가지 않는다.
- 대조: Linux `rt_mutex`는 `rt_mutex_adjust_prio_chain()`으로 사슬을 끝까지 걷는다(깊이 상한 `max_lock_depth`, 기본 1024). 범용 OS가 이 복잡도를 감수하는 이유는 유저 공간 락 사슬을 예측할 수 없기 때문이다. MCU RTOS는 "사슬이 생기는 설계를 하지 마라"를 선택했다.

### 9.4 그럼 어떻게 하나

1. **락을 쥔 채 다른 락을 기다리지 않는다.** 사슬은 "B를 쥐고 A를 기다리는" 한 줄에서 생긴다.
2. 꼭 필요하면 **ICPP**를 쓴다. ICPP에서는 사슬이 생기지 않는다. L이 A를 잡는 순간 ceiling(A) ≥ M으로 올라가므로 M은 L이 A를 놓을 때까지 CPU를 받지 못하고, 따라서 "B를 쥐고 A를 기다리는" 상태에 도달할 수 없다(§6.4의 "블로킹은 시작 전에만").
3. 사슬 대신 **게이트키퍼(서버) 태스크**: A를 한 태스크만 소유하게 하고 나머지는 요청 큐로 보낸다(D7). 락이 없으니 사슬도 없다. 다만 §10의 큐 역전을 피하려면 서버 태스크의 우선순위를 클라이언트 최고 이상으로 둔다.

---

## 10. 락이 아닌 역전 — 큐 · ISR · 스케줄러 잠금 · 공유 하드웨어

우선순위 상속은 **소유자가 있는 락**에만 작동한다. 커널이 "누가 H를 막고 있는지" 알아야 올려 줄 수 있기 때문이다. 소유자 개념이 없는 대기에서는 같은 모양의 역전이 일어나도 커널이 아무것도 해 주지 않는다.

### 10.1 예제 8 — 큐로 기다리는 데이터를 낮은 태스크가 만든다

무엇을 확인하는 코드인지: H(소비자, 추정기)가 L(생산자)이 2 ms 계산해서 보내는 프레임을 큐로 기다린다. M이 L을 밀어내면 프레임이 늦는다. FIX=1은 생산자 우선순위를 소비자와 같게 올린다.

```c
/* 예제 7: 락이 없어도 생기는 역전 — H가 L이 만드는 데이터를 큐로 기다리는데 M이 L을 밀어낸다
 *  FIX=0: L(1)이 생산 · FIX=1: 생산자를 소비자와 같은 우선순위(3)로 */
#include "d3.h"
#include "queue.h"
#ifndef FIX
#define FIX 0
#endif
static const uint16_t Wms[] = { 0, 2, 4, 8 };
#define NR (sizeof Wms / sizeof Wms[0])
enum { PERIOD = 25, START = 10 };
static QueueHandle_t q;
static uint32_t ready_at[NR], got_at[NR];
static void tL(void *p) {          /* 생산자: t0에 깨어 2 ms 계산 후 프레임을 보낸다 */
    (void)p; TickType_t last = 0;
    for (uint32_t r = 0; r < NR; r++) {
        xTaskDelayUntil(&last, r ? PERIOD : START);
        work_us(2000); uint32_t v = r; xQueueSend(q, &v, 0);
    }
    vTaskSuspend(NULL);
}
static void tH(void *p) {          /* 소비자(추정기): 프레임이 오자마자 처리해야 한다 */
    (void)p; uint32_t v;
    for (uint32_t r = 0; r < NR; r++) {
        xQueueReceive(q, &v, portMAX_DELAY); got_at[v] = D3_CYC();
        ready_at[v] = d3_tk[START + v * PERIOD] + 2000u * 25u;   /* 방해가 없을 때 준비됐을 시각 */
    }
    board_printf_raw("FIX=%d producer prio=%d\n", FIX, FIX ? 3 : 1);
    for (uint32_t r = 0; r < NR; r++)
        board_printf_raw("W=%u ms: frame late by %u us\n", Wms[r], (got_at[r] - ready_at[r]) / 25u);
    board_exit(0);
}
static void tM(void *p) {          /* 중간: t0+1 ms에 W ms 동안 CPU 사용, 큐와 무관 */
    (void)p; TickType_t last = 0;
    for (uint32_t r = 0; r < NR; r++) { xTaskDelayUntil(&last, r ? PERIOD : START + 1); work_us(Wms[r] * 1000u); }
    vTaskSuspend(NULL);
}
int main(void) {
    calibrate(); q = xQueueCreate(4, sizeof(uint32_t));
    xTaskCreate(tL, "L", 512, NULL, FIX ? 3 : 1, NULL);
    xTaskCreate(tM, "M", 512, NULL, 2, NULL);
    xTaskCreate(tH, "H", 512, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```text
FIX=0 producer prio=1
W=0 ms: frame late by 36 us
W=2 ms: frame late by 2037 us
W=4 ms: frame late by 4037 us
W=8 ms: frame late by 8038 us

FIX=1 producer prio=3
W=0 ms: frame late by 28 us
W=2 ms: frame late by 28 us
W=4 ms: frame late by 28 us
W=8 ms: frame late by 26 us
```

출력에서 볼 것: FIX=0에서 프레임 지연이 W와 정확히 같다(2037, 4037, 8038 us). 예제 1과 똑같은 직선인데 락이 없으니 상속이 개입할 수 없다. 큐에는 "소유자"가 없다. FIX=1(생산자를 소비자 우선순위로)은 W와 무관하게 26~28 us다. 이것이 **파이프라인 우선순위 규칙**이다: **높은 우선순위 태스크가 기다리는 데이터를 만드는 태스크는 그 소비자 이상의 우선순위를 가져야 한다.** 드론에서 "IMU 드라이버 태스크 < 추정기"로 배치하면 정확히 이 버그가 난다.

### 10.2 ISR이 만드는 역전

ISR은 모든 태스크보다 높다. ISR 안에서 "원래 낮은 우선순위로 해도 되는 일"(CAN 메시지 파싱, 로그 포맷팅)을 하면 그 시간만큼 최고 우선순위 태스크가 늦는다. 우선순위 표로 보면 "낮은 일 > 높은 일"이므로 역전이다. ISR 폭주(인터럽트 storm)는 상한까지 없앤다. 해결은 ISR은 최소한만 하고 나머지를 적절한 우선순위의 태스크로 미루는 것(E1, E4)과, 인터럽트 우선순위를 태스크 중요도와 맞게 배치하는 것(E2)이다.

### 10.3 스케줄러 잠금 · 크리티컬 섹션 = "모두를 막는 ceiling"

`vTaskSuspendAll()`/`k_sched_lock()`과 `taskENTER_CRITICAL()`/`irq_lock()`은 ceiling = 무한대인 ICPP와 같다. 예제 1을 `LOCK=3`(`vTaskSuspendAll`)으로 돌린 결과다.

```text
CFG LOCK=3
R W=0 h_block_us=3 h_resp_us=2235 m_resp_us=230 t0=735004
R W=4 h_block_us=3 h_resp_us=2235 m_resp_us=4231 t0=2610004
R W=12 h_block_us=3 h_resp_us=2235 m_resp_us=12233 t0=5110004
```

출력에서 볼 것: H 응답이 2235 us로 묶여 있다(유한). 하지만 이 방식은 **자원을 공유하지 않는 모든 태스크**까지 막는다. 이 실험에 H보다 높은 1 kHz 제어 루프가 있었다면 그것도 L의 3 ms 동안 못 돈다. 그래서 스케줄러 잠금 · 크리티컬 섹션은 **수 us짜리 구간**에만 쓴다(C7). `h_block_us = 3`은 H가 L의 구간이 끝난 뒤에야 처음 CPU를 받아 측정한 값이라 의미가 없다. 응답 시간을 봐야 한다.

### 10.4 공유 하드웨어 — 낮은 태스크가 시작한 DMA · 버스 트랜잭션

낮은 태스크가 SPI DMA로 SD 카드에 4 KB 블록 쓰기를 시작했다면, 그 전송이 끝날 때까지 높은 태스크는 같은 SPI 버스를 쓸 수 없다. 이 대기는 mutex 대기가 아니라 **하드웨어가 바쁜 시간**이라 상속으로 줄일 수 없다(상속은 CPU 시간만 준다). 해결은 하드웨어 수준에서 한다.

- 큰 전송을 **작은 조각**으로 나눠, 조각 사이에 높은 요청이 끼어들 수 있게 한다.
- 버스를 나눈다(IMU는 SPI1, SD는 SDMMC).
- 버스 관리 태스크(게이트키퍼, D7)가 요청 큐를 **우선순위 순서**로 처리한다.
- DMA 채널 우선순위(STM32 DMA의 채널 priority 등)를 설계에 반영한다.

### 10.5 숨은 공유 자원

C 라이브러리의 힙 할당기와 stdio(`printf`)는 내부에 락을 쓴다. 높은 태스크에서 `malloc`이나 `printf`를 부르면 같은 함수를 쓰는 낮은 태스크와 락을 공유하게 된다. 그 락이 상속을 하는지는 C 라이브러리의 retarget 구현에 달려 있다. 제어 경로에서 동적 할당 · 포맷 출력을 하지 않는 규칙(F1, F4)은 메모리 안정성뿐 아니라 역전 방지이기도 하다.

### 10.6 trace로 진단하는 법 (M3 미리 보기)

"H가 가끔 늦다"를 trace에서 볼 때 찾는 패턴은 하나다: **H가 ready 또는 blocked인데 H보다 낮은 태스크가 CPU를 쥐고 있는 구간.** 그 구간마다 H가 무엇을 기다리는지(어느 객체), 그 객체의 소유자/생산자가 누구인지, 그 소유자를 밀어낸 것이 누구인지를 적는다.

| trace에서 보이는 것 | 진단 | 대책 |
|---|---|---|
| H blocked on mutex, 소유자 L은 ready, CPU는 M | 상속 없는 락(semaphore) 또는 사슬 · 상한 | mutex로, 사슬 제거, `CONFIG_PRIORITY_CEILING` 확인 |
| H blocked on mutex, 소유자 L이 상속받아 돌고 있음 | 정상적인 유한 역전 | 임계 구역 길이 측정 · 단축 |
| H blocked on queue, 생산자 L ready, CPU는 M | 파이프라인 역전 | 생산자 우선순위 상향 |
| H ready, CPU는 ISR이 길게 | ISR 과부하 | 지연 처리(E4), IRQ 우선순위(E2) |
| H ready, 낮은 태스크가 스케줄러 잠금 중 | 긴 `vTaskSuspendAll`/`k_sched_lock` | mutex로 바꾸거나 구간 단축 |
| H blocked on 드라이버 완료, 버스는 낮은 태스크의 DMA 중 | 하드웨어 역전 | 전송 쪼개기, 버스 분리, 게이트키퍼 |

이 노트의 실험이 쓴 방식(FreeRTOS `traceTASK_SWITCHED_IN` · `traceTASK_PRIORITY_INHERIT` · `traceTASK_PRIORITY_DISINHERIT`, Zephyr `CONFIG_TRACING_USER`)이 SEGGER SystemView · Percepio Tracealyzer가 하는 일의 축소판이다. 현장에서는 상용 도구를 쓰더라도 **"상속 이벤트가 찍히는가"** 를 먼저 확인한다. 상속 이벤트가 하나도 없는데 H가 락에서 오래 기다린다면 그 락은 상속을 안 하는 락이다.

---

## 11. RTOS 커널 안에서는 무슨 일이 일어나나

### 11.1 예제 11 — 소스에서 직접 확인

무엇을 확인하는 코드인지: 이 노트의 주장(카운터 기반 해제, "Just inherit", mutex별 원래 우선순위, ceiling 컴파일 조건, POSIX 미지원)이 V11.3.2 / v4.4.2 소스의 어디에 있는지 grep으로 확인한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel; \
  grep -n "uxMutexesHeld\|Only disinherit if no other\|Just inherit the priority" tasks.c; \
  cd /var/rtoslab/zephyrproject/zephyr; \
  grep -n "owner_orig_prio\|adjust_owner_prio(\|new_prio_for_inheritance(\|CONFIG_PRIORITY_CEILING <" kernel/mutex.c; \
  grep -n "case PTHREAD_PRIO_INHERIT:" -A1 lib/posix/options/mutex.c'
```

```text
418:        UBaseType_t uxMutexesHeld;
6805:                    /* Just inherit the priority. */
6862:            configASSERT( pxTCB->uxMutexesHeld );
6863:            ( pxTCB->uxMutexesHeld )--;
6869:                /* Only disinherit if no other mutexes are held. */
6870:                if( pxTCB->uxMutexesHeld == ( UBaseType_t ) 0 )
6955:            configASSERT( pxTCB->uxMutexesHeld );
6973:                /* Only disinherit if no other mutexes are held.  This is a
6977:                if( pxTCB->uxMutexesHeld == uxOnlyOneMutexHeld )
7784:            ( pxTCB->uxMutexesHeld )++;
82:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
83:static int32_t new_prio_for_inheritance(int32_t target, int32_t limit)
92:static bool adjust_owner_prio(struct k_mutex *mutex, int32_t new_prio)
110:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
123:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
124:		mutex->owner_orig_prio = (mutex->lock_count == 0U) ?
126:					mutex->owner_orig_prio;
132:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
135:			mutex->owner_orig_prio);
158:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
159:	new_prio = new_prio_for_inheritance(_current->base.prio,
165:		resched = adjust_owner_prio(mutex, new_prio);
176:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
196:			new_prio_for_inheritance(waiter->base.prio, mutex->owner_orig_prio) :
197:			mutex->owner_orig_prio;
201:		resched = adjust_owner_prio(mutex, new_prio) || resched;
273:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
274:	adjust_owner_prio(mutex, mutex->owner_orig_prio);
291:#if (CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO)
292:		mutex->owner_orig_prio = new_owner->base.prio;
345:	case PTHREAD_PRIO_INHERIT:
346-		return ENOTSUP;
```

출력에서 볼 것: FreeRTOS는 TCB 필드 하나(`uxMutexesHeld`, 418행)로 중첩을 처리한다. Zephyr는 lock(124행)에서 `owner_orig_prio`를 적고 unlock(274행)에서 그 값으로 되돌린다. 상속 코드 전체가 `CONFIG_PRIORITY_CEILING < K_LOWEST_THREAD_PRIO` 조건 안에 있다. 그리고 **Zephyr POSIX 계층의 `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)`는 `ENOTSUP`를 돌려준다**(`PTHREAD_PRIO_PROTECT`도 `ENOTSUP`, `pthread_mutexattr_setprioceiling` 등은 `ENOSYS`를 돌려주는 빈 함수). 그런데 pthread mutex는 내부적으로 `k_mutex` 풀(`posix_mutex_pool`)이라 실제로는 **항상 상속을 한다**. "PI를 요청하면 실패하는데 PI는 된다"는 이상한 조합이라, Linux 코드를 Zephyr로 옮길 때 `setprotocol` 반환값을 assert하면 깨진다(I6).

### 11.2 호출 경로 한 장

```svg
<svg viewBox="0 0 680 372" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerUnits="userSpaceOnUse" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">FreeRTOS V11.3.2</text>
<text x="350" y="18" font-size="13" font-weight="bold">Zephyr v4.4.2</text>
<text x="10" y="40" font-size="12">take / lock 경로</text>
<text x="350" y="40" font-size="12">take / lock 경로</text>
<rect x="10" y="48" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="64" font-size="12">xQueueSemaphoreTake (queue.c)</text>
<text x="20" y="81" font-size="12">비어 있고 블록하기 직전에</text>
<rect x="10" y="100" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="116" font-size="12">xTaskPriorityInherit (tasks.c)</text>
<text x="20" y="133" font-size="12">holder 우선순위 &lt; 내 것이면 올림</text>
<rect x="10" y="152" width="320" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="168" font-size="12">holder가 ready면 ready list 이동</text>
<text x="20" y="185" font-size="12">blocked면 숫자만 바꿈 (사슬 X)</text>
<rect x="350" y="48" width="320" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="360" y="64" font-size="12">z_impl_k_mutex_lock (kernel/mutex.c)</text>
<text x="360" y="81" font-size="12">owner가 있고 K_NO_WAIT가 아니면</text>
<rect x="350" y="100" width="320" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="360" y="116" font-size="12">new_prio_for_inheritance()</text>
<text x="360" y="133" font-size="12">높은 쪽 선택 → CONFIG_PRIORITY_CEILING 상한</text>
<rect x="350" y="152" width="320" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="360" y="168" font-size="12">adjust_owner_prio → z_thread_prio_set</text>
<text x="360" y="185" font-size="12">pending이면 wait_q 재정렬만 (사슬 X)</text>
<line x1="170" y1="88" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="170" y1="140" x2="170" y2="150" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="510" y1="88" x2="510" y2="98" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="510" y1="140" x2="510" y2="150" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="10" y="220" font-size="12">give / unlock 경로</text>
<text x="350" y="220" font-size="12">give / unlock 경로</text>
<rect x="10" y="228" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="244" font-size="12">xQueueGenericSend → prvCopyDataToQueue</text>
<text x="20" y="261" font-size="12">mutex면 소유자 정리</text>
<rect x="10" y="280" width="320" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="296" font-size="12">xTaskPriorityDisinherit (tasks.c)</text>
<text x="20" y="313" font-size="12">uxMutexesHeld-- 후 0일 때만 base로</text>
<rect x="350" y="228" width="320" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="360" y="244" font-size="12">z_impl_k_mutex_unlock</text>
<text x="360" y="261" font-size="12">adjust_owner_prio(owner_orig_prio)</text>
<rect x="350" y="280" width="320" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="360" y="296" font-size="12">대기자 있음: 새 owner 깨우고 z_reschedule</text>
<text x="360" y="313" font-size="12">대기자 없음: 우선순위만 내리고 반환</text>
<line x1="170" y1="268" x2="170" y2="278" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="510" y1="268" x2="510" y2="278" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="10" y="346" font-size="12">타임아웃: vTaskPriorityDisinheritAfterTimeout — 쥔 mutex가 1개일 때만 내림</text>
<text x="10" y="364" font-size="12">타임아웃: lock 함수 안에서 남은 대기자 기준으로 new_prio 재계산 후 adjust_owner_prio</text>
</svg>
```
그림 9 — 상속이 일어나는 호출 경로. 빨강 상자가 이 노트가 실측한 "한계"가 들어 있는 곳이다(사슬 미추적, 중첩 처리, 재스케줄 생략).

### 11.3 자료구조 쪽에서 본 차이

| 개념 | FreeRTOS (`TCB_t`, `Queue_t`) | Zephyr (`struct k_thread`, `struct k_mutex`) |
|---|---|---|
| 기본 우선순위 | `uxBasePriority` | `mutex->owner_orig_prio` (mutex마다) |
| 유효 우선순위 | `uxPriority` | `thread->base.prio` |
| 소유자 | `pxQueue->u.xSemaphore.xMutexHolder` | `mutex->owner` |
| 중첩 추적 | `uxMutexesHeld` 카운터 (태스크마다) | 없음 (잡은 순서 규칙에 의존) |
| 재귀 잠금 | 별도 API(`xSemaphoreTakeRecursive`), `uxRecursiveCallCount` | `lock_count` (같은 `k_mutex`가 재귀 허용) |
| 대기자 정렬 | `xTasksWaitingToReceive` (우선순위 순 삽입) | `wait_q` (우선순위 큐) |
| ISR에서 | 금지 (holder가 NULL이면 상속 안 함) | `__ASSERT(!arch_is_in_isr())` |

`vTaskPrioritySet()`과 상속의 상호작용도 알아 둔다. 상속받은 상태에서 `vTaskPrioritySet()`을 부르면 `uxBasePriority`는 항상 바뀌지만, `uxPriority`는 "상속 중이 아니거나 새 값이 더 클 때만" 바뀐다(소스 주석: "Only change the priority being used if the task is not currently using an inherited priority or the new priority is bigger than the inherited priority"). 그래서 §7의 ICPP 래퍼가 PI mutex와 섞여도 상속이 깨지지 않는다. 그리고 실행 중인 태스크의 우선순위를 **내릴 때는 항상 yield**한다(예제 9의 switch 1000번).

---

## 12. 실무에서 다시 보기

### 12.1 드론 비행 제어기 — 추정기가 로거 때문에 늦는다

STM32H7급 FC를 생각하자. IMU 데이터 레디 인터럽트가 1 kHz로 추정기(EKF, H)를 깨운다. 텔레메트리 태스크(M)는 MAVLink 패킷을 만들어 UART로 보낸다. SD 로거(L)는 블랙박스 로그를 SD 카드에 쓴다. 보드 설계상 기압계와 SD 카드가 **같은 SPI 버스**에 붙어 있고, 버스는 mutex 하나로 보호한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" markerUnits="userSpaceOnUse" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="250" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="39" font-size="13">추정기 EKF (H, 1 kHz)</text>
<text x="20" y="57" font-size="12">기압계를 SPI로 읽어야 한다</text>
<rect x="10" y="100" width="250" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="20" y="119" font-size="13">텔레메트리 (M, 50 Hz)</text>
<text x="20" y="137" font-size="12">MAVLink 패킹, 수 ms CPU</text>
<rect x="10" y="180" width="250" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="199" font-size="13">SD 로거 (L, 배경)</text>
<text x="20" y="217" font-size="12">mutex 안에서 블록 쓰기</text>
<rect x="300" y="100" width="130" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="312" y="119" font-size="13">SPI2 mutex</text>
<text x="312" y="137" font-size="12">기압계 · SD</text>
<line x1="260" y1="43" x2="320" y2="98" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="320" y1="148" x2="262" y2="200" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="135" y1="147" x2="135" y2="178" stroke="#e08a3c" stroke-width="2" marker-end="url(#ah4)"/>
<rect x="460" y="20" width="210" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="39" font-size="12">① semaphore → mutex(PIP)</text>
<text x="470" y="57" font-size="12">무한 → 유한 (예제 1, 2)</text>
<rect x="460" y="76" width="210" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="95" font-size="12">② 락 안에서는 DMA 시작만</text>
<text x="470" y="113" font-size="12">완료는 락 밖에서 기다림</text>
<rect x="460" y="132" width="210" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="151" font-size="12">③ 버스 분리</text>
<text x="470" y="169" font-size="12">SD는 SDMMC, 센서는 SPI</text>
<rect x="460" y="188" width="210" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="207" font-size="12">④ 버스 게이트키퍼 태스크</text>
<text x="470" y="225" font-size="12">요청 큐를 우선순위 순서로 (D7)</text>
<text x="10" y="262" font-size="12">증상: 몇 분에 한 번 EKF 주기가 3~10 ms 밀림 → 자세 추정 지터 → 기체가 순간 흔들림</text>
<text x="10" y="282" font-size="12">특징: SD 카드가 내부 GC로 쓰기 지연을 길게 낼 때만 나타나 재현이 어렵다</text>
<text x="10" y="302" font-size="12">확인: trace에서 EKF blocked on SPI2 mutex + 소유자 = 로거 + CPU = 텔레메트리</text>
<text x="10" y="322" font-size="12">대책은 오른쪽 ①이 기본, ②가 상한을 줄이고, ③ · ④는 구조로 공유 자체를 없앤다</text>
</svg>
```
그림 10 — 드론 FC의 전형적인 역전 구조와 대책(설계 예시, 실측 아님). 그림 1의 L/M/H가 그대로 실제 태스크 이름으로 바뀌었을 뿐이다.

②를 하려다 흔히 쓰는 코드는 이렇다(패턴 설명용 조각, 실행하지 않음).

```c
/* 실행하지 않은 코드 조각 — 락 안에서는 DMA 디스크립터만 걸고, 완료는 락 밖에서 기다리지 않는다 */
void sd_write_block(const uint8_t *buf)
{
    xSemaphoreTake(spi2_mtx, portMAX_DELAY);
    spi2_cs_assert(SD_CS);
    spi2_dma_start(buf, 512);                        /* 수 us: 레지스터만 쓴다 */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);         /* DMA 완료 ISR이 깨움 — 이 동안 락을 쥐고 있다! */
    spi2_cs_release(SD_CS);
    xSemaphoreGive(spi2_mtx);
}
```

이 조각에는 함정이 있다. DMA 완료를 **락을 쥔 채로** 기다리므로, 임계 구역 길이 = 전송 시간 + SD 카드의 busy 시간이다. CPU는 놀고 있지만 EKF는 여전히 그 시간만큼 막힌다(§10.4의 하드웨어 역전). SD 카드는 CS를 놓기 전에 busy를 길게 낼 수 있으므로, 같은 버스에 시간 민감한 장치를 두지 않는 ③이 근본 해결이다. 버스를 못 나누면 블록 쓰기를 작은 조각으로 쪼개고 조각 사이에 락을 놓는다.

### 12.2 로봇 관절 모터 컨트롤러

FOC 전류 루프(20 kHz)는 보통 ISR이나 최고 우선순위 태스크에서 돈다. 여기서 역전은 대부분 **락이 아니라 ISR과 공유 데이터**에서 온다. CAN 수신 ISR이 설정값(setpoint)을 고치는 동안 제어 루프가 읽으면 레이스이고, 이를 막으려고 낮은 태스크가 긴 `taskENTER_CRITICAL`을 쓰면 그 구간만큼 전류 루프가 늦는다(§10.3의 "모두를 막는 ceiling"). 해결은 이중 버퍼 + 원자적 포인터 교환(D8)이나, BASEPRI로 제어 IRQ만 남기고 막는 것(E2)이다. 전류 루프 같은 "zero-latency" 인터럽트는 RTOS API를 부르지 않으므로 상속이 개입할 여지 자체가 없다. 설계로 공유를 없애야 한다.

### 12.3 센서 허브 MCU

Linux SoC 옆에서 IMU 타임스탬프를 찍는 센서 허브는 "SPI 슬레이브로 호스트에 데이터 전달(L)"과 "IMU 샘플링 · 타임스탬프(H)"가 같은 DMA 컨트롤러를 공유하는 경우가 많다. 호스트가 큰 읽기를 걸어 낮은 태스크가 긴 DMA를 시작하면 IMU 샘플 DMA가 그 뒤에 줄을 선다. DMA 채널 우선순위(하드웨어 중재)를 IMU 쪽으로 올리는 것이 "하드웨어 수준의 상속"이다.

### 12.4 Don의 SSD 펌웨어로 번역하면

| RTOS 개념 | SSD 펌웨어에서 본 모습 |
|---|---|
| 유한 역전 | 호스트 명령이 진행 중인 GC 단계(run-to-completion)를 기다림. 단계 길이로 상한이 정해짐 |
| 무한 역전 | 선점형으로 바꾸고 GC가 매핑 테이블 락을 쥔 채 SMART · thermal 태스크에 밀리는 경우 |
| priority inheritance | "락을 쥔 백그라운드 작업은 잠깐 foreground 큐로 승격" 같은 executive 규칙 |
| ICPP | 특정 테이블을 만지는 동안은 "호스트 경로 우선순위로 실행"하도록 고정한 설계 |
| 하드웨어 역전 | NAND 채널을 GC의 긴 erase가 쥐고 있어 호스트 read가 그 다이 뒤에 줄 섬 → erase suspend로 해결 |
| 진단 | 호스트 명령 latency outlier를 펌웨어 이벤트 로그(타임스탬프 링버퍼)로 역추적 |

마지막 줄이 이 노트의 실험 방법 그대로다. **erase suspend**는 "낮은 작업이 쥔 자원을 높은 작업을 위해 잠깐 내놓게 하는" 하드웨어 버전의 역전 대책이고, 면접에서 "다른 도메인의 같은 문제"로 꺼내기 좋은 경험이다.

### 12.5 예제 12 — trace를 구간 목록으로 바꾸는 작은 도구

무엇을 확인하는 코드인지: 이 노트의 간트 차트들은 모두 trace 덤프에서 만들었다. 핵심은 "switch-in 이벤트 사이 = 그 태스크의 실행 구간"이라는 한 가지 규칙이다. 현장에서 SystemView 같은 도구가 없어도 RAM 링버퍼 + 이 정도 스크립트면 역전을 증명할 수 있다.

```python
# 예제 12: trace(T cyc ev name prio) → 구간 목록과 태스크별 CPU 시간 (간트 차트의 원자료)
import sys
path, t0, ms = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
t1 = t0 + int(ms * 25000)                       # 25 MHz cycle
ev = [l.split() for l in open(path, errors='replace') if l.startswith('T ')]
ev = [(int(c), e, n, int(p)) for _, c, e, n, p in ev]
segs, cur = [], None                            # cur = (이름, 시작, 우선순위)
for c, e, n, p in ev:
    if e not in 'S+-=':
        continue
    if e == 'S' or (cur and cur[0] == n):       # switch-in, 또는 실행 중 우선순위 변경
        if cur and c > t0 and cur[1] < t1:
            segs.append((cur[0], max(cur[1], t0), min(c, t1), cur[2]))
        cur = (n, c, p)
tot = {}
for n, a, b, p in segs:
    print(f'{(a - t0) / 25000:7.3f} ~ {(b - t0) / 25000:7.3f} ms  {n} prio={p}')
    tot[n] = tot.get(n, 0) + (b - a)
print('CPU ms:', ', '.join(f'{n}={v / 25000:.3f}' for n, v in sorted(tot.items())))
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/D3/py/tracesum.py \
  /Users/donh/.rtos_scratch/D3/out/inv1.txt 2610006 8
```

```text
  0.000 ~   0.007 ms  I prio=0
  0.007 ~   1.007 ms  L prio=1
  1.007 ~   1.032 ms  H prio=3
  1.032 ~   3.055 ms  L prio=3
  3.055 ~   3.062 ms  L prio=1
  3.062 ~   3.279 ms  H prio=3
  3.279 ~   7.295 ms  M prio=2
  7.295 ~   7.305 ms  L prio=1
  7.305 ~   8.000 ms  I prio=0
CPU ms: H=0.243, I=0.701, L=3.039, M=4.016
```

같은 라운드를 binary semaphore(예제 1)로 돌린 trace에 적용한 마지막 줄:

```text
CPU ms: H=0.238, I=0.706, L=3.040, M=4.016
```

출력에서 볼 것: 두 경우 **태스크별 CPU 시간은 거의 같다**(H 0.24 ms, L 3.04 ms, M 4.02 ms). 일의 총량은 똑같고 **순서만** 다르다. 우선순위 역전은 CPU가 모자라서 생기는 문제가 아니라 순서의 문제라는 것을 숫자로 보여 준다. 그래서 CPU 사용률 지표(M4)로는 절대 안 보이고, 시간축 trace로만 보인다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| binary semaphore · `k_sem`을 자원 락으로 사용 | H의 지연이 다른 태스크 부하에 비례해 가끔 크게 튐 | 소유자 개념이 없어 상속 불가 (예제 1, 3) | `xSemaphoreCreateMutex()` · `k_mutex`로 교체 |
| mutex 안에서 긴 일(SD 쓰기, printf, 플래시 erase) | mutex를 써도 H가 수 ms 지연 | 유한 역전의 상한 자체가 큼 | 임계 구역을 데이터 복사 · DMA 시작만으로 줄임 |
| B를 쥔 채 A를 기다리는 코드 | 상속을 써도 간헐적 무한 역전 | FreeRTOS · Zephyr 모두 사슬 미추적 (예제 7) | 락 중첩 제거, ICPP, 게이트키퍼 태스크 |
| FreeRTOS에서 mutex 두 개를 오래 중첩 | 관계없는 중간 태스크가 가끔 늦음 | `uxMutexesHeld` > 0인 동안 상속 유지 (예제 5) | 중첩 구간 단축, 하나의 락으로 통합 |
| Zephyr에서 부스트 전에 잡은 mutex를 먼저 놓음 | 상속이 중간에 풀려 H가 다시 밀림 | `owner_orig_prio`로 복귀 (예제 6) | LIFO 해제, 잡는 순서 규칙 준수 |
| `CONFIG_PRIORITY_CEILING`을 "PCP 켜기"로 오해해 설정 | `k_mutex`인데 무한 역전 | 상속 높이 상한일 뿐 (예제 3) | 기본값(-128) 유지하거나 의미를 알고 설정 |
| 생산자 태스크를 소비자보다 낮게 배치 | 데이터가 늦게 도착, 락이 없어 원인 불명 | 큐에는 소유자가 없음 (예제 8) | 파이프라인 우선순위: 생산자 ≥ 소비자 |
| `vTaskPrioritySet`으로 ICPP 구현하며 "잡은 뒤 올림" | 드물게 여전히 역전 | 잡기와 올리기 사이 창 | 잡기 전에 올리고 놓은 뒤 내림 |
| ICPP ceiling = 최고 사용자 우선순위 + time slicing | ICPP인데 H가 락에서 막히는 trace | 같은 우선순위 round-robin (예제 4) | slicing 끄기 또는 락 객체 유지 |
| 긴 `vTaskSuspendAll`/`k_sched_lock`으로 보호 | 자원과 무관한 최고 우선순위 루프까지 지연 | 모두를 막는 ceiling | mutex로 바꾸고 스케줄러 잠금은 수 us만 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "Explain priority inversion and how to fix it."

**A.** 높은 태스크 H가 낮은 태스크 L이 쥔 락을 기다리는 건 정상이고 L의 임계 구역 길이로 묶여 있다(유한 역전). 문제는 그 사이 락과 무관한 중간 태스크 M이 L을 선점하는 경우다. 그러면 H가 M의 실행 시간 전부를 기다리고, 상한이 없어진다(무한 역전). 해결은 상속이 있는 mutex(L을 H의 우선순위로 올려 M이 못 끼게)나 priority ceiling(락 잡는 순간 ceiling으로)이고, 남는 유한 부분은 임계 구역을 짧게 해서 줄인다. 직접 QEMU에서 재 보니 binary semaphore면 M이 1 ms 더 돌 때 H도 정확히 1 ms 늦었고, mutex로 바꾸자 W와 무관하게 2.27 ms로 고정됐다.

> Priority inversion is when a high-priority task waits on a lock held by a low-priority task. That part is bounded by the critical section length and is the inherent cost of locking. The real problem is when a medium-priority task that doesn't touch the lock preempts the low-priority holder: now the high-priority task waits for the medium task's entire execution, so the blocking time is unbounded. The fixes are priority inheritance — the holder temporarily runs at the waiter's priority so the medium task can't preempt it — or a priority ceiling protocol, where the holder runs at the resource's ceiling as soon as it takes the lock. Then you keep the remaining bounded part small by keeping critical sections short. I reproduced it on a Cortex-M3 model: with a binary semaphore the high task's response grew one-for-one with the medium task's work, from 2.3 to 14.3 ms; with a FreeRTOS mutex it stayed flat at 2.27 ms.

**꼬리 질문**: "Is a binary semaphore OK as a lock if critical sections are short?" → 짧아도 M의 길이에는 상한이 없으니 안 된다. 상속은 소유자가 있는 mutex만 할 수 있다.

**Q.** "What's the difference between priority inheritance and priority ceiling?"

**A.** 상속은 실제로 누가 막혔을 때만 소유자를 올리고, 미리 알 것이 없다. 대신 데드락을 못 막고, 락 여러 개에서 락마다 한 번씩 막히는 chained blocking이 생긴다. ceiling은 자원마다 "쓸 수 있는 가장 높은 태스크"를 미리 정해 두고, ICPP는 락을 잡는 즉시 그 높이로 올린다. 블로킹은 최대 한 번, 임계 구역 하나 길이이고 데드락도 없다. 대가는 경쟁이 없어도 매번 우선순위를 바꾸는 비용과 정적 분석이 필요하다는 점이다. FreeRTOS에서 `vTaskPrioritySet`으로 ICPP를 만들어 보니 PIP mutex의 2배 비용(330 vs 153 cycle)이었다.

> Priority inheritance raises the holder only when someone actually blocks on it, and needs no static knowledge, but it doesn't prevent deadlock and allows chained blocking — a task can be blocked once per lock it needs. A ceiling protocol assigns each resource a ceiling, the highest priority of any task that uses it. With the immediate ceiling protocol the holder jumps to that ceiling as soon as it locks, so a task is blocked at most once, for at most one critical section, before it starts running, and deadlock is impossible on a single core. The cost is changing priority on every lock even without contention, and you have to know resource usage at design time. The original ceiling protocol from Sha, Rajkumar and Lehoczky gives the same bound but only raises on contention, using a system-ceiling check; it's rarely implemented in general-purpose RTOSes.

**꼬리 질문**: "Which one does FreeRTOS use?" → 내장은 상속(PIP)뿐. ICPP는 직접 래퍼로 만들고, POSIX `PTHREAD_PRIO_PROTECT`나 OSEK resource가 ICPP다.

**Q.** "Tell me about the Mars Pathfinder bug."

**A.** 1997년 착륙 후 VxWorks 위의 비행 소프트웨어가 전체 리셋을 반복했다. 높은 우선순위의 버스 분배 태스크(`bc_dist`)가 낮은 우선순위 기상 태스크(ASI/MET)가 쥔 `select()` 내부 mutex를 기다리는 동안 중간 우선순위 태스크들이 기상 태스크를 밀어냈고, 다음 버스 주기에 스케줄러 태스크(`bc_sched`)가 `bc_dist` 미완료를 발견해 리셋했다. 그 mutex는 상속 옵션이 꺼진 채 만들어져 있었다. JPL은 지상 복제기에서 trace를 켜고 재현했고, 전역 변수를 바꿔 상속을 켜는 패치를 올려 고쳤다. 교훈은 알고리즘이 아니라 기본 설정, 그리고 trace의 가치다.

> After landing in 1997, Pathfinder's VxWorks-based flight software kept doing total system resets. The high-priority bus distribution task was blocked on a mutex inside the select mechanism that was held by the low-priority meteorological task, and medium-priority tasks kept preempting the meteorological task. On the next bus cycle the scheduler task saw that the distribution task hadn't finished — a hard deadline — and reset the system. The mutex had been created with priority inheritance turned off. JPL reproduced it on a ground replica with tracing enabled, and fixed it by uploading a patch that changed a global variable so those semaphores were created with inheritance on. The lesson for me is that the kernel already had the fix; the bug was a configuration default, and it was only found because of time-line tracing.

**꼬리 질문**: "Why didn't they catch it before launch?" → Reeves에 따르면 착륙 전에 이 문제를 본 적은 있었지만 추적하려 할 때 재현되지 않았고, 진입 · 착륙 소프트웨어에 집중하느라 우선순위를 낮게 뒀으며, 발사 전 시험은 "best case" 데이터율 · 과학 활동 위주였다. 간헐적 타이밍 버그는 부하와 위상이 맞아야 나오므로 최악 부하 시험과 trace가 필요하다(M9).

**Q.** "Does using a FreeRTOS mutex fully solve priority inversion?"

**A.** 아니다. 세 가지가 남는다. 첫째, 유한 역전은 그대로라 임계 구역 길이를 관리해야 한다. 둘째, 상속이 사슬을 따라가지 않는다. B를 쥐고 A를 기다리는 M이 있으면 L은 M 높이까지만 올라서 중간 X가 L을 밀 수 있다. 직접 3단 사슬을 만들어 보니 FreeRTOS와 Zephyr 모두 H의 대기가 X의 일에 비례했다. 셋째, 여러 mutex를 쥐면 FreeRTOS는 마지막 mutex를 놓을 때까지 상속을 유지해 무관한 태스크를 늦춘다. 그리고 큐 · ISR · 하드웨어 대기처럼 소유자가 없는 대기에는 상속이 아예 작동하지 않는다.

> No. It turns unbounded inversion into bounded inversion for the simple case, but three things remain. First, the bounded part — the critical section length — is still yours to manage. Second, FreeRTOS inheritance isn't transitive: if the holder is itself blocked on another mutex, the boost isn't propagated down the chain. I built a three-level chain and both FreeRTOS and Zephyr let an unrelated middle task preempt the bottom holder, so the top task's wait grew with that task's work. Third, with nested mutexes FreeRTOS only restores the base priority when the task releases all of them, so it can stay boosted longer than necessary. And inheritance only applies to owned locks — waiting on a queue, an ISR, or a DMA transfer started by a low-priority task can invert priorities with no kernel help.

**꼬리 질문**: "How would you design to avoid it?" → 락 중첩 금지, 임계 구역 측정, 파이프라인 우선순위(생산자 ≥ 소비자), 공유 버스는 게이트키퍼 태스크, 필요한 곳만 ICPP.

**Q.** "How would you debug a control loop that's occasionally late by a few milliseconds?"

**A.** 먼저 시간축 trace를 켠다(SystemView, Tracealyzer, 또는 RAM 링버퍼). 늦은 주기에서 제어 태스크가 ready나 blocked인데 더 낮은 태스크가 CPU를 쥔 구간을 찾고, 제어 태스크가 기다리는 객체와 그 소유자, 그 소유자를 밀어낸 태스크를 적는다. 상속 이벤트가 찍히는지 본다. 안 찍히면 상속 없는 락이다. 그다음 임계 구역 길이 분포를 잰다.

> I'd turn on time-line tracing first — SystemView, Tracealyzer, or just a RAM ring buffer of context switches and lock events — and look at a late period. The signature is the control task being ready or blocked while a lower-priority task holds the CPU. I note what object it's waiting on, who owns it, and who preempted the owner. If I never see priority-inherit events, the lock isn't a mutex. Then I measure hold times for that lock under load, because even with inheritance the bound is only as small as the longest critical section.

**Q.** "Zephyr has CONFIG_PRIORITY_CEILING. Is that the priority ceiling protocol?"

**A.** 아니다. `k_mutex` 상속이 올라갈 수 있는 최고 높이의 상한이다. 기본 -128이라 사실상 제한이 없고, 락을 쓰는 스레드보다 낮게 잡으면 상속이 무력화된다. 실험에서 4로 두자 `k_mutex`인데도 무한 역전이 돌아왔다. Zephyr에는 ICPP가 내장돼 있지 않고, POSIX 계층의 `PTHREAD_PRIO_PROTECT`도 미구현이다.

> No. It caps how high k_mutex inheritance can boost a thread — the default of minus 128 means effectively no cap. If you set it below the priority of the threads using the mutex, inheritance is neutralized; I set it to 4 in a test and got the same linear, unbounded blocking as a plain semaphore. Zephyr doesn't implement an immediate-ceiling protocol in the kernel, and its POSIX layer returns ENOTSUP for both PTHREAD_PRIO_INHERIT and PTHREAD_PRIO_PROTECT, even though pthread mutexes are k_mutexes underneath and do inherit.

**Q.** "Why does a binary semaphore not do priority inheritance but a mutex does?"

**A.** 상속하려면 "누구를 올릴지" 알아야 하는데, 세마포어는 아무나 give할 수 있어 소유자가 없다(ISR이 give하는 신호용이 본래 쓰임, D1). mutex는 take한 태스크가 소유자로 기록되고(`xMutexHolder`, `mutex->owner`) 그 태스크만 give할 수 있으니 커널이 소유자를 올릴 수 있다.

> Inheritance needs to know whom to boost. A semaphore has no owner — any task or ISR can give it, which is exactly what makes it good for signaling. A mutex records the task that took it as the owner and only that task may release it, so the kernel can raise the owner's priority while someone is waiting.

---

## 15. 직접 해보기

1. **개념**: 태스크 L(1), M(2), H(3)와 락 S. L이 S를 4 ms 쥐고, H는 L이 S를 잡은 1 ms 뒤 깨어 S를 요청하며, M은 그 0.5 ms 뒤 깨어 6 ms를 돈다. (a) 상속 없음 (b) PIP (c) ICPP에서 H가 기다리는 시간은?
   정답/힌트: (a) 3 + 6 = 9 ms (b) 3 ms (c) 3 ms (단, ICPP에서는 H가 1 ms에 깨도 CPU를 못 받으므로 "요청" 자체가 4 ms에 일어난다).
2. **코드**: 예제 1의 `inv.c`에 태스크 N(prio 4, 락 없음, t0 + 1.5 ms에 1 ms 일)을 추가하고 `LOCK=1`과 `LOCK=2`에서 N의 응답 시간을 비교하라. ICPP의 "자원을 안 쓰는 태스크도 막는다"가 ceiling 위 태스크에는 해당하지 않음을 확인하라.
   정답/힌트: N은 ceiling(3)보다 높으므로 두 경우 모두 약 1 ms(자기 일)만 걸려야 한다. ceiling을 4로 잘못 잡으면 N도 막힌다.
3. **측정**: 예제 7(`chain.c`)을 ICPP 래퍼로 바꿔(A의 ceiling = 2, B의 ceiling = 4) 같은 W로 돌리고 H의 대기가 W와 무관해지는지 확인하라.
   정답/힌트: M이 L(ceiling 2와 같은 2)을 선점하지 못해 사슬이 생기지 않는다. time slicing을 끄고 돌려야 깔끔하다(§7.2).
4. **소스 읽기**: FreeRTOS `xTaskPriorityInherit()`에 "holder가 다른 mutex에 막혀 있으면 그 holder도 올린다"를 추가하려면 TCB에 무엇이 더 필요한가? 
   정답/힌트: holder가 기다리는 mutex(또는 queue) 포인터가 TCB에 없다. 대기 리스트 아이템의 컨테이너(`xEventListItem.pxContainer`)로 queue를 역추적해야 하고, 사슬 길이 상한과 그 대기 리스트의 재정렬도 필요하다. Linux `rt_mutex`의 `pi_blocked_on`이 이 정보다.
5. **Zephyr**: 예제 6 SCEN 3에서 L이 B를 **부스트된 뒤에** 잡도록(H가 A를 기다리기 시작한 다음 B를 잡도록) 바꿔 다시 재라. 헤더 주석의 규칙대로면 결과가 어떻게 바뀌어야 하나?
   정답/힌트: B의 `owner_orig_prio`가 부스트된 값(1)으로 기록되므로 B를 놓아도 1을 유지, A를 놓을 때 5로 돌아간다. M이 끼어들지 못한다.
6. **설계**: 예제 10의 태스크 세트에 "IMU 드라이버 태스크(prio 6)가 SPI를 30 us 쓴다"를 추가하면 SPI의 ceiling과 각 B_i가 어떻게 바뀌나? `bound.py`로 계산하라.
   정답/힌트: ceiling(spi) = 6이 되어 rate(5)도 logger의 SPI 구간에 막힐 수 있게 된다. 가장 높은 태스크가 공유 버스를 쓰는 순간 모든 태스크의 B가 그 버스의 최장 구간에 묶인다.
7. **측정**: 예제 8의 FIX를 "생산자 우선순위 = 2.5"처럼 소비자보다 낮지만 M보다 높게 두면? (FreeRTOS에서는 정수이므로 우선순위를 L 1, M 2, P 3, H 4로 다시 배치)
   정답/힌트: M이 생산자를 밀 수 없으니 지연이 사라진다. 규칙의 정확한 형태는 "생산자 ≥ 소비자"가 아니라 "생산자 > 소비자를 늦추면 안 되는 모든 태스크"다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| priority inversion | 우선순위 역전 | 높은 태스크가 낮은 태스크 때문에 실행하지 못하는 상태 |
| bounded inversion | 유한 역전 | 대기가 낮은 태스크의 임계 구역 길이로 묶인 역전, 락의 본질적 비용 |
| unbounded inversion | 무한 역전 | 무관한 중간 태스크의 실행까지 대기에 더해져 상한이 없어진 역전 |
| blocking time B_i | 블로킹 시간 | 낮은 태스크 때문에 태스크 i가 기다리는 최악 시간, RTA 식의 B |
| base / effective priority | 기본 / 유효 우선순위 | 설정한 값과 상속으로 올라간 지금 값 (`uxBasePriority`/`uxPriority`) |
| PIP | basic priority inheritance protocol | 막힌 태스크의 우선순위를 소유자가 물려받는 규칙 |
| transitive inheritance | 전이적 상속 | 소유자도 막혀 있으면 사슬 끝의 소유자까지 상속을 전달 |
| chained blocking | 연쇄 블로킹 | PIP에서 락마다 한 번씩 여러 번 막히는 현상 |
| push-through blocking | 밀어내기 블로킹 | 락을 안 쓰는 중간 태스크가 상속받은 소유자 때문에 기다리는 것 |
| priority ceiling | 우선순위 천장 | 자원을 쓸 수 있는 태스크 중 최고 우선순위 |
| OCPP | original priority ceiling protocol | system ceiling보다 높을 때만 잠글 수 있게 하는 규칙 (1990) |
| ICPP | immediate ceiling priority protocol | 잠그는 즉시 ceiling으로 올리는 규칙, highest locker / priority protect |
| system ceiling | 시스템 천장 | 지금 잠긴 자원들의 ceiling 중 최댓값 |
| SRP | Stack Resource Policy | Baker의 ceiling 계열 규칙, 같은 스택 공유 가능 (N4, I9) |
| `uxMutexesHeld` | FreeRTOS TCB 필드 | 태스크가 쥔 mutex 수, 0이 될 때만 상속 해제 |
| `owner_orig_prio` | Zephyr `k_mutex` 필드 | 그 mutex를 잡던 순간 소유자의 우선순위, unlock 때 복귀값 |
| `CONFIG_PRIORITY_CEILING` | Zephyr Kconfig | `k_mutex` 상속 높이의 상한 (PCP 아님) |
| gatekeeper task | 게이트키퍼 태스크 | 자원을 한 태스크만 소유하고 다른 태스크는 요청 큐로 쓰는 패턴 |

---

## 17. 요약 & 체크리스트

우선순위 역전은 높은 태스크가 낮은 태스크가 쥔 자원을 기다리는 상태이고, 락을 쓰는 이상 0으로 만들 수 없다. 문제는 락과 무관한 중간 태스크가 소유자를 밀어내 대기에 상한이 사라지는 무한 역전이다. QEMU에서 binary semaphore로 재현하면 H의 응답이 M의 일과 기울기 1로 늘었고(2.28 → 14.28 ms), FreeRTOS mutex와 Zephyr `k_mutex`의 상속은 이를 L의 남은 임계 구역(약 2.27 ms)으로 묶었다. 이론적으로 PIP는 데드락과 chained blocking을 막지 못하고, OCPP · ICPP는 블로킹 1회 · 데드락 없음을 보장한다. ICPP는 `vTaskPrioritySet`으로 직접 만들 수 있지만 경쟁이 없어도 비용이 PIP의 2배이고 같은 우선순위 time slicing과 얽힌다. 실제 RTOS 구현은 교과서보다 단순하다: FreeRTOS와 Zephyr 모두 상속이 사슬을 따라가지 않아 3단 사슬에서 무한 역전이 되살아났고, 중첩 mutex에서 FreeRTOS는 너무 오래, Zephyr는 잡은 순서 규칙을 어기면 너무 일찍 상속을 푼다. 큐 · ISR · DMA처럼 소유자가 없는 대기는 상속이 돕지 못한다. 그래서 실무 답은 "mutex를 쓰고, 임계 구역을 재서 짧게, 락을 중첩하지 말고, 파이프라인 우선순위를 맞추고, trace로 증명한다"이다. Mars Pathfinder는 이 모든 것을 한 번에 보여 준 사례로, 해결책은 이미 커널 안에 있었고 꺼져 있었을 뿐이다.

- [ ] 유한 역전과 무한 역전을 L/M/H 시간축으로 그리며 30초 안에 설명할 수 있다
- [ ] binary semaphore가 상속을 못 하는 이유(소유자 없음)를 말할 수 있다
- [ ] 예제 1·2의 측정(기울기 1 vs 평평)을 근거로 말할 수 있다
- [ ] PIP의 규칙 4개(막힘, 상속, 해제, 전이)와 남는 문제(데드락, chained, push-through)를 말할 수 있다
- [ ] OCPP의 system ceiling 규칙과 ICPP의 "잡는 즉시 ceiling"을 구분할 수 있다
- [ ] PCP의 B_i = 낮은 태스크의 임계 구역 하나(최장)라는 식을 쓰고 계산할 수 있다
- [ ] FreeRTOS에서 ICPP 래퍼를 쓰고, time slicing · 블로킹 · SMP 조건을 말할 수 있다
- [ ] FreeRTOS `uxMutexesHeld` 규칙과 Zephyr `owner_orig_prio` 규칙의 차이를 2 × 2 표로 설명할 수 있다
- [ ] 두 RTOS 모두 전이적 상속을 하지 않는다는 것을 소스 위치와 실측으로 말할 수 있다
- [ ] `CONFIG_PRIORITY_CEILING`이 무엇이고 무엇이 아닌지 말할 수 있다
- [ ] 큐 · ISR · 스케줄러 잠금 · DMA로 생기는 역전을 예로 들고 대책을 말할 수 있다
- [ ] Mars Pathfinder 이야기를 Reeves의 설명 기준으로 사실대로 말할 수 있다

---

## 참고 자료

- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Transactions on Computers, vol. 39, no. 9, 1990 — PIP와 PCP(OCPP)의 정의와 증명
- T. P. Baker, "Stack-Based Scheduling of Realtime Processes", Real-Time Systems, 1991 — Stack Resource Policy (N4, I9에서)
- Mike Jones, "What really happened on Mars?" (1997-12-05) 및 Glenn E. Reeves의 답장 (1997-12-15) — CMU에 보관된 사본: https://www.cs.cmu.edu/afs/cs/user/raj/www/mars.html , Reeves 답장 전문 사본: https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html
- FreeRTOS-Kernel V11.3.2 소스: `tasks.c`(`xTaskPriorityInherit`, `xTaskPriorityDisinherit`, `vTaskPriorityDisinheritAfterTimeout`, `vTaskPrioritySet`, `pvTaskIncrementMutexHeldCount`), `queue.c`(`xQueueSemaphoreTake`, `prvCopyDataToQueue`), `include/semphr.h`
- FreeRTOS 문서: freertos.org의 Kernel features → Queues, mutexes and semaphores → Mutexes 페이지 (binary semaphore와 mutex의 상속 차이 설명)
- Zephyr v4.4.2 소스: `kernel/mutex.c`(`new_prio_for_inheritance`, `adjust_owner_prio`, 파일 머리 주석의 중첩 규칙), `kernel/sched.c`(`z_thread_prio_set`), `kernel/include/ksched.h`(`z_get_new_prio_with_ceiling`), `kernel/Kconfig`(`PRIORITY_CEILING`), `lib/posix/options/mutex.c`
- Zephyr 문서: Mutexes (Priority Inheritance 절) — https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html
- Linux `kernel/locking/rtmutex.c`(`rt_mutex_adjust_prio_chain`, `max_lock_depth`) — 사슬을 따라가는 PI의 예, 실측은 EL N1 §7
- Jane W. S. Liu, "Real-Time Systems", Prentice Hall, 2000 — 자원 접근 제어 프로토콜과 블로킹 시간 분석을 다루는 교재
- 이 노트의 측정: QEMU 10.2 `mps2-an385`, `-icount shift=5`, FreeRTOS V11.3.2 ARM_CM3 포트, Zephyr v4.4.2 — 실칩 절대값과 다름
