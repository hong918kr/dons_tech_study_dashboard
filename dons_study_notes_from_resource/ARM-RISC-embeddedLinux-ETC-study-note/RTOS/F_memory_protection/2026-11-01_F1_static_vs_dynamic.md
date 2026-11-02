# F1. 정적 vs 동적 할당 — "RTOS에서 malloc 써도 되나"에 숫자로 답하기

> **이 노트를 다 읽으면**: MISRA·JSF·Power of 10이 왜 "초기화 뒤 heap 금지"를 요구하는지 4가지 위험으로 설명하고, FreeRTOS에서 초기화 후 heap을 잠그는 패턴을 짤 수 있다 · heap_1~heap_5의 자료구조(`BlockLink_t`, `xStart`/`pxEnd`, 크기 순 vs 주소 순 목록, 병합)를 V11.3.2 소스로 설명할 수 있다 · alloc/free 시간이 "빈 블록 수 N에 비례"한다는 것을 QEMU 실측(heap_4 malloc 블록당 352 ns)으로 보이고 WCET 식으로 말할 수 있다 · heap_2가 전체 빈 공간이 충분한데도 실패하는 순간을 재현하고, 정적 전용 빌드로 메모리 예산을 링크 시점에 확정할 수 있다
> **왜 필요한가**: "Can you use malloc in an RTOS?"는 펌웨어 면접 단골이다. "안 됩니다"만 말하면 반쪽 답이다. 언제 되고 언제 안 되는지, 안 되는 이유를 숫자로, 되게 만드는 방법(초기화 시점 할당·풀·heap_4 + 감시)을 말해야 한다. 현장에서는 "장비가 며칠 뒤 멈춘다"의 단골 원인이 heap 단편화다
> **Don 기준 난이도**: SSD 펌웨어에서 정적 풀·미리 잡은 버퍼만 썼으니 "왜 malloc을 피하나"는 몸으로 안다 / 새로 배울 부분은 FreeRTOS heap_x 다섯 개의 내부 구현과 선택 기준, 할당 시간의 상한을 식으로 쓰는 법, 단편화를 측정하고 "실패하지 않음"을 증명하는 방법, 정적 전용 FreeRTOS 빌드, Zephyr의 정적 객체 기본 스타일
> **선행 노트**: A4(TCB·태스크 RAM = heap_4에서 4 × depth + 112 B), C1(커널 자료구조), B5(.bss 0 채우기, ucHeap을 `.noinit`으로), B11(libc 선택, `_sbrk`), EL F9(Linux buddy·slab — MCU 차이만 쓴다). 이어지는 노트: F2(메모리 풀·`k_heap`/`sys_heap` 내부), F3(스택 산정), F7(재진입성·malloc 락), F8(메모리 배치·heap_5 다중 뱅크)

---

## 0. 큰 그림 — 메모리를 "언제" 나눠 주나

### 0.1 직관

메모리 할당은 결국 **"이 RAM 조각은 누구 것인가"를 언제 정하느냐**의 문제다.

- **정적 할당**: 컴파일·링크할 때 정한다. `static uint8_t buf[512];`가 그렇다. 링커가 주소를 고르고, `.bss`나 `.data`에 자리를 박아 둔다. 실행 중에는 아무 결정도 하지 않는다.
- **동적 할당**: 실행 중에 정한다. `malloc(512)`가 그렇다. 할당기(allocator)가 빈 조각 목록을 뒤져서 하나를 골라 준다. 언제, 얼마나, 몇 번 부를지는 코드가 돌아 봐야 안다.

Don이 SSD 펌웨어에서 쓰던 방식이 첫 번째다. 명령 슬롯, NAND 페이지 버퍼, 매핑 테이블 캐시를 모두 부팅 때(또는 링크 때) 고정 개수로 잡아 두고, 실행 중에는 "빈 슬롯 목록"에서 꺼내고 돌려줄 뿐이다. 그 목록도 크기가 하나뿐인 고정 블록 풀이다(F2). 이 노트의 결론도 결국 그쪽으로 간다. 다만 RTOS는 기본으로 동적 할당기를 들고 오고(FreeRTOS `xTaskCreate`는 내부에서 `pvPortMalloc`을 두 번 부른다, A4), 서드파티 스택(TCP/IP, USB, 파일 시스템)은 malloc을 부른다. 그래서 "안 쓴다"가 아니라 **"어디까지 허용하고 어떻게 증명하나"**가 실무 질문이 된다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<text x="170" y="22" font-size="14" text-anchor="middle">동적: heap_4 (.bss 20,708 B)</text>
<text x="510" y="22" font-size="14" text-anchor="middle">정적 전용 (.bss 9,840 B)</text>
<rect x="50" y="245.7" width="240" height="54.3" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="170.0" y="277.0" font-size="12" text-anchor="middle">커널·libc 정적 4,324 B</text>
<rect x="50" y="174.6" width="240" height="71.1" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="214.3" font-size="12" text-anchor="middle">heap에서 실제 쓴 것 5,664 B</text>
<rect x="50" y="40.0" width="240" height="134.6" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170.0" y="111.5" font-size="12" text-anchor="middle">예약만 하고 안 쓴 heap 10,720 B</text>
<rect x="390" y="246.0" width="240" height="54.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510.0" y="277.2" font-size="12" text-anchor="middle">커널·libc 정적 4,300 B</text>
<rect x="390" y="176.5" width="240" height="69.6" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510.0" y="215.5" font-size="12" text-anchor="middle">앱 객체 (심볼 13개) 5,540 B</text>
<text x="170" y="324" font-size="12" text-anchor="middle">크기를 바꿔도 링크는 통과, 모자라면 실행 중 실패</text>
<text x="510" y="324" font-size="12" text-anchor="middle">모자라면 링크 실패: region RAM overflowed</text>
<text x="340" y="344" font-size="12" text-anchor="middle">같은 앱(태스크 3, 큐 2, 뮤텍스, 타이머, 이벤트 그룹) — 예제 7 size·nm 실측</text>
</svg>
```
그림 1 — 같은 앱(태스크 3개, 큐 2개, 뮤텍스, 타이머, 이벤트 그룹)을 동적(heap_4, `configTOTAL_HEAP_SIZE` 16 KB)과 정적 전용으로 빌드한 `.bss`(예제 7, `arm-none-eabi-size -A`와 `nm -S` 실측). 회색은 idle·timer 태스크 스택과 TCB, 타이머 큐, newlib `__sf` 등 앱이 고를 수 없는 부분이다(두 빌드 차이 24 B는 heap_4의 관리 변수). 동적 빌드는 heap을 통째로 예약해 두고 그중 5,664 B만 썼다. 정적 빌드는 쓴 만큼만 있고, 각 객체가 이름 있는 심볼이라 map 파일에서 바로 보인다.

### 0.2 RTOS에서 동적 할당이 문제가 되는 네 가지 이유

| 위험 | 무슨 일이 일어나나 | 왜 RTOS·MCU에서 더 나쁜가 | 이 노트에서 |
|---|---|---|---|
| 시간이 일정하지 않다 | malloc·free가 빈 블록 목록을 걷는다 → 목록 길이에 비례 | 마감(deadline)이 있는 태스크의 WCET에 들어간다. 그리고 할당 중에는 스케줄러가 멈춘다(`vTaskSuspendAll`) | §3 (예제 3, 4) |
| 단편화 | 빈 공간 합은 충분한데 연속된 조각이 없어서 실패 | MMU가 없어 페이지를 다시 맞춰 붙일 수 없고, compaction도 없다 | §4 (예제 2, 5, 6) |
| 실행 중 실패 | 언젠가 NULL이 돌아온다 | 드론 비행 중에 "메모리 부족"은 복구 경로가 없다. 사람이 재부팅해 줄 수 없다 | §4, §5 |
| 메모리 오염이 넓게 번진다 | overflow·double free가 이웃 블록 헤더를 망가뜨린다 | 보호 장치(MPU) 없이 모든 태스크가 같은 heap을 공유한다 | §6 (예제 8) |

이 넷 중 "시간"과 "단편화"는 **할당기 알고리즘**의 성질이고, "실행 중 실패"와 "오염"은 **heap이라는 공유 자원**의 성질이다. 그래서 대책도 두 갈래다. 알고리즘을 바꾸거나(heap_4, 고정 크기 풀), 공유 heap을 실행 중에 아예 안 쓰거나(정적 할당, 초기화 후 잠금).

### 0.3 Linux 커널 할당기와 무엇이 다른가 (EL F9 재사용)

buddy 할당자, SLUB, GFP 플래그, 워터마크, compaction은 EL F9에서 다뤘다. MCU RTOS에서 달라지는 점만 정리한다.

| 항목 | Linux 커널 (EL F9) | MCU RTOS (이 노트) |
|---|---|---|
| 기본 단위 | 4 KB 페이지 (buddy), 그 위에 객체 캐시 (slab) | 바이트 단위 블록, 헤더 8 B (FreeRTOS heap_2/4/5) |
| 단편화 대응 | buddy 병합 + compaction + 페이지 테이블로 가상 연속 | heap_4/5의 이웃 병합뿐. 옮겨 붙이기 없음 |
| 메모리 부족 시 | reclaim, OOM killer, 실패 시 GFP에 따라 대기 | 즉시 NULL + `vApplicationMallocFailedHook` |
| 크기 | 수 GB, 수백 MB 여유 | 수십~수백 KB, 여유는 보통 수 KB |
| 고정 크기 객체 | `kmem_cache` (slab) | 고정 블록 풀 — FreeRTOS에는 없고 직접 만든다, Zephyr `k_mem_slab` (F2) |
| 인터럽트에서 할당 | `GFP_ATOMIC` | FreeRTOS `pvPortMalloc`은 ISR 금지 (`vTaskSuspendAll` 사용), Zephyr `k_malloc`은 ISR에서 가능하지만 결정성은 없다 |

Linux의 slab은 "같은 크기 객체는 같은 크기 칸에"라는 발상이고, RTOS에서 이것을 직접 만든 것이 고정 블록 풀이다(F2). 이 노트는 그 앞 단계, **범용 가변 크기 할당기가 왜 문제이고 FreeRTOS의 다섯 할당기가 각각 얼마나 문제인가**를 다룬다.

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) + FreeRTOS V11.3.2(ARM_CM3 포트) / Zephyr v4.4.2(같은 보드). 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns)로 돌리고 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz → 1 tick = 40 ns)로 쟀다. **QEMU icount 기준 값이며, 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스를 모델링하지 않는다). 다만 이 노트가 보는 것은 "목록 길이에 따른 기울기"와 "분포의 꼬리"라서 모양은 실칩과 같다. 모든 예제는 `-Wall -Wextra` 경고 0개다(§3.3의 `configASSERT`를 끈 빌드만 커널 소스에서 경고 3줄이 나오고, 그대로 보여 준다).

랩 템플릿 Makefile은 heap 파일을 하나 골라 링크한다(`HEAP=heap_4`). 이 노트는 두 가지를 더 해야 해서 템플릿 Makefile을 내 폴더로 복사해 고쳤다. (1) `HEAP=none`이면 heap 파일을 링크하지 않는다(정적 전용 빌드, 또는 예제가 heap 소스를 직접 `#include`). (2) `LDS=`로 링커 스크립트를 바꾼다(RAM 64 KB 실험).

```make
# F1 copy of lab freertos-mps2 Makefile: HEAP=none (static-only build) and LDS=<linker script> added
TPL    := /var/rtoslab/templates
K      := /var/rtoslab/src/FreeRTOS-Kernel
CC     := arm-none-eabi-gcc
OUT    ?= ./out
SRCS   ?= main.c
HEAP   ?= heap_4
LDS    ?= $(TPL)/common/mps2_m3.ld
CFLAGS := -mthumb -mcpu=cortex-m3 -O2 -g3 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections \
          $(addprefix -I,$(sort $(dir $(SRCS)))) -I$(TPL)/freertos-mps2 -I$(TPL)/common \
          -I$(K)/include -I$(K)/portable/GCC/ARM_CM3 -I$(K)/portable/MemMang $(EXTRA_CFLAGS)
LDFLAGS:= -T $(LDS) -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=$(OUT)/app.map $(EXTRA_LDFLAGS)
HSRC   := $(if $(filter none,$(HEAP)),,$(K)/portable/MemMang/$(HEAP).c)
KSRC   := $(K)/tasks.c $(K)/list.c $(K)/queue.c $(K)/timers.c $(K)/event_groups.c $(K)/stream_buffer.c \
          $(HSRC) $(K)/portable/GCC/ARM_CM3/port.c
ALL    := $(SRCS) $(TPL)/freertos-mps2/hooks.c $(TPL)/common/startup.c $(TPL)/common/board.c $(KSRC)
$(OUT)/app.elf: FORCE | $(OUT)
	$(CC) $(CFLAGS) $(ALL) $(LDFLAGS) -o $@
	arm-none-eabi-size $@
FORCE:
$(OUT): ; mkdir -p $@
```

```sh
#!/bin/bash
# run.sh <name> <outtag> [make args...] — build ~/.rtos_scratch/F1/<name>.c → /var/tmp/rtos/F1/<outtag>, run with ICOUNT=1
N=$1; O=$2; shift 2
S=/Users/donh/.rtos_scratch/F1; B=/var/tmp/rtos/F1/$O
mkdir -p $B
make -s -f $S/Makefile SRCS="$S/$N.c" OUT=$B "$@" >$B.build 2>&1 || { cat $B.build; exit 9; }
grep -i "warn" $B.build
[ -n "$NORUN" ] && { tail -2 $B.build; exit 0; }
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $B/app.elf ${TO:-60}; echo "[rc=$?]"
```

앱 폴더의 `FreeRTOSConfig.h`는 템플릿 것을 그대로 포함하고, 예제별 스위치만 더한다.

```c
/* F1: lab template config + optional F1 hooks */
#ifndef F1_FREERTOS_CONFIG_H
#define F1_FREERTOS_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifdef F1_STATIC_ONLY                     /* static-only kernel: no heap_x.c linked */
#undef  configSUPPORT_DYNAMIC_ALLOCATION
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#undef  configUSE_STATS_FORMATTING_FUNCTIONS
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#endif
#ifdef F1_NOASSERT
#undef configASSERT
#endif
#ifdef F1_TRACE_MALLOC
#include <stddef.h>
void f1_trace_malloc(void *p, size_t n);
#define traceMALLOC(p, n) f1_trace_malloc((p), (n))
#endif
#endif
```

heap 내부(빈 블록 목록)를 직접 걷기 위해, 예제 대부분은 `heap_N.c`를 **예제 파일 안에 `#include`**한다. heap 파일의 변수(`xStart`, `pxEnd`)가 `static`이라 다른 파일에서는 안 보이기 때문이다. 할당기 코드는 한 줄도 바꾸지 않는다.

```c
/* heapsel.h — compile one FreeRTOS heap_N.c INTO this translation unit (HEAP=none build)
 * so the example can walk its private free list. -DHN=2/4/5 */
#ifndef HEAPSEL_H
#define HEAPSEL_H
#if HN == 1
#include "heap_1.c"
#elif HN == 2
#include "heap_2.c"
#define FL_END (&xEnd)
#elif HN == 3
#include "heap_3.c"
#elif HN == 4
#include "heap_4.c"
#define FL_END (pxEnd)
#elif HN == 5
#include "heap_5.c"
#define FL_END (pxEnd)
#endif
#ifdef FL_END
/* walk the free list: count blocks, largest block, total */
static inline void fl_stats(size_t *nblk, size_t *largest, size_t *total)
{
    size_t n = 0, mx = 0, t = 0;
    vTaskSuspendAll();
    for (BlockLink_t *b = xStart.pxNextFreeBlock; b && b != FL_END; b = b->pxNextFreeBlock) {
        if (b->xBlockSize == 0) continue;           /* heap_5 region-end marker */
        n++; t += b->xBlockSize; if (b->xBlockSize > mx) mx = b->xBlockSize;
    }
    (void)xTaskResumeAll();
    *nblk = n; *largest = mx; *total = t;
}
#endif
#endif
```

---
## 1. 규칙이 말하는 것 — "금지"가 아니라 "초기화 뒤 금지"

### 1.1 코딩 표준과 인증 프로젝트의 정책

안전 규격과 코딩 표준은 대부분 동적 할당을 막는다. 그런데 문장을 잘 보면 **"언제"**를 붙인 것이 많다.

| 출처 | 내용 | 시점 |
|---|---|---|
| MISRA C:2012 / C:2023 Directive 4.12 | Dynamic memory allocation shall not be used | 전체 금지 (deviation으로 허용 가능) |
| MISRA C:2012 / C:2023 Rule 21.3 | `<stdlib.h>`의 메모리 할당·해제 함수(malloc, calloc, realloc, free)를 쓰지 않는다 | 전체 금지 (libc 함수 수준) |
| JSF AV C++ Rule 206 | 자유 저장소(heap)에서의 할당·해제는 초기화가 끝난 뒤에 일어나면 안 된다 | **초기화 뒤 금지** |
| NASA/JPL "Power of 10" Rule 3 | 초기화가 끝난 뒤 동적 메모리 할당을 쓰지 않는다 | **초기화 뒤 금지** |
| ISO 26262-6 소프트웨어 유닛 설계 원칙 | 동적 객체·변수를 쓰지 않거나, 쓴다면 생성 시 온라인 검사 | ASIL에 따라 권장 수준이 다르다 |

MISRA의 두 항목은 겹친다. Dir 4.12는 정책, Rule 21.3은 "libc의 그 함수들"이라는 구체적인 금지다. FreeRTOS 커널 자체도 MISRA 검사를 받는데, `pvPortMalloc`의 반환값을 TCB 포인터로 바꾸는 줄마다 `MISRA Ref 11.5.1 [Malloc memory assignment]` deviation 주석이 붙어 있다(V11.3.2 `tasks.c`). 커널이 "동적 할당 경로가 있다"는 것을 문서로 인정하고, 인증 프로젝트에는 정적 할당 API를 쓰라고 권하는 구조다.

실무 인증 프로젝트(드론 비행 제어 DO-178C, 차량 ECU ISO 26262)에서 흔한 정책은 셋 중 하나다.

1. **정적 전용**: 모든 객체를 링크 시점에 고정한다. FreeRTOS `configSUPPORT_DYNAMIC_ALLOCATION 0`. 링커가 메모리 예산을 검사한다(§5).
2. **초기화 시점 할당 + 잠금**: 부팅 중에는 `xTaskCreate` 같은 편한 API를 쓰고, 초기화가 끝나면 heap을 잠근다. 잠근 뒤 할당 시도는 assert. JSF·Power of 10이 허용하는 꼴이다. heap_1이 이 정책의 할당기다(free가 없다).
3. **동적 할당 허용 + 증명**: 서드파티 스택 때문에 실행 중 할당이 불가피하면, 할당기를 정하고(heap_4 또는 고정 블록 풀), 최악 시간과 "실패하지 않음"을 시험·분석으로 보인다(§3, §4). 문서상 deviation이 필요하다.

Don의 SSD 펌웨어는 1번과 2번 사이였을 것이다. 부팅 때 DRAM·SRAM 크기를 보고 풀 개수를 정하고(2번), 그 뒤로는 풀에서 꺼내고 돌려줄 뿐이다.

### 1.2 예제 1 — 누가 `pvPortMalloc`을 부르나, 그리고 초기화 후 잠금

무엇을 확인하는 코드인가: 흔한 앱 초기화에서 커널이 `pvPortMalloc`을 몇 번, 얼마씩 부르는지 기록하고, 초기화가 끝나면 heap을 잠근다. 그다음 "무해해 보이는" API 하나가 실행 중에 heap을 쓰는 것을 잡는다. 링커의 `--wrap=pvPortMalloc`을 쓰면 다른 오브젝트 파일에서 `pvPortMalloc`을 부르는 곳이 모두 `__wrap_pvPortMalloc`으로 가고, 원래 함수는 `__real_pvPortMalloc`이 된다. 커널 소스를 고치지 않고 호출자 주소(`__builtin_return_address(0)`)를 얻을 수 있다.

```c
/* ex1 — who calls pvPortMalloc, and the "lock the heap after init" pattern */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include "board.h"

void *__real_pvPortMalloc(size_t n);
static volatile int heap_locked;
static struct { void *caller; size_t want; void *p; } lg[32];
static int nlg;

void *__wrap_pvPortMalloc(size_t n)            /* -Wl,--wrap=pvPortMalloc */
{
    void *caller = __builtin_return_address(0);
    if (heap_locked) {
        board_printf_raw("\n[HEAP LOCKED] pvPortMalloc(%u) from %x after init!\n",
                         (unsigned)n, (unsigned)caller);
        configASSERT(0);
    }
    void *p = __real_pvPortMalloc(n);
    if (nlg < 32) { lg[nlg].caller = caller; lg[nlg].want = n; lg[nlg].p = p; nlg++; }
    return p;
}

static void tmr_cb(TimerHandle_t t) { (void)t; }
static void worker(void *pv) { (void)pv; for (;;) vTaskDelay(10); }

static void monitor(void *pv)
{
    (void)pv;
    static char buf[256];
    vTaskDelay(5);
    board_printf_raw("monitor: calling vTaskListTasks() ...\n");
    vTaskListTasks(buf, sizeof buf);           /* looks harmless */
    board_printf_raw("not reached when heap is locked\n");
    for (;;) vTaskDelay(100);
}

int main(void)
{
    size_t f0 = xPortGetFreeHeapSize();
    xTaskCreate(worker,  "imu",  256, NULL, 3, NULL);
    xTaskCreate(worker,  "ctl",  512, NULL, 2, NULL);
    xTaskCreate(monitor, "mon",  384, NULL, 1, NULL);
    QueueHandle_t q1 = xQueueCreate(16, sizeof(uint32_t));
    QueueHandle_t q2 = xQueueCreate(4, 64);
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    EventGroupHandle_t eg = xEventGroupCreate();
    TimerHandle_t t = xTimerCreate("hb", 100, pdTRUE, NULL, tmr_cb);
    StreamBufferHandle_t sb = xStreamBufferCreate(512, 1);
    (void)q1; (void)q2; (void)m; (void)eg; (void)t; (void)sb;

    board_printf_raw("#  caller    want  block@     \n");
    for (int i = 0; i < nlg; i++)
        board_printf_raw("%d  %x  %u  %x\n", i, (unsigned)lg[i].caller,
                         (unsigned)lg[i].want, (unsigned)lg[i].p);
    size_t f1 = xPortGetFreeHeapSize();
    board_printf_raw("allocs=%d  heap used by init = %u B (free %u -> %u)\n",
                     nlg, (unsigned)(configTOTAL_HEAP_SIZE - f1), (unsigned)f0, (unsigned)f1);
    heap_locked = 1;                           /* from here on: no heap */
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/F1/run.sh ex1 ex1 EXTRA_LDFLAGS=-Wl,--wrap=pvPortMalloc'
```

```text
#  caller    want  block@     
0  c35  1024  20001290
1  c3f  96  20001698
2  c35  2048  20001700
3  c3f  96  20001f08
4  c35  1536  20001f70
5  c3f  96  20002578
6  1a37  148  200025e0
7  1a37  340  20002680
8  1c1f  84  200027e0
9  2291  32  20002840
10  224f  44  20002868
11  22f1  553  200028a0
allocs=12  heap used by init = 6232 B (free 0 -> 124840)
monitor: calling vTaskListTasks() ...

[HEAP LOCKED] pvPortMalloc(180) from 1469 after init!

[configASSERT] /Users/donh/.rtos_scratch/F1/ex1.c:22
[board_exit] code=253
[rc=1]
```

호출자 주소를 함수 이름으로 바꿨다. 반환 주소는 Thumb 비트(1)가 붙어 있어서 1을 빼고 `addr2line -i`(인라인 펼침)로 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F1/ex1; for a in c35 c3f 1a37 1c1f 2291 224f 22f1 1469; do printf "%s " $a; arm-none-eabi-addr2line -f -i -s -e app.elf $(printf "%x" $((0x$a - 1))) | tr "\n" " "; echo; done'
```

```text
c35 prvCreateTask tasks.c:1683 xTaskCreate tasks.c:1741 
c3f prvCreateTask tasks.c:1689 xTaskCreate tasks.c:1741 
1a37 xQueueGenericCreate queue.c:531 
1c1f xQueueGenericCreate queue.c:531 xQueueCreateMutex queue.c:654 
2291 xEventGroupCreate event_groups.c:156 
224f xTimerCreate timers.c:351 
22f1 xStreamBufferGenericCreate stream_buffer.c:401 
1469 vTaskListTasks tasks.c:7451
```

출력에서 볼 것:

- **태스크 하나 = 할당 두 번.** 스택 먼저(`tasks.c:1683`, 256 words = 1024 B), TCB 나중(`:1689`, 96 B). 순서의 이유(스택 overflow가 자기 TCB를 덮지 않게)는 A4 §2.5에서 다뤘다. 큐는 `Queue_t` 84 B + 저장 공간을 **한 번에** 잡는다: 16 × 4 + 84 = 148, 4 × 64 + 84 = 340. 뮤텍스는 저장 공간이 0인 큐라서 84 B다. 스트림 버퍼는 512 + 1 + 40 = 553 B(용량보다 1바이트 더 잡는 것은 가득 참과 빔을 구별하기 위해서다).
- 요청 12번의 합은 6,097 B인데 heap에서 줄어든 것은 6,232 B다. 블록마다 8 B 헤더(`BlockLink_t`)가 붙고 8 B 정렬로 올림하기 때문이다(블록 크기 합 6,216 B). 나머지 16 B는 처음 초기화 때 heap 시작 정렬과 끝 표지 `pxEnd`가 가져갔다. 작은 객체가 많을수록 헤더 비율이 커진다: 이벤트 그룹 32 B는 40 B 블록이 된다(헤더가 20%).
- `free 0 -> 124840`: **heap_4의 `xPortGetFreeHeapSize()`는 첫 할당 전에는 0을 돌려준다.** `prvHeapInit()`이 첫 `pvPortMalloc` 안에서 늦게 돌기 때문이다. 부팅 직후 "heap이 0"이라고 찍히는 로그는 고장이 아니다.
- `vTaskStartScheduler()`는 할당을 하나도 하지 않았다(목록이 12에서 끝난다). 템플릿이 `configSUPPORT_STATIC_ALLOCATION 1` + `configKERNEL_PROVIDED_STATIC_MEMORY 1`이라 idle·timer 태스크와 타이머 명령 큐(`timers.c`의 `xStaticTimerQueue`)를 커널이 정적 배열로 만들기 때문이다. 정적 할당을 끄면 이 셋도 heap에서 나온다.
- 잠근 뒤 걸린 것은 **`vTaskListTasks()`**다. 태스크 목록을 문자열로 만드는 이 디버그 API는 안에서 `TaskStatus_t` 배열(태스크 5개분, 180 B)을 `pvPortMalloc`으로 잡았다가 푼다(`tasks.c:7451`, `vTaskGetRunTimeStatistics`도 같다). 텔레메트리 태스크가 1초마다 이걸 부르면 그것만으로 실행 중 할당이 된다. 대신 `uxTaskGetSystemState()`에 정적 배열을 넘기면 할당 없이 같은 정보를 얻는다.

### 1.3 잠금을 어떻게 구현하나 — 세 가지 방법

| 방법 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| 링커 `--wrap=pvPortMalloc` | 예제 1처럼 래퍼에서 플래그 검사 | 커널·heap 소스 수정 없음, 호출자 주소까지 얻음 | heap 파일 안에서 부르는 호출(`pvPortCalloc` → `pvPortMalloc`)은 같은 파일이라 감싸지 않는다 |
| `traceMALLOC(p, n)` 매크로 | `FreeRTOSConfig.h`에서 정의 → `pvPortMalloc` 안에서 불린다 | FreeRTOS 공식 훅, 실패한 할당도 보인다 | 이미 할당한 뒤에 불린다, 호출자 주소를 얻기 어렵다 |
| 정적 전용 빌드 | `configSUPPORT_DYNAMIC_ALLOCATION 0` + heap 파일 미링크 | 실행 중 할당이 **물리적으로 불가능**, 위반은 컴파일·링크 에러 | 모든 객체에 버퍼를 직접 줘야 한다 (§5) |

잠금 시점은 "스케줄러 시작"보다 **"초기화 완료"**로 잡는다. 초기화 태스크가 스케줄러 시작 뒤에 다른 태스크를 만드는 구조가 흔하기 때문이다. `xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED`로 검사하면 그 구조에서 오탐이 난다.

함정: libc의 `malloc`은 이 잠금에 안 걸린다. newlib-nano의 `printf`는 처음 출력할 때 `stdout` 버퍼를 `_malloc_r`로 잡는다(B11). 그 경로는 `_sbrk`로 가므로 `_sbrk`에도 같은 잠금을 넣거나, libc malloc이 아예 링크되지 않았는지 `nm`으로 확인해야 한다. libc malloc과 RTOS 태스크가 섞일 때의 락 문제(`__malloc_lock`이 기본으로 빈 함수)는 F7에서 다룬다.

---

## 2. FreeRTOS heap_1 ~ heap_5 — 다섯 할당기의 내부

### 2.1 한눈에

FreeRTOS 커널은 할당기를 하나 고르라고 한다. `portable/MemMang/heap_N.c` 중 정확히 하나를 링크한다(정적 전용 빌드는 0개). 모두 같은 함수 이름(`pvPortMalloc`, `vPortFree`)을 정의하므로 둘을 같이 링크하면 중복 정의 에러가 난다.

| | heap_1 | heap_2 | heap_3 | heap_4 | heap_5 |
|---|---|---|---|---|---|
| free 가능 | 아니오 (`vPortFree`는 assert) | 예 | 예 | 예 | 예 |
| 빈 블록 목록 순서 | 목록 없음 (bump) | **크기 순** (작은 것부터) | libc가 정함 | **주소 순** | 주소 순 |
| 이웃 병합 | — | **안 함** | libc가 정함 | 함 | 함 |
| 고르는 방식 | 다음 바이트 | best fit (크기 순 목록의 첫 맞는 것) | libc | first fit (낮은 주소 첫 맞는 것) | first fit |
| heap 메모리 | `ucHeap[configTOTAL_HEAP_SIZE]` | `ucHeap[]` | 링커의 heap 영역(`_sbrk`) | `ucHeap[]` | `vPortDefineHeapRegions()`로 준 여러 영역 |
| 블록 헤더 | 없음 | 8 B | libc 헤더 | 8 B | 8 B |
| 최소 사용량 통계 | 없음 | 없음 | 없음 | `xPortGetMinimumEverFreeHeapSize` | 같음 |
| `vPortGetHeapStats` | 없음 | 없음 | 없음 | 있음 | 있음 |
| `configHEAP_CLEAR_MEMORY_ON_FREE` | — | 있음 | — | 있음 | 있음 |
| `configENABLE_HEAP_PROTECTOR` | — | — | — | 있음 | 있음 |
| 쓰는 곳 | 초기화 시점 할당만 하는 앱, 인증 프로젝트 | 같은 크기만 반복 할당·해제하는 앱 (사실상 heap_4로 대체) | 서드파티가 libc malloc을 꼭 써야 할 때 | **일반적인 기본값** | 내부 SRAM + 외부 SDRAM처럼 떨어진 RAM 여러 개 |

모든 heap(heap_3 제외)은 `vTaskSuspendAll()` / `xTaskResumeAll()`로 감싸서 태스크끼리 상호 배제한다. 인터럽트는 끄지 않는다. 그래서 **ISR에서 `pvPortMalloc`을 부르면 안 된다**(스케줄러 잠금은 ISR을 막지 못하므로, 태스크가 목록을 고치는 도중에 ISR이 들어와 같은 목록을 고치면 깨진다). 그리고 할당이 길어지면 그동안 **어떤 태스크로도 컨텍스트 스위치가 안 된다.** 높은 우선순위 태스크가 깨어나도 heap 연산이 끝날 때까지 기다린다. heap 연산 시간이 바로 스케줄러 잠금 시간이다(스케줄러 잠금과 크리티컬 섹션의 차이는 C7).

### 2.2 공통 자료구조 — `BlockLink_t`

heap_2/4/5는 같은 헤더를 쓴다(V11.3.2 `heap_4.c`).

```c
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK * pxNextFreeBlock; /**< The next free block in the list. */
    size_t xBlockSize;                     /**< The size of the free block. */
} BlockLink_t;
```

블록 하나의 모양은 이렇다(주소는 아래에서 위로).

```
 +---------------------------+  <- block start (8-byte aligned)
 | pxNextFreeBlock (4 B)     |     free: next free block / allocated: NULL (or canary)
 | xBlockSize      (4 B)     |     size incl. header; MSB = 1 means "allocated"
 +---------------------------+  <- pointer returned by pvPortMalloc
 | user data ...             |
 | (rounded up to 8 bytes)   |
 +---------------------------+  <- next block header
```

- `xBlockSize`의 **최상위 비트**가 "할당됨" 표시다(`heapBLOCK_ALLOCATED_BITMASK`). 그래서 한 블록의 최대 크기는 2 GB이고, `vPortFree`는 이 비트가 서 있는지 assert로 확인한다. 이미 푼 블록을 또 풀면 비트가 0이라 걸린다(예제 8).
- 할당된 블록의 `pxNextFreeBlock`은 NULL이다. 이것도 `vPortFree`가 확인한다. 앞 블록의 버퍼가 넘쳐서 이 칸을 덮으면 대개 걸린다(예제 8에서 "대개"가 아닌 경우를 본다).
- 헤더 크기 `xHeapStructSize`는 `sizeof(BlockLink_t)`를 `portBYTE_ALIGNMENT`(Cortex-M = 8)로 올림한 값이라 8 B다. 남은 조각이 `heapMINIMUM_BLOCK_SIZE`(헤더의 2배 = 16 B)보다 크지 않으면 쪼개지 않고 통째로 준다.
- 목록의 양 끝: `xStart`(정적 변수, 크기 0인 머리)와 끝 표지. heap_2는 끝 표지도 정적 변수 `xEnd`이고 그 크기를 heap 전체 크기로 둔다(크기 순 목록이라 "가장 큰 값"이 끝을 뜻한다). heap_4/5는 끝 표지 `pxEnd`를 **heap 영역의 맨 끝 8 B 안에** 놓는다. 그래서 heap_4는 heap에서 8 B를 덜 쓸 수 있다.

### 2.3 heap_1 — 앞으로만 가는 포인터

`pvPortMalloc`은 크기를 8로 올림하고 `xNextFreeByte`를 그만큼 더할 뿐이다. 목록도 헤더도 없다. 할당 시간이 **입력과 상관없이 일정**하고(예제 3), 단편화가 생길 수 없다. 대신 `vPortFree(pv)`는 `configASSERT(pv == NULL)`이다. 태스크·큐를 지우지 않는 앱, 즉 "초기화 때 다 만들고 끝"인 앱에 맞는다. 정책 2번(초기화 시점 할당)을 할당기 수준에서 강제하는 셈이다. `vTaskDelete`를 쓰는 앱에 heap_1을 넣으면 idle 태스크가 TCB를 회수하다가(A4, C9) assert에 걸린다.

### 2.4 heap_2 — 크기 순 목록, 병합 없음

빈 블록 목록이 **크기 순**이다. `prvInsertBlockIntoFreeList`는 매크로로 되어 있고, 자기보다 큰 블록을 만날 때까지 목록을 걷는다. `pvPortMalloc`은 작은 것부터 걸으므로 처음 맞는 블록이 곧 가장 작게 맞는 블록이다(best fit). 그리고 **이웃 블록을 합치지 않는다.** 소스 머리 주석이 직접 말한다: "does not combine adjacent free blocks into a single larger block (and so will fragment memory)".

그래서 heap_2에서 블록은 쪼개지기만 하고 다시 커지지 않는다. 512 B 블록을 풀면 그 자리는 영원히 512 B 이하 요청에만 쓰인다. 같은 크기로 만들고 지우기를 반복하는 앱(같은 스택 크기의 태스크를 계속 만들고 지우는 앱)에서는 그 블록이 그대로 재사용되니 괜찮다. 크기가 섞이면 무너진다(§4).

### 2.5 heap_4 — 주소 순 목록, 이웃 병합

목록이 **주소 순**이다. `vPortFree`의 `prvInsertBlockIntoFreeList`가 자기 자리를 찾을 때까지 걷고, 자리에서 두 가지를 확인한다.

1. 바로 앞 빈 블록의 끝(`pxIterator + xBlockSize`)이 내 시작이면 → 앞 블록에 붙는다.
2. 내 끝이 바로 다음 빈 블록의 시작이면 → 다음 블록을 흡수한다(다음이 `pxEnd`면 제외).

둘 다 맞으면(구멍 하나를 메운 경우) 세 블록이 하나가 된다. 이 병합 덕분에 이웃끼리 같이 풀린 공간이 다시 큰 블록으로 돌아온다. `pvPortMalloc`은 낮은 주소부터 처음 맞는 블록을 고른다(first fit). 잘라 낸 나머지는 원래 블록 자리에 그대로 남으니 목록 순서가 저절로 유지된다(삽입 탐색이 필요 없다 — heap_2보다 malloc 쪽이 단순하다).

### 2.6 예제 2 — 같은 순서로 풀어도 목록 모양이 다르다

무엇을 확인하는 코드인가: 1 KB heap에 A~E를 할당하고 B, D, C 순으로 푼 뒤, heap_2와 heap_4의 빈 블록 목록을 걸어서 찍는다. 마지막에 500 B를 요청한다. 오프셋은 `ucHeap` 시작 기준이다.

```c
/* ex2 — free-list shape: heap_2 (size-ordered, no merge) vs heap_4 (address-ordered, merge) */
#include "FreeRTOS.h"
#include "task.h"
#include "heapsel.h"
#include "board.h"

void vApplicationMallocFailedHook(void) { }        /* keep running on failure */

static void dump(const char *tag)
{
    board_printf_raw("%s free list:", tag);
    for (BlockLink_t *b = xStart.pxNextFreeBlock; b != FL_END; b = b->pxNextFreeBlock)
        board_printf_raw(" [+%u %uB]", (unsigned)((uint8_t *)b - ucHeap), (unsigned)b->xBlockSize);
    board_printf_raw("   (free %u)\n", (unsigned)xPortGetFreeHeapSize());
}

static void app(void *pv)
{
    (void)pv;
    uint8_t *A = pvPortMalloc(100), *B = pvPortMalloc(200), *C = pvPortMalloc(100);
    uint8_t *D = pvPortMalloc(300), *E = pvPortMalloc(200);
    board_printf_raw("heap_%d: A+%u B+%u C+%u D+%u E+%u\n", HN, (unsigned)(A - ucHeap),
        (unsigned)(B - ucHeap), (unsigned)(C - ucHeap), (unsigned)(D - ucHeap), (unsigned)(E - ucHeap));
    dump("after A..E     ");
    vPortFree(B); dump("free B         ");
    vPortFree(D); dump("free D         ");
    vPortFree(C); dump("free C         ");
    void *X = pvPortMalloc(500);
    board_printf_raw("malloc(500) -> %s\n", X ? "OK" : "NULL  (total free is enough, no single block is)");
    board_exit(0);
}

int main(void)
{
    xTaskCreateStatic(app, "app", 512, NULL, 1, (static StackType_t[512]){0}, &(static StaticTask_t){0});
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for h in 2 4; do /Users/donh/.rtos_scratch/F1/run.sh ex2 ex2_h$h HEAP=none EXTRA_CFLAGS="-DHN=$h -DconfigTOTAL_HEAP_SIZE=1024"; done'
```

```text
heap_2: A+12 B+124 C+332 D+444 E+756
after A..E      free list: [+956 64B]   (free 64)
free B          free list: [+956 64B] [+116 208B]   (free 272)
free D          free list: [+956 64B] [+116 208B] [+436 312B]   (free 584)
free C          free list: [+956 64B] [+324 112B] [+116 208B] [+436 312B]   (free 696)
malloc(500) -> NULL  (total free is enough, no single block is)
[board_exit] code=0
[rc=0]
heap_4: A+12 B+124 C+332 D+444 E+756
after A..E      free list: [+956 56B]   (free 56)
free B          free list: [+116 208B] [+956 56B]   (free 264)
free D          free list: [+116 208B] [+436 312B] [+956 56B]   (free 576)
free C          free list: [+116 632B] [+956 56B]   (free 688)
malloc(500) -> OK
[board_exit] code=0
[rc=0]
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle">A..E를 할당하고 B, D, C 순서로 free한 뒤 (heap 1024 B, 예제 2)</text>
<text x="40" y="48" font-size="13">heap_2 — 빈 블록 4개, 크기 순 목록: 64 → 112 → 208 → 312</text>
<rect x="42.3" y="56" width="65.6" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="75.2" y="78" font-size="12" text-anchor="middle">A 사용</text>
<rect x="108.0" y="56" width="121.9" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="168.9" y="78" font-size="12" text-anchor="middle">빈 208</text>
<rect x="229.8" y="56" width="65.6" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="262.7" y="78" font-size="12" text-anchor="middle">빈 112</text>
<rect x="295.5" y="56" width="182.8" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="386.9" y="78" font-size="12" text-anchor="middle">빈 312</text>
<rect x="478.3" y="56" width="121.9" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="539.2" y="78" font-size="12" text-anchor="middle">E 사용</text>
<rect x="600.2" y="56" width="37.5" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="618.9" y="78" font-size="12" text-anchor="middle">64</text>
<text x="40" y="142" font-size="13">heap_4 — 빈 블록 2개, 주소 순 목록: +116 → +956 (B+C+D 병합)</text>
<rect x="42.3" y="150" width="65.6" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="75.2" y="172" font-size="12" text-anchor="middle">A 사용</text>
<rect x="108.0" y="150" width="370.3" height="34" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="293.1" y="172" font-size="12" text-anchor="middle">합쳐진 빈 블록 632</text>
<rect x="478.3" y="150" width="121.9" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="539.2" y="172" font-size="12" text-anchor="middle">E 사용</text>
<rect x="600.2" y="150" width="32.8" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="616.6" y="172" font-size="12" text-anchor="middle">56</text>
<line x1="40.0" y1="190" x2="40.0" y2="196" stroke="currentColor"/>
<text x="40.0" y="210" font-size="12" text-anchor="middle">+0</text>
<line x1="190.0" y1="190" x2="190.0" y2="196" stroke="currentColor"/>
<text x="190.0" y="210" font-size="12" text-anchor="middle">+256</text>
<line x1="340.0" y1="190" x2="340.0" y2="196" stroke="currentColor"/>
<text x="340.0" y="210" font-size="12" text-anchor="middle">+512</text>
<line x1="490.0" y1="190" x2="490.0" y2="196" stroke="currentColor"/>
<text x="490.0" y="210" font-size="12" text-anchor="middle">+768</text>
<line x1="640.0" y1="190" x2="640.0" y2="196" stroke="currentColor"/>
<text x="640.0" y="210" font-size="12" text-anchor="middle">+1024</text>
<line x1="40" y1="190" x2="640" y2="190" stroke="currentColor"/>
<text x="340" y="242" font-size="13" text-anchor="middle">malloc(500): heap_2는 NULL (빈 공간 합 696 B, 가장 큰 블록 312 B)</text>
<text x="340" y="264" font-size="13" text-anchor="middle">heap_4는 OK (632 B 블록 하나에서 잘라 준다)</text>
</svg>
```
그림 2 — 예제 2의 마지막 상태를 오프셋 그대로 그렸다. 초록 = 빈 블록(숫자는 바이트), 주황 = heap_4가 병합한 블록, 회색 = 사용 중. 같은 할당·해제 순서에서 heap_2는 빈 블록 4개, heap_4는 2개다.

출력에서 볼 것:

- `A+12`: 첫 블록 헤더는 +4에 있다(`ucHeap`이 4 B 정렬이라 8 B 정렬로 4 B를 버렸다), 사용자 포인터는 그 뒤 8 B인 +12. 100 B 요청이 112 B 블록이 됐다(100 + 8 = 108 → 8의 배수 112).
- heap_2의 목록은 **크기 순**이다: 64 → 112 → 208 → 312. 주소는 뒤죽박죽이다. heap_4의 목록은 **주소 순**이다: +116 → +956.
- C를 풀 때 heap_4는 B(+116, 208) 뒤에 C(+324)가 바로 붙고 C 뒤에 D(+436)가 바로 붙는 것을 보고 셋을 하나(632 B)로 합쳤다. heap_2는 112 B짜리 블록을 하나 더 늘렸을 뿐이다.
- 빈 공간 합은 heap_2 696 B, heap_4 688 B로 거의 같다(heap_4는 `pxEnd`가 끝 8 B를 쓴다). 그런데 500 B 요청은 heap_2에서 **실패**했다. 이것이 단편화(fragmentation)의 정의 그대로다: **빈 공간의 합은 충분한데, 연속된 한 조각이 없다.**

### 2.7 heap_3 — libc malloc 감싸기

heap_3은 106줄짜리 파일이다. `pvPortMalloc`은 `vTaskSuspendAll(); malloc(n); xTaskResumeAll();`이 전부다. 할당기는 libc의 것이고(랩은 newlib-nano: 주소 순 빈 목록 + 병합), heap 크기는 `configTOTAL_HEAP_SIZE`가 아니라 **링커 스크립트의 heap 영역**(`_sbrk`가 늘리는 `__heap_start`~`__heap_end`, B5·B11)이 정한다. 그래서 `xPortGetFreeHeapSize()`도 없다.

쓸 이유는 하나다. 서드파티 코드가 `malloc`을 직접 부르는데, 그것과 RTOS 객체가 같은 heap을 나눠 쓰게 하고 싶을 때. 이때 스케줄러 잠금은 `pvPortMalloc`을 거치는 호출만 보호한다. 서드파티가 `malloc`을 직접 부르면 보호가 없다 → newlib의 `__malloc_lock`/`__malloc_unlock`을 구현해야 한다(F7). 그리고 libc 할당기의 최악 시간은 FreeRTOS가 아니라 libc 구현에 달렸다.

### 2.8 heap_5 — 떨어진 RAM 여러 개를 한 목록으로

heap_5는 heap_4와 같은 알고리즘인데, heap 메모리를 `ucHeap` 배열 하나가 아니라 **여러 영역**에서 받는다.

```c
HeapRegion_t xHeapRegions[] =
{
    { ( uint8_t * ) 0x20000000UL, 0x10000 },   /* 내부 SRAM 64 KB */
    { ( uint8_t * ) 0xC0000000UL, 0x200000 },  /* 외부 SDRAM 2 MB */
    { NULL, 0 }                                /* 끝 */
};
vPortDefineHeapRegions( xHeapRegions );        /* 첫 pvPortMalloc보다 먼저, 정확히 한 번 */
```

`vPortDefineHeapRegions()`(V11.3.2 `heap_5.c`)는 영역마다 첫 빈 블록과 끝 표지(크기 0인 블록)를 만들고, **앞 영역의 끝 표지가 다음 영역의 첫 블록을 가리키게** 이어서 주소 순 목록 하나로 만든다. 영역은 주소 오름차순이어야 하고(assert로 확인), 두 번 부르면 assert다. heap_4와 다른 점이 몇 가지 있다.

- heap이 자동으로 초기화되지 않는다. `pvPortMalloc`이 `configASSERT( pxEnd )`로 먼저 확인한다. 정적 생성자나 초기화 순서 때문에 `vPortDefineHeapRegions` 전에 객체를 하나라도 만들면 그 자리에서 assert다.
- 영역 사이에는 크기 0인 끝 표지가 끼어 있어서 두 영역이 주소상 붙어 있어도 병합되지 않는다. `vPortGetHeapStats`는 최소 블록 크기를 셀 때 크기 0 블록을 건너뛴다.
- 블록 포인터 범위 검사(`heapVALIDATE_BLOCK_POINTER`)가 **`configENABLE_HEAP_PROTECTOR`가 1일 때만** 켜진다. heap_4는 이 검사가 protector와 상관없이 `configASSERT`만 정의되어 있으면 항상 돈다. 영역이 여럿이라 "heap 범위"가 하나의 배열이 아니기 때문이다(protector가 켜지면 가장 낮은·높은 주소를 기록해서 검사한다. `configVALIDATE_HEAP_BLOCK_POINTER`로 직접 정의할 수도 있다 — V11.2.0에서 추가). 이 차이가 예제 3에서 heap_5가 heap_4보다 빠르게 나오는 이유다.
- V11.3.1에서 고친 버그: protector를 켠 heap_5에서 할당이 실패하면(목록 끝 표지까지 간 경우) 끝 표지에 범위 검사를 해서 **NULL 대신 assert**가 났다. V11.3.2 소스에는 `if( pxBlock != pxEnd )` 가드가 들어가 있다(History.txt V11.3.1 항목). 그 전 버전으로 heap_5 + protector를 쓰는 프로젝트는 "메모리 부족이 HardFault/assert로 보이는" 증상을 겪는다.

어느 영역에 무엇을 둘지(빠른 SRAM에는 스택, 느린 SDRAM에는 큰 버퍼)는 heap_5가 고르지 못한다. 주소 순 first fit이라 **낮은 주소 영역부터** 채운다. 스택만 다른 영역에서 받고 싶으면 `configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 1`로 `pvPortMallocStack`/`vPortFreeStack`을 직접 구현한다(`portable.h`). 메모리 뱅크 배치 실무는 F8에서 다룬다.

---
## 3. 할당 시간의 결정성 — "얼마나 걸리나"를 식으로

### 3.1 직관 — 목록을 걷는 할당기는 목록 길이만큼 느리다

heap_2/4/5의 `pvPortMalloc`은 빈 블록 목록을 처음부터 걸으면서 맞는 블록을 찾는다. `vPortFree`도 자기 자리를 찾을 때까지 목록을 걷는다. 그러니 시간은 **(고정 비용) + (블록 하나당 비용) × (지나간 빈 블록 수)**다. 이 "지나간 수"는 요청 크기와 지금까지의 할당·해제 역사에 따라 달라진다. 결정적(deterministic)이란 "항상 같은 시간"이라는 뜻이 아니라 **"상한을 미리 말할 수 있다"**는 뜻이다. 그러려면 두 가지가 필요하다. 블록 하나당 비용(측정 또는 명령 수 계산)과, 목록 길이의 상한(분석).

heap_1은 목록이 없으니 상수다. 고정 블록 풀(F2)도 "빈 칸 목록의 머리에서 하나 꺼내기"라 상수다. TLSF처럼 크기 등급별 비트맵으로 O(1)을 만드는 범용 할당기도 있다(Zephyr `sys_heap`도 크기 등급 bucket을 쓴다 — 내부는 F2).

### 3.2 예제 3 — 같은 무작위 부하에서 heap_1~heap_5의 시간 분포

무엇을 확인하는 코드인가: 슬롯 64개에 대해 3000번 "슬롯이 비었으면 16~1039 B 할당, 차 있으면 해제"를 같은 난수 순서로 돌린다. 매 호출을 인터럽트를 끈 채(tick 인터럽트가 끼어드는 것을 빼려고) FPGAIO 카운터로 잰다. heap_1은 free가 없으니 작은 크기(16~79 B)로 할당만 한다. heap_5는 32 KB 두 영역(사이에 256 B 틈), 나머지는 64 KB 하나다. heap_3은 newlib-nano malloc이다.

```c
/* ex3 — alloc/free time distribution under one random workload, heap_1..5 (FPGAIO 25 MHz counter) */
#include "FreeRTOS.h"
#include "task.h"
#include "heapsel.h"
#include "board.h"
#define CNT()  REG32(0x40028018u)                 /* MPS2 FPGAIO COUNTER, 25 MHz */
#define NOPS   3000
#define SLOTS  64
static uint32_t rng = 12345;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static size_t rsize(void) { return (size_t)16u << (xr() % 7u) | (xr() & 15u); } /* 16..1039, log-ish */
static void *slot[SLOTS];
static uint16_t tm[NOPS]; static uint8_t kind[NOPS];      /* 1 = malloc, 2 = free, 3 = failed malloc */
static unsigned fails;
void vApplicationMallocFailedHook(void) { fails++; }
#if HN == 5
static uint8_t pool[65536 + 256] __attribute__((aligned(8)));
static const HeapRegion_t regions[] = { { pool, 32768 }, { pool + 32768 + 256, 32768 }, { NULL, 0 } };
#endif
static void app(void *pv)
{
    (void)pv;
    uint32_t a = CNT(), b = CNT(), ovh = b - a;
    size_t maxblk = 0;
    for (int i = 0; i < NOPS; i++) {
        int s = (int)(xr() % SLOTS);
        uint32_t t0, t1;
        if (HN == 1 || slot[s] == NULL) {
            size_t n = rsize();
            if (HN == 1) n = 16 + (xr() & 63);           /* heap_1 never frees: small sizes */
            __asm volatile("cpsid i");
            t0 = CNT(); slot[s] = pvPortMalloc(n); t1 = CNT();
            __asm volatile("cpsie i");
            kind[i] = slot[s] ? 1 : 3;
        } else {
            __asm volatile("cpsid i");
            t0 = CNT(); vPortFree(slot[s]); t1 = CNT();
            __asm volatile("cpsie i");
            slot[s] = NULL; kind[i] = 2;
        }
        tm[i] = (uint16_t)(t1 - t0);
#ifdef FL_END
        size_t nb, lg, tot; fl_stats(&nb, &lg, &tot); if (nb > maxblk) maxblk = nb;
#endif
    }
    board_printf_raw("heap_%d ovh=%u fails=%u max_free_blocks=%u\n", HN, (unsigned)ovh, fails, (unsigned)maxblk);
    for (int i = 0; i < NOPS; i++)
        board_printf_raw("%c%u%s", "?mfx"[kind[i]], tm[i], (i % 20 == 19) ? "\n" : " ");
    board_printf_raw("END\n");
    board_exit(0);
}
int main(void)
{
#if HN == 5
    vPortDefineHeapRegions(regions);
#endif
    xTaskCreateStatic(app, "app", 512, NULL, 1, (static StackType_t[512]){0}, &(static StaticTask_t){0});
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for h in 1 2 3 4 5; do /Users/donh/.rtos_scratch/F1/run.sh ex3 ex3_h$h HEAP=none EXTRA_CFLAGS="-DHN=$h -DconfigTOTAL_HEAP_SIZE=65536" > /var/tmp/rtos/F1/ex3_h$h.out 2>&1; head -3 /var/tmp/rtos/F1/ex3_h$h.out; done'
```

```text
heap_1 ovh=1 fails=1715 max_free_blocks=0
m80 m73 m77 m77 m77 m76 m77 m77 m77 m77 m76 m77 m77 m77 m77 m76 m77 m77 m77 m77
m76 m77 m77 m77 m77 m76 m77 m77 m77 m77 m76 m77 m77 m77 m77 m76 m73 m77 m77 m77
heap_2 ovh=0 fails=0 max_free_blocks=85
m115 m107 m102 m107 m107 m107 m108 m107 m107 f76 m107 m117 m117 m117 m116 f81 f85 m126 m97 m117
f76 m103 m117 m102 m117 m112 m112 f81 m97 m107 m107 m117 f80 f81 m126 m97 m107 f81 m127 f81
heap_3 ovh=1 fails=0 max_free_blocks=0
m141 m121 m122 m122 m122 m121 m121 m122 m122 f78 m99 m128 m128 m128 m128 f86 f94 m105 m99 m104
f82 m111 m111 m111 m139 m139 m139 f96 m96 m105 m104 m139 f83 f100 m99 m100 m99 f96 m99 f104
heap_4 ovh=1 fails=0 max_free_blocks=19
m138 m126 m121 m126 m125 m125 m126 m126 m126 f95 m125 m135 m135 m134 m134 f108 f108 m134 m119 m125
f95 m134 m134 m134 m135 m130 m129 f101 m119 m126 m125 m135 f95 f111 m125 m125 m125 f102 m126 f111
heap_5 ovh=1 fails=0 max_free_blocks=20
m108 m112 m108 m112 m112 m112 m112 m112 m112 f88 m112 m118 m119 m118 m118 f96 f96 m119 m106 m112
f88 m118 m119 m118 m119 m114 m114 f91 m107 m112 m112 m119 f88 f100 m112 m112 m112 f91 m112 f100
```

(각 파일은 3000개 값과 `END`까지 이어진다. 앞 두 줄만 보였다.) `ovh`는 카운터를 두 번 연달아 읽은 차이(0~2 tick)이고, 40 ns 눈금이 명령 32 ns와 맞지 않아 생기는 양자화라 빼지 않았다. 분포는 Python으로 정리했다.

```python
# ex3_stats.py — percentiles of malloc/free time per heap (counter ticks of 40 ns)
import re, sys, statistics as st
def load(h):
    toks = open(f"data/ex3_h{h}.out").read().split("END")[0].split()
    return [(t[0], int(t[1:])) for t in toks if re.fullmatch(r"[mfx]\d+", t)]
def pct(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(p / 100 * len(v)))]
print(f"{'heap':7}{'op':7}{'n':>6}{'min':>7}{'p50':>7}{'p90':>7}{'p99':>7}{'max':>7}  max-min (us)")
for h in range(1, 6):
    d = load(h)
    for op, name in (("m", "malloc"), ("f", "free"), ("x", "fail")):
        v = [t for k, t in d if k == op]
        if not v: continue
        print(f"heap_{h} {name:7}{len(v):6}{min(v):7}{pct(v,50):7}{pct(v,90):7}{pct(v,99):7}{max(v):7}"
              f"  {(max(v)-min(v))*0.04:6.2f}")
```

```text
heap   op          n    min    p50    p90    p99    max  max-min (us)
heap_1 malloc   1285     72     77     77     77     80    0.32
heap_1 fail     1715     73     78     78     78     78    0.20
heap_2 malloc   1518     92    225    428    500    760   26.72
heap_2 free     1482     76    169    309    361    385   12.36
heap_3 malloc   1518     95    127    167    189    217    4.88
heap_3 free     1482     78    115    148    162    172    3.76
heap_4 malloc   1518    113    153    215    249    284    6.84
heap_4 free     1482     95    116    138    152    155    2.40
heap_5 malloc   1518    102    132    176    202    227    5.00
heap_5 free     1482     88    106    128    141    145    2.28
```

```svg
<svg viewBox="0 0 680 354" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle">무작위 alloc/free 3000회 — 1회 시간 (us, QEMU icount)</text>
<text x="192" y="57" font-size="12" text-anchor="end">heap_1 malloc</text>
<line x1="240.5" y1="53" x2="245.0" y2="53" stroke="#888" stroke-width="1.5"/>
<rect x="243.3" y="46" width="2.0" height="14" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="243.3" y1="44" x2="243.3" y2="62" stroke="currentColor" stroke-width="2"/>
<text x="251.0" y="57" font-size="12">3.2</text>
<text x="192" y="85" font-size="12" text-anchor="end">heap_2 malloc</text>
<line x1="251.8" y1="81" x2="627.5" y2="81" stroke="#d0564a" stroke-width="1.5"/>
<rect x="326.6" y="74" width="154.7" height="14" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<line x1="326.6" y1="72" x2="326.6" y2="90" stroke="currentColor" stroke-width="2"/>
<text x="600.0" y="85" font-size="12">30.4</text>
<text x="192" y="113" font-size="12" text-anchor="end">heap_3 (newlib-nano) malloc</text>
<line x1="253.4" y1="109" x2="322.1" y2="109" stroke="#e08a3c" stroke-width="1.5"/>
<rect x="271.4" y="102" width="34.9" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="271.4" y1="100" x2="271.4" y2="118" stroke="currentColor" stroke-width="2"/>
<text x="328.1" y="113" font-size="12">8.7</text>
<text x="192" y="141" font-size="12" text-anchor="end">heap_4 malloc</text>
<line x1="263.6" y1="137" x2="359.8" y2="137" stroke="#4a7bd0" stroke-width="1.5"/>
<rect x="286.1" y="130" width="54.0" height="14" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="286.1" y1="128" x2="286.1" y2="146" stroke="currentColor" stroke-width="2"/>
<text x="365.8" y="141" font-size="12">11.4</text>
<text x="192" y="169" font-size="12" text-anchor="end">heap_5 malloc</text>
<line x1="257.4" y1="165" x2="327.7" y2="165" stroke="#3f9a6b" stroke-width="1.5"/>
<rect x="274.2" y="158" width="39.4" height="14" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="274.2" y1="156" x2="274.2" y2="174" stroke="currentColor" stroke-width="2"/>
<text x="333.7" y="169" font-size="12">9.1</text>
<text x="192" y="211" font-size="12" text-anchor="end">heap_2 free</text>
<line x1="242.8" y1="207" x2="416.6" y2="207" stroke="#d0564a" stroke-width="1.5"/>
<rect x="295.1" y="200" width="108.0" height="14" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<line x1="295.1" y1="198" x2="295.1" y2="216" stroke="currentColor" stroke-width="2"/>
<text x="422.6" y="211" font-size="12">15.4</text>
<text x="192" y="239" font-size="12" text-anchor="end">heap_3 (newlib-nano) free</text>
<line x1="243.9" y1="235" x2="296.8" y2="235" stroke="#e08a3c" stroke-width="1.5"/>
<rect x="264.7" y="228" width="26.4" height="14" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="264.7" y1="226" x2="264.7" y2="244" stroke="currentColor" stroke-width="2"/>
<text x="302.8" y="239" font-size="12">6.9</text>
<text x="192" y="267" font-size="12" text-anchor="end">heap_4 free</text>
<line x1="253.4" y1="263" x2="287.2" y2="263" stroke="#4a7bd0" stroke-width="1.5"/>
<rect x="265.2" y="256" width="20.2" height="14" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="265.2" y1="254" x2="265.2" y2="272" stroke="currentColor" stroke-width="2"/>
<text x="293.2" y="267" font-size="12">6.2</text>
<text x="192" y="295" font-size="12" text-anchor="end">heap_5 free</text>
<line x1="249.5" y1="291" x2="281.6" y2="291" stroke="#3f9a6b" stroke-width="1.5"/>
<rect x="259.6" y="284" width="19.7" height="14" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="259.6" y1="282" x2="259.6" y2="300" stroke="currentColor" stroke-width="2"/>
<text x="287.6" y="295" font-size="12">5.8</text>
<line x1="200" y1="310" x2="650" y2="310" stroke="currentColor"/>
<line x1="200.0" y1="310" x2="200.0" y2="314" stroke="currentColor"/>
<text x="200.0" y="327" font-size="12" text-anchor="middle">0</text>
<line x1="256.2" y1="310" x2="256.2" y2="314" stroke="currentColor"/>
<text x="256.2" y="327" font-size="12" text-anchor="middle">4</text>
<line x1="312.5" y1="310" x2="312.5" y2="314" stroke="currentColor"/>
<text x="312.5" y="327" font-size="12" text-anchor="middle">8</text>
<line x1="368.8" y1="310" x2="368.8" y2="314" stroke="currentColor"/>
<text x="368.8" y="327" font-size="12" text-anchor="middle">12</text>
<line x1="425.0" y1="310" x2="425.0" y2="314" stroke="currentColor"/>
<text x="425.0" y="327" font-size="12" text-anchor="middle">16</text>
<line x1="481.2" y1="310" x2="481.2" y2="314" stroke="currentColor"/>
<text x="481.2" y="327" font-size="12" text-anchor="middle">20</text>
<line x1="537.5" y1="310" x2="537.5" y2="314" stroke="currentColor"/>
<text x="537.5" y="327" font-size="12" text-anchor="middle">24</text>
<line x1="593.8" y1="310" x2="593.8" y2="314" stroke="currentColor"/>
<text x="593.8" y="327" font-size="12" text-anchor="middle">28</text>
<line x1="650.0" y1="310" x2="650.0" y2="314" stroke="currentColor"/>
<text x="650.0" y="327" font-size="12" text-anchor="middle">32</text>
<text x="340" y="344" font-size="12" text-anchor="middle">세로 막대 = p50, 상자 = p50~p99, 선 = min~max, 오른쪽 숫자 = max</text>
</svg>
```
그림 3 — 예제 3의 분포를 us로 바꿨다(1 tick = 40 ns). heap_1은 선 하나(3.1 us 근처 고정), heap_2는 꼬리가 30 us까지 간다. 같은 부하에서 빈 블록 수 최대값은 heap_2 85개, heap_4 19개, heap_5 20개였다.

출력에서 볼 것:

- **heap_1은 모든 할당이 72~80 tick(약 3 us)**이다. 실패한 할당(heap이 찬 뒤 1715번)도 같은 시간이다. 범위 0.32 us는 카운터 양자화 수준이다. 3 us가 생각보다 큰 이유는 할당기가 아니라 감싸는 `vTaskSuspendAll()`/`xTaskResumeAll()`이다(재개할 때 크리티컬 섹션에 들어가 pending ready 목록과 밀린 tick을 확인한다). heap 연산의 바닥 비용은 사실상 이 스케줄러 잠금 비용이다.
- **heap_2가 가장 느리고 꼬리가 가장 길다**(malloc max 760 tick = 30.4 us). 병합을 안 하니 빈 블록이 85개까지 쌓였고, 큰 요청은 작은 블록을 전부 지나야 한다. "heap_2가 heap_4보다 단순해서 빠르다"는 틀린 직관이다.
- heap_4의 꼬리는 heap_2의 1/3 수준이다(malloc max 11.4 us). 빈 블록이 19개를 넘지 않았기 때문이다(§3.4에서 이 상한을 증명한다).
- heap_5가 heap_4보다 p50 기준 14% 빠르다. 알고리즘은 같다. 차이는 §2.8에서 본 **블록 포인터 범위 검사**다. heap_4는 `configASSERT`가 정의되어 있으면 목록을 한 칸 걸을 때마다 `heapVALIDATE_BLOCK_POINTER`(비교 두 번)를 하고, heap_5는 protector가 꺼져 있으면 그 검사가 빈 매크로다. 바로 아래에서 확인한다.
- heap_3(newlib-nano)은 heap_4와 비슷한 꼴이다. 이 부하에서는 오히려 조금 빠르다. 다만 그 시간은 libc 버전에 달렸고, FreeRTOS가 상한을 보장하지 않는다.

### 3.3 확인 — heap_4에서 `configASSERT`를 끄면

`FreeRTOSConfig.h`의 `F1_NOASSERT` 스위치는 템플릿의 `configASSERT`를 지운다.

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/F1/run.sh ex3 ex3_h4na HEAP=none EXTRA_CFLAGS="-DHN=4 -DF1_NOASSERT -DconfigTOTAL_HEAP_SIZE=65536" > /var/tmp/rtos/F1/ex3_h4na.out 2>&1; head -1 /var/tmp/rtos/F1/ex3_h4na.out; grep -i warn /var/tmp/rtos/F1/ex3_h4na.build'
```

```text
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:8564:17: warning: variable 'pxTCB' set but not used [-Wunused-but-set-variable]
/var/rtoslab/src/FreeRTOS-Kernel/tasks.c:8564:17: warning: variable 'pxTCB' set but not used [-Wunused-but-set-variable]
/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:284:33: warning: unused variable 'pxVectorTable' [-Wunused-variable]
```

같은 Python 계산(op, n, min, p50, p90, p99, max 순):

```text
m 1518 88 115 154 176 198
f 1482 76 94 116 129 134
```

출력에서 볼 것: assert를 끄니 heap_4 malloc p50이 153 → 115 tick(−25%), max가 284 → 198 tick으로 줄었다. heap_5(p50 132)보다도 빠르다. 즉 heap_4와 heap_5의 차이는 알고리즘이 아니라 **검사 비용**이었다. 경고 3줄은 커널 소스(`tasks.c`, `port.c`)에서 assert 안에서만 쓰던 변수가 남아서 나온 것이고, 내 코드에서는 경고가 없다(A4에서도 같은 경고를 봤다). 실무 결론: 양산 빌드에서 assert를 끌지는 신중히 정한다. 끄면 빨라지지만 예제 8의 오염 검출이 사라진다. 시간 예산에는 **검사를 켠 상태의 숫자**를 넣는 것이 안전하다.

### 3.4 예제 4 — 시간 vs 빈 블록 수 N, 그리고 상한 식

무엇을 확인하는 코드인가: 32 B 블록 2N+1개를 할당하고 짝수 번째만 풀어서 **서로 붙지 않은 빈 블록 N개**를 만든다(heap_4도 병합할 수 없다). 그 뒤 2048 B를 요청하면 어떤 구멍에도 안 맞으니 N개를 모두 지나 맨 끝 큰 블록까지 가야 한다(최악 경로). 그 블록을 다시 풀면 주소 순 목록의 맨 끝까지 걸어서 끼운다.

```c
/* ex4 — malloc/free time vs number of free fragments N (worst-case walk) */
#include "FreeRTOS.h"
#include "task.h"
#include "heapsel.h"
#include "board.h"
#define CNT() REG32(0x40028018u)
void vApplicationMallocFailedHook(void) { }
static void *blk[1024];
static void app(void *pv)
{
    (void)pv;
    static const int Ns[] = { 0, 16, 32, 64, 128, 256, 400 };
    for (unsigned k = 0; k < sizeof Ns / sizeof Ns[0]; k++) {
        int N = Ns[k];
        for (int i = 0; i < 2 * N + 1; i++) blk[i] = pvPortMalloc(32);
        for (int i = 0; i < 2 * N; i += 2) vPortFree(blk[i]);     /* N holes, none adjacent */
        size_t nb, lg, tot; fl_stats(&nb, &lg, &tot);
        __asm volatile("cpsid i");
        uint32_t t0 = CNT(); void *big = pvPortMalloc(2048); uint32_t t1 = CNT();  /* must skip all holes */
        uint32_t t2 = CNT(); vPortFree(big); uint32_t t3 = CNT();                  /* insert at the far end */
        __asm volatile("cpsie i");
        board_printf_raw("heap_%d N=%d free_blocks=%u malloc_big=%u free_big=%u ticks\n",
                         HN, N, (unsigned)nb, (unsigned)(t1 - t0), (unsigned)(t3 - t2));
        for (int i = 1; i < 2 * N + 1; i += 2) vPortFree(blk[i]);
        if (N == 0) vPortFree(blk[0]); else vPortFree(blk[2 * N]);
    }
    board_exit(0);
}
int main(void)
{
    xTaskCreateStatic(app, "app", 512, NULL, 1, (static StackType_t[512]){0}, &(static StaticTask_t){0});
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for h in 2 4; do /Users/donh/.rtos_scratch/F1/run.sh ex4 ex4_h$h HEAP=none EXTRA_CFLAGS="-DHN=$h -DconfigTOTAL_HEAP_SIZE=65536"; done'
```

```text
heap_2 N=0 free_blocks=1 malloc_big=101 free_big=76 ticks
heap_2 N=16 free_blocks=18 malloc_big=265 free_big=144 ticks
heap_2 N=32 free_blocks=34 malloc_big=418 free_big=208 ticks
heap_2 N=64 free_blocks=66 malloc_big=725 free_big=336 ticks
heap_2 N=128 free_blocks=129 malloc_big=1330 free_big=588 ticks
heap_2 N=256 free_blocks=257 malloc_big=2560 free_big=1100 ticks
heap_2 N=400 free_blocks=401 malloc_big=3941 free_big=1676 ticks
[board_exit] code=0
[rc=0]
heap_4 N=0 free_blocks=1 malloc_big=119 free_big=100 ticks
heap_4 N=16 free_blocks=17 malloc_big=260 free_big=155 ticks
heap_4 N=32 free_blocks=33 malloc_big=401 free_big=206 ticks
heap_4 N=64 free_blocks=65 malloc_big=683 free_big=309 ticks
heap_4 N=128 free_blocks=129 malloc_big=1245 free_big=514 ticks
heap_4 N=256 free_blocks=257 malloc_big=2372 free_big=923 ticks
heap_4 N=400 free_blocks=401 malloc_big=3640 free_big=1384 ticks
[board_exit] code=0
[rc=0]
```

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="355" y="22" font-size="14" text-anchor="middle">빈 블록 N개를 지나야 할 때 1회 시간 (us)</text>
<line x1="70" y1="290" x2="640" y2="290" stroke="currentColor"/>
<line x1="70" y1="290" x2="70" y2="40" stroke="currentColor"/>
<text x="64" y="294.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="290.0" x2="640" y2="290.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="231.5" font-size="12" text-anchor="end">40</text>
<line x1="70" y1="227.5" x2="640" y2="227.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="169.0" font-size="12" text-anchor="end">80</text>
<line x1="70" y1="165.0" x2="640" y2="165.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="106.5" font-size="12" text-anchor="end">120</text>
<line x1="70" y1="102.5" x2="640" y2="102.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="44.0" font-size="12" text-anchor="end">160</text>
<line x1="70" y1="40.0" x2="640" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="70.0" y="307" font-size="12" text-anchor="middle">0</text>
<text x="212.5" y="307" font-size="12" text-anchor="middle">100</text>
<text x="355.0" y="307" font-size="12" text-anchor="middle">200</text>
<text x="497.5" y="307" font-size="12" text-anchor="middle">300</text>
<text x="640.0" y="307" font-size="12" text-anchor="middle">400</text>
<text x="355" y="326" font-size="12" text-anchor="middle">빈 블록 수 N</text>
<polyline points="70.0,283.7 92.8,273.4 115.6,263.9 161.2,244.7 252.4,206.9 434.8,130.0 640.0,43.7" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="70.0" cy="283.7" r="2.5" fill="#d0564a"/>
<circle cx="92.8" cy="273.4" r="2.5" fill="#d0564a"/>
<circle cx="115.6" cy="263.9" r="2.5" fill="#d0564a"/>
<circle cx="161.2" cy="244.7" r="2.5" fill="#d0564a"/>
<circle cx="252.4" cy="206.9" r="2.5" fill="#d0564a"/>
<circle cx="434.8" cy="130.0" r="2.5" fill="#d0564a"/>
<circle cx="640.0" cy="43.7" r="2.5" fill="#d0564a"/>
<rect x="90" y="48" width="12" height="10" fill="#d0564a"/><text x="108" y="57" font-size="12">heap_2 malloc</text>
<polyline points="70.0,282.6 92.8,273.8 115.6,264.9 161.2,247.3 252.4,212.2 434.8,141.8 640.0,62.5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="282.6" r="2.5" fill="#4a7bd0"/>
<circle cx="92.8" cy="273.8" r="2.5" fill="#4a7bd0"/>
<circle cx="115.6" cy="264.9" r="2.5" fill="#4a7bd0"/>
<circle cx="161.2" cy="247.3" r="2.5" fill="#4a7bd0"/>
<circle cx="252.4" cy="212.2" r="2.5" fill="#4a7bd0"/>
<circle cx="434.8" cy="141.8" r="2.5" fill="#4a7bd0"/>
<circle cx="640.0" cy="62.5" r="2.5" fill="#4a7bd0"/>
<rect x="90" y="66" width="12" height="10" fill="#4a7bd0"/><text x="108" y="75" font-size="12">heap_4 malloc</text>
<polyline points="70.0,285.2 92.8,281.0 115.6,277.0 161.2,269.0 252.4,253.2 434.8,221.2 640.0,185.2" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="285.2" r="2.5" fill="#e08a3c"/>
<circle cx="92.8" cy="281.0" r="2.5" fill="#e08a3c"/>
<circle cx="115.6" cy="277.0" r="2.5" fill="#e08a3c"/>
<circle cx="161.2" cy="269.0" r="2.5" fill="#e08a3c"/>
<circle cx="252.4" cy="253.2" r="2.5" fill="#e08a3c"/>
<circle cx="434.8" cy="221.2" r="2.5" fill="#e08a3c"/>
<circle cx="640.0" cy="185.2" r="2.5" fill="#e08a3c"/>
<rect x="90" y="84" width="12" height="10" fill="#e08a3c"/><text x="108" y="93" font-size="12">heap_2 free</text>
<polyline points="70.0,283.8 92.8,280.3 115.6,277.1 161.2,270.7 252.4,257.9 434.8,232.3 640.0,203.5" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="70.0" cy="283.8" r="2.5" fill="#3f9a6b"/>
<circle cx="92.8" cy="280.3" r="2.5" fill="#3f9a6b"/>
<circle cx="115.6" cy="277.1" r="2.5" fill="#3f9a6b"/>
<circle cx="161.2" cy="270.7" r="2.5" fill="#3f9a6b"/>
<circle cx="252.4" cy="257.9" r="2.5" fill="#3f9a6b"/>
<circle cx="434.8" cy="232.3" r="2.5" fill="#3f9a6b"/>
<circle cx="640.0" cy="203.5" r="2.5" fill="#3f9a6b"/>
<rect x="90" y="102" width="12" height="10" fill="#3f9a6b"/><text x="108" y="111" font-size="12">heap_4 free</text>
</svg>
```
그림 4 — 예제 4. 네 선 모두 직선이다. 기울기(N = 0 → 400): heap_4 malloc 8.80 tick/블록(352 ns), free 3.21 tick/블록(128 ns), heap_2 malloc 9.60, free 4.00 tick/블록.

출력에서 볼 것:

- 완전한 직선이다. heap_4 malloc의 블록당 비용 352 ns ≈ 명령 11개(포인터 따라가기, 크기 비교, 끝 검사, 범위 검사 두 번). free는 128 ns ≈ 명령 4개(주소 비교만).
- N = 400이면 heap_4 malloc 한 번이 **146 us**다. 1 kHz 제어 루프(주기 1000 us)의 15%가 할당 한 번에 사라지고, 그동안 스케줄러가 잠겨서 더 높은 우선순위 태스크도 못 돈다.
- heap_2의 `free_blocks`가 N+2, N+1처럼 1~2개 더 많다. 앞 단계에서 풀었던 블록이 병합되지 않고 남아 있기 때문이다. 실험을 반복할수록 목록이 길어진다 — heap_2의 시간은 **현재 상태가 아니라 역사 전체**에 달렸다.

이제 상한을 식으로 쓴다. 측정값으로 heap_4 malloc은

T_malloc(N) ≈ 119 + 8.8 × N tick = 4.8 us + 0.35 us × N

이다(QEMU icount 기준). 남은 질문은 N의 상한이다. **heap_4에서는 빈 블록 두 개가 주소상 붙어 있을 수 없다**(풀 때 이웃과 병합하므로). 그러니 이웃한 빈 블록 사이에는 반드시 사용 중인 블록이 하나 이상 있다. 동시에 살아 있는 블록이 최대 L개라면

N_free ≤ L + 1

이다. 예제 3은 슬롯이 64개라 L ≤ 64, N_free ≤ 65이고, 상한은 4.8 + 0.35 × 65 ≈ 27.6 us다. 실측 최대는 11.4 us(빈 블록 19개)로 상한 안에 있다. 이 식이 "heap_4의 할당 시간은 결정적인가?"에 대한 답이다. **상한이 있고, 그 상한은 동시에 살아 있는 블록 수로 정해진다.** 시스템 설계 단계에서 L을 셀 수 있으면(태스크 수 × 2 + 큐 수 + 버퍼 수 …) WCET를 쓸 수 있다.

heap_2에는 이 상한이 없다. 붙은 빈 블록도 따로 남으니 N_free는 L이 아니라 **지금까지 쪼개진 횟수**로 자란다. 예제 3에서 L ≤ 64인데 빈 블록이 85개였다. 이것이 heap_2를 새 설계에 쓰지 않는 두 번째 이유다(첫 번째는 §4의 단편화).

### 3.5 함정

- **최악 시간은 "가장 큰 요청"이 만든다.** 큰 버퍼는 작은 구멍을 다 지나야 맞는 블록이 나온다. 주기 태스크가 가끔 큰 버퍼를 할당하면 그 주기만 튄다.
- **할당 중에는 스케줄러가 잠긴다.** 낮은 우선순위 태스크의 큰 할당 하나가 높은 우선순위 태스크의 응답 시간에 그대로 더해진다. 이것은 우선순위 역전과 같은 꼴이다(D3). 인터럽트는 계속 들어오므로 ISR 지연에는 영향이 없다. 다만 그 ISR이 깨운 태스크는 할당이 끝날 때까지 못 돈다.
- **QEMU 숫자는 상대값이다.** 실칩에서는 heap이 외부 SDRAM이나 캐시 없는 영역에 있으면 포인터를 한 번 따라갈 때마다 수십 사이클이 들 수 있다. 블록당 비용은 실칩에서 다시 잰다(DWT CYCCNT, M 모듈). 식의 꼴(a + b × N)은 같다.

---

## 4. 단편화 — 빈 공간은 있는데 실패한다

### 4.1 정의 두 가지

- **외부 단편화**(external fragmentation): 빈 공간이 여러 조각으로 흩어져서, 합은 충분한데 요청 하나를 담을 연속된 조각이 없다. 예제 2의 500 B 실패. 가변 크기 할당기의 문제다.
- **내부 단편화**(internal fragmentation): 블록 안에서 낭비되는 공간. 100 B 요청이 112 B 블록이 되면 12 B가 내부 단편화다. 헤더와 정렬, 그리고 고정 크기 풀에서 "64 B 칸에 40 B 객체"가 만드는 낭비다. 고정 블록 풀은 외부 단편화를 없애는 대신 내부 단편화를 받아들이는 거래다(F2).

측정 지표는 단순하다. **가장 큰 빈 블록 / 전체 빈 공간**. 1에 가까우면 건강하고, 0에 가까우면 위험하다. FreeRTOS heap_4/5의 `vPortGetHeapStats()`가 `xSizeOfLargestFreeBlockInBytes`와 `xAvailableHeapSpaceInBytes`를 같이 준다. heap_2에는 이 API가 없어서, 예제는 빈 목록을 직접 걸었다(`heapsel.h`의 `fl_stats`).

### 4.2 예제 5 — 같은 부하, 전체 빈 공간 vs 가장 큰 블록

무엇을 확인하는 코드인가: 32 KB heap에 슬롯 80개로 "비었으면 16~1039 B 할당, 차 있으면 해제"를 2만 번 돌리고, 100번마다 전체 빈 공간·가장 큰 빈 블록·빈 블록 수를 찍는다. 할당이 실패하면 그 순간의 상태를 찍고, 전체 빈 공간이 요청보다 충분히 크면(외부 단편화로 인한 실패면) 멈춘다. heap_2와 heap_4에 같은 난수 순서를 준다.

```c
/* ex5 — fragmentation trace: total free vs largest free block, heap_2 vs heap_4, same workload */
#include "FreeRTOS.h"
#include "task.h"
#include "heapsel.h"
#include "board.h"
#ifndef SLOTS
#define SLOTS 40
#endif
#define NOPS  20000
static uint32_t rng = 777;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static size_t rsize(void) { return (size_t)16u << (xr() % 7u) | (xr() & 15u); }   /* 16..1039 */
static void *slot[SLOTS];
static volatile int failed;
void vApplicationMallocFailedHook(void) { failed = 1; }
static void app(void *pv)
{
    (void)pv;
    for (int i = 0; i < NOPS; i++) {
        int s = (int)(xr() % SLOTS);
        if (slot[s]) { vPortFree(slot[s]); slot[s] = NULL; }
        else {
            size_t n = rsize(), nb, lg, tot;
            fl_stats(&nb, &lg, &tot);                    /* state just before the request */
            slot[s] = pvPortMalloc(n);
            if (!slot[s]) {
                board_printf_raw("FAIL op=%d want=%u (+hdr) total_free=%u largest=%u blocks=%u\n",
                                 i, (unsigned)n, (unsigned)tot, (unsigned)lg, (unsigned)nb);
                if (tot > n + 16) { board_printf_raw("  -> enough total free, no single block big enough\n"); break; }
            }
        }
        if (i % 100 == 0) {
            size_t nb, lg, tot; fl_stats(&nb, &lg, &tot);
            board_printf_raw("T %d %u %u %u\n", i, (unsigned)tot, (unsigned)lg, (unsigned)nb);
        }
    }
    size_t nb, lg, tot; fl_stats(&nb, &lg, &tot);
    board_printf_raw("heap_%d end: total_free=%u largest=%u blocks=%u\n", HN, (unsigned)tot, (unsigned)lg, (unsigned)nb);
    board_exit(0);
}
int main(void)
{
    xTaskCreateStatic(app, "app", 512, NULL, 1, (static StackType_t[512]){0}, &(static StaticTask_t){0});
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for h in 2 4; do /Users/donh/.rtos_scratch/F1/run.sh ex5 ex5_h$h HEAP=none EXTRA_CFLAGS="-DHN=$h -DSLOTS=80 -DconfigTOTAL_HEAP_SIZE=32768" > /var/tmp/rtos/F1/ex5_h$h.out 2>&1; done'
```

heap_2 (`T 연산번호 전체빈공간 가장큰블록 블록수`, 1000번 간격으로 골랐다):

```text
FAIL op=6988 want=1034 (+hdr) total_free=17992 largest=1032 blocks=72
  -> enough total free, no single block big enough
heap_2 end: total_free=17992 largest=1032 blocks=72
[board_exit] code=0
[rc=0]
T 0 32704 32704 1
T 1000 22032 5112 57
T 2000 23304 5112 52
T 3000 15768 3024 60
T 4000 15944 1040 65
T 5000 19312 1048 80
T 6000 19096 1048 76
T 6900 23440 1048 79
```

heap_4:

```text
heap_4 end: total_free=24360 largest=17616 blocks=13
[board_exit] code=0
[rc=0]
T 0 32704 32704 1
T 4000 16000 13480 12
T 7000 18864 14672 16
T 10000 21288 15776 14
T 15000 22232 17280 14
T 19900 20352 14384 13
```

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<text x="355" y="22" font-size="14" text-anchor="middle">같은 작업 부하: 전체 빈 공간 vs 가장 큰 빈 블록 (32 KB heap)</text>
<line x1="70" y1="300" x2="640" y2="300" stroke="currentColor"/>
<line x1="70" y1="300" x2="70" y2="40" stroke="currentColor"/>
<text x="64" y="304.0" font-size="12" text-anchor="end">0 KB</text>
<text x="64" y="241.0" font-size="12" text-anchor="end">8 KB</text>
<text x="64" y="177.9" font-size="12" text-anchor="end">16 KB</text>
<text x="64" y="114.9" font-size="12" text-anchor="end">24 KB</text>
<text x="64" y="51.9" font-size="12" text-anchor="end">32 KB</text>
<text x="70.0" y="317" font-size="12" text-anchor="middle">0</text>
<text x="212.5" y="317" font-size="12" text-anchor="middle">5000</text>
<text x="355.0" y="317" font-size="12" text-anchor="middle">10000</text>
<text x="497.5" y="317" font-size="12" text-anchor="middle">15000</text>
<text x="640.0" y="317" font-size="12" text-anchor="middle">20000</text>
<text x="355" y="336" font-size="12" text-anchor="middle">연산 번호 (alloc 또는 free)</text>
<polyline points="70.0,42.3 72.8,129.7 75.7,156.9 78.5,129.8 81.4,125.0 84.2,138.9 87.1,157.2 90.0,178.2 92.8,133.9 95.7,167.3 98.5,126.6 101.3,138.4 104.2,118.4 107.0,134.5 109.9,145.6 112.8,126.4 115.6,131.1 118.5,153.6 121.3,175.1 124.2,127.3 127.0,116.6 129.8,128.0 132.7,126.2 135.6,144.4 138.4,120.6 141.2,132.7 144.1,160.3 146.9,177.8 149.8,117.8 152.7,168.8 155.5,175.9 158.3,135.2 161.2,152.5 164.1,99.4 166.9,131.7 169.8,115.1 172.6,174.6 175.4,137.4 178.3,122.8 181.2,191.3 184.0,173.9 186.8,119.9 189.7,122.5 192.6,154.0 195.4,184.3 198.2,145.2 201.1,137.8 203.9,158.1 206.8,157.7 209.7,158.6 212.5,148.5 215.3,173.8 218.2,145.1 221.1,182.4 223.9,143.4 226.8,118.3 229.6,150.0 232.4,107.5 235.3,138.6 238.2,131.9 241.0,149.4 243.8,144.9 246.7,140.8 249.6,143.9 252.4,138.7 255.2,144.9 258.1,128.6 260.9,143.6 263.8,150.7 266.6,115.6 269.5,151.4 272.4,129.6 275.2,138.5 278.1,142.9 280.9,144.4 283.8,114.5 286.6,131.5 289.4,151.1 292.3,130.7 295.1,134.8 298.0,151.6 300.9,156.4 303.7,150.9 306.6,106.7 309.4,95.2 312.2,155.2 315.1,140.3 317.9,127.0 320.8,114.8 323.6,135.4 326.5,152.1 329.4,130.4 332.2,104.6 335.1,127.6 337.9,119.1 340.8,137.6 343.6,124.5 346.4,149.0 349.3,151.1 352.1,190.9 355.0,132.3 357.9,126.5 360.7,119.7 363.6,171.5 366.4,141.7 369.2,131.1 372.1,111.8 374.9,130.8 377.8,158.1 380.6,102.6 383.5,107.6 386.4,153.7 389.2,130.5 392.1,150.7 394.9,160.3 397.8,125.7 400.6,138.1 403.4,109.1 406.3,111.9 409.1,125.2 412.0,125.2 414.9,146.3 417.7,133.0 420.6,149.2 423.4,172.4 426.2,154.5 429.1,166.2 431.9,174.3 434.8,158.7 437.6,131.2 440.5,135.6 443.4,139.1 446.2,146.6 449.1,135.0 451.9,136.6 454.8,117.7 457.6,126.5 460.4,160.5 463.3,140.7 466.1,156.0 469.0,128.6 471.9,136.4 474.7,137.6 477.6,115.8 480.4,128.4 483.2,105.7 486.1,137.3 488.9,133.1 491.8,150.4 494.6,134.5 497.5,124.8 500.4,110.4 503.2,128.7 506.1,130.4 508.9,145.5 511.8,153.9 514.6,148.3 517.5,128.4 520.3,107.1 523.1,119.3 526.0,143.7 528.9,141.2 531.7,168.2 534.5,181.6 537.4,134.2 540.2,89.9 543.1,114.8 546.0,136.0 548.8,109.6 551.6,105.4 554.5,102.5 557.4,147.0 560.2,182.8 563.0,134.7 565.9,132.7 568.8,125.7 571.6,146.1 574.5,168.3 577.3,142.3 580.1,140.4 583.0,134.5 585.9,120.4 588.7,132.3 591.5,138.7 594.4,149.7 597.2,144.1 600.1,124.3 603.0,120.3 605.8,123.8 608.6,138.6 611.5,149.4 614.4,135.0 617.2,159.6 620.0,140.7 622.9,124.5 625.8,132.5 628.6,147.3 631.5,112.5 634.3,156.5 637.1,139.7" fill="none" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="5 3"/>
<polyline points="70.0,42.3 72.8,144.3 75.7,193.7 78.5,202.1 81.4,177.3 84.2,184.8 87.1,193.0 90.0,201.2 92.8,201.2 95.7,225.9 98.5,154.8 101.3,167.2 104.2,183.6 107.0,166.1 109.9,166.1 112.8,149.6 115.6,170.1 118.5,178.3 121.3,211.3 124.2,178.4 127.0,178.5 129.8,146.3 132.7,146.3 135.6,175.1 138.4,151.0 141.2,183.9 144.1,183.9 146.9,204.5 149.8,192.1 152.7,192.1 155.5,200.3 158.3,208.6 161.2,208.6 164.1,172.9 166.9,172.9 169.8,161.0 172.6,206.3 175.4,189.7 178.3,177.3 181.2,210.2 184.0,193.8 186.8,193.8 189.7,193.8 192.6,185.2 195.4,218.1 198.2,164.0 201.1,176.3 203.9,192.8 206.8,184.6 209.7,209.3 212.5,217.5 215.3,193.6 218.2,183.1 221.1,195.7 223.9,204.0 226.8,153.0 229.6,169.5 232.4,194.1 235.3,153.1 238.2,169.5 241.0,176.1 243.8,184.4 246.7,184.4 249.6,192.7 252.4,184.4 255.2,176.1 258.1,164.9 260.9,173.1 263.8,189.6 266.6,134.9 269.5,184.4 272.4,172.2 275.2,168.0 278.1,192.6 280.9,192.6 283.8,168.0 286.6,168.0 289.4,184.2 292.3,184.2 295.1,184.2 298.0,182.4 300.9,194.9 303.7,194.9 306.6,124.0 309.4,159.1 312.2,167.4 315.1,165.1 317.9,165.1 320.8,165.1 323.6,149.7 326.5,172.6 329.4,176.7 332.2,176.7 335.1,176.7 337.9,145.8 340.8,170.5 343.6,162.3 346.4,178.7 349.3,186.9 352.1,215.6 355.0,175.7 357.9,159.3 360.7,155.5 363.6,192.5 366.4,205.0 369.2,176.0 372.1,140.9 374.9,173.9 377.8,190.3 380.6,130.4 383.5,138.7 386.4,188.3 389.2,176.0 392.1,204.9 394.9,184.2 397.8,184.2 400.6,147.2 403.4,121.7 406.3,142.3 409.1,154.7 412.0,167.0 414.9,175.2 417.7,175.3 420.6,167.9 423.4,225.4 426.2,225.4 429.1,196.6 431.9,209.1 434.8,209.1 437.6,164.0 440.5,164.0 443.4,172.2 446.2,196.8 449.1,188.6 451.9,170.3 454.8,162.2 457.6,164.2 460.4,193.1 463.3,188.9 466.1,172.3 469.0,155.7 471.9,155.7 474.7,172.4 477.6,155.3 480.4,155.3 483.2,155.3 486.1,163.9 488.9,180.6 491.8,180.6 494.6,184.7 497.5,163.9 500.4,163.9 503.2,172.1 506.1,163.1 508.9,154.7 511.8,171.4 514.6,200.2 517.5,154.1 520.3,133.3 523.1,170.7 526.0,170.7 528.9,183.0 531.7,191.2 534.5,224.2 537.4,182.6 540.2,139.4 543.1,139.4 546.0,171.4 548.8,159.1 551.6,159.1 554.5,131.2 557.4,188.8 560.2,213.5 563.0,188.8 565.9,162.0 568.8,145.5 571.6,167.7 574.5,188.3 577.3,196.6 580.1,159.5 583.0,186.4 585.9,166.0 588.7,166.0 591.5,160.7 594.4,177.3 597.2,193.9 600.1,185.7 603.0,146.1 605.8,199.7 608.6,160.5 611.5,193.6 614.4,193.6 617.2,197.6 620.0,205.9 622.9,197.6 625.8,166.0 628.6,182.4 631.5,186.7 634.3,186.7 637.1,186.7" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polyline points="70.0,42.3 72.8,129.8 75.7,157.2 78.5,129.9 81.4,125.2 84.2,139.0 87.1,157.4 90.0,178.5 92.8,134.3 95.7,167.3 98.5,126.4 101.3,138.2 104.2,118.3 107.0,134.4 109.9,145.6 112.8,126.3 115.6,131.3 118.5,153.8 121.3,174.9 124.2,126.8 127.0,116.4 129.8,127.4 132.7,126.1 135.6,144.3 138.4,120.4 141.2,132.3 144.1,159.8 146.9,177.2 149.8,117.8 152.7,168.8 155.5,175.8 158.3,135.5 161.2,152.8 164.1,99.6 166.9,131.8 169.8,115.2 172.6,174.8 175.4,137.5 178.3,122.9 181.2,191.8 184.0,174.4 186.8,119.4 189.7,122.3 192.6,154.3 195.4,184.4 198.2,144.9 201.1,137.6 203.9,157.9 206.8,157.4 209.7,158.2 212.5,147.8 215.3,173.7 218.2,144.9 221.1,182.4 223.9,143.2 226.8,118.2 229.6,149.8 232.4,107.5 235.3,138.6 238.2,131.5 241.0,149.5 243.8,145.2 246.7,140.6 249.6,143.8 252.4,138.7 255.2,145.1 258.1,128.1 260.9,143.3 263.8,150.4 266.6,115.3" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5 3"/>
<polyline points="70.0,42.3 72.8,148.5 75.7,202.1 78.5,202.1 81.4,202.1 84.2,218.6 87.1,218.6 90.0,218.6 92.8,218.6 95.7,259.7 98.5,259.7 101.3,259.7 104.2,259.7 107.0,259.7 109.9,259.7 112.8,259.7 115.6,259.7 118.5,259.7 121.3,259.7 124.2,259.7 127.0,259.7 129.8,259.7 132.7,259.7 135.6,259.7 138.4,259.7 141.2,259.7 144.1,259.7 146.9,259.7 149.8,259.7 152.7,259.7 155.5,276.2 158.3,276.2 161.2,276.2 164.1,276.2 166.9,276.2 169.8,276.2 172.6,276.2 175.4,276.2 178.3,276.2 181.2,284.3 184.0,291.8 186.8,291.7 189.7,291.7 192.6,291.7 195.4,291.7 198.2,291.7 201.1,291.7 203.9,291.7 206.8,291.7 209.7,291.7 212.5,291.7 215.3,291.7 218.2,291.7 221.1,291.7 223.9,291.7 226.8,291.7 229.6,291.7 232.4,291.7 235.3,291.7 238.2,291.7 241.0,291.7 243.8,291.7 246.7,291.7 249.6,291.7 252.4,291.7 255.2,291.7 258.1,291.7 260.9,291.7 263.8,291.7 266.6,291.7" fill="none" stroke="#d0564a" stroke-width="2"/>
<line x1="269.2" y1="40" x2="269.2" y2="300" stroke="#d0564a" stroke-dasharray="3 3"/>
<text x="275.2" y="260" font-size="12">heap_2 실패: op 6988</text>
<text x="275.2" y="276" font-size="12">요청 1034 B, 전체 빈 공간 17992 B, 최대 블록 1032 B</text>
<line x1="440" y1="50" x2="466" y2="50" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5 3"/><text x="474" y="54" font-size="12">heap_4 전체 빈 공간</text>
<line x1="440" y1="68" x2="466" y2="68" stroke="#3f9a6b" stroke-width="2"/><text x="474" y="72" font-size="12">heap_4 최대 블록</text>
<line x1="440" y1="86" x2="466" y2="86" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/><text x="474" y="90" font-size="12">heap_2 전체 빈 공간</text>
<line x1="440" y1="104" x2="466" y2="104" stroke="#d0564a" stroke-width="2"/><text x="474" y="108" font-size="12">heap_2 최대 블록</text>
</svg>
```
그림 5 — 예제 5의 100번 간격 기록 전체. 점선 = 전체 빈 공간, 실선 = 가장 큰 빈 블록. 두 할당기의 점선은 거의 같이 움직이는데(같은 부하이므로), heap_2의 실선은 4000번 근처에서 바닥(약 1 KB)에 붙고 6988번째 연산에서 실패한다.

출력에서 볼 것:

- 실패 순간 heap_2에는 **빈 공간이 17,992 B** 있었다. 요청은 1,034 B(블록으로는 1,048 B). 가장 큰 블록은 1,032 B — **16 B가 모자라서** 실패했다. 빈 공간 비율로는 55%가 비어 있는 heap이다. `xPortGetFreeHeapSize()`만 감시했다면 이 실패를 전혀 예측하지 못했을 것이다.
- heap_2의 가장 큰 블록이 계단식으로 줄어든다: 32,704 → 5,112 → 3,024 → 1,040. 한 번 쪼개진 블록은 다시 커지지 않으니 "가장 큰 블록"은 **줄어들기만 한다**(늘어날 수 있는 경우는 처음 큰 블록 하나가 남아 있을 때뿐). 요청 최대 크기(1,048 B 블록) 근처에서 멈춘 것은, 그보다 작은 블록은 그 크기의 요청이 다시 쓰고 그보다 큰 블록은 결국 누군가 쪼개기 때문이다.
- heap_4는 2만 번 동안 가장 큰 블록이 13 KB 아래로 내려간 적이 없고, 빈 블록은 12~16개다. 같은 부하에서 단편화가 **평형**에 머문다. 병합이 쪼개기를 따라잡는다.
- 이 실험은 "heap_4는 단편화가 없다"를 보이지 않는다. 이 부하에서 이 크기일 때 실패하지 않았다는 것만 보인다. 그 차이가 §4.4의 질문이다.

### 4.3 예제 6 — 오래 도는 서버: "며칠 뒤 멈춘다"의 재현

무엇을 확인하는 코드인가: 실제 장비의 꼴로 바꾼다. 텔레메트리 서버라고 하자. 요청 하나마다 진행 중인 패킷 24개 중 하나가 끝나서 풀리고 새 패킷(64~1500 B)이 할당된다. 요청의 1%는 세션 객체(200 B)를 열고, 세션은 2000~10000 요청 동안 산다(오래 사는 작은 블록 = 단편화의 씨앗). 1000 요청마다 로그 flush가 4 KB 버퍼를 잠깐 잡았다 놓는다. 할당이 처음 실패하는 요청 번호를 찾는다. 100 req/s라고 치면 요청 번호 ÷ 100이 초다. `-DCLASSES`는 패킷 크기를 128/512/1500 세 종류로 고정한다.

```c
/* ex6 — long-running "server": variable packets + rare long-lived sessions + a 4 KB flush buffer every 1000 req */
#include "FreeRTOS.h"
#include "task.h"
#include "heapsel.h"
#include "board.h"
#define INFL 24                     /* packets in flight */
#define SESS 16                     /* session objects (long-lived) */
#define NREQ 1000000u
static uint32_t rng = 2026;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void *pkt[INFL]; static void *ses[SESS]; static uint32_t ses_end[SESS];
static volatile int failed;
void vApplicationMallocFailedHook(void) { failed = 1; }
static void app(void *pv)
{
    (void)pv;
    size_t minlg = (size_t)-1;
    for (uint32_t r = 1; r <= NREQ; r++) {
        int k = (int)(xr() % INFL);                       /* one in-flight packet completes */
        vPortFree(pkt[k]);
        #ifdef CLASSES
        static const uint16_t cls[3] = { 128, 512, 1500 };   /* fixed size classes */
        pkt[k] = pvPortMalloc(cls[xr() % 3]);
#else
        pkt[k] = pvPortMalloc(64 + xr() % 1437);          /* 64..1500 B payload */
#endif
        if (xr() % 100 == 0) {                            /* 1 % of requests open a session */
            int s = (int)(xr() % SESS);
            if (!ses[s]) { ses[s] = pvPortMalloc(200); ses_end[s] = r + 2000 + xr() % 8000; }
        }
        for (int s = 0; s < SESS; s++)
            if (ses[s] && r >= ses_end[s]) { vPortFree(ses[s]); ses[s] = NULL; }
        if (r % 1000 == 0) {                              /* periodic log flush needs 4 KB */
            void *f = pvPortMalloc(4096);
            vPortFree(f);
            size_t nb, lg, tot; fl_stats(&nb, &lg, &tot);
            if (lg < minlg) minlg = lg;
            if (!f) {
                board_printf_raw("heap_%d FAIL at request %u (= %u s at 100 req/s): want 4096, total_free=%u largest=%u blocks=%u\n",
                                 HN, (unsigned)r, (unsigned)(r / 100), (unsigned)tot, (unsigned)lg, (unsigned)nb);
                break;
            }
            if (r % 100000 == 0)
                board_printf_raw("heap_%d r=%u total_free=%u largest=%u blocks=%u min_largest=%u\n",
                                 HN, (unsigned)r, (unsigned)tot, (unsigned)lg, (unsigned)nb, (unsigned)minlg);
        }
        if (failed && pkt[k] == NULL) {
            size_t nb, lg, tot; fl_stats(&nb, &lg, &tot);
            board_printf_raw("heap_%d packet FAIL at request %u (= %u s at 100 req/s): total_free=%u largest=%u blocks=%u\n",
                             HN, (unsigned)r, (unsigned)(r / 100), (unsigned)tot, (unsigned)lg, (unsigned)nb);
            break;
        }
    }
#if HN == 4
    board_printf_raw("heap_4 minimum ever free = %u of %u\n", (unsigned)xPortGetMinimumEverFreeHeapSize(), configTOTAL_HEAP_SIZE);
#endif
    board_exit(0);
}
int main(void)
{
    xTaskCreateStatic(app, "app", 512, NULL, 1, (static StackType_t[512]){0}, &(static StaticTask_t){0});
    vTaskStartScheduler();
    return 0;
}
```

heap_2, 48 KB:

```sh
limactl shell elinux -- bash -lc 'TO=300 /Users/donh/.rtos_scratch/F1/run.sh ex6 ex6_h2 HEAP=none EXTRA_CFLAGS="-DHN=2 -DconfigTOTAL_HEAP_SIZE=49152"'
```

```text
heap_2 packet FAIL at request 249 (= 2 s at 100 req/s): total_free=31176 largest=1064 blocks=146
[board_exit] code=0
[rc=0]
```

heap을 늘리면 얼마나 버티나:

```sh
limactl shell elinux -- bash -lc 'for hs in 65536 98304 131072; do echo "heap=$hs"; TO=300 /Users/donh/.rtos_scratch/F1/run.sh ex6 ex6_h2 HEAP=none EXTRA_CFLAGS="-DHN=2 -DconfigTOTAL_HEAP_SIZE=$hs" | grep -v "r=.00000"; done'
```

```text
heap=65536
heap_2 packet FAIL at request 380 (= 3 s at 100 req/s): total_free=45264 largest=1312 blocks=200
[board_exit] code=0
[rc=0]
heap=98304
heap_2 FAIL at request 1000 (= 10 s at 100 req/s): want 4096, total_free=74336 largest=4040 blocks=391
[board_exit] code=0
[rc=0]
heap=131072
heap_2 FAIL at request 3000 (= 30 s at 100 req/s): want 4096, total_free=110064 largest=1512 blocks=773
[board_exit] code=0
[rc=0]
```

256 KB와 크기 3종, 그리고 heap_4:

```sh
limactl shell elinux -- bash -lc 'echo "heap=262144 random"; TO=300 /Users/donh/.rtos_scratch/F1/run.sh ex6 ex6_h2 HEAP=none EXTRA_CFLAGS="-DHN=2 -DconfigTOTAL_HEAP_SIZE=262144" | grep -v "r=.00000"; echo "heap=49152 CLASSES"; TO=300 /Users/donh/.rtos_scratch/F1/run.sh ex6 ex6_h2c HEAP=none EXTRA_CFLAGS="-DHN=2 -DCLASSES -DconfigTOTAL_HEAP_SIZE=49152"; echo "heap_4 CLASSES"; TO=300 /Users/donh/.rtos_scratch/F1/run.sh ex6 ex6_h4c HEAP=none EXTRA_CFLAGS="-DHN=4 -DCLASSES -DconfigTOTAL_HEAP_SIZE=49152" | tail -4'
```

```text
heap=262144 random
heap_2 FAIL at request 74000 (= 740 s at 100 req/s): want 4096, total_free=240648 largest=2600 blocks=2450
[board_exit] code=0
[rc=0]
heap=49152 CLASSES
heap_2 r=100000 total_free=23192 largest=4104 blocks=55 min_largest=4104
heap_2 FAIL at request 174000 (= 1740 s at 100 req/s): want 4096, total_free=26152 largest=2592 blocks=59
[board_exit] code=0
[rc=0]
heap_4 CLASSES
heap_4 r=1000000 total_free=29904 largest=22592 blocks=16 min_largest=14448
heap_4 minimum ever free = 16072 of 49152
[board_exit] code=0
[rc=0]
```

heap_4, 48 KB, 무작위 크기(100만 요청):

```sh
limactl shell elinux -- bash -lc 'TO=300 /Users/donh/.rtos_scratch/F1/run.sh ex6 ex6_h4 HEAP=none EXTRA_CFLAGS="-DHN=4 -DconfigTOTAL_HEAP_SIZE=49152"'
```

```text
heap_4 r=100000 total_free=26832 largest=16760 blocks=21 min_largest=14592
heap_4 r=200000 total_free=29544 largest=18856 blocks=16 min_largest=13208
heap_4 r=300000 total_free=31432 largest=25912 blocks=17 min_largest=12656
heap_4 r=400000 total_free=26184 largest=16720 blocks=21 min_largest=12656
heap_4 r=500000 total_free=30104 largest=21112 blocks=16 min_largest=12656
heap_4 r=600000 total_free=27656 largest=19864 blocks=22 min_largest=12656
heap_4 r=700000 total_free=23608 largest=16624 blocks=17 min_largest=12656
heap_4 r=800000 total_free=26528 largest=22088 blocks=16 min_largest=11088
heap_4 r=900000 total_free=28904 largest=23184 blocks=14 min_largest=11088
heap_4 r=1000000 total_free=29448 largest=26144 blocks=14 min_largest=11088
heap_4 minimum ever free = 16008 of 49152
[board_exit] code=0
[rc=0]
```

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle">서버 시나리오: 처음 할당이 실패할 때까지 요청 수 (로그 눈금)</text>
<text x="222" y="51" font-size="12" text-anchor="end">heap_2, 48 KB, 무작위 크기</text>
<rect x="230" y="40" width="36.6" height="16" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="272.6" y="53" font-size="12">249</text>
<text x="222" y="79" font-size="12" text-anchor="end">heap_2, 64 KB, 무작위 크기</text>
<rect x="230" y="68" width="53.6" height="16" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="289.6" y="81" font-size="12">380</text>
<text x="222" y="107" font-size="12" text-anchor="end">heap_2, 96 KB, 무작위 크기</text>
<rect x="230" y="96" width="92.5" height="16" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="328.5" y="109" font-size="12">1,000</text>
<text x="222" y="135" font-size="12" text-anchor="end">heap_2, 128 KB, 무작위 크기</text>
<rect x="230" y="124" width="136.6" height="16" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="372.6" y="137" font-size="12">3,000</text>
<text x="222" y="163" font-size="12" text-anchor="end">heap_2, 256 KB, 무작위 크기</text>
<rect x="230" y="152" width="265.4" height="16" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="501.4" y="165" font-size="12">74,000</text>
<text x="222" y="191" font-size="12" text-anchor="end">heap_2, 48 KB, 크기 3종</text>
<rect x="230" y="180" width="299.8" height="16" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="535.8" y="193" font-size="12">174,000</text>
<text x="222" y="219" font-size="12" text-anchor="end">heap_4, 48 KB, 무작위 크기</text>
<rect x="230" y="208" width="370.0" height="16" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="594.0" y="221" font-size="12" text-anchor="end">1,000,000 (실패 없음)</text>
<text x="222" y="247" font-size="12" text-anchor="end">heap_4, 48 KB, 크기 3종</text>
<rect x="230" y="236" width="370.0" height="16" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="594.0" y="249" font-size="12" text-anchor="end">1,000,000 (실패 없음)</text>
<line x1="230" y1="264" x2="600" y2="264" stroke="currentColor"/>
<text x="230.0" y="280" font-size="12" text-anchor="middle">10^2</text>
<text x="322.5" y="280" font-size="12" text-anchor="middle">10^3</text>
<text x="415.0" y="280" font-size="12" text-anchor="middle">10^4</text>
<text x="507.5" y="280" font-size="12" text-anchor="middle">10^5</text>
<text x="600.0" y="280" font-size="12" text-anchor="middle">10^6</text>
<text x="340" y="300" font-size="12" text-anchor="middle">100 req/s라면 10^3 = 10초, 10^5 = 17분, 10^6 = 2.8시간</text>
</svg>
```
그림 6 — 예제 6을 한 그림에. heap_2는 heap을 5배(48 → 256 KB)로 늘려도 실패를 12분 늦출 뿐이다. 크기를 3종으로 고정하면 29분까지 버티지만 결국 실패한다. heap_4는 두 부하 모두 100만 요청(100 req/s로 2.8시간) 동안 실패가 없었다.

출력에서 볼 것:

- **heap_2는 48 KB에서 2.5초 만에 패킷 할당이 실패했다.** 빈 공간은 31 KB(65%)였다. 1,500 B 패킷이 들어갈 블록이 없었다(가장 큰 블록 1,064 B). 무작위 크기와 병합 없음의 조합에서는 큰 블록이 금방 사라진다.
- **heap을 늘려도 해결이 아니다.** 64 → 96 → 128 → 256 KB에서 실패 시점은 3.8 s → 10 s → 30 s → 740 s다. 256 KB일 때 빈 공간 240 KB(92%)가 있는데 4 KB를 못 잡았다. 빈 블록이 2,450개로 쪼개져 있었다. 개발 중 시험(몇 분)은 통과하고 필드에서 몇십 분 뒤 멈추는 구조다. "heap을 넉넉히 주면 된다"는 직관이 왜 틀렸는지 보여 준다.
- 크기를 3종으로 고정하면 heap_2가 훨씬 오래 버틴다(17만 요청, 29분). 같은 크기의 블록이 그대로 재사용되기 때문이다. 그래도 실패했다. 세션 객체(200 B)는 크기가 따로라서, 맞는 블록이 없을 때 패킷 블록이나 4 KB flush 블록을 쪼갰고, 그 조각은 다시 합쳐지지 않는다. **"크기가 고정이면 heap_2도 된다"는 크기가 정말로 하나뿐일 때만 맞다.** 그럴 때 맞는 도구는 고정 블록 풀(F2)이다.
- heap_4는 100만 요청 동안 가장 큰 블록의 최저값이 11 KB였다(4 KB 요청보다 2.7배 크다). 빈 블록 수는 14~22개에서 오르내렸다. 최소 여유(`xPortGetMinimumEverFreeHeapSize`)는 16 KB였다. 이 두 숫자가 "이 부하에서 이 heap 크기면 여유가 얼마"를 말한다.

### 4.4 "단편화 때문에 실패하지 않는다"를 어떻게 증명하나

면접 질문 "How do you prove no fragmentation failure?"의 답은 강한 것부터 넷이다.

1. **구조로 없앤다 — 초기화 뒤 free가 없다.** 정적 할당, 또는 heap_1, 또는 초기화 후 잠금(예제 1). 할당 순서가 매 부팅 같으므로 첫 부팅에서 성공하면 영원히 성공한다. 증명이 필요 없는 유일한 방법이고, 인증 프로젝트가 이것을 고르는 이유다.
2. **구조로 없앤다 — 고정 크기 블록 풀.** 크기가 하나뿐인 풀에서는 외부 단편화가 정의상 없다(어떤 빈 칸이든 어떤 요청에 맞는다). 풀마다 "최대 동시 사용 개수"만 세면 된다. 크기 종류가 몇 개면 풀을 몇 개 둔다. Don의 SSD 펌웨어가 쓴 방식이다(F2).
3. **분석으로 상한을 둔다.** 가변 크기 할당기에 대해서는 이론상 최악 경계가 있다. Robson(1974)은 최대 동시 사용량이 M이고 블록 크기가 최대 n배까지 다를 때, 어떤 할당기든 실패하지 않으려면 M의 약 log₂(n)배 수준의 메모리가 필요할 수 있다는 것을 보였다. 실무에서 이 경계는 너무 커서(1500/64 ≈ 23배 → log₂ ≈ 4.5배) heap 크기를 정하는 데 쓰이기보다 "가변 크기는 최악을 보장하기 비싸다"는 근거로 쓰인다.
4. **시험과 감시로 근거를 쌓는다 — heap_4.** 최악 부하(최대 동시 사용, 최대 요청 크기, 가장 긴 수명 섞기)로 오래 돌리는 스트레스 시험(예제 6처럼 실제 시간보다 빠르게), 그리고 필드에서 **가장 큰 빈 블록과 최소 여유를 텔레메트리로 보낸다.** 기준은 "가장 큰 빈 블록의 최저값 ≥ 가장 큰 요청 × 여유율". 이것은 증명이 아니라 증거다. 인증 문서에는 deviation과 함께 이 시험 결과를 붙인다.

1과 2는 "실패하지 않는다"고 말할 수 있고, 3과 4는 "실패하지 않을 것이라는 근거가 이렇다"고 말할 수 있다. 면접에서는 이 구별을 말하는 것이 핵심이다.

---
## 5. 정적 할당 빌드 — 메모리 예산을 링크 시점에 확정한다

### 5.1 FreeRTOS의 두 스위치

| 설정 | 기본값 (`FreeRTOS.h`) | 1이면 |
|---|---|---|
| `configSUPPORT_DYNAMIC_ALLOCATION` | 1 | `xTaskCreate`, `xQueueCreate`, `xSemaphoreCreateMutex`, `xTimerCreate`, `xEventGroupCreate`, `xStreamBufferCreate` … 이 생긴다. heap 파일이 필요하다 |
| `configSUPPORT_STATIC_ALLOCATION` | 0 | `xTaskCreateStatic`, `xQueueCreateStatic`, `xSemaphoreCreateMutexStatic`, `xTimerCreateStatic`, `xEventGroupCreateStatic`, `xStreamBufferCreateStatic` … 이 생긴다. idle·timer 태스크 메모리를 `vApplicationGetIdleTaskMemory()`/`vApplicationGetTimerTaskMemory()`로 받는다 |
| `configKERNEL_PROVIDED_STATIC_MEMORY` | 0 | 위 두 콜백을 커널이 정적 배열로 직접 제공한다(MPU 포트에서는 금지 — `tasks.c` 48줄의 `#error`) |

둘 다 0이면 `#error`다(`FreeRTOS.h` 2893줄). 둘 다 1이면 섞어 쓸 수 있고, 커널은 TCB의 `ucStaticallyAllocated`(A4에서 본 필드)에 "스택과 TCB 둘 다 동적 / 스택만 정적 / 둘 다 정적"을 기록해 뒀다가 `prvDeleteTCB()`에서 **자기가 할당한 것만** `vPortFree`한다(`tasks.c`의 `tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB` = 0, `tskSTATICALLY_ALLOCATED_STACK_ONLY` = 1, `tskSTATICALLY_ALLOCATED_STACK_AND_TCB` = 2). 정적으로 만든 태스크를 `vTaskDelete`해도 메모리는 그대로 내 배열이다. 그 배열을 다시 쓰려면 태스크가 정말 지워진 뒤(idle이 정리한 뒤, C9)여야 한다.

정적 API의 버퍼 타입(`StaticTask_t`, `StaticQueue_t`, …)은 커널 내부 구조체와 **크기만 같은** 공개 구조체다. 커널은 생성할 때 `configASSERT( xSize == sizeof( TCB_t ) )`처럼 크기가 맞는지 확인한다(A4 §2.1).

### 5.2 예제 7 — 같은 앱, 동적 vs 정적 전용

무엇을 확인하는 코드인가: 태스크 3개(256/512/384 words), 큐 2개, 뮤텍스, 타이머, 이벤트 그룹으로 된 같은 앱을 (1) heap_4 + `configTOTAL_HEAP_SIZE` 16 KB, (2) `configSUPPORT_DYNAMIC_ALLOCATION 0` + heap 파일 없음으로 빌드해서 크기와 심볼을 비교한다.

```c
/* ex7 — the same app, dynamic (heap_4) vs static-only (configSUPPORT_DYNAMIC_ALLOCATION=0) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "event_groups.h"
#include "board.h"
#ifndef CTL_WORDS
#define CTL_WORDS 512
#endif
static void tmr_cb(TimerHandle_t t) { (void)t; }
static void worker(void *pv) { (void)pv; for (;;) vTaskDelay(10); }
static void report(void *pv)
{
    (void)pv; vTaskDelay(20);
#if configSUPPORT_DYNAMIC_ALLOCATION
    board_printf_raw("dynamic: heap free now %u, min ever %u of %u\n", (unsigned)xPortGetFreeHeapSize(),
                     (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned)configTOTAL_HEAP_SIZE);
#else
    board_printf_raw("static: no heap at all, every object has a symbol\n");
#endif
    board_exit(0);
}
#if configSUPPORT_DYNAMIC_ALLOCATION == 0
static StackType_t imu_stk[256], ctl_stk[CTL_WORDS], log_stk[384];
static StaticTask_t imu_tcb, ctl_tcb, log_tcb;
static uint8_t q1_buf[16 * sizeof(uint32_t)], q2_buf[4 * 64];
static StaticQueue_t q1_s, q2_s;
static StaticSemaphore_t mtx_s;
static StaticTimer_t tmr_s;
static StaticEventGroup_t eg_s;
#endif
int main(void)
{
#if configSUPPORT_DYNAMIC_ALLOCATION
    BaseType_t ok = xTaskCreate(worker, "imu", 256, NULL, 3, NULL);
    ok &= xTaskCreate(worker, "ctl", CTL_WORDS, NULL, 2, NULL);
    ok &= xTaskCreate(report, "log", 384, NULL, 1, NULL);
    ok &= xQueueCreate(16, sizeof(uint32_t)) != NULL;
    ok &= xQueueCreate(4, 64) != NULL;
    ok &= xSemaphoreCreateMutex() != NULL;
    ok &= xTimerCreate("hb", 100, pdTRUE, NULL, tmr_cb) != NULL;
    ok &= xEventGroupCreate() != NULL;
    if (!ok) { board_printf_raw("dynamic: object creation FAILED at run time\n"); board_exit(1); }
#else
    xTaskCreateStatic(worker, "imu", 256, NULL, 3, imu_stk, &imu_tcb);
    xTaskCreateStatic(worker, "ctl", CTL_WORDS, NULL, 2, ctl_stk, &ctl_tcb);
    xTaskCreateStatic(report, "log", 384, NULL, 1, log_stk, &log_tcb);
    xQueueCreateStatic(16, sizeof(uint32_t), q1_buf, &q1_s);
    xQueueCreateStatic(4, 64, q2_buf, &q2_s);
    xSemaphoreCreateMutexStatic(&mtx_s);
    xTimerCreateStatic("hb", 100, pdTRUE, NULL, tmr_cb, &tmr_s);
    xEventGroupCreateStatic(&eg_s);
#endif
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F1; $S/run.sh ex7 ex7_dyn HEAP=heap_4 EXTRA_CFLAGS="-DconfigTOTAL_HEAP_SIZE=16384"; tail -2 /var/tmp/rtos/F1/ex7_dyn.build; $S/run.sh ex7 ex7_sta HEAP=none EXTRA_CFLAGS="-DF1_STATIC_ONLY"; tail -2 /var/tmp/rtos/F1/ex7_sta.build'
```

```text
dynamic: heap free now 10720, min ever 10720 of 16384
[board_exit] code=0
[rc=0]
   text	   data	    bss	    dec	    hex	filename
  12776	    100	  20712	  33588	   8334	/var/tmp/rtos/F1/ex7_dyn/app.elf
static: no heap at all, every object has a symbol
[board_exit] code=0
[rc=0]
   text	   data	    bss	    dec	    hex	filename
  11656	    100	   9840	  21596	   545c	/var/tmp/rtos/F1/ex7_sta/app.elf
```

RAM을 누가 차지하나 (`nm -S --size-sort`, `.bss`/`.data` 심볼의 큰 쪽 14줄씩):

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F1; for b in ex7_dyn ex7_sta; do echo "== $b"; arm-none-eabi-nm -S --size-sort $b/app.elf | grep -i " [bBdD] " | tail -14; done'
```

```text
== ex7_dyn
20000e70 00000040 B xQueueRegistry
2000000c 0000004c D _impure_data
20000eb0 00000054 b xStaticTimerQueue.0
20000cc8 00000060 b xIdleTaskTCB.3
20000868 00000060 b xTimerTaskTCB.1
20000dcc 000000a0 b pxReadyTasksLists
20000f04 000000a0 b ucStaticTimerQueueStorage.1
2000500c 00000138 B __sf
200008c8 00000400 b uxIdleTaskStack.2
20000068 00000800 b uxTimerTaskStack.0
20000ffc 00004000 b ucHeap
20005150 003faeb0 B end
000031e8 1fffce18 d __preinit_array_start
20400000 ffc05150 B _estack
== ex7_sta
200002ec 00000060 b log_tcb
2000226c 00000060 b xIdleTaskTCB.3
20001e0c 00000060 b xTimerTaskTCB.1
20002370 000000a0 b pxReadyTasksLists
200024ac 000000a0 b ucStaticTimerQueueStorage.1
200001ac 00000100 b q2_buf
20002590 00000138 B __sf
2000120c 00000400 b imu_stk
20001e6c 00000400 b uxIdleTaskStack.2
2000040c 00000600 b log_stk
20000a0c 00000800 b ctl_stk
2000160c 00000800 b uxTimerTaskStack.0
00002d88 1fffd278 d __preinit_array_start
20400000 ffc026d8 B _estack
```

앱 객체 13개의 합과 섹션 크기:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F1; arm-none-eabi-nm -S ex7_sta/app.elf | grep -E " (imu_stk|ctl_stk|log_stk|imu_tcb|ctl_tcb|log_tcb|q1_buf|q2_buf|q1_s|q2_s|mtx_s|tmr_s|eg_s)$" | awk "{s+=strtonum(\"0x\"\$2); n++; print} END {print n, \"symbols, sum =\", s}"; arm-none-eabi-size -A ex7_sta/app.elf | grep -E "^\.(bss|data)"; arm-none-eabi-size -A ex7_dyn/app.elf | grep -E "^\.(bss|data)"'
```

```text
20000a0c 00000800 b ctl_stk
2000034c 00000060 b ctl_tcb
20000068 0000001c b eg_s
2000120c 00000400 b imu_stk
200003ac 00000060 b imu_tcb
2000040c 00000600 b log_stk
200002ec 00000060 b log_tcb
200000b0 00000054 b mtx_s
200002ac 00000040 b q1_buf
20000158 00000054 b q1_s
200001ac 00000100 b q2_buf
20000104 00000054 b q2_s
20000084 0000002c b tmr_s
13 symbols, sum = 5540
.data                100   536870912
.bss                9840   536871016
.data                100   536870912
.bss               20708   536871016
```

출력에서 볼 것:

- **정적 빌드의 RAM은 쓴 만큼이다.** 앱 객체 13개 = 5,540 B, 나머지 4,300 B는 커널·libc 몫이다. 동적 빌드는 `ucHeap` 16 KB를 통째로 예약했고, 실제로 쓴 것은 16,384 − 10,720 = 5,664 B다. 동적 빌드가 124 B 더 쓴 것은 블록 헤더(8 B × 11블록)와 정렬, 끝 표지다. 나머지 10.7 KB는 **아무도 안 쓰는 예약**이다. 동적 빌드에서 heap 크기를 정하는 일은 "얼마나 쓸지 추측하기"이고, 정적 빌드에서는 그 질문 자체가 없다.
- **정적 빌드의 map 파일은 "무엇이 얼마나"를 이름으로 보여 준다.** `ctl_stk 0x800`(512 words = 2 KB)처럼. 동적 빌드에서는 그 정보가 `ucHeap` 16 KB 한 줄 안에 숨어 있고, 실행해 봐야 안다.
- 코드는 정적 빌드가 1,120 B 작다(heap_4 코드 676 B, 동적 생성 경로).
- 이벤트 그룹의 정적 버퍼는 28 B(`eg_s 0x1c`)인데 예제 1의 동적 할당은 32 B였다. `event_groups.c`의 `EventGroup_t`에는 `ucStaticallyAllocated` 필드가 **정적과 동적을 둘 다 켰을 때만** 있다. 정적 전용 빌드에서는 그 1바이트(+정렬 3바이트)가 빠진다. 같은 코드라도 설정에 따라 객체 크기가 바뀌므로 예산은 그 빌드의 `sizeof(StaticEventGroup_t)`로 잡는다.
- `nm`의 마지막 몇 줄(`end`, `__preinit_array_start`, `_estack`)의 엉터리 크기는 링커 심볼에 다음 심볼까지의 거리를 붙인 것이다(B11). `__sf`(312 B)와 `_impure_data`는 newlib-nano의 stdio로, 두 빌드 모두에 있다 — `board.c`의 `setvbuf` 때문이다(B11). 정적 빌드에서도 libc malloc 코드(`_malloc_r`)는 링크되어 있다. "heap 파일이 없으니 실행 중 할당이 없다"는 FreeRTOS 객체에 대해서만 맞다. libc 쪽은 `nm`으로 따로 확인한다(F7).

### 5.3 링크 실패는 기능이다

같은 앱을 RAM 64 KB 칩(작은 STM32급)이라고 치자. 랩 링커 스크립트를 복사해서 `RAM LENGTH = 64K`로 바꿨다(`ram64k.ld`). 그리고 ctl 태스크 스택을 실수로 16384 words(64 KB)로 적었다고 하자.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F1; echo "--- static, 64K RAM, ctl 4096 words"; NORUN=1 $S/run.sh ex7 ex7_sta64 HEAP=none LDS=$S/ram64k.ld EXTRA_CFLAGS="-DF1_STATIC_ONLY -DCTL_WORDS=4096"; echo "--- static, 64K RAM, ctl 16384 words"; $S/run.sh ex7 ex7_sta64b HEAP=none LDS=$S/ram64k.ld EXTRA_CFLAGS="-DF1_STATIC_ONLY -DCTL_WORDS=16384" 2>&1 | grep -v "^collect2\|^make"; echo "--- dynamic, 64K RAM, heap 32K, ctl 16384 words"; $S/run.sh ex7 ex7_dyn64 HEAP=heap_4 LDS=$S/ram64k.ld EXTRA_CFLAGS="-DconfigTOTAL_HEAP_SIZE=32768 -DCTL_WORDS=16384"; tail -1 /var/tmp/rtos/F1/ex7_dyn64.build'
```

```text
--- static, 64K RAM, ctl 4096 words
   text	   data	    bss	    dec	    hex	filename
  11656	    100	  24176	  35932	   8c5c	/var/tmp/rtos/F1/ex7_sta64/app.elf
--- static, 64K RAM, ctl 16384 words
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: /var/tmp/rtos/F1/ex7_sta64b/app.elf section `.bss' will not fit in region `RAM'
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: RAM overflow
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: region `RAM' overflowed by 7896 bytes
--- dynamic, 64K RAM, heap 32K, ctl 16384 words

[MallocFailedHook] heap exhausted
[board_exit] code=252
[rc=1]
  12776	    100	  37096	  49972	   c334	/var/tmp/rtos/F1/ex7_dyn64/app.elf
```

출력에서 볼 것:

- 정적 빌드는 **빌드가 실패했다.** 링커가 "RAM을 7,896 B 넘쳤다"고 정확한 숫자까지 말한다(`RAM overflow`는 랩 링커 스크립트의 `ASSERT` 메시지, B5). 펌웨어가 장비에 올라가기 전에, CI에서 잡힌다.
- 동적 빌드는 **빌드가 성공했다**(`.bss` 37 KB, 64 KB 안에 들어간다). 실패는 실행 중에 `xTaskCreate`가 64 KB 스택을 32 KB heap에서 못 받으면서 났다. 이 예제는 부팅 직후라 바로 보였지만, 그 할당이 "특정 모드에 들어갈 때만" 일어나는 코드였다면 필드에서야 드러난다.
- 이것이 인증 프로젝트가 정적 할당을 고르는 가장 실용적인 이유다. **메모리 예산 검사를 사람과 시험이 아니라 링커가 한다.** Don이 SSD 펌웨어에서 "SRAM이 넘치면 링크가 깨진다"를 겪어 봤다면 같은 장치다.

정적 전용 빌드에서는 실수로 동적 API를 부르면 더 일찍 걸린다. `xQueueCreate`는 `configSUPPORT_DYNAMIC_ALLOCATION 1`일 때만 매크로로 정의되므로, 0이면 그 이름이 없어서 컴파일 단계에서 implicit declaration 에러가 난다(`-Werror=implicit-function-declaration`이 기본인 GCC 14 이상). 커널이 아닌 코드가 `pvPortMalloc`을 직접 부르면 heap 파일이 없으니 링크 에러다.

### 5.4 정적 할당의 비용

- **API가 길다.** 객체마다 버퍼 두 개(저장 공간 + 제어 블록)를 선언해서 넘긴다. 매크로로 감싸는 것이 보통이다(Zephyr는 처음부터 그렇게 했다, §7).
- **수명이 고정이다.** "가끔만 쓰는 큰 버퍼"도 항상 RAM을 차지한다. 두 모드가 절대 동시에 안 쓰는 버퍼라면 `union`으로 겹치거나, 그 모드 전용 풀을 둔다. 이 판단을 링크 시점에 사람이 해야 한다.
- **크기는 여전히 추측이다.** 정적 할당은 "총량이 들어가는가"를 보장할 뿐, 스택 512 words가 충분한지는 말해 주지 않는다. 그건 F3(스택 산정)의 일이다.

---

## 6. heap을 쓸 때의 보호 장치와 진단

### 6.1 진단 API

| API / 설정 | heap | 무엇을 주나 | 언제 쓰나 |
|---|---|---|---|
| `xPortGetFreeHeapSize()` | 1, 2, 4, 5 | 지금 빈 바이트 합 | 기본 로그. **단편화는 안 보인다** (예제 5: 55% 비어 있는데 실패) |
| `xPortGetMinimumEverFreeHeapSize()` | 4, 5 | 부팅 뒤 빈 바이트의 최저값 | heap 크기 정하기: `configTOTAL_HEAP_SIZE` = 최대 사용량 + 여유 |
| `xPortResetHeapMinimumEverFreeHeapSize()` | 4, 5 | 위 최저값을 지금 값으로 리셋 (V11.2.0) | 초기화를 뺀 "운전 중" 최저값만 보고 싶을 때 |
| `vPortGetHeapStats(&s)` | 4, 5 | 가장 큰·작은 빈 블록, 빈 블록 수, 성공한 alloc·free 수, 최저값 | **단편화 감시**: `xSizeOfLargestFreeBlockInBytes`를 텔레메트리로 |
| `vApplicationMallocFailedHook()` | 모두 (`configUSE_MALLOC_FAILED_HOOK 1`) | 할당 실패 시 호출 | 개발: 멈추고 기록. 양산: 기록 후 안전 상태 |
| `traceMALLOC` / `traceFREE` | 모두 | 할당·해제마다 호출되는 매크로 | 누수 추적, 잠금 패턴(예제 1) |

`vPortGetHeapStats()`는 빈 목록 전체를 걷는다(스케줄러 잠금 상태로). 빈 블록이 많으면 그만큼 오래 잠근다. 1초에 한 번 텔레메트리 정도면 괜찮지만 제어 루프 안에서 부르지 않는다.

`vApplicationMallocFailedHook`에서 할 일은 정해져 있다. 개발 빌드에서는 `configASSERT(0)`처럼 바로 멈춰서 콜 스택을 본다(누가 실패했나). 양산 빌드에서는 실패한 크기와 그 순간의 `vPortGetHeapStats` 결과를 noinit 기록에 남기고(E8의 리셋 기록 패턴) 시스템 정책에 따른다. 이 훅은 `pvPortMalloc` 안, 스케줄러 재개 **뒤**에 불리므로 여기서 다른 RTOS API를 불러도 된다. 다만 **훅에서 돌아오면 호출자는 NULL을 받는다.** 모든 생성 API의 반환값을 검사하는 코드가 기본이다.

### 6.2 `configHEAP_CLEAR_MEMORY_ON_FREE`

1이면 `vPortFree`가 블록의 사용자 영역을 0으로 지운다(heap_2/4/5, V10.5.0(2022-09)에서 추가 — History.txt의 "Heap improvements" 항목). 목적은 두 가지다. 보안(풀린 메모리에 키·개인 정보가 남지 않게)과 디버깅(use-after-free가 0을 읽어 빨리 드러나게). 비용은 해제하는 바이트 수에 비례한다. 1 KB 버퍼 해제가 memset 1 KB만큼 길어지고, 그 memset은 **스케줄러를 잠그기 전**에 한다(소스상 `heapFREE_BLOCK` 직후, `vTaskSuspendAll` 앞). 그래서 다른 태스크를 막지는 않지만 호출한 태스크의 WCET에는 들어간다.

B5에서 heap을 `.noinit`으로 옮기면 웜 리셋 뒤 heap에 이전 값이 남는다고 했다. 이 설정은 그것과 별개다(해제 시점에 지울 뿐, 부팅 때 지우지 않는다). "받은 메모리가 0"을 원하면 `pvPortCalloc`을 쓴다(heap_2/4/5에 있다).

### 6.3 `configENABLE_HEAP_PROTECTOR` — 포인터 난독화 + 범위 검사

V11.0.0에서 heap_4/5에 들어왔다(History.txt: "Add bounds checking and obfuscation to internal heap block pointers"). 이름은 protector지만 **카나리 바이트를 블록 끝에 두는 방식이 아니다.** 소스를 보면 이렇게 동작한다.

- `vApplicationGetRandomHeapCanary(&xHeapCanary)`가 heap 초기화 때 한 번 불려 난수를 받는다(앱이 제공해야 한다, 없으면 링크 에러).
- 헤더의 `pxNextFreeBlock`을 저장할 때 **canary와 XOR해서** 저장하고, 읽을 때 다시 XOR한다(`heapPROTECT_BLOCK_POINTER`). 할당된 블록의 `pxNextFreeBlock`은 NULL이 아니라 `NULL ^ canary` = canary 값이 된다(V11.2.0부터: "Add pointer protection to the pxNextFreeBlock member of the allocated block's metadata").
- 목록을 걸을 때마다 포인터가 heap 범위 안인지 `heapVALIDATE_BLOCK_POINTER`로 확인한다(heap_4는 protector와 상관없이 항상, heap_5는 protector가 켜졌을 때만, §2.8).

효과: overflow가 헤더를 덮으면, 공격자든 버그든 **canary를 모르고는 "그럴듯한" 포인터를 만들 수 없다.** 덮인 값은 XOR을 풀면 무작위 주소가 되어 범위 검사에 걸린다. 다음 예제에서 이것이 정확히 어떤 경우를 더 잡는지 본다.

### 6.4 예제 8 — overflow와 double free, protector 끄고·켜고

무엇을 확인하는 코드인가: 64 B 블록 a, b를 연달아 할당한다(b의 헤더가 a 바로 뒤). 경우 1은 a를 4바이트 넘쳐 0x41로 쓰고 b를 푼다. 경우 2는 a를 두 번 푼다. 경우 3은 a를 4바이트 넘쳐 **0으로** 쓰고 b를 푼다. 각각 protector 0/1.

```c
/* ex8 — heap_4 corruption detection: overflow into next header, double free; protector off/on */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include <string.h>
#ifndef CASE
#define CASE 1
#endif
#if configENABLE_HEAP_PROTECTOR == 1
void vApplicationGetRandomHeapCanary(portPOINTER_SIZE_TYPE *c) { *c = 0x5A17C0DEu ^ REG32(0x40028018u); }
#endif
void vApplicationMallocFailedHook(void) { board_printf_raw("malloc failed hook\n"); }
static void app(void *pv)
{
    (void)pv;
    uint8_t *a = pvPortMalloc(64), *b = pvPortMalloc(64);
    board_printf_raw("case %d protector=%d a=%x b=%x\n", CASE, configENABLE_HEAP_PROTECTOR,
                     (unsigned)a, (unsigned)b);
#if CASE == 1
    memset(a, 0x41, 64 + 4);              /* 4-byte overflow: hits b's header pxNextFreeBlock */
    board_printf_raw("overflowed a by 4 bytes, now vPortFree(b)\n");
    vPortFree(b);
#elif CASE == 3
    memset(a, 0x00, 64 + 4);              /* overflow with zeros: header word becomes NULL */
    board_printf_raw("overflowed a by 4 zero bytes, now vPortFree(b)\n");
    vPortFree(b);
#else
    vPortFree(a);
    board_printf_raw("freed a once, now vPortFree(a) again\n");
    vPortFree(a);
#endif
    board_printf_raw("survived: free=%u (corruption undetected)\n", (unsigned)xPortGetFreeHeapSize());
    board_exit(0);
}
int main(void)
{
    xTaskCreateStatic(app, "app", 512, NULL, 1, (static StackType_t[512]){0}, &(static StaticTask_t){0});
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F1; for c in 1 2; do for p in 0 1; do echo "--- CASE=$c PROT=$p"; $S/run.sh ex8 ex8_${c}_$p HEAP=heap_4 EXTRA_CFLAGS="-DCASE=$c -DconfigENABLE_HEAP_PROTECTOR=$p"; done; done'
```

```text
--- CASE=1 PROT=0
case 1 protector=0 a=20001868 b=200018b0
overflowed a by 4 bytes, now vPortFree(b)

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:370
[board_exit] code=253
[rc=1]
--- CASE=1 PROT=1
case 1 protector=1 a=20001868 b=200018b0
overflowed a by 4 bytes, now vPortFree(b)

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:370
[board_exit] code=253
[rc=1]
--- CASE=2 PROT=0
case 2 protector=0 a=20001868 b=200018b0
freed a once, now vPortFree(a) again

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:369
[board_exit] code=253
[rc=1]
--- CASE=2 PROT=1
case 2 protector=1 a=20001868 b=200018b0
freed a once, now vPortFree(a) again

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:369
[board_exit] code=253
[rc=1]
```

```sh
limactl shell elinux -- bash -lc 'sed -n 368,371p /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c; S=/Users/donh/.rtos_scratch/F1; for p in 0 1; do echo "--- CASE=3 PROT=$p"; $S/run.sh ex8 ex8_3_$p HEAP=heap_4 EXTRA_CFLAGS="-DCASE=3 -DconfigENABLE_HEAP_PROTECTOR=$p"; done'
```

```text
        heapVALIDATE_BLOCK_POINTER( pxLink );
        configASSERT( heapBLOCK_IS_ALLOCATED( pxLink ) != 0 );
        configASSERT( pxLink->pxNextFreeBlock == heapPROTECT_BLOCK_POINTER( NULL ) );

--- CASE=3 PROT=0
case 3 protector=0 a=20001868 b=200018b0
overflowed a by 4 zero bytes, now vPortFree(b)
survived: free=130984 (corruption undetected)
[board_exit] code=0
[rc=0]
--- CASE=3 PROT=1
case 3 protector=1 a=20001868 b=200018b0
overflowed a by 4 zero bytes, now vPortFree(b)

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c:370
[board_exit] code=253
[rc=1]
```

출력에서 볼 것:

- a와 b의 간격은 0x48 = 72 B = 64 B 데이터 + 8 B 헤더다. 4바이트 overflow는 정확히 b 헤더의 첫 워드 `pxNextFreeBlock`을 덮는다.
- **경우 1(0x41로 덮음)은 protector가 없어도 잡혔다.** 할당된 블록의 `pxNextFreeBlock`은 NULL이어야 하는데(370줄) 0x41414141이라서다. 이 검사는 `configASSERT`만 켜져 있으면 늘 있다.
- **경우 2(double free)도 둘 다 잡혔다.** 첫 free가 `xBlockSize`의 할당 비트를 지웠으니 두 번째 free에서 369줄 "할당됨이어야 한다"에 걸린다. 다만 첫 free 뒤에 그 블록이 다른 할당에 다시 쓰였다면(할당 비트가 다시 섰다면) 두 번째 free는 **남의 블록을 푼다** — 이것은 어떤 설정으로도 못 잡는다.
- **경우 3(0으로 덮음)이 핵심이다.** protector가 없으면 덮인 값 0이 "정상 값 NULL"과 같아서 **아무것도 못 잡고 계속 돈다**(`survived`). protector를 켜면 정상 값이 NULL이 아니라 canary라서 0은 틀린 값이 되고 370줄에서 잡힌다. 0으로 넘치는 버그는 흔하다(`memset(buf, 0, sizeof buf + 1)`, 문자열 끝 NUL 하나 더 쓰기). protector가 더 잡는 것은 바로 이 **"그럴듯한 값으로 덮인 경우"**다.
- 검출 시점은 **해제할 때**다. overflow가 일어난 순간이 아니다. 덮은 코드(a의 주인)와 걸린 코드(b를 푼 쪽)가 다른 태스크일 수 있다. 그래서 assert 위치만 보고 범인을 찾으면 안 된다. 범인을 그 자리에서 잡으려면 MPU 가드나 빨간 영역(redzone)이 필요하다(F3, F4).

---

## 7. Zephyr — 정적 객체가 기본, heap은 크기를 정한 풀

### 7.1 기본 스타일: `K_*_DEFINE`

Zephyr는 커널 객체를 **매크로로 정적 정의**하는 것이 기본 문체다. `K_THREAD_DEFINE`, `K_SEM_DEFINE`, `K_MUTEX_DEFINE`, `K_MSGQ_DEFINE`, `K_HEAP_DEFINE`, `K_THREAD_STACK_DEFINE`(모두 `include/zephyr/kernel.h`, 스택은 `kernel/thread_stack.h`). 매크로는 객체를 `STRUCT_SECTION_ITERABLE`로 정해진 링커 섹션(`k_sem_area`, `k_msgq_area` …)에 놓고, 커널은 부팅 때 그 섹션을 훑어 초기화한다. `K_THREAD_DEFINE`으로 만든 스레드는 `main()` 전에 이미 존재하고, 지정한 지연 뒤 스스로 시작한다. FreeRTOS의 `xTaskCreateStatic` + 버퍼 두 개를 한 줄로 줄인 셈이고, **링크 시점 예산 검사가 기본값**이다.

동적 쪽도 있다. `k_malloc`/`k_free`/`k_calloc`/`k_aligned_alloc`은 **시스템 heap** 하나에서 나온다(`kernel/mempool.c`). 시스템 heap의 크기는 `CONFIG_HEAP_MEM_POOL_SIZE`로 정하고, **기본값은 0**이다(`kernel/Kconfig`). 서브시스템이 `HEAP_MEM_POOL_ADD_SIZE_*`로 최소 크기를 요구하면 그 합으로 올라가고(`HEAP_MEM_POOL_IGNORE_MIN`으로 무시 가능), 최종값이 0이면 시스템 heap이 아예 없어서 `k_malloc`은 항상 NULL이다. 즉 Zephyr는 **"쓰려면 크기를 명시적으로 정하라"**가 기본이다. 최종 크기는 `kernel/CMakeLists.txt`가 `K_HEAP_MEM_POOL_SIZE`로 컴파일 정의에 넣고, `mempool.c`가 `K_HEAP_DEFINE(_system_heap, …)`으로 정적 배열을 만든다. heap 자체도 정적 배열이다.

libc `malloc`은 별개의 arena다(`CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE`, 기본 −1 = 남은 RAM 전부, 일부 설정에서는 다른 값). 시스템 heap과 libc arena가 **두 개의 다른 heap**이라는 점을 기억한다. 그 아래의 할당기(`sys_heap`, `k_heap`, `k_mem_slab`)의 구조와 시간 성질은 F2에서 다룬다. 동적 스레드(`CONFIG_DYNAMIC_THREAD`)는 v4.4.2에서도 EXPERIMENTAL이다.

### 7.2 예제 9 — 정적 객체 + 4 KB 시스템 heap

무엇을 확인하는 코드인가: 세마포어, 메시지 큐, 스레드를 매크로로 정적 정의하고, `CONFIG_HEAP_MEM_POOL_SIZE=4096`인 시스템 heap에서 `k_malloc(256)`을 NULL이 나올 때까지 부른다. `CONFIG_SYS_HEAP_RUNTIME_STATS`로 heap 통계를 읽는다.

`prj.conf`:

```text
CONFIG_HEAP_MEM_POOL_SIZE=4096
CONFIG_SYS_HEAP_RUNTIME_STATS=y
CONFIG_QEMU_ICOUNT_SHIFT=5
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(f1_zep)
target_sources(app PRIVATE src/main.c)
```

`src/main.c`:

```c
/* ex9 — Zephyr: static objects by default, k_malloc only from a fixed-size system heap */
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <stdio.h>

K_SEM_DEFINE(data_ready, 0, 1);                          /* object exists at link time */
K_MSGQ_DEFINE(imu_q, sizeof(uint32_t), 16, 4);
static void imu_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    uint32_t s = 42; k_msgq_put(&imu_q, &s, K_NO_WAIT); k_sem_give(&data_ready);
}
K_THREAD_DEFINE(imu_tid, 1024, imu_fn, NULL, NULL, NULL, 5, 0, 0);

extern struct k_heap _system_heap;                       /* defined in kernel/mempool.c */

int main(void)
{
    k_sem_take(&data_ready, K_FOREVER);
    printf("static objects: sem=%p msgq=%p thread=%p\n", (void *)&data_ready, (void *)&imu_q, (void *)imu_tid);
    struct sys_memory_stats st;
    sys_heap_runtime_stats_get(&_system_heap.heap, &st);
    printf("system heap: free=%u alloc=%u\n", (unsigned)st.free_bytes, (unsigned)st.allocated_bytes);
    int n = 0;
    while (k_malloc(256) != NULL) n++;                     /* until the fixed pool runs out */
    sys_heap_runtime_stats_get(&_system_heap.heap, &st);
    printf("k_malloc(256) succeeded %d times, then NULL; free=%u max_alloc=%u\n",
           n, (unsigned)st.free_bytes, (unsigned)st.max_allocated_bytes);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'rm -rf /var/tmp/rtos/F1/zb; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/F1/zep /var/tmp/rtos/F1/zb > /var/tmp/rtos/F1/zb.log 2>&1; echo build_rc=$?; grep -iE "warning|error" /var/tmp/rtos/F1/zb.log | head; tail -4 /var/tmp/rtos/F1/zb.log; /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/F1/zb 5'
```

```text
build_rc=0
           FLASH:       14324 B         4 MB      0.34%
             RAM:       11580 B         4 MB      0.28%
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
static objects: sem=0x200000e0 msgq=0x200000b8 thread=0x200000f0
system heap: free=4100 alloc=0
k_malloc(256) succeeded 15 times, then NULL; free=140 max_alloc=3900
```

객체가 어디에 놓였나:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F1/zb/zephyr; arm-none-eabi-nm -S zephyr.elf | grep -E " (data_ready|imu_q|imu_tid|_system_heap|_k_thread_obj_imu_tid|_k_thread_stack_imu_tid|kheap__system_heap|_msgq_buf_imu_q)"; arm-none-eabi-objdump -h zephyr.elf | grep -E "k_sem_area|k_msgq_area|k_heap_area|noinit|bss"'
```

```text
200000f0 00000080 B _k_thread_obj_imu_tid
200003a8 00000400 B _k_thread_stack_imu_tid
200000a4 00000014 D _system_heap
200000e0 00000010 D data_ready
200000b8 00000028 D imu_q
20001ca8 00001054 B kheap__system_heap
 12 k_heap_area   00000014  200000a4  000037a4  0000387c  2**2
 13 k_msgq_area   00000028  200000b8  000037b8  00003890  2**2
 14 k_sem_area    00000010  200000e0  000037e0  000038b8  2**2
 26 bss           000002b7  200000f0  200000f0  000038d0  2**3
 27 noinit        00002994  200003a8  200003a8  000038d0  2**3
```

출력에서 볼 것:

- `K_SEM_DEFINE`, `K_MSGQ_DEFINE`, `K_HEAP_DEFINE`으로 만든 객체는 각각 `k_sem_area`, `k_msgq_area`, `k_heap_area` 섹션에 **초기값을 가진 데이터**(`D`)로 놓였다. 링크 시점에 이미 완성된 객체다. 스레드 객체(`_k_thread_obj_imu_tid`, 128 B)와 스택(1 KB)은 `.bss`/`noinit`에 있다.
- 시스템 heap도 **정적 배열**이다: `kheap__system_heap` 0x1054 = 4,180 B(요청 4,096 B + 관리 구조). Zephyr에서 "heap을 쓴다"는 것은 "이 크기의 정적 배열 안에서 동적으로 나눈다"는 뜻이고, 총량은 링크 시점에 고정된다. 넘치면 FreeRTOS 정적 빌드처럼 링크가 실패한다.
- 4 KB에서 256 B는 15번 성공하고 16번째가 NULL이었다(15 × 256 = 3,840 B). 할당 하나에 256 B보다 조금 더 든다(`max_alloc` 3,900 B, 남은 140 B). 이 청크 단위 오버헤드와 `sys_heap`의 크기 등급 구조는 F2에서 다룬다. `k_malloc`의 NULL은 FreeRTOS처럼 훅을 부르지 않는다. 호출자가 검사해야 한다.

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나

이 노트의 개념이 소스 어디에 있는지 모았다(FreeRTOS V11.3.2, Zephyr v4.4.2에서 확인).

| 개념 | FreeRTOS | Zephyr |
|---|---|---|
| 할당기 구현 | `portable/MemMang/heap_1.c` ~ `heap_5.c`: `pvPortMalloc`, `vPortFree`, `prvInsertBlockIntoFreeList`, `prvHeapInit`(heap_4), `vPortDefineHeapRegions`(heap_5) | `kernel/mempool.c`: `k_malloc` → `z_alloc_helper` → `sys_heap_noalign_alloc` (`lib/heap/`, F2) |
| 블록 헤더 | `BlockLink_t { pxNextFreeBlock; xBlockSize; }`, 할당 비트 = `xBlockSize`의 MSB | `sys_heap` 청크 헤더 (F2) |
| 할당 중 상호 배제 | `vTaskSuspendAll()` / `xTaskResumeAll()` (ISR 금지) | `k_heap`의 spinlock (ISR에서 `K_NO_WAIT`로 가능) |
| 커널 객체가 할당하는 곳 | `tasks.c` `prvCreateTask`(스택 → TCB 순서, A4), `queue.c` `xQueueGenericCreate`(제어 블록 + 저장 공간 한 번에), `timers.c` `xTimerCreate`, `event_groups.c` `xEventGroupCreate`, `stream_buffer.c` `xStreamBufferGenericCreate` | 기본은 할당 없음: `K_*_DEFINE` → `STRUCT_SECTION_ITERABLE` 섹션 |
| 숨은 실행 중 할당 | `vTaskListTasks`, `vTaskGetRunTimeStatistics`(`tasks.c` 7451, 7620줄 근처), newlib stdio (F7) | `z_thread_malloc`(스레드의 `resource_pool`, 일부 커널 API가 사용), libc arena |
| 해제 시 정적·동적 구분 | `prvDeleteTCB`: `ucStaticallyAllocated` (0 동적 / 1 스택만 정적 / 2 둘 다 정적) | 정적 객체는 해제 개념이 없음 |
| 커널 자신의 객체 | `configSUPPORT_STATIC_ALLOCATION 1`이면 idle·timer 태스크, 타이머 큐를 정적으로 (`timers.c` `prvCheckForValidListAndQueue`의 `xStaticTimerQueue`) | 처음부터 정적 |
| 설정 검사 | `FreeRTOS.h`: 둘 다 0이면 `#error`, stats formatting은 동적 필요 | `kernel/Kconfig` `HEAP_MEM_POOL_SIZE` 기본 0 |
| 스택 전용 heap | `portable.h` `configSTACK_ALLOCATION_FROM_SEPARATE_HEAP` → `pvPortMallocStack` | 스레드 스택은 항상 정적 (`K_THREAD_STACK_DEFINE`) 또는 `CONFIG_DYNAMIC_THREAD` |

이어지는 곳: 고정 블록 풀과 `k_mem_slab`/`k_heap`/`sys_heap` 내부는 **F2**, 스택 크기 산정과 overflow 탐지는 **F3**, heap과 스택 사이 경계를 하드웨어로 지키는 MPU는 **F4**, newlib `_reent`·malloc 락은 **F7**, heap_5 영역을 여러 RAM 뱅크(TCM/SRAM/SDRAM)에 배치하는 실무는 **F8**, 할당 시간을 응답 시간 분석에 넣는 것은 **N3**, 인증 문서의 deviation은 **O 모듈**에서 다룬다.

---
## 9. 실무에서 다시 보기

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="290" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165.0" y="46.5" font-size="13" text-anchor="middle">스케줄러 시작 뒤에 free가 필요한가?</text>
<rect x="370" y="20" width="290" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515.0" y="38.7" font-size="12" text-anchor="middle">정적 할당 (DYNAMIC=0)</text>
<text x="515.0" y="53.7" font-size="12" text-anchor="middle">또는 heap_1 + 초기화 후 잠금</text>
<rect x="20" y="120" width="290" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165.0" y="146.6" font-size="13" text-anchor="middle">요청 크기가 몇 종류로 고정인가?</text>
<rect x="370" y="120" width="290" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515.0" y="138.7" font-size="12" text-anchor="middle">고정 크기 블록 풀 (F2)</text>
<text x="515.0" y="153.7" font-size="12" text-anchor="middle">O(1), 단편화 없음</text>
<rect x="20" y="220" width="290" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165.0" y="246.6" font-size="13" text-anchor="middle">서드파티가 libc malloc을 부르나?</text>
<rect x="370" y="200" width="290" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="515.0" y="218.7" font-size="12" text-anchor="middle">heap_4 (RAM 뱅크 여럿이면 heap_5)</text>
<text x="515.0" y="233.7" font-size="12" text-anchor="middle">+ 통계 감시 + 스트레스 시험</text>
<rect x="370" y="268" width="290" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="515.0" y="286.7" font-size="12" text-anchor="middle">heap_3 (libc malloc 감싸기)</text>
<text x="515.0" y="301.7" font-size="12" text-anchor="middle">+ malloc 락 구현 (F7)</text>
<line x1="311" y1="42" x2="368" y2="42" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="339.5" y="36.0" font-size="12" text-anchor="middle">아니오</text>
<line x1="165" y1="65" x2="165" y2="118" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="173.0" y="95.5" font-size="12">예</text>
<line x1="311" y1="142" x2="368" y2="142" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="339.5" y="136.0" font-size="12" text-anchor="middle">예</text>
<line x1="165" y1="165" x2="165" y2="218" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="173.0" y="195.5" font-size="12">아니오</text>
<line x1="311" y1="236" x2="368" y2="224" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="339.5" y="224.0" font-size="12" text-anchor="middle">아니오</text>
<line x1="311" y1="250" x2="368" y2="288" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="339.5" y="263.0" font-size="12" text-anchor="middle">예</text>
</svg>
```
그림 7 — 할당 방식을 고르는 흐름. 위에서부터 강한 보장이다. "아니오"로 내려갈수록 증명 대신 시험·감시에 기댄다.

### 9.1 드론 비행 제어기

비행 제어 루프(IMU 1~8 kHz, rate loop, 모터 출력)는 **실행 중 할당 0**이 원칙이다. 태스크·큐·세마포어는 정적 생성, 센서 샘플은 정적 링버퍼, 로그는 정적 이중 버퍼. 그런데 같은 MCU에 MAVLink 파서, 파일 시스템(SD 카드 로그), 때로는 네트워크 스택이 올라간다. 이들은 가변 크기 버퍼를 원한다. 흔한 구성은 이렇다.

- 비행 경로: 정적만. 코드 리뷰와 정적 분석(MISRA Dir 4.12)으로 지킨다.
- 부가 기능: heap_4를 따로 두고, 그 heap을 쓰는 태스크는 비행 태스크보다 낮은 우선순위로 둔다. 다만 §3.5처럼 **할당 중에는 스케줄러가 잠긴다**는 점 때문에, 비행 태스크의 응답 시간 분석에 "최악 할당 시간"을 블로킹 항으로 넣는다. 예제 4의 식으로 그 값을 정한다. 더 깔끔한 방법은 부가 기능용 할당기를 스케줄러 잠금 대신 뮤텍스(우선순위 상속)로 보호하는 별도 풀로 만드는 것이다(F2).
- 필드 감시: 1초마다 `vPortGetHeapStats()`의 가장 큰 빈 블록과 최소 여유를 텔레메트리에 싣는다. 비행 로그를 분석하는 쪽에서 "가장 큰 블록의 최저값"이 비행 시간에 따라 줄어드는지를 본다. 줄어든다면 예제 6의 heap_2처럼 언젠가 실패한다.

PX4(NuttX 위)는 이 구성의 실제 예다. 모듈 시작 때 `new`로 객체를 만들고 비행 중에는 할당을 피하는 것이 관례다(Q1에서 다룬다).

### 9.2 로봇 관절 모터 컨트롤러

FOC 전류 루프 20 kHz는 ISR 안에서 돈다. 여기서는 어떤 할당도 안 되고(FreeRTOS heap은 ISR 금지), 통신(CAN) 쪽도 메시지 크기가 고정(CAN FD 64 B 이하)이라 **고정 블록 풀이나 정적 큐**로 충분하다. 이런 장비는 정적 전용 빌드(`configSUPPORT_DYNAMIC_ALLOCATION 0`)가 가장 자연스럽다. RAM이 32~128 KB인 작은 MCU에서는 §5.3의 "링크 실패로 예산 검사"가 그대로 이득이다.

### 9.3 센서 허브 MCU

Linux SoC 옆에서 IMU 타임스탬프를 모아 SPI로 넘기는 MCU는 버스트 크기가 들쭉날쭉할 수 있다. 그래도 최대 버스트는 정해져 있다. "최대 크기 버퍼 N개"의 풀을 정적으로 두면, 작은 버스트에서 내부 단편화(빈 칸 낭비)가 생기지만 외부 단편화는 없다. RAM이 넉넉하면 이 거래가 거의 항상 이긴다. Zephyr라면 `K_MEM_SLAB_DEFINE`이 그 풀이다(F2).

### 9.4 Don의 SSD 컨트롤러

SSD 펌웨어는 이 노트의 결론을 이미 실천하던 곳이다. 부팅 때 DRAM 크기를 보고 매핑 테이블 캐시, 쓰기 버퍼, 명령 슬롯의 개수를 정하고(초기화 시점 할당), 실행 중에는 고정 크기 슬롯을 빈 목록에서 꺼내고 돌려줄 뿐이다(고정 블록 풀). 면접에서 "RTOS에서 malloc을 쓰나요?"에 Don이 할 수 있는 가장 강한 답은 이 경험이다: "SSD 펌웨어에서는 수백만 IOPS 경로에서 malloc을 한 번도 안 불렀다. 모든 버퍼가 부팅 때 크기가 정해진 풀에 있었다. 이유는 시간 상한과 단편화다." 그다음 FreeRTOS 용어로 옮기면 된다: heap_1 + 초기화 후 잠금, 또는 정적 전용 빌드 + 풀.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `xPortGetFreeHeapSize()`만 감시 | 여유가 50% 넘는데 할당 실패 | 외부 단편화는 합계에 안 보인다 (예제 5) | `vPortGetHeapStats()`의 가장 큰 빈 블록을 감시 |
| 크기가 섞인 앱에 heap_2 | 시험은 통과, 필드에서 수십 분~수 시간 뒤 실패 | 병합이 없어 큰 블록이 사라진다 (예제 6) | heap_4, 또는 크기별 고정 풀 |
| "heap을 넉넉히" 로 해결 | 실패가 늦어질 뿐, 재현이 더 어려워진다 | heap 5배에 실패 12분 지연 (예제 6) | 할당 패턴을 바꾼다 (풀, 정적) |
| 생성 API 반환값 미검사 | 나중에 엉뚱한 곳에서 HardFault | `xQueueCreate`가 NULL을 돌려줬다 | 모든 생성 결과 assert, 또는 정적 생성 |
| ISR에서 `pvPortMalloc` | 드물게 heap 목록이 깨져 무작위 오동작 | heap 보호가 스케줄러 잠금이라 ISR을 못 막는다 | ISR은 미리 할당한 버퍼만, 할당은 태스크로 미룬다 (E1) |
| 운전 중에 `vTaskListTasks` 등 디버그 API | 잠근 heap에 걸리거나, 실행 중 할당이 생긴다 | 안에서 `pvPortMalloc` (예제 1) | `uxTaskGetSystemState()` + 정적 배열 |
| heap_5 + 정적 생성자에서 객체 생성 | 부팅 직후 assert | `vPortDefineHeapRegions` 전에 할당 | 영역 정의를 `main` 첫 줄로, 생성자에서 RTOS 객체 금지 |
| heap_3 + 서드파티 malloc, 락 없음 | 가끔 heap 손상 | newlib `__malloc_lock`이 빈 함수 (B11) | 락 구현 (F7) 또는 heap_3 대신 heap_4로 서드파티 malloc을 리다이렉트 |
| 받은 메모리가 0이라고 가정 | 콜드 부팅에서만 동작, 웜 리셋 뒤 깨짐 | heap이 `.noinit`이거나 재사용 블록 (B5) | `pvPortCalloc` 또는 명시적 memset |
| 부팅 직후 heap 여유 0 로그에 놀람 | 고장으로 오판 | heap_4는 첫 할당 때 초기화 (예제 1) | 첫 할당 뒤에 읽는다 |
| 낮은 우선순위 태스크의 큰 할당 | 높은 우선순위 태스크가 가끔 늦는다 | 할당 중 스케줄러 잠금, 블록 수 비례 (예제 4) | 응답 시간 분석에 넣거나, 별도 풀 |

---

## 11. 면접에서 이렇게 말한다

**Q.** Can you use malloc in an RTOS?

**A.** 쓸 수는 있지만 조건이 붙는다. 위험은 넷이다: 시간이 빈 블록 수에 비례하고, 단편화로 여유가 있어도 실패하고, 실패가 실행 중에 나고, 오염이 모든 태스크로 번진다. 그래서 비행 제어 같은 핵심 경로는 정적 할당이나 초기화 시점 할당 + 잠금으로 실행 중 할당을 없앤다. 서드파티 때문에 꼭 필요하면 heap_4나 고정 블록 풀을 쓰고, 최악 시간과 가장 큰 빈 블록을 측정·감시한다.

> Yes, but with conditions. A general-purpose heap has four problems in a real-time system: allocation time grows with the number of free blocks, fragmentation can make a request fail while plenty of total memory is free, the failure happens at run time, and a single overflow corrupts metadata shared by every task. So on the control path I allocate everything statically or only during initialization and then lock the heap — that's what MISRA, JSF rule 206 and JPL's Power of Ten ask for. Where a third-party stack needs dynamic memory, I use heap_4 or fixed-size pools, bound the worst-case time, and monitor the largest free block in the field. On QEMU I measured a heap_2 server fail after 249 requests with 65 percent of the heap still free.

**꼬리 질문**: "What about C++ new?" → 같은 heap이다. `operator new`를 `pvPortMalloc`으로 바꾸거나 초기화 때만 쓰고, 예외가 꺼진 빌드에서는 실패를 `nullptr`(`new (std::nothrow)`)로 받는다.

**Q.** What's the difference between heap_2 and heap_4? Which would you pick?

**A.** 둘 다 블록 헤더 8 B짜리 빈 목록 할당기다. heap_2는 목록이 크기 순이고 이웃을 합치지 않는다. heap_4는 주소 순이고 풀 때 앞뒤 빈 블록과 합친다. 그래서 heap_2는 크기가 섞이면 큰 블록이 사라져 결국 실패하고, 빈 블록 수에 상한도 없다. 새 설계라면 heap_4다. heap_1은 free가 없고, heap_3은 libc malloc 감싸기, heap_5는 heap_4를 여러 RAM 영역으로 넓힌 것이다.

> Both keep a singly linked free list with an eight-byte header per block. heap_2 keeps the list sorted by size and never merges neighbors; heap_4 keeps it sorted by address and coalesces with the blocks before and after on every free. In my test with the same random workload, heap_2 failed a 1 KB request with 18 KB free because its largest block was 16 bytes too small, while heap_4 never dropped below a 13 KB largest block over twenty thousand operations. heap_2 was also slower in the tail, because unmerged fragments made the list 85 entries long versus 19. So heap_4 by default; heap_1 if nothing is ever freed; heap_5 when RAM is split across banks.

**Q.** Is pvPortMalloc deterministic? What's its worst-case execution time?

**A.** 상수 시간은 아니지만 상한은 있다. heap_4 malloc은 고정 비용 + 블록당 비용 × 지나는 빈 블록 수다. QEMU에서 블록당 352 ns로 완전한 직선이었다. heap_4는 빈 블록끼리 붙을 수 없어서 빈 블록 수 ≤ 동시 사용 블록 수 + 1이다. 그러니 동시 사용 블록 수를 알면 WCET를 식으로 쓸 수 있다. 그리고 할당 중에는 스케줄러가 잠기므로 그 시간이 다른 태스크의 블로킹 항이 된다.

> It isn't constant-time, but it is bounded. heap_4 walks the free list, so the cost is a fixed part plus a per-block part times the number of free blocks it passes. On a Cortex-M3 model I measured an exact straight line, about 350 nanoseconds per free block, so 400 fragments meant 146 microseconds for one allocation. Because heap_4 coalesces, two free blocks can never be adjacent, so the free-block count is at most the number of live allocations plus one — that gives a closed-form bound if you know how many blocks can be live at once. heap_2 has no such bound. And since the allocator suspends the scheduler, that time is blocking for every other task, so it goes into the response-time analysis.

**Q.** How do you prove you won't get a fragmentation failure?

**A.** 증명할 수 있는 방법은 둘뿐이다. 초기화 뒤에 free를 안 하거나(정적·heap_1·잠금), 크기가 하나뿐인 고정 블록 풀을 쓴다. 가변 크기 heap에는 Robson의 최악 경계가 있지만 너무 커서 실용적이지 않다. 그래서 heap_4를 쓸 때는 증명이 아니라 증거를 낸다: 최악 부하 스트레스 시험, 그리고 필드에서 가장 큰 빈 블록의 최저값 감시.

> Strictly, only two designs let you prove it. Either nothing is freed after initialization — static allocation, heap_1, or a locked heap — so if the first boot succeeds every boot succeeds; or every size class has its own fixed-block pool, where external fragmentation can't exist by construction and you only count peak usage per pool. For a general heap there are theoretical worst-case bounds, like Robson's, but they're too large to size a microcontroller heap. So with heap_4 I give evidence rather than proof: an accelerated stress test with worst-case sizes and lifetimes, and field telemetry of the minimum-ever largest free block compared against the largest request.

**Q.** Why isn't free heap size enough to monitor heap health?

**A.** 합계는 단편화를 못 본다. 예제에서 heap의 55%가 비어 있었는데 1 KB 요청이 실패했다. 가장 큰 빈 블록을 봐야 한다. FreeRTOS는 `vPortGetHeapStats`가 그 값을 준다(heap_4/5). 그리고 부팅 뒤 최저 여유(`xPortGetMinimumEverFreeHeapSize`)로 heap 크기를 정한다.

> Free bytes is a sum; fragmentation is about the largest contiguous piece. I had a heap that was 55 percent free and still failed a one-kilobyte request. So I report the largest free block from vPortGetHeapStats, plus the minimum-ever free size for sizing, and I alert on the largest block trending down over uptime.

**Q.** How do you build FreeRTOS without any heap?

**A.** `configSUPPORT_DYNAMIC_ALLOCATION 0`, `configSUPPORT_STATIC_ALLOCATION 1`로 하고 heap 파일을 링크하지 않는다. 모든 객체를 `xTaskCreateStatic`, `xQueueCreateStatic` 같은 API에 버퍼를 넘겨 만들고, idle·timer 태스크 메모리는 `vApplicationGet…TaskMemory` 콜백이나 `configKERNEL_PROVIDED_STATIC_MEMORY`로 준다. 이점은 메모리 예산을 링커가 검사한다는 것이다. 64 KB RAM 설정에서 스택을 잘못 적으면 정적 빌드는 "RAM overflowed by 7896 bytes"로 링크가 실패했고, 동적 빌드는 링크가 통과한 뒤 실행 중에 실패했다.

> Set configSUPPORT_DYNAMIC_ALLOCATION to 0 and static allocation to 1, and don't link any heap_x file. Every object is created with the Static variant and buffers I declare, and the idle and timer task memory comes from the vApplicationGet callbacks or configKERNEL_PROVIDED_STATIC_MEMORY. The real win is that the linker enforces the memory budget: with a 64 KB RAM map and an oversized stack, the static build failed to link — region RAM overflowed by 7,896 bytes — while the heap build linked fine and only failed at run time in the malloc-failed hook.

**Q.** Can you call pvPortMalloc from an ISR?

**A.** 안 된다. FreeRTOS heap은 스케줄러 잠금으로 보호하는데, 그건 태스크끼리만 막고 ISR은 못 막는다. ISR은 미리 할당한 버퍼(정적 링버퍼, 풀)를 쓰고, 할당이 필요하면 태스크로 미룬다. Zephyr의 `k_heap`은 스핀락이라 ISR에서 `K_NO_WAIT`로 가능하지만, 시간 상한은 따로 따져야 한다.

> No. The FreeRTOS heaps protect their free list by suspending the scheduler, which stops other tasks but not interrupts, so an ISR could interrupt a task in the middle of a list update. ISRs use pre-allocated buffers — a static ring buffer or a fixed pool — and defer anything that needs allocation to a task. Zephyr's k_heap uses a spinlock, so it is callable from an ISR with K_NO_WAIT, but I'd still bound its time before putting it there.

---

## 12. 직접 해보기

1. **계산**: heap_4에서 동시에 살아 있는 블록이 최대 30개다. 예제 4의 식(4.8 us + 0.35 us × N)으로 malloc 한 번의 상한을 구하라. 같은 시스템에서 heap_2라면 왜 같은 식을 쓸 수 없나?
   정답/힌트: N ≤ 31 → 4.8 + 0.35 × 31 ≈ 15.7 us. heap_2는 붙은 빈 블록도 따로 남아 N이 동시 사용 수가 아니라 쪼개진 역사로 정해지므로 상한이 없다.
2. **코드**: 예제 1의 `monitor`에서 `vTaskListTasks` 대신 `uxTaskGetSystemState()`에 정적 `TaskStatus_t` 배열(8칸)을 넘겨 태스크 이름과 상태를 찍어라. 잠근 heap에 걸리지 않음을 확인하라.
   정답/힌트: `static TaskStatus_t st[8]; UBaseType_t n = uxTaskGetSystemState(st, 8, NULL);` — 배열이 작으면 0을 돌려준다.
3. **측정**: 예제 5를 heap_4로 `-DSLOTS=100`, `120`으로 돌려 heap_4가 실패하는 지점을 찾아라. 실패 순간 "전체 빈 공간 > 요청"인가(단편화)? 아니면 정말 다 찼나?
   정답/힌트: 출력의 `total_free`와 `largest`를 비교한다. 두 값이 둘 다 요청보다 작으면 단순 부족이고, `total_free`만 크면 heap_4도 단편화로 실패한 것이다. heap_4가 "단편화가 없다"는 말이 아님을 직접 확인하는 과제다.
4. **코드**: 예제 6의 `-DCLASSES` 부하를 크기별 고정 블록 풀 3개(128/512/1500 B)와 세션 풀(200 B), flush 풀(4 KB 1칸)로 바꿔 100만 요청을 돌려라. 실패가 사라지고 풀별 최대 사용 개수가 나오면, 그 개수가 곧 RAM 예산이다(F2의 예고편).
   정답/힌트: 풀 = 정적 배열 + 빈 칸 단일 연결 목록. 꺼내기·돌려주기가 포인터 두 번이라 시간도 상수다.
5. **측정**: 예제 3을 heap_4 + `-DconfigHEAP_CLEAR_MEMORY_ON_FREE=1`로 돌려 free 시간 분포가 어떻게 바뀌는지 보고, 요청 크기와 free 시간의 관계를 설명하라.
   정답/힌트: free가 블록 크기에 비례해 길어진다(memset). 다만 memset은 스케줄러 잠금 전에 하므로 다른 태스크를 막는 시간은 거의 그대로다.
6. **Zephyr**: 예제 9에서 `CONFIG_HEAP_MEM_POOL_SIZE`를 지우고(기본 0) 다시 빌드·실행하라. 빌드가 되나? `k_malloc`은 무엇을 돌려주나?
   정답/힌트: `_system_heap`이 없어서 `extern struct k_heap _system_heap` 참조가 링크 에러가 된다. 통계 줄을 지우면 `k_malloc`은 항상 NULL이다(Kconfig 도움말: 최종 값이 0이면 시스템 heap이 없다).
7. **설계**: 드론 FC에 MAVLink 파서와 SD 카드 로거(FatFs)를 올린다. 무엇을 정적으로, 무엇을 풀로, 어디에 heap_4를 허용할지 표로 정하고, 비행 태스크의 응답 시간에 heap이 끼치는 블로킹 항을 식으로 적어라.
   정답/힌트: 비행 경로는 전부 정적. MAVLink 메시지는 최대 길이 고정(280 B 이하)이라 풀. FatFs는 `ff_memalloc`을 정적 버퍼로 바꿀 수 있다. heap이 남으면 비행 태스크의 블로킹 항 B ≥ 그 heap의 최악 할당 시간(예제 4의 식).

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 정적 할당 | static allocation | 링크 시점에 주소와 크기가 정해지는 메모리 |
| 동적 할당 | dynamic allocation | 실행 중에 할당기가 빈 조각을 골라 주는 메모리 |
| 초기화 시점 할당 | allocate-at-init | 부팅 중에만 할당하고 그 뒤 heap을 잠그는 정책 (JSF 206, Power of 10) |
| 할당기 | allocator | malloc·free를 구현하는 코드와 자료구조 |
| 빈 블록 목록 | free list | 할당되지 않은 블록을 잇는 연결 목록 |
| `BlockLink_t` | 블록 헤더 | FreeRTOS heap_2/4/5의 8 B 헤더: 다음 빈 블록 + 크기(MSB = 할당됨) |
| first fit | 처음 맞는 것 | 목록 앞에서부터 처음 들어가는 블록을 고름 (heap_4/5) |
| best fit | 가장 잘 맞는 것 | 들어가는 블록 중 가장 작은 것을 고름 (heap_2의 크기 순 목록) |
| 병합 | coalescing | 이웃한 빈 블록을 하나로 합침 (heap_4/5) |
| 외부 단편화 | external fragmentation | 빈 공간 합은 충분한데 연속 조각이 없음 |
| 내부 단편화 | internal fragmentation | 블록 안에서 버려지는 공간 (헤더, 정렬, 큰 칸에 작은 객체) |
| 결정성 | determinism | 실행 시간의 상한을 미리 말할 수 있음 |
| WCET | 최악 실행 시간 | worst-case execution time, 응답 시간 분석의 입력 |
| 스케줄러 잠금 | scheduler suspend | `vTaskSuspendAll`, 컨텍스트 스위치를 막지만 인터럽트는 허용 |
| heap protector | 힙 포인터 보호 | 헤더 포인터를 난수 canary와 XOR, 범위 검사 (V11) |
| 고정 블록 풀 | fixed-size block pool | 같은 크기 칸만 가진 할당기, O(1), 외부 단편화 없음 (F2) |
| TLSF | Two-Level Segregated Fit | 크기 등급 비트맵으로 O(1)인 범용 할당기 |
| 시스템 heap | Zephyr system heap | `k_malloc`의 heap, `CONFIG_HEAP_MEM_POOL_SIZE`(기본 0) |
| deviation | 규칙 예외 | MISRA 규칙을 근거와 함께 문서로 어기는 것 |

---

## 14. 요약 & 체크리스트

RTOS에서 범용 heap의 문제는 넷이다: 시간이 빈 블록 수에 비례하고, 단편화로 여유가 있어도 실패하고, 그 실패가 실행 중에 나고, 오염이 모든 태스크로 번진다. 규칙(MISRA Dir 4.12·Rule 21.3, JSF 206, Power of 10)은 그래서 동적 할당을 막거나 "초기화 뒤 금지"로 제한한다. FreeRTOS는 할당기 다섯 개를 준다. heap_1은 free가 없는 bump 할당기라 상수 시간(약 3 us, 그중 대부분이 스케줄러 잠금 비용)이고, heap_2는 크기 순·병합 없음이라 크기가 섞이면 큰 블록이 사라진다(55% 여유에서 1 KB 실패, 서버 부하에서 2.5초 만에 실패, heap 5배로 늘려도 12분). heap_4는 주소 순·병합이라 빈 블록 수 ≤ 동시 사용 블록 수 + 1이 되고, 시간은 4.8 us + 0.35 us × N(QEMU)으로 상한을 쓸 수 있으며, 같은 서버 부하에서 100만 요청 동안 실패가 없었다. heap_3은 libc malloc을 감싸고(락은 F7), heap_5는 heap_4를 여러 RAM 영역으로 넓힌다(범위 검사는 protector일 때만). `xPortGetFreeHeapSize`는 단편화를 못 보므로 `vPortGetHeapStats`의 가장 큰 빈 블록을 감시한다. protector는 헤더 포인터를 canary로 XOR해서, 0처럼 "그럴듯한" 값으로 덮인 overflow까지 해제 시점에 잡는다. 정적 전용 빌드(`configSUPPORT_DYNAMIC_ALLOCATION 0`)는 실행 중 할당을 물리적으로 없애고 메모리 예산을 링커가 검사하게 한다. Zephyr는 `K_*_DEFINE` 정적 객체가 기본이고, `k_malloc`의 시스템 heap도 크기를 정한(기본 0) 정적 배열이다. "단편화로 실패하지 않음"을 증명할 수 있는 것은 초기화 뒤 free 없음과 고정 블록 풀뿐이고, heap_4는 스트레스 시험과 필드 감시로 증거를 쌓는다.

- [ ] 동적 할당의 위험 네 가지를 RTOS·MCU 관점에서 말할 수 있다
- [ ] MISRA Dir 4.12 / Rule 21.3, JSF 206, Power of 10 Rule 3이 각각 무엇을 금지하는지 구별한다
- [ ] 초기화 후 heap 잠금을 `--wrap`이나 `traceMALLOC`으로 구현하고, 숨은 실행 중 할당(`vTaskListTasks`, newlib stdio)을 찾을 수 있다
- [ ] heap_1~heap_5의 목록 순서·병합·용도를 표로 말할 수 있다
- [ ] `BlockLink_t`, 할당 비트(MSB), `xStart`/`pxEnd`, 최소 블록 크기를 설명할 수 있다
- [ ] heap_4 할당 시간의 상한을 a + b × N과 N ≤ L + 1로 유도할 수 있다
- [ ] 할당 중 스케줄러 잠금이 다른 태스크의 블로킹 항이 되는 이유를 안다
- [ ] 외부·내부 단편화를 구별하고, 가장 큰 빈 블록으로 단편화를 측정할 수 있다
- [ ] heap_2가 크기가 섞인 부하에서 왜 결국 실패하는지, heap을 늘려도 왜 안 되는지 숫자로 말할 수 있다
- [ ] 정적 전용 FreeRTOS를 빌드하고, 링크 실패가 왜 이득인지 설명할 수 있다
- [ ] heap protector가 무엇을 더 잡는지(0으로 덮인 헤더), 검출 시점이 왜 해제 때인지 안다
- [ ] Zephyr의 `K_*_DEFINE`과 `CONFIG_HEAP_MEM_POOL_SIZE`(기본 0)를 설명하고, 내부는 F2로 연결할 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `portable/MemMang/heap_1.c` ~ `heap_5.c`(`BlockLink_t`, `prvInsertBlockIntoFreeList`, `prvHeapInit`, `vPortDefineHeapRegions`, `vPortGetHeapStats`, `heapPROTECT_BLOCK_POINTER`, `heapVALIDATE_BLOCK_POINTER`), `include/portable.h`(`HeapStats_t`, `HeapRegion_t`, `pvPortMallocStack`), `include/FreeRTOS.h`(`configSUPPORT_*_ALLOCATION` 기본값과 검사), `tasks.c`(`prvCreateTask`, `prvDeleteTCB`, `ucStaticallyAllocated`, `vTaskListTasks`), `queue.c`(`xQueueGenericCreate`), `timers.c`(`prvCheckForValidListAndQueue`), `event_groups.c`(`EventGroup_t`), `History.txt`(V10.5.0 heap 개선, V11.0.0 heap protector, V11.2.0 할당 블록 포인터 보호·`xPortResetHeapMinimumEverFreeHeapSize`·`configVALIDATE_HEAP_BLOCK_POINTER`, V11.3.1 heap_5 protector assert 수정)
- FreeRTOS 문서: "Memory Management"(heap_1~heap_5 설명), "Static Vs Dynamic Memory Allocation" — freertos.org 커널 문서
- Zephyr v4.4.2 소스: `kernel/mempool.c`(`k_malloc`, `_system_heap`), `kernel/Kconfig`(`HEAP_MEM_POOL_SIZE`, `HEAP_MEM_POOL_IGNORE_MIN`, `DYNAMIC_THREAD`), `kernel/CMakeLists.txt`(`K_HEAP_MEM_POOL_SIZE`), `include/zephyr/kernel.h`(`K_THREAD_DEFINE`, `K_SEM_DEFINE`, `K_MSGQ_DEFINE`, `K_HEAP_DEFINE`), `lib/heap/Kconfig`(`SYS_HEAP_RUNTIME_STATS`), `lib/libc/common/Kconfig`(`COMMON_LIBC_MALLOC_ARENA_SIZE`)
- Zephyr 문서: Memory Heaps — https://docs.zephyrproject.org/latest/kernel/memory_management/heap.html
- MISRA C:2023 Directive 4.12 (MathWorks Polyspace 설명) — https://www.mathworks.com/help/bugfinder/ref/misrac2023d4.12.html
- MISRA C:2023 Rule 21.3 (MathWorks Polyspace 설명) — https://au.mathworks.com/help/bugfinder/ref/misrac2023rule21.3.html
- Lockheed Martin, Joint Strike Fighter Air Vehicle C++ Coding Standards (2005), AV Rule 206
- G. J. Holzmann, "The Power of Ten — Rules for Developing Safety Critical Code", IEEE Computer, 2006 (Rule 3)
- J. M. Robson, "Bounds for Some Functions Concerning Dynamic Storage Allocation", Journal of the ACM, 1974
- M. S. Johnstone, P. R. Wilson, "The Memory Fragmentation Problem: Solved?", ISMM 1998
- M. Masmano, I. Ripoll, A. Crespo, J. Real, "TLSF: a New Dynamic Memory Allocator for Real-Time Systems", ECRTS 2004
- 관련 노트: A4(TCB·태스크 RAM, 스택 → TCB 순서), C1(커널 자료구조), C7(크리티컬 섹션·스케줄러 잠금), C9(idle의 메모리 회수), B5(.bss·`.noinit` heap), B11(libc·`_sbrk`·newlib stdio의 malloc), E1(ISR 규칙), E8(noinit 기록), F2(풀·`k_heap`·`sys_heap`), F3(스택), F4(MPU), F7(재진입성·malloc 락), F8(메모리 배치), D3(우선순위 역전), N3(응답 시간 분석), EL F9(Linux buddy·slab)
