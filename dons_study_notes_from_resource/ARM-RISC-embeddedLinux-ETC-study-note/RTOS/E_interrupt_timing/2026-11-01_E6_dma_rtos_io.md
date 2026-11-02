# E6. DMA + RTOS I/O — 핑퐁 · 원형 버퍼의 소유권, HT/TC 완료 → 태스크 통지, UART idle-line 수신, TX 게이트키퍼, Cortex-M7 캐시 처리

> **이 노트를 다 읽으면**: 원형 DMA의 두 절반을 "지금 누구 것인가"로 설명하고, CPU가 늦어서 DMA가 한 바퀴 돌아 덮어쓰는 overrun을 소유권 플래그로 잡아낼 수 있다(QEMU 실측: overrun 41회 경보, 깨진 프레임 4개) · HT/TC 콜백을 task notification(비트 · 카운트)이나 큐(포인터 + 순번)로 태스크에 넘기는 세 방법의 차이와 각각이 잃는 정보를 말할 수 있다 · STM32식 "IDLE IRQ + NDTR → 새 바이트 = pos − last_pos(wrap 포함)" 수신기와 Zephyr async UART API(`UART_RX_BUF_REQUEST` → `uart_rx_buf_rsp`, `UART_RX_RDY`, `UART_RX_BUF_RELEASED`)를 직접 짜고 돌릴 수 있다 · TX DMA를 mutex 게이트 + 완료 통지로 감싸고, Cortex-M7에서 언제 clean · invalidate하는지(특히 idle-line의 "반쯤 찬 라인")를 규칙으로 말할 수 있다 · 바이트 IRQ / DMA HT·TC / DMA + IDLE의 CPU 부하와 지연을 숫자로 비교할 수 있다(115200 baud에서 순부하 15.9 % 대 1.3 %, 프레임 끝 지연 2732 µs 대 96 µs)
> **왜 필요한가**: 드론 FC의 GNSS · 텔레메트리 UART, IMU SPI 버스트, 모터 컨트롤러의 ADC 샘플링은 전부 "DMA가 채우고 태스크가 처리"한다. 면접에서는 "가변 길이 UART 프레임을 DMA로 어떻게 받나?", "DMA 완료를 태스크에 어떻게 알리나, ISR에서 무엇을 하면 안 되나?", "핑퐁 버퍼에서 CPU가 늦으면?", "M7에서 DMA 데이터가 가끔 옛날 값이다 — 왜?"가 단골이다
> **Don 기준 난이도**: DMA 디스크립터 · 완료 큐 · 캐시 유지 · 링버퍼는 SSD 펌웨어에서 이미 몸으로 안다 / 새로 배울 부분은 **RTOS와 맞물리는 지점** — 완료 콜백이 도는 문맥(ISR, Zephyr에선 드라이버에 따라 workqueue), 통지 수단별로 사라지는 정보(순서 · 횟수), STM32 IDLE + NDTR 관용구, Zephyr async UART의 버퍼 교대 프로토콜, "이벤트는 힌트이고 위치(NDTR)가 진실"이라는 설계 감각
> **선행 노트**: B8(캐시 · TCM · MPU — 캐시 원리와 유지 연산은 거기서), E1(FromISR 규칙), E4(ISR → 태스크 처리 경로), D4(큐 · 빈 버퍼 큐 + 찬 버퍼 큐로 소유권 넘기기), D5(task notification · stream buffer), D8(SPSC 링 · 배리어), D2 · D3(mutex · 우선순위 상속), EL H9(Linux DMA API — 여기서는 MCU에서 달라지는 점만), EL H8(UART · tty). 이어지는 노트: J 모듈(드라이버 모델), N7(WCET와 캐시)
> 🔁 **Bare-Metal 겹침**: BM I8(DMA 컨트롤러 — 원형 · 더블 버퍼 사용법, half / full 완료 → 태스크 통지, 캐시 정리 부분) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "채널 · 스트림 · DMAMUX 요청 매핑, linked-list 디스크립터 레지스터, M2M, 우선순위 · 버스트 · FIFO, 에러 플래그 같은 **컨트롤러 레지스터 수준**"만 더한다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러에서 하던 일을 떠올리면 이 노트의 절반은 이미 안다. 호스트 쓰기 데이터는 DMA 엔진이 SRAM 버퍼로 옮기고, 엔진은 끝나면 완료 큐에 엔트리를 넣고 인터럽트를 올렸다. 펌웨어는 그 버퍼를 **완료 엔트리를 본 다음에만** 만졌고, 다 쓴 버퍼는 free 리스트로 돌려줬다. 캐시가 있는 Cortex-R에서는 그 사이에 clean · invalidate가 끼었다.

MCU + RTOS에서도 뼈대는 같다. 달라지는 것은 세 가지다.

1. **완료 이벤트를 받는 쪽이 "펌웨어 메인 루프"가 아니라 태스크다.** ISR은 짧게 끝내고 태스크를 깨운다(E1, E4). 무엇으로 깨우느냐(notification 비트, 카운트, 큐)에 따라 **순서와 횟수 정보가 살아남기도, 사라지기도** 한다.
2. **버퍼가 고정 크기 원형 하나인 경우가 많다.** SSD처럼 버퍼 풀을 넉넉히 두기보다, UART RX처럼 원형 버퍼 하나를 DMA가 계속 돌며 채우고 CPU가 뒤따라 읽는다. 그래서 "어느 절반이 지금 누구 것인가"가 설계의 중심이 된다.
3. **데이터 길이를 모른다.** GNSS NMEA 문장, MAVLink 패킷, 디버그 콘솔 명령은 길이가 제각각이다. DMA의 "반 찼다 / 다 찼다" 이벤트만으로는 마지막 프레임이 버퍼 안에 갇힌다. 그래서 UART의 **IDLE line** 검출이 필요하다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="40" width="120" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="70" y="62" font-size="13" text-anchor="middle">UART · SPI</text>
<text x="70" y="80" font-size="12" text-anchor="middle">(장치 FIFO)</text>
<line x1="130" y1="65" x2="173" y2="65" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="175" y="40" width="110" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="230" y="62" font-size="13" text-anchor="middle">DMA 엔진</text>
<text x="230" y="80" font-size="12" text-anchor="middle">NDTR 감소</text>
<line x1="285" y1="65" x2="328" y2="65" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="330" y="30" width="150" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="405" y="52" font-size="13" text-anchor="middle">RAM 원형 버퍼</text>
<rect x="345" y="62" width="58" height="26" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="374" y="80" font-size="12" text-anchor="middle">절반 A</text>
<rect x="407" y="62" width="58" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="436" y="80" font-size="12" text-anchor="middle">절반 B</text>
<line x1="230" y1="90" x2="230" y2="148" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="238" y="125" font-size="12">HT · TC</text>
<line x1="70" y1="90" x2="70" y2="148" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="78" y="125" font-size="12">IDLE</text>
<rect x="10" y="150" width="290" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="155" y="170" font-size="13" text-anchor="middle">ISR — 플래그 지우기 + 통지만 (수 µs)</text>
<text x="155" y="186" font-size="12" text-anchor="middle">FromISR API, portYIELD_FROM_ISR</text>
<line x1="300" y1="172" x2="378" y2="172" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="339" y="164" font-size="12" text-anchor="middle">notify</text>
<rect x="380" y="150" width="290" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="525" y="170" font-size="13" text-anchor="middle">처리 태스크 — 파싱 · 복사 · 제어</text>
<text x="525" y="186" font-size="12" text-anchor="middle">invalidate → 읽기 → 소유권 반납</text>
<line x1="525" y1="150" x2="455" y2="102" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="532" y="120" font-size="12">읽기 (DMA는</text>
<text x="532" y="136" font-size="12">다른 절반을 쓰는 중)</text>
<text x="340" y="230" font-size="13" text-anchor="middle">규칙 하나: 한 바이트는 언제나 한 주인만 만진다 — DMA 아니면 CPU</text>
<text x="340" y="254" font-size="12" text-anchor="middle">주인이 바뀌는 순간 = 완료 이벤트(HT · TC · IDLE · TX 완료). 그 순간에 캐시 유지 연산이 낀다</text>
<text x="340" y="278" font-size="12" text-anchor="middle">SSD 펌웨어의 "완료 엔트리를 본 다음에만 버퍼를 만진다"와 같은 규칙</text>
</svg>
```
그림 1 — MCU + RTOS의 DMA I/O 경로. 장치 → DMA → RAM 원형 버퍼, 완료 이벤트(HT · TC · IDLE)는 ISR을 거쳐 태스크로. 데이터 소유권은 이벤트 순간에만 넘어간다.

같은 UART 수신을 세 가지로 할 수 있고, 이 노트의 측정(§7)이 셋을 숫자로 비교한다.

| 방식 | 인터럽트 횟수 | 프레임 끝을 언제 아나 | 어울리는 곳 |
|---|---|---|---|
| 바이트마다 RX IRQ | 바이트당 1회 | 즉시(바이트 단위) | 낮은 속도(≤ 115200), 명령 콘솔 |
| 원형 DMA + HT/TC | 절반당 1회 | 버퍼 절반이 찰 때 — 마지막 프레임은 갇힌다 | 끊김 없는 고정 속도 스트림(오디오, ADC) |
| 원형 DMA + HT/TC + IDLE | 절반당 1회 + 프레임당 1회 | 프레임 끝 + 1 바이트 시간 | 가변 길이 패킷(GNSS, MAVLink, CRSF, 콘솔) |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) + FreeRTOS V11.3.2에는 **DMA 컨트롤러가 없다.** 그래서 두 갈래로 실습한다. ① **진짜 DMA**: QEMU `mps2-an505`(Cortex-M33)에는 ARM PrimeCell **PL081** DMA 컨트롤러 4개가 모델링되어 있다(아래 예제 1에서 PeriphID로 확인) — 여기서 bare-metal과 FreeRTOS(ARM_CM33_NTZ 포트, secure 전용)로 메모리 → 메모리 전송과 TC 인터럽트를 진짜로 돌린다. 단 QEMU의 PL081은 **주변장치 흐름 제어(UART가 바이트마다 요청)를 구현하지 않고**(`pl080_run`의 flow ≥ 4는 "not implemented", soft request 레지스터도 미구현), 전송은 레지스터 쓰기 한 번 안에서 즉시 끝난다. ② **에뮬레이션**: 원형 RX DMA · HT/TC · UART IDLE은 AN385에서 **TIMER0 ISR이 1 바이트 시간마다 "하드웨어 한 걸음"을 흉내 내는 방식**으로 만들었다(`emu.h`, §2.3). 이 부분은 진짜 DMA가 아니라는 것을 예제마다 "(에뮬레이션)"으로 표시한다. Zephyr v4.4.2 API는 `native_sim/native/64`의 `zephyr,uart-emul`과 `zephyr,dma-emul`로 돌린다. 캐시 효과는 QEMU가 모델링하지 않으므로(B8 §7) VM 호스트에서 **C로 짠 캐시 라인 시뮬레이터**로 보인다. STM32H7식 HAL 코드는 Cortex-M7로 **빌드만 함**. 타이밍은 FreeRTOS · bare-metal 모두 `ICOUNT=1`(명령 1개 = 32 ns, 결정적)이고, **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클 미모델링). AN385 시계는 FPGAIO COUNTER(25 MHz, 1 카운트 = 40 ns), AN505 시계는 SysTick(CPU 20 MHz). 모든 빌드는 `-Wall -Wextra` 경고 0개다.

AN505 빌드 스크립트는 B7의 다중 코어 하네스(`mc.sh`)를 복사해 외부 IRQ 96개짜리 벡터 테이블을 붙였다.

```sh
# m33/mk.sh <out_dir> <src...>  — Cortex-M33 / QEMU mps2-an505, RUN=1이면 실행(icount shift=5), FR=1이면 FreeRTOS 포함
arm-none-eabi-gcc -mthumb -mcpu=cortex-m33 -mfloat-abi=soft -O2 -g -std=gnu11 -Wall -Wextra -ffreestanding \
  -ffunction-sections -fdata-sections -I$H $CFL "$@" $EXTRA $H/start.c $H/semi.c \
  -nostdlib -T $out/mem.ld -Wl,--gc-sections -lgcc -o $out/app.elf
timeout --foreground 20 qemu-system-arm -machine mps2-an505 -cpu cortex-m33 -nographic -monitor none -serial null \
  -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -kernel $out/app.elf
```

---

## 1. DMA와 RTOS 사이의 계약 — 소유권, 완료 문맥, 컴파일러

### 1.1 직관 — 택배 보관함

DMA 버퍼는 택배 보관함 칸이다. 기사(DMA)가 칸을 채우는 동안 고객(CPU)은 그 칸을 열면 안 되고, 기사가 "넣었음" 문자(완료 인터럽트)를 보낸 뒤에야 연다. 고객이 다 꺼내고 "비웠음"을 알려야 기사가 그 칸에 다시 넣는다. 문자를 받는 사람이 고객 본인(태스크)이 아니라 경비실(ISR)이면, 경비실은 **문자 내용을 고객에게 전달만** 하고 상자를 직접 뜯지 않는다.

정의로 바꾸면 세 가지 약속이다.

- **소유권(ownership)**: 버퍼의 각 바이트는 어느 순간 DMA 아니면 CPU, 한 주인에게만 속한다. 주인이 바뀌는 순간은 "DMA 시작"(CPU → DMA)과 "완료 이벤트"(DMA → CPU) 두 개뿐이다. EL H9의 streaming DMA `dma_map`/`dma_unmap`이 이 순간을 함수로 만든 것이고, MCU에서는 그 함수가 없으니 **펌웨어가 직접 지킨다**.
- **완료 문맥(callback context)**: 완료 콜백은 대개 **DMA IRQ 핸들러 안**에서 불린다(STM32 HAL의 `HAL_..._CpltCallback`, Zephyr `dma_stm32.c`의 `dma_stm32_irq_handler` → `stream->dma_callback`). 그러니 콜백 안에서는 E1의 ISR 규칙이 그대로 적용된다 — 블로킹 금지, `FromISR` API만, 짧게.
- **컴파일러에게도 알린다**: 컴파일러는 DMA가 메모리를 바꾼다는 것을 모른다. 완료를 확인한 **뒤에** 버퍼를 읽도록 컴파일러 배리어가 필요하다(예제 2).

### 1.2 예제 1 — 진짜 DMA 하나: QEMU AN505의 PL081로 M2M 전송 + TC 인터럽트

무엇을 확인하나: QEMU `mps2-an505`에 진짜 DMA 컨트롤러(PL081)가 있는지, TC 인터럽트가 어느 IRQ로 오는지, 그리고 linked-list 두 항목(절반 A, 절반 B)이 둘 다 끝나도 인터럽트 플래그가 "횟수"를 세지 않는다는 것.

먼저 레지스터 헤더다. 주소는 QEMU `hw/arm/mps2-tz.c`의 AN505 표(`dma0` = 0x40110000, IRQ DMACINTR 58 · INTERR 56 · INTTC 57)를 따랐고, CPU가 secure 상태로 부팅하므로 secure 별칭 0x5011_0000을 쓴다. 레지스터 오프셋은 PL080/PL081 TRM의 것이다(자세한 레지스터 설명은 Bare-Metal BM I8 몫).

```c
/* pl081.h — ARM PrimeCell PL081 DMAC (QEMU mps2-an505 DMA0, secure alias 0x50110000) */
#ifndef PL081_H
#define PL081_H
#include <stdint.h>
#define DMA0          0x50110000u
#define DMAC(o)       (*(volatile uint32_t *)(DMA0 + (o)))
#define INTTCSTAT     DMAC(0x004)
#define INTTCCLR      DMAC(0x008)
#define RAWTCSTAT     DMAC(0x014)
#define ENBLDCHNS     DMAC(0x01C)
#define DMACCONFIG    DMAC(0x030)      /* bit0 E: controller enable */
#define DMACSYNC      DMAC(0x034)
/* QEMU 10.2.1 quirk: 채널 Configuration 쓰기 직후엔 IRQ 선이 갱신되지 않는다.
   전역 레지스터를 한 번 쓰면 pl080_update()가 돌아 INTTC가 올라간다. 실칩에는 필요 없음. */
#define QEMU_PL08X_IRQ_SYNC() (DMACSYNC = 0)
#define CH_SRC(n)     DMAC(0x100 + 0x20 * (n))
#define CH_DST(n)     DMAC(0x104 + 0x20 * (n))
#define CH_LLI(n)     DMAC(0x108 + 0x20 * (n))
#define CH_CTRL(n)    DMAC(0x10C + 0x20 * (n))
#define CH_CFG(n)     DMAC(0x110 + 0x20 * (n))
/* CnControl: [11:0] size(items) [20:18] SWidth [23:21] DWidth [26] SI [27] DI [31] I(TC irq) */
#define CTRL_W32      ((2u << 18) | (2u << 21))
#define CTRL_SI       (1u << 26)
#define CTRL_DI       (1u << 27)
#define CTRL_I        (1u << 31)
/* CnConfiguration: [0] E [13:11] flow (0 = M2M) [15] ITC (TC irq mask) [17] A(active) */
#define CFG_E         (1u << 0)
#define CFG_ITC       (1u << 15)
#define DMA0_TC_IRQ   57u              /* AN505: DMA0 INTERR=56, INTTC=57, combined INTR=58 */
struct lli { uint32_t src, dst, next, ctrl; };   /* PL08x linked-list item, 16 B */
#define NVIC_ISER(n)  (*(volatile uint32_t *)(0xE000E100u + 4u * (n)))
#define NVIC_IPR8(n)  (*(volatile uint8_t  *)(0xE000E400u + (n)))
static inline void nvic_on(uint32_t irq, uint8_t prio) { NVIC_IPR8(irq) = prio; NVIC_ISER(irq >> 5) = 1u << (irq & 31); }
static inline uint32_t ipsr(void) { uint32_t r; __asm volatile("mrs %0, ipsr" : "=r"(r)); return r; }
#endif
```

`QEMU_PL08X_IRQ_SYNC()`는 이 랩에서 직접 찾은 QEMU 동작이다. 처음에는 TC 상태 레지스터가 1인데도 인터럽트가 오지 않아 WFI에서 멈췄다. `-d trace:nvic_set_irq_level`로 보니 채널 설정 쓰기 뒤에는 IRQ 56~58이 0으로만 갱신되었고, `DMACCONFIG`나 `DMACSync` 같은 **전역 레지스터를 한 번 더 쓰자** `NVIC external irq 73 level set to 1`(73 = 16 + 57)이 찍혔다. 실칩에서는 필요 없는, 에뮬레이터 우회 코드다.

```c
/* ex1: QEMU mps2-an505의 진짜 PL081 DMA — M2M 한 번, 그리고 2-item linked list(핑퐁 디스크립터) */
#include "semi.h"
#include "../pl081.h"
static uint32_t src[64], dst[64];
static struct lli item_b;                       /* 두 번째 절반을 위한 디스크립터 */
static volatile uint32_t irq_count, irq_tcstat, irq_ipsr;
void IRQ57_Handler(void)                         /* DMA0 INTTC */
{
    irq_tcstat = INTTCSTAT;                      /* 채널별 TC 비트 (카운터가 아니다) */
    INTTCCLR = irq_tcstat;
    irq_ipsr = ipsr();
    irq_count++;
}
static unsigned check(unsigned from, unsigned n)
{
    unsigned bad = 0;
    for (unsigned i = from; i < from + n; i++) bad += dst[i] != src[i];
    return bad;
}
int main(void)
{
    semi_printf("PL081 PeriphID = %x %x %x %x\n", DMAC(0xFE0), DMAC(0xFE4), DMAC(0xFE8), DMAC(0xFEC));
    for (unsigned i = 0; i < 64; i++) src[i] = 0xA5000000u | i;
    nvic_on(DMA0_TC_IRQ, 0x40);
    DMACCONFIG = 1;                              /* controller enable */
    /* 1) 단일 M2M 64워드, TC 인터럽트 */
    CH_SRC(0) = (uint32_t)src; CH_DST(0) = (uint32_t)dst; CH_LLI(0) = 0;
    CH_CTRL(0) = 64 | CTRL_W32 | CTRL_SI | CTRL_DI | CTRL_I;
    CH_CFG(0) = CFG_E | CFG_ITC;                 /* flow=0: memory-to-memory, 지금 시작 */
    QEMU_PL08X_IRQ_SYNC();
    unsigned bad_now = check(0, 64);             /* WFI 전에 이미 끝났나? (QEMU는 즉시 복사) */
    while (irq_count == 0) __asm volatile("wfi");
    semi_printf("single: bad_before_wfi=%u irq=%u tcstat=0x%x IPSR=%u (=16+%u) enbld=0x%x\n",
                bad_now, irq_count, irq_tcstat, irq_ipsr, irq_ipsr - 16, ENBLDCHNS);
    /* 2) 두 절반을 LLI 두 개로: A(0..31) → B(32..63), 둘 다 I비트 */
    for (unsigned i = 0; i < 64; i++) { dst[i] = 0; src[i] = 0x5A000000u | i; }
    item_b = (struct lli){ (uint32_t)&src[32], (uint32_t)&dst[32], 0, 32 | CTRL_W32 | CTRL_SI | CTRL_DI | CTRL_I };
    irq_count = 0;
    CH_SRC(0) = (uint32_t)src; CH_DST(0) = (uint32_t)dst; CH_LLI(0) = (uint32_t)&item_b;
    CH_CTRL(0) = 32 | CTRL_W32 | CTRL_SI | CTRL_DI | CTRL_I;
    CH_CFG(0) = CFG_E | CFG_ITC;
    QEMU_PL08X_IRQ_SYNC();
    while (irq_count == 0) __asm volatile("wfi");
    for (volatile int d = 0; d < 1000; d++) { }
    semi_printf("lli x2: bad A=%u bad B=%u, TC events=2 but irq_count=%u\n",
                check(0, 32), check(32, 32), irq_count);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'RUN=1 /Users/donh/.rtos_scratch/E6/m33/mk.sh /var/tmp/rtos/E6/ex1 /Users/donh/.rtos_scratch/E6/ex1_pl081.c'
```

```text
PL081 PeriphID = 81 10 4 a
single: bad_before_wfi=0 irq=1 tcstat=0x1 IPSR=73 (=16+57) enbld=0x0
lli x2: bad A=0 bad B=0, TC events=2 but irq_count=1
```

출력에서 볼 것: PeriphID `81 10 4 a`는 PL081의 부품 번호(0x081)다 — QEMU AN505에 진짜 DMA 모델이 있다. TC 인터럽트는 IPSR 73 = IRQ 57(INTTC)에서 핸들러 모드로 왔다. `bad_before_wfi=0`은 QEMU가 채널 enable 쓰기 안에서 복사를 끝낸다는 뜻이다(실칩은 버스 속도로 진행). 가장 중요한 줄은 마지막이다. **linked-list 항목 두 개가 모두 완료(TC 사건 2개)됐는데 인터럽트는 1번**이다. TC 상태는 채널별 **비트 하나**이고 카운터가 아니라서, CPU가 첫 사건을 처리하기 전에 두 번째가 오면 둘이 합쳐진다. STM32의 `HTIF`/`TCIF`도 sticky 비트라서 똑같다. 이 사실이 §2의 설계 전부를 좌우한다 — **이벤트 개수를 세지 말고, 위치나 소유권 상태로 판단하라.**

### 1.3 예제 2 — 컴파일러는 DMA를 모른다: 버퍼 읽기가 DMA 시작보다 앞으로 간다

무엇을 확인하나: C에서 한 번도 쓰지 않는 버퍼를 DMA가 채울 때, `-O2` 컴파일러가 그 버퍼 읽기를 DMA 레지스터 쓰기보다 **앞으로** 옮길 수 있다는 것. (예제 1을 디버깅하다 실제로 만난 현상을 최소 코드로 줄였다.)

```c
/* ex2: 컴파일러는 DMA가 버퍼에 쓴다는 것을 모른다 — 읽기가 DMA 시작보다 앞으로 옮겨진다 */
#include "semi.h"
#include "pl081.h"
static uint32_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8}, dst[8];  /* dst: C는 안 쓰고 DMA만 쓴다 */
int main(void)
{
    DMACCONFIG = 1;
    CH_SRC(0) = (uint32_t)src; CH_DST(0) = (uint32_t)dst; CH_LLI(0) = 0;
    CH_CTRL(0) = 8 | CTRL_W32 | CTRL_SI | CTRL_DI;
    CH_CFG(0) = CFG_E;                           /* QEMU: 이 쓰기 안에서 복사가 끝난다 */
#ifdef FIX
    __asm volatile("" ::: "memory");             /* 컴파일러 배리어: 명령 0개 */
#endif
    semi_printf("dst[0]=%u dst[7]=%u\n", dst[0], dst[7]);
    return 0;
}
```

```sh
for v in plain FIX; do F=; [ $v = FIX ] && F=-DFIX
  CFL=$F RUN=1 /Users/donh/.rtos_scratch/E6/m33/mk.sh /var/tmp/rtos/E6/ex2_$v /Users/donh/.rtos_scratch/E6/ex2_hoist.c
  arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/E6/ex2_$v/app.elf | awk '/<main>:/,/^$/'; done
```

```text
--- plain
dst[0]=1 dst[7]=0
100001c8:	ldr	r1, [pc, #44]	@ (100001f8 <main+0x38>)     r1 = &dst (0x38000020)
100001ce:	ldr	r2, [r1, #28]                                  dst[7] 읽기  ← DMA 시작보다 먼저!
100001d0:	str	r5, [r3, #48]	@ 0x30                         DMACCONFIG
100001d2:	str.w	r7, [r3, #256]	@ 0x100                    CH_SRC
...
100001e2:	str.w	r5, [r3, #272]	@ 0x110                    CH_CFG = E (전송 시작)
100001e8:	ldr	r1, [r1, #0]                                   dst[0] 읽기
100001ea:	bl	10000374 <semi_printf>
--- FIX
dst[0]=1 dst[7]=8
100001e0:	str.w	r2, [r3, #272]	@ 0x110                    CH_CFG = E (전송 시작)
100001e4:	ldr	r0, [pc, #28]	@ (10000204 <main+0x44>)
100001e6:	ldr	r2, [r1, #28]                                  dst[7] 읽기 — 이제 시작 뒤
100001e8:	ldr	r1, [r1, #0]
```

(objdump 출력에서 main의 해당 줄만 남기고, 오른쪽 설명은 내가 붙였다.)

출력에서 볼 것: 배리어가 없으면 `dst[7]` 로드(`ldr r2, [r1, #28]`)가 DMA 레지스터 쓰기 **앞**에 있어서 0이 찍힌다. C 표준에서 `volatile` 접근은 다른 `volatile` 접근하고만 순서가 지켜지고, 보통 메모리 읽기는 그 사이로 자유롭게 옮겨질 수 있기 때문이다(D8 §5.4와 같은 이야기). `"memory"` clobber 하나로 고쳐진다.

RTOS에서는 이 문제가 덜 보인다. `xSemaphoreTake`, `ulTaskNotifyTake`, `k_sem_take`는 **컴파일러가 안을 볼 수 없는 외부 함수 호출**이라서 "메모리가 바뀌었을 수 있다"고 가정하게 만든다 — 사실상 컴파일러 배리어다. 위험한 것은 bare-metal식 `while (!done_flag) { }` 스핀 대기 뒤의 읽기, 그리고 인라인된 짧은 헬퍼다. 규칙: **완료 확인과 버퍼 읽기 사이에는 컴파일러 배리어가 있어야 하고, RTOS 대기 함수가 그 역할을 한다. 직접 플래그를 돌면 `__asm volatile("" ::: "memory")`(CMSIS `__COMPILER_BARRIER()`)를 넣는다.** 캐시가 있는 M7에서는 여기에 invalidate가 더해진다(§6).

### 1.4 계약 요약

| 시점 | CPU가 할 일 | 하면 안 되는 일 | RTOS 쪽 |
|---|---|---|---|
| DMA 시작 전 (CPU → DMA) | TX: 데이터 다 쓰고 clean / RX: invalidate(또는 non-cacheable) | 시작 뒤 버퍼에 쓰기 | 버퍼 포인터를 드라이버에 넘김 = 소유권 이전 |
| 전송 중 | 다른 일 · 다른 절반 처리 | 그 버퍼(절반) 읽기 · 쓰기 | 태스크는 블록(통지 대기) |
| 완료 IRQ (ISR) | 플래그 지우기, 위치 기록, 통지 | 파싱 · 긴 복사 · 블로킹 API | `FromISR` + `portYIELD_FROM_ISR` |
| 완료 후 (DMA → CPU) | RX: invalidate → 읽기 / 컴파일러 배리어 | 다음 DMA가 이미 덮었는지 확인 없이 오래 붙잡기 | 처리 뒤 "반납"(소유권 플래그, 빈 버퍼 큐) |

---

## 2. 더블 · 핑퐁 버퍼와 원형 DMA — 누가 어느 절반을 언제 쥐나

> 🔁 이 절은 Bare-Metal BM I8(원형 · 더블 버퍼 사용법, half / full 완료 통지)과 겹친다 (주인: 이 노트).

### 2.1 직관 — 컨베이어 벨트 위 두 쟁반

컨베이어 벨트(DMA)가 쟁반 두 개(절반 A, 절반 B)에 번갈아 물건을 담는다. A가 차면 벨트는 B로 넘어가고 "A 다 찼음" 벨(HT, half-transfer)을 울린다. 직원(태스크)은 A를 비운다. B가 차면 "B 다 찼음" 벨(TC, transfer-complete)이 울리고 벨트는 다시 A로 돌아온다. 직원이 **벨트가 A로 돌아오기 전에** A를 다 비우면 문제없다. 못 비우면 벨트가 직원 손 위에 물건을 쏟는다 — 이것이 **overrun**이다.

용어 정리:

- **원형(circular) DMA**: 전송 길이 N을 다 채우면 주소와 카운터가 자동으로 처음으로 돌아가 멈추지 않고 계속 도는 모드(STM32 `CIRC` 비트, Zephyr `dma_config.cyclic`).
- **HT / TC**: 절반 지점과 끝 지점에서 서는 sticky 플래그(STM32 `HTIF`/`TCIF`). 둘 다 "카운터"가 아니라 "비트"다(예제 1에서 확인).
- **NDTR**: 남은 전송 수(STM32 `DMA_SxNDTR`, 다른 칩은 `CNDTR`, `BTCNT` 등). DMA의 현재 쓰기 위치 = N − NDTR. 이 노트에서 "위치"라고 하면 이것이다.
- **더블 버퍼(double-buffer)**: 버퍼 둘을 번갈아 쓰는 일반 개념. 구현 방법은 셋이 있다(아래 표).

| 형태 | 하드웨어 | CPU가 받는 신호 | 특징 · 주의 |
|---|---|---|---|
| 원형 버퍼 하나를 반으로 | STM32 DMA `CIRC`, 대부분의 MCU DMA | HT = 앞 절반 완료, TC = 뒤 절반 완료 + 처음으로 | 가장 흔하다. 절반 크기 = 처리 단위 |
| 하드웨어 더블 버퍼 모드 | STM32 F4/F7/H7 DMA의 `DBM` 모드(주소 레지스터 `M0AR`/`M1AR`, 현재 대상 `CT` 비트) | 버퍼가 바뀔 때마다 TC, `CT`로 지금 DMA가 쓰는 쪽을 앎 | 쓰지 않는 쪽 주소만 바꿀 수 있다 → 버퍼 풀에서 새 버퍼를 끼워 **zero-copy 회전** 가능 |
| linked-list 디스크립터 순환 | PL08x LLI(예제 1), STM32 GPDMA/LPDMA linked-list, NXP eDMA scatter-gather | 항목마다 TC(설정에 따라) | 디스크립터 자체도 DMA가 읽는 메모리 → M7이면 clean 대상. 레지스터 세부는 BM I8 |

### 2.2 소유권 타임라인

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">정상: CPU가 절반 시간 안에 처리를 끝낸다</text>
<text x="10" y="53" font-size="13">DMA</text>
<text x="10" y="83" font-size="13">CPU</text>
<rect x="70" y="36" width="150" height="24" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="145" y="53" font-size="12" text-anchor="middle">A 쓰기</text>
<rect x="220" y="36" width="150" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="295" y="53" font-size="12" text-anchor="middle">B 쓰기</text>
<rect x="370" y="36" width="150" height="24" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="445" y="53" font-size="12" text-anchor="middle">A 쓰기</text>
<rect x="520" y="36" width="150" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="595" y="53" font-size="12" text-anchor="middle">B 쓰기</text>
<rect x="226" y="66" width="90" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="271" y="83" font-size="12" text-anchor="middle">A 처리</text>
<rect x="376" y="66" width="90" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="421" y="83" font-size="12" text-anchor="middle">B 처리</text>
<rect x="526" y="66" width="90" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="571" y="83" font-size="12" text-anchor="middle">A 처리</text>
<line x1="220" y1="32" x2="220" y2="100" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="370" y1="32" x2="370" y2="100" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="520" y1="32" x2="520" y2="100" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="220" y="114" font-size="12" text-anchor="middle">HT</text>
<text x="370" y="114" font-size="12" text-anchor="middle">TC</text>
<text x="520" y="114" font-size="12" text-anchor="middle">HT</text>
<text x="10" y="150" font-size="13">느림: A 처리가 DMA의 다음 A 쓰기와 겹친다 → overrun</text>
<text x="10" y="183" font-size="13">DMA</text>
<text x="10" y="213" font-size="13">CPU</text>
<rect x="70" y="166" width="150" height="24" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="145" y="183" font-size="12" text-anchor="middle">A 쓰기</text>
<rect x="220" y="166" width="150" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="295" y="183" font-size="12" text-anchor="middle">B 쓰기</text>
<rect x="370" y="166" width="150" height="24" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="445" y="183" font-size="12" text-anchor="middle">A 쓰기</text>
<rect x="520" y="166" width="150" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="595" y="183" font-size="12" text-anchor="middle">B 쓰기</text>
<rect x="226" y="196" width="144" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="298" y="213" font-size="12" text-anchor="middle">A 처리 (느림)</text>
<rect x="370" y="196" width="80" height="24" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="410" y="213" font-size="12" text-anchor="middle">겹침</text>
<line x1="220" y1="162" x2="220" y2="230" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="370" y1="162" x2="370" y2="230" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="220" y="244" font-size="12" text-anchor="middle">HT</text>
<text x="370" y="244" font-size="12" text-anchor="middle">TC: A가 아직 CPU 것 → overrun++</text>
<text x="340" y="276" font-size="12" text-anchor="middle">HT 순간: A의 주인 = CPU, DMA는 B로. TC 순간: B의 주인 = CPU, DMA는 A로</text>
<text x="340" y="294" font-size="12" text-anchor="middle">그 순간 반대쪽 절반을 CPU가 아직 쥐고 있으면 DMA가 그 위에 쓰기 시작한다</text>
</svg>
```
그림 2 — 원형 DMA의 소유권 타임라인. 위: 처리 시간 < 절반 시간이면 안전. 아래: 처리 시간이 절반 시간을 넘으면 TC 순간 DMA가 아직 CPU가 쥔 A로 들어가 덮어쓴다(빨간 겹침).

조건을 식으로 쓰면 이렇다. 절반 시간 T_half = (N/2) × (바이트당 비트 수) / baud. 115200 baud, 8N1(10비트), N = 64면 T_half = 32 × 10 / 115200 = 2.78 ms. 처리 태스크의 **최악 응답 시간**(ISR → 태스크가 깨어나기까지 + 처리 + 더 높은 우선순위 태스크의 선점, N 모듈의 RTA로 계산)이 T_half보다 짧아야 한다. 짧지 않으면 버퍼를 키우거나(N), 처리 태스크 우선순위를 올리거나, 데이터를 먼저 복사해 내고 소유권을 바로 반납한다(§7).

### 2.3 에뮬레이션 하드웨어 — `emu.h`

QEMU AN385에는 DMA가 없고, AN505의 PL081은 UART가 요청하는 주변장치 흐름 제어를 지원하지 않는다. 그래서 "UART + 원형 DMA"를 소프트웨어로 만들었다. **TIMER0 ISR(우선순위 0, 커널 밖 — FreeRTOS API를 부르지 않는다)이 1 바이트 시간마다 한 번 돌며** 바이트를 버퍼에 쓰고(DMA 모드) 또는 RDR에 넣고(바이트 IRQ 모드), HT · TC · IDLE · RXNE 플래그를 세운 뒤 펌웨어 IRQ를 NVIC 펜딩 레지스터(`NVIC_ISPR0`)로 올린다. 펌웨어 IRQ 번호는 AN385 템플릿 벡터 테이블에 이미 이름이 있는 줄을 빌렸다(IRQ 10 = "DMA", IRQ 0 = "UART"). 트래픽은 결정적인 스크립트다 — 프레임 40개, 길이 8~64 B(마지막은 `'\n'`), 프레임 사이 2~30 바이트 시간 쉼, 총 1421 B.

```c
/* emu.h — UART + 원형 DMA "하드웨어" 에뮬레이션 (QEMU mps2-an385에는 DMA 컨트롤러가 없다).
 * TIMER0 ISR(우선순위 0, 커널 밖) = 1 바이트 시간마다 도는 하드웨어. 펌웨어 IRQ는 NVIC 펜딩으로 올린다. */
#ifndef EMU_H
#define EMU_H
#include "board.h"
#define CYC()        REG32(0x40028018u)          /* FPGAIO COUNTER, 25 MHz: 1 count = 40 ns */
#define NVIC_ISPR0   REG32(0xE000E200u)
#define EMU_DMA_IRQ  10u                          /* "DMA1_Stream5" 대역: DUALTIMER 벡터를 빌린다 */
#define EMU_UART_IRQ 0u                           /* "USART2" 대역: UART0RX 벡터를 빌린다 */
#define EMU_DMA_IRQHandler  DUALTIMER_IRQHandler
#define EMU_UART_IRQHandler UART0RX_IRQHandler
enum { F_HT = 1, F_TC = 2, F_IDLE = 4, F_RXNE = 8, F_ORE = 16 };   /* sticky 플래그 (카운터 아님) */
struct emu {
    uint8_t *buf; uint32_t n;                     /* DMA 원형 버퍼 */
    volatile uint32_t pos;                        /* DMA 쓰기 위치 → NDTR = n - pos */
    volatile uint32_t flags, ie;                  /* 상태 · 인터럽트 enable (F_*) */
    volatile uint8_t rdr;                         /* 바이트 IRQ 모드의 데이터 레지스터 */
    int dma;                                      /* 1 = 바이트를 DMA가 buf에 쓴다, 0 = RDR + RXNE */
    uint32_t nframes, k, i, len, gap, idle_armed; /* 트래픽 스크립트 상태 */
    volatile uint32_t done, bytes, lost;
    volatile uint32_t *probe; uint32_t probe_at_done, us_at_done;   /* 측정용: 끝난 순간의 값 */
    uint32_t t_end[64];                           /* 프레임 k의 마지막 바이트 시각 (CYC) */
};
static struct emu E;
static inline uint32_t emu_ndtr(void) { return E.n - E.pos; }
static inline uint32_t frame_len(uint32_t k) { return 8u + (k * 37u) % 57u; }      /* 8..64 B */
static inline uint32_t frame_gap(uint32_t k) { return 2u + (k * 13u) % 29u; }      /* 2..30 바이트 시간 */
static inline uint8_t  frame_byte(uint32_t k, uint32_t i, uint32_t len)
{ return i == len - 1 ? '\n' : (uint8_t)('A' + (k + i) % 26u); }
static inline void emu_pend(uint32_t irq) { NVIC_ISPR0 = 1u << irq; }
static inline void emu_raise(uint32_t f, uint32_t irq) { E.flags |= f; if (E.ie & f) emu_pend(irq); }
void TIMER0_IRQHandler(void)                      /* 1 바이트 시간 = 하드웨어 한 걸음 */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (E.k >= E.nframes) {                       /* 스크립트 끝: 마지막 IDLE을 내고 멈춤 */
        if (E.idle_armed) { E.idle_armed = 0; emu_raise(F_IDLE, EMU_UART_IRQ); }
        else if (!E.done) { E.done = 1; E.us_at_done = board_now_us(); if (E.probe) E.probe_at_done = *E.probe; }
        return;
    }
    if (E.gap) { E.gap--; if (E.idle_armed) { E.idle_armed = 0; emu_raise(F_IDLE, EMU_UART_IRQ); } return; }
    uint8_t b = frame_byte(E.k, E.i, E.len);
    E.bytes++; E.idle_armed = 1;
    if (E.dma) {                                  /* DMA가 메모리에 쓴다 — CPU 개입 없음 */
        E.buf[E.pos] = b;
        if (++E.pos == E.n / 2) emu_raise(F_HT, EMU_DMA_IRQ);
        if (E.pos == E.n) { E.pos = 0; emu_raise(F_TC, EMU_DMA_IRQ); }   /* 원형: 처음으로 */
    } else {
        if (E.flags & F_RXNE) { E.flags |= F_ORE; E.lost++; }          /* 전 바이트를 아직 안 읽음 */
        else { E.rdr = b; emu_raise(F_RXNE, EMU_UART_IRQ); }
    }
    if (++E.i == E.len) { E.t_end[E.k & 63] = CYC(); E.k++; E.i = 0; E.len = frame_len(E.k); E.gap = frame_gap(E.k); }
}
static void emu_start(uint8_t *buf, uint32_t n, int dma, uint32_t ie, uint32_t nframes, uint32_t byte_cyc)
{
    E.ie = ie; E.buf = buf; E.n = n; E.dma = dma; E.nframes = nframes; E.len = frame_len(0); E.gap = 4;
    NVIC_IPR(EMU_DMA_IRQ) = 0x40; NVIC_IPR(EMU_UART_IRQ) = 0x40;      /* 펌웨어 IRQ: 커널 인지 */
    NVIC_ISER0 = (1u << EMU_DMA_IRQ) | (1u << EMU_UART_IRQ);
    TIMER_CTRL(TIMER0_BASE) = 0; TIMER_RELOAD(TIMER0_BASE) = byte_cyc - 1; TIMER_VALUE(TIMER0_BASE) = byte_cyc - 1;
    NVIC_IPR(8) = 0x00; NVIC_ISER0 = 1u << 8;                          /* 하드웨어: 최고 우선순위 */
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
}
#define BAUD_CYC(baud) (25000000u / ((baud) / 10u))   /* 10비트(8N1) 한 바이트 = TIMER0 카운트 */
#endif
```

IDLE은 STM32 규칙을 흉내 냈다: 바이트를 하나라도 받은 뒤 **한 바이트 시간 동안 아무것도 안 오면** 한 번 선다(`idle_armed`). 태스크 쪽 공통 코드 `fw.h`는 받은 바이트를 줄 단위로 모아 스크립트와 비교하고(맞으면 good, 틀리면 bad, 앞 프레임이 사라졌으면 다음 후보로 재동기), 프레임 마지막 바이트가 "도착한" 시각부터 태스크가 그 프레임을 완성한 시각까지를 지연으로 잰다.

```c
/* fw.h — 수신 태스크 쪽 공통: 프레임 조립 · 검증 · 지연 측정 (emu.h의 트래픽 스크립트와 비교) */
#ifndef FW_H
#define FW_H
#include "emu.h"
static uint8_t line[96]; static uint32_t ln, exp_k;
static volatile uint32_t good, bad, lat_max, lat_sum;
static uint32_t match(uint32_t k)
{
    if (ln != frame_len(k)) return 0;
    for (uint32_t i = 0; i < ln; i++) if (line[i] != frame_byte(k, i, ln)) return 0;
    return 1;
}
static void frame_done(void)
{
    uint32_t now = CYC();
    for (uint32_t k = exp_k; k < exp_k + 4 && k < E.nframes; k++)
        if (match(k)) {                           /* 앞 프레임이 사라졌으면 다음 후보로 재동기 */
            uint32_t lat = now - E.t_end[k & 63];
            lat_sum += lat; if (lat > lat_max) lat_max = lat;
            good++; exp_k = k + 1; ln = 0; return;
        }
    bad++; ln = 0;
}
static inline void feed(uint8_t b) { if (ln < sizeof line) line[ln++] = b; if (b == '\n') frame_done(); }
static inline void emu_clear(uint32_t f)          /* W1C 레지스터 흉내: 하드웨어 ISR과 경쟁하지 않게 */
{ __asm volatile("cpsid i" ::: "memory"); E.flags &= ~f; __asm volatile("cpsie i" ::: "memory"); }
static void report(const char *tag)
{
    board_printf_raw("%s: frames sent=%u good=%u bad=%u bytes=%u lat_avg=%u us lat_max=%u us\n", tag,
                     E.nframes, good, bad, E.bytes, good ? lat_sum / good / 25u : 0u, lat_max / 25u);
}
#endif
```

`emu_clear`가 `cpsid i`로 감싼 이유: 실제 STM32의 플래그 클리어 레지스터(`DMA_LIFCR`, `USART_ICR`)는 **쓰기-1-클리어(W1C)** 라서 원자적이다. 소프트웨어 흉내는 읽기-수정-쓰기라서, 그 사이에 더 높은 우선순위의 "하드웨어" ISR이 새 플래그를 세우면 지워질 수 있다(C7의 RMW 레이스). 실칩 코드에서는 이 감싸기가 필요 없다.

### 2.4 HT/TC를 태스크에 어떻게 알리나 — 통지 수단별로 잃는 정보

| 수단 | ISR 쪽 코드 | 태스크가 아는 것 | 잃는 것 | 어울리는 곳 |
|---|---|---|---|---|
| notification 비트 (`eSetBits`) | `xTaskNotifyFromISR(h, HALF_A, eSetBits, &w)` | 준비된 절반들의 집합 | **순서**(A와 B 비트가 같이 서 있으면 어느 쪽이 먼저?), 같은 비트의 **반복 횟수** | 처리가 절대 한 바퀴 늦지 않는 단순 핑퐁 |
| notification 카운트 + 위치 레지스터 | `vTaskNotifyGiveFromISR(h, &w)`, 태스크가 NDTR을 읽음 | 이벤트가 있었다는 사실, 진짜 위치 | 이벤트 종류(필요 없다 — 위치가 진실) | idle-line 수신(§3), 원형 RX 전반 |
| 큐에 `{포인터, 길이, 순번}` | `xQueueSendFromISR(q, &desc, &w)` | 순서 · 횟수 · 내용 모두 | 큐가 차면 ISR이 버려야 한다(D4 §5 정책) | 하드웨어 더블 버퍼(DBM) 버퍼 풀 회전, 블록 단위 ADC · 오디오 |
| stream buffer (ISR에서 복사) | `xStreamBufferSendFromISR` | 바이트 순서 | ISR 안의 복사 시간 | 작은 데이터, copy-out을 ISR에서 끝낼 때(D5 §4) |

소유권 관점에서 큐 방식이 가장 정직하다. 버퍼 포인터가 큐를 따라 이동하는 것이 곧 소유권 이동이고(D4 §3 "빈 버퍼 큐 + 찬 버퍼 큐"), 태스크가 처리 후 빈 버퍼 큐로 돌려보내는 것이 반납이다. 원형 버퍼 하나를 쓰는 HT/TC 방식에서는 포인터 대신 **"어느 절반"** 을 넘기고, 반납은 아래 예제처럼 플래그로 한다.

### 2.5 예제 3 — 원형 DMA + HT/TC → `eSetBits` 통지, 그리고 overrun 감지 (에뮬레이션)

무엇을 확인하나: HT/TC ISR이 `xTaskNotifyAndQueryFromISR(eSetBits)`로 "어느 절반"을 알리고, 소유권 플래그 `cpu_owns[]`로 overrun을 감지하는 핑퐁 수신기. 태스크가 버퍼 안에서 직접(zero-copy) 천천히 읽게 하는 `WORK_US`를 0, 2000, 4000 µs로 바꿔 돌린다.

```c
/* ex4: 원형 DMA + HT/TC → xTaskNotifyFromISR(eSetBits, 어느 절반) → 태스크가 그 절반을 처리 (에뮬레이션) */
#include "FreeRTOS.h"
#include "task.h"
#include "fw.h"
#define N 64u
#define HALF_A 1u
#define HALF_B 2u
#ifndef WORK_US
#define WORK_US 0                                 /* 절반 하나 처리에 드는 추가 일(us). 절반 = 32 B ≈ 2.8 ms */
#endif
static uint8_t rxbuf[N] __attribute__((aligned(32)));
static TaskHandle_t h_rx;
static volatile uint8_t cpu_owns[2];              /* 1 = 그 절반은 지금 CPU 것 (DMA가 다시 쓰면 안 됨) */
static volatile uint32_t overrun, events, merged, same_bit;
void EMU_DMA_IRQHandler(void)
{
    BaseType_t w = pdFALSE;
    uint32_t f = E.flags & (F_HT | F_TC), bits = 0;
    emu_clear(f);
    if (f & F_HT) { if (cpu_owns[1]) overrun++; cpu_owns[0] = 1; bits |= HALF_A; }  /* DMA가 B로 들어감 */
    if (f & F_TC) { if (cpu_owns[0]) overrun++; cpu_owns[1] = 1; bits |= HALF_B; }  /* DMA가 A로 돌아감 */
    uint32_t prev;
    events++;
    xTaskNotifyAndQueryFromISR(h_rx, bits, eSetBits, &prev, &w);
    if (prev & bits) same_bit++;                  /* 같은 비트가 아직 안 읽힘 → 사건 하나가 사라졌다 */
    portYIELD_FROM_ISR(w);
}
static void process(uint32_t h)                  /* zero-copy: 버퍼 안에서 직접, 천천히 읽는다 */
{
    for (uint32_t i = h * (N / 2); i < (h + 1) * (N / 2); i++) {
#if WORK_US
        uint32_t t0 = board_now_us(); while (board_now_us() - t0 < WORK_US / (N / 2)) { }  /* 바이트당 일 */
#endif
        feed(rxbuf[i]);
    }
    cpu_owns[h] = 0;                              /* 소유권 반납 */
}
static void rx_task(void *a)
{
    (void)a;
    uint32_t bits, wake = 0;
    emu_start(rxbuf, N, 1, F_HT | F_TC, 40, BAUD_CYC(115200));
    while (!E.done) {
        if (xTaskNotifyWait(0, ~0u, &bits, pdMS_TO_TICKS(20)) != pdTRUE) continue;
        wake++;
        if ((bits & (HALF_A | HALF_B)) == (HALF_A | HALF_B)) merged++;   /* 둘 다 → 순서를 모른다 */
        if (bits & HALF_A) process(0);
        if (bits & HALF_B) process(1);
    }
    report("pingpong");
    board_printf_raw("WORK_US=%u: DMA events=%u wakeups=%u both-bits=%u same-bit-lost=%u overrun=%u tail_left_in_buf=%u B\n",
                     WORK_US, events, wake, merged, same_bit, overrun, E.pos % (N / 2));
    board_exit(0);
}
int main(void)
{
    xTaskCreate(rx_task, "rx", 512, NULL, 3, &h_rx);
    vTaskStartScheduler();
    return 1;
}
```

```sh
for w in 0 2000 4000; do
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/E6/an385/ex4_pingpong.c \
       OUT=/var/tmp/rtos/E6/ex4_$w EXTRA_CFLAGS=-DWORK_US=$w
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E6/ex4_$w/app.elf 30; done
```

```text
pingpong: frames sent=40 good=39 bad=0 bytes=1421 lat_avg=2734 us lat_max=5393 us
WORK_US=0: DMA events=44 wakeups=44 both-bits=0 same-bit-lost=0 overrun=0 tail_left_in_buf=13 B
[board_exit] code=0
pingpong: frames sent=40 good=39 bad=0 bytes=1421 lat_avg=3758 us lat_max=5580 us
WORK_US=2000: DMA events=44 wakeups=44 both-bits=0 same-bit-lost=0 overrun=0 tail_left_in_buf=13 B
[board_exit] code=0
pingpong: frames sent=40 good=35 bad=4 bytes=1421 lat_avg=6347 us lat_max=8873 us
WORK_US=4000: DMA events=44 wakeups=43 both-bits=0 same-bit-lost=0 overrun=41 tail_left_in_buf=13 B
[board_exit] code=0
```

출력에서 볼 것 — 네 가지.

1. **마지막 프레임이 갇힌다.** 세 경우 모두 good이 39 이하이고 `tail_left_in_buf=13 B`다. 1421 = 44 × 32 + 13이라서 마지막 13 B(마지막 프레임의 끝)는 절반을 채우지 못해 HT도 TC도 오지 않았다. 고정 속도 스트림(ADC, I2S 오디오)이면 문제없지만 가변 길이 패킷에는 치명적이다 — §3의 IDLE이 답이다.
2. **지연이 크다.** `WORK_US=0`에서도 프레임 끝 → 태스크 완성까지 평균 2734 µs, 최대 5393 µs다. 프레임이 끝나도 절반이 찰 때까지 기다리기 때문이다(선로가 쉬지 않으면 절반 시간 2.78 ms, 프레임 사이 쉼이 끼면 더 길다). 지연이 처리 속도가 아니라 **버퍼 크기**에 묶인다.
3. **처리 시간이 절반 시간(2.78 ms)을 넘으면 overrun.** `WORK_US=2000`(절반 시간의 72 %)은 지연만 늘고 깨짐이 없다. `WORK_US=4000`은 HT/TC 44번 중 41번이 "반대쪽 절반을 CPU가 아직 쥔 채" 일어났고(overrun=41), 그중 실제로 데이터가 덮여 깨진 프레임은 4개다. 경보가 깨짐보다 훨씬 많은 이유: 소유권 플래그는 "DMA가 그 절반에 **들어가기 시작했다**"를 잡을 뿐, CPU가 덮인 바이트를 이미 읽고 지나갔는지는 모른다. 그래서 이 경보는 **보수적인 조기 경보**로 쓰고, 실제 손상 판정은 프레임 CRC · 순번으로 한다.
4. **비트 통지의 한계.** `wakeups=43`은 마지막 이벤트가 루프 종료 뒤에 와서 소비되지 않은 것이다. 이 실행에서는 `both-bits`와 `same-bit-lost`가 0이었지만, 태스크가 한 바퀴(5.6 ms) 이상 멈추면 같은 비트가 두 번 서서 사건이 사라지고(`xTaskNotifyAndQueryFromISR`의 이전 값으로 잡는다), A와 B가 같이 서 있으면 처리 순서를 알 수 없다. 이 둘을 피하려면 순번을 큐로 넘기거나, 아예 §3처럼 위치를 진실로 삼는다.

### 2.6 overrun을 감지하는 방법들

- **소유권 플래그**(예제 3): ISR이 HT/TC 순간 반대쪽 절반이 아직 CPU 것인지 본다. 싸고 즉시 알 수 있지만 보수적이다.
- **바퀴(lap) 카운터**: ISR이 TC마다 `lap++`, DMA가 쓴 총 바이트 = lap × N + (N − NDTR). 태스크는 자기가 읽은 총 바이트와 비교해서 차이가 N을 넘으면 그만큼 잃었다. 단 lap과 NDTR을 따로 읽으면 그 사이에 wrap이 일어날 수 있다 — lap 읽기 → NDTR 읽기 → lap 다시 읽기, 다르면 재시도(D8 §4의 seqlock 발상). 그리고 TC가 하드웨어에서는 섰지만 ISR이 아직 안 돌았으면 lap이 늦다 — TC 플래그(`TCIF`)를 같이 읽어 보정한다.
- **하드웨어 오류 플래그**: DMA가 아예 못 따라가면 UART `ORE`(overrun error)가 서고, DMA FIFO error(`FEIF`) · transfer error(`TEIF`)도 있다. 이 레지스터 세부는 BM I8.
- **내용 검사**: 프로토콜의 CRC · 순번(MAVLink `seq`, CRSF CRC8). 최종 판정은 항상 이것으로 한다.

고치는 방법은 셋이다. ① 버퍼를 키운다 — T_half가 처리 태스크의 최악 응답 시간보다 길어지게. ② 처리 태스크 우선순위를 올리거나 처리를 쪼갠다. ③ **복사해 내고 바로 반납한다**(copy-out) — 소유권을 쥐는 시간을 memcpy 시간으로 줄인다(§7에서 비용 비교).

### 2.7 함정

- HT/TC 인터럽트를 "횟수"로 믿는다 — 플래그는 비트다(예제 1). ISR이 늦으면 둘이 합쳐진다.
- STM32 HAL에서 `HAL_UART_RxHalfCpltCallback`/`HAL_UART_RxCpltCallback`를 쓰면서 콜백 안에서 파싱까지 한다 — 콜백은 DMA ISR 문맥이다(E1). 통지만 한다.
- 절반 크기를 "적당히" 정한다 — T_half와 처리 태스크의 최악 응답 시간을 비교해서 정한다(N 모듈).
- 원형 모드에서 TC 콜백이 "전송 끝"이라고 믿고 DMA를 다시 시작한다 — 원형 모드는 멈추지 않는다. 다시 시작하면 위치가 0으로 리셋되어 데이터가 어긋난다.

---

## 3. UART idle-line + 원형 DMA — 가변 길이 프레임 받기

### 3.1 직관 — "말이 끊기면 문장이 끝난 것"

사람 말을 받아 적을 때 "종이 반 장이 찰 때까지" 기다렸다가 읽으면 마지막 문장이 늘 늦는다(예제 3의 2.7 ms 지연, 갇힌 13 B). 대신 **말이 한 박자 끊기면** 그때까지 적은 것을 읽으면 된다. UART의 **IDLE line 검출**이 그 "한 박자"다. STM32 USART는 바이트를 하나라도 받은 뒤 RX 선이 **한 프레임 시간(1 바이트 시간) 동안 high로 쉬면** `IDLE` 플래그를 세우고 인터럽트를 낼 수 있다(`IDLEIE`). 많은 MCU에 같은 기능이 있다(NXP LPUART의 `STAT[IDLE]` 등). 하드웨어 IDLE 검출이 없는 UART에서는 드라이버가 "마지막 바이트 뒤 일정 시간"을 타이머로 재서 흉내 낸다 — Zephyr async API의 `timeout` 인자가 그 일반화다.

규칙은 하나다. **이벤트(HT · TC · IDLE)는 "새 데이터가 있을지 모른다"는 힌트일 뿐이고, 진실은 DMA의 위치(N − NDTR)다.** 세 이벤트가 같은 함수를 부르고, 그 함수는 지난번 위치 `last`부터 지금 위치 `pos`까지를 처리한다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="22" font-size="13">경우 1: pos > last — 한 조각 [last, pos)</text>
<rect x="40" y="40" width="600" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="134" y="40" width="159" height="28" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="340" y1="36" x2="340" y2="72" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="213" y="59" font-size="12" text-anchor="middle">새 바이트 17개</text>
<text x="134" y="88" font-size="12" text-anchor="middle">last=10</text>
<text x="293" y="88" font-size="12" text-anchor="middle">pos=27</text>
<text x="340" y="104" font-size="12" text-anchor="middle">N/2 (HT)</text>
<text x="40" y="88" font-size="12" text-anchor="middle">0</text>
<text x="640" y="88" font-size="12" text-anchor="middle">N</text>
<text x="10" y="138" font-size="13">경우 2: pos &lt; last — DMA가 끝을 넘어 처음으로 돌았다 → 두 조각</text>
<rect x="40" y="156" width="600" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="509" y="156" width="131" height="28" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="40" y="156" width="75" height="28" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="340" y1="152" x2="340" y2="188" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="574" y="175" font-size="12" text-anchor="middle">① [last, N)</text>
<text x="77" y="175" font-size="12" text-anchor="middle">② [0, pos)</text>
<text x="509" y="204" font-size="12" text-anchor="middle">last=50</text>
<text x="115" y="204" font-size="12" text-anchor="middle">pos=8</text>
<path d="M 640 196 C 660 240, 20 240, 40 196" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="340" y="250" font-size="12" text-anchor="middle">TC: 원형 DMA가 0으로 돌아감</text>
<text x="340" y="272" font-size="13" text-anchor="middle">새 바이트 수 = (pos − last + N) mod N,   pos = N − NDTR (NDTR = N이면 pos = 0)</text>
</svg>
```
그림 3 — idle-line 수신의 위치 계산. 이벤트가 무엇이든 [last, pos)를 처리하고 last = pos. wrap이면 끝 조각과 처음 조각 둘로 나눈다(N = 64 예).

알고리즘을 코드로 쓰면 이게 전부다(예제 4의 핵심 6줄).

```c
uint32_t pos = N - ndtr;                 /* DMA가 다음에 쓸 자리 */
if (pos == N) pos = 0;                   /* 막 reload된 순간 */
if (pos == last) return;                 /* 새 데이터 없음 (HT 뒤 IDLE처럼 겹친 이벤트) */
if (pos > last) consume(&buf[last], pos - last);
else { consume(&buf[last], N - last); consume(&buf[0], pos); }
last = pos;
```

STM32에서 알아 둘 것(레지스터 세부는 BM I8 · 칩 RM):

- IDLE 플래그는 W1C가 아니라 칩 세대마다 지우는 방법이 다르다 — F1/F4는 SR 읽기 후 DR 읽기 순서로, F7/H7/G4 등 새 USART는 `ICR`의 `IDLECF`에 1을 쓴다. 안 지우면 IRQ가 계속 들어온다(인터럽트 폭주, E4).
- ST HAL은 이 패턴을 `HAL_UARTEx_ReceiveToIdle_DMA()`로 묶어 준다. 원형 DMA로 열면 HT · TC · IDLE **세 경우 모두** `HAL_UARTEx_RxEventCallback(huart, Size)`를 부르고, 이때 `Size`는 "이번에 받은 길이"가 아니라 **버퍼 안의 현재 위치**다(원형 모드). 그래서 콜백 안에서 `Size - last`를 직접 계산해야 한다 — 이걸 "길이"로 읽는 버그가 흔하다. 최근 HAL에는 어떤 이벤트였는지 묻는 `HAL_UARTEx_GetRxEventType()`도 있다.
- 콜백은 USART/DMA IRQ 문맥이다. 위 6줄을 ISR에서 할지 태스크에서 할지는 선택이다. 이 노트는 ISR에서 통지만 하고 **위치 계산과 처리를 태스크에서** 한다 — ISR이 짧아지고, 겹친 알림(HT 직후 IDLE)이 공짜로 합쳐진다.

### 3.2 예제 4 — IDLE + HT/TC → `vTaskNotifyGiveFromISR`, 태스크가 NDTR로 처리 (에뮬레이션)

무엇을 확인하나: 같은 트래픽을 idle-line 방식으로 받으면 마지막 프레임까지 다 오고 지연이 "한 바이트 시간 + α"로 줄어드는지, 그리고 wrap(두 조각)이 실제로 얼마나 자주 일어나는지. 버퍼 64 B와 256 B로 돌린다.

```c
/* ex5: 원형 DMA + HT/TC + UART IDLE → 태스크가 NDTR로 "새 바이트 = pos - last_pos (wrap 포함)"를 처리 (에뮬레이션) */
#include "FreeRTOS.h"
#include "task.h"
#include "fw.h"
#ifndef N
#define N 64u
#endif
static uint8_t rxbuf[N] __attribute__((aligned(32)));
static TaskHandle_t h_rx;
static volatile uint32_t n_irq_dma, n_irq_idle;
static uint32_t wraps_split;                       /* 두 조각(끝 + 처음)으로 나뉜 처리 횟수 */
void EMU_DMA_IRQHandler(void)                      /* HT / TC: "버퍼가 반 / 다 찼다" */
{
    BaseType_t w = pdFALSE;
    emu_clear(E.flags & (F_HT | F_TC)); n_irq_dma++;
    vTaskNotifyGiveFromISR(h_rx, &w);              /* 사건은 힌트일 뿐 — 진실은 NDTR */
    portYIELD_FROM_ISR(w);
}
void EMU_UART_IRQHandler(void)                     /* IDLE: "한 바이트 시간 동안 조용했다" = 프레임 끝 */
{
    BaseType_t w = pdFALSE;
    emu_clear(F_IDLE); n_irq_idle++;
    vTaskNotifyGiveFromISR(h_rx, &w);
    portYIELD_FROM_ISR(w);
}
static void consume(const uint8_t *p, uint32_t len) { while (len--) feed(*p++); }
static void rx_task(void *a)
{
    (void)a;
    uint32_t last = 0, wake = 0;
    emu_start(rxbuf, N, 1, F_HT | F_TC | F_IDLE, 40, BAUD_CYC(115200));
    while (!E.done || last != N - emu_ndtr()) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)) == 0) continue;
        wake++;
        uint32_t pos = N - emu_ndtr();             /* STM32: N - DMA_SxNDTR */
        if (pos == N) pos = 0;
        if (pos == last) continue;                 /* 이미 처리함 (HT 뒤 IDLE 등 겹친 알림) */
        if (pos > last) consume(&rxbuf[last], pos - last);
        else { consume(&rxbuf[last], N - last); consume(&rxbuf[0], pos); wraps_split++; }
        last = pos;
    }
    report("idle-line");
    board_printf_raw("N=%u: irq dma=%u idle=%u, wakeups=%u, wrap-split=%u\n",
                     N, n_irq_dma, n_irq_idle, wake, wraps_split);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(rx_task, "rx", 512, NULL, 3, &h_rx);
    vTaskStartScheduler();
    return 1;
}
```

```sh
for n in 64 256; do
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/E6/an385/ex5_idle.c \
       OUT=/var/tmp/rtos/E6/ex5_$n EXTRA_CFLAGS=-DN=${n}u
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E6/ex5_$n/app.elf 30; done
```

```text
idle-line: frames sent=40 good=40 bad=0 bytes=1421 lat_avg=97 us lat_max=107 us
N=64: irq dma=44 idle=40, wakeups=84, wrap-split=22
[board_exit] code=0
idle-line: frames sent=40 good=40 bad=0 bytes=1421 lat_avg=106 us lat_max=120 us
N=256: irq dma=11 idle=40, wakeups=51, wrap-split=5
[board_exit] code=0
```

출력에서 볼 것: 40개 프레임이 **전부** 왔다(예제 3은 39개). 프레임 끝 → 태스크 완성 지연은 평균 97 µs인데, 그중 86.8 µs는 "한 바이트 시간 동안 조용한지" 확인하는 IDLE 검출 자체의 시간이고 나머지 약 10 µs가 IRQ + 통지 + 컨텍스트 스위치 + 처리다. 버퍼 크기와 무관하다(256 B에서도 106 µs) — 지연이 버퍼가 아니라 **선로 시간**에 묶였다. 버퍼를 키우면 DMA IRQ가 44 → 11로 줄고, wrap 처리(두 조각)도 22 → 5로 준다. 깨움 수 84 = 44 + 40, 즉 알림이 합쳐지지 않았다 — 115200 baud에서는 태스크가 늘 다음 이벤트 전에 끝나기 때문이다. 합쳐져도 위치로 처리하므로 상관없다.

256 B 버퍼에서 lat_avg가 9 µs 늘어난 것은 wrap이 적은 대신 한 번에 처리하는 조각이 길어서다(조각 길이만큼 `feed` 호출). "버퍼가 크면 무조건 좋다"가 아니라 **IRQ 수 대 한 번의 처리 길이** 사이의 교환이다.

### 3.3 idle-line 방식의 함정 세 가지

- **IDLE은 "프레임 끝"이 아니라 "조용함"이다.** 송신 측이 프레임 중간에 한 바이트 시간 이상 쉬면(USB-UART 브리지, 다른 태스크에 밀린 송신기) 프레임이 두 조각으로 온다. 그러니 수신 쪽은 IDLE을 **"지금까지 온 것을 파서에 밀어 넣어라"** 로만 쓰고, 프레임 경계는 프로토콜(`'\n'`, 길이 필드, SLIP/COBS)로 판단한다. 예제 4의 `feed()`가 그렇다.
- **위치만 보면 한 바퀴 이상의 손실은 안 보인다.** (pos − last) mod N은 정확히 N개 늦었을 때 0이 된다. lap 카운터(§2.6)나 프로토콜 순번이 필요하다.
- **HT/TC를 끄면 안 된다.** "IDLE만 있으면 되지 않나?" — 쉬지 않고 N바이트 넘게 이어지는 스트림(펌웨어 업로드, 고속 텔레메트리)에서는 IDLE이 오지 않으니 HT/TC가 유일한 이벤트다. 셋 다 켠다.

### 3.4 예제 5 — Zephyr async UART API: 버퍼 두 개 교대 (native_sim + `zephyr,uart-emul`)

Zephyr에는 이 모든 것을 드라이버가 해 주는 **async UART API**(`CONFIG_UART_ASYNC_API`)가 있다. 앱은 콜백 하나를 등록하고 버퍼를 주면, 드라이버가 이벤트로 알려 준다. v4.4.2 `include/zephyr/drivers/uart.h`의 `enum uart_event_type`(grep으로 확인):

| 이벤트 | 뜻 | 앱이 할 일 |
|---|---|---|
| `UART_RX_BUF_REQUEST` | "새 버퍼로 받기 시작했다, 다음 버퍼를 미리 달라" | **즉시** `uart_rx_buf_rsp(dev, buf, len)` — 안 주면 현재 버퍼가 차는 순간 수신이 멈춘다 |
| `UART_RX_RDY` | `buf[offset .. offset+len)`에 새 데이터. timeout(=idle) · 버퍼 가득 · disable 때 | 그 구간을 파서로 (콜백 문맥이므로 짧게 → 큐/통지) |
| `UART_RX_BUF_RELEASED` | 드라이버가 이 버퍼에서 손을 뗐다 | 이제 앱 소유 — 재사용 가능 |
| `UART_RX_DISABLED` | 수신 정지(다시 `uart_rx_enable` 가능) | 재시작 결정 |
| `UART_RX_STOPPED` | 오류(overrun, framing 등)로 정지, 이유 포함 | 오류 기록 · 재시작 |
| `UART_TX_DONE` / `UART_TX_ABORTED` | `uart_tx()` 버퍼 전송 완료 / 중단 | TX 버퍼 반납 |

`uart_rx_enable(dev, buf, len, timeout)`의 `timeout`은 **µs 단위 비활동 시간**이고(헤더 주석: 바이트를 하나 이상 받은 뒤 이만큼 조용하면 `UART_RX_RDY`를 낸다, 단위 µs, `SYS_FOREVER_US`면 끔), 이것이 idle-line의 Zephyr판이다. 예제 3·4의 "절반"과 달리 Zephyr는 **버퍼 두 개를 교대**하는 모델이다(하드웨어 더블 버퍼 모드와 같은 생각).

무엇을 확인하나: 32 B 버퍼 두 개로 길이 12, 15, 23 B 문장을 차례로 넣었을 때 이벤트가 어떤 순서로 오고, 버퍼 경계를 넘는 문장이 어떻게 두 `RX_RDY`로 나뉘는지. 콜백이 어느 문맥에서 도는지도 찍는다.

```c
/* Z1: Zephyr async UART API — 버퍼 두 개 교대(UART_RX_BUF_REQUEST/RDY/RELEASED), native_sim + uart-emul */
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/serial/uart_emul.h>
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(euart0));
static uint8_t bufs[2][32];
static int next;                                   /* 다음에 내줄 버퍼 */
static const char *ev_name[] = { "TX_DONE", "TX_ABORTED", "RX_RDY", "RX_BUF_REQUEST",
                                 "RX_BUF_RELEASED", "RX_DISABLED", "RX_STOPPED" };
static void cb(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(user);
    switch (evt->type) {
    case UART_RX_BUF_REQUEST:                      /* "다음 버퍼 줘" — 지금 바로 줘야 끊김이 없다 */
        printk("  %-15s -> give buf%d   (isr=%d)\n", ev_name[evt->type], next, k_is_in_isr());
        uart_rx_buf_rsp(dev, bufs[next], sizeof bufs[0]);
        next ^= 1;
        break;
    case UART_RX_RDY:                              /* 새 바이트: buf[offset .. offset+len) */
    {
        const uint8_t *p = evt->data.rx.buf + evt->data.rx.offset;
        int n = (int)evt->data.rx.len, nl = n > 0 && p[n - 1] == '\n';
        printk("  %-15s buf%d off=%2u len=%2u \"%.*s%s\"\n", ev_name[evt->type], evt->data.rx.buf == bufs[1],
               (unsigned)evt->data.rx.offset, (unsigned)evt->data.rx.len, n - nl, p, nl ? "\\n" : "");
    }
        break;
    case UART_RX_BUF_RELEASED:                     /* 드라이버가 손을 뗐다 → 앱 소유 */
        printk("  %-15s buf%d\n", ev_name[evt->type], evt->data.rx_buf.buf == bufs[1]);
        break;
    default:
        printk("  %s\n", ev_name[evt->type]);
    }
}
static void send(const char *s)
{
    printk("put %u B\n", (unsigned)strlen(s));
    uart_emul_put_rx_data(uart, (const uint8_t *)s, strlen(s));
    k_msleep(5);                                   /* 5 ms 조용함 > timeout 1 ms → RX_RDY */
}
int main(void)
{
    uart_callback_set(uart, cb, NULL);
    uart_rx_enable(uart, bufs[0], sizeof bufs[0], 1000);   /* timeout 1000 us = idle-line 역할 */
    next = 1;
    send("$GPGGA,1*5A\n");                         /* 12 B: buf0 안에서 끝 */
    send("$GPRMC,22,A*1F\n");                      /* 15 B: buf0 남은 20 B 안 */
    send("$IMU,0123,4567,89AB*00\n");              /* 23 B: buf0 끝을 넘어 buf1로 */
    uart_rx_disable(uart);
    k_msleep(5);
    return 0;
}
```

```text
# prj.conf
CONFIG_SERIAL=y
CONFIG_EMUL=y
CONFIG_UART_ASYNC_API=y
CONFIG_PRINTK=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000
# app.overlay: / { euart0: uart-emul { compatible = "zephyr,uart-emul"; status = "okay";
#                  current-speed = <0>; rx-fifo-size = <256>; tx-fifo-size = <256>; }; };
```

```sh
/var/rtoslab/bin/rtos-zbuild native_sim/native/64 /Users/donh/.rtos_scratch/E6/zuart /var/tmp/rtos/E6/zuart
/var/tmp/rtos/E6/zuart/zephyr/zephyr.exe --stop_at=1
```

```text
*** Booting Zephyr OS build v4.4.2 ***
put 12 B
  RX_BUF_REQUEST  -> give buf1   (isr=0)
  RX_RDY          buf0 off= 0 len=12 "$GPGGA,1*5A\n"
put 15 B
  RX_RDY          buf0 off=12 len=15 "$GPRMC,22,A*1F\n"
put 23 B
  RX_RDY          buf0 off=27 len= 5 "$IMU,"
  RX_BUF_RELEASED buf0
  RX_BUF_REQUEST  -> give buf0   (isr=0)
  RX_RDY          buf1 off= 0 len=18 "0123,4567,89AB*00\n"
  RX_BUF_RELEASED buf1
  RX_DISABLED
```

(앞의 `uart connected to pseudotty` 줄과 끝의 `Stopped at 1.000s` 줄은 생략했다.)

출력에서 볼 것: 첫 바이트가 들어오자마자 `RX_BUF_REQUEST`가 와서 buf1을 미리 줬다. 1 ms 조용하면 `RX_RDY`가 `offset`과 `len`으로 **새 구간만** 알려 준다(0+12, 12+15). 세 번째 문장 23 B는 buf0에 남은 5 B를 채우고(`off=27 len=5 "$IMU,"`) 나머지 18 B는 buf1로 갔다 — **문장이 버퍼 경계에서 잘린다.** 그래서 콜백은 바이트를 파서(줄 조립기)에 밀어 넣을 뿐, `RX_RDY` 하나를 "문장 하나"로 다루면 안 된다(§3.3 첫 함정과 같다). 버퍼가 다 차면 `RX_BUF_RELEASED`로 소유권이 앱에 돌아오고, 드라이버는 다시 `RX_BUF_REQUEST`로 다음 버퍼를 달라고 한다 — 두 버퍼가 핑퐁으로 돈다.

`isr=0`에 주의한다. `uart_emul`은 이벤트를 **자기 workqueue 스레드**에서 낸다(`uart_emul.c`의 `uart_emul_async_rx_handler`, `uart_emul_async_rx_timeout_handler`는 `k_work` 핸들러). 진짜 하드웨어 드라이버는 다르고, 한 드라이버 안에서도 섞인다. `drivers/serial/uart_stm32.c`를 읽으면 HT/TC로 인한 `UART_RX_RDY`는 DMA 콜백 `uart_stm32_dma_rx_cb` → 즉 **DMA ISR 문맥**에서 나오고, IDLE로 인한 것은 `uart_stm32_isr`에서 `timeout`이 0이면 그 자리(USART ISR)에서, 0이 아니면 `async_timer_start()`로 건 지연 작업 `uart_stm32_async_rx_timeout`(**시스템 workqueue 스레드**)에서 나온다. 이식 가능한 코드는 콜백 안에서 `k_sem_give`, `k_msgq_put(..., K_NO_WAIT)`, `ring_buf_put` 같은 ISR-안전 API만 쓴다.

### 3.5 Zephyr STM32 드라이버 안에서는 — 예제 4와 같은 계산

`uart_stm32.c`의 `uart_stm32_dma_rx_flush()`를 읽으면 예제 4의 6줄이 그대로 있다(v4.4.2, 발췌 요약).

- `DMA_STATUS_COMPLETE`(TC) → counter = 버퍼 길이, `DMA_STATUS_BLOCK`(HT) → counter = 길이/2, 그 밖(IDLE · 타임아웃) → `dma_get_status()`의 `pending_length`로 `rx_rcv_len = buffer_length - stat.pending_length` — 즉 **N − NDTR**.
- 주석 "If DMA wrapped: emit tail [offset..end), then head [0..counter)." — wrap이면 끝 조각과 처음 조각을 따로 `UART_RX_RDY`로 낸다(그림 3의 경우 2).
- "Already handled by timeout path" 검사로, IDLE이 이미 처리한 구간을 HT가 다시 내지 않게 한다(예제 4의 `pos == last`).
- 그리고 `CONFIG_DCACHE`가 켜진 STM32에서는 `uart_rx_enable`/`uart_tx`가 `stm32_buf_in_nocache()`로 버퍼가 **non-cacheable 영역**에 있는지 검사하고, 아니면 `"Rx buffer should be placed in a nocache memory region"` 로그와 함께 `-EFAULT`를 돌려준다. 즉 Zephyr STM32 UART async는 캐시 유지 연산 대신 **non-cacheable 버퍼를 요구**한다(§6.3).

---

## 4. TX DMA — 완료 통지 + "DMA busy" 게이트키퍼

### 4.1 직관 — 출구가 하나인 우체국 창구

텔레메트리 태스크, 로그 태스크, 커맨드 응답 태스크가 모두 같은 UART로 내보낸다. DMA 채널(창구)은 하나라서 한 번에 한 손님만 받는다. 필요한 것은 둘이다. **게이트**(지금 창구가 비었나 — mutex), **완료 통지**(내 소포가 다 나갔나 — ISR이 주는 세마포어나 notification). 완료를 기다리는 동안 호출자는 블록되고, 그동안 **호출자의 버퍼는 DMA 것**이다. 리턴하는 순간 소유권이 호출자에게 돌아온다 — 이 순간이 API의 계약이다.

다른 설계는 **게이트키퍼 태스크**(E4, D4)다. TX 전용 태스크 하나가 큐에서 `{포인터, 길이}`를 받아 차례로 DMA로 보낸다. 호출자는 큐에 넣고 바로 돌아가므로 블록되지 않지만, 대신 **버퍼를 복사해 넣거나(copy-in) 버퍼 풀로 소유권을 넘겨야** 한다. 아래 예제는 첫 번째(mutex + 블로킹) 방식이다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="30" y="12" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="90" y="32" font-size="13" text-anchor="middle">tel (prio 3)</text>
<rect x="190" y="12" width="120" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="250" y="32" font-size="13" text-anchor="middle">log (prio 1)</text>
<rect x="355" y="12" width="130" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="420" y="32" font-size="13" text-anchor="middle">dma_busy mutex</text>
<rect x="530" y="12" width="120" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="590" y="32" font-size="13" text-anchor="middle">PL081 + ISR</text>
<line x1="90" y1="42" x2="90" y2="330" stroke="currentColor" stroke-dasharray="2 4"/>
<line x1="250" y1="42" x2="250" y2="330" stroke="currentColor" stroke-dasharray="2 4"/>
<line x1="420" y1="42" x2="420" y2="330" stroke="currentColor" stroke-dasharray="2 4"/>
<line x1="590" y1="42" x2="590" y2="330" stroke="currentColor" stroke-dasharray="2 4"/>
<line x1="250" y1="66" x2="418" y2="66" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="335" y="60" font-size="12" text-anchor="middle">take — 획득 (0 µs)</text>
<line x1="90" y1="98" x2="418" y2="98" stroke="#d0564a" marker-end="url(#ah4)"/>
<text x="170" y="92" font-size="12" text-anchor="middle">take — 블록 (997 µs)</text>
<text x="335" y="118" font-size="12" text-anchor="middle">log가 prio 3 상속</text>
<rect x="200" y="128" width="100" height="26" rx="4" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="250" y="146" font-size="12" text-anchor="middle">prep (prio 3)</text>
<line x1="250" y1="172" x2="588" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="420" y="166" font-size="12" text-anchor="middle">start (2969 µs)</text>
<line x1="590" y1="196" x2="252" y2="196" stroke="#3f9a6b" marker-end="url(#ah4)"/>
<text x="420" y="190" font-size="12" text-anchor="middle">TC IRQ 57 → notify give</text>
<line x1="250" y1="222" x2="418" y2="222" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="335" y="216" font-size="12" text-anchor="middle">give</text>
<line x1="420" y1="246" x2="92" y2="246" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="250" y="240" font-size="12" text-anchor="middle">획득 (대기 2004 µs)</text>
<line x1="90" y1="272" x2="588" y2="272" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="420" y="266" font-size="12" text-anchor="middle">start</text>
<line x1="590" y1="298" x2="92" y2="298" stroke="#3f9a6b" marker-end="url(#ah4)"/>
<text x="420" y="292" font-size="12" text-anchor="middle">TC IRQ → notify (3032 µs 완료)</text>
<text x="250" y="324" font-size="12" text-anchor="middle">log 리턴 (3083 µs, prio 1)</text>
</svg>
```
그림 4 — 예제 6의 실제 순서(시각은 실측). 낮은 우선순위 log가 DMA 게이트를 쥔 채 패킷을 조립하는 동안 높은 우선순위 tel이 게이트에서 블록되고, mutex의 우선순위 상속으로 log가 prio 3으로 올라가 빨리 끝낸다.

### 4.2 예제 6 — 진짜 PL081 + FreeRTOS: mutex 게이트 + 완료 통지 (QEMU mps2-an505)

무엇을 확인하나: `dma_tx()` 드라이버 API가 ① mutex로 채널을 독점하고 ② 전송을 시작한 뒤 ③ TC ISR의 `vTaskNotifyGiveFromISR`로 깨어나 ④ 게이트를 놓는 구조가 실제 DMA 인터럽트로 동작하는지. 일부러 낮은 우선순위 태스크가 게이트를 쥔 채 3 ms짜리 패킷 조립을 하게 해서, 높은 우선순위 태스크가 얼마나 기다리고 우선순위 상속(D3)이 걸리는지 본다.

`frt_time.h`는 B7 `ex_pingpong`과 같은 시계다(tick × reload + SysTick 진행분 = CPU 20 MHz 클록 수).

```c
/* frt_time.h — FreeRTOS on M33(QEMU an505): tick × reload + SysTick 진행분 = CPU 클록(20 MHz) 수 (B7과 같은 방식) */
#ifndef FRT_TIME_H
#define FRT_TIME_H
#define SYST_RVR REG32(0xE000E014u)
#define SYST_CVR REG32(0xE000E018u)
static inline uint32_t now_cyc(void)
{
    uint32_t t, v, r = SYST_RVR + 1u;
    do { t = xTaskGetTickCount(); v = SYST_CVR; } while (t != xTaskGetTickCount());
    return t * r + (r - 1u - v);
}
#endif
```

```c
/* ex3: FreeRTOS + 진짜 PL081 — "DMA busy" 게이트키퍼(mutex) + 완료 통지(ISR → vTaskNotifyGiveFromISR) */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "../pl081.h"
#include "frt_time.h"                           /* now_cyc(): 20 MHz CPU 클록 수 */
static SemaphoreHandle_t dma_busy; static StaticSemaphore_t dma_busy_buf;
static TaskHandle_t volatile dma_owner;          /* 완료를 기다리는 태스크 (ISR이 깨운다) */
static volatile uint32_t isr_ipsr; static uint32_t t_origin; static TaskHandle_t h_log;
void IRQ57_Handler(void)                         /* DMA0 INTTC: 커널 인지 IRQ (prio 0x40 ≥ 0x20) */
{
    BaseType_t woken = pdFALSE;
    INTTCCLR = INTTCSTAT;
    isr_ipsr = ipsr();
    vTaskNotifyGiveFromISR(dma_owner, &woken);
    portYIELD_FROM_ISR(woken);
}
/* 드라이버 API: 버퍼를 넘기고, 끝나면 돌려받는다. 리턴 = 소유권이 다시 호출자에게 */
static unsigned dma_tx(void *dst, const void *src, unsigned words, void (*prep)(void))
{
    if (xSemaphoreTake(dma_busy, pdMS_TO_TICKS(50)) != pdTRUE) return 1;     /* 게이트 */
    if (prep) prep();                            /* 예: 패킷 조립 · 캐시 clean 자리 */
    dma_owner = xTaskGetCurrentTaskHandle();
    CH_SRC(0) = (uint32_t)src; CH_DST(0) = (uint32_t)dst; CH_LLI(0) = 0;
    CH_CTRL(0) = words | CTRL_W32 | CTRL_SI | CTRL_DI | CTRL_I;
    CH_CFG(0) = CFG_E | CFG_ITC;
    QEMU_PL08X_IRQ_SYNC();
    uint32_t ok = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));                /* 완료 대기 */
    dma_owner = NULL; xSemaphoreGive(dma_busy);
    return ok ? 0 : 2;     /* 0 = 성공, 1 = 게이트 timeout, 2 = 완료 timeout */
}
static uint32_t pkt_lo[64], pkt_hi[16], uart_fifo_lo[64], uart_fifo_hi[16];
static void slow_prep(void)                      /* 약 3 ms 걸리는 패킷 조립 */
{
    for (volatile uint32_t i = 0; i < 15000; i++) { }
    semi_printf("[%5u us] log : prep end, my prio=%u (inherited?)\n", (now_cyc() - t_origin) / 20,
                (unsigned)uxTaskPriorityGet(NULL));
}
static void t_log(void *a)                       /* prio 1: 큰 로그 패킷 */
{
    (void)a;
    uint32_t c0 = t_origin = now_cyc();
    semi_printf("[%5u us] log : take DMA, prep 3 ms\n", (now_cyc() - c0) / 20);
    unsigned r = dma_tx(uart_fifo_lo, pkt_lo, 64, slow_prep);
    semi_printf("[%5u us] log : done r=%u prio=%u\n", (now_cyc() - c0) / 20, r, (unsigned)uxTaskPriorityGet(NULL));
    vTaskDelay(5);
    semi_exit(0);
}
static void t_telem(void *a)                     /* prio 3: 짧은 텔레메트리, 1 ms 뒤에 깨어남 */
{
    (void)a; vTaskDelay(1);
    uint32_t c0 = t_origin;
    semi_printf("[%5u us] tel : want DMA (log prio=%u)\n", (now_cyc() - c0) / 20,
                (unsigned)uxTaskPriorityGet(h_log));
    uint32_t t0 = now_cyc();
    unsigned r = dma_tx(uart_fifo_hi, pkt_hi, 16, NULL);
    semi_printf("[%5u us] tel : done r=%u, waited %u us, ISR IPSR=%u\n", (now_cyc() - c0) / 20, r,
                (now_cyc() - t0) / 20, isr_ipsr);
    for (;;) vTaskDelay(100);
}
int main(void)
{
    static StackType_t s1[256], s2[256]; static StaticTask_t b1, b2;
    dma_busy = xSemaphoreCreateMutexStatic(&dma_busy_buf);
    nvic_on(DMA0_TC_IRQ, 0x40);
    DMACCONFIG = 1;
    h_log = xTaskCreateStatic(t_log, "log", 256, NULL, 1, s1, &b1);
    xTaskCreateStatic(t_telem, "tel", 256, NULL, 3, s2, &b2);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'FR=1 RUN=1 /Users/donh/.rtos_scratch/E6/m33/mk.sh /var/tmp/rtos/E6/ex3 /Users/donh/.rtos_scratch/E6/ex3_txgate.c'
```

```text
[    0 us] log : take DMA, prep 3 ms
[  997 us] tel : want DMA (log prio=1)
[ 2969 us] log : prep end, my prio=3 (inherited?)
[ 3032 us] tel : done r=0, waited 2004 us, ISR IPSR=73
[ 3083 us] log : done r=0 prio=1
```

출력에서 볼 것: TC 인터럽트가 IPSR 73(IRQ 57)에서 `vTaskNotifyGiveFromISR`로 기다리던 태스크를 깨웠다(r=0, 완료 timeout 없음). 997 µs에 깨어난 tel은 게이트에서 **2004 µs** 막혔다 — log가 게이트를 쥔 채 조립(prep)을 했기 때문이다. 그동안 log의 우선순위가 1 → 3으로 올라갔다(`prep end, my prio=3`) — mutex의 우선순위 상속(D3)이 "낮은 태스크가 게이트를 쥔 채 중간 우선순위 태스크에 밀려 무한정 늦어지는" 상황을 막았다. 그리고 tel이 log보다 먼저 `done`을 찍는다 — log가 게이트를 놓는 순간(`xSemaphoreGive`) 상속이 풀리고 prio 3 tel이 바로 선점했기 때문이다.

교훈은 설계 쪽이다. **게이트를 쥔 채 긴 일(조립 · 포맷팅 · 압축)을 하지 마라.** 조립은 게이트 밖에서 자기 버퍼에 하고, 게이트 안에서는 clean + 시작 + 대기만 한다. 그러면 tel의 대기는 log의 실제 전송 시간(바이트 수 × 바이트 시간)으로 줄어든다. 실칩에서 115200 baud로 256 B를 보내면 22 ms라서, 그것도 길면 게이트키퍼 태스크 + 우선순위 큐로 바꾼다.

### 4.3 TX에서 소유권 · 캐시 · 타임아웃

- **호출자 버퍼를 그대로 DMA에 넘기면(zero-copy)** 리턴 전까지 호출자는 그 버퍼를 건드리면 안 된다. 스택 버퍼를 넘기고 블록하지 않고 리턴하는 API는 스택이 덮인 뒤 DMA가 읽는 버그를 만든다. 비동기 API(Zephyr `uart_tx`)라면 `UART_TX_DONE`까지 버퍼가 살아 있어야 한다.
- **copy-in**: 드라이버가 자기 DMA 버퍼(정렬 · 캐시 처리된 전용 섹션)로 복사하고 호출자 버퍼는 즉시 돌려준다. 작은 텔레메트리 패킷이면 복사 비용이 싸고 캐시 · 정렬 문제를 드라이버 한 곳에 가둔다(§5 예제 7의 `tlm_send`).
- **M7이면 시작 전 clean**(§6). 빠뜨리면 DMA가 캐시에만 있는 새 데이터 대신 RAM의 옛 데이터를 보낸다.
- **완료 대기에 타임아웃**을 둔다. UART CTS 흐름 제어로 막히거나 DMA 오류가 나면 영원히 기다린다. 타임아웃이면 DMA를 멈추고(abort) 게이트를 놓는다 — 놓지 않으면 시스템 전체 출력이 멈춘다.

---

## 5. 드론 FC의 SPI IMU 버스트 — DMA를 쓸까 말까

### 5.1 직관 — 8 kHz마다 15바이트

드론 FC의 rate loop는 IMU(예: ICM-4268x급) 데이터가 준비될 때마다 돈다. IMU가 data-ready(DRDY) 핀으로 EXTI 인터럽트를 내면, 펌웨어는 SPI로 "가속도 X 상위 레지스터부터 연속 읽기" 명령 1바이트 + 데이터 14바이트(가속도 6 + 온도 2 + 자이로 6, 칩마다 배치가 다르다)를 읽는다. 24 MHz SPI면 15 B = 120비트 = 5 µs다.

| 방법 | CPU 시간 | 지연 | 비고 |
|---|---|---|---|
| 폴링(바쁜 대기) 15 B | 약 5 µs + 바이트마다 레지스터 접근 | 가장 짧다 | 8 kHz면 CPU의 4 % 이상을 SPI 대기에 버림 |
| 바이트 IRQ | 15회 IRQ | 짧다 | IRQ 진입 비용 × 15 — 최악 |
| DMA 버스트 | 시작 몇 줄 + 완료 IRQ 1회 | DMA 설정 비용만큼 길다 | CPU는 그 5 µs 동안 다른 일. 8 kHz × 2 IMU면 이득이 크다 |

경험칙: **수십 바이트 이하, 수 µs 안에 끝나는 전송은 DMA 설정 비용(채널 설정 + 캐시 유지 + 완료 IRQ)이 전송보다 비쌀 수 있다.** 그래도 FC에서 IMU를 DMA로 읽는 이유는 rate loop 태스크의 지터를 줄이고 CPU를 PID · 필터 계산에 쓰기 위해서다. 흐름은 `DRDY EXTI ISR → CS low + SPI DMA 시작 → DMA TC ISR → CS high + invalidate + rate 태스크 통지`이고, 태스크는 깨어나서 바로 15 B를 읽는다. 전송이 짧아서 invalidate도 라인 하나(32 B)다.

### 5.2 예제 7 — STM32H7식 HAL 코드 세 가지 (Cortex-M7, 빌드만 함)

무엇을 확인하나: 이 노트의 패턴 셋(GNSS idle-line RX, 텔레메트리 TX 게이트, IMU SPI 버스트)을 STM32 HAL 스타일로 쓴 코드가 Cortex-M7 + FreeRTOS(ARM_CM7 포트) + CMSIS 6 `core_cm7.h`로 경고 없이 컴파일되고, DMA 버퍼 섹션이 32 B 정렬로 나오며, `SCB_InvalidateDCache_by_Addr`가 실제로 `DCIMVAC` 쓰기가 되는지. QEMU에 STM32H7 DMA · 캐시 모델이 없으므로 **빌드만 함**이고, HAL 헤더가 랩에 없어서 쓰는 HAL 함수의 프로토타입만 `hal_stub.h`에 선언했다(실제 HAL 시그니처: `HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t)`, `HAL_UART_Transmit_DMA(UART_HandleTypeDef *, const uint8_t *, uint16_t)`, `HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *, const uint8_t *, uint8_t *, uint16_t)` — 최근 HAL 기준, 옛 버전은 `const`가 없다).

```c
/* fc_dma.c — 드론 FC(STM32H7형, Cortex-M7 + D-cache)의 DMA I/O 세 가지: GNSS RX(idle) · 텔레메트리 TX · IMU SPI 버스트
 * HAL 스타일, 빌드만 함 (QEMU에 STM32H7 DMA · 캐시 모델이 없다) */
#include "hal_stub.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#define DMA_BUF __attribute__((section(".dma_buf"), aligned(32)))  /* 32 B 정렬 + 32의 배수 크기 */
extern UART_HandleTypeDef huart_gnss, huart_tlm; extern SPI_HandleTypeDef hspi_imu;
extern TaskHandle_t h_gnss, h_rate;
/* ① GNSS: 원형 DMA + idle — HAL은 HT · TC · IDLE 모두에서 Size = "버퍼 안 현재 위치"로 부른다 */
static DMA_BUF uint8_t gnss_rx[256];
static volatile uint16_t gnss_pos;
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)   /* ISR 문맥 */
{
    BaseType_t w = pdFALSE;
    if (huart != &huart_gnss) return;
    gnss_pos = Size;
    vTaskNotifyGiveFromISR(h_gnss, &w);
    portYIELD_FROM_ISR(w);
}
void gnss_task_step(uint16_t *last, void (*eat)(const uint8_t *, uint32_t))
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint16_t pos = gnss_pos % sizeof gnss_rx, l = *last;
    if (pos == l) return;
    uint32_t a = l & ~31u, b = (pos > l ? pos : sizeof gnss_rx);           /* 첫 조각: [l, b) */
    SCB_InvalidateDCache_by_Addr(&gnss_rx[a], (int32_t)(((b + 31u) & ~31u) - a));
    eat(&gnss_rx[l], b - l);
    if (pos < l) { SCB_InvalidateDCache_by_Addr(gnss_rx, (int32_t)((pos + 31u) & ~31u)); eat(gnss_rx, pos); }
    *last = pos;
}
/* ② 텔레메트리 TX: busy 게이트(mutex) + 완료 세마포어, 보내기 전 clean */
static DMA_BUF uint8_t tlm_tx[128];
static SemaphoreHandle_t tlm_gate, tlm_done;               /* mutex, binary semaphore */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t w = pdFALSE;
    if (huart == &huart_tlm) { xSemaphoreGiveFromISR(tlm_done, &w); portYIELD_FROM_ISR(w); }
}
int tlm_send(const uint8_t *msg, uint16_t n)
{
    if (n > sizeof tlm_tx || xSemaphoreTake(tlm_gate, pdMS_TO_TICKS(5)) != pdTRUE) return -1;
    for (uint16_t i = 0; i < n; i++) tlm_tx[i] = msg[i];   /* copy-in: 호출자 버퍼는 바로 돌려준다 */
    SCB_CleanDCache_by_Addr(tlm_tx, (int32_t)((n + 31u) & ~31u));
    int r = HAL_UART_Transmit_DMA(&huart_tlm, tlm_tx, n) == HAL_OK
            && xSemaphoreTake(tlm_done, pdMS_TO_TICKS(5)) == pdTRUE ? 0 : -2;
    xSemaphoreGive(tlm_gate);
    return r;
}
/* ③ IMU: data-ready EXTI → SPI DMA 버스트 15 B(레지스터 주소 + accel 6 + temp 2 + gyro 6) → rate 태스크 */
static DMA_BUF uint8_t imu_tx[32] = { 0x80 | 0x1F };       /* read | ACCEL_DATA_X1 (ICM-4268x 형) */
static DMA_BUF uint8_t imu_rx[32];
void imu_drdy_isr(void)                                    /* EXTI ISR에서 호출 */
{
    HAL_GPIO_WritePin_CS(0);
    HAL_SPI_TransmitReceive_DMA(&hspi_imu, imu_tx, imu_rx, 15);
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    BaseType_t w = pdFALSE;
    if (hspi != &hspi_imu) return;
    HAL_GPIO_WritePin_CS(1);
    SCB_InvalidateDCache_by_Addr(imu_rx, sizeof imu_rx);   /* 라인 하나 — ISR에서 해도 싸다 */
    vTaskNotifyGiveFromISR(h_rate, &w);                    /* 1~8 kHz rate loop를 깨운다 */
    portYIELD_FROM_ISR(w);
}
void fc_dma_init(void)
{
    tlm_gate = xSemaphoreCreateMutex(); tlm_done = xSemaphoreCreateBinary();
    SCB_CleanDCache_by_Addr(imu_tx, sizeof imu_tx);        /* 상수 명령 바이트도 한 번 clean */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart_gnss, gnss_rx, sizeof gnss_rx);
}
```

```sh
arm-none-eabi-gcc -mthumb -mcpu=cortex-m7 -mfloat-abi=soft -O2 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections \
  -I. -I/var/rtoslab/templates/freertos-mps2 -I/var/rtoslab/templates/common -I$K/include -I$K/portable/GCC/ARM_CM7/r0p1 \
  -I/var/rtoslab/zephyrproject/modules/hal/cmsis_6/CMSIS/Core/Include -c fc_dma.c -o fc_dma.o && echo BUILD_OK
arm-none-eabi-objdump -h fc_dma.o | grep -E "Idx|dma_buf"
arm-none-eabi-objdump -t fc_dma.o | grep -E "gnss_rx|tlm_tx|imu_rx|imu_tx"
arm-none-eabi-objdump -d --no-show-raw-insn fc_dma.o | awk '/<HAL_SPI_TxRxCpltCallback>:/,/^$/'
```

```text
BUILD_OK
Idx Name          Size      VMA       LMA       File off  Algn
 10 .dma_buf      000001c0  00000000  00000000  00000310  2**5
000000c0 l     O .dma_buf	00000100 gnss_rx
00000040 l     O .dma_buf	00000080 tlm_tx
00000000 l     O .dma_buf	00000020 imu_rx
00000020 l     O .dma_buf	00000020 imu_tx
00000000 <HAL_SPI_TxRxCpltCallback>:
...
  16:	bl	0 <HAL_GPIO_WritePin_CS>
  1a:	ldr	r2, [pc, #64]	@ (5c <HAL_SPI_TxRxCpltCallback+0x5c>)
  1c:	dsb	sy
  20:	ldr	r3, [pc, #60]	@ (60 <HAL_SPI_TxRxCpltCallback+0x60>)
  22:	str.w	r2, [r3, #604]	@ 0x25c
  26:	dsb	sy
  2a:	isb	sy
...
  36:	bl	0 <vTaskGenericNotifyGiveFromISR>
...
  40:	mov.w	r3, #3758153728	@ 0xe000e000
  44:	mov.w	r2, #268435456	@ 0x10000000
  48:	str.w	r2, [r3, #3332]	@ 0xd04
...
  60:	.word	0xe000ed00
```

출력에서 볼 것: `.dma_buf` 섹션 정렬이 `2**5` = 32 B이고 네 버퍼의 오프셋(0x00, 0x20, 0x40, 0xc0)과 크기(0x20, 0x20, 0x80, 0x100)가 모두 32의 배수다 — 어떤 버퍼도 다른 변수와 캐시 라인을 나눠 쓰지 않는다(B8 §6.5의 이웃 파괴 버그 방지). `imu_rx` 32 B 무효화는 루프 없이 `dsb` → `0xE000ED00 + 0x25C = 0xE000EF5C`(**DCIMVAC**, 주소로 라인 하나 invalidate) 쓰기 → `dsb; isb` 네 명령으로 컴파일됐다. `portYIELD_FROM_ISR`은 `0xE000ED04`(ICSR)에 `0x10000000`(PENDSVSET, 비트 28)을 쓰는 것으로 보인다 — PendSV를 걸어 ISR이 끝난 뒤 문맥 전환(B4). 링크하지 않았으므로 `bl 0 <...>`는 아직 풀리지 않은 재배치다.

코드에서 볼 것 두 가지. GNSS 쪽은 invalidate 범위를 **라인 경계로 넓혀서** 매번 읽기 직전에 다시 한다(§6.2에서 왜 매번인지 시뮬레이션으로 본다). CMSIS 6의 `SCB_InvalidateDCache_by_Addr`는 시작 주소가 정렬되지 않아도 `op_size = dsize + (addr & 31)`로 걸친 라인을 모두 처리한다(`m-profile/armv7m_cachel1.h` · `core_starmc1.h`에서 확인) — 즉 버퍼가 정렬돼 있지 않으면 **이웃 변수의 라인까지 지운다**. 그래서 버퍼 정렬이 함수의 정확성보다 먼저다.

---

## 6. Cortex-M7 캐시 처리 — DMA 프로토콜에 끼워 넣기

> 🔁 이 절은 Bare-Metal BM I8(캐시 정리 부분)과 겹친다 (주인: 이 노트). 캐시 원리 · 유지 연산 · MPU 속성 · TCM 자체는 RTOS B8이 주인이다.

캐시 라인 · write-back · 투기적 읽기 · `SCB_CleanDCache_by_Addr`/`SCB_InvalidateDCache_by_Addr`의 의미와 "정렬 안 된 invalidate가 이웃을 지운다" 버그는 **B8 §6**(시뮬레이터 예제 S1 · S2 포함)에서 이미 다뤘다. 여기서는 그 규칙을 **이 노트의 패턴 다섯 개에 어디에 끼우는지**만 정한다.

### 6.1 패턴별 규칙표

| 패턴 | DMA 시작 전 | 완료 이벤트 뒤 (CPU가 읽기 전) | 버퍼 배치 |
|---|---|---|---|
| TX(텔레메트리, 예제 6 · 7) | 데이터 다 쓴 뒤 `SCB_CleanDCache_by_Addr`(라인 올림 길이) | 없음 | 32 B 정렬, 크기 32의 배수 |
| RX 핑퐁 HT/TC(예제 3) | 버퍼 전체 invalidate 한 번(dirty 라인이 DMA 도중 evict되어 덮는 것 방지) | **그 절반만** invalidate → 읽기 | 절반 크기도 32의 배수(절반 경계 = 라인 경계) |
| RX idle-line(예제 4) | 버퍼 전체 invalidate 한 번 | **[last, pos)를 라인으로 넓혀 매번** invalidate → 읽기(§6.2) | 버퍼 시작 · 끝이 라인 경계, 같은 라인에 다른 변수 금지 |
| SPI 버스트(예제 7 IMU) | TX 명령 바이트 clean, RX invalidate | RX 라인 invalidate(ISR에서 라인 하나) | TX · RX 버퍼를 서로 다른 라인에 |
| 디스크립터(linked-list, DBM 주소 교체) | 디스크립터 clean | DMA가 상태를 써 넣는 디스크립터면 invalidate | non-cacheable 섹션이 보통 더 간단 |

RX에서 "CPU는 그 버퍼에 절대 쓰지 않는다"를 지키면 그 라인에는 dirty 데이터가 생기지 않으므로 invalidate는 **clean한 복사본을 버리는 것**일 뿐 안전하다. 문제는 버퍼 끝 라인에 다른 변수가 같이 사는 경우뿐이고(B8 §6.5), 그래서 정렬과 크기 규칙이 붙는다.

### 6.2 idle-line의 "반쯤 찬 라인" — 왜 매번 invalidate하나

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="12">t1: DMA가 [0,12) 씀 → IDLE → invalidate 후 읽기 → 라인 0 전체(0..31)가 캐시로 들어온다</text>
<text x="100" y="47" font-size="12" text-anchor="end">RAM</text>
<rect x="110" y="34" width="180" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="290" y="34" width="300" height="18" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="100" y="69" font-size="12" text-anchor="end">캐시</text>
<rect x="110" y="56" width="180" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="290" y="56" width="300" height="18" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="440" y="69" font-size="12" text-anchor="middle">12..31 = 옛 값 (복사본)</text>
<text x="10" y="102" font-size="12">t2: DMA가 [12,27) 씀 — RAM만 바뀐다. 캐시의 12..26은 이제 낡았다</text>
<text x="100" y="129" font-size="12" text-anchor="end">RAM</text>
<rect x="110" y="116" width="405" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="515" y="116" width="75" height="18" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="100" y="151" font-size="12" text-anchor="end">캐시</text>
<rect x="110" y="138" width="180" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="290" y="138" width="225" height="18" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<rect x="515" y="138" width="75" height="18" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="402" y="151" font-size="12" text-anchor="middle">낡은 12..26</text>
<text x="10" y="186" font-size="12">t3: IDLE → CPU가 [12,27)을 읽는다</text>
<rect x="110" y="198" width="230" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="225" y="215" font-size="12" text-anchor="middle">invalidate 안 함 (또는 HT 때만)</text>
<text x="225" y="231" font-size="12" text-anchor="middle">→ 캐시 hit, 옛 값을 읽는다</text>
<rect x="360" y="198" width="230" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="475" y="215" font-size="12" text-anchor="middle">읽기 직전 [12,27) invalidate</text>
<text x="475" y="231" font-size="12" text-anchor="middle">→ miss, RAM에서 새 값</text>
<text x="340" y="268" font-size="12" text-anchor="middle">한 라인(32 B)을 DMA와 CPU가 "시간차로" 나눠 쓴다 — 조각을 읽을 때마다 그 라인을 다시 버려야 한다</text>
<text x="340" y="288" font-size="12" text-anchor="middle">(CPU는 RX 버퍼에 쓰지 않으므로 dirty가 없고, 버리는 것은 안전하다)</text>
</svg>
```
그림 5 — idle-line 수신에서 생기는 "반쯤 찬 라인". 첫 조각을 읽으면 라인 전체가 캐시에 들어오고, 그 라인의 나머지는 DMA가 나중에 채운다. 다음 조각을 읽기 직전에 다시 invalidate하지 않으면 옛 값을 읽는다.

핑퐁(HT/TC)에서는 절반 경계 = 라인 경계라서 "절반이 끝난 뒤 그 절반 invalidate"가 정확히 맞는다. idle-line은 조각이 라인 중간에서 끝나므로 그 규칙이 깨진다. B8의 시뮬레이터는 TX/RX 일회성 버그를 다뤘고, 아래는 **원형 버퍼 + 조각 읽기**에 특화한 작은 시뮬레이터다.

### 6.3 예제 8 — 원형 RX + idle-line 조각 읽기의 캐시 정책 4가지 (호스트 C 시뮬레이션)

무엇을 확인하나: 128 B 원형 RX 버퍼(라인 4개)를 DMA가 채우고 CPU가 idle-line 조각(3~42 B, 400개)을 읽을 때, 유지 정책별로 틀린 바이트가 몇 개 나오는지. **QEMU는 캐시를 모델링하지 않으므로 이것은 VM 호스트에서 도는 시뮬레이션이다** — 실제 M7 측정이 아니다. 모델: 32 B 라인, write-back, CPU는 RX 버퍼에 쓰지 않음, DMA는 RAM에만 쓴다.

```c
/* cachesim_idle.c — 호스트 시뮬레이션: 원형 RX DMA + idle-line 조각 읽기에서 D-cache 유지 정책 4가지
 * (QEMU는 캐시를 모델링하지 않는다. 32 B 라인, write-back, CPU는 RX 버퍼에 쓰지 않음) */
#include <stdio.h>
#include <stdint.h>
#define N 128u                                   /* RX 원형 버퍼 = 라인 4개 */
#define LINE 32u
static uint8_t ram[N], cline[N / LINE][LINE], valid[N / LINE];
static unsigned fills, inv_ops, bus_reads;
static uint8_t cpu_read(unsigned a, int nocache)
{
    if (nocache) { bus_reads++; return ram[a]; }
    unsigned l = a / LINE;
    if (!valid[l]) { for (unsigned i = 0; i < LINE; i++) cline[l][i] = ram[l * LINE + i]; valid[l] = 1; fills++; }
    return cline[l][a % LINE];
}
static void inv_range(unsigned a, unsigned len)  /* CMSIS 방식: 걸친 라인 전부 */
{
    for (unsigned l = a / LINE; l <= (a + len - 1) / LINE; l++) { valid[l] = 0; inv_ops++; }
}
int main(void)
{
    static const char *name[] = { "P0 no maintenance", "P1 invalidate half at HT/TC only",
                                  "P2 invalidate [last,pos) before each read", "P3 non-cacheable buffer" };
    for (int p = 0; p < 4; p++) {
        unsigned pos = 0, last = 0, wr = 0, rd = 0, wrong = 0, seed = 7;
        fills = inv_ops = bus_reads = 0;
        for (unsigned i = 0; i < N / LINE; i++) valid[i] = 0;
        for (unsigned k = 0; k < 400; k++) {     /* 400 개 조각 (idle-line마다 하나) */
            seed = seed * 1103515245u + 12345u;
            unsigned len = 3 + (seed >> 16) % 40;           /* 3..42 B 프레임 */
            for (unsigned i = 0; i < len; i++) {           /* DMA: RAM에만 쓴다 */
                ram[pos] = (uint8_t)(++wr * 7u);
                pos = (pos + 1) % N;
                if (p == 1 && (pos == N / 2 || pos == 0)) inv_range(pos ? 0 : N / 2, N / 2);  /* HT·TC ISR */
            }
            unsigned n = (pos + N - last) % N;   /* IDLE: 새 바이트 = pos - last (wrap) */
            if (p == 2) { if (last + n <= N) inv_range(last, n); else { inv_range(last, N - last); inv_range(0, n - (N - last)); } }
            for (unsigned i = 0; i < n; i++) {
                uint8_t v = cpu_read((last + i) % N, p == 3);
                if (v != (uint8_t)(++rd * 7u)) wrong++;
            }
            last = pos;
        }
        printf("%-44s wrong=%5u / %u  line_fills=%4u inv_ops=%4u uncached_reads=%5u\n",
               name[p], wrong, rd, fills, inv_ops, bus_reads);
    }
    return 0;
}
```

```sh
gcc -O2 -Wall -Wextra -o /var/tmp/rtos/E6/host/cachesim_idle /Users/donh/.rtos_scratch/E6/host/cachesim_idle.c
/var/tmp/rtos/E6/host/cachesim_idle
```

```text
P0 no maintenance                            wrong= 6568 / 9416  line_fills=   4 inv_ops=   0 uncached_reads=    0
P1 invalidate half at HT/TC only             wrong= 4994 / 9416  line_fills= 297 inv_ops= 294 uncached_reads=    0
P2 invalidate [last,pos) before each read    wrong=    0 / 9416  line_fills= 679 inv_ops= 679 uncached_reads=    0
P3 non-cacheable buffer                      wrong=    0 / 9416  line_fills=    0 inv_ops=    0 uncached_reads= 9416
```

출력에서 볼 것: 유지 연산이 없으면(P0) 처음 채운 라인 4개를 끝까지 쓰므로 70 %가 틀린다. **핑퐁식으로 "HT/TC 때 그 절반을 invalidate"(P1)해도 53 %가 틀린다** — HT 전에 IDLE 조각을 이미 읽어서 라인이 반쯤 찬 채 캐시에 들어갔고, 그 라인의 나머지를 다음 IDLE 조각에서 읽기 때문이다(그림 5). 핑퐁 코드에 IDLE을 "덧붙이기만" 하면 정확히 이 버그가 된다. P2(읽기 직전 [last, pos)를 라인으로 넓혀 invalidate)와 P3(non-cacheable)는 0이다. 비용은 다르다 — P2는 라인 채움 679번(라인당 32 B 버스트 읽기), P3는 바이트마다 버스 읽기 9416번. 바이트 하나씩 파싱하는 UART라면 둘의 차이는 작고 **P3가 실수할 여지가 없어서** 보통 이긴다. Zephyr STM32 UART async가 아예 non-cacheable 버퍼를 요구하는 이유(§3.5)이기도 하다.

### 6.4 세 가지 배치 선택 — 이 노트의 패턴 기준

B8 §6.6의 결정 흐름(TCM 접근 가능? → CPU 계산량? → 기본은 non-cacheable)을 그대로 쓰되, I/O 패턴으로 다시 정리하면:

- **UART RX 링 · 디스크립터 · 작은 제어 패킷 → non-cacheable 섹션.** MPU 영역 하나를 Normal non-cacheable로(B8 §3.3), Zephyr는 `CONFIG_NOCACHE_MEMORY` + `__nocache`(`include/zephyr/linker/section_tags.h`), 또는 DT 메모리 영역에 `DT_MEM_ARM_MPU_RAM_NOCACHE` 속성(`stm32_buf_in_nocache`가 `mem_attr_check_buf`로 검사).
- **큰 블록(카메라 줄, 오디오 프레임, FFT 입력) → cacheable + 유지 연산.** CPU가 데이터를 여러 번 읽고 계산하므로 캐시 hit의 이득이 유지 비용보다 크다. 블록 경계를 라인 경계로 맞춘다.
- **D-cache 없는 DTCM → 그 DMA가 DTCM에 닿을 때만.** STM32F7은 DMA가 DTCM에 닿고, STM32H7의 DMA1/DMA2는 DTCM에 닿지 않는다(MDMA는 닿는다) — B8 §4.2 표. 닿으면 일관성 문제 자체가 사라지고 CPU 접근도 0 wait state라 IMU 버퍼 같은 작은 고빈도 버퍼에 가장 좋다.

---

## 7. zero-copy vs copy-out, 그리고 측정 — 세 수신 방식의 CPU 부하와 지연

### 7.1 zero-copy와 copy-out

- **zero-copy**: 태스크가 DMA 버퍼 안에서 직접 파싱한다. 복사 비용이 없지만 **처리하는 내내 소유권을 쥔다** — 예제 3의 `WORK_US=4000`처럼 처리가 느려지면 바로 overrun이다. 하드웨어 더블 버퍼(DBM)나 버퍼 풀과 같이 쓰면 "찬 버퍼는 큐로 넘기고 DMA에는 새 빈 버퍼를 끼운다"가 되어 소유권 시간이 길어도 안전하다(D4 §3).
- **copy-out**: 이벤트를 받자마자 새 바이트를 태스크 쪽 버퍼(링, stream buffer, 줄 버퍼)로 복사하고 소유권을 즉시 반납한다. 소유권 시간이 memcpy 시간(Cortex-M3 icount 기준 수 µs/수십 B)으로 줄어 overrun 여유가 커지는 대신, 바이트당 복사 비용과 RAM이 든다. 예제 4의 `feed()`가 줄 버퍼로의 copy-out이다.

경험칙: **UART · 작은 패킷은 copy-out, 큰 블록은 버퍼 풀 + zero-copy.** 판단 기준은 "최악 처리 시간 대 절반 시간"이다(§2.2).

### 7.2 예제 9 — 같은 트래픽, 네 가지 수신 방식 (에뮬레이션, QEMU icount 기준)

무엇을 확인하나: 프레임 40개(1421 B)를 ① 바이트마다 RX IRQ + 태스크 통지, ② 원형 DMA + HT/TC, ③ 원형 DMA + HT/TC + IDLE로 받을 때 펌웨어 IRQ 수 · 태스크 깨움 수 · CPU 부하 · 프레임 끝 지연. ⓪은 하드웨어 에뮬레이션만 돌리는 기준선이다(에뮬레이션 TIMER0 ISR 자체도 CPU를 쓰므로 그만큼 빼야 한다). CPU 부하는 최저 우선순위 `spinner` 태스크가 도는 속도로 잰다 — 부하가 없을 때 10 ms 동안의 spin 속도 대비, 전송 구간(첫 바이트 ~ 마지막 바이트 + 1 바이트 시간)의 spin 속도가 얼마나 줄었나. 115200 baud와 1 Mbaud 두 속도로 돌린다.

```c
/* ex6: 같은 트래픽(40 프레임, 1421 B)을 네 방식으로 받는다 — CPU 부하 · IRQ 수 · 깨움 수 · 프레임 끝 지연 (에뮬레이션)
 * MODE 0 = 하드웨어만(기준선), 1 = 바이트마다 RX IRQ, 2 = 원형 DMA + HT/TC, 3 = 원형 DMA + HT/TC + IDLE */
#include "FreeRTOS.h"
#include "task.h"
#include "fw.h"
#define N 64u
static uint8_t rxbuf[N] __attribute__((aligned(32)));
static uint8_t ring[256]; static volatile uint8_t r_head, r_tail;   /* MODE 1: ISR → 태스크 SPSC 링 (D8) */
static TaskHandle_t h_rx;
static volatile uint32_t spins, n_irq;
void EMU_UART_IRQHandler(void)
{
    BaseType_t w = pdFALSE; n_irq++;
    if (E.flags & F_RXNE) { ring[r_head++] = E.rdr; emu_clear(F_RXNE); }   /* RDR 읽기 = RXNE 클리어 */
    if (E.flags & F_IDLE) emu_clear(F_IDLE);
    vTaskNotifyGiveFromISR(h_rx, &w); portYIELD_FROM_ISR(w);
}
void EMU_DMA_IRQHandler(void)
{
    BaseType_t w = pdFALSE; n_irq++;
    emu_clear(E.flags & (F_HT | F_TC));
    vTaskNotifyGiveFromISR(h_rx, &w); portYIELD_FROM_ISR(w);
}
static void spinner(void *a) { (void)a; for (;;) spins++; }         /* 남는 CPU를 세는 최저 우선순위 태스크 */
static void rx_task(void *a)
{
    (void)a;
    static const uint32_t ie[4] = { 0, F_RXNE, F_HT | F_TC, F_HT | F_TC | F_IDLE };
    uint32_t last = 0, wake = 0, s0 = spins, t0 = board_now_us();
    vTaskDelay(pdMS_TO_TICKS(10));                                   /* 부하 없는 spin 속도 보정 */
    uint32_t rate0 = (spins - s0) * 100u / (board_now_us() - t0);   /* spins per 100 us */
    E.probe = &spins; s0 = spins; t0 = board_now_us();
    emu_start(rxbuf, N, MODE != 1, ie[MODE], 40, BAUD_CYC(BAUD));
    while (!E.done) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5)) == 0) continue;
        wake++;
        if (MODE == 1) { while (r_tail != r_head) feed(ring[r_tail++]); continue; }
        uint32_t pos = (N - emu_ndtr()) % N;
        if (pos >= last) for (uint32_t i = last; i < pos; i++) feed(rxbuf[i]);
        else { for (uint32_t i = last; i < N; i++) feed(rxbuf[i]); for (uint32_t i = 0; i < pos; i++) feed(rxbuf[i]); }
        last = pos;
    }
    uint32_t dt = E.us_at_done - t0, rate = (E.probe_at_done - s0) * 100u / dt;   /* 시작 ~ 마지막 바이트 + 1 바이트 */
    report("bench");
    board_printf_raw("MODE=%u BAUD=%u: busy=%u.%u%% fw_irq=%u wakeups=%u lost_bytes=%u elapsed=%u us\n",
                     MODE, BAUD, (1000u - rate * 1000u / rate0) / 10u, (1000u - rate * 1000u / rate0) % 10u,
                     n_irq, wake, E.lost, dt);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(rx_task, "rx", 512, NULL, 3, &h_rx);
    xTaskCreate(spinner, "spin", 128, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
for b in 115200 1000000; do for m in 0 1 2 3; do
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/E6/an385/ex6_bench.c \
       OUT=/var/tmp/rtos/E6/ex6_${m}_$b EXTRA_CFLAGS="-DMODE=$m -DBAUD=$b"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E6/ex6_${m}_$b/app.elf 60 | grep -v board_exit; done; done
```

```text
bench: frames sent=40 good=0 bad=0 bytes=1421 lat_avg=0 us lat_max=0 us
MODE=0 BAUD=115200: busy=1.7% fw_irq=0 wakeups=0 lost_bytes=0 elapsed=177594 us
bench: frames sent=40 good=40 bad=0 bytes=1421 lat_avg=10 us lat_max=12 us
MODE=1 BAUD=115200: busy=17.6% fw_irq=1421 wakeups=1421 lost_bytes=0 elapsed=177594 us
bench: frames sent=40 good=39 bad=0 bytes=1421 lat_avg=2732 us lat_max=5392 us
MODE=2 BAUD=115200: busy=2.7% fw_irq=44 wakeups=44 lost_bytes=0 elapsed=177594 us
bench: frames sent=40 good=40 bad=0 bytes=1421 lat_avg=96 us lat_max=106 us
MODE=3 BAUD=115200: busy=3.0% fw_irq=84 wakeups=84 lost_bytes=0 elapsed=177594 us
bench: frames sent=40 good=0 bad=0 bytes=1421 lat_avg=0 us lat_max=0 us
MODE=0 BAUD=1000000: busy=13.1% fw_irq=0 wakeups=0 lost_bytes=0 elapsed=20462 us
bench: frames sent=40 good=40 bad=0 bytes=1421 lat_avg=9 us lat_max=17 us
MODE=1 BAUD=1000000: busy=76.8% fw_irq=1421 wakeups=1031 lost_bytes=0 elapsed=20462 us
bench: frames sent=40 good=39 bad=0 bytes=1421 lat_avg=323 us lat_max=631 us
MODE=2 BAUD=1000000: busy=22.8% fw_irq=44 wakeups=44 lost_bytes=0 elapsed=20462 us
bench: frames sent=40 good=40 bad=0 bytes=1421 lat_avg=24 us lat_max=30 us
MODE=3 BAUD=1000000: busy=26.1% fw_irq=84 wakeups=84 lost_bytes=0 elapsed=20462 us
```

(MODE 0은 펌웨어가 아무것도 받지 않으므로 good=0이 맞다.)

### 7.3 예제 10 — 측정값을 차트 좌표로 (Python)

무엇을 확인하나: 기준선을 뺀 순부하와 지연을 계산하고, 그림 6의 막대 좌표를 만든다(좌표는 손으로 넣지 않는다).

```python
# chart_bench.py — ex6 측정값(QEMU icount)으로 SVG 막대 차트 좌표를 계산한다
import math
base = {115200: 1.7, 1000000: 13.1}
busy = {115200: [17.6, 2.7, 3.0], 1000000: [76.8, 22.8, 26.1]}     # MODE 1, 2, 3
lat  = {115200: [10, 2732, 96],   1000000: [9, 323, 24]}            # 평균 지연 us
modes = ["바이트 IRQ", "DMA HT/TC", "DMA+IDLE"]
col = {115200: "#4a7bd0", 1000000: "#e08a3c"}
out = []
# 왼쪽 패널: 순 CPU 부하 (선형 0..70 %)
x0, y0, h, w = 60, 250, 190, 250                 # 축 원점, 높이, 폭
for gi, m in enumerate(modes):
    for bi, b in enumerate((115200, 1000000)):
        net = round(busy[b][gi] - base[b], 1)
        bh = net / 70 * h
        x = x0 + 15 + gi * 80 + bi * 28
        out.append(f'<rect x="{x}" y="{y0 - bh:.1f}" width="24" height="{bh:.1f}" fill="{col[b]}" fill-opacity="0.75"/>')
        out.append(f'<text x="{x + 12}" y="{y0 - bh - 4:.1f}" font-size="12" text-anchor="middle">{net}</text>')
# 오른쪽 패널: 평균 지연 (로그 1..10000 us)
x1 = 390
for gi, m in enumerate(modes):
    for bi, b in enumerate((115200, 1000000)):
        v = lat[b][gi]
        bh = math.log10(v) / 4 * h
        x = x1 + 15 + gi * 80 + bi * 28
        out.append(f'<rect x="{x}" y="{y0 - bh:.1f}" width="24" height="{bh:.1f}" fill="{col[b]}" fill-opacity="0.75"/>')
        out.append(f'<text x="{x + 12}" y="{y0 - bh - 4:.1f}" font-size="12" text-anchor="middle">{v}</text>')
print("\n".join(out))
for b in base:
    print(b, "net:", [round(v - base[b], 1) for v in busy[b]])
```

```text
<rect x="75" y="206.8" width="24" height="43.2" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="87" y="202.8" font-size="12" text-anchor="middle">15.9</text>
...
<rect x="593" y="184.4" width="24" height="65.6" fill="#e08a3c" fill-opacity="0.75"/>
<text x="605" y="180.4" font-size="12" text-anchor="middle">24</text>
115200 net: [15.9, 1.0, 1.3]
1000000 net: [63.7, 9.7, 13.0]
```

(좌표 24줄 중 처음 2줄과 마지막 2줄만 남겼다. 전부 그림 6에 들어갔다.)

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<rect x="60" y="8" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="78" y="19" font-size="12">115200 baud</text>
<rect x="190" y="8" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/>
<text x="208" y="19" font-size="12">1 Mbaud</text>
<text x="185" y="44" font-size="13" text-anchor="middle">펌웨어 순 CPU 부하 % (기준선 뺌)</text>
<text x="515" y="44" font-size="13" text-anchor="middle">프레임 끝 → 태스크 지연 µs (로그)</text>
<line x1="60" y1="60" x2="60" y2="250" stroke="currentColor"/>
<line x1="60" y1="250" x2="310" y2="250" stroke="currentColor"/>
<text x="54" y="254" font-size="12" text-anchor="end">0</text>
<text x="54" y="159" font-size="12" text-anchor="end">35</text>
<text x="54" y="64" font-size="12" text-anchor="end">70</text>
<line x1="60" y1="155" x2="310" y2="155" stroke="#888" stroke-dasharray="2 4"/>
<line x1="390" y1="60" x2="390" y2="250" stroke="currentColor"/>
<line x1="390" y1="250" x2="640" y2="250" stroke="currentColor"/>
<text x="384" y="254" font-size="12" text-anchor="end">1</text>
<text x="384" y="206" font-size="12" text-anchor="end">10</text>
<text x="384" y="159" font-size="12" text-anchor="end">100</text>
<text x="384" y="111" font-size="12" text-anchor="end">1000</text>
<text x="384" y="64" font-size="12" text-anchor="end">10000</text>
<line x1="390" y1="202.5" x2="640" y2="202.5" stroke="#888" stroke-dasharray="2 4"/>
<line x1="390" y1="155" x2="640" y2="155" stroke="#888" stroke-dasharray="2 4"/>
<line x1="390" y1="107.5" x2="640" y2="107.5" stroke="#888" stroke-dasharray="2 4"/>
<rect x="75" y="206.8" width="24" height="43.2" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="87" y="202.8" font-size="12" text-anchor="middle">15.9</text>
<rect x="103" y="77.1" width="24" height="172.9" fill="#e08a3c" fill-opacity="0.75"/>
<text x="115" y="73.1" font-size="12" text-anchor="middle">63.7</text>
<rect x="155" y="247.3" width="24" height="2.7" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="167" y="243.3" font-size="12" text-anchor="middle">1.0</text>
<rect x="183" y="223.7" width="24" height="26.3" fill="#e08a3c" fill-opacity="0.75"/>
<text x="195" y="219.7" font-size="12" text-anchor="middle">9.7</text>
<rect x="235" y="246.5" width="24" height="3.5" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="247" y="242.5" font-size="12" text-anchor="middle">1.3</text>
<rect x="263" y="214.7" width="24" height="35.3" fill="#e08a3c" fill-opacity="0.75"/>
<text x="275" y="210.7" font-size="12" text-anchor="middle">13.0</text>
<rect x="405" y="202.5" width="24" height="47.5" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="417" y="198.5" font-size="12" text-anchor="middle">10</text>
<rect x="433" y="204.7" width="24" height="45.3" fill="#e08a3c" fill-opacity="0.75"/>
<text x="445" y="200.7" font-size="12" text-anchor="middle">9</text>
<rect x="485" y="86.8" width="24" height="163.2" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="497" y="82.8" font-size="12" text-anchor="middle">2732</text>
<rect x="513" y="130.8" width="24" height="119.2" fill="#e08a3c" fill-opacity="0.75"/>
<text x="525" y="126.8" font-size="12" text-anchor="middle">323</text>
<rect x="565" y="155.8" width="24" height="94.2" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="577" y="151.8" font-size="12" text-anchor="middle">96</text>
<rect x="593" y="184.4" width="24" height="65.6" fill="#e08a3c" fill-opacity="0.75"/>
<text x="605" y="180.4" font-size="12" text-anchor="middle">24</text>
<text x="101" y="268" font-size="12" text-anchor="middle">바이트 IRQ</text>
<text x="181" y="268" font-size="12" text-anchor="middle">DMA HT/TC</text>
<text x="261" y="268" font-size="12" text-anchor="middle">DMA+IDLE</text>
<text x="431" y="268" font-size="12" text-anchor="middle">바이트 IRQ</text>
<text x="511" y="268" font-size="12" text-anchor="middle">DMA HT/TC</text>
<text x="591" y="268" font-size="12" text-anchor="middle">DMA+IDLE</text>
<text x="340" y="296" font-size="12" text-anchor="middle">QEMU mps2-an385 icount 기준 · 에뮬레이션 하드웨어 자체 부하(1.7 % / 13.1 %)를 뺀 값</text>
</svg>
```
그림 6 — 측정 차트(예제 9). 왼쪽: 펌웨어 순 CPU 부하. 바이트 IRQ는 1 Mbaud에서 63.7 %로 CPU를 거의 다 먹고, DMA는 9.7~13.0 %. 오른쪽: 프레임 끝 → 태스크 지연(로그 축). HT/TC만 쓰면 지연이 버퍼 크기에 묶여 2.7 ms까지 늘고, IDLE을 더하면 바이트 IRQ와 같은 자릿수로 돌아온다.

### 7.4 숫자 읽기

- **바이트 IRQ의 대가.** 115200 baud에서 순부하 15.9 %는 1421 B × 1회 = IRQ 1421번 + 태스크 깨움 1421번의 값이다. 바이트 하나에 15.9 % × 177.6 ms / 1421 ≈ 19.9 µs(icount로 명령 약 620개 — ISR 진입 · 통지 · 문맥 전환 두 번 · 태스크 처리)가 든다. 1 Mbaud에서는 바이트 간격이 10 µs라서 **부하가 63.7 %로 폭증**하고 깨움이 1031번으로 줄었다(1421 − 1031 = 390번은 태스크가 깨어나기 전에 다음 바이트가 와서 통지 카운트가 합쳐진 것). 바이트를 잃지는 않았지만(`lost_bytes=0`) 여유가 거의 없다 — 다른 태스크 하나만 끼어들어도 RXNE를 놓친다(ORE).
- **DMA의 순부하는 거의 "파싱 비용"이다.** 115200에서 HT/TC 1.0 %, +IDLE 1.3 %. 1421 B를 `feed()`하는 비용(바이트당 약 1.25 µs) 자체가 대부분이고, IRQ 44~84번의 비용은 작다. IDLE을 더한 비용은 0.3 %p(115200) · 3.3 %p(1 Mbaud) — 지연을 2732 → 96 µs로 줄이는 값으로는 싸다.
- **HT/TC만 쓰면 지연이 버퍼에 묶인다.** 115200에서 평균 2732 µs, 1 Mbaud에서 323 µs — 둘의 비(8.5배)는 바이트 시간의 비(8.7배)와 거의 같다. 즉 지연은 처리 속도가 아니라 "절반이 차는 속도"로 정해진다(이 트래픽은 쉼이 끼어 실효 바이트 간격이 177.6 ms / 1421 ≈ 125 µs라서, 115200에서 절반 32 B가 차는 데 평균 약 4 ms). 그리고 두 속도 모두 마지막 프레임을 못 받았다(good=39).
- **IDLE 지연 ≈ 1 바이트 시간 + 약 10 µs.** 115200에서 96 µs(바이트 시간 86.8 µs), 1 Mbaud에서 24 µs(바이트 시간 10 µs + 처리). 바이트 IRQ(9~10 µs)보다 길지만 그 차이는 IDLE 검출이 원래 요구하는 "한 바이트 시간 조용함"이다.

결론 한 줄: **가변 길이 UART는 원형 DMA + HT/TC + IDLE이 기본이고, 바이트 IRQ는 낮은 속도의 콘솔에만 쓴다.** 수치는 QEMU icount 기준이므로 실칩의 절대값은 다르지만(STM32H7 480 MHz라면 바이트 IRQ 비용이 훨씬 작다), 비율과 경향은 같다 — IRQ 횟수가 1421 대 84라는 사실은 칩과 무관하다.

---

## 8. Zephyr DMA API — `dma_config` · `dma_start` · 콜백 문맥

### 8.1 API 한 장

Zephyr의 DMA는 칩 독립 API(`include/zephyr/drivers/dma.h`)다. 드라이버(UART, SPI, I2S)가 내부에서 쓰는 경우가 대부분이지만, ADC 블록 샘플링이나 메모리 복사처럼 앱이 직접 쓸 때도 있다. v4.4.2에서 확인한 이름들:

| 항목 | 이름 | 이 노트와의 관계 |
|---|---|---|
| 설정 · 시작 · 정지 | `dma_config(dev, ch, &cfg)`, `dma_start(dev, ch)`, `dma_stop(dev, ch)`, `dma_reload(...)`, `dma_get_status(dev, ch, &st)` | `st.pending_length` = NDTR에 해당(§3.5) |
| 채널 | `dma_request_channel(dev, filter)`, `dma_release_channel(dev, ch)` | |
| 설정 구조체 | `struct dma_config`: `channel_direction`(`MEMORY_TO_MEMORY` 등), `cyclic`, `half_complete_callback_en`, `complete_callback_en`, `source_data_size`, `dest_burst_length`, `block_count`, `head_block`, `dma_callback`, `user_data` | `cyclic` = 원형, `half_complete_callback_en` = HT |
| 블록 | `struct dma_block_config`: `source_address`, `dest_address`, `block_size`, `next_block`, `source_reload_en`/`dest_reload_en` | 64비트 주소는 `CONFIG_DMA_64BIT` |
| 콜백 | `typedef void (*dma_callback_t)(const struct device *dev, void *user_data, uint32_t channel, int status)` | status: `DMA_STATUS_COMPLETE`(0), `DMA_STATUS_BLOCK`(1), `DMA_STATUS_HALF_COMPLETE`(2), 음수 = 에러 |

주의 하나: 헤더에 `DMA_STATUS_HALF_COMPLETE`가 있지만, **STM32 드라이버(`dma_stm32.c`의 `dma_stm32_irq_handler`)는 HT를 `DMA_STATUS_BLOCK`으로 보고한다**(HT가 서 있으면 `status = DMA_STATUS_BLOCK`, 아니면 TC → `DMA_STATUS_COMPLETE`). 그리고 HT와 TC가 동시에 서 있으면 `if/else if`라서 한 번 진입에 HT만 처리하고 TC는 남겨 둔다 — IRQ가 다시 들어와 TC를 처리한다. 드라이버마다 status 의미가 조금씩 다르므로 이식 코드는 "BLOCK이든 HALF든 위치를 `dma_get_status`로 다시 읽는다"로 쓰는 것이 안전하다(§3의 원칙 그대로). 콜백 문맥은 **DMA ISR**이다.

### 8.2 예제 11 — `dma_config` / `dma_start`, 콜백 → `k_sem_give` (native_sim + `zephyr,dma-emul`)

무엇을 확인하나: M2M 블록 하나를 Zephyr DMA API로 설정 · 시작하고 콜백에서 세마포어를 주는 기본 골격, 그리고 에뮬레이터에서 콜백이 어느 문맥에서 도는지.

```c
/* Z2: Zephyr DMA API — dma_config / dma_start, 콜백 → k_sem_give, 콜백이 어느 문맥에서 도나 (native_sim + dma-emul) */
#include <zephyr/kernel.h>
#include <zephyr/drivers/dma.h>
static const struct device *dma = DEVICE_DT_GET(DT_NODELABEL(dma));
static uint8_t src[64], dst[64];
static K_SEM_DEFINE(done, 0, 1);
static volatile int cb_status = 99, cb_isr = -1;
static volatile int cb_prio = 99, cb_is_main = -1;
static k_tid_t main_tid;
static void dma_cb(const struct device *dev, void *user, uint32_t ch, int status)
{
    ARG_UNUSED(dev); ARG_UNUSED(user); ARG_UNUSED(ch);
    cb_status = status;                             /* DMA_STATUS_COMPLETE(0) / BLOCK(1) / 음수 = 에러 */
    cb_isr = k_is_in_isr();
    cb_prio = k_thread_priority_get(k_current_get());
    cb_is_main = k_current_get() == main_tid;          /* 누구의 스레드에서 불렸나 */
    k_sem_give(&done);                             /* ISR에서도 안전한 API만 쓴다 */
}
int main(void)
{
    main_tid = k_current_get();
    for (int i = 0; i < 64; i++) src[i] = (uint8_t)i;
    struct dma_block_config blk = {
        .source_address = (uintptr_t)src, .dest_address = (uintptr_t)dst, .block_size = sizeof src,
    };
    struct dma_config cfg = {
        .channel_direction = MEMORY_TO_MEMORY, .complete_callback_en = 1,
        .source_data_size = 1, .dest_data_size = 1, .source_burst_length = 16, .dest_burst_length = 16,
        .block_count = 1, .head_block = &blk, .dma_callback = dma_cb,
    };
    int ch = dma_request_channel(dma, NULL);
    int r1 = dma_config(dma, ch, &cfg);
    int64_t t0 = k_uptime_ticks();
    int r2 = dma_start(dma, ch);
    int r3 = k_sem_take(&done, K_MSEC(100));
    printk("ch=%d config=%d start=%d take=%d after %lld ticks\n", ch, r1, r2, r3, k_uptime_ticks() - t0);
    printk("callback: status=%d in_isr=%d on_main_thread=%d cb_thread_prio=%d, dst[63]=%u\n",
           cb_status, cb_isr, cb_is_main, cb_prio, dst[63]);
    struct dma_status st;
    if (dma_get_status(dma, ch, &st) == 0)
        printk("status: busy=%d pending_length=%u\n", st.busy, st.pending_length);
    dma_release_channel(dma, ch);
    return 0;
}
```

```text
# prj.conf: CONFIG_DMA=y, CONFIG_DMA_EMUL=y, CONFIG_PRINTK=y, CONFIG_DMA_64BIT=y
# app.overlay: &dma { dma-channels = <2>; status = "okay"; };   (native_sim.dts의 zephyr,dma-emul 노드)
```

```text
*** Booting Zephyr OS build v4.4.2 ***
ch=0 config=0 start=0 take=0 after 0 ticks
callback: status=0 in_isr=0 on_main_thread=0 cb_thread_prio=0, dst[63]=63
```

(끝의 `Stopped at 1.010s` 줄은 생략. status 줄이 없는 것은 `dma_emul.c`의 `dma_emul_get_status()`가 `-ENOSYS`만 돌려주기 때문이다 — 에뮬레이터로는 위치(`pending_length`)를 읽는 원형 수신을 시험할 수 없다.)

출력에서 볼 것: 설정 · 시작 · 대기가 모두 0(성공)이고 데이터가 복사됐다. 콜백은 `DMA_STATUS_COMPLETE`(0)로, **ISR이 아니고(in_isr=0) main 스레드도 아닌** 스레드에서 불렸다 — `dma_emul.c`는 인스턴스마다 전용 workqueue(`k_work_queue_start`, DT `priority` 기본 0)를 만들어 거기서 `memcpy`하고 콜백을 부른다. 실칩 드라이버는 DMA ISR에서 부르므로, 콜백 안에서는 이 예제처럼 `k_sem_give` 같은 ISR-안전 API만 쓴다 — 그러면 두 환경 모두에서 맞다. `CONFIG_DMA_64BIT=y`는 64비트 native_sim에서 `dma_block_config` 주소 필드를 64비트로 만든다 — 끄면 Zephyr의 `dma_emul.c` 자체가 포인터 캐스트 경고 2개를 낸다(드라이버 쪽 경고라 앱 코드로는 못 고친다).

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나

완료 IRQ에서 태스크가 깨어나기까지의 경로는 E1 · E4 · D5에서 본 "ISR → 통지 → PendSV"와 같다. DMA라서 달라지는 것은 없고, 그래서 **DMA 드라이버의 ISR은 반드시 커널 인지(kernel-aware) 우선순위**(FreeRTOS: `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 숫자가 크거나 같게, 예제 6은 0x40 ≥ 0x20)에 있어야 한다(E2). 에뮬레이션의 TIMER0(우선순위 0)이 FreeRTOS API를 전혀 부르지 않는 이유이기도 하다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="20" font-size="13" text-anchor="middle">FreeRTOS V11.3.2 (예제 6)</text>
<text x="510" y="20" font-size="13" text-anchor="middle">Zephyr v4.4.2 (STM32 실칩 경로)</text>
<rect x="20" y="32" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="54" font-size="12" text-anchor="middle">DMA TC IRQ 57 → IRQ57_Handler (IPSR 73)</text>
<rect x="20" y="84" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="106" font-size="12" text-anchor="middle">vTaskGenericNotifyGiveFromISR (tasks.c)</text>
<rect x="20" y="136" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="158" font-size="12" text-anchor="middle">listREMOVE_ITEM + prvAddTaskToReadyList</text>
<rect x="20" y="188" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="210" font-size="12" text-anchor="middle">xHigherPriorityTaskWoken = pdTRUE</text>
<rect x="20" y="240" width="300" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="170" y="262" font-size="12" text-anchor="middle">portYIELD_FROM_ISR → ICSR PENDSVSET</text>
<rect x="20" y="292" width="300" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170" y="314" font-size="12" text-anchor="middle">PendSV → vTaskSwitchContext → 태스크</text>
<rect x="360" y="32" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="54" font-size="12" text-anchor="middle">dma_stm32_irq_handler (HT → BLOCK, TC)</text>
<rect x="360" y="84" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="106" font-size="12" text-anchor="middle">dma_callback(dev, user_data, ch, status)</text>
<rect x="360" y="136" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="158" font-size="12" text-anchor="middle">k_sem_give → z_unpend_first_thread</text>
<rect x="360" y="188" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="210" font-size="12" text-anchor="middle">z_ready_thread + z_reschedule (ISR이면 보류)</text>
<rect x="360" y="240" width="300" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510" y="262" font-size="12" text-anchor="middle">z_arm_int_exit → ICSR PENDSVSET</text>
<rect x="360" y="292" width="300" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510" y="314" font-size="12" text-anchor="middle">z_arm_pendsv → 깨어난 스레드</text>
<line x1="170" y1="66" x2="170" y2="82" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="118" x2="170" y2="134" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="170" x2="170" y2="186" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="222" x2="170" y2="238" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="274" x2="170" y2="290" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="66" x2="510" y2="82" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="118" x2="510" y2="134" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="170" x2="510" y2="186" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="222" x2="510" y2="238" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="274" x2="510" y2="290" stroke="currentColor" marker-end="url(#ah7)"/>
</svg>
```
그림 7 — DMA 완료 IRQ에서 태스크 실행까지, 두 커널의 소스 경로. 둘 다 "ISR 안에서는 ready로만 옮기고, 실제 전환은 PendSV(가장 낮은 우선순위 예외)로 미룬다"(B4, C2).

**FreeRTOS**(`tasks.c`, V11.3.2):

- `vTaskNotifyGiveFromISR(h, &w)`는 매크로이고 `vTaskGenericNotifyGiveFromISR(h, tskDEFAULT_INDEX_TO_NOTIFY, &w)`(tasks.c 8315행)로 간다. `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`가 IRQ 우선순위를 검사하고, `taskENTER_CRITICAL_FROM_ISR()` 안에서 `ulNotifiedValue[i]++`, `ucNotifyState[i] = taskNOTIFICATION_RECEIVED`.
- 대상이 `taskWAITING_NOTIFICATION`이었으면 스케줄러가 돌고 있을 때 `listREMOVE_ITEM(&pxTCB->xStateListItem)` + `prvAddTaskToReadyList(pxTCB)`, 스케줄러가 정지돼 있으면 `xPendingReadyList`에 넣는다. 깨운 태스크의 우선순위가 현재보다 높으면 `*pxHigherPriorityTaskWoken = pdTRUE`와 `xYieldPendings[0] = pdTRUE`.
- 예제 3의 `xTaskNotifyAndQueryFromISR(..., eSetBits, &prev, ...)`는 `xTaskGenericNotifyFromISR`(8133행)로 가서 `ulNotifiedValue |= bits` — 같은 비트를 두 번 OR해도 한 번과 같다는 것이 "same-bit-lost"의 정체다. 이전 값 `prev`로만 잡을 수 있다.
- `portYIELD_FROM_ISR(x)` = `portEND_SWITCHING_ISR(x)`(ARM_CM3 `portmacro.h` 98 · 111행) → `portYIELD()` → ICSR(`0xE000ED04`)에 `PENDSVSET`(비트 28). 예제 7의 objdump에 보인 `str 0x10000000 → [0xe000e000 + 0xd04]`가 바로 이것이다.

**Zephyr**(v4.4.2):

- `dma_stm32_irq_handler()`가 플래그를 보고 status를 정한 뒤 `stream->dma_callback(dev, stream->user_data, callback_arg, status)`를 부른다(§8.1).
- 콜백 안의 `k_sem_give` → `z_impl_k_sem_give`(`kernel/sem.c` 95행): 대기 스레드가 있으면 `z_unpend_first_thread` → `z_ready_thread` → `z_reschedule`. ISR 안에서는 `resched()` 검사(`arch_irq_unlocked(key) && !arch_is_in_isr()`, `kernel/sched.c`)가 거짓이라 바로 전환하지 않는다.
- ISR이 끝날 때 `z_arm_int_exit`(`arch/arm/core/cortex_m/exc_exit.c`)가 더 높은 스레드가 ready면 `SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk`, 그리고 `z_arm_pendsv`(`swap_helper.S`)가 문맥을 바꾼다.

커널은 DMA도 캐시도 모른다(B8 §8.1: FreeRTOS 커널 소스에 캐시 코드 없음). 소유권 · 캐시 · 위치 계산은 전부 **드라이버와 앱의 몫**이다. Zephyr는 드라이버 계층(`uart_stm32.c`, `dma_stm32.c`, `sys_cache_data_*_range`, `__nocache`)에서 일부를 대신해 주고, FreeRTOS는 벤더 HAL(ST HAL, NXP SDK)이 대신한다.

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 (STM32H7급, Cortex-M7 + D-cache)

- **GNSS(115200~460800 baud, NMEA · UBX)**: 원형 DMA 256~512 B + HT/TC + IDLE, `HAL_UARTEx_ReceiveToIdle_DMA`. 버퍼는 non-cacheable 섹션(또는 매 조각 invalidate, 예제 8의 P2). GNSS 태스크는 우선순위가 낮아도 된다 — 버퍼 절반 시간(460800 baud, 256 B → 2.8 ms)보다 최악 응답이 짧기만 하면.
- **RC 수신기(CRSF 420000 baud, SBUS 100000 baud)**: 프레임이 짧고(CRSF 최대 64 B) 지연이 조종감에 직결된다 → IDLE 필수. CRSF는 CRC8로 프레임 검증.
- **텔레메트리 · MAVLink TX**: 패킷 조립은 게이트 밖, `tlm_send`(copy-in + clean + DMA + 완료 세마포어, 예제 7). 여러 태스크가 보내면 TX 게이트키퍼 태스크 + 우선순위 큐.
- **IMU SPI 버스트(1~8 kHz, 15 B)**: DRDY EXTI → SPI DMA → TC → invalidate 1 라인 → rate 태스크 notify(예제 7). IMU 두 개면 SPI 버스마다 채널 하나. ESC DShot도 타이머 + DMA로 비트 패턴을 내보낸다(DMA로 타이머 CCR 갱신 — 레지스터 세부는 BM).

### 10.2 로봇 관절 모터 컨트롤러 (FOC 20 kHz)

전류 센서 ADC는 PWM 타이머가 트리거하고(E5) DMA가 3상 전류를 버퍼로 옮긴다. 20 kHz FOC 루프는 **DMA TC ISR 안에서 바로 돈다**(태스크로 넘기면 문맥 전환 지연이 50 µs 주기에 비해 너무 크다 — E2의 "커널 밖" 고속 ISR). 이때 ISR은 RTOS API를 쓰지 않고, 느린 계층(위치 루프 1 kHz, CAN 통신)은 태스크에서 돈다. 더블 버퍼는 "ISR이 계산 중인 샘플"과 "DMA가 채우는 샘플"을 분리하는 용도다.

### 10.3 센서 허브 MCU (Linux SoC 옆)

IMU · 바로미터 샘플을 타임스탬프와 함께 모아서 SPI/UART로 SoC에 보낸다. 수신 쪽(SoC 명령)은 idle-line, 송신 쪽은 버퍼 풀 + 큐(소유권이 큐를 따라 이동, D4 §3) + TX DMA. Linux 쪽은 `dmaengine`과 `dma_map_single`(EL H9)로 같은 일을 하지만, MCU에는 IOMMU · 주소 변환이 없어 **CPU 주소 = DMA 주소**이고 캐시 유지도 직접 한다는 점이 다르다.

### 10.4 SSD 컨트롤러 펌웨어 (Don의 경험)

호스트 쓰기의 DMA 완료 → 완료 큐 엔트리 → 펌웨어가 버퍼를 NAND 프로그램 큐로 넘기는 흐름은 **"큐에 포인터 + 순번"** 방식(§2.4 표의 세 번째 줄)과 같다. SSD에서는 버퍼 풀이 크고 이벤트가 하드웨어 큐에 쌓여 "비트가 합쳐지는" 문제가 없었다. MCU에서는 원형 버퍼 하나와 sticky 플래그 하나가 전부라서, 같은 습관(완료 엔트리 = 진실)을 **"위치 레지스터 = 진실"** 로 옮겨야 한다. 면접에서 이 연결을 말하면 강하다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| HT/TC만 켜고 가변 길이 프레임을 받음 | 마지막 응답이 다음 데이터가 올 때까지 안 옴, 명령 응답이 "한 박자 늦음" | 절반이 차야 이벤트가 온다(예제 3: 13 B 갇힘) | IDLE 인터럽트 추가, 위치로 처리(예제 4) |
| 원형 모드 HAL 콜백의 `Size`를 "받은 길이"로 씀 | 데이터 중복 · 누락, 랜덤 파싱 오류 | 원형 모드에서 `Size` = 버퍼 안 현재 위치 | `new = (Size - last + N) % N` |
| 처리 시간 > 절반 시간 | 가끔 깨진 프레임, 부하가 오를 때만 | DMA가 CPU가 쥔 절반 위에 씀(예제 3: overrun 41, 깨짐 4) | 버퍼 키우기 · 우선순위 · copy-out, 소유권 플래그로 감지 |
| `eSetBits`로 절반을 알리고 횟수 · 순서를 믿음 | 장시간 멈춘 뒤 절반 하나를 건너뜀, 순서 뒤바뀜 | 비트 OR은 반복 · 순서를 잃는다 | 위치 기반 처리, 또는 큐로 순번 전달 |
| DMA 완료 콜백 안에서 파싱 · 로그 · 블로킹 API | 다른 IRQ 지연 증가, `configASSERT` 실패 | 콜백 = DMA ISR 문맥 | 통지만 하고 태스크에서 처리(E1, E4) |
| DMA IRQ 우선순위를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높게(숫자 작게) 두고 FromISR 호출 | 드물게 리스트 손상, HardFault, `portASSERT_IF_INTERRUPT_PRIORITY_INVALID` | 커널 크리티컬 섹션이 그 IRQ를 막지 못한다 | DMA ISR은 커널 인지 우선순위로(E2) |
| 스핀 대기 후 컴파일러 배리어 없이 DMA 버퍼 읽기 | `-O2`에서만 옛 값 | 읽기가 DMA 시작 · 완료 확인 앞으로 이동(예제 2) | `"memory"` clobber, 또는 RTOS 대기 함수 사용 |
| M7에서 idle-line 조각을 HT 때만 invalidate | 가끔 몇 바이트가 옛 값 | 반쯤 찬 라인이 캐시에 남음(예제 8 P1: 53 % 오류) | 읽기 직전 [last, pos)를 라인으로 넓혀 invalidate, 또는 non-cacheable |
| TX 버퍼 clean 누락 / 정렬 안 된 RX 버퍼 invalidate | 송신 데이터가 이전 패킷, 근처 변수 값이 되돌아감 | 캐시에만 있는 데이터, 이웃 라인 파괴(B8 §6.5) | 32 B 정렬 · 32 배수 크기 전용 섹션(예제 7) |
| 게이트(mutex)를 쥔 채 패킷 조립 | 높은 우선순위 송신자가 ms 단위로 대기 | 게이트 구간이 길다(예제 6: 2004 µs) | 조립은 게이트 밖, 게이트 안은 clean + 시작 + 대기만 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How do you receive variable-length UART frames with DMA on an STM32 running an RTOS?

**A.** 원형 DMA로 버퍼를 계속 채우고, HT · TC · UART IDLE 세 인터럽트를 모두 켠다. 세 ISR은 플래그만 지우고 수신 태스크에 notification을 준다. 태스크는 깨어나면 NDTR을 읽어 pos = N − NDTR을 구하고, 지난번 위치부터 pos까지(wrap이면 두 조각)를 파서에 넣는다. 이벤트는 힌트이고 위치가 진실이라서 알림이 합쳐져도 데이터를 잃지 않는다. 프레임 경계는 IDLE이 아니라 프로토콜(구분자, 길이, CRC)로 판단한다. QEMU 에뮬레이션에서 HT/TC만 쓰면 프레임 끝 지연이 평균 2.7 ms였고 IDLE을 더하면 96 µs였다.

> I run the RX DMA in circular mode and enable three interrupts: half-transfer, transfer-complete and UART IDLE. Each ISR just clears its flag and notifies the receive task. The task reads NDTR, computes the write position as N minus NDTR, and processes everything from the last position to the current one, in two pieces if it wrapped. Events are only hints; the DMA position is the source of truth, so coalesced notifications never lose data. Frame boundaries come from the protocol — delimiter, length field, CRC — not from IDLE itself. In my emulated test at 115200 baud, HT/TC alone gave about 2.7 ms end-of-frame latency; adding IDLE brought it to about 100 µs.

**꼬리 질문**: "IDLE이 프레임 중간에 오면?" → 파서에 지금까지 온 것을 넣을 뿐이고, 나머지는 다음 이벤트로 이어진다. 그래서 IDLE을 프레임 끝으로 믿으면 안 된다.

**Q.** What happens in a ping-pong DMA scheme if the CPU is too slow, and how do you detect it?

**A.** DMA는 멈추지 않으므로 TC 순간 다시 앞 절반으로 들어가 CPU가 아직 읽는 데이터 위에 쓴다 — overrun이다. 감지는 세 층으로 한다. ① 소유권 플래그: HT/TC ISR에서 반대쪽 절반을 CPU가 아직 쥐고 있으면 경보(보수적, 실제 손상보다 많이 울린다 — 실험에서 경보 41, 깨진 프레임 4). ② lap 카운터: DMA가 쓴 총량과 읽은 총량 비교. ③ 프로토콜 CRC · 순번으로 최종 판정. 고치는 법은 절반 시간을 처리 태스크의 최악 응답 시간보다 길게(버퍼 크기 · 우선순위) 하거나, 바로 복사해 내고 소유권을 반납하는 것이다.

> The DMA never stops, so at TC it re-enters the first half while the CPU is still reading it, and the data gets overwritten. I detect it at three levels: an ownership flag checked in the HT/TC ISR — if the other half is still owned by the CPU, raise an overrun alarm; a lap counter comparing bytes written versus bytes consumed; and finally the protocol CRC or sequence number. The fix is sizing: the half-buffer time must exceed the worst-case response time of the processing task, or I copy the data out immediately and release the half.

**Q.** How do you signal DMA completion to a task, and what are the trade-offs?

**A.** 세 가지다. ① task notification 비트(`eSetBits`)로 "어느 절반" — 가장 싸지만 순서와 반복 횟수를 잃는다. ② notification 카운트 + 위치 레지스터 — 원형 RX의 기본, 위치가 진실이라 합쳐져도 괜찮다. ③ 큐로 `{포인터, 길이, 순번}` — 순서 · 횟수 · 소유권이 모두 명시적이고 버퍼 풀 회전에 맞지만, 큐가 차면 ISR이 정책을 정해야 한다. 어느 경우든 ISR은 `FromISR` + `portYIELD_FROM_ISR`만 하고 처리는 태스크에서 한다.

> Three options. Notification bits — cheapest, but you lose ordering and repeat counts. A notification count plus re-reading the DMA position — my default for circular RX, because the position is the truth. Or a queue carrying buffer pointer, length and a sequence number — explicit ordering and ownership transfer, ideal for buffer pools, but the ISR needs a policy when the queue is full. In all cases the ISR only uses FromISR calls and yields; parsing happens in the task.

**Q.** On a Cortex-M7, what cache maintenance does a DMA driver need?

**A.** TX는 시작 전에 clean, RX는 시작 전에 invalidate(dirty 라인이 DMA 도중 evict되어 덮는 것 방지)하고 **완료 후 읽기 직전에 다시** invalidate(투기적 읽기로 다시 채워진 라인 제거). 버퍼는 32 B 정렬 · 32 배수 크기 전용 섹션에 둔다 — 아니면 invalidate가 이웃 변수를 지운다. idle-line처럼 라인 중간에서 끊기는 조각을 읽으면 그 라인이 반쯤 찬 채 캐시에 들어오므로 조각마다 다시 invalidate해야 한다. 작은 UART 버퍼는 MPU로 non-cacheable 영역에 두는 편이 단순하고, Zephyr STM32 UART async는 실제로 그걸 요구한다. DMA가 닿는 칩이면 DTCM도 답이다.

> Clean before TX. For RX, invalidate before starting and again right before the CPU reads, because of speculative line fills. Buffers live in a dedicated section, 32-byte aligned and sized in multiples of 32, otherwise the invalidate destroys neighbouring variables. With idle-line reception a chunk can end mid-line, so that line is cached half-filled — I invalidate the rounded range before every read. For small UART buffers I prefer an MPU non-cacheable region; Zephyr's STM32 async UART driver actually requires it. DTCM works too, if that DMA master can reach it.

**Q.** Several tasks share one UART TX DMA channel. How do you design it?

**A.** 두 가지. 블로킹 API면 mutex 게이트 + 완료 통지: 게이트 획득 → 자기 DMA 버퍼로 copy-in → clean → 시작 → TC ISR이 준 세마포어 대기(타임아웃) → 게이트 반납. mutex라서 우선순위 상속이 걸린다. 핵심은 게이트 안에서 긴 일을 하지 않는 것 — 실험에서 낮은 태스크가 게이트 안에서 3 ms 조립하자 높은 태스크가 2 ms 막혔다. 비블로킹이 필요하면 TX 게이트키퍼 태스크 하나가 큐에서 버퍼를 받아 보낸다.

> Either a blocking API with a mutex gate plus a completion semaphore — take the gate, copy into the driver's own aligned buffer, clean, start, wait on the semaphore given by the TC ISR with a timeout, release the gate. The mutex gives priority inheritance. The key rule is to keep the gated section short: in my test a low-priority task assembling a packet inside the gate blocked a high-priority sender for 2 ms. If callers must not block, a single TX gatekeeper task drains a queue of buffers.

**Q.** When is DMA not worth it?

**A.** 전송이 짧고 자주 없을 때. DMA 설정(채널 · 주소 · 캐시 유지)과 완료 IRQ 비용이 몇 바이트 폴링보다 비쌀 수 있다. 예를 들어 레지스터 2바이트 읽기는 폴링이 낫다. 반대로 IMU 15 B를 8 kHz로 읽는 것처럼 짧아도 주기가 높으면, CPU를 PID 계산에 돌려주고 rate loop 지터를 줄이려고 DMA를 쓴다. 판단은 "CPU가 그동안 할 일이 있는가"와 "IRQ 횟수"로 한다 — 측정에서 1 Mbaud 바이트 IRQ는 부하 64 %, DMA는 10~13 %였다.

> When transfers are tiny and rare: channel setup, cache maintenance and the completion interrupt can cost more than polling a few bytes. But for short, high-rate transfers like a 15-byte IMU burst at 8 kHz, DMA frees the CPU for the control math and reduces jitter. At 1 Mbaud my measurement showed 64 % CPU for per-byte interrupts versus 10–13 % with DMA.

**Q.** In Zephyr, how does the async UART API hand buffers to the application?

**A.** `uart_rx_enable(dev, buf, len, timeout_us)`로 첫 버퍼를 준다. 드라이버가 받기 시작하면 `UART_RX_BUF_REQUEST`를 내고, 앱은 즉시 `uart_rx_buf_rsp()`로 두 번째 버퍼를 준다. 데이터는 `UART_RX_RDY`로 `buf + offset`, `len`만큼 알려 주고(timeout = idle, 버퍼 가득), 버퍼를 다 쓰면 `UART_RX_BUF_RELEASED`로 앱에 돌려준다 — 두 버퍼가 핑퐁으로 돈다. 콜백 문맥은 드라이버마다 다르다(STM32는 DMA/USART ISR 또는 timeout이 0이 아니면 workqueue, 에뮬레이터는 workqueue). 그래서 콜백에서는 ISR-안전 API만 쓴다.

> You pass the first buffer with uart_rx_enable and an inactivity timeout in microseconds. When reception starts the driver raises UART_RX_BUF_REQUEST and you immediately supply the next buffer with uart_rx_buf_rsp. Data arrives as UART_RX_RDY with offset and length; when a buffer is full you get UART_RX_BUF_RELEASED and own it again — the two buffers ping-pong. The callback context depends on the driver, so I only use ISR-safe calls in it.

---

## 13. 직접 해보기

1. (개념) 921600 baud, 8N1, 원형 버퍼 128 B로 받는다. 절반 시간은? 처리 태스크의 최악 응답 시간이 1 ms라면 안전한가?
   정답/힌트: 바이트 시간 10.85 µs, 절반 64 B → 694 µs. 1 ms > 694 µs라서 overrun 위험. 버퍼를 256 B 이상(절반 1.39 ms)으로.
2. (코드) 예제 3을 `eSetBits` 대신 `xQueueSendFromISR`로 `{half, seq}`를 보내게 바꾸고, `WORK_US=8000`에서 순번 건너뜀으로 손실을 정확히 세어 보라.
   정답/힌트: 큐 깊이 4면 ISR은 가득 참을 만나지 않고, 태스크가 받은 seq의 차이가 1보다 크면 그만큼 사라진 이벤트다. overrun 플래그와 비교해 보라.
3. (코드) 예제 4에 §2.6의 lap 카운터를 넣고(DMA ISR에서 TC마다 `lap++`), 태스크가 "쓴 총량 − 읽은 총량 > N"이면 손실을 보고하게 하라. 태스크에 큰 `vTaskDelay`를 한 번 넣어 검증하라.
   정답/힌트: lap → NDTR → lap 순서로 읽고 lap이 바뀌었으면 다시 읽는다. TC 플래그가 서 있는데 ISR이 아직이면 lap + 1로 본다.
4. (측정) 예제 9를 N = 32, 64, 256으로 돌려 MODE 2의 지연과 MODE 3의 IRQ 수가 N에 어떻게 비례하는지 표로 만들라.
   정답/힌트: MODE 2 지연 ≈ 절반 채움 시간에 비례, MODE 3 IRQ 수 = (1421 / (N/2)) + 40.
5. (Zephyr) 예제 5에서 `uart_rx_buf_rsp`를 일부러 부르지 않으면 어떤 이벤트가 오는가? 돌려 보고 설명하라.
   정답/힌트: 첫 버퍼가 차면 `RX_RDY` → `RX_BUF_RELEASED` → `RX_DISABLED`(다음 버퍼가 없어서 정지). `uart_emul.c` RX 핸들러의 "the application did not provide a new buffer. Stop RX" 경로(남은 데이터가 있을 때).
6. (캐시) 예제 8에 P4 "invalidate를 정렬하지 않고 정확히 [last, pos) 바이트만"을 흉내 내는 정책을 추가하라(라인 단위로 내림하지 않고 시작 라인만). 결과를 P2와 비교하고, CMSIS 6 구현이 왜 `op_size`에 시작 오프셋을 더하는지 설명하라.
   정답/힌트: 끝 라인을 빼먹으면 틀린 바이트가 다시 생긴다. CMSIS는 걸친 라인 전부를 처리하도록 오프셋을 더한다.
7. (설계) 드론 FC에서 GNSS(460800), CRSF(420000), MAVLink(921600) 세 UART와 IMU SPI 두 개의 DMA · IRQ 우선순위 배치표를 써 보라. 어떤 ISR이 커널 밖이어도 되는가?
   정답/힌트: 전부 커널 인지 우선순위의 짧은 ISR로 충분하다(이벤트당 수 µs). 커널 밖은 FOC · DShot 타이밍처럼 µs 지터가 문제인 것만(E2).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| circular DMA | 원형 DMA | 끝에 닿으면 자동으로 처음으로 돌아가 멈추지 않는 전송 |
| HT / TC | half-transfer / transfer-complete | 절반 · 끝 지점에서 서는 sticky 플래그. 카운터가 아니다 |
| NDTR | number of data to transfer | 남은 전송 수. 쓰기 위치 = N − NDTR |
| ping-pong / double buffer | 핑퐁 · 더블 버퍼 | 둘 중 하나는 DMA가, 다른 하나는 CPU가 쓰는 구조 |
| DBM / CT | double-buffer mode / current target | STM32 DMA의 하드웨어 더블 버퍼 모드와 현재 대상 비트 |
| IDLE line | 유휴 선 검출 | 바이트 수신 뒤 한 프레임 시간 동안 조용하면 서는 UART 플래그 |
| overrun | 덮어쓰기 · 넘침 | DMA가 CPU가 아직 쥔 데이터 위에 씀(또는 UART RDR을 못 읽음 — ORE) |
| ownership | 소유권 | 버퍼를 지금 만질 수 있는 쪽(DMA 또는 CPU) |
| zero-copy / copy-out | 제자리 처리 / 복사해 내기 | DMA 버퍼 안에서 처리 vs 먼저 복사하고 반납 |
| gatekeeper | 게이트키퍼 | 공유 자원(DMA 채널, UART)을 한 곳에서만 쓰게 하는 mutex 또는 전용 태스크 |
| clean / invalidate | 캐시 정리 / 무효화 | dirty 라인을 RAM에 쓰기 / 캐시 복사본 버리기(B8) |
| non-cacheable region | 캐시 안 되는 영역 | MPU로 지정한 DMA 버퍼 섹션, Zephyr `__nocache` |
| async UART API | Zephyr 비동기 UART | `uart_rx_enable`, `UART_RX_RDY`, `UART_RX_BUF_REQUEST` 등 이벤트 기반 DMA UART |
| W1C | write-1-to-clear | 1을 써서 플래그를 지우는 레지스터. 원자적이다 |
| PL081 | ARM PrimeCell DMA | QEMU AN505에 모델링된 2채널 DMA 컨트롤러(PL080의 작은 판) |

---

## 15. 요약 & 체크리스트

DMA + RTOS I/O의 뼈대는 **소유권**이다. 버퍼의 각 바이트는 DMA 아니면 CPU 하나만 만지고, 주인은 DMA 시작과 완료 이벤트(HT · TC · IDLE · TX 완료)에서만 바뀐다. 완료 이벤트는 DMA ISR에서 오므로 ISR은 플래그를 지우고 태스크에 통지만 한다. 통지 수단에 따라 순서와 횟수가 사라지므로(HT/TC 플래그도 비트라 합쳐진다 — PL081 실측), 원형 RX에서는 **이벤트를 힌트로, DMA 위치(N − NDTR)를 진실로** 삼는다. 가변 길이 UART는 원형 DMA + HT/TC + IDLE이 기본이다 — QEMU 에뮬레이션에서 HT/TC만 쓰면 지연 2.7 ms에 마지막 프레임이 갇혔고, IDLE을 더하면 96 µs에 전부 왔으며, 바이트 IRQ는 1 Mbaud에서 CPU의 64 %를 먹었다. 처리 시간이 절반 시간을 넘으면 overrun이고, 소유권 플래그 · lap 카운터 · CRC로 감지하고 버퍼 크기 · 우선순위 · copy-out으로 고친다. TX는 mutex 게이트 + 완료 통지로 감싸고 게이트 안을 짧게 둔다. Cortex-M7에서는 TX 전 clean, RX 전후 invalidate, 32 B 정렬 전용 섹션이 규칙이고, idle-line 조각은 반쯤 찬 라인 때문에 **매번** invalidate하거나 non-cacheable에 둔다.

- [ ] 원형 DMA에서 HT · TC 순간 각 절반의 주인이 누구인지 그림으로 그릴 수 있다
- [ ] "절반 시간 > 처리 태스크 최악 응답 시간" 조건을 baud · N으로 계산할 수 있다
- [ ] HT/TC 플래그가 카운터가 아니라 비트라는 것과 그 결과(이벤트 합쳐짐)를 설명할 수 있다
- [ ] notification 비트 · 카운트 + 위치 · 큐(포인터 + 순번)의 차이와 각각 잃는 정보를 말할 수 있다
- [ ] IDLE + NDTR 수신기의 6줄(wrap 두 조각 포함)을 화이트보드에 쓸 수 있다
- [ ] HAL 원형 모드 `HAL_UARTEx_RxEventCallback`의 `Size`가 위치라는 것을 안다
- [ ] Zephyr async UART 이벤트 순서(BUF_REQUEST → RDY → BUF_RELEASED)와 `uart_rx_buf_rsp`의 역할을 안다
- [ ] 완료 콜백 문맥(STM32 = ISR, 일부 경로 · 에뮬레이터 = workqueue)을 확인하고 ISR-안전 API만 쓴다
- [ ] overrun 감지 세 층(소유권 플래그, lap 카운터, CRC · 순번)을 말할 수 있다
- [ ] TX 게이트(mutex + 완료 통지 + 타임아웃)를 짜고 게이트 안을 짧게 유지한다
- [ ] M7 패턴별 clean · invalidate 위치와 32 B 정렬 섹션을 설계할 수 있고, idle-line의 반쯤 찬 라인 문제를 설명할 수 있다
- [ ] 바이트 IRQ / DMA HT·TC / DMA + IDLE의 부하와 지연을 측정 숫자로 비교할 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM I8 | DMA 컨트롤러 — 원형 · 더블 버퍼 사용법 | §2 전체(소유권 타임라인, 하드웨어 형태 표, 예제 3 overrun), §3 idle-line(예제 4 · 5), §7 zero-copy vs copy-out과 측정(예제 9) | E6을 링크하고 RTOS 없는 슈퍼 루프에서 같은 위치 기반 수신을 폴링으로 도는 최소 코드 |
| BM I8 | DMA 컨트롤러 — half / full 완료 → 태스크 통지 | §1.2 예제 1(TC는 비트), §2.4 통지 수단 표, §4 TX 게이트(예제 6), §8 Zephyr DMA API, §9 커널 경로 | 통지 대신 플래그 + WFI로 기다리는 bare-metal 판(예제 1이 그 꼴), 컴파일러 배리어 규칙은 §1.3 링크 |
| BM I8 | DMA 컨트롤러 — 캐시 정리 | §6 전체(패턴별 규칙표, 반쯤 찬 라인, 예제 8), §5 예제 7의 32 B 정렬 섹션 | 캐시 원리는 B8 링크. startup에서 MPU non-cacheable 영역을 켜는 순서만 |
| BM I8 | DMA 컨트롤러 — 레지스터 수준 (이 노트 범위 밖) | §1.2 `pl081.h`는 실습용 최소 정의만 | **주인 = Bare-Metal**: 채널 · 스트림 · DMAMUX 요청 매핑, linked-list 디스크립터 형식, M2M, 우선순위 · 버스트 · FIFO 임계값, 에러 플래그(TEIF · FEIF · DMEIF)와 복구 |

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `tasks.c`(`vTaskGenericNotifyGiveFromISR`, `xTaskGenericNotifyFromISR`, `ulTaskGenericNotifyTake`, `xTaskGenericNotifyWait`), `include/task.h`(`eSetBits`, `xTaskNotifyAndQueryFromISR`), `include/semphr.h`, `portable/GCC/ARM_CM3/portmacro.h`(`portEND_SWITCHING_ISR`), `portable/GCC/ARM_CM33_NTZ/non_secure`(예제 1 · 2 · 6의 M33 포트)
- FreeRTOS 문서 — Task Notifications: https://en.freertos.org/Documentation/02-Kernel/02-Kernel-features/03-Direct-to-task-notifications/01-Task-notifications
- Zephyr v4.4.2 소스: `include/zephyr/drivers/uart.h`(async API, `enum uart_event_type`), `include/zephyr/drivers/dma.h`(`struct dma_config`, `DMA_STATUS_*`), `drivers/serial/uart_stm32.c`(`uart_stm32_dma_rx_flush`, `stm32_buf_in_nocache` 검사), `drivers/dma/dma_stm32.c`(`dma_stm32_irq_handler`), `drivers/serial/uart_emul.c`, `drivers/dma/dma_emul.c`, `kernel/sem.c`, `arch/arm/core/cortex_m/exc_exit.c`, `soc/st/stm32/common/stm32_cache.c`
- Zephyr 문서 — UART API(async): https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html , DMA: https://docs.zephyrproject.org/latest/hardware/peripherals/dma.html
- QEMU 소스: `hw/dma/pl080.c`(PL080/PL081 모델 — flow control ≥ 4와 soft request 미구현), `hw/arm/mps2-tz.c`(AN505 DMA0~3 주소 · IRQ 표)
- Arm, PrimeCell DMA Controller (PL081) Technical Reference Manual (DDI 0218) — 레지스터 오프셋
- Arm CMSIS 6 `CMSIS/Core/Include/m-profile/armv7m_cachel1.h`(`SCB_InvalidateDCache_by_Addr`, `SCB_CleanDCache_by_Addr`)
- ST, STM32H7 레퍼런스 매뉴얼 RM0433 (DMA · USART IDLE · DTCM 접근 경로), ST HAL `stm32xx_hal_uart_ex.c`(`HAL_UARTEx_ReceiveToIdle_DMA`, `HAL_UARTEx_RxEventCallback`)
- 이 노트가 기대는 노트: RTOS B8(캐시 · TCM · MPU), D4(큐와 소유권), D5(notification · stream buffer), D8(SPSC 링 · 배리어), E1 · E2 · E4, EL H8(UART), EL H9(Linux DMA)
