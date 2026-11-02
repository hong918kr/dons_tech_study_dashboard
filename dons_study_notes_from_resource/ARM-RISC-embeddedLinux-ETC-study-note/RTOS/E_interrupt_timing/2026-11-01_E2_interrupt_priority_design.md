# E2. 인터럽트 우선순위 설계 — kernel-aware와 zero-latency 사이에 선을 긋고, 배치표를 숫자로 만들고, 검증까지 한다

> **이 노트를 다 읽으면**: kernel-aware ISR과 zero-latency ISR의 경계(FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY` / `configMAX_API_CALL_INTERRUPT_PRIORITY`, Zephyr `CONFIG_ZERO_LATENCY_IRQS` · `IRQ_ZERO_LATENCY` · `CONFIG_ZERO_LATENCY_LEVELS`)를 설계 도구로 쓸 수 있다 · 드론 비행 제어기와 FOC 모터 컨트롤러의 우선순위 배치표를 "주기 · 실행 시간 · 기다릴 수 있는 시간"에서 출발해 숫자로 만들고 RTA로 검증할 수 있다 · zero-latency ISR이 링버퍼 + 문지기(doorbell) IRQ로 커널 세계와 말하게 만들 수 있다 · 중첩 깊이와 MSP 크기, PRIGROUP, 레벨 사이 공유 자원과 ISR 사이 우선순위 역전을 실측으로 설명할 수 있다
> **왜 필요한가**: 면접 단골 "인터럽트 우선순위는 어떻게 정하나? FOC 루프는 어디에 두나? zero-latency ISR이 태스크를 깨워야 하면?"과, 입사 후 첫 리뷰에서 받는 "이 드라이버의 IRQ 레벨은 왜 이 숫자냐, 배치표에 근거가 있나?"에 답하려면 레지스터 지식(B3)을 넘어 **배치를 설계하고 검증하는 방법**이 필요하다
> **Don 기준 난이도**: 인터럽트 우선순위 · 마스킹 · ISR 공유 자료 보호는 SSD 펌웨어에서 몸으로 안다 / 새로 배울 부분은 RTOS가 만드는 "선"을 기준으로 두 세계를 나누는 설계, 배치표를 만드는 절차(원천 목록 → 세계 나누기 → 레벨 묶기 → RTA 검증), 문지기 패턴, ceiling으로 ISR 사이 역전을 막는 법
> **선행 노트**: B3(NVIC 레지스터 · 비트 수 · BASEPRI · PRIGROUP — 레지스터 설명은 거기서 끝났다), B2(예외 진입 · 중첩 · 프레임 크기), B4(PendSV · SysTick · SVC), A7 §7(하이브리드의 측정 동기), C7(임계구역 구현과 길이 측정), D8(락 없는 링버퍼 · seqlock), D3(태스크 사이 우선순위 역전), EL H6(GIC — Linux 쪽 인터럽트 컨트롤러). 이어지는 노트: E1(FromISR 규칙), E3(지연 분해 · 측정법), E4(지연 처리 · 인터럽트 폭주), N1–N3(실시간 분석 이론)
> 🔁 **Bare-Metal 겹침**: BM J8(우선순위 · 중첩 설계: 선점 그룹, 우선순위 배치표, 공유 자원과 우선순위) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 CMSIS `NVIC_SetPriority()` · `NVIC_SetPriorityGrouping()` 함수만으로 배치표를 구현하는 관점"만 더한다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러에서 인터럽트 우선순위를 정하던 장면을 떠올려 보자. 호스트 doorbell, NAND 채널 완료, DMA 완료, 온도 타이머, UART 디버그 콘솔. 누군가 처음에 "호스트 명령이 제일 중요하니 제일 높게"라고 정했고, 그 뒤로 새 인터럽트가 들어올 때마다 "대충 중간쯤"에 끼워 넣었다. 몇 년 뒤 아무도 그 숫자의 근거를 모른다. 그러다 어느 날 "NAND 완료 처리가 가끔 300 µs 늦는다"는 버그가 오고, 원인은 디버그 UART ISR이 그보다 높은 레벨에서 로그를 포맷하고 있었기 때문이다.

RTOS를 쓰는 MCU에서는 여기에 질문이 하나 더 붙는다. **RTOS 커널도 인터럽트를 막는다.** Cortex-M3 이상에서 FreeRTOS와 Zephyr는 임계구역에서 BASEPRI를 어떤 값으로 올린다(B3). 그 값이 하나의 **선**이 되어 인터럽트 세계를 둘로 나눈다.

- **선 아래 = kernel-aware ISR**: 커널 API(`...FromISR`, `k_sem_give`)를 부를 수 있다. 대신 커널(과 드라이버, 앱)의 임계구역이 이 ISR을 막는다. 최악 지연에 "가장 긴 임계구역"이 들어간다.
- **선 위 = zero-latency ISR**: 커널이 무엇을 하든 바로 들어온다. 대신 커널 API를 **하나도** 못 부른다. 커널과 말하려면 다른 길이 필요하다.

B3은 이 선이 레지스터 수준에서 어떻게 생기는지, 정확히 0x20에서 끊긴다는 것을 쟀다. 이 노트는 그다음 질문을 푼다. **어느 인터럽트를 선 위에 둘지, 선 아래는 몇 단으로 나눌지, 같은 레벨에 무엇을 묶을지, 그리고 그 배치가 맞다는 것을 무엇으로 증명할지.**

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">두 세계와 문지기 — 4비트 NVIC, 선 = 레벨 3 (이 노트의 기본 배치)</text>
<rect x="10.0" y="40.0" width="310.0" height="70.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="165.0" y="62.2" font-size="12" text-anchor="middle">레벨 0–2 (0x00–0x20): zero-latency ISR</text>
<text x="165.0" y="79.2" font-size="12" text-anchor="middle">커널 API 금지 · 임계구역이 못 막음</text>
<text x="165.0" y="96.2" font-size="12" text-anchor="middle">FOC 전류 루프, DShot, IMU 타임스탬프</text>
<line x1="10.0" y1="122.0" x2="320.0" y2="122.0" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/>
<text x="14.0" y="138.0" font-size="12" text-anchor="start">선: configMAX_SYSCALL = 0x30 = 임계구역의 BASEPRI</text>
<rect x="10.0" y="150.0" width="310.0" height="70.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165.0" y="172.2" font-size="12" text-anchor="middle">레벨 3–14 (0x30–0xE0): kernel-aware ISR</text>
<text x="165.0" y="189.2" font-size="12" text-anchor="middle">FromISR API 허용 · 임계구역이 막음</text>
<text x="165.0" y="206.2" font-size="12" text-anchor="middle">IMU SPI DMA, CAN, UART, USB, 문지기</text>
<rect x="10.0" y="232.0" width="310.0" height="44.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165.0" y="258.2" font-size="12" text-anchor="middle">레벨 15 (0xF0): PendSV · SysTick (커널)</text>
<rect x="10.0" y="288.0" width="310.0" height="44.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="165.0" y="314.2" font-size="12" text-anchor="middle">태스크 (Thread 모드, PSP) — 우선순위는 별개</text>
<rect x="360.0" y="40.0" width="310.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="515.0" y="69.2" font-size="12" text-anchor="middle">① ZL ISR: 계산 → 락 없는 링에 쓰기</text>
<rect x="360.0" y="110.0" width="310.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="515.0" y="139.2" font-size="12" text-anchor="middle">② 같은 ISR: STIR(또는 ISPR)로 문지기 pend</text>
<rect x="360.0" y="180.0" width="310.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515.0" y="209.2" font-size="12" text-anchor="middle">③ 문지기 ISR: vTaskNotifyGiveFromISR</text>
<rect x="360.0" y="250.0" width="310.0" height="50.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="515.0" y="279.2" font-size="12" text-anchor="middle">④ 태스크: 링을 비우고 느린 루프 실행</text>
<line x1="515.0" y1="90.0" x2="515.0" y2="108.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="515.0" y1="160.0" x2="515.0" y2="178.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="515.0" y1="230.0" x2="515.0" y2="248.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="320.0" y1="75.0" x2="356.0" y2="65.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="320.0" y1="185.0" x2="356.0" y2="205.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="320.0" y1="310.0" x2="356.0" y2="278.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="10.0" y="362.0" font-size="12" text-anchor="start">선 위 ISR은 커널을 모르는 대신 커널에 막히지 않는다. 커널과 말해야 하면 직접 부르지 말고</text>
<text x="10.0" y="382.0" font-size="12" text-anchor="start">선 아래의 빈 IRQ 하나(문지기)를 소프트웨어로 pend해서 대신 부르게 한다.</text>
</svg>
```

그림 1 — 이 노트의 기본 그림. 4비트 NVIC(16레벨)에서 레벨 0–2는 zero-latency, 레벨 3부터 선 아래 kernel-aware, 레벨 15는 커널의 PendSV · SysTick. 선 위 ISR이 태스크를 깨워야 할 때는 오른쪽의 네 단계(링 → 문지기 pend → 문지기가 FromISR → 태스크)로 우회한다.

이 노트에서 Don이 가져갈 도구는 넷이다.

| 도구 | 무엇을 하나 | 이 노트의 절 |
|---|---|---|
| 두 세계의 규칙 | 선 위 · 아래에서 할 수 있는 일, 커널 설정 이름과 숫자 | §1 |
| 배치표 작성법 | 원천 목록 → 세계 나누기 → 레벨 묶기 → 예약 → 검증 | §2, §3, §4 |
| 설계 트레이드오프 측정 | 선 위/아래 지터, 중첩 깊이 vs 스택, ISR 사이 역전, 주기 vs 우선순위 | §5–§8 |
| 검증 장치 | RTA 계산, 부팅 시 배치 검사, zero-latency ISR 정적 호출 검사 | §3, §8, §9 |

### 실습 환경 한 줄

- QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) + **FreeRTOS V11.3.2**(ARM_CM3 포트) / bare-metal(lab 템플릿) / **Zephyr v4.4.2**(`mps2/an385`). Python 계산은 VM 호스트(Ubuntu 26.04)에서 돌렸다.
- **실칩처럼 4비트 NVIC**로 돌렸다: `qemu-system-arm ... -global armv7m.num-prio-bits=4`. lab 기본은 8비트지만 STM32 계열이 4비트라서 배치표 숫자를 실제 칩과 같게 만들었다(레벨 n = 바이트 `n << 4`). Zephyr 예제는 보드 DTS가 3비트(`arm,num-irq-priority-bits = <3>`)로 쓰므로 QEMU 8비트 그대로 돌렸다.
- 타이밍은 모두 `-icount shift=5`(명령 1개 = 32 ns 가상 시간, 결정적)이고 시계는 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz, 1 count = 40 ns)다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다** — QEMU는 캐시, 플래시 wait state, 버스, 예외 스태킹 사이클(Cortex-M3 최소 12사이클)을 모델링하지 않는다. 숫자가 말하는 것은 "막혔나 / 안 막혔나", "무엇만큼 늦어지나" 같은 **모양**이다.
- 실습용 IRQ: 템플릿 벡터 테이블에는 IRQ 0, 1, 8, 9, 10에만 핸들러 이름이 있다. 이 노트는 배치표에 원천이 열 개 넘게 필요해서 **벡터 테이블을 RAM으로 복사(VTOR, B2 예제 2)**하고 빈 IRQ 줄(2–7, 11–16)에 핸들러를 꽂아 **소프트웨어로 pend**한다. 주기 원천이 필요할 때는 하드웨어 타이머 TIMER0(IRQ 8)과 DUALTIMER(IRQ 10)를 쓴다.
- 모든 예제는 `-Wall -Wextra`에서 경고 0개다.

### 도우미 헤더 `e2.h`와 FreeRTOS 설정

모든 C 예제가 함께 쓰는 레지스터 정의와 작은 함수다. NVIC 레지스터 자체의 설명은 B3 §1에 있다. 여기서 새로운 것은 `PRIO(n)`(논리 레벨 → 4비트 칩의 바이트)과 RAM 벡터 테이블 두 함수다.

```c
/* e2.h — helpers for the E2 note (Cortex-M3, QEMU MPS2-AN385) */
#ifndef E2_H
#define E2_H
#include <stdint.h>
#include <string.h>
#include "board.h"

#define ISER(n)   REG32(0xE000E100u + 4u * (n))
#define ICER(n)   REG32(0xE000E180u + 4u * (n))
#define ISPR(n)   REG32(0xE000E200u + 4u * (n))
#define ICPR(n)   REG32(0xE000E280u + 4u * (n))
#define IABR(n)   REG32(0xE000E300u + 4u * (n))
#define IPR8(n)   (*(volatile uint8_t *)(0xE000E400u + (n)))
#define STIR      REG32(0xE000EF00u)
#define VTOR      REG32(0xE000ED08u)
#define AIRCR     REG32(0xE000ED0Cu)
#define SHPR8(e)  (*(volatile uint8_t *)(0xE000ED18u + (e) - 4u))
#define CYC()     REG32(0x40028018u)            /* FPGAIO COUNTER, 25 MHz = 40 ns/count */
#define DT_LOAD   REG32(0x40002000u)            /* dual timer, timer 1 -> IRQ 10 */
#define DT_VALUE  REG32(0x40002004u)
#define DT_CTRL   REG32(0x40002008u)
#define DT_INTCLR REG32(0x4000200Cu)

#ifndef PRIO_BITS
#define PRIO_BITS 4                              /* run QEMU with -global armv7m.num-prio-bits=4 */
#endif
#define PRIO(n)   ((uint8_t)((n) << (8 - PRIO_BITS)))   /* logical level -> NVIC byte */

static inline void irq_prio(unsigned irq, unsigned lvl) { IPR8(irq) = PRIO(lvl); }
static inline void irq_on_n(unsigned irq)  { ISER(irq >> 5) = 1u << (irq & 31u); }
static inline void irq_pend(unsigned irq)  { STIR = irq; __asm volatile("dsb\n isb" ::: "memory"); }
static inline uint32_t get_basepri(void) { uint32_t v; __asm volatile("mrs %0, basepri" : "=r"(v)); return v; }
static inline void set_basepri(uint32_t v) { __asm volatile("msr basepri, %0\n isb" : : "r"(v) : "memory"); }
static inline void set_basepri_max(uint32_t v) { __asm volatile("msr basepri_max, %0\n isb" : : "r"(v) : "memory"); }
static inline uint32_t get_msp(void) { uint32_t v; __asm volatile("mrs %0, msp" : "=r"(v)); return v; }
static inline void spin_cyc(uint32_t n) { uint32_t t = CYC(); while (CYC() - t < n) { } }
static inline void spin_us(uint32_t us) { spin_cyc(us * 25u); }

/* RAM vector table so that any of the 32 IRQ lines can get a handler */
typedef void (*vec_t)(void);
extern const vec_t g_vectors[16 + 32];
static vec_t ram_vec[16 + 32] __attribute__((aligned(256), unused));
static inline void vec_install(void)
{
    memcpy(ram_vec, g_vectors, sizeof ram_vec);
    __asm volatile("cpsid i" ::: "memory");
    VTOR = (uint32_t)ram_vec;
    __asm volatile("dsb\n isb\n cpsie i" ::: "memory");
}
static inline void vec_set(unsigned irq, vec_t f) { ram_vec[16 + irq] = f; }
#endif
```

볼 점:

- `irq_pend()`는 STIR 쓰기 뒤에 `dsb; isb`를 둔다. 없으면 "pend 직후 다음 명령"이 ISR보다 먼저 실행될 수 있다(실칩에서도 그렇고, QEMU에서도 그렇게 보였다).
- `vec_install()`의 `cpsid i → VTOR 쓰기 → dsb; isb → cpsie i` 순서는 B2 예제 2와 같다.

FreeRTOS 예제는 lab 템플릿 설정을 가져와 **인터럽트 두 줄만** 4비트 칩용으로 바꾼다. STM32Cube가 만들어 주는 설정과 같은 모양(논리 번호 + 시프트)이다.

```c
/* E2: lab template config, but laid out for a 4-bit NVIC (run QEMU with armv7m.num-prio-bits=4) */
#ifndef E2_FREERTOS_CONFIG_H
#define E2_FREERTOS_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef  configMAX_SYSCALL_INTERRUPT_PRIORITY
#undef  configKERNEL_INTERRUPT_PRIORITY
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#ifndef configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 3      /* the line: levels 0..2 are zero-latency */
#endif
#define configKERNEL_INTERRUPT_PRIORITY      ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#endif
```

`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 3` → `configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x30`. 레벨 0, 1, 2가 선 위다. 이 숫자가 왜 3인지가 §2–§3의 내용이다.

빌드 · 실행은 작은 스크립트 하나로 한다(템플릿 Makefile로 빌드하고 QEMU를 4비트 + icount로 직접 띄운다).

```sh
# run.sh <bm|fr> <src> -- <name> [bits=4] [timeout]  (VM 안에서)
make -s -f /var/rtoslab/templates/$TEMPLATE/Makefile SRCS="$SRCS" OUT=/var/tmp/rtos/E2/$NAME EXTRA_CFLAGS="$EXTRA"
timeout --foreground $TO qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
  -serial stdio -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off \
  -global armv7m.num-prio-bits=4 -kernel /var/tmp/rtos/E2/$NAME/app.elf
```

---

## 1. 두 세계 — kernel-aware ISR과 zero-latency ISR

> 🔁 이 절은 Bare-Metal BM J8(선점 그룹 · 우선순위 배치)과 겹친다 (주인: 이 노트).

### 1.1 직관 — 커널이 그어 놓은 선

Don의 SSD 펌웨어에는 "선"이 없었다. 공유 큐를 만질 때 `cpsid i`로 전부 끄면 모든 인터럽트가 그 구간만큼 늦었다. RTOS의 BASEPRI 방식은 "커널이 아는 인터럽트만" 끈다. 비유하면 이렇다.

- **커널 = 회의실 안의 사람들.** 회의 중(임계구역)에는 문을 잠근다. 회의실 사람과 말해야 하는 방문객(kernel-aware ISR)은 문 앞에서 기다린다.
- **zero-latency ISR = 회의실과 상관없이 복도를 뛰어다니는 소방관.** 문이 잠겨도 상관없다. 대신 회의실 안의 화이트보드(커널 자료구조)는 절대 만지면 안 된다. 회의실 사람에게 할 말이 있으면 **문 옆 우편함(링버퍼)에 쪽지를 넣고 초인종(문지기 IRQ)을 누른다**. 초인종 소리는 회의가 끝나면 들린다.

선의 위치를 정하는 것은 "소방관을 몇 명 둘지" 정하는 일이다. 소방관이 많을수록 "커널을 못 쓰는 코드"가 늘고, 소방관이 너무 적으면 급한 일이 회의 앞에서 기다린다.

### 1.2 정의 — 선의 이름과 숫자 (FreeRTOS)

- **kernel-aware ISR**: 우선순위가 커널의 임계구역 마스크 이하(숫자로는 크거나 같음)인 ISR. 커널 API의 ISR용 변형을 부를 수 있다.
- **zero-latency ISR** (FreeRTOS 문서는 "커널이 절대 막지 않는 인터럽트", Zephyr는 ZLI): 그보다 급한 ISR. 커널의 임계구역이 막지 못하므로 "커널 때문에 생기는 지연"이 0이다. 하드웨어 진입 지연(Cortex-M3 12사이클 등)과 더 급한 ISR 때문에 생기는 지연은 그대로 있다 — "zero"는 **커널이 더하는 지연이 0**이라는 뜻이다.

FreeRTOS에서 이 선의 설정 이름은 **포트마다 다르다**. 템플릿 설정 파일(`examples/template_configuration/FreeRTOSConfig.h`)의 주석이 그대로 말한다: "Another name for configMAX_SYSCALL_INTERRUPT_PRIORITY - the name used depends on the FreeRTOS port."

### 예제 1 — 어느 포트가 어느 이름을 쓰나 (소스 grep)

무엇을 확인하나: FreeRTOS V11.3.2 소스에서 포트별로 두 설정 이름이 몇 개 파일에 나오는지 센다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC; for p in ARM_CM0 ARM_CM3 ARM_CM4F ARM_CM7/r0p1 ARM_CM33_NTZ/non_secure ARM_CR5 ARM_CA9 ARM_AARCH64; do s=$(grep -l "configMAX_SYSCALL_INTERRUPT_PRIORITY" $p/*.[ch] 2>/dev/null | wc -l); a=$(grep -l "configMAX_API_CALL_INTERRUPT_PRIORITY" $p/*.[ch] 2>/dev/null | wc -l); printf "%-26s MAX_SYSCALL:%s  MAX_API_CALL:%s\n" $p $s $a; done'
```

```text
ARM_CM0                    MAX_SYSCALL:0  MAX_API_CALL:0
ARM_CM3                    MAX_SYSCALL:2  MAX_API_CALL:0
ARM_CM4F                   MAX_SYSCALL:2  MAX_API_CALL:0
ARM_CM7/r0p1               MAX_SYSCALL:2  MAX_API_CALL:0
ARM_CM33_NTZ/non_secure    MAX_SYSCALL:2  MAX_API_CALL:0
ARM_CR5                    MAX_SYSCALL:1  MAX_API_CALL:2
ARM_CA9                    MAX_SYSCALL:1  MAX_API_CALL:2
ARM_AARCH64                MAX_SYSCALL:1  MAX_API_CALL:2
```

출력에서 볼 것:

- **Cortex-M3/M4F/M7/M33은 `configMAX_SYSCALL_INTERRUPT_PRIORITY`** (port.c와 portmacro.h 두 파일). 값은 NVIC **바이트**(4비트 칩의 레벨 5 = `5 << 4` = 0x50)다.
- **GIC를 쓰는 Cortex-R5 · Cortex-A9 · AArch64 포트는 `configMAX_API_CALL_INTERRUPT_PRIORITY`**. 이쪽의 MAX_SYSCALL 1개는 port.c 안의 주석뿐이다. 값은 GIC의 **논리 우선순위 번호**이고 포트가 `portPRIORITY_SHIFT`만큼 밀어서 GIC CPU 인터페이스의 우선순위 마스크 레지스터(ICCPMR)에 쓴다 — Cortex-M의 BASEPRI 자리다. `ARM_CR5/port.c`는 이 값이 0이면 `#error`, `configUNIQUE_INTERRUPT_PRIORITIES / 2` 이하여도 `#error`다(소스에서 확인). GIC 자체는 EL H6에서 다뤘다.
- **Cortex-M0(ARM_CM0)은 둘 다 0개** — 선이 없다. Armv6-M에는 BASEPRI가 없어서 FreeRTOS 임계구역이 `cpsid i`(PRIMASK)다. **M0에서는 zero-latency ISR을 만들 수 없다.** 모든 인터럽트가 커널 임계구역에 막힌다. Zephyr도 같다(`CONFIG_ZERO_LATENCY_IRQS`가 `depends on CPU_CORTEX_M_HAS_BASEPRI`).

설계로 번역하면: **"이 칩에서 선 위 레벨이 가능한가?"가 MCU를 고르는 조건이 될 수 있다.** 20 kHz FOC를 Cortex-M0+ 위의 RTOS와 같이 돌리려면 커널 임계구역 최악값이 그대로 FOC 지터가 된다. 그래서 모터 제어용 MCU는 대개 M4F/M33/M7급이다.

### 1.3 정의 — Zephyr의 ZLI: Kconfig 두 개 + 플래그 하나 + direct ISR

Zephyr는 선을 앱이 아니라 **커널이 정한다**(`irq_lock()` = 앱이 쓸 수 있는 가장 급한 일반 IRQ 레벨까지 전부 막기, B3 §8). 그 위에 레벨을 만들려면 세 가지를 다 해야 한다(`arch/arm/core/cortex_m/Kconfig`, `include/zephyr/arch/arm/irq.h`, `arch/arm/core/cortex_m/irq_manage.c`에서 확인).

1. `CONFIG_ZERO_LATENCY_IRQS=y` — 가장 급한 레벨을 ZLI용으로 예약하고 일반 IRQ를 그만큼 아래로 민다. `depends on CPU_CORTEX_M_HAS_BASEPRI`.
2. `CONFIG_ZERO_LATENCY_LEVELS=n` — 예약할 레벨 수(기본 1, `range 1 $(UINT8_MAX)`). 1이면 모든 ZLI가 고정 레벨 0이고 `IRQ_DIRECT_CONNECT`의 priority 인자는 **무시**된다. 2 이상이면 인자를 **그대로**(오프셋 없이) 하드웨어 레벨로 쓴다.
3. `IRQ_DIRECT_CONNECT(irq, prio, isr, IRQ_ZERO_LATENCY)` + `ISR_DIRECT_DECLARE(isr) { ... return 0; }` — 공용 래퍼(`_isr_wrapper`)는 커널 자료를 만지므로 ZLI는 반드시 direct ISR이어야 하고, 반환값(재스케줄 요청)은 항상 0이어야 한다.

레벨 수와 NVIC 바이트의 관계를 `exception.h`의 식(`_IRQ_PRIO_OFFSET = _EXCEPTION_RESERVED_PRIO + _EXC_SVC_PRIO`, `_EXC_SVC_PRIO = ZERO_LATENCY_LEVELS 또는 0`)으로 3비트 칩(mps2/an385 DTS)에 대해 풀면 이렇다. ZLI=n과 LEVELS=1은 B3 예제 13에서, LEVELS=2는 아래 예제 5에서 실제로 읽은 값이다.

| 설정 | fault | ZLI | SVC | 일반 IRQ 0 → | `IRQ_PRIO_LOWEST` | `irq_lock()` BASEPRI | PendSV |
|---|---|---|---|---|---|---|---|
| ZLI 끔 | 0x00 | — | 0x00 | 0x20 | 6 | 0x20 | 0xE0 |
| ZLI, LEVELS=1 | 0x00 | 0x00 (고정) | 0x20 | 0x40 | 5 | 0x40 | 0xE0 |
| ZLI, LEVELS=2 | 0x00 | 0x00, 0x20 | 0x40 | 0x60 | 4 | 0x60 | 0xE0 |

설계 관점에서 읽을 것:

- 3비트 칩(8레벨)에서 LEVELS=2를 쓰면 일반 IRQ가 쓸 수 있는 레벨이 5개(0–4)만 남는다. **ZLI 레벨 하나 = 일반 IRQ 레벨 하나를 잃는 것**이다.
- ZLI 레벨 0은 **fault(MemManage · BusFault · UsageFault)와 같은 레벨 0x00**이다. Armv7-M 규칙상 동기 fault는 현재 실행 우선순위보다 급할 때만 자기 핸들러로 들어가고 아니면 HardFault로 승격된다. 그러니 **ZLI 레벨 0 핸들러 안에서 난 MemManage/BusFault는 HardFault가 된다.** 정상 동작에는 영향이 없지만 fault 분석(M1) 때 "왜 HardFault냐"의 답이 여기 있다.
- SVC가 ZLI 바로 아래로 밀린다. Zephyr는 커널 oops나 시스템 콜에 SVC를 쓰므로 ZLI는 그것도 막을 수 없게 된다.

### 1.4 zero-latency ISR이 해도 되는 일과 안 되는 일

| 하고 싶은 일 | 선 위 (zero-latency) | 선 아래 (kernel-aware) | 이유 |
|---|---|---|---|
| 주변장치 레지스터 읽고 쓰기, 계산 | 된다 | 된다 | 커널과 무관 |
| `xQueueSendFromISR`, `vTaskNotifyGiveFromISR`, `k_sem_give` | **안 된다** | 된다 | 커널 자료를 고치는 중에 끼어들 수 있다 |
| `taskENTER_CRITICAL_FROM_ISR()`, `irq_lock()` | **의미 없음 / 안 된다** | 된다 | BASEPRI를 선까지 올려 봐야 자기보다 낮은 것만 막는다. FreeRTOS는 이것도 API로 보고 assert한다 |
| 락 없는 링버퍼(SPSC)에 쓰기, seqlock 쓰기 | 된다 | 된다 | 소유권이 나뉘어 락이 필요 없다(D8) |
| 문지기 IRQ를 STIR · ISPR로 pend | 된다 | 된다 | NVIC 레지스터 쓰기일 뿐 |
| `printf`, 로그 큐 쓰기 | **안 된다** | 피한다 | 로그가 커널 큐 · 뮤텍스를 쓰는 경우가 대부분 |
| 커널 tick 값 읽기(`xTaskGetTickCountFromISR`) | **안 된다** (값이 찢어질 수 있다) | 된다 | 64비트 tick, 임계구역 안의 갱신 |
| 커널이 읽는 자료(태스크 TCB, 커널 객체) 수정 | **안 된다** | API로만 | Zephyr 문서: "may not modify any data inspected by kernel APIs" |
| 동기 예외 일으키기(`k_panic`, assert) | **안 된다** | 된다 | Zephyr 문서: "shall not generate exceptions that need to be handled synchronously" |

Zephyr 문서(`doc/kernel/services/interrupts.rst`, Zero Latency Interrupts 절)의 표현이 정확하다: ZLI는 "treat all kernel APIs as undefined behavior". FreeRTOS는 같은 규칙을 configASSERT로 런타임에 잡고(`port.c:803`, 예제 3에서 다시 본다), Zephyr는 등록 방식을 빌드 타임에 잡는다(예제 6).

### 1.5 zero-latency ISR이 커널 세계와 말하는 법 — 링 + 문지기

선 위 ISR이 태스크를 깨워야 하는 일은 흔하다. FOC 전류 루프가 2 kHz마다 속도 루프에 "새 전류 · 위치 샘플이 있다"고 알려야 하고, IMU 타임스탬프 ISR이 "샘플이 왔다"고 알려야 한다. 방법은 정해져 있다.

1. **자료는 락 없는 구조로 넘긴다.** 단일 생산자(ZL ISR) · 단일 소비자(태스크) 링버퍼, 또는 "최신 값 하나"면 seqlock · 더블 버퍼(D8 §4–§6).
2. **알림은 문지기(doorbell) IRQ로 넘긴다.** 아무 주변장치도 연결되지 않은 IRQ 줄 하나를 선 아래 레벨에 두고, ZL ISR이 `STIR = n`(또는 `NVIC_SetPendingIRQ(n)` = ISPR 쓰기)으로 pend한다. 문지기 ISR은 kernel-aware이므로 `vTaskNotifyGiveFromISR()`을 부를 수 있다.
3. 문지기는 **임계구역이 끝나야** 들어온다. 즉 "알림 지연"에는 커널 임계구역이 들어가지만, **ZL ISR 자신의 지연에는 안 들어간다**. 급한 일(전류 루프)과 덜 급한 일(태스크 깨우기)이 자연스럽게 분리된다.

PendSV가 "가장 낮은 레벨의 소프트웨어 인터럽트로 미뤄 둔 전환"이었던 것(B4)과 같은 생각이다. 문지기는 "선 바로 아래에 둔, 커널 호출을 미뤄 둔 소프트웨어 인터럽트"다. Don의 SSD 펌웨어로 말하면, 하드웨어 큐 포인터만 갱신하고 실행부 루프에 플래그를 세우던 ISR과 같다 — 다만 플래그 대신 NVIC의 pending 비트를 쓴다.

함정 하나: **문지기 IRQ 줄은 진짜로 비어 있어야 한다.** 칩의 예약 IRQ나 쓰지 않는 주변장치의 IRQ를 고르는데, 그 주변장치를 나중에 누가 켜면 문지기 ISR이 엉뚱하게 불린다. Zephyr에서는 DTS가 이미 쓰는 IRQ를 고르면 `gen_isr_tables.py: error: multiple registrations at table_index 4 for irq 4`로 빌드가 멈춘다(이 노트를 쓰면서 mps2/an385의 IRQ 4를 골랐다가 실제로 만난 에러다 — 그래서 예제 5는 IRQ 20–22를 쓴다).

---

## 2. 배치표 작성법 — 숫자에서 출발한다

> 🔁 이 절은 Bare-Metal BM J8(우선순위 배치표)과 겹친다 (주인: 이 노트).

### 2.1 직관 — "중요한 것"이 아니라 "못 기다리는 것"이 위로 간다

배치표에서 가장 흔한 실수는 우선순위를 **중요도**로 정하는 것이다. "비행 제어가 제일 중요하니 자세 제어 1 kHz 루프 타이머가 최상위." 그런데 인터럽트 우선순위가 정하는 것은 **누가 누구를 기다리게 하느냐**다. 1 kHz 루프는 1 ms 안에만 끝나면 되므로 20 µs 기다려도 아무 일이 없지만, DShot 비트 타이밍이나 IMU 데이터 레디 타임스탬프는 1 µs만 늦어도 데이터가 틀린다. **기다릴 수 있는 시간(tolerance)이 짧은 것이 위로 간다.** 이것이 실시간 이론의 deadline-monotonic 배치다(N2: rate-monotonic은 마감 = 주기일 때의 특수한 경우).

### 2.2 원천마다 적는 네 숫자

| 칸 | 뜻 | 어디서 얻나 |
|---|---|---|
| T (주기 또는 최소 도착 간격) | 이 인터럽트가 얼마나 자주 오나 | 센서 ODR, 프로토콜 프레임 속도, 최악 버스트 |
| C (ISR 실행 시간, WCET) | ISR 한 번이 CPU를 쥐는 최대 시간 | 측정(GPIO 토글 + scope, DWT CYCCNT — E3), 코드 리뷰 |
| tol (기다릴 수 있는 시간) | 인터럽트가 뜬 뒤 ISR이 **시작**하기까지 허용되는 최대 지연 | 하드웨어 FIFO 깊이 ÷ 도착 속도, 다음 이벤트까지 남은 시간, 타임스탬프 정확도 요구 |
| kernel? | ISR이 태스크를 직접 깨워야 하나 | 설계 |

tol을 구하는 예:

- CAN 1 Mbit/s, 수신 FIFO 3단. 데이터 0바이트 프레임 + 프레임 간격이 약 47비트 → 프레임 하나 약 47 µs, FIFO가 넘치기까지 3 × 47 ≈ 140 µs. 8바이트 프레임(스터핑 전 약 110비트)만 온다면 3 × 110 ≈ 330 µs. 이 노트의 표는 8바이트 프레임 버스를 가정하고 여유를 뺀 280 µs를 썼다 — **짧은 프레임이 올 수 있는 버스라면 가장 짧은 프레임(140 µs)으로 계산**한다.
- UART 921600 baud, 하드웨어 FIFO 없음(1바이트 데이터 레지스터) → 1바이트 ≈ 10.9 µs. tol ≈ 10 µs! 그래서 고속 UART는 **DMA**로 받는다. DMA idle-line 인터럽트는 프레임 단위라 tol이 ms 단위로 늘어난다(E6). **배치표 작업의 절반은 "이 원천의 tol을 DMA · FIFO로 늘릴 수 있나"를 묻는 것이다.**
- IMU DRDY 핀에서 타임스탬프를 소프트웨어로 찍는다면 tol = 요구 타임스탬프 정확도(예: 1 µs). 하드웨어 입력 캡처를 쓰면 tol이 다음 샘플까지(125 µs @ 8 kHz)로 늘어난다(E5). **가장 좋은 zero-latency ISR은 하드웨어로 대체된 ISR이다.**

### 2.3 일곱 단계

```svg
<svg viewBox="0 0 680 482" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">우선순위 배치표 작성법 — 7단계</text>
<rect x="20.0" y="40.0" width="640.0" height="44.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="66.2" font-size="12" text-anchor="middle">① 원천 목록: 주기 T, ISR 실행 시간 C, 기다릴 수 있는 시간 tol, 커널 필요 여부</text>
<line x1="340.0" y1="84.0" x2="340.0" y2="98.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="20.0" y="100.0" width="640.0" height="44.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="126.2" font-size="12" text-anchor="middle">② 커널 차단 예산 BK: 커널 + 드라이버의 가장 긴 임계구역 (C7에서 잰다)</text>
<line x1="340.0" y1="144.0" x2="340.0" y2="158.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="20.0" y="160.0" width="640.0" height="44.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340.0" y="186.2" font-size="12" text-anchor="middle">③ tol &lt; BK 이면 zero-latency 세계, 아니면 kernel-aware 세계</text>
<line x1="340.0" y1="204.0" x2="340.0" y2="218.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="20.0" y="220.0" width="640.0" height="44.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340.0" y="246.2" font-size="12" text-anchor="middle">④ 세계마다 tol(마감)이 짧은 순서로 정렬 — deadline-monotonic</text>
<line x1="340.0" y1="264.0" x2="340.0" y2="278.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="20.0" y="280.0" width="640.0" height="44.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="306.2" font-size="12" text-anchor="middle">⑤ 서로 기다려 줄 수 있는 원천은 같은 레벨로 묶기 → 중첩 · 스택 감소</text>
<line x1="340.0" y1="324.0" x2="340.0" y2="338.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="20.0" y="340.0" width="640.0" height="44.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="366.2" font-size="12" text-anchor="middle">⑥ 예약: 레벨 0(비상), 선, 최저 레벨(PendSV · SysTick), 빈 레벨 남기기</text>
<line x1="340.0" y1="384.0" x2="340.0" y2="398.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="20.0" y="400.0" width="640.0" height="44.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340.0" y="426.2" font-size="12" text-anchor="middle">⑦ 검증: RTA로 최악 대기 · 부팅 시 배치 검사 · ZL 정적 호출 검사</text>
<text x="20.0" y="468.0" font-size="12" text-anchor="start">③에서 zero-latency로 간 원천이 커널을 불러야 하면 문지기 IRQ를 선 아래에 하나 추가한다.</text>
</svg>
```

그림 2 — 배치표 작성법 일곱 단계. ③의 기준 BK가 선을 정하고, ⑤가 레벨 수(= 중첩 깊이 = MSP 크기)를 정하고, ⑦이 숫자로 증명한다.

각 단계의 설명:

1. **원천 목록**: 칩의 모든 인터럽트 원천(쓰는 것만)을 표로 적는다. HAL이 몰래 켜는 것(SysTick을 HAL 타임베이스로 쓰는 STM32Cube의 `HAL_InitTick`, USB 라이브러리의 SOF)도 넣는다.
2. **커널 차단 예산 BK**: "선 아래 ISR이 커널 때문에 최대 얼마나 막히나" = 커널 + 모든 드라이버 + 앱의 **가장 긴 BASEPRI 임계구역**. C7 §6.3처럼 실제 워크로드에서 재야 한다. 그리고 §4 예제 4에서 보겠지만 **그 임계구역 안에 끼어든 zero-latency ISR 시간도 더해진다.**
3. **세계 나누기**: tol < BK인 원천은 선 아래에 두면 마감을 못 지킨다 → zero-latency. 나머지는 kernel-aware. 커널을 불러야 하는 zero-latency 원천이 있으면 문지기를 하나 추가한다.
4. **정렬**: 세계마다 tol이 짧은 순서(deadline-monotonic). tol이 같으면 주기가 짧은 순서(rate-monotonic).
5. **묶기**: 서로를 기다려 줄 수 있는 원천들은 **같은 레벨**에 둔다. 같은 레벨끼리는 선점하지 않으므로(B3 §4) 중첩이 줄고, MSP가 줄고, 같은 레벨 ISR끼리 공유하는 자료는 락이 필요 없다(§7). 판정: 그 레벨의 모든 원천 m에 대해 `tol_m ≥ (선 아래면 BK) + Σ(같은 레벨 다른 원천의 C)`.
6. **예약**: 레벨 0은 비상용(과전류 래치, 모터 정지)으로 남긴다. 최저 레벨은 FreeRTOS의 PendSV · SysTick(`configKERNEL_INTERRUPT_PRIORITY`). 중간에 빈 레벨을 남겨 나중에 들어올 원천의 자리로 쓴다.
7. **검증**: RTA로 각 원천의 최악 대기를 계산해 tol과 비교(예제 11), 부팅 시 NVIC를 다시 읽어 배치표와 비교(예제 3), zero-latency ISR에서 커널 함수로 가는 호출 경로가 없는지 정적 검사(예제 12).

### 예제 2 — 배치표 도구 (Python): 드론 비행 제어기

무엇을 확인하나: §2.3의 ①–⑥을 그대로 코드로 옮긴 50줄짜리 도구에 드론 비행 제어기의 원천 11개를 넣고, 4비트 칩 배치표와 C 헤더를 뽑는다. BK는 25 µs로 잡았다(드라이버까지 포함한 가장 긴 BASEPRI 구간 예산).

```python
# ex1 — priority layout tool: rank by tolerable wait, split at the kernel line, pack into levels
import sys
BITS, BK = 4, 25.0        # 4-bit NVIC; BK = longest kernel/driver critical section budget (us)
#  name           rate_hz  wcet_us  tol_us  needs_kernel  (tol = longest wait before the ISR may start)
DRONE = [
  ("ESC_KILL",          0,   1.0,    1.0, False),   # emergency stop comparator, rare
  ("IMU_DRDY",       8000,   0.5,    1.0, False),   # capture timestamp only
  ("DSHOT_DMA",      8000,   2.0,    4.0, False),   # re-arm the motor-frame DMA
  ("IMU_SPI_DMA",    8000,   3.0,   40.0, True ),   # wakes the rate-loop task
  ("RC_UART_IDLE",    250,   4.0,  200.0, True ),   # CRSF frame done
  ("CAN_RX",         1000,   5.0,  280.0, True ),   # 3-deep FIFO at 1 Mbit
  ("USB_OTG",        1000,  10.0,  900.0, True ),
  ("GNSS_UART_DMA",    10,   4.0, 2000.0, True ),
  ("TELEM_UART_DMA",  100,   4.0, 2000.0, True ),
  ("BARO_I2C_DMA",     50,   3.0, 5000.0, True ),
  ("SDMMC_DMA",       200,   6.0, 5000.0, True ),
]
def pack(srcs, first, block):
    """greedy: same level only if every member can wait for block + all the others in the level"""
    levels, lvl = [], first
    for s in sorted(srcs, key=lambda s: (s[3], -s[1])):
        cur = levels[-1][1] if levels else None
        ok = cur is not None and all(m[3] >= block + sum(x[2] for x in cur + [s] if x is not m) for m in cur + [s])
        if ok: cur.append(s)
        else: levels.append([lvl, [s]]); lvl += 1
    return levels, lvl
def layout(srcs, reserve0=True):
    zl = [s for s in srcs if s[3] < BK and s[0] != "ESC_KILL"]
    ka = [s for s in srcs if s[3] >= BK]
    rows = [(0, srcs[0], "ZL")] if reserve0 else []
    zlv, line = pack(zl, 1 if reserve0 else 0, 0.0)
    kav, end = pack(ka, line, BK)     # kernel-aware ISRs also wait for BK
    for lv, ms in zlv: rows += [(lv, m, "ZL") for m in ms]
    for lv, ms in kav: rows += [(lv, m, "KA") for m in ms]
    top = (1 << BITS) - 1
    assert end <= top, "not enough levels"
    return rows, line, top
if __name__ == "__main__":
    rows, line, top = layout(DRONE)
    print(f"{BITS}-bit NVIC, kernel line = level {line} (byte 0x{line << (8-BITS):02x}), BK = {BK} us")
    print(f"{'lvl':>3} {'byte':>4}  {'source':<15}{'world':<6}{'tol_us':>7} {'wcet':>5}  api")
    for lv, s, w in rows:
        api = "FromISR" if w == "KA" else ("doorbell" if s[4] else "none")
        print(f"{lv:>3} 0x{lv << (8-BITS):02x}  {s[0]:<15}{w:<6}{s[3]:>7.0f} {s[2]:>5.1f}  {api}")
    print(f"{top:>3} 0x{top << (8-BITS):02x}  PendSV,SysTick  kernel")
    used = sorted({r[0] for r in rows}); print("levels used:", used, "-> preemption depth <=", len(used))
    if "-h" in sys.argv:
        print("/* generated */")
        print(f"#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY {line}")
        for lv, s, w in rows: print(f"#define LVL_{s[0]:<15} {lv}")
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/E2 && python3 ex1_layout.py -h'
```

```text
4-bit NVIC, kernel line = level 3 (byte 0x30), BK = 25.0 us
lvl byte  source         world  tol_us  wcet  api
  0 0x00  ESC_KILL       ZL          1   1.0  none
  1 0x10  IMU_DRDY       ZL          1   0.5  none
  2 0x20  DSHOT_DMA      ZL          4   2.0  none
  3 0x30  IMU_SPI_DMA    KA         40   3.0  FromISR
  3 0x30  RC_UART_IDLE   KA        200   4.0  FromISR
  3 0x30  CAN_RX         KA        280   5.0  FromISR
  4 0x40  USB_OTG        KA        900  10.0  FromISR
  4 0x40  TELEM_UART_DMA KA       2000   4.0  FromISR
  4 0x40  GNSS_UART_DMA  KA       2000   4.0  FromISR
  4 0x40  SDMMC_DMA      KA       5000   6.0  FromISR
  4 0x40  BARO_I2C_DMA   KA       5000   3.0  FromISR
 15 0xf0  PendSV,SysTick  kernel
levels used: [0, 1, 2, 3, 4] -> preemption depth <= 5
/* generated */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 3
#define LVL_ESC_KILL        0
#define LVL_IMU_DRDY        1
#define LVL_DSHOT_DMA       2
#define LVL_IMU_SPI_DMA     3
#define LVL_RC_UART_IDLE    3
#define LVL_CAN_RX          3
#define LVL_USB_OTG         4
#define LVL_TELEM_UART_DMA  4
#define LVL_GNSS_UART_DMA   4
#define LVL_SDMMC_DMA       4
#define LVL_BARO_I2C_DMA    4
```

출력에서 볼 것:

- **선 = 레벨 3**(0x30)이 계산으로 나왔다. tol < 25 µs인 원천이 셋(비상 정지, IMU DRDY, DShot)이고 레벨 0을 비상용으로 예약했으므로 1, 2가 ZL, 3부터 kernel-aware.
- ZL 셋은 tol이 1–4 µs라 **서로를 기다려 줄 수 없다** → 레벨 셋. DShot(C = 2 µs)이 DRDY와 같은 레벨이면 DRDY가 2 µs 기다려야 하는데 tol이 1 µs다.
- kernel-aware 8개는 **두 레벨이면 충분**하다. IMU SPI DMA(tol 40 µs)는 BK 25 + RC 4 + CAN 5 = 34 µs를 기다려도 되지만, USB(C = 10 µs)까지 같은 레벨이면 44 µs가 되어 넘는다 → USB부터 레벨 4.
- 쓰는 레벨 5개 → **중첩 깊이 최대 5**(+ 커널 레벨 15가 맨 아래에 하나 더). §6에서 이것을 MSP 크기로 바꾼다.
- 이 도구는 **첫 안**이다. 같은 원천이 여러 번 오는 것(주기 안의 반복 도착)과 더 높은 레벨의 간섭을 세지 않는다. 그것은 예제 11의 RTA가 검증한다.

---

## 3. 드론 비행 제어기 배치표 — 근거와 검증

### 3.1 완성된 표

예제 2의 결과에 근거 칸을 붙인 표다. STM32H7급(4비트), FreeRTOS 기준이다. Betaflight · PX4가 실제로 이 값을 쓴다는 뜻은 아니다 — 같은 절차로 만든 **설계 예시**다.

| 레벨 (바이트) | 원천 | 세계 | 근거 (tol, C) | ISR이 하는 일 |
|---|---|---|---|---|
| 0 (0x00) | ESC_KILL — 비상 정지 비교기 / 킬 스위치 | ZL | 1 µs, 드물다 | 모터 출력 타이머 정지, 플래그 래치 |
| 1 (0x10) | IMU_DRDY — 데이터 레디 엣지 | ZL | 1 µs (타임스탬프 정확도) | 카운터 캡처, SPI DMA 시작 |
| 2 (0x20) | DSHOT_DMA — 모터 프레임 DMA 완료 | ZL | 4 µs (다음 프레임 재장전) | DMA 재장전만 |
| 선 | `configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x30` | | | |
| 3 (0x30) | IMU_SPI_DMA, RC_UART_IDLE, CAN_RX | KA | 40 / 200 / 280 µs | rate 루프 태스크 통지, 프레임을 stream buffer로 |
| 4 (0x40) | USB_OTG, TELEM, GNSS, SDMMC, BARO | KA | 900 µs 이상 | 버퍼 넘기기, 태스크 통지 |
| 5–14 | (비움) | | | 나중에 들어올 원천 |
| 15 (0xF0) | PendSV, SysTick | 커널 | | `configKERNEL_INTERRUPT_PRIORITY` |

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">드론 비행 제어기 배치표 (예제 2 결과, 4비트) — 오른쪽은 예제 11의 RTA 최악 대기</text>
<rect x="10.0" y="40.0" width="90.0" height="38.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="55.0" y="63.2" font-size="12" text-anchor="middle">0  0x00</text>
<rect x="110.0" y="40.0" width="400.0" height="38.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120.0" y="63.2" font-size="12" text-anchor="start">ESC_KILL — 비상 정지 비교기</text>
<text x="600.0" y="64.0" font-size="12" text-anchor="middle">0 µs / 1 µs</text>
<rect x="10.0" y="86.0" width="90.0" height="38.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="55.0" y="109.2" font-size="12" text-anchor="middle">1  0x10</text>
<rect x="110.0" y="86.0" width="400.0" height="38.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120.0" y="109.2" font-size="12" text-anchor="start">IMU_DRDY — 타임스탬프 캡처</text>
<text x="600.0" y="110.0" font-size="12" text-anchor="middle">1.0 / 1 µs</text>
<rect x="10.0" y="132.0" width="90.0" height="38.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="55.0" y="155.2" font-size="12" text-anchor="middle">2  0x20</text>
<rect x="110.0" y="132.0" width="400.0" height="38.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120.0" y="155.2" font-size="12" text-anchor="start">DSHOT_DMA — 모터 프레임 재장전</text>
<text x="600.0" y="156.0" font-size="12" text-anchor="middle">1.5 / 4 µs</text>
<line x1="10.0" y1="175.0" x2="670.0" y2="175.0" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/>
<text x="520.0" y="170.0" font-size="12" text-anchor="start">선 0x30</text>
<rect x="10.0" y="184.0" width="90.0" height="38.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="55.0" y="207.2" font-size="12" text-anchor="middle">3  0x30</text>
<rect x="110.0" y="184.0" width="400.0" height="38.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120.0" y="207.2" font-size="12" text-anchor="start">IMU_SPI_DMA · RC_UART_IDLE · CAN_RX</text>
<text x="600.0" y="208.0" font-size="12" text-anchor="middle">33.5 / 40 µs</text>
<rect x="10.0" y="230.0" width="90.0" height="38.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="55.0" y="253.2" font-size="12" text-anchor="middle">4  0x40</text>
<rect x="110.0" y="230.0" width="400.0" height="38.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120.0" y="253.2" font-size="12" text-anchor="start">USB · TELEM · GNSS · SDMMC · BARO</text>
<text x="600.0" y="254.0" font-size="12" text-anchor="middle">50.5 / ≥900 µs</text>
<rect x="10.0" y="276.0" width="90.0" height="38.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="55.0" y="299.2" font-size="12" text-anchor="middle">5–14</text>
<rect x="110.0" y="276.0" width="400.0" height="38.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="120.0" y="299.2" font-size="12" text-anchor="start">비워 둔다 — 나중에 들어올 원천의 자리</text>
<rect x="10.0" y="322.0" width="90.0" height="38.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="55.0" y="345.2" font-size="12" text-anchor="middle">15  0xF0</text>
<rect x="110.0" y="322.0" width="400.0" height="38.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="345.2" font-size="12" text-anchor="start">PendSV · SysTick</text>
<text x="600.0" y="34.0" font-size="12" text-anchor="middle">대기 / 허용</text>
<text x="10.0" y="392.0" font-size="12" text-anchor="start">ZL 세 레벨은 서로 tol이 수 µs라 한 레벨에 묶을 수 없다. kernel-aware는 두 레벨이면 충분하다:</text>
<text x="10.0" y="410.0" font-size="12" text-anchor="start">레벨 3 안의 셋은 서로를 기다려도 tol을 넘지 않지만, USB(C=10 µs)까지 넣으면 IMU가 40 µs를 넘는다.</text>
</svg>
```

그림 3 — 드론 비행 제어기 배치표. 오른쪽 숫자는 예제 11의 RTA로 구한 최악 대기 / 허용(tol).

이 표에서 설계 리뷰가 물을 것들:

- **"rate 루프(자세 제어 내부 루프)는 어디 있나?"** — 인터럽트가 아니라 **태스크**다. IMU SPI DMA 완료(레벨 3)가 `vTaskNotifyGiveFromISR`로 깨운다. 지연 예산 = 레벨 3 최악 대기 33.5 µs + 컨텍스트 스위치 + 태스크 실행. 8 kHz(125 µs) 루프라면 남는 시간이 충분한지가 E3 · N3의 질문이다.
- **"IMU_DRDY는 왜 SPI DMA보다 위인가?"** — DRDY는 타임스탬프 1 µs 정확도가 요구되는 원천이고, SPI DMA 완료는 데이터를 40 µs 안에 넘기기만 하면 된다. 같은 센서의 두 인터럽트가 **서로 다른 세계**에 있다. 타이머 입력 캡처로 DRDY 시각을 하드웨어가 찍게 하면 레벨 1 자체가 사라진다.
- **"SysTick이 최저인데 tick이 밀리지 않나?"** — FreeRTOS Cortex-M 포트는 PendSV와 SysTick을 둘 다 `configKERNEL_INTERRUPT_PRIORITY`(최저)로 둔다(`xPortStartScheduler()`의 `portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI`). 모든 ISR이 tick보다 급하다. SysTick pending 비트는 하나뿐이라 **ISR 부하가 연속 1 tick(1 ms)을 넘으면 tick을 잃는다**. 이 표의 ISR 부하는 그보다 훨씬 작다. Zephyr는 반대로 SysTick을 가장 급한 일반 IRQ 레벨에 둔다(B3 §8, 예제 5 출력 `SysTick=0x60`).

### 예제 3 — 배치표를 칩에 적용하고 부팅 때 검사한다

무엇을 확인하나: 예제 2의 표를 그대로 C 배열로 옮겨 4비트 NVIC에 적용하고, 스케줄러가 뜬 뒤 `layout_check()`가 NVIC를 **다시 읽어** 표와 비교한다. 검사 항목은 다섯: 읽은 바이트가 `PRIO(lvl)`과 같은가(시프트 실수), ZL은 선 위 · KA는 선 아래인가(설정과 표의 불일치), 켜진 IRQ가 모두 표에 있는가(우선순위를 안 정한 IRQ = 리셋값 0 = 최상위), PendSV · SysTick이 최저인가, PRIGROUP이 "모든 비트 선점"인가. 그다음 IRQ 11개를 전부 pend해서 KA는 FromISR을 부르게 한다. 마지막에 일부러 실수 두 개(시프트 빠진 쓰기, 표에 없는 IRQ enable)를 넣고 다시 검사한다.

```c
/* ex2 — apply the ex1 layout on a 4-bit NVIC and verify it at boot (layout_check) */
#include "FreeRTOS.h"
#include "task.h"
#include "e2.h"
typedef struct { uint8_t irq, lvl, ka; const char *name; } src_t;
static const src_t L[] = {   /* from ex1: lvl 0-2 zero-latency, 3-4 kernel-aware */
  {2,0,0,"ESC_KILL"}, {3,1,0,"IMU_DRDY"}, {4,2,0,"DSHOT_DMA"}, {5,3,1,"IMU_SPI_DMA"},
  {6,3,1,"RC_UART_IDLE"}, {7,3,1,"CAN_RX"}, {11,4,1,"USB_OTG"}, {12,4,1,"TELEM_UART"},
  {13,4,1,"GNSS_UART"}, {14,4,1,"SDMMC_DMA"}, {15,4,1,"BARO_I2C"} };
#define NL (sizeof L / sizeof L[0])
static TaskHandle_t h;
static volatile uint32_t zl_hits;
static void isr(void)
{
    uint32_t ipsr; __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    for (unsigned i = 0; i < NL; i++)
        if (L[i].irq == ipsr - 16) {
            if (L[i].ka) { BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(h, &w); portYIELD_FROM_ISR(w); }
            else zl_hits++;                       /* zero-latency: no kernel call */
        }
}
static int layout_check(void)
{
    int bad = 0; uint32_t en = ISER(0);
    for (unsigned i = 0; i < NL; i++) {
        uint8_t b = IPR8(L[i].irq);
        int ok = b == PRIO(L[i].lvl) && (L[i].ka ? b >= configMAX_SYSCALL_INTERRUPT_PRIORITY
                                                  : b <  configMAX_SYSCALL_INTERRUPT_PRIORITY);
        if (!ok) { bad++; board_printf_raw("  BAD %s irq%u byte=0x%x want 0x%x %s line 0x%x\n", L[i].name, L[i].irq, b,
                         PRIO(L[i].lvl), L[i].ka ? ">=" : "<", configMAX_SYSCALL_INTERRUPT_PRIORITY); }
        en &= ~(1u << L[i].irq);
    }
    for (unsigned n = 0; n < 32; n++)
        if (en & (1u << n)) { bad++; board_printf_raw("  BAD irq%u enabled but not in layout (byte=0x%x)\n", n, IPR8(n)); }
    if (SHPR8(14) != 0xF0 || SHPR8(15) != 0xF0) { bad++; board_printf_raw("  BAD PendSV/SysTick\n"); }
    if (((AIRCR >> 8) & 7u) > 8u - PRIO_BITS - 1u) { bad++; board_printf_raw("  BAD PRIGROUP\n"); }
    return bad;
}
static void task(void *a)
{
    (void)a;
    board_printf_raw("configMAX_SYSCALL=0x%x PendSV=0x%x SysTick=0x%x PRIGROUP=%u\n",
        configMAX_SYSCALL_INTERRUPT_PRIORITY, SHPR8(14), SHPR8(15), (AIRCR >> 8) & 7u);
    board_printf_raw("check #1: %d problem(s)\n", layout_check());
    for (unsigned i = 0; i < NL; i++) irq_pend(L[i].irq);
    uint32_t n = ulTaskNotifyTake(pdTRUE, 0);    /* notification value = number of gives */
    board_printf_raw("pended %u IRQs: zero-latency ran %u, kernel-aware gave %u\n", NL, zl_hits, n);
    IPR8(5) = 3;                                  /* "HAL" writes an unshifted level */
    irq_on_n(9 + 7);                              /* and enables IRQ 16 it never prioritised */
    board_printf_raw("check #2 after two mistakes:\n");
    board_printf_raw("check #2: %d problem(s)\n", layout_check());
    board_exit(0);
}
int main(void)
{
    vec_install();
    for (unsigned i = 0; i < NL; i++) { vec_set(L[i].irq, isr); irq_prio(L[i].irq, L[i].lvl); irq_on_n(L[i].irq); }
    xTaskCreate(task, "t", 512, NULL, 2, &h);
    vTaskStartScheduler();
    return 1;
}
```

먼저 **FreeRTOSConfig의 선(레벨 4)이 표의 선(레벨 3)과 다른 상태**로 돌렸다. 설정 파일과 배치표를 다른 사람이 고치면 실제로 생기는 일이다.

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; EXTRA=-DconfigLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=4 ./run.sh fr ex2_check.c -- ex2 4'
```

```text
configMAX_SYSCALL=0x40 PendSV=0xf0 SysTick=0xf0 PRIGROUP=0
  BAD IMU_SPI_DMA irq5 byte=0x30 want 0x30 >= line 0x40
  BAD RC_UART_IDLE irq6 byte=0x30 want 0x30 >= line 0x40
  BAD CAN_RX irq7 byte=0x30 want 0x30 >= line 0x40
check #1: 3 problem(s)

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:803
[board_exit] code=253
```

선을 표와 맞춘(레벨 3, 기본 설정) 실행:

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; ./run.sh fr ex2_check.c -- ex2b 4'
```

```text
configMAX_SYSCALL=0x30 PendSV=0xf0 SysTick=0xf0 PRIGROUP=0
check #1: 0 problem(s)
pended 11 IRQs: zero-latency ran 3, kernel-aware gave 8
check #2 after two mistakes:
  BAD IMU_SPI_DMA irq5 byte=0x0 want 0x30 >= line 0x30
  BAD irq16 enabled but not in layout (byte=0x0)
check #2: 2 problem(s)
[board_exit] code=0
```

출력에서 볼 것:

- 첫 실행: 검사가 **세 원천이 선(0x40) 위에 있다**고 먼저 알려 준다. 그다음 실제로 IMU_SPI_DMA ISR이 `vTaskNotifyGiveFromISR`을 부르는 순간 FreeRTOS의 `port.c:803`(`configASSERT( ucCurrentPriority >= ucMaxSysCallPriority )`)이 멈춘다. **configASSERT는 그 IRQ가 실제로 올 때만 잡는다** — 1 Hz짜리 원천이면 비행 중에야 터진다. 부팅 검사는 모든 원천을 시작할 때 한 번에 본다.
- 두 번째 실행: 문제 0개. ZL 3개는 커널을 안 부르고 실행, KA 8개는 `vTaskNotifyGiveFromISR`을 8번(통지 값 = 8) 불렀고 assert가 없다.
- 실수 두 개: `IPR8(5) = 3`(시프트 없이 레벨 번호)은 4비트 칩에서 **0x00 = 최상위**가 된다. "IRQ 16을 켰는데 우선순위를 안 정함"도 0x00이다. 둘 다 KA 원천이 **선 위 최상위**로 올라가는, 가장 흔하고 가장 위험한 실수다(B3 §3의 함정이 시스템 수준에서 나타난 모양).
- 실무 팁: 이 검사를 `configASSERT`와 같이 **릴리스 빌드에도** 남겨 두면 좋다. 비용은 부팅 때 수십 µs다. 검사 표(`L[]`)를 예제 2의 도구가 생성한 헤더에서 만들면 배치표 문서와 코드가 어긋날 수 없다.

---

## 4. FOC 모터 컨트롤러 배치표 — 20 kHz 전류 루프를 선 위에

> 🔁 이 절은 Bare-Metal BM J8(모터 · 고속 ISR 배치)과 겹친다 (주인: 이 노트).

### 4.1 무엇이 다른가

로봇 관절의 FOC(field-oriented control) 컨트롤러는 PWM 주기마다(20 kHz = 50 µs) ADC가 상전류를 샘플하고, ISR이 Clarke · Park 변환 → 전류 PI → 역변환 → SVPWM 듀티 갱신을 **끝까지** 한다. 듀티는 다음 PWM 주기 경계(섀도 레지스터 로드)까지 써야 하므로 마감이 단단하다. 지터는 토크 리플로 바로 나타난다. 그 위에 속도 · 위치 루프(1–4 kHz), CAN 명령, 진단이 있다.

A7 §7은 "전류 루프를 RTOS 태스크로 깨우면 25 MHz에서 깨우는 데만 주기의 14%"를 쟀고, 그래서 전류 루프는 **ISR 안에서 끝까지**, 그리고 **선 위**에 둔다. 이 절은 그 결정을 배치표로 만들고 문지기 경로를 실제로 돌린다.

| 레벨 (바이트) | 원천 | 세계 | 근거 |
|---|---|---|---|
| 0 (0x00) | 과전류 · 게이트 드라이버 fault | ZL | PWM은 타이머 break 입력이 하드웨어로 이미 끈다. ISR은 상태 래치 · 원인 기록만 |
| 1 (0x10) | ADC 주입 변환 완료 (20 kHz) | ZL | 전류 루프 전체. C ≈ 8–15 µs, 마감 = 다음 PWM 섀도 로드 |
| 2 (0x20) | 엔코더 index · 홀 엣지 캡처 | ZL | 각도 기준 갱신. 타이머 엔코더 모드면 ISR 자체가 없다 |
| 선 | `configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x30` | | |
| 3 (0x30) | 문지기 IRQ (2 kHz) | KA | 전류 루프가 pend → 속도 루프 태스크 통지 |
| 4 (0x40) | CAN FD RX (1 kHz 명령) | KA | FIFO 3단, tol 수백 µs |
| 5 (0x50) | UART 진단 · 로그 DMA | KA | ms 단위 |
| 15 (0xF0) | PendSV, SysTick | 커널 | |

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">로봇 관절 FOC 모터 컨트롤러 배치표 (4비트, 선 = 레벨 3)</text>
<rect x="10.0" y="40.0" width="90.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="55.0" y="62.2" font-size="12" text-anchor="middle">0  0x00</text>
<rect x="110.0" y="40.0" width="560.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120.0" y="62.2" font-size="12" text-anchor="start">과전류 · 게이트 드라이버 fault 래치 (PWM은 HW break가 이미 끔)</text>
<rect x="10.0" y="84.0" width="90.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="55.0" y="106.2" font-size="12" text-anchor="middle">1  0x10</text>
<rect x="110.0" y="84.0" width="560.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120.0" y="106.2" font-size="12" text-anchor="start">ADC 주입 변환 완료 20 kHz → FOC 전류 루프 끝까지</text>
<rect x="10.0" y="128.0" width="90.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="55.0" y="150.2" font-size="12" text-anchor="middle">2  0x20</text>
<rect x="110.0" y="128.0" width="560.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120.0" y="150.2" font-size="12" text-anchor="start">엔코더 index · 홀 엣지 캡처</text>
<line x1="10.0" y1="169.0" x2="670.0" y2="169.0" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/>
<rect x="10.0" y="178.0" width="90.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="55.0" y="200.2" font-size="12" text-anchor="middle">3  0x30</text>
<rect x="110.0" y="178.0" width="560.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120.0" y="200.2" font-size="12" text-anchor="start">문지기 IRQ (2 kHz) → 속도 · 위치 루프 태스크 깨우기</text>
<rect x="10.0" y="222.0" width="90.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="55.0" y="244.2" font-size="12" text-anchor="middle">4  0x40</text>
<rect x="110.0" y="222.0" width="560.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120.0" y="244.2" font-size="12" text-anchor="start">CAN FD RX (명령 1 kHz, FIFO 3단)</text>
<rect x="10.0" y="266.0" width="90.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="55.0" y="288.2" font-size="12" text-anchor="middle">5  0x50</text>
<rect x="110.0" y="266.0" width="560.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120.0" y="288.2" font-size="12" text-anchor="start">UART 진단 · 로그 DMA</text>
<rect x="10.0" y="310.0" width="90.0" height="36.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="55.0" y="332.2" font-size="12" text-anchor="middle">15  0xF0</text>
<rect x="110.0" y="310.0" width="560.0" height="36.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="332.2" font-size="12" text-anchor="start">PendSV · SysTick</text>
<text x="10.0" y="372.0" font-size="12" text-anchor="start">태스크: 속도 루프(5) &gt; CAN 명령 처리(4) &gt; 진단 · 로그(1). FOC ISR은 링 + 문지기로만 바깥과 말한다.</text>
</svg>
```

그림 4 — FOC 모터 컨트롤러 배치표. 전류 루프가 레벨 1에 있고, 바깥과는 링 + 문지기(레벨 3)로만 말한다.

왜 과전류가 전류 루프보다 위인가? 과전류 ISR은 드물고 짧으며, 전류 루프 한가운데(8 µs)에서 기다리게 하면 그동안 상태를 놓칠 수 있다. 하지만 **실제 보호는 하드웨어(타이머 break)가 한다** — ISR 우선순위로 안전을 만들지 않는다(O 모듈의 원칙: 안전 기능은 소프트웨어 지연에 기대지 않는다).

### 예제 4 — 문지기 패턴: FOC ISR → 락 없는 링 → STIR → 속도 루프 태스크

무엇을 확인하나: DUALTIMER가 20 kHz로 FOC ISR(레벨 1, ZL)을 부른다. ISR은 8 µs 계산을 흉내 내고 샘플(순번, 시각)을 SPSC 링에 넣고, 10번에 한 번(2 kHz) 빈 IRQ 3을 STIR로 pend한다. 문지기 ISR(레벨 3, KA)은 `vTaskNotifyGiveFromISR`로 속도 루프 태스크(우선순위 5)를 깨우고, 태스크는 링을 비우며 순번이 빠졌는지 검사한다. 동시에 우선순위 1의 logger 태스크가 길이 1–20 µs의 `taskENTER_CRITICAL()` 구간을 무작위로 반복한다(무작위가 아니면 icount의 결정성 때문에 위상이 한 곳에 고정되어 최악을 못 본다 — 실제로 처음엔 그랬다). 4000 FOC 주기(0.2 s) 동안 ① FOC 진입 지연, ② STIR → 문지기, ③ 문지기 → 태스크 최댓값을 잰다.

```c
/* ex3 — FOC 20 kHz zero-latency ISR -> lock-free ring -> STIR doorbell (kernel-aware) -> task */
#include "FreeRTOS.h"
#include "task.h"
#include "e2.h"
#define IRQ_BELL 3u                         /* spare line used only as a software doorbell */
#define RING 64u
typedef struct { uint32_t seq, t; } smp_t;
static smp_t ring[RING];
static volatile uint32_t head, tail, ovf;   /* head: ISR writes, tail: task writes */
static volatile uint32_t t_bell, t_bell_isr, foc_lat_max, bell_lat_max, task_lat_max, lost, nfoc;
static TaskHandle_t h_speed;

void DUALTIMER_IRQHandler(void)            /* level 1 (0x10): ZERO-LATENCY, no kernel API */
{
    uint32_t lat = DT_LOAD - DT_VALUE, now = CYC(); DT_INTCLR = 1;
    if (lat > foc_lat_max) foc_lat_max = lat;
    spin_cyc(200);                          /* ~8 us: Clarke/Park, PI, SVPWM */
    uint32_t hd = head;
    if (hd - __atomic_load_n(&tail, __ATOMIC_ACQUIRE) < RING) {
        ring[hd % RING] = (smp_t){ nfoc, now };
        __atomic_store_n(&head, hd + 1, __ATOMIC_RELEASE);
    } else ovf++;
    if (++nfoc % 10u == 0) { t_bell = CYC(); STIR = IRQ_BELL; }   /* 2 kHz doorbell */
}
static void bell_isr(void)                  /* level 3 (0x30): kernel-aware bridge */
{
    t_bell_isr = CYC();
    if (t_bell_isr - t_bell > bell_lat_max) bell_lat_max = t_bell_isr - t_bell;
    BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(h_speed, &w); portYIELD_FROM_ISR(w);
}
static void speed_task(void *a)              /* 2 kHz speed loop, priority 5 */
{
    (void)a; uint32_t expect = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t d = CYC() - t_bell_isr; if (d > task_lat_max) task_lat_max = d;
        while (tail != __atomic_load_n(&head, __ATOMIC_ACQUIRE)) {
            smp_t s = ring[tail % RING];
            if (s.seq != expect) lost += s.seq - expect;
            expect = s.seq + 1;
            __atomic_store_n(&tail, tail + 1, __ATOMIC_RELEASE);
        }
        if (expect >= 4000) break;
    }
    DT_CTRL = 0;
    board_printf_raw("FOC cycles=%u  lost=%u  ring overflow=%u\n", expect, lost, ovf);
    board_printf_raw("FOC ISR entry latency max      %u ns\n", foc_lat_max * 40);
    board_printf_raw("STIR -> doorbell ISR max        %u ns\n", bell_lat_max * 40);
    board_printf_raw("doorbell ISR -> speed task max  %u ns\n", task_lat_max * 40);
    board_exit(0);
}
static void logger_task(void *a)            /* priority 1: random 1..20 us critical sections */
{
    (void)a; uint32_t x = 1;
    for (;;) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;  /* xorshift: break phase locking */
        taskENTER_CRITICAL(); spin_us(1 + x % 20u); taskEXIT_CRITICAL(); spin_us(1 + (x >> 8) % 4u);
    }
}
int main(void)
{
    vec_install(); vec_set(IRQ_BELL, bell_isr);
    irq_prio(IRQ_BELL, 3); irq_on_n(IRQ_BELL);
    DT_LOAD = 1250 - 1; irq_prio(10, 1); irq_on_n(10);       /* 50 us = 20 kHz */
    xTaskCreate(speed_task, "spd", 512, NULL, 5, &h_speed);
    xTaskCreate(logger_task, "log", 256, NULL, 1, NULL);
    DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u;                 /* enable, periodic, IRQ, 32-bit */
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; ./run.sh fr ex3_doorbell.c -- ex3 4 60'
```

```text
FOC cycles=4000  lost=0  ring overflow=0
FOC ISR entry latency max      200 ns
STIR -> doorbell ISR max        21280 ns
doorbell ISR -> speed task max  8400 ns
[board_exit] code=0
```

출력에서 볼 것:

- **샘플 4000개, 손실 0, 링 넘침 0.** 락이 하나도 없는데 생산자(ISR)와 소비자(태스크)가 안전하게 주고받았다. `head`는 ISR만, `tail`은 태스크만 쓰고, acquire/release로 순서를 지킨다(D8 §5).
- **FOC 진입 지연 최대 200 ns** — logger가 4000 주기 내내 임계구역을 들락거렸는데도 커널이 더한 지연이 없다.
- **STIR → 문지기 최대 21.3 µs** — 문지기는 선 아래(0x30)라서 logger의 임계구역(BASEPRI 0x30)이 끝나기를 기다린다. 그런데 logger의 임계구역은 최대 20 µs인데 왜 21.3인가? 디버그 계측을 넣어 보니 **최악의 경우 FOC가 logger가 BASEPRI를 올린 직후에 들어왔다**. FOC ISR이 쓴 8 µs는 logger의 20 µs 바쁜 대기에 포함되지 않으므로(바쁜 대기는 벽시계로 20 µs를 센다) 임계구역이 실제로 약 28.5 µs 동안 열려 있었고, STIR은 그 8 µs 지점에서 일어났다. 일반화하면: **선 아래 ISR의 최악 대기 = 가장 긴 임계구역 + 그 안에 끼어드는 ZL ISR 시간.** 예제 2의 BK에는 이것까지 넣어야 한다.
- **문지기 → 태스크 최대 8.4 µs** — PendSV를 거친 컨텍스트 스위치 + 그 사이에 끼어든 FOC ISR(8 µs). 속도 루프(500 µs 주기)에 비하면 작다.

```svg
<svg viewBox="0 0 680 318" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">문지기 경로의 최악 장면 — 예제 4 실측 최댓값에 맞춘 개략도 (µs)</text>
<text x="10.0" y="66.0" font-size="12" text-anchor="start">logger (임계구역)</text>
<text x="10.0" y="106.0" font-size="12" text-anchor="start">FOC ISR (0x10)</text>
<text x="10.0" y="146.0" font-size="12" text-anchor="start">문지기 ISR (0x30)</text>
<text x="10.0" y="186.0" font-size="12" text-anchor="start">속도 루프 태스크</text>
<rect x="130.0" y="50.0" width="110.5" height="22.0" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<rect x="240.5" y="50.0" width="260.0" height="22.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="132.6" y="90.0" width="106.6" height="22.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="516.1" y="130.0" width="16.9" height="22.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="539.5" y="170.0" width="85.8" height="22.0" fill="#888" fill-opacity="0.6" stroke="#888"/>
<line x1="239.2" y1="112.0" x2="239.2" y2="138.0" stroke="#d0564a" stroke-width="1.2" marker-end="url(#ah5)"/>
<text x="243.2" y="128.0" font-size="12" text-anchor="start">STIR</text>
<line x1="239.2" y1="150.0" x2="514.8" y2="150.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/>
<text x="377.0" y="164.0" font-size="12" text-anchor="middle">21.3 µs: BASEPRI 0x30에 막힘</text>
<line x1="533.0" y1="200.0" x2="625.3" y2="200.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/>
<text x="578.5" y="216.0" font-size="12" text-anchor="middle">≤ 8.4 µs</text>
<line x1="130.0" y1="236.0" x2="650.0" y2="236.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="130.0" y1="236.0" x2="130.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="130.0" y="255.0" font-size="12" text-anchor="middle">0</text>
<line x1="195.0" y1="236.0" x2="195.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="195.0" y="255.0" font-size="12" text-anchor="middle">5</text>
<line x1="260.0" y1="236.0" x2="260.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="260.0" y="255.0" font-size="12" text-anchor="middle">10</text>
<line x1="325.0" y1="236.0" x2="325.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="325.0" y="255.0" font-size="12" text-anchor="middle">15</text>
<line x1="390.0" y1="236.0" x2="390.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="390.0" y="255.0" font-size="12" text-anchor="middle">20</text>
<line x1="455.0" y1="236.0" x2="455.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="455.0" y="255.0" font-size="12" text-anchor="middle">25</text>
<line x1="520.0" y1="236.0" x2="520.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="520.0" y="255.0" font-size="12" text-anchor="middle">30</text>
<line x1="585.0" y1="236.0" x2="585.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="585.0" y="255.0" font-size="12" text-anchor="middle">35</text>
<line x1="650.0" y1="236.0" x2="650.0" y2="241.0" stroke="currentColor" stroke-width="1.2"/>
<text x="650.0" y="255.0" font-size="12" text-anchor="middle">40</text>
<text x="10.0" y="280.0" font-size="12" text-anchor="start">임계구역 = logger 코드 20 µs + 그 안에 끼어든 FOC 8 µs = 28.5 µs.</text>
<text x="10.0" y="300.0" font-size="12" text-anchor="start">ZL ISR이 쓴 시간도 선 아래 ISR의 대기에 그대로 더해진다.</text>
</svg>
```

그림 5 — 문지기 경로의 최악 장면을 예제 4의 최댓값에 맞춰 그린 개략도. 빨강(FOC)은 logger의 임계구역 한가운데로 그냥 들어오지만, 문지기(초록)는 임계구역이 끝날 때까지 21.3 µs 기다린다. 이 지연은 **속도 루프의 알림**에만 붙고 전류 루프에는 붙지 않는다.

### 예제 5 — Zephyr: ZLI 두 레벨 + 문지기

무엇을 확인하나: 같은 패턴을 Zephyr v4.4.2에서 `CONFIG_ZERO_LATENCY_LEVELS=2`로 만든다. IRQ 21(ZLI 레벨 1, "FOC")이 IRQ 20(ZLI 레벨 0, "과전류")을 pend해 **ZLI 안에서 ZLI가 중첩**되는지, IRQ 22(일반 IRQ, 문지기)를 pend해 `k_sem_give()`로 main 스레드를 깨우는지 본다. 이 모든 일을 `irq_lock()`으로 20 µs 잠근 구간 안에서 시작한다. 마지막으로 `arch_zli_lock()`(PRIMASK)이 ZLI까지 막는지도 본다.

```text
# zli2/prj.conf
CONFIG_BOOT_BANNER=n
CONFIG_ZERO_LATENCY_IRQS=y
CONFIG_ZERO_LATENCY_LEVELS=2
```

```c
/* ex9 — Zephyr v4.4.2: two ZLI levels + a doorbell IRQ that bridges into the kernel */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#define REG(a)  (*(volatile uint32_t *)(a))
#define CYC()   REG(0x40028018u)
#define IPR(n)  (*(volatile uint8_t *)(0xE000E400u + (n)))
#define SHPR(e) (*(volatile uint8_t *)(0xE000ED18u + (e) - 4u))
#define IRQ_STOP 20  /* ZLI level 0: e.g. overcurrent latch */
#define IRQ_FOC  21  /* ZLI level 1: current loop          */
#define IRQ_BELL 22  /* ordinary IRQ, prio 0: may use the kernel */
static K_SEM_DEFINE(sem, 0, 1);
static char log[16]; static volatile int nl;
static volatile uint32_t t_foc, t_bell, t_thr;

ISR_DIRECT_DECLARE(stop_isr) { log[nl++] = 'S'; return 0; }
ISR_DIRECT_DECLARE(foc_isr)
{
    log[nl++] = 'F'; t_foc = CYC();
    NVIC_SetPendingIRQ(IRQ_STOP);            /* higher ZLI level nests inside */
    __DSB(); __ISB();
    NVIC_SetPendingIRQ(IRQ_BELL);            /* ring the doorbell: no kernel call here */
    log[nl++] = 'f';
    return 0;                                /* ZLI: never reschedule */
}
static void bell_isr(const void *a) { ARG_UNUSED(a); log[nl++] = 'B'; t_bell = CYC(); k_sem_give(&sem); }

int main(void)
{
    IRQ_DIRECT_CONNECT(IRQ_STOP, 0, stop_isr, IRQ_ZERO_LATENCY);
    IRQ_DIRECT_CONNECT(IRQ_FOC, 1, foc_isr, IRQ_ZERO_LATENCY);
    IRQ_CONNECT(IRQ_BELL, 0, bell_isr, NULL, 0);
    irq_enable(IRQ_STOP); irq_enable(IRQ_FOC); irq_enable(IRQ_BELL);
    printk("ZLI levels=%d  IRQ_PRIO_LOWEST=%d\n", CONFIG_ZERO_LATENCY_LEVELS, (int)IRQ_PRIO_LOWEST);
    printk("NVIC: STOP=0x%02x FOC=0x%02x BELL=0x%02x | SVC=0x%02x PendSV=0x%02x SysTick=0x%02x MemManage=0x%02x\n",
           IPR(IRQ_STOP), IPR(IRQ_FOC), IPR(IRQ_BELL), SHPR(11), SHPR(14), SHPR(15), SHPR(4));

    unsigned int key = irq_lock();
    uint32_t bp = __get_BASEPRI();
    NVIC_SetPendingIRQ(IRQ_FOC); __DSB(); __ISB();
    for (uint32_t t = CYC(); CYC() - t < 20u * 25u; ) { }   /* 20 us locked section */
    log[nl++] = '|';                         /* still inside irq_lock() */
    irq_unlock(key);
    k_sem_take(&sem, K_FOREVER); t_thr = CYC();
    log[nl] = 0;
    printk("irq_lock BASEPRI=0x%02x  order: %s   (| = irq_unlock)\n", bp, log);
    printk("FOC -> bell ISR %u ns (held by irq_lock), bell -> thread %u ns\n",
           (t_bell - t_foc) * 40u, (t_thr - t_bell) * 40u);

    nl = 0; key = arch_zli_lock();           /* PRIMASK: even ZLIs wait */
    NVIC_SetPendingIRQ(IRQ_FOC); __DSB(); __ISB();
    log[nl++] = '|'; arch_zli_unlock(key);
    k_sem_take(&sem, K_FOREVER); log[nl] = 0;
    printk("arch_zli_lock order: %s\n", log);
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'B=/var/tmp/rtos/E2; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/E2/zli2 $B/zli2 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $B/zli2/zephyr/zephyr.elf 10'
```

```text
           FLASH:       13764 B         4 MB      0.33%
             RAM:        6160 B         4 MB      0.15%
ZLI levels=2  IRQ_PRIO_LOWEST=4
NVIC: STOP=0x00 FOC=0x20 BELL=0x60 | SVC=0x40 PendSV=0xe0 SysTick=0x60 MemManage=0x00
irq_lock BASEPRI=0x60  order: FSf|B   (| = irq_unlock)
FOC -> bell ISR 22000 ns (held by irq_lock), bell -> thread 1680 ns
arch_zli_lock order: |FSfB
```

(빌드 경고 0개. 빌드 로그는 메모리 요약 두 줄만 남겼다. `.config`에는 `CONFIG_ZERO_LATENCY_IRQS=y`, `CONFIG_ZERO_LATENCY_LEVELS=2`, `CONFIG_CPU_CORTEX_M_HAS_PROGRAMMABLE_FAULT_PRIOS=y`가 있다.)

출력에서 볼 것:

- **바이트**: ZLI 두 레벨이 0x00, 0x20(인자 0, 1을 그대로 하드웨어 레벨로), SVC가 0x40으로 밀렸고, 일반 IRQ "0"이 0x60, `irq_lock()` BASEPRI도 0x60. §1.3 표의 LEVELS=2 행 그대로다. **MemManage도 0x00 = ZLI 레벨 0과 같다**(§1.3의 HardFault 승격 이야기).
- **순서 `FSf|B`**: lock 안에서 FOC(F)가 들어오고, 그 안에서 과전류(S)가 중첩되고, FOC가 끝나고(f), `irq_unlock`(|) 뒤에야 문지기(B)가 들어온다. 두 ZLI 레벨이 서로 선점하고, 둘 다 `irq_lock()`을 무시한다.
- **FOC → 문지기 22 µs**: 20 µs 잠금 구간 + 나머지. 예제 4의 FreeRTOS와 같은 모양이다.
- **`arch_zli_lock()` 순서 `|FSfB`**: PRIMASK라서 ZLI도 unlock까지 기다린다. Zephyr의 `arch_interface.h` 주석이 "This lock breaks the promise of zero latency interrupts"라고 경고한다. 트리 안에서 실제로 쓰는 곳은 `lib/os/reboot.c`, `lib/os/poweroff.c`, 그리고 nRF 드라이버들이다 — 예를 들어 `drivers/timer/nrf_rtc_timer.c`의 `full_int_lock()`은 `CONFIG_NRF_RTC_TIMER_LOCK_ZERO_LATENCY_IRQS`(ZLI를 켜면 기본 y)일 때 PRIMASK를 쓴다. Kconfig 도움말의 이유: "higher priority contexts (including ZLIs) that might preempt the handler and call nrf_rtc_timer API". **ZLI가 어떤 API를 부른다면 그 API는 ZLI까지 막는 잠금을 써야 한다** — 그 순간 그 ZLI는 더 이상 zero-latency가 아니다.

### 예제 6 — Zephyr 빌드가 잡아 주는 ZLI 실수, 그리고 못 잡는 실수

무엇을 확인하나: `CONFIG_ZERO_LATENCY_LEVELS=2`에서 세 가지 실수를 빌드한다. ① ZLI를 `IRQ_CONNECT`(공용 래퍼)로 등록, ② 예약하지 않은 레벨 2를 ZLI로 요청, ③ 일반 IRQ에 `IRQ_PRIO_LOWEST`(4)를 넘는 5를 요청. 빌드가 되면 실행해서 실제 바이트를 읽는다.

```c
/* ex10 — which ZLI mistakes the build catches (Zephyr v4.4.2, CONFIG_ZERO_LATENCY_LEVELS=2) */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
static void __maybe_unused norm_isr(const void *a) { ARG_UNUSED(a); }
ISR_DIRECT_DECLARE(zli_isr) { return 0; }
int main(void)
{
#if E2_BAD == 1
    IRQ_CONNECT(21, 0, norm_isr, NULL, IRQ_ZERO_LATENCY);      /* ZLI through the common wrapper */
#elif E2_BAD == 2
    IRQ_DIRECT_CONNECT(21, 2, zli_isr, IRQ_ZERO_LATENCY);      /* level 2 not reserved (LEVELS=2) */
#elif E2_BAD == 3
    IRQ_CONNECT(21, 5, norm_isr, NULL, 0);                     /* beyond IRQ_PRIO_LOWEST (4) */
#else
    IRQ_DIRECT_CONNECT(21, 1, zli_isr, IRQ_ZERO_LATENCY);
    IRQ_CONNECT(22, 4, norm_isr, NULL, 0);
#endif
    printk("IRQ21 byte=0x%02x SVC=0x%02x irq_lock BASEPRI=0x%02x\n", *(volatile uint8_t *)0xE000E415u,
           *(volatile uint8_t *)0xE000ED1Fu, (unsigned)_EXC_IRQ_DEFAULT_PRIO);
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'B=/var/tmp/rtos/E2; for n in 1 2 3 0; do echo "== E2_BAD=$n"; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/E2/zbad $B/zbad$n -- -DE2_BAD=$n 2>&1 | grep -o "static assertion failed: \"[^\"]*\"\|warning:.*\|FLASH:.*" | sort -u; [ -f $B/zbad$n/zephyr/zephyr.elf ] && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $B/zbad$n/zephyr/zephyr.elf 5; done'
```

```text
== E2_BAD=1
static assertion failed: "ZLI interrupts must be registered using IRQ_DIRECT_CONNECT()"
== E2_BAD=2
FLASH:       12572 B         4 MB      0.30%
IRQ21 byte=0x40 SVC=0x40 irq_lock BASEPRI=0x60
== E2_BAD=3
static assertion failed: "Invalid interrupt priority. Values must not exceed IRQ_PRIO_LOWEST"
== E2_BAD=0
FLASH:       12584 B         4 MB      0.30%
IRQ21 byte=0x20 SVC=0x40 irq_lock BASEPRI=0x60
```

(`CMakeLists.txt`에 `target_compile_definitions(app PRIVATE E2_BAD=${E2_BAD})`를 넣어 앱 파일에만 매크로를 줬다. 처음에 `BAD`라는 이름을 전역 `EXTRA_CFLAGS`로 줬더니 picolibc의 `regex2.h`가 같은 이름을 정의해 "BAD redefined" 경고가 났다 — 매크로 이름에 접두사를 붙이는 이유다.)

출력에서 볼 것:

- ①과 ③은 `BUILD_ASSERT`(`include/zephyr/arch/arm/irq.h`의 `ARCH_IRQ_CONNECT`와 `_CHECK_PRIO`)가 막는다. FreeRTOS라면 런타임 configASSERT(그것도 그 IRQ가 와야)로만 잡히는 종류다.
- **②는 빌드가 통과한다.** `_CHECK_PRIO`는 `(ZLI이고 (LEVELS == 1 또는 prio < LEVELS)) 또는 prio <= IRQ_PRIO_LOWEST`라서, ZLI가 레벨 2를 요청해도 뒤쪽 조건(2 ≤ 4)으로 통과한다. 그리고 `arm_irq_priority_set()`은 LEVELS > 1이면 인자를 그대로 쓰므로 실제 바이트는 0x40 = **SVC와 같은 레벨**이 된다. `irq_lock()`(0x60)보다는 위라서 "zero-latency처럼" 동작은 하지만, 예약한 ZLI 레벨이 아닌 SVC 레벨에 앉아 있다. v4.4.2 소스에서 확인한 동작이고, 설계 리뷰에서는 **ZLI 우선순위 인자가 `CONFIG_ZERO_LATENCY_LEVELS`보다 작은지 사람이 확인**해야 한다.

---

## 5. 선 위냐 아래냐 — 바쁜 커널 아래에서 잰다

> 🔁 이 절은 Bare-Metal BM J8(우선순위 배치의 결과)과 겹친다 (주인: 이 노트).

B3 예제 10과 C7 §6은 "임계구역 길이 L을 바꾸면 선 아래 IRQ의 최대 지연이 L을 따라간다"를 인공적인 스윕으로 쟀다. 여기서는 **배치 결정**을 잰다: 같은 FOC ISR을 ① 선 위(레벨 1), ② 선 아래 최상위(레벨 3, CAN은 레벨 4), ③ 선 아래 CAN 밑(레벨 4, CAN은 레벨 3)에 두고, 커널이 실제로 바쁜 상황에서 진입 지연 분포를 본다.

### 예제 7 — FOC 진입 지연 히스토그램: 세 가지 배치

무엇을 확인하나: FOC ISR(20 kHz, 8 µs)과 "CAN RX" ISR(TIMER0, 약 1 kHz, 파싱 30 µs 후 `xQueueSendFromISR`), CAN 프레임을 받는 태스크(우선순위 4), 무작위 1–20 µs 임계구역을 반복하는 logger(우선순위 1)가 함께 돈다. FOC ISR은 첫 줄에서 DUALTIMER의 `LOAD - VALUE`(0이 된 뒤 지난 사이클)로 진입 지연을 재고 히스토그램에 넣는다. 4000번.

```c
/* ex4 — FOC ISR entry-latency histogram under a busy kernel: above the line vs below it */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "e2.h"
#ifndef FOC_LVL
#define FOC_LVL 1                                /* 1 = zero-latency, 3/4 = kernel-aware */
#endif
#ifndef CAN_LVL
#define CAN_LVL 3
#endif
#define NB 7
static const uint32_t edge_us[NB] = { 1, 2, 5, 10, 20, 40, 1000 };
static volatile uint32_t hist[NB], nfoc, late, maxlat;
static QueueHandle_t q;

void DUALTIMER_IRQHandler(void)                 /* FOC current loop, 20 kHz */
{
    uint32_t lat = DT_LOAD - DT_VALUE; DT_INTCLR = 1;
    uint32_t ns = lat * 40u, i = 0;
    while (ns >= edge_us[i] * 1000u) i++;
    hist[i]++; if (ns > maxlat) maxlat = ns; if (ns > 10000u) late++;
    spin_cyc(200);                               /* ~8 us of math */
    nfoc++;
}
void TIMER0_IRQHandler(void)                    /* "CAN RX" 1 kHz, kernel-aware, 30 us of parsing */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    spin_us(30);
    uint32_t v = nfoc; BaseType_t w = pdFALSE;
    xQueueSendFromISR(q, &v, &w); portYIELD_FROM_ISR(w);
}
static void rx_task(void *a)                    /* priority 4: consumes CAN frames */
{
    (void)a; uint32_t v;
    for (;;) { xQueueReceive(q, &v, portMAX_DELAY); spin_us(5); }
}
static void logger_task(void *a)                /* priority 1: random kernel-style critical sections */
{
    (void)a; uint32_t x = 7;
    for (;;) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        taskENTER_CRITICAL(); spin_us(1 + x % 20u); taskEXIT_CRITICAL(); spin_us(1 + (x >> 8) % 4u);
        if (nfoc >= 4000) break;
    }
    DT_CTRL = 0; TIMER_CTRL(TIMER0_BASE) = 0;
    board_printf_raw("FOC lvl %u (0x%x) CAN lvl %u, line 0x%x:", FOC_LVL, PRIO(FOC_LVL), CAN_LVL, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    for (unsigned i = 0; i < NB; i++) board_printf_raw(" <%u:%u", edge_us[i], hist[i]);
    board_printf_raw("\n  max %u ns, late(>10us) %u of %u\n", maxlat, late, nfoc);
    board_exit(0);
}
int main(void)
{
    q = xQueueCreate(8, sizeof(uint32_t));
    irq_prio(10, FOC_LVL); irq_on_n(10); DT_LOAD = 1250 - 1;          /* 50 us */
    irq_prio(8, CAN_LVL); irq_on_n(8);
    TIMER_RELOAD(TIMER0_BASE) = 25000 + 37 - 1; TIMER_VALUE(TIMER0_BASE) = 25000 + 37 - 1;
    xTaskCreate(rx_task, "rx", 256, NULL, 4, NULL);
    xTaskCreate(logger_task, "log", 256, NULL, 1, NULL);
    DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u; TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; for c in "1 3" "3 4" "4 3"; do set -- $c; EXTRA="-DFOC_LVL=$1 -DCAN_LVL=$2" ./run.sh fr ex4_jitter.c -- ex4_$1$2 4 60; done'
```

```text
FOC lvl 1 (0x10) CAN lvl 3, line 0x30: <1:4000 <2:0 <5:0 <10:0 <20:0 <40:0 <1000:0
  max 80 ns, late(>10us) 0 of 4000
[board_exit] code=0
FOC lvl 3 (0x30) CAN lvl 4, line 0x30: <1:1268 <2:283 <5:701 <10:940 <20:791 <40:17 <1000:0
  max 21080 ns, late(>10us) 803 of 4000
[board_exit] code=0
FOC lvl 4 (0x40) CAN lvl 3, line 0x30: <1:1135 <2:282 <5:695 <10:864 <20:905 <40:96 <1000:23
  max 49240 ns, late(>10us) 1021 of 4000
[board_exit] code=0
```

(`<1:4000`은 "1 µs 미만 4000번", `<1000`은 "40 µs 이상" 칸이다.)

```svg
<svg viewBox="0 0 680 318" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">FOC ISR 진입 지연 분포 — 바쁜 커널 아래 4000회 (예제 7 실측, QEMU icount)</text>
<line x1="70.0" y1="270.0" x2="660.0" y2="270.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="70.0" y1="270.0" x2="70.0" y2="60.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="65.0" y1="270.0" x2="70.0" y2="270.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="65.0" y1="220.0" x2="70.0" y2="220.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="224.0" font-size="12" text-anchor="end">1000</text>
<line x1="65.0" y1="170.0" x2="70.0" y2="170.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="174.0" font-size="12" text-anchor="end">2000</text>
<line x1="65.0" y1="120.0" x2="70.0" y2="120.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="124.0" font-size="12" text-anchor="end">3000</text>
<line x1="65.0" y1="70.0" x2="70.0" y2="70.0" stroke="currentColor" stroke-width="1.2"/>
<text x="62.0" y="74.0" font-size="12" text-anchor="end">4000</text>
<text x="112.1" y="288.0" font-size="12" text-anchor="middle">&lt;1</text>
<rect x="79.1" y="70.0" width="20.0" height="200.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="101.1" y="206.6" width="20.0" height="63.4" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="123.1" y="213.2" width="20.0" height="56.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="196.4" y="288.0" font-size="12" text-anchor="middle">1–2</text>
<rect x="185.4" y="255.8" width="20.0" height="14.1" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="207.4" y="255.9" width="20.0" height="14.1" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="280.7" y="288.0" font-size="12" text-anchor="middle">2–5</text>
<rect x="269.7" y="234.9" width="20.0" height="35.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="291.7" y="235.2" width="20.0" height="34.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="365.0" y="288.0" font-size="12" text-anchor="middle">5–10</text>
<rect x="354.0" y="223.0" width="20.0" height="47.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="376.0" y="226.8" width="20.0" height="43.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="449.3" y="288.0" font-size="12" text-anchor="middle">10–20</text>
<rect x="438.3" y="230.4" width="20.0" height="39.6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="460.3" y="224.8" width="20.0" height="45.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="533.6" y="288.0" font-size="12" text-anchor="middle">20–40</text>
<rect x="522.6" y="269.1" width="20.0" height="1.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="532.6" y="265.1" font-size="12" text-anchor="middle">17</text>
<rect x="544.6" y="265.2" width="20.0" height="4.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="554.6" y="261.2" font-size="12" text-anchor="middle">96</text>
<text x="617.9" y="288.0" font-size="12" text-anchor="middle">≥40</text>
<rect x="628.9" y="268.9" width="20.0" height="1.1" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="638.9" y="264.9" font-size="12" text-anchor="middle">23</text>
<text x="365.0" y="308.0" font-size="12" text-anchor="middle">진입 지연 (µs)</text>
<rect x="330.0" y="44.0" width="14.0" height="12.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="350.0" y="55.0" font-size="12" text-anchor="start">레벨 1 (선 위)</text>
<rect x="330.0" y="64.0" width="14.0" height="12.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="350.0" y="75.0" font-size="12" text-anchor="start">레벨 3 (선 아래 최상위)</text>
<rect x="330.0" y="84.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="350.0" y="95.0" font-size="12" text-anchor="start">레벨 4 (CAN 아래)</text>
<text x="330.0" y="122.0" font-size="12" text-anchor="start">최대: 0.08 / 21.1 / 49.2 µs</text>
<text x="330.0" y="140.0" font-size="12" text-anchor="start">10 µs 초과: 0 / 803 / 1021 회</text>
</svg>
```

그림 6 — 예제 7 실측. 빨강(선 위)은 4000번 전부 1 µs 미만, 초록(선 아래 최상위)은 임계구역이 끝나기를 기다리며 최대 21 µs, 파랑(CAN 아래)은 CAN ISR 30 µs까지 더해져 최대 49 µs.

출력에서 볼 것:

- **선 위**: 최대 80 ns. 커널 임계구역, CAN ISR, 큐 연산, 컨텍스트 스위치 어느 것도 FOC를 늦추지 못한다. 10 µs 넘은 횟수 0.
- **선 아래 최상위**: 최대 21.1 µs ≈ logger의 최장 임계구역 20 µs + 탈출 비용. 4000번 중 803번(20%)이 10 µs를 넘었다. 전류 루프 마감(예: ISR 시작이 10 µs 안)이라면 **다섯 번에 한 번 마감을 놓친다**. 평균만 보면 괜찮아 보이는(1 µs 미만이 1268번) 분포라서 더 위험하다.
- **CAN 아래**: 최대 49.2 µs = 임계구역 20 + CAN ISR 30. 40 µs 넘은 23번은 정확히 "CAN ISR이 임계구역 직후에 시작된" 경우다. **주기가 50 µs인 ISR이 49 µs 늦게 시작**했다 — 한 번만 더 늦으면 pending 비트가 합쳐져 한 주기를 통째로 잃는다(§8 예제 10).
- 결론: **"선 아래 최상위"는 선 위의 대용이 아니다.** 선 아래 최상위의 최악값은 임계구역이 정한다. 선 위로 올리는 것만이 커널을 지터 원천에서 빼는 방법이다.

---

## 6. 중첩 깊이 · 스택 · PRIGROUP — 레벨 수는 공짜가 아니다

> 🔁 이 절은 Bare-Metal BM J8(선점 그룹 · 중첩)과 겹친다 (주인: 이 노트).

### 6.1 직관 — 레벨 하나 = MSP 위의 프레임 하나

B2 §7.2가 정리한 규칙: 서로 다른 **선점(그룹) 레벨**의 수만큼만 중첩이 깊어질 수 있고, 같은 레벨끼리는 선점하지 않는다(뒤에 온 것은 tail-chain으로 이어진다). 중첩이 한 단계 깊어질 때마다 하드웨어 프레임(8워드 = 32 B, 8바이트 정렬 패딩 4 B 가능, FPU 문맥이 살아 있으면 26워드 + 정렬 = 104 B — B2 예제 12, B6)과 그 ISR의 스택 사용량이 MSP에 쌓인다.

RTOS에서는 이게 더 중요하다. FreeRTOS · Zephyr 모두 태스크는 PSP, ISR은 MSP를 쓴다. 태스크 실행 중 첫 예외의 프레임은 **태스크 스택(PSP)**에 쌓이고, 그 위로 중첩되는 모든 것은 **MSP 하나**에 쌓인다. FreeRTOS ARM_CM3 포트는 스케줄러 시작 때 `prvPortStartFirstTask()`에서 MSP를 벡터 테이블 0번 값(스택 꼭대기)으로 되돌려 main()이 쓰던 스택을 ISR 전용으로 재활용한다. 그러니 **MSP 크기 = ISR 중첩 최악값**이다(링커 스크립트의 `_Main_Stack_Size`, lab은 0x1000 = 4 KB).

### 예제 8 — 중첩 깊이 vs MSP 사용량 vs 가장 급한 IRQ의 대기

무엇을 확인하나: IRQ 2–7(6개)에 같은 몸체의 ISR을 꽂는다. 각 ISR은 지역 배열 64 B를 쓰고, 다음 IRQ를 pend한 뒤 3 µs 일한다. MSP 아래 2 KB를 0xDEADBEEF로 칠해 두고 끝난 뒤 칠이 벗겨진 깊이(high-water mark)를 잰다. 세 가지 배치를 비교한다: ① 6개 모두 다른 레벨(다음 것이 항상 더 급함), ② 같은 레벨값에 PRIGROUP 5(4비트 칩에서 그룹 2비트 + 서브 2비트 → 레벨 1–3과 4–6이 각각 한 그룹), ③ 6개 모두 같은 레벨. 가장 급한 마지막 IRQ가 pend된 뒤 시작까지 기다린 시간도 잰다.

```c
/* ex5 — nesting depth vs MSP usage vs latency: 6 IRQs, distinct levels / PRIGROUP groups / one level */
#include "e2.h"
#define N 6
static volatile uint32_t t_pend_top, t_top, depth, maxdepth;
static char order[2 * N + 1]; static volatile unsigned no;
static void body(unsigned k)                    /* IRQ 2+k: 64 B of locals, then pend the next one */
{
    volatile uint32_t buf[16];
    if (k == N - 1) t_top = CYC();              /* first thing the top IRQ does */
    if (++depth > maxdepth) maxdepth = depth;
    order[no++] = (char)('a' + k);
    for (unsigned i = 0; i < 16; i++) buf[i] = i * k;
    if (k + 1 < N) { if (k + 1 == N - 1) t_pend_top = CYC(); irq_pend(2 + k + 1); }
    spin_us(3);
    (void)buf[3]; order[no++] = (char)('A' + k); depth--;
}
#define ISR(k) static void isr##k(void) { body(k); }
ISR(0) ISR(1) ISR(2) ISR(3) ISR(4) ISR(5)
static const vec_t tab[N] = { isr0, isr1, isr2, isr3, isr4, isr5 };

static uint32_t run(const char *name, uint32_t prigroup, const uint8_t lvl[N])
{
    AIRCR = (0x05FAu << 16) | (prigroup << 8);
    for (unsigned k = 0; k < N; k++) irq_prio(2 + k, lvl[k]);
    uint32_t *sp = (uint32_t *)get_msp(), *lo = sp - 512;
    for (uint32_t *p = lo; p < sp - 8; p++) *p = 0xDEADBEEFu;     /* paint 2 KB below SP */
    depth = maxdepth = 0; no = 0;
    irq_pend(2); __asm volatile("dsb\n isb");
    while (no < 2 * N) { }
    uint32_t *p = lo; while (*p == 0xDEADBEEFu) p++;
    uint32_t used = (uint32_t)(sp - 8 - p) * 4u + 32u;
    order[no] = 0;
    board_printf_raw("%s PRIGROUP=%u depth=%u MSP=%u B  top-IRQ wait=%u ns  order=%s\n", name,
                     prigroup, maxdepth, used, (t_top - t_pend_top) * 40u, order);
    return used;
}
int main(void)
{
    vec_install();
    for (unsigned k = 0; k < N; k++) { vec_set(2 + k, tab[k]); irq_on_n(2 + k); }
    static const uint8_t distinct[N] = { 6, 5, 4, 3, 2, 1 };   /* each one preempts the previous */
    static const uint8_t same[N]     = { 4, 4, 4, 4, 4, 4 };
    run("distinct ", 0, distinct);
    run("groups/4 ", 5, distinct);          /* 4-bit: PRIGROUP 5 -> 2 group bits, 2 sub bits */
    run("one level", 0, same);
    AIRCR = (0x05FAu << 16);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; ./run.sh bm ex5_nest.c -- ex5 4'
```

```text
distinct  PRIGROUP=0 depth=6 MSP=672 B  top-IRQ wait=480 ns  order=abcdefFEDCBA
groups/4  PRIGROUP=5 depth=2 MSP=224 B  top-IRQ wait=4080 ns  order=aAbBcdDeEfFC
one level PRIGROUP=0 depth=1 MSP=112 B  top-IRQ wait=4160 ns  order=aAbBcCdDeEfF
[board_exit] code=0
```

레벨 하나가 왜 112 B인지 ISR 몸체의 프롤로그로 확인했다.

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/E2/ex5/app.elf | awk "/<body>:/,/^$/" | head -6'
```

```asm
000000c0 <body>:
      c0:	push	{r4, r5, r6}
      c2:	cmp	r0, #5
      c4:	sub	sp, #68	@ 0x44
      c6:	bne.n	d0 <body+0x10>
      c8:	ldr	r2, [pc, #140]	@ (158 <body+0x98>)
```

(각 `isrN`은 `movs r0, #N; b.w body`로 꼬리 호출이라 자기 프레임이 없다.)

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">중첩 깊이 · MSP 사용량 · 최상위 IRQ 대기 (예제 8 실측)</text>
<text x="20.0" y="50.0" font-size="12" text-anchor="start" font-weight="bold">MSP 사용 (바이트)</text>
<text x="380.0" y="50.0" font-size="12" text-anchor="start" font-weight="bold">최상위 IRQ 대기 (µs)</text>
<text x="20.0" y="84.0" font-size="12" text-anchor="start">6개 레벨 모두 다름 — 깊이 6</text>
<rect x="20.0" y="92.0" width="300.0" height="20.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="326.0" y="107.0" font-size="12" text-anchor="start">672</text>
<rect x="380.0" y="92.0" width="26.5" height="20.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="412.5" y="107.0" font-size="12" text-anchor="start">0.48</text>
<text x="20.0" y="144.0" font-size="12" text-anchor="start">PRIGROUP 5: 2그룹 — 깊이 2</text>
<rect x="20.0" y="152.0" width="100.0" height="20.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="126.0" y="167.0" font-size="12" text-anchor="start">224</text>
<rect x="380.0" y="152.0" width="225.6" height="20.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="611.6" y="167.0" font-size="12" text-anchor="start">4.08</text>
<text x="20.0" y="204.0" font-size="12" text-anchor="start">모두 같은 레벨 — 깊이 1</text>
<rect x="20.0" y="212.0" width="50.0" height="20.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="76.0" y="227.0" font-size="12" text-anchor="start">112</text>
<rect x="380.0" y="212.0" width="230.0" height="20.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="616.0" y="227.0" font-size="12" text-anchor="start">4.16</text>
<text x="20.0" y="268.0" font-size="12" text-anchor="start">레벨 하나 = 112 B (HW 프레임 32 + push 12 + 지역 변수 68). 중첩을 줄이면 스택은 줄고</text>
<text x="20.0" y="288.0" font-size="12" text-anchor="start">가장 급한 ISR이 덜 급한 ISR이 끝나기를 기다린다 — 스택과 지연의 교환.</text>
</svg>
```

그림 7 — 예제 8 실측. 레벨을 줄이면 MSP는 1/6로 줄고, 가장 급한 IRQ의 대기는 0.48 µs → 4 µs로 는다.

출력에서 볼 것:

- **레벨당 112 B = 하드웨어 프레임 32 + `push {r4, r5, r6}` 12 + 지역 변수 68**(64 B 배열 + 정렬). 깊이 6 → 672 B, 깊이 2 → 224 B, 깊이 1 → 112 B. 정확히 비례한다.
- 순서 문자열(소문자 = 진입, 대문자 = 끝): ① `abcdefFEDCBA` — 끝까지 중첩. ② `aAbBcdDeEfFC` — 같은 그룹(a, b, c)끼리는 줄을 서고, 다른 그룹(d)은 c 안으로 들어가고, d · e · f끼리는 다시 줄을 선다. ③ `aAbBcCdDeEfF` — 전부 줄을 선다(tail-chain).
- **대가**: 가장 급한 IRQ의 대기가 0.48 µs → 4.08 µs. 앞 ISR의 남은 일(3 µs + 몸체)을 기다린다. **스택과 지연을 교환한 것**이다. 예제 2의 ⑤단계("서로 기다려 줄 수 있으면 묶는다")가 바로 이 교환을 tol로 판정하는 규칙이다.

### 6.2 MSP 크기 계산 — 배치표에서 바로 나온다

```
MSP_worst = Σ over preemption levels L used by ISRs (and the kernel level):
              frame(L) + max_stack(ISR at level L)
frame(L)  = 32 B (no FP context)  /  104 B (FP context active, lazy stacking reserves it)
            + 4 B possible alignment padding
```

드론 표(레벨 0–4 + 커널 레벨 15 = 6단)에 ISR마다 최대 200 B, 모두 FPU를 쓸 수 있다고 하면 6 × (104 + 4 + 200) ≈ 1.8 KB. 여기에 HardFault 핸들러가 크래시 덤프를 쓰는 스택과 여유 50%를 더해 3 KB 정도로 잡는다. ISR 하나의 최대 스택은 `-fstack-usage`(GCC의 `.su` 파일)와 호출 그래프로 정적으로 구하고, 실측은 예제 8처럼 칠하기로 한다(F3에서 자세히). **레벨을 하나 더 쓰기로 할 때마다 이 식에 한 줄이 더해진다** — 배치표 리뷰에서 "이 레벨이 정말 필요한가"를 묻는 이유다.

### 6.3 PRIGROUP — 설계 선택으로서의 서브 우선순위

PRIGROUP은 우선순위 바이트를 "그룹(선점)" 비트와 "서브(같은 그룹 안의 줄 서기 순서)" 비트로 나눈다(B3 §5). 설계 도구로 보면:

- **장점**: 레벨값은 그대로 두고 중첩만 줄인다(예제 8 ②). 같은 그룹 안에서 동시에 pending이면 서브 우선순위가 높은 것이 먼저 나간다 — "선점은 안 하되 줄은 먼저 선다".
- **FreeRTOS에서는 쓸 수 없다.** ARM_CM3/M4/M7/M33 포트는 `xPortStartScheduler()`에서 구현 비트 수로 `ulMaxPRIGROUPValue`를 계산하고, `vPortValidateInterruptPriority()`가 `configASSERT( ( portAIRCR_REG & portPRIORITY_GROUP_MASK ) <= ulMaxPRIGROUPValue )`(`port.c:819`)로 **모든 비트가 선점 비트**인지 확인한다. 소스 주석: "For simplicity all bits must be defined to be pre-emption priority bits." 이유는 BASEPRI가 **그룹 우선순위로** 비교되기 때문이다 — 서브 비트가 있으면 선의 의미가 흐려진다(B3 예제 5 · §5.2). STM32Cube HAL에서는 `HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4)`(4비트 전부 선점)이 이 조건이다.
- **Zephyr**는 아키텍처 코드에서 PRIGROUP을 건드리지 않는다(v4.4.2 `arch/arm/core/cortex_m/`와 `include/zephyr/arch/arm/`에 `PRIGROUP` · `PriorityGrouping` grep 결과 없음) → 리셋값 0 = 모든 비트 선점으로 쓴다.
- 그래서 **RTOS에서는 "같은 레벨에 묶기"가 PRIGROUP의 대용**이다. 같은 레벨 안의 줄 서기 순서는 서브 우선순위 대신 **IRQ 번호**(작은 번호가 먼저)가 정한다 — 같은 레벨에 묶을 때 더 급한 쪽이 번호가 작은지 확인하거나, 순서가 중요하지 않은 것만 묶는다.
- RTOS가 없는 bare-metal에서는 PRIGROUP이 여전히 쓸 만한 도구다 — Bare-Metal 노트가 CMSIS `NVIC_SetPriorityGrouping()` · `NVIC_EncodePriority()`로 이 관점을 더한다.

---

## 7. 서로 다른 레벨이 공유하는 자원 — ISR 사이의 우선순위 역전

> 🔁 이 절은 Bare-Metal BM J8(공유 자원과 우선순위)과 겹친다 (주인: 이 노트).

### 7.1 직관 — 낮은 ISR이 높은 ISR과 자료를 나눌 때

D3은 태스크 사이의 우선순위 역전(H가 L의 mutex를 기다리는 동안 M이 끼어든다)을 다뤘다. **ISR 사이에도 똑같은 일이 생긴다.** ISR은 블록할 수 없으므로 mutex 대신 "상대가 못 들어오게 막기"를 쓰는데, 무엇을 막느냐에 따라 결과가 갈린다.

상황: L(레벨 3)과 H(레벨 1)가 두 워드짜리 자료 `sh`를 공유한다(항상 짝으로 읽어야 한다). L이 갱신하는 데 10 µs가 걸린다. 그 사이에 Z(레벨 0, 상관없는 급한 일), H, M(레벨 2, 상관없는 40 µs짜리 일)이 동시에 도착한다. L이 고를 수 있는 보호 방법은 넷이다.

1. **보호 없음** — H가 L 한가운데로 들어와 찢어진 값을 읽는다.
2. **ICER(H)** — 상대 IRQ 하나만 끈다. Don이 SSD 펌웨어에서 "그 인터럽트만 잠깐 disable"하던 방식이다.
3. **BASEPRI_MAX = H의 레벨** — 공유자 중 가장 높은 레벨까지 올린다(**priority ceiling**). D3의 ICPP와 같은 생각이고, Rust의 RTIC 프레임워크가 하드웨어 레벨에서 쓰는 Stack Resource Policy의 핵심이다.
4. **PRIMASK** — 전부 끈다.

### 예제 9 — 네 가지 보호 방법, 네 가지 대기 시간

무엇을 확인하나: L ISR이 공유 자료 갱신을 시작하고, 그 순간 **ISPR 한 번 쓰기로** Z, H, M을 동시에 pending으로 만든다(STIR을 세 번 쓰면 첫 번째가 바로 들어와 버려서 "동시 도착"이 안 된다 — 이것도 처음에 실제로 겪었다). 보호 방법마다 Z, H, M이 각자 시작하기까지 기다린 시간과, H가 찢어진 값을 본 횟수를 잰다.

```c
/* ex6 — priority inversion between ISRs: how L protects data it shares with H
 * Z lvl0 (unrelated, urgent)  H lvl1 (shares 'sh' with L)  M lvl2 (unrelated, 40 us)  L lvl3 */
#include "e2.h"
enum { NONE, ICER_H, CEILING, PRIMASK_ALL };
static const char *const nm[] = { "none       ", "ICER(H)    ", "BASEPRI=H  ", "PRIMASK    " };
static volatile struct { uint32_t a, b; } sh;              /* must be read as a matched pair */
static volatile uint32_t t0, tz, th, tm, torn, mode;
static void z_isr(void) { tz = CYC(); }
static void h_isr(void) { th = CYC(); if (sh.a != sh.b) torn++; }
static void m_isr(void) { tm = CYC(); spin_us(40); }
static void l_isr(void)
{
    uint32_t key = 0;
    if (mode == ICER_H) ICER(0) = 1u << 3;                 /* mask only the sharer */
    if (mode == CEILING) { key = get_basepri(); set_basepri_max(PRIO(1)); }  /* raise to H */
    if (mode == PRIMASK_ALL) __asm volatile("cpsid i" ::: "memory");
    sh.a++;
    t0 = CYC(); ISPR(0) = (1u << 2) | (1u << 3) | (1u << 4);   /* Z, H, M arrive together */
    __asm volatile("dsb\n isb" ::: "memory");
    spin_us(10);                                           /* 10 us update of the shared pair */
    sh.b++;
    if (mode == ICER_H) ISER(0) = 1u << 3;
    if (mode == CEILING) set_basepri(key);
    if (mode == PRIMASK_ALL) __asm volatile("cpsie i\n isb" ::: "memory");
}
int main(void)
{
    vec_install();
    vec_set(2, z_isr); vec_set(3, h_isr); vec_set(4, m_isr); vec_set(5, l_isr);
    irq_prio(2, 0); irq_prio(3, 1); irq_prio(4, 2); irq_prio(5, 3);
    for (unsigned n = 2; n <= 5; n++) irq_on_n(n);
    board_printf_raw("L protects   Z wait     H wait     M wait   torn H reads\n");
    for (mode = NONE; mode <= PRIMASK_ALL; mode++) {
        torn = 0; irq_pend(5);
        while (!(th && tm && tz)) { }
        board_printf_raw("%s %u ns   %u ns   %u ns   %u\n", nm[mode],
                         (tz - t0) * 40u, (th - t0) * 40u, (tm - t0) * 40u, torn);
        th = tm = tz = 0;
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; ./run.sh bm ex6_isr_inversion.c -- ex6 4'
```

```text
L protects   Z wait     H wait     M wait   torn H reads
none        200 ns   360 ns   800 ns   1
ICER(H)     200 ns   51240 ns   360 ns   0
BASEPRI=H   200 ns   10880 ns   11200 ns   0
PRIMASK     10840 ns   11000 ns   11320 ns   0
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">ISR 사이의 우선순위 역전 — L이 공유 자원을 지키는 두 방법 (예제 9 실측, µs)</text>
<text x="10.0" y="44.0" font-size="12" text-anchor="start" font-weight="bold">ICER(H): H만 끄기</text>
<text x="10.0" y="76.0" font-size="12" text-anchor="start">Z 레벨0</text>
<text x="10.0" y="100.0" font-size="12" text-anchor="start">H 레벨1</text>
<text x="10.0" y="124.0" font-size="12" text-anchor="start">M 레벨2</text>
<text x="10.0" y="148.0" font-size="12" text-anchor="start">L 레벨3</text>
<rect x="122.0" y="64.0" width="3.0" height="16.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="123.6" y="112.0" width="400.4" height="16.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="120.0" y="136.0" width="2.0" height="16.0" fill="#888" fill-opacity="0.6" stroke="#888"/>
<rect x="524.0" y="136.0" width="107.0" height="16.0" fill="#888" fill-opacity="0.6" stroke="#888"/>
<rect x="632.4" y="88.0" width="3.6" height="16.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<line x1="120.0" y1="58.0" x2="632.0" y2="58.0" stroke="#d0564a" stroke-width="1" stroke-dasharray="3 3"/>
<text x="628.0" y="54.0" font-size="12" text-anchor="end">H 대기 51.2</text>
<text x="10.0" y="170.0" font-size="12" text-anchor="start" font-weight="bold">BASEPRI = H 레벨 (ceiling)</text>
<text x="10.0" y="202.0" font-size="12" text-anchor="start">Z 레벨0</text>
<text x="10.0" y="226.0" font-size="12" text-anchor="start">H 레벨1</text>
<text x="10.0" y="250.0" font-size="12" text-anchor="start">M 레벨2</text>
<text x="10.0" y="274.0" font-size="12" text-anchor="start">L 레벨3</text>
<rect x="122.0" y="190.0" width="3.0" height="16.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<rect x="120.0" y="262.0" width="108.0" height="16.0" fill="#888" fill-opacity="0.6" stroke="#888"/>
<rect x="228.8" y="214.0" width="2.2" height="16.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="232.0" y="238.0" width="400.0" height="16.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<line x1="120.0" y1="184.0" x2="229.0" y2="184.0" stroke="#d0564a" stroke-width="1" stroke-dasharray="3 3"/>
<text x="233.0" y="188.0" font-size="12" text-anchor="start">H 대기 10.9</text>
<text x="10.0" y="318.0" font-size="12" text-anchor="start">위 ICER: H보다 낮은 M이 40 µs 동안 H를 막는다(역전). 아래 ceiling: M도 막혀 H 대기 = L 구간.</text>
</svg>
```

그림 8 — 예제 9의 두 가운데 경우를 실측 시각으로 그린 간트 차트. 위(ICER): H만 껐더니 H보다 낮은 M이 L을 선점해 40 µs 동안 달리고, H는 51.2 µs 뒤에야 들어온다. 아래(ceiling): M도 막혀서 H는 L의 구간(10.9 µs)만 기다리고, M은 그 뒤에 달린다.

출력에서 볼 것:

- **보호 없음**: 모두 빠르지만 H가 찢어진 값을 1번 읽었다. 틀린 답이 빠르게 나온다.
- **ICER(H)**: H 대기 **51.2 µs** = M 40 µs + L 나머지 10 µs. H는 M보다 높은데 M 때문에 기다렸다 — **ISR 사이의 무한 역전과 같은 구조**다(M이 길어지면 H 대기도 끝없이 늘어난다). "그 인터럽트만 끄기"가 안전해 보이지만 중간 레벨을 통과시키는 것이 문제다.
- **BASEPRI = H 레벨(ceiling)**: H 대기 **10.9 µs** = L의 구간 길이로 묶인다. M도 L의 구간 동안 막혔다가(11.2 µs) 뒤에 달린다 — M은 H보다 낮으므로 H 다음이 맞다. **Z는 여전히 0.2 µs** — ceiling은 공유자보다 높은 레벨을 건드리지 않는다.
- **PRIMASK**: H는 ceiling과 같지만 **Z까지 10.8 µs 기다렸다.** 공유와 상관없는 가장 급한 일이 희생된다. RTOS의 BASEPRI 선을 PRIMASK로 바꾼 것과 같은 효과다(B3 예제 10).
- 정리: **레벨 사이 공유 자원의 보호는 "공유자 중 가장 높은 레벨까지 BASEPRI_MAX"가 기본값이다.** 구간이 짧아야 한다는 조건(H의 tol ≥ 구간 길이)은 그대로다.

### 7.2 레벨 조합별 공유 규칙

| 공유하는 두 쪽 | 쓸 수 있는 보호 | 피해야 할 것 | 비고 |
|---|---|---|---|
| 같은 레벨의 두 ISR | **필요 없음** | — | 서로 선점하지 않는다. 공유자를 같은 레벨에 묶는 것 자체가 설계 해법 |
| 다른 레벨의 두 ZL ISR | BASEPRI_MAX = 높은 쪽 레벨 (ceiling), 또는 락 없는 구조 | ICER(상대만 끄기) | ZL ISR이 BASEPRI를 올려도 된다 — 커널 API가 아니라 레지스터일 뿐 |
| ZL ISR ↔ KA ISR 또는 태스크 | 락 없는 SPSC 링, seqlock, 더블 버퍼 (D8) | `taskENTER_CRITICAL()` / `irq_lock()` | 커널 임계구역은 ZL을 막지 못한다. 막으려면 BASEPRI를 ZL 레벨까지(또는 PRIMASK) 올려야 하고 그 순간 그 ZL은 zero-latency가 아니다 |
| 다른 레벨의 두 KA ISR | ceiling, 또는 커널 객체(`xQueueSendFromISR` 등) | ICER | 커널 객체는 내부에서 `portSET_INTERRUPT_MASK_FROM_ISR()` = 선까지 BASEPRI — 선 아래 전부를 막는 큰 ceiling |
| KA ISR ↔ 태스크 | `taskENTER_CRITICAL()`, FromISR API 객체 | 태스크에서 PRIMASK | C7의 주제 |

설계 원칙 하나 더: **공유 자료가 생기는 순간 두 ISR의 레벨을 다시 본다.** 공유자를 같은 레벨에 둘 수 있으면(서로의 C를 tol 안에서 기다려 줄 수 있으면) 그것이 가장 싸다. 예제 2의 ⑤단계에 "공유하는 원천은 같은 레벨 우선"이라는 규칙을 더할 수 있다.

---

## 8. 우선순위와 ISR 주기 — rate-monotonic 감각

> 🔁 이 절은 Bare-Metal BM J8(우선순위 배치의 근거)과 겹친다 (주인: 이 노트).

### 8.1 직관 — "중요한 1 kHz"를 "급한 20 kHz" 위에 두면

§2.1의 말을 실험으로 확인한다. 20 kHz ISR A(C = 10 µs)와 1 kHz ISR B(C = 120 µs, "비행 제어 계산이라 중요하다")가 있다. 중요도로 B를 위에 두면 B가 도는 120 µs 동안 A는 두 번 이상 도착하는데, NVIC의 pending 비트는 **하나**라서 여러 번 온 인터럽트가 한 번으로 합쳐진다. A는 실행 횟수를 잃는다. 이론으로는 rate-monotonic(주기가 짧을수록 높게)이 고정 우선순위 중 최적이라는 Liu & Layland의 결과가 있다(N2). 인터럽트도 하드웨어가 스케줄하는 고정 우선순위 작업이므로 그대로 적용된다.

### 예제 10 — 두 배치, 100 ms 동안

무엇을 확인하나: DUALTIMER(A, 50 µs 주기, 10 µs 일)와 TIMER0(B, 1 ms 주기, 120 µs 일)를 100 ms 돌려 A가 몇 번 실행됐는지(2000번이어야 한다)와 각자의 최대 응답 시간(도착 → ISR 끝)을 잰다. B가 위인 배치와 A가 위인 배치를 비교한다.

```c
/* ex7 — priority vs ISR rate: fast 20 kHz ISR (10 us) and slow 1 kHz ISR (120 us), two orderings */
#include "e2.h"
static volatile uint32_t na, nb, a_rmax, b_rmax, a_late;
void DUALTIMER_IRQHandler(void)                       /* A: period 50 us, C = 10 us */
{
    DT_INTCLR = 1;
    spin_us(10);
    uint32_t r = DT_LOAD - DT_VALUE;                  /* response since release (valid if < 50 us) */
    if (r > a_rmax) a_rmax = r;
    na++;
}
void TIMER0_IRQHandler(void)                          /* B: period 1 ms, C = 120 us */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    spin_us(120);
    uint32_t r = TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE);
    if (r > b_rmax) b_rmax = r;
    nb++;
}
static void run(const char *name, unsigned lvl_a, unsigned lvl_b)
{
    irq_prio(10, lvl_a); irq_prio(8, lvl_b);
    na = nb = a_rmax = b_rmax = 0;
    TIMER_RELOAD(TIMER0_BASE) = 25000 - 1; TIMER_VALUE(TIMER0_BASE) = 25000 - 1;
    DT_LOAD = 1250 - 1;
    uint32_t t0 = CYC();
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u; DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u;
    while (CYC() - t0 < 100000u * 25u) { }            /* 100 ms */
    DT_CTRL = 0; TIMER_CTRL(TIMER0_BASE) = 0;
    ICPR(0) = (1u << 8) | (1u << 10);
    int lost = 2000 - (int)na;                         /* pending bit coalesces: late = lost */
    board_printf_raw("%s A ran %u (lost %d)  A max resp ", name, na, lost > 0 ? lost : 0);
    if (lost > 0) board_printf_raw("n/a (> period)"); else board_printf_raw("%u ns", a_rmax * 40u);
    board_printf_raw("  B ran %u  B max resp %u ns\n", nb, b_rmax * 40u);
}
int main(void)
{
    irq_on_n(8); irq_on_n(10);
    run("B above A (\"B is important\"):", 3, 2);
    run("A above B (rate-monotonic):   ", 2, 3);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; cd $R; ./run.sh bm ex7_rate.c -- ex7 4 60'
```

```text
B above A ("B is important"): A ran 1802 (lost 198)  A max resp n/a (> period)  B ran 100  B max resp 120440 ns
A above B (rate-monotonic):    A ran 2002 (lost 0)  A max resp 10360 ns  B ran 100  B max resp 131120 ns
[board_exit] code=0
```

출력에서 볼 것:

- **B가 위**: A가 2000번 중 **198번을 잃었다**(B 한 번에 약 2번). A의 응답 시간은 한 주기를 넘어가서 타이머 값으로는 잴 수 없었다("n/a"). B는 120 µs로 깔끔하다 — B 입장에서는 아무 문제가 없어 보인다는 것이 함정이다.
- **A가 위(rate-monotonic)**: A는 한 번도 안 잃고 최대 응답 10.4 µs. B의 최대 응답은 131 µs로 늘었지만 마감 1 ms에 비하면 여유가 크다. **"중요한" B는 조금 늦어졌을 뿐 아무것도 잃지 않았다.**
- A가 2002번인 것은 측정 창 경계에서 한두 번 더 센 것이다.

### 예제 11 — RTA로 미리 계산하기 (Python)

무엇을 확인하나: 응답 시간 분석(RTA, N3)의 식 `R = C + B + Σ_{j ∈ 더 높은 레벨} ⌈R / T_j⌉ × C_j`를 반복 계산한다. B는 블로킹: 같은 레벨의 다른 ISR 하나(비선점이라 하나가 이미 돌고 있을 수 있다) + 선 아래면 커널 임계구역 BK. 예제 10의 두 배치와 예제 2의 드론 배치표를 검증한다.

```python
# ex8 — response-time analysis (N3) for ISR priority choices: ex7's pair, then ex1's drone layout
from math import ceil
def rta(tasks, block=lambda lv: 0.0):
    """tasks: list of (name, T_us, C_us, level); same level = no preemption (worst: wait one C)"""
    out = {}
    for n, T, C, lv in tasks:
        hp = [t for t in tasks if t[3] < lv]
        same = [t[2] for t in tasks if t[3] == lv and t[0] != n]
        B = block(lv) + (max(same) if same else 0.0)      # one non-preemptable job ahead of us
        R = C + B
        while True:
            Rn = C + B + sum(ceil(R / Tj) * Cj for _, Tj, Cj, _ in hp)
            if Rn == R or Rn > 10 * T: break
            R = Rn
        out[n] = R
    return out
A, B = ("A_20kHz", 50, 10, None), ("B_1kHz", 1000, 120, None)
for title, la, lb in (("B above A", 3, 2), ("A above B (RM)", 2, 3)):
    r = rta([(A[0], A[1], A[2], la), (B[0], B[1], B[2], lb)])
    print(f"{title:15s} R(A)={r['A_20kHz']:6.0f} us (D=50)  R(B)={r['B_1kHz']:6.0f} us (D=1000)")
print("lost A releases per B job if B above A:", 120 // 50, "-> per 100 ms:", 100 * (120 // 50))

from ex1_layout import DRONE, layout, BK
rows, line, _ = layout(DRONE)
ts = [(s[0], 1e6 / s[1] if s[1] else 1e6, s[2], lv) for lv, s, w in rows]
r = rta(ts, block=lambda lv: BK if lv >= line else 0.0)        # kernel CS blocks only below the line
print(f"\ndrone layout check (BK={BK} us for kernel-aware levels):")
for lv, s, w in rows:
    wait = r[s[0]] - s[2]
    print(f"  lvl {lv} {s[0]:<15} worst wait {wait:6.1f} us  tol {s[3]:6.0f}  {'OK' if wait <= s[3] else 'MISS'}")
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/E2 && python3 ex8_rta.py'
```

```text
B above A       R(A)=   130 us (D=50)  R(B)=   120 us (D=1000)
A above B (RM)  R(A)=    10 us (D=50)  R(B)=   150 us (D=1000)
lost A releases per B job if B above A: 2 -> per 100 ms: 200

drone layout check (BK=25.0 us for kernel-aware levels):
  lvl 0 ESC_KILL        worst wait    0.0 us  tol      1  OK
  lvl 1 IMU_DRDY        worst wait    1.0 us  tol      1  OK
  lvl 2 DSHOT_DMA       worst wait    1.5 us  tol      4  OK
  lvl 3 IMU_SPI_DMA     worst wait   33.5 us  tol     40  OK
  lvl 3 RC_UART_IDLE    worst wait   33.5 us  tol    200  OK
  lvl 3 CAN_RX          worst wait   32.5 us  tol    280  OK
  lvl 4 USB_OTG         worst wait   46.5 us  tol    900  OK
  lvl 4 TELEM_UART_DMA  worst wait   50.5 us  tol   2000  OK
  lvl 4 GNSS_UART_DMA   worst wait   50.5 us  tol   2000  OK
  lvl 4 SDMMC_DMA       worst wait   50.5 us  tol   5000  OK
  lvl 4 BARO_I2C_DMA    worst wait   50.5 us  tol   5000  OK
```

출력에서 볼 것:

- **예제 10과 맞춰 보기**: B 위일 때 R(A) = 130 µs > 마감 50 → 놓친다고 예측. "B 한 번에 A 2번 손실 → 100 ms에 200번" 예측, 실측 198번. A 위일 때 R(B) = 150 µs 예측, 실측 131 µs. 실측이 작은 것은 두 타이머가 같은 순간에 출발해 위상이 고정됐기 때문이다 — RTA는 **최악 위상(동시 도착, critical instant)**을 가정한 상한이고, 측정은 그 위상을 못 만났다. 그래서 측정만으로 배치를 증명할 수 없다(E3의 주제).
- **드론 배치표 검증**: 11개 원천 모두 OK. 가장 빠듯한 것은 IMU_DRDY(최악 1.0 / 허용 1 µs — 비상 정지 ISR 한 번만큼)와 IMU_SPI_DMA(33.5 / 40 µs = BK 25 + 같은 레벨 CAN 5 + 위 세 레벨 3.5). IMU_DRDY가 경계에 걸렸다는 것이 이 계산의 수확이다 — 실제 설계에서는 DRDY를 **타이머 입력 캡처**로 바꿔 tol을 늘리는 것이 맞다(§3.1).
- 이 식은 ISR 사이의 RTA다. 태스크의 RTA에는 모든 ISR이 "더 높은 우선순위"로 들어간다 — ISR 부하 Σ(C/T)는 모든 태스크가 내는 세금이다(N3에서 ISR 항을 넣는 법).

### 8.2 정리 — 주기 · 중요도 · 우선순위

| 원천의 성격 | 우선순위 정하는 기준 | 예 |
|---|---|---|
| 주기적, 마감 = 주기 | 주기가 짧을수록 높게 (rate-monotonic) | 20 kHz FOC > 8 kHz IMU > 1 kHz 루프 |
| 주기적, 마감 < 주기 | 마감이 짧을수록 높게 (deadline-monotonic) | DShot DMA 재장전(마감 4 µs) |
| 비주기 · 드묾 | 최소 도착 간격을 T로, tol을 마감으로 | 비상 정지, CAN 버스 오프 |
| 버스트 | 버스트 안의 최소 간격으로 T를 잡는다 | UART 바이트 (DMA로 바꾸면 사라짐) |
| "중요하지만 느긋한" 일 | 낮게. 중요도는 **태스크 우선순위**와 마감 검증으로 지킨다 | 1 kHz 자세 제어 계산 |

---

## 9. 배치표를 지키는 도구 — zero-latency ISR 정적 호출 검사

배치표가 맞아도 6개월 뒤 누군가 FOC ISR에 "디버그용으로 잠깐" 로그 한 줄을 넣는다. configASSERT를 켠 빌드라면 잡히지만, 그 로그 함수가 1/100 확률로만 불린다면 시험에서 안 잡히고 필드에서 가끔 큐가 깨진다. 이런 실수는 **ELF의 호출 그래프**로 빌드 때 잡을 수 있다.

### 예제 12 — ZL ISR에서 커널 함수로 가는 경로 찾기

무엇을 확인하나: `objdump -d` 출력에서 함수마다 `bl` · `b.w`(꼬리 호출) 대상을 모아 호출 그래프를 만들고, zero-latency ISR에서 출발해 FreeRTOS · Zephyr 커널 함수 이름 패턴에 닿는 경로가 있으면 실패로 끝낸다. 대상은 예제 4의 ELF(문지기 설계)와, FOC ISR이 100번에 한 번 `log_sample()` → `xQueueSendFromISR()`을 부르는 "나쁜" 빌드다.

```c
/* ex11 target — a "harmless" debug log that sneaks a kernel call into a zero-latency ISR */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "e2.h"
static QueueHandle_t q;
static volatile uint32_t n;
static void __attribute__((noinline)) log_sample(uint32_t v)
{
    BaseType_t w = pdFALSE;
    xQueueSendFromISR(q, &v, &w);                /* two calls deep: easy to miss in review */
}
void DUALTIMER_IRQHandler(void)                 /* level 1 = zero-latency */
{
    DT_INTCLR = 1;
    if (++n % 100u == 0) log_sample(n);
}
void TIMER0_IRQHandler(void)                    /* level 3 = kernel-aware: allowed */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t w = pdFALSE; uint32_t v = n;
    xQueueSendFromISR(q, &v, &w); portYIELD_FROM_ISR(w);
}
int main(void)
{
    q = xQueueCreate(4, sizeof(uint32_t));
    irq_prio(10, 1); irq_prio(8, 3);
    vTaskStartScheduler();
    return 1;
}
```

```python
# ex11 — static check: no kernel function may be reachable from a zero-latency ISR
import re, subprocess, sys
elf, roots = sys.argv[1], sys.argv[2:]
dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", elf],
                     capture_output=True, text=True).stdout
calls, cur = {}, None
for line in dis.splitlines():
    m = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
    if m: cur = m.group(1); calls[cur] = set(); continue
    m = re.search(r"\s(bl|blx|b\.w|b)\s+[0-9a-f]+ <([^>+]+)>", line)  # direct calls + tail calls
    if cur and m and m.group(2) != cur: calls[cur].add(m.group(2))
KERNEL = re.compile(r"^(x|v|ux|ul|pv|e)(Task|Queue|Timer|EventGroup|StreamBuffer|Port|List)"
                    r"|^prv|^vPortValidate|^z_|^k_")
bad = 0
for root in roots:
    seen, stack, path = set(), [(root, [root])], {}
    while stack:
        f, p = stack.pop()
        if f in seen: continue
        seen.add(f)
        if KERNEL.search(f): print(f"  {root}: KERNEL CALL {' -> '.join(p)}"); bad += 1; continue
        stack += [(g, p + [g]) for g in calls.get(f, ())]
    print(f"{root}: {len(seen)} functions reachable")
print("FAIL" if bad else "PASS", f"({bad} kernel path(s))")
sys.exit(1 if bad else 0)
```

```sh
limactl shell elinux -- bash -lc 'R=/Users/donh/.rtos_scratch/E2; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$R/ex11_bad.c OUT=/var/tmp/rtos/E2/ex11 >/dev/null; cd /var/tmp/rtos/E2; python3 $R/ex11_zlcheck.py ex3/app.elf DUALTIMER_IRQHandler; echo "exit=$?"; python3 $R/ex11_zlcheck.py ex11/app.elf DUALTIMER_IRQHandler; echo "exit=$?"'
```

```text
DUALTIMER_IRQHandler: 1 functions reachable
PASS (0 kernel path(s))
exit=0
  DUALTIMER_IRQHandler: KERNEL CALL DUALTIMER_IRQHandler -> log_sample -> xQueueGenericSendFromISR
DUALTIMER_IRQHandler: 3 functions reachable
FAIL (1 kernel path(s))
exit=1
```

출력에서 볼 것:

- 예제 4의 FOC ISR은 도달 가능한 함수가 자기 하나뿐이다(`spin_cyc`는 인라인됐다). PASS.
- 나쁜 빌드는 `DUALTIMER_IRQHandler -> log_sample -> xQueueGenericSendFromISR` 경로를 찾아 **종료 코드 1**로 끝난다. `xQueueSendFromISR`은 매크로라서 실제 심볼은 `xQueueGenericSendFromISR`이다 — 소스 grep이 아니라 ELF를 봐야 하는 이유다.
- 한계: 함수 포인터 호출(`blx rN`)은 따라가지 못한다. 콜백 테이블을 쓰는 HAL이라면 "ZL ISR에서는 간접 호출 금지"를 코딩 규칙으로 더하거나, 간접 호출이 보이면 경고로 띄운다. 그래도 직접 호출 실수의 대부분을 CI 한 줄로 막는다.

---

## 10. RTOS 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 392" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">배치표가 커널 소스의 어디에서 강제되나</text>
<rect x="10.0" y="40.0" width="190.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="105.0" y="69.2" font-size="12" text-anchor="middle">xPortStartScheduler()</text>
<line x1="200.0" y1="65.0" x2="223.0" y2="65.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="225.0" y="40.0" width="215.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="332.5" y="69.2" font-size="12" text-anchor="middle">0xFF 프로브 → 구현 비트 수</text>
<line x1="440.0" y1="65.0" x2="463.0" y2="65.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="465.0" y="40.0" width="205.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="567.5" y="60.7" font-size="12" text-anchor="middle">ucMaxSysCallPriority</text>
<text x="567.5" y="77.7" font-size="12" text-anchor="middle">SHPR3 PendSV · SysTick</text>
<rect x="10.0" y="102.0" width="190.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="105.0" y="131.2" font-size="12" text-anchor="middle">xQueueSendFromISR() 등</text>
<line x1="200.0" y1="127.0" x2="223.0" y2="127.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="225.0" y="102.0" width="215.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="332.5" y="122.7" font-size="12" text-anchor="middle">portASSERT_IF_INTERRUPT_</text>
<text x="332.5" y="139.7" font-size="12" text-anchor="middle">PRIORITY_INVALID()</text>
<line x1="440.0" y1="127.0" x2="463.0" y2="127.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="465.0" y="102.0" width="205.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="567.5" y="122.7" font-size="12" text-anchor="middle">vPortValidateInterrupt</text>
<text x="567.5" y="139.7" font-size="12" text-anchor="middle">Priority(): IPR ≥ 선?</text>
<rect x="10.0" y="164.0" width="190.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="105.0" y="184.7" font-size="12" text-anchor="middle">IRQ_CONNECT ·</text>
<text x="105.0" y="201.7" font-size="12" text-anchor="middle">IRQ_DIRECT_CONNECT</text>
<line x1="200.0" y1="189.0" x2="223.0" y2="189.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="225.0" y="164.0" width="215.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="332.5" y="184.7" font-size="12" text-anchor="middle">_CHECK_PRIO</text>
<text x="332.5" y="201.7" font-size="12" text-anchor="middle">(BUILD_ASSERT)</text>
<line x1="440.0" y1="189.0" x2="463.0" y2="189.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="465.0" y="164.0" width="205.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="567.5" y="184.7" font-size="12" text-anchor="middle">arm_irq_priority_set()</text>
<text x="567.5" y="201.7" font-size="12" text-anchor="middle">prio + _IRQ_PRIO_OFFSET</text>
<rect x="10.0" y="226.0" width="190.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="105.0" y="255.2" font-size="12" text-anchor="middle">irq_lock()</text>
<line x1="200.0" y1="251.0" x2="223.0" y2="251.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="225.0" y="226.0" width="215.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="332.5" y="246.7" font-size="12" text-anchor="middle">arch_irq_lock():</text>
<text x="332.5" y="263.7" font-size="12" text-anchor="middle">BASEPRI_MAX</text>
<line x1="440.0" y1="251.0" x2="463.0" y2="251.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="465.0" y="226.0" width="205.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="567.5" y="246.7" font-size="12" text-anchor="middle">_EXC_IRQ_DEFAULT_PRIO</text>
<text x="567.5" y="263.7" font-size="12" text-anchor="middle">(첫 일반 IRQ 레벨)</text>
<rect x="10.0" y="288.0" width="190.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="105.0" y="317.2" font-size="12" text-anchor="middle">arch_zli_lock()</text>
<line x1="200.0" y1="313.0" x2="223.0" y2="313.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="225.0" y="288.0" width="215.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="332.5" y="317.2" font-size="12" text-anchor="middle">PRIMASK: ZLI까지 막음</text>
<line x1="440.0" y1="313.0" x2="463.0" y2="313.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah9)"/>
<rect x="465.0" y="288.0" width="205.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="567.5" y="308.7" font-size="12" text-anchor="middle">nrf_rtc_timer.c</text>
<text x="567.5" y="325.7" font-size="12" text-anchor="middle">full_int_lock()</text>
<text x="10.0" y="360.0" font-size="12" text-anchor="start">파랑 = FreeRTOS V11.3.2 portable/GCC/ARM_CM3</text>
<text x="10.0" y="380.0" font-size="12" text-anchor="start">주황 · 빨강 = Zephyr v4.4.2 arch/arm/core/cortex_m, include/zephyr/arch/arm, drivers/timer</text>
</svg>
```

그림 9 — 배치표의 규칙이 커널 소스의 어디에서 강제되나. 위 둘은 FreeRTOS(런타임 검사), 아래 셋은 Zephyr(빌드 검사 + 잠금 두 종류).

### FreeRTOS V11.3.2 (`portable/GCC/ARM_CM3/port.c`, `portmacro.h`)

- **선의 기록**: `xPortStartScheduler()`가 첫 외부 IRQ의 IPR에 0xFF를 써서 구현 비트 수를 알아내고 `ucMaxSysCallPriority = configMAX_SYSCALL_INTERRUPT_PRIORITY & 읽은 값`, `ulMaxPRIGROUPValue`를 저장한다. 그다음 `portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI`, `|= portNVIC_SYSTICK_PRI`(둘 다 `configKERNEL_INTERRUPT_PRIORITY`), `portNVIC_SHPR2_REG = 0`(SVCall 최상위). 자세한 프로브 과정은 B3 §9.
- **선의 강제(런타임)**: 모든 FromISR API 입구의 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()` = `vPortValidateInterruptPriority()`. IPSR로 현재 예외 번호를 읽어 그 IRQ의 IPR 바이트가 `ucMaxSysCallPriority` 이상인지(`port.c:803`), PRIGROUP이 허용값 이하인지(`port.c:819`) 본다. 예제 3의 첫 실행이 `port.c:803`에서 멈췄다.
- **선의 사용**: 태스크 임계구역 `vPortEnterCritical()` → `vPortRaiseBASEPRI()`, ISR 임계구역 `ulPortRaiseBASEPRI()`(이전 값 반환). 둘 다 BASEPRI = `configMAX_SYSCALL_INTERRUPT_PRIORITY`. 그 위의 레벨은 커널 코드 어디에서도 마스크되지 않는다 — 이것이 zero-latency의 근거다(C7 §3).
- **ISR 스택**: `prvPortStartFirstTask()`가 VTOR → 벡터 0번에서 초기 MSP 값을 읽어 MSP를 되돌린다. 이후 MSP는 ISR 전용이다(§6.1).
- **GIC 포트**(ARM_CR5, ARM_CA9, ARM_AARCH64): 같은 역할을 `configMAX_API_CALL_INTERRUPT_PRIORITY`와 GIC의 우선순위 마스크 레지스터(ICCPMR)가 한다. `ulMaxAPIPriorityMask = ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT )`(ARM_CR5 `port.c`).

### Zephyr v4.4.2

- **레벨 예약**: `include/zephyr/arch/arm/cortex_m/exception.h` — `_EXC_SVC_PRIO = COND_CODE_1(CONFIG_ZERO_LATENCY_IRQS, (CONFIG_ZERO_LATENCY_LEVELS), (0))`, `_IRQ_PRIO_OFFSET = _EXCEPTION_RESERVED_PRIO + _EXC_SVC_PRIO`, `IRQ_PRIO_LOWEST`.
- **빌드 검사**: `include/zephyr/arch/arm/irq.h` — `ARCH_IRQ_CONNECT`의 `BUILD_ASSERT(!(flags_p & IRQ_ZERO_LATENCY), ...)`, `ARCH_IRQ_DIRECT_CONNECT`의 "ZLI interrupt registered but feature is disabled", 공통 `_CHECK_PRIO`(예제 6에서 레벨 2 ZLI가 통과한 조건식).
- **런타임 배치**: `arch/arm/core/cortex_m/irq_manage.c`의 `arm_irq_priority_set()` — ZLI면 `ZERO_LATENCY_LEVELS == 1`일 때 `_EXC_ZERO_LATENCY_IRQS_PRIO`(0), 아니면 인자 그대로. 일반 IRQ는 `prio += _IRQ_PRIO_OFFSET`. `__ASSERT`(CONFIG_ASSERT=y일 때만)로 범위 확인 후 `NVIC_SetPriority()`.
- **두 잠금**: `include/zephyr/arch/arm/asm_inline_gcc.h` — `arch_irq_lock()` = `__set_BASEPRI_MAX(_EXC_IRQ_DEFAULT_PRIO)`(ZLI 통과), `arch_zli_lock()` = `__disable_irq()`(ZLI도 막음, `CONFIG_ZERO_LATENCY_IRQS`일 때만 존재).
- **실제 사용처**: `drivers/counter/counter_nrfx_rtc.c`, `counter_nrfx_timer.c`가 `IRQ_ZERO_LATENCY` 플래그로 등록하는 경로를 갖고 있고, `drivers/timer/nrf_rtc_timer.c`는 ZLI가 자기 API를 부를 수 있으므로 PRIMASK 잠금을 쓴다(예제 5 설명). 트리 안의 ZLI 사용자는 대부분 Nordic 무선 · 타이머 쪽이다 — BLE 컨트롤러처럼 무선 타이밍을 µs 단위로 지켜야 하는 코드가 이 기능의 대표 고객이다.

다음 모듈로의 연결: FromISR API의 규칙과 `xHigherPriorityTaskWoken`은 **E1**, 지연을 "HW 진입 + 마스킹 + 핸들러 + 스케줄링"으로 나눠 재는 방법은 **E3**, 문지기 다음의 "ISR → 태스크" 처리 설계와 인터럽트 폭주 대비는 **E4**, RTA의 이론과 블로킹 항 B의 정확한 정의는 **N2 · N3**, MSP · 태스크 스택 크기 산정은 **F3**이다.

---

## 11. 실무에서 다시 보기

### 11.1 드론 비행 제어기 — "새 센서 하나 추가" 리뷰

드론 팀에 광류(optical flow) 센서를 SPI로 하나 더 붙이는 PR이 올라온다. 드라이버 작성자는 IRQ 레벨을 "IMU랑 비슷하니까 레벨 1"로 썼다. 배치표 리뷰는 이렇게 진행된다.

1. 원천 표에 한 줄 추가: 광류 DRDY, T = 10 ms(100 Hz), C = 3 µs, tol = 수 ms(타임스탬프 정확도 요구가 100 µs 수준), kernel? = 예(태스크 통지).
2. tol(수 ms) ≫ BK(25 µs) → kernel-aware. 레벨 1(ZL)은 틀렸다 — 게다가 레벨 1에서 `vTaskNotifyGiveFromISR`을 부르면 `port.c:803`에서 멈추거나(assert 켬), 가끔 커널 리스트를 깬다(assert 끔).
3. 예제 2 도구에 넣으면 레벨 4에 들어간다. 예제 11의 RTA로 레벨 3 원천들의 대기가 늘지 않았는지 확인(레벨 4는 레벨 3보다 낮으므로 영향 없음).
4. 예제 3의 부팅 검사 표에 한 줄 추가, 예제 12의 정적 검사는 그대로 통과.

이 과정이 "대충 중간쯤"을 대신한다. Don이 SSD 팀에서 겪었던 "근거를 아무도 모르는 숫자"가 생기지 않는다.

### 11.2 로봇 관절 모터 컨트롤러 — CAN 명령이 늦게 반영된다

증상: 상위 제어기가 1 kHz로 보내는 토크 명령이 가끔 2 ms 늦게 반영된다. 배치는 §4의 표 그대로다. 원인 추적: CAN RX(레벨 4)는 FIFO를 비워 큐에 넣고, 명령 처리 태스크(우선순위 4)가 setpoint를 갱신한다. 그런데 FOC ISR(레벨 1)이 setpoint를 읽는 방식이 "태스크가 두 워드를 차례로 쓰고, ISR이 두 워드를 읽는" 구조였고, 이를 막으려고 태스크가 `taskENTER_CRITICAL()`을 썼다 — **ZL ISR에 대해서는 아무것도 막지 못한다**(§7.2 표). 찢어진 setpoint를 FOC가 읽는 것을 막기 위해 FOC 쪽에 "값이 튀면 무시" 필터가 있었고, 그 필터가 정상 갱신도 가끔 한 주기 미뤘다. 고친 방법: setpoint를 seqlock(태스크 → ISR 방향은 D8 §4.3의 더블 버퍼 + 인덱스 교체)으로 넘기고 필터를 지웠다. **공유 자료의 보호 방법은 두 쪽의 레벨이 정한다.**

### 11.3 Linux SoC 옆 센서 허브 (Zephyr, nRF52급 3비트)

센서 허브에서 ZLI를 하나 쓰면 3비트(8레벨) 중 ZLI 0, SVC 1이 빠지고 일반 IRQ는 0–5로 줄어든다(§1.3 표, LEVELS=1). BLE까지 같은 칩에서 돌리면 무선 스택이 이미 ZLI 레벨을 쓰고 있을 수 있다. 이때 센서 타임스탬프용 GPIO를 ZLI로 추가하기 전에 **하드웨어 타임스탬프(GPIOTE → PPI → TIMER 캡처 같은 칩 내부 이벤트 연결)**로 대체할 수 있는지 먼저 본다. Nordic 칩의 PPI는 CPU 개입 없이 이벤트를 태스크로 연결하는 하드웨어라서 우선순위 문제 자체가 사라진다. 우선순위 배치표의 마지막 수단이 ZLI이고, 그보다 먼저 쓸 수단이 하드웨어다.

### 11.4 Don의 SSD 컨트롤러 펌웨어로 번역하면

SSD 컨트롤러의 인터럽트를 이 노트의 방법으로 다시 짠다면:

| SSD 원천 | tol | 세계 | 레벨 |
|---|---|---|---|
| NAND 채널 PHY 타이밍 오류 · ECC 비상 | µs | ZL | 0 |
| 호스트 doorbell (NVMe SQ tail) | 수십 µs (지연이 IOPS · QoS로 바로 나타남) | KA (태스크 깨움) | 선 바로 아래 |
| DMA 완료 | 수십 µs | KA | 같은 레벨 (공유 큐 → 락 불필요) |
| 온도 · 전력 타이머 | ms | KA | 낮게 |
| UART 디버그 콘솔 | ms (DMA) | KA | 가장 낮게 |

Don이 인터뷰에서 쓸 수 있는 연결 문장: "In SSD firmware we inherited priority numbers nobody could justify. Now I build the table from each source's tolerable latency: anything that cannot wait for the longest kernel critical section goes above the BASEPRI line and talks to the kernel only through a lock-free buffer and a doorbell interrupt; everything else is grouped into as few levels as the deadlines allow, and I verify it with response-time analysis and a boot-time check."

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 우선순위를 "중요도"로 정함 (1 kHz 제어 ISR을 20 kHz ISR 위에) | 빠른 ISR이 가끔 실행 횟수를 잃음, 원인 불명의 샘플 누락 | pending 비트가 합쳐짐 (예제 10: 2000번 중 198번) | 주기 · 마감 기준(rate/deadline-monotonic), RTA로 확인 |
| 선 위 ISR에서 FromISR 호출 (디버그 로그 한 줄 포함) | `port.c:803` assert, 또는 assert 끈 빌드에서 가끔 리스트 깨짐 → 엉뚱한 HardFault | 커널이 막지 못하는 문맥에서 커널 자료 수정 | 링 + 문지기, 예제 12 정적 검사를 CI에 |
| FreeRTOSConfig의 선과 배치표가 어긋남 | 특정 IRQ가 올 때만 assert (드문 IRQ면 필드에서) | 설정과 표를 다른 사람이 고침 | 표에서 설정을 생성, 예제 3 부팅 검사 |
| 우선순위 안 정한 IRQ를 enable / 시프트 없이 레벨 번호 씀 | 그 IRQ가 최상위(0x00)가 되어 FOC보다 급해짐 | 리셋값 0, 4비트 칩 왼쪽 정렬 | 모든 enable 전에 `NVIC_SetPriority()`, 부팅 검사의 "표에 없는 enable" 항목 |
| 선 아래 최상위면 충분하다고 생각 | 평균은 좋은데 20%가 마감 초과 (예제 7) | 최악값 = 가장 긴 임계구역 | µs 마감이면 선 위로, 아니면 임계구역을 줄인다 |
| 선 아래 대기 예산에 ZL ISR 시간을 안 넣음 | 계산보다 수 µs 더 늦음 (예제 4: 20 → 21.3 µs) | 임계구역 안에 ZL ISR이 끼어들어 구간이 늘어남 | BK = 최장 임계구역 + 그 안의 ZL 간섭 |
| ISR마다 다른 레벨 | MSP 오버플로 (깊이 × 프레임), 드물게만 재현 | 중첩 깊이 = 레벨 수 (예제 8: 레벨당 112 B) | 같은 레벨에 묶기, MSP를 배치표에서 계산 |
| FreeRTOS에서 PRIGROUP으로 그룹 나눔 (HAL 기본값) | `port.c:819` assert, 또는 선이 설계와 다름 | 포트는 모든 비트가 선점 비트여야 함 | `NVIC_PRIORITYGROUP_4` / `NVIC_SetPriorityGrouping(0)`, 묶기는 같은 레벨로 |
| 다른 레벨 ISR끼리 공유 자료를 "상대 IRQ만 끄기"로 보호 | 높은 ISR이 중간 ISR 길이만큼 늦음 (예제 9: 51 µs) | ISR 사이 우선순위 역전 | BASEPRI_MAX = 공유자 최고 레벨 (ceiling) 또는 같은 레벨 |
| 태스크가 `taskENTER_CRITICAL()`로 ZL ISR과의 공유를 보호 | 찢어진 값, 가끔 튀는 제어 출력 | 커널 임계구역은 ZL을 막지 못함 | seqlock · 더블 버퍼 · SPSC (D8) |
| Zephyr ZLI의 prio 인자를 LEVELS 이상으로 | 빌드는 통과, 실제로 SVC 레벨에 앉음 (예제 6) | `_CHECK_PRIO`의 조건식 | 리뷰에서 `prio < CONFIG_ZERO_LATENCY_LEVELS` 확인 |
| 문지기로 쓰는 IRQ가 실제 주변장치 줄 | 주변장치를 켜는 순간 문지기 ISR이 엉뚱하게 불림, Zephyr는 빌드 에러 | IRQ 번호 충돌 | 칩 매뉴얼의 예약 · 미사용 줄, Zephyr는 DTS 확인 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "How do you assign interrupt priorities in an RTOS-based Cortex-M system?"

**A.** 중요도가 아니라 "얼마나 기다릴 수 있나"로 정한다. 원천마다 주기, ISR 실행 시간, 허용 지연을 적고, 커널의 가장 긴 임계구역(BASEPRI 구간)보다 못 기다리는 것만 선 위(zero-latency)에 두고 나머지는 선 아래에서 마감 순서로 정렬한다. 서로 기다려 줄 수 있는 것은 같은 레벨에 묶어 중첩과 스택을 줄이고, RTA로 최악 대기를 계산해 검증한다.

> I rank sources by how long they can wait, not by how important they are. For each interrupt I write down its period, its ISR execution time and its tolerable latency. Anything that cannot tolerate the longest kernel critical section goes above `configMAX_SYSCALL_INTERRUPT_PRIORITY` and never calls the kernel. Everything else is ordered deadline-monotonically below the line, and sources that can wait for each other share a level to limit nesting and stack. Then I check the worst-case wait of each source with response-time analysis and verify the programmed NVIC values at boot.

**꼬리 질문**: "Why not give every interrupt its own level?" → 레벨 수 = 최대 중첩 깊이 = MSP 크기. 레벨당 프레임 32 B(FPU면 104 B) + ISR 스택이 더해진다. 같은 레벨끼리는 선점하지 않으므로 공유 자료에 락도 필요 없다.

**Q.** "What is a zero-latency interrupt, and what can't it do?"

**A.** 커널 임계구역의 마스크(BASEPRI)보다 높은 우선순위의 인터럽트다. 커널이 무엇을 하든 바로 들어오지만, 커널 API를 하나도 못 부른다 — FromISR도, 세마포어 give도, tick 읽기도. 커널 자료를 고치는 도중에 끼어들 수 있기 때문이다. Zephyr는 `CONFIG_ZERO_LATENCY_IRQS`와 `IRQ_ZERO_LATENCY`, direct ISR로 명시하고, FreeRTOS는 선보다 높은 모든 IRQ가 그렇다.

> It's an interrupt whose priority is above the mask the kernel uses for its critical sections, so the RTOS never delays it. The price is that it must not touch the kernel at all: no FromISR calls, no semaphore gives, not even reading the tick count, because it can preempt the kernel in the middle of updating its own lists. In Zephyr you opt in with `CONFIG_ZERO_LATENCY_IRQS`, a reserved level, `IRQ_DIRECT_CONNECT` and the `IRQ_ZERO_LATENCY` flag; in FreeRTOS on Cortex-M it is simply any priority numerically below `configMAX_SYSCALL_INTERRUPT_PRIORITY`.

**Q.** "Your zero-latency current-loop ISR needs to wake the speed-loop task. How?"

**A.** 직접 못 부르니 두 단계로 나눈다. 자료는 락 없는 SPSC 링이나 seqlock으로 넘기고, 알림은 선 아래의 빈 IRQ 하나를 STIR로 pend한다. 그 문지기 ISR이 `vTaskNotifyGiveFromISR`을 부른다. 문지기는 커널 임계구역이 끝나야 들어오므로 알림은 늦을 수 있지만 전류 루프 자체는 영향이 없다. QEMU에서 4000주기 동안 손실 0, 전류 루프 진입 지연 최대 0.2 µs, 알림 지연 최대 21 µs였다.

> I split data and notification. The data goes through a single-producer single-consumer ring or a seqlock, which needs no lock. The notification is a doorbell: the ISR pends an otherwise unused interrupt line below the kernel line through STIR, and that kernel-aware ISR calls `vTaskNotifyGiveFromISR`. The doorbell can be delayed by kernel critical sections, but the current loop never is. In my measurement the loop's entry latency stayed under 0.2 microseconds while the doorbell path saw up to about 21.

**Q.** "What's `configMAX_API_CALL_INTERRUPT_PRIORITY` versus `configMAX_SYSCALL_INTERRUPT_PRIORITY`?"

**A.** 같은 개념, 포트마다 다른 이름이다. Cortex-M 포트는 MAX_SYSCALL을 쓰고 값은 NVIC 바이트(시프트된 값)다. GIC를 쓰는 Cortex-R5 · Cortex-A9 · AArch64 포트는 MAX_API_CALL을 쓰고 값은 GIC 논리 우선순위 번호이며, 포트가 그것을 GIC의 우선순위 마스크 레지스터에 쓴다. Cortex-M0 포트는 BASEPRI가 없어서 둘 다 없다.

> They are the same idea under different names. The Cortex-M ports use `configMAX_SYSCALL_INTERRUPT_PRIORITY`, expressed as the shifted NVIC byte, and enforce it with BASEPRI. The GIC-based ports — Cortex-R5, Cortex-A9, AArch64 — use `configMAX_API_CALL_INTERRUPT_PRIORITY` as a logical GIC priority and program it into the GIC CPU interface priority mask. The Cortex-M0 port has neither, because ARMv6-M has no BASEPRI, so there are no zero-latency interrupts there at all.

**Q.** "Two ISRs at different priorities share a data structure. How do you protect it?"

**A.** 먼저 같은 레벨에 둘 수 있는지 본다 — 그러면 서로 선점하지 않아 보호가 필요 없다. 안 되면 낮은 쪽이 BASEPRI_MAX를 공유자 중 높은 쪽 레벨까지 올린다(priority ceiling). 상대 IRQ만 끄면 그 사이 레벨의 다른 ISR이 끼어들어 높은 ISR이 그만큼 기다리는 역전이 생긴다. 측정에서 상대만 끄기는 51 µs, ceiling은 11 µs였다. PRIMASK는 상관없는 최상위 ISR까지 막는다.

> First I try to put them on the same preemption level, because then they can't preempt each other and need no protection. Otherwise the lower one raises BASEPRI with `BASEPRI_MAX` to the level of the highest sharer for the few instructions of the update — a priority ceiling. Disabling only the other interrupt is a trap: an unrelated interrupt in between can preempt the lower ISR while the higher one is held off, which is priority inversion between ISRs. In my measurement that cost 51 microseconds versus 11 with the ceiling, and PRIMASK would also have delayed an unrelated top-priority interrupt.

**Q.** "The control team says the 1 kHz attitude-control interrupt is the most important, so it should have the highest priority. Do you agree?"

**A.** 아니다. 중요도와 긴급도는 다르다. 1 kHz 계산이 120 µs를 쓰는 동안 20 kHz ISR은 두 번 이상 도착하고, NVIC의 pending 비트는 하나라서 실행이 합쳐져 사라진다. 측정에서 100 ms 동안 2000번 중 198번을 잃었다. 주기가 짧은 쪽을 위에 두면 1 kHz 쪽은 응답이 120에서 131 µs로 늘 뿐 마감 1 ms를 넉넉히 지킨다. 중요한 일은 마감 검증으로 지키고, 우선순위는 rate-monotonic으로 준다.

> No. Importance and urgency are different things. While a 120-microsecond, 1 kHz handler runs, a 20 kHz interrupt arrives two or three times, and because the NVIC has a single pending bit those arrivals collapse into one — I measured 198 lost activations out of 2000 in 100 milliseconds. With rate-monotonic ordering nothing was lost and the 1 kHz handler's response only grew from 120 to about 130 microseconds, far inside its 1 millisecond deadline. Important work is protected by verifying its deadline, not by giving it the top priority.

**Q.** "How would you make sure nobody breaks the priority design six months later?"

**A.** 세 겹으로 한다. 배치표를 데이터로 두고 거기서 설정 헤더와 부팅 검사 표를 생성한다. 부팅 때 NVIC를 다시 읽어 표와 비교한다(시프트 실수, 표에 없는 enable, 선과 세계의 불일치, PendSV · SysTick, PRIGROUP). 그리고 CI에서 ELF의 호출 그래프를 따라 zero-latency ISR에서 커널 함수로 가는 경로가 없는지 검사한다. configASSERT는 그 IRQ가 실제로 와야만 잡으므로 그것만으로는 부족하다.

> I keep the priority table as data and generate both the config header and a boot-time check from it; at boot the firmware reads back every NVIC priority and compares it with the table, flags any enabled interrupt that isn't in the table, and checks PendSV, SysTick and the priority grouping. In CI a small script walks the ELF call graph from every zero-latency handler and fails the build if it can reach a kernel function. configASSERT alone isn't enough because it only fires when that particular interrupt actually occurs.

---

## 14. 직접 해보기

1. **개념** — 4비트 칩, FreeRTOS, `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`. 쓸 수 있는 zero-latency 레벨은 몇 개이고, 그 레벨들의 NVIC 바이트는? 같은 칩에서 Zephyr `CONFIG_ZERO_LATENCY_LEVELS=2`라면 일반 IRQ에 남는 레벨은 몇 개인가?
   정답/힌트: FreeRTOS는 레벨 0–4, 바이트 0x00–0x40, 5개. Zephyr는 오프셋 = fault 예약 1 + LEVELS 2 = 3(하드웨어 레벨 0 = fault · ZLI0, 1 = ZLI1, 2 = SVC) → `IRQ_PRIO_LOWEST = 16 - 3 - 1 = 12`, 일반 IRQ 0–12 = 13개(그중 마지막은 PendSV와 같은 레벨).
2. **배치표** — 예제 2의 `DRONE`에 "광류 DRDY: 100 Hz, C = 3 µs, tol = 2000 µs, kernel = True"와 "ESC 텔레메트리 UART(DMA 없음, 1바이트 레지스터): 115200 baud, C = 1 µs, tol = 80 µs"를 넣고 다시 돌려라. 어느 레벨에 들어가나? UART를 DMA로 바꾸면(tol = 1000 µs)?
   정답/힌트: 도구로 돌려 확인한 답 — 광류는 레벨 4, UART(tol 80 µs ≥ BK)는 레벨 3(IMU 판정 40 ≥ 25 + 4 + 5 + 1). 레벨 3이 더 빠듯해졌다는 것이 진짜 답이다. DMA로 바꾸면(tol 1000 µs) 레벨 4로 내려간다.
3. **코드** — 예제 4에서 문지기를 쓰지 말고 FOC ISR이 10번에 한 번 `vTaskNotifyGiveFromISR`을 직접 부르게 바꿔 돌려라. 무엇이 일어나나? 그다음 configASSERT를 지운 빌드로 오래 돌리면?
   정답/힌트: `port.c:803` assert. assert를 지우면 대부분 잘 돌다가 logger의 임계구역 안에서 FOC가 리스트를 고치는 순간이 쌓이면 리스트가 깨진다 — "가끔" 버그의 원형. 예제 12 검사가 이 빌드를 실패시키는지도 확인.
4. **측정** — 예제 7에서 logger의 최대 임계구역을 20 µs → 5 µs로 줄이고(`x % 5u`) 레벨 3 배치의 히스토그램을 다시 그려라. 10 µs 초과 횟수는?
   정답/힌트: 레벨 3의 최악값이 약 5 µs + 탈출 비용으로 내려가 10 µs 초과가 거의 0이 된다. "선 아래 최상위"의 품질은 임계구역 길이가 정한다는 것의 확인.
5. **측정** — 예제 8에 FPU를 쓰는 코어를 가정해 보자. Cortex-M4F(`mps2-an386`, B2 예제 12)에서 ISR 몸체에 `float` 연산 한 줄을 넣고 같은 실험을 하면 레벨당 바이트는 얼마가 될지 먼저 계산하고, 템플릿 common을 복사해 직접 확인하라.
   정답/힌트: 하드웨어 프레임 32 → 104 B(26워드 + 정렬), 레벨당 약 184 B. lazy stacking이라도 공간은 예약된다(B6).
6. **설계** — 예제 9에 "L과 H를 같은 레벨에 둔다"는 다섯 번째 방법을 추가하라. H 대기와 Z · M 대기는? 이 방법이 안 되는 조건은?
   정답/힌트: H는 L이 끝날 때까지(약 10 µs) 기다리고 보호 코드는 0줄. H의 tol < L의 C이면 쓸 수 없다.
7. **검증** — 예제 11의 RTA에 "선 아래 ISR의 블로킹 BK에 ZL ISR 간섭을 더하라"(예제 4의 21.3 µs 교훈)를 반영해 드론 표를 다시 검증하라. 여전히 모두 OK인가?
   정답/힌트: BK를 25 → 25 + (ZL ISR들의 C 합 3.5) 정도로 늘리면 IMU_SPI_DMA가 37 µs로 아직 40 안. 여유가 3 µs뿐이라는 것이 리뷰 포인트.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| kernel-aware ISR | 커널을 아는 ISR | 커널 임계구역 마스크 이하 레벨. FromISR API를 부를 수 있고 임계구역에 막힌다 |
| zero-latency ISR (ZLI) | 커널이 지연을 더하지 않는 ISR | 마스크보다 급한 레벨. 커널 API 금지 |
| 선 (kernel line) | 두 세계의 경계 | FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`, Zephyr `_EXC_IRQ_DEFAULT_PRIO` |
| `configMAX_API_CALL_INTERRUPT_PRIORITY` | GIC 포트의 선 | Cortex-R5 · A9 · AArch64 포트, GIC 논리 우선순위 |
| `CONFIG_ZERO_LATENCY_LEVELS` | Zephyr ZLI 레벨 수 | 1이면 고정 레벨 0, 2 이상이면 인자를 그대로 하드웨어 레벨로 |
| 문지기 (doorbell) IRQ | 선 아래의 빈 IRQ 줄 | ZL ISR이 STIR · ISPR로 pend, 대신 커널을 부른다 |
| 배치표 (priority layout table) | 원천 → 레벨 표 | 주기 · C · tol · kernel 여부에서 계산한다 |
| tol (tolerable latency) | 기다릴 수 있는 시간 | 도착부터 ISR 시작까지 허용 지연 |
| BK | 커널 차단 예산 | 선 아래 ISR을 막는 가장 긴 임계구역 + 그 안의 ZL 간섭 |
| deadline-monotonic | 마감 순 배치 | 마감이 짧을수록 높은 우선순위 |
| rate-monotonic | 주기 순 배치 | 주기가 짧을수록 높은 우선순위 (마감 = 주기일 때) |
| 선점 레벨 (group priority) | 서로 선점하는 단위 | 같은 레벨끼리는 줄을 서고(tail-chain) 선점하지 않는다 |
| 중첩 깊이 | 동시에 active인 예외 수 | 최대값 = 사용하는 선점 레벨 수, MSP 크기를 정한다 |
| priority ceiling | 공유자 최고 레벨까지 올리기 | ISR 사이에서는 `BASEPRI_MAX` 한 명령 |
| RTA (response-time analysis) | 응답 시간 분석 | `R = C + B + Σ ⌈R/T⌉ × C`를 반복해 최악 응답을 구한다 |
| critical instant | 최악 위상 | 모든 높은 원천이 동시에 도착하는 순간. RTA의 가정 |
| `arch_zli_lock()` | Zephyr PRIMASK 잠금 | ZLI까지 막는다 — zero-latency 약속을 깬다 |
| 부팅 시 배치 검사 | NVIC 다시 읽기 | 표와 실제 바이트 · 선 · enable 상태를 비교 |

---

## 16. 요약 & 체크리스트

RTOS가 임계구역에서 올리는 BASEPRI가 인터럽트 세계를 둘로 나눈다. 선 아래 kernel-aware ISR은 커널을 쓰는 대신 가장 긴 임계구역(+ 그 안에 끼어든 zero-latency ISR 시간)만큼 늦을 수 있고, 선 위 zero-latency ISR은 커널에 막히지 않는 대신 커널과 직접 말할 수 없어 락 없는 버퍼와 문지기 IRQ로 우회한다. 배치표는 중요도가 아니라 원천마다의 주기 · 실행 시간 · 허용 지연에서 출발해, 허용 지연이 커널 차단 예산보다 짧은 것만 선 위에 두고, 각 세계를 마감 순서로 정렬하고, 서로 기다려 줄 수 있는 것은 같은 레벨에 묶어 중첩과 MSP를 줄인다. 레벨 사이 공유 자원은 ceiling(BASEPRI_MAX)이나 같은 레벨 배치로 보호하고, "상대만 끄기"는 ISR 사이 역전을 만든다. 마지막으로 RTA, 부팅 시 NVIC 검사, zero-latency ISR 정적 호출 검사로 배치가 맞다는 것을 숫자와 도구로 증명한다.

- [ ] kernel-aware와 zero-latency ISR의 차이와 각자 할 수 없는 일을 설명할 수 있다
- [ ] FreeRTOS Cortex-M의 `configMAX_SYSCALL_INTERRUPT_PRIORITY`와 GIC 포트의 `configMAX_API_CALL_INTERRUPT_PRIORITY`, M0에 선이 없는 이유를 말할 수 있다
- [ ] Zephyr `CONFIG_ZERO_LATENCY_IRQS` · `CONFIG_ZERO_LATENCY_LEVELS` · `IRQ_ZERO_LATENCY` · direct ISR의 관계와 레벨별 NVIC 바이트를 계산할 수 있다
- [ ] 원천마다 T, C, tol, kernel 여부를 적고 일곱 단계로 배치표를 만들 수 있다
- [ ] 드론 비행 제어기와 FOC 모터 컨트롤러의 배치표를 근거와 함께 그릴 수 있다
- [ ] 링 + 문지기 패턴을 구현하고 그 지연(전류 루프 vs 알림)을 구분해 말할 수 있다
- [ ] 선 아래 대기 예산에 ZL ISR 간섭이 더해지는 이유를 설명할 수 있다
- [ ] 레벨 수 → 중첩 깊이 → MSP 크기를 계산하고, FreeRTOS가 PRIGROUP 서브 비트를 허용하지 않는 이유를 안다
- [ ] 레벨이 다른 ISR 사이 공유 자원을 ceiling으로 보호하고, 상대만 끄기가 왜 역전을 만드는지 측정 숫자로 말할 수 있다
- [ ] 중요도와 긴급도를 구분하고 rate/deadline-monotonic 배치를 RTA로 검증할 수 있다
- [ ] 부팅 시 배치 검사와 ZL ISR 정적 호출 검사를 만들어 CI에 넣을 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM J8 | 우선순위 · 중첩 설계 — 선점 그룹 | §6 (중첩 깊이 · MSP · PRIGROUP, 예제 8) | RTOS 제약이 없을 때 `NVIC_SetPriorityGrouping()` · `NVIC_EncodePriority()`로 그룹 · 서브를 실제로 쓰는 법 |
| BM J8 | 우선순위 · 중첩 설계 — 우선순위 배치표 | §2, §3, §4, §8 (작성법, 드론 · FOC 표, RTA) | 커널 선 없이 CMSIS `NVIC_SetPriority()`만으로 같은 표를 구현하는 코드, 벡터 테이블 정리 |
| BM J8 | 우선순위 · 중첩 설계 — 공유 자원과 우선순위 | §7 (ISR 사이 역전, ceiling, 예제 9) | 슈퍼 루프(main) ↔ ISR 공유를 `__get_BASEPRI()` / `__set_BASEPRI_MAX()`로 감싸는 CMSIS 관점 |
| BM J7 · RTOS E3 | 인터럽트 지연 측정 | §5 (배치 결정의 측정, 예제 7) | 측정 기법 자체는 RTOS E3이 주인 — Bare-Metal은 링크만 |

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `portable/GCC/ARM_CM3/port.c`(`xPortStartScheduler`, `vPortValidateInterruptPriority`, `prvPortStartFirstTask`), `portable/GCC/ARM_CM3/portmacro.h`, `portable/GCC/ARM_CR5/port.c`(`configMAX_API_CALL_INTERRUPT_PRIORITY` 검사), `examples/template_configuration/FreeRTOSConfig.h`(두 이름의 관계 주석)
- FreeRTOS 문서, "Running the RTOS on a ARM Cortex-M Core": https://www.freertos.org/RTOS-Cortex-M3-M4.html
- FreeRTOS 문서, "Using FreeRTOS on ARM Cortex-A Embedded Processors"(GIC 포트의 `configMAX_API_CALL_INTERRUPT_PRIORITY`): https://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
- Zephyr v4.4.2 소스: `arch/arm/core/cortex_m/Kconfig`(`ZERO_LATENCY_IRQS`, `ZERO_LATENCY_LEVELS`), `arch/arm/core/cortex_m/irq_manage.c`(`arm_irq_priority_set`), `include/zephyr/arch/arm/irq.h`(`IRQ_ZERO_LATENCY`, `_CHECK_PRIO`, `ARCH_IRQ_CONNECT`), `include/zephyr/arch/arm/cortex_m/exception.h`, `include/zephyr/arch/arm/asm_inline_gcc.h`(`arch_irq_lock`, `arch_zli_lock`), `drivers/timer/nrf_rtc_timer.c`, `drivers/timer/Kconfig.nrf_rtc`
- Zephyr 문서, Interrupts — Zero Latency Interrupts 절(`doc/kernel/services/interrupts.rst`): https://docs.zephyrproject.org/latest/kernel/services/interrupts.html
- Arm, Armv7-M Architecture Reference Manual (ARM DDI 0403) — 예외 우선순위, BASEPRI, PRIGROUP, fault 승격 규칙
- Arm, Cortex-M3 Technical Reference Manual — 예외 진입 · tail-chaining
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", Journal of the ACM, 1973 — rate-monotonic
- T. P. Baker, "Stack-based scheduling of realtime processes", Real-Time Systems, 1991 — Stack Resource Policy(ISR 사이 ceiling의 이론적 배경)
- 이 저장소의 관련 노트: RTOS B2, B3, B4, B6, A7, C7, D3, D8, E1, E3, E4, F3, N2, N3, EL H6
