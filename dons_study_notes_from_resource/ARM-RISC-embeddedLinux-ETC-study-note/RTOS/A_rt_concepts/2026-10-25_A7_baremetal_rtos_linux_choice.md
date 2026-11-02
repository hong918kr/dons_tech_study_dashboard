# A7. bare-metal vs RTOS vs Linux vs 하이퍼바이저 — 이 MCU(이 코어)에 무엇을 올리나, 같은 보드에서 잰 비용으로 고른다

> **이 노트를 다 읽으면**: 같은 Cortex-M3 보드에서 bare-metal · FreeRTOS · Zephyr의 부팅 시간 · 인터럽트 지연 · 컨텍스트 스위치 · tick 비용 · RAM을 직접 재서 숫자로 비교할 수 있다 · "슈퍼 루프가 언제 무너지고 RTOS가 언제 이기는지"를 측정 결과로 설명할 수 있다 · 가장 빡빡한 루프만 zero-latency ISR로 빼고 나머지를 RTOS 태스크로 두는 하이브리드를 설계할 수 있다 · 드론 FC · ESC · 센서 허브 · SSD 컨트롤러에서 각각 왜 그 선택을 했는지 말할 수 있다
> **왜 필요한가**: 시스템 설계 면접의 단골 질문 "bare-metal로 하겠나, RTOS로 하겠나, Linux로 하겠나?"에 "경우에 따라 다르다"가 아니라 **"이 칩에서 RTOS가 깨우는 데 몇 µs, tick이 CPU 몇 %, 태스크 하나에 RAM 몇 바이트 — 그래서 이 루프는 ISR, 나머지는 태스크"**로 답해야 통과한다
> **Don 기준 난이도**: 인터럽트 · 우선순위 · 슈퍼 루프 · SSD 실행부(executive)는 이미 잘 안다 / 새로 배울 부분은 RTOS가 실제로 얼마를 받아 가는지(측정), 설정 하나(time slicing, 디버그 옵션, heap 크기)가 그 값을 몇 배 바꾼다는 사실, 하이퍼바이저가 MCU급 코어에 들어오는 지점
> **선행 노트**: A1(실시간 정의), A2(실행 모델의 진화), EL N1 · N3 · N4(Linux 쪽 판단과 측정). 이어지는 노트: A3(구성 요소 · footprint 자세히), B3 · E2(인터럽트 우선순위 설계), C2 · C5(컨텍스트 스위치 · tick 내부), O6 · P6(분할 · 하이퍼바이저)

---

## 0. 큰 그림 — "RTOS를 쓸까?"가 아니라 "이 코어의 일을 누가 고르나?"

Don이 SSD 컨트롤러 펌웨어에서 쓰던 구조를 떠올려 보자. Cortex-R 코어마다 실행부(executive)가 있었고, 그 실행부는 "하드웨어 큐에 뭐가 들어왔나 → 해당 핸들러를 끝까지 실행 → 다음"을 반복했다. FreeRTOS도 Zephyr도 없었다. 그런데 같은 회사의 다른 제품(예: 관리용 마이크로컨트롤러)에는 RTOS가 들어가 있었을 수도 있다. 둘 다 맞는 선택이다. 차이는 **"다음에 무엇을 실행할지 누가 고르나"**와 **"그 결정에 몇 사이클을 쓸 수 있나"**다.

한 코어(한 MCU) 위에서 고를 수 있는 답은 네 가지다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="8.0" y="20.0" width="160.0" height="210.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="88.0" y="83.0" font-size="13" text-anchor="middle" font-weight="bold">bare-metal</text>
<text x="88.0" y="101.0" font-size="12" text-anchor="middle">main 루프가 스케줄러</text>
<text x="88.0" y="119.0" font-size="12" text-anchor="middle">NVIC 우선순위 = 선점</text>
<text x="88.0" y="137.0" font-size="12" text-anchor="middle">IRQ→처리 0.56 µs</text>
<text x="88.0" y="155.0" font-size="12" text-anchor="middle">(루프가 한가할 때만)</text>
<text x="88.0" y="173.0" font-size="12" text-anchor="middle">예: ESC · Betaflight</text>
<rect x="176.0" y="20.0" width="160.0" height="210.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="256.0" y="83.0" font-size="13" text-anchor="middle" font-weight="bold">RTOS</text>
<text x="256.0" y="101.0" font-size="12" text-anchor="middle">커널이 우선순위로 고름</text>
<text x="256.0" y="119.0" font-size="12" text-anchor="middle">태스크마다 스택</text>
<text x="256.0" y="137.0" font-size="12" text-anchor="middle">IRQ→태스크 5~19 µs</text>
<text x="256.0" y="155.0" font-size="12" text-anchor="middle">(바쁜 하위 일과 무관)</text>
<text x="256.0" y="173.0" font-size="12" text-anchor="middle">예: PX4 · 센서 허브</text>
<rect x="344.0" y="20.0" width="160.0" height="210.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="424.0" y="83.0" font-size="13" text-anchor="middle" font-weight="bold">Linux</text>
<text x="424.0" y="101.0" font-size="12" text-anchor="middle">MMU + 프로세스</text>
<text x="424.0" y="119.0" font-size="12" text-anchor="middle">생태계 · 드라이버 최고</text>
<text x="424.0" y="137.0" font-size="12" text-anchor="middle">부팅 수 초, 꼬리 ms</text>
<text x="424.0" y="155.0" font-size="12" text-anchor="middle">(EL N1 · N4)</text>
<text x="424.0" y="173.0" font-size="12" text-anchor="middle">예: 컴패니언 컴퓨터</text>
<rect x="512.0" y="20.0" width="160.0" height="210.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="592.0" y="83.0" font-size="13" text-anchor="middle" font-weight="bold">하이퍼바이저 분할</text>
<text x="592.0" y="101.0" font-size="12" text-anchor="middle">코어 · 메모리를 나눠 줌</text>
<text x="592.0" y="119.0" font-size="12" text-anchor="middle">파티션마다 OS 하나</text>
<text x="592.0" y="137.0" font-size="12" text-anchor="middle">중요도 섞기(혼합)</text>
<text x="592.0" y="155.0" font-size="12" text-anchor="middle">(O6 · P6)</text>
<text x="592.0" y="173.0" font-size="12" text-anchor="middle">예: Cortex-R52 EL2</text>
<line x1="20.0" y1="255.0" x2="660.0" y2="255.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah1)"/>
<line x1="660.0" y1="255.0" x2="20.0" y2="255.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah1)"/>
<text x="20.0" y="280.0" font-size="12">결정성 · 단순함 · 작은 RAM</text>
<text x="660.0" y="280.0" font-size="12" text-anchor="end">생태계 · 격리 · 기능</text>
</svg>
```

그림 1 — 한 코어 위의 네 가지 답. 왼쪽으로 갈수록 결정성과 단순함, 오른쪽으로 갈수록 생태계와 격리 기능을 얻는다. 가운데 숫자는 이 노트에서 직접 잰 값이다.

이 노트는 **MCU 쪽 판단**을 다룬다. "이 MCU(또는 SoC 안 이 코어)에서 bare-metal로 쓸까, RTOS를 올릴까(어떤 것), Linux가 필요한가, 하이퍼바이저로 나눌까?" 그리고 그 판단을 감이 아니라 **같은 가상 보드에서 잰 숫자**로 한다.

### EL N4가 이미 정한 것 (Linux 쪽 판단 — 다시 쓰지 않는다)

EL N4는 "기능 하나를 Linux / RT 코어 / 외부 MCU / safety MCU 중 어디에 두나"를 다뤘다. 요약만 적는다.

- 판단 기준: deadline과 지터, 놓쳤을 때의 결과, 안전 인증, 부팅 시간, 전원(always-on), 장애 격리, 생태계, 비용 · OTA · 조직.
- 질문 순서: 하드웨어 파손 → 안전 → 생존성(Linux가 죽어도?) → 지연 → 나머지. 흐름도는 기능 단위로 적용.
- 다섯 분할 패턴: PREEMPT_RT 단독, 외부 MCU, 이종 SoC(remoteproc/rpmsg), hypervisor 분할, dual-kernel(Xenomai).
- 경계 설계: 프레임 · CRC · seq · 타임스탬프 · 시간 동기 · heartbeat · failsafe · 버전.
- 원칙: 루프는 그 deadline을 보장하는 층에서 닫고, Linux는 setpoint만 준다.

**이 노트가 새로 하는 것**: 그렇게 MCU로 내려온 일을 **그 MCU 안에서 어떻게 돌릴지** 정한다. 그리고 그 비용을 잰다.

### 실습 환경 한 줄

- QEMU `mps2-an385` = Cortex-M3 @ 25 MHz 가상 보드. 같은 보드 위에 **bare-metal**(lab 템플릿) · **FreeRTOS V11.3.2**(ARM_CM3 포트) · **Zephyr v4.4.2**(`mps2/an385`)를 올리고, 같은 인터럽트(TIMER0, IRQ 8)와 같은 시계로 잰다.
- 모든 타이밍은 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간)로 돌렸다. 그래서 실행할 때마다 **같은 숫자**가 나온다(결정적).
- **주의 — 숫자의 의미**: QEMU icount 기준이다. 실제 칩과 절대값은 다르다. 캐시, 플래시 wait state, 버스, 예외 진입 하드웨어 스태킹(Cortex-M3는 최소 12사이클)을 모델링하지 않는다. 이 노트가 숫자로 보여 주려는 것은 **상대 비교**(bare-metal 대비 RTOS가 몇 배, 설정 하나로 몇 배)와 **모양**(바쁜 슈퍼 루프에서는 지연이 일의 길이를 따라가고, RTOS에서는 일정)이다.
- Linux 숫자는 다시 재지 않고 EL N1 · N3 · N4의 실측을 인용한다. 덧붙여 VM 호스트(Lima `elinux`, aarch64, 커널 7.0.0-38-generic)에서 스레드 전환과 1 kHz 깨어남 지연만 가볍게 쟀다(§9). VM 기준이다.

---

## 1. 네 가지 답 — 각각 "누가 다음 일을 고르나"

실행 모델이 어떻게 진화했는지(super-loop → foreground/background → 협력형 → 선점형 → async)는 A2에서 다뤘다. 여기서는 **선택지로서** 한 줄씩만 정리한다.

| 선택지 | 다음 일을 고르는 주체 | 동시성 수단 | 메모리 보호 | 대표 크기 |
|---|---|---|---|---|
| bare-metal 슈퍼 루프 | `main()` 안의 코드 순서 | ISR(NVIC 우선순위로 선점) + 플래그 | 없음(MPU를 직접 쓰면 가능) | 수 KB |
| bare-metal + 협력형 스케줄러 | 내가 짠 시간표(예: Betaflight scheduler) | ISR + 끝까지 실행되는(run-to-completion) 함수 | 없음 | 수~수십 KB |
| RTOS (FreeRTOS · Zephyr · NuttX …) | 커널의 우선순위 스케줄러 | 태스크(각자 스택) + ISR + IPC | 선택(MPU, Zephyr userspace) | 수~수십 KB 커널 |
| Linux | 커널 스케줄러(EEVDF + RT 클래스) | 프로세스 · 스레드 | MMU, 프로세스 격리 | MB 단위 |
| 하이퍼바이저 / 분리 커널 | 정적 파티션 표(코어 · 메모리 · 장치를 나눠 줌) | 파티션마다 OS 하나 | 2단계 MMU/MPU | 하이퍼바이저 자체는 작다 |

여기서 중요한 사실 하나: **"bare-metal"이라고 해서 스케줄러가 없는 게 아니다.** 슈퍼 루프의 코드 순서, NVIC 우선순위, 플래그 검사 위치가 곧 스케줄러다. 차이는 그 스케줄러가 **선점을 하지 않는다**는 점(ISR 빼고)이다. 이 차이가 실제로 어떤 숫자를 만드는지 §3에서 잰다.

또 하나: 이 다섯 줄은 **섞인다**. 실제 제품은 "bare-metal ISR로 20 kHz 전류 루프 + RTOS 태스크로 CAN · 진단"(§7) 또는 "Cortex-R52 위 하이퍼바이저 + 파티션마다 RTOS"(§11)처럼 혼합이 기본이다.

---

## 2. 측정 준비 — 같은 보드, 같은 IRQ, 같은 시계

세 실행 환경을 공정하게 비교하려면 세 가지가 같아야 한다.

1. **같은 인터럽트 원천**: CMSDK TIMER0 (0x40000000, IRQ 8). 주기 인터럽트를 내고, 핸들러에서 "타이머가 0을 친 뒤 몇 사이클 지났나"(`RELOAD - VALUE`)를 읽으면 그 자체가 지연 측정이다. scope로 GPIO를 재는 것과 같은 원리인데, 타이머가 스스로 스톱워치 역할을 한다.
2. **같은 시계**: lab의 `board_now_us()`는 `board_init()`에서 시작하므로 reset 이후 시간을 못 잰다. 그리고 Zephyr에는 그 함수가 없다. 그래서 **reset부터 세는 하드웨어 카운터**를 찾았다.
3. **같은 실행 조건**: 모두 `ICOUNT=1`.

### 예제 1 — reset부터 세는 25 MHz 카운터 찾기

무엇을 확인하나: MPS2 보드의 FPGA IO 블록(0x40028000)에 있는 `COUNTER` 레지스터(오프셋 0x18)가 reset부터 25 MHz로 세는지, 그리고 ICOUNT에서 결정적인지 본다. QEMU에는 DWT CYCCNT가 없어서 이게 사이클 카운터 대용이다.

```c
#include "board.h"
#define FPGAIO 0x40028000u
int main(void)
{
    uint32_t c0 = REG32(FPGAIO + 0x18), p0 = REG32(FPGAIO + 0x1C), h100 = REG32(FPGAIO + 0x14);
    uint32_t t0 = board_now_us();
    for (volatile int i = 0; i < 100000; i++) { }
    uint32_t c1 = REG32(FPGAIO + 0x18), t1 = board_now_us();
    board_printf_raw("counter at main=%u prescale=%u clk100=%u now_us=%u\n", c0, p0, h100, t0);
    board_printf_raw("after loop: dcounter=%u dus=%u\n", c1 - c0, t1 - t0);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/rtos/A7 && make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A7/probe.c OUT=/var/tmp/rtos/A7/probe && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/probe/app.elf; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/probe/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4288	     96	    328	   4712	   1268	/var/tmp/rtos/A7/probe/app.elf
counter at main=370 prescale=0 clk100=0 now_us=7
after loop: dcounter=480015 dus=19200
[board_exit] code=0
counter at main=370 prescale=0 clk100=0 now_us=7
after loop: dcounter=480015 dus=19200
[board_exit] code=0
```

출력에서 볼 것: 19200 µs 동안 카운터가 480015 늘었다 → 정확히 **25 카운트/µs = 25 MHz**. `main()`에 들어왔을 때 이미 370(= 14.8 µs)이었으니 reset부터 센다. 두 번 돌려도 숫자가 똑같다(icount의 결정성). 이제 이 레지스터를 `cyc()`라고 부르고 1 cyc = 40 ns로 쓴다.

### 측정 도우미 헤더 `a7.h`

bare-metal과 FreeRTOS 예제가 같이 쓰는 헤더다. Zephyr 예제는 같은 레지스터를 직접 읽는다.

```c
/* a7.h — A7 measurement helpers for QEMU mps2-an385 (bare-metal + FreeRTOS) */
#ifndef A7_H
#define A7_H
#include "board.h"
#define FPGA_COUNTER  REG32(0x40028018u)          /* 25 MHz counter, starts at reset */
static inline uint32_t cyc(void) { return FPGA_COUNTER; }   /* 1 cyc = 40 ns */
#define CYC_NS(c)     ((unsigned)((c) * 40u))

/* TIMER0 (IRQ 8): periodic interrupt every 'period' cycles at NVIC priority 'prio' */
static inline void timer0_start(uint32_t period, uint8_t prio)
{
    TIMER_CTRL(TIMER0_BASE)   = 0;
    TIMER_RELOAD(TIMER0_BASE) = period - 1;
    TIMER_VALUE(TIMER0_BASE)  = period - 1;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    NVIC_IPR(8) = prio;
    NVIC_ISER0  = 1u << 8;
    TIMER_CTRL(TIMER0_BASE)   = 1u | 8u;         /* enable + IRQ enable */
}
static inline void timer0_stop(void) { TIMER_CTRL(TIMER0_BASE) = 0; }
/* cycles since TIMER0 last hit zero (= since the IRQ was raised) */
static inline uint32_t timer0_elapsed(void)
{
    return TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE);
}

typedef struct { uint32_t n, min, max; uint64_t sum; } stat_t;
static inline void stat_add(stat_t *s, uint32_t v)
{
    if (s->n == 0 || v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->sum += v; s->n++;
}
static inline void stat_print(const char *name, const stat_t *s)
{
    board_printf_raw("%s n=%u min=%u avg=%u max=%u ns\n", name, s->n,
                     CYC_NS(s->min), CYC_NS((uint32_t)(s->sum / s->n)), CYC_NS(s->max));
}
#endif
```

`timer0_elapsed()`가 핵심이다. TIMER0은 0을 치는 순간 인터럽트를 올리고 RELOAD 값으로 다시 장전된다. 그러니 어느 코드에서든 `RELOAD - VALUE`를 읽으면 "인터럽트가 걸린 뒤 지금까지 몇 사이클"이 나온다. ISR 첫 줄에서 읽으면 **IRQ → ISR 지연**, 태스크가 깨어난 직후 읽으면 **IRQ → 태스크 지연**이다.

---

## 3. 인터럽트 → 처리 코드까지 — 슈퍼 루프가 무너지는 지점

### 3.1 직관

인터럽트 자체(IRQ → ISR)는 bare-metal이든 RTOS든 하드웨어가 처리한다. 차이는 **ISR 다음**이다. 실제 일(필터, 제어 계산)은 보통 ISR 밖에서 한다(ISR은 짧게 — E4). 그 "밖"이 어디냐에 따라 지연이 갈린다.

- bare-metal: ISR이 플래그를 세우고, **슈퍼 루프가 그 플래그를 볼 때** 일을 한다. 루프가 다른 일(300 µs짜리 통신 처리) 중이면 그게 끝날 때까지 기다린다.
- RTOS: ISR이 세마포어/통지를 주고(`...FromISR`), 그 태스크가 지금 도는 태스크보다 우선순위가 높으면 **ISR이 끝나자마자 커널이 그 태스크로 전환**한다(PendSV). 하위 태스크의 300 µs 일은 중간에 밀린다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="60.0" font-size="13" font-weight="bold">bare-metal</text>
<text x="10.0" y="78.0" font-size="12">슈퍼 루프</text>
<text x="10.0" y="180.0" font-size="13" font-weight="bold">RTOS</text>
<text x="10.0" y="198.0" font-size="12">선점형 커널</text>
<rect x="130.0" y="45.0" width="38.2" height="30.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<rect x="168.2" y="45.0" width="382.5" height="30.0" rx="2" fill="#888" fill-opacity="0.25" stroke="#888"/>
<text x="359.5" y="65.0" font-size="12" text-anchor="middle">다른 일 300 µs (필터 · 통신)</text>
<rect x="550.8" y="45.0" width="25.5" height="30.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="576.2" y="45.0" width="63.8" height="30.0" rx="2" fill="#888" fill-opacity="0.25" stroke="#888"/>
<line x1="206.5" y1="30.0" x2="206.5" y2="45.0" stroke="#d0564a" stroke-width="2" marker-end="url(#ah2)"/>
<text x="206.5" y="26.0" font-size="12" text-anchor="middle">IRQ</text>
<rect x="206.5" y="40.0" width="4.0" height="40.0" rx="0" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<line x1="206.5" y1="92.0" x2="550.8" y2="92.0" stroke="#d0564a" stroke-width="1" marker-end="url(#ah2)"/>
<text x="378.6" y="110.0" font-size="12" text-anchor="middle">플래그를 볼 때까지 최대 300 µs (측정 max 297.8 µs)</text>
<text x="563.5" y="40.0" font-size="12" text-anchor="middle">처리</text>
<rect x="130.0" y="165.0" width="76.5" height="30.0" rx="2" fill="#888" fill-opacity="0.25" stroke="#888"/>
<rect x="206.5" y="160.0" width="4.0" height="40.0" rx="0" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<rect x="209.1" y="165.0" width="25.5" height="30.0" rx="2" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<rect x="234.6" y="165.0" width="405.4" height="30.0" rx="2" fill="#888" fill-opacity="0.25" stroke="#888"/>
<text x="437.3" y="185.0" font-size="12" text-anchor="middle">하위 우선순위 일은 밀렸다가 이어서 실행</text>
<line x1="206.5" y1="150.0" x2="206.5" y2="165.0" stroke="#d0564a" stroke-width="2" marker-end="url(#ah2)"/>
<text x="206.5" y="146.0" font-size="12" text-anchor="middle">IRQ</text>
<line x1="209.1" y1="205.0" x2="209.1" y2="214.0" stroke="#3f9a6b" stroke-width="1"/>
<text x="209.1" y="230.0" font-size="12">ISR → PendSV → 제어 태스크: 항상 6.9~8.3 µs (FreeRTOS 측정)</text>
<line x1="130.0" y1="255.0" x2="640.0" y2="255.0" stroke="currentColor" stroke-width="1"/>
<line x1="130.0" y1="255.0" x2="130.0" y2="260.0" stroke="currentColor" stroke-width="1"/>
<text x="130.0" y="275.0" font-size="12" text-anchor="middle">0 µs</text>
<line x1="257.5" y1="255.0" x2="257.5" y2="260.0" stroke="currentColor" stroke-width="1"/>
<text x="257.5" y="275.0" font-size="12" text-anchor="middle">100 µs</text>
<line x1="385.0" y1="255.0" x2="385.0" y2="260.0" stroke="currentColor" stroke-width="1"/>
<text x="385.0" y="275.0" font-size="12" text-anchor="middle">200 µs</text>
<line x1="512.5" y1="255.0" x2="512.5" y2="260.0" stroke="currentColor" stroke-width="1"/>
<text x="512.5" y="275.0" font-size="12" text-anchor="middle">300 µs</text>
<line x1="640.0" y1="255.0" x2="640.0" y2="260.0" stroke="currentColor" stroke-width="1"/>
<text x="640.0" y="275.0" font-size="12" text-anchor="middle">400 µs</text>
<text x="130.0" y="295.0" font-size="12">회색 = 다른 일, 빨강 = ISR, 파랑/초록 = IRQ에 반응하는 코드</text>
</svg>
```

그림 2 — 같은 IRQ, 같은 300 µs짜리 "다른 일". 슈퍼 루프는 플래그를 일이 끝난 뒤에야 보고, 선점형 RTOS는 ISR 직후 제어 태스크로 넘어간다. 숫자는 아래 예제 2 · 3의 실측이다.

### 예제 2 — bare-metal: 부팅 시간, IRQ → ISR, ISR → 슈퍼 루프

무엇을 확인하나: 1 ms 주기 TIMER0 인터럽트 200번에 대해 (a) ISR 첫 줄까지, (b) 슈퍼 루프가 플래그를 보고 처리할 때까지를 잰다. 루프가 놀 때와 300 µs짜리 다른 일을 할 때를 비교한다. reset → `main()` 시간도 같이 찍는다.

```c
/* ex1 — bare-metal: boot time, IRQ entry latency, ISR -> main-loop flag latency */
#include "a7.h"
#define N 200
static volatile uint32_t flag, isr_lat;
static stat_t s_isr, s_flag_idle, s_flag_busy;

void TIMER0_IRQHandler(void)
{
    isr_lat = timer0_elapsed();          /* first thing: how late is the ISR? */
    TIMER_INTCLR(TIMER0_BASE) = 1;
    flag = 1;                            /* "work pending" for the super-loop */
}

static void busy_work_us(uint32_t us)    /* other super-loop jobs (filters, comms...) */
{
    uint32_t t0 = cyc();
    while (cyc() - t0 < us * 25u) { }
}

static void run(stat_t *s, uint32_t other_work_us)
{
    for (uint32_t got = 0; got < N; ) {
        if (flag) {                      /* super-loop polls the flag */
            uint32_t lat = timer0_elapsed();
            flag = 0;
            stat_add(&s_isr, isr_lat);
            stat_add(s, lat);
            got++;
        }
        if (other_work_us) busy_work_us(other_work_us);
    }
}

int main(void)
{
    uint32_t t_main = cyc();             /* reset -> first line of main */
    timer0_start(25000 + 37, 0x40);      /* ~1 ms, odd period so phases drift */
    run(&s_flag_idle, 0);                /* main loop only polls */
    run(&s_flag_busy, 300);              /* main loop also does 300 us of other work */
    timer0_stop();
    board_printf_raw("boot: reset->main = %u ns\n", CYC_NS(t_main));
    stat_print("IRQ->ISR first line       ", &s_isr);
    stat_print("ISR->main loop (idle loop)", &s_flag_idle);
    stat_print("ISR->main loop (300us job)", &s_flag_busy);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A7/ex1_bm.c OUT=/var/tmp/rtos/A7/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex1/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   5520	     96	    408	   6024	   1788	/var/tmp/rtos/A7/ex1/app.elf
boot: reset->main = 15600 ns
IRQ->ISR first line        n=400 min=80 avg=80 max=120 ns
ISR->main loop (idle loop) n=200 min=520 avg=560 max=640 ns
ISR->main loop (300us job) n=200 min=760 avg=145120 max=297800 ns
[board_exit] code=0
```

출력에서 볼 것:

- **IRQ → ISR 80 ns**: 명령 두세 개. QEMU가 예외 진입 스태킹을 0사이클로 처리하기 때문이다. 실제 Cortex-M3는 zero-wait 메모리에서 최소 12사이클(25 MHz면 480 ns)이 든다. 이 숫자는 "세 환경의 차이"를 보는 기준선으로만 쓴다.
- **루프가 놀 때 0.56 µs**: 슈퍼 루프는 사실 매우 빠르다. 루프가 플래그만 보고 있으면 RTOS 어떤 것보다 빠르다.
- **루프가 300 µs 일을 할 때 평균 145 µs, 최대 297.8 µs**: 지연이 **다른 일의 길이를 그대로 따라간다**. 평균이 대략 그 절반인 것도 당연하다(IRQ가 일의 아무 시점에나 떨어지니까). 이게 슈퍼 루프의 본질적 약점이다: **최악 지연 = 루프 안에서 가장 긴 일 하나**. 새 기능이 루프에 추가될 때마다 모든 루프의 최악 지연이 바뀐다.

### 예제 3 — FreeRTOS: 같은 측정, 태스크 통지로 깨우기

무엇을 확인하나: 같은 TIMER0 ISR에서 `vTaskNotifyGiveFromISR()`로 우선순위 3의 제어 태스크를 깨우고, 태스크가 `ulTaskNotifyTake()`에서 돌아온 직후 시간을 잰다. 그리고 우선순위 1의 "300 µs 일" 태스크를 켜도 값이 변하는지 본다. reset → `main()`, reset → 첫 태스크도 찍는다.

```c
/* ex2 — FreeRTOS: boot time, IRQ->ISR, IRQ->task wake (notify), idle vs busy low task */
#include "FreeRTOS.h"
#include "task.h"
#include "a7.h"
#define N 200
static TaskHandle_t h_ctrl;
static volatile uint32_t isr_lat, t_main, t_task, busy_on;
static stat_t s_isr, s_wake_idle, s_wake_busy;

void TIMER0_IRQHandler(void)
{
    isr_lat = timer0_elapsed();
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(h_ctrl, &woken);       /* "semaphore give" from ISR */
    portYIELD_FROM_ISR(woken);                    /* pend PendSV if ctrl > current */
}

static void busy_task(void *arg)                  /* priority 1: 300 us chunks forever */
{
    (void)arg;
    for (;;) { while (!busy_on) { } uint32_t t0 = cyc(); while (cyc() - t0 < 300u * 25u) { } }
}

static void collect(stat_t *s)
{
    for (int i = 0; i < N; i++) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  /* block until the ISR gives */
        uint32_t lat = timer0_elapsed();
        stat_add(&s_isr, isr_lat);
        stat_add(s, lat);
    }
}

static void ctrl_task(void *arg)                  /* priority 3: the "control loop" */
{
    (void)arg;
    t_task = cyc();                               /* reset -> first task line */
    timer0_start(25000 + 37, 0x40);               /* 0x40 >= MAX_SYSCALL(0x20): API allowed */
    collect(&s_wake_idle);
    busy_on = 1;
    collect(&s_wake_busy);
    timer0_stop();
    board_printf_raw("boot: reset->main = %u ns, reset->first task = %u ns\n",
                     CYC_NS(t_main), CYC_NS(t_task));
    stat_print("IRQ->ISR first line        ", &s_isr);
    stat_print("IRQ->task (idle CPU)       ", &s_wake_idle);
    stat_print("IRQ->task (busy prio-1 job)", &s_wake_busy);
    board_exit(0);
}

int main(void)
{
    t_main = cyc();
    xTaskCreate(ctrl_task, "ctrl", 512, NULL, 3, &h_ctrl);
    xTaskCreate(busy_task, "busy", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A7/ex2_frt.c OUT=/var/tmp/rtos/A7/ex2 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex2/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  13636	    100	 135496	 149232	  246f0	/var/tmp/rtos/A7/ex2/app.elf
boot: reset->main = 1366560 ns, reset->first task = 1515000 ns
IRQ->ISR first line         n=400 min=120 avg=120 max=160 ns
IRQ->task (idle CPU)        n=200 min=8280 avg=8280 max=8320 ns
IRQ->task (busy prio-1 job) n=200 min=8280 avg=8280 max=8320 ns
[board_exit] code=0
```

출력에서 볼 것:

- **IRQ → 태스크 8.28 µs, 하위 태스크가 바빠도 8.28 µs**. 바로 이것이 RTOS를 쓰는 이유다. bare-metal의 "한가할 때 0.56 µs"보다는 15배 느리지만, **다른 일이 무엇이든 변하지 않는다**(max도 8.32 µs). 지연이 "가장 긴 다른 일"에서 "커널 경로 길이"로 바뀌었다.
- 8.28 µs = 207사이클(25 MHz 기준). 그 안에 `vTaskNotifyGiveFromISR` → PendSV 예약 → ISR 복귀 → PendSV 핸들러(레지스터 저장 · `vTaskSwitchContext` · 복원) → `ulTaskNotifyTake`의 나머지 경로가 있다(§13).
- **reset → main 1.37 ms**: bare-metal(15.6 µs)의 88배다. 이유는 §4에서 본다(힌트: bss 135496바이트).

**같은 코드, 설정만 바꿔서**: 템플릿 기본 설정은 디버그 편의 기능이 켜져 있다(`configCHECK_FOR_STACK_OVERFLOW 2` — 전환마다 스택 끝 16바이트 패턴 검사, `configGENERATE_RUN_TIME_STATS 1` — 전환마다 `board_now_us()` 호출). 이걸 끈 "lean" 설정과 heap 16 KB로 다시 돌렸다. lean 설정은 앱 폴더의 `FreeRTOSConfig.h`가 템플릿보다 먼저 잡히는 규칙을 이용했다(폴더를 include 경로에 넣으려고 빈 `lean.c`를 하나 같이 빌드한다).

```c
/* lean/FreeRTOSConfig.h — template config minus the debug-time costs */
#ifndef A7_LEAN_CONFIG_H
#define A7_LEAN_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW   0     /* no 16-byte pattern check per switch */
#undef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS    0     /* no board_now_us() per switch */
#undef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY         0
#undef configUSE_STATS_FORMATTING_FUNCTIONS
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#endif
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/A7; M="make -s -f /var/rtoslab/templates/freertos-mps2/Makefile"; 
echo "== default, heap 16KB"; $M SRCS=$S/ex2_frt.c OUT=/var/tmp/rtos/A7/ex2_h16 EXTRA_CFLAGS=-DconfigTOTAL_HEAP_SIZE=16384 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex2_h16/app.elf;
echo "== lean, heap 16KB"; $M SRCS="$S/ex2_frt.c $S/lean/lean.c" OUT=/var/tmp/rtos/A7/ex2_lean EXTRA_CFLAGS=-DconfigTOTAL_HEAP_SIZE=16384 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex2_lean/app.elf'
```

```text
== default, heap 16KB
boot: reset->main = 219680 ns, reset->first task = 368120 ns
IRQ->ISR first line         n=400 min=120 avg=120 max=160 ns
IRQ->task (idle CPU)        n=200 min=8280 avg=8280 max=8320 ns
IRQ->task (busy prio-1 job) n=200 min=8280 avg=8280 max=8320 ns
[board_exit] code=0
== lean, heap 16KB
   text	   data	    bss	    dec	    hex	filename
  13452	    100	  20768	  34320	   8610	/var/tmp/rtos/A7/ex2_lean/app.elf
boot: reset->main = 219360 ns, reset->first task = 367160 ns
IRQ->ISR first line         n=400 min=120 avg=120 max=160 ns
IRQ->task (idle CPU)        n=200 min=6880 avg=6880 max=6920 ns
IRQ->task (busy prio-1 job) n=200 min=6880 avg=6880 max=6920 ns
[board_exit] code=0
```

출력에서 볼 것: heap을 128 KB → 16 KB로 줄이자 reset → main이 1.37 ms → 0.22 ms. lean 설정은 IRQ → 태스크를 8.28 → **6.88 µs**(17% 감소)로 줄였다. 디버그 옵션은 개발 중에는 켜 두는 게 맞지만(스택 오버플로를 잡아 준다), "RTOS 비용"을 말할 때는 **어떤 설정의 숫자인지**까지 말해야 한다.

### 예제 4 — Zephyr: 같은 보드, 같은 IRQ, `k_sem`으로 깨우기

무엇을 확인하나: Zephyr v4.4.2에서 같은 측정을 한다. `IRQ_CONNECT(8, …)`로 TIMER0 ISR을 붙이고, ISR에서 `k_sem_give()`, main 스레드(우선순위 0)가 `k_sem_take()`에서 깨어난 직후를 잰다. 부트 배너는 끄고(`CONFIG_BOOT_BANNER=n`) main 첫 줄 시간을 찍는다. QEMU 종료는 lab의 `board_exit()`와 같은 semihosting 명령을 직접 쓴다.

```text
# z_lat/CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(a7_zlat)
target_sources(app PRIVATE src/main.c)
# z_lat/prj.conf
CONFIG_BOOT_BANNER=n
```

```c
/* ex3 — Zephyr v4.4.2 on mps2/an385: boot time, IRQ->ISR, IRQ->thread wake (k_sem) */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#define REG(a)     (*(volatile uint32_t *)(a))
#define CYC()      REG(0x40028018u)               /* FPGAIO 25 MHz counter from reset */
#define T0         0x40000000u                    /* CMSDK TIMER0, IRQ 8 */
#define T0_ELAPSED() (REG(T0 + 8) - REG(T0 + 4))  /* RELOAD - VALUE */
#define N 200
struct st { uint32_t n, min, max; uint64_t sum; };
static struct st s_isr, s_idle, s_busy;
static volatile uint32_t isr_lat, busy_on;
static K_SEM_DEFINE(sem, 0, 1);

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
    isr_lat = T0_ELAPSED();
    REG(T0 + 0xC) = 1;                            /* INTCLR */
    k_sem_give(&sem);                             /* wakes main thread (prio 0) */
}
static void busy_fn(void *a, void *b, void *c)   /* prio 5: 300 us chunks */
{
    for (;;) { while (!busy_on) { } uint32_t t = CYC(); while (CYC() - t < 300 * 25) { } }
}
K_THREAD_DEFINE(busy, 1024, busy_fn, NULL, NULL, NULL, 5, 0, 0);

static void collect(struct st *s)
{
    for (int i = 0; i < N; i++) {
        k_sem_take(&sem, K_FOREVER);
        uint32_t lat = T0_ELAPSED();
        add(&s_isr, isr_lat); add(s, lat);
    }
}
int main(void)
{
    uint32_t t_main = CYC();                      /* reset -> first line of main thread */
    IRQ_CONNECT(8, 2, t0_isr, NULL, 0);
    REG(T0) = 0; REG(T0 + 8) = 25000 + 37 - 1; REG(T0 + 4) = 25000 + 37 - 1;
    irq_enable(8);
    REG(T0) = 1 | 8;
    collect(&s_idle);
    busy_on = 1;
    collect(&s_busy);
    REG(T0) = 0;
    printk("boot: reset->main = %u ns\n", t_main * 40);
    show("IRQ->ISR first line          ", &s_isr);
    show("IRQ->thread (idle CPU)       ", &s_idle);
    show("IRQ->thread (busy prio-5 job)", &s_busy);
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");  /* semihosting exit */
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A7/z_lat /var/tmp/rtos/A7/z_lat && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/z_lat/zephyr/zephyr.elf'
```

```text
           FLASH:       14328 B         4 MB      0.34%
             RAM:        7360 B         4 MB      0.18%
boot: reset->main = 205000 ns
IRQ->ISR first line           n=400 min=280 avg=280 max=320 ns
IRQ->thread (idle CPU)        n=200 min=19200 avg=19200 max=19240 ns
IRQ->thread (busy prio-5 job) n=200 min=19200 avg=19200 max=19240 ns
```

출력에서 볼 것: 모양은 FreeRTOS와 같다 — 하위 스레드가 바빠도 값이 그대로. 그런데 **IRQ → 스레드 19.2 µs**로 FreeRTOS의 두 배가 넘는다. IRQ → ISR도 280 ns로 더 길다(`IRQ_CONNECT`는 공용 `_isr_wrapper`가 소프트웨어 ISR 표를 찾아 내 함수를 부르는 구조라 한 단계가 더 있다. 그걸 건너뛰는 `IRQ_DIRECT_CONNECT`도 있다 — E1).

> 🔄 **2026-11-01 정정 (E3)**: 이 예제의 "idle CPU" 줄도 실제로는 prio-5 `busy_fn` 스레드가 계속 돌고 있어 CPU가 유휴가 아니었다. 진짜 유휴 CPU에서 다시 재면 14.08 µs, CPU를 쓰는 스레드가 있으면 19.48 µs다 — 차이의 이유는 E3 노트에서 분해한다.

19.2 µs는 어디서 왔나? 빌드된 `.config`를 보면 `CONFIG_TIMESLICING=y`, `CONFIG_TIMESLICE_SIZE=20`, `CONFIG_ARM_MPU=y`다. 두 가지를 하나씩 꺼 봤다.

```sh
limactl shell elinux -- bash -lc 'for v in "ts_off:-DCONFIG_TIMESLICING=n" "mpu_off:-DCONFIG_ARM_MPU=n" "both_off:-DCONFIG_TIMESLICING=n -DCONFIG_ARM_MPU=n"; do n=${v%%:*}; a=${v#*:}; echo "== $n"; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A7/z_lat /var/tmp/rtos/A7/z_lat_$n -- $a >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/z_lat_$n/zephyr/zephyr.elf; done'
```

```text
== ts_off
boot: reset->main = 191520 ns
IRQ->ISR first line           n=400 min=280 avg=280 max=320 ns
IRQ->thread (idle CPU)        n=200 min=5200 avg=5200 max=5200 ns
IRQ->thread (busy prio-5 job) n=200 min=5200 avg=5200 max=5200 ns
== mpu_off
boot: reset->main = 191200 ns
IRQ->ISR first line           n=400 min=280 avg=280 max=320 ns
IRQ->thread (idle CPU)        n=200 min=19200 avg=19200 max=19240 ns
IRQ->thread (busy prio-5 job) n=200 min=19200 avg=19200 max=19240 ns
== both_off
boot: reset->main = 177720 ns
IRQ->ISR first line           n=400 min=280 avg=280 max=320 ns
IRQ->thread (idle CPU)        n=200 min=5200 avg=5200 max=5200 ns
IRQ->thread (busy prio-5 job) n=200 min=5200 avg=5200 max=5200 ns
```

출력에서 볼 것: **범인은 time slicing**이다. 끄면 19.2 → **5.2 µs**로, FreeRTOS lean(6.88 µs)보다도 빠르다. MPU는 이 경로에 영향이 없었다(userspace · 스택 가드를 안 쓰면 전환 때 MPU를 다시 쓰지 않는다). 소스로 확인하면 이렇다(`kernel/sched.c`, `kernel/timeslicing.c`).

- 스레드를 깨우면 `update_cache()`가 "다음 스레드"를 정하고, 그게 현재와 다르면 `z_reset_time_slice(thread)`를 부른다.
- `z_reset_time_slice()`는 슬라이스 타임아웃을 `z_abort_timeout()`으로 지우고 `z_add_timeout(…, K_TICKS(slice_size - 1))`로 다시 건다(main 스레드는 우선순위 0이라 `CONFIG_TIMESLICE_PRIORITY=0` 조건에 걸려 20 ms 슬라이스 대상이다).
- 타임아웃 리스트의 맨 앞이 바뀌면 tickless 커널은 `sys_clock_set_timeout()`(`drivers/timer/cortex_m_systick.c`)으로 SysTick을 다시 프로그래밍한다. 이 함수 안에 경과 사이클 계산과 반올림 나눗셈이 있다.

즉 **전환할 때마다 타이머 하드웨어를 다시 맞추는 일**이 붙어 있었다. 같은 우선순위 스레드끼리 CPU를 나눠 쓸 일이 없으면 time slicing은 꺼도 된다. 이게 이 노트의 첫 번째 교훈이다: **"RTOS A가 B보다 느리다"는 대부분 "기본 설정이 다르다"이다.** 반드시 설정과 함께 숫자를 말한다.

### 3.2 정리 — 무엇을 비교했나

| 경로 (25 MHz, ICOUNT) | 한가할 때 | 다른 일 300 µs 중일 때 (max) | 결정 요인 |
|---|---|---|---|
| bare-metal ISR → 슈퍼 루프 플래그 | 0.56 µs | 297.8 µs | 루프 안 가장 긴 일 |
| FreeRTOS 기본 → 태스크 | 8.28 µs | 8.32 µs | 커널 경로 + 디버그 옵션 |
| FreeRTOS lean → 태스크 | 6.88 µs | 6.92 µs | 커널 경로 |
| Zephyr 기본 → 스레드 | 19.2 µs | 19.24 µs | 커널 경로 + time slice 재설정 |
| Zephyr slicing off → 스레드 | 5.2 µs | 5.2 µs | 커널 경로 |

**흔한 함정**: "bare-metal이 RTOS보다 빠르다"는 **평균**에서만 맞다. **최악값**은 슈퍼 루프에 무엇을 넣었느냐에 달렸고, 기능이 늘수록 나빠진다. 반대로 "RTOS는 결정적이다"도 **그 태스크가 가장 높은 우선순위일 때**만 맞다. 우선순위가 같은 태스크, 긴 임계구역(§7), 우선순위 역전(D3)이 있으면 RTOS도 무너진다.

---

## 4. 부팅 시간 — reset부터 내 코드까지

예제 2 · 3 · 4가 찍은 부팅 시간을 모았다.

| 실행 환경 | 측정 지점 | reset부터 |
|---|---|---|
| bare-metal | `main()` 첫 줄 | 15.6 µs |
| Zephyr 기본 | main 스레드 첫 줄 | 205.0 µs |
| Zephyr slicing · MPU off | main 스레드 첫 줄 | 177.7 µs |
| FreeRTOS heap 16 KB | `main()` 첫 줄 / 첫 태스크 | 219.7 µs / 368.1 µs |
| FreeRTOS heap 128 KB (템플릿 기본) | `main()` 첫 줄 / 첫 태스크 | 1366.6 µs / 1515.0 µs |
| Linux (EL N4 랩, QEMU TCG + 공유 VM) | BusyBox 첫 셸 | 8.91 s (인용) |

```svg
<svg viewBox="0 0 680 276" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="230.0" y1="30.0" x2="230.0" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="230.0" y="24.0" font-size="12" text-anchor="middle">10 µs</text>
<line x1="291.7" y1="30.0" x2="291.7" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="291.7" y="24.0" font-size="12" text-anchor="middle">100 µs</text>
<line x1="353.3" y1="30.0" x2="353.3" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="353.3" y="24.0" font-size="12" text-anchor="middle">1 ms</text>
<line x1="415.0" y1="30.0" x2="415.0" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="415.0" y="24.0" font-size="12" text-anchor="middle">10 ms</text>
<line x1="476.7" y1="30.0" x2="476.7" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="476.7" y="24.0" font-size="12" text-anchor="middle">100 ms</text>
<line x1="538.3" y1="30.0" x2="538.3" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="538.3" y="24.0" font-size="12" text-anchor="middle">1 s</text>
<line x1="600.0" y1="30.0" x2="600.0" y2="236.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="600.0" y="24.0" font-size="12" text-anchor="middle">10 s</text>
<text x="222.0" y="55.0" font-size="12" text-anchor="end">bare-metal: reset→main</text>
<rect x="230.0" y="42.0" width="11.9" height="16.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="247.9" y="55.0" font-size="12">15.6 µs</text>
<text x="222.0" y="83.0" font-size="12" text-anchor="end">Zephyr (slice·MPU off)</text>
<rect x="230.0" y="70.0" width="77.1" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="313.1" y="83.0" font-size="12">177.7 µs</text>
<text x="222.0" y="111.0" font-size="12" text-anchor="end">Zephyr 기본</text>
<rect x="230.0" y="98.0" width="80.9" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="316.9" y="111.0" font-size="12">205.0 µs</text>
<text x="222.0" y="139.0" font-size="12" text-anchor="end">FreeRTOS heap 16 KB: 첫 태스크</text>
<rect x="230.0" y="126.0" width="96.6" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="332.6" y="139.0" font-size="12">368.1 µs</text>
<text x="222.0" y="167.0" font-size="12" text-anchor="end">FreeRTOS heap 128 KB: main</text>
<rect x="230.0" y="154.0" width="131.7" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="367.7" y="167.0" font-size="12">1.37 ms</text>
<text x="222.0" y="195.0" font-size="12" text-anchor="end">FreeRTOS heap 128 KB: 첫 태스크</text>
<rect x="230.0" y="182.0" width="134.5" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="370.5" y="195.0" font-size="12">1.51 ms</text>
<text x="222.0" y="223.0" font-size="12" text-anchor="end">Linux 셸 (EL N4, TCG)</text>
<rect x="230.0" y="210.0" width="366.9" height="16.0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="602.9" y="223.0" font-size="12">8.91 s</text>
<text x="10.0" y="261.0" font-size="12">로그 축. reset부터 FPGAIO 25 MHz 카운터로 잼 (Linux 값은 EL N4 인용, 실제 보드 아님)</text>
</svg>
```

그림 3 — reset부터 사용자 코드까지(로그 축). MCU 쪽은 전부 ms 이하, Linux는 초 단위다. FreeRTOS 두 막대의 차이는 커널이 아니라 **heap 크기**에서 나온다.

읽는 법:

- **FreeRTOS의 `main()` 전 1.37 ms는 커널 비용이 아니다.** 아직 커널 코드는 한 줄도 안 돌았다. `heap_4.c`의 `ucHeap[configTOTAL_HEAP_SIZE]`가 `.bss`에 있고, `Reset_Handler`가 `.bss` 전체(135 KB)를 0으로 채우느라 걸린 시간이다. heap을 16 KB로 줄이면 0.22 ms. 실제 칩에서도 같다 — 큰 `.bss`(DMA 버퍼, 로그 링버퍼, heap)는 부팅을 늦춘다. 부팅이 급한 제품은 큰 버퍼를 `.noinit` 섹션에 두고 0 채우기를 건너뛴다(B5).
- **`main()` → 첫 태스크 약 148 µs**(368.1 - 219.7)가 진짜 FreeRTOS 비용이다: `xTaskCreate` 두 번(heap 할당 + TCB · 스택 초기화), `vTaskStartScheduler()`가 idle 태스크와 timer 태스크를 만들고, SysTick을 켜고, SVC로 첫 태스크에 진입한다(C3).
- **Zephyr 205 µs**에는 `.bss` 0 채우기(ISR 스택 4 KB 포함), 커널 초기화, 드라이버 `SYS_INIT` 단계(UART 등), main 스레드 생성까지 다 들어 있다. Zephyr는 "커널을 시작한 다음 main을 스레드로 부르는" 구조라 main 첫 줄이 곧 "스레드 환경 안의 첫 줄"이다.
- **Linux 8.91 s**는 TCG 에뮬레이션 + 부하 상황이라 과장된 값이다(EL N4). 실제 보드에서도 최적화해서 1~수 초다(EL C7). MCU는 **수백 µs 안에 PWM 핀을 안전값으로 잡는다** — EL N4 §1.4의 "Linux가 뜨기 전 수 초 동안 누가 모터를 잡고 있나?"의 답이 이 숫자다.

**Don 경험과 연결**: SSD 컨트롤러는 전원이 들어오고 호스트가 PCIe 링크를 훈련하는 동안 펌웨어가 이미 떠 있어야 하고, 플래시 매핑 테이블 복구까지 시간 예산이 있었다. 그때 "큰 테이블을 0으로 채우지 말고 필요할 때 채운다"는 최적화를 했다면 그게 정확히 위의 `.bss` 이야기다.

---

## 5. 컨텍스트 스위치 비용 — 태스크로 나누는 값

### 5.1 직관

슈퍼 루프에서 "일 A에서 일 B로 넘어가기"는 함수 호출 하나다. RTOS에서는 **레지스터 8개(R4–R11) 저장 → TCB 교체 → 다른 스택에서 8개 복원**이 들어간다(하드웨어가 나머지 8개를 자동으로 쌓는다 — B2). 거기에 "누구로 바꿀지" 고르는 결정과, 블록/깨움 API의 리스트 조작이 붙는다. 태스크로 나눌수록 이 값을 더 자주 낸다.

두 가지를 따로 잰다.

- **순수 전환**: 같은 우선순위 태스크 둘이 서로 `taskYIELD()`/`k_yield()`만 반복. 전환 한 번 = PendSV 진입 + 결정 + 복원.
- **핑퐁**: 태스크 A가 B에게 통지(세마포어) → A는 기다림(블록) → B가 깨어나 A에게 통지 → B는 기다림. 실제 코드에서 쓰는 "주고받기" 비용이다. 전환 한 번 = give + take(블록) + 전환.

### 예제 5 — FreeRTOS: notify 핑퐁, taskYIELD, 그리고 함수 호출

무엇을 확인하나: 같은 바이너리 안에서 (1) 스케줄러 시작 전에 `noinline` 함수 호출 비용, (2) 태스크 A · B의 통지 핑퐁 5000왕복, (3) 같은 우선순위 태스크 둘의 `taskYIELD()` 10000회를 잰다.

```c
/* ex4 — FreeRTOS context-switch cost: notify ping-pong and taskYIELD, vs function call */
#include "FreeRTOS.h"
#include "task.h"
#include "a7.h"
#define N 5000
static TaskHandle_t hA, hB;
static volatile uint32_t sink;

__attribute__((noinline)) static void job(void) { sink++; }   /* super-loop "switch" */

static void ping(void *arg)             /* A: give B, wait for B */
{
    (void)arg;
    uint32_t t0 = cyc();
    for (int i = 0; i < N; i++) { xTaskNotifyGive(hB); ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
    uint32_t pp = cyc() - t0;           /* N round trips = 2N switches */
    vTaskDelete(hB);
    board_printf_raw("notify ping-pong: %u ns per switch (give+take+switch)\n",
                     CYC_NS(pp) / (2u * N));
    vTaskPrioritySet(NULL, 1);          /* drop to the yielders' priority */
    for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
}
static void pong(void *arg)             /* B: wait for A, give A */
{
    (void)arg;
    for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); xTaskNotifyGive(hA); }
}

static volatile uint32_t yields, t_y0;
static void yielder(void *arg)          /* two of these at prio 2: yield back and forth */
{
    (void)arg;
    if (t_y0 == 0) t_y0 = cyc();
    while (yields < 2u * N) { yields++; taskYIELD(); }
    uint32_t dt = cyc() - t_y0;
    board_printf_raw("taskYIELD: %u ns per switch\n", CYC_NS(dt) / (2u * N));
    board_exit(0);
}
static void starter(void *arg)          /* prio 4: runs after ping-pong finished */
{
    (void)arg;
    vTaskDelay(1);
    xTaskCreate(yielder, "y1", 256, NULL, 2, NULL);
    xTaskCreate(yielder, "y2", 256, NULL, 2, NULL);
    vTaskDelete(NULL);
}

int main(void)
{
    uint32_t t0 = cyc();
    for (int i = 0; i < N; i++) { job(); job(); }
    board_printf_raw("function call: %u ns per call\n", CYC_NS(cyc() - t0) / (2u * N));
    xTaskCreate(ping, "A", 256, NULL, 3, &hA);
    xTaskCreate(pong, "B", 256, NULL, 3, &hB);
    xTaskCreate(starter, "st", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/A7; M="make -s -f /var/rtoslab/templates/freertos-mps2/Makefile";
echo "== default config"; $M SRCS=$S/ex4_frt_cs.c OUT=/var/tmp/rtos/A7/ex4 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex4/app.elf;
echo "== lean config"; $M SRCS="$S/ex4_frt_cs.c $S/lean/lean.c" OUT=/var/tmp/rtos/A7/ex4_lean >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex4_lean/app.elf'
```

```text
== default config
function call: 224 ns per call
notify ping-pong: 13378 ns per switch (give+take+switch)
taskYIELD: 3332 ns per switch
[board_exit] code=0
== lean config
function call: 224 ns per call
notify ping-pong: 11964 ns per switch (give+take+switch)
taskYIELD: 1922 ns per switch
[board_exit] code=0
```

출력에서 볼 것:

- **순수 전환 1.92 µs (lean) / 3.33 µs (기본)** = 48 / 83사이클. 디버그 옵션 두 개가 전환 비용을 70% 올렸다(전환마다 스택 패턴 비교 + 런타임 통계 시계 읽기).
- **핑퐁은 전환당 12~13 µs** — 순수 전환의 6배. 비용 대부분은 전환 자체가 아니라 **블록 API**에 있다. V11.3.2의 `ulTaskGenericNotifyTake()`(`tasks.c`)는 블록할 때 `vTaskSuspendAll()` → `taskENTER_CRITICAL()` → `prvAddCurrentTaskToDelayedList()` → `xTaskResumeAll()` → `taskYIELD_WITHIN_API()` 순서를 밟는다. `xTaskResumeAll()`만 428바이트짜리 함수다(§13).
- 함수 호출은 0.224 µs. "태스크 하나 = 자기 스택을 가진 main 루프 하나"라는 편리함의 값이 전환당 함수 호출 10~60개다.

### 예제 6 — Zephyr: `k_sem` 핑퐁과 `k_yield`

무엇을 확인하나: Zephyr에서 같은 두 측정을 한다. main 스레드를 우선순위 3으로 내려서 pong 스레드와 맞추고, `k_yield` 측정용 스레드 두 개는 100 ms 뒤에 시작하게(`K_THREAD_DEFINE`의 마지막 인자 = 시작 지연 ms) 했다. CMakeLists.txt와 prj.conf는 예제 4와 같다(프로젝트 이름만 다름).

```c
/* ex5 — Zephyr context-switch cost: k_sem ping-pong and k_yield */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)   /* FPGAIO 25 MHz counter */
#define N 5000
static K_SEM_DEFINE(s_a, 0, 1);
static K_SEM_DEFINE(s_b, 0, 1);
static K_SEM_DEFINE(done, 0, 2);
static volatile uint32_t yields, ty0, ty1;

static void pong(void *a, void *b, void *c)          /* B: wait for A, give A */
{
    for (;;) { k_sem_take(&s_b, K_FOREVER); k_sem_give(&s_a); }
}
K_THREAD_DEFINE(t_pong, 1024, pong, NULL, NULL, NULL, 3, 0, 0);

static void yielder(void *a, void *b, void *c)       /* two equal-prio threads */
{
    if (!ty0) ty0 = CYC();
    while (yields < 2u * N) { yields++; k_yield(); }
    if (!ty1) ty1 = CYC();
    k_sem_give(&done);
}
K_THREAD_DEFINE(t_y1, 1024, yielder, NULL, NULL, NULL, 4, 0, 100);  /* start later */
K_THREAD_DEFINE(t_y2, 1024, yielder, NULL, NULL, NULL, 4, 0, 100);

int main(void)                                         /* main = A, prio 0 -> set 3 */
{
    k_thread_priority_set(k_current_get(), 3);
    uint32_t t0 = CYC();
    for (int i = 0; i < N; i++) { k_sem_give(&s_b); k_sem_take(&s_a, K_FOREVER); }
    uint32_t pp = CYC() - t0;
    printk("k_sem ping-pong: %u ns per switch (give+take+switch)\n", pp * 40 / (2 * N));
    k_thread_abort(t_pong);
    k_sem_take(&done, K_FOREVER);                      /* y1/y2 start at 100 ms */
    k_sem_take(&done, K_FOREVER);
    printk("k_yield: %u ns per switch\n", (ty1 - ty0) * 40 / (2 * N));
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "default:" "ts_off:-DCONFIG_TIMESLICING=n"; do n=${v%%:*}; a=${v#*:}; echo "== $n"; /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A7/z_cs /var/tmp/rtos/A7/z_cs_$n -- $a >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/z_cs_$n/zephyr/zephyr.elf; done'
```

```text
== default
k_sem ping-pong: 21186 ns per switch (give+take+switch)
k_yield: 16736 ns per switch
== ts_off
k_sem ping-pong: 7600 ns per switch (give+take+switch)
k_yield: 2656 ns per switch
```

출력에서 볼 것: 기본 설정의 `k_yield` 16.7 µs 중 14 µs가 time slice 재설정이다. 끄면 **순수 전환 2.66 µs**, 핑퐁 7.6 µs. 핑퐁은 오히려 FreeRTOS(12 µs)보다 빠르다 — Zephyr의 `k_sem_take`(`kernel/sem.c`)는 `z_pend_curr()`로 대기 큐에 넣고 곧바로 전환하는 비교적 짧은 경로이고, FreeRTOS 통지 블록 경로는 앞에서 본 suspend-all/resume-all을 거치기 때문이다.

```svg
<svg viewBox="0 0 680 314" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="240.0" y1="30.0" x2="240.0" y2="274.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="240.0" y="24.0" font-size="12" text-anchor="middle">0 µs</text>
<line x1="316.0" y1="30.0" x2="316.0" y2="274.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="316.0" y="24.0" font-size="12" text-anchor="middle">5 µs</text>
<line x1="392.0" y1="30.0" x2="392.0" y2="274.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="392.0" y="24.0" font-size="12" text-anchor="middle">10 µs</text>
<line x1="468.0" y1="30.0" x2="468.0" y2="274.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="468.0" y="24.0" font-size="12" text-anchor="middle">15 µs</text>
<line x1="544.0" y1="30.0" x2="544.0" y2="274.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="544.0" y="24.0" font-size="12" text-anchor="middle">20 µs</text>
<line x1="620.0" y1="30.0" x2="620.0" y2="274.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="620.0" y="24.0" font-size="12" text-anchor="middle">25 µs</text>
<text x="232.0" y="55.0" font-size="12" text-anchor="end">함수 호출 (슈퍼 루프의 "전환")</text>
<rect x="240.0" y="42.0" width="3.4" height="16.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="249.4" y="55.0" font-size="12">0.22</text>
<text x="232.0" y="81.0" font-size="12" text-anchor="end">FreeRTOS taskYIELD (lean)</text>
<rect x="240.0" y="68.0" width="29.2" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="275.2" y="81.0" font-size="12">1.92</text>
<text x="232.0" y="107.0" font-size="12" text-anchor="end">Zephyr k_yield (slice off)</text>
<rect x="240.0" y="94.0" width="40.4" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="286.4" y="107.0" font-size="12">2.66</text>
<text x="232.0" y="133.0" font-size="12" text-anchor="end">FreeRTOS taskYIELD (기본)</text>
<rect x="240.0" y="120.0" width="50.6" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="296.6" y="133.0" font-size="12">3.33</text>
<text x="232.0" y="159.0" font-size="12" text-anchor="end">Zephyr k_sem 핑퐁 (slice off)</text>
<rect x="240.0" y="146.0" width="115.5" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="361.5" y="159.0" font-size="12">7.60</text>
<text x="232.0" y="185.0" font-size="12" text-anchor="end">FreeRTOS notify 핑퐁 (lean)</text>
<rect x="240.0" y="172.0" width="181.9" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="427.9" y="185.0" font-size="12">11.96</text>
<text x="232.0" y="211.0" font-size="12" text-anchor="end">FreeRTOS notify 핑퐁 (기본)</text>
<rect x="240.0" y="198.0" width="203.3" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="449.3" y="211.0" font-size="12">13.38</text>
<text x="232.0" y="237.0" font-size="12" text-anchor="end">Zephyr k_yield (기본)</text>
<rect x="240.0" y="224.0" width="254.4" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="500.4" y="237.0" font-size="12">16.74</text>
<text x="232.0" y="263.0" font-size="12" text-anchor="end">Zephyr k_sem 핑퐁 (기본)</text>
<rect x="240.0" y="250.0" width="322.0" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="568.0" y="263.0" font-size="12">21.19</text>
<text x="10.0" y="299.0" font-size="12">전환 1회당 시간 (QEMU ICOUNT=1, 25 MHz 환산). 핑퐁 = 주기 API + 블록 + 전환</text>
</svg>
```

그림 4 — 전환 한 번의 비용(µs, 25 MHz 환산). 파랑은 bare-metal의 "전환"(함수 호출). 같은 RTOS 안에서도 설정과 API에 따라 10배까지 벌어진다.

**흔한 함정**: "FreeRTOS 컨텍스트 스위치는 수십 사이클"이라는 말은 **PendSV 핸들러 자체**(레지스터 저장/복원)의 이야기다. 설계에 쓰는 숫자는 "ISR → 태스크가 일을 시작할 때까지" 또는 "태스크 → 태스크 메시지 왕복"이다. 그건 수백 사이클이다. 면접에서 숫자를 말할 때 **무엇부터 무엇까지**인지 같이 말한다.

---

## 6. tick 비용 — 아무 일도 안 해도 내는 세금

### 6.1 직관

tick은 RTOS의 심장 박동이다. 매 tick마다 SysTick 인터럽트가 들어와 tick 카운터를 올리고, 지연(delay) 리스트에서 깨울 태스크가 있는지 보고, 같은 우선순위끼리 시간을 나눠 쓰게(time slicing) 한다(C5). bare-metal에는 이 세금이 없다. tick rate를 올리면 `vTaskDelay(1)`의 해상도는 좋아지지만 세금이 비례해서 오른다.

측정 방법은 "idle 루프 카운터"다. 바쁜 루프가 200 ms 동안 몇 번 도는지를 (a) 인터럽트를 막고 한 번, (b) tick을 켜고 한 번 센다. 줄어든 비율이 tick이 가져간 CPU다.

### 예제 7 — FreeRTOS: 100 / 1000 / 10000 Hz

무엇을 확인하나: 같은 소스를 `configTICK_RATE_HZ`만 바꿔 세 번 빌드한다. 기준선은 `portDISABLE_INTERRUPTS()`(BASEPRI를 올려 SysTick을 막음)로 잰다.

```c
/* ex6 — FreeRTOS tick overhead: busy counter with ticks vs. with interrupts masked */
#include "FreeRTOS.h"
#include "task.h"
#include "a7.h"
#define WINDOW (25000000u / 5)              /* 200 ms of virtual time */

static uint32_t count_for_window(void)      /* the "idle-loop counter" */
{
    uint32_t n = 0, t0 = cyc();
    while (cyc() - t0 < WINDOW) n++;
    return n;
}

static void meas_task(void *arg)            /* prio 1, only ready task (+idle) */
{
    (void)arg;
    portDISABLE_INTERRUPTS();               /* BASEPRI masks SysTick: no tick */
    uint32_t base = count_for_window();
    portENABLE_INTERRUPTS();
    TickType_t k0 = xTaskGetTickCount();
    uint32_t with = count_for_window();
    TickType_t ticks = xTaskGetTickCount() - k0;
    uint32_t bp = (uint32_t)((uint64_t)(base - with) * 10000u / base);   /* 0.01 % units */
    /* time lost per tick = lost fraction x window / ticks */
    uint32_t ns_per_tick = (uint32_t)((uint64_t)(base - with) * CYC_NS(WINDOW) / base / ticks);
    board_printf_raw("tick=%u Hz: base=%u with=%u ticks=%u  CPU lost=%u.%u%u%%  ~%u ns/tick\n",
                     (unsigned)configTICK_RATE_HZ, base, with, (unsigned)ticks,
                     bp / 100u, (bp / 10u) % 10u, bp % 10u, ns_per_tick);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(meas_task, "meas", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/A7; M="make -s -f /var/rtoslab/templates/freertos-mps2/Makefile"; for hz in 100 1000 10000; do $M SRCS="$S/ex6_tick.c" OUT=/var/tmp/rtos/A7/ex6_$hz EXTRA_CFLAGS="-DconfigTICK_RATE_HZ=$hz" 2>&1 | grep -i warn; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex6_$hz/app.elf | grep tick; done'
```

```text
tick=100 Hz: base=1250000 with=1249816 ticks=20  CPU lost=0.01%  ~1472 ns/tick
tick=1000 Hz: base=1250000 with=1248160 ticks=200  CPU lost=0.14%  ~1472 ns/tick
tick=10000 Hz: base=1250000 with=1231600 ticks=2000  CPU lost=1.47%  ~1472 ns/tick
```

출력에서 볼 것: tick 하나 = **1.47 µs**(37사이클)로 일정하고, CPU 손실은 rate에 정비례한다 — 100 Hz 0.01%, 1 kHz 0.14%, 10 kHz 1.47%. (lean 설정으로도 돌렸는데 세 줄 모두 똑같았다 — 전환이 없는 tick에는 디버그 옵션이 끼지 않는다.) 25 MHz에서 10 kHz tick은 이미 1.5%다. 같은 명령 수라면 480 MHz 코어에서는 0.08%다(§10의 계산).

### 예제 8 — Zephyr: tickless vs 주기 tick

무엇을 확인하나: Zephyr는 기본이 **tickless**다(`CONFIG_TICKLESS_KERNEL=y` — 다음 타임아웃이 있을 때만 타이머 인터럽트를 건다, C6). 이 보드의 기본 tick rate는 100 Hz(`QEMU_TARGET`이면 100이 기본값, `kernel/Kconfig`). tickless(slicing 켬/끔)와 주기 tick 100/1000/10000 Hz를 비교한다. 기준선은 `irq_lock()`.

```c
/* ex7 — Zephyr tick overhead: tickless vs periodic tick, busy counter method */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)   /* FPGAIO 25 MHz counter */
#define WINDOW (25000000u / 5)                       /* 200 ms */

static uint32_t count_for_window(void)
{
    uint32_t n = 0, t0 = CYC();
    while (CYC() - t0 < WINDOW) n++;
    return n;
}

int main(void)
{
    unsigned int key = irq_lock();                   /* BASEPRI: SysTick masked */
    uint32_t base = count_for_window();
    irq_unlock(key);
    int64_t k0 = k_uptime_ticks();
    uint32_t with = count_for_window();
    int64_t ticks = k_uptime_ticks() - k0;
    uint32_t bp = (uint32_t)((uint64_t)(base - with) * 10000u / base);
    printk("%s %5u Hz: base=%u with=%u kernel ticks=%u  CPU lost=%u.%u%u%%  lost=%u ns\n",
           IS_ENABLED(CONFIG_TICKLESS_KERNEL) ? "tickless" : "periodic",
           CONFIG_SYS_CLOCK_TICKS_PER_SEC, base, with, (uint32_t)ticks,
           bp / 100, (bp / 10) % 10, bp % 10,
           (uint32_t)((uint64_t)(base - with) * (WINDOW * 40ull) / base));
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'R=/var/rtoslab/bin; for v in "tl:" "tl_ts_off:-DCONFIG_TIMESLICING=n" "p100:-DCONFIG_TICKLESS_KERNEL=n -DCONFIG_SYS_CLOCK_TICKS_PER_SEC=100" "p1000:-DCONFIG_TICKLESS_KERNEL=n -DCONFIG_SYS_CLOCK_TICKS_PER_SEC=1000" "p10000:-DCONFIG_TICKLESS_KERNEL=n -DCONFIG_SYS_CLOCK_TICKS_PER_SEC=10000"; do n=${v%%:*}; a=${v#*:}; printf "%-10s" $n; $R/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A7/z_tick /var/tmp/rtos/A7/z_tick_$n -- $a | grep -i "warn\|error"; ICOUNT=1 $R/rtos-run-mps2 /var/tmp/rtos/A7/z_tick_$n/zephyr/zephyr.elf; done'
```

```text
tl        tickless   100 Hz: base=1041667 with=1040667 kernel ticks=20  CPU lost=0.09%  lost=191999 ns
tl_ts_off tickless   100 Hz: base=1041667 with=1041667 kernel ticks=20  CPU lost=0.00%  lost=0 ns
p100      periodic   100 Hz: base=1041667 with=1040780 kernel ticks=20  CPU lost=0.08%  lost=170303 ns
p1000     periodic  1000 Hz: base=1041667 with=1036640 kernel ticks=200  CPU lost=0.48%  lost=965183 ns
p10000    periodic 10000 Hz: base=1041667 with=995240 kernel ticks=2000  CPU lost=4.45%  lost=8913981 ns
```

출력에서 볼 것:

- **tickless + slicing off = 0%**. 바쁜 스레드 하나에 대기 중인 타임아웃이 없으면 타이머 인터럽트가 **한 번도** 안 들어온다. `kernel ticks=20`은 시간이 흐른 만큼 계산으로 센 값이지 인터럽트 횟수가 아니다.
- tickless + slicing on은 0.09%(192 µs/200 ms). 바쁜 main 스레드의 20 ms 슬라이스 타이머가 200 ms 동안 10번 울린 것으로 해석된다(그렇다면 한 번에 약 19 µs).
- 주기 tick은 tick당 **약 4.5 µs**(1 kHz에서 4.83, 10 kHz에서 4.46 µs/tick — §10 계산), FreeRTOS의 3배다. Zephyr의 tick 경로는 `sys_clock_isr()` → `sys_clock_announce()`가 타임아웃 리스트를 일반적인 방식으로 처리하고, 64비트 시간 계산을 한다. 10 kHz에서는 CPU의 4.45%가 사라진다.
- 100 Hz 주기 tick의 tick당 8.5 µs에는 20 ms 슬라이스 만료 처리가 섞여 있다. slicing을 끄고 한 번 더 쟀다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A7/z_tick /var/tmp/rtos/A7/z_tick_p100_tsoff -- -DCONFIG_TICKLESS_KERNEL=n -DCONFIG_SYS_CLOCK_TICKS_PER_SEC=100 -DCONFIG_TIMESLICING=n >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/z_tick_p100_tsoff/zephyr/zephyr.elf'
```

```text
periodic   100 Hz: base=1041667 with=1041330 kernel ticks=20  CPU lost=0.03%  lost=64703 ns
```

- 즉 순수 tick은 64703 ns / 20 = **약 3.2 µs/tick**이다(FreeRTOS 1.47 µs의 2배 남짓). 1 kHz · 10 kHz에서 본 4.83 · 4.46 µs/tick은 이보다 크다. 슬라이스 만료 몫(20 ms에 한 번)만으로는 다 설명되지 않는데, 이 노트에서는 원인을 더 파지 않았다 — tick 경로의 한 줄씩 분석은 C5의 몫이다.
- 기준선이 FreeRTOS(1250000)와 다른 것(1041667)은 컴파일러 코드 생성 차이(루프 한 바퀴 4 cyc vs 4.8 cyc)일 뿐이고, 비교는 각자의 기준선 대비로 한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="70.0" y1="240.0" x2="660.0" y2="240.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="64.0" y="244.0" font-size="12" text-anchor="end">0%</text>
<line x1="70.0" y1="200.0" x2="660.0" y2="200.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="64.0" y="204.0" font-size="12" text-anchor="end">1%</text>
<line x1="70.0" y1="160.0" x2="660.0" y2="160.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="64.0" y="164.0" font-size="12" text-anchor="end">2%</text>
<line x1="70.0" y1="120.0" x2="660.0" y2="120.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="64.0" y="124.0" font-size="12" text-anchor="end">3%</text>
<line x1="70.0" y1="80.0" x2="660.0" y2="80.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="64.0" y="84.0" font-size="12" text-anchor="end">4%</text>
<line x1="70.0" y1="40.0" x2="660.0" y2="40.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="64.0" y="44.0" font-size="12" text-anchor="end">5%</text>
<rect x="95.0" y="239.0" width="28.0" height="1.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="109.0" y="234.0" font-size="12" text-anchor="middle">0.01</text>
<rect x="129.0" y="236.8" width="28.0" height="3.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="143.0" y="231.8" font-size="12" text-anchor="middle">0.08</text>
<text x="143.8" y="260.0" font-size="12" text-anchor="middle">100 Hz</text>
<rect x="242.5" y="234.4" width="28.0" height="5.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="256.5" y="229.4" font-size="12" text-anchor="middle">0.14</text>
<rect x="276.5" y="220.8" width="28.0" height="19.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="290.5" y="215.8" font-size="12" text-anchor="middle">0.48</text>
<text x="291.2" y="260.0" font-size="12" text-anchor="middle">1000 Hz</text>
<rect x="390.0" y="181.2" width="28.0" height="58.8" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="404.0" y="176.2" font-size="12" text-anchor="middle">1.47</text>
<rect x="424.0" y="62.0" width="28.0" height="178.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="438.0" y="57.0" font-size="12" text-anchor="middle">4.45</text>
<text x="438.8" y="260.0" font-size="12" text-anchor="middle">10000 Hz</text>
<text x="551.5" y="218.0" font-size="12" text-anchor="middle">해당</text>
<text x="551.5" y="234.0" font-size="12" text-anchor="middle">없음</text>
<rect x="571.5" y="236.4" width="28.0" height="3.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="585.5" y="231.4" font-size="12" text-anchor="middle">0.09</text>
<rect x="605.5" y="239.0" width="28.0" height="1.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="619.5" y="234.0" font-size="12" text-anchor="middle">0.00</text>
<text x="586.2" y="260.0" font-size="12" text-anchor="middle">tickless</text>
<line x1="70.0" y1="240.0" x2="660.0" y2="240.0" stroke="currentColor" stroke-width="1"/>
<text x="10.0" y="22.0" font-size="12">바쁜 루프가 잃은 CPU (%) — tick 인터럽트만 있을 때</text>
<text x="10.0" y="285.0" font-size="12">초록 FreeRTOS (1.47 µs/tick) · 주황 Zephyr (4.5 µs/tick) · 파랑 Zephyr tickless + slice off</text>
</svg>
```

그림 5 — tick만으로 바쁜 루프가 잃은 CPU. 주기 tick은 rate에 정비례하고, tickless는 깨울 일이 없으면 0이다. FreeRTOS의 `configUSE_TICKLESS_IDLE`은 **idle일 때만** tick을 멈추므로 바쁜 태스크 측정에는 해당이 없다.

**Don 경험과 연결**: SSD 실행부에는 주기 tick이 없거나, 있어도 아주 느렸을 것이다. 시간은 자유 실행 하드웨어 타이머를 읽어서 알고(`board_now_us()` 같은 방식), 타임아웃은 "다음 마감 시각"을 비교해서 처리한다. 그게 바로 tickless 커널이 하는 일이다. tickless는 "새로운 기술"이 아니라 **실행부 개발자가 원래 하던 방식을 커널이 해 주는 것**이다.

---

## 7. 하이브리드 — 가장 빡빡한 루프는 커널 밖으로

### 7.1 직관

§3의 결론은 "RTOS로 깨우는 데 수 µs, 슈퍼 루프는 최악이 일의 길이"였다. 그럼 20 kHz FOC 전류 루프(주기 50 µs)는? RTOS 태스크로 깨우면 25 MHz에서 6.9 µs = 주기의 14%가 깨우는 데만 든다(§10). 그래서 실무 답은 대개 **둘 다**다.

- 전류 루프는 **ISR 안에서 끝까지** 계산한다(PWM 갱신까지). 이 ISR은 RTOS API를 안 부른다.
- 이 ISR을 **커널이 막지 못하는 우선순위**에 둔다. Cortex-M3/M4/M7 FreeRTOS 포트는 임계구역에서 PRIMASK가 아니라 **BASEPRI**를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`로 올린다. 그보다 **숫자가 작은**(더 높은) 우선순위의 인터럽트는 커널이 임계구역 안에 있어도 바로 들어온다. 이걸 흔히 zero-latency 인터럽트라고 부른다(Zephyr는 `CONFIG_ZERO_LATENCY_IRQS`와 `IRQ_ZERO_LATENCY` 플래그).
- 나머지(속도 루프, CAN, 진단, 로그)는 RTOS 태스크로.

BASEPRI · 우선순위 비트 · PRIGROUP 같은 세부는 B3, 우선순위 배치표 작성법은 E2가 다룬다. 여기서는 **측정으로 효과만** 확인한다.

### 예제 9 — zero-latency IRQ vs kernel-aware IRQ, 임계구역 아래에서

무엇을 확인하나: 우선순위 1 태스크가 50 µs짜리 `taskENTER_CRITICAL()` 구간을 반복한다(공유 큐를 만지는 로거라고 생각하자). 그동안 두 인터럽트의 진입 지연을 잰다 — TIMER0(우선순위 0x40, `configMAX_SYSCALL_INTERRUPT_PRIORITY` 0x20 아래 = kernel-aware)과 DUALTIMER(우선순위 0x00, 위 = zero-latency). 주기를 약간 다르게 해서 위상이 임계구역과 골고루 겹치게 했다.

```c
/* ex8 — hybrid: zero-latency IRQ (above MAX_SYSCALL) vs kernel-aware IRQ under critical sections */
#include "FreeRTOS.h"
#include "task.h"
#include "a7.h"
#define DT        0x40002000u                    /* CMSDK dual timer, timer 1, IRQ 10 */
#define DT_LOAD   REG32(DT + 0x00)
#define DT_VALUE  REG32(DT + 0x04)
#define DT_CTRL   REG32(DT + 0x08)
#define DT_INTCLR REG32(DT + 0x0C)
#define N 300
static stat_t s_aware, s_zero;

void TIMER0_IRQHandler(void)                     /* prio 0x40: may call ...FromISR */
{
    uint32_t lat = timer0_elapsed();
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (s_aware.n < N) stat_add(&s_aware, lat);
}
void DUALTIMER_IRQHandler(void)                  /* prio 0x00: BASEPRI never masks it */
{
    uint32_t lat = DT_LOAD - DT_VALUE;
    DT_INTCLR = 1;
    if (s_zero.n < N) stat_add(&s_zero, lat);    /* NO FreeRTOS API allowed here */
}

static void spin_us(uint32_t us) { uint32_t t = cyc(); while (cyc() - t < us * 25u) { } }

static void lowprio_task(void *arg)              /* e.g. a logger touching a shared queue */
{
    (void)arg;
    for (;;) {
        taskENTER_CRITICAL();                    /* BASEPRI = 0x20 */
        spin_us(50);                             /* 50 us critical section */
        taskEXIT_CRITICAL();
        spin_us(50);
        if (s_aware.n >= N && s_zero.n >= N) break;
    }
    timer0_stop(); DT_CTRL = 0;
    stat_print("kernel-aware IRQ (prio 0x40) entry", &s_aware);
    stat_print("zero-latency IRQ (prio 0x00) entry", &s_zero);
    board_exit(0);
}

int main(void)
{
    timer0_start(25000 + 37, 0x40);
    DT_LOAD = 25000 + 101 - 1;                   /* different period -> phases drift */
    NVIC_IPR(10) = 0x00;
    NVIC_ISER0 = 1u << 10;
    DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u;     /* enable, periodic, IRQ, 32-bit */
    xTaskCreate(lowprio_task, "low", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/A7; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="$S/ex8_zl.c" OUT=/var/tmp/rtos/A7/ex8 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A7/ex8/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12892	    100	 135448	 148440	  243d8	/var/tmp/rtos/A7/ex8/app.elf
kernel-aware IRQ (prio 0x40) entry n=300 min=120 avg=12600 max=50840 ns
zero-latency IRQ (prio 0x00) entry n=300 min=120 avg=120 max=160 ns
[board_exit] code=0
```

출력에서 볼 것: kernel-aware IRQ는 **최대 50.84 µs** — 임계구역 길이(50 µs) 그대로 기다렸다. 평균 12.6 µs는 "절반쯤 임계구역 안에 떨어진 경우가 섞인" 값이다. zero-latency IRQ는 **최대 160 ns**로, 같은 시간에 아무 영향도 받지 않았다. 이게 하이브리드의 근거다: **커널(과 커널을 쓰는 모든 코드)의 임계구역 최악값이 zero-latency ISR에는 들어가지 않는다.**

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="22.0" font-size="12">NVIC 우선순위 (숫자가 작을수록 높다) — FreeRTOS 템플릿 설정</text>
<text x="20.0" y="71.0" font-size="13" font-weight="bold">0x00</text>
<rect x="70.0" y="45.0" width="330.0" height="44.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="235.0" y="70.0" font-size="12" text-anchor="middle">zero-latency ISR (FOC 전류 · DShot 타이머)</text>
<text x="415.0" y="63.0" font-size="12">BASEPRI가 절대 막지 않음</text>
<text x="415.0" y="81.0" font-size="12">측정 max 160 ns</text>
<text x="20.0" y="151.0" font-size="13" font-weight="bold">0x40</text>
<rect x="70.0" y="125.0" width="330.0" height="44.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="235.0" y="150.0" font-size="12" text-anchor="middle">kernel-aware ISR (TIMER0 · UART · DMA 완료)</text>
<text x="415.0" y="143.0" font-size="12">임계구역이 끝날 때까지 대기</text>
<text x="415.0" y="161.0" font-size="12">측정 max 50.84 µs</text>
<text x="20.0" y="231.0" font-size="13" font-weight="bold">0xFF</text>
<rect x="70.0" y="205.0" width="330.0" height="44.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="235.0" y="230.0" font-size="12" text-anchor="middle">SysTick · PendSV (커널 자신)</text>
<text x="415.0" y="223.0" font-size="12">tick · 컨텍스트 스위치는</text>
<text x="415.0" y="241.0" font-size="12">가장 낮은 우선순위</text>
<line x1="60.0" y1="112.0" x2="670.0" y2="112.0" stroke="#d0564a" stroke-width="2" stroke-dasharray="4 3"/>
<text x="70.0" y="106.0" font-size="12">configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x20</text>
<text x="415.0" y="106.0" font-size="12">선 아래만 FromISR API 허용</text>
<text x="20.0" y="290.0" font-size="12">태스크 (Thread 모드) — taskENTER_CRITICAL()은 BASEPRI = 0x20으로 이 선 아래를 막는다</text>
<text x="20.0" y="315.0" font-size="12">하이브리드: 가장 빡빡한 루프만 선 위에, 나머지는 RTOS 태스크로 (자세히 B3 · E2)</text>
</svg>
```

그림 6 — 하이브리드의 우선순위 지도(FreeRTOS 템플릿 값). 빨간 점선 위의 ISR은 커널이 절대 막지 않지만, 그 대가로 FreeRTOS API를 하나도 부를 수 없다.

대가도 분명하다.

- zero-latency ISR에서 `xQueueSendFromISR` 같은 API를 부르면 커널 자료구조가 깨진다(임계구역 보호 밖이니까). FreeRTOS는 `configASSERT`가 켜져 있으면 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`로 잡아 준다(`port.c`의 `vPortValidateInterruptPriority`).
- 태스크에 알리고 싶으면 **락 없는 수단**을 쓴다: 단일 생산자 · 단일 소비자 링버퍼(D8), 또는 zero-latency ISR이 kernel-aware 우선순위의 소프트웨어 인터럽트를 pend해서(`NVIC->STIR` 또는 ISPR 쓰기) 그 ISR이 대신 태스크를 깨우게 한다.
- 이 ISR의 WCET는 내가 책임진다. RTOS는 아무 도움도 안 준다 — 즉 **이 부분은 bare-metal**이다.

**Don 경험과 연결**: SSD에서 NAND 채널 인터럽트나 DMA 완료처럼 "절대 늦으면 안 되는" 경로를 다른 처리와 분리해 높은 우선순위에 두고, 나머지는 실행부 루프에 넘겼다면 — 그게 같은 구조다. 이름만 "zero-latency ISR + RTOS 태스크"로 바뀌었다.

---

## 8. 메모리 — FLASH · RAM과 태스크 하나의 값

자세한 분해(커널 파일별 크기, 링커 map 읽기)는 A3가 다룬다. 여기서는 결정에 필요한 만큼만 본다: **최소 앱의 크기**와 **태스크 하나를 더할 때의 RAM**.

### 예제 10 — 세 최소 앱의 크기, 태스크 하나의 RAM

무엇을 확인하나: (1) bare-metal "hello", (2) FreeRTOS에서 스택 크기를 바꿔 가며 `xTaskCreate()` 전후의 `xPortGetFreeHeapSize()` 차이, (3) Zephyr "hello"에서 `struct k_thread` 크기를 찍고, 셋을 `arm-none-eabi-size`로 비교한다.

```c
/* min_bm.c — smallest bare-metal app: one line out */
#include "board.h"
int main(void) { board_puts("hello from bare-metal\n"); return 0; }
```

```c
/* ex9 — FreeRTOS: RAM cost of one more task (heap_4 accounting) */
#include "FreeRTOS.h"
#include "task.h"
#include "a7.h"
static void dummy(void *arg) { (void)arg; for (;;) vTaskDelay(portMAX_DELAY); }

static void probe(void *arg)
{
    (void)arg;
    for (unsigned words = 128; words <= 512; words *= 2) {
        size_t before = xPortGetFreeHeapSize();
        TaskHandle_t h;
        xTaskCreate(dummy, "d", words, NULL, 1, &h);
        size_t used = before - xPortGetFreeHeapSize();
        board_printf_raw("stack %u words (%u B): heap used %u B -> TCB+overhead %u B\n",
                         words, words * 4u, (unsigned)used, (unsigned)(used - words * 4u));
        vTaskDelete(h);
        vTaskDelay(1);                       /* let idle free the deleted task */
    }
    board_printf_raw("sizeof(StaticTask_t)=%u\n", (unsigned)sizeof(StaticTask_t));
    board_exit(0);
}

int main(void)
{
    xTaskCreate(probe, "probe", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```c
/* z_min — smallest Zephyr app + thread object sizes */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
int main(void)
{
    printk("hello from zephyr: sizeof(struct k_thread)=%u, K_THREAD_STACK_LEN(512)=%u\n",
           (unsigned)sizeof(struct k_thread), (unsigned)K_THREAD_STACK_LEN(512));
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/A7; O=/var/tmp/rtos/A7; 
make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/min_bm.c OUT=$O/min_bm >/dev/null;
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex9_frt_ram.c OUT=$O/ex9 >/dev/null;
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="$S/ex9_frt_ram.c $S/lean/lean.c" OUT=$O/ex9_lean16 EXTRA_CFLAGS=-DconfigTOTAL_HEAP_SIZE=16384 >/dev/null;
/var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_min $O/z_min; /var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_min $O/z_min_lean -- -DCONFIG_TIMESLICING=n -DCONFIG_ARM_MPU=n -DCONFIG_PRINTK=y;
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex9/app.elf; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/z_min/zephyr/zephyr.elf;
arm-none-eabi-size $O/min_bm/app.elf $O/ex9/app.elf $O/ex9_lean16/app.elf $O/z_min/zephyr/zephyr.elf $O/z_min_lean/zephyr/zephyr.elf'
```

```text
           FLASH:       12552 B         4 MB      0.30%
             RAM:        6112 B         4 MB      0.15%
           FLASH:       11392 B         4 MB      0.27%
             RAM:        6024 B         4 MB      0.14%
stack 128 words (512 B): heap used 624 B -> TCB+overhead 112 B
stack 256 words (1024 B): heap used 1136 B -> TCB+overhead 112 B
stack 512 words (2048 B): heap used 2160 B -> TCB+overhead 112 B
sizeof(StaticTask_t)=96
[board_exit] code=0
hello from zephyr: sizeof(struct k_thread)=128, K_THREAD_STACK_LEN(512)=512
   text	   data	    bss	    dec	    hex	filename
   4140	     96	    328	   4564	   11d4	/var/tmp/rtos/A7/min_bm/app.elf
  12224	    100	 135400	 147724	  2410c	/var/tmp/rtos/A7/ex9/app.elf
  12040	    100	  20672	  32812	   802c	/var/tmp/rtos/A7/ex9_lean16/app.elf
  12384	    168	   5943	  18495	   483f	/var/tmp/rtos/A7/z_min/zephyr/zephyr.elf
  11228	    164	   5861	  17253	   4365	/var/tmp/rtos/A7/z_min_lean/zephyr/zephyr.elf
```

출력에서 볼 것:

- **FLASH**: bare-metal 4.1 KB(그중 대부분은 lab의 UART/newlib-nano 지원 코드), FreeRTOS 12.0~12.2 KB, Zephyr 11.2~12.4 KB. 커널 자체는 **8 KB 안팎**이다. 128 KB 플래시 MCU에서도 문제 되지 않는 크기다. 16~32 KB 플래시의 아주 작은 MCU(ESC용 Cortex-M0 등)에서는 의미 있는 비율이 된다.
- **RAM**: FreeRTOS 기본의 135 KB는 커널이 아니라 `configTOTAL_HEAP_SIZE`(128 KB)다. 16 KB heap이면 20.7 KB. Zephyr 6 KB 안에는 ISR 스택 4 KB(보드 기본값 `CONFIG_ISR_STACK_SIZE=4096`), main 스택 1 KB, idle 스택 256 B가 들어 있다. 즉 **RTOS의 RAM은 대부분 스택**이다.
- **태스크 하나 = TCB + 스택**: FreeRTOS TCB 96 B(이 설정의 `StaticTask_t`) + heap_4 블록 헤더 8 B × 2(TCB와 스택을 따로 할당) = 112 B, 그리고 스택. Zephyr `struct k_thread` 128 B + 스택(`K_THREAD_DEFINE`이면 정적 배치). 실제로는 **스택이 TCB보다 5~20배 크다**. 태스크 10개 × 1 KB 스택 = 10 KB — 64 KB RAM MCU에서 이게 "RTOS를 쓸까"의 실제 질문이다. bare-metal은 스택 하나(MSP)를 모두가 공유한다.

**흔한 함정**: "FreeRTOS는 RAM을 135 KB나 먹는다"는 잘못된 결론이다. heap 크기는 내가 정한다. 반대로 "커널은 몇 KB밖에 안 된다"만 보고 태스크를 20개 만들면, 스택 합계가 RAM을 다 먹는다. 스택 크기는 high-water mark로 재서 정한다(F3).

---

## 9. Linux 기준값 — 다시 재지 않고 인용 + VM에서 두 줄

Linux 쪽 측정은 EL N1 · N3 · N4에 있다. 이 노트의 비교 차트에 넣을 값만 가져온다(모두 QEMU TCG 게스트 또는 공유 VM, 부하 상태 — 절대값은 실제 보드가 아니다).

| 항목 | 값 | 출처 |
|---|---|---|
| 부팅 → BusyBox 첫 셸 | 8.91 s | EL N4 §1.4 (6.18 커널, TCG) |
| cyclictest 1 kHz, PREEMPT, idle | min 68~72 µs, max 44~55 ms | EL N1 예제 5 |
| cyclictest 1 kHz, PREEMPT_RT, 부하 | min 45~55 µs, max 54~58 ms | EL N1 예제 6 |
| 실제 하드웨어 + PREEMPT_RT 튜닝의 흔한 보고 범위 | 최악 수십 ~ 100 µs대 | EL N1 §10.3 (기억 기반 범위) |

여기에 더해 VM 호스트(Lima `elinux`, 6 vCPU, 측정 당시 load average 약 9~10 — 다른 작성자들의 빌드가 돌고 있었다)에서 RTOS 예제와 같은 두 가지를 쟀다: **같은 CPU에 묶은 두 스레드의 전환**과 **1 kHz 주기 스레드의 깨어남 지연**.

### 예제 11 — Linux 스레드 전환과 1 kHz 깨어남 (VM 호스트)

무엇을 확인하나: 파이프 두 개로 핑퐁하는 두 스레드를 CPU0에 고정해 전환 비용을 재고, `clock_nanosleep(TIMER_ABSTIME)` 1 ms 주기 3000번의 늦음을 잰다. 한 번은 일반(`SCHED_OTHER`), 한 번은 `sudo chrt -f 80`(SCHED_FIFO 80).

```c
/* linux_ref.c — Linux reference on the VM host: thread switch cost + 1 ms wake-up latency */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#define N 100000
static int p1[2], p2[2];

static uint64_t ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void pin0(void) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(0, &s); sched_setaffinity(0, sizeof s, &s); }

static void *pong(void *a)
{
    (void)a; pin0(); char c;
    for (int i = 0; i < N; i++) { if (read(p1[0], &c, 1) != 1 || write(p2[1], &c, 1) != 1) break; }
    return NULL;
}

int main(void)
{
    pin0();                                         /* both threads on CPU0: real switches */
    if (pipe(p1) || pipe(p2)) return 1;
    pthread_t t; pthread_create(&t, NULL, pong, NULL);
    char c = 'x';
    uint64_t t0 = ns();
    for (int i = 0; i < N; i++) { if (write(p1[1], &c, 1) != 1 || read(p2[0], &c, 1) != 1) return 1; }
    uint64_t dt = ns() - t0;
    pthread_join(t, NULL);
    printf("pipe ping-pong: %.0f ns per switch (write+read+switch)\n", (double)dt / (2.0 * N));

    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    uint64_t mn = ~0ull, mx = 0, sum = 0; int n = 3000;
    for (int i = 0; i < n; i++) {                   /* 1 kHz periodic, absolute deadlines */
        next.tv_nsec += 1000000; if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        uint64_t late = ns() - ((uint64_t)next.tv_sec * 1000000000ull + (uint64_t)next.tv_nsec);
        if (late < mn) mn = late;
        if (late > mx) mx = late;
        sum += late;
    }
    printf("1 kHz wake-up latency: min %llu avg %llu max %llu ns (n=%d, policy=%s)\n",
           (unsigned long long)mn, (unsigned long long)(sum / n), (unsigned long long)mx, n,
           sched_getscheduler(0) == SCHED_FIFO ? "SCHED_FIFO" : "SCHED_OTHER");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'nproc; uptime; uname -r; gcc -O2 -Wall -Wextra -pthread /Users/donh/.rtos_scratch/A7/linux_ref.c -o /var/tmp/rtos/A7/linux_ref && /var/tmp/rtos/A7/linux_ref && sudo chrt -f 80 /var/tmp/rtos/A7/linux_ref'
```

```text
6
 01:54:35 up 7 days, 10:43,  1 user,  load average: 9.33, 10.20, 6.32
7.0.0-38-generic
pipe ping-pong: 3796 ns per switch (write+read+switch)
1 kHz wake-up latency: min 9952 avg 135601 max 4789683 ns (n=3000, policy=SCHED_OTHER)
pipe ping-pong: 778 ns per switch (write+read+switch)
1 kHz wake-up latency: min 3430 avg 178848 max 2908919 ns (n=3000, policy=SCHED_FIFO)
```

출력에서 볼 것:

- **전환 0.78~3.8 µs** — 숫자만 보면 RTOS(25 MHz)와 비슷하거나 빠르다. 하지만 이건 **GHz급 Apple 실리콘 코어**에서 잰 값이다. 사이클로 환산하면 수천 사이클이고, 같은 일을 25 MHz MCU의 RTOS는 50~300사이클에 한다. Linux의 전환은 시스템 콜 진입, 주소 공간(같은 프로세스라 TLB는 유지), 스케줄러 클래스, 파이프 버퍼를 지난다.
- **1 kHz 깨어남 최악 2.9 ms (FIFO) / 4.8 ms (일반)** — 평균도 136~179 µs다. 공유 VM이라 과장됐지만(EL N1 §6.2 "VM에서 RT를 증명하려 하지 말 것"), **모양**은 EL N1 · N3과 같다: 중앙값은 괜찮은데 꼬리가 수백~수천 배다. RTOS 예제의 max/min 비는 1.005였다(8.28 vs 8.32 µs).

이 두 줄이 Linux를 고르는 이유와 안 고르는 이유를 다 보여 준다. 생태계와 GHz 코어의 처리량은 압도적이지만, **최악값은 보장하지 않는다.** 자세한 이유(IRQ off 구간, 선점 off, page fault, 하이퍼바이저의 vCPU 탈취)는 EL N1, 언제 Linux에 맡겨도 되는지는 EL N4.

---

## 10. 종합 — 한 장의 차트와 비용/이득 표

### 10.1 측정 차트

```svg
<svg viewBox="0 0 680 376" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="250.0" y1="30.0" x2="250.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="250.0" y="24.0" font-size="12" text-anchor="middle">10 ns</text>
<line x1="315.0" y1="30.0" x2="315.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="315.0" y="24.0" font-size="12" text-anchor="middle">100 ns</text>
<line x1="380.0" y1="30.0" x2="380.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="380.0" y="24.0" font-size="12" text-anchor="middle">1 µs</text>
<line x1="445.0" y1="30.0" x2="445.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="445.0" y="24.0" font-size="12" text-anchor="middle">10 µs</text>
<line x1="510.0" y1="30.0" x2="510.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="510.0" y="24.0" font-size="12" text-anchor="middle">100 µs</text>
<line x1="575.0" y1="30.0" x2="575.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="575.0" y="24.0" font-size="12" text-anchor="middle">1 ms</text>
<line x1="640.0" y1="30.0" x2="640.0" y2="326.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
<text x="640.0" y="24.0" font-size="12" text-anchor="middle">10 ms</text>
<text x="242.0" y="55.0" font-size="12" text-anchor="end">bare-metal IRQ→ISR</text>
<rect x="250.0" y="42.0" width="58.7" height="16.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="314.7" y="55.0" font-size="12">80 ns</text>
<text x="242.0" y="81.0" font-size="12" text-anchor="end">FreeRTOS IRQ→ISR</text>
<rect x="250.0" y="68.0" width="70.1" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="326.1" y="81.0" font-size="12">120 ns</text>
<text x="242.0" y="107.0" font-size="12" text-anchor="end">Zephyr IRQ→ISR</text>
<rect x="250.0" y="94.0" width="94.1" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="350.1" y="107.0" font-size="12">280 ns</text>
<text x="242.0" y="133.0" font-size="12" text-anchor="end">bare-metal ISR→루프(한가)</text>
<rect x="250.0" y="120.0" width="113.6" height="16.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="369.6" y="133.0" font-size="12">560 ns</text>
<text x="242.0" y="159.0" font-size="12" text-anchor="end">Zephyr IRQ→스레드 (slice off)</text>
<rect x="250.0" y="146.0" width="176.5" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="432.5" y="159.0" font-size="12">5.20 µs</text>
<text x="242.0" y="185.0" font-size="12" text-anchor="end">FreeRTOS IRQ→태스크 (lean)</text>
<rect x="250.0" y="172.0" width="184.4" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="440.4" y="185.0" font-size="12">6.88 µs</text>
<text x="242.0" y="211.0" font-size="12" text-anchor="end">FreeRTOS IRQ→태스크 (기본)</text>
<rect x="250.0" y="198.0" width="189.7" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="445.7" y="211.0" font-size="12">8.28 µs</text>
<text x="242.0" y="237.0" font-size="12" text-anchor="end">Zephyr IRQ→스레드 (기본)</text>
<rect x="250.0" y="224.0" width="213.4" height="16.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="469.4" y="237.0" font-size="12">19.20 µs</text>
<text x="242.0" y="263.0" font-size="12" text-anchor="end">kernel-aware IRQ, 임계구역 중 max</text>
<rect x="250.0" y="250.0" width="240.9" height="16.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="496.9" y="263.0" font-size="12">50.84 µs</text>
<text x="242.0" y="289.0" font-size="12" text-anchor="end">bare-metal ISR→루프(바쁨) max</text>
<rect x="250.0" y="276.0" width="290.8" height="16.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="546.8" y="289.0" font-size="12">297.80 µs</text>
<text x="242.0" y="315.0" font-size="12" text-anchor="end">Linux VM 1 kHz 깨어남 max</text>
<rect x="250.0" y="302.0" width="355.1" height="16.0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="611.1" y="315.0" font-size="12">2.91 ms</text>
<text x="10.0" y="346.0" font-size="12">로그 축. QEMU mps2-an385 ICOUNT=1 실측 (Linux만 VM 호스트 실측, load avg 약 10)</text>
<text x="10.0" y="366.0" font-size="12">파랑 = bare-metal, 초록 = FreeRTOS, 주황 = Zephyr, 빨강 = Linux</text>
</svg>
```

그림 7 — 이 노트에서 잰 지연을 한 축(로그)에 모았다. 왼쪽 위 세 줄(IRQ → ISR)은 실행 환경과 거의 무관하고, 차이는 **ISR 다음**에서 생긴다. bare-metal은 한가할 때 가장 빠르고 바쁠 때 가장 느리다. RTOS는 그 사이에서 **일정**하다. Linux는 꼬리가 ms 단위다.

### 10.2 비용/이득 표 (측정값)

| 항목 | bare-metal | FreeRTOS V11.3.2 (기본 → lean) | Zephyr v4.4.2 (기본 → slicing off) | Linux (참고) |
|---|---|---|---|---|
| reset → 사용자 코드 | 15.6 µs | 첫 태스크 1515 µs (heap 128 KB) → 367 µs (heap 16 KB) | main 205 µs → 178 µs (MPU도 off) | 8.91 s (EL N4, TCG) |
| IRQ → ISR 첫 줄 | 80 ns | 120 ns | 280 ns | 커널이 받음 (EL N1) |
| IRQ → 처리 코드, 한가 | 0.56 µs | 8.28 → 6.88 µs | 19.2 → 5.2 µs | VM 1 kHz min 3.4 µs |
| IRQ → 처리 코드, 다른 일 300 µs 중 (max) | 297.8 µs | 8.32 → 6.92 µs | 19.24 → 5.2 µs | VM 1 kHz max 2.9 ms |
| 전환 1회 (순수) | 함수 호출 0.22 µs | 3.33 → 1.92 µs | 16.7 → 2.66 µs | 0.78~3.8 µs (GHz 코어) |
| 메시지 왕복 1회당 | 플래그 · 링버퍼 | 13.4 → 12.0 µs | 21.2 → 7.6 µs | — |
| tick 세금 | 없음 | 1.47 µs/tick (10 kHz → 1.47%) | 약 4.5 µs/tick (10 kHz → 4.45%), tickless 0% | — |
| FLASH (text) | 4.1 KB (대부분 보드 지원) | 12.2 → 12.0 KB | 12.4 → 11.2 KB | 커널만 수 MB (일반적 규모) |
| RAM (data + bss) | 0.4 KB | 135.5 KB (heap 128 KB) → 20.8 KB | 6.1 → 6.0 KB | 수십 MB 이상 (일반적 규모) |
| 태스크 하나 더 | 함수 하나 (스택 공유) | TCB 96 B + 헤더 16 B + 스택 | `k_thread` 128 B + 스택 | 스레드 스택 기본 MB 단위 가상 |
| 얻는 것 | 최소 지연 · 최소 메모리 · 검증 쉬움 | 일정한 최악 지연, 블로킹 API, 작은 커널, 거대한 사용자층 | 일정한 최악 지연 + 드라이버 · DT · 네트워크 · 보안 · 테스트 생태계 | 생태계 전체, MMU 격리, GHz 처리량 |
| 잃는 것 | 최악 지연이 기능 추가마다 변함, 블로킹 I/O 불가 | 수 µs 깨움 비용, 태스크마다 스택, 설정 의존 | 같은 + 기본 설정이 무거움(slicing), 학습 곡선 | 최악값 보장 없음, 초 단위 부팅, 인증 어려움 |

(Linux 행의 "스레드 스택 기본 MB 단위 가상"은 glibc 기본 스택 크기가 보통 8 MB 가상 영역이라는 일반적 사실이다 — 실제 RAM은 쓴 만큼만 잡힌다.)

### 예제 12 — 숫자를 설계 예산으로 바꾸기 (Python)

무엇을 확인하나: 잰 값을 사이클로 환산하고, 8 kHz rate loop(주기 125 µs)와 20 kHz 전류 루프(50 µs)에서 깨우는 비용이 주기의 몇 %인지, Zephyr tick 한 번의 비용, 그리고 같은 명령 수가 더 빠른 코어에서 얼마일지(클럭 비례만 — 캐시 · wait state 무시)를 계산한다.

```python
# derive.py — turn the measured QEMU numbers (25 MHz, ICOUNT=1) into design budgets
CYC_NS = 40                      # 25 MHz
meas_us = {                      # IRQ -> code that does the work (avg, us)
    "bare-metal flag (idle loop)": 0.56,
    "FreeRTOS notify (lean)":      6.88,
    "FreeRTOS notify (default)":   8.28,
    "Zephyr k_sem (no slicing)":   5.20,
    "Zephyr k_sem (default)":      19.2,
}
print(f"{'path':30s} {'cycles':>7s} {'% of 8 kHz':>10s} {'% of 20 kHz':>11s}")
for k, us in meas_us.items():
    cyc = us * 1000 / CYC_NS
    print(f"{k:30s} {cyc:7.0f} {100*us/125:9.1f}% {100*us/50:10.1f}%")

# per-tick cost from the busy-counter runs (lost ns over a 200 ms window / ticks)
for name, lost_ns, ticks in [("Zephyr periodic 100 Hz", 170303, 20),
                             ("Zephyr periodic 1 kHz", 965183, 200),
                             ("Zephyr periodic 10 kHz", 8913981, 2000)]:
    print(f"{name:24s} {lost_ns/ticks/1000:5.2f} us/tick")

# what one RTOS wake-up costs if the same instruction count ran on faster cores
# (rough: ignores flash wait states, caches, bus — only clock scaling)
for mhz in (25, 170, 480):
    print(f"{mhz:3d} MHz core: FreeRTOS lean wake ~ {6.88*25/mhz:5.2f} us, "
          f"10 kHz tick ~ {1.47*25/mhz*10000/1e4:5.3f}% CPU")
```

```sh
limactl shell elinux -- python3 -I /Users/donh/.rtos_scratch/A7/derive.py
```

```text
path                            cycles % of 8 kHz % of 20 kHz
bare-metal flag (idle loop)         14       0.4%        1.1%
FreeRTOS notify (lean)             172       5.5%       13.8%
FreeRTOS notify (default)          207       6.6%       16.6%
Zephyr k_sem (no slicing)          130       4.2%       10.4%
Zephyr k_sem (default)             480      15.4%       38.4%
Zephyr periodic 100 Hz    8.52 us/tick
Zephyr periodic 1 kHz     4.83 us/tick
Zephyr periodic 10 kHz    4.46 us/tick
 25 MHz core: FreeRTOS lean wake ~  6.88 us, 10 kHz tick ~ 1.470% CPU
170 MHz core: FreeRTOS lean wake ~  1.01 us, 10 kHz tick ~ 0.216% CPU
480 MHz core: FreeRTOS lean wake ~  0.36 us, 10 kHz tick ~ 0.077% CPU
```

출력에서 볼 것:

- 25 MHz에서 20 kHz 루프를 RTOS 태스크로 깨우면 주기의 **14~38%**가 깨우는 데 든다. 이건 안 된다 → 그 루프는 ISR 안에서(§7).
- 같은 일을 170 MHz(STM32G4급) 코어에 올리면 깨움 약 1 µs = 50 µs 주기의 2%. 480 MHz(STM32H7급)에서는 0.36 µs. **빠른 MCU에서는 RTOS 비용이 거의 사라진다.** 그래서 H7 드론 FC에서 NuttX(PX4)가 8 kHz 루프를 돌려도 문제없고, 작은 M0 ESC에서는 RTOS를 거의 안 쓴다. 판단 기준은 "RTOS가 느리냐"가 아니라 **"깨움 비용이 가장 짧은 주기의 몇 %냐"**다. 경험적 기준으로 수 % 이하면 태스크, 10%를 넘으면 ISR로 본다.
- Zephyr 100 Hz에서 tick당 8.5 µs로 더 큰 것은 20 ms 슬라이스 만료 처리가 2 tick마다 겹치기 때문이다. §6에서 slicing을 끄고 다시 재면 100 Hz에서 tick당 3.2 µs였다(1 kHz 이상에서는 슬라이스 만료가 tick 수에 묻힌다).
- 클럭 비례 환산은 **하한 추정**이다. 실제 칩은 플래시 wait state(H7에서 수 사이클), 캐시 미스, 버스 경합 때문에 더 걸린다. 실제 칩에서는 GPIO 토글 + scope 또는 DWT CYCCNT로 다시 잰다(E3).

---

## 11. 언제 무엇이 이기나

### 11.1 bare-metal이 이길 때

- **코어의 일이 "주기 루프 하나 + ISR 몇 개"로 끝날 때.** ESC, FOC 모터 드라이버, LED 드라이버, 단순 센서 노드. 독립된 주기가 하나뿐이면 스케줄러가 할 일이 없다.
- **가장 짧은 deadline이 RTOS 깨움 비용의 10배도 안 될 때** (§10: 25 MHz에서 20 kHz 루프). 이때 루프는 어차피 ISR 안에 있어야 하고, 나머지가 작으면 RTOS가 필요 없다.
- **MCU가 아주 작을 때.** RAM 4~8 KB면 태스크 스택 몇 개로 끝난다. 플래시 16~32 KB면 커널 8 KB가 아프다.
- **안전 · 검증을 단순하게 하고 싶을 때.** 슈퍼 루프 + 고정 시간표(cyclic executive, C11)는 실행 순서가 코드에 그대로 보인다. WCET 분석, 커버리지, 리뷰가 쉽다. 인증된 RTOS를 사거나 인증 근거를 만드는 비용도 없다.
- **결정적 슈퍼 루프로 충분할 때**: 모든 일을 짧은 조각으로 쪼개고(예: 300 µs 일을 50 µs × 6), 각 조각이 "다음 마감 전에 끝날 때만" 실행하는 예산 스케줄러를 두면 슈퍼 루프의 최악 지연을 통제할 수 있다. Betaflight가 정확히 이렇게 한다(§14).

### 11.2 RTOS가 이길 때

- **독립된 주기가 여러 개일 때.** IMU 1 kHz, 기압계 50 Hz, GNSS 10 Hz, 텔레메트리 50 Hz, 로그 비주기. 슈퍼 루프에서 이걸 다 맞추려면 손으로 시간표를 짜고, 하나 바뀔 때마다 다시 짠다. RTOS에서는 태스크마다 `vTaskDelayUntil()` 하나다.
- **블로킹 I/O가 있을 때.** "SPI 전송 끝날 때까지 기다린다", "UART에서 한 줄이 올 때까지 기다린다"를 슈퍼 루프에서는 상태 기계로 쪼개야 한다. RTOS에서는 그냥 기다리고(블록), 그동안 CPU는 다른 태스크로 간다. 코드가 순서대로 읽힌다.
- **스택 · 미들웨어가 필요할 때.** TCP/IP(lwIP, Zephyr net), USB 스택, BLE, 파일시스템, MQTT. 대부분이 스레드와 세마포어를 전제로 만들어져 있다.
- **팀이 커질 때.** 기능마다 태스크로 나누면 사람마다 독립적으로 작업하고, 우선순위표가 인터페이스 계약이 된다. 슈퍼 루프는 누군가 300 µs짜리 일을 넣는 순간 모두의 지연이 바뀐다(§3 예제 2).
- **우리 측정이 말해 주는 것**: RTOS는 지연을 **일정**하게 만든다(8.28 ± 0.04 µs). 이 "일정함"이 RTOS의 진짜 상품이다.

### 11.3 Linux가 이길 때 (EL N4 참조만)

MMU · 수백 MB RAM · 리치 스택(카메라 파이프라인, ML 추론, 파일시스템, 네트워크 전체, 컨테이너, OTA)이 필요할 때다. 기준과 패턴은 EL N4 §1 · §3, 최악값의 성질은 EL N1. 이 노트에서 덧붙일 말은 하나다: Linux가 필요하다고 MCU 쪽 판단이 끝나는 게 아니다. **Linux 옆의 MCU(또는 같은 SoC의 M/R 코어)에서 다시 이 노트의 판단을 한다.**

### 11.4 하이퍼바이저 · 분리 커널이 이길 때 (개념만 — O6 · P6이 자세히)

- **한 SoC에 중요도가 다른 소프트웨어를 섞어야 할 때**(mixed criticality). 예: 자동차 존 컨트롤러에서 ASIL D 제어(브레이크 관련 신호 처리)와 QM(안전 비관련) 진단 · 통신을 같은 다중 코어 칩에 올리면서, 한쪽 버그가 다른 쪽을 망가뜨리지 못하게 해야 한다.
- **MCU급 코어에도 하이퍼바이저가 들어온다.** Armv8-R(Cortex-R52/R52+)은 EL2를 가지고 있고, EL2용 MPU로 **2단계 메모리 보호**를 한다. 그래서 MMU 없이도 파티션마다 RTOS(또는 bare-metal) 게스트를 격리할 수 있다(B9). 오픈소스 Bao 하이퍼바이저는 정적 분할(스케줄러 없음, vCPU 하나 = 물리 CPU 하나)을 하고, Armv8-R AArch32 지원 플랫폼으로 Arm FVP, MPS3-AN536, NXP S32Z270 등을 README에 올려 두었다(2026-10-25 확인).
- **비용**: 파티션 사이 통신 설계, 공유 자원(캐시, 버스, 주변장치) 간섭, 툴체인 · 디버그 복잡도. 그리고 인증된 상용 분리 커널(PikeOS, INTEGRITY 등 — A6 · O2)이 비싸다.
- 판단 질문: "격리가 필요한가?" → 칩을 나눌 수 있으면(외부 safety MCU, EL N4 패턴 b) 그게 보통 더 단순하다. **칩을 나눌 수 없을 때**(BOM, 면적, 지연, 대역폭) 하이퍼바이저가 답이 된다.

---

## 12. MCU 쪽 결정 흐름

EL N4의 흐름도는 "이 기능을 Linux와 MCU 중 어디에 두나"였다. 이 흐름도는 그다음 질문 — **"이 MCU(이 코어)에서 무엇으로 돌리나"** — 이다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20.0" y="20.0" width="400.0" height="60.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30.0" y="44.0" font-size="12">1. 이 코어의 일 = 주기 루프 하나 + ISR 몇 개?</text>
<text x="30.0" y="62.0" font-size="12">(여러 독립 주기 · 블로킹 I/O · 프로토콜 스택이 없다)</text>
<line x1="420.0" y1="50.0" x2="488.0" y2="50.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="454.0" y="44.0" font-size="12" text-anchor="middle">예</text>
<rect x="490.0" y="25.0" width="180.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="580.0" y="44.0" font-size="12" text-anchor="middle">bare-metal</text>
<text x="580.0" y="62.0" font-size="12" text-anchor="middle">super-loop / ISR 구동</text>
<line x1="220.0" y1="80.0" x2="220.0" y2="108.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="232.0" y="100.0" font-size="12">아니오</text>
<rect x="20.0" y="110.0" width="400.0" height="60.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30.0" y="134.0" font-size="12">2. MMU · 수백 MB RAM · 리치 스택이 필요?</text>
<text x="30.0" y="152.0" font-size="12">(카메라 · ML · 파일시스템 · 네트워크 전부)</text>
<line x1="420.0" y1="140.0" x2="488.0" y2="140.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="454.0" y="134.0" font-size="12" text-anchor="middle">예</text>
<rect x="490.0" y="115.0" width="180.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="580.0" y="134.0" font-size="12" text-anchor="middle">Linux</text>
<text x="580.0" y="152.0" font-size="12" text-anchor="middle">EL N4로 분할 설계</text>
<line x1="220.0" y1="170.0" x2="220.0" y2="198.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="232.0" y="190.0" font-size="12">아니오</text>
<rect x="20.0" y="200.0" width="400.0" height="60.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30.0" y="224.0" font-size="12">3. 중요도가 다른 SW를 한 칩에 섞어야?</text>
<text x="30.0" y="242.0" font-size="12">(ASIL D 제어 + QM 인포 · 서로 격리)</text>
<line x1="420.0" y1="230.0" x2="488.0" y2="230.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="454.0" y="224.0" font-size="12" text-anchor="middle">예</text>
<rect x="490.0" y="205.0" width="180.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="580.0" y="224.0" font-size="12" text-anchor="middle">하이퍼바이저</text>
<text x="580.0" y="242.0" font-size="12" text-anchor="middle">분리 커널 (O6 · P6)</text>
<line x1="220.0" y1="260.0" x2="220.0" y2="288.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="232.0" y="280.0" font-size="12">아니오</text>
<rect x="20.0" y="290.0" width="400.0" height="60.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30.0" y="314.0" font-size="12">4. 인증 · 기존 코드 · 팀이 정한 RTOS가 있나?</text>
<text x="30.0" y="332.0" font-size="12">(SafeRTOS · 벤더 SDK · NuttX/PX4)</text>
<line x1="420.0" y1="320.0" x2="488.0" y2="320.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="454.0" y="314.0" font-size="12" text-anchor="middle">예</text>
<rect x="490.0" y="295.0" width="180.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="580.0" y="314.0" font-size="12" text-anchor="middle">그 RTOS</text>
<text x="580.0" y="332.0" font-size="12" text-anchor="middle">인증본 / SDK 기본</text>
<line x1="220.0" y1="350.0" x2="220.0" y2="378.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="232.0" y="370.0" font-size="12">아니오</text>
<rect x="20.0" y="380.0" width="400.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="220.0" y="399.0" font-size="12" text-anchor="middle">RTOS — FreeRTOS(작고 단순) / Zephyr(드라이버 · DT · 보안)</text>
<text x="220.0" y="417.0" font-size="12" text-anchor="middle">선택은 A6, 측정 비용은 이 노트 §10</text>
<rect x="490.0" y="380.0" width="180.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="580.0" y="399.0" font-size="12" text-anchor="middle">+ 가장 짧은 루프는</text>
<text x="580.0" y="417.0" font-size="12" text-anchor="middle">zero-latency ISR로 (§7)</text>
<line x1="420.0" y1="405.0" x2="488.0" y2="405.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah8)"/>
<text x="20.0" y="455.0" font-size="12">기능 하나하나에 적용한다 (N4와 같은 원칙). 한 칩 안에서도 답이 여럿 나온다.</text>
</svg>
```

그림 8 — MCU 쪽 결정 흐름. 위에서부터 묻고, 맨 아래 RTOS로 왔더라도 가장 짧은 루프는 zero-latency ISR로 빼는 하이브리드를 함께 검토한다.

질문마다 무엇을 근거로 답하나:

| 질문 | 근거로 쓸 숫자 · 사실 | 이 노트 |
|---|---|---|
| 1. 일이 루프 하나 + ISR? | 독립 주기 수, 블로킹 I/O 유무, 프로토콜 스택 유무 | §11.1 |
| 2. MMU · 리치 스택 필요? | RAM 수백 MB, 카메라/ML/파일시스템 | §11.3, EL N4 |
| 3. 중요도 섞기? | 안전 등급(ASIL · SIL · DAL)이 다른 SW를 한 칩에 | §11.4 |
| 4. 정해진 RTOS? | 인증 요구, 벤더 SDK, 기존 코드(PX4 = NuttX), 팀 경험 | A6 |
| RTOS 선택 후 | 가장 짧은 주기 대비 깨움 비용 %, tick rate, 태스크 수 × 스택 | §10 |
| 하이브리드 여부 | 깨움 비용이 주기의 10% 이상인 루프가 있나 | §7 |

---

## 13. RTOS 커널 안에서는 무슨 일이 일어나나 — 측정한 비용의 출처

측정한 세 가지 비용(tick, ISR → 태스크, 전환)이 소스의 어디서 나오는지 이름만 짚는다. 한 줄씩 따라가는 것은 C2(컨텍스트 스위치), C5(tick), C1(자료구조)의 몫이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10.0" y="20.0" font-size="13" font-weight="bold">FreeRTOS V11.3.2 (GCC/ARM_CM3)</text>
<rect x="10.0" y="30.0" width="150.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85.0" y="49.0" font-size="12" text-anchor="middle">SysTick 예외</text>
<text x="85.0" y="67.0" font-size="12" text-anchor="middle">(우선순위 0xFF)</text>
<rect x="190.0" y="30.0" width="220.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="300.0" y="49.0" font-size="12" text-anchor="middle">xPortSysTickHandler</text>
<text x="300.0" y="67.0" font-size="12" text-anchor="middle">port.c — BASEPRI 올림</text>
<rect x="440.0" y="30.0" width="230.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="555.0" y="49.0" font-size="12" text-anchor="middle">xTaskIncrementTick</text>
<text x="555.0" y="67.0" font-size="12" text-anchor="middle">tasks.c — 지연 리스트 검사</text>
<line x1="160.0" y1="55.0" x2="188.0" y2="55.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)"/>
<line x1="410.0" y1="55.0" x2="438.0" y2="55.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)"/>
<rect x="10.0" y="100.0" width="150.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="85.0" y="119.0" font-size="12" text-anchor="middle">PendSV 예외</text>
<text x="85.0" y="137.0" font-size="12" text-anchor="middle">(ICSR 비트로 예약)</text>
<rect x="190.0" y="100.0" width="220.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="300.0" y="119.0" font-size="12" text-anchor="middle">xPortPendSVHandler</text>
<text x="300.0" y="137.0" font-size="12" text-anchor="middle">R4-R11 저장/복원</text>
<rect x="440.0" y="100.0" width="230.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="555.0" y="119.0" font-size="12" text-anchor="middle">vTaskSwitchContext</text>
<text x="555.0" y="137.0" font-size="12" text-anchor="middle">pxCurrentTCB 교체</text>
<line x1="160.0" y1="125.0" x2="188.0" y2="125.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)"/>
<line x1="410.0" y1="125.0" x2="438.0" y2="125.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)"/>
<line x1="555.0" y1="80.0" x2="555.0" y2="98.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)" stroke-dasharray="4 3"/>
<text x="565.0" y="93.0" font-size="12">깨울 태스크 있으면</text>
<text x="10.0" y="185.0" font-size="13" font-weight="bold">Zephyr v4.4.2 (cortex_m)</text>
<rect x="10.0" y="195.0" width="150.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="85.0" y="214.0" font-size="12" text-anchor="middle">sys_clock_isr</text>
<text x="85.0" y="232.0" font-size="12" text-anchor="middle">cortex_m_systick.c</text>
<rect x="190.0" y="195.0" width="220.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="300.0" y="214.0" font-size="12" text-anchor="middle">sys_clock_announce</text>
<text x="300.0" y="232.0" font-size="12" text-anchor="middle">kernel/timeout.c</text>
<rect x="440.0" y="195.0" width="230.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="555.0" y="214.0" font-size="12" text-anchor="middle">z_arm_pendsv</text>
<text x="555.0" y="232.0" font-size="12" text-anchor="middle">swap_helper.S</text>
<line x1="160.0" y1="220.0" x2="188.0" y2="220.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)"/>
<rect x="10.0" y="260.0" width="400.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="210.0" y="279.0" font-size="12" text-anchor="middle">k_sem_give → update_cache → z_reset_time_slice</text>
<text x="210.0" y="297.0" font-size="12" text-anchor="middle">→ z_add_timeout → sys_clock_set_timeout (§3 · §5 비용)</text>
<line x1="410.0" y1="285.0" x2="555.0" y2="285.0" stroke="currentColor" stroke-width="1"/>
<line x1="555.0" y1="285.0" x2="555.0" y2="247.0" stroke="currentColor" stroke-width="1" marker-end="url(#ah9)"/>
<text x="565.0" y="275.0" font-size="12">ready_q.cache로</text>
</svg>
```

그림 9 — 측정한 오버헤드의 출처. FreeRTOS는 tick → 지연 리스트 검사 → (필요하면) PendSV 예약 → 전환, Zephyr는 같은 구조에 tickless 타이머 재설정과 time slice 재설정이 붙는다.

### FreeRTOS V11.3.2 (`portable/GCC/ARM_CM3/port.c`, `tasks.c`)

- **tick 세금(§6)**: `SysTick_Handler`(= `xPortSysTickHandler`, `FreeRTOSConfig.h`의 `#define`으로 연결) → `portDISABLE_INTERRUPTS()`(BASEPRI 올림) → `xTaskIncrementTick()` → 깨울 태스크가 있거나 time slicing이 필요하면 `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT`(PendSV 예약) → `portENABLE_INTERRUPTS()`.
- **전환(§5)**: `PendSV_Handler`(= `xPortPendSVHandler`, naked 어셈블리) → PSP 읽기 → R4–R11을 태스크 스택에 저장 → `pxCurrentTCB`에 스택 포인터 기록 → BASEPRI를 올리고 `vTaskSwitchContext()` → 새 `pxCurrentTCB`에서 R4–R11 복원 → PSP 설정 → `bx lr`. 디버그 옵션의 비용은 `vTaskSwitchContext()` 안에서 붙는다: `taskCHECK_FOR_STACK_OVERFLOW()`와 런타임 통계(`portGET_RUN_TIME_COUNTER_VALUE()` = 이 lab에선 `board_now_us()`).
- **ISR → 태스크(§3)**: `vTaskNotifyGiveFromISR()` → 태스크를 ready 리스트로 → 깨운 태스크가 더 높으면 `*pxHigherPriorityTaskWoken = pdTRUE` → `portYIELD_FROM_ISR()`가 PendSV 예약 → ISR이 끝나면 tail-chaining으로 PendSV(B2).

### Zephyr v4.4.2

- **tick**: `sys_clock_isr()`(`drivers/timer/cortex_m_systick.c`) → `sys_clock_announce()`(`kernel/timeout.c`)가 만료된 타임아웃 콜백 실행(슬라이스 만료 포함).
- **전환**: `z_arm_pendsv`(`arch/arm/core/cortex_m/swap_helper.S`) — 현재 스레드의 callee-saved 레지스터를 `struct k_thread`에 저장하고, 미리 정해 둔 `_kernel.ready_q.cache`의 스레드를 복원한다. Zephyr는 "누구로 바꿀지"를 **깨우는 시점**(`update_cache()`)에 정해 두는 구조다.
- **slicing 비용(§3 · §5)**: `update_cache()`(`kernel/sched.c`) → `z_reset_time_slice()`(`kernel/timeslicing.c`) → `z_abort_timeout()` + `z_add_timeout()`(`kernel/timeout.c`) → `sys_clock_set_timeout()`.

### 예제 13 — 핫 패스 함수 크기와 PendSV 핸들러 보기

무엇을 확인하나: 이미 빌드한 ELF에서 위 함수들의 크기를 `nm -S`로 보고, FreeRTOS PendSV 핸들러를 역어셈블한다.

```sh
limactl shell elinux -- bash -lc 'O=/var/tmp/rtos/A7; echo "--- FreeRTOS (ex4_lean)"; arm-none-eabi-nm -S --size-sort $O/ex4_lean/app.elf | grep -E " (xTaskIncrementTick|vTaskSwitchContext|xPortPendSVHandler|xPortSysTickHandler|ulTaskGenericNotifyTake|vTaskGenericNotifyGiveFromISR|xTaskResumeAll|vTaskSuspendAll|xTaskGenericNotify)$"; echo "--- Zephyr (z_cs_default)"; arm-none-eabi-nm -S --size-sort $O/z_cs_default/zephyr/zephyr.elf | grep -E " (z_arm_pendsv|sys_clock_isr|z_reset_time_slice|sys_clock_set_timeout|z_add_timeout|sys_clock_announce|z_impl_k_sem_give|z_impl_k_sem_take|_isr_wrapper|z_arm_int_exit|z_get_next_switch_handle|elapsed|z_abort_timeout|__aeabi_uldivmod|z_time_slice)$"; echo "--- PendSV"; arm-none-eabi-objdump -d --no-show-raw-insn $O/ex4_lean/app.elf | awk "/<PendSV_Handler>:/,/^$/"'
```

```text
--- FreeRTOS (ex4_lean)
00000e90 00000010 T vTaskSuspendAll
000010b8 00000020 T xTaskIncrementTick
000010d8 00000080 T vTaskSwitchContext
000014a4 000000b0 T ulTaskGenericNotifyTake
00001554 00000124 T xTaskGenericNotify
00000ea0 000001ac T xTaskResumeAll
--- Zephyr (z_cs_default)
000017f4 00000014 t elapsed
000007e0 00000020 T z_arm_int_exit
0000088c 00000024 T _isr_wrapper
00000bfc 00000044 t elapsed
00001744 00000044 T z_reset_time_slice
0000082c 00000048 T z_arm_pendsv
000010a4 0000004c T z_impl_k_sem_take
00001788 0000005c T z_time_slice
00000c74 00000060 T sys_clock_isr
0000103c 00000068 T z_impl_k_sem_give
00002a08 0000006a T z_abort_timeout
00000cd4 000000e0 T sys_clock_set_timeout
00001840 00000120 T z_add_timeout
--- PendSV
00002294 <PendSV_Handler>:
    2294:	mrs	r0, PSP
    2298:	isb	sy
    229c:	ldr	r3, [pc, #52]	@ (22d4 <PendSV_Handler+0x40>)
    229e:	ldr	r2, [r3, #0]
    22a0:	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
    22a4:	str	r0, [r2, #0]
    22a6:	push	{r3, lr}
    22a8:	mov.w	r0, #32
    22ac:	msr	BASEPRI, r0
    22b0:	bl	10d8 <vTaskSwitchContext>
    22b4:	mov.w	r0, #0
    22b8:	msr	BASEPRI, r0
    22bc:	ldmia.w	sp!, {r3, lr}
    22c0:	ldr	r1, [r3, #0]
    22c2:	ldr	r0, [r1, #0]
    22c4:	ldmia.w	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
    22c8:	msr	PSP, r0
    22cc:	isb	sy
    22d0:	bx	lr
    22d2:	.short	0x0000
    22d4:	.word	0x20000e60
```

출력에서 볼 것:

- **PendSV 핸들러는 명령 19개**다. `stmdb r0!, {r4-r11}`로 저장, `msr BASEPRI, #32`(= `configMAX_SYSCALL_INTERRUPT_PRIORITY` 0x20)로 커널 임계구역, `bl vTaskSwitchContext`, `ldmia`로 복원. 그러니 lean 설정의 순수 전환 1.92 µs(약 60명령) 중 3분의 1이 이 핸들러이고, 나머지가 `vTaskSwitchContext`(128 B)와 `taskYIELD` 경로다.
- `xTaskIncrementTick`이 32바이트뿐인 것은 GCC가 함수를 쪼갰기 때문이다(`nm`에 `xTaskIncrementTick.part.0` 412 B가 따로 있다). 32바이트 앞부분은 "스케줄러가 멈춰 있으면 pended tick만 세고 끝" 빠른 경로다.
- 블록 경로의 무게: `xTaskResumeAll` 428 B, `ulTaskGenericNotifyTake` 176 B — §5에서 핑퐁이 순수 전환의 6배였던 이유가 크기에서도 보인다.
- Zephyr의 slicing 경로: `z_add_timeout` 288 B + `sys_clock_set_timeout` 224 B + `z_abort_timeout` 106 B + `z_reset_time_slice` 68 B. 이게 전환마다 한 번씩 돈다(slicing on일 때). `z_arm_pendsv` 자체는 72 B로 FreeRTOS와 비슷하다.

---

## 14. 실무에서 다시 보기 — 네 가지 제품

### 14.1 드론 비행 제어기 (STM32H7급): PX4/NuttX vs Betaflight

같은 H7 보드 위에 두 철학이 있다.

**PX4 = RTOS(NuttX)**. PX4 문서는 "NuttX is the primary RTOS for running PX4 on a flight-control board"라고 쓴다. 모듈(센서 드라이버, EKF2, 자세 · 속도 제어기, MAVLink, 로거)은 각자 스택과 우선순위를 가진 **태스크**이거나, 같은 스택과 우선순위를 공유하는 **work queue** 항목이다. work queue 항목끼리는 서로 끊지 못하므로 협력해야 하고, sleep · 블로킹 I/O를 하면 안 된다. 모듈 사이 통신은 공유 메모리 기반 publish/subscribe인 uORB(Q1). 이유: 기능이 **수십 개**이고(미션, 지오펜스, 카메라 트리거, 다양한 센서), 오픈소스 기여자 수백 명이 독립적으로 모듈을 붙여야 한다 — §11.2의 "독립 주기 여러 개 + 팀 확장"이다. 480 MHz H7에서는 RTOS 깨움 비용이 µs 이하라(§10) 8 kHz 루프도 감당된다.

**Betaflight = bare-metal + 시간 예산 협력형 스케줄러**. RTOS가 없다. `src/main/scheduler/scheduler.c`의 `scheduler()`가 run-to-completion 태스크를 고른다. 구조를 소스에서 확인하면 이렇다(2026-10-25 master).

- 자이로 · 필터 · PID(`TASK_GYRO`, `TASK_FILTER`, `TASK_PID`)는 **"complete priority"**: 다음 자이로 마감 시각(`nextTargetCycles`)이 가까워지면 사이클 카운터를 바쁘게 기다렸다가 셋을 차례로 실행한다.
- 자이로가 EXTI 인터럽트를 쓰면 실제 인터럽트 간격을 재서 스케줄러 주기를 자이로에 **고정(lock)**한다("Bring the scheduler into lock with the gyro").
- 나머지 태스크(RX, OSD, 텔레메트리, 블랙박스 …)는 나이(age)에 따라 오르는 동적 우선순위로 고르되, **예상 실행 시간 + 가드가 다음 자이로 마감 전에 들어갈 때만** 실행한다. 예상 시간은 실측으로 갱신된다.

이건 §3 예제 2의 문제("슈퍼 루프의 최악 지연 = 가장 긴 일")를 **선점 없이** 해결한 방식이다: 긴 일을 마감 전에 시작하지 않는다. 대가는 각 태스크가 짧게 끝나도록 쪼개져 있어야 하고, 시간 예측이 틀리면 루프가 밀린다는 점이다. 기능 범위가 레이싱 · 프리스타일 비행에 집중되어 있어서 이 단순함이 맞다. (ArduPilot은 ChibiOS RTOS 위에서 돈다 — Q2.)

면접에서 쓸 문장: "PX4 picked an RTOS because of breadth — many independent modules and contributors. Betaflight picked a bare-metal, gyro-locked cooperative scheduler because of depth — one hard loop, run as fast and as predictably as possible."

### 14.2 ESC · 모터 컨트롤러: bare-metal (+ 필요하면 작은 RTOS 태스크)

FOC 전류 루프 20~40 kHz, 주기 25~50 µs. PWM 타이머가 ADC를 트리거하고, ADC 완료 인터럽트에서 Clarke/Park 변환 → PI → 역변환 → PWM 비교값 갱신을 **ISR 안에서 끝낸다**. §10 계산으로 25 MHz에서는 RTOS 깨움만 주기의 14~38%라 불가능, 170 MHz에서도 2%라 할 이유가 없다. 남는 일(속도 루프 1 kHz, CAN/UART 명령, 온도 감시)이 작으면 슈퍼 루프로 충분하다. 로봇 관절 드라이버처럼 CAN-FD 프로토콜 스택, 진단, 펌웨어 업데이트까지 붙으면 **하이브리드**(§7): 전류 루프 ISR은 zero-latency 우선순위, 나머지는 RTOS 태스크. 이때 전류 루프 → 속도 태스크로 가는 데이터는 SPSC 링버퍼나 더블 버퍼로 넘긴다(D8).

### 14.3 Linux SoC 옆의 센서 허브: Zephyr 또는 FreeRTOS

IMU DRDY 1~8 kHz, 자력계 100 Hz, 기압계 50 Hz, 하드웨어 타임스탬프, Linux로 가는 SPI/UART 링크(프레이밍 · 시간 동기는 EL N4 §5~§7), 펌웨어 업데이트. 독립 주기 여러 개 + 블로킹 버스 I/O + 링크 프로토콜 = §11.2의 전형이다. 설계 예:

| 실행 단위 | 우선순위 | 하는 일 | 근거 |
|---|---|---|---|
| IMU DRDY ISR | kernel-aware 최상위 | 타이머 캡처 값(타임스탬프) 읽고, SPI DMA 시작 | 타임스탬프는 하드웨어 캡처로 — ISR 지연이 끼지 않게 (E5) |
| DMA 완료 ISR | kernel-aware | 샘플을 링버퍼에, IMU 태스크에 통지 | ISR → 태스크 (E6) |
| IMU 태스크 | 높음 | 필터 · 다운샘플 · 패킷화 | 6.9 µs 깨움은 1 kHz 주기의 0.7% |
| 저속 센서 태스크 | 중간 | I2C 블로킹 읽기 50~100 Hz | 블로킹 I/O를 그냥 기다림 |
| 링크 태스크 | 중간 | Linux로 프레임 송신, 명령 수신 | EL N4의 프로토콜 |
| 관리 태스크 | 낮음 | 상태 · 로그 · DFU | 나머지 |

Zephyr를 고르면 센서 드라이버 API, DT, MCUboot, Linux와 같은 DT 개념이 덤으로 오고(H 모듈), FreeRTOS를 고르면 벤더 SDK와 작은 커널이 온다(A6). 어느 쪽이든 **slicing · 디버그 옵션 · tick rate를 측정하고 정한다**(§3 · §6).

### 14.4 SSD 컨트롤러: 왜 범용 RTOS를 피하고 자체 실행부를 쓰나 (Don의 경험)

엔터프라이즈 SSD 컨트롤러는 Cortex-R 여러 개가 front-end(NVMe 명령), FTL, back-end(NAND 채널)로 파이프라인을 이룬다. 많은 컨트롤러 펌웨어가 범용 RTOS 대신 **자체 실행부**(run-to-completion 이벤트 루프 + 하드웨어 큐)를 쓴다. 이 노트의 숫자로 이유를 설명할 수 있다(아래는 일반적인 구조에 대한 설명이고, 특정 회사 제품의 내부는 아니다).

- **명령당 예산이 µs 이하다.** 한 코어가 초당 100만 명령(1M IOPS)을 처리하려면 명령당 1 µs다. 800 MHz Cortex-R에서 FreeRTOS lean의 깨움(25 MHz에서 172사이클)이 클럭 비례로만 쳐도 약 0.2 µs = **예산의 20%**다. 명령마다 블록/깨움을 하면 성능 목표가 무너진다.
- **일이 원래 이벤트 구동이다.** 하드웨어 큐(NVMe SQ, NAND 완료 큐)에서 이벤트를 꺼내 짧은 핸들러를 끝까지 실행하면 된다. "기다림"은 상태 기계의 상태로 표현되고, 스택을 가진 태스크가 필요 없다 → 태스크 스택 RAM이 0이다. TCM이 작으니 이게 크다.
- **결정성과 디버그**: 실행 순서가 코드와 큐에 보인다. 양산 펌웨어의 타이밍 버그를 재현할 때 선점이 없으면 경우의 수가 적다.
- **멀티코어 = 파이프라인**: 동시성은 태스크가 아니라 **코어 분할 + 하드웨어 큐**로 얻는다. RTOS의 SMP 스케줄러가 줄 것이 별로 없다.
- RTOS가 들어가는 곳도 있다: 관리 · 열 제어 · 전원 · 호스트 관리 인터페이스처럼 블로킹 I/O와 여러 주기가 섞인 쪽(일반적 경향 — 제품마다 다름).

면접에서 이 이야기는 강력하다: "I've shipped firmware without an RTOS — a run-to-completion executive on multicore Cortex-R — and I can tell you exactly why: per-command budgets under a microsecond. And I'd pick an RTOS for a sensor hub for the opposite reason."

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 슈퍼 루프에 긴 일을 하나 추가 | 전혀 상관없는 제어 루프의 지터가 갑자기 커짐 | 슈퍼 루프의 최악 지연 = 루프 안 가장 긴 일 (예제 2: 297.8 µs) | 일을 조각내고 마감 전에만 실행(예산 스케줄러), 또는 RTOS 태스크로 |
| "RTOS가 느리다"를 기본 설정 숫자로 판단 | Zephyr가 FreeRTOS보다 3배 느리다는 결론 | time slicing · 디버그 옵션이 켜진 채로 측정 (19.2 vs 5.2 µs) | 설정을 명시하고, 같은 조건으로 다시 잰다 |
| 20 kHz 루프를 RTOS 태스크로 깨움 | CPU 사용률 급증, 루프 주기 지터 | 깨움 비용이 주기의 14~38% (25 MHz) | 루프는 ISR 안에서 끝내고 zero-latency 우선순위로 |
| zero-latency ISR에서 `...FromISR` 호출 | 가끔 큐 손상, 원인 모를 HardFault | BASEPRI 임계구역 밖에서 커널 자료구조를 만짐 | `configASSERT` 켜기, ISR → 태스크는 링버퍼나 하위 우선순위 SW 인터럽트로 |
| tick rate를 10 kHz로 올림 ("해상도가 좋아지니까") | CPU 1.5~4.5% 증발, 배터리 수명 감소 | tick마다 1.5~4.5 µs (25 MHz) | 1 kHz 이하 + 정밀한 시간은 HW 타이머, 또는 tickless |
| heap을 크게 잡아 놓고 부팅이 느리다고 함 | reset → main 1.4 ms | `.bss` 0 채우기 (heap 128 KB) | heap을 실제 사용량에 맞추거나 큰 버퍼를 `.noinit`으로 |
| 태스크를 기능마다 하나씩 20개 | RAM 부족, 스택 오버플로 | 태스크 비용의 대부분은 스택 (TCB 96~128 B는 작다) | 같은 주기 · 같은 우선순위 일은 한 태스크로, high-water mark로 스택 크기 |
| 안전 기능을 "RTOS니까 안전" 하게 설계 | 인증 단계에서 근거 부족 | 일반 RTOS 빌드는 인증 근거(요구사항 추적, 커버리지)가 없음 | 인증 RTOS 또는 단순한 bare-metal + 고정 시간표, 아니면 별도 safety MCU |
| 혼합 중요도를 같은 RTOS 태스크로만 분리 | QM 코드 버그가 ASIL 태스크 메모리를 덮어씀 | 같은 주소 공간, MPU 미사용 | MPU 태스크 격리(F 모듈), 칩 분리, 또는 하이퍼바이저(§11.4) |
| Linux 쪽에서 1 kHz 루프를 닫음 | 평소 괜찮다가 가끔 수 ms 튐 | 최악값 보장 없음 (예제 11: max 2.9 ms) | 루프는 MCU에서 닫고 Linux는 setpoint만 (EL N4) |

---

## 16. 면접에서 이렇게 말한다

**Q.** "When would you choose bare-metal over an RTOS?"

**A.** 코어의 일이 주기 루프 하나와 ISR 몇 개로 끝나고, 가장 짧은 deadline이 RTOS가 태스크를 깨우는 비용에 비해 너무 짧을 때다. 예를 들어 25 MHz Cortex-M3에서 RTOS가 ISR → 태스크에 7~8 µs를 쓰는데 FOC 루프 주기가 50 µs면 그것만으로 15%다. 이런 루프는 어차피 ISR 안에서 닫아야 하고, 남는 일이 작으면 RTOS는 RAM과 검증 부담만 더한다.

> I go bare-metal when the core's job is one periodic loop plus a few ISRs, and the tightest deadline is only a few times the RTOS wake-up cost. On a 25 MHz Cortex-M3 I measured about 7 to 8 microseconds from IRQ to a task with FreeRTOS — that's 15 percent of a 20 kHz FOC period. That loop belongs in the ISR anyway, and if what's left is small, an RTOS only adds RAM and verification work.

**꼬리 질문**: "그럼 슈퍼 루프의 단점은?" → 최악 지연이 루프 안 가장 긴 일로 정해지고, 기능을 더할 때마다 바뀐다(측정: 300 µs 일이 있으면 최대 297.8 µs).

**Q.** "What does an RTOS actually cost you? Give me numbers."

**A.** 네 가지로 답한다. 깨우는 비용(ISR → 태스크), 전환 비용, tick 세금, RAM. 같은 25 MHz 보드에서 FreeRTOS는 각각 6.9 µs, 순수 전환 1.9 µs, tick당 1.5 µs, 태스크당 TCB 96 바이트 + 스택이었다. 그리고 설정이 이 숫자를 몇 배 바꾼다 — Zephyr는 time slicing을 끄자 19 µs가 5 µs가 됐다. 대신 얻는 것은 **일정함**이다. 하위 태스크가 무엇을 하든 깨움 지연이 같았다.

> Four costs: wake-up latency, context switch, tick overhead, and RAM per task. On the same 25 MHz Cortex-M3, FreeRTOS gave me 6.9 microseconds IRQ-to-task, about 1.9 microseconds per raw switch, 1.5 microseconds per tick, and 96 bytes of TCB plus the stack per task. Configuration matters a lot — Zephyr went from 19 to 5 microseconds just by disabling time slicing. What you buy is constancy: the wake-up latency didn't change at all when a lower-priority task was busy.

**꼬리 질문**: "그 숫자를 STM32H7에서는?" → 클럭 비례로 하한 0.36 µs 정도, 실제는 플래시 wait state와 캐시 때문에 더 걸리니 DWT CYCCNT나 GPIO + scope로 다시 잰다.

**Q.** "Your super-loop misses deadlines whenever the telemetry code runs long. What do you do?"

**A.** 두 가지 길이 있다. 선점 없이 고치려면 긴 일을 조각내고, 각 조각을 "다음 마감 전에 끝날 수 있을 때만" 실행하는 예산 스케줄러를 둔다 — Betaflight가 자이로에 고정된 스케줄러로 하는 방식이다. 선점으로 고치려면 RTOS를 넣고 제어 루프를 가장 높은 우선순위 태스크로 둔다. 그러면 텔레메트리가 아무리 길어도 깨움 지연이 일정하다. 기능이 계속 늘어날 제품이면 RTOS 쪽을 고른다.

> Two options. Without preemption: slice the long job and only run a slice when its estimated time fits before the next deadline — that's what Betaflight's gyro-locked scheduler does. With preemption: move to an RTOS and make the control loop the highest-priority task, so its latency no longer depends on how long telemetry takes. If the feature list will keep growing, I take the RTOS route.

**Q.** "How would you run a 20 kHz current loop and a CAN stack on the same MCU?"

**A.** 하이브리드로 한다. 전류 루프는 PWM 동기 ADC 완료 ISR 안에서 끝까지 계산하고, 그 ISR을 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 우선순위에 둬서 커널 임계구역이 절대 막지 못하게 한다. 그 ISR은 RTOS API를 안 부르고, 속도 루프 태스크와는 락 없는 링버퍼로 통신한다. CAN, 진단, 업데이트는 RTOS 태스크다. 측정해 보면 50 µs 임계구역 아래에서 kernel-aware 인터럽트는 최대 50 µs를 기다렸고, zero-latency 인터럽트는 160 ns였다.

> A hybrid. The current loop runs to completion inside the ADC-complete ISR, and that ISR sits above configMAX_SYSCALL_INTERRUPT_PRIORITY, so kernel critical sections can never delay it. It makes no RTOS calls; it hands data to the speed-loop task through a lock-free ring buffer. CAN, diagnostics and firmware update are normal RTOS tasks. When I measured it under a 50 microsecond critical section, the kernel-aware interrupt waited up to 50 microseconds and the zero-latency one stayed at 160 nanoseconds.

**꼬리 질문**: "그 ISR에서 태스크를 깨워야 하면?" → 하위(kernel-aware) 우선순위의 소프트웨어 인터럽트를 pend해서 그 ISR이 `...FromISR`을 부르게 한다.

**Q.** "Why does PX4 use NuttX while Betaflight has no RTOS?"

**A.** 범위의 차이다. PX4는 센서 드라이버, EKF, 여러 제어기, 미션, 로깅, MAVLink 등 수십 개 모듈을 수백 명이 독립적으로 붙이는 플랫폼이라 태스크와 work queue, uORB publish/subscribe가 맞다. Betaflight는 자이로 → 필터 → PID라는 루프 하나를 최대한 빠르고 일정하게 도는 게 목적이라, 자이로에 고정된 협력형 스케줄러가 남는 시간에만 다른 일을 끼워 넣는다. 둘 다 H7에서 돌지만 최적화 대상이 다르다.

> Breadth versus depth. PX4 is a platform: dozens of modules — drivers, EKF2, controllers, missions, logging — developed independently, so tasks, work queues and uORB pub/sub fit. Betaflight optimizes a single gyro-filter-PID loop, so it uses a bare-metal cooperative scheduler locked to the gyro that only slots other work in when it fits before the next gyro deadline.

**Q.** "When would you put a hypervisor on an MCU-class core like a Cortex-R52?"

**A.** 한 칩에 중요도가 다른 소프트웨어를 섞어야 하는데 칩을 나눌 수 없을 때다. Armv8-R은 EL2와 2단계 MPU가 있어서 MMU 없이도 파티션마다 RTOS를 격리할 수 있다. 다만 칩을 나눌 수 있으면(별도 safety MCU) 그게 대개 더 단순하다. 하이퍼바이저를 쓰면 파티션 간 통신, 캐시 · 버스 간섭, 인증된 분리 커널 비용을 같이 산다.

> When I must mix criticality levels on one chip and can't split them across chips. Armv8-R gives you EL2 and a second-stage MPU, so you can isolate an RTOS per partition without an MMU — Bao, for example, supports Armv8-R AArch32 platforms with static partitioning. But if I can use a separate safety MCU, that's usually simpler; a hypervisor brings inter-partition communication, cache and bus interference, and certification cost.

**Q.** "Your SSD firmware didn't use FreeRTOS. Why not?"

**A.** 명령당 시간 예산 때문이다. 한 코어가 초당 백만 명령이면 명령당 1 µs인데, RTOS 깨움 한 번이 고클럭 Cortex-R에서도 수백 ns다. 그리고 일이 원래 하드웨어 큐 이벤트를 짧은 핸들러로 끝까지 처리하는 형태라 태스크 스택이 필요 없다. 동시성은 코어 파이프라인과 하드웨어 큐로 얻었다. 반대로 블로킹 I/O와 여러 주기가 섞인 센서 허브라면 RTOS를 고른다 — 같은 기준으로 반대 결론이 나온다.

> Per-command budget. At a million commands per second per core you have about a microsecond per command, and even on a fast Cortex-R an RTOS block-and-wake costs a few hundred nanoseconds. The work is naturally event-driven — pop from a hardware queue, run a short handler to completion — so we didn't need per-task stacks; concurrency came from the multicore pipeline and hardware queues. For a sensor hub with blocking I/O and several rates I'd pick an RTOS — same criteria, opposite answer.

**Q.** "What tick rate would you choose, and does it matter?"

**A.** 대부분 1 kHz 이하로 둔다. 측정하면 tick 하나가 25 MHz에서 1.5~4.5 µs라 10 kHz면 CPU 1.5~4.5%가 그냥 사라진다. tick은 timeout과 delay의 해상도일 뿐이고, 정밀한 주기는 하드웨어 타이머 인터럽트로 만든다. 배터리 장비면 tickless를 쓴다 — Zephyr 기본값은 tickless라 깨울 일이 없으면 tick 인터럽트가 0번이었다.

> Usually 1 kHz or lower. I measured 1.5 to 4.5 microseconds per tick at 25 MHz, so a 10 kHz tick burns 1.5 to 4.5 percent of the CPU for nothing. The tick is only the resolution for delays and timeouts; precise periodic work should come from a hardware timer. On battery devices I use tickless mode — Zephyr's default — where a busy system with no pending timeouts took zero tick interrupts.

---

## 17. 직접 해보기

1. **개념 — 배치 결정**: 소형 농업 드론의 보조 MCU(STM32G4, 170 MHz, RAM 128 KB)가 맡을 일: 초음파 고도계 20 Hz(UART), 펌프 모터 PWM + 전류 감시 10 kHz, 유량 센서 펄스 카운트, 메인 FC와 CAN 메시지 50 Hz, 펌웨어 업데이트. bare-metal / RTOS / 하이브리드 중 무엇으로, 각 일을 어떤 실행 단위에 둘지 표로 써라.
   **정답/힌트**: 독립 주기 여러 개 + CAN 스택 + DFU → RTOS. 10 kHz 전류 감시는 깨움 비용(170 MHz에서 약 1 µs = 주기 100 µs의 1%)상 태스크도 가능하지만, 과전류 차단은 비교기/타이머 브레이크 하드웨어 + 최상위 ISR로(EL N4 §1.1 표).
2. **코드 — 슈퍼 루프 고치기**: 예제 2의 `run(&s_flag_busy, 300)`을 "50 µs 조각 6개"로 바꿔서(루프 한 바퀴에 50 µs만 일하고 플래그 확인) 최대 지연을 다시 재라.
   **정답/힌트**: 작성 시 `run(&s_flag_busy, 50)`으로 바꿔 돌린 실측은 평균 25.7 µs, 최대 50.44 µs(300 µs일 때 297.8 µs). 조각 크기 = 최악 지연이다. 이게 협력형 스케줄러의 원리다 — 대신 같은 300 µs 일을 끝내려면 루프를 여섯 바퀴 돌아야 하니, 일을 상태 기계로 쪼개는 수고가 든다.
3. **측정 — Zephyr slicing을 끄지 않고 줄이기**: 예제 4를 `-DCONFIG_TIMESLICE_SIZE=0`(slicing 기능은 켠 채 슬라이스 크기 0)으로 빌드해서 IRQ → 스레드를 재라. 19.2 µs와 5.2 µs 중 어디에 가까울지 먼저 예측하라.
   **정답/힌트**: `z_time_slice_size()`가 0을 돌려주면 `z_reset_time_slice()`는 `z_abort_timeout()`만 하고 `z_add_timeout()`을 건너뛴다. 작성 시 실측은 **6.72 µs** — 타이머 재설정(약 12.5 µs)은 사라지고 `z_abort_timeout()`과 slice 판정 비용(약 1.5 µs)만 남는다. 같은 우선순위끼리 시간을 나눌 일이 없으면 `CONFIG_TIMESLICING=n`이 가장 싸다.
4. **측정 — 태스크 RAM 확인**: 예제 10의 `probe` 태스크에서 `uxTaskGetStackHighWaterMark(NULL)`을 출력하고, 256 워드 중 실제로 몇 워드가 쓰였는지 보라. 스택을 얼마로 줄일 수 있나?
   **정답/힌트**: 출력 서식(`board_printf_raw`)이 스택을 꽤 쓴다. high-water mark + 안전 여유(예: 25%)로 정한다(F3).
5. **계산 — 내 칩으로 옮기기**: 예제 12의 `derive.py`에 "IRQ → 태스크 6.88 µs (25 MHz)"를 480 MHz H7, 플래시 wait state 때문에 명령당 평균 1.5사이클로 바꿔 넣어 8 kHz rate loop 주기 대비 %를 구하라.
   **정답/힌트**: 172사이클 × 1.5 / 480 MHz ≈ 0.54 µs → 125 µs의 0.43%. 실제 칩에서는 DWT CYCCNT로 확인.
6. **설계 — 하이브리드 우선순위표**: 로봇 관절 드라이버(FOC 20 kHz, 엔코더 SPI, CAN-FD, 온도, DFU)의 NVIC 우선순위표와 태스크 우선순위표를 그림 6 형식으로 만들어라. `configMAX_SYSCALL_INTERRUPT_PRIORITY` 선 위에 무엇을 두나?
   **정답/힌트**: 선 위 = PWM/ADC 전류 루프 ISR(그리고 하드웨어 과전류 브레이크). 선 아래 = 엔코더 DMA 완료, CAN 수신, UART. 태스크 = 속도/위치 루프(높음), CAN 프로토콜(중간), 온도 · DFU(낮음). 자세히는 E2.
7. **측정 — Linux 꼬리 키우기**: 예제 11을 `stress-ng --cpu 6 --timeout 30` 아래에서 `sudo chrt -f 80`으로 다시 돌려 max가 어떻게 변하는지 보라. VM이라는 점을 결과 해석에 반드시 적어라.
   **정답/힌트**: 공유 VM에서는 호스트가 vCPU를 뺏는 시간이 지배한다(EL N1 §2.3, §6.2). 숫자보다 "중앙값 대비 꼬리 비율"을 보고하라.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| bare-metal | OS 없이 하드웨어 위에서 직접 | `main()`과 ISR이 전부. 스케줄러는 코드 순서와 NVIC 우선순위 |
| super-loop (슈퍼 루프) | 무한 루프 하나가 모든 일을 차례로 | 최악 지연 = 루프 안 가장 긴 일 |
| 협력형 스케줄러 | cooperative scheduler | 일이 스스로 끝나야 다음 일로. 선점 없음 (Betaflight) |
| run-to-completion | 끝까지 실행 | 핸들러가 중간에 기다리지 않고 끝난다. SSD 실행부 · work queue의 방식 |
| 실행부 | executive | RTOS 없이 직접 만든 작은 스케줄링 루프. SSD 펌웨어에서 흔함 |
| RTOS 깨움 비용 | IRQ → task latency | ISR이 신호를 주고 태스크가 실제로 돌기 시작할 때까지 (측정 5.2~19.2 µs) |
| 컨텍스트 스위치 | context switch | 레지스터 저장 → TCB 교체 → 복원 (측정 순수 1.9~16.7 µs) |
| PendSV | 미뤄 둔 소프트웨어 예외 | 가장 낮은 우선순위로 예약한 전환 전용 예외 (B4) |
| SysTick | Cortex-M 코어 타이머 | RTOS tick의 기본 원천 |
| tick 세금 | tick overhead | 매 tick 인터럽트가 가져가는 CPU (측정 1.47~4.5 µs/tick) |
| tickless | 깨울 일이 있을 때만 타이머 인터럽트 | Zephyr 기본. FreeRTOS는 idle일 때만(`configUSE_TICKLESS_IDLE`) |
| time slicing | 같은 우선순위끼리 시간 나누기 | Zephyr 기본 on. 전환마다 슬라이스 타이머 재설정 비용 |
| zero-latency 인터럽트 | 커널이 막지 않는 인터럽트 | `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높음. RTOS API 금지 |
| kernel-aware 인터럽트 | 커널 API를 부를 수 있는 인터럽트 | 그 대신 커널 임계구역 동안 기다린다 |
| BASEPRI | 우선순위 마스크 레지스터 | 이 값 이하(숫자 이상) 우선순위만 막는다. FreeRTOS CM3 임계구역 |
| 하이브리드 | ISR(bare-metal) + RTOS 태스크 | 가장 빡빡한 루프만 커널 밖에 |
| footprint | 메모리 사용량 | FLASH(text) + RAM(data + bss + 스택 + heap) |
| TCB | Task Control Block | 태스크 하나의 커널 기록. FreeRTOS 96 B, Zephyr `k_thread` 128 B (이 설정) |
| 하이퍼바이저 | hypervisor | 코어 · 메모리 · 장치를 파티션으로 나눠 여러 OS를 격리 |
| 분리 커널 | separation kernel | 격리만 책임지는 작은 커널(인증용). PikeOS 등 |
| mixed criticality | 혼합 중요도 | 안전 등급이 다른 SW를 한 칩에 |
| EL2 (Armv8-R) | 하이퍼바이저 예외 레벨 | Cortex-R52의 2단계 MPU로 파티션 격리 |
| icount | QEMU 명령 수 기반 가상 시간 | `ICOUNT=1` → 명령 1개 = 32 ns, 결정적 측정 |
| FPGAIO COUNTER | MPS2 FPGA IO의 카운터 | 0x40028018, reset부터 25 MHz — 이 노트의 사이클 시계 |

---

## 19. 요약 & 체크리스트

"bare-metal vs RTOS vs Linux vs 하이퍼바이저"는 "이 코어에서 다음 일을 누가 고르나"의 문제다. 같은 25 MHz Cortex-M3(QEMU icount)에서 재 보면, bare-metal 슈퍼 루프는 한가할 때 0.56 µs로 가장 빠르지만 바쁠 때는 루프 안 가장 긴 일(297.8 µs)이 최악 지연이 된다. RTOS는 깨우는 데 5~19 µs를 쓰지만 하위 일이 무엇이든 **일정**하다. 그 비용은 설정에 크게 좌우된다(Zephyr time slicing 19.2 → 5.2 µs, FreeRTOS 디버그 옵션 8.28 → 6.88 µs, heap 크기가 부팅을 1.5 ms → 0.37 ms). tick 세금은 tick당 1.5~4.5 µs라 rate에 비례하고, tickless면 0이 될 수 있다. 태스크 하나의 RAM은 대부분 스택이다. 판단은 "깨움 비용이 가장 짧은 주기의 몇 %인가"로 한다 — 몇 % 이하면 태스크, 10%를 넘으면 그 루프는 zero-latency ISR(하이브리드). Linux는 MMU · 리치 스택이 필요할 때(EL N4), 하이퍼바이저는 칩을 나눌 수 없는데 중요도를 섞어야 할 때(Cortex-R52 EL2, O6 · P6)다.

- [ ] 같은 보드에서 bare-metal · FreeRTOS · Zephyr의 IRQ → 처리 지연을 숫자로 말할 수 있다 (0.56 / 6.9~8.3 / 5.2~19.2 µs)
- [ ] 슈퍼 루프의 최악 지연이 "가장 긴 일"로 정해진다는 것을 측정(297.8 µs)으로 설명할 수 있다
- [ ] RTOS가 파는 것이 "빠름"이 아니라 "일정함"이라는 것을 설명할 수 있다
- [ ] Zephyr time slicing과 FreeRTOS 디버그 옵션이 비용을 얼마나 바꾸는지, 소스의 어느 함수 때문인지 말할 수 있다
- [ ] 부팅 시간에서 커널 비용과 `.bss` 0 채우기를 구분할 수 있다
- [ ] tick당 비용과 rate별 CPU 손실을 계산하고 tick rate를 고를 수 있다
- [ ] zero-latency ISR + RTOS 태스크 하이브리드를 우선순위 지도로 그리고, 대가(API 금지)를 말할 수 있다
- [ ] 태스크 하나의 RAM(TCB + 스택)을 재고 스택이 지배한다는 것을 안다
- [ ] "깨움 비용 / 가장 짧은 주기" 비율로 bare-metal과 RTOS를 가를 수 있다
- [ ] PX4(NuttX)와 Betaflight(bare-metal 협력형)의 선택 이유를 설명할 수 있다
- [ ] SSD 실행부가 범용 RTOS를 피한 이유를 명령당 예산으로 설명할 수 있다
- [ ] 하이퍼바이저가 MCU급 코어(Cortex-R52 EL2)에 들어오는 조건을 말할 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (`/var/rtoslab/src/FreeRTOS-Kernel`): `tasks.c`(`xTaskIncrementTick`, `vTaskSwitchContext`, `ulTaskGenericNotifyTake`, `xTaskResumeAll`), `portable/GCC/ARM_CM3/port.c`(`xPortPendSVHandler`, `xPortSysTickHandler`, `vPortValidateInterruptPriority`), `portable/MemMang/heap_4.c` — https://github.com/FreeRTOS/FreeRTOS-Kernel
- Zephyr v4.4.2 소스 (`/var/rtoslab/zephyrproject/zephyr`): `kernel/sched.c`(`update_cache`), `kernel/timeslicing.c`(`z_reset_time_slice`), `kernel/timeout.c`(`z_add_timeout`, `sys_clock_announce`), `kernel/sem.c`, `drivers/timer/cortex_m_systick.c`(`sys_clock_isr`, `sys_clock_set_timeout`), `arch/arm/core/cortex_m/swap_helper.S`(`z_arm_pendsv`), `arch/arm/core/cortex_m/Kconfig`(`ZERO_LATENCY_IRQS`), `kernel/Kconfig`(`SYS_CLOCK_TICKS_PER_SEC`, `TICKLESS_KERNEL`)
- Zephyr 문서 — Scheduling: https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html
- QEMU 문서 — Arm MPS2 and MPS3 boards: https://www.qemu.org/docs/master/system/arm/mps2.html
- Betaflight 스케줄러 소스 `src/main/scheduler/scheduler.c` (2026-10-25 master 확인): https://github.com/betaflight/betaflight/blob/master/src/main/scheduler/scheduler.c
- PX4 문서 — Architectural Overview (NuttX, tasks vs work queues, uORB; 2026-10-25 확인): https://docs.px4.io/main/en/concept/architecture.html
- Bao hypervisor README (지원 플랫폼, 정적 분할; 2026-10-25 확인): https://github.com/bao-project/bao-hypervisor
- Joseph Yiu, "The Definitive Guide to Arm Cortex-M3 and Cortex-M4 Processors" — 예외 진입 · 인터럽트 지연 · BASEPRI
- Arm, "Armv8-R AArch32 / Cortex-R52 Technical Reference Manual" — EL2, 2단계 MPU
- Don의 Embedded Linux 노트: EL N1(Linux는 실시간인가 — cyclictest 실측), EL N3(지연 측정), EL N4(Linux vs RTOS vs MCU 분담 — 부팅 8.91 s 실측, 판단 기준 · 패턴)
- 같은 RTOS 노트 세트: A1(실시간 정의), A2(실행 모델), A3(구성 요소 · footprint), A6(RTOS 지형), B3 · E2(우선순위 설계), C2 · C5 · C6(전환 · tick · tickless), D8(락 없는 통신), O6 · P6(분할 · 하이퍼바이저), Q1 · Q2 · Q8(PX4 · ArduPilot · 스토리지)
