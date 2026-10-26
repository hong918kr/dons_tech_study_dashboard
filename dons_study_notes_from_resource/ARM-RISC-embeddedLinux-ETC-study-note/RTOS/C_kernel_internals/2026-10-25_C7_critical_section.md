# C7. 크리티컬 섹션 구현 — PRIMASK · BASEPRI · 스케줄러 잠금을 소스로 읽고, 길이 L이 지연에 얼마를 더하는지 잰다

> **이 노트를 다 읽으면**: 태스크(메인) ↔ ISR 데이터 레이스를 QEMU에서 직접 일으켜 잃어버린 갱신 수를 셀 수 있다 · PRIMASK 저장/복원, BASEPRI(`taskENTER_CRITICAL`), 스케줄러 잠금(`vTaskSuspendAll`), 뮤텍스 중 무엇을 언제 쓰는지 표 하나로 고를 수 있다 · FreeRTOS ARM_CM3/ARM_CM0 포트와 Zephyr `irq_lock`/`k_spin_lock`/`k_sched_lock`의 실제 구현(중첩을 세는 방식까지)을 설명할 수 있다 · 크리티컬 섹션 길이 L이 IRQ 지연과 태스크 지연에 얼마를 더하는지 측정 차트로 보여 줄 수 있다
> **왜 필요한가**: 면접 단골 "크리티컬 섹션은 어떻게 구현하나? 안에서 하면 안 되는 일은?"에 "인터럽트를 끈다"에서 멈추지 않고 **"Cortex-M3 이상은 BASEPRI로 커널이 아는 인터럽트만 막고, 중첩은 카운터로 세며, 길이가 곧 최악 지연이라 µs 단위로 재서 관리한다"**까지 말해야 통과한다. 실무에서는 "가끔 카운터가 안 맞는다", "가끔 시간이 거꾸로 간다", "시스템이 통째로 멈췄다"의 원인 1순위다
> **Don 기준 난이도**: 인터럽트 끄기 · 공유 변수 · SSD 펌웨어의 멀티코어 락은 이미 안다 / 새로 배울 부분은 FreeRTOS가 PRIMASK가 아니라 BASEPRI를 쓰는 이유와 그 대가, 중첩 카운터가 숨기는 함정(스케줄러 시작 전 0xaaaaaaaa), 스케줄러 잠금이 "ISR은 막지 않지만 태스크 지연은 그대로"라는 측정 결과, Zephyr의 key 방식 중첩
> **선행 노트**: A4(태스크 모델), A7(하이브리드 — kernel-aware IRQ가 50 µs 임계구역 아래서 50.84 µs 기다림 vs zero-latency 160 ns), B3(NVIC · BASEPRI 하드웨어), EL F4(Linux 락 — `spin_lock_irqsave` 등은 참조만). 이어지는 노트: D2(뮤텍스), D8(락 없는 링버퍼 · LDREX/STREX), E1 · E2(FromISR API · 우선순위 배치)
> 🔁 **Bare-Metal 겹침**: BM J2(메인 ↔ ISR 데이터 레이스) · J3(락 없는 링버퍼) · J4(원자 연산) — 이 노트가 주인(크리티컬 섹션 구현과 메인 ↔ ISR 레이스). Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 `__disable_irq()`/`__set_PRIMASK()`만으로 슈퍼 루프의 공유 변수를 지키는 관점"만 더한다. 링버퍼와 LDREX/STREX의 깊은 설명은 RTOS D8이 주인이다

---

## 0. 큰 그림 — "한 번에 한 명만"을 하드웨어 몇 개로 만드나

Don이 SSD 컨트롤러 펌웨어에서 겪었던 장면을 떠올려 보자. 명령 큐의 tail 인덱스를 펌웨어 루프가 올리는데, 같은 인덱스를 DMA 완료 인터럽트도 건드린다. 평소에는 멀쩡하다가 부하 시험 몇 시간 뒤에 명령 하나가 사라진다. 원인은 `tail++` 한 줄이 실제로는 **읽기 → 더하기 → 쓰기** 세 단계이고, 그 사이에 ISR이 끼어들었기 때문이다. 해결은 대개 "그 세 줄 동안만 인터럽트를 끈다"였다. 그 "동안"을 **크리티컬 섹션(critical section)**이라고 부른다 — 한 번에 한 실행 흐름만 들어가야 하는 코드 구간이다.

RTOS에서는 끼어드는 주체가 둘로 늘어난다. **ISR**(하드웨어 인터럽트)과 **다른 태스크**(스케줄러의 선점)다. 그래서 막는 도구도 여러 단계가 생긴다. 무엇을 막느냐가 다르고, 대가(지연)도 다르다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">네 가지 도구가 "막는 것" — 위로 갈수록 강하고 비싸다</text>
<text x="215" y="48" font-size="12" text-anchor="middle">zero-latency</text>
<text x="215" y="63" font-size="12" text-anchor="middle">ISR</text>
<text x="325" y="48" font-size="12" text-anchor="middle">kernel-aware</text>
<text x="325" y="63" font-size="12" text-anchor="middle">ISR</text>
<text x="435" y="48" font-size="12" text-anchor="middle">다른 태스크</text>
<text x="435" y="63" font-size="12" text-anchor="middle">(선점)</text>
<text x="590" y="48" font-size="12" text-anchor="middle">IRQ 지연에</text>
<text x="590" y="63" font-size="12" text-anchor="middle">더해지는 값</text>
<rect x="10" y="75" width="145" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="82" y="94" font-size="12" text-anchor="middle">PRIMASK</text>
<text x="82" y="110" font-size="12" text-anchor="middle">cpsid i</text>
<rect x="165" y="75" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="215" y="102" font-size="12" text-anchor="middle">막음</text>
<rect x="275" y="75" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="325" y="102" font-size="12" text-anchor="middle">막음</text>
<rect x="385" y="75" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="435" y="102" font-size="12" text-anchor="middle">막음</text>
<text x="590" y="102" font-size="12" text-anchor="middle">모든 IRQ에 +L</text>
<rect x="10" y="127" width="145" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="82" y="146" font-size="12" text-anchor="middle">BASEPRI</text>
<text x="82" y="162" font-size="12" text-anchor="middle">taskENTER_CRITICAL</text>
<rect x="165" y="127" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="215" y="154" font-size="12" text-anchor="middle">통과</text>
<rect x="275" y="127" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="325" y="154" font-size="12" text-anchor="middle">막음</text>
<rect x="385" y="127" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="435" y="154" font-size="12" text-anchor="middle">막음</text>
<text x="590" y="154" font-size="12" text-anchor="middle">aware IRQ에만 +L</text>
<rect x="10" y="179" width="145" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="82" y="198" font-size="12" text-anchor="middle">스케줄러 잠금</text>
<text x="82" y="214" font-size="12" text-anchor="middle">vTaskSuspendAll</text>
<rect x="165" y="179" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="215" y="206" font-size="12" text-anchor="middle">통과</text>
<rect x="275" y="179" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="325" y="206" font-size="12" text-anchor="middle">통과</text>
<rect x="385" y="179" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="435" y="206" font-size="12" text-anchor="middle">막음</text>
<text x="590" y="206" font-size="12" text-anchor="middle">IRQ 0, 태스크 +L</text>
<rect x="10" y="231" width="145" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="82" y="250" font-size="12" text-anchor="middle">뮤텍스 (D2)</text>
<text x="82" y="266" font-size="12" text-anchor="middle">xSemaphoreTake</text>
<rect x="165" y="231" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="215" y="258" font-size="12" text-anchor="middle">통과</text>
<rect x="275" y="231" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="325" y="258" font-size="12" text-anchor="middle">통과</text>
<rect x="385" y="231" width="100" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="435" y="250" font-size="12" text-anchor="middle">같은 자원을</text>
<text x="435" y="266" font-size="12" text-anchor="middle">원하는 것만</text>
<text x="590" y="258" font-size="12" text-anchor="middle">경쟁자만 기다림</text>
</svg>
```

그림 1 — 네 가지 보호 도구. 빨강 = 그 주체가 끼어들지 못하게 막는다, 초록 = 그대로 끼어든다. 오른쪽 칸의 값은 이 노트 §6에서 직접 잰 결과의 요약이다(L = 크리티컬 섹션 길이).

이 노트의 순서는 다음과 같다.

1. **문제를 먼저 일으킨다** — 진짜 레이스(잃어버린 갱신, 찢어진 64비트 타임스탬프)를 QEMU에서 숫자로 센다 (§1).
2. **해법 사다리** — PRIMASK 저장/복원(§2) → BASEPRI와 FreeRTOS 중첩 카운터(§3) → 스케줄러 잠금(§4) → 뮤텍스와 "언제 무엇을" 결정표(§5).
3. **측정** — 길이 L에 따른 IRQ 지연 · 태스크 지연 곡선, 실제 워크로드에서 BASEPRI가 올라가 있던 총 시간과 히스토그램(§6).
4. **규칙** — 짧게, 블로킹 금지(assert와 실제 멈춤을 재현), printf 금지, 최악값을 잰다(§7).
5. **Zephyr** — `irq_lock` key 방식, `k_spin_lock`이 UP에서는 곧 irq lock, `k_sched_lock`(§8).
6. 커널 자신은 크리티컬 섹션을 어떻게 쓰나(`xQueueGenericSend`) — §9.

### Bare-Metal · Linux 노트와의 경계

- **NVIC 하드웨어**(구현 우선순위 비트 수, PRIGROUP, BASEPRI의 비교 규칙, FAULTMASK)는 **B3**가 주인이다. 여기서는 "BASEPRI = 이 값 이상(숫자)의 우선순위를 막는 문턱"이라는 한 줄만 쓰고, 그 문턱을 **커널이 어떻게 쓰는지**를 깊게 본다.
- **락 없는 방법**(SPSC 링버퍼, LDREX/STREX, C11 atomics, DMB)은 **D8**이 주인이다. §1의 hi-lo-hi 재시도 읽기는 레이스를 이해하는 데 필요한 만큼만 보인다.
- **Linux 커널 락**(`spin_lock_irqsave`, `mutex`, lockdep)은 EL F4에서 다뤘다. Linux의 `spin_lock_irqsave(lock, flags)`는 이 노트의 "PRIMASK 저장 → 끄기 → 복원"과 같은 모양이고, 차이는 SMP라서 스핀락 변수가 하나 더 붙는다는 것뿐이다(Zephyr `k_spin_lock`이 정확히 이 구조다, §8).

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위에서 bare-metal, FreeRTOS V11.3.2(ARM_CM3 포트), Zephyr v4.4.2를 돌린다. 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 결정적)이다. **숫자는 QEMU icount 기준이며 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 12사이클을 모델링하지 않음). 이 노트가 숫자로 보이려는 것은 **모양**(L에 비례하나, 평평한가)과 **상대 비교**다. 시계는 A7 예제 1에서 확인한 FPGAIO COUNTER(0x40028018, 25 MHz, 1 카운트 = 40 ns)다.

모든 예제가 쓰는 작은 헤더 `c7.h`다(A7의 `a7.h`를 이 노트용으로 줄이고 PRIMASK · BASEPRI 접근자를 더했다).

```c
/* c7.h — C7 measurement helpers for QEMU mps2-an385 (bare-metal + FreeRTOS) */
#ifndef C7_H
#define C7_H
#include "board.h"
#define CYC()        REG32(0x40028018u)            /* FPGAIO 25 MHz counter: 1 cyc = 40 ns */
#define CYC_NS(c)    ((unsigned)((c) * 40u))
/* TIMER0 (IRQ 8) periodic, DUALTIMER timer1 (IRQ 10) periodic */
#define DT_LOAD      REG32(0x40002000u)
#define DT_VALUE     REG32(0x40002004u)
#define DT_CTRL      REG32(0x40002008u)
#define DT_INTCLR    REG32(0x4000200Cu)
static inline void timer0_start(uint32_t period, uint8_t prio)
{
    TIMER_CTRL(TIMER0_BASE) = 0;
    TIMER_RELOAD(TIMER0_BASE) = period - 1;
    TIMER_VALUE(TIMER0_BASE) = period - 1;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    NVIC_IPR(8) = prio;
    NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
}
static inline void timer0_stop(void) { TIMER_CTRL(TIMER0_BASE) = 0; }
static inline uint32_t timer0_elapsed(void)       /* cycles since TIMER0 raised its IRQ */
{
    return TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE);
}
static inline void dt_start(uint32_t period, uint8_t prio)
{
    DT_CTRL = 0;
    DT_LOAD = period - 1;
    NVIC_IPR(10) = prio;
    NVIC_ISER0 = 1u << 10;
    DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u;      /* enable, periodic, IRQ, 32-bit */
}
static inline uint32_t dt_elapsed(void) { return DT_LOAD - DT_VALUE; }
static inline void spin_cyc(uint32_t c) { uint32_t t = CYC(); while (CYC() - t < c) { } }
/* PRIMASK / BASEPRI accessors (what CMSIS __get_PRIMASK etc. do) */
static inline uint32_t get_primask(void) { uint32_t r; __asm volatile("mrs %0, primask" : "=r"(r)); return r; }
static inline uint32_t get_basepri(void) { uint32_t r; __asm volatile("mrs %0, basepri" : "=r"(r)); return r; }
static inline void set_primask(uint32_t v) { __asm volatile("msr primask, %0" : : "r"(v) : "memory"); }
static inline uint32_t irq_save(void)              /* save PRIMASK, then mask */
{
    uint32_t r = get_primask();
    __asm volatile("cpsid i" : : : "memory");
    return r;
}
static inline void irq_restore(uint32_t r) { set_primask(r); }
#endif
```

`timer0_elapsed()`가 지연 측정의 핵심이다. TIMER0은 0을 치는 순간 IRQ를 올리고 RELOAD로 다시 장전되므로, 어디서든 `RELOAD - VALUE`를 읽으면 "IRQ가 걸린 뒤 몇 사이클"이 나온다. ISR 첫 줄에서 읽으면 IRQ 진입 지연, 깨어난 태스크에서 읽으면 IRQ → 태스크 지연이다. DUALTIMER도 같은 방식이다(`dt_elapsed()`).

---

## 1. 문제 먼저 — 태스크(메인) ↔ ISR 데이터 레이스

> 🔁 이 절은 Bare-Metal BM J2(메인 ↔ ISR 데이터 레이스)와 겹친다 (주인: 이 노트).

### 1.1 직관 — `count++`는 한 줄이지만 세 걸음이다

C의 `g_count++`는 Cortex-M에서 **LDR(메모리 → 레지스터) → ADD → STR(레지스터 → 메모리)** 세 명령이다. 메모리에 있는 값을 레지스터로 "복사해 와서" 고친 다음 "덮어쓴다". 복사해 온 순간부터 덮어쓰기 전까지 ISR이 같은 변수를 고치면, 메인의 STR이 ISR의 결과를 **덮어 지운다**. 이걸 **잃어버린 갱신(lost update)**, 또는 RMW(read-modify-write) 레이스라고 부른다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">RMW 레이스 — 메인의 LDR과 STR 사이에 ISR이 끼면 ISR의 +1이 사라진다</text>
<text x="10" y="68" font-size="12">메인</text>
<rect x="60" y="50" width="90" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="105" y="69" font-size="12" text-anchor="middle">LDR r3=41</text>
<rect x="430" y="50" width="90" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="475" y="69" font-size="12" text-anchor="middle">ADD r3=42</text>
<rect x="530" y="50" width="90" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="575" y="69" font-size="12" text-anchor="middle">STR 42</text>
<text x="10" y="128" font-size="12">ISR</text>
<rect x="170" y="110" width="80" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="210" y="129" font-size="12" text-anchor="middle">LDR 41</text>
<rect x="255" y="110" width="80" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="295" y="129" font-size="12" text-anchor="middle">ADD 42</text>
<rect x="340" y="110" width="80" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="380" y="129" font-size="12" text-anchor="middle">STR 42</text>
<line x1="155" y1="80" x2="168" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="422" y1="108" x2="432" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="10" y="188" font-size="12">메모리</text>
<rect x="60" y="170" width="300" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="210" y="189" font-size="12" text-anchor="middle">g_count = 41</text>
<rect x="365" y="170" width="160" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="445" y="189" font-size="12" text-anchor="middle">42 (ISR가 씀)</text>
<rect x="530" y="170" width="140" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="600" y="189" font-size="12" text-anchor="middle">42 (43이어야)</text>
<line x1="60" y1="225" x2="670" y2="225" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="640" y="243" font-size="12" text-anchor="end">시간</text>
<text x="60" y="243" font-size="12">두 번 더했는데 한 번만 남는다 = 잃어버린 갱신 1개</text>
</svg>
```

그림 2 — RMW 레이스. 메인이 41을 레지스터로 가져간 뒤 ISR이 42를 써도, 메인은 자기 레지스터의 41에 1을 더해 42를 덮어쓴다.

### 1.2 예제 1 — 잃어버린 갱신을 세기 (bare-metal)

무엇을 확인하나: 메인 루프와 TIMER0 ISR(3.88 µs마다)이 같은 `g_count`를 `++`한다. "메인이 더한 수 + ISR이 더한 수"와 실제 값의 차이가 잃어버린 갱신 수다.

```c
/* ex1 — main <-> ISR read-modify-write race on a shared counter (bare-metal) */
#include "c7.h"
#define N 200000u
volatile uint32_t g_count;                  /* shared: main and ISR both do ++ */
volatile uint32_t g_isr_n;

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    g_count++;                              /* ISR side: ldr / adds / str */
    g_isr_n++;
}
int main(void)
{
    timer0_start(97, 0x40);                 /* IRQ every 97 cycles = 3.88 us */
    for (uint32_t i = 0; i < N; i++)
        g_count++;                          /* main side: same ldr / adds / str */
    timer0_stop();
    uint32_t expect = N + g_isr_n;
    board_printf_raw("main adds=%u isr adds=%u expected=%u got=%u lost=%u\n",
                     N, g_isr_n, expect, g_count, expect - g_count);
    return 0;
}
```

세 가지 조건으로 돌렸다: ICOUNT(결정적 가상 시간), ICOUNT 없음(호스트 속도), 그리고 ICOUNT + `one-insn-per-tb`(QEMU가 명령 하나마다 인터럽트를 확인하게 강제 — 인터럽트가 "아무 명령 사이에나" 들어올 수 있는지 확인용).

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex1_race.c OUT=/var/tmp/rtos/C7/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex1/app.elf; /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex1/app.elf; cd /var/tmp/rtos/C7/ex1; timeout 20 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -accel tcg,one-insn-per-tb=on -kernel app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4304	     96	    336	   4736	   1280	/var/tmp/rtos/C7/ex1/app.elf
main adds=200000 isr adds=9153 expected=209153 got=203662 lost=5491
[board_exit] code=0
main adds=200000 isr adds=84 expected=200084 got=200084 lost=0
[board_exit] code=0
main adds=200000 isr adds=9153 expected=209153 got=203662 lost=5491
[board_exit] code=0
```

출력에서 볼 것: ICOUNT에서 ISR이 9153번 더했는데 **5491번(60%)이 사라졌다**. `one-insn-per-tb`로 돌려도 숫자가 똑같다 — icount 모드는 이미 인터럽트를 정확한 명령 경계에 넣고 있다는 뜻이다. ICOUNT 없이 돌리면(호스트가 루프를 순식간에 끝내서 ISR이 84번밖에 안 옴) `lost=0`이다. **"내 책상에서는 재현이 안 된다"의 정체**가 이것이다 — 레이스는 타이밍이 맞아야만 보인다.

왜 하필 60%인가? 메인 루프의 기계어를 보면 답이 나온다.

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/C7/ex1/app.elf | sed -n "/<main>:/,/^$/p" | sed -n 22,26p'
```

```text
     122:	ldr	r3, [r5, #0]
     124:	subs	r4, #1
     126:	add.w	r3, r3, #1
     12a:	str	r3, [r5, #0]
     12c:	bne.n	122 <main+0x3e>
```

루프 한 바퀴는 명령 5개다. 인터럽트가 들어올 수 있는 경계는 5곳이고, 그중 `ldr` 뒤 · `subs` 뒤 · `add` 뒤의 **3곳**이 "읽었는데 아직 안 쓴" 창(window)이다. 3/5 = 60%, 측정 5491/9153 = 59.99%. 레이스 확률은 **창의 길이 ÷ 루프 길이**라는 단순한 산수다. 실제 칩에서는 ISR 주기가 훨씬 길어서(예: 1 kHz) 확률이 수천 분의 1로 떨어진다 — 그래서 몇 시간에 한 번 터진다.

**Don 경험과 연결**: SSD 펌웨어에서 "통계 카운터가 조금씩 안 맞는다"는 버그가 바로 이것이다. 통계 카운터는 틀려도 기능이 안 깨지니 오래 숨어 있는다. 같은 패턴이 큐 인덱스에 있으면 명령 유실이 된다.

### 1.3 예제 2 — 찢어진 64비트 타임스탬프 (tearing)

두 번째 고전 레이스는 **찢어진 읽기(torn read, tearing)**다. 32비트 코어에서 64비트 값(또는 구조체 두 필드)을 읽는 데 명령 두 개가 필요하면, 그 사이에 값이 바뀌어 "앞 절반은 옛 값, 뒤 절반은 새 값"을 읽는다.

가장 흔한 실무 형태는 **"하드웨어 타이머 + 소프트웨어 상위 워드"로 만든 긴 시계**다. 드론 비행 제어기라면 IMU 샘플마다 µs 타임스탬프를 찍는데, 하드웨어 타이머는 16/32비트라 금방 넘친다(wrap). 그래서 넘칠 때마다 ISR이 `g_hi++`를 하고, `now()`는 `g_hi`와 하드웨어 카운터를 합친다. 두 번 읽는 사이에 wrap이 일어나면 시간이 **한 주기만큼 과거로** 돌아간다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">찢어진 읽기 — hi를 읽은 뒤 lo를 읽기 전에 타이머가 넘친다</text>
<text x="10" y="63" font-size="12">now()</text>
<rect x="70" y="45" width="120" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="130" y="64" font-size="12" text-anchor="middle">hi = g_hi → 7</text>
<rect x="390" y="45" width="130" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="455" y="64" font-size="12" text-anchor="middle">lo = 하드웨어 → 3</text>
<rect x="530" y="45" width="140" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="600" y="64" font-size="12" text-anchor="middle">(7, 3) 과거로!</text>
<text x="10" y="118" font-size="12">타이머</text>
<rect x="200" y="100" width="90" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="245" y="119" font-size="12" text-anchor="middle">wrap: lo→0</text>
<text x="10" y="168" font-size="12">ISR</text>
<rect x="295" y="150" width="90" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="169" font-size="12" text-anchor="middle">g_hi = 8</text>
<line x1="245" y1="130" x2="300" y2="148" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="10" y="210" font-size="12">바로 전 now() = (7, 50000) → 이번 now() = (7, 3): 한 주기(2 ms)만큼 시간이 거꾸로 간다</text>
</svg>
```

그림 3 — 찢어진 읽기. 정답은 (8, 3)인데 hi는 옛 값, lo는 새 값이라 (7, 3)이 된다.

무엇을 확인하나: 네 가지 `now()` 구현을 각각 200번의 wrap 동안 계속 불러, "시간이 거꾸로 간 횟수"를 센다. ① 그냥 두 번 읽기, ② 인터럽트를 끄고 두 번 읽기, ③ 끄고 읽은 다음 "wrap이 났는데 ISR이 아직 못 돈" 경우를 보정, ④ hi → lo → hi를 읽어 hi가 바뀌었으면 다시(락 없는 재시도 — 깊은 설명은 D8).

```c
/* ex2 — 64-bit timestamp tearing: HW down-counter + software wrap count */
#include "c7.h"
#define TOP      50020u                      /* TIMER0 counts TOP..0, IRQ on wrap (2 ms) */
#define T0_STAT  REG32(TIMER0_BASE + 0x0C)   /* read = INTSTATUS: wrapped, IRQ not yet cleared */
volatile uint32_t g_hi;                      /* incremented by ISR on each wrap */

void TIMER0_IRQHandler(void) { TIMER_INTCLR(TIMER0_BASE) = 1; g_hi++; }
static inline uint32_t hw_lo(void) { return TOP - TIMER_VALUE(TIMER0_BASE); }

static uint64_t now_naive(void)              /* two reads, nothing protects them */
{
    uint32_t hi = g_hi;
    uint32_t lo = hw_lo();
    return (uint64_t)hi * (TOP + 1) + lo;
}
static uint64_t now_masked(void)             /* masks IRQs, but HW still wraps! */
{
    uint32_t k = irq_save();
    uint32_t hi = g_hi, lo = hw_lo();
    irq_restore(k);
    return (uint64_t)hi * (TOP + 1) + lo;
}
static uint64_t now_masked_fix(void)         /* masked + account for a pending wrap */
{
    uint32_t k = irq_save();
    uint32_t hi = g_hi, lo = hw_lo();
    if (T0_STAT & 1u) { lo = hw_lo(); hi++; } /* wrapped, ISR not run yet */
    irq_restore(k);
    return (uint64_t)hi * (TOP + 1) + lo;
}
static uint64_t now_retry(void)              /* lock-free: hi, lo, hi again */
{
    uint32_t h1, h2, lo;
    do { h1 = g_hi; lo = hw_lo(); h2 = g_hi; } while (h1 != h2);
    return (uint64_t)h1 * (TOP + 1) + lo;
}
static void run(const char *name, uint64_t (*now)(void))
{
    uint32_t back = 0, calls = 0, start = g_hi;
    uint64_t prev = now();
    while (g_hi - start < 200) {             /* 200 wraps = 400 ms */
        uint64_t t = now();
        if (t < prev) back++;
        prev = t; calls++;
    }
    board_printf_raw("%s: calls=%u wraps=200 time-went-backwards=%u\n", name, calls, back);
}
int main(void)
{
    timer0_start(TOP + 1, 0x40);
    run("naive", now_naive);
    run("masked", now_masked);
    run("masked+pending", now_masked_fix);
    run("hi-lo-hi retry", now_retry);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex2_tear.c OUT=/var/tmp/rtos/C7/ex2 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex2/app.elf 60'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4580	     96	    336	   5012	   1394	/var/tmp/rtos/C7/ex2/app.elf
naive: calls=595410 wraps=200 time-went-backwards=40
masked: calls=520954 wraps=200 time-went-backwards=20
masked+pending: calls=403320 wraps=200 time-went-backwards=0
hi-lo-hi retry: calls=520948 wraps=200 time-went-backwards=0
[board_exit] code=0
```

출력에서 볼 것: 그냥 읽으면 wrap 200번 중 **40번(20%) 시간이 거꾸로 갔다**. 놀라운 것은 ②번 — **인터럽트를 꺼도 20번** 거꾸로 간다. 인터럽트를 끄면 ISR은 막지만 **하드웨어 카운터는 계속 돈다**. 끈 상태에서 wrap이 나면 ISR은 대기(pending) 상태라 `g_hi`는 옛 값인데 lo는 이미 0 근처다. ③번처럼 "wrap 플래그(INTSTATUS)가 서 있으면 hi를 하나 올려 계산"해야 0이 된다. ④번 재시도는 인터럽트를 끄지 않고도 0이다 — 단, **인터럽트가 켜진 문맥에서 부를 때만** 맞다(끈 상태에서 부르면 ISR이 영영 안 돌아 hi가 안 바뀌므로 ②와 같은 버그가 된다).

> 교훈: **크리티컬 섹션은 "소프트웨어 끼어들기"만 막는다. 하드웨어는 멈추지 않는다.** 타이머 · DMA 카운터 · FIFO 레벨처럼 하드웨어가 바꾸는 값과 소프트웨어 값을 섞을 때는 "끈 동안 일어난 일"을 반드시 따로 처리한다.

이 시계 확장 패턴은 FreeRTOS와 Zephyr에도 그대로 있다. Zephyr의 Cortex-M SysTick 드라이버(`drivers/timer/cortex_m_systick.c`)는 `k_spin_lock()` 안에서 카운터를 읽고 SysTick의 `COUNTFLAG`(wrap 플래그)를 확인해 놓친 wrap을 더한다 — ③번과 같은 해법이다(C5 · C6에서 tick 드라이버를 볼 때 다시 나온다).

### 1.4 `volatile uint64_t`는 안전한가? — 코어에 따라 다르다

같은 64비트 읽기를 Cortex-M3와 Cortex-M0으로 컴파일해서 비교했다.

```c
#include <stdint.h>
volatile uint64_t g_ts;
uint64_t read_ts(void) { return g_ts; }
void inc_ts(void) { g_ts++; }
```

```sh
limactl shell elinux -- bash -lc 'for c in cortex-m3 cortex-m0; do echo "== $c"; arm-none-eabi-gcc -mthumb -mcpu=$c -O2 -Wall -Wextra -c /Users/donh/.rtos_scratch/C7/u64.c -o /var/tmp/rtos/C7/u64_$c.o && arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/C7/u64_$c.o | sed -n "/<read_ts>:/,/^$/p"; done'
```

```text
== cortex-m3
00000000 <read_ts>:
   0:	ldr	r3, [pc, #4]	@ (8 <read_ts+0x8>)
   2:	ldrd	r0, r1, [r3]
   6:	bx	lr
   8:	.word	0x00000000

== cortex-m0
00000000 <read_ts>:
   0:	ldr	r3, [pc, #4]	@ (8 <read_ts+0x8>)
   2:	ldr	r0, [r3, #0]
   4:	ldr	r1, [r3, #4]
   6:	bx	lr
   8:	.word	0x00000000
```

출력에서 볼 것: M3에서는 GCC가 `ldrd` 한 명령으로 읽고, M0(ARMv6-M에는 LDRD가 없다)에서는 `ldr` 두 개로 쪼갠다. M0에서는 두 `ldr` 사이에 ISR이 들어와 바로 찢어진다. M3에서도 **안심하면 안 된다**. 아키텍처가 단일 복사 원자성(single-copy atomicity)을 보장하는 단위는 워드(32비트)이고, 컴파일러가 언제나 `ldrd`를 고른다는 약속도 없다(최적화 수준, 정렬, 다른 컴파일러). 그리고 `inc_ts()` 같은 RMW는 M3에서도 `ldrd → adds/adc → strd`라서 예제 1과 같은 레이스가 그대로 있다. **"32비트 정렬된 단일 읽기/쓰기만 원자적"**이라고 외워 두고, 나머지는 보호한다.

### 1.5 무엇이 위험한가 — 한 장 정리

| 공유 데이터 접근 | 32비트 Cortex-M에서 ISR에 대해 안전? | 이유 | 처방 |
|---|---|---|---|
| 정렬된 `uint32_t` 한 번 읽기 / 한 번 쓰기 | 안전 | LDR/STR 한 명령, 워드 단일 복사 원자성 | `volatile`만 (컴파일러가 캐시하지 않게) |
| `x++`, `x += n`, 비트 OR 대입 같은 RMW | **위험** | LDR → 연산 → STR 사이 창 | 크리티컬 섹션, 또는 LDREX/STREX(D8) |
| 64비트 값, 구조체 여러 필드 | **위험** (M0은 무조건) | 명령 두 개 이상 | 크리티컬 섹션, 시퀀스 카운터/재시도(D8) |
| 소프트웨어 값 + 하드웨어 카운터 조합 | **끈 상태에서도 위험** | 하드웨어는 계속 변한다 | 끈 상태 + pending 플래그 보정 (예제 2 ③) |
| 비트필드(`struct { unsigned a:1, b:1; }`) 쓰기 | **위험** | 컴파일러가 워드 전체를 RMW | 비트필드를 ISR과 공유하지 말 것 |
| `uint8_t` / `uint16_t` 한 번 쓰기 | 안전 (Cortex-M은 STRB/STRH) | 단일 명령 | 다만 옆 바이트와 RMW로 묶이지 않는지 확인 |

**흔한 함정**: `volatile`이 원자성을 준다고 믿는 것. `volatile`은 "매번 메모리에서 읽고 쓰라"는 컴파일러 지시일 뿐, 읽기와 쓰기 사이에 끼어드는 것을 막지 못한다. 예제 1의 `g_count`도 `volatile`이었다.

---

## 2. 해법 1 — PRIMASK: 모든 인터럽트를 끈다 (`cpsid i`)

> 🔁 이 절은 Bare-Metal BM J2(PRIMASK 저장 / 복원 크리티컬 섹션)와 겹친다 (주인: 이 노트).

### 2.1 정의

**PRIMASK**는 Cortex-M의 1비트 특수 레지스터다. 1이면 NMI와 HardFault를 뺀 **모든 설정 가능한 우선순위의 예외**가 막힌다. 명령은 둘이다.

- `cpsid i` — PRIMASK = 1 (Change Processor State, Interrupt Disable). CMSIS 이름은 `__disable_irq()`.
- `cpsie i` — PRIMASK = 0. CMSIS `__enable_irq()`.
- `mrs r0, primask` / `msr primask, r0` — 현재 값을 읽고 쓴다. CMSIS `__get_PRIMASK()` / `__set_PRIMASK()`.

막힌 동안 들어온 인터럽트는 버려지지 않고 **NVIC의 pending 비트에 남아 있다가** PRIMASK가 0이 되는 순간 들어온다. 다만 pending 비트는 1비트라서, 막힌 동안 **같은 인터럽트가 여러 번 오면 한 번으로 합쳐진다**(예제 4에서 실제로 20번이 1번이 되는 것을 본다).

### 2.2 올바른 모양 — 저장하고, 끄고, 복원한다

```c
uint32_t k = __get_PRIMASK();   /* 1. 지금 상태를 저장 (이미 꺼져 있었을 수도 있다) */
__disable_irq();                /* 2. 끈다 */
/* ... 짧은 크리티컬 섹션 ... */
__set_PRIMASK(k);               /* 3. "켠다"가 아니라 "원래대로 돌린다" */
```

3번이 핵심이다. `__enable_irq()`로 무조건 켜면, 이 함수를 **이미 인터럽트를 끈 호출자**가 불렀을 때 호출자의 보호까지 풀어 버린다. Linux의 `spin_lock_irqsave(lock, flags)` / `spin_unlock_irqrestore(lock, flags)`가 `_irq` 버전과 따로 있는 이유가 정확히 이것이다(EL F4 §4.6).

### 2.3 예제 3 — 예제 1을 고치고, "무조건 cpsie" 중첩 버그를 재현

무엇을 확인하나: 네 가지 몸체를 같은 ISR 부하(3.88 µs마다) 아래서 20만 번 돌린다. ① 보호 없음, ② PRIMASK 저장/복원, ③ 바깥은 제대로 껐는데 안에서 부른 helper가 `cpsid`/`cpsie`를 무조건 씀, ④ helper도 저장/복원. 각 몸체의 비용(ISR 없이 2만 번 돌린 평균)도 잰다.

```c
/* ex3 — PRIMASK critical sections: fixes ex1, and the blind-cpsie nesting bug */
#include "c7.h"
#define N 200000u
volatile uint32_t g_count, g_isr_n, g_other;
void TIMER0_IRQHandler(void) { TIMER_INTCLR(TIMER0_BASE) = 1; g_count++; g_isr_n++; }

static void helper_blind(void)              /* "my own little critical section" */
{
    __asm volatile("cpsid i" ::: "memory");
    g_other++;
    __asm volatile("cpsie i" ::: "memory"); /* BUG: unmasks even if caller had masked */
}
static void helper_nested(void)             /* correct: save / restore PRIMASK */
{
    uint32_t k = irq_save();
    g_other++;
    irq_restore(k);
}
static void body_none(void)   { g_count++; }
static void body_save(void)   { uint32_t k = irq_save(); g_count++; irq_restore(k); }
static void body_blind(void)  { uint32_t k = irq_save(); helper_blind();  g_count++; irq_restore(k); }
static void body_nested(void) { uint32_t k = irq_save(); helper_nested(); g_count++; irq_restore(k); }

static void run(const char *name, void (*body)(void))
{
    g_count = 0; g_isr_n = 0;
    uint32_t t = CYC();
    for (uint32_t i = 0; i < 20000; i++) body();     /* cost, no IRQ load */
    uint32_t cost = CYC() - t;
    g_count = 0; g_isr_n = 0;
    timer0_start(97, 0x40);
    for (uint32_t i = 0; i < N; i++) body();
    timer0_stop();
    uint32_t expect = N + g_isr_n;
    board_printf_raw("%s: lost=%u of %u isr adds, cost=%u ns/iter\n", name,
                     expect - g_count, g_isr_n, cost * 40 / 20000);
}
int main(void)
{
    run("none            ", body_none);
    run("PRIMASK save    ", body_save);
    run("nested, blind   ", body_blind);
    run("nested, saved   ", body_nested);
    board_printf_raw("PRIMASK at end = %u\n", get_primask());
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex3_primask.c OUT=/var/tmp/rtos/C7/ex3 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex3/app.elf 60'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4664	     96	    344	   5104	   13f0	/var/tmp/rtos/C7/ex3/app.elf
none            : lost=3660 of 14645 isr adds, cost=256 ns/iter
PRIMASK save    : lost=0 of 20137 isr adds, cost=352 ns/iter
nested, blind   : lost=3660 of 31121 isr adds, cost=544 ns/iter
nested, saved   : lost=0 of 32951 isr adds, cost=576 ns/iter
PRIMASK at end = 0
[board_exit] code=0
```

출력에서 볼 것:

- **보호 없음**: 14645 중 3660(25%)이 사라졌다(함수 호출이 끼어 루프가 길어져서 창의 비율이 예제 1보다 작다).
- **PRIMASK 저장/복원**: 0. 비용은 +96 ns = **명령 3개**(`mrs`, `cpsid`, `msr`) — icount에서 명령 1개 = 32 ns이니 정확히 맞다. 실제 칩에서도 몇 사이클이다. 크리티컬 섹션의 비용은 "들어가고 나오는 값"이 아니라 **"안에 머무는 시간"**이다(§6).
- **중첩, 무조건 cpsie**: 바깥 함수는 분명히 껐는데 **3660개가 그대로 사라졌다**. helper가 돌아온 순간 인터럽트가 켜져, 바깥의 `g_count++`가 보호 없이 실행된다. 숫자가 "보호 없음"과 정확히 같다(3660)는 것도 재미있다 — icount의 결정성 덕분에 같은 위상에서 같은 창이 열렸다.
- **중첩, 저장/복원**: 0.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">중첩된 크리티컬 섹션 — 안쪽이 "켠다"면 바깥의 남은 구간이 무방비가 된다</text>
<text x="10" y="58" font-size="12">무조건 cpsie</text>
<rect x="120" y="42" width="110" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="175" y="60" font-size="12" text-anchor="middle">바깥 cpsid</text>
<rect x="235" y="42" width="140" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="305" y="60" font-size="12" text-anchor="middle">helper cpsid…cpsie</text>
<rect x="380" y="42" width="170" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="465" y="60" font-size="12" text-anchor="middle">g_count++ (무방비!)</text>
<rect x="555" y="42" width="115" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="612" y="60" font-size="12" text-anchor="middle">바깥 restore</text>
<text x="10" y="118" font-size="12">저장 / 복원</text>
<rect x="120" y="102" width="110" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="175" y="120" font-size="12" text-anchor="middle">k1=0, cpsid</text>
<rect x="235" y="102" width="140" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="305" y="120" font-size="12" text-anchor="middle">k2=1 … PRIMASK=1</text>
<rect x="380" y="102" width="170" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="465" y="120" font-size="12" text-anchor="middle">g_count++ (보호됨)</text>
<rect x="555" y="102" width="115" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="612" y="120" font-size="12" text-anchor="middle">PRIMASK=k1=0</text>
<text x="10" y="178" font-size="12">FreeRTOS 카운터</text>
<rect x="120" y="162" width="110" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="175" y="180" font-size="12" text-anchor="middle">nesting 0→1</text>
<rect x="235" y="162" width="140" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="305" y="180" font-size="12" text-anchor="middle">1→2 … 2→1</text>
<rect x="380" y="162" width="170" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="465" y="180" font-size="12" text-anchor="middle">BASEPRI 그대로 0x20</text>
<rect x="555" y="162" width="115" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="612" y="180" font-size="12" text-anchor="middle">1→0: BASEPRI=0</text>
<text x="10" y="220" font-size="12">중첩을 지키는 두 방식: ① 값을 저장해 되돌린다 (PRIMASK 저장/복원, Zephyr key)</text>
<text x="10" y="240" font-size="12">② 깊이를 센다 (FreeRTOS uxCriticalNesting)</text>
</svg>
```

그림 4 — 중첩된 크리티컬 섹션의 세 가지 처리. 첫 줄의 빨간 구간이 예제 3에서 3660개를 잃어버린 곳이다.

### 2.4 PRIMASK의 장단점

- **장점**: 모든 Cortex-M(M0/M0+/M23 포함)에 있다. 명령 1개. 의미가 단순하다 — "이 코어에서는 지금 나 혼자다". 단일 코어에서는 이것만으로 ISR과 태스크 둘 다 막는다(PendSV · SysTick도 인터럽트이므로 선점도 일어나지 않는다).
- **단점**: **너무 많이 막는다.** 20 kHz FOC 전류 루프처럼 커널과 상관없는 ISR까지 기다리게 한다. 크리티컬 섹션 길이 L이 **모든** 인터럽트의 지연에 그대로 더해진다(§6 측정).
- **FAULTMASK**(`cpsid f`)는 HardFault까지 막는(NMI만 통과) 더 강한 마스크다. 일반 코드의 크리티컬 섹션에는 쓰지 않는다 — 자세히는 B3.

**Don 경험과 연결**: SSD 컨트롤러가 멀티코어였다면, 한 코어의 `cpsid i`는 **그 코어**의 인터럽트만 막는다. 다른 코어와 공유한 데이터에는 하드웨어 세마포어나 스핀락이 따로 필요했을 것이다. RTOS에서도 SMP(예: RP2040 듀얼 M0+, FreeRTOS SMP)면 같은 문제가 생기고, 그래서 FreeRTOS SMP 빌드의 `taskENTER_CRITICAL`은 인터럽트 마스크 + 코어 간 스핀락(`portGET_TASK_LOCK`/`portGET_ISR_LOCK`)이 된다(이 노트는 단일 코어만 측정한다).

---

## 3. 해법 2 — BASEPRI: 커널이 아는 인터럽트만 막는다 (FreeRTOS `taskENTER_CRITICAL`)

> 🔁 이 절은 Bare-Metal BM J2(BASEPRI)와 겹친다 (주인: 이 노트). BASEPRI 하드웨어 규칙 자체는 RTOS B3.

### 3.1 직관 — 문턱 하나

PRIMASK가 "전부 끄는 스위치"라면 **BASEPRI**는 "문턱"이다. BASEPRI에 값 X를 쓰면 **우선순위 숫자가 X 이상인(= 논리적으로 X만큼 또는 그보다 낮은) 인터럽트**만 막히고, 숫자가 X보다 작은(더 높은) 인터럽트는 그대로 들어온다. BASEPRI = 0이면 문턱이 없다(아무것도 안 막음). ARMv7-M(M3/M4/M7)과 ARMv8-M Mainline(M33/M55/M85)에만 있고, **M0/M0+/M23(ARMv6-M, ARMv8-M Baseline)에는 없다**.

FreeRTOS는 이 문턱을 `configMAX_SYSCALL_INTERRUPT_PRIORITY`로 정한다. lab 템플릿에서는 0x20이다. 그래서 우선순위 공간이 두 쪽으로 나뉜다(A7 그림 6과 같은 그림).

- 숫자 0x00 ~ 0x1F: **zero-latency ISR** — 커널이 절대 막지 않는다. 대신 FreeRTOS API를 하나도 부를 수 없다.
- 숫자 0x20 ~ 0xFF: **kernel-aware ISR** — `...FromISR` API를 부를 수 있다. 커널의 크리티컬 섹션 동안에는 기다린다. SysTick · PendSV(0xFF, `configKERNEL_INTERRUPT_PRIORITY`)도 여기 있다.

왜 이렇게 하나? 커널 자료구조(ready list, 이벤트 리스트)를 만지는 코드는 태스크든 ISR이든 **같은 문턱** 아래에서만 실행된다. 그러니 커널이 문턱을 올리면 "커널 자료구조를 만질 수 있는 모든 것"이 멈춘다 — 정확히 필요한 만큼만 막는다. 커널과 무관한 모터 ISR은 계속 돈다.

### 3.2 소스로 읽기 — `portmacro.h`의 세 인라인 함수

`portable/GCC/ARM_CM3/portmacro.h` (FreeRTOS V11.3.2):

```c
#define portSET_INTERRUPT_MASK_FROM_ISR()         ulPortRaiseBASEPRI()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )    vPortSetBASEPRI( x )
#define portDISABLE_INTERRUPTS()                  vPortRaiseBASEPRI()
#define portENABLE_INTERRUPTS()                   vPortSetBASEPRI( 0 )
#define portENTER_CRITICAL()                      vPortEnterCritical()
#define portEXIT_CRITICAL()                       vPortExitCritical()
```

```c
portFORCE_INLINE static void vPortRaiseBASEPRI( void )
{
    uint32_t ulNewBASEPRI;

    __asm volatile
    (
        "   mov %0, %1                                              \n" \
        "   msr basepri, %0                                         \n" \
        "   isb                                                     \n" \
        "   dsb                                                     \n" \
        : "=r" ( ulNewBASEPRI ) : "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) : "memory"
    );
}

portFORCE_INLINE static uint32_t ulPortRaiseBASEPRI( void )
{
    uint32_t ulOriginalBASEPRI, ulNewBASEPRI;

    __asm volatile
    (
        "   mrs %0, basepri                                         \n" \
        "   mov %1, %2                                              \n" \
        "   msr basepri, %1                                         \n" \
        "   isb                                                     \n" \
        "   dsb                                                     \n" \
        : "=r" ( ulOriginalBASEPRI ), "=r" ( ulNewBASEPRI ) : "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) : "memory"
    );

    /* This return will not be reached but is necessary to prevent compiler
     * warnings. */
    return ulOriginalBASEPRI;
}

portFORCE_INLINE static void vPortSetBASEPRI( uint32_t ulNewMaskValue )
{
    __asm volatile
    (
        "   msr basepri, %0 " ::"r" ( ulNewMaskValue ) : "memory"
    );
}
```

세 함수의 역할:

| 함수 | 하는 일 | 누가 쓰나 |
|---|---|---|
| `vPortRaiseBASEPRI()` | BASEPRI = 0x20 (이전 값은 **버린다**) + `isb`/`dsb` | `portDISABLE_INTERRUPTS` → `vPortEnterCritical`, SysTick 핸들러 |
| `ulPortRaiseBASEPRI()` | 이전 BASEPRI를 **반환**하고 0x20으로 올림 | `taskENTER_CRITICAL_FROM_ISR()` (ISR용) |
| `vPortSetBASEPRI(x)` | BASEPRI = x | `portENABLE_INTERRUPTS`(x = 0), `taskEXIT_CRITICAL_FROM_ISR(x)` |

`isb`/`dsb`는 "BASEPRI 쓰기가 다음 명령부터 확실히 효과를 내게" 하는 배리어다(깊은 설명은 D8, BM J5). 같은 파일의 `task.h` 쪽 매크로는 단일 코어 빌드에서 이렇게 연결된다.

```c
#define taskENTER_CRITICAL()                 portENTER_CRITICAL()
#if ( configNUMBER_OF_CORES == 1 )
    #define taskENTER_CRITICAL_FROM_ISR()    portSET_INTERRUPT_MASK_FROM_ISR()
...
#define taskEXIT_CRITICAL()                    portEXIT_CRITICAL()
#if ( configNUMBER_OF_CORES == 1 )
    #define taskEXIT_CRITICAL_FROM_ISR( x )    portCLEAR_INTERRUPT_MASK_FROM_ISR( x )
```

### 3.3 소스로 읽기 — `port.c`의 중첩 카운터

태스크용 크리티컬 섹션은 값을 저장하지 않고 **깊이를 센다**. `portable/GCC/ARM_CM3/port.c`:

```c
static UBaseType_t uxCriticalNesting = 0xaaaaaaaa;      /* line 141 */
...
void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;

    /* This is not the interrupt safe version of the enter critical function so
     * assert() if it is being called from an interrupt context.  Only API
     * functions that end in "FromISR" can be used in an interrupt.  Only assert if
     * the critical nesting count is 1 to protect against recursive calls if the
     * assert function also uses a critical section. */
    if( uxCriticalNesting == 1 )
    {
        configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 );
    }
}
void vPortExitCritical( void )
{
    configASSERT( uxCriticalNesting );
    uxCriticalNesting--;

    if( uxCriticalNesting == 0 )
    {
        portENABLE_INTERRUPTS();
    }
}
```

읽을 점 네 가지.

1. **들어갈 때마다 무조건 BASEPRI = 0x20**, 나올 때는 **카운터가 0이 될 때만** BASEPRI = 0. 중첩은 카운터가 지킨다. 대신 "들어가기 전 BASEPRI가 0이었다"고 **가정**한다. 그래서 태스크 코드에서 `taskDISABLE_INTERRUPTS()`(카운터를 안 올림)로 끈 뒤 `taskENTER_CRITICAL()/EXIT` 짝을 부르면, EXIT가 BASEPRI를 0으로 내려 바깥의 끄기를 지워 버린다(예제 4 마지막 두 줄). 2.3절의 "무조건 cpsie" 버그의 FreeRTOS판이다.
2. **ISR 안에서 부르면 assert**. `portNVIC_INT_CTRL_REG`는 ICSR(0xE000ED04), 아래 8비트 VECTACTIVE는 "지금 실행 중인 예외 번호"다. 0이 아니면 ISR(Handler 모드)이라는 뜻이다(예제 5).
3. **초기값 0xaaaaaaaa**. 스케줄러가 시작하기 전에 `xTaskCreate`, `xQueueCreate` 등이 크리티컬 섹션을 쓰면 EXIT 후에도 카운터가 0이 안 되므로 **BASEPRI가 0x20으로 남는다**. 의도된 동작이다 — 스케줄러가 시작하기 전에 kernel-aware ISR이 API를 부르면 안 되기 때문이다. `xPortStartScheduler()`가 `uxCriticalNesting = 0`으로 바꾸고(line 399), 첫 태스크를 띄우는 `vPortSVCHandler`가 `msr basepri, r0`(r0 = 0)로 문턱을 내린다. 결과: **`main()`에서 태스크를 만든 뒤 `vTaskStartScheduler()` 전에 인터럽트가 안 들어온다**. "main에서 UART 초기화하고 printf 인터럽트를 기다리는데 멈춘다"의 원인이 이것이다.
4. 카운터는 **전역 변수 하나**다(태스크별로 저장하지 않는다). 크리티컬 섹션 안에서는 PendSV가 막혀 있으므로 컨텍스트 스위치가 일어나지 않는다는 전제에 기대고 있다. 그래서 크리티컬 섹션 안에서 블로킹 API를 부르면 이 전제가 깨진다(§7, 예제 11).

ISR용은 반대로 **값을 저장한다**. `taskENTER_CRITICAL_FROM_ISR()`이 돌려준 이전 BASEPRI를 `taskEXIT_CRITICAL_FROM_ISR(x)`에 넘겨 되돌린다. ISR은 중첩될 수 있고(더 높은 ISR이 낮은 ISR을 선점), ISR마다 진입 시 BASEPRI가 다를 수 있으니 전역 카운터 하나로는 셀 수 없기 때문이다.

### 3.4 예제 4 — BASEPRI 값, 중첩, FromISR, 그리고 합쳐지는 인터럽트

무엇을 확인하나: ① `main()`에서 태스크를 만들기 전후의 BASEPRI, ② 태스크에서 중첩 진입/탈출 때마다 BASEPRI, ③ 2 ms 크리티컬 섹션 동안 kernel-aware ISR(0x40, 100 µs 주기)과 zero-latency ISR(0x00)이 각각 몇 번 돌았나, ④ ISR 안에서 `taskENTER_CRITICAL_FROM_ISR()`이 돌려준 값, ⑤ `taskDISABLE_INTERRUPTS()` 뒤의 enter/exit 짝.

```c
/* ex4 — FreeRTOS ARM_CM3 critical sections: BASEPRI, nesting count, FromISR mask */
#include "FreeRTOS.h"
#include "task.h"
#include "c7.h"
static volatile uint32_t n_aware, n_zero, isr_saved1, isr_saved2, isr_bp;
void TIMER0_IRQHandler(void)                       /* prio 0x40 (kernel-aware) */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    UBaseType_t s1 = taskENTER_CRITICAL_FROM_ISR();
    UBaseType_t s2 = taskENTER_CRITICAL_FROM_ISR(); /* nesting in ISR = keep the old value */
    isr_bp = get_basepri(); n_aware++;
    taskEXIT_CRITICAL_FROM_ISR(s2);
    taskEXIT_CRITICAL_FROM_ISR(s1);
    isr_saved1 = s1; isr_saved2 = s2;
}
void DUALTIMER_IRQHandler(void) { DT_INTCLR = 1; n_zero++; }   /* prio 0x00 */
#define SHOW(tag) board_printf_raw("%s BASEPRI=0x%x PRIMASK=%u\n", tag, get_basepri(), get_primask())

static void task(void *arg)
{
    (void)arg;
    SHOW("task start         ");
    taskENTER_CRITICAL();  SHOW("enter #1           ");
    taskENTER_CRITICAL();  SHOW("enter #2 (nested)  ");
    taskEXIT_CRITICAL();   SHOW("exit  #2 (still in)");
    uint32_t a0 = n_aware, z0 = n_zero;
    spin_cyc(25 * 2000);                            /* 2 ms inside the critical section */
    uint32_t a1 = n_aware, z1 = n_zero;
    taskEXIT_CRITICAL();   SHOW("exit  #1           ");
    board_printf_raw("during 2 ms CS: aware ISR ran %u times, zero-latency ISR ran %u times\n",
                     a1 - a0, z1 - z0);
    board_printf_raw("right after exit: aware ISR count +%u\n", n_aware - a1);
    board_printf_raw("ISR: FROM_ISR saved=0x%x, nested saved=0x%x, BASEPRI inside=0x%x\n",
                     isr_saved1, isr_saved2, isr_bp);
    taskDISABLE_INTERRUPTS(); SHOW("taskDISABLE_INTERRUPTS");
    taskENTER_CRITICAL(); taskEXIT_CRITICAL();
    SHOW("...+enter/exit pair  ");                  /* the DISABLE was silently undone */
    board_exit(0);
}
int main(void)
{
    SHOW("main, before create");
    xTaskCreate(task, "t", 512, NULL, 1, NULL);
    SHOW("main, after create ");                    /* kernel left BASEPRI raised */
    timer0_start(25000 / 10, 0x40);                 /* 100 us */
    dt_start(25000 / 10 + 7, 0x00);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex4_basepri.c OUT=/var/tmp/rtos/C7/ex4 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex4/app.elf 60'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12508	    100	 135424	 148032	  24240	/var/tmp/rtos/C7/ex4/app.elf
main, before create BASEPRI=0x0 PRIMASK=0
main, after create  BASEPRI=0x20 PRIMASK=0
task start          BASEPRI=0x0 PRIMASK=0
enter #1            BASEPRI=0x20 PRIMASK=0
enter #2 (nested)   BASEPRI=0x20 PRIMASK=0
exit  #2 (still in) BASEPRI=0x20 PRIMASK=0
exit  #1            BASEPRI=0x0 PRIMASK=0
during 2 ms CS: aware ISR ran 0 times, zero-latency ISR ran 20 times
right after exit: aware ISR count +1
ISR: FROM_ISR saved=0x0, nested saved=0x20, BASEPRI inside=0x20
taskDISABLE_INTERRUPTS BASEPRI=0x20 PRIMASK=0
...+enter/exit pair   BASEPRI=0x0 PRIMASK=0
[board_exit] code=0
```

출력에서 볼 것:

- `main, after create BASEPRI=0x20` — `xTaskCreate` 하나로 문턱이 올라가 그대로 남았다(0xaaaaaaaa 효과). 첫 태스크에서는 0이다.
- 중첩: enter #2와 exit #2 사이 내내 0x20, **exit #1에서야** 0. PRIMASK는 한 번도 안 건드렸다.
- 2 ms 동안 **kernel-aware ISR은 0번, zero-latency ISR은 20번**. 문턱이 정확히 반으로 나눴다.
- 탈출 직후 kernel-aware ISR은 **+1**만 돌았다. 2 ms 동안 타이머는 20번 울렸지만 pending 비트가 하나라 **19번은 사라졌다**. "크리티컬 섹션이 길면 인터럽트를 놓친다"의 정확한 의미다 — 지연만이 아니라 **유실**이다.
- ISR의 `FROM_ISR`은 이전 값 0을 돌려주고, 중첩된 두 번째는 0x20을 돌려준다. 안쪽 EXIT는 0x20으로, 바깥 EXIT는 0으로 되돌린다 — **저장/복원 방식**이라 ISR 안의 중첩도 안전하다.
- 마지막 두 줄: `taskDISABLE_INTERRUPTS()`로 올린 문턱을 enter/exit 짝이 **말없이 내렸다**.

### 3.5 예제 5 — 잘못 쓰면 잡아 주는 두 개의 assert

무엇을 확인하나: ① kernel-aware ISR(0x40)에서 태스크용 `taskENTER_CRITICAL()`을 부르면 `vPortEnterCritical`의 VECTACTIVE assert가 터지는가, ② 문턱 위(0x00)의 ISR에서 `xQueueSendFromISR`를 부르면 `vPortValidateInterruptPriority`의 assert가 터지는가. lab 템플릿의 `configASSERT`는 파일:줄을 출력하고 종료코드 253으로 끝난다.

```c
/* ex5 — the asserts that catch wrong critical-section use from an ISR */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "c7.h"
static QueueHandle_t q;
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
#if VARIANT == 1          /* task-level API inside an ISR (prio 0x40) */
    taskENTER_CRITICAL();
    taskEXIT_CRITICAL();
#else                     /* FromISR API, but ISR priority 0x00 is above MAX_SYSCALL */
    uint32_t v = 1;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(q, &v, &woken);
    portYIELD_FROM_ISR(woken);
#endif
}
static void task(void *arg)
{
    (void)arg;
    board_printf_raw("task running, starting TIMER0 at prio 0x%x\n", VARIANT == 1 ? 0x40 : 0x00);
    timer0_start(25000, VARIANT == 1 ? 0x40 : 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    board_printf_raw("no assert?!\n");
    board_exit(1);
}
int main(void)
{
    q = xQueueCreate(4, sizeof(uint32_t));
    xTaskCreate(task, "t", 512, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt; for v in 1 2; do make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex5_assert.c OUT=/var/tmp/rtos/C7/ex5_$v EXTRA_CFLAGS=-DVARIANT=$v >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex5_$v/app.elf 20; echo "exit=$?"; done'
```

```text
task running, starting TIMER0 at prio 0x40

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253
exit=1
task running, starting TIMER0 at prio 0x0

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:803
[board_exit] code=253
exit=1
```

출력에서 볼 것: port.c:438은 `vPortEnterCritical`의 `configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 )`, port.c:803은 `vPortValidateInterruptPriority`의 `configASSERT( ucCurrentPriority >= ucMaxSysCallPriority )`다. 두 번째 assert는 "우선순위를 안 정한 인터럽트는 기본값 0 = 가장 높은 우선순위라서 무조건 걸린다"는 FreeRTOS 소스 주석과 같은 이야기다. **개발 빌드에서는 `configASSERT`를 반드시 켠다** — 꺼져 있으면 이 두 실수는 assert 대신 리스트 손상(§7 예제 11의 멈춤)으로 나타난다.

**왜 ISR에서 `taskENTER_CRITICAL`이 문제인가?** ISR이 태스크의 크리티컬 섹션을 선점한 게 아니라는 건 확실하다(문턱이 막았을 테니). 문제는 **나올 때**다. 카운터가 1 → 0이 되면 `vPortExitCritical`이 BASEPRI를 **0으로** 내린다. 그런데 이 ISR보다 낮은 우선순위 ISR이 BASEPRI를 올린 상태에서 이 ISR이 들어왔을 수도 있다(ISR 진입은 BASEPRI를 바꾸지 않는다). 0으로 내리면 그 낮은 ISR의 보호가 풀린다. 그래서 ISR은 저장/복원 방식(`FROM_ISR`)만 써야 한다.

### 3.6 예제 6 — 매크로가 실제로 무엇으로 컴파일되나: ARM_CM3 vs ARM_CM0

무엇을 확인하나: 같은 C 코드(`taskENTER_CRITICAL` 짝, `FROM_ISR` 짝)를 ARM_CM3 포트와 ARM_CM0 포트로 컴파일해서 기계어를 비교한다. 그리고 실제 `vPortEnterCritical`/`vPortExitCritical`(ARM_CM3) 본체를 본다. 빌드만 하는 분석 예제다.

```c
/* ex6 — what the critical-section macros compile to: ARM_CM3 port vs ARM_CM0 port */
#include "FreeRTOS.h"
#include "task.h"
volatile uint32_t g;
void cs_task(void)                      /* task level: function call, nesting counter */
{
    taskENTER_CRITICAL();
    g++;
    taskEXIT_CRITICAL();
}
void cs_isr(void)                       /* ISR level: inline, returns old mask */
{
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    g++;
    taskEXIT_CRITICAL_FROM_ISR(s);
}
```

```sh
limactl shell elinux -- bash -lc 'K=/var/rtoslab/src/FreeRTOS-Kernel; T=/var/rtoslab/templates; O=/var/tmp/rtos/C7/ex10; S=/Users/donh/.rtos_scratch/C7/port; arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -Wall -Wextra -c $S/cs_shapes.c -I$T/freertos-mps2 -I$T/common -I$K/include -I$K/portable/GCC/ARM_CM3 -o $O/cs_CM3.o && arm-none-eabi-gcc -mthumb -mcpu=cortex-m0 -O2 -Wall -Wextra -DconfigENABLE_MPU=0 -c $S/cs_shapes.c -I$T/freertos-mps2 -I$T/common -I$K/include -I$K/portable/GCC/ARM_CM0 -o $O/cs_CM0.o && for p in CM3 CM0; do echo "== ARM_$p"; arm-none-eabi-objdump -d --no-show-raw-insn $O/cs_$p.o | grep -v "^$\|file format\|Disassembly"; done'
```

```text
== ARM_CM3
00000000 <cs_task>:
   0:	push	{r3, lr}
   2:	bl	0 <vPortEnterCritical>
   6:	ldr	r2, [pc, #16]	@ (18 <cs_task+0x18>)
   8:	ldr	r3, [r2, #0]
   a:	adds	r3, #1
   c:	str	r3, [r2, #0]
   e:	ldmia.w	sp!, {r3, lr}
  12:	b.w	0 <vPortExitCritical>
  16:	nop
  18:	.word	0x00000000
0000001c <cs_isr>:
  1c:	mrs	r1, BASEPRI
  20:	mov.w	r3, #32
  24:	msr	BASEPRI, r3
  28:	isb	sy
  2c:	dsb	sy
  30:	ldr	r2, [pc, #12]	@ (40 <cs_isr+0x24>)
  32:	ldr	r3, [r2, #0]
  34:	adds	r3, #1
  36:	str	r3, [r2, #0]
  38:	msr	BASEPRI, r1
  3c:	bx	lr
  3e:	nop
  40:	.word	0x00000000
== ARM_CM0
00000000 <cs_task>:
   0:	push	{r4, lr}
   2:	bl	0 <vPortEnterCritical>
   6:	ldr	r2, [pc, #12]	@ (14 <cs_task+0x14>)
   8:	ldr	r3, [r2, #0]
   a:	adds	r3, #1
   c:	str	r3, [r2, #0]
   e:	bl	0 <vPortExitCritical>
  12:	pop	{r4, pc}
  14:	.word	0x00000000
00000018 <cs_isr>:
  18:	push	{r4, lr}
  1a:	bl	0 <ulSetInterruptMask>
  1e:	ldr	r2, [pc, #12]	@ (2c <cs_isr+0x14>)
  20:	ldr	r3, [r2, #0]
  22:	adds	r3, #1
  24:	str	r3, [r2, #0]
  26:	bl	0 <vClearInterruptMask>
  2a:	pop	{r4, pc}
  2c:	.word	0x00000000
```

ARM_CM3 포트의 함수 본체(예제 4의 `app.elf`에서):

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/C7/ex4/app.elf | sed -n "/<vPortEnterCritical>:/,/^$/p;/<vPortExitCritical>:/,/^$/p"'
```

```text
00001f90 <vPortEnterCritical>:
    1f90:	push	{r3, lr}
    1f92:	mov.w	r3, #32
    1f96:	msr	BASEPRI, r3
    1f9a:	isb	sy
    1f9e:	dsb	sy
    1fa2:	ldr	r2, [pc, #44]	@ (1fd0 <vPortEnterCritical+0x40>)
    1fa4:	ldr	r3, [r2, #0]
    1fa6:	adds	r3, #1
    1fa8:	cmp	r3, #1
    1faa:	str	r3, [r2, #0]
    1fac:	beq.n	1fb0 <vPortEnterCritical+0x20>
    1fae:	pop	{r3, pc}
    1fb0:	mov.w	r3, #3758153728	@ 0xe000e000
    1fb4:	ldr.w	r3, [r3, #3332]	@ 0xd04
    1fb8:	uxtb	r3, r3
    1fba:	cmp	r3, #0
    1fbc:	beq.n	1fae <vPortEnterCritical+0x1e>
    1fbe:	mov.w	r2, #438	@ 0x1b6
    ...
00001fdc <vPortExitCritical>:
    1fdc:	ldr	r2, [pc, #36]	@ (2004 <vPortExitCritical+0x28>)
    1fde:	push	{r3, lr}
    1fe0:	ldr	r3, [r2, #0]
    1fe2:	cbz	r3, 1ff0 <vPortExitCritical+0x14>
    1fe4:	subs	r3, #1
    1fe6:	str	r3, [r2, #0]
    1fe8:	cbnz	r3, 1fee <vPortExitCritical+0x12>
    1fea:	msr	BASEPRI, r3
    1fee:	pop	{r3, pc}
    ...
```

(assert 출력 부분은 `...`로 줄였다.) 출력에서 볼 것:

- **ARM_CM3**: 태스크용은 함수 호출(`bl vPortEnterCritical`) — 카운터를 올리고 VECTACTIVE(0xE000ED04의 아래 바이트, `uxtb`)를 검사한다. 탈출은 `cbz`(카운터가 이미 0이면 assert) → `subs` → `cbnz`(아직 0이 아니면 그냥 돌아감) → `msr BASEPRI, r3`(r3 = 0)이다. 진입 · 탈출 합쳐 명령 20개 남짓이니 icount에서 1 µs 안쪽이다. ISR용은 **완전히 인라인**: `mrs BASEPRI → msr BASEPRI #32 → isb → dsb → … → msr BASEPRI, r1`. 함수 호출조차 없다 — ISR 경로를 짧게 하려는 설계다.
- **ARM_CM0**: BASEPRI가 없으므로 ISR용도 `ulSetInterruptMask()` 호출이다. 이 함수의 본체는 `portable/GCC/ARM_CM0/portasm.c`에 있다.

```c
uint32_t ulSetInterruptMask( void ) /* __attribute__(( naked )) PRIVILEGED_FUNCTION */
{
    __asm volatile
    (
        " .syntax unified                                 \n"
        "                                                 \n"
        " mrs r0, PRIMASK                                 \n"
        " cpsid i                                         \n"
        " bx lr                                           \n"
        ::: "memory"
    );
}
```

그리고 `vClearInterruptMask`는 `msr PRIMASK, r0`, 태스크용 `portDISABLE_INTERRUPTS()`는 `cpsid i`, `portENABLE_INTERRUPTS()`는 `cpsie i`다(`ARM_CM0/portmacro.h`). 즉 **M0 포트의 크리티컬 섹션은 2절의 PRIMASK 방식 그대로**이고, zero-latency ISR이라는 개념이 없다 — 커널이 크리티컬 섹션에 들어가면 모든 인터럽트가 기다린다. M0 칩에 20 kHz 모터 루프와 RTOS를 같이 올리기 어려운 이유다.

| 항목 | ARM_CM3 / CM4F / CM7 / CM33 | ARM_CM0 (M0 / M0+ / M23) |
|---|---|---|
| 태스크 크리티컬 섹션 | BASEPRI = `configMAX_SYSCALL_INTERRUPT_PRIORITY` + 카운터 | PRIMASK(`cpsid i`) + 카운터 |
| ISR 크리티컬 섹션 | 인라인 `mrs/msr BASEPRI`, 이전 값 반환 | `ulSetInterruptMask()` = `mrs PRIMASK` + `cpsid i` |
| 막히지 않는 ISR | 문턱보다 높은 우선순위 (zero-latency) | 없음 (NMI만) |
| `vPortValidateInterruptPriority` | 있음 (문턱 위 ISR의 API 호출 검출) | 없음 (검사할 문턱이 없다) |

---

## 4. 해법 3 — 스케줄러 잠금: 다른 태스크만 막는다 (`vTaskSuspendAll`)

### 4.1 직관과 정의

**스케줄러 잠금(scheduler lock)**은 "인터럽트는 그대로 받되, **컨텍스트 스위치만** 미뤄 달라"는 요청이다. FreeRTOS는 `vTaskSuspendAll()` / `xTaskResumeAll()`, Zephyr는 `k_sched_lock()` / `k_sched_unlock()`(§8). 태스크끼리만 공유하는 데이터를 **조금 길게**(수십 µs ~ ms) 만져야 하는데 그동안 인터럽트를 막고 싶지 않을 때 쓴다.

bare-metal 비유: 슈퍼 루프에서 "이번 바퀴는 다른 job으로 넘어가지 말고 이 job을 끝까지 하자"는 것과 같다. ISR은 여전히 돈다.

### 4.2 소스로 읽기 — 카운터 하나와 두 개의 "나중에" 목록

`tasks.c`(V11.3.2) line 3890:

```c
void vTaskSuspendAll( void )
{
    traceENTER_vTaskSuspendAll();
    #if ( configNUMBER_OF_CORES == 1 )
    {
        /* A critical section is not required as the variable is of type
         * BaseType_t. Each task maintains its own context, and a context switch
         * cannot occur if the variable is non zero. So, as long as the writing
         * from the register back into the memory is atomic, it is not a
         * problem.
         ...
        portSOFTWARE_BARRIER();
        /* The scheduler is suspended if uxSchedulerSuspended is non-zero.  An increment
         * is used to allow calls to vTaskSuspendAll() to nest. */
        uxSchedulerSuspended = ( UBaseType_t ) ( uxSchedulerSuspended + 1U );
        /* Enforces ordering for ports and optimised compilers that may otherwise place
         * the above increment elsewhere. */
        portMEMORY_BARRIER();
    }
```

(긴 주석은 일부 생략.) 단일 코어에서 **들어갈 때는 크리티컬 섹션조차 없다**. 카운터를 올리는 RMW가 예제 1처럼 깨지지 않느냐? 주석이 설명한다 — 이 변수를 바꾸는 다른 주체는 다른 **태스크**뿐인데, 카운터가 0인 동안 끼어든 태스크는 돌아가기 전에 반드시 카운터를 0으로 되돌려 놓는다. ISR은 이 변수를 **읽기만** 한다. 그래서 RMW 창이 있어도 결과가 맞다. (레이스가 "있느냐"가 아니라 "누가 쓰느냐"로 판단하는 좋은 예다.)

잠긴 동안 ISR이 하는 일은 두 곳에서 "나중에" 목록으로 바뀐다.

- **tick**: `xTaskIncrementTick()`(line 5064 부근)은 잠겨 있으면 `xTickCount`를 올리지 않고 `xPendedTicks += 1U`만 한다. 그래서 잠긴 동안 `xTaskGetTickCount()`가 **멈춘다**.
- **깨우기**: `xTaskRemoveFromEventList()`(line 5500, `...FromISR`이 태스크를 깨울 때 부름)는 잠겨 있으면 ready list 대신 `xPendingReadyList`에 넣는다(line 5550: "The delayed and ready lists cannot be accessed, so hold this task pending until the scheduler is resumed.").

`xTaskResumeAll()`(line 4064)은 BASEPRI 크리티컬 섹션 안에서 카운터를 내리고, 0이 되면 ① `xPendingReadyList`를 비워 ready list로 옮기고, ② `xPendedTicks`만큼 `xTaskIncrementTick()`을 **몰아서** 부르고, ③ 더 높은 태스크가 깨어났으면 yield한다.

```c
            uxSchedulerSuspended = ( UBaseType_t ) ( uxSchedulerSuspended - 1U );
            ...
            if( uxSchedulerSuspended == ( UBaseType_t ) 0U )
            {
                ...
                    while( listLIST_IS_EMPTY( &xPendingReadyList ) == pdFALSE )
                    {
                        pxTCB = listGET_OWNER_OF_HEAD_ENTRY( ( &xPendingReadyList ) );
                        listREMOVE_ITEM( &( pxTCB->xEventListItem ) );
                        portMEMORY_BARRIER();
                        listREMOVE_ITEM( &( pxTCB->xStateListItem ) );
                        prvAddTaskToReadyList( pxTCB );
                        ...
                    {
                        TickType_t xPendedCounts = xPendedTicks; /* Non-volatile copy. */
                        if( xPendedCounts > ( TickType_t ) 0U )
                        {
                            do
                            {
                                if( xTaskIncrementTick() != pdFALSE )
                                {
                                    xYieldPendings[ xCoreID ] = pdTRUE;
                                }
                                ...
                                --xPendedCounts;
                            } while( xPendedCounts > ( TickType_t ) 0U );
                            xPendedTicks = 0;
```

주의: 몰아서 처리하는 이 루프는 **BASEPRI 크리티컬 섹션 안**이다. 오래 잠갔다면(xPendedTicks가 크면) 풀 때 그만큼 긴 크리티컬 섹션이 생긴다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">스케줄러 잠금 5 ms — ISR은 돌지만, tick과 깨우기는 "나중에" 목록에 쌓인다</text>
<text x="10" y="58" font-size="12">lo 태스크</text>
<rect x="110" y="42" width="430" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="325" y="60" font-size="12" text-anchor="middle">vTaskSuspendAll() … 5 ms 작업 … </text>
<rect x="545" y="42" width="125" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="607" y="60" font-size="12" text-anchor="middle">xTaskResumeAll</text>
<text x="10" y="108" font-size="12">ISR (1 kHz)</text>
<rect x="150" y="92" width="16" height="26" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="236" y="92" width="16" height="26" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="322" y="92" width="16" height="26" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="408" y="92" width="16" height="26" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="494" y="92" width="16" height="26" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="560" y="110" font-size="12">5번 모두 바로 실행</text>
<text x="10" y="158" font-size="12">SysTick</text>
<text x="110" y="158" font-size="12">xTickCount 그대로, xPendedTicks = 1, 2, 3, 4, 5</text>
<text x="10" y="198" font-size="12">hi 태스크</text>
<text x="110" y="198" font-size="12">첫 ISR이 깨움 → xPendingReadyList에서 대기</text>
<rect x="580" y="182" width="90" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="625" y="200" font-size="12" text-anchor="middle">이제 실행</text>
<line x1="158" y1="120" x2="158" y2="185" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="600" y1="70" x2="610" y2="180" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="10" y="240" font-size="12">측정(예제 7): 잠금 없이 ISR → hi 태스크 7 µs, 잠금 중 4098 µs. ISR 진입 지연은 그대로.</text>
<text x="10" y="262" font-size="12">풀 때: 밀린 tick 5개를 몰아서 처리 → hi를 ready로 → yield (전부 BASEPRI 안에서)</text>
</svg>
```

그림 5 — 스케줄러 잠금 동안의 타임라인(예제 7의 실제 상황).

### 4.3 예제 7 — ISR은 돌고, tick은 멈췄다가 따라잡고, 깨우기는 기다린다

무엇을 확인하나: 1 kHz kernel-aware ISR이 `vTaskNotifyGiveFromISR`로 높은 우선순위 태스크(hi, 3)를 깨운다. 낮은 태스크(lo, 1)가 ① 잠금 없이 한 번, ② `vTaskSuspendAll()` 안에서 5 ms를 보내는 동안 한 번 깨움을 요청한다. 잠긴 동안 ISR 횟수, tick 증가, hi 실행 여부, 푼 뒤의 tick과 `xTaskResumeAll()` 반환값, 그리고 두 경우의 ISR → hi 지연을 잰다.

```c
/* ex7 — scheduler lock (vTaskSuspendAll): ISRs run, ticks pend, wakeups wait */
#include "FreeRTOS.h"
#include "task.h"
#include "c7.h"
static TaskHandle_t hi;
static volatile uint32_t n_isr, arm, t_give, t_hi_run;
void TIMER0_IRQHandler(void)                         /* 1 kHz, prio 0x40 */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    n_isr++;
    if (arm) {                                        /* first IRQ after arming wakes hi */
        arm = 0; t_give = CYC();
        BaseType_t w = pdFALSE;
        vTaskNotifyGiveFromISR(hi, &w);
        portYIELD_FROM_ISR(w);
    }
}
static void hi_task(void *a)                          /* prio 3 */
{
    (void)a;
    for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); t_hi_run = CYC(); }
}
static void lo_task(void *a)                          /* prio 1 */
{
    (void)a;
    vTaskDelay(2);
    arm = 1; vTaskDelay(3);                           /* reference: wake with scheduler ON */
    board_printf_raw("scheduler running : ISR->hi task = %u us\n", (t_hi_run - t_give) / 25);
    TickType_t k0 = xTaskGetTickCount(); uint32_t i0 = n_isr, c0 = CYC();
    vTaskSuspendAll();
    arm = 1;
    spin_cyc(25 * 5000);                              /* 5 ms with the scheduler locked */
    TickType_t k1 = xTaskGetTickCount(); uint32_t i1 = n_isr, hi_ran = (t_hi_run > c0);
    BaseType_t yielded = xTaskResumeAll();
    TickType_t k2 = xTaskGetTickCount();
    board_printf_raw("inside 5 ms lock  : ISRs ran=%u, tick advanced=%u, hi ran=%u\n",
                     i1 - i0, (unsigned)(k1 - k0), hi_ran);
    board_printf_raw("after ResumeAll   : tick advanced=%u (caught up), returned yielded=%d\n",
                     (unsigned)(k2 - k0), (int)yielded);
    board_printf_raw("scheduler locked  : ISR->hi task = %u us\n", (t_hi_run - t_give) / 25);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(hi_task, "hi", 256, NULL, 3, &hi);
    xTaskCreate(lo_task, "lo", 512, NULL, 1, NULL);
    timer0_start(25000 + 13, 0x40);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex6_suspendall.c OUT=/var/tmp/rtos/C7/ex6 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex6/app.elf 20'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12868	    100	 135424	 148392	  243a8	/var/tmp/rtos/C7/ex6/app.elf
scheduler running : ISR->hi task = 7 us
inside 5 ms lock  : ISRs ran=5, tick advanced=0, hi ran=0
after ResumeAll   : tick advanced=5 (caught up), returned yielded=1
scheduler locked  : ISR->hi task = 4098 us
[board_exit] code=0
```

출력에서 볼 것:

- 잠긴 5 ms 동안 **ISR은 5번 다 돌았다** — 인터럽트 지연은 늘지 않는다.
- 그런데 **tick은 0** 늘었다. `xTaskGetTickCount()`로 시간을 재는 코드는 잠긴 동안 시간이 멈춘 것처럼 본다. 풀자마자 5가 됐다(xPendedTicks 몰아 처리).
- hi는 첫 ISR(잠근 지 약 0.9 ms 뒤)에 깨워졌지만 **풀 때까지 4098 µs** 기다렸다. 잠금 없을 때 7 µs의 585배다. `xTaskResumeAll()`이 1(pdTRUE)을 돌려준 것은 "안에서 yield했다"는 뜻이다.

결론: **스케줄러 잠금은 ISR 지연을 지키지만, 높은 우선순위 태스크의 지연은 PRIMASK/BASEPRI와 똑같이 L만큼 늘린다.** 드론 비행 제어기라면 rate loop 태스크가 IMU ISR의 통지를 받고 깨어나야 하는데, 로거 태스크가 5 ms 스케줄러 잠금을 걸면 rate loop 하나가 통째로 늦는다. §6의 측정 차트가 이걸 L 전체 범위에서 보여 준다.

### 4.4 스케줄러 잠금 안에서 하면 안 되는 일

- **블로킹 API 금지**. `vTaskDelay`, 타임아웃 있는 `xQueueSend/Receive`, `xSemaphoreTake` — 잠긴 채로는 다른 태스크로 넘어갈 수 없으니 블록할 방법이 없다. FreeRTOS는 assert로 잡는다(예제 11: `tasks.c:2517`, `queue.c:965`).
- **ISR과 공유한 데이터 보호에 쓰지 말 것**. ISR은 그대로 끼어든다. 태스크 ↔ ISR 공유는 BASEPRI 크리티컬 섹션이다.
- **오래 잡지 말 것**. 위에서 본 대로 태스크 지연이 늘고, 풀 때 밀린 tick 처리가 BASEPRI 안에서 일어난다.

---

## 5. 해법 4 — 뮤텍스, 그리고 "언제 무엇을 쓰나"

### 5.1 뮤텍스는 한 줄만

**뮤텍스(mutex)**는 "이 자원을 원하는 태스크만" 기다리게 한다. 소유권이 있고, 기다리는 동안 잠들고(CPU를 양보), FreeRTOS 뮤텍스는 우선순위 상속으로 역전을 줄인다. 인터럽트도 다른 무관한 태스크도 막지 않는다. 대신 **ISR에서는 쓸 수 없고**(잠들 수 없으니까), 잡고 놓는 비용이 수 µs다(큐 연산 + 경우에 따라 컨텍스트 스위치). 자세한 구현 · 재귀 뮤텍스 · 타임아웃 · 역전은 **D2**, 우선순위 역전 이론은 **D3**가 다룬다.

### 5.2 결정표 — 무엇을 막아야 하나에서 출발한다

| 질문 | 답 | 쓸 도구 | FreeRTOS / Zephyr |
|---|---|---|---|
| 공유 상대에 ISR이 있나? | 예, 수 µs 이내 작업 | BASEPRI 크리티컬 섹션 | `taskENTER_CRITICAL` / `k_spin_lock` |
| 공유 상대가 zero-latency ISR인가? | 예 | 크리티컬 섹션으로는 못 지킨다 → 락 없는 SPSC(D8) 또는 짧은 PRIMASK | `__disable_irq` / D8 링버퍼 |
| ISR 안에서 다른 ISR과 공유? | 예 | ISR용 저장/복원 | `taskENTER_CRITICAL_FROM_ISR` / `k_spin_lock` |
| 태스크끼리만, 짧다(수 µs) | 예 | BASEPRI 크리티컬 섹션도 OK | `taskENTER_CRITICAL` |
| 태스크끼리만, 길다(수십 µs~ms), 블로킹 없음 | 예 | 스케줄러 잠금 (태스크 지연 L 감수) | `vTaskSuspendAll` / `k_sched_lock` |
| 태스크끼리만, 길거나 블로킹(I/O 대기) 포함 | 예 | 뮤텍스 (D2) | `xSemaphoreTake(mutex)` / `k_mutex_lock` |
| 32비트 정렬 변수 하나를 읽기/쓰기만 | 예 | 보호 필요 없음 (`volatile` 또는 atomic) | — |
| M0 / M0+ 코어 | 예 | PRIMASK뿐 (BASEPRI 없음) | ARM_CM0 포트 / Zephyr `irq_lock`(PRIMASK) |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="230" y="10" width="220" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="32" font-size="13" text-anchor="middle">ISR도 이 데이터를 만지나?</text>
<line x1="290" y1="44" x2="150" y2="78" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="190" y="62" font-size="12">예</text>
<line x1="390" y1="44" x2="520" y2="78" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="470" y="62" font-size="12">아니오</text>
<rect x="20" y="80" width="260" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="150" y="102" font-size="13" text-anchor="middle">그 ISR이 문턱 위(zero-latency)?</text>
<rect x="390" y="80" width="270" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="525" y="102" font-size="13" text-anchor="middle">블로킹(I/O 대기)이 들어가나?</text>
<line x1="90" y1="114" x2="70" y2="160" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="50" y="140" font-size="12">예</text>
<line x1="210" y1="114" x2="230" y2="160" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="230" y="140" font-size="12">아니오</text>
<rect x="10" y="162" width="150" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="184" font-size="12" text-anchor="middle">락 없는 SPSC (D8)</text>
<text x="85" y="202" font-size="12" text-anchor="middle">또는 짧은 PRIMASK</text>
<rect x="170" y="162" width="160" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="250" y="184" font-size="12" text-anchor="middle">BASEPRI 크리티컬</text>
<text x="250" y="202" font-size="12" text-anchor="middle">섹션 (수 µs 이내)</text>
<line x1="460" y1="114" x2="420" y2="160" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="420" y="140" font-size="12">예</text>
<line x1="590" y1="114" x2="600" y2="160" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="605" y="140" font-size="12">아니오</text>
<rect x="350" y="162" width="130" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="415" y="184" font-size="12" text-anchor="middle">뮤텍스 (D2)</text>
<text x="415" y="202" font-size="12" text-anchor="middle">경쟁자만 기다림</text>
<rect x="500" y="162" width="170" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585" y="184" font-size="12" text-anchor="middle">길이가 수 µs 이내?</text>
<line x1="545" y1="196" x2="520" y2="240" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="510" y="222" font-size="12">예</text>
<line x1="625" y1="196" x2="625" y2="240" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="632" y="222" font-size="12">아니오</text>
<rect x="420" y="242" width="150" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="495" y="264" font-size="12" text-anchor="middle">BASEPRI 크리티컬</text>
<text x="495" y="282" font-size="12" text-anchor="middle">섹션도 충분</text>
<rect x="580" y="242" width="95" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="627" y="264" font-size="12" text-anchor="middle">스케줄러</text>
<text x="627" y="282" font-size="12" text-anchor="middle">잠금</text>
<text x="10" y="320" font-size="12">어느 길이든 마지막 질문: "이 구간의 최악 길이를 쟀나?" — 잰 값이 가장 짧은 마감의 몇 %인지 본다 (§6)</text>
</svg>
```

그림 6 — 보호 도구를 고르는 흐름. 출발점은 "무엇을 막아야 하나"다. 무조건 가장 센 도구(PRIMASK)를 쓰는 것이 아니라, 필요한 만큼만 막는다.

**Don 경험과 연결**: SSD 펌웨어에서는 "ISR과 공유 → 인터럽트 끄기", "코어 간 공유 → 하드웨어 세마포어"로 두 칸뿐이었을 것이다. RTOS는 그 사이에 "태스크끼리"라는 칸을 하나 더 만들고, 거기에 스케줄러 잠금과 뮤텍스라는 더 싼 도구를 준다. 거꾸로, 인터럽트를 끄는 것은 **태스크끼리만** 공유하는 데이터에는 과한 도구다.

---

## 6. 측정 — 크리티컬 섹션 길이 L은 지연에 얼마를 더하나

A7 예제 9는 "BASEPRI 50 µs 아래서 kernel-aware IRQ는 50.84 µs, zero-latency IRQ는 160 ns"라는 **한 점**을 쟀다. 여기서는 그 점을 **곡선**으로 넓힌다 — L을 0 ~ 100 µs로 바꾸고, 세 가지 잠금(PRIMASK, BASEPRI, 스케줄러 잠금)과 세 가지 피해자(kernel-aware IRQ, zero-latency IRQ, IRQ가 깨우는 높은 태스크)를 모두 잰다.

### 6.1 예제 8 — L 스윕: 3가지 잠금 × 7가지 길이 × 3가지 지연

무엇을 확인하나: 우선순위 1 태스크(lo)가 "잠금 → L µs → 풀기 → 10~40 µs(의사 난수) 쉬기"를 반복한다. 그동안 TIMER0(0x40, kernel-aware, 약 500 µs 주기)의 진입 지연, DUALTIMER(0x00, zero-latency, 주기 살짝 다름)의 진입 지연, 그리고 TIMER0 ISR이 통지로 깨우는 우선순위 3 태스크(hi)의 IRQ → 태스크 지연을 각각 300개씩 모아 최댓값을 낸다. 쉬는 시간을 난수로 섞은 이유: icount는 결정적이라 주기가 맞물리면 IRQ가 늘 같은 위상(예: 잠금 밖)에만 떨어진다. 첫 시도에서 고정 20 µs로 했더니 PRIMASK L = 50 µs인데 최대 지연이 200 ns로 나왔다 — 위상 고정(aliasing)이다. **최악값 측정에서는 위상을 섞는 것이 측정 설계의 일부**다.

```c
/* ex8 — IRQ / task latency vs critical-section length L: PRIMASK, BASEPRI, sched lock */
#include "FreeRTOS.h"
#include "task.h"
#include "c7.h"
#define NS 300                                       /* samples per IRQ per config */
typedef struct { uint32_t n, max; uint64_t sum; } st_t;
static st_t s_aw, s_zl, s_tk;
static TaskHandle_t hi;
static volatile int running;
static uint32_t seed = 1;
static void add(st_t *s, uint32_t v) { if (s->n < NS) { s->n++; s->sum += v; if (v > s->max) s->max = v; } }
void TIMER0_IRQHandler(void)                         /* prio 0x40: kernel-aware */
{
    uint32_t lat = timer0_elapsed();
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (!running) return;
    add(&s_aw, lat);
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR(hi, &w);
    portYIELD_FROM_ISR(w);
}
void DUALTIMER_IRQHandler(void)                      /* prio 0x00: zero-latency */
{
    uint32_t lat = dt_elapsed();
    DT_INTCLR = 1;
    if (running) add(&s_zl, lat);
}
static void hi_task(void *a)                         /* prio 3: woken by TIMER0 ISR */
{
    (void)a;
    for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); uint32_t l = timer0_elapsed(); if (running) add(&s_tk, l); }
}
static void lo_task(void *a)                         /* prio 1: the code holding the lock */
{
    (void)a;
    static const uint16_t Ls[] = { 0, 2, 5, 10, 20, 50, 100 };
    static const char *mn[] = { "PRIMASK", "BASEPRI", "SCHED  " };
    board_printf_raw("mode    L_us aware_avg aware_max zero_max task_max (ns)\n");
    for (int m = 0; m < 3; m++)
        for (unsigned j = 0; j < sizeof Ls / sizeof Ls[0]; j++) {
            uint32_t L = Ls[j] * 25u;
            s_aw = s_zl = s_tk = (st_t){0};
            running = 1;
            while (s_aw.n < NS || s_zl.n < NS || s_tk.n < NS) {
                if (m == 0)      { uint32_t k = irq_save(); spin_cyc(L); irq_restore(k); }
                else if (m == 1) { taskENTER_CRITICAL(); spin_cyc(L); taskEXIT_CRITICAL(); }
                else             { vTaskSuspendAll(); spin_cyc(L); (void)xTaskResumeAll(); }
                seed = seed * 1103515245u + 12345u;  /* LCG: 10..40 us of unlocked work */
                spin_cyc((10 + (seed >> 16) % 31) * 25);
            }
            running = 0;
            board_printf_raw("%s %u %u %u %u %u\n", mn[m], Ls[j], CYC_NS((uint32_t)(s_aw.sum / NS)),
                             CYC_NS(s_aw.max), CYC_NS(s_zl.max), CYC_NS(s_tk.max));
        }
    board_exit(0);
}
int main(void)
{
    xTaskCreate(hi_task, "hi", 256, NULL, 3, &hi);
    xTaskCreate(lo_task, "lo", 512, NULL, 1, NULL);
    timer0_start(12500 + 37, 0x40);                  /* ~500 us, odd period: phases drift */
    dt_start(12500 + 101, 0x00);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex8_sweep.c OUT=/var/tmp/rtos/C7/ex8 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex8/app.elf 120'
```

```text
   text	   data	    bss	    dec	    hex	filename
  13136	    104	 135456	 148696	  244d8	/var/tmp/rtos/C7/ex8/app.elf
mode    L_us aware_avg aware_max zero_max task_max (ns)
PRIMASK 0 160 320 360 10320
PRIMASK 2 200 2640 2400 11320
PRIMASK 5 560 5320 5320 14520
PRIMASK 10 1800 10000 10160 20000
PRIMASK 20 4280 20280 20320 30240
PRIMASK 50 15680 50360 50200 59080
PRIMASK 100 39360 100960 100320 109640
BASEPRI 0 160 1080 160 10320
BASEPRI 2 320 3120 160 11800
BASEPRI 5 720 6080 160 15960
BASEPRI 10 1680 11040 160 19720
BASEPRI 20 5160 20880 160 30040
BASEPRI 50 16840 50640 160 59320
BASEPRI 100 36080 99840 160 109400
SCHED   0 200 1720 160 13400
SCHED   2 200 1760 160 15440
SCHED   5 200 2120 160 14800
SCHED   10 200 1760 160 18800
SCHED   20 200 1760 160 28640
SCHED   50 160 1760 160 59440
SCHED   100 160 1560 160 108800
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="185" y="30" font-size="13" text-anchor="middle" font-weight="bold">IRQ 진입 지연 최댓값 (µs)</text>
<line x1="60" y1="250" x2="310" y2="250" stroke="currentColor"/>
<line x1="60" y1="50" x2="60" y2="250" stroke="currentColor"/>
<line x1="56" y1="250.0" x2="60" y2="250.0" stroke="currentColor"/>
<text x="53" y="254.0" font-size="12" text-anchor="end">0</text>
<line x1="56" y1="208.3" x2="60" y2="208.3" stroke="currentColor"/>
<text x="53" y="212.3" font-size="12" text-anchor="end">25</text>
<line x1="56" y1="166.7" x2="60" y2="166.7" stroke="currentColor"/>
<text x="53" y="170.7" font-size="12" text-anchor="end">50</text>
<line x1="56" y1="125.0" x2="60" y2="125.0" stroke="currentColor"/>
<text x="53" y="129.0" font-size="12" text-anchor="end">75</text>
<line x1="56" y1="83.3" x2="60" y2="83.3" stroke="currentColor"/>
<text x="53" y="87.3" font-size="12" text-anchor="end">100</text>
<line x1="56" y1="50.0" x2="60" y2="50.0" stroke="currentColor"/>
<text x="53" y="54.0" font-size="12" text-anchor="end">120</text>
<line x1="60.0" y1="250" x2="60.0" y2="254" stroke="currentColor"/>
<text x="60.0" y="268" font-size="12" text-anchor="middle">0</text>
<line x1="110.0" y1="250" x2="110.0" y2="254" stroke="currentColor"/>
<text x="110.0" y="268" font-size="12" text-anchor="middle">20</text>
<line x1="185.0" y1="250" x2="185.0" y2="254" stroke="currentColor"/>
<text x="185.0" y="268" font-size="12" text-anchor="middle">50</text>
<line x1="310.0" y1="250" x2="310.0" y2="254" stroke="currentColor"/>
<text x="310.0" y="268" font-size="12" text-anchor="middle">100</text>
<polyline points="60.0,249.5 65.0,245.6 72.5,241.1 85.0,233.3 110.0,216.2 185.0,166.1 310.0,81.7" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="60.0" cy="249.5" r="2.5" fill="#d0564a"/>
<circle cx="65.0" cy="245.6" r="2.5" fill="#d0564a"/>
<circle cx="72.5" cy="241.1" r="2.5" fill="#d0564a"/>
<circle cx="85.0" cy="233.3" r="2.5" fill="#d0564a"/>
<circle cx="110.0" cy="216.2" r="2.5" fill="#d0564a"/>
<circle cx="185.0" cy="166.1" r="2.5" fill="#d0564a"/>
<circle cx="310.0" cy="81.7" r="2.5" fill="#d0564a"/>
<polyline points="60.0,248.2 65.0,244.8 72.5,239.9 85.0,231.6 110.0,215.2 185.0,165.6 310.0,83.6" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 3"/>
<circle cx="60.0" cy="248.2" r="2.5" fill="#e08a3c"/>
<circle cx="65.0" cy="244.8" r="2.5" fill="#e08a3c"/>
<circle cx="72.5" cy="239.9" r="2.5" fill="#e08a3c"/>
<circle cx="85.0" cy="231.6" r="2.5" fill="#e08a3c"/>
<circle cx="110.0" cy="215.2" r="2.5" fill="#e08a3c"/>
<circle cx="185.0" cy="165.6" r="2.5" fill="#e08a3c"/>
<circle cx="310.0" cy="83.6" r="2.5" fill="#e08a3c"/>
<polyline points="60.0,249.7 65.0,249.7 72.5,249.7 85.0,249.7 110.0,249.7 185.0,249.7 310.0,249.7" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="60.0" cy="249.7" r="2.5" fill="#4a7bd0"/>
<circle cx="65.0" cy="249.7" r="2.5" fill="#4a7bd0"/>
<circle cx="72.5" cy="249.7" r="2.5" fill="#4a7bd0"/>
<circle cx="85.0" cy="249.7" r="2.5" fill="#4a7bd0"/>
<circle cx="110.0" cy="249.7" r="2.5" fill="#4a7bd0"/>
<circle cx="185.0" cy="249.7" r="2.5" fill="#4a7bd0"/>
<circle cx="310.0" cy="249.7" r="2.5" fill="#4a7bd0"/>
<polyline points="60.0,247.1 65.0,247.1 72.5,246.5 85.0,247.1 110.0,247.1 185.0,247.1 310.0,247.4" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="2 3"/>
<circle cx="60.0" cy="247.1" r="2.5" fill="#3f9a6b"/>
<circle cx="65.0" cy="247.1" r="2.5" fill="#3f9a6b"/>
<circle cx="72.5" cy="246.5" r="2.5" fill="#3f9a6b"/>
<circle cx="85.0" cy="247.1" r="2.5" fill="#3f9a6b"/>
<circle cx="110.0" cy="247.1" r="2.5" fill="#3f9a6b"/>
<circle cx="185.0" cy="247.1" r="2.5" fill="#3f9a6b"/>
<circle cx="310.0" cy="247.4" r="2.5" fill="#3f9a6b"/>
<text x="525" y="30" font-size="13" text-anchor="middle" font-weight="bold">IRQ → 태스크 지연 최댓값 (µs)</text>
<line x1="400" y1="250" x2="650" y2="250" stroke="currentColor"/>
<line x1="400" y1="50" x2="400" y2="250" stroke="currentColor"/>
<line x1="396" y1="250.0" x2="400" y2="250.0" stroke="currentColor"/>
<text x="393" y="254.0" font-size="12" text-anchor="end">0</text>
<line x1="396" y1="208.3" x2="400" y2="208.3" stroke="currentColor"/>
<text x="393" y="212.3" font-size="12" text-anchor="end">25</text>
<line x1="396" y1="166.7" x2="400" y2="166.7" stroke="currentColor"/>
<text x="393" y="170.7" font-size="12" text-anchor="end">50</text>
<line x1="396" y1="125.0" x2="400" y2="125.0" stroke="currentColor"/>
<text x="393" y="129.0" font-size="12" text-anchor="end">75</text>
<line x1="396" y1="83.3" x2="400" y2="83.3" stroke="currentColor"/>
<text x="393" y="87.3" font-size="12" text-anchor="end">100</text>
<line x1="396" y1="50.0" x2="400" y2="50.0" stroke="currentColor"/>
<text x="393" y="54.0" font-size="12" text-anchor="end">120</text>
<line x1="400.0" y1="250" x2="400.0" y2="254" stroke="currentColor"/>
<text x="400.0" y="268" font-size="12" text-anchor="middle">0</text>
<line x1="450.0" y1="250" x2="450.0" y2="254" stroke="currentColor"/>
<text x="450.0" y="268" font-size="12" text-anchor="middle">20</text>
<line x1="525.0" y1="250" x2="525.0" y2="254" stroke="currentColor"/>
<text x="525.0" y="268" font-size="12" text-anchor="middle">50</text>
<line x1="650.0" y1="250" x2="650.0" y2="254" stroke="currentColor"/>
<text x="650.0" y="268" font-size="12" text-anchor="middle">100</text>
<polyline points="400.0,232.8 405.0,231.1 412.5,225.8 425.0,216.7 450.0,199.6 525.0,151.5 650.0,67.3" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="400.0" cy="232.8" r="2.5" fill="#d0564a"/>
<circle cx="405.0" cy="231.1" r="2.5" fill="#d0564a"/>
<circle cx="412.5" cy="225.8" r="2.5" fill="#d0564a"/>
<circle cx="425.0" cy="216.7" r="2.5" fill="#d0564a"/>
<circle cx="450.0" cy="199.6" r="2.5" fill="#d0564a"/>
<circle cx="525.0" cy="151.5" r="2.5" fill="#d0564a"/>
<circle cx="650.0" cy="67.3" r="2.5" fill="#d0564a"/>
<polyline points="400.0,232.8 405.0,230.3 412.5,223.4 425.0,217.1 450.0,199.9 525.0,151.1 650.0,67.7" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 3"/>
<circle cx="400.0" cy="232.8" r="2.5" fill="#e08a3c"/>
<circle cx="405.0" cy="230.3" r="2.5" fill="#e08a3c"/>
<circle cx="412.5" cy="223.4" r="2.5" fill="#e08a3c"/>
<circle cx="425.0" cy="217.1" r="2.5" fill="#e08a3c"/>
<circle cx="450.0" cy="199.9" r="2.5" fill="#e08a3c"/>
<circle cx="525.0" cy="151.1" r="2.5" fill="#e08a3c"/>
<circle cx="650.0" cy="67.7" r="2.5" fill="#e08a3c"/>
<polyline points="400.0,227.7 405.0,224.3 412.5,225.3 425.0,218.7 450.0,202.3 525.0,150.9 650.0,68.7" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="2 3"/>
<circle cx="400.0" cy="227.7" r="2.5" fill="#3f9a6b"/>
<circle cx="405.0" cy="224.3" r="2.5" fill="#3f9a6b"/>
<circle cx="412.5" cy="225.3" r="2.5" fill="#3f9a6b"/>
<circle cx="425.0" cy="218.7" r="2.5" fill="#3f9a6b"/>
<circle cx="450.0" cy="202.3" r="2.5" fill="#3f9a6b"/>
<circle cx="525.0" cy="150.9" r="2.5" fill="#3f9a6b"/>
<circle cx="650.0" cy="68.7" r="2.5" fill="#3f9a6b"/>
<text x="185" y="290" font-size="12" text-anchor="middle">크리티컬 섹션 길이 L (µs)</text>
<text x="525" y="290" font-size="12" text-anchor="middle">크리티컬 섹션 길이 L (µs)</text>
<line x1="60" y1="306" x2="82" y2="306" stroke="#d0564a" stroke-width="3"/>
<text x="88" y="310" font-size="12">PRIMASK (aware·zero 둘 다)</text>
<line x1="400" y1="306" x2="422" y2="306" stroke="#e08a3c" stroke-width="3"/>
<text x="428" y="310" font-size="12">BASEPRI, aware IRQ</text>
<line x1="60" y1="324" x2="82" y2="324" stroke="#4a7bd0" stroke-width="3"/>
<text x="88" y="328" font-size="12">BASEPRI, zero-latency IRQ</text>
<line x1="400" y1="324" x2="422" y2="324" stroke="#3f9a6b" stroke-width="3"/>
<text x="428" y="328" font-size="12">스케줄러 잠금</text>
</svg>
```

그림 7 — 예제 8의 측정 결과(QEMU icount, 각 점은 300개 표본의 최댓값). 왼쪽: IRQ 진입 지연. 오른쪽: IRQ가 깨우는 우선순위 3 태스크까지의 지연. 왼쪽에서 PRIMASK(빨강)와 BASEPRI-aware(주황 점선)는 L을 따라 올라가지만, BASEPRI-zero-latency(파랑)와 스케줄러 잠금(초록 점선)은 바닥에 붙어 있다. 오른쪽에서는 세 선이 **겹친다**.

출력에서 볼 것 — 이 노트의 가장 중요한 숫자들이다.

1. **PRIMASK**: kernel-aware든 zero-latency든 **최대 지연 ≈ L**(L = 100 → 100.96 / 100.32 µs). 모두가 기다린다.
2. **BASEPRI**: kernel-aware는 ≈ L + 약 1 µs(진입/탈출 함수 비용), **zero-latency는 L과 무관하게 160 ns**. 문턱이 일을 한다.
3. **스케줄러 잠금**: 두 IRQ 모두 **L과 무관**(kernel-aware 최대 1.5~2.1 µs, zero-latency 160 ns). 1.7 µs 남짓은 `vTaskSuspendAll`/`xTaskResumeAll` 안의 짧은 BASEPRI 구간과 tick ISR 같은 커널 자신의 크리티컬 섹션이다(§6.3에서 하나씩 잰다).
4. **그런데 오른쪽(태스크 지연)은 세 잠금 모두 ≈ L + 10 µs다**(L = 100 → 109.64 / 109.40 / 108.80 µs). 높은 태스크 입장에서는 무엇으로 잠갔든 **똑같이 L만큼 늦는다**. 스케줄러 잠금이 "싸다"는 것은 ISR 입장에서만 맞는 말이다.
5. 평균(aware_avg)은 최댓값보다 훨씬 작다(PRIMASK L = 100에서 39 µs vs 101 µs). 평균은 "얼마나 자주 겹치나"(듀티)에 좌우되고, 마감을 결정하는 건 **최댓값**이다. 크리티컬 섹션 비용은 반드시 최댓값으로 보고한다.

> 실제 칩에서 달라지는 점: 예외 진입 하드웨어 스태킹(Cortex-M3/M4 최소 12사이클), 플래시 wait state, 캐시가 기본 지연(L = 0일 때의 값)을 키운다. 하지만 **"최대 지연 = 기본 지연 + 그 IRQ를 막는 가장 긴 크리티컬 섹션"**이라는 모양은 같다. 실칩에서는 GPIO 토글 + 로직 분석기 또는 DWT CYCCNT로 같은 표를 만든다.

### 6.2 설계 예산으로 바꾸기

"IRQ 지연 = 기본 지연 + 그 IRQ를 막는 가장 긴 크리티컬 섹션 + (그보다 높은 ISR들의 실행 시간)"이다. 마지막 항은 E2(우선순위 배치)와 J(분석)에서 다룬다. 예를 들어 드론 비행 제어기라면:

| 경로 | 마감 | 막는 가장 긴 크리티컬 섹션 | 판단 |
|---|---|---|---|
| DShot 출력 타이머 ISR (zero-latency, 0x00) | 수 µs | 없음 (문턱 위) | BASEPRI면 OK. PRIMASK 쓰는 코드가 하나라도 있으면 그 길이가 들어온다 |
| IMU data-ready ISR (kernel-aware) → rate loop 태스크 (8 kHz = 125 µs) | 125 µs 안에 시작 | 커널 내부 ≈ 2 µs + 앱이 만든 가장 긴 것 | 앱 크리티컬 섹션을 10 µs 이하로(주기의 8%) |
| 텔레메트리 UART RX ISR (FIFO 16바이트, 115200 baud) | 16 × 86.8 µs ≈ 1.39 ms | 같은 | 넉넉 — 하지만 로거가 1.39 ms를 잡으면 FIFO가 넘친다(§6.3) |

### 6.3 예제 9 — 실제 워크로드에서 BASEPRI가 올라가 있던 시간: 계측과 히스토그램

"우리 펌웨어의 가장 긴 크리티컬 섹션은 얼마인가?"에 답하려면 **모든 진입/탈출에 타임스탬프를 찍어야** 한다. 소스를 고치지 않고 하는 방법이 링커의 `--wrap`이다. `-Wl,--wrap=vPortEnterCritical`을 주면 다른 오브젝트(tasks.c, queue.c, 앱)의 `vPortEnterCritical` 호출이 모두 `__wrap_vPortEnterCritical`로 가고, 원래 함수는 `__real_vPortEnterCritical`로 부를 수 있다.

무엇을 확인하나: 1 kHz 센서 ISR → 큐 → 소비 태스크, 1 ms 주기 태스크, 10 ms마다 한 줄 남기는 로거 태스크가 도는 작은 시스템에서 500 ms 동안 모든 `taskENTER_CRITICAL` 구간의 길이 분포 · 총합 · 최댓값과 그 호출 위치를 잰다. 로거를 두 가지로 만든다 — 좋은 로거(크리티컬 섹션 안에서는 공유 버퍼에 16바이트 복사만), 나쁜 로거(크리티컬 섹션 안에서 UART로 직접 출력). QEMU의 UART는 baud 지연이 없으므로 **115200 baud의 글자당 86.8 µs를 바쁜 대기로 흉내 냈다**.

```c
/* ex9 — how long is BASEPRI raised? wrap vPortEnterCritical/ExitCritical, histogram */
#include "FreeRTOS.h"
#include "queue.h"
#include "c7.h"
void __real_vPortEnterCritical(void); void __real_vPortExitCritical(void);
static uint32_t depth, t_in, ra_in, n_cs, max_cs, max_ra, total, hist[8];
static const uint32_t edge[8] = { 25, 50, 125, 250, 500, 1250, 2500, 0xFFFFFFFF }; /* 1,2,5,10,20,50,100us */
void __wrap_vPortEnterCritical(void)
{
    __real_vPortEnterCritical();                     /* now masked */
    if (depth++ == 0) { t_in = CYC(); ra_in = (uint32_t)__builtin_return_address(0); }
}
void __wrap_vPortExitCritical(void)
{
    if (--depth == 0) {
        uint32_t d = CYC() - t_in, b = 0;
        n_cs++; total += d;
        if (d > max_cs) { max_cs = d; max_ra = ra_in; }
        while (d >= edge[b]) b++;
        hist[b]++;
    }
    __real_vPortExitCritical();
}
static QueueHandle_t q;
void TIMER0_IRQHandler(void)                         /* 1 kHz sensor ISR */
{
    static uint32_t n; BaseType_t w = pdFALSE;
    TIMER_INTCLR(TIMER0_BASE) = 1; n++; xQueueSendFromISR(q, &n, &w); portYIELD_FROM_ISR(w);
}
static void consumer(void *a) { uint32_t v; (void)a; for (;;) xQueueReceive(q, &v, portMAX_DELAY); }
static void periodic(void *a) { (void)a; for (;;) vTaskDelay(1); }       /* tick-driven task */
#if BAD
static void uart_slow_puts(const char *s) { while (*s++) spin_cyc(2170); } /* 115200 baud: 86.8 us/char */
#else
static char shared_log[64];
#endif
static void logger(void *a)
{
    (void)a;
    static const char line[] = "imu ok t=123456\n";  /* 16 chars */
    for (;;) {
        vTaskDelay(10);
        taskENTER_CRITICAL();
#if BAD
        uart_slow_puts(line);                        /* blocking print inside the CS */
#else
        for (unsigned i = 0; i < sizeof line; i++) shared_log[i] = line[i];  /* copy only */
#endif
        taskEXIT_CRITICAL();
    }
}
static void report(void *a)
{
    (void)a;
    uint32_t t0 = CYC(); vTaskDelay(500); uint32_t el = CYC() - t0;  taskDISABLE_INTERRUPTS();
    board_printf_raw("BAD=%d: %u critical sections in 500 ms, masked %u us = %u.%u%% of time\n",
                     BAD, n_cs, total / 25, total * 100 / el, (total * 1000 / el) % 10);
    board_printf_raw("hist <1 <2 <5 <10 <20 <50 <100 >=100us: %u %u %u %u %u %u %u %u\n", hist[0],
                     hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    board_printf_raw("max %u ns, entered from 0x%x\n", CYC_NS(max_cs), max_ra);
    board_exit(0);
}
int main(void)
{
    q = xQueueCreate(8, sizeof(uint32_t)); timer0_start(25000, 0x40);
    xTaskCreate(consumer, "cons", 256, NULL, 2, NULL); xTaskCreate(periodic, "per", 256, NULL, 3, NULL);
    xTaskCreate(logger, "log", 256, NULL, 1, NULL);    xTaskCreate(report, "rep", 512, NULL, 4, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt9; for b in 0 1; do make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex9_masked.c OUT=/var/tmp/rtos/C7/ex9_$b EXTRA_CFLAGS=-DBAD=$b EXTRA_LDFLAGS="-Wl,--wrap=vPortEnterCritical -Wl,--wrap=vPortExitCritical" && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex9_$b/app.elf 60; done; arm-none-eabi-addr2line -f -e /var/tmp/rtos/C7/ex9_0/app.elf 0x1a62 | head -1'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12956	    100	 135472	 148528	  24430	/var/tmp/rtos/C7/ex9_0/app.elf
BAD=0: 4628 critical sections in 500 ms, masked 4988 us = 0.9% of time
hist <1 <2 <5 <10 <20 <50 <100 >=100us: 2557 2064 5 2 0 0 0 0
max 7560 ns, entered from 0x1a63
[board_exit] code=0
   text	   data	    bss	    dec	    hex	filename
  13008	    100	 135472	 148580	  24464	/var/tmp/rtos/C7/ex9_1/app.elf
BAD=1: 4620 critical sections in 500 ms, masked 67651 us = 13.5% of time
hist <1 <2 <5 <10 <20 <50 <100 >=100us: 2508 2060 5 2 0 0 0 45
max 1393280 ns, entered from 0x1bf
[board_exit] code=0
prvCheckForValidListAndQueue
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="230" x2="650" y2="230" stroke="currentColor"/>
<line x1="70" y1="40" x2="70" y2="230" stroke="currentColor"/>
<line x1="66" y1="230.0" x2="650" y2="230.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="63" y="234.0" font-size="12" text-anchor="end">1</text>
<line x1="66" y1="182.5" x2="650" y2="182.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="63" y="186.5" font-size="12" text-anchor="end">10</text>
<line x1="66" y1="135.0" x2="650" y2="135.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="63" y="139.0" font-size="12" text-anchor="end">100</text>
<line x1="66" y1="87.5" x2="650" y2="87.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="63" y="91.5" font-size="12" text-anchor="end">1000</text>
<line x1="66" y1="40.0" x2="650" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="63" y="44.0" font-size="12" text-anchor="end">10000</text>
<rect x="80.2" y="68.1" width="24" height="161.9" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="92.2" y="64.1" font-size="12" text-anchor="middle">2557</text>
<rect x="107.2" y="68.5" width="24" height="161.5" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="119.2" y="64.5" font-size="12" text-anchor="middle">2508</text>
<text x="106.2" y="248" font-size="12" text-anchor="middle">0–1</text>
<rect x="152.8" y="72.6" width="24" height="157.4" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="164.8" y="68.6" font-size="12" text-anchor="middle">2064</text>
<rect x="179.8" y="72.6" width="24" height="157.4" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="191.8" y="68.6" font-size="12" text-anchor="middle">2060</text>
<text x="178.8" y="248" font-size="12" text-anchor="middle">1–2</text>
<rect x="225.2" y="196.8" width="24" height="33.2" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="237.2" y="192.8" font-size="12" text-anchor="middle">5</text>
<rect x="252.2" y="196.8" width="24" height="33.2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="264.2" y="192.8" font-size="12" text-anchor="middle">5</text>
<text x="251.2" y="248" font-size="12" text-anchor="middle">2–5</text>
<rect x="297.8" y="215.7" width="24" height="14.3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="309.8" y="211.7" font-size="12" text-anchor="middle">2</text>
<rect x="324.8" y="215.7" width="24" height="14.3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="336.8" y="211.7" font-size="12" text-anchor="middle">2</text>
<text x="323.8" y="248" font-size="12" text-anchor="middle">5–10</text>
<text x="396.2" y="248" font-size="12" text-anchor="middle">10–20</text>
<text x="468.8" y="248" font-size="12" text-anchor="middle">20–50</text>
<text x="541.2" y="248" font-size="12" text-anchor="middle">50–100</text>
<rect x="614.8" y="151.5" width="24" height="78.5" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="626.8" y="147.5" font-size="12" text-anchor="middle">45</text>
<text x="613.8" y="248" font-size="12" text-anchor="middle">≥100</text>
<text x="360.0" y="268" font-size="12" text-anchor="middle">크리티컬 섹션 한 번의 길이 (µs) — 500 ms 동안, 세로축은 개수(로그)</text>
<text x="70" y="22" font-size="13" font-weight="bold">BASEPRI가 올라가 있던 시간의 분포 (예제 9)</text>
<rect x="430" y="10" width="14" height="12" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="450" y="21" font-size="12">좋은 로거: 0.9%</text>
<rect x="555" y="10" width="14" height="12" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="575" y="21" font-size="12">나쁜 로거: 13.5%</text>
</svg>
```

그림 8 — 예제 9의 히스토그램(세로축 로그). 커널 자신의 크리티컬 섹션은 거의 다 2 µs 미만이다. 나쁜 로거는 개수로는 45개(1%)뿐이지만 **총 마스크 시간을 0.9% → 13.5%로**, 최댓값을 7.56 µs → **1393 µs**로 만든다.

출력에서 볼 것:

- 500 ms에 크리티컬 섹션이 **4628번** — ms당 9번이다. 1 kHz ISR 하나, 1 ms 태스크 하나, 큐 하나뿐인 작은 시스템에서도 커널은 이렇게 자주 잠근다. 그래서 "몇 번"이 아니라 **"가장 긴 것"**이 중요하다.
- 좋은 경우의 최댓값 7.56 µs는 `prvCheckForValidListAndQueue`(timers.c) — 스케줄러 시작 때 타이머 큐를 한 번 만드는 구간이다(시작 전이라 실제로는 아무도 기다리지 않는다). 정상 운전 중의 최댓값은 5 µs 미만 칸의 값들이다.
- 나쁜 경우 최댓값 1393.28 µs = 16글자 × 86.8 µs. 이 동안 kernel-aware ISR은 전부 기다리고, 1 kHz 센서 ISR은 **한 번 이상 통째로 사라진다**(예제 4에서 본 pending 비트 합쳐짐). 115200 baud UART RX의 16바이트 FIFO가 넘치는 데 걸리는 시간(1.39 ms)과 거의 같다 — 이 로거 하나로 텔레메트리 수신이 가끔 깨진다.
- 이 계측은 `vPortEnterCritical`만 잡는다. ISR 쪽의 `taskENTER_CRITICAL_FROM_ISR`(인라인 `mrs/msr BASEPRI`)와 SysTick 핸들러의 `vPortRaiseBASEPRI`는 함수 호출이 아니라서 `--wrap`으로 안 잡힌다. 그것까지 재려면 trace 훅(`traceISR_ENTER` 등, M2 · M3)이나 하드웨어 트레이스를 쓴다.

### 6.4 예제 9b — 커널의 크리티컬 섹션을 호출 위치별로

무엇을 확인하나: 같은 좋은 로거 워크로드에서, 진입한 호출 위치(`__builtin_return_address(0)`)별로 횟수 · 평균 · 최댓값을 모은다(예제 9에 16칸짜리 표를 더한 것 — 코드는 생략, 바뀐 부분은 `site[]` 표 갱신 6줄). 주소는 `addr2line`으로 함수:줄로 바꿨다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt9b; O=/var/tmp/rtos/C7/ex9b; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex9b.c OUT=$O EXTRA_CFLAGS=-DBAD=0 EXTRA_LDFLAGS="-Wl,--wrap=vPortEnterCritical -Wl,--wrap=vPortExitCritical" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 60 | grep site | while read a ra n avg max u; do printf "%s %s %s %s  " $ra $n $avg $max; arm-none-eabi-addr2line -f -s -e $O/app.elf $ra | paste -sd" "; done'
```

```text
0xd8c n=1062 avg=1440 max=1480  xTaskResumeAll tasks.c:4086
0x1636 n=1 avg=1600 max=1600  xQueueGenericReset queue.c:320
0x6ce n=6 avg=2800 max=6720  prvAddNewTaskToReadyList tasks.c:2046
0x1aae n=1 avg=7640 max=7640  prvCheckForValidListAndQueue timers.c:1123
0x1a60 n=1 avg=920 max=920  vQueueWaitForMessageRestricted queue.c:3159 (discriminator 1)
0x1580 n=502 avg=800 max=800  prvUnlockQueue queue.c:2509
0x15d0 n=502 avg=800 max=840  prvUnlockQueue queue.c:2590
0x18ae n=501 avg=1080 max=1080  xQueueReceive queue.c:1537
0x18ce n=501 avg=840 max=880  xQueueReceive queue.c:1606 (discriminator 1)
0x126c n=501 avg=920 max=960  xTaskCheckForTimeOut tasks.c:5706
0x18f8 n=501 avg=640 max=680  prvIsQueueEmpty queue.c:2625
0x191a n=500 avg=1800 max=1800  xQueueReceive queue.c:1537
0x1bc n=49 avg=600 max=600  logger ex9b.c:52
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">커널 자신의 크리티컬 섹션 — 호출 위치별 평균 길이 (ns, 예제 9b)</text>
<text x="250" y="55" font-size="12" text-anchor="end">xQueueReceive (1537, 대기 전)</text>
<rect x="260" y="43" width="306.0" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="572.0" y="56" font-size="12">1800</text>
<text x="250" y="85" font-size="12" text-anchor="end">xTaskResumeAll (4086)</text>
<rect x="260" y="73" width="244.8" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="510.8" y="86" font-size="12">1440</text>
<text x="250" y="115" font-size="12" text-anchor="end">xQueueReceive (1537, 첫 진입)</text>
<rect x="260" y="103" width="183.6" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="449.6" y="116" font-size="12">1080</text>
<text x="250" y="145" font-size="12" text-anchor="end">xTaskCheckForTimeOut</text>
<rect x="260" y="133" width="156.4" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="422.4" y="146" font-size="12">920</text>
<text x="250" y="175" font-size="12" text-anchor="end">xQueueReceive (1606)</text>
<rect x="260" y="163" width="142.8" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="408.8" y="176" font-size="12">840</text>
<text x="250" y="205" font-size="12" text-anchor="end">prvUnlockQueue (2509·2590)</text>
<rect x="260" y="193" width="136.0" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="402.0" y="206" font-size="12">800</text>
<text x="250" y="235" font-size="12" text-anchor="end">prvIsQueueEmpty</text>
<rect x="260" y="223" width="108.8" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="374.8" y="236" font-size="12">640</text>
<text x="250" y="265" font-size="12" text-anchor="end">logger 복사 (앱)</text>
<rect x="260" y="253" width="102.0" height="18" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="368.0" y="266" font-size="12">600</text>
<text x="260" y="285" font-size="12">모두 2 µs 미만 — 커널은 섹션을 "리스트 몇 개 바꾸는 만큼"만 잡는다</text>
</svg>
```

그림 9 — 커널 크리티컬 섹션의 호출 위치별 평균 길이(예제 9b, 정상 운전 중 반복되는 것만).

출력에서 볼 것:

- **블로킹 `xQueueReceive` 한 번에 크리티컬 섹션이 6번**: 첫 진입(1537), 대기 준비 후 `prvLockQueue`(1606), `xTaskCheckForTimeOut`, `prvIsQueueEmpty`, `prvUnlockQueue` 두 번(2509 · 2590). 각각 0.6 ~ 1.8 µs다. 커널은 긴 일을 하나의 긴 크리티컬 섹션으로 하지 않고, **짧은 조각 여러 개 + 그 사이는 스케줄러 잠금**으로 나눈다(§9).
- `xTaskResumeAll`의 크리티컬 섹션(1.44 µs)은 1062번 — 1 ms `vTaskDelay`(500번)와 큐 대기 경로(500번)가 모두 이걸 지난다.
- 태스크 생성(`prvAddNewTaskToReadyList`) 최대 6.72 µs, 타이머 큐 생성 7.64 µs — 초기화 때만이다. **런타임에 태스크/큐를 만들면 이 값이 운전 중 최악값이 된다**. 그래서 안전 지향 설계는 생성을 부팅 때 끝낸다.

### 6.5 예제 10 — 측정 숫자를 차트로 (Python)

무엇을 확인하나: 그림 7 ~ 9는 손으로 그린 것이 아니라 예제 8 · 9 · 9b의 출력 숫자를 Python으로 좌표 변환해 만든 SVG다. 아래는 그림 7(스윕) 부분이다. 히스토그램(로그 축)과 호출 위치 막대도 같은 파일에서 같은 방식으로 만든다(그 부분 약 50줄은 생략). VM에서 실행했다.

```python
# ex10 — turn measured outputs (ex8, ex9, ex9b) into SVG charts for the note
import math
Ls = [0, 2, 5, 10, 20, 50, 100]
ex8 = """PRIMASK 0 160 320 360 10320
PRIMASK 2 200 2640 2400 11320
... (ex8 output: 21 rows, middle rows omitted here)
SCHED 100 160 1560 160 108800"""
d = {}
for line in ex8.splitlines():
    m, L, avg, aw, zl, tk = line.split()
    d.setdefault(m, {})[int(L)] = (int(avg), int(aw), int(zl), int(tk))
B, O, G, R, GR = "#4a7bd0", "#e08a3c", "#3f9a6b", "#d0564a", "#888"
out = []
def panel(x0, title, series):
    w, h, y0 = 250, 200, 50                     # plot area
    s = [f'<text x="{x0 + w/2:.0f}" y="30" font-size="13" text-anchor="middle" font-weight="bold">{title}</text>']
    s.append(f'<line x1="{x0}" y1="{y0 + h}" x2="{x0 + w}" y2="{y0 + h}" stroke="currentColor"/>')
    s.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0 + h}" stroke="currentColor"/>')
    for v in (0, 25, 50, 75, 100, 120):
        y = y0 + h - v / 120 * h
        s.append(f'<line x1="{x0 - 4}" y1="{y:.1f}" x2="{x0}" y2="{y:.1f}" stroke="currentColor"/>')
        s.append(f'<text x="{x0 - 7}" y="{y + 4:.1f}" font-size="12" text-anchor="end">{v}</text>')
    for L in (0, 20, 50, 100):
        x = x0 + L / 100 * w
        s.append(f'<line x1="{x:.1f}" y1="{y0 + h}" x2="{x:.1f}" y2="{y0 + h + 4}" stroke="currentColor"/>')
        s.append(f'<text x="{x:.1f}" y="{y0 + h + 18}" font-size="12" text-anchor="middle">{L}</text>')
    for col, dash, pts in series:
        p = " ".join(f"{x0 + L / 100 * w:.1f},{y0 + h - min(v / 1000, 120) / 120 * h:.1f}" for L, v in pts)
        s.append(f'<polyline points="{p}" fill="none" stroke="{col}" stroke-width="2"{dash}/>')
        for L, v in pts:
            s.append(f'<circle cx="{x0 + L / 100 * w:.1f}" cy="{y0 + h - min(v / 1000, 120) / 120 * h:.1f}" r="2.5" fill="{col}"/>')
    return s
ser = lambda m, i: [(L, d[m][L][i]) for L in Ls]
out += ['<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">']
out += panel(60, "IRQ 진입 지연 최댓값 (µs)", [
    (R, "", ser("PRIMASK", 1)), (O, ' stroke-dasharray="6 3"', ser("BASEPRI", 1)),
    (B, "", ser("BASEPRI", 2)), (G, ' stroke-dasharray="2 3"', ser("SCHED", 1))])
out += panel(400, "IRQ → 태스크 지연 최댓값 (µs)", [
    (R, "", ser("PRIMASK", 3)), (O, ' stroke-dasharray="6 3"', ser("BASEPRI", 3)), (G, ' stroke-dasharray="2 3"', ser("SCHED", 3))])
out.append('<text x="185" y="290" font-size="12" text-anchor="middle">크리티컬 섹션 길이 L (µs)</text>')
out.append('<text x="525" y="290" font-size="12" text-anchor="middle">크리티컬 섹션 길이 L (µs)</text>')
leg = [(R, "PRIMASK (aware·zero 둘 다)"), (O, "BASEPRI, aware IRQ"), (B, "BASEPRI, zero-latency IRQ"), (G, "스케줄러 잠금")]
for i, (c, t) in enumerate(leg):
    x, y = 60 + (i % 2) * 340, 306 + (i // 2) * 18
    out.append(f'<line x1="{x}" y1="{y}" x2="{x + 22}" y2="{y}" stroke="{c}" stroke-width="3"/>')
    out.append(f'<text x="{x + 28}" y="{y + 4}" font-size="12">{t}</text>')
out.append('</svg>')
open("chart_sweep.svg", "w").write("\n".join(out) + "\n")

```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/rtos/C7/charts && cd /var/tmp/rtos/C7/charts && python3 -I /Users/donh/.rtos_scratch/C7/charts/gen.py && ls -l *.svg | awk "{print \$5, \$9}"'
```

```text
wrote chart_sweep.svg chart_hist.svg chart_sites.svg
chart_sweep.svg XML ok
chart_hist.svg XML ok
chart_sites.svg XML ok
3762 chart_hist.svg
2260 chart_sites.svg
7882 chart_sweep.svg
```

출력에서 볼 것: 세 SVG가 XML로 파싱된다(스크립트 끝에서 `xml.dom.minidom`으로 확인). 차트 좌표는 측정값에서 바로 나오므로, 측정을 다시 하면 숫자만 바꿔 다시 그리면 된다.

---

## 7. 규칙 — 짧게, 블로킹 금지, printf 금지, 최악값을 잰다

### 7.1 네 가지 규칙과 근거

| 규칙 | 근거 (이 노트의 측정) |
|---|---|
| **짧게** — 수 µs, 길어도 가장 짧은 마감의 몇 % | 최대 IRQ 지연 ≈ L (예제 8). 커널 자신은 2 µs 미만을 지킨다 (예제 9b) |
| **블로킹 API 금지** | 크리티컬 섹션 안: 멈춤, 리스트 손상 (예제 11). 스케줄러 잠금 안: assert (예제 11) |
| **printf · UART 출력 · 플래시 쓰기 금지** | 16글자 = 1.39 ms 마스크, 총 마스크 시간 0.9% → 13.5% (예제 9) |
| **평균이 아니라 최댓값을 잰다** | PRIMASK L = 100: 평균 39 µs, 최대 101 µs (예제 8). 위상이 맞물리면 최댓값이 숨는다 |

추가로:

- **크리티컬 섹션 안에서 메모리 할당 금지**(`pvPortMalloc`은 heap_4에서 스케줄러 잠금을 쓰고 걸리는 시간이 단편화에 따라 달라진다 — D7).
- **안에서 루프를 돌 때는 상한을 둔다**(리스트 순회, 재시도). 크리티컬 섹션 길이의 상한 = 루프 상한 × 한 바퀴.
- **안에서 부르는 함수를 확인한다**. "짧아 보이는" 함수가 내부에서 로그를 남기거나 assert 출력을 할 수 있다.

### 7.2 예제 11 — 블로킹을 하면 무슨 일이 일어나나

무엇을 확인하나: 길이 1짜리 큐를 가득 채운 뒤 세 가지 잘못을 저지른다. ① 스케줄러 잠금 안에서 `vTaskDelay(5)`, ② 스케줄러 잠금 안에서 타임아웃 10 tick짜리 `xQueueSend`, ③ **BASEPRI 크리티컬 섹션 안에서** 타임아웃 10 tick짜리 `xQueueSend`. ③은 assert가 없어서 시스템이 멈출 것이다 — 멈춘 것을 알아내려고 **문턱 위(0x00) 워치독 ISR**을 하나 두었다. tick 훅이 올리는 심장 박동(heartbeat)이 20 ms 동안 안 바뀌면 BASEPRI, ICSR의 PendSV/SysTick pending 비트, 그리고 **PSP 스택 프레임에서 꺼낸 태스크의 PC**를 출력한다(zero-latency ISR이라 FreeRTOS API는 안 부르고 레지스터만 읽는다).

```c
/* ex11 — blocking inside a scheduler lock (assert) or a critical section (hang) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "c7.h"
static QueueHandle_t q;
static volatile uint32_t beat, last_beat, stale;
void vApplicationTickHook(void) { beat++; }          /* configUSE_TICK_HOOK=1 */
void DUALTIMER_IRQHandler(void)                      /* prio 0x00 watchdog, 1 ms */
{
    DT_INTCLR = 1;
    if (beat != last_beat) { last_beat = beat; stale = 0; return; }
    if (++stale < 20) return;                         /* tick frozen for 20 ms */
    uint32_t psp, icsr = REG32(0xE000ED04u);
    __asm volatile("mrs %0, psp" : "=r"(psp));
    board_printf_raw("[watchdog] tick frozen 20 ms: BASEPRI=0x%x PendSV pending=%u "
                     "SysTick pending=%u task PC=0x%x\n", get_basepri(),
                     (icsr >> 28) & 1u, (icsr >> 26) & 1u, ((uint32_t *)psp)[6]);
    board_exit(2);
}
static void task(void *a)
{
    (void)a;
    uint32_t v = 0;
    xQueueSend(q, &v, 0);                             /* queue (length 1) is now full */
    board_printf_raw("variant %d\n", VARIANT);
#if VARIANT == 1
    vTaskSuspendAll();  vTaskDelay(5);  xTaskResumeAll();
#elif VARIANT == 2
    vTaskSuspendAll();  xQueueSend(q, &v, 10);  xTaskResumeAll();
#else
    taskENTER_CRITICAL();  xQueueSend(q, &v, 10);  taskEXIT_CRITICAL();
#endif
    board_printf_raw("returned normally\n");
    board_exit(0);
}
int main(void)
{
    q = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreate(task, "t", 512, NULL, 1, NULL);
    dt_start(25000, 0x00);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/C7/frt; for v in 1 2 3; do make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex7_block.c OUT=/var/tmp/rtos/C7/ex7_$v EXTRA_CFLAGS="-DVARIANT=$v -DconfigUSE_TICK_HOOK=1" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/ex7_$v/app.elf 20; done; arm-none-eabi-addr2line -f -e /var/tmp/rtos/C7/ex7_3/app.elf 0x121e'
```

```text
variant 1

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:2517
[board_exit] code=253
variant 2

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/queue.c:965
[board_exit] code=253
variant 3
[watchdog] tick frozen 20 ms: BASEPRI=0x20 PendSV pending=1 SysTick pending=1 task PC=0x121e
[board_exit] code=2
vListInsert
/var/rtoslab/src/FreeRTOS-Kernel/list.c:192 (discriminator 1)
```

출력에서 볼 것:

- ①: `tasks.c:2517`은 `vTaskDelay` 안의 `configASSERT( uxSchedulerSuspended == 1U )` — 바깥에서 이미 한 번 잠갔으니 2가 되어 걸린다. ②: `queue.c:965`는 `xQueueGenericSend` 첫머리의 `configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) )`. **스케줄러 잠금 안의 블로킹은 FreeRTOS가 잡아 준다.**
- ③: assert가 없다. 대신 **tick이 20 ms 동안 멈췄고**, BASEPRI는 여전히 0x20, PendSV와 SysTick이 둘 다 pending인 채 **처리되지 못하고 있다**(둘 다 우선순위 0xFF라 문턱 아래). 태스크의 PC는 `vListInsert`(list.c:192)의 리스트 순회 루프다.

③에서 일어난 일을 따라가 보자. `xQueueGenericSend`는 큐가 가득 차 있으니 자신을 대기 리스트에 넣고(`vTaskPlaceOnEventList`) `portYIELD_WITHIN_API()`로 PendSV를 요청한다. 하지만 바깥 크리티컬 섹션 때문에 BASEPRI = 0x20이라 PendSV가 **들어오지 못한다**. 그래서 yield가 일어나지 않은 채 함수 안의 `for(;;)` 루프가 다시 돌고, tick도 막혀 있으니 타임아웃도 영원히 안 온다. 두 번째 바퀴에서 이미 리스트에 들어 있는 항목을 **또 넣다가** 리스트가 고리 모양으로 깨지고, `vListInsert`의 순회가 끝나지 않는다. FreeRTOS도 이 자리에 주석을 달아 두었다(list.c, 순회 루프 바로 위).

```c
        /* *** NOTE ***********************************************************
        *  If you find your application is crashing here then likely causes are
        *  listed below.  In addition see https://www.freertos.org/Why-FreeRTOS/FAQs for
        *  more tips, and ensure configASSERT() is defined!
        ...
        *   3) Calling an API function from within a critical section or when
        *      the scheduler is suspended, or calling an API function that does
        *      not end in "FromISR" from an interrupt.
```

이 실험이 주는 디버깅 요령: **멈춘 시스템에서 BASEPRI(또는 PRIMASK)가 0이 아니고 PendSV가 pending이면, "크리티컬 섹션 안에서 블로킹했다"를 1순위로 의심한다.** 실칩에서는 디버거로 멈춰서 `BASEPRI`, `ICSR`, 태스크 PC를 보면 같은 정보가 나온다. 그리고 zero-latency 워치독(또는 하드웨어 워치독의 early-warning 인터럽트를 문턱 위에 두는 것)이 이런 멈춤에서 마지막 진단 기록을 남길 수 있는 유일한 코드다.

---

## 8. Zephyr — `irq_lock` key, `k_spin_lock`, `k_sched_lock`

### 8.1 소스로 읽기 — `arch_irq_lock()` (v4.4.2)

`irq_lock()`은 단일 코어 빌드에서 `arch_irq_lock()`이고(SMP에서는 전역 스핀락 `z_smp_global_lock()`), ARM 구현은 `include/zephyr/arch/arm/asm_inline_gcc.h`에 있다.

```c
static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	unsigned int key;

#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
#if CONFIG_MP_MAX_NUM_CPUS == 1 || defined(CONFIG_ARMV8_M_BASELINE)
	key = __get_PRIMASK();
	__disable_irq();
#else
#error "Cortex-M0 and Cortex-M0+ require SoC specific support for cross core synchronisation."
#endif
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	key = __get_BASEPRI();
	__set_BASEPRI_MAX(_EXC_IRQ_DEFAULT_PRIO);
	__ISB();
...
static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
	if (key != 0U) {
		return;
	}
	__enable_irq();
	__ISB();
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	__set_BASEPRI(key);
	__ISB();
```

FreeRTOS와 비교해 읽을 점.

- **중첩을 key로 지킨다**: 카운터가 없다. `irq_lock()`이 이전 BASEPRI(M0이면 PRIMASK)를 key로 돌려주고, `irq_unlock(key)`가 그 값으로 **되돌린다**. 2절의 PRIMASK 저장/복원과 같은 방식이고, 그래서 태스크든 ISR이든 **같은 API**를 쓴다(FreeRTOS처럼 `FromISR` 짝을 따로 두지 않는다). 대신 key를 **역순으로** 넘겨야 한다(irq.h 주석: "Interrupts are re-enabled by passing each of the keys to irq_unlock() in the reverse order they were acquired.").
- **`__set_BASEPRI_MAX`**: 현재 문턱보다 **올리는 쪽으로만** 쓴다(BASEPRI_MAX 레지스터 쓰기는 새 값이 더 높은 우선순위 문턱일 때만 반영된다 — 세부는 B3). 이미 더 높게 잠근 상태에서 불러도 문턱을 낮추지 않는다. FreeRTOS `vPortRaiseBASEPRI`는 그냥 `msr basepri`로 덮어쓴다.
- **문턱 값**: `_EXC_IRQ_DEFAULT_PRIO = Z_EXC_PRIO(_IRQ_PRIO_OFFSET)`(`include/zephyr/arch/arm/cortex_m/exception.h`). M3처럼 fault 우선순위를 프로그래밍할 수 있는 코어는 `_EXCEPTION_RESERVED_PRIO = 1`(가장 높은 0번은 fault용), `CONFIG_ZERO_LATENCY_IRQS`를 켜면 `CONFIG_ZERO_LATENCY_LEVELS`만큼 더 비켜선다. mps2/an385의 DT는 `arm,num-irq-priority-bits = <3>`이라 한 단계 = 0x20이다. 그래서 기본 0x20, ZLI를 켜면 0x40이 나와야 한다 — 예제 12에서 확인한다.
- **경고**: irq.h는 "Holding the interrupt lock when a context switch occurs is illegal."이라고 적는다. FreeRTOS와 같은 규칙이다.

### 8.2 `k_spin_lock` — 단일 코어에서는 곧 irq lock

`include/zephyr/spinlock.h`:

```c
static ALWAYS_INLINE k_spinlock_key_t k_spin_lock(struct k_spinlock *l)
{
	ARG_UNUSED(l);
	k_spinlock_key_t k;

	/* Note that we need to use the underlying arch-specific lock
	 * implementation.  The "irq_lock()" API in SMP context is
	 * actually a wrapper for a global spinlock!
	 */
	k.key = arch_irq_lock();

	z_spinlock_validate_pre(l);
#ifdef CONFIG_SMP
	...  /* ticket or CAS spin on l->owner / l->locked */
#endif /* CONFIG_SMP */
	z_spinlock_validate_post(l);

	return k;
}
```

단일 코어(`CONFIG_SMP=n`)에서는 `arch_irq_lock()` + (켜져 있으면) 검증 훅뿐이다. `struct k_spinlock`의 필드는 `CONFIG_SMP`이나 `CONFIG_SPIN_VALIDATE`일 때만 생기므로 **크기가 0**일 수 있다(예제 12에서 0). Zephyr 커널 내부는 거의 전부 `k_spin_lock`을 쓰고, 그래야 같은 코드가 SMP에서도 맞다. **새 Zephyr 코드에서는 `irq_lock` 대신 `k_spin_lock`**이 권장이다 — Linux의 `spin_lock_irqsave`와 같은 모양(EL F4)이다.

### 8.3 `k_sched_lock` — 바이트 하나를 내린다

`kernel/sched.c`:

```c
void k_sched_lock(void)
{
	LOG_DBG("scheduler locked (%p:%d)",
		_current, _current->base.sched_locked);

	K_SPINLOCK(&_sched_spinlock) {
		SYS_PORT_TRACING_FUNC(k_thread, sched_lock);

		__ASSERT(!arch_is_in_isr(), "");
		__ASSERT(_current->base.sched_locked != 1U, "");

		--_current->base.sched_locked;

		compiler_barrier();
	}
}
```

`sched_locked`는 **스레드마다** 있는 `uint8_t`이고, `prio`(`int8_t`)와 붙어 16비트 `preempt`를 이룬다(`include/zephyr/kernel/thread.h`). 잠그면 0 → 0xFF로 내려가서 합친 값이 0x0080 이상 = "협력형(cooperative) 스레드처럼 취급"이 된다. FreeRTOS `uxSchedulerSuspended`(전역 하나)와 다른 점:

- Zephyr 문서(kernel.h): "If the thread subsequently performs an operation that makes it unready, it will be context switched out in the normal manner. When the thread again becomes the current thread, its non-preemptible status is maintained." — 즉 **잠근 채 블록하면 assert가 아니라 그냥 다른 스레드가 돈다**(잠금은 그 스레드에 붙어 있다). FreeRTOS는 같은 상황을 assert로 막는다(예제 11 ①).
- 같은 주석: "It does not prevent interrupts from happening, nor does it prevent threads with MetaIRQ priorities from preempting the current thread." 그리고 "In general this is a historical API not well-suited to modern applications, use with care."

### 8.4 예제 12 — Zephyr에서 key 값, BASEPRI, 그리고 같은 L = 50 µs 측정

무엇을 확인하나: mps2/an385에서 ① 중첩 `irq_lock`의 key와 BASEPRI, ② `k_spin_lock`의 BASEPRI와 `sizeof(struct k_spinlock)`, ③ `k_sched_lock` 중 BASEPRI와 `sched_locked`, ④ `IRQ_CONNECT` 우선순위가 실제 NVIC에 몇으로 들어갔나, ⑤ 세 잠금으로 50 µs를 잡는 동안 TIMER0(우선순위 2) 진입 지연, DUALTIMER(우선순위 0) 진입 지연, TIMER0이 `k_sem_give`로 깨우는 스레드(우선순위 2)의 지연. 기본 빌드와 `CONFIG_ZERO_LATENCY_IRQS=y` 빌드를 비교한다(ZLI는 `IRQ_DIRECT_CONNECT`로만 연결할 수 있다 — `IRQ_CONNECT`로 하면 `BUILD_ASSERT`가 "ZLI interrupts must be registered using IRQ_DIRECT_CONNECT()"로 빌드를 멈춘다. 첫 시도에서 실제로 걸렸다).

```c
/* ex12 — Zephyr v4.4.2 (mps2/an385): irq_lock keys, k_spin_lock, k_sched_lock, latency */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/spinlock.h>
#define REG(a)  (*(volatile uint32_t *)(a))
#define CYC()   REG(0x40028018u)                         /* FPGAIO 25 MHz */
#define T0      0x40000000u                              /* TIMER0, IRQ 8 */
#define DT      0x40002000u                              /* DUALTIMER, IRQ 10 */
#define NS 300
static inline uint32_t basepri(void) { uint32_t r; __asm volatile("mrs %0, basepri" : "=r"(r)); return r; }
static volatile uint32_t mx_aw, mx_zl, mx_th, n_aw, n_th, on;
static struct k_spinlock lock;
static K_SEM_DEFINE(sem, 0, 1);
static void t0_isr(const void *a)
{
    uint32_t l = REG(T0 + 8) - REG(T0 + 4); REG(T0 + 0xC) = 1; ARG_UNUSED(a);
    if (on) { if (l > mx_aw) mx_aw = l; n_aw++; k_sem_give(&sem); }
}
static void dt_body(void) { uint32_t l = REG(DT) - REG(DT + 4); REG(DT + 0xC) = 1; if (on && l > mx_zl) mx_zl = l; }
#ifdef CONFIG_ZERO_LATENCY_IRQS
ISR_DIRECT_DECLARE(dt_direct) { dt_body(); return 0; }  /* ZLI: direct ISR, no kernel calls */
#else
static void dt_isr(const void *a) { ARG_UNUSED(a); dt_body(); }
#endif
static void hi_fn(void *a, void *b, void *c)            /* prio 2: woken by TIMER0 ISR */
{
    for (;;) { k_sem_take(&sem, K_FOREVER); uint32_t l = REG(T0 + 8) - REG(T0 + 4);
               if (on) { if (l > mx_th) mx_th = l; n_th++; } }
}
K_THREAD_DEFINE(hi, 1024, hi_fn, NULL, NULL, NULL, 2, 0, 0);
static void spin(uint32_t us) { uint32_t t = CYC(); while (CYC() - t < us * 25u) { } }
int main(void)
{
    k_thread_priority_set(k_current_get(), 5);       /* main = the lock holder, prio 5 */
    unsigned k1 = irq_lock(); uint32_t b1 = basepri();
    unsigned k2 = irq_lock(); uint32_t b2 = basepri();
    irq_unlock(k2); uint32_t b3 = basepri(); irq_unlock(k1); uint32_t b4 = basepri();
    printk("irq_lock: key1=0x%x BASEPRI=0x%x key2=0x%x BASEPRI=0x%x | unlock2 0x%x unlock1 0x%x\n",
           k1, b1, k2, b2, b3, b4);
    k_spinlock_key_t sk = k_spin_lock(&lock); uint32_t bs = basepri(); k_spin_unlock(&lock, sk);
    k_sched_lock(); uint32_t bl = basepri(); uint8_t sl = k_current_get()->base.sched_locked; k_sched_unlock();
    printk("k_spin_lock: BASEPRI=0x%x sizeof(k_spinlock)=%u | k_sched_lock: BASEPRI=0x%x sched_locked=0x%x\n",
           bs, (unsigned)sizeof(lock), bl, sl);
    IRQ_CONNECT(8, 2, t0_isr, NULL, 0);
#ifdef CONFIG_ZERO_LATENCY_IRQS
    IRQ_DIRECT_CONNECT(10, 0, dt_direct, IRQ_ZERO_LATENCY);
#else
    IRQ_CONNECT(10, 0, dt_isr, NULL, 0);
#endif
    printk("NVIC prio: TIMER0=0x%x DUALTIMER=0x%x\n", *(volatile uint8_t *)0xE000E408u, *(volatile uint8_t *)0xE000E40Au);
    REG(T0 + 8) = 12500 + 37 - 1; REG(T0) = 1 | 8; irq_enable(8);
    REG(DT) = 12500 + 101 - 1; REG(DT + 8) = 0xE2; irq_enable(10);
    static const char *nm[] = { "irq_lock   ", "k_spin_lock", "k_sched_lock" };
    uint32_t seed = 1;
    for (int m = 0; m < 3; m++) {
        mx_aw = mx_zl = mx_th = n_aw = n_th = 0; on = 1;
        while (n_aw < NS || n_th < NS) {
            if (m == 0) { unsigned k = irq_lock(); spin(50); irq_unlock(k); }
            else if (m == 1) { k_spinlock_key_t k = k_spin_lock(&lock); spin(50); k_spin_unlock(&lock, k); }
            else { k_sched_lock(); spin(50); k_sched_unlock(); }
            seed = seed * 1103515245u + 12345u; spin(10 + (seed >> 16) % 31);
        }
        on = 0;
        printk("%s L=50us: aware IRQ max=%u ns, other IRQ max=%u ns, IRQ->thread max=%u ns\n",
               nm[m], mx_aw * 40, mx_zl * 40, mx_th * 40);
    }
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");   /* semihosting exit */
    return 0;
}
```

`prj.conf`는 `CONFIG_BOOT_BANNER=n` 한 줄, `CMakeLists.txt`는 A7의 Zephyr 예제와 같은 4줄(`find_package(Zephyr …)` + `target_sources(app PRIVATE src/main.c)`)이다.

```sh
limactl shell elinux -- bash -lc 'for v in "default:" "zli:-DCONFIG_ZERO_LATENCY_IRQS=y"; do n=${v%%:*}; a=${v#*:}; echo "== $n"; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C7/zcs /var/tmp/rtos/C7/zcs_$n -- $a 2>&1 | grep -i "warning\|error" ; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C7/zcs_$n/zephyr/zephyr.elf 60; done'
```

```text
== default
irq_lock: key1=0x0 BASEPRI=0x20 key2=0x20 BASEPRI=0x20 | unlock2 0x20 unlock1 0x0
k_spin_lock: BASEPRI=0x20 sizeof(k_spinlock)=0 | k_sched_lock: BASEPRI=0x0 sched_locked=0xff
NVIC prio: TIMER0=0x60 DUALTIMER=0x20
irq_lock    L=50us: aware IRQ max=51480 ns, other IRQ max=50760 ns, IRQ->thread max=70680 ns
k_spin_lock L=50us: aware IRQ max=51280 ns, other IRQ max=50600 ns, IRQ->thread max=70480 ns
k_sched_lock L=50us: aware IRQ max=1440 ns, other IRQ max=16760 ns, IRQ->thread max=71800 ns
== zli
irq_lock: key1=0x0 BASEPRI=0x40 key2=0x40 BASEPRI=0x40 | unlock2 0x40 unlock1 0x0
k_spin_lock: BASEPRI=0x40 sizeof(k_spinlock)=0 | k_sched_lock: BASEPRI=0x0 sched_locked=0xff
NVIC prio: TIMER0=0x80 DUALTIMER=0x0
irq_lock    L=50us: aware IRQ max=50240 ns, other IRQ max=200 ns, IRQ->thread max=69440 ns
k_spin_lock L=50us: aware IRQ max=50640 ns, other IRQ max=200 ns, IRQ->thread max=70200 ns
k_sched_lock L=50us: aware IRQ max=1280 ns, other IRQ max=200 ns, IRQ->thread max=71480 ns
```

출력에서 볼 것:

- **key 방식 중첩**: key1 = 0(이전 문턱 없음), key2 = 0x20(이미 잠긴 상태). unlock2 뒤에도 0x20, unlock1 뒤에 0. FreeRTOS 카운터와 결과는 같지만 **저장한 값으로 되돌리는** 방식이다.
- **문턱 값**: 기본 0x20, ZLI를 켜면 **0x40**. 8.1절에서 소스로 계산한 값 그대로다. `IRQ_CONNECT(8, 2, …)`는 NVIC에 0x60((2 + 1) × 0x20)으로, ZLI에서는 0x80((2 + 2) × 0x20)으로 들어갔다 — **Zephyr의 IRQ 우선순위 숫자는 "커널 예약분 뒤로 밀린" 논리 번호**다. 기본 빌드에서는 `IRQ_CONNECT(10, 0, …)`도 0x20이라 `irq_lock`(BASEPRI 0x20)에 막힌다("other IRQ max = 50.76 µs"). Zephyr에서 우선순위 0은 "zero-latency"가 아니다. ZLI 빌드에서 `IRQ_ZERO_LATENCY`로 연결해야 NVIC 0x00이 되고, 그때 **200 ns**로 50 µs 잠금을 무시한다.
- **`k_spin_lock` = `irq_lock`**: BASEPRI도 같고 지연도 같다(51.28 vs 51.48 µs). 크기 0바이트.
- **`k_sched_lock`**: BASEPRI는 0 그대로, `sched_locked = 0xff`. aware IRQ는 1.44 µs로 L과 무관 — FreeRTOS 스케줄러 잠금과 같은 모양이다. 그런데 기본 빌드의 "other IRQ"(NVIC 0x20, 일반 IRQ)가 **16.76 µs**다. 스레드 깨우기 · 전환 경로 안에 있는 Zephyr 커널 자신의 `k_spin_lock` 구간이 그만큼 길었다는 뜻이다(A7에서 잰 Zephyr의 깨우기 경로가 FreeRTOS보다 길었던 것과 같은 원인). ZLI 빌드에서는 그것조차 200 ns다.
- **IRQ → 스레드는 세 잠금 모두 약 70 µs** = 50 µs + Zephyr의 깨우기 경로 약 20 µs. FreeRTOS 예제 8의 결론과 같다 — 무엇으로 잠갔든 높은 스레드는 L만큼 늦는다.

### 8.5 FreeRTOS ↔ Zephyr 대응표

| 하고 싶은 일 | FreeRTOS V11.3.2 (ARM_CM3) | Zephyr v4.4.2 (ARMv7-M) |
|---|---|---|
| 태스크에서 ISR과 공유 데이터 보호 | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` (카운터) | `k_spin_lock(&l)` / `k_spin_unlock(&l, key)` (key) |
| ISR에서 같은 보호 | `x = taskENTER_CRITICAL_FROM_ISR()` / `taskEXIT_CRITICAL_FROM_ISR(x)` | 위와 같은 API |
| 문턱 값 | `configMAX_SYSCALL_INTERRUPT_PRIORITY` (직접 정함) | `_EXC_IRQ_DEFAULT_PRIO` (우선순위 비트 · ZLI 설정에서 계산) |
| 문턱 위 ISR | NVIC 숫자를 문턱보다 작게 직접 설정 | `CONFIG_ZERO_LATENCY_IRQS=y` + `IRQ_DIRECT_CONNECT(…, IRQ_ZERO_LATENCY)` |
| 모든 인터럽트 끄기 | `__disable_irq()` (커널 API 아님) | `irq_lock()`은 BASEPRI — PRIMASK는 CMSIS `__disable_irq()` |
| 스케줄러 잠금 | `vTaskSuspendAll()` / `xTaskResumeAll()` (전역, 블록하면 assert) | `k_sched_lock()` / `k_sched_unlock()` (스레드별, 블록하면 그냥 전환) |
| 잘못 쓰면 | `configASSERT` (port.c:438, port.c:803, tasks.c:2517, queue.c:965) | `__ASSERT` (`CONFIG_ASSERT=y`일 때), `CONFIG_SPIN_VALIDATE` |

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나 — 커널은 크리티컬 섹션을 어떻게 쓰나

예제 9b에서 커널은 블로킹 `xQueueReceive` 한 번에 크리티컬 섹션을 6번 잡았고, 하나하나가 2 µs 미만이었다. 그 설계를 `xQueueGenericSend`(queue.c line 949)로 따라가 보자. 핵심은 **세 겹의 보호를 길이에 따라 나눠 쓴다**는 것이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah10" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">xQueueGenericSend — 짧은 일은 크리티컬 섹션, 긴 일은 스케줄러 잠금 + 큐 잠금</text>
<rect x="20" y="40" width="300" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="60" font-size="12" text-anchor="middle">① taskENTER_CRITICAL (BASEPRI)</text>
<text x="170" y="78" font-size="12" text-anchor="middle">자리 있음? → 복사 → 받는 태스크 깨움</text>
<text x="170" y="96" font-size="12" text-anchor="middle">가득? → 타임아웃 기록 후 EXIT</text>
<line x1="320" y1="73" x2="370" y2="73" stroke="currentColor" marker-end="url(#ah10)"/>
<rect x="372" y="40" width="290" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="517" y="60" font-size="12" text-anchor="middle">자리 있으면 여기서 끝</text>
<text x="517" y="78" font-size="12" text-anchor="middle">(대부분의 호출, 약 1~2 µs)</text>
<text x="517" y="96" font-size="12" text-anchor="middle">타임아웃 0이면 errQUEUE_FULL</text>
<line x1="170" y1="106" x2="170" y2="130" stroke="currentColor" marker-end="url(#ah10)"/>
<rect x="20" y="132" width="640" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="152" font-size="12">② vTaskSuspendAll() — 다른 태스크만 막는다, ISR은 계속 돈다</text>
<rect x="40" y="162" width="600" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="50" y="182" font-size="12">③ prvLockQueue — 짧은 크리티컬 섹션으로 cRxLock / cTxLock 표시만</text>
<text x="50" y="202" font-size="12">이 동안 ISR의 ...FromISR은 대기 리스트를 건드리지 않고 cTxLock 카운트만 올린다</text>
<text x="50" y="222" font-size="12">xTaskCheckForTimeOut → prvIsQueueFull → vTaskPlaceOnEventList (리스트 정렬 삽입)</text>
<text x="50" y="238" font-size="12">prvUnlockQueue — 쌓인 카운트만큼 깨우기 처리 (짧은 크리티컬 섹션)</text>
<line x1="340" y1="252" x2="340" y2="276" stroke="currentColor" marker-end="url(#ah10)"/>
<rect x="160" y="278" width="360" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="296" font-size="12" text-anchor="middle">xTaskResumeAll → 안 했으면 portYIELD_WITHIN_API</text>
<text x="340" y="312" font-size="12" text-anchor="middle">PendSV로 블록 → 깨어나면 ①부터 다시</text>
</svg>
```

그림 10 — `xQueueGenericSend`의 세 겹 보호. 순서도 줄 번호는 V11.3.2 queue.c 기준(① 971, ② 1117, ③ 1118).

왜 이렇게 나눴나?

- ①은 **큐 데이터**(아이템 복사, `uxMessagesWaiting`)와 **대기 리스트에서 하나 꺼내기**처럼 ISR도 동시에 만지는 것만 다룬다. 짧다. ISR의 `xQueueGenericSendFromISR`도 같은 데이터를 `taskENTER_CRITICAL_FROM_ISR()`(queue.c line 1205) 안에서 만진다.
- ②는 **대기 리스트에 자기를 정렬 삽입**하는 일 — 대기 태스크 수에 비례해 길어질 수 있는 일이다. 이걸 BASEPRI 안에서 하면 리스트가 길수록 IRQ 지연이 늘어난다. 그래서 스케줄러 잠금으로 "다른 태스크"만 막는다.
- 그런데 ② 동안 ISR이 이 큐에 보내면? ISR이 대기 리스트를 만지면 ②의 삽입과 충돌한다. 그래서 ③ **큐 잠금**(`cTxLock`/`cRxLock`)을 건다. 잠긴 큐에 ISR이 보내면 `xQueueGenericSendFromISR`은 대기 리스트를 건드리지 않고(`if( cTxLock == queueUNLOCKED )` 분기, line 1223) `prvIncrementQueueTxLock`(line 1319)으로 "나중에 처리할 일 개수"만 올린다. `prvUnlockQueue`가 그 개수만큼 깨우기를 대신 해 준다.

이것이 커널의 크리티컬 섹션 규칙이다 — **"ISR과 공유하는 최소한만 BASEPRI로, 길어질 수 있는 리스트 작업은 스케줄러 잠금으로, 그 사이 ISR 요청은 카운터로 미룬다."** 앱 코드도 같은 원칙을 따르면 된다. 같은 생각이 tick에도 있다: SysTick 핸들러(`xPortSysTickHandler`)는 `portDISABLE_INTERRUPTS()`(BASEPRI 0x20) 안에서 `xTaskIncrementTick()`을 부르고, PendSV 핸들러는 `vTaskSwitchContext()`를 부르는 동안만 BASEPRI를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`로 올린다(port.c line 472 ~ 475).

Zephyr도 구조는 같다. 커널 객체마다 `struct k_spinlock`을 두고(`kernel/sem.c`의 `lock`, `kernel/sched.c`의 `_sched_spinlock` 등) 짧게 잡는다. 대기 큐는 기본이 `CONFIG_WAITQ_SIMPLE`(이중 연결 리스트)이고, `CONFIG_WAITQ_SCALABLE`이면 균형 트리(rbtree)라 대기자가 많을 때 삽입 시간이 덜 늘어난다 — 크리티컬 섹션 길이 상한을 바꾸는 선택이다(D 모듈에서 다시 본다).

이어지는 노트: 큐 내부는 **D5**, 뮤텍스 · 우선순위 상속은 **D2 · D3**, `FromISR`와 지연 처리 패턴은 **E1 · E3**, 컨텍스트 스위치(PendSV)는 **C2**, tick은 **C5**.

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 — IMU 타임스탬프와 DShot

STM32H7급 FC에서 IMU data-ready EXTI ISR은 64비트 µs 타임스탬프를 찍어 샘플과 함께 큐(또는 링버퍼)에 넣는다. 예제 2의 교훈이 그대로 적용된다 — 32비트 타이머 + 소프트웨어 상위 워드 시계는 **wrap pending 보정**이 있어야 한다. 그리고 DShot 출력은 타이머 + DMA로 µs 단위 타이밍을 맞추므로 그 ISR을 문턱 위(zero-latency)에 둔다. 이렇게 하면 로거나 파라미터 저장 태스크가 실수로 긴 크리티컬 섹션을 만들어도 모터 출력은 흔들리지 않는다(예제 8의 파란 선). 대가는 그 ISR에서 RTOS API를 못 쓴다는 것 — 태스크에 알릴 때는 D8의 SPSC 링버퍼 또는 kernel-aware 소프트웨어 인터럽트를 pend한다(A7 §7).

```c
/* IMU ISR (kernel-aware) — the critical section covers only the shared ring head */
void EXTI_IMU_IRQHandler(void)
{
    uint64_t ts = now_us_masked_fix();       /* ex2 (3): wrap-safe 64-bit time */
    UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    ring_push(&imu_ring, ts);                /* a few stores, bounded */
    taskEXIT_CRITICAL_FROM_ISR(s);
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR(rate_task, &w);   /* wake the rate loop */
    portYIELD_FROM_ISR(w);
}
```

(빌드 · 실행하지 않은 스케치다 — 함수 이름은 설명용.)

### 10.2 로봇 관절 모터 컨트롤러 — 20 kHz FOC와 CAN

FOC 전류 루프(50 µs 주기)는 ADC 완료 ISR에서 끝까지 계산하고, 문턱 위에 둔다. 속도/위치 루프(1 kHz)와 CAN 명령 처리 태스크가 공유하는 "목표 토크"는 32비트 `float` 하나라 읽기/쓰기만 하면 보호가 필요 없다(§1.5 표 첫 줄 — 정렬된 32비트 단일 접근). 하지만 "목표 토크 + 제한값 + 모드" 세 필드를 묶어 바꾸면 찢어진 읽기가 생긴다. 이때 전류 루프는 zero-latency라 BASEPRI 크리티컬 섹션으로는 보호가 안 된다(문턱 위 ISR은 막히지 않는다!). 해법은 ① 더블 버퍼 + 인덱스 한 번 쓰기(D8), ② 정말 짧은 PRIMASK 구간(수백 ns) — 이 경우 그 길이가 전류 루프 지터에 그대로 들어간다는 것을 측정해서 확인한다.

### 10.3 Linux SoC 옆의 센서 허브 (Zephyr)

Zephyr 센서 허브에서 드라이버 코드는 `k_spin_lock`을 쓰고, 이게 UP 빌드에서 BASEPRI 문턱이라는 것을 안다(예제 12). 타임스탬프를 SoC와 맞추는 PPS 캡처 ISR이 있다면 `CONFIG_ZERO_LATENCY_IRQS`를 켜고 `IRQ_DIRECT_CONNECT(…, IRQ_ZERO_LATENCY)`로 연결한다 — 이걸 안 하면 Zephyr 우선순위 0도 커널 잠금에 막힌다(예제 12 기본 빌드의 50.76 µs).

### 10.4 SSD 컨트롤러 펌웨어 (Don의 경험)

SSD 펌웨어의 "인터럽트 끄고 큐 조작" 코드를 RTOS로 옮기면 **세 가지를 다시 본다**. ① 그 데이터를 ISR이 만지나? 아니면 태스크끼리면 스케줄러 잠금이나 뮤텍스로 내린다. ② 만진다면 BASEPRI로 바꾸고, NAND/DMA 완료처럼 µs 마감이 있는 ISR은 문턱 위로 뺀다. ③ 구간 안에 로그(UART/메모리 로그 플러시), assert 출력, 플래시 쓰기가 있는지 확인한다 — 예제 9의 나쁜 로거가 SSD 펌웨어에서는 "디버그 로그를 켜면 타임아웃이 난다"로 나타난다. 그리고 멀티코어였다면 크리티컬 섹션은 **코어 하나**만 지킨다는 점(§2.4).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR과 공유한 카운터를 `x++` | 통계가 조금씩 모자람, 가끔 큐 유실 | RMW 창 (예제 1: 창 3/5 → 60%) | 크리티컬 섹션, 또는 LDREX/STREX(D8) |
| 64비트 시계를 hi/lo 두 번 읽기 | 드물게 시간이 2 ms(한 주기) 뒤로 | 찢어진 읽기 (예제 2) | hi-lo-hi 재시도, 또는 마스크 + pending 보정 |
| "끄면 안전"이라 믿고 하드웨어 카운터 읽기 | 끈 상태에서도 시간이 거꾸로 | 하드웨어는 계속 돈다 (예제 2 ②) | INTSTATUS/COUNTFLAG로 놓친 wrap 보정 |
| helper가 `__enable_irq()`로 무조건 켬 | 보호했는데도 유실 (예제 3: 3660개) | 중첩 무시 | 저장/복원(`__get_PRIMASK` → `__set_PRIMASK`) |
| `taskDISABLE_INTERRUPTS()` 뒤에 enter/exit 짝 | 끈 줄 알았던 구간이 열려 있음 | exit가 BASEPRI = 0 (예제 4) | 태스크에서는 `taskENTER_CRITICAL`만 쓴다 |
| ISR에서 `taskENTER_CRITICAL()` | assert port.c:438, assert 꺼져 있으면 낮은 ISR 보호 해제 | 카운터 방식은 ISR용이 아님 | `taskENTER_CRITICAL_FROM_ISR()` |
| 우선순위 안 정한 IRQ에서 `...FromISR` | assert port.c:803, 꺼져 있으면 무작위 손상 | 기본 우선순위 0 = 문턱 위 | NVIC 우선순위를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이상(숫자)으로 |
| 크리티컬 섹션 안에서 블로킹 API | 시스템 정지, PC가 `vListInsert`, BASEPRI ≠ 0, PendSV pending | yield가 막혀 리스트 이중 삽입 (예제 11 ③) | 블로킹은 밖으로. 꼭 기다려야 하면 뮤텍스 |
| 스케줄러 잠금 안에서 `vTaskDelay` | assert tasks.c:2517 | 잠긴 채 블록 불가 (예제 11 ①) | 잠금 범위를 줄인다 |
| 크리티컬 섹션 안에서 printf/UART 출력 | 텔레메트리 RX 오버런, 센서 ISR 유실 | 1.39 ms 마스크 (예제 9) | 버퍼에 복사만 하고 출력은 밖에서 (로거 태스크) |
| `main()`에서 태스크 만든 뒤 인터럽트를 기다림 | 스케줄러 시작 전 멈춤 | `uxCriticalNesting = 0xaaaaaaaa` → BASEPRI 0x20 유지 (예제 4) | 인터럽트를 쓰는 초기화는 첫 태스크에서 |
| Zephyr에서 우선순위 0이면 안 막힐 줄 앎 | `irq_lock` 동안 50 µs 지연 | 0도 문턱(0x20) 아래 (예제 12) | `CONFIG_ZERO_LATENCY_IRQS` + `IRQ_ZERO_LATENCY` |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How does FreeRTOS implement a critical section on a Cortex-M4, and why doesn't it just disable all interrupts?"

**A.** 태스크용 `taskENTER_CRITICAL()`은 BASEPRI를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`로 올리고 전역 카운터를 1 올린다. 나올 때 카운터가 0이 되면 BASEPRI를 0으로 내린다. PRIMASK를 안 쓰는 이유는 커널 자료구조를 만질 수 있는 인터럽트(문턱 아래)만 막으면 충분하기 때문이다. 문턱 위의 인터럽트는 커널이 절대 막지 않으므로 모터 제어 같은 ISR의 지연이 커널과 무관해진다. 내가 QEMU에서 재 보니 50 µs 크리티컬 섹션 아래서 kernel-aware IRQ는 50.6 µs, 문턱 위 IRQ는 160 ns였다.

> On ARMv7-M the port raises BASEPRI to configMAX_SYSCALL_INTERRUPT_PRIORITY and increments a nesting counter; on exit, when the counter returns to zero, it writes BASEPRI back to zero. It doesn't use PRIMASK because only interrupts that can touch kernel data structures need to be held off — those are exactly the ones allowed to call FromISR APIs, below the threshold. Anything above the threshold is never masked by the kernel, so a motor-control ISR's latency doesn't depend on how long the kernel or other code sits in a critical section. When I measured it on QEMU, a 50 µs critical section delayed a kernel-aware IRQ by up to 50.6 µs and a zero-latency IRQ by 160 ns. The price is that zero-latency ISRs can't call any RTOS API.

**꼬리 질문**: "What about Cortex-M0?" → BASEPRI가 없어서 ARM_CM0 포트는 `cpsid i`(PRIMASK)를 쓴다. 모든 인터럽트가 커널 크리티컬 섹션을 기다린다.

**Q.** "Why are there separate FromISR versions of the critical section macros?"

**A.** 태스크용은 카운터로 중첩을 세고, 나올 때 무조건 BASEPRI = 0으로 내린다. ISR은 다른 ISR을 선점한 상태일 수 있고, 그 낮은 ISR이 BASEPRI를 올려 둔 상태일 수도 있다. 그런데 0으로 내리면 그 보호를 풀어 버린다. 그래서 ISR용은 이전 BASEPRI 값을 반환하고 나올 때 그 값으로 되돌린다. FreeRTOS는 ISR에서 태스크용을 부르면 ICSR의 VECTACTIVE를 보고 assert를 띄운다.

> The task-level version counts nesting in a global and unconditionally writes BASEPRI to zero when the count hits zero. An ISR can be nested on top of another ISR that has already raised BASEPRI, so dropping it to zero would silently remove the lower ISR's protection. The FromISR version returns the previous BASEPRI and restores exactly that value — save and restore instead of counting. The port actually asserts if you call the task version from Handler mode: vPortEnterCritical checks the VECTACTIVE field of ICSR, and I've seen that assert fire at port.c line 438.

**Q.** "What's the difference between disabling interrupts and suspending the scheduler?"

**A.** 인터럽트를 끄면 ISR과 다른 태스크가 둘 다 못 끼어든다. 스케줄러를 잠그면 ISR은 그대로 돌고 컨텍스트 스위치만 미뤄진다. tick은 `xPendedTicks`에, ISR이 깨운 태스크는 `xPendingReadyList`에 쌓였다가 `xTaskResumeAll`에서 한꺼번에 처리된다. 중요한 건, 높은 우선순위 태스크 입장에서는 둘이 똑같이 L만큼 늦는다는 것이다. 내가 잰 바로는 100 µs 잠금에서 IRQ → 태스크 지연이 셋 다 109 µs 근처였다. 그래서 스케줄러 잠금은 ISR 지연만 지켜 준다. 그리고 ISR과 공유한 데이터는 지키지 못한다.

> Disabling interrupts — or raising BASEPRI — keeps out both ISRs and other tasks. Suspending the scheduler keeps ISRs running and only defers context switches: ticks accumulate in xPendedTicks, tasks readied by ISRs go to xPendingReadyList, and xTaskResumeAll replays both. The subtle point is that, from a high-priority task's point of view, both cost the same: in my sweep, a 100 µs lock delayed the IRQ-to-task path by about 109 µs whether it was PRIMASK, BASEPRI or vTaskSuspendAll. So the scheduler lock protects interrupt latency, not task latency, and it doesn't protect data shared with ISRs at all.

**Q.** "What must you never do inside a critical section?"

**A.** 블로킹 API(큐 대기, delay, 뮤텍스 대기), printf나 UART 출력, 플래시 쓰기, 메모리 할당, 상한 없는 루프다. 블로킹은 특히 위험하다. 크리티컬 섹션 안에서 타임아웃 있는 `xQueueSend`를 부르면 PendSV가 막혀서 yield가 안 되고, tick도 막혀서 타임아웃이 오지 않는다. 그러다 리스트가 이중 삽입으로 깨져 `vListInsert`에서 영원히 돈다. 실험에서 실제로 그렇게 멈췄다. 출력도 금지다. 115200 baud에서 16글자 로그 한 줄이 1.39 ms 마스크라서, 이것 하나로 총 마스크 시간이 0.9%에서 13.5%로 늘었다.

> No blocking calls, no printf or UART output, no flash writes, no allocation, no unbounded loops. Blocking is the nasty one: with BASEPRI raised, the PendSV that would perform the yield is masked and so is the tick, so the call never really blocks and never times out — in my experiment the task ended up re-inserting itself into an event list and spinning forever in vListInsert with BASEPRI still at 0x20 and PendSV pending. Inside a scheduler lock, FreeRTOS catches the same mistake with a configASSERT. Printing is the classic latency killer: one 16-character line at 115200 baud is 1.4 ms with interrupts masked, which took my test system from 0.9% to 13.5% total masked time.

**Q.** "How would you find the longest critical section in an existing firmware?"

**A.** 진입/탈출에 타임스탬프를 찍는다. FreeRTOS라면 링커의 `--wrap=vPortEnterCritical`과 `--wrap=vPortExitCritical`로 소스 수정 없이 감싸고, 깊이가 0 → 1일 때 시작 시각과 리턴 주소를, 1 → 0일 때 길이를 기록해서 히스토그램과 최댓값, 그리고 최댓값의 호출 위치를 남긴다. 인라인인 ISR 쪽 BASEPRI 구간은 trace 훅이나 하드웨어 트레이스로 본다. 측정할 때는 위상을 섞어서 최악의 경우가 실제로 나오게 한다.

> Instrument entry and exit. With FreeRTOS I wrap vPortEnterCritical and vPortExitCritical at link time using --wrap, timestamp on the outermost entry with a cycle counter, record the return address, and on the outermost exit bin the duration into a histogram and keep the max with its call site. That gave me a per-call-site table — every kernel section was under 2 µs in steady state, and the outlier was a logger printing inside its critical section. The inline FromISR sections need trace hooks or ETM. And I randomise the workload phase, because with deterministic timing the worst case can simply never line up.

**Q.** "Is reading a 64-bit timestamp on a Cortex-M3 atomic?"

**A.** 기대하면 안 된다. M3에서는 GCC가 `ldrd`를 쓰기도 하지만, 아키텍처가 보장하는 원자성은 32비트 단위다. M0에서는 `ldr` 두 개다. 그리고 흔한 구조는 하드웨어 타이머에 소프트웨어 상위 워드를 붙이는 방식인데, 이건 인터럽트를 꺼도 찢어진다. 하드웨어 카운터는 끈 상태에서도 넘치기 때문이다. 해법은 hi-lo-hi 재시도 읽기, 또는 끈 상태에서 wrap 플래그를 확인해 보정하는 것이다. 실험에서 그냥 읽으면 wrap 200번에 40번, 끄고 읽어도 20번 시간이 거꾸로 갔고, 보정하니 0이었다.

> Don't rely on it. GCC happens to emit LDRD on the M3, but architectural single-copy atomicity is per word, and on an M0 it's two LDRs. The more common bug is the extended timer — a hardware counter plus a software high word — which tears even with interrupts masked, because the hardware keeps counting and can wrap while the ISR is pending. In my test, naive reads went backwards 40 times in 200 wraps, masked reads still 20 times; reading high–low–high and retrying, or checking the timer's pending flag under the mask, took it to zero.

**Q.** "How do critical sections differ in Zephyr?"

**A.** `irq_lock()`이 key를 반환하고 `irq_unlock(key)`이 그 값으로 되돌리는 저장/복원 방식이다. 카운터가 없고, 태스크와 ISR이 같은 API를 쓴다. ARMv7-M에서는 BASEPRI_MAX로 `_EXC_IRQ_DEFAULT_PRIO`까지 올린다. mps2-an385(3비트)에서 그 값은 0x20이고, ZLI를 켜면 0x40이다. `k_spin_lock`은 UP 빌드에서 그냥 `arch_irq_lock()`이다. `k_sched_lock`은 스레드별 바이트를 내려서 그 스레드를 협력형처럼 만든다. 그래서 잠근 채 블록하면 assert가 나지 않고 그냥 다른 스레드가 돈다. 그리고 Zephyr에서 IRQ 우선순위 0은 zero-latency가 아니다. `CONFIG_ZERO_LATENCY_IRQS`와 `IRQ_ZERO_LATENCY`가 있어야 한다.

> Zephyr uses keys rather than a counter: irq_lock returns the previous BASEPRI — or PRIMASK on Baseline cores — and irq_unlock restores it, so the same API works in threads and ISRs as long as keys are released in reverse order. On ARMv7-M it uses BASEPRI_MAX up to _EXC_IRQ_DEFAULT_PRIO, which I measured as 0x20 on mps2-an385 and 0x40 once zero-latency IRQs are enabled. k_spin_lock on a uniprocessor build is literally arch_irq_lock plus optional validation, and k_sched_lock just decrements a per-thread byte that makes the thread behave as cooperative. One gotcha: priority 0 in IRQ_CONNECT is not zero-latency — it still sits under the lock threshold unless you enable CONFIG_ZERO_LATENCY_IRQS and connect with IRQ_ZERO_LATENCY.

**Q.** "You disable interrupts for 2 ms in a driver. What breaks besides latency?"

**A.** 인터럽트가 사라진다. NVIC pending 비트는 1비트라서, 막힌 동안 같은 인터럽트가 여러 번 오면 하나로 합쳐진다. 실험에서 100 µs 주기 타이머가 2 ms 크리티컬 섹션 동안 20번 울렸는데, 풀린 뒤 ISR은 한 번만 돌았다. 그 타이머로 시간을 세는 코드라면 19 tick을 잃는다. UART RX FIFO는 넘치고, 하드웨어 watchdog을 ISR에서 먹이는 구조라면 리셋까지 갈 수 있다.

> You lose events, not just time. The NVIC pending bit is a single bit, so repeated assertions of the same interrupt while it's masked collapse into one. In my test, a 100 µs timer fired twenty times during a 2 ms critical section, and the ISR ran exactly once afterwards — anything counting those interrupts lost nineteen of them. UART FIFOs overrun, DMA half/complete events merge, and a watchdog fed from an interrupt can reset the system.

---

## 13. 직접 해보기

1. **(개념)** 예제 1의 루프에 명령을 하나 더 넣어(예: `g_dummy++`를 `g_count++` 앞에) 루프를 7명령으로 만들면 잃어버리는 비율은 얼마로 예상되나? 실행해서 확인하라.
   정답/힌트: `ldr`부터 `str` 직전까지의 경계 수 ÷ 루프 경계 수. 새 명령이 창 밖에 들어가면 3/7 ≈ 43%, 창 안이면 더 커진다 — `objdump`로 순서를 먼저 본다.
2. **(코드)** 예제 1을 LDREX/STREX 루프(`__atomic_fetch_add(&g_count, 1, __ATOMIC_RELAXED)` — GCC가 M3에서 LDREX/STREX로 만든다)로 고쳐 lost = 0을 확인하고, 비용(ns/iter)을 PRIMASK 버전과 비교하라.
   정답/힌트: `-mcpu=cortex-m3`에서 `ldrex/strex` 재시도 루프가 생긴다(M0에서는 라이브러리 호출이나 링크 에러). 깊은 설명은 D8.
3. **(코드)** 예제 2의 `now_retry()`를 `irq_save()` 안에서 부르면 무슨 일이 일어나는지 예측하고 실행하라.
   정답/힌트: ISR이 못 돌아 `h1 == h2`가 늘 참 → ②와 같은 찢어짐이 다시 나온다. "락 없는 재시도는 인터럽트가 켜진 문맥 전용"이다.
4. **(측정)** 예제 8에 네 번째 모드 "뮤텍스(`xSemaphoreTake/Give`)"를 추가해, L = 50 µs일 때 hi 태스크(뮤텍스와 무관)의 지연이 얼마인지 재라.
   정답/힌트: hi가 그 뮤텍스를 안 쓰면 L과 무관해야 한다(약 10 µs). 뮤텍스는 "경쟁자만" 기다리게 한다 — 그림 1의 마지막 줄.
5. **(측정)** 예제 9의 나쁜 로거에서 센서 ISR(1 kHz)이 몇 번 사라지는지 세어라. ISR 카운트와 `xTaskGetTickCount()`로 기대한 횟수를 비교한다.
   정답/힌트: 1.39 ms 마스크 동안 1 kHz IRQ가 한 번 이상 들어오면 pending 비트가 합쳐진다. 10 ms마다 한 번씩 최대 1개 정도를 잃는다.
6. **(코드)** Zephyr 예제 12에서 `k_sched_lock()` 안에 `k_msleep(1)`을 넣어 FreeRTOS 예제 11 ①과 결과를 비교하라(`CONFIG_ASSERT=y`로도 한 번).
   정답/힌트: Zephyr는 잠금이 스레드에 붙어 있어 그냥 잠들고 다른 스레드가 돈다(kernel.h 문서). 깨어나면 여전히 잠긴 상태다.
7. **(설계)** 20 kHz FOC ISR(문턱 위)과 1 kHz 속도 루프 태스크가 "목표 전류 d/q + 제한값" 세 필드를 공유한다. 크리티컬 섹션 없이 찢어짐을 없애는 방법을 설계하라.
   정답/힌트: 더블 버퍼 + 인덱스(32비트 한 번 쓰기)로 발행, ISR은 인덱스를 한 번 읽고 그 버퍼만 읽는다. 쓰는 쪽이 ISR보다 빠르게 두 번 바꾸지 않는다는 조건을 확인한다(D8).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| critical section | 크리티컬 섹션 | 한 번에 한 실행 흐름만 들어가야 하는 코드 구간 |
| RMW (read-modify-write) | 읽기-고치기-쓰기 | `x++`처럼 읽고 바꿔 다시 쓰는 연산. 사이에 끼면 갱신을 잃는다 |
| lost update | 잃어버린 갱신 | 두 쪽이 더했는데 한쪽 결과만 남는 것 |
| tearing / torn read | 찢어진 읽기 | 여러 명령으로 읽는 값의 앞뒤 절반이 서로 다른 시점의 값 |
| single-copy atomicity | 단일 복사 원자성 | 한 번의 접근이 쪼개지지 않고 보이는 성질. Cortex-M은 정렬된 워드까지 |
| PRIMASK | 우선순위 마스크 레지스터 | 1이면 NMI · HardFault 외 모든 예외 차단. `cpsid i` / `cpsie i` |
| BASEPRI | 기준 우선순위 레지스터 | 이 값 이상(숫자) 우선순위 차단. ARMv7-M · ARMv8-M Mainline만 |
| BASEPRI_MAX | BASEPRI의 조건부 쓰기 별칭 | 문턱을 올리는 쓰기만 반영 (Zephyr `__set_BASEPRI_MAX`) |
| FAULTMASK | 폴트 마스크 | HardFault까지 차단(NMI만 통과). 크리티컬 섹션에는 안 씀 |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | FreeRTOS 문턱 | 커널이 크리티컬 섹션에서 BASEPRI에 쓰는 값. 이 아래만 FromISR 허용 |
| kernel-aware ISR | 커널 인지 ISR | 문턱 아래 우선순위. FromISR API 사용 가능, 커널 크리티컬 섹션을 기다림 |
| zero-latency ISR | 제로 레이턴시 ISR | 문턱 위 우선순위. 커널이 막지 않음, RTOS API 금지 |
| `uxCriticalNesting` | 중첩 카운터 | ARM_CM3 포트의 전역 깊이. 초기값 0xaaaaaaaa, 스케줄러 시작 때 0 |
| key (Zephyr) | 잠금 키 | `irq_lock`이 돌려준 이전 마스크 값. `irq_unlock(key)`로 되돌림 |
| scheduler lock | 스케줄러 잠금 | ISR은 받고 컨텍스트 스위치만 미룸. `vTaskSuspendAll` / `k_sched_lock` |
| `xPendedTicks` | 밀린 tick | 스케줄러가 잠긴 동안 쌓인 tick 수. ResumeAll에서 몰아 처리 |
| `xPendingReadyList` | 대기 중 ready 목록 | 잠긴 동안 ISR이 깨운 태스크를 잠시 두는 리스트 |
| queue lock (`cTxLock`/`cRxLock`) | 큐 잠금 | 커널이 큐 리스트 작업 중일 때 ISR 요청을 개수로 미루는 장치 |
| pending bit | 대기 비트 | 막힌 동안 들어온 IRQ를 기억하는 NVIC 1비트. 여러 번 와도 1번 |
| VECTACTIVE | 실행 중 예외 번호 | ICSR(0xE000ED04) 아래 8비트. 0이 아니면 Handler 모드 |
| `--wrap` | 링커 감싸기 옵션 | `sym` 호출을 `__wrap_sym`으로 돌리고 원본은 `__real_sym` |
| aliasing (phase lock) | 위상 고정 | 결정적 타이밍에서 주기가 맞물려 최악 경우가 안 나오는 현상 |

---

## 15. 요약 & 체크리스트

크리티컬 섹션은 "한 번에 한 명"을 하드웨어로 만드는 일이고, Cortex-M에서 도구는 네 단계다. **PRIMASK**(`cpsid i`)는 모든 인터럽트를 막고, 저장/복원으로 중첩을 지켜야 한다(무조건 `cpsie`는 바깥 보호를 지운다 — 예제 3에서 3660개 유실). **BASEPRI**는 문턱 아래만 막는다. FreeRTOS ARM_CM3 포트는 태스크에서는 BASEPRI = `configMAX_SYSCALL_INTERRUPT_PRIORITY` + 전역 카운터(초기 0xaaaaaaaa라 스케줄러 시작 전엔 열리지 않는다), ISR에서는 인라인 저장/복원(`taskENTER_CRITICAL_FROM_ISR`)을 쓴다. 잘못 쓰면 port.c:438과 port.c:803의 assert가 잡는다. ARM_CM0 포트는 BASEPRI가 없어 PRIMASK뿐이다. **스케줄러 잠금**은 ISR을 막지 않지만, tick과 깨우기를 `xPendedTicks` · `xPendingReadyList`에 미뤘다가 풀 때 처리하므로 **높은 태스크는 똑같이 L만큼 늦는다**(예제 7: 7 µs → 4098 µs). **뮤텍스**(D2)는 경쟁자만 기다리게 한다. 측정(예제 8)으로 보면 최대 IRQ 지연 ≈ 기본 지연 + 그 IRQ를 막는 가장 긴 L이다. PRIMASK는 모두에게, BASEPRI는 kernel-aware에게만 L을 더하고, zero-latency는 160 ns로 평평하다. 태스크 지연은 셋 다 L + 10 µs다. 실제 워크로드(예제 9)에서 커널 자신의 크리티컬 섹션은 2 µs 미만이었다. 로그 한 줄을 안에서 출력하면 최대 1.39 ms, 총 마스크 시간 13.5%가 됐다. 안에서 블로킹하면 스케줄러 잠금에서는 assert, BASEPRI에서는 `vListInsert` 무한 루프로 멈춘다(예제 11). Zephyr는 key 방식(`irq_lock`/`irq_unlock`)을 쓰고, `k_spin_lock`은 UP 빌드에서 곧 irq lock이다. 우선순위 0이 zero-latency가 되려면 `CONFIG_ZERO_LATENCY_IRQS`가 필요하다(예제 12).

- [ ] `x++`가 왜 레이스인지 LDR/ADD/STR로 설명하고, 레이스 확률을 "창 ÷ 루프"로 계산할 수 있다
- [ ] 하드웨어 타이머 + 소프트웨어 상위 워드 시계가 인터럽트를 꺼도 찢어지는 이유와 두 가지 해법을 말할 수 있다
- [ ] PRIMASK 저장/복원 패턴을 쓰고, 무조건 `cpsie`의 중첩 버그를 설명할 수 있다
- [ ] FreeRTOS ARM_CM3 `vPortEnterCritical`/`vPortExitCritical`/`ulPortRaiseBASEPRI`의 동작을 소스 수준으로 설명할 수 있다
- [ ] `uxCriticalNesting = 0xaaaaaaaa`가 스케줄러 시작 전 인터럽트를 막는 이유를 안다
- [ ] 태스크용과 FromISR용 크리티컬 섹션이 왜 따로 있는지(카운터 vs 저장/복원) 말할 수 있다
- [ ] ARM_CM0 포트가 PRIMASK만 쓰는 이유와 그 결과(zero-latency ISR 없음)를 안다
- [ ] `vTaskSuspendAll` 동안 tick · 깨우기가 어디에 쌓이고 언제 처리되는지 설명할 수 있다
- [ ] "스케줄러 잠금은 ISR 지연만 지킨다"를 측정 숫자로 말할 수 있다
- [ ] 크리티컬 섹션 길이를 `--wrap`으로 계측해 히스토그램과 최댓값 호출 위치를 낼 수 있다
- [ ] 크리티컬 섹션 안 블로킹이 멈춤으로 이어지는 경로(PendSV 막힘 → 리스트 이중 삽입)를 설명할 수 있다
- [ ] Zephyr `irq_lock` key 방식, `k_spin_lock`(UP), `k_sched_lock`, ZLI 설정을 FreeRTOS와 대응시킬 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM J2 | 메인 ↔ ISR 데이터 레이스 (tearing, RMW, PRIMASK 저장/복원, BASEPRI) | §1 (예제 1 · 2 · 1.4 · 1.5 표), §2 (예제 3), §3.1 | 이 노트를 링크하고, RTOS 없는 슈퍼 루프에서 CMSIS `__get_PRIMASK`/`__set_PRIMASK`만으로 같은 패턴을 쓰는 짧은 코드와 "BASEPRI를 bare-metal에서 쓰면 무엇이 좋은가"(모터 ISR만 남기기) 한 문단 |
| BM J3 | 락 없는 링버퍼 (SPSC, 2의 거듭제곱, 가득/빔, 배리어) | §1.3 ④(hi-lo-hi 재시도), §5.2 결정표의 "zero-latency ISR과 공유" 행, §10.2 | 링버퍼 자체는 RTOS D8이 주인 — Bare-Metal은 D8을 링크하고 화이트보드용 최소 구현만 |
| BM J4 | 원자 연산 (LDREX/STREX · CLREX, C11 atomics, M0 대체) | §1.4(M3 `ldrd` vs M0 `ldr` 두 개), §3.6(M0 = PRIMASK), §13 연습 2 | LDREX/STREX 깊은 설명은 RTOS D8 — Bare-Metal은 링크와 "M0에서는 인터럽트 잠금으로 대체"(이 노트 §3.6) 한 줄 |

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (`/var/rtoslab/src/FreeRTOS-Kernel`): `portable/GCC/ARM_CM3/portmacro.h`(`vPortRaiseBASEPRI`, `ulPortRaiseBASEPRI`, `vPortSetBASEPRI`), `portable/GCC/ARM_CM3/port.c`(`uxCriticalNesting` line 141, `vPortEnterCritical`/`vPortExitCritical` line 426 ~ 452, `vPortValidateInterruptPriority` line 766, `xPortSysTickHandler`, PendSV의 BASEPRI line 472 ~ 475), `portable/GCC/ARM_CM0/portmacro.h` · `portasm.c`(`ulSetInterruptMask`, `vClearInterruptMask`), `include/task.h`(`taskENTER_CRITICAL` 등 매크로), `tasks.c`(`vTaskSuspendAll` line 3890, `xTaskResumeAll` line 4064, `xTaskRemoveFromEventList` line 5500, `vTaskDelay` line 2506), `queue.c`(`prvLockQueue` line 258, `xQueueGenericSend` line 949), `list.c`(`vListInsert`의 NOTE 주석)
- FreeRTOS 문서: "Running the RTOS on a ARM Cortex-M Core" — https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html (port.c 주석이 가리키는 페이지), 설정 문서 `configMAX_SYSCALL_INTERRUPT_PRIORITY` · `configASSERT` — https://www.FreeRTOS.org/a00110.html
- Zephyr v4.4.2 소스 (`/var/rtoslab/zephyrproject/zephyr`): `include/zephyr/arch/arm/asm_inline_gcc.h`(`arch_irq_lock`/`arch_irq_unlock`), `include/zephyr/arch/arm/cortex_m/exception.h`(`_EXC_IRQ_DEFAULT_PRIO`, `_IRQ_PRIO_OFFSET`), `include/zephyr/irq.h`(`irq_lock` 문서), `include/zephyr/spinlock.h`(`k_spin_lock`), `kernel/sched.c`(`k_sched_lock`), `include/zephyr/kernel.h`(`k_sched_lock` 문서), `include/zephyr/kernel/thread.h`(`sched_locked`/`prio`/`preempt`), `arch/arm/core/cortex_m/Kconfig`(`ZERO_LATENCY_IRQS`, `ZERO_LATENCY_LEVELS`), `include/zephyr/arch/arm/irq.h`(`IRQ_ZERO_LATENCY`, `IRQ_DIRECT_CONNECT` 요구 BUILD_ASSERT), `drivers/timer/cortex_m_systick.c`(`elapsed()`의 COUNTFLAG 보정), `kernel/Kconfig`(`WAITQ_SIMPLE`/`WAITQ_SCALABLE`)
- Zephyr 문서: Interrupts — https://docs.zephyrproject.org/latest/kernel/services/interrupts.html (zero-latency interrupts, `irq_lock`)
- Arm, "ARMv7-M Architecture Reference Manual" (DDI 0403) — PRIMASK · BASEPRI · BASEPRI_MAX · FAULTMASK 정의, 단일 복사 원자성. Arm, "ARMv6-M Architecture Reference Manual" (DDI 0419) — BASEPRI 없음
- Arm CMSIS-Core: `__disable_irq`, `__enable_irq`, `__get_PRIMASK`, `__set_PRIMASK`, `__set_BASEPRI_MAX` — https://arm-software.github.io/CMSIS_6/latest/Core/index.html
- GNU ld 문서 `--wrap=symbol` — https://sourceware.org/binutils/docs/ld/Options.html
- 이 노트의 선행 · 연결 노트: RTOS A7(하이브리드 측정 · zero-latency 160 ns), RTOS B3(NVIC · BASEPRI 하드웨어), RTOS D2(뮤텍스), RTOS D8(락 없는 통신), EL F4(Linux locking — `spin_lock_irqsave`)
