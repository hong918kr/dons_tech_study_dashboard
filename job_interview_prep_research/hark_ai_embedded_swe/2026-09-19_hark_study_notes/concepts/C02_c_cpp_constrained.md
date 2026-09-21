# C02. 자원 제약 환경의 C/C++ — 컴파일러와 하드웨어 사이에서 정확하고 작고 예측 가능한 코드 쓰기

> **이 노트를 다 읽으면**: `volatile`·`const`·섹션 배치가 실제로 무엇을 보장하는지 설명할 수 있다 · 정적 할당과 memory pool로 malloc 없는 설계를 할 수 있다 · 정수 승격·정렬·엔디언·fixed-point 함정을 코드 리뷰에서 잡을 수 있다 · Cortex-M 원자성(LDREX/STREX, critical section)과 임베디드 C++ 규칙(예외/RTTI 끄기, RAII, constexpr, placement new)을 면접에서 설명할 수 있다
> **JD 연결**: "Strong proficiency in C and/or C++ in resource-constrained environments" · "Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers" · "support model inference within memory and latency budgets"
> **Don 기준 난이도**: 양산 SSD 펌웨어에서 매일 쓰던 영역이라 대부분 익숙함 / 새로 정리할 부분은 C11 atomics와 메모리 모델, ARMv6-M(M0+)과 ARMv7-M의 원자성 차이, 임베디드 C++의 구체적 비용(정적 초기화 guard, 템플릿 bloat), MISRA C:2012 규칙 체계

---

## 0. 큰 그림

임베디드 C/C++은 "언어 문법"보다 **세 당사자 사이의 약속**을 다루는 일이다. 소스 코드는 C 추상 기계(abstract machine)에 대한 약속이고, 컴파일러는 그 약속만 지키면 어떤 최적화든 한다. 하드웨어(레지스터, DMA, 인터럽트)는 C 추상 기계가 모르는 존재다. 버그는 대부분 이 셋의 가정이 어긋날 때 생긴다.

```
   개발자의 의도                     C 표준(추상 기계)                 실제 하드웨어
 "이 레지스터를 두 번 읽어라"  --->  관찰 가능한 동작만 보존      --->  메모리 매핑 레지스터
 "ISR가 이 변수를 바꾼다"            (volatile 접근, I/O)              인터럽트, DMA, 캐시
 "이 구조체는 패킷 모양"             그 외는 as-if 규칙으로             정렬 제약, 엔디언
 "이 덧셈은 16비트"                  마음대로 재배치·삭제               버스 폭, 원자성 단위
                ^                          |
                +------ 컴파일러 최적화 ---+   (-O2/-Os에서 가정 차이가 버그로 드러남)
```

Hark 같은 배터리 기기의 always-on MCU라면 SRAM이 수백 KB~수 MB이고 그 안에 RTOS, 오디오 버퍼, wake word 모델의 tensor arena가 같이 들어가야 한다(C08). 그래서 **메모리를 언제, 어디에, 얼마나 쓰는지 빌드 타임에 아는 코드**가 목표다.

---

## 1. `volatile` — 무엇을 보장하고 무엇을 보장하지 않나

### 1.1 정의

`volatile`로 한정된 객체에 대한 접근은 C 표준에서 **관찰 가능한 동작(observable behavior)**이다. 컴파일러는 소스에 적힌 횟수와 순서대로 그 접근을 수행해야 하고, 값을 레지스터에 캐싱하거나 접근을 삭제·병합할 수 없다.

### 1.2 왜 필요한가

```c
#include <stdint.h>
#include <stdbool.h>

#define UART_SR (*(volatile uint32_t *)0x40001000u)   /* 가상 주소: 상태 레지스터 */
#define UART_SR_TXE (1u << 7)                          /* 가상 비트: TX empty */

static bool g_rx_done;            /* 버그: volatile 없음 */

void UART_IRQHandler(void) { g_rx_done = true; }

void wait_rx(void)
{
    while (!g_rx_done) { }        /* -O2: 한 번만 읽고 무한 루프로 바뀔 수 있다 */
}

void wait_tx_empty(void)
{
    while ((UART_SR & UART_SR_TXE) == 0u) { }   /* volatile이라 매번 버스에서 읽는다 */
}
```

- `g_rx_done`에 `volatile`이 없으면 컴파일러는 "루프 안에서 아무도 이 값을 바꾸지 않는다"고 보고 `if (!g_rx_done) for(;;);`로 바꿔도 된다. ISR는 C 추상 기계 입장에서 "보이지 않는" 실행 흐름이기 때문이다.
- 레지스터 매크로는 반드시 `volatile` 포인터를 통해 접근한다. 상태 레지스터 폴링, read-to-clear 레지스터, FIFO 데이터 레지스터(읽을 때마다 다음 값)는 읽기 자체가 부작용이다.

### 1.3 `volatile`이 보장하지 않는 것

| 오해 | 실제 |
|---|---|
| `volatile`이면 원자적이다 | 아니다. `volatile uint32_t x; x++;`는 load, add, store 세 명령이다. 중간에 ISR가 끼면 갱신이 사라진다 |
| `volatile`이면 다른 메모리 접근과 순서가 보장된다 | volatile 접근끼리의 순서만 보장한다. 일반 변수 접근은 그 앞뒤로 재배치될 수 있다 |
| `volatile`이면 CPU/버스 순서도 보장된다 | 컴파일러 순서만이다. 버스 write buffer, 캐시, 멀티코어 순서는 `__DMB()`/`__DSB()` 같은 barrier가 필요하다 |
| 멀티스레드 동기화에 `volatile`로 충분하다 | C11/C++11에서는 `_Atomic`/`std::atomic`을 써야 data race가 UB가 아니다 |
| 64비트 `volatile` 읽기는 한 번에 된다 | Cortex-M은 32비트 머신이다. `uint64_t`는 두 번의 32비트 접근이라 중간에 값이 바뀔 수 있다 |

### 1.4 정확한 패턴: 데이터 + 플래그

```c
#include <stdint.h>
#include <stdbool.h>
#include "cmsis_compiler.h"   /* __DMB() 등 CMSIS intrinsic (CMSIS-Core 헤더) */

static uint8_t g_buf[64];              /* ISR가 채우는 데이터 (일반 변수) */
static volatile bool g_ready;          /* 발행 플래그 */

void DMA_IRQHandler(void)
{
    /* DMA가 g_buf를 이미 채웠다고 가정 */
    __DMB();                            /* 데이터 쓰기가 플래그보다 먼저 보이도록 */
    g_ready = true;
}

bool consume(uint8_t *out)
{
    if (!g_ready) {
        return false;
    }
    __DMB();                            /* 플래그를 본 뒤에 데이터를 읽도록 */
    for (int i = 0; i < 64; i++) {
        out[i] = g_buf[i];
    }
    g_ready = false;
    return true;
}
```

- 단일 코어 Cortex-M에서 ISR와 thread 사이라면 하드웨어 순서는 대부분 문제가 되지 않는다(코어는 자기 자신의 접근 순서를 program order로 관찰한다). 문제는 **컴파일러 재배치**다. `__DMB()`는 CMSIS에서 memory clobber가 있는 인라인 어셈블리라 컴파일러 barrier 역할도 한다.
- DMA나 다른 코어(듀얼 코어 MCU, SoC와 공유 메모리)가 관여하면 하드웨어 barrier가 실제로 필요하다. M7처럼 캐시가 있으면 cache maintenance까지 필요하다(C01 7.3).
- C11 방식으로는 `atomic_store_explicit(&ready, true, memory_order_release)`와 `atomic_load_explicit(&ready, memory_order_acquire)`가 같은 의도를 표준으로 표현한다.

> Don 경험과 연결: SSD FW에서 호스트 command를 DMA로 받아 completion 플래그를 세우던 구조와 같다. 멀티코어 SSD 컨트롤러(R8 여러 개)에서 공유 큐를 쓸 때 DMB를 넣던 이유를 그대로 말하면 된다.

---

## 2. `const`와 섹션 배치

### 2.1 `const`는 어디에 놓이나

```c
#include <stdint.h>

const uint16_t crc_table[256] = { 0x0000u, 0x1021u /* ... */ };  /* .rodata -> flash */
uint16_t       work_table[256] = { 0x0000u, 0x1021u };            /* .data -> flash + RAM 복사 */
static const char *const names[] = { "idle", "run" };             /* 포인터도 const -> .rodata */
static const char *names2[] = { "idle", "run" };                  /* 포인터 배열은 .data (RAM) */
```

- 전역 `const` 객체는 `.rodata`에 들어가 flash에 남는다. RAM을 쓰지 않는다. 큰 테이블(CRC, 사인 테이블, **모델 가중치**)은 반드시 `const`로 만든다.
- `const char *names2[]`는 "const char를 가리키는 포인터"의 배열이다. 포인터 자체는 수정 가능하므로 `.data`로 가서 RAM을 먹는다. 포인터까지 const로 하려면 `const char *const`.
- `const` 지역 변수는 스택에 있을 수 있다. 큰 const 테이블은 함수 안에서도 `static const`로 선언한다. 그렇지 않으면 호출할 때마다 스택에 복사하는 코드가 생길 수 있다.
- `const volatile uint32_t`는 모순이 아니다. "소프트웨어는 쓰지 않지만 하드웨어가 바꾸는" 읽기 전용 상태 레지스터를 표현한다.

### 2.2 섹션 속성

```c
#include <stdint.h>

/* 모델 가중치: 외부 QSPI flash 영역에 배치 (링커 스크립트에 .ext_flash 정의 필요) */
__attribute__((section(".ext_flash"), aligned(16)))
const int8_t g_model_weights[64 * 1024] = { 0 };

/* DMA 버퍼: 캐시 라인(32B) 정렬, 캐시 없는 SRAM 영역에 배치 */
__attribute__((section(".dma_ram"), aligned(32)))
static int16_t g_audio_pingpong[2][256];

/* 리셋 후에도 남는 crash 레코드 */
__attribute__((section(".noinit")))
static volatile uint32_t g_crash_pc;
```

- 섹션 이름은 링커 스크립트에 대응하는 출력 섹션이 있어야 의미가 있다(C01 6절).
- `aligned(32)`: Cortex-M7 D-cache 라인 크기가 32바이트다. DMA 버퍼가 다른 변수와 캐시 라인을 공유하면 invalidate 때 이웃 변수가 날아간다.
- Zephyr는 `__noinit`, `__aligned(x)`, `__dtcm_bss_section` 같은 매크로를 제공한다. FreeRTOS 프로젝트는 보통 GCC 속성을 직접 쓴다.

---

## 3. 메모리 할당 전략: 스택, 힙, 정적, pool

### 3.1 비교

| 방식 | 할당 시점 | 장점 | 단점 | 임베디드에서 |
|---|---|---|---|---|
| 정적(전역/`static`) | 링크 타임 | 크기가 map에 보임, 실패 없음, 결정적 | 유연성 없음, 동시에 안 쓰는 메모리도 점유 | **기본값** |
| 스택(자동 변수) | 함수 진입 | 빠름, 자동 해제 | 크기 초과 시 조용히 다른 메모리 파괴 | task별 스택 크기 관리 필수 |
| 힙(`malloc`) | 런타임 | 유연 | 단편화, 비결정적 시간, 실패 가능, 스레드 안전성 | 초기화 때만 허용 or 금지 (MISRA 21.3) |
| memory pool | 런타임, 고정 크기 블록 | O(1), 단편화 없음, 결정적 | 블록 크기 고정, 내부 낭비 | 메시지·패킷·이벤트 버퍼 |
| arena(bump) | 런타임, 해제는 일괄 | 매우 빠름, 오버헤드 0 | 개별 해제 불가 | TFLM tensor arena, 프레임 단위 작업 |

### 3.2 왜 힙을 피하나

```
 힙 단편화 예 (총 1KB 힙)
 t0: [A 256][B 256][C 256][D 256]
 t1: free(B), free(D)
     [A 256][ free 256 ][C 256][ free 256 ]     -> 여유 512B
 t2: malloc(400) -> 실패! 연속 400B가 없다
```

- 수개월 켜져 있는 always-on 기기에서 단편화는 "며칠 뒤 가끔 실패"로 나타난다. 재현이 가장 어려운 종류의 버그다.
- `malloc` 실행 시간은 free list 상태에 따라 달라져 ISR·실시간 경로에 쓸 수 없다.
- newlib `malloc`은 `_sbrk`와 lock hook(`__malloc_lock`)이 RTOS와 연결되어 있어야 스레드 안전하다.
- FreeRTOS는 heap_1(할당만), heap_2(구식), heap_3(malloc 래퍼), heap_4(병합), heap_5(여러 영역) 중 선택하거나 `configSUPPORT_STATIC_ALLOCATION`으로 정적 생성(`xTaskCreateStatic`)을 쓴다(C04).

### 3.3 고정 블록 memory pool

```c
/* mem_pool.h/.c 합본 - 고정 크기 블록, O(1) alloc/free, ISR-safe(critical section) */
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include "cmsis_compiler.h"     /* __get_PRIMASK, __disable_irq, __set_PRIMASK */

#define POOL_BLOCK_SIZE   64u
#define POOL_BLOCK_COUNT  32u

typedef union block {
    union block *next;                              /* free일 때: 다음 free 블록 */
    alignas(max_align_t) uint8_t data[POOL_BLOCK_SIZE];  /* 사용 중일 때: 페이로드 */
} block_t;

static block_t  s_blocks[POOL_BLOCK_COUNT];         /* 정적 저장소: map에 크기가 보인다 */
static block_t *s_free;                             /* free list head */
static uint32_t s_used, s_high_water;               /* 텔레메트리용 */

void pool_init(void)
{
    s_free = NULL;
    for (size_t i = 0; i < POOL_BLOCK_COUNT; i++) { /* 모든 블록을 free list에 연결 */
        s_blocks[i].next = s_free;
        s_free = &s_blocks[i];
    }
    s_used = 0u;
    s_high_water = 0u;
}

void *pool_alloc(void)
{
    uint32_t primask = __get_PRIMASK();             /* 이전 인터럽트 상태 저장 */
    __disable_irq();
    block_t *b = s_free;
    if (b != NULL) {
        s_free = b->next;                           /* head pop */
        if (++s_used > s_high_water) {
            s_high_water = s_used;
        }
    }
    __set_PRIMASK(primask);                         /* 중첩 호출 안전: 이전 상태로 복원 */
    return b;                                       /* 고갈되면 NULL - 호출자가 처리 */
}

void pool_free(void *p)
{
    if (p == NULL) {
        return;
    }
    block_t *b = (block_t *)p;
    /* 방어: 풀 범위 밖 포인터와 블록 경계가 아닌 포인터 거부 */
    uintptr_t off = (uintptr_t)b - (uintptr_t)s_blocks;
    if ((uintptr_t)b < (uintptr_t)s_blocks || off >= sizeof(s_blocks) || (off % sizeof(block_t)) != 0u) {
        return;                                     /* 실제 제품에선 assert/로그 */
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    b->next = s_free;                               /* head push */
    s_free = b;
    s_used--;
    __set_PRIMASK(primask);
}
```

줄 단위 설명:

- `union block`: free 상태에서는 블록 자체의 앞부분을 `next` 포인터로 재활용한다. 별도 메타데이터 메모리가 없다(intrusive free list).
- `alignas(max_align_t)`: 블록에 어떤 타입을 넣어도 정렬이 맞도록 한다. `double`/`uint64_t`는 8바이트 정렬, LDRD/STRD는 정렬되지 않으면 fault가 난다.
- `__get_PRIMASK()` 저장 후 복원: 무조건 `__enable_irq()`를 하면 이미 인터럽트가 꺼진 상태(다른 critical section 안)에서 부른 경우 인터럽트를 잘못 켜 버린다. **save/restore 패턴**이 정답이다.
- `s_high_water`: 풀 크기를 실측으로 정하기 위한 지표다. 양산 FW에서는 텔레메트리로 올려 필드 최악값을 본다.
- double free는 이 구현으로는 감지하지 못한다. 필요하면 블록별 사용 비트맵을 둔다.
- 여러 크기가 필요하면 32/64/256바이트 풀을 여러 개 두고 크기에 맞는 풀에서 꺼낸다. Zephyr `k_mem_slab`, FreeRTOS에는 직접적인 고정 블록 API가 없어 보통 이렇게 직접 만든다.

### 3.4 스택 사용량 파악

- 빌드: `-fstack-usage`로 함수별 `.su` 파일을 만들고, 호출 그래프와 합쳐 최악 경로를 계산한다(`-fcallgraph-info=su` 또는 외부 도구). 재귀와 함수 포인터가 있으면 정적 분석이 깨진다.
- 런타임: **stack painting**. 스택을 알려진 패턴(예: `0xA5A5A5A5`)으로 채워 두고, 나중에 덮이지 않은 부분을 세어 high-water mark를 얻는다. FreeRTOS `uxTaskGetStackHighWaterMark()`가 이 방식이다.
- 하드웨어: ARMv8-M의 `MSPLIM`/`PSPLIM`, 또는 MPU guard 영역(C01 7절).
- 흔한 스택 폭식: 큰 지역 배열, `printf` 계열(수백 바이트~1KB 이상), 깊은 콜백 체인, 가변 길이 배열(VLA, MISRA에서 금지).

---

## 4. 정렬, 패킹, 엔디언

### 4.1 정렬과 패딩

```c
#include <stdint.h>

struct sensor_sample {        /* offset */
    uint8_t  id;              /* 0      */
                              /* 1..3   padding (3B) */
    uint32_t timestamp;       /* 4      */
    int16_t  x;               /* 8      */
    int16_t  y;               /* 10     */
    uint8_t  flags;           /* 12     */
                              /* 13..15 tail padding: 배열에서 다음 요소 정렬 위해 */
};                            /* sizeof = 16 */

struct sensor_sample_reordered {  /* 큰 멤버부터 */
    uint32_t timestamp;       /* 0  */
    int16_t  x;               /* 4  */
    int16_t  y;               /* 6  */
    uint8_t  id;              /* 8  */
    uint8_t  flags;           /* 9  */
};                            /* sizeof = 12 (tail padding 2B) */

_Static_assert(sizeof(struct sensor_sample_reordered) == 12, "layout changed");
```

- AAPCS에서 각 기본 타입은 자기 크기로 정렬된다(`uint64_t`/`double`은 8바이트). 구조체 정렬은 멤버 중 최대 정렬이다.
- 멤버를 큰 것부터 나열하면 패딩이 줄어든다. 수천 개 샘플을 버퍼링하는 센서 허브라면 25% 메모리 절약이다.
- `_Static_assert`(C11, C23에서는 `static_assert`)로 레이아웃을 빌드 타임에 고정한다. 통신 프로토콜 구조체, 공유 메모리 구조체에 필수다.

### 4.2 `packed`의 비용

```c
#include <stdint.h>

struct __attribute__((packed)) wire_hdr {
    uint8_t  type;
    uint32_t length;    /* offset 1: 비정렬 */
    uint16_t crc;       /* offset 5 */
};                      /* sizeof = 7 */

uint32_t get_len(const struct wire_hdr *h)
{
    return h->length;   /* 컴파일러가 packed를 알고 있으므로 안전한 코드(바이트 접근 또는 비정렬 LDR)를 만든다 */
}

uint32_t get_len_bad(const struct wire_hdr *h)
{
    const uint32_t *p = &h->length;   /* GCC 경고: -Waddress-of-packed-member */
    return *p;          /* 컴파일러는 p가 4바이트 정렬이라고 가정 -> LDM/LDRD 등에서 fault 가능 */
}
```

- Cortex-M3/M4/M7은 일반 `LDR`/`STR`/`LDRH`/`STRH`의 비정렬 접근을 하드웨어가 처리한다(느리고, `SCB->CCR.UNALIGN_TRP`를 켜면 fault). `LDM`/`STM`/`LDRD`/`STRD`, exclusive 명령은 비정렬이면 항상 fault다.
- **Cortex-M0/M0+(ARMv6-M)는 비정렬 접근을 전혀 지원하지 않는다**. 바로 HardFault다. Don의 SSD M0+ 경험이 있다면 이 차이를 말할 수 있다.
- 비정렬 필드의 주소를 일반 포인터로 빼내는 순간 packed 정보가 사라진다. 이게 가장 흔한 packed 버그다.
- 더 안전한 방법: 와이어 포맷은 `uint8_t` 배열로 두고 명시적 직렬화 함수로 읽고 쓴다(아래 4.3). 이러면 정렬과 엔디언 문제가 한 번에 해결된다.

### 4.3 엔디언

```
 uint32_t v = 0x11223344 를 주소 0x20000000에 저장

 Little-endian (Cortex-M 기본):   0x20000000: 44 33 22 11
 Big-endian (네트워크 바이트 순서): 0x20000000: 11 22 33 44
```

Cortex-M은 사실상 모두 little-endian이다(아키텍처상 BE8 선택이 가능하지만 구현 선택이며 드물다). 네트워크 프로토콜, 일부 센서 레지스터(예: 16비트 값을 MSB 먼저 보내는 I2C 센서 다수), BLE는 little-endian이지만 IP 계열은 big-endian이다.

```c
#include <stdint.h>
#include <stddef.h>

/* 엔디언 독립 직렬화: 호스트 엔디언과 정렬에 무관하게 동작 */
static inline uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline void store_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/* I2C 센서가 [MSB, LSB] 순으로 보내는 16비트 부호 있는 값 */
static inline int16_t sensor_raw16(const uint8_t *rx)
{
    return (int16_t)(uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
}
```

- `(uint32_t)p[0] << 24`: 캐스팅 없이 `p[0] << 24`를 하면 `p[0]`이 `int`로 승격된 뒤 시프트되어 `0x80` 이상 값에서 부호 비트로 들어간다. C에서 그것은 UB다(5절).
- GCC는 이 패턴을 인식해 `REV` 명령 한두 개로 최적화한다. CMSIS `__REV()`, `__REV16()`, `__REVSH()`를 직접 써도 된다.
- `sensor_raw16`: `uint16_t`로 조립한 뒤 `int16_t`로 바꾼다. 범위를 넘는 unsigned→signed 변환은 C17까지 구현 정의(implementation-defined)이고 GCC는 2의 보수로 정의한다. C23부터는 2의 보수가 표준이다.

---

## 5. 정수 승격과 산술 함정

### 5.1 규칙 요약

- **정수 승격(integer promotion)**: `int`보다 작은 타입(`char`, `short`, `uint8_t`, `uint16_t`, `bool`)은 산술 연산 전에 `int`로 바뀐다(값을 모두 표현할 수 있으면). Cortex-M에서 `int`는 32비트다.
- **일반 산술 변환(usual arithmetic conversions)**: 두 피연산자 타입이 다르면 공통 타입으로 맞춘다. 같은 크기에서 signed와 unsigned가 만나면 **unsigned가 이긴다**.
- signed 오버플로는 **UB**, unsigned는 모듈러 wrap-around로 정의되어 있다.

### 5.2 함정 모음

```c
#include <stdint.h>
#include <stdbool.h>

void pitfalls(void)
{
    uint8_t a = 200u, b = 100u;
    uint8_t sum8 = a + b;              /* (1) int 300 -> uint8_t 44로 잘림. 경고는 -Wconversion에서만 */

    uint8_t mask = 0x0Fu;
    bool eq = ((uint8_t)~mask == 0xF0u); /* (2) ~mask는 int 0xFFFFFFF0. 캐스트 없으면 비교가 false */

    int32_t  neg = -1;
    uint32_t one = 1u;
    bool lt = (neg < one);             /* (3) neg가 uint32_t 0xFFFFFFFF로 변환 -> false */

    uint16_t x = 0xFFFFu;
    uint32_t sq = (uint32_t)x * x;     /* (4) 캐스트 없이 x * x면 int 곱: 65535*65535 > INT_MAX -> UB */

    uint32_t bit31 = 1u << 31;         /* (5) 1 << 31은 int 시프트로 부호 비트 침범 -> UB */

    uint32_t t_now = 5u, t_start = 0xFFFFFFF0u;
    uint32_t elapsed = t_now - t_start; /* (6) unsigned wrap이 정확한 경과 시간(21)을 준다 */

    (void)sum8; (void)eq; (void)lt; (void)sq; (void)bit31; (void)elapsed;
}
```

1. `a + b`는 `int` 300이다. `uint8_t`에 넣으면 44가 된다. 체크섬 계산에서 의도한 동작일 수도 있지만 대부분 버그다.
2. `~mask`는 `int`로 승격된 뒤 반전되어 `0xFFFFFFF0`이 된다. 레지스터 비트 클리어 `reg &= ~mask` 에서 mask가 `uint8_t`면 대부분 괜찮지만 비교나 시프트와 섞이면 틀린다.
3. 가장 유명한 함정. `-Wsign-compare`(`-Wextra`에 포함)로 잡힌다.
4. `uint16_t` 두 개의 곱이 `int`로 승격되어 signed overflow UB가 된다. "unsigned끼리 곱했는데 UB"라는 반직관적 사례로 면접에서 자주 나온다.
5. 레지스터 비트 매크로는 항상 `1u << n` 또는 `UINT32_C(1) << n`. CMSIS도 `(1UL << n)`을 쓴다.
6. **타이머 wrap-around 비교의 정답 패턴**. `if ((now - start) >= timeout)`은 카운터가 한 번 넘쳐도 맞다. `if (now >= start + timeout)`은 틀린다.

### 5.3 나눗셈과 시프트

- `int` 음수 나눗셈은 0 방향으로 버림(C99부터). `-7 / 2 == -3`. 반면 산술 시프트 `-7 >> 1`은 구현 정의(GCC는 산술 시프트, 결과 -4). 부호 있는 값을 시프트로 나누면 반올림 방향이 다르다.
- Cortex-M0/M0+에는 하드웨어 나눗셈 명령이 없다. `/`와 `%`가 `__aeabi_uidiv` 같은 라이브러리 호출(수십 사이클)이 된다. M3 이상은 `UDIV`/`SDIV`가 있다(2~12 사이클). 2의 거듭제곱으로 설계하면 시프트와 마스크로 바뀐다(링 버퍼 크기를 2의 거듭제곱으로 하는 이유).

---

## 6. Fixed-point 연산

### 6.1 왜 쓰나

FPU가 없는 코어(M0+, M3)에서 float은 소프트웨어 에뮬레이션이라 수십~수백 사이클이다. FPU가 있어도(M4F) 오디오 DSP·ML 커널은 **SIMD 정수 명령**(`SMLAD` 등)으로 한 번에 두 개의 16비트 MAC을 하는 fixed-point가 더 빠르고 메모리도 절반이다. INT8 양자화 ML 추론(C08)도 본질적으로 fixed-point 스케일 연산이다.

### 6.2 Q 포맷

```
 Q15 (int16_t): 부호 1비트 + 소수 15비트, 범위 [-1.0, 1.0 - 2^-15]
   값 = raw / 32768.   0x4000 = 0.5,  0x8000 = -1.0,  0x7FFF = 0.99997
 Q31 (int32_t): 부호 1비트 + 소수 31비트, 범위 [-1.0, 1.0)
 Q16.16 (int32_t): 정수 16비트(부호 포함) + 소수 16비트, 범위 [-32768, 32768)

 Q15 x Q15 곱:
   int16 * int16 = int32 (Q30)  --(>> 15)-->  Q15
   예외: -1.0 * -1.0 = +1.0 은 Q15로 표현 불가 -> 포화(saturate) 필요
```

### 6.3 코드

```c
#include <stdint.h>

typedef int16_t q15_t;
typedef int32_t q31_t;

static inline q15_t q15_sat(int32_t v)
{
    if (v > INT16_MAX) return INT16_MAX;       /* Cortex-M4: __SSAT(v, 16) 한 명령 */
    if (v < INT16_MIN) return INT16_MIN;
    return (q15_t)v;
}

static inline q15_t q15_mul(q15_t a, q15_t b)
{
    int32_t p = (int32_t)a * (int32_t)b;       /* Q30, 오버플로 없음 (최대 2^30) */
    p += (1 << 14);                            /* 반올림: 버리기 전 0.5 LSB 더하기 */
    return q15_sat(p >> 15);                   /* Q15로 복귀 + -1*-1 포화 */
}

static inline q15_t q15_add(q15_t a, q15_t b)
{
    return q15_sat((int32_t)a + (int32_t)b);   /* 오디오에서 wrap은 딸깍 소리, 포화는 클리핑 */
}

/* Q16.16 곱: 64비트 중간값이 필요하다 */
typedef int32_t q16_16_t;
static inline q16_16_t q16_mul(q16_16_t a, q16_16_t b)
{
    int64_t p = (int64_t)a * (int64_t)b;       /* Cortex-M3/M4: SMULL 한 명령 */
    return (q16_16_t)((p + (1 << 15)) >> 16);  /* 음수 >>는 GCC에서 산술 시프트 */
}

/* 1차 IIR 저역통과 (지수 이동 평균): y += alpha * (x - y), alpha는 Q15 */
static inline q15_t ema_q15(q15_t y, q15_t x, q15_t alpha)
{
    return q15_add(y, q15_mul(alpha, q15_sat((int32_t)x - y)));
}
```

- `(int32_t)a * (int32_t)b`: 캐스팅은 가독성과 의도 표현이다(승격으로 어차피 `int`가 되지만, 64비트 버전과 일관되게 둔다).
- `+= (1 << 14)`: 버림(truncation)만 하면 음의 방향으로 바이어스가 누적된다. 필터 누적에서 DC 오프셋이 생기는 원인이다.
- `x - y`가 Q15 범위를 넘을 수 있으므로 한 번 더 포화한다.
- CMSIS-DSP는 `arm_mult_q15()`, `arm_fir_q15()` 같은 최적화 함수와 `q15_t`, `q31_t`, `q7_t` 타입을 제공한다. CMSIS-Core intrinsic `__SSAT()`, `__QADD()`, `__SMLAD()`(ARMv7E-M)로 포화·SIMD를 직접 쓸 수도 있다.

### 6.4 흔한 함정

- Q 포맷이 다른 값을 그냥 더한다(Q15 + Q12). 스케일을 맞춰야 한다.
- 누적기 비트 부족. 256개 Q15 곱을 누적하면 Q30 값 256개 = 최대 2^38 → `int64_t` 누적기가 필요하다(CMSIS-DSP의 q15 dot product도 64비트 누적).
- 음수 오른쪽 시프트를 나눗셈과 동일시한다(반올림 방향 차이).

---

## 7. Cortex-M 원자성과 critical section

### 7.1 무엇이 원자적인가

| 접근 | Cortex-M (단일 코어) |
|---|---|
| 정렬된 8/16/32비트 load 또는 store 한 번 | 원자적 (single-copy atomic) |
| 비정렬 접근 | 원자적이지 않음 (여러 버스 트랜잭션) |
| 64비트 `uint64_t` 읽기/쓰기 | 원자적이지 않음 (`LDRD`/`LDM`는 중간에 인터럽트될 수 있고 재시작 또는 계속 동작) |
| read-modify-write (`x++`, 비트 OR 대입 같은 코드) | 원자적이지 않음 |
| 비트밴드 쓰기 (M3/M4 선택) | 원자적 한 비트 쓰기 |
| NVIC ISER/ICER, GPIO BSRR 류 set/clear 레지스터 | 하드웨어 설계로 RMW 불필요 |

### 7.2 lost update

```
 Thread (x++ 컴파일 결과)          ISR (x++)
 LDR r0, [x]      ; r0 = 5
       <---------- 인터럽트 ---->  LDR r1,[x] ; 5
                                   ADD r1,#1  ; 6
                                   STR r1,[x] ; x = 6
       <---------- 복귀 ---------
 ADD r0, #1       ; r0 = 6
 STR r0, [x]      ; x = 6   <- ISR의 증가분이 사라짐 (정답 7)
```

### 7.3 해결 1: critical section (인터럽트 마스크)

```c
#include <stdint.h>
#include "cmsis_compiler.h"

static inline uint32_t irq_lock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();                 /* CPSID i. 이 명령 이후 인터럽트가 들어오지 않음 (추가 barrier 불필요) */
    return primask;
}

static inline void irq_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);          /* 원래 상태 복원 (중첩 안전) */
}

volatile uint32_t g_event_count;

void count_event(void)
{
    uint32_t key = irq_lock();
    g_event_count++;
    irq_unlock(key);
}
```

- Zephyr는 `irq_lock()`/`irq_unlock(key)`라는 같은 모양의 API를 제공한다(ARMv7-M에서는 BASEPRI 기반). FreeRTOS는 `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`, ISR 안에서는 `taskENTER_CRITICAL_FROM_ISR()`가 반환값을 저장한다.
- critical section은 **짧게**. 인터럽트 지연 = 가장 긴 critical section 길이다. 오디오 I2S DMA 인터럽트를 놓치면 소리가 끊긴다.
- 멀티코어(듀얼 코어 MCU, SoC와 공유 메모리)에서는 인터럽트 마스크로 부족하다. 하드웨어 세마포어/mailbox나 exclusive access가 필요하다(벤더마다 다름).

### 7.4 해결 2: LDREX/STREX (lock-free)

ARMv7-M, ARMv8-M(Baseline 포함)은 **exclusive access** 명령을 제공한다. ARMv6-M(M0/M0+)에는 없다.

```
 retry:
   LDREX r0, [x]        ; x를 읽고 local exclusive monitor를 "Exclusive" 상태로
   ADD   r0, r0, #1
   STREX r1, r0, [x]    ; monitor가 아직 Exclusive면 저장 + r1=0, 아니면 저장 안 함 + r1=1
   CMP   r1, #0
   BNE   retry          ; 실패하면 처음부터 다시
```

- 그 사이에 예외(인터럽트)가 발생하면 Cortex-M은 예외 진입/복귀 과정에서 local monitor를 클리어하므로 STREX가 실패하고 재시도한다. 그래서 ISR가 끼어들어도 lost update가 생기지 않는다. 인터럽트를 끄지 않으므로 지연이 늘지 않는다.

```c
#include <stdint.h>
#include "cmsis_compiler.h"   /* __LDREXW, __STREXW (ARMv7-M / ARMv8-M) */

static inline uint32_t atomic_add_u32(volatile uint32_t *p, uint32_t v)
{
    uint32_t newv;
    do {
        newv = __LDREXW(p) + v;            /* exclusive load */
    } while (__STREXW(newv, p) != 0u);     /* 0 = 성공, 1 = 다른 접근이 끼어듦 -> 재시도 */
    return newv;
}
```

- C11 표준 방식: `#include <stdatomic.h>` 후 `atomic_fetch_add(&counter, 1)`. GCC는 M3/M4/M33에서 이것을 LDREX/STREX 루프로 인라인한다. **M0+에서는 lock-free 명령이 없어 `__atomic_fetch_add_4` 같은 라이브러리 함수 호출이 생성되고, 링크 에러가 나거나 직접 구현(인터럽트 마스크)을 제공해야 한다**.
- LDREX와 STREX 사이에는 다른 메모리 접근을 최소로 한다. 그 사이 코드가 길면 인터럽트가 잦을 때 계속 실패한다(livelock 가능성).

### 7.5 ISR–thread 공유의 정석: SPSC 링 버퍼

한 생산자(ISR)와 한 소비자(task)라면 **lock 없이** 인덱스 분리만으로 안전하다. 생산자만 `head`를 쓰고 소비자만 `tail`을 쓰며, 각 인덱스 갱신이 정렬된 32비트 store 한 번(원자적)이기 때문이다. 자세한 코드와 변형은 `study/S01_c_coding_drills.md`에서 다룬다. 핵심은 "데이터를 쓴 뒤 barrier, 그다음 인덱스 발행"이다.

### 7.6 선택 가이드

| 상황 | 추천 |
|---|---|
| ISR ↔ task, 1:1 스트림 | SPSC 링 버퍼 (lock-free) |
| 카운터·플래그 비트 갱신 | `stdatomic` 또는 LDREX/STREX (M0+는 짧은 critical section) |
| 여러 필드를 일관되게 갱신 | critical section (또는 RTOS mutex, ISR 관여 안 할 때) |
| 긴 작업, task끼리만 공유 | RTOS mutex (priority inheritance, C04) |
| 코어 간 공유 | 하드웨어 세마포어/IPC mailbox, cache 고려 |

---

## 8. 임베디드 C++

### 8.1 무엇을 끄고 무엇을 쓰나

| 기능 | 비용 | 임베디드 관행 |
|---|---|---|
| 예외 (`throw`) | unwind 테이블(`.ARM.exidx`, `.ARM.extab`), 런타임 코드 수~수십 KB, 비결정적 시간 | `-fno-exceptions`. 에러는 반환값/`expected` 스타일 |
| RTTI (`dynamic_cast`, `typeid`) | 클래스마다 type_info, 문자열 | `-fno-rtti` |
| 가상 함수 | 객체마다 vptr 4B, 클래스마다 vtable(flash), 간접 호출 | 허용. 인터페이스 추상화 비용으로 수용 가능 |
| 템플릿 | 인스턴스마다 코드 복제(code bloat) | 허용하되 map으로 크기 확인 |
| `constexpr` | 0 (컴파일 타임 계산) | 적극 사용 |
| RAII | 0에 가까움 (인라인) | 적극 사용: lock guard, critical section, 전원 도메인 |
| `new`/`delete`, STL 컨테이너 | 힙 사용 | 금지 또는 초기화 때만. `std::array`, 정적 pool, placement new |
| `std::function` | 크기 초과 캡처 시 힙 할당 | 피하거나 함수 포인터 + context, 고정 크기 delegate |
| iostream | 수백 KB | 금지 |
| 함수 지역 `static` 객체 | 스레드 안전 초기화 guard(`__cxa_guard_acquire`) | `-fno-threadsafe-statics` 또는 전역으로 |

빌드 옵션 예: `-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit`. `-fno-use-cxa-atexit`는 종료 시 소멸자 등록을 줄인다(펌웨어는 `main`에서 반환하지 않으므로 전역 소멸자가 필요 없다).

### 8.2 RAII: critical section guard

```cpp
#include <cstdint>
#include "cmsis_compiler.h"

class IrqLock {
public:
    IrqLock() : primask_(__get_PRIMASK()) { __disable_irq(); }
    ~IrqLock() { __set_PRIMASK(primask_); }
    IrqLock(const IrqLock&) = delete;             // 복사 금지: 두 번 복원되는 사고 방지
    IrqLock& operator=(const IrqLock&) = delete;
private:
    std::uint32_t primask_;
};

volatile std::uint32_t g_events;

bool try_consume()
{
    IrqLock lock;                 // 생성자에서 인터럽트 끔
    if (g_events == 0u) {
        return false;             // 어느 경로로 나가도 소멸자가 복원
    }
    g_events = g_events - 1u;
    return true;
}
```

- C의 `irq_lock/irq_unlock`은 early return 경로에서 unlock을 빠뜨리기 쉽다. RAII는 스코프를 벗어나는 모든 경로에서 소멸자를 보장한다. 이것이 임베디드 C++의 가장 큰 실익이다.
- `-O2`에서 이 클래스는 C 버전과 같은 기계어가 된다(추가 비용 0). objdump로 확인할 수 있다.

### 8.3 `constexpr`로 컴파일 타임 계산

```cpp
#include <array>
#include <cstdint>

constexpr std::uint16_t crc16_step(std::uint16_t crc)
{
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 0x8000u) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u)
                              : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

constexpr std::array<std::uint16_t, 256> make_crc_table()
{
    std::array<std::uint16_t, 256> t{};
    for (unsigned i = 0; i < 256; ++i) {
        t[i] = crc16_step(static_cast<std::uint16_t>(i << 8));
    }
    return t;
}

constexpr auto kCrcTable = make_crc_table();       // 컴파일 타임 생성 -> .rodata (flash)
static_assert(kCrcTable[1] == 0x1021u, "CRC-16/CCITT table");

constexpr std::uint32_t kSysClk  = 64'000'000u;
constexpr std::uint32_t kBaud    = 115'200u;
constexpr std::uint32_t kBaudDiv = (kSysClk + kBaud / 2u) / kBaud;   // 반올림 나눗셈
static_assert(kBaudDiv > 0u && kBaudDiv < 65536u, "baud divider out of range");
```

- 테이블을 손으로 붙여 넣는 대신 컴파일러가 만들고 `static_assert`로 검증한다. 런타임 초기화 코드도 RAM도 필요 없다(C++17 이상에서 `std::array`의 constexpr 연산 사용).
- 클럭·분주비 계산을 `static_assert`로 빌드 타임에 범위 검사하면 잘못된 보드 설정이 컴파일 에러가 된다.

### 8.4 placement new와 정적 저장소

```cpp
#include <new>          // placement new
#include <cstddef>
#include <cstdint>

class AudioPipeline {
public:
    explicit AudioPipeline(std::uint32_t rate) : rate_(rate) {}
    std::uint32_t rate() const { return rate_; }
private:
    std::uint32_t rate_;
};

alignas(AudioPipeline) static std::byte s_pipe_storage[sizeof(AudioPipeline)];
static AudioPipeline* s_pipe = nullptr;

void pipeline_start(std::uint32_t rate)
{
    s_pipe = new (s_pipe_storage) AudioPipeline(rate);   // 힙 없이 원하는 시점에 생성
}

void pipeline_stop()
{
    if (s_pipe != nullptr) {
        s_pipe->~AudioPipeline();                         // 소멸자는 명시 호출
        s_pipe = nullptr;
    }
}
```

- placement new는 메모리를 할당하지 않고 **주어진 주소에서 생성자만 실행**한다. 저장소는 정적이므로 map에 크기가 보인다.
- `alignas(AudioPipeline)`: 저장소 정렬을 객체 정렬에 맞춘다. 빠뜨리면 M0+에서 fault가 난다.
- 생성 시점을 제어할 수 있어서 "클럭·전원 설정 뒤에 드라이버 객체 생성" 순서를 지킬 수 있다(8.5의 정적 초기화 순서 문제 회피). 엄격하게는 C++17 `std::launder` 논의가 있지만, 반환된 포인터 `s_pipe`를 쓰면 문제없다.
- Zephyr/ETL(Embedded Template Library) 같은 라이브러리는 이런 고정 용량 컨테이너(`etl::vector<T, N>`)를 제공한다.

### 8.5 정적 초기화 순서 문제 (static initialization order fiasco)

```
 uart.cpp:   Uart g_uart(115200);            // 생성자: 클럭 트리 객체를 참조
 clock.cpp:  ClockTree g_clock;              // 생성자: PLL 설정
 -> 서로 다른 번역 단위의 전역 객체 생성 순서는 표준이 정하지 않는다.
    g_uart가 먼저 생성되면 아직 생성되지 않은 g_clock을 사용한다 (링크 순서에 따라 달라짐).
```

해결책:

1. 전역 객체의 생성자는 **하드웨어를 건드리지 않는다**. 멤버 초기화만 하고, 실제 초기화는 `main()`에서 명시적 `init()` 호출 순서로.
2. construct-on-first-use: 함수 안 `static` 객체 반환. 단, 스레드 안전 guard 비용이 생기고(`-fno-threadsafe-statics`면 없음) 첫 호출이 ISR에서 일어나면 위험하다.
3. `constexpr` 생성자(`constinit`, C++20)로 컴파일 타임 초기화를 강제하면 순서 문제가 원천적으로 없다.

### 8.6 템플릿 비용 관리

- `RingBuffer<uint8_t, 64>`, `RingBuffer<uint8_t, 128>`, `RingBuffer<int16_t, 256>`은 **세 벌의 코드**다. 크기만 다른 인스턴스가 많으면 공통 로직을 크기를 인자로 받는 비템플릿 베이스 클래스로 빼고 템플릿은 저장소만 담당하게 한다(thin template).
- `arm-none-eabi-nm -C --size-sort`로 템플릿 인스턴스 크기를 확인한다. `-flto`와 `--icf`(identical code folding, 링커에 따라 지원)도 도움이 된다.

---

## 9. MISRA 개요

### 9.1 무엇인가

MISRA C는 자동차 산업에서 시작된 **C 코딩 가이드라인**으로, 정의되지 않은 동작과 구현 정의 동작, 오해하기 쉬운 구조를 피하게 한다. 현재 판은 MISRA C:2012(Amendment 1~4 포함)와 이를 통합한 MISRA C:2023, C++ 쪽은 MISRA C++:2023(AUTOSAR C++14 지침 통합)이다. 컨슈머 기기에서는 강제가 아니지만 **양산 펌웨어 품질 기준으로 부분 채택**하는 회사가 많다.

| 분류 | 의미 |
|---|---|
| Mandatory | 반드시 지킴, deviation 불가 |
| Required | 지켜야 함, 공식 deviation 절차(근거 문서화)로 예외 가능 |
| Advisory | 권고, 지키지 않아도 기록 수준 |
| Directive vs Rule | Directive는 프로세스·문서 요구(정적 분석으로 완전 판정 어려움), Rule은 코드로 판정 가능 |

### 9.2 대표 규칙 (MISRA C:2012 기준)

| 규칙 | 내용 | 이유 |
|---|---|---|
| Rule 21.3 (Required) | `<stdlib.h>`의 메모리 할당·해제 함수 사용 금지 | 단편화, 비결정성 (3절) |
| Rule 17.2 (Required) | 직접·간접 재귀 금지 | 스택 사용량을 정적으로 알 수 없음 |
| Rule 15.1 (Advisory) | `goto` 사용 금지 | 제어 흐름 가독성 |
| Rule 10.x (Essential type model) | 본질적 타입이 다른 값 사이 암묵 변환·연산 제한 | 5절의 승격 함정 방지 |
| Rule 18.8 (Required) | 가변 길이 배열(VLA) 금지 | 스택 폭발 |
| Rule 17.7 (Required) | 반환값이 있는 함수의 반환값을 무시하지 말 것 (의도적이면 `(void)` 캐스트) | 에러 무시 방지 |

- 정적 분석 도구: cppcheck(MISRA addon, 규칙 텍스트는 사용자가 라이선스 문서에서 제공), PC-lint Plus, Polyspace, LDRA, Parasoft C/C++test, Helix QAC.
- 면접 포인트: "MISRA를 전부 지키면 좋은 코드"가 아니라 **"UB를 피하고 리뷰 비용을 줄이는 도구로 부분 채택, deviation은 문서화"**라고 말하는 것이 실무적이다.

> Don 경험과 연결: 엔터프라이즈 SSD FW도 코딩 표준과 정적 분석(Coverity 등)을 양산 게이트로 쓴다. "우리 팀은 malloc 금지·재귀 금지·정적 분석 경고 0을 릴리스 조건으로 뒀다" 같은 구체적 경험이 있으면 그대로 말한다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR 공유 플래그에 `volatile` 누락 | `-O0`에선 되고 `-Os`에선 무한 대기 | 컴파일러가 값을 레지스터에 캐싱 | `volatile` 또는 `_Atomic`, 릴리스 옵션으로 테스트 |
| `volatile`을 원자성으로 착각 | 카운터가 가끔 덜 증가 | RMW 중 ISR 끼어듦 | critical section, LDREX/STREX, `stdatomic` |
| `1 << 31` 비트 매크로 | 최적화 따라 이상한 비교 결과 | signed 시프트 UB | `1u << 31`, `UINT32_C(1)` |
| `uint16_t` 곱셈 오버플로 | 드물게 틀린 결과, UBSan 경고 | `int` 승격 후 signed overflow | 한쪽을 `uint32_t`로 캐스팅 |
| signed/unsigned 비교 | 음수 에러코드가 "크다"로 판정 | usual arithmetic conversion | `-Wextra -Wsign-compare`, 타입 통일 |
| packed 멤버 주소를 포인터로 | M0+ HardFault, M4 LDRD fault | 정렬 가정 깨짐 | 바이트 직렬화 함수, `-Waddress-of-packed-member` |
| 큰 지역 배열/printf | 간헐적 전역 변수 오염, HardFault | 스택 오버플로 | `-fstack-usage`, painting, MSPLIM/MPU guard |
| 런타임 `malloc` | 며칠 후 할당 실패, 지연 스파이크 | 단편화 | 정적/pool/arena, 초기화 때만 할당 |
| critical section에서 무조건 `__enable_irq()` | 중첩 호출 시 보호 구간이 풀림 | 이전 상태 미보존 | PRIMASK save/restore |
| 전역 C++ 객체 생성자에서 HW 초기화 | 링크 순서 바뀌면 부팅 실패 | 정적 초기화 순서 미정 | 명시적 `init()`, `constinit`, placement new |
| Q15 곱 후 포화 누락 | 오디오 클릭 노이즈, 필터 발산 | -1 x -1, 누적 오버플로 | `__SSAT`, 64비트 누적기 |

---

## 11. 면접에서 이렇게 말한다

**Q.** What does `volatile` guarantee, and what doesn't it?

**A.** 컴파일러가 해당 접근을 삭제·병합·캐싱하지 않고 소스 순서대로 수행한다는 것만 보장한다. 원자성, 일반 변수와의 순서, 하드웨어/멀티코어 메모리 순서는 보장하지 않는다. 레지스터와 ISR 공유 플래그에 쓰고, 동기화는 atomics·barrier·critical section으로 한다.

English: "`volatile` tells the compiler every access is observable, so it can't cache the value in a register, merge or drop accesses, or reorder them relative to other volatile accesses. It does not make read-modify-write atomic, it doesn't order ordinary memory accesses around it, and it emits no hardware barrier. I use it for memory-mapped registers and ISR flags, and I use critical sections, C11 atomics or `__DMB` for actual synchronization."

**Q.** How do you make a counter shared between an ISR and a task safe on a Cortex-M?

**A.** 한 명만 쓰고 정렬된 32비트면 읽기는 원자적이라 그대로 된다. 둘 다 증가시키면 RMW라 보호가 필요하다. M3/M4/M33은 LDREX/STREX 루프나 `atomic_fetch_add`, M0+는 PRIMASK save/restore critical section. critical section은 짧게 유지한다.

English: "If only one side writes and the variable is an aligned 32-bit word, a single load or store is atomic, so it's fine with `volatile`. If both sides modify it, the increment is a load-add-store sequence, so I either use an exclusive-access loop, LDREX and STREX, which C11 `atomic_fetch_add` compiles to on an M3 or M4, or on an M0+ which has no exclusives, a short critical section that saves and restores PRIMASK."

**Q.** Why avoid `malloc` in firmware? What do you use instead?

**A.** 단편화로 장시간 후 실패, 비결정적 실행 시간, 스레드 안전성 문제. 대신 정적 할당을 기본으로, 가변 개수 객체는 고정 블록 pool, 수명이 같은 묶음은 arena를 쓴다. high-water mark를 텔레메트리로 수집해 크기를 정한다.

English: "On a device that runs for months, heap fragmentation turns into a failure that shows up after days and is nearly impossible to reproduce, and allocation time isn't bounded. So the default is static allocation that I can see in the map file, fixed-block pools for things like messages and packets with O(1) alloc and free, and arenas for groups with the same lifetime, like the TFLite Micro tensor arena. I track pool high-water marks in telemetry to size them."

**Q.** What's wrong with `uint16_t a = 0xFFFF; uint32_t r = a * a;`?

**A.** 두 `uint16_t`가 `int`로 승격된 뒤 곱해져 `int` 범위를 넘으므로 signed overflow UB다. 한쪽을 `uint32_t`로 캐스팅해야 한다.

English: "Both operands are promoted to `int` before the multiply, so it's a signed 32-bit multiplication of 65535 by 65535, which overflows `int`, and signed overflow is undefined behavior. Casting one operand to `uint32_t` makes it a well-defined unsigned multiply."

**Q.** Would you use C++ on a microcontroller? Which features?

**A.** 쓴다. 예외와 RTTI는 끄고, 힙을 쓰는 STL과 iostream은 금지. RAII(lock guard), constexpr, `std::array`, 가벼운 템플릿, 인터페이스용 가상 함수는 비용 대비 이득이 크다. 전역 객체 생성자에서 하드웨어 초기화 안 하기, 템플릿 bloat는 map으로 확인.

English: "Yes, with a clear subset. I build with `-fno-exceptions` and `-fno-rtti`, avoid anything that allocates, like most STL containers and `std::function`, and never use iostream. What I do use is RAII for locks and resource ownership, `constexpr` and `static_assert` to move computation and checks to compile time, `std::array`, virtual interfaces for drivers where the indirection is worth it, and templates carefully, checking the map file for bloat. I also keep hardware out of global constructors to avoid the static initialization order problem."

**Q.** How do you represent 0.5 in Q15, and how do you multiply two Q15 numbers?

**A.** 0.5는 `0x4000`(16384). 곱은 32비트 중간값(Q30)을 만들고 반올림 상수 `1 << 14`를 더한 뒤 15비트 오른쪽 시프트, -1 x -1 경우를 위해 포화한다.

English: "Q15 has one sign bit and fifteen fractional bits, so 0.5 is 16384, hex 4000. To multiply, I do a 16 by 16 to 32-bit multiply, which gives Q30, add half an LSB for rounding, shift right by fifteen and saturate, because minus one times minus one is plus one, which doesn't fit in Q15. On an M4 the saturation is a single `SSAT` instruction, and CMSIS-DSP has optimized versions."

**Q.** A struct is `packed` for a wire protocol. What can go wrong?

**A.** 비정렬 멤버 접근이 느려지고, 멤버 주소를 일반 포인터로 넘기면 정렬 가정이 깨져 M0+에선 바로 HardFault, M4에서도 LDRD/LDM에서 fault. 엔디언도 호스트에 종속된다. 바이트 배열 + 명시적 직렬화 함수가 더 안전하다.

English: "Packed members can be misaligned, so access is slower, and if you take the address of a member and pass it as a normal pointer, the compiler assumes alignment again. On an M0+ that's an immediate HardFault, and even on an M4 multi-word loads fault. The layout is also tied to the host's endianness. I prefer keeping the wire format as a byte array and using explicit load and store helpers, with `static_assert` on any struct that has to match a fixed layout."

---

## 12. 직접 해보기

### 실습 1. volatile 누락을 어셈블리로 확인

```sh
cat > vol.c <<'EOF'
#include <stdbool.h>
bool flag;              /* volatile 없음 */
void wait(void) { while (!flag) { } }
EOF
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -S vol.c -o - | sed -n '/wait:/,/size/p'
# 결과: flag를 한 번 읽고 "b ." 같은 무한 루프로 바뀐 것을 확인
# bool flag -> volatile bool flag 로 바꿔 다시 비교
```

툴체인이 없다면 [Compiler Explorer](https://godbolt.org)에서 컴파일러를 "ARM GCC (none)"로 고르고 `-mcpu=cortex-m4 -O2`로 같은 실험을 한다.

### 실습 2. 정수 승격 함정을 UBSan으로 잡기 (호스트)

```sh
cat > promo.c <<'EOF'
#include <stdint.h>
#include <stdio.h>
int main(void) {
    volatile uint16_t x = 0xFFFFu;
    uint32_t r = x * x;               /* int 곱 -> signed overflow */
    int32_t neg = -1; uint32_t one = 1u;
    printf("r=%u  (-1 < 1u) = %d\n", (unsigned)r, neg < one);
    return 0;
}
EOF
gcc -O2 -Wall -Wextra -Wconversion -fsanitize=undefined promo.c -o promo && ./promo
# UBSan: "signed integer overflow: 65535 * 65535 cannot be represented in type 'int'"
# 컴파일 경고: comparison of integer expressions of different signedness
```

호스트 `int`도 32비트라 Cortex-M과 같은 승격 결과를 재현할 수 있다. 펌웨어 로직을 호스트 단위 테스트(Unity, CppUTest, GoogleTest)로 돌리면서 UBSan/ASan을 켜는 것은 실무에서도 강력한 기법이다.

### 실습 3. C++ 기능별 크기 측정

```sh
cat > raii.cpp <<'EOF'
#include <cstdint>
struct Guard { Guard(){ asm volatile("cpsid i" ::: "memory"); } ~Guard(){ asm volatile("cpsie i" ::: "memory"); } };
volatile std::uint32_t g;
void inc() { Guard lock; g = g + 1u; }
EOF
arm-none-eabi-g++ -mcpu=cortex-m4 -mthumb -Os -fno-exceptions -fno-rtti -c raii.cpp -o a.o
arm-none-eabi-g++ -mcpu=cortex-m4 -mthumb -Os -c raii.cpp -o b.o     # 예외 켠 상태
arm-none-eabi-size a.o b.o
arm-none-eabi-objdump -d a.o          # Guard가 cpsid/cpsie 두 명령으로 사라졌는지 확인
arm-none-eabi-objdump -h b.o | grep -E "exidx|extab"
```

(이 실습의 Guard는 크기 비교용으로 PRIMASK를 저장하지 않는 단순 버전이다. 실제 코드는 8.2의 save/restore 버전을 쓴다.)

### 실습 4. MISRA 검사 맛보기

```sh
cppcheck --addon=misra --enable=style mem_pool.c 2>&1 | head -40
# 규칙 번호는 나오지만 규칙 텍스트는 MISRA 라이선스 문서가 있어야 표시된다(--rule-texts 옵션용 파일 필요)
```

3.3절의 memory pool 코드를 파일로 저장해 돌려 보고, 어떤 규칙(예: 포인터 캐스팅 관련 11.x, 조기 return 관련 15.5)에 걸리는지 확인한다. 어떤 것을 고치고 어떤 것을 deviation으로 남길지 스스로 판단해 본다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Abstract machine | C 추상 기계 | 표준이 정의한 가상 실행 모델, 컴파일러는 관찰 가능한 동작만 보존 |
| Observable behavior | 관찰 가능한 동작 | volatile 접근, 파일 I/O 등 |
| UB | Undefined Behavior | 표준이 아무 요구도 하지 않음, 최적화가 무엇이든 할 수 있음 |
| Implementation-defined | 구현 정의 동작 | 컴파일러가 문서화해야 하는 선택 (음수 시프트 등) |
| Integer promotion | 정수 승격 | `int`보다 작은 타입이 연산 전 `int`로 |
| Usual arithmetic conversions | 일반 산술 변환 | 이항 연산 피연산자 공통 타입 결정 |
| `.rodata` | 읽기 전용 데이터 섹션 | const 전역, 문자열 리터럴, flash에 위치 |
| Memory pool | 고정 블록 할당기 | O(1), 단편화 없음 |
| Arena | bump allocator | 일괄 해제, TFLM tensor arena |
| High-water mark | 최대 사용량 | 스택·풀 크기 산정 근거 |
| Padding | 정렬용 빈 바이트 | 멤버 순서로 최소화 |
| Endianness | 바이트 순서 | Cortex-M은 little-endian |
| Q15 / Q31 | fixed-point 포맷 | 부호 1비트 + 소수 15/31비트 |
| Saturation | 포화 연산 | 오버플로 시 최대/최소값으로 고정 |
| LDREX/STREX | exclusive load/store | lock-free RMW, ARMv6-M에는 없음 |
| Critical section | 임계 구역 | 인터럽트 마스크로 보호하는 코드 구간 |
| Memory barrier | `DMB`/`DSB`/`ISB` | 메모리 접근·명령 순서 보장 |
| RAII | Resource Acquisition Is Initialization | 소멸자로 자원 해제 보장 |
| Placement new | 주어진 메모리에 객체 생성 | 힙 없이 생성 시점 제어 |
| Static init order fiasco | 정적 초기화 순서 문제 | 번역 단위 간 전역 생성 순서 미정 |
| MISRA C | 안전 지향 C 코딩 가이드라인 | Mandatory/Required/Advisory, deviation 절차 |

---

## 14. 요약 & 체크리스트

`volatile`은 컴파일러가 접근을 없애거나 합치지 못하게 할 뿐이고 원자성과 하드웨어 순서는 보장하지 않는다. 동기화는 critical section(PRIMASK/BASEPRI save/restore), LDREX/STREX 또는 C11 atomics, barrier로 한다(M0+는 exclusive 명령 없음). `const` 전역은 flash에 남아 RAM을 아끼고, 섹션 속성으로 DMA 버퍼·noinit·외부 flash 배치를 제어한다. 메모리는 정적 할당이 기본이고, 가변 객체는 고정 블록 pool, 같은 수명 묶음은 arena를 쓰며, 스택은 `-fstack-usage`·painting·하드웨어 limit으로 관리한다. 구조체는 큰 멤버부터 배치하고 와이어 포맷은 바이트 직렬화로 엔디언과 정렬 문제를 동시에 없앤다. 정수 승격(`uint16_t` 곱의 UB, signed/unsigned 비교, `1 << 31`)과 타이머 wrap-around 패턴을 기억한다. FPU 없는 코어와 DSP·ML 커널에는 Q15/Q31 fixed-point와 포화를 쓴다. 임베디드 C++은 예외·RTTI·힙을 끄고 RAII·constexpr·placement new를 쓰며, 전역 생성자에서 하드웨어를 건드리지 않는다. MISRA는 UB 회피 도구로 부분 채택하고 deviation을 문서화한다.

- [ ] `volatile`이 보장하는 것 1가지와 보장하지 않는 것 3가지를 예시와 함께 말할 수 있다
- [ ] `const char *` vs `const char *const` 배열이 flash/RAM 중 어디 가는지 설명할 수 있다
- [ ] 고정 블록 memory pool을 intrusive free list와 PRIMASK save/restore로 보지 않고 쓸 수 있다
- [ ] 구조체 패딩을 오프셋 단위로 계산하고 재배치로 크기를 줄일 수 있다
- [ ] 엔디언 독립 `load_be32`/`store_le16`을 쓰고 왜 캐스팅이 필요한지 설명할 수 있다
- [ ] 정수 승격 함정 5가지(uint8 합, `~`, signed/unsigned 비교, uint16 곱, `1 << 31`)를 화이트보드에 쓸 수 있다
- [ ] Q15 곱셈(반올림 + 포화)과 Q16.16 곱셈(64비트 중간값)을 구현할 수 있다
- [ ] lost update 타임라인을 그리고 LDREX/STREX와 critical section 해결책을 비교할 수 있다
- [ ] 임베디드 C++에서 끄는 기능과 쓰는 기능을 비용 근거와 함께 말할 수 있다
- [ ] MISRA의 Mandatory/Required/Advisory와 대표 규칙(21.3, 17.2)을 설명할 수 있다

---

## 참고 자료

- [ISO C working group (WG14) — 표준 초안 문서](https://www.open-std.org/jtc1/sc22/wg14/)
- [cppreference — Implicit conversions (C)](https://en.cppreference.com/w/c/language/conversion)
- [cppreference — volatile type qualifier (C)](https://en.cppreference.com/w/c/language/volatile)
- [cppreference — Atomic operations library (C)](https://en.cppreference.com/w/c/atomic)
- [cppreference — Static initialization order fiasco](https://en.cppreference.com/w/cpp/language/siof)
- [GCC — Common Variable Attributes (section, aligned, packed)](https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html)
- [GCC — Code Gen Options (-fno-exceptions 등)](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)
- [GCC — C++ Dialect Options (-fno-rtti, -fno-threadsafe-statics)](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)
- [ARMv7-M Architecture Reference Manual (exclusive access, alignment)](https://developer.arm.com/documentation/ddi0403/latest/)
- [CMSIS-Core intrinsic functions](https://arm-software.github.io/CMSIS_6/latest/Core/group__intrinsic__CPU__gr.html)
- [CMSIS-DSP (GitHub)](https://github.com/ARM-software/CMSIS-DSP)
- [Procedure Call Standard for the Arm Architecture (AAPCS32)](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [MISRA official site](https://misra.org.uk/)
- [cppcheck (MISRA addon)](https://cppcheck.sourceforge.io/)
- [Embedded Template Library (ETL)](https://www.etlcpp.com/)
- [Zephyr — Memory slabs (k_mem_slab)](https://docs.zephyrproject.org/latest/kernel/memory_management/slabs.html)
- [FreeRTOS — Memory management (heap_1~5)](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/01-Memory-management)
