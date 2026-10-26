# C12. 커널 소스 읽기 투어 — 자료구조 → 핫 패스 → API 하나 끝까지, gdb backtrace를 지도 삼아 FreeRTOS · Zephyr 커널을 읽는다

> **이 노트를 다 읽으면**: 처음 보는 RTOS 커널을 "자료구조 → 핫 패스 → API 하나 끝까지 → 실행으로 확인" 순서로 읽는 방법을 손에 익힌다 · FreeRTOS의 매크로 미로(`configXXX` · `traceXXX` · `portXXX` · `mtCOVERAGE_TEST_MARKER` · `PRIVILEGED_FUNCTION`)를 `gcc -E`로 걷어 내고 "이 설정에서 실제로 도는 코드"만 읽을 수 있다 · QEMU + gdb 중단점의 backtrace로 xTaskCreate · 블로킹 send · FromISR · tick · PendSV, 그리고 Zephyr의 k_thread_create · k_sem_give · k_sleep · ISR 종료 · sys_clock_announce 경로를 직접 찍고 호출 그래프로 그릴 수 있다 · 함수 크기와 분기 수로 "먼저 읽을 15개"를 고르고 2주 읽기 계획을 세울 수 있다
> **왜 필요한가**: 시니어 펌웨어 면접의 "FreeRTOS에서 xQueueSend가 블록되면 내부에서 무슨 일이 일어나나?", "Zephyr는 PendSV에서 무엇을 하나?" 같은 질문은 소스를 직접 읽어 본 사람만 함수 이름과 순서로 답한다. 입사 후에는 벤더 SDK에 딸려 온 낯선 포트, 수정된 커널, "가끔 태스크가 안 깬다" 같은 버그를 만났을 때 커널 소스를 30분 안에 읽어 내는 능력이 곧 디버깅 속도다
> **Don 기준 난이도**: 큰 C 코드베이스 읽기 · JTAG로 콜 스택 보기 · objdump는 SSD 펌웨어에서 매일 했다 / 새로 배울 부분은 FreeRTOS 특유의 매크로 층, "태스크 쪽 스택"과 "핸들러 쪽 스택"이 끊어진 backtrace를 읽는 법, 두 커널이 스케줄링 결정을 서로 다른 시점에 한다는 사실, 그리고 `--gc-sections`가 gdb의 함수 이름을 속이는 함정
> **선행 노트**: A3(RTOS 구성 요소 — 소스 트리 줄 수 · footprint는 거기서 끝냈다), A4(태스크 모델), B2(예외 진입 · 복귀 — `<signal handler called>`의 정체), B4(SysTick · PendSV · SVC), C2(PendSV 19개 명령), C7(크리티컬 섹션). 같은 모듈의 C1(자료구조) · C3(첫 태스크 시작) · C5(tick) · C8(커널 진입 경로)과는 ID로만 연결한다

---

## 0. 큰 그림 — 왜 "읽는 법"을 따로 배우나

Don이 SSD 컨트롤러 팀에 처음 들어갔을 때를 떠올려 보자. 수십만 줄짜리 펌웨어가 있고, 첫 버그 티켓은 "가끔 명령 하나가 타임아웃 난다"였다. 그때 처음부터 끝까지 파일을 읽지 않았을 것이다. 대신 **명령 큐 구조체가 어떻게 생겼는지** 먼저 보고, **명령이 들어와서 나가는 한 경로**(doorbell ISR → 디스패처 → NAND 작업 → 완료 ISR)를 JTAG 중단점으로 따라갔을 것이다. 커널 소스 읽기도 똑같다. 다만 RTOS 커널에는 세 가지 특별한 장애물이 있다.

1. **매크로 층**: FreeRTOS `tasks.c`는 8967줄인데, 이 노트의 설정에서 실제로 컴파일되는 줄은 3226줄이다(§3에서 잰다). 나머지는 주석, 다른 설정용 `#if` 가지, 빈 trace 훅이다.
2. **끊어진 콜 스택**: 컨텍스트 스위치는 "함수 호출"이 아니다. 태스크는 PendSV 예외로 빠져나가고, 다른 태스크의 스택으로 돌아온다. 그래서 backtrace가 PendSV에서 뚝 끊긴다. 어디서 끊기는지 알아야 그래프를 이어 붙일 수 있다.
3. **결정과 실행이 떨어져 있다**: "누가 다음에 도나"를 정하는 코드와 레지스터를 바꾸는 코드가 다른 파일, 다른 시점에 있다. FreeRTOS와 Zephyr는 그 시점이 다르다(§8).

이 노트는 소스 트리의 줄 수나 footprint를 다시 세지 않는다(그건 A3이 했다). 대신 **안내를 받으며 읽는 투어**를 한다. 지도는 실제로 QEMU에서 멈춰 세우고 찍은 gdb backtrace다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">커널 소스를 읽는 네 단계 — 왼쪽에서 오른쪽으로, 막히면 한 칸 왼쪽으로 돌아간다</text>
<rect x="10" y="40" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="62" font-size="13" text-anchor="middle" font-weight="bold">① 자료구조 먼저</text>
<rect x="10" y="82" width="150" height="112" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="85" y="104" font-size="12" text-anchor="middle">TCB_t · List_t</text>
<text x="85" y="128" font-size="12" text-anchor="middle">pxReadyTasksLists</text>
<text x="85" y="152" font-size="12" text-anchor="middle">struct k_thread</text>
<text x="85" y="176" font-size="12" text-anchor="middle">_kernel.ready_q</text>
<line x1="162" y1="57" x2="178" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="180" y="40" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="62" font-size="13" text-anchor="middle" font-weight="bold">② 핫 패스</text>
<rect x="180" y="82" width="150" height="112" rx="5" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
<text x="255" y="104" font-size="12" text-anchor="middle">tick (SysTick)</text>
<text x="255" y="128" font-size="12" text-anchor="middle">PendSV 스위치</text>
<text x="255" y="152" font-size="12" text-anchor="middle">give / take</text>
<text x="255" y="176" font-size="12" text-anchor="middle">FromISR 경로</text>
<line x1="332" y1="57" x2="348" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="350" y="40" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="425" y="62" font-size="13" text-anchor="middle" font-weight="bold">③ API 하나 끝까지</text>
<rect x="350" y="82" width="150" height="112" rx="5" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
<text x="425" y="104" font-size="12" text-anchor="middle">xQueueSend →</text>
<text x="425" y="128" font-size="12" text-anchor="middle">PendSV까지</text>
<text x="425" y="152" font-size="12" text-anchor="middle">k_sem_give →</text>
<text x="425" y="176" font-size="12" text-anchor="middle">z_arm_pendsv까지</text>
<line x1="502" y1="57" x2="518" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="520" y="40" width="150" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="595" y="62" font-size="13" text-anchor="middle" font-weight="bold">④ 실행으로 확인</text>
<rect x="520" y="82" width="150" height="112" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
<text x="595" y="104" font-size="12" text-anchor="middle">gdb bt (QEMU)</text>
<text x="595" y="128" font-size="12" text-anchor="middle">objdump · nm</text>
<text x="595" y="152" font-size="12" text-anchor="middle">gcc -E 전개</text>
<text x="595" y="176" font-size="12" text-anchor="middle">addr2line</text>
<rect x="10" y="208" width="660" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="226" font-size="12" text-anchor="middle" font-weight="bold">함수마다 묻는 세 질문</text>
<text x="340" y="242" font-size="12" text-anchor="middle">어떤 전역 상태를 바꾸나? · 누가 부르나(태스크/ISR)? · 어디서 블록·양보하나?</text>
<text x="10" y="274" font-size="12">도구: grep -n · gcc -E · gcc -fcallgraph-info · gdb (QEMU -S, bt, ptype /o)</text>
<text x="10" y="292" font-size="12">      objdump -d · nm --size-sort · addr2line</text>
<text x="10" y="310" font-size="12">이 VM에는 ctags · cscope · global · cflow가 없다 → 위 도구만으로 충분히 읽힌다</text>
</svg>
```
그림 1 — 커널 소스를 읽는 네 단계. 자료구조를 모르고 함수를 읽으면 "리스트에서 무엇을 빼는지" 모르는 채로 줄만 따라가게 된다. 막히면 한 칸 왼쪽으로 돌아간다.

### 0.1 이 노트의 지도

| 절 | 내용 | 대표 예제 |
|---|---|---|
| 1 | 읽기 전략과 도구 상자 — 이 VM에 무엇이 있고 없나 | 예제 1 도구 점검 |
| 2 | 단계 1: 자료구조 먼저 — TCB · List_t · `_kernel` 배치를 gdb로 | 예제 2 `ptype /o` |
| 3 | 매크로 미로 — 다섯 가족과 `gcc -E` 전/후 | 예제 3 인구 조사 · 예제 4 전개 · 예제 5 이름 바꾸기 매크로 |
| 4 | FreeRTOS 투어 — 다섯 경로를 gdb로 밟고 그래프로 | 예제 6 앱 · 예제 7 backtrace · 예제 8 gdb 함정 · 예제 9 정적 콜 그래프 |
| 5 | Zephyr 투어 — 같은 다섯 경로 | 예제 10 앱 · 예제 11 backtrace · 예제 12 협력형 확인 |
| 6 | 크기 · 복잡도로 읽을 순서 정하기 → "먼저 읽을 15개" | 예제 13 크기 · 예제 14 분기 수 · 예제 15 기계어 분기 · 예제 16 이름 검증 |
| 7 | 읽기 체크리스트와 2주 읽기 계획 | — |
| 8 | 커널 안에서는 — 두 커널의 결정 시점 | — |
| 9~14 | 실무 · 실수 · 면접 · 연습 · 용어 · 요약 | — |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) 위의 FreeRTOS V11.3.2(ARM_CM3 포트, `/var/rtoslab/templates/freertos-mps2`)와 Zephyr v4.4.2(`rtos-zbuild mps2/an385`), 같은 가상 보드. 실행 출력은 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간), **gdb로 멈춰 세운 실행은 icount 없이** 돌렸다(아래 §4.7에서 이유를 설명한다 — gdb 정지가 끼면 tick 숫자는 의미가 없고, 우리가 보는 것은 "호출 순서"다). 분석 도구는 VM의 `arm-none-eabi-gcc 14.2` · `gdb`(Ubuntu, ARM 지원) · `objdump` · `nm` · Python 3. 시간 숫자가 나오면 "QEMU 기준, 실제 칩과 절대값은 다르다(캐시 · 플래시 wait state · 예외 스태킹 사이클 미모델링)".

---

## 1. 읽기 전략과 도구 상자

### 1.1 세 단계 — 왜 이 순서인가

**단계 1: 자료구조 먼저.** 커널 함수의 90%는 "전역 자료구조 몇 개를 한 상태에서 다른 상태로 옮기는 일"이다. FreeRTOS라면 `pxReadyTasksLists[]`(우선순위별 ready 리스트), `xDelayedTaskList1/2`(깨울 시각 순 정렬 리스트), `xPendingReadyList`(스케줄러 잠금 중에 깬 태스크), 각 TCB의 `xStateListItem` · `xEventListItem` 두 고리. 이걸 머리에 그려 두면 `listREMOVE_ITEM( &( pxTCB->xStateListItem ) )` 한 줄이 "이 태스크를 지금 있는 상태 리스트(ready든 delayed든)에서 뺀다"로 읽힌다. 깊은 설명은 C1이 맡는다.

**단계 2: 핫 패스.** 1초에 수천 번 도는 코드가 커널의 심장이다. tick 핸들러, PendSV(컨텍스트 스위치), give/take의 "깨우기" 부분, FromISR. 핫 패스는 짧고(대부분 100줄 안쪽), 여기서 커널의 설계 철학(ISR은 짧게, 결정은 미룬다)이 다 드러난다.

**단계 3: API 하나를 끝까지.** `xQueueSend` 하나를 골라 "사용자 호출 → 커널 리스트 변경 → 스위치 요청 → PendSV → 다른 태스크"까지 끊김 없이 따라간다. 이 한 줄을 다 읽으면 나머지 API(세마포어, 뮤텍스, 이벤트 그룹)는 같은 부품의 재조합이라서 빨리 읽힌다.

**단계 4: 실행으로 확인.** 읽은 대로 도는지 gdb로 한 번 밟는다. Don이 SSD에서 "코드 리뷰만으로는 못 믿고 로직 분석기로 확인"하던 습관 그대로다. 정적으로 읽은 그래프와 실제 backtrace가 다르면, 다른 쪽이 정답이다(§5.4에서 실제로 그런 일이 생긴다).

### 1.2 도구 상자

| 도구 | 무엇을 알려 주나 | 이 노트의 예제 |
|---|---|---|
| `grep -n` | 정의 위치 · 호출 위치 (파일:줄) | 전체 |
| `gcc -E` (`-P` 유무) | 매크로 · `#if`를 이 설정으로 걷어 낸 "정답 소스" | 예제 4 · 14 |
| `gcc -fcallgraph-info` | 컴파일러가 본 정적 호출 그래프 (`.ci` 파일) | 예제 9 |
| gdb + QEMU `-S` · `bt` | 실제로 도는 호출 경로 (동적) | 예제 7 · 11 |
| gdb `ptype /o` | 구조체 필드 오프셋 · 크기 | 예제 2 |
| `objdump -d` | 컴파일러가 실제 만든 명령 (인라인 · 분할 확인) | 예제 5 · 15 |
| `nm --size-sort -S` | 함수 크기 순위 · 숨은 함수(`.part.0`) | 예제 13 |
| `addr2line -f -i` | 주소 → 함수 · 줄 (gdb 이름을 의심할 때) | 예제 8 |
| ctags · cscope · GNU global · cflow | 심볼 탐색 · 정적 콜 그래프 | 이 VM에는 없다 (예제 1) |

**예제 1 — 이 VM에 어떤 소스 탐색 도구가 있나**

무엇을 확인하는 코드인지: 흔히 쓰는 소스 탐색 도구가 설치되어 있는지 본다(설치는 하지 않는다 — 공용 VM 규칙).

```sh
which ctags cscope global cflow gdb gdb-multiarch arm-none-eabi-gdb clang-format 2>&1 | sed "s/^/which: /"
for t in ctags cscope global cflow; do command -v $t >/dev/null || echo "$t: not installed"; done
```

```text
which: /usr/bin/gdb
ctags: not installed
cscope: not installed
global: not installed
cflow: not installed
```

출력에서 볼 것: 심볼 인덱서가 하나도 없다. 그래도 문제없다. **`grep -n`으로 정의를 찾고, `gcc -fcallgraph-info`로 정적 그래프를, gdb로 동적 그래프를** 얻으면 인덱서가 하는 일의 대부분을 대신한다. 집 Mac이나 회사 PC에는 `universal-ctags`와 `cscope`를 깔아 두면 편하다(에디터에서 정의로 점프). 하지만 면접장이나 고객사 서버처럼 아무것도 없는 곳에서도 읽을 수 있어야 한다.

### 1.3 흔한 함정 (전략)

- **파일 맨 위부터 읽기**: `tasks.c`의 첫 600줄은 매크로 정의와 전역 선언이다. 여기서 지친다. 전역 선언(§2)만 표로 옮겨 적고 바로 핫 패스로 간다.
- **모든 `#if` 가지를 다 읽기**: SMP(`configNUMBER_OF_CORES > 1`), MPU, 정적 할당 가지는 지금 설정과 무관하다. `gcc -E`로 걷어 낸다(§3).
- **정적 그래프만 믿기**: 함수 포인터(`t->fn(t)`), 매크로 안의 호출, 인라인 함수는 정적 도구가 놓치거나 잘못 그린다. 반드시 한 번은 gdb로 밟는다.

---

## 2. 단계 1: 자료구조 먼저 — 지도부터 펼친다

### 2.1 FreeRTOS 커널의 전역 상태 (tasks.c)

FreeRTOS의 스케줄러 상태는 `tasks.c` 상단 전역 변수에 다 있다(V11.3.2 줄 번호).

| 변수 (tasks.c:줄) | 뜻 | 누가 바꾸나 |
|---|---|---|
| `pxCurrentTCB` (465) | 지금 CPU를 쥔 태스크의 TCB | `vTaskSwitchContext`만 |
| `pxReadyTasksLists[configMAX_PRIORITIES]` (478) | 우선순위별 ready 리스트 | `prvAddTaskToReadyList` 매크로, `uxListRemove` |
| `xDelayedTaskList1` · `2` (479~) | 깨울 tick 순으로 정렬된 지연 리스트 두 개 (오버플로 대비) | `prvAddCurrentTaskToDelayedList`, `xTaskIncrementTick` |
| `xPendingReadyList` (483) | 스케줄러 잠금 중에 깬 태스크 대기실 | `xTaskRemoveFromEventList`, `xTaskResumeAll` |
| `xSuspendedTaskList` (494) | 무기한 대기(`portMAX_DELAY`) · suspend | `prvAddCurrentTaskToDelayedList` |
| `xTickCount` (506) | tick 카운터 | `xTaskIncrementTick`만 |
| `uxTopReadyPriority` (507) | ready 우선순위 비트맵 (CM3 포트는 비트맵 + CLZ) | `portRECORD_READY_PRIORITY` |
| `xYieldPendings[]` (510) | "스위치가 필요하다" 표시 | `xTaskRemoveFromEventList`, `vTaskSwitchContext` |
| `uxSchedulerSuspended` (533) | `vTaskSuspendAll` 중첩 수 | `vTaskSuspendAll` / `xTaskResumeAll` |

**예제 2 — gdb `ptype /o`로 TCB와 리스트의 바이트 배치 보기**

무엇을 확인하는 코드인지: 실제 빌드(예제 6의 ELF)의 DWARF 정보로 TCB · `List_t` · `ListItem_t`의 필드 오프셋을 찍는다. QEMU 없이 ELF만으로 된다.

```sh
gdb -q -batch -ex "ptype /o struct tskTaskControlBlock" -ex "ptype /o List_t" \
    -ex "ptype /o struct xLIST_ITEM" /var/tmp/rtos/C12/fr_tour_g/app.elf
```

```text
/* offset      |    size */  type = struct tskTaskControlBlock {
/*      0      |       4 */    volatile StackType_t *pxTopOfStack;
/*      4      |      20 */    ListItem_t xStateListItem;
/*     24      |      20 */    ListItem_t xEventListItem;
/*     44      |       4 */    UBaseType_t uxPriority;
/*     48      |       4 */    StackType_t *pxStack;
/*     52      |      16 */    char pcTaskName[16];
/*     68      |       4 */    UBaseType_t uxTCBNumber;
/*     72      |       4 */    UBaseType_t uxTaskNumber;
/*     76      |       4 */    UBaseType_t uxBasePriority;
/*     80      |       4 */    UBaseType_t uxMutexesHeld;
/*     84      |       4 */    uint32_t ulRunTimeCounter;
/*     88      |       4 */    volatile uint32_t ulNotifiedValue[1];
/*     92      |       1 */    volatile uint8_t ucNotifyState[1];
/*     93      |       1 */    uint8_t ucStaticallyAllocated;
/*     94      |       1 */    uint8_t ucDelayAborted;
/* XXX  1-byte padding   */

                               /* total size (bytes):   96 */
                             }
type = struct xLIST {
/*      0      |       4 */    UBaseType_t uxNumberOfItems;
/*      4      |       4 */    ListItem_t *pxIndex;
/*      8      |      12 */    MiniListItem_t xListEnd;

                               /* total size (bytes):   20 */
                             }
/* offset      |    size */  type = struct xLIST_ITEM {
/*      0      |       4 */    TickType_t xItemValue;
/*      4      |       4 */    struct xLIST_ITEM *pxNext;
/*      8      |       4 */    struct xLIST_ITEM *pxPrevious;
/*     12      |       4 */    void *pvOwner;
/*     16      |       4 */    struct xLIST *pvContainer;

                               /* total size (bytes):   20 */
                             }
```

출력에서 볼 것:

- `pxTopOfStack`이 **오프셋 0**이다. 그래서 PendSV 어셈블리가 `ldr r2, [r3]`(r2 = TCB 주소) 다음에 `str r0, [r2]` 한 줄로 "스택 꼭대기 저장"을 끝낸다(C2). 소스 주석에 "MUST BE THE FIRST MEMBER"라고 적힌 이유가 이 오프셋이다.
- TCB 안에 리스트 고리가 **두 개**다. `xStateListItem`은 ready/delayed/suspended 중 하나에, `xEventListItem`은 "무엇을 기다리나"(큐의 `xTasksWaitingToSend` 등)에 걸린다. 블로킹 send는 두 고리를 **동시에** 건다(§4.3). 하나는 "언제까지", 하나는 "무엇을".
- `ListItem_t.pvContainer`가 "지금 어느 리스트에 있나"를 기억한다. 그래서 `listREMOVE_ITEM`은 리스트 포인터 인자 없이 항목만 받아도 뺄 수 있다.
- 이 설정의 TCB는 96바이트다(설정마다 다르다 — A3에서 설정별로 쟀다).

### 2.2 Zephyr 커널의 전역 상태

Zephyr는 전역 상태를 `struct z_kernel _kernel`(kernel/init.c:48) 하나에 모았다. 단일 코어 설정에서 핵심은 두 필드다.

```sh
gdb -q -batch -ex "ptype /o struct z_kernel" /var/tmp/rtos/C12/z_tour/zephyr/zephyr.elf
```

```text
/* offset      |    size */  type = struct z_kernel {
/*      0      |      20 */    struct _cpu cpus[1];
/*     20      |      12 */    struct _ready_q {
/*     20      |       4 */        struct k_thread *cache;
/*     24      |       8 */        sys_dlist_t runq;
                                   /* total size (bytes):   12 */
                               } ready_q;
                               /* total size (bytes):   32 */
                             }
```

```sh
gdb -q -batch -ex "print sizeof(struct k_thread)" -ex "print /d (int)&((struct k_thread*)0)->callee_saved" \
    -ex "print /d (int)&((struct k_thread*)0)->arch" -ex "print /d (int)&((struct k_thread*)0)->base.timeout" \
    -ex "ptype struct _callee_saved" /var/tmp/rtos/C12/z_tour/zephyr/zephyr.elf
```

```text
$1 = 128
$2 = 48
$3 = 116
$4 = 24
type = struct _callee_saved {
    uint32_t v1;
    uint32_t v2;
    uint32_t v3;
    uint32_t v4;
    uint32_t v5;
    uint32_t v6;
    uint32_t v7;
    uint32_t v8;
    uint32_t psp;
}
```

출력에서 볼 것:

- `_kernel.cpus[0].current`가 FreeRTOS의 `pxCurrentTCB`, `_kernel.ready_q.runq`가 ready 리스트(이 설정은 `CONFIG_SCHED_SIMPLE=y` — 우선순위 순 이중 연결 리스트 하나)다.
- **`_kernel.ready_q.cache`** 가 FreeRTOS에 없는 필드다. "다음에 돌 스레드"를 미리 골라 둔 캐시다. 이게 두 커널의 가장 큰 구조 차이이고, §5와 §8 전체가 이 한 필드 이야기다.
- `struct _callee_saved`(v1~v8 = R4~R11, 그리고 psp)가 TCB 안 오프셋 48에 있다. **Zephyr는 R4~R11을 스레드 구조체에 저장**하고, FreeRTOS는 **태스크 스택에** 저장한다(`stmdb r0!, {r4-r11}`). 같은 Cortex-M3인데 설계가 다르다. 덤프에서 레지스터를 찾을 때 어디를 볼지가 달라진다.
- `base.timeout`(오프셋 24)이 스레드마다 하나씩 박혀 있다. `k_sleep`은 이 노드를 타임아웃 리스트에 꽂는다(§5.3).

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">어느 파일에 무엇이 있나 — 화살표 = "이 파일이 저 파일의 함수를 부른다"</text>
<text x="165" y="44" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS V11.3.2</text>
<text x="515" y="44" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2 (mps2/an385)</text>
<rect x="20" y="60" width="290" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="79" font-size="12" text-anchor="middle" font-weight="bold">queue.c  3400줄</text>
<text x="165" y="95" font-size="12" text-anchor="middle">큐 · 세마포어 · 뮤텍스</text>
<rect x="20" y="130" width="290" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="149" font-size="12" text-anchor="middle" font-weight="bold">tasks.c  8967줄</text>
<text x="165" y="165" font-size="12" text-anchor="middle">TCB · ready list · tick · 스위치 결정</text>
<rect x="20" y="200" width="290" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="165" y="219" font-size="12" text-anchor="middle" font-weight="bold">list.c  248줄</text>
<text x="165" y="235" font-size="12" text-anchor="middle">이중 연결 리스트 연산</text>
<rect x="20" y="292" width="290" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="165" y="311" font-size="12" text-anchor="middle" font-weight="bold">port.c  822줄 (ARM_CM3)</text>
<text x="165" y="327" font-size="12" text-anchor="middle">PendSV · SysTick · SVC · 초기 스택</text>
<line x1="80" y1="106" x2="80" y2="128" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="90" y="121" font-size="12">PlaceOn / RemoveFromEventList</text>
<line x1="80" y1="176" x2="80" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="90" y="191" font-size="12">vListInsert · listINSERT_END</text>
<path d="M 20,315 C -2,260 -2,200 18,160" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="24" y="266" font-size="12">vTaskSwitchContext</text>
<text x="24" y="282" font-size="12">xTaskIncrementTick</text>
<rect x="370" y="60" width="290" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="515" y="79" font-size="12" text-anchor="middle" font-weight="bold">kernel/sem.c  237줄</text>
<text x="515" y="95" font-size="12" text-anchor="middle">k_sem give / take</text>
<rect x="370" y="130" width="290" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515" y="149" font-size="12" text-anchor="middle" font-weight="bold">kernel/sched.c  1637줄</text>
<text x="515" y="165" font-size="12" text-anchor="middle">ready_thread · update_cache · sleep</text>
<rect x="370" y="200" width="290" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="515" y="219" font-size="12" text-anchor="middle" font-weight="bold">kernel/timeout.c  364줄</text>
<text x="515" y="235" font-size="12" text-anchor="middle">z_add_timeout · announce</text>
<rect x="370" y="292" width="290" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="515" y="311" font-size="12" text-anchor="middle" font-weight="bold">cortex_m/swap_helper.S  369줄</text>
<text x="515" y="327" font-size="12" text-anchor="middle">z_arm_pendsv (결정 안 함)</text>
<line x1="420" y1="106" x2="420" y2="128" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="430" y="121" font-size="12">z_ready_thread · z_reschedule</text>
<line x1="420" y1="176" x2="420" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="430" y="191" font-size="12">z_add_thread_timeout</text>
<line x1="420" y1="246" x2="420" y2="290" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
<text x="430" y="274" font-size="12">arch_swap → PendSV (sched.c에서)</text>
</svg>
```
그림 2 — 핵심 파일 지도. 왼쪽 FreeRTOS는 "결정"이 `tasks.c`에 모여 있고 `port.c`가 그것을 부른다. 오른쪽 Zephyr는 결정이 `sched.c`에서 끝나고, 어셈블리(`swap_helper.S`)는 결정을 읽기만 한다. 줄 수는 `wc -l`.

### 2.3 Don 경험과 연결

SSD 펌웨어의 명령 디스패처에도 "ready 큐", "NAND 대기 큐", "완료 큐"가 있고 명령 구조체에 큐 고리가 두세 개 박혀 있었을 것이다. FreeRTOS TCB의 `xStateListItem` / `xEventListItem`은 정확히 그 패턴(intrusive list — 항목이 노드를 품는 리스트)이다. 차이는 하나다. SSD 디스패처는 보통 "어느 큐가 비었나"를 루프에서 폴링하지만, RTOS는 **리스트 이동이 일어날 때마다 즉시 "스위치가 필요한가"를 판단**한다.

---

## 3. 매크로 미로 — FreeRTOS를 읽기 어렵게 만드는 것의 정체

### 3.1 다섯 가족

FreeRTOS는 한 소스로 40여 개 아키텍처와 수백 가지 설정 조합을 지원한다. 그 대가가 매크로다. 처음 보면 함수의 절반이 대문자 매크로라서 길을 잃는다. 가족별로 "무시해도 되는지"만 알면 된다.

| 가족 | 예 | 정체 | 읽을 때 |
|---|---|---|---|
| `configXXX` | `configUSE_PREEMPTION`, `configNUMBER_OF_CORES` | `FreeRTOSConfig.h`의 컴파일 타임 설정. `#if`로 코드 가지를 고른다 | 지금 설정 값을 옆에 펴 두고 읽는다. 또는 `gcc -E` |
| `traceXXX` | `traceENTER_xQueueGenericSend`, `traceTASK_SWITCHED_IN` | 추적 훅. 기본값은 빈 매크로(FreeRTOS.h) | **무시**. SystemView 등이 채운다(M3) |
| `portXXX` | `portYIELD`, `portENTER_CRITICAL`, `portGET_HIGHEST_PRIORITY` | CPU별 구현으로 바뀌는 자리. `portmacro.h`에 정의 | **반드시 정의를 따라간다** — 여기가 하드웨어와 닿는 곳 |
| `mtCOVERAGE_TEST_MARKER()` | `else { mtCOVERAGE_TEST_MARKER(); }` | 테스트 커버리지 측정용 빈 매크로(FreeRTOS.h:2792) | **무시**. 이 `else` 가지는 "아무 일도 안 함"이다 |
| `PRIVILEGED_FUNCTION` · `PRIVILEGED_DATA` | 함수 선언 끝 | MPU 포트에서 특권 섹션에 배치(mpu_wrappers.h:283), 아니면 빈 매크로 | **무시** (MPU 포트가 아니면). F 모듈에서 다시 본다 |

그 밖에 `taskXXX`(tasks.h의 얇은 래퍼: `taskENTER_CRITICAL()` → `portENTER_CRITICAL()` → `vPortEnterCritical()`), `listXXX`(list.h의 인라인 리스트 연산 매크로), `STATIC`(tasks.c:151 — 단위 테스트용으로 `static`을 끌 수 있게 한 것)이 있다.

**예제 3 — 매크로 가족 인구 조사**

무엇을 확인하는 코드인지: 주석을 지운 `tasks.c` · `queue.c`에서 매크로 가족별 등장 횟수와 `#if` 계열 줄 수를 센다.

```python
#!/usr/bin/env python3
"""census.py <file.c>... — FreeRTOS "매크로 미로" 인구 조사: 접두어별 매크로 사용 횟수와 #if 줄 수"""
import re, sys, collections
for path in sys.argv[1:]:
    src = open(path, errors='replace').read()
    code = re.sub(r'/\*.*?\*/', '', src, flags=re.S)          # 주석 제거
    lines = code.split('\n')
    pp = sum(1 for l in lines if re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b', l))
    c = collections.Counter()
    for name in re.findall(r'\b(trace[A-Z]\w*|config[A-Z]\w*|port[A-Z]\w*|mtCOVERAGE_TEST_MARKER|PRIVILEGED_\w+|list[A-Z]\w*|task[A-Z_]\w*)\b', code):
        key = ('mtCOVERAGE' if name.startswith('mt') else 'PRIVILEGED_' if name.startswith('PRIV') else re.match(r'[a-z]+', name).group(0))
        c[key] += 1
    nonblank = sum(1 for l in lines if l.strip())
    print(f"{path.split('/')[-1]}: {len(src.splitlines())} lines, {nonblank} non-blank code lines, {pp} #if/#else/#endif lines")
    for k in ['config', 'trace', 'port', 'task', 'list', 'mtCOVERAGE', 'PRIVILEGED_']:
        print(f"   {k:<12} {c[k]:5}")
```

```sh
cd /var/rtoslab/src/FreeRTOS-Kernel && python3 /Users/donh/.rtos_scratch/C12/census.py tasks.c queue.c
```

```text
tasks.c: 8967 lines, 5540 non-blank code lines, 719 #if/#else/#endif lines
   config         540
   trace          229
   port           222
   task           208
   list            94
   mtCOVERAGE     125
   PRIVILEGED_     43
queue.c: 3400 lines, 2192 non-blank code lines, 133 #if/#else/#endif lines
   config         128
   trace          137
   port            21
   task            54
   list            23
   mtCOVERAGE      84
   PRIVILEGED_     10
```

출력에서 볼 것: `tasks.c`에는 `#if` 계열 줄만 719줄이고, `config` 매크로가 540번 나온다. `trace`(229)와 `mtCOVERAGE`(125)는 **기본 설정에서 전부 빈 매크로**라서, 읽을 때 머릿속에서 지워도 된다. 반대로 `port`(222)는 하나하나가 하드웨어 동작이다.

### 3.2 `gcc -E`로 걷어 내기 — "이 설정의 정답 소스"

`gcc -E`는 전처리만 하고 멈춘다. `#include`를 펼치고, `#if`를 이 설정으로 판정해 버릴 가지를 지우고, 매크로를 전부 전개한다. `-P`를 붙이면 `# 123 "file"` 줄 표지를 뺀다(사람이 읽기 좋다). 표지를 남기면 어느 줄이 어느 파일에서 왔는지 추적할 수 있다(예제 14이 그걸 쓴다).

**예제 4 — 같은 함수, 전개 전과 후**

무엇을 확인하는 코드인지: 템플릿 Makefile과 **같은 `-I` · `-mcpu` 플래그**로 `tasks.c`를 전처리하고, `xTaskRemoveFromEventList` 정의 하나만 잘라 본다.

```sh
#!/bin/bash
# expand.sh <func> — tasks.c를 템플릿과 같은 플래그로 gcc -E 한 뒤 함수 정의 하나만 잘라 보여 준다
K=/var/rtoslab/src/FreeRTOS-Kernel; T=/var/rtoslab/templates; O=/var/tmp/rtos/C12/pp; mkdir -p $O
arm-none-eabi-gcc -E -P -mthumb -mcpu=cortex-m3 -I$T/freertos-mps2 -I$T/common -I$K/include \
  -I$K/portable/GCC/ARM_CM3 $K/tasks.c -o $O/tasks.i
echo "tasks.c: $(wc -l < $K/tasks.c) lines  ->  tasks.i: $(wc -l < $O/tasks.i) lines (headers included)"
python3 - "$1" $O/tasks.i <<'PY'
import sys, re
f, path = sys.argv[1], sys.argv[2]
L = open(path).read().split('\n')
for i, s in enumerate(L):
    if re.match(r'^\s*(BaseType_t|void) ' + f + r'\(', s) and not s.rstrip().endswith(';'):
        j = i
        while not L[j].rstrip().endswith(')'): j += 1
        if L[j + 1].strip() == '{':
            k = j + 1
            while L[k] != '}': k += 1
            body = [x for x in L[i:k + 1] if x.strip()]
            print(f"--- {f}: {len(body)} non-empty lines after -E ---")
            print('\n'.join(body)); break
PY
```

전개 **전** — 원본 `tasks.c:5500~5590` 91줄에서 주석과 빈 줄만 뺀 모습(줄 번호는 원본 그대로):

```c
 5500: BaseType_t xTaskRemoveFromEventList( const List_t * const pxEventList )
 5501: {
 5502:     TCB_t * pxUnblockedTCB;
 5503:     BaseType_t xReturn;
 5505:     traceENTER_xTaskRemoveFromEventList( pxEventList );
 5523:     pxUnblockedTCB = listGET_OWNER_OF_HEAD_ENTRY( pxEventList );
 5524:     configASSERT( pxUnblockedTCB );
 5525:     listREMOVE_ITEM( &( pxUnblockedTCB->xEventListItem ) );
 5527:     if( uxSchedulerSuspended == ( UBaseType_t ) 0U )
 5528:     {
 5529:         listREMOVE_ITEM( &( pxUnblockedTCB->xStateListItem ) );
 5530:         prvAddTaskToReadyList( pxUnblockedTCB );
 5532:         #if ( configUSE_TICKLESS_IDLE != 0 )
 5533:         {
 5542:             prvResetNextTaskUnblockTime();
 5543:         }
 5544:         #endif
 5545:     }
 5546:     else
 5547:     {
 5550:         listINSERT_END( &( xPendingReadyList ), &( pxUnblockedTCB->xEventListItem ) );
 5551:     }
 5553:     #if ( configNUMBER_OF_CORES == 1 )
 5554:     {
 5555:         if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )
 5556:         {
 5560:             xReturn = pdTRUE;
 5564:             xYieldPendings[ 0 ] = pdTRUE;
 5565:         }
 5566:         else
 5567:         {
 5568:             xReturn = pdFALSE;
 5569:         }
 5570:     }
 5571:     #else
 5572:     {
 5573:         xReturn = pdFALSE;
 5575:         #if ( configUSE_PREEMPTION == 1 )
 5576:         {
 5577:             prvYieldForTask( pxUnblockedTCB );
 5579:             if( xYieldPendings[ portGET_CORE_ID() ] != pdFALSE )
 5580:             {
 5581:                 xReturn = pdTRUE;
 5582:             }
 5583:         }
 5584:         #endif
 5585:     }
 5586:     #endif
 5588:     traceRETURN_xTaskRemoveFromEventList( xReturn );
 5589:     return xReturn;
 5590: }
```

(위 발췌는 주석 · 빈 줄을 지우고 원래 줄 번호를 붙여 주는 작은 스크립트 `src.py`로 뽑았다. 이 노트의 모든 소스 발췌가 같은 방식이다.)

전개 **후** — `/var/tmp/rtos/C12/expand.sh xTaskRemoveFromEventList` 실행 결과:

```text
tasks.c: 8967 lines  ->  tasks.i: 4214 lines (headers included)
--- xTaskRemoveFromEventList: 31 non-empty lines after -E ---
BaseType_t xTaskRemoveFromEventList( const List_t * const pxEventList )
{
    TCB_t * pxUnblockedTCB;
    BaseType_t xReturn;
    ;
    pxUnblockedTCB = ( ( &( ( pxEventList )->xListEnd ) )->pxNext->pvOwner );
    do { if( !( pxUnblockedTCB ) ) { board_printf_raw( "\n[configASSERT] %s:%d\n", "/var/rtoslab/src/FreeRTOS-Kernel/tasks.c", 5524 ); board_exit( 253 ); } } while( 0 );
    do { List_t * const pxList = ( &( pxUnblockedTCB->xEventListItem ) )->pvContainer; ( &( pxUnblockedTCB->xEventListItem ) )->pxNext->pxPrevious = ( &( pxUnblockedTCB->xEventListItem ) )->pxPrevious; ( &( pxUnblockedTCB->xEventListItem ) )->pxPrevious->pxNext = ( &( pxUnblockedTCB->xEventListItem ) )->pxNext; if( pxList->pxIndex == ( &( pxUnblockedTCB->xEventListItem ) ) ) { pxList->pxIndex = ( &( pxUnblockedTCB->xEventListItem ) )->pxPrevious; } ( &( pxUnblockedTCB->xEventListItem ) )->pvContainer = ((void *)0); ( ( pxList )->uxNumberOfItems ) = ( UBaseType_t ) ( ( ( pxList )->uxNumberOfItems ) - 1U ); } while( 0 );
    if( uxSchedulerSuspended == ( UBaseType_t ) 0U )
    {
        do { List_t * const pxList = ( &( pxUnblockedTCB->xStateListItem ) )->pvContainer; ... (같은 리스트 삭제, 생략) ... } while( 0 );
        do { ; ( uxTopReadyPriority ) |= ( 1UL << ( ( ( pxUnblockedTCB )->uxPriority ) ) ); do { ListItem_t * const pxIndex = ( &( pxReadyTasksLists[ ( pxUnblockedTCB )->uxPriority ] ) )->pxIndex; ... (리스트 끝에 삽입, 생략) ... } while( 0 ); ; } while( 0 );
    }
    else
    {
        do { ListItem_t * const pxIndex = ( &( xPendingReadyList ) )->pxIndex; ... (생략) ... } while( 0 );
    }
    {
        if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )
        {
            xReturn = ( ( BaseType_t ) 1 );
            xYieldPendings[ 0 ] = ( ( BaseType_t ) 1 );
        }
        else
        {
            xReturn = ( ( BaseType_t ) 0 );
        }
    }
    ;
    return xReturn;
}
```

(긴 한 줄짜리 리스트 매크로 전개 세 곳은 `...`로 줄였다. 나머지는 실제 출력 그대로다.)

출력에서 볼 것:

- `traceENTER_...` / `traceRETURN_...`이 `;` 하나로 사라졌다. 빈 매크로라는 증거다.
- `#if ( configUSE_TICKLESS_IDLE != 0 )` 가지와 SMP 가지(5571~5585)가 통째로 없다. 이 설정에선 **존재하지 않는 코드**다. 원본에서 SMP 가지를 읽느라 쓴 시간은 버린 시간이다.
- `listGET_OWNER_OF_HEAD_ENTRY`는 `xListEnd.pxNext->pvOwner` — "리스트 끝 표지의 다음 = 맨 앞 항목의 주인 TCB". 이벤트 리스트는 우선순위 순으로 정렬돼 있으니(C1) 맨 앞 = 가장 높은 우선순위 대기자다.
- `prvAddTaskToReadyList`는 두 줄로 펼쳐진다: `uxTopReadyPriority |= 1 << prio`(비트맵에 표시) + 해당 우선순위 리스트 끝에 삽입. 이게 C1의 "O(1) ready 리스트"의 실체다.
- `configASSERT`는 템플릿이 정의한 `board_printf_raw` + `board_exit(253)`로 바뀐다. 실제 제품에서는 보통 `__BKPT` 또는 무한 루프다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">측정: 같은 파일이 이 설정(FreeRTOSConfig.h)에서 실제로 컴파일되는 양</text>
<text x="10" y="70" font-size="13" font-weight="bold">tasks.c</text>
<rect x="90.0" y="44.0" width="468.3" height="13.0" fill="#888" fill-opacity="0.75"/>
<text x="564" y="55" font-size="12">8967</text>
<rect x="90.0" y="61.0" width="289.3" height="13.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="385" y="72" font-size="12">5540</text>
<rect x="90.0" y="78.0" width="168.5" height="13.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="264" y="89" font-size="12">3226</text>
<text x="10" y="134" font-size="13" font-weight="bold">queue.c</text>
<rect x="90.0" y="108.0" width="177.6" height="13.0" fill="#888" fill-opacity="0.75"/>
<text x="274" y="119" font-size="12">3400</text>
<rect x="90.0" y="125.0" width="114.5" height="13.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="210" y="136" font-size="12">2192</text>
<rect x="90.0" y="142.0" width="92.9" height="13.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="189" y="153" font-size="12">1778</text>
<rect x="90.0" y="180.0" width="12.0" height="12.0" fill="#888" fill-opacity="0.75"/>
<text x="108" y="191" font-size="12">전체 줄</text>
<rect x="280.0" y="180.0" width="12.0" height="12.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="298" y="191" font-size="12">주석·빈 줄 뺀 코드</text>
<rect x="470.0" y="180.0" width="12.0" height="12.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="488" y="191" font-size="12">gcc -E 뒤 남는 줄</text>
<text x="10" y="222" font-size="13" font-weight="bold">함수 하나: xTaskRemoveFromEventList (tasks.c:5500–5590)</text>
<rect x="90.0" y="234.0" width="364.0" height="14.0" fill="#888" fill-opacity="0.75"/>
<text x="460" y="245" font-size="12">원본 91줄 (주석·#if 포함)</text>
<rect x="90.0" y="256.0" width="124.0" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="220" y="267" font-size="12">gcc -E 뒤 31줄 (비지 않은 줄)</text>
<text x="10" y="292" font-size="12">→ 읽어야 할 진짜 코드는 화면에 보이는 것의 약 1/3이다. -E 출력은 "이 설정의 정답지"다</text>
</svg>
```
그림 3 — 측정: 매크로 · `#if`를 걷어 내면 읽을 양이 1/3로 준다. "남는 줄"은 `gcc -E`(줄 표지 유지) 출력에서 원본 파일에 속한 비지 않은 줄을 센 값이다.

`tasks.c`에서 전처리 뒤 남는 줄 수(3226)와 `queue.c`(1778)는 아래 한 줄짜리 계산으로 셌다 — `# 줄번호 "파일"` 표지를 따라가며 원본 파일에 속한 비지 않은 줄만 센다.

```text
tasks.c lines surviving -E (this config): 3226
queue.c lines surviving -E (this config): 1778
```

### 3.3 이름을 바꾸는 매크로 — gdb에서 함수가 "안 보일" 때

**예제 5 — `xPortPendSVHandler`는 ELF 어디에 있나**

무엇을 확인하는 코드인지: 소스에는 `void xPortPendSVHandler( void )`(port.c:455)라고 적혀 있는데, ELF 심볼 표에서 그 이름을 찾고, PendSV 핸들러의 기계어를 본다.

```sh
E=/var/tmp/rtos/C12/fr_tour/app.elf
arm-none-eabi-nm $E | grep -E " (xPortPendSVHandler|PendSV_Handler|xPortSysTickHandler|SysTick_Handler|vPortSVCHandler|SVC_Handler|vTaskSwitchContext|xTaskIncrementTick|xTaskIncrementTick.part.0)$"
arm-none-eabi-objdump -d --no-show-raw-insn $E | sed -n "/<PendSV_Handler>:/,/^$/p"
arm-none-eabi-objdump -d --no-show-raw-insn $E | sed -n "/<SysTick_Handler>:/,/^$/p"
```

```text
000022dc T PendSV_Handler
00002298 T SVC_Handler
0000240c T SysTick_Handler
00000fa0 T vTaskSwitchContext
00000f80 T xTaskIncrementTick
000008e8 t xTaskIncrementTick.part.0
000022dc <PendSV_Handler>:
    22dc:	mrs	r0, PSP
    22e0:	isb	sy
    22e4:	ldr	r3, [pc, #52]	@ (231c <PendSV_Handler+0x40>)
    22e6:	ldr	r2, [r3, #0]
    22e8:	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
    22ec:	str	r0, [r2, #0]
    22ee:	push	{r3, lr}
    22f0:	mov.w	r0, #32
    22f4:	msr	BASEPRI, r0
    22f8:	bl	fa0 <vTaskSwitchContext>
    22fc:	mov.w	r0, #0
    2300:	msr	BASEPRI, r0
    2304:	ldmia.w	sp!, {r3, lr}
    2308:	ldr	r1, [r3, #0]
    230a:	ldr	r0, [r1, #0]
    230c:	ldmia.w	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
    2310:	msr	PSP, r0
    2314:	isb	sy
    2318:	bx	lr
    231a:	.short	0x0000
    231c:	.word	0x20000e78

0000240c <SysTick_Handler>:
    240c:	push	{r3, lr}
    240e:	mov.w	r3, #32
    2412:	msr	BASEPRI, r3
    2416:	isb	sy
    241a:	dsb	sy
    241e:	bl	f80 <xTaskIncrementTick>
    2422:	cbz	r0, 2430 <SysTick_Handler+0x24>
    2424:	mov.w	r3, #3758153728	@ 0xe000e000
    2428:	mov.w	r2, #268435456	@ 0x10000000
    242c:	str.w	r2, [r3, #3332]	@ 0xd04
    2430:	movs	r3, #0
    2432:	msr	BASEPRI, r3
    2436:	pop	{r3, pc}
```

출력에서 볼 것:

- `xPortPendSVHandler` · `xPortSysTickHandler` · `vPortSVCHandler`라는 심볼은 **없다**. 템플릿의 `FreeRTOSConfig.h`가 `#define xPortPendSVHandler PendSV_Handler`처럼 이름을 CMSIS 벡터 이름으로 바꿨기 때문이다. gdb에서 `break xPortPendSVHandler`가 "Function not defined"로 실패하면 이걸 의심한다. 소스 이름이 아니라 **ELF 이름**으로 중단점을 건다.
- `xTaskIncrementTick`(0xf80)과 별도로 `xTaskIncrementTick.part.0`(0x8e8, 소문자 `t` = 파일 지역)이 있다. GCC가 함수를 "자주 도는 짧은 앞부분"과 "드문 긴 뒷부분"으로 쪼갠 것(부분 인라인 · IPA split)이다. 크기 순위(예제 13)에서 1등이 이 `.part.0`이다. 소스에는 없는 이름이니 놀라지 말 것.
- PendSV의 `mov.w r0, #32` = `configMAX_SYSCALL_INTERRUPT_PRIORITY`(0x20)이 인라인 어셈블리의 `"i"` 제약으로 상수가 되어 박힌 것이다. 소스(port.c:471 `mov r0, %0`)만 보면 이 값이 무엇인지 모른다. 기계어가 "이 설정의 정답"이다.
- SysTick의 `str.w r2, [r3, #3332]` = `0xE000ED04`(ICSR)에 `1 << 28`(PENDSVSET) 쓰기 — port.c:507 `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;` 한 줄이다. `cbz r0`가 "xTaskIncrementTick이 pdFALSE면 건너뛴다"다.

### 3.4 흔한 함정 (매크로)

- **`gcc -E`를 다른 플래그로 돌리기**: `-I` 순서가 다르면 다른 `FreeRTOSConfig.h`를 집는다. 반드시 실제 빌드와 같은 플래그(Zephyr라면 `compile_commands.json`의 명령)로 돌린다(예제 14).
- **`-P` 출력의 줄 번호를 원본 줄 번호로 착각**: `-P`는 줄 표지를 지운다. 원본과 대조하려면 `-P` 없이 돌린다.
- **trace 매크로가 빈 줄이라고 단정**: SystemView나 Tracealyzer를 붙인 프로젝트에서는 `traceTASK_SWITCHED_IN`이 실제 코드(ITM 쓰기)다. 회사 프로젝트의 `FreeRTOSConfig.h`부터 확인한다.

---

## 4. 단계 2 · 3: FreeRTOS 투어 — 다섯 경로를 gdb로 밟는다

### 4.1 예제 6 — 다섯 경로를 한 번씩 밟는 앱

무엇을 확인하는 코드인지: 태스크 생성(경로 1), 꽉 찬 큐에 블로킹 send(경로 2), TIMER0 ISR의 `xQueueSendFromISR`(경로 3), tick(경로 4), PendSV(경로 5)를 각각 최소 한 번 지나가는 앱. 태스크는 셋: `producer`(prio 2), `consumer`(prio 1), `waiter`(prio 3).

```c
/* C12 예제: FreeRTOS 다섯 경로(생성 · 블로킹 send · FromISR · tick · PendSV)를 한 번씩 밟는 앱 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"

static QueueHandle_t q;      /* 길이 1: 두 번째 send가 블록된다 */
static QueueHandle_t qisr;   /* TIMER0 ISR → 태스크 */
static volatile uint32_t isr_count;

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t woken = pdFALSE;
    uint32_t v = ++isr_count;
    xQueueSendFromISR(qisr, &v, &woken);       /* 경로 3 */
    portYIELD_FROM_ISR(woken);
}

static void producer(void *arg)                 /* prio 2 */
{
    (void)arg;
    for (uint32_t i = 0; i < 3; i++) {
        xQueueSend(q, &i, portMAX_DELAY);       /* 경로 2: i=1에서 블록 */
        board_printf_raw("P sent %u\n", (unsigned)i);
    }
    vTaskDelete(NULL);
}

static void consumer(void *arg)                 /* prio 1 */
{
    (void)arg;
    uint32_t v;
    for (int i = 0; i < 3; i++) {
        xQueueReceive(q, &v, portMAX_DELAY);
        board_printf_raw("C got %u\n", (unsigned)v);
    }
    NVIC_IPR(8) = 0x40;                         /* 0x20(MAX_SYSCALL)보다 낮은 우선순위 */
    NVIC_ISER0 = 1u << 8;
    TIMER_RELOAD(TIMER0_BASE) = BOARD_CPU_HZ / 500;   /* 2 ms */
    TIMER_CTRL(TIMER0_BASE) = 0x9;              /* EN + IRQ EN */
    for (;;) vTaskDelay(pdMS_TO_TICKS(50));     /* 경로 4: tick이 깨운다 */
}

static void waiter(void *arg)                   /* prio 3 */
{
    (void)arg;
    uint32_t v;
    for (int i = 0; i < 2; i++) {
        xQueueReceive(qisr, &v, portMAX_DELAY);
        board_printf_raw("W got isr #%u at tick %u\n", (unsigned)v, (unsigned)xTaskGetTickCount());
    }
    TIMER_CTRL(TIMER0_BASE) = 0;
    board_printf_raw("done\n");
    board_exit(0);
}

int main(void)
{
    q = xQueueCreate(1, sizeof(uint32_t));
    qisr = xQueueCreate(4, sizeof(uint32_t));
    xTaskCreate(producer, "prod", 256, NULL, 2, NULL);   /* 경로 1 */
    xTaskCreate(consumer, "cons", 256, NULL, 1, NULL);
    xTaskCreate(waiter, "wait", 256, NULL, 3, NULL);
    vTaskStartScheduler();                       /* 경로 5: 첫 PendSV부터 */
    return 1;
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C12/fr_tour/main.c OUT=/var/tmp/rtos/C12/fr_tour
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C12/fr_tour/app.elf
```

```text
   text	   data	    bss	    dec	    hex	filename
  13300	    100	 135416	 148816	  24550	/var/tmp/rtos/C12/fr_tour/app.elf
P sent 0
P sent 1
C got 0
P sent 2
C got 1
C got 2
W got isr #1 at tick 2
W got isr #2 at tick 4
done
[board_exit] code=0
```

출력에서 볼 것: `P sent 1`이 `C got 0`보다 **먼저** 나온다. 소스를 따라가면 이유가 보인다. producer(prio 2)가 0을 넣고, 1을 넣으려다 큐가 꽉 차서 블록 → consumer(prio 1)가 돌아 `xQueueReceive`로 0을 꺼내는 순간, 그 안의 `xTaskRemoveFromEventList`가 producer를 깨우고 "더 높은 우선순위가 깼다"며 **receive 함수 안에서 바로 양보**한다 → producer가 먼저 돌아 1을 넣고 `P sent 1`을 찍고 2를 넣으려다 다시 블록 → 그제야 consumer가 `C got 0`을 찍는다. 출력 순서 하나가 "깨우기 = 즉시 선점"이라는 커널 규칙의 증거다. ISR 두 번은 2 ms 간격(tick 2, 4)이다.

### 4.2 예제 7 — gdb로 경로 다섯 개 찍기

무엇을 확인하는 코드인지: QEMU를 `-S`(멈춘 채 시작)와 **전용 gdb 포트**(`-gdb tcp::31212` — 같은 VM에서 다른 작성자가 기본 포트 1234를 쓸 수 있어서 피했다)로 띄우고, gdb 배치 스크립트로 경로마다 중단점을 걸어 backtrace를 찍는다.

```sh
#!/bin/bash
# rungdb.sh <elf> <gdbscript> <uartlog>  — QEMU를 -S로 멈춰 띄우고 gdb 배치 실행 (포트 31212)
E=$1; S=$2; U=$3
timeout 30 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
  -serial file:$U -semihosting-config enable=on,target=native \
  $QX -kernel $E -gdb tcp::31212 -S &
QP=$!
sleep 1
timeout 25 gdb -q -batch -x $S $E 2>&1
kill $QP 2>/dev/null; wait $QP 2>/dev/null
```

gdb 스크립트(`fr_tour.gdb`, 핵심만 — 전체 75줄은 같은 패턴의 반복이다):

```text
set pagination off
set confirm off
set print frame-arguments none
target remote :31212
define hdr
  printf "==== [%s] IPSR=%d tick=%u cur=%s\n", $arg0, $xpsr & 0x1ff, xTickCount, pxCurrentTCB ? pxCurrentTCB->pcTaskName : "(none)"
end
tbreak pxPortInitialiseStack
commands
  hdr "1 create"
  bt
  continue
end
tbreak vTaskPlaceOnEventList if pxCurrentTCB->uxPriority == 2
commands
  hdr "2 block"
  bt
  set $blocked = 1
  continue
end
tbreak queue.c:1142
...
tbreak vTaskSwitchContext if $blocked == 1 && pxCurrentTCB->uxPriority == 2
commands
  hdr "2c PendSV: switch-out"
  bt
  tbreak *($lr & ~1)
  commands
    printf "  after vTaskSwitchContext: pxCurrentTCB=%s\n", pxCurrentTCB->pcTaskName
    continue
  end
  continue
end
tbreak xTaskRemoveFromEventList if ($xpsr & 0x1ff) != 0
...
tbreak main.c:17
commands
  hdr "3b portYIELD_FROM_ISR"
  printf "  woken=%d  xYieldPendings[0]=%d  ICSR.PENDSVSET(before)=%d\n", woken, xYieldPendings[0], (*(unsigned*)0xE000ED04 >> 28) & 1
  continue
end
tbreak xTaskIncrementTick if xTickCount == 8
...
continue
```

몇 가지 기술: `$xpsr & 0x1ff`는 IPSR(지금 처리 중인 예외 번호 — 0 = Thread 모드, 14 = PendSV, 15 = SysTick, 16+8 = 24 = IRQ8 TIMER0)이다. `tbreak *($lr & ~1)`은 "이 함수가 돌아갈 주소"에 일회용 중단점을 걸어 **함수가 끝난 직후 상태**를 찍는 기법이다(`finish`는 commands 안에서 다루기 까다롭다). 조건식에 **inferior 함수 호출을 쓰지 말 것** — 처음에 `uxQueueMessagesWaiting(q)`를 조건에 넣었더니 타깃이 비정상 종료되고 gdb가 Segmentation fault로 죽었다.

빌드는 예제 6과 같은 소스를 `EXTRA_LDFLAGS=-Wl,--no-gc-sections`로 다시 링크한 것이다(이유는 §4.7 예제 8).

```sh
make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C12/fr_tour/main.c \
     OUT=/var/tmp/rtos/C12/fr_tour_g EXTRA_LDFLAGS=-Wl,--no-gc-sections
QX="" /Users/donh/.rtos_scratch/C12/rungdb.sh /var/tmp/rtos/C12/fr_tour_g/app.elf /Users/donh/.rtos_scratch/C12/fr_tour.gdb /var/tmp/rtos/C12/fr_uart.txt
```

출력은 경로별로 나눠 아래 절에 싣는다("Temporary breakpoint N at ..." 설정 줄은 뺐다). 세 가지 공통 모양을 먼저 익혀 두자.

- `#N  0x... in 함수 (...) at 파일:줄` — 일반 호출 프레임. 줄 번호는 **호출한 줄**이다.
- `#N  함수 (...) at 파일:줄`(주소 없이) — **인라인 프레임**. 바로 위 프레임과 같은 기계어 주소다.
- `<signal handler called>` — gdb가 Cortex-M의 예외 진입(하드웨어가 쌓은 8워드 프레임, B2)을 알아보고 그 아래에 **끼어들기 직전의 태스크 문맥**을 이어 붙인 것이다.

### 4.3 경로 1 — xTaskCreate → prvCreateTask → prvInitialiseNewTask → pxPortInitialiseStack → prvAddNewTaskToReadyList

```text
==== [1 create] IPSR=0 tick=0 cur=(none)
#0  pxPortInitialiseStack (pxTopOfStack=..., pxCode=..., pvParameters=...) at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:188
#1  0x0000088e in prvInitialiseNewTask (pxTaskCode=..., pcName=..., uxStackDepth=..., pvParameters=..., uxPriority=..., pxCreatedTask=..., pxNewTCB=..., xRegions=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1996
#2  0x00000cc2 in prvCreateTask (pxTaskCode=..., pcName=..., uxStackDepth=..., pvParameters=..., uxPriority=..., pxCreatedTask=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1722
#3  xTaskCreate (pxTaskCode=..., pcName=..., uxStackDepth=..., pvParameters=..., uxPriority=..., pxCreatedTask=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1741
#4  0x00000228 in main () at /Users/donh/.rtos_scratch/C12/fr_tour/main.c:62
```

`prvAddNewTaskToReadyList`의 backtrace는 앞선 탐색 실행(같은 앱, `--gc-sections` 빌드)에서 찍었다:

```text
#0  prvAddNewTaskToReadyList (pxNewTCB=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:2044
#1  0x00000b50 in xTaskCreate (pxTaskCode=..., pcName=..., uxStackDepth=..., pvParameters=..., uxPriority=..., pxCreatedTask=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1752
#2  0x00000228 in main () at /Users/donh/.rtos_scratch/C12/fr_tour/main.c:62
```

읽는 법: `#2 prvCreateTask`와 `#3 xTaskCreate`는 주소가 하나(`0x00000cc2`)뿐이다 — `prvCreateTask`(static)가 `xTaskCreate` 안에 인라인됐다. `xTaskCreate`는 두 단계다. 1741줄에서 TCB와 스택을 할당 · 초기화하고, 1752줄에서 ready 리스트에 넣는다.

소스 발췌 — `xTaskCreate`(tasks.c:1729~):

```c
 1741:         pxNewTCB = prvCreateTask( pxTaskCode, pcName, uxStackDepth, pvParameters, uxPriority, pxCreatedTask );
 1743:         if( pxNewTCB != NULL )
 1744:         {
 1752:             prvAddNewTaskToReadyList( pxNewTCB );
 1753:             xReturn = pdPASS;
 1754:         }
 1755:         else
 1756:         {
 1757:             xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
 1758:         }
```

`pxPortInitialiseStack`(port.c:182~) — 새 태스크 스택에 **가짜 예외 프레임**을 쌓는다. 첫 스위치 때 PendSV가 "이 태스크가 예외로 빠져나갔다가 돌아오는 중"이라고 믿고 복원하게 만드는 것이다(깊은 설명은 C3):

```c
  188:     pxTopOfStack--;
  189:     *pxTopOfStack = portINITIAL_XPSR;                                   /* xPSR: Thumb 비트 */
  190:     pxTopOfStack--;
  191:     *pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK; /* PC = 태스크 함수 */
  192:     pxTopOfStack--;
  193:     *pxTopOfStack = ( StackType_t ) portTASK_RETURN_ADDRESS;            /* LR = prvTaskExitError */
  194:     pxTopOfStack -= 5;                                                  /* R12, R3, R2, R1 */
  195:     *pxTopOfStack = ( StackType_t ) pvParameters;                       /* R0 = 인자 */
  196:     pxTopOfStack -= 8;                                                  /* R11..R4 */
  198:     return pxTopOfStack;
```

(오른쪽 주석은 이해를 돕기 위해 이 노트가 붙였다.) 193줄의 LR = `prvTaskExitError`가 §4.7에서 gdb를 헷갈리게 하는 주인공이다.

`prvAddNewTaskToReadyList`(tasks.c:2040~, 단일 코어판) 핵심:

```c
 2044:         taskENTER_CRITICAL();
 2046:             uxCurrentNumberOfTasks = ( UBaseType_t ) ( uxCurrentNumberOfTasks + 1U );
 2048:             if( pxCurrentTCB == NULL )
 2052:                 pxCurrentTCB = pxNewTCB;          /* 첫 태스크면 바로 current로 */
 2059:                     prvInitialiseTaskLists();      /* 첫 태스크면 리스트 전부 초기화 */
 2071:                 if( xSchedulerRunning == pdFALSE )
 2073:                     if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority )
 2075:                         pxCurrentTCB = pxNewTCB;  /* 시작 전엔 가장 높은 것을 current로 */
 2098:             prvAddTaskToReadyList( pxNewTCB );
 2102:         taskEXIT_CRITICAL();
 2104:         if( xSchedulerRunning != pdFALSE )
 2108:             taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxNewTCB );   /* 실행 중 생성이면 선점 검사 */
```

읽을 거리: 커널 리스트는 **첫 `xTaskCreate` 때** 초기화된다(2059). 스케줄러 시작 전에는 "가장 높은 우선순위로 만든 태스크"를 `pxCurrentTCB`로 미리 정해 둔다(2075). 그래서 `vTaskStartScheduler`의 첫 태스크는 스위치 결정 없이 바로 그 TCB에서 시작한다(C3).

### 4.4 경로 2 — xQueueGenericSend의 블로킹 경로 → vTaskPlaceOnEventList → taskYIELD_WITHIN_API

```text
==== [2 block] IPSR=0 tick=4 cur=prod
#0  vTaskPlaceOnEventList (pxEventList=..., xTicksToWait=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5407
#1  0x00003370 in xQueueGenericSend (xQueue=..., pvItemToQueue=..., xTicksToWait=..., xCopyPosition=...) at /var/rtoslab/src/FreeRTOS-Kernel/queue.c:1126
#2  0x00000178 in producer (arg=...) at /Users/donh/.rtos_scratch/C12/fr_tour/main.c:24
#3  0x00005c10 in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
Backtrace stopped: previous frame identical to this frame (corrupt stack?)

==== [2b taskYIELD_WITHIN_API] IPSR=0 tick=5 cur=prod
#0  xQueueGenericSend (xQueue=..., pvItemToQueue=..., xTicksToWait=..., xCopyPosition=...) at /var/rtoslab/src/FreeRTOS-Kernel/queue.c:1142
#1  0x00000178 in producer (arg=...) at /Users/donh/.rtos_scratch/C12/fr_tour/main.c:24
#2  0x00005c10 in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
Backtrace stopped: previous frame identical to this frame (corrupt stack?)

==== [2c PendSV: switch-out] IPSR=14 tick=6 cur=prod
#0  vTaskSwitchContext () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5219
#1  0x00005bec in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
  after vTaskSwitchContext: pxCurrentTCB=cons
```

읽는 법: 맨 아래 `PendSV_Handler`(0x5c10)는 진짜 호출자가 아니다. 태스크 스택 맨 밑의 가짜 LR(`prvTaskExitError`, 0x5c10)을 gdb가 "주소 − 1"로 찾다 보니 바로 앞 함수인 PendSV_Handler로 보인 것이다(§4.7). 실제 경로는 `producer → xQueueGenericSend(1126) → vTaskPlaceOnEventList`, 그다음 같은 함수의 1142줄에서 양보, 그다음 PendSV(IPSR=14) 안에서 `vTaskSwitchContext`가 `prod`를 `cons`로 바꾼다. tick 숫자(4, 5, 6)가 계속 오르는 것은 gdb가 멈출 때마다 실제 시간이 흐르기 때문이다(icount 없이 돌렸다). 순서만 본다.

`xQueueGenericSend`(queue.c:949~1164)의 뼈대 — 블로킹 경로만 남긴 발췌:

```c
  969:     for( ; ; )
  970:     {
  971:         taskENTER_CRITICAL();
  977:             if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) )
  978:             {   /* 자리가 있으면: 복사 → 받는 쪽 대기자를 깨우고 → return pdPASS (985~1081) */
 1081:                 return pdPASS;
 1082:             }
 1083:             else
 1084:             {
 1085:                 if( xTicksToWait == ( TickType_t ) 0 )
 1096:                     return errQUEUE_FULL;                 /* 기다리지 않는 호출 */
 1098:                 else if( xEntryTimeSet == pdFALSE )
 1102:                     vTaskInternalSetTimeOutState( &xTimeOut );   /* 타임아웃 기준 시각 기록 */
 1110:             }
 1112:         taskEXIT_CRITICAL();
 1117:         vTaskSuspendAll();                 /* 스케줄러 잠금: 태스크 선점은 막고 ISR은 허용 */
 1118:         prvLockQueue( pxQueue );           /* ISR이 이 큐의 이벤트 리스트를 건드리지 못하게 */
 1121:         if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
 1122:         {
 1123:             if( prvIsQueueFull( pxQueue ) != pdFALSE )
 1124:             {
 1126:                 vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToSend ), xTicksToWait );
 1133:                 prvUnlockQueue( pxQueue );
 1140:                 if( xTaskResumeAll() == pdFALSE )
 1141:                 {
 1142:                     taskYIELD_WITHIN_API();
 1143:                 }
 1144:             }
 1145:             else
 1146:             {   /* 그사이 자리가 났다 → 다시 루프 */
 1148:                 prvUnlockQueue( pxQueue );
 1149:                 ( void ) xTaskResumeAll();
 1150:             }
 1151:         }
 1152:         else
 1153:         {   /* 타임아웃 */
 1155:             prvUnlockQueue( pxQueue );
 1156:             ( void ) xTaskResumeAll();
 1161:             return errQUEUE_FULL;
 1162:         }
 1163:     }
```

이 함수에서 배울 FreeRTOS의 관용구 세 가지:

1. **짧은 임계 구역 + 긴 스케줄러 잠금**: 큐 상태를 볼 때만 인터럽트를 막고(971~1112), 대기 리스트에 넣는 긴 작업은 `vTaskSuspendAll`(태스크 선점만 막음) 아래에서 한다. ISR 지연을 짧게 하려는 설계다(C7).
2. **큐 잠금(`prvLockQueue`, queue.c:258)**: 스케줄러를 잠근 동안 ISR이 이 큐에 넣으면, ISR은 이벤트 리스트를 건드리지 않고 `cTxLock` 카운터만 올린다. `prvUnlockQueue`(queue.c:2499)가 나중에 그 수만큼 대기자를 깨운다. 그래서 예제 9의 정적 그래프에서 FromISR가 `uxTaskGetNumberOfTasks`를 부른다(잠금 카운터 상한 검사).
3. **for 루프 재시도**: 깬 뒤 다시 처음부터 검사한다. 깨어난 사이에 더 높은 우선순위 태스크가 자리를 먼저 차지했을 수 있기 때문이다.

`vTaskPlaceOnEventList`(tasks.c:5402~)는 두 줄이 전부다:

```c
 5423:     vListInsert( pxEventList, &( pxCurrentTCB->xEventListItem ) );   /* 무엇을: 우선순위 순 정렬 삽입 */
 5425:     prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );          /* 언제까지: ready에서 빼서 delayed(또는 suspended)로 */
```

`taskYIELD_WITHIN_API()`는 task.h:3587에서 `portYIELD_WITHIN_API()`로, FreeRTOS.h:2724에서 `portYIELD`로, portmacro.h:85에서 "ICSR에 PENDSVSET 쓰기 + dsb + isb"로 펼쳐진다. **함수 호출이 아니라 PendSV 예외 요청**이다. 그래서 2c의 backtrace는 태스크 쪽(`producer`)과 이어지지 않고 `PendSV_Handler`에서 시작한다 — PendSV는 MSP(핸들러 스택) 위에서 돈다.

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">FreeRTOS 경로 1 · 2 — gdb backtrace를 그대로 옮긴 호출 그래프 (위 = 호출자)</text>
<text x="165" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 1: xTaskCreate</text>
<rect x="40" y="56" width="250" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="72" font-size="12" text-anchor="middle" font-weight="bold">main</text>
<text x="165" y="88" font-size="12" text-anchor="middle">main.c:62</text>
<rect x="40" y="112" width="250" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="128" font-size="12" text-anchor="middle" font-weight="bold">xTaskCreate</text>
<text x="165" y="144" font-size="12" text-anchor="middle">tasks.c:1741 / 1752</text>
<line x1="165" y1="96" x2="165" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="40" y="168" width="250" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="184" font-size="12" text-anchor="middle" font-weight="bold">prvCreateTask (inline)</text>
<text x="165" y="200" font-size="12" text-anchor="middle">tasks.c:1722</text>
<line x1="165" y1="152" x2="165" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="40" y="224" width="250" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="240" font-size="12" text-anchor="middle" font-weight="bold">prvInitialiseNewTask</text>
<text x="165" y="256" font-size="12" text-anchor="middle">tasks.c:1996</text>
<line x1="165" y1="208" x2="165" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="40" y="280" width="250" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="165" y="296" font-size="12" text-anchor="middle" font-weight="bold">pxPortInitialiseStack</text>
<text x="165" y="312" font-size="12" text-anchor="middle">port.c:188 가짜 예외 프레임</text>
<line x1="165" y1="264" x2="165" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="40" y="350" width="250" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="366" font-size="12" text-anchor="middle" font-weight="bold">prvAddNewTaskToReadyList</text>
<text x="165" y="382" font-size="12" text-anchor="middle">tasks.c:2044 → ready list 삽입</text>
<path d="M 40,132 C 8,180 8,300 38,368" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="14" y="410" font-size="12">두 번째 bt: xTaskCreate(1752) →</text>
<text x="14" y="428" font-size="12">  prvAddNewTaskToReadyList (할당 뒤 삽입)</text>
<text x="505" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 2: 꽉 찬 큐에 xQueueSend</text>
<rect x="380" y="56" width="250" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="505" y="72" font-size="12" text-anchor="middle" font-weight="bold">producer (prio 2)</text>
<text x="505" y="88" font-size="12" text-anchor="middle">main.c:24</text>
<rect x="380" y="112" width="250" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="505" y="128" font-size="12" text-anchor="middle" font-weight="bold">xQueueGenericSend</text>
<text x="505" y="144" font-size="12" text-anchor="middle">queue.c:1126 → 1142</text>
<line x1="505" y1="96" x2="505" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="380" y="168" width="250" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="505" y="184" font-size="12" text-anchor="middle" font-weight="bold">vTaskPlaceOnEventList</text>
<text x="505" y="200" font-size="12" text-anchor="middle">tasks.c:5407 이벤트 리스트로</text>
<line x1="505" y1="152" x2="505" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="380" y="224" width="250" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="505" y="240" font-size="12" text-anchor="middle" font-weight="bold">taskYIELD_WITHIN_API</text>
<text x="505" y="256" font-size="12" text-anchor="middle">queue.c:1142 = portYIELD</text>
<line x1="505" y1="208" x2="505" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="380" y="280" width="250" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="505" y="296" font-size="12" text-anchor="middle" font-weight="bold">PendSV_Handler</text>
<text x="505" y="312" font-size="12" text-anchor="middle">port.c:459 (IPSR=14)</text>
<line x1="505" y1="264" x2="505" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="380" y="336" width="250" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="505" y="352" font-size="12" text-anchor="middle" font-weight="bold">vTaskSwitchContext</text>
<text x="505" y="368" font-size="12" text-anchor="middle">tasks.c:5219 → cur: prod → cons</text>
<line x1="505" y1="320" x2="505" y2="334" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="380" y="410" font-size="12">3번째 상자는 queue.c:1126에서, 4번째는</text>
<text x="380" y="428" font-size="12">xTaskResumeAll()이 pdFALSE일 때 1142에서</text>
<rect x="10" y="446" width="660" height="104" rx="5" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="20" y="468" font-size="13" font-weight="bold">읽는 법</text>
<text x="20" y="488" font-size="12">· 실선 상자 = 실제 bt에 찍힌 프레임. (inline) = gdb가 인라인 프레임으로 보여 준 함수 (주소가 같다)</text>
<text x="20" y="508" font-size="12">· 경로 2의 PendSV 아래 두 상자는 다른 정지점의 bt — 핸들러 스택(MSP)이라 태스크 쪽과 이어지지 않는다</text>
<text x="20" y="528" font-size="12">· 색: 파랑 = 태스크 문맥, 초록 = 커널 리스트 변경, 주황 = 스위치 요청, 빨강 = 포트(어셈블리)</text>
</svg>
```
그림 4 — 경로 1 · 2의 호출 그래프. 상자 하나 = backtrace 프레임 하나(파일:줄은 bt 그대로). 경로 2는 세 개의 정지점(2 · 2b · 2c)을 이어 붙였다.

### 4.5 경로 3 — xQueueSendFromISR → xTaskRemoveFromEventList → xYieldPendings

```text
==== [3 fromISR] IPSR=24 tick=10 cur=IDLE
#0  xTaskRemoveFromEventList (pxEventList=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5523
#1  0x00003608 in xQueueGenericSendFromISR (xQueue=..., pvItemToQueue=..., pxHigherPriorityTaskWoken=..., xCopyPosition=...) at /var/rtoslab/src/FreeRTOS-Kernel/queue.c:1259
#2  0x000001c6 in TIMER0_IRQHandler () at /Users/donh/.rtos_scratch/C12/fr_tour/main.c:16
#3  <signal handler called>
#4  prvIdleTask (pvParameters=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5960
#5  0x00005c10 in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
  xYieldPendings[0] before = 0

==== [3b portYIELD_FROM_ISR] IPSR=24 tick=10 cur=IDLE
  woken=1  xYieldPendings[0]=1  ICSR.PENDSVSET(before)=0

==== [3c PendSV after ISR] IPSR=14 tick=10 cur=IDLE
#0  vTaskSwitchContext () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5219
#1  0x00005bec in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
  after vTaskSwitchContext: pxCurrentTCB=wait
```

읽는 법: IPSR=24(IRQ8)에서, `<signal handler called>` 아래가 끼어들기 직전의 태스크(IDLE)다. `xTaskRemoveFromEventList`에 들어갈 때 `xYieldPendings[0]`은 0이었고, ISR의 17줄(`portYIELD_FROM_ISR`)에 왔을 때는 `woken=1`, `xYieldPendings[0]=1`이다. 그런데 **PENDSVSET은 아직 0**이다. 즉 `xQueueSendFromISR`는 스위치를 요청하지 않는다. 요청은 ISR 끝의 `portYIELD_FROM_ISR(woken)`이 한다. 이게 E1의 "FromISR API + `xHigherPriorityTaskWoken` + `portYIELD_FROM_ISR`" 규칙이 왜 세 조각인지에 대한 소스 수준 답이다. 그 뒤 PendSV(IPSR=14)가 꼬리 물기로 실행되어 IDLE → wait로 바꾼다.

`xQueueGenericSendFromISR`(queue.c:1167~)의 핵심 — 블로킹 경로가 아예 없다:

```c
 1195:     portASSERT_IF_INTERRUPT_PRIORITY_INVALID();      /* 0x20보다 높은 우선순위 ISR에서 부르면 assert */
 1205:     uxSavedInterruptStatus = ( UBaseType_t ) taskENTER_CRITICAL_FROM_ISR();
 1207:         if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) )
 1208:         {
 1209:             const int8_t cTxLock = pxQueue->cTxLock;
 1219:             ( void ) prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );
 1223:             if( cTxLock == queueUNLOCKED )
 1224:             {   /* 큐가 잠겨 있지 않을 때만 직접 깨운다 */
 1257:                         if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE )
 1259:                             if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE )
 1265:                                     *pxHigherPriorityTaskWoken = pdTRUE;
```

`xTaskRemoveFromEventList`의 전체는 §3.2 예제 4에서 이미 읽었다. 요점: 스케줄러가 잠겨 있지 않으면(5527) ready 리스트로, 잠겨 있으면 `xPendingReadyList`로(5550). 깬 태스크가 지금 태스크보다 높으면 `xYieldPendings[0] = pdTRUE`(5564)를 남기고 pdTRUE를 돌려준다.

### 4.6 경로 4 · 5 — tick과 PendSV

```text
==== [4 tick] IPSR=15 tick=8 cur=IDLE
#0  xTaskIncrementTick () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:4848
#1  0x00005d3e in SysTick_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:501
#2  <signal handler called>
#3  prvIdleTask (pvParameters=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5910
#4  0x00005c10 in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

`xPortSysTickHandler`(port.c:491~) — 소스 이름과 ELF 이름이 다른 함수(예제 5):

```c
  497:     portDISABLE_INTERRUPTS();             /* BASEPRI = 0x20 */
  501:         if( xTaskIncrementTick() != pdFALSE )
  502:         {
  507:             portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;   /* 스위치는 PendSV에 맡긴다 */
  508:         }
  514:     portENABLE_INTERRUPTS();
```

`xTaskIncrementTick`(tasks.c:4831~)의 뼈대:

```c
 4848:     if( uxSchedulerSuspended == ( UBaseType_t ) 0U )
 4849:     {
 4852:         const TickType_t xConstTickCount = xTickCount + ( TickType_t ) 1;
 4856:         xTickCount = xConstTickCount;
 4858:         if( xConstTickCount == ( TickType_t ) 0U )
 4860:             taskSWITCH_DELAYED_LISTS();                       /* 32비트 tick 오버플로: 두 리스트 교대 */
 4871:         if( xConstTickCount >= xNextTaskUnblockTime )         /* 깨울 게 없으면 여기서 끝 — O(1) */
 4873:             for( ; ; )
 4894:                     pxTCB = listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList );
 4897:                     if( xConstTickCount < xItemValue )
 4904:                         xNextTaskUnblockTime = xItemValue; break;  /* 아직 시각 안 된 첫 항목에서 멈춤 */
 4913:                     listREMOVE_ITEM( &( pxTCB->xStateListItem ) );
 4917:                     if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
 4919:                         listREMOVE_ITEM( &( pxTCB->xEventListItem ) );   /* 타임아웃: 이벤트 대기도 해제 */
 4928:                     prvAddTaskToReadyList( pxTCB );
 4944:                     if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
 4946:                         xSwitchRequired = pdTRUE;
 4971:         if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > 1U )
 4973:             xSwitchRequired = pdTRUE;                         /* 같은 우선순위 time slicing */
```

(4904줄은 원본에서 두 줄인 것을 한 줄로 붙였다.) 핫 패스의 핵심은 4871줄이다. **`xNextTaskUnblockTime`이라는 캐시 하나 덕분에 대부분의 tick은 비교 한 번으로 끝난다.** 깨울 태스크가 있을 때만 정렬된 지연 리스트 앞에서부터 꺼낸다. 이 구조가 C5의 주제다. 그리고 4917~4919줄이 "타임아웃으로 깬 태스크는 이벤트 리스트에서도 빠진다" — `xQueueSend(..., 100)`이 100 tick 뒤 `errQUEUE_FULL`을 돌려주는 메커니즘이다.

PendSV → `vTaskSwitchContext`(tasks.c:5215~) — 경로 2c와 3c에서 이미 찍었다. 단일 코어판의 핵심:

```c
 5219:         if( uxSchedulerSuspended != ( UBaseType_t ) 0U )
 5223:             xYieldPendings[ 0 ] = pdTRUE;           /* 잠금 중이면 결정을 미룬다 */
 5227:             xYieldPendings[ 0 ] = pdFALSE;
 5235:                     ulTotalRunTime[ 0 ] = portGET_RUN_TIME_COUNTER_VALUE();   /* 런타임 통계 */
 5247:                     pxCurrentTCB->ulRunTimeCounter += ( ulTotalRunTime[ 0 ] - ulTaskSwitchedInTime[ 0 ] );
 5259:             taskCHECK_FOR_STACK_OVERFLOW();           /* configCHECK_FOR_STACK_OVERFLOW=2: 스택 끝 패턴 검사 */
 5273:             taskSELECT_HIGHEST_PRIORITY_TASK();       /* CLZ(uxTopReadyPriority) → 그 리스트의 다음 TCB */
```

`taskSELECT_HIGHEST_PRIORITY_TASK`(tasks.c:238, 포트 최적화판)는 `portGET_HIGHEST_PRIORITY`(portmacro.h:168 — `31 - CLZ(uxTopReadyPriority)`)로 가장 높은 비트를 찾고, `listGET_OWNER_OF_NEXT_ENTRY`로 그 우선순위 리스트를 한 칸 돌려 다음 TCB를 `pxCurrentTCB`에 넣는다. 같은 우선순위 round-robin이 "리스트 인덱스를 한 칸 돌리기"로 구현된 것이다. PendSV 어셈블리 19개 명령은 C2가 한 줄씩 해부한다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">FreeRTOS 경로 3 · 4 · 5 — ISR · tick · PendSV (모두 Handler 모드에서 찍은 bt)</text>
<text x="113" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 3: FromISR</text>
<rect x="8" y="56" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="113" y="72" font-size="12" text-anchor="middle" font-weight="bold">TIMER0_IRQHandler</text>
<text x="113" y="88" font-size="12" text-anchor="middle">main.c:16 (IPSR=24)</text>
<rect x="8" y="112" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="113" y="128" font-size="12" text-anchor="middle" font-weight="bold">xQueueGenericSendFromISR</text>
<text x="113" y="144" font-size="12" text-anchor="middle">queue.c:1259</text>
<line x1="113" y1="96" x2="113" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="8" y="168" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="113" y="184" font-size="12" text-anchor="middle" font-weight="bold">xTaskRemoveFromEventList</text>
<text x="113" y="200" font-size="12" text-anchor="middle">tasks.c:5523</text>
<line x1="113" y1="152" x2="113" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="8" y="224" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="113" y="240" font-size="12" text-anchor="middle" font-weight="bold">xYieldPendings[0]=1</text>
<text x="113" y="256" font-size="12" text-anchor="middle">woken=1 (main.c:17)</text>
<line x1="113" y1="208" x2="113" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="8" y="280" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="113" y="296" font-size="12" text-anchor="middle" font-weight="bold">portYIELD_FROM_ISR</text>
<text x="113" y="312" font-size="12" text-anchor="middle">ICSR.PENDSVSET=1</text>
<line x1="113" y1="264" x2="113" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<text x="340" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 4: tick</text>
<rect x="235" y="56" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="72" font-size="12" text-anchor="middle" font-weight="bold">SysTick_Handler</text>
<text x="340" y="88" font-size="12" text-anchor="middle">port.c:501 (IPSR=15)</text>
<rect x="235" y="112" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="128" font-size="12" text-anchor="middle" font-weight="bold">xTaskIncrementTick</text>
<text x="340" y="144" font-size="12" text-anchor="middle">tasks.c:4848</text>
<line x1="340" y1="96" x2="340" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="235" y="168" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="184" font-size="12" text-anchor="middle" font-weight="bold">지연 리스트 → ready</text>
<text x="340" y="200" font-size="12" text-anchor="middle">tasks.c:4913–4928</text>
<line x1="340" y1="152" x2="340" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="235" y="224" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="240" font-size="12" text-anchor="middle" font-weight="bold">pdTRUE 이면</text>
<text x="340" y="256" font-size="12" text-anchor="middle">port.c:507 PENDSVSET</text>
<line x1="340" y1="208" x2="340" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<text x="567" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 5: PendSV</text>
<rect x="462" y="56" width="210" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="567" y="72" font-size="12" text-anchor="middle" font-weight="bold">PendSV_Handler</text>
<text x="567" y="88" font-size="12" text-anchor="middle">port.c:459 (IPSR=14)</text>
<rect x="462" y="112" width="210" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="567" y="128" font-size="12" text-anchor="middle" font-weight="bold">R4–R11 → 태스크 스택</text>
<text x="567" y="144" font-size="12" text-anchor="middle">stmdb r0!, {r4-r11}</text>
<line x1="567" y1="96" x2="567" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="462" y="168" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="567" y="184" font-size="12" text-anchor="middle" font-weight="bold">vTaskSwitchContext</text>
<text x="567" y="200" font-size="12" text-anchor="middle">tasks.c:5219</text>
<line x1="567" y1="152" x2="567" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="462" y="224" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="567" y="240" font-size="12" text-anchor="middle" font-weight="bold">SELECT_HIGHEST</text>
<text x="567" y="256" font-size="12" text-anchor="middle">tasks.c:5273 (CLZ)</text>
<line x1="567" y1="208" x2="567" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="462" y="280" width="210" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="567" y="296" font-size="12" text-anchor="middle" font-weight="bold">새 TCB에서 복원</text>
<text x="567" y="312" font-size="12" text-anchor="middle">ldmia · msr psp · bx lr</text>
<line x1="567" y1="264" x2="567" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<path d="M 218,332 C 300,360 380,380 460,96" fill="none" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4,3" marker-end="url(#ah5)"/>
<path d="M 445,250 C 452,200 452,140 460,96" fill="none" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4,3" marker-end="url(#ah5)"/>
<text x="10" y="370" font-size="12">점선 = PENDSVSET 뒤 하드웨어가 PendSV를 꼬리 물기(tail-chain)로 실행</text>
<text x="10" y="388" font-size="12">실측(bt 3c): 스위치 전 pxCurrentTCB=IDLE → vTaskSwitchContext 뒤 = wait</text>
<rect x="10" y="404" width="660" height="56" rx="5" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="20" y="426" font-size="12">핵심: FreeRTOS에서 "누가 다음인가"는 PendSV 안의 vTaskSwitchContext가 정한다.</text>
<text x="20" y="446" font-size="12">ISR과 tick은 리스트만 고치고 PENDSVSET만 켠다 → ISR은 짧고, 결정은 가장 낮은 우선순위로 미뤄진다</text>
</svg>
```
그림 5 — 경로 3 · 4 · 5. 세 경로 모두 Handler 모드(IPSR ≠ 0)에서 찍었다. ISR과 SysTick은 리스트만 고치고 PENDSVSET을 켠다. 결정(`vTaskSwitchContext`)은 PendSV 안에서만 일어난다.

### 4.7 예제 8 — gdb backtrace를 믿기 전에 확인할 것 네 가지

무엇을 확인하는 코드인지: 템플릿 기본 빌드(`--gc-sections` 켬, 예제 6의 `fr_tour/app.elf`)에서 같은 중단점을 걸었을 때 gdb가 보여 주는 이름을, `addr2line`과 `nm`, DWARF 덤프로 검증한다.

```sh
E=/var/tmp/rtos/C12/fr_tour/app.elf
QX="" /Users/donh/.rtos_scratch/C12/rungdb.sh $E /Users/donh/.rtos_scratch/C12/misname.gdb /dev/null 2>&1 | grep "^#"
arm-none-eabi-addr2line -f -e $E 0xd4 0x2320 | paste - -
arm-none-eabi-nm -n $E | grep -E " (waiter|consumer|producer|PendSV_Handler|prvTaskExitError)$"
```

(`misname.gdb` = `tbreak vTaskPlaceOnEventList` → `bt` → `kill`.)

```text
#0  vTaskPlaceOnEventList (pxEventList=..., xTicksToWait=...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5407
#1  0x00001aa2 in xQueueReceive (xQueue=..., pvBuffer=..., xTicksToWait=...) at /var/rtoslab/src/FreeRTOS-Kernel/queue.c:1616
#2  0x000000d4 in xQueuePeek (xQueue=..., pvBuffer=..., xTicksToWait=...) at /var/rtoslab/src/FreeRTOS-Kernel/queue.c:2001
#3  0x00002320 in PendSV_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:459
waiter	/Users/donh/.rtos_scratch/C12/fr_tour/main.c:51
prvTaskExitError	/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:204
000000c0 t waiter
0000010c t consumer
00000160 t producer
000022dc T PendSV_Handler
00002320 t prvTaskExitError
```

그리고 DWARF에서 `xQueuePeek` 항목:

```sh
arm-none-eabi-objdump --dwarf=info $E | sed -n "<xQueuePeek 항목 앞뒤>p"
```

```text
    <8b51>   DW_AT_name        : (indirect string, offset: 0x1760d): xQueuePeek
    <8b55>   DW_AT_decl_file   : 1
    <8b55>   DW_AT_decl_line   : 1884
    <8b57>   DW_AT_decl_column : 12
    <8b58>   DW_AT_prototyped  : 1
    <8b58>   DW_AT_type        : <0x7354>
    <8b5c>   DW_AT_low_pc      : 0
    <8b60>   DW_AT_high_pc     : 0x158
```

출력에서 볼 것 — 네 가지 함정:

1. **`--gc-sections`가 남긴 유령 DWARF**: `#2 0x000000d4 in xQueuePeek`는 거짓말이다. `addr2line`은 0xd4를 `waiter`(main.c:51)라고 한다. 이 앱은 `xQueuePeek`를 쓰지 않으므로 링커가 코드를 버렸는데, 디버그 정보는 남아서 `low_pc = 0`, 길이 0x158로 **주소 0~0x158을 덮는다**. 하필 앱 함수들이 0xc0~0x1a0에 있어서 gdb가 이 유령을 집었다. 해결: gdb 실행용 빌드는 `-Wl,--no-gc-sections`로 링크했다(예제 7). 실제 제품에서 이 증상을 보면 "낮은 주소 프레임 이름이 이상하다 → addr2line · nm으로 대조"를 습관으로.
2. **"주소 − 1" 규칙과 가짜 LR**: 맨 아래 `0x00002320 in PendSV_Handler`. 0x2320은 `prvTaskExitError`의 첫 주소인데, gdb는 호출자 프레임을 찾을 때 리턴 주소에서 1을 빼서 찾는다(호출 명령은 리턴 주소 바로 앞에 있으니까). 0x231f는 PendSV_Handler의 리터럴 풀 끝이다. 실제로는 `pxPortInitialiseStack`이 심은 가짜 LR(port.c:193)이다. 즉 **"backtrace 맨 밑의 PendSV_Handler = 태스크 스택의 바닥"**으로 읽는다. 이어지는 `Backtrace stopped: previous frame identical to this frame`도 같은 이유로 무시해도 된다.
3. **인라인 프레임**: 주소 없는 프레임(예: 경로 1의 `#3 xTaskCreate`)은 같은 기계어 안에 인라인된 함수다. 중단점이 인라인 사본마다 걸리므로 "Breakpoint at ... (3 locations)"처럼 여러 위치가 나온다(`xTaskIncrementTick`이 3곳 — 본체, `.part.0`, `xTaskResumeAll` 안 인라인).
4. **gdb + icount = 타이밍 왜곡**: 처음에 `ICOUNT=1`로 gdb를 붙였더니 tick 8에서 `waiter`가 돌고 있고 ISR이 tick 13472에 찍히는 등 시간 흐름이 엉망이었다. 중단점마다 멈췄다 가는 동안 icount 가상 시계와 타이머 진행이 어긋난다. **호출 순서를 볼 때는 icount를 끄고, 시간을 잴 때는 gdb를 붙이지 않는다.** 이 노트의 모든 시간 측정은 gdb 없이 돌렸다.

### 4.8 예제 9 — 정적 콜 그래프: `gcc -fcallgraph-info`

무엇을 확인하는 코드인지: 실행하지 않고 컴파일러에게 `queue.c`의 호출 그래프를 받아, `xQueueGenericSend`와 `xQueueGenericSendFromISR`가 무엇을 부르는지 본다. ctags · cflow가 없을 때의 대안이다.

```sh
cd /var/tmp/rtos/C12/obj; K=/var/rtoslab/src/FreeRTOS-Kernel; T=/var/rtoslab/templates
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -fcallgraph-info -I$T/freertos-mps2 -I$T/common -I$K/include \
    -I$K/portable/GCC/ARM_CM3 -c $K/queue.c -o cg_queue.o
grep -c "edge:" cg_queue.ci
grep "^edge: { sourcename: \"xQueueGenericSend\"" cg_queue.ci | head -3
grep "sourcename: \"xQueueGenericSend\"" cg_queue.ci | sed "s/.*targetname: \"\([^\"]*\)\".*/\1/" | sort -u | tr "\n" " "; echo
grep "sourcename: \"xQueueGenericSendFromISR\"" cg_queue.ci | sed "s/.*targetname: \"\([^\"]*\)\".*/\1/" | sort -u | tr "\n" " "
```

```text
292
edge: { sourcename: "xQueueGenericSend" targetname: "board_printf_raw" label: "/var/rtoslab/src/FreeRTOS-Kernel/queue.c:960:5" }
edge: { sourcename: "xQueueGenericSend" targetname: "board_exit" label: "/var/rtoslab/src/FreeRTOS-Kernel/queue.c:960:5" }
edge: { sourcename: "xQueueGenericSend" targetname: "board_printf_raw" label: "/var/rtoslab/src/FreeRTOS-Kernel/queue.c:961:5" }
/var/rtoslab/src/FreeRTOS-Kernel/queue.c:prvCopyDataToQueue /var/rtoslab/src/FreeRTOS-Kernel/queue.c:prvNotifyQueueSetContainer /var/rtoslab/src/FreeRTOS-Kernel/queue.c:prvUnlockQueue board_exit board_printf_raw vPortEnterCritical vPortExitCritical vTaskInternalSetTimeOutState vTaskPlaceOnEventList vTaskSuspendAll xTaskCheckForTimeOut xTaskGetSchedulerState xTaskRemoveFromEventList xTaskResumeAll 
/var/rtoslab/src/FreeRTOS-Kernel/queue.c:prvCopyDataToQueue /var/rtoslab/src/FreeRTOS-Kernel/queue.c:prvNotifyQueueSetContainer board_exit board_printf_raw uxTaskGetNumberOfTasks vPortValidateInterruptPriority xTaskRemoveFromEventList 
```

출력에서 볼 것:

- 정적 그래프는 **모든 가능한 호출**을 보여 준다. `xQueueGenericSend`가 부르는 함수 14개 중 블로킹 경로는 6개(`vTaskSuspendAll`, `xTaskCheckForTimeOut`, `vTaskPlaceOnEventList`, `prvUnlockQueue`, `xTaskResumeAll`, `vTaskInternalSetTimeOutState`)다. 동적 그래프(gdb)는 **실제로 지나간 하나**만 보여 준다. 둘을 겹쳐 보면 "이번엔 안 지나간 가지"가 보인다.
- `board_printf_raw` · `board_exit`는 `configASSERT`가 펼쳐진 것이다(960 · 961줄). 매크로 속 호출도 컴파일러는 놓치지 않는다.
- 정적 그래프에 **안 나오는 것**: `portYIELD`(함수가 아니라 레지스터 쓰기), PendSV로의 "점프"(예외), 함수 포인터 호출. 그래서 경로 2의 "→ PendSV → vTaskSwitchContext" 연결은 gdb로만 확인된다.
- FromISR 쪽의 `uxTaskGetNumberOfTasks`는 `prvIncrementQueueTxLock`(queue.c:277, 매크로)이 큐 잠금 카운터의 상한을 태스크 수로 검사하기 때문이다 — §4.4의 큐 잠금 관용구가 여기서 다시 보인다.

---

## 5. Zephyr 투어 — 같은 다섯 경로, 다른 설계

### 5.1 예제 10 — Zephyr 판 투어 앱

무엇을 확인하는 코드인지: 스레드 생성, `k_sem_give`로 더 높은 우선순위 깨우기, `k_sleep`(timeout 경로), TIMER0 ISR의 `k_sem_give`(ISR 종료 → PendSV), tick announce를 한 번씩 지나간다. Zephyr는 숫자가 **작을수록** 높은 우선순위다(main = 5, hi = 2, idle = 15).

```c
/* C12 Zephyr 예제: 생성 · sem give(깨우기) · sleep(timeout) · ISR→PendSV · tick announce 경로를 한 번씩 */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>

#define T0_BASE   0x40000000u
#define T0_CTRL   (*(volatile uint32_t *)(T0_BASE + 0x0))
#define T0_RELOAD (*(volatile uint32_t *)(T0_BASE + 0x8))
#define T0_INTCLR (*(volatile uint32_t *)(T0_BASE + 0xC))
#define T0_IRQ    8

K_SEM_DEFINE(sem_t2t, 0, 1);     /* main → hi */
K_SEM_DEFINE(sem_isr, 0, 1);     /* TIMER0 ISR → hi */
K_THREAD_STACK_DEFINE(hi_stack, 1024);
static struct k_thread hi_thread;
static volatile uint32_t isr_count;

static void t0_isr(const void *arg)
{
	ARG_UNUSED(arg);
	T0_INTCLR = 1;
	isr_count++;
	k_sem_give(&sem_isr);            /* 경로 4: ISR exit → PendSV */
}

static void hi_entry(void *a, void *b, void *c)   /* prio 2 */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	k_sem_take(&sem_t2t, K_FOREVER);
	printk("hi: woke by main at %lld ms\n", k_uptime_get());
	k_sleep(K_MSEC(5));              /* 경로 3: timeout 경로 */
	printk("hi: sleep done at %lld ms\n", k_uptime_get());
	k_sem_take(&sem_isr, K_FOREVER);
	printk("hi: woke by ISR #%u at %lld ms\n", isr_count, k_uptime_get());
	T0_CTRL = 0;
	printk("done\n");
}

int main(void)                     /* prio 5 */
{
	k_thread_create(&hi_thread, hi_stack, K_THREAD_STACK_SIZEOF(hi_stack),
			hi_entry, NULL, NULL, NULL, 2, 0, K_NO_WAIT);   /* 경로 1 */
	k_sem_give(&sem_t2t);            /* 경로 2: give → 높은 우선순위 깨움 → 즉시 스위치 */
	printk("main: after give\n");
	k_msleep(10);
	IRQ_CONNECT(T0_IRQ, 2, t0_isr, NULL, 0);
	irq_enable(T0_IRQ);
	T0_RELOAD = 25000000u / 1000u * 3u;     /* 3 ms */
	T0_CTRL = 0x9;
	k_msleep(20);
	return 0;
}
```

`prj.conf`:

```text
CONFIG_PRINTK=y
CONFIG_MAIN_THREAD_PRIORITY=5
CONFIG_DEBUG_OPTIMIZATIONS=y
```

(`CONFIG_DEBUG_OPTIMIZATIONS=y`는 `-Og` — gdb에서 변수와 프레임이 잘 보이게 했다. 크기 측정(예제 13)은 이 줄을 뺀 `-Os` 빌드로 따로 했다.) `CMakeLists.txt`는 표준 3줄(`find_package(Zephyr)`, `project`, `target_sources(app PRIVATE src/main.c)`)이다.

```sh
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C12/z_tour /var/tmp/rtos/C12/z_tour
timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
    -kernel /var/tmp/rtos/C12/z_tour/zephyr/zephyr.elf
```

```text
           FLASH:       17116 B         4 MB      0.41%
             RAM:        7304 B         4 MB      0.17%
*** Booting Zephyr OS build v4.4.2 ***
hi: woke by main at 0 ms
main: after give
hi: sleep done at 20 ms
hi: woke by ISR #1 at 30 ms
done
qemu-system-arm: terminating on signal 15 from pid 2354810 (timeout)
```

출력에서 볼 것:

- `hi: woke by main`이 `main: after give`보다 먼저다. FreeRTOS 예제와 같은 규칙(깨우기 = 즉시 선점)이 `k_sem_give` 안에서 일어난다.
- **`k_sleep(K_MSEC(5))`가 20 ms 걸렸다.** 이 보드의 `.config`는 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100`(10 ms tick)이다. 소스를 읽으면 이유가 정확히 나온다: 5 ms는 1 tick으로 올림되고, `z_add_timeout`(timeout.c:111)이 `to->dticks = timeout.ticks + 1 + ticks_elapsed`로 **1 tick을 더한다**("적어도 그만큼은 잔다"를 보장하려고 — 지금 tick의 일부가 이미 지났을 수 있으니까). 1 + 1 = 2 tick = 20 ms. 드론 FC에서 `k_sleep(K_MSEC(1))`을 1 kHz 루프로 쓰려다 망하는 이유가 이 한 줄이다(E7).
- 이 설정은 `CONFIG_TICKLESS_KERNEL=y`, `CONFIG_SCHED_SIMPLE=y`, `CONFIG_TIMESLICING=y`, `CONFIG_USE_SWITCH`는 꺼져 있다(`.config`에서 확인). 마지막 것이 중요하다 — Cortex-M3에서 Zephyr는 옛 방식 `arch_swap`(PendSV) 경로를 쓴다.

### 5.2 예제 11 — gdb로 Zephyr 경로 찍기

같은 `rungdb.sh`, 다른 스크립트(`z_tour.gdb`, 90줄). 출력에 나오는 `cur prio`는 `_kernel.cpus[0].current->base.prio`다. 세 가지 기술을 더 썼다: 조건에 `thread == &hi_thread`처럼 **포인터 비교**(inferior 호출 없음), 앞 중단점이 세운 gdb 변수(`$given`, `$insleep`)로 "이 사건 뒤의 첫 번째만" 잡기, 마지막에 `main.c:35`에서 `kill`/`quit`(Zephyr 앱은 스스로 QEMU를 끝내지 않으므로). 첫 실행에서 `disable $bpnum`이 다른 중단점을 가리켜 의도와 다르게 동작했다 — 중단점 번호를 숫자로 직접 쓰는 것이 안전하다.

```sh
QX="" /Users/donh/.rtos_scratch/C12/rungdb.sh /var/tmp/rtos/C12/z_tour/zephyr/zephyr.elf /Users/donh/.rtos_scratch/C12/z_tour.gdb /var/tmp/rtos/C12/z_uart.txt
```

전체 사건 순서(각 정지점의 머리줄만 `grep "^===="`):

```text
==== [1 create] IPSR=0 cur prio=5
==== [1a start new thread] IPSR=0 cur prio=5
==== [1b PendSV after create] IPSR=14 cur prio=5
==== [2 sem_give wake] IPSR=0 cur prio=5
==== [2a z_reschedule] IPSR=0 cur prio=5
==== [2b PendSV (from arch_swap)] IPSR=14 cur prio=5
==== [3 k_sleep z_add_timeout] IPSR=0 cur prio=2
==== [3 k_sleep z_add_timeout] IPSR=0 cur prio=2
==== [5 tick announce] IPSR=15 cur prio=15
==== [4 ISR give] IPSR=24 cur prio=15
==== [4b ISR exit] IPSR=24 cur prio=15
==== [4c PendSV after ISR] IPSR=14 cur prio=15
==== reached done line, stopping
```

**경로 1 — k_thread_create → z_impl_k_thread_create → z_setup_new_thread → arch_new_thread**

```text
==== [1 create] IPSR=0 cur prio=5
#0  arch_new_thread (thread=..., stack=..., stack_ptr=..., entry=..., p1=..., p2=..., p3=...) at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/thread.c:92
#1  0x000012e6 in z_setup_new_thread (new_thread=..., stack=..., stack_size=..., entry=..., p1=..., p2=..., p3=..., prio=..., options=..., name=...) at /var/rtoslab/zephyrproject/zephyr/kernel/thread.c:639
#2  0x000033ec in z_impl_k_thread_create (new_thread=..., stack=..., stack_size=..., entry=..., p1=..., p2=..., p3=..., prio=..., options=..., delay=...) at /var/rtoslab/zephyrproject/zephyr/kernel/thread.c:793
#3  0x000002f8 in k_thread_create (new_thread=..., stack=..., stack_size=..., entry=..., p1=..., p2=..., p3=..., prio=..., options=..., delay=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:85
#4  main () at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:41

==== [1a start new thread] IPSR=0 cur prio=5
#0  z_impl_k_wakeup (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:1269
#1  0x00001294 in k_wakeup (thread=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:251
#2  k_thread_start (thread=...) at /var/rtoslab/zephyrproject/zephyr/include/zephyr/kernel.h:1306
#3  thread_schedule_new (thread=..., delay=...) at /var/rtoslab/zephyrproject/zephyr/kernel/include/kthread.h:55
#4  0x00003408 in z_impl_k_thread_create (new_thread=..., stack=..., stack_size=..., entry=..., p1=..., p2=..., p3=..., prio=..., options=..., delay=...) at /var/rtoslab/zephyrproject/zephyr/kernel/thread.c:797
#5  0x000002f8 in k_thread_create (new_thread=..., stack=..., stack_size=..., entry=..., p1=..., p2=..., p3=..., prio=..., options=..., delay=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:85
#6  main () at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:41

==== [1b PendSV after create] IPSR=14 cur prio=5
  current prio=5  ready_q.cache prio=2
```

읽는 법: `k_thread_create`의 파일이 `build/zephyr/include/generated/zephyr/syscalls/kernel.h`다. **Zephyr API는 빌드 때 생성되는 syscall 래퍼**다. userspace를 끈 빌드에선 래퍼가 `z_impl_`을 바로 부르고, 켠 빌드에선 SVC를 거쳐 `z_vrfy_` → `z_impl_`로 간다(C8). 그래서 Zephyr 소스를 읽을 때는 **API 이름 앞에 `z_impl_`을 붙여서 grep**한다. 이게 Zephyr 읽기의 첫 번째 요령이다.

`z_impl_k_thread_create`(thread.c:785~):

```c
  793: 	z_setup_new_thread(new_thread, stack, stack_size, entry, p1, p2, p3,
  794: 			  prio, options, NULL);
  796: 	if (!K_TIMEOUT_EQ(delay, K_FOREVER)) {
  797: 		thread_schedule_new(new_thread, delay);     /* K_NO_WAIT → k_thread_start → k_wakeup */
  798: 	}
```

`arch_new_thread`(arch/arm/core/cortex_m/thread.c:90~) — FreeRTOS `pxPortInitialiseStack`의 짝:

```c
  140: 	void *entry_wrapper = z_thread_entry;
  154: 	struct __basic_sf *iframe = Z_STACK_PTR_TO_FRAME(struct __basic_sf, stack_ptr);
  162: 	iframe->pc = (uint32_t)entry_wrapper;      /* PC = z_thread_entry (사용자 함수 아님) */
  165: 	iframe->pc &= 0xfffffffe;
  166: 	iframe->a1 = (uint32_t)entry;              /* R0 = 사용자 entry */
  167: 	iframe->a2 = (uint32_t)p1;
  168: 	iframe->a3 = (uint32_t)p2;
  169: 	iframe->a4 = (uint32_t)p3;
  171: 	iframe->xpsr = 0x01000000UL;
  173: 	thread->callee_saved.psp = (uint32_t)iframe;   /* PSP는 TCB에 저장 */
```

차이 두 가지: Zephyr는 사용자 함수를 바로 PC에 넣지 않고 `z_thread_entry`라는 **트램펄린**을 거친다(그래서 Zephyr backtrace 맨 밑은 항상 `z_thread_entry`다 — 아래 출력들을 보라). 사용자 함수가 return하면 `z_thread_entry`가 스레드를 정리한다. FreeRTOS는 LR = `prvTaskExitError`(return하면 assert)다. 그리고 초기 프레임 주소를 스택이 아니라 `thread->callee_saved.psp`에 둔다(§2.2).

**경로 2 — k_sem_give → z_impl_k_sem_give → wake → reschedule**

```text
==== [2 sem_give wake] IPSR=0 cur prio=5
#0  z_ready_thread (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:368
#1  0x0000120c in z_impl_k_sem_give (sem=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sem.c:107
#2  0x000002fe in k_sem_give (sem=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:1225
#3  main () at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:43

==== [2a z_reschedule] IPSR=0 cur prio=5
  current prio=5  ready_q.cache prio=2
#0  z_reschedule (lock=..., key=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:800
#1  0x00001214 in z_impl_k_sem_give (sem=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sem.c:115
#2  0x000002fe in k_sem_give (sem=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:1225
#3  main () at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:43

==== [2b PendSV (from arch_swap)] IPSR=14 cur prio=5
  current prio=5  ready_q.cache prio=2
#0  z_arm_pendsv () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/swap_helper.S:67
```

`z_impl_k_sem_give`(sem.c:95~) — 짧고 읽기 좋은 Zephyr 커널 코드의 표본:

```c
   97: 	k_spinlock_key_t key = k_spin_lock(&lock);       /* 단일 코어: irq_lock(BASEPRI) */
  103: 	thread = z_unpend_first_thread(&sem->wait_q);    /* 대기자 중 가장 높은 우선순위 하나 */
  105: 	if (unlikely(thread != NULL)) {
  106: 		arch_thread_return_value_set(thread, 0);     /* 깨는 쪽 k_sem_take의 반환값 = 0 */
  107: 		z_ready_thread(thread);
  108: 		resched = true;
  109: 	} else {
  110: 		sem->count += (sem->count != sem->limit) ? 1U : 0U;
  111: 		resched = handle_poll_events(sem);
  112: 	}
  114: 	if (unlikely(resched)) {
  115: 		z_reschedule(&lock, key);
  116: 	} else {
  117: 		k_spin_unlock(&lock, key);
  118: 	}
```

`ready_thread` → `update_cache`(sched.c:348 · 294~) — **여기서 "다음 스레드"가 정해진다**:

```c
  357: 	if (!z_is_thread_queued(thread) && z_is_thread_ready(thread)) {
  360: 		queue_thread(thread);                 /* runq에 우선순위 순 삽입 */
  361: 		update_cache(0);
  294: static ALWAYS_INLINE void update_cache(int preempt_ok)
  297: 	struct k_thread *thread = next_up();      /* runq_best(): runq 맨 앞 */
  299: 	if (should_preempt(thread, preempt_ok)) {
  302: 			z_reset_time_slice(thread);       /* (CONFIG_TIMESLICING) 새 스레드의 slice 재설정 */
  306: 		_kernel.ready_q.cache = thread;
  307: 	} else {
  308: 		_kernel.ready_q.cache = _current;
```

`reschedule`(sched.c:560~)와 그 아래 — bt에 안 나오는 인라인 단계는 소스로 확인했다:

```c
  546: static inline bool need_swap(void)
  555: 	new_thread = _kernel.ready_q.cache;
  556: 	return new_thread != _current;
  560: static void reschedule(struct k_spinlock *lock, k_spinlock_key_t key)
  562: 	if (resched(key.key) && need_swap()) {      /* ISR 안이 아니고, cache가 current와 다르면 */
  563: 		z_swap(lock, key);
```

`CONFIG_USE_SWITCH`가 꺼진 이 빌드에서 `z_swap`은 kswap.h 아래쪽(`#else /* !CONFIG_USE_SWITCH */`)의 판이다: `z_swap` → `z_swap_irqlock` → `arch_swap`(arch/arm/include/cortex_m/kernel_arch_func.h:87):

```c
static ALWAYS_INLINE int arch_swap(unsigned int key)
{
	/* store off key and return value */
	_current->arch.basepri = key;
	_current->arch.swap_return_value = -EAGAIN;

	/* set pending bit to make sure we will take a PendSV exception */
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

	/* clear mask or enable all irqs to take a pendsv */
	irq_unlock(0);

	/* Context switch is performed here. Returning implies the
	 * thread has been context-switched-in again.
	 */
	return _current->arch.swap_return_value;
}
```

(kernel_arch_func.h:87~103, `#ifndef CONFIG_USE_SWITCH` 안의 정의를 그대로 옮겼다.) PENDSVSET을 켜고 `irq_unlock(0)`으로 마스크를 푸는 순간 PendSV가 들어와 스위치가 일어나고, 이 함수의 `return`은 **나중에 이 스레드가 다시 선택됐을 때** 실행된다. FreeRTOS의 `portYIELD`와 같은 원리를 C 함수 하나로 감싼 것이다.

kswap.h에 `do_swap`(77줄)이라는 그럴듯한 함수가 있지만 `CONFIG_USE_SWITCH=y`(SMP · 새 방식) 전용이다. **같은 이름 `z_swap`이 kswap.h에 두 번 정의되어 있고(191줄 · 228줄 부근), 어느 쪽이 컴파일되는지는 설정이 정한다** — 정적으로 읽을 때 가장 쉽게 틀리는 곳이고, `gcc -E`나 gdb가 필요한 이유다.

그리고 `z_arm_pendsv`(swap_helper.S:48~)는 결정하지 않는다:

```asm
    ldr r2, [r1, #_kernel_offset_to_current]          ; 지금 스레드
    ldr r0, =_thread_offset_to_callee_saved
    add r0, r2                                         ; R4-R11, PSP를 TCB 안에 저장
    ...
    /* fetch the thread to run from the ready queue cache */
    ldr r2, [r1, #_kernel_offset_to_ready_q_cache]
    str r2, [r1, #_kernel_offset_to_current]           ; current = cache
    ...
    ldmia r0, {r4-r11, ip}                             ; 새 스레드 TCB에서 복원 (339줄)
```

(swap_helper.S의 64~80 · 140~150 · 339줄에서 골라 붙였다. `;` 주석은 이 노트가 붙였다.) 2a와 2b의 출력이 이 설계를 숫자로 증명한다: `z_reschedule`에 들어갈 때 이미 `ready_q.cache prio=2`(hi)이고, PendSV는 그걸 `current`로 옮기기만 한다.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">Zephyr 경로 1 · 2 — bt 프레임(실선) + 소스로만 확인한 인라인 단계(점선)</text>
<text x="165" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 1: k_thread_create</text>
<rect x="20" y="56" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="72" font-size="12" text-anchor="middle" font-weight="bold">main</text>
<text x="165" y="88" font-size="12" text-anchor="middle">main.c:41</text>
<rect x="20" y="112" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="128" font-size="12" text-anchor="middle" font-weight="bold">z_impl_k_thread_create</text>
<text x="165" y="144" font-size="12" text-anchor="middle">thread.c:793 / 797</text>
<line x1="165" y1="96" x2="165" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="20" y="168" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="184" font-size="12" text-anchor="middle" font-weight="bold">z_setup_new_thread</text>
<text x="165" y="200" font-size="12" text-anchor="middle">thread.c:639</text>
<line x1="165" y1="152" x2="165" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="20" y="224" width="290" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="165" y="240" font-size="12" text-anchor="middle" font-weight="bold">arch_new_thread</text>
<text x="165" y="256" font-size="12" text-anchor="middle">cortex_m/thread.c:92 초기 프레임</text>
<line x1="165" y1="208" x2="165" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="20" y="290" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="306" font-size="12" text-anchor="middle" font-weight="bold">thread_schedule_new → k_thread_start</text>
<text x="165" y="322" font-size="12" text-anchor="middle">kthread.h:55 (inline)</text>
<rect x="20" y="346" width="290" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="362" font-size="12" text-anchor="middle" font-weight="bold">z_impl_k_wakeup</text>
<text x="165" y="378" font-size="12" text-anchor="middle">sched.c:1269 → ready + reschedule</text>
<line x1="165" y1="330" x2="165" y2="344" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<path d="M 20,132 C 2,180 2,250 18,308" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<text x="505" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 2: k_sem_give (main → hi)</text>
<rect x="370" y="56" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="515" y="72" font-size="12" text-anchor="middle" font-weight="bold">main (prio 5)</text>
<text x="515" y="88" font-size="12" text-anchor="middle">main.c:43</text>
<rect x="370" y="112" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="515" y="128" font-size="12" text-anchor="middle" font-weight="bold">z_impl_k_sem_give</text>
<text x="515" y="144" font-size="12" text-anchor="middle">sem.c:107 / 115</text>
<line x1="515" y1="96" x2="515" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="370" y="168" width="290" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515" y="184" font-size="12" text-anchor="middle" font-weight="bold">z_ready_thread → ready_thread</text>
<text x="515" y="200" font-size="12" text-anchor="middle">sched.c:368 → 357</text>
<line x1="515" y1="152" x2="515" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="370" y="224" width="290" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="5,3"/>
<text x="515" y="240" font-size="12" text-anchor="middle" font-weight="bold">update_cache (inline)</text>
<text x="515" y="256" font-size="12" text-anchor="middle">sched.c:306 cache = hi</text>
<line x1="515" y1="208" x2="515" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="370" y="280" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="515" y="296" font-size="12" text-anchor="middle" font-weight="bold">z_reschedule → reschedule</text>
<text x="515" y="312" font-size="12" text-anchor="middle">sched.c:800 → 562</text>
<line x1="515" y1="264" x2="515" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="370" y="336" width="290" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="5,3"/>
<text x="515" y="352" font-size="12" text-anchor="middle" font-weight="bold">z_swap → arch_swap (inline)</text>
<text x="515" y="368" font-size="12" text-anchor="middle">kswap.h · PENDSVSET</text>
<line x1="515" y1="320" x2="515" y2="334" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="370" y="392" width="290" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="515" y="408" font-size="12" text-anchor="middle" font-weight="bold">z_arm_pendsv</text>
<text x="515" y="424" font-size="12" text-anchor="middle">current = ready_q.cache</text>
<line x1="515" y1="376" x2="515" y2="390" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="10" y="452" width="660" height="58" rx="5" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="20" y="474" font-size="12">실측(bt 2a·2b): z_reschedule 진입 때 이미 current prio=5, ready_q.cache prio=2 —</text>
<text x="20" y="494" font-size="12">Zephyr는 "다음 스레드"를 깨우는 순간(update_cache) 정해 두고, PendSV는 캐시를 읽기만 한다</text>
</svg>
```
그림 6 — Zephyr 경로 1 · 2. 실선 = bt에 찍힌 프레임, 점선 = 인라인이라 bt에 없지만 소스에서 확인한 단계.

### 5.3 경로 3 — k_sleep → z_tick_sleep → timeout

```text
==== [3 k_sleep z_add_timeout] IPSR=0 cur prio=2
#0  z_add_timeout (to=..., fn=..., timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeout.c:89
#1  0x00001a96 in z_reset_time_slice (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeslicing.c:83
#2  0x0000146a in update_cache (preempt_ok=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:302
#3  unready_thread (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:604
#4  0x000017f2 in z_tick_sleep (timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:1191
#5  0x0000357c in z_impl_k_sleep (timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:1226
#6  0x00000238 in k_sleep (timeout=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:185
#7  hi_entry (a=..., b=..., c=...) at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:31
#8  0x000028da in z_thread_entry (entry=..., p1=..., p2=..., p3=...) at /var/rtoslab/zephyrproject/zephyr/lib/os/thread_entry.c:60

==== [3 k_sleep z_add_timeout] IPSR=0 cur prio=2
#0  z_add_timeout (to=..., fn=..., timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeout.c:89
#1  0x00001800 in z_add_thread_timeout (thread=..., ticks=...) at /var/rtoslab/zephyrproject/zephyr/kernel/include/timeout_q.h:82
#2  z_tick_sleep (timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:1192
#3  0x0000357c in z_impl_k_sleep (timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:1226
#4  0x00000238 in k_sleep (timeout=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:185
#5  hi_entry (a=..., b=..., c=...) at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:31
#6  0x000028da in z_thread_entry (entry=..., p1=..., p2=..., p3=...) at /var/rtoslab/zephyrproject/zephyr/lib/os/thread_entry.c:60
```

`z_tick_sleep`(sched.c:1172~):

```c
 1181: 	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
 1182: 		k_yield();                                  /* k_sleep(K_NO_WAIT) = 양보 */
 1186: 	k_spinlock_key_t key = k_spin_lock(&_sched_spinlock);
 1191: 	unready_thread(_current);                      /* runq에서 빼고 update_cache */
 1192: 	expected_wakeup_ticks = (uint32_t)z_add_thread_timeout(_current, timeout);
 1193: 	z_mark_thread_as_sleeping(_current);
 1195: 	(void)z_swap(&_sched_spinlock, key);           /* 여기서 PendSV로 나가 다른 스레드가 돈다 */
 1197: 	if (!z_is_aborted_thread_timeout(_current)) {
 1198: 		return 0;                                  /* 정상 기상 */
```

### 5.4 gdb가 보여 준 "예상 밖" — time slice의 z_add_timeout

처음에 `k_sleep`의 timeout 경로를 잡으려고 `z_add_timeout`에 중단점을 걸었는데, **첫 번째로 잡힌 것은 sleep 타임아웃이 아니었다**. 위 출력의 첫 번째 bt를 보면 `unready_thread → update_cache → z_reset_time_slice → z_add_timeout`이다. `CONFIG_TIMESLICING=y`라서, ready 캐시가 다른 스레드로 바뀔 때마다 **새 스레드의 time slice 타이머를 같은 타임아웃 리스트에 다시 건다**(timeslicing.c:83). `k_sem_take(K_FOREVER)`에서도, ISR의 give에서도 똑같이 불렸다(전체 로그 `z_bt_full.txt`). 소스만 읽었다면 "`z_add_timeout`은 sleep · timeout 전용"이라고 생각했을 것이다. **실행해 본 그래프가 정적으로 읽은 그래프를 고친다** — 단계 4를 건너뛰면 안 되는 이유다. 실무 함의: Zephyr에서 같은 우선순위 스레드가 하나뿐이어도 스위치마다 타임아웃 리스트 연산이 하나 더 있다. 쓰지 않으면 `CONFIG_TIMESLICING=n`.

### 5.5 경로 4 · 5 — ISR 종료 → PendSV, 그리고 sys_clock_announce

```text
==== [5 tick announce] IPSR=15 cur prio=15
#0  sys_clock_announce_locked (ticks=..., key=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeout.c:224
#1  0x0000331a in sys_clock_announce (ticks=...) at /var/rtoslab/zephyrproject/zephyr/include/zephyr/drivers/timer/system_timer.h:89
#2  0x00000dd0 in sys_clock_isr () at /var/rtoslab/zephyrproject/zephyr/drivers/timer/cortex_m_systick.c:397
#3  <signal handler called>
#4  __ISB () at /var/rtoslab/zephyrproject/modules/hal/cmsis_6/CMSIS/Core/Include/cmsis_gcc.h:175
#5  arch_cpu_idle () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/cpu_idle.c:105
#6  0x00003398 in k_cpu_idle () at /var/rtoslab/zephyrproject/zephyr/include/zephyr/kernel.h:6807
#7  idle (unused1=..., unused2=..., unused3=...) at /var/rtoslab/zephyrproject/zephyr/kernel/idle.c:75
#8  0x000028da in z_thread_entry (entry=..., p1=..., p2=..., p3=...) at /var/rtoslab/zephyrproject/zephyr/lib/os/thread_entry.c:60

==== [4 ISR give] IPSR=24 cur prio=15
#0  z_impl_k_sem_give (sem=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sem.c:96
#1  0x000002c4 in k_sem_give (sem=...) at /var/tmp/rtos/C12/z_tour/zephyr/include/generated/zephyr/syscalls/kernel.h:1225
#2  t0_isr (arg=...) at /Users/donh/.rtos_scratch/C12/z_tour/src/main.c:23
#3  0x00000b12 in _isr_wrapper () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/isr_wrapper.c:80
#4  <signal handler called>
#5  __ISB () at /var/rtoslab/zephyrproject/modules/hal/cmsis_6/CMSIS/Core/Include/cmsis_gcc.h:175
#6  arch_cpu_idle () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/cpu_idle.c:105
... (#7~#9 = k_cpu_idle · idle · z_thread_entry, 위와 같음)

==== [4b ISR exit] IPSR=24 cur prio=15
  current prio=15  ready_q.cache prio=2
#0  z_arm_exc_exit () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/exc_exit.c:58
#1  0x00000b16 in _isr_wrapper () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/isr_wrapper.c:90
#2  <signal handler called>
... (#3~#7 = __ISB · arch_cpu_idle · k_cpu_idle · idle · z_thread_entry)

==== [4c PendSV after ISR] IPSR=14 cur prio=15
  current prio=15  ready_q.cache prio=2
```

타임아웃으로 깨는 경로는 같은 앱의 다른 실행 로그(`z_bt_full.txt`, `z_add_timeout`에 무조건 중단점을 건 실행)에 찍혔다:

```text
==== [3 k_sleep z_add_timeout] IPSR=15 cur=other(prio 15)
#0  z_add_timeout (to=..., fn=..., timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeout.c:89
#1  0x00001a96 in z_reset_time_slice (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeslicing.c:83
#2  0x00001400 in update_cache (preempt_ok=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:302
#3  ready_thread (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:361
#4  0x00003532 in z_sched_wake_thread_locked (thread=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:674
#5  0x00003552 in z_thread_timeout (timeout=...) at /var/rtoslab/zephyrproject/zephyr/kernel/sched.c:686
#6  0x00001cd6 in sys_clock_announce_locked (ticks=..., key=...) at /var/rtoslab/zephyrproject/zephyr/kernel/timeout.c:252
#7  0x0000331a in sys_clock_announce (ticks=...) at /var/rtoslab/zephyrproject/zephyr/include/zephyr/drivers/timer/system_timer.h:89
#8  0x00000dd0 in sys_clock_isr () at /var/rtoslab/zephyrproject/zephyr/drivers/timer/cortex_m_systick.c:397
#9  <signal handler called>
#10 __ISB () at /var/rtoslab/zephyrproject/modules/hal/cmsis_6/CMSIS/Core/Include/cmsis_gcc.h:175
...
```

(이 실행은 스크립트 머리줄 형식이 이전 판이라 `cur=other(prio 15)`로 찍혔다.)

`sys_clock_announce_locked`(timeout.c:223~) — Zephyr tick 처리의 전부:

```c
  237: 	announce_remaining = ticks;                    /* tickless: 지난 tick 수를 한 번에 */
  241: 	for (t = first();
  242: 	     (t != NULL) && (t->dticks <= announce_remaining);
  243: 	     t = first()) {
  244: 		int dt = t->dticks;
  246: 		curr_tick += dt;
  248: 		remove_timeout(t);
  251: 		k_spin_unlock(&timeout_lock, key);
  252: 		t->fn(t);                                  /* 함수 포인터! sleep이면 z_thread_timeout */
  253: 		key = k_spin_lock(&timeout_lock);
  254: 		announce_remaining -= dt;
  255: 	}
  264: 	sys_clock_set_timeout(next_timeout(0), false); /* 다음 깨울 시각으로 HW 타이머 재설정 */
  269: 	z_time_slice();                                /* (CONFIG_TIMESLICING) */
```

252줄의 `t->fn(t)`가 정적 도구의 사각지대다. 함수 포인터라서 `-fcallgraph-info`는 어디로 가는지 모른다. gdb bt가 `z_thread_timeout`이라고 알려 준다. 타임아웃 리스트는 **델타 리스트**(각 노드가 앞 노드와의 tick 차이 `dticks`를 가짐)다 — FreeRTOS의 "절대 시각 정렬 리스트 두 개 + 오버플로 교대"와 다른 설계다(C5 · C6).

`z_arm_exc_exit`(exc_exit.c:55~, `z_arm_int_exit`는 `FUNC_ALIAS`로 같은 주소):

```c
   57: #ifdef CONFIG_PREEMPT_ENABLED
   58: 	if (_kernel.ready_q.cache != _kernel.cpus->current) {
   59: 		SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
   60: 	}
   61: #endif /* CONFIG_PREEMPT_ENABLED */
```

4b 출력이 이 비교의 순간이다: `current prio=15`(idle), `cache prio=2`(hi) → 다르다 → PENDSVSET → 4c PendSV. FreeRTOS에서 개발자가 ISR 끝에 손으로 쓰는 `portYIELD_FROM_ISR(woken)`을 **Zephyr는 모든 ISR 출구(`_isr_wrapper` 90줄)에서 자동으로** 한다. 그래서 Zephyr ISR에는 `xHigherPriorityTaskWoken` 같은 인자가 없다(E1). tick ISR(`sys_clock_isr`)도 끝에서 `z_arm_int_exit()`를 부른다(cortex_m_systick.c:408 — 앞선 탐색 실행의 bt에 `#1 sys_clock_isr () at cortex_m_systick.c:408`로 찍혔다).

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">Zephyr 경로 3 · 4 · 5 — k_sleep · ISR 종료 · tick announce (bt 그대로)</text>
<text x="113" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 3: k_sleep</text>
<rect x="8" y="56" width="210" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="113" y="72" font-size="12" text-anchor="middle" font-weight="bold">hi_entry</text>
<text x="113" y="88" font-size="12" text-anchor="middle">main.c:31</text>
<rect x="8" y="112" width="210" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="113" y="128" font-size="12" text-anchor="middle" font-weight="bold">z_impl_k_sleep</text>
<text x="113" y="144" font-size="12" text-anchor="middle">sched.c:1226</text>
<line x1="113" y1="96" x2="113" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="8" y="168" width="210" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="113" y="184" font-size="12" text-anchor="middle" font-weight="bold">z_tick_sleep</text>
<text x="113" y="200" font-size="12" text-anchor="middle">sched.c:1191 / 1192</text>
<line x1="113" y1="152" x2="113" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="8" y="224" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="113" y="240" font-size="12" text-anchor="middle" font-weight="bold">z_add_thread_timeout</text>
<text x="113" y="256" font-size="12" text-anchor="middle">timeout_q.h:82</text>
<line x1="113" y1="208" x2="113" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="8" y="280" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="113" y="296" font-size="12" text-anchor="middle" font-weight="bold">z_add_timeout</text>
<text x="113" y="312" font-size="12" text-anchor="middle">timeout.c:89 깨울 시각</text>
<line x1="113" y1="264" x2="113" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<text x="10" y="352" font-size="12">같은 sleep에서 z_add_timeout이 한 번 더:</text>
<text x="10" y="370" font-size="12">unready_thread → update_cache</text>
<text x="10" y="388" font-size="12">→ z_reset_time_slice (time slice)</text>
<text x="340" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 4: ISR → PendSV</text>
<rect x="235" y="56" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="72" font-size="12" text-anchor="middle" font-weight="bold">_isr_wrapper</text>
<text x="340" y="88" font-size="12" text-anchor="middle">isr_wrapper.c:80 (IPSR=24)</text>
<rect x="235" y="112" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="128" font-size="12" text-anchor="middle" font-weight="bold">t0_isr → k_sem_give</text>
<text x="340" y="144" font-size="12" text-anchor="middle">main.c:23 · sem.c:96</text>
<line x1="340" y1="96" x2="340" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="235" y="168" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="5,3"/>
<text x="340" y="184" font-size="12" text-anchor="middle" font-weight="bold">ready_thread</text>
<text x="340" y="200" font-size="12" text-anchor="middle">cache = hi (prio 2)</text>
<line x1="340" y1="152" x2="340" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="235" y="224" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="240" font-size="12" text-anchor="middle" font-weight="bold">z_arm_exc_exit</text>
<text x="340" y="256" font-size="12" text-anchor="middle">isr_wrapper.c:90</text>
<line x1="340" y1="208" x2="340" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="235" y="280" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="296" font-size="12" text-anchor="middle" font-weight="bold">cache ≠ current</text>
<text x="340" y="312" font-size="12" text-anchor="middle">exc_exit.c:58 PENDSVSET</text>
<line x1="340" y1="264" x2="340" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="235" y="336" width="210" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="352" font-size="12" text-anchor="middle" font-weight="bold">z_arm_pendsv</text>
<text x="340" y="368" font-size="12" text-anchor="middle">idle(15) → hi(2)</text>
<line x1="340" y1="320" x2="340" y2="334" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<text x="567" y="44" font-size="13" text-anchor="middle" font-weight="bold">경로 5: tick</text>
<rect x="462" y="56" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="567" y="72" font-size="12" text-anchor="middle" font-weight="bold">sys_clock_isr</text>
<text x="567" y="88" font-size="12" text-anchor="middle">cortex_m_systick.c:397</text>
<rect x="462" y="112" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="567" y="128" font-size="12" text-anchor="middle" font-weight="bold">sys_clock_announce</text>
<text x="567" y="144" font-size="12" text-anchor="middle">system_timer.h (inline)</text>
<line x1="567" y1="96" x2="567" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="462" y="168" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="567" y="184" font-size="12" text-anchor="middle" font-weight="bold">announce_locked</text>
<text x="567" y="200" font-size="12" text-anchor="middle">timeout.c:252 t-&gt;fn(t)</text>
<line x1="567" y1="152" x2="567" y2="166" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="462" y="224" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="567" y="240" font-size="12" text-anchor="middle" font-weight="bold">z_thread_timeout</text>
<text x="567" y="256" font-size="12" text-anchor="middle">sched.c:686</text>
<line x1="567" y1="208" x2="567" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="462" y="280" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="567" y="296" font-size="12" text-anchor="middle" font-weight="bold">z_sched_wake_thread_locked</text>
<text x="567" y="312" font-size="12" text-anchor="middle">sched.c:674</text>
<line x1="567" y1="264" x2="567" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="462" y="336" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="567" y="352" font-size="12" text-anchor="middle" font-weight="bold">ready_thread</text>
<text x="567" y="368" font-size="12" text-anchor="middle">update_cache</text>
<line x1="567" y1="320" x2="567" y2="334" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="10" y="404" width="660" height="146" rx="5" fill="#888" fill-opacity="0.05" stroke="#888"/>
<text x="20" y="426" font-size="13" font-weight="bold">읽는 법</text>
<text x="20" y="446" font-size="12">· tick ISR도 끝에서 z_arm_exc_exit를 부른다 (bt: cortex_m_systick.c:408)</text>
<text x="20" y="466" font-size="12">· ISR 안의 k_sem_give는 스위치를 하지 않는다: 캐시만 바꾸고, ISR 종료에서 PENDSVSET</text>
<text x="20" y="486" font-size="12">· tickless(CONFIG_TICKLESS_KERNEL=y): announce는 지난 tick 수(dticks)를 한 번에 알린다</text>
<text x="20" y="506" font-size="12">· 점선 상자 = 다른 정지점 bt(경로 4의 3번째)에서 확인한 단계</text>
<text x="20" y="526" font-size="12">· 경로 5의 아래 세 상자는 z_bt_full.txt(같은 앱, 다른 실행)의 bt</text>
</svg>
```
그림 7 — Zephyr 경로 3 · 4 · 5. ISR 안의 `k_sem_give`는 `cache`만 바꾸고, 스위치 요청은 공통 ISR 출구가 한다.

### 5.6 FreeRTOS vs Zephyr — 소스에서 읽은 차이 한 표

| 항목 | FreeRTOS V11.3.2 (ARM_CM3) | Zephyr v4.4.2 (mps2/an385, !USE_SWITCH) |
|---|---|---|
| "다음 태스크" 결정 시점 | PendSV 안 `vTaskSwitchContext` (tasks.c:5215) | 깨우는 순간 `update_cache` (sched.c:294) |
| PendSV가 하는 일 | 저장 → C 함수 호출(결정) → 복원 | 저장 → `ready_q.cache` 읽기 → 복원 (C 결정 없음) |
| R4~R11 저장 위치 | 태스크 스택 (`stmdb r0!`) | TCB 안 `callee_saved` (오프셋 48) |
| ISR 끝 스위치 요청 | 개발자가 `portYIELD_FROM_ISR(woken)` | 공통 출구 `z_arm_exc_exit`가 자동 |
| API 이름 → 구현 | `xQueueSend` → 매크로 → `xQueueGenericSend` | `k_sem_give` → 생성된 syscall 래퍼 → `z_impl_k_sem_give` |
| 블로킹 때 커널 보호 | 짧은 임계 구역 + `vTaskSuspendAll` + 큐 잠금 | `_sched_spinlock`(단일 코어 = irq_lock) 한 겹 |
| tick | 매 tick `xTaskIncrementTick` (비교 1번이 대부분) | tickless 기본, `sys_clock_announce(dticks)` |
| 타임아웃 리스트 | 절대 시각 정렬 리스트 2개(오버플로 교대) | 델타 리스트 1개 + `t->fn(t)` 콜백 |
| 새 태스크 진입 | PC = 태스크 함수, LR = `prvTaskExitError` | PC = `z_thread_entry` 트램펄린 |

### 5.7 예제 12 — 소스에서 읽은 규칙 하나를 실행으로 확인: 협력형 스레드는 비키지 않는다

`update_cache`가 부르는 `should_preempt`(kernel/include/kthread.h:224~)를 읽으면 이런 규칙이 보인다:

```c
  230: 	if (preempt_ok != 0) {
  231: 		return true;                                  /* yield 등 명시적 양보 */
  237: 	if (z_is_thread_prevented_from_running(_current)) {
  238: 		return true;                                  /* 지금 스레드가 블록 중이면 */
  244: 	if (thread_is_preemptible(_current) || thread_is_metairq(thread)) {
  245: 		return true;                                  /* 지금 스레드가 선점형이면 */
  258: 	return false;                                     /* 그 밖(협력형 · sched_lock)은 비키지 않음 */
```

무엇을 확인하는 코드인지: 읽은 대로라면 **지금 스레드가 협력형(음수 우선순위)이면, 더 높은 우선순위 스레드를 깨워도 바로 비키지 않는다.** 예제 10에서 두 줄만 바꿔 확인한다: `main` 우선순위를 `-1`(prj.conf `CONFIG_MAIN_THREAD_PRIORITY=-1`), `hi`를 `-2`(더 높은 협력형).

```sh
sed 's/hi_entry, NULL, NULL, NULL, 2, 0, K_NO_WAIT/hi_entry, NULL, NULL, NULL, -2, 0, K_NO_WAIT/' z_tour/src/main.c > z_coop/src/main.c
printf 'CONFIG_PRINTK=y\nCONFIG_MAIN_THREAD_PRIORITY=-1\n' > z_coop/prj.conf
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C12/z_coop /var/tmp/rtos/C12/z_coop
timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -kernel /var/tmp/rtos/C12/z_coop/zephyr/zephyr.elf
```

```text
           FLASH:       13840 B         4 MB      0.33%
             RAM:        7304 B         4 MB      0.17%
*** Booting Zephyr OS build v4.4.2 ***
main: after give
hi: woke by main at 0 ms
hi: sleep done at 20 ms
hi: woke by ISR #1 at 30 ms
done
qemu-system-arm: terminating on signal 15 from pid 2472069 (timeout)
```

출력에서 볼 것: 예제 10과 순서가 뒤집혔다 — `main: after give`가 먼저다. `hi`(-2)가 `main`(-1)보다 높은데도 `k_sem_give` 안에서 스위치가 일어나지 않았고, `main`이 `k_msleep`으로 스스로 블록할 때 비로소 `hi`가 돌았다. FreeRTOS에는 이 개념(태스크 단위 협력형)이 없다 — FreeRTOS에서 같은 효과를 내려면 `configUSE_PREEMPTION=0`(전체) 또는 `vTaskSuspendAll`을 쓴다(C4). 이 실험이 "소스 읽기 → 가설 → 실행 확인"의 한 바퀴다.

---

## 6. 크기 · 복잡도로 읽을 순서 정하기

함수가 수백 개일 때 "어디부터?"를 감으로 정하지 말고 숫자로 정한다. 지표는 셋이다: **크기**(바이트 — 기능이 많은 곳), **분기 수**(McCabe 순환 복잡도 근사 — 경우의 수가 많은 곳), 그리고 **핫 패스 여부**(이 노트의 gdb 투어에서 지나간 곳). 앞의 둘이 크고 셋째에도 걸리는 함수가 "먼저 읽을" 함수다.

### 6.1 예제 13 — 함수 크기 순위 (`nm --size-sort`)

무엇을 확인하는 코드인지: 템플릿과 같은 플래그(`-O2 -ffunction-sections`)로 FreeRTOS 커널 파일을 오브젝트로 컴파일하고 함수 크기 상위를 뽑는다. Zephyr는 `-Os` 빌드(`CONFIG_DEBUG_OPTIMIZATIONS` 줄을 뺀 `z_tour_os`)의 `libkernel.a`에서 스케줄러 관련 오브젝트만 본다.

```sh
#!/bin/bash
# sizes.sh — 템플릿과 같은 플래그로 커널 파일을 오브젝트로 컴파일하고, 함수 크기 상위를 뽑는다
K=/var/rtoslab/src/FreeRTOS-Kernel; T=/var/rtoslab/templates; O=/var/tmp/rtos/C12/obj; mkdir -p $O
CF="-mthumb -mcpu=cortex-m3 -O2 -g -Wall -Wextra -ffunction-sections -I$T/freertos-mps2 -I$T/common -I$K/include -I$K/portable/GCC/ARM_CM3"
for f in tasks queue list; do arm-none-eabi-gcc $CF -c $K/$f.c -o $O/$f.o; done
arm-none-eabi-gcc $CF -c $K/portable/GCC/ARM_CM3/port.c -o $O/port.o
for f in tasks queue list port; do
  n=$(arm-none-eabi-nm --size-sort -S $O/$f.o | grep -ci " t ")
  s=$(arm-none-eabi-size $O/$f.o | awk 'NR==2{print $1}')
  echo "$f.o: text=$s B, functions=$n"
done
echo "--- top 12 by size (tasks.o + queue.o) ---"
for f in tasks queue; do arm-none-eabi-nm --size-sort -S -t d $O/$f.o | grep -i " t " | sed "s/^/$f /"; done \
  | sort -k3,3nr | head -12 | awk '{printf "%-6s %5d B  %s\n", $1, $3, $5}'
```

```text
tasks.o: text=10097 B, functions=79
queue.o: text=4647 B, functions=42
list.o: text=144 B, functions=5
port.o: text=923 B, functions=11
--- top 12 by size (tasks.o + queue.o) ---
tasks    420 B  xTaskIncrementTick.part.0
queue    404 B  xQueueGenericSend
queue    396 B  xQueueSemaphoreTake
tasks    376 B  xTaskResumeAll
queue    352 B  xQueueReceive
tasks    348 B  xTaskGenericNotifyFromISR
queue    344 B  xQueuePeek
tasks    304 B  vTaskGenericNotifyGiveFromISR
tasks    300 B  vTaskDelete
tasks    292 B  vTaskGetRunTimeStatistics
tasks    284 B  vTaskListTasks
tasks    276 B  vTaskPrioritySet
```

Zephyr 쪽(`zsizes.sh`):

```sh
#!/bin/bash
# zsizes.sh <build_dir> — Zephyr libkernel.a에서 스케줄러 관련 오브젝트의 함수 크기 순위 (-Os 빌드)
Z=$1/zephyr/kernel/libkernel.a
arm-none-eabi-size $Z | grep -E "^ .*(sched|thread|timeout|sem|timeslicing)\.c\.obj" | awk '{printf "%-18s text=%5d B\n", $6, $1}'
echo "--- top 12 functions ---"
arm-none-eabi-nm -S -t d $Z | awk '/:$/{cur=$1; next}
  NF==4 && $3 ~ /[tT]/ && cur ~ /^(sched|thread|timeout|sem|timeslicing)\.c\.obj:$/ {
    o=cur; sub(/\.c\.obj:/, "", o); printf "%5d B  %-12s %s\n", $2+0, o, $4 }' | sort -nr | head -12
```

```text
sem.c.obj          text=  312 B
thread.c.obj       text=  622 B
sched.c.obj        text= 3038 B
timeslicing.c.obj  text=  348 B
timeout.c.obj      text= 1110 B
--- top 12 functions ---
  316 B  sched        z_thread_abort
  288 B  timeout      z_add_timeout
  240 B  sched        z_impl_k_thread_suspend
  216 B  sched        z_thread_prio_set
  184 B  timeout      sys_clock_announce_locked
  132 B  sched        z_impl_k_yield
  128 B  sched        ready_thread
  124 B  thread       k_thread_state_str
  120 B  sched        z_tick_sleep
  108 B  sem          z_impl_k_sem_reset
  108 B  sched        z_impl_k_thread_join
  108 B  sched        move_current_to_end_of_prio_q
```

출력에서 볼 것: FreeRTOS 1등은 소스에 없는 이름 `xTaskIncrementTick.part.0`이다(예제 5). 큐의 send/receive/take 셋이 400 B 안팎으로 나란히 있다 — 같은 뼈대(§4.4)의 변주라는 뜻이고, 하나를 읽으면 셋을 읽은 셈이다. Zephyr는 `-Os`에 `ALWAYS_INLINE`이 많아 함수가 작고, 큰 것은 수명 관리(`abort`, `suspend`)와 타임아웃 리스트(`z_add_timeout`)다. `ready_thread`(128 B)가 큰 이유는 `queue_thread` · `update_cache` · `next_up`이 전부 그 안에 인라인됐기 때문이다(예제 15에서 다시 확인). 측정 차트:

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">측정: 함수 크기 순위 (arm-none-eabi-nm --size-sort, Cortex-M3)</text>
<text x="10" y="46" font-size="13" font-weight="bold">FreeRTOS V11.3.2 tasks.o + queue.o (-O2, 템플릿 플래그)</text>
<text x="242" y="69" font-size="12" text-anchor="end">xTaskIncrementTick.part.0</text>
<rect x="250.0" y="58.0" width="343.6" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="600" y="69" font-size="12">420 B</text>
<text x="242" y="88" font-size="12" text-anchor="end">xQueueGenericSend</text>
<rect x="250.0" y="77.0" width="330.5" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="587" y="88" font-size="12">404 B</text>
<text x="242" y="107" font-size="12" text-anchor="end">xQueueSemaphoreTake</text>
<rect x="250.0" y="96.0" width="324.0" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="580" y="107" font-size="12">396 B</text>
<text x="242" y="126" font-size="12" text-anchor="end">xTaskResumeAll</text>
<rect x="250.0" y="115.0" width="307.6" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="564" y="126" font-size="12">376 B</text>
<text x="242" y="145" font-size="12" text-anchor="end">xQueueReceive</text>
<rect x="250.0" y="134.0" width="288.0" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="544" y="145" font-size="12">352 B</text>
<text x="242" y="164" font-size="12" text-anchor="end">xTaskGenericNotifyFromISR</text>
<rect x="250.0" y="153.0" width="284.7" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="541" y="164" font-size="12">348 B</text>
<text x="242" y="183" font-size="12" text-anchor="end">xQueuePeek</text>
<rect x="250.0" y="172.0" width="281.5" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="537" y="183" font-size="12">344 B</text>
<text x="242" y="202" font-size="12" text-anchor="end">vTaskGenericNotifyGiveFromISR</text>
<rect x="250.0" y="191.0" width="248.7" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="505" y="202" font-size="12">304 B</text>
<text x="242" y="221" font-size="12" text-anchor="end">vTaskDelete</text>
<rect x="250.0" y="210.0" width="245.5" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="501" y="221" font-size="12">300 B</text>
<text x="242" y="240" font-size="12" text-anchor="end">vTaskGetRunTimeStatistics</text>
<rect x="250.0" y="229.0" width="238.9" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="495" y="240" font-size="12">292 B</text>
<text x="10" y="274" font-size="13" font-weight="bold">Zephyr v4.4.2 sched · thread · timeout · sem · timeslicing (-Os)</text>
<text x="242" y="297" font-size="12" text-anchor="end">z_thread_abort</text>
<rect x="250.0" y="286.0" width="258.5" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="515" y="297" font-size="12">316 B</text>
<text x="242" y="316" font-size="12" text-anchor="end">z_add_timeout</text>
<rect x="250.0" y="305.0" width="235.6" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="492" y="316" font-size="12">288 B</text>
<text x="242" y="335" font-size="12" text-anchor="end">z_impl_k_thread_suspend</text>
<rect x="250.0" y="324.0" width="196.4" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="452" y="335" font-size="12">240 B</text>
<text x="242" y="354" font-size="12" text-anchor="end">z_thread_prio_set</text>
<rect x="250.0" y="343.0" width="176.7" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="433" y="354" font-size="12">216 B</text>
<text x="242" y="373" font-size="12" text-anchor="end">sys_clock_announce_locked</text>
<rect x="250.0" y="362.0" width="150.5" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="407" y="373" font-size="12">184 B</text>
<text x="242" y="392" font-size="12" text-anchor="end">z_impl_k_yield</text>
<rect x="250.0" y="381.0" width="108.0" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="364" y="392" font-size="12">132 B</text>
<text x="242" y="411" font-size="12" text-anchor="end">ready_thread</text>
<rect x="250.0" y="400.0" width="104.7" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="361" y="411" font-size="12">128 B</text>
<text x="242" y="430" font-size="12" text-anchor="end">k_thread_state_str</text>
<rect x="250.0" y="419.0" width="101.5" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="357" y="430" font-size="12">124 B</text>
<text x="242" y="449" font-size="12" text-anchor="end">z_tick_sleep</text>
<rect x="250.0" y="438.0" width="98.2" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="354" y="449" font-size="12">120 B</text>
<text x="242" y="468" font-size="12" text-anchor="end">z_impl_k_sem_reset</text>
<rect x="250.0" y="457.0" width="88.4" height="14.0" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="344" y="468" font-size="12">108 B</text>
<text x="10" y="490" font-size="12">같은 축(0–440 B). 큰 함수 = 기능이 많은 함수이지 "먼저 읽을" 함수와 같지는 않다 (§6.4 목록 참고)</text>
</svg>
```
그림 8 — 측정: 함수 크기 상위 10. FreeRTOS(-O2)와 Zephyr(-Os)는 최적화 수준이 달라 절대 비교는 의미가 없고, 각 커널 안의 순위만 본다.

### 6.2 예제 14 — 분기 수: 원본 vs 이 설정으로 전처리한 소스

무엇을 확인하는 코드인지: 전처리 출력(`-P` 없이, 줄 표지 유지)을 토큰으로 쪼개 함수별 분기 키워드(`if` `for` `while` `case` `&&` `||` `?`)를 센다. 줄 표지를 따라가며 **해당 원본 파일에서 온 함수만** 센다. 같은 스크립트를 원본 `.c`에 돌리면 모든 `#if` 가지를 다 센 값이 나온다.

```python
#!/usr/bin/env python3
"""cx.py <file.i> <원본 파일 이름 조각> [N] — 전처리된(.i, -P 없이) 소스에서 함수별 분기 수(≈McCabe) 순위.
설정(#if)이 이미 적용된 코드만 센다. 분기 = if/for/while/case/&&/||/? 의 개수, 복잡도 = 1 + 분기."""
import re, sys
path, want = sys.argv[1], sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 15
cur = ''; toks = []                    # (token, file)
tokre = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|&&|\|\||[{}();?]|\S')
for line in open(path, errors='replace'):
    m = re.match(r'#\s*(?:line\s+)?\d+\s+"([^"]+)"', line)
    if m: cur = m.group(1); continue
    if line.startswith('#'): continue
    for t in tokre.findall(line): toks.append((t, cur))
res = []; depth = 0; i = 0; hdr = []
while i < len(toks):
    t, f = toks[i]
    if depth == 0:
        if t == '{' and hdr and hdr[-1][0] == ')':
            k = len(hdr) - 1; par = 0
            while k >= 0:
                if hdr[k][0] == ')': par += 1
                elif hdr[k][0] == '(':
                    par -= 1
                    if par == 0: break
                k -= 1
            name = hdr[k - 1][0] if k > 0 else '?'
            hfile = hdr[k - 1][1] if k > 0 else ''
            j = i; d = 0; br = 0; nt = 0
            while j < len(toks):
                tj = toks[j][0]
                if tj == '{': d += 1
                elif tj == '}':
                    d -= 1
                    if d == 0: break
                elif tj in ('if', 'for', 'while', 'case', '&&', '||', '?'): br += 1
                nt += 1; j += 1
            if want in hfile and '=' not in [x[0] for x in hdr]:
                res.append((br + 1, nt, name))
            i = j + 1; hdr = []; continue
        if t in (';', '}'): hdr = []
        else: hdr.append((t, f))
    i += 1
res.sort(reverse=True)
print(f"{want}: {len(res)} functions, total complexity {sum(r[0] for r in res)}")
print(f"{'cplx':>5} {'tokens':>6}  function")
for c, nt, name in res[:N]: print(f"{c:5} {nt:6}  {name}")
```

실행 스크립트(`cx_run.sh`)는 FreeRTOS를 템플릿 플래그로 `gcc -E`하고, Zephyr `sched.c`는 빌드 폴더의 `compile_commands.json`에서 **실제 컴파일 명령을 꺼내 `-c`를 `-E`로 바꿔** 전처리한다(Zephyr는 `autoconf.h`와 수십 개 `-I`가 있어 손으로 플래그를 맞출 수 없다):

```python
for e in json.load(open(D + '/compile_commands.json')):
    if e['file'].endswith('kernel/sched.c'):
        a = shlex.split(e['command']); i = a.index('-o'); a[i + 1] = O + '/sched.i'
        a = [x for x in a if x != '-c'] + ['-E']
        subprocess.run(a, cwd=e['directory'], check=True)
```

```text
FreeRTOS-Kernel/tasks.c: 90 functions, total complexity 610
 cplx tokens  function
   25   1000  xTaskGenericNotifyFromISR
   23    845  xTaskGenericNotify
   23    818  vTaskPrioritySet
   22   1007  xTaskIncrementTick
   20    984  xTaskResumeAll
   16    823  vTaskGenericNotifyGiveFromISR
   16    482  vTaskSuspend
   16    465  vTaskDelete
   15    582  vTaskPriorityDisinheritAfterTimeout
   14    470  vTaskGetInfo
   14    400  vTaskSwitchContext
   14    333  xTaskDelayUntil
   13    329  eTaskGetState
   12    834  xTaskRemoveFromEventList
   12    653  vTaskRemoveFromUnorderedEventList

FreeRTOS-Kernel/queue.c: 48 functions, total complexity 347
 cplx tokens  function
   30    913  xQueueGenericSend
   25    597  xQueueGenericSendFromISR
   24    824  xQueueSemaphoreTake
   22    712  xQueueReceive
   21    696  xQueuePeek
   21    551  xQueueGiveFromISR
   16    444  xQueueReceiveFromISR
   14    353  prvNotifyQueueSetContainer
   13    262  xQueueGenericCreateStatic
   12    440  xQueueGenericReset
   11    315  prvUnlockQueue
   10    227  vQueueDelete
    9    247  xQueuePeekFromISR
    9    227  xQueueTakeMutexRecursive
    9    198  vQueueAddToRegistry

zephyr/kernel/sched.c: 65 functions, total complexity 317
 cplx tokens  function
   38   1117  z_impl_k_sleep
   35   1157  z_impl_k_usleep
   22    549  z_tick_sleep
   22    487  k_sched_lock
   20    452  k_sched_unlock
   13    809  z_sched_waitq_walk
   12    248  z_thread_prio_set
   12    203  halt_thread
    9    262  z_thread_halt
    9    206  z_impl_k_thread_join
    7    137  z_impl_k_thread_suspend
    5     63  z_impl_k_thread_priority_set
    5     42  ready_thread
    4     99  thread_halt_spin
    4     96  z_thread_abort
```

같은 스크립트를 원본 `.c`에 돌린 결과(상위 8개, 모든 `#if` 가지 포함):

```text
: 117 functions, total complexity 1010
 cplx tokens  function
   55   1813  prvSelectHighestPriorityTask
   49   1372  xTaskIncrementTick
   36   1153  vTaskPrioritySet
   35    685  prvYieldForTask
   27   1007  xTaskGenericNotifyFromISR
   26   1127  vTaskListTasks
   26    859  portTASK_FUNCTION
   26    821  xTaskResumeAll

: 52 functions, total complexity 391
 cplx tokens  function
   35   1212  xQueueGenericSend
   32   1202  xQueueSemaphoreTake
   26   1067  xQueueGiveFromISR
   25   1043  xQueueGenericSendFromISR
   25    825  xQueuePeek
   20    752  xQueueReceive
   19    547  prvUnlockQueue
   12    466  xQueueCRReceive

: 84 functions, total complexity 231
 cplx tokens  function
   24    575  next_up
   13    429  halt_thread
   10    304  z_thread_halt
    9    295  z_thread_prio_set
    8    356  z_get_next_switch_handle
    6    154  z_impl_k_thread_suspend
    5    184  z_impl_k_thread_join
    5    142  z_sched_waitq_walk
```

출력에서 볼 것:

- 원본 `tasks.c`의 1등 `prvSelectHighestPriorityTask`(55)는 **SMP 전용**이다. 이 설정의 전처리 결과에는 아예 없다. 원본 복잡도로 순서를 정하면 쓰지도 않는 함수부터 읽게 된다.
- `xTaskIncrementTick`: 원본 49 → 이 설정 22. SMP · tickless · tick hook 가지가 빠졌다.
- Zephyr `z_impl_k_sleep`: 원본 3 → 전처리 뒤 **38**. 전처리 출력을 보면 `if`는 0개, `?`가 31개다 — `k_ticks_to_ms_ceil64` 같은 시간 단위 변환 매크로가 상수 비교 `?:`로 펼쳐진 것이다(`((1000) == (100)) ? ...`). 컴파일러가 전부 상수로 접는다. 즉 **전처리 뒤 분기 수도 거짓말을 할 수 있다** — 그래서 셋째 지표(기계어)가 필요하다.
- 반대로 Zephyr 원본 1등 `next_up`(24)은 SMP · meta-IRQ 가지를 빼면 2다.

### 6.3 예제 15 — 기계어 조건 분기 수

무엇을 확인하는 코드인지: `objdump -d`에서 함수별 조건 분기 명령(`b<cond>`, `cbz`/`cbnz`, IT 블록)을 센다. 컴파일러가 상수를 접고 인라인한 **최종 결과**의 분기 수다.

```python
#!/usr/bin/env python3
"""mbranch.py <obj>... — objdump -d 결과에서 함수별 조건 분기 명령 수(b<cond>, cbz/cbnz, it 블록)를 센다."""
import re, subprocess, sys, collections
cnt = collections.Counter(); size = collections.Counter(); cur = None
cond = re.compile(r'\t(b(eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)(\.n|\.w)?|cbn?z|it[te]*)\s')
for obj in sys.argv[1:]:
    out = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn', obj], capture_output=True, text=True).stdout
    for line in out.split('\n'):
        m = re.match(r'^[0-9a-f]+ <([^>]+)>:$', line)
        if m: cur = m.group(1); continue
        if cur and re.match(r'^\s+[0-9a-f]+:\t', line):
            size[cur] += 1
            if cond.search(line): cnt[cur] += 1
for name in sorted(size, key=lambda n: -cnt[n])[:int(__import__('os').environ.get('N', '14'))]:
    print(f"{cnt[name]:4} cond-branches {size[name]:5} insns  {name}")
```

```sh
cd /var/tmp/rtos/C12; python3 mbranch.py obj/tasks.o obj/queue.o
mkdir -p zobj && cd zobj && arm-none-eabi-ar x ../z_tour_os/zephyr/kernel/libkernel.a sched.c.obj timeout.c.obj sem.c.obj thread.c.obj
N=10 python3 mbranch.py sched.c.obj timeout.c.obj sem.c.obj thread.c.obj
```

```text
  26 cond-branches   152 insns  xQueueGenericSend
  20 cond-branches   128 insns  xQueueReceive
  20 cond-branches   140 insns  xQueueSemaphoreTake
  20 cond-branches   125 insns  xQueuePeek
  18 cond-branches    87 insns  xQueueGenericSendFromISR
  16 cond-branches    98 insns  prvCallForEachTask
  15 cond-branches   103 insns  vTaskDelete
  15 cond-branches   125 insns  xTaskResumeAll
  14 cond-branches   135 insns  xTaskIncrementTick.part.0
  14 cond-branches   101 insns  vTaskPrioritySet
  14 cond-branches    73 insns  xQueueGiveFromISR
  14 cond-branches    75 insns  xQueueReceiveFromISR
  13 cond-branches    92 insns  vTaskGetInfo
  12 cond-branches    88 insns  vTaskSuspend
--- Zephyr -Os ---
  16 cond-branches   107 insns  z_thread_abort
  14 cond-branches    89 insns  z_thread_prio_set
  13 cond-branches    89 insns  z_impl_k_thread_suspend
  12 cond-branches    55 insns  ready_thread
  10 cond-branches    93 insns  z_add_timeout
   8 cond-branches    36 insns  unready_thread
   8 cond-branches    44 insns  z_sched_waitq_walk
   7 cond-branches    47 insns  move_current_to_end_of_prio_q
   7 cond-branches    54 insns  z_impl_k_yield
   7 cond-branches    45 insns  k_thread_state_str
```

(`--- Zephyr -Os ---` 줄은 두 명령 사이에 `echo`로 찍었다.) 몇 함수를 골라 같은 스크립트를 `N=400`으로 돌려 grep한 값:

```text
  14 cond-branches   135 insns  xTaskIncrementTick.part.0
  10 cond-branches    81 insns  vTaskSwitchContext
   7 cond-branches    91 insns  prvAddNewTaskToReadyList
   5 cond-branches    85 insns  xTaskRemoveFromEventList
   2 cond-branches    38 insns  xTaskCreate
   1 cond-branches    11 insns  xTaskIncrementTick
   1 cond-branches    19 insns  vTaskPlaceOnEventList
   3 cond-branches    15 insns  reschedule.isra.0
   3 cond-branches    41 insns  z_tick_sleep
   3 cond-branches    17 insns  z_impl_k_sleep
   1 cond-branches    22 insns  z_impl_k_wakeup
   0 cond-branches    22 insns  z_pend_curr
   0 cond-branches     2 insns  z_reschedule
```

출력에서 볼 것: `xTaskIncrementTick`(본체)은 명령 11개, 분기 1개다 — "스케줄러가 잠겨 있지 않으면 `.part.0`으로 간다"만 남은 껍데기다. `z_impl_k_sleep`은 기계어로는 분기 3개(원본과 같다). `z_reschedule`은 명령 2개 — `reschedule.isra.0`(인자 정리 최적화로 생긴 이름)로 꼬리 호출한다. `ready_thread`는 원본 3, 전처리 5, 기계어 12 — 인라인이 분기를 끌어들였다.

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">측정: 분기 수를 세는 세 가지 방법 — 원본 소스 · gcc -E 뒤 · 기계어 조건 분기</text>
<rect x="10.0" y="34.0" width="12.0" height="12.0" fill="#888" fill-opacity="0.75"/>
<text x="28" y="45" font-size="12">원본 .c (모든 #if 포함)</text>
<rect x="230.0" y="34.0" width="12.0" height="12.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="248" y="45" font-size="12">gcc -E 뒤 (이 설정)</text>
<rect x="450.0" y="34.0" width="12.0" height="12.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="468" y="45" font-size="12">objdump 조건 분기 명령</text>
<text x="202" y="80" font-size="12" text-anchor="end">xQueueGenericSend</text>
<rect x="210.0" y="62.0" width="280.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="495" y="71" font-size="12">35</text>
<rect x="210.0" y="74.0" width="240.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="455" y="83" font-size="12">30</text>
<rect x="210.0" y="86.0" width="208.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="423" y="95" font-size="12">26</text>
<text x="202" y="122" font-size="12" text-anchor="end">xQueueSemaphoreTake</text>
<rect x="210.0" y="104.0" width="256.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="471" y="113" font-size="12">32</text>
<rect x="210.0" y="116.0" width="192.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="407" y="125" font-size="12">24</text>
<rect x="210.0" y="128.0" width="160.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="375" y="137" font-size="12">20</text>
<text x="202" y="164" font-size="12" text-anchor="end">xQueueGenericSendFromISR</text>
<rect x="210.0" y="146.0" width="200.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="415" y="155" font-size="12">25</text>
<rect x="210.0" y="158.0" width="200.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="415" y="167" font-size="12">25</text>
<rect x="210.0" y="170.0" width="144.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="359" y="179" font-size="12">18</text>
<text x="202" y="206" font-size="12" text-anchor="end">xQueueReceive</text>
<rect x="210.0" y="188.0" width="160.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="375" y="197" font-size="12">20</text>
<rect x="210.0" y="200.0" width="176.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="391" y="209" font-size="12">22</text>
<rect x="210.0" y="212.0" width="160.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="375" y="221" font-size="12">20</text>
<text x="202" y="248" font-size="12" text-anchor="end">xTaskIncrementTick</text>
<rect x="210.0" y="230.0" width="392.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="607" y="239" font-size="12">49</text>
<rect x="210.0" y="242.0" width="176.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="391" y="251" font-size="12">22</text>
<rect x="210.0" y="254.0" width="120.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="335" y="263" font-size="12">15</text>
<text x="202" y="290" font-size="12" text-anchor="end">xTaskResumeAll</text>
<rect x="210.0" y="272.0" width="208.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="423" y="281" font-size="12">26</text>
<rect x="210.0" y="284.0" width="160.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="375" y="293" font-size="12">20</text>
<rect x="210.0" y="296.0" width="120.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="335" y="305" font-size="12">15</text>
<text x="202" y="332" font-size="12" text-anchor="end">xTaskRemoveFromEventList</text>
<rect x="210.0" y="314.0" width="128.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="343" y="323" font-size="12">16</text>
<rect x="210.0" y="326.0" width="96.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="311" y="335" font-size="12">12</text>
<rect x="210.0" y="338.0" width="40.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="255" y="347" font-size="12">5</text>
<text x="202" y="374" font-size="12" text-anchor="end">z_impl_k_sleep (Zephyr)</text>
<rect x="210.0" y="356.0" width="24.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="239" y="365" font-size="12">3</text>
<rect x="210.0" y="368.0" width="304.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="519" y="377" font-size="12">38</text>
<rect x="210.0" y="380.0" width="24.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="239" y="389" font-size="12">3</text>
<text x="202" y="416" font-size="12" text-anchor="end">z_tick_sleep (Zephyr)</text>
<rect x="210.0" y="398.0" width="32.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="247" y="407" font-size="12">4</text>
<rect x="210.0" y="410.0" width="176.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="391" y="419" font-size="12">22</text>
<rect x="210.0" y="422.0" width="24.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="239" y="431" font-size="12">3</text>
<text x="202" y="458" font-size="12" text-anchor="end">ready_thread (Zephyr)</text>
<rect x="210.0" y="440.0" width="24.0" height="10.0" fill="#888" fill-opacity="0.75"/>
<text x="239" y="449" font-size="12">3</text>
<rect x="210.0" y="452.0" width="40.0" height="10.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="255" y="461" font-size="12">5</text>
<rect x="210.0" y="464.0" width="96.0" height="10.0" fill="#e08a3c" fill-opacity="0.75"/>
<text x="311" y="473" font-size="12">12</text>
<line x1="210" y1="58" x2="210" y2="486" stroke="currentColor" stroke-width="1.3"/>
<text x="10" y="504" font-size="12">z_impl_k_sleep: 원본 3 → -E 뒤 38 (시간 변환 매크로의 ?:, 컴파일 때 상수로 접힘) → 기계어 3</text>
<text x="10" y="522" font-size="12">ready_thread: 인라인된 update_cache · next_up 때문에 기계어 분기가 원본보다 많다</text>
<text x="10" y="540" font-size="12">빌드: FreeRTOS -O2 tasks.o/queue.o · Zephyr -Os sched.c.obj (방법은 예제 14 · 15)</text>
</svg>
```
그림 9 — 측정: 같은 함수의 분기 수를 세 방법으로. 원본은 안 쓰는 가지까지 세고(xTaskIncrementTick 49), 전처리 뒤는 상수 매크로를 세고(z_impl_k_sleep 38), 기계어는 인라인을 센다(ready_thread 12). 읽을 순서는 셋을 같이 보고 정한다.

### 6.4 "먼저 읽을 15개" — FreeRTOS

크기 · 분기(§6.1~6.3) × 핫 패스 여부(§4)를 합쳐 고른 순서다. 위에서 아래로 읽는다. 각 함수 옆의 질문에 답할 수 있으면 다음으로 간다.

| 순서 | 함수 (파일:줄) | 왜 지금 | 읽고 나서 답할 질문 |
|---|---|---|---|
| 1 | `vListInsertEnd` · `vListInsert` · `uxListRemove` (list.c:105 · 139 · 217) | 모든 커널 연산의 바닥. 248줄 전부 | `pxIndex`는 왜 있나? 끝 표지 `xListEnd`의 값은? |
| 2 | `prvAddTaskToReadyList` (tasks.c:287, 매크로) | ready 리스트 + 비트맵 | 비트맵 비트는 언제 지우나(`taskRESET_READY_PRIORITY`)? |
| 3 | `taskSELECT_HIGHEST_PRIORITY_TASK` (tasks.c:238) | 스케줄링 결정 자체 | 같은 우선순위 round-robin은 어디서? |
| 4 | `vTaskSwitchContext` (tasks.c:5215) | PendSV가 부르는 유일한 C 함수 | 스케줄러 잠금 중이면 왜 결정을 미루나? |
| 5 | `xPortPendSVHandler` (port.c:455) | 컨텍스트 스위치 (C2) | BASEPRI를 왜 올렸다 내리나? |
| 6 | `xPortSysTickHandler` → `xTaskIncrementTick` (port.c:491 · tasks.c:4831) | tick 핫 패스 | `xNextTaskUnblockTime`이 없으면 비용은? |
| 7 | `prvAddCurrentTaskToDelayedList` (tasks.c:8685) | 모든 블로킹의 공통 출구 | `portMAX_DELAY`는 어느 리스트로? 오버플로는? |
| 8 | `vTaskPlaceOnEventList` · `xTaskRemoveFromEventList` (tasks.c:5402 · 5500) | 블록 · 깨우기의 짝 | 스케줄러 잠금 중에 깨면 어디로 가나? |
| 9 | `vTaskSuspendAll` · `xTaskResumeAll` (tasks.c:3890 · 4064) | 긴 커널 작업의 보호막 | Resume에서 `xPendingReadyList`와 밀린 tick은? |
| 10 | `xQueueGenericSend` (queue.c:949) | API 하나 끝까지의 대표 | for 루프는 왜 다시 도나? |
| 11 | `prvLockQueue` · `prvUnlockQueue` (queue.c:258 · 2499) | ISR과 블로킹 태스크의 경합 처리 | `cTxLock`이 올라간 동안 ISR이 넣은 데이터는? |
| 12 | `xQueueGenericSendFromISR` (queue.c:1167) | ISR 규칙의 실체 (E1) | 왜 직접 스위치하지 않나? |
| 13 | `xTaskCheckForTimeOut` (tasks.c:5693) | 타임아웃 남은 시간 계산 | 깨어났는데 시간이 남았으면? |
| 14 | `xTaskCreate` → `prvInitialiseNewTask` → `pxPortInitialiseStack` (tasks.c:1729 · port.c:182) | 가짜 프레임 (C3) | 태스크 함수가 return하면? |
| 15 | `vTaskStartScheduler` → `xPortStartScheduler` → `prvPortStartFirstTask` (tasks.c:3742 · port.c:268 · 247) | 부팅 → 첫 태스크 | 왜 SVC로 첫 태스크에 들어가나? |

### 6.5 Zephyr 판 10개

| 순서 | 함수 (파일:줄) | 읽고 나서 답할 질문 |
|---|---|---|
| 1 | `struct z_kernel` · `struct _thread_base` (include/zephyr/kernel_structs.h:205 · include/zephyr/kernel/thread.h:46) | `ready_q.cache`는 누가 쓰고 누가 읽나? |
| 2 | `queue_thread` · `runq_best` · `next_up` (sched.c:114 · 101 · 185) | `SCHED_SIMPLE`과 `SCHED_SCALABLE`은 어디서 갈리나? |
| 3 | `update_cache` · `should_preempt` (sched.c:294 · kthread.h:224) | 협력형(음수) 스레드는 언제 선점 안 되나? |
| 4 | `ready_thread` · `unready_thread` (sched.c:348 · 599) | 둘 다 왜 `update_cache`를 부르나? |
| 5 | `reschedule` · `need_swap` (sched.c:560 · 546) | ISR 안에서 부르면(`resched()`)? |
| 6 | `z_swap` → `z_swap_irqlock` → `arch_swap` (kswap.h · kernel_arch_func.h:87) | `USE_SWITCH` 판과 무엇이 다른가? |
| 7 | `z_arm_pendsv` (swap_helper.S:48) | C 함수를 하나도 안 부르는 이유는? |
| 8 | `z_impl_k_sem_give` · `z_impl_k_sem_take` (sem.c:95 · 132) → `z_pend_curr` (sched.c:691) | 블록과 깨우기가 어느 두 함수로 대칭인가? |
| 9 | `z_tick_sleep` · `z_add_timeout` (sched.c:1172 · timeout.c:88) | `+1 tick`은 왜 있나? |
| 10 | `sys_clock_announce_locked` · `z_arm_exc_exit` (timeout.c:223 · exc_exit.c:55) | tickless에서 announce는 몇 tick씩? |

### 6.6 예제 16 — 이 노트의 함수 이름이 실제로 있는지 검증

무엇을 확인하는 코드인지: 노트에 나온 FreeRTOS 56개 · Zephyr 48개 이름이 V11.3.2 / v4.4.2 소스에 **정의**(함수 정의, `#define`, 어셈블리 `SECTION_FUNC`, `FUNC_ALIAS`, `portTASK_FUNCTION`)로 있는지 확인한다. 블록 주석은 공백으로 바꿔 위치를 유지한 채 지우고, 함수 정의는 `.c`/`.S`를 먼저 찾는다(헤더의 MPU 래퍼 `#define`에 먼저 걸리지 않게).

```python
def check(root, files, names):
    def nocomment(t):   # 블록 주석을 공백으로 바꿔 위치·줄 번호를 그대로 유지
        t = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), t, flags=re.S)
        return t
    text = {f: nocomment(open(f'{root}/{f}', errors='replace').read()) for f in files}
    text = dict(sorted(text.items(), key=lambda kv: kv[0].endswith('.h')))   # .c/.S 먼저
    bad = 0
    for n in names:
        hit = None
        for f, t in text.items():
            pats = [rf'^[ \t]*#[ \t]*define[ \t]+{n}\b', rf'^[^;\n#]*\b{n}\s*\([^;{{}}]*?\)[ \t\w()]*\n?\s*\{{',
                    rf'SECTION_FUNC\(\s*\w+\s*,\s*{n}\s*\)', rf'FUNC_ALIAS\(\s*\w+\s*,\s*{n}\s*,', rf'portTASK_FUNCTION\(\s*{n}\b']
            for p in pats:
                for m in re.finditer(p, t, re.M):
                    if not re.match(r'\s*(return|if|while|for|\*|/\*|\(void\)|[A-Za-z_]\w*\s*=)', m.group(0)):
                        hit = f'{f}:{t[:m.start()].count(chr(10)) + 1}'; break
                if hit: break
            if hit: break
        if not hit: bad += 1
        print(f"  {'OK ' if hit else 'MISSING'} {n:34} {hit or ''}")
    return bad
```

(파일 목록과 이름 목록 부분은 길어서 뺐다. 전체 스크립트는 `~/.rtos_scratch/C12/verify_names.py`.)

```text
  OK  xTaskCreate                        tasks.c:1729
  OK  prvCreateTask                      tasks.c:1631
  OK  prvInitialiseNewTask               tasks.c:1804
  OK  pxPortInitialiseStack              portable/GCC/ARM_CM3/port.c:182
  OK  prvAddNewTaskToReadyList           tasks.c:2040
  ...
  OK  xQueueGenericSend                  queue.c:949
  OK  vTaskPlaceOnEventList              tasks.c:5402
  OK  xTaskRemoveFromEventList           tasks.c:5500
  OK  xPortSysTickHandler                portable/GCC/ARM_CM3/port.c:491
  OK  xTaskIncrementTick                 tasks.c:4831
  OK  xPortPendSVHandler                 portable/GCC/ARM_CM3/port.c:455
  OK  vTaskSwitchContext                 tasks.c:5215
  OK  portYIELD                          portable/GCC/ARM_CM3/portmacro.h:85
  OK  portYIELD_WITHIN_API               include/FreeRTOS.h:2724
  OK  taskCHECK_FOR_STACK_OVERFLOW       include/stack_macros.h:70
  ...
FreeRTOS: 56/56 found

  OK  z_impl_k_thread_create             kernel/thread.c:785
  OK  z_setup_new_thread                 kernel/thread.c:579
  OK  arch_new_thread                    arch/arm/core/cortex_m/thread.c:90
  OK  z_impl_k_sem_give                  kernel/sem.c:95
  OK  update_cache                       kernel/sched.c:294
  OK  z_swap                             kernel/include/kswap.h:191
  OK  arch_swap                          arch/arm/include/cortex_m/kernel_arch_func.h:87
  OK  z_arm_pendsv                       arch/arm/core/cortex_m/swap_helper.S:48
  OK  z_tick_sleep                       kernel/sched.c:1172
  OK  sys_clock_announce_locked          kernel/timeout.c:223
  OK  z_arm_exc_exit                     arch/arm/core/cortex_m/exc_exit.c:55
  ...
Zephyr: 48/48 found
```

(107줄 중 대표 줄만 남기고 `...`로 줄였다.) 출력에서 볼 것: 104개 이름 모두 정의가 있다. 이 검증 스크립트도 처음엔 틀렸다 — 정규식 `[^;]*`가 줄을 넘어가 주석 속 이름에 걸리거나, `^\s*#define`의 `\s`가 앞 줄 개행까지 먹어 줄 번호가 3줄 어긋났다. 이름 검증조차 "출력을 눈으로 대조"해야 믿을 수 있다. `z_swap`이 191줄로 나온 것은 `USE_SWITCH` 판이 파일에서 먼저 나오기 때문이다 — 이 빌드가 쓰는 판은 같은 파일 아래쪽이다(§5.2).

---

## 7. 읽기 체크리스트와 2주 읽기 계획

### 7.1 함수 하나를 읽을 때 묻는 8문항

1. 이 함수는 **어떤 전역 상태**(리스트, 카운터, `pxCurrentTCB` / `_kernel`)를 바꾸나?
2. **누가 부르나** — 태스크 문맥만? ISR에서도? (FromISR 판이 따로 있나? Zephyr라면 `arch_is_in_isr()` 검사가 있나?)
3. **어떤 보호** 아래에서 도나 — 인터럽트 마스크(`taskENTER_CRITICAL` / `k_spin_lock`), 스케줄러 잠금, 큐 잠금, 없음?
4. **어디서 블록하거나 양보**하나 — `portYIELD` / `z_swap`이 있는 줄은?
5. **이 설정에서 살아남는 가지**는? (`gcc -E`로 확인)
6. **실패 경로**는 무엇을 돌려주나 — `errQUEUE_FULL`, `-EAGAIN`, assert?
7. **실제로 도는지** 확인했나 — gdb 중단점 하나, backtrace 한 장.
8. **한 문장 요약**: "이 함수는 ___를 ___로 옮기고, ___면 스위치를 요청한다."

### 7.2 2주 계획

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah11" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">2주 읽기 계획 — 하루 60~90분, 매일 "읽기 → gdb로 한 번 밟기 → 콜 그래프 한 장"</text>
<rect x="10" y="60" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="55" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 1</text>
<text x="55" y="110" font-size="12" text-anchor="middle">list.c</text>
<text x="55" y="128" font-size="12" text-anchor="middle">전체</text>
<text x="55" y="156" font-size="12" text-anchor="middle">→ C1</text>
<rect x="105" y="60" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 2</text>
<text x="150" y="110" font-size="12" text-anchor="middle">tasks.c</text>
<text x="150" y="128" font-size="12" text-anchor="middle">전역·TCB</text>
<text x="150" y="156" font-size="12" text-anchor="middle">→ C1</text>
<rect x="200" y="60" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="245" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 3</text>
<text x="245" y="110" font-size="12" text-anchor="middle">xTaskCreate</text>
<text x="245" y="128" font-size="12" text-anchor="middle">경로</text>
<text x="245" y="156" font-size="12" text-anchor="middle">→ C3</text>
<rect x="295" y="60" width="90" height="110" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 4</text>
<text x="340" y="110" font-size="12" text-anchor="middle">port.c</text>
<text x="340" y="128" font-size="12" text-anchor="middle">PendSV</text>
<text x="340" y="156" font-size="12" text-anchor="middle">→ C2</text>
<rect x="390" y="60" width="90" height="110" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="435" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 5</text>
<text x="435" y="110" font-size="12" text-anchor="middle">tick ·</text>
<text x="435" y="128" font-size="12" text-anchor="middle">지연 리스트</text>
<text x="435" y="156" font-size="12" text-anchor="middle">→ C5 · C6</text>
<rect x="485" y="60" width="90" height="110" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 6</text>
<text x="530" y="110" font-size="12" text-anchor="middle">스케줄러</text>
<text x="530" y="128" font-size="12" text-anchor="middle">잠금</text>
<text x="530" y="156" font-size="12" text-anchor="middle">→ C7</text>
<rect x="580" y="60" width="90" height="110" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="625" y="82" font-size="13" text-anchor="middle" font-weight="bold">Day 7</text>
<text x="625" y="110" font-size="12" text-anchor="middle">Zephyr</text>
<text x="625" y="128" font-size="12" text-anchor="middle">sched.c</text>
<text x="625" y="156" font-size="12" text-anchor="middle">→ C4 · C8</text>
<rect x="10" y="210" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="55" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 8</text>
<text x="55" y="260" font-size="12" text-anchor="middle">queue</text>
<text x="55" y="278" font-size="12" text-anchor="middle">send/recv</text>
<text x="55" y="306" font-size="12" text-anchor="middle">→ D4</text>
<rect x="105" y="210" width="90" height="110" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="150" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 9</text>
<text x="150" y="260" font-size="12" text-anchor="middle">FromISR</text>
<text x="150" y="278" font-size="12" text-anchor="middle">경로</text>
<text x="150" y="306" font-size="12" text-anchor="middle">→ E1</text>
<rect x="200" y="210" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="245" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 10</text>
<text x="245" y="260" font-size="12" text-anchor="middle">세마포어 ·</text>
<text x="245" y="278" font-size="12" text-anchor="middle">뮤텍스</text>
<text x="245" y="306" font-size="12" text-anchor="middle">→ D1–D3</text>
<rect x="295" y="210" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 11</text>
<text x="340" y="260" font-size="12" text-anchor="middle">task</text>
<text x="340" y="278" font-size="12" text-anchor="middle">통지</text>
<text x="340" y="306" font-size="12" text-anchor="middle">→ D5</text>
<rect x="390" y="210" width="90" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="435" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 12</text>
<text x="435" y="260" font-size="12" text-anchor="middle">timers.c</text>
<text x="435" y="306" font-size="12" text-anchor="middle">→ D6</text>
<rect x="485" y="210" width="90" height="110" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="530" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 13</text>
<text x="530" y="260" font-size="12" text-anchor="middle">Zephyr</text>
<text x="530" y="278" font-size="12" text-anchor="middle">sem·msgq</text>
<text x="530" y="306" font-size="12" text-anchor="middle">→ D10</text>
<rect x="580" y="210" width="90" height="110" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="625" y="232" font-size="13" text-anchor="middle" font-weight="bold">Day 14</text>
<text x="625" y="260" font-size="12" text-anchor="middle">복습 ·</text>
<text x="625" y="278" font-size="12" text-anchor="middle">그래프</text>
<text x="625" y="306" font-size="12" text-anchor="middle">→ C12</text>
<text x="10" y="50" font-size="13" font-weight="bold">1주차: 모듈 C (스케줄러 · 스위치 · tick)</text>
<text x="10" y="200" font-size="13" font-weight="bold">2주차: 모듈 D/E (동기화 · IPC · ISR)</text>
<text x="10" y="352" font-size="12">각 칸 아래의 ID = 그날 같이 읽을 노트. 읽은 함수마다 §7의 체크리스트 8문항에 답을 적는다</text>
</svg>
```
그림 10 — 2주 읽기 계획. 하루 60~90분. 매일 끝에 그날 읽은 경로의 콜 그래프를 한 장 그린다(이 노트의 그림 4~7처럼, bt를 근거로).

| 날 | 읽을 소스 | 같이 읽을 노트 | 그날의 gdb 과제 |
|---|---|---|---|
| 1 | list.c 전체(248줄), list.h 매크로 | C1 | `vListInsert`에서 멈추고 리스트를 `p *pxList`로 따라가기 |
| 2 | tasks.c 전역(465~533), TCB 정의(377), `prvAddTaskToReadyList` | C1 | 예제 2의 `ptype /o`, `p/x uxTopReadyPriority` |
| 3 | `xTaskCreate` 경로, `prvInitialiseNewTask` | C3 | 경로 1 bt, 새 스택의 가짜 프레임 16워드를 `x/16wx` |
| 4 | port.c 전체(822줄) — PendSV · SVC · SysTick · 임계 구역 | C2 · B4 | 예제 5 objdump와 소스 대조 |
| 5 | `xTaskIncrementTick`, `prvAddCurrentTaskToDelayedList`, `vTaskDelay` | C5 · C6 | `xNextTaskUnblockTime` 값 변화를 tick마다 찍기 |
| 6 | `vTaskSuspendAll` · `xTaskResumeAll`, 임계 구역 매크로 | C7 | Resume 안에서 `xPendingReadyList` 비우는 순간 잡기 |
| 7 | Zephyr sched.c 앞 600줄 + kswap.h | C4 · C8 | 예제 11 경로 2 |
| 8 | `xQueueGenericSend` · `xQueueReceive` · `prvUnlockQueue` | D4 | 경로 2 bt + 큐 잠금 중 ISR give |
| 9 | FromISR 함수들, `vPortValidateInterruptPriority` | E1 · E2 | 우선순위 0x10 ISR에서 FromISR 호출 → assert 확인 |
| 10 | `xQueueSemaphoreTake`, `xTaskPriorityInherit` · `Disinherit` | D1 · D2 · D3 | 뮤텍스 상속 순간 `uxPriority` 변화 |
| 11 | task notification (`xTaskGenericNotify` 등) | D5 | 큐 대비 명령 수 비교(objdump) |
| 12 | timers.c (1345줄) | D6 | 타이머 데몬 태스크의 bt |
| 13 | Zephyr sem.c · msg_q.c · mutex.c | D10 | `z_pend_curr` 대칭 확인 |
| 14 | 복습: 그림 4~7을 안 보고 다시 그리기 | C12 | 내 그래프 vs 실제 bt 대조 |

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나 — 두 커널이 "결정"을 두는 자리

이 노트는 그 자체가 "커널 안" 투어였으니, 여기서는 투어 전체에서 나온 가장 중요한 한 가지만 정리한다: **스케줄링 결정이 어디서 일어나나.**

- FreeRTOS: 리스트를 바꾸는 쪽(`xTaskRemoveFromEventList`, `xTaskIncrementTick`)은 "더 높은 게 깼다"는 **표시**(`xYieldPendings[0]`, 반환값 pdTRUE)만 남기고 PENDSVSET을 켠다. **결정**(`taskSELECT_HIGHEST_PRIORITY_TASK`)은 PendSV 안의 `vTaskSwitchContext`에서 한다. 장점: ISR이 아무리 많은 태스크를 깨워도 결정은 한 번. 대가: PendSV 안에서 C 함수 호출과 런타임 통계 · 스택 검사가 돈다(예제 5에서 `bl vTaskSwitchContext`).
- Zephyr: 리스트를 바꾸는 순간 `update_cache`가 **결정까지 끝내서** `_kernel.ready_q.cache`에 넣는다. PendSV(`z_arm_pendsv`)와 ISR 출구(`z_arm_exc_exit`)는 `cache != current` 비교와 레지스터 교체만 한다. 장점: PendSV가 짧고 C 호출이 없다. ISR 출구에서 자동으로 스위치 여부를 안다. 대가: 깨울 때마다 `next_up` 계산(그리고 time slicing이 켜져 있으면 `z_reset_time_slice` → `z_add_timeout`, §5.4).

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah10" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">"다음에 누가 도나"를 언제 정하나 — 같은 ISR give 장면을 두 커널로</text>
<text x="10" y="64" font-size="13" font-weight="bold">FreeRTOS</text>
<rect x="100" y="44" width="130" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="165" y="64" font-size="12" text-anchor="middle">ISR: 리스트 이동</text>
<rect x="234" y="44" width="136" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="302" y="64" font-size="12" text-anchor="middle">xYieldPendings=1</text>
<rect x="374" y="44" width="150" height="32" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="449" y="64" font-size="12" text-anchor="middle">PendSV: 결정+교체</text>
<rect x="528" y="44" width="142" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="599" y="64" font-size="12" text-anchor="middle">새 태스크</text>
<text x="100" y="94" font-size="12">PendSV 안의 vTaskSwitchContext(tasks.c:5215)가 고른다</text>
<text x="10" y="160" font-size="13" font-weight="bold">Zephyr</text>
<rect x="100" y="140" width="176" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="188" y="160" font-size="12" text-anchor="middle">ISR: 리스트+cache 결정</text>
<rect x="280" y="140" width="110" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="335" y="160" font-size="12" text-anchor="middle">exc_exit 비교</text>
<rect x="394" y="140" width="130" height="32" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="459" y="160" font-size="12" text-anchor="middle">PendSV: 교체만</text>
<rect x="528" y="140" width="142" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="599" y="160" font-size="12" text-anchor="middle">새 스레드</text>
<text x="100" y="190" font-size="12">ready 순간 update_cache(sched.c:294)가 고르고 PendSV는 읽기만</text>
<line x1="100" y1="236" x2="670" y2="236" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="100" y="256" font-size="12">시간 →  (상자 폭은 개념적 길이, 측정값 아님)</text>
<text x="10" y="278" font-size="12">그래서 FreeRTOS PendSV에는 bl vTaskSwitchContext가 있고,</text>
<text x="10" y="296" font-size="12">Zephyr z_arm_pendsv에는 스케줄러 C 호출 없이 ready_q.cache 읽기만 있다</text>
</svg>
```
그림 11 — 같은 "ISR이 높은 우선순위 태스크를 깨움" 장면에서 두 커널이 결정을 내리는 시점. 상자 폭은 개념적 길이다(이 노트는 이 두 경로의 사이클을 따로 재지 않았다 — 같은 보드에서 잰 전환 비용은 A7 · C2).

이 차이를 알면 디버깅 질문이 바뀐다. FreeRTOS에서 "태스크가 안 깬다"면 `xYieldPendings`와 PENDSVSET이 켜졌는지(ISR의 `portYIELD_FROM_ISR` 누락이 1순위), Zephyr라면 `ready_q.cache`가 기대한 스레드인지(협력형 우선순위 · `k_sched_lock`이 1순위)를 본다. 깊은 설명은 C1(리스트 · 비트맵), C2(PendSV), C4(정책 · 협력형), C5(tick), C7(잠금), C8(syscall 래퍼 · `z_impl_`), E1(ISR 규칙)이 맡는다.

---

## 9. 실무에서 다시 보기

### 9.1 드론 비행 제어기 — "IMU 태스크가 가끔 1 tick 늦게 깬다"

예를 들어 드론 비행 제어기에서 IMU DMA 완료 ISR이 `xQueueSendFromISR`로 자세 제어 태스크(최고 우선순위)를 깨우는데, 스코프로 보면 가끔 1 ms 늦게 돈다. §4.5의 투어를 아는 사람은 바로 두 곳을 본다. (1) ISR 끝에 `portYIELD_FROM_ISR(woken)`이 있나 — 없으면 PENDSVSET이 안 켜져서 **다음 tick의 SysTick이 우연히 PendSV를 켤 때까지** 기다린다(최대 1 tick 지연, 증상과 정확히 일치). (2) `woken`을 여러 FromISR 호출에 걸쳐 OR로 모으나 — 두 번째 호출이 `pdFALSE`로 덮어쓰면 같은 증상. 검증은 gdb 한 줄이다: ISR의 마지막 줄에 중단점, `p woken`, `p/x *(unsigned*)0xE000ED04`.

### 9.2 로봇 관절 모터 컨트롤러 (Zephyr) — CAN ISR 뒤 제어 스레드가 안 도는 순간

예를 들어 Zephyr 기반 모터 컨트롤러에서 CAN 수신 ISR이 `k_sem_give`로 제어 스레드를 깨우는데, 가끔 다음 tick까지 안 돈다. §5.5를 읽었다면 `z_arm_exc_exit`의 비교(`ready_q.cache != current`)가 핵심임을 안다. **ISR 직전에 돌던 스레드가 협력형(음수 우선순위)이거나 `k_sched_lock()` 중이면** `should_preempt`가 false라서 cache가 바뀌지 않는다(예제 12에서 확인 — 더 높은 우선순위를 깨워도 비키지 않는다). gdb로 ISR 출구에서 `p _kernel.ready_q.cache->base.prio`와 `p _kernel.cpus[0].current->base.sched_locked`를 찍으면 10초 안에 갈린다.

### 9.3 벤더 SDK의 낯선 포트를 받았을 때

새 회사에서 받은 MCU SDK의 FreeRTOS가 `portable/GCC/ARM_CM7/r0p1`이거나, 칩 벤더가 손댄 `port.c`일 수 있다. 읽는 순서는 이 노트의 15개 목록 중 **포트 쪽 4개**(5 · 6 · 14 · 15)부터다. `nm`으로 핸들러 이름이 벡터 테이블과 연결됐는지(예제 5), `objdump`로 PendSV의 BASEPRI 값이 `configMAX_SYSCALL_INTERRUPT_PRIORITY`와 같은지, 그리고 `vPortValidateInterruptPriority`가 켜져 있는지를 본다. 실칩은 NVIC 우선순위 비트가 3~4개라서 QEMU(8비트)와 다르다는 점도 이때 확인한다(E2).

### 9.4 SSD 펌웨어 경험을 RTOS 말로

Don이 SSD에서 하던 "명령 경로 따라가기"는 이 노트의 단계 3과 같다. 차이는 SSD 실행부가 보통 **협력형 루프 + 이벤트 큐**라서 콜 스택이 끊기지 않는 반면, RTOS는 PendSV에서 스택이 바뀐다는 것뿐이다. 면접에서 "큰 낯선 코드베이스를 어떻게 읽나?"라는 질문이 나오면, SSD 경험(자료구조 → 한 명령의 경로 → JTAG로 확인)을 이 노트의 FreeRTOS 예로 번역해서 답하면 된다(§11 Q1).

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 소스 이름으로 중단점 (`break xPortPendSVHandler`) | "Function not defined" | `FreeRTOSConfig.h`의 `#define xPortPendSVHandler PendSV_Handler` | `nm`으로 ELF 이름 확인 후 그 이름으로 |
| `--gc-sections` 빌드의 bt를 그대로 믿음 | 낮은 주소 프레임 이름이 엉뚱함 (`xQueuePeek`) | 버려진 함수의 DWARF가 `low_pc = 0`으로 남음 | `addr2line` · `nm`으로 대조, 디버그용은 `--no-gc-sections` |
| bt 맨 밑 `PendSV_Handler`를 진짜 호출자로 읽음 | "태스크가 PendSV에서 불렸다"는 오해 | 가짜 LR(`prvTaskExitError`) + "주소 − 1" 조회 | 태스크 스택의 바닥으로 읽는다 |
| gdb 조건식에 함수 호출 (`uxQueueMessagesWaiting(q)`) | 타깃 비정상 종료, gdb Segfault | 정지 중 inferior call이 커널 상태를 건드림 | 구조체 필드 · 포인터 비교만 쓴다 |
| icount + gdb로 시간 측정 | tick 숫자가 수천씩 튐 | 정지 · 재개가 가상 시계와 어긋남 | 순서는 gdb, 시간은 gdb 없이 |
| SMP · MPU `#if` 가지까지 정독 | 하루를 쓰고도 핫 패스를 못 봄 | 설정과 무관한 코드 | `gcc -E` 출력으로 읽는다 |
| 원본 복잡도로 순서 결정 | SMP 전용 `prvSelectHighestPriorityTask`부터 읽음 | 모든 `#if`를 셈 | 전처리 · 기계어 지표와 같이 본다 |
| Zephyr API 이름으로 grep (`k_sem_give(`) | 정의가 안 나오고 호출만 수백 개 | 생성된 syscall 래퍼 | `z_impl_k_sem_give`로 grep |
| kswap.h의 첫 `z_swap`을 정답으로 읽음 | "Zephyr PendSV가 `do_swap`을 부른다"는 오해 | `CONFIG_USE_SWITCH` 판이 먼저 나옴 | `.config` 확인 후 맞는 `#else` 가지를 읽는다 |
| 정적 그래프만으로 결론 | `t->fn(t)` 다음을 모름, 예외 경로 누락 | 함수 포인터 · PendSV는 정적 도구 사각지대 | gdb bt로 한 번 밟는다 |

---

## 11. 면접에서 이렇게 말한다

**Q.** How do you approach reading an unfamiliar RTOS kernel or a large firmware codebase?

**A.** 자료구조부터 보고, 핫 패스 몇 개를 고르고, API 하나를 끝까지 따라간 뒤, 디버거로 실제 경로를 확인한다. 매크로가 많으면 실제 빌드 플래그로 전처리한 출력을 읽는다.

> I start with the data structures, not the functions — in FreeRTOS that's the TCB with its two list items, the per-priority ready lists with the priority bitmap, and the delayed lists. Then I pick the hot paths: the tick handler, PendSV, and the give/take wake-up path. Then I follow one API end to end, for example xQueueSend from the call all the way to the PendSV that switches tasks. For a macro-heavy codebase I read the preprocessed output with the exact build flags, because in FreeRTOS only about a third of tasks.c survives preprocessing for a given config. Finally I confirm the path with breakpoints and backtraces on QEMU or the target, because the static reading is sometimes wrong — function pointers and exception entry don't show up in a static call graph.

**꼬리 질문**: "How would you know which functions to read first?" → 크기(nm --size-sort), 분기 수(전처리 뒤 + 기계어), 핫 패스 여부를 합친다(§6).

**Q.** Walk me through what happens inside FreeRTOS when a task calls xQueueSend on a full queue.

**A.** 임계 구역에서 큐가 꽉 찬 걸 확인하고 타임아웃 기준을 기록한 뒤, 스케줄러를 잠그고 큐를 잠근 상태에서 자기 TCB를 큐의 send 대기 리스트(우선순위 순)와 지연 리스트에 동시에 건다. 잠금을 풀고 `portYIELD`로 PendSV를 요청하면 PendSV 안의 `vTaskSwitchContext`가 다음 태스크를 고른다. 깨어나면 루프 처음부터 다시 검사한다.

> xQueueSend maps to xQueueGenericSend. Inside a short critical section it sees the queue is full, records the timeout start, and leaves the critical section. Then it suspends the scheduler and locks the queue, so ISRs can still post but won't touch the event lists — they just bump a lock counter. It calls vTaskPlaceOnEventList, which inserts the task's event list item into the queue's xTasksWaitingToSend list in priority order, and moves its state list item from the ready list to the delayed list — so it's waiting on "what" and "until when" at the same time. It unlocks the queue, resumes the scheduler, and if that didn't already yield it calls taskYIELD_WITHIN_API, which just sets PENDSVSET. The actual decision happens in PendSV, in vTaskSwitchContext. When the task wakes — either a receiver removed it from the event list or the tick timed it out — it loops back and re-checks the queue, because a higher-priority task may have taken the slot first.

**꼬리 질문**: "Why lock the queue instead of just disabling interrupts for the whole thing?" → 대기 리스트 조작이 길어서 ISR 지연을 늘리지 않으려고. 큐 잠금 중 ISR 쓰기는 `cTxLock`로 세었다가 `prvUnlockQueue`가 처리한다.

**Q.** Why must you call portYIELD_FROM_ISR at the end of an ISR that used a FromISR API?

**A.** FromISR API는 대기 태스크를 ready로 옮기고 `xYieldPendings`와 woken 플래그만 세운다. PENDSVSET은 켜지 않는다. ISR 끝에서 `portYIELD_FROM_ISR(woken)`이 PENDSVSET을 켜야 ISR이 끝나자마자 PendSV가 꼬리 물기로 돈다. 빠뜨리면 다음 tick까지 늦는다.

> The FromISR call only moves the waiting task to the ready list and reports back through the woken flag — it never pends PendSV itself. I verified this in gdb: at the last line of the ISR, woken was 1 and xYieldPendings was set, but the PENDSVSET bit in ICSR was still 0. portYIELD_FROM_ISR is what sets PENDSVSET, so PendSV tail-chains right after the ISR returns. If you forget it, the woken task waits until something else pends PendSV — typically the next SysTick — so you get up to one tick of extra latency, which looks like random jitter on a scope.

**꼬리 질문**: "How does Zephyr handle the same thing?" → 모든 ISR 공통 출구 `z_arm_exc_exit`가 `ready_q.cache != current`를 비교해 자동으로 PENDSVSET.

**Q.** What's the difference between how FreeRTOS and Zephyr decide which thread runs next on Cortex-M?

**A.** FreeRTOS는 PendSV 안에서 `vTaskSwitchContext`가 비트맵 + CLZ로 결정한다. Zephyr는 스레드를 ready로 만들거나 빼는 순간 `update_cache`가 결정해서 `_kernel.ready_q.cache`에 넣어 두고, PendSV는 그 캐시를 current로 옮기기만 한다. 레지스터 저장 위치도 다르다(태스크 스택 vs 스레드 구조체).

> FreeRTOS defers the decision: list operations just mark that a yield is needed, and PendSV calls vTaskSwitchContext, which uses the ready-priority bitmap and count-leading-zeros to pick the next TCB. Zephyr decides eagerly: every time a thread becomes ready or unready, update_cache computes the next thread and stores it in _kernel.ready_q.cache. The PendSV handler, z_arm_pendsv, doesn't call any scheduler C code — it saves callee-saved registers into the outgoing thread struct, loads ready_q.cache into current, and restores. That's also why Zephyr can decide at every ISR exit, in z_arm_exc_exit, whether to pend PendSV just by comparing cache against current.

**Q.** You set a breakpoint and the backtrace shows a function your firmware never calls. What do you suspect?

**A.** `--gc-sections`로 버려진 함수의 DWARF가 주소 0 근처에 남아 gdb가 그 이름을 집은 것을 먼저 의심한다. `addr2line`과 `nm`으로 주소를 대조한다. 그 밖에 인라인 프레임, "리턴 주소 − 1" 조회로 앞 함수 이름이 나오는 경우, 최적화로 생긴 `.part.0` · `.isra.0` 이름도 확인한다.

> First suspicion is stale debug info from garbage-collected sections: the linker drops the unused function's code, but its DWARF entry stays with low_pc relocated to zero, so it covers low addresses and gdb picks it for a frame there. I hit exactly this — gdb labeled my task function as xQueuePeek. I cross-check with addr2line and nm, or link the debug build with --no-gc-sections. Other usual suspects are inlined frames, gdb's return-address-minus-one lookup landing in the previous function — the bottom PendSV_Handler frame on a FreeRTOS task stack is really the fake return address prvTaskExitError — and compiler-generated names like .part.0 or .isra.0.

**Q.** How would you find which kernel functions are worth reviewing or optimizing first?

**A.** 크기(nm), 분기 수(실제 설정으로 전처리한 소스와 기계어 둘 다), 그리고 핫 패스 여부(트레이스나 프로파일)를 함께 본다. 원본 소스의 복잡도는 쓰지 않는 `#if` 가지까지 세어서 속기 쉽다.

> I combine three signals: function size from nm --size-sort, branch count measured both on the preprocessed source for our config and on the generated machine code, and whether the function is on a hot path — from a trace or just by breakpoint counts. Raw source metrics are misleading in RTOS code: in FreeRTOS tasks.c the most complex function in the raw file is SMP-only and doesn't even exist in a single-core build, and in Zephyr a sleep function jumps from 3 to 38 branches after preprocessing because of constant time-conversion macros that the compiler folds away. The machine-code count tells you what actually executes.

---

## 12. 직접 해보기

1. **개념 — 두 고리**: FreeRTOS 태스크가 `xQueueReceive(q, &v, 100)`으로 블록된 동안 `xStateListItem`과 `xEventListItem`은 각각 어느 리스트에 있나? 100 tick 뒤 타임아웃으로 깰 때, 데이터가 와서 깰 때 각각 어떤 함수가 두 고리를 빼나?
   정답/힌트: 상태 고리 = `pxDelayedTaskList`(정렬), 이벤트 고리 = `xTasksWaitingToReceive`. 타임아웃이면 `xTaskIncrementTick`(4913 · 4919줄)이 둘 다, 데이터면 `xTaskRemoveFromEventList`(5525 · 5529줄)가 둘 다 뺀다.

2. **코드 — 경로 하나 더 찍기**: 예제 6에 `xSemaphoreTake` / `xSemaphoreGive`(뮤텍스)를 추가하고, `xTaskPriorityInherit`에 중단점을 걸어 bt를 찍은 뒤 그림 4 형식으로 콜 그래프를 그려라.
   정답/힌트: `xQueueSemaphoreTake`(queue.c:1659) 안에서 `xTaskPriorityInherit`이 불린다. 낮은 우선순위가 뮤텍스를 쥔 상태에서 높은 우선순위가 take해야 한다.

3. **도구 — `gcc -E`**: `vTaskSwitchContext`를 예제 4 스크립트로 전개하고, `taskSELECT_HIGHEST_PRIORITY_TASK`와 `taskCHECK_FOR_STACK_OVERFLOW`가 각각 몇 줄로 펼쳐지는지 세라. `configCHECK_FOR_STACK_OVERFLOW`를 1로 바꾸면(`EXTRA_CFLAGS`가 아니라 앱 폴더의 `FreeRTOSConfig.h` — 템플릿 값은 `#ifndef` 없이 고정) 무엇이 달라지나?
   정답/힌트: stack_macros.h:70~의 판들이 설정값에 따라 갈린다. 2는 스택 끝 16바이트 패턴 비교, 1은 스택 포인터 비교만.

4. **측정 — 분기 지표 비교**: `timers.c`를 예제 13 · 14 · 15로 측정해 "먼저 읽을 3개"를 고르고 이유를 한 줄씩 써라.
   정답/힌트: 이 설정의 전처리 기준 분기 수 상위는 `prvProcessReceivedCommands`(23) · `xTimerGenericCommandFromTask`(9) · `prvProcessTimerOrBlockTask`(7)였다(예제 14 스크립트로 확인). 핫 패스(데몬 태스크 루프)인 첫째와 셋째를 먼저 읽는다.

5. **Zephyr — 협력형 실험 확장**: 예제 12는 `main`이 협력형이면 더 높은 `hi`를 깨워도 안 비킨다는 것을 보였다. 이번엔 `main`은 선점형(5) 그대로 두고 `hi`만 `-2`로 바꾸면 어떻게 되나? 그리고 `main`을 5로 두되 give 앞뒤를 `k_sched_lock()` / `k_sched_unlock()`으로 감싸면? 각각 예제 11의 2a 정지점(`ready_q.cache prio`)으로 확인하라.
   정답/힌트: 첫째는 예제 10과 같은 순서(지금 스레드가 선점형이므로 `should_preempt` true). 둘째는 `sched_locked` 때문에 cache가 current로 남고, `k_sched_unlock`에서 스위치가 일어난다 — 직접 돌려서 확인할 것(이 노트에서는 실행하지 않았다).

6. **gdb 함정 재현**: 예제 8을 직접 돌려 `xQueuePeek` 유령을 본 뒤, `--no-gc-sections`로 다시 링크해 같은 bt가 `waiter`로 바뀌는 것을 확인하라. 그다음 DWARF에서 `low_pc : 0`인 함수를 모두 세어라.
   정답/힌트: `arm-none-eabi-objdump --dwarf=info app.elf | grep -c "DW_AT_low_pc      : 0$"` — 버려진 함수 수와 비슷하다.

7. **설계 — 결정 시점**: "ISR 하나가 태스크 다섯 개를 연달아 깨운다"는 상황에서 FreeRTOS와 Zephyr가 각각 몇 번 "다음 태스크 결정"을 하는지 소스 근거로 답하라.
   정답/힌트: FreeRTOS는 PendSV에서 1번(그 전엔 비교만). Zephyr는 깨울 때마다 `update_cache`가 돌아 5번(+ time slicing이면 `z_reset_time_slice`도).

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 핫 패스 (hot path) | 가장 자주 도는 코드 경로 | tick · PendSV · give/take 깨우기 — 커널 읽기의 2단계 |
| 매크로 미로 | FreeRTOS의 config/trace/port 매크로 층 | 한 소스로 수많은 설정 · CPU를 지원한 대가 |
| `gcc -E` | 전처리만 하는 컴파일러 옵션 | `#if` · 매크로를 걷어 낸 "이 설정의 정답 소스" |
| 줄 표지 (line marker) | `# 123 "file.c"` | `-E` 출력에서 원본 위치를 알려 줌. `-P`로 끔 |
| backtrace (bt) | 현재 호출 스택 | gdb가 스택 · 레지스터 · DWARF로 되짚은 호출 순서 |
| 인라인 프레임 | 주소 없이 표시되는 bt 프레임 | 호출자 안에 펼쳐진 함수. 같은 기계어 주소 |
| `<signal handler called>` | gdb의 예외 경계 표시 | Cortex-M 예외 진입 프레임 아래에 끼어든 문맥을 이어 붙임 |
| IPSR | 현재 예외 번호 (xPSR 하위 9비트) | 0 = Thread, 14 = PendSV, 15 = SysTick, 16+n = IRQ n |
| 정적 콜 그래프 | 실행 없이 얻은 호출 관계 | `gcc -fcallgraph-info` — 모든 가능한 호출, 함수 포인터는 모름 |
| 동적 콜 그래프 | 실행 중 실제 경로 | gdb bt — 실제로 지나간 하나 |
| DWARF | ELF 안의 디버그 정보 형식 | 함수 주소 범위 · 변수 위치. `--gc-sections` 후 유령이 남을 수 있음 |
| `.part.0` · `.isra.0` | GCC가 만든 함수 사본 이름 | 부분 인라인 분할 / 인자 정리 최적화 결과 |
| McCabe 복잡도 | 1 + 분기 수 | 경우의 수의 근사. 원본 · 전처리 · 기계어로 값이 다름 |
| 이벤트 리스트 | "무엇을 기다리나" 리스트 | 큐의 `xTasksWaitingToSend` 등. 우선순위 순 정렬 |
| `xYieldPendings` | 스위치 필요 표시 (FreeRTOS) | 깨운 쪽이 세우고 `vTaskSwitchContext`가 지움 |
| `ready_q.cache` | 미리 고른 다음 스레드 (Zephyr) | `update_cache`가 쓰고 PendSV · ISR 출구가 읽음 |
| syscall 래퍼 | 빌드 때 생성되는 Zephyr API 함수 | `k_sem_give` → `z_impl_k_sem_give` (userspace면 SVC 경유) |
| 델타 리스트 | 앞 노드와의 차이를 저장하는 타임아웃 리스트 | Zephyr `dticks` — 맨 앞만 줄이면 됨 |
| 트램펄린 | 진짜 진입 함수를 대신 부르는 작은 함수 | Zephyr `z_thread_entry` — return 처리 담당 |

---

## 14. 요약 & 체크리스트

커널 소스는 처음부터 읽지 않는다. **자료구조**(TCB의 두 고리, ready 리스트 + 비트맵, 지연 리스트 / `_kernel.ready_q`)를 먼저 펴고, **핫 패스**(tick, PendSV, 깨우기)를 읽고, **API 하나를 끝까지**(`xQueueSend` → `vTaskPlaceOnEventList` → `portYIELD` → PendSV → `vTaskSwitchContext`) 따라간 뒤, **gdb backtrace로 확인**한다. FreeRTOS의 매크로 미로는 실제 빌드 플래그의 `gcc -E`로 걷어 내면 `tasks.c` 8967줄이 3226줄로 준다. gdb의 bt는 강력하지만 `--gc-sections` 유령 DWARF, "주소 − 1"로 보이는 바닥의 `PendSV_Handler`, 인라인 프레임, icount 왜곡을 알고 읽어야 한다. 두 커널의 가장 큰 차이는 **결정 시점**이다 — FreeRTOS는 PendSV 안에서, Zephyr는 깨우는 순간 `update_cache`에서 정하고 PendSV는 캐시를 읽기만 한다. 읽을 순서는 크기 · 분기 수(원본 · 전처리 · 기계어) · 핫 패스를 같이 보고 정하며, 이 노트의 15개(FreeRTOS)와 10개(Zephyr) 목록과 2주 계획이 출발점이다.

- [ ] 커널을 읽는 네 단계와 각 단계의 도구를 말할 수 있다
- [ ] FreeRTOS TCB의 `pxTopOfStack`이 오프셋 0인 이유와 두 리스트 고리의 역할을 설명할 수 있다
- [ ] 매크로 다섯 가족 중 무시해도 되는 것과 반드시 따라가야 하는 것을 구분한다
- [ ] 실제 빌드 플래그로 `gcc -E`를 돌려 함수 하나를 전개해 볼 수 있다 (Zephyr는 `compile_commands.json`에서)
- [ ] QEMU `-S` + gdb로 중단점 · 조건 · `tbreak *($lr & ~1)`을 써서 경로를 찍을 수 있다
- [ ] `xQueueSend` 블로킹 경로를 함수 이름과 순서로 말할 수 있다 (임계 구역 → 스케줄러 잠금 → 큐 잠금 → 이벤트 · 지연 리스트 → PendSV)
- [ ] FromISR가 PENDSVSET을 켜지 않는다는 것을 gdb 출력으로 보였고, `portYIELD_FROM_ISR`의 역할을 설명할 수 있다
- [ ] Zephyr에서 `k_sem_give` → `z_impl_k_sem_give` → `ready_thread` → `update_cache` → `reschedule` → `arch_swap` → `z_arm_pendsv`를 그릴 수 있다
- [ ] `k_sleep(K_MSEC(5))`가 100 Hz tick에서 20 ms가 되는 이유를 소스 줄로 설명할 수 있다
- [ ] gdb bt의 네 가지 함정(유령 DWARF, 주소 − 1, 인라인, icount)을 알고 `addr2line` · `nm`으로 대조할 수 있다
- [ ] 크기 · 분기 수 세 지표로 "먼저 읽을 함수"를 골라 본 적이 있다
- [ ] 2주 계획의 Day 1을 시작했다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 — VM `/var/rtoslab/src/FreeRTOS-Kernel`: `tasks.c`, `queue.c`, `list.c`, `include/FreeRTOS.h`(trace · `mtCOVERAGE_TEST_MARKER` 기본값), `include/task.h`, `include/list.h`, `include/stack_macros.h`, `include/mpu_wrappers.h`, `portable/GCC/ARM_CM3/port.c` · `portmacro.h`. GitHub: [FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- FreeRTOS 공식 문서 — [freertos.org](https://www.freertos.org/)의 "Customization (FreeRTOSConfig.h)"와 "Running the RTOS on a ARM Cortex-M Core" 페이지 (페이지 주소는 2024년 사이트 개편 뒤 바뀌어 여기 적지 않는다)
- Zephyr v4.4.2 소스 — VM `/var/rtoslab/zephyrproject/zephyr`: `kernel/sched.c`, `kernel/thread.c`, `kernel/sem.c`, `kernel/timeout.c`, `kernel/timeslicing.c`, `kernel/include/kswap.h` · `kthread.h` · `ksched.h` · `timeout_q.h`, `arch/arm/core/cortex_m/swap_helper.S` · `thread.c` · `exc_exit.c` · `isr_wrapper.c`, `drivers/timer/cortex_m_systick.c`, `include/zephyr/drivers/timer/system_timer.h`. GitHub: [zephyrproject-rtos/zephyr](https://github.com/zephyrproject-rtos/zephyr)
- Zephyr 문서 — [Scheduling](https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html), [Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html), [Kernel Timing](https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html), [System Calls](https://docs.zephyrproject.org/latest/kernel/usermode/syscalls.html)
- GCC 문서 — [Preprocessor Options (`-E`, `-P`)](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html), [Developer Options (`-fcallgraph-info`)](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html)
- GDB 문서 — [Backtraces](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html), [ptype /o](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Symbols.html), [Break Conditions](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Conditions.html)
- Arm, "Armv7-M Architecture Reference Manual" (DDI 0403) — 예외 모델 · ICSR · IPSR · BASEPRI. Arm, "Cortex-M3 Technical Reference Manual"
- 이 노트의 스크립트와 로그 — Mac `~/.rtos_scratch/C12/` (`fr_tour/`, `z_tour/`, `*.gdb`, `rungdb.sh`, `expand.sh`, `census.py`, `cx.py`, `mbranch.py`, `verify_names.py`, `gen_svg.py`), VM `/var/tmp/rtos/C12/` (`fr_bt.txt`, `z_bt.txt`, `z_bt_full.txt`, `verify.txt`)
- 관련 노트 — A3(소스 트리 · footprint), B2(예외 진입), B4(PendSV · SysTick · SVC), C1 · C2 · C3 · C4 · C5 · C6 · C7 · C8, D1~D5 · D10, E1 · E2 · E7, M2(RTOS-aware 디버깅), M10(정적 분석 · 콜 그래프)
