# C1. 커널 자료구조 — 리스트 몇 개와 비트맵 하나로 "다음은 누구?"에 답한다

> **이 노트를 다 읽으면**: FreeRTOS `list.c`의 `List_t`·`ListItem_t`·`MiniListItem_t`를 바이트 오프셋까지 그리고 네 함수(`vListInitialise`·`vListInsertEnd`·`vListInsert`·`uxListRemove`)를 줄 단위로 설명할 수 있다 · 커널 전역 리스트(ready 배열, delayed 2개, pending, suspended, 삭제 대기)와 큐의 이벤트 리스트를 **실행 중인 QEMU에서 직접 덤프**해 읽을 수 있다 · 최고 우선순위 선택이 generic(위에서부터 훑기)과 port-optimised(비트맵 + `clz`)에서 어떻게 다르고 각각 얼마나 드는지 측정 숫자로 말할 수 있다 · FreeRTOS와 Zephyr(SIMPLE·SCALABLE·MULTIQ, timeout delta 리스트)에서 O(1)과 O(n) 연산이 어디에 숨어 있는지 표로 정리하고 결정성(determinism)에 미치는 영향을 설명할 수 있다
> **왜 필요한가**: RTOS 면접 심화 질문 1순위가 "Is the scheduler decision O(1)? How does FreeRTOS find the highest-priority ready task?"이고, 현장에서는 `vListInsert()` 안 무한 루프(리스트 손상), 태스크가 많아질수록 늘어나는 tick ISR 길이 같은 버그가 전부 이 자료구조에서 나온다.
> **Don 기준 난이도**: 이중 연결 리스트·포인터·비트맵·find-first-set은 이미 손에 익었다 / 센티널(끝 표시) 항목이 리스트 구조체 **안에** 박혀 있는 설계, TCB 하나가 두 리스트에 동시에 걸리는 방식, tick wrap 대비 리스트 두 개, "우선순위를 값으로 뒤집어 정렬"하는 요령, 어떤 O(n)이 인터럽트를 막은 채로 도는지를 새로 배운다
> **선행 노트**: A3(RTOS 구성 요소), A4(태스크 모델 — TCB 필드와 "상태 = 리스트 소속"은 거기서 다 했다). Linux CFS·EEVDF의 rbtree 런큐는 EL F1(참고만). 이어지는 노트: C2(컨텍스트 스위치), C4(스케줄링 정책), C5(tick과 시간 — delayed 리스트를 **언제** 쓰는지), C7(크리티컬 섹션), C12(소스 투어), D3(우선순위 상속)

---

## 0. 큰 그림 — 스케줄러는 "리스트 관리자"다

RTOS 스케줄러가 매번 하는 질문은 하나다: **"지금 Ready인 태스크 중 우선순위가 가장 높은 것은 누구인가?"** 이 질문에 빨리, 그리고 **항상 같은 시간 안에** 답하려고 커널은 몇 개의 리스트와 비트맵 하나를 들고 있다. 태스크가 블록되면 이 리스트에서 저 리스트로 옮기고, 깨어나면 다시 옮기고, 스위치할 때는 맨 앞을 집는다. 그게 전부다. 그래서 RTOS 커널 소스를 읽는 첫 관문이 `list.c`(FreeRTOS에서 248줄)다.

Don의 SSD 펌웨어로 바꿔 보면 익숙한 모양이다. SSD 컨트롤러 펌웨어는 보통 malloc 없이 **고정 SRAM에 미리 잡아 둔 명령 슬롯**을 쓰고, 슬롯 구조체 안에 `next`/`prev` 포인터를 박아 두어 "호스트 명령 대기 큐", "NAND 다이별 큐", "완료 큐" 사이를 옮긴다(슬롯 복사 없이 포인터만 바꾼다). "어느 다이 큐에 일이 있나"를 비트맵으로 들고 있다가 find-first-set 한 번으로 고르는 것도 흔한 기법이다. RTOS 커널이 정확히 이 일을 한다 — 슬롯 대신 **TCB**, 큐 대신 **ready/delayed/event 리스트**, 다이 비트맵 대신 **우선순위 비트맵**이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">커널은 리스트 몇 개로 모든 태스크의 상태를 표현한다 (FreeRTOS)</text>
<text x="80" y="46" font-size="12" text-anchor="middle" font-weight="bold">pxReadyTasksLists[ ]</text>
<rect x="20" y="54" width="120" height="20" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="28" y="69" font-size="12" text-anchor="start">[7]</text>
<rect x="20" y="78" width="120" height="20" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="28" y="93" font-size="12" text-anchor="start">[6]</text>
<text x="66" y="93" font-size="12" text-anchor="start">MON</text>
<rect x="20" y="102" width="120" height="20" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="28" y="117" font-size="12" text-anchor="start">[5]</text>
<rect x="20" y="126" width="120" height="20" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="28" y="141" font-size="12" text-anchor="start">[4]</text>
<rect x="20" y="150" width="120" height="20" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="28" y="165" font-size="12" text-anchor="start">[3]</text>
<rect x="20" y="174" width="120" height="20" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="28" y="189" font-size="12" text-anchor="start">[2]</text>
<rect x="20" y="198" width="120" height="20" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="28" y="213" font-size="12" text-anchor="start">[1]</text>
<text x="66" y="213" font-size="12" text-anchor="start">W5</text>
<rect x="20" y="222" width="120" height="20" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="28" y="237" font-size="12" text-anchor="start">[0]</text>
<text x="66" y="237" font-size="12" text-anchor="start">IDLE</text>
<text x="80" y="266" font-size="12" text-anchor="middle">비트맵 0x43</text>
<text x="80" y="284" font-size="12" text-anchor="middle">= bit 6, 1, 0</text>
<text x="200" y="60" font-size="12" text-anchor="start" font-weight="bold">pxDelayedTaskList → xDelayedTaskList1 (깨울 tick 순 정렬)</text>
<rect x="200" y="66" width="72.5" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="236.25" y="81" font-size="12" text-anchor="middle">W2 v=30</text>
<rect x="288.5" y="66" width="72.5" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="324.75" y="81" font-size="12" text-anchor="middle">W1 v=50</text>
<line x1="274.5" y1="77" x2="287.5" y2="77" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="377" y="66" width="80" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="417" y="81" font-size="12" text-anchor="middle">W4 v=200</text>
<line x1="363" y1="77" x2="376" y2="77" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="200" y="124" font-size="12" text-anchor="start" font-weight="bold">xSuspendedTaskList (vTaskSuspend + 무한 대기)</text>
<rect x="200" y="130" width="72.5" height="22" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="236.25" y="145" font-size="12" text-anchor="middle">Tmr Svc</text>
<rect x="288.5" y="130" width="70" height="22" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="323.5" y="145" font-size="12" text-anchor="middle">W3</text>
<line x1="274.5" y1="141" x2="287.5" y2="141" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="200" y="188" font-size="12" text-anchor="start" font-weight="bold">xPendingReadyList (스케줄러 잠김 중 깨어남)</text>
<rect x="200" y="194" width="70" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="235" y="209" font-size="12" text-anchor="middle">W3 v=6</text>
<text x="200" y="252" font-size="12" text-anchor="start" font-weight="bold">큐 q의 xTasksWaitingToReceive (우선순위 순)</text>
<rect x="200" y="258" width="70" height="22" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="235" y="273" font-size="12" text-anchor="middle">W4 v=4</text>
<rect x="286" y="258" width="70" height="22" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="321" y="273" font-size="12" text-anchor="middle">W3 v=6</text>
<line x1="272" y1="269" x2="285" y2="269" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="200" y="314" font-size="12" text-anchor="start">v = xItemValue · 위 3줄은 xStateListItem, 맨 아래는 xEventListItem</text>
</svg>
```

그림 1 — ex3(예제 3)의 덤프 1을 그대로 옮긴 커널 리스트 지도. 왼쪽은 우선순위별 ready 리스트 배열과 "비어 있지 않은 우선순위" 비트맵, 오른쪽은 블록된 태스크가 걸리는 리스트들. pending 줄은 같은 예제의 덤프 3, 큐 줄은 §2.5의 규칙(값 = 8 − 우선순위)으로 채웠다.

이 노트가 답하는 질문은 세 가지다.

1. **리스트 하나는 어떻게 생겼나** — 항목 구조, 끝 표시(sentinel), 정렬 삽입과 끝 삽입(§1)
2. **커널은 리스트를 몇 개, 무엇에 쓰나** — 실제 QEMU에서 살아 있는 리스트를 덤프해서 본다(§2)
3. **"다음은 누구?"에 드는 비용은 얼마이고 어디가 O(n)인가** — generic vs CLZ, 정렬 삽입, tick ISR을 측정(§3, §4), Zephyr의 세 가지 ready queue와 비교(§5)

### 0.1 이 노트의 지도

| 절 | 내용 | 예제 |
|---|---|---|
| §1 | `list.c` 해부 — 구조체 레이아웃, 네 함수, round-robin 인덱스 | 예제 1 (gdb 레이아웃), 예제 2 (리스트 단독 실험) |
| §2 | 커널 전역 리스트와 이벤트 리스트 — 살아 있는 덤프 | 예제 3 (C로 덤프), 예제 4 (gdb Python 덤프) |
| §3 | 최고 우선순위 선택 — generic vs 비트맵 + `clz` | 예제 5 (역어셈블), 예제 6 (선택 비용 측정) |
| §4 | 결정성 — O(1)과 O(n)의 위치, 정렬 삽입·tick ISR 측정 | 예제 7, 예제 8 |
| §5 | Zephyr — `_kernel`, SIMPLE/SCALABLE/MULTIQ, wait_q, timeout delta 리스트 | 예제 9 (덤프), 예제 10 (전환 비용 측정) |
| §6 | 커널 안 호출 경로 — API 한 번이 어떤 리스트 연산이 되나 | — |

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩 `/var/rtoslab`: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위의 **FreeRTOS V11.3.2**(ARM_CM3 포트, heap_4, 템플릿 `FreeRTOSConfig.h`: 1 kHz tick, 우선순위 8단계, `configUSE_PORT_OPTIMISED_TASK_SELECTION 1`)와 **Zephyr v4.4.2**(같은 보드 `mps2/an385`). 시간 측정은 전부 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자)에 MPS2 FPGAIO 카운터(0x40028018, 25 MHz → 1 cycle = 40 ns = 명령 1.25개)로 쟀다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스·예외 스태킹 사이클을 모델링하지 않는다). 그러나 이 노트가 보려는 것은 **N에 따른 기울기**(항목 하나당 명령 몇 개)이고, 그건 역어셈블한 루프의 명령 수와 정확히 맞아떨어진다(§3.4, §4.2). 모든 예제는 `-Wall -Wextra` 경고 0개다.

```sh
# FreeRTOS 예제는 이 스크립트로 빌드·실행했다 (~/.rtos_scratch/C1/run.sh)
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C1/$N.c OUT=/var/tmp/rtos/C1/$N "$@" && \
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C1/$N/app.elf 30; echo "exit=$?"
```

---

## 1. 리스트 하나 해부 — `list.c`

### 1.1 직관 — "끼워 넣는" 리스트와 리스트 안의 끝 표시

교과서의 연결 리스트는 노드를 따로 malloc해서 데이터 포인터를 담는다. 커널은 그렇게 하지 않는다. **리스트 항목(`ListItem_t`)을 TCB 구조체 안에 박아 두고**(intrusive list), 항목에서 주인(TCB)으로 돌아가는 포인터 `pvOwner`를 둔다. 그래서 태스크를 리스트에 넣거나 뺄 때 메모리 할당이 전혀 없고, 포인터 네 개만 고친다. Don이 SSD 명령 슬롯에 `next`/`prev`를 박아 두던 것과 같다. Linux 커널의 `struct list_head` + `container_of`도 같은 발상인데, FreeRTOS는 `container_of` 계산 대신 `pvOwner` 포인터 한 칸(4 B)을 써서 더 단순하게 간다.

두 번째 특징은 **끝 표시 항목(sentinel)** 이 리스트 구조체 `List_t` **안에** 들어 있다는 점이다. 리스트는 원형(circular)이고, 끝 표시 `xListEnd`는 값이 `portMAX_DELAY`(32비트 tick이면 0xffffffff)로 고정된 가짜 항목이다. 그래서

- 빈 리스트도 "END → END"로 닫혀 있어서 NULL 검사가 필요 없다.
- 정렬 삽입 루프는 "다음 항목 값이 내 값보다 크면 멈춘다"만 검사하면 된다 — END의 값이 최댓값이라 반드시 거기서 멈춘다.
- 맨 끝(=END의 바로 앞)이 `xListEnd.pxPrevious`로 바로 보이므로 끝 삽입이 O(1)이다.

세 번째 특징은 **역방향 포인터 두 개**다. 항목은 자기가 어느 리스트에 있는지(`pxContainer`)를 알고, 리스트는 자기 항목 수(`uxNumberOfItems`)를 센다. 덕분에 "이 태스크를 지금 있는 리스트에서 빼라"(`uxListRemove`)가 리스트를 몰라도 O(1)이고, "이 우선순위 ready 리스트가 비었나"가 O(1)이다.

### 1.2 정의 — 소스에서 본 세 구조체

`include/list.h`(V11.3.2)의 정의를 무결성 검사 매크로를 빼고 옮기면:

```c
struct xLIST_ITEM {
    configLIST_VOLATILE TickType_t xItemValue;            /* 정렬 키 (대개 오름차순) */
    struct xLIST_ITEM * configLIST_VOLATILE pxNext;
    struct xLIST_ITEM * configLIST_VOLATILE pxPrevious;
    void * pvOwner;                                       /* 이 항목을 품은 객체 (보통 TCB) */
    struct xLIST * configLIST_VOLATILE pxContainer;       /* 지금 들어 있는 리스트 (없으면 NULL) */
};
typedef struct xLIST_ITEM ListItem_t;

struct xMINI_LIST_ITEM {                                  /* configUSE_MINI_LIST_ITEM == 1 일 때 */
    configLIST_VOLATILE TickType_t xItemValue;
    struct xLIST_ITEM * configLIST_VOLATILE pxNext;
    struct xLIST_ITEM * configLIST_VOLATILE pxPrevious;
};
typedef struct xMINI_LIST_ITEM MiniListItem_t;

typedef struct xLIST {
    configLIST_VOLATILE UBaseType_t uxNumberOfItems;
    ListItem_t * configLIST_VOLATILE pxIndex;             /* round-robin 순회 위치 */
    MiniListItem_t xListEnd;                              /* 끝 표시 — 리스트 안에 내장 */
} List_t;
```

| 필드 | 크기 (CM3) | 누가 쓰나 | 한 줄 설명 |
|---|---|---|---|
| `xItemValue` | 4 B (`TickType_t`) | `vListInsert`의 정렬 키 | delayed 리스트에선 깨울 tick, 이벤트 리스트에선 `configMAX_PRIORITIES − 우선순위` (§2.5) |
| `pxNext`, `pxPrevious` | 4 + 4 B | 모든 연산 | 원형 이중 연결 |
| `pvOwner` | 4 B | `listGET_OWNER_OF_...` | 항목 → TCB. 덤프할 때 이름을 찾는 길 |
| `pxContainer` | 4 B | `uxListRemove`, `eTaskGetState` | 항목 → 리스트. NULL이면 어디에도 없음 |
| `uxNumberOfItems` | 4 B | `listLIST_IS_EMPTY` | 비었는지 O(1) |
| `pxIndex` | 4 B | `listGET_OWNER_OF_NEXT_ENTRY` | 같은 우선순위끼리 돌아가며 고르는 커서 (§3.5) |
| `xListEnd` | 12 B (`MiniListItem_t`) | 모두 | 값 = `portMAX_DELAY`인 끝 표시. owner·container가 필요 없어 8 B 아낀다 |

설정 세 개가 이 모양을 바꾼다. `configUSE_MINI_LIST_ITEM`(기본 1 — `FreeRTOS.h`에서 `#ifndef`로 1)이 0이면 끝 표시도 20 B짜리 `ListItem_t`가 된다(MISRA·엄격한 aliasing 규칙 때문에 끄는 경우가 있다). `configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES`가 1이면 항목과 리스트 앞뒤에 알려진 값(`pdINTEGRITY_CHECK_VALUE`)을 넣고 `listTEST_LIST_INTEGRITY()`가 `configASSERT`로 검사한다 — 메모리 덮어쓰기를 잡는 카나리다. `configLIST_VOLATILE`은 기본이 빈 매크로인데, 링크 타임 최적화(LTO)처럼 공격적인 최적화를 쓸 때 `volatile`로 정의하라고 열어 둔 구멍이다.

### 1.3 예제 1 — gdb `ptype /o`로 실제 레이아웃과 전역 리스트 주소 보기

**무엇을 확인하나**: 이 랩 설정으로 컴파일된 세 구조체의 실제 크기·오프셋, 그리고 `tasks.c`의 전역 리스트들이 RAM 어디에 몇 바이트로 놓였는지. 예제 3의 ELF(정적 한정자를 뗀 빌드)를 그대로 쓰고, 마지막 두 줄은 예제 6의 `configMAX_PRIORITIES` 4·64 빌드에서 ready 배열 크기를 비교한다.

```sh
E=/var/tmp/rtos/C1/ex3/app.elf
gdb -batch -ex "ptype /o ListItem_t" -ex "ptype /o MiniListItem_t" -ex "ptype /o List_t" $E
arm-none-eabi-nm -S -n $E | grep -E " (pxReadyTasksLists|xDelayedTaskList1|xDelayedTaskList2|pxDelayedTaskList|pxOverflowDelayedTaskList|xPendingReadyList|xSuspendedTaskList|xTasksWaitingTermination|uxTopReadyPriority|xNextTaskUnblockTime|pxCurrentTCB)$"
for p in 4 64; do printf "MAXPRI=%-3s " $p; arm-none-eabi-nm -S /var/tmp/rtos/C1/sel_o0_p$p/app.elf | grep " pxReadyTasksLists$"; done
```

```text
type = struct xLIST_ITEM {
/*      0      |       4 */    TickType_t xItemValue;
/*      4      |       4 */    struct xLIST_ITEM *pxNext;
/*      8      |       4 */    struct xLIST_ITEM *pxPrevious;
/*     12      |       4 */    void *pvOwner;
/*     16      |       4 */    struct xLIST *pvContainer;

                               /* total size (bytes):   20 */
                             }
type = struct xMINI_LIST_ITEM {
/*      0      |       4 */    TickType_t xItemValue;
/*      4      |       4 */    struct xLIST_ITEM *pxNext;
/*      8      |       4 */    struct xLIST_ITEM *pxPrevious;

                               /* total size (bytes):   12 */
                             }
type = struct xLIST {
/*      0      |       4 */    UBaseType_t uxNumberOfItems;
/*      4      |       4 */    ListItem_t *pxIndex;
/*      8      |      12 */    MiniListItem_t xListEnd;

                               /* total size (bytes):   20 */
                             }
200012b8 00000004 B xNextTaskUnblockTime
200012d0 00000004 B uxTopReadyPriority
200012dc 00000014 B xSuspendedTaskList
200012f4 00000014 B xTasksWaitingTermination
20001308 00000014 B xPendingReadyList
2000131c 00000004 B pxOverflowDelayedTaskList
20001320 00000004 B pxDelayedTaskList
20001324 00000014 B xDelayedTaskList2
20001338 00000014 B xDelayedTaskList1
2000134c 000000a0 B pxReadyTasksLists
200013ec 00000004 B pxCurrentTCB
MAXPRI=4   2000057c 00000050 b pxReadyTasksLists
MAXPRI=64  2000057c 00000500 b pxReadyTasksLists
```

**출력에서 볼 것**: `ListItem_t` 20 B, `MiniListItem_t` 12 B, `List_t` 20 B(4 + 4 + 12). 소스는 `pxContainer`인데 gdb는 **`pvContainer`** 라고 보여 준다 — `FreeRTOS.h`의 하위 호환 블록(`configENABLE_BACKWARD_COMPATIBILITY`, 기본 1)에 `#define pxContainer pvContainer`가 있어서 컴파일러가 실제로 본 이름은 `pvContainer`다. 디버거 스크립트(예제 4)를 쓸 때 이 이름을 써야 한다. 전역 리스트는 하나에 20 B(`0x14`)이고, ready 배열은 `configMAX_PRIORITIES × 20` B다: 8단계 = `0xa0`(160 B), 4단계 = `0x50`(80 B), 64단계 = `0x500`(1280 B). 우선순위를 "넉넉하게 32개"로 잡으면 ready 배열만 640 B — 작은 MCU에서는 무시 못 할 RAM이다. 또 하나: 이 빌드의 심볼 타입이 대문자 `B`(전역)인 것은 `-DportREMOVE_STATIC_QUALIFIER`로 빌드했기 때문이다. 보통 빌드(아래 두 줄)는 소문자 `b`(파일 static)다(§2.3).

### 1.4 예제 2 — `list.c`만 떼어서 돌려 보기

**무엇을 확인하나**: 스케줄러 없이 `List_t` 두 개를 직접 만들어서 (1) `vListInsert`의 정렬 삽입과 같은 값의 순서, (2) `vListInsertEnd` + `listGET_OWNER_OF_NEXT_ENTRY`의 round-robin, (3) `pxIndex`가 가리키는 항목을 지울 때 인덱스가 어떻게 되는지를 주소와 함께 찍는다. 항목의 owner로는 TCB 대신 이름 문자열을 넣었다.

```c
/* ex1: FreeRTOS list.c 단독 실험 — 정렬 삽입, 끝 삽입, pxIndex 라운드로빈, 제거 */
#include "FreeRTOS.h"
#include "list.h"
#include "board.h"
static List_t S, R;                       /* S: 정렬 리스트, R: 라운드로빈 리스트 */
static ListItem_t it[6];
static const char *nm[6] = { "A", "B", "C", "D", "E", "F" };
static const char *who(const ListItem_t *p, const List_t *l) {
    if ((const void *)p == (const void *)&l->xListEnd) return "END";
    return (const char *)p->pvOwner;
}
static void show(const char *tag, const List_t *l) {
    board_printf_raw("%s: n=%u idx=%s  END@%x", tag, (unsigned)l->uxNumberOfItems,
                     who(l->pxIndex, l), (unsigned)&l->xListEnd);
    const ListItem_t *p = l->xListEnd.pxNext;
    while ((const void *)p != (const void *)&l->xListEnd) {
        board_printf_raw(" -> %s@%x(v=%u)", who(p, l), (unsigned)p, (unsigned)p->xItemValue);
        p = p->pxNext;
    }
    board_printf_raw(" -> END\n");
}
int main(void) {
    board_printf_raw("sizeof ListItem_t=%u MiniListItem_t=%u List_t=%u\n",
        (unsigned)sizeof(ListItem_t), (unsigned)sizeof(MiniListItem_t), (unsigned)sizeof(List_t));
    for (int i = 0; i < 6; i++) { vListInitialiseItem(&it[i]); listSET_LIST_ITEM_OWNER(&it[i], nm[i]); }
    vListInitialise(&S);
    board_printf_raw("empty: END.v=%x END.next=%x END.prev=%x\n", (unsigned)S.xListEnd.xItemValue,
        (unsigned)S.xListEnd.pxNext, (unsigned)S.xListEnd.pxPrevious);
    const TickType_t v[4] = { 30, 10, 20, 20 };          /* D(20)는 같은 값 C(20) 뒤로 간다 */
    for (int i = 0; i < 4; i++) { listSET_LIST_ITEM_VALUE(&it[i], v[i]); vListInsert(&S, &it[i]); show("vListInsert", &S); }
    board_printf_raw("head owner = %s, head value = %u\n",
        (const char *)listGET_OWNER_OF_HEAD_ENTRY(&S), (unsigned)listGET_ITEM_VALUE_OF_HEAD_ENTRY(&S));
    for (int i = 0; i < 4; i++) uxListRemove(&it[i]);
    vListInitialise(&R);
    for (int i = 0; i < 3; i++) { vListInsertEnd(&R, &it[i]); }
    show("InsertEnd A,B,C", &R);
    void *o;
    board_printf_raw("NEXT_ENTRY x5:");
    for (int k = 0; k < 5; k++) { listGET_OWNER_OF_NEXT_ENTRY(o, &R); board_printf_raw(" %s", (const char *)o); }
    board_printf_raw("   (idx=%s)\n", who(R.pxIndex, &R));
    vListInsertEnd(&R, &it[3]);
    show("InsertEnd D", &R);
    board_printf_raw("NEXT_ENTRY x4:");
    for (int k = 0; k < 4; k++) { listGET_OWNER_OF_NEXT_ENTRY(o, &R); board_printf_raw(" %s", (const char *)o); }
    board_printf_raw("\n");
    ListItem_t *victim = R.pxIndex;                      /* idx가 가리키는 항목 제거 */
    UBaseType_t left = uxListRemove(victim);
    board_printf_raw("removed %s, left=%u, its container now=%x\n", (const char *)victim->pvOwner,
                     (unsigned)left, (unsigned)victim->pxContainer);
    show("after remove", &R);
    board_exit(0);
}
```

```text
sizeof ListItem_t=20 MiniListItem_t=12 List_t=20
empty: END.v=ffffffff END.next=200000f4 END.prev=200000f4
vListInsert: n=1 idx=END  END@200000f4 -> A@20000060(v=30) -> END
vListInsert: n=2 idx=END  END@200000f4 -> B@20000074(v=10) -> A@20000060(v=30) -> END
vListInsert: n=3 idx=END  END@200000f4 -> B@20000074(v=10) -> C@20000088(v=20) -> A@20000060(v=30) -> END
vListInsert: n=4 idx=END  END@200000f4 -> B@20000074(v=10) -> C@20000088(v=20) -> D@2000009c(v=20) -> A@20000060(v=30) -> END
head owner = B, head value = 10
InsertEnd A,B,C: n=3 idx=END  END@200000e0 -> A@20000060(v=30) -> B@20000074(v=10) -> C@20000088(v=20) -> END
NEXT_ENTRY x5: A B C A B   (idx=B)
InsertEnd D: n=4 idx=B  END@200000e0 -> A@20000060(v=30) -> D@2000009c(v=20) -> B@20000074(v=10) -> C@20000088(v=20) -> END
NEXT_ENTRY x4: C A D B
removed B, left=3, its container now=0
after remove: n=3 idx=D  END@200000e0 -> A@20000060(v=30) -> D@2000009c(v=20) -> C@20000088(v=20) -> END
[board_exit] code=0
exit=0
```

**출력에서 볼 것**: 빈 리스트는 END의 next/prev가 자기 자신(0x200000f4)이다. 30, 10, 20, 20 순서로 넣으면 B(10) → C(20) → D(20) → A(30)로 정렬되고, **같은 값 20은 먼저 들어온 C가 앞**이다(FIFO). round-robin 리스트에서 `NEXT_ENTRY`는 A B C A B로 돌고, 인덱스가 B에 있을 때 D를 끝 삽입하면 D는 리스트의 물리적 끝이 아니라 **인덱스(B) 바로 앞**에 들어가서 C → A → D → B 순서로 나온다 — "지금 바퀴의 맨 마지막 차례"에 끼는 것이다. 인덱스가 가리키던 B를 지우면 `pxIndex`는 B의 이전 항목 D로 물러나고, 지워진 항목의 `pxContainer`는 0이 된다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">vListInsert 4번 뒤의 List_t (ex1 실제 주소, 앞 0x2000_00 생략)</text>
<rect x="16" y="36" width="138" height="186" rx="4" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="24" y="54" font-size="12" text-anchor="start" font-weight="bold">List_t S @ec (20 B)</text>
<text x="24" y="74" font-size="12" text-anchor="start">uxNumberOfItems=4</text>
<text x="24" y="94" font-size="12" text-anchor="start">pxIndex = &amp;END</text>
<text x="24" y="114" font-size="12" text-anchor="start">xListEnd (내장) ↓</text>
<rect x="26" y="150" width="118" height="62" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="85" y="168" font-size="12" text-anchor="middle" font-weight="bold">xListEnd @f4</text>
<text x="85" y="186" font-size="12" text-anchor="middle">v=ffffffff</text>
<text x="85" y="204" font-size="12" text-anchor="middle">MiniListItem</text>
<rect x="180" y="150" width="100" height="62" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="230" y="168" font-size="12" text-anchor="middle" font-weight="bold">item B @74</text>
<text x="230" y="186" font-size="12" text-anchor="middle">v=10</text>
<text x="230" y="204" font-size="12" text-anchor="middle">owner=B</text>
<rect x="305" y="150" width="100" height="62" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="355" y="168" font-size="12" text-anchor="middle" font-weight="bold">item C @88</text>
<text x="355" y="186" font-size="12" text-anchor="middle">v=20</text>
<text x="355" y="204" font-size="12" text-anchor="middle">owner=C</text>
<rect x="430" y="150" width="100" height="62" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="480" y="168" font-size="12" text-anchor="middle" font-weight="bold">item D @9c</text>
<text x="480" y="186" font-size="12" text-anchor="middle">v=20</text>
<text x="480" y="204" font-size="12" text-anchor="middle">owner=D</text>
<rect x="555" y="150" width="100" height="62" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="605" y="168" font-size="12" text-anchor="middle" font-weight="bold">item A @60</text>
<text x="605" y="186" font-size="12" text-anchor="middle">v=30</text>
<text x="605" y="204" font-size="12" text-anchor="middle">owner=A</text>
<line x1="144" y1="160" x2="178" y2="160" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="180" y1="202" x2="146" y2="202" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="280" y1="160" x2="303" y2="160" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="305" y1="202" x2="282" y2="202" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="405" y1="160" x2="428" y2="160" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="430" y1="202" x2="407" y2="202" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="530" y1="160" x2="553" y2="160" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="555" y1="202" x2="532" y2="202" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<path d="M 625 150 L 625 136 L 116 136 L 116 148" fill="none" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="400" y="130" font-size="12" text-anchor="middle">pxNext: 마지막 항목 → 다시 END (원형)</text>
<path d="M 66 212 L 66 240 L 585 240 L 585 214" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<text x="340" y="258" font-size="12" text-anchor="middle">pxPrevious (점선): END의 이전 = 마지막 항목 → 끝 삽입이 O(1)</text>
<text x="180" y="66" font-size="12" text-anchor="start">같은 값 20: 나중에 넣은 D가 C 뒤 (&lt;= 비교 → FIFO)</text>
<text x="180" y="88" font-size="12" text-anchor="start">END.v = portMAX_DELAY(0xffffffff) → 정렬 순회는 END에서 반드시 멈춘다</text>
</svg>
```

그림 2 — 예제 2에서 네 번 정렬 삽입한 뒤의 리스트 `S`(주소는 출력 그대로, 앞의 0x2000_00 생략). 끝 표시 `xListEnd`는 `List_t` 안(@ec + 8 = @f4)에 박혀 있고, 실선이 `pxNext`, 점선이 `pxPrevious`다.

### 1.5 네 함수를 줄 단위로

`list.c`는 함수 다섯 개(초기화 두 개 + 삽입 두 개 + 제거 한 개)가 전부다. 트레이스 매크로를 빼면 이렇다.

**`vListInitialise(pxList)`** — `pxIndex`와 END의 next/prev를 전부 END 자신으로, END의 값을 `portMAX_DELAY`로, 항목 수를 0으로. 리스트가 "END 하나짜리 원"이 된다. `listLIST_IS_INITIALISED()`는 "END 값이 `portMAX_DELAY`인가"로 초기화 여부를 판단한다.

**`vListInsertEnd(pxList, pxNewListItem)`** — O(1). 새 항목을 `pxIndex` **바로 앞**에 넣는다.

```c
ListItem_t * const pxIndex = pxList->pxIndex;
pxNewListItem->pxNext = pxIndex;
pxNewListItem->pxPrevious = pxIndex->pxPrevious;
pxIndex->pxPrevious->pxNext = pxNewListItem;
pxIndex->pxPrevious = pxNewListItem;
pxNewListItem->pxContainer = pxList;
( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems + 1U );
```

`pxIndex`가 END에 있으면(한 번도 순회하지 않은 리스트) "END 앞 = 물리적 끝"이라 이름 그대로 끝 삽입이다. 순회 중이면 "다음 바퀴에서 가장 늦게 나올 자리"다. ready 리스트에 태스크를 넣는 `prvAddTaskToReadyList()`가 이 함수(정확히는 같은 일을 하는 인라인 매크로 `listINSERT_END`)를 쓴다 — 그래서 막 깨어난 태스크는 같은 우선순위 줄의 **맨 뒤**에 선다.

**`vListInsert(pxList, pxNewListItem)`** — O(n). 값 오름차순 자리를 찾아 넣는다.

```c
const TickType_t xValueOfInsertion = pxNewListItem->xItemValue;
if( xValueOfInsertion == portMAX_DELAY )
    pxIterator = pxList->xListEnd.pxPrevious;            /* 최댓값이면 바로 맨 끝 */
else
    for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd );
         pxIterator->pxNext->xItemValue <= xValueOfInsertion;
         pxIterator = pxIterator->pxNext ) { }           /* 빈 루프: 자리 찾기만 */
pxNewListItem->pxNext = pxIterator->pxNext;
pxNewListItem->pxNext->pxPrevious = pxNewListItem;
pxNewListItem->pxPrevious = pxIterator;
pxIterator->pxNext = pxNewListItem;
pxNewListItem->pxContainer = pxList;
( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems + 1U );
```

두 가지 세부가 중요하다. 첫째, 비교가 `<=`라서 **같은 값은 기존 항목 뒤**로 간다(예제 2의 C, D). 소스 주석도 "같은 값이면 뒤에 넣어야 ready 리스트의 태스크들이 CPU를 나눠 쓴다"고 설명한다. 둘째, 값이 `portMAX_DELAY`면 루프를 건너뛴다. 끝 표시도 같은 값이라 `<=` 루프가 END를 지나쳐 **영원히 돌기** 때문이다.

같은 함수 안에 FreeRTOS에서 가장 유명한 주석이 있다: "If you find your application is crashing here then likely causes are listed below." 원인으로 꼽힌 다섯 가지는 (1) 스택 오버플로, (2) Cortex-M 인터럽트 우선순위 설정 오류(`configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 ISR에서 API 호출), (3) 크리티컬 섹션 안이나 스케줄러 정지 중에 API 호출, 또는 ISR에서 `FromISR`가 아닌 API 호출, (4) 초기화 전 큐·세마포어 사용(스케줄러 시작 전에 인터럽트가 뜨는 경우), (5) 중첩 인터럽트 포트에서 tick 인터럽트 우선순위가 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 경우다. 전부 "**리스트가 동시에 두 곳에서 고쳐져서 원이 깨졌다**"는 한 가지 증상의 다른 원인이다. 원이 깨지면 이 빈 `for` 루프가 END를 영영 못 만나서 CPU가 여기서 멈춘다 — 디버거를 붙였을 때 PC가 `vListInsert`에 있으면 99% 이 경우다(§8).

**`uxListRemove(pxItemToRemove)`** — O(1). 이웃끼리 서로 잇고, 지우는 항목이 `pxIndex`였으면 인덱스를 **이전 항목으로** 물린다(그래야 다음 `NEXT_ENTRY`가 지운 항목의 다음 태스크를 고른다). `pxContainer`를 NULL로 만들고 **남은 항목 수를 반환**한다. 이 반환값이 중요하다 — 0이면 "이 우선순위 ready 리스트가 막 비었다"는 뜻이라 호출자가 비트맵의 해당 비트를 지운다(§3.2).

```c
List_t * const pxList = pxItemToRemove->pxContainer;
pxItemToRemove->pxNext->pxPrevious = pxItemToRemove->pxPrevious;
pxItemToRemove->pxPrevious->pxNext = pxItemToRemove->pxNext;
if( pxList->pxIndex == pxItemToRemove )
    pxList->pxIndex = pxItemToRemove->pxPrevious;
pxItemToRemove->pxContainer = NULL;
( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems - 1U );
return pxList->uxNumberOfItems;
```

`list.h`에는 함수 말고 매크로도 많다. 자주 보이는 것만:

| 매크로 | 하는 일 | 비용 |
|---|---|---|
| `listGET_OWNER_OF_HEAD_ENTRY(pxList)` | 맨 앞 항목의 owner (= 가장 먼저 깰 태스크, 가장 높은 대기자) | O(1) |
| `listGET_ITEM_VALUE_OF_HEAD_ENTRY(pxList)` | 맨 앞 항목의 값 (delayed 리스트에서 "다음 깨울 tick") | O(1) |
| `listGET_OWNER_OF_NEXT_ENTRY(pxTCB, pxList)` | `pxIndex`를 한 칸 전진(END는 건너뜀)시키고 그 owner | O(1) |
| `listLIST_IS_EMPTY`, `listCURRENT_LIST_LENGTH` | 항목 수 비교 | O(1) |
| `listIS_CONTAINED_WITHIN(pxList, pxItem)` | `pxContainer == pxList` | O(1) |
| `listREMOVE_ITEM`, `listINSERT_END` | `uxListRemove`·`vListInsertEnd`의 인라인 버전 (tasks.c가 핫패스에서 씀) | O(1) |

### 1.6 흔한 함정

- **한 항목을 두 리스트에 넣기**: 항목에는 `pxNext`/`pxPrevious`가 한 쌍뿐이다. 빼지 않고 다른 리스트에 넣으면 앞 리스트의 원이 깨진다. 커널이 `prvAddCurrentTaskToDelayedList()`에서 먼저 `uxListRemove(&xStateListItem)`를 부르는 이유("the same list item is used for both lists")다.
- **`portMAX_DELAY` 값의 의미 혼동**: 리스트 입장에선 "가장 큰 값"일 뿐이다. "무한 대기"라는 의미는 `tasks.c`가 붙인다(`INCLUDE_vTaskSuspend == 1`이면 무한 대기 태스크를 delayed 리스트가 아니라 suspended 리스트에 넣는다 — A4 §3.2).
- **리스트를 직접 걷기**: 애플리케이션이 커널 리스트를 걸으면(예제 3처럼) 반드시 크리티컬 섹션 안에서 해야 한다. 걷는 도중 tick ISR이 항목을 옮기면 엉뚱한 리스트로 넘어가 무한 루프가 된다.

---

## 2. 커널 전역 리스트 지도 — `tasks.c`

### 2.1 리스트 목록

`tasks.c` 앞부분(V11.3.2 기준 478~500행 근처)에 커널이 쓰는 리스트가 전부 모여 있다.

```c
PRIVILEGED_DATA STATIC List_t pxReadyTasksLists[ configMAX_PRIORITIES ]; /* 우선순위별 ready */
PRIVILEGED_DATA STATIC List_t xDelayedTaskList1;                         /* delayed (현재/overflow 번갈아) */
PRIVILEGED_DATA STATIC List_t xDelayedTaskList2;
PRIVILEGED_DATA STATIC List_t * volatile pxDelayedTaskList;              /* 지금 쓰는 delayed 리스트 */
PRIVILEGED_DATA STATIC List_t * volatile pxOverflowDelayedTaskList;      /* tick wrap 너머에 깰 태스크 */
PRIVILEGED_DATA STATIC List_t xPendingReadyList;                         /* 스케줄러 정지 중 깨어난 태스크 */
PRIVILEGED_DATA STATIC List_t xTasksWaitingTermination;                  /* 삭제됐지만 메모리 회수 전 */
PRIVILEGED_DATA STATIC List_t xSuspendedTaskList;                        /* suspend + 무한 대기 */
PRIVILEGED_DATA STATIC volatile UBaseType_t uxTopReadyPriority;          /* generic: 최고 우선순위 / 최적화: 비트맵 */
PRIVILEGED_DATA STATIC volatile TickType_t xNextTaskUnblockTime;         /* delayed 리스트 맨 앞의 값 캐시 */
```

| 리스트 | 어떤 항목이 걸리나 | 정렬 키 (`xItemValue`) | 삽입 함수 | 누가 빼나 |
|---|---|---|---|---|
| `pxReadyTasksLists[p]` | `xStateListItem` | 쓰지 않음 (옛 값이 남아 있다) | `listINSERT_END` O(1) | 블록·suspend·삭제 때 `uxListRemove` |
| `*pxDelayedTaskList` | `xStateListItem` | 깨울 tick (절댓값) | `vListInsert` O(n) | tick ISR이 맨 앞부터 |
| `*pxOverflowDelayedTaskList` | `xStateListItem` | 깨울 tick (wrap 너머) | `vListInsert` O(n) | tick wrap 때 포인터 교환 |
| `xSuspendedTaskList` | `xStateListItem` | 쓰지 않음 | `listINSERT_END` O(1) | `vTaskResume`, 이벤트 도착 |
| `xPendingReadyList` | **`xEventListItem`** | 쓰지 않음 | `listINSERT_END` O(1) | `xTaskResumeAll` |
| `xTasksWaitingTermination` | `xStateListItem` | 쓰지 않음 | `listINSERT_END` O(1) | idle 태스크 (C9) |
| 큐의 `xTasksWaitingToReceive` / `ToSend` | `xEventListItem` | `configMAX_PRIORITIES − 우선순위` | `vListInsert` O(n) | `xTaskRemoveFromEventList`가 맨 앞 |

이 표에서 기억할 점 셋. (1) TCB 하나에 리스트 항목이 **두 개**다 — `xStateListItem`은 "상태"(ready/delayed/suspended/삭제 대기) 리스트에, `xEventListItem`은 "무엇을 기다리나"(큐·세마포어·이벤트 그룹의 대기자 리스트, 또는 pending ready 리스트)에 걸린다. timeout 있는 큐 대기는 두 리스트에 **동시에** 걸린다. (2) 정렬 삽입(O(n))은 delayed 리스트와 이벤트 리스트 두 곳뿐이고, ready 리스트는 항상 끝 삽입(O(1))이다. (3) 상태 판정(`eTaskGetState`)과 상태 전이표는 A4 §3.2에서 다 했으니 여기서는 **구조**만 본다.

`xItemValue`의 의미가 리스트마다 다르다는 점도 함정이다. 태스크가 delayed 리스트에서 ready 리스트로 옮겨 가도 `xStateListItem.xItemValue`는 지우지 않는다 — ready 리스트는 끝 삽입이라 값을 안 보기 때문이다. 예제 3의 덤프에서 ready 리스트의 MON이 `v=2`(마지막으로 깨어난 tick), 막 깨어난 W4가 `v=200`(timeout 시각)을 그대로 들고 있는 게 그 흔적이다.

### 2.2 리스트들은 `static`이다 — 밖에서 보는 세 가지 방법

이 리스트들은 `STATIC`(= `static`) 이라 애플리케이션 코드에서 `extern`으로 볼 수 없다. 살아 있는 리스트를 보는 방법은 셋이다.

| 방법 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| `uxTaskGetSystemState()` | `configUSE_TRACE_FACILITY 1`이면 모든 태스크의 `TaskStatus_t` 배열(이름·상태·우선순위·스택 여유)을 준다 | 공식 API, 양산 코드에서도 안전 | 리스트 **순서**·항목 값·주소는 안 보인다 |
| `portREMOVE_STATIC_QUALIFIER` | `tasks.c` 150행: 이 매크로가 정의되면 `#define STATIC`(빈 매크로) → 리스트가 전역 심볼이 된다 | C 코드로 직접 걸을 수 있다 (예제 3) | 디버그 전용. 커널 내부 구조에 묶인다 |
| 디버거 (gdb + DWARF) | static 심볼도 디버그 정보에 있으니 gdb가 읽는다 | 코드 수정 없이, 멈춘 순간 그대로 (예제 4) | 타깃을 멈춰야 한다 |

소스 주석이 `portREMOVE_STATIC_QUALIFIER`의 존재 이유를 직접 말한다 — "kernel aware debuggers"가 이 심볼들을 찾는다. Ozone·SystemView·IDE의 RTOS 플러그인이 정확히 예제 4가 하는 일(정해진 이름의 리스트를 걸으며 `pvOwner` → TCB 이름)을 한다(M2).

### 2.3 예제 3 — 살아 있는 커널 리스트를 C로 덤프하기

**무엇을 확인하나**: `-DportREMOVE_STATIC_QUALIFIER`로 빌드해서 리스트를 `extern`으로 잡고, 감시 태스크 MON(우선순위 6)이 네 순간에 모든 리스트를 주소·값과 함께 찍는다. 태스크 구성: W1·W2(우선순위 3, 각각 `vTaskDelay(50)`·`(30)` 반복), W3(2, 큐를 무한 대기), W4(4, 큐를 200 tick timeout으로 대기), W5(1, 무한 루프 — idle이 못 돌게). 덤프는 (1) 모두 자리 잡은 tick 2, (2) MON이 큐에 하나 넣은 직후(W4가 깸), (3) `vTaskSuspendAll()` 안에서 하나 더 넣은 직후(W3가 깸), (4) `xTaskResumeAll()` 직후. 덤프가 타이밍을 망치지 않도록 크리티컬 섹션 안에서는 문자열 버퍼에 쓰기만 하고 출력은 밖에서 한다.

```c
/* ex3: 커널 전역 리스트를 살아 있는 채로 덤프 (빌드: -DportREMOVE_STATIC_QUALIFIER) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
extern List_t pxReadyTasksLists[configMAX_PRIORITIES], xDelayedTaskList1, xDelayedTaskList2,
              xPendingReadyList, xSuspendedTaskList, xTasksWaitingTermination;
extern List_t * volatile pxDelayedTaskList, * volatile pxOverflowDelayedTaskList;
extern volatile UBaseType_t uxTopReadyPriority;
extern volatile TickType_t xTickCount, xNextTaskUnblockTime;
static QueueHandle_t q;
static char buf[1400]; static int pos;                      /* 크리티컬 안에서 문자열로 스냅샷 */
static void put(const char *s) { while (*s && pos < (int)sizeof buf - 1) buf[pos++] = *s++; }
static void putx(uint32_t v) { char t[11]; int i = 10; t[i] = 0; do { t[--i] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v); put(&t[i]); }
static void putu(uint32_t v) { char t[11]; int i = 10; t[i] = 0; do { t[--i] = '0' + v % 10; v /= 10; } while (v); put(&t[i]); }
static void list(const char *n, List_t *l) {
    if (l->uxNumberOfItems == 0) return;
    put("  "); put(n); put("@"); putx((uint32_t)l); put(" n="); putu(l->uxNumberOfItems); put(":");
    for (ListItem_t *p = l->xListEnd.pxNext; (void *)p != (void *)&l->xListEnd; p = p->pxNext) {
        put(" ["); put(pcTaskGetName(p->pvOwner)); put(" v="); putu(p->xItemValue);
        put(" @"); putx((uint32_t)p); put("]");
    }
    if (l->pxIndex != (ListItem_t *)&l->xListEnd) { put(" idx="); put(pcTaskGetName(l->pxIndex->pvOwner)); }
    put("\n");
}
static void dump(const char *tag) {
    taskENTER_CRITICAL();
    pos = 0; put("== "); put(tag); put(" tick="); putu(xTickCount); put(" top=0x"); putx(uxTopReadyPriority);
    put(" nextUnblock="); putu(xNextTaskUnblockTime);
    put(pxDelayedTaskList == &xDelayedTaskList1 ? " cur=D1\n" : " cur=D2\n");
    static const char *rn[8] = { "ready0", "ready1", "ready2", "ready3", "ready4", "ready5", "ready6", "ready7" };
    for (int i = configMAX_PRIORITIES - 1; i >= 0; i--) list(rn[i], &pxReadyTasksLists[i]);
    list("delayed1", &xDelayedTaskList1); list("delayed2", &xDelayedTaskList2);
    list("pending", &xPendingReadyList); list("suspended", &xSuspendedTaskList);
    list("waitTerm", &xTasksWaitingTermination);
    buf[pos] = 0;
    taskEXIT_CRITICAL();
    board_puts(buf);
}
static void dly(void *pv) { for (;;) vTaskDelay((TickType_t)(uintptr_t)pv); }
static void rx(void *pv) { uint32_t v; for (;;) xQueueReceive(q, &v, (TickType_t)(uintptr_t)pv); }
static void spin(void *pv) { (void)pv; for (;;) { } }
static void mon(void *pv) {
    (void)pv; uint32_t v = 1;
    vTaskDelay(2); dump("t=2 all settled");
    xQueueSend(q, &v, 0); dump("after xQueueSend (W4 woke)");
    vTaskSuspendAll(); xQueueSend(q, &v, 0); dump("scheduler suspended + send");
    xTaskResumeAll(); dump("after xTaskResumeAll");
    board_exit(0);
}
int main(void) {
    q = xQueueCreate(2, sizeof(uint32_t));
    xTaskCreate(mon, "MON", 512, NULL, 6, NULL);
    xTaskCreate(dly, "W1", 128, (void *)50, 3, NULL);
    xTaskCreate(dly, "W2", 128, (void *)30, 3, NULL);
    xTaskCreate(rx, "W3", 128, (void *)portMAX_DELAY, 2, NULL);
    xTaskCreate(rx, "W4", 128, (void *)200, 4, NULL);
    xTaskCreate(spin, "W5", 128, NULL, 1, NULL);
    vTaskStartScheduler();
}
```

```sh
/Users/donh/.rtos_scratch/C1/run.sh ex3 EXTRA_CFLAGS=-DportREMOVE_STATIC_QUALIFIER     # ICOUNT=1
```

```text
== t=2 all settled tick=2 top=0x43 nextUnblock=30 cur=D1
  ready6@200013c4 n=1: [MON v=2 @20001dfc] idx=MON
  ready1@20001360 n=1: [W5 v=0 @20002a2c] idx=W5
  ready0@2000134c n=1: [IDLE v=0 @2000124c]
  delayed1@20001338 n=3: [W2 v=30 @200022dc] [W1 v=50 @2000206c] [W4 v=200 @200027bc]
  suspended@200012dc n=2: [Tmr Svc v=0 @20000dec] [W3 v=0 @2000254c]
== after xQueueSend (W4 woke) tick=2 top=0x53 nextUnblock=30 cur=D1
  ready6@200013c4 n=1: [MON v=2 @20001dfc] idx=MON
  ready4@2000139c n=1: [W4 v=200 @200027bc]
  ready1@20001360 n=1: [W5 v=0 @20002a2c] idx=W5
  ready0@2000134c n=1: [IDLE v=0 @2000124c]
  delayed1@20001338 n=2: [W2 v=30 @200022dc] [W1 v=50 @2000206c]
  suspended@200012dc n=2: [Tmr Svc v=0 @20000dec] [W3 v=0 @2000254c]
== scheduler suspended + send tick=2 top=0x53 nextUnblock=30 cur=D1
  ready6@200013c4 n=1: [MON v=2 @20001dfc] idx=MON
  ready4@2000139c n=1: [W4 v=200 @200027bc]
  ready1@20001360 n=1: [W5 v=0 @20002a2c] idx=W5
  ready0@2000134c n=1: [IDLE v=0 @2000124c]
  delayed1@20001338 n=2: [W2 v=30 @200022dc] [W1 v=50 @2000206c]
  pending@20001308 n=1: [W3 v=6 @20002560]
  suspended@200012dc n=2: [Tmr Svc v=0 @20000dec] [W3 v=0 @2000254c]
== after xTaskResumeAll tick=2 top=0x57 nextUnblock=30 cur=D1
  ready6@200013c4 n=1: [MON v=2 @20001dfc] idx=MON
  ready4@2000139c n=1: [W4 v=200 @200027bc]
  ready2@20001374 n=1: [W3 v=0 @2000254c]
  ready1@20001360 n=1: [W5 v=0 @20002a2c] idx=W5
  ready0@2000134c n=1: [IDLE v=0 @2000124c]
  delayed1@20001338 n=2: [W2 v=30 @200022dc] [W1 v=50 @2000206c]
  suspended@200012dc n=1: [Tmr Svc v=0 @20000dec]
[board_exit] code=0
exit=0
```

**출력에서 볼 것**: 리스트 주소를 예제 1의 `nm` 결과와 맞춰 보면 `ready6` = `pxReadyTasksLists` + 6 × 20 = 0x2000134c + 0x78 = 0x200013c4다. 덤프마다 하나씩 읽자.

| 덤프 | 바뀐 것 | 읽는 법 |
|---|---|---|
| 1 (tick 2) | ready에는 MON(6)·W5(1)·IDLE(0)만, 비트맵 `top=0x43` = 비트 6·1·0 | **Running인 MON도 ready[6]에 그대로 있다**. IDLE 줄에 `idx`가 없는 것은 IDLE이 한 번도 선택되지 않았다는 뜻(W5가 CPU를 쥐고 있다) |
| 1 | delayed1: W2(30) → W1(50) → W4(200) | 깨울 tick 오름차순. `nextUnblock=30`은 맨 앞 값의 캐시 |
| 1 | suspended: Tmr Svc, W3 | `portMAX_DELAY` 대기는 delayed가 아니라 여기 (타이머 태스크도 명령 큐를 무한 대기 중) |
| 2 (send 직후) | W4가 delayed에서 빠져 ready[4]로, `top=0x53` | 이벤트로 깨면 delayed 리스트에서도 빠진다. W4의 `v=200`은 지워지지 않은 옛 timeout 값 |
| 3 (스케줄러 정지 중 send) | **pending에 W3(v=6)**, suspended에도 W3(v=0) | 같은 TCB의 두 항목: 이벤트 항목(0x20002560)은 pending에, 상태 항목(0x2000254c)은 아직 suspended에. `v=6` = 8 − 우선순위 2 |
| 4 (resume 후) | W3가 ready[2]로, pending 비고, `top=0x57` | `xTaskResumeAll()`이 pending을 비우며 상태 항목을 옮겼다 |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">ex3 덤프 3: 스케줄러가 잠긴 동안 깨어난 W3 (실제 주소)</text>
<rect x="20" y="40" width="210" height="170" rx="4" fill="#e08a3c" fill-opacity="0.08" stroke="#e08a3c"/>
<text x="30" y="60" font-size="13" text-anchor="start" font-weight="bold">TCB W3 @20002548</text>
<rect x="30" y="70" width="190" height="22" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="36" y="85" font-size="12" text-anchor="start">+0</text>
<text x="72" y="85" font-size="12" text-anchor="start">pxTopOfStack</text>
<rect x="30" y="96" width="190" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="36" y="111" font-size="12" text-anchor="start">+4</text>
<text x="72" y="111" font-size="12" text-anchor="start">xStateListItem v=0</text>
<rect x="30" y="122" width="190" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="36" y="137" font-size="12" text-anchor="start">+24</text>
<text x="72" y="137" font-size="12" text-anchor="start">xEventListItem v=6</text>
<rect x="30" y="148" width="190" height="22" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="36" y="163" font-size="12" text-anchor="start">+44</text>
<text x="72" y="163" font-size="12" text-anchor="start">uxPriority = 2</text>
<rect x="30" y="174" width="190" height="22" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="36" y="189" font-size="12" text-anchor="start">+52</text>
<text x="72" y="189" font-size="12" text-anchor="start">pcTaskName "W3"</text>
<text x="290" y="60" font-size="12" text-anchor="start" font-weight="bold">xSuspendedTaskList @200012dc (n=2)</text>
<rect x="290" y="70" width="96" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="338" y="86" font-size="12" text-anchor="middle" font-weight="bold">END</text>
<text x="338" y="103" font-size="12" text-anchor="middle">@200012e4</text>
<rect x="400" y="70" width="110" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="455" y="86" font-size="12" text-anchor="middle" font-weight="bold">Tmr Svc</text>
<text x="455" y="103" font-size="12" text-anchor="middle">@20000dec</text>
<rect x="530" y="70" width="110" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="585" y="86" font-size="12" text-anchor="middle" font-weight="bold">W3</text>
<text x="585" y="103" font-size="12" text-anchor="middle">@2000254c</text>
<line x1="386" y1="90" x2="398" y2="90" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<line x1="510" y1="90" x2="528" y2="90" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="290" y="150" font-size="12" text-anchor="start" font-weight="bold">xPendingReadyList @20001308 (n=1)</text>
<rect x="290" y="160" width="96" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="338" y="176" font-size="12" text-anchor="middle" font-weight="bold">END</text>
<text x="338" y="193" font-size="12" text-anchor="middle">@20001310</text>
<rect x="400" y="160" width="110" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="455" y="176" font-size="12" text-anchor="middle" font-weight="bold">W3</text>
<text x="455" y="193" font-size="12" text-anchor="middle">@20002560</text>
<line x1="386" y1="180" x2="398" y2="180" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<path d="M 220 107 C 400 130, 560 140, 585 112" fill="none" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<path d="M 220 133 C 260 230, 400 230, 455 203" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
<text x="20" y="238" font-size="12" text-anchor="start">같은 TCB의 두 갈고리: 상태 항목은 아직 suspended에, 이벤트 항목은 pending에 걸려 있다.</text>
<text x="20" y="258" font-size="12" text-anchor="start">두 항목의 주소 차 0x14 = 20 B = sizeof(ListItem_t). xTaskResumeAll()이 W3을 ready[2]로 옮긴다.</text>
<rect x="20" y="274" width="200" height="44" rx="4" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/>
<text x="120" y="292" font-size="12" text-anchor="middle">덤프 1: 비트맵 = 0x43</text>
<text x="120" y="310" font-size="12" text-anchor="middle">bit 6 1 0</text>
<rect x="240" y="274" width="200" height="44" rx="4" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/>
<text x="340" y="292" font-size="12" text-anchor="middle">W4 깨움: 비트맵 = 0x53</text>
<text x="340" y="310" font-size="12" text-anchor="middle">bit 6 4 1 0</text>
<line x1="222" y1="296" x2="238" y2="296" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<rect x="460" y="274" width="200" height="44" rx="4" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/>
<text x="560" y="292" font-size="12" text-anchor="middle">resume 후: 비트맵 = 0x57</text>
<text x="560" y="310" font-size="12" text-anchor="middle">bit 6 4 2 1 0</text>
<line x1="442" y1="296" x2="458" y2="296" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
</svg>
```

그림 3 — 덤프 3의 W3. 상태 항목과 이벤트 항목의 주소 차가 정확히 20 B(`ListItem_t` 하나)이고 TCB 시작은 0x2000254c − 4 = 0x20002548이다(A4 예제 1의 TCB 레이아웃: `xStateListItem`이 +4, `xEventListItem`이 +24). 아래 줄은 덤프마다 바뀐 비트맵.

### 2.4 왜 pending ready 리스트가 따로 있나

`vTaskSuspendAll()`(스케줄러 정지)은 **인터럽트를 막지 않는다**. 스위치만 막는다. 그래서 정지 구간에도 ISR은 돌고, ISR이 `xQueueSendFromISR()`로 대기자를 깨울 수 있다. 그런데 이때 ISR이 ready 리스트를 직접 고치면 안 된다 — 정지 구간 안의 태스크 코드가 바로 그 리스트들을 크리티컬 섹션 없이 만지는 중일 수 있기 때문이다(그게 스케줄러 정지의 존재 이유다: 긴 리스트 작업을 인터럽트를 막지 않고 하려고). 해법이 pending 리스트다. `xTaskRemoveFromEventList()`를 보면:

```c
pxUnblockedTCB = listGET_OWNER_OF_HEAD_ENTRY( pxEventList );   /* 가장 높은 대기자 */
listREMOVE_ITEM( &( pxUnblockedTCB->xEventListItem ) );
if( uxSchedulerSuspended == ( UBaseType_t ) 0U ) {
    listREMOVE_ITEM( &( pxUnblockedTCB->xStateListItem ) );
    prvAddTaskToReadyList( pxUnblockedTCB );
} else {
    listINSERT_END( &( xPendingReadyList ), &( pxUnblockedTCB->xEventListItem ) );
}
```

정지 중이면 **이벤트 항목만** pending 리스트에 끝 삽입한다(O(1), 상태 리스트는 건드리지 않음). 이벤트 항목은 방금 이벤트 리스트에서 빠져서 비어 있으니 안전하게 재사용할 수 있다. 덤프 3이 정확히 이 순간이다. `xTaskResumeAll()`이 pending을 비우면서 상태 항목을 delayed/suspended에서 빼 ready로 옮긴다.

Don의 펌웨어 감각으로 말하면: "메인 루프가 큐를 재정렬하는 동안 ISR은 큐를 직접 만지지 말고 별도의 도착 버퍼에만 넣어라, 메인 루프가 끝나면 합친다" — 인터럽트를 오래 막지 않으려는 고전적인 패턴이다.

### 2.5 이벤트 리스트 — 왜 값이 `configMAX_PRIORITIES − 우선순위`인가

큐에 대기자가 여럿이면 **가장 높은 우선순위가 먼저** 받아야 한다. 리스트는 값 **오름차순**으로 정렬되고 맨 앞을 꺼내니, 높은 우선순위가 작은 값이 되도록 뒤집으면 된다. 태스크를 만들 때 `prvInitialiseNewTask()`가

```c
listSET_LIST_ITEM_VALUE( &( pxNewTCB->xEventListItem ), ( TickType_t ) configMAX_PRIORITIES - ( TickType_t ) uxPriority );
```

로 값을 정해 두고, 블록할 때 `vTaskPlaceOnEventList()`가 `vListInsert(pxEventList, &pxCurrentTCB->xEventListItem)`로 정렬 삽입한다. 소스 주석: "xItemValue = ( configMAX_PRIORITIES - uxPriority ). Therefore, the event list is sorted in descending priority order." 깨울 때는 `listGET_OWNER_OF_HEAD_ENTRY`로 맨 앞 하나 — O(1)이다. 비용은 **넣을 때**(대기자 수만큼) 치르고 **꺼낼 때**(ISR에서 give할 때) 안 치르는 설계다. ISR 쪽 경로를 짧게 하려는 선택이다.

예외가 둘 있다.

- **이벤트 그룹**: `vTaskPlaceOnUnorderedEventList()`는 이벤트 항목 값에 **기다리는 비트 패턴**을 넣고 최상위 비트 `taskEVENT_LIST_ITEM_VALUE_IN_USE`(32비트 tick에서 0x80000000)를 켠다. 정렬하지 않고 끝 삽입한다(이벤트 그룹은 set할 때 대기자 전부를 훑어야 하니 순서가 의미 없다 — D7).
- **우선순위 변경**: `vTaskPrioritySet()`과 우선순위 상속(`xTaskPriorityInherit()`)은 `IN_USE` 비트가 꺼져 있을 때만 이벤트 항목 값을 새 우선순위로 고친다. 그리고 ready 리스트에 있으면 새 우선순위 리스트로 옮기지만, **이벤트 리스트 안의 위치는 다시 정렬하지 않는다**(소스: "If the task is in the blocked or suspended list we need do nothing more than change its priority variable"). 대기 중에 우선순위가 바뀐 태스크는 옛 자리에 남는다 — 드물지만 알아 둘 만한 비대칭이다(D3).

### 2.6 예제 4 — gdb Python으로 "kernel-aware" 덤프: 이벤트 리스트와 tick wrap

**무엇을 확인하나**: 두 가지를 한 번에 본다. (1) 큐 하나에 우선순위 2·4·3·4인 수신자 넷이 블록됐을 때 이벤트 리스트 순서, (2) tick 카운터가 0xffffffff를 넘을 때 delayed 리스트 두 개의 역할이 바뀌는 순간. tick 시작값을 `-DconfigINITIAL_TICK_COUNT=0xFFFFFFF0`으로 wrap 직전에 두었다(이 설정은 `FreeRTOS.h`에서 `#ifndef`로 기본 0이다). 앱 코드는 리스트를 전혀 모른다 — QEMU를 `-S -gdb`로 멈춘 채 띄우고, gdb가 `snap()`에 중단점을 걸어 Python 스크립트로 리스트를 걷는다. RTOS-aware 디버거 플러그인이 하는 일을 30줄로 직접 해 보는 것이다.

```c
/* ex4: 큐 이벤트 리스트(우선순위 정렬)와 overflow delayed 리스트를 gdb로 덤프할 대상 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
static QueueHandle_t q;
static volatile int phase;
void __attribute__((noinline)) snap(int ph) { phase = ph; __asm volatile("" ::: "memory"); }
static void rx(void *pv) {                       /* pv = timeout ticks */
    uint32_t v;
    for (;;) xQueueReceive(q, &v, (TickType_t)(uintptr_t)pv);
}
static void ctl(void *pv) {
    (void)pv;
    vTaskDelay(1);  snap(1);                     /* 모두 블록된 직후 (tick 0xfffffff1) */
    vTaskDelay(20); snap(2);                     /* tick이 0을 넘은 뒤 (wrap 후) */
    board_printf_raw("tick now %u\n", (unsigned)xTaskGetTickCount());
    board_exit(0);
}
int main(void) {
    q = xQueueCreate(1, sizeof(uint32_t));
    vQueueAddToRegistry(q, "q");
    xTaskCreate(rx, "R2",  128, (void *)100, 2, NULL);
    xTaskCreate(rx, "R4a", 128, (void *)5,   4, NULL);
    xTaskCreate(rx, "R3",  128, (void *)portMAX_DELAY, 3, NULL);
    xTaskCreate(rx, "R4b", 128, (void *)40,  4, NULL);
    xTaskCreate(ctl, "CTL", 256, NULL, 6, NULL);
    vTaskStartScheduler();
}
```

```python
# kdump.py — gdb에서 FreeRTOS 리스트를 걷는 작은 "kernel-aware" 스크립트
import gdb
def name(owner):
    if int(owner) == 0: return "-"
    return gdb.parse_and_eval("((TCB_t *)%d)->pcTaskName" % int(owner)).string()
def walk(label, expr):
    l = gdb.parse_and_eval(expr); end = l['xListEnd'].address
    n = int(l['uxNumberOfItems'])
    if n == 0: return
    out = []; p = l['xListEnd']['pxNext']
    while int(p) != int(end):
        out.append("%s(v=%u)@%x" % (name(p['pvOwner']), int(p['xItemValue']) & 0xffffffff, int(p)))
        p = p['pxNext']
    print("  %-28s n=%d  %s" % (label, n, " -> ".join(out)))
class KDump(gdb.Command):
    def __init__(self): super().__init__("kdump", gdb.COMMAND_USER)
    def invoke(self, arg, tty):
        g = gdb.parse_and_eval
        d1 = int(g("&xDelayedTaskList1"))
        print("== snap %d  xTickCount=%u (0x%x)  xNumOfOverflows=%d" % (int(g(arg)), int(g("xTickCount")), int(g("xTickCount")), int(g("xNumOfOverflows"))))
        print("  pxDelayedTaskList=%s  pxOverflowDelayedTaskList=%s  xNextTaskUnblockTime=%u" % (
            "D1" if int(g("pxDelayedTaskList")) == d1 else "D2",
            "D1" if int(g("pxOverflowDelayedTaskList")) == d1 else "D2", int(g("xNextTaskUnblockTime"))))
        walk("xDelayedTaskList1", "xDelayedTaskList1"); walk("xDelayedTaskList2", "xDelayedTaskList2")
        walk("xSuspendedTaskList", "xSuspendedTaskList")
        walk("q.xTasksWaitingToReceive", "((Queue_t *)q)->xTasksWaitingToReceive")
KDump()
```

```text
source /Users/donh/.rtos_scratch/C1/kdump.py
break snap
commands
silent
kdump $r0
continue
end
continue
```

```sh
# gdbrun.sh: QEMU(icount, -S -gdb tcp::1243)를 띄우고 gdb -batch -x ex4.gdb로 붙는다
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C1/ex4.c \
     OUT=/var/tmp/rtos/C1/ex4 EXTRA_CFLAGS=-DconfigINITIAL_TICK_COUNT=0xFFFFFFF0
PORT=1243 /Users/donh/.rtos_scratch/C1/gdbrun.sh /var/tmp/rtos/C1/ex4/app.elf /Users/donh/.rtos_scratch/C1/ex4.gdb
```

```text
qemu-system-arm: warning: icount sleep disabled and no active timers
Reset_Handler () at /var/rtoslab/templates/common/startup.c:42
42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
Breakpoint 1 at 0xd8: file /Users/donh/.rtos_scratch/C1/ex4.c, line 8.
== snap 1  xTickCount=4294967281 (0xfffffff1)  xNumOfOverflows=0
  pxDelayedTaskList=D1  pxOverflowDelayedTaskList=D2  xNextTaskUnblockTime=4294967285
  xDelayedTaskList1            n=1  R4a(v=4294967285)@200014ec
  xDelayedTaskList2            n=2  R4b(v=24)@200019cc -> R2(v=84)@2000127c
  xSuspendedTaskList           n=2  Tmr Svc(v=0)@20000874 -> R3(v=0)@2000175c
  q.xTasksWaitingToReceive     n=4  R4a(v=4)@20001500 -> R4b(v=4)@200019e0 -> R3(v=5)@20001770 -> R2(v=6)@20001290
== snap 2  xTickCount=6 (0x6)  xNumOfOverflows=1
  pxDelayedTaskList=D2  pxOverflowDelayedTaskList=D1  xNextTaskUnblockTime=9
  xDelayedTaskList2            n=3  R4a(v=9)@200014ec -> R4b(v=24)@200019cc -> R2(v=84)@2000127c
  xSuspendedTaskList           n=2  Tmr Svc(v=0)@20000874 -> R3(v=0)@2000175c
  q.xTasksWaitingToReceive     n=4  R4b(v=4)@200019e0 -> R4a(v=4)@20001500 -> R3(v=5)@20001770 -> R2(v=6)@20001290
--- uart:
tick now 7
[board_exit] code=0
```

**출력에서 볼 것**:

- **이벤트 리스트 순서**: 만든 순서는 R2·R4a·R3·R4b였지만 리스트는 R4a(4) → R4b(4) → R3(5) → R2(6). 값 = 8 − 우선순위로 오름차순이니 우선순위 내림차순이다. 같은 우선순위 4끼리는 **먼저 블록한 R4a가 앞**(우선순위가 높은 태스크가 먼저 실행되어 먼저 블록했다).
- **snap 1의 delayed 리스트**: tick이 0xfffffff1일 때 R4a는 5 tick 뒤 = 0xfffffff5라 **현재 리스트 D1**, R4b(40 tick 뒤 = 24)와 R2(100 tick 뒤 = 84)는 더하기가 넘쳐서 작은 값이 되었으니 **overflow 리스트 D2**에 들어갔다. `prvAddCurrentTaskToDelayedList()`의 판단이 `if( xTimeToWake < xConstTickCount )` 한 줄이다.
- **snap 2**: tick이 0을 지나며 `taskSWITCH_DELAYED_LISTS()`가 두 포인터를 맞바꿨다(`pxDelayedTaskList`=D2, `xNumOfOverflows=1`). 이제 D2가 현재 리스트이고 D1(새 overflow)은 비었다.
- R4a는 timeout으로 몇 번 깨었다가 다시 블록하면서 값이 9가 되었고, 이벤트 리스트에서도 **같은 값 4의 맨 뒤**(R4b 뒤)로 다시 줄을 섰다. 대기자 사이의 순서는 "우선순위, 같으면 블록한 순서"다.
- R3(무한 대기)는 delayed가 아니라 suspended에 있지만 이벤트 리스트에는 다른 대기자와 똑같이 들어 있다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">ex4 gdb 덤프: tick wrap 전후의 delayed 리스트와 큐 이벤트 리스트</text>
<rect x="8" y="36" width="330" height="306" rx="4" fill="#e08a3c" fill-opacity="0.04" stroke="#e08a3c"/>
<text x="14" y="58" font-size="13" text-anchor="start" font-weight="bold">snap 1: xTickCount = 0xfffffff1</text>
<text x="14" y="84" font-size="12" text-anchor="start" font-weight="bold">pxDelayedTaskList → D1</text>
<rect x="14" y="92" width="92" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="60" y="107" font-size="12" text-anchor="middle" font-weight="bold">R4a</text>
<text x="60" y="122" font-size="12" text-anchor="middle">0xfffffff5</text>
<text x="14" y="160" font-size="12" text-anchor="start" font-weight="bold">pxOverflowDelayedTaskList → D2</text>
<rect x="14" y="168" width="74" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="51" y="183" font-size="12" text-anchor="middle" font-weight="bold">R4b</text>
<text x="51" y="198" font-size="12" text-anchor="middle">v=24</text>
<rect x="96" y="168" width="74" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="133" y="183" font-size="12" text-anchor="middle" font-weight="bold">R2</text>
<text x="133" y="198" font-size="12" text-anchor="middle">v=84</text>
<line x1="88" y1="186" x2="95" y2="186" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="14" y="236" font-size="12" text-anchor="start" font-weight="bold">큐 q.xTasksWaitingToReceive</text>
<rect x="14" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="51" y="259" font-size="12" text-anchor="middle" font-weight="bold">R4a</text>
<text x="51" y="274" font-size="12" text-anchor="middle">v=4</text>
<rect x="96" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="133" y="259" font-size="12" text-anchor="middle" font-weight="bold">R4b</text>
<text x="133" y="274" font-size="12" text-anchor="middle">v=4</text>
<line x1="88" y1="262" x2="95" y2="262" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="178" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="215" y="259" font-size="12" text-anchor="middle" font-weight="bold">R3</text>
<text x="215" y="274" font-size="12" text-anchor="middle">v=5</text>
<line x1="170" y1="262" x2="177" y2="262" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="260" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="297" y="259" font-size="12" text-anchor="middle" font-weight="bold">R2</text>
<text x="297" y="274" font-size="12" text-anchor="middle">v=6</text>
<line x1="252" y1="262" x2="259" y2="262" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="14" y="314" font-size="12" text-anchor="start">v = configMAX_PRIORITIES(8) − 우선순위</text>
<text x="14" y="332" font-size="12" text-anchor="start">→ 작을수록 앞 = 높은 우선순위가 먼저 깸</text>
<rect x="348" y="36" width="330" height="306" rx="4" fill="#4a7bd0" fill-opacity="0.04" stroke="#4a7bd0"/>
<text x="354" y="58" font-size="13" text-anchor="start" font-weight="bold">snap 2: xTickCount = 6 (wrap 1회)</text>
<text x="354" y="84" font-size="12" text-anchor="start" font-weight="bold">pxDelayedTaskList → D2</text>
<rect x="354" y="92" width="74" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="391" y="107" font-size="12" text-anchor="middle" font-weight="bold">R4a</text>
<text x="391" y="122" font-size="12" text-anchor="middle">v=9</text>
<rect x="436" y="92" width="74" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="473" y="107" font-size="12" text-anchor="middle" font-weight="bold">R4b</text>
<text x="473" y="122" font-size="12" text-anchor="middle">v=24</text>
<line x1="428" y1="110" x2="435" y2="110" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="518" y="92" width="74" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="555" y="107" font-size="12" text-anchor="middle" font-weight="bold">R2</text>
<text x="555" y="122" font-size="12" text-anchor="middle">v=84</text>
<line x1="510" y1="110" x2="517" y2="110" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="354" y="160" font-size="12" text-anchor="start" font-weight="bold">pxOverflowDelayedTaskList → D1</text>
<rect x="354" y="168" width="120" height="36" rx="4" fill="#888" fill-opacity="0.05" stroke="#888" stroke-dasharray="4 3"/>
<text x="414" y="191" font-size="12" text-anchor="middle">(비어 있음)</text>
<text x="354" y="236" font-size="12" text-anchor="start" font-weight="bold">큐 q.xTasksWaitingToReceive</text>
<rect x="354" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="391" y="259" font-size="12" text-anchor="middle" font-weight="bold">R4b</text>
<text x="391" y="274" font-size="12" text-anchor="middle">v=4</text>
<rect x="436" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="473" y="259" font-size="12" text-anchor="middle" font-weight="bold">R4a</text>
<text x="473" y="274" font-size="12" text-anchor="middle">v=4</text>
<line x1="428" y1="262" x2="435" y2="262" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="518" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="259" font-size="12" text-anchor="middle" font-weight="bold">R3</text>
<text x="555" y="274" font-size="12" text-anchor="middle">v=5</text>
<line x1="510" y1="262" x2="517" y2="262" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="600" y="244" width="74" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="637" y="259" font-size="12" text-anchor="middle" font-weight="bold">R2</text>
<text x="637" y="274" font-size="12" text-anchor="middle">v=6</text>
<line x1="592" y1="262" x2="599" y2="262" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="354" y="314" font-size="12" text-anchor="start">R4a는 timeout 뒤 다시 줄 → 같은 값의 맨 뒤</text>
<text x="354" y="332" font-size="12" text-anchor="start">tick 0에서 두 포인터가 자리를 바꿨다</text>
</svg>
```

그림 4 — 예제 4 gdb 덤프 두 장면. tick wrap 순간 두 delayed 리스트의 역할이 포인터 교환 한 번으로 바뀐다.

### 2.7 tick wrap 트릭 — 구조만 (시간 로직은 C5)

delayed 리스트를 하나만 쓰면 "깨울 tick = 지금 + 대기"가 32비트를 넘쳐 작은 수가 될 때 정렬이 망가진다(0xfffffff5보다 24가 먼저 깨는 것으로 보인다). FreeRTOS의 해법은 **리스트 두 개 + 포인터 두 개**다.

- 넘치지 않은 깨울 시각 → `pxDelayedTaskList`가 가리키는 리스트에 정렬 삽입
- 넘친 깨울 시각 → `pxOverflowDelayedTaskList`가 가리키는 리스트에 정렬 삽입
- tick이 0으로 넘어가는 순간(`xTaskIncrementTick()` 안 `if( xConstTickCount == ( TickType_t ) 0U )`) → `taskSWITCH_DELAYED_LISTS()`: 현재 리스트가 비었는지 `configASSERT`로 확인하고(넘치기 전 시각은 모두 지났으니 비어 있어야 한다) 두 포인터를 맞바꾼 뒤 `xNextTaskUnblockTime`을 다시 계산

그래서 리스트 안의 비교는 항상 "같은 바퀴 안의 절댓값"끼리만 일어나고, 비교 한 번 늘리지 않고 wrap을 처리한다. 리스트를 복사하지 않고 **포인터만** 바꾸는 게 핵심이다. Zephyr는 아예 다른 길(64비트 tick + delta 리스트)을 간다(§5.4). 언제 깨우는지, `vTaskDelay`와 `vTaskDelayUntil`의 차이, tick 카운터 폭(16/32/64비트) 선택은 C5에서 다룬다.

### 2.8 삭제 대기 리스트

`xTasksWaitingTermination`은 자기 삭제(`vTaskDelete(NULL)`)한 태스크가 들어가는 곳이다. 자기 스택 위에서 도는 중이라 스스로 free할 수 없으니 상태 항목만 이 리스트에 넣고, idle 태스크의 `prvCheckTasksWaitingTermination()`이 나중에 TCB와 스택을 회수한다. A4 예제 2에서 idle이 굶으면 heap이 안 돌아오는 것을 이미 측정했으니 여기서는 "구조상 또 하나의 끝 삽입 리스트"로만 기억한다(자세히는 C9).

---

## 3. "다음은 누구?" — 최고 우선순위 Ready 태스크 고르기

### 3.1 직관 — 층별 대기줄과 "사람 있는 층" 표시등

ready 리스트 배열은 우선순위마다 하나씩 있는 대기줄이다. 스케줄러는 "사람이 서 있는 가장 높은 층"을 찾아서 그 줄의 다음 사람을 부른다. 찾는 방법이 둘이다: 꼭대기 층부터 한 층씩 내려가며 줄을 들여다보거나(generic), 층마다 "사람 있음" 표시등을 한 비트씩 켜 둔 판을 보고 맨 위 켜진 등을 한 번에 읽거나(port-optimised). 후자가 비트맵 + CLZ다.

FreeRTOS에서 이 선택은 `vTaskSwitchContext()`(PendSV 핸들러가 부른다, C2) 안의 매크로 `taskSELECT_HIGHEST_PRIORITY_TASK()` 하나다. 어느 쪽 구현을 쓸지는 `configUSE_PORT_OPTIMISED_TASK_SELECTION`이 정한다.

### 3.2 두 구현 — 소스 그대로

**generic** (`configUSE_PORT_OPTIMISED_TASK_SELECTION 0`, `tasks.c`):

```c
#define taskRECORD_READY_PRIORITY( uxPriority )   \
    do { if( ( uxPriority ) > uxTopReadyPriority ) uxTopReadyPriority = ( uxPriority ); } while( 0 )

#define taskSELECT_HIGHEST_PRIORITY_TASK()                                            \
do {                                                                                  \
    UBaseType_t uxTopPriority = uxTopReadyPriority;                                   \
    while( listLIST_IS_EMPTY( &( pxReadyTasksLists[ uxTopPriority ] ) ) != pdFALSE )  \
    {   configASSERT( uxTopPriority );  --uxTopPriority;  }                           \
    listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) ); \
    uxTopReadyPriority = uxTopPriority;                                               \
} while( 0 )

#define taskRESET_READY_PRIORITY( uxPriority )      /* generic에서는 빈 매크로 */
```

`uxTopReadyPriority`는 "이보다 높은 우선순위에는 확실히 아무도 없다"는 **상한**이다. 태스크가 ready가 될 때 더 높으면 올리고(O(1)), 블록될 때는 아무것도 안 한다. 내리는 일은 다음 선택 때 빈 리스트를 하나씩 내려가며 한다(lazy). 그래서 비용은 "방금 비워진 최고 우선순위"와 "실제 최고 ready 우선순위" 사이의 **빈 층 수**에 비례한다. 최악은 `configMAX_PRIORITIES − 1`층 전부.

**port-optimised** (`portable/GCC/ARM_CM3/portmacro.h`):

```c
__attribute__( ( always_inline ) ) static inline uint8_t ucPortCountLeadingZeros( uint32_t ulBitmap )
{
    uint8_t ucReturn;
    __asm volatile ( "clz %0, %1" : "=r" ( ucReturn ) : "r" ( ulBitmap ) : "memory" );
    return ucReturn;
}
#define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities )    ( uxReadyPriorities ) |= ( 1UL << ( uxPriority ) )
#define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities )     ( uxReadyPriorities ) &= ~( 1UL << ( uxPriority ) )
#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities )  uxTopPriority = ( 31UL - ( uint32_t ) ucPortCountLeadingZeros( ( uxReadyPriorities ) ) )
```

이때 같은 변수 `uxTopReadyPriority`가 **비트맵**으로 쓰인다(비트 p = "우선순위 p ready 리스트가 비어 있지 않음"). 예제 3의 `top=0x43`이 그 비트맵이었다. 선택은 `31 − clz(비트맵)` 한 번, 그 다음 `listGET_OWNER_OF_NEXT_ENTRY`. 비트를 지우는 `taskRESET_READY_PRIORITY()`는 해당 ready 리스트 길이가 0이 될 때만 부른다 — 그래서 `uxListRemove()`가 남은 항목 수를 반환하는 것이다.

제약도 소스에 그대로 있다: `configMAX_PRIORITIES > 32`이면 `#error`("It is very rare that a system requires more than 10 to 15 difference priorities as tasks that share a priority will time slice"). 비트맵이 32비트 한 워드라서다. 포트별 기본값은 V11.3.2 기준으로 이렇다.

| 포트 (GCC) | 기본값 | 근거 |
|---|---|---|
| ARM_CM3, ARM_CM4F, ARM_CM7 | 1 (`#ifndef`로) | ARMv7-M에는 `CLZ` 명령이 있다 |
| ARM_CM33_NTZ 등 ARMv8-M | Main Extension이 있고 단일 코어면 1, 아니면 0 | ARMv8-M Baseline(M23)에는 `CLZ`가 없다 |
| ARM_CM0 | 정의 없음 → `tasks.c` 기본값 0 (generic) | ARMv6-M에는 `CLZ`가 없다 |
| RISC-V | 1, `__builtin_clz()` 사용 | Zbb 확장이 없으면 컴파일러가 소프트웨어 루틴으로 만든다 (그 경우 비용은 확인 못 함) |
| SMP 빌드 (`configNUMBER_OF_CORES > 1`) | 1로 두면 `#error` | SMP 선택은 `prvSelectHighestPriorityTask()`가 코어별로 한다 (C10) |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">최고 우선순위 찾기: 위에서부터 훑기 vs 비트맵 + clz</text>
<text x="20" y="50" font-size="13" text-anchor="start" font-weight="bold">generic (configUSE_PORT_OPTIMISED_TASK_SELECTION 0)</text>
<rect x="20" y="62" width="44" height="28" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="42" y="81" font-size="12" text-anchor="middle">[7]</text>
<rect x="72" y="62" width="44" height="28" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="94" y="81" font-size="12" text-anchor="middle">[6]</text>
<rect x="124" y="62" width="44" height="28" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="146" y="81" font-size="12" text-anchor="middle">[5]</text>
<rect x="176" y="62" width="44" height="28" rx="4" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="198" y="81" font-size="12" text-anchor="middle">[4]</text>
<rect x="228" y="62" width="44" height="28" rx="4" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="250" y="81" font-size="12" text-anchor="middle">[3]</text>
<rect x="280" y="62" width="44" height="28" rx="4" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="302" y="81" font-size="12" text-anchor="middle">[2]</text>
<rect x="332" y="62" width="44" height="28" rx="4" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="354" y="81" font-size="12" text-anchor="middle">[1]</text>
<rect x="384" y="62" width="44" height="28" rx="4" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="406" y="81" font-size="12" text-anchor="middle">[0]</text>
<path d="M 42 96 Q 68 112 92 96" fill="none" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<path d="M 94 96 Q 120 112 144 96" fill="none" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<path d="M 146 96 Q 172 112 196 96" fill="none" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<text x="450" y="66" font-size="12" text-anchor="start">빈 리스트 1개마다 명령 5개</text>
<text x="450" y="84" font-size="12" text-anchor="start">(cbz, ldr, subs, cmp, beq)</text>
<text x="450" y="102" font-size="12" text-anchor="start">→ 빈 우선순위 수에 비례</text>
<text x="20" y="140" font-size="13" text-anchor="start" font-weight="bold">port-optimised (ARM_CM3 기본값 1)</text>
<rect x="20" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="28.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="39" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="47.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="58" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="66.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="77" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="85.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="96" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="104.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="115" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="123.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="134" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="142.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="153" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="161.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="172" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="180.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="191" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="199.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="210" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="218.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="229" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="237.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="248" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="256.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="267" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="275.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="286" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="294.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="305" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="313.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="324" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="332.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="343" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="351.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="362" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="370.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="381" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="389.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="400" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="408.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="419" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="427.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="438" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="446.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="457" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="465.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="476" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="484.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="495" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="503.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="514" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="522.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="533" y="152" width="17" height="26" rx="2" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="541.5" y="170" font-size="12" text-anchor="middle">1</text>
<rect x="552" y="152" width="17" height="26" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="560.5" y="170" font-size="12" text-anchor="middle">0</text>
<rect x="571" y="152" width="17" height="26" rx="2" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="579.5" y="170" font-size="12" text-anchor="middle">1</text>
<rect x="590" y="152" width="17" height="26" rx="2" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="598.5" y="170" font-size="12" text-anchor="middle">1</text>
<rect x="609" y="152" width="17" height="26" rx="2" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="617.5" y="170" font-size="12" text-anchor="middle">1</text>
<text x="20" y="196" font-size="12" text-anchor="start">bit31</text>
<text x="626" y="196" font-size="12" text-anchor="end">bit0</text>
<text x="541" y="214" font-size="12" text-anchor="middle">bit4</text>
<line x1="20" y1="228" x2="531" y2="228" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<text x="267" y="246" font-size="12" text-anchor="middle">clz = 앞쪽 0의 개수 = 27</text>
<text x="20" y="272" font-size="12" text-anchor="start">uxTopReadyPriority = 0x17 → 31 − clz(0x17) = 31 − 27 = 4 → pxReadyTasksLists[4]</text>
<text x="20" y="292" font-size="12" text-anchor="start">명령 1개 (clz) + 뺄셈. 우선순위가 몇 개든 같은 비용, 단 32개까지 (비트맵이 32비트 한 워드)</text>
</svg>
```

그림 5 — 같은 ready 상태(우선순위 4·2·1·0)에서 두 방식이 최고 우선순위 4를 찾는 길. generic은 7→6→5→4로 빈 줄을 세 번 확인하고, port-optimised는 비트맵 0x17에 `clz` 한 번(27) → 31 − 27 = 4.

### 3.3 예제 5 — 역어셈블로 `clz` 확인하기

**무엇을 확인하나**: 예제 6의 두 빌드(`configMAX_PRIORITIES 8`, 선택 방식 0/1)에서 `vTaskSwitchContext`의 기계어를 비교한다. 템플릿 `FreeRTOSConfig.h`는 이 값을 `#ifndef` 없이 1로 박아 두었으므로, 예제 6의 앱 폴더에 템플릿을 include한 뒤 `#undef`하는 설정 파일을 두었다(예제 6 참고).

```sh
arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/C1/sel_o1_p8/app.elf | awk '/<vTaskSwitchContext>:/,/^$/'
arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/C1/sel_o0_p8/app.elf | awk '/<vTaskSwitchContext.part.0>:/,/^$/'
```

```asm
@ port-optimised (sel_o1_p8) <vTaskSwitchContext> — 선택 부분만 (앞의 run-time stats·스택 검사 0xd64~0xdb0 생략)
     db2:	ldr	r3, [pc, #132]	@ (e38 <vTaskSwitchContext+0xd4>)
     db4:	ldr	r3, [r3, #0]
     db6:	clz	r3, r3
     dba:	uxtb	r3, r3
     dbc:	rsb	r3, r3, #31
     dc0:	ldr	r0, [pc, #120]	@ (e3c <vTaskSwitchContext+0xd8>)
     dc2:	add.w	r3, r3, r3, lsl #2
     dc6:	lsls	r2, r3, #2
     dc8:	ldr	r1, [r0, r2]
     dca:	add.w	r3, r0, r3, lsl #2
     dce:	cbz	r1, e12 <vTaskSwitchContext+0xae>
     dd0:	ldr	r1, [r3, #4]
     dd2:	adds	r2, #8
     dd4:	ldr	r1, [r1, #4]
     dd6:	add	r2, r0
     dd8:	cmp	r1, r2
     dda:	str	r1, [r3, #4]
     ddc:	itt	eq
     dde:	ldreq	r1, [r3, #12]
     de0:	streq	r1, [r3, #4]
     de2:	ldr	r3, [r1, #12]
     de4:	str	r3, [r4, #0]
     de6:	ldr	r3, [r4, #0]
     de8:	pop	{r4, r5, r6, pc}

@ generic (sel_o0_p8) <vTaskSwitchContext.part.0> — 선택 부분만 (0x6ec~0x738 생략)
     73a:	ldr	r5, [pc, #128]	@ (7bc <vTaskSwitchContext.part.0+0xd0>)
     73c:	ldr	r0, [pc, #128]	@ (7c0 <vTaskSwitchContext.part.0+0xd4>)
     73e:	ldr	r3, [r5, #0]
     740:	add.w	r2, r3, r3, lsl #2
     744:	ldr.w	r6, [r0, r2, lsl #2]
     748:	lsls	r1, r3, #2
     74a:	add.w	r2, r0, r2, lsl #2
     74e:	cbnz	r6, 75e <vTaskSwitchContext.part.0+0x72>
     750:	cbz	r3, 79a <vTaskSwitchContext.part.0+0xae>
     752:	ldr.w	r1, [r2, #-20]!
     756:	subs	r3, #1
     758:	cmp	r1, #0
     75a:	beq.n	750 <vTaskSwitchContext.part.0+0x64>
     75c:	lsls	r1, r3, #2
     75e:	adds	r2, r1, r3
     760:	add.w	r1, r0, r2, lsl #2
     764:	mov	r0, r1
     766:	ldr	r2, [r1, #4]
     768:	adds	r0, #8
     76a:	ldr	r2, [r2, #4]
     76c:	cmp	r2, r0
     76e:	str	r2, [r1, #4]
     770:	itt	eq
     772:	ldreq	r2, [r1, #12]
     774:	streq	r2, [r1, #4]
     776:	ldr	r2, [r2, #12]
     778:	str	r2, [r4, #0]
     77a:	str	r3, [r5, #0]
     77c:	ldr	r3, [r4, #0]
     77e:	pop	{r4, r5, r6, pc}
```

**출력에서 볼 것**: port-optimised 빌드에는 `ldr r3, [r3, #0]`(비트맵 로드) → **`clz r3, r3`** → `uxtb` → `rsb r3, r3, #31`(31 − clz)이 일직선으로 있고 반복이 없다. 그 뒤 `add.w r3, r3, r3, lsl #2` / `lsls r2, r3, #2`는 우선순위 × 20(= `List_t` 크기)으로 배열 주소를 만드는 계산이다(5 × 4). generic 빌드에는 0x750~0x75a에 **빈 리스트 한 개당 명령 5개짜리 루프**가 있다: `cbz r3`(= `configASSERT(uxTopPriority)`, 0까지 내려가면 assert) → `ldr.w r1, [r2, #-20]!`(한 칸 아래 리스트의 `uxNumberOfItems`, 주소를 20 B 내리며 읽음) → `subs r3, #1` → `cmp r1, #0` → `beq`. 두 빌드 모두 앞부분에 run-time stats(`board_now_us`)와 스택 오버플로 검사(0xa5a5a5a5 비교, A4 §5.5)가 들어 있다 — 이 랩 설정에서는 그쪽이 선택보다 더 비싸다는 것도 보인다.

### 3.4 예제 6 — 선택 비용을 우선순위 개수별로 재기

**무엇을 확인하나**: 우선순위 `configMAX_PRIORITIES − 1`인 H와 우선순위 1인 L 두 태스크(+ idle)가 태스크 알림으로 핑퐁한다. H가 `ulTaskNotifyTake(portMAX_DELAY)`로 블록하면 스케줄러는 꼭대기에서 우선순위 1까지 내려가야 L을 찾는다 — generic의 최악에 가까운 상황이다. "H 블록 → L 실행 시작"(block->L)과 "L의 give → H 복귀"(give->H)를 FPGAIO 카운터로 2000번 재서 평균을 낸다. 우선순위 개수 4~64, 선택 방식 0/1을 바꿔 가며 빌드한다(1은 32까지만 허용). 타이머 태스크는 꺼서 리스트를 단순하게 했다.

```c
/* C1 sel 전용: 템플릿 설정 + 태스크 선택 방식(C1_OPT)과 타이머 끄기 */
#ifndef C1_SEL_CONFIG_H
#define C1_SEL_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef  configUSE_PORT_OPTIMISED_TASK_SELECTION
#define configUSE_PORT_OPTIMISED_TASK_SELECTION C1_OPT
#undef  configUSE_TIMERS
#define configUSE_TIMERS 0
#undef  INCLUDE_xTimerPendFunctionCall
#define INCLUDE_xTimerPendFunctionCall 0
#endif
```

```c
/* ex6: 블록 → 다음 태스크 실행까지 비용을 configMAX_PRIORITIES와 선택 방식별로 잰다 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)   /* FPGAIO 25 MHz counter */
#define N 2000
static TaskHandle_t hH;
static volatile uint32_t t0, sum_block, sum_wake, cnt;
static void high(void *pv) {                          /* 우선순위 configMAX_PRIORITIES-1 */
    (void)pv;
    for (int i = 0; i < N; i++) {
        t0 = CYC();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);     /* H 블록 → 선택 → L 실행 */
        sum_wake += CYC() - t0;                      /* L이 give한 시각부터 H 복귀까지 */
    }
    board_printf_raw("OPT=%u MAXPRI=%u block->L %u.%u cyc, give->H %u.%u cyc (n=%u)\n",
        (unsigned)configUSE_PORT_OPTIMISED_TASK_SELECTION, (unsigned)configMAX_PRIORITIES,
        (unsigned)(sum_block / cnt), (unsigned)(sum_block % cnt * 10 / cnt),
        (unsigned)(sum_wake / N), (unsigned)(sum_wake % N * 10 / N), (unsigned)cnt);
    board_exit(0);
}
static void low(void *pv) {                           /* 우선순위 1 */
    (void)pv;
    for (;;) {
        uint32_t t1 = CYC();
        sum_block += t1 - t0; cnt++;
        t0 = CYC();
        xTaskNotifyGive(hH);                         /* H가 깨어나 즉시 선점 */
    }
}
int main(void) {
    xTaskCreate(high, "H", 256, NULL, configMAX_PRIORITIES - 1, &hH);
    xTaskCreate(low, "L", 256, NULL, 1, NULL);
    vTaskStartScheduler();
}
```

```sh
# sel.sh — 9개 빌드·실행
for opt in 0 1; do for p in 4 8 16 32 64; do
  [ $opt = 1 ] && [ $p -gt 32 ] && continue
  O=/var/tmp/rtos/C1/sel_o${opt}_p$p
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C1/sel/main.c OUT=$O \
     EXTRA_CFLAGS="-DC1_OPT=$opt -DconfigMAX_PRIORITIES=$p" >/dev/null || exit 1
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 60 | grep OPT
done; done
```

```text
OPT=0 MAXPRI=4 block->L 235.6 cyc, give->H 191.4 cyc (n=2000)
OPT=0 MAXPRI=8 block->L 249.9 cyc, give->H 192.2 cyc (n=2000)
OPT=0 MAXPRI=16 block->L 282.1 cyc, give->H 192.2 cyc (n=2000)
OPT=0 MAXPRI=32 block->L 346.1 cyc, give->H 192.3 cyc (n=2000)
OPT=0 MAXPRI=64 block->L 474.6 cyc, give->H 192.0 cyc (n=2000)
OPT=1 MAXPRI=4 block->L 231.5 cyc, give->H 192.3 cyc (n=2000)
OPT=1 MAXPRI=8 block->L 231.5 cyc, give->H 192.3 cyc (n=2000)
OPT=1 MAXPRI=16 block->L 231.5 cyc, give->H 192.3 cyc (n=2000)
OPT=1 MAXPRI=32 block->L 231.5 cyc, give->H 192.3 cyc (n=2000)
```

**출력에서 볼 것**: port-optimised는 우선순위가 4개든 32개든 **231.5 cycle로 한 자리도 안 변한다**. generic은 4개일 때 235.6에서 64개일 때 474.6으로 늘어난다. 기울기 (474.6 − 235.6) / 60 = **3.98 cycle / 빈 우선순위** = 명령 4.98개(1 cycle = 명령 1.25개) — 예제 5에서 센 루프 명령 5개와 정확히 맞는다. 반대 방향(give->H, 192 cycle 근처)은 두 방식이 같다. 깨울 때는 generic도 `if (p > top) top = p` 한 번이라 O(1)이고, 비용은 **블록하는 쪽**(최고 우선순위가 비는 순간)에만 붙는다.

```svg
<svg viewBox="0 0 640 348" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" font-size="14" text-anchor="middle" font-weight="bold">블록 → 다음 태스크 실행까지 (FreeRTOS, QEMU icount)</text>
<line x1="70" y1="298" x2="620" y2="298" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="58" x2="70" y2="298" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="298" x2="620" y2="298" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="302" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="250" x2="620" y2="250" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="254" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="202" x2="620" y2="202" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="206" font-size="12" text-anchor="end">200</text>
<line x1="70" y1="154" x2="620" y2="154" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="158" font-size="12" text-anchor="end">300</text>
<line x1="70" y1="106" x2="620" y2="106" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="110" font-size="12" text-anchor="end">400</text>
<line x1="70" y1="58" x2="620" y2="58" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="62" font-size="12" text-anchor="end">500</text>
<line x1="104.375" y1="298" x2="104.375" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="104.375" y="316" font-size="12" text-anchor="middle">4</text>
<line x1="138.75" y1="298" x2="138.75" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="138.75" y="316" font-size="12" text-anchor="middle">8</text>
<line x1="207.5" y1="298" x2="207.5" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="207.5" y="316" font-size="12" text-anchor="middle">16</text>
<line x1="345" y1="298" x2="345" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="345" y="316" font-size="12" text-anchor="middle">32</text>
<line x1="620" y1="298" x2="620" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="620" y="316" font-size="12" text-anchor="middle">64</text>
<text x="345" y="340" font-size="13" text-anchor="middle">configMAX_PRIORITIES</text>
<text x="14" y="48" font-size="12" text-anchor="start">25 MHz 카운터 cycle</text>
<polyline points="104.4,184.9 138.8,178.0 207.5,162.6 345.0,131.9 620.0,70.2" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="104.4" cy="184.9" r="3.5" fill="#4a7bd0"/>
<circle cx="138.8" cy="178.0" r="3.5" fill="#4a7bd0"/>
<circle cx="207.5" cy="162.6" r="3.5" fill="#4a7bd0"/>
<circle cx="345.0" cy="131.9" r="3.5" fill="#4a7bd0"/>
<circle cx="620.0" cy="70.2" r="3.5" fill="#4a7bd0"/>
<line x1="90" y1="78" x2="112" y2="78" stroke="#4a7bd0" stroke-width="2.5"/>
<text x="118" y="82" font-size="12" text-anchor="start">generic: uxTopReadyPriority부터 아래로 훑기</text>
<polyline points="104.4,186.9 138.8,186.9 207.5,186.9 345.0,186.9" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="104.4" cy="186.9" r="3.5" fill="#e08a3c"/>
<circle cx="138.8" cy="186.9" r="3.5" fill="#e08a3c"/>
<circle cx="207.5" cy="186.9" r="3.5" fill="#e08a3c"/>
<circle cx="345.0" cy="186.9" r="3.5" fill="#e08a3c"/>
<line x1="90" y1="98" x2="112" y2="98" stroke="#e08a3c" stroke-width="2.5"/>
<text x="118" y="102" font-size="12" text-anchor="start">port-optimised: 비트맵 + clz</text>
<text x="602.812" y="139.6" font-size="12" text-anchor="end">+4.0 cycle / 빈 우선순위 1개</text>
<text x="345" y="204.4" font-size="12" text-anchor="middle">항상 231.5</text>
</svg>
```

그림 6 — 예제 6의 측정값. 25 MHz 칩이라고 치면 템플릿 기본값 8단계에서 generic의 추가 비용은 18 cycle(0.7 us), 32단계에서 115 cycle(4.6 us), 64단계에서 239 cycle(9.6 us)이다. 절대값은 QEMU icount 기준.

실무 해석: 우선순위 8~16단계에서 generic의 추가 비용은 컨텍스트 스위치 전체(이 랩에서 약 230 cycle)의 10% 안팎이다. "CLZ라서 O(1)"이 중요한 이유는 평균 속도보다 **상한이 우선순위 개수와 무관하다**는 데 있다. Cortex-M0(CLZ 없음)에서는 generic만 가능하니 우선순위 개수를 작게(5~8) 잡는 게 그 자체로 최적화다.

### 3.5 같은 우선순위끼리 — `pxIndex` round-robin

최고 우선순위를 정한 뒤 그 리스트의 "누구"를 고르는 것이 `listGET_OWNER_OF_NEXT_ENTRY`다.

```c
( pxConstList )->pxIndex = ( pxConstList )->pxIndex->pxNext;
if( ( void * ) ( pxConstList )->pxIndex == ( void * ) &( ( pxConstList )->xListEnd ) )
    ( pxConstList )->pxIndex = ( pxConstList )->xListEnd.pxNext;      /* END는 건너뛴다 */
( pxTCB ) = ( pxConstList )->pxIndex->pvOwner;
```

리스트에서 태스크를 빼거나 끝으로 옮기지 않고 **커서만 한 칸** 민다. 예제 2의 "NEXT_ENTRY x5: A B C A B"가 이 동작이다. 같은 우선순위 태스크가 여럿이면 tick마다(time slicing, `configUSE_TIME_SLICING 1`) 또는 yield할 때마다 커서가 돌아서 공평하게 돈다. 새로 ready가 된 태스크는 `listINSERT_END`로 커서 바로 앞, 즉 "이번 바퀴의 맨 마지막"에 들어간다(예제 2의 D). 정책 차원의 이야기(time slicing 켜고 끄기, 협력형, 지터)는 C4에서 한다.

### 3.6 흔한 함정

- **"우선순위를 넉넉히 32개"**: RAM(ready 배열 640 B)과 generic 선택 비용이 같이 는다. 포트 최적화가 꺼진 칩(M0·M23)에서 특히 손해다. 실제로 쓰는 단계 수 + 여유 1~2개면 충분하다.
- **비트맵 직접 해석 실수**: `uxTopReadyPriority`는 설정에 따라 "숫자"(generic)이기도 하고 "비트맵"(optimised)이기도 하다. 디버거로 볼 때 어느 쪽인지 먼저 확인해야 한다(예제 3의 0x43은 우선순위 67이 아니다).
- **우선순위 0 리스트가 비는 상황**: idle 태스크가 항상 ready[0]에 있으므로 generic 루프는 0에서 반드시 멈춘다. idle을 지우거나 블록시키면(idle hook에서 블록 API 호출) `configASSERT(uxTopPriority)`가 터진다 — idle hook에서 블록 금지 규칙의 구조적 이유다(C9).

---

## 4. 결정성 — O(1)과 O(n)은 어디에 숨어 있나

### 4.1 FreeRTOS 연산 복잡도 표

"RTOS 스케줄러는 O(1)인가?"의 정확한 답은 "**선택은 O(1), 하지만 정렬 삽입과 tick의 일괄 깨움은 O(n)이다**"다. 어떤 O(n)이 어떤 보호 아래 도는지가 결정성을 가른다.

| 연산 | 함수 | 복잡도 (n = …) | 실행 맥락 | 인터럽트 |
|---|---|---|---|---|
| 최고 우선순위 선택 (CLZ) | `taskSELECT_HIGHEST_PRIORITY_TASK` | O(1) | PendSV | 짧게 마스크 (C2) |
| 최고 우선순위 선택 (generic) | 같은 매크로 | O(빈 우선순위 수) ≤ O(P) | PendSV | 같음 |
| ready로 넣기 | `prvAddTaskToReadyList` | O(1) | 태스크/ISR | 크리티컬 섹션 |
| 리스트에서 빼기 | `uxListRemove` | O(1) | 어디서나 | 크리티컬 섹션 |
| delayed 리스트에 넣기 | `vListInsert` | O(delayed 태스크 수) | 태스크 (`vTaskDelay` 등) | **스케줄러 정지, 인터럽트 허용** |
| 이벤트 리스트에 넣기 | `vTaskPlaceOnEventList` → `vListInsert` | O(그 객체의 대기자 수) | 태스크 (큐 대기) | 스케줄러 정지 + 큐 잠금, 인터럽트 허용 |
| 이벤트 대기자 하나 깨우기 | `xTaskRemoveFromEventList` | O(1) (맨 앞) | 태스크/ISR (give) | 크리티컬 섹션 |
| tick 처리, 깨울 태스크 없음 | `xTaskIncrementTick` | O(1) (`xNextTaskUnblockTime` 비교) | SysTick ISR | **BASEPRI로 마스크** |
| tick 처리, 같은 tick에 K개 깨움 | 같은 함수 | O(K) | SysTick ISR | **BASEPRI로 마스크** |
| 이벤트 그룹 set | `xEventGroupSetBits` | O(대기자 수) | 태스크 | 스케줄러 정지 |
| 스케줄러 재개 | `xTaskResumeAll` | O(pending 수) | 태스크 | 크리티컬 섹션 |

"스케줄러 정지 + 인터럽트 허용" 줄이 FreeRTOS 설계의 요점이다. O(n) 정렬 삽입은 인터럽트를 막지 않은 채 돌기 때문에 **인터럽트 지연(latency)은 n과 무관**하다. 대신 다른 **태스크의 응답 시간**은 그만큼 늦어질 수 있다(정지 구간에는 스위치가 안 일어나니까). 반면 tick ISR 안의 O(K)는 BASEPRI로 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하 인터럽트를 막은 채 돈다 — 이것은 **인터럽트 지연에 직접 더해진다**. 아래 두 예제가 이 두 줄을 잰다.

### 4.2 예제 7 — delayed 리스트 정렬 삽입의 최악

**무엇을 확인하나**: 아주 먼 미래(100000 tick 이상)에 깨도록 잠든 태스크 N개를 delayed 리스트에 깔아 두고, H(우선순위 3)가 (a) 5 tick 대기(맨 앞에 삽입) 또는 (b) 200000 tick 대기(N개를 다 지나 맨 뒤에 삽입)로 `vTaskDelay`를 부를 때 "호출 → L 실행 시작"까지를 500번 잰다. L은 재자마자 `xTaskAbortDelay(hH)`로 H를 깨워 다음 측정을 돌린다(delayed 리스트에서 빼기는 O(1)이라 측정을 오염시키지 않는다).

```c
/* ex7: delayed 리스트 정렬 삽입 비용 — 잠든 태스크 NSLEEP개 앞(head) 또는 뒤(tail)에 끼워 넣기 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)   /* FPGAIO 25 MHz counter */
#ifndef NSLEEP
#define NSLEEP 0
#endif
#define ITER 500
static TaskHandle_t hH;
static volatile uint32_t t0, flag, sum, cnt, mx;
static void sleeper(void *pv) { vTaskDelay(100000 + (TickType_t)(uintptr_t)pv); for (;;) vTaskSuspend(NULL); }
static void high(void *pv) {                          /* prio 3 */
    TickType_t d = (TickType_t)(uintptr_t)pv;         /* 5 → head, 200000 → tail */
    vTaskDelay(50);                                   /* sleeper들이 먼저 잠들게 */
    for (int i = 0; i < ITER; i++) {
        flag = 1; t0 = CYC();
        vTaskDelay(d);                                /* 정렬 삽입 → 스위치 → L */
    }
    board_printf_raw("NSLEEP=%u delay=%u: block->L avg %u.%u max %u cyc (n=%u)\n", (unsigned)NSLEEP,
        (unsigned)d, (unsigned)(sum / cnt), (unsigned)(sum % cnt * 10 / cnt), (unsigned)mx, (unsigned)cnt);
    if (d == 5) { sum = cnt = mx = 0; xTaskCreate(high, "H2", 256, (void *)200000, 3, &hH); vTaskDelete(NULL); }
    board_exit(0);
}
static void low(void *pv) {                           /* prio 1: 재고 H를 즉시 깨운다 */
    (void)pv;
    for (;;) {
        if (flag) {
            uint32_t dt = CYC() - t0;
            flag = 0; sum += dt; cnt++; if (dt > mx) mx = dt;
            xTaskAbortDelay(hH);                     /* delayed 리스트에서 O(1) 제거 */
        }
    }
}
int main(void) {
    for (int i = 0; i < NSLEEP; i++) xTaskCreate(sleeper, "S", 128, (void *)(uintptr_t)i, 2, NULL);
    xTaskCreate(high, "H", 256, (void *)5, 3, &hH);
    xTaskCreate(low, "L", 256, NULL, 1, NULL);
    vTaskStartScheduler();
}
```

```sh
for n in 0 8 16 32 64 128; do
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C1/ex7.c OUT=/var/tmp/rtos/C1/ex7_n$n EXTRA_CFLAGS="-DNSLEEP=$n" >/dev/null
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C1/ex7_n$n/app.elf 60 | grep NSLEEP
done
```

```text
NSLEEP=0 delay=5: block->L avg 211.4 max 261 cyc (n=500)
NSLEEP=0 delay=200000: block->L avg 211.4 max 248 cyc (n=500)
NSLEEP=8 delay=5: block->L avg 211.4 max 260 cyc (n=500)
NSLEEP=8 delay=200000: block->L avg 243.6 max 293 cyc (n=500)
NSLEEP=16 delay=5: block->L avg 211.4 max 261 cyc (n=500)
NSLEEP=16 delay=200000: block->L avg 275.6 max 324 cyc (n=500)
NSLEEP=32 delay=5: block->L avg 211.4 max 261 cyc (n=500)
NSLEEP=32 delay=200000: block->L avg 339.7 max 389 cyc (n=500)
NSLEEP=64 delay=5: block->L avg 211.4 max 261 cyc (n=500)
NSLEEP=64 delay=200000: block->L avg 467.9 max 517 cyc (n=500)
NSLEEP=128 delay=5: block->L avg 211.4 max 261 cyc (n=500)
NSLEEP=128 delay=200000: block->L avg 724.4 max 773 cyc (n=500)
```

**출력에서 볼 것**: 맨 앞 삽입은 N이 0이든 128이든 **211.4 cycle**로 같다(루프가 첫 비교에서 멈춘다). 맨 뒤 삽입은 N에 정확히 비례한다: (724.4 − 211.4) / 128 = **4.0 cycle / 항목** = 명령 5개. `vListInsert`를 역어셈블하면 루프가 `mov r4, r3` / `ldr r3, [r3, #4]` / `ldr r2, [r3, #0]` / `cmp r2, r5` / `bls` 다섯 개다(아래). 최댓값(점선)은 평균보다 약 50 cycle 큰데, 측정 구간에 tick ISR이 끼어든 경우다.

```asm
000016bc <vListInsert>:
    16bc:	push	{r4, r5}
    16be:	ldr	r5, [r1, #0]
    16c0:	adds	r3, r5, #1
    16c2:	beq.n	16e6 <vListInsert+0x2a>
    16c4:	add.w	r3, r0, #8
    16c8:	mov	r4, r3
    16ca:	ldr	r3, [r3, #4]
    16cc:	ldr	r2, [r3, #0]
    16ce:	cmp	r2, r5
    16d0:	bls.n	16c8 <vListInsert+0xc>
    16d2:	ldr	r2, [r0, #0]
    16d4:	str	r3, [r1, #4]
    16d6:	adds	r2, #1
    16d8:	str	r1, [r3, #8]
    16da:	str	r4, [r1, #8]
    16dc:	str	r1, [r4, #4]
    16de:	str	r0, [r1, #16]
    16e0:	pop	{r4, r5}
    16e2:	str	r2, [r0, #0]
    16e4:	bx	lr
    16e6:	ldr	r4, [r0, #16]
    16e8:	ldr	r3, [r4, #4]
    16ea:	b.n	16d2 <vListInsert+0x16>
```

```svg
<svg viewBox="0 0 640 348" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" font-size="14" text-anchor="middle" font-weight="bold">vTaskDelay 정렬 삽입: delayed 리스트 길이에 따른 비용</text>
<line x1="70" y1="298" x2="620" y2="298" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="58" x2="70" y2="298" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="298" x2="620" y2="298" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="302" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="238" x2="620" y2="238" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="242" font-size="12" text-anchor="end">200</text>
<line x1="70" y1="178" x2="620" y2="178" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="182" font-size="12" text-anchor="end">400</text>
<line x1="70" y1="118" x2="620" y2="118" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="122" font-size="12" text-anchor="end">600</text>
<line x1="70" y1="58" x2="620" y2="58" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="62" font-size="12" text-anchor="end">800</text>
<line x1="70" y1="298" x2="70" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="70" y="316" font-size="12" text-anchor="middle">0</text>
<line x1="104.375" y1="298" x2="104.375" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="104.375" y="316" font-size="12" text-anchor="middle">8</text>
<line x1="138.75" y1="298" x2="138.75" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="138.75" y="316" font-size="12" text-anchor="middle">16</text>
<line x1="207.5" y1="298" x2="207.5" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="207.5" y="316" font-size="12" text-anchor="middle">32</text>
<line x1="345" y1="298" x2="345" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="345" y="316" font-size="12" text-anchor="middle">64</text>
<line x1="620" y1="298" x2="620" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="620" y="316" font-size="12" text-anchor="middle">128</text>
<text x="345" y="340" font-size="13" text-anchor="middle">이미 delayed 리스트에 있는 태스크 수 N</text>
<text x="14" y="48" font-size="12" text-anchor="start">25 MHz 카운터 cycle</text>
<polyline points="70.0,234.6 104.4,224.9 138.8,215.3 207.5,196.1 345.0,157.6 620.0,80.7" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="70.0" cy="234.6" r="3.5" fill="#d0564a"/>
<circle cx="104.4" cy="224.9" r="3.5" fill="#d0564a"/>
<circle cx="138.8" cy="215.3" r="3.5" fill="#d0564a"/>
<circle cx="207.5" cy="196.1" r="3.5" fill="#d0564a"/>
<circle cx="345.0" cy="157.6" r="3.5" fill="#d0564a"/>
<circle cx="620.0" cy="80.7" r="3.5" fill="#d0564a"/>
<line x1="90" y1="78" x2="112" y2="78" stroke="#d0564a" stroke-width="2.5"/>
<text x="118" y="82" font-size="12" text-anchor="start">맨 뒤에 삽입 (가장 늦게 깸) 평균</text>
<polyline points="70.0,223.6 104.4,210.1 138.8,200.8 207.5,181.3 345.0,142.9 620.0,66.1" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/>
<circle cx="70.0" cy="223.6" r="3.5" fill="#d0564a"/>
<circle cx="104.4" cy="210.1" r="3.5" fill="#d0564a"/>
<circle cx="138.8" cy="200.8" r="3.5" fill="#d0564a"/>
<circle cx="207.5" cy="181.3" r="3.5" fill="#d0564a"/>
<circle cx="345.0" cy="142.9" r="3.5" fill="#d0564a"/>
<circle cx="620.0" cy="66.1" r="3.5" fill="#d0564a"/>
<line x1="90" y1="98" x2="112" y2="98" stroke="#d0564a" stroke-width="2.5" stroke-dasharray="4 3"/>
<text x="118" y="102" font-size="12" text-anchor="start">맨 뒤 삽입 최댓값</text>
<polyline points="70.0,234.6 104.4,234.6 138.8,234.6 207.5,234.6 345.0,234.6 620.0,234.6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="234.6" r="3.5" fill="#4a7bd0"/>
<circle cx="104.4" cy="234.6" r="3.5" fill="#4a7bd0"/>
<circle cx="138.8" cy="234.6" r="3.5" fill="#4a7bd0"/>
<circle cx="207.5" cy="234.6" r="3.5" fill="#4a7bd0"/>
<circle cx="345.0" cy="234.6" r="3.5" fill="#4a7bd0"/>
<circle cx="620.0" cy="234.6" r="3.5" fill="#4a7bd0"/>
<line x1="90" y1="118" x2="112" y2="118" stroke="#4a7bd0" stroke-width="2.5"/>
<text x="118" y="122" font-size="12" text-anchor="start">맨 앞에 삽입 (가장 먼저 깸) 평균</text>
<text x="615.703" y="157" font-size="12" text-anchor="end">+4.0 cycle / 항목 (5 명령)</text>
</svg>
```

그림 7 — 예제 7. 태스크 128개가 잠들어 있으면 맨 뒤 삽입 한 번에 513 cycle(25 MHz에서 20.5 us)이 더 든다. 인터럽트는 그동안 막히지 않는다.

### 4.3 예제 8 — 같은 tick에 K개가 깨어나면 tick ISR이 길어진다

**무엇을 확인하나**: 태스크 K개가 모두 tick 100에 깨도록 잠들게 하고, SysTick ISR 하나하나의 길이를 잰다. 포트의 SysTick 핸들러 이름을 앱 설정에서 `c1_port_systick`으로 바꾸고, 진짜 벡터 `SysTick_Handler`는 앱이 정의해서 카운터로 감싼다. 평소 tick(10~89번째)의 평균·최대와 깨움 tick(100번째)의 길이를 출력한다.

```c
/* C1 tick 전용: SysTick 핸들러를 감싸서 tick ISR 길이를 잰다 */
#ifndef C1_TICK_CONFIG_H
#define C1_TICK_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef  xPortSysTickHandler
#define xPortSysTickHandler c1_port_systick    /* port.c의 함수 이름을 바꾼다 */
#endif
```

```c
/* ex8: 같은 tick에 K개 태스크가 깨어날 때 tick ISR 길이 (O(K), 인터럽트 마스크 상태) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)
#ifndef K
#define K 0
#endif
#define WAKE 100
void c1_port_systick(void);
static volatile uint32_t dur[160], nt;
void SysTick_Handler(void) {                          /* 벡터 테이블이 부르는 진짜 핸들러 */
    uint32_t t0 = CYC();
    c1_port_systick();                                 /* BASEPRI 올리고 xTaskIncrementTick() */
    uint32_t dt = CYC() - t0;
    if (nt < 160) dur[nt] = dt;
    nt++;
}
static void waker(void *pv) {
    (void)pv;
    vTaskDelay(WAKE - xTaskGetTickCount());            /* 모두 tick 100에 깨도록 */
    vTaskSuspend(NULL);
}
static void ctl(void *pv) {
    (void)pv;
    for (int i = 0; i < K; i++) xTaskCreate(waker, "W", 128, NULL, 2, NULL);
    vTaskDelay(WAKE + 20);
    uint32_t base = 0, bmax = 0;
    for (int i = 10; i < 90; i++) { base += dur[i]; if (dur[i] > bmax) bmax = dur[i]; }
    board_printf_raw("K=%u  normal tick ISR avg %u max %u cyc | wake tick ISR %u cyc\n",
                     (unsigned)K, (unsigned)(base / 80), (unsigned)bmax, (unsigned)dur[WAKE - 1]);
    board_exit(0);
}
int main(void) {
    xTaskCreate(ctl, "CTL", 256, NULL, 5, NULL);
    vTaskStartScheduler();
}
```

```text
K=0  normal tick ISR avg 39 max 39 cyc | wake tick ISR 39 cyc
K=1  normal tick ISR avg 39 max 39 cyc | wake tick ISR 98 cyc
K=2  normal tick ISR avg 39 max 39 cyc | wake tick ISR 137 cyc
K=4  normal tick ISR avg 39 max 39 cyc | wake tick ISR 215 cyc
K=8  normal tick ISR avg 39 max 39 cyc | wake tick ISR 372 cyc
K=16  normal tick ISR avg 39 max 39 cyc | wake tick ISR 686 cyc
K=32  normal tick ISR avg 39 max 39 cyc | wake tick ISR 1313 cyc
K=64  normal tick ISR avg 39 max 39 cyc | wake tick ISR 2567 cyc
```

**출력에서 볼 것**: 깰 태스크가 없는 평소 tick ISR은 항상 39 cycle — `xTaskIncrementTick()`이 `xConstTickCount >= xNextTaskUnblockTime` 비교 한 번으로 끝난다(delayed 리스트를 보지도 않는다. 맨 앞 값을 `xNextTaskUnblockTime`에 캐시해 둔 덕분이다). 깨움 tick은 K에 비례해 늘어서 K=64면 **2567 cycle = 102.7 us**, 기울기 (2567 − 39) / 64 ≈ **39.5 cycle(명령 약 49개) / 태스크**다. 깨어나는 태스크마다 delayed 리스트에서 빼고, 이벤트 리스트에 있으면 거기서도 빼고, ready 리스트에 넣고, 비트맵을 켜고, 선점이 필요한지 비교한다. 이 전부가 BASEPRI가 올라간 상태에서 일어난다.

```svg
<svg viewBox="0 0 640 348" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" font-size="14" text-anchor="middle" font-weight="bold">같은 tick에 K개가 깨어날 때 tick ISR 길이</text>
<line x1="70" y1="298" x2="620" y2="298" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="58" x2="70" y2="298" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="298" x2="620" y2="298" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="302" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="238" x2="620" y2="238" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="242" font-size="12" text-anchor="end">700</text>
<line x1="70" y1="178" x2="620" y2="178" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="182" font-size="12" text-anchor="end">1400</text>
<line x1="70" y1="118" x2="620" y2="118" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="122" font-size="12" text-anchor="end">2100</text>
<line x1="70" y1="58" x2="620" y2="58" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="62" font-size="12" text-anchor="end">2800</text>
<line x1="70" y1="298" x2="70" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="70" y="316" font-size="12" text-anchor="middle">0</text>
<line x1="138.75" y1="298" x2="138.75" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="138.75" y="316" font-size="12" text-anchor="middle">8</text>
<line x1="207.5" y1="298" x2="207.5" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="207.5" y="316" font-size="12" text-anchor="middle">16</text>
<line x1="345" y1="298" x2="345" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="345" y="316" font-size="12" text-anchor="middle">32</text>
<line x1="620" y1="298" x2="620" y2="302" stroke="currentColor" stroke-width="1.2"/>
<text x="620" y="316" font-size="12" text-anchor="middle">64</text>
<text x="345" y="340" font-size="13" text-anchor="middle">같은 tick에 깨어나는 태스크 수 K</text>
<text x="14" y="48" font-size="12" text-anchor="start">25 MHz 카운터 cycle</text>
<polyline points="70.0,294.7 78.6,289.6 87.2,286.3 104.4,279.6 138.8,266.1 207.5,239.2 345.0,185.5 620.0,78.0" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="70.0" cy="294.7" r="3.5" fill="#d0564a"/>
<circle cx="78.6" cy="289.6" r="3.5" fill="#d0564a"/>
<circle cx="87.2" cy="286.3" r="3.5" fill="#d0564a"/>
<circle cx="104.4" cy="279.6" r="3.5" fill="#d0564a"/>
<circle cx="138.8" cy="266.1" r="3.5" fill="#d0564a"/>
<circle cx="207.5" cy="239.2" r="3.5" fill="#d0564a"/>
<circle cx="345.0" cy="185.5" r="3.5" fill="#d0564a"/>
<circle cx="620.0" cy="78.0" r="3.5" fill="#d0564a"/>
<line x1="90" y1="78" x2="112" y2="78" stroke="#d0564a" stroke-width="2.5"/>
<text x="118" y="82" font-size="12" text-anchor="start">tick ISR (BASEPRI로 마스크된 구간)</text>
<text x="611.406" y="96.5714" font-size="12" text-anchor="end">K=64 → 2567 cycle = 103 us</text>
<text x="173.125" y="287.714" font-size="12" text-anchor="start">평소 39 cycle</text>
</svg>
```

그림 8 — 예제 8. 같은 tick에 64개가 깨면 그 tick 동안 커널 인터럽트 우선순위 이하의 ISR은 최대 약 100 us 늦게 들어온다(QEMU icount 기준).

### 4.4 그래서 결정적인가 — 실무 규칙

O(n)이 있다고 RTOS가 비결정적인 것은 아니다. 태스크 수 n은 **설계 시점에 정해진 상한**이 있으므로 최악 시간도 상한이 있다(bounded). 문제는 그 상한이 n과 함께 커진다는 것, 그리고 어느 O(n)이 인터럽트를 막느냐다. 실무 규칙으로 정리하면:

- **태스크 수를 적게** 유지한다. 드론 FC급 시스템도 태스크 10~20개면 충분하다. 128개를 만들면 정렬 삽입·tick 일괄 깨움의 최악이 그만큼 커진다.
- **같은 tick에 많은 태스크를 깨우지 않는다.** 주기가 같은 태스크 여럿을 `vTaskDelayUntil`로 같은 위상에 두면 매 주기 같은 tick에 일괄 깨움이 생긴다. 위상을 엇갈리거나, 한 태스크가 큐·알림으로 나머지를 깨우는 구조가 낫다.
- **긴 timeout을 가진 대기자가 많으면** delayed 리스트가 길어진다. 무한 대기(`portMAX_DELAY`)는 delayed가 아니라 suspended 리스트로 가니(O(1) 끝 삽입) 대기 시간에 의미가 없다면 무한 대기가 정렬 비용도 없다.
- **한 큐에 대기자를 많이 두지 않는다.** 이벤트 리스트 삽입이 대기자 수에 비례한다. 여러 소비자가 한 큐를 기다리는 구조라면 대기자는 보통 2~3개를 넘기지 않는다.
- **측정한다.** tick ISR 길이와 PendSV 길이는 GPIO 토글이나 사이클 카운터(DWT CYCCNT)로 쉽게 잴 수 있다. 이 노트의 예제 8을 실제 보드에서 GPIO로 바꾸면 그대로 쓸 수 있다.

Don에게 익숙한 말로: "핫패스는 O(1), O(n)은 인터럽트를 열어 둔 백그라운드에서, 그리고 n의 상한을 문서로 남겨라" — SSD 펌웨어에서 명령 디스패치 경로를 다루던 규칙과 같다.

---

## 5. Zephyr — 같은 문제, 다른 답

### 5.1 `_kernel`과 `sys_dlist_t`

Zephyr 커널의 전역 상태는 구조체 하나 `struct z_kernel _kernel`(`include/zephyr/kernel_structs.h`)에 모여 있다.

```c
struct z_kernel {
    struct _cpu cpus[CONFIG_MP_MAX_NUM_CPUS];   /* 코어별: nested, irq_stack, current, idle_thread ... */
    /* ... */
    struct _ready_q ready_q;                    /* CONFIG_SCHED_CPU_MASK_PIN_ONLY가 아니면 여기 */
#if defined(CONFIG_THREAD_MONITOR)
    struct k_thread *threads;                   /* 모든 스레드의 단일 연결 리스트 */
#endif
    /* ... */
};

struct _ready_q {
#ifndef CONFIG_SMP
    struct k_thread *cache;                     /* "다음에 돌 스레드" — 항상 미리 계산해 둔다 */
#endif
#if defined(CONFIG_SCHED_SIMPLE)
    sys_dlist_t runq;
#elif defined(CONFIG_SCHED_SCALABLE)
    struct _priq_rb runq;                       /* rbtree + next_order_key */
#elif defined(CONFIG_SCHED_MULTIQ)
    struct _priq_mq runq;                       /* sys_dlist_t queues[K_NUM_THREAD_PRIO] + bitmask[] */
#endif
};
```

리스트 기본형은 `sys_dlist_t`(`include/zephyr/sys/dlist.h`)다. 노드 `struct _dnode`는 포인터 두 개뿐이고(union으로 head/next, tail/prev를 겸한다), 리스트 머리도 같은 구조체라서 FreeRTOS의 `xListEnd`처럼 **머리 자체가 센티널**인 원형 리스트다. FreeRTOS와 다른 점은 노드에 값·owner·container가 없다는 것이다 — 값은 스레드 구조체(`base.prio`)에서 읽고, owner는 `CONTAINER_OF(node, struct k_thread, base.qnode_dlist)`로 계산한다(Linux `container_of`와 같은 방식). 노드 8 B 대 20 B, 대신 "어느 리스트에 있나"는 노드만 봐서는 모른다(스레드 상태 비트와 `base.pended_on`으로 안다).

스레드 쪽 고리는 `struct _thread_base`(`include/zephyr/kernel/thread.h`)에 있다:

| 필드 | 쓰임 | FreeRTOS 대응 |
|---|---|---|
| `union { sys_dnode_t qnode_dlist; struct rbnode qnode_rb; }` | ready queue **또는** wait_q의 노드 (동시에 둘 다 있을 일은 없으니 union) | `xStateListItem`(ready) + `xEventListItem`(대기) 역할을 하나로 |
| `_wait_q_t *pended_on` | 지금 기다리는 객체의 wait_q | `xEventListItem.pxContainer` |
| `int8_t prio` | 우선순위 (숫자가 **작을수록 높다**, 음수 = 협력형) | `uxPriority` (클수록 높다) |
| `uint32_t order_key` | SCALABLE rbtree에서 같은 우선순위의 FIFO 순서 | 리스트 순서 자체 |
| `struct _timeout timeout` | timeout 리스트 노드 + `dticks` | delayed 리스트 안의 `xStateListItem` |

중요한 설계 차이: FreeRTOS TCB는 ready/delayed/suspended용 항목과 이벤트용 항목을 따로 갖지만, Zephyr 스레드는 **ready queue 또는 wait_q 중 하나**에만 걸리는 노드 하나 + **timeout 전용 노드** 하나를 갖는다. timeout 있는 대기는 wait_q와 timeout 리스트 두 곳에 동시에 걸린다 — 구조는 다르지만 "두 개의 고리"라는 발상은 같다.

### 5.2 ready queue 구현 세 가지 — Kconfig로 고른다

`kernel/Kconfig`의 `choice SCHED_ALGORITHM`(v4.4.2, 기본 `SCHED_SIMPLE`)과 `kernel/include/priority_q.h`의 구현을 같이 읽으면:

| Kconfig | 자료구조 | add | best (다음 스레드) | remove | yield | 메모 |
|---|---|---|---|---|---|---|
| `CONFIG_SCHED_SIMPLE` (기본) | `sys_dlist_t` 하나, **우선순위순 정렬** | O(n) — `z_priq_simple_add`가 앞에서부터 "나보다 낮은 첫 스레드" 앞에 끼움 | O(1) — 머리 | O(1) | O(같은 우선순위 수) | Kconfig 도움말은 "simple unordered list"라고 쓰지만 **코드는 정렬 삽입**이다 |
| `CONFIG_SCHED_SCALABLE` | red/black tree (`struct _priq_rb`) | O(log n) | O(log n) — `rb_get_min` | O(log n) | remove + add | 같은 우선순위 FIFO는 `order_key` 증가값으로. 도움말: 대략 20개 넘는 runnable 스레드용, 코드 약 2 KB 추가 |
| `CONFIG_SCHED_MULTIQ` | `sys_dlist_t queues[K_NUM_THREAD_PRIO]` + `bitmask[]` | O(1) — 해당 큐에 append, 비트 켬 | O(1) — `cached_queue_index` (UP) | O(1), 큐가 비면 비트맵 재탐색 | O(1) | FreeRTOS 방식과 같은 "교과서 multi-queue". `SCHED_DEADLINE`과 함께 못 씀. 리스트 머리만큼 RAM |

MULTIQ의 최고 우선순위 찾기는 `z_priq_mq_best_queue_index()`의 `TRAILING_ZEROS(pq->bitmask[i])`(32비트 빌드에서 `u32_count_trailing_zeros`)다. FreeRTOS가 **앞쪽 0 개수(CLZ)** 를 쓰는 것과 반대로 **뒤쪽 0 개수(CTZ)** 를 쓰는 이유는 Zephyr에서 숫자가 작을수록 우선순위가 높기 때문이다(큐 인덱스 0 = `K_HIGHEST_THREAD_PRIO` = 가장 높은 협력형). Cortex-M3에는 CTZ 명령이 없어서 컴파일러가 보통 `rbit` + `clz`로 만든다. 그리고 UP(단일 코어) 빌드에서는 그마저도 매번 하지 않는다 — `cached_queue_index`에 최고 인덱스를 들고 있다가 add 때 더 높으면 갱신, 큐가 빌 때만 비트맵을 다시 훑는다.

Zephyr에는 선택 결과 캐시가 하나 더 있다: UP 빌드의 `_kernel.ready_q.cache`. 스레드가 ready/unready 될 때마다 `update_cache()`가 `next_up()`으로 "다음에 돌 스레드"를 미리 계산해 두고, 실제 스위치(`z_swap`)는 캐시를 읽기만 한다. FreeRTOS가 PendSV 안에서 고르는 것과 달리 **고르는 시점을 상태 변화 시점으로 앞당긴** 설계다. 또 UP에서는 FreeRTOS처럼 **현재 스레드도 ready queue에 남아 있다**(`kernel/sched.c` 주석: "In uniprocessor mode, we can leave the current thread in the queue"). SMP에서는 현재 스레드를 queue에서 빼 둔다(C10).

### 5.3 wait_q — 대기자 리스트

커널 객체(세마포어·뮤텍스·큐 …)의 대기자 리스트는 `_wait_q_t`이고, 구현은 `choice WAITQ_ALGORITHM`이 정한다: `CONFIG_WAITQ_SIMPLE`(기본, `sys_dlist_t` + `z_priq_simple_add` = 우선순위 정렬 삽입) 또는 `CONFIG_WAITQ_SCALABLE`(rbtree). 깨울 때는 `z_unpend_first_thread()` → `_priq_wait_best()` = 머리 하나. FreeRTOS 이벤트 리스트와 정확히 같은 정책(넣을 때 정렬, 꺼낼 때 O(1))이다.

대기 진입 경로는 `z_pend_curr()` → `pend_locked()` → `add_to_waitq_locked()`(`_priq_wait_add`) + `add_thread_timeout()`(`z_add_thread_timeout` → `z_add_timeout`)이다. 여기서 **FreeRTOS와 결정적인 차이**가 하나 있다: Zephyr는 이 정렬 삽입들을 `_sched_spinlock`을 쥔 채 한다. 단일 코어에서 `k_spin_lock()`은 `arch_irq_lock()`, 즉 **인터럽트 마스크**다. FreeRTOS가 O(n) 삽입을 "스케줄러 정지 + 인터럽트 허용"으로 하는 것과 달리, Zephyr의 wait_q·timeout 정렬 삽입 길이는 인터럽트 지연에 더해진다. 대기자·timeout 수가 적은 일반적인 시스템에서는 몇 노드짜리라 문제가 안 되지만, "스레드 수 상한을 문서로 남겨라"는 규칙이 Zephyr에서 더 중요한 이유다.

### 5.4 timeout 리스트 — delta 리스트와 64비트 tick

`kernel/timeout.c`는 timeout을 **하나의** `sys_dlist_t timeout_list`(static)에 넣는다. 각 노드(`struct _timeout`)의 `dticks`는 절대 시각이 아니라 **바로 앞 노드와의 차이**다(delta list). `z_add_timeout()`이 앞에서부터 `dticks`를 빼 가며 자리를 찾고(O(n)), 끼운 뒤 다음 노드의 `dticks`에서 자기 몫을 뺀다. 시간이 흐르면 `sys_clock_announce()` → `sys_clock_announce_locked()`가 **머리 노드의 `dticks`만** 줄이고, 0이 된 머리들을 꺼내 콜백(`t->fn`, 스레드면 깨우기)을 부른다. 시계는 `static uint64_t curr_tick` 64비트라 FreeRTOS 같은 wrap 대비 리스트 두 개가 필요 없다.

| 항목 | FreeRTOS delayed 리스트 | Zephyr timeout 리스트 |
|---|---|---|
| 키 | 깨울 tick 절댓값 (`TickType_t`, 보통 32비트) | 앞 노드와의 차이 `dticks` |
| wrap 처리 | 리스트 두 개 + 포인터 교환 | 64비트 `curr_tick`이라 사실상 없음 |
| 삽입 | O(n), 스케줄러 정지 (인터럽트 허용) | O(n), `timeout_lock` 스핀락 (인터럽트 마스크) |
| tick 처리 | 맨 앞 값과 `xNextTaskUnblockTime` 비교 | 머리의 `dticks`만 감소 |
| 만료 K개 처리 | ISR 안에서 BASEPRI 올린 채 O(K) | 루프마다 `timeout_lock`을 풀고 콜백 호출 → 만료 사이사이 인터럽트 허용 |
| 무엇이 들어가나 | 태스크만 (소프트웨어 타이머는 별도 타이머 리스트) | 스레드 timeout, `k_timer`, 타임슬라이스 timeout 등 전부 한 리스트 |
| tickless | `xNextTaskUnblockTime`으로 다음 깨움 계산 (C6) | 머리의 `dticks`가 곧 다음 인터럽트까지 거리 → `sys_clock_set_timeout()` |

### 5.5 예제 9 — Zephyr 커널 큐를 살아 있는 채로 덤프하기

**무엇을 확인하나**: 같은 앱을 세 ready queue 구현으로 빌드해서, main(우선순위 0)이 5 ms 자고 일어난 순간 (1) `_kernel.ready_q`의 내용(구현별로 걷는 법이 다르다), (2) 세마포어 `sem.wait_q`의 순서, (3) timeout 리스트를 찍는다. 스레드 구성: A5·B5(우선순위 5, 무한 루프), C7(7, 무한 루프 — 영원히 ready로 굶는다), W2·W4·W3(세마포어를 무한 대기), W3t(3, 500 ms timeout 대기), S30·S10·S20(1, 각각 30·10·20 ms 수면). `_kernel`은 `kernel_structs.h`가 `extern`으로 공개하므로 C에서 바로 볼 수 있지만, `timeout_list`는 `static`이라 S10의 timeout 노드에서 출발해 원을 한 바퀴 돌며 찍는다(우리 스레드가 아닌 노드는 `?`). tick은 1 kHz로 맞췄다.

```c
/* zdump: Zephyr _kernel.ready_q, k_sem wait_q, timeout(delta) 리스트를 살아 있는 채로 덤프 */
#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#define NT 10
K_THREAD_STACK_ARRAY_DEFINE(stk, NT, 512);
static struct k_thread th[NT];
static K_SEM_DEFINE(sem, 0, 1);
static const char *nm(struct k_thread *t) { const char *n = k_thread_name_get(t); return n ? n : "?"; }
static struct k_thread *owner(sys_dnode_t *n)     /* n이 우리 스레드의 timeout 노드면 그 스레드 */
{
	for (int i = 0; i < NT; i++) if (n == &th[i].base.timeout.node) return &th[i];
	return NULL;
}
static void spin(void *a, void *b, void *c) { for (;;) { } }
static void waiter(void *to, void *b, void *c)
{
	int ms = (int)(intptr_t)to;
	k_sem_take(&sem, ms < 0 ? K_FOREVER : K_MSEC(ms));
	for (;;) k_sleep(K_FOREVER);
}
static void sleeper(void *ms, void *b, void *c) { k_msleep((int)(intptr_t)ms); for (;;) k_sleep(K_FOREVER); }
static void mk(int i, const char *n, k_thread_entry_t f, int arg, int prio) {
	k_thread_create(&th[i], stk[i], 512, f, (void *)(intptr_t)arg, NULL, NULL, prio, 0, K_NO_WAIT);
	k_thread_name_set(&th[i], n);
}
int main(void)
{
	k_thread_name_set(k_current_get(), "main");
	mk(0, "A5", spin, 0, 5);  mk(1, "B5", spin, 0, 5);  mk(2, "C7", spin, 0, 7);
	mk(3, "W2", waiter, -1, 2); mk(4, "W4", waiter, -1, 4); mk(5, "W3", waiter, -1, 3);
	mk(6, "W3t", waiter, 500, 3);
	mk(7, "S30", sleeper, 30, 1); mk(8, "S10", sleeper, 10, 1); mk(9, "S20", sleeper, 20, 1);
	k_msleep(5);                                     /* 모두 블록/준비된 뒤 main(prio 0)이 깬다 */
	unsigned int key = irq_lock();
	struct k_thread *t;
	printk("ready_q.cache=%s  runq:", nm(_kernel.ready_q.cache));
#if defined(CONFIG_SCHED_SIMPLE)
	SYS_DLIST_FOR_EACH_CONTAINER(&_kernel.ready_q.runq, t, base.qnode_dlist)
		printk(" %s(p%d)@%p", nm(t), t->base.prio, &t->base.qnode_dlist);
#elif defined(CONFIG_SCHED_SCALABLE)
	RB_FOR_EACH_CONTAINER(&_kernel.ready_q.runq.tree, t, base.qnode_rb)
		printk(" %s(p%d,key%u)", nm(t), t->base.prio, t->base.order_key);
#elif defined(CONFIG_SCHED_MULTIQ)
	printk(" bitmask=0x%08lx cached_idx=%u\n", _kernel.ready_q.runq.bitmask[0], _kernel.ready_q.runq.cached_queue_index);
	for (int q = 0; q < K_NUM_THREAD_PRIO; q++)
		SYS_DLIST_FOR_EACH_CONTAINER(&_kernel.ready_q.runq.queues[q], t, base.qnode_dlist)
			printk("   queues[%d] (prio %d): %s\n", q, q + K_HIGHEST_THREAD_PRIO, nm(t));
#endif
	printk("\nsem.wait_q:");
	SYS_DLIST_FOR_EACH_CONTAINER(&sem.wait_q.waitq, t, base.qnode_dlist)
		printk(" %s(p%d)", nm(t), t->base.prio);
	printk("\ntimeout ring (S10부터 한 바퀴, dticks = 앞 항목과의 차이):\n ");
	sys_dnode_t *n = &th[8].base.timeout.node;
	do {
		printk(" -> %s@%p:%d", owner(n) ? nm(owner(n)) : "?", n,
		       (int)CONTAINER_OF(n, struct _timeout, node)->dticks);
		n = n->next;
	} while (n != &th[8].base.timeout.node);
	printk("\nsizeof(_kernel)=%u sizeof(struct k_thread)=%u sizeof(_wait_q_t)=%u\n",
	       (unsigned)sizeof(_kernel), (unsigned)sizeof(struct k_thread), (unsigned)sizeof(_wait_q_t));
	irq_unlock(key);
	return 0;
}
```

```text
CONFIG_PRINTK=y
CONFIG_THREAD_NAME=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
```

```sh
for s in SIMPLE SCALABLE MULTIQ; do
  /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C1/zdump /var/tmp/rtos/C1/zdump_$s -- -DCONFIG_SCHED_$s=y
  timeout 8 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
     -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/C1/zdump_$s/zephyr/zephyr.elf
done
```

```text
## CONFIG_SCHED_SIMPLE=y
*** Booting Zephyr OS build v4.4.2 ***
ready_q.cache=main  runq: main(p0)@0x20000798 A5(p5)@0x200000b8 B5(p5)@0x20000158 C7(p7)@0x200001f8
sem.wait_q: W2(p2) W3(p3) W3t(p3) W4(p4)
timeout ring (S10부터 한 바퀴, dticks = 앞 항목과의 차이):
  -> S10@0x200005d0:5 -> S20@0x20000670:10 -> ?@0x200008d8:5 -> S30@0x20000530:5 -> W3t@0x20000490:470 -> ?@0x20000088:16777472
sizeof(_kernel)=32 sizeof(struct k_thread)=160 sizeof(_wait_q_t)=8
## CONFIG_SCHED_SCALABLE=y
*** Booting Zephyr OS build v4.4.2 ***
ready_q.cache=main  runq: main(p0,key11) A5(p5,key1) B5(p5,key2) C7(p7,key3)
sem.wait_q: W2(p2) W3(p3) W3t(p3) W4(p4)
timeout ring (S10부터 한 바퀴, dticks = 앞 항목과의 차이):
  -> S10@0x20000618:5 -> S20@0x200006c0:10 -> ?@0x20000940:5 -> S30@0x20000570:5 -> W3t@0x200004c8:470 -> ?@0x20000088:16777472
sizeof(_kernel)=40 sizeof(struct k_thread)=168 sizeof(_wait_q_t)=8
## CONFIG_SCHED_MULTIQ=y
*** Booting Zephyr OS build v4.4.2 ***
ready_q.cache=main  runq: bitmask=0x00a10000 cached_idx=16
   queues[16] (prio 0): main
   queues[21] (prio 5): A5
   queues[21] (prio 5): B5
   queues[23] (prio 7): C7

sem.wait_q: W2(p2) W3(p3) W3t(p3) W4(p4)
timeout ring (S10부터 한 바퀴, dticks = 앞 항목과의 차이):
  -> S10@0x200005d0:5 -> S20@0x20000670:10 -> ?@0x200008d8:5 -> S30@0x20000530:5 -> W3t@0x20000490:470 -> ?@0x20000088:16777472
sizeof(_kernel)=288 sizeof(struct k_thread)=160 sizeof(_wait_q_t)=8

20000960 00000020 B _kernel
200008d8 00000018 b slice_timeouts
20000088 00000008 d timeout_list
```

**출력에서 볼 것**:

- **SIMPLE**: `runq`가 main(p0) → A5(p5) → B5(p5) → C7(p7)로 **우선순위순 정렬**되어 있다(Kconfig 도움말의 "unordered"와 달리). 지금 돌고 있는 main도 runq 안에 있고(UP), `ready_q.cache`가 main이다. 같은 우선순위 A5·B5는 먼저 add된 A5가 앞. 굶고 있는 C7은 계속 ready로 남아 있다. idle은 어느 구현에서도 runq에 없다.
- **SCALABLE**: 같은 순서를 rbtree in-order로 걸었다. `order_key`는 add될 때마다 1씩 늘어난 번호다 — A5=1, B5=2, C7=3은 생성 순서이고, main=11은 자고 일어나며 다시 add되는 사이에 다른 스레드들의 add가 쌓인 결과다. 같은 우선순위끼리는 이 키가 작은 쪽이 먼저(FIFO).
- **MULTIQ**: `bitmask=0x00a10000` = 비트 16·21·23. 큐 인덱스는 `prio − K_HIGHEST_THREAD_PRIO`(−16)이라 prio 0 → 16, prio 5 → 21, prio 7 → 23. `cached_queue_index=16`이 곧 다음 스레드(main)다. 32개 큐 머리(8 B × 32 = 256 B) 때문에 `_kernel`이 288 B로 SIMPLE(32 B)의 9배다.
- **wait_q**: 생성 순서는 W2·W4·W3·W3t였지만 대기 리스트는 W2(p2) → W3(p3) → W3t(p3) → W4(p4) — 우선순위순, 같으면 먼저 기다린 순서. ready queue 설정과 무관하게 세 빌드 모두 같다(wait_q는 별도 Kconfig `WAITQ_SIMPLE` 기본값).
- **timeout 원**: 마지막 `nm`이 답을 준다. `?@0x20000088`이 리스트 머리 `timeout_list`(그래서 그 `dticks` 16777472는 timeout이 아닌 메모리를 읽은 쓰레기 값)이고, `?@0x200008d8`은 커널의 `slice_timeouts`(타임슬라이스 타이머, `TIMESLICE_SIZE` 기본 20 ms)다. 머리부터 읽으면 S10(5) → S20(10) → 슬라이스(5) → S30(5) → W3t(470), 누적하면 5·15·20·25·495 tick. main이 tick 6에 깼으니(5 ms + `z_add_timeout`이 붙이는 1 tick) S10은 11 − 6 = 5, S20은 21 − 6 = 15, S30은 31 − 6 = 25, W3t는 501 − 6 = 495 tick 남았다는 뜻이고, 정확히 맞는다. 시간이 가면 머리의 5만 줄어든다 — delta 리스트의 요점이다.
- `sizeof(struct k_thread)`가 SCALABLE에서만 168 B(나머지 160 B)인 것은 `order_key`(4 B, `CONFIG_SCHED_SCALABLE` 또는 `CONFIG_WAITQ_SCALABLE`일 때만 있음)와 정렬 패딩 때문이다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">Zephyr _kernel.ready_q — 같은 상태, 세 가지 구현 (예제 9 덤프)</text>
<text x="16" y="48" font-size="13" text-anchor="start" font-weight="bold">SCHED_SIMPLE: sys_dlist_t 하나, 우선순위순 정렬 (32 B _kernel)</text>
<rect x="16" y="58" width="70" height="28" rx="4" fill="#888" fill-opacity="0.05" stroke="#888" stroke-dasharray="4 3"/>
<text x="51" y="77" font-size="12" text-anchor="middle">runq</text>
<line x1="86" y1="72" x2="99" y2="72" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="100" y="58" width="86" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="143" y="77" font-size="12" text-anchor="middle">main p0</text>
<rect x="200" y="58" width="86" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="243" y="77" font-size="12" text-anchor="middle">A5 p5</text>
<line x1="186" y1="72" x2="199" y2="72" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="300" y="58" width="86" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="343" y="77" font-size="12" text-anchor="middle">B5 p5</text>
<line x1="286" y1="72" x2="299" y2="72" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="400" y="58" width="86" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="443" y="77" font-size="12" text-anchor="middle">C7 p7</text>
<line x1="386" y1="72" x2="399" y2="72" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="506" y="77" font-size="12" text-anchor="start">best = 머리 O(1)</text>
<text x="16" y="108" font-size="12" text-anchor="start">add = 앞에서부터 "나보다 낮은 첫 스레드" 찾기 O(n), cache = main</text>
<text x="16" y="140" font-size="13" text-anchor="start" font-weight="bold">SCHED_SCALABLE: rbtree, (prio, order_key) 순서 (40 B _kernel)</text>
<rect x="16" y="150" width="96" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="64" y="169" font-size="12" text-anchor="middle">main 0,11</text>
<rect x="126" y="150" width="96" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="174" y="169" font-size="12" text-anchor="middle">A5 5,1</text>
<line x1="112" y1="164" x2="125" y2="164" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="236" y="150" width="96" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="284" y="169" font-size="12" text-anchor="middle">B5 5,2</text>
<line x1="222" y1="164" x2="235" y2="164" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="346" y="150" width="96" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="394" y="169" font-size="12" text-anchor="middle">C7 7,3</text>
<line x1="332" y1="164" x2="345" y2="164" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="16" y="198" font-size="12" text-anchor="start">in-order 순회 결과. best = rb_get_min O(log n), add 때마다 order_key 1 증가</text>
<text x="16" y="230" font-size="13" text-anchor="start" font-weight="bold">SCHED_MULTIQ: dlist 32개 + 비트맵 (288 B _kernel)</text>
<rect x="16" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="36" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="56" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="76" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="96" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="116" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="136" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="156" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="176" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="196" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="216" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="236" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="256" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="276" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="296" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="316" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="336" y="242" width="18" height="24" rx="2" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<rect x="356" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="376" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="396" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="416" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="436" y="242" width="18" height="24" rx="2" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<rect x="456" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="476" y="242" width="18" height="24" rx="2" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<rect x="496" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="516" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="536" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="556" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="576" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="596" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="616" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<rect x="636" y="242" width="18" height="24" rx="2" fill="#888" fill-opacity="0.04" stroke="#888"/>
<text x="25" y="282" font-size="12" text-anchor="middle">0</text>
<text x="325" y="282" font-size="12" text-anchor="middle">15</text>
<text x="345" y="282" font-size="12" text-anchor="middle">16</text>
<text x="445" y="282" font-size="12" text-anchor="middle">21</text>
<text x="485" y="282" font-size="12" text-anchor="middle">23</text>
<text x="645" y="282" font-size="12" text-anchor="middle">31</text>
<text x="16" y="302" font-size="12" text-anchor="start">queues[16]=main (prio 0), queues[21]=A5→B5 (prio 5), queues[23]=C7 (prio 7)</text>
<text x="16" y="322" font-size="12" text-anchor="start">bitmask = 0x00a10000 (비트 16, 21, 23), cached_queue_index = 16</text>
<text x="16" y="342" font-size="12" text-anchor="start">인덱스 = prio − K_HIGHEST_THREAD_PRIO(−16). 숫자가 작을수록 높으니 CTZ로 찾는다</text>
<text x="16" y="366" font-size="12" text-anchor="start">idle 스레드는 어느 구현에서도 runq에 없다 — 비었을 때 next_up()이 따로 고른다</text>
</svg>
```

그림 9 — 예제 9의 덤프를 구현별로 그린 ready queue. 같은 네 스레드가 정렬 리스트, rbtree, 우선순위 배열 + 비트맵으로 들어 있다.

### 5.6 예제 10 — ready queue 구현별 전환 비용과 크기

**무엇을 확인하나**: 같은 우선순위(5) worker N개가 `k_yield()`만 반복할 때 전환 1회의 비용을 잰다. 각 worker는 돌기 시작하자마자 "직전 worker가 돈 시각"과의 차이를 기록하니, 그 차이가 곧 yield → 다음 worker 실행까지의 시간이다. main(우선순위 0)이 worker를 2·4·8·16·32·64개로 늘려 가며 20 ms씩 자는 동안 worker들만 돈다(main이 끼어든 창 경계의 샘플은 버린다). yield는 "현재 스레드를 같은 우선순위 줄의 맨 뒤로"라서 SIMPLE에서는 같은 우선순위 스레드를 전부 지나가야 자리가 나오고(O(N)), MULTIQ는 dequeue + append(O(1)), SCALABLE은 rbtree remove + insert(O(log N))다. ready queue 비용만 보려고 `CONFIG_TIMESLICING=n`으로 한 번, 기본값(`y`, `TIMESLICE_SIZE` 20 ms)으로 한 번 빌드했다.

```c
/* zyield: 같은 우선순위 스레드 N개가 k_yield()로 돌 때 1회 전환 비용 — ready queue 구현별 */
#include <zephyr/kernel.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)   /* MPS2 FPGAIO 25 MHz counter */
#define NMAX 64
#ifndef WIN_MS
#define WIN_MS 20
#endif
K_THREAD_STACK_ARRAY_DEFINE(stk, NMAX, 384);
static struct k_thread th[NMAX];
static volatile uint32_t last, dmin, dsum, dcnt;
static void worker(void *a, void *b, void *c)
{
	for (;;) {                                     /* 앞 스레드가 돈 시각과의 차이 = 전환 1회 */
		uint32_t now = CYC(), d = now - last;
		last = now;
		if (d < 100000) {                      /* main이 끼어든 구간(창 경계)은 버린다 */
			dsum += d; dcnt++;
			if (d < dmin) dmin = d;
		}
		k_yield();                             /* 같은 우선순위끼리 돌아가며 */
	}
}
int main(void)                                     /* main = prio 0 (worker보다 높다) */
{
	static const int ns[] = { 2, 4, 8, 16, 32, 64 };
	int made = 0;
	const char *alg = IS_ENABLED(CONFIG_SCHED_SIMPLE) ? "SIMPLE" :
			  IS_ENABLED(CONFIG_SCHED_SCALABLE) ? "SCALABLE" : "MULTIQ";
	for (unsigned k = 0; k < ARRAY_SIZE(ns); k++) {
		while (made < ns[k]) {
			k_thread_create(&th[made], stk[made], 384, worker, NULL, NULL, NULL,
					5, 0, K_NO_WAIT);
			made++;
		}
		dmin = UINT32_MAX; dsum = 0; dcnt = 0; last = CYC();
		k_msleep(WIN_MS);                      /* 그동안 worker들만 돈다 */
		uint32_t n = dcnt;
		printk("%s N=%d yields=%u min=%u avg=%u.%u cyc\n", alg, ns[k], n, dmin,
		       dsum / n, dsum % n * 10 / n);
	}
	printk("DONE\n");
	return 0;
}
```

```sh
#!/bin/bash
# zy.sh: zyield — 3 구현 x (타임슬라이스 끔 / 켬, 기본값)
R=/var/tmp/rtos/C1
for ts in n y; do for s in SIMPLE SCALABLE MULTIQ; do
  D=$R/zy_${s}_ts$ts
  /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C1/zyield $D -- -DCONFIG_SCHED_$s=y -DCONFIG_TIMESLICING=$ts >/dev/null || echo FAIL $s $ts
  echo "## CONFIG_SCHED_$s=y CONFIG_TIMESLICING=$ts"
  timeout 20 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
     -icount shift=5,align=off,sleep=off -kernel $D/zephyr/zephyr.elf 2>&1 | grep -E "N=|DONE"
  arm-none-eabi-size $D/zephyr/zephyr.elf | tail -1 | awk '{print "size: text=" $1 " data=" $2 " bss=" $3}'
  arm-none-eabi-nm -S $D/zephyr/zephyr.elf | grep -E " _kernel$"
done; done
```

```text
## CONFIG_SCHED_SIMPLE=y CONFIG_TIMESLICING=n
SIMPLE N=2 yields=9734 min=76 avg=76.8 cyc
SIMPLE N=4 yields=8190 min=91 avg=91.2 cyc
SIMPLE N=8 yields=6221 min=91 avg=120.0 cyc
SIMPLE N=16 yields=4197 min=177 avg=177.6 cyc
SIMPLE N=32 yields=2534 min=249 avg=292.8 cyc
SIMPLE N=64 yields=1398 min=249 avg=523.0 cyc
DONE
size: text=12808 data=164 bss=38694
200022a0 00000020 B _kernel
## CONFIG_SCHED_SCALABLE=y CONFIG_TIMESLICING=n
SCALABLE N=2 yields=3551 min=210 avg=210.4 cyc
SCALABLE N=4 yields=2101 min=280 avg=355.1 cyc
SCALABLE N=8 yields=1745 min=381 avg=427.1 cyc
SCALABLE N=16 yields=1474 min=381 avg=504.5 cyc
SCALABLE N=32 yields=1255 min=405 avg=588.7 cyc
SCALABLE N=64 yields=1095 min=429 avg=666.9 cyc
DONE
size: text=14440 data=164 bss=39238
200024b8 00000028 B _kernel
## CONFIG_SCHED_MULTIQ=y CONFIG_TIMESLICING=n
MULTIQ N=2 yields=10267 min=72 avg=72.8 cyc
MULTIQ N=4 yields=10259 min=72 avg=72.8 cyc
MULTIQ N=8 yields=10255 min=72 avg=72.8 cyc
MULTIQ N=16 yields=10246 min=72 avg=72.8 cyc
MULTIQ N=32 yields=10228 min=72 avg=72.8 cyc
MULTIQ N=64 yields=10191 min=72 avg=72.8 cyc
DONE
size: text=12996 data=164 bss=38950
200022a0 00000120 B _kernel
## CONFIG_SCHED_SIMPLE=y CONFIG_TIMESLICING=y
SIMPLE N=2 yields=2765 min=232 avg=279.6 cyc
SIMPLE N=4 yields=2616 min=246 avg=294.7 cyc
SIMPLE N=8 yields=2365 min=275 avg=325.1 cyc
SIMPLE N=16 yields=2738 min=332 avg=370.8 cyc
SIMPLE N=32 yields=1505 min=448 avg=503.6 cyc
SIMPLE N=64 yields=1008 min=519 avg=736.1 cyc
DONE
size: text=13248 data=168 bss=38727
200022c0 00000020 B _kernel
## CONFIG_SCHED_SCALABLE=y CONFIG_TIMESLICING=y
SCALABLE N=2 yields=1824 min=365 avg=419.7 cyc
SCALABLE N=4 yields=1339 min=510 avg=568.2 cyc
SCALABLE N=8 yields=1183 min=572 avg=641.5 cyc
SCALABLE N=16 yields=1050 min=576 avg=719.5 cyc
SCALABLE N=32 yields=932 min=672 avg=803.9 cyc
SCALABLE N=64 yields=839 min=701 avg=881.2 cyc
DONE
size: text=14920 data=168 bss=39271
200024d8 00000028 B _kernel
## CONFIG_SCHED_MULTIQ=y CONFIG_TIMESLICING=y
MULTIQ N=2 yields=2800 min=228 avg=276.1 cyc
MULTIQ N=4 yields=3891 min=228 avg=262.7 cyc
MULTIQ N=8 yields=2797 min=228 avg=275.9 cyc
MULTIQ N=16 yields=3889 min=228 avg=262.6 cyc
MULTIQ N=32 yields=2792 min=228 avg=275.6 cyc
MULTIQ N=64 yields=3879 min=228 avg=262.2 cyc
DONE
size: text=13424 data=168 bss=38983
200022c0 00000120 B _kernel
```

**출력에서 볼 것** (평균 avg를 본다. min은 가장 싼 전환 한 번):

- **MULTIQ (타임슬라이스 끔)**: N = 2든 64든 **72.8 cycle**로 평평하다. O(1)이다. 이 랩에서 Zephyr의 yield → 다음 스레드 전환은 명령 약 90개다(QEMU는 예외 스태킹 사이클을 모델링하지 않으니 실칩에선 더 든다).
- **SIMPLE (기본값)**: N = 2에서 76.8로 MULTIQ와 거의 같지만 N = 64에서 523.0. (523.0 − 76.8) / 62 ≈ **7.2 cycle(명령 약 9개) / 같은 우선순위 스레드** — `z_priq_simple_yield`가 같은 우선순위 스레드를 하나씩 `z_sched_prio_cmp`로 비교하며 지나가는 비용이다. N이 4 이하인 보통 앱에서는 차이가 없다.
- **SCALABLE**: N = 2에서 이미 210.4(MULTIQ의 2.9배)이고 N이 두 배가 될 때마다 약 70~80 cycle씩 는다 — O(log N) + 큰 상수. 이 측정 범위(N ≤ 64)에서는 SIMPLE보다 항상 비싸다. Kconfig 도움말이 "Most applications don't want this"라고 하는 이유다.
- **타임슬라이스 켬(기본값)**: 모든 구현에서 전환 한 번에 약 190~200 cycle이 더 붙는다(MULTIQ 72.8 → 262~276). 다른 스레드로 바뀔 때마다 `update_cache()` → `z_reset_time_slice()`가 `z_abort_timeout()` + `z_add_timeout()`으로 슬라이스 timeout을 delta 리스트에서 빼고 다시 넣고, 그것이 머리가 되면 `sys_clock_set_timeout()`으로 SysTick을 다시 설정하기 때문이다. 창마다 262와 276이 번갈아 나오는 원인은 확인하지 못했다(main의 timeout과 슬라이스 timeout 중 무엇이 머리인지에 따라 SysTick 재설정 여부가 달라지는 것으로 추정한다). **ready queue 선택보다 타임슬라이스 설정이 전환 비용에 더 크게 작용한다**는 것이 이 측정의 부수 결과다 — 같은 우선순위 round-robin이 필요 없으면 `CONFIG_TIMESLICING=n` 또는 `CONFIG_TIMESLICE_SIZE=0`을 검토할 만하다.
- **크기**(타임슬라이스 끔): text가 SIMPLE 12808 B, MULTIQ 12996 B(+188 B), SCALABLE 14440 B(+1632 B — Kconfig 도움말의 "약 2 KB"와 같은 규모). `_kernel`은 32 / 40 / 288 B.

```svg
<svg viewBox="0 0 640 358" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" font-size="14" text-anchor="middle" font-weight="bold">Zephyr k_yield 1회 비용: ready queue 구현별 (같은 우선순위 N개)</text>
<line x1="70" y1="308" x2="620" y2="308" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="58" x2="70" y2="308" stroke="currentColor" stroke-width="1.2"/>
<line x1="70" y1="308" x2="620" y2="308" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="312" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="272.286" x2="620" y2="272.286" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="276.286" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="236.571" x2="620" y2="236.571" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="240.571" font-size="12" text-anchor="end">200</text>
<line x1="70" y1="200.857" x2="620" y2="200.857" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="204.857" font-size="12" text-anchor="end">300</text>
<line x1="70" y1="165.143" x2="620" y2="165.143" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="169.143" font-size="12" text-anchor="end">400</text>
<line x1="70" y1="129.429" x2="620" y2="129.429" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="133.429" font-size="12" text-anchor="end">500</text>
<line x1="70" y1="93.7143" x2="620" y2="93.7143" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="97.7143" font-size="12" text-anchor="end">600</text>
<line x1="70" y1="58" x2="620" y2="58" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64" y="62" font-size="12" text-anchor="end">700</text>
<line x1="90" y1="308" x2="90" y2="312" stroke="currentColor" stroke-width="1.2"/>
<text x="90" y="326" font-size="12" text-anchor="middle">2</text>
<line x1="192" y1="308" x2="192" y2="312" stroke="currentColor" stroke-width="1.2"/>
<text x="192" y="326" font-size="12" text-anchor="middle">4</text>
<line x1="294" y1="308" x2="294" y2="312" stroke="currentColor" stroke-width="1.2"/>
<text x="294" y="326" font-size="12" text-anchor="middle">8</text>
<line x1="396" y1="308" x2="396" y2="312" stroke="currentColor" stroke-width="1.2"/>
<text x="396" y="326" font-size="12" text-anchor="middle">16</text>
<line x1="498" y1="308" x2="498" y2="312" stroke="currentColor" stroke-width="1.2"/>
<text x="498" y="326" font-size="12" text-anchor="middle">32</text>
<line x1="600" y1="308" x2="600" y2="312" stroke="currentColor" stroke-width="1.2"/>
<text x="600" y="326" font-size="12" text-anchor="middle">64</text>
<text x="345" y="350" font-size="13" text-anchor="middle">같은 우선순위로 runnable한 스레드 수 N (로그 눈금)</text>
<text x="14" y="48" font-size="12" text-anchor="start">25 MHz 카운터 cycle (평균)</text>
<polyline points="90.0,280.6 192.0,275.4 294.0,265.1 396.0,244.6 498.0,203.4 600.0,121.2" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="90.0" cy="280.6" r="3.5" fill="#4a7bd0"/>
<circle cx="192.0" cy="275.4" r="3.5" fill="#4a7bd0"/>
<circle cx="294.0" cy="265.1" r="3.5" fill="#4a7bd0"/>
<circle cx="396.0" cy="244.6" r="3.5" fill="#4a7bd0"/>
<circle cx="498.0" cy="203.4" r="3.5" fill="#4a7bd0"/>
<circle cx="600.0" cy="121.2" r="3.5" fill="#4a7bd0"/>
<line x1="90" y1="76" x2="112" y2="76" stroke="#4a7bd0" stroke-width="2.5"/>
<text x="118" y="80" font-size="12" text-anchor="start">SCHED_SIMPLE (정렬 dlist, 기본값)</text>
<polyline points="90.0,232.9 192.0,181.2 294.0,155.5 396.0,127.8 498.0,97.7 600.0,69.8" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="90.0" cy="232.9" r="3.5" fill="#e08a3c"/>
<circle cx="192.0" cy="181.2" r="3.5" fill="#e08a3c"/>
<circle cx="294.0" cy="155.5" r="3.5" fill="#e08a3c"/>
<circle cx="396.0" cy="127.8" r="3.5" fill="#e08a3c"/>
<circle cx="498.0" cy="97.7" r="3.5" fill="#e08a3c"/>
<circle cx="600.0" cy="69.8" r="3.5" fill="#e08a3c"/>
<line x1="90" y1="96" x2="112" y2="96" stroke="#e08a3c" stroke-width="2.5"/>
<text x="118" y="100" font-size="12" text-anchor="start">SCHED_SCALABLE (rbtree)</text>
<polyline points="90.0,282.0 192.0,282.0 294.0,282.0 396.0,282.0 498.0,282.0 600.0,282.0" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="90.0" cy="282.0" r="3.5" fill="#3f9a6b"/>
<circle cx="192.0" cy="282.0" r="3.5" fill="#3f9a6b"/>
<circle cx="294.0" cy="282.0" r="3.5" fill="#3f9a6b"/>
<circle cx="396.0" cy="282.0" r="3.5" fill="#3f9a6b"/>
<circle cx="498.0" cy="282.0" r="3.5" fill="#3f9a6b"/>
<circle cx="600.0" cy="282.0" r="3.5" fill="#3f9a6b"/>
<line x1="90" y1="116" x2="112" y2="116" stroke="#3f9a6b" stroke-width="2.5"/>
<text x="118" y="120" font-size="12" text-anchor="start">SCHED_MULTIQ (배열 + 비트맵)</text>
<polyline points="90.0,209.4 192.0,214.2 294.0,209.5 396.0,214.2 498.0,209.6 600.0,214.4" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/>
<circle cx="90.0" cy="209.4" r="3.5" fill="#3f9a6b"/>
<circle cx="192.0" cy="214.2" r="3.5" fill="#3f9a6b"/>
<circle cx="294.0" cy="209.5" r="3.5" fill="#3f9a6b"/>
<circle cx="396.0" cy="214.2" r="3.5" fill="#3f9a6b"/>
<circle cx="498.0" cy="209.6" r="3.5" fill="#3f9a6b"/>
<circle cx="600.0" cy="214.4" r="3.5" fill="#3f9a6b"/>
<line x1="90" y1="136" x2="112" y2="136" stroke="#3f9a6b" stroke-width="2.5" stroke-dasharray="4 3"/>
<text x="118" y="140" font-size="12" text-anchor="start">MULTIQ + TIMESLICING=y (기본 20 ms)</text>
</svg>
```

그림 10 — 예제 10. 실선은 타임슬라이스를 끈 ready queue 자체의 비용, 점선은 기본값(타임슬라이스 20 ms)의 MULTIQ. 가로축은 로그 눈금이라 SIMPLE의 직선 증가가 휘어 보인다.

### 5.7 FreeRTOS vs Zephyr 한눈에

| 항목 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| ready 구조 | 우선순위별 리스트 배열 (고정) | SIMPLE(정렬 dlist, 기본) / SCALABLE(rbtree) / MULTIQ(배열 + 비트맵) 중 Kconfig |
| 최고 우선순위 찾기 | generic 훑기 또는 비트맵 + CLZ, PendSV 안에서 | 구현별 best, 상태 변화 때 `update_cache()`로 미리 |
| 같은 우선순위 순서 | `pxIndex` 커서 회전 | 정렬 삽입 위치(SIMPLE) / `order_key`(SCALABLE) / append(MULTIQ) |
| 리스트 노드 | 20 B (값·owner·container 포함) | 8 B (`CONTAINER_OF`로 owner 계산) |
| TCB/스레드의 고리 | `xStateListItem` + `xEventListItem` | `qnode_dlist`/`qnode_rb`(union) + `timeout.node` |
| 대기자 리스트 | 이벤트 리스트, 값 = `configMAX_PRIORITIES − prio` | `_wait_q_t`, WAITQ_SIMPLE/SCALABLE |
| 시간 리스트 | 절댓값 정렬 2개 + wrap 때 교환 | delta 리스트 1개 + 64비트 시계 |
| O(n) 삽입의 보호 | 스케줄러 정지 (인터럽트 허용) | 스핀락 (UP에서는 인터럽트 마스크) |
| 현재 태스크 | ready 리스트에 남아 있음 | UP: runq에 남아 있음, SMP: 빠짐 |

---

## 6. RTOS 커널 안에서는 무슨 일이 일어나나 — API 한 번이 리스트 연산 몇 개인가

자료구조를 알았으니, 자주 쓰는 API 한 번이 어떤 리스트 연산으로 바뀌는지 경로로 따라가 보자(함수 이름은 V11.3.2 `tasks.c`·`queue.c`, v4.4.2 `kernel/sched.c`·`kernel/timeout.c`·`kernel/sem.c`에서 확인).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">큐에서 timeout 있게 받기 → 깨우기 → 스위치: 리스트 연산 경로</text>
<text x="170" y="46" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS V11.3.2</text>
<text x="510" y="46" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2</text>
<rect x="20" y="58" width="300" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="170" y="76" font-size="12" text-anchor="middle">xQueueReceive(q, &amp;v, 100)</text>
<rect x="20" y="94" width="300" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="170" y="112" font-size="12" text-anchor="middle">vTaskPlaceOnEventList</text>
<line x1="170" y1="84" x2="170" y2="93" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="130" width="300" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170" y="148" font-size="12" text-anchor="middle">vListInsert(이벤트 리스트) O(n)</text>
<line x1="170" y1="120" x2="170" y2="129" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="166" width="300" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="170" y="184" font-size="12" text-anchor="middle">prvAddCurrentTaskToDelayedList</text>
<line x1="170" y1="156" x2="170" y2="165" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="202" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="220" font-size="12" text-anchor="middle">uxListRemove(ready) O(1) + 비트 끔</text>
<line x1="170" y1="192" x2="170" y2="201" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="238" width="300" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170" y="256" font-size="12" text-anchor="middle">vListInsert(delayed) O(n)</text>
<line x1="170" y1="228" x2="170" y2="237" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="274" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="292" font-size="12" text-anchor="middle">PendSV → 선택 (clz) O(1)</text>
<line x1="170" y1="264" x2="170" y2="273" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="310" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="328" font-size="12" text-anchor="middle">give → RemoveFromEventList 머리 O(1)</text>
<line x1="170" y1="300" x2="170" y2="309" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="20" y="346" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="364" font-size="12" text-anchor="middle">prvAddTaskToReadyList O(1)</text>
<line x1="170" y1="336" x2="170" y2="345" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="58" width="300" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510" y="76" font-size="12" text-anchor="middle">k_sem_take(&amp;s, K_MSEC(100))</text>
<rect x="360" y="94" width="300" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510" y="112" font-size="12" text-anchor="middle">z_pend_curr → pend_locked</text>
<line x1="510" y1="84" x2="510" y2="93" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="130" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="148" font-size="12" text-anchor="middle">unready_thread → runq_remove</text>
<line x1="510" y1="120" x2="510" y2="129" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="166" width="300" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510" y="184" font-size="12" text-anchor="middle">_priq_wait_add O(n)</text>
<line x1="510" y1="156" x2="510" y2="165" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="202" width="300" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510" y="220" font-size="12" text-anchor="middle">z_add_timeout (delta) O(n)</text>
<line x1="510" y1="192" x2="510" y2="201" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="238" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="256" font-size="12" text-anchor="middle">z_swap ← ready_q.cache</text>
<line x1="510" y1="228" x2="510" y2="237" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="274" width="300" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="292" font-size="12" text-anchor="middle">give → z_unpend_first_thread 머리</text>
<line x1="510" y1="264" x2="510" y2="273" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="310" width="300" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="328" font-size="12" text-anchor="middle">z_ready_thread → runq_add</text>
<line x1="510" y1="300" x2="510" y2="309" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<rect x="360" y="346" width="300" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="364" font-size="12" text-anchor="middle">update_cache → next_up</text>
<line x1="510" y1="336" x2="510" y2="345" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah7)"/>
<text x="20" y="392" font-size="12" text-anchor="start">빨강 = 정렬 삽입 O(n), 주황 = 구현에 따라 다름 (SIMPLE은 O(n), MULTIQ는 O(1))</text>
</svg>
```

그림 11 — 큐에서 timeout 있게 받기 → 다른 태스크(또는 ISR)가 보내서 깨우기 → 스위치까지, 두 커널의 리스트 연산. 빨간 상자가 정렬 삽입 O(n), 주황 상자는 ready queue 구현에 따라 O(1)~O(n)이다.

FreeRTOS 쪽을 말로 풀면:

1. `xQueueReceive(q, &v, 100)`이 큐가 비어 있음을 보고 `vTaskSuspendAll()` + 큐 잠금 → `vTaskPlaceOnEventList(&q->xTasksWaitingToReceive, 100)`.
2. 그 안에서 `vListInsert(이벤트 리스트, &xEventListItem)` — 대기자 수만큼 O(n) — 그리고 `prvAddCurrentTaskToDelayedList(100, pdTRUE)`: `uxListRemove(&xStateListItem)`(ready에서 빼고, 비면 비트 지움) → 깨울 시각 계산 → 넘치면 overflow 리스트, 아니면 현재 delayed 리스트에 `vListInsert` — delayed 수만큼 O(n) → 맨 앞이 되었으면 `xNextTaskUnblockTime` 갱신.
3. `xTaskResumeAll()` → `portYIELD_WITHIN_API()` → PendSV → `vTaskSwitchContext()` → `taskSELECT_HIGHEST_PRIORITY_TASK()`(CLZ, O(1)) → C2의 레지스터 교체.
4. 다른 태스크가 `xQueueSend()` (또는 ISR이 `xQueueSendFromISR()`) → `xTaskRemoveFromEventList()`: 이벤트 리스트 머리 O(1) → (스케줄러 정지 아니면) delayed 리스트에서 빼고 `prvAddTaskToReadyList()` O(1) → 깬 태스크가 더 높으면 yield.
5. 100 tick 안에 아무도 안 보내면 SysTick → `xTaskIncrementTick()`이 delayed 머리를 보고 시간이 됐으면 빼서 ready로(이벤트 리스트에서도 뺀다) → 수신 함수가 깨어나 timeout을 보고 `errQUEUE_EMPTY` 반환.

Zephyr 쪽은 `k_sem_take(&s, K_MSEC(100))` → `z_pend_curr()` → `pend_locked()` → `add_to_waitq_locked()`(`unready_thread()`로 runq에서 빼고 `_priq_wait_add` 정렬 삽입) + `add_thread_timeout()` → `z_add_timeout()`(delta 리스트 삽입) → `z_swap()`. 깨우기는 `k_sem_give()` → `z_unpend_first_thread()`(`_priq_wait_best` 머리 + timeout 취소) → `z_ready_thread()` → `ready_thread()` → `queue_thread()` → `runq_add()` → `update_cache()`.

더 깊은 내용의 갈 곳: 레지스터를 실제로 바꾸는 PendSV 어셈블리는 C2, tick 처리와 delayed 리스트를 언제·어떻게 쓰는지는 C5, tickless에서 `xNextTaskUnblockTime`이 다음 인터럽트 시각이 되는 과정은 C6, 크리티컬 섹션과 BASEPRI는 C7, SMP의 코어별 선택은 C10, 파일 단위 읽기 순서는 C12, 우선순위 상속이 리스트를 어떻게 옮기는지는 D3.

---

## 7. 실무에서 다시 보기

### 7.1 드론 비행 제어기 — "1 kHz에 깨는 태스크가 몇 개인가"

STM32H7급 FC에서 IMU 읽기·자세 추정·rate PID·모터 출력은 보통 IMU 데이터 준비 인터럽트(1~8 kHz)에 묶여 하나의 체인으로 돈다. 흔한 실수는 이 체인을 태스크 여러 개로 쪼개고 각자 `vTaskDelayUntil(&last, 1)`로 1 ms 주기를 주는 것이다. 그러면 매 tick마다 같은 tick에 K개가 깨면서(예제 8) tick ISR이 그만큼 길어지고, 그동안 BASEPRI 아래의 ISR(UART 텔레메트리, ESC 텔레메트리 DMA 완료 …)이 밀린다. 더 나은 구조는 IMU 인터럽트가 **한 태스크**에 알림(`vTaskNotifyGiveFromISR`)을 주고, 그 태스크가 체인을 순서대로 돌리거나 다음 단계를 직접 알림으로 깨우는 것이다 — 깨움이 tick ISR이 아니라 ISR 한 번 + O(1) 이벤트 경로로 간다. 우선순위는 "IMU 체인 > 제어 > 텔레메트리 > 로깅 > idle"처럼 5~8단계면 충분하고, H7(Cortex-M7)이면 CLZ 선택이 기본으로 켜진다.

### 7.2 로봇 관절 모터 컨트롤러 — Cortex-M0+에서

FOC 전류 루프(20 kHz)는 ISR에서 돌고 RTOS는 CAN 통신·상태 기계·진단 정도를 맡는 작은 MCU(M0+)가 많다. 여기는 CLZ가 없어 generic 선택만 가능하다. 대응은 간단하다: `configMAX_PRIORITIES`를 실제로 쓰는 4~5로 줄인다. 예제 6에서 보듯 generic 비용은 "빈 우선순위 수"에 비례하니 단계 수를 줄이는 게 곧 최적화이고, ready 배열 RAM(단계당 20 B)도 준다.

### 7.3 센서 허브 MCU (Zephyr) — 기본값 SIMPLE이 맞나

Linux SoC 옆에서 IMU 타임스탬프와 센서 융합 전처리를 하는 Zephyr 센서 허브는 스레드가 5~10개, 그중 동시에 runnable한 것은 1~3개가 보통이다. 이 경우 기본값 `SCHED_SIMPLE`이 코드 크기와 상수 비용 모두에서 가장 낫다(예제 10). 같은 우선순위 스레드가 많아 round-robin을 자주 돈다면 `SCHED_MULTIQ`, 수십 개 이상이 동시에 runnable하거나 `SCHED_DEADLINE`(EDF, C11)을 쓴다면 `SCHED_SCALABLE`을 검토한다. 결정 근거는 "동시에 runnable한 스레드의 최대 수"이고, 그 숫자를 설계 문서에 남긴다.

### 7.4 Don의 SSD 펌웨어와 나란히 놓기

| SSD 펌웨어에서 (일반적인 구조) | RTOS 커널에서 |
|---|---|
| 고정 SRAM에 잡은 명령 슬롯 배열, 슬롯 안의 `next`/`prev` | 정적·동적 TCB, TCB 안의 `ListItem_t` 두 개 |
| 호스트 큐 / 다이별 큐 / 완료 큐 사이로 슬롯 이동 (포인터만) | ready / delayed / 이벤트 / suspended 리스트 사이로 TCB 이동 |
| "일 있는 다이" 비트맵 + find-first-set | `uxTopReadyPriority` 비트맵 + `clz` |
| 타임아웃 테이블을 정렬해 두고 타이머 ISR은 머리만 확인 | delayed 리스트 정렬 삽입 + `xNextTaskUnblockTime` 캐시 |
| ISR은 도착 큐에만 넣고 메인 루프가 합친다 | `xPendingReadyList` + `xTaskResumeAll()` |
| 명령 큐 손상 → 디스패처 무한 루프 | 리스트 원 손상 → `vListInsert` 무한 루프 |

면접에서 "RTOS 커널 내부를 아는가"라는 질문에 Don은 이 표의 왼쪽 경험을 오른쪽 용어로 번역해서 말할 수 있다. "저는 SSD 펌웨어에서 같은 패턴을 손으로 짰고, FreeRTOS `list.c`가 그걸 일반화한 것이라는 걸 소스와 덤프로 확인했다"가 가장 강한 답이다.

---

## 8. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR 우선순위가 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은데 `FromISR` API 호출 | 가끔 `vListInsert` 안에서 영원히 돈다, 또는 HardFault | 커널이 BASEPRI로 막지 못하는 ISR이 리스트를 동시에 고쳐 원이 깨짐 | NVIC 우선순위 숫자를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이상(덜 긴급)으로. `configASSERT` 켜면 포트가 잡아 준다 |
| 스택 오버플로가 이웃 TCB를 덮음 | 엉뚱한 태스크가 사라지거나 `uxListRemove`에서 fault | TCB 안 `ListItem_t` 포인터가 덮여 리스트가 깨짐 | `configCHECK_FOR_STACK_OVERFLOW 2`, 스택 high water mark 확인 (A4 §5, F3) |
| 리스트를 크리티컬 섹션 없이 직접 순회 (디버그 코드) | 드물게 무한 루프·잘못된 이름 | 순회 중 tick이 항목을 다른 리스트로 옮김 | `taskENTER_CRITICAL()` 안에서 스냅샷만 뜨고 밖에서 출력 (예제 3) |
| 같은 주기 태스크 수십 개를 같은 위상으로 `vTaskDelayUntil` | 특정 tick마다 인터럽트 지연이 튄다 | 같은 tick 일괄 깨움 O(K), BASEPRI 마스크 (예제 8) | 위상 엇갈리기, 한 태스크 + 알림 체인으로 합치기 |
| 우선순위를 32단계로 넉넉히 | RAM 증가, M0에서 스위치 지연 증가 | ready 배열 20 B × P, generic 훑기 O(P) | 실제 단계 수 + 여유 1~2 |
| M0·M23 포트에서 `configUSE_PORT_OPTIMISED_TASK_SELECTION 1` | 빌드 에러 또는 `clz` 미정의 | ARMv6-M·ARMv8-M Baseline에 `CLZ` 없음 | 0(generic) 사용, 단계 수 줄이기 |
| `uxTopReadyPriority`를 우선순위 숫자로 읽음 | "우선순위 67인 태스크?" 같은 오해 | optimised 빌드에선 비트맵 | 빌드 설정 확인, `31 − clz`로 해석 |
| 대기 중인 태스크의 우선순위를 바꿈 | 그 태스크가 예상보다 늦게(또는 일찍) 깸 | 이벤트 리스트 안 위치는 재정렬되지 않음 (§2.5) | 대기 전에 우선순위 확정, 또는 뮤텍스 상속에 맡기기 (D3) |
| Zephyr에서 SIMPLE인데 같은 우선순위 스레드 수십 개가 계속 yield | 스레드 수에 비례해 전환이 느려짐 | `z_priq_simple_yield`가 같은 우선순위 스레드를 전부 지나감 (예제 10) | MULTIQ로 바꾸거나 스레드 수·우선순위 구조 재설계 |

---

## 9. 면접에서 이렇게 말한다

**Q.** "How does FreeRTOS find the highest-priority ready task? Is it O(1)?"

**A.** 우선순위마다 ready 리스트가 하나씩 있다. Cortex-M3 이상에서는 "비어 있지 않은 우선순위" 비트맵을 들고 있다가 `31 − clz(비트맵)`로 한 번에 찾으니 O(1)이고, CLZ가 없는 M0 같은 포트는 최고 후보에서 한 칸씩 내려가며 빈 리스트를 건너뛰는 generic 방식이라 빈 우선순위 수에 비례한다. 같은 우선순위 안에서는 리스트의 인덱스 커서를 한 칸 밀어 round-robin한다. QEMU에서 재 보니 CLZ 방식은 우선순위 4~32개에서 비용이 완전히 같았고, generic은 빈 우선순위 하나당 명령 5개씩 늘었다.

> FreeRTOS keeps one ready list per priority. On Cortex-M3 and up, the port keeps a 32-bit bitmap of non-empty priorities, so selection is `31 - clz(bitmap)` followed by advancing the list's round-robin index — O(1), independent of the number of priorities, limited to 32 priorities. On cores without CLZ, like Cortex-M0, it uses the generic path: a cached upper bound, walking down past empty lists, so the cost is proportional to the number of empty priority levels. I measured both on QEMU: the CLZ path was flat from 4 to 32 priorities, and the generic path added about five instructions per empty level — exactly the loop in the disassembly.

**꼬리 질문**: "So is the whole scheduler O(1)?" → 선택은 O(1)이지만 delayed 리스트·이벤트 리스트 정렬 삽입과 같은 tick의 일괄 깨움은 O(n)이다(다음 질문).

**Q.** "Where are the O(n) operations in FreeRTOS, and do they affect interrupt latency?"

**A.** 정렬 삽입 두 곳(delayed 리스트, 큐·세마포어의 대기자 리스트)과 tick에서 여러 태스크를 한꺼번에 깨우는 경우다. 정렬 삽입은 스케줄러만 멈추고 인터럽트는 열어 둔 채 하므로 인터럽트 지연엔 영향이 없고 태스크 응답 시간에만 영향이 있다. 반면 tick ISR 안의 일괄 깨움은 BASEPRI를 올린 채 도니까 인터럽트 지연에 바로 더해진다. 실측으로 태스크 64개가 같은 tick에 깨면 그 ISR이 약 2,500 cycle이었다.

> Two sorted inserts — into the delayed list and into an object's waiter list — are O(n) in the number of delayed tasks or waiters. FreeRTOS does those with the scheduler suspended but interrupts enabled, so they don't add to interrupt latency; they only delay other tasks. The one that does hurt interrupt latency is the tick handler waking many tasks on the same tick: each wake is O(1), but K of them run with BASEPRI raised. On QEMU I saw the tick ISR go from 39 cycles to about 2,500 cycles with 64 tasks waking on one tick. The design rule is to keep task counts small and avoid lining up many periodic tasks on the same phase.

**Q.** "Why does FreeRTOS have two delayed task lists?"

**A.** 깨울 시각을 절댓값 tick으로 정렬하는데, "지금 + 대기"가 32비트를 넘으면 작은 숫자가 되어 정렬이 깨진다. 그래서 넘친 시각은 overflow 리스트에 따로 넣고, tick이 0으로 넘어가는 순간 두 리스트 포인터를 맞바꾼다. 복사 없이 포인터 교환 한 번이고, 그 순간 현재 리스트는 반드시 비어 있어야 하니 assert로 확인한다.

> Wake times are stored as absolute tick values and the list is sorted ascending. When "now plus timeout" overflows the tick type, the wake time becomes a small number and would sort before tasks that wake earlier. So FreeRTOS puts overflowed wake times in a second list. When the tick count wraps to zero, the current list must be empty, and the kernel just swaps the two list pointers and recomputes the next unblock time — no copying, one pointer swap.

**Q.** "Why is the event list item value set to configMAX_PRIORITIES minus the priority?"

**A.** 리스트는 값 오름차순으로 정렬되고 맨 앞을 꺼낸다. 우선순위를 뒤집어 값으로 넣으면 가장 높은 대기자가 맨 앞에 서서, give 쪽(ISR일 수도 있다)은 머리 하나만 O(1)로 꺼내면 된다. 비용을 짧아야 하는 깨우는 쪽이 아니라 블록하는 쪽에 둔 설계다. 같은 우선순위는 먼저 블록한 순서(FIFO)다.

> The list code only knows ascending sort by item value, and wakers take the head. Storing `configMAX_PRIORITIES - priority` makes the highest-priority waiter sort first, so the give path — which may be an ISR — just removes the head in O(1). The O(n) cost is paid by the task that blocks, not by the waker. Equal priorities stay FIFO because insert places equal values after existing ones.

**Q.** "What is the pending ready list for?"

**A.** 스케줄러 정지(`vTaskSuspendAll`)는 인터럽트를 막지 않는다. 그 사이 ISR이 대기자를 깨우면 ready 리스트를 건드리면 안 되니, 태스크의 이벤트 리스트 항목만 pending 리스트 끝에 붙여 두고, `xTaskResumeAll()`이 나중에 진짜 ready 리스트로 옮긴다. 내 덤프에서 같은 TCB의 상태 항목은 아직 suspended에, 이벤트 항목은 pending에 걸린 순간을 직접 봤다.

> Suspending the scheduler keeps interrupts enabled, so an ISR can unblock a task while task code is in the middle of manipulating the kernel lists. The ISR must not touch the ready lists then, so it appends the task's event list item to `xPendingReadyList` in O(1), and `xTaskResumeAll()` later moves those tasks to their ready lists. In a live dump I could see one TCB with its state item still in the suspended list and its event item in the pending list.

**Q.** "How is Zephyr's ready queue different, and which option would you pick?"

**A.** Zephyr는 Kconfig로 세 가지를 고른다. 기본 SIMPLE은 우선순위 정렬된 이중 연결 리스트 하나라서 다음 스레드는 머리 O(1), 넣기는 O(n). SCALABLE은 rbtree라 O(log n)이지만 코드가 2 KB쯤 크고 상수가 크다. MULTIQ는 FreeRTOS처럼 우선순위별 리스트 + 비트맵이라 O(1)인데 리스트 머리만큼 RAM이 들고 deadline 스케줄링과 같이 못 쓴다. 동시에 runnable한 스레드가 몇 개 안 되는 일반적인 MCU 앱이면 SIMPLE이 맞고, 같은 우선순위 스레드가 많으면 MULTIQ, 수십 개 이상이면 SCALABLE이다.

> Zephyr makes it a Kconfig choice. The default, SCHED_SIMPLE, is one priority-sorted doubly linked list: picking the next thread is the head, inserting is O(n). SCHED_SCALABLE is a red-black tree, O(log n) but with more code and a higher constant. SCHED_MULTIQ is the classic array of per-priority lists plus a bitmap — O(1), costs RAM for the list heads, and can't be combined with deadline scheduling. For a typical MCU app with a handful of runnable threads I'd keep SIMPLE; with many equal-priority threads that yield or time-slice, MULTIQ; with dozens of runnable threads, SCALABLE. I measured yield cost per configuration to back that choice.

**Q.** "Your device hangs and the debugger shows the PC inside vListInsert. What do you check?"

**A.** 리스트 원이 깨져서 정렬 삽입 루프가 끝 표시를 못 만나는 전형적인 증상이다. 소스 주석이 꼽은 순서대로 본다: 스택 오버플로(오버플로 검사 2 켜기), ISR 우선순위가 syscall 상한보다 높은데 FromISR API를 부르는지, 크리티컬 섹션·스케줄러 정지 중 API 호출이나 ISR에서 일반 API 호출, 초기화 전 객체 사용, tick 인터럽트 우선순위. `configASSERT`와 리스트 무결성 바이트를 켜면 더 일찍 잡힌다.

> That's the classic symptom of a corrupted list: the sorted-insert loop never reaches the end marker. I'd go through the causes listed in the source comment: stack overflow (enable overflow checking method 2 and look at high-water marks), an ISR above `configMAX_SYSCALL_INTERRUPT_PRIORITY` calling FreeRTOS APIs, calling non-FromISR APIs from an ISR or APIs inside a critical section, using a queue before it's created or before the scheduler starts, and the tick interrupt priority. Turning on `configASSERT` and `configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES` catches it closer to the cause.

**Q.** "Is the running task in the ready list?"

**A.** FreeRTOS에서는 그렇다. Running은 ready 리스트에 그대로 있는 태스크 중 `pxCurrentTCB`가 가리키는 것일 뿐이고, 블록할 때 거기서 뺀다. Zephyr도 단일 코어에서는 현재 스레드를 runq에 남겨 두고, SMP에서는 뺀다.

> In FreeRTOS, yes — the running task stays in its ready list and is simply the one `pxCurrentTCB` points to; it's removed only when it blocks, suspends or is deleted. My live dump shows the monitor task in `pxReadyTasksLists[6]` while it was the one printing. Zephyr does the same on uniprocessor builds, but on SMP the current thread is taken out of the run queue.

---

## 10. 직접 해보기

1. **개념** — 예제 2에서 인덱스가 C에 있을 때 E를 `vListInsertEnd`로 넣으면 이후 `NEXT_ENTRY` 네 번은 무엇을 돌려주나? 손으로 그린 뒤 예제 2를 고쳐 확인하라.
정답/힌트: E는 인덱스(C) 바로 앞에 들어가므로 C 다음부터 한 바퀴를 돌면 E가 마지막이다. 리스트 순서를 그려 보면 바로 보인다.

2. **개념** — `configMAX_PRIORITIES 5`에서 우선순위 3인 태스크 둘이 같은 큐를 기다리고, 우선순위 4인 태스크 하나가 나중에 같은 큐를 기다린다. 이벤트 리스트의 값과 순서를 적어라. 그 뒤 우선순위 3 중 하나의 우선순위를 `vTaskPrioritySet`으로 4로 올리면 리스트 순서는?
정답/힌트: 값은 2, 2, 1 → 순서 4(값 1), 3, 3. 우선순위를 바꿔도 값만 1로 바뀌고 위치는 그대로라 정렬이 깨진 상태가 된다(§2.5).

3. **코드** — 예제 3에 `uxTaskGetSystemState()`로 얻은 `eCurrentState`를 함께 찍어서, 덤프에서 본 리스트 소속과 공식 API가 말하는 상태를 한 줄씩 대조하라.
정답/힌트: `TaskStatus_t` 배열 크기는 `uxTaskGetNumberOfTasks()`. 무한 대기 태스크(W3)가 suspended 리스트에 있지만 `eBlocked`로 나오는지 확인.

4. **측정** — 예제 6을 `configMAX_PRIORITIES 32`, generic으로 두고 L의 우선순위를 1, 8, 16, 30으로 바꿔 block->L을 재라. 예측: 우선순위 하나 올릴 때마다 약 4 cycle 줄어야 한다.
정답/힌트: 비용은 "최고 후보와 실제 최고 ready 사이의 빈 층 수"라서 L이 H에 가까울수록 싸진다. L=30이면 port-optimised와 거의 같아야 한다.

5. **측정** — 예제 8을 고쳐 K개 태스크가 같은 tick이 아니라 tick 100, 101, 102 …에 하나씩 깨게 하라. 깨움 tick ISR의 최댓값이 어떻게 바뀌나?
정답/힌트: 각 tick에서 하나씩만 깨니 최댓값은 K=1일 때(98 cycle 근처)와 같아야 한다. "위상 엇갈리기"의 효과를 숫자로 보이는 실험이다.

6. **Zephyr** — 예제 9를 `CONFIG_WAITQ_SCALABLE=y`로 빌드하면 `sem.wait_q.waitq`를 dlist로 걸을 수 없다. rbtree로 걷도록 고치고(`RB_FOR_EACH_CONTAINER(&sem.wait_q.waitq.tree, t, base.qnode_rb)`) 순서가 같은지 확인하라.
정답/힌트: 순서는 같아야 한다(우선순위, 같으면 `order_key` = 들어온 순서). 바뀌는 것은 코드 크기와 삽입 비용뿐이다.

7. **설계** — 드론 FC에 1 kHz 태스크 6개(IMU, 추정, rate PID, attitude, 믹서, 모터)를 각자 `vTaskDelayUntil`로 돌리는 설계를 리뷰한다고 하자. 이 노트의 자료구조 관점에서 문제 두 가지와 대안 하나를 영어 세 문장으로 써라.
정답/힌트: (1) 매 tick 6개 일괄 깨움 → tick ISR이 BASEPRI 아래 인터럽트를 그만큼 막는다, (2) 6개의 delayed 정렬 삽입·선택이 매 ms 반복된다. 대안: IMU 데이터 준비 인터럽트 → 태스크 알림 체인 또는 한 태스크 안의 순차 실행.

---

## 11. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| intrusive list | 끼워 넣는 리스트 | 노드를 따로 할당하지 않고 객체(TCB) 안에 박아 두는 연결 리스트 |
| sentinel / end marker | 끝 표시 항목 | 값이 최댓값인 가짜 항목. 빈 리스트 처리와 정렬 루프 종료를 단순하게 한다 (`xListEnd`) |
| `ListItem_t` | FreeRTOS 리스트 항목 | 값·next·prev·owner·container, 20 B |
| `MiniListItem_t` | 작은 리스트 항목 | 끝 표시용, 값·next·prev만 12 B |
| `List_t` | FreeRTOS 리스트 | 항목 수·인덱스·내장 끝 표시, 20 B |
| `pvOwner` | 항목의 주인 | 항목 → TCB 역포인터 |
| `pxContainer` | 항목의 소속 리스트 | DWARF에선 하위 호환 매크로 때문에 `pvContainer` |
| `pxIndex` | 리스트 커서 | 같은 우선순위 round-robin에서 마지막으로 고른 항목 |
| `xStateListItem` | 상태 항목 | ready/delayed/suspended/삭제 대기 리스트에 걸린다 |
| `xEventListItem` | 이벤트 항목 | 큐·세마포어 대기자 리스트 또는 pending ready 리스트에 걸린다 |
| `pxReadyTasksLists` | ready 리스트 배열 | 우선순위마다 하나, 크기 `configMAX_PRIORITIES` |
| delayed list | 지연 리스트 | timeout 있는 블록 태스크, 깨울 tick 순 정렬 |
| overflow delayed list | 넘침 지연 리스트 | tick wrap 너머에 깰 태스크. wrap 때 포인터 교환 |
| `xPendingReadyList` | 보류 ready 리스트 | 스케줄러 정지 중 깨어난 태스크를 잠시 두는 곳 |
| `uxTopReadyPriority` | 최고 ready 우선순위 | generic이면 상한 숫자, optimised면 비트맵 |
| CLZ | count leading zeros | 최상위 1 앞의 0 개수를 세는 명령. ARMv7-M 이상에 있다 |
| CTZ | count trailing zeros | 최하위 1 뒤의 0 개수. Zephyr MULTIQ가 쓴다 (M3에선 `rbit` + `clz`) |
| `xNextTaskUnblockTime` | 다음 깨움 시각 캐시 | delayed 리스트 머리 값. tick ISR이 이것만 비교한다 |
| `_kernel` | Zephyr 커널 전역 구조체 | `struct z_kernel` — cpus[], ready_q, threads … |
| `sys_dlist_t` | Zephyr 이중 연결 리스트 | 머리가 센티널인 원형 리스트, 노드 8 B |
| `_wait_q_t` | Zephyr 대기자 리스트 | 커널 객체마다 하나, WAITQ_SIMPLE/SCALABLE |
| delta list | 차이 리스트 | 각 노드가 앞 노드와의 시간 차이를 들고 있는 timeout 리스트 |
| `dticks` | Zephyr timeout 차이 값 | `struct _timeout`의 앞 노드 대비 tick 수 |
| SCHED_SIMPLE / SCALABLE / MULTIQ | Zephyr ready queue 구현 | 정렬 dlist / rbtree / 우선순위 배열 + 비트맵 |
| deterministic / bounded | 결정적 / 상한이 있는 | 최악 시간이 설계 시점에 정해진 상한을 넘지 않는 것 |

---

## 12. 요약 & 체크리스트

RTOS 커널의 스케줄러는 결국 리스트 관리자다. FreeRTOS는 20 B짜리 끼워 넣는 리스트 항목(`ListItem_t`)을 TCB에 두 개 박아 두고, 끝 표시를 리스트 안에 내장한 원형 이중 연결 리스트로 ready(우선순위별 배열)·delayed(두 개, tick wrap 때 포인터 교환)·pending·suspended·삭제 대기·객체 대기자 리스트를 만든다. ready 리스트는 끝 삽입 O(1), delayed와 대기자 리스트는 정렬 삽입 O(n)이고, 대기자 리스트는 값을 `configMAX_PRIORITIES − 우선순위`로 뒤집어 높은 우선순위가 머리에 오게 한다. 최고 우선순위 선택은 Cortex-M3 이상에서 비트맵 + `clz`로 우선순위 개수와 무관한 O(1)이고, generic은 빈 층당 명령 5개다. O(n) 정렬 삽입은 스케줄러 정지(인터럽트 허용) 아래에서 돌아 인터럽트 지연에 영향이 없지만, 같은 tick의 일괄 깨움은 BASEPRI 아래 O(K)라 인터럽트 지연에 더해진다. Zephyr는 Kconfig로 ready queue(정렬 dlist·rbtree·multi-queue)를 고르고, timeout은 64비트 시계의 delta 리스트 하나로 처리하며, 정렬 삽입은 스핀락(단일 코어에서는 인터럽트 마스크) 아래에서 한다.

- [ ] `List_t`·`ListItem_t`·`MiniListItem_t`의 필드와 크기(20/20/12 B)를 그리고, 끝 표시가 리스트 안에 있는 이유를 설명할 수 있다
- [ ] `vListInsert`가 `<=`로 비교하는 이유(같은 값 FIFO)와 `portMAX_DELAY`를 따로 처리하는 이유(무한 루프 방지)를 말할 수 있다
- [ ] `vListInsertEnd`가 `pxIndex` 앞에 넣는다는 것과 그것이 round-robin에 갖는 의미를 안다
- [ ] `uxListRemove`의 반환값이 비트맵을 지우는 데 쓰인다는 것을 안다
- [ ] 커널 전역 리스트 7종과 각각의 정렬 키·삽입 함수·복잡도를 표로 쓸 수 있다
- [ ] TCB의 두 항목이 어느 리스트에 걸리는지, 동시에 두 리스트에 걸리는 경우를 예로 들 수 있다
- [ ] pending ready 리스트가 필요한 이유를 "스케줄러 정지는 인터럽트를 막지 않는다"로 설명할 수 있다
- [ ] 이벤트 리스트 값이 `configMAX_PRIORITIES − 우선순위`인 이유와 우선순위 변경 시 재정렬되지 않는다는 점을 안다
- [ ] tick wrap에서 delayed 리스트 두 개가 포인터 교환으로 처리되는 과정을 그릴 수 있다
- [ ] generic vs port-optimised 선택의 코드·명령 수·측정 기울기를 말하고, CLZ가 없는 코어를 꼽을 수 있다
- [ ] FreeRTOS의 O(n) 연산이 어떤 보호(스케줄러 정지 / BASEPRI) 아래 도는지와 그것이 인터럽트 지연·태스크 응답에 미치는 영향을 구분할 수 있다
- [ ] Zephyr SIMPLE·SCALABLE·MULTIQ의 자료구조·복잡도·고르는 기준, delta timeout 리스트를 설명할 수 있다

---

## 참고 자료

- FreeRTOS-Kernel V11.3.2 소스: `list.c`, `include/list.h`, `tasks.c`(전역 리스트 선언, `taskSELECT_HIGHEST_PRIORITY_TASK`, `prvAddCurrentTaskToDelayedList`, `xTaskRemoveFromEventList`, `vTaskPlaceOnEventList`, `xTaskIncrementTick`, `taskSWITCH_DELAYED_LISTS`), `queue.c`(`xTasksWaitingToReceive`), `include/FreeRTOS.h`(`configUSE_MINI_LIST_ITEM`, 하위 호환 매크로), `portable/GCC/ARM_CM3/portmacro.h`(`ucPortCountLeadingZeros`, `portGET_HIGHEST_PRIORITY`) — 랩 경로 `/var/rtoslab/src/FreeRTOS-Kernel`, 원본 https://github.com/FreeRTOS/FreeRTOS-Kernel
- FreeRTOS 문서 — Customisation(`FreeRTOSConfig.h` 설정 설명, `configUSE_PORT_OPTIMISED_TASK_SELECTION`, `configMAX_PRIORITIES`): https://www.freertos.org/a00110.html
- FreeRTOS 문서 — Running the RTOS on a ARM Cortex-M Core(인터럽트 우선순위와 `configMAX_SYSCALL_INTERRUPT_PRIORITY`, `vListInsert` 주석이 가리키는 페이지): https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html
- Zephyr v4.4.2 소스: `include/zephyr/kernel_structs.h`(`struct z_kernel`, `_ready_q`, `_priq_rb`, `_priq_mq`, `_wait_q_t`, `struct _timeout`), `kernel/include/priority_q.h`(세 구현), `kernel/sched.c`(`queue_thread`, `next_up`, `update_cache`, `z_pend_curr`), `kernel/timeout.c`(`z_add_timeout`, `sys_clock_announce_locked`), `kernel/Kconfig`(`SCHED_ALGORITHM`, `WAITQ_ALGORITHM` choice), `include/zephyr/sys/dlist.h` — 랩 경로 `/var/rtoslab/zephyrproject/zephyr`, 원본 https://github.com/zephyrproject-rtos/zephyr
- Zephyr 문서 — Scheduling(ready queue 구현 선택 설명): https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html
- Arm, ARMv7-M Architecture Reference Manual — `CLZ`, `RBIT` 명령 정의 (ARMv6-M에는 없음)
- Richard Barry, "Mastering the FreeRTOS Real Time Kernel — A Hands-On Tutorial Guide" (FreeRTOS 공식 무료 책) — 태스크 상태와 스케줄링 장
- Embedded Linux 노트 EL F1(스케줄러, CFS/EEVDF rbtree 런큐) — 범용 OS의 런큐와 비교할 때
