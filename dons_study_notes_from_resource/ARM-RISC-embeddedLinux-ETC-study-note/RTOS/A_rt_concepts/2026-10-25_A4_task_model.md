# A4. 태스크 모델 — 함수 + 자기 스택 + TCB, 그리고 다섯 가지 상태

> **이 노트를 다 읽으면**: 태스크를 "함수 + 자기 스택 + TCB"로 정의하고 FreeRTOS `tskTCB`·Zephyr `struct k_thread`의 필드를 오프셋까지 설명할 수 있다 · Running/Ready/Blocked/Suspended/Deleted 전이를 API 이름과 커널 리스트로 말하고 실제 실행 trace로 보여 줄 수 있다 · 태스크 함수가 return하면 무슨 일이 생기는지, 스택 크기를 어떻게 재고(0xa5 high water mark) 넘치면 어떻게 잡히는지 안다 · 정적/동적 생성의 RAM 비용을 바이트 단위로 계산할 수 있다
> **왜 필요한가**: RTOS 면접은 거의 항상 "What is a task? What's in a TCB? What states can a task be in?"로 시작하고, 현장 버그 1순위(스택 overflow, 삭제된 태스크 메모리, 굶는 idle)는 전부 이 노트의 개념에서 나온다.
> **Don 기준 난이도**: 스택·레지스터 저장·링크드 리스트·heap은 이미 안다 / "TCB 첫 워드가 왜 SP인가", FreeRTOS가 상태를 **리스트 소속**으로 표현하는 방식, words vs bytes 함정, idle 태스크가 메모리를 회수한다는 사실을 새로 배운다
> **선행 노트**: A1(실시간 개념), A2(실행 모델의 진화), A3(RTOS 구성 요소). Linux 프로세스·스레드·`task_struct`는 EL A2, EL F1. 이어지는 노트: C1(커널 자료구조), C2(컨텍스트 스위치), C3(첫 태스크 시작), C4(스케줄링 정책), C9(idle 훅), F1(정적 할당), F3(스택 산정), F5(유저 모드 격리)

---

## 0. 큰 그림 — 태스크는 "자기 스택을 가진 main 루프"

Don이 SSD 컨트롤러 펌웨어에서 쓰던 구조를 떠올려 보자. 코어마다 `while (1)` main 루프가 하나 있고, 그 안에서 "executive"가 할 일 테이블(호스트 명령 처리, NAND 스케줄링, GC, 로그 flush …)을 돌아가며 부른다. 각 job은 **한 번 불리면 끝까지 달리고 return** 해야 한다. 중간에 "NAND 응답 올 때까지 기다리기"를 하고 싶으면, 기다리는 대신 상태 변수에 "다음에 여기서부터"를 적어 두고 return한 뒤, 다음 바퀴에 그 상태를 보고 이어서 한다 — 손으로 짠 상태 기계(state machine)다.

RTOS 태스크는 이 일을 **스택 하나를 더 주는 것**으로 해결한다. 태스크마다 자기 스택이 있으니, 함수 한가운데서 "기다릴게"라고 말하고(블로킹 호출) 멈춰도 지역 변수·호출 경로·레지스터가 그 스택에 그대로 남는다. 커널은 그 태스크의 SP 값만 TCB에 적어 두고 다른 태스크의 스택으로 갈아탄다. 나중에 돌아올 때는 SP를 되돌려 놓기만 하면 함수가 멈췄던 바로 그 줄부터 이어진다. 그래서 태스크 코드는 상태 기계 대신 **평범한 순차 코드 + 무한 루프**로 쓸 수 있다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="130" y="22" text-anchor="middle" font-size="14">bare-metal super-loop</text>
  <rect x="30" y="38" width="200" height="140" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="44" y="62" font-size="12">main() {</text>
  <text x="44" y="82" font-size="12">  for (;;) {</text>
  <text x="44" y="102" font-size="12">    job_host_cmd();</text>
  <text x="44" y="122" font-size="12">    job_nand_sched();</text>
  <text x="44" y="142" font-size="12">    job_gc_step();</text>
  <text x="44" y="162" font-size="12">  } }</text>
  <rect x="30" y="192" width="200" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="130" y="215" text-anchor="middle" font-size="13">스택 1개 (MSP)</text>
  <text x="130" y="252" text-anchor="middle" font-size="12">job은 끝까지 달리고 return</text>
  <text x="130" y="270" text-anchor="middle" font-size="12">기다림 = 손으로 짠 상태 기계</text>
  <text x="475" y="22" text-anchor="middle" font-size="14">RTOS: 태스크 = 함수 + 스택 + TCB</text>
  <rect x="285" y="38" width="120" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="61" text-anchor="middle" font-size="12">imu_task()</text>
  <rect x="285" y="80" width="120" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="110" text-anchor="middle" font-size="12">스택 1 KB</text>
  <rect x="285" y="136" width="120" height="32" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="157" text-anchor="middle" font-size="12">TCB</text>
  <rect x="415" y="38" width="120" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="61" text-anchor="middle" font-size="12">pid_task()</text>
  <rect x="415" y="80" width="120" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="110" text-anchor="middle" font-size="12">스택 2 KB</text>
  <rect x="415" y="136" width="120" height="32" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="157" text-anchor="middle" font-size="12">TCB</text>
  <rect x="545" y="38" width="120" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="61" text-anchor="middle" font-size="12">telemetry()</text>
  <rect x="545" y="80" width="120" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="110" text-anchor="middle" font-size="12">스택 1.5 KB</text>
  <rect x="545" y="136" width="120" height="32" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="157" text-anchor="middle" font-size="12">TCB</text>
  <rect x="285" y="210" width="380" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="475" y="232" text-anchor="middle" font-size="13">스케줄러: 우선순위로 TCB 하나 선택</text>
  <g stroke="currentColor" stroke-width="1.2">
    <line x1="345" y1="208" x2="345" y2="171" marker-end="url(#ah1)"/>
    <line x1="475" y1="208" x2="475" y2="171" marker-end="url(#ah1)"/>
    <line x1="605" y1="208" x2="605" y2="171" marker-end="url(#ah1)"/>
  </g>
  <text x="475" y="270" text-anchor="middle" font-size="12">세 태스크는 같은 주소 공간을 공유한다 (MPU 없으면 서로 보호 안 됨)</text>
</svg>
```

그림 1 — 왼쪽은 Don이 아는 SSD 펌웨어식 super-loop(스택 하나, job은 return해야 함), 오른쪽은 RTOS 태스크(태스크마다 함수·스택·TCB가 따로 있고, 스케줄러가 TCB 하나를 골라 그 스택으로 갈아탄다).

한 줄로 정의하면: **태스크 = 실행할 함수 + 그 함수만 쓰는 스택 + 커널이 그 태스크를 관리하려고 들고 있는 구조체(TCB, Task Control Block)**. 나머지 개념(상태, 우선순위, 블로킹, 컨텍스트 스위치)은 전부 이 셋 위에 얹힌다.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | 태스크·스레드·프로세스 — RTOS에서 달라지는 점 | — |
| §2 | TCB 해부: FreeRTOS `tskTCB`, 첫 워드 = SP, heap 배치, Zephyr `k_thread` | 예제 1, 2 |
| §3 | 다섯 상태와 전이: 모든 전이를 `eTaskGetState`로 찍고 간트 차트로 | 예제 3 |
| §4 | 태스크 함수의 모양: 무한 루프, `pvParameters`, return하면? | 예제 4, 5 |
| §5 | 태스크별 스택: words vs bytes, 0xa5 high water mark, overflow 훅 | 예제 6, 7 |
| §6 | 정적 vs 동적 생성: RAM 비용 측정, 스케줄러 시작 전 삭제 함정 | 예제 8, 9 |
| §7 | 우선순위 맛보기 + `vTaskListTasks`/run-time stats | 예제 10 |
| §8 | Zephyr로 같은 일: `K_THREAD_DEFINE`, `k_thread_create`, 스레드 목록 | 예제 11 |
| §9 | RTOS 커널 안: `xTaskCreate` → … → ready list | — |
| §10 | 실무: 드론 FC 태스크 RAM 예산 | 예제 12 |

소스 파일 이름과 예제 번호는 다르다: 예제 1·2 = `ex1.c`(예제 1은 그 ELF를 gdb로 분석), 3 = `ex2/ex2.c`, 4 = `ex3.c`, 5 = `ex3b/`, 6 = `ex4.c`, 7 = `ex5.c`, 8 = `ex6.c`, 9 = `ex6b.c`, 10 = `ex7.c`, 11 = `zapp/`, 12 = `py/budget.py` (모두 `~/.rtos_scratch/A4/` 아래, 빌드 산출물은 VM `/var/tmp/rtos/A4/`).

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩 `/var/rtoslab`: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위의 **FreeRTOS V11.3.2**(ARM_CM3 포트, heap_4, 템플릿 `FreeRTOSConfig.h` 기본값: 1 kHz tick, 우선순위 8단계, `configCHECK_FOR_STACK_OVERFLOW 2`, run-time stats on)와 **Zephyr v4.4.2**(같은 보드 `mps2/an385`). 시간이 나오는 예제는 전부 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자)로 돌렸다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스를 모델링하지 않는다). 모든 예제는 `-Wall -Wextra`로 경고 0개다(§4.4의 "configASSERT를 끈" 빌드만 커널 소스에서 경고 2개가 나오는데, 그것도 그대로 보여 준다).

```sh
# 이 노트의 모든 FreeRTOS 예제는 이 두 줄짜리 스크립트로 빌드·실행했다 (~/.rtos_scratch/A4/run.sh)
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A4/$N.c OUT=/var/tmp/rtos/A4/$(basename $N) "$@" && \
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A4/$(basename $N)/app.elf; echo "exit=$?"
```

---

## 1. 태스크란 무엇인가 — task, thread, process

### 1.1 직관

태스크는 "CPU를 잠시 빌려 쓰는 독립된 실행 흐름"이다. 단일 코어 MCU에서 실제로 명령을 실행하는 흐름은 언제나 하나뿐이다. 여러 태스크가 "동시에" 도는 것처럼 보이는 이유는, 커널이 아주 빠르게 **현재 태스크의 레지스터를 그 태스크 스택에 저장 → 다음 태스크의 스택에서 레지스터를 복원**하기 때문이다(이 과정이 컨텍스트 스위치, C2). 이 저장·복원이 가능하려면 태스크마다 다음 세 가지가 필요하다.

1. **함수** — 태스크가 실행할 코드. FreeRTOS에서는 `void f(void *pvParameters)`, Zephyr에서는 `void f(void *p1, void *p2, void *p3)` 모양이다.
2. **스택** — 그 태스크의 지역 변수, 함수 호출 경로(복귀 주소), 그리고 멈춰 있는 동안의 레지스터 값이 저장되는 메모리.
3. **TCB** — 커널이 태스크를 관리하는 구조체. 최소한 "저장된 SP가 어디인가", "우선순위", "지금 어느 대기 줄에 서 있나"를 담는다.

### 1.2 RTOS에서 task, thread, process는 어떻게 다른가

Linux의 프로세스·스레드와 `task_struct`는 EL A2·EL F1에서 이미 정리했다. 여기서는 **RTOS 쪽에서 달라지는 점만** 본다.

| 항목 | FreeRTOS task | Zephyr thread | Linux thread (EL A2) | Linux process (EL A2) |
|---|---|---|---|---|
| 주소 공간 | 모든 태스크가 하나를 공유 | 모든 스레드가 공유 (userspace 켜면 memory domain으로 분리) | 프로세스 안에서 공유 | 프로세스마다 따로 (MMU) |
| 격리 | 없음 (FreeRTOS-MPU 포트면 restricted task) | MPU로 user thread 격리 가능 | 같은 프로세스끼리 없음 | page table로 강한 격리 |
| 제어 블록 | `TCB_t` (이 빌드 96 B) | `struct k_thread` (이 빌드 192 B) | `task_struct` (수 KB) | `task_struct` + `mm_struct` |
| 스택 | 생성할 때 크기 고정, 자동 확장 없음 | 빌드 시점 `K_THREAD_STACK_DEFINE` 크기 고정 | 가상 메모리, 필요할 때 물리 할당 | 같음 |
| 생성 비용 | `malloc` 두 번 + 초기화 (수 us) | 정적 객체 초기화 | 수십 us (EL A2 측정) | 수십~수백 us |
| 한 태스크가 메모리를 망가뜨리면 | 시스템 전체가 위험 | 같음 (userspace가 아니면) | 프로세스 전체 사망 | 그 프로세스만 사망 |

즉 RTOS의 "태스크"와 Zephyr의 "스레드"는 이름만 다르고 같은 것이다 — **Linux의 스레드와 가장 닮았다**(주소 공간 공유, 스택·레지스터만 각자). 차이는 MMU가 없다는 것: 다른 태스크의 전역 변수, 심지어 다른 태스크의 스택과 TCB까지 포인터 하나로 덮어쓸 수 있다. MPU로 영역을 나누는 방법은 F4, F5에서 다룬다. 이름 대응표(ThreadX thread, CMSIS-RTOS2 thread 등)는 A5에 있다.

### 1.3 흔한 오해

- "태스크마다 CPU 시간이 공평하게 나뉜다" — 아니다. RTOS 스케줄러는 **우선순위 고정 선점**이 기본이다. 높은 우선순위 태스크가 Ready인 한 낮은 태스크는 영원히 못 돈다(§7, C4).
- "태스크를 만들면 바로 돈다" — 스케줄러 시작 전이면 Ready 리스트에 들어갈 뿐이다. 시작 후라도 새 태스크 우선순위가 현재 태스크보다 높을 때만 즉시 돈다(§9).
- "태스크 = 프로세스" — 면접에서 이렇게 말하면 감점이다. 보호된 주소 공간이 없으니 프로세스가 아니다.

---

## 2. TCB 해부 — FreeRTOS `tskTCB`와 Zephyr `struct k_thread`

### 2.1 소스에서 본 `tskTCB`

FreeRTOS의 TCB는 `tasks.c` 안에 `typedef struct tskTaskControlBlock { … } tskTCB; typedef tskTCB TCB_t;`로 정의되어 있다(파일 밖에서는 보이지 않는다 — 애플리케이션은 `TaskHandle_t`라는 불투명 포인터만 받는다). V11.3.2 소스에서 필드를 그대로 옮기면(조건부 필드는 `#if` 조건과 함께):

| 필드 | 조건 | 뜻 |
|---|---|---|
| `volatile StackType_t * pxTopOfStack` | 항상, **반드시 첫 멤버** | 이 태스크가 마지막으로 저장한 SP. 스위치 때 여기에 쓰고 여기서 읽는다 |
| `xMPU_SETTINGS xMPUSettings` | `portUSING_MPU_WRAPPERS == 1` | MPU 포트에서만, 반드시 두 번째 멤버 |
| `ListItem_t xStateListItem` | 항상 | 이 항목이 꽂힌 리스트 = 태스크 상태 (Ready/Blocked/Suspended) |
| `ListItem_t xEventListItem` | 항상 | 큐·세마포어 같은 객체의 "기다리는 태스크" 리스트에 꽂히는 항목 |
| `UBaseType_t uxPriority` | 항상 | 현재 우선순위 (0이 가장 낮다) |
| `StackType_t * pxStack` | 항상 | 스택 메모리의 시작(가장 낮은 주소) |
| `char pcTaskName[configMAX_TASK_NAME_LEN]` | 항상 | 디버깅용 이름 (이 빌드 16바이트) |
| `StackType_t * pxEndOfStack` | 스택이 위로 자라는 포트 또는 `configRECORD_STACK_HIGH_ADDRESS` | 스택의 반대쪽 끝 |
| `UBaseType_t uxTCBNumber, uxTaskNumber` | `configUSE_TRACE_FACILITY` | 디버거·trace 도구용 일련번호 |
| `UBaseType_t uxBasePriority, uxMutexesHeld` | `configUSE_MUTEXES` | 우선순위 상속 전 원래 우선순위, 쥐고 있는 뮤텍스 수 (D3) |
| `configRUN_TIME_COUNTER_TYPE ulRunTimeCounter` | `configGENERATE_RUN_TIME_STATS` | Running 상태로 보낸 누적 시간 (§7) |
| `volatile uint32_t ulNotifiedValue[]`, `uint8_t ucNotifyState[]` | `configUSE_TASK_NOTIFICATIONS` | 태스크 알림 값과 대기 상태 (D5) |
| `uint8_t ucStaticallyAllocated` | 정적·동적 둘 다 가능할 때 | 삭제 때 `vPortFree`를 할지 결정 (§6) |
| `uint8_t ucDelayAborted` | `INCLUDE_xTaskAbortDelay` | `xTaskAbortDelay`로 깨워졌는지 |

SMP(`configNUMBER_OF_CORES > 1`)에서는 `xTaskRunState`, `uxCoreAffinityMask` 같은 필드가 더 붙는다(C10). 소스 주석에 "The old naming convention is used to prevent breaking kernel aware debuggers"라고 적혀 있다 — `tskTCB`라는 옛 이름을 지키는 이유는 Ozone·SystemView 같은 RTOS-aware 디버거가 이 이름으로 구조체를 찾기 때문이다(M2).

### 2.2 예제 1 — gdb `ptype /o`로 실제 레이아웃 보기

**무엇을 확인하나**: 이 랩의 설정으로 컴파일된 TCB가 실제로 몇 바이트이고, 각 필드가 몇 번째 바이트에 있는지 — 디버그 정보(DWARF)에서 직접 읽는다. 예제 2의 ELF를 그대로 쓴다.

```sh
gdb -batch -ex "ptype /o TCB_t" /var/tmp/rtos/A4/ex1/app.elf
```

```text
type = struct tskTaskControlBlock {
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
```

**출력에서 볼 것**: TCB는 96바이트, 그중 40바이트가 리스트 항목 두 개(`ListItem_t` = `xItemValue`, `pxNext`, `pxPrevious`, `pvOwner`, `pvContainer` 다섯 워드 = 20 B)다. 설정을 바꾸면 크기가 바뀐다 — 예를 들어 `configUSE_MUTEXES 0`이면 8 B, trace facility를 끄면 8 B가 빠진다. 그래서 FreeRTOS는 애플리케이션이 정적 할당용 메모리 크기를 알 수 있도록 같은 레이아웃의 "가짜" 구조체 `StaticTask_t`(`FreeRTOS.h`, 필드 이름이 `pxDummy1`, `xDummy3[2]` …)를 공개하고, `xTaskCreateStatic` 경로(`prvCreateStaticTask()`)에서 `xSize = sizeof( StaticTask_t )`를 구해 `configASSERT( xSize == sizeof( TCB_t ) )`로 둘이 같은지 확인한다.

### 2.3 예제 2 — 핸들의 첫 워드 = SP, 그리고 위조된 초기 프레임

**무엇을 확인하나**: `TaskHandle_t`는 TCB 주소 그 자체이고, 그 첫 워드는 스택 안을 가리키며, 갓 만든 태스크의 스택 꼭대기에는 "이 태스크가 예외에서 복귀하는 중"인 것처럼 꾸민 레지스터 16개가 이미 쌓여 있다는 것. 스케줄러는 시작하지 않고 `main`에서 바로 메모리를 들여다본다.

```c
/* ex1: TCB 첫 워드 = pxTopOfStack, 그리고 새 태스크 스택에 위조된 초기 프레임 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void worker(void *pv) { for (;;) { (void)pv; vTaskDelay(1); } }

static const char *frame_name[16] = {
    "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11",
    "r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr" };

int main(void)
{
    TaskHandle_t h;
    static uint32_t param = 0x1234abcd;
    xTaskCreate(worker, "worker", 128, &param, 3, &h);   /* 128 words = 512 B */

    uint32_t *tcb = (uint32_t *)h;
    board_printf_raw("sizeof(StaticTask_t) = %u bytes\n", (unsigned)sizeof(StaticTask_t));
    board_printf_raw("handle (TCB addr)    = 0x%x\n", (unsigned)tcb);
    board_printf_raw("TCB word[0]          = 0x%x  <- pxTopOfStack\n", tcb[0]);
    board_printf_raw("TCB word[2] pxNext   = 0x%x, word[4] pvOwner = 0x%x, word[5] pvContainer = 0x%x\n",
                     tcb[2], tcb[4], tcb[5]);
    board_printf_raw("TCB word[11] prio    = %u\n", tcb[11]);
    board_printf_raw("TCB word[12] pxStack = 0x%x\n", tcb[12]);
    board_printf_raw("TCB word[13..] name  = %s\n", (char *)&tcb[13]);
    board_printf_raw("stack bytes used by fake frame = %u (= %u words)\n",
                     (unsigned)(tcb[12] + 128 * 4 - tcb[0]), (unsigned)((tcb[12] + 128 * 4 - tcb[0]) / 4));

    uint32_t *sp = (uint32_t *)tcb[0];
    for (int i = 0; i < 16; i++)
        board_printf_raw("  [sp+%d] 0x%x %s = 0x%x\n", i * 4, (unsigned)&sp[i], frame_name[i], sp[i]);
    board_printf_raw("  (word just above frame) 0x%x = 0x%x\n", (unsigned)&sp[16], sp[16]);
    board_printf_raw("&param = 0x%x, worker = 0x%x\n", (unsigned)&param, (unsigned)worker);
    board_printf_raw("bottom of stack pxStack[0..1] = 0x%x 0x%x\n",
                     ((uint32_t *)tcb[12])[0], ((uint32_t *)tcb[12])[1]);
    board_exit(0);
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   8496	    104	 131752	 140352	  22440	/var/tmp/rtos/A4/ex1/app.elf
sizeof(StaticTask_t) = 96 bytes
handle (TCB addr)    = 0x200003d8
TCB word[0]          = 0x20000388  <- pxTopOfStack
TCB word[2] pxNext   = 0x20000148, word[4] pvOwner = 0x200003d8, word[5] pvContainer = 0x20000140
TCB word[11] prio    = 3
TCB word[12] pxStack = 0x200001d0
TCB word[13..] name  = worker
stack bytes used by fake frame = 72 (= 18 words)
  [sp+0] 0x20000388 r4 = 0xa5a5a5a5
  [sp+4] 0x2000038c r5 = 0xa5a5a5a5
  [sp+8] 0x20000390 r6 = 0xa5a5a5a5
  [sp+12] 0x20000394 r7 = 0xa5a5a5a5
  [sp+16] 0x20000398 r8 = 0xa5a5a5a5
  [sp+20] 0x2000039c r9 = 0xa5a5a5a5
  [sp+24] 0x200003a0 r10 = 0xa5a5a5a5
  [sp+28] 0x200003a4 r11 = 0xa5a5a5a5
  [sp+32] 0x200003a8 r0 = 0x20000000
  [sp+36] 0x200003ac r1 = 0xa5a5a5a5
  [sp+40] 0x200003b0 r2 = 0xa5a5a5a5
  [sp+44] 0x200003b4 r3 = 0xa5a5a5a5
  [sp+48] 0x200003b8 r12 = 0xa5a5a5a5
  [sp+52] 0x200003bc lr = 0x1025
  [sp+56] 0x200003c0 pc = 0xc0
  [sp+60] 0x200003c4 xpsr = 0x1000000
  (word just above frame) 0x200003c8 = 0xa5a5a5a5
&param = 0x20000000, worker = 0xc1
bottom of stack pxStack[0..1] = 0xa5a5a5a5 0xa5a5a5a5
[board_exit] code=0
exit=0
```

같은 ELF의 심볼과 맞춰 보면(분석 명령도 실제로 돌렸다):

```sh
arm-none-eabi-nm -S /var/tmp/rtos/A4/ex1/app.elf | grep -E "pxReadyTasksLists|prvTaskExitError| worker|ucHeap"
```

```text
00001024 00000048 t prvTaskExitError
20000104 000000a0 b pxReadyTasksLists
200001c4 00020000 b ucHeap
000000c0 0000000a t worker
```

**출력에서 볼 것**:

- `pvOwner`(word 4) = `0x200003d8` = TCB 자기 자신. 리스트 항목이 "나를 품은 TCB"를 가리킨다 — 리스트를 따라가다 항목을 만나면 `pvOwner`로 TCB를 바로 얻는다.
- `pvContainer`(word 5) = `0x20000140` = `pxReadyTasksLists`(0x20000104) + 3 × 20 = **`pxReadyTasksLists[3]`**. 우선순위 3짜리 태스크가 우선순위 3 Ready 리스트에 꽂혀 있다. `pxNext` = 0x20000148은 그 리스트의 끝 표지(`xListEnd`, 리스트 시작 + 8)다 — 항목이 하나뿐이니 앞뒤가 다 끝 표지다.
- 스택 꼭대기 16워드가 **Cortex-M 예외 프레임을 흉내 낸 것**이다. 아래쪽 8워드(R4–R11)는 소프트웨어(PendSV)가 저장·복원하는 몫, 위쪽 8워드(R0–R3, R12, LR, PC, xPSR)는 하드웨어가 예외 복귀 때 자동으로 꺼내는 몫이다(B2). 값을 보면:
- `pc = 0xc0` = `worker`(심볼 0xc1에서 Thumb 비트를 뗀 값, `portSTART_ADDRESS_MASK`). 첫 복귀 때 CPU가 여기로 점프한다.
- `r0 = 0x20000000` = `&param`. C 호출 규약(AAPCS)에서 첫 인자는 R0 — 그래서 `pvParameters`가 함수 인자로 들어간다.
- `lr = 0x1025` = `prvTaskExitError`(0x1024 + Thumb 비트). 태스크 함수가 return하면 여기로 간다(§4.3).
- `xpsr = 0x01000000` = Thumb 상태 비트(T)만 켠 값. 이게 0이면 첫 복귀에서 바로 fault가 난다.
- 나머지 레지스터는 값을 쓰지 않고 칸만 비워 둬서 스택 채움 값 `0xa5a5a5a5`가 그대로 보인다.
- 프레임은 64 B(16워드)인데 "72 B 사용"으로 나온 것은 8바이트 정렬 때문이다: 스택 끝 `0x200003d0` 바로 아래 워드(`0x200003cc`)를 8 B 경계(`0x200003c8`)로 내리고(`portBYTE_ALIGNMENT_MASK`), 그다음 `pxTopOfStack--`로 한 칸 더 내려간 자리부터 프레임을 쓴다. 그래서 프레임 위에 0xa5 워드가 두 개 남는다.

이 프레임을 처음 꺼내는 과정(SVC로 첫 태스크 진입)은 C3, 평소의 저장·복원은 C2에서 한 줄씩 읽는다. 여기서는 "**새 태스크는 이미 한 번 멈췄다가 돌아오는 태스크처럼 꾸며져 있다**"만 기억하면 된다.

### 2.4 왜 `pxTopOfStack`은 반드시 첫 멤버인가

소스 주석이 대문자로 "THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT"라고 적어 둔 이유는 포트의 어셈블리 코드에 있다. ARM_CM3 포트의 PendSV 핸들러(`portable/GCC/ARM_CM3/port.c`의 `xPortPendSVHandler`)에서 저장·복원 부분만 뽑으면:

```asm
    mrs r0, psp                 @ 지금 태스크의 SP (하드웨어가 8워드를 이미 쌓았다)
    ldr r3, =pxCurrentTCB
    ldr r2, [r3]                @ r2 = 현재 TCB 주소
    stmdb r0!, {r4-r11}         @ 나머지 8워드를 태스크 스택에 쌓고
    str r0, [r2]                @ TCB의 "오프셋 0"에 새 SP 저장  <- 첫 멤버라서 [r2]
    ...
    bl vTaskSwitchContext       @ pxCurrentTCB를 다음 태스크로 바꾼다
    ...
    ldr r1, [r3]                @ r1 = 새 TCB 주소
    ldr r0, [r1]                @ 오프셋 0 = 새 태스크의 SP
    ldmia r0!, {r4-r11}
    msr psp, r0
    bx r14                      @ 예외 복귀 → 하드웨어가 나머지 8워드를 꺼낸다
```

`str r0, [r2]`와 `ldr r0, [r1]` — 오프셋 없이 TCB 주소 자체에서 읽고 쓴다. 첫 멤버가 아니면 `[r2, #offset]`으로 써야 하고, 그 오프셋은 설정(`#if`)마다 달라지니 어셈블리가 C 구조체 정의를 따라가야 한다. 첫 멤버로 고정하면 **어떤 설정에서도 어셈블리가 바뀌지 않는다**. 포트가 수십 개(Cortex-M, RISC-V, Xtensa …)인 FreeRTOS에서는 이 규칙 하나로 모든 포트 어셈블리가 TCB 레이아웃과 독립이 된다. MPU 포트가 `xMPUSettings`를 "반드시 두 번째"로 두는 것도 같은 이유(포트 어셈블리가 고정 오프셋 4로 읽는다)다.

```svg
<svg viewBox="0 0 680 335" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="80" y="30" text-anchor="middle" font-size="13">pxReadyTasksLists[ ]</text>
  <rect x="15" y="40" width="130" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="59" text-anchor="middle" font-size="12">[4..7]</text>
  <rect x="15" y="72" width="130" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="80" y="91" text-anchor="middle" font-size="12">[3] worker</text>
  <rect x="15" y="104" width="130" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="123" text-anchor="middle" font-size="12">[2]</text>
  <rect x="15" y="136" width="130" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="155" text-anchor="middle" font-size="12">[1]</text>
  <rect x="15" y="168" width="130" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="187" text-anchor="middle" font-size="12">[0] IDLE</text>
  <text x="15" y="228" font-size="12">이중 연결 리스트,</text>
  <text x="15" y="246" font-size="12">pvOwner → TCB</text>
  <text x="295" y="30" text-anchor="middle" font-size="13">TCB 96 B @0x200003d8</text>
  <rect x="200" y="40" width="190" height="24" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="210" y="57" font-size="12">+0 pxTopOfStack</text>
  <rect x="200" y="64" width="190" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="81" font-size="12">+4 xStateListItem</text>
  <rect x="200" y="88" width="190" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="105" font-size="12">+24 xEventListItem</text>
  <rect x="200" y="112" width="190" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="129" font-size="12">+44 uxPriority = 3</text>
  <rect x="200" y="136" width="190" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="153" font-size="12">+48 pxStack</text>
  <rect x="200" y="160" width="190" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="177" font-size="12">+52 pcTaskName</text>
  <rect x="200" y="184" width="190" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="201" font-size="12">+76 uxBasePriority</text>
  <rect x="200" y="208" width="190" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="225" font-size="12">+84 ulRunTimeCounter</text>
  <rect x="200" y="232" width="190" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="249" font-size="12">+88 ulNotifiedValue</text>
  <text x="570" y="30" text-anchor="middle" font-size="13">스택 512 B (위 = 높은 주소)</text>
  <rect x="470" y="40" width="200" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="57" font-size="12">0xa5 2워드 (8 B 정렬)</text>
  <rect x="470" y="64" width="200" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="81" font-size="12">xPSR = 0x01000000</text>
  <rect x="470" y="88" width="200" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="105" font-size="12">PC = worker</text>
  <rect x="470" y="112" width="200" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="129" font-size="12">LR = prvTaskExitError</text>
  <rect x="470" y="136" width="200" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="153" font-size="12">R12, R3, R2, R1</text>
  <rect x="470" y="160" width="200" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="177" font-size="12">R0 = pvParameters</text>
  <rect x="470" y="184" width="200" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="201" font-size="12">R11 ... R4</text>
  <rect x="470" y="208" width="200" height="50" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="480" y="237" font-size="12">0xa5a5a5a5 (아직 안 씀)</text>
  <rect x="470" y="258" width="200" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="275" font-size="12">pxStack[0] 0x200001d0</text>
  <g stroke="currentColor" stroke-width="1.2" fill="none">
    <line x1="147" y1="86" x2="198" y2="76" marker-end="url(#ah2)"/>
    <line x1="392" y1="52" x2="468" y2="206" marker-end="url(#ah2)"/>
    <line x1="392" y1="148" x2="468" y2="270" marker-end="url(#ah2)"/>
  </g>
  <text x="15" y="310" font-size="12">PendSV: ldr r1,[pxCurrentTCB] 다음 ldr r0,[r1] — 오프셋 0에서 SP를 바로 얻는다 (설정과 무관)</text>
</svg>
```

그림 2 — 예제 2의 실제 주소로 그린 TCB ↔ 스택 ↔ ready list. Ready 리스트의 항목은 TCB 안의 `xStateListItem`이고, TCB의 첫 워드는 스택 안 R4가 저장된 칸(현재 SP)을, `pxStack`은 스택의 가장 낮은 주소를 가리킨다.

### 2.5 TCB와 스택은 heap 안에서 어떻게 놓이나

예제 2의 주소를 다시 보자: 스택 `0x200001d0`~`0x200003d0`(512 B), TCB `0x200003d8`. 스택 끝과 TCB 사이 8바이트는 heap_4의 블록 헤더(`BlockLink_t`: 다음 빈 블록 포인터 + 크기)다. `prvCreateTask()`는 스택이 아래로 자라는 포트(`portSTACK_GROWTH < 0`, Cortex-M)에서 **스택을 먼저, TCB를 나중에** 할당한다. 소스 주석: "allocate the stack then the TCB so the stack does not grow into the TCB". heap_4는 낮은 주소부터 채우므로 TCB가 스택보다 높은 주소에 놓이고, 아래로 자라는 스택이 넘쳐도 **자기 TCB는 덮지 않는다**. 대신 그 아래쪽 이웃 블록(먼저 만든 다른 태스크의 TCB일 수 있다!)을 덮는다.

```svg
<svg viewBox="0 0 680 210" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker>
  <marker id="ah3r" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker></defs>
  <rect x="20" y="80" width="120" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="101" text-anchor="middle" font-size="12">이전 블록</text>
  <text x="80" y="119" text-anchor="middle" font-size="12">(다른 객체)</text>
  <rect x="140" y="80" width="30" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="155" y="110" text-anchor="middle" font-size="12">hdr</text>
  <rect x="170" y="80" width="300" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="320" y="101" text-anchor="middle" font-size="12">태스크 스택 512 B</text>
  <text x="320" y="119" text-anchor="middle" font-size="12">0x200001d0 ~ 0x200003d0</text>
  <rect x="470" y="80" width="30" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="485" y="110" text-anchor="middle" font-size="12">hdr</text>
  <rect x="500" y="80" width="120" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="560" y="101" text-anchor="middle" font-size="12">TCB 96 B</text>
  <text x="560" y="119" text-anchor="middle" font-size="12">0x200003d8</text>
  <line x1="460" y1="62" x2="190" y2="62" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <text x="325" y="52" text-anchor="middle" font-size="12">push할수록 SP가 낮은 주소로 내려간다</text>
  <line x1="175" y1="150" x2="40" y2="150" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#ah3r)"/>
  <text x="20" y="172" font-size="12">넘치면 hdr과 이전 블록을 덮는다 (예제 7, 예제 9)</text>
  <text x="20" y="200" font-size="12">낮은 주소</text>
  <text x="620" y="200" text-anchor="end" font-size="12">높은 주소</text>
</svg>
```

그림 3 — `xTaskCreate` 한 번이 heap_4에 만드는 두 블록. 스택 먼저, TCB 나중이라 overflow는 자기 TCB가 아니라 **아래쪽 이웃**을 망가뜨린다. 그래서 overflow 증상은 엉뚱한 태스크에서 나타난다.

### 2.6 Zephyr `struct k_thread`

Zephyr의 TCB는 `include/zephyr/kernel/thread.h`의 `struct k_thread`다. 앞부분 두 멤버가 핵심이다.

- `struct _thread_base base` — 스케줄러가 쓰는 공통 부분: ready/wait 큐 노드(`qnode_dlist` 또는 `qnode_rb`), `pended_on`(기다리는 wait queue), `prio`(int8_t)와 `sched_locked`, `thread_state`(상태 비트), timeout 노드.
- `struct _callee_saved callee_saved` — 아키텍처가 정의. Cortex-M(`include/zephyr/arch/arm/thread.h`)에서는 `v1`~`v8`(= R4–R11)와 `psp`.

그 뒤로 `init_data`, `join_queue`, (`CONFIG_THREAD_MONITOR`면) `entry`, `next_thread`, (`CONFIG_THREAD_NAME`이면) `name[]`, (`CONFIG_THREAD_STACK_INFO`면) `stack_info`, `resource_pool`, 그리고 "must always be at the end"라고 적힌 `arch`가 온다. §8 예제 11의 Zephyr 빌드에서 `ptype /o struct k_thread`를 찍으면 최상위 필드는 이렇다:

```sh
gdb -batch -ex "ptype /o struct k_thread" /var/tmp/rtos/A4/zbuild/zephyr/zephyr.elf | grep -E "^/\*[ 0-9|:]+\*/    [a-z_A-Z]"
```

```text
/*      0      |      64 */    struct _thread_base {
/*     64      |      36 */    struct _callee_saved {
/*    100      |       4 */    void *init_data;
/*    104      |       8 */    _wait_q_t join_queue;
/*    112      |      16 */    struct __thread_entry {
/*    128      |       4 */    struct k_thread *next_thread;
/*    132      |      32 */    char name[32];
/*    164      |       4 */    int errno_var;
/*    168      |      12 */    struct _thread_stack_info {
/*    180      |       4 */    struct k_heap *resource_pool;
/*    184      |       8 */    struct _thread_arch {
```

**FreeRTOS와 결정적으로 다른 점**: Zephyr Cortex-M은 R4–R11을 **태스크 스택이 아니라 TCB 안(`callee_saved`)에** 저장한다. `arch/arm/core/cortex_m/swap_helper.S`를 보면 `ldr r0, =_thread_offset_to_callee_saved` 다음 `stmia r0, {r4-r11, ip}`로 TCB에 직접 쓴다. 오프셋은 빌드 때 C 구조체에서 자동 생성되는 상수(`_thread_offset_to_callee_saved`, 이 빌드 64)라서 "첫 멤버" 규칙이 필요 없다. 대신 Zephyr TCB는 커진다(이 빌드 192 B — `name[32]`과 각종 옵션 필드 포함).

| 비교 | FreeRTOS `TCB_t` | Zephyr `struct k_thread` |
|---|---|---|
| 정의 위치 | `tasks.c` (비공개) + 공개용 `StaticTask_t` | `include/zephyr/kernel/thread.h` (공개) |
| 이 랩 빌드 크기 | 96 B | 192 B |
| 저장된 SP | `pxTopOfStack` (오프셋 0) | `callee_saved.psp` (오프셋 64+32) |
| R4–R11 저장 위치 | 태스크 스택 | TCB의 `callee_saved` |
| 상태 표현 | `xStateListItem`이 꽂힌 리스트 | `base.thread_state` 비트 |
| 우선순위 | `uxPriority` (클수록 높음) | `base.prio` (작을수록 높음, 음수 = 협력형) |
| 이름 | `pcTaskName[16]` | `name[CONFIG_THREAD_MAX_NAME_LEN]` (이 빌드 32) |

---

## 3. 태스크 상태와 전이

### 3.1 다섯 상태

FreeRTOS `task.h`의 `eTaskState`는 `eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid`다. 뜻을 bare-metal 말로 풀면:

- **Running** — 지금 CPU를 쥐고 명령을 실행 중. 단일 코어면 언제나 정확히 하나(아무도 할 일이 없으면 idle 태스크가 Running).
- **Ready** — 당장 돌 수 있는데 더 높은(또는 같은) 우선순위 태스크가 CPU를 쥐고 있어서 줄 서 있음.
- **Blocked** — 무언가를 기다리는 중: 시간(`vTaskDelay`), 큐 데이터, 세마포어, 알림. Don의 말로 "폴링 대신 스케줄러에게 '이 일이 생기면 깨워 달라'고 맡겨 둔 상태". CPU를 전혀 쓰지 않는다. 대부분 timeout이 있다.
- **Suspended** — 누가 `vTaskSuspend()`로 명시적으로 세워 둔 상태. timeout도 이벤트도 없다. `vTaskResume()`만이 꺼낸다.
- **Deleted** — `vTaskDelete()`로 지워졌지만 아직 메모리가 회수되지 않은 상태(자기 자신을 지운 경우, idle 태스크가 회수할 때까지).

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="30" width="150" height="46" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="58" text-anchor="middle" font-size="13">xTaskCreate()</text>
  <rect x="490" y="30" width="150" height="46" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="49" text-anchor="middle" font-size="13">Deleted</text>
  <text x="565" y="67" text-anchor="middle" font-size="12">idle이 메모리 회수</text>
  <rect x="40" y="150" width="150" height="46" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="178" text-anchor="middle" font-size="14">Ready</text>
  <rect x="490" y="150" width="150" height="46" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="178" text-anchor="middle" font-size="14">Running</text>
  <rect x="265" y="300" width="150" height="46" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="328" text-anchor="middle" font-size="14">Blocked</text>
  <rect x="40" y="300" width="150" height="46" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="328" text-anchor="middle" font-size="14">Suspended</text>
  <g stroke="currentColor" stroke-width="1.2" fill="none">
    <line x1="115" y1="78" x2="115" y2="146" marker-end="url(#ah4)"/>
    <line x1="192" y1="163" x2="486" y2="163" marker-end="url(#ah4)"/>
    <line x1="488" y1="184" x2="194" y2="184" marker-end="url(#ah4)"/>
    <line x1="540" y1="198" x2="402" y2="297" marker-end="url(#ah4)"/>
    <line x1="300" y1="298" x2="172" y2="200" marker-end="url(#ah4)"/>
    <line x1="263" y1="323" x2="194" y2="323" marker-end="url(#ah4)"/>
    <line x1="115" y1="298" x2="115" y2="200" marker-end="url(#ah4)"/>
    <line x1="565" y1="148" x2="565" y2="80" marker-end="url(#ah4)"/>
  </g>
  <text x="125" y="116" font-size="12">생성</text>
  <text x="340" y="155" text-anchor="middle" font-size="12">스케줄러가 고름 (최고 우선순위)</text>
  <text x="340" y="202" text-anchor="middle" font-size="12">선점 · taskYIELD · time slice</text>
  <text x="494" y="244" font-size="12">vTaskDelay, xQueueReceive,</text>
  <text x="494" y="260" font-size="12">xSemaphoreTake, 알림 대기</text>
  <text x="228" y="264" text-anchor="end" font-size="12">이벤트·timeout</text>
  <text x="108" y="252" text-anchor="end" font-size="12">vTaskResume()</text>
  <text x="228" y="364" text-anchor="middle" font-size="12">vTaskSuspend()</text>
  <text x="575" y="118" font-size="12">vTaskDelete</text>
  <text x="340" y="110" text-anchor="middle" font-size="12">vTaskSuspend()는 Ready · Running에서도 → Suspended</text>
</svg>
```

그림 4 — FreeRTOS 태스크 상태 전이도. 화살표 이름이 그 전이를 일으키는 API다. Ready ↔ Running은 스케줄러가, 나머지는 태스크 자신(또는 다른 태스크·ISR)의 API 호출이 일으킨다.

### 3.2 FreeRTOS에서 상태 = "어느 리스트에 꽂혀 있나"

FreeRTOS TCB에는 "상태" 필드가 없다. 대신 `xStateListItem`이 **어느 리스트에 들어 있느냐**가 곧 상태다. `eTaskGetState()`(tasks.c)의 구현이 정확히 그렇게 판정한다: 먼저 호출한 태스크 자신이면 `eRunning`, 아니면 `listLIST_ITEM_CONTAINER(&pxTCB->xStateListItem)`과 `xEventListItem`의 소속 리스트를 읽어서 비교한다.

| `xStateListItem`이 있는 리스트 (tasks.c 전역) | `eTaskGetState` 결과 | 비고 |
|---|---|---|
| `pxReadyTasksLists[prio]` | `eReady` (또는 호출자 자신이면 `eRunning`) | **Running 태스크도 Ready 리스트에 그대로 있다**. Running = `pxCurrentTCB`가 가리키는 태스크 |
| `xPendingReadyList` (`xEventListItem`이 여기) | `eReady` | 스케줄러가 잠긴 동안 깨어난 태스크 |
| `pxDelayedTaskList` / `pxOverflowDelayedTaskList` | `eBlocked` | timeout이 있는 대기 (tick 카운터 wrap 대비 리스트 2개, C5) |
| `xSuspendedTaskList` + `xEventListItem`이 어떤 큐에 있음 | `eBlocked` | **timeout = `portMAX_DELAY`인 대기**는 여기로 간다 |
| `xSuspendedTaskList` + 알림 대기 중 | `eBlocked` | `ulTaskNotifyTake(…, portMAX_DELAY)` |
| `xSuspendedTaskList` + 그 외 | `eSuspended` | 진짜 `vTaskSuspend` |
| `xTasksWaitingTermination` 또는 아무 리스트에도 없음 | `eDeleted` | idle이 아직 회수 안 함 |

`portMAX_DELAY`로 무한 대기하는 태스크는 "깨울 시각"이 없으니 delayed 리스트(깨울 시각 순으로 정렬)에 넣을 수 없다. 그래서 suspended 리스트에 넣고, `eTaskGetState`는 이벤트 리스트를 한 번 더 보고 Blocked라고 답한다(`INCLUDE_vTaskSuspend == 1`일 때만 이렇게 동작한다. 0이면 `portMAX_DELAY`도 아주 긴 timeout일 뿐이다). 다음 예제에서 이걸 리스트 주소로 직접 확인한다.

### 3.3 예제 3 — 한 태스크를 모든 상태로 몰아 보기

**무엇을 확인하나**: 태스크 TGT(우선순위 2)를 컨트롤러 CTRL(우선순위 4)이 Ready → Running → Blocked → Suspended → Ready → Blocked(큐, 무한) → Ready → Blocked(알림, 무한) → Ready → Deleted로 몰고 가면서, 매 순간 `eTaskGetState()`와 `xStateListItem.pvContainer`(TCB word 5)를 기록한다. 동시에 `traceTASK_SWITCHED_IN()` 훅으로 "언제 누가 CPU를 잡았나"를 기록해서 간트 차트를 만든다. idle이 Deleted 태스크를 언제 회수하는지 보려고, 중간에 idle을 굶기는 BUSY(우선순위 1, 무한 루프)도 하나 띄운다. 출력이 타이밍을 망치지 않도록 기록은 배열에 모았다가 마지막에 찍는다.

trace 훅을 넣으려고 이 예제만 앱 폴더에 `FreeRTOSConfig.h`를 두었다(템플릿 것을 include하고 훅 매크로만 더한다. 앱 폴더가 include 경로 맨 앞이라 이게 먼저 잡힌다):

```c
/* ex2 전용: 템플릿 설정 + 컨텍스트 스위치 trace 훅 */
#ifndef A4_EX2_CONFIG_H
#define A4_EX2_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
void a4_trace_in(void *tcb);
#define traceTASK_SWITCHED_IN()  a4_trace_in((void *)pxCurrentTCB)
#endif
```

```c
/* ex2: 한 태스크(TGT)를 모든 상태로 몰아 보며 eTaskGetState + 스위치 trace 기록 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
static TaskHandle_t hC, hT, hB; static QueueHandle_t q;
static const char *st[] = { "Running", "Ready", "Blocked", "Suspended", "Deleted" };
static struct { uint32_t t; const char *ev; int s; uint32_t list; } lg[24]; static int nl;
static struct { uint32_t t; char nm[5]; } tr[64]; static int nt;
void a4_trace_in(void *tcb) {                             /* 이름 앞 4글자 복사 (TCB+52) */
    if (nt < 64) { tr[nt].t = board_now_us(); for (int i = 0; i < 4; i++) tr[nt].nm[i] = ((char *)tcb)[52 + i]; nt++; } }
static void note(const char *ev, TaskHandle_t h) {        /* 상태 + xStateListItem.pvContainer 기록 */
    taskENTER_CRITICAL();
    lg[nl].t = board_now_us(); lg[nl].ev = ev; lg[nl].s = eTaskGetState(h);
    lg[nl++].list = ((uint32_t *)h)[5];
    taskEXIT_CRITICAL();
}
static void spin_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) { } }
static void busy(void *pv) { (void)pv; for (;;) { } }     /* prio 1: idle을 굶긴다 */
static void tgt(void *pv) {
    (void)pv; uint32_t v;
    note("TGT runs itself", hT);  spin_us(300); vTaskDelay(2);
    note("TGT back from delay", hT); spin_us(300); xQueueReceive(q, &v, portMAX_DELAY);
    note("TGT got queue item", hT); spin_us(300); ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    spin_us(300); vTaskDelete(NULL);                          /* 자기 삭제 */
}
static void ctrl(void *pv) {
    (void)pv; uint32_t v = 7;
    note("ctrl starts", hT);           vTaskDelay(1);
    note("TGT in vTaskDelay(2)", hT);  vTaskSuspend(hT);
    note("after vTaskSuspend", hT);    vTaskDelay(3);
    note("3 ms later, still", hT);     vTaskResume(hT);
    note("after vTaskResume", hT);     vTaskDelay(1);
    note("TGT waits queue forever", hT); xQueueSend(q, &v, 0);
    note("after xQueueSend", hT);      vTaskDelay(1);
    note("TGT waits notify forever", hT);
    xTaskCreate(busy, "BUSY", 128, NULL, 1, &hB);
    xTaskNotifyGive(hT);
    note("after xTaskNotifyGive", hT); vTaskDelay(1);
    note("TGT called vTaskDelete(NULL)", hT);
    size_t f0 = xPortGetFreeHeapSize(); vTaskDelete(hB);
    size_t f1 = xPortGetFreeHeapSize(); vTaskDelay(1);        /* 이제 idle이 돈다 */
    size_t f2 = xPortGetFreeHeapSize();
    for (int i = 0; i < nl; i++)
        board_printf_raw("%u us  %s  -> %s  (list 0x%x)\n", lg[i].t, lg[i].ev, st[lg[i].s], lg[i].list);
    board_printf_raw("heap free: TGT deleted=%u, BUSY deleted=%u, after idle ran=%u\n",
                     (unsigned)f0, (unsigned)f1, (unsigned)f2);
    for (int i = 0; i < nt; i++)
        board_printf_raw("SW %u %s\n", tr[i].t, tr[i].nm);
    board_exit(0);
}
int main(void) {
    q = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreate(ctrl, "CTRL", 256, NULL, 4, &hC);
    xTaskCreate(tgt, "TGT", 256, NULL, 2, &hT);
    note("created (no scheduler)", hT);
    vTaskStartScheduler();
}
```

```sh
/Users/donh/.rtos_scratch/A4/run.sh ex2/ex2      # ICOUNT=1
arm-none-eabi-nm -S /var/tmp/rtos/A4/ex2/app.elf | grep -E " (pxReadyTasksLists|xDelayedTaskList1|xDelayedTaskList2|xSuspendedTaskList|xTasksWaitingTermination|xPendingReadyList)$"
```

```text
   text	   data	    bss	    dec	    hex	filename
  14968	    100	 136576	 151644	  2505c	/var/tmp/rtos/A4/ex2/app.elf
75 us  created (no scheduler)  -> Ready  (list 0x2000128c)
158 us  ctrl starts  -> Ready  (list 0x2000128c)
171 us  TGT runs itself  -> Running  (list 0x2000128c)
1151 us  TGT in vTaskDelay(2)  -> Blocked  (list 0x20001250)
1159 us  after vTaskSuspend  -> Suspended  (list 0x200011f4)
4151 us  3 ms later, still  -> Suspended  (list 0x200011f4)
4158 us  after vTaskResume  -> Ready  (list 0x2000128c)
4171 us  TGT back from delay  -> Running  (list 0x2000128c)
5151 us  TGT waits queue forever  -> Blocked  (list 0x200011f4)
5161 us  after xQueueSend  -> Ready  (list 0x2000128c)
5176 us  TGT got queue item  -> Running  (list 0x2000128c)
6151 us  TGT waits notify forever  -> Blocked  (list 0x200011f4)
6182 us  after xTaskNotifyGive  -> Ready  (list 0x2000128c)
7151 us  TGT called vTaskDelete(NULL)  -> Deleted  (list 0x2000120c)
heap free: TGT deleted=128064, BUSY deleted=128688, after idle ran=129824
SW 137 Tmr 
SW 156 CTRL
SW 169 TGT
SW 481 IDLE
SW 1149 CTRL
SW 1170 IDLE
SW 4149 CTRL
SW 4169 TGT
SW 4489 IDLE
SW 5149 CTRL
SW 5171 TGT
SW 5486 IDLE
SW 6149 CTRL
SW 6192 TGT
SW 6502 BUSY
SW 7149 CTRL
SW 7174 IDLE
SW 8149 CTRL
[board_exit] code=0
exit=0
20001264 000000a0 b pxReadyTasksLists
20001250 00000014 b xDelayedTaskList1
2000123c 00000014 b xDelayedTaskList2
20001220 00000014 b xPendingReadyList
200011f4 00000014 b xSuspendedTaskList
2000120c 00000014 b xTasksWaitingTermination
```

### 3.4 출력 읽기 — 리스트 주소가 상태를 말한다

리스트 주소를 심볼로 바꾸면: `0x2000128c` = `pxReadyTasksLists` + 2 × 20 = **`pxReadyTasksLists[2]`**, `0x20001250` = `xDelayedTaskList1`, `0x200011f4` = `xSuspendedTaskList`, `0x2000120c` = `xTasksWaitingTermination`.

| 시각 (us) | 사건 | eTaskGetState | 실제로 꽂힌 리스트 | 배울 점 |
|---|---|---|---|---|
| 75 | 생성 직후, 스케줄러 전 | Ready | ready[2] | 만들면 Ready 리스트로 간다 |
| 171 | TGT가 자기 자신을 조회 | **Running** | **ready[2]** | Running도 Ready 리스트에 있다. 차이는 `pxCurrentTCB`뿐 |
| 1151 | `vTaskDelay(2)` 중 | Blocked | delayed list 1 | 깨울 시각 순으로 정렬된 리스트 |
| 1159 | `vTaskSuspend(hT)` | Suspended | suspended | delay 중이어도 끌어내 suspended로 옮긴다 |
| 4151 | 3 ms 뒤 (원래 delay는 끝났을 시각) | Suspended | suspended | Suspended에는 timeout이 없다 |
| 4158 | `vTaskResume(hT)` | Ready | ready[2] | **delay가 취소된 채** Ready로. 4171에 `vTaskDelay`에서 그냥 return했다 |
| 5151 | `xQueueReceive(…, portMAX_DELAY)` | **Blocked** | **suspended** | 무한 대기는 suspended 리스트에 있지만 Blocked로 보고된다 |
| 5161 | `xQueueSend` | Ready | ready[2] | 이벤트가 오면 Ready. CTRL이 더 높아서 아직 못 돈다 |
| 6151 | `ulTaskNotifyTake(…, portMAX_DELAY)` | Blocked | suspended | 알림 대기는 이벤트 리스트 없이 `ucNotifyState`로 판정 |
| 7151 | TGT가 `vTaskDelete(NULL)` 한 뒤 | Deleted | waiting termination | 자기 삭제는 즉시 free 못 한다 (자기 스택 위에서 도는 중이니까) |

heap 숫자도 이야기를 한다. 7151 us에 TGT는 Deleted인데 heap은 그대로다(128064). BUSY(우선순위 1 무한 루프)가 CPU를 쥐고 있어서 **idle 태스크가 한 번도 못 돌았기 때문**이다. CTRL이 BUSY를 지우면(남의 태스크 삭제) 그 자리에서 624 B(512 B 스택 + 96 B TCB + 헤더 16 B)가 돌아오고(128688), CTRL이 1 ms 자는 동안 idle이 돌아서(7174 us `SW … IDLE`) TGT의 1136 B(1024 + 96 + 16)를 회수한다(129824). 그래서 FreeRTOS 문서가 "자기 삭제를 쓰면 idle 태스크가 CPU 시간을 굶지 않게 하라"고 말한다(C9).

함정 하나 더: `vTaskSuspend()`는 대기 중인 태스크를 **이벤트 리스트에서도 빼 버린다**(`vTaskSuspend` 소스: "Is the task waiting on an event also?" → `uxListRemove(&xEventListItem)`, 알림 대기면 `ucNotifyState`를 `taskNOT_WAITING_NOTIFICATION`으로 되돌림). 재개되면 그 API 안에서 조건을 다시 검사하는데, `vTaskDelay`처럼 재검사 루프가 없는 호출은 4171 us처럼 그냥 일찍 끝난다. 반대로 Zephyr는 대기 중에 suspend해도 "pending+suspended"로 대기를 유지한다(§3.6).

### 3.5 측정 타임라인

위 trace(`SW …` 줄)를 그대로 그리면 이렇다. 각 막대는 그 태스크가 Running이었던 구간이다.

```svg
<svg viewBox="0 0 680 335" xmlns="http://www.w3.org/2000/svg">
<text x="380" y="18" text-anchor="middle" font-size="13">ex2 측정 trace — 누가 CPU를 쥐었나 (QEMU icount, us)</text>
<text x="92" y="48" text-anchor="end" font-size="12">Tmr Svc (7)</text>
<line x1="100" y1="56" x2="660" y2="56" stroke="#888" stroke-width="0.5"/>
<rect x="109.4" y="35" width="1.5" height="18" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="92" y="78" text-anchor="end" font-size="12">CTRL (4)</text>
<line x1="100" y1="86" x2="660" y2="86" stroke="#888" stroke-width="0.5"/>
<rect x="110.7" y="65" width="1.5" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="178.5" y="65" width="1.5" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="383.3" y="65" width="1.5" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="451.6" y="65" width="1.5" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="519.9" y="65" width="2.9" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="588.2" y="65" width="1.7" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="656.5" y="65" width="2.1" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="92" y="108" text-anchor="end" font-size="12">TGT (2)</text>
<line x1="100" y1="116" x2="660" y2="116" stroke="#888" stroke-width="0.5"/>
<rect x="111.5" y="95" width="21.3" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="384.7" y="95" width="21.9" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="453.1" y="95" width="21.5" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="522.9" y="95" width="21.2" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="92" y="138" text-anchor="end" font-size="12">BUSY (1)</text>
<line x1="100" y1="146" x2="660" y2="146" stroke="#888" stroke-width="0.5"/>
<rect x="544.0" y="125" width="44.2" height="18" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="92" y="168" text-anchor="end" font-size="12">IDLE (0)</text>
<line x1="100" y1="176" x2="660" y2="176" stroke="#888" stroke-width="0.5"/>
<rect x="132.8" y="155" width="45.6" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<rect x="179.9" y="155" width="203.4" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<rect x="406.6" y="155" width="45.1" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<rect x="474.7" y="155" width="45.3" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<rect x="589.9" y="155" width="66.6" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="92" y="212" text-anchor="end" font-size="12">TGT 상태</text>
<rect x="100.0" y="196" width="11.5" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="111.5" y="196" width="21.3" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="122.2" y="212" text-anchor="middle" font-size="12">X</text>
<rect x="132.8" y="196" width="46.3" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="156.0" y="212" text-anchor="middle" font-size="12">B</text>
<rect x="179.2" y="196" width="204.8" height="24" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="281.6" y="212" text-anchor="middle" font-size="12">S</text>
<rect x="384.0" y="196" width="1.5" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="384.7" y="196" width="21.9" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="395.6" y="212" text-anchor="middle" font-size="12">X</text>
<rect x="406.6" y="196" width="45.9" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="429.5" y="212" text-anchor="middle" font-size="12">B</text>
<rect x="452.5" y="196" width="1.5" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="453.1" y="196" width="21.5" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="463.9" y="212" text-anchor="middle" font-size="12">X</text>
<rect x="474.7" y="196" width="47.5" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="498.4" y="212" text-anchor="middle" font-size="12">B</text>
<rect x="522.2" y="196" width="1.5" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="522.9" y="196" width="21.2" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="533.5" y="212" text-anchor="middle" font-size="12">X</text>
<rect x="544.0" y="196" width="45.9" height="24" fill="#888" fill-opacity="0.35" stroke="#888"/>
<text x="567.0" y="212" text-anchor="middle" font-size="12">D</text>
<text x="593.9" y="212" font-size="12">해제됨</text>
<line x1="100" y1="230" x2="660" y2="230" stroke="currentColor"/>
<line x1="100.0" y1="230" x2="100.0" y2="235" stroke="currentColor"/>
<text x="100.0" y="248" text-anchor="middle" font-size="12">0 ms</text>
<line x1="100.0" y1="32" x2="100.0" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="168.3" y1="230" x2="168.3" y2="235" stroke="currentColor"/>
<text x="168.3" y="248" text-anchor="middle" font-size="12">1 ms</text>
<line x1="168.3" y1="32" x2="168.3" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="236.6" y1="230" x2="236.6" y2="235" stroke="currentColor"/>
<text x="236.6" y="248" text-anchor="middle" font-size="12">2 ms</text>
<line x1="236.6" y1="32" x2="236.6" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="304.9" y1="230" x2="304.9" y2="235" stroke="currentColor"/>
<text x="304.9" y="248" text-anchor="middle" font-size="12">3 ms</text>
<line x1="304.9" y1="32" x2="304.9" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="373.2" y1="230" x2="373.2" y2="235" stroke="currentColor"/>
<text x="373.2" y="248" text-anchor="middle" font-size="12">4 ms</text>
<line x1="373.2" y1="32" x2="373.2" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="441.5" y1="230" x2="441.5" y2="235" stroke="currentColor"/>
<text x="441.5" y="248" text-anchor="middle" font-size="12">5 ms</text>
<line x1="441.5" y1="32" x2="441.5" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="509.8" y1="230" x2="509.8" y2="235" stroke="currentColor"/>
<text x="509.8" y="248" text-anchor="middle" font-size="12">6 ms</text>
<line x1="509.8" y1="32" x2="509.8" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="578.0" y1="230" x2="578.0" y2="235" stroke="currentColor"/>
<text x="578.0" y="248" text-anchor="middle" font-size="12">7 ms</text>
<line x1="578.0" y1="32" x2="578.0" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<line x1="646.3" y1="230" x2="646.3" y2="235" stroke="currentColor"/>
<text x="646.3" y="248" text-anchor="middle" font-size="12">8 ms</text>
<line x1="646.3" y1="32" x2="646.3" y2="230" stroke="#888" stroke-width="0.4" stroke-dasharray="2 3"/>
<circle cx="132.8" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="132.8" y="192" text-anchor="middle" font-size="12">1</text>
<circle cx="179.2" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="179.2" y="192" text-anchor="middle" font-size="12">2</text>
<circle cx="384.0" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="384.0" y="192" text-anchor="middle" font-size="12">3</text>
<circle cx="406.6" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="406.6" y="192" text-anchor="middle" font-size="12">4</text>
<circle cx="452.5" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="452.5" y="192" text-anchor="middle" font-size="12">5</text>
<circle cx="474.7" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="474.7" y="192" text-anchor="middle" font-size="12">6</text>
<circle cx="522.2" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="522.2" y="192" text-anchor="middle" font-size="12">7</text>
<circle cx="544.0" cy="188" r="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="544.0" y="192" text-anchor="middle" font-size="12">8</text>
<text x="20" y="270" font-size="12">1 vTaskDelay(2)</text>
<text x="185" y="270" font-size="12">2 vTaskSuspend</text>
<text x="350" y="270" font-size="12">3 vTaskResume</text>
<text x="515" y="270" font-size="12">4 xQueueReceive(∞)</text>
<text x="20" y="288" font-size="12">5 xQueueSend</text>
<text x="185" y="288" font-size="12">6 ulTaskNotifyTake(∞)</text>
<text x="350" y="288" font-size="12">7 xTaskNotifyGive</text>
<text x="515" y="288" font-size="12">8 vTaskDelete(NULL)</text>
<rect x="20" y="303" width="14" height="14" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="40" y="314" font-size="12">R = Ready</text>
<rect x="150" y="303" width="14" height="14" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="170" y="314" font-size="12">X = Running</text>
<rect x="280" y="303" width="14" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="300" y="314" font-size="12">B = Blocked</text>
<rect x="410" y="303" width="14" height="14" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="430" y="314" font-size="12">S = Suspended</text>
<rect x="540" y="303" width="14" height="14" fill="#888" fill-opacity="0.35" stroke="#888"/>
<text x="560" y="314" font-size="12">D = Deleted</text>
</svg>
```

그림 5 — 예제 3의 실측 간트 차트(QEMU icount). 위 다섯 줄은 누가 CPU를 쥐었나, 아래 "TGT 상태" 줄은 eTaskGetState로 본 TGT의 상태다. 숫자 동그라미는 상태를 바꾼 API 호출 시점이다. 6.5~7.1 ms 동안 BUSY가 CPU를 독차지해서 IDLE이 없고, 그 사이 TGT는 D(Deleted, 메모리 미회수)로 남는다. CTRL은 1 ms tick마다 깨서 20~40 us만 일하고 다시 Blocked로 간다 — 막대가 가늘게 보이는 이유다.

이 그림에서 볼 RTOS의 본질 세 가지:

- 한 시점에 Running은 하나다(위 다섯 줄 중 한 줄만 칠해져 있다).
- 높은 우선순위(CTRL 4)는 깨어나자마자 낮은 것(TGT 2, IDLE 0)을 밀어낸다 — 1149, 4149, 5149 … us. 이것이 선점(preemption)이다(C4).
- 아무도 할 일이 없으면 IDLE이 돈다. IDLE이 길다는 것 = CPU가 놀고 있다는 것. run-time stats(§7)로 이 비율을 숫자로 본다.

### 3.6 Zephyr의 상태

Zephyr에는 열거형 상태 대신 `base.thread_state`에 비트가 있다(`include/zephyr/kernel_structs.h`): `_THREAD_DUMMY`(BIT 0), `_THREAD_PENDING`(1), `_THREAD_SLEEPING`(2), `_THREAD_DEAD`(3), `_THREAD_SUSPENDED`(4), `_THREAD_ABORTING`(5), `_THREAD_SUSPENDING`(6), `_THREAD_QUEUED`(7). `k_thread_state_str()`(kernel/thread.c)가 켜진 비트를 "pending+suspended"처럼 `+`로 이어 문자열로 만든다. 여러 비트가 동시에 켜질 수 있다는 것이 FreeRTOS와 다르다.

| FreeRTOS | Zephyr 비트 (문자열) | §8 예제 11에서 실제로 본 것 |
|---|---|---|
| Running | 별도 비트 없음 — 실행 중인 스레드는 `_current`. 단일 코어에서는 FreeRTOS처럼 런큐에 그대로 둔다 (`kernel/sched.c` 주석) | `main`이 실행 중에 "queued"로 보였다 |
| Ready | `_THREAD_QUEUED` ("queued") | `k_thread_create` 직후 "queued" |
| Blocked (시간) | `_THREAD_SLEEPING` ("sleeping") | `k_msleep` 중인 sleeper |
| Blocked (객체 대기) | `_THREAD_PENDING` ("pending") | `k_sem_take(…, K_FOREVER)` 중인 waiter |
| Suspended | `_THREAD_SUSPENDED` ("suspended") | 대기 중 suspend → "pending+suspended" |
| Deleted | `_THREAD_DEAD` ("dead") | entry 함수가 return한 뒤 "dead" |
| — | (비트 0개, 빈 문자열) | idle 스레드: 런큐에 넣지 않고 따로 고른다 |

---

## 4. 태스크 함수의 모양 — 무한 루프, 절대 return하지 않는다

### 4.1 기본형

FreeRTOS 태스크 함수의 타입은 `typedef void (* TaskFunction_t)( void * arg );`(`projdefs.h`)다. 거의 모든 태스크는 이 모양이다.

```c
static void imu_task(void *pvParameters)
{
    const struct imu_cfg *cfg = pvParameters;    /* 1. 인자 꺼내기 */
    imu_init(cfg);                               /* 2. 한 번만 할 초기화 */
    for (;;) {                                   /* 3. 무한 루프 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /*    블로킹: DRDY ISR이 깨워 줄 때까지 CPU 0% */
        imu_read_and_publish(cfg);               /*    일 */
    }
    /* 여기 도달하면 안 된다. 끝내야 한다면 vTaskDelete(NULL); */
}
```

핵심은 루프 안에 **반드시 블로킹 호출이 있어야 한다**는 것이다. 블로킹 없이 도는 루프(예제 3의 BUSY, §7의 hog)는 자기보다 낮은 우선순위 태스크를 전부 굶긴다. bare-metal super-loop의 `while(1)`은 CPU를 다 써도 상관없었지만, RTOS 태스크의 `for(;;)`는 "일하고 → 기다리고"의 반복이어야 한다.

### 4.2 `pvParameters` — 한 함수로 여러 태스크

`pvParameters`는 생성할 때 준 포인터가 그대로 R0로 들어온다(예제 2의 위조 프레임 `r0 = &param`). 같은 함수로 인스턴스를 여러 개 만들 때 쓴다 — 예를 들어 IMU 두 개, 모터 네 개, UART 세 개. §5 예제 6이 `worker` 함수 하나로 태스크 9개를 만들고 `(void *)k`로 번호를 넘긴다.

함정: **스택 위의 지역 변수 주소를 넘기면 안 된다.** `main()`이나 다른 태스크의 지역 변수는 그 함수가 끝나거나(스케줄러 시작 후 `main`의 스택은 재사용될 수 있다, C3) 값이 바뀌면 깨진다. `static`, 전역, 또는 heap 객체의 주소를 넘긴다. 예제 2가 `static uint32_t param`을 쓴 이유다.

### 4.3 예제 4 — 태스크 함수가 return하면?

**무엇을 확인하나**: 위조 프레임의 LR에 심어 둔 `prvTaskExitError`가 실제로 무엇을 하는지. V11.3.2 ARM_CM3 포트의 구현(`portable/GCC/ARM_CM3/port.c`)은 이렇다:

```c
static void prvTaskExitError( void )
{
    volatile uint32_t ulDummy = 0UL;
    /* A function that implements a task must not exit or attempt to return to
     * its caller as there is nothing to return to.  If a task wants to exit it
     * should instead call vTaskDelete( NULL ). ... */
    configASSERT( uxCriticalNesting == ~0UL );
    portDISABLE_INTERRUPTS();
    while( ulDummy == 0 ) { }
}
```

`uxCriticalNesting`은 스케줄러 시작 때 0이 되므로 `configASSERT(uxCriticalNesting == ~0UL)`는 **일부러 항상 실패하는 assert**다("Artificially force an assert() to be triggered"). assert가 없으면 인터럽트를 끄고 영원히 돈다.

```c
/* ex3: 태스크 함수가 return하면? (FreeRTOS V11.3.2 ARM_CM3 포트) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void oneshot(void *pv)
{
    board_printf_raw("[%u us] oneshot: param=%s, work done, returning...\n",
                     board_now_us(), (const char *)pv);
    /* 여기서 return → LR에 심어 둔 prvTaskExitError로 점프 */
}

static void alive(void *pv)
{
    (void)pv;
    for (int i = 0;; i++) {
        board_printf_raw("[%u us] alive tick %d\n", board_now_us(), i);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

int main(void)
{
    xTaskCreate(alive, "alive", 256, NULL, 1, NULL);
    xTaskCreate(oneshot, "oneshot", 256, "hello", 2, NULL);
    vTaskStartScheduler();
}
```

```text
   text	   data	    bss	    dec	    hex	filename
  11824	    100	 135400	 147324	  23f7c	/var/tmp/rtos/A4/ex3/app.elf
[145 us] oneshot: param=hello, work done, returning...

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:212
[board_exit] code=253
exit=1
```

**출력에서 볼 것**: return 직후 `port.c:212`(`configASSERT( uxCriticalNesting == ~0UL )` 줄)에서 assert가 터졌다. 랩 템플릿의 `configASSERT`는 파일·줄을 찍고 종료 코드 253으로 QEMU를 끈다. 개발 빌드에서는 이렇게 바로 위치가 나오니 찾기 쉽다.

### 4.4 예제 5 — configASSERT를 끈 빌드에서는? (양산 빌드에서 흔하다)

**무엇을 확인하나**: assert를 끄면 시스템이 어떻게 "조용히" 죽는지, 그리고 디버거로 붙었을 때 무엇이 보이는지. 같은 소스를 `configASSERT`만 `#undef`한 설정으로 빌드하고, QEMU를 gdb 서버(`-s`)로 띄운 뒤 1초 후에 붙는다.

```c
/* ex3b: configASSERT 없는 빌드 (양산 빌드에서 흔한 설정) */
#ifndef A4_EX3B_CONFIG_H
#define A4_EX3B_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef configASSERT
#endif
```

```sh
#!/bin/bash
# ex3b: configASSERT 없이 빌드 → 실행 → 1초 뒤 gdb로 붙어서 PC 확인
O=/var/tmp/rtos/A4/ex3b
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A4/ex3b/ex3b.c OUT=$O >/dev/null
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial file:$O/uart.txt \
  -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -s -kernel $O/app.elf &
QP=$!; sleep 1
cat $O/uart.txt
gdb -batch -ex "target remote :1234" -ex "info registers pc primask basepri" -ex "bt" \
    -ex "p pxCurrentTCB->pcTaskName" -ex "p uxCriticalNesting" $O/app.elf 2>&1 | grep -v "^warning"
kill $QP
```

```text
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c: In function 'ulTaskGetRunTimePercent':
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:8564:17: warning: variable 'pxTCB' set but not used [-Wunused-but-set-variable]
/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c: In function 'xPortStartScheduler':
/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:284:33: warning: unused variable 'pxVectorTable' [-Wunused-variable]
...
[132 us] oneshot: param=hello, work done, returning...
prvTaskExitError () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:215
215	    while( ulDummy == 0 )
pc             0x196a              0x196a <prvTaskExitError+22>
primask        0x0                 0
basepri        0x20                32
#0  prvTaskExitError () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:215
#1  0x00001954 in prvInsertBlockIntoFreeList (pxBlockToInsert=0xa5a5a59d) at /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:541
#2  vPortFree (pv=0xa5a5a5a5) at /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:395
#3  0x80000068 in ?? ()
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
$1 = "oneshot\000\000\000\000\000\000\000\000"
$2 = 0
[Inferior 1 (process 1) detached]
```

(컴파일러 경고의 소스 줄 표시 부분은 `...`로 줄였고, 마지막 QEMU 종료 메시지는 생략했다.)

**출력에서 볼 것**:

- 경고 2개는 **커널 소스**에서 나온다: assert를 끄면 assert에만 쓰이던 변수가 "안 쓰임"이 된다. 우리 코드의 경고는 0개다. 양산 빌드에서 assert를 끌 때 이런 경고가 나오는 것은 정상이다.
- CPU는 `prvTaskExitError`의 `while` 루프(port.c:215)에서 돌고 있다. `basepri = 0x20` = `configMAX_SYSCALL_INTERRUPT_PRIORITY` — `portDISABLE_INTERRUPTS()`는 PRIMASK가 아니라 **BASEPRI**로 막는다(B3, C7). 우선순위 0x20 이하(숫자가 큰) 인터럽트, 즉 SysTick(255)과 PendSV가 전부 막혔으니 **tick도 컨텍스트 스위치도 없다**. `alive` 태스크(우선순위 1)는 한 줄도 찍지 못했다 — 태스크 하나의 실수가 시스템 전체를 멈춘다.
- backtrace는 엉터리다(`vPortFree(pv=0xa5a5a5a5)`). `prvTaskExitError`는 진짜로 "호출"된 게 아니라 LR에 심어 둔 주소로 **점프**한 것이라 그 위에 정상 프레임이 없고, 스택에는 0xa5 채움 값만 있다. gdb가 그걸 프레임으로 해석했다. 현장에서 "backtrace가 heap 함수로 이상하게 나오고 PC가 `prvTaskExitError`"이면 이 상황을 의심한다. 포트 소스 위쪽 주석이 이 문제를 언급하고 `configTASK_RETURN_ADDRESS`로 LR 값을 바꿀 수 있게 해 둔 이유다.

### 4.5 끝내야 하는 태스크는 `vTaskDelete(NULL)` — Zephyr는 return해도 된다

일회성 태스크(부팅 시 캘리브레이션 등)는 마지막 줄에서 `vTaskDelete(NULL);`을 부른다. §3 예제 3에서 본 것처럼 TCB와 스택은 idle이 나중에 회수한다. 실무에서는 아예 지우지 않고 영원히 블록시키거나(정적 할당 정책), 처음부터 태스크 대신 초기화 코드로 처리하는 경우가 많다.

Zephyr는 다르다. `lib/os/thread_entry.c`의 `z_thread_entry()`가 사용자 entry 함수를 감싸서 부른다:

```c
	entry(p1, p2, p3);

	k_thread_abort(k_current_get());
```

그래서 Zephyr 스레드는 entry 함수에서 그냥 return하면 정상 종료(`_THREAD_DEAD`)된다 — §8 예제 11에서 "returning from entry (OK in Zephyr)" 뒤에 상태가 "dead"로 바뀐다. 이 차이 때문에 Zephyr의 위조 프레임 PC는 사용자 함수가 아니라 `z_thread_entry`이고, 사용자 함수 주소는 R0(`a1`)에 들어간다(`arch/arm/core/cortex_m/thread.c`의 `arch_new_thread`: `iframe->pc = (uint32_t)entry_wrapper; iframe->a1 = (uint32_t)entry;`). FreeRTOS에서 같은 안전망을 원하면 태스크 함수 마지막에 `vTaskDelete(NULL)`을 직접 쓰는 수밖에 없다.

---

## 5. 태스크별 스택

### 5.1 스택에는 무엇이 들어가나

태스크 스택에 쌓이는 것은 네 종류다.

1. **지역 변수** — 특히 지역 배열. 컴파일러가 함수 진입 때 `sub sp, #N`으로 한 번에 자리를 잡는다.
2. **호출 경로** — 함수마다 복귀 주소(LR)와 callee-saved 레지스터(R4–R11 중 그 함수가 쓰는 것). 깊이 = 호출 체인의 합.
3. **문맥 저장 프레임** — 이 태스크가 선점·블록될 때 R0–R3, R12, LR, PC, xPSR(하드웨어 8워드) + R4–R11(PendSV 8워드) = 16워드 = 64 B. FPU 문맥까지 저장되면 더 커진다(B6, M4F 이상).
4. **ISR이 쓰는 몫** — Cortex-M에서는 인터럽트가 들어오면 하드웨어가 **현재 스택(PSP)**에 8워드를 쌓지만, ISR 본문은 MSP(main 스택)에서 돈다. FreeRTOS Cortex-M 포트는 태스크가 PSP, ISR·커널이 MSP를 쓰도록 설정한다(B1, C3). 그래서 **ISR의 지역 변수는 태스크 스택을 먹지 않는다** — 태스크마다 "최악 ISR 중첩 깊이"를 더할 필요가 없다는 게 Cortex-M의 큰 장점이다. (RISC-V 등 일부 포트는 ISR이 현재 태스크 스택을 쓰기도 한다. B10)

### 5.2 크기 단위 함정 — FreeRTOS는 words, Zephyr는 bytes

| API | 스택 크기 인자 단위 | `1024`를 넣으면 |
|---|---|---|
| FreeRTOS `xTaskCreate(…, uxStackDepth, …)` | **`StackType_t` 개수 (words)** — Cortex-M은 4바이트 | 4096 B |
| FreeRTOS `xTaskCreateStatic(…, uxStackDepth, …, puxStackBuffer, …)` | words (`StackType_t` 배열 길이) | 4096 B |
| FreeRTOS `uxTaskGetStackHighWaterMark()` 반환값 | words | — |
| Zephyr `K_THREAD_DEFINE(name, stack_size, …)` | **bytes** | 1024 B |
| Zephyr `K_THREAD_STACK_DEFINE(sym, size)` + `k_thread_create(…, K_THREAD_STACK_SIZEOF(sym), …)` | bytes | 1024 B |
| Zephyr `k_thread_stack_space_get()` 결과 | bytes | — |
| CMSIS-RTOS2 `osThreadAttr_t.stack_size` | bytes | 1024 B |

FreeRTOS 소스에서 `xTaskCreate`는 `pvPortMallocStack( ( ( ( size_t ) uxStackDepth ) * sizeof( StackType_t ) ) )`로 할당한다 — 곱하기 `sizeof(StackType_t)`가 단위의 증거다. 8비트 AVR 포트처럼 `StackType_t`가 1바이트인 포트에서는 같은 숫자가 바이트가 된다. 회사 코드를 FreeRTOS에서 Zephyr로(또는 반대로) 옮길 때 숫자를 그대로 복사하면 스택이 **4배 작아지거나 4배 커진다**. 면접에서 "다른 RTOS로 포팅할 때 조심할 점"에 단골로 나오는 답이다.

### 5.3 0xa5 채우기와 high water mark

FreeRTOS는 `prvInitialiseNewTask()`에서 스택 전체를 `tskSTACK_FILL_BYTE`(0xa5)로 채운다. 조건(tasks.c): `configCHECK_FOR_STACK_OVERFLOW > 1` 또는 `configUSE_TRACE_FACILITY == 1` 또는 `INCLUDE_uxTaskGetStackHighWaterMark == 1`이면 `tskSET_NEW_STACKS_TO_KNOWN_VALUE`가 1이 된다. 그다음 `uxTaskGetStackHighWaterMark()`는 `prvTaskCheckFreeStackSpace()`로 **스택 바닥(`pxStack`)부터 위로 올라가며 0xa5가 연속으로 몇 바이트 남았는지** 센다:

```c
    while( *pucStackByte == ( uint8_t ) tskSTACK_FILL_BYTE )
    {
        pucStackByte -= portSTACK_GROWTH;
        uxCount++;
    }
    uxCount /= ( configSTACK_DEPTH_TYPE ) sizeof( StackType_t );
```

즉 "high water mark" = **지금까지 가장 깊이 내려갔을 때 남아 있던 여유(words)**다. 이름이 헷갈리지만 "사용량"이 아니라 "최소 여유"다. 값이 작을수록 위험하다. 한 번 덮인 0xa5는 되돌아오지 않으니, 측정 전에 최악 경로(가장 깊은 호출, 가장 큰 버퍼, 에러 처리 경로)를 한 번은 실행시켜야 의미가 있다.

### 5.4 예제 6 — 태스크 모양에 따라 스택을 얼마나 쓰나

**무엇을 확인하나**: 같은 4 KB(1024 words) 스택을 준 태스크 9개가 각각 다른 일(호출 없음, 지역 배열 256 B·1 KB, 재귀 8·32단, 작은 포맷터, newlib-nano `snprintf`·`printf`)을 한 뒤 high water mark가 얼마인지. `-fstack-usage`로 컴파일러가 계산한 함수별 프레임 크기도 같이 본다.

```c
/* ex4: 태스크 모양(호출 깊이·지역 버퍼·printf)에 따라 스택을 얼마나 쓰나 — high water mark */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define DEPTH 1024                                   /* words = 4096 B, 모두 같은 크기 */
static volatile uint32_t sink;

#define NI __attribute__((noinline))   /* 인라인되면 body() 프레임이 커져서 비교가 흐려진다 */
static NI void leaf(void)   { for (int i = 0; i < 100; i++) sink += (uint32_t)i; }
static NI void buf256(void) { volatile uint8_t b[256];  for (int i = 0; i < 256; i++)  b[i] = (uint8_t)i; sink += b[9]; }
static NI void buf1k(int n) { volatile uint8_t b[1024]; for (int i = 0; i < n; i++)    b[i] = (uint8_t)i; sink += b[9]; }
static NI uint32_t rec(int d) { volatile uint32_t loc[4] = { (uint32_t)d }; return d ? rec(d - 1) + loc[0] : loc[0]; }
static void body(int k)
{
    char s[32];
    switch (k) {
    case 0: leaf(); break;
    case 1: buf256(); break;
    case 2: buf1k(1024); break;
    case 8: buf1k(64); break;                                /* 1 KB 배열 중 앞 64 B만 씀 */
    case 3: sink += rec(8); break;
    case 4: sink += rec(32); break;
    case 5: board_printf_raw("%s", ""); break;               /* 작은 포맷터 */
    case 6: snprintf(s, sizeof s, "%d/%s", k, "x"); sink += (uint32_t)s[0]; break; /* newlib-nano */
    case 7: printf("%.0d", 0); break;                        /* newlib-nano printf, 출력 0글자 */
    }
}
static const char *nm[9] = { "leaf", "buf256", "buf1024", "rec8", "rec32", "rawprintf", "snprintf", "printf", "buf1k-64B" };
static TaskHandle_t h[9];

static void worker(void *pv) { for (;;) { body((int)pv); vTaskDelay(1); } }

static void report(void *pv)
{
    (void)pv;
    vTaskDelay(10);
    board_printf_raw("task       depth  HWM(free)  used words  used bytes\n");
    for (int k = 0; k < 9; k++) {
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(h[k]);
        board_printf_raw("%s\t%u\t%u\t%u\t%u\n", nm[k], DEPTH, (unsigned)hwm,
                         (unsigned)(DEPTH - hwm), (unsigned)((DEPTH - hwm) * 4));
    }
    board_printf_raw("report task itself: HWM=%u of 256\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    board_printf_raw("idle task: HWM=%u of %u\n", (unsigned)uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle()),
                     (unsigned)configMINIMAL_STACK_SIZE);
    board_exit(0);
}

int main(void)
{
    for (int k = 0; k < 9; k++)
        xTaskCreate(worker, nm[k], DEPTH, (void *)k, 2, &h[k]);
    xTaskCreate(report, "report", 256, NULL, 3, NULL);
    vTaskStartScheduler();
}
```

```sh
/Users/donh/.rtos_scratch/A4/run.sh ex4 EXTRA_CFLAGS=-fstack-usage
cat /var/tmp/rtos/A4/ex4/app.elf-ex4.su
grep -E "vTaskDelay|xTaskResumeAll|vTaskSwitchContext" /var/tmp/rtos/A4/ex4/app.elf-tasks.su
```

```text
   text	   data	    bss	    dec	    hex	filename
  17396	    100	 135440	 152936	  25568	/var/tmp/rtos/A4/ex4/app.elf
task       depth  HWM(free)  used words  used bytes
leaf	1024	992	32	128
buf256	1024	946	78	312
buf1024	1024	754	270	1080
rec8	1024	956	68	272
rec32	1024	812	212	848
rawprintf	1024	992	32	128
snprintf	1024	907	117	468
printf	1024	940	84	336
buf1k-64B	1024	754	270	1080
report task itself: HWM=229 of 256
idle task: HWM=230 of 256
[board_exit] code=0
exit=0
/Users/donh/.rtos_scratch/A4/ex4.c:11:16:leaf	0	static
/Users/donh/.rtos_scratch/A4/ex4.c:12:16:buf256	256	static
/Users/donh/.rtos_scratch/A4/ex4.c:13:16:buf1k	1024	static
/Users/donh/.rtos_scratch/A4/ex4.c:14:20:rec	24	static
/Users/donh/.rtos_scratch/A4/ex4.c:35:13:report	32	static
/Users/donh/.rtos_scratch/A4/ex4.c:33:13:worker	48	static
/Users/donh/.rtos_scratch/A4/ex4.c:51:5:main	32	static
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:4064:12:xTaskResumeAll	48	static
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:2506:10:vTaskDelay	8	static
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:5215:10:vTaskSwitchContext	16	static
```

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
<text x="370" y="18" text-anchor="middle" font-size="13">태스크 모양별 실제 스택 사용량 (스택 4096 B 중, 측정)</text>
<text x="112" y="48" text-anchor="end" font-size="12">leaf</text>
<rect x="120" y="36" width="53.3" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="179.3" y="48" font-size="12">128 B</text>
<text x="112" y="74" text-anchor="end" font-size="12">rawprintf</text>
<rect x="120" y="62" width="53.3" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="179.3" y="74" font-size="12">128 B</text>
<text x="112" y="100" text-anchor="end" font-size="12">rec8</text>
<rect x="120" y="88" width="113.3" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="239.3" y="100" font-size="12">272 B</text>
<text x="112" y="126" text-anchor="end" font-size="12">buf256</text>
<rect x="120" y="114" width="130.0" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="256.0" y="126" font-size="12">312 B</text>
<text x="112" y="152" text-anchor="end" font-size="12">printf</text>
<rect x="120" y="140" width="140.0" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="266.0" y="152" font-size="12">336 B</text>
<text x="112" y="178" text-anchor="end" font-size="12">snprintf</text>
<rect x="120" y="166" width="195.0" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="321.0" y="178" font-size="12">468 B</text>
<text x="112" y="204" text-anchor="end" font-size="12">rec32</text>
<rect x="120" y="192" width="353.3" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="479.3" y="204" font-size="12">848 B</text>
<text x="112" y="230" text-anchor="end" font-size="12">buf1024</text>
<rect x="120" y="218" width="450.0" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="576.0" y="230" font-size="12">1080 B</text>
<text x="112" y="256" text-anchor="end" font-size="12">buf1k-64B</text>
<rect x="120" y="244" width="450.0" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="576.0" y="256" font-size="12">1080 B</text>
<line x1="120" y1="272" x2="620" y2="272" stroke="currentColor"/>
<line x1="120" y1="30" x2="120" y2="272" stroke="currentColor"/>
<line x1="120.0" y1="272" x2="120.0" y2="277" stroke="currentColor"/>
<text x="120.0" y="290" text-anchor="middle" font-size="12">0</text>
<line x1="203.3" y1="272" x2="203.3" y2="277" stroke="currentColor"/>
<text x="203.3" y="290" text-anchor="middle" font-size="12">200</text>
<line x1="286.7" y1="272" x2="286.7" y2="277" stroke="currentColor"/>
<text x="286.7" y="290" text-anchor="middle" font-size="12">400</text>
<line x1="370.0" y1="272" x2="370.0" y2="277" stroke="currentColor"/>
<text x="370.0" y="290" text-anchor="middle" font-size="12">600</text>
<line x1="453.3" y1="272" x2="453.3" y2="277" stroke="currentColor"/>
<text x="453.3" y="290" text-anchor="middle" font-size="12">800</text>
<line x1="536.7" y1="272" x2="536.7" y2="277" stroke="currentColor"/>
<text x="536.7" y="290" text-anchor="middle" font-size="12">1000</text>
<line x1="620.0" y1="272" x2="620.0" y2="277" stroke="currentColor"/>
<text x="620.0" y="290" text-anchor="middle" font-size="12">1200</text>
<line x1="173.3" y1="30" x2="173.3" y2="272" stroke="#888" stroke-dasharray="4 3"/>
<text x="120" y="310" font-size="12">점선 = 기본선 128 B (태스크 루프 + vTaskDelay + 저장된 문맥 16워드)</text>
<rect x="120" y="320" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="140" y="331" font-size="12">지역 배열이 있는 태스크 (buf1k-64B: 1 KB 배열 중 64 B만 써도 1080 B로 잡힌다)</text>
</svg>
```

그림 6 — 예제 6의 측정값(사용 바이트 = (1024 − HWM) × 4). 주황 막대가 지역 배열을 가진 태스크다.

**출력에서 볼 것**:

- **기본선 128 B(32 words)**: 아무 일도 안 하는 `leaf`도 128 B를 쓴다. `worker` 프레임 48 B(`body`가 인라인됨) + `vTaskDelay` 8 B + 블록될 때 쌓이는 문맥 16워드(64 B) ≈ 120 B에 정렬이 더해진 값이다. 즉 **어떤 태스크든 최소 30워드 남짓은 문맥 저장과 루프 골격에 나간다**. `configMINIMAL_STACK_SIZE`(이 템플릿 256 words)의 idle이 230 words 남긴 것도 같은 이야기다.
- **지역 배열은 그대로 더해진다**: `buf256`은 기본선 + 256 B 남짓(312 B), `buf1024`는 + 1 KB(1080 B). `-fstack-usage`의 256, 1024와 맞는다.
- **`buf1k-64B` = `buf1024`**: 1 KB 배열 중 앞 64 B만 써도 1080 B로 잡힌다. `b[0]`이 배열의 가장 낮은 주소(= 스택의 가장 깊은 곳)이기 때문이다. high water mark는 "얼마나 많이 썼나"가 아니라 **"가장 깊이 어디까지 건드렸나"**를 잰다. 반대로 배열의 뒤쪽만 쓰는 코드라면 실제로는 프레임이 깊이 내려갔는데 0xa5가 안 지워져서 사용량이 **작게** 보일 수 있다 — 측정만 믿지 말고 `-fstack-usage` 같은 정적 값과 함께 보는 이유(F3).
- **재귀**: `rec` 한 단은 24 B(`-fstack-usage`) + 복귀 주소 등. 8단 272 B, 32단 848 B — 깊이에 선형으로 늘어난다. MCU에서 재귀를 피하라는 규칙(MISRA C:2012 Rule 17.2)의 실감 나는 이유다.
- **printf 계열**: newlib-nano `snprintf`가 468 B, `printf`가 336 B를 썼다(이 짧은 포맷에서). 포맷·인자에 따라 더 늘어난다. 랩의 `board_printf_raw` 같은 작은 포맷터는 기본선과 같다. "로그 한 줄 넣었더니 HardFault"의 흔한 원인이 이것이다.

### 5.5 예제 7 — 넘치면 어떻게 잡히나 (`configCHECK_FOR_STACK_OVERFLOW = 2`)

overflow 검사 방식은 F3에서 자세히 다룬다. 여기서는 데모 수준으로 "언제, 무엇을 보고" 잡는지만 본다. 방식 2의 매크로(`include/stack_macros.h`, 스택이 아래로 자라는 포트)는 컨텍스트 스위치 때(`vTaskSwitchContext` 안의 `taskCHECK_FOR_STACK_OVERFLOW()`) **나가는 태스크**에 대해 두 가지를 본다: 저장된 SP가 `pxStack` 아래로 내려갔나(방식 1과 같은 검사), 그리고 스택 바닥 16바이트(`pulStack[0..3]`)가 여전히 `0xa5a5a5a5`인가. 어긋나면 `vApplicationStackOverflowHook(handle, name)`을 부른다.

**무엇을 확인하나**: 512 B 스택 태스크가 600 B 지역 배열을 쓰면 훅이 언제 불리는지. 넘친 바이트가 떨어질 곳으로 victim 바로 아래에 512 B 블록을 미리 하나 잡아 둔다(그림 3의 "이전 블록").

```c
/* ex5: configCHECK_FOR_STACK_OVERFLOW=2 — 스택 바닥 16바이트 0xa5 패턴 검사가 overflow를 잡는 순간 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static uint32_t t_overflow;
static TaskHandle_t hOv;

void vApplicationStackOverflowHook(TaskHandle_t t, char *name)   /* 템플릿의 weak 훅을 덮어씀 */
{
    uint32_t *bottom = (uint32_t *)((uint32_t *)t)[12];          /* TCB word[12] = pxStack */
    board_printf_raw("[%u us] StackOverflowHook: task=%s (overflow happened at %u us)\n",
                     board_now_us(), name, t_overflow);
    board_printf_raw("  pxStack[0..3] = %x %x %x %x (expected a5a5a5a5)\n",
                     bottom[0], bottom[1], bottom[2], bottom[3]);
    board_exit(251);
}

static __attribute__((noinline)) void deep(void)
{
    volatile uint8_t b[600];                                     /* 512 B 스택에 600 B 지역 배열 */
    for (unsigned i = 0; i < sizeof b; i++) b[i] = 0;          /* memset((void*)b)는 최적화로 지워진다 */
    t_overflow = board_now_us();
}

static void victim(void *pv)
{
    (void)pv;
    board_printf_raw("[%u us] victim: stack=128 words, HWM before=%u\n",
                     board_now_us(), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    deep();                                                      /* 여기서 스택 바닥을 넘어 씀 */
    board_printf_raw("[%u us] victim: returned from deep(), still running...\n", board_now_us());
    for (;;) vTaskDelay(1);                                      /* 다음 컨텍스트 스위치에서 검사 */
}

static void other(void *pv) { (void)pv; for (;;) vTaskDelay(1); }

int main(void)
{
    (void)pvPortMalloc(512);                 /* victim 스택 아래쪽 이웃 블록 (넘쳐 쓴 바이트가 떨어질 곳) */
    xTaskCreate(victim, "victim", 128, NULL, 2, &hOv);
    xTaskCreate(other, "other", 256, NULL, 1, NULL);
    vTaskStartScheduler();
}
```

```text
   text	   data	    bss	    dec	    hex	filename
  12108	    100	 135408	 147616	  240a0	/var/tmp/rtos/A4/ex5/app.elf
[144 us] victim: stack=128 words, HWM before=118
[303 us] victim: returned from deep(), still running...
[332 us] StackOverflowHook: task=victim (overflow happened at 303 us)
  pxStack[0..3] = 0 0 0 0 (expected a5a5a5a5)
[board_exit] code=251
exit=1
```

**출력에서 볼 것**:

- overflow는 303 us에 일어났는데 훅은 332 us에 불렸다. 그 29 us 동안 victim은 **넘친 줄도 모르고** printf까지 했다. 방식 2는 "넘치는 순간"이 아니라 **다음 컨텍스트 스위치 때** 검사한다. 그 사이에 이웃 메모리(여기서는 미리 잡아 둔 512 B 블록, 실전에서는 다른 태스크의 TCB)가 이미 망가졌을 수 있다.
- 훅 안에서 본 스택 바닥 16바이트가 0으로 덮여 있다 — 이게 검출 근거다. 배열이 바닥 16바이트를 건너뛰고 그 아래만 썼다면(큰 배열의 일부만 쓰는 코드) 이 검사는 놓친다. 그래서 진짜 보호는 MPU 가드 리전이나 Armv8-M의 PSPLIM 같은 **하드웨어** 검사다(F3, F4).
- 디버깅 일화: 처음에 `memset((void *)b, 0, sizeof b)`로 썼더니 훅이 안 불리고 시스템이 멀쩡히 돌았다. `objdump -d`로 `deep`을 보니 `memset`이 통째로 사라져 있었다 — `volatile`을 캐스트로 벗긴 배열은 함수가 끝나면 죽는 메모리라서 컴파일러가 dead store로 지웠다. 스택 테스트 코드는 **생성 코드를 확인**해야 한다.

### 5.6 스택 크기를 정하는 실무 규칙 (자세히는 F3)

1. 처음에는 넉넉하게 준다(예: 2~4 KB).
2. 최악 경로를 다 실행시키는 스트레스 테스트를 돌린 뒤 `uxTaskGetStackHighWaterMark()`(또는 `vTaskListTasks`의 HWM 열, §7)를 읽는다.
3. 사용량 + 마진(팀마다 다르지만 흔히 20~50%, 그리고 FPU 문맥이 있으면 그만큼 더)으로 줄인다.
4. `-fstack-usage`와 호출 그래프 도구로 정적 상한을 함께 확인한다(측정이 놓친 경로 대비).
5. 양산에서도 `configCHECK_FOR_STACK_OVERFLOW 2` + MPU 가드를 켜 두고, 훅에서 태스크 이름을 noinit RAM에 남긴 뒤 리셋한다(F8, E8).

---

## 6. 정적 vs 동적 생성

### 6.1 네 가지 API

| API | 메모리 | 크기 확정 시점 | 실패 가능 | 대표 용도 |
|---|---|---|---|---|
| FreeRTOS `xTaskCreate()` | `pvPortMalloc`(heap_1~5)으로 스택 + TCB | 런타임 | 예 (`errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`) | 일반 애플리케이션, 프로토타입 |
| FreeRTOS `xTaskCreateStatic()` | 호출자가 준 `StackType_t[]` + `StaticTask_t` | 링크 시점 (.bss) | 아니오 (인자 NULL이면 NULL 반환) | 인증·안전 프로젝트, heap 없는 시스템 |
| Zephyr `k_thread_create()` | 호출자가 준 `K_THREAD_STACK_DEFINE` 스택 + `struct k_thread` | 링크 시점 (보통 정적 객체) | 아니오 | 런타임에 시작 시점·인자를 정할 때 |
| Zephyr `K_THREAD_DEFINE()` | 매크로가 스택과 `k_thread`를 정적으로 정의 | 링크 시점, 부팅 때 자동 시작 | 아니오 | 고정된 스레드 세트 (가장 흔함) |

Zephyr는 기본이 정적이다. 동적 스택도 있긴 하다(`k_thread_stack_alloc()`, `CONFIG_DYNAMIC_THREAD`). FreeRTOS는 `configSUPPORT_STATIC_ALLOCATION`/`configSUPPORT_DYNAMIC_ALLOCATION`으로 둘 다 또는 하나만 켠다. 이 랩은 둘 다 켰고 `configKERNEL_PROVIDED_STATIC_MEMORY 1`이라 idle·timer 태스크 메모리를 커널이 정적으로 제공한다.

### 6.2 예제 8 — 태스크 하나가 RAM을 얼마나 먹나

**무엇을 확인하나**: `xTaskCreate` 전후 `xPortGetFreeHeapSize()` 차이(스택 크기별), `xTaskCreateStatic`의 heap 사용량, heap보다 큰 요청이 실패하는 모습, 스케줄러 시작이 heap을 쓰는지. 측정은 스케줄러가 돈 뒤 태스크 안에서 한다(왜 `main`에서 하면 안 되는지는 예제 9).

```c
/* ex6: 태스크 하나가 RAM을 얼마나 먹나 — 동적(xTaskCreate) vs 정적(xTaskCreateStatic), heap_4 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void idle_like(void *pv) { (void)pv; for (;;) vTaskDelay(100); }
void vApplicationMallocFailedHook(void) { board_printf_raw("  [MallocFailedHook] called\n"); }

#define SDEPTH 256
static StackType_t  s_stack[SDEPTH];                 /* .bss에 들어간다 (링크 시점에 크기 확정) */
static StaticTask_t s_tcb;
static size_t before_start, before_any;

static void meas(void *pv)
{
    (void)pv;
    const UBaseType_t depth[] = { 64, 128, 256, 512, 1024 };
    TaskHandle_t h;
    board_printf_raw("heap free: before any malloc=%u, before vTaskStartScheduler=%u, in first task=%u\n",
                     (unsigned)before_any, (unsigned)before_start, (unsigned)xPortGetFreeHeapSize());
    board_printf_raw("sizeof(TCB)=sizeof(StaticTask_t)=%u, sizeof(StackType_t)=%u\n",
                     (unsigned)sizeof(StaticTask_t), (unsigned)sizeof(StackType_t));
    board_printf_raw("depth(words)  stack bytes  heap used  overhead\n");
    for (int i = 0; i < 5; i++) {
        size_t f0 = xPortGetFreeHeapSize();
        xTaskCreate(idle_like, "dyn", depth[i], NULL, 1, &h);
        size_t used = f0 - xPortGetFreeHeapSize();
        board_printf_raw("%u\t\t%u\t\t%u\t\t%u\n", (unsigned)depth[i], (unsigned)(depth[i] * 4),
                         (unsigned)used, (unsigned)(used - depth[i] * 4 - sizeof(StaticTask_t)));
        vTaskDelete(h);                               /* 남의 태스크 삭제 → 그 자리에서 free */
        if (xPortGetFreeHeapSize() != f0) board_printf_raw("  leak!\n");
    }
    size_t f0 = xPortGetFreeHeapSize();
    h = xTaskCreateStatic(idle_like, "static", SDEPTH, NULL, 1, s_stack, &s_tcb);
    board_printf_raw("static: handle==&s_tcb? %s, heap used=%u, .bss cost=%u\n",
                     (void *)h == (void *)&s_tcb ? "yes" : "no",
                     (unsigned)(f0 - xPortGetFreeHeapSize()), (unsigned)(sizeof s_stack + sizeof s_tcb));
    BaseType_t r = xTaskCreate(idle_like, "huge", 40000, NULL, 1, NULL);   /* 160 KB > heap 128 KB */
    board_printf_raw("xTaskCreate(40000 words) = %d (errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY=%d)\n",
                     (int)r, (int)errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY);
    board_printf_raw("min ever free heap = %u\n", (unsigned)xPortGetMinimumEverFreeHeapSize());
    board_exit(0);
}

int main(void)
{
    before_any = xPortGetFreeHeapSize();              /* heap_4는 첫 malloc 때 초기화된다 */
    xTaskCreate(meas, "meas", 512, NULL, 3, NULL);
    before_start = xPortGetFreeHeapSize();
    vTaskStartScheduler();
}
```

```sh
/Users/donh/.rtos_scratch/A4/run.sh ex6
arm-none-eabi-nm -S --size-sort /var/tmp/rtos/A4/ex6/app.elf | grep -E "s_stack|s_tcb|uxIdleTaskStack|xIdleTaskTCB|uxTimerTaskStack|xTimerTaskTCB|ucHeap|xStaticTimerQueue|ucStaticTimerQueueStorage"
```

```text
   text	   data	    bss	    dec	    hex	filename
  12812	    100	 136528	 149440	  247c0	/var/tmp/rtos/A4/ex6/app.elf
heap free: before any malloc=0, before vTaskStartScheduler=128896, in first task=128896
sizeof(TCB)=sizeof(StaticTask_t)=96, sizeof(StackType_t)=4
depth(words)  stack bytes  heap used  overhead
64		256		368		16
128		512		624		16
256		1024		1136		16
512		2048		2160		16
1024		4096		4208		16
static: handle==&s_tcb? yes, heap used=0, .bss cost=1120
  [MallocFailedHook] called
xTaskCreate(40000 words) = -1 (errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY=-1)
min ever free heap = 124688
[board_exit] code=0
exit=0
20001318 00000054 b xStaticTimerQueue.0
20000070 00000060 b s_tcb
20001130 00000060 b xIdleTaskTCB.3
20000cd0 00000060 b xTimerTaskTCB.1
2000136c 000000a0 b ucStaticTimerQueueStorage.1
200000d0 00000400 b s_stack
20000d30 00000400 b uxIdleTaskStack.2
200004d0 00000800 b uxTimerTaskStack.0
20001464 00020000 b ucHeap
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<text x="380" y="18" text-anchor="middle" font-size="13">태스크 1개의 RAM 비용 (측정: heap 감소량 / sizeof)</text>
<text x="182" y="48" text-anchor="end" font-size="12">xTaskCreate 64w</text>
<rect x="190.0" y="36" width="23.9" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="213.9" y="36" width="8.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="222.8" y="36" width="1.5" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="230.3" y="48" font-size="12">368 B</text>
<text x="182" y="74" text-anchor="end" font-size="12">xTaskCreate 128w</text>
<rect x="190.0" y="62" width="47.7" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="237.7" y="62" width="8.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="246.7" y="62" width="1.5" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="254.2" y="74" font-size="12">624 B</text>
<text x="182" y="100" text-anchor="end" font-size="12">xTaskCreate 256w</text>
<rect x="190.0" y="88" width="95.4" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="285.4" y="88" width="8.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="294.4" y="88" width="1.5" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="301.9" y="100" font-size="12">1136 B</text>
<text x="182" y="126" text-anchor="end" font-size="12">xTaskCreate 512w</text>
<rect x="190.0" y="114" width="190.8" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="380.8" y="114" width="8.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="389.8" y="114" width="1.5" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="397.3" y="126" font-size="12">2160 B</text>
<text x="182" y="152" text-anchor="end" font-size="12">xTaskCreate 1024w</text>
<rect x="190.0" y="140" width="381.7" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="571.7" y="140" width="8.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="580.6" y="140" width="1.5" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="588.1" y="152" font-size="12">4208 B</text>
<text x="182" y="178" text-anchor="end" font-size="12">xTaskCreateStatic 256w</text>
<rect x="190.0" y="166" width="95.4" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="285.4" y="166" width="8.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="300.4" y="178" font-size="12">1120 B</text>
<text x="182" y="204" text-anchor="end" font-size="12">Zephyr 1024 B</text>
<rect x="190.0" y="192" width="95.4" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="285.4" y="192" width="17.9" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="309.3" y="204" font-size="12">1216 B</text>
<line x1="190" y1="220" x2="600" y2="220" stroke="currentColor"/>
<line x1="190.0" y1="220" x2="190.0" y2="225" stroke="currentColor"/>
<text x="190.0" y="238" text-anchor="middle" font-size="12">0</text>
<line x1="283.2" y1="220" x2="283.2" y2="225" stroke="currentColor"/>
<text x="283.2" y="238" text-anchor="middle" font-size="12">1000</text>
<line x1="376.4" y1="220" x2="376.4" y2="225" stroke="currentColor"/>
<text x="376.4" y="238" text-anchor="middle" font-size="12">2000</text>
<line x1="469.5" y1="220" x2="469.5" y2="225" stroke="currentColor"/>
<text x="469.5" y="238" text-anchor="middle" font-size="12">3000</text>
<line x1="562.7" y1="220" x2="562.7" y2="225" stroke="currentColor"/>
<text x="562.7" y="238" text-anchor="middle" font-size="12">4000</text>
<rect x="20" y="253" width="14" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="40" y="264" font-size="12">스택</text>
<rect x="110" y="253" width="14" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="130" y="264" font-size="12">TCB (96 B / k_thread 192 B)</text>
<rect x="360" y="253" width="14" height="14" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="380" y="264" font-size="12">heap_4 블록 헤더 8 B × 2</text>
<text x="20" y="288" font-size="12">정적 생성 두 줄은 heap 0 B — 같은 바이트가 .bss로 옮겨 가서 링크 시점에 확정된다</text>
</svg>
```

그림 7 — 예제 8과 예제 11에서 잰 "태스크 1개의 RAM 비용". 동적 생성은 정확히 `4 × depth + 96 + 16` 바이트다.

**출력에서 볼 것**:

- **공식**: `heap 사용량 = 4 × depth(words) + sizeof(TCB) 96 + 16`. 16은 heap_4가 블록마다 붙이는 8바이트 헤더(`BlockLink_t`) 두 개(스택 블록, TCB 블록)다. 크기가 8의 배수가 아니면 정렬 때문에 더 붙는다. TCB 자체는 작다 — **태스크 RAM의 대부분은 스택**이다.
- **정적 생성**: heap 0 B, 대신 `s_stack`(0x400 = 1024 B) + `s_tcb`(0x60 = 96 B) = 1120 B가 .bss에 있다. 핸들이 곧 `&s_tcb`다. 바이트 수는 거의 같고 **"언제 실패를 아느냐"**가 다르다: 정적은 RAM이 모자라면 링크가 실패한다(빌드 시점), 동적은 런타임에 `-1`이 돌아온다(현장에서).
- **heap_4는 첫 할당 때 초기화된다**: `before any malloc=0`. `xPortGetFreeHeapSize()`를 아무 할당 전에 읽으면 0이 나온다 — "heap이 0이다!"라고 놀라지 말 것.
- **스케줄러 시작은 heap을 안 쓴다**(128896 → 128896): idle 태스크(`uxIdleTaskStack` 1 KB + `xIdleTaskTCB`)와 timer 태스크(`uxTimerTaskStack` 2 KB + TCB), timer 큐가 전부 .bss에 정적으로 있다(`configKERNEL_PROVIDED_STATIC_MEMORY`). 이 3.3 KB는 태스크를 하나도 안 만들어도 이미 나가는 고정비다.
- **할당 실패**: 160 KB 요청은 `vApplicationMallocFailedHook`을 부르고 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`(-1)를 돌려준다. 반환값을 무시하고 핸들을 쓰면 NULL 역참조다. `xPortGetMinimumEverFreeHeapSize()`(124688 = 128896 − 4208)는 "가장 빠듯했던 순간"을 기억한다 — 양산 텔레메트리에 넣을 값이다.

### 6.3 예제 9 — 스케줄러 시작 전에 `vTaskDelete`하면

예제 8의 첫 버전은 측정을 `main()`에서 했다(만들고 지우고를 반복). 결과는 스케줄러 시작 순간의 HardFault였다. 원인을 좁힌 최소 재현:

**무엇을 확인하나**: 스케줄러가 시작되기 전에는 `xTaskCreate`가 `pxCurrentTCB`를 "지금까지 가장 높은 우선순위로 만든 태스크"로 바꿔 둔다(`prvAddNewTaskToReadyList`: `if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority ) pxCurrentTCB = pxNewTCB;`). 그 태스크를 지우면?

```c
/* ex6b: 스케줄러 시작 전에 vTaskDelete — pxCurrentTCB가 해제된 TCB를 가리킨 채 남는다 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void A(void *pv) { (void)pv; board_printf_raw("A runs?! (A was deleted before start)\n"); board_exit(1); }
static void B(void *pv) { (void)pv; board_printf_raw("B runs (expected)\n"); board_exit(0); }

int main(void)
{
    TaskHandle_t hA;
    xTaskCreate(A, "A", 128, NULL, 2, &hA);     /* pxCurrentTCB = A (지금까지 가장 높은 우선순위) */
    vTaskDelete(hA);                            /* 스케줄러 전: TCB·스택 즉시 free, pxCurrentTCB는 그대로 */
    xTaskCreate(B, "B", 512, NULL, 1, NULL);    /* prio 1 < (해제된) A의 2 → pxCurrentTCB 안 바뀜 */
    board_printf_raw("B created; starting scheduler\n");
    vTaskStartScheduler();
}
```

```text
   text	   data	    bss	    dec	    hex	filename
  12088	    100	 135400	 147588	  24084	/var/tmp/rtos/A4/ex6b/app.elf
B created; starting scheduler

[HardFault] PC=0x1e2a LR=0xfffffff9 xPSR=0x2100000b CFSR=0x1000000 HFSR=0x40000000
[board_exit] code=255
exit=1
```

원인 추적(실제로 돌린 명령):

```sh
arm-none-eabi-addr2line -f -e /var/tmp/rtos/A4/ex6b/app.elf 0x1e2a      # → SVC_Handler, port.c:230
arm-none-eabi-objdump -d /var/tmp/rtos/A4/ex6b/app.elf | grep -A4 "<SVC_Handler>:"
# gdb: vTaskStartScheduler에 중단점을 걸고 pxCurrentTCB를 본다
```

```text
SVC_Handler
/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:230
00001e24 <SVC_Handler>:
    1e24:	4b07      	ldr	r3, [pc, #28]	@ (1e44 <SVC_Handler+0x20>)
    1e26:	6819      	ldr	r1, [r3, #0]
    1e28:	6808      	ldr	r0, [r1, #0]
    1e2a:	e8b0 0ff0 	ldmia.w	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
$1 = (TCB_t * volatile) 0x20001210 <ucHeap+532>
$2 = 0xa5a5a5a5
$3 = '\245' <repeats 16 times>
$4 = 1
$5 = "B", '\000' <repeats 14 times>
$6 = (TCB_t *) 0x20001810 <ucHeap+2068>
```

(`$1` = `pxCurrentTCB`, `$2` = 그 `pxTopOfStack`, `$3` = 그 `pcTaskName`, `$4` = `pxReadyTasksLists[1]`의 항목 수, `$5`·`$6` = 그 리스트에 있는 진짜 B의 이름과 TCB 주소.)

**출력에서 볼 것**: `vTaskDelete`는 스케줄러가 안 돌 때 "실행 중인 태스크"가 없다고 보고 TCB와 스택을 **그 자리에서 free**한다(`xSchedulerRunning == pdFALSE`면 termination 리스트를 거치지 않는다). 그런데 `pxCurrentTCB`는 갱신하지 않는다. 다음 `xTaskCreate(B)`는 큰 스택(2 KB)을 heap 앞쪽부터 잡았고, 그 스택을 0xa5로 채우면서 **A가 있던 자리(0x20001210)를 덮었다** — 이름까지 0xa5(`'\245'`)다. B 자신은 ready[1]에 멀쩡히 있지만(0x20001810), 스케줄러는 `pxCurrentTCB`를 믿고 첫 태스크를 꺼낸다: SVC 핸들러가 `[pxCurrentTCB]` → `pxTopOfStack` = 0xa5a5a5a5를 SP로 읽고, `ldmia`가 정렬되지 않은 주소를 읽다가 UsageFault(CFSR = 0x01000000 = UNALIGNED) → HardFault로 승격(HFSR FORCED)됐다. §2.4의 "첫 워드 = SP" 규칙이 그대로 사고 경로가 됐다.

교훈: **스케줄러 시작 전에는 태스크를 지우지 않는다.** 필요 없는 태스크는 만들지 말거나, 시작 후에 지운다. 측정·테스트 코드는 태스크 안에서 돌린다.

### 6.4 언제 무엇을 쓰나

- **인증·안전 프로젝트(DO-178C, ISO 26262, IEC 61508)**: 런타임 할당을 피하는 정책이 흔하다 → `xTaskCreateStatic` 또는 Zephyr `K_THREAD_DEFINE`. 모든 RAM이 링크 맵에 보이고, "부팅 후 메모리 부족"이 원리적으로 없다(F1, O 모듈).
- **일반 제품**: 부팅 때 한 번만 `xTaskCreate`로 만들고 절대 지우지 않는다 → 단편화가 없으니 heap_1·heap_4 어느 쪽이든 결정적이다. 반환값은 반드시 검사한다.
- **런타임에 만들고 지우는 태스크**(접속마다 하나 등): 피한다. 꼭 필요하면 태스크 풀을 미리 만들어 재사용하거나, 큐로 일을 넘기는 worker 패턴을 쓴다.

---

## 7. 우선순위 맛보기와 태스크 목록 보기

### 7.1 번호 규칙 (깊이는 C4)

- **FreeRTOS**: 0 ~ `configMAX_PRIORITIES − 1`(이 랩 0~7). **숫자가 클수록 높다.** idle 태스크는 `tskIDLE_PRIORITY` = 0. timer 서비스 태스크는 `configTIMER_TASK_PRIORITY`(이 랩 7). Ready 태스크 중 가장 높은 것이 Running이 되고, 같은 우선순위가 여럿이면 `configUSE_TIME_SLICING 1`일 때 tick마다 돌아가며 실행된다(round-robin time slicing).
- **Zephyr**: **숫자가 작을수록 높다.** 음수는 협력형(cooperative, 선점되지 않음), 0 이상은 선점형. 이 빌드에서 main은 0, idle은 15(`CONFIG_NUM_PREEMPT_PRIORITIES=15`, 가장 낮음). time slicing은 `CONFIG_TIMESLICING`(이 빌드 y, 20 ms).
- Cortex-M NVIC 인터럽트 우선순위(숫자가 작을수록 높다)와 FreeRTOS 태스크 우선순위(클수록 높다)는 **방향이 반대**다. 태스크 우선순위와 인터럽트 우선순위는 완전히 다른 축이고, 모든 ISR은 모든 태스크보다 먼저다(B3, E1).

### 7.2 예제 10 — `vTaskListTasks`와 run-time stats

**무엇을 확인하나**: 같은 우선순위 1의 바쁜 태스크 두 개(time slicing), 5 ms마다 1 ms 일하는 우선순위 3 "제어 루프", 200 ms 뒤에 보고하는 우선순위 4 모니터. V11에서 `vTaskList()`/`vTaskGetRunTimeStats()`는 버퍼 길이를 받는 `vTaskListTasks()`/`vTaskGetRunTimeStatistics()`의 매크로 별칭이 되었다(`task.h`). 이 템플릿은 run-time 카운터로 `board_now_us()`(1 us 해상도)를 쓴다.

```c
/* ex7: vTaskListTasks / vTaskGetRunTimeStatistics — 우선순위, 같은 우선순위 time slicing, idle */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static char buf[1024];
static volatile uint32_t cnt[2];

static void hog(void *pv)                     /* 같은 우선순위 1인 바쁜 태스크 두 개 */
{
    int i = (int)pv;
    for (;;) cnt[i]++;
}

static void ctl(void *pv)                     /* 우선순위 3: 5 ms마다 1 ms쯤 일하는 "제어 루프" */
{
    (void)pv;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t t0 = board_now_us();
        while (board_now_us() - t0 < 1000) { }
        xTaskDelayUntil(&last, pdMS_TO_TICKS(5));
    }
}

static void mon(void *pv)                     /* 우선순위 4: 200 ms 뒤 한 번 보고 */
{
    (void)pv;
    vTaskDelay(pdMS_TO_TICKS(200));
    vTaskListTasks(buf, sizeof buf);
    board_printf_raw("Name\t\tState\tPrio\tHWM\tNum\n%s\n", buf);
    vTaskGetRunTimeStatistics(buf, sizeof buf);
    board_printf_raw("Name\t\tAbs(us)\t\tPercent\n%s\n", buf);
    board_printf_raw("hog0 loops=%u, hog1 loops=%u\n", cnt[0], cnt[1]);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(hog, "hog0", 128, (void *)0, 1, NULL);
    xTaskCreate(hog, "hog1", 128, (void *)1, 1, NULL);
    xTaskCreate(ctl, "ctl", 256, NULL, 3, NULL);
    xTaskCreate(mon, "mon", 512, NULL, 4, NULL);
    vTaskStartScheduler();
}
```

```text
Name            State   Prio    HWM     Num
mon             X       4       470     4
ctl             R       3       232     3
hog0            R       1       118     1
hog1            R       1       118     2
IDLE            R       0       246     5
Tmr Svc         B       7       480     6

Name                Abs(us)                Percent
mon                    1346                <1%
ctl                    40360                19%
hog0                   79545                39%
hog1                   80081                39%
IDLE                   0                <1%
Tmr Svc                198                <1%

hog0 loops=618390, hog1 loops=622120
[board_exit] code=0
exit=0
```

(탭을 `expand -t 8`로 공백으로 펼쳤다. 첫 줄 크기 표시는 생략.)

**출력에서 볼 것**:

- State 글자: `X` = Running(`tskRUNNING_CHAR`), `R` = Ready, `B` = Blocked, `S` = Suspended, `D` = Deleted(`tasks.c`의 `tsk…_CHAR` 매크로). 보고하는 `mon` 자신이 `X`다. `ctl`이 `R`인 것은 200 ms가 5 ms 주기의 배수라 같은 tick에 같이 깨어났는데 `mon`이 더 높아서 줄 서 있기 때문이다.
- HWM 열이 §5의 high water mark(words)다. 양산 펌웨어에서 이 표를 디버그 셸 명령 하나로 찍게 해 두면 스택 문제를 현장에서 바로 본다.
- run-time stats: `ctl` 19%(5 ms 중 1 ms = 20%), `hog0`·`hog1`이 39%씩 — 같은 우선순위끼리 tick마다 번갈아 돌아서 반반이다(루프 횟수 618390 대 622120). **IDLE은 0%**. 우선순위 1에서 블로킹 없이 도는 태스크가 있으면 idle은 영원히 못 돈다 → idle hook(저전력 진입, WFI)도, 자기 삭제한 태스크의 메모리 회수도 일어나지 않는다. 바쁜 루프 태스크를 idle 바로 위에 두는 설계가 위험한 이유다.

---

## 8. Zephyr로 같은 일 — 예제 11

**무엇을 확인하나**: Zephyr v4.4.2에서 `K_THREAD_DEFINE`(빌드 시점 정의, 부팅 때 자동 시작)과 `K_THREAD_STACK_DEFINE` + `k_thread_create`(런타임 시작)로 스레드를 만들고, 한 스레드를 queued → pending → pending+suspended → pending → queued → dead로 몰면서 `k_thread_state_str()`로 찍는다. `k_thread_foreach()`로 스레드 목록(우선순위, 상태, 스택 크기, 미사용 바이트)을 찍고, 마지막에 thread analyzer 출력을 본다. FreeRTOS와 **같은 QEMU 보드**다.

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_THREAD_NAME=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_STACK_INFO=y
CONFIG_INIT_STACKS=y
CONFIG_THREAD_ANALYZER=y
CONFIG_THREAD_ANALYZER_USE_PRINTK=y
```

```c
/* zapp: Zephyr v4.4.2 — K_THREAD_DEFINE(정적) + k_thread_create, 상태 문자열, 스레드 목록 */
#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>

#define STK 1024                                    /* Zephyr 스택 크기는 "바이트" */
static K_SEM_DEFINE(sem, 0, 1);

static void sleeper(void *p1, void *p2, void *p3) { for (;;) k_msleep(1000); }
K_THREAD_DEFINE(sleeper_tid, STK, sleeper, NULL, NULL, NULL, 5, 0, 0);  /* 빌드 시점 생성, 부팅 때 시작 */

K_THREAD_STACK_DEFINE(w_stack, STK);
static struct k_thread w_thread;
static void waiter(void *p1, void *p2, void *p3)
{
	k_sem_take(&sem, K_FOREVER);                    /* pending */
	printk("  waiter: got sem, returning from entry (OK in Zephyr)\n");
}

static void show(const char *ev, k_tid_t t)
{
	char b[32];
	printk("%-28s waiter state = %s\n", ev, k_thread_state_str(t, b, sizeof b));
}

static void cb(const struct k_thread *t, void *u)
{
	size_t unused = 0; char b[32];
	k_thread_stack_space_get(t, &unused);
	printk("  %-12s prio %3d  state %-10s stack %4u B  unused %4u B\n", k_thread_name_get((k_tid_t)t),
	       k_thread_priority_get((k_tid_t)t), k_thread_state_str((k_tid_t)t, b, sizeof b),
	       (unsigned)t->stack_info.size, (unsigned)unused);
}

int main(void)
{
	printk("sizeof(struct k_thread) = %u, sizeof(_thread_base) = %u, callee_saved = %u\n",
	       (unsigned)sizeof(struct k_thread), (unsigned)sizeof(struct _thread_base),
	       (unsigned)sizeof(struct _callee_saved));
	k_tid_t w = k_thread_create(&w_thread, w_stack, K_THREAD_STACK_SIZEOF(w_stack), waiter,
				    NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(w, "waiter");
	show("after k_thread_create", w);
	k_msleep(1);   show("waiter blocked on k_sem_take", w);
	k_thread_suspend(w); show("after k_thread_suspend", w);
	k_thread_resume(w);  show("after k_thread_resume", w);
	printk("thread list (k_thread_foreach):\n");
	k_thread_foreach(cb, NULL);
	k_sem_give(&sem);    show("after k_sem_give", w);
	k_msleep(1);         show("after waiter returned", w);
	thread_analyzer_print(0);
	return 0;
}
```

```sh
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A4/zapp /var/tmp/rtos/A4/zbuild && \
timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -kernel /var/tmp/rtos/A4/zbuild/zephyr/zephyr.elf
```

```text
           FLASH:       17288 B         4 MB      0.41%
             RAM:        8784 B         4 MB      0.21%
*** Booting Zephyr OS build v4.4.2 ***
sizeof(struct k_thread) = 192, sizeof(_thread_base) = 64, callee_saved = 36
after k_thread_create        waiter state = queued
waiter blocked on k_sem_take waiter state = pending
after k_thread_suspend       waiter state = pending+suspended
after k_thread_resume        waiter state = pending
thread list (k_thread_foreach):
  waiter       prio   7  state pending    stack 1024 B  unused  892 B
  sleeper_tid  prio   5  state sleeping   stack 1024 B  unused  896 B
  idle         prio  15  state            stack  256 B  unused  208 B
  main         prio   0  state queued     stack 1024 B  unused  620 B
after k_sem_give             waiter state = queued
  waiter: got sem, returning from entry (OK in Zephyr)
after waiter returned        waiter state = dead
Thread analyze:
 sleeper_tid         : STACK: unused 896 usage 128 / 1024 (12 %); CPU: 0 %
                     : Total CPU cycles used: 1884
 idle                : STACK: unused 208 usage 48 / 256 (18 %); CPU: 90 %
                     : Total CPU cycles used: 907873
 main                : STACK: unused 484 usage 540 / 1024 (52 %); CPU: 8 %
                     : Total CPU cycles used: 87612
 ISR0                : STACK: unused 3944 usage 152 / 4096 (3 %); CPU: 0 %
                     : Total CPU cycles used: 0
qemu-system-arm: terminating on signal 15 from pid 938476 (timeout)
exit=124
```

빌드된 객체 크기도 확인했다:

```sh
arm-none-eabi-nm -S --size-sort /var/tmp/rtos/A4/zbuild/zephyr/zephyr.elf | grep -E "w_stack|_k_thread_stack_sleeper_tid|w_thread|_k_thread_obj_sleeper_tid|z_idle_stacks|z_main_stack|z_interrupt_stacks"
```

```text
200000b8 000000c0 b w_thread
20000178 000000c0 B _k_thread_obj_sleeper_tid
20001d50 00000100 b z_idle_stacks
20000950 00000400 B _k_thread_stack_sleeper_tid
20000550 00000400 B w_stack
20001e50 00000400 B z_main_stack
20000d50 00001000 B z_interrupt_stacks
```

**출력에서 볼 것**:

- `struct k_thread` = 192 B(FreeRTOS TCB의 두 배). 스레드마다 192 + 스택이 전부 .bss에 정적으로 있다(`_k_thread_obj_sleeper_tid` 0xc0, `_k_thread_stack_sleeper_tid` 0x400). heap은 쓰지 않았다.
- `STK 1024`가 정확히 **1024 바이트** 스택이 됐다(FreeRTOS였다면 4096 B). 이 빌드는 MPU 스택 가드(`CONFIG_MPU_STACK_GUARD`)가 꺼져 있어서 크기가 그대로지만, 가드나 userspace를 켜면 정렬·가드 영역 때문에 실제 배열이 커질 수 있다(F4).
- 대기 중에 suspend하면 **"pending+suspended"** — 대기를 유지한 채 suspend 비트만 더한다. resume하면 다시 "pending". FreeRTOS `vTaskSuspend`가 대기를 취소하는 것(§3.4)과 다르다.
- entry 함수가 return하자 "dead"가 됐다(`z_thread_entry` → `k_thread_abort`). 그 뒤 thread analyzer 목록에서 waiter가 빠졌다(`k_thread_abort`가 모니터 목록에서도 제거한다).
- **ISR 스택이 따로 있다**: `z_interrupt_stacks` 4096 B, analyzer가 "ISR0 usage 152 / 4096"으로 보여 준다. FreeRTOS Cortex-M 포트에서는 이 역할을 MSP(링커 스크립트의 main 스택)가 한다. 두 RTOS 모두 ISR을 태스크 스택에서 돌리지 않는다.
- idle은 우선순위 15(가장 낮음), 상태 문자열이 비어 있다(런큐 밖). 이 데모에서는 대부분 잠들어 있으니 idle CPU 90%다.
- 참고: mps2/an385 보드의 Zephyr 기본 tick은 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100`(10 ms)이고 tickless다. FreeRTOS 템플릿(1 kHz)과 다르니 시간 숫자를 비교할 때 주의한다(C5, C6).

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나

`xTaskCreate()` 한 줄이 커널 안에서 거치는 길을 FreeRTOS V11.3.2 `tasks.c`·`port.c`와 Zephyr v4.4.2 소스로 따라가 보자. 각 단계의 깊은 설명은 해당 모듈로 미룬다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="24" text-anchor="middle" font-size="14">FreeRTOS V11.3.2</text>
  <text x="510" y="24" text-anchor="middle" font-size="14">Zephyr v4.4.2</text>
  <rect x="15" y="36" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="25" y="54" font-size="13">xTaskCreate()</text>
  <text x="25" y="72" font-size="12">tasks.c</text>
  <rect x="15" y="91" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="25" y="109" font-size="13">prvCreateTask()</text>
  <text x="25" y="127" font-size="12">pvPortMalloc: 스택 → TCB 순서</text>
  <rect x="15" y="146" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="25" y="164" font-size="13">prvInitialiseNewTask()</text>
  <text x="25" y="182" font-size="12">0xa5 채움 · 이름 · 우선순위 · 리스트 항목</text>
  <rect x="15" y="201" width="310" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="25" y="219" font-size="13">pxPortInitialiseStack()</text>
  <text x="25" y="237" font-size="12">port.c: 가짜 예외 프레임 16워드 (C3)</text>
  <rect x="15" y="256" width="310" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="25" y="274" font-size="13">prvAddNewTaskToReadyList()</text>
  <text x="25" y="292" font-size="12">ready list 끝에 삽입, 더 높으면 yield</text>
  <rect x="355" y="36" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="54" font-size="13">k_thread_create()</text>
  <text x="365" y="72" font-size="12">kernel/thread.c</text>
  <rect x="355" y="91" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="109" font-size="13">z_setup_new_thread()</text>
  <text x="365" y="127" font-size="12">스택 정보 · 이름 · 우선순위 설정</text>
  <rect x="355" y="146" width="310" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="365" y="164" font-size="13">arch_new_thread()</text>
  <text x="365" y="182" font-size="12">8워드 프레임, PC = z_thread_entry</text>
  <rect x="355" y="201" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="219" font-size="13">thread_schedule_new()</text>
  <text x="365" y="237" font-size="12">K_NO_WAIT → k_thread_start</text>
  <rect x="355" y="256" width="310" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="365" y="274" font-size="13">ready_thread() → runq_add()</text>
  <text x="365" y="292" font-size="12">kernel/sched.c, 필요시 reschedule</text>
  <g stroke="currentColor" stroke-width="1.2">
    <line x1="170" y1="80" x2="170" y2="89" marker-end="url(#ah5)"/>
    <line x1="170" y1="135" x2="170" y2="144" marker-end="url(#ah5)"/>
    <line x1="170" y1="190" x2="170" y2="199" marker-end="url(#ah5)"/>
    <line x1="170" y1="245" x2="170" y2="254" marker-end="url(#ah5)"/>
    <line x1="510" y1="80" x2="510" y2="89" marker-end="url(#ah5)"/>
    <line x1="510" y1="135" x2="510" y2="144" marker-end="url(#ah5)"/>
    <line x1="510" y1="190" x2="510" y2="199" marker-end="url(#ah5)"/>
    <line x1="510" y1="245" x2="510" y2="254" marker-end="url(#ah5)"/>
  </g>
</svg>
```

그림 8 — 태스크 생성의 커널 내부 경로. 주황색 단계가 아키텍처(포트) 코드이고 나머지는 이식 가능한 커널 코드다. 두 RTOS 모두 "메모리 준비 → TCB 채우기 → 스택에 가짜 첫 프레임 → Ready 줄에 세우기" 순서다.

**FreeRTOS 단계별로**:

1. `xTaskCreate()` → `prvCreateTask()`: `portSTACK_GROWTH < 0`이면 `pvPortMallocStack(uxStackDepth × sizeof(StackType_t))` 다음 `pvPortMalloc(sizeof(TCB_t))`. TCB를 `memset(0)`하고 `ucStaticallyAllocated = tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB`. (`xTaskCreateStatic()`은 `prvCreateStaticTask()`가 호출자 버퍼를 그대로 쓰고 `tskSTATICALLY_ALLOCATED_STACK_AND_TCB`로 표시한다. 삭제할 때 `prvDeleteTCB()`가 이 값을 보고 `vPortFree`를 할지 말지 정한다.)
2. `prvInitialiseNewTask()`: 스택을 0xa5로 채우고(§5.3), `pxTopOfStack = &pxStack[uxStackDepth − 1]`을 8바이트 정렬로 내리고, 이름을 복사하고, `uxPriority`/`uxBasePriority`를 넣고, `vListInitialiseItem(&xStateListItem)`, `listSET_LIST_ITEM_OWNER(&xStateListItem, pxNewTCB)`(→ 예제 2의 `pvOwner`), 이벤트 리스트 항목 값은 `configMAX_PRIORITIES − uxPriority`(이벤트 리스트를 우선순위 순으로 정렬하려는 트릭, C1).
3. `pxPortInitialiseStack()`(port.c): 예제 2에서 본 16워드 위조 프레임을 쓰고 새 `pxTopOfStack`을 돌려준다. **첫 실행은 "예외 복귀"로 시작한다** — SVC 핸들러가 이 프레임을 복원하면 CPU는 `worker(pvParameters)`를 "호출된" 것처럼 시작한다. 이 마술의 전 과정이 C3(첫 태스크 시작)의 주제다.
4. `prvAddNewTaskToReadyList()`: 크리티컬 섹션 안에서 `uxCurrentNumberOfTasks++`, 첫 태스크면 `prvInitialiseTaskLists()`(모든 리스트 초기화), 스케줄러 전이면 더 높은 우선순위일 때 `pxCurrentTCB` 교체(예제 9의 함정), `uxTCBNumber` 부여, 그리고 매크로 `prvAddTaskToReadyList()`:

```c
#define prvAddTaskToReadyList( pxTCB )                                                                     \
    do {                                                                                                   \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                                           \
        taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                                                \
        listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->xStateListItem ) ); \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB );                                                      \
    } while( 0 )
```

`taskRECORD_READY_PRIORITY`는 `configUSE_PORT_OPTIMISED_TASK_SELECTION 1`(이 랩)일 때 `uxTopReadyPriority` 비트맵에 비트를 세운다 — 나중에 `CLZ` 명령 하나로 가장 높은 Ready 우선순위를 찾기 위해서다(C1). 스케줄러가 돌고 있고 새 태스크가 현재보다 높으면 `taskYIELD_ANY_CORE_IF_USING_PREEMPTION()`으로 PendSV를 걸어 즉시 넘긴다(C2).

**상태 전이가 리스트 이동이라는 것**도 소스에서 그대로 보인다: `vTaskDelay()` → `prvAddCurrentTaskToDelayedList()`가 `xStateListItem`을 ready 리스트에서 빼서 delayed 리스트에 깨울 시각 순으로 넣는다. tick ISR의 `xTaskIncrementTick()`이 시각이 된 항목을 다시 ready 리스트로 옮긴다(C5). `vTaskSuspend()`는 suspended 리스트로, `vTaskDelete()`는 `xTasksWaitingTermination`으로. idle 태스크(`prvIdleTask`)는 루프마다 `prvCheckTasksWaitingTermination()`을 불러 그 리스트를 비우며 `prvDeleteTCB()`로 메모리를 돌려준다(C9).

**Zephyr 단계별로**: `k_thread_create()`(syscall 래퍼) → `z_impl_k_thread_create()` → `z_setup_new_thread()`(kernel/thread.c: `z_init_thread_base()`로 우선순위와 초기 상태 `_THREAD_SLEEPING` 설정, `setup_thread_stack()`으로 스택 버퍼·`stack_info` 준비(`CONFIG_INIT_STACKS`면 0xaa로 채움), 이름) → `arch_new_thread()`(arch/arm/core/cortex_m/thread.c: 8워드 `__basic_sf` 프레임, `pc = z_thread_entry`, `a1 = entry`, `a2~a4 = p1~p3`, `xpsr = 0x01000000`, `callee_saved.psp = iframe`) → `thread_schedule_new()`(kernel/include/kthread.h: `K_NO_WAIT`면 `k_thread_start()`, 아니면 timeout 등록) → `ready_thread()` → `runq_add()`(kernel/sched.c). `K_THREAD_DEFINE`으로 만든 스레드는 부팅 때 `z_init_static_threads()`(kernel/init.c)가 같은 `z_setup_new_thread()` + `thread_schedule_new()`를 부른다. Zephyr의 위조 프레임은 8워드뿐이고 R4–R11은 TCB의 `callee_saved`에 있다는 점이 FreeRTOS와 다르다(§2.6).

```
(FreeRTOS ARM_CM3) new task stack top, high address first
  pxStack + depth  -> [ 0xa5 pad (8-byte align) ]
                      [ xPSR = 0x01000000        ]  \
                      [ PC   = task function     ]   |
                      [ LR   = prvTaskExitError  ]   |  hardware frame (8 words)
                      [ R12, R3, R2, R1          ]   |  restored by exception return
                      [ R0   = pvParameters      ]  /
                      [ R11 ... R4               ]  <- software frame (8 words), PendSV/SVC
  pxTopOfStack     -> (points at R4)
                      [ 0xa5a5a5a5 ...           ]
  pxStack          -> lowest address
```

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 — 태스크 세트와 RAM 예산

예를 들어 STM32H7급 비행 제어기라면 태스크 세트가 대략 이렇게 나온다: IMU 읽기(8 kHz, DRDY 인터럽트 → 알림), 자세 rate 루프(1~8 kHz), ESC DShot 출력, RC 입력, GNSS 파서, 텔레메트리, 블랙박스 로거, MAVLink 수신. 설계 리뷰에서 바로 나오는 질문이 "RAM이 얼마 드나"다. 예제 8·11의 측정값으로 공식을 확인하고 계산해 보자.

**무엇을 확인하나**: 예제 8에서 잰 heap 사용량이 공식 `4 × depth + 96 + 16`과 모든 크기에서 정확히 맞는지(assert), 그리고 그 공식으로 FC 태스크 세트의 RAM을 RTOS별로 계산.

```python
# 드론 FC 태스크 세트의 RAM 예산 — ex6/zapp에서 잰 식으로 계산
measured = {64: 368, 128: 624, 256: 1136, 512: 2160, 1024: 4208}   # ex6: heap used
for w, used in measured.items():
    assert used == 4 * w + 96 + 16, w                            # 식 검증: 스택 + TCB + 헤더 2개
print("formula OK: heap_used = 4*depth_words + 96 + 16")

tasks = [  # (이름, 스택 바이트) — 예시 설계값
    ("imu_rate_8k", 1024), ("attitude_1k", 2048), ("esc_dshot", 768), ("rc_input", 768),
    ("gnss_parse", 1536), ("telemetry", 2048), ("logger", 3072), ("mavlink_rx", 1536)]
fr_dyn = sum(s + 96 + 16 for _, s in tasks)
fr_sta = sum(s + 96 for _, s in tasks)
zep    = sum(s + 192 for _, s in tasks)
print(f"{'task':12} {'stack B':>8} {'FreeRTOS words':>15}")
for n, s in tasks:
    print(f"{n:12} {s:8} {s // 4:15}")
print(f"stacks only        : {sum(s for _, s in tasks)} B")
print(f"FreeRTOS dynamic   : {fr_dyn} B  (heap_4)")
print(f"FreeRTOS static    : {fr_sta} B  (.bss)")
print(f"Zephyr (this build): {zep} B  (.bss, k_thread 192 B)")
print(f"+ kernel tasks (FreeRTOS idle 1024+96, timer 2048+96): {1024+96+2048+96} B")
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/A4/py/budget.py
```

```text
formula OK: heap_used = 4*depth_words + 96 + 16
task          stack B  FreeRTOS words
imu_rate_8k      1024             256
attitude_1k      2048             512
esc_dshot         768             192
rc_input          768             192
gnss_parse       1536             384
telemetry        2048             512
logger           3072             768
mavlink_rx       1536             384
stacks only        : 12800 B
FreeRTOS dynamic   : 13696 B  (heap_4)
FreeRTOS static    : 13568 B  (.bss)
Zephyr (this build): 14336 B  (.bss, k_thread 192 B)
+ kernel tasks (FreeRTOS idle 1024+96, timer 2048+96): 3264 B
```

**출력에서 볼 것**: 태스크 8개에 스택이 12.8 KB, TCB·헤더는 1 KB 안팎이다. RTOS 선택(FreeRTOS냐 Zephyr냐)보다 **스택 크기를 어떻게 정하느냐**가 RAM을 좌우한다. 그리고 "FreeRTOS words" 열 — 같은 설계를 FreeRTOS로 옮기면 `xTaskCreate`에 넣을 숫자는 바이트의 1/4이다. H7의 DTCM(128 KB)에 rate 루프 태스크 스택을 두고 나머지는 AXI SRAM에 두는 식의 배치는 F8에서 다룬다.

### 10.2 로봇 관절 모터 컨트롤러 — "무엇을 태스크로 만들지 않을까"

FOC 전류 루프(20 kHz, 50 us 주기)는 보통 **태스크가 아니다**. ADC 변환 완료 인터럽트 안에서 직접 돈다. 태스크로 만들면 매 주기 ISR → 알림 → 컨텍스트 스위치 비용(수 us, C2·E3)과 지터가 붙기 때문이다. 태스크는 그보다 느린 것들 — 속도·위치 루프(1~2 kHz), CAN 명령 처리, 결함 감시, 파라미터 저장 — 에 쓴다. 면접에서 "모든 것을 태스크로 만들었다"보다 "이건 ISR, 이건 태스크, 이유는 주기와 지터 예산"이라고 답하는 쪽이 강하다(J7, Q3).

### 10.3 센서 허브 MCU — 정적 생성과 고정 스택

Linux SoC 옆에서 IMU 타임스탬프를 찍는 센서 허브는 몇 년씩 리셋 없이 돌아야 한다. 이런 장비는 보통 `K_THREAD_DEFINE`/`xTaskCreateStatic`으로 모든 태스크를 빌드 시점에 만들고, 태스크를 지우지 않으며, 부팅 후 heap 할당을 금지한다. 런타임 상태 점검은 §7의 `vTaskListTasks` 같은 표(또는 Zephyr thread analyzer)를 셸 명령이나 주기 텔레메트리로 내보내 HWM이 줄어드는 추세를 본다.

### 10.4 Don의 SSD 펌웨어와 나란히 놓기

| SSD 펌웨어 (bare-metal executive) | RTOS |
|---|---|
| 코어별 `while(1)` main 루프 | 태스크 여러 개 + 스케줄러 |
| job 테이블의 함수 포인터 + 상태 변수 | 태스크 함수 + 그 태스크의 스택 (상태가 스택에 남는다) |
| "NAND 완료 플래그를 다음 바퀴에 확인" | `ulTaskNotifyTake()`로 Blocked, ISR이 `vTaskNotifyGiveFromISR()`로 깨움 |
| job 우선순위 = 테이블 순서·호출 빈도 | `uxPriority` + 선점 |
| 스택 1개 크기만 산정 | 태스크마다 스택 산정 (총합이 RAM을 먹는다) |
| watchdog: main 루프 한 바퀴마다 kick | 태스크별 heartbeat를 모아 감시 태스크가 kick (E8) |
| job이 무한 루프에 빠지면 전체 정지 | 높은 우선순위 태스크가 블록 안 하면 아래가 전부 정지 (예제 10의 IDLE 0%) |

Don이 SSD 펌웨어에서 손으로 짜던 상태 기계 중 "기다림 때문에 생긴 상태"는 RTOS에서 사라진다. 대신 **스택 RAM과 컨텍스트 스위치 비용**을 낸다. 이 거래가 A2(실행 모델의 진화)의 결론이고, A7(bare-metal vs RTOS)의 판단 기준이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| Zephyr 코드의 스택 크기를 FreeRTOS로 그대로 복사 (또는 반대) | 스택 4배 과다로 RAM 부족, 또는 4배 부족으로 랜덤 HardFault | FreeRTOS는 words, Zephyr는 bytes (§5.2) | 단위를 이름에 박는다 (`STACK_BYTES`, `STACK_WORDS`), 매크로로 변환 |
| 태스크 함수가 return | assert(port.c:212) 또는 BASEPRI로 전체 정지, backtrace 엉터리 | LR = `prvTaskExitError` (§4.3, §4.4) | 무한 루프로 쓰거나 끝에 `vTaskDelete(NULL)` |
| 블로킹 없는 루프를 낮은 우선순위로 | IDLE 0%, 자기 삭제한 태스크 메모리 미회수, idle hook·저전력 안 됨 | idle이 굶음 (예제 3, 예제 10) | 루프 안에 블로킹 호출, 백그라운드 일은 idle hook이나 낮은 주기 태스크로 |
| `xTaskCreate` 반환값 무시 | 핸들 NULL 역참조 HardFault, 일부 기능만 조용히 빠짐 | heap 부족 (`errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`) | 반환값 검사, malloc-failed 훅, `xPortGetMinimumEverFreeHeapSize` 로깅 |
| 지역 변수 주소를 `pvParameters`로 전달 | 태스크가 쓰레기 값을 읽음 | 생성한 쪽 스택이 재사용됨 | `static`·전역·heap 객체를 넘긴다 |
| 큰 지역 배열·printf를 작은 스택 태스크에서 | 다른 태스크가 엉뚱하게 죽음, 훅이 뒤늦게 불림 | overflow가 아래쪽 이웃 블록을 덮음 (그림 3, 예제 7) | HWM 측정 + 마진, 큰 버퍼는 static, MPU 가드 (F3) |
| 스케줄러 시작 전 `vTaskDelete` | 시작하자마자 HardFault (CFSR UNALIGNED) | `pxCurrentTCB`가 해제된 TCB를 가리킴 (예제 9) | 시작 전에는 지우지 않는다 |
| `eTaskGetState`가 Blocked라서 timeout이 있다고 가정 | 영원히 안 깨는 태스크를 "곧 깰 것"으로 오판 | `portMAX_DELAY` 대기도 Blocked로 보고 (§3.2) | 대기 API의 timeout을 코드에서 확인, 감시는 heartbeat로 |
| 대기 중인 태스크를 `vTaskSuspend`로 "잠깐 멈춤" | 재개 후 `vTaskDelay`가 일찍 끝나거나 대기가 풀려 있음 | FreeRTOS suspend는 대기를 취소 (§3.4) | 일시 정지는 플래그·이벤트로 설계, Zephyr와 의미가 다름을 기억 |
| HWM을 "사용량"으로 읽음 | 여유가 많은 줄 알고 스택을 줄였다가 사고 | HWM = 남은 최소 여유(words) (§5.3) | used = depth − HWM 으로 계산, 최악 경로 실행 후 측정 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "What is a task in an RTOS, and how is it different from a thread or a process?"

**A.** 태스크는 함수 + 자기 스택 + TCB다. 모든 태스크가 한 주소 공간을 공유하므로 Linux의 스레드와 가장 닮았고, MMU로 격리된 프로세스와는 다르다. MPU가 있으면 영역을 나눌 수 있지만 기본은 무보호다. 각 태스크는 자기 스택을 가지므로 블로킹 호출로 멈췄다가 그 줄부터 이어 갈 수 있다.

> A task is a function plus its own stack plus a TCB the kernel uses to manage it. All tasks share one address space, so it's closest to a Linux thread, not a process — there's no MMU isolation unless you configure the MPU. Because each task has its own stack, it can block in the middle of a function and resume exactly where it stopped, which is what lets you write sequential code instead of hand-written state machines.

**꼬리 질문**: "그럼 태스크 하나가 다른 태스크 메모리를 망가뜨리면?" → 시스템 전체 위험, MPU·FreeRTOS-MPU·Zephyr userspace로 막는다(F4, F5).

**Q.** "What's in a TCB? Why must pxTopOfStack be the first member in FreeRTOS?"

**A.** 저장된 SP, 상태 리스트 항목, 이벤트 리스트 항목, 우선순위, 스택 시작 주소, 이름, 뮤텍스 상속용 원래 우선순위, 알림 값, run-time 카운터 등이다. 이 랩 설정에서 96바이트. SP가 첫 멤버인 이유는 PendSV 어셈블리가 `[pxCurrentTCB]`에서 오프셋 없이 SP를 읽고 쓰기 때문이다. 설정마다 TCB 레이아웃이 달라져도 모든 포트의 어셈블리를 고칠 필요가 없다.

> It holds the saved stack pointer, a state list item and an event list item, the priority, the stack base, the name, the base priority for priority inheritance, notification values and a runtime counter — 96 bytes in my build. pxTopOfStack is first because the port's PendSV assembly does `ldr r0, [pxCurrentTCB]` and `str r0, [tcb]` with no offset. Keeping it at offset zero means every port's assembly stays valid no matter which config options add or remove TCB fields. Zephyr avoids the rule differently: it stores R4–R11 and PSP in `callee_saved` inside the thread struct and uses build-generated offsets.

**Q.** "What states can a task be in? How does FreeRTOS track them?"

**A.** Running, Ready, Blocked, Suspended, Deleted. FreeRTOS는 상태 필드가 없고 TCB의 `xStateListItem`이 어느 리스트에 있는지로 표현한다: ready 리스트(우선순위별), delayed 리스트, suspended 리스트, termination 리스트. Running 태스크도 ready 리스트에 있고 `pxCurrentTCB`가 가리킬 뿐이다. `portMAX_DELAY` 대기는 suspended 리스트에 들어가지만 Blocked로 보고된다 — 실행 trace로 확인했다.

> Running, Ready, Blocked, Suspended and Deleted. FreeRTOS has no state field — the state is which list the TCB's xStateListItem is linked into: the per-priority ready lists, the delayed lists, the suspended list or the termination list. The running task actually stays in its ready list; it's just the one pxCurrentTCB points to. One subtlety I verified on QEMU: a task blocked with portMAX_DELAY sits on the suspended list but eTaskGetState reports it as Blocked, because it also sits on an event list.

**Q.** "What happens if a FreeRTOS task function returns?"

**A.** 초기 스택 프레임의 LR이 `prvTaskExitError`라서 거기로 간다. Cortex-M3 포트에서 그 함수는 일부러 실패하는 configASSERT를 걸고, assert가 없으면 BASEPRI로 인터럽트를 막고 무한 루프다. tick도 컨텍스트 스위치도 멈추니 시스템 전체가 선다. 끝내려면 `vTaskDelete(NULL)`. Zephyr는 `z_thread_entry`가 감싸고 있어서 return이 정상 종료다.

> The initial fake stack frame puts prvTaskExitError in LR, so returning jumps there. On the CM3 port it triggers a deliberate configASSERT; without asserts it masks interrupts via BASEPRI and spins forever, so the tick and PendSV are blocked and the whole system stops — I saw BASEPRI at 0x20 and a garbage backtrace in gdb. The fix is an infinite loop or vTaskDelete(NULL). In Zephyr, returning is fine because z_thread_entry calls k_thread_abort for you.

**Q.** "How do you size a task stack, and how do you detect overflow?"

**A.** 넉넉히 주고 최악 경로를 돌린 뒤 high water mark(0xa5 패턴이 남은 최소 여유, words)를 읽고 마진을 붙여 줄인다. `-fstack-usage` 정적 값과 같이 본다. 검출은 `configCHECK_FOR_STACK_OVERFLOW 2`(스위치 때 SP와 바닥 16바이트 패턴 검사)인데 사후 검출이라 MPU 가드나 PSPLIM이 진짜 보호다. 단위 함정: FreeRTOS words, Zephyr bytes.

> Start generous, run worst-case paths, read uxTaskGetStackHighWaterMark — it's the minimum free space in words, measured by scanning the 0xa5 fill pattern — then trim with a margin and cross-check with -fstack-usage. For detection, method 2 checks the saved SP and the last 16 bytes of pattern at each context switch, but it's after the fact: in my test the task ran another 29 microseconds after overflowing before the hook fired. Real protection is an MPU guard region or PSPLIM on Armv8-M. And watch units: FreeRTOS takes words, Zephyr takes bytes.

**Q.** "Static or dynamic task creation — which and why?"

**A.** 동적은 `xTaskCreate`가 heap에서 스택 + TCB를 잡는다(측정: 4 × depth + 96 + 16 B). 정적은 같은 바이트를 .bss에 둔다(heap 0). 바이트는 비슷하고 차이는 실패를 언제 아느냐 — 정적은 링크 때, 동적은 런타임. 안전 인증 프로젝트는 정적을 선호한다. 동적이라도 부팅 때만 만들고 지우지 않으면 결정적이다.

> Dynamic creation allocates the stack and TCB from the FreeRTOS heap — I measured exactly 4 × depth + 96 + 16 bytes with heap_4. Static creation puts the same bytes in .bss, so heap usage is zero. The memory cost is about the same; the real difference is when you find out you're out of RAM — at link time versus in the field. Safety-certified projects prefer static. If you do use dynamic, create everything at boot, never delete, and always check the return value.

**Q.** "Why might a deleted task's memory never be freed?"

**A.** 자기 자신을 지운 태스크는 자기 스택 위에서 도는 중이라 즉시 free할 수 없어 termination 리스트에 들어가고, idle 태스크가 나중에 회수한다. idle보다 높은 우선순위에 블로킹 없는 태스크가 있으면 idle이 못 돌아 메모리가 영영 안 돌아온다. 실행 trace에서 BUSY 태스크가 도는 동안 Deleted 상태가 유지되고 heap이 그대로인 것을 확인했다.

> When a task deletes itself it's still running on its own stack, so the kernel can't free it immediately — it goes onto the termination list and the idle task frees it later. If any task above idle never blocks, idle is starved and that memory is never reclaimed. I reproduced it: with a busy-looping priority-1 task, the deleted task stayed in eDeleted and the free heap didn't change until the busy task was removed and idle got to run.

**꼬리 질문**: "다른 태스크를 지우면?" → 그 자리에서 `prvDeleteTCB`로 바로 free(예제 3의 BUSY, 624 B 즉시 반환). 단, 스케줄러 시작 전에 지우면 `pxCurrentTCB`가 매달린다(예제 9).

---

## 13. 직접 해보기

1. **개념** — FreeRTOS TCB에 "state" 필드가 없는데 `eTaskGetState()`는 어떻게 Blocked와 Suspended를 구분하나? `portMAX_DELAY` 대기와 진짜 suspend를 구분하는 근거 두 가지를 말해 보라.
   정답/힌트: `xStateListItem`이 suspended 리스트에 있을 때 `xEventListItem`의 소속 리스트가 NULL이 아니거나(객체 대기), `ucNotifyState`가 `taskWAITING_NOTIFICATION`이면(알림 대기) Blocked. 둘 다 아니면 Suspended (tasks.c `eTaskGetState`).
2. **코드** — 예제 2를 고쳐서 TGT가 `xQueueReceive(q, &v, pdMS_TO_TICKS(3))`(유한 timeout)로 기다리게 하라. 그때 `pvContainer`가 어느 리스트를 가리키나? timeout이 지나면 반환값은?
   정답/힌트: delayed 리스트(`xDelayedTaskList1` 또는 2)를 가리키고, timeout 후 `pdFALSE`(`errQUEUE_EMPTY`). 유한 timeout 대기는 delayed 리스트 + 큐의 이벤트 리스트 두 곳에 동시에 걸린다.
3. **측정** — 예제 6에 FPU를 쓰는 태스크를 넣고 싶다면 이 랩(Cortex-M3, FPU 없음)에서는 못 한다. 대신 `rec(64)`와 `snprintf`에 `%d` 인자 5개를 넣은 경우를 추가해 HWM을 재고, 그림 6에 막대를 더해 보라.
   정답/힌트: 재귀는 대략 단당 20~30 B씩 선형 증가. printf 계열은 포맷 복잡도에 따라 수백 B. 숫자는 직접 재야 한다.
4. **코드** — Zephyr 예제 11에서 `K_THREAD_DEFINE`의 마지막 인자(delay)를 `100`으로 바꾸면 부팅 직후 sleeper의 `k_thread_state_str`은 무엇인가? 예상하고 확인하라.
   정답/힌트: 시작 전 timeout 대기 중이라 "sleeping"이 예상된다(`thread_schedule_new`가 `z_add_thread_timeout` 경로로 간다). 직접 돌려서 확인할 것.
5. **설계** — 드론 FC에 1 kHz rate 루프 태스크(우선순위 6), 100 Hz 텔레메트리(3), 로거(2), 그리고 블로킹 없이 도는 "백그라운드 체크섬 계산"이 있다. 체크섬을 어디에 둬야 하나? 우선순위 1 태스크로 두면 무엇이 깨지나?
   정답/힌트: 우선순위 1에 바쁜 루프를 두면 idle이 0%가 되어 idle hook(WFI 저전력)과 자기 삭제 태스크 회수가 멈춘다(예제 10). idle hook 안에서 조금씩 하거나, 주기적으로 블록하는 낮은 우선순위 태스크로 쪼갠다.
6. **디버깅** — 현장에서 "PC = `prvTaskExitError`, backtrace에 `vPortFree(pv=0xa5a5a5a5)`"인 덤프가 왔다. 무엇이 일어났고, 어떤 태스크인지 어떻게 찾나?
   정답/힌트: 어떤 태스크 함수가 return했다. `pxCurrentTCB->pcTaskName`을 읽으면 그 태스크다(예제 5에서 "oneshot"). backtrace는 LR로 점프한 탓에 의미가 없다.
7. **측정** — 예제 8을 `HEAP=heap_1`과 `HEAP=heap_2`로 다시 빌드해 같은 표를 만들어 보라(make 인자 `HEAP=heap_2`). heap_1에서는 무엇이 달라지나?
   정답/힌트: 먼저 `xPortGetMinimumEverFreeHeapSize` 줄을 지워야 링크된다(heap_1·heap_2에는 이 함수가 없다). 직접 돌려 보면 heap_1은 블록 헤더가 없어서 64 words가 352 B(overhead 0)이고, 첫 `vTaskDelete`에서 `heap_1.c:151`의 `configASSERT( pv == NULL )`가 터진다 — heap_1은 free 자체가 금지다. heap 구현별 비교는 F1.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| task / thread | 태스크 / 스레드 | 함수 + 자기 스택 + TCB. FreeRTOS는 task, Zephyr는 thread라고 부르는 같은 것 |
| TCB (Task Control Block) | 태스크 제어 블록 | 커널이 태스크를 관리하는 구조체. FreeRTOS `TCB_t`(96 B), Zephyr `struct k_thread`(192 B), 이 랩 기준 |
| `TaskHandle_t` | 태스크 핸들 | FreeRTOS에서 TCB 주소 그 자체(불투명 포인터) |
| `pxTopOfStack` | 저장된 SP | TCB 오프셋 0. 컨텍스트 스위치 어셈블리가 오프셋 없이 읽고 쓴다 |
| `pxCurrentTCB` | 현재 TCB | 지금 Running인 태스크의 TCB를 가리키는 커널 전역 변수 |
| ready list | Ready 리스트 | 우선순위별 이중 연결 리스트 배열 `pxReadyTasksLists[]` (Running 태스크도 여기 있다) |
| delayed list | 지연 리스트 | 깨울 시각 순으로 정렬된 Blocked 태스크 리스트 (tick wrap 대비 2개) |
| Blocked | 대기 | 시간·큐·세마포어·알림을 기다리며 CPU를 쓰지 않는 상태 |
| Suspended | 정지 | `vTaskSuspend`로 세운 상태. timeout 없음, `vTaskResume`만 꺼냄 |
| Deleted | 삭제됨 | 지워졌지만 idle이 아직 메모리를 회수하지 않은 상태 |
| idle task | idle 태스크 | 우선순위 0(FreeRTOS)/최저(Zephyr). 할 일이 없을 때 돌고, 삭제된 태스크 메모리를 회수 |
| fake initial frame | 위조 초기 프레임 | 새 태스크 스택에 미리 써 둔 예외 복귀 프레임. 첫 실행이 "예외 복귀"로 시작한다 (C3) |
| `prvTaskExitError` | 태스크 종료 오류 함수 | 초기 프레임의 LR. 태스크 함수가 return하면 도착하는 곳 |
| high water mark | 최고 수위 표시 | 스택이 가장 깊이 내려갔을 때의 남은 여유. FreeRTOS는 words 단위 |
| stack fill pattern | 스택 채움 값 | FreeRTOS 0xa5, Zephyr `CONFIG_INIT_STACKS` 0xaa. 사용량 측정의 기준 |
| `configCHECK_FOR_STACK_OVERFLOW` | 스택 overflow 검사 설정 | 1 = 스위치 때 SP 검사, 2 = + 바닥 16바이트 패턴 검사 |
| static allocation | 정적 할당 | `xTaskCreateStatic`·`K_THREAD_DEFINE`. 메모리가 .bss에 링크 시점에 확정 |
| `pvParameters` | 태스크 인자 | 생성할 때 준 포인터. 초기 프레임의 R0로 전달된다 |
| `callee_saved` | 피호출자 저장 레지스터 | Zephyr Cortex-M에서 R4–R11과 PSP를 TCB 안에 저장하는 필드 |
| `_THREAD_QUEUED` 등 | Zephyr 상태 비트 | queued·pending·sleeping·suspended·dead … 여러 비트가 동시에 켜질 수 있다 |
| time slicing | 시분할 | 같은 우선순위 Ready 태스크들이 tick마다 번갈아 실행 (C4) |

---

## 15. 요약 & 체크리스트

태스크는 "함수 + 자기 스택 + TCB"이고, 자기 스택이 있기 때문에 함수 한가운데서 블록했다가 그 줄부터 이어 갈 수 있다. FreeRTOS TCB(이 랩 96 B)의 첫 워드는 저장된 SP라서 포트 어셈블리가 오프셋 없이 다루고, 새 태스크의 스택에는 PC = 태스크 함수, R0 = `pvParameters`, LR = `prvTaskExitError`, xPSR = Thumb인 16워드 가짜 프레임이 미리 쌓인다. 상태는 TCB의 리스트 항목이 어느 커널 리스트에 꽂혀 있느냐로 표현되며(Running도 ready 리스트에 있다), `portMAX_DELAY` 대기는 suspended 리스트에 있어도 Blocked로 보고되고, 자기 삭제한 태스크는 idle이 돌 때까지 Deleted로 남는다. 태스크 함수가 return하면 FreeRTOS는 assert 또는 BASEPRI 정지로 시스템 전체가 서지만 Zephyr는 정상 종료다. 스택은 FreeRTOS가 words, Zephyr가 bytes이며, 0xa5 패턴으로 "가장 깊이 내려간 곳"을 재고, 방식 2 overflow 검사는 다음 컨텍스트 스위치에서야 잡는다. 태스크 하나의 RAM은 대부분 스택이고(동적 = 4 × depth + 96 + 16 B, 정적 = 같은 바이트가 .bss로), 정적·동적의 진짜 차이는 "RAM 부족을 링크 때 아느냐 현장에서 아느냐"다.

- [ ] 태스크·스레드·프로세스 차이를 주소 공간과 격리로 설명할 수 있다
- [ ] FreeRTOS `tskTCB`의 주요 필드 8개를 말하고, `pxTopOfStack`이 첫 멤버인 이유를 PendSV 코드로 설명할 수 있다
- [ ] Zephyr `struct k_thread`가 R4–R11을 TCB에 저장한다는 차이를 말할 수 있다
- [ ] 새 태스크의 위조 초기 프레임 16워드에서 PC, LR, R0, xPSR의 값을 말할 수 있다
- [ ] 다섯 상태와 각 전이를 일으키는 API를 그림 없이 그릴 수 있다
- [ ] 상태 ↔ 커널 리스트 대응(ready, delayed, suspended, termination)과 `portMAX_DELAY` 함정을 설명할 수 있다
- [ ] 태스크 함수가 return할 때 FreeRTOS(assert/BASEPRI 정지)와 Zephyr(정상 종료)의 차이를 말할 수 있다
- [ ] 스택 크기 단위(FreeRTOS words, Zephyr bytes)를 틀리지 않는다
- [ ] high water mark가 무엇을 재는지(가장 깊은 지점의 남은 여유)와 한계를 안다
- [ ] 방식 2 overflow 검사가 언제 검사하고 무엇을 놓치는지 안다
- [ ] 태스크 하나의 RAM 비용을 바이트 단위로 계산할 수 있다 (`4 × depth + 96 + 16`)
- [ ] idle이 굶으면 무엇이 멈추는지(메모리 회수, idle hook) 예를 들 수 있다

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 — `tasks.c`(`tskTCB`, `prvCreateTask`, `prvInitialiseNewTask`, `prvAddNewTaskToReadyList`, `eTaskGetState`, `vTaskSuspend`, `vTaskDelete`, `prvCheckTasksWaitingTermination`, `prvTaskCheckFreeStackSpace`, `vTaskListTasks`), `include/task.h`(`eTaskState`, API 설명 주석), `include/FreeRTOS.h`(`StaticTask_t`), `include/stack_macros.h`(overflow 검사), `portable/GCC/ARM_CM3/port.c`(`pxPortInitialiseStack`, `prvTaskExitError`, `xPortPendSVHandler`, `vPortSVCHandler`), `portable/MemMang/heap_4.c`. 랩 경로 `/var/rtoslab/src/FreeRTOS-Kernel`. GitHub: https://github.com/FreeRTOS/FreeRTOS-Kernel
- Richard Barry, "Mastering the FreeRTOS Real Time Kernel" — Task Management 장 (태스크 상태, 우선순위, idle 태스크, `vTaskDelete`). 현재판 소스: https://github.com/FreeRTOS/FreeRTOS-Kernel-Book
- FreeRTOS 공식 문서 — https://www.freertos.org/ (Task states, `xTaskCreate`, `xTaskCreateStatic`, `uxTaskGetStackHighWaterMark`, stack overflow checking 페이지)
- Zephyr v4.4.2 소스 — `include/zephyr/kernel/thread.h`(`struct k_thread`, `struct _thread_base`), `include/zephyr/kernel_structs.h`(`_THREAD_*` 상태 비트), `include/zephyr/arch/arm/thread.h`(`_callee_saved`), `kernel/thread.c`(`z_setup_new_thread`, `k_thread_state_str`), `kernel/sched.c`(`ready_thread`, `runq_add`, 단일 코어 런큐 주석), `kernel/init.c`(`z_init_static_threads`), `lib/os/thread_entry.c`(`z_thread_entry`), `arch/arm/core/cortex_m/thread.c`(`arch_new_thread`), `arch/arm/core/cortex_m/swap_helper.S`, `subsys/debug/thread_analyzer/`. 랩 경로 `/var/rtoslab/zephyrproject/zephyr`.
- Zephyr Project Documentation — Threads: https://docs.zephyrproject.org/latest/kernel/services/threads/index.html
- Arm, "Cortex-M3 Devices Generic User Guide" — 예외 진입·복귀 스택 프레임, MSP/PSP, BASEPRI, CFSR/HFSR (B1, B2에서 자세히)
- 관련 Don 노트 — EL A2(프로세스·스레드), EL F1(`task_struct`·스케줄러), 이 모듈 A2(실행 모델), A3(구성 요소), A5(API 로제타석)
