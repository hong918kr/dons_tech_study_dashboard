# D9. 데드락 · 기아 · 라이브락 — "가끔 멈춤"을 재현하고, 대기 그래프와 순위표로 잡는다

> **이 노트를 다 읽으면**: AB/BA 두 뮤텍스 데드락을 FreeRTOS·Zephyr에서 재현하고 태스크 덤프(`uxTaskGetSystemState`, `vTaskListTasks`, `k_thread_foreach` + `pended_on`)만 보고 "누가 무엇을 기다리고 그걸 누가 쥐고 있나"를 읽어 낼 수 있다 · 큐 순환, 혼자만 줄 수 있는 세마포어, 타이머 데몬 막기, 스케줄러 정지·크리티컬 섹션 안 블로킹 같은 **락이 아닌 데드락**을 알아본다 · 락 순위표 + 런타임 순서 검사기, 유한 타임아웃 + 카운터, 대기 그래프 모니터, 태스크별 heartbeat → HW watchdog을 직접 짜서 돌린다 · 우선순위 기아, idle 기아(삭제된 TCB가 안 풀림), 라이브락(예의 바른 재시도), 인터럽트 라이브락을 측정 차트로 보여 줄 수 있다
> **왜 필요한가**: 현장 버그 리포트 "드론이 비행 중 가끔 멈췄다가 watchdog으로 재부팅된다"의 대표 원인이 이 셋이다. 면접에서는 "deadlock의 네 조건은? RTOS에서 어떻게 예방·탐지하나? livelock과 starvation은 어떻게 다른가?"가 단골이고, 답이 "lock ordering"에서 끝나지 않고 **순위표를 코드로 강제하는 법, 멈춘 장비에서 원인을 읽는 법, watchdog 설계**까지 가야 통과한다
> **Don 기준 난이도**: 멀티코어 SSD 펌웨어의 자원 락 순서, watchdog, 실패 로그 남기기는 이미 안다 / 새로 배울 부분은 RTOS 커널이 "누가 무엇을 기다리나"를 어디에 기록하는지(`xEventListItem`의 컨테이너, `pxMutexHolder`, Zephyr `pended_on`), 락이 아닌 RTOS 고유 데드락(큐·타이머 데몬·스케줄러 정지), idle 태스크가 굶으면 메모리가 새는 FreeRTOS 동작, 그리고 FreeRTOS 이벤트 그룹의 heartbeat 함정
> **선행 노트**: A4(태스크 모델), C1(커널 자료구조 — ready/delayed/event 리스트), C4(스케줄링 정책), C7(크리티컬 섹션 · 스케줄러 잠금), C9(idle · hook), D1(세마포어), D2(뮤텍스), D3(우선순위 역전 · PCP — 이 노트는 참조만), EL F4(Linux 락 · lockdep — 참조만). 이어지는 노트: D6(소프트웨어 타이머), D7(게이트키퍼 · 작업 위임), E4(인터럽트 폭주 대비), M(디버깅 모듈 — HardFault · RTOS-aware 디버깅)

---

## 0. 큰 그림 — "가끔 멈춤"의 세 얼굴

Don이 SSD 펌웨어에서 봤던 장면을 떠올려 보자. 고부하 시험 수십 시간째에 컨트롤러가 응답을 멈춘다. JTAG을 붙여 보면 코어 0은 NAND 채널 3의 락을 쥔 채 버퍼 풀의 빈 버퍼를 기다리고, 코어 1은 버퍼를 하나 쥔 채 채널 3의 락을 기다린다. 아무도 죽지 않았고, 아무 예외도 없다. 그냥 **서로 기다린다**. 이것이 데드락(deadlock)이다.

RTOS에서도 똑같은 일이 생긴다. 차이는 "코어" 대신 "태스크"가, "스핀" 대신 "Blocked 상태"가 기다린다는 점이다. 그리고 RTOS에는 데드락의 친척이 둘 더 있다.

- **데드락(deadlock)**: 둘 이상의 태스크가 서로가 가진 것을 기다려서 **아무도 영원히 진행하지 못하는** 상태. 모두 Blocked다. CPU는 idle이 쓴다.
- **기아(starvation)**: 시스템은 돌아가는데 **특정 태스크만 계속 밀려서** 진행하지 못하는 상태. 그 태스크는 Ready(또는 대기열 맨 뒤)인데 차례가 안 온다.
- **라이브락(livelock)**: 태스크들이 **계속 실행은 하는데** 서로 양보·재시도만 반복해서 아무 일도 끝내지 못하는 상태. CPU는 100% 바쁘다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">밖에서 보면 셋 다 "멈춤" — 안을 보면 다르다</text>
<rect x="10" y="35" width="210" height="200" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115" y="58" font-size="14" font-weight="bold" text-anchor="middle">데드락</text>
<text x="115" y="80" font-size="12" text-anchor="middle">서로 상대를 기다린다</text>
<text x="22" y="110" font-size="12">상태: 관련 태스크 모두 Blocked</text>
<text x="22" y="134" font-size="12">CPU: idle만 돈다</text>
<text x="22" y="158" font-size="12">진척: 0, 영원히</text>
<text x="22" y="182" font-size="12">탐지: 대기 그래프의 순환</text>
<text x="22" y="206" font-size="12">예방: 락 순위표</text>
<rect x="235" y="35" width="210" height="200" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="58" font-size="14" font-weight="bold" text-anchor="middle">기아</text>
<text x="340" y="80" font-size="12" text-anchor="middle">누군가 계속 밀린다</text>
<text x="247" y="110" font-size="12">상태: Ready인데 못 돈다</text>
<text x="247" y="134" font-size="12">CPU: 다른 태스크가 독점</text>
<text x="247" y="158" font-size="12">진척: 0 또는 아주 느림</text>
<text x="247" y="182" font-size="12">탐지: 진척 카운터, heartbeat</text>
<text x="247" y="206" font-size="12">예방: 우선순위·부하 설계</text>
<rect x="460" y="35" width="210" height="200" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="565" y="58" font-size="14" font-weight="bold" text-anchor="middle">라이브락</text>
<text x="565" y="80" font-size="12" text-anchor="middle">돌긴 도는데 헛돈다</text>
<text x="472" y="110" font-size="12">상태: Running / Ready</text>
<text x="472" y="134" font-size="12">CPU: 100% 사용</text>
<text x="472" y="158" font-size="12">진척: 0 (재시도만 반복)</text>
<text x="472" y="182" font-size="12">탐지: 시도 대비 완료 비율</text>
<text x="472" y="206" font-size="12">예방: 비대칭·무작위 백오프</text>
</svg>
```
그림 1 — 세 가지 "멈춤". 데드락은 모두 잠들어 있고, 기아는 한 명만 못 먹고, 라이브락은 모두 깨어 있는데 헛돈다. 증상만으로는 구별이 안 되므로 **태스크 상태와 카운터를 같이 봐야** 한다.

이 노트의 순서는 이렇다. 데드락의 조건(§1) → FreeRTOS·Zephyr에서 재현하고 밖에서 읽기(§2) → 락이 아닌 데드락(§3) → 예방(§4) → 탐지와 복구(§5) → 기아(§6) → 라이브락(§7) → 커널 안(§8) → 실무(§9).

경계: Linux 커널의 lockdep(락 의존 그래프를 자동으로 만들어 "아직 안 일어난 데드락"까지 경고하는 검사기)은 EL F4 §13에서 다뤘다. 여기서는 **lockdep이 없는 MCU에서 같은 일을 손으로 하는 법**만 쓴다. 우선순위 역전과 PIP/PCP는 D3, 뮤텍스 API 기본은 D2가 주인이다. 교과서식 데드락 이론(자원 할당 그래프, 은행원 알고리즘)은 Don의 OSTEP 노트(락 · 동시성 버그 장)와 Verkada 동시성 노트에 있으니 다시 쓰지 않는다.

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) + FreeRTOS V11.3.2(ARM_CM3 포트, 1 kHz tick) / 같은 보드의 Zephyr v4.4.2(100 Hz tick). 모든 FreeRTOS 실행은 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 결정적)이다. **수치는 QEMU icount 기준이고 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·예외 스태킹 미모델링). 작성 시점(10-31) VM 루트 파일시스템이 emergency read-only 상태라 빌드 산출물은 tmpfs `/tmp/rtos_D9/`에 두었다(평소 위치는 `/var/tmp/rtos/D9/`).

모든 FreeRTOS 예제는 아래 작은 스크립트로 빌드·실행했다. 템플릿 Makefile을 그대로 쓰고(`-Wall -Wextra`), 경고가 있으면 보여 준다.

```sh
# ~/.rtos_scratch/D9/run.sh <이름> [make 추가 인자]
N=$1; shift
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D9/$N.c OUT=/tmp/rtos_D9/$N "$@" >/tmp/rtos_D9/$N.build 2>&1 || { cat /tmp/rtos_D9/$N.build; exit 1; }
grep -iE "warning|error" /tmp/rtos_D9/$N.build
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D9/$N/app.elf ${TO:-20}
echo "[rc=$?]"
```

예제들이 같이 쓰는 덤프 헬퍼다. `uxTaskGetSystemState()`는 `configUSE_TRACE_FACILITY=1`일 때 생기는 API로, 모든 태스크의 이름·상태·현재/기본 우선순위·스택 여유를 `TaskStatus_t` 배열로 돌려준다.

```c
/* D9 공용 헬퍼: 태스크 상태 덤프 */
#ifndef D9_COMMON_H
#define D9_COMMON_H
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"
static const char *st_name(eTaskState s) {
    static const char *n[] = {"Running","Ready","Blocked","Suspended","Deleted","Invalid"};
    return n[s];
}
static inline void dump_tasks(void) {
    static TaskStatus_t ts[12];
    UBaseType_t n = uxTaskGetSystemState(ts, 12, NULL);
    board_printf_raw("  %s\n", "name      state      prio(cur/base)  stackHWM(words)");
    for (UBaseType_t i = 0; i < n; i++)
        board_printf_raw("  %s\t%s\t%u/%u\t%u\n", ts[i].pcTaskName, st_name(ts[i].eCurrentState),
                         (unsigned)ts[i].uxCurrentPriority, (unsigned)ts[i].uxBasePriority,
                         (unsigned)ts[i].usStackHighWaterMark);
}
#endif
```

---

## 1. 데드락이란 — Coffman의 네 조건

### 1.1 직관

좁은 일차선 다리 양쪽에서 차 두 대가 동시에 들어왔다. 둘 다 "상대가 비켜 주면 간다"고 기다린다. 아무도 후진하지 않으면 영원히 서 있다. 여기에 데드락의 네 재료가 다 들어 있다.

1971년 Coffman, Elphick, Shoshani는 데드락이 생기려면 **네 조건이 동시에** 성립해야 한다고 정리했다. 거꾸로 말하면 **하나만 깨도 데드락은 불가능**하다. 예방 전략은 전부 "어느 조건을 깰 것인가"의 선택이다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">Coffman 네 조건 — RTOS 예와 깨는 법</text>
<rect x="8" y="34" width="160" height="150" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="88" y="56" font-size="13" font-weight="bold" text-anchor="middle">1. 상호 배제</text>
<text x="88" y="74" font-size="12" text-anchor="middle">mutual exclusion</text>
<text x="88" y="100" font-size="12" text-anchor="middle">뮤텍스는 한 명만</text>
<text x="88" y="130" font-size="12" text-anchor="middle">깨기: 공유를 없앰</text>
<text x="88" y="150" font-size="12" text-anchor="middle">게이트키퍼 태스크</text>
<text x="88" y="170" font-size="12" text-anchor="middle">lock-free 큐 (D8)</text>
<rect x="174" y="34" width="160" height="150" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="254" y="56" font-size="13" font-weight="bold" text-anchor="middle">2. 쥔 채 기다림</text>
<text x="254" y="74" font-size="12" text-anchor="middle">hold and wait</text>
<text x="254" y="100" font-size="12" text-anchor="middle">SD 쥔 채 SPI 대기</text>
<text x="254" y="130" font-size="12" text-anchor="middle">깨기: 한 번에 다 잡기</text>
<text x="254" y="150" font-size="12" text-anchor="middle">또는 try-lock 실패 시</text>
<text x="254" y="170" font-size="12" text-anchor="middle">쥔 것을 다 놓기</text>
<rect x="340" y="34" width="160" height="150" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="420" y="56" font-size="13" font-weight="bold" text-anchor="middle">3. 뺏을 수 없음</text>
<text x="420" y="74" font-size="12" text-anchor="middle">no preemption</text>
<text x="420" y="100" font-size="12" text-anchor="middle">남의 뮤텍스 못 뺏음</text>
<text x="420" y="130" font-size="12" text-anchor="middle">깨기: 타임아웃 후</text>
<text x="420" y="150" font-size="12" text-anchor="middle">스스로 포기·롤백</text>
<text x="420" y="170" font-size="12" text-anchor="middle">(복구 코드가 필요)</text>
<rect x="506" y="34" width="160" height="150" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="586" y="56" font-size="13" font-weight="bold" text-anchor="middle">4. 순환 대기</text>
<text x="586" y="74" font-size="12" text-anchor="middle">circular wait</text>
<text x="586" y="100" font-size="12" text-anchor="middle">A → B → A 순환</text>
<text x="586" y="130" font-size="12" text-anchor="middle">깨기: 락 순위표</text>
<text x="586" y="150" font-size="12" text-anchor="middle">+ 런타임 순서 검사</text>
<text x="586" y="170" font-size="12" text-anchor="middle">PCP/ICPP도 여기 (D3)</text>
<text x="10" y="210" font-size="12">네 조건이 동시에 성립해야 데드락이다 → 하나만 깨도 예방된다. 펌웨어 실무의 1순위는 4번(순위표)이다.</text>
</svg>
```
그림 2 — 네 조건과 각각을 깨는 전형적인 방법. 1번은 설계를 바꾸는 일이고, 2·3번은 코드 경로마다 복구 로직이 필요하고, 4번은 규칙 하나로 전체를 막는다.

### 1.2 정의를 RTOS 말로 옮기면

| 조건 | 뜻 | RTOS에서 성립하는 이유 | 깨는 법 (이 노트 절) |
|---|---|---|---|
| 상호 배제 | 자원을 한 번에 한 태스크만 쓴다 | 뮤텍스·이진 세마포어의 존재 이유 | 공유 대신 소유권 이전·게이트키퍼 (§4.4, D7) |
| 쥔 채 기다림 | 자원 하나를 쥔 채 다른 자원을 기다린다 | `xSemaphoreTake` 두 번 중첩 | try-lock + 전부 놓기 (§4.3, §7 함정) |
| 뺏을 수 없음 | 남이 쥔 자원을 강제로 회수 못 한다 | 뮤텍스는 소유자만 놓는다 (`k_mutex_unlock`은 남이 부르면 `-EPERM`) | 유한 타임아웃 → 스스로 놓기 (§5.1) |
| 순환 대기 | 대기 관계가 원을 이룬다 | 태스크마다 락 순서가 다름 | 전역 순위표 + 검사기 (§4.1–4.2), PCP/ICPP (D3) |

여기서 **대기 관계(wait-for relation)**란 "태스크 T가 자원 R을 기다리고, R은 태스크 U가 쥐고 있다 → T는 U를 기다린다"는 화살표다. 이 화살표로 그린 그래프를 **대기 그래프(wait-for graph)**라고 부르고, 데드락은 정확히 **이 그래프에 순환(cycle)이 있는 상태**다. §5.2에서 이 그래프를 런타임에 그리는 모니터를 짠다.

### 1.3 흔한 함정

- "우리 시스템은 단일 코어라 데드락이 없다" — 틀렸다. 데드락은 병렬 실행이 아니라 **선점 + 블로킹**에서 생긴다. 단일 코어 RTOS에서도 §2에서 1초 만에 멈춘다.
- "뮤텍스를 안 쓰면 데드락이 없다" — 틀렸다. 큐·세마포어·이벤트 그룹·타이머 데몬으로도 순환 대기가 생긴다(§3).
- "priority inheritance가 있으니 괜찮다" — 상속은 우선순위 역전을 줄일 뿐 순환을 깨지 않는다. §2의 덤프에서 상속이 일어난 채로 데드락이 걸린 모습을 직접 본다. 데드락을 원천적으로 막는 것은 PCP/ICPP 쪽이다(D3에서 유도).

---

## 2. AB/BA 데드락 재현 — "한참 잘 돌다가 가끔 멈춘다"

### 2.1 시나리오 — 드론 FC의 로거와 IMU

예를 들어 드론 비행 제어기라면 이런 코드가 실제로 있다.

- `logger` 태스크(prio 2): SD 카드 로그 버퍼를 지키는 `mSD`를 잡고, 로그를 포맷한 뒤, SD 카드가 붙은 SPI 버스를 쓰려고 `mSPI`를 잡는다. 순서: **SD → SPI**.
- `imu` 태스크(prio 3, 2 ms 주기): SPI 버스로 IMU를 읽으려고 `mSPI`를 잡는다. 그런데 16번에 한 번 "이상치" 경로에서 그 자리에서 바로 로그를 남기려고 `mSD`를 잡는다. 순서: **SPI → SD**. 반대다.

평소에는 문제가 없다. 이상치 경로는 드물고, 그 순간에 logger가 마침 SD만 쥐고 SPI를 기다리기 직전이어야 하기 때문이다. 로그 길이(포맷 시간)는 매번 다르게 LFSR로 흉내 냈다.

무엇을 확인하는 코드인가: 순서가 반대인 두 락 경로가 언제 멈추는지, 멈춘 뒤 덤프가 무엇을 보여 주는지.

```c
/* ex1: AB/BA 두 뮤텍스 데드락 — 한참 잘 돌다가 "가끔" 멈춘다 */
#include "common.h"
static SemaphoreHandle_t mSD, mSPI;           /* SD 카드 로그 뮤텍스, SPI 버스 뮤텍스 */
static volatile uint32_t logCnt, imuCnt;
static void busy_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) {} }
#ifndef SEED
#define SEED 0xACE1u
#endif
static uint32_t lfsr = SEED;
static uint32_t rnd(void) { lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u); return lfsr; }
static void logger(void *p) {                 /* 순서: SD → SPI */
    (void)p;
    for (;;) {
        xSemaphoreTake(mSD, portMAX_DELAY);
        busy_us(100 + rnd() % 900);            /* 로그 포맷팅: 길이가 매번 다르다 */
        xSemaphoreTake(mSPI, portMAX_DELAY);   /* SD 카드는 SPI 버스 위에 있다 */
        logCnt++;
        xSemaphoreGive(mSPI); xSemaphoreGive(mSD);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static void imu(void *p) {                    /* 순서: SPI → SD (반대!) — 드문 경로에서만 */
    (void)p;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(2));
        xSemaphoreTake(mSPI, portMAX_DELAY);
        busy_us(50);                           /* IMU burst read */
        if (++imuCnt % 16 == 0) {              /* 이상치일 때만 SD에 바로 기록 */
            xSemaphoreTake(mSD, portMAX_DELAY);
            xSemaphoreGive(mSD);
        }
        xSemaphoreGive(mSPI);
    }
}
static const char *holder(SemaphoreHandle_t m) {
    TaskHandle_t h = xSemaphoreGetMutexHolder(m);
    return h ? pcTaskGetName(h) : "(free)";
}
static void monitor(void *p) {
    (void)p;
    static char buf[400];
    uint32_t prevL = 0, prevI = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        uint32_t l = logCnt, i = imuCnt;
#ifndef QUIET
        board_printf_raw("t=%u ms logCnt=%u imuCnt=%u\n", (unsigned)xTaskGetTickCount(), (unsigned)l, (unsigned)i);
#endif
        if (l == prevL && i == prevI) break;
        if (xTaskGetTickCount() > 60000) { board_printf_raw("NO_DEADLOCK\n"); board_exit(0); }   /* 100 ms 동안 둘 다 진전 없음 */
        prevL = l; prevI = i;
    }
#ifdef QUIET
    board_printf_raw("HANG_AT_MS=%u logCnt=%u\n", (unsigned)xTaskGetTickCount(), (unsigned)logCnt); board_exit(0);
#endif
    board_printf_raw("-- uxTaskGetSystemState --\n"); dump_tasks();
    board_printf_raw("-- vTaskListTasks (name state prio hwm num) --\n");
    vTaskListTasks(buf, sizeof buf); board_puts(buf);
    board_printf_raw("holder(mSD)=%s holder(mSPI)=%s\n", holder(mSD), holder(mSPI));
    board_exit(0);
}
int main(void) {
    mSD = xSemaphoreCreateMutex(); mSPI = xSemaphoreCreateMutex();
    xTaskCreate(logger, "logger", 256, NULL, 2, NULL);
    xTaskCreate(imu, "imu", 256, NULL, 3, NULL);
    xTaskCreate(monitor, "monitor", 512, NULL, 4, NULL);
    vTaskStartScheduler();
    for (;;) {}
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/D9/run.sh ex1 EXTRA_CFLAGS=-DINCLUDE_xSemaphoreGetMutexHolder=1'
```

```text
t=100 ms logCnt=20 imuCnt=49
t=200 ms logCnt=40 imuCnt=99
t=300 ms logCnt=60 imuCnt=149
t=400 ms logCnt=80 imuCnt=199
t=500 ms logCnt=100 imuCnt=249
t=600 ms logCnt=120 imuCnt=299
t=700 ms logCnt=140 imuCnt=349
t=800 ms logCnt=159 imuCnt=399
t=900 ms logCnt=179 imuCnt=449
t=1000 ms logCnt=196 imuCnt=496
t=1100 ms logCnt=196 imuCnt=496
-- uxTaskGetSystemState --
  name      state      prio(cur/base)  stackHWM(words)
  monitor	Running	4/4	474
  IDLE	Ready	0/0	230
  Tmr Svc	Blocked	7/7	480
  imu	Blocked	3/3	224
  logger	Blocked	3/2	220
-- vTaskListTasks (name state prio hwm num) --
monitor        	X	4	462	3
IDLE           	R	0	230	4
Tmr Svc        	B	7	480	5
imu            	B	3	224	2
logger         	B	3	220	1
holder(mSD)=logger holder(mSPI)=imu
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: 1초 가까이 정상으로 돌다가(카운터가 100 ms마다 20·50씩 오른다) t=1000 ms 이후 **두 카운터가 동시에 멈췄다**. `imuCnt=496`은 16의 배수다 — 정확히 이상치 경로에서 멈췄다. 덤프에서 두 태스크 모두 `Blocked`이고, `logger`의 우선순위가 `3/2`(현재 3, 기본 2)로 올라가 있다. imu가 logger가 쥔 `mSD`를 기다리면서 **priority inheritance가 일어났지만 데드락은 그대로**다. `xSemaphoreGetMutexHolder`로 소유자를 물으면 `mSD`는 logger, `mSPI`는 imu — 그림이 완성된다.

`vTaskListTasks()`는 V11에서 `vTaskList()`의 새 이름(버퍼 크기를 받는 안전한 버전)이다. 상태 글자는 X=Running, R=Ready, B=Blocked, S=Suspended, D=Deleted. 마지막 열은 태스크 번호다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">데드락이 생기는 순간 (시간 →, tick 경계 하나를 확대)</text>
<text x="10" y="62" font-size="12">imu (3)</text>
<text x="10" y="102" font-size="12">logger (2)</text>
<text x="10" y="142" font-size="12">IDLE (0)</text>
<line x1="90" y1="160" x2="670" y2="160" stroke="currentColor"/>
<rect x="90" y="85" width="150" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="165" y="101" font-size="12" text-anchor="middle">SD 잡고 포맷 중</text>
<line x1="240" y1="40" x2="240" y2="165" stroke="#888" stroke-dasharray="4 3"/>
<text x="240" y="178" font-size="12" text-anchor="middle">tick: imu 깨어남</text>
<rect x="240" y="45" width="130" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="305" y="61" font-size="12" text-anchor="middle">SPI 잡고 IMU 읽기</text>
<rect x="370" y="45" width="10" height="24" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="386" y="40" font-size="12">이상치 → SD 요청: Blocked</text>
<rect x="380" y="85" width="120" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="440" y="101" font-size="12" text-anchor="middle">포맷 마저 (prio 3)</text>
<rect x="500" y="85" width="10" height="24" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="516" y="80" font-size="12">SPI 요청: Blocked</text>
<rect x="510" y="125" width="160" height="24" fill="#888" fill-opacity="0.3" stroke="#888"/>
<text x="590" y="141" font-size="12" text-anchor="middle">영원히 idle</text>
<rect x="90" y="200" width="580" height="16" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
<text x="96" y="212" font-size="12">mSD 소유: logger (끝까지 놓지 않음)</text>
<rect x="240" y="230" width="430" height="16" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
<text x="246" y="242" font-size="12">mSPI 소유: imu (끝까지 놓지 않음)</text>
<line x1="375" y1="72" x2="375" y2="198" stroke="#d0564a" marker-end="url(#ah1)"/>
<line x1="505" y1="112" x2="505" y2="228" stroke="#d0564a" marker-end="url(#ah1)"/>
<text x="90" y="264" font-size="12">빨간 화살표 = "이 락을 기다린다". 두 화살표가 서로 상대의 소유 막대를 가리키면 순환이다.</text>
</svg>
```
그림 3 — logger가 SD만 쥐고 포맷하는 동안 tick 경계가 지나면 imu가 선점한다. imu는 SPI를 쥐고 드문 경로에서 SD를 요청해 막히고, 상속받은 logger는 포맷을 끝낸 뒤 SPI를 요청해 막힌다. 포맷이 tick 경계를 넘을 만큼 길 때만 생기므로 "가끔"이다.

### 2.2 "가끔"은 얼마나 가끔인가 — 시드 20개로 측정

같은 코드를 LFSR 시드만 바꿔(-DSEED, -DQUIET로 마지막 줄만 출력) 20번 돌리고 멈춘 시각을 적었다. 60초 동안 안 멈추면 `NO_DEADLOCK`을 찍도록 했다.

```sh
for s in 0x1 0x2B 0x3C7 ... 0x5555; do      # 시드 20개 (전체 목록은 아래 출력)
  make -s -f .../freertos-mps2/Makefile SRCS=.../ex1.c OUT=/tmp/rtos_D9/ex1s -B \
       EXTRA_CFLAGS="-DINCLUDE_xSemaphoreGetMutexHolder=1 -DQUIET -DSEED=$s"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D9/ex1s/app.elf 30 | grep -E "HANG|NO_DEAD"
done
```

```text
0x1 HANG_AT_MS=41200 logCnt=8096
0x2B HANG_AT_MS=18700 logCnt=3657
0x3C7 HANG_AT_MS=1900 logCnt=352
0x4D1 HANG_AT_MS=22300 logCnt=4367
0x5A5A HANG_AT_MS=47100 logCnt=9260
0x6B2 HANG_AT_MS=6400 logCnt=1231
0x7F01 HANG_AT_MS=32700 logCnt=6422
0x811 HANG_AT_MS=24000 logCnt=4714
0x9C3 HANG_AT_MS=5500 logCnt=1052
0xA77 HANG_AT_MS=7400 logCnt=1438
0xB0B HANG_AT_MS=2500 logCnt=462
0xC35 HANG_AT_MS=200 logCnt=6
0xD19 HANG_AT_MS=1100 logCnt=189
0xE4F HANG_AT_MS=20200 logCnt=3973
0xF00D HANG_AT_MS=12600 logCnt=2453
0x1234 HANG_AT_MS=38500 logCnt=7581
0x2468 HANG_AT_MS=21600 logCnt=4227
0x3579 HANG_AT_MS=32000 logCnt=6294
0x4321 HANG_AT_MS=32600 logCnt=6422
0x5555 HANG_AT_MS=10500 logCnt=2033
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">같은 코드, 시드만 다르게 20번: 멈출 때까지 걸린 시간 (QEMU icount)</text>
<line x1="60" y1="240" x2="660" y2="240" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="240" stroke="currentColor"/>
<line x1="56" y1="240.0" x2="660" y2="240.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="244.0" font-size="12" text-anchor="end">0</text>
<line x1="56" y1="200.0" x2="660" y2="200.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="204.0" font-size="12" text-anchor="end">10</text>
<line x1="56" y1="160.0" x2="660" y2="160.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="164.0" font-size="12" text-anchor="end">20</text>
<line x1="56" y1="120.0" x2="660" y2="120.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="124.0" font-size="12" text-anchor="end">30</text>
<line x1="56" y1="80.0" x2="660" y2="80.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="84.0" font-size="12" text-anchor="end">40</text>
<line x1="56" y1="40.0" x2="660" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="44.0" font-size="12" text-anchor="end">50</text>
<text x="14" y="140" font-size="12" transform="rotate(-90 14 140)" text-anchor="middle">초 (s)</text>
<rect x="63.0" y="239.2" width="24.0" height="0.8" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="75.0" y="235.2" font-size="12" text-anchor="middle">0.2</text>
<rect x="93.0" y="235.6" width="24.0" height="4.4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<rect x="123.0" y="232.4" width="24.0" height="7.6" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="135.0" y="228.4" font-size="12" text-anchor="middle">1.9</text>
<rect x="153.0" y="230.0" width="24.0" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="183.0" y="218.0" width="24.0" height="22.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="195.0" y="214.0" font-size="12" text-anchor="middle">5.5</text>
<rect x="213.0" y="214.4" width="24.0" height="25.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="243.0" y="210.4" width="24.0" height="29.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="255.0" y="206.4" font-size="12" text-anchor="middle">7.4</text>
<rect x="273.0" y="198.0" width="24.0" height="42.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="303.0" y="189.6" width="24.0" height="50.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="315.0" y="185.6" font-size="12" text-anchor="middle">12.6</text>
<rect x="333.0" y="165.2" width="24.0" height="74.8" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="363.0" y="159.2" width="24.0" height="80.8" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="375.0" y="155.2" font-size="12" text-anchor="middle">20.2</text>
<rect x="393.0" y="153.6" width="24.0" height="86.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="423.0" y="150.8" width="24.0" height="89.2" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="435.0" y="146.8" font-size="12" text-anchor="middle">22.3</text>
<rect x="453.0" y="144.0" width="24.0" height="96.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="483.0" y="112.0" width="24.0" height="128.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="495.0" y="108.0" font-size="12" text-anchor="middle">32.0</text>
<rect x="513.0" y="109.6" width="24.0" height="130.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="525.0" y="105.6" font-size="12" text-anchor="middle">32.6</text>
<rect x="543.0" y="109.2" width="24.0" height="130.8" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="555.0" y="105.2" font-size="12" text-anchor="middle">32.7</text>
<rect x="573.0" y="86.0" width="24.0" height="154.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="585.0" y="82.0" font-size="12" text-anchor="middle">38.5</text>
<rect x="603.0" y="75.2" width="24.0" height="164.8" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="615.0" y="71.2" font-size="12" text-anchor="middle">41.2</text>
<rect x="633.0" y="51.6" width="24.0" height="188.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="645.0" y="47.6" font-size="12" text-anchor="middle">47.1</text>
<text x="360" y="260" font-size="12" text-anchor="middle">LFSR 시드 20개 (짧은 순 정렬) — 최소 0.2 s, 중앙값 19.5 s, 최대 47.1 s</text>
<rect x="70" y="274" width="12" height="12" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="88" y="285" font-size="12">2 s 안에 멈춤 (출하 전 짧은 시험에서 잡힐 수 있는 것)</text>
<rect x="420" y="274" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="438" y="285" font-size="12">더 오래 걸림</text>
</svg>
```
그림 4 — 같은 버그가 0.2 s 만에 드러나기도 하고 47 s를 버티기도 한다. 20번 모두 결국 멈췄지만, 2 s짜리 스모크 테스트였다면 3/20만 잡혔다. 실제 칩에서는 인터럽트·센서 데이터까지 타이밍을 흔들므로 이런 분포를 미리 잴 수도 없다 — "몇 시간 뒤 가끔"이 이렇게 생긴다.

이 숫자가 주는 교훈은 하나다. **데드락은 테스트로 "없음"을 증명할 수 없다.** 그래서 §4에서 "순서 위반이 실행되는 그 순간" 잡는 검사기를 만든다. 같은 시드에서 데드락은 1000 ms에 났지만 검사기는 32 ms에 잡는다.

### 2.3 멈춘 장비의 안을 gdb로 보기 — 커널은 이미 다 알고 있다

FreeRTOS에는 "태스크 T가 무엇을 기다리나"를 돌려주는 공개 API가 없다. 하지만 커널은 그 정보를 **TCB 안에** 갖고 있다. 블록될 때 `vTaskPlaceOnEventList()`가 태스크의 `xEventListItem`을 대기 대상 큐의 `xTasksWaitingToReceive` 리스트에 꽂기 때문에, 리스트 항목의 컨테이너 포인터가 곧 "기다리는 큐"다. 디버거로 이걸 직접 확인한다.

무엇을 확인하는 코드인가: 데드락 순간(monitor가 `board_exit`를 부르는 지점)에 멈춰서 TCB·Queue_t 내부를 읽는 gdb 스크립트.

```text
set pagination off
set confirm off
target remote :3391
break board_exit
continue
set $lg = (TCB_t *)((Queue_t *)mSD)->u.xSemaphore.xMutexHolder
set $im = (TCB_t *)((Queue_t *)mSPI)->u.xSemaphore.xMutexHolder
printf "mSD  holder = %s,  mSPI holder = %s\n", $lg->pcTaskName, $im->pcTaskName
printf "logger.xEventListItem.pvContainer = %p\n", $lg->xEventListItem.pvContainer
printf "&mSPI->xTasksWaitingToReceive     = %p\n", &((Queue_t *)mSPI)->xTasksWaitingToReceive
printf "imu.xEventListItem.pvContainer    = %p\n", $im->xEventListItem.pvContainer
printf "&mSD->xTasksWaitingToReceive      = %p\n", &((Queue_t *)mSD)->xTasksWaitingToReceive
printf "logger.xStateListItem.pvContainer = %p  (xSuspendedTaskList = %p)\n", $lg->xStateListItem.pvContainer, &xSuspendedTaskList
printf "logger uxPriority=%u uxBasePriority=%u uxMutexesHeld=%u\n", $lg->uxPriority, $lg->uxBasePriority, $lg->uxMutexesHeld
printf "imu    uxPriority=%u uxBasePriority=%u uxMutexesHeld=%u\n", $im->uxPriority, $im->uxBasePriority, $im->uxMutexesHeld
printf "mSD waiters=%u  mSPI waiters=%u\n", ((Queue_t *)mSD)->xTasksWaitingToReceive.uxNumberOfItems, ((Queue_t *)mSPI)->xTasksWaitingToReceive.uxNumberOfItems
kill
quit
```

```sh
limactl shell elinux -- bash -lc '(timeout 60 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
   -serial file:/tmp/rtos_D9/ex2.uart -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off \
   -gdb tcp::3391 -S -kernel /tmp/rtos_D9/ex1/app.elf &); sleep 1; \
   timeout 50 gdb -q -batch -x /Users/donh/.rtos_scratch/D9/ex2.gdb /tmp/rtos_D9/ex1/app.elf'
```

```text
Breakpoint 1, board_exit (code=code@entry=0) at /var/rtoslab/templates/common/board.c:81
81	    board_printf_raw("[board_exit] code=%d\n", code);
mSD  holder = logger,  mSPI holder = imu
logger.xEventListItem.pvContainer = 0x200013dc
&mSPI->xTasksWaitingToReceive     = 0x200013dc
imu.xEventListItem.pvContainer    = 0x2000137c
&mSD->xTasksWaitingToReceive      = 0x2000137c
logger.xStateListItem.pvContainer = 0x200010ac  (xSuspendedTaskList = 0x200010ac)
logger uxPriority=3 uxBasePriority=2 uxMutexesHeld=1
imu    uxPriority=3 uxBasePriority=3 uxMutexesHeld=1
mSD waiters=1  mSPI waiters=1
[Inferior 1 (process 1) killed]
```

출력에서 볼 것: 세 가지 놀라운 사실이 있다.

1. logger의 `xEventListItem` 컨테이너 주소(`0x200013dc`)가 **`mSPI`의 수신 대기 리스트 주소와 정확히 같다**. imu는 `mSD` 쪽(`0x2000137c`). 대기 그래프의 "기다림" 화살표가 TCB 안에 이미 있다.
2. `portMAX_DELAY`로 기다리는 태스크의 `xStateListItem`은 delayed 리스트가 아니라 **`xSuspendedTaskList`**에 들어 있다(`tasks.c` `prvAddCurrentTaskToDelayedList`: `xTicksToWait == portMAX_DELAY`이고 `INCLUDE_vTaskSuspend == 1`이면 시간으로 깨어나지 않도록 suspended 리스트에 넣는다). 그런데도 덤프가 `Blocked`라고 말하는 것은 `eTaskGetState()`가 "suspended 리스트에 있지만 이벤트 리스트에도 걸려 있으면 Blocked"로 판정하기 때문이다.
3. 소스에는 필드 이름이 `pxContainer`인데 gdb는 `pvContainer`만 안다. `FreeRTOS.h` 끝의 하위 호환 블록(`configENABLE_BACKWARD_COMPATIBILITY`, 기본 1)에 `#define pxContainer pvContainer`가 있어서 **컴파일된 구조체의 실제 멤버 이름은 `pvContainer`**다. 디버거 스크립트·RTOS-aware 플러그인을 쓸 때 걸리는 함정이다.

### 2.4 Zephyr에서 같은 데드락 — `pended_on`으로 대기 그래프 따라가기

Zephyr는 블록된 스레드가 무엇에 매달려 있는지를 `thread->base.pended_on`(대기 큐 `_wait_q_t` 포인터)에 기록한다. `struct k_mutex`의 첫 멤버가 `wait_q`라서, `pended_on == &mutex->wait_q`이면 그 뮤텍스를 기다리는 것이다. `k_thread_foreach()`(`CONFIG_THREAD_MONITOR=y` 필요)로 모든 스레드를 돌면서 이걸 읽으면 커널 데이터만으로 대기 그래프를 만들 수 있다.

무엇을 확인하는 코드인가: Zephyr AB/BA 데드락 후 스레드 덤프와 그래프 따라가기.

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_NAME=y
CONFIG_QEMU_ICOUNT_SHIFT=5
# CMakeLists.txt 끝에: target_compile_options(app PRIVATE -Wall -Wextra)
```

```c
/* z1: Zephyr AB/BA 데드락 → k_thread_foreach로 pended_on을 읽어 대기 그래프를 만든다 */
#include <zephyr/kernel.h>
K_MUTEX_DEFINE(mSD);
K_MUTEX_DEFINE(mSPI);
static struct { struct k_mutex *m; const char *name; } known[] = { {&mSD, "mSD"}, {&mSPI, "mSPI"} };
static const char *mutex_of(_wait_q_t *wq, struct k_mutex **out) {   /* pended_on → 어떤 뮤텍스? */
    for (size_t i = 0; i < ARRAY_SIZE(known); i++)
        if (wq == &known[i].m->wait_q) { *out = known[i].m; return known[i].name; }
    *out = NULL; return wq ? "(other wait_q)" : "-";
}
static void logger(void *a, void *b, void *c) { ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) { k_mutex_lock(&mSD, K_FOREVER); k_msleep(2);
               k_mutex_lock(&mSPI, K_FOREVER);
               k_mutex_unlock(&mSPI); k_mutex_unlock(&mSD); k_msleep(5); } }
static void imu(void *a, void *b, void *c) { ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) { k_mutex_lock(&mSPI, K_FOREVER); k_msleep(1);
               k_mutex_lock(&mSD, K_FOREVER);
               k_mutex_unlock(&mSD); k_mutex_unlock(&mSPI); k_msleep(3); } }
K_THREAD_DEFINE(t_log, 1024, logger, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(t_imu, 1024, imu, NULL, NULL, NULL, 5, 0, 0);
static void dump_one(const struct k_thread *ct, void *u) { ARG_UNUSED(u);
    struct k_thread *t = (struct k_thread *)ct; char st[32]; struct k_mutex *m;
    const char *mn = mutex_of(t->base.pended_on, &m);
    printk("  %-10s prio=%3d state=%-10s pended_on=%-6s owner=%s\n", k_thread_name_get(t), t->base.prio,
           k_thread_state_str(t, st, sizeof st), mn, m && m->owner ? k_thread_name_get(m->owner) : "-");
}
int main(void) {
    k_thread_name_set(t_log, "logger"); k_thread_name_set(t_imu, "imu");
    k_msleep(100);
    printk("t=%lld ms thread dump:\n", k_uptime_get()); k_thread_foreach(dump_one, NULL);
    /* 대기 그래프 따라가기: 스레드 → 기다리는 뮤텍스 → 그 owner → ... */
    struct k_thread *t = t_log, *start = t_log; struct k_mutex *m;
    printk("walk: %s", k_thread_name_get(t));
    for (int hop = 0; hop < 4; hop++) {
        const char *mn = mutex_of(t->base.pended_on, &m);
        if (!m || !m->owner) { printk(" (no cycle)\n"); return 0; }
        t = m->owner; printk(" -[%s]-> %s", mn, k_thread_name_get(t));
        if (t == start) { printk("  => CYCLE (deadlock)\n"); return 0; }
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/D9/zb.sh /Users/donh/.rtos_scratch/D9/z1 /tmp/rtos_D9/z1 && \
  timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -icount shift=5,align=off,sleep=off -kernel /tmp/rtos_D9/z1/zephyr/zephyr.elf'
```

(`zb.sh`는 `rtos-zbuild mps2/an385`와 같은 일을 하되 Zephyr 캐시를 `-DUSER_CACHE_DIR=/tmp/rtos_D9/zcache`로 돌린 것이다 — 읽기 전용 루트 FS 때문.)

```text
           FLASH:       15948 B         4 MB      0.38%
             RAM:        8704 B         4 MB      0.21%
*** Booting Zephyr OS build v4.4.2 ***
t=110 ms thread dump:
  logger     prio=  5 state=pending    pended_on=mSPI   owner=imu
  imu        prio=  5 state=pending    pended_on=mSD    owner=logger
  idle       prio= 15 state=           pended_on=-      owner=-
  main       prio=  0 state=queued     pended_on=-      owner=-
walk: logger -[mSPI]-> imu -[mSD]-> logger  => CYCLE (deadlock)
qemu-system-arm: terminating on signal 15 from pid 606343 (timeout)
```

출력에서 볼 것: 두 스레드가 `pending`이고, logger(원래 prio 6)가 imu의 prio 5를 **상속**했다(Zephyr는 숫자가 작을수록 높다). 그래프를 따라가면 logger → mSPI → imu → mSD → logger로 원이 닫힌다. 마지막 줄은 `timeout`이 QEMU를 끝낸 것이다 — 데드락난 펌웨어는 스스로 끝나지 않으므로 **항상 `timeout`으로 묶어서 돌린다**.

Zephyr의 thread analyzer(`CONFIG_THREAD_ANALYZER`)는 스택 사용량·CPU 사용률을 찍는 도구라 대기 대상은 보여 주지 않는다. 대기 대상은 위처럼 `pended_on`을 직접 읽거나, 디버거의 RTOS-aware 뷰(M 모듈)로 본다.

---

## 3. 락이 아닌 데드락 — RTOS에서 더 자주 만나는 모양

뮤텍스 두 개의 AB/BA는 교과서 예제다. 실제 RTOS 펌웨어에서 더 자주 보는 것은 **큐·세마포어·타이머 데몬·스케줄러 상태**가 만드는 순환이다. 공통점은 "내가 기다리는 신호를 줄 수 있는 유일한 주체가 지금 나 때문에 못 움직인다"이다.

### 3.1 세 가지 패턴을 한 번에

무엇을 확인하는 코드인가: MODE=1 서로의 큐가 가득 차서 막힘, MODE=2 자기만 줄 수 있는 세마포어를 먼저 기다림, MODE=3 타이머 콜백이 뮤텍스에서 막혀 타이머 데몬 전체가 멈춤.

```c
/* ex3: 락이 아닌 데드락 3종 — MODE=1 큐 순환, 2 자기 자신을 기다림, 3 타이머 데몬 막기 */
#include "common.h"
#include "queue.h"
#include "timers.h"
#ifndef MODE
#define MODE 1
#endif
static QueueHandle_t qA, qB;
static SemaphoreHandle_t sDone, mBus;
static volatile uint32_t prog;
#if MODE == 1   /* 서로 상대 큐에 4개씩 보내고 나서야 자기 큐를 읽는다 (큐 깊이 2) */
static void tA(void *p) { (void)p; uint32_t v = 0;
    for (;;) {
        for (int i = 0; i < 4; i++) xQueueSend(qB, &v, portMAX_DELAY);
        for (int i = 0; i < 4; i++) xQueueReceive(qA, &v, portMAX_DELAY);
        prog++;
    } }
static void tB(void *p) { (void)p; uint32_t v = 0;
    for (;;) {
        for (int i = 0; i < 4; i++) xQueueSend(qA, &v, portMAX_DELAY);
        for (int i = 0; i < 4; i++) xQueueReceive(qB, &v, portMAX_DELAY);
        prog++;
    } }
#elif MODE == 2 /* "완료를 기다린 다음 시작한다" — 순서가 뒤집힌 코드 */
static void tA(void *p) { (void)p;
    for (;;) { xSemaphoreTake(sDone, portMAX_DELAY);   /* DMA 완료를 기다림 */
               xSemaphoreGive(sDone);                   /* (원래는 DMA ISR이 줄 것) */
               prog++; } }
static void tB(void *p) { (void)p; vTaskSuspend(NULL); }
#else           /* 타이머 콜백이 뮤텍스에서 막히면 데몬 전체가 멈춘다 */
static void cbA(TimerHandle_t t) { (void)t; xSemaphoreTake(mBus, portMAX_DELAY); xSemaphoreGive(mBus); }
static void cbB(TimerHandle_t t) { (void)t; xSemaphoreGive(sDone); }
static void tA(void *p) { (void)p;
    xTimerStart(xTimerCreate("tA", pdMS_TO_TICKS(10), pdFALSE, NULL, cbA), 0);
    xTimerStart(xTimerCreate("tB", pdMS_TO_TICKS(20), pdFALSE, NULL, cbB), 0);
    xSemaphoreTake(mBus, portMAX_DELAY);               /* 버스를 잡고 */
    xSemaphoreTake(sDone, portMAX_DELAY);              /* tB 콜백이 신호를 줄 때까지 기다림 */
    prog++; xSemaphoreGive(mBus); vTaskSuspend(NULL); }
static void tB(void *p) { (void)p; vTaskSuspend(NULL); }
#endif
static void monitor(void *p) { (void)p;
    vTaskDelay(pdMS_TO_TICKS(200));
    board_printf_raw("MODE=%d t=%u ms prog=%u\n", MODE, (unsigned)xTaskGetTickCount(), (unsigned)prog);
    dump_tasks();
#if MODE == 1
    board_printf_raw("qA: %u items/%u free, qB: %u items/%u free\n",
        (unsigned)uxQueueMessagesWaiting(qA), (unsigned)uxQueueSpacesAvailable(qA),
        (unsigned)uxQueueMessagesWaiting(qB), (unsigned)uxQueueSpacesAvailable(qB));
#endif
    board_exit(0); }
int main(void) {
    qA = xQueueCreate(2, sizeof(uint32_t)); qB = xQueueCreate(2, sizeof(uint32_t));
    sDone = xSemaphoreCreateBinary(); mBus = xSemaphoreCreateMutex();
    xTaskCreate(tA, "tA", 256, NULL, 2, NULL);
    xTaskCreate(tB, "tB", 256, NULL, 2, NULL);
    xTaskCreate(monitor, "monitor", 512, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for m in 1 2 3; do limactl shell elinux -- bash -lc "/Users/donh/.rtos_scratch/D9/run.sh ex3 -B EXTRA_CFLAGS=-DMODE=$m"; done
```

```text
MODE=1 t=200 ms prog=0
  name      state      prio(cur/base)  stackHWM(words)
  monitor	Running	5/5	480
  IDLE	Ready	0/0	231
  Tmr Svc	Blocked	7/7	481
  tA	Blocked	2/2	220
  tB	Blocked	2/2	220
qA: 2 items/0 free, qB: 2 items/0 free
[board_exit] code=0
[rc=0]
MODE=2 t=200 ms prog=0
  name      state      prio(cur/base)  stackHWM(words)
  monitor	Running	5/5	480
  IDLE	Ready	0/0	230
  Tmr Svc	Blocked	7/7	480
  tA	Blocked	2/2	226
  tB	Suspended	2/2	234
[board_exit] code=0
[rc=0]
MODE=3 t=200 ms prog=0
  name      state      prio(cur/base)  stackHWM(words)
  monitor	Running	5/5	480
  IDLE	Ready	0/0	237
  tB	Suspended	2/2	234
  tA	Blocked	7/2	214
  Tmr Svc	Blocked	7/7	469
[board_exit] code=0
[rc=0]
```

출력에서 볼 것:

- MODE=1: 두 큐 모두 `2 items/0 free`. tA는 꽉 찬 qB에 세 번째를 넣으려고, tB는 꽉 찬 qA에 넣으려고 막혔다. 둘 다 "보내기를 다 끝낸 뒤에 받는다"는 구조라 영원히 받지 않는다. 뮤텍스는 하나도 없다.
- MODE=2: tA 혼자 `Blocked`. 그 세마포어를 줄 코드는 tA 자신 안에, 그것도 take **뒤에** 있다. 덤프만 보면 "누군가 신호를 안 준다"로 보여서 ISR·DMA를 의심하게 되는 유형이다.
- MODE=3: tA의 우선순위가 `7/2` — **타이머 데몬(prio 7)이 tA가 쥔 `mBus`를 기다리면서 상속을 줬다**. 즉 데몬이 콜백 cbA 안에서 막혀 있다. 그래서 20 ms에 떠야 할 cbB는 데몬의 명령 큐에서 영원히 대기하고, cbB가 줄 `sDone`을 기다리는 tA도 영원히 막힌다. 타이머 하나의 실수가 **시스템의 모든 소프트웨어 타이머**를 세운다(D6: 타이머 콜백에서 블록 금지).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">락 없이도 순환은 생긴다</text>
<text x="20" y="45" font-size="12" font-weight="bold">(a) 큐 순환 — 보내기를 다 끝내야 받는다</text>
<rect x="20" y="60" width="90" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="65" y="83" font-size="12" text-anchor="middle">tA</text>
<rect x="210" y="60" width="90" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="255" y="83" font-size="12" text-anchor="middle">tB</text>
<rect x="115" y="120" width="90" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="140" font-size="12" text-anchor="middle">qB 가득</text>
<rect x="115" y="170" width="90" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="190" font-size="12" text-anchor="middle">qA 가득</text>
<line x1="70" y1="98" x2="125" y2="118" stroke="#d0564a" marker-end="url(#ah2)"/>
<line x1="205" y1="128" x2="245" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="250" y1="98" x2="200" y2="168" stroke="#d0564a" marker-end="url(#ah2)"/>
<line x1="120" y1="178" x2="60" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="20" y="225" font-size="12">빨강: 보내려고 기다림 / 검정: 비워 줄 수 있는 유일한 소비자</text>
<line x1="340" y1="35" x2="340" y2="235" stroke="#888" stroke-dasharray="4 3"/>
<text x="355" y="45" font-size="12" font-weight="bold">(b) 타이머 데몬이 콜백 안에서 막힘</text>
<rect x="360" y="60" width="120" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="420" y="83" font-size="12" text-anchor="middle">tA (mBus 소유)</text>
<rect x="540" y="60" width="130" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="605" y="83" font-size="12" text-anchor="middle">Tmr Svc: cbA</text>
<rect x="540" y="150" width="130" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="605" y="173" font-size="12" text-anchor="middle">cbB (대기 중)</text>
<line x1="540" y1="72" x2="484" y2="72" stroke="#d0564a" marker-end="url(#ah2)"/>
<text x="512" y="64" font-size="12" text-anchor="middle">mBus</text>
<line x1="605" y1="98" x2="605" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="612" y="120" font-size="12">데몬 정지</text>
<text x="612" y="136" font-size="12">실행 불가</text>
<line x1="538" y1="170" x2="430" y2="100" stroke="#d0564a" marker-end="url(#ah2)"/>
<text x="360" y="200" font-size="12">tA는 cbB의 sDone을 기다림</text>
<text x="355" y="225" font-size="12">데몬 하나가 서면 모든 SW 타이머가 선다</text>
</svg>
```
그림 5 — (a) 두 큐가 서로의 유일한 소비자를 막는 순환. (b) 타이머 콜백이 뮤텍스를 기다리는 순간 데몬이 막히고, 그 데몬만 실행할 수 있는 다른 콜백을 기다리는 태스크까지 막힌다.

### 3.2 블록하면 안 되는 곳에서 블록 — 하나는 assert, 하나는 조용히 무한 루프

무엇을 확인하는 코드인가: MODE=1 `vTaskSuspendAll()` 상태에서 타임아웃 있는 take, MODE=2 `taskENTER_CRITICAL()` 안에서 타임아웃 있는 take, MODE=3 일반 뮤텍스를 소유자가 다시 take.

```c
/* ex4: 블록하면 안 되는 곳에서 블록 / 비재귀 뮤텍스 자기 데드락 */
#include "common.h"
#ifndef MODE
#define MODE 1
#endif
static SemaphoreHandle_t sem, mtx, rmtx;
static void worker(void *p) { (void)p;
#if MODE == 1                                   /* 스케줄러 정지 중 블로킹 API */
    vTaskSuspendAll();
    board_printf_raw("suspended; taking sem with 10 ticks...\n");
    xSemaphoreTake(sem, 10);
    xTaskResumeAll();
#elif MODE == 2                                 /* 크리티컬 섹션 안에서 블로킹 API */
    uint32_t t0 = board_now_us();
    taskENTER_CRITICAL();
    BaseType_t r = xSemaphoreTake(sem, 10);
    taskEXIT_CRITICAL();
    board_printf_raw("in critical: take=%d after %u us, tick=%u\n", (int)r,
                     (unsigned)(board_now_us() - t0), (unsigned)xTaskGetTickCount());
#else                                           /* 같은 뮤텍스를 두 번 */
    xSemaphoreTakeRecursive(rmtx, 0);
    BaseType_t r2 = xSemaphoreTakeRecursive(rmtx, 0);
    board_printf_raw("recursive mutex 2nd take = %d\n", (int)r2);
    xSemaphoreGiveRecursive(rmtx); xSemaphoreGiveRecursive(rmtx);
    xSemaphoreTake(mtx, 0);
    TickType_t t0 = xTaskGetTickCount();
    BaseType_t r = xSemaphoreTake(mtx, pdMS_TO_TICKS(50));   /* 내가 쥔 것을 또 */
    board_printf_raw("normal mutex 2nd take = %d after %u ticks, holder=%s\n", (int)r,
                     (unsigned)(xTaskGetTickCount() - t0), pcTaskGetName(xSemaphoreGetMutexHolder(mtx)));
#endif
    board_exit(0);
}
int main(void) {
    sem = xSemaphoreCreateBinary(); mtx = xSemaphoreCreateMutex(); rmtx = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(worker, "worker", 512, NULL, 2, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for m in 1 2 3; do limactl shell elinux -- bash -lc "TO=5 /Users/donh/.rtos_scratch/D9/run.sh ex4 -B EXTRA_CFLAGS='-DMODE=$m -DINCLUDE_xSemaphoreGetMutexHolder=1'"; done
```

```text
suspended; taking sem with 10 ticks...

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/queue.c:1682
[board_exit] code=253
[rc=1]
qemu-system-arm: terminating on signal 15 from pid 160827 (timeout)
[rc=124]
recursive mutex 2nd take = 1
normal mutex 2nd take = 0 after 50 ticks, holder=worker
[board_exit] code=0
[rc=0]
```

출력에서 볼 것:

- MODE=1: `queue.c:1682`의 assert가 바로 잡는다. 그 줄은 `xQueueSemaphoreTake()` 첫머리의 `configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );`다. 스케줄러를 멈춘 채 블록하면 아무도 나를 다시 스케줄할 수 없으니 막아 둔 것이다. **`configASSERT`를 정의해 두면** 이 실수는 첫 실행에 잡힌다.
- MODE=2: 아무 출력 없이 5초 timeout. assert도 없다. gdb로 2초 뒤에 멈춰 보면 이렇다.

```text
0x0000130c in vListInsert (pxList=pxList@entry=0x20001034 <ucHeap+48>, pxNewListItem=0x20001950 <ucHeap+2380>) at /var/rtoslab/src/FreeRTOS-Kernel/list.c:192
192	        for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext )
#1  0x00000dfc in vTaskPlaceOnEventList (pxEventList=pxEventList@entry=0x20001034 <ucHeap+48>, xTicksToWait=10) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5423
#2  0x00001a3a in xQueueSemaphoreTake (xQueue=0x20001010 <ucHeap+12>, xTicksToWait=<optimized out>) at /var/rtoslab/src/FreeRTOS-Kernel/queue.c:1806
...
uxCriticalNesting=1 xTickCount=0 basepri=$1 = 0x20
$2 = 0x1400e800
```

(`bt`·`p uxCriticalNesting`·`p/x $basepri`·`p/x ICSR(0xE000ED04)`를 찍은 결과에서 핵심 줄만 남겼다.) 크리티컬 섹션 안이라 BASEPRI=0x20으로 PendSV와 SysTick이 모두 막혀 있다. ICSR=`0x1400e800`은 PENDSVSET(bit 28)과 PENDSTSET(bit 26)이 **둘 다 걸린 채 대기 중**이라는 뜻이다. 태스크는 문맥 전환이 안 되니 계속 달리면서 `xQueueSemaphoreTake`의 재시도 루프를 돌고, 이미 이벤트 리스트에 들어 있는 자기 항목을 **같은 리스트에 또 넣다가** 리스트가 자기 자신을 가리키게 되어 `vListInsert`의 for 루프에서 영원히 돈다. tick이 0에서 멈췄으니 타임아웃도 영원히 안 온다. `list.c`의 그 for 루프 바로 위 주석이 "여기서 멈추면 원인은 … 3) 크리티컬 섹션 안이나 스케줄러 정지 중 API 호출, ISR에서 FromISR 아닌 API 호출"이라고 적어 두었다 — **"vListInsert에서 멈춤"은 FreeRTOS 현장의 유명한 증상**이다.
- MODE=3: FreeRTOS **일반 뮤텍스는 재귀가 아니다**. 소유자가 다시 take하면 자기 자신을 기다리며 블록하고, 50 tick 뒤 타임아웃(0)으로 돌아온다. `portMAX_DELAY`였다면 영원히 멈춘다. assert도 없다. 재귀 뮤텍스(`xSemaphoreCreateRecursiveMutex` + `...TakeRecursive`)는 두 번째 take가 성공(1)한다.

### 3.3 Zephyr `k_mutex`는 재귀적이다

무엇을 확인하는 코드인가: 같은 스레드의 두 번째 `k_mutex_lock`, 남의 뮤텍스 unlock, 혼자만 줄 수 있는 세마포어.

```c
/* z2: Zephyr k_mutex는 재귀적이다 — FreeRTOS 일반 뮤텍스(ex4 MODE 3)와 비교 */
#include <zephyr/kernel.h>
K_MUTEX_DEFINE(m);
K_SEM_DEFINE(sDone, 0, 1);
static void other(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    k_msleep(10);
    printk("other: unlock not-owned mutex -> %d (EPERM=%d)\n", k_mutex_unlock(&m), EPERM);
}
K_THREAD_DEFINE(t_other, 1024, other, NULL, NULL, NULL, 5, 0, 0);
int main(void) {
    printk("lock #1 -> %d\n", k_mutex_lock(&m, K_NO_WAIT));
    printk("lock #2 (same thread) -> %d, lock_count=%u owner=%s\n", k_mutex_lock(&m, K_NO_WAIT),
           m.lock_count, m.owner == k_current_get() ? "main" : "?");
    k_msleep(20);                                  /* other가 남의 뮤텍스를 풀어 보게 한다 */
    k_mutex_unlock(&m);
    printk("after 1 unlock: lock_count=%u owner=%p\n", m.lock_count, (void *)m.owner);
    k_mutex_unlock(&m);
    printk("after 2 unlocks: lock_count=%u owner=%p\n", m.lock_count, (void *)m.owner);
    int64_t t0 = k_uptime_get();
    int r = k_sem_take(&sDone, K_MSEC(50));       /* 나 말고는 줄 사람이 없는 세마포어 */
    printk("k_sem_take(self-only sem) -> %d (EAGAIN=%d) after %lld ms\n", r, EAGAIN, k_uptime_get() - t0);
    return 0;
}
```

```text
*** Booting Zephyr OS build v4.4.2 ***
lock #1 -> 0
lock #2 (same thread) -> 0, lock_count=2 owner=main
other: unlock not-owned mutex -> -1 (EPERM=1)
after 1 unlock: lock_count=1 owner=0x20000228
after 2 unlocks: lock_count=0 owner=0
k_sem_take(self-only sem) -> -11 (EAGAIN=11) after 60 ms
```

출력에서 볼 것: Zephyr는 같은 스레드의 재잠금을 성공시키고 `lock_count`를 올린다(`kernel/mutex.c`: `if (likely((mutex->lock_count == 0U) || (mutex->owner == _current)))`). 소유자가 아닌 스레드의 unlock은 `-EPERM`. 50 ms 타임아웃이 60 ms로 나온 것은 Zephyr mps2 보드의 tick이 100 Hz(10 ms)라 올림 + 1 tick이 붙었기 때문이다.

FreeRTOS 쪽에서 "남이 풀기"도 확인해 보자.

```c
/* ex4c: 소유자가 아닌 태스크가 뮤텍스를 give하면? (일반 / 재귀) */
#include "common.h"
static SemaphoreHandle_t mtx, rmtx;
static void owner(void *p) { (void)p;
    xSemaphoreTake(mtx, 0); xSemaphoreTakeRecursive(rmtx, 0);
    vTaskSuspend(NULL); }
static void thief(void *p) { (void)p;
    vTaskDelay(5);
    board_printf_raw("GiveRecursive by non-owner -> %d\n", (int)xSemaphoreGiveRecursive(rmtx));
    board_printf_raw("Give(normal) by non-owner...\n");
    BaseType_t r = xSemaphoreGive(mtx);
    board_printf_raw(" -> %d\n", (int)r);
    board_exit(0); }
int main(void) {
    mtx = xSemaphoreCreateMutex(); rmtx = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(owner, "owner", 256, NULL, 3, NULL);
    xTaskCreate(thief, "thief", 256, NULL, 2, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```text
GiveRecursive by non-owner -> 0
Give(normal) by non-owner...

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:6861
[board_exit] code=253
[rc=1]
```

출력에서 볼 것: 재귀 뮤텍스는 `xQueueGiveMutexRecursive()`가 소유자를 확인하고 조용히 실패(0)한다. 일반 뮤텍스는 give 경로가 `xTaskPriorityDisinherit()`로 들어가 `configASSERT( pxTCB == pxCurrentTCB )`(tasks.c:6861)에 걸린다. `configASSERT`를 비워 둔 양산 빌드에서는 이 검사가 사라진다는 점을 기억하자.

| 동작 | FreeRTOS 일반 뮤텍스 | FreeRTOS 재귀 뮤텍스 | Zephyr `k_mutex` |
|---|---|---|---|
| 소유자의 두 번째 take | 자기 자신을 기다림 (자기 데드락) | 성공, 카운트 +1 | 성공, `lock_count` +1 |
| 남의 뮤텍스 give/unlock | `configASSERT` (tasks.c:6861) | `pdFALSE`(0) 반환 | `-EPERM` |
| 감지 장치 | 없음 (타임아웃만) | — | — |
| 이식할 때 주의 | Zephyr → FreeRTOS로 옮기면 재귀 경로가 데드락이 된다 | 놓는 횟수를 맞춰야 한다 | 재귀가 "숨은 중첩 잠금"을 감춘다 |

함정: Zephyr 코드에서 무심코 중첩 잠금을 쓰다가 FreeRTOS로 옮기면 그 경로가 자기 데드락이 된다. 반대로 재귀가 허용되면 "내가 이미 이 락을 쥔 채 콜백을 불렀다"는 설계 냄새가 가려진다. 순위 검사기(§4.2)는 같은 락 재진입도 위반으로 잡게 짜는 것이 좋다.

---

## 4. 예방 — 순환을 처음부터 못 만들게

### 4.1 락 순서 규칙 — 전역 순위표

가장 싸고 강력한 예방은 Coffman 4번(순환 대기)을 깨는 것이다. 규칙은 한 줄이다.

> **모든 락에 정수 순위(rank)를 매기고, 어떤 태스크든 이미 쥔 락보다 순위가 큰 락만 새로 잡을 수 있다.**

순위가 항상 증가하는 방향으로만 잡으면 대기 그래프의 화살표도 항상 "작은 순위 → 큰 순위" 방향이라 원이 닫힐 수 없다. 원이 닫히려면 어딘가에서 큰 순위를 쥔 채 작은 순위를 기다려야 하기 때문이다.

순위를 어떻게 매기나? 경험칙은 **"바깥(상위 계층) 자원이 작은 순위, 안쪽(하위 드라이버·버스) 자원이 큰 순위"**다. 로그 버퍼를 쥐고 → 파일시스템을 쥐고 → SD 드라이버를 쥐고 → SPI 버스를 쥔다. 위에서 아래로 내려가는 호출 방향과 같게 매기면 자연스럽게 지켜진다. 예를 들어 드론 비행 제어기라면 이런 표를 `locks.h` 한 곳에 둔다.

| 순위 | 락 | 보호 대상 | 이 락을 쥔 채 잡아도 되는 것 |
|---|---|---|---|
| 10 | `lkParam` | 파라미터 저장소(RAM 사본) | 20 이상 전부 |
| 20 | `lkLog` | 로그 링버퍼 · SD 파일 핸들 | 30 이상 |
| 30 | `lkFs` | FAT 파일시스템 상태 | 40 이상 |
| 40 | `lkSpi1` | SPI1 버스 (SD 카드 + IMU) | 50 이상 |
| 50 | `lkI2c1` | I2C1 버스 (기압계 · 자력계) | 없음 (잎) |
| — | 크리티컬 섹션 | 수 µs짜리 공유 변수 | 어떤 락도 잡지 않는다 |

함정은 §2의 imu처럼 **"드문 경로"가 순서를 뒤집는 것**이다. 정상 경로는 다 지키는데 에러 처리·로그·통계 경로가 하위 락을 쥔 채 상위 락을 부른다. 코드 리뷰로는 잘 안 보이므로 다음 절처럼 런타임에 검사한다.

### 4.2 런타임 락 순서 검사기 — FreeRTOS용 작은 lockdep

Linux의 lockdep은 모든 락 클래스의 의존 그래프를 유지하는 큰 장치다(EL F4 §13). MCU에서는 그 축소판으로 충분하다. 태스크마다 **지금 쥔 락의 스택**을 두고, 새 락을 잡을 때 스택 맨 위 락의 순위보다 큰지 확인한다. 태스크별 저장 공간은 FreeRTOS의 TLS(thread local storage) 포인터(`vTaskSetThreadLocalStoragePointer`, `configNUM_THREAD_LOCAL_STORAGE_POINTERS`)에 건다. 같은 구조체에 "지금 기다리는 락"과 "언제부터"도 기록해 두면 §5의 탐지기들이 그대로 쓴다.

```c
/* lockdbg.h — FreeRTOS용 작은 락 디버그 계층: 순위(rank) 검사 + 대기 정보 기록 + 타임아웃 카운터 */
#ifndef LOCKDBG_H
#define LOCKDBG_H
#include "common.h"
#define MAX_HELD 4
typedef struct { SemaphoreHandle_t h; uint8_t rank; const char *name; uint32_t timeouts; } lock_t;
typedef struct {                       /* 태스크마다 하나 — TLS 슬롯 0에 건다 */
    lock_t *held[MAX_HELD]; uint8_t depth;
    lock_t *volatile waiting;          /* 지금 기다리는 락 (대기 그래프의 간선) */
    TickType_t waitSince;
} lockctx_t;
static inline lockctx_t *lk_ctx(void) {
    return (lockctx_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);
}
static inline void lk_attach(lockctx_t *c) { vTaskSetThreadLocalStoragePointer(NULL, 0, c); }
static inline void lk_init(lock_t *l, uint8_t rank, const char *name) {
    l->h = xSemaphoreCreateMutex(); l->rank = rank; l->name = name; l->timeouts = 0;
}
static inline BaseType_t lk_take(lock_t *l, TickType_t to) {
    lockctx_t *c = lk_ctx();
#ifdef LOCK_ORDER_CHECK
    if (c->depth && c->held[c->depth - 1]->rank >= l->rank) {
        board_printf_raw("[lock-order] %s: holds %s(rank %u) then wants %s(rank %u) at t=%u\n",
            pcTaskGetName(NULL), c->held[c->depth - 1]->name, c->held[c->depth - 1]->rank,
            l->name, l->rank, (unsigned)xTaskGetTickCount());
        configASSERT(0);
    }
#endif
    c->waitSince = xTaskGetTickCount();
    c->waiting = l;
    BaseType_t r = xSemaphoreTake(l->h, to);
    c->waiting = NULL;
    if (r == pdTRUE) { configASSERT(c->depth < MAX_HELD); c->held[c->depth++] = l; }
    else l->timeouts++;
    return r;
}
static inline void lk_give(lock_t *l) {
    lockctx_t *c = lk_ctx();
    configASSERT(c->depth && c->held[c->depth - 1] == l);   /* LIFO 해제 강제 */
    c->depth--;
    xSemaphoreGive(l->h);
}
#endif
```

`rank >= l->rank`로 비교하므로 **같은 락을 다시 잡는 것(자기 데드락)도 위반**으로 잡는다. `lk_give`는 LIFO 해제를 강제한다(순서 뒤집힌 해제는 그 자체로 데드락은 아니지만, 스택이 꼬이면 검사기가 틀린 판단을 하므로).

무엇을 확인하는 코드인가: §2의 logger/imu 코드를 래퍼로 바꿔서, 순서 위반이 **데드락이 일어나기 전에** 잡히는지. `FIXED`를 정의하면 imu가 SPI를 놓은 뒤에 SD를 잡는다.

```c
/* ex5: 런타임 락 순서 검사기 — 데드락이 "일어나기 전에" 잘못된 순서를 잡는다 */
#include "lockdbg.h"
static lock_t lkSD, lkSPI;                    /* 순위표: SD=10 < SPI=20 (작은 것부터 잡는다) */
static lockctx_t cLog, cImu;
static volatile uint32_t logCnt, imuCnt;
static void busy_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) {} }
static uint32_t lfsr = 0xACE1u;
static uint32_t rnd(void) { lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u); return lfsr; }
static void logger(void *p) { (void)p; lk_attach(&cLog);
    for (;;) {
        lk_take(&lkSD, portMAX_DELAY);
        busy_us(100 + rnd() % 900);
        lk_take(&lkSPI, portMAX_DELAY);
        logCnt++;
        lk_give(&lkSPI); lk_give(&lkSD);
        vTaskDelay(pdMS_TO_TICKS(5));
    } }
static void imu(void *p) { (void)p; lk_attach(&cImu);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(2));
        lk_take(&lkSPI, portMAX_DELAY);
        busy_us(50);
        uint32_t n = ++imuCnt;
#ifndef FIXED
        if (n % 16 == 0) { lk_take(&lkSD, portMAX_DELAY); lk_give(&lkSD); }  /* SPI → SD: 위반 */
        lk_give(&lkSPI);
#else
        lk_give(&lkSPI);                       /* 고친 버전: SPI를 놓은 뒤에 SD */
        if (n % 16 == 0) { lk_take(&lkSD, portMAX_DELAY); lk_give(&lkSD); }
#endif
    } }
static void monitor(void *p) { (void)p;
    vTaskDelay(pdMS_TO_TICKS(60000));
    board_printf_raw("60 s OK: logCnt=%u imuCnt=%u\n", (unsigned)logCnt, (unsigned)imuCnt);
    board_exit(0); }
int main(void) {
    lk_init(&lkSD, 10, "SD"); lk_init(&lkSPI, 20, "SPI");
    xTaskCreate(logger, "logger", 256, NULL, 2, NULL);
    xTaskCreate(imu, "imu", 256, NULL, 3, NULL);
    xTaskCreate(monitor, "monitor", 256, NULL, 4, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
F='-DconfigNUM_THREAD_LOCAL_STORAGE_POINTERS=1 -DINCLUDE_xSemaphoreGetMutexHolder=1'
TO=60  run.sh ex5 -B EXTRA_CFLAGS="$F -DLOCK_ORDER_CHECK"
TO=120 run.sh ex5 -B EXTRA_CFLAGS="$F -DLOCK_ORDER_CHECK -DFIXED"
```

```text
[lock-order] imu: holds SPI(rank 20) then wants SD(rank 10) at t=32

[configASSERT] /Users/donh/.rtos_scratch/D9/lockdbg.h:26
[board_exit] code=253
[rc=1]
60 s OK: logCnt=11837 imuCnt=29999
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: 같은 시드(0xACE1)에서 데드락은 t=1000 ms에 났는데(§2.1), 검사기는 **imu가 처음 이상치 경로를 탄 t=32 ms**(16번째 샘플 = 2 ms × 16)에 잡았다. 이번 실행에서 logger가 SD를 쥐고 있었는지는 상관없다 — 순서 위반은 **실행되기만 하면** 잡힌다. 이것이 lockdep 계열 검사기의 핵심 가치다. 데드락은 확률 사건이지만 순서 위반은 결정적 사건이다. 고친 버전은 60초(가상) 동안 logger 11,837회, imu 29,999회를 문제없이 돌았다.

검사 비용은 얼마나 될까.

```c
/* ex5b: 락 래퍼 비용 — 경합 없는 take/give 1000쌍, FPGAIO 25 MHz 카운터로 */
#include "lockdbg.h"
#define CYC() REG32(0x40028018u)
static lock_t lk; static lockctx_t c;
static void t(void *p) { (void)p; lk_attach(&c);
    uint32_t a = CYC();
    for (int i = 0; i < 1000; i++) { xSemaphoreTake(lk.h, portMAX_DELAY); xSemaphoreGive(lk.h); }
    uint32_t b = CYC();
    for (int i = 0; i < 1000; i++) { lk_take(&lk, portMAX_DELAY); lk_give(&lk); }
    uint32_t d = CYC();
    board_printf_raw("raw take+give: %u cycles/pair, lk_take+lk_give: %u cycles/pair\n",
                     (unsigned)((b - a) / 1000), (unsigned)((d - b) / 1000));
    board_exit(0); }
int main(void) { lk_init(&lk, 10, "L");
    xTaskCreate(t, "t", 256, NULL, 2, NULL); vTaskStartScheduler(); for (;;) {} }
```

```text
raw take+give: 151 cycles/pair, lk_take+lk_give: 192 cycles/pair
[board_exit] code=0
[rc=0]
raw take+give: 151 cycles/pair, lk_take+lk_give: 190 cycles/pair
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: 위가 `LOCK_ORDER_CHECK` 켠 빌드, 아래가 끈 빌드다(여기서 cycles는 FPGAIO 25 MHz 카운터 눈금, QEMU icount 기준). 경합 없는 take+give 한 쌍 151 → 190 눈금, 약 +26%다. 대부분은 순위 비교가 아니라 TLS 조회와 대기 정보 기록이다(검사 on/off 차이는 2눈금). 경합 없는 뮤텍스 한 쌍이 수 µs라서, 수백 Hz로 잡는 락이라면 양산에도 켜 둘 만한 비용이다. 20 kHz 전류 루프 같은 경로에는 애초에 뮤텍스를 쓰지 않는다(E2).

### 4.3 try-lock과 백오프 — 쥔 채 기다리지 않기

Coffman 2번(쥔 채 기다림)을 깨는 방법은 "두 번째 락은 기다리지 않고 시도만 하고, 실패하면 **쥔 것을 다 놓고** 물러났다가 처음부터 다시"다. 순서를 강제할 수 없는 경우(예: 콜백 구조 때문에 어느 락이 먼저 잡힐지 모르는 코드)에 쓴다. 대가는 두 가지다.

- 물러날 때 **이미 한 일을 되돌릴 수 있어야** 한다(부분 전송 취소, 버퍼 반납).
- 백오프가 대칭이면 **라이브락**이 된다. §7.1에서 같은 코드가 1초 동안 일을 0건 하는 것을 측정한다.

### 4.4 단일 소유자 설계 — 게이트키퍼

Coffman 1번(상호 배제)을 아예 없애는 설계도 있다. SPI 버스를 여러 태스크가 뮤텍스로 나눠 쓰는 대신, **SPI 버스는 `spi_task` 하나만 만지고** 다른 태스크는 요청을 큐로 보낸 뒤 응답을 기다린다. 이 태스크를 게이트키퍼(gatekeeper)라고 부른다. 버스 락이 없으니 버스 락이 끼는 순환도 없다. 단, 요청자가 다른 락을 쥔 채 응답을 기다리면 그것이 다시 hold-and-wait가 된다는 점, 게이트키퍼가 다른 게이트키퍼에 동기 요청을 보내면 §3.1(a)의 큐 순환이 된다는 점은 남는다. 구현과 비용은 D7에서 다룬다.

### 4.5 PCP / ICPP — 우선순위로 순환을 막기

Immediate Ceiling Priority Protocol(ICPP)은 락을 잡는 순간 그 태스크의 우선순위를 "그 락을 쓸 수 있는 모든 태스크 중 최고 우선순위(ceiling)"로 올린다. 단일 코어에서는 락을 쥔 태스크를 그 락을 원할 수 있는 어떤 태스크도 선점할 수 없으므로, 그 락 집합 안에서는 순환 대기가 생길 수 없다. 즉 ICPP는 우선순위 역전 대책이면서 **데드락 예방 장치**다. FreeRTOS·Zephyr 뮤텍스는 기본이 상속(PIP)이고 PIP는 데드락을 막지 못한다 — §2에서 상속이 일어난 채 멈춘 것을 봤다. 증명과 구현(수동 `vTaskPrioritySet` 방식 포함)은 D3이 주인이다.

### 4.6 예방 수단 비교

| 수단 | 깨는 조건 | 비용 | 언제 쓰나 |
|---|---|---|---|
| 락 순위표 + 검사기 | 순환 대기 | take당 수십 눈금, 규칙 관리 | 기본값. 락이 2개 이상이면 항상 |
| 한 번에 다 잡기 | 쥔 채 기다림 | 필요 없는 락까지 오래 잡음 | 잡을 락 집합을 미리 알 때 |
| try-lock + 전부 놓기 | 쥔 채 기다림 | 롤백 코드, 라이브락 위험 | 순서를 강제할 수 없는 콜백 구조 |
| 유한 타임아웃 + 포기 | 뺏을 수 없음 (스스로 놓음) | 에러 경로, 오탐 | 모든 take에 (탐지 겸용, §5.1) |
| 게이트키퍼 | 상호 배제 | 문맥 전환 2회/요청, 태스크 RAM | 공유 버스·주변장치 |
| ICPP | 순환 대기 | ceiling 계산, 불필요한 차단 | 안전 인증 코드, 정적 분석 가능한 시스템 |
| 락 대신 lock-free 큐 | 상호 배제 | SPSC로 제한 | ISR ↔ 태스크 데이터 (D8) |

---

## 5. 탐지와 복구 — 그래도 멈췄을 때

예방은 규칙이고, 규칙은 언젠가 깨진다. 양산 펌웨어에는 **멈췄다는 사실을 알아채는 장치**가 겹겹이 있어야 한다. 바깥에서 안쪽 순서로: 하드웨어 watchdog(시스템 전체가 멈췄나) → 태스크별 heartbeat(특정 태스크가 멈췄나) → 대기 그래프 모니터(왜 멈췄나) → 모든 take의 타임아웃(어디서 멈추려 하나).

### 5.1 모든 take에 유한 타임아웃 + 카운터

`portMAX_DELAY`는 "영원히 기다려도 괜찮다"는 선언이다. 데드락이 나면 그 선언대로 영원히 기다린다. 반대로 **모든 take에 그 자원의 최악 보유 시간보다 넉넉한 유한 타임아웃**을 주면, 데드락은 "멈춤" 대신 "타임아웃 카운터 증가"로 드러난다. 타임아웃이 나면 쥔 것을 놓고(= 뺏을 수 없음 조건을 스스로 깸) 물러난 뒤 다시 시도한다.

무엇을 확인하는 코드인가: §5.2의 3-태스크 순환(imu: SPI→I2C, baro: I2C→SD, logger: SD→SPI)을 20 ms 타임아웃 + 태스크마다 다른 백오프로 돌리면 시스템이 계속 진행하는지.

```c
/* ex7: 모든 take에 유한 타임아웃 + 카운터 — 데드락이 "멈춤" 대신 "숫자"로 드러난다 */
#include "lockdbg.h"
#define NT 3
static lock_t lk[NT];
static lockctx_t ctx[NT];
static volatile uint32_t done[NT], backoffs[NT];
static void busy_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) {} }
static void worker(void *p) {
    int i = (int)(uintptr_t)p; lk_attach(&ctx[i]);
    for (;;) {
        if (lk_take(&lk[i], pdMS_TO_TICKS(20)) != pdTRUE) continue;
        busy_us(1200 + 300 * i);
        if (lk_take(&lk[(i + 1) % NT], pdMS_TO_TICKS(20)) != pdTRUE) {
            lk_give(&lk[i]);                     /* 쥔 것을 내려놓고 */
            backoffs[i]++;
            vTaskDelay(pdMS_TO_TICKS(1 + 2 * i)); /* 태스크마다 다른 길이로 물러난다 */
            continue;
        }
        done[i]++;
        lk_give(&lk[(i + 1) % NT]); lk_give(&lk[i]);
        vTaskDelay(pdMS_TO_TICKS(3 + 2 * i));
    } }
static void report(void *p) { (void)p;
    for (int s = 1; s <= 4; s++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        board_printf_raw("t=%u ms done=%u/%u/%u backoffs=%u/%u/%u timeouts(SPI,I2C,SD)=%u/%u/%u\n",
            (unsigned)xTaskGetTickCount(), (unsigned)done[0], (unsigned)done[1], (unsigned)done[2],
            (unsigned)backoffs[0], (unsigned)backoffs[1], (unsigned)backoffs[2],
            (unsigned)lk[0].timeouts, (unsigned)lk[1].timeouts, (unsigned)lk[2].timeouts);
    }
    board_exit(0); }
int main(void) {
    static const char *nm[NT] = {"SPI", "I2C", "SD"};
    static const char *tn[NT] = {"imu", "baro", "logger"};
    for (int i = 0; i < NT; i++) lk_init(&lk[i], (uint8_t)(10 * (i + 1)), nm[i]);
    for (int i = 0; i < NT; i++) xTaskCreate(worker, tn[i], 256, (void *)(uintptr_t)i, 2, NULL);
    xTaskCreate(report, "report", 256, NULL, 6, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```text
t=500 ms done=55/29/22 backoffs=1/7/6 timeouts(SPI,I2C,SD)=6/1/7
t=1000 ms done=109/56/42 backoffs=1/13/13 timeouts(SPI,I2C,SD)=13/1/13
t=1500 ms done=163/83/62 backoffs=1/20/20 timeouts(SPI,I2C,SD)=20/1/20
t=2000 ms done=217/110/83 backoffs=1/27/26 timeouts(SPI,I2C,SD)=26/1/27
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: 같은 구조가 `portMAX_DELAY`였다면 t=3 ms에 영원히 멈춘다(§5.2). 타임아웃 버전은 2초 동안 세 태스크가 217/110/83회 일을 끝냈다. 대신 **500 ms마다 6~7번씩 타임아웃이 꾸준히 난다** — 순서 버그가 사라진 게 아니라 숫자로 바뀐 것이다. 이 카운터를 텔레메트리로 내보내면 "비행 중 SD 락 타임아웃 분당 800회" 같은 형태로 필드에서 보인다. 타임아웃은 탐지 장치이지 해결책이 아니다.

타임아웃 값 고르기:

- 그 락의 **최악 보유 시간(WCET로 측정) × 2~10**. 너무 짧으면 정상 경합을 데드락으로 오판하고, 너무 길면 제어 주기를 놓친다.
- 제어 루프 안의 take라면 **그 주기보다 짧게**. 예: 1 kHz rate loop가 쓰는 락에 20 ms 타임아웃은 이미 20주기를 잃은 뒤다.
- 타임아웃 = 에러다. 반드시 카운트하고, 로그를 남기고, 필요하면 안전 상태로 간다. 조용히 재시도만 하면 라이브락(§7)을 숨긴다.

### 5.2 대기 그래프 모니터 — 누가 누구를 기다리나를 런타임에

§1.2의 대기 그래프를 실제로 그려 보자. 간선 두 종류의 정보가 필요하다.

- **태스크 → 락 (기다림)**: `lk_take`가 블록 직전에 `ctx->waiting`에 기록한다. (커널은 이 정보를 `xEventListItem`의 컨테이너로 갖고 있지만 공개 API가 없다 — §2.3.)
- **락 → 태스크 (소유)**: 커널이 `xMutexHolder`에 갖고 있고, `xSemaphoreGetMutexHolder()`로 읽는다(`INCLUDE_xSemaphoreGetMutexHolder=1`).

모니터 태스크가 주기적으로 한 태스크에서 출발해 "기다리는 락 → 그 소유자 → 그 소유자가 기다리는 락 → …"을 따라가다 출발점으로 돌아오면 순환이다. 소스 주석(`queue.c` `xQueueGetMutexHolder`)이 경고하듯 소유자 읽기는 **순간값**이라 읽는 사이에 바뀔 수 있으므로, 두 번 연속 같은 순환이 보일 때만 확정한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">대기 그래프: 태스크(둥근 상자) → 기다리는 락(네모) → 소유자</text>
<rect x="270" y="40" width="140" height="40" rx="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="65" font-size="13" text-anchor="middle">imu</text>
<rect x="480" y="120" width="110" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="535" y="143" font-size="13" text-anchor="middle">I2C (30)</text>
<rect x="430" y="210" width="140" height="40" rx="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="500" y="235" font-size="13" text-anchor="middle">baro</text>
<rect x="285" y="250" width="110" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="273" font-size="13" text-anchor="middle">SD (10)</text>
<rect x="110" y="210" width="140" height="40" rx="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="180" y="235" font-size="13" text-anchor="middle">logger</text>
<rect x="90" y="120" width="110" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="145" y="143" font-size="13" text-anchor="middle">SPI (20)</text>
<line x1="405" y1="80" x2="490" y2="118" stroke="#d0564a" marker-end="url(#ah3)"/>
<text x="460" y="92" font-size="12">기다림</text>
<line x1="530" y1="158" x2="510" y2="207" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="530" y="190" font-size="12">소유</text>
<line x1="440" y1="245" x2="398" y2="262" stroke="#d0564a" marker-end="url(#ah3)"/>
<line x1="285" y1="262" x2="250" y2="245" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="160" y1="208" x2="148" y2="160" stroke="#d0564a" marker-end="url(#ah3)"/>
<line x1="180" y1="118" x2="272" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="200" y="88" font-size="12">소유</text>
<text x="10" y="45" font-size="12">빨강 = 기다림</text>
<text x="10" y="63" font-size="12">검정 = 소유</text>
<text x="600" y="60" font-size="12" text-anchor="middle">순환 길이 3</text>
</svg>
```
그림 6 — ex6의 세 태스크. 각자 자기 첫 락을 쥔 채 다음 락을 기다린다. 화살표를 따라가면 imu → I2C → baro → SD → logger → SPI → imu로 원이 닫힌다. 순위표로 보면 baro(I2C 30 → SD 10)와 logger의 순서는 이미 규칙 위반이다.

무엇을 확인하는 코드인가: 세 태스크 순환 데드락을 모니터가 찾아서 경로와 대기 시작 시각을 찍는지.

```c
/* ex6: 대기 그래프(wait-for graph) 모니터 — 뮤텍스 소유자 + 대기 기록으로 순환을 찾는다 */
#include "lockdbg.h"
#define NT 3
static lock_t lk[NT];                          /* SPI, I2C, SD */
static lockctx_t ctx[NT];
static TaskHandle_t th[NT];
static void busy_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) {} }
static void worker(void *p) {                  /* 태스크 i: lk[i] → lk[(i+1)%NT] */
    int i = (int)(uintptr_t)p; lk_attach(&ctx[i]);
    for (uint32_t n = 0;; n++) {
        lk_take(&lk[i], portMAX_DELAY);
        busy_us(1200 + 300 * i);          /* 1 tick보다 길다 → time slice로 섞인다 */
        lk_take(&lk[(i + 1) % NT], portMAX_DELAY);
        lk_give(&lk[(i + 1) % NT]); lk_give(&lk[i]);
        vTaskDelay(pdMS_TO_TICKS(3 + 2 * i));
    } }
static int task_index(TaskHandle_t h) { for (int i = 0; i < NT; i++) if (th[i] == h) return i; return -1; }
static int find_cycle(int start, int path[NT + 1]) {   /* start에서 간선을 따라가 start로 돌아오면 길이 반환 */
    int cur = start, len = 0;
    while (len <= NT) {
        lock_t *w = ctx[cur].waiting;
        if (!w) return 0;                      /* 기다리지 않음 → 순환 없음 */
        int nxt = task_index(xSemaphoreGetMutexHolder(w->h));
        if (nxt < 0) return 0;                 /* 소유자 없음/모르는 태스크 */
        path[len++] = cur; cur = nxt;
        if (cur == start) return len;
    }
    return 0;
}
static void wfg_monitor(void *p) { (void)p;
    int seen = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        int path[NT + 1], len = find_cycle(0, path);
        if (!len) { seen = 0; continue; }
        if (++seen < 2) continue;              /* 두 번 연속 같아야 확정 (소유자 읽기는 순간값) */
        board_printf_raw("DEADLOCK cycle at t=%u ms:", (unsigned)xTaskGetTickCount());
        for (int k = 0; k < len; k++) {
            lockctx_t *c = &ctx[path[k]];
            board_printf_raw(" %s -[%s since %u]->", pcTaskGetName(th[path[k]]), c->waiting->name,
                             (unsigned)c->waitSince);
        }
        board_printf_raw(" %s\n", pcTaskGetName(th[path[0]]));
        board_exit(0);
    } }
int main(void) {
    static const char *nm[NT] = {"SPI", "I2C", "SD"};
    static const char *tn[NT] = {"imu", "baro", "logger"};
    for (int i = 0; i < NT; i++) lk_init(&lk[i], (uint8_t)(10 * (i + 1)), nm[i]);
    for (int i = 0; i < NT; i++) xTaskCreate(worker, tn[i], 256, (void *)(uintptr_t)i, 2, &th[i]);
    xTaskCreate(wfg_monitor, "wfg", 256, NULL, 6, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```text
DEADLOCK cycle at t=100 ms: imu -[I2C since 3]-> baro -[SD since 3]-> logger -[SPI since 3]-> imu
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: 세 태스크가 모두 t=3 ms부터 기다리기 시작했고, 50 ms 주기 모니터가 50 ms에 처음 보고 100 ms에 두 번째로 확인해 확정했다. 메시지 한 줄에 **경로 · 락 이름 · 대기 시작 시각**이 다 있어서, 필드 로그에 이 한 줄만 남아도 원인 분석이 끝난다. 확정 후 할 일은 §5.4.

설계 메모:

- 모니터는 대상 태스크들보다 **높은 우선순위**여야 한다. 그래야 CPU를 쓰는 기아·라이브락 상황에서도 돈다. 다만 모니터 자체가 락을 잡으면 안 된다(여기서는 `xSemaphoreGetMutexHolder` 안의 짧은 크리티컬 섹션뿐).
- 뮤텍스가 아닌 대기(큐·세마포어)는 소유자 개념이 없어서 이 그래프에 안 나온다. 그런 대기는 §5.3의 heartbeat와 "대기 시작 시각이 너무 오래됨"(`waitSince`)으로 잡는다.
- Zephyr에서는 래퍼 없이 `pended_on`과 `k_mutex.owner`만으로 같은 그래프를 만든다(§2.4).

### 5.3 태스크별 software watchdog → hardware watchdog

마지막 방어선은 하드웨어 watchdog(정해진 시간 안에 "kick"하지 않으면 리셋하는 카운터)이다. 흔한 실수는 **아무 태스크(또는 idle hook, 타이머 콜백)에서 그냥 kick하는 것**이다. 그러면 IMU 태스크가 데드락으로 멈춰도 kick하는 태스크는 멀쩡하니 리셋이 안 난다. 올바른 구조는 이렇다.

- 감시할 태스크마다 heartbeat 비트를 하나씩 준다. 각 태스크는 자기 루프가 한 바퀴 돌 때마다 비트를 세운다.
- supervisor 태스크가 감시 창(window)마다 비트를 읽고 지운다. **모든 비트가 섰을 때만** HW watchdog을 kick한다.
- 하나라도 빠지면 kick하지 않는다 → HW watchdog이 리셋한다. supervisor 자신이 멈춰도 kick이 없으니 리셋된다.

QEMU `mps2-an385`에는 CMSDK APB watchdog이 `0x40008000`에 있다(`info mtree`로 확인). 카운터가 한 번 0이 되면 인터럽트(AN385에서는 NMI)를 걸고 다시 장전하며, 인터럽트를 지우지 않은 채 두 번째로 0이 되면 `RESEN`이 켜져 있을 때 시스템을 리셋한다. 리셋 뒤에도 남는 기록은 링커 스크립트의 `.noinit` 섹션(`.bss`처럼 0으로 지워지지 않는 RAM)에 둔다.

무엇을 확인하는 코드인가: boot #1에서 t≈300 ms에 AB/BA 데드락을 주입하고, heartbeat 누락 → kick 중단 → NMI → 리셋 → boot #2에서 원인 기록을 읽는 전체 흐름.

```c
/* ex8: 태스크별 heartbeat → 모두 살아 있을 때만 HW watchdog(CMSDK, 0x40008000)을 찬다 */
#include "common.h"
#include "event_groups.h"
#define WDOG(o) REG32(0x40008000u + (o))      /* LOAD 0x0, CTRL 0x8, INTCLR 0xC, LOCK 0xC00 */
#define HB_IMU (1u << 0)
#define HB_LOG (1u << 1)
#define HB_ALL (HB_IMU | HB_LOG)
__attribute__((section(".noinit"))) static uint32_t magic, boots, lastMissing, lastT;
static EventGroupHandle_t hb;
static SemaphoreHandle_t mSD, mSPI;
static void wdog_start(uint32_t ms) {
    WDOG(0xC00) = 0x1ACCE551u;                 /* unlock */
    WDOG(0x0) = BOARD_CPU_HZ / 1000u * ms;     /* 한 번 0이 되면 NMI, 두 번이면 reset */
    WDOG(0x8) = 3u;                            /* INTEN | RESEN */
}
static void wdog_kick(void) { WDOG(0xC) = 1u; }
void NMI_Handler(void) {                       /* 1차 만료: 마지막 기록만 남기고 2차 만료(reset)를 기다린다 */
    lastMissing = HB_ALL & ~xEventGroupGetBitsFromISR(hb);
    lastT = xTaskGetTickCountFromISR();
    board_printf_raw("[WDT NMI] t=%u ms missing heartbeat mask=0x%x -> reset soon\n",
                     (unsigned)lastT, (unsigned)lastMissing);
    for (;;) {}
}
static void imu(void *p) { (void)p;
    for (int n = 0;; n++) {
        xSemaphoreTake(mSPI, portMAX_DELAY);
        if (boots == 1 && n == 150) { vTaskDelay(2); xSemaphoreTake(mSD, portMAX_DELAY); } /* 결함 주입 */
        xSemaphoreGive(mSPI);
        xEventGroupSetBits(hb, HB_IMU);
        vTaskDelay(pdMS_TO_TICKS(2));
    } }
static void logger(void *p) { (void)p;
    for (;;) {
        xSemaphoreTake(mSD, portMAX_DELAY);
        vTaskDelay(1);
        xSemaphoreTake(mSPI, portMAX_DELAY);
        xSemaphoreGive(mSPI); xSemaphoreGive(mSD);
        xEventGroupSetBits(hb, HB_LOG);
        vTaskDelay(pdMS_TO_TICKS(5));
    } }
static void supervisor(void *p) { (void)p;
    wdog_start(100);
    for (;;) {
#ifdef STALE_BUG   /* 타임아웃이면 clear-on-exit가 안 일어나 옛 비트가 남는다 */
        EventBits_t b = xEventGroupWaitBits(hb, HB_ALL, pdTRUE, pdTRUE, pdMS_TO_TICKS(50));
#else
        vTaskDelay(pdMS_TO_TICKS(50));
        EventBits_t b = xEventGroupClearBits(hb, HB_ALL);   /* 지우기 전 값 반환: 창마다 새로 */
#endif
        if ((b & HB_ALL) == HB_ALL) wdog_kick();
        else board_printf_raw("[sup] t=%u missing=0x%x (no kick)\n",
                              (unsigned)xTaskGetTickCount(), (unsigned)(HB_ALL & ~b));
    } }
int main(void) {
    if (magic != 0xB007B007u) { magic = 0xB007B007u; boots = 0; }
    boots++;
    board_printf_raw("== boot #%u ==\n", (unsigned)boots);
    if (boots == 2) {
        board_printf_raw("previous boot died: missing=0x%x at t=%u ms\n", (unsigned)lastMissing, (unsigned)lastT);
        board_exit(0);
    }
    hb = xEventGroupCreate(); mSD = xSemaphoreCreateMutex(); mSPI = xSemaphoreCreateMutex();
    xTaskCreate(imu, "imu", 256, NULL, 3, NULL);
    xTaskCreate(logger, "logger", 256, NULL, 2, NULL);
    xTaskCreate(supervisor, "sup", 256, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```text
== boot #1 ==
[sup] t=350 missing=0x3 (no kick)
[sup] t[WDT NMI] t=400 ms missing heartbeat mask=0x3 -> reset soon
== boot #2 ==
previous boot died: missing=0x3 at t=400 ms
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: imu의 151번째 루프(t≈300 ms)에서 데드락이 걸리고, 다음 감시 창(t=350)에서 두 비트가 모두 빠진 것(`0x3`)을 보고 kick을 멈췄다. 마지막 kick이 t=300이었으므로 100 ms 뒤인 t=400에 1차 만료 → NMI가 **supervisor의 printf 한가운데를 끊고** 들어왔다(`[sup] t[WDT NMI]` — NMI는 BASEPRI로도 못 막는다). NMI는 기록만 남기고 2차 만료를 기다리고, t≈500에 리셋. boot #2가 `.noinit`의 기록을 읽어 "이전 부팅이 왜 죽었나"를 보고한다. 실제 제품이라면 여기서 이 기록을 플래시 로그나 텔레메트리로 보낸다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">ex8 타임라인 (ms) — 감시 창 50 ms, HW watchdog 100 ms</text>
<line x1="40" y1="120" x2="660" y2="120" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="40" y="140" font-size="12" text-anchor="middle">0</text>
<text x="160" y="140" font-size="12" text-anchor="middle">100</text>
<text x="280" y="140" font-size="12" text-anchor="middle">200</text>
<text x="400" y="140" font-size="12" text-anchor="middle">300</text>
<text x="460" y="140" font-size="12" text-anchor="middle">350</text>
<text x="520" y="140" font-size="12" text-anchor="middle">400</text>
<text x="640" y="140" font-size="12" text-anchor="middle">500</text>
<line x1="100" y1="100" x2="100" y2="120" stroke="#3f9a6b" stroke-width="2"/>
<line x1="160" y1="100" x2="160" y2="120" stroke="#3f9a6b" stroke-width="2"/>
<line x1="220" y1="100" x2="220" y2="120" stroke="#3f9a6b" stroke-width="2"/>
<line x1="280" y1="100" x2="280" y2="120" stroke="#3f9a6b" stroke-width="2"/>
<line x1="340" y1="100" x2="340" y2="120" stroke="#3f9a6b" stroke-width="2"/>
<line x1="400" y1="100" x2="400" y2="120" stroke="#3f9a6b" stroke-width="2"/>
<text x="250" y="92" font-size="12" text-anchor="middle">50 ms마다 kick (비트 다 섬)</text>
<rect x="400" y="50" width="12" height="70" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="406" y="44" font-size="12" text-anchor="middle">데드락</text>
<line x1="460" y1="100" x2="460" y2="120" stroke="#e08a3c" stroke-width="2" stroke-dasharray="3 2"/>
<text x="460" y="172" font-size="12" text-anchor="middle">비트 0x3 없음</text>
<text x="460" y="188" font-size="12" text-anchor="middle">kick 안 함</text>
<line x1="520" y1="60" x2="520" y2="120" stroke="#d0564a" stroke-width="2"/>
<text x="520" y="54" font-size="12" text-anchor="middle">NMI: 기록</text>
<line x1="640" y1="60" x2="640" y2="120" stroke="#d0564a" stroke-width="3"/>
<text x="640" y="54" font-size="12" text-anchor="middle">리셋</text>
<text x="590" y="172" font-size="12" text-anchor="middle">boot #2가</text>
<text x="590" y="188" font-size="12" text-anchor="middle">.noinit 기록 읽음</text>
<text x="40" y="215" font-size="12">최악 탐지 지연 = 감시 창 + watchdog 주기 × 2 ≈ 250 ms (이 설정)</text>
</svg>
```
그림 7 — kick은 "모든 태스크가 살아 있다"는 증거가 모였을 때만 일어난다. 데드락 후 최대 한 감시 창 안에 kick이 끊기고, watchdog 1차 만료에서 NMI로 원인을 기록하고, 2차 만료에서 리셋한다.

**함정 — 이벤트 그룹 heartbeat의 "묵은 비트"**

처음에는 supervisor를 `xEventGroupWaitBits(hb, HB_ALL, pdTRUE /*clear on exit*/, pdTRUE /*wait all*/, 50 ms)`로 짰다. `-DSTALE_BUG`로 그 버전을 돌린 결과다.

```text
== boot #1 ==
[sup] t=345 missing=0x2 (no kick)
[sup] t=395 missing=0x2 (no kick)
[WDT NMI] t=395 ms missing heartbeat mask=0x2 -> reset soon
== boot #2 ==
previous boot died: missing=0x2 at t=395 ms
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: imu도 분명히 멈췄는데 missing이 `0x2`(logger만)로 나온다. 원인은 `event_groups.c`의 타임아웃 경로다. 대기가 **조건 충족으로 끝날 때만** clear-on-exit가 비트를 지우고, 타임아웃으로 끝나면 "현재 비트 값을 돌려줄 뿐"(소스 주석: `The task timed out, just return the current event bit value.`) 지우지 않는다. 그래서 imu가 멈추기 직전에 세운 비트가 창이 바뀌어도 계속 남아 "imu는 살아 있다"고 거짓말한다. 원인 기록이 틀리면 필드 분석이 엉뚱한 태스크를 쫓는다. 고친 버전은 창마다 `xEventGroupClearBits()`(지우기 직전 값을 돌려준다)로 **읽고 지우기를 한 번에** 한다.

### 5.4 탐지한 다음에는 — 복구 정책

| 상황 | 할 일 | 예 |
|---|---|---|
| 개발·CI | 즉시 `configASSERT` → 멈추고 디버거 | ex5의 순서 검사기 |
| 양산, 비안전 기능 | 카운터 + 로그 + 해당 기능만 재시작 | 로거 태스크 재생성, SD 재마운트 |
| 양산, 안전 기능 | 안전 상태로 전환 후 리셋 | 드론: 모터 failsafe(착륙·disarm) → watchdog 리셋 |
| 원인 기록 | `.noinit` RAM 또는 백업 레지스터에 경로·시각 저장 | ex8 boot #2 보고 |
| 재발 방지 | 대기 그래프 한 줄 + 빌드 해시를 텔레메트리로 | "imu -[I2C]-> baro -[SD]-> logger" |

데드락을 "풀어서 계속 진행"하는 것(희생자를 골라 락을 강제로 뺏기)은 RTOS에서 거의 하지 않는다. 뺏긴 쪽의 자료구조가 반쯤 고쳐진 상태로 남기 때문이다. **기록하고, 안전하게 멈추고, 다시 시작한다**가 표준이다.

---

## 6. 기아 — 시스템은 도는데 한 명이 굶는다

### 6.1 우선순위 기아 — 고정 우선순위 선점의 본성

고정 우선순위 선점 스케줄러(C4)는 "Ready인 태스크 중 가장 높은 것"을 돌린다. 이건 버그가 아니라 계약이다. 따라서 높은 태스크가 CPU를 다 쓰면 낮은 태스크는 **정의상** 한 번도 못 돈다. 낮은 태스크가 얼마나 진척하는지는 높은 태스크들의 부하(utilization)가 정한다.

무엇을 확인하는 코드인가: prio 3 태스크가 10 ms 주기 중 DUTY%만큼 바쁠 때, 쉬지 않는 prio 1 태스크가 1초에 몇 번 도는지와 최장 공백.

```c
/* ex9: 우선순위 기아 — 높은 태스크의 duty가 오를수록 낮은 태스크 진척은? */
#include "common.h"
#ifndef DUTY
#define DUTY 50                                /* 높은 태스크가 10 ms 주기 중 바쁜 비율(%) */
#endif
static volatile uint32_t lowWork, maxGapUs;
static void busy_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) {} }
static void high(void *p) { (void)p;           /* 예: 자세 추정 + 제어 (prio 3) */
    TickType_t last = xTaskGetTickCount();
    for (;;) { busy_us(100u * DUTY); vTaskDelayUntil(&last, pdMS_TO_TICKS(10)); } }
static void low(void *p) { (void)p;            /* 예: 텔레메트리 압축 (prio 1), 쉬지 않는 배경 일 */
    uint32_t prev = board_now_us();
    for (;;) {
        uint32_t now = board_now_us();
        if (now - prev > maxGapUs) maxGapUs = now - prev;
        prev = now;
        lowWork++;
    } }
static void report(void *p) { (void)p;
    vTaskDelay(pdMS_TO_TICKS(1000));
    board_printf_raw("DUTY=%u lowWork=%u maxGapUs=%u\n", (unsigned)DUTY, (unsigned)lowWork, (unsigned)maxGapUs);
    board_exit(0); }
int main(void) {
    xTaskCreate(high, "high", 256, NULL, 3, NULL);
    xTaskCreate(low, "low", 256, NULL, 1, NULL);
    xTaskCreate(report, "report", 256, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for d in 0 10 25 50 75 90 95 99 100; do
  make -s -B -f .../freertos-mps2/Makefile SRCS=.../ex9.c OUT=/tmp/rtos_D9/ex9 EXTRA_CFLAGS="-DDUTY=$d"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D9/ex9/app.elf 30 | grep DUTY
done
```

```text
DUTY=0 lowWork=1730956 maxGapUs=17
DUTY=10 lowWork=1557683 maxGapUs=1017
DUTY=25 lowWork=1297533 maxGapUs=2517
DUTY=50 lowWork=864256 maxGapUs=5017
DUTY=75 lowWork=430734 maxGapUs=7517
DUTY=90 lowWork=170828 maxGapUs=9017
DUTY=95 lowWork=84050 maxGapUs=9517
DUTY=99 lowWork=14567 maxGapUs=9917
DUTY=100 lowWork=0 maxGapUs=0
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">높은 태스크 duty vs 낮은 태스크 진척 (1 s, QEMU icount)</text>
<line x1="60" y1="230" x2="640" y2="230" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="230" stroke="currentColor"/>
<line x1="56" y1="230.0" x2="640" y2="230.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="234.0" font-size="12" text-anchor="end">0%</text>
<text x="60.0" y="248" font-size="12" text-anchor="middle">0%</text>
<line x1="56" y1="182.5" x2="640" y2="182.5" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="186.5" font-size="12" text-anchor="end">25%</text>
<text x="205.0" y="248" font-size="12" text-anchor="middle">25%</text>
<line x1="56" y1="135.0" x2="640" y2="135.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="139.0" font-size="12" text-anchor="end">50%</text>
<text x="350.0" y="248" font-size="12" text-anchor="middle">50%</text>
<line x1="56" y1="87.5" x2="640" y2="87.5" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="91.5" font-size="12" text-anchor="end">75%</text>
<text x="495.0" y="248" font-size="12" text-anchor="middle">75%</text>
<line x1="56" y1="40.0" x2="640" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="44.0" font-size="12" text-anchor="end">100%</text>
<text x="640.0" y="248" font-size="12" text-anchor="middle">100%</text>
<polyline points="60.0,40.0 118.0,59.0 205.0,87.6 350.0,135.1 495.0,182.7 582.0,211.2 611.0,220.8 634.2,228.4 640.0,230.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="60.0" cy="40.0" r="4" fill="#4a7bd0"/>
<circle cx="118.0" cy="59.0" r="4" fill="#4a7bd0"/>
<circle cx="205.0" cy="87.6" r="4" fill="#4a7bd0"/>
<circle cx="350.0" cy="135.1" r="4" fill="#4a7bd0"/>
<circle cx="495.0" cy="182.7" r="4" fill="#4a7bd0"/>
<circle cx="582.0" cy="211.2" r="4" fill="#4a7bd0"/>
<circle cx="611.0" cy="220.8" r="4" fill="#4a7bd0"/>
<circle cx="634.2" cy="228.4" r="4" fill="#4a7bd0"/>
<circle cx="640.0" cy="230.0" r="4" fill="#4a7bd0"/>
<text x="126.0" y="51.0" font-size="12">최대 공백 1.0 ms</text>
<text x="358.0" y="127.1" font-size="12">최대 공백 5.0 ms</text>
<text x="503.0" y="174.7" font-size="12">최대 공백 7.5 ms</text>
<text x="636" y="60" font-size="12" text-anchor="end">duty 100% → 진척 0 (완전 기아)</text>
<text x="350" y="270" font-size="12" text-anchor="middle">prio 3 태스크가 10 ms 주기 중 바쁜 비율 (duty)</text>
<text x="14" y="135" font-size="12" transform="rotate(-90 14 135)" text-anchor="middle">prio 1 진척 (duty 0 대비)</text>
</svg>
```
그림 8 — 낮은 태스크의 진척은 높은 태스크의 남은 CPU에 정확히 비례하고(직선), 최장 공백은 "높은 태스크가 한 번에 붙잡는 시간"과 같다. DUTY=100이면 0이다 — maxGapUs도 0인 것은 low가 **한 번도 실행되지 않아** 공백을 잴 기회조차 없었기 때문이다.

출력에서 볼 것: 진척이 선형으로 줄어드는 것은 당연하다. 실무에서 무서운 것은 두 가지다. 첫째, **99%에서도 낮은 태스크는 "가끔은" 돈다**(14,567회). 텔레메트리가 느려질 뿐 멈추지는 않아서 기아를 알아채기 어렵다. 둘째, 최장 공백(9.9 ms)이 낮은 태스크의 **응답 시간 상한**이 된다. 낮은 태스크가 watchdog heartbeat를 맡고 있으면 이 공백이 곧 오탐 리셋의 원인이다. 그래서 heartbeat 감시 창은 "가장 낮은 감시 대상 태스크의 최악 응답 시간"보다 길어야 한다. 이 응답 시간을 계산하는 법(RTA)은 N 모듈이 다룬다.

### 6.2 같은 우선순위끼리의 공정성 — time slicing

같은 우선순위의 두 CPU-bound 태스크는 어떻게 나눠 쓸까? FreeRTOS는 `configUSE_TIME_SLICING=1`(기본)이면 tick마다 같은 우선순위의 다음 태스크로 넘긴다.

무엇을 확인하는 코드인가: 블록하지 않는 두 prio 2 태스크의 몫을 time slicing on/off, 그리고 "1 ms마다 잠깐 깨어나는 높은 태스크"(TICKER) 유무로 비교.

```c
/* ex10: 같은 우선순위끼리의 공정성 — configUSE_TIME_SLICING, 그리고 높은 태스크의 깨어남 */
#include "common.h"
#ifndef TICKER
#define TICKER 0                               /* 1이면 prio 4 태스크가 1 ms마다 잠깐 깨어난다 */
#endif
static volatile uint32_t cnt[2];
static void spin(void *p) { volatile uint32_t *c = p; for (;;) (*c)++; }   /* 절대 블록하지 않음 */
static void ticker(void *p) { (void)p; for (;;) vTaskDelay(1); }
static void report(void *p) { (void)p;
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t a = cnt[0], b = cnt[1];
    board_printf_raw("TIME_SLICING=%d TICKER=%d  A=%u B=%u  A share=%u%%\n", configUSE_TIME_SLICING, TICKER,
                     (unsigned)a, (unsigned)b, (unsigned)(100ull * a / (a + b)));
    board_exit(0); }
int main(void) {
    xTaskCreate(spin, "A", 256, (void *)&cnt[0], 2, NULL);
    xTaskCreate(spin, "B", 256, (void *)&cnt[1], 2, NULL);
    if (TICKER) xTaskCreate(ticker, "tick", 256, NULL, 4, NULL);
    xTaskCreate(report, "report", 256, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for s in 1 0; do for t in 0 1; do
  run.sh ex10 -B EXTRA_CFLAGS="-DconfigUSE_TIME_SLICING=$s -DTICKER=$t"
done; done
```

```text
TIME_SLICING=1 TICKER=0  A=3888114 B=3888250  A share=49%
TIME_SLICING=1 TICKER=1  A=3847257 B=3847374  A share=49%
TIME_SLICING=0 TICKER=0  A=7803837 B=0  A share=100%
TIME_SLICING=0 TICKER=1  A=3848630 B=3848749  A share=49%
```

(각 실행 뒤의 `[board_exit] code=0`, `[rc=0]` 줄은 생략했다.)

출력에서 볼 것: time slicing을 끄면 B는 1초 동안 **0번** 돈다 — 같은 우선순위 기아. 그런데 마지막 줄이 놀랍다. slicing을 꺼도 1 ms마다 높은 태스크가 깨어났다 잠들면 다시 반반이다. 이유는 `tasks.c`의 `taskSELECT_HIGHEST_PRIORITY_TASK()`가 `listGET_OWNER_OF_NEXT_ENTRY()`로 ready 리스트의 **인덱스를 한 칸 전진시키면서** 다음 태스크를 고르기 때문이다. 선점이 한 번 일어나 문맥 전환이 다시 일어날 때마다 같은 우선순위의 "다음" 태스크가 뽑힌다. tick에 의한 회전(`configUSE_TIME_SLICING`이 켜는 것, `xTaskIncrementTick` 안의 `listCURRENT_LIST_LENGTH(...) > 1` 검사)과 별개로 **문맥 전환 자체가 라운드 로빈**인 셈이다. 그래서 "slicing을 껐는데 왜 번갈아 도나?"라는 현장 질문의 답이 된다. 반대로 slicing을 켜 둬도 둘 다 블록하지 않는 CPU-bound 설계라면 둘 다 그보다 낮은 태스크를 굶긴다.

### 6.3 idle 기아 — FreeRTOS에서는 메모리가 샌다

idle 태스크(prio 0)는 할 일이 없을 때 도는 태스크다(C9). FreeRTOS에서 idle은 할 일이 하나 더 있다. **스스로 삭제한 태스크의 TCB와 스택을 해제하는 일**이다. `vTaskDelete(NULL)`을 부른 태스크는 지금 자기 스택 위에서 달리고 있으니 그 자리에서 스택을 해제할 수 없다. 그래서 커널은 그 TCB를 `xTasksWaitingTermination` 리스트에 넣고 `uxDeletedTasksWaitingCleanUp`을 올려 두고, idle 루프 첫 줄의 `prvCheckTasksWaitingTermination()`이 나중에 `prvDeleteTCB()`로 해제한다(V11.3.2 `tasks.c`). 다른 태스크를 지우는 `vTaskDelete(other)`는 그 자리에서 `prvDeleteTCB()`를 부른다. 그러면 idle이 못 돌면?

무엇을 확인하는 코드인가: 10 ms마다 worker를 만들어 스스로 지우게 하면서(SELF=1) 또는 spawner가 지우게 하면서(SELF=0), prio 1 hog가 idle을 굶길 때 heap과 태스크 수.

```c
/* ex11: idle 기아 — idle이 못 돌면 스스로 지운 태스크의 TCB·스택이 회수되지 않는다 */
#include "common.h"
#ifndef SELF
#define SELF 1                                 /* 1: worker가 vTaskDelete(NULL), 0: spawner가 지운다 */
#endif
static volatile uint32_t idleRuns, hogPause;
void vApplicationIdleHook(void) { idleRuns++; }
static void worker(void *p) { (void)p;
#if SELF
    vTaskDelete(NULL);                         /* 자기 자신 삭제 → 회수는 idle 몫 */
#else
    vTaskSuspend(NULL);                        /* spawner가 지울 때까지 대기 */
#endif
}
static void spawner(void *p) { (void)p;
    for (;;) {
        TaskHandle_t w;
        if (xTaskCreate(worker, "w", 256, NULL, 3, &w) != pdPASS) board_printf_raw("create failed\n");
#if !SELF
        vTaskDelete(w);                        /* 남이 지우면 그 자리에서 prvDeleteTCB */
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    } }
static void hog(void *p) { (void)p;            /* prio 1, 블록하지 않는 배경 일 → idle(prio 0) 기아 */
    for (;;) { if (hogPause) { vTaskDelay(pdMS_TO_TICKS(20)); hogPause = 0; } }
}
static void report(void *p) { (void)p;
    for (int s = 1; s <= 8; s++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s == 6) hogPause = 1;              /* 600 ms 무렵 hog가 20 ms 쉰다 */
        board_printf_raw("t=%u freeHeap=%u tasks=%u idleRuns=%u\n", (unsigned)xTaskGetTickCount(),
            (unsigned)xPortGetFreeHeapSize(), (unsigned)uxTaskGetNumberOfTasks(), (unsigned)idleRuns);
    }
    board_exit(0); }
int main(void) {
    xTaskCreate(spawner, "spawner", 256, NULL, 2, NULL);
    xTaskCreate(hog, "hog", 256, NULL, 1, NULL);
    xTaskCreate(report, "report", 256, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for s in 1 0; do run.sh ex11 -B EXTRA_CFLAGS="-DSELF=$s -DconfigUSE_IDLE_HOOK=1"; done
```

```text
t=100 freeHeap=116288 tasks=15 idleRuns=0
t=200 freeHeap=104928 tasks=25 idleRuns=0
t=300 freeHeap=93568 tasks=35 idleRuns=0
t=400 freeHeap=82208 tasks=45 idleRuns=0
t=500 freeHeap=70848 tasks=55 idleRuns=0
t=600 freeHeap=59488 tasks=65 idleRuns=0
t=700 freeHeap=118560 tasks=13 idleRuns=37522
t=800 freeHeap=107200 tasks=23 idleRuns=37522
[board_exit] code=0
[rc=0]
t=100 freeHeap=127648 tasks=5 idleRuns=0
t=200 freeHeap=127648 tasks=5 idleRuns=0
t=300 freeHeap=127648 tasks=5 idleRuns=0
t=400 freeHeap=127648 tasks=5 idleRuns=0
t=500 freeHeap=127648 tasks=5 idleRuns=0
t=600 freeHeap=127648 tasks=5 idleRuns=0
t=700 freeHeap=127648 tasks=5 idleRuns=38678
t=800 freeHeap=127648 tasks=5 idleRuns=38678
[board_exit] code=0
[rc=0]
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">idle이 못 도는 동안의 남은 heap (worker 10 ms마다 생성·삭제)</text>
<line x1="70" y1="220" x2="640" y2="220" stroke="currentColor"/>
<line x1="70" y1="40" x2="70" y2="220" stroke="currentColor"/>
<line x1="66" y1="220.0" x2="640" y2="220.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="224.0" font-size="12" text-anchor="end">50k</text>
<line x1="66" y1="175.0" x2="640" y2="175.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="179.0" font-size="12" text-anchor="end">70k</text>
<line x1="66" y1="130.0" x2="640" y2="130.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="134.0" font-size="12" text-anchor="end">90k</text>
<line x1="66" y1="85.0" x2="640" y2="85.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="89.0" font-size="12" text-anchor="end">110k</text>
<line x1="66" y1="40.0" x2="640" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="44.0" font-size="12" text-anchor="end">130k</text>
<text x="70.0" y="238" font-size="12" text-anchor="middle">0</text>
<text x="212.5" y="238" font-size="12" text-anchor="middle">200</text>
<text x="355.0" y="238" font-size="12" text-anchor="middle">400</text>
<text x="497.5" y="238" font-size="12" text-anchor="middle">600</text>
<text x="640.0" y="238" font-size="12" text-anchor="middle">800</text>
<rect x="497.5" y="40" width="14.2" height="180" fill="#3f9a6b" fill-opacity="0.15" stroke="none"/>
<text x="490.4" y="80.5" font-size="12" text-anchor="end">600 ms: hog가 20 ms 쉼 → idle 실행</text>
<polyline points="141.2,70.9 212.5,96.4 283.8,122.0 355.0,147.5 426.2,173.1 497.5,198.7 568.8,65.7 640.0,91.3" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="141.2" cy="70.9" r="3.5" fill="#d0564a"/>
<circle cx="212.5" cy="96.4" r="3.5" fill="#d0564a"/>
<circle cx="283.8" cy="122.0" r="3.5" fill="#d0564a"/>
<circle cx="355.0" cy="147.5" r="3.5" fill="#d0564a"/>
<circle cx="426.2" cy="173.1" r="3.5" fill="#d0564a"/>
<circle cx="497.5" cy="198.7" r="3.5" fill="#d0564a"/>
<circle cx="568.8" cy="65.7" r="3.5" fill="#d0564a"/>
<circle cx="640.0" cy="91.3" r="3.5" fill="#d0564a"/>
<polyline points="141.2,45.3 212.5,45.3 283.8,45.3 355.0,45.3 426.2,45.3 497.5,45.3 568.8,45.3 640.0,45.3" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="141.2" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="212.5" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="283.8" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="355.0" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="426.2" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="497.5" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="568.8" cy="45.3" r="3.5" fill="#3f9a6b"/>
<circle cx="640.0" cy="45.3" r="3.5" fill="#3f9a6b"/>
<text x="84.2" y="197.5" font-size="12">빨강 vTaskDelete(NULL): 100 ms마다 11,360 B씩 샘</text>
<text x="84.2" y="213.5" font-size="12">초록 남이 vTaskDelete(w): 127,648 B 그대로</text>
<text x="355" y="260" font-size="12" text-anchor="middle">시간 (ms)</text>
<text x="16" y="130" font-size="12" transform="rotate(-90 16 130)" text-anchor="middle">xPortGetFreeHeapSize (B)</text>
</svg>
```
그림 9 — 스스로 지운 태스크의 메모리는 idle이 돌 때만 돌아온다. hog가 20 ms 쉬는 동안 idle이 37,522번 돌면서 쌓인 TCB를 한꺼번에 해제했다(heap 59,488 → 118,560 B).

출력에서 볼 것:

- SELF=1: idle hook 카운트가 600 ms까지 0이고, 100 ms마다 heap이 정확히 11,360 B(태스크 10개 × 1,136 B = 스택 256 word 1,024 B + TCB와 heap_4 헤더)씩 줄었다. `uxTaskGetNumberOfTasks()`도 15 → 65로 늘었다 — **삭제됐지만 회수되지 않은 태스크도 개수에 들어간다**(`uxCurrentNumberOfTasks`는 회수할 때 줄어든다). 이대로면 약 1.1초 뒤 `vApplicationMallocFailedHook`이 불린다. 증상은 "몇 시간 뒤 `xTaskCreate` 실패" 또는 "malloc failed hook"인데, 진짜 원인은 메모리 누수가 아니라 **idle 기아**다.
- SELF=0: 다른 태스크가 지우면 heap은 한 바이트도 안 변한다. idle이 굶어도 상관없다.
- 둘 다 idle hook은 600 ms까지 0번 돈다. idle hook에 저전력 진입(`__WFI`)이나 HW watchdog kick을 넣어 둔 설계라면 그것도 같이 멈춘다 — idle hook kick은 "가장 낮은 우선순위까지 CPU가 돌아온다"는 증거로는 쓸 만하지만, 이렇게 기아만으로도 리셋이 난다.

고치는 법: CPU-bound 배경 일은 idle보다 높은 우선순위에 두더라도 주기적으로 블록하게 한다(`vTaskDelay(1)` 한 줄), 동적으로 태스크를 만들고 지우는 설계 자체를 피한다(태스크 풀 + 큐), 또는 배경 일을 idle hook 안에서 작은 조각으로 한다. Zephyr는 종료된 스레드의 스택을 커널이 해제하지 않는다(정적 스택이 기본이고, 동적 스택은 `k_thread_stack_free`를 앱이 부른다) — FreeRTOS만의 함정이다.

### 6.4 바쁜 뮤텍스에서 대기자 기아

FreeRTOS 뮤텍스의 대기 리스트는 **우선순위 순**이다. `vTaskPlaceOnEventList()`가 `xEventListItem`의 값을 `configMAX_PRIORITIES - uxPriority`로 두고 `vListInsert()`로 정렬 삽입하기 때문이다(그래서 give하면 가장 높은 대기자가 받는다). 공정한 FIFO가 아니다.

무엇을 확인하는 코드인가: prio 4·3·2 세 태스크가 같은 뮤텍스를 "쥔 채 DMA 완료 2 ms 대기 → 놓고 1 ms 준비 → 다시 요청"으로 쓸 때 각자의 획득 횟수.

```c
/* ex12: 바쁜 뮤텍스에서 낮은 우선순위 대기자의 기아 — 대기열은 우선순위 순 */
#include "common.h"
static SemaphoreHandle_t m;
static volatile uint32_t got[3], maxWaitMs[3], tmo[3];
static void user(void *p) {
    int i = (int)(uintptr_t)p;
    for (;;) {
        TickType_t t0 = xTaskGetTickCount();
        if (xSemaphoreTake(m, pdMS_TO_TICKS(500)) != pdTRUE) { tmo[i]++; continue; }
        TickType_t w = xTaskGetTickCount() - t0;
        if (w > maxWaitMs[i]) maxWaitMs[i] = w;
        got[i]++;
        vTaskDelay(pdMS_TO_TICKS(2));          /* 뮤텍스를 쥔 채 DMA 완료 대기 2 ms */
        xSemaphoreGive(m);
        vTaskDelay(pdMS_TO_TICKS(1));          /* 다음 전송 준비 1 ms */
    } }
static void report(void *p) { (void)p;
    vTaskDelay(pdMS_TO_TICKS(3000));
    for (int i = 0; i < 3; i++)
        board_printf_raw("user%d(prio %d): got=%u maxWait=%u ms timeouts(500ms)=%u\n", i, 4 - i,
                         (unsigned)got[i], (unsigned)maxWaitMs[i], (unsigned)tmo[i]);
    board_exit(0); }
int main(void) {
    m = xSemaphoreCreateMutex();
    for (int i = 0; i < 3; i++) xTaskCreate(user, "user", 256, (void *)(uintptr_t)i, 4 - i, NULL);
    xTaskCreate(report, "report", 256, NULL, 6, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```text
user0(prio 4): got=750 maxWait=1 ms timeouts(500ms)=0
user1(prio 3): got=750 maxWait=2 ms timeouts(500ms)=0
user2(prio 2): got=0 maxWait=0 ms timeouts(500ms)=5
[board_exit] code=0
[rc=0]
```

출력에서 볼 것: 3초 동안 prio 2는 **한 번도** 못 잡았다(500 ms 타임아웃만 5번). 뮤텍스가 풀리는 순간마다 user0 또는 user1이 이미 대기 중이고, 둘이 번갈아 받아 가며 뮤텍스가 한 번도 "아무도 안 기다리는" 상태가 되지 않는다. CPU는 대부분 놀고 있다(둘 다 뮤텍스를 쥔 채 블록) — **CPU 기아가 아니라 자원 기아**다. 원인은 뮤텍스를 쥔 채 블록하는 설계(쥔 시간 2 ms ≫ 필요한 시간)다. 고치는 법은 쥔 채 기다리지 않기(DMA 시작 → 놓기 → 완료 통지 받기), 버스 게이트키퍼로 요청을 FIFO로 받기(D7), 또는 요청 비율 자체를 줄이기. Zephyr `k_mutex`의 대기 큐도 기본이 우선순위 순(`_priq_wait_add`)이라 같은 일이 생긴다.

---

## 7. 라이브락 — 깨어 있는데 아무것도 끝내지 못한다

### 7.1 예의 바른 재시도가 맞물릴 때

§4.3의 try-lock + 전부 놓기는 데드락을 깨지만, **두 태스크가 같은 리듬으로 놓고 다시 잡으면** 둘 다 영원히 실패한다. 복도에서 마주친 두 사람이 동시에 같은 쪽으로 비켜서는 상황이다.

무엇을 확인하는 코드인가: A는 m0→m1, B는 m1→m0을 try-lock으로 잡는다. 준비 작업(1.5 ms)이 tick(1 ms)보다 길어서 둘 다 "첫 락을 쥔 채" 선점된다. 실패하면 첫 락을 놓고 백오프한 뒤 다시 시도한다. 백오프 방식 4가지를 비교한다.

```c
/* ex13: 라이브락 — 둘 다 "예의 바르게" 놓고 다시 시도하는데 아무도 끝내지 못한다 */
#include "common.h"
#ifndef MODE
#define MODE 0              /* 0 고정 spin, 1 무작위 spin, 2 비대칭 spin, 3 블록하는 백오프 */
#endif
static SemaphoreHandle_t m[2];
static volatile uint32_t done[2], fails[2];
static void busy_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) {} }
static void worker(void *p) {
    int i = (int)(uintptr_t)p;
    SemaphoreHandle_t first = m[i], second = m[1 - i];      /* A: m0→m1, B: m1→m0 */
    uint32_t seed = 0x1234u + 77u * (uint32_t)i; (void)seed;
    for (;;) {
        if (xSemaphoreTake(first, 0) != pdTRUE) continue;
        busy_us(1500);                                      /* 첫 자원으로 하는 준비 작업 (1 tick보다 길다) */
        if (xSemaphoreTake(second, 0) == pdTRUE) {          /* 둘 다 얻음 → 일 완료 */
            done[i]++;
            xSemaphoreGive(second); xSemaphoreGive(first);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        xSemaphoreGive(first);                              /* 양보: 쥔 것을 놓는다 */
        fails[i]++;
#if MODE == 0
        busy_us(20);                                        /* 고정 길이 → 둘이 같은 리듬 */
#elif MODE == 1
        seed = seed * 1103515245u + 12345u;
        busy_us(20 + (seed >> 16) % 800);                   /* 무작위 길이 → 리듬이 깨진다 */
#elif MODE == 2
        busy_us(i ? 1200 : 20);                             /* 비대칭: B가 항상 길게 물러난다 */
#else
        vTaskDelay(1);                                      /* 블록 → 상대가 CPU와 락을 둘 다 얻는다 */
#endif
    } }
static void report(void *p) { (void)p;
    vTaskDelay(pdMS_TO_TICKS(1000));
    board_printf_raw("MODE=%d  A done=%u fails=%u | B done=%u fails=%u\n", MODE,
        (unsigned)done[0], (unsigned)fails[0], (unsigned)done[1], (unsigned)fails[1]);
    board_exit(0); }
int main(void) {
    m[0] = xSemaphoreCreateMutex(); m[1] = xSemaphoreCreateMutex();
    xTaskCreate(worker, "A", 256, (void *)0, 2, NULL);
    xTaskCreate(worker, "B", 256, (void *)1, 2, NULL);
    xTaskCreate(report, "report", 256, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for md in 0 1 2 3; do run.sh ex13 -B EXTRA_CFLAGS=-DMODE=$md; done
```

```text
MODE=0  A done=0 fails=499 | B done=0 fails=499
MODE=1  A done=65 fails=292 | B done=65 fails=292
MODE=2  A done=111 fails=111 | B done=111 fails=111
MODE=3  A done=166 fails=1 | B done=167 fails=0
```

(각 줄 뒤의 `[board_exit]`, `[rc=0]`은 생략했다.)

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">MODE=0 고정 백오프: tick마다 교대하지만 둘 다 "첫 락을 쥔 채" 넘겨준다</text>
<text x="10" y="62" font-size="12">A</text>
<text x="10" y="102" font-size="12">B</text>
<line x1="60" y1="120" x2="670" y2="120" stroke="currentColor"/>
<text x="60" y="136" font-size="12" text-anchor="middle">0</text>
<text x="205" y="136" font-size="12" text-anchor="middle">1 ms</text>
<text x="350" y="136" font-size="12" text-anchor="middle">2 ms</text>
<text x="495" y="136" font-size="12" text-anchor="middle">3 ms</text>
<text x="640" y="136" font-size="12" text-anchor="middle">4 ms</text>
<rect x="60" y="45" width="145" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="350" y="45" width="145" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="205" y="85" width="145" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="495" y="85" width="145" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="350" y="45" width="6" height="24" fill="#d0564a" stroke="#d0564a"/>
<rect x="495" y="85" width="6" height="24" fill="#d0564a" stroke="#d0564a"/>
<text x="362" y="61" font-size="12">m1 실패 → m0 놓았다 다시</text>
<text x="507" y="101" font-size="12">m0 실패 → 반복</text>
<rect x="60" y="160" width="610" height="16" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
<text x="66" y="172" font-size="12">m0: A가 거의 항상 쥠 (놓는 20 µs 동안 B는 실행 중이 아니다)</text>
<rect x="60" y="184" width="610" height="16" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="66" y="196" font-size="12">m1: B가 거의 항상 쥠</text>
<text x="10" y="222" font-size="12">놓는 순간에 상대가 CPU를 갖고 있지 않으니, 놓아 봐야 아무도 못 가져간다.</text>
</svg>
```
그림 10 — 단일 코어 라이브락의 정체. 둘 다 양보는 하는데 **양보하는 순간에 상대가 실행 중이 아니다**. 선점은 항상 1.5 ms짜리 준비 작업 도중(첫 락을 쥔 상태)에 일어난다.

```svg
<svg viewBox="0 0 680 265" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">라이브락: 1 s 동안 끝낸 일 (A+B) — 백오프 방식별</text>
<line x1="200" y1="40" x2="200" y2="234" stroke="currentColor"/>
<text x="192" y="71" font-size="12" text-anchor="end">고정 spin 20 µs</text>
<text x="208.0" y="71" font-size="12">done 0, fails 998</text>
<text x="192" y="117" font-size="12" text-anchor="end">무작위 spin 20~820 µs</text>
<rect x="200" y="96" width="123.8" height="34" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="331.8" y="117" font-size="12">done 130, fails 584</text>
<text x="192" y="163" font-size="12" text-anchor="end">비대칭 spin 20 / 1200 µs</text>
<rect x="200" y="142" width="211.4" height="34" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="419.4" y="163" font-size="12">done 222, fails 222</text>
<text x="192" y="209" font-size="12" text-anchor="end">블록 vTaskDelay(1)</text>
<rect x="200" y="188" width="317.1" height="34" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<text x="525.1" y="209" font-size="12">done 333, fails 1</text>
<text x="200" y="250" font-size="12">고정 spin: CPU 100% 사용 중인데 일은 0 — 블록된 태스크는 하나도 없다</text>
</svg>
```
그림 11 — 같은 코드, 백오프만 다르게. 고정 spin은 1초 동안 일 0건(실패 998회), 무작위 spin은 130건, 비대칭 spin은 222건, 블록하는 백오프는 333건(실패 1회).

출력에서 볼 것:

- MODE=0: **CPU는 100% 쓰이는데 일은 0건**이다. 태스크 덤프를 찍으면 둘 다 Running/Ready라서 데드락 탐지기(대기 그래프)에 안 걸린다. 진척 카운터(done)와 시도 카운터(fails)를 같이 봐야 보인다.
- MODE=1: 백오프 길이를 무작위로 하면 가끔 백오프가 tick 경계를 넘어 상대가 그 사이에 두 락을 다 얻는다. 이더넷의 random exponential backoff와 같은 원리다. 하지만 여전히 실패가 성공의 4.5배다.
- MODE=2: 한쪽만 길게(1.2 ms) 물러나면 대칭이 깨진다. "우선순위가 낮은 쪽이 더 오래 물러난다" 같은 규칙이 이것이다.
- MODE=3: RTOS에서의 정답은 대개 **spin하지 말고 블록하는 것**이다. `vTaskDelay(1)`로 물러나면 상대가 CPU와 락을 동시에 얻어 일을 끝낸다. spin 백오프는 멀티코어(상대가 진짜로 동시에 도는 경우)에서나 의미가 있다. 더 근본적인 정답은 물론 순위표(§4.1)로 try-lock 자체를 없애는 것이다.

### 7.2 인터럽트 라이브락 — ISR이 CPU를 다 먹는다

라이브락의 다른 얼굴은 **인터럽트 폭주(interrupt storm)**다. 인터럽트는 어떤 태스크보다 우선하므로, 인터럽트율 × ISR 시간이 100%에 다가가면 태스크는 아무것도 못 한다. CPU는 쉬지 않고 일하는데(ISR) 시스템은 아무것도 끝내지 못한다. Linux 네트워크 스택이 NAPI(인터럽트 → 폴링 전환)를 만든 이유와 같은 현상이다.

무엇을 확인하는 코드인가: TIMER0 인터럽트(ISR 1회 20 µs 일)의 비율을 올리면서 prio 1 태스크의 진척과 "500 tick이 실제로 얼마나 걸렸나".

```c
/* ex14: 인터럽트 라이브락 — ISR 폭주가 태스크 CPU를 다 먹는다 */
#include "common.h"
#ifndef RATE
#define RATE 10000                             /* TIMER0 인터럽트 Hz */
#endif
#define ISR_US 20                              /* ISR 1회 작업량 (FIFO 비우기 등) */
static volatile uint32_t isrCnt, taskWork;
void TIMER0_IRQHandler(void) {
    TIMER_INTCLR(TIMER0_BASE) = 1;
    uint32_t t0 = board_now_us();
    while (board_now_us() - t0 < ISR_US) {}
    isrCnt++;
}
static void worker(void *p) { (void)p; for (;;) taskWork++; }   /* 예: 제어 루프 대신 계산 일 */
static void report(void *p) { (void)p;
    if (RATE) {
        TIMER_RELOAD(TIMER0_BASE) = BOARD_CPU_HZ / (RATE ? RATE : 1);
        TIMER_VALUE(TIMER0_BASE) = BOARD_CPU_HZ / (RATE ? RATE : 1);
        NVIC_IPR(8) = 0x40;                    /* kernel-aware 우선순위 */
        NVIC_ISER0 = 1u << 8;
        TIMER_CTRL(TIMER0_BASE) = 0x9;         /* EN | IRQ EN */
    }
    uint32_t w0 = taskWork, i0 = isrCnt, u0 = board_now_us();
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t w = taskWork - w0, n = isrCnt - i0, us = board_now_us() - u0;
    board_printf_raw("RATE=%u Hz isr=%u taskWork=%u  500 ticks took %u us\n", (unsigned)RATE,
                     (unsigned)n, (unsigned)w, (unsigned)us);
    board_exit(0); }
int main(void) {
    xTaskCreate(worker, "worker", 256, NULL, 1, NULL);
    xTaskCreate(report, "report", 256, NULL, 5, NULL);
    vTaskStartScheduler(); for (;;) {}
}
```

```sh
for r in 0 5000 10000 20000 30000 40000 45000 48000 49000 50000 55000; do
  make -s -B -f .../freertos-mps2/Makefile SRCS=.../ex14.c OUT=/tmp/rtos_D9/ex14 EXTRA_CFLAGS="-DRATE=$r"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D9/ex14/app.elf 30 | grep RATE || echo "RATE=$r (no report within 30 s)"
done
```

```text
RATE=0 Hz isr=0 taskWork=3900336  500 ticks took 499993 us
RATE=5000 Hz isr=2499 taskWork=3503901  500 ticks took 499992 us
RATE=10000 Hz isr=4997 taskWork=3107629  500 ticks took 499992 us
RATE=20000 Hz isr=9991 taskWork=2315407  500 ticks took 499992 us
RATE=30000 Hz isr=14987 taskWork=1523270  500 ticks took 499993 us
RATE=40000 Hz isr=19968 taskWork=733464  500 ticks took 500021 us
RATE=45000 Hz isr=22484 taskWork=337211  500 ticks took 500065 us
RATE=48000 Hz isr=24000 taskWork=94878  500 ticks took 500201 us
RATE=49000 Hz isr=24479 taskWork=35457  500 ticks took 500371 us
RATE=50000 Hz isr=30337 taskWork=115  500 ticks took 611981 us
qemu-system-arm: terminating on signal 15 from pid 584881 (timeout)
RATE=55000 (no report within 30 s)
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">TIMER0 인터럽트율 vs 태스크 진척 (ISR 1회 20 µs, 500 tick, QEMU icount)</text>
<line x1="60" y1="240" x2="620" y2="240" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="240" stroke="currentColor"/>
<line x1="56" y1="240.0" x2="620" y2="240.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="244.0" font-size="12" text-anchor="end">0%</text>
<line x1="56" y1="190.0" x2="620" y2="190.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="194.0" font-size="12" text-anchor="end">25%</text>
<line x1="56" y1="140.0" x2="620" y2="140.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="144.0" font-size="12" text-anchor="end">50%</text>
<line x1="56" y1="90.0" x2="620" y2="90.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="94.0" font-size="12" text-anchor="end">75%</text>
<line x1="56" y1="40.0" x2="620" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="44.0" font-size="12" text-anchor="end">100%</text>
<text x="60.0" y="258" font-size="12" text-anchor="middle">0k</text>
<text x="153.3" y="258" font-size="12" text-anchor="middle">10k</text>
<text x="246.7" y="258" font-size="12" text-anchor="middle">20k</text>
<text x="340.0" y="258" font-size="12" text-anchor="middle">30k</text>
<text x="433.3" y="258" font-size="12" text-anchor="middle">40k</text>
<text x="526.7" y="258" font-size="12" text-anchor="middle">50k</text>
<text x="620.0" y="258" font-size="12" text-anchor="middle">60k</text>
<line x1="60.0" y1="40.0" x2="526.7" y2="240.0" stroke="#888" stroke-dasharray="5 4"/>
<polyline points="60.0,40.0 106.7,60.3 153.3,80.6 246.7,121.3 340.0,161.9 433.3,202.4 480.0,222.7 508.0,235.1 517.3,238.2 526.7,240.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="60.0" cy="40.0" r="4" fill="#e08a3c"/>
<circle cx="106.7" cy="60.3" r="4" fill="#e08a3c"/>
<circle cx="153.3" cy="80.6" r="4" fill="#e08a3c"/>
<circle cx="246.7" cy="121.3" r="4" fill="#e08a3c"/>
<circle cx="340.0" cy="161.9" r="4" fill="#e08a3c"/>
<circle cx="433.3" cy="202.4" r="4" fill="#e08a3c"/>
<circle cx="480.0" cy="222.7" r="4" fill="#e08a3c"/>
<circle cx="508.0" cy="235.1" r="4" fill="#e08a3c"/>
<circle cx="517.3" cy="238.2" r="4" fill="#e08a3c"/>
<circle cx="526.7" cy="240.0" r="4" fill="#e08a3c"/>
<line x1="573.3" y1="40" x2="573.3" y2="240" stroke="#d0564a" stroke-dasharray="3 3"/>
<text x="567.3" y="54" font-size="12" text-anchor="end">55k: tick도 멈춤</text>
<text x="567.3" y="70" font-size="12" text-anchor="end">30 s 동안 출력 없음</text>
<text x="78.7" y="200.0" font-size="12">점선 = 100% − ISR 부하(20 µs × rate)</text>
<text x="340" y="280" font-size="12" text-anchor="middle">인터럽트율 (Hz)</text>
<text x="14" y="140" font-size="12" transform="rotate(-90 14 140)" text-anchor="middle">태스크 진척 (0 Hz 대비)</text>
<text x="70" y="302" font-size="12">48k: 2.4%  ·  49k: 0.9%  ·  50k: 0.003% (500 tick이 612 ms로 늘어남 — SysTick도 밀림)</text>
</svg>
```
그림 12 — 태스크 진척은 "100% − ISR 부하"(점선)보다 조금 아래에 있다(ISR 진입·복귀 비용). 50 kHz(부하 100%)에서 진척은 0.003%, 55 kHz에서는 아예 보고가 안 나온다.

출력에서 볼 것:

- 40 kHz에서 ISR 부하는 80%인데 태스크 진척은 18.8%다. 나머지 1.2%p는 ISR 진입·복귀와 문맥 저장 비용이다(QEMU는 예외 스태킹 사이클을 모델링하지 않으므로 실제 칩에서는 이 차이가 더 크다).
- 48 kHz부터 "500 ticks took"가 500 ms를 넘기 시작한다. SysTick(가장 낮은 우선순위 255)이 TIMER0(0x40)에 밀려 **tick 처리 자체가 늦어진다**. 50 kHz에서는 500 tick에 612 ms — RTOS의 시간이 실제 시간보다 22% 느리게 간다. 타임아웃·주기 태스크가 전부 늘어난다.
- 55 kHz(부하 110%)에서는 30초 동안 아무 출력이 없다. ISR이 끝나기 전에 다음 인터럽트가 pending되어 SysTick이 한 번도 안 돌고, tick이 멈추니 `vTaskDelay(500)`이 영원히 안 끝난다. 밖에서 보면 데드락과 똑같이 "멈춤"이지만 CPU는 100% ISR이다. isr 카운터만 계속 오른다.
- 덤으로 50 kHz의 `isr=30337`은 612 ms 동안 49.6 kHz로, 인터럽트를 **잃기 시작했다**는 뜻이다. NVIC pending 비트는 하나라서 처리 전에 두 번 온 인터럽트는 한 번으로 합쳐진다.

방어: ISR은 최소한만 하고 나머지는 태스크로 미룬다(E4), ISR 안에서 "이번 ms에 N회 넘으면 그 소스를 마스크하고 태스크에 폴링을 맡긴다"는 예산(budget)을 둔다, 고장 난 센서의 인터럽트 라인이 떠서 폭주하는 경우를 대비해 인터럽트율 카운터를 텔레메트리로 낸다. 감시 측면에서는 HW watchdog을 **태스크 heartbeat로만** 차는 §5.3 구조가 이 경우도 잡는다(태스크가 못 도니 kick이 없다). ISR이나 SysTick hook에서 kick하면 못 잡는다.

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나

데드락 탐지에 필요한 두 간선 — "누가 무엇을 기다리나"와 "누가 무엇을 쥐었나" — 은 커널 자료구조 안에 이미 있다. FreeRTOS는 그걸 밖으로 내주지 않고, Zephyr는 구조체 필드로 그대로 보인다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">FreeRTOS V11.3.2 — logger가 mSPI를 기다리는 순간</text>
<g transform="translate(0,20)">
<rect x="10" y="32" width="210" height="110" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="50" font-size="12" font-weight="bold">TCB_t (logger)</text>
<text x="20" y="72" font-size="12">xStateListItem</text>
<text x="20" y="92" font-size="12">xEventListItem</text>
<text x="20" y="112" font-size="12">uxPriority 3 / uxBasePriority 2</text>
<text x="20" y="132" font-size="12">uxMutexesHeld 1</text>
<rect x="270" y="32" width="200" height="110" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="280" y="50" font-size="12" font-weight="bold">Queue_t (mSPI)</text>
<text x="280" y="72" font-size="12">uxQueueType = MUTEX</text>
<text x="280" y="92" font-size="12">xTasksWaitingToReceive</text>
<text x="280" y="112" font-size="12">u.xSemaphore.xMutexHolder</text>
<text x="280" y="132" font-size="12">uxMessagesWaiting 0</text>
<rect x="510" y="32" width="160" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="520" y="50" font-size="12">xSuspendedTaskList</text>
<text x="520" y="68" font-size="12">(portMAX_DELAY 대기)</text>
<rect x="510" y="98" width="160" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="520" y="116" font-size="12" font-weight="bold">TCB_t (imu)</text>
<text x="520" y="134" font-size="12">xEventListItem → mSD</text>
<path d="M120,68 C200,10 440,10 506,50" fill="none" stroke="#888" marker-end="url(#ah5)"/>
<text x="330" y="16" font-size="12" text-anchor="middle">pvContainer</text>
<line x1="125" y1="88" x2="272" y2="88" stroke="#d0564a" marker-end="url(#ah5)"/>
<text x="195" y="82" font-size="12" text-anchor="middle">pvContainer</text>
<line x1="460" y1="108" x2="506" y2="116" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="10" y="166" font-size="12">기다림 = xEventListItem의 컨테이너 (공개 API 없음) · 소유 = xMutexHolder (xSemaphoreGetMutexHolder)</text>
<line x1="10" y1="180" x2="670" y2="180" stroke="#888" stroke-dasharray="4 3"/>
<text x="10" y="202" font-size="13" font-weight="bold">Zephyr v4.4.2 — 같은 순간</text>
<rect x="10" y="214" width="210" height="80" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="232" font-size="12" font-weight="bold">struct k_thread (logger)</text>
<text x="20" y="254" font-size="12">base.pended_on</text>
<text x="20" y="274" font-size="12">base.prio 5 (원래 6)</text>
<rect x="270" y="214" width="200" height="100" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="280" y="232" font-size="12" font-weight="bold">struct k_mutex (mSPI)</text>
<text x="280" y="254" font-size="12">wait_q (첫 멤버)</text>
<text x="280" y="274" font-size="12">owner</text>
<text x="280" y="294" font-size="12">lock_count, owner_orig_prio</text>
<rect x="510" y="250" width="160" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="520" y="268" font-size="12" font-weight="bold">struct k_thread (imu)</text>
<text x="520" y="286" font-size="12">base.pended_on → mSD</text>
<line x1="125" y1="250" x2="272" y2="250" stroke="#d0564a" marker-end="url(#ah5)"/>
<line x1="330" y1="270" x2="506" y2="270" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="10" y="322" font-size="12">기다림 = pended_on == &amp;mutex-&gt;wait_q · 소유 = mutex-&gt;owner — 둘 다 그냥 읽으면 된다</text>
</g>
</svg>
```
그림 13 — 대기 그래프의 두 간선이 커널 안 어디에 있나. FreeRTOS는 리스트 항목의 컨테이너 포인터가 "기다림"을 뜻하고 그 리스트가 Queue_t 안에 있다. Zephyr는 `pended_on`이 뮤텍스의 첫 멤버 `wait_q`를 가리킨다.

### 8.1 소스 위치 (직접 grep해서 확인)

| 개념 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| 블록할 때 대기 리스트에 걸기 | `tasks.c` `vTaskPlaceOnEventList()` → `vListInsert(pxEventList, &pxCurrentTCB->xEventListItem)` (우선순위 정렬) | `kernel/sched.c` `add_to_waitq_locked()`: `thread->base.pended_on = wait_q; _priq_wait_add(...)` |
| 무한 대기의 상태 리스트 | `prvAddCurrentTaskToDelayedList()`: `portMAX_DELAY`이면 `xSuspendedTaskList` | 타임아웃 없음 = 타이머 미등록 |
| 상태 판정 | `eTaskGetState()`: suspended 리스트 + 이벤트 리스트 걸림 → `eBlocked` | `k_thread_state_str()` → `pending` |
| 뮤텍스 소유자 | `queue.c` `Queue_t.u.xSemaphore.xMutexHolder`, 읽기 `xQueueGetMutexHolder()` (`INCLUDE_xSemaphoreGetMutexHolder`) | `struct k_mutex.owner` (`include/zephyr/kernel.h`) |
| 재귀 | 일반 뮤텍스는 재귀 아님, 재귀 뮤텍스 `uxRecursiveCallCount` | `kernel/mutex.c` `lock_count`, owner == `_current`이면 성공 |
| 남이 풀기 | 일반: `xTaskPriorityDisinherit()`의 `configASSERT( pxTCB == pxCurrentTCB )` (tasks.c:6861) / 재귀: `xQueueGiveMutexRecursive()`가 `pdFAIL` | `k_mutex_unlock()`: `CHECKIF(mutex->owner != _current)` → `-EPERM` |
| 스케줄러 정지 중 블록 | `xQueueSemaphoreTake()` 첫머리 `configASSERT(... taskSCHEDULER_SUSPENDED ... xTicksToWait != 0)` (queue.c:1682) | 이 노트에서 확인하지 않음 (`k_sched_lock`은 C7) |
| 크리티컬 섹션 안 블록 | 감지 없음 → `vListInsert` 무한 루프 (`list.c` 주석이 원인 목록을 적어 둠) | — |
| 삭제된 태스크 회수 | 자기 삭제 → `xTasksWaitingTermination` → idle의 `prvCheckTasksWaitingTermination()` | 스택은 앱 소유 (정적) |
| 리스트 항목 필드 이름 | 소스 `pxContainer`, 바이너리 `pvContainer` (`FreeRTOS.h` 하위 호환 `#define`) | — |


### 8.2 왜 FreeRTOS는 "누가 무엇을 기다리나" API를 안 주나

`TCB_t`와 `Queue_t`는 의도적으로 불투명(opaque)하다. 공개 API가 있으면 커널이 그 정보를 일관되게 유지할 책임이 생기는데, 대기 대상은 큐·세마포어·이벤트 그룹·스트림 버퍼·태스크 통지마다 표현이 달라서(통지는 리스트가 아니라 `ucNotifyState` 플래그) 하나의 API로 묶기 어렵다. 그래서 FreeRTOS 생태계에서는 이 정보를 **RTOS-aware 디버거**(커널 심볼을 직접 읽는 IDE 플러그인, SEGGER SystemView 같은 트레이스)로 본다(M 모듈). 런타임 탐지가 필요하면 ex6처럼 **래퍼가 직접 기록**하는 것이 표준이다.

---

## 9. 실무에서 다시 보기

### 9.1 드론 FC — 로거 · IMU · SPI 버스

§2의 시나리오는 실제로 흔하다. STM32H7 FC에서 SD 카드와 IMU가 같은 SPI에 붙어 있고(보드 면적 때문에), 로거와 IMU 드라이버를 다른 사람이 짰다. 고치는 순서는 이렇다.

1. **즉시**: 순위표(§4.1)를 만들고 `lk_take` 래퍼로 바꾼다. CI의 HIL 비행 시뮬레이션에서 `LOCK_ORDER_CHECK`를 켠다. 위반은 첫 이상치에서 assert로 잡힌다.
2. **설계**: SPI 버스를 게이트키퍼 태스크로 바꾼다. IMU 읽기는 DMA로 하고, 로그는 "로그 큐에 넣기"로만 남긴다(IMU 태스크는 어떤 락도 쥔 채 로그를 쓰지 않는다).
3. **보험**: 모든 take에 유한 타임아웃 + 카운터, 태스크별 heartbeat → HW watchdog. watchdog 리셋 전 NMI에서 모터 출력을 failsafe 값으로 쓰고 `.noinit`에 대기 그래프 한 줄을 남긴다. 다음 부팅에서 MAVLink STATUSTEXT로 지상국에 보고한다.

### 9.2 로봇 관절 모터 컨트롤러 — CAN과 인터럽트 폭주

20 kHz FOC 전류 루프는 zero-latency ISR에서 돌고(E2), CAN 송신은 태스크 두 개(상태 보고, 진단 응답)가 `mCanTx`를 나눠 쓴다. 여기서 실제로 나는 사고는 데드락보다 **인터럽트 라이브락**이다. 버스에 노이즈가 생겨 CAN 에러 인터럽트가 수십 kHz로 들어오면 §7.2처럼 태스크가 굶고, tick이 늦어지고, heartbeat가 끊겨 리셋된다. 방어는 CAN 에러 인터럽트에 예산을 두고(초과하면 에러 인터럽트를 끄고 태스크에서 에러 카운터를 폴링), bus-off 복구는 태스크에서 하는 것이다(J6). 리셋 원인 기록에 "인터럽트별 횟수 상위 3개"를 넣어 두면 필드에서 바로 구별된다.

### 9.3 Don의 SSD 펌웨어 — 채널 락 순서와 버퍼 풀 고갈

SSD 펌웨어는 RTOS 데드락 패턴의 보물창고다.

- **NAND 채널 락 순서**: 멀티플레인·멀티채널 쓰기는 여러 채널 락을 동시에 잡는다. 규칙은 "항상 채널 번호 오름차순"이다 — §4.1 순위표를 채널 번호로 자동 생성한 것과 같다. GC가 "소스 블록 채널 → 목적지 블록 채널" 순으로 잡는 코드가 있으면 호스트 쓰기 경로와 AB/BA가 난다.
- **버퍼 풀 고갈 데드락**: 호스트 쓰기가 버퍼를 다 차지하고 GC를 기다리는데, GC는 데이터를 옮기려고 버퍼 하나가 필요하다. 락이 아니라 **카운팅 자원**(카운팅 세마포어)의 순환이다(§3 계열). 표준 해법은 GC 전용 예약 버퍼(reserve pool) — 자원을 분리해서 상호 배제 조건을 깬다.
- **FTL 맵 캐시 락 ↔ NAND 읽기 완료 대기**: 맵 캐시 락을 쥔 채 맵 페이지 NAND 읽기 완료를 기다리면, 그 완료를 처리할 태스크가 같은 락을 원하는 순간 멈춘다. "락을 쥔 채 I/O 완료를 기다리지 않는다"는 §6.4의 교훈과 같다.

Don이 SSD에서 JTAG으로 하던 "각 코어가 무엇을 쥐고 무엇을 기다리나" 분석을, RTOS에서는 §2.3의 gdb 스크립트나 §5.2의 모니터가 대신한다.

### 9.4 "가끔 멈춤" 분류표

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">멈춘 장비 앞에서 — 무엇부터 보나</text>
<rect x="250" y="32" width="180" height="32" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="53" font-size="12" text-anchor="middle">멈춤 · watchdog 리셋</text>
<rect x="250" y="84" width="180" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="105" font-size="12" text-anchor="middle">tick이 증가하나?</text>
<line x1="340" y1="64" x2="340" y2="82" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="470" y="78" width="200" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="570" y="96" font-size="12" text-anchor="middle">아니오: ISR 폭주 (§7.2)</text>
<text x="570" y="113" font-size="12" text-anchor="middle">크리티컬 섹션 안 블록 (§3.2)</text>
<line x1="430" y1="100" x2="468" y2="100" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="250" y="140" width="180" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="161" font-size="12" text-anchor="middle">멈춘 태스크의 상태는?</text>
<line x1="340" y1="116" x2="340" y2="138" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="348" y="132" font-size="12">예</text>
<rect x="20" y="200" width="200" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="221" font-size="12" text-anchor="middle">Blocked: 대기 그래프 순환?</text>
<rect x="250" y="200" width="180" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="218" font-size="12" text-anchor="middle">Ready인데 안 돎</text>
<text x="340" y="235" font-size="12" text-anchor="middle">→ 기아 (§6)</text>
<rect x="460" y="200" width="210" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="565" y="218" font-size="12" text-anchor="middle">Running · 시도만 증가</text>
<text x="565" y="235" font-size="12" text-anchor="middle">→ 라이브락 (§7.1)</text>
<line x1="300" y1="172" x2="140" y2="198" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="172" x2="340" y2="198" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="380" y1="172" x2="540" y2="198" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="10" y="270" width="150" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="288" font-size="12" text-anchor="middle">예: 데드락</text>
<text x="85" y="305" font-size="12" text-anchor="middle">순위표 위반 찾기</text>
<rect x="175" y="270" width="230" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="290" y="288" font-size="12" text-anchor="middle">아니오: 줄 사람 없는 신호</text>
<text x="290" y="305" font-size="12" text-anchor="middle">큐 순환 · 타이머 데몬 (§3.1)</text>
<line x1="90" y1="232" x2="85" y2="268" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="150" y1="232" x2="260" y2="268" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="10" y="334" font-size="12">덤프에 필요한 것: 태스크 상태 · 현재/기본 우선순위 · 대기 대상 · 뮤텍스 소유자 · 진척/시도/ISR 카운터</text>
</svg>
```
그림 14 — 멈춘 장비를 분류하는 순서. 먼저 시간이 흐르는지(tick), 그다음 멈춘 태스크의 상태, Blocked라면 대기 그래프. 이걸 하려면 **멈추기 전에** 덤프와 카운터를 심어 두어야 한다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 드문 에러·로그 경로에서 락 순서 뒤집기 | 수 초~수 시간 뒤 "가끔" 멈춤, 재현 안 됨 | 순환 대기 (§2) | 순위표 + `lk_take` 검사기를 CI에서 켜기 (§4.2) |
| 모든 take를 `portMAX_DELAY`로 | 멈췄는데 아무 로그도 없음 | 탐지 장치 없음 | 유한 타임아웃 + 카운터 + 로그 (§5.1) |
| 타이머 콜백에서 블록 (`xSemaphoreTake(m, portMAX_DELAY)`) | 모든 SW 타이머가 동시에 멈춤, Tmr Svc가 Blocked | 데몬 하나가 막힘 (§3.1 MODE 3) | 콜백은 블록 시간 0, 일은 태스크로 넘기기 (D6, D7) |
| 크리티컬 섹션 안에서 블로킹 API | 출력 없이 멈춤, gdb로 보면 `vListInsert` 무한 루프, tick 0 | PendSV·SysTick이 BASEPRI에 막힘 (§3.2) | 크리티컬 섹션은 수 µs 계산만. API 호출 금지 |
| `vTaskSuspendAll()` 중 블로킹 API | `configASSERT` queue.c:1682 | 스케줄러 정지 중 블록 (§3.2) | `configASSERT`를 반드시 정의, 정지 구간에서 API 금지 |
| Zephyr 코드를 FreeRTOS로 옮기며 중첩 잠금 유지 | 그 경로에서만 영원히 멈춤 | `k_mutex`는 재귀, FreeRTOS 일반 뮤텍스는 아님 (§3.3) | 재귀 뮤텍스 또는 중첩 제거, 검사기가 재진입도 잡게 |
| watchdog을 idle hook·타이머 콜백에서 kick | 태스크가 데드락인데 리셋 안 됨 | kick이 "모두 살아 있음"을 증명하지 않음 | 태스크별 heartbeat → supervisor가 모두 확인 후 kick (§5.3) |
| heartbeat를 `xEventGroupWaitBits(... clear on exit ...)`로 | 원인 기록에 멈춘 태스크가 빠짐 | 타임아웃이면 비트를 안 지움 (§5.3) | 창마다 `xEventGroupClearBits()`로 읽고 지우기 |
| 블록하지 않는 prio ≥ 1 배경 태스크 + `vTaskDelete(NULL)` | heap이 점점 줄다 `MallocFailedHook` | idle 기아 → TCB 회수 안 됨 (§6.3) | 배경 태스크도 주기적으로 블록, 동적 생성·삭제 대신 태스크 풀 |
| 대칭 try-lock 재시도 | CPU 100%, 일 0건, 아무도 Blocked 아님 | 라이브락 (§7.1) | 순위표로 try-lock 제거, 아니면 블록하는 백오프 |
| ISR 안에서 할 일을 다 함 | 부하가 오르면 tick이 늦어지고 결국 멈춤 | 인터럽트 라이브락 (§7.2) | ISR 최소화 + 예산, 처리는 태스크로 (E4) |

---

## 11. 면접에서 이렇게 말한다

**Q.** What are the four conditions for deadlock, and which one do you usually break in firmware?

**A.** 상호 배제, 쥔 채 기다림, 뺏을 수 없음, 순환 대기 네 개가 동시에 성립해야 한다. 펌웨어에서는 순환 대기를 깬다. 모든 락에 순위를 매기고 순위가 올라가는 방향으로만 잡게 하고, 그걸 래퍼에서 런타임에 검사한다. 데드락은 확률 사건이지만 순서 위반은 실행만 되면 잡힌다.

> Mutual exclusion, hold-and-wait, no preemption, and circular wait — all four must hold at once. In firmware I break circular wait: every lock gets a rank in one header, you may only acquire locks in increasing rank, and a thin wrapper around take checks the rank against the top of a per-task held-lock stack and asserts on violation. That turns a probabilistic hang into a deterministic assert — in my QEMU repro the deadlock took anywhere from 0.2 to 47 seconds depending on timing, while the checker fired 32 ms in, the first time the bad path ran.

**꼬리 질문**: "Does priority inheritance prevent deadlock?" → 아니다. 상속은 역전 시간만 줄인다. 덤프에서 상속이 일어난 채로 멈춘 것을 봤다. 데드락까지 막는 것은 priority ceiling(ICPP)이다.

**Q.** A drone in the field occasionally freezes and gets reset by the watchdog. How do you find out whether it's a deadlock?

**A.** 먼저 멈추기 전에 증거를 남기는 장치가 있어야 한다. 태스크별 heartbeat로만 HW watchdog을 차고, watchdog 1차 만료 NMI에서 어느 heartbeat가 빠졌는지, 태스크 상태와 뮤텍스 소유자를 `.noinit` RAM에 쓰고, 다음 부팅에서 보고한다. 분류는 tick이 흐르는지, 멈춘 태스크가 Blocked인지 Ready인지 Running인지, Blocked면 대기 그래프에 순환이 있는지 순서로 본다.

> I make sure the evidence survives the reset. The hardware watchdog is only kicked when every monitored task has set its heartbeat bit in the current window; on the first watchdog expiry the NMI records the missing heartbeats, each task's state, and the mutex holders into a no-init RAM section, and the next boot reports it. Then I triage: is the tick still advancing — if not, it's an interrupt storm or a blocking call inside a critical section. If the stuck tasks are Blocked, I walk the wait-for graph — task, the mutex it waits on, that mutex's holder — and a cycle means deadlock. Ready-but-not-running is starvation; running with only retry counters moving is livelock.

**Q.** How would you detect a deadlock at runtime in FreeRTOS?

**A.** 두 간선이 필요하다. 소유는 `xSemaphoreGetMutexHolder`로 읽고, 기다림은 커널이 TCB의 이벤트 리스트 항목으로만 갖고 있고 공개 API가 없으니 take 래퍼가 블록 직전에 기록한다. 높은 우선순위 모니터가 주기적으로 그래프를 따라가 순환을 찾되, 소유자 읽기가 순간값이라 두 번 연속 같을 때만 확정한다.

> I need both edges of the wait-for graph. The holder side comes from xSemaphoreGetMutexHolder. The waiting side the kernel only keeps internally — the task's xEventListItem sits in the mutex's xTasksWaitingToReceive list — with no public API, so my take wrapper records "waiting on X since tick T" in a per-task context stored in a thread-local-storage slot. A high-priority monitor walks task → lock → holder every 50 ms and declares a deadlock only if it sees the same cycle twice, because the holder read is a snapshot. In Zephyr it's simpler: thread->base.pended_on points at the mutex's wait_q and the mutex has an owner field.

**Q.** What's the difference between deadlock, livelock, and starvation?

**A.** 데드락은 모두 Blocked라 CPU가 놀고, 라이브락은 모두 실행 중인데 재시도만 반복해 일이 0건이고, 기아는 시스템은 진행하는데 특정 태스크만 못 돈다. 탐지 방법도 다르다 — 대기 그래프, 시도 대비 완료 비율, 태스크별 진척 카운터.

> Deadlock: everyone involved is blocked, the CPU is idle, and nothing will ever change. Livelock: everyone is running, the CPU is busy, but they keep backing off and retrying so no work completes — I reproduced two try-lock tasks doing zero work and about a thousand failed attempts in a second. Starvation: the system makes progress, but one task never gets its turn — a low-priority task under a CPU-bound higher one, or a low-priority waiter on a busy mutex, since FreeRTOS wait lists are priority-ordered, not FIFO. You detect them differently: a wait-for graph cycle, a completed-versus-attempted ratio, and per-task progress counters.

**Q.** Can you deadlock without any mutex?

**A.** 그렇다. 서로의 꽉 찬 큐에 보내려고 기다리는 두 태스크, 자기만 줄 수 있는 세마포어를 먼저 기다리는 태스크, 타이머 콜백이 뮤텍스에서 막혀 타이머 데몬이 서고 그 데몬이 실행할 다른 콜백을 기다리는 태스크. 크리티컬 섹션 안에서 블로킹 API를 부르면 assert도 없이 커널 리스트가 꼬여 멈춘다.

> Yes, and in RTOS code those are more common than AB/BA. Two tasks that each send a batch to the other's full queue before receiving; a task that takes a semaphore it is itself supposed to give later; a timer callback that blocks on a mutex, which stalls the timer daemon, so another callback that would give a semaphore never runs. And calling a blocking API inside taskENTER_CRITICAL on Cortex-M: PendSV and SysTick are masked, the task never switches out, re-inserts its own list item and spins forever in vListInsert with the tick stuck at zero — no assert catches it.

**Q.** Why might FreeRTOS run out of heap even though nothing leaks?

**A.** 스스로 `vTaskDelete(NULL)`한 태스크의 TCB와 스택은 idle 태스크가 해제한다. 블록하지 않는 prio 1 이상 태스크가 idle을 굶기면 회수가 멈춰 heap이 계속 준다. 측정해 보니 10 ms마다 태스크를 만들고 지우는 동안 100 ms에 11 KB씩 줄었고, idle이 20 ms 돌자 한 번에 회복했다.

> A task that deletes itself can't free its own stack while running on it, so FreeRTOS parks the TCB on xTasksWaitingTermination and the idle task frees it in prvCheckTasksWaitingTermination. If a never-blocking task above priority zero starves idle, that cleanup never happens. In my test, spawning a self-deleting worker every 10 ms lost 11,360 bytes of heap every 100 ms and the task count kept climbing; the moment the hog blocked for 20 ms, idle ran and the heap came back in one go. Deleting a task from another task frees it immediately, so the fix is to let background work block, or to avoid dynamic create/delete altogether.

**Q.** How do you pick timeouts for mutex takes?

**A.** 그 락의 최악 보유 시간을 재서 몇 배로 잡고, 제어 루프 안이면 그 주기보다 짧게 한다. 타임아웃은 에러로 다룬다 — 카운터, 로그, 필요하면 안전 상태. 조용히 재시도만 하면 라이브락을 숨긴다.

> I measure the worst-case hold time of that lock and use a small multiple of it, and if the take sits inside a control loop it has to be shorter than the loop period, otherwise the timeout fires after you've already missed many cycles. A timeout is an error, not a retry hint: it increments a per-lock counter that goes out in telemetry, logs the call site, and for safety functions transitions to a safe state. Silent retry loops are how you turn a deadlock into a livelock nobody notices.

**꼬리 질문**: "What does the timeout counter tell you that a hang doesn't?" → 어느 락이, 얼마나 자주, 어떤 비행 단계에서 막히는지. 멈춤은 한 번의 사건이지만 카운터는 분포를 준다.

---

## 12. 직접 해보기

1. (개념) 그림 6의 세 태스크 중 하나만 고쳐서 순환을 없애라. 순위표(SPI 20, I2C 30, SD 10)를 기준으로 어느 태스크가 규칙을 어겼고, 어떻게 바꾸면 되나?
   정답/힌트: imu(SPI 20 → I2C 30)와 logger(SD 10 → SPI 20)는 증가 방향이라 합법이다. 위반은 baro(I2C 30 → SD 10) 하나뿐이고, baro가 SD를 먼저 잡게(10 → 30) 바꾸면 순환이 깨진다.
2. (코드) `lockdbg.h`에 "락을 쥔 채 블록하는 API 호출"을 잡는 검사를 추가하라. 예: `lk_held_any()`를 만들고, 큐 send/receive 래퍼에서 `depth > 0`이면서 타임아웃이 0이 아니면 경고를 찍게.
   정답/힌트: §6.4의 자원 기아와 §3의 락이 아닌 데드락 상당수가 "락을 쥔 채 다른 것을 기다림"에서 나온다. 경고만 찍고 assert는 하지 말 것 — 의도된 경우도 있다.
3. (코드) ex6 모니터를 확장해 **큐 대기**도 잡아라. 각 태스크가 `waitSince`를 기록하니, 뮤텍스가 아닌 대기가 1초 넘게 이어지면 "오래 기다림" 경고를 찍으면 된다. ex3 MODE=1에 붙여 확인하라.
   정답/힌트: 큐는 소유자가 없어서 순환 판정 대신 "나이(age)" 판정을 쓴다.
4. (측정) ex9에서 높은 태스크 주기를 10 ms → 1 ms로 바꾸고 같은 duty로 돌려 maxGapUs가 어떻게 변하는지 재라. 진척 비율은 그대로인데 최장 공백은 어떻게 되나?
   정답/힌트: 진척은 duty만의 함수, 최장 공백은 duty × 주기. heartbeat 감시 창 설계에 그대로 쓰인다.
5. (측정) ex13 MODE=1의 무작위 백오프 범위를 20~200 µs, 20~800 µs, 20~2000 µs로 바꿔 done을 재고, 왜 범위가 tick(1 ms)보다 커야 효과가 큰지 설명하라.
   정답/힌트: 단일 코어에서는 상대가 "내가 놓은 순간에 실행 중"이어야 락을 가져간다. 백오프가 tick 경계를 넘어야 그 기회가 생긴다.
6. (측정) ex14에 ISR 예산을 넣어라: 1 ms 동안 인터럽트가 30회를 넘으면 TIMER0 인터럽트를 끄고(`NVIC_ICER0`) 태스크에서 1 ms 뒤 다시 켠다. 50 kHz와 55 kHz에서 taskWork와 "500 ticks took"이 어떻게 바뀌나?
   정답/힌트: 처리량 일부를 버리는 대신 tick과 태스크 진척이 보장된다. Linux NAPI의 인터럽트 → 폴링 전환과 같은 생각이다.
7. (Zephyr) z1에 세 번째 스레드와 뮤텍스를 추가해 3-순환을 만들고, `main`의 그래프 따라가기가 길이 3 순환을 찾는지 확인하라. `k_thread_foreach` 대신 `k_thread_foreach_unlocked`를 쓰면 무엇이 달라지나?
   정답/힌트: `_unlocked` 버전은 목록 원소를 읽을 때만 `k_spin_lock`을 잡고 **콜백 동안은 풀어 둔다**. 그래서 콜백에서 오래 걸리는 일을 해도 되지만, 순회 중 생성·종료된 스레드가 빠지거나 끼는 경합이 있다(include/zephyr/kernel.h 주석).

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| deadlock | 교착 상태 | 서로가 쥔 것을 기다려 아무도 영원히 진행하지 못함 |
| Coffman conditions | 데드락 네 조건 | 상호 배제 · 쥔 채 기다림 · 뺏을 수 없음 · 순환 대기 |
| wait-for graph | 대기 그래프 | 태스크 → 기다리는 자원 → 소유자 화살표. 순환 = 데드락 |
| lock ordering / rank | 락 순서 · 순위 | 순위가 증가하는 방향으로만 잡는 규칙 |
| lockdep | Linux 락 검사기 | 락 의존 그래프로 일어나지 않은 데드락까지 경고 (EL F4) |
| starvation | 기아 | 시스템은 진행하는데 특정 태스크만 차례가 안 옴 |
| livelock | 라이브락 | 모두 실행 중인데 양보·재시도만 반복해 일이 0건 |
| interrupt storm | 인터럽트 폭주 | 인터럽트율 × ISR 시간이 CPU를 다 먹는 상태 |
| try-lock | 시도 잠금 | 기다리지 않고 한 번 시도 (`xSemaphoreTake(m, 0)`, `K_NO_WAIT`) |
| backoff | 물러남 | 실패 후 잠시 쉬었다 재시도. 대칭이면 라이브락 |
| gatekeeper | 게이트키퍼 | 자원을 한 태스크만 만지고 나머지는 큐로 요청 (D7) |
| ICPP | Immediate Ceiling Priority Protocol | 락을 잡는 즉시 ceiling으로 우선순위 상승. 데드락도 막음 (D3) |
| heartbeat | 생존 신호 | 태스크가 루프마다 세우는 비트. supervisor가 확인 |
| watchdog (HW) | 감시 타이머 | 제때 kick하지 않으면 NMI·리셋 (CMSDK 0x40008000) |
| `.noinit` | 비초기화 섹션 | 리셋 후에도 0으로 지워지지 않는 RAM. 원인 기록용 |
| `xEventListItem` | FreeRTOS TCB 필드 | 블록할 때 대기 대상 큐의 리스트에 꽂히는 항목 |
| `xMutexHolder` | FreeRTOS Queue_t 필드 | 뮤텍스 소유 TCB. `xSemaphoreGetMutexHolder()`로 읽음 |
| `pended_on` | Zephyr `k_thread.base` 필드 | 블록된 스레드가 매달린 `_wait_q_t` |
| `xTasksWaitingTermination` | FreeRTOS 내부 리스트 | 스스로 삭제한 태스크가 idle의 회수를 기다리는 곳 |
| TLS pointer | 태스크 로컬 저장소 | `vTaskSetThreadLocalStoragePointer` — 태스크별 디버그 정보 보관 |

---

## 14. 요약 & 체크리스트

데드락은 네 조건(상호 배제·쥔 채 기다림·뺏을 수 없음·순환 대기)이 동시에 성립할 때 생기고, 단일 코어 RTOS에서도 선점과 블로킹만으로 충분히 생긴다. QEMU에서 같은 AB/BA 코드가 타이밍에 따라 0.2초에서 47초 사이에 멈췄다 — 테스트로는 "없음"을 증명할 수 없다. 그래서 예방은 **전역 락 순위표 + take 래퍼의 런타임 순서 검사**(첫 위반 32 ms에 assert, 비용 take+give 151 → 190 눈금)가 기본이고, 탐지는 **모든 take의 유한 타임아웃 + 카운터**, **대기 그래프 모니터**(FreeRTOS는 소유자 API + 래퍼 기록, Zephyr는 `pended_on` + `owner`), **태스크별 heartbeat로만 차는 HW watchdog + NMI에서 `.noinit` 기록**을 겹친다. RTOS에서는 락이 아닌 데드락(큐 순환, 혼자만 줄 세마포어, 타이머 데몬 막기, 크리티컬 섹션 안 블록 → `vListInsert` 무한 루프)이 더 흔하다. 기아는 고정 우선순위의 본성이며(진척 = 남은 CPU, 최장 공백 = 높은 태스크의 한 번 점유 시간), FreeRTOS에서는 idle 기아가 자기 삭제 태스크의 메모리 회수를 멈추고, 뮤텍스 대기 리스트가 우선순위 순이라 낮은 대기자가 굶는다. 라이브락은 대칭 try-lock 재시도(1초 0건)와 인터럽트 폭주(부하 100%에서 tick까지 멈춤)로 나타나며, 블록하는 백오프와 ISR 예산으로 막는다.

- [ ] Coffman 네 조건을 말하고 각각을 깨는 펌웨어 기법을 하나씩 댈 수 있다
- [ ] AB/BA 데드락 후 `uxTaskGetSystemState` 덤프에서 상속된 우선순위(cur/base)를 읽고 원인을 설명할 수 있다
- [ ] FreeRTOS에서 "기다림" 정보가 `xEventListItem`의 컨테이너에, "소유"가 `xMutexHolder`에 있다는 것과 gdb로 읽는 법을 안다
- [ ] Zephyr `pended_on == &mutex->wait_q`와 `k_mutex.owner`로 대기 그래프를 따라갈 수 있다
- [ ] 큐 순환·자기 세마포어·타이머 데몬·스케줄러 정지·크리티컬 섹션 블록의 증상을 구별할 수 있다
- [ ] FreeRTOS 일반 뮤텍스(재귀 아님, 남이 give하면 assert)와 Zephyr `k_mutex`(재귀, `-EPERM`)의 차이를 안다
- [ ] 락 순위표와 런타임 순서 검사기를 직접 짤 수 있다
- [ ] 타임아웃 값을 최악 보유 시간과 제어 주기로 정하고, 타임아웃을 에러로 다룬다
- [ ] heartbeat → supervisor → HW watchdog 구조와 이벤트 그룹 "묵은 비트" 함정을 설명할 수 있다
- [ ] idle 기아가 FreeRTOS에서 메모리 고갈로 나타나는 이유(자기 삭제 → idle 회수)를 설명할 수 있다
- [ ] 라이브락을 진척/시도 카운터로 구별하고, 단일 코어에서는 블록하는 백오프가 정답인 이유를 말할 수 있다
- [ ] 인터럽트율 × ISR 시간으로 태스크 진척을 예측하고, 100% 근처에서 tick까지 멈추는 현상을 설명할 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `tasks.c`(`vTaskPlaceOnEventList`, `prvAddCurrentTaskToDelayedList`, `eTaskGetState`, `vTaskDelete`, `prvCheckTasksWaitingTermination`, `xTaskPriorityDisinherit`, `taskSELECT_HIGHEST_PRIORITY_TASK`), `queue.c`(`Queue_t`, `xQueueGetMutexHolder`, `xQueueSemaphoreTake`, `xQueueGiveMutexRecursive`), `list.c`(`vListInsert` 주석), `event_groups.c`(`xEventGroupWaitBits` 타임아웃 경로), `include/FreeRTOS.h`(하위 호환 `#define pxContainer pvContainer`) — 랩 `/var/rtoslab/src/FreeRTOS-Kernel`
- Zephyr v4.4.2 소스: `include/zephyr/kernel/thread.h`(`pended_on`), `include/zephyr/kernel.h`(`struct k_mutex`, `k_thread_foreach`, `k_thread_state_str`, `k_thread_stack_free`), `kernel/sched.c`(`add_to_waitq_locked`), `kernel/mutex.c`(재귀 잠금, `-EPERM`), `subsys/debug/thread_analyzer/Kconfig` — 랩 `/var/rtoslab/zephyrproject/zephyr`
- FreeRTOS 문서: [configASSERT 설명 (Customization 페이지)](https://www.freertos.org/a00110.html), [FAQ](https://www.freertos.org/Why-FreeRTOS/FAQs) — `list.c` 주석이 직접 가리키는 두 페이지
- Zephyr 문서: [Mutexes](https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html), [Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html)
- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", ACM Computing Surveys 3(2), 1971 — 네 조건의 원전
- Arm, Cortex-M System Design Kit Technical Reference Manual — CMSDK APB watchdog(1차 만료 인터럽트, 2차 만료 리셋, LOCK 레지스터 `0x1ACCE551`)
- QEMU 소스 `hw/arm/mps2.c` · `hw/watchdog/cmsdk-apb-watchdog.c` — AN385의 watchdog 배치(이 노트에서는 `info mtree` 출력으로 `0x40008000` 확인)
- Don의 다른 노트(링크 없이 이름만): OSTEP 락 · 동시성 버그 장 노트, Verkada 동시성 노트(mutex), EL F4(lockdep §13), EL A8(유저 공간 동기화)
