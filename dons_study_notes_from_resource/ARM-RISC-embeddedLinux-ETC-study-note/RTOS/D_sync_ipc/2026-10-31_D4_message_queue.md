# D4. 메시지 큐 — 크기 고정 칸 우편함과 두 줄의 대기자: 복사·깊이·소유권을 숫자로 설계한다

> **이 노트를 다 읽으면**: FreeRTOS 큐가 item을 `memcpy`로 **값 복사**한다는 것을 `Queue_t`의 링 포인터(`pcHead`·`pcWriteTo`·`pcReadFrom`)를 직접 덤프해 설명하고, item 크기별 send/receive 비용을 FreeRTOS와 Zephyr `k_msgq`에서 같은 보드로 잰 숫자로 말할 수 있다 · 큰 데이터는 "빈 버퍼 큐 + 찬 버퍼 큐" 두 개로 **포인터와 소유권만** 넘기는 버퍼 풀을 짜고 "보낸 뒤엔 만지지 않는다" 규칙을 지킬 수 있다 · 버스트 크기·도착률·서비스 시간에서 필요한 큐 깊이를 계산(최악 버스트 분석, Little's law)하고 타이머 ISR 실험의 손실 숫자와 맞춰 볼 수 있다 · full일 때의 정책(블록+timeout, 새것 버림, 옛것 버림, `xQueueOverwrite` mailbox), `FromISR` 규칙과 yield 플래그, 큐 셋의 함정, tagged union 이벤트 + 응답 큐 + 상태 기계 패턴을 실행 결과로 설명할 수 있다
> **왜 필요한가**: RTOS 면접에서 "How do tasks communicate? Queue vs pointer? How deep should the queue be? What happens if it's full?"는 태스크 설계 질문의 출발점이고, 현장의 "가끔 IMU 샘플이 빠진다", "명령 응답이 엉뚱한 요청에 붙는다", "ISR에서 보냈는데 태스크가 1 ms 늦게 돈다" 같은 버그가 전부 큐 설계에서 나온다.
> **Don 기준 난이도**: 링 버퍼·포인터·DMA 버퍼 소유권·SSD 명령/완료 큐는 이미 손에 익었다 / FreeRTOS 큐 내부(복사 두 번, 큐 잠금 `cRxLock`·`cTxLock`, 깨어난 sender의 재시도 루프), Zephyr의 직접 전달(direct handoff), 큐 셋의 숨은 규칙, 깊이를 대기행렬 수학으로 정하는 법을 새로 배운다
> **선행 노트**: A4(태스크 모델·상태), A5(API 로제타 — 큐 API 이름 대응표는 거기 있다), C1(이벤트 리스트 — 대기자 정렬은 거기서 다 했다), C7(크리티컬 섹션), EL A7 §4(POSIX message queue). 이어지는 노트: D1(세마포어 = item 크기 0인 큐), D5(task notification · stream/message buffer · `k_poll`), D10(Zephyr `k_fifo`·`k_pipe`·zbus), E1(ISR 규칙)

---

## 0. 큰 그림 — 큐는 태스크 설계의 기본 배관이다

RTOS 펌웨어를 태스크로 쪼개는 순간 질문이 하나 생긴다: **"이 태스크가 만든 데이터를 저 태스크에게 어떻게 넘기나?"** 전역 변수 + 플래그로 넘기면 경쟁 조건(race)과 "언제 깨어나나" 문제가 같이 온다. 메시지 큐(message queue)는 이 두 문제를 한 객체로 푼다. **데이터를 안전하게 옮겨 담는 그릇**이면서 동시에 **"데이터가 오면 깨워 달라"는 블로킹 지점**이다.

직관은 이렇다. 큐는 **크기가 똑같은 칸이 N개 있는 우편함**이다. 편지(item)는 칸 크기에 맞춰 **복사해서** 넣는다. 그리고 우편함 앞에는 **줄이 두 개** 있다. 우편함이 꽉 찼을 때 편지를 넣으려고 기다리는 사람들의 줄(보내는 쪽 대기자), 우편함이 비었을 때 편지를 꺼내려고 기다리는 사람들의 줄(받는 쪽 대기자). 줄은 우선순위 순으로 서고, 칸이 비거나 차는 순간 커널이 줄 맨 앞 사람을 깨운다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330" y="20" font-size="14" text-anchor="middle" font-weight="bold">큐 = 크기 고정 칸 N개 + 기다리는 사람 줄 두 개</text>
<text x="330" y="56" font-size="12" text-anchor="middle">칸 크기 = uxItemSize 바이트, 칸 수 = uxLength (넣을 때 복사)</text>
<rect x="20" y="64" width="160" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="100" y="88" font-size="13" text-anchor="middle">생산자 태스크 / ISR</text>
<text x="100" y="108" font-size="12" text-anchor="middle">xQueueSend()</text>
<line x1="182" y1="92" x2="226" y2="92" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<rect x="230" y="72" width="40" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="97" font-size="13" text-anchor="middle">A</text>
<rect x="270" y="72" width="40" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="290" y="97" font-size="13" text-anchor="middle">B</text>
<rect x="310" y="72" width="40" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="97" font-size="13" text-anchor="middle">C</text>
<rect x="350" y="72" width="40" height="40" fill="#888" fill-opacity="0.05" stroke="#888"/>
<rect x="390" y="72" width="40" height="40" fill="#888" fill-opacity="0.05" stroke="#888"/>
<line x1="434" y1="92" x2="476" y2="92" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<rect x="480" y="64" width="160" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="560" y="88" font-size="13" text-anchor="middle">소비자 태스크</text>
<text x="560" y="108" font-size="12" text-anchor="middle">xQueueReceive()</text>
<text x="330" y="134" font-size="12" text-anchor="middle">FIFO: 먼저 넣은 A가 먼저 나온다</text>
<rect x="20" y="160" width="300" height="92" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="182" font-size="13" text-anchor="middle">보내는 사람 줄 — 큐가 꽉 찼을 때</text>
<text x="170" y="202" font-size="12" text-anchor="middle">xTasksWaitingToSend (우선순위 순)</text>
<rect x="60" y="214" width="60" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="90" y="232" font-size="12" text-anchor="middle">P3</text>
<rect x="140" y="214" width="60" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="232" font-size="12" text-anchor="middle">P1</text>
<rect x="340" y="160" width="300" height="92" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="490" y="182" font-size="13" text-anchor="middle">받는 사람 줄 — 큐가 비었을 때</text>
<text x="490" y="202" font-size="12" text-anchor="middle">xTasksWaitingToReceive (우선순위 순)</text>
<rect x="380" y="214" width="60" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="410" y="232" font-size="12" text-anchor="middle">C5</text>
<rect x="460" y="214" width="60" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="490" y="232" font-size="12" text-anchor="middle">C2</text>
<text x="330" y="280" font-size="12" text-anchor="middle">꽉 참과 비어 있음은 동시에 생기지 않는다 → 한 번에 한 줄만 붐빈다 (Zephyr는 wait_q 하나로 합친다)</text>
</svg>
```
그림 1 — 큐의 직관. 칸은 크기가 고정이고, 넣을 때 복사한다. 꽉 차면 보내는 쪽이, 비면 받는 쪽이 줄을 서서 잔다. 커널은 칸 상태가 바뀌는 순간 해당 줄의 맨 앞(가장 높은 우선순위)을 깨운다.

Don의 SSD 펌웨어로 바꿔 보면 이미 매일 쓰던 물건이다. NVMe의 **Submission Queue(SQ)** 는 호스트가 64 B 명령 엔트리를 채우고 doorbell로 알리는 링이고, **Completion Queue(CQ)** 는 컨트롤러가 16 B 완료 엔트리를 채우는 링이다. 컨트롤러 펌웨어 안에서도 "호스트 명령 디스패처 → FTL → NAND 스케줄러" 사이를 내부 명령 큐로 잇는다. 차이는 둘이다. (1) SSD 펌웨어의 내부 큐는 보통 **슬롯 인덱스나 포인터**만 옮기고 데이터는 DMA 버퍼에 그대로 둔다. FreeRTOS 큐는 기본이 **값 복사**다. (2) SSD 펌웨어는 executive 루프가 큐를 **폴링**하지만, RTOS 큐는 비어 있으면 태스크를 **재운다**. 폴링 대신 "데이터가 오면 깨워 달라"를 스케줄러에 맡기는 것이다.

이 노트의 순서:

1. 큐의 정의와 API 한 장(§1)
2. 값 복사 vs 포인터 전달 — 내부 포인터 덤프와 item 크기별 비용 측정(§2)
3. 큰 데이터의 소유권 넘기기 — 두 큐 버퍼 풀(§3)
4. 깊이 설계 — 버스트 수학, Little's law, 타이머 ISR 손실 실험(§4)
5. full일 때의 정책(§5), 블로킹·timeout·앞에 넣기·엿보기(§6), ISR 규칙(§7)
6. 여러 큐 기다리기 — 큐 셋(§8), 메시지 설계와 이벤트 큐 + 상태 기계(§9)
7. 커널 안 — FreeRTOS `queue.c`, Zephyr `kernel/msg_q.c`(§10), 그리고 실무(§11)

### 실습 환경 한 줄

Lima VM `elinux` 안의 QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) + FreeRTOS Kernel V11.3.2(ARM_CM3 포트, 1 kHz tick, `-O2`, newlib-nano) + Zephyr v4.4.2(`mps2/an385`, 같은 가상 보드). 타이밍은 전부 **`ICOUNT=1`**(`-icount shift=5`: 명령 1개 = 32 ns 가상 시간, 매번 같은 숫자)로 재고, 시계는 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz, 1 = 40 ns)를 쓴다. 그래서 "사이클" × 1.25 = 실행된 명령 수다. **숫자는 QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클 미모델링). 비교와 경향을 보는 용도다. 빌드 산출물은 VM의 `/tmp/rtos/D4/`(tmpfs)에 두었고, 계산·차트는 VM의 Python 3.14로 만들었다.

---

## 1. 큐란 무엇인가 — 정의와 API 한 장

**메시지 큐(message queue)** 는 고정 크기 item을 최대 N개 담는 FIFO 버퍼 + 그 버퍼가 비었을 때/찼을 때 기다리는 태스크들의 대기 리스트다. 생성할 때 두 숫자를 정한다: **길이(length, 칸 수)** 와 **item 크기(item size, 바이트)**. 둘 다 생성 후 바뀌지 않는다. 그래서 RAM은 `길이 × item 크기 + 제어 구조체`로 생성 순간에 확정된다 — 런타임 malloc이 없는 MCU 설계에 맞는 성질이다.

FreeRTOS는 "queue"라는 이름 하나로 많은 것을 만든다. 세마포어·뮤텍스도 내부적으로는 item 크기 0인 큐다(D1·D2에서 다룬다). 이 노트는 데이터를 실어 나르는 진짜 큐만 본다. 다섯 RTOS의 이름 대응은 A5 §1.2 표에 있고, POSIX `mq_open()`(우선순위 있는 커널 우편함)은 EL A7 §4에서 했다. 여기서는 FreeRTOS와 Zephyr `k_msgq` 두 개만 나란히 둔다.

| 하는 일 | FreeRTOS V11.3.2 | Zephyr v4.4.2 `k_msgq` | 메모 |
|---|---|---|---|
| 생성 | `xQueueCreate(len, size)` / `xQueueCreateStatic(len, size, buf, &sq)` | `K_MSGQ_DEFINE(name, size, len, align)` / `k_msgq_init(&q, buf, size, len)` | Zephyr는 정적 정의가 기본 |
| 뒤에 넣기 | `xQueueSend()` = `xQueueSendToBack()` | `k_msgq_put(&q, &v, timeout)` | FreeRTOS 성공 `pdPASS`(1), Zephyr 성공 0 |
| 앞에 넣기 | `xQueueSendToFront(q, &v, ticks)` | `k_msgq_put_front(&q, &v)` | Zephyr 쪽은 **timeout 인자가 없고 블록하지 않는다** |
| 덮어쓰기 | `xQueueOverwrite(q, &v)` (길이 1 전용) | 없음 (`k_msgq_purge` 후 put으로 흉내) | FreeRTOS는 길이 ≠ 1이면 `configASSERT` |
| 꺼내기 | `xQueueReceive(q, &v, ticks)` | `k_msgq_get(&q, &v, timeout)` | |
| 엿보기 | `xQueuePeek(q, &v, ticks)` | `k_msgq_peek(&q, &v)`, `k_msgq_peek_at(&q, &v, idx)` | 꺼내지 않고 복사만 |
| 개수 | `uxQueueMessagesWaiting()`, `uxQueueSpacesAvailable()` | `k_msgq_num_used_get()`, `k_msgq_num_free_get()` | 읽는 순간의 스냅샷 |
| 비우기 | `xQueueReset(q)` | `k_msgq_purge(&q)` | Zephyr purge는 기다리던 sender를 `-ENOMSG`로 깨운다 |
| ISR에서 | `xQueueSendFromISR(q, &v, &woken)` 등 `...FromISR` | 같은 함수 + `K_NO_WAIT` | §7 |
| 여러 큐 대기 | 큐 셋 `xQueueCreateSet()` … | `k_poll()` + `K_POLL_TYPE_MSGQ_DATA_AVAILABLE` | §8, D5 |

API에서 이미 보이는 함정 하나: **반환값 규약이 반대다.** FreeRTOS `xQueueSend()`는 실패하면 `errQUEUE_FULL`(= 0 = `pdFALSE`)을, Zephyr `k_msgq_put()`은 성공하면 0을 돌려준다. OSAL 없이 코드를 옮기면 `if (send(...))`의 논리가 뒤집힌다(A5에서 실행으로 보였다).

---

## 2. 값 복사 의미론 — FreeRTOS 큐는 바이트를 옮겨 담는다

### 2.1 직관

FreeRTOS 큐에 `xQueueSend(q, &m, 0)`을 부르면 커널은 `&m`에서 `uxItemSize` 바이트를 **그 순간** 큐 저장 영역으로 `memcpy`한다. 그 다음부터 `m`은 보내는 쪽의 것이다. 덮어써도, 스택에서 사라져도 큐 안의 사본은 멀쩡하다. 받는 쪽은 `xQueueReceive(q, &v, …)`에서 큐 안의 사본을 다시 자기 버퍼로 `memcpy`한다. **넣을 때 한 번, 꺼낼 때 한 번** — 데이터가 두 번 복사된다.

이게 기본인 이유는 FreeRTOS 소스 주석이 직접 말한다. `queue.c`의 큐 정의 위에 "Items are queued by copy, not reference."라고 적혀 있다. 값 복사는 (1) 보내는 쪽이 바로 버퍼를 재사용할 수 있고, (2) 스택 변수를 보내도 안전하며, (3) 누가 메모리를 해제하나 같은 소유권 문제가 아예 없다. 대가는 복사 비용과 큐 RAM(칸 × item 크기)이다.

포인터를 보내고 싶으면 "포인터 값 자체"를 item으로 만들면 된다: `xQueueCreate(n, sizeof(blk_t *))` + `xQueueSend(q, &p, 0)`. 이러면 4 바이트(주소)만 복사된다. 대신 **그 주소가 가리키는 메모리의 수명과 소유권은 이제 프로그래머 책임**이다.

### 2.2 예제 1 — 같은 스택 변수를 값으로, 포인터로 보내 보기

무엇을 확인하는 코드인가: 생산자가 **스택 변수 하나**를 세 번 보내면서 보낼 때마다 내용을 바꾼다. 값 큐(`qv`)와 포인터 큐(`qp`)에서 소비자가 무엇을 보게 되는지 비교한다. 소비자는 우선순위가 낮아 세 번의 send가 다 끝난 뒤에 돈다.

```c
/* D4 ex1 — copy semantics: queue of structs (by value) vs queue of pointers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"

typedef struct { uint32_t seq; int16_t gyro[3]; } imu_msg_t;   /* 10 B -> sizeof 12 */

static QueueHandle_t qv, qp;   /* qv: items are imu_msg_t, qp: items are imu_msg_t * */

static void producer(void *pv)
{
    (void)pv;
    imu_msg_t m = { .seq = 1, .gyro = { 10, 20, 30 } };   /* ONE stack variable, reused */
    for (int i = 0; i < 3; i++) {
        xQueueSend(qv, &m, 0);          /* copies sizeof(imu_msg_t) bytes now */
        imu_msg_t *p = &m;
        xQueueSend(qp, &p, 0);          /* copies only the 4-byte address */
        m.seq++; m.gyro[0] += 100;      /* producer keeps writing its buffer */
    }
    board_printf_raw("producer: done, m.seq is now %u\n", (unsigned)m.seq);
    vTaskDelay(pdMS_TO_TICKS(5));       /* keep the stack frame alive */
    board_exit(0);
}

static void consumer(void *pv)
{
    (void)pv;
    vTaskDelay(1);                      /* start late: all 3 sends already done */
    imu_msg_t v; imu_msg_t *p;
    for (int i = 0; i < 3; i++) {
        xQueueReceive(qv, &v, 0);
        xQueueReceive(qp, &p, 0);
        board_printf_raw("by value: seq=%u gx=%d   by pointer(%x): seq=%u gx=%d\n",
                         (unsigned)v.seq, v.gyro[0], (unsigned)(uintptr_t)p,
                         (unsigned)p->seq, p->gyro[0]);
    }
    vTaskDelete(NULL);
}

int main(void)
{
    qv = xQueueCreate(4, sizeof(imu_msg_t));
    qp = xQueueCreate(4, sizeof(imu_msg_t *));
    board_printf_raw("sizeof(imu_msg_t)=%u  item sizes: qv=%u qp=%u\n",
                     (unsigned)sizeof(imu_msg_t), (unsigned)sizeof(imu_msg_t),
                     (unsigned)sizeof(imu_msg_t *));
    xTaskCreate(producer, "prod", 256, NULL, 2, NULL);
    xTaskCreate(consumer, "cons", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex1/ex1.c OUT=/tmp/rtos/D4/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex1/app.elf 10'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12996	    100	 135408	 148504	  24418	/tmp/rtos/D4/ex1/app.elf
sizeof(imu_msg_t)=12  item sizes: qv=12 qp=4
producer: done, m.seq is now 4
by value: seq=1 gx=10   by pointer(200014e4): seq=4 gx=310
by value: seq=2 gx=110   by pointer(200014e4): seq=4 gx=310
by value: seq=3 gx=210   by pointer(200014e4): seq=4 gx=310
[board_exit] code=0
```

출력에서 볼 것: 값 큐는 보낸 순간의 스냅샷 세 개(seq 1·2·3)를 그대로 돌려준다. 포인터 큐는 세 번 모두 **같은 주소 `0x200014e4`**(생산자 스택)를 돌려주고, 그 주소에는 마지막 값(seq 4)만 남아 있다. 게다가 이 예제는 생산자가 `vTaskDelay`로 스택 프레임을 살려 두었기 때문에 이 정도로 끝났다. 생산자 함수가 반환했거나 태스크가 지워졌다면 소비자는 **이미 다른 용도로 쓰이는 스택**을 읽는다 — 출력이 "가끔" 틀리는 최악의 버그다. `sizeof(imu_msg_t)`가 10이 아니라 12인 것도 보자(`uint32_t` 정렬 패딩 2 B). item 크기는 `sizeof`로 정하니 패딩도 같이 복사된다(§9.1).

### 2.3 예제 2 — `Queue_t`의 링 포인터를 직접 움직여 보기

FreeRTOS 큐는 링 버퍼다. 그런데 Don이 아는 "head/tail 인덱스" 링과 조금 다르게 생겼다. 쓰기 포인터 `pcWriteTo`는 **다음에 쓸 칸**을, 읽기 포인터 `u.xQueue.pcReadFrom`은 **마지막으로 읽은 칸**을 가리킨다. 그래서 receive는 "포인터를 한 칸 먼저 옮기고 그 칸을 복사"하고, `xQueueSendToFront()`는 "지금 `pcReadFrom` 칸에 쓰고 포인터를 한 칸 뒤로" 한다. 앞에 넣기가 O(1)인 이유가 이 설계다.

`Queue_t`는 `queue.c` 안의 private 구조체라 앱에서 볼 수 없다. 대신 공개 타입 `StaticQueue_t`가 같은 크기의 "더미" 구조체로 제공된다(kernel-aware 디버거용). 예제는 `Queue_t`와 같은 필드 순서의 미러 구조체를 만들고, 크기가 `StaticQueue_t`와 같은지로 미러가 맞는지 검증한 뒤 포인터를 덤프한다.

무엇을 확인하는 코드인가: 길이 4, item 1 B 큐에 문자를 넣고 빼면서 `pcWriteTo`·`pcReadFrom`이 몇 번 칸을 가리키는지, `xQueueSendToFront`·`xQueuePeek`·`xQueueOverwrite`가 포인터를 어떻게 움직이는지 본다.

```c
/* D4 ex2 — watch Queue_t's ring pointers move: send, sendToFront, peek, receive, overwrite */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#include <string.h>

typedef struct {                       /* mirror of private Queue_t in queue.c (V11.3.2) */
    int8_t *pcHead, *pcWriteTo;
    int8_t *pcTail, *pcReadFrom;       /* u.xQueue */
    List_t xTasksWaitingToSend, xTasksWaitingToReceive;
    volatile UBaseType_t uxMessagesWaiting;
    UBaseType_t uxLength, uxItemSize;
    volatile int8_t cRxLock, cTxLock;
    uint8_t ucStaticallyAllocated;
    void *pxQueueSetContainer;
    UBaseType_t uxQueueNumber; uint8_t ucQueueType;
} QueueMirror_t;

static void dump(const char *what, QueueHandle_t h)
{
    QueueMirror_t *q = (QueueMirror_t *)h;
    unsigned sz = (unsigned)q->uxItemSize;
    board_printf_raw("%s: n=%u spaces=%u  writeTo=slot%u readFrom=slot%u  [",
        what, (unsigned)uxQueueMessagesWaiting(h), (unsigned)uxQueueSpacesAvailable(h),
        (unsigned)((q->pcWriteTo - q->pcHead) / sz), (unsigned)((q->pcReadFrom - q->pcHead) / sz));
    for (unsigned i = 0; i < q->uxLength; i++)
        board_printf_raw(" %c", (char)q->pcHead[i * sz]);
    board_printf_raw(" ]\n");
}

int main(void)
{
    static StaticQueue_t sq; static uint8_t store[4];
    QueueHandle_t q = xQueueCreateStatic(4, 1, store, &sq);
    memset(store, '.', sizeof store);
    board_printf_raw("sizeof(StaticQueue_t)=%u mirror=%u  head=%x tail-head=%u\n",
        (unsigned)sizeof(StaticQueue_t), (unsigned)sizeof(QueueMirror_t),
        (unsigned)(uintptr_t)((QueueMirror_t *)q)->pcHead,
        (unsigned)(((QueueMirror_t *)q)->pcTail - ((QueueMirror_t *)q)->pcHead));
    char c, r;
    dump("created", q);
    c = 'A'; xQueueSend(q, &c, 0);        dump("send A", q);
    c = 'B'; xQueueSend(q, &c, 0);        dump("send B", q);
    c = 'X'; xQueueSendToFront(q, &c, 0); dump("sendToFront X", q);
    xQueuePeek(q, &r, 0);    board_printf_raw("  peek -> %c (n unchanged)\n", r);
    xQueueReceive(q, &r, 0); board_printf_raw("  recv -> %c\n", r); dump("after recv", q);
    c = 'C'; xQueueSend(q, &c, 0);        dump("send C", q);
    c = 'D'; xQueueSend(q, &c, 0);        dump("send D (full)", q);
    c = 'E'; BaseType_t ok = xQueueSend(q, &c, 0);
    board_printf_raw("  send E on full -> %d (errQUEUE_FULL=%d)\n", (int)ok, (int)errQUEUE_FULL);
    while (xQueueReceive(q, &r, 0) == pdPASS) board_printf_raw("  drain -> %c\n", r);
    dump("drained", q);

    static StaticQueue_t sm; static uint8_t mstore[1];
    QueueHandle_t mb = xQueueCreateStatic(1, 1, mstore, &sm);  /* mailbox */
    for (c = '1'; c <= '3'; c++) { xQueueOverwrite(mb, &c); dump("overwrite", mb); }
    xQueuePeek(mb, &r, 0); board_printf_raw("  mailbox peek -> %c, n=%u\n", r,
                                            (unsigned)uxQueueMessagesWaiting(mb));
    board_exit(0);
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex2/ex2.c OUT=/tmp/rtos/D4/ex2 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex2/app.elf 10'
```

```text
sizeof(StaticQueue_t)=84 mirror=84  head=20000110 tail-head=4
created: n=0 spaces=4  writeTo=slot0 readFrom=slot3  [ . . . . ]
send A: n=1 spaces=3  writeTo=slot1 readFrom=slot3  [ A . . . ]
send B: n=2 spaces=2  writeTo=slot2 readFrom=slot3  [ A B . . ]
sendToFront X: n=3 spaces=1  writeTo=slot2 readFrom=slot2  [ A B . X ]
  peek -> X (n unchanged)
  recv -> X
after recv: n=2 spaces=2  writeTo=slot2 readFrom=slot3  [ A B . X ]
send C: n=3 spaces=1  writeTo=slot3 readFrom=slot3  [ A B C X ]
send D (full): n=4 spaces=0  writeTo=slot0 readFrom=slot3  [ A B C D ]
  send E on full -> 0 (errQUEUE_FULL=0)
  drain -> A
  drain -> B
  drain -> C
  drain -> D
drained: n=0 spaces=4  writeTo=slot0 readFrom=slot3  [ A B C D ]
overwrite: n=1 spaces=0  writeTo=slot0 readFrom=slot0  [ 1 ]
overwrite: n=1 spaces=0  writeTo=slot0 readFrom=slot0  [ 2 ]
overwrite: n=1 spaces=0  writeTo=slot0 readFrom=slot0  [ 3 ]
  mailbox peek -> 3, n=1
[board_exit] code=0
```

출력에서 볼 것:

- **생성 직후 `readFrom=slot3`**: `xQueueGenericReset()`이 `pcReadFrom = pcHead + (uxLength − 1) × uxItemSize`로 둔다. "마지막으로 읽은 칸이 맨 끝"이라고 해 두면 첫 receive가 한 칸 옮기면서 slot0으로 감아 돌아온다.
- **`sendToFront X`**: X는 slot3(당시 `pcReadFrom` 위치)에 쓰이고 `pcReadFrom`이 slot2로 물러난다. 다음 receive가 slot3으로 옮겨 X를 읽는다 — 큐 맨 앞에 끼어든 것이다. `pcWriteTo`는 그대로다.
- **peek**: 데이터는 복사하지만 개수(`n`)와 포인터는 그대로다(소스에서는 `pcReadFrom`을 잠깐 옮겼다가 원래 값으로 되돌린다).
- **꽉 찬 뒤 E**: 반환값 0 = `errQUEUE_FULL`. 칸 내용은 바뀌지 않는다. 기본 정책은 "새것을 거절"이다(§5).
- **소비된 칸은 지워지지 않는다**: drain 뒤에도 `[ A B C D ]`가 메모리에 남아 있다. 큐는 개수(`uxMessagesWaiting`)로만 유효 구간을 안다. 민감한 데이터를 큐로 보냈다면 이 점을 기억한다.
- **mailbox(길이 1 + `xQueueOverwrite`)**: 개수는 1에서 늘지 않고 내용만 바뀐다. 읽는 쪽은 `xQueuePeek`로 "최신값"을 지우지 않고 본다.
- `StaticQueue_t` 84 B = 미러 84 B: 이 빌드 설정(정적+동적 할당, 큐 셋, trace 켬)의 제어 구조체 크기다. 저장 영역 4 B는 별도다.

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330" y="20" font-size="14" text-anchor="middle" font-weight="bold">Queue_t와 저장 영역 — "send B" 직후 (예제 2)</text>
<rect x="12" y="40" width="208" height="170" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="116" y="60" font-size="13" text-anchor="middle" font-weight="bold">Queue_t (84 B)</text>
<text x="22" y="82" font-size="12" text-anchor="start">pcHead, pcWriteTo</text>
<text x="22" y="100" font-size="12" text-anchor="start">u.xQueue.pcTail, pcReadFrom</text>
<text x="22" y="118" font-size="12" text-anchor="start">xTasksWaitingToSend</text>
<text x="22" y="136" font-size="12" text-anchor="start">xTasksWaitingToReceive</text>
<text x="22" y="154" font-size="12" text-anchor="start">uxMessagesWaiting = 2</text>
<text x="22" y="172" font-size="12" text-anchor="start">uxLength 4, uxItemSize 1</text>
<text x="22" y="190" font-size="12" text-anchor="start">cRxLock, cTxLock …</text>
<rect x="226" y="60" width="88" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="270" y="87" font-size="13" text-anchor="middle">slot0: A</text>
<rect x="314" y="60" width="88" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="358" y="87" font-size="13" text-anchor="middle">slot1: B</text>
<rect x="402" y="60" width="88" height="44" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="446" y="87" font-size="13" text-anchor="middle">slot2</text>
<rect x="490" y="60" width="88" height="44" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="534" y="87" font-size="13" text-anchor="middle">slot3</text>
<text x="402" y="52" font-size="12" text-anchor="middle">저장 영역 = uxLength × uxItemSize</text>
<line x1="226" y1="142" x2="226" y2="108" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="226" y="156" font-size="12" text-anchor="middle">pcHead</text>
<line x1="446" y1="142" x2="446" y2="108" stroke="#e08a3c" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="446" y="156" font-size="12" text-anchor="middle">pcWriteTo (다음에 쓸 칸)</text>
<line x1="534" y1="182" x2="534" y2="108" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="534" y="196" font-size="12" text-anchor="middle">pcReadFrom (마지막으로 읽은 칸)</text>
<line x1="578" y1="222" x2="578" y2="108" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="600" y="236" font-size="12" text-anchor="end">pcTail (넘으면 pcHead로)</text>
<text x="330" y="270" font-size="12" text-anchor="middle">다음 receive: pcReadFrom을 한 칸 옮김 → slot3 넘어 slot0으로 감김 → A 복사</text>
</svg>
```
그림 2 — 예제 2의 "send B" 상태. `xQueueGenericCreate()`는 `Queue_t`와 저장 영역을 `pvPortMalloc()` 한 번으로 붙여 잡는다(`sizeof(Queue_t) + xQueueSizeInBytes`). 읽기 포인터가 "마지막으로 읽은 칸"을 가리키는 덕분에 앞에 넣기(`xQueueSendToFront`)와 덮어쓰기(`xQueueOverwrite`)가 같은 코드 경로(읽기 포인터 위치에 쓰고 한 칸 뒤로)로 구현된다.

### 2.4 예제 3·4 — item 크기별 비용을 FreeRTOS와 Zephyr에서 같은 보드로 재기

값 복사의 비용은 얼마인가? 두 가지를 잰다.

- **(a) 블록 없는 send / receive**: 같은 태스크가 빈 큐에 넣고 바로 꺼낸다. 커널 함수 자체의 고정 비용 + `memcpy` 한 번씩.
- **(b) 깨움 경로**: 우선순위가 높은 소비자가 빈 큐에서 **블록**해 있고, 생산자가 send한다. send 시작부터 소비자의 receive가 반환될 때까지 — 복사, 대기자 깨우기, 컨텍스트 스위치, 소비자 쪽 복사가 다 들어간 "한 메시지 전달" 시간이다.

item 크기는 4·16·64·256·1024 B, 그리고 정렬이 깨지는 255 B. `memcpy` 단독 시간도 같이 잰다. 측정 오버헤드(빈 측정)는 빼 준다.

무엇을 확인하는 코드인가: FreeRTOS에서 item 크기에 따라 (a)·(b)가 어떻게 늘어나는지, 그리고 증가분이 `memcpy` 몇 번어치인지.

```c
/* D4 ex3 — FreeRTOS queue cost vs item size: (a) send/receive no-block, (b) send->blocked receiver wake */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define CYC() REG32(0x40028018u)           /* FPGAIO 25 MHz counter: 1 = 40 ns */
#define N 200
static const unsigned sizes[] = { 4, 16, 64, 255, 256, 1024 };
static uint8_t txbuf[1024], rxbuf[1024];
static QueueHandle_t q;
static volatile uint32_t t0, lat_sum;
static volatile unsigned cur;

static void consumer(void *pv)              /* prio 3: always blocked in receive */
{
    (void)pv;
    for (;;) {
        xQueueReceive(q, rxbuf, portMAX_DELAY);
        lat_sum += CYC() - t0;              /* send() start -> receive() returned here */
    }
}

static void bench(void *pv)                 /* prio 2 */
{
    (void)pv;
    TaskHandle_t hc = NULL;
    board_printf_raw("size  memcpy  send  recv  send->wake->recv   (cycles @25MHz, 1=40ns)\n");
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        cur = sizes[k];
        q = xQueueCreate(4, cur);
        uint32_t a, s = 0, r = 0, m = 0, e = 0;
        for (int i = 0; i < N; i++) {        /* (a) same task, never blocks */
            a = CYC(); e += CYC() - a;                       /* empty measurement */
            a = CYC(); memcpy(rxbuf, txbuf, cur); m += CYC() - a;
            a = CYC(); xQueueSend(q, txbuf, 0);    s += CYC() - a;
            a = CYC(); xQueueReceive(q, rxbuf, 0); r += CYC() - a;
        }
        e /= N;
        xTaskCreate(consumer, "cons", 256, NULL, 3, &hc);  /* runs now, blocks */
        lat_sum = 0;
        for (int i = 0; i < N; i++) { t0 = CYC(); xQueueSend(q, txbuf, 0); }
        vTaskDelete(hc); vQueueDelete(q);
        board_printf_raw("%u  %u  %u  %u  %u\n", cur, m / N - e, s / N - e, r / N - e,
                         lat_sum / N);
    }
    board_exit(0);
}

int main(void)
{
    for (unsigned i = 0; i < sizeof txbuf; i++) txbuf[i] = (uint8_t)i;
    xTaskCreate(bench, "bench", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex3/ex3.c OUT=/tmp/rtos/D4/ex3 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex3/app.elf 20'
```

```text
size  memcpy  send  recv  send->wake->recv   (cycles @25MHz, 1=40ns)
4  17  90  74  284
16  24  97  81  297
64  44  116  100  335
255  145  221  206  545
256  130  203  187  508
1024  476  549  533  1201
[board_exit] code=0
```

출력에서 볼 것: 4 B item의 send는 90 사이클(3.6 µs, 약 112 명령), receive는 74 사이클이다. 이게 **커널 고정 비용**(크리티컬 섹션 진입·탈출, 개수 검사, 대기 리스트 검사)이고 item 크기와 거의 무관하다. item이 커지면 send와 receive가 각각 정확히 `memcpy` 증가분만큼 늘어난다(1024 B: send 549 = 90 + (476 − 17)). 깨움 경로 증가분은 1201 − 284 = 917 사이클로 `memcpy` 증가분 459의 **정확히 두 배**다 — 넣을 때 한 번, 꺼낼 때 한 번. 255 B는 256 B보다 오히려 느리다(꼬리 바이트 처리와, 큐 칸 시작 주소가 255 B 간격이라 4 B 정렬이 깨지는 칸 때문). newlib의 Cortex-M3 `memcpy`는 4 B 정렬일 때 `ldr`/`str` 16번 펼친 루프를 쓴다(`objdump`로 확인 — 루프 본체가 `ldr r3,[r1,#0] / str r3,[r0,#0] …`).

같은 측정을 Zephyr `k_msgq`로 한다. Zephyr의 기본 빌드는 `CONFIG_SIZE_OPTIMIZATIONS=y`(`-Os`)와 picolibc의 크기 최적화(`CONFIG_PICOLIBC_SIZE_OPTIMIZATIONS=y`)라서, FreeRTOS 템플릿(`-O2` + newlib)과 공정하게 비교하려고 **기본 빌드**와 **속도 빌드**(`CONFIG_SPEED_OPTIMIZATIONS=y`, `CONFIG_PICOLIBC_SIZE_OPTIMIZATIONS=n`) 두 개를 돌린다.

무엇을 확인하는 코드인가: 예제 3과 똑같은 측정을 Zephyr에서. 소비자 스레드는 우선순위 4(숫자가 작을수록 높다), main은 5.

```c
/* D4 ex4z — Zephyr k_msgq cost vs item size, same measurements as FreeRTOS ex3 */
#include <zephyr/kernel.h>
#include <string.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)   /* FPGAIO 25 MHz: 1 = 40 ns */
#define N 200
static const unsigned sizes[] = { 4, 16, 64, 255, 256, 1024 };
static uint8_t txbuf[1024], rxbuf[1024];
static char __aligned(4) qbuf[4 * 1024];
static struct k_msgq q;
static volatile uint32_t t0, lat_sum;
K_THREAD_STACK_DEFINE(cstack, 1024);
static struct k_thread cthr;

static void consumer(void *a, void *b, void *c)     /* prio 4 (higher than main's 5) */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (;;) {
		k_msgq_get(&q, rxbuf, K_FOREVER);
		lat_sum += CYC() - t0;
	}
}

int main(void)
{
	k_thread_priority_set(k_current_get(), 5);
	for (unsigned i = 0; i < sizeof txbuf; i++) txbuf[i] = (uint8_t)i;
	printk("size  memcpy  put  get  put->wake->get   (cycles @25MHz, 1=40ns)\n");
	for (unsigned k = 0; k < ARRAY_SIZE(sizes); k++) {
		unsigned cur = sizes[k];
		k_msgq_init(&q, qbuf, cur, 4);
		uint32_t a, s = 0, r = 0, m = 0, e = 0;
		for (int i = 0; i < N; i++) {
			a = CYC(); e += CYC() - a;
			a = CYC(); memcpy(rxbuf, txbuf, cur); m += CYC() - a;
			a = CYC(); k_msgq_put(&q, txbuf, K_NO_WAIT); s += CYC() - a;
			a = CYC(); k_msgq_get(&q, rxbuf, K_NO_WAIT); r += CYC() - a;
		}
		e /= N;
		k_tid_t tid = k_thread_create(&cthr, cstack, K_THREAD_STACK_SIZEOF(cstack),
					      consumer, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
		k_yield();                       /* consumer is higher prio: already blocked */
		lat_sum = 0;
		for (int i = 0; i < N; i++) { t0 = CYC(); k_msgq_put(&q, txbuf, K_NO_WAIT); }
		k_thread_abort(tid);
		printk("%u  %u  %u  %u  %u\n", cur, m / N - e, s / N - e, r / N - e, lat_sum / N);
	}
	printk("DONE\n");
	return 0;
}
```

`prj.conf`는 `CONFIG_PRINTK=y`, `CONFIG_MAIN_STACK_SIZE=2048` 두 줄이다. Zephyr 보드 기본 `-icount shift=7`을 피하려고 QEMU를 직접 `shift=5`로 실행한다(랩 README의 주의 사항).

```sh
limactl shell elinux -- bash -lc 'export XDG_CACHE_HOME=/tmp/rtos/D4/xdgcache; Z=/Users/donh/.rtos_scratch/D4/ex4z
/var/rtoslab/bin/rtos-zbuild mps2/an385 $Z /tmp/rtos/D4/ex4z
/var/rtoslab/bin/rtos-zbuild mps2/an385 $Z /tmp/rtos/D4/ex4z_fast -- -DCONFIG_SPEED_OPTIMIZATIONS=y -DCONFIG_PICOLIBC_SIZE_OPTIMIZATIONS=n
for d in ex4z ex4z_fast; do echo "## $d"; timeout 20 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel /tmp/rtos/D4/$d/zephyr/zephyr.elf 2>&1 | sed "/DONE/q"; done'
```

```text
## ex4z
*** Booting Zephyr OS build v4.4.2 ***
size  memcpy  put  get  put->wake->get   (cycles @25MHz, 1=40ns)
4  20  56  49  504
16  58  94  88  543
64  212  248  241  696
255  823  859  855  1308
256  826  865  856  1311
1024  3286  3322  3318  3768
DONE
## ex4z_fast
*** Booting Zephyr OS build v4.4.2 ***
size  memcpy  put  get  put->wake->get   (cycles @25MHz, 1=40ns)
4  16  48  48  451
16  23  55  54  458
64  43  75  74  476
255  144  178  178  578
256  128  160  160  563
1024  473  505  504  908
DONE
```

출력에서 볼 것, 세 가지.

1. **Zephyr 기본 빌드의 `memcpy`는 바이트 루프다.** 1024 B에 3286 사이클(바이트당 3.2) — newlib(476)의 7배. `arm-none-eabi-objdump -d`로 보면 picolibc 크기 최적화 `memcpy`가 `ldrb.w r4,[r1],#1` / `strb.w r4,[r3,#1]!` 네 줄짜리 루프다. 큰 메시지를 Zephyr 기본 설정으로 큐에 넣으면 커널이 아니라 **libc 설정** 때문에 느리다. 속도 빌드에서는 newlib과 거의 같다(1024 B: 473).
2. **블록 없는 put/get은 Zephyr가 더 싸다**(4 B: 48 + 48 vs FreeRTOS 90 + 74). Zephyr는 spinlock(단일 코어에서는 `irq_lock`) 한 번에 링 버퍼 조작만 하고, FreeRTOS는 크리티컬 섹션 + 큐 셋 검사 + 대기 리스트 검사가 더 들어간다.
3. **깨움 경로는 고정 비용과 기울기가 반대다.** 4 B에서는 FreeRTOS 284 vs Zephyr 451로 FreeRTOS가 싸다(Zephyr의 `z_pend_curr` → `z_swap` 경로가 무겁다). 그런데 1024 B까지의 증가분은 FreeRTOS 917(복사 **두 번**) vs Zephyr 457(복사 **한 번**)이다. Zephyr `k_msgq_put()`은 기다리는 수신자가 있으면 큐 저장 영역을 건너뛰고 **수신자의 버퍼로 바로** `memcpy`한다(direct handoff — §10.5에서 소스로 본다). 그래서 약 400 B 근처부터 Zephyr 깨움 경로가 더 빨라진다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="330.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">큐 비용 vs item 크기 (QEMU mps2-an385, ICOUNT=1, 사이클 @25 MHz)</text>
<line x1="70" y1="300" x2="520" y2="300" stroke="currentColor"/>
<line x1="70" y1="30" x2="70" y2="300" stroke="currentColor"/>
<line x1="66" y1="300.0" x2="520" y2="300.0" stroke="#888" stroke-opacity="0.25"/>
<text x="62.0" y="304.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="250.0" x2="520" y2="250.0" stroke="#888" stroke-opacity="0.25"/>
<text x="62.0" y="254.0" font-size="12" text-anchor="end">250</text>
<line x1="66" y1="200.0" x2="520" y2="200.0" stroke="#888" stroke-opacity="0.25"/>
<text x="62.0" y="204.0" font-size="12" text-anchor="end">500</text>
<line x1="66" y1="150.0" x2="520" y2="150.0" stroke="#888" stroke-opacity="0.25"/>
<text x="62.0" y="154.0" font-size="12" text-anchor="end">750</text>
<line x1="66" y1="100.0" x2="520" y2="100.0" stroke="#888" stroke-opacity="0.25"/>
<text x="62.0" y="104.0" font-size="12" text-anchor="end">1000</text>
<line x1="66" y1="50.0" x2="520" y2="50.0" stroke="#888" stroke-opacity="0.25"/>
<text x="62.0" y="54.0" font-size="12" text-anchor="end">1250</text>
<text x="70.0" y="318.0" font-size="12" text-anchor="middle">4 B</text>
<text x="180.0" y="318.0" font-size="12" text-anchor="middle">16 B</text>
<text x="290.0" y="318.0" font-size="12" text-anchor="middle">64 B</text>
<text x="400.0" y="318.0" font-size="12" text-anchor="middle">256 B</text>
<text x="510.0" y="318.0" font-size="12" text-anchor="middle">1024 B</text>
<text x="290.0" y="340.0" font-size="12" text-anchor="middle">item 크기 (가로축은 4배 간격)</text>
<text x="18.0" y="170.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 170.0)">cycles</text>
<polyline points="70.0,243.2 180.0,240.6 290.0,233.0 400.0,198.4 510.0,59.8" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="243.2" r="3" fill="#4a7bd0"/>
<circle cx="180.0" cy="240.6" r="3" fill="#4a7bd0"/>
<circle cx="290.0" cy="233.0" r="3" fill="#4a7bd0"/>
<circle cx="400.0" cy="198.4" r="3" fill="#4a7bd0"/>
<circle cx="510.0" cy="59.8" r="3" fill="#4a7bd0"/>
<text x="520.0" y="63.8" font-size="12" text-anchor="start">FreeRTOS 깨움 경로</text>
<polyline points="70.0,209.8 180.0,208.4 290.0,204.8 400.0,187.4 510.0,118.4" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="209.8" r="3" fill="#e08a3c"/>
<circle cx="180.0" cy="208.4" r="3" fill="#e08a3c"/>
<circle cx="290.0" cy="204.8" r="3" fill="#e08a3c"/>
<circle cx="400.0" cy="187.4" r="3" fill="#e08a3c"/>
<circle cx="510.0" cy="118.4" r="3" fill="#e08a3c"/>
<text x="520.0" y="122.4" font-size="12" text-anchor="start">Zephyr 깨움 경로</text>
<polyline points="70.0,267.2 180.0,264.4 290.0,256.8 400.0,222.0 510.0,83.6" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5 4"/>
<circle cx="70.0" cy="267.2" r="3" fill="#4a7bd0"/>
<circle cx="180.0" cy="264.4" r="3" fill="#4a7bd0"/>
<circle cx="290.0" cy="256.8" r="3" fill="#4a7bd0"/>
<circle cx="400.0" cy="222.0" r="3" fill="#4a7bd0"/>
<circle cx="510.0" cy="83.6" r="3" fill="#4a7bd0"/>
<text x="520.0" y="87.6" font-size="12" text-anchor="start">FreeRTOS send+recv</text>
<polyline points="70.0,280.8 180.0,278.2 290.0,270.2 400.0,236.0 510.0,98.2" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 4"/>
<circle cx="70.0" cy="280.8" r="3" fill="#e08a3c"/>
<circle cx="180.0" cy="278.2" r="3" fill="#e08a3c"/>
<circle cx="290.0" cy="270.2" r="3" fill="#e08a3c"/>
<circle cx="400.0" cy="236.0" r="3" fill="#e08a3c"/>
<circle cx="510.0" cy="98.2" r="3" fill="#e08a3c"/>
<text x="520.0" y="103.6" font-size="12" text-anchor="start">Zephyr put+get</text>
</svg>
```
그림 3 — 예제 3(FreeRTOS)·예제 4(Zephyr 속도 빌드)의 측정값. 실선은 "보내기 → 블록된 수신자 깨움 → 수신 반환", 점선은 블록 없는 send + receive 한 쌍. FreeRTOS 깨움 경로의 기울기가 Zephyr의 두 배인 것이 "복사 두 번 vs 한 번"이다. 4 B 포인터 하나를 보내면 크기와 상관없이 맨 왼쪽 점의 비용이다(FreeRTOS 284 사이클 = 11.4 µs).

### 2.5 그래서 언제 포인터를 넘기나 — 판단 기준

숫자로 정리하면 이렇다. 이 보드(25 MHz, 캐시 없음)에서 FreeRTOS 메시지 하나의 고정 비용은 약 284 사이클이고, 바이트당 약 0.9 사이클(복사 두 번)이 더 붙는다. **item이 64 B면 복사가 고정 비용의 18%, 256 B면 79%, 1024 B면 고정 비용의 3배**다. 여기에 RAM이 더해진다: 길이 16 큐에 256 B item이면 저장 영역만 4 KB다.

| item 크기 · 성격 | 권장 | 이유 |
|---|---|---|
| ≤ 16~32 B (IMU 샘플, 명령, 이벤트) | **값 복사** | 복사 비용이 고정 비용에 묻힌다. 소유권 문제 0 |
| 32~128 B (CAN FD 프레임 64 B, 텔레메트리 패킷) | 대개 값 복사, 빈도가 높으면 측정 후 결정 | RAM(길이 × 크기)이 먼저 문제가 된다 |
| ≥ 수백 B (DMA 블록, 카메라 라인, 로그 페이지) | **포인터 + 버퍼 풀**(§3) | 복사가 지배적이고 RAM이 두 배로 든다 |
| 가변 길이 바이트 스트림 (UART 수신, 로그 문자열) | 큐 말고 stream / message buffer | D5 — 단일 생산자·소비자용으로 더 가볍다 |
| "최신값만 의미 있음" (자세 추정 결과, 배터리 전압) | 길이 1 mailbox + `xQueueOverwrite` | §5 — 큐에 쌓을 이유가 없다 |

### 2.6 함정

- **스택 변수의 주소를 보낸다**: 예제 1처럼 "가끔" 틀린다. 포인터를 보낼 때는 그 메모리가 **정적이거나 풀에서 빌린 것**이어야 한다.
- **item 크기를 `sizeof(포인터)`로 만들고 구조체를 보낸다** (또는 반대): `xQueueCreate(8, sizeof(msg_t *))`에 `xQueueSend(q, &msg, 0)`을 부르면 구조체의 앞 4 B만 복사된다. 컴파일러는 `void *`라 아무 말도 하지 않는다. 생성과 send/receive가 같은 타입을 쓰도록 래퍼 함수를 두는 것이 안전하다.
- **받는 버퍼가 item보다 작다**: `xQueueReceive(q, &small, …)`는 `uxItemSize` 바이트를 무조건 쓴다 — 스택 오버플로. POSIX `mq_receive`처럼 크기를 검사해 주지 않는다.
- **Zephyr 기본 빌드에서 큰 메시지**: 바이트 루프 `memcpy`(위 1번). 큰 메시지를 쓰는 Zephyr 제품은 libc 최적화 설정을 확인한다.

---

## 3. 소유권 넘기기 — 빈 버퍼 큐 + 찬 버퍼 큐

### 3.1 직관

큰 데이터는 복사하지 않고 **포인터만 큐로 보낸다**. 그 순간 버퍼의 주인이 바뀐다. 이걸 규칙으로 만든 것이 **두 큐 버퍼 풀(two-queue buffer pool)** 이다.

- 버퍼 N개를 정적으로 잡아 둔다(`pool[N]`).
- **free 큐**: 빈 버퍼의 포인터들. 처음에 N개를 다 넣어 둔다.
- **full 큐**: 채워진 버퍼의 포인터들.
- 생산자: free 큐에서 하나 꺼낸다 → 채운다 → full 큐로 보낸다. **보낸 뒤에는 그 버퍼를 절대 만지지 않는다.**
- 소비자: full 큐에서 꺼낸다 → 처리한다 → free 큐로 돌려준다. 돌려준 뒤에는 만지지 않는다.

버퍼 하나는 언제나 **정확히 한 곳**에 있다: free 큐 안, 생산자 손, full 큐 안, 소비자 손. 큐 두 개의 길이를 둘 다 N으로 잡으면 어느 쪽 send도 실패할 수 없다(버퍼가 N개뿐이니까). "풀이 비었다"는 free 큐 receive 실패로 바로 드러난다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330" y="18" font-size="14" text-anchor="middle" font-weight="bold">두 큐 버퍼 풀 — 큐에는 4 B 포인터만, 데이터는 제자리</text>
<rect x="250" y="36" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="270" y="56" font-size="12" text-anchor="middle">p2</text>
<rect x="290" y="36" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="310" y="56" font-size="12" text-anchor="middle">p0</text>
<rect x="330" y="36" width="40" height="30" fill="#888" fill-opacity="0.05" stroke="#888"/>
<rect x="370" y="36" width="40" height="30" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="330" y="84" font-size="12" text-anchor="middle">full_q — 채운 블록의 포인터 (소유권 ISR → 태스크)</text>
<rect x="20" y="110" width="170" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="105" y="136" font-size="13" text-anchor="middle">ISR (생산자)</text>
<text x="105" y="158" font-size="12" text-anchor="middle">free_q에서 꺼내 채움</text>
<rect x="250" y="110" width="160" height="70" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330" y="136" font-size="13" text-anchor="middle">pool[NBUF]</text>
<text x="330" y="158" font-size="12" text-anchor="middle">64 B 블록, 복사 0번</text>
<rect x="470" y="110" width="170" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="555" y="136" font-size="13" text-anchor="middle">태스크 (소비자)</text>
<text x="555" y="158" font-size="12" text-anchor="middle">처리 후 free_q로 반납</text>
<path d="M150,108 Q170,51 246,51" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<path d="M412,51 Q500,51 520,106" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<rect x="250" y="214" width="40" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="270" y="234" font-size="12" text-anchor="middle">p1</text>
<rect x="290" y="214" width="40" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="310" y="234" font-size="12" text-anchor="middle">p3</text>
<rect x="330" y="214" width="40" height="30" fill="#888" fill-opacity="0.05" stroke="#888"/>
<rect x="370" y="214" width="40" height="30" fill="#888" fill-opacity="0.05" stroke="#888"/>
<path d="M520,182 Q500,229 414,229" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<path d="M248,229 Q170,229 150,184" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<text x="330" y="266" font-size="12" text-anchor="middle">free_q — 빈 블록의 포인터 (소유권 태스크 → 풀 → ISR)</text>
<text x="330" y="288" font-size="12" text-anchor="middle">블록 하나는 언제나 한 곳에만 있다: free_q · ISR 손 · full_q · 태스크 손</text>
</svg>
```
그림 4 — 두 큐 버퍼 풀. 큐에 실리는 것은 4 B 포인터뿐이고 64 B 블록은 `pool[]`에서 움직이지 않는다. 두 큐 길이를 모두 NBUF로 잡으면 send는 실패할 수 없고, 풀 고갈은 free 큐 receive 실패로만 나타난다.

### 3.2 규칙 — "보낸 쪽은 보낸 뒤에 만지지 않는다"

포인터를 send한 순간 생산자의 손에는 **주소만 남고 권리는 없다**. 소비자는 이미 깨어났을 수 있고(우선순위가 높으면 send 안에서 바로 선점한다), 처리를 끝내고 free 큐로 돌려줬을 수도 있고, 다음 ISR이 그 블록을 다시 꺼내 채우는 중일 수도 있다. 이 규칙을 어기는 전형적인 코드가 "send 다음 줄에 `b->len = n;` 한 줄 더"다. 테스트에서는 소비자가 늦게 돌아 안 보이다가, 우선순위가 바뀌는 날 터진다.

규칙을 강제하는 실무 기법 셋:

- **owner 필드**: 블록 안에 `owner`를 두고 넘길 때마다 바꾼다. 받는 쪽은 "내 것이어야 할 블록"인지 확인한다(디버그 빌드에서 `configASSERT`).
- **체크섬/시퀀스**: 생산자가 send 직전에 계산해 넣고 소비자가 검증한다. 보낸 뒤 쓰기(use-after-send)를 잡는다.
- **send 뒤 포인터를 NULL로**: `xQueueSend(full_q, &b, 0); b = NULL;` — 다음 줄에서 `b->`를 쓰면 HardFault로 바로 드러난다.

### 3.3 예제 5 — 1 kHz ISR 생산자 + 태스크 소비자 버퍼 풀

무엇을 확인하는 코드인가: TIMER0 ISR(1 kHz, "IMU DMA 완료" 흉내)이 free 큐에서 64 B 블록을 꺼내 채우고 full 큐로 넘긴다. 소비자는 0.5초마다 한 번 6 ms 걸리는 "플래시 쓰기"를 한다. (1) 풀 4개와 8개에서 손실이 어떻게 다른지, (2) owner 필드·체크섬이 "보낸 뒤 만지기" 버그(`-DBUG_TOUCH_AFTER_SEND`)를 잡는지 본다. TIMER0 우선순위는 `0x40`으로, `configMAX_SYSCALL_INTERRUPT_PRIORITY`(0x20)보다 숫자가 커야(= 논리적으로 낮아야) `FromISR` API를 쓸 수 있다(E1·B3).

```c
/* D4 ex5 — ownership transfer: free-list queue + full queue of pointers (two-queue buffer pool) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#ifndef NBUF
#define NBUF 4
#endif
enum { OWN_POOL = 1, OWN_ISR, OWN_TASK };
typedef struct { uint32_t owner, seq, t_us, sum; int16_t s[24]; } blk_t;  /* 64 B */
static blk_t pool[NBUF];
static QueueHandle_t free_q, full_q;              /* both carry blk_t * (4 B items) */
static volatile uint32_t seq, drops, bad, done, minfree = NBUF;

void TIMER0_IRQHandler(void)                      /* 1 kHz "IMU DMA complete" */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t woken = pdFALSE; blk_t *b;
    UBaseType_t nf = uxQueueMessagesWaitingFromISR(free_q);
    if (nf < minfree) minfree = nf;
    if (xQueueReceiveFromISR(free_q, &b, &woken) != pdPASS) { drops++; return; }
    if (b->owner != OWN_POOL) bad++;
    b->owner = OWN_ISR; b->seq = seq++; b->t_us = board_now_us(); b->sum = 0;
    for (int i = 0; i < 24; i++) { b->s[i] = (int16_t)(b->seq + i); b->sum += (uint16_t)b->s[i]; }
    b->owner = OWN_TASK;                          /* hand over ... */
    xQueueSendFromISR(full_q, &b, &woken);        /* ... and never touch b again */
#ifdef BUG_TOUCH_AFTER_SEND
    b->s[0] = 0x7fff;                             /* "just one more field" bug */
#endif
    portYIELD_FROM_ISR(woken);
}

static void consumer(void *pv)
{
    (void)pv; blk_t *b;
    while (done < 2000) {
        xQueueReceive(full_q, &b, portMAX_DELAY);
        uint32_t sum = 0;
        for (int i = 0; i < 24; i++) sum += (uint16_t)b->s[i];
        if (b->owner != OWN_TASK || sum != b->sum) bad++;
        if (b->seq % 500 == 499) {                /* every 0.5 s: 6 ms flash write */
            uint32_t t = board_now_us(); while (board_now_us() - t < 6000) { }
        }
        b->owner = OWN_POOL; done++;
        xQueueSend(free_q, &b, 0);                /* give it back: owner is the pool again */
    }
    board_printf_raw("NBUF=%u processed=%u drops=%u bad=%u min_free=%u\n", NBUF,
        (unsigned)done, (unsigned)drops, (unsigned)bad, (unsigned)minfree);
    board_exit(0);
}

int main(void)
{
    free_q = xQueueCreate(NBUF, sizeof(blk_t *));
    full_q = xQueueCreate(NBUF, sizeof(blk_t *));   /* NBUF deep: can never overflow */
    for (int i = 0; i < NBUF; i++) { blk_t *b = &pool[i]; b->owner = OWN_POOL; xQueueSend(free_q, &b, 0); }
    TIMER_RELOAD(TIMER0_BASE) = 25000 - 1;          /* 25 MHz / 25000 = 1 kHz */
    TIMER_CTRL(TIMER0_BASE) = (1u << 0) | (1u << 3);
    NVIC_IPR(8) = 0x40;  NVIC_ISER0 = 1u << 8;      /* below configMAX_SYSCALL (0x20) */
    xTaskCreate(consumer, "cons", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D4/ex5/ex5.c; for v in "4 x" "8 x" "8 BUG"; do set -- $v; X=""; [ $2 = BUG ] && X="-DBUG_TOUCH_AFTER_SEND"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S OUT=/tmp/rtos/D4/ex5_$1$2 EXTRA_CFLAGS="-DNBUF=$1 $X" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex5_$1$2/app.elf 20; done'
```

```text
NBUF=4 processed=2000 drops=12 bad=0 min_free=0
[board_exit] code=0
NBUF=8 processed=2000 drops=0 bad=0 min_free=2
[board_exit] code=0
NBUF=8 processed=2000 drops=0 bad=2000 min_free=2
[board_exit] code=0
```

출력에서 볼 것: 6 ms 정지 동안 1 kHz ISR은 블록 6개를 원한다. 풀 4개 중 1개는 소비자 손에 있으니 빈 블록은 3개 — 정지 한 번에 3개씩, 2000블록 동안 정지 4번(seq 499·999·1499·1999)이라 손실 12다. 풀 8개면 정지 중 최소 여유가 2개(`min_free=2`) 남고 손실 0이다. 풀 크기도 §4의 깊이 계산과 같은 문제다: **소비자가 못 오는 시간 × 도착률 + 소비자가 쥐고 있는 개수**. 버그 빌드에서는 2000개 **전부**가 `bad`로 잡힌다. ISR이 send 뒤에 쓴 한 줄은, 소비자가 ISR 리턴 뒤에 돌기 때문에 매번 체크섬을 깨뜨린다. 소비자가 더 높은 우선순위의 다른 ISR 뒤에 돌거나 멀티코어였다면 "가끔"만 깨졌을 것이다 — 그래서 체크섬 같은 검증을 디버그 빌드에 넣어 둔다.

### 3.4 Don의 SSD 펌웨어와 연결

이 구조는 SSD 펌웨어의 **명령 슬롯 풀**과 같다. NVMe 명령이 들어오면 디스패처가 free 슬롯 리스트에서 슬롯(명령 컨텍스트 + 데이터 버퍼 디스크립터)을 하나 꺼내고, 슬롯 인덱스만 FTL 큐 → NAND 큐 → 완료 큐로 옮기다가, 완료가 끝나면 free 리스트로 돌린다. 슬롯 인덱스(또는 NVMe Command Identifier)가 이 예제의 `blk_t *`이고, "슬롯 하나는 언제나 한 단계 큐에만 있다"가 소유권 규칙이다. 차이는 둘이다. SSD 펌웨어는 보통 인덱스를 담은 lock-free 링(단일 생산자·소비자)이나 연결 리스트를 쓰고, RTOS 큐는 거기에 **블로킹**(free가 비면 생산자를 재우고, full이 비면 소비자를 재움)을 얹어 준다. ISR ↔ 태스크 사이를 커널 없이 SPSC 링으로 잇는 방법은 D8에서 다룬다.

### 3.5 함정

- **풀 크기를 "평균"으로 잡는다**: 예제 5에서 평균 처리 시간은 1 ms보다 훨씬 짧지만 6 ms 정지 한 번에 손실이 난다. 최악 정지 시간으로 잡는다(§4).
- **같은 블록을 두 번 반납(double free)**: free 큐에 같은 포인터가 두 개 들어가면 나중에 두 생산자가 같은 블록을 채운다. owner 필드 검사로 잡는다(free 큐에 넣기 전 `owner == OWN_POOL`이면 이미 반납된 것).
- **free 큐를 풀보다 짧게 만든다**: 반납 send가 실패하면 블록이 영영 사라진다(누수). 두 큐 모두 길이 = NBUF.
- **DMA + D-cache**: 실칩(M7/H7)에서 DMA가 채운 블록 포인터를 받으면, 소비자가 읽기 전에 해당 캐시 라인을 invalidate해야 한다. QEMU는 캐시를 모델링하지 않아 여기서는 보여 줄 수 없다 — B8·E6에서 다룬다.

---

## 4. 깊이 설계 — 버스트, 서비스 시간, Little's law

### 4.1 직관 — 깊이는 "소비자가 못 오는 동안 쌓이는 양"이다

큐 깊이를 정하는 질문은 "평균 몇 개가 들어 있나"가 아니라 **"소비자가 가장 오래 못 오는 동안 몇 개가 도착하나"** 다. 큐는 도착과 처리의 **일시적인** 속도 차이를 흡수하는 완충기다. 장기적으로 도착률이 처리율보다 크면(과부하) 어떤 깊이도 소용없다 — 큐는 언젠가 차고, 깊이는 "언제 터지나"만 늦춘다. 그래서 깊이 설계는 두 단계다.

1. **안정 조건**: 평균 이용률 ρ = λ × S < 1 (λ = 평균 도착률, S = 메시지 하나의 처리 시간). 이게 깨지면 설계를 바꿔야 한다(소비자 빠르게, 묶어서 처리, 생산 줄이기).
2. **최악 적체(backlog)**: ρ < 1이어도 도착이 몰리는 구간(버스트)이나 소비자가 막히는 구간(더 높은 우선순위 태스크, 긴 크리티컬 섹션, 플래시 쓰기)에 큐가 쌓인다. 깊이 ≥ 그 최대 적체.

### 4.2 수학 — Don의 응용수학으로 한 장

**(1) 소비자 공백(gap) 모델.** 소비자가 최대 R_max 동안 큐를 비우지 못하고, 그동안 최대 도착률 λ_peak로 들어온다면

> D ≥ ⌈ λ_peak × R_max ⌉ + (소비자가 쥐고 있는 개수)

R_max는 소비자 태스크의 **최악 응답 시간**(worst-case response time)이다 — 더 높은 우선순위 태스크와 ISR이 소비자를 얼마나 오래 막을 수 있는지. 이걸 계산하는 것이 응답 시간 분석(RTA, N 모듈)이다. 예제 5의 풀 크기 계산(정지 6 ms × 1 kHz = 6, 소비자 손에 1)이 이 공식이다.

**(2) 버스트 모델.** 간격 T_a로 B개가 몰려오고, 소비자가 하나에 S(> T_a)씩 걸려 시각 0, S, 2S, …에 하나씩 꺼낸다면, 마지막 도착 시각 (B − 1)T_a까지 꺼낸 개수는 k = ⌊(B − 1)T_a / S⌋ + 1이고 최대 적체는

> Q_max = B − k = B − ⌊(B − 1) T_a / S⌋ − 1

이다. 버스트가 끝난 뒤 다음 버스트 전에 다 비워야 하므로 **버스트 주기 P ≥ B × S**(= 그 구간의 ρ < 1)도 함께 확인한다.

**(3) Little's law.** 안정된 대기 시스템에서 **L = λ W** — 평균 대기 개수 L은 도착률 λ × 평균 대기 시간 W다. 분포와 무관하게 성립하는 정리라서, 측정값 셋 중 둘을 재면 나머지를 검산할 수 있다. 실무에서는 "평균 큐 길이 × (1/λ) = 평균 지연"으로, 큐가 지연을 얼마나 더하는지를 바로 계산하는 데 쓴다. 단, **L은 평균이라 깊이 설계에는 쓰지 않는다** — 깊이는 (1)·(2)의 최악값으로 정한다. 평균 4.7개인 큐에 깊이 21이 필요하다는 것을 아래 실험이 보여 준다.

### 4.3 예제 6 — 타이머 ISR 버스트 생산자로 깊이별 손실 재기

실험 설계: TIMER0 ISR이 100 µs마다 돈다(10 kHz). 20 ms 주기 중 **앞 6 ms(60틱) 동안만** 틱마다 메시지 하나를 `xQueueSendFromISR`로 넣는다 — 버스트 B = 60, T_a = 100 µs, 주기 P = 20 ms. 소비자는 메시지 하나에 150 µs를 쓴다(바쁜 대기). 평균 이용률 ρ = (60 / 20 ms) × 150 µs = 0.45로 안정적이다. 큐가 차면 **새것을 버리고 센다**(drop newest). 깊이 1~32에서 20 버스트씩 돌린다. 깊이 32 실행에서는 (a) 두 번째 주기의 큐 길이를 틱마다 기록하고, (b) ISR이 매 틱 큐 길이를 더해 시간 평균 L을, 소비자가 메시지마다 "큐에 들어간 시각 → 꺼낸 시각"을 더해 W를 구해 Little's law를 검산한다.

무엇을 확인하는 코드인가: 깊이별 손실이 §4.2 버스트 공식대로 나오는지, 큐 길이 궤적, L = λW.

```c
/* D4 ex6 — queue depth vs drops: bursty 10 kHz ISR producer, 150 us consumer; Little's law check */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define CYC() REG32(0x40028018u)                  /* 25 MHz: 1 = 40 ns */
#define PERIOD 200                                /* ticks of 100 us = 20 ms pattern */
#define BURST  60                                 /* first 60 ticks: one message each */
#define SVC    3750                               /* 150 us service time in CYC units */
typedef struct { uint32_t t_enq, seq; } msg_t;
static QueueHandle_t q;
static volatile uint32_t tick, sent, drops, occ_sum, wait_sum, got;
static volatile uint8_t trace[PERIOD]; static volatile int tracing;

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    UBaseType_t n = uxQueueMessagesWaitingFromISR(q);
    occ_sum += n;                                 /* time-average of L (sampled every 100 us) */
    uint32_t ph = tick++ % PERIOD;                /* trace: 2nd period of the depth-32 run */
    if (tracing && tick > PERIOD && tick <= 2 * PERIOD) trace[ph] = (uint8_t)n;
    if (ph >= BURST) return;
    BaseType_t woken = pdFALSE;
    msg_t m = { CYC(), sent++ };
    if (xQueueSendFromISR(q, &m, &woken) != pdPASS) drops++;   /* policy: drop newest */
    portYIELD_FROM_ISR(woken);
}

static void consumer(void *pv)
{
    (void)pv; msg_t m;
    for (;;) {
        xQueueReceive(q, &m, portMAX_DELAY);
        wait_sum += CYC() - m.t_enq; got++;       /* W = time spent waiting in the queue */
        uint32_t t = CYC(); while (CYC() - t < SVC) { }
    }
}

static void ctl(void *pv)
{
    (void)pv;
    static const unsigned depth[] = { 1, 2, 4, 8, 12, 16, 18, 20, 24, 32 };
    board_printf_raw("depth sent drops drop%% L_avg x100  lambda*W x100\n");
    for (unsigned k = 0; k < sizeof depth / sizeof depth[0]; k++) {
        TaskHandle_t hc; q = xQueueCreate(depth[k], sizeof(msg_t));
        tick = sent = drops = occ_sum = wait_sum = got = 0; tracing = (depth[k] == 32);
        xTaskCreate(consumer, "cons", 256, NULL, 3, &hc);
        TIMER_CTRL(TIMER0_BASE) = (1u << 0) | (1u << 3);
        vTaskDelay(pdMS_TO_TICKS(400));            /* 20 bursts */
        TIMER_CTRL(TIMER0_BASE) = 0;
        vTaskDelay(pdMS_TO_TICKS(10));             /* let the consumer drain */
        vTaskDelete(hc); vQueueDelete(q);
        uint32_t L100 = occ_sum * 100 / tick;      /* avg items in queue x100 */
        /* lambda = got / (tick * 100us);  W(us) = wait_sum / 25 / got  ->  lambda*W = wait_sum/(25*100*tick) */
        uint32_t lw100 = (uint32_t)((uint64_t)wait_sum * 100 / (2500ull * tick));
        board_printf_raw("%u %u %u %u %u %u\n", depth[k], (unsigned)sent, (unsigned)drops,
                         (unsigned)(drops * 100 / sent), (unsigned)L100, (unsigned)lw100);
    }
    board_printf_raw("trace(depth 32, one 20 ms period, every 5th tick):");
    for (int i = 0; i < PERIOD; i += 5) board_printf_raw(" %u", trace[i]);
    board_printf_raw("\n"); board_exit(0);
}

int main(void)
{
    TIMER_RELOAD(TIMER0_BASE) = 2500 - 1; NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8; /* 100 us */
    xTaskCreate(ctl, "ctl", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex6/ex6.c OUT=/tmp/rtos/D4/ex6 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex6/app.elf 60'
```

```text
depth sent drops drop% L_avg x100  lambda*W x100
1 1200 400 33 10 20
2 1200 380 31 39 49
4 1200 340 28 95 106
8 1200 260 21 199 211
12 1200 180 15 293 305
16 1200 100 8 376 390
18 1200 60 5 414 428
20 1200 20 1 448 463
24 1200 0 0 465 480
32 1200 0 0 466 481
trace(depth 32, one 20 ms period, every 5th tick): 0 1 3 5 7 8 10 12 14 15 17 19 21 17 14 11 8 4 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
[board_exit] code=0
```

출력에서 볼 것: 손실 ÷ 20 버스트 = 깊이 1에서 20, 2에서 19, 4에서 17, … 20에서 1, 24 이상에서 0. **버스트당 손실 = max(0, 21 − D)** 라는 깨끗한 직선이다. 큐 길이 궤적은 버스트 동안 틱당 약 0.35개씩 쌓여 6 ms 끝(틱 60)에 **21**에서 꼭짓점을 찍고, 버스트가 끝나면 150 µs당 하나씩 3 ms 만에 비워진다. 그 뒤 14 ms는 빈 큐다. 그래서 시간 평균 L은 4.66개뿐인데 필요한 깊이는 21이다 — **평균으로 깊이를 잡으면 안 되는 이유**가 숫자로 보인다. 손실률 1%(깊이 20)도 "1%면 괜찮다"가 아니다. 그 1%는 매 버스트의 **마지막** 메시지 하나이고, IMU라면 매 20 ms마다 같은 위상의 샘플이 빠지는 체계적 오류다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">큐 깊이별 버스트당 손실 (측정 막대 · 모델 max(0, 21 − D) 점선)</text>
<line x1="60" y1="270" x2="620" y2="270" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="270" stroke="currentColor"/>
<line x1="56" y1="270.0" x2="620" y2="270.0" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="56" y1="220.0" x2="620" y2="220.0" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="224.0" font-size="12" text-anchor="end">5</text>
<line x1="56" y1="170.0" x2="620" y2="170.0" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="174.0" font-size="12" text-anchor="end">10</text>
<line x1="56" y1="120.0" x2="620" y2="120.0" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="124.0" font-size="12" text-anchor="end">15</text>
<line x1="56" y1="70.0" x2="620" y2="70.0" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="74.0" font-size="12" text-anchor="end">20</text>
<rect x="68.0" y="70.0" width="40.0" height="200.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="88.0" y="86.0" font-size="12" text-anchor="middle">20</text>
<text x="88.0" y="288.0" font-size="12" text-anchor="middle">1</text>
<rect x="124.0" y="80.0" width="40.0" height="190.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="144.0" y="96.0" font-size="12" text-anchor="middle">19</text>
<text x="144.0" y="288.0" font-size="12" text-anchor="middle">2</text>
<rect x="180.0" y="100.0" width="40.0" height="170.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="200.0" y="116.0" font-size="12" text-anchor="middle">17</text>
<text x="200.0" y="288.0" font-size="12" text-anchor="middle">4</text>
<rect x="236.0" y="140.0" width="40.0" height="130.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="256.0" y="156.0" font-size="12" text-anchor="middle">13</text>
<text x="256.0" y="288.0" font-size="12" text-anchor="middle">8</text>
<rect x="292.0" y="180.0" width="40.0" height="90.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="312.0" y="196.0" font-size="12" text-anchor="middle">9</text>
<text x="312.0" y="288.0" font-size="12" text-anchor="middle">12</text>
<rect x="348.0" y="220.0" width="40.0" height="50.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="368.0" y="236.0" font-size="12" text-anchor="middle">5</text>
<text x="368.0" y="288.0" font-size="12" text-anchor="middle">16</text>
<rect x="404.0" y="240.0" width="40.0" height="30.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="424.0" y="256.0" font-size="12" text-anchor="middle">3</text>
<text x="424.0" y="288.0" font-size="12" text-anchor="middle">18</text>
<rect x="460.0" y="260.0" width="40.0" height="10.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="480.0" y="254.0" font-size="12" text-anchor="middle">1</text>
<text x="480.0" y="288.0" font-size="12" text-anchor="middle">20</text>
<text x="536.0" y="264.0" font-size="12" text-anchor="middle">0</text>
<text x="536.0" y="288.0" font-size="12" text-anchor="middle">24</text>
<text x="592.0" y="264.0" font-size="12" text-anchor="middle">0</text>
<text x="592.0" y="288.0" font-size="12" text-anchor="middle">32</text>
<polyline points="88.0,70.0 144.0,80.0 200.0,100.0 256.0,140.0 312.0,180.0 368.0,220.0 424.0,240.0 480.0,260.0 536.0,270.0 592.0,270.0" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 4"/>
<text x="340.0" y="310.0" font-size="12" text-anchor="middle">큐 깊이 D (uxQueueLength)  —  20 버스트 × 60개, 소비자 150 µs/개</text>
<text x="16.0" y="160.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 160.0)">drops / burst</text>
</svg>
```
그림 5 — 예제 6의 측정 손실(막대)과 모델 max(0, 21 − D)(점선). 열 개 깊이 모두 모델과 정확히 일치한다. 깊이 21 이상이면 손실 0.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">버스트 한 번 동안의 큐 길이 (깊이 32, ISR이 100 µs마다 샘플)</text>
<rect x="60" y="44" width="168.0" height="196" fill="#e08a3c" fill-opacity="0.12" stroke="none"/>
<text x="144.0" y="60.0" font-size="12" text-anchor="middle">버스트 6 ms: 10 kHz 도착</text>
<line x1="60" y1="240" x2="620" y2="240" stroke="currentColor"/>
<line x1="60" y1="38" x2="60" y2="240" stroke="currentColor"/>
<line x1="56" y1="240.0" x2="620" y2="240.0" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="244.0" font-size="12" text-anchor="end">0</text>
<line x1="56" y1="199.2" x2="620" y2="199.2" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="203.2" font-size="12" text-anchor="end">5</text>
<line x1="56" y1="158.3" x2="620" y2="158.3" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="162.3" font-size="12" text-anchor="end">10</text>
<line x1="56" y1="117.5" x2="620" y2="117.5" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="121.5" font-size="12" text-anchor="end">15</text>
<line x1="56" y1="76.7" x2="620" y2="76.7" stroke="#888" stroke-opacity="0.25"/>
<text x="52.0" y="80.7" font-size="12" text-anchor="end">20</text>
<text x="60.0" y="258.0" font-size="12" text-anchor="middle">0</text>
<text x="116.0" y="258.0" font-size="12" text-anchor="middle">2</text>
<text x="172.0" y="258.0" font-size="12" text-anchor="middle">4</text>
<text x="228.0" y="258.0" font-size="12" text-anchor="middle">6</text>
<text x="284.0" y="258.0" font-size="12" text-anchor="middle">8</text>
<text x="340.0" y="258.0" font-size="12" text-anchor="middle">10</text>
<text x="396.0" y="258.0" font-size="12" text-anchor="middle">12</text>
<text x="452.0" y="258.0" font-size="12" text-anchor="middle">14</text>
<text x="508.0" y="258.0" font-size="12" text-anchor="middle">16</text>
<text x="564.0" y="258.0" font-size="12" text-anchor="middle">18</text>
<text x="620.0" y="258.0" font-size="12" text-anchor="middle">20</text>
<polyline points="60.0,240.0 74.0,231.8 88.0,215.5 102.0,199.2 116.0,182.8 130.0,174.7 144.0,158.3 158.0,142.0 172.0,125.7 186.0,117.5 200.0,101.2 214.0,84.8 228.0,68.5 242.0,101.2 256.0,125.7 270.0,150.2 284.0,174.7 298.0,207.3 312.0,231.8 326.0,240.0 340.0,240.0 354.0,240.0 368.0,240.0 382.0,240.0 396.0,240.0 410.0,240.0 424.0,240.0 438.0,240.0 452.0,240.0 466.0,240.0 480.0,240.0 494.0,240.0 508.0,240.0 522.0,240.0 536.0,240.0 550.0,240.0 564.0,240.0 578.0,240.0 592.0,240.0 606.0,240.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="60" y1="68.5" x2="620" y2="68.5" stroke="#d0564a" stroke-dasharray="5 4"/>
<text x="616.0" y="62.5" font-size="12" text-anchor="end">최대 21 = 필요한 깊이</text>
<text x="290.0" y="158.3" font-size="12" text-anchor="start">소비 6.7 k/s로 비움</text>
<text x="340.0" y="280.0" font-size="12" text-anchor="middle">시간 (ms) — 20 ms 주기 중 앞 6 ms만 도착</text>
</svg>
```
그림 6 — 깊이 32 실행의 두 번째 20 ms 주기, 0.5 ms 간격 샘플. 앞 6 ms의 버스트 동안 쌓이고(주황 구간), 버스트가 끝나면 소비자 속도로 비워진다. 최댓값 21이 곧 필요한 깊이다.

### 4.4 예제 13 — 모델로 깊이 계산하고 측정과 맞추기 (Python)

무엇을 확인하는 코드인가: §4.2 버스트 공식이 21을 예측하는지, 서비스 시간 S가 조금만 달라져도 필요한 깊이가 얼마나 바뀌는지, Little's law 검산, 그리고 드론 IMU 큐 깊이 계산.

```python
# D4 py — queue depth from burst math + Little's law check against ex6 measurements
import math

def burst_backlog(B, Ta, S):
    """B arrivals spaced Ta (us); consumer needs S us each and dequeues at 0, S, 2S, ...
    returns peak items waiting in the queue (the one in service is not in the queue)"""
    k = math.floor((B - 1) * Ta / S) + 1          # dequeues done by the last arrival
    return max(0, B - k)

print("ex6 scenario: B=60, Ta=100us")
for S in (150, 152, 155, 160):
    print(f"  S={S}us  peak backlog = {burst_backlog(60, 100, S)}")
rho = (60 / 20000) * 150
print(f"  utilisation rho = lambda_avg*S = {rho:.2f}  (must be < 1 or no depth is enough)")

meas = {1: 400, 2: 380, 4: 340, 8: 260, 12: 180, 16: 100, 18: 60, 20: 20, 24: 0, 32: 0}
print("depth  measured drops/burst  model max(0,21-D)")
for d, v in meas.items():
    print(f"  {d:>2}   {v/20:>5.1f}   {max(0, 21 - d):>3}")

# Little's law on the depth-32 run: L_avg=4.66 (ISR sampled), lambda*W=4.81
L, LW = 4.66, 4.81
print(f"Little: L={L}  lambda*W={LW}  diff={100*(LW-L)/LW:.1f}%  (W includes receive+switch)")

# Drone FC: IMU 8 kHz into a queue, estimator wakes at 1 kHz, worst-case response 2.5 ms
lam, R_worst = 8000, 2.5e-3
need = math.ceil(lam * R_worst)
print(f"drone IMU: lambda={lam}/s, consumer worst-case gap {R_worst*1e3} ms -> depth >= {need}"
      f" (+25% margin -> {math.ceil(need*1.25)})")
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/D4/py/depth.py
```

```text
ex6 scenario: B=60, Ta=100us
  S=150us  peak backlog = 20
  S=152us  peak backlog = 21
  S=155us  peak backlog = 21
  S=160us  peak backlog = 23
  utilisation rho = lambda_avg*S = 0.45  (must be < 1 or no depth is enough)
depth  measured drops/burst  model max(0,21-D)
   1    20.0    20
   2    19.0    19
   4    17.0    17
   8    13.0    13
  12     9.0     9
  16     5.0     5
  18     3.0     3
  20     1.0     1
  24     0.0     0
  32     0.0     0
Little: L=4.66  lambda*W=4.81  diff=3.1%  (W includes receive+switch)
drone IMU: lambda=8000/s, consumer worst-case gap 2.5 ms -> depth >= 20 (+25% margin -> 25)
```

출력에서 볼 것: 명목 서비스 시간 S = 150 µs로 계산하면 20이 나오는데 측정은 21이다. **ISR 자신이 소비자의 CPU를 훔치기 때문**이다. 100 µs마다 ISR(큐 길이 읽기 + `xQueueSendFromISR` + yield 검사)이 소비자의 바쁜 대기 안에서 돌고, 소비자는 벽시계 기준 150 µs를 재므로 메시지 하나를 처리하는 동안 실제로 하는 일은 줄고 실효 S가 152~155 µs 구간으로 늘어난다. 공식은 S가 151.3 µs를 넘는 순간 21로 바뀐다. 교훈: **서비스 시간은 생산자 ISR의 오버헤드까지 넣은 실효값**으로 잡고, 경계값 근처라면 한 칸 이상 여유를 둔다. 이 계산은 "깊이 = 21"이라는 구체적인 숫자를 리뷰에서 방어할 수 있게 해 준다.

Little's law는 3.1% 차이로 맞는다. 차이의 방향도 설명된다: L은 ISR이 **넣기 직전에** 샘플한 큐 길이라 방금 들어올 메시지를 세지 않고, W는 "넣은 시각 → receive가 **반환한** 시각"이라 receive 비용과 컨텍스트 스위치(약 11 µs, 예제 3)를 포함한다. 둘 다 λW를 L보다 크게 만든다.

### 4.5 깊이와 지연 — 깊은 큐는 공짜가 아니다

큐를 깊게 하면 손실은 줄지만 **지연이 늘어난다**. Little's law의 W = L / λ — 큐에 21개가 쌓인 순간 들어온 메시지는 21 × 150 µs ≈ 3.2 ms를 기다린다. 제어 루프에서는 이게 손실보다 나쁠 수 있다. 3 ms 늦은 자이로 샘플로 자세를 제어하느니 최신 샘플 하나를 쓰는 게 낫다. 그래서 데이터 성격에 따라 **깊이와 정책을 같이** 고른다:

- **모든 샘플이 필요한 데이터**(적분하는 IMU 델타, 로그, 명령): 최악 적체만큼 깊게 + 손실 카운터.
- **최신값만 의미 있는 데이터**(자세 추정 결과, 목표 RPM, 배터리 전압): 깊이 1 + 덮어쓰기(§5). 지연이 항상 최소다.
- **일정 시간 지난 데이터는 쓸모없음**(텔레메트리 프레임): 적당한 깊이 + 옛것 버림, 또는 타임스탬프를 보고 소비자가 오래된 것을 버린다(§9).

### 4.6 함정

- **평균 이용률로 깊이를 정한다**: ρ = 0.45인 시스템에 깊이 21이 필요했다. 깊이는 최악 적체로.
- **ISR/상위 태스크의 CPU 도둑질을 빼먹는다**: 예제 6의 20 vs 21. 실효 서비스 시간으로 계산한다.
- **과부하를 깊이로 막으려 한다**: ρ ≥ 1이면 깊이는 터지는 시점만 늦춘다. 이용률을 먼저 계산한다.
- **"손실 0"을 테스트로만 확인한다**: 최악 버스트와 최악 소비자 정지가 **동시에** 오는 경우는 테스트에서 거의 안 나온다. 공식으로 계산하고 high-water mark(`uxQueueMessagesWaiting` 최댓값)를 필드 로그에 남긴다.

---

## 5. 큐가 꽉 찼을 때 — 정책을 고른다

### 5.1 다섯 가지 정책

큐가 꽉 찼을 때 생산자가 할 수 있는 일은 다섯 가지다. 기본값은 없다 — **매 send마다 timeout 인자로 고르는 것**이다.

| 정책 | FreeRTOS 코드 | 생산자에게 | 데이터에게 | 맞는 데이터 |
|---|---|---|---|---|
| 무한 블록 | `xQueueSend(q, &v, portMAX_DELAY)` | 소비자 속도로 느려진다 (back-pressure) | 손실 없음 | 명령, 설정 변경 (잃으면 안 됨, 생산자가 태스크) |
| timeout 블록 | `xQueueSend(q, &v, pdMS_TO_TICKS(t))` | 최대 t만큼 늦는다 | 실패 시 손실, 반환값으로 앎 | 로그, 텔레메트리 (조금 기다릴 만함) |
| 새것 버림 | `xQueueSend(q, &v, 0)` 실패 → `drops++` | 안 늦는다 | 가장 최근 것을 잃는다 | ISR 생산자 전부 (블록 불가) |
| 옛것 버림 | 실패 시 `xQueueReceive(q, &old, 0)` 후 다시 send | 안 늦는다 | 가장 오래된 것을 잃는다 | 최신 데이터가 더 가치 있는 스트림 |
| 덮어쓰기 (mailbox) | 길이 1 큐 + `xQueueOverwrite(q, &v)` | 안 늦는다 | 최신값 하나만 남는다 | 상태값 (자세, 목표값, 전압) |

### 5.2 예제 7 — 같은 생산자·소비자에 정책 다섯 개

무엇을 확인하는 코드인가: 생산자(우선순위 2)가 1 ms마다 샘플 1~10을 보내고, 소비자(우선순위 1)는 하나에 3 ms를 쓴다. 깊이 3(mailbox는 1)에서 정책마다 생산자가 걸린 시간, 생산자가 아는 손실, 소비자가 실제로 받은 값을 비교한다. mailbox 소비자는 `xQueuePeek`로 읽는다.

```c
/* D4 ex7 — what to do when the queue is full: block / timeout / drop newest / drop oldest / overwrite */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
enum { BLOCK, TIMEOUT_1MS, DROP_NEWEST, DROP_OLDEST, OVERWRITE };
static const char *name[] = { "block forever", "timeout 1 ms", "drop newest", "drop oldest", "overwrite(len1)" };
static QueueHandle_t q; static volatile int policy, prod_done;
static volatile uint32_t got[16], ngot, prod_ms, lost;

static void producer(void *pv)                  /* prio 2: one sample per ms, 10 samples */
{
    (void)pv; uint32_t fails = 0, t0 = xTaskGetTickCount();
    for (uint32_t v = 1; v <= 10; v++) {
        switch (policy) {
        case BLOCK:       xQueueSend(q, &v, portMAX_DELAY); break;
        case TIMEOUT_1MS: if (xQueueSend(q, &v, pdMS_TO_TICKS(1)) != pdPASS) fails++; break;
        case DROP_NEWEST: if (xQueueSend(q, &v, 0) != pdPASS) fails++; break;
        case DROP_OLDEST: if (xQueueSend(q, &v, 0) != pdPASS) {
                              uint32_t old; xQueueReceive(q, &old, 0); fails++;
                              xQueueSend(q, &v, 0); } break;
        case OVERWRITE:   xQueueOverwrite(q, &v); break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    prod_ms = xTaskGetTickCount() - t0; lost = fails; prod_done = 1; vTaskDelete(NULL);
}

static void consumer(void *pv)                  /* prio 1: needs 3 ms per item */
{
    (void)pv; uint32_t v;
    for (;;) {
        BaseType_t ok = (policy == OVERWRITE) ? xQueuePeek(q, &v, pdMS_TO_TICKS(20))
                                              : xQueueReceive(q, &v, pdMS_TO_TICKS(20));
        if (ok != pdPASS) break;
        got[ngot++] = v;
        if (policy == OVERWRITE && prod_done) break;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    vTaskDelete(NULL);
}

static void ctl(void *pv)
{
    (void)pv;
    for (policy = BLOCK; policy <= OVERWRITE; policy++) {
        q = xQueueCreate(policy == OVERWRITE ? 1 : 3, sizeof(uint32_t)); prod_done = 0; ngot = 0;
        xTaskCreate(consumer, "cons", 256, NULL, 1, NULL);
        xTaskCreate(producer, "prod", 256, NULL, 2, NULL);
        vTaskDelay(pdMS_TO_TICKS(80));
        vQueueDelete(q);
        board_printf_raw("%s: producer %u ms, lost %u, consumer got:", name[policy],
                         (unsigned)prod_ms, (unsigned)lost);
        for (unsigned i = 0; i < ngot; i++) board_printf_raw(" %u", (unsigned)got[i]);
        board_printf_raw("\n");
    }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(ctl, "ctl", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex7/ex7.c OUT=/tmp/rtos/D4/ex7 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex7/app.elf 30'
```

```text
block forever: producer 19 ms, lost 0, consumer got: 1 2 3 4 5 6 7 8 9 10
timeout 1 ms: producer 13 ms, lost 3, consumer got: 1 2 3 4 5 7 9
drop newest: producer 10 ms, lost 4, consumer got: 1 2 3 4 5 8
drop oldest: producer 10 ms, lost 4, consumer got: 1 2 5 8 9 10
overwrite(len1): producer 10 ms, lost 0, consumer got: 1 4 7 10 10
[board_exit] code=0
```

출력에서 볼 것:

- **무한 블록**: 손실 0, 대신 생산자가 10 ms 일을 19 ms에 끝냈다. 소비자 속도로 끌려간 것이다(back-pressure). 생산자가 센서 샘플링 태스크였다면 샘플 주기가 깨졌다.
- **timeout 1 ms**: 생산자는 13 ms로 덜 늦고 3개를 잃는다. 반환값으로 손실을 **안다**.
- **새것 버림**: 생산자는 정시(10 ms), 손실 4개. 받은 값은 앞쪽(1~5)에 몰려 있고 최신 9·10을 잃었다.
- **옛것 버림**: 같은 손실 4개인데 받은 값이 **최신 쪽**(8·9·10)을 포함한다. 같은 손실 수라도 어떤 샘플을 잃느냐가 정책으로 정해진다. 주의: "receive 후 send"는 두 동작이라 **원자적이지 않다** — 사이에 다른 생산자가 끼어들 수 있다(생산자가 하나일 때만 안전).
- **덮어쓰기**: 생산자는 손실을 "모른다"(`lost 0`). 실제로는 1·4·7·10만 읽혔고 나머지 6개는 조용히 덮였다. 그리고 마지막에 **10을 두 번** 읽었다 — peek는 지우지 않으니 같은 값을 다시 읽을 수 있다. mailbox를 쓰는 쪽은 "새 값인가?"를 알려면 시퀀스 번호를 같이 넣어야 한다(§9).

### 5.3 정책 고르기 — 결정 순서

1. 생산자가 ISR인가? → 블록 불가. 새것 버림 / 옛것 버림(ISR에서 하려면 `FromISR` 두 번) / 덮어쓰기 중에서 고르고 **반드시 손실을 센다**.
2. 최신값만 의미 있나? → 길이 1 mailbox + `xQueueOverwrite`, 읽는 쪽은 `xQueuePeek` + 시퀀스 번호.
3. 잃으면 안 되는가(명령·설정)? → 블록. 단 **무한 블록은 데드락 후보**다(소비자가 죽으면 생산자도 영원히 멈춤). 실무 기본은 "충분히 긴 timeout + 실패 시 에러 보고"다.
4. 나머지(로그·텔레메트리): 짧은 timeout 또는 새것 버림 + 카운터. 카운터는 텔레메트리로 내보내 필드에서 보이게 한다.

### 5.4 함정

- **반환값을 무시한다**: `xQueueSend(q, &v, 0);`만 쓰고 결과를 안 보면 손실이 조용히 일어난다. 손실 카운터는 공짜에 가깝고 디버깅 시간을 몇 주 줄인다.
- **ISR에서 옛것 버림을 직접 짠다**: `xQueueReceiveFromISR` + `xQueueSendFromISR`는 둘 다 짧지만, 사이에 더 높은 우선순위 ISR이 같은 큐를 만지면 순서가 꼬인다. ISR 생산자가 여럿이면 새것 버림이 안전하다.
- **`xQueueOverwrite`를 길이 2 이상 큐에 쓴다**: `configASSERT(!((xCopyPosition == queueOVERWRITE) && (pxQueue->uxLength != 1)))`가 잡는다. assert가 꺼진 릴리스 빌드에서는 개수가 틀어진다.

---

## 6. 블로킹 send/receive, timeout, 앞에 넣기, 엿보기

### 6.1 timeout 인자의 뜻

`xQueueSend()`·`xQueueReceive()`의 마지막 인자 `xTicksToWait`는 "조건이 안 되면 **최대 몇 tick까지 잘 것인가**"다.

- `0`: 절대 블록하지 않는다(폴링). 바로 `errQUEUE_FULL` / `errQUEUE_EMPTY`.
- `pdMS_TO_TICKS(ms)`: ms를 tick으로 바꾼다. 1 kHz tick에서는 같은 숫자지만 100 Hz면 10 ms 단위로 내림된다(`pdMS_TO_TICKS(5)`가 0이 되어 폴링으로 바뀌는 함정 — C5).
- `portMAX_DELAY`: `INCLUDE_vTaskSuspend == 1`이면 **무한 대기**(태스크가 delayed 리스트가 아닌 suspended 리스트에 걸린다 — C1 표). 0이면 그냥 최대 tick 수다.
- timeout은 "블록을 시작한 순간부터의 **총** 시간"이다. FreeRTOS는 `vTaskInternalSetTimeOutState()`로 시작 시각을 적고, 깨어났다가 다시 블록해도(§6.3의 재시도) `xTaskCheckForTimeOut()`이 남은 시간만 다시 기다린다. 그래서 재시도가 있어도 총 대기는 약속한 시간을 넘지 않는다.

깨어나는 순서는 **우선순위 순, 같은 우선순위면 먼저 온 순**이다. 대기 리스트가 `configMAX_PRIORITIES − 우선순위` 값으로 정렬 삽입되기 때문이고(C1 §2.5 설명, §2.6에서 덤프로 확인), 커널은 맨 앞 하나만 깨운다(O(1)). Zephyr의 `wait_q`도 우선순위 순이다.

### 6.2 앞에 넣기, 엿보기, 개수 읽기

- **`xQueueSendToFront()`**: 긴급 메시지(예: "모터 정지")를 줄 맨 앞에 끼운다. 예제 2에서 본 대로 O(1)이다. 단 큐가 **꽉 차 있으면 앞에 넣기도 실패/블록**한다 — 긴급 메시지가 꽉 찬 큐 뒤에서 기다리게 된다. 정말 긴급한 경로는 별도 큐(또는 task notification, D5)로 뺀다. 그리고 앞에 넣기를 여러 번 하면 그것들끼리는 **LIFO**다.
- **`xQueuePeek()`**: 꺼내지 않고 복사만. 여러 소비자가 같은 mailbox를 읽을 때, 또는 "다음 메시지 타입을 보고 처리할지 결정"할 때 쓴다. 대기 중이던 peek 태스크도 데이터가 오면 깨어난다(소스: peek 성공 시 "The task waiting for data to arrive on the queue could be another peeker" 경로로 `xTasksWaitingToReceive`의 다음 대기자를 깨운다).
- **`uxQueueMessagesWaiting()`·`uxQueueSpacesAvailable()`**: 크리티컬 섹션 안에서 읽은 **그 순간의 스냅샷**이다. "개수가 0보다 크면 receive(…, 0)" 같은 코드는 사이에 다른 소비자가 가져가면 실패한다. 개수는 모니터링·high-water mark용이고, 흐름 제어는 timeout 있는 receive 하나로 한다.
- **`xQueueReset()`**: 큐를 비운다. 기다리던 **sender** 하나를 깨워 준다(빈 칸이 생겼으니까). 기다리던 receiver는 그대로 기다린다.

### 6.3 예제 11 — 깨어난 sender는 다시 경쟁한다 (FreeRTOS)

블로킹 send에는 잘 알려지지 않은 성질이 하나 있다. FreeRTOS에서 꽉 찬 큐를 기다리던 sender가 깨어나는 것은 "**빈 칸이 생겼으니 다시 시도해 봐**"라는 뜻이지 "네 데이터를 넣어 줬다"가 아니다. 깨어난 sender가 실제로 CPU를 받기 전에 더 높은 우선순위의 다른 태스크가 그 칸을 차지할 수 있다.

실험: 길이 1 큐가 처음부터 'I'로 차 있다. S(우선순위 1)가 'S'를 보내려고 **먼저** 블록한다. R(우선순위 3)이 5 ms 뒤 하나를 꺼내고 곧바로 M(우선순위 2)을 세마포어로 깨운다. 그 다음 R이 잠들면, ready인 태스크는 S(1)와 M(2)이다. trace 매크로 `traceBLOCKING_ON_QUEUE_SEND`에 카운터를 걸어 S가 몇 번 블록했는지 센다.

무엇을 확인하는 코드인가: 먼저 기다린 S와 나중에 온 M 중 누구의 메시지가 먼저 큐에 들어가는지.

```c
/* D4 ex11 — FreeRTOS: a woken sender must RETRY, so a third task can take the free slot */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"
volatile unsigned d4_blk;                         /* bumped by traceBLOCKING_ON_QUEUE_SEND */
static QueueHandle_t q; static SemaphoreHandle_t go;

static void S(void *pv)                           /* prio 1: was waiting FIRST */
{
    (void)pv; char c = 'S';
    xQueueSend(q, &c, portMAX_DELAY);
    board_printf_raw("  S finally sent (blocked %u times in total)\n", d4_blk);
    vTaskDelete(NULL);
}

static void M(void *pv)                           /* prio 2: arrives later */
{
    (void)pv; char c = 'M';
    xSemaphoreTake(go, portMAX_DELAY);
    xQueueSend(q, &c, portMAX_DELAY);
    board_printf_raw("  M sent without blocking\n");
    vTaskDelete(NULL);
}

static void R(void *pv)                           /* prio 3: drains one item every 5 ms */
{
    (void)pv; char c;
    for (int i = 0; i < 3; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        xQueueReceive(q, &c, 0);
        board_printf_raw("R got %c\n", c);
        if (i == 0) xSemaphoreGive(go);           /* wake M right after freeing the slot */
    }
    board_exit(0);
}

int main(void)
{
    char c = 'I';
    q = xQueueCreate(1, 1); xQueueSend(q, &c, 0);   /* full from the start */
    go = xSemaphoreCreateBinary();
    xTaskCreate(S, "S", 256, NULL, 1, NULL);
    xTaskCreate(M, "M", 256, NULL, 2, NULL);
    xTaskCreate(R, "R", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex11/ex11.c OUT=/tmp/rtos/D4/ex11 EXTRA_CFLAGS="-D\"traceBLOCKING_ON_QUEUE_SEND(x)=do{extern volatile unsigned d4_blk; d4_blk++;}while(0)\"" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex11/app.elf 10'
```

```text
R got I
  M sent without blocking
R got M
  S finally sent (blocked 2 times in total)
R got S
[board_exit] code=0
```

출력에서 볼 것: S가 먼저 기다렸는데 R이 받은 순서는 **I, M, S**다. R의 receive가 S를 대기 리스트에서 꺼내 ready로 옮겼지만, S의 데이터는 아직 S의 스택에 있다. R이 잠든 뒤 우선순위가 더 높은 M이 먼저 돌아 빈 칸에 'M'을 넣었고, S는 나중에 돌아 `xQueueGenericSend()`의 `for (;;)` 루프를 다시 돌다가 또 꽉 찬 것을 보고 **두 번째로 블록**했다(`blocked 2 times`). 우선순위 스케줄링에서는 이게 "맞는" 동작이다(높은 우선순위가 이긴다). 하지만 "먼저 기다린 쪽이 먼저 넣는다"는 FIFO 공정성은 **보장되지 않는다**. 같은 실험을 Zephyr에서 하면 결과가 다르다 — §10.5에서 소스와 함께 본다.

---

## 7. ISR에서 큐 쓰기 — `FromISR` 규칙과 yield 플래그

> ISR 규칙 전체(금지 API 목록, `configMAX_SYSCALL_INTERRUPT_PRIORITY`, Zephyr `k_is_in_isr()`)는 E1에서 다룬다. 여기서는 **큐에 한정된** 세 규칙과 그 측정만 본다.

### 7.1 세 규칙

1. **ISR은 절대 블록하지 않는다.** ISR은 태스크가 아니라서 재울 TCB가 없다. 그래서 FreeRTOS는 timeout 인자 자체가 없는 `xQueueSendFromISR()`·`xQueueReceiveFromISR()`·`xQueueOverwriteFromISR()`·`xQueuePeekFromISR()`·`uxQueueMessagesWaitingFromISR()`를 따로 둔다. Zephyr는 같은 `k_msgq_put()`에 반드시 `K_NO_WAIT`를 준다(`put_msg_in_queue()` 첫 줄의 `__ASSERT(!arch_is_in_isr() || K_TIMEOUT_EQ(timeout, K_NO_WAIT), "")`).
2. **ISR 안에서 태스크용 API를 부르지 않는다.** `xQueueSend()`는 `taskENTER_CRITICAL()`을 쓰는데, ARM_CM3 포트의 `vPortEnterCritical()`이 `configASSERT((portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK) == 0)`으로 "지금 예외 처리 중이면 안 된다"를 검사한다. 아래 예제에서 실제로 걸린다.
3. **깨운 태스크가 더 높으면 ISR 끝에서 스위치를 요청한다.** `FromISR` 함수는 대기자를 깨우되 스위치는 하지 않고, 깨운 태스크가 현재 태스크보다 높으면 `*pxHigherPriorityTaskWoken = pdTRUE`로 알려만 준다. ISR 마지막에 `portYIELD_FROM_ISR(woken)`을 부르면 PendSV가 걸려 ISR이 끝나자마자 그 태스크로 간다(B4). 안 부르면? 아래 측정이 답이다.

### 7.2 예제 8 — yield를 빼먹으면 얼마나 늦나 + 잘못된 API의 assert

무엇을 확인하는 코드인가: TIMER0 ISR(1.37 ms 주기 — 1 ms tick과 위상이 계속 어긋나도록)이 타임스탬프를 큐에 넣고, 우선순위 3 소비자가 받는 데 걸린 시간을 300번 잰다. 우선순위 1의 무한 루프 태스크(`hog`)가 항상 CPU를 쥐고 있다. 세 빌드: (1) woken 무시, (2) `portYIELD_FROM_ISR(woken)`, (3) ISR에서 `xQueueSend()`(잘못된 API).

```c
/* D4 ex8 — ISR -> queue -> task latency with and without portYIELD_FROM_ISR (+ misuse demo) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define CYC() REG32(0x40028018u)                  /* 25 MHz: 1 = 40 ns */
#define NS 300
static QueueHandle_t q;
static volatile uint32_t n, lmin = ~0u, lmax, lsum;

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    uint32_t t = CYC();
#if defined(MISUSE)
    xQueueSend(q, &t, 0);                         /* WRONG: task API inside an ISR */
#else
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(q, &t, &woken);
#if defined(YIELD)
    portYIELD_FROM_ISR(woken);                    /* switch as soon as the ISR returns */
#endif
#endif
}

static void consumer(void *pv)                    /* prio 3 */
{
    (void)pv; uint32_t t;
    while (n < NS) {
        xQueueReceive(q, &t, portMAX_DELAY);
        uint32_t d = CYC() - t;
        if (d < lmin) lmin = d;
        if (d > lmax) lmax = d;
        lsum += d; n++;
    }
    TIMER_CTRL(TIMER0_BASE) = 0;
    board_printf_raw("%s: n=%u min=%u avg=%u max=%u cycles (1=40ns)\n",
#if defined(YIELD)
        "with portYIELD_FROM_ISR",
#else
        "woken ignored          ",
#endif
        (unsigned)n, (unsigned)lmin, (unsigned)(lsum / n), (unsigned)lmax);
    board_exit(0);
}

static void hog(void *pv) { (void)pv; for (;;) { } }  /* prio 1: always runnable */

int main(void)
{
    q = xQueueCreate(4, sizeof(uint32_t));
    TIMER_RELOAD(TIMER0_BASE) = 34250 - 1;        /* 1.37 ms: drifts against the 1 ms tick */
    TIMER_CTRL(TIMER0_BASE) = (1u << 0) | (1u << 3);
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    xTaskCreate(consumer, "cons", 256, NULL, 3, NULL);
    xTaskCreate(hog, "hog", 128, NULL, 1, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D4/ex8/ex8.c; for v in NONE YIELD MISUSE; do make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S OUT=/tmp/rtos/D4/ex8_$v EXTRA_CFLAGS="-D$v" >/dev/null; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex8_$v/app.elf 30; echo "exit=$?"; done'
```

```text
woken ignored          : n=300 min=372 avg=12747 max=25122 cycles (1=40ns)
[board_exit] code=0
exit=0
with portYIELD_FROM_ISR: n=300 min=283 avg=283 max=320 cycles (1=40ns)
[board_exit] code=0
exit=0

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253
exit=1
```

출력에서 볼 것: yield를 하면 ISR → 태스크 지연이 **283 사이클(11.3 µs), 최대 320(12.8 µs)** 으로 거의 일정하다. yield를 빼먹으면 평균 12747 사이클(**510 µs**), 최대 25122(**1.005 ms**) — 소비자는 다음 SysTick이 올 때까지 기다린다. `xTaskRemoveFromEventList()`가 "더 높은 태스크가 깨어났다"를 `xYieldPendings`에 적어 두고, 다음 tick ISR(`xTaskIncrementTick()`)이 그걸 보고 스위치를 요청하기 때문이다. 그래서 최대가 정확히 tick 주기(1 ms)다. 평균이 절반(0.5 ms)인 것은 1.37 ms 주기가 tick과 위상이 고르게 어긋나서 균등 분포가 된 결과다. 그리고 min 372는 ISR이 tick 직전에 온 경우다. 셋째 빌드는 `port.c:438`의 `configASSERT`에서 멈췄다 — 잘못된 API를 개발 단계에서 바로 잡아 주는 것이 `configASSERT`를 켜 두는 이유다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<text x="330.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">ISR → 큐 → 태스크 지연 (300회, 로그 축, µs)</text>
<text x="192.0" y="59.0" font-size="12" text-anchor="end">yield 안 함 · min</text>
<rect x="200" y="44" width="71.0" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="277.0" y="59.0" font-size="12" text-anchor="start">14.9</text>
<text x="192.0" y="93.0" font-size="12" text-anchor="end">yield 안 함 · avg</text>
<rect x="200" y="78" width="301.0" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="507.0" y="93.0" font-size="12" text-anchor="start">509.9</text>
<text x="192.0" y="127.0" font-size="12" text-anchor="end">yield 안 함 · max</text>
<rect x="200" y="112" width="345.2" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="551.2" y="127.0" font-size="12" text-anchor="start">1004.9</text>
<text x="192.0" y="161.0" font-size="12" text-anchor="end">portYIELD_FROM_ISR · min</text>
<rect x="200" y="146" width="53.2" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="259.2" y="161.0" font-size="12" text-anchor="start">11.3</text>
<text x="192.0" y="195.0" font-size="12" text-anchor="end">portYIELD_FROM_ISR · avg</text>
<rect x="200" y="180" width="53.2" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="259.2" y="195.0" font-size="12" text-anchor="start">11.3</text>
<text x="192.0" y="229.0" font-size="12" text-anchor="end">portYIELD_FROM_ISR · max</text>
<rect x="200" y="214" width="61.2" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="267.2" y="229.0" font-size="12" text-anchor="start">12.8</text>
<line x1="200" y1="252" x2="590" y2="252" stroke="currentColor"/>
<line x1="245.1" y1="40" x2="245.1" y2="252" stroke="#888" stroke-opacity="0.3"/>
<text x="245.1" y="268.0" font-size="12" text-anchor="middle">10 µs</text>
<line x1="395.0" y1="40" x2="395.0" y2="252" stroke="#888" stroke-opacity="0.3"/>
<text x="395.0" y="268.0" font-size="12" text-anchor="middle">100 µs</text>
<line x1="544.9" y1="40" x2="544.9" y2="252" stroke="#888" stroke-opacity="0.3"/>
<text x="544.9" y="268.0" font-size="12" text-anchor="middle">1000 µs</text>
<text x="395.0" y="288.0" font-size="12" text-anchor="middle">1 ms tick: yield를 빼먹으면 다음 tick까지 최대 1 ms를 기다린다</text>
</svg>
```
그림 7 — 예제 8의 ISR → 큐 → 태스크 지연(로그 축). `portYIELD_FROM_ISR`를 빼먹으면 지연이 tick 주기(1 ms)까지 늘어나고 분포가 넓게 퍼진다. 드론 FC의 1~8 kHz IMU 경로에서는 치명적이다.

### 7.3 Zephyr에서는

Zephyr에는 `FromISR` 짝이 없다. 같은 `k_msgq_put(&q, &v, K_NO_WAIT)`를 ISR에서 부르고, yield 플래그도 없다. 커널이 ISR 출구에서 알아서 스위치를 결정한다(`z_reschedule()`은 ISR 안에서는 스위치하지 않고, Cortex-M 포트는 ISR이 끝날 때 PendSV로 다시 스케줄한다 — C8). 그래서 "yield를 빼먹는" 버그는 Zephyr에는 없다. 대신 ISR에서 `K_FOREVER`를 주는 실수는 `CONFIG_ASSERT=y`일 때만 잡히니 개발 빌드에서는 assert를 켠다.

---

## 8. 여러 큐를 한꺼번에 기다리기 — 큐 셋

### 8.1 직관

태스크 하나가 IMU 큐, 명령 큐, 주기 세마포어 셋 중 **아무거나 먼저 오는 것**을 처리하고 싶다. 하나에서 블록하면 나머지를 못 본다. timeout 0으로 돌아가며 폴링하면 CPU를 태운다. FreeRTOS의 답이 **큐 셋(queue set)** 이고, Zephyr의 답이 `k_poll()`이다(D5에서 자세히).

큐 셋은 그 자체가 **"데이터가 생긴 멤버의 핸들"을 담는 큐**다. 멤버 큐에 item이 들어갈 때마다 커널이 그 멤버의 핸들(4 B 포인터)을 셋 큐에 하나 넣는다. 태스크는 셋 큐 하나에서 블록하다가(`xQueueSelectFromSet()`), 핸들을 받으면 **그 멤버에서 timeout 0으로** 실제 데이터를 꺼낸다. 소스를 보면 `xQueueSelectFromSet()`은 정말로 `xQueueReceive(xQueueSet, &xReturn, xTicksToWait)` 한 줄이다. 켜려면 `configUSE_QUEUE_SETS 1`(랩 템플릿은 이미 1).

```svg
<svg viewBox="0 0 660 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330" y="20" font-size="14" text-anchor="middle" font-weight="bold">큐 셋 = 멤버 핸들을 담는 또 하나의 큐</text>
<rect x="20" y="56" width="150" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="95" y="79" font-size="12" text-anchor="middle">q_imu (len 4)</text>
<rect x="20" y="108" width="150" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="95" y="131" font-size="12" text-anchor="middle">q_cmd (len 2)</text>
<rect x="20" y="160" width="150" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="95" y="183" font-size="12" text-anchor="middle">s_tick (binary, 1)</text>
<line x1="172" y1="74" x2="218" y2="118" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="172" y1="126" x2="218" y2="126" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="172" y1="178" x2="218" y2="134" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="220" y="56" width="230" height="140" rx="6" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="335" y="78" font-size="12" text-anchor="middle">set (len 7 = 4 + 2 + 1)</text>
<rect x="230" y="104" width="30" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="245" y="124" font-size="12" text-anchor="middle">imu</text>
<rect x="260" y="104" width="30" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="275" y="124" font-size="12" text-anchor="middle">imu</text>
<rect x="290" y="104" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="305" y="124" font-size="12" text-anchor="middle">cmd</text>
<rect x="320" y="104" width="30" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="335" y="124" font-size="12" text-anchor="middle">imu</text>
<rect x="350" y="104" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="365" y="124" font-size="12" text-anchor="middle">tik</text>
<rect x="380" y="104" width="30" height="30" fill="#888" fill-opacity="0.05" stroke="#888"/>
<rect x="410" y="104" width="30" height="30" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="335" y="168" font-size="12" text-anchor="middle">항목 = 멤버 핸들 (4 B)</text>
<line x1="452" y1="126" x2="488" y2="126" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="490" y="72" width="150" height="108" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="96" font-size="13" text-anchor="middle">reader 태스크</text>
<text x="565" y="120" font-size="12" text-anchor="middle">① select → 핸들</text>
<text x="565" y="142" font-size="12" text-anchor="middle">② 그 멤버를</text>
<text x="565" y="162" font-size="12" text-anchor="middle">timeout 0으로 읽기</text>
<text x="330" y="232" font-size="12" text-anchor="middle">멤버에 쓰면 prvNotifyQueueSetContainer()가 set에 그 멤버 핸들을 하나 넣는다</text>
<text x="330" y="256" font-size="12" text-anchor="middle">set이 꽉 차면 configASSERT · 멤버를 직접 읽으면 set에 남은 핸들이 거짓말을 한다</text>
</svg>
```
그림 8 — 큐 셋의 구조. 셋 큐에는 데이터가 아니라 "데이터가 들어온 멤버의 핸들"이 이벤트 개수만큼 쌓인다. 그래서 셋 길이는 멤버 길이의 합이어야 하고, 멤버 데이터는 핸들을 받은 뒤에만 꺼내야 개수가 맞는다.

### 8.2 예제 9 — 올바른 사용과 네 가지 함정

무엇을 확인하는 코드인가: `MODE`로 다섯 빌드를 만든다. 0 = 올바른 사용, 1 = 셋 길이 부족, 2 = 비어 있지 않은 큐를 셋에 추가, 3 = 멤버 큐에서 직접 블록, 4 = select 없이 멤버를 읽음.

```c
/* D4 ex9 — queue sets: correct use + four gotchas (MODE=0..4) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"
#ifndef MODE
#define MODE 0
#endif
static QueueHandle_t q_imu, q_cmd; static SemaphoreHandle_t s_tick; static QueueSetHandle_t set;

static void waiter(void *pv)                      /* MODE 3: blocks on a MEMBER directly */
{
    (void)pv; uint32_t v; TickType_t t0 = xTaskGetTickCount();
    BaseType_t ok = xQueueReceive(q_imu, &v, pdMS_TO_TICKS(50));
    board_printf_raw("direct receive on member: ok=%d v=%u after %u ms (sender posted at 0 ms)\n",
                     (int)ok, (unsigned)v, (unsigned)(xTaskGetTickCount() - t0));
    board_exit(0);
}

static void sender(void *pv)                      /* prio 1: posts a mix of events */
{
    (void)pv;
    for (uint32_t i = 1; i <= 3; i++) {
        xQueueSend(q_imu, &i, 0);
        if (i == 2) { uint32_t c = 0xC0DE; xQueueSend(q_cmd, &c, 0); }
    }
    xSemaphoreGive(s_tick);
#if MODE == 4
    uint32_t v; xQueueReceive(q_imu, &v, 0);       /* WRONG: read a member without select */
#endif
    vTaskDelete(NULL);
}

static void reader(void *pv)                      /* prio 2 */
{
    (void)pv; uint32_t v;
    vTaskDelay(1);                                /* let sender post everything first */
    for (;;) {
        QueueSetMemberHandle_t m = xQueueSelectFromSet(set, pdMS_TO_TICKS(10));
        if (m == NULL) break;
        if (m == q_imu)       board_printf_raw("imu %s\n", xQueueReceive(q_imu, &v, 0) ? "ok" : "EMPTY!");
        else if (m == q_cmd)  { xQueueReceive(q_cmd, &v, 0); board_printf_raw("cmd 0x%x\n", (unsigned)v); }
        else if (m == s_tick) { xSemaphoreTake(s_tick, 0); board_printf_raw("tick sem\n"); }
    }
    board_printf_raw("set drained\n"); board_exit(0);
}

int main(void)
{
    q_imu = xQueueCreate(4, sizeof(uint32_t)); q_cmd = xQueueCreate(2, sizeof(uint32_t));
    s_tick = xSemaphoreCreateBinary();
    set = xQueueCreateSet(MODE == 1 ? 2 : 4 + 2 + 1);   /* must be >= sum of member lengths */
#if MODE == 2
    uint32_t z = 9; xQueueSend(q_imu, &z, 0);           /* member not empty when added */
#endif
    board_printf_raw("MODE %d add: imu=%d cmd=%d sem=%d\n", MODE, (int)xQueueAddToSet(q_imu, set),
                     (int)xQueueAddToSet(q_cmd, set), (int)xQueueAddToSet(s_tick, set));
    xTaskCreate(sender, "send", 256, NULL, 1, NULL);
    xTaskCreate(MODE == 3 ? waiter : reader, "rx", 256, NULL, MODE == 3 ? 3 : 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D4/ex9/ex9.c; for m in 0 1 2 3 4; do make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S OUT=/tmp/rtos/D4/ex9_$m EXTRA_CFLAGS="-DMODE=$m" >/dev/null; echo "--- MODE=$m"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex9_$m/app.elf 10; done'
```

```text
--- MODE=0
MODE 0 add: imu=1 cmd=1 sem=1
imu ok
imu ok
cmd 0xc0de
imu ok
tick sem
set drained
[board_exit] code=0
--- MODE=1
MODE 1 add: imu=1 cmd=1 sem=1

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/queue.c:3355
[board_exit] code=253
--- MODE=2
MODE 2 add: imu=0 cmd=1 sem=1
cmd 0xc0de
tick sem
set drained
[board_exit] code=0
--- MODE=3
MODE 3 add: imu=1 cmd=1 sem=1
direct receive on member: ok=1 v=1 after 50 ms (sender posted at 0 ms)
[board_exit] code=0
--- MODE=4
MODE 4 add: imu=1 cmd=1 sem=1
imu ok
imu ok
cmd 0xc0de
imu EMPTY!
tick sem
set drained
[board_exit] code=0
```

출력에서 볼 것:

- **MODE 0**: 셋 큐가 이벤트를 **도착 순서대로** 돌려준다(imu, imu, cmd, imu, tick). 멤버별 우선순위는 없다 — 명령이 IMU보다 급해도 순서대로다. 급한 것은 별도 태스크/큐로 뺀다.
- **MODE 1 — 셋 길이 부족**: 이벤트 3개째에서 `queue.c:3355`의 `configASSERT(pxQueueSetContainer->uxMessagesWaiting < pxQueueSetContainer->uxLength)`. 헤더 주석(`queue.h`의 `xQueueCreateSet` 설명)이 정답을 준다: "uxEventQueueLength should be set to the total sum of the length of the queues added to the set" — 이진 세마포어·뮤텍스는 1, 카운팅 세마포어는 최대 카운트. assert가 꺼진 빌드라면 `if (uxMessagesWaiting < uxLength)` 분기에서 **이벤트를 조용히 버린다** — 멤버에 데이터는 있는데 select가 영영 안 알려 주는 버그가 된다.
- **MODE 2 — 비어 있지 않은 큐 추가**: `xQueueAddToSet(q_imu)`가 0(`pdFAIL`)을 돌려주고, 그 뒤의 IMU 데이터는 셋에 한 번도 나타나지 않는다. `xQueueAddToSet()`은 (a) 대상이 진짜 셋인지(item 크기 == `sizeof(Queue_t *)` — 이 버전 소스에 있는 타입 혼동 방지 검사), (b) 이미 다른 셋 멤버인지, (c) 멤버가 비어 있는지 셋을 검사한다. **셋 구성은 스케줄러 시작 전, 모든 큐가 빈 상태에서** 한다. 반환값을 꼭 확인한다.
- **MODE 3 — 멤버에서 직접 블록**: 데이터는 0 ms에 들어왔는데 직접 기다리던 태스크는 **50 ms(timeout 끝)에야** 깨어났다. 멤버가 셋에 속하면 `xQueueGenericSend()`는 `prvNotifyQueueSetContainer()`만 부르고 그 멤버 자신의 `xTasksWaitingToReceive`는 **보지 않는다**(소스의 `if (pxQueue->pxQueueSetContainer != NULL) { … } else { … xTaskRemoveFromEventList(&pxQueue->xTasksWaitingToReceive) … }` 구조). 태스크는 timeout으로 깨어난 뒤 `xQueueReceive()`의 "Timed out. If there is no data in the queue exit, otherwise loop back" 경로에서 데이터를 발견해 성공을 돌려줬다. 즉 성공은 하는데 **timeout만큼 늦는다** — 무한 대기였다면 영원히 잠든다.
- **MODE 4 — select 없이 멤버 읽기**: sender가 셋을 거치지 않고 IMU 하나를 몰래 꺼냈다. 셋에는 IMU 핸들이 3개 남아 있는데 실제 IMU 데이터는 2개 — 세 번째 select가 `q_imu`를 돌려줬지만 `EMPTY!`. 헤더의 Note 4가 이 규칙이다: "A receive … must not be performed on a member of a queue set unless a call to xQueueSelectFromSet() has first returned a handle to that set member."

### 8.3 큐 셋은 언제 쓰나 — 대개 안 쓴다

FreeRTOS 헤더 주석 Note 1이 직접 "queue sets are very rarely needed in practice as there are simpler methods of blocking on multiple objects"라고 쓴다. 이유는 위의 함정 넷 + RAM(멤버 칸마다 셋에 4 B, Note 3) + 순서 보장 없음 + 셋에 뮤텍스를 넣으면 우선순위 상속이 안 됨(Note 2). 대안은 셋이다.

1. **이벤트 큐 하나로 합치기**: 생산자들이 모두 같은 큐에 tagged union 이벤트를 넣는다(§9). 가장 흔하고 가장 단순하다. 순서가 도착 순서로 보존된다.
2. **task notification 비트로 "어디에 일이 있나" 알리기**: 각 생산자가 자기 큐에 넣고 소비자에게 `xTaskNotify(h, BIT_IMU, eSetBits)`. 소비자는 `xTaskNotifyWait()`로 비트를 받고 해당 큐들을 timeout 0으로 비운다(D5).
3. **Zephyr `k_poll()`**: 여러 객체(세마포어, `k_msgq`, `k_fifo`, 시그널)의 이벤트 배열에서 블록한다. `K_POLL_TYPE_MSGQ_DATA_AVAILABLE`이 `k_msgq` 용이다. `k_poll`은 "준비됐다"만 알려 주고 데이터는 따로 `k_msgq_get(…, K_NO_WAIT)`으로 가져온다는 점이 큐 셋과 같다(`kernel/msg_q.c`의 put 경로가 `handle_poll_events()` → `z_handle_obj_poll_events(…, K_POLL_STATE_MSGQ_DATA_AVAILABLE)`를 부른다). 자세한 내용은 D5.

---

## 9. 메시지 설계 — tagged union, 명령/응답, 시퀀스 번호, 이벤트 큐 + 상태 기계

### 9.1 tagged union 이벤트

이벤트 큐 하나에 여러 종류를 실으려면 **태그(type) + union**으로 만든다. 태그가 "union의 어느 멤버가 유효한지"를 말한다. 설계 규칙:

- **고정 헤더**: `type`, `seq`(시퀀스 번호), `t_us`(생성 시각)를 맨 앞에 둔다. 디버거·로그·트레이스가 type과 무관하게 같은 위치를 읽을 수 있다.
- **크기는 가장 큰 멤버가 정한다**: 큐 RAM = 길이 × `sizeof(evt_t)`. 한 종류만 크면(예: 64 B 로그 문자열) 그 종류는 포인터 + 풀로 빼서 union을 작게 유지한다.
- **패딩을 의식한다**: 아래 예제의 `evt_t`는 필드 합이 1 + 2 + 4 + 8 = 15 B인데 `sizeof`가 16이다. 큐는 패딩까지 복사한다. 필드 순서를 큰 것부터 두면 패딩이 줄어든다. 큐를 통해 다른 코어/프로세서로 보낸다면(rpmsg 등 P5) 패딩·엔디안·정렬을 명시적으로 고정한다(`_Static_assert(sizeof(evt_t) == 16, "")`).
- **포인터를 담으면 소유권 규칙(§3)이 같이 따라온다**: 아래 예제의 `reply` 큐 핸들처럼 "수명이 긴 객체의 핸들"은 괜찮지만, 스택 버퍼 주소는 안 된다.

### 9.2 명령/응답 — 응답 큐, 시퀀스 번호, timeout

태스크에게 "일을 시키고 결과를 받는" 패턴(RPC)은 큐 두 개로 만든다. 요청은 서버 태스크의 이벤트 큐로, **응답은 요청에 실어 보낸 클라이언트 자신의 응답 큐로**. 서버는 누가 보냈는지 몰라도 `e.u.cmd.reply`로 답하면 된다.

여기서 반드시 생기는 문제: 클라이언트가 timeout으로 포기한 뒤 **늦은 응답**이 도착한다. 클라이언트가 다음 요청을 보내고 응답을 기다리면 이전 요청의 늦은 응답을 받게 된다 — "응답이 엉뚱한 요청에 붙는다". 해법이 **시퀀스 번호**다. 요청마다 `seq`를 올리고, 응답에 그대로 돌려받아 비교해서 다르면 버린다. Don에게 익숙한 그림으로는 NVMe의 Command Identifier(CID)다 — 완료 엔트리의 CID로 어느 명령의 완료인지 맞춘다.

서버 쪽 규칙 하나: **응답 send는 절대 블록하지 않는다**(`timeout 0`). 클라이언트가 이미 포기했거나 응답 큐가 찼다고 서버가 멈추면, 클라이언트 하나의 문제가 서버 전체를 멈춘다.

### 9.3 예제 10 — 이벤트 큐 + 상태 기계 + 응답 큐

무엇을 확인하는 코드인가: "모터 컨트롤러" FSM 태스크가 이벤트 큐 하나에서 IMU·명령·고장 이벤트를 받는다. 상태(`st`)는 FSM 태스크만 만지므로 뮤텍스가 필요 없다. 클라이언트는 START(거부되어야 함) → ARM → START(서버가 8 ms 걸려 timeout) → STOP → STOP을 보낸다. 늦은 응답이 시퀀스 번호로 걸러지는지 본다.

```c
/* D4 ex10 — one event queue + state machine; command/response with reply queue and seq numbers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
typedef enum { EV_CMD = 1, EV_IMU, EV_FAULT } ev_t;
typedef enum { OP_ARM = 1, OP_START, OP_STOP } op_t;
typedef enum { ST_IDLE, ST_ARMED, ST_RUN, ST_FAULT } st_t; static const char *stn[] = { "IDLE", "ARMED", "RUN", "FAULT" };
typedef struct {                     /* tagged union: 'type' says which member is valid */
    uint8_t type; uint16_t seq; uint32_t t_us;
    union { struct { uint8_t op; int16_t arg; QueueHandle_t reply; } cmd;
            struct { int16_t gz; } imu;  struct { uint16_t code; } fault; } u;
} evt_t;
typedef struct { uint16_t seq; int8_t status; uint8_t state; } rsp_t; static QueueHandle_t evq;

static void fsm(void *pv)            /* the ONLY place that owns 'st': no mutex needed */
{
    (void)pv; st_t st = ST_IDLE; evt_t e; uint32_t imu_n = 0;
    for (;;) {
        xQueueReceive(evq, &e, portMAX_DELAY);
        st_t old = st; int8_t status = 0;
        switch (e.type) {
        case EV_IMU: imu_n++; break;
        case EV_FAULT: st = ST_FAULT; break;
        case EV_CMD:
            if      (e.u.cmd.op == OP_ARM   && st == ST_IDLE)  st = ST_ARMED;
            else if (e.u.cmd.op == OP_START && st == ST_ARMED) { vTaskDelay(pdMS_TO_TICKS(8)); st = ST_RUN; }
            else if (e.u.cmd.op == OP_STOP  && st != ST_FAULT) st = ST_IDLE;
            else status = -1;                              /* rejected in this state */
            rsp_t r = { e.seq, status, (uint8_t)st };
            xQueueSend(e.u.cmd.reply, &r, 0);               /* never block on a client */
            break;
        }
        if (st != old) board_printf_raw("  fsm: %s -> %s (event %u, seq %u, imu so far %u)\n",
                                        stn[old], stn[st], e.type, e.seq, (unsigned)imu_n);
    }
}

static void imu(void *pv) { (void)pv; evt_t e = { .type = EV_IMU };
    for (;;) { e.seq++; e.t_us = board_now_us(); e.u.imu.gz = 3; xQueueSend(evq, &e, 0); vTaskDelay(2); } }

static void client(void *pv)
{
    (void)pv; static const uint8_t ops[] = { OP_START, OP_ARM, OP_START, OP_STOP, OP_STOP };
    QueueHandle_t myq = xQueueCreate(2, sizeof(rsp_t)); uint16_t seq = 100; rsp_t r;
    for (unsigned i = 0; i < sizeof ops; i++) {
        evt_t e = { .type = EV_CMD, .seq = ++seq, .t_us = board_now_us(),
                    .u.cmd = { .op = ops[i], .arg = 1500, .reply = myq } };
        xQueueSend(evq, &e, pdMS_TO_TICKS(1));
        for (;;) {
            if (xQueueReceive(myq, &r, pdMS_TO_TICKS(5)) != pdPASS) {
                board_printf_raw("client: op %u seq %u TIMEOUT\n", ops[i], seq); break; }
            if (r.seq != seq) { board_printf_raw("client: drop stale reply seq %u\n", r.seq); continue; }
            board_printf_raw("client: op %u seq %u -> status %d state %s\n", ops[i], seq, r.status, stn[r.state]);
            break;
        }
    }
    evt_t f = { .type = EV_FAULT, .seq = 1, .u.fault.code = 0x31 }; xQueueSend(evq, &f, 0);
    vTaskDelay(5); board_printf_raw("sizeof(evt_t)=%u sizeof(rsp_t)=%u\n", (unsigned)sizeof(evt_t), (unsigned)sizeof(rsp_t));
    board_exit(0);
}

int main(void)
{
    evq = xQueueCreate(8, sizeof(evt_t));
    xTaskCreate(fsm, "fsm", 256, NULL, 3, NULL); xTaskCreate(imu, "imu", 256, NULL, 2, NULL);
    xTaskCreate(client, "cli", 256, NULL, 1, NULL);
    vTaskStartScheduler(); return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D4/ex10/ex10.c OUT=/tmp/rtos/D4/ex10 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos/D4/ex10/app.elf 10'
```

```text
client: op 2 seq 101 -> status -1 state IDLE
  fsm: IDLE -> ARMED (event 1, seq 102, imu so far 1)
client: op 1 seq 102 -> status 0 state ARMED
client: op 2 seq 103 TIMEOUT
  fsm: ARMED -> RUN (event 1, seq 103, imu so far 1)
  fsm: RUN -> IDLE (event 1, seq 104, imu so far 3)
client: drop stale reply seq 103
client: op 3 seq 104 -> status 0 state IDLE
client: op 3 seq 105 -> status 0 state IDLE
  fsm: IDLE -> FAULT (event 3, seq 1, imu so far 5)
sizeof(evt_t)=16 sizeof(rsp_t)=4
[board_exit] code=0
```

출력에서 볼 것: IDLE에서의 START(seq 101)는 상태 기계가 거부한다(`status -1`). 세 번째 명령 START(seq 103)는 서버가 8 ms 걸려 클라이언트의 5 ms timeout을 넘겼다. 클라이언트는 포기하고 STOP(seq 104)을 보냈는데, 응답 큐에서 먼저 나온 것은 **seq 103의 늦은 응답**이다. 시퀀스 비교가 그것을 버리고(`drop stale reply seq 103`) 진짜 104 응답을 받는다. 시퀀스 번호가 없었다면 클라이언트는 STOP의 결과를 "state RUN"으로 잘못 알았다. 하나 더: 서버가 8 ms 동안 `vTaskDelay`로 잠든 사이 이벤트 큐의 IMU 이벤트가 쌓였다(`imu so far` 1 → 3). **FSM 핸들러 안에서 블록하면 그 동안 모든 이벤트가 멈춘다** — 오래 걸리는 일은 FSM 밖 작업 태스크에 넘기고 완료를 다시 이벤트로 받는 것이 정석이다(D7 게이트키퍼·작업 위임). `sizeof(evt_t)` = 16: 필드 합 15 B + 패딩 1 B.

### 9.4 이벤트 큐 + 상태 기계 패턴

예제 10의 구조는 RTOS 펌웨어의 가장 흔한 뼈대다. 이름은 여러 가지다 — active object, actor, "event-driven task". 규칙은 셋이다.

1. **태스크마다 이벤트 큐 하나.** 그 태스크에게 일을 시키는 유일한 길이다.
2. **상태는 그 태스크만 만진다.** 다른 태스크는 이벤트를 보낼 뿐이다. 그래서 상태 변수에 뮤텍스가 필요 없고, 우선순위 역전(D3)도 생기지 않는다.
3. **핸들러는 짧게, 블록하지 않는다.** 이벤트 하나를 처리하고 다음 receive로 돌아온다(run-to-completion).

Anduril C 문제은행의 FSM 문제들(상태 전이표, 이벤트 디스패치)이 바로 이 "receive → switch(state, event) → 전이" 루프의 몸통이다 — 거기서 연습한 전이 함수를 그대로 이 태스크의 핸들러로 끼우면 된다. Don의 SSD 펌웨어에서도 명령 상태 기계(명령 하나가 "디코드 → FTL 조회 → NAND 발행 → 완료 대기 → CQ 기록"을 거치는)가 이벤트(NAND 완료 인터럽트, 타이머)로 전진하는 구조였을 것이다.

### 9.5 함정

- **union의 다른 멤버를 읽는다**: type 검사 없이 `e.u.cmd.reply`를 쓰면 IMU 이벤트의 쓰레기 값을 큐 핸들로 쓴다 → HardFault. `switch (e.type)` 밖에서 union을 만지지 않는다.
- **seq가 없는 명령/응답**: 늦은 응답이 다음 요청에 붙는다(예제 10).
- **서버가 응답 큐에 블록한다**: 클라이언트 하나가 서버를 멈춘다. 응답은 timeout 0.
- **FSM 핸들러에서 `vTaskDelay`·긴 블록**: 이벤트 큐가 그동안 쌓이고, 깊이 계산(§4)의 R_max가 그만큼 늘어난다.
- **이벤트 구조체가 너무 크다**: 한 종류 때문에 모든 칸이 커진다. 큰 페이로드는 포인터 + 풀(§3).

---

## 10. RTOS 커널 안에서는 무슨 일이 일어나나

### 10.1 FreeRTOS `Queue_t` — 필드 지도 (`queue.c`)

| 필드 | 뜻 | 메모 |
|---|---|---|
| `pcHead` | 저장 영역 시작 | 뮤텍스로 쓸 때는 `uxQueueType` 별명, NULL이면 뮤텍스(`queueQUEUE_IS_MUTEX`) |
| `pcWriteTo` | 다음에 쓸 칸 | send to back이 쓰고 한 칸 전진 |
| `u.xQueue.pcTail` | 저장 영역 끝 바로 뒤 | 넘으면 `pcHead`로 감는다 |
| `u.xQueue.pcReadFrom` | **마지막으로 읽은** 칸 | receive는 먼저 전진 후 복사. 앞에 넣기·덮어쓰기는 이 칸에 쓰고 후퇴 |
| `u.xSemaphore` | (뮤텍스일 때) holder, 재귀 카운트 | `u`는 union — 큐와 뮤텍스가 공간을 나눠 쓴다 (D2) |
| `xTasksWaitingToSend` | 꽉 차서 기다리는 sender | 우선순위 순 정렬 (C1) |
| `xTasksWaitingToReceive` | 비어서 기다리는 receiver (peek 포함) | 우선순위 순 정렬 |
| `uxMessagesWaiting` | 현재 개수 | `volatile`. 세마포어에서는 이게 카운트 (D1) |
| `uxLength`, `uxItemSize` | 칸 수, 칸 크기 | 생성 후 불변. 세마포어는 item 크기 0 |
| `cRxLock`, `cTxLock` | 큐 잠금 중 들어온 receive/send 수 | `queueUNLOCKED`(−1)이면 잠기지 않음 |
| `pxQueueSetContainer` | 이 큐가 속한 셋 | `configUSE_QUEUE_SETS` |
| `ucStaticallyAllocated`, `uxQueueNumber`, `ucQueueType` | 정적 여부, trace용 번호·종류 | 설정에 따라 있음 |

### 10.2 send 경로 — `xQueueGenericSend()` (`queue.c` 949행~)

모든 send 매크로(`xQueueSend`, `xQueueSendToFront`, `xQueueOverwrite`)는 `xQueueGenericSend(q, pv, ticks, position)` 하나로 간다. 구조는 `for (;;)` 루프다.

1. `taskENTER_CRITICAL()`(BASEPRI로 커널 인터럽트 막기 — C7). 빈 칸이 있거나 overwrite면:
2. `prvCopyDataToQueue()`(2395행): `memcpy`로 칸에 쓰고 포인터 이동, `uxMessagesWaiting++`. item 크기 0이면(뮤텍스) 우선순위 상속 해제(`xTaskPriorityDisinherit`)를 여기서 한다 — 뮤텍스 give가 결국 이 함수다.
3. 셋 멤버면 `prvNotifyQueueSetContainer()`(3344행) — 셋 큐에 자기 핸들을 `prvCopyDataToQueue()`로 넣고 **셋의** 대기자를 깨운다. 멤버 자신의 대기자는 보지 않는다(예제 9 MODE 3의 원인).
4. 셋 멤버가 아니면 `xTasksWaitingToReceive`가 비었는지 보고, 아니면 `xTaskRemoveFromEventList()`로 맨 앞(가장 높은 우선순위) 하나를 ready로. 그 태스크가 나보다 높으면 `queueYIELD_IF_USING_PREEMPTION()` — **크리티컬 섹션 안에서 PendSV를 걸어 두고**, 섹션을 나가는 순간 스위치가 일어난다(소스 주석: "Yes it is ok to do this from within the critical section - the kernel takes care of that").
5. `taskEXIT_CRITICAL()`, `pdPASS` 반환.

꽉 찼으면:

1. timeout 0이면 바로 `errQUEUE_FULL`. 처음 블록하는 거면 `vTaskInternalSetTimeOutState()`로 시작 시각을 기록.
2. 크리티컬 섹션을 **나간 뒤** `vTaskSuspendAll()`(스케줄러 정지) + `prvLockQueue()`(큐 잠금: `cRxLock`·`cTxLock`을 `queueLOCKED_UNMODIFIED`로).
3. `xTaskCheckForTimeOut()`: 아직 시간이 남았고 여전히 꽉 찼으면 `vTaskPlaceOnEventList(&xTasksWaitingToSend, ticks)` — 대기 리스트에 정렬 삽입(O(n), C1) + delayed 리스트에 등록.
4. `prvUnlockQueue()` → `xTaskResumeAll()` → 필요하면 `taskYIELD_WITHIN_API()`. 여기서 태스크가 잔다.
5. 깨어나면(누가 receive해서, 또는 timeout) **루프 처음으로** — 다시 빈 칸을 확인한다. 이게 예제 11에서 S가 두 번 블록한 이유다. 깨어남은 "다시 시도하라"는 신호일 뿐이다.

receive 경로 `xQueueReceive()`(1509행)는 거울상이다. 데이터가 있으면 `prvCopyDataFromQueue()`(2478행: `pcReadFrom += uxItemSize`, 감기, `memcpy`) 후 `uxMessagesWaiting − 1`, 그리고 `xTasksWaitingToSend` 맨 앞을 깨운다.

### 10.3 왜 "큐 잠금"이 따로 있나 — `cRxLock` / `cTxLock`

블록 준비 단계(2~4)는 대기 리스트에 정렬 삽입하는 O(n) 작업이다. 이걸 인터럽트를 막은 채(크리티컬 섹션) 하면 ISR 지연이 대기자 수에 비례해 늘어난다. 그래서 FreeRTOS는 **인터럽트는 열어 두고 스케줄러만 멈춘 상태**(`vTaskSuspendAll`)에서 이 일을 한다. 그런데 그 사이 ISR이 같은 큐에 `xQueueSendFromISR()`를 하면? ISR이 이벤트 리스트를 고치면 태스크 쪽 리스트 작업과 충돌한다. 해법이 큐 잠금이다.

- 큐가 잠겨 있으면(`cTxLock != queueUNLOCKED`) ISR은 **데이터 복사와 개수 증가만** 하고 대기자는 깨우지 않는다. 대신 `prvIncrementQueueTxLock()`으로 "잠긴 동안 send가 몇 번 있었나"를 센다(`xQueueGenericSendFromISR()`의 `if (cTxLock == queueUNLOCKED) { … } else { … }`).
- 태스크가 `prvUnlockQueue()`(2499행)를 부르면 `cTxLock`이 센 만큼 `xTasksWaitingToReceive`에서 대기자를 깨운다(스케줄러가 아직 정지 상태라 pending ready 리스트로 간다 — C1 §2.4). receive 쪽은 `cRxLock`으로 같은 일을 한다.

Don에게 익숙한 말로 하면 "인터럽트를 오래 막지 않으려고, ISR이 할 일의 **후반부를 카운터로 미뤄 두고** 태스크가 잠금을 풀 때 몰아서 처리"하는 설계다. FreeRTOS가 ISR 지연을 대기자 수와 무관하게 유지하는 핵심 장치다.

### 10.4 ISR send — `xQueueGenericSendFromISR()` (1167행)

- `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`: 이 ISR의 우선순위가 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높으면(숫자가 작으면) assert(E1·B3).
- `taskENTER_CRITICAL_FROM_ISR()`(BASEPRI 저장·설정) → 빈 칸 검사 → `prvCopyDataToQueue()` → 잠기지 않았으면 대기자 깨우고 `*pxHigherPriorityTaskWoken = pdTRUE`, 잠겼으면 `cTxLock` 증가 → 복원.
- **블록 경로가 아예 없다.** 꽉 차면 `errQUEUE_FULL`을 돌려줄 뿐이다.

### 10.5 Zephyr `kernel/msg_q.c` — wait_q 하나와 직접 전달

`struct k_msgq`(`include/zephyr/kernel.h`)는 `wait_q`, `lock`(spinlock), `msg_size`, `max_msgs`, `buffer_start`/`buffer_end`, `read_ptr`/`write_ptr`, `used_msgs`, (CONFIG_POLL이면) `poll_events`, `flags`다. 이 빌드에서 `nm -S`로 본 크기는 40 B(FreeRTOS `Queue_t` 84 B). 구조의 차이 둘:

1. **대기 리스트가 하나다.** 꽉 찬 큐에는 sender만, 빈 큐에는 receiver만 기다릴 수 있으니 `wait_q` 하나면 된다(그림 1의 설명).
2. **직접 전달(direct handoff).** `put_msg_in_queue()`(150행)는 빈 칸이 있을 때 먼저 `z_unpend_first_thread(&msgq->wait_q)`로 기다리는 receiver를 찾는다. 있으면 큐 저장 영역을 건너뛰고 `memcpy(pending_thread->base.swap_data, data, msgq->msg_size)` — **receiver가 블록할 때 맡겨 둔 버퍼 주소로 바로 복사**하고 `arch_thread_return_value_set(pending_thread, 0)`로 반환값까지 정해 둔 뒤 ready로 만든다. receiver는 깨어나면 할 일이 없다. 반대 방향도 같다. `z_impl_k_msgq_get()`(300행)은 하나를 꺼낸 뒤 기다리는 **sender**가 있으면 그 sender가 블록할 때 맡긴 `swap_data`(sender의 메시지 주소)에서 큐로 `memcpy`해 넣고 sender를 성공(0)으로 깨운다.

그래서 Zephyr에서는 (a) 블록된 receiver로 가는 메시지가 **한 번만 복사**되고(예제 4의 기울기 절반), (b) 블록된 sender의 메시지가 **receiver의 문맥에서 즉시 큐에 들어간다** — 깨어난 sender가 다시 경쟁할 일이 없다. 예제 11과 같은 시나리오를 Zephyr로 돌려 확인한다.

무엇을 확인하는 코드인가: 예제 11과 똑같은 구성(길이 1, 'I'로 가득, S가 먼저 블록, R이 하나 꺼내고 M을 깨움). Zephyr 우선순위는 S = 7, M = 6, R(main) = 5(숫자가 작을수록 높다).

```c
/* D4 ex11z — Zephyr: k_msgq_get() copies the waiting sender's message in for it (no retry) */
#include <zephyr/kernel.h>
K_MSGQ_DEFINE(q, 1, 1, 1);
K_SEM_DEFINE(go, 0, 1);

static void S(void *a, void *b, void *c)          /* prio 7 (lowest): waiting FIRST */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	char m = 'S';
	k_msgq_put(&q, &m, K_FOREVER);
	printk("  S returned from put\n");
}

static void M(void *a, void *b, void *c)          /* prio 6: arrives later */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	char m = 'M';
	k_sem_take(&go, K_FOREVER);
	printk("  M put: queue has %u/1 -> M must %s\n", k_msgq_num_used_get(&q),
	       k_msgq_num_used_get(&q) ? "block" : "not block");
	k_msgq_put(&q, &m, K_FOREVER);
	printk("  M returned from put\n");
}

K_THREAD_DEFINE(ts, 1024, S, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(tm, 1024, M, NULL, NULL, NULL, 6, 0, 0);

int main(void)                                    /* main = R, prio 5 (highest of the three) */
{
	char m = 'I';
	k_thread_priority_set(k_current_get(), 5);
	k_msgq_put(&q, &m, K_NO_WAIT);                /* full from the start */
	for (int i = 0; i < 3; i++) {
		k_msleep(5);
		k_msgq_get(&q, &m, K_NO_WAIT);
		printk("R got %c\n", m);
		if (i == 0) k_sem_give(&go);
	}
	printk("DONE\n");
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'export XDG_CACHE_HOME=/tmp/rtos/D4/xdgcache; D=/tmp/rtos/D4/ex11z; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D4/ex11z $D >/dev/null && timeout 10 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel $D/zephyr/zephyr.elf 2>&1 | sed "/DONE/q"'
```

```text
*** Booting Zephyr OS build v4.4.2 ***
R got I
  M put: queue has 1/1 -> M must block
  S returned from put
R got S
  M returned from put
R got M
DONE
```

출력에서 볼 것: R이 'I'를 꺼낸 **그 호출 안에서** S의 'S'가 큐에 들어갔다. 그래서 M이 돌았을 때 큐는 이미 1/1이고 M이 블록한다. 받은 순서는 **I, S, M** — 먼저 기다린 쪽이 먼저다. FreeRTOS(예제 11)는 I, M, S였다. 어느 쪽이 "맞다"가 아니라 **보장이 다르다**: Zephyr는 대기자 FIFO(우선순위 순 안에서) 공정성을 주고, FreeRTOS는 "깨어난 뒤 그 시점의 최고 우선순위가 이긴다"를 준다. OSAL로 두 RTOS를 감싼 코드가 이 순서에 의존하면 이식할 때 동작이 바뀐다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330" y="20" font-size="14" text-anchor="middle" font-weight="bold">꽉 찬 큐를 기다리던 sender S가 깨어나는 방식</text>
<text x="170" y="46" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS — 깨워서 재시도</text>
<text x="490" y="46" font-size="13" text-anchor="middle" font-weight="bold">Zephyr — 대신 넣어 주고 깨움</text>
<rect x="20" y="58" width="300" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="77" font-size="12" text-anchor="middle">① R이 xQueueReceive로 I를 꺼냄</text>
<text x="170" y="95" font-size="12" text-anchor="middle">→ 칸 1개 빔</text>
<rect x="20" y="118" width="300" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="137" font-size="12" text-anchor="middle">② S를 대기 줄에서 ready로 옮김</text>
<text x="170" y="155" font-size="12" text-anchor="middle">(S의 데이터는 아직 S 스택에)</text>
<rect x="20" y="178" width="300" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170" y="197" font-size="12" text-anchor="middle">③ M(prio 2)이 S보다 먼저 실행</text>
<text x="170" y="215" font-size="12" text-anchor="middle">빈 칸에 M이 들어감</text>
<rect x="20" y="238" width="300" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170" y="257" font-size="12" text-anchor="middle">④ S가 for (;;) 재시도 → 또 꽉 참</text>
<text x="170" y="275" font-size="12" text-anchor="middle">→ 다시 블록 (d4_blk = 2)</text>
<line x1="170" y1="104" x2="170" y2="116" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="164" x2="170" y2="176" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="224" x2="170" y2="236" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="340" y="58" width="300" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="490" y="77" font-size="12" text-anchor="middle">① R이 k_msgq_get으로 I를 꺼냄</text>
<text x="490" y="95" font-size="12" text-anchor="middle">→ 칸 1개 빔</text>
<rect x="340" y="118" width="300" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="490" y="137" font-size="12" text-anchor="middle">② 같은 spinlock 안에서 S의</text>
<text x="490" y="155" font-size="12" text-anchor="middle">swap_data를 큐에 memcpy</text>
<rect x="340" y="178" width="300" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="490" y="197" font-size="12" text-anchor="middle">③ S를 ready로 (반환값 0을 미리 설정)</text>
<text x="490" y="215" font-size="12" text-anchor="middle">S의 put은 이미 끝난 셈</text>
<rect x="340" y="238" width="300" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="490" y="257" font-size="12" text-anchor="middle">④ M이 put → 꽉 참 → M이 블록</text>
<text x="490" y="275" font-size="12" text-anchor="middle">(먼저 기다린 S가 먼저 들어감)</text>
<line x1="490" y1="104" x2="490" y2="116" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="490" y1="164" x2="490" y2="176" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="490" y1="224" x2="490" y2="236" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="170" y="310" font-size="13" text-anchor="middle">R이 받은 순서: I, M, S</text>
<text x="490" y="310" font-size="13" text-anchor="middle">R이 받은 순서: I, S, M</text>
</svg>
```
그림 9 — 예제 11(FreeRTOS)과 예제 11z(Zephyr)의 차이를 소스 경로로 그린 것. FreeRTOS `xQueueReceive()`는 `xTaskRemoveFromEventList(&xTasksWaitingToSend)`로 깨우기만 하고, Zephyr `z_impl_k_msgq_get()`은 깨우기 전에 `memcpy(msgq->write_ptr, pending_thread->base.swap_data, msg_size)`로 sender의 일을 대신 끝낸다. 같은 방식이 receiver 쪽에도 적용되어 Zephyr는 블록된 receiver로 가는 메시지를 한 번만 복사한다.

### 10.6 예제 12 — 코드 크기와 객체 크기 (`nm`)

무엇을 확인하는 코드인가: 큐 구현이 플래시와 RAM을 얼마나 쓰는지. 예제 3(FreeRTOS)과 예제 4·11z(Zephyr 기본 빌드)의 ELF에서 큐 관련 심볼 크기를 본다.

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-nm --size-sort -S /tmp/rtos/D4/ex3/app.elf | grep -i "queue\|memcpy"; for d in ex4z ex11z; do echo "## $d"; arm-none-eabi-nm -S --size-sort /tmp/rtos/D4/$d/zephyr/zephyr.elf | grep -iE "msgq|put_msg| q$"; done'
```

```text
200017bc 00000004 b xTimerQueue
20001680 00000040 B xQueueRegistry
00001b78 00000050 T vQueueWaitForMessageRestricted
200016c0 00000054 b xStaticTimerQueue.0
00001b10 00000068 T vQueueAddToRegistry
00001bc8 0000006c t prvCheckForValidListAndQueue
00001aa4 0000006c T vQueueDelete
00001744 0000006c T xQueueGenericCreate
00001484 00000078 t prvCopyDataToQueue
000016cc 00000078 T xQueueGenericCreateStatic
000014fc 00000088 t prvNotifyQueueSetContainer
00001584 0000009a t prvUnlockQueue
20001714 000000a0 b ucStaticTimerQueueStorage.1
00001620 000000ac T xQueueGenericReset
000024e8 000000ec T memcpy
00001944 00000160 T xQueueReceive
000017b0 00000194 T xQueueGenericSend
## ex4z
00002aee 0000000e T z_impl_k_msgq_put
00002ad0 0000001e T k_msgq_init
200002d0 00000028 b q
0000119c 000000c4 T z_impl_k_msgq_get
000010d4 000000c8 t put_msg_in_queue
## ex11z
00002b20 0000000e T z_impl_k_msgq_put
200000a4 00000028 D q
000010e8 000000c4 T z_impl_k_msgq_get
00001020 000000c8 t put_msg_in_queue
```

출력에서 볼 것: FreeRTOS의 send 0x194 = 404 B, receive 0x160 = 352 B, 그리고 `prvCopyDataToQueue`(120)·`prvUnlockQueue`(154)·`prvNotifyQueueSetContainer`(136) 등 — 큐 하나 쓰는 데 약 1.5 KB가 링크된다(`--gc-sections`로 안 쓰는 함수는 빠진 뒤). 이 앱이 큐를 직접 쓰지 않아도 **소프트웨어 타이머가 내부적으로 큐**(`xTimerQueue`, 저장 영역 `ucStaticTimerQueueStorage` 160 B = 길이 10 × 16 B 명령)를 쓴다는 것도 보인다 — D6에서 다시 만난다. `xQueueRegistry` 64 B는 `configQUEUE_REGISTRY_SIZE 8`의 디버거용 이름표다. Zephyr는 put 경로 `put_msg_in_queue` 200 B + get 196 B로 절반 이하이고, `struct k_msgq` `q`는 0x28 = 40 B다. Zephyr의 put/get이 작고 빠른(예제 4의 블록 없는 경로) 이유가 이 크기 차이에 그대로 보인다 — 큐 잠금·큐 셋·재시도 루프가 없다.

---

## 11. 실무에서 다시 보기

### 11.1 드론 비행 제어기 — IMU ISR → 큐 → 자세 추정기

예를 들어 STM32H7급 FC에서 IMU(SPI, 8 kHz)의 data-ready 인터럽트 → DMA로 샘플 읽기 → DMA 완료 ISR이 샘플을 넘기고 → 추정기 태스크가 처리한다.

- **값이냐 포인터냐**: 샘플 하나는 자이로 3축 + 가속도 3축 × 16비트 + 타임스탬프 = 16 B 정도라 **값 복사**가 맞다(예제 3: 16 B는 4 B와 비용이 거의 같다). IMU FIFO를 통째로(예: 32 샘플 512 B) DMA로 읽는 설계라면 **두 큐 버퍼 풀**(예제 5)로 DMA 버퍼 포인터를 넘긴다.
- **깊이**: 추정기 태스크의 최악 응답 시간이 2.5 ms라면 8 kHz × 2.5 ms = 20개, 여유 25%로 25(예제 13의 마지막 줄). 실효 서비스 시간에 ISR 오버헤드를 넣는 것을 잊지 않는다(예제 6의 20 vs 21).
- **정책**: ISR이 생산자라 블록 불가 → 새것 버림 + 손실 카운터, 카운터는 텔레메트리로. 자이로 적분(각도 추정)에는 샘플 손실이 오차가 되므로 손실 0이 목표이고, 손실이 나면 타임스탬프 간격으로 dt를 보정한다(그래서 메시지에 타임스탬프를 넣는다 — §9.1).
- **ISR 경로**: `xQueueSendFromISR` + `portYIELD_FROM_ISR` 필수(예제 8: 빼먹으면 최대 1 ms 지연 — 8 kHz 샘플 8개분). 이 경로가 너무 무거우면(태스크 하나에 신호만 필요) task notification(D5)이나 SPSC 링 + notification(D8)으로 바꾼다.
- **추정 결과 → rate loop**: 자세 추정 결과는 "최신값만 의미 있음"이라 길이 1 mailbox + `xQueueOverwrite` / `xQueuePeek`(예제 7). 오래된 자세로 제어하는 것보다 낫다.

### 11.2 텔레메트리 · 명령 큐 — 지상국 명령을 처리하는 FC

UART/USB로 들어온 MAVLink류 명령은 파서 태스크가 디코드해서 **비행 모드 FSM 태스크의 이벤트 큐**로 보낸다(예제 10 구조). 명령마다 시퀀스 번호, 응답(ACK)은 응답 큐 또는 텔레메트리 송신 큐로. 원격 명령은 잃으면 안 되므로 파서 → FSM은 짧은 timeout 블록 + 실패 시 NACK. FSM 핸들러가 오래 걸리는 일(예: 센서 캘리브레이션 5초)은 작업 태스크로 넘기고 완료를 이벤트로 받는다 — 그 사이에도 "ARM 금지" 같은 안전 이벤트를 즉시 처리할 수 있어야 하기 때문이다.

텔레메트리 송신 쪽은 반대로 "오래된 것은 쓸모없음"이다. 송신 큐가 차면 **옛것 버림**이 맞고, 버린 수를 다음 텔레메트리 패킷에 실어 지상국이 링크 품질을 알게 한다.

### 11.3 로봇 관절 모터 컨트롤러 — CAN RX 큐

CAN 컨트롤러의 하드웨어 수신 FIFO는 작다(예: STM32 bxCAN은 3단 FIFO 두 개). 그래서 CAN RX ISR이 프레임을 꺼내 **소프트웨어 큐로 값 복사**한다(클래식 CAN 프레임 = ID 4 B + DLC + 데이터 8 B ≈ 16 B 구조체, CAN FD 64 B 데이터면 72 B 정도 → 길이 32면 2.3 KB). 깊이 계산은 버스 부하에서 한다. 1 Mbit/s 클래식 CAN에서 8 B 데이터 프레임은 비트 스터핑 포함 약 110~130비트라 버스가 100% 차도 초당 약 7600~9000 프레임이고, 그중 이 노드 필터를 통과하는 비율 × 프로토콜 태스크의 최악 응답 시간이 깊이다. FOC 전류 루프(20 kHz)는 이 큐와 무관하게 ISR/타이머로 돌고, CAN 큐 소비자는 그보다 낮은 우선순위의 명령 태스크다 — 그래서 소비자의 최악 응답 시간에 전류 루프 ISR 부하가 그대로 더해진다(§4.4의 교훈). 명령 프레임 중 "비상 정지"는 큐를 타지 않고 ISR에서 바로 PWM을 끄거나 `xQueueSendToFront`보다 더 확실한 별도 경로(task notification + 최고 우선순위 태스크)로 보낸다. CAN 컨트롤러 자체(필터·bus-off 복구)는 J6.

### 11.4 Don의 SSD 펌웨어 — 같은 문제, 다른 이름

| SSD 펌웨어 | RTOS 큐 개념 | 이 노트의 예제 |
|---|---|---|
| NVMe SQ(64 B 명령) · CQ(16 B 완료) | 고정 크기 칸 링, 생산자/소비자 분리 | 예제 2 (링 포인터) |
| CQ 엔트리의 Command Identifier · phase tag | 시퀀스 번호, "새 항목인가" 판정 | 예제 10 (stale reply), 예제 7 (mailbox 같은 값 재읽기) |
| 명령 슬롯 free 리스트 → 단계별 큐 → 반납 | 두 큐 버퍼 풀, 소유권 이동 | 예제 5 |
| Queue Depth(QD), 큐가 차면 호스트가 기다림 | 깊이 설계, back-pressure(블록 send) | 예제 6, 7 |
| executive 루프가 큐를 폴링 | 블로킹 receive (스케줄러가 깨워 줌) | 예제 3 (깨움 경로 11 µs) |
| 완료 인터럽트 → 메인 루프 깃발 | `FromISR` + yield 플래그 | 예제 8 |

SSD에서 "QD를 올리면 처리량은 늘고 지연은 늘어난다"는 경험이 Little's law(L = λW) 그 자체다. 큐에 쌓인 명령 수(L)를 처리량(λ)으로 나누면 평균 대기 지연(W)이다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 스택 변수의 주소를 포인터 큐로 보냄 | 소비자가 "가끔" 엉뚱한 값을 읽음 | 값이 아니라 주소가 복사됨 (예제 1) | 값 복사로 바꾸거나 정적 버퍼 풀 사용 |
| send 후 버퍼를 다시 씀 | 체크섬 오류, 간헐적 데이터 깨짐 | 소유권이 이미 넘어감 (예제 5 BUG) | send 뒤 `b = NULL`, owner 필드·체크섬 검증 |
| 평균 부하로 깊이 결정 | 버스트 끝부분 샘플이 주기적으로 빠짐 | 최악 적체 > 깊이 (예제 6) | 버스트 공식·최악 응답 시간으로 깊이, high-water mark 기록 |
| send 반환값 무시 | 손실이 있는데 아무도 모름 | timeout 0 실패가 조용히 지나감 | 손실 카운터 + 텔레메트리 |
| ISR에서 `xQueueSend()` 호출 | `configASSERT` (port.c:438) 또는 assert 꺼진 빌드에서 크래시 | 태스크용 크리티컬 섹션을 ISR에서 사용 | `xQueueSendFromISR()` |
| `portYIELD_FROM_ISR` 누락 | ISR → 태스크 지연이 최대 1 tick (예제 8: 1.005 ms) | 스위치를 다음 tick까지 미룸 | ISR 끝에 `portYIELD_FROM_ISR(woken)` |
| 큐 셋 길이를 멤버 합보다 작게 | `configASSERT` (queue.c:3355), assert 끄면 이벤트 유실 | 셋 큐가 넘침 (예제 9 MODE 1) | 셋 길이 = 멤버 길이 합 |
| 셋 멤버를 직접 receive / 직접 블록 | `EMPTY!` 또는 timeout만큼 늦은 깨어남 | 셋 큐의 핸들 수와 실제 데이터 불일치 (MODE 3·4) | select가 준 핸들만 timeout 0으로 읽기, 또는 셋 대신 이벤트 큐 |
| 응답에 시퀀스 번호 없음 | 명령 결과가 다른 요청에 붙음 | timeout 뒤 늦은 응답 (예제 10) | 요청마다 seq, 불일치 응답 버림 |
| item 크기와 받는 버퍼 불일치 | 스택 깨짐, HardFault | 커널은 `uxItemSize`만큼 무조건 복사 | 타입별 래퍼 함수, `_Static_assert` |

---

## 13. 면접에서 이렇게 말한다

**Q.** "How does a FreeRTOS queue pass data — by copy or by reference? When would you pass a pointer instead?"

**A.** 기본은 값 복사다. send 때 큐 칸으로 `memcpy`, receive 때 다시 `memcpy`해서 두 번 복사된다. 보내는 쪽이 버퍼를 바로 재사용할 수 있고 소유권 문제가 없는 대신, 크기에 비례한 비용과 RAM이 든다. Cortex-M3 25 MHz QEMU에서 재 보니 깨움 경로가 4 B에서 11 µs, 1 KB에서 48 µs였고 증가분이 정확히 memcpy 두 번이었다. 그래서 수십 바이트까지는 값으로, 수백 바이트 이상은 포인터만 보내고 버퍼는 풀에서 소유권을 넘긴다.

> By default it copies by value: the item is memcpy'd into the queue storage on send and copied out again on receive, so two copies. That makes ownership trivial — the sender can reuse its buffer immediately — but cost and RAM scale with item size. On a Cortex-M3 model I measured about 11 µs for a 4-byte message through the wake-up path and about 48 µs for 1 KB, and the slope was exactly two memcpys. So small messages go by value, and anything in the hundreds of bytes goes as a pointer from a buffer pool, with explicit ownership transfer.

**꼬리 질문**: "Is Zephyr's k_msgq the same?" → 값 복사지만 기다리는 수신자가 있으면 수신자 버퍼로 바로 복사(direct handoff)해서 한 번만 복사한다.

**Q.** "How do you transfer ownership of a large buffer between an ISR and a task safely?"

**A.** 두 큐 버퍼 풀을 쓴다. 빈 버퍼 포인터를 담은 free 큐와 찬 버퍼 포인터를 담은 full 큐, 둘 다 길이를 버퍼 개수와 같게. ISR은 free에서 꺼내 채우고 full로 보낸 뒤 그 버퍼를 다시는 만지지 않는다. 태스크는 처리 후 free로 돌려준다. 버퍼는 항상 정확히 한 곳에 있고, 풀 고갈은 free 큐 receive 실패로 바로 보인다. 디버그 빌드에서는 owner 필드와 체크섬으로 "보낸 뒤 쓰기"를 잡는다.

> I use a two-queue buffer pool: a free queue holding pointers to empty buffers and a full queue holding pointers to filled ones, both as deep as the pool. The ISR takes a buffer from the free queue, fills it, sends the pointer to the full queue, and never touches it again; the task processes it and returns it to the free queue. Every buffer is in exactly one place at any time, sends can never fail, and pool exhaustion shows up as a failed receive on the free queue. In debug builds I add an owner field and a checksum to catch use-after-send.

**Q.** "How do you size a message queue?"

**A.** 평균이 아니라 최악 적체로 정한다. 먼저 이용률 λ×S가 1보다 작은지 확인하고, 그 다음 소비자가 못 오는 최대 시간(최악 응답 시간) × 최대 도착률, 또는 버스트 공식 B − ⌊(B−1)T_a/S⌋ − 1로 최대 적체를 구한다. 10 kHz로 60개 버스트, 소비자 150 µs인 실험에서 공식은 20, 측정은 21이었다 — ISR 오버헤드가 실효 서비스 시간을 늘렸기 때문이다. 평균 큐 길이는 4.7이었다. 그리고 high-water mark를 필드 로그로 남긴다.

> From the worst-case backlog, not the average. First check utilisation λ·S is below one — otherwise no depth helps. Then the depth is the peak arrival rate times the longest time the consumer can be away, which is its worst-case response time, or for a burst, B minus the number served during the burst. In an experiment with 60-message bursts at 10 kHz and a 150 µs consumer the formula said 20 and the measurement said 21, because the producer ISR itself stretches the effective service time, while the average occupancy was only 4.7. I also log the high-water mark in the field.

**꼬리 질문**: "What does Little's law tell you here?" → L = λW, 평균 대기 개수로 평균 지연을 계산한다. 깊이가 아니라 **큐가 더하는 지연**을 볼 때 쓴다. 실험에서 L 4.66 vs λW 4.81로 3% 안에서 맞았다.

**Q.** "What happens when a queue is full? What policies do you use?"

**A.** send의 timeout으로 고른다. 무한 블록은 손실이 없지만 생산자가 소비자 속도로 끌려가고 데드락 후보가 된다. timeout 블록, 새것 버림, 옛것 버림(receive 후 send — 생산자가 하나일 때만), 길이 1 큐 + `xQueueOverwrite` mailbox가 있다. ISR은 블록할 수 없으니 버리고 센다. 명령은 블록 + timeout, 로그는 버림 + 카운터, 상태값은 mailbox다. mailbox는 같은 값을 다시 읽을 수 있어서 시퀀스 번호를 같이 넣는다.

> It's chosen per send through the timeout. Blocking forever never loses data but throttles the producer to the consumer's pace and can deadlock. Then there's block-with-timeout, drop-newest, drop-oldest — receive one then send, only safe with a single producer — and a length-one queue with xQueueOverwrite as a mailbox. ISRs can't block, so they drop and count. I use blocking with a timeout for commands, drop-and-count for logs, and a mailbox for state like the latest attitude estimate, with a sequence number so the reader knows whether the value is new.

**Q.** "What are the rules for using a queue from an ISR?"

**A.** 블록하지 않는 `FromISR` 버전만 쓰고, `xHigherPriorityTaskWoken`를 받아 ISR 끝에서 `portYIELD_FROM_ISR`를 부른다. ISR 우선순위는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하여야 한다. yield를 빼먹으면 깨운 태스크가 다음 tick까지 기다린다 — 측정해 보니 yield가 있으면 11 µs, 없으면 평균 0.5 ms, 최대 1 ms였다. ISR에서 일반 `xQueueSend`를 부르면 포트의 크리티컬 섹션 assert가 잡는다.

> Only the non-blocking FromISR variants, passing a higher-priority-task-woken flag and calling portYIELD_FROM_ISR at the end, and only from interrupts at or below configMAX_SYSCALL_INTERRUPT_PRIORITY. Forgetting the yield defers the switch to the next tick — I measured 11 µs with it versus 0.5 ms on average and 1 ms worst case without it. Calling the plain xQueueSend from an ISR trips the port's critical-section assert. In Zephyr you call the same k_msgq_put with K_NO_WAIT and the kernel reschedules on interrupt exit.

**Q.** "How do you wait on multiple queues at once?"

**A.** FreeRTOS에는 큐 셋이 있지만 함정이 많다. 셋 길이가 멤버 길이 합보다 작으면 assert가 나고, 비어 있지 않은 큐는 추가가 안 되고, select가 준 핸들 없이 멤버를 읽으면 개수가 어긋나고, 멤버에서 직접 블록한 태스크는 깨워지지 않는다. 그래서 보통은 모든 생산자가 tagged union 이벤트를 큐 하나에 넣는 이벤트 큐 패턴을 쓰거나, task notification 비트로 "어느 큐에 일이 있나"를 알린다. Zephyr는 `k_poll`.

> FreeRTOS has queue sets, but they're full of traps: the set must be as long as the sum of its members or it asserts, you can't add a non-empty queue, reading a member without first getting its handle from select desynchronises the set, and a task blocked directly on a member isn't woken. So I usually funnel everything into one event queue of tagged-union messages, or use task-notification bits to say which queue has work. In Zephyr the equivalent is k_poll.

**Q.** "If a task blocked on a full queue is woken because space became available, is it guaranteed to get that slot?"

**A.** FreeRTOS에서는 아니다. 깨어남은 "다시 시도하라"는 뜻이고 `xQueueGenericSend`가 루프를 다시 돌기 때문에, 그 사이 더 높은 우선순위 태스크가 칸을 가져갈 수 있다 — 실험에서 먼저 기다린 S보다 나중에 온 M이 먼저 들어갔다. Zephyr `k_msgq_get`은 기다리던 sender의 메시지를 그 자리에서 큐에 복사해 넣고 성공으로 깨우므로 먼저 기다린 쪽이 먼저 들어간다.

> Not in FreeRTOS. The wake-up only means "retry": xQueueGenericSend loops, so a higher-priority task that runs first can take the slot — in my test a later medium-priority sender got in ahead of the low-priority one that had been waiting. Zephyr's k_msgq_get copies the waiting sender's message into the queue inside the same locked section and wakes it with success, so first-come waiters win. Code behind an OSAL shouldn't depend on either ordering.

**Q.** "Describe an event-driven task design you'd use for a motor controller."

**A.** 태스크마다 이벤트 큐 하나, 상태는 그 태스크만 만지고, 핸들러는 짧게 블록 없이. 이벤트는 type·seq·timestamp 헤더 + union. 명령은 클라이언트의 응답 큐 핸들을 실어 보내고 서버는 timeout 0으로 답하며, 클라이언트는 seq로 늦은 응답을 버린다. 오래 걸리는 일은 작업 태스크로 넘기고 완료를 다시 이벤트로 받는다. 상태에 뮤텍스가 필요 없고 우선순위 역전도 없다.

> One event queue per task, state owned only by that task, short non-blocking handlers. Events are a tagged union with a common header — type, sequence number, timestamp. Commands carry the client's reply-queue handle, the server replies with a zero timeout so a slow client can't stall it, and the client discards replies whose sequence number doesn't match, which handles late responses after a timeout. Long operations go to a worker and come back as completion events. No mutex on the state and no priority inversion.

---

## 14. 직접 해보기

1. **(개념)** 길이 8, item 24 B인 FreeRTOS 큐가 이 랩 설정에서 쓰는 RAM은 몇 바이트인가? `xQueueCreate()`와 `xQueueCreateStatic()`에서 heap 사용이 어떻게 다른가?
   정답/힌트: 제어 구조체 84 B + 저장 영역 8 × 24 = 192 B = 276 B. 동적 생성은 `pvPortMalloc(sizeof(Queue_t) + 192)` 한 번(heap_4 블록 헤더 8 B 추가), 정적 생성은 heap 0.
2. **(코드)** 예제 2에 `xQueueSendToFront`를 연속 두 번(Y, Z) 넣은 뒤 drain해서 순서를 확인하라. 앞에 넣기끼리는 FIFO인가 LIFO인가?
   정답/힌트: LIFO. Z가 Y보다 먼저 나온다 — 둘 다 `pcReadFrom` 자리에 쓰고 후퇴하기 때문.
3. **(측정)** 예제 3을 item 8·12·20 B로 돌려서 4 B 정렬이 아닌 크기(예: 6, 10 B)와 비교하라. `memcpy`가 바이트 경로로 떨어지는 크기가 보이는가?
   정답/힌트: 칸 시작 주소가 4의 배수가 아니면(6·10 B item의 홀수 번째 칸) newlib `memcpy`가 정렬 검사(`orr r3,r1,r0` / `ands r3,#3`)에서 바이트 경로로 간다. 평균 비용이 크기 대비 튀는 것을 본다.
4. **(측정)** 예제 6에서 소비자 서비스 시간을 120 µs와 180 µs로 바꿔 필요한 깊이를 예측(예제 13의 `burst_backlog`)한 뒤 측정과 비교하라. 180 µs에서 버스트 주기 20 ms 안에 다 비우는지(P ≥ B × S)도 확인하라.
   정답/힌트: 120 µs → 약 11~12, 180 µs → 약 27~28(ISR 오버헤드로 +1 가능). 180 µs × 60 = 10.8 ms < 20 ms라 안정.
5. **(코드)** 예제 8의 ISR 생산자에 "옛것 버림" 정책(`xQueueReceiveFromISR` 후 `xQueueSendFromISR`)을 넣고 깊이 2로 소비자를 느리게 해서, 받은 값이 최신 쪽인지 확인하라.
   정답/힌트: 두 `FromISR` 호출 사이에 같은 큐를 만지는 더 높은 ISR이 없을 때만 안전하다. `woken`은 두 호출의 결과를 OR로 모은다(같은 변수를 둘 다에 넘기면 된다).
6. **(설계)** 예제 9의 큐 셋 프로그램을 "이벤트 큐 하나 + tagged union"으로 다시 짜라. RAM(셋 7칸 × 4 B + 멤버 저장 영역 vs 이벤트 큐 7칸 × `sizeof(evt_t)`)과 코드 줄 수를 비교하라.
   정답/힌트: 셋 방식은 셋 큐 28 B + 멤버 24 B + 제어 구조체 4개(336 B), 이벤트 큐는 7 × 8 B(type + 값) + 84 B. 함정 넷이 사라진다.
7. **(Zephyr)** 예제 10을 Zephyr `k_msgq`로 옮겨라. 반환값 규약(0 = 성공), `k_msgq_put_front`에 timeout이 없다는 점, 응답 send의 `K_NO_WAIT`를 확인하라.
   정답/힌트: `if (xQueueReceive(...) != pdPASS)` → `if (k_msgq_get(...) != 0)`. timeout 반환값은 `-EAGAIN`, 꽉 찬 `K_NO_WAIT` put은 `-ENOMSG`.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| message queue | 메시지 큐 | 고정 크기 item N개의 FIFO + 보내는 쪽/받는 쪽 대기 리스트 |
| item size / length | 칸 크기 / 칸 수 | 생성 때 정하고 바뀌지 않는다. RAM = 길이 × 크기 + 제어 구조체 |
| copy semantics | 값 복사 의미론 | send·receive 때 데이터를 `memcpy`로 옮긴다 (FreeRTOS 기본) |
| ownership transfer | 소유권 이동 | 포인터를 보내면 버퍼의 권리가 받는 쪽으로 넘어간다. 보낸 쪽은 만지지 않는다 |
| two-queue buffer pool | 두 큐 버퍼 풀 | free 큐 + full 큐로 버퍼 포인터를 돌리는 무복사 패턴 |
| back-pressure | 역압 | 큐가 차면 생산자가 블록되어 소비자 속도로 느려지는 것 |
| mailbox | 메일박스 | 길이 1 큐 + 덮어쓰기. 최신값 하나만 유지 (`xQueueOverwrite`) |
| high-water mark | 최고 수위 | 큐에 동시에 쌓인 최대 개수. 깊이 검증·필드 로그용 |
| utilisation (ρ) | 이용률 | λ × S. 1 이상이면 어떤 깊이도 소용없다 |
| Little's law | 리틀의 법칙 | L = λW. 평균 대기 개수 = 도착률 × 평균 대기 시간 |
| worst-case response time | 최악 응답 시간 | 소비자가 큐를 비우지 못할 수 있는 최대 시간. 깊이 공식의 R_max (N 모듈) |
| queue set | 큐 셋 | 멤버 핸들을 담는 큐. 여러 큐/세마포어를 한 번에 기다림 |
| tagged union | 태그 붙은 공용체 | type 필드가 union의 유효 멤버를 알려 주는 메시지 구조 |
| reply queue | 응답 큐 | 요청에 실어 보내는 클라이언트 전용 큐 핸들 |
| sequence number | 시퀀스 번호 | 요청-응답 짝 맞추기, 새 값 여부 판정 |
| queue lock (`cRxLock`/`cTxLock`) | 큐 잠금 | 스케줄러 정지 중 ISR의 대기자 깨우기를 카운터로 미뤄 두는 장치 |
| direct handoff | 직접 전달 | Zephyr가 대기 중인 상대의 버퍼와 바로 복사하는 것 (`swap_data`) |
| `xHigherPriorityTaskWoken` | 깨움 플래그 | `FromISR`가 "더 높은 태스크를 깨웠다"고 알려 주는 출력 인자 |

---

## 16. 요약 & 체크리스트

메시지 큐는 "크기 고정 칸 N개 + 두 줄의 대기자"다. FreeRTOS는 item을 넣을 때·꺼낼 때 두 번 `memcpy`하고(예제 3: 깨움 경로 4 B 284 사이클, 기울기 = 복사 두 번), Zephyr `k_msgq`는 기다리는 상대가 있으면 그 버퍼와 바로 복사해 한 번으로 줄인다(예제 4: 기울기 절반, 단 기본 빌드의 picolibc `memcpy`는 바이트 루프라 7배 느리다). 큰 데이터는 포인터만 보내고 두 큐 버퍼 풀로 소유권을 넘기며, 보낸 뒤에는 만지지 않는다(예제 5). 깊이는 평균이 아니라 최악 적체로 정한다 — 이용률 확인 후 버스트 공식이나 "도착률 × 소비자 최악 응답 시간"으로, ISR 오버헤드를 넣은 실효 서비스 시간으로 계산한다(예제 6·13: 모델 max(0, 21 − D)와 측정이 열 개 깊이에서 일치, Little's law 3% 안). full일 때 정책은 send마다 timeout으로 고르고 손실은 센다(예제 7). ISR은 `FromISR` + `portYIELD_FROM_ISR`(예제 8: 11 µs vs 최대 1 ms). 큐 셋은 함정이 넷이라 보통 이벤트 큐 하나 + tagged union + 상태 기계로 대신하고, 명령/응답에는 응답 큐와 시퀀스 번호를 쓴다(예제 9·10). 커널 안에서는 `xQueueGenericSend()`의 재시도 루프·큐 잠금·셋 통지가 FreeRTOS의 성격을, `put_msg_in_queue()`의 직접 전달이 Zephyr의 성격을 정한다(예제 11·11z: 대기 sender의 순서가 다르다).

- [ ] FreeRTOS 큐가 값 복사이고 깨움 경로에서 복사가 두 번 일어난다는 것을 측정 숫자로 말할 수 있다
- [ ] `pcWriteTo`(다음에 쓸 칸)와 `pcReadFrom`(마지막으로 읽은 칸)의 뜻과 앞에 넣기·덮어쓰기가 O(1)인 이유를 그릴 수 있다
- [ ] item 크기에 따라 값 복사 / 포인터 + 풀 / stream buffer / mailbox를 고를 수 있다
- [ ] 두 큐 버퍼 풀을 짜고 "보낸 뒤 만지지 않기"를 owner 필드·체크섬으로 검증할 수 있다
- [ ] 이용률, 버스트 공식, 최악 응답 시간으로 큐 깊이를 계산하고 Little's law로 지연을 설명할 수 있다
- [ ] 블록 / timeout / 새것 버림 / 옛것 버림 / 덮어쓰기의 차이와 각각 맞는 데이터를 말할 수 있다
- [ ] `xTicksToWait`의 의미(0, `pdMS_TO_TICKS`, `portMAX_DELAY`)와 timeout이 재시도에도 총 시간으로 지켜지는 원리를 설명할 수 있다
- [ ] ISR 큐 규칙 셋(블록 금지, `FromISR`, yield 플래그)과 yield 누락의 비용을 말할 수 있다
- [ ] 큐 셋의 함정 넷과 대안(이벤트 큐, task notification, `k_poll`)을 말할 수 있다
- [ ] tagged union 이벤트, 응답 큐, 시퀀스 번호로 이벤트 큐 + 상태 기계 태스크를 설계할 수 있다
- [ ] `xQueueGenericSend()`의 블록 경로(스케줄러 정지 + 큐 잠금 + 이벤트 리스트 + 재시도)와 `cRxLock`·`cTxLock`의 존재 이유를 설명할 수 있다
- [ ] Zephyr `k_msgq`의 단일 `wait_q`와 직접 전달이 FreeRTOS와 어떤 동작 차이(복사 횟수, 대기 sender 순서)를 만드는지 말할 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `queue.c`(`Queue_t`, `xQueueGenericSend`, `xQueueGenericSendFromISR`, `xQueueReceive`, `xQueuePeek`, `prvCopyDataToQueue`, `prvCopyDataFromQueue`, `prvUnlockQueue`, `prvNotifyQueueSetContainer`, `xQueueAddToSet`, `xQueueSelectFromSet`), `include/queue.h`(큐 셋 Note 1~4, `uxEventQueueLength` 설명), `tasks.c`(`xTaskRemoveFromEventList`, `vTaskPlaceOnEventList`, `xYieldPendings`), `portable/GCC/ARM_CM3/port.c`(`vPortEnterCritical`의 ISR assert), `include/FreeRTOS.h`(`StaticQueue_t`) — 랩 `/var/rtoslab/src/FreeRTOS-Kernel`
- FreeRTOS 문서: [Queue sets](https://www.freertos.org/Documentation/02-Kernel/04-API-references/07-Queue-sets/00-RTOS-queue-sets) (헤더 주석이 가리키는 페이지), "Embedded RTOS Queues"(값 복사 근거, `queue.c` 주석이 가리키는 `https://www.FreeRTOS.org/Embedded-RTOS-Queues.html`)
- Richard Barry, "Mastering the FreeRTOS Real Time Kernel — A Hands-On Tutorial Guide" — Queue Management 장(장 번호는 판마다 달라 생략)
- Zephyr v4.4.2 소스: `kernel/msg_q.c`(`put_msg_in_queue`, `z_impl_k_msgq_get`, `z_impl_k_msgq_purge`, `handle_poll_events`), `include/zephyr/kernel.h`(`struct k_msgq`, `K_MSGQ_DEFINE`, `k_msgq_put_front`의 "does not block" 주석, `K_POLL_TYPE_MSGQ_DATA_AVAILABLE`), `kernel/sched.c`(`z_reschedule`, `resched`), `arch/arm/core/cortex_m/exc_exit.c`(`z_arm_exc_exit`의 PendSV) — 랩 `/var/rtoslab/zephyrproject/zephyr`
- Zephyr 문서: [Message Queues](https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html), [Polling API](https://docs.zephyrproject.org/latest/kernel/services/polling.html)
- J. D. C. Little, "A Proof for the Queuing Formula: L = λW", Operations Research 9(3), 1961
- NVM Express Base Specification — Submission/Completion Queue 엔트리 형식(SQE 64 B, CQE 16 B, Command Identifier, Phase Tag)
- 관련 노트: A5(API 로제타), C1(이벤트 리스트·pending ready 리스트), C5(tick과 `pdMS_TO_TICKS`), C7(크리티컬 섹션), D1(세마포어 = 크기 0 큐), D5(task notification · stream/message buffer · `k_poll`), D8(SPSC 링), D10(Zephyr 데이터 전달 객체 전반), E1(ISR 규칙), EL A7 §4(POSIX message queue)
