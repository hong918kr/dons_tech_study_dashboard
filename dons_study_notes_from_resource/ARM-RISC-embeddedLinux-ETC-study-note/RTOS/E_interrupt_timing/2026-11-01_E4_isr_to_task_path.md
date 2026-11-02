# E4. ISR → 태스크 처리 경로 — ISR에 무엇을 남기고, 어떤 단위로 넘기고, 폭주 · 과부하에서 어떻게 살아남나

> **이 노트를 다 읽으면**: ISR 안에 남길 네 가지(원인 지우기 · 시각 찍기 · 데이터 옮기기 · 깨우기)와 플래그를 **어디서** 지워야 하는지를 실측(끝에서 지우면 499개 중 45개 유실, W1C를 "전부" 지우면 RX 21% 유실, 안 지우면 121.8 ms 동안 tick 0개)으로 설명할 수 있다 · 사건마다 깨우기와 "N개 또는 T µs" 묶음 깨우기를 깨움 비용 모델(QEMU에서 깨움 1번 = 17.4 µs)로 비교해 N을 지연 예산에서 계산할 수 있다 · 인터럽트 폭주(끼인 level IRQ, 튀는 GPIO, 트래픽 홍수)를 감시 카운터 · 원천 끄기 · 백오프 복구 · NAPI식 폴링 전환으로 막는 코드를 FreeRTOS와 Zephyr로 쓸 수 있다 · 큐가 찼을 때 drop-newest / drop-oldest / latest-only 중 데이터 종류에 맞는 정책을 고르고 유실을 셀 수 있다
> **왜 필요한가**: 펌웨어 설계 면접의 단골 "인터럽트에서 무엇을 하고 무엇을 미루나?", "UART가 1 Mbaud로 쏟아지면?", "버튼 인터럽트가 수십 번 들어오면?", "센서 인터럽트가 멈추지 않으면 시스템은?"에 구조와 숫자로 답해야 한다. 실무에서는 드론 FC의 IMU · GNSS · RC 수신, 모터 컨트롤러의 CAN, 센서 허브의 타임스탬프가 전부 이 경로 위에 있다
> **Don 기준 난이도**: SSD 펌웨어의 "완료 ISR은 기록만, executive가 처리", NVMe 인터럽트 coalescing, W1C 상태 레지스터는 이미 안다 / 새로 배울 부분은 RTOS에서 그 구조의 **비용**(깨움 1번의 CPU 값), 묶음 크기를 고르는 계산, 폭주가 RTOS의 시간(tick)까지 멈춘다는 사실, NAPI식 모드 전환을 MCU에서 직접 조립하는 방법
> **선행 노트**: A2(실행 모델 — M2 인터럽트 + 메인 루프), D1(세마포어 — 잃어버린 give), D5(task notification · stream buffer trigger level), D7(작업 위임 네 패턴 — 이 노트의 바로 앞), D8(SPSC 링 · DSB), EL F6(Linux 인터럽트 · 폭풍 감지), EL F7(bottom half). 이어지는 노트: E1(FromISR 규칙), E2(우선순위 배치), E3(지연 측정), E6(DMA 완료 경로), J(드라이버 패턴)
> 🔁 **Bare-Metal 겹침**: BM J1(ISR 설계 원칙 — 짧게, 플래그 클리어 위치(늦게 지우면 재진입), 이벤트를 기록하고 메인에서 처리) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "super-loop + 플래그 폴링으로 이벤트를 처리하는 관점"만 더한다

---

## 0. 큰 그림 — ISR은 접수 창구, 태스크는 처리 부서

Don이 SSD 컨트롤러에서 매일 보던 장면부터 시작하자. NAND 채널 완료 인터럽트가 오면 ISR은 완료 상태를 읽어 지우고, 완료 기록 하나를 링에 넣고, executive를 깨우고 나간다. 호스트 쪽에서도 같은 일이 반대 방향으로 일어난다 — 컨트롤러는 명령 하나 끝날 때마다 호스트에 MSI-X를 쏘지 않고, NVMe **Interrupt Coalescing** 기능(Set Features, Feature ID 08h)의 Aggregation Threshold(몇 개 모이면)와 Aggregation Time(100 µs 단위, 얼마나 기다렸으면)으로 **모아서** 쏜다. 그리고 펌웨어 버그로 완료 비트를 안 지우면 CPU가 ISR에 갇혀 watchdog이 터지는 것도 Don은 본 적이 있을 것이다.

이 노트는 그 경험을 RTOS 위에서 하나의 **설계**로 정리한다. D7이 "왜 ISR 밖으로 일을 옮기나"와 "옮기는 수단 네 가지(전용 태스크, timer daemon, `k_work`, 게이트키퍼)"를 측정으로 보였다. 이 노트는 그다음 질문들을 다룬다.

1. **ISR 안에 정확히 무엇을 남기나, 그리고 플래그는 어디서 지우나** (§1) — Bare-Metal BM J1의 주인 절.
2. **어떤 단위로 넘기나** — 사건마다 깨우기 vs 모아서 깨우기, 묶음 크기 N과 시간 상한 T를 숫자로 고르기 (§2).
3. **누가 받나** — 전용 고우선순위 태스크 vs 공용 worker (§3, 측정은 D7에 있고 여기서는 결정 규칙).
4. **비정상일 때** — 인터럽트 폭주(storm)와 과부하(큐가 참)에서 시스템이 사는 법 (§4, §5).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">ISR → 태스크 처리 경로의 네 가지 설계 결정</text>
<rect x="10" y="40" width="115" height="150" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="67" y="62" font-size="13" text-anchor="middle">하드웨어 사건</text>
<text x="67" y="90" font-size="12" text-anchor="middle">IMU data-ready</text>
<text x="67" y="112" font-size="12" text-anchor="middle">UART RX FIFO</text>
<text x="67" y="134" font-size="12" text-anchor="middle">CAN 프레임</text>
<text x="67" y="156" font-size="12" text-anchor="middle">버튼 GPIO</text>
<rect x="145" y="40" width="165" height="150" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="227" y="62" font-size="13" text-anchor="middle">ISR = 접수 창구</text>
<text x="160" y="90" font-size="12">① 원인 지우기 (ack)</text>
<text x="160" y="112" font-size="12">② 사건 시각 찍기</text>
<text x="160" y="134" font-size="12">③ 데이터 옮기기</text>
<text x="160" y="156" font-size="12">④ 담당자 깨우기</text>
<text x="160" y="178" font-size="12">→ 수 µs 안에 끝</text>
<rect x="330" y="40" width="155" height="150" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="407" y="62" font-size="13" text-anchor="middle">넘기는 통로</text>
<text x="345" y="90" font-size="12">notify 개수 세기</text>
<text x="345" y="112" font-size="12">SPSC 링 + 깨움</text>
<text x="345" y="134" font-size="12">N개 또는 T µs</text>
<text x="345" y="156" font-size="12">큐 (값 복사)</text>
<rect x="505" y="40" width="165" height="150" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="587" y="62" font-size="13" text-anchor="middle">태스크 = 처리 부서</text>
<text x="520" y="90" font-size="12">파싱 · 보정 계산</text>
<text x="520" y="112" font-size="12">I2C / SPI 트랜잭션</text>
<text x="520" y="134" font-size="12">플래시 · 로그</text>
<text x="520" y="156" font-size="12">프로토콜 상태 기계</text>
<line x1="125" y1="115" x2="143" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="310" y1="115" x2="328" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="485" y1="115" x2="503" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="145" y="210" width="165" height="50" rx="6" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="4,3"/>
<text x="227" y="231" font-size="12" text-anchor="middle">결정 1 · §1</text>
<text x="227" y="250" font-size="12" text-anchor="middle">무엇을 남기고 언제 지우나</text>
<rect x="330" y="210" width="155" height="50" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-dasharray="4,3"/>
<text x="407" y="231" font-size="12" text-anchor="middle">결정 2 · §2, §5</text>
<text x="407" y="250" font-size="12" text-anchor="middle">한 건씩? 묶어서? 차면?</text>
<rect x="505" y="210" width="165" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-dasharray="4,3"/>
<text x="587" y="231" font-size="12" text-anchor="middle">결정 3 · §3</text>
<text x="587" y="250" font-size="12" text-anchor="middle">전용 태스크? 공용 worker?</text>
<rect x="10" y="275" width="660" height="44" rx="6" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4,3"/>
<text x="340" y="295" font-size="12" text-anchor="middle">결정 4 · §4 — 비정상일 때: 끼인 level IRQ, 튀는 GPIO, 트래픽 홍수(storm)</text>
<text x="340" y="312" font-size="12" text-anchor="middle">감시 카운터 → 원천 끄기 → 태스크에서 복구 / 폴링 모드로 전환</text>
</svg>
```

그림 1 — 사건이 하드웨어에서 태스크까지 가는 길과 그 위의 네 가지 결정. ISR은 수 µs 안에 끝나는 접수 창구이고, 시간이 들거나 블록할 수 있는 일은 전부 태스크(처리 부서)로 간다. 결정 4는 정상 경로가 깨졌을 때의 방어선이다.

이 노트의 측정 결과를 미리 한 표로 보면 이렇다.

| 절 | 주제 | 예제 | 핵심 숫자 (QEMU icount) |
|---|---|---|---|
| §1.3 | 플래그 클리어 위치 | 예제 1 | 맨 앞 유실 0 · 맨 끝 45/499 유실 · 안 지움 → 121.8 ms 동안 tick +0 |
| §1.4 | W1C · 읽기 전 클리어 | 예제 2 | "전부 지우기" RX 238/1122 · TX 255/1128 유실, 읽기 전 클리어 6개 유실 + 6개 중복 |
| §1.5 | 쓰기 버퍼 · DSB | 예제 3 | QEMU에서는 차이 0 (모델링 안 함) — 실칩 규칙과 생성 코드만 확인 |
| §1.6 | 시각은 ISR에서 | 예제 4 | 태스크에서 찍으면 간격 730~1263 µs, ISR에서 찍으면 993~1000 µs |
| §2 | 묶음 깨우기 | 예제 5 · 6 | N=1 CPU 44% → N=16 11%, 대신 최대 지연 11 µs → 761 µs, 깨움 1번 = 17.4 µs |
| §4.3 | 끼인 level IRQ | 예제 7 · 8 | 감시 없음: 제어 루프 30 ms 정지 + tick 28개 증발, 감시 있음: 거의 그대로 |
| §4.5 | 튀는 GPIO | 예제 9 | edge 158개 → 매 edge 처리 79번 눌림(진짜 5), 마스크 + 태스크 debounce는 ISR 10번에 정답 5 |
| §4.6 | 트래픽 홍수 | 예제 10 | 패킷 간격 4 µs: 패킷마다 IRQ면 처리 0 · 제어 루프 1/200 (livelock), NAPI식이면 16000 · 201/200 |
| §5 | 큐가 찼을 때 | 예제 11 | drop-newest 최대 묵은 나이 19 ms, drop-oldest 7.9 ms, latest-only 0.9 ms |

### 0.1 경계 — D7, Bare-Metal, Linux와 무엇이 다른가

- **D7과의 경계**: ISR 안에서 300 µs 일을 하면 같은 우선순위 IRQ가 298 µs 늦어진다는 "짧게 하라"의 측정, 핸들러 태스크 우선순위를 높게 · 같게 · 낮게 두었을 때의 지연(8.1 µs vs 수백 µs), timer daemon · `k_work` · 게이트키퍼의 구현과 비용은 D7에 있다. 이 노트는 그 위에서 **ISR 내부의 순서**, **넘기는 단위**, **비정상 상황**을 다룬다. 신호 수단 자체(notification, 세마포어)는 D1 · D5, 링버퍼 구현은 D8을 참조한다.
- **Bare-Metal과의 경계**: BM J1의 "ISR 설계 원칙"은 이 노트가 주인이다. RTOS가 없을 때의 차이는 "깨우기" 대신 **플래그를 세우고 super-loop가 폴링한다**는 것 하나뿐이고, 나머지(지우는 위치, 기록하고 나가기, 폭주 방어)는 똑같다. 그래서 §1은 RTOS 없이도 그대로 읽히게 썼다.
- **Linux와의 경계**: top half / bottom half · threaded IRQ는 EL F6 · F7에서, Linux 커널의 폭풍 감지(`note_interrupt`가 10만 개 중 99,900개 초과 unhandled면 라인을 끈다)는 EL F6 §8에서 봤다. NAPI는 Linux 네트워크 드라이버가 쓰는 "IRQ 한 번 받고 끈 뒤 budget만큼 폴링" 구조다(`include/linux/netdevice.h`의 `NAPI_POLL_WEIGHT`는 64). MCU에서 달라지는 점은 **이 모든 것을 커널이 주지 않는다**는 것 — 감시 카운터, 모드 전환, 백오프를 드라이버가 직접 조립한다(§4, §6).

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위의 **FreeRTOS V11.3.2**(템플릿 설정: 1 kHz tick, 우선순위 8단계, `configMAX_SYSCALL_INTERRUPT_PRIORITY` = 0x20)와 **Zephyr v4.4.2**(같은 보드 `mps2/an385`). 모든 타이밍은 `-icount shift=5`(명령 1개 = 32 ns 가상 시간; FreeRTOS는 `ICOUNT=1`, Zephyr는 `qemu-system-arm`을 같은 옵션으로 직접 실행)에서 MPS2 FPGAIO 카운터(0x40028018, 25 MHz → 1 cycle = 40 ns)로 쟀다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 쓰기 버퍼 · 예외 스태킹 사이클을 모델링하지 않는다 — 특히 깨움 · 전환 비용은 명령 수로만 매겨진다). 이 노트가 보려는 것은 **설계 선택 사이의 상대 차이**다. 인터럽트 원천은 실제 장치인 CMSDK TIMER0(IRQ8, level 인터럽트 — 상태 비트를 지울 때까지 라인이 올라가 있다)와 DUALTIMER(IRQ10), 그리고 board가 IRQ로 쓰지 않는 TIMER1 벡터(IRQ9)를 NVIC ISPR로 소프트웨어 트리거한 "에뮬레이션 장치"다. 에뮬레이션 장치(W1C 상태 레지스터, FIFO, 접점 튐, 고장 난 ack)는 예제마다 **에뮬레이션이라고 표시**했다. FreeRTOS 예제는 `-Wall -Wextra` 경고 0개, Zephyr 앱도 앱 소스 경고 0개다.

모든 FreeRTOS 예제가 함께 쓰는 헤더 두 개와 설정 래퍼:

```c
/* E4 공용: FPGAIO 25 MHz 카운터, TIMER0(IRQ8), DUALTIMER(IRQ10), NVIC, busy-wait */
#ifndef E4_H
#define E4_H
#include <stdint.h>
#include "board.h"
#define CYC()        REG32(0x40028018u)            /* 25 MHz: 1 cyc = 40 ns */
#define DT_LOAD      REG32(0x40002000u)            /* DUALTIMER timer1 (IRQ10) */
#define DT_VALUE     REG32(0x40002004u)
#define DT_CTRL      REG32(0x40002008u)            /* 0xE2 periodic, 0xA3 one-shot (EN,IE,32b) */
#define DT_INTCLR    REG32(0x4000200Cu)
#define NVIC_ICER0   REG32(0xE000E180u)
#define NVIC_ISPR0   REG32(0xE000E200u)
#define NVIC_ICPR0   REG32(0xE000E280u)
#define IRQ_T0 8u
#define IRQ_SW 9u                                  /* TIMER1 벡터: board가 IRQ를 안 쓰므로 SW 트리거용 */
#define IRQ_DT 10u
static inline void busy_cyc(uint32_t c) { uint32_t t0 = CYC(); while ((uint32_t)(CYC() - t0) < c) { } }
static inline void busy_us(uint32_t us) { busy_cyc(us * 25u); }
static inline void irq_on(uint32_t n, uint8_t prio) { NVIC_IPR(n) = prio; NVIC_ISER0 = 1u << n; }
static inline void irq_off(uint32_t n) { NVIC_ICER0 = 1u << n; }
static inline void timer0_start(uint32_t period_cyc)
{
    TIMER_RELOAD(TIMER0_BASE) = period_cyc - 1u;
    TIMER_VALUE(TIMER0_BASE)  = period_cyc - 1u;
    TIMER_CTRL(TIMER0_BASE)   = 0x9;               /* EN + IRQ EN */
}
static inline void timer0_stop(void) { TIMER_CTRL(TIMER0_BASE) = 0; TIMER_INTCLR(TIMER0_BASE) = 1; }
#endif
```

```c
/* E4: 템플릿 설정 + context switch 카운터 */
#ifndef E4_CFG_WRAP_H
#define E4_CFG_WRAP_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
extern volatile uint32_t e4_switches;
#define traceTASK_SWITCHED_IN() (e4_switches++)
#endif
```

`traceTASK_SWITCHED_IN()`은 커널이 태스크를 CPU에 올릴 때마다 부르는 trace 훅이다(`tasks.c`의 `vTaskSwitchContext` 안). 여기에 카운터를 걸면 "초당 context switch 수"를 커널 수정 없이 셀 수 있다. 링버퍼는 D8 §5의 SPSC 설계를 최소로 줄인 것이다(설계 이유 · 배리어 논의는 D8에 있다).

```c
/* 최소 SPSC 링 (설계 · 배리어 논의는 D8): ISR = 생산자, 태스크 = 소비자, 크기 2^k */
#ifndef E4_RING_H
#define E4_RING_H
#include <stdint.h>
typedef struct { uint32_t ts, seq; } ev_t;
#define RING_N 256u
typedef struct { volatile uint32_t head, tail; ev_t b[RING_N]; } ring_t;
static inline uint32_t ring_count(const ring_t *r) { return r->head - r->tail; }
static inline int ring_put(ring_t *r, ev_t e)
{
    if (r->head - r->tail == RING_N) return 0;           /* 가득 참 */
    r->b[r->head % RING_N] = e;
    __asm volatile("" ::: "memory");                     /* 내용 → 인덱스 순서 (1코어 M3, D8 §5.4) */
    r->head++;
    return 1;
}
static inline int ring_get(ring_t *r, ev_t *e)
{
    if (r->head == r->tail) return 0;
    *e = r->b[r->tail % RING_N];
    __asm volatile("" ::: "memory");
    r->tail++;
    return 1;
}
#endif
```

빌드 · 실행은 예제마다 같은 모양이다.

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/E4/f/ex1.c OUT=/var/tmp/rtos/E4/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E4/ex1/app.elf'
```

---

## 1. ISR 설계 원칙 — 짧게, 정확한 자리에서 지우고, 기록하고 나간다

> 🔁 이 절은 Bare-Metal BM J1(ISR 설계 원칙 — 짧게, 플래그 클리어 위치, 이벤트를 기록하고 메인에서 처리)과 겹친다 (주인: 이 노트).

### 1.1 직관 — 접수 창구의 네 동작

은행 접수 창구를 떠올리자. 창구 직원은 손님이 오면 **번호표를 뽑아 주고(원인 지우기), 접수 시각을 적고(타임스탬프), 서류를 받아 바구니에 넣고(데이터 옮기기), 담당 부서에 벨을 누른다(깨우기).** 대출 심사는 창구에서 하지 않는다. 창구에서 심사를 시작하면 뒤에 선 손님 전부가 기다리고, 줄이 길어지면 손님이 돌아간다(사건 유실).

ISR이 정확히 그렇다. Cortex-M에서 ISR이 도는 동안 **자기보다 우선순위가 같거나 낮은 모든 것** — 다른 IRQ, SysTick, PendSV, 모든 태스크 — 이 멈춘다(B3 · E2). D7 예제 1에서 300 µs짜리 ISR이 같은 우선순위 IMU IRQ를 298 µs 늦춘 것이 그 증거다.

### 1.2 정의 — MCU의 top half / bottom half

ISR(top half)에 남기는 것은 **"지금 안 하면 하드웨어가 잊어버리거나, 지금 안 하면 다시 할 수 없는 일"**뿐이다.

| ISR에 남기는 것 | 왜 지금이어야 하나 | 보통 걸리는 시간 |
|---|---|---|
| ① 원인 확인 · 지우기 (ack) | level IRQ는 지울 때까지 다시 들어온다(§1.3). 상태 비트 하나에 사건 여러 개가 합쳐질 수 있다 | 레지스터 읽기 1 + 쓰기 1 |
| ② 사건 시각 찍기 | 태스크가 깨어날 때 찍으면 깨움 지연 · 선점만큼 틀어진다(§1.6) | 카운터 읽기 1 |
| ③ 데이터 옮기기 | 하드웨어 FIFO · 데이터 레지스터는 다음 사건이 덮어쓴다 | FIFO 깊이만큼의 읽기 |
| ④ 담당자 깨우기 | notify · 세마포어 · 큐 · work 제출 (D1 · D5 · D7) | FromISR API 1번 |
| ⑤ (예외) 안전 동작 | 과전류에서 PWM 끄기처럼 µs가 늦으면 하드웨어가 탄다 | 레지스터 쓰기 몇 개 |

태스크(bottom half)로 보내는 것은 나머지 전부다. 특히 **블록할 수 있는 일**(I2C/SPI 완료 대기, 뮤텍스, 플래시, `printf`, 할당)과 **길이가 들쭉날쭉한 일**(파싱, 보정 계산, 프로토콜 상태 기계, 로그 포맷팅)이다.

ISR 길이의 예산은 이렇게 잡는다. **ISR 하나의 최악 실행 시간은, 그보다 낮거나 같은 우선순위에 있는 모든 IRQ와 태스크의 최악 지연에 그대로 더해진다.** 그리고 **(ISR 실행 시간) × (발생 빈도)**는 시스템 CPU에서 먼저 떼어 가는 몫이다. 예를 들어 드론 FC에서 8 kHz IMU ISR이 3 µs면 2.4%이고, 같은 ISR이 FIFO 파싱까지 해서 30 µs가 되면 24%를 태스크들이 쓰기 전에 가져간다. 그 시간 동안 SysTick도 밀린다.

RTOS 없이(Bare-Metal) 같은 구조를 쓰면 ④가 "`volatile` 플래그 세우기 또는 링에 넣기"가 되고, super-loop가 그 플래그를 폴링한다. ①②③은 바뀌지 않는다. 그래서 아래 §1.3~§1.6은 RTOS가 있든 없든 그대로 적용된다.

### 1.3 플래그 클리어 위치 — 세 가지 실패

인터럽트 원인을 지우는 위치가 틀리면 세 가지 다른 증상이 나온다.

- **안 지움(또는 지우기가 안 먹음)** → level IRQ가 ISR 복귀 즉시 다시 들어온다. CPU가 ISR을 영원히 돈다(인터럽트 폭주, §4).
- **너무 늦게 지움** → 두 가지가 생긴다. (a) 상태 비트가 하나뿐인 장치에서, ISR이 처리하는 동안 같은 사건이 다시 오면 그 비트는 이미 1이라 합쳐지고, ISR 끝에서 지우면 **두 번째 사건까지 지워진다**(유실). (b) 지우는 쓰기가 ISR의 마지막 명령이면, 쓰기 버퍼 · 버스 브리지를 지나 주변장치에 도착하기 전에 예외 복귀가 일어나 NVIC가 아직 올라가 있는 라인을 보고 **ISR에 한 번 더 들어온다**(가짜 재진입 → 이중 카운트, §1.5).
- **너무 일찍 지움(데이터를 읽기 전에)** → 데이터 레지스터가 "비었다"고 판단한 하드웨어가 다음 값을 덮어쓴다. 앞의 값은 소리 없이 사라지고, 나중 값은 두 번 읽힌다(§1.4 예제 2의 (C)).

결론부터 말하면 규칙은 이렇다: **ISR 첫머리에서 상태를 읽고, 읽은 비트만 지우고, 그다음 데이터를 처리한다. 단, 데이터 레지스터가 있는 장치는 "데이터를 꺼낸 뒤" 그 비트를 지운다.** 아래 예제들이 각 실패를 실제로 보여 준다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">상태 비트 1개짜리 level IRQ — 어디서 지우나 (예제 1의 구조)</text>
<text x="10" y="48" font-size="12">사건</text>
<line x1="120" y1="38" x2="120" y2="54" stroke="#3f9a6b" stroke-width="2"/>
<line x1="320" y1="38" x2="320" y2="54" stroke="#3f9a6b" stroke-width="2"/>
<line x1="520" y1="38" x2="520" y2="54" stroke="#3f9a6b" stroke-width="2"/>
<text x="120" y="68" font-size="12" text-anchor="middle">1</text>
<text x="320" y="68" font-size="12" text-anchor="middle">2</text>
<text x="520" y="68" font-size="12" text-anchor="middle">3</text>
<text x="10" y="100" font-size="12">(A) 맨 앞</text>
<rect x="120" y="85" width="260" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="102" font-size="12" text-anchor="middle">ISR 1 (긴 처리 260 µs)</text>
<rect x="380" y="85" width="120" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="440" y="102" font-size="12" text-anchor="middle">ISR 2 (늦게라도)</text>
<rect x="520" y="85" width="120" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="580" y="102" font-size="12" text-anchor="middle">ISR 3</text>
<line x1="122" y1="80" x2="122" y2="114" stroke="#3f9a6b" stroke-dasharray="3,2"/>
<text x="10" y="150" font-size="12">(B) 맨 끝</text>
<rect x="120" y="135" width="260" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="152" font-size="12" text-anchor="middle">ISR 1 (긴 처리 260 µs)</text>
<line x1="378" y1="130" x2="378" y2="164" stroke="#d0564a" stroke-width="2"/>
<rect x="520" y="135" width="120" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="580" y="152" font-size="12" text-anchor="middle">ISR 3</text>
<text x="390" y="180" font-size="12">끝에서 지우면 사건 2까지 지워진다 → 잃음</text>
<text x="10" y="215" font-size="12">(C) 안 지움</text>
<rect x="120" y="200" width="520" height="24" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="380" y="217" font-size="12" text-anchor="middle">ISR → 복귀 → 즉시 재진입 → … (CPU를 놓지 않는다)</text>
<line x1="120" y1="260" x2="660" y2="260" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="640" y="280" font-size="12" text-anchor="middle">시간</text>
<text x="120" y="280" font-size="12">주기 200 µs, 10번에 1번은 처리가 주기보다 길다</text>
</svg>
```

그림 2 — 상태 비트 하나짜리 level IRQ에서 지우는 위치. (A) 맨 앞에서 지우면 처리 중에 온 사건 2가 비트를 다시 세워 ISR이 늦게라도 한 번 더 돈다. (B) 맨 끝에서 지우면 사건 2가 세운 비트까지 지워 사건 2가 사라진다. (C) 안 지우면 복귀하자마자 다시 들어온다.

**예제 1 — 실제 CMSDK TIMER0로 "맨 앞 / 맨 끝 / 안 지움"**

무엇을 확인하는 코드인지: TIMER0(IRQ8)가 200 µs마다 사건을 낸다. ISR은 보통 120 µs를 쓰지만 10번에 1번은 260 µs(주기보다 길다)를 쓴다. CMSDK 타이머의 인터럽트는 level이다 — `INTCLR`에 쓸 때까지 라인이 올라가 있고, QEMU의 NVIC는 예외 복귀 때 라인이 여전히 올라가 있으면 다시 pending으로 만든다(실칩과 같은 동작). 세 가지 클리어 위치에서 100 ms 동안 ISR 실행 횟수를 "예상 사건 수"와 비교하고, 우선순위 1인 heartbeat 태스크(1 tick마다 +1)가 몇 번 돌았는지 본다. (C)는 정말로 영원히 돌기 때문에 20만 번째 진입에서 타이머를 끄는 **실험용 안전장치**를 넣었다.

```c
/* ex1: 플래그 클리어 위치 — 실제 CMSDK TIMER0(level IRQ)로 "맨 앞 / 맨 끝 / 안 지움" 비교 */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
volatile uint32_t e4_switches;
#define PERIOD_CYC (200u * 25u)                    /* 200 us마다 사건 */
static volatile int mode;                          /* 0: 맨 앞, 1: 맨 끝, 2: 안 지움 */
static volatile uint32_t isr_n, seq, storm_guard, trip_cyc, trip_hb, trip_tick, hb;
static uint32_t work_cyc(uint32_t s) { return (s % 10u == 9u) ? 260u * 25u : 120u * 25u; }
void TIMER0_IRQHandler(void)
{
    if (mode == 0) TIMER_INTCLR(TIMER0_BASE) = 1;  /* (A) 사건을 받자마자 지운다 */
    isr_n++;
    if (mode == 2) {                                /* (C) 원인을 안 지우는 버그 */
        if (++storm_guard >= 200000u) {             /* 실험을 끝내려는 안전장치 */
            TIMER_CTRL(TIMER0_BASE) = 0; TIMER_INTCLR(TIMER0_BASE) = 1;
            trip_cyc = CYC(); trip_hb = hb; trip_tick = xTaskGetTickCountFromISR();
        }
        return;
    }
    busy_cyc(work_cyc(seq++));                      /* 10번에 1번은 주기(200us)보다 긴 260us */
    if (mode == 1) TIMER_INTCLR(TIMER0_BASE) = 1;  /* (B) 일이 끝난 뒤 지운다 */
}
/* hb: 낮은 우선순위 태스크의 생존 신호 */
static void hb_fn(void *p) { (void)p; for (;;) { hb++; vTaskDelay(1); } }
static void run(int m, const char *name)
{
    mode = m; isr_n = seq = storm_guard = 0;
    uint32_t t0 = CYC(), hb0 = hb, tk0 = xTaskGetTickCount();
    timer0_start(PERIOD_CYC);
    vTaskDelay(pdMS_TO_TICKS(100));
    timer0_stop();
    uint32_t el = CYC() - t0;
    if (m == 2) { board_printf_raw("(C) storm: %u us wall, hb +%u, tick +%u\n",
        (trip_cyc - t0) / 25u, trip_hb - hb0, trip_tick - tk0); return; }
    board_printf_raw("%s expected=%u isr_runs=%u lost=%d hb=+%u elapsed=%u us\n", name,
        el / PERIOD_CYC, isr_n, (int)(el / PERIOD_CYC) - (int)isr_n, hb - hb0, el / 25u);
}
static void main_fn(void *p)
{
    (void)p;
    irq_on(IRQ_T0, 0x40);
    run(0, "(A) clear first");
    run(1, "(B) clear last ");
    run(2, "(C) never clear");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(hb_fn, "hb", 256, NULL, 1, NULL);
    xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```text
(A) clear first expected=499 isr_runs=499 lost=0 hb=+100 elapsed=99993 us
(B) clear last  expected=499 isr_runs=454 lost=45 hb=+100 elapsed=99966 us
(C) storm: 121800 us wall, hb +1, tick +0
[board_exit] code=0
```

출력에서 볼 것:

- **(A) 맨 앞에서 지우면 499개 전부 처리**했다. 260 µs짜리 ISR 도중에 온 사건은 비트를 다시 세웠고, 복귀 직후 ISR이 한 번 더 돌았다(늦었지만 잃지 않았다).
- **(B) 맨 끝에서 지우면 45개 유실**. 10번에 1번(약 50번) 생긴 "처리 중 도착"이 대부분 지워졌다. 증상이 "가끔 사건 개수가 모자란다"라서 디버깅이 어렵다 — SSD에서 "완료 카운트가 가끔 하나 모자라 명령이 타임아웃"과 같은 종류다.
- **(C) 안 지우면 121.8 ms 동안 20만 번 재진입**(QEMU에서 1번당 약 0.6 µs — 실칩은 스태킹 12 cycle + 복귀가 더해진다). 그동안 **tick이 0개** 늘었다. SysTick은 가장 낮은 우선순위라 한 번도 들어가지 못했고, 따라서 RTOS의 시간 자체가 멈췄다. heartbeat +1은 폭주가 시작되기 전 200 µs 사이에 한 번 돈 것이다. `vTaskDelay`로 재던 "100 ms"는 실제로는 121.8 + 100 ms 뒤에야 끝났다 — 폭주가 시스템 시계를 왜곡한다는 증거다(§4.2).

**"맨 앞에서 지우기"가 언제나 정답은 아니다**

(A)가 이긴 이유는 이 장치의 상태 비트가 **사건이 있었다는 사실만** 담기 때문이다. 장치에 따라 규칙이 달라진다.

| 장치 상태 비트의 종류 | 예 | 지우는 법 · 위치 |
|---|---|---|
| 사건 래치 1비트 (W1C 또는 쓰기로 지움) | 타이머 update, DMA 완료, EXTI pending | 첫머리에서 지운다. 처리 중 온 사건은 다시 래치된다 |
| 여러 비트가 모인 W1C 상태 레지스터 | UART/SPI/I2C ISR, CAN 상태, NVMe 스타일 장치 | 첫머리에서 읽고 **읽은 비트만** 다시 써서 지운다 (§1.4) |
| 데이터와 짝인 플래그 | UART RXNE, ADC EOC, SPI RXNE | **데이터를 읽으면 하드웨어가 지운다** — 데이터 레지스터 읽기가 곧 ack |
| 레벨 상태 (조건이 사라져야 내려감) | "FIFO 비어 있지 않음", 센서 INT 핀 level | 원인(FIFO)을 비워야 내려간다. 다 못 비우면 다시 들어오는 게 정상 |
| 오류 플래그 | UART ORE(overrun), FE, CAN error | 반드시 지운다 — 안 지우면 끼인 IRQ의 대표 원인 (§4.1) |
| 읽어야 지워지는 상태 (read-to-clear) | 일부 센서 INT_STATUS, 옛 MCU의 SR → DR 순서 | 읽는 순서 자체가 ack. 디버거로 레지스터 창을 열어 두면 디버거가 지워 버린다 |

데이터와 짝인 플래그는 Don이 STM32 계열에서 본 그대로다. 예를 들어 STM32 USART에서 RXNE 인터럽트를 켜면 overrun(ORE)도 같은 인터럽트를 낸다. 수신 데이터를 늦게 읽어 ORE가 서면 데이터 레지스터 읽기만으로는 ORE가 안 지워지는 계열(F7 · H7 · G4는 ICR의 ORECF에 써야 한다)이 있고, 그걸 모르는 드라이버는 ORE 때문에 ISR에 갇힌다. 칩 세대마다 지우는 법이 다르므로 레퍼런스 매뉴얼의 "how to clear" 열을 반드시 확인한다.

### 1.4 W1C 상태 레지스터 — "전부 지우기"와 "읽기 전에 지우기"

W1C(write-1-to-clear) 레지스터는 1을 쓴 비트만 0이 된다. 읽기-수정-쓰기 없이 원하는 비트만 지울 수 있게 하려는 설계다. 그런데 흔한 실수가 두 가지 있다. 첫째, ISR 끝에서 `STATUS = 0xFFFFFFFF`로 "깔끔하게" 다 지우는 것. 읽은 뒤에 새로 선 비트까지 지운다. 둘째, 데이터와 짝인 비트를 데이터를 꺼내기 전에 지우는 것.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">W1C 상태 레지스터 — 읽은 뒤 "전부 지우기"가 사건을 삼키는 순간</text>
<text x="95" y="48" font-size="12" text-anchor="middle">드라이버 ISR</text>
<text x="340" y="48" font-size="12" text-anchor="middle">STATUS 레지스터</text>
<text x="585" y="48" font-size="12" text-anchor="middle">하드웨어</text>
<line x1="95" y1="56" x2="95" y2="275" stroke="#888" stroke-dasharray="3,3"/>
<line x1="340" y1="56" x2="340" y2="275" stroke="#888" stroke-dasharray="3,3"/>
<line x1="585" y1="56" x2="585" y2="275" stroke="#888" stroke-dasharray="3,3"/>
<rect x="285" y="66" width="110" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="83" font-size="12" text-anchor="middle">RX = 1</text>
<line x1="330" y1="100" x2="100" y2="100" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="215" y="95" font-size="12" text-anchor="middle">st = STATUS (RX)</text>
<line x1="580" y1="130" x2="350" y2="130" stroke="#3f9a6b" marker-end="url(#ah3)"/>
<text x="465" y="125" font-size="12" text-anchor="middle">새 사건: TX 비트 세움</text>
<rect x="285" y="138" width="110" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="155" font-size="12" text-anchor="middle">RX = 1, TX = 1</text>
<text x="20" y="180" font-size="12">나쁜 예</text>
<line x1="100" y1="190" x2="330" y2="190" stroke="#d0564a" marker-end="url(#ah3)"/>
<text x="215" y="185" font-size="12" text-anchor="middle">STATUS = 0xFFFFFFFF</text>
<text x="465" y="195" font-size="12" text-anchor="middle">TX도 지워짐 → 아무도 모름</text>
<text x="20" y="235" font-size="12">좋은 예</text>
<line x1="100" y1="245" x2="330" y2="245" stroke="#3f9a6b" marker-end="url(#ah3)"/>
<text x="215" y="240" font-size="12" text-anchor="middle">STATUS = st (RX만)</text>
<text x="465" y="250" font-size="12" text-anchor="middle">TX는 남아 IRQ가 다시 온다</text>
</svg>
```

그림 3 — W1C 레지스터에서 `STATUS = 0xFFFFFFFF`가 사건을 삼키는 순간. 드라이버가 RX를 읽은 뒤 하드웨어가 TX를 세웠는데, 끝에서 전부 지우면 TX는 아무도 모르게 사라진다. 읽은 값 `st`를 그대로 다시 쓰면 TX는 남아 IRQ가 다시 온다.

**예제 2 — 에뮬레이션 W1C 장치로 세 가지 순서 비교**

무엇을 확인하는 코드인지: DUALTIMER ISR(우선순위 0x00, RTOS API를 부르지 않는다)이 **하드웨어 역할**을 한다(에뮬레이션). 4~40 µs(평균 22 µs) 간격으로 RX 사건과 TX 사건을 번갈아 내고, RX면 데이터 레지스터에 순번을 쓴다(아직 안 읽은 데이터가 있으면 버리고 overrun을 센다). 사건마다 장치 IRQ(IRQ9)를 올린다. 드라이버 ISR(0x40)은 약 15 µs를 쓴다. 세 가지 순서로 50 ms씩 돌리고, RX 순번의 연속성으로 유실 · 중복을 센다. W1C의 원자성은 실제 하드웨어에서는 공짜지만 에뮬레이션에서는 `cpsid i`로 흉내 냈다.

```c
/* ex2: W1C 상태 레지스터를 "읽은 비트만" vs "전부" 지우기, 데이터를 읽기 전에 지우기 (에뮬레이션 장치) */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
volatile uint32_t e4_switches;
/* ---- 에뮬레이션 장치: DUALTIMER ISR(최고 우선순위 0x00)가 "하드웨어" 역할 ---- */
#define ST_RX 1u
#define ST_TX 2u
static volatile uint32_t dev_status, dev_data, hw_seq, hw_ovr, hw_tx, n;
static inline void dev_w1c(uint32_t m)        /* 실제 HW의 W1C는 원자적이다 → 에뮬레이션은 IRQ를 잠깐 막는다 */
{ __asm volatile("cpsid i" ::: "memory"); dev_status &= ~m; __asm volatile("cpsie i" ::: "memory"); }
static uint32_t rnd = 12345u;
void DUALTIMER_IRQHandler(void)
{
    DT_INTCLR = 1;
    rnd = rnd * 1103515245u + 12345u;
    REG32(0x40002018u) = (4u + (rnd >> 16) % 37u) * 25u;   /* BGLOAD: 다음 간격 4~40 us (평균 22) */
    if (++n & 1u) {                           /* RX 사건: 데이터 레지스터가 비어 있어야 새 샘플을 쓴다 */
        if (dev_status & ST_RX) hw_ovr++;     /* HW overrun: 아직 안 읽었으면 버리고 OVR 카운트 */
        else { dev_data = ++hw_seq; dev_status |= ST_RX; }
    } else { hw_tx++; dev_status |= ST_TX; }  /* TX 완료 사건 */
    NVIC_ISPR0 = 1u << IRQ_SW;                /* 장치 IRQ 올림 */
}
/* ---- 드라이버 ISR (IRQ9, 0x40) ---- */
static volatile int mode;                     /* 0: 읽은 비트만 지움, 1: 전부 지움, 2: 데이터 읽기 전에 지움 */
static volatile uint32_t got_rx, got_tx, last, lost, dup;
static void take(uint32_t d) { got_rx++; if (d == last) dup++; else if (d != last + 1u) lost += d - last - 1u; last = d; }
void TIMER1_IRQHandler(void)
{
    uint32_t st = dev_status;
    if (mode == 0) dev_w1c(st);                /* (A) 본 비트만 먼저 지운다 */
    if (mode == 2) dev_w1c(st & ~ST_RX);
    if (mode == 2 && (st & ST_RX)) { dev_w1c(ST_RX); busy_us(6); take(dev_data); }  /* (C) */
    else if (st & ST_RX) take(dev_data);
    if (st & ST_TX) got_tx++;
    busy_us(9);                                /* 상태 기계 · 버퍼 관리 흉내 */
    if (mode == 1) dev_w1c(0xFFFFFFFFu);       /* (B) 끝에서 "전부" 지운다 */
}
static void run(int m, const char *name)
{
    mode = m; dev_status = 0; rnd = 12345u; hw_seq = hw_ovr = hw_tx = n = 0; got_rx = got_tx = last = lost = dup = 0;
    DT_LOAD = 23u * 25u; DT_CTRL = 0xE2;      /* RX 또는 TX 사건, 간격은 HW 모델이 바꾼다 */
    vTaskDelay(pdMS_TO_TICKS(50));
    DT_CTRL = 0; vTaskDelay(1);
    board_printf_raw("%s rx: hw=%u got=%u lost=%u dup=%u hw_ovr=%u | tx: hw=%u got=%u\n",
        name, hw_seq, got_rx, lost, dup, hw_ovr, hw_tx, got_tx);
}
static void main_fn(void *p)
{
    (void)p;
    irq_on(IRQ_DT, 0x00); irq_on(IRQ_SW, 0x40);
    run(0, "(A) w1c(read bits) first");
    run(1, "(B) w1c(all) at end     ");
    run(2, "(C) clear RX before read");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```text
(A) w1c(read bits) first rx: hw=1128 got=1128 lost=0 dup=0 hw_ovr=0 | tx: hw=1128 got=1128
(B) w1c(all) at end      rx: hw=1122 got=884 lost=238 dup=0 hw_ovr=6 | tx: hw=1128 got=873
(C) clear RX before read rx: hw=1128 got=1128 lost=6 dup=6 hw_ovr=0 | tx: hw=1128 got=1127
[board_exit] code=0
```

출력에서 볼 것:

- **(A) 읽은 비트만 첫머리에서 지우기: RX 1128 · TX 1128 전부.** 처리 중에 선 비트는 지워지지 않고 남아, 하드웨어가 올린 IRQ로 ISR이 다시 돌며 처리한다.
- **(B) 끝에서 전부 지우기: RX 238개(21%), TX 255개(23%) 유실.** 그림 3의 장면이 22 µs마다 일어난다. hw_ovr 6은 RX 비트가 지워지지 않은 채 남아 있던 틈에 하드웨어가 새 샘플을 버린 경우다. hw 쪽 RX 수(1122)가 다른 줄(1128)과 다른 것도 그 6개 때문이다.
- **(C) RX 비트를 데이터 읽기 전에 지우기: 6개 유실 + 6개 중복.** 지운 순간과 데이터를 읽는 순간 사이(여기서는 6 µs의 장부 정리)에 하드웨어가 "비었다"고 보고 새 샘플을 썼다. 드라이버는 새 샘플을 읽었고(앞 샘플 유실), 그 새 샘플에 대한 IRQ로 다시 들어와 같은 값을 또 읽었다(중복). overrun 카운터도 0이라 **하드웨어도 경고하지 않는다** — 가장 찾기 어려운 버그다. TX 1127/1128은 마지막 사건이 측정 종료 직전에 와서 아직 처리되지 않은 것이다.
- 교훈을 한 줄로: **상태는 한 번 읽고, 읽은 비트만 지우고, 데이터 레지스터는 꺼낸 뒤에 놓아준다.**

### 1.5 마지막 쓰기와 예외 복귀 — 쓰기 버퍼 · DSB

§1.3의 (b) "가짜 재진입"은 쓰기 버퍼 때문에 생긴다. 쓰기 버퍼와 posted write, 그리고 DSB가 "앞의 쓰기가 끝날 때까지 기다린다"는 정의는 D8 §7.1에서 자세히 봤다(장면 1이 바로 "플래그 클리어 후 DSB"다). 여기서는 ISR 설계 관점만 정리한다.

- ISR의 **마지막 명령이 플래그 클리어 쓰기**이면, 그 쓰기가 느린 APB 주변장치에 도착하기 전에 예외 복귀가 끝날 수 있다. NVIC는 라인이 아직 올라가 있으니 IRQ를 다시 pending으로 만들고, ISR이 **원인 없이 한 번 더** 들어온다. 그 ISR이 상태 확인 없이 카운트를 올리면 이중 카운트가 된다.
- 고치는 법은 세 가지다. ① **지우기를 ISR 앞쪽으로 옮긴다**(§1.3의 (A) — 쓰기가 도착할 시간을 번다). ② 클리어 뒤에 `__DSB()`를 넣는다. ③ 같은 주변장치 레지스터를 **한 번 다시 읽는다** — 읽기는 앞선 쓰기가 끝나야 돌아오므로 버스 브리지까지 확실히 비운다. 칩 벤더 문서는 보통 ③이나 ②를 권한다.
- 그리고 무엇보다 ISR이 **상태 비트를 확인하고, 원인이 없으면 그냥 나가게** 짠다. 공유 라인 · 가짜 재진입 · 노이즈에 모두 안전해진다.

**예제 3 — 세 가지 꼬리, 그리고 QEMU가 보여 주지 못하는 것**

무엇을 확인하는 코드인지: 지우기를 ISR 맨 끝에 두되 꼬리를 셋으로 바꾼다 — 그냥 / `dsb` / 다시 읽기. ISR 첫머리에서 `INTSTATUS`가 0인데 들어온 횟수(가짜 재진입)를 세고, 생성 코드를 objdump로 본다.

```c
/* ex3: "맨 끝에서 지우기"의 세 가지 꼬리 — 그냥 / DSB / 다시 읽기. 가짜 재진입(원인 없는 진입)을 센다 */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
volatile uint32_t e4_switches;
#define T0_STATUS REG32(TIMER0_BASE + 0xC)    /* 읽으면 INTSTATUS, 쓰면 INTCLEAR */
static volatile int mode;
static volatile uint32_t runs, spurious;
__attribute__((noinline)) void isr_plain(void) { busy_us(20); T0_STATUS = 1; }
__attribute__((noinline)) void isr_dsb(void)   { busy_us(20); T0_STATUS = 1; __asm volatile("dsb" ::: "memory"); }
__attribute__((noinline)) void isr_rb(void)    { busy_us(20); T0_STATUS = 1; (void)T0_STATUS; }
void TIMER0_IRQHandler(void)
{
    runs++;
    if ((T0_STATUS & 1u) == 0) { spurious++; return; }   /* 원인이 없는데 들어왔다 */
    if (mode == 0) isr_plain(); else if (mode == 1) isr_dsb(); else isr_rb();
}
static void main_fn(void *p)
{
    (void)p;
    static const char *nm[3] = { "clear last, plain   ", "clear last + DSB    ", "clear last + readback" };
    irq_on(IRQ_T0, 0x40);
    for (int m = 0; m < 3; m++) {
        mode = m; runs = spurious = 0;
        timer0_start(100u * 25u);
        vTaskDelay(pdMS_TO_TICKS(50));
        timer0_stop();
        board_printf_raw("%s runs=%u spurious=%u\n", nm[m], runs, spurious);
    }
    board_exit(0);
}
int main(void)
{
    xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```text
clear last, plain    runs=499 spurious=0
clear last + DSB     runs=499 spurious=0
clear last + readback runs=499 spurious=0
[board_exit] code=0
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/E4/ex3 && for f in isr_plain isr_dsb; do arm-none-eabi-objdump -d --no-show-raw-insn app.elf | awk "/<$f>:/,/^\$/"; done'
```

```asm
00000148 <isr_plain>:
     148:	ldr	r2, [pc, #20]	@ (160 <isr_plain+0x18>)
     14a:	ldr	r1, [r2, #24]
     14c:	ldr	r3, [r2, #24]
     14e:	subs	r3, r3, r1
     150:	cmp.w	r3, #500	@ 0x1f4
     154:	bcc.n	14c <isr_plain+0x4>
     156:	mov.w	r3, #1073741824	@ 0x40000000
     15a:	movs	r2, #1
     15c:	str	r2, [r3, #12]
     15e:	bx	lr
     160:	.word	0x40028000

00000164 <isr_dsb>:
     164:	ldr	r2, [pc, #24]	@ (180 <isr_dsb+0x1c>)
     166:	ldr	r1, [r2, #24]
     168:	ldr	r3, [r2, #24]
     16a:	subs	r3, r3, r1
     16c:	cmp.w	r3, #500	@ 0x1f4
     170:	bcc.n	168 <isr_dsb+0x4>
     172:	mov.w	r3, #1073741824	@ 0x40000000
     176:	movs	r2, #1
     178:	str	r2, [r3, #12]
     17a:	dsb	sy
     17e:	bx	lr
     180:	.word	0x40028000
```

출력에서 볼 것:

- **세 꼬리 모두 spurious=0.** QEMU는 쓰기를 즉시 반영하고 쓰기 버퍼 · 버스 지연을 모델링하지 않으므로, 실칩에서 생기는 가짜 재진입을 재현할 수 없다. 이 예제는 "재현되지 않음"을 정직하게 보여 주는 대조군이다. 실칩(특히 버스가 복잡한 M7 · 브리지 뒤의 APB 주변장치)에서는 plain 꼬리가 가짜 재진입을 낼 수 있다.
- 생성 코드: `str r2, [r3, #12]`(INTCLR 쓰기) 바로 뒤가 `bx lr`(복귀)이다. DSB 버전은 그 사이에 `dsb sy` 하나가 들어간다. 다시 읽기 버전(`isr_rb`)은 `ldr r3, [r3, #12]`가 들어간다. 비용은 명령 하나지만, 그 명령이 버스가 비워질 때까지 CPU를 세운다.
- 실무 권장: 상태 확인으로 가짜 진입을 무해하게 만들고, 클리어는 앞쪽에 두고, 칩 errata가 요구하면 DSB나 다시 읽기를 더한다.

### 1.6 사건을 기록하고, 처리는 태스크에서 — 시각은 ISR에서 찍는다

"기록하고 나간다"에서 가장 값진 기록은 **사건 시각**이다. 태스크가 깨어나서 시각을 찍으면 그 값에는 깨움 지연(D7의 8.1 µs)에 더해 **더 높은 우선순위 태스크가 그사이 돈 시간**이 섞인다. 자세 추정기 · 센서 융합은 샘플 사이 간격 `dt`로 적분한다. `dt`가 흔들리면 추정이 흔들린다.

**예제 4 — ISR 타임스탬프 vs 태스크 타임스탬프**

무엇을 확인하는 코드인지: IMU data-ready를 흉내 낸 TIMER0(997 µs 주기 — tick과 위상이 조금씩 흘러가게)의 ISR이 ①지우기 ②시각 + 순번 기록 ③링에 넣기 ④notify만 한다. 처리 태스크(우선순위 2)는 깨어나면 링을 비우면서 "자기가 찍은 시각"도 같이 기록한다. 더 높은 우선순위(3)의 제어 루프가 3 ms마다 250 µs를 쓴다. 2000개 샘플에서 두 타임스탬프의 차이와 샘플 간격 `dt`의 범위를 비교한다.

```c
/* ex4: 사건 시각은 ISR에서 찍는다 — ISR 타임스탬프 vs 태스크가 깨어나서 찍은 타임스탬프 */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
#include "ring.h"
volatile uint32_t e4_switches;
#define P_US 997u                                    /* IMU data-ready ~1 kHz (tick과 위상이 흘러간다) */
#define UPD(lo, hi, v) do { if ((v) < (lo)) (lo) = (v); if ((v) > (hi)) (hi) = (v); } while (0)
static ring_t rq;
static TaskHandle_t h_imu;
static volatile uint32_t seq;
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;                   /* 1) 원인 지우기 */
    ev_t e = { CYC(), seq++ };                       /* 2) 시각 + 순번 기록 */
    ring_put(&rq, e);                                /* 3) 기록을 넘긴다 */
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR(h_imu, &w);               /* 4) 처리할 태스크를 깨운다 */
    portYIELD_FROM_ISR(w);
}
static void ctl_fn(void *p)                          /* 더 높은 우선순위: 3 ms마다 250 us 쓰는 제어 루프 */
{
    (void)p; TickType_t t = xTaskGetTickCount();
    for (;;) { vTaskDelayUntil(&t, 3); busy_us(250); }
}
static void imu_fn(void *p)
{
    (void)p;
    uint32_t n = 0, prev_i = 0, prev_t = 0, emax = 0, esum = 0;
    uint32_t dimin = ~0u, dimax = 0, dtmin = ~0u, dtmax = 0;
    while (n < 2000u) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ev_t e;
        while (ring_get(&rq, &e)) {
            uint32_t t_task = CYC();                 /* 나쁜 예: 여기서 시각을 찍으면? */
            uint32_t err = t_task - e.ts; esum += err; if (err > emax) emax = err;
            if (n) {
                uint32_t di = e.ts - prev_i, dt = t_task - prev_t;
                UPD(dimin, dimax, di);
                UPD(dtmin, dtmax, dt);
            }
            prev_i = e.ts; prev_t = t_task; n++;
        }
    }
    board_printf_raw("n=%u  task_stamp - isr_stamp: avg=%u us max=%u us\n", n, esum / n / 25u, emax / 25u);
    board_printf_raw("dt from ISR stamps : %u .. %u us\n", dimin / 25u, dimax / 25u);
    board_printf_raw("dt from task stamps: %u .. %u us\n", dtmin / 25u, dtmax / 25u);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(ctl_fn, "ctl", 256, NULL, 3, NULL);
    xTaskCreate(imu_fn, "imu", 256, NULL, 2, &h_imu);
    irq_on(IRQ_T0, 0x40);
    timer0_start(P_US * 25u);
    vTaskStartScheduler();
    return 0;
}
```

```text
n=2000  task_stamp - isr_stamp: avg=21 us max=275 us
dt from ISR stamps : 993 .. 1000 us
dt from task stamps: 730 .. 1263 us
[board_exit] code=0
```

출력에서 볼 것:

- **ISR 타임스탬프의 간격은 993~1000 µs**(참값 997 µs, µs 단위 내림 포함 ±4 µs). 이 흔들림은 커널 크리티컬 섹션(BASEPRI로 0x20 이하를 막는 구간)이 IRQ 진입을 잠깐 늦춘 것이다 — 측정 · 분해 방법은 E3.
- **태스크 타임스탬프의 간격은 730~1263 µs.** 평균 오차는 21 µs지만 최대 275 µs — 제어 루프 250 µs + 깨움 비용이다. 이 `dt`로 자이로를 적분하면 몇 샘플마다 25% 넘게 틀린 각도 증가분이 들어간다.
- 링에 넣은 기록(`ts`, `seq`) 덕분에 태스크는 늦게 깨어나도 **정확한 시각과 빠진 샘플 여부**를 안다. 이것이 "이벤트를 기록하고 나중에 처리"의 진짜 의미다. 더 정밀하게는 타이머 입력 캡처(E5)로 하드웨어가 시각을 찍게 한다.

### 1.7 Bare-Metal super-loop와 같은 원칙, 다른 마지막 한 줄

RTOS 없이 같은 구조를 쓰면 ISR의 ④가 `ring_put` + `volatile` 플래그로 끝나고, super-loop가 플래그(또는 링 count)를 폴링한다. 차이는 둘이다. 첫째, super-loop는 **루프 한 바퀴의 최악 시간**이 응답 지연의 상한이다(A2의 M1 · M2). RTOS는 핸들러 태스크의 **우선순위**가 상한을 정한다(§3). 둘째, super-loop에서 "깨어나면 비울 때까지 처리"는 자연스럽지만, RTOS에서는 notification이 **개수를 센다**는 점을 활용해 "몇 번 깨웠는지"와 "링에 몇 개 있는지"를 구분해야 한다(§2.2). 플래그 · 링을 공유하는 레이스(찢어진 읽기, RMW)는 C7 · D8에서 다뤘다.

### 1.8 이 절의 함정

- 상태를 확인하지 않고 "들어왔으니 사건 하나"로 세는 ISR — 공유 라인, 가짜 재진입, 노이즈에서 전부 이중 카운트가 된다.
- ISR 끝의 `STATUS = 0xFFFFFFFF` — 예제 2에서 사건의 21~23%를 삼켰다.
- 데이터 레지스터를 읽기 전에 RX 비트 지우기 — 유실과 중복이 짝으로 나오고 overrun도 안 뜬다.
- 디버거 레지스터 창에 read-to-clear 레지스터를 띄워 두기 — 디버거가 읽어서 사건을 지운다. "디버거를 붙이면 버그가 사라진다"의 단골 원인.
- 타임스탬프를 태스크에서 찍기 — `dt`가 수백 µs 흔들린다.
- ISR에서 "조금만" 처리하기로 하고 파싱 · 로그를 넣기 — ISR 길이는 다른 모든 것의 지연에 더해진다(D7 예제 1).
---

## 2. 넘기는 단위 — 사건마다 깨우기 vs 모아서 깨우기

### 2.1 직관 — 엘리베이터와 택배 트럭

엘리베이터가 손님 한 명 탈 때마다 출발하면 기다리는 시간은 가장 짧지만 운행 횟수(에너지)가 가장 많다. 택배 트럭은 짐이 찰 때까지 기다리면 운행이 줄지만, 첫 짐은 오래 기다린다. 그래서 택배 회사는 **"트럭이 차거나, 마감 시각이 되면"** 출발한다. ISR → 태스크도 같다. 깨움(context switch 두 번 + 커널 API)은 공짜가 아니고, 사건이 초당 수만 개면 깨움 비용이 일 자체보다 커진다.

### 2.2 세 가지 넘기는 형태

| 형태 | ISR 쪽 | 태스크 쪽 | 장점 | 비용 |
|---|---|---|---|---|
| 사건마다 깨우기 | 매번 `vTaskNotifyGiveFromISR` | `ulTaskNotifyTake(pdTRUE)` → 링 비우기 | 지연 최소 | 사건당 깨움 1번 |
| 링 + 1번 깨우기 (자연 합침) | 링에 넣고 매번 notify, 단 태스크가 바쁘면 깨움이 합쳐짐 | 깨어나면 **비울 때까지** | 부하가 크면 저절로 묶인다 | 한가할 때는 사건당 깨움 |
| N개 또는 T µs | 링에 넣고, N개째에 notify, 첫 항목이 T 타이머를 건다 | 같다 | CPU 상한을 정할 수 있다 | 지연이 최대 (N-1) × 간격 또는 T |

두 번째 형태가 이미 "부하가 크면 저절로 묶인다"는 점이 중요하다. FreeRTOS notification은 `vTaskNotifyGiveFromISR`가 값을 1씩 올리고 `ulTaskNotifyTake(pdTRUE, …)`가 누적값을 돌려주며 0으로 지운다(D5 §1, §7.1, 이 노트 §6의 소스). 태스크가 링을 비우는 동안 온 notify는 값만 올려 두므로, 다음 Take는 블록하지 않고 바로 돌아와 빈 링을 한 번 확인하고 끝난다. 그래서 핸들러는 **"깨어날 때마다 하나"가 아니라 "깨어나면 비울 때까지"** 처리해야 한다(D7 §2.4).

세 번째 형태는 SSD의 NVMe interrupt coalescing(Threshold + Time), NIC의 interrupt moderation(Linux `ethtool -C`의 rx-frames · rx-usecs), 그리고 하드웨어 쪽의 **UART RX FIFO 임계값 + receiver timeout(idle line)**, IMU **FIFO watermark** 인터럽트와 같은 구조다. 하드웨어가 해 주면 그게 가장 싸다(E6). 소프트웨어로 할 때는 아래 예제처럼 ISR과 one-shot 타이머 하나로 만든다. D5 §4.3의 stream buffer trigger level은 "N개"만 있는 형태이고, T는 수신 쪽 블록 시간으로 대신한다(그래서 T가 tick 해상도에 묶인다).

### 2.3 예제 5 — N을 1에서 64까지: 전환 수 · CPU · 종단 지연

무엇을 확인하는 코드인지: TIMER0가 20 kHz(50 µs 간격)로 사건을 낸다(CAN 프레임, ADC 블록 같은 고속 원천). ISR은 사건 시각을 링에 넣고, N개가 모이면 처리 태스크를 깨운다. 묶음의 첫 항목이 들어올 때 DUALTIMER를 T = 2 ms one-shot으로 걸고, N개가 차기 전에 T가 지나면 DUALTIMER ISR이 남은 것을 내보낸다. 처리 태스크는 항목당 2 µs를 쓰고 "사건 시각 → 처리 시각"(종단 지연)을 잰다. 가장 낮은 우선순위의 `bg` 태스크가 남는 CPU를 세고(사건 없을 때와의 비율로 CPU 사용률 계산), trace 훅으로 초당 context switch를 센다. 마지막 두 줄은 느린 원천(1 kHz)에서 T가 없을 때와 있을 때다.

```c
/* ex5: 사건마다 깨우기 vs "N개 또는 T us" 모아 깨우기 — 전환 수 · CPU · 종단 지연 */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
#include "ring.h"
volatile uint32_t e4_switches;
static ring_t rq;
static TaskHandle_t h_rx;
static volatile uint32_t N = 1, T_US = 2000, batch, seq, wakes, bg, drop, lat_n, lat_sum, lat_max;
static void kick(BaseType_t *w) { DT_CTRL = 0; batch = 0; vTaskNotifyGiveFromISR(h_rx, w); }
void TIMER0_IRQHandler(void)                         /* 사건 원천: 20 kHz (CAN 프레임, ADC 블록 …) */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    ev_t e = { CYC(), seq++ };
    BaseType_t w = pdFALSE;
    if (!ring_put(&rq, e)) { drop++; return; }
    if (++batch >= N) kick(&w);                      /* N개 모이면 깨운다 */
    else if (batch == 1 && T_US) { DT_LOAD = T_US * 25u; DT_CTRL = 0xA3; }  /* 첫 항목이 T 타이머를 건다 */
    portYIELD_FROM_ISR(w);
}
void DUALTIMER_IRQHandler(void)                      /* T us 동안 N개가 안 차면 남은 것을 내보낸다 */
{
    DT_INTCLR = 1;
    BaseType_t w = pdFALSE;
    if (batch) kick(&w);
    portYIELD_FROM_ISR(w);
}
static void rx_fn(void *p)                           /* 처리 태스크: 항목당 2 us */
{
    (void)p; ev_t e;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); wakes++;
        while (ring_get(&rq, &e)) {
            busy_us(2);
            uint32_t l = CYC() - e.ts; lat_n++; lat_sum += l; if (l > lat_max) lat_max = l;
        }
    }
}
static void bg_fn(void *p) { (void)p; for (;;) bg++; }   /* 남는 CPU를 재는 바닥 태스크 */
static uint32_t run(uint32_t n, uint32_t t_us, uint32_t period_us, uint32_t base)
{
    N = n; T_US = t_us; batch = 0; seq = wakes = drop = lat_n = lat_sum = lat_max = 0;
    vTaskDelay(1);
    uint32_t bg0 = bg, sw0 = e4_switches;
    if (period_us) timer0_start(period_us * 25u);
    vTaskDelay(pdMS_TO_TICKS(200)); timer0_stop();
    uint32_t used = bg - bg0, sw = e4_switches - sw0;
    if (period_us)
        board_printf_raw("P=%u N=%u T=%u: ev=%u wakes/s=%u sw/s=%u cpu=%u%% lat avg=%u max=%u us drop=%u\n",
            period_us, n, t_us, seq, wakes * 5u, sw * 5u, 100u - (uint32_t)((uint64_t)used * 100u / base),
            lat_n ? lat_sum / lat_n / 25u : 0, lat_max / 25u, drop);
    xTaskNotifyGive(h_rx); vTaskDelay(pdMS_TO_TICKS(5));   /* 측정 뒤 남은 꼬리 비우기 */
    return used;
}
static void main_fn(void *p)
{
    (void)p; irq_on(IRQ_T0, 0x40); irq_on(IRQ_DT, 0x40);
    uint32_t base = run(1, 0, 0, 1); board_printf_raw("baseline bg=%u per 200 ms\n", base);
    static const uint32_t ns[] = { 1, 2, 4, 8, 16, 32, 64 };
    for (unsigned i = 0; i < 7; i++) run(ns[i], 2000, 50, base);
    run(16, 0, 1000, base); run(16, 2000, 1000, base);   /* 느린 원천(1 kHz): T 없음 / T = 2 ms */
    board_exit(0);
}
int main(void)
{
    xTaskCreate(bg_fn, "bg", 256, NULL, 1, NULL); xTaskCreate(rx_fn, "rx", 256, NULL, 3, &h_rx);
    xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```text
baseline bg=1560090 per 200 ms
P=50 N=1 T=2000: ev=3999 wakes/s=19995 sw/s=40000 cpu=44% lat avg=11 max=11 us drop=0
P=50 N=2 T=2000: ev=3999 wakes/s=9995 sw/s=20000 cpu=27% lat avg=38 max=61 us drop=0
P=50 N=4 T=2000: ev=3999 wakes/s=4995 sw/s=10000 cpu=18% lat avg=91 max=161 us drop=0
P=50 N=8 T=2000: ev=3999 wakes/s=2495 sw/s=5000 cpu=14% lat avg=197 max=361 us drop=0
P=50 N=16 T=2000: ev=3999 wakes/s=1245 sw/s=2500 cpu=11% lat avg=359 max=761 us drop=0
P=50 N=32 T=2000: ev=3999 wakes/s=620 sw/s=1250 cpu=10% lat avg=734 max=1561 us drop=0
P=50 N=64 T=2000: ev=3999 wakes/s=485 sw/s=980 cpu=10% lat avg=973 max=2012 us drop=0
P=1000 N=16 T=0: ev=199 wakes/s=60 sw/s=130 cpu=1% lat avg=7534 max=15011 us drop=0
P=1000 N=16 T=2000: ev=199 wakes/s=330 sw/s=670 cpu=2% lat avg=1015 max=2012 us drop=0
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">예제 5 실측 — 20 kHz 사건, "N개 또는 T=2 ms" 모아 깨우기 (QEMU icount)</text>
<line x1="50" y1="270" x2="320" y2="270" stroke="currentColor"/>
<line x1="50" y1="270" x2="50" y2="60" stroke="currentColor"/>
<line x1="46" y1="270.0" x2="50" y2="270.0" stroke="currentColor"/>
<text x="43" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="46" y1="228.0" x2="50" y2="228.0" stroke="currentColor"/>
<text x="43" y="232.0" font-size="12" text-anchor="end">10</text>
<line x1="46" y1="186.0" x2="50" y2="186.0" stroke="currentColor"/>
<text x="43" y="190.0" font-size="12" text-anchor="end">20</text>
<line x1="46" y1="144.0" x2="50" y2="144.0" stroke="currentColor"/>
<text x="43" y="148.0" font-size="12" text-anchor="end">30</text>
<line x1="46" y1="102.0" x2="50" y2="102.0" stroke="currentColor"/>
<text x="43" y="106.0" font-size="12" text-anchor="end">40</text>
<line x1="46" y1="60.0" x2="50" y2="60.0" stroke="currentColor"/>
<text x="43" y="64.0" font-size="12" text-anchor="end">50</text>
<text x="50" y="50" font-size="12">CPU 사용률 (%)</text>
<rect x="56.3" y="85.2" width="26" height="184.8" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="69.3" y="80.2" font-size="12" text-anchor="middle">44</text>
<text x="69.3" y="286" font-size="12" text-anchor="middle">1</text>
<rect x="94.9" y="156.6" width="26" height="113.4" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="107.9" y="151.6" font-size="12" text-anchor="middle">27</text>
<text x="107.9" y="286" font-size="12" text-anchor="middle">2</text>
<rect x="133.4" y="194.4" width="26" height="75.6" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="146.4" y="189.4" font-size="12" text-anchor="middle">18</text>
<text x="146.4" y="286" font-size="12" text-anchor="middle">4</text>
<rect x="172.0" y="211.2" width="26" height="58.8" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="185.0" y="206.2" font-size="12" text-anchor="middle">14</text>
<text x="185.0" y="286" font-size="12" text-anchor="middle">8</text>
<rect x="210.6" y="223.8" width="26" height="46.2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="223.6" y="218.8" font-size="12" text-anchor="middle">11</text>
<text x="223.6" y="286" font-size="12" text-anchor="middle">16</text>
<rect x="249.1" y="228.0" width="26" height="42.0" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="262.1" y="223.0" font-size="12" text-anchor="middle">10</text>
<text x="262.1" y="286" font-size="12" text-anchor="middle">32</text>
<rect x="287.7" y="228.0" width="26" height="42.0" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="300.7" y="223.0" font-size="12" text-anchor="middle">10</text>
<text x="300.7" y="286" font-size="12" text-anchor="middle">64</text>
<text x="185" y="304" font-size="12" text-anchor="middle">N (한 번 깨울 때 모으는 개수)</text>
<line x1="400" y1="270" x2="660" y2="270" stroke="currentColor"/>
<line x1="400" y1="270" x2="400" y2="60" stroke="currentColor"/>
<line x1="396" y1="248.9" x2="660" y2="248.9" stroke="#888" stroke-opacity="0.3"/>
<text x="393" y="252.9" font-size="12" text-anchor="end">10</text>
<line x1="396" y1="178.9" x2="660" y2="178.9" stroke="#888" stroke-opacity="0.3"/>
<text x="393" y="182.9" font-size="12" text-anchor="end">100</text>
<line x1="396" y1="108.9" x2="660" y2="108.9" stroke="#888" stroke-opacity="0.3"/>
<text x="393" y="112.9" font-size="12" text-anchor="end">1000</text>
<text x="400" y="50" font-size="12">종단 지연 (µs, log)</text>
<polyline points="418.6,246.0 455.7,194.0 492.9,164.5 530.0,139.9 567.1,117.2 604.3,95.4 641.4,87.7" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="418.6" cy="246.0" r="3" fill="#e08a3c"/>
<circle cx="455.7" cy="194.0" r="3" fill="#e08a3c"/>
<circle cx="492.9" cy="164.5" r="3" fill="#e08a3c"/>
<circle cx="530.0" cy="139.9" r="3" fill="#e08a3c"/>
<circle cx="567.1" cy="117.2" r="3" fill="#e08a3c"/>
<circle cx="604.3" cy="95.4" r="3" fill="#e08a3c"/>
<circle cx="641.4" cy="87.7" r="3" fill="#e08a3c"/>
<polyline points="418.6,246.0 455.7,208.3 492.9,181.8 530.0,158.3 567.1,140.1 604.3,118.3 641.4,109.8" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="418.6" cy="246.0" r="3" fill="#4a7bd0"/>
<circle cx="455.7" cy="208.3" r="3" fill="#4a7bd0"/>
<circle cx="492.9" cy="181.8" r="3" fill="#4a7bd0"/>
<circle cx="530.0" cy="158.3" r="3" fill="#4a7bd0"/>
<circle cx="567.1" cy="140.1" r="3" fill="#4a7bd0"/>
<circle cx="604.3" cy="118.3" r="3" fill="#4a7bd0"/>
<circle cx="641.4" cy="109.8" r="3" fill="#4a7bd0"/>
<text x="418.6" y="286" font-size="12" text-anchor="middle">1</text>
<text x="455.7" y="286" font-size="12" text-anchor="middle">2</text>
<text x="492.9" y="286" font-size="12" text-anchor="middle">4</text>
<text x="530.0" y="286" font-size="12" text-anchor="middle">8</text>
<text x="567.1" y="286" font-size="12" text-anchor="middle">16</text>
<text x="604.3" y="286" font-size="12" text-anchor="middle">32</text>
<text x="641.4" y="286" font-size="12" text-anchor="middle">64</text>
<text x="641.4" y="79.7" font-size="12" text-anchor="end">T가 자름 2012</text>
<text x="530" y="304" font-size="12" text-anchor="middle">N</text>
<rect x="420" y="74" width="12" height="10" fill="#e08a3c"/><text x="438" y="83" font-size="12">최대</text>
<rect x="480" y="74" width="12" height="10" fill="#4a7bd0"/><text x="498" y="83" font-size="12">평균</text>
</svg>
```

그림 4 — 예제 5 실측. 왼쪽: N을 2배로 늘릴 때마다 깨움이 반으로 줄고 CPU가 44% → 27% → 18% → 14% → 11%로 내려가다 10%(깨움과 무관한 바닥)에 붙는다. 오른쪽: 대신 최대 지연은 (N−1) × 50 µs로 늘고, N=64에서는 T = 2 ms가 상한을 자른다.

출력에서 볼 것:

- **N=1(사건마다 깨우기)은 초당 깨움 2만 번, context switch 4만 번, CPU 44%.** 일 자체(항목당 2 µs × 2만 = 4%)의 열 배다. 나머지는 ISR · notify · 전환 두 번이다. QEMU icount에서 전환 비용이 명령 수로만 매겨진다는 점을 감안해도, 고속 원천에 사건마다 깨우기는 "일보다 깨움이 비싼" 설계가 된다.
- **N=2만 해도 CPU 27%**, N=8이면 14%. 곡선이 처음에 가장 가파르다 — 작은 묶음이 거의 모든 이득을 가져간다.
- **최대 지연은 정확히 (N−1) × 50 + 11 µs**: 161(N=4), 361(N=8), 761(N=16), 1561(N=32). 묶음의 첫 항목이 나머지 N−1개를 기다린 시간이다. 평균은 그 절반쯤.
- **N=64는 64 × 50 µs = 3.2 ms가 T = 2 ms보다 길어서 T가 먼저 터졌다**: 초당 깨움 485번(묶음 ≈ 41개), 최대 2012 µs. T가 지연의 상한 역할을 한다.
- **느린 원천(1 kHz)에서 T가 없으면** 16개가 찰 때까지 최대 15 ms를 기다린다(lat max 15011 µs). T = 2 ms를 넣으면 최대 2012 µs로 묶인다. **T 없는 N은 트래픽이 줄면 지연이 무한정 늘어나는 함정**이다 — 마지막 몇 바이트가 다음 패킷이 올 때까지 갇히는 UART 드라이버 버그가 바로 이것이다.

### 2.4 예제 6 — 깨움 비용 모델을 맞추고 예측해 보기

무엇을 확인하는 코드인지: 예제 5의 N=1과 N=64 두 점으로 "깨움 1번의 CPU 시간"과 "깨움과 무관한 CPU"를 구하고, 나머지 N의 CPU와 최대 지연을 예측해 실측과 비교한다(VM에서 Python으로 실행).

```python
# ex5 출력으로 "깨움 1번 비용" 모델을 맞추고, 나머지 N을 예측해 실측과 비교한다
import re, sys
rows = []
for line in open(sys.argv[1]):
    m = re.match(r"P=50 N=(\d+) T=\d+: ev=\d+ wakes/s=(\d+) sw/s=\d+ cpu=(\d+)% lat avg=(\d+) max=(\d+)", line)
    if m: rows.append(tuple(int(x) for x in m.groups()))
(n1, w1, c1, _, _), (n64, w64, c64, _, _) = rows[0], rows[-1]
wake_us = (c1 - c64) / 100 / (w1 - w64) * 1e6          # 깨움 1번이 먹는 CPU 시간
base = c64 - w64 * wake_us / 1e4                        # 깨움과 무관한 부분 (ISR + 처리)
print(f"fit: cost per wake = {wake_us:.1f} us, wake-independent CPU = {base:.1f} %")
print(" N  wakes/s  cpu meas/pred   lat_max meas/pred(us)")
for n, w, c, la, lm in rows:
    pred_c = base + w * wake_us / 1e4
    pred_max = min((n - 1) * 50, 2000) + 11             # 마지막 항목은 (N-1)개 간격을 기다리되 T=2 ms에서 잘린다
    print(f"{n:2d} {w:8d}  {c:4d} / {pred_c:5.1f}   {lm:6d} / {pred_max:5d}")
```

```sh
limactl shell elinux -- bash -lc 'python3 /Users/donh/.rtos_scratch/E4/py/model.py /Users/donh/.rtos_scratch/E4/out/ex5.txt'
```

```text
fit: cost per wake = 17.4 us, wake-independent CPU = 9.2 %
 N  wakes/s  cpu meas/pred   lat_max meas/pred(us)
 1    19995    44 /  44.0       11 /    11
 2     9995    27 /  26.6       61 /    61
 4     4995    18 /  17.9      161 /   161
 8     2495    14 /  13.5      361 /   361
16     1245    11 /  11.3      761 /   761
32      620    10 /  10.2     1561 /  1561
64      485    10 /  10.0     2012 /  2011
```

출력에서 볼 것:

- **깨움 1번 = 17.4 µs**(QEMU icount 기준: notify + 전환 두 번 + Take). 바닥 9.2%는 ISR 2만 번 + 처리 2 µs × 2만 번 + bg 측정 오차다.
- 모델 `CPU(N) = 바닥 + (초당 깨움) × 17.4 µs`가 모든 N에서 1%p 안으로 맞는다. 최대 지연 모델 `min((N−1) × P, T) + 11 µs`도 정확히 맞는다.
- 그래서 N은 **계산으로 고른다**: 지연 예산 L과 사건 간격 P가 있으면 `N ≤ (L − c) / P + 1`. 예를 들어 CAN 프레임을 20 kHz로 받는데 처리 지연 예산이 400 µs라면 N ≤ (400 − 11) / 50 + 1 = 8.8 → N = 8, 그때 CPU는 14%(사건마다 깨우기 44%의 3분의 1). 그리고 T는 "트래픽이 끊겼을 때 허용하는 최대 지연"으로 따로 정한다.

실칩에서의 숫자 감각: Cortex-M4 168 MHz에서 FreeRTOS 깨움 한 번은 대략 수 µs 규모이고(전환 자체는 수백 cycle), M7 480 MHz에서는 1 µs 안팎까지 내려간다. 이 노트에서 그 값을 직접 재지는 않았다(QEMU는 cycle을 모델링하지 않는다). 측정 방법은 E3(GPIO 토글 + 스코프, DWT CYCCNT)에 있다. 비율은 같다 — 사건 간격이 깨움 비용의 10배보다 짧아지기 시작하면 묶음을 고려한다.

### 2.5 이 절의 함정

- T 없는 N — 트래픽이 줄면 마지막 항목이 영원히 기다린다(예제 5의 15 ms).
- "깨어날 때마다 하나 처리" — notification이 개수를 세는데 하나만 처리하면 링에 남은 것은 다음 사건이 올 때까지 갇힌다. 항상 비울 때까지.
- 묶음 크기를 링 크기에 너무 가깝게 — 태스크가 늦게 깨면 링이 넘친다. N은 링 크기의 절반 이하로.
- T를 tick(1 ms)으로 구현 — T의 해상도가 tick에 묶인다. µs 단위 T가 필요하면 예제 5처럼 HW one-shot 타이머나 UART idle-line 인터럽트를 쓴다.
- 지연이 중요한 원천(IMU → rate loop)을 묶기 — 제어 루프는 묶지 않는다. 묶음은 처리량이 중요한 데이터(로그, 텔레메트리, CAN 진단, 오디오 블록)에 쓴다.

---

## 3. 받는 쪽 — 전용 고우선순위 핸들러 vs 공용 worker

D7이 수단별 비용을 쟀다: ISR → 전용 태스크 8.1 µs, 핸들러 우선순위가 바쁜 앱과 **같으면** 평균 757 µs(같은 우선순위에서는 즉시 전환하지 않는다 — `vTaskGenericNotifyGiveFromISR`의 `>` 비교), timer daemon에 느린 콜백 하나가 끼면 다른 위임이 최대 1943 µs, Zephyr system workqueue에서 한 항목이 20 ms sleep하면 다른 항목이 20.2 ms 지연. 그 숫자에서 나오는 결정 규칙만 정리한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">위임된 일을 누구에게 — 받는 쪽 고르기</text>
<rect x="200" y="35" width="280" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="60" font-size="12" text-anchor="middle">ISR이 넘긴 일의 마감은 몇 ms인가?</text>
<line x1="270" y1="75" x2="150" y2="110" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="410" y1="75" x2="530" y2="110" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="175" y="95" font-size="12" text-anchor="middle">짧다 (≤ 수 ms)</text>
<text x="510" y="95" font-size="12" text-anchor="middle">길다 / 없다</text>
<rect x="20" y="112" width="260" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150" y="137" font-size="12" text-anchor="middle">방해할 수 있는 태스크보다 높게?</text>
<rect x="400" y="112" width="260" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="530" y="137" font-size="12" text-anchor="middle">공용 worker로 충분하다</text>
<line x1="150" y1="152" x2="150" y2="185" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="187" width="260" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150" y="208" font-size="12" text-anchor="middle">전용 핸들러 태스크</text>
<text x="150" y="226" font-size="12" text-anchor="middle">우선순위 = 마감 순서 (RM식)</text>
<text x="150" y="241" font-size="12" text-anchor="middle">D7 §2: 8.1 µs, 같으면 수백 µs</text>
<line x1="530" y1="152" x2="530" y2="185" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="400" y="187" width="260" height="58" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="530" y="208" font-size="12" text-anchor="middle">timer daemon / k_work / worker</text>
<text x="530" y="226" font-size="12" text-anchor="middle">줄 서기(head-of-line) 주의</text>
<text x="530" y="241" font-size="12" text-anchor="middle">D7 §3: 느린 콜백 → 1943 µs</text>
<rect x="20" y="265" width="640" height="50" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-dasharray="4,3"/>
<text x="340" y="286" font-size="12" text-anchor="middle">한 ISR의 후속 일이 둘이면 쪼갠다: 급한 부분은 전용 태스크,</text>
<text x="340" y="304" font-size="12" text-anchor="middle">나머지(로그 · 통계 · 보고)는 공용 worker — "ISR 하나 = 태스크 하나"가 규칙은 아니다</text>
</svg>
```

그림 5 — 위임된 일을 누구에게 줄지 고르는 흐름. 마감이 짧은 일은 방해할 수 있는 모든 태스크보다 엄격히 높은 전용 태스크로, 마감이 긴 일은 공용 worker로. 한 ISR의 후속 일을 둘로 쪼개는 것도 정상이다.

| 기준 | 전용 핸들러 태스크 | 공용 worker (timer daemon, `k_work`, 직접 만든 worker) |
|---|---|---|
| 지연 | 우선순위만 맞으면 µs 단위, 예측 가능 | 앞에 줄 선 항목의 길이에 달림 (head-of-line blocking) |
| RAM | 태스크 하나당 스택 + TCB (FreeRTOS 기본 스택 1 KB 안팎) | 공유 — 항목당 수십 바이트 |
| 우선순위 표현 | 일마다 따로 | worker 하나의 우선순위를 모두가 공유 |
| 맞는 일 | 마감 있는 센서 · 제어 · 통신 프레임 처리 | 로그, 통계, LED, 느린 센서, fault 보고, 재시도 |
| 함정 | 같은 우선순위로 두면 1 tick 단위 지연 | 한 항목이 블록 · 긴 계산이면 전부 밀린다 |

우선순위는 **IRQ 우선순위가 아니라 위임된 일의 마감**으로 정한다. 마감이 짧을수록 높게 — rate-monotonic의 직관과 같다(N 모듈에서 이론). 예를 들어 드론 FC라면:

| 원천 | ISR에서 | 넘기는 단위 | 받는 쪽 |
|---|---|---|---|
| IMU data-ready (1~8 kHz) | 시각 + FIFO 읽기 DMA 시작 | 사건마다 | 전용, 가장 높은 태스크 (rate loop) |
| DShot 텔레메트리 / ESC | DMA 완료 ack | 사건마다 | 전용, 높음 |
| GNSS UART (115200~921600 baud) | idle-line 또는 FIFO 임계값 | N 바이트 또는 idle(T) | 전용, 중간 |
| 바로미터 (50 Hz) | 변환 완료 ack | 사건마다 | 공용 worker |
| 블랙박스 로그 · MAVLink 통계 | — | 묶음 | 가장 낮은 worker / 게이트키퍼 |
| 모터 과전류 | **PWM 차단은 ISR에서** | 보고만 위임 | 공용 worker (D7 §8.2) |

그리고 한 가지를 더 지킨다: 공용 worker에 넣는 일은 **블록하지 않고 짧아야** 한다. 그렇지 않으면 그 worker를 쓰는 모든 위임이 같이 늦어진다(D7 §3.2, §4.7의 측정).
---

## 4. 인터럽트 폭주(storm) — 원인과 방어

### 4.1 정의와 원인

**인터럽트 폭주(interrupt storm)**는 인터럽트가 설계 빈도보다 훨씬 자주(극단적으로는 ISR 복귀 즉시 계속) 들어와 CPU를 다른 일에 내주지 않는 상태다. 정상 트래픽이 과한 경우(홍수)와 장치 · 소프트웨어 고장인 경우가 섞여 있다.

| 원인 | 장면 | 증상 | 1차 방어 |
|---|---|---|---|
| 지우지 않은 / 안 지워지는 level IRQ | 오류 플래그(ORE) 미처리, ack 순서 버그, 장치 고장으로 INT 핀이 붙음 | 복귀 즉시 재진입, 시스템 정지 (예제 1 (C)) | ISR 안 감시 카운터 → 끄기 → 복구 태스크 (§4.3) |
| 튀는 GPIO (bounce) | 기계식 버튼 · 리밋 스위치 · 릴레이 접점 | 한 번 누름에 edge 수십 개 (예제 9) | 마스크 + 태스크 debounce, HW 글리치 필터 |
| 떠 있는 입력 핀 | 풀업 · 풀다운 없는 EXTI, 케이블 빠짐 | 노이즈마다 IRQ, 환경(손 대면)에 따라 변함 | 풀업 설정, 감시 카운터 |
| 트래픽 홍수 | UART 1 Mbaud 연속 수신, CAN babbling node, 이더넷 브로드캐스트 | 처리량 폭락 + 다른 태스크 기아 (livelock, 예제 10) | 묶음(§2), NAPI식 폴링 전환 (§4.6), 하드웨어 필터 |
| 잘못된 트리거 설정 | edge여야 할 것을 level로, 극성 반대 | 조건이 유지되는 내내 재진입 | 설정 검토 — 감시 카운터가 첫 증거를 준다 |
| 공유 라인에서 다른 장치 | 한 IRQ에 장치 여럿, 그중 하나가 안 지움 | "내 ISR은 원인이 없다고 하는데 계속 들어온다" | 모든 장치 상태 확인, EL F6 §8의 Linux 방식 참고 |

### 4.2 왜 RTOS 전체가 멈추나 — 시간까지

예제 1 (C)를 다시 보자. 121.8 ms 동안 tick이 0개 늘었다. 이유는 셋이다.

1. **ISR은 모든 태스크보다 높다.** 가장 높은 우선순위 태스크도 가장 낮은 IRQ보다 낮다. 폭주 중에는 어떤 태스크도 돌지 못한다 — 태스크 우선순위로는 막을 수 없다.
2. **SysTick과 PendSV는 보통 가장 낮은 우선순위다**(FreeRTOS `configKERNEL_INTERRUPT_PRIORITY` = 255). 폭주하는 IRQ가 그보다 높으면 tick 인터럽트가 pending인 채로 남고, pending은 **한 번만** 기억되므로 그사이 놓친 tick들은 사라진다. RTOS 시간(`xTaskGetTickCount`, 타임아웃, 소프트웨어 타이머)이 실제 시간보다 뒤처진다. 예제 7에서 벽시계 128 ms 동안 tick은 100개였다(28개 증발).
3. **watchdog을 차는 태스크도 못 돈다.** 결국 watchdog 리셋이 유일한 출구가 된다(E8). 리셋 원인 기록에 "watchdog"만 남고 폭주의 흔적은 없다 — 그래서 감시 카운터가 fault 로그를 남겨야 한다.

### 4.3 예제 7 — 끼인 level IRQ: 감시 카운터 + 끄기 + 복구 태스크

무엇을 확인하는 코드인지: TIMER0가 200 µs마다 사건을 내는 정상 장치인데, 30~60 ms 구간에서는 **ack가 안 먹는다**(에뮬레이션: ISR이 그 구간에서 `INTCLR`를 쓰지 않는다 — INT 핀이 붙어 버린 센서와 같은 상황). 1 kHz 제어 루프(우선순위 2)가 실행 시각을 기록한다. guard=0은 아무 방어 없이, guard=1은 ISR이 **1 ms 창에 20개 넘게**(정상은 5개) 들어오면 자기 IRQ를 NVIC에서 끄고 fault를 세고 복구 태스크를 깨운다. 복구 태스크는 5 ms 백오프 후 장치를 재초기화(상태 지우기, NVIC pending 지우기)하고 IRQ를 다시 켠다. 8번 실패하면 포기하고 장치를 "degraded"로 둔다.

```c
/* ex7: 끼인 level IRQ(ack가 안 먹는 장치) → 태스크 기아. 감시 카운터 + 끄기 + 복구 태스크로 고친다 */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
volatile uint32_t e4_switches;
#define MS 25000u
static volatile uint32_t t0, guard_on, win_start, win_n, isr_n, faults;
static TaskHandle_t h_sup;
static uint32_t ctl_ts[200]; static volatile uint32_t ctl_n;
static int stuck_now(void) { uint32_t t = CYC() - t0; return t > 30u * MS && t < 60u * MS; }  /* 30~60 ms 고장 */
void TIMER0_IRQHandler(void)
{
    isr_n++;
    if (!stuck_now()) TIMER_INTCLR(TIMER0_BASE) = 1;      /* 고장 구간에서는 ack가 안 먹는다 (에뮬레이션) */
    if (!guard_on) return;
    uint32_t now = CYC();
    if (now - win_start > MS) { win_start = now; win_n = 0; }   /* 1 ms 창 */
    if (++win_n > 20u) {                                  /* 정상은 창당 5개, 예산 20개 */
        irq_off(IRQ_T0); faults++;                        /* 원천을 끄고 */
        BaseType_t w = pdFALSE;
        vTaskNotifyGiveFromISR(h_sup, &w);                /* 복구는 태스크에게 */
        portYIELD_FROM_ISR(w);
    }
}
static void sup_fn(void *p)                               /* 복구 태스크: 백오프 후 장치 재초기화 + 다시 켜기 */
{
    (void)p;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        board_printf_raw("  [fault] storm at %u ms, faults=%u -> IRQ8 off, retry in 5 ms\n", (CYC() - t0) / MS, faults);
        vTaskDelay(pdMS_TO_TICKS(5));
        if (faults >= 8u) { board_printf_raw("  [fault] give up: device degraded\n"); continue; }
        TIMER_INTCLR(TIMER0_BASE) = 1; NVIC_ICPR0 = 1u << IRQ_T0; win_n = 0;
        irq_on(IRQ_T0, 0x40);
    }
}
static void ctl_fn(void *p)                               /* 1 kHz 제어 루프: 실행 시각을 기록 */
{
    (void)p; TickType_t t = xTaskGetTickCount();
    for (;;) { vTaskDelayUntil(&t, 1); if (ctl_n < 200u) ctl_ts[ctl_n++] = CYC(); busy_us(100); }
}
static void run(int g)
{
    guard_on = g; faults = isr_n = ctl_n = 0; win_n = 0;
    vTaskDelay(1);
    TickType_t k0 = xTaskGetTickCount(); t0 = win_start = CYC();
    irq_on(IRQ_T0, 0x40); timer0_start(200u * 25u);
    vTaskDelay(pdMS_TO_TICKS(100)); timer0_stop(); irq_off(IRQ_T0);
    uint32_t wall = (CYC() - t0) / MS, w[12] = { 0 };
    for (uint32_t i = 0; i < ctl_n; i++) { uint32_t b = (ctl_ts[i] - t0) / (10u * MS); if (b < 12u) w[b]++; }
    board_printf_raw("guard=%d: wall=%u ms ticks=%u isr=%u ctl/10ms:", g, wall, xTaskGetTickCount() - k0, isr_n);
    for (int i = 0; i < 12; i++) board_printf_raw(" %u", w[i]);
    board_printf_raw("\n");
}
static void main_fn(void *p) { (void)p; run(0); run(1); board_exit(0); }
int main(void)
{
    xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    xTaskCreate(sup_fn, "sup", 256, NULL, 3, &h_sup);
    xTaskCreate(ctl_fn, "ctl", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```text
guard=0: wall=128 ms ticks=100 isr=49836 ctl/10ms: 11 10 10 0 0 0 11 10 10 10 10 10
  [fault] storm at 30 ms, faults=1 -> IRQ8 off, retry in 5 ms
  [fault] storm at 35 ms, faults=2 -> IRQ8 off, retry in 5 ms
  [fault] storm at 40 ms, faults=3 -> IRQ8 off, retry in 5 ms
  [fault] storm at 45 ms, faults=4 -> IRQ8 off, retry in 5 ms
  [fault] storm at 50 ms, faults=5 -> IRQ8 off, retry in 5 ms
  [fault] storm at 55 ms, faults=6 -> IRQ8 off, retry in 5 ms
guard=1: wall=99 ms ticks=100 isr=474 ctl/10ms: 11 10 10 9 10 10 11 10 10 9 0 0
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">예제 7 실측 — 1 kHz 제어 루프가 10 ms마다 몇 번 돌았나 (30~60 ms에 IRQ 끼임)</text>
<rect x="240.0" y="60" width="180.0" height="180" fill="#d0564a" fill-opacity="0.12" stroke="none"/>
<text x="330.0" y="76" font-size="12" text-anchor="middle">장치 ack 고장 구간</text>
<line x1="60" y1="240" x2="660" y2="240" stroke="currentColor"/>
<line x1="60" y1="240" x2="60" y2="60" stroke="currentColor"/>
<text x="53" y="244.0" font-size="12" text-anchor="end">0</text>
<line x1="56" y1="240.0" x2="60" y2="240.0" stroke="currentColor"/>
<text x="53" y="175.8" font-size="12" text-anchor="end">5</text>
<line x1="56" y1="171.8" x2="60" y2="171.8" stroke="currentColor"/>
<text x="53" y="107.6" font-size="12" text-anchor="end">10</text>
<line x1="56" y1="103.6" x2="60" y2="103.6" stroke="currentColor"/>
<rect x="70.0" y="90.0" width="20" height="150.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="94.0" y="90.0" width="20" height="150.0" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="90.0" y="256" font-size="12" text-anchor="middle">0~10</text>
<rect x="130.0" y="103.6" width="20" height="136.4" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="154.0" y="103.6" width="20" height="136.4" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="150.0" y="256" font-size="12" text-anchor="middle">10~20</text>
<rect x="190.0" y="103.6" width="20" height="136.4" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="214.0" y="103.6" width="20" height="136.4" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="210.0" y="256" font-size="12" text-anchor="middle">20~30</text>
<rect x="250.0" y="240.0" width="20" height="0.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="260.0" y="235" font-size="12" text-anchor="middle">0</text>
<rect x="274.0" y="117.3" width="20" height="122.7" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="270.0" y="256" font-size="12" text-anchor="middle">30~40</text>
<rect x="310.0" y="240.0" width="20" height="0.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="320.0" y="235" font-size="12" text-anchor="middle">0</text>
<rect x="334.0" y="103.6" width="20" height="136.4" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="330.0" y="256" font-size="12" text-anchor="middle">40~50</text>
<rect x="370.0" y="240.0" width="20" height="0.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="380.0" y="235" font-size="12" text-anchor="middle">0</text>
<rect x="394.0" y="103.6" width="20" height="136.4" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="390.0" y="256" font-size="12" text-anchor="middle">50~60</text>
<rect x="430.0" y="90.0" width="20" height="150.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="454.0" y="90.0" width="20" height="150.0" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="450.0" y="256" font-size="12" text-anchor="middle">60~70</text>
<rect x="490.0" y="103.6" width="20" height="136.4" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="514.0" y="103.6" width="20" height="136.4" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="510.0" y="256" font-size="12" text-anchor="middle">70~80</text>
<rect x="550.0" y="103.6" width="20" height="136.4" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="574.0" y="103.6" width="20" height="136.4" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="570.0" y="256" font-size="12" text-anchor="middle">80~90</text>
<rect x="610.0" y="103.6" width="20" height="136.4" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="634.0" y="117.3" width="20" height="122.7" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="630.0" y="256" font-size="12" text-anchor="middle">90~100</text>
<text x="360" y="274" font-size="12" text-anchor="middle">벽시계 시간 (ms)</text>
<rect x="90" y="270" width="12" height="10" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/><text x="108" y="279" font-size="12">감시 없음: 30 ms 정지, tick 28개 사라짐</text>
<rect x="400" y="270" width="12" height="10" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/><text x="418" y="279" font-size="12">감시 + 끄기 + 복구: 거의 그대로</text>
</svg>
```

그림 6 — 예제 7 실측. 감시가 없으면(빨강) 고장 구간 30 ms 동안 제어 루프가 한 번도 돌지 못했다. 감시 + 끄기 + 복구(초록)는 같은 고장에서 제어 루프가 10 ms당 9~11번을 유지했다.

출력에서 볼 것:

- **guard=0: 벽시계 128 ms인데 tick 100개**, ISR 49836번. 30~60 ms의 세 창에서 제어 루프 0번. 장치가 저절로 나은 60 ms 이후에야 돌아왔다. 장치가 영원히 고장 났다면 watchdog 리셋까지 갔을 것이다.
- **guard=1: 6번의 폭주를 감지**(30, 35, …, 55 ms — 5 ms 백오프 간격 그대로), ISR은 474번뿐. 고장 구간에서도 제어 루프가 10 ms당 9~10번 돌았다. 각 폭주는 창 예산(20개)을 넘는 순간 끊겨서 수십 µs 안에 끝났다. 60 ms 이후 장치가 낫자 6번째 재시도가 성공해 정상으로 돌아왔다. 마지막 두 창의 0은 측정 창(99 ms)이 끝난 뒤라 비어 있는 것이다.
- **대가**: IRQ를 끈 5 ms 동안의 정상 사건(약 25개 × 6번)은 잃었다. 감시는 "장치 하나를 잠깐 희생하고 시스템을 살린다"는 선택이다 — Linux가 `note_interrupt`로 라인을 꺼 버리는 것(EL F6 §8)과 같은 철학이고, 차이는 MCU에서는 **복구 · 재시도 · 포기 정책까지 드라이버가 정한다**는 것이다.

감시 카운터의 설계 포인트:

- **창과 예산**은 정상 최대 빈도의 몇 배로 잡는다(여기서는 4배). 너무 빡빡하면 정상 버스트에서 오탐, 너무 느슨하면 감지 전 CPU를 많이 잃는다. 시간 소스는 tick이 아니라 **자유 실행 하드웨어 카운터**(DWT CYCCNT, 타이머)다 — 폭주 중에는 tick이 멈추기 때문이다.
- **끄는 대상**은 가능한 한 좁게: 장치 안의 인터럽트 enable 비트 → 그 장치의 NVIC 라인 → (최후) 공유 라인. 끄는 것은 ISR 안에서, 다시 켜는 것은 태스크에서.
- **복구는 백오프 + 횟수 제한**: 재초기화 → 다시 켜기 → 또 폭주하면 백오프를 늘리고, N번이면 포기하고 fault로 올린다(안전 상태, 텔레메트리 보고).
- **흔적을 남긴다**: fault 카운터, 마지막 폭주 시각, 그 순간의 상태 레지스터 값을 RAM의 no-init 영역이나 로그에 남긴다(E8의 리셋 원인 기록과 같은 자리).

### 4.4 예제 8 — Zephyr로 같은 패턴: `irq_disable` + `k_work_schedule`

무엇을 확인하는 코드인지: 같은 고장 장치를 Zephyr v4.4.2로. ISR은 `IRQ_CONNECT(8, 2, …)`로 등록하고, 예산을 넘으면 `irq_disable(8)`(Cortex-M에서는 `arm_irq_disable` → `NVIC_DisableIRQ`)로 끄고 `k_work_schedule(&recover, K_MSEC(5))`로 5 ms 뒤 복구를 예약한다. `k_work_schedule_for_queue`는 v4.4.2 `kernel.h` 문서에 `@isr_ok`로 표시되어 있다. 복구 핸들러는 system workqueue 스레드에서 돈다. 메인 스레드가 1 kHz로 돌며 실행 시각을 기록한다.

```c
/* z1: Zephyr — 끼인 level IRQ 감시: ISR이 irq_disable + k_work_schedule(백오프), work가 복구 + irq_enable */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#define T0_CTRL   (*(volatile uint32_t *)0x40000000u)
#define T0_VALUE  (*(volatile uint32_t *)0x40000004u)
#define T0_RELOAD (*(volatile uint32_t *)0x40000008u)
#define T0_CLR    (*(volatile uint32_t *)0x4000000Cu)
#define CYC()     (*(volatile uint32_t *)0x40028018u)   /* FPGAIO 25 MHz */
#define MS 25000u
static volatile uint32_t t0, win_start, win_n, isr_n, faults;
static uint32_t loop_ts[120]; static volatile uint32_t loop_n;
static struct k_work_delayable recover;
static int stuck_now(void) { uint32_t t = CYC() - t0; return t > 30u * MS && t < 60u * MS; }
static void t0_isr(const void *arg)
{
	ARG_UNUSED(arg);
	isr_n++;
	if (!stuck_now()) T0_CLR = 1;                       /* 30~60 ms: ack가 안 먹는 장치 (에뮬레이션) */
	uint32_t now = CYC();
	if (now - win_start > MS) { win_start = now; win_n = 0; }
	if (++win_n > 20u) {                                /* 1 ms 창에 20개 초과 = 폭주 */
		irq_disable(8); faults++;
		k_work_schedule(&recover, K_MSEC(5));          /* isr-ok: 5 ms 뒤 workqueue에서 복구 */
	}
}
static void recover_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	printk("  [fault] storm #%u at %u ms -> re-init, irq_enable(8)\n", faults, (CYC() - t0) / MS);
	T0_CLR = 1; NVIC_ClearPendingIRQ(8); win_n = 0;
	irq_enable(8);
}
int main(void)
{
	k_work_init_delayable(&recover, recover_fn);
	IRQ_CONNECT(8, 2, t0_isr, NULL, 0);
	t0 = win_start = CYC();
	T0_RELOAD = 200u * 25u - 1u; T0_VALUE = T0_RELOAD; T0_CTRL = 0x9;   /* 200 us 사건 */
	irq_enable(8);
	int64_t next = k_uptime_ticks();
	while (CYC() - t0 < 100u * MS) {                   /* 1 kHz 주기 루프 */
		next += 1; k_sleep(K_TIMEOUT_ABS_TICKS(next));
		if (loop_n < 120u) loop_ts[loop_n++] = CYC();
	}
	T0_CTRL = 0; irq_disable(8);
	uint32_t w[10] = { 0 };
	for (uint32_t i = 0; i < loop_n; i++) { uint32_t b = (loop_ts[i] - t0) / (10u * MS); if (b < 10u) w[b]++; }
	printk("isr=%u faults=%u loop/10ms:", isr_n, faults);
	for (int i = 0; i < 10; i++) printk(" %u", w[i]);
	printk("\n");
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/E4/zb.sh z1 6'
```

```text
app warnings: 0
           FLASH:       15076 B         4 MB      0.36%
             RAM:        7856 B         4 MB      0.19%
*** Booting Zephyr OS build v4.4.2 ***
  [fault] storm #1 at 35 ms -> re-init, irq_enable(8)
  [fault] storm #2 at 41 ms -> re-init, irq_enable(8)
  [fault] storm #3 at 48 ms -> re-init, irq_enable(8)
  [fault] storm #4 at 54 ms -> re-init, irq_enable(8)
  [fault] storm #5 at 60 ms -> re-init, irq_enable(8)
isr=453 faults=5 loop/10ms: 10 10 10 10 9 10 10 10 10 10
```

출력에서 볼 것:

- FreeRTOS 예제 7과 같은 결과: 폭주 5번을 감지하고, 메인 루프는 10 ms당 9~10번을 유지했다. 재시도 간격이 약 6.3 ms인 것은 5 ms 지연이 tick(1 ms) 경계로 올림되고, 복구 work가 system workqueue에서 차례를 기다리기 때문이다.
- `k_work_schedule`은 **이미 예약된 항목이면 아무것도 안 한다**(문서: "a no-op if the work item is already scheduled or submitted"). 그래서 감지가 여러 번 겹쳐도 복구는 한 번만 예약된다 — 여기서는 원하는 동작이다. 매번 뒤로 미루고 싶으면 `k_work_reschedule`(§4.5의 gpio-keys가 그렇게 한다).
- `main()`이 리턴하면 Zephyr는 idle로 남으므로 QEMU는 `timeout`이 끝낸다(QEMU 종료 메시지는 생략했다).

### 4.5 예제 9 — 튀는 버튼: 매 edge vs ISR 시간 잠금 vs 마스크 + 태스크 debounce

무엇을 확인하는 코드인지: DUALTIMER ISR이 50 µs마다 기계식 접점을 흉내 낸다(에뮬레이션): 20 ms 주기로 10 ms 누름 · 10 ms 뗌, 상태가 바뀐 뒤 1.5 ms 동안은 무작위로 튄다. 레벨이 바뀔 때마다 EXTI 역할의 IRQ9를 올린다(양쪽 edge). 세 가지 처리법으로 100 ms(진짜 누름 5번) 동안 ISR 횟수와 인식된 누름 수를 센다.

```c
/* ex9: 튀는(bouncing) 버튼 GPIO — 매 edge 처리 / ISR 안 시간 잠금 / IRQ 끄고 태스크에서 debounce */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
volatile uint32_t e4_switches;
#define MS 25000u
static volatile uint32_t pin, edges, t0, rnd = 7u;
void DUALTIMER_IRQHandler(void)          /* "하드웨어" 접점 모델: 20 ms마다 누름/뗌, 처음 1.5 ms는 튄다 */
{
    DT_INTCLR = 1;
    uint32_t t = (CYC() - t0) % (20u * MS), want = t < 10u * MS;   /* 10 ms 누름, 10 ms 뗌 */
    uint32_t in_bounce = (t % (10u * MS)) < 1500u * 25u;
    rnd = rnd * 1103515245u + 12345u;
    uint32_t lvl = in_bounce ? ((rnd >> 16) & 1u) : want;
    if (lvl != pin) { pin = lvl; edges++; NVIC_ISPR0 = 1u << IRQ_SW; }   /* edge → EXTI IRQ */
}
static volatile int mode;
static volatile uint32_t isr_n, press, last_acc, stable;
static TaskHandle_t h_db;
void TIMER1_IRQHandler(void)             /* EXTI: 양쪽 edge */
{
    isr_n++;
    if (mode == 0) { if (pin) press++; return; }                      /* (A) edge마다 */
    if (mode == 1) {                                                   /* (B) ISR 안 5 ms 잠금 */
        uint32_t now = CYC();
        if (now - last_acc > 5u * MS) { last_acc = now; if (pin) press++; }
        return;
    }
    irq_off(IRQ_SW);                                                   /* (C) 라인을 막고 태스크로 */
    BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(h_db, &w); portYIELD_FROM_ISR(w);
}
static void db_fn(void *p)
{
    for ((void)p;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(5));                                  /* 튐이 끝나기를 기다린다 */
        uint32_t lvl = pin;
        if (lvl != stable) { stable = lvl; if (lvl) press++; }         /* 안정된 레벨 변화만 사건 */
        NVIC_ICPR0 = 1u << IRQ_SW; irq_on(IRQ_SW, 0x40);              /* 쌓인 pending 버리고 다시 켬 */
    }
}
static void main_fn(void *p)
{
    (void)p; irq_on(IRQ_DT, 0x00); irq_on(IRQ_SW, 0x40);
    static const char *nm[3] = { "(A) every edge   ", "(B) ISR lockout  ", "(C) mask+task 5ms" };
    for (int m = 0; m < 3; m++) {
        mode = m; pin = stable = 0; edges = isr_n = press = 0; rnd = 7u; last_acc = CYC() - 10u * MS;
        irq_on(IRQ_SW, 0x40);
        t0 = CYC(); DT_LOAD = 50u * 25u; DT_CTRL = 0xE2;               /* 50 us마다 접점 샘플 */
        vTaskDelay(pdMS_TO_TICKS(100)); DT_CTRL = 0; vTaskDelay(pdMS_TO_TICKS(10));
        board_printf_raw("%s edges=%u isr=%u presses=%u (truth 5)\n", nm[m], edges, isr_n, press);
    }
    board_exit(0);
}
int main(void)
{   xTaskCreate(db_fn, "db", 256, NULL, 3, &h_db); xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler(); return 0; }
```

```text
(A) every edge    edges=158 isr=158 presses=79 (truth 5)
(B) ISR lockout   edges=158 isr=158 presses=5 (truth 5)
(C) mask+task 5ms edges=158 isr=10 presses=5 (truth 5)
[board_exit] code=0
```

출력에서 볼 것:

- **(A) edge마다 처리: edge 158개, 누름 79번으로 인식**(진짜 5번). 튐 하나하나가 "눌림"이 된다. 드론 FC의 arm 스위치나 로봇의 리밋 스위치였다면 끔찍한 결과다.
- **(B) ISR 안 5 ms 시간 잠금: 정답 5번, 그러나 ISR은 여전히 158번.** 첫 edge 하나만 받고 5 ms 동안 무시한다. 결과는 맞지만 폭주 비용(ISR 진입)은 그대로이고, 노이즈 스파이크 하나에도 반응한다(안정성 확인이 없다).
- **(C) 첫 edge에서 라인을 마스크하고 태스크가 5 ms 뒤 레벨을 읽기: 정답 5번, ISR 10번.** 누름과 뗌마다 ISR 1번(5 + 5). 태스크는 튐이 끝난 뒤의 **안정된 레벨 변화**만 사건으로 만들고, 쌓인 pending을 지운 다음 라인을 다시 연다. 폭주 방어와 debounce가 한 번에 된다. 이것이 BM J1의 "이벤트를 기록하고 메인에서 처리"를 RTOS로 쓴 모양이다.
- Zephyr의 `input` 서브시스템 `gpio-keys` 드라이버는 다른 방식을 쓴다: 매 edge의 GPIO 콜백에서 `k_work_reschedule(&pin_data->work, K_MSEC(cfg->debounce_interval_ms))`로 **마감을 계속 뒤로 미루고**, 마지막 edge 뒤 debounce 간격(DT 속성 `debounce-interval-ms`, 기본 30)이 지나면 work가 레벨을 읽는다(§6). 결과는 (C)와 같고, ISR 횟수는 (A)처럼 edge 수만큼이다.
- 하드웨어가 있으면 그게 먼저다: 많은 MCU의 GPIO · 타이머 입력에는 디지털 글리치 필터(샘플링 필터)가 있고, RC 필터 + 슈미트 트리거도 있다.

### 4.6 트래픽 홍수와 NAPI식 모드 전환 — receive livelock

정상 트래픽이 너무 많아도 폭주와 같은 일이 생긴다. Mogul과 Ramakrishnan이 "Eliminating Receive Livelock in an Interrupt-Driven Kernel"(USENIX 1996)에서 정리한 **receive livelock**이다 — 패킷마다 인터럽트를 받고 인터럽트 수준에서 일을 하면, 입력률이 어느 선을 넘는 순간 CPU가 전부 "받기"에 쓰이고 "처리"는 0이 된다. 받은 것을 처리 못 하니 결국 버리는데, 이미 비싼 값을 치르고 받은 뒤에 버린다. Linux NAPI는 그 답이다: 첫 패킷에서 IRQ를 끄고, budget만큼씩 폴링하고, 비면 IRQ를 다시 켠다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">NAPI식 모드 전환 — 인터럽트 모드 ↔ 폴링 모드 (예제 10의 rx 태스크)</text>
<rect x="30" y="60" width="230" height="90" rx="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="145" y="85" font-size="13" text-anchor="middle">인터럽트 모드</text>
<text x="145" y="108" font-size="12" text-anchor="middle">장치 RX IRQ 켜짐</text>
<text x="145" y="128" font-size="12" text-anchor="middle">태스크는 블록 (CPU 0)</text>
<rect x="420" y="60" width="230" height="90" rx="10" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="535" y="85" font-size="13" text-anchor="middle">폴링 모드</text>
<text x="535" y="108" font-size="12" text-anchor="middle">장치 RX IRQ 꺼짐</text>
<text x="535" y="128" font-size="12" text-anchor="middle">tick당 예산 80개 처리</text>
<line x1="260" y1="80" x2="418" y2="80" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="340" y="70" font-size="12" text-anchor="middle">IRQ 1번: ISR이 끄고 깨움</text>
<line x1="420" y1="132" x2="262" y2="132" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="340" y="150" font-size="12" text-anchor="middle">FIFO 비었다 → 켜고 재확인</text>
<path d="M600,150 C640,200 560,215 530,170" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="560" y="230" font-size="12" text-anchor="middle">예산 소진 → vTaskDelay(1)</text>
<text x="560" y="247" font-size="12" text-anchor="middle">(IRQ는 꺼 둔 채)</text>
<rect x="30" y="200" width="330" height="80" rx="6" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="4,3"/>
<text x="195" y="222" font-size="12" text-anchor="middle">경쟁 창: "비었다" 확인과 IRQ 켜기 사이</text>
<text x="195" y="242" font-size="12" text-anchor="middle">에 도착한 패킷은 IRQ를 못 낸다</text>
<text x="195" y="262" font-size="12" text-anchor="middle">→ 켠 뒤 FIFO를 한 번 더 본다</text>
</svg>
```

그림 7 — NAPI식 모드 전환. 인터럽트 모드에서 IRQ 한 번이 오면 ISR이 장치 RX 인터럽트를 끄고 태스크를 깨운다. 태스크는 예산만큼 처리하고, 남았으면 IRQ를 끈 채로 다음 tick에 이어 하고, 비었으면 IRQ를 켠 뒤 FIFO를 한 번 더 확인한다.

**예제 10 — 패킷마다 IRQ vs NAPI식**

무엇을 확인하는 코드인지: DUALTIMER ISR(0x00)이 장치 역할(에뮬레이션)을 한다: 간격 P마다 64칸 FIFO에 패킷 하나를 넣고(차 있으면 하드웨어가 버리고 hw_drop), RX 인터럽트가 켜져 있으면 장치 IRQ(IRQ9)를 올린다. 패킷 하나를 FIFO에서 RAM으로 꺼내는 데 3 µs, 처리하는 데 4 µs가 든다. **per-IRQ** 모드는 흔한 드라이버처럼 ISR이 FIFO를 RAM 링으로 복사하고 태스크를 깨운다. **NAPI** 모드는 ISR이 장치 RX 인터럽트만 끄고 태스크를 깨우며, 태스크가 tick당 최대 80개를 꺼내 처리한다. 우선순위 3의 1 kHz 제어 루프(150 µs)와 가장 낮은 bg 태스크가 같이 돈다. 측정은 벽시계 200 ms에 하드웨어 모델이 스냅샷을 찍는다(폭주 중에는 tick이 멈추므로 `vTaskDelay`로 잰 200 ms는 믿을 수 없다).

```c
/* ex10: 홍수(flood) — 패킷마다 IRQ + ISR 복사 vs NAPI식 "IRQ 끄고 예산만큼 폴링" (에뮬레이션 장치) */
#include "FreeRTOS.h"
#include "task.h"
#include "e4.h"
#include "ring.h"
volatile uint32_t e4_switches;
#define FIFO 64u
static volatile uint32_t f_in, f_out, hw_drop, sw_drop, dev_ie, isr_n, done, bg, ctl_n, t_end, s_done, s_ctl, s_bg;
static volatile int napi; static ring_t rq; static TaskHandle_t h_rx;
void DUALTIMER_IRQHandler(void)                     /* "하드웨어": 패킷 1개 도착 (0x00, RTOS 호출 없음) */
{
    DT_INTCLR = 1;
    if ((int32_t)(CYC() - t_end) > 0) {             /* 벽시계 200 ms 끝: 원천 정지 + 스냅샷 */
        DT_CTRL = 0; s_done = done; s_ctl = ctl_n; s_bg = bg; return;
    }
    if (f_in - f_out == FIFO) { hw_drop++; return; } /* 장치 FIFO 넘침 = HW가 버림 (비용 0) */
    f_in++;
    if (dev_ie) NVIC_ISPR0 = 1u << IRQ_SW;
}
static void copy_out(void) { busy_us(3); f_out++; }   /* 장치 FIFO → RAM 복사 3 us */
void TIMER1_IRQHandler(void)                        /* 장치 RX ISR (0x40) */
{
    isr_n++;
    if (napi) dev_ie = 0;                           /* NAPI: RX 인터럽트를 끄고 태스크가 폴링 */
    else while (f_in != f_out) { copy_out(); ev_t e = { 0, 0 }; if (!ring_put(&rq, e)) sw_drop++; }
    BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(h_rx, &w); portYIELD_FROM_ISR(w);
}
static void rx_fn(void *p)                          /* 처리 4 us/패킷 */
{
    for ((void)p;;) {
        ev_t e; ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!napi) { while (ring_get(&rq, &e)) { busy_us(4); done++; } continue; }
        for (;;) {
            unsigned budget = 80;                   /* tick당 최대 80개 */
            while (f_in != f_out && budget) { copy_out(); busy_us(4); done++; budget--; }
            if (!budget) { vTaskDelay(1); continue; }   /* 아직 남았다: IRQ는 끈 채로 다음 tick */
            dev_ie = 1;                             /* 비었다: 인터럽트 모드로 복귀 */
            if (f_in == f_out) break;               /* 켜는 사이 도착분 재확인 (경쟁 창) */
            dev_ie = 0;
        }
    }
}
static void ctl_fn(void *p)                         /* 높은 우선순위 1 kHz 루프 (150 us) */
{ (void)p; TickType_t t = xTaskGetTickCount(); for (;;) { vTaskDelayUntil(&t, 1); ctl_n++; busy_us(150); } }
static void bg_fn(void *p) { (void)p; for (;;) bg++; }
static void run(int m, uint32_t per_us, uint32_t base)
{
    napi = m; dev_ie = 1; f_in = f_out = hw_drop = sw_drop = isr_n = done = ctl_n = 0;
    vTaskDelay(1); uint32_t b0 = bg;
    t_end = CYC() + 200u * 25000u; DT_LOAD = per_us * 25u; DT_CTRL = 0xE2;
    vTaskDelay(pdMS_TO_TICKS(200));
    while (DT_CTRL) vTaskDelay(1);
    board_printf_raw("%s P=%u: arrived=%u done=%u hw_drop=%u sw_drop=%u isr/s=%u ctl=%u/200 bg=%u%%\n",
        m ? "NAPI   " : "per-IRQ", per_us, f_in + hw_drop, s_done, hw_drop, sw_drop, isr_n * 5u, s_ctl,
        (uint32_t)((uint64_t)(s_bg - b0) * 100u / base));
    vTaskDelay(pdMS_TO_TICKS(10));
}
static void main_fn(void *p)
{
    (void)p; irq_on(IRQ_DT, 0x00); irq_on(IRQ_SW, 0x40);
    uint32_t base = bg; vTaskDelay(pdMS_TO_TICKS(200)); base = bg - base;
    static const uint32_t P[] = { 20, 8, 4 };
    for (int i = 0; i < 3; i++) { run(0, P[i], base); run(1, P[i], base); }
    board_exit(0);
}
int main(void)
{   xTaskCreate(bg_fn, "bg", 256, NULL, 1, NULL); xTaskCreate(rx_fn, "rx", 256, NULL, 2, &h_rx);
    xTaskCreate(ctl_fn, "ctl", 256, NULL, 3, NULL); xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler(); return 0; }
```

```text
per-IRQ P=20: arrived=9980 done=9979 hw_drop=0 sw_drop=0 isr/s=49900 ctl=202/200 bg=0%
NAPI    P=20: arrived=9980 done=9979 hw_drop=0 sw_drop=0 isr/s=24480 ctl=202/200 bg=1%
per-IRQ P=8: arrived=24875 done=18272 hw_drop=0 sw_drop=6346 isr/s=124375 ctl=201/200 bg=0%
NAPI    P=8: arrived=24875 done=16000 hw_drop=8811 sw_drop=0 isr/s=5 ctl=201/200 bg=19%
per-IRQ P=4: arrived=49504 done=0 hw_drop=0 sw_drop=49248 isr/s=10 ctl=1/200 bg=0%
NAPI    P=4: arrived=49504 done=16000 hw_drop=33440 sw_drop=0 isr/s=5 ctl=201/200 bg=17%
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">예제 10 실측 — 200 ms 동안 처리한 패킷 수 (막대 위: 제어 루프 실행 횟수/200)</text>
<line x1="70" y1="250" x2="660" y2="250" stroke="currentColor"/>
<line x1="70" y1="250" x2="70" y2="50" stroke="currentColor"/>
<line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/>
<text x="63" y="254.0" font-size="12" text-anchor="end">0k</text>
<line x1="66" y1="210.0" x2="70" y2="210.0" stroke="currentColor"/>
<text x="63" y="214.0" font-size="12" text-anchor="end">10k</text>
<line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/>
<text x="63" y="174.0" font-size="12" text-anchor="end">20k</text>
<line x1="66" y1="130.0" x2="70" y2="130.0" stroke="currentColor"/>
<text x="63" y="134.0" font-size="12" text-anchor="end">30k</text>
<line x1="66" y1="90.0" x2="70" y2="90.0" stroke="currentColor"/>
<text x="63" y="94.0" font-size="12" text-anchor="end">40k</text>
<line x1="66" y1="50.0" x2="70" y2="50.0" stroke="currentColor"/>
<text x="63" y="54.0" font-size="12" text-anchor="end">50k</text>
<line x1="100.0" y1="210.1" x2="236.7" y2="210.1" stroke="#888" stroke-dasharray="4,3"/>
<rect x="120.0" y="210.1" width="56" height="39.9" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="148.0" y="205.1" font-size="12" text-anchor="middle">ctl 202</text>
<rect x="190.0" y="210.1" width="56" height="39.9" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="218.0" y="205.1" font-size="12" text-anchor="middle">ctl 202</text>
<text x="168.3" y="266" font-size="12" text-anchor="middle">패킷 간격 20 µs</text>
<text x="168.3" y="282" font-size="12" text-anchor="middle">도착 9980 (점선)</text>
<line x1="296.7" y1="150.5" x2="433.3" y2="150.5" stroke="#888" stroke-dasharray="4,3"/>
<rect x="316.7" y="176.9" width="56" height="73.1" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="344.7" y="171.9" font-size="12" text-anchor="middle">ctl 201</text>
<rect x="386.7" y="186.0" width="56" height="64.0" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="414.7" y="181.0" font-size="12" text-anchor="middle">ctl 201</text>
<text x="365.0" y="266" font-size="12" text-anchor="middle">패킷 간격 8 µs</text>
<text x="365.0" y="282" font-size="12" text-anchor="middle">도착 24875 (점선)</text>
<line x1="493.3" y1="52.0" x2="630.0" y2="52.0" stroke="#888" stroke-dasharray="4,3"/>
<rect x="513.3" y="250.0" width="56" height="0.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="541.3" y="245.0" font-size="12" text-anchor="middle">ctl 1</text>
<rect x="583.3" y="186.0" width="56" height="64.0" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="611.3" y="181.0" font-size="12" text-anchor="middle">ctl 201</text>
<text x="561.7" y="266" font-size="12" text-anchor="middle">패킷 간격 4 µs</text>
<text x="561.7" y="282" font-size="12" text-anchor="middle">도착 49504 (점선)</text>
<rect x="120" y="306" width="12" height="10" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/><text x="138" y="315" font-size="12">패킷마다 IRQ + ISR에서 복사</text>
<rect x="390" y="306" width="12" height="10" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/><text x="408" y="315" font-size="12">NAPI식: IRQ 끄고 tick당 80개</text>
</svg>
```

그림 8 — 예제 10 실측. 패킷 간격이 4 µs가 되면 "패킷마다 IRQ + ISR 복사"는 처리 0개에 제어 루프 1/200 — 완전한 livelock이다. NAPI식은 같은 입력에서 16000개를 처리하고 제어 루프 201/200을 지켰다.

출력에서 볼 것:

- **P=20 µs(초당 5만 패킷)**: 둘 다 전부 처리하지만 bg가 0~1% — CPU가 거의 꽉 찼다. NAPI의 ISR이 절반(24480/s)인 것은 FIFO가 비는 순간마다 인터럽트 모드로 돌아가기 때문이다. 부하가 적을 때 NAPI는 per-IRQ와 비슷하게 동작한다 — 그게 설계 의도다.
- **P=8 µs**: per-IRQ는 초당 IRQ 12만 번, 18272개를 처리하고 6346개를 **복사한 뒤 버렸다**(sw_drop — 이미 3 µs씩 쓴 뒤). bg 0%. NAPI는 IRQ가 초당 5번, tick당 80개 = 16000개를 처리하고 나머지는 **장치 FIFO에서 공짜로** 버렸다(hw_drop). bg 19% — 다른 태스크가 쓸 CPU가 남는다.
- **P=4 µs**: per-IRQ는 ISR이 FIFO를 비우는 동안 다음 패킷이 계속 도착해 **ISR에서 나오지 못했다**: 200 ms 동안 처리 0개, 제어 루프 1번(폭주 시작 전 1번), 링이 찬 뒤 49248개를 복사 후 버림. 이것이 receive livelock이다. NAPI는 입력이 두 배가 되어도 처리 16000개, 제어 루프 201/200, bg 17%로 **같은 자리에 머문다**. 과부하에서 처리량이 무너지지 않고 상한에서 평평하게 유지되는 것이 핵심이다.
- **예산 80개**는 정책이다: 80 × 7 µs = 560 µs/ms → CPU의 56%를 이 장치에 준다. 예산을 키우면 처리량이 늘고 다른 태스크 몫이 준다. Linux NAPI의 기본 weight 64와 같은 역할이다.
- **경쟁 창**(그림 7 아래): "비었다"를 확인한 뒤 IRQ를 켜기 전에 패킷이 오면 그 패킷은 IRQ를 내지 못한다(켜기 전이었으니까). 그래서 켠 뒤에 FIFO를 한 번 더 보고, 있으면 다시 끄고 폴링한다. 이 재확인을 빼먹으면 "트래픽이 잠깐 멈췄다 다시 오면 수신이 멈추는" 버그가 된다. 장치가 level 인터럽트("FIFO 비어 있지 않음")를 낸다면 켜는 순간 IRQ가 바로 오므로 이 창이 저절로 닫힌다 — edge식 · 사건식 장치에서 특히 조심한다.

### 4.7 방어 사다리 — 정리

| 단계 | 하는 일 | 이 노트의 예 |
|---|---|---|
| 예방 | 정확한 ack 순서, 오류 플래그 처리, 풀업 · 글리치 필터, 트리거 설정 검토 | §1.3, §1.4 |
| 감소 | 묶음(N 또는 T), 하드웨어 FIFO 임계값 · idle-line, DMA | §2, E6 |
| 감지 | ISR 안 창별 카운터(하드웨어 시계 기준), 예산 초과 = 폭주 | 예제 7 · 8 |
| 격리 | 장치 IE 비트 → NVIC 라인 끄기 (ISR 안에서) | 예제 7 · 8 · 9 |
| 전환 | 인터럽트 모드 → 폴링 모드, 예산으로 CPU 상한 | 예제 10 |
| 복구 | 태스크에서 백오프 → 재초기화 → 다시 켜기, 횟수 제한 | 예제 7 · 8 |
| 보고 · 강등 | fault 카운터 · 시각 · 상태값 기록, N번 실패 시 degraded / 안전 상태 | 예제 7의 "give up" |

---

## 5. 과부하와 backpressure — 큐가 차면 무엇을 버리나

### 5.1 원칙 — ISR은 기다릴 수 없으니 정책이 미리 있어야 한다

태스크끼리는 큐가 차면 생산자가 블록해서 기다리면 된다(backpressure가 저절로 생긴다). ISR은 블록할 수 없다. 큐가 찬 순간 ISR은 **즉시** 셋 중 하나를 해야 한다: 새것을 버린다(drop-newest), 가장 오래된 것을 버리고 새것을 넣는다(drop-oldest), 최신 하나만 유지한다(latest-only). 어느 경우든 **버린 개수를 센다** — 세지 않은 유실은 디버깅할 수 없다. 그리고 가능하면 **상류에 backpressure를 건다**: UART RTS/CTS 하드웨어 흐름 제어, 송신 측에 credit 돌려주기, DMA 중지. CAN처럼 상류를 멈출 수 없는 버스는 버리는 정책밖에 없다.

### 5.2 예제 11 — drop-newest / drop-oldest / latest-only

무엇을 확인하는 코드인지: TIMER0 ISR이 1 kHz로 샘플(시각 + 순번)을 만든다. 소비 태스크는 50 ms 시점에 20 ms 동안 멈춘다(플래시 erase로 블록된 상황). (A) 8칸 큐에 `xQueueSendFromISR` — 차면 실패하고 새 샘플을 버린다. (B) 차면 `xQueueReceiveFromISR`로 가장 오래된 것을 꺼내 버리고 새것을 넣는다. (C) 길이 1 큐에 `xQueueOverwriteFromISR` — 항상 최신 하나. 각 정책에서 받은 개수, 유실 구간, **멈춤 직후 처리한 샘플의 나이**를 본다.

```c
/* ex11: 큐가 찼을 때 — drop-newest / drop-oldest / latest-only(overwrite). 소비자가 20 ms 멈춘다 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "e4.h"
#include "ring.h"
volatile uint32_t e4_switches;
static QueueHandle_t q8, q1;
static volatile int pol;
static volatile uint32_t seq, ovr;
void TIMER0_IRQHandler(void)                          /* 1 kHz 샘플 생산 */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    ev_t e = { CYC(), seq++ }, junk;
    BaseType_t w = pdFALSE;
    if (pol == 2) xQueueOverwriteFromISR(q1, &e, &w);  /* (C) 길이 1 mailbox: 항상 최신 */
    else if (xQueueSendFromISR(q8, &e, &w) != pdPASS) {
        ovr++;                                         /* 가득 참: 몇 개 잃었는지는 반드시 센다 */
        if (pol == 1) { xQueueReceiveFromISR(q8, &junk, &w); xQueueSendFromISR(q8, &e, &w); }  /* (B) */
    }                                                  /* (A) 그냥 새것을 버린다 */
    portYIELD_FROM_ISR(w);
}
static void main_fn(void *p)
{
    (void)p; irq_on(IRQ_T0, 0x40);
    static const char *nm[3] = { "(A) drop-newest", "(B) drop-oldest", "(C) latest-only" };
    for (int m = 0; m < 3; m++) {
        QueueHandle_t q = (m == 2) ? q1 : q8;
        pol = m; seq = ovr = 0; xQueueReset(q);
        timer0_start(1000u * 25u);
        uint32_t got = 0, prev = 0, gaps = 0, gap_from = 0, gap_to = 0, age_after = 0, age_max = 0;
        TickType_t t_start = xTaskGetTickCount();
        while (xTaskGetTickCount() - t_start < 100u) {
            if (xTaskGetTickCount() - t_start == 50u) vTaskDelay(pdMS_TO_TICKS(20));  /* 플래시 erase로 멈춤 */
            ev_t e;
            if (xQueueReceive(q, &e, pdMS_TO_TICKS(2)) != pdPASS) continue;
            uint32_t age = (CYC() - e.ts) / 25u;
            if (got && e.seq != prev + 1u) { gaps++; gap_from = prev + 1u; gap_to = e.seq - 1u; age_after = age; }
            if (age > age_max) age_max = age;
            prev = e.seq; got++;
        }
        timer0_stop();
        board_printf_raw("%s: made=%u got=%u ovr=%u gaps=%u lost seq %u..%u, age after gap=%u us, max age=%u us\n",
            nm[m], seq, got, ovr, gaps, gap_from, gap_to, age_after, age_max);
    }
    board_exit(0);
}
int main(void)
{
    q8 = xQueueCreate(8, sizeof(ev_t)); q1 = xQueueCreate(1, sizeof(ev_t));
    xTaskCreate(main_fn, "main", 512, NULL, 3, NULL);
    vTaskStartScheduler(); return 0;
}
```

```text
(A) drop-newest: made=100 got=89 ovr=11 gaps=1 lost seq 58..68, age after gap=29 us, max age=18993 us
(B) drop-oldest: made=100 got=89 ovr=11 gaps=1 lost seq 50..60, age after gap=7930 us, max age=7930 us
(C) latest-only: made=100 got=82 ovr=0 gaps=1 lost seq 50..67, age after gap=867 us, max age=867 us
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">큐(8칸)가 찬 동안 무엇이 남나 — 예제 11 실측 (소비자 20 ms 정지, 1 kHz 생산)</text>
<text x="150" y="48" font-size="12" text-anchor="middle">정책</text>
<text x="450" y="48" font-size="12" text-anchor="middle">순번 50~69 중 살아남은 것 (초록), 버려진 것 (빨강)</text>
<text x="20" y="82" font-size="12">(A) drop-newest</text>
<text x="20" y="132" font-size="12">(B) drop-oldest</text>
<text x="20" y="182" font-size="12">(C) latest-only</text>
<rect x="180" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="190.5" y="106" font-size="12" text-anchor="middle">50</text>
<rect x="203" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="226" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="236.5" y="106" font-size="12" text-anchor="middle">52</text>
<rect x="249" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="272" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="282.5" y="106" font-size="12" text-anchor="middle">54</text>
<rect x="295" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="318" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="328.5" y="106" font-size="12" text-anchor="middle">56</text>
<rect x="341" y="66" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="364" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="374.5" y="106" font-size="12" text-anchor="middle">58</text>
<rect x="387" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="410" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="420.5" y="106" font-size="12" text-anchor="middle">60</text>
<rect x="433" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="456" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="466.5" y="106" font-size="12" text-anchor="middle">62</text>
<rect x="479" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="502" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="512.5" y="106" font-size="12" text-anchor="middle">64</text>
<rect x="525" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="548" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="558.5" y="106" font-size="12" text-anchor="middle">66</text>
<rect x="571" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="594" y="66" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="604.5" y="106" font-size="12" text-anchor="middle">68</text>
<rect x="617" y="66" width="21" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="180" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="190.5" y="156" font-size="12" text-anchor="middle">50</text>
<rect x="203" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="226" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="236.5" y="156" font-size="12" text-anchor="middle">52</text>
<rect x="249" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="272" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="282.5" y="156" font-size="12" text-anchor="middle">54</text>
<rect x="295" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="318" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="328.5" y="156" font-size="12" text-anchor="middle">56</text>
<rect x="341" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="364" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="374.5" y="156" font-size="12" text-anchor="middle">58</text>
<rect x="387" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="410" y="116" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="420.5" y="156" font-size="12" text-anchor="middle">60</text>
<rect x="433" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="456" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="466.5" y="156" font-size="12" text-anchor="middle">62</text>
<rect x="479" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="502" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="512.5" y="156" font-size="12" text-anchor="middle">64</text>
<rect x="525" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="548" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="558.5" y="156" font-size="12" text-anchor="middle">66</text>
<rect x="571" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="594" y="116" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="604.5" y="156" font-size="12" text-anchor="middle">68</text>
<rect x="617" y="116" width="21" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="180" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="190.5" y="206" font-size="12" text-anchor="middle">50</text>
<rect x="203" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="226" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="236.5" y="206" font-size="12" text-anchor="middle">52</text>
<rect x="249" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="272" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="282.5" y="206" font-size="12" text-anchor="middle">54</text>
<rect x="295" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="318" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="328.5" y="206" font-size="12" text-anchor="middle">56</text>
<rect x="341" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="364" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="374.5" y="206" font-size="12" text-anchor="middle">58</text>
<rect x="387" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="410" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="420.5" y="206" font-size="12" text-anchor="middle">60</text>
<rect x="433" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="456" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="466.5" y="206" font-size="12" text-anchor="middle">62</text>
<rect x="479" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="502" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="512.5" y="206" font-size="12" text-anchor="middle">64</text>
<rect x="525" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="548" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="558.5" y="206" font-size="12" text-anchor="middle">66</text>
<rect x="571" y="166" width="21" height="24" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<rect x="594" y="166" width="21" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="604.5" y="206" font-size="12" text-anchor="middle">68</text>
<rect x="617" y="166" width="21" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="232" font-size="12">(A) 정지 전 옛 샘플 8개를 19 ms 늦게 처리 · (B) 가장 최근 8개, 최대 7.9 ms 묵음</text>
<text x="20" y="252" font-size="12">(C) 하나만, 0.9 ms — 단 ovr=0: 덮어쓰기는 잃은 개수를 세 주지 않는다 (순번으로 센다)</text>
<text x="20" y="272" font-size="12">회색 = 정지와 무관하게 정상 처리된 것 (69는 셋 다 살아남음)</text>
</svg>
```

그림 9 — 예제 11 실측을 순번으로 그린 것. 같은 11~18개를 잃어도 **어느 것을** 잃는지가 정책마다 다르다.

출력에서 볼 것:

- **(A) drop-newest: 50~57은 남고 58~68을 잃었다.** 소비자는 깨어나서 19 ms 묵은 샘플 8개부터 처리했다(max age 18993 µs). 제어 데이터라면 최악이다 — 가장 쓸모없는 옛 값을 먼저 쓴다. 대신 **명령 · 로그 · 프로토콜 스트림**에는 맞다: 순서가 보존되고 유실이 한 구간에 몰려 상위 프로토콜이 재전송을 요청하기 쉽다.
- **(B) drop-oldest: 50~60을 잃고 최근 8개(61~68)를 남겼다.** 최대 나이 7.9 ms. 최근 이력이 필요한 데이터(필터 입력, 블랙박스의 사고 직전 기록)에 맞다.
- **(C) latest-only: 하나만(68), 나이 0.9 ms.** 자세 setpoint, 최신 센서 값, 상태 표시처럼 **최신 값만 의미 있는** 데이터에 맞다. 그런데 **ovr=0** — 덮어쓰기는 실패가 아니므로 잃은 18개를 아무도 세지 않았다. 순번(seq)을 같이 넣어 소비 쪽에서 빈 구간을 세야 한다.
- `xQueueOverwriteFromISR`는 길이 1 큐에만 쓸 수 있다 — `queue.c`의 `xQueueGenericSendFromISR`가 `configASSERT`로 막는다(§6).

### 5.3 데이터 종류별 정책

| 데이터 | 정책 | 이유 |
|---|---|---|
| 제어 입력 · setpoint · 최신 상태 | latest-only (길이 1 + 덮어쓰기) 또는 seqlock 더블 버퍼(D8 §4) | 옛 값은 해롭다 |
| 필터 · 추정기 입력, 사고 기록 | drop-oldest (원형 이력) | 최근 이력이 중요 |
| 명령 · 프로토콜 프레임 · 로그 | drop-newest + 카운트 + 상류 backpressure | 순서 보존, 재전송 가능 |
| 오디오 · 스트림 블록 | drop-newest 또는 블록 단위 버림 + 은닉(concealment) | 연속성이 중요 |
| fault · 경보 | 버리지 않는다 — 전용 경로(카운터 · 비트), 큐가 아니라 상태로 | 유실 불가 |

### 5.4 SPSC 링에서 drop-oldest를 하면 안 되는 이유와 대안

D8의 SPSC 링은 "생산자만 head를, 소비자만 tail을 쓴다"는 소유권 규칙으로 락 없이 돈다. drop-oldest를 하려면 생산자(ISR)가 소비자의 tail을 옮겨야 하는데, 소비자가 그 칸을 읽는 도중일 수 있다 — 소유권이 깨지고 레이스가 생긴다. 대안은 셋이다. ① FreeRTOS 큐처럼 크리티컬 섹션이 있는 구조를 쓴다(예제 11 (B)). ② 링 대신 **순번을 붙인 덮어쓰기 버퍼**를 쓰고, 소비자가 읽은 뒤 순번으로 "그사이 덮였는지"를 판정한다(seqlock과 같은 생각, D8 §4). ③ 링은 drop-newest로 두고 소비자가 늦었을 때 **스스로 앞부분을 건너뛴다**(소비자가 tail을 옮기므로 소유권이 지켜진다).

### 5.5 "가장 싼 자리에서 버린다"

예제 10을 다시 보자. per-IRQ 모드는 6346~49248개를 **3 µs씩 복사한 뒤** 링이 차서 버렸고, NAPI 모드는 하드웨어 FIFO에서 **비용 0으로** 버렸다. 과부하에서 버리는 것은 피할 수 없다. 설계가 고를 수 있는 것은 **어디서** 버리느냐다 — 파이프라인의 가능한 한 앞에서, 일을 들이기 전에 버린다. 하드웨어 필터(CAN 수신 필터, 이더넷 MAC 주소 필터)가 가장 앞이고, 그다음이 장치 FIFO, 그다음이 ISR, 가장 나쁜 곳이 처리를 절반쯤 한 뒤다.
---

## 6. RTOS 커널 안에서는 무슨 일이 일어나나

이 노트의 패턴 중 커널이 직접 주는 것은 적다. 폭주 감시, 모드 전환, 묶음 정책, 버림 정책은 **어느 커널에도 없고** 드라이버가 짓는다. 커널이 주는 재료는 세 가지다: **세는 깨움**, **실패를 돌려주는 넣기**, **ISR에서 부를 수 있는 지연 실행**. 그 재료가 소스의 어디에 있는지 확인한다.

**예제 12 — 소스에서 재료 확인 (FreeRTOS V11.3.2, Zephyr v4.4.2)**

무엇을 확인하는 코드인지: 이 노트가 기대는 동작 다섯 가지 — notify가 개수를 센다, Take가 0으로 지우거나 1 줄인다, ISR 큐 넣기가 차면 실패한다, 덮어쓰기는 길이 1만, Zephyr의 IRQ 끄기 · msgq 실패 · gpio-keys debounce — 를 grep으로 찾는다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel && grep -n "ulNotifiedValue\[ uxIndexToNotify \] )++;" tasks.c; grep -n "ulNotifiedValue\[ uxIndexToWaitOn \] = ( uint32_t ) 0U;\|= ulReturn - ( uint32_t ) 1;" tasks.c; grep -n "xReturn = errQUEUE_FULL;" queue.c | head -2; grep -n "queueOVERWRITE ) && ( pxQueue->uxLength != 1 )" queue.c; cd /var/rtoslab/zephyrproject/zephyr && grep -n "NVIC_DisableIRQ\|NVIC_EnableIRQ" arch/arm/core/cortex_m/irq_manage.c | head -2; grep -n "result = -ENOMSG;" kernel/msg_q.c | head -1; grep -n "k_work_reschedule(&pin_data->work" drivers/input/input_gpio_keys.c; grep -n "default: 30" dts/bindings/input/gpio-keys.yaml'
```

```text
8047:                    ( pxTCB->ulNotifiedValue[ uxIndexToNotify ] )++;
8190:                    ( pxTCB->ulNotifiedValue[ uxIndexToNotify ] )++;
8358:            ( pxTCB->ulNotifiedValue[ uxIndexToNotify ] )++;
7873:                    pxCurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] = ( uint32_t ) 0U;
7877:                    pxCurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] = ulReturn - ( uint32_t ) 1;
1327:            xReturn = errQUEUE_FULL;
1498:            xReturn = errQUEUE_FULL;
962:    configASSERT( !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );
1179:    configASSERT( ( pxQueue != NULL ) && !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );
58:	NVIC_EnableIRQ((IRQn_Type)irq);
63:	NVIC_DisableIRQ((IRQn_Type)irq);
213:		result = -ENOMSG;
139:	k_work_reschedule(&pin_data->work, K_MSEC(cfg->debounce_interval_ms));
18:    default: 30
```

출력에서 볼 것 (줄 번호로 함수를 찾아 읽었다):

- `tasks.c` 8358줄은 `vTaskGenericNotifyGiveFromISR`(8315줄 시작) 안이다 — ISR의 give는 **값을 1 올린다**. 8047줄은 `xTaskGenericNotify`, 8190줄은 `xTaskGenericNotifyFromISR`의 `eIncrement` 경우다. 그래서 깨움이 합쳐져도 "몇 번 왔는지"는 남는다.
- 7873줄과 7877줄은 `ulTaskGenericNotifyTake`(7797줄 시작) 안이다. `xClearCountOnExit`가 `pdTRUE`면 0으로, `pdFALSE`면 1 줄인다. 이 노트의 핸들러는 전부 `pdTRUE` + "링을 비울 때까지"다 — 몇 번 깨웠는지보다 링에 몇 개 있는지가 진실이기 때문이다.
- `queue.c` 1327줄은 `xQueueGenericSendFromISR` 안의 실패 경로다(1498줄은 `xQueueGiveFromISR`). 큐가 차 있고 덮어쓰기가 아니면 `errQUEUE_FULL`을 돌려주고 끝난다 — 기다리지 않는다. 1179줄의 `configASSERT`가 "덮어쓰기는 길이 1 큐에만"을 강제한다. 예제 11 (C)가 길이 1 큐를 따로 만든 이유다.
- Zephyr `arch/arm/core/cortex_m/irq_manage.c`의 `arm_irq_enable` / `arm_irq_disable`은 CMSIS `NVIC_EnableIRQ` / `NVIC_DisableIRQ` 한 줄이다. 커널은 "이 IRQ가 폭주 중이라 꺼졌다"는 상태를 따로 기억하지 않는다 — 드라이버의 몫이다.
- `kernel/msg_q.c` 213줄: `K_NO_WAIT`로 넣다가 차면 `-ENOMSG`. ISR에서 `k_msgq_put`은 항상 `K_NO_WAIT`로 부른다. FreeRTOS의 `errQUEUE_FULL`과 같은 자리다.
- `drivers/input/input_gpio_keys.c` 139줄: GPIO 콜백(ISR 문맥)마다 `k_work_reschedule`로 debounce 마감을 미룬다. 바인딩의 기본 `debounce-interval-ms`는 30이다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">이 노트의 패턴이 커널 소스의 어디를 지나가나</text>
<text x="175" y="45" font-size="13" text-anchor="middle">FreeRTOS V11.3.2</text>
<text x="505" y="45" font-size="13" text-anchor="middle">Zephyr v4.4.2</text>
<rect x="20" y="58" width="310" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="175" y="77" font-size="12" text-anchor="middle">벡터 테이블 → 앱 ISR 직접</text>
<text x="175" y="94" font-size="12" text-anchor="middle">끝에 portYIELD_FROM_ISR (앱 책임)</text>
<rect x="350" y="58" width="310" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="505" y="77" font-size="12" text-anchor="middle">_isr_wrapper → ISR (IRQ_CONNECT)</text>
<text x="505" y="94" font-size="12" text-anchor="middle">→ z_arm_int_exit가 PendSV 판단</text>
<rect x="20" y="118" width="310" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="175" y="137" font-size="12" text-anchor="middle">vTaskGenericNotifyGiveFromISR</text>
<text x="175" y="154" font-size="12" text-anchor="middle">ulNotifiedValue++ (사건 수를 센다)</text>
<rect x="350" y="118" width="310" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="505" y="137" font-size="12" text-anchor="middle">k_sem_give / k_msgq_put(K_NO_WAIT)</text>
<text x="505" y="154" font-size="12" text-anchor="middle">가득 차면 -ENOMSG</text>
<rect x="20" y="178" width="310" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="175" y="197" font-size="12" text-anchor="middle">xQueueGenericSendFromISR</text>
<text x="175" y="214" font-size="12" text-anchor="middle">가득 → errQUEUE_FULL, 덮어쓰기는 길이 1만</text>
<rect x="350" y="178" width="310" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="505" y="197" font-size="12" text-anchor="middle">k_work_schedule (isr-ok) → 백오프 복구</text>
<text x="505" y="214" font-size="12" text-anchor="middle">gpio-keys: 매 edge k_work_reschedule</text>
<rect x="20" y="238" width="310" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175" y="257" font-size="12" text-anchor="middle">ulTaskGenericNotifyTake</text>
<text x="175" y="274" font-size="12" text-anchor="middle">pdTRUE: 0으로 / pdFALSE: 1 감소</text>
<rect x="350" y="238" width="310" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="505" y="257" font-size="12" text-anchor="middle">irq_disable / irq_enable</text>
<text x="505" y="274" font-size="12" text-anchor="middle">= NVIC_DisableIRQ / NVIC_EnableIRQ</text>
<line x1="175" y1="102" x2="175" y2="116" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="505" y1="102" x2="505" y2="116" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="340" y="305" font-size="12" text-anchor="middle">폭주 감시 · 모드 전환 · 정책은 어느 커널에도 없다 — 드라이버(앱)가 짓는다.</text>
<text x="340" y="325" font-size="12" text-anchor="middle">커널이 주는 것은 "세는 깨움", "실패를 돌려주는 넣기", "ISR에서 부를 수 있는 지연 실행"뿐</text>
</svg>
```

그림 10 — 이 노트의 패턴이 지나가는 커널 경로. FreeRTOS는 ISR이 벡터 테이블에서 바로 불리고 전환 요청(`portYIELD_FROM_ISR`)이 앱 책임이다. Zephyr는 `_isr_wrapper`가 ISR을 감싸고 끝에서 `z_arm_int_exit`(`exc_exit.c`, `z_arm_exc_exit`의 별칭)가 PendSV를 판단한다(D7 §2.3, §7.2).

두 커널의 차이가 이 노트의 설계에 주는 영향은 하나다. FreeRTOS에서 ISR 끝의 `portYIELD_FROM_ISR(w)`를 빼먹으면 핸들러가 다음 tick까지 늦는다(D7 §2.3, E1). Zephyr는 래퍼가 항상 검사하므로 그 실수가 없다. 대신 Zephyr에서는 `IRQ_CONNECT`가 아닌 `IRQ_DIRECT_CONNECT`(zero-latency 계열, E2)로 등록한 ISR은 래퍼를 거치지 않으므로 `ISR_DIRECT_PM`, `ISR_DIRECT_FOOTER` 같은 매크로로 같은 일을 직접 해야 한다.

---

## 7. 실무에서 다시 보기

### 7.1 드론 비행 제어기 — 원천마다 다른 경로

- **IMU**(SPI, 1~8 kHz): data-ready 핀 ISR은 시각을 찍고 SPI DMA를 시작하고 끝. DMA 완료 ISR이 rate-loop 태스크를 사건마다 깨운다(묶지 않는다). 최신 IMU 칩은 FIFO + watermark 인터럽트를 주므로, 로깅용 고속 샘플은 FIFO로 묶어 받고 제어용은 사건마다 받는 식으로 두 경로를 나누기도 한다.
- **GNSS · 텔레메트리 UART**: UART RX FIFO 임계값 + idle-line(또는 receiver timeout) 인터럽트 = 하드웨어 "N 또는 T". 문장 단위로 파서 태스크를 깨운다(D5 §4.3의 stream buffer trigger level과 결합).
- **RC 수신기 · 스위치 입력**: arm 스위치는 debounce(예제 9 (C)) 없이 쓰면 안 된다. 그리고 RC 링크가 끊겨 입력 핀이 떠 버리면 폭주가 난다 — 감시 카운터가 failsafe 진입의 근거가 된다.
- **블랙박스 로그**: drop-newest + 유실 카운터(로그 안에 "N개 유실" 기록을 남긴다), 플래시 쓰기 태스크는 가장 낮은 우선순위.

### 7.2 로봇 관절 모터 컨트롤러 — CAN 홍수

FOC 전류 루프 ISR(20 kHz)은 위임하지 않는다 — ISR 안에서 끝까지 한다(E2의 zero-latency 계층). 위임 대상은 CAN이다. 같은 버스의 다른 노드가 고장 나 같은 프레임을 쉬지 않고 보내면(babbling node) 수신 IRQ가 폭주한다. 방어는 순서대로: **하드웨어 수신 필터**로 내 ID만 받기(가장 싼 버림), 수신 FIFO watermark로 묶기, 창별 카운터로 폭주 감지 → CAN RX 인터럽트를 끄고 폴링 모드로 → 일정 시간 지속되면 fault로 보고하고 안전 정지. bus-off 복구 자체는 J6.

### 7.3 센서 허브 MCU — 타임스탬프가 제품

Linux SoC 옆에서 IMU · 카메라 트리거 · 휠 엔코더의 시각을 찍는 센서 허브에서는 §1.6이 제품 사양 그 자체다. 모든 사건은 ISR(가능하면 타이머 입력 캡처, E5)에서 시각이 찍히고, 호스트로 보내는 SPI · USB 패킷은 "N개 또는 T µs"로 묶는다. 호스트가 늦으면 drop-oldest(최신 이력 유지) + 유실 카운터를 패킷 헤더에 싣는다. 호스트 시계로의 변환은 Q7.

### 7.4 SSD 펌웨어와의 대응

| SSD 펌웨어 (Don의 경험) | 이 노트의 개념 |
|---|---|
| 완료 ISR: 상태 읽기 → 읽은 비트만 W1C → 완료 링에 기록 | §1.2 ~ §1.4 |
| executive가 완료 링을 비울 때까지 처리 | §2.2 "깨어나면 비울 때까지" |
| NVMe Interrupt Coalescing (Aggregation Threshold / Time) | §2 "N개 또는 T µs" |
| 완료 비트 미처리로 ISR 무한 루프 → watchdog | §4.1 끼인 IRQ, §4.2 |
| 호스트 큐 가득 → 명령 fetch 중지 | §5.1 상류 backpressure |
| 에러 인터럽트 폭주 시 해당 채널 격리 후 복구 | §4.3 감시 · 격리 · 복구 |

---

## 8. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR 끝에서 상태 비트를 지움 | 사건 개수가 가끔 모자람 (예제 1: 45/499) | 처리 중 온 사건이 같은 비트에 합쳐진 뒤 같이 지워짐 | 첫머리에서 지우기 |
| W1C에 `0xFFFFFFFF` 쓰기 | 다른 종류 사건이 간헐적으로 사라짐 (예제 2: 21~23%) | 읽은 뒤 선 비트까지 지움 | 읽은 값 그대로 다시 쓰기 |
| 데이터 읽기 전에 RX 비트 지우기 | 유실 + 같은 값 중복, overrun 없음 | 하드웨어가 빈 레지스터로 보고 덮어씀 | 데이터 먼저, 비트는 나중 (또는 읽기가 ack) |
| 클리어가 ISR 마지막 명령 | 실칩에서만 이중 카운트 | 쓰기 버퍼 · 브리지 때문에 복귀 뒤 도착 | 앞으로 옮기기, DSB 또는 다시 읽기, 상태 확인 |
| 오류 플래그(ORE 등) 미처리 | 수신 중 시스템 멈춤, watchdog 리셋 | 끼인 level IRQ | 오류 플래그 지우는 경로 필수, 감시 카운터 |
| 태스크에서 타임스탬프 | 추정기 · 융합 품질 저하, `dt` 수백 µs 흔들림 | 깨움 지연 + 선점이 섞임 | ISR(또는 입력 캡처)에서 찍어 기록과 함께 넘기기 |
| T 없는 묶음 | 트래픽이 줄면 마지막 데이터가 늦음 (예제 5: 15 ms) | N개가 안 참 | one-shot 타이머 / idle-line으로 T 추가 |
| 깨어날 때 하나만 처리 | 링에 데이터가 갇혀 다음 사건 때 나옴 | notification 합쳐짐 | 비울 때까지 처리 |
| 폭주 감시를 tick으로 잼 | 폭주 중 감시가 안 돎 | tick 자체가 멈춤 | 하드웨어 카운터 기준 창 |
| NAPI 복귀 때 재확인 누락 | 트래픽이 잠깐 멈췄다 오면 수신 정지 | IRQ 켜기 전 도착분이 IRQ를 못 냄 | 켠 뒤 FIFO 재확인 |
| `xQueueOverwrite`로 유실 은폐 | "데이터는 오는데 가끔 튐", 유실 카운트 0 | 덮어쓰기는 실패가 아님 | 순번을 같이 넣어 소비 쪽에서 세기 |

---

## 9. 면접에서 이렇게 말한다

**Q.** What do you do inside an ISR, and what do you defer to a task?

**A.** ISR에는 하드웨어가 지금 필요로 하는 네 가지만 남긴다 — 원인 확인 · 지우기, 사건 시각 찍기, FIFO 같은 휘발성 데이터를 꺼내기, 담당 태스크 깨우기. 블록할 수 있는 일(I2C, 플래시, 뮤텍스, printf)과 길이가 변하는 일(파싱, 계산)은 전부 태스크로 보낸다. 이유는 ISR 길이가 같거나 낮은 우선순위의 모든 것의 지연에 더해지고, 빈도를 곱한 만큼 CPU를 먼저 가져가기 때문이다. 예외는 과전류 PWM 차단처럼 µs가 늦으면 하드웨어가 타는 안전 동작이다.

> In the ISR I keep only what the hardware needs right now: identify and clear the source, timestamp the event, drain volatile data like a FIFO, and wake the handler task. Anything that can block or has variable length — bus transactions, flash, parsing, math — goes to a task, because the ISR's worst-case length adds to the latency of everything at equal or lower priority and its rate times length comes straight off the CPU budget. The exception is safety actions like shutting PWM on overcurrent, which must happen in the ISR.

**꼬리 질문**: "그 태스크의 우선순위는?" → 위임된 일의 마감으로 정하고, 방해할 수 있는 태스크보다 엄격히 높게(같으면 FreeRTOS는 다음 tick까지 안 바꾼다).

**Q.** Where in the ISR do you clear the interrupt flag, and why does it matter?

**A.** 첫머리에서 상태를 한 번 읽고 읽은 비트만 지운다. 끝에서 지우면 처리 중 다시 온 사건이 같은 비트에 합쳐졌다가 같이 지워져 유실된다 — QEMU 실측에서 499개 중 45개였다. W1C 레지스터에 전부 1을 쓰면 읽은 뒤 선 다른 비트까지 지워진다. 반대로 데이터 레지스터와 짝인 비트는 데이터를 꺼낸 뒤 놓아야 한다. 그리고 지우기가 ISR의 마지막 쓰기면 쓰기 버퍼 때문에 복귀 뒤에 도착해 가짜 재진입이 생길 수 있으니 앞에 두거나 DSB · 다시 읽기를 한다.

> Early: read the status once at the top and write back only the bits I saw. Clearing at the end can swallow an event that re-latched during processing — in my measurement 45 of 499 events vanished. Writing all ones to a W1C register kills bits that arrived after my read. Data-coupled flags are the exception: take the data first. And if the clear is the last store before exception return, a posted write may land late and cause a spurious re-entry, so I clear early or add a DSB or a read-back, and the ISR always checks the status before counting.

**Q.** Events arrive at 20 kHz. Do you wake the task for every event?

**A.** 깨움 비용과 지연 예산으로 계산한다. QEMU 실측에서 깨움 한 번이 17.4 µs였고, 사건마다 깨우면 CPU 44%, 8개씩 묶으면 14%였다. 대신 최대 지연이 (N−1) × 간격만큼 는다. 그래서 N은 지연 예산에서 계산하고(400 µs 예산, 50 µs 간격이면 N=8), 트래픽이 줄었을 때를 위해 시간 상한 T를 반드시 같이 둔다 — NVMe interrupt coalescing의 threshold + time과 같은 구조다. 가능하면 하드웨어(FIFO 임계값 + idle-line, DMA)가 묶게 한다. 제어 루프 입력은 묶지 않는다.

> I size it from the wake cost and the latency budget. In my measurement one wake cost about 17 µs; waking per event used 44% CPU, batching by 8 dropped it to 14%, at the price of up to (N−1) event intervals of latency. So N comes from the latency budget, and I always pair it with a timeout T so a trickle of traffic isn't stranded — the same threshold-plus-time scheme as NVMe interrupt coalescing. If the hardware offers a FIFO threshold with an idle or receiver timeout, or DMA, I let it do the batching. Control-loop inputs are never batched.

**Q.** What is an interrupt storm and how do you protect an RTOS system from it?

**A.** IRQ가 설계 빈도보다 훨씬 자주, 극단적으로는 복귀 즉시 계속 들어오는 상태다. 원인은 안 지워진 level IRQ, 붙어 버린 INT 핀, 튀는 GPIO, 떠 있는 핀, 트래픽 홍수. ISR은 모든 태스크보다 높아서 태스크 우선순위로는 못 막고, SysTick까지 막혀 RTOS 시간이 멈춘다 — 실측에서 128 ms 동안 tick 100개였다. 방어는 ISR 안 감시 카운터(하드웨어 시계 기준 창별 예산), 넘으면 그 소스만 끄고 복구 태스크를 깨우고, 태스크가 백오프 뒤 재초기화하고 다시 켜며, 횟수 제한 뒤에는 fault로 올린다. 실측에서 같은 고장에 제어 루프가 0번에서 10 ms당 9~10번으로 유지됐다.

> It's an IRQ firing far above its design rate, in the extreme re-entering immediately on return — from an uncleared level source, a stuck interrupt pin, contact bounce, a floating input, or a traffic flood. Since every ISR outranks every task, task priorities can't help, and the tick interrupt starves too, so RTOS time stops. My defense is a per-window counter in the ISR based on a hardware timer; over budget, the ISR masks only that source and wakes a recovery task, which backs off, reinitializes the device, re-enables, and after a few failures escalates to a fault. In a test, that kept a 1 kHz loop running through a 30 ms stuck-IRQ fault that otherwise froze it completely.

**Q.** How would you handle a UART or CAN receiving at line rate without starving the rest of the system?

**A.** 받는 쪽 livelock을 피하는 게 핵심이다. 패킷마다 IRQ를 받고 ISR에서 복사하면 입력률이 넘는 순간 처리량이 0이 된다 — 실측에서 4 µs 간격에서 처리 0, 제어 루프 1/200. 그래서 NAPI처럼 첫 IRQ에서 장치 RX 인터럽트를 끄고 태스크가 tick당 예산만큼 폴링하고, 비면 IRQ를 켠 뒤 FIFO를 한 번 더 확인한다. 과부하에서는 처리량이 예산에서 평평하게 유지되고(16000개), 넘친 것은 하드웨어 FIFO에서 공짜로 버려진다. 앞단에는 하드웨어 필터와 FIFO 임계값을 쓴다.

> The danger is receive livelock: with an interrupt and a copy per frame, once input exceeds capacity the system spends all its time receiving and delivers nothing — I measured zero delivered frames and a starved control loop at a 4 µs frame interval. So I switch to NAPI-style polling: the first interrupt masks the device RX interrupt and wakes a task that drains up to a budget per tick, and when the FIFO is empty it re-enables the interrupt and re-checks the FIFO to close the race. Under overload, throughput stays flat at the budget and excess frames are dropped for free in hardware. In front of that I use acceptance filters and FIFO thresholds.

**Q.** Your ISR's queue is full. What do you do?

**A.** ISR은 기다릴 수 없으니 정책이 미리 있어야 하고, 어떤 정책이든 버린 개수를 센다. 제어 값은 최신만 의미 있으니 길이 1 덮어쓰기(단 덮어쓰기는 유실을 안 세므로 순번을 넣는다), 필터 입력이나 사고 기록은 drop-oldest, 명령 · 로그 · 프로토콜은 drop-newest + 카운트 + 가능하면 상류 흐름 제어(RTS/CTS). 실측에서 20 ms 정지 뒤 drop-newest는 19 ms 묵은 데이터를 먼저 처리했고, latest-only는 0.9 ms짜리 하나를 처리했다. 그리고 SPSC 링에서 drop-oldest는 생산자가 소비자 인덱스를 건드려서 레이스가 되니 다른 구조를 쓴다.

> The ISR can't block, so the policy must be decided up front, and every policy counts what it drops. For control values only the latest matters, so a depth-one overwrite — with a sequence number, since overwrite silently hides loss. For filter inputs or crash history, drop-oldest. For commands, logs, and protocol frames, drop-newest with a counter and upstream flow control such as RTS/CTS where possible. After a 20 ms consumer stall, drop-newest processed 19 ms-old data first while latest-only delivered one sample 0.9 ms old. And drop-oldest on a lock-free SPSC ring is a race, because the producer would have to move the consumer's index.

**Q.** A button interrupt fires dozens of times per press. Fix it.

**A.** 기계식 접점의 튐이다. 실측에서 5번 누름에 edge 158개가 들어왔고 그대로 처리하면 79번 눌림으로 인식됐다. 첫 edge에서 그 EXTI 라인을 마스크하고 태스크를 깨워, 태스크가 debounce 시간(5~30 ms) 뒤 레벨을 읽어 안정된 변화만 사건으로 만들고 pending을 지운 뒤 라인을 다시 연다. ISR이 10번으로 줄고 결과는 정확했다. ISR 안 시간 잠금도 결과는 맞지만 ISR 수는 그대로이고 노이즈 하나에 반응한다. 하드웨어 글리치 필터가 있으면 먼저 쓴다. Zephyr의 gpio-keys는 edge마다 `k_work_reschedule`로 마감을 미루는 방식이다.

> That's contact bounce: I measured 158 edges for 5 presses, which a naive handler counts as 79 presses. I mask that line on the first edge and wake a task that waits the debounce interval, samples the settled level, turns only a stable change into an event, clears the pending bit, and unmasks. That cut the ISRs to 10 and got exactly 5 presses. A time lockout inside the ISR gives the right count but keeps every interrupt and reacts to a single glitch. If the MCU has an input glitch filter I enable it first; Zephyr's gpio-keys driver does the same thing by rescheduling a delayed work item on every edge.

---

## 10. 직접 해보기

1. **개념**: 상태 비트 하나짜리 level IRQ에서 "맨 끝 클리어"가 사건을 잃는 조건을 ISR 길이 C와 사건 간격 P로 써 보라. 예제 1에서 유실이 50이 아니라 45인 이유도 생각해 보라.
   정답/힌트: 처리 중(C 안에) 다음 사건이 오면 잃는다 → C > P인 ISR에서만. 260 µs ISR 직후 다음 ISR의 시작 위상에 따라 일부는 사건이 ISR 끝난 뒤 도착한다.
2. **코드**: 예제 2의 (B)를 "읽은 비트만, 그러나 끝에서" 지우도록 바꿔 돌려 보라. 유실이 사라지나? 남는 것이 있다면 무엇인가?
   정답/힌트: 서로 다른 비트는 살지만, 같은 비트(RX)가 처리 중 다시 서면 그 RX는 지워진다 — 위치 문제와 범위 문제는 따로다.
3. **측정**: 예제 5에서 처리 비용을 항목당 2 µs → 10 µs로 바꾸고 N=1, 8, 32를 다시 재라. 깨움 비용 비중이 어떻게 바뀌는지 예제 6 모델로 예측한 뒤 확인하라.
   정답/힌트: 바닥이 2만 × 10 µs = 20%로 오르고 깨움 몫(17.4 µs × 초당 깨움)은 그대로 — 일이 무거울수록 묶음의 상대 이득이 준다.
4. **코드**: 예제 7의 감시 창 예산을 20 → 6으로 줄여 보라(정상 5개/ms). 정상 구간에서 오탐이 나는가? 타이머 주기를 지터가 있게 바꾸면?
   정답/힌트: 예산이 정상 최대 버스트에 너무 가까우면 오탐 — 정상 최대의 2~4배로 잡고, 창 경계 효과도 고려한다.
5. **코드**: 예제 10의 NAPI 루프에서 "켠 뒤 재확인"(`if (f_in == f_out) break;` 다음 줄들)을 지우고 P=40 µs에서 돌려 보라. 무슨 일이 생기나?
   정답/힌트: 켜기 직전에 도착한 패킷이 IRQ를 못 내서 다음 패킷이 올 때까지 갇힌다. 이 에뮬레이션 장치는 "도착 시점에만" IRQ를 내므로 창이 실제로 열린다.
6. **설계**: 드론 FC의 GNSS(921600 baud, NMEA + UBX), RC(SBUS 100 kbaud), 텔레메트리(57600 baud) UART 셋에 대해 ISR에서 할 일, 넘기는 단위(N, T), 받는 태스크와 우선순위, 큐가 찼을 때 정책을 표로 써 보라.
   정답/힌트: RC는 지연이 중요 → 프레임 단위(idle-line) 즉시, latest-only. GNSS는 문장 단위 N + idle T, drop-newest + 카운트. 텔레메트리는 묶음 + drop-newest.
7. **측정**: 예제 11에 "drop-oldest를 SPSC 링으로 생산자가 tail을 옮겨서" 구현해 보고, 소비자가 빠르게 읽을 때 중복 · 깨진 항목이 생기는지 확인하라.
   정답/힌트: 소비자가 칸을 복사하는 도중 ISR이 tail을 옮기고 그 칸에 새 값을 쓰면 찢어진 항목이 나온다 — §5.4의 대안 ③으로 고친다.

---

## 11. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| deferred interrupt processing | 지연된 인터럽트 처리 | ISR은 최소 일만, 나머지는 태스크 문맥에서 |
| top half / bottom half | 상반부 / 하반부 | Linux 용어. MCU에서는 ISR / 핸들러 태스크 |
| ack (acknowledge) | 인터럽트 원인 확인 · 해제 | 장치 상태 비트를 지워 IRQ 라인을 내리는 것 |
| level-triggered | 레벨 트리거 | 라인이 올라가 있는 동안 계속 pending — 지우지 않으면 재진입 |
| edge-triggered | 엣지 트리거 | 변화 순간 한 번 래치 — 지우지 않아도 재진입은 없지만 합쳐질 수 있다 |
| W1C | write-1-to-clear | 1을 쓴 비트만 지워지는 상태 레지스터 |
| read-to-clear | 읽으면 지워짐 | 읽기 자체가 ack인 레지스터. 디버거가 읽어도 지워진다 |
| posted write | 응답 먼저, 전달은 나중 | 버스 브리지가 쓰기를 붙잡아 두는 것 — 클리어가 늦게 도착 |
| spurious interrupt | 가짜 인터럽트 | 원인 없이 들어온 진입 — ISR은 상태를 보고 그냥 나가야 한다 |
| coalescing | 합치기 | 여러 사건을 한 번의 깨움 · 인터럽트로 처리 |
| batching (N or T) | 묶음 | N개가 모이거나 T가 지나면 깨운다 |
| interrupt storm | 인터럽트 폭주 | 설계 빈도를 훨씬 넘는 IRQ로 CPU가 묶임 |
| receive livelock | 수신 라이브락 | 받기만 하다 처리 0 — CPU는 바쁜데 일은 안 됨 |
| NAPI | New API (Linux 네트워크) | IRQ 한 번 → 끄고 budget만큼 폴링 → 비면 다시 켬 |
| budget / quota | 예산 | 폴링 한 번(또는 tick당) 처리할 최대 개수 — CPU 상한 |
| debounce | 튐 제거 | 접점 튐이 끝난 뒤 안정된 레벨만 사건으로 |
| backpressure | 역압 | 소비가 느릴 때 생산을 늦추게 하는 신호 (RTS/CTS 등) |
| drop-newest / drop-oldest | 새것 / 옛것 버리기 | 큐가 찼을 때의 버림 정책 |
| latest-only (mailbox) | 최신 하나 | 길이 1 덮어쓰기 — 유실을 세지 않는다 |
| head-of-line blocking | 줄 앞 막힘 | 공용 worker에서 앞 항목이 길면 뒤가 전부 밀림 |

---

## 12. 요약 & 체크리스트

ISR은 접수 창구다 — 원인을 확인해 읽은 비트만 첫머리에서 지우고(데이터 짝 비트는 데이터를 꺼낸 뒤), 사건 시각을 찍고, 휘발성 데이터를 꺼내고, 담당 태스크를 깨우고 나간다. 끝에서 지우면 사건을 잃고(45/499), W1C를 전부 지우면 다른 사건을 잃고(21~23%), 안 지우면 RTOS 시간까지 멈춘다(121.8 ms 동안 tick 0). 넘기는 단위는 깨움 비용(QEMU 17.4 µs)과 지연 예산으로 계산한다: 사건마다 깨우면 CPU 44%, 8개 묶음이면 14%, 대신 최대 지연 (N−1) × 간격 — 그리고 T가 없으면 느린 트래픽이 갇힌다. 받는 쪽 우선순위는 위임된 일의 마감으로 정한다(D7). 폭주는 태스크 우선순위로 못 막으니 ISR 안 감시 카운터 → 소스 끄기 → 태스크에서 백오프 복구로 막고, 홍수는 NAPI식 폴링 전환으로 처리량을 예산에서 평평하게 유지하며, 큐가 차면 데이터 종류에 맞게 버리되 반드시 센다. 버리는 자리는 파이프라인의 가능한 한 앞이다.

- [ ] ISR에 남길 네 가지와 예외(안전 동작)를 말할 수 있다
- [ ] 상태 비트 종류별(래치 · W1C · 데이터 짝 · 레벨 · 오류 · read-to-clear) 지우는 법을 안다
- [ ] 끝에서 지우기 · 전부 지우기 · 읽기 전 지우기의 증상을 숫자로 설명할 수 있다
- [ ] 마지막 쓰기 + 예외 복귀의 가짜 재진입과 세 가지 대처(앞으로, DSB, 다시 읽기)를 안다
- [ ] 타임스탬프를 ISR에서 찍어야 하는 이유를 `dt` 흔들림으로 설명할 수 있다
- [ ] 깨움 비용 모델로 N을 지연 예산에서 계산하고 T를 같이 둘 수 있다
- [ ] 전용 핸들러 vs 공용 worker를 마감과 head-of-line으로 고를 수 있다
- [ ] 폭주가 tick까지 멈추는 이유와 감시 카운터를 하드웨어 시계로 재야 하는 이유를 안다
- [ ] 감시 → 격리 → 복구 → 보고 · 강등 사다리를 FreeRTOS와 Zephyr로 쓸 수 있다
- [ ] debounce를 마스크 + 태스크로 구현하고 ISR 안 잠금과의 차이를 안다
- [ ] receive livelock과 NAPI식 전환, 재확인 경쟁 창을 설명할 수 있다
- [ ] drop-newest / drop-oldest / latest-only를 데이터 종류에 맞게 고르고 유실을 셀 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM J1 | ISR 설계 원칙 (짧게, 플래그 클리어 위치 — 늦게 지우면 재진입, 이벤트를 기록하고 메인에서 처리) | §1 전체 — §1.2 남길 것 표, §1.3 예제 1(클리어 위치) · 장치별 표, §1.4 예제 2(W1C · 읽기 전 클리어), §1.5 예제 3(쓰기 버퍼 · DSB, D8 §7 링크), §1.6 예제 4(기록하고 나가기 · 타임스탬프), §1.7, §4.5 예제 9(debounce) | E4를 링크하고 **super-loop + 플래그 폴링으로 이벤트를 처리하는 관점**만: ISR이 플래그 · 링을 세우고 메인 루프가 폴링하는 최소 코드, 루프 한 바퀴 최악 시간이 응답 상한이라는 점 |
| BM J2 (일부) | 메인 ↔ ISR 데이터 레이스 | §1.7에서 C7 · D8 참조만 | 변경 없음 (주인은 RTOS C7 · D8) |
| BM J7 (일부) | 인터럽트 지연 · 지터 측정 | §1.6 ISR 타임스탬프 흔들림에서 E3 참조만 | 변경 없음 (주인은 RTOS E3) |

---

## 참고 자료

- FreeRTOS V11.3.2 소스(랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `tasks.c`의 `vTaskGenericNotifyGiveFromISR`, `ulTaskGenericNotifyTake`, `traceTASK_SWITCHED_IN` 호출 위치 · `queue.c`의 `xQueueGenericSendFromISR`, `xQueueReceiveFromISR` · `include/queue.h`의 `xQueueOverwriteFromISR`
- Richard Barry, "Mastering the FreeRTOS Real Time Kernel" — Interrupt Management 장(deferred interrupt processing, 중앙 집중 위임): https://github.com/FreeRTOS/FreeRTOS-Kernel-Book
- FreeRTOS 문서 — Task notifications: https://en.freertos.org/Documentation/02-Kernel/02-Kernel-features/03-Direct-to-task-notifications/01-Task-notifications
- Zephyr v4.4.2 소스(랩 `/var/rtoslab/zephyrproject/zephyr`): `include/zephyr/irq.h`(`irq_enable` · `irq_disable`), `arch/arm/core/cortex_m/irq_manage.c`, `arch/arm/core/cortex_m/isr_wrapper.c`, `arch/arm/core/cortex_m/exc_exit.c`, `include/zephyr/kernel.h`(`k_work_schedule_for_queue`의 `@isr_ok`), `kernel/msg_q.c`, `drivers/input/input_gpio_keys.c`, `dts/bindings/input/gpio-keys.yaml`
- Zephyr 문서 — Interrupts: https://docs.zephyrproject.org/latest/kernel/services/interrupts.html
- Zephyr 문서 — Workqueue Threads: https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html
- Arm Application Note 321, "ARM Cortex-M Programming Guide to Memory Barrier Instructions" (DSB · 쓰기 버퍼 — 자세한 내용은 D8 §7)
- J. C. Mogul, K. K. Ramakrishnan, "Eliminating Receive Livelock in an Interrupt-Driven Kernel", USENIX Annual Technical Conference 1996 (ACM Transactions on Computer Systems 1997판)
- Linux 커널 헤더 `include/linux/netdevice.h`(`NAPI_POLL_WEIGHT`) — VM의 linux-headers-7.0.0-38에서 확인
- NVM Express Base Specification — Interrupt Coalescing 기능(Feature Identifier 08h, Aggregation Threshold · Aggregation Time)
- 이 노트와 이어지는 노트: RTOS D1, D5, D7, D8, E1, E2, E3, E5, E6, E8 · EL F6, F7 · Bare-Metal J1
