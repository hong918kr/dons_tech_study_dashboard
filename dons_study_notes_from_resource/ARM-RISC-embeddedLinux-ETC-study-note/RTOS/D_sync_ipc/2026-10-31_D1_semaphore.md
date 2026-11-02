# D1. 세마포어 — ISR이 세우고 태스크가 내리는 깃발, N개를 세는 토큰 그릇

> **이 노트를 다 읽으면**: binary와 counting 세마포어를 "신호용"과 "자원 계수용"으로 나눠 고를 수 있다 · 빠른 IRQ에서 binary 세마포어가 이벤트를 몇 개 잃는지 측정하고 모델로 예측할 수 있다 · `xSemaphoreGiveFromISR` + `pxHigherPriorityTaskWoken` + `portYIELD_FROM_ISR`의 역할과, yield를 빼먹으면 지연이 최대 1 tick까지 늘어나는 이유를 소스로 설명할 수 있다 · FreeRTOS(`Queue_t`, `xQueueSemaphoreTake`, `cTxLock`)와 Zephyr(`struct k_sem`, `z_impl_k_sem_take`)의 세마포어 구현과 비용(RAM 84 B 대 16 B, give 2.98 µs 대 0.64 µs)을 숫자로 비교할 수 있다
> **왜 필요한가**: 면접 최빈출 질문 "mutex와 semaphore의 차이는?"에 "binary 세마포어는 값이 1인 mutex"라고 답하면 떨어진다. 실무에서는 "IMU 샘플이 가끔 빠진다", "ISR이 신호를 줬는데 태스크가 1 ms 늦게 돈다", "버퍼 풀이 가끔 바닥난다"의 원인이 세마포어 사용법에 있다
> **Don 기준 난이도**: ISR이 완료 플래그를 세우고 메인 루프가 내리는 패턴, DMA 버퍼 풀은 SSD 펌웨어에서 이미 안다 / 새로 배울 부분은 "플래그를 폴링하지 않고 커널에 맡기는" 블로킹 세마포어의 의미(대기 리스트, 깨울 순서, 토큰을 넘기는 방식), FromISR 규약, 그리고 FreeRTOS가 세마포어를 "항목 크기 0인 큐"로 만든 탓에 생기는 비용과 큐 잠금(`cTxLock`)
> **선행 노트**: A4(태스크 상태), A5(API 로제타석 — 세마포어 API 표와 "give 경로" 소스 추적은 거기서 했다), C1(이벤트 리스트·`xPendingReadyList`), C2(PendSV 컨텍스트 스위치), C7(크리티컬 섹션 — BASEPRI), EL F4 §6(Linux `struct semaphore`). 이어지는 노트: D2(뮤텍스), D3(우선순위 역전), D5(task notification — 더 가벼운 신호), E1·E2(ISR 설계와 우선순위 배치)

---

## 0. 큰 그림 — 깃발과 토큰

Don이 SSD 컨트롤러 펌웨어에서 매일 썼던 패턴부터 보자. NAND 읽기 DMA가 끝나면 완료 인터럽트가 뜨고, ISR은 명령 슬롯의 `done` 플래그를 1로 만든다. 메인 루프(executive)는 돌면서 플래그를 보고, 1이면 0으로 내리고 후처리를 한다. 이 패턴의 문제는 두 가지였다. 첫째, 메인 루프가 다른 일을 하는 동안에는 플래그를 못 본다(응답 시간 = 루프 한 바퀴). 둘째, 플래그가 이미 1인데 또 완료가 오면 두 번째 완료는 **흔적 없이 사라진다** — 그래서 SSD 펌웨어는 플래그 대신 완료 큐(completion queue)나 카운터를 썼다.

RTOS의 **세마포어(semaphore)**는 이 플래그에 두 가지를 더한 것이다.

- **대기 리스트**: 플래그가 0일 때 태스크는 폴링하지 않고 "1이 되면 깨워 달라"고 커널에 맡기고 잠든다(block). 그동안 CPU는 다른 태스크가 쓴다.
- **카운터**: 플래그를 0/1이 아니라 0..N으로 세면(counting semaphore) 두 번째 완료도 사라지지 않는다.

연산은 둘뿐이다. **give**(주기, 다익스트라의 V 연산, `xSemaphoreGive`/`k_sem_give`)는 count를 1 올리거나, 기다리는 태스크가 있으면 그중 하나를 깨운다. **take**(받기, P 연산, `xSemaphoreTake`/`k_sem_take`)는 count가 0보다 크면 1 내리고 바로 돌아오고, 0이면 정해진 시간까지 잠든다.

같은 객체를 쓰는 방법이 두 가지로 갈린다. 이 구분이 이 노트 전체의 뼈대다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">① 신호용 — ISR이 세우고 태스크가 내리는 깃발</text>
<rect x="70.0" y="40.0" width="200.0" height="44.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="170.0" y="58.2" font-size="12" text-anchor="middle">IMU data-ready ISR</text>
<text x="170.0" y="74.2" font-size="12" text-anchor="middle">xSemaphoreGiveFromISR</text>
<line x1="170.0" y1="84.0" x2="170.0" y2="116.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="178.0" y="104.0" font-size="12" text-anchor="start">give</text>
<rect x="70.0" y="118.0" width="200.0" height="50.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="170.0" y="139.2" font-size="12" text-anchor="middle">binary 세마포어</text>
<text x="170.0" y="155.2" font-size="12" text-anchor="middle">count 0 → 1 (깃발 세움)</text>
<line x1="170.0" y1="168.0" x2="170.0" y2="200.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="178.0" y="188.0" font-size="12" text-anchor="start">take (깃발 내림)</text>
<rect x="70.0" y="202.0" width="200.0" height="50.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="170.0" y="223.2" font-size="12" text-anchor="middle">estimator 태스크</text>
<text x="170.0" y="239.2" font-size="12" text-anchor="middle">잠들어 있다가 깨어남</text>
<text x="170.0" y="276.0" font-size="12" text-anchor="middle">주는 쪽 ≠ 받는 쪽, 처음 값 0</text>
<text x="170.0" y="294.0" font-size="12" text-anchor="middle">"일이 생겼다"를 알린다</text>
<line x1="340.0" y1="34.0" x2="340.0" y2="300.0" stroke="#888" stroke-width="1.3" stroke-dasharray="4 3"/>
<text x="510.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">② 자원 계수용 — N개 토큰이 든 그릇</text>
<rect x="360.0" y="60.0" width="110.0" height="36.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="415.0" y="82.2" font-size="12" text-anchor="middle">telemetry</text>
<rect x="360.0" y="120.0" width="110.0" height="36.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="415.0" y="142.2" font-size="12" text-anchor="middle">logger</text>
<rect x="500.0" y="70.0" width="160.0" height="90.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<circle cx="530.0" cy="104.0" r="11" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<circle cx="564.0" cy="104.0" r="11" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<circle cx="598.0" cy="104.0" r="11" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<circle cx="632.0" cy="104.0" r="11" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="580.0" y="140.0" font-size="12" text-anchor="middle">count = 남은 버퍼 2</text>
<line x1="470.0" y1="78.0" x2="498.0" y2="92.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="476.0" y="62.0" font-size="12" text-anchor="start">take</text>
<line x1="470.0" y1="138.0" x2="498.0" y2="124.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="470.0" y="174.0" font-size="12" text-anchor="start">take = 빌리기</text>
<rect x="520.0" y="212.0" width="140.0" height="44.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="590.0" y="230.2" font-size="12" text-anchor="middle">SPI DMA 완료 ISR</text>
<text x="590.0" y="246.2" font-size="12" text-anchor="middle">give = 반납</text>
<line x1="590.0" y1="212.0" x2="590.0" y2="162.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="510.0" y="276.0" font-size="12" text-anchor="middle">주는 쪽 = 받는 쪽(빌린 사람이 반납), 처음 값 N</text>
<text x="510.0" y="294.0" font-size="12" text-anchor="middle">"남은 것이 몇 개인가"를 센다</text>
<text x="340.0" y="322.0" font-size="12" text-anchor="middle">공통 구조: count 하나 + 대기 리스트 하나. 쓰임새가 다를 뿐 같은 커널 객체다</text>
</svg>
```

그림 1 — 세마포어의 두 쓰임. 왼쪽 **신호용(signaling)**: 주는 쪽(ISR)과 받는 쪽(태스크)이 다르고, 처음 값은 0이다. "일이 생겼다"를 알린다. 오른쪽 **자원 계수용(resource counting)**: 같은 태스크가 빌리고(take) 반납하며(give), 처음 값은 자원 개수 N이다. "남은 것이 몇 개인가"를 센다.

RTOS 펌웨어 엔지니어의 일에서 세마포어가 나오는 장면은 대략 이렇다.

| 장면 | 쓰임 | 이 노트의 절 |
|---|---|---|
| IMU data-ready 인터럽트 → 자세 추정 태스크 깨우기 | 신호용 (binary 또는 counting) | §3, §4 |
| SPI/I2C DMA 완료 → 전송을 시작한 태스크 깨우기 | 신호용 (binary, 타임아웃 포함) | §4, §9 |
| SPI DMA 버퍼 4개를 여러 태스크가 나눠 쓰기 | 자원 계수용 (counting) | §7 |
| 공유 UART를 한 태스크만 쓰게 하기 | **세마포어가 아니라 mutex** | §6, D2 |
| 태스크 A가 초기화를 끝낼 때까지 B를 기다리게 하기 | 신호용 (binary, 한 번만 give) | §2 |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) 위에서 FreeRTOS V11.3.2와 Zephyr v4.4.2를 **같은 가상 보드**로 돌린다. 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 결정적)이고, 시간은 FPGAIO 카운터(25 MHz, 1 카운트 = 40 ns)나 CMSDK TIMER0의 경과 카운트로 잰다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스·예외 스태킹 사이클 미모델링). 비교와 모양을 보는 숫자다. ISR 원천은 진짜 하드웨어 타이머 IRQ(CMSDK TIMER0, IRQ 8)다. 아래 `sh` 블록은 VM 안에서 실행한 명령이고, Mac에서는 `limactl shell --workdir /tmp elinux -- bash -lc '<명령>'`으로 감싸서 돌렸다. 이 노트를 쓰는 날 VM의 루트 파일시스템이 읽기 전용(ext4 emergency_ro)으로 바뀌어서 빌드 산출물은 tmpfs인 `/tmp/rtos_D1/`에 두었고, Zephyr 빌드는 `HOME=/tmp/rtos_D1/home`으로 캐시 위치를 옮겨서 돌렸다.

---

## 1. 세마포어의 정의 — count, 대기 리스트, 두 연산

### 1.1 직관: 깃발 하나 vs 토큰 그릇

- **binary 세마포어**: count가 0 또는 1인 세마포어. 깃발 하나다. 이미 서 있는 깃발을 또 세워도 깃발은 하나다.
- **counting 세마포어**: count가 0..max인 세마포어. 토큰이 든 그릇이다. 넣는 만큼 쌓이고(max까지), 꺼내는 만큼 줄어든다.

둘은 별개의 커널 객체가 아니다. FreeRTOS에서는 둘 다 같은 `Queue_t`이고 길이(`uxLength`)만 1이냐 N이냐가 다르다. Zephyr에서는 둘 다 `struct k_sem`이고 `limit`이 1이냐 N이냐가 다르다. Zephyr 소스(`kernel/sem.c`) 맨 위 주석이 아예 "The semaphores are of the 'counting' type"이라고 적어 둔다. binary는 limit 1짜리 counting일 뿐이다.

### 1.2 상태로 보면 세 가지뿐이다

```svg
<svg viewBox="0 0 680 318" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20.0" y="110.0" width="170.0" height="70.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="105.0" y="141.1" font-size="13" text-anchor="middle" font-weight="bold">count &gt; 0</text>
<text x="105.0" y="158.1" font-size="13" text-anchor="middle">대기자 없음</text>
<rect x="255.0" y="110.0" width="170.0" height="70.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="340.0" y="141.1" font-size="13" text-anchor="middle" font-weight="bold">count = 0</text>
<text x="340.0" y="158.1" font-size="13" text-anchor="middle">대기자 없음</text>
<rect x="490.0" y="110.0" width="170.0" height="70.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="575.0" y="141.1" font-size="13" text-anchor="middle" font-weight="bold">count = 0</text>
<text x="575.0" y="158.1" font-size="13" text-anchor="middle">대기자 1명 이상</text>
<path d="M 190 130 L 253 130" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="222.0" y="122.0" font-size="12" text-anchor="middle">take</text>
<path d="M 255 160 L 192 160" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="222.0" y="176.0" font-size="12" text-anchor="middle">give</text>
<path d="M 425 130 L 488 130" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="456.0" y="122.0" font-size="12" text-anchor="middle">take</text>
<path d="M 490 160 L 427 160" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="458.0" y="176.0" font-size="12" text-anchor="middle">give</text>
<path d="M 70 110 C 70 60, 140 60, 140 108" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="105.0" y="34.0" font-size="12" text-anchor="middle">give: count++</text>
<text x="105.0" y="50.0" font-size="12" text-anchor="middle">(binary는 이미 1이면 실패)</text>
<path d="M 70 180 C 70 230, 140 230, 140 182" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="105.0" y="246.0" font-size="12" text-anchor="middle">take: count-- (2 이상일 때)</text>
<path d="M 540 110 C 540 60, 610 60, 610 108" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="575.0" y="52.0" font-size="12" text-anchor="middle">take: 한 명 더 줄 섬</text>
<path d="M 540 180 C 540 230, 610 230, 610 182" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah2)"/>
<text x="575.0" y="246.0" font-size="12" text-anchor="middle">give: 맨 앞 1명 깨움</text>
<text x="575.0" y="262.0" font-size="12" text-anchor="middle">(대기자가 남아 있으면 그대로)</text>
<text x="340.0" y="100.0" font-size="12" text-anchor="middle">take: 0이면 블록(또는 즉시 실패)</text>
<text x="340.0" y="205.0" font-size="12" text-anchor="middle">give: 대기자가 없으면 count++</text>
<text x="340.0" y="290.0" font-size="12" text-anchor="middle">대기자가 있으면 give는 맨 앞(최고 우선순위) 대기자를 깨운다.</text>
<text x="340.0" y="308.0" font-size="12" text-anchor="middle">토큰을 바로 넘기는지(Zephyr) 깨워서 다시 경쟁시키는지(FreeRTOS)는 커널마다 다르다 (§1.6)</text>
</svg>
```

그림 2 — 세마포어의 상태. "count > 0", "count = 0이고 아무도 안 기다림", "count = 0이고 누군가 기다림". count > 0인데 기다리는 태스크가 있는 상태는 (잠깐의 과도 상태를 빼면) 없다. 그런 태스크는 take에서 바로 받아 갔을 것이기 때문이다.

정의를 표로 정리한다.

| 연산 | count > 0일 때 | count = 0일 때 | 반환 |
|---|---|---|---|
| take(timeout = 0) | count-- 후 즉시 성공 | 즉시 실패 | FreeRTOS `pdPASS`/`pdFALSE`, Zephyr `0`/`-EBUSY` |
| take(timeout = T) | count-- 후 즉시 성공 | 최대 T 동안 잠듦. give가 오면 성공, 시간이 다 되면 실패 | FreeRTOS `pdFALSE`, Zephyr `-EAGAIN` (타임아웃) |
| take(무한) | count-- 후 즉시 성공 | give가 올 때까지 잠듦 | `portMAX_DELAY` / `K_FOREVER` |
| give (대기자 없음) | count++ (max에서 멈춤) | count = 1 | FreeRTOS: max면 `errQUEUE_FULL`. Zephyr: `void` |
| give (대기자 있음) | — | 맨 앞 대기자 하나를 깨움 | 위와 같음 |

마지막 두 행의 "max에서 멈춤"이 §3의 주인공이다. 그리고 "Zephyr give는 `void`"라는 작은 차이가 실무에서 꽤 크다(§3.4).

API 이름의 다섯 RTOS 대응표와 `K_SEM_DEFINE(name, initial, limit)` 인자 순서 함정(FreeRTOS는 `(max, initial)`)은 A5 §1.2에서 다뤘다. 여기서는 의미를 실행으로 확인한다.

### 1.3 예제 1 — FreeRTOS: 포화, 타임아웃, 폴링

무엇을 확인하는 코드인지: binary에 give를 세 번 하면 몇 번 take할 수 있나, counting(5, 0)에 7번 주면 어떻게 되나, 빈 세마포어를 10 ms 타임아웃으로 take하면 정확히 얼마나 기다리나.

```c
/* D1 ex1 — FreeRTOS: give/take semantics, saturation, timeout */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

static void probe(void *arg)
{
    (void)arg;
    SemaphoreHandle_t b = xSemaphoreCreateBinary();          /* starts EMPTY (0) */
    board_printf_raw("binary: initial count=%u\n", (unsigned)uxSemaphoreGetCount(b));
    for (int i = 0; i < 3; i++) {
        BaseType_t r = xSemaphoreGive(b);
        board_printf_raw("binary: give #%d -> %s, count=%u\n", i, r == pdPASS ? "pdPASS" : "errQUEUE_FULL",
                         (unsigned)uxSemaphoreGetCount(b));
    }
    int took = 0;
    while (xSemaphoreTake(b, 0) == pdPASS) took++;
    board_printf_raw("binary: 3 gives -> %d takes succeeded\n", took);

    SemaphoreHandle_t c = xSemaphoreCreateCounting(5, 0);   /* (max, initial) */
    int ok = 0, full = 0;
    for (int i = 0; i < 7; i++) (xSemaphoreGive(c) == pdPASS) ? ok++ : full++;
    board_printf_raw("counting(5,0): 7 gives -> %d ok, %d full, count=%u\n", ok, full,
                     (unsigned)uxSemaphoreGetCount(c));
    took = 0;
    while (xSemaphoreTake(c, 0) == pdPASS) took++;
    board_printf_raw("counting: %d takes succeeded\n", took);

    TickType_t t0 = xTaskGetTickCount();
    uint32_t u0 = board_now_us();
    BaseType_t r = xSemaphoreTake(b, pdMS_TO_TICKS(10));   /* empty: block up to 10 ticks */
    board_printf_raw("take(10 ms) on empty -> %s after %u ticks (%u us)\n", r == pdPASS ? "pdPASS" : "pdFALSE",
                     (unsigned)(xTaskGetTickCount() - t0), (unsigned)(board_now_us() - u0));
    r = xSemaphoreTake(b, 0);
    board_printf_raw("take(0) on empty -> %s (poll, never blocks)\n", r == pdPASS ? "pdPASS" : "pdFALSE");
    board_exit(0);
}

int main(void)
{
    xTaskCreate(probe, "probe", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex1_semantics.c \
  OUT=/tmp/rtos_D1/ex1 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex1/app.elf
```

```text
binary: initial count=0
binary: give #0 -> pdPASS, count=1
binary: give #1 -> errQUEUE_FULL, count=1
binary: give #2 -> errQUEUE_FULL, count=1
binary: 3 gives -> 1 takes succeeded
counting(5,0): 7 gives -> 5 ok, 2 full, count=5
counting: 5 takes succeeded
take(10 ms) on empty -> pdFALSE after 10 ticks (9827 us)
take(0) on empty -> pdFALSE (poll, never blocks)
[board_exit] code=0
```

출력에서 볼 것:

- `xSemaphoreCreateBinary()`는 **비어 있는(0)** 상태로 만들어진다. 신호용으로 바로 쓰기 좋게 그렇게 정했다. 옛 API `vSemaphoreCreateBinary()`는 만들자마자 give까지 해서 1로 시작했다(지금도 `semphr.h`에 매크로가 남아 있다 — 옛 코드를 옮길 때 초기값이 뒤바뀌는 함정).
- 두 번째 give부터 `errQUEUE_FULL`이 돌아온다. 세 번 줬는데 take는 한 번만 성공한다. **give 두 번이 사라졌다.** FreeRTOS는 적어도 반환값으로 알려 준다.
- counting(5, 0)은 5까지 쌓이고 나머지 2번은 같은 이유로 실패한다.
- 10 ms 타임아웃은 "10 tick"이다. 실제로 기다린 시간은 9827 µs로 10 ms보다 **짧다**. take를 부른 시점이 tick 경계 사이 어딘가였고, 타임아웃은 tick 단위로 세기 때문이다. tick 해상도의 함정(최소 대기 시간이 1 tick보다 짧을 수 있다)은 C5에서 자세히 다뤘다.

### 1.4 예제 2 — Zephyr: 같은 실험, 다른 반환 규약

무엇을 확인하는 코드인지: Zephyr `k_sem`의 포화가 어떻게 보이는지, `K_NO_WAIT`·타임아웃·`k_sem_reset()`·잘못된 init의 반환값.

```c
/* D1 ex2 — Zephyr v4.4.2: k_sem saturation (silent), timeouts, k_sem_reset */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "zexit.h"

K_SEM_DEFINE(bin, 0, 1);                       /* (initial, limit) = binary */
K_SEM_DEFINE(cnt, 0, 5);                       /* counting, limit 5 */
K_SEM_DEFINE(rst, 0, 1);

static void waiter(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	int r = k_sem_take(&rst, K_FOREVER);
	printk("waiter: k_sem_take(K_FOREVER) returned %d (-EAGAIN=%d)\n", r, -EAGAIN);
}
K_THREAD_DEFINE(w, 1024, waiter, NULL, NULL, NULL, 2, 0, 0);

int main(void)
{
	for (int i = 0; i < 3; i++) {
		k_sem_give(&bin);                      /* returns void: no way to see the loss */
		printk("bin: give #%d -> count=%u\n", i, k_sem_count_get(&bin));
	}
	for (int i = 0; i < 7; i++) {
		k_sem_give(&cnt);
	}
	printk("cnt(limit 5): 7 gives -> count=%u\n", k_sem_count_get(&cnt));

	printk("take(K_NO_WAIT) on bin(1) -> %d\n", k_sem_take(&bin, K_NO_WAIT));
	printk("take(K_NO_WAIT) on bin(0) -> %d (-EBUSY=%d)\n", k_sem_take(&bin, K_NO_WAIT), -EBUSY);
	int64_t t0 = k_uptime_get();
	int r = k_sem_take(&bin, K_MSEC(10));
	printk("take(K_MSEC(10)) on empty -> %d (-EAGAIN=%d) after %lld ms\n", r, -EAGAIN,
	       k_uptime_get() - t0);

	k_sleep(K_MSEC(1));                        /* let waiter (prio 2) block on rst */
	printk("main: k_sem_reset(&rst) with 1 waiter\n");
	k_sem_reset(&rst);
	k_sleep(K_MSEC(1));
	printk("k_sem_init(&s, 5, 0) -> %d (-EINVAL=%d)\n", k_sem_init(&rst, 5, 0), -EINVAL);
	z_exit();
	return 0;
}
```

```sh
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D1/z_sem /tmp/rtos_D1/z_sem >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/z_sem/zephyr/zephyr.elf 3
```

```text
bin: give #0 -> count=1
bin: give #1 -> count=1
bin: give #2 -> count=1
cnt(limit 5): 7 gives -> count=5
take(K_NO_WAIT) on bin(1) -> 0
take(K_NO_WAIT) on bin(0) -> -16 (-EBUSY=-16)
take(K_MSEC(10)) on empty -> -11 (-EAGAIN=-11) after 20 ms
main: k_sem_reset(&rst) with 1 waiter
waiter: k_sem_take(K_FOREVER) returned -11 (-EAGAIN=-11)
k_sem_init(&s, 5, 0) -> -22 (-EINVAL=-22)
```

출력에서 볼 것:

- `k_sem_give()`는 `void`라서 **포화를 알 방법이 반환값에 없다**. count가 1에서 그대로라는 것만 `k_sem_count_get()`으로 보인다. `kernel/sem.c`의 `sem->count += (sem->count != sem->limit) ? 1U : 0U;` 한 줄이 조용히 버린다.
- 빈 세마포어에 `K_NO_WAIT`은 `-EBUSY`, 시간 만료는 `-EAGAIN`이다. 둘을 구분해야 "원래 비어 있었다"와 "기다렸는데 안 왔다"를 가른다.
- `K_MSEC(10)` 타임아웃이 **20 ms** 걸렸다. 두 가지가 겹쳤다. (1) `mps2/an385` 보드의 tick이 100 Hz다 — `kernel/Kconfig`의 `SYS_CLOCK_TICKS_PER_SEC`가 `default 100 if QEMU_TARGET || SOC_POSIX`. 10 ms = 1 tick. (2) `kernel/timeout.c`의 `z_add_timeout()`은 상대 타임아웃에 `timeout.ticks + 1 + ticks_elapsed`를 쓴다. "적어도 요청한 시간만큼은 기다린다"를 보장하려고 1 tick을 더하는 것이다. FreeRTOS(최소 보장 없음, 9.8 ms)와 반대 방향의 선택이다. 1 kHz tick이면 이 차이는 1 ms로 줄어든다.
- `k_sem_reset()`은 count를 0으로 만들면서 기다리던 스레드를 **`-EAGAIN`으로 깨운다**(타임아웃과 같은 값). 기다리는 쪽에서는 "리셋당했다"와 "시간 만료"를 구분하지 못한다.
- `k_sem_init(&s, 5, 0)`(limit 0)은 `-EINVAL`. FreeRTOS 습관대로 `(max, initial)` 순서로 쓰면 여기서 걸린다(A5 §1.2).

### 1.5 같은 이름, 다른 의미 — 정리

| 항목 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| binary 생성 직후 값 | 0 (`xSemaphoreCreateBinary`) | `K_SEM_DEFINE(s, 초기값, 1)`에서 정함 |
| 포화된 give | `errQUEUE_FULL` 반환 | 조용히 무시 (`void`) |
| 타임아웃 단위·반올림 | tick, 최소 보장 없음 (9.8 ms) | tick, +1 tick (100 Hz에서 20 ms) |
| 타임아웃 실패 값 | `pdFALSE` | `-EAGAIN` (`K_NO_WAIT`이면 `-EBUSY`) |
| 리셋 | 없음 (`xQueueReset`은 큐용) | `k_sem_reset()` — 대기자를 `-EAGAIN`으로 깨움 |
| ISR에서 give | `xSemaphoreGiveFromISR` + yield 직접 | `k_sem_give` 그대로 (§4) |
| give가 대기자에게 토큰을 | 깨워서 **다시 경쟁**시킨다 | **직접 넘긴다** (§1.6) |

### 1.6 예제 3 — give는 토큰을 넘기나, 깨우기만 하나

A5 §7에서 Zephyr `z_impl_k_sem_give()`는 대기자가 있으면 count를 올리지 않고 `arch_thread_return_value_set(thread, 0)`으로 "받았음"을 심어 준다는 것을 봤다. FreeRTOS `xQueueGenericSend()`는 반대로 count(`uxMessagesWaiting`)를 먼저 올리고 대기자를 ready로 옮길 뿐이다. 깨어난 태스크는 `xQueueSemaphoreTake()`의 `for` 루프를 한 바퀴 더 돌면서 **다시** count를 확인한다. 그 사이에 누가 가져가면? 실험했다.

무엇을 확인하는 코드인지: 낮은 우선순위 태스크가 먼저 기다리고 있다. 높은 우선순위 태스크가 give하자마자 같은 세마포어를 `take(0)`한다.

```c
/* D1 ex12 — FreeRTOS: does give hand the token to the waiter, or can the giver take it back? */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

static SemaphoreHandle_t sem;
static void low(void *a)                      /* prio 1: waits first */
{
    (void)a;
    BaseType_t r = xSemaphoreTake(sem, pdMS_TO_TICKS(5));
    board_printf_raw("low : take -> %s\n", r == pdPASS ? "pdPASS" : "timeout (token was stolen)");
    board_exit(0);
}
static void high(void *a)                     /* prio 3: gives, then immediately takes */
{
    (void)a;
    vTaskDelay(1);                            /* let low block on sem */
    xSemaphoreGive(sem);                      /* low -> ready, but high keeps running */
    BaseType_t r = xSemaphoreTake(sem, 0);
    board_printf_raw("high: give then take(0) -> %s\n", r == pdPASS ? "pdPASS (re-took it)" : "fail");
    vTaskSuspend(NULL);
}
int main(void)
{
    sem = xSemaphoreCreateBinary();
    xTaskCreate(low, "low", 256, NULL, 1, NULL);
    xTaskCreate(high, "high", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

Zephyr 판(`z_steal`)은 같은 순서를 `k_sem_give()` → `k_sem_take(K_NO_WAIT)`로 한다(main 스레드 우선순위 0, low 스레드 5).

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex12_steal.c \
  OUT=/tmp/rtos_D1/ex12 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex12/app.elf 10
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D1/z_steal /tmp/rtos_D1/z_steal >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/z_steal/zephyr/zephyr.elf 5
```

```text
high: give then take(0) -> pdPASS (re-took it)
low : take -> timeout (token was stolen)
[board_exit] code=0
           FLASH:       13524 B         4 MB      0.32%
             RAM:        7280 B         4 MB      0.17%
high: give then take(K_NO_WAIT) -> -16 (-EBUSY=-16), count=0
low : take -> 0 (0 = got the token)
```

출력에서 볼 것: FreeRTOS에서는 give한 높은 태스크가 **자기가 준 토큰을 도로 가져갔고**, 깨어난 low는 다시 잠들었다가 5 ms 뒤 타임아웃으로 실패했다. Zephyr에서는 give 순간 토큰이 low의 것이 되어 high의 `K_NO_WAIT` take가 `-EBUSY`, count는 0이다. (FreeRTOS 출력 순서가 high → low인 것도 의미가 있다: give가 low를 깨워도 high가 더 높으니 high가 계속 돈다.)

이것은 "공정성"의 차이다. FreeRTOS 방식은 높은 우선순위가 언제든 끼어들 수 있어 단순하고 우선순위 원칙에 충실하지만, 낮은 쪽은 "깨어났는데 못 받는" 일을 겪는다. Zephyr 방식은 깨어난 스레드가 반드시 받는다. 실무 결론: **"give 한 번 = 대기자 하나가 반드시 받는다"를 FreeRTOS에서 가정하지 말 것.** 특히 give한 쪽이 같은 세마포어를 take하는 구조(자원 계수용에서 흔하다)에서 낮은 우선순위 태스크가 굶을 수 있다(§7에서 실제로 본다). 깨울 순서 자체(우선순위 순, ThreadX는 FIFO)는 A5 §7.1에서 실험했다.

---

## 2. 두 가지 쓰임 — 신호용 vs 자원 계수용

같은 객체를 두 방식으로 쓴다는 것을 표로 정리하면 설계 실수가 보인다.

| 항목 | 신호용 (signaling, event) | 자원 계수용 (resource counting) |
|---|---|---|
| 질문 | "일이 생겼나?" | "남은 게 몇 개인가?" |
| 처음 값 | 0 | 자원 개수 N |
| 누가 give | 생산자(ISR, 다른 태스크) | 자원을 반납하는 쪽(대개 빌린 태스크, 또는 완료 ISR) |
| 누가 take | 소비자(처리 태스크) | 자원을 빌리는 태스크 |
| give와 take의 짝 | 다른 실행 흐름 | 같은 흐름이 빌리고 반납하는 경우가 많다 |
| 대표 예 | IMU data-ready, DMA 완료, "초기화 끝" | DMA 버퍼 N개, DMA 채널 N개, 소켓 슬롯 N개 |
| binary로 충분? | 이벤트가 겹치지 않으면. 겹치면 counting 또는 다른 수단(§3) | N = 1이면 binary지만 그건 사실상 **lock** → mutex를 써라(§6) |
| 흔한 실수 | 이벤트 유실, ISR yield 누락 | count는 맞는데 "어느 것"이 비었는지 모름(§7) |

**신호용의 특별한 경우 — 한 번만 주는 신호.** 태스크 B가 태스크 A의 초기화(예: IMU 캘리브레이션)를 기다려야 하면, A가 끝에서 binary 세마포어를 한 번 give하고 B는 시작할 때 take한다. 단, 기다리는 태스크가 여럿이면 binary 하나로는 **한 명만** 깬다. "모두 깨우기"는 이벤트 그룹(D5)이나 Zephyr `k_event`, 또는 각자 세마포어를 주는 식으로 해결한다.

**자원 계수용과 mutex의 경계.** "N개 중 하나를 빌린다"는 counting 세마포어의 영역이다. 그런데 N = 1이면 그것은 "하나뿐인 자원을 독점한다" = 상호배제이고, 그때는 소유권과 우선순위 상속이 있는 mutex가 맞다. N > 1의 자원 계수용에는 소유권 개념이 없어도 된다 — 버퍼 0번을 누가 들고 있든 상관없고, "몇 개 남았나"만 중요하다.

---

## 3. give를 아무도 기다리지 않을 때 — 잃어버린 이벤트

### 3.1 직관

신호용 세마포어에서 가장 흔한 버그는 이것이다. ISR은 125 µs마다 give한다. 태스크는 보통 샘플 하나를 20 µs에 처리하지만, 10번에 한 번은 자력계 융합 같은 무거운 일로 400 µs가 걸린다. 그 400 µs 동안 IRQ가 세 번 온다. binary 세마포어는 첫 번째 give로 1이 되고, 두 번째·세 번째 give는 **이미 1이라 버려진다**. 태스크가 돌아왔을 때 보이는 것은 "깃발 하나"뿐이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">IRQ 125 µs 간격, 10번째 샘플 처리에 400 µs가 걸릴 때 (ex3의 한 장면)</text>
<line x1="110.0" y1="300.0" x2="654.0" y2="300.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="110.0" y1="300.0" x2="110.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="110.0" y="318.0" font-size="12" text-anchor="middle">0</text>
<line x1="178.0" y1="300.0" x2="178.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="178.0" y="318.0" font-size="12" text-anchor="middle">100</text>
<line x1="246.0" y1="300.0" x2="246.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="246.0" y="318.0" font-size="12" text-anchor="middle">200</text>
<line x1="314.0" y1="300.0" x2="314.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="314.0" y="318.0" font-size="12" text-anchor="middle">300</text>
<line x1="382.0" y1="300.0" x2="382.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="382.0" y="318.0" font-size="12" text-anchor="middle">400</text>
<line x1="450.0" y1="300.0" x2="450.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="450.0" y="318.0" font-size="12" text-anchor="middle">500</text>
<line x1="518.0" y1="300.0" x2="518.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="518.0" y="318.0" font-size="12" text-anchor="middle">600</text>
<line x1="586.0" y1="300.0" x2="586.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="586.0" y="318.0" font-size="12" text-anchor="middle">700</text>
<line x1="654.0" y1="300.0" x2="654.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="654.0" y="318.0" font-size="12" text-anchor="middle">800</text>
<text x="650.0" y="290.0" font-size="12" text-anchor="end">µs</text>
<text x="10.0" y="52.0" font-size="12" text-anchor="start">IRQ</text>
<line x1="110.0" y1="40.0" x2="110.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="195.0" y1="40.0" x2="195.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="280.0" y1="40.0" x2="280.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="365.0" y1="40.0" x2="365.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="450.0" y1="40.0" x2="450.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="535.0" y1="40.0" x2="535.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<line x1="620.0" y1="40.0" x2="620.0" y2="58.0" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="10.0" y="92.0" font-size="12" text-anchor="start">estimator</text>
<rect x="118.2" y="78.0" width="272.0" height="20.0" rx="2" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="254.2" y="93.0" font-size="12" text-anchor="middle">10번째 샘플: 400 µs</text>
<text x="10.0" y="145.0" font-size="12" text-anchor="start" font-weight="bold">binary</text>
<text x="10.0" y="161.0" font-size="12" text-anchor="start">count</text>
<path d="M 110.0 165.0 L 195.0 165.0 L 195.0 151.0 L 390.2 151.0 L 390.2 165.0 L 450.0 165.0 L 450.0 151.0 L 458.2 151.0 L 458.2 165.0 L 535.0 165.0 L 535.0 151.0 L 543.2 151.0 L 543.2 165.0 L 620.0 165.0 L 620.0 151.0 L 628.2 151.0 L 628.2 165.0 L 654.0 165.0" stroke="#d0564a" stroke-width="2" fill="none"/>
<line x1="110.0" y1="165.0" x2="654.0" y2="165.0" stroke="#888" stroke-width="0.8" stroke-dasharray="4 3"/>
<text x="280.0" y="143.0" font-size="13" text-anchor="middle">✕</text>
<text x="365.0" y="143.0" font-size="13" text-anchor="middle">✕</text>
<rect x="390.2" y="171.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="458.2" y="171.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="543.2" y="171.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="628.2" y="171.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="314.0" y="127.0" font-size="12" text-anchor="middle">give 실패(errQUEUE_FULL) = 이벤트 2개 잃음</text>
<text x="10.0" y="235.0" font-size="12" text-anchor="start" font-weight="bold">counting</text>
<text x="10.0" y="251.0" font-size="12" text-anchor="start">count</text>
<path d="M 110.0 255.0 L 195.0 255.0 L 195.0 241.0 L 280.0 241.0 L 280.0 227.0 L 365.0 227.0 L 365.0 213.0 L 390.2 213.0 L 390.2 227.0 L 405.8 227.0 L 405.8 241.0 L 421.4 241.0 L 421.4 255.0 L 450.0 255.0 L 450.0 241.0 L 458.2 241.0 L 458.2 255.0 L 535.0 255.0 L 535.0 241.0 L 543.2 241.0 L 543.2 255.0 L 620.0 255.0 L 620.0 241.0 L 628.2 241.0 L 628.2 255.0 L 654.0 255.0" stroke="#4a7bd0" stroke-width="2" fill="none"/>
<line x1="110.0" y1="255.0" x2="654.0" y2="255.0" stroke="#888" stroke-width="0.8" stroke-dasharray="4 3"/>
<rect x="390.2" y="261.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="405.8" y="261.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="421.4" y="261.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="458.2" y="261.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="543.2" y="261.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="628.2" y="261.0" width="13.6" height="10.0" rx="1" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="334.4" y="203.0" font-size="12" text-anchor="middle">count가 3까지 쌓였다가 하나씩 처리됨</text>
<text x="10.0" y="190.0" font-size="12" text-anchor="start"></text>
</svg>
```

그림 3 — 같은 IRQ 열에 대한 binary와 counting의 count 변화(그림은 ex3 조건을 단순화한 시뮬레이션). binary(빨강)는 250·375 µs의 give가 튕겨 나가 샘플 두 개를 잃는다(✕). counting(파랑)은 3까지 쌓였다가 긴 처리가 끝난 뒤 연달아 처리된다(초록 막대).

Don의 SSD 경험으로 말하면, 완료 플래그 하나에 완료 두 개가 겹친 상황이다. SSD 펌웨어가 완료 플래그 대신 completion queue(엔트리 개수를 세는 링)를 쓴 것과 같은 이유로 counting 세마포어를 쓴다.

### 3.2 예제 4 — 8 kHz "IMU data-ready" IRQ로 측정하기

무엇을 확인하는 코드인지: CMSDK TIMER0을 125 µs(8 kHz) 주기로 돌려 ISR에서 give한다. estimator 태스크(우선순위 3)는 take할 때마다 샘플 하나를 처리하고, 10번째마다 L µs가 걸린다. L을 50~1200 µs로 바꿔 가며 binary와 counting(max 32)에서 IRQ 1000번 중 처리된 수, 잃은 수, give 실패 수, take 직후 남은 count의 최댓값을 센다.

```c
/* D1 ex3 — lost events: 8 kHz "IMU data-ready" IRQ -> binary vs counting semaphore */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

#define PERIOD_US 125u                      /* 8 kHz data-ready */
#define NEVENTS   1000u
static SemaphoreHandle_t s_bin, s_cnt;
static SemaphoreHandle_t volatile cur;
static volatile uint32_t n_isr, n_fail, n_done, max_cnt, long_us;

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (n_isr >= NEVENTS) { timer0_stop(); return; }
    n_isr++;
    BaseType_t woken = pdFALSE;
    if (xSemaphoreGiveFromISR(cur, &woken) != pdPASS) n_fail++;   /* errQUEUE_FULL */
    portYIELD_FROM_ISR(woken);
}
static void estimator(void *arg)            /* prio 3: one "sample" per take */
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(cur, pdMS_TO_TICKS(2)) != pdPASS) continue;
        n_done++;
        UBaseType_t c = uxSemaphoreGetCount(cur);
        if (c > max_cnt) max_cnt = c;
        spin_cyc(((n_done % 10) == 0 ? long_us : 20u) * 25u);  /* every 10th: mag fusion */
    }
}
static void run(const char *name, SemaphoreHandle_t s, uint32_t L)
{
    cur = s; long_us = L; n_isr = n_fail = n_done = max_cnt = 0;
    vTaskDelay(pdMS_TO_TICKS(5));           /* estimator re-reads cur */
    n_done = 0;
    timer0_start(PERIOD_US * 25u, 0x40);
    while (n_isr < NEVENTS) vTaskDelay(1);
    vTaskDelay(pdMS_TO_TICKS(20));          /* let estimator drain */
    board_printf_raw("%s L=%u us: isr=%u done=%u lost=%u give_fail=%u max_count=%u\n", name,
                     (unsigned)L, (unsigned)n_isr, (unsigned)n_done, (unsigned)(n_isr - n_done),
                     (unsigned)n_fail, (unsigned)max_cnt);
}
static void control(void *arg)              /* prio 1 */
{
    (void)arg;
    static const uint16_t Ls[] = { 50, 100, 200, 300, 400, 600, 800, 1000, 1200 };
    for (unsigned i = 0; i < sizeof Ls / sizeof Ls[0]; i++) {
        run("binary  ", s_bin, Ls[i]);
        run("count32 ", s_cnt, Ls[i]);
    }
    board_exit(0);
}
int main(void)
{
    s_bin = xSemaphoreCreateBinary();
    s_cnt = xSemaphoreCreateCounting(32, 0);
    cur = s_bin;
    xTaskCreate(estimator, "est", 256, NULL, 3, NULL);
    xTaskCreate(control, "ctl", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex3_lost.c \
  OUT=/tmp/rtos_D1/ex3 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex3/app.elf 60
```

```text
binary   L=50 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=0
count32  L=50 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=0
binary   L=100 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=0
count32  L=100 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=0
binary   L=200 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=0
count32  L=200 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=0
binary   L=300 us: isr=1000 done=910 lost=90 give_fail=90 max_count=0
count32  L=300 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=1
binary   L=400 us: isr=1000 done=834 lost=166 give_fail=166 max_count=0
count32  L=400 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=2
binary   L=600 us: isr=1000 done=771 lost=229 give_fail=229 max_count=0
count32  L=600 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=3
binary   L=800 us: isr=1000 done=670 lost=330 give_fail=330 max_count=0
count32  L=800 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=5
binary   L=1000 us: isr=1000 done=591 lost=409 give_fail=409 max_count=0
count32  L=1000 us: isr=1000 done=1000 lost=0 give_fail=0 max_count=7
binary   L=1200 us: isr=1000 done=560 lost=440 give_fail=440 max_count=0
count32  L=1200 us: isr=1000 done=905 lost=95 give_fail=95 max_count=31
[board_exit] code=0
```

출력에서 볼 것:

- **binary는 L = 300 µs부터 잃기 시작한다.** 200 µs까지는 0이다. 처리 시간이 IRQ 주기(125 µs)보다 길어도 바로 잃지는 않는다 — 긴 처리 중에 IRQ가 **두 번 이상** 와야 잃는다.
- 잃은 수(`lost`)와 `give_fail`이 **정확히 같다**. FreeRTOS에서는 `xSemaphoreGiveFromISR()`의 반환값(`errQUEUE_FULL`)만 세도 유실을 100% 잡아낼 수 있다는 뜻이다. 양산 코드에 이 카운터 하나를 넣어 두면 "가끔 샘플이 빠진다"를 현장에서 숫자로 확인할 수 있다.
- **counting(max 32)은 L = 1000 µs까지 하나도 잃지 않는다.** take 직후 남은 count(`max_count`)가 1, 2, 3, 5, 7로 늘어난다 — 긴 처리 동안 쌓인 IRQ다. 이 숫자가 counting 세마포어의 max를 정하는 근거다.
- **L = 1200에서는 counting도 95개를 잃었다.** count가 31(= max 32에서 하나 꺼낸 직후)까지 차고 넘쳤다. 이건 버스트가 아니라 **평균 과부하**다(§3.3).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">잃어버린 이벤트 수 (1000번 IRQ 중) — 실측, QEMU icount</text>
<line x1="70.0" y1="270.0" x2="640.0" y2="270.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70.0" y1="270.0" x2="70.0" y2="40.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="66.0" y1="270.0" x2="70.0" y2="270.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="66.0" y1="226.0" x2="70.0" y2="226.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="230.0" font-size="12" text-anchor="end">100</text>
<line x1="70.0" y1="226.0" x2="640.0" y2="226.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<line x1="66.0" y1="182.0" x2="70.0" y2="182.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="186.0" font-size="12" text-anchor="end">200</text>
<line x1="70.0" y1="182.0" x2="640.0" y2="182.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<line x1="66.0" y1="138.0" x2="70.0" y2="138.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="142.0" font-size="12" text-anchor="end">300</text>
<line x1="70.0" y1="138.0" x2="640.0" y2="138.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<line x1="66.0" y1="94.0" x2="70.0" y2="94.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="98.0" font-size="12" text-anchor="end">400</text>
<line x1="70.0" y1="94.0" x2="640.0" y2="94.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<line x1="66.0" y1="50.0" x2="70.0" y2="50.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="54.0" font-size="12" text-anchor="end">500</text>
<line x1="70.0" y1="50.0" x2="640.0" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<line x1="70.0" y1="270.0" x2="70.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="70.0" y="288.0" font-size="12" text-anchor="middle">0</text>
<line x1="165.0" y1="270.0" x2="165.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="165.0" y="288.0" font-size="12" text-anchor="middle">200</text>
<line x1="260.0" y1="270.0" x2="260.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="260.0" y="288.0" font-size="12" text-anchor="middle">400</text>
<line x1="355.0" y1="270.0" x2="355.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="355.0" y="288.0" font-size="12" text-anchor="middle">600</text>
<line x1="450.0" y1="270.0" x2="450.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="450.0" y="288.0" font-size="12" text-anchor="middle">800</text>
<line x1="545.0" y1="270.0" x2="545.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="545.0" y="288.0" font-size="12" text-anchor="middle">1000</text>
<line x1="640.0" y1="270.0" x2="640.0" y2="274.0" stroke="currentColor" stroke-width="1.3"/>
<text x="640.0" y="288.0" font-size="12" text-anchor="middle">1200</text>
<text x="355.0" y="308.0" font-size="12" text-anchor="middle">10번째 샘플 처리 시간 L (µs)  — 나머지 샘플은 20 µs, IRQ는 125 µs마다</text>
<path d="M 93.8 270.0 L 117.5 270.0 L 165.0 270.0 L 212.5 230.4 L 260.0 197.0 L 355.0 169.2 L 450.0 124.8 L 545.0 90.0 L 640.0 76.4" stroke="#d0564a" stroke-width="2" fill="none"/>
<circle cx="93.8" cy="270.0" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="117.5" cy="270.0" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="165.0" cy="270.0" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="212.5" cy="230.4" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="260.0" cy="197.0" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="355.0" cy="169.2" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="450.0" cy="124.8" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="545.0" cy="90.0" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<circle cx="640.0" cy="76.4" r="3.5" fill="#d0564a" fill-opacity="0.9" stroke="#d0564a"/>
<path d="M 93.8 270.0 L 117.5 270.0 L 165.0 270.0 L 212.5 270.0 L 260.0 270.0 L 355.0 270.0 L 450.0 270.0 L 545.0 270.0 L 640.0 228.2" stroke="#4a7bd0" stroke-width="2" fill="none"/>
<circle cx="93.8" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="117.5" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="165.0" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="212.5" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="260.0" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="355.0" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="450.0" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="545.0" cy="270.0" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="640.0" cy="228.2" r="3.5" fill="#4a7bd0" fill-opacity="0.9" stroke="#4a7bd0"/>
<circle cx="93.8" cy="270.0" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="117.5" cy="270.0" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="165.0" cy="270.0" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="212.5" cy="230.0" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="260.0" cy="196.7" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="355.0" cy="168.5" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="450.0" cy="123.3" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="545.0" cy="88.8" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<circle cx="640.0" cy="74.4" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<text x="268.0" y="189.0" font-size="12" text-anchor="start">166</text>
<text x="553.0" y="82.0" font-size="12" text-anchor="start">409</text>
<text x="636.0" y="218.2" font-size="12" text-anchor="end">95</text>
<rect x="90.0" y="58.0" width="250.0" height="66.0" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-width="1.3"/>
<line x1="100.0" y1="74.0" x2="130.0" y2="74.0" stroke="#d0564a" stroke-width="2"/>
<text x="138.0" y="78.0" font-size="12" text-anchor="start">binary 세마포어</text>
<line x1="100.0" y1="94.0" x2="130.0" y2="94.0" stroke="#4a7bd0" stroke-width="2"/>
<text x="138.0" y="98.0" font-size="12" text-anchor="start">counting (max 32)</text>
<circle cx="115" cy="113" r="7" fill="none" stroke="currentColor" stroke-width="1"/>
<text x="138.0" y="117.0" font-size="12" text-anchor="start">모델 1000·k/(10+k)</text>
<text x="540.2" y="252.4" font-size="12" text-anchor="end">U &gt; 1 (평균 과부하) →</text>
</svg>
```

그림 4 — 실측: 10번째 샘플의 처리 시간 L에 따른 유실 수(IRQ 1000번 중). binary(빨강)는 L이 250 µs를 넘으면서 계단처럼 늘고, 큰 원(모델, §3.3)과 거의 정확히 겹친다. counting(파랑)은 평균 부하가 1을 넘는 L = 1200 µs에서만 잃는다.

### 3.3 예제 5 — 몇 개를 잃을지 미리 계산하기

측정값이 너무 깔끔해서 모델을 세워 봤다. 긴 처리가 IRQ 직후(깨어나는 데 약 W = 12 µs) 시작해 L µs 동안 이어지면, 그 사이 도착하는 IRQ는 `floor((L + W) / 125)`개다. binary에서는 그중 첫 번째만 깃발로 남고 나머지 k개는 잃는다. 처리된 샘플 10개마다 IRQ가 10 + k개 소모되므로 IRQ 1000개 중 잃는 수는 `1000 × k / (10 + k)`다. counting은 평균 부하 U = (9 × 20 + L) / (10 × 125)가 1보다 작으면 결국 다 따라잡는다.

```python
# D1 ex11 — why does a binary semaphore lose exactly that many events? model vs measurement (ex3)
import math, re
P, W, N = 125, 12, 1000          # IRQ period us, wake+take overhead us, IRQs per run
meas = {}
for line in open("ex3.out"):
    m = re.match(r"(\w+)\s+L=(\d+) us: isr=\d+ done=(\d+) lost=(\d+) .*max_count=(\d+)", line)
    if m: meas[(m[1], int(m[2]))] = (int(m[3]), int(m[4]), int(m[5]))
print(" L(us)  IRQs-in-long-work  k=lost/long  model lost  measured lost  U(avg load)")
for L in sorted({k[1] for k in meas}):
    irqs = math.floor((L + W) / P)          # IRQs that land while the 10th sample is processed
    k = max(0, irqs - 1)                    # binary: first one sets the flag, rest bounce off
    model = N * k / (10 + k)                # each 10 processed samples cost 10+k IRQs
    U = (9 * 20 + L) / (10 * P)             # counting sem only helps while U < 1
    print(f"{L:6d}  {irqs:17d}  {k:11d}  {model:10.1f}  {meas[('binary', L)][1]:13d}  {U:11.2f}")
```

```sh
python3 ex11_model.py
```

```text
 L(us)  IRQs-in-long-work  k=lost/long  model lost  measured lost  U(avg load)
    50                  0            0         0.0              0         0.18
   100                  0            0         0.0              0         0.22
   200                  1            0         0.0              0         0.30
   300                  2            1        90.9             90         0.38
   400                  3            2       166.7            166         0.46
   600                  4            3       230.8            229         0.62
   800                  6            5       333.3            330         0.78
  1000                  8            7       411.8            409         0.94
  1200                  9            8       444.4            440         1.10
```

출력에서 볼 것: 모델과 실측이 L = 300 → 90.9 대 90, 1000 → 411.8 대 409처럼 1% 안에서 맞는다(작은 차이는 ISR 자신이 긴 처리 시간을 잠깐 뺏는 효과 등). 그리고 counting이 실패한 L = 1200은 정확히 U = 1.10 > 1인 칸이다. 정리하면 두 가지 서로 다른 조건이 있다.

| 조건 | 무엇을 정하나 | 깨지면 |
|---|---|---|
| 버스트 조건: 가장 긴 처리 동안 오는 이벤트 수 ≤ max count | counting 세마포어의 **max** | 버스트 끝에서 유실 (binary는 max = 1이라 2개부터) |
| 평균 조건: U = 평균 처리 시간 / 평균 도착 간격 < 1 | 시스템 **설계** 자체 | 어떤 동기화 수단으로도 못 막는다 — 처리를 줄이거나 나눠야 한다 |

counting 세마포어는 **버스트를 흡수하는 완충기**일 뿐, 처리량을 늘려 주지 않는다. "샘플이 빠지니 max를 1000으로 올리자"는 평균 조건이 깨진 상황에서는 지연만 늘린다(L = 1200에서 max를 늘리면 유실 시점이 뒤로 밀릴 뿐 결국 잃는다).

### 3.4 신호용 세마포어를 고를 때의 판단

이벤트를 잃으면 안 되는지, 그리고 이벤트마다 데이터가 따로 있는지로 고른다.

| 상황 | 고를 것 | 이유 |
|---|---|---|
| "새 데이터가 있다"만 중요, 최신 것만 읽으면 됨 (예: 50 Hz 기압계) | binary 세마포어 (유실 허용) | 겹친 이벤트는 어차피 같은 레지스터를 읽는다. 최신값 덮어쓰기가 맞다 |
| 이벤트 개수가 중요, 데이터는 하드웨어 FIFO에 쌓임 (예: IMU FIFO) | counting 세마포어, max = 최악 버스트 + 여유 | take 횟수 = 읽어야 할 샘플 수 |
| 이벤트마다 별도 데이터(타임스탬프, 길이) | 큐(D4) 또는 ISR→태스크 링버퍼(D8) + 신호 | 세마포어는 개수만 센다. 데이터는 따로 실어 보내야 한다 |
| 가장 빠른 1:1 신호 | task notification(D5) — `ulTaskNotifyTake(pdFALSE, …)`이면 counting처럼 동작 | §5에서 측정: FreeRTOS 세마포어보다 깨우기 지연이 약 20% 짧다 |

함정 두 개를 덧붙인다.

- **Zephyr에서는 유실이 안 보인다.** `k_sem_give()`가 `void`이므로 FreeRTOS처럼 반환값으로 셀 수 없다. 유실 감시가 필요하면 ISR에서 별도 카운터를 올리고 태스크에서 처리 수와 비교하거나, give 전에 `k_sem_count_get(&s) == limit`인지 직접 본다.
- **binary로 바꿔서 "해결"된 것처럼 보이는 경우.** counting에서 binary로 바꾸면 count가 무한정 쌓이는 현상(평균 과부하의 증상)이 사라져서 고친 것처럼 보이지만, 실제로는 이벤트를 조용히 버리게 된 것이다.

---

## 4. ISR → 태스크 신호 — FromISR, woken 플래그, yield

### 4.1 FreeRTOS의 세 단계 규약

FreeRTOS ISR에서 세마포어를 주는 코드는 항상 이 모양이다.

```c
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;                 /* 1. 원인 지우기 */
    BaseType_t woken = pdFALSE;                    /* 2. 반드시 pdFALSE로 초기화 */
    xSemaphoreGiveFromISR(sem, &woken);            /* 3. 블록하지 않는 ISR용 API */
    portYIELD_FROM_ISR(woken);                     /* 4. 깨운 태스크가 더 급하면 PendSV */
}
```

각 줄의 의미는 이렇다.

- `xSemaphoreGiveFromISR(sem, &woken)` = `xQueueGiveFromISR()`(`queue.c`). 크리티컬 섹션을 `taskENTER_CRITICAL_FROM_ISR()`(BASEPRI 저장·올리기)로 열고, count를 올리고, 대기자가 있으면 `xTaskRemoveFromEventList()`로 하나를 ready로 옮긴다. 그 태스크가 **지금 돌던 태스크보다 높으면** `woken`을 `pdTRUE`로 만든다. **컨텍스트 스위치는 하지 않는다.** ISR 안에서는 스위치할 수 없고, 같은 ISR에서 give를 여러 번 할 수도 있기 때문이다.
- `woken`을 `pdFALSE`로 초기화하는 이유: API는 `pdTRUE`로 **올리기만** 하고 `pdFALSE`로 내리지 않는다. 한 ISR에서 여러 FromISR API를 불러도 하나라도 높은 태스크를 깨웠으면 `pdTRUE`로 남는다.
- `portYIELD_FROM_ISR(woken)` = `portEND_SWITCHING_ISR()`(`portable/GCC/ARM_CM3/portmacro.h` 98·111행). `woken`이 참이면 ICSR에 PENDSVSET을 쓴다. PendSV는 가장 낮은 우선순위라 **이 ISR(과 중첩된 다른 ISR)이 다 끝난 직후** 실행되어 깨운 태스크로 전환한다(B4, C2).

ISR에서 태스크용 `xSemaphoreGive()`를 부르면 `vPortEnterCritical()`의 VECTACTIVE 검사(`port.c`)에서 assert로 잡힌다는 것은 A5 예제 8에서 봤다. 그리고 이 ISR의 NVIC 우선순위는 `configMAX_SYSCALL_INTERRUPT_PRIORITY`(템플릿 0x20)보다 **숫자가 커야**(= 덜 급해야) 한다. 이 노트의 예제는 모두 0x40을 쓴다. 그 이유(BASEPRI로 막을 수 있는 IRQ만 커널 API를 부를 수 있다)는 B3·C7, 배치 전략은 E2에서 다룬다.

### 4.2 yield를 빼먹으면 — 그림으로

`portYIELD_FROM_ISR()` 한 줄을 빼먹어도 프로그램은 "동작한다". 태스크는 결국 깨어나니까. 문제는 **언제** 깨어나느냐다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">(a) portYIELD_FROM_ISR(woken) 있음</text>
<text x="10.0" y="48.0" font-size="12" text-anchor="start">IRQ / tick</text>
<text x="10.0" y="78.0" font-size="12" text-anchor="start">waiter (prio 3)</text>
<text x="10.0" y="108.0" font-size="12" text-anchor="start">busy (prio 1)</text>
<rect x="130.0" y="37.0" width="6.0" height="14.0" rx="1" fill="#888" fill-opacity="0.5" stroke="#888" stroke-width="1.3"/>
<rect x="550.0" y="37.0" width="6.0" height="14.0" rx="1" fill="#888" fill-opacity="0.5" stroke="#888" stroke-width="1.3"/>
<rect x="256.0" y="37.0" width="6.0" height="14.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.3"/>
<rect x="136.0" y="96.0" width="120.0" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0" stroke-width="1.3"/>
<rect x="259.8" y="66.0" width="37.8" height="16.0" rx="2" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="298.0" y="96.0" width="336.0" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="306.4" y="78.0" font-size="12" text-anchor="start">ISR 끝에서 바로 PendSV → 9.2 µs 만에 실행</text>
<text x="257.3" y="32.0" font-size="12" text-anchor="middle">give</text>
<text x="10.0" y="160.0" font-size="13" text-anchor="start" font-weight="bold">(b) 빼먹음 — woken을 받고도 버림</text>
<text x="10.0" y="186.0" font-size="12" text-anchor="start">IRQ / tick</text>
<text x="10.0" y="216.0" font-size="12" text-anchor="start">waiter (prio 3)</text>
<text x="10.0" y="246.0" font-size="12" text-anchor="start">busy (prio 1)</text>
<rect x="130.0" y="175.0" width="6.0" height="14.0" rx="1" fill="#888" fill-opacity="0.5" stroke="#888" stroke-width="1.3"/>
<rect x="550.0" y="175.0" width="6.0" height="14.0" rx="1" fill="#888" fill-opacity="0.5" stroke="#888" stroke-width="1.3"/>
<rect x="256.0" y="175.0" width="6.0" height="14.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.3"/>
<rect x="136.0" y="234.0" width="414.0" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0" stroke-width="1.3"/>
<rect x="552.5" y="204.0" width="37.8" height="16.0" rx="2" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b" stroke-width="1.3"/>
<rect x="590.3" y="234.0" width="43.7" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0" stroke-width="1.3"/>
<rect x="258.5" y="204.0" width="291.5" height="16.0" rx="2" fill="#d0564a" fill-opacity="0.08" stroke="#d0564a" stroke-width="1.3" stroke-dasharray="4 3"/>
<text x="403.0" y="216.0" font-size="12" text-anchor="middle">ready인데 못 돈다 (최대 1 tick)</text>
<text x="546.0" y="170.0" font-size="12" text-anchor="end">tick: xYieldPendings → PendSV</text>
<text x="257.3" y="170.0" font-size="12" text-anchor="middle">give</text>
<line x1="130.0" y1="300.0" x2="634.0" y2="300.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="130.0" y1="300.0" x2="130.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="130.0" y="320.0" font-size="12" text-anchor="middle">0</text>
<line x1="214.0" y1="300.0" x2="214.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="214.0" y="320.0" font-size="12" text-anchor="middle">200</text>
<line x1="298.0" y1="300.0" x2="298.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="298.0" y="320.0" font-size="12" text-anchor="middle">400</text>
<line x1="382.0" y1="300.0" x2="382.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="382.0" y="320.0" font-size="12" text-anchor="middle">600</text>
<line x1="466.0" y1="300.0" x2="466.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="466.0" y="320.0" font-size="12" text-anchor="middle">800</text>
<line x1="550.0" y1="300.0" x2="550.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="550.0" y="320.0" font-size="12" text-anchor="middle">1000</text>
<line x1="634.0" y1="300.0" x2="634.0" y2="305.0" stroke="currentColor" stroke-width="1.3"/>
<text x="634.0" y="320.0" font-size="12" text-anchor="middle">1200</text>
<text x="634.0" y="336.0" font-size="12" text-anchor="end">µs</text>
</svg>
```

그림 5 — 낮은 우선순위 busy 태스크가 도는 중에 300 µs에 IRQ가 와서 waiter를 깨운다. (a) yield가 있으면 ISR이 끝나자마자 PendSV로 waiter가 돈다. (b) yield를 빼먹으면 waiter는 ready 리스트에 올라가 있지만 아무도 스위치를 요청하지 않았다. 다음 SysTick(1 ms 경계)에서야 커널이 "미뤄 둔 yield"를 보고 전환한다.

(b)에서 "다음 tick에서는 전환된다"는 것은 FreeRTOS가 넣어 둔 안전망이다. `tasks.c`의 `xTaskRemoveFromEventList()`(5500행)를 보자.

```c
    #if ( configNUMBER_OF_CORES == 1 )
    {
        if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )
        {
            xReturn = pdTRUE;

            /* Mark that a yield is pending in case the user is not using the
             * "xHigherPriorityTaskWoken" parameter to an ISR safe FreeRTOS function. */
            xYieldPendings[ 0 ] = pdTRUE;
        }
```

그리고 tick 처리 함수 `xTaskIncrementTick()`(4831행에서 시작)은 끝에서 `xYieldPendings[ 0 ] != pdFALSE`이면 `xSwitchRequired = pdTRUE`로 만들어 SysTick 핸들러가 PendSV를 걸게 한다(`configUSE_PREEMPTION == 1`일 때). 그러니 yield를 빼먹은 대가는 "영원히 안 깨움"이 아니라 **"다음 tick까지 최대 1 tick 지연"**이다. 다음 tick 전에 다른 커널 API 호출(예: busy 태스크가 `vTaskDelay`)이 스위치를 일으키면 그보다 일찍 깰 수도 있다.

### 4.3 예제 6 — 지연 분포를 직접 재기

무엇을 확인하는 코드인지: TIMER0을 **1.037 ms** 주기로 돌린다(1 ms tick과 어긋나게 해서 IRQ가 tick 사이의 모든 위상에 떨어지게). waiter(우선순위 3)는 take에서 깨어나자마자 `timer0_elapsed()` — IRQ가 뜬 순간부터 지난 TIMER0 카운트 — 를 읽는다. yield 있음/없음 × CPU 한가함/낮은 우선순위 hog가 돎, 네 경우를 200번씩.

```c
/* D1 ex4 — ISR -> task wake latency with and without portYIELD_FROM_ISR */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

#define N 200
static SemaphoreHandle_t sem;
static volatile int use_yield = 1, busy_on;
static uint32_t hist[11];                   /* 100 us bins, last = >= 1000 us */

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woken);     /* woken = pdTRUE: waiter (3) > running task */
    if (use_yield) portYIELD_FROM_ISR(woken);   /* forgetting this line = the bug */
}
static void busy(void *arg) { (void)arg; for (;;) { while (!busy_on) { } } }  /* prio 1 hog */

static void phase(const char *name, int y, int b)
{
    stat_t s = { 0 };
    for (int i = 0; i < 11; i++) hist[i] = 0;
    use_yield = y; busy_on = b;
    timer0_start(1037u * 25u, 0x40);        /* 1.037 ms: drifts against the 1 ms tick */
    for (int i = 0; i < N; i++) {
        xSemaphoreTake(sem, portMAX_DELAY);
        uint32_t lat = timer0_elapsed();    /* cycles since the IRQ was raised */
        stat_add(&s, lat);
        uint32_t bin = lat / 2500u;         /* 2500 cyc = 100 us */
        hist[bin > 10 ? 10 : bin]++;
    }
    timer0_stop(); busy_on = 0;
    stat_print(name, &s);
    board_printf_raw("  hist(100us bins):");
    for (int i = 0; i < 11; i++) board_printf_raw(" %u", (unsigned)hist[i]);
    board_printf_raw("\n");
}
static void waiter(void *arg)               /* prio 3 */
{
    (void)arg;
    phase("yield,    idle CPU ", 1, 0);
    phase("yield,    busy prio1", 1, 1);
    phase("NO yield, idle CPU ", 0, 0);
    phase("NO yield, busy prio1", 0, 1);
    board_exit(0);
}
int main(void)
{
    sem = xSemaphoreCreateBinary();
    xTaskCreate(waiter, "wait", 256, NULL, 3, NULL);
    xTaskCreate(busy, "busy", 128, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex4_yield.c \
  OUT=/tmp/rtos_D1/ex4 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex4/app.elf 60
```

```text
yield,    idle CPU  n=200 min=9200 avg=9200 max=9200 ns
  hist(100us bins): 200 0 0 0 0 0 0 0 0 0 0
yield,    busy prio1 n=200 min=9200 avg=9200 max=9200 ns
  hist(100us bins): 200 0 0 0 0 0 0 0 0 0 0
NO yield, idle CPU  n=200 min=10520 avg=516640 max=1005080 ns
  hist(100us bins): 19 21 14 21 21 18 19 24 24 16 3
NO yield, busy prio1 n=200 min=10520 avg=518960 max=1005440 ns
  hist(100us bins): 17 21 16 19 21 22 18 24 24 14 4
[board_exit] code=0
```

출력에서 볼 것:

- yield가 있으면 200번 모두 **9.2 µs**(min = avg). CPU가 놀든 hog가 돌든 같다 — 우선순위 선점이 제대로 동작하는 모습이다.
- yield를 빼먹으면 **평균 517 µs, 최대 1005 µs**. 히스토그램이 0~1000 µs에 거의 고르게 퍼진다. IRQ 위상이 tick에 대해 균일하게 흩어졌으니 "다음 tick까지 남은 시간"도 균일하다. 1000 µs를 넘는 몇 개는 tick 핸들러 자체와 스위치 시간이 더해진 것이다.
- CPU가 놀 때(idle 태스크)도 똑같이 늦다. idle 태스크도 그냥 "가장 낮은 우선순위 태스크"이고, 아무도 스위치를 요청하지 않았으니 tick까지 돈다.
- **평균이 50배 이상 나빠진다**. 1 kHz rate loop였다면 매 주기 평균 절반을 잃는 셈이다. 그런데 기능 테스트(값이 맞나)는 다 통과한다 — 그래서 이 버그는 오래 숨는다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">ISR → 태스크 지연 분포, portYIELD_FROM_ISR을 빼먹었을 때 (n=200, 실측)</text>
<line x1="70.0" y1="280.0" x2="630.0" y2="280.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70.0" y1="280.0" x2="70.0" y2="84.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="66.0" y1="280.0" x2="70.0" y2="280.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="284.0" font-size="12" text-anchor="end">0</text>
<line x1="66.0" y1="245.0" x2="70.0" y2="245.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="249.0" font-size="12" text-anchor="end">5</text>
<line x1="66.0" y1="210.0" x2="70.0" y2="210.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="214.0" font-size="12" text-anchor="end">10</text>
<line x1="66.0" y1="175.0" x2="70.0" y2="175.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="179.0" font-size="12" text-anchor="end">15</text>
<line x1="66.0" y1="140.0" x2="70.0" y2="140.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="144.0" font-size="12" text-anchor="end">20</text>
<line x1="66.0" y1="105.0" x2="70.0" y2="105.0" stroke="currentColor" stroke-width="1.3"/>
<text x="62.0" y="109.0" font-size="12" text-anchor="end">25</text>
<rect x="75.0" y="161.0" width="40.0" height="119.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="95.0" y="156.0" font-size="12" text-anchor="middle">17</text>
<text x="95.0" y="296.0" font-size="12" text-anchor="middle">0</text>
<rect x="125.0" y="133.0" width="40.0" height="147.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="145.0" y="128.0" font-size="12" text-anchor="middle">21</text>
<text x="145.0" y="296.0" font-size="12" text-anchor="middle">100</text>
<rect x="175.0" y="168.0" width="40.0" height="112.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="195.0" y="163.0" font-size="12" text-anchor="middle">16</text>
<text x="195.0" y="296.0" font-size="12" text-anchor="middle">200</text>
<rect x="225.0" y="147.0" width="40.0" height="133.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="245.0" y="142.0" font-size="12" text-anchor="middle">19</text>
<text x="245.0" y="296.0" font-size="12" text-anchor="middle">300</text>
<rect x="275.0" y="133.0" width="40.0" height="147.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="295.0" y="128.0" font-size="12" text-anchor="middle">21</text>
<text x="295.0" y="296.0" font-size="12" text-anchor="middle">400</text>
<rect x="325.0" y="126.0" width="40.0" height="154.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="345.0" y="121.0" font-size="12" text-anchor="middle">22</text>
<text x="345.0" y="296.0" font-size="12" text-anchor="middle">500</text>
<rect x="375.0" y="154.0" width="40.0" height="126.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="395.0" y="149.0" font-size="12" text-anchor="middle">18</text>
<text x="395.0" y="296.0" font-size="12" text-anchor="middle">600</text>
<rect x="425.0" y="112.0" width="40.0" height="168.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="445.0" y="107.0" font-size="12" text-anchor="middle">24</text>
<text x="445.0" y="296.0" font-size="12" text-anchor="middle">700</text>
<rect x="475.0" y="112.0" width="40.0" height="168.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="495.0" y="107.0" font-size="12" text-anchor="middle">24</text>
<text x="495.0" y="296.0" font-size="12" text-anchor="middle">800</text>
<rect x="525.0" y="182.0" width="40.0" height="98.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="545.0" y="177.0" font-size="12" text-anchor="middle">14</text>
<text x="545.0" y="296.0" font-size="12" text-anchor="middle">900</text>
<rect x="575.0" y="252.0" width="40.0" height="28.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="595.0" y="247.0" font-size="12" text-anchor="middle">4</text>
<text x="595.0" y="296.0" font-size="12" text-anchor="middle">1000+</text>
<text x="340.0" y="316.0" font-size="12" text-anchor="middle">지연 (µs, 100 µs 칸) — 다음 tick(1 ms)까지 기다리므로 0~1 ms에 고르게 퍼진다</text>
<text x="30.0" y="96.0" font-size="12" text-anchor="start">개수</text>
<rect x="400.0" y="36.0" width="260.0" height="46.0" rx="5" fill="#3f9a6b" fill-opacity="0.1" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="530.0" y="54.0" font-size="12" text-anchor="middle">yield를 부르면: 200개 전부 9.2 µs</text>
<text x="530.0" y="72.0" font-size="12" text-anchor="middle">(전부 첫 칸, min = max)</text>
</svg>
```

그림 6 — 실측: yield를 빼먹었을 때 ISR → 태스크 지연 히스토그램(템플릿 기본 설정, hog 있음, n = 200). 0~1 ms에 균일 분포. yield가 있으면 200개 전부 9.2 µs로 첫 칸 하나에 몰린다.

같은 실험을 디버그 옵션을 끈 "lean" 설정(§5.1)으로도 돌렸다.

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile \
  SRCS="/Users/donh/.rtos_scratch/D1/ex4_yield.c /Users/donh/.rtos_scratch/D1/lean/lean.c" \
  OUT=/tmp/rtos_D1/ex4_lean >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex4_lean/app.elf 60
```

```text
yield,    idle CPU  n=200 min=6600 avg=6600 max=6600 ns
  hist(100us bins): 200 0 0 0 0 0 0 0 0 0 0
yield,    busy prio1 n=200 min=6600 avg=6600 max=8080 ns
  hist(100us bins): 200 0 0 0 0 0 0 0 0 0 0
NO yield, idle CPU  n=200 min=9320 avg=507800 max=979320 ns
  hist(100us bins): 22 19 16 21 21 14 23 24 18 22 0
NO yield, busy prio1 n=200 min=7920 avg=504960 max=1003440 ns
  hist(100us bins): 20 21 15 20 21 21 19 24 24 14 1
[board_exit] code=0
```

lean에서는 yield 있음이 6.6 µs로 줄지만(첫 번째 hog 측정에서만 한 번 8.08 µs), yield 없음은 여전히 0~1 ms 균일이다. **설정 튜닝으로 줄일 수 있는 것은 µs 단위, yield 누락이 만드는 것은 ms 단위다.**

### 4.4 Zephyr는 왜 yield가 필요 없나

Zephyr ISR에서는 그냥 `k_sem_give(&sem)`을 부르고 끝이다(§5의 예제 8). 재스케줄 판단을 ISR 출구가 대신 한다. `IRQ_CONNECT()`로 등록한 ISR은 공용 `_isr_wrapper`(`arch/arm/core/cortex_m/isr_wrapper.c`)를 거쳐 불리고, wrapper는 끝에서 `z_arm_exc_exit()`(`exc_exit.c`, 별칭 `z_arm_int_exit`)를 부른다.

```c
void z_arm_exc_exit(void)
{
#ifdef CONFIG_PREEMPT_ENABLED
	if (_kernel.ready_q.cache != _kernel.cpus->current) {
		SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
	}
#endif /* CONFIG_PREEMPT_ENABLED */
	...
}
```

`k_sem_give()`가 대기 스레드를 ready로 만들면 `z_ready_thread()` → `update_cache()`가 `_kernel.ready_q.cache`(다음에 돌 스레드)를 바꿔 둔다. ISR 출구에서 그것이 현재 스레드와 다르면 PendSV를 건다. FreeRTOS의 `woken` + `portYIELD_FROM_ISR` 두 단계를 커널이 자동으로 하는 셈이다. 대가는 모든 ISR이 wrapper를 거친다는 것(A7에서 IRQ → ISR 첫 줄이 FreeRTOS보다 길었던 이유)이고, 이를 건너뛰는 `ISR_DIRECT_DECLARE`/`IRQ_DIRECT_CONNECT`를 쓰면 출구 처리(`ISR_DIRECT_PM()`, 반환값으로 재스케줄 여부)를 직접 챙겨야 한다(E1).

| 항목 | FreeRTOS | Zephyr |
|---|---|---|
| ISR용 API | 이름이 다른 함수 (`…FromISR`) | 같은 함수, 대기 시간은 `K_NO_WAIT`만 |
| 재스케줄 요청 | 앱이 `portYIELD_FROM_ISR(woken)` | ISR 출구(`z_arm_exc_exit`)가 자동 |
| 빼먹었을 때 | 다음 tick까지 지연 (§4.3) | 빼먹을 것이 없다 (direct ISR 제외) |
| ISR에서 take | `xSemaphoreTakeFromISR` (블록 안 함, 드물게 씀) | `k_sem_take(&s, K_NO_WAIT)`만 허용 — 아니면 `__ASSERT` (`CONFIG_ASSERT=y`일 때만 검사) |

---

## 5. 측정 — 세마포어는 얼마나 비싼가 (FreeRTOS vs Zephyr)

### 5.1 비교 조건

"A가 B보다 빠르다"는 대부분 "기본 설정이 다르다"였다는 A7의 교훈 그대로, 설정을 둘씩 놓고 잰다.

- **FreeRTOS 기본** = 랩 템플릿 설정: `configASSERT` 켜짐, `configUSE_QUEUE_SETS 1`, `configUSE_TRACE_FACILITY 1`, `configCHECK_FOR_STACK_OVERFLOW 2`, `configGENERATE_RUN_TIME_STATS 1`.
- **FreeRTOS lean** = 위 다섯 개를 끈 설정. 앱 폴더 `lean/`에 템플릿 `FreeRTOSConfig.h`를 복사해 값만 바꾸고 빈 `lean.c`를 같이 빌드해서 그 폴더가 include 경로에 들어가게 했다(A7과 같은 방법). `configASSERT`는 `( ( void ) sizeof( x ) )`로 정의했다 — 그냥 지우면 `port.c`의 assert에만 쓰이는 지역 변수 `pxVectorTable`이 `-Wunused-variable` 경고를 낸다.
- **Zephyr 기본** = `mps2/an385` 기본 `.config`(`CONFIG_TIMESLICING=y`, `CONFIG_TIMESLICE_SIZE=20`, 100 Hz tick, `CONFIG_ASSERT` 꺼짐).
- **Zephyr slicing off** = `-DCONFIG_TIMESLICING=n`.

### 5.2 예제 7 — FreeRTOS 벤치

무엇을 확인하는 코드인지: 대기자 없는 give/take 1회 비용(1000번 평균), `GiveFromISR`와 `xTaskNotifyGive` 비용, 낮은 태스크의 give → 높은 태스크의 take 반환까지 지연(세마포어 vs task notification), 세마포어 객체 크기와 heap 사용량.

```c
/* D1 ex6 — FreeRTOS cost: uncontended give/take, task->task wake, sizes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

#define N 1000
static SemaphoreHandle_t sem, cnt;
static TaskHandle_t hw;
static volatile uint32_t t_give;
static stat_t s_sem, s_ntf;

static void hi_waiter(void *arg)            /* prio 4: measures give -> take return */
{
    (void)arg;
    for (int i = 0; i < 200; i++) { xSemaphoreTake(sem, portMAX_DELAY); stat_add(&s_sem, CYC() - t_give); }
    for (int i = 0; i < 200; i++) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); stat_add(&s_ntf, CYC() - t_give); }
    vTaskSuspend(NULL);
}
static void lo_giver(void *arg)             /* prio 2 */
{
    (void)arg;
    uint32_t t;
    t = CYC(); for (int i = 0; i < N; i++) xSemaphoreGive(cnt);
    board_printf_raw("give      (no waiter)   %u ns/op\n", (unsigned)((CYC() - t) * 40 / N));
    t = CYC(); for (int i = 0; i < N; i++) xSemaphoreTake(cnt, 0);
    board_printf_raw("take(0)   (count > 0)   %u ns/op\n", (unsigned)((CYC() - t) * 40 / N));
    t = CYC(); for (int i = 0; i < N; i++) xSemaphoreTake(cnt, 0);
    board_printf_raw("take(0)   (count = 0)   %u ns/op\n", (unsigned)((CYC() - t) * 40 / N));
    BaseType_t w;
    t = CYC(); for (int i = 0; i < N; i++) xSemaphoreGiveFromISR(cnt, &w);
    board_printf_raw("GiveFromISR (no waiter) %u ns/op\n", (unsigned)((CYC() - t) * 40 / N));
    for (int i = 0; i < N; i++) xSemaphoreTake(cnt, 0);
    t = CYC(); for (int i = 0; i < N; i++) xTaskNotifyGive(hw);   /* waiter not created yet: */
    board_printf_raw("xTaskNotifyGive (no wait) %u ns/op\n", (unsigned)((CYC() - t) * 40 / N));
    xTaskNotifyStateClear(hw); ulTaskNotifyValueClear(hw, 0xffffffffu);
    vTaskResume(hw);                        /* now let the high-prio waiter run */
    for (int i = 0; i < 200; i++) { t_give = CYC(); xSemaphoreGive(sem); }
    for (int i = 0; i < 200; i++) { t_give = CYC(); xTaskNotifyGive(hw); }
    stat_print("task->task  semaphore  ", &s_sem);
    stat_print("task->task  notify(D5) ", &s_ntf);
    board_printf_raw("sizeof(StaticSemaphore_t)=%u sizeof(StaticQueue_t)=%u\n",
                     (unsigned)sizeof(StaticSemaphore_t), (unsigned)sizeof(StaticQueue_t));
    size_t h0 = xPortGetFreeHeapSize();
    SemaphoreHandle_t x = xSemaphoreCreateBinary();
    board_printf_raw("xSemaphoreCreateBinary() took %u heap bytes\n", (unsigned)(h0 - xPortGetFreeHeapSize()));
    (void)x;
    board_exit(0);
}
int main(void)
{
    sem = xSemaphoreCreateBinary();
    cnt = xSemaphoreCreateCounting(N, 0);
    xTaskCreate(hi_waiter, "hi", 256, NULL, 4, &hw);
    vTaskSuspend(hw);                       /* scheduler not started: just marks suspended */
    xTaskCreate(lo_giver, "lo", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
M="make -s -f /var/rtoslab/templates/freertos-mps2/Makefile"; S=/Users/donh/.rtos_scratch/D1
$M SRCS=$S/ex6_bench.c OUT=/tmp/rtos_D1/ex6 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex6/app.elf 30
$M SRCS="$S/ex6_bench.c $S/lean/lean.c" OUT=/tmp/rtos_D1/ex6_lean >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex6_lean/app.elf 30
```

기본 설정:

```text
give      (no waiter)   2979 ns/op
take(0)   (count > 0)   2147 ns/op
take(0)   (count = 0)   2083 ns/op
GiveFromISR (no waiter) 1633 ns/op
xTaskNotifyGive (no wait) 1987 ns/op
task->task  semaphore   n=200 min=9560 avg=9560 max=11040 ns
task->task  notify(D5)  n=200 min=7720 avg=7760 max=9200 ns
sizeof(StaticSemaphore_t)=84 sizeof(StaticQueue_t)=84
xSemaphoreCreateBinary() took 96 heap bytes
[board_exit] code=0
```

lean 설정:

```text
   text	   data	    bss	    dec	    hex	filename
  14228	    100	 135416	 149744	  248f0	/tmp/rtos_D1/ex6_lean/app.elf
give      (no waiter)   1825 ns/op
take(0)   (count > 0)   1251 ns/op
take(0)   (count = 0)   1185 ns/op
GiveFromISR (no waiter) 1121 ns/op
xTaskNotifyGive (no wait) 1667 ns/op
task->task  semaphore   n=200 min=6560 avg=6560 max=8040 ns
task->task  notify(D5)  n=200 min=5640 avg=5640 max=7120 ns
sizeof(StaticSemaphore_t)=72 sizeof(StaticQueue_t)=72
xSemaphoreCreateBinary() took 80 heap bytes
[board_exit] code=0
```

출력에서 볼 것:

- give 한 번이 기본 2.98 µs, lean 1.83 µs. 25 MHz Cortex-M3 감각으로 75사이클, 45사이클 정도다. assert·queue set·trace 검사가 40%를 차지했다.
- `take(0)`은 count가 있든 없든 거의 같다(2.15 대 2.08 µs). 빈 세마포어 폴링도 공짜가 아니다 — 크리티컬 섹션을 열고 닫는 비용이 대부분이다.
- `GiveFromISR`가 태스크용 `Give`보다 싸다(기본 1.63 대 2.98 µs). 태스크용 `xQueueGenericSend()`는 블록 가능성을 위한 `for` 루프·타임아웃 구조·mutex 상속 검사를 거치고, ISR용은 그런 것이 없다.
- **태스크 → 태스크 깨우기 지연**: 세마포어 9.56 µs, notification 7.72 µs(lean 6.56 대 5.64). notification이 약 15~20% 빠르다. max가 min보다 약 1.5 µs 긴 값은 avg가 min과 같거나 거의 같으니 극소수(처음 한두 번의 일회성 경로로 보인다)다.
- 객체 크기: `StaticSemaphore_t` = `StaticQueue_t` = **84 B**(lean 72 B). 동적 생성은 heap에서 96 B(lean 80 B)를 가져간다 — 84 B를 8바이트 정렬로 올린 88 B + heap_4 블록 헤더 8 B.

### 5.3 예제 8 — Zephyr 벤치 (진짜 TIMER0 IRQ 포함)

무엇을 확인하는 코드인지: 같은 항목을 Zephyr에서. ISR → 스레드는 TIMER0을 `IRQ_CONNECT(8, 2, …)`로 연결해 ISR에서 `k_sem_give()`를 부르고, 깨어난 스레드가 TIMER0 경과 카운트를 읽는다(A7의 측정 방식과 같다).

```c
/* D1 ex5 — Zephyr v4.4.2 mps2/an385: k_sem cost, thread->thread and ISR->thread wake */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#include "zexit.h"
#define REG(a)       (*(volatile uint32_t *)(a))
#define CYC()        REG(0x40028018u)                 /* FPGAIO 25 MHz counter */
#define T0           0x40000000u                      /* CMSDK TIMER0, IRQ 8 */
#define T0_ELAPSED() (REG(T0 + 8) - REG(T0 + 4))
#define N 1000
struct st { uint32_t n, min, max; uint64_t sum; };
static struct st s_tt, s_it;
static volatile uint32_t t_give;
K_SEM_DEFINE(sem, 0, 1);
K_SEM_DEFINE(cnt, 0, K_SEM_MAX_LIMIT);
K_SEM_DEFINE(go, 0, 1);

static void add(struct st *s, uint32_t v)
{
	if (!s->n || v < s->min) s->min = v;
	if (v > s->max) s->max = v;
	s->sum += v; s->n++;
}
static void show(const char *m, struct st *s)
{
	printk("%s n=%u min=%u avg=%u max=%u ns\n", m, s->n, s->min * 40,
	       (uint32_t)(s->sum / s->n) * 40, s->max * 40);
}
static void t0_isr(const void *arg)
{
	ARG_UNUSED(arg);
	REG(T0 + 0xC) = 1;                                /* INTCLR */
	k_sem_give(&sem);                                 /* same API as in a thread */
}
static void hi_fn(void *a, void *b, void *c)          /* prio 1 (higher than main = 5) */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	k_sem_take(&go, K_FOREVER);
	for (int i = 0; i < 200; i++) { k_sem_take(&sem, K_FOREVER); add(&s_tt, CYC() - t_give); }
	IRQ_CONNECT(8, 2, t0_isr, NULL, 0);
	REG(T0 + 8) = 25000 - 1; REG(T0 + 4) = 25000 - 1; irq_enable(8); REG(T0) = 1 | 8;
	for (int i = 0; i < 200; i++) { k_sem_take(&sem, K_FOREVER); add(&s_it, T0_ELAPSED()); }
	REG(T0) = 0;
	k_sem_give(&go);
}
K_THREAD_DEFINE(hi, 1024, hi_fn, NULL, NULL, NULL, 1, 0, 0);

int main(void)                                        /* main thread: prio 0 by default */
{
	k_thread_priority_set(k_current_get(), 5);
	uint32_t t = CYC(); for (int i = 0; i < N; i++) k_sem_give(&cnt);
	printk("k_sem_give (no waiter)  %u ns/op\n", (CYC() - t) * 40 / N);
	t = CYC(); for (int i = 0; i < N; i++) (void)k_sem_take(&cnt, K_NO_WAIT);
	printk("k_sem_take (count > 0)  %u ns/op\n", (CYC() - t) * 40 / N);
	t = CYC(); for (int i = 0; i < N; i++) (void)k_sem_take(&cnt, K_NO_WAIT);
	printk("k_sem_take (count = 0)  %u ns/op\n", (CYC() - t) * 40 / N);
	k_sem_give(&go);                                  /* hi runs and blocks on sem */
	for (int i = 0; i < 200; i++) { t_give = CYC(); k_sem_give(&sem); }
	k_sem_take(&go, K_FOREVER);
	show("thread->thread k_sem ", &s_tt);
	show("ISR->thread    k_sem ", &s_it);
	printk("sizeof(struct k_sem)=%u\n", (unsigned)sizeof(struct k_sem));
	z_exit();
	return 0;
}
```

```sh
for v in "default:" "ts_off:-DCONFIG_TIMESLICING=n"; do n=${v%%:*}; a=${v#*:}; echo "== $n"; \
  /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D1/z_bench /tmp/rtos_D1/zb_$n -- $a && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/zb_$n/zephyr/zephyr.elf 10; done
```

```text
== default
           FLASH:       14876 B         4 MB      0.35%
             RAM:        7368 B         4 MB      0.18%
k_sem_give (no waiter)  640 ns/op
k_sem_take (count > 0)  672 ns/op
k_sem_take (count = 0)  704 ns/op
thread->thread k_sem  n=200 min=18840 avg=18840 max=18880 ns
ISR->thread    k_sem  n=200 min=13680 avg=13680 max=13680 ns
sizeof(struct k_sem)=16
== ts_off
           FLASH:       14432 B         4 MB      0.34%
             RAM:        7328 B         4 MB      0.17%
k_sem_give (no waiter)  640 ns/op
k_sem_take (count > 0)  672 ns/op
k_sem_take (count = 0)  704 ns/op
thread->thread k_sem  n=200 min=4800 avg=4800 max=4840 ns
ISR->thread    k_sem  n=200 min=4880 avg=4880 max=4880 ns
sizeof(struct k_sem)=16
```

출력에서 볼 것:

- **give 0.64 µs, take 0.67 µs** — FreeRTOS lean보다도 2~3배 싸다. 설정(time slicing)과 무관하다. 객체는 **16 B**.
- **깨우기 지연은 time slicing이 지배한다.** 기본 설정에서 스레드 → 스레드 18.84 µs, ISR → 스레드 13.68 µs인데, slicing을 끄면 둘 다 4.8~4.9 µs로 FreeRTOS lean(6.6 µs)보다 빠르다. A7 §3.1에서 찾은 원인 그대로다: 스레드를 전환할 때마다 `update_cache()` → `z_reset_time_slice()`가 slice 타임아웃을 지웠다 다시 걸고, tickless 커널은 그때마다 SysTick을 다시 프로그래밍한다. 기본 설정에서 스레드 → 스레드가 ISR → 스레드보다 5 µs 더 긴 이유는 이 노트에서 추적하지 않았다(확인 못 함 — 스레드 문맥 `z_reschedule()` 경로와 ISR 출구 경로의 slice 재설정 차이로 추정).
- ISR → 스레드 측정도 FreeRTOS와 같은 진짜 하드웨어 IRQ다. 결과는 매번 같은 값(min = max)이다.

### 5.4 한 장으로

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">세마포어 비용 — 실측 (QEMU mps2-an385, icount 32 ns/명령)</text>
<line x1="60.0" y1="270.0" x2="380.0" y2="270.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="60.0" y1="270.0" x2="60.0" y2="54.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="56.0" y1="270.0" x2="60.0" y2="270.0" stroke="currentColor" stroke-width="1.3"/>
<text x="53.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="56.0" y1="217.5" x2="60.0" y2="217.5" stroke="currentColor" stroke-width="1.3"/>
<text x="53.0" y="221.5" font-size="12" text-anchor="end">5</text>
<line x1="56.0" y1="165.0" x2="60.0" y2="165.0" stroke="currentColor" stroke-width="1.3"/>
<text x="53.0" y="169.0" font-size="12" text-anchor="end">10</text>
<line x1="56.0" y1="112.5" x2="60.0" y2="112.5" stroke="currentColor" stroke-width="1.3"/>
<text x="53.0" y="116.5" font-size="12" text-anchor="end">15</text>
<line x1="56.0" y1="60.0" x2="60.0" y2="60.0" stroke="currentColor" stroke-width="1.3"/>
<text x="53.0" y="64.0" font-size="12" text-anchor="end">20</text>
<text x="30.0" y="48.0" font-size="12" text-anchor="start">µs</text>
<rect x="80.0" y="173.4" width="32.0" height="96.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="96.0" y="169.4" font-size="12" text-anchor="middle">9.2</text>
<rect x="115.0" y="200.7" width="32.0" height="69.3" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="131.0" y="196.7" font-size="12" text-anchor="middle">6.6</text>
<rect x="150.0" y="126.4" width="32.0" height="143.6" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="166.0" y="122.4" font-size="12" text-anchor="middle">13.7</text>
<rect x="185.0" y="218.8" width="32.0" height="51.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="201.0" y="214.8" font-size="12" text-anchor="middle">4.9</text>
<text x="149.0" y="287.0" font-size="12" text-anchor="middle">ISR → 태스크</text>
<rect x="240.0" y="169.6" width="32.0" height="100.4" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="256.0" y="165.6" font-size="12" text-anchor="middle">9.6</text>
<rect x="275.0" y="201.1" width="32.0" height="68.9" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="291.0" y="197.1" font-size="12" text-anchor="middle">6.6</text>
<rect x="310.0" y="72.2" width="32.0" height="197.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="326.0" y="68.2" font-size="12" text-anchor="middle">18.8</text>
<rect x="345.0" y="219.6" width="32.0" height="50.4" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="361.0" y="215.6" font-size="12" text-anchor="middle">4.8</text>
<text x="309.0" y="287.0" font-size="12" text-anchor="middle">태스크 → 태스크</text>
<text x="220.0" y="306.0" font-size="12" text-anchor="middle">give → take 반환까지 (깨우기 지연)</text>
<line x1="440.0" y1="270.0" x2="670.0" y2="270.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="440.0" y1="270.0" x2="440.0" y2="54.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="436.0" y1="270.0" x2="440.0" y2="270.0" stroke="currentColor" stroke-width="1.3"/>
<text x="433.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="436.0" y1="210.0" x2="440.0" y2="210.0" stroke="currentColor" stroke-width="1.3"/>
<text x="433.0" y="214.0" font-size="12" text-anchor="end">1</text>
<line x1="436.0" y1="150.0" x2="440.0" y2="150.0" stroke="currentColor" stroke-width="1.3"/>
<text x="433.0" y="154.0" font-size="12" text-anchor="end">2</text>
<line x1="436.0" y1="90.0" x2="440.0" y2="90.0" stroke="currentColor" stroke-width="1.3"/>
<text x="433.0" y="94.0" font-size="12" text-anchor="end">3</text>
<rect x="452.0" y="91.2" width="32.0" height="178.8" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="468.0" y="87.2" font-size="12" text-anchor="middle">2.98</text>
<rect x="487.0" y="160.2" width="32.0" height="109.8" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="503.0" y="156.2" font-size="12" text-anchor="middle">1.83</text>
<rect x="522.0" y="231.6" width="32.0" height="38.4" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="538.0" y="227.6" font-size="12" text-anchor="middle">0.64</text>
<text x="503.5" y="287.0" font-size="12" text-anchor="middle">give</text>
<rect x="560.0" y="141.0" width="32.0" height="129.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="576.0" y="137.0" font-size="12" text-anchor="middle">2.15</text>
<rect x="595.0" y="195.0" width="32.0" height="75.0" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="611.0" y="191.0" font-size="12" text-anchor="middle">1.25</text>
<rect x="630.0" y="229.8" width="32.0" height="40.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="646.0" y="225.8" font-size="12" text-anchor="middle">0.67</text>
<text x="611.5" y="287.0" font-size="12" text-anchor="middle">take</text>
<text x="555.0" y="306.0" font-size="12" text-anchor="middle">대기자 없을 때 1회 비용 (µs)</text>
<rect x="20.0" y="334.0" width="14.0" height="12.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="40.0" y="345.0" font-size="12" text-anchor="start">FreeRTOS 기본</text>
<rect x="175.0" y="334.0" width="14.0" height="12.0" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="195.0" y="345.0" font-size="12" text-anchor="start">FreeRTOS lean</text>
<rect x="330.0" y="334.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="350.0" y="345.0" font-size="12" text-anchor="start">Zephyr 기본</text>
<rect x="470.0" y="334.0" width="14.0" height="12.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="490.0" y="345.0" font-size="12" text-anchor="start">Zephyr slicing off</text>
</svg>
```

그림 7 — 실측 정리(QEMU icount). 왼쪽: give → take 반환까지 깨우기 지연. 오른쪽: 대기자 없을 때 give·take 1회 비용(Zephyr는 두 설정에서 같아서 하나로 그렸다).

| 항목 (QEMU icount, 25 MHz M3 기준) | FreeRTOS 기본 | FreeRTOS lean | Zephyr 기본 | Zephyr slicing off |
|---|---|---|---|---|
| ISR → 태스크 (세마포어) | 9.20 µs | 6.60 µs | 13.68 µs | 4.88 µs |
| 태스크 → 태스크 (세마포어) | 9.56 µs | 6.56 µs | 18.84 µs | 4.80 µs |
| 태스크 → 태스크 (notification, D5) | 7.72 µs | 5.64 µs | — | — |
| give (대기자 없음) | 2.98 µs | 1.83 µs | 0.64 µs | 0.64 µs |
| take (count > 0) | 2.15 µs | 1.25 µs | 0.67 µs | 0.67 µs |
| 세마포어 RAM | 84 B (+heap 헤더 → 96 B) | 72 B (80 B) | 16 B | 16 B |

결론 세 줄:

1. FreeRTOS 세마포어가 무거운 이유는 "항목 크기 0인 **큐**"라서다(§8). 큐의 일반성(블록되는 송신자 리스트, 큐 셋, mutex 상속 분기)을 세마포어도 다 들고 다닌다. FreeRTOS 문서가 `semphr.h` 곳곳에 "In many usage scenarios it is faster and more memory efficient to use a direct to task notification in place of a binary semaphore"라고 적어 둔 이유다(D5).
2. Zephyr `k_sem`은 count·limit·대기 큐만 있는 전용 객체라 작고 싸다. 대신 **모든 세마포어가 전역 spinlock 하나를 공유한다**(`kernel/sem.c` 주석: "We use a system-wide lock to synchronize semaphores, which has unfortunate performance impact vs. using a per-object lock"). 단일 코어에서는 그 lock이 `irq_lock`(BASEPRI) 한 번이라 상관없지만, SMP(C10)에서는 서로 무관한 세마포어끼리 경합한다.
3. 1 kHz rate loop에서 5~20 µs는 주기의 0.5~2%다. 세마포어 비용 자체보다 **yield 누락(최대 1000 µs)이나 time slicing 설정(+14 µs)** 같은 사용·설정 실수가 몇 배 크다.

---

## 6. "세마포어로 상호배제하면 안 되는 이유"

### 6.1 겉보기엔 똑같다

binary 세마포어를 1로 시작하면 take = lock, give = unlock처럼 쓸 수 있다. FreeRTOS에서는 mutex도 `xSemaphoreCreateMutex()`로 만들고 같은 `xSemaphoreTake`/`xSemaphoreGive`를 쓴다. 이름까지 같으니 "binary 세마포어 = mutex"라고 생각하기 쉽다. 하지만 mutex에는 세마포어에 없는 것이 두 가지 있다.

| 성질 | binary 세마포어 | mutex |
|---|---|---|
| 소유권(owner) | 없음 — 누구든 give할 수 있다 | 있음 — take한 태스크만 give할 수 있다 |
| 우선순위 상속 (priority inheritance) | 없음 | 있음 — 높은 태스크가 기다리면 소유자의 우선순위를 올린다 |
| ISR에서 give | 된다 (그래서 신호용으로 쓴다) | 안 된다 (소유자가 ISR일 수 없다) |
| 재귀 take | 자기 자신과 데드락 | recursive mutex면 된다 (D2) |
| 디버그 도구가 "누가 쥐고 있나" | 알 수 없다 | `xSemaphoreGetMutexHolder()` 등으로 안다 |

"소유권이 없다"는 것이 신호용에서는 **장점**(ISR이 주고 태스크가 받는다)이고, 잠금용에서는 **결함**이다. 같은 성질이 쓰임에 따라 장점도 결함도 된다.

### 6.2 예제 9 — 같은 코드를 세마포어와 mutex로

무엇을 확인하는 코드인지: (1부) 낮은 L(우선순위 1)이 잠금을 쥔 채 일하는 중에 높은 H(3)가 같은 잠금을 기다리고, 무관한 중간 M(2)이 2 ms에 깨어나 5 ms 동안 CPU를 쓴다. H가 얼마나 기다리나, 그동안 L의 우선순위는 몇인가. (2부) L이 다시 잠금을 쥔 동안, H의 "정리 코드"가 take한 적 없는 잠금을 give한다. `-DUSE_MUTEX`면 같은 코드를 mutex로.

```c
/* D1 ex7 — why a binary semaphore is not a mutex: no PI, anyone can give. -DUSE_MUTEX to compare */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

static SemaphoreHandle_t lock;
static volatile int in_cs, max_in_cs;
static void work_us(uint32_t us) { for (uint32_t i = 0; i < us / 100; i++) spin_cyc(2500); } /* ~CPU time */
static unsigned now_ms(void) { return (unsigned)(board_now_us() / 1000); }

static void enter(const char *who)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (++in_cs > max_in_cs) max_in_cs = in_cs;
    board_printf_raw("[%u ms] %s in CS (in_cs=%d)\n", now_ms(), who, in_cs);
}
static void leave(void) { in_cs--; xSemaphoreGive(lock); }

static void H(void *a)                       /* prio 3 */
{
    (void)a;
    vTaskDelay(1);
    uint32_t t = board_now_us();
    enter("H");                              /* L holds it -> H blocks */
    board_printf_raw("H waited %u us for the lock\n", (unsigned)(board_now_us() - t));
    leave();
    vTaskDelay(10);                          /* part 2: L holds the lock again */
    board_printf_raw("[%u ms] buggy cleanup path: H gives a lock it never took -> ", now_ms());
    board_printf_raw("%s\n", xSemaphoreGive(lock) == pdPASS ? "pdPASS" : "fail");
    enter("H"); vTaskDelay(2); leave();
    board_printf_raw("max tasks inside CS at once = %d\n", max_in_cs);
    board_exit(0);
}
static void M(void *a)                       /* prio 2: unrelated CPU hog, wakes at 2 ms */
{
    (void)a;
    vTaskDelay(2);
    work_us(5000);
    vTaskSuspend(NULL);
}
static void L(void *a)                       /* prio 1 */
{
    (void)a;
    enter("L");
    work_us(1500);
    board_printf_raw("[%u ms] L inside CS, H is waiting: L prio = %u\n", now_ms(),
                     (unsigned)uxTaskPriorityGet(NULL));
    work_us(1500);
    leave();
    vTaskDelay(4);
    enter("L"); vTaskDelay(8); leave();      /* part 2: long hold */
    vTaskSuspend(NULL);
}
int main(void)
{
#ifdef USE_MUTEX
    lock = xSemaphoreCreateMutex();
#else
    lock = xSemaphoreCreateBinary(); xSemaphoreGive(lock);   /* binary used as a "lock" */
#endif
    xTaskCreate(H, "H", 256, NULL, 3, NULL);
    xTaskCreate(M, "M", 256, NULL, 2, NULL);
    xTaskCreate(L, "L", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
for v in "" "-DUSE_MUTEX"; do echo "== ${v:-binary}"; \
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex7_notmutex.c \
  OUT=/tmp/rtos_D1/ex7$v EXTRA_CFLAGS="$v" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex7$v/app.elf 10; done
```

```text
== binary
[0 ms] L in CS (in_cs=1)
[1 ms] L inside CS, H is waiting: L prio = 1
[8 ms] H in CS (in_cs=1)
H waited 7043 us for the lock
[12 ms] L in CS (in_cs=1)
[18 ms] buggy cleanup path: H gives a lock it never took -> pdPASS
[18 ms] H in CS (in_cs=2)
max tasks inside CS at once = 2
[board_exit] code=0
== -DUSE_MUTEX
[0 ms] L in CS (in_cs=1)
[1 ms] L inside CS, H is waiting: L prio = 3
[3 ms] H in CS (in_cs=1)
H waited 2091 us for the lock
[12 ms] L in CS (in_cs=1)
[13 ms] buggy cleanup path: H gives a lock it never took -> 
[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:6861
[board_exit] code=253
```

출력에서 볼 것:

- **우선순위 상속이 없다.** binary에서는 H가 기다리는 동안 L의 우선순위가 1 그대로이고, 그래서 M(2)이 L을 밀어내고 5 ms를 다 쓴다. H는 L의 남은 일(약 2 ms)이 아니라 **M의 일까지 포함한 7.0 ms**를 기다렸다. mutex에서는 L이 3으로 올라가(상속) M이 끼어들지 못하고, H는 2.1 ms만 기다렸다. 이것이 우선순위 역전(priority inversion)의 가장 작은 모형이다. 무한 역전, Mars Pathfinder, 상속과 ceiling의 비교는 D3에서 다룬다.
- **소유권이 없다.** binary에서 H가 쥔 적 없는 잠금을 give하자 `pdPASS`로 성공했고, H는 L이 아직 안에 있는데 들어갔다 — `in_cs=2`, 두 태스크가 동시에 임계 구역 안에 있다. 공유 SPI 버스였다면 두 전송이 섞였을 것이다. mutex에서는 같은 give가 `tasks.c:6861`의 assert에 걸렸다. 그 줄은 `xTaskPriorityDisinherit()` 안의 `configASSERT( pxTCB == pxCurrentTCB );` — "mutex를 돌려주는 태스크는 지금 돌고 있는 소유자여야 한다"는 검사다.

```c
        if( pxMutexHolder != NULL )
        {
            /* A task can only have an inherited priority if it holds the mutex.
             * If the mutex is held by a task then it cannot be given from an
             * interrupt, and if a mutex is given by the holding task then it must
             * be the running state task. */
            configASSERT( pxTCB == pxCurrentTCB );
```

주의: 이 소유권 검사는 `configASSERT`가 켜져 있을 때만 동작한다. 양산 빌드에서 assert를 끄면 FreeRTOS mutex도 남의 give를 막지 못한다. Zephyr `k_mutex_unlock()`은 소유자가 아니면 **항상** `-EPERM`을 반환한다(`kernel/mutex.c`). 어느 쪽이든, 세마포어에는 이런 검사가 아예 없다.

### 6.3 이중 give — 소유권이 없어서 생기는 또 하나의 버그

2부의 버그는 실무에서 이런 모양으로 나온다. 에러 처리 경로가 두 군데서 정리를 하고, 둘 다 `give`를 부른다.

```c
if (spi_xfer(buf) != 0) {
    xSemaphoreGive(bus_lock);        /* 에러 경로에서 한 번 */
    goto fail;
}
...
fail:
    xSemaphoreGive(bus_lock);        /* 공통 정리에서 또 한 번 */
```

binary 세마포어는 두 번째 give를 `errQUEUE_FULL`로 버리니 "괜찮아 보인다". 그러나 두 give 사이에 다른 태스크가 잠금을 가져갔다면, 두 번째 give는 **그 태스크의 잠금을 풀어 버린다** — 예제 9의 `in_cs=2`가 정확히 그 상황이다. counting 세마포어(max 2 이상)를 잠금으로 썼다면 더 나쁘다. 두 번째 give가 count를 2로 만들어 그 뒤로는 항상 두 명이 동시에 들어간다. mutex였다면 소유자 검사에서 즉시 드러났을 버그다.

### 6.4 그래서 규칙

- **상호배제(공유 자원 하나를 독점) = mutex**(D2). 우선순위 상속과 소유권 검사가 공짜로 따라온다.
- **ISR이 끼는 짧은 공유 변수 보호 = 크리티컬 섹션**(C7). mutex는 ISR에서 못 쓴다.
- **신호 = 세마포어 또는 notification**(D5). 여기에는 소유권이 없어야 한다.
- **N개 동일 자원 = counting 세마포어**(§7). 이때도 "어느 것"을 고르는 부분은 따로 보호해야 한다.

---

## 7. 자원 계수 — SPI DMA 버퍼 풀

### 7.1 예제 10 — 버퍼 4개, 태스크 3개, 완료 IRQ 하나

드론 비행 제어기에서 OSD·텔레메트리·블랙박스 로거가 같은 SPI 버스로 프레임을 보낸다고 하자. DMA 버퍼는 4개뿐이다. 세 태스크가 버퍼를 하나씩 빌려(take) 프레임을 채우고 DMA를 시작한다. SPI DMA 완료 인터럽트가 버퍼를 풀에 반납(give)한다. 여기서 "SPI DMA 완료"는 300 µs마다 뜨는 TIMER0 IRQ로 흉내 낸다.

무엇을 확인하는 코드인지: counting 세마포어(max 4, 초기 4)가 동시에 날아가는 버퍼를 정확히 4개로 제한하는가, "count"와 "어느 버퍼"가 어긋나지 않는가, 높은 우선순위 태스크와 낮은 태스크의 대기 시간이 어떻게 다른가.

```c
/* D1 ex8 — counting semaphore = "free SPI DMA buffers" (4), DMA-complete IRQ returns them */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

#define NBUF 4
static SemaphoreHandle_t pool;               /* count = number of free buffers */
static uint8_t bufs[NBUF][256];
static volatile uint32_t free_mask = (1u << NBUF) - 1;   /* WHICH ones are free */
static volatile uint8_t fifo[8], head, tail;  /* buffers "on the wire", in order */
static volatile uint32_t max_inflight, bad;

void TIMER0_IRQHandler(void)                  /* "SPI DMA complete" every 300 us */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (head == tail) return;                 /* nothing in flight */
    uint8_t i = fifo[tail++ & 7];
    free_mask |= 1u << i;                     /* ISR: tasks cannot preempt us here */
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(pool, &woken);
    portYIELD_FROM_ISR(woken);
}
static uint8_t *buf_get(void)
{
    xSemaphoreTake(pool, portMAX_DELAY);      /* guarantees: at least one bit is set */
    taskENTER_CRITICAL();                     /* ...but not WHICH: still need the lock */
    uint8_t i = (uint8_t)__builtin_ctz(free_mask);
    free_mask &= ~(1u << i);
    fifo[head++ & 7] = i;                     /* "start DMA" */
    uint32_t n = (uint8_t)(head - tail); if (n > max_inflight) max_inflight = n;
    if (n + (unsigned)__builtin_popcount(free_mask) != NBUF) bad++;
    taskEXIT_CRITICAL();
    return bufs[i];
}
static void producer(void *arg)
{
    const char *name = arg;
    uint32_t blocked = 0, worst = 0;
    for (int k = 0; k < 50; k++) {
        if (uxSemaphoreGetCount(pool) == 0) blocked++;
        uint32_t t = CYC();
        uint8_t *b = buf_get();
        if (CYC() - t > worst) worst = CYC() - t;
        b[0] = (uint8_t)k; spin_cyc(25 * 40); /* 40 us to fill the frame */
    }
    board_printf_raw("%s: 50 frames, had to wait %u times, worst wait %u us\n", name,
                     (unsigned)blocked, (unsigned)(worst / 25));
    vTaskSuspend(NULL);
}
static void report(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(100));
    board_printf_raw("max in flight=%u (NBUF=%u), invariant violations=%u, free now=%u\n",
                     (unsigned)max_inflight, NBUF, (unsigned)bad, (unsigned)uxSemaphoreGetCount(pool));
    board_exit(0);
}
int main(void)
{
    pool = xSemaphoreCreateCounting(NBUF, NBUF);   /* all 4 free at start */
    timer0_start(300u * 25u, 0x40);
    xTaskCreate(producer, "telem", 256, "telemetry", 3, NULL);
    xTaskCreate(producer, "log",   256, "logger   ", 2, NULL);
    xTaskCreate(producer, "osd",   256, "osd      ", 2, NULL);
    xTaskCreate(report,   "rep",   256, NULL, 4, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex8_pool.c \
  OUT=/tmp/rtos_D1/ex8 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex8/app.elf 10
```

```text
telemetry: 50 frames, had to wait 45 times, worst wait 258 us
logger   : 50 frames, had to wait 50 times, worst wait 13660 us
osd      : 50 frames, had to wait 50 times, worst wait 13941 us
max in flight=4 (NBUF=4), invariant violations=0, free now=4
[board_exit] code=0
```

출력에서 볼 것:

- **동시에 날아간 버퍼는 최대 4개**, "날아가는 수 + 빈 비트 수 = 4" 불변식 위반 0번, 끝나고 count 4. counting 세마포어가 개수를 정확히 지켰다.
- **세마포어는 "몇 개"만 알지 "어느 것"은 모른다.** take가 성공하면 빈 버퍼가 **적어도 하나** 있다는 것만 보장된다. 어느 비트가 비었는지 찾고 차지하는 부분(`free_mask`)은 여전히 크리티컬 섹션이 필요했다(태스크 셋이 동시에 고를 수 있으니까). 반납하는 ISR 쪽은 태스크가 끼어들 수 없으니 그냥 비트를 켠다. 순서도 중요하다: ISR은 **비트를 먼저 켜고 give**한다. 반대로 하면 give로 깨어난 태스크가 아직 꺼져 있는 마스크를 보고 `__builtin_ctz(0)`(정의되지 않은 동작)을 부를 수 있다.
- **우선순위 순으로 받는다.** 버퍼가 300 µs마다 하나씩 돌아오는데, 기다리는 태스크 중 가장 높은 telemetry(3)가 매번 먼저 받는다. telemetry의 최악 대기는 258 µs(반납 한 번)인데, 같은 우선순위 2인 logger·osd는 **13.7~13.9 ms**를 기다렸다 — telemetry가 50 프레임을 다 보낼 때까지 거의 굶었다. FreeRTOS 이벤트 리스트가 우선순위 순서이고(C1, A5 §7.1) give가 깨운 뒤 다시 경쟁시키는 방식(§1.6)이기 때문이다. 공정성이 필요하면 태스크마다 버퍼 몫을 나누거나(세마포어 여러 개), 높은 쪽의 속도를 제한한다.

### 7.2 Don의 SSD 경험으로 번역하면

SSD 컨트롤러의 "free buffer pool + 슬롯 카운터"와 같다. 펌웨어는 보통 비트맵 하나로 슬롯을 관리하고 `popcount`로 남은 개수를 셌다. RTOS의 counting 세마포어는 그 **개수 쪽**을 커널 객체로 만든 것이고, 덤으로 "0이면 잠들었다가 반납되면 깨워 주는" 대기 기능이 붙었다. 슬롯을 고르는 비트맵 쪽은 여전히 펌웨어 몫이다. 자원이 여러 종류이고 "어느 것"까지 함께 넘겨야 하면 세마포어 + 비트맵 대신 **빈 버퍼 포인터를 담은 큐**(D4)가 더 깔끔하다 — 큐에서 꺼내는 순간 "몇 개"와 "어느 것"이 동시에 해결된다.

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나

A5 §7이 "give 경로"를 세 커널에서 따라갔다. 여기서는 그 나머지 — 객체의 모양, take 경로, ISR과 태스크가 같은 세마포어를 동시에 건드릴 때의 큐 잠금 — 을 본다.

### 8.1 FreeRTOS: 세마포어 = 항목 크기 0인 큐

`include/semphr.h`의 생성 매크로가 전부를 말한다.

```c
#define semSEMAPHORE_QUEUE_ITEM_LENGTH      ( ( UBaseType_t ) 0U )
#define xSemaphoreCreateBinary()    xQueueGenericCreate( ( UBaseType_t ) 1, semSEMAPHORE_QUEUE_ITEM_LENGTH, queueQUEUE_TYPE_BINARY_SEMAPHORE )
#define xSemaphoreCreateCounting( uxMaxCount, uxInitialCount )    xQueueCreateCountingSemaphore( ( uxMaxCount ), ( uxInitialCount ) )
#define xSemaphoreTake( xSemaphore, xBlockTime )    xQueueSemaphoreTake( ( xSemaphore ), ( xBlockTime ) )
#define xSemaphoreGive( xSemaphore )    xQueueGenericSend( ( QueueHandle_t ) ( xSemaphore ), NULL, semGIVE_BLOCK_TIME, queueSEND_TO_BACK )
#define xSemaphoreGiveFromISR( xSemaphore, pxHigherPriorityTaskWoken )    xQueueGiveFromISR( ( QueueHandle_t ) ( xSemaphore ), ( pxHigherPriorityTaskWoken ) )
#define uxSemaphoreGetCount( xSemaphore )           uxQueueMessagesWaiting( ( QueueHandle_t ) ( xSemaphore ) )
```

binary는 길이 1, 항목 크기 0인 큐다. counting은 `queue.c`의 `xQueueCreateCountingSemaphore()`가 길이 `uxMaxCount`, 항목 크기 0인 큐를 만들고 `uxMessagesWaiting = uxInitialCount`로 채운다. 즉 **세마포어의 count = 큐에 "들어 있는 항목 수"**, max = 큐 길이다. 항목 크기가 0이라 `xQueueGenericCreate()`는 저장 공간 0바이트를 붙여 `sizeof( Queue_t )`만 할당한다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20.0" y="24.0" font-size="13" text-anchor="start" font-weight="bold">FreeRTOS Queue_t를 세마포어로 쓸 때 (84 B)</text>
<rect x="20.0" y="36.0" width="300.0" height="22.0" rx="0" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="28.0" y="51.0" font-size="12" text-anchor="start">pcHead</text>
<text x="312.0" y="51.0" font-size="12" text-anchor="end">4</text>
<rect x="20.0" y="58.0" width="300.0" height="22.0" rx="0" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="28.0" y="73.0" font-size="12" text-anchor="start">pcWriteTo</text>
<text x="312.0" y="73.0" font-size="12" text-anchor="end">4</text>
<rect x="20.0" y="80.0" width="300.0" height="22.0" rx="0" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="28.0" y="95.0" font-size="12" text-anchor="start">u.xSemaphore (holder, 재귀 횟수)</text>
<text x="312.0" y="95.0" font-size="12" text-anchor="end">8</text>
<rect x="20.0" y="102.0" width="300.0" height="22.0" rx="0" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="28.0" y="117.0" font-size="12" text-anchor="start">xTasksWaitingToSend (List_t)</text>
<text x="312.0" y="117.0" font-size="12" text-anchor="end">20</text>
<rect x="20.0" y="124.0" width="300.0" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="28.0" y="139.0" font-size="12" text-anchor="start">xTasksWaitingToReceive (List_t)</text>
<text x="312.0" y="139.0" font-size="12" text-anchor="end">20</text>
<text x="330.0" y="139.0" font-size="12" text-anchor="start">← take에서 잠든 태스크들</text>
<rect x="20.0" y="146.0" width="300.0" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="28.0" y="161.0" font-size="12" text-anchor="start">uxMessagesWaiting</text>
<text x="312.0" y="161.0" font-size="12" text-anchor="end">4</text>
<text x="330.0" y="161.0" font-size="12" text-anchor="start">← count (세마포어 값)</text>
<rect x="20.0" y="168.0" width="300.0" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="28.0" y="183.0" font-size="12" text-anchor="start">uxLength</text>
<text x="312.0" y="183.0" font-size="12" text-anchor="end">4</text>
<text x="330.0" y="183.0" font-size="12" text-anchor="start">← max (binary = 1)</text>
<rect x="20.0" y="190.0" width="300.0" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="28.0" y="205.0" font-size="12" text-anchor="start">uxItemSize</text>
<text x="312.0" y="205.0" font-size="12" text-anchor="end">4</text>
<text x="330.0" y="205.0" font-size="12" text-anchor="start">← 0 (복사할 데이터 없음)</text>
<rect x="20.0" y="212.0" width="300.0" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="28.0" y="227.0" font-size="12" text-anchor="start">cRxLock, cTxLock, static, pad</text>
<text x="312.0" y="227.0" font-size="12" text-anchor="end">4</text>
<text x="330.0" y="227.0" font-size="12" text-anchor="start">← 잠긴 동안 ISR이 준 횟수</text>
<rect x="20.0" y="234.0" width="300.0" height="22.0" rx="0" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="28.0" y="249.0" font-size="12" text-anchor="start">pxQueueSetContainer</text>
<text x="312.0" y="249.0" font-size="12" text-anchor="end">4</text>
<text x="330.0" y="249.0" font-size="12" text-anchor="start">configUSE_QUEUE_SETS</text>
<rect x="20.0" y="256.0" width="300.0" height="22.0" rx="0" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="28.0" y="271.0" font-size="12" text-anchor="start">uxQueueNumber, ucQueueType</text>
<text x="312.0" y="271.0" font-size="12" text-anchor="end">8</text>
<text x="330.0" y="271.0" font-size="12" text-anchor="start">configUSE_TRACE_FACILITY</text>
<text x="20.0" y="298.0" font-size="12" text-anchor="start">회색 = 세마포어 동작에는 안 쓰는 칸 (큐·mutex 전용 또는 설정 옵션)</text>
<text x="20.0" y="316.0" font-size="12" text-anchor="start">lean 설정(queue set·trace 끔)이면 72 B, heap_4에서는 헤더 포함 80 B</text>
<text x="480.0" y="24.0" font-size="13" text-anchor="start" font-weight="bold">Zephyr struct k_sem (16 B)</text>
<rect x="480.0" y="36.0" width="190.0" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="488.0" y="51.0" font-size="12" text-anchor="start">wait_q (sys_dlist_t)</text>
<text x="662.0" y="51.0" font-size="12" text-anchor="end">8</text>
<rect x="480.0" y="58.0" width="190.0" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="488.0" y="73.0" font-size="12" text-anchor="start">count</text>
<text x="662.0" y="73.0" font-size="12" text-anchor="end">4</text>
<rect x="480.0" y="80.0" width="190.0" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="488.0" y="95.0" font-size="12" text-anchor="start">limit</text>
<text x="662.0" y="95.0" font-size="12" text-anchor="end">4</text>
<text x="480.0" y="120.0" font-size="12" text-anchor="start">CONFIG_POLL이면 +8 B</text>
</svg>
```

그림 8 — 같은 Cortex-M3 빌드에서 세마포어 하나가 차지하는 RAM. FreeRTOS `Queue_t`는 84 B 중 count·max·대기 리스트·잠금 카운터만 세마포어 동작에 쓰이고, 나머지는 큐·mutex·설정 옵션용이다. Zephyr `struct k_sem`은 대기 큐(`_wait_q_t` = `sys_dlist_t`) + count + limit = 16 B.

84 B는 계산과 정확히 맞는다: 포인터 2개(8) + union(8) + `List_t` 두 개(20 × 2) + `uxMessagesWaiting`·`uxLength`·`uxItemSize`(12) + `int8_t` 세 개와 패딩(4) = 72 B, 여기에 `configUSE_QUEUE_SETS`가 4 B, `configUSE_TRACE_FACILITY`가 8 B를 더한다. 예제 7의 lean 출력(72 B)이 이 계산을 확인해 준다.

### 8.2 take 경로 — `xQueueSemaphoreTake()`

`queue.c` 1659행의 `xQueueSemaphoreTake()`를 줄여서 읽는다(trace·coverage 매크로와 mutex 상속 분기를 뺐다).

```c
    for( ; ; )
    {
        taskENTER_CRITICAL();
        {
            const UBaseType_t uxSemaphoreCount = pxQueue->uxMessagesWaiting;
            if( uxSemaphoreCount > ( UBaseType_t ) 0 )
            {
                pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxSemaphoreCount - ( UBaseType_t ) 1 );
                /* (mutex면 여기서 소유자 기록) */
                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE )
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != pdFALSE )
                        queueYIELD_IF_USING_PREEMPTION();
                taskEXIT_CRITICAL();
                return pdPASS;
            }
            else if( xTicksToWait == ( TickType_t ) 0 ) { taskEXIT_CRITICAL(); return errQUEUE_EMPTY; }
            else if( xEntryTimeSet == pdFALSE ) { vTaskInternalSetTimeOutState( &xTimeOut ); xEntryTimeSet = pdTRUE; }
        }
        taskEXIT_CRITICAL();

        vTaskSuspendAll();
        prvLockQueue( pxQueue );
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            if( prvIsQueueEmpty( pxQueue ) != pdFALSE )
            {
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                prvUnlockQueue( pxQueue );
                if( xTaskResumeAll() == pdFALSE ) taskYIELD_WITHIN_API();
            }
            else { prvUnlockQueue( pxQueue ); ( void ) xTaskResumeAll(); }
        }
        else { prvUnlockQueue( pxQueue ); ( void ) xTaskResumeAll(); if( prvIsQueueEmpty( pxQueue ) != pdFALSE ) return errQUEUE_EMPTY; }
    }
```

```svg
<svg viewBox="0 0 680 480" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20.0" y="14.0" width="330.0" height="32.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="185.0" y="34.2" font-size="12" text-anchor="middle">① taskENTER_CRITICAL (BASEPRI)</text>
<line x1="185.0" y1="46.0" x2="185.0" y2="58.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<rect x="20.0" y="59.0" width="330.0" height="32.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="185.0" y="79.2" font-size="12" text-anchor="middle">② count &gt; 0 ?</text>
<rect x="420.0" y="59.0" width="240.0" height="32.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="540.0" y="79.2" font-size="12" text-anchor="middle">count--, 크리티컬 나감 → pdPASS</text>
<line x1="350.0" y1="75.0" x2="418.0" y2="75.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="385.0" y="70.0" font-size="12" text-anchor="middle">예</text>
<line x1="185.0" y1="91.0" x2="185.0" y2="103.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="195.0" y="102.0" font-size="12" text-anchor="start">아니오</text>
<rect x="20.0" y="104.0" width="330.0" height="32.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="185.0" y="124.2" font-size="12" text-anchor="middle">③ xTicksToWait == 0 ?</text>
<rect x="420.0" y="104.0" width="240.0" height="32.0" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="540.0" y="124.2" font-size="12" text-anchor="middle">errQUEUE_EMPTY (폴링 실패)</text>
<line x1="350.0" y1="120.0" x2="418.0" y2="120.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="385.0" y="115.0" font-size="12" text-anchor="middle">예</text>
<line x1="185.0" y1="136.0" x2="185.0" y2="148.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="195.0" y="147.0" font-size="12" text-anchor="start">아니오</text>
<rect x="20.0" y="149.0" width="330.0" height="32.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="185.0" y="169.2" font-size="12" text-anchor="middle">④ 진입 시각 기록, taskEXIT_CRITICAL</text>
<line x1="185.0" y1="181.0" x2="185.0" y2="193.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<rect x="20.0" y="194.0" width="330.0" height="32.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="185.0" y="214.2" font-size="12" text-anchor="middle">⑤ vTaskSuspendAll + prvLockQueue</text>
<line x1="185.0" y1="226.0" x2="185.0" y2="238.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<rect x="20.0" y="239.0" width="330.0" height="32.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="185.0" y="259.2" font-size="12" text-anchor="middle">⑥ 타임아웃 지났나?</text>
<rect x="420.0" y="239.0" width="240.0" height="32.0" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="540.0" y="259.2" font-size="12" text-anchor="middle">unlock + resume, 비었으면 실패</text>
<line x1="350.0" y1="255.0" x2="418.0" y2="255.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="385.0" y="250.0" font-size="12" text-anchor="middle">예</text>
<line x1="185.0" y1="271.0" x2="185.0" y2="283.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="195.0" y="282.0" font-size="12" text-anchor="start">아니오</text>
<rect x="20.0" y="284.0" width="330.0" height="32.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="185.0" y="304.2" font-size="12" text-anchor="middle">⑦ 아직 비었나? (prvIsQueueEmpty)</text>
<rect x="420.0" y="284.0" width="240.0" height="32.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="540.0" y="304.2" font-size="12" text-anchor="middle">unlock + resume → ①로</text>
<line x1="350.0" y1="300.0" x2="418.0" y2="300.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="385.0" y="295.0" font-size="12" text-anchor="middle">아니오</text>
<line x1="185.0" y1="316.0" x2="185.0" y2="328.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<text x="195.0" y="327.0" font-size="12" text-anchor="start">예</text>
<rect x="20.0" y="329.0" width="330.0" height="32.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="185.0" y="349.2" font-size="12" text-anchor="middle">⑧ vTaskPlaceOnEventList</text>
<line x1="185.0" y1="361.0" x2="185.0" y2="373.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<rect x="20.0" y="374.0" width="330.0" height="32.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="185.0" y="394.2" font-size="12" text-anchor="middle">⑨ prvUnlockQueue (cTxLock 정산)</text>
<line x1="185.0" y1="406.0" x2="185.0" y2="418.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/>
<rect x="20.0" y="419.0" width="330.0" height="32.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="185.0" y="439.2" font-size="12" text-anchor="middle">⑩ xTaskResumeAll → yield = 잠듦</text>
<path d="M 20 435.0 L 8 435.0 L 8 30.0 L 18 30.0" stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#ah9)"/>
<text x="185.0" y="469.0" font-size="12" text-anchor="middle">give가 깨우면 ①부터 다시 — 그 사이 누가 가져갔으면 또 잔다</text>
<rect x="420.0" y="327.0" width="240.0" height="124.0" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3" stroke-dasharray="4 3"/>
<text x="540.0" y="347.0" font-size="12" text-anchor="middle">⑤~⑨: 스케줄러 정지, IRQ는 열림</text>
<text x="540.0" y="369.0" font-size="12" text-anchor="middle">이 사이 ISR의 GiveFromISR는</text>
<text x="540.0" y="387.0" font-size="12" text-anchor="middle">count++, cTxLock++ 만 하고</text>
<text x="540.0" y="405.0" font-size="12" text-anchor="middle">이벤트 리스트는 건드리지 않는다</text>
<text x="540.0" y="429.0" font-size="12" text-anchor="middle">→ ⑨에서 대신 깨운다 (ex10)</text>
</svg>
```

그림 9 — `xQueueSemaphoreTake()`의 흐름. 빠른 길(②에서 count > 0)은 크리티컬 섹션 하나로 끝난다. 느린 길은 크리티컬 섹션을 **닫고**, 스케줄러만 멈춘 채(⑤) 대기 리스트에 들어간다. 그래서 대기 리스트 정렬 삽입(O(대기자 수)) 동안 인터럽트는 열려 있다.

읽을 거리 세 가지.

1. **두 단계 잠금.** 리스트를 고치는 긴 일(⑧ `vTaskPlaceOnEventList`의 정렬 삽입, 타임아웃이 있으면 delayed 리스트 삽입)을 BASEPRI를 올린 채 하면 IRQ 지연이 늘어난다. 그래서 FreeRTOS는 짧은 count 검사만 크리티컬 섹션에서 하고, 긴 일은 `vTaskSuspendAll()`(다른 **태스크**만 막음)과 **큐 잠금**(`prvLockQueue`) 아래에서 한다. 스케줄러 정지 중에 깨어난 태스크가 `xPendingReadyList`로 가는 구조는 C1, 이 방식이 IRQ 지연에 주는 효과는 C7에서 쟀다.
2. **세마포어에도 `xTasksWaitingToSend`가 있다.** take가 성공하면 "give하려고 기다리는 태스크"를 깨운다. 그런데 `xSemaphoreGive()`는 블록 시간이 `semGIVE_BLOCK_TIME` = 0이라 실제로 이 리스트에 들어갈 태스크는 없다. 큐 코드를 공유하는 흔적이다.
3. **깨어나면 처음부터.** 잠들었다가 give로 깨어나면 ⑩의 yield에서 돌아와 `for` 루프 맨 위로 간다. 그 사이 다른 태스크가 count를 가져갔으면 다시 잔다 — §1.6에서 본 "토큰을 빼앗기는" 현상이 바로 이 루프 구조에서 나온다. 깨울 때 반환값을 심어 주는 Zephyr와 다른 점이다.

### 8.3 큐 잠금 — ISR이 "잠긴 큐"에 give하면

⑤~⑨ 구간에서는 인터럽트가 열려 있다. 그 사이에 ISR이 `xSemaphoreGiveFromISR()`를 부르면? 이벤트 리스트는 지금 태스크 쪽이 고치는 중이라 ISR이 건드리면 안 된다. 그래서 `Queue_t`에 `cRxLock`·`cTxLock`(각 `int8_t`)이 있다.

- `prvLockQueue()`는 두 값을 `queueUNLOCKED`(-1)에서 `queueLOCKED_UNMODIFIED`(0)로 바꾼다.
- `xQueueGiveFromISR()`는 count는 정상적으로 올리지만, `cTxLock`이 `queueUNLOCKED`가 아니면 이벤트 리스트를 건드리지 않고 `prvIncrementQueueTxLock()`로 **`cTxLock`만 1 올린다**. `woken`도 올리지 않는다.
- `prvUnlockQueue()`(2499행)가 `cTxLock`이 0보다 큰 만큼 `xTasksWaitingToReceive`에서 태스크를 꺼내 깨우고(`xTaskRemoveFromEventList`, 스케줄러가 멈춰 있으니 `xPendingReadyList`로), 필요하면 `vTaskMissedYield()`로 yield를 예약한다.

이 창은 보통 몇 µs라 실제로 걸리는 장면을 보기 어렵다. trace 매크로로 창을 억지로 벌려서 관찰했다.

**예제 11 — 큐가 잠긴 순간에 ISR이 give하게 만들기**

무엇을 확인하는 코드인지: 앱 폴더의 `FreeRTOSConfig.h`에서 trace 매크로 4개를 정의한다. 이 매크로는 `queue.c`·`tasks.c` **안에서** 펼쳐지므로 `Queue_t`의 private 멤버(`cTxLock`, `uxMessagesWaiting`)를 읽을 수 있다. `traceBLOCKING_ON_QUEUE_RECEIVE`(⑦과 ⑧ 사이, 큐가 잠긴 상태)에서 TIMER0을 20 µs 뒤로 걸고 IRQ가 올 때까지 기다린다.

```c
/* ex10/FreeRTOSConfig.h — template config + trace hooks that peek inside queue.c */
#ifndef D1_EX10_CONFIG_H
#define D1_EX10_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
void d1_ev(int code, const void *q, int txlock, unsigned count);
/* these expand INSIDE queue.c / tasks.c, where Queue_t members are visible */
#define traceBLOCKING_ON_QUEUE_RECEIVE(q)                                   \
    do { d1_ev(1, (q), (q)->cTxLock, (unsigned)(q)->uxMessagesWaiting);     \
         d1_ev(5, (q), (q)->cTxLock, (unsigned)(q)->uxMessagesWaiting); } while (0)
#define traceQUEUE_SEND_FROM_ISR(q)       d1_ev(2, (q), (q)->cTxLock, (unsigned)(q)->uxMessagesWaiting)
#define traceQUEUE_RECEIVE(q)             d1_ev(3, (q), (q)->cTxLock, (unsigned)(q)->uxMessagesWaiting)
#define traceMOVED_TASK_TO_READY_STATE(t) d1_ev(4, NULL, 0, (unsigned)(t)->uxPriority)
#endif
```

```c
/* D1 ex10 — an ISR gives while the taker holds the queue LOCK (cTxLock): event is deferred, not lost */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d1.h"

static SemaphoreHandle_t sem;
static volatile int armed, fired;
static volatile BaseType_t isr_woken = -1;
static struct { int code, lock; unsigned cnt; uint32_t t; } lg[16];
static volatile unsigned nlg;
static const char *const what[] = { "", "take: queue locked, about to block", "ISR: GiveFromISR enters",
    "take: count>0 -> success", "task moved to ready list (prio)", "take: after ISR, still locked" };

void d1_ev(int code, const void *q, int txlock, unsigned count)
{
    if (!armed || (q != NULL && q != (const void *)sem) || nlg >= 16) return;
    lg[nlg].code = code; lg[nlg].lock = txlock; lg[nlg].cnt = count; lg[nlg].t = CYC(); nlg++;
    if (code == 1) {                          /* widen the window: fire the IRQ right here */
        timer0_start(25u * 20u, 0x40);        /* 20 us */
        while (!fired) { }                    /* scheduler suspended, IRQs enabled */
    }
}
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1; timer0_stop();
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woken);
    isr_woken = woken; fired = 1;
    portYIELD_FROM_ISR(woken);
}
static void taker(void *arg)
{
    (void)arg;
    armed = 1;
    BaseType_t r = xSemaphoreTake(sem, portMAX_DELAY);
    armed = 0;
    for (unsigned i = 0; i < nlg; i++) {
        board_printf_raw("%u: +%u ns %s", i, (unsigned)((lg[i].t - lg[0].t) * 40), what[lg[i].code]);
        if (lg[i].code == 4) board_printf_raw(" =%u\n", lg[i].cnt);
        else board_printf_raw("  cTxLock=%d count=%u\n", lg[i].lock, lg[i].cnt);
    }
    board_printf_raw("ISR woken flag=%d, take returned %s, count now=%u\n", (int)isr_woken,
                     r == pdPASS ? "pdPASS" : "fail", (unsigned)uxSemaphoreGetCount(sem));
    board_exit(0);
}
int main(void)
{
    sem = xSemaphoreCreateBinary();
    xTaskCreate(taker, "taker", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D1/ex10/main.c \
  EXTRA_CFLAGS=-I/Users/donh/.rtos_scratch/D1 OUT=/tmp/rtos_D1/ex10 >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D1/ex10/app.elf 10
```

```text
0: +0 ns take: queue locked, about to block  cTxLock=0 count=0
1: +23440 ns ISR: GiveFromISR enters  cTxLock=0 count=0
2: +25760 ns take: after ISR, still locked  cTxLock=1 count=1
3: +36520 ns task moved to ready list (prio) =2
4: +43520 ns take: count>0 -> success  cTxLock=-1 count=1
ISR woken flag=0, take returned pdPASS, count now=0
[board_exit] code=0
```

출력에서 볼 것 (줄 번호 = 이벤트 순서):

- 0: 태스크가 빈 세마포어를 보고 잠들려는 순간. `cTxLock=0` = 잠김(아직 수정 없음), count 0.
- 1: 23 µs 뒤 ISR이 `xSemaphoreGiveFromISR`에 들어왔다. 이 시점의 `cTxLock`은 0 → ISR은 잠긴 큐를 본다.
- 2: ISR이 끝난 뒤: **count는 1로 올랐고 `cTxLock`도 1**. 이벤트 리스트는 건드리지 않았다. 그래서 ISR의 `woken` 플래그는 **0**이다(마지막 줄) — 기다리는 태스크가 "있는" 상황인데도 ISR은 아무도 깨우지 않았다고 보고한다.
- 그런데 코드는 이미 `prvIsQueueEmpty()` 검사를 통과했으므로 태스크는 그대로 `vTaskPlaceOnEventList()`로 대기 리스트에 들어간다. count가 1인데 잠드는 셈이다.
- 3: `prvUnlockQueue()`가 `cTxLock = 1`을 보고 방금 잠든 자기 자신을 대기 리스트에서 꺼내 pending ready로 옮겼고, `xTaskResumeAll()`이 ready 리스트로 옮겼다(`traceMOVED_TASK_TO_READY_STATE`, 우선순위 2).
- 4: 루프를 한 바퀴 더 돌아 count > 0을 보고 성공. 이때 `cTxLock = -1`(`queueUNLOCKED`)로 돌아와 있다. take 결과 `pdPASS`, count 0.

결론: 잠긴 창에 들어온 give는 **잃어버리지 않고 미뤄졌다가** unlock에서 정산된다. 대신 그 give를 받은 ISR은 "높은 태스크를 깨웠다"를 모르므로 yield를 요청하지 않고, 태스크가 실제로 다시 도는 시점은 unlock 뒤의 `xTaskResumeAll()`이 결정한다. `cTxLock`이 `int8_t`라서 잠긴 동안의 give 횟수에는 상한이 있다(`prvIncrementQueueTxLock()`이 태스크 수보다 작을 때만 올리고 `queueINT8_MAX`를 assert로 막는다) — 깨울 수 있는 대기자 수보다 많이 세 봐야 소용없기 때문이다.

### 8.4 Zephyr: `struct k_sem`과 `z_impl_k_sem_take()`

`include/zephyr/kernel.h`의 정의는 짧다.

```c
struct k_sem {
	_wait_q_t wait_q;
	unsigned int count;
	unsigned int limit;

	Z_DECL_POLL_EVENT

	SYS_PORT_TRACING_TRACKING_FIELD(k_sem)
	...
};
```

`Z_DECL_POLL_EVENT`는 `CONFIG_POLL`일 때만 `sys_dlist_t poll_events;`(8 B)가 된다(`k_poll`로 세마포어를 기다릴 수 있게 — D5). take 경로(`kernel/sem.c`):

```c
int z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	int ret;

	__ASSERT(((arch_is_in_isr() == false) ||
		  K_TIMEOUT_EQ(timeout, K_NO_WAIT)), "");

	k_spinlock_key_t key = k_spin_lock(&lock);

	if (likely(sem->count > 0U)) {
		sem->count--;
		k_spin_unlock(&lock, key);
		ret = 0;
		goto out;
	}

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		k_spin_unlock(&lock, key);
		ret = -EBUSY;
		goto out;
	}

	ret = z_pend_curr(&lock, key, &sem->wait_q, timeout);
out:
	return ret;
}
```

(trace 매크로는 뺐다.) 느린 길은 `z_pend_curr()`(`kernel/sched.c`) 하나다. 스케줄러 lock(`_sched_spinlock`)을 잡고 `pend_locked()`로 현재 스레드를 `wait_q`에 우선순위 순으로 넣고 타임아웃을 건 뒤, 세마포어 lock을 놓고(`k_spin_release`) 넘겨받은 key로 `z_swap()`해서 잔다. 깨어날 때의 반환값은 give 쪽이 `arch_thread_return_value_set(thread, 0)`으로 심어 둔 0이다. 아무도 심지 않은 채 타임아웃(`z_thread_timeout()`)으로 깨어나면, 잠들기 전에 `arch_swap()`이 미리 넣어 둔 기본값 `-EAGAIN`(`arch/arm/include/cortex_m/kernel_arch_func.h` 91행 `_current->arch.swap_return_value = -EAGAIN;`)이 그대로 돌아온다. `k_sem_reset()`이 대기자를 `-EAGAIN`으로 깨우는 것(§1.4)도 이 값과 맞춘 것이다. **루프가 없다** — 깨어났으면 이미 받은 것이다. FreeRTOS처럼 "스케줄러만 멈추고 인터럽트를 연 채 리스트를 고치는" 두 단계 구조도 없다. `wait_q` 삽입·`z_swap` 준비 전체를 lock(단일 코어에서는 BASEPRI) 안에서 한다. 단순한 대신 대기자가 아주 많으면 그 시간만큼 IRQ가 막힌다(`CONFIG_WAITQ_SCALABLE`이면 rbtree로 O(log n), C1).

### 8.5 예제 12 — 코드 크기와 생성 코드

무엇을 확인하는 코드인지: 두 커널 세마포어 함수의 코드 크기(`nm -S`), 그리고 Zephyr `k_sem_give`의 실제 명령.

```sh
for e in /tmp/rtos_D1/ex6/app.elf /tmp/rtos_D1/ex6_lean/app.elf; do echo "== ${e#/tmp/rtos_D1/}"; \
  arm-none-eabi-nm -S --size-sort $e | grep -E " (xQueueSemaphoreTake|xQueueGenericSend|xQueueGiveFromISR|prvUnlockQueue|xTaskRemoveFromEventList|xTaskGenericNotify|ulTaskGenericNotifyTake)$"; done
echo "== zb_ts_off/zephyr/zephyr.elf"; arm-none-eabi-nm -S --size-sort /tmp/rtos_D1/zb_ts_off/zephyr/zephyr.elf | \
  grep -E " (z_impl_k_sem_give|z_impl_k_sem_take|z_pend_curr|z_ready_thread|z_reschedule|sem|cnt|go)$"
```

```text
== ex6/app.elf
00001b84 0000009a t prvUnlockQueue
00001768 000000b0 T ulTaskGenericNotifyTake
00001f78 000000bc T xQueueGiveFromISR
00001338 000000fc T xTaskRemoveFromEventList
00001818 00000124 T xTaskGenericNotify
00002194 0000018c T xQueueSemaphoreTake
00001de4 00000194 T xQueueGenericSend
== ex6_lean/app.elf
00001b30 00000072 T xQueueGiveFromISR
00001810 0000008c t prvUnlockQueue
000014cc 000000b8 T ulTaskGenericNotifyTake
00001174 000000e0 T xTaskRemoveFromEventList
00001584 00000104 T xTaskGenericNotify
00001a1c 00000114 T xQueueGenericSend
00001cc0 0000014a T xQueueSemaphoreTake
== zb_ts_off/zephyr/zephyr.elf
00002b9c 00000006 T z_reschedule
200000a0 00000010 D cnt
200000b0 00000010 D go
200000c0 00000010 D sem
00002b36 0000001e T z_ready_thread
000016dc 0000003c T z_pend_curr
000014e0 0000004c T z_impl_k_sem_take
00001478 00000068 T z_impl_k_sem_give
```

출력에서 볼 것 (두 번째 열이 바이트 크기, 16진수):

- FreeRTOS: `xQueueGenericSend`(give) 0x194 = 404 B, `xQueueSemaphoreTake` 0x18c = 396 B. lean에서도 276 B, 330 B다. 큐 전체를 위한 일반 코드다.
- Zephyr: `z_impl_k_sem_give` 0x68 = 104 B, `z_impl_k_sem_take` 0x4c = 76 B. 세마포어 객체 `sem`·`cnt`·`go`는 각각 0x10 = 16 B이고 `D`(초기화된 데이터) — `K_SEM_DEFINE`이 빌드 시점에 초기값을 박아 넣기 때문에 런타임 init 호출이 없다.

```sh
arm-none-eabi-objdump -d --no-show-raw-insn /tmp/rtos_D1/zb_ts_off/zephyr/zephyr.elf --disassemble=z_impl_k_sem_give
```

```text
00001478 <z_impl_k_sem_give>:
    1478:	push	{r4, r5, r6, lr}
    147a:	mrs	r6, BASEPRI
    147e:	movs	r3, #32
    1480:	msr	BASEPRI_MAX, r3
    1484:	isb	sy
    1488:	ldr	r4, [r0, #0]
    148a:	cmp	r0, r4
    148c:	beq.n	14c4 <z_impl_k_sem_give+0x4c>
    148e:	cbz	r4, 14c4 <z_impl_k_sem_give+0x4c>
    1490:	ldrd	r3, r2, [r4]
    1494:	movs	r5, #0
    1496:	str	r3, [r2, #0]
    1498:	str	r2, [r3, #4]
    149a:	ldrb	r3, [r4, #16]
    149c:	strd	r5, r5, [r4]
    14a0:	bic.w	r3, r3, #2
    14a4:	strb	r3, [r4, #16]
    14a6:	str	r5, [r4, #8]
    14a8:	add.w	r0, r4, #24
    14ac:	bl	2c10 <z_abort_timeout>
    14b0:	mov	r0, r4
    14b2:	str	r5, [r4, #120]	@ 0x78
    14b4:	bl	2b36 <z_ready_thread>
    14b8:	mov	r1, r6
    14ba:	ldmia.w	sp!, {r4, r5, r6, lr}
    14be:	ldr	r0, [pc, #28]	@ (14dc <z_impl_k_sem_give+0x64>)
    14c0:	b.w	2b9c <z_reschedule>
    14c4:	ldrd	r3, r2, [r0, #8]
    14c8:	cmp	r2, r3
    14ca:	it	ne
    14cc:	addne	r3, #1
    14ce:	str	r3, [r0, #8]
    14d0:	msr	BASEPRI, r6
    14d4:	isb	sy
    14d8:	pop	{r4, r5, r6, pc}
    14da:	nop
    14dc:	.word	0x2000039a
```

출력에서 볼 것: 대기자 없는 길(`14c4`로 점프)은 `mrs BASEPRI` → `msr BASEPRI_MAX, #32` → 대기 큐 머리 검사 2개 → `ldrd count, limit` → `cmp`/`it ne`/`addne` → `msr BASEPRI` 복원, 약 16개 명령이다. `0x20`은 Zephyr의 `irq_lock()` 값으로, FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`(템플릿 0x20)와 같은 역할이다(C7). 그리고 `k_spin_lock(&lock)`의 `lock`은 단일 코어·검증 없음 설정에서 **빈 구조체**(C에서 크기 0, `include/zephyr/spinlock.h`)라 명령이 하나도 안 나온다 — 마지막 `.word 0x2000039a`는 `z_reschedule()`에 넘길 그 lock의 주소일 뿐이다. 측정한 0.64 µs = 20명령 × 32 ns와 맞는다(호출·반복 오버헤드 포함).

### 8.6 정리 — 소스 지도

| 개념 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| 객체 | `Queue_t`(`queue.c`), 항목 크기 0 | `struct k_sem`(`include/zephyr/kernel.h`) |
| count / max | `uxMessagesWaiting` / `uxLength` | `count` / `limit` |
| 대기자 | `xTasksWaitingToReceive`(`List_t`, 우선순위 정렬) | `wait_q`(`_wait_q_t`, 우선순위 정렬) |
| take | `xQueueSemaphoreTake()` — 루프, 두 단계 잠금 | `z_impl_k_sem_take()` → `z_pend_curr()` |
| give (태스크) | `xQueueGenericSend()` → `prvCopyDataToQueue()` → `xTaskRemoveFromEventList()` | `z_impl_k_sem_give()` → `z_unpend_first_thread()` → `z_ready_thread()` → `z_reschedule()` |
| give (ISR) | `xQueueGiveFromISR()`, 잠겼으면 `cTxLock++` | 같은 함수, ISR 출구 `z_arm_exc_exit()`가 PendSV |
| 잠금 | `taskENTER_CRITICAL`(BASEPRI) + `vTaskSuspendAll` + 큐 잠금 | 전역 `static struct k_spinlock lock`(UP: BASEPRI) |
| 미뤄 둔 yield | `xYieldPendings[0]` → 다음 tick | 해당 없음 |

---

## 9. 실무에서 다시 보기

### 9.1 드론 FC — IMU data-ready → 자세 추정 태스크

예를 들어 드론 비행 제어기라면 IMU(예: 8 kHz gyro)의 data-ready 핀이 EXTI 인터럽트를 띄우고, rate loop 태스크가 샘플을 처리한다. 세마포어로 짠다면 이 노트의 결과를 이렇게 반영한다(설계 스케치 — 빌드하지 않은 코드).

```c
static SemaphoreHandle_t imu_sem;            /* counting: max = 최악 버스트 + 여유 (§3.3) */
static volatile uint32_t imu_overrun;         /* give 실패 = 잃은 샘플 (§3.2: lost == give_fail) */

void EXTI_IMU_IRQHandler(void)
{
    clear_exti_pending();
    BaseType_t woken = pdFALSE;
    if (xSemaphoreGiveFromISR(imu_sem, &woken) != pdPASS) imu_overrun++;
    portYIELD_FROM_ISR(woken);                /* 빼먹으면 평균 0.5 ms 늦게 돈다 (§4.3) */
}

static void rate_loop(void *arg)
{
    for (;;) {
        if (xSemaphoreTake(imu_sem, pdMS_TO_TICKS(2)) != pdPASS) {
            sensor_fault();                   /* 2 ms 동안 한 번도 안 옴 = IMU 죽음 감지 */
            continue;
        }
        read_imu_fifo_one_sample();
        run_rate_pid();
    }
}
```

설계 포인트: (1) 최대 count는 "가장 긴 처리 동안 오는 샘플 수"로 정한다 — 측정으로 정하는 숫자다. (2) 평균 부하 U가 1에 가까우면 세마포어로 해결할 수 없다 — PID를 1/2 rate로 돌리거나 융합을 다른 태스크로 뺀다. (3) take 타임아웃을 센서 고장 감지(watchdog)로 겸용한다. (4) 정말 빠른 경로라면 세마포어 대신 task notification(`vTaskNotifyGiveFromISR` + `ulTaskNotifyTake(pdFALSE, …)`)이 counting 동작을 더 싸게 준다(D5). C4 §6의 드론 우선순위 표에서 rate loop를 notification으로 둔 이유다.

### 9.2 DMA 완료 신호와 "늦게 도착한 give"

SPI 전송 함수는 대개 이렇게 생겼다.

```c
int spi_xfer(const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)xSemaphoreTake(spi_done, 0);       /* 이전 전송의 늦은 give를 비운다 */
    start_spi_dma(tx, rx, n);
    if (xSemaphoreTake(spi_done, pdMS_TO_TICKS(5)) != pdPASS) {
        abort_spi_dma();                      /* 타임아웃: DMA를 확실히 멈춘다 */
        return -1;
    }
    return 0;
}
```

첫 줄이 핵심이다. 전송 A가 5 ms 타임아웃으로 포기한 **뒤에** A의 DMA 완료 IRQ가 늦게 뜨면, binary 세마포어에 give 하나가 남는다. 다음 전송 B의 take는 그 묵은 give를 받아 **B가 끝나기도 전에** 성공을 반환한다. B의 수신 버퍼를 아직 DMA가 쓰는 중인데 태스크가 읽는다 — 재현이 아주 어려운 데이터 깨짐이다. 대응은 (1) 전송 시작 전에 `take(…, 0)`로 비우기(Zephyr는 `k_sem_reset()`), (2) 타임아웃 경로에서 DMA를 확실히 abort하고 IRQ pending을 지우기, (3) 더 엄격하게는 ISR이 완료한 전송 번호를 기록하고 태스크가 자기 번호인지 확인하기다. Don이 SSD 펌웨어에서 "타임아웃 난 명령의 완료가 늦게 와서 다른 명령 슬롯을 완료 처리" 버그를 막으려고 명령 태그를 확인했던 것과 같은 원리다.

### 9.3 SPI DMA 버퍼 풀

§7의 예제가 그대로 실무 코드의 뼈대다. 실제 코드에서 추가할 것: 버퍼 take에 타임아웃을 두고 실패 시 프레임을 버리는 정책(로그 프레임은 버려도 되지만 제어 명령은 안 된다), 우선순위가 다른 생산자들 사이의 몫 나누기(§7.1에서 logger가 13.9 ms 굶은 것), 그리고 "어느 버퍼"를 위한 비트맵 보호. 버퍼 포인터 자체를 넘겨야 하면 큐가 더 깔끔하다(D4).

### 9.4 Don의 SSD 경험을 RTOS 말로 번역하기

| SSD 펌웨어 (executive) | RTOS |
|---|---|
| ISR이 `done` 플래그 세우고 루프가 폴링 | binary 세마포어 + 블록하는 take |
| 완료 플래그 대신 completion queue / 카운터 | counting 세마포어 (또는 큐) |
| free 버퍼 비트맵 + `popcount` | counting 세마포어(개수) + 비트맵(어느 것) |
| 명령 태그로 늦은 완료 걸러내기 | take 전 비우기 + 전송 번호 확인 (§9.2) |
| 루프 한 바퀴 = 응답 시간 | ISR → 태스크 9.2 µs(yield 있음) / 최대 1 tick(없음) |

면접에서는 이렇게 말한다: "SSD 펌웨어에서는 완료 플래그를 폴링하는 executive를 썼는데, 플래그 하나에 완료가 두 번 겹치면 하나가 사라져서 완료 큐로 바꿨다. RTOS에서 binary 세마포어가 정확히 그 플래그고, counting 세마포어나 큐가 완료 큐다. 차이는 태스크가 폴링하지 않고 커널에게 깨워 달라고 맡긴다는 것이다."

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 빠른 IRQ 신호에 binary 세마포어 | 가끔 샘플·패킷이 빠짐, 부하가 높을 때만 | 긴 처리 중 겹친 give가 포화로 버려짐 (§3) | counting(max = 최악 버스트), give 반환값으로 유실 카운트 |
| `portYIELD_FROM_ISR(woken)` 누락 | 기능은 정상, 응답이 0~1 ms로 들쭉날쭉 | 깨운 태스크가 다음 tick까지 ready로 대기 (§4.3) | ISR 끝에 반드시 yield. 코드 리뷰 체크 항목 |
| `woken`을 초기화 안 함 | 가끔 불필요한 스위치, 또는 쓰레기 값 | API는 `pdTRUE`로 올리기만 함 | `BaseType_t woken = pdFALSE;` |
| ISR에서 `xSemaphoreGive`(FromISR 아님) | assert(`port.c`, VECTACTIVE 검사) 또는 리스트 깨짐 | 태스크용 API는 ISR에서 크리티컬 섹션 규칙이 다름 (A5) | ISR에서는 `…FromISR`만 |
| 세마포어로 상호배제 | 높은 태스크가 가끔 수 ms 늦음, 또는 두 태스크가 동시에 자원 사용 | 우선순위 상속 없음, 소유권 없음 (§6) | mutex(D2) |
| 이중 give (에러 경로) | 드물게 공유 자원 데이터 섞임 | 남의 잠금을 풀어 버림 (§6.3) | mutex로 바꾸면 소유자 검사로 즉시 드러남 |
| 타임아웃 뒤 늦게 온 완료 give | 다음 전송이 너무 일찍 "완료", 데이터 깨짐 | binary에 묵은 give가 남음 (§9.2) | 시작 전 `take(0)`/`k_sem_reset()`, DMA abort, 전송 번호 |
| counting max를 크게 키워 과부하 해결 시도 | 유실은 줄었지만 지연이 계속 늘다가 결국 유실 | 평균 부하 U ≥ 1은 완충으로 못 고침 (§3.3) | 처리 시간 줄이기, rate 낮추기, 일 나누기 |
| `vSemaphoreCreateBinary` 코드를 `xSemaphoreCreateBinary`로 기계적 치환 | 첫 take가 영원히 블록 | 옛 API는 1로 시작, 새 API는 0으로 시작 (§1.3) | 필요하면 생성 직후 give 한 번 |
| FreeRTOS에서 "give 1번 = 대기자가 받는다" 가정 | 낮은 우선순위 대기자가 타임아웃, 굶음 | 깨워서 다시 경쟁시키는 구조 (§1.6, §7.1) | give한 쪽이 다시 take하지 않게 설계, 몫 나누기 |

---

## 11. 면접에서 이렇게 말한다

**Q.** "What's the difference between a mutex and a semaphore?"

**A.** 세마포어는 count와 대기 리스트다. 신호(ISR이 주고 태스크가 받음)나 N개 자원 세기에 쓴다. mutex는 상호배제 전용이고 소유권과 우선순위 상속이 있다. binary 세마포어로 잠금을 하면 누구나 give할 수 있어 남의 잠금을 풀 수 있고, 상속이 없어 우선순위 역전이 생긴다. 실측으로 같은 시나리오에서 높은 태스크 대기가 세마포어 7.0 ms, mutex 2.1 ms였다.

> A semaphore is a counter plus a wait list, used for signaling — an ISR gives, a task takes — or for counting N identical resources. A mutex is only for mutual exclusion and adds two things a semaphore lacks: ownership, so only the holder can release it, and priority inheritance. If you use a binary semaphore as a lock, any task can "unlock" it, and a medium-priority task can preempt the holder while a high-priority task waits. In a small test I ran, the high-priority wait was 7 ms with a binary semaphore versus 2 ms with a mutex.

**꼬리 질문**: "Can you give a mutex from an ISR?" → 아니다. 소유자가 ISR일 수 없고 상속이 의미가 없다. FreeRTOS `xQueueGiveFromISR()`는 소유자가 있는 mutex면 assert한다.

**Q.** "When would you use a counting semaphore instead of a binary one?"

**A.** 두 경우다. 이벤트가 처리보다 빨리 겹쳐 올 수 있어 개수를 잃으면 안 될 때, 그리고 N개 동일 자원(DMA 버퍼·채널)을 셀 때. 8 kHz IRQ에 10번째 처리가 400 µs인 실험에서 binary는 1000개 중 166개를 잃었고 counting은 0개였다. 단 평균 부하가 1을 넘으면 counting도 결국 잃는다 — 그건 설계 문제다.

> When events can arrive faster than the task handles them in a burst and I can't afford to drop any — the count remembers how many happened — or when I'm tracking a pool of N identical resources like DMA buffers. In a measurement with an 8 kHz interrupt and an occasional 400 µs processing step, a binary semaphore dropped 166 of 1000 events and a counting semaphore dropped none. But a counting semaphore only absorbs bursts; if average load exceeds 100%, nothing saves you except doing less work.

**Q.** "Walk me through giving a semaphore from an ISR in FreeRTOS."

**A.** `woken = pdFALSE`로 시작해서 `xSemaphoreGiveFromISR(sem, &woken)`를 부르면 커널이 대기자를 ready로 옮기고, 그게 지금 태스크보다 높으면 `woken`을 참으로 만든다. ISR 끝에서 `portYIELD_FROM_ISR(woken)`이 PendSV를 걸어 ISR이 끝나자마자 전환된다. 빼먹으면 다음 tick까지 늦는다 — 측정으로 9.2 µs가 평균 0.5 ms, 최대 1 ms가 됐다. ISR 우선순위는 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 낮아야 한다.

> Initialize a BaseType_t woken to pdFALSE, call xSemaphoreGiveFromISR with its address — the kernel moves the waiting task to the ready list and sets woken if that task outranks the one I interrupted — then end the ISR with portYIELD_FROM_ISR(woken), which pends PendSV so the switch happens right as the ISR exits. If you forget the yield, the task still runs, but only at the next tick: in my measurement the wake latency went from a constant 9 µs to uniformly spread between zero and one millisecond. The interrupt's priority must also be numerically at or above configMAX_SYSCALL_INTERRUPT_PRIORITY so it's allowed to call the API.

**꼬리 질문**: "How does Zephyr handle that?" → 같은 `k_sem_give()`를 ISR에서 부르고, ISR 출구(`z_arm_exc_exit`)가 다음 스레드가 바뀌었으면 PendSV를 건다. 빼먹을 단계가 없다.

**Q.** "How is a semaphore implemented in FreeRTOS?"

**A.** 항목 크기 0인 큐다. count는 `uxMessagesWaiting`, max는 `uxLength`, 대기자는 `xTasksWaitingToReceive`. 그래서 객체가 84 B(lean 72 B)이고 give가 Zephyr `k_sem`(16 B, 0.64 µs)보다 3배쯤 비싸다. take는 크리티컬 섹션에서 count를 보고, 없으면 스케줄러만 멈추고 큐를 잠근 채 대기 리스트에 들어간다. 그 사이 ISR의 give는 `cTxLock`에 기록되었다가 unlock에서 정산된다.

> It's a queue with an item size of zero: the count is the number of items in the queue, the maximum is the queue length, and blocked takers sit on the queue's receive wait list. That generality costs RAM and time — 84 bytes per semaphore in my build and about 3 µs per give on a 25 MHz-class core, versus 16 bytes and 0.6 µs for Zephyr's dedicated k_sem. Taking does a quick check inside a critical section, and if it must block, it suspends the scheduler and locks the queue so it can insert itself into the sorted wait list with interrupts enabled; an ISR that gives during that window just bumps a lock counter, and the unlock path wakes the task.

**Q.** "An engineer says sensor samples are occasionally lost. How do you debug it?"

**A.** 먼저 신호 경로를 본다. binary 세마포어면 give 반환값(`errQUEUE_FULL`)을 세는 카운터를 넣는다 — 실험에서 잃은 수와 정확히 같았다. 그다음 가장 긴 처리 시간과 IRQ 주기로 버스트 조건을, 평균 처리 시간으로 부하 조건을 따진다. 버스트면 counting이나 notification으로, 평균 과부하면 처리를 줄인다.

> First I'd instrument the signal path: if it's a binary semaphore, count failed gives in the ISR — in my test that counter matched the lost samples exactly. Then I'd measure the longest processing time against the interrupt period to see whether it's a burst problem, and average processing time to check overall load. A burst problem is fixed with a counting semaphore sized to the worst burst, or a counting task notification; an overload problem needs less work per sample or a lower rate.

**Q.** "What happens if a semaphore is given and nobody is waiting?"

**A.** count가 올라간다. binary는 1에서 멈추고 그 이후 give는 버려진다(FreeRTOS는 `errQUEUE_FULL`, Zephyr는 아무 표시 없음). 나중에 오는 take는 기다리지 않고 바로 성공한다. 그래서 신호를 놓치지 않는다는 점이 이벤트 플래그를 폴링하는 것보다 좋지만, 타임아웃 뒤 늦게 온 give가 남아 다음 대기를 잘못 통과시키는 버그도 여기서 나온다.

> The count goes up, so a later take returns immediately — the signal isn't lost the way an unwatched edge would be. A binary semaphore saturates at one, so extra gives are dropped: FreeRTOS reports it as errQUEUE_FULL, Zephyr's k_sem_give is void and drops it silently. The flip side is stale gives: if a DMA completion arrives after the waiting task already timed out, it leaves a token that makes the next transfer's wait return early, so I clear the semaphore before starting each transfer.

**Q.** "Does the woken task always get the semaphore?"

**A.** Zephyr에서는 그렇다 — give가 토큰을 대기자에게 직접 넘기고 반환값 0을 심는다. FreeRTOS에서는 아니다 — give는 count를 올리고 대기자를 ready로 옮길 뿐이라, 더 높은 태스크가 먼저 take하면 빼앗긴다. 실험에서 give한 높은 태스크가 바로 take해서 가져갔고 낮은 대기자는 타임아웃났다.

> In Zephyr yes: give hands the token directly to the highest-priority waiter and sets its return value, so it can't be stolen. In FreeRTOS no: give increments the count and readies the waiter, which re-checks the count when it runs, so a higher-priority task can take it first. I verified that a high-priority task that gives and immediately takes gets the token back, and the low-priority waiter times out.

---

## 12. 직접 해보기

1. **(개념)** binary 세마포어를 1로 시작해서 잠금으로 쓰는 코드가 있다. 이 코드를 mutex로 바꾸지 않아도 되는 경우가 있을까? 조건을 말해 보라.
   정답/힌트: 그 자원을 쓰는 태스크가 모두 같은 우선순위이고(역전 없음), give가 반드시 take한 태스크에서만 일어난다는 것이 코드 구조로 보장될 때. 그래도 디버그 도구(소유자 표시)와 assert를 잃으니 mutex가 낫다.
2. **(계산)** IRQ가 250 µs 간격, 20번째마다 처리가 900 µs, 나머지는 30 µs다. W = 12 µs로 binary가 IRQ 1000번 중 몇 개를 잃는지, counting의 max를 몇으로 해야 하는지 §3.3 모델로 계산하라.
   정답/힌트: floor(912/250) = 3 → k = 2, 처리 20개마다 IRQ 22개 → 1000 × 2/22 ≈ 91개. 긴 처리 중 3개가 쌓이니 max ≥ 3, 여유를 둬서 4~8. U = (19 × 30 + 900)/(20 × 250) = 0.29.
3. **(코드)** 예제 4(`ex3_lost.c`)의 세마포어를 task notification(`vTaskNotifyGiveFromISR` + `ulTaskNotifyTake(pdFALSE, …)`)으로 바꾸고 같은 표를 다시 뽑아라. 유실이 생기는가?
   정답/힌트: notification 값은 32비트 카운터라 counting처럼 동작해 L = 1000까지 유실 0이어야 한다. 어디에 상한이 있는지(`ulTaskNotifyTake`의 반환값)는 D5.
4. **(측정)** 예제 6(`ex4_yield.c`)에서 tick을 100 Hz(`EXTRA_CFLAGS=-DconfigTICK_RATE_HZ=100`)로 바꾸고 TIMER0 주기를 10.37 ms로 바꿔 yield 누락의 최대 지연이 10 ms로 늘어나는지 확인하라.
   정답/힌트: 템플릿 config는 `configTICK_RATE_HZ`를 `#ifndef`로 감싸 두었다(README). 최대 지연 ≈ 1 tick이 핵심.
5. **(코드)** 예제 3(`ex12_steal.c`)에서 high가 give 뒤 `taskYIELD()`를 부르면 결과가 바뀌는가? 이유는?
   정답/힌트: 안 바뀐다. low(1)가 ready가 되어도 high(3)가 더 높아 `taskYIELD()`는 high 자신에게 돌아온다. low가 받게 하려면 high가 block해야 한다.
6. **(소스)** `queue.c`의 `prvIncrementQueueTxLock()` 매크로를 읽고, 잠긴 동안 ISR이 대기자 수보다 많이 give하면 어떻게 되는지 설명하라. count는 어떻게 되나?
   정답/힌트: `cTxLock`은 태스크 수 이상 올라가지 않지만 count(`uxMessagesWaiting`)는 정상적으로 max까지 올라간다. 깨울 수 있는 대기자 수 이상 세 봐야 소용없기 때문이다.
7. **(Zephyr)** 예제 8(`z_bench`)에 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`을 넣고 다시 재라. time slicing이 켜진 기본 설정의 지연이 바뀌는가?
   정답/힌트: slice 재설정 비용은 SysTick 재프로그래밍 경로에서 나오므로 tick 주파수에 따라 달라질 수 있다. 직접 재서 A7 §3.1의 설명과 맞춰 보라.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| semaphore | 세마포어 | count + 대기 리스트. give/take 두 연산 |
| binary semaphore | 이진 세마포어 | count 0/1. 깃발 하나 |
| counting semaphore | 계수 세마포어 | count 0..max. 토큰 그릇 |
| give / take (V / P) | 주기 / 받기 | count를 올리거나 대기자를 깨움 / count를 내리거나 잠듦 |
| signaling | 신호용 | "일이 생겼다"를 알리는 쓰임. 처음 값 0 |
| resource counting | 자원 계수용 | "몇 개 남았나"를 세는 쓰임. 처음 값 N |
| saturation | 포화 | count가 max라 give가 버려지는 상태 |
| lost event | 잃어버린 이벤트 | 포화로 버려진 give |
| `pxHigherPriorityTaskWoken` | woken 플래그 | FromISR API가 "더 높은 태스크를 깨웠다"를 알리는 출력 인자 |
| `portYIELD_FROM_ISR` | ISR 출구 yield | woken이면 PendSV를 걸어 ISR 직후 전환 |
| `xYieldPendings` | 미뤄 둔 yield | yield를 빼먹어도 다음 tick에 전환하게 하는 커널 플래그 |
| `uxMessagesWaiting` | 큐 항목 수 | FreeRTOS 세마포어의 count |
| `cTxLock` / `cRxLock` | 큐 잠금 카운터 | 큐가 잠긴 동안 ISR이 넣은/뺀 횟수. unlock에서 정산 |
| `xTasksWaitingToReceive` | 수신 대기 리스트 | take에서 잠든 태스크들 (우선순위 정렬) |
| `struct k_sem` | Zephyr 세마포어 | `wait_q` + `count` + `limit`, 16 B |
| `z_pend_curr` | 현재 스레드 대기 | Zephyr에서 대기 큐에 넣고 swap하는 공통 함수 |
| direct handoff | 직접 넘김 | give가 대기자에게 토큰을 바로 주는 방식 (Zephyr) |
| ownership | 소유권 | mutex만 가짐. 쥔 태스크만 놓을 수 있음 |
| priority inheritance | 우선순위 상속 | mutex 소유자를 대기자 우선순위로 올림 (D3) |
| utilization U | 평균 부하 | 평균 처리 시간 / 평균 도착 간격. 1 이상이면 완충으로 못 막음 |

---

## 14. 요약 & 체크리스트

세마포어는 count 하나와 대기 리스트 하나다. 같은 객체를 **신호용**(ISR이 주고 태스크가 받음, 처음 0)과 **자원 계수용**(N개를 빌리고 반납, 처음 N)으로 쓴다. binary는 포화되면 give를 버리므로 빠른 IRQ에서 이벤트를 잃는다 — 8 kHz IRQ와 400 µs 처리에서 1000개 중 166개를 잃었고, 그 수는 `floor((L + W)/P) - 1` 모델로 1% 안에서 예측됐다. counting은 버스트를 흡수하지만 평균 부하가 1을 넘으면 소용없다. FreeRTOS ISR에서는 `xSemaphoreGiveFromISR` + `woken` + `portYIELD_FROM_ISR` 세 단계가 한 세트이고, yield를 빼먹으면 지연이 9.2 µs에서 0~1 ms 균일 분포로 바뀐다(커널의 `xYieldPendings`가 다음 tick에 구해 준다). Zephyr는 ISR 출구가 자동으로 재스케줄한다. FreeRTOS 세마포어는 항목 크기 0인 큐(84 B, give 2.98 µs)이고 take는 두 단계 잠금과 `cTxLock`으로 ISR과 공존한다. Zephyr `k_sem`은 16 B, give 0.64 µs의 전용 객체지만 깨우기 지연은 time slicing 설정이 지배했다(18.8 → 4.8 µs). 세마포어는 소유권과 우선순위 상속이 없으므로 상호배제에는 mutex를 쓴다.

- [ ] 신호용과 자원 계수용을 처음 값·누가 give/take하나로 구분해 설명할 수 있다
- [ ] binary 세마포어가 이벤트를 잃는 조건(긴 처리 중 IRQ 2개 이상)을 그림으로 그릴 수 있다
- [ ] 버스트 조건(max)과 평균 조건(U < 1)을 구분하고 counting의 max를 계산할 수 있다
- [ ] FreeRTOS give 반환값으로 유실을 세고, Zephyr에서는 왜 못 세는지 안다
- [ ] FromISR · woken 초기화 · `portYIELD_FROM_ISR`의 역할과 누락 시 증상(최대 1 tick)을 말할 수 있다
- [ ] Zephyr ISR에서 yield가 필요 없는 이유(`z_arm_exc_exit`)를 안다
- [ ] FreeRTOS give는 깨워서 다시 경쟁, Zephyr give는 직접 넘김이라는 차이를 실험 결과로 설명할 수 있다
- [ ] 세마포어로 상호배제하면 안 되는 이유 두 가지(소유권, 상속)를 실측 숫자와 함께 말할 수 있다
- [ ] counting 세마포어 풀에서 "몇 개"와 "어느 것"을 따로 보호해야 하는 이유를 안다
- [ ] `Queue_t`의 어느 필드가 세마포어 count·max·대기자인지, 왜 84 B인지 말할 수 있다
- [ ] `xQueueSemaphoreTake`의 두 단계 잠금과 `cTxLock` 정산을 그릴 수 있다
- [ ] 타임아웃 뒤 늦게 온 give 버그와 대응(비우기·abort·번호 확인)을 설명할 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `include/semphr.h`(세마포어 매크로, "faster and more memory efficient to use a direct to task notification" 주석), `queue.c`(`Queue_t`, `xQueueGenericCreate`, `xQueueCreateCountingSemaphore`, `xQueueGenericSend`, `xQueueGiveFromISR`, `xQueueSemaphoreTake`, `prvLockQueue`, `prvUnlockQueue`, `prvIncrementQueueTxLock`), `tasks.c`(`xTaskRemoveFromEventList`, `xYieldPendings`, `xTaskIncrementTick`, `xTaskPriorityDisinherit`), `portable/GCC/ARM_CM3/portmacro.h`(`portYIELD_FROM_ISR`) — https://github.com/FreeRTOS/FreeRTOS-Kernel
- FreeRTOS 문서 — Cortex-M 인터럽트 우선순위와 API 호출 규칙 (소스 주석이 가리키는 페이지): https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html
- FreeRTOS 문서 — 큐 (세마포어가 올라탄 객체): https://www.FreeRTOS.org/Embedded-RTOS-Queues.html
- Richard Barry, "Mastering the FreeRTOS Real Time Kernel — A Hands-On Tutorial Guide" — 인터럽트 관리 장(binary·counting 세마포어로 ISR 처리를 태스크에 넘기는 패턴), 자원 관리 장(mutex와 우선순위 역전)
- Zephyr v4.4.2 소스: `kernel/sem.c`(`z_impl_k_sem_init`, `z_impl_k_sem_give`, `z_impl_k_sem_take`, `z_impl_k_sem_reset`, 전역 lock 주석), `include/zephyr/kernel.h`(`struct k_sem`, `K_SEM_DEFINE`, `K_SEM_MAX_LIMIT`), `kernel/sched.c`(`z_pend_curr`, `z_ready_thread`, `z_reschedule`), `kernel/include/ksched.h`(`z_unpend_first_thread`), `kernel/timeout.c`(`z_add_timeout`의 +1 tick), `arch/arm/core/cortex_m/exc_exit.c`(`z_arm_exc_exit`) — https://github.com/zephyrproject-rtos/zephyr
- Zephyr 문서 — Semaphores: https://docs.zephyrproject.org/latest/kernel/services/synchronization/semaphores.html
- E. W. Dijkstra, "Cooperating Sequential Processes" (EWD123) — P/V 연산의 원전
- 이 랩의 관련 노트: A5(API 로제타석·give 경로·깨울 순서), A7(IRQ → 태스크 지연과 Zephyr time slicing), C1(이벤트 리스트), C4 §5.3(협력형에서도 ISR yield는 전환), C7(크리티컬 섹션), EL F4 §6(Linux `struct semaphore`)
