# O3. Linux ↔ 외부 MCU 통신 설계 — 링크 고르기, COBS+CRC 프레임, ACK/재전송, 시간 동기, MCU 펌웨어 업데이트

> **이 노트를 다 읽으면**: UART·SPI·CAN·USB·Ethernet 중 링크를 근거 있게 고른다 · COBS + CRC-32 + seq/ACK/NAK 프로토콜을 양쪽(C) 코드로 짤 수 있다 · Linux 쪽 tty를 raw + epoll로 다루고 지연·처리량·오류 복구를 직접 측정한다 · NTP식 4-타임스탬프로 MCU 시계를 CLOCK_MONOTONIC에 맞추고 A/B 슬롯 MCU 펌웨어 업데이트(전원 차단 후 이어받기 · 롤백)를 설계한다
> **왜 필요한가**: Scout AI 같은 로봇/국방 장비는 "Jetson급 Linux + 센서·모터용 MCU" 구조다. 면접에서 "그 둘 사이 프로토콜을 어떻게 설계하겠나", "MCU 펌웨어는 필드에서 어떻게 올리나"가 반드시 나온다.
> **Don 기준 난이도**: 프레이밍 · CRC · ISR 링버퍼 · 부트로더는 SSD 펌웨어에서 이미 해 본 영역 / 새로 배울 것은 Linux tty 경로, 재전송 윈도우의 실제 숫자, 시계 동기 수학, "Linux 쪽에서 본" 업데이트 오케스트레이션
> **선행 노트**: N4(무엇을 MCU에 두나 · heartbeat · failsafe), H8(UART/tty/termios/serdev), L4(OTA), H7(SPI), G10(firmware loading). 관련: O2(on-die 코어는 rpmsg), Q3(PPS/PTP), Q4(CAN)

---

## 0. 큰 그림 — 두 개의 컴퓨터가 한 몸처럼 움직이려면

N4에서 "무엇을 MCU로 내리나"와 heartbeat/failsafe를 다뤘다. 이 노트는 그 다음 질문이다: **두 칩 사이의 선(link)과 그 위의 약속(protocol)을 어떻게 만드나.** N4 §5에 프레임 항목표와 간단한 CRC16 프레임이 있었다면, 여기서는 그걸 제품 수준으로 끝까지 판다 — 링크 선택, COBS 프레이밍, 재전송 윈도우, Linux 드라이버 경로, 시간 동기, 그리고 MCU 펌웨어를 Linux가 올리는 경로까지.

Don의 SSD 경험으로 번역하면 이렇다. NVMe 호스트와 SSD 컨트롤러 사이에는 PCIe라는 링크, submission/completion 큐라는 프로토콜, phase bit와 CID로 "어느 명령의 완료인가"를 맞추는 규칙, 그리고 firmware download/commit 명령으로 펌웨어를 바꾸는 경로가 있었다. Linux ↔ MCU도 똑같은 네 층이다. 차이는 링크가 훨씬 느리고 더러우며(UART 한 가닥, 커넥터, 모터 노이즈), 양쪽 다 내가 짜야 한다는 것뿐이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="220" height="230" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="52" text-anchor="middle" font-size="14">Linux SoC (Jetson, i.MX8)</text>
  <text x="40" y="82" font-size="12">인식 · 경로계획 · 로깅</text>
  <text x="40" y="104" font-size="12">링크 데몬: 프레임 · 재전송</text>
  <text x="40" y="126" font-size="12">시간 변환: MCU tick → Linux</text>
  <text x="40" y="148" font-size="12">MCU 펌웨어 업데이트 오케스트라</text>
  <rect x="40" y="170" width="180" height="70" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="130" y="192" text-anchor="middle" font-size="12">커널: tty · 8250/PL011</text>
  <text x="130" y="212" text-anchor="middle" font-size="12">spidev · SocketCAN · USB</text>
  <text x="130" y="230" text-anchor="middle" font-size="12">(H8 · H7 · Q4 · H12)</text>
  <rect x="270" y="110" width="140" height="70" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="134" text-anchor="middle" font-size="13">링크 (이 노트)</text>
  <text x="340" y="154" text-anchor="middle" font-size="12">UART · SPI · CAN</text>
  <text x="340" y="170" text-anchor="middle" font-size="12">USB · Ethernet</text>
  <line x1="240" y1="145" x2="268" y2="145" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="410" y1="145" x2="438" y2="145" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="440" y="30" width="220" height="230" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="550" y="52" text-anchor="middle" font-size="14">MCU (Cortex-M)</text>
  <text x="460" y="82" font-size="12">IMU 1 kHz 샘플 + 타임스탬프</text>
  <text x="460" y="104" font-size="12">모터 PWM · 전류 루프</text>
  <text x="460" y="126" font-size="12">heartbeat · failsafe (N4)</text>
  <text x="460" y="148" font-size="12">부트로더 + A/B 슬롯</text>
  <rect x="460" y="170" width="180" height="70" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="192" text-anchor="middle" font-size="12">UART ISR → 링버퍼</text>
  <text x="550" y="212" text-anchor="middle" font-size="12">SysTick / 타이머 캡처</text>
  <text x="550" y="230" text-anchor="middle" font-size="12">flash 드라이버</text>
  <text x="340" y="285" text-anchor="middle" font-size="12">링크 위 약속: 프레임 경계 · 무결성 · 순서/재전송 · 버전 · 시간 · 업데이트</text>
</svg>
```

그림 1 — Scout형 시스템. 왼쪽은 "느리지만 똑똑한" Linux, 오른쪽은 "멍청하지만 확실한" MCU. 이 노트는 가운데 주황 상자와 그 위의 약속을 다룬다.

### 0.1 이 노트의 실험실 — 전부 실제로 돌린다

진짜 보드 대신 QEMU 두 개를 선으로 잇는다. 왼쪽은 랩 커널 Image-dom(6.18.55-dom) + rootfs-dom(Ubuntu 24.04, systemd)을 쓰는 aarch64 Linux 게스트이고, PCI 16550A UART(`-device pci-serial`)가 `/dev/ttyS0`로 보인다. 오른쪽은 `qemu-system-arm -M mps2-an386`(Cortex-M4, 25 MHz)에서 도는 **직접 짠 bare-metal 펌웨어**다(arm-none-eabi-gcc 14.2, 4 KB). 둘 사이에 Python 프록시를 끼워 비트 뒤집기 · 프레임 드롭 · "MCU 전원 차단"을 주입한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="200" height="200" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="40" text-anchor="middle" font-size="13">qemu-system-aarch64</text>
  <text x="110" y="58" text-anchor="middle" font-size="12">Image-dom + rootfs-dom</text>
  <text x="25" y="88" font-size="12">o3_fw / o3_ping (C)</text>
  <text x="25" y="110" font-size="12">open ttyS0 · raw · epoll</text>
  <text x="25" y="132" font-size="12">tty core → 8250 driver</text>
  <text x="25" y="154" font-size="12">pci-serial (16550A)</text>
  <text x="25" y="190" font-size="12">-chardev socket :40301</text>
  <rect x="250" y="70" width="170" height="110" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="92" text-anchor="middle" font-size="13">o3_proxy.py (VM)</text>
  <text x="335" y="114" text-anchor="middle" font-size="12">프레임마다 확률로</text>
  <text x="335" y="134" text-anchor="middle" font-size="12">비트 1개 뒤집기 · 드롭</text>
  <text x="335" y="154" text-anchor="middle" font-size="12">MCU 재연결 (전원 복귀)</text>
  <line x1="210" y1="125" x2="248" y2="125" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="248" y1="135" x2="212" y2="135" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="420" y1="125" x2="458" y2="125" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="458" y1="135" x2="422" y2="135" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="460" y="20" width="210" height="200" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="40" text-anchor="middle" font-size="13">qemu-system-arm</text>
  <text x="565" y="58" text-anchor="middle" font-size="12">-M mps2-an386 (M4)</text>
  <text x="475" y="88" font-size="12">CMSDK UART0 → IRQ0</text>
  <text x="475" y="110" font-size="12">ISR → 2 KB 링버퍼</text>
  <text x="475" y="132" font-size="12">o3 프로토콜 · SysTick µs</text>
  <text x="475" y="154" font-size="12">semihosting → flash 파일</text>
  <text x="475" y="190" font-size="12">-serial tcp :40302</text>
  <text x="340" y="242" text-anchor="middle" font-size="12">kill -9 로 MCU "전원 차단" → 감시 스크립트가 1 s 뒤 재기동</text>
</svg>
```

그림 2 — 랩 토폴로지. "flash"는 semihosting으로 VM의 파일(`o3_flash.bin`)에 쓰므로 MCU QEMU를 죽였다 살려도 남는다 — 전원 차단 내성 실험이 가능한 이유다.

**코드 판에 대한 메모**: 노트에 실린 코드는 최종판이다. 실험 도중 버그를 고치며 바꾼 곳이 세 군데 있고(MCU 시계 소스 §7.3, 텔레메트리 필드 추가, `o3_fw.c`의 RESET 응답 처리 §4.7), 이전 판으로 잰 출력은 그 자리에 "첫 판" 또는 "첫 번째 부팅"이라고 표시했다. 처리량·RTT 측정은 바뀐 부분과 무관한 경로다.

**숫자에 대한 경고**: 모든 시간 측정은 KVM 없는 TCG 에뮬레이션, 그리고 19명이 공유하는 VM(load average 6~10)에서 나온 값이다. 절대값은 실제 하드웨어와 다르다(대개 훨씬 나쁘다). 이 노트에서 숫자는 "비율과 경향"을 보여 주는 용도이고, 실제 하드웨어 값은 계산식으로 따로 적는다. VM 호스트 커널은 `7.0.0-38-generic`, 게스트 커널은 `6.18.55-dom`이다.

---

## 1. 링크 고르기 — UART vs SPI vs CAN vs USB vs Ethernet

### 1.1 직관 — "누가 말을 꺼낼 수 있나"가 제일 먼저다

링크를 고를 때 대역폭부터 보면 틀린다. 첫 질문은 **MCU가 아무 때나 Linux에게 말을 걸 수 있나**다. IMU 샘플, 에러, 버튼 같은 비동기 이벤트는 MCU에서 생긴다.

- UART · CAN · Ethernet은 **양방향 대칭** — 양쪽 다 아무 때나 보낸다.
- SPI · I2C는 **마스터만 클럭을 만든다.** Linux가 마스터이면 MCU는 "보낼 게 있어요" GPIO(IRQ 선)를 하나 더 써서 Linux를 깨우고, Linux가 읽으러 와야 한다. MCU가 마스터이면 Linux가 SPI slave가 되는데, Linux의 SPI target(slave) 모드는 지원 컨트롤러가 적고 실시간성이 나빠 실무에서 피한다.
- USB는 **호스트가 폴링**한다. MCU(디바이스)는 호스트가 IN 토큰을 줄 때만 보낸다. 다만 그 폴링을 호스트 컨트롤러 HW가 해 주므로 소프트웨어 입장에선 거의 비동기처럼 보인다(interrupt endpoint는 bInterval 주기 보장).

### 1.2 비교표

| 항목 | UART | SPI | CAN / CAN FD | USB (FS/HS) | Ethernet |
|---|---|---|---|---|---|
| 현실적 속도 | 115.2 kbit/s ~ 3~4 Mbit/s | 10~50 MHz 클럭 | 1 Mbit/s / FD 데이터 구간 2~8 Mbit/s | 12 Mbit/s / 480 Mbit/s | 100 Mbit/s ~ 1 Gbit/s |
| 누가 시작 | 양쪽 | 마스터만 (+ IRQ GPIO) | 양쪽 (멀티 마스터, 중재) | 호스트 (폴링) | 양쪽 |
| 선 수 / 거리 | 2~4 (TX RX GND, RTS/CTS) / 보드 안 ~ 수 m (RS-422/485로 더 멀리) | 4 + IRQ / 보드 안 수십 cm | 차동 2선 / 수십 m, 노드 여러 개 | 4선 / 5 m | 2~4 쌍 + magnetics / 100 m |
| 오류 검출 (HW) | parity 정도 — 사실상 없음 | 없음 | 15-bit CRC + ACK 슬롯 + 자동 재전송 + error counter | CRC5/CRC16 + 재전송 (bulk) | FCS CRC-32 (재전송은 상위층) |
| 프레이밍 | 없음 — 바이트 스트림 | CS 구간이 경계 | HW 프레임 (8 B / FD 64 B) | 패킷 / 전송 | 프레임 + IP/UDP |
| DMA / CPU 부하 | 작은 FIFO — 고속이면 DMA 필수 | DMA 쉬움 (길이 고정) | 메일박스 / FIFO, 프레임당 IRQ | 컨트롤러가 다 함 | MAC DMA 링 |
| Linux 쪽 | tty (H8), serdev | spidev 또는 커널 드라이버 (H7) | SocketCAN (Q4) | CDC-ACM(tty), 커스텀 bulk, gadget | 소켓, PTP HW 타임스탬프 |
| MCU 쪽 비용 | 거의 0 | 낮음 | CAN 컨트롤러 + 트랜시버 | USB 스택 (수~수십 KB) | MAC + PHY + lwIP 스택 |
| 잘 맞는 곳 | 보조 MCU 1개, 텔레메트리, 디버그 | 고속 센서 집약, 짧은 거리 | 모터 드라이버 여러 개, 차량, 노이즈 | MCU가 별도 보드·케이블 | 카메라·LiDAR, 고대역, PTP 동기 |

(속도 범위는 일반적인 제품 사양 기준의 기억 기반 값이다 — 실제 칩은 데이터시트로 확인.)

### 1.3 숫자로 고르기 — 대역폭 예산 계산

센서 키트 예: IMU 1 kHz × 32 B, 모터 상태 4축 × 500 Hz × 16 B, 명령 4축 × 500 Hz × 12 B, 로그·진단 약간.

```text
IMU      1000 x (32 + 11 overhead) =  43,000 B/s
motor     2000 x (16 + 11)         =  54,000 B/s
command   2000 x (12 + 11)         =  46,000 B/s (반대 방향)
------------------------------------------------
MCU->Linux ~ 97 kB/s  = 0.78 Mbit/s payload+frame,  UART 8N1 이면 x10/8 = 0.97 Mbit/s
```

UART 8N1은 바이트당 10비트이므로 115200 baud는 11.5 kB/s밖에 안 된다. 이 예산이면 **최소 2 Mbit/s UART에 DMA**, 또는 SPI/CAN FD가 필요하다. 여유율 50% 이상을 두는 것이 원칙이다(재전송·버스트·향후 기능).

| baud (8N1) | 바이트 시간 | 최대 처리량 | 32 B 프레임 1개 전송 시간 |
|---|---|---|---|
| 115200 | 86.8 µs | 11.5 kB/s | 2.8 ms |
| 921600 | 10.9 µs | 92 kB/s | 0.35 ms |
| 3000000 | 3.3 µs | 300 kB/s | 0.11 ms |

### 1.4 판단 요약

- **보조 MCU 한 개 + 수십 kB/s** → UART(가능하면 2~4 Mbit/s + DMA + RTS/CTS). 가장 흔한 답. 이 노트 랩이 이것이다.
- **MCU가 고속 센서를 모아 큰 블록으로** → SPI(Linux 마스터) + "data ready" IRQ GPIO. 길이 고정 트랜잭션이라 DMA가 쉽다.
- **노드 여러 개 · 케이블 · 노이즈 · 차량** → CAN/CAN FD. HW가 CRC·재전송·중재를 해 주므로 위 프로토콜의 절반이 공짜다(Q4).
- **MCU가 따로 떨어진 보드** → USB CDC-ACM이나 Ethernet. Ethernet이면 PTP로 시간 동기까지(Q3).
- **같은 SoC 안의 M 코어** → 선이 아니라 공유 메모리 + mailbox, 즉 rpmsg(O2). 이 노트의 프로토콜 설계 원칙은 그대로 적용된다.

> 함정: "UART에 parity 켜면 오류 검출 되잖아요" — parity는 1비트 오류만, 그것도 바이트 단위로 잡고, 바이트 유실·삽입(overrun, 노이즈 start bit)은 전혀 못 잡는다. **스트림 링크에는 반드시 프레임 CRC가 필요하다.**

---

## 2. MCU 쪽 — 4 KB짜리 "외부 MCU" 펌웨어

### 2.1 무엇을 만드나

Don에게 가장 익숙한 층부터 간다. 실제 STM32/NXP MCU 펌웨어와 구조가 같다: 벡터 테이블 → Reset_Handler(.data 복사, .bss 0) → main 루프, UART RX는 **인터럽트로 링버퍼에 넣고**, main이 링버퍼에서 바이트를 꺼내 프레임으로 조립한다. 주변장치는 QEMU `mps2-an386`의 것을 쓴다.

| 블록 | 주소 / 번호 | 쓰는 레지스터 |
|---|---|---|
| CMSDK APB UART0 | `0x40004000`, RX IRQ = IRQ0 | DATA +0, STATE +4 (bit0 TXFULL, bit1 RXFULL), CTRL +8, INTSTATUS/INTCLEAR +0xC |
| CMSDK TIMER0 | `0x40000000` | CTRL +0, VALUE +4 (25 MHz 다운카운터), RELOAD +8 |
| SysTick | `0xE000E010` | 1 ms 주기 인터럽트 — WFI에서 깨우는 용도 |
| NVIC | `0xE000E100` ISER0 | IRQ0 enable |
| SCB AIRCR | `0xE000ED0C` | `0x05FA0004` = SYSRESETREQ (소프트 리셋) |

(UART0 RX가 IRQ0인 것은 QEMU mps2-an385/386 모델의 배선이다 — 실험에서 ISR이 실제로 불리는 것으로 확인했다. 실제 MPS2 보드 문서와의 일치는 확인 안 함.)

### 2.2 예제 1 — 하드웨어 층 `m_hw.c` (앞부분: 벡터 · ISR)

무엇을 확인하나: 벡터 테이블, UART RX ISR → 2 KB 링버퍼(single producer = ISR, single consumer = main, 인덱스는 32-bit라 원자적), 링버퍼가 차면 버리고 `rx_ovf`를 센다.

```c
/* m_hw.c - mps2-an386 (Cortex-M4 @25 MHz) bare metal: vectors, CMSDK UART0 with an
 * RX-IRQ ring buffer, free-running timer clock, WFI idle, system reset.          */
#include "m_hw.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define U0 0x40004000u                 /* +0 DATA +4 STATE +8 CTRL +0xC INT +0x10 BAUDDIV */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);
static volatile uint8_t rxq[2048];
static volatile uint32_t rx_h, rx_t, ms;
volatile uint32_t rx_ovf;

void Reset_Handler(void)
{
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    main();
    for (;;) ;
}
static void Fault_Handler(void) { sh_puts("FAULT\n"); for (;;) ; }
static void SysTick_Handler(void) { ms++; }
static void UART0RX_Handler(void)
{
    REG(U0 + 0xC) = 2;                 /* INTCLEAR.RX FIRST, then drain (see 2.3) */
    while (REG(U0 + 4) & 2) {          /* STATE.RXFULL */
        uint8_t c = REG(U0);
        if (rx_h - rx_t < sizeof rxq) rxq[rx_h++ % sizeof rxq] = c; else rx_ovf++;
    }
}
__attribute__((section(".vectors"), used)) static void (*const vec[17])(void) = {
    (void (*)(void))&_estack, Reset_Handler, Fault_Handler, Fault_Handler,
    Fault_Handler, Fault_Handler, Fault_Handler, [15] = SysTick_Handler,
    [16] = UART0RX_Handler,            /* IRQ0 = UART0 RX on QEMU mps2-an385/386 */
};
```

### 2.3 예제 2 — `m_hw.c` 뒷부분: 초기화 · 시간 · WFI

```c
void hw_init(void)
{
    REG(U0 + 0x10) = 16;               /* BAUDDIV (QEMU does not pace bytes by it) */
    REG(U0 + 8) = 1 | 2 | 8;           /* TX_EN | RX_EN | RX_INT_EN */
    REG(0xE000E100) = 1u << 0;         /* NVIC_ISER0: IRQ0 */
    REG(0xE000E014) = 25000 - 1;       /* SysTick reload: 1 ms at 25 MHz */
    REG(0xE000E018) = 0;
    REG(0xE000E010) = 7;               /* CLKSOURCE=CPU | TICKINT | ENABLE */
    REG(0x40000008) = 0xFFFFFFFFu;     /* TIMER0 RELOAD */
    REG(0x40000004) = 0xFFFFFFFFu;     /* TIMER0 VALUE */
    REG(0x40000000) = 1;               /* TIMER0 CTRL.EN (no IRQ) */
}
int uart_getc(void)
{
    if (rx_t == rx_h) return -1;
    uint8_t c = rxq[rx_t % sizeof rxq];
    rx_t++;
    return c;
}
void uart_write(const uint8_t *p, int n)
{
    while (n--) { while (REG(U0 + 4) & 1) ; REG(U0) = *p++; }   /* STATE.TXFULL */
}
uint32_t ticks_ms(void) { return ms; }
/* free-running CMSDK TIMER0 (25 MHz down-counter): time no longer depends on how many
 * SysTick interrupts actually got serviced. Must be called at least every 171 s.   */
uint64_t now_us(void)                  /* includes a SIMULATED +100 ppm crystal error */
{
    static uint32_t last, hi;
    uint32_t c = 0xFFFFFFFFu - REG(0x40000004);           /* TIMER0 VALUE */
    if (c < last) hi++;
    last = c;
    uint64_t us = (((uint64_t)hi << 32) | c) / 25;
    return us + us / 10000;
}
/* sleep until an interrupt, without the "byte arrived just before WFI" race */
void idle_wait(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    if (rx_t == rx_h) __asm__ volatile("wfi");
    __asm__ volatile("cpsie i" ::: "memory");
}
void sys_reset(void) { REG(0xE000ED0C) = 0x05FA0004; for (;;) ; }  /* AIRCR.SYSRESETREQ */
```

출력에서 볼 것은 없다(다음 예제에서 빌드). 코드에서 볼 것 세 가지:

- `now_us()`는 **인터럽트 개수를 세지 않고** 자유 계수 타이머(TIMER0)의 값을 읽는다. 처음엔 SysTick 인터럽트 횟수(`ms++`)로 시간을 만들었다가 §7에서 크게 데었다 — 그 이야기는 §7.3.
- `+ us / 10000`은 **일부러 넣은 +100 ppm 크리스탈 오차**다. QEMU 두 대는 같은 호스트 시계를 쓰기 때문에 그냥 두면 drift가 0이라 시간 동기 실험이 재미없다. 실제 MCU 크리스탈은 ±10~50 ppm, 온도에 따라 더 움직인다(기억 기반 — 크리스탈 데이터시트로 확인).
- `idle_wait()`는 `cpsid i` → 링버퍼 확인 → `wfi` → `cpsie i`. 인터럽트를 끈 채로 확인해야 "확인한 직후, WFI 직전에 바이트가 들어와서 다음 SysTick까지 1 ms 잠드는" 경쟁을 막는다. Cortex-M의 WFI는 PRIMASK로 막힌 인터럽트가 pending이 되어도 깨어난다(ARMv7-M 규칙).

semihosting 층(`m_semi.c`, 29줄, 본문 생략)은 `r0 = 동작 번호, r1 = 인자 블록 주소`를 넣고 `bkpt 0xAB`를 실행해 QEMU에게 호스트 파일 I/O를 시킨다 — `SYS_OPEN`(0x01, 모드 3 = "r+b", 7 = "w+b"), `SYS_SEEK`(0x0A), `SYS_READ`(0x06)/`SYS_WRITE`(0x05, 둘 다 "못 처리한 바이트 수"를 돌려줌), `SYS_WRITE0`(0x04, 문자열 출력). 이 파일이 이 랩의 "flash"다. 실제 MCU라면 이 자리에 flash 컨트롤러 드라이버(섹터 erase, 정렬된 program, 완료 폴링)가 들어간다.

### 2.4 예제 3 — 빌드하고 혼자 띄워 보기

무엇을 확인하나: `arm-none-eabi-gcc`로 경고 0개 빌드, 크기, 그리고 MCU를 TCP 시리얼 서버로 띄우고 연결하자마자 나오는 첫 바이트(HELLO 프레임).

```sh
CF="-mcpu=cortex-m4 -mthumb -Os -g -Wall -Wextra -ffunction-sections -nostartfiles \
    --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -T m.ld"
arm-none-eabi-gcc $CF m_hw.c m_semi.c m_flash.c m_main.c -o /var/tmp/O3/o3_mcu.elf
arm-none-eabi-size /var/tmp/O3/o3_mcu.elf
# run_mcu.sh 핵심: UART0 = TCP 서버, semihosting 켜기
qemu-system-arm -M mps2-an386 -nographic -monitor none \
  -semihosting-config enable=on,target=native -kernel o3_mcu.elf \
  -serial tcp:127.0.0.1:40302,server=on,wait=on,nodelay=on
socat -u TCP:127.0.0.1:40302 - | od -An -tx1      # 다른 터미널
```

```text
   text	   data	    bss	    dec	    hex	filename
   4159	      4	   2404	   6567	   19a7	/var/tmp/O3/o3_mcu.elf
```

```text
 03 10 01 04 ff 16 01 03 01 08 01 01 01 01 01 01
 01 01 01 01 01 01 01 01 01 01 01 05 15 6c 88 3d
 00
```

```text
mcu: fw 1.0 up
```

출력에서 볼 것: 코드 4 KB, RAM 2.4 KB(대부분 링버퍼). 첫 바이트 `03`은 COBS 코드("다음 0x00까지 2바이트"), `10`은 헤더 첫 바이트(버전 1, flags 0), `01`은 type=HELLO다. `01 01 01 ...`이 줄지어 있는 것은 0으로 채워진 필드(업데이트 진행 0/0, 에러 카운터 0)를 COBS가 "0 하나 = 코드 0x01 하나"로 바꾼 모습이다. 마지막 `00`이 프레임 구분자다. `mcu: fw 1.0 up`은 semihosting `SYS_WRITE0`로 QEMU stdout에 찍힌 부팅 메시지(디버그 UART 대용)다.

### 2.5 실제로 밟은 버그 — "인터럽트 클리어는 먼저, 비우기는 나중에"

처음 짠 ISR은 이랬다(교과서에 흔한 순서).

```c
static void UART0RX_Handler(void)          /* BUGGY version (m_hw_bug.c) */
{
    while (REG(U0 + 4) & 2) {              /* drain while RXFULL */
        uint8_t c = REG(U0);
        if (rx_h - rx_t < sizeof rxq) rxq[rx_h++ % sizeof rxq] = c; else rx_ovf++;
    }
    REG(U0 + 0xC) = 2;                     /* ...then clear INTSTATUS.RX */
}
```

깨끗한 링크에서는 멀쩡했다. 그런데 §6의 오류 주입(2% 비트 뒤집기 + 2% 드롭, 재전송 버스트가 몰려옴)을 켜자 펌웨어 업데이트가 **매번 멈췄다**. ISR 순서(클리어 먼저/나중) × WFI 방식(PRIMASK 감싸기/맨 WFI) 네 조합을 시드 3개씩 돌렸다.

```sh
for v in A B C D; do for s in 1 2 3; do
  ELF=v_$v.elf SEED=$s bash t_proxy.sh 0.02 0.02 8 | grep -E "exit=|DATA"; done; done
```

```text
variant=A seed=1 exit=3 
variant=A seed=2 exit=3 
variant=A seed=3 exit=3 
variant=B seed=1 exit=0 DATA 65536 B in 6.51 s = 9.8 KiB/s  W=8 retx=160 nak=15 timeout=5 host_crc_err=4 
variant=B seed=2 exit=0 DATA 65536 B in 12.10 s = 5.3 KiB/s  W=8 retx=128 nak=11 timeout=5 host_crc_err=10 
variant=B seed=3 exit=0 DATA 65536 B in 11.54 s = 5.5 KiB/s  W=8 retx=126 nak=11 timeout=5 host_crc_err=12 
variant=C seed=1 exit=3 
variant=C seed=2 exit=3 
variant=C seed=3 exit=3 
variant=D seed=1 exit=0 DATA 65536 B in 3.45 s = 18.6 KiB/s  W=8 retx=128 nak=16 timeout=0 host_crc_err=3 
variant=D seed=2 exit=0 DATA 65536 B in 4.99 s = 12.8 KiB/s  W=8 retx=120 nak=14 timeout=1 host_crc_err=6 
variant=D seed=3 exit=0 DATA 65536 B in 2.91 s = 22.0 KiB/s  W=8 retx=103 nak=12 timeout=1 host_crc_err=8
```

(A = 비우고 나서 클리어 + PRIMASK WFI, B = 클리어 먼저 + PRIMASK WFI, C = 비우고 클리어 + 맨 WFI, D = 클리어 먼저 + 맨 WFI. `exit=3`은 Linux 쪽이 응답 없이 타임아웃을 계속 맞고 포기한 것.)

결과는 깔끔하다: **WFI 방식과 무관하게, "비우고 나서 클리어"는 6/6 멈추고 "클리어 먼저"는 6/6 성공.** 멈춘 MCU에 gdb를 붙여(`-gdb tcp::40303`) 레지스터를 봤다.

```sh
gdb --batch -ex "target remote 127.0.0.1:40303" -ex "x/4wx 0x40004000" \
    -ex "p/x *(unsigned*)0xE000E200" -ex "p rx_h" -ex "p rx_t" -ex "bt 1" o3_mcu_bug.elf
```

```text
idle_wait () at m_hw_bug.c:67
67	    __asm__ volatile("cpsie i" ::: "memory");
0x40004000:	0x000000d3	0x00000002	0x0000000b	0x00000002
$1 = 0x1
$2 = 18473
$3 = 18473
#0  idle_wait () at m_hw_bug.c:67
```

출력에서 볼 것: STATE=`0x2`(RXFULL — 바이트 하나가 UART에 앉아 있다), 링버퍼는 비었고(`rx_h == rx_t`), CPU는 idle 루프에 있다. 즉 **UART 안에 데이터가 있는데 소프트웨어는 그걸 영영 안 꺼낸다.** QEMU의 CMSDK UART 모델은 RXFULL이면 다음 바이트를 받지 않으므로(바이트 1개 버퍼), 이후 링크가 통째로 멈춘다. 일반 원리는 이렇다: "비우기 루프의 마지막 검사"와 "클리어" 사이에 바이트가 들어오면, 그 바이트가 만든 인터럽트 사건을 내가 지워 버린다. 실제 칩에서는 FIFO가 넘치며 overrun으로 나타나거나, 같은 식으로 영구 정지가 된다. 정확히 QEMU 모델 안에서 어느 신호가 어떻게 되는지(gdb에선 INTSTATUS와 NVIC pending 비트가 1로 보였다 — 정지 시점의 스냅숏이라 해석에 주의)는 QEMU 소스로 확인하지 않았다. 확실한 것은 측정 결과와 처방이다.

- **처방**: 래치형(사건을 기억하는) 인터럽트 상태는 **먼저 클리어하고, 그 다음 "비었다"를 확인할 때까지 비운다.** 그러면 클리어 후에 들어온 바이트는 새 인터럽트를 만들거나, 아직 루프 안이면 루프가 가져간다.
- Don의 SSD 경험과 같은 패턴이다: completion queue를 처리할 때 "doorbell/인터럽트 ack 먼저, 그 다음 phase bit가 바뀐 엔트리를 끝까지 소비".
- 그리고 이 버그는 **오류 주입 없이는 안 나왔다.** 재전송 버스트처럼 바이트가 몰려 들어오는 상황에서만 경쟁 창이 열린다. 링크 프로토콜은 반드시 결함 주입으로 시험해야 하는 이유다.

---

## 3. 프레이밍 — 바이트 스트림에서 메시지를 되찾기

### 3.1 직관 — UART는 "끝없는 테이프"다

UART는 바이트를 줄 뿐 "여기가 메시지 시작"이라는 정보가 없다. 수신 측이 중간에 켜지거나, 노이즈로 바이트 하나가 빠지거나 생기면 그 뒤를 어떻게 다시 맞출지(resync)가 프레이밍의 전부다. 세 가지 흔한 방법이 있다.

| 방식 | 구조 | 재동기 | 오버헤드 | 함정 |
|---|---|---|---|---|
| SOF + 길이 + CRC | `0xA5 len ... crc` (N4 예제) | SOF를 찾고 길이만큼 읽은 뒤 CRC 확인, 실패하면 **1바이트 밀어서** 다시 SOF 탐색 | 고정 2~4 B | 페이로드 안의 `0xA5`가 가짜 SOF가 된다. 길이 필드가 깨지면 엉뚱하게 긴 프레임을 기다린다 → 길이 상한 + 바이트 간 타임아웃 필수 |
| SLIP (RFC 1055) | `0xC0`로 끝, 본문의 `0xC0`/`0xDB`는 2바이트로 escape | 다음 `0xC0`에서 즉시 | 데이터 의존, 최악 **2배** | 최악 길이가 2배라 버퍼·대역폭 예산이 어렵다 |
| **COBS** | 0x00을 없애도록 인코딩, `0x00`이 구분자 | 다음 `0x00`에서 즉시 | 최대 254바이트당 1바이트 + 구분자 | 디코딩 전 전체 프레임을 모아야 함(스트리밍 디코딩도 가능하지만 코드가 늘어난다) |

COBS(Consistent Overhead Byte Stuffing)의 아이디어: 데이터를 "0이 아닌 바이트 묶음"으로 자르고, 각 묶음 앞에 "다음 0까지 몇 바이트"를 적는다. 그러면 본문에 0x00이 한 번도 안 나오고, 0x00은 오직 프레임 끝을 뜻한다. 오버헤드가 **데이터와 무관하게 최대 ⌈n/254⌉ + 1바이트**라서, 최대 프레임 크기와 대역폭을 미리 정확히 계산할 수 있다 — 이 노트가 COBS를 고른 이유다.

### 3.2 O3 프레임 형식

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">디코딩된 프레임 (raw, 9 ~ 249 B)</text>
  <rect x="20" y="32" width="90" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="65" y="50" text-anchor="middle" font-size="12">ver4 | flags4</text>
  <text x="65" y="67" text-anchor="middle" font-size="12">1 B</text>
  <rect x="110" y="32" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="50" text-anchor="middle" font-size="12">type</text>
  <text x="140" y="67" text-anchor="middle" font-size="12">1 B</text>
  <rect x="170" y="32" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="50" text-anchor="middle" font-size="12">seq</text>
  <text x="200" y="67" text-anchor="middle" font-size="12">1 B</text>
  <rect x="230" y="32" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="50" text-anchor="middle" font-size="12">ack</text>
  <text x="260" y="67" text-anchor="middle" font-size="12">1 B</text>
  <rect x="290" y="32" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="320" y="50" text-anchor="middle" font-size="12">len</text>
  <text x="320" y="67" text-anchor="middle" font-size="12">1 B</text>
  <rect x="350" y="32" width="190" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="445" y="50" text-anchor="middle" font-size="12">payload</text>
  <text x="445" y="67" text-anchor="middle" font-size="12">0 ~ 240 B</text>
  <rect x="540" y="32" width="120" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="600" y="50" text-anchor="middle" font-size="12">CRC-32 (LE)</text>
  <text x="600" y="67" text-anchor="middle" font-size="12">4 B</text>
  <text x="20" y="98" font-size="12">CRC 범위: 헤더 5 B + payload (CRC 자신 제외)</text>
  <line x1="340" y1="108" x2="340" y2="138" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="352" y="128" font-size="12">COBS 인코딩 + 구분자</text>
  <text x="20" y="160" font-size="13">선 위의 모습 (wire, 최대 251 B)</text>
  <rect x="20" y="170" width="60" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="50" y="194" text-anchor="middle" font-size="12">code</text>
  <rect x="80" y="170" width="480" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="320" y="194" text-anchor="middle" font-size="12">0x00 이 하나도 없는 본문 (code 바이트가 중간중간 끼어 있음)</text>
  <rect x="560" y="170" width="100" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="610" y="194" text-anchor="middle" font-size="12">0x00 = 끝</text>
  <text x="20" y="238" font-size="12">flags: bit0 REL (ACK 필요) · bit1 NAK.  type: HELLO PING PONG TELEM FW_BEGIN FW_DATA FW_END</text>
  <text x="20" y="256" font-size="12">FW_COMMIT RESET ACK.  seq: 송신 번호, ack: "여기까지 받았다" (누적, 피기백)</text>
</svg>
```

그림 3 — O3 프레임. 버전은 매 프레임 헤더 4비트에 넣었다(HELLO에도 별도로 있다). 길이 필드는 COBS가 경계를 주므로 사실 중복이지만 **"디코딩 길이 == 5 + len + 4"** 검사로 한 번 더 걸러 준다.

설계 결정과 이유:

- **최대 payload 240 B** — 디코딩 프레임이 249 B라 COBS 블록 하나(254) 안에 들어가고, MCU 쪽 수신 버퍼 251 B가 고정된다. 큰 데이터(펌웨어)는 232 B 조각 + 4 B 오프셋으로 나눈다. 프레임이 짧을수록 비트 오류 하나가 버리는 양이 적고 재전송이 싸다.
- **CRC-32(IEEE 802.3, 반사형 0xEDB88320)** — 240 B까지의 프레임에서 CRC-16-CCITT도 3비트 오류까지는 잡지만(해밍 거리 4, 길이 범위는 다항식별 표 확인 필요), 32비트는 무작위 오류의 미검출 확률이 2^-32 ≈ 2.3×10^-10으로 16비트(1.5×10^-5)보다 5자리 낮다. 펌웨어 이미지처럼 "틀리면 벽돌"인 데이터가 같은 링크를 지나므로 32비트를 골랐다. MCU에 HW CRC 유닛이 있으면(STM32 등 다수) 비용도 거의 없다.
- **seq/ack 8비트** — 윈도우가 최대 8이므로 모듈로 256이면 충분하다(윈도우 < 128이어야 "옛 번호/새 번호"가 모호하지 않다).
- **버전 4비트를 모든 프레임에** — 버전이 다르면 `o3_unpack`이 -3을 돌려 조용히 버린다. 호환되지 않는 펌웨어끼리 엉뚱한 명령을 실행하는 일이 없다.

### 3.3 예제 4 — 공용 헤더 `o3_cobs.h` (CRC-32 + COBS)

무엇을 확인하나: 테이블 없는 CRC-32(코드 크기 우선 — 테이블 1 KB를 아낀다)와 COBS 인코더/디코더. MCU와 Linux가 **같은 파일**을 include한다. 양쪽 구현이 갈라지는 것이 프로토콜 버그의 1순위 원인이기 때문이다.

```c
/* o3_cobs.h - CRC-32 (IEEE 802.3) + COBS, shared by MCU (arm-none-eabi) and Linux.
 * No malloc, no tables: fits in a few hundred bytes of MCU flash.            */
#ifndef O3_COBS_H
#define O3_COBS_H
#include <stdint.h>
#include <string.h>

/* incremental CRC-32: c = o3_crc32_upd(0xFFFFFFFF, ...) ... final = ~c */
static inline uint32_t o3_crc32_upd(uint32_t c, const uint8_t *p, int n)
{
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
    }
    return c;
}
static inline uint32_t o3_crc32(const uint8_t *p, int n)
{
    return ~o3_crc32_upd(0xFFFFFFFFu, p, n);
}

/* COBS encode n bytes -> out (needs n + n/254 + 2). Appends the 0x00 delimiter.
 * Returns the number of bytes written including the delimiter.              */
static inline int cobs_enc(const uint8_t *in, int n, uint8_t *out)
{
    int ci = 0, o = 1;
    uint8_t code = 1;
    for (int i = 0; i < n; i++) {
        if (in[i]) { out[o++] = in[i]; code++; }
        if (!in[i] || code == 0xFF) { out[ci] = code; ci = o++; code = 1; }
    }
    out[ci] = code;
    out[o++] = 0;
    return o;
}

/* COBS decode n bytes (delimiter NOT included). Returns length or -1. */
static inline int cobs_dec(const uint8_t *in, int n, uint8_t *out)
{
    int i = 0, o = 0;
    while (i < n) {
        uint8_t code = in[i++];
        if (code == 0 || i + code - 1 > n)
            return -1;
        for (int k = 1; k < code; k++)
            out[o++] = in[i++];
        if (code != 0xFF && i < n)
            out[o++] = 0;
    }
    return o;
}
#endif
```

### 3.4 예제 5 — `o3_frame.h` (헤더 pack/unpack, 바이트 단위 수신기)

```c
/* o3_frame.h - O3 link protocol v1 framing.
 * wire = COBS( hdr[5] | payload[len] | crc32_le[4] ) 0x00
 * hdr  = ver<<4|flags, type, seq, ack, len   (len <= O3_MAXP)                  */
#ifndef O3_FRAME_H
#define O3_FRAME_H
#include "o3_cobs.h"
#define O3_VER  1
#define O3_MAXP 240
#define O3_RAW  (5 + O3_MAXP + 4)
#define O3_WIRE (O3_RAW + O3_RAW / 254 + 2)
enum { F_REL = 1, F_NAK = 2 };                /* needs ACK / negative ACK */
enum { T_HELLO = 1, T_PING, T_PONG, T_TELEM,
       T_FW_BEGIN = 0x20, T_FW_DATA, T_FW_END, T_FW_COMMIT, T_RESET, T_ACK = 0x7F };
enum { ST_OK, ST_BAD, ST_BADCRC, ST_BADOFF, ST_RESUMED };
typedef struct { uint8_t flags, type, seq, ack, len; uint8_t p[O3_MAXP]; } o3_msg;
typedef struct { int n, ovf; uint8_t b[O3_WIRE]; } o3_rx;

static inline void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static inline void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static inline void put64(uint8_t *p, uint64_t v) { put32(p, v); put32(p + 4, v >> 32); }
static inline uint16_t get16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static inline uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }

static inline int o3_pack(const o3_msg *m, uint8_t *wire)
{
    uint8_t raw[O3_RAW];
    raw[0] = O3_VER << 4 | m->flags; raw[1] = m->type;
    raw[2] = m->seq; raw[3] = m->ack; raw[4] = m->len;
    memcpy(raw + 5, m->p, m->len);
    put32(raw + 5 + m->len, o3_crc32(raw, 5 + m->len));
    return cobs_enc(raw, 9 + m->len, wire);
}
/* 0 ok, -1 COBS/short, -2 CRC, -3 version/len mismatch */
static inline int o3_unpack(const uint8_t *enc, int n, o3_msg *m)
{
    uint8_t raw[O3_WIRE];
    int r = cobs_dec(enc, n, raw);
    if (r < 9) return -1;
    if (o3_crc32(raw, r - 4) != get32(raw + r - 4)) return -2;
    if (raw[0] >> 4 != O3_VER || raw[4] != r - 9) return -3;
    m->flags = raw[0] & 15; m->type = raw[1]; m->seq = raw[2];
    m->ack = raw[3]; m->len = raw[4];
    memcpy(m->p, raw + 5, m->len);
    return 0;
}
/* feed one byte; returns 1 when b[0..n) holds a complete frame (delimiter stripped) */
static inline int o3_feed(o3_rx *r, uint8_t c)
{
    if (c == 0) { int done = r->n > 0 && !r->ovf; if (!done) r->n = 0; r->ovf = 0; return done; }
    if (r->n < O3_WIRE) r->b[r->n++] = c; else r->ovf = 1;
    return 0;
}
#endif
```

`o3_feed()`는 바이트 하나씩 먹는 수신 상태 기계다. 0x00이 오면 프레임 완성, 버퍼가 넘치면 `ovf`를 세우고 다음 0x00까지 버린다 — **어떤 쓰레기가 와도 다음 구분자에서 반드시 재동기**한다. 정수는 전부 `put32/get32`로 리틀엔디언 직렬화한다. 구조체를 그대로 `memcpy`하면 패딩·엔디언·정렬이 컴파일러와 CPU마다 달라진다(Cortex-M과 aarch64는 둘 다 LE지만, 그걸 믿지 않는 것이 습관이어야 한다).

### 3.5 예제 6 — CRC와 COBS는 실제로 얼마나 잡나

무엇을 확인하나: 실제 프레임 하나의 모든 비트를 하나씩 뒤집기(272가지), 그리고 1~4바이트 쓰레기 버스트 100만 번. 어느 층(COBS 구조 / CRC / 헤더 검사)에서 걸리는지 센다.

```c
/* t_frame.c - what does COBS + CRC-32 actually catch? Flip every single bit of a
 * real wire frame, then hit it with 1e6 random 1..4-byte garbage bursts.        */
#include "o3_frame.h"
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    o3_msg m = { .flags = F_REL, .type = T_FW_DATA, .seq = 42, .ack = 7, .len = 24 };
    for (int i = 0; i < 24; i++) m.p[i] = i % 3 ? i * 7 : 0;      /* payload with zeros */
    uint8_t w[O3_WIRE], c[O3_WIRE]; o3_msg out;
    int n = o3_pack(&m, w);
    printf("raw %d B -> wire %d B:", 9 + m.len, n);
    for (int i = 0; i < n; i++) printf(" %02x", w[i]);
    printf("\nroundtrip=%d (0 = ok)\n", o3_unpack(w, n - 1, &out));
    int cnt[5] = {0};                                    /* idx = -rc : 0 ok(undetected) */
    for (int bit = 0; bit < (n - 1) * 8; bit++) {
        memcpy(c, w, n); c[bit / 8] ^= 1 << (bit % 8);
        cnt[-o3_unpack(c, n - 1, &out)]++;
    }
    printf("single-bit flips: %d  undetected=%d cobs=%d crc=%d hdr=%d\n",
           (n - 1) * 8, cnt[0], cnt[1], cnt[2], cnt[3]);
    memset(cnt, 0, sizeof cnt); srand(1);
    for (int t = 0; t < 1000000; t++) {
        memcpy(c, w, n);
        int at = rand() % (n - 1), len = 1 + rand() % 4;
        for (int k = at; k < at + len && k < n - 1; k++) c[k] ^= 1 + rand() % 255;
        cnt[-o3_unpack(c, n - 1, &out)]++;
    }
    printf("1e6 random bursts: undetected=%d cobs=%d crc=%d hdr=%d\n", cnt[0], cnt[1], cnt[2], cnt[3]);
    uint8_t z[240] = {0}, f[240]; memset(f, 0xFF, sizeof f);
    printf("COBS 240 B payload: all-zero -> %d B, all-0xFF -> %d B (incl. delimiter)\n",
           cobs_enc(z, 240, c), cobs_enc(f, 240, c));
    return 0;
}
```

```sh
gcc -std=gnu11 -O2 -Wall -Wextra t_frame.c -o /tmp/o3_t_frame && /tmp/o3_t_frame
```

```text
raw 33 B -> wire 35 B: 06 11 21 2a 07 18 03 07 0e 03 1c 23 03 31 38 03 46 4d 03 5b 62 03 70 77 03 85 8c 07 9a a1 d7 f5 75 5d 00
roundtrip=0 (0 = ok)
single-bit flips: 272  undetected=0 cobs=71 crc=201 hdr=0
1e6 random bursts: undetected=0 cobs=557159 crc=442841 hdr=0
COBS 240 B payload: all-zero -> 242 B, all-0xFF -> 242 B (incl. delimiter)
```

출력에서 볼 것:

- 33 B 프레임이 선 위에서 35 B(code 바이트 1 + 구분자 1). payload에 0이 8개나 있어도 오버헤드는 2바이트다 — 0 자리에 code 바이트가 들어가기 때문이다.
- 단일 비트 오류 272개 중 **71개는 CRC까지 가기도 전에 COBS 구조 검사에서** 걸렸다(code 바이트가 깨지면 길이가 안 맞음). 나머지 201개는 CRC가 잡았다. 미검출 0.
- 100만 번의 버스트도 미검출 0. CRC-32의 무작위 미검출 확률(2^-32)로 보면 100만 번에 기대값 0.0002개다.
- 240 B payload는 내용이 무엇이든 242 B — **최악 길이가 고정**되는 것이 COBS의 장점이다(SLIP이었다면 0xC0만 240개면 480 B).

> 함정: 비트가 뒤집혀 본문에 0x00이 생기면 프레임이 둘로 쪼개진다. 앞 조각은 CRC로 버려지고, 뒤 조각도 버려진다. 수신기가 "쪼개진 조각"을 정상 프레임으로 오인하지 않도록 **최소 길이(9 B) 검사**와 CRC가 둘 다 필요하다.

---

## 4. 신뢰성 — seq · ACK · NAK · 재전송 · 윈도우 · 백프레셔 · 버전

### 4.1 모든 메시지가 같은 대접을 받을 필요는 없다

가장 중요한 설계 결정은 **"이 메시지가 사라지면 다시 보낼 가치가 있나"**로 메시지를 둘로 나누는 것이다.

| 종류 | 예 | 전달 방식 | 이유 |
|---|---|---|---|
| 스트림 (unreliable) | IMU 샘플, 모터 상태, TELEM | seq만 붙이고 ACK 없음. 수신 측은 seq 구멍을 **세기만** 한다 | 10 ms 뒤에 새 값이 온다. 옛 값을 재전송하면 최신 값이 늦어질 뿐 |
| 명령 (reliable) | FW_BEGIN/DATA/END, 설정 변경, RESET | `F_REL` + seq + ACK + 재전송 | 빠지면 상태가 어긋난다 |
| 주기적 상태 명령 | 모터 setpoint (N4 §5) | 재전송 대신 **매 주기 절대값 + 유효 시각**을 다시 보냄 | 재전송보다 다음 주기가 빠르다. 멱등이라 중복도 무해 |
| 질의/응답 | PING/PONG, HELLO | 응답이 곧 ACK. 타임아웃 시 질의를 다시 보냄 | 질의 자체가 멱등 |

### 4.2 메시지 ID 표 (프로토콜 v1)

| type | 방향 | REL | payload | 응답 |
|---|---|---|---|---|
| 0x01 HELLO | 양방향 | 아니오 | 요청: telem on/off 1 B. 응답: proto, fw_ver, rx window, 슬롯·trial, 업데이트 진행(len/off), crc_err, rx_ovf | 요청을 받으면 MCU는 **rx seq를 0으로 리셋**하고 HELLO 응답 |
| 0x02 PING | L→M | 아니오 | t1 (Linux ns) 8 B | PONG |
| 0x03 PONG | M→L | 아니오 | t1 echo, t2, t3 (MCU µs) 24 B | — |
| 0x04 TELEM | M→L | 아니오 | 샘플 번호, MCU 시각, SysTick ms | — |
| 0x20 FW_BEGIN | L→M | 예 | len, CRC-32, ver | ACK + 이어받을 offset |
| 0x21 FW_DATA | L→M | 예 | offset 4 B + 데이터 ≤ 232 B | ACK |
| 0x22 FW_END | L→M | 예 | — | ACK(OK / BADCRC) |
| 0x23 FW_COMMIT | L→M | 예 | — | ACK |
| 0x24 RESET | L→M | 예 | — | ACK 후 MCU 리셋 (재부팅 HELLO가 2차 응답) |
| 0x7F ACK | M→L | — | status 1 B (+ 추가) | flags `F_NAK`면 NAK |

새 메시지를 추가할 때 규칙: **type 번호는 재사용 금지**, 모르는 type은 무시(ACK도 안 함 → 송신 측 타임아웃이 "모르는 명령"을 알려 줌, 더 낫게는 status `ST_BAD` ACK), payload 끝에 필드를 덧붙이는 것은 하위 호환, 필드 의미 변경은 proto 버전 증가.

### 4.3 수신 측 규칙 (MCU) — 그리고 송신 측 규칙 (Linux)

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13">수신 측 (MCU) — 프레임 하나가 완성될 때마다</text>
  <rect x="20" y="32" width="150" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="95" y="57" text-anchor="middle" font-size="12">0x00 수신 → unpack</text>
  <line x1="170" y1="52" x2="198" y2="52" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="200" y="32" width="150" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="275" y="50" text-anchor="middle" font-size="12">CRC/COBS 실패</text>
  <text x="275" y="65" text-anchor="middle" font-size="12">→ NAK (한 번만)</text>
  <line x1="95" y1="72" x2="95" y2="98" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="100" width="150" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="95" y="125" text-anchor="middle" font-size="12">REL 프레임: seq 비교</text>
  <line x1="170" y1="120" x2="198" y2="120" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="200" y="100" width="150" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="275" y="118" text-anchor="middle" font-size="12">seq == 기대값</text>
  <text x="275" y="133" text-anchor="middle" font-size="12">실행, 기대값++, ACK</text>
  <line x1="95" y1="140" x2="95" y2="168" stroke="currentColor"/>
  <line x1="95" y1="168" x2="198" y2="168" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="200" y="150" width="150" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="275" y="168" text-anchor="middle" font-size="12">seq &lt; 기대값 (중복)</text>
  <text x="275" y="183" text-anchor="middle" font-size="12">실행 안 함, 재ACK</text>
  <line x1="95" y1="168" x2="95" y2="218" stroke="currentColor"/>
  <line x1="95" y1="218" x2="198" y2="218" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="200" y="200" width="150" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="275" y="218" text-anchor="middle" font-size="12">seq &gt; 기대값 (구멍)</text>
  <text x="275" y="233" text-anchor="middle" font-size="12">버림 + NAK 한 번</text>
  <text x="370" y="20" font-size="13">송신 측 (Linux) — Go-Back-N</text>
  <rect x="370" y="32" width="290" height="208" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="385" y="56" font-size="12">base = 가장 오래된 미확인, nxt = 다음 seq</text>
  <text x="385" y="80" font-size="12">nxt - base &lt; W 면 새 프레임 + 사본 보관</text>
  <text x="385" y="104" font-size="12">ACK(a) 가 창 안이면 base = a+1</text>
  <text x="385" y="128" font-size="12">NAK: base 부터 nxt 앞까지 즉시 전부 재송신</text>
  <text x="385" y="152" font-size="12">300 ms 동안 아무것도 없음: 전부 재송신</text>
  <text x="385" y="176" font-size="12">HELLO 수신: MCU 재부팅 → 세션 재시작</text>
  <text x="385" y="200" font-size="12">  (FW_BEGIN 이 이어받을 offset 을 알려 줌)</text>
  <text x="385" y="224" font-size="12">타임아웃 200회 누적: 링크 사망 보고</text>
  <text x="20" y="272" font-size="12">NAK 을 "한 번만" 보내는 이유: 구멍 뒤의 프레임 7개마다 NAK 이 나가면 송신 측이 윈도우를</text>
  <text x="20" y="292" font-size="12">7번 재송신한다 (재전송 폭주). 다음 정상 프레임을 받으면 nak_sent 를 풀어 다시 NAK 가능.</text>
  <text x="20" y="316" font-size="12">중복에 재ACK 하는 이유: ACK 가 사라졌을 수 있다. 실행은 안 한다 (멱등성 보장).</text>
</svg>
```

그림 4 — 수신/송신 상태 기계. TCP의 축소판이다. MCU는 순서가 맞는 프레임만 받는다(수신 버퍼가 하나뿐이라 순서 바뀜을 저장할 RAM이 없다 — 이것이 Selective Repeat 대신 Go-Back-N을 고른 이유).

Don의 SSD 경험과 연결하면: NVMe는 링크(PCIe)가 TLP 단에서 CRC(LCRC)와 ACK/NAK 재전송(Data Link Layer의 replay buffer)을 해 주기 때문에 NVMe 명령층은 재전송을 신경 쓰지 않는다. UART에는 그런 층이 없으니 **PCIe DLL의 replay buffer를 소프트웨어로 직접 만드는 것**이 이 절이다. `win[256]` 배열이 바로 replay buffer다.

### 4.4 예제 7 — MCU 쪽 `m_main.c` (앞부분: 송신 · HELLO · 수신 규칙)

```c
/* m_main.c - MCU side of the O3 link: dispatch, cumulative ACK, NAK-once,
 * duplicate re-ACK, PING/PONG timestamps, 100 Hz unreliable telemetry.      */
#include "m_hw.h"
static uint8_t rx_exp, last_st, nak_sent, telem_on;
static uint32_t tseq;
static uint32_t crc_err, gap_drop;
static o3_rx R;

static void send(uint8_t type, uint8_t flags, uint8_t seq, const uint8_t *p, int n)
{
    o3_msg m = { .flags = flags, .type = type, .seq = seq, .ack = (uint8_t)(rx_exp - 1), .len = n };
    uint8_t w[O3_WIRE];
    memcpy(m.p, p, n);
    uart_write(w, o3_pack(&m, w));
}
static void hello(void)
{
    uint8_t p[22];
    p[0] = O3_VER; put16(p + 1, fw_version()); p[3] = 8;     /* proto, fw, rx window */
    boot_info(p + 4); put32(p + 14, crc_err); put32(p + 18, rx_ovf);
    send(T_HELLO, 0, 0, p, 22);
}
static void nak(void) { if (!nak_sent) { nak_sent = 1; send(T_ACK, F_NAK, 0, &last_st, 1); } }
static void handle(const o3_msg *m, uint64_t t2)
{
    uint8_t p[24]; int n = 1;
    if (m->type == T_HELLO) { rx_exp = 0; nak_sent = 0; telem_on = m->len && m->p[0]; hello(); return; }
    if (m->type == T_PING) {
        memcpy(p, m->p, 8); put64(p + 8, t2); put64(p + 16, now_us());
        send(T_PONG, 0, m->seq, p, 24); return;
    }
    if (!(m->flags & F_REL)) return;
    if (m->seq != rx_exp) {
        if ((uint8_t)(rx_exp - 1 - m->seq) < 128) send(T_ACK, 0, 0, &last_st, 1);  /* dup */
        else { gap_drop++; nak(); }                                             /* gap */
        return;
    }
    p[0] = last_st = fw_cmd(m, p + 1, &n);
    rx_exp++; nak_sent = 0;
    send(T_ACK, 0, 0, p, n);
    if (m->type == T_RESET) sys_reset();
}
```

### 4.5 예제 8 — `m_main.c` 뒷부분: main 루프

```c
int main(void)
{
    hw_init();
    boot_select();
    sh_puts(fw_version() == 0x0100 ? "mcu: fw 1.0 up\n" : "mcu: fw (updated) up\n");
    hello();
    uint64_t next = now_us();
    for (;;) {
        int c;
        while ((c = uart_getc()) >= 0) {
            if (!o3_feed(&R, c)) continue;
            uint64_t t2 = now_us(); o3_msg m;
            if (o3_unpack(R.b, R.n, &m) == 0) handle(&m, t2); else { crc_err++; nak(); }
            R.n = 0;
        }
        if (telem_on && now_us() >= next) {
            uint8_t p[16]; put32(p, tseq); put64(p + 4, now_us()); put32(p + 12, ticks_ms());
            send(T_TELEM, 0, tseq++, p, 16); next += 10000;
        }
        idle_wait();
    }
}
```

코드에서 볼 것:

- `last_st`를 기억했다가 중복 프레임에 다시 돌려준다. FW_END 응답(OK/BADCRC)의 ACK가 사라지면 Linux가 FW_END를 다시 보내는데, MCU는 **검증을 다시 하지 않고** 저장해 둔 결과만 다시 보낸다. "같은 요청 두 번 = 한 번"(멱등)이 재전송 설계의 핵심이다.
- `RESET`은 ACK를 보내고 나서 리셋한다. 그래도 ACK가 사라질 수 있다 — 그래서 송신 측은 "재부팅 HELLO"도 RESET의 성공 응답으로 받아들인다(§4.7에서 이걸 안 해서 생긴 버그를 본다).
- PONG의 t2는 프레임의 0x00이 **main 루프에서 처리된 시각**이다. ISR에서 찍으면 더 정확하지만(링버퍼 대기 시간 제외) 코드가 늘어난다. 정밀 동기에서는 ISR 타임스탬프나 타이머 input capture를 쓴다(§7.5).
- 텔레메트리는 100 Hz, ACK 없음, 자기 일련번호. Linux는 구멍만 센다.

### 4.6 예제 9 — Linux 쪽 송신기 `o3_fw.c` (Go-Back-N + 이어받기)

무엇을 확인하나: 윈도우 W, 고정 RTO 300 ms, NAK 즉시 재송신, MCU 재부팅(HELLO) 감지 시 세션 재시작 + 이어받기, RESET 후 새 버전 확인, COMMIT.

```c
/* o3_fw.c - MCU firmware update over the lossy link: Go-Back-N window W, fixed RTO,
 * fast retransmit on NAK, resume from the MCU's persisted offset after a reboot.   */
#include "o3_link.h"
#include <unistd.h>
#define CHUNK 232
static o3_msg win[256], ack, h;     /* h = last HELLO seen */
static int fd, W; static uint8_t base, nxt; static long retx, naks, tmo;
static void tx(uint8_t type, const uint8_t *p, int n)
{
    o3_msg *m = &win[nxt]; m->flags = F_REL; m->type = type; m->seq = nxt++; m->len = n;
    memcpy(m->p, p, n); link_send(fd, m);
}
static void resend(void) { for (uint8_t s = base; s != nxt; s++) { link_send(fd, &win[s]); retx++; } }
static int drain(int lim)             /* until <= lim frames in flight; -2 = MCU rebooted */
{
    o3_msg m;
    while ((uint8_t)(nxt - base) > lim) {
        if (!link_poll(fd, &m, 300)) { if (++tmo > 200) { fprintf(stderr, "link dead\n"); exit(3); } resend(); continue; }
        if (m.type == T_HELLO) { h = m; return -2; }      /* MCU rebooted */
        if (m.type != T_ACK) continue;
        if ((uint8_t)(m.ack - base) < (uint8_t)(nxt - base)) { base = m.ack + 1; ack = m; }
        if (m.flags & F_NAK) { naks++; resend(); }
    }
    return ack.p[0];
}
int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s DEV IMAGE WINDOW VER [nocommit]\n", argv[0]); return 2; }
    static uint8_t img[128 * 1024]; uint8_t p[O3_MAXP];
    FILE *f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 1; }
    uint32_t len = fread(img, 1, sizeof img, f), crc = o3_crc32(img, len); fclose(f);
    fd = link_open(argv[1]); W = atoi(argv[3]);
    int64_t t0 = now_ns(); uint32_t sent = 0;
again:
    if (link_hello(fd, &h, 0)) { fprintf(stderr, "no HELLO\n"); return 1; }
    base = nxt = 0;
    fprintf(stderr, "HELLO fw=%u.%u slot=%u trial=%u upd=%u/%u\n", h.p[2], h.p[1], h.p[4], h.p[5], get32(h.p + 10), get32(h.p + 6));
    put32(p, len); put32(p + 4, crc); put16(p + 8, atoi(argv[4]));
    tx(T_FW_BEGIN, p, 10);
    int st = drain(0); if (st == -2) goto again;
    uint32_t off = get32(ack.p + 1);
    fprintf(stderr, "BEGIN len=%u crc=%08x -> st=%d start_off=%u\n", len, crc, st, off);
    for (; off < len; off += CHUNK) {
        int k = len - off < CHUNK ? len - off : CHUNK;
        put32(p, off); memcpy(p + 4, img + off, k); tx(T_FW_DATA, p, 4 + k); sent += k;
        if (drain(W - 1) == -2) goto again;
    }
    if (drain(0) == -2) goto again;
    double s = (now_ns() - t0) / 1e9;
    fprintf(stderr, "DATA %u B in %.2f s = %.1f KiB/s  W=%d retx=%ld nak=%ld timeout=%ld host_crc_err=%ld\n",
            sent, s, sent / 1024.0 / s, W, retx, naks, tmo, st_crc);
    tx(T_FW_END, p, 0);   if ((st = drain(0)) != ST_OK) { fprintf(stderr, "END st=%d\n", st); return 1; }
    tx(T_RESET, p, 0);                 /* its ACK may be lost: the reboot HELLO also answers */
    if (drain(0) != -2) while (link_poll(fd, &h, 3000) && h.type != T_HELLO) ;
    fprintf(stderr, "after reset: fw=%u.%u slot=%u trial=%u\n", h.p[2], h.p[1], h.p[4], h.p[5]);
    if (argc > 5) return 0;
    link_hello(fd, &h, 0); base = nxt = 0; tx(T_FW_COMMIT, p, 0); drain(0);
    fprintf(stderr, "COMMIT done\n");
    return 0;
}
```

### 4.7 실제로 밟은 버그 두 개 — 응답의 응답이 사라질 때

첫 번째 게스트 실행에서 5% 오류 조건 업데이트가 이렇게 끝났다(첫 판 코드, 실제 출력).

```text
DATA 98304 B in 13.10 s = 7.3 KiB/s  W=8 retx=370 nak=39 timeout=8 host_crc_err=19
after reset: fw=2.0 slot=1 trial=0
COMMIT done
```

새 펌웨어 3.0을 올렸는데 "after reset"이 옛 버전 2.0을 보고했다. 그런데 다음 실행의 HELLO는 `fw=3.0`이었다 — 업데이트는 성공했고 **보고가 틀렸다.** 원인: RESET의 ACK가 프록시에서 버려짐 → `drain()`이 타임아웃 후 RESET을 재송신 → 그사이 MCU가 재부팅해 보낸 HELLO를 `drain()`이 "-2(재부팅)"로 먹어 버림 → 다음 줄의 "HELLO 기다리기"는 3초 동안 아무것도 못 받고, 변수 `h`에는 실행 초반의 옛 HELLO가 남아 있었다. 고친 코드가 위의 `if (drain(0) != -2) ...`와 `h = m`이다. 고친 뒤 같은 조건(시드 12)의 결과:

```text
DATA 98304 B in 17.12 s = 5.6 KiB/s  W=8 retx=344 nak=31 timeout=13 host_crc_err=25
after reset: fw=3.0 slot=1 trial=1
COMMIT done
```

두 번째: 전원 차단 실험(§8.4)에서 HELLO가 세 번 찍혔는데 MCU 재부팅은 한 번뿐이었다. 원인(추정 — 바이트 단위로 추적하진 않았다): 재부팅 HELLO 하나, 그리고 `link_hello()`가 응답이 500 ms 안에 안 오면 **HELLO 요청을 다시 보내는** 구조라 요청이 두 개 나갔고, MCU는 두 요청 모두에 "rx seq = 0 리셋 + HELLO 응답"을 했다. 두 번째 리셋이 진행 중인 세션을 한 번 더 끊었다. 이 코드는 다음 HELLO에서 다시 이어받아 결국 성공했지만, 제품이라면 고쳐야 한다: **HELLO 요청에 세션 번호(nonce)를 넣고, MCU는 응답에 그 번호를 되돌려 주고, 같은 nonce의 중복 요청에는 리셋 없이 응답만** 한다. Linux는 현재 nonce가 아닌 HELLO 응답은 버린다. (이 개선은 이 노트 코드에는 넣지 않았다 — 연습문제 4.)

### 4.8 백프레셔 — 받는 쪽이 못 따라오면

- **윈도우 = 크레딧.** MCU가 HELLO에 "rx window = 8"을 알려 준다. 근거: 링버퍼 2048 B ÷ 최대 프레임 251 B ≈ 8. Linux는 이 이상 미확인 프레임을 띄우지 않으므로 MCU 링버퍼가 넘칠 수 없다. MCU가 flash를 지우느라 느려지면 ACK가 늦게 나가고, 송신 측은 자연히 멈춘다(ACK clocking).
- **HW 흐름 제어(RTS/CTS)** 는 바이트 단위 백프레셔다. 고속 UART(≥ 1 Mbit/s)에서 MCU가 DMA 재장전 사이에 잠깐 못 받는 순간을 막는다. 프로토콜 윈도우와 함께 쓴다.
- **스트림 방향(MCU→Linux 텔레메트리)은 백프레셔 대신 버린다.** Linux 앱이 멈춰 tty 버퍼(N_TTY 4 KB)가 차면 커널이 throttle을 걸고(H8 §4.4), 그래도 넘치면 바이트가 사라진다. 그때 텔레메트리 seq 구멍 카운터가 올라간다 — 그것이 모니터링 지표다(Q7).
- **오래된 명령 버리기.** 큐에 쌓인 setpoint는 전부 보내지 말고 최신 것만(N4 §5 "최신값만 유지").

### 4.9 버전 협상 — HELLO 한 번에 결정할 것

MCU가 보내는 HELLO의 `proto`, `fw_ver`, `slot`, `trial`, 업데이트 진행도를 보고 Linux 데몬이 결정한다.

1. `proto`가 지원 범위 밖 → 모터 ARM 금지(N4 §9), 업데이트 경로로.
2. `fw_ver`가 rootfs에 동봉된 기대 버전과 다름 → 업데이트(§8).
3. `trial = 1` → "새 펌웨어 시험 부팅 중" → 자가 점검(센서 응답, heartbeat 왕복) 후 COMMIT.
4. `upd_off > 0` → 이전 업데이트가 중간에 끊겼음 → 같은 이미지로 이어받기.

---

## 5. Linux 쪽 — tty를 "8비트 깨끗한 파이프"로 만들고 기다리는 법

### 5.1 게스트에서 본 링크 장치

무엇을 확인하나: QEMU `pci-serial`이 Linux에서 어떻게 보이는지, 8250 드라이버가 FIFO를 어떻게 잡았는지, 열기 전 기본 termios가 무엇인지. (rootfs-dom 게스트, 6.18.55-dom)

```sh
lspci -nn | grep -i serial
dmesg | grep -i "ttyS0"
cat /proc/tty/driver/serial | head -2
ls /sys/class/tty/ttyS0/
cat /sys/class/tty/ttyS0/rx_trig_bytes /sys/class/tty/ttyS0/xmit_fifo_size /sys/class/tty/ttyS0/uartclk
stty -F /dev/ttyS0 -a | head -4
```

```text
00:02.0 Serial controller [0700]: Red Hat, Inc. QEMU PCI 16550A Adapter [1b36:0002] (rev 01)
[   15.782007] 0000:00:02.0: ttyS0 at I/O 0x1120 (irq = 18, base_baud = 115200) is a 16550A
serinfo:1.0 driver revision:
0: uart:16550A port:00001120 irq:18 tx:0 rx:0 CTS|DSR|CD
close_delay     dev      iomem_base       port           type
closing_wait    device   iomem_reg_shift  power          uartclk
console         flags    irq              rx_trig_bytes  uevent
custom_divisor  io_type  line             subsystem      xmit_fifo_size
8
16
1843200
speed 9600 baud; rows 0; columns 0; line = 0;
intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>;
eol2 = <undef>; swtch = <undef>; start = ^Q; stop = ^S; susp = ^Z; rprnt = ^R;
werase = ^W; lnext = ^V; discard = ^O; min = 1; time = 0;
```

출력에서 볼 것:

- 16550A, FIFO 16 B, **RX 트리거 8 B**(`rx_trig_bytes`). 6.18 소스 `drivers/tty/serial/8250/8250_port.c`의 `[PORT_16550A]` 항목이 `.fcr = UART_FCR_ENABLE_FIFO | UART_FCR_R_TRIG_10`, `.rxtrig_bytes = {1, 4, 8, 14}`이다 — `R_TRIG_10`이 8바이트다.
- `uartclk` 1.8432 MHz = 16 × 115200. 클래식 PC UART 클럭이다.
- 기본 termios는 **9600 baud, canonical, echo 켜짐**(`stty -a`의 나머지 줄에 `icanon echo`가 있다). 이 상태로 바이너리 프레임을 보내면 0x0D가 0x0A로 바뀌고, 0x03(^C)이 SIGINT가 되고, 0x11/0x13이 XON/XOFF로 먹힌다(H8 예제 6에서 직접 봤다). **앱이 열 때마다 raw로 설정**해야 한다.

### 5.2 예제 10 — `o3_link.c`: raw termios + epoll + 프레임 조립

무엇을 확인하나: `cfmakeraw()` + `VMIN=0, VTIME=0` + `O_NONBLOCK`으로 "있으면 주고 없으면 바로 돌아오는" read를 만들고, 기다림은 `epoll_wait()`에 맡긴다. 한 번의 `read()`로 최대 512 B를 받아 `o3_feed()`에 흘린다. t4 타임스탬프(`last_rx_ns`)는 read가 돌아온 순간이다.

```c
/* o3_link.c - Linux side: raw termios, epoll wait, byte stream -> frames. */
#include "o3_link.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <poll.h>
long st_crc, st_telem, st_telem_gap, st_tx;
o3_msg last_tl;                             /* newest telemetry frame */
int64_t last_rx_ns;
static int ep = -1;
static o3_rx R;

int link_open(const char *dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror(dev); exit(1); }
    struct termios t;
    if (tcgetattr(fd, &t)) { perror("tcgetattr"); exit(1); }
    cfmakeraw(&t);                          /* no echo, no ICRNL, no ISIG: 8-bit clean */
    t.c_cflag |= CLOCAL | CREAD; t.c_cflag &= ~(CRTSCTS | CSTOPB);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;    /* we wait in epoll, not in read() */
    cfsetspeed(&t, getenv("O3_9600") ? B9600 : B115200);
    if (tcsetattr(fd, TCSANOW, &t)) { perror("tcsetattr"); exit(1); }
    tcflush(fd, TCIOFLUSH);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
    if ((ep = epoll_create1(EPOLL_CLOEXEC)) < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev)) { perror("epoll"); exit(1); }
    return fd;
}
void link_send(int fd, const o3_msg *m)
{
    uint8_t w[O3_WIRE]; int n = o3_pack(m, w), o = 0;
    while (o < n) {                         /* tty may accept a partial write */
        ssize_t r = write(fd, w + o, n - o);
        if (r > 0) o += r; else { struct pollfd q = { fd, POLLOUT, 0 }; poll(&q, 1, 10); }
    }
    st_tx++;
}
int link_poll(int fd, o3_msg *m, int timeout_ms)
{
    static uint8_t buf[512]; static int pos, len; static uint32_t tl_next;
    int64_t end = now_ns() + (int64_t)timeout_ms * 1000000;
    for (;;) {
        while (pos < len) {
            if (!o3_feed(&R, buf[pos++])) continue;
            int r = o3_unpack(R.b, R.n, m); R.n = 0;
            if (r) { st_crc++; continue; }
            if (m->type != T_TELEM) return 1;
            if (st_telem++ && get32(m->p) != tl_next) st_telem_gap += get32(m->p) - tl_next;
            tl_next = get32(m->p) + 1; last_tl = *m;
        }
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) { pos = 0; len = n; last_rx_ns = now_ns(); continue; }
        int left = (int)((end - now_ns() + 999999) / 1000000);
        struct epoll_event e;
        if (left <= 0 || epoll_wait(ep, &e, 1, left) == 0) return 0;
    }
}
```

헤더 `o3_link.h`에는 `now_ns()`(CLOCK_MONOTONIC)와 HELLO 핸드셰이크가 있다.

```c
#ifndef O3_LINK_H
#define O3_LINK_H
#include "o3_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
extern long st_crc, st_telem, st_telem_gap, st_tx;
extern int64_t last_rx_ns;
extern o3_msg last_tl;            /* CLOCK_MONOTONIC when the last frame completed */
int link_open(const char *dev);
void link_send(int fd, const o3_msg *m);
int link_poll(int fd, o3_msg *m, int timeout_ms);   /* 1 msg, 0 timeout */
static inline int64_t now_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
/* HELLO handshake: resets the MCU's rx sequence; retried because the link is lossy */
static inline int link_hello(int fd, o3_msg *h, int telem)
{
    for (int i = 0; i < 10; i++) {
        o3_msg q = { .type = T_HELLO, .len = 1, .p = { (uint8_t)telem } };
        link_send(fd, &q);
        int64_t end = now_ns() + 500000000;
        while (now_ns() < end && link_poll(fd, h, 100))
            if (h->type == T_HELLO) return 0;
    }
    return -1;
}
#endif
```

설계 포인트:

| 선택 | 이 코드 | 대안과 언제 쓰나 |
|---|---|---|
| 기다리는 방법 | `VMIN=0 VTIME=0` + epoll | `VMIN=n VTIME=t`(블로킹 read가 "n바이트 또는 바이트 간 t×100 ms"에 돌아옴) — 단일 스레드 간단 도구용. 여러 fd(소켓, timerfd, 다른 UART)를 같이 기다리는 데몬은 epoll이 정답 |
| 타임아웃 | epoll의 ms 타임아웃 | `timerfd`를 epoll에 같이 넣으면 재전송 타이머 여러 개를 이벤트로 다룰 수 있다(A4 참고) |
| 흐름 제어 | 끔 (`~CRTSCTS`) — QEMU 링크엔 선이 없다 | 실제 고속 UART는 `CRTSCTS` 켜기 + MCU도 RTS 구동 |
| 부분 write | 루프 + `poll(POLLOUT)` | tty 출력 버퍼가 차면 non-blocking write는 일부만 쓴다 — 무시하면 프레임이 잘린다 |
| 읽기 크기 | 512 B | 1바이트 read는 syscall이 바이트 수만큼 늘어난다(921600 baud면 초당 9만 번) |

### 5.3 `low_latency` 플래그 — 6.18에서는 아무 일도 안 한다

예전 문서와 블로그는 "`setserial /dev/ttyS0 low_latency`로 지연을 줄여라"라고 한다. 이 플래그는 flip buffer를 workqueue 대신 IRQ 문맥에서 바로 line discipline으로 넘기라는 뜻이었다. 6.18 소스에서 확인했다.

```sh
cd /var/lab/src/linux-6.18.55
grep -rn "low_latency\|LOW_LATENCY" drivers/tty/*.c drivers/tty/serial/serial_core.c \
     include/linux/serial_core.h include/uapi/linux/tty_flags.h include/linux/tty_port.h
sed -n 528,535p drivers/tty/tty_buffer.c
```

```text
include/linux/serial_core.h:525:#define UPF_LOW_LATENCY		((__force upf_t) ASYNC_LOW_LATENCY    /* 13 */ )
include/linux/serial_core.h:551:#define UPF_USR_MASK		((__force upf_t) (UPF_SPD_MASK|UPF_LOW_LATENCY))
include/uapi/linux/tty_flags.h:29:#define ASYNCB_LOW_LATENCY	13 /* Request low latency behaviour */
include/uapi/linux/tty_flags.h:70:#define ASYNC_LOW_LATENCY	(1U << ASYNCB_LOW_LATENCY)
include/uapi/linux/tty_flags.h:79:		ASYNC_LOW_LATENCY)
void tty_flip_buffer_push(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;

	tty_flip_buffer_commit(buf->tail);
	queue_work(system_unbound_wq, &buf->work);
}
```

출력에서 볼 것: 플래그는 정의돼 있고 사용자가 설정할 수 있는 비트(`UPF_USR_MASK`)에도 들어 있지만, tty core(`drivers/tty/*.c`)와 serial core 어디에서도 읽지 않는다. `tty_flip_buffer_push()`는 **항상** `system_unbound_wq`에 작업을 넣는다. 즉 수신 경로에는 언제나 "IRQ → workqueue 스레드 깨우기 → line discipline → 앱 깨우기"라는 두 번의 스케줄링이 있다. 지연을 줄이는 실제 손잡이는 그 스레드들의 우선순위(PREEMPT_RT에서 IRQ 스레드 우선순위, N2), CPU 고정(`isolcpus`, IRQ affinity), 그리고 FIFO 트리거 레벨이다. (일부 드라이버가 개별적으로 이 플래그를 볼 수는 있다 — 쓰는 드라이버 소스를 grep해서 확인할 것.)

### 5.4 FIFO 트리거 레벨과 DMA — 지연 vs CPU 부하

16550의 RX FIFO는 "트리거 레벨만큼 차면 인터럽트", 그보다 적게 들어오고 멈추면 **약 4 문자 시간 뒤 character timeout 인터럽트**를 낸다(16550 계열 데이터시트 동작, 기억 기반). 그래서 짧은 프레임의 마지막 바이트 지연은 이렇다.

| 115200 baud에서 | 트리거 1 B | 트리거 8 B (기본) | 트리거 14 B |
|---|---|---|---|
| 인터럽트 수 (연속 수신 1 kB) | ~1000 | ~125 | ~73 |
| 짧은 프레임(< 트리거) 끝 지연 | ~0 | ≈ 4 × 86.8 µs = 0.35 ms (timeout) | ≈ 0.35 ms |
| 긴 스트림 중 바이트 대기 | 0 | 최대 7 × 86.8 µs = 0.6 ms | 최대 1.1 ms |
| 오버런 위험 (IRQ 지연이 길 때) | 높음 — 1 B 여유가 없음 | 중간 — 8 B 여유 | 낮음 |

DMA(`CONFIG_SERIAL_8250_DMA=y`가 이 커널에 켜져 있다, PL011도 DMA 지원)는 인터럽트를 더 줄이지만, RX DMA는 "버퍼가 다 찼을 때"나 "라인이 쉬었을 때(idle/timeout)"에야 데이터를 넘기므로 짧은 메시지 지연을 오히려 늘릴 수 있다. 규칙: **고속 대량 스트림은 DMA, 짧은 명령·응답 위주는 FIFO 인터럽트 + 낮은 트리거.** 2~4 Mbit/s에서 DMA 없이 1바이트마다 인터럽트면 CPU 하나가 IRQ로 녹는다.

실제로 재 봤다 — 게스트(BusyBox initramfs, Image-dom)에서 트리거 1/14 × baud 9600/115200 네 조합, 각 100회 PING.

```sh
for trig in 1 14; do for b in 115200 9600; do
  echo $trig > /sys/class/tty/ttyS0/rx_trig_bytes
  if [ $b = 9600 ]; then export O3_9600=1; else unset O3_9600; fi
  /o3_ping /dev/ttyS0 100 0 > /tmp/p.csv     # + awk 로 min/p50/max
done; done
```

(HELLO/요약 줄은 생략)

```text
trig=1 baud=115200
rtt_us min 3823.58 p50 16600.8 max 138828
trig=1 baud=9600
rtt_us min 4131.09 p50 15136 max 79884.4
trig=14 baud=115200
rtt_us min 2492.62 p50 11322.5 max 55201.1
trig=14 baud=9600
rtt_us min 2657.63 p50 11197.6 max 71086.7
```

출력에서 볼 것: **차이가 없다.** 9600 baud에서 19 B PING + 35 B PONG이면 실제 선이라면 전송만 56 ms인데, 측정 최솟값이 2.6 ms다. 즉 QEMU의 16550과 CMSDK UART 모델은 baud에 맞춰 바이트 속도를 늦추지 않고, 트리거 효과는 TCG 스케줄링 잡음(수 ms) 아래에 묻힌다. **에뮬레이터로는 UART 타이밍을 측정할 수 없다** — 실제 보드에서 로직 분석기로 TX 핀과 GPIO 마커를 같이 찍어야 한다(§10.4). 위 표의 계산식이 실제 하드웨어용 답이다.

### 5.5 실제로 밟은 함정 — 40 ms의 정체는 Nagle

랩을 처음 연결했을 때(VM 호스트에서 pty ↔ socat ↔ TCP ↔ MCU) RTT가 **정확히 40 ms 근처**에 몰려 있었다. 40 ms는 Linux TCP delayed ACK의 대표값이고, 작은 쓰기를 모아 보내는 Nagle 알고리즘이 그 ACK를 기다린 것이다. QEMU chardev(`nodelay=on`)와 socat(`,nodelay`) 양쪽에 TCP_NODELAY를 켜자 사라졌다.

```sh
# t_rtt.sh: MCU QEMU (-serial tcp:...,server=on${NODELAY}) <- socat PTY <-> TCP${SNODELAY}
NODELAY=            SNODELAY=          bash t_rtt.sh o3_mcu.elf 300
NODELAY=,nodelay=on SNODELAY=,nodelay  bash t_rtt.sh o3_mcu.elf 300
```

```text
qemu chardev=[] socat=[]
p.csv            n= 300 RTT us min= 40909 p50= 41992 p90= 44225 p99=  59897 max=   85127
qemu chardev=[,nodelay=on] socat=[,nodelay]
p.csv            n= 300 RTT us min=   548 p50=  1813 p90=  5021 p99=  20934 max=   23186
```

출력에서 볼 것: 최솟값이 40.9 ms에서 0.55 ms로 — 75배. 이건 에뮬레이터만의 이야기가 아니다. 실무의 같은 함정 두 가지:

- **USB-시리얼 어댑터(FTDI 등)의 latency timer** — 어댑터가 작은 수신 데이터를 모아 두는 시간으로, FTDI는 기본 16 ms다. `/sys/bus/usb-serial/devices/ttyUSB0/latency_timer`에 1을 쓰면 1 ms. 디버그용 USB-UART로 MCU를 붙여 놓고 "RTT가 16 ms 단위로 계단"이면 이것이다.
- **MCU ↔ Linux를 TCP로 잇는 경우**(Ethernet MCU, 또는 시뮬레이터) — 작은 명령 프레임마다 `TCP_NODELAY`.

### 5.6 SPI라면 — spidev로 충분한가, 커널 드라이버가 필요한가

| 기준 | spidev (유저스페이스, H7) | 커널 드라이버 (spi_driver) |
|---|---|---|
| 트랜잭션 시작 | 앱이 `ioctl(SPI_IOC_MESSAGE)` | IRQ 핸들러/threaded IRQ가 즉시 |
| MCU의 "data ready" IRQ | gpiod로 이벤트 대기 → ioctl (유저 깨우기 1회 + syscall) | `request_threaded_irq` → `spi_sync()` — 깨우기 1회 적음 |
| 지연 꼬리 | 앱 스케줄링에 좌우 | IRQ 스레드 우선순위로 제어 |
| 다른 서브시스템 연결 | 직접 (소켓, ROS 2) | IIO/input/hwmon/net 등으로 노출 가능 (I2, I3) |
| 개발·디버그 | 빠름, 크래시해도 커널 안전 | 느림, 버그는 커널 패닉 |
| 판단 | 프로토타입, 수백 Hz 이하, 데이터를 앱 하나가 씀 | kHz급 data-ready, 여러 소비자, 표준 인터페이스 필요 |

UART의 경우도 같은 질문이 있다. **serdev**(H8 §8)는 "UART 끝에 달린 장치"를 DT로 기술하고 커널 드라이버가 tty를 대신 소유하는 프레임워크다(BT HCI, GNSS가 대표). 외부 MCU용 serdev 드라이버를 쓰는 때는 (1) MCU 데이터를 커널 서브시스템(IIO, input, power supply, watchdog)으로 노출해야 할 때, (2) 부팅 초기 유저스페이스 전에 MCU와 대화해야 할 때(예: 전원 시퀀스, 워치독 킥), (3) 여러 프로세스가 MCU 기능을 나눠 써야 해서 커널이 중재해야 할 때다. 그 외엔 이 노트처럼 **유저스페이스 데몬 하나가 tty를 소유하고 나머지는 소켓/ROS 2로 받는 구조**가 가장 흔하고 디버깅하기 쉽다.

---

## 6. 측정 — 왕복 시간, 처리량, 그리고 오류 주입

### 6.1 예제 11 — 측정 도구 `o3_ping.c`

무엇을 확인하나: HELLO 후 N번 PING/PONG을 하며 네 타임스탬프(t1 Linux 송신, t2 MCU 수신, t3 MCU 송신, t4 Linux 수신)를 CSV로 남긴다. 같은 데이터로 RTT(§6.2)와 시계 오프셋(§7)을 둘 다 계산한다.

```c
/* o3_ping.c - HELLO, then N PING/PONG exchanges: CSV t1,t2,t3,t4 for RTT and
 * clock-offset estimation. t1/t4 = Linux CLOCK_MONOTONIC ns, t2/t3 = MCU us.   */
#include "o3_link.h"
#include <unistd.h>
int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s DEV N period_ms [telem]\n", argv[0]); return 2; }
    int fd = link_open(argv[1]), n = atoi(argv[2]), per = atoi(argv[3]);
    o3_msg h, m;
    if (link_hello(fd, &h, argc > 4)) { fprintf(stderr, "no HELLO\n"); return 1; }
    fprintf(stderr, "HELLO proto=%u fw=%u.%u window=%u slot=%u trial=%u upd=%u/%u mcu_crc_err=%u\n",
            h.p[0], h.p[2], h.p[1], h.p[3], h.p[4], h.p[5], get32(h.p + 10), get32(h.p + 6), get32(h.p + 14));
    printf("t1_ns,t2_us,t3_us,t4_ns\n");
    int lost = 0;
    for (int i = 0; i < n; i++) {
        o3_msg q = { .type = T_PING, .seq = (uint8_t)i, .len = 8 };
        int64_t t1 = now_ns();
        put64(q.p, (uint64_t)t1);
        link_send(fd, &q);
        int got = 0;
        while (!got && link_poll(fd, &m, 500))
            got = m.type == T_PONG && (int64_t)get64(m.p) == t1;
        if (got) printf("%lld,%llu,%llu,%lld\n", (long long)t1, (unsigned long long)get64(m.p + 8),
                        (unsigned long long)get64(m.p + 16), (long long)last_rx_ns);
        else lost++;
        if (per) usleep(per * 1000);
    }
    fprintf(stderr, "pings=%d lost=%d host_crc_err=%ld telem=%ld telem_gap=%ld\n",
            n, lost, st_crc, st_telem, st_telem_gap);
    return 0;
}
```

### 6.2 왕복 시간 — 게스트 ttyS0 ↔ MCU

```sh
./o3_ping /dev/ttyS0 1000 0 > rtt_idle.csv                       # 연속 1000회
stress-ng --cpu 2 --timeout 120s --quiet & sleep 2
./o3_ping /dev/ttyS0 1000 0 > rtt_load.csv; kill %1               # 게스트 vCPU 2개 포화
python3 ana_rtt.py r2_idle.csv r2_idle10.csv r1_load.csv          # VM 호스트에서 분석
```

```text
HELLO proto=1 fw=1.0 window=8 slot=0 trial=0 upd=0/0 mcu_crc_err=1
pings=1000 lost=0 host_crc_err=0 telem=0 telem_gap=0
```

```text
r2_idle.csv      n=1000 RTT us min=  2480 p50= 11892 p90= 27318 p99=  55096 max=   84098
r2_idle10.csv    n=1000 RTT us min=  2727 p50= 12580 p90= 30925 p99=  72964 max=  122544
r1_load.csv      n=1000 RTT us min=  2039 p50=  9934 p90= 23497 p99=  51438 max=   83358
```

(`r2_idle10`은 10 ms 간격 PING, `r1_load`는 첫 번째 게스트 부팅에서 stress-ng 아래 잰 것. 분석 스크립트 `ana_rtt.py`는 정렬 후 백분위만 뽑는 9줄짜리라 생략.)

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="230" x2="660" y2="230" stroke="currentColor"/><line x1="80" y1="40" x2="80" y2="230" stroke="currentColor"/><line x1="80" y1="205.1" x2="660" y2="205.1" stroke="#888" stroke-opacity="0.3"/><text x="74" y="209.1" font-size="12" text-anchor="end">1 ms</text><line x1="80" y1="122.6" x2="660" y2="122.6" stroke="#888" stroke-opacity="0.3"/><text x="74" y="126.6" font-size="12" text-anchor="end">10 ms</text>
<line x1="80" y1="40.0" x2="660" y2="40.0" stroke="#888" stroke-opacity="0.3"/><text x="74" y="44.0" font-size="12" text-anchor="end">100 ms</text><rect x="118.5" y="71.1" width="32" height="158.9" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="134.5" y="67.1" font-size="12" text-anchor="middle">42.0</text><rect x="154.5" y="58.4" width="32" height="171.6" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="170.5" y="54.4" font-size="12" text-anchor="middle">59.9</text><text x="152.5" y="248" font-size="13" text-anchor="middle">host pty</text><text x="152.5" y="264" font-size="12" text-anchor="middle">Nagle 켜짐</text><rect x="263.5" y="183.8" width="32" height="46.2" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="279.5" y="179.8" font-size="12" text-anchor="middle">1.8</text><rect x="299.5" y="96.1" width="32" height="133.9" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="315.5" y="92.1" font-size="12" text-anchor="middle">20.9</text><text x="297.5" y="248" font-size="13" text-anchor="middle">host pty</text><text x="297.5" y="264" font-size="12" text-anchor="middle">nodelay</text><rect x="408.5" y="116.4" width="32" height="113.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="424.5" y="112.4" font-size="12" text-anchor="middle">11.9</text><rect x="444.5" y="61.4" width="32" height="168.6" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="460.5" y="57.4" font-size="12" text-anchor="middle">55.1</text><text x="442.5" y="248" font-size="13" text-anchor="middle">게스트</text><text x="442.5" y="264" font-size="12" text-anchor="middle">idle</text><rect x="553.5" y="122.8" width="32" height="107.2" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="569.5" y="118.8" font-size="12" text-anchor="middle">9.9</text><rect x="589.5" y="63.8" width="32" height="166.2" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="605.5" y="59.8" font-size="12" text-anchor="middle">51.4</text><text x="587.5" y="248" font-size="13" text-anchor="middle">게스트</text><text x="587.5" y="264" font-size="12" text-anchor="middle">stress-ng</text><rect x="240" y="282" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="258" y="293" font-size="12">p50 (ms)</text><rect x="360" y="282" width="12" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="378" y="293" font-size="12">p99 (ms)</text>
<text x="80" y="24" font-size="12">PING → PONG 왕복 시간, 로그 축 (TCG 에뮬레이션 실측)</text></svg>
```

그림 5 — RTT 백분위(로그 축, 실측). 왼쪽 두 개는 VM 호스트에서 pty로 직접 붙인 경우(§5.5 Nagle 실험), 오른쪽 두 개는 Linux 게스트의 진짜 8250 드라이버 경로.

출력에서 볼 것:

- 게스트 경로의 중앙값은 약 10~12 ms, p99는 50~70 ms, 최댓값은 80~120 ms다. 최솟값(2~2.5 ms)과 최댓값이 **40배** 차이 난다. TCG라 절대값은 부풀려졌지만 "꼬리가 중앙값의 수 배"라는 모양은 실제 하드웨어에서도 같다(N4 §6에서도 같은 결론).
- 게스트에 stress-ng를 걸어도 나빠지지 않았다(오히려 p50이 낮음). 이 VM에서는 **게스트 내부 부하보다 19명이 공유하는 호스트의 부하(load average 5~15)가 지배적**이라는 뜻이다. 에뮬레이터 측정의 한계 — 비교 실험은 같은 시간대에 번갈아 돌려야 한다.
- 결론은 N4와 같다: 이 링크 위로 제어 루프를 닫지 않는다. 다만 이 노트의 관점에서 중요한 숫자는 **"RTO(재전송 타이머)를 얼마로 잡나"**다. p99가 55~73 ms, 최댓값 122 ms이므로 RTO 300 ms는 "정상 응답을 잃어버렸다고 착각"하지 않을 만큼 크다. 실제 921600 baud 보드라면 RTT가 수백 µs ~ 수 ms이므로 RTO 20~50 ms 정도가 출발점이다(측정해서 p99.9 × 2~3).

### 6.3 처리량 — 윈도우 크기의 효과

같은 64 KiB / 96 KiB 이미지를 윈도우 1, 2, 4, 8로 보냈다(깨끗한 링크).

```sh
./o3_fw /dev/ttyS0 fw_v2.bin 1 512
./o3_fw /dev/ttyS0 fw_v3.bin 2 768
./o3_fw /dev/ttyS0 fw_v2.bin 4 512
./o3_fw /dev/ttyS0 fw_v3.bin 8 768
```

```text
HELLO fw=1.0 slot=0 trial=0 upd=0/0
BEGIN len=65536 crc=e8528385 -> st=0 start_off=0
DATA 65536 B in 6.60 s = 9.7 KiB/s  W=1 retx=0 nak=0 timeout=0 host_crc_err=0
after reset: fw=2.0 slot=1 trial=1
COMMIT done
HELLO fw=2.0 slot=1 trial=0 upd=0/0
BEGIN len=98304 crc=211a4a38 -> st=0 start_off=0
DATA 98304 B in 5.47 s = 17.6 KiB/s  W=2 retx=0 nak=0 timeout=0 host_crc_err=0
after reset: fw=3.0 slot=0 trial=1
COMMIT done
HELLO fw=3.0 slot=0 trial=0 upd=0/0
BEGIN len=65536 crc=e8528385 -> st=0 start_off=0
DATA 65536 B in 2.37 s = 27.0 KiB/s  W=4 retx=0 nak=0 timeout=0 host_crc_err=0
after reset: fw=2.0 slot=1 trial=1
COMMIT done
HELLO fw=2.0 slot=1 trial=0 upd=0/0
BEGIN len=98304 crc=211a4a38 -> st=0 start_off=0
DATA 98304 B in 3.94 s = 24.4 KiB/s  W=8 retx=0 nak=0 timeout=0 host_crc_err=0
after reset: fw=3.0 slot=0 trial=1
COMMIT done
```

출력에서 볼 것:

- W=1(stop-and-wait)은 9.7 KiB/s. 232 B ÷ 9.7 KiB/s ≈ 23 ms — **프레임마다 RTT 한 번을 통째로 기다린다.** W=2에서 거의 두 배, W=4에서 세 배로 오르고, W=8은 W=4와 비슷하다. W=4 근처에서 링크(여기서는 에뮬레이터의 바이트 처리 속도 + MCU의 semihosting 쓰기)가 포화된 것이다.
- 이것이 **대역폭-지연 곱(bandwidth-delay product)** 이다: 필요한 윈도우 ≈ 링크 속도 × RTT ÷ 프레임 크기. 실제 921600 baud(92 kB/s), RTT 2 ms라면 92 kB/s × 2 ms = 184 B → W=1~2면 충분하다. 반대로 Linux 쪽 RTT 꼬리가 길면(부하) W가 커야 처리량이 유지된다. MCU RAM이 허락하는 만큼(링버퍼 ÷ 최대 프레임) 윈도우를 잡는다.
- 업데이트마다 슬롯이 0 → 1 → 0 → 1로 번갈아 가고, 재부팅 직후 `trial=1`, COMMIT 후 다음 HELLO에서 `trial=0`이다. A/B 동작이 매번 확인된다(§8).

### 6.4 예제 12 — 오류 주입기 `o3_proxy.py`

무엇을 확인하나: 프레임(0x00 단위)마다 확률 C로 비트 1개 뒤집기, 확률 D로 프레임 통째로 버리기. MCU 쪽 연결이 끊기면(전원 차단) 다시 붙을 때까지 기다린다. 시드를 고정해서 같은 오류 패턴을 재현한다.

```python
#!/usr/bin/env python3
"""o3_proxy.py - sits on the 'wire' between the Linux guest UART and the MCU UART.
Per COBS frame (ends with 0x00): flip one random bit with prob C, drop with prob D.
Reconnects to the MCU when it 'loses power' (its QEMU exits)."""
import socket, selectors, random, signal, sys, time
LPORT, MPORT, C, D, SEED = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
rng = random.Random(SEED)
st = {"frames": 0, "flipped": 0, "dropped": 0, "mcu_reconnects": 0}

def connect(port):
    while True:
        try:
            s = socket.create_connection(("127.0.0.1", port))
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # no Nagle: 40 ms trap
            return s
        except OSError:
            time.sleep(0.05)

def done(*_):
    print("proxy:", st, file=sys.stderr, flush=True); sys.exit(0)
signal.signal(signal.SIGTERM, done); signal.signal(signal.SIGINT, done)

lin, mcu = connect(LPORT), connect(MPORT)
pend = {"L": bytearray(), "M": bytearray()}
sel = selectors.DefaultSelector()
sel.register(lin, selectors.EVENT_READ, "L"); sel.register(mcu, selectors.EVENT_READ, "M")
while True:
    for key, _ in sel.select():
        side = key.data
        try: data = key.fileobj.recv(4096)
        except OSError: data = b""             # kill -9 of the MCU QEMU -> ECONNRESET
        if not data:
            if side == "L": done()
            sel.unregister(mcu); mcu.close(); pend["M"].clear()
            mcu = connect(MPORT); st["mcu_reconnects"] += 1
            sel.register(mcu, selectors.EVENT_READ, "M"); continue
        out, buf = bytearray(), pend[side]
        for b in data:
            buf.append(b)
            if b: continue
            st["frames"] += 1
            r = rng.random()
            if r < D: st["dropped"] += 1
            else:
                if r < D + C and len(buf) > 1:
                    i = rng.randrange(len(buf) - 1); buf[i] ^= 1 << rng.randrange(8); st["flipped"] += 1
                out += buf
            buf.clear()
        try: (mcu if side == "L" else lin).sendall(out)
        except OSError: pass                   # MCU is "unpowered": bytes fall on the floor
```

### 6.5 오류 아래에서의 복구

```sh
bash ctl.sh proxy 0.01 0.01 11 ; ./o3_fw /dev/ttyS0 fw_v2.bin 8 512 ; bash ctl.sh pstat
bash ctl.sh proxy 0.05 0.05 12 ; ./o3_fw /dev/ttyS0 fw_v3.bin 8 768 ; bash ctl.sh pstat
bash ctl.sh proxy 0.05 0.05 13 ; ./o3_fw /dev/ttyS0 fw_v2.bin 1 512 ; bash ctl.sh pstat
```

```text
DATA 65536 B in 3.32 s = 19.3 KiB/s  W=8 retx=48 nak=6 timeout=0 host_crc_err=4
after reset: fw=2.0 slot=1 trial=1
COMMIT done
proxy: {'frames': 633, 'flipped': 10, 'dropped': 2, 'mcu_reconnects': 0}
```

```text
DATA 98304 B in 17.12 s = 5.6 KiB/s  W=8 retx=344 nak=31 timeout=13 host_crc_err=25
after reset: fw=3.0 slot=1 trial=1
COMMIT done
proxy: {'frames': 1252, 'flipped': 57, 'dropped': 54, 'mcu_reconnects': 0}
```

```text
DATA 65536 B in 22.56 s = 2.8 KiB/s  W=1 retx=66 nak=17 timeout=49 host_crc_err=13
after reset: fw=2.0 slot=1 trial=1
COMMIT done
proxy: {'frames': 697, 'flipped': 32, 'dropped': 36, 'mcu_reconnects': 0}
```

(두 번째 블록은 §4.7의 버그를 고친 뒤 두 번째 부팅에서 같은 시드로 다시 돌린 것. 첫 번째와 세 번째는 첫 번째 부팅.)

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="220" x2="660" y2="220" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="220" stroke="currentColor"/><line x1="60" y1="220.0" x2="660" y2="220.0" stroke="#888" stroke-opacity="0.3"/><text x="54" y="224.0" font-size="12" text-anchor="end">0</text><line x1="60" y1="160.0" x2="660" y2="160.0" stroke="#888" stroke-opacity="0.3"/><text x="54" y="164.0" font-size="12" text-anchor="end">10</text>
<line x1="60" y1="100.0" x2="660" y2="100.0" stroke="#888" stroke-opacity="0.3"/><text x="54" y="104.0" font-size="12" text-anchor="end">20</text><line x1="60" y1="40.0" x2="660" y2="40.0" stroke="#888" stroke-opacity="0.3"/><text x="54" y="44.0" font-size="12" text-anchor="end">30</text><rect x="80.9" y="161.8" width="44" height="58.2" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="102.9" y="157.8" font-size="12" text-anchor="middle">9.7</text>
<text x="102.9" y="238" font-size="13" text-anchor="middle">W=1</text><text x="102.9" y="254" font-size="12" text-anchor="middle">깨끗</text><rect x="166.6" y="114.4" width="44" height="105.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="188.6" y="110.4" font-size="12" text-anchor="middle">17.6</text><text x="188.6" y="238" font-size="13" text-anchor="middle">W=2</text><text x="188.6" y="254" font-size="12" text-anchor="middle">깨끗</text>
<rect x="252.3" y="58.0" width="44" height="162.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="274.3" y="54.0" font-size="12" text-anchor="middle">27.0</text><text x="274.3" y="238" font-size="13" text-anchor="middle">W=4</text><text x="274.3" y="254" font-size="12" text-anchor="middle">깨끗</text><rect x="338.0" y="73.6" width="44" height="146.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="360.0" y="69.6" font-size="12" text-anchor="middle">24.4</text><text x="360.0" y="238" font-size="13" text-anchor="middle">W=8</text><text x="360.0" y="254" font-size="12" text-anchor="middle">깨끗</text><rect x="423.7" y="104.2" width="44" height="115.8" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="445.7" y="100.2" font-size="12" text-anchor="middle">19.3</text><text x="445.7" y="238" font-size="13" text-anchor="middle">W=8</text>
<text x="445.7" y="254" font-size="12" text-anchor="middle">1%+1%</text><rect x="509.4" y="186.4" width="44" height="33.6" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="531.4" y="182.4" font-size="12" text-anchor="middle">5.6</text><text x="531.4" y="238" font-size="13" text-anchor="middle">W=8</text><text x="531.4" y="254" font-size="12" text-anchor="middle">5%+5%</text><rect x="595.1" y="203.2" width="44" height="16.8" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="617.1" y="199.2" font-size="12" text-anchor="middle">2.8</text><text x="617.1" y="238" font-size="13" text-anchor="middle">W=1</text><text x="617.1" y="254" font-size="12" text-anchor="middle">5%+5%</text><text x="60" y="24" font-size="12">펌웨어 전송 처리량 KiB/s (게스트 ttyS0 → MCU, 오류 = 비트 뒤집기% + 드롭%)</text>
<text x="60" y="282" font-size="12">파랑: 깨끗한 링크 · 주황/빨강: 프록시 오류 주입</text></svg>
```

그림 6 — 처리량(실측). 깨끗한 링크에선 윈도우가 처리량을 3배로 만들고, 5% 오류에선 같은 윈도우 8도 4분의 1로 떨어진다.

출력에서 볼 것:

- **모든 실행에서 FW_END의 CRC-32 검증이 통과했다**(END가 실패하면 `END st=2`가 찍히고 종료한다). 1252 프레임 중 57개가 비트 손상, 54개가 사라졌는데도 MCU flash의 이미지는 비트 단위로 정확하다. CRC가 손상을 잡고(MCU 쪽 → NAK, Linux 쪽 → `host_crc_err` 25), seq가 손실을 잡고, 재전송이 메웠다.
- 5%에서 W=8의 재전송 344개 = 프레임 424개의 81%. Go-Back-N은 구멍 하나에 윈도우 전체를 다시 보낸다. 오류율이 높은 링크에서는 **Selective Repeat**(수신 측이 순서 밖 프레임을 보관, MCU RAM 필요)나 **작은 윈도우 + 빠른 NAK**가 낫다. 또는 근본 처방 — 링크 자체를 고친다(차동 신호 RS-422, 접지, 케이블 차폐, baud 낮추기). 프로토콜은 드문 오류를 위한 것이지 나쁜 배선을 위한 것이 아니다.
- W=1 + 5%에서 `timeout=49`: stop-and-wait은 NAK이 사라지면 RTO(300 ms)를 통째로 기다린다. 22.6초 중 약 15초가 타임아웃 대기였다. **NAK(빠른 재전송)이 타임아웃보다 중요한 이유**다.
- 오류 주입 없이 이 결과를 얻을 방법은 없다. 실제 제품에서도 같은 프록시(또는 MCU 펌웨어 안의 디버그용 "드롭 확률" 변수)로 CI에서 돌려야 한다(P6).

---

## 7. 시간 동기 — MCU tick을 CLOCK_MONOTONIC으로 바꾸기

### 7.1 무엇이 새로운가

N4 §7은 "샘플은 생긴 곳(MCU)에서 타임스탬프를 찍고, Linux는 offset + drift 모델로 변환한다"는 원칙과 최소 RTT 필터의 효과를 보였다(그때 "MCU"는 VM 호스트의 프로그램이었다). 여기서는 **진짜 bare-metal 타이머 → 진짜 8250 드라이버 경로**로 같은 일을 하고, 그 과정에서 MCU 쪽 시계를 어떻게 만들어야 하는지(§7.3), 모델을 어떻게 계속 갱신하는지(§7.5), 메시지 대신 HW 펄스를 쓰면 무엇이 달라지는지(§7.6)를 다룬다.

### 7.2 네 타임스탬프와 두 공식

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="140" y="24" text-anchor="middle" font-size="13">Linux (CLOCK_MONOTONIC, ns)</text>
  <text x="520" y="24" text-anchor="middle" font-size="13">MCU (TIMER0, µs)</text>
  <line x1="140" y1="36" x2="140" y2="280" stroke="#4a7bd0" stroke-width="2"/>
  <line x1="520" y1="36" x2="520" y2="280" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="140" y1="70" x2="516" y2="130" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="130" y="74" text-anchor="end" font-size="12">t1 송신</text>
  <text x="530" y="134" font-size="12">t2 수신</text>
  <line x1="520" y1="160" x2="144" y2="250" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="530" y="164" font-size="12">t3 송신</text>
  <text x="130" y="254" text-anchor="end" font-size="12">t4 수신</text>
  <text x="330" y="92" text-anchor="middle" font-size="12">PING (t1)</text>
  <text x="330" y="196" text-anchor="middle" font-size="12">PONG (t1, t2, t3)</text>
  <rect x="200" y="214" width="260" height="58" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="234" text-anchor="middle" font-size="12">offset = ((t2 - t1) + (t3 - t4)) / 2</text>
  <text x="330" y="254" text-anchor="middle" font-size="12">delay = (t4 - t1) - (t3 - t2)</text>
  <text x="330" y="268" text-anchor="middle" font-size="12">오차 상한 = delay / 2</text>
  <text x="540" y="200" font-size="12">MCU 처리 시간</text>
  <text x="540" y="216" font-size="12">t3 - t2 는 빼 준다</text>
</svg>
```

그림 7 — NTP와 같은 4-타임스탬프 교환. 왕복이 대칭이면(가는 길 = 오는 길) offset이 정확하다. 비대칭의 최대치는 delay 전체이므로 오차 상한은 delay/2다. 그래서 **delay가 가장 작은 교환이 가장 믿을 만하다** — 최소 지연 필터의 근거.

단위에 주의: t1, t4는 Linux ns, t2, t3는 MCU µs다. 같은 단위(µs)로 바꾼 뒤 계산하고, offset은 "MCU 시계 − Linux 시계"다.

### 7.3 실제로 데인 것 — "인터럽트를 세는 시계"는 시간을 잃는다

처음 펌웨어의 `now_us()`는 이랬다: SysTick 1 ms 인터럽트마다 `ms++`, 그리고 `ms × 1000 + (현재 카운터 값)`. 이 펌웨어로 1200번 교환한 데이터를 분석하자 이런 숫자가 나왔다(두 번째 게스트 부팅, `r2_ts.csv`).

```text
all samples    n=1200 drift=-248936.0 ppm  residual p50=160084.0 us max=1003457.6 us
min-delay/20   n=  60 drift=-248867.2 ppm  residual p50=169847.8 us max=877819.2 us
```

drift가 −249,000 ppm, 즉 **MCU 시계가 25% 느리다.** 크리스탈 오차(수십 ppm)로는 설명이 안 된다. MCU에 SysTick 카운트와 자유 계수 타이머를 동시에 텔레메트리로 내보내게 하고(`o3_tl.c`), Linux 시각과 나란히 찍었다.

```c
/* o3_tl.c - MCU telemetry: free-running-timer us vs SysTick-counted ms vs Linux time */
#include "o3_link.h"
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s DEV SECONDS\n", argv[0]); return 2; }
    int fd = link_open(argv[1]); o3_msg m;
    if (link_hello(fd, &m, 1)) { fprintf(stderr, "no HELLO\n"); return 1; }
    int64_t t0 = now_ns(), end = t0 + atoll(argv[2]) * 1000000000LL, next = t0;
    long n0 = st_telem; uint64_t us0 = 0; uint32_t ms0 = 0;
    while (now_ns() < end) {
        link_poll(fd, &m, 100);
        if (st_telem == n0) continue;
        uint64_t us = get64(last_tl.p + 4); uint32_t ms = get32(last_tl.p + 12);
        if (!us0) { us0 = us; ms0 = ms; t0 = last_rx_ns; }
        if (now_ns() >= next) {
            double L = (last_rx_ns - t0) / 1e6;     /* Linux ms since first sample */
            printf("linux=%8.0f ms  timer=%8.0f ms  systick=%8u ms  (timer-linux %+6.0f, systick-linux %+7.0f)\n",
                   L, (us - us0) / 1e3, ms - ms0, (us - us0) / 1e3 - L, (ms - ms0) - L);
            next += 5000000000LL;
        }
    }
    fprintf(stderr, "telem=%ld gap=%ld\n", st_telem, st_telem_gap);
    return 0;
}
```

VM 호스트에서 pty로 직접 붙인 경우:

```text
linux=       0 ms  timer=       0 ms  systick=       0 ms  (timer-linux     +0, systick-linux      +0)
linux=    4926 ms  timer=    4917 ms  systick=    3741 ms  (timer-linux     -8, systick-linux   -1185)
linux=    9957 ms  timer=    9959 ms  systick=    7854 ms  (timer-linux     +2, systick-linux   -2103)
linux=   14944 ms  timer=   14947 ms  systick=   11787 ms  (timer-linux     +2, systick-linux   -3157)
linux=   19984 ms  timer=   19987 ms  systick=   15771 ms  (timer-linux     +3, systick-linux   -4213)
linux=   24935 ms  timer=   24938 ms  systick=   19552 ms  (timer-linux     +3, systick-linux   -5383)
linux=   29973 ms  timer=   29977 ms  systick=   23195 ms  (timer-linux     +4, systick-linux   -6778)
telem=3106 gap=0
```

게스트 ttyS0 경로(세 번째 부팅):

```text
linux=       0 ms  timer=       0 ms  systick=       0 ms  (timer-linux     +0, systick-linux      +0)
linux=    4978 ms  timer=    3017 ms  systick=    2581 ms  (timer-linux  -1960, systick-linux   -2397)
linux=    9944 ms  timer=    9990 ms  systick=    8874 ms  (timer-linux    +46, systick-linux   -1070)
linux=   14954 ms  timer=   15000 ms  systick=   13192 ms  (timer-linux    +46, systick-linux   -1762)
linux=   19971 ms  timer=   20020 ms  systick=   17170 ms  (timer-linux    +49, systick-linux   -2801)
linux=   24882 ms  timer=   24932 ms  systick=   21155 ms  (timer-linux    +50, systick-linux   -3727)
linux=   29923 ms  timer=   29971 ms  systick=   25563 ms  (timer-linux    +48, systick-linux   -4360)
telem=6126 gap=0
```

출력에서 볼 것:

- 자유 계수 타이머(`timer`)는 30초 동안 Linux와 수 ms 안에서 같이 간다(+4 ms/30 s ≈ +130 ppm, 일부러 넣은 +100 ppm 근처). **SysTick 인터럽트 횟수(`systick`)는 30초에 4~7초를 잃었다.** QEMU(TCG)가 호스트 부하 때문에 MCU를 제때 못 돌리면, 밀린 SysTick 주기들이 인터럽트 한 번으로 합쳐지거나 사라진다. 시간 = "인터럽트 몇 번 받았나"로 만들면 그만큼 시간이 증발한다.
- 실제 MCU에서도 같은 일이 생긴다: 인터럽트를 1 ms 넘게 막는 코드(긴 critical section, flash erase 중 CPU stall, 디버거 정지)가 있으면 tick을 잃는다. **타임스탬프는 HW 카운터 값에서 직접 읽고**(자유 계수 32비트 타이머, Cortex-M3+의 DWT CYCCNT 등), 인터럽트는 "랩어라운드 확장"에만 쓴다. 이 노트의 `now_us()`가 그렇게 고친 버전이다(랩어라운드는 25 MHz에서 171초마다 — 그 안에 한 번은 불려야 한다).
- 게스트 첫 줄의 `timer-linux −1960`은 시계 문제가 아니라 **텔레메트리 따라잡기 폭주**다. MCU의 `next`가 부팅 시각부터 10 ms씩 증가하므로, 텔레메트리를 늦게 켜면 밀린 주기만큼 프레임을 한꺼번에 쏟아낸다(30초에 6126개 — 정상의 2배). 그 묶음의 도착 시각과 샘플 시각이 어긋난 것이다. N4 §8의 heartbeat 따라잡기와 같은 버그이고, 처방도 같다: 켤 때 `next = now`, 밀리면 건너뛰기(`while (next <= now) next += period`). 이 노트 코드에는 남겨 두었다(연습문제 5).

### 7.4 예제 13 — offset · drift 추정 `ana_ts.py`

무엇을 확인하나: 교환마다 offset/delay를 계산하고, (1) 전 샘플 직선 피팅, (2) 20개 창마다 delay 최소 샘플만 골라 피팅, (3) "최근 8개 점으로 다음 점 예측" — 온라인 추적기가 실제로 낼 오차.

```python
"""ana_ts.py CSV - NTP-style offset/delay per exchange, min-delay filter, drift fit."""
import sys, csv
R = [tuple(int(x) for x in (r['t1_ns'], r['t2_us'], r['t3_us'], r['t4_ns'])) for r in csv.DictReader(open(sys.argv[1]))]
W = int(sys.argv[2]) if len(sys.argv) > 2 else 20
S = []
for t1, t2, t3, t4 in R:
    t1, t4 = t1 / 1e3, t4 / 1e3                      # Linux -> us
    off = ((t2 - t1) + (t3 - t4)) / 2                # MCU clock minus Linux clock
    dly = (t4 - t1) - (t3 - t2)                      # round trip minus MCU hold time
    S.append((t1, off, dly))
x0, o0 = S[0][0], S[0][1]
def fit(P):
    n = len(P); mx = sum(p[0] for p in P) / n; my = sum(p[1] for p in P) / n
    b = sum((p[0] - mx) * (p[1] - my) for p in P) / sum((p[0] - mx) ** 2 for p in P)
    return b, my - b * mx
pts = [((t - x0) / 1e6, o - o0) for t, o, d in S]                     # x: s, y: us
best = [min(S[i:i + W], key=lambda s: s[2]) for i in range(0, len(S) - W + 1, W)]
fpts = [((t - x0) / 1e6, o - o0) for t, o, d in best]
for name, P in (("all samples", pts), ("min-delay/%d" % W, fpts)):
    b, a = fit(P); res = [y - (a + b * x) for x, y in P]
    print("%-14s n=%4d drift=%7.1f ppm  residual p50=%7.1f us max=%8.1f us" % (
        name, len(P), b, sorted(abs(r) for r in res)[len(res) // 2], max(abs(r) for r in res)))
d = sorted(s[2] for s in S)
print("delay us: min=%.0f p50=%.0f max=%.0f -> offset error bound (min delay/2) = %.0f us"
      % (d[0], d[len(d) // 2], d[-1], d[0] / 2))
# one-step-ahead prediction with the last 8 filtered points (what an online tracker would do)
err = []
for i in range(8, len(fpts)):
    b, a = fit(fpts[i - 8:i]); x, y = fpts[i]; err.append(abs(y - (a + b * x)))
err.sort(); print("online predict (8-pt window): p50=%.1f us max=%.1f us (n=%d)" % (err[len(err)//2], err[-1], len(err)))
```

세 조건 — 게스트 idle, 게스트 stress-ng, 그리고 2%+2% 오류 주입 — 각 1200회, 50 ms 간격, 텔레메트리 100 Hz를 같이 켠 상태.

```sh
./o3_ping /dev/ttyS0 1200 50 1 > ts.csv                    # (stress-ng, proxy 0.02 0.02 도 같은 방식)
python3 ana_ts.py r3_ts.csv 20
```

```text
pings=1200 lost=0 host_crc_err=1 telem=8745 telem_gap=0
pings=1200 lost=90 host_crc_err=276 telem=11233 telem_gap=445
proxy: {'frames': 14045, 'flipped': 289, 'dropped': 246, 'mcu_reconnects': 0}
```

```text
== ts
all samples    n=1200 drift=   74.7 ppm  residual p50= 3548.4 us max= 33420.6 us
min-delay/20   n=  60 drift=  100.1 ppm  residual p50=  673.8 us max=  7602.7 us
delay us: min=2851 p50=19039 max=137343 -> offset error bound (min delay/2) = 1426 us
online predict (8-pt window): p50=793.1 us max=8605.9 us (n=52)
== tsload
all samples    n=1200 drift=  146.0 ppm  residual p50= 2586.1 us max=109517.7 us
min-delay/20   n=  60 drift=  105.3 ppm  residual p50=  385.5 us max=  7465.9 us
delay us: min=2566 p50=13242 max=256690 -> offset error bound (min delay/2) = 1283 us
online predict (8-pt window): p50=692.3 us max=7761.9 us (n=52)
== tslossy
all samples    n=1110 drift=  106.6 ppm  residual p50=  681.1 us max= 33975.6 us
min-delay/20   n=  55 drift=   97.6 ppm  residual p50=  230.5 us max=  6506.5 us
delay us: min=1886 p50=5015 max=108084 -> offset error bound (min delay/2) = 943 us
online predict (8-pt window): p50=312.0 us max=7119.5 us (n=47)
```

(첫 출력 블록의 1행은 stress-ng 실행, 2~3행은 오류 주입 실행의 `o3_ping`/프록시 요약.)

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="240" x2="660" y2="240" stroke="currentColor"/>
<line x1="70" y1="40" x2="70" y2="240" stroke="currentColor"/>
<line x1="70" y1="240.0" x2="660" y2="240.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="244.0" font-size="12" text-anchor="end">-40 ms</text>
<line x1="70" y1="195.6" x2="660" y2="195.6" stroke="#888" stroke-opacity="0.3"/><text x="64" y="199.6" font-size="12" text-anchor="end">-30 ms</text>
<line x1="70" y1="151.1" x2="660" y2="151.1" stroke="#888" stroke-opacity="0.3"/><text x="64" y="155.1" font-size="12" text-anchor="end">-20 ms</text>
<line x1="70" y1="106.7" x2="660" y2="106.7" stroke="#888" stroke-opacity="0.3"/><text x="64" y="110.7" font-size="12" text-anchor="end">-10 ms</text>
<line x1="70" y1="62.2" x2="660" y2="62.2" stroke="#888" stroke-opacity="0.3"/><text x="64" y="66.2" font-size="12" text-anchor="end">0 ms</text>
<text x="70.0" y="256" font-size="12" text-anchor="middle">0 s</text>
<text x="203.8" y="256" font-size="12" text-anchor="middle">20 s</text>
<text x="337.6" y="256" font-size="12" text-anchor="middle">40 s</text>
<text x="471.4" y="256" font-size="12" text-anchor="middle">60 s</text>
<text x="605.2" y="256" font-size="12" text-anchor="middle">80 s</text>
<circle cx="70.0" cy="91.1" r="1.4" fill="#888"/><circle cx="70.5" cy="65.2" r="1.4" fill="#888"/><circle cx="70.9" cy="53.7" r="1.4" fill="#888"/><circle cx="71.3" cy="57.8" r="1.4" fill="#888"/><circle cx="71.9" cy="68.2" r="1.4" fill="#888"/><circle cx="72.3" cy="68.5" r="1.4" fill="#888"/><circle cx="72.8" cy="52.2" r="1.4" fill="#888"/><circle cx="73.2" cy="57.2" r="1.4" fill="#888"/><circle cx="73.6" cy="77.1" r="1.4" fill="#888"/><circle cx="74.1" cy="98.8" r="1.4" fill="#888"/><circle cx="74.6" cy="40.0" r="1.4" fill="#888"/><circle cx="75.1" cy="70.6" r="1.4" fill="#888"/><circle cx="75.5" cy="76.7" r="1.4" fill="#888"/><circle cx="75.9" cy="59.8" r="1.4" fill="#888"/><circle cx="76.3" cy="58.2" r="1.4" fill="#888"/><circle cx="76.7" cy="64.5" r="1.4" fill="#888"/><circle cx="77.1" cy="66.6" r="1.4" fill="#888"/><circle cx="77.5" cy="61.1" r="1.4" fill="#888"/><circle cx="77.9" cy="92.2" r="1.4" fill="#888"/><circle cx="78.3" cy="95.0" r="1.4" fill="#888"/><circle cx="78.9" cy="81.2" r="1.4" fill="#888"/><circle cx="79.3" cy="64.7" r="1.4" fill="#888"/><circle cx="79.7" cy="67.4" r="1.4" fill="#888"/><circle cx="80.1" cy="111.3" r="1.4" fill="#888"/><circle cx="80.6" cy="40.0" r="1.4" fill="#888"/><circle cx="81.1" cy="40.0" r="1.4" fill="#888"/><circle cx="81.6" cy="40.0" r="1.4" fill="#888"/><circle cx="82.2" cy="41.1" r="1.4" fill="#888"/><circle cx="82.7" cy="40.0" r="1.4" fill="#888"/><circle cx="83.3" cy="61.2" r="1.4" fill="#888"/><circle cx="83.7" cy="60.0" r="1.4" fill="#888"/><circle cx="84.2" cy="75.9" r="1.4" fill="#888"/><circle cx="84.8" cy="97.8" r="1.4" fill="#888"/><circle cx="85.7" cy="40.0" r="1.4" fill="#888"/><circle cx="86.2" cy="54.9" r="1.4" fill="#888"/><circle cx="86.7" cy="40.0" r="1.4" fill="#888"/><circle cx="87.2" cy="40.0" r="1.4" fill="#888"/><circle cx="87.7" cy="40.0" r="1.4" fill="#888"/><circle cx="88.2" cy="55.1" r="1.4" fill="#888"/><circle cx="88.7" cy="40.0" r="1.4" fill="#888"/><circle cx="89.3" cy="70.2" r="1.4" fill="#888"/><circle cx="89.8" cy="56.0" r="1.4" fill="#888"/><circle cx="90.2" cy="95.9" r="1.4" fill="#888"/><circle cx="90.7" cy="50.8" r="1.4" fill="#888"/><circle cx="91.2" cy="40.0" r="1.4" fill="#888"/><circle cx="91.7" cy="40.0" r="1.4" fill="#888"/><circle cx="92.1" cy="42.6" r="1.4" fill="#888"/><circle cx="92.7" cy="46.0" r="1.4" fill="#888"/><circle cx="93.2" cy="43.2" r="1.4" fill="#888"/><circle cx="93.6" cy="46.0" r="1.4" fill="#888"/><circle cx="94.0" cy="40.0" r="1.4" fill="#888"/><circle cx="94.5" cy="64.0" r="1.4" fill="#888"/><circle cx="95.0" cy="40.0" r="1.4" fill="#888"/><circle cx="95.5" cy="86.9" r="1.4" fill="#888"/><circle cx="96.0" cy="90.8" r="1.4" fill="#888"/><circle cx="96.4" cy="74.9" r="1.4" fill="#888"/><circle cx="96.9" cy="60.9" r="1.4" fill="#888"/><circle cx="97.3" cy="95.2" r="1.4" fill="#888"/><circle cx="97.8" cy="105.4" r="1.4" fill="#888"/><circle cx="98.3" cy="62.3" r="1.4" fill="#888"/><circle cx="98.7" cy="124.5" r="1.4" fill="#888"/><circle cx="99.3" cy="135.7" r="1.4" fill="#888"/><circle cx="100.3" cy="69.1" r="1.4" fill="#888"/><circle cx="100.7" cy="76.5" r="1.4" fill="#888"/><circle cx="101.2" cy="40.0" r="1.4" fill="#888"/><circle cx="101.9" cy="46.8" r="1.4" fill="#888"/><circle cx="102.4" cy="40.0" r="1.4" fill="#888"/><circle cx="102.9" cy="40.0" r="1.4" fill="#888"/><circle cx="103.5" cy="70.9" r="1.4" fill="#888"/><circle cx="103.9" cy="40.3" r="1.4" fill="#888"/><circle cx="104.4" cy="40.0" r="1.4" fill="#888"/><circle cx="104.9" cy="41.2" r="1.4" fill="#888"/><circle cx="105.4" cy="63.8" r="1.4" fill="#888"/><circle cx="105.8" cy="40.0" r="1.4" fill="#888"/><circle cx="106.5" cy="40.0" r="1.4" fill="#888"/><circle cx="107.0" cy="44.8" r="1.4" fill="#888"/><circle cx="107.5" cy="98.7" r="1.4" fill="#888"/><circle cx="108.2" cy="58.4" r="1.4" fill="#888"/><circle cx="108.7" cy="104.2" r="1.4" fill="#888"/><circle cx="109.2" cy="40.0" r="1.4" fill="#888"/><circle cx="109.8" cy="48.4" r="1.4" fill="#888"/><circle cx="110.3" cy="58.2" r="1.4" fill="#888"/><circle cx="110.7" cy="87.0" r="1.4" fill="#888"/><circle cx="111.2" cy="69.8" r="1.4" fill="#888"/><circle cx="111.6" cy="40.0" r="1.4" fill="#888"/><circle cx="112.1" cy="57.3" r="1.4" fill="#888"/><circle cx="112.5" cy="71.6" r="1.4" fill="#888"/><circle cx="112.9" cy="111.8" r="1.4" fill="#888"/><circle cx="113.5" cy="67.2" r="1.4" fill="#888"/><circle cx="114.0" cy="59.1" r="1.4" fill="#888"/><circle cx="114.6" cy="61.6" r="1.4" fill="#888"/><circle cx="115.0" cy="68.7" r="1.4" fill="#888"/><circle cx="115.4" cy="67.5" r="1.4" fill="#888"/><circle cx="115.8" cy="75.0" r="1.4" fill="#888"/><circle cx="116.2" cy="57.9" r="1.4" fill="#888"/><circle cx="116.6" cy="69.8" r="1.4" fill="#888"/><circle cx="116.9" cy="90.9" r="1.4" fill="#888"/><circle cx="117.4" cy="44.7" r="1.4" fill="#888"/><circle cx="117.8" cy="54.2" r="1.4" fill="#888"/><circle cx="118.3" cy="49.7" r="1.4" fill="#888"/><circle cx="118.8" cy="75.7" r="1.4" fill="#888"/><circle cx="119.3" cy="43.9" r="1.4" fill="#888"/><circle cx="119.9" cy="66.2" r="1.4" fill="#888"/><circle cx="120.3" cy="83.2" r="1.4" fill="#888"/><circle cx="120.7" cy="90.9" r="1.4" fill="#888"/><circle cx="121.2" cy="40.0" r="1.4" fill="#888"/><circle cx="121.8" cy="40.0" r="1.4" fill="#888"/><circle cx="122.4" cy="42.6" r="1.4" fill="#888"/><circle cx="123.0" cy="40.0" r="1.4" fill="#888"/><circle cx="123.8" cy="87.6" r="1.4" fill="#888"/><circle cx="124.3" cy="73.3" r="1.4" fill="#888"/><circle cx="124.7" cy="41.8" r="1.4" fill="#888"/><circle cx="125.2" cy="62.2" r="1.4" fill="#888"/><circle cx="126.4" cy="40.0" r="1.4" fill="#888"/><circle cx="127.3" cy="88.0" r="1.4" fill="#888"/><circle cx="128.1" cy="40.0" r="1.4" fill="#888"/><circle cx="128.8" cy="40.0" r="1.4" fill="#888"/><circle cx="129.3" cy="40.0" r="1.4" fill="#888"/><circle cx="129.9" cy="41.9" r="1.4" fill="#888"/><circle cx="130.4" cy="96.4" r="1.4" fill="#888"/><circle cx="130.9" cy="74.7" r="1.4" fill="#888"/><circle cx="131.4" cy="64.6" r="1.4" fill="#888"/><circle cx="131.8" cy="83.2" r="1.4" fill="#888"/><circle cx="132.3" cy="67.6" r="1.4" fill="#888"/><circle cx="132.7" cy="76.5" r="1.4" fill="#888"/><circle cx="133.1" cy="64.6" r="1.4" fill="#888"/><circle cx="133.6" cy="40.0" r="1.4" fill="#888"/><circle cx="134.1" cy="64.9" r="1.4" fill="#888"/><circle cx="134.7" cy="40.0" r="1.4" fill="#888"/><circle cx="135.2" cy="73.6" r="1.4" fill="#888"/><circle cx="136.0" cy="42.1" r="1.4" fill="#888"/><circle cx="136.5" cy="63.6" r="1.4" fill="#888"/><circle cx="137.5" cy="75.4" r="1.4" fill="#888"/><circle cx="138.1" cy="45.1" r="1.4" fill="#888"/><circle cx="138.7" cy="48.8" r="1.4" fill="#888"/><circle cx="139.2" cy="40.0" r="1.4" fill="#888"/><circle cx="139.7" cy="40.2" r="1.4" fill="#888"/><circle cx="140.2" cy="42.3" r="1.4" fill="#888"/><circle cx="140.8" cy="111.0" r="1.4" fill="#888"/><circle cx="141.3" cy="47.4" r="1.4" fill="#888"/><circle cx="141.8" cy="44.5" r="1.4" fill="#888"/><circle cx="142.4" cy="40.0" r="1.4" fill="#888"/><circle cx="142.9" cy="48.8" r="1.4" fill="#888"/><circle cx="143.4" cy="58.8" r="1.4" fill="#888"/><circle cx="143.9" cy="40.0" r="1.4" fill="#888"/><circle cx="144.4" cy="40.0" r="1.4" fill="#888"/><circle cx="144.9" cy="51.0" r="1.4" fill="#888"/><circle cx="145.3" cy="40.0" r="1.4" fill="#888"/><circle cx="145.8" cy="40.0" r="1.4" fill="#888"/><circle cx="146.3" cy="40.0" r="1.4" fill="#888"/><circle cx="146.8" cy="40.0" r="1.4" fill="#888"/><circle cx="147.3" cy="40.0" r="1.4" fill="#888"/><circle cx="147.9" cy="209.8" r="1.4" fill="#888"/><circle cx="149.2" cy="40.0" r="1.4" fill="#888"/><circle cx="150.1" cy="40.0" r="1.4" fill="#888"/><circle cx="150.6" cy="43.3" r="1.4" fill="#888"/><circle cx="151.3" cy="50.4" r="1.4" fill="#888"/><circle cx="152.0" cy="82.0" r="1.4" fill="#888"/><circle cx="152.5" cy="40.0" r="1.4" fill="#888"/><circle cx="153.3" cy="40.0" r="1.4" fill="#888"/><circle cx="153.9" cy="44.4" r="1.4" fill="#888"/><circle cx="154.5" cy="40.0" r="1.4" fill="#888"/><circle cx="155.0" cy="40.0" r="1.4" fill="#888"/><circle cx="155.5" cy="50.4" r="1.4" fill="#888"/><circle cx="156.0" cy="40.0" r="1.4" fill="#888"/><circle cx="156.6" cy="42.7" r="1.4" fill="#888"/><circle cx="157.2" cy="188.2" r="1.4" fill="#888"/><circle cx="158.3" cy="79.3" r="1.4" fill="#888"/><circle cx="158.7" cy="77.3" r="1.4" fill="#888"/><circle cx="159.2" cy="83.2" r="1.4" fill="#888"/><circle cx="159.6" cy="71.3" r="1.4" fill="#888"/><circle cx="160.0" cy="68.0" r="1.4" fill="#888"/><circle cx="160.4" cy="71.6" r="1.4" fill="#888"/><circle cx="160.8" cy="40.0" r="1.4" fill="#888"/><circle cx="161.4" cy="40.0" r="1.4" fill="#888"/><circle cx="161.9" cy="45.8" r="1.4" fill="#888"/><circle cx="162.4" cy="42.0" r="1.4" fill="#888"/><circle cx="162.9" cy="47.6" r="1.4" fill="#888"/><circle cx="163.5" cy="55.8" r="1.4" fill="#888"/><circle cx="164.1" cy="50.9" r="1.4" fill="#888"/><circle cx="164.6" cy="40.0" r="1.4" fill="#888"/><circle cx="165.2" cy="48.7" r="1.4" fill="#888"/><circle cx="165.8" cy="51.6" r="1.4" fill="#888"/><circle cx="166.5" cy="68.5" r="1.4" fill="#888"/><circle cx="166.8" cy="66.5" r="1.4" fill="#888"/><circle cx="167.3" cy="103.3" r="1.4" fill="#888"/><circle cx="167.8" cy="70.9" r="1.4" fill="#888"/><circle cx="168.2" cy="80.3" r="1.4" fill="#888"/><circle cx="168.6" cy="40.0" r="1.4" fill="#888"/><circle cx="169.2" cy="40.0" r="1.4" fill="#888"/><circle cx="169.9" cy="40.0" r="1.4" fill="#888"/><circle cx="171.0" cy="40.0" r="1.4" fill="#888"/><circle cx="171.5" cy="41.3" r="1.4" fill="#888"/><circle cx="172.0" cy="67.5" r="1.4" fill="#888"/><circle cx="172.7" cy="40.0" r="1.4" fill="#888"/><circle cx="173.3" cy="40.0" r="1.4" fill="#888"/><circle cx="173.7" cy="51.7" r="1.4" fill="#888"/><circle cx="174.3" cy="41.3" r="1.4" fill="#888"/><circle cx="174.9" cy="52.7" r="1.4" fill="#888"/><circle cx="175.5" cy="40.0" r="1.4" fill="#888"/><circle cx="175.9" cy="40.0" r="1.4" fill="#888"/><circle cx="176.5" cy="67.2" r="1.4" fill="#888"/><circle cx="177.0" cy="74.2" r="1.4" fill="#888"/><circle cx="177.4" cy="66.4" r="1.4" fill="#888"/><circle cx="177.8" cy="77.2" r="1.4" fill="#888"/><circle cx="178.3" cy="143.7" r="1.4" fill="#888"/><circle cx="178.9" cy="56.7" r="1.4" fill="#888"/><circle cx="179.3" cy="63.3" r="1.4" fill="#888"/><circle cx="179.7" cy="77.0" r="1.4" fill="#888"/><circle cx="180.1" cy="64.0" r="1.4" fill="#888"/><circle cx="180.5" cy="71.5" r="1.4" fill="#888"/><circle cx="180.9" cy="57.3" r="1.4" fill="#888"/><circle cx="181.4" cy="157.6" r="1.4" fill="#888"/><circle cx="182.1" cy="90.0" r="1.4" fill="#888"/><circle cx="182.5" cy="110.7" r="1.4" fill="#888"/><circle cx="183.0" cy="65.0" r="1.4" fill="#888"/><circle cx="183.4" cy="65.1" r="1.4" fill="#888"/><circle cx="183.8" cy="65.9" r="1.4" fill="#888"/><circle cx="184.2" cy="50.6" r="1.4" fill="#888"/><circle cx="184.8" cy="60.2" r="1.4" fill="#888"/><circle cx="185.2" cy="105.0" r="1.4" fill="#888"/><circle cx="185.7" cy="96.9" r="1.4" fill="#888"/><circle cx="186.3" cy="40.0" r="1.4" fill="#888"/><circle cx="186.8" cy="56.1" r="1.4" fill="#888"/><circle cx="187.3" cy="41.1" r="1.4" fill="#888"/><circle cx="187.9" cy="41.4" r="1.4" fill="#888"/><circle cx="188.5" cy="64.6" r="1.4" fill="#888"/><circle cx="189.1" cy="40.0" r="1.4" fill="#888"/><circle cx="189.6" cy="40.0" r="1.4" fill="#888"/><circle cx="190.1" cy="40.0" r="1.4" fill="#888"/><circle cx="190.6" cy="40.3" r="1.4" fill="#888"/><circle cx="191.1" cy="40.0" r="1.4" fill="#888"/><circle cx="191.7" cy="40.0" r="1.4" fill="#888"/><circle cx="192.1" cy="40.0" r="1.4" fill="#888"/><circle cx="192.7" cy="40.0" r="1.4" fill="#888"/><circle cx="193.3" cy="40.0" r="1.4" fill="#888"/><circle cx="193.8" cy="40.0" r="1.4" fill="#888"/><circle cx="194.3" cy="40.0" r="1.4" fill="#888"/><circle cx="194.9" cy="132.1" r="1.4" fill="#888"/><circle cx="195.7" cy="63.2" r="1.4" fill="#888"/><circle cx="196.3" cy="67.5" r="1.4" fill="#888"/><circle cx="196.7" cy="68.0" r="1.4" fill="#888"/><circle cx="197.1" cy="94.0" r="1.4" fill="#888"/><circle cx="197.5" cy="63.0" r="1.4" fill="#888"/><circle cx="197.9" cy="69.3" r="1.4" fill="#888"/><circle cx="198.3" cy="66.9" r="1.4" fill="#888"/><circle cx="198.7" cy="62.9" r="1.4" fill="#888"/><circle cx="199.1" cy="80.7" r="1.4" fill="#888"/><circle cx="199.7" cy="70.3" r="1.4" fill="#888"/><circle cx="200.1" cy="121.7" r="1.4" fill="#888"/><circle cx="200.7" cy="71.1" r="1.4" fill="#888"/><circle cx="201.1" cy="90.0" r="1.4" fill="#888"/><circle cx="201.6" cy="73.4" r="1.4" fill="#888"/><circle cx="202.0" cy="55.6" r="1.4" fill="#888"/><circle cx="202.4" cy="58.2" r="1.4" fill="#888"/><circle cx="202.8" cy="87.7" r="1.4" fill="#888"/><circle cx="203.3" cy="79.2" r="1.4" fill="#888"/><circle cx="203.7" cy="124.6" r="1.4" fill="#888"/><circle cx="204.3" cy="40.0" r="1.4" fill="#888"/><circle cx="204.9" cy="78.6" r="1.4" fill="#888"/><circle cx="205.4" cy="67.3" r="1.4" fill="#888"/><circle cx="205.7" cy="82.5" r="1.4" fill="#888"/><circle cx="206.2" cy="64.3" r="1.4" fill="#888"/><circle cx="206.6" cy="92.6" r="1.4" fill="#888"/><circle cx="207.1" cy="74.4" r="1.4" fill="#888"/><circle cx="207.5" cy="78.6" r="1.4" fill="#888"/><circle cx="207.9" cy="80.9" r="1.4" fill="#888"/><circle cx="208.4" cy="77.2" r="1.4" fill="#888"/><circle cx="208.8" cy="49.6" r="1.4" fill="#888"/><circle cx="209.3" cy="40.0" r="1.4" fill="#888"/><circle cx="209.9" cy="93.0" r="1.4" fill="#888"/><circle cx="210.3" cy="73.6" r="1.4" fill="#888"/><circle cx="210.7" cy="70.3" r="1.4" fill="#888"/><circle cx="211.1" cy="77.2" r="1.4" fill="#888"/><circle cx="211.7" cy="82.4" r="1.4" fill="#888"/><circle cx="212.1" cy="76.8" r="1.4" fill="#888"/><circle cx="212.6" cy="85.1" r="1.4" fill="#888"/><circle cx="213.2" cy="69.2" r="1.4" fill="#888"/><circle cx="213.8" cy="73.8" r="1.4" fill="#888"/><circle cx="214.2" cy="76.9" r="1.4" fill="#888"/><circle cx="214.6" cy="114.2" r="1.4" fill="#888"/><circle cx="215.2" cy="69.9" r="1.4" fill="#888"/><circle cx="215.6" cy="108.7" r="1.4" fill="#888"/><circle cx="216.1" cy="61.8" r="1.4" fill="#888"/><circle cx="216.6" cy="60.4" r="1.4" fill="#888"/><circle cx="217.0" cy="74.0" r="1.4" fill="#888"/><circle cx="217.4" cy="82.6" r="1.4" fill="#888"/><circle cx="217.9" cy="73.2" r="1.4" fill="#888"/><circle cx="218.3" cy="71.0" r="1.4" fill="#888"/><circle cx="218.8" cy="167.5" r="1.4" fill="#888"/><circle cx="219.4" cy="95.4" r="1.4" fill="#888"/><circle cx="219.9" cy="70.0" r="1.4" fill="#888"/><circle cx="220.3" cy="73.3" r="1.4" fill="#888"/><circle cx="220.8" cy="70.8" r="1.4" fill="#888"/><circle cx="221.2" cy="75.9" r="1.4" fill="#888"/><circle cx="221.6" cy="68.5" r="1.4" fill="#888"/><circle cx="222.0" cy="85.0" r="1.4" fill="#888"/><circle cx="222.5" cy="40.0" r="1.4" fill="#888"/><circle cx="223.0" cy="84.3" r="1.4" fill="#888"/><circle cx="223.4" cy="69.7" r="1.4" fill="#888"/><circle cx="223.8" cy="101.1" r="1.4" fill="#888"/><circle cx="224.4" cy="88.1" r="1.4" fill="#888"/><circle cx="224.8" cy="110.5" r="1.4" fill="#888"/><circle cx="225.4" cy="106.1" r="1.4" fill="#888"/><circle cx="225.9" cy="130.3" r="1.4" fill="#888"/><circle cx="226.4" cy="57.5" r="1.4" fill="#888"/><circle cx="226.8" cy="155.1" r="1.4" fill="#888"/><circle cx="227.5" cy="69.1" r="1.4" fill="#888"/><circle cx="227.9" cy="67.2" r="1.4" fill="#888"/><circle cx="228.3" cy="78.6" r="1.4" fill="#888"/><circle cx="228.7" cy="40.0" r="1.4" fill="#888"/><circle cx="229.3" cy="40.0" r="1.4" fill="#888"/><circle cx="229.7" cy="56.8" r="1.4" fill="#888"/><circle cx="230.3" cy="46.5" r="1.4" fill="#888"/><circle cx="230.9" cy="46.0" r="1.4" fill="#888"/><circle cx="231.4" cy="62.5" r="1.4" fill="#888"/><circle cx="231.8" cy="70.1" r="1.4" fill="#888"/><circle cx="232.3" cy="67.9" r="1.4" fill="#888"/><circle cx="232.7" cy="98.5" r="1.4" fill="#888"/><circle cx="233.2" cy="97.4" r="1.4" fill="#888"/><circle cx="233.7" cy="82.8" r="1.4" fill="#888"/><circle cx="234.3" cy="55.8" r="1.4" fill="#888"/><circle cx="234.7" cy="61.4" r="1.4" fill="#888"/><circle cx="235.2" cy="63.7" r="1.4" fill="#888"/><circle cx="235.6" cy="52.6" r="1.4" fill="#888"/><circle cx="236.0" cy="62.7" r="1.4" fill="#888"/><circle cx="236.4" cy="61.9" r="1.4" fill="#888"/><circle cx="236.8" cy="43.9" r="1.4" fill="#888"/><circle cx="237.3" cy="40.0" r="1.4" fill="#888"/><circle cx="237.8" cy="43.1" r="1.4" fill="#888"/><circle cx="238.4" cy="72.8" r="1.4" fill="#888"/><circle cx="238.8" cy="71.6" r="1.4" fill="#888"/><circle cx="239.2" cy="81.7" r="1.4" fill="#888"/><circle cx="239.7" cy="57.0" r="1.4" fill="#888"/><circle cx="240.1" cy="76.2" r="1.4" fill="#888"/><circle cx="240.5" cy="79.3" r="1.4" fill="#888"/><circle cx="240.9" cy="93.2" r="1.4" fill="#888"/><circle cx="241.4" cy="67.8" r="1.4" fill="#888"/><circle cx="241.8" cy="73.2" r="1.4" fill="#888"/><circle cx="242.2" cy="63.4" r="1.4" fill="#888"/><circle cx="242.7" cy="82.0" r="1.4" fill="#888"/><circle cx="243.1" cy="73.7" r="1.4" fill="#888"/><circle cx="243.5" cy="88.2" r="1.4" fill="#888"/><circle cx="244.0" cy="72.2" r="1.4" fill="#888"/><circle cx="244.4" cy="92.0" r="1.4" fill="#888"/><circle cx="244.9" cy="74.8" r="1.4" fill="#888"/><circle cx="245.4" cy="66.0" r="1.4" fill="#888"/><circle cx="245.8" cy="94.6" r="1.4" fill="#888"/><circle cx="246.2" cy="74.3" r="1.4" fill="#888"/><circle cx="246.6" cy="105.6" r="1.4" fill="#888"/><circle cx="247.2" cy="56.2" r="1.4" fill="#888"/><circle cx="247.6" cy="65.1" r="1.4" fill="#888"/><circle cx="248.0" cy="78.7" r="1.4" fill="#888"/><circle cx="248.5" cy="64.6" r="1.4" fill="#888"/><circle cx="248.9" cy="43.7" r="1.4" fill="#888"/><circle cx="249.4" cy="40.0" r="1.4" fill="#888"/><circle cx="249.9" cy="40.0" r="1.4" fill="#888"/><circle cx="250.5" cy="40.0" r="1.4" fill="#888"/><circle cx="251.0" cy="78.3" r="1.4" fill="#888"/><circle cx="251.7" cy="63.5" r="1.4" fill="#888"/><circle cx="252.3" cy="40.0" r="1.4" fill="#888"/><circle cx="252.8" cy="133.6" r="1.4" fill="#888"/><circle cx="253.5" cy="75.8" r="1.4" fill="#888"/><circle cx="253.9" cy="76.5" r="1.4" fill="#888"/><circle cx="254.4" cy="89.4" r="1.4" fill="#888"/><circle cx="254.9" cy="81.1" r="1.4" fill="#888"/><circle cx="255.4" cy="82.3" r="1.4" fill="#888"/><circle cx="255.8" cy="78.5" r="1.4" fill="#888"/><circle cx="256.2" cy="75.5" r="1.4" fill="#888"/><circle cx="256.7" cy="40.0" r="1.4" fill="#888"/><circle cx="257.1" cy="40.0" r="1.4" fill="#888"/><circle cx="257.7" cy="50.6" r="1.4" fill="#888"/><circle cx="258.3" cy="84.5" r="1.4" fill="#888"/><circle cx="258.8" cy="90.8" r="1.4" fill="#888"/><circle cx="259.2" cy="73.0" r="1.4" fill="#888"/><circle cx="259.7" cy="40.0" r="1.4" fill="#888"/><circle cx="260.2" cy="97.8" r="1.4" fill="#888"/><circle cx="260.7" cy="86.4" r="1.4" fill="#888"/><circle cx="261.2" cy="84.2" r="1.4" fill="#888"/><circle cx="261.7" cy="69.6" r="1.4" fill="#888"/><circle cx="262.1" cy="75.4" r="1.4" fill="#888"/><circle cx="262.5" cy="80.0" r="1.4" fill="#888"/><circle cx="263.0" cy="101.9" r="1.4" fill="#888"/><circle cx="263.5" cy="69.0" r="1.4" fill="#888"/><circle cx="263.9" cy="78.9" r="1.4" fill="#888"/><circle cx="264.3" cy="81.1" r="1.4" fill="#888"/><circle cx="264.8" cy="83.0" r="1.4" fill="#888"/><circle cx="265.2" cy="40.0" r="1.4" fill="#888"/><circle cx="265.6" cy="48.9" r="1.4" fill="#888"/><circle cx="266.1" cy="67.8" r="1.4" fill="#888"/><circle cx="266.6" cy="40.0" r="1.4" fill="#888"/><circle cx="267.2" cy="45.7" r="1.4" fill="#888"/><circle cx="267.7" cy="40.0" r="1.4" fill="#888"/><circle cx="268.2" cy="40.0" r="1.4" fill="#888"/><circle cx="268.7" cy="40.0" r="1.4" fill="#888"/><circle cx="269.1" cy="40.0" r="1.4" fill="#888"/><circle cx="269.7" cy="40.7" r="1.4" fill="#888"/><circle cx="270.1" cy="51.7" r="1.4" fill="#888"/><circle cx="270.5" cy="148.2" r="1.4" fill="#888"/><circle cx="271.2" cy="40.0" r="1.4" fill="#888"/><circle cx="271.7" cy="47.2" r="1.4" fill="#888"/><circle cx="272.2" cy="40.0" r="1.4" fill="#888"/><circle cx="272.8" cy="40.0" r="1.4" fill="#888"/><circle cx="273.2" cy="40.0" r="1.4" fill="#888"/><circle cx="273.7" cy="96.7" r="1.4" fill="#888"/><circle cx="274.2" cy="68.9" r="1.4" fill="#888"/><circle cx="274.6" cy="73.8" r="1.4" fill="#888"/><circle cx="275.0" cy="72.3" r="1.4" fill="#888"/><circle cx="275.4" cy="76.2" r="1.4" fill="#888"/><circle cx="275.9" cy="68.6" r="1.4" fill="#888"/><circle cx="276.3" cy="56.5" r="1.4" fill="#888"/><circle cx="276.9" cy="47.1" r="1.4" fill="#888"/><circle cx="277.4" cy="65.4" r="1.4" fill="#888"/><circle cx="277.8" cy="68.4" r="1.4" fill="#888"/><circle cx="278.2" cy="66.0" r="1.4" fill="#888"/><circle cx="278.6" cy="81.6" r="1.4" fill="#888"/><circle cx="279.1" cy="56.2" r="1.4" fill="#888"/><circle cx="279.5" cy="73.3" r="1.4" fill="#888"/><circle cx="280.0" cy="58.6" r="1.4" fill="#888"/><circle cx="280.4" cy="67.6" r="1.4" fill="#888"/><circle cx="280.8" cy="61.2" r="1.4" fill="#888"/><circle cx="281.4" cy="53.8" r="1.4" fill="#888"/><circle cx="281.8" cy="40.0" r="1.4" fill="#888"/><circle cx="282.3" cy="48.9" r="1.4" fill="#888"/><circle cx="282.8" cy="70.0" r="1.4" fill="#888"/><circle cx="283.2" cy="59.5" r="1.4" fill="#888"/><circle cx="283.5" cy="64.8" r="1.4" fill="#888"/><circle cx="284.0" cy="66.2" r="1.4" fill="#888"/><circle cx="284.4" cy="96.9" r="1.4" fill="#888"/><circle cx="284.9" cy="56.4" r="1.4" fill="#888"/><circle cx="285.3" cy="77.7" r="1.4" fill="#888"/><circle cx="285.7" cy="61.4" r="1.4" fill="#888"/><circle cx="286.1" cy="113.5" r="1.4" fill="#888"/><circle cx="286.7" cy="81.9" r="1.4" fill="#888"/><circle cx="287.2" cy="77.1" r="1.4" fill="#888"/><circle cx="287.6" cy="78.1" r="1.4" fill="#888"/><circle cx="288.0" cy="40.6" r="1.4" fill="#888"/><circle cx="288.5" cy="60.2" r="1.4" fill="#888"/><circle cx="288.9" cy="74.3" r="1.4" fill="#888"/><circle cx="289.3" cy="59.2" r="1.4" fill="#888"/><circle cx="289.7" cy="71.0" r="1.4" fill="#888"/><circle cx="290.1" cy="62.5" r="1.4" fill="#888"/><circle cx="290.5" cy="62.0" r="1.4" fill="#888"/><circle cx="290.9" cy="66.3" r="1.4" fill="#888"/><circle cx="291.3" cy="72.7" r="1.4" fill="#888"/><circle cx="291.7" cy="73.5" r="1.4" fill="#888"/><circle cx="292.1" cy="74.0" r="1.4" fill="#888"/><circle cx="292.5" cy="75.8" r="1.4" fill="#888"/><circle cx="292.9" cy="81.0" r="1.4" fill="#888"/><circle cx="293.4" cy="70.0" r="1.4" fill="#888"/><circle cx="293.8" cy="88.1" r="1.4" fill="#888"/><circle cx="294.3" cy="72.1" r="1.4" fill="#888"/><circle cx="294.7" cy="80.2" r="1.4" fill="#888"/><circle cx="295.1" cy="40.0" r="1.4" fill="#888"/><circle cx="295.6" cy="40.0" r="1.4" fill="#888"/><circle cx="296.2" cy="88.8" r="1.4" fill="#888"/><circle cx="296.7" cy="40.0" r="1.4" fill="#888"/><circle cx="297.2" cy="58.3" r="1.4" fill="#888"/><circle cx="297.8" cy="71.0" r="1.4" fill="#888"/><circle cx="298.5" cy="59.7" r="1.4" fill="#888"/><circle cx="299.0" cy="65.0" r="1.4" fill="#888"/><circle cx="299.4" cy="55.8" r="1.4" fill="#888"/><circle cx="299.8" cy="73.7" r="1.4" fill="#888"/><circle cx="300.2" cy="67.5" r="1.4" fill="#888"/><circle cx="300.6" cy="65.5" r="1.4" fill="#888"/><circle cx="301.1" cy="71.3" r="1.4" fill="#888"/><circle cx="301.5" cy="61.7" r="1.4" fill="#888"/><circle cx="301.8" cy="56.0" r="1.4" fill="#888"/><circle cx="302.4" cy="40.0" r="1.4" fill="#888"/><circle cx="302.9" cy="40.0" r="1.4" fill="#888"/><circle cx="303.3" cy="51.6" r="1.4" fill="#888"/><circle cx="303.8" cy="40.0" r="1.4" fill="#888"/><circle cx="304.3" cy="40.0" r="1.4" fill="#888"/><circle cx="304.8" cy="53.6" r="1.4" fill="#888"/><circle cx="305.3" cy="76.1" r="1.4" fill="#888"/><circle cx="305.7" cy="67.0" r="1.4" fill="#888"/><circle cx="306.1" cy="40.0" r="1.4" fill="#888"/><circle cx="306.6" cy="40.0" r="1.4" fill="#888"/><circle cx="307.1" cy="40.0" r="1.4" fill="#888"/><circle cx="307.6" cy="55.3" r="1.4" fill="#888"/><circle cx="308.2" cy="74.8" r="1.4" fill="#888"/><circle cx="308.6" cy="46.4" r="1.4" fill="#888"/><circle cx="309.3" cy="112.7" r="1.4" fill="#888"/><circle cx="310.0" cy="49.1" r="1.4" fill="#888"/><circle cx="310.5" cy="40.0" r="1.4" fill="#888"/><circle cx="311.1" cy="89.7" r="1.4" fill="#888"/><circle cx="311.7" cy="40.0" r="1.4" fill="#888"/><circle cx="312.2" cy="71.2" r="1.4" fill="#888"/><circle cx="312.8" cy="40.0" r="1.4" fill="#888"/><circle cx="313.4" cy="40.0" r="1.4" fill="#888"/><circle cx="314.0" cy="89.0" r="1.4" fill="#888"/><circle cx="314.5" cy="65.3" r="1.4" fill="#888"/><circle cx="314.9" cy="63.4" r="1.4" fill="#888"/><circle cx="315.3" cy="75.9" r="1.4" fill="#888"/><circle cx="315.7" cy="70.4" r="1.4" fill="#888"/><circle cx="316.1" cy="67.8" r="1.4" fill="#888"/><circle cx="316.5" cy="60.5" r="1.4" fill="#888"/><circle cx="317.0" cy="42.5" r="1.4" fill="#888"/><circle cx="317.5" cy="49.7" r="1.4" fill="#888"/><circle cx="317.9" cy="62.8" r="1.4" fill="#888"/><circle cx="318.5" cy="49.2" r="1.4" fill="#888"/><circle cx="319.0" cy="64.1" r="1.4" fill="#888"/><circle cx="319.6" cy="43.1" r="1.4" fill="#888"/><circle cx="320.1" cy="50.2" r="1.4" fill="#888"/><circle cx="320.6" cy="60.2" r="1.4" fill="#888"/><circle cx="321.0" cy="75.8" r="1.4" fill="#888"/><circle cx="321.5" cy="112.0" r="1.4" fill="#888"/><circle cx="322.0" cy="96.9" r="1.4" fill="#888"/><circle cx="322.6" cy="98.0" r="1.4" fill="#888"/><circle cx="323.2" cy="84.8" r="1.4" fill="#888"/><circle cx="323.8" cy="66.4" r="1.4" fill="#888"/><circle cx="324.2" cy="40.0" r="1.4" fill="#888"/><circle cx="324.7" cy="53.0" r="1.4" fill="#888"/><circle cx="325.2" cy="40.0" r="1.4" fill="#888"/><circle cx="325.7" cy="40.0" r="1.4" fill="#888"/><circle cx="326.2" cy="108.4" r="1.4" fill="#888"/><circle cx="326.7" cy="75.1" r="1.4" fill="#888"/><circle cx="327.1" cy="65.9" r="1.4" fill="#888"/><circle cx="327.5" cy="135.6" r="1.4" fill="#888"/><circle cx="328.1" cy="70.3" r="1.4" fill="#888"/><circle cx="328.5" cy="70.2" r="1.4" fill="#888"/><circle cx="328.9" cy="83.6" r="1.4" fill="#888"/><circle cx="329.3" cy="76.4" r="1.4" fill="#888"/><circle cx="329.8" cy="63.0" r="1.4" fill="#888"/><circle cx="330.2" cy="57.3" r="1.4" fill="#888"/><circle cx="330.7" cy="60.8" r="1.4" fill="#888"/><circle cx="331.4" cy="46.2" r="1.4" fill="#888"/><circle cx="331.9" cy="73.3" r="1.4" fill="#888"/><circle cx="332.3" cy="62.3" r="1.4" fill="#888"/><circle cx="332.7" cy="95.8" r="1.4" fill="#888"/><circle cx="333.2" cy="78.6" r="1.4" fill="#888"/><circle cx="333.6" cy="198.3" r="1.4" fill="#888"/><circle cx="334.4" cy="47.5" r="1.4" fill="#888"/><circle cx="334.9" cy="68.2" r="1.4" fill="#888"/><circle cx="335.3" cy="120.4" r="1.4" fill="#888"/><circle cx="335.8" cy="40.0" r="1.4" fill="#888"/><circle cx="336.3" cy="40.0" r="1.4" fill="#888"/><circle cx="336.9" cy="117.2" r="1.4" fill="#888"/><circle cx="337.6" cy="40.0" r="1.4" fill="#888"/><circle cx="338.1" cy="40.0" r="1.4" fill="#888"/><circle cx="338.6" cy="40.0" r="1.4" fill="#888"/><circle cx="339.2" cy="82.2" r="1.4" fill="#888"/><circle cx="339.6" cy="66.9" r="1.4" fill="#888"/><circle cx="340.0" cy="128.3" r="1.4" fill="#888"/><circle cx="340.6" cy="73.2" r="1.4" fill="#888"/><circle cx="341.0" cy="78.4" r="1.4" fill="#888"/><circle cx="341.4" cy="40.0" r="1.4" fill="#888"/><circle cx="341.9" cy="40.0" r="1.4" fill="#888"/><circle cx="342.4" cy="40.0" r="1.4" fill="#888"/><circle cx="342.9" cy="58.0" r="1.4" fill="#888"/><circle cx="343.5" cy="60.3" r="1.4" fill="#888"/><circle cx="344.0" cy="128.5" r="1.4" fill="#888"/><circle cx="344.8" cy="86.3" r="1.4" fill="#888"/><circle cx="345.4" cy="40.0" r="1.4" fill="#888"/><circle cx="345.9" cy="40.0" r="1.4" fill="#888"/><circle cx="346.4" cy="77.1" r="1.4" fill="#888"/><circle cx="346.9" cy="58.0" r="1.4" fill="#888"/><circle cx="347.3" cy="137.2" r="1.4" fill="#888"/><circle cx="348.0" cy="80.5" r="1.4" fill="#888"/><circle cx="348.4" cy="156.4" r="1.4" fill="#888"/><circle cx="349.0" cy="128.0" r="1.4" fill="#888"/><circle cx="349.6" cy="81.7" r="1.4" fill="#888"/><circle cx="350.0" cy="79.0" r="1.4" fill="#888"/><circle cx="350.5" cy="75.9" r="1.4" fill="#888"/><circle cx="350.9" cy="96.0" r="1.4" fill="#888"/><circle cx="351.4" cy="61.1" r="1.4" fill="#888"/><circle cx="351.8" cy="61.0" r="1.4" fill="#888"/><circle cx="352.1" cy="62.8" r="1.4" fill="#888"/><circle cx="352.5" cy="152.1" r="1.4" fill="#888"/><circle cx="353.2" cy="107.5" r="1.4" fill="#888"/><circle cx="353.7" cy="56.8" r="1.4" fill="#888"/><circle cx="354.1" cy="66.9" r="1.4" fill="#888"/><circle cx="354.5" cy="40.0" r="1.4" fill="#888"/><circle cx="355.0" cy="40.0" r="1.4" fill="#888"/><circle cx="355.5" cy="47.1" r="1.4" fill="#888"/><circle cx="356.1" cy="40.0" r="1.4" fill="#888"/><circle cx="356.6" cy="40.0" r="1.4" fill="#888"/><circle cx="357.1" cy="87.2" r="1.4" fill="#888"/><circle cx="357.8" cy="63.9" r="1.4" fill="#888"/><circle cx="358.4" cy="70.4" r="1.4" fill="#888"/><circle cx="359.1" cy="40.0" r="1.4" fill="#888"/><circle cx="359.6" cy="48.8" r="1.4" fill="#888"/><circle cx="360.1" cy="40.0" r="1.4" fill="#888"/><circle cx="360.7" cy="105.9" r="1.4" fill="#888"/><circle cx="361.2" cy="167.8" r="1.4" fill="#888"/><circle cx="361.9" cy="71.1" r="1.4" fill="#888"/><circle cx="362.3" cy="94.1" r="1.4" fill="#888"/><circle cx="362.8" cy="95.5" r="1.4" fill="#888"/><circle cx="363.2" cy="55.7" r="1.4" fill="#888"/><circle cx="363.9" cy="69.5" r="1.4" fill="#888"/><circle cx="364.3" cy="79.6" r="1.4" fill="#888"/><circle cx="364.8" cy="59.4" r="1.4" fill="#888"/><circle cx="365.2" cy="91.8" r="1.4" fill="#888"/><circle cx="365.7" cy="60.6" r="1.4" fill="#888"/><circle cx="366.1" cy="60.4" r="1.4" fill="#888"/><circle cx="366.4" cy="46.2" r="1.4" fill="#888"/><circle cx="366.9" cy="81.8" r="1.4" fill="#888"/><circle cx="367.3" cy="88.0" r="1.4" fill="#888"/><circle cx="367.8" cy="57.2" r="1.4" fill="#888"/><circle cx="368.2" cy="87.7" r="1.4" fill="#888"/><circle cx="368.7" cy="88.7" r="1.4" fill="#888"/><circle cx="369.4" cy="74.0" r="1.4" fill="#888"/><circle cx="369.8" cy="62.7" r="1.4" fill="#888"/><circle cx="370.2" cy="63.8" r="1.4" fill="#888"/><circle cx="370.6" cy="80.4" r="1.4" fill="#888"/><circle cx="371.1" cy="67.2" r="1.4" fill="#888"/><circle cx="371.6" cy="81.4" r="1.4" fill="#888"/><circle cx="372.1" cy="40.0" r="1.4" fill="#888"/><circle cx="372.5" cy="40.0" r="1.4" fill="#888"/><circle cx="373.0" cy="96.1" r="1.4" fill="#888"/><circle cx="373.7" cy="72.3" r="1.4" fill="#888"/><circle cx="374.4" cy="51.8" r="1.4" fill="#888"/><circle cx="375.0" cy="40.0" r="1.4" fill="#888"/><circle cx="375.5" cy="40.0" r="1.4" fill="#888"/><circle cx="376.0" cy="59.2" r="1.4" fill="#888"/><circle cx="376.3" cy="62.3" r="1.4" fill="#888"/><circle cx="376.7" cy="76.2" r="1.4" fill="#888"/><circle cx="377.1" cy="62.9" r="1.4" fill="#888"/><circle cx="377.5" cy="84.9" r="1.4" fill="#888"/><circle cx="378.0" cy="101.4" r="1.4" fill="#888"/><circle cx="378.5" cy="86.8" r="1.4" fill="#888"/><circle cx="378.9" cy="60.4" r="1.4" fill="#888"/><circle cx="379.3" cy="62.3" r="1.4" fill="#888"/><circle cx="379.7" cy="63.7" r="1.4" fill="#888"/><circle cx="380.2" cy="62.2" r="1.4" fill="#888"/><circle cx="380.7" cy="68.8" r="1.4" fill="#888"/><circle cx="381.1" cy="83.6" r="1.4" fill="#888"/><circle cx="381.5" cy="122.9" r="1.4" fill="#888"/><circle cx="382.0" cy="64.9" r="1.4" fill="#888"/><circle cx="382.5" cy="40.0" r="1.4" fill="#888"/><circle cx="382.9" cy="40.0" r="1.4" fill="#888"/><circle cx="383.5" cy="69.8" r="1.4" fill="#888"/><circle cx="383.9" cy="97.9" r="1.4" fill="#888"/><circle cx="384.5" cy="58.5" r="1.4" fill="#888"/><circle cx="385.0" cy="40.0" r="1.4" fill="#888"/><circle cx="385.5" cy="40.0" r="1.4" fill="#888"/><circle cx="386.1" cy="40.0" r="1.4" fill="#888"/><circle cx="386.6" cy="40.0" r="1.4" fill="#888"/><circle cx="387.1" cy="47.1" r="1.4" fill="#888"/><circle cx="387.6" cy="53.8" r="1.4" fill="#888"/><circle cx="388.1" cy="50.3" r="1.4" fill="#888"/><circle cx="388.5" cy="40.0" r="1.4" fill="#888"/><circle cx="389.0" cy="40.0" r="1.4" fill="#888"/><circle cx="389.5" cy="44.1" r="1.4" fill="#888"/><circle cx="390.0" cy="80.5" r="1.4" fill="#888"/><circle cx="390.5" cy="68.7" r="1.4" fill="#888"/><circle cx="391.0" cy="72.9" r="1.4" fill="#888"/><circle cx="391.4" cy="40.0" r="1.4" fill="#888"/><circle cx="391.9" cy="61.7" r="1.4" fill="#888"/><circle cx="392.5" cy="40.0" r="1.4" fill="#888"/><circle cx="393.0" cy="40.0" r="1.4" fill="#888"/><circle cx="393.5" cy="73.0" r="1.4" fill="#888"/><circle cx="394.2" cy="81.1" r="1.4" fill="#888"/><circle cx="394.6" cy="45.1" r="1.4" fill="#888"/><circle cx="395.2" cy="63.5" r="1.4" fill="#888"/><circle cx="395.7" cy="58.0" r="1.4" fill="#888"/><circle cx="396.1" cy="66.8" r="1.4" fill="#888"/><circle cx="396.5" cy="75.4" r="1.4" fill="#888"/><circle cx="396.9" cy="40.0" r="1.4" fill="#888"/><circle cx="397.4" cy="40.0" r="1.4" fill="#888"/><circle cx="397.9" cy="48.8" r="1.4" fill="#888"/><circle cx="398.4" cy="64.4" r="1.4" fill="#888"/><circle cx="399.0" cy="74.0" r="1.4" fill="#888"/><circle cx="399.4" cy="40.0" r="1.4" fill="#888"/><circle cx="399.9" cy="58.3" r="1.4" fill="#888"/><circle cx="400.5" cy="40.0" r="1.4" fill="#888"/><circle cx="401.0" cy="46.0" r="1.4" fill="#888"/><circle cx="401.5" cy="40.0" r="1.4" fill="#888"/><circle cx="402.0" cy="80.2" r="1.4" fill="#888"/><circle cx="402.6" cy="44.7" r="1.4" fill="#888"/><circle cx="403.1" cy="70.6" r="1.4" fill="#888"/><circle cx="403.5" cy="46.6" r="1.4" fill="#888"/><circle cx="404.0" cy="40.0" r="1.4" fill="#888"/><circle cx="404.5" cy="40.0" r="1.4" fill="#888"/><circle cx="405.0" cy="40.0" r="1.4" fill="#888"/><circle cx="405.5" cy="40.0" r="1.4" fill="#888"/><circle cx="405.9" cy="96.9" r="1.4" fill="#888"/><circle cx="406.5" cy="40.0" r="1.4" fill="#888"/><circle cx="407.3" cy="53.6" r="1.4" fill="#888"/><circle cx="408.0" cy="40.0" r="1.4" fill="#888"/><circle cx="408.6" cy="40.0" r="1.4" fill="#888"/><circle cx="409.2" cy="40.0" r="1.4" fill="#888"/><circle cx="409.7" cy="51.5" r="1.4" fill="#888"/><circle cx="410.2" cy="57.7" r="1.4" fill="#888"/><circle cx="410.7" cy="51.9" r="1.4" fill="#888"/><circle cx="411.3" cy="72.6" r="1.4" fill="#888"/><circle cx="411.9" cy="61.8" r="1.4" fill="#888"/><circle cx="412.6" cy="40.0" r="1.4" fill="#888"/><circle cx="413.0" cy="70.5" r="1.4" fill="#888"/><circle cx="413.4" cy="71.2" r="1.4" fill="#888"/><circle cx="413.8" cy="84.4" r="1.4" fill="#888"/><circle cx="414.3" cy="89.9" r="1.4" fill="#888"/><circle cx="414.8" cy="116.9" r="1.4" fill="#888"/><circle cx="415.3" cy="74.9" r="1.4" fill="#888"/><circle cx="415.8" cy="60.9" r="1.4" fill="#888"/><circle cx="416.2" cy="76.8" r="1.4" fill="#888"/><circle cx="416.6" cy="66.3" r="1.4" fill="#888"/><circle cx="417.0" cy="67.6" r="1.4" fill="#888"/><circle cx="417.5" cy="54.0" r="1.4" fill="#888"/><circle cx="418.1" cy="40.0" r="1.4" fill="#888"/><circle cx="418.6" cy="44.7" r="1.4" fill="#888"/><circle cx="419.1" cy="40.0" r="1.4" fill="#888"/><circle cx="419.6" cy="40.0" r="1.4" fill="#888"/><circle cx="420.1" cy="40.2" r="1.4" fill="#888"/><circle cx="420.6" cy="40.0" r="1.4" fill="#888"/><circle cx="421.1" cy="73.3" r="1.4" fill="#888"/><circle cx="421.7" cy="40.0" r="1.4" fill="#888"/><circle cx="422.2" cy="55.1" r="1.4" fill="#888"/><circle cx="422.8" cy="92.4" r="1.4" fill="#888"/><circle cx="423.4" cy="48.1" r="1.4" fill="#888"/><circle cx="423.9" cy="45.8" r="1.4" fill="#888"/><circle cx="424.4" cy="84.0" r="1.4" fill="#888"/><circle cx="424.9" cy="104.3" r="1.4" fill="#888"/><circle cx="425.4" cy="133.3" r="1.4" fill="#888"/><circle cx="426.0" cy="69.1" r="1.4" fill="#888"/><circle cx="426.4" cy="40.0" r="1.4" fill="#888"/><circle cx="426.9" cy="71.8" r="1.4" fill="#888"/><circle cx="427.3" cy="88.1" r="1.4" fill="#888"/><circle cx="427.8" cy="40.0" r="1.4" fill="#888"/><circle cx="428.3" cy="64.1" r="1.4" fill="#888"/><circle cx="428.8" cy="75.3" r="1.4" fill="#888"/><circle cx="429.3" cy="68.9" r="1.4" fill="#888"/><circle cx="429.7" cy="40.0" r="1.4" fill="#888"/><circle cx="430.3" cy="72.0" r="1.4" fill="#888"/><circle cx="430.8" cy="40.0" r="1.4" fill="#888"/><circle cx="431.6" cy="48.3" r="1.4" fill="#888"/><circle cx="432.1" cy="40.0" r="1.4" fill="#888"/><circle cx="432.5" cy="59.5" r="1.4" fill="#888"/><circle cx="432.9" cy="98.2" r="1.4" fill="#888"/><circle cx="433.5" cy="42.9" r="1.4" fill="#888"/><circle cx="433.9" cy="50.2" r="1.4" fill="#888"/><circle cx="434.4" cy="47.6" r="1.4" fill="#888"/><circle cx="434.9" cy="40.0" r="1.4" fill="#888"/><circle cx="435.4" cy="40.0" r="1.4" fill="#888"/><circle cx="435.9" cy="54.6" r="1.4" fill="#888"/><circle cx="436.4" cy="40.0" r="1.4" fill="#888"/><circle cx="437.0" cy="40.0" r="1.4" fill="#888"/><circle cx="437.6" cy="40.0" r="1.4" fill="#888"/><circle cx="438.2" cy="40.0" r="1.4" fill="#888"/><circle cx="438.6" cy="40.0" r="1.4" fill="#888"/><circle cx="439.3" cy="53.5" r="1.4" fill="#888"/><circle cx="439.8" cy="49.4" r="1.4" fill="#888"/><circle cx="440.3" cy="84.8" r="1.4" fill="#888"/><circle cx="440.8" cy="98.2" r="1.4" fill="#888"/><circle cx="441.3" cy="40.0" r="1.4" fill="#888"/><circle cx="441.7" cy="68.3" r="1.4" fill="#888"/><circle cx="442.2" cy="40.0" r="1.4" fill="#888"/><circle cx="442.8" cy="91.2" r="1.4" fill="#888"/><circle cx="443.3" cy="71.8" r="1.4" fill="#888"/><circle cx="443.7" cy="72.6" r="1.4" fill="#888"/><circle cx="444.2" cy="67.2" r="1.4" fill="#888"/><circle cx="444.6" cy="73.2" r="1.4" fill="#888"/><circle cx="445.0" cy="40.3" r="1.4" fill="#888"/><circle cx="445.5" cy="41.1" r="1.4" fill="#888"/><circle cx="446.0" cy="40.0" r="1.4" fill="#888"/><circle cx="446.4" cy="46.5" r="1.4" fill="#888"/><circle cx="446.9" cy="43.2" r="1.4" fill="#888"/><circle cx="447.4" cy="60.1" r="1.4" fill="#888"/><circle cx="447.9" cy="68.5" r="1.4" fill="#888"/><circle cx="448.3" cy="73.2" r="1.4" fill="#888"/><circle cx="448.8" cy="80.0" r="1.4" fill="#888"/><circle cx="449.4" cy="66.3" r="1.4" fill="#888"/><circle cx="449.8" cy="75.9" r="1.4" fill="#888"/><circle cx="450.2" cy="60.6" r="1.4" fill="#888"/><circle cx="450.7" cy="63.6" r="1.4" fill="#888"/><circle cx="451.0" cy="91.4" r="1.4" fill="#888"/><circle cx="451.5" cy="65.5" r="1.4" fill="#888"/><circle cx="451.9" cy="59.4" r="1.4" fill="#888"/><circle cx="452.3" cy="139.5" r="1.4" fill="#888"/><circle cx="453.5" cy="74.1" r="1.4" fill="#888"/><circle cx="453.9" cy="126.3" r="1.4" fill="#888"/><circle cx="454.7" cy="89.4" r="1.4" fill="#888"/><circle cx="455.2" cy="40.0" r="1.4" fill="#888"/><circle cx="455.8" cy="45.7" r="1.4" fill="#888"/><circle cx="456.2" cy="40.0" r="1.4" fill="#888"/><circle cx="456.8" cy="75.2" r="1.4" fill="#888"/><circle cx="457.3" cy="113.4" r="1.4" fill="#888"/><circle cx="458.0" cy="84.5" r="1.4" fill="#888"/><circle cx="458.4" cy="40.0" r="1.4" fill="#888"/><circle cx="458.9" cy="72.3" r="1.4" fill="#888"/><circle cx="459.4" cy="46.7" r="1.4" fill="#888"/><circle cx="460.0" cy="40.0" r="1.4" fill="#888"/><circle cx="460.6" cy="104.8" r="1.4" fill="#888"/><circle cx="461.3" cy="68.0" r="1.4" fill="#888"/><circle cx="462.0" cy="41.3" r="1.4" fill="#888"/><circle cx="462.5" cy="70.0" r="1.4" fill="#888"/><circle cx="463.0" cy="96.3" r="1.4" fill="#888"/><circle cx="463.7" cy="129.5" r="1.4" fill="#888"/><circle cx="464.6" cy="40.0" r="1.4" fill="#888"/><circle cx="465.2" cy="56.5" r="1.4" fill="#888"/><circle cx="465.7" cy="64.5" r="1.4" fill="#888"/><circle cx="466.3" cy="40.0" r="1.4" fill="#888"/><circle cx="467.7" cy="51.0" r="1.4" fill="#888"/><circle cx="468.1" cy="94.4" r="1.4" fill="#888"/><circle cx="468.6" cy="61.6" r="1.4" fill="#888"/><circle cx="469.0" cy="139.7" r="1.4" fill="#888"/><circle cx="469.7" cy="40.0" r="1.4" fill="#888"/><circle cx="470.2" cy="104.7" r="1.4" fill="#888"/><circle cx="470.7" cy="50.7" r="1.4" fill="#888"/><circle cx="471.1" cy="40.0" r="1.4" fill="#888"/><circle cx="471.7" cy="119.7" r="1.4" fill="#888"/><circle cx="472.6" cy="40.0" r="1.4" fill="#888"/><circle cx="473.2" cy="114.6" r="1.4" fill="#888"/><circle cx="473.9" cy="98.0" r="1.4" fill="#888"/><circle cx="474.4" cy="68.9" r="1.4" fill="#888"/><circle cx="474.9" cy="71.4" r="1.4" fill="#888"/><circle cx="475.3" cy="81.9" r="1.4" fill="#888"/><circle cx="475.8" cy="66.7" r="1.4" fill="#888"/><circle cx="476.2" cy="40.0" r="1.4" fill="#888"/><circle cx="476.7" cy="40.0" r="1.4" fill="#888"/><circle cx="477.2" cy="40.0" r="1.4" fill="#888"/><circle cx="477.7" cy="64.2" r="1.4" fill="#888"/><circle cx="478.2" cy="101.9" r="1.4" fill="#888"/><circle cx="478.8" cy="103.2" r="1.4" fill="#888"/><circle cx="479.3" cy="59.1" r="1.4" fill="#888"/><circle cx="479.8" cy="83.1" r="1.4" fill="#888"/><circle cx="480.2" cy="75.6" r="1.4" fill="#888"/><circle cx="480.6" cy="91.4" r="1.4" fill="#888"/><circle cx="481.1" cy="76.6" r="1.4" fill="#888"/><circle cx="481.5" cy="85.6" r="1.4" fill="#888"/><circle cx="482.0" cy="61.3" r="1.4" fill="#888"/><circle cx="482.4" cy="101.8" r="1.4" fill="#888"/><circle cx="482.9" cy="70.6" r="1.4" fill="#888"/><circle cx="483.3" cy="81.6" r="1.4" fill="#888"/><circle cx="483.7" cy="58.4" r="1.4" fill="#888"/><circle cx="484.1" cy="61.4" r="1.4" fill="#888"/><circle cx="484.5" cy="110.1" r="1.4" fill="#888"/><circle cx="485.0" cy="82.3" r="1.4" fill="#888"/><circle cx="485.5" cy="75.3" r="1.4" fill="#888"/><circle cx="486.0" cy="71.9" r="1.4" fill="#888"/><circle cx="486.4" cy="64.3" r="1.4" fill="#888"/><circle cx="486.8" cy="51.0" r="1.4" fill="#888"/><circle cx="487.2" cy="61.1" r="1.4" fill="#888"/><circle cx="487.6" cy="60.1" r="1.4" fill="#888"/><circle cx="488.0" cy="77.2" r="1.4" fill="#888"/><circle cx="488.4" cy="68.7" r="1.4" fill="#888"/><circle cx="488.8" cy="60.9" r="1.4" fill="#888"/><circle cx="489.2" cy="73.6" r="1.4" fill="#888"/><circle cx="489.6" cy="72.4" r="1.4" fill="#888"/><circle cx="490.0" cy="60.6" r="1.4" fill="#888"/><circle cx="490.4" cy="68.2" r="1.4" fill="#888"/><circle cx="490.8" cy="81.5" r="1.4" fill="#888"/><circle cx="491.2" cy="97.6" r="1.4" fill="#888"/><circle cx="491.7" cy="63.7" r="1.4" fill="#888"/><circle cx="492.1" cy="66.2" r="1.4" fill="#888"/><circle cx="492.5" cy="83.0" r="1.4" fill="#888"/><circle cx="492.9" cy="64.6" r="1.4" fill="#888"/><circle cx="493.3" cy="65.0" r="1.4" fill="#888"/><circle cx="493.7" cy="53.1" r="1.4" fill="#888"/><circle cx="494.2" cy="64.5" r="1.4" fill="#888"/><circle cx="494.6" cy="93.8" r="1.4" fill="#888"/><circle cx="495.1" cy="79.6" r="1.4" fill="#888"/><circle cx="495.6" cy="78.0" r="1.4" fill="#888"/><circle cx="496.0" cy="68.5" r="1.4" fill="#888"/><circle cx="496.5" cy="40.0" r="1.4" fill="#888"/><circle cx="497.3" cy="40.0" r="1.4" fill="#888"/><circle cx="497.8" cy="40.0" r="1.4" fill="#888"/><circle cx="498.3" cy="40.0" r="1.4" fill="#888"/><circle cx="498.8" cy="51.5" r="1.4" fill="#888"/><circle cx="499.3" cy="48.2" r="1.4" fill="#888"/><circle cx="499.8" cy="41.4" r="1.4" fill="#888"/><circle cx="500.3" cy="40.0" r="1.4" fill="#888"/><circle cx="500.8" cy="87.0" r="1.4" fill="#888"/><circle cx="501.3" cy="105.9" r="1.4" fill="#888"/><circle cx="501.8" cy="73.1" r="1.4" fill="#888"/><circle cx="502.2" cy="72.7" r="1.4" fill="#888"/><circle cx="502.6" cy="80.1" r="1.4" fill="#888"/><circle cx="503.1" cy="74.0" r="1.4" fill="#888"/><circle cx="503.5" cy="68.3" r="1.4" fill="#888"/><circle cx="503.9" cy="73.6" r="1.4" fill="#888"/><circle cx="504.4" cy="66.6" r="1.4" fill="#888"/><circle cx="504.8" cy="69.2" r="1.4" fill="#888"/><circle cx="505.2" cy="71.6" r="1.4" fill="#888"/><circle cx="505.6" cy="91.9" r="1.4" fill="#888"/><circle cx="506.1" cy="40.0" r="1.4" fill="#888"/><circle cx="506.6" cy="54.0" r="1.4" fill="#888"/><circle cx="507.1" cy="40.0" r="1.4" fill="#888"/><circle cx="507.5" cy="55.8" r="1.4" fill="#888"/><circle cx="508.0" cy="63.2" r="1.4" fill="#888"/><circle cx="508.4" cy="73.3" r="1.4" fill="#888"/><circle cx="508.8" cy="103.7" r="1.4" fill="#888"/><circle cx="509.3" cy="67.7" r="1.4" fill="#888"/><circle cx="509.7" cy="70.5" r="1.4" fill="#888"/><circle cx="510.1" cy="152.8" r="1.4" fill="#888"/><circle cx="510.8" cy="79.9" r="1.4" fill="#888"/><circle cx="511.2" cy="72.2" r="1.4" fill="#888"/><circle cx="511.6" cy="79.3" r="1.4" fill="#888"/><circle cx="512.1" cy="67.9" r="1.4" fill="#888"/><circle cx="512.5" cy="124.7" r="1.4" fill="#888"/><circle cx="513.1" cy="108.8" r="1.4" fill="#888"/><circle cx="513.6" cy="73.4" r="1.4" fill="#888"/><circle cx="514.1" cy="81.2" r="1.4" fill="#888"/><circle cx="514.5" cy="80.9" r="1.4" fill="#888"/><circle cx="514.9" cy="105.3" r="1.4" fill="#888"/><circle cx="515.4" cy="74.4" r="1.4" fill="#888"/><circle cx="515.9" cy="81.6" r="1.4" fill="#888"/><circle cx="516.3" cy="92.4" r="1.4" fill="#888"/><circle cx="516.8" cy="84.2" r="1.4" fill="#888"/><circle cx="517.3" cy="95.2" r="1.4" fill="#888"/><circle cx="517.8" cy="62.7" r="1.4" fill="#888"/><circle cx="518.2" cy="63.9" r="1.4" fill="#888"/><circle cx="518.6" cy="63.0" r="1.4" fill="#888"/><circle cx="519.0" cy="65.4" r="1.4" fill="#888"/><circle cx="519.4" cy="64.3" r="1.4" fill="#888"/><circle cx="519.8" cy="40.0" r="1.4" fill="#888"/><circle cx="520.3" cy="61.3" r="1.4" fill="#888"/><circle cx="520.7" cy="110.7" r="1.4" fill="#888"/><circle cx="521.2" cy="96.9" r="1.4" fill="#888"/><circle cx="521.7" cy="73.4" r="1.4" fill="#888"/><circle cx="522.1" cy="76.8" r="1.4" fill="#888"/><circle cx="522.5" cy="118.1" r="1.4" fill="#888"/><circle cx="523.1" cy="68.3" r="1.4" fill="#888"/><circle cx="523.5" cy="85.2" r="1.4" fill="#888"/><circle cx="524.0" cy="61.1" r="1.4" fill="#888"/><circle cx="524.3" cy="80.9" r="1.4" fill="#888"/><circle cx="524.8" cy="69.1" r="1.4" fill="#888"/><circle cx="525.2" cy="83.5" r="1.4" fill="#888"/><circle cx="525.6" cy="73.1" r="1.4" fill="#888"/><circle cx="526.0" cy="58.4" r="1.4" fill="#888"/><circle cx="526.6" cy="40.0" r="1.4" fill="#888"/><circle cx="527.1" cy="63.4" r="1.4" fill="#888"/><circle cx="527.5" cy="68.7" r="1.4" fill="#888"/><circle cx="527.9" cy="118.9" r="1.4" fill="#888"/><circle cx="528.4" cy="83.1" r="1.4" fill="#888"/><circle cx="528.9" cy="81.2" r="1.4" fill="#888"/><circle cx="529.3" cy="76.4" r="1.4" fill="#888"/><circle cx="529.7" cy="78.5" r="1.4" fill="#888"/><circle cx="530.1" cy="84.6" r="1.4" fill="#888"/><circle cx="530.6" cy="55.1" r="1.4" fill="#888"/><circle cx="531.0" cy="83.2" r="1.4" fill="#888"/><circle cx="531.4" cy="40.0" r="1.4" fill="#888"/><circle cx="531.9" cy="155.7" r="1.4" fill="#888"/><circle cx="532.8" cy="40.0" r="1.4" fill="#888"/><circle cx="533.3" cy="70.9" r="1.4" fill="#888"/><circle cx="533.7" cy="66.3" r="1.4" fill="#888"/><circle cx="534.1" cy="65.8" r="1.4" fill="#888"/><circle cx="534.6" cy="138.7" r="1.4" fill="#888"/><circle cx="535.4" cy="85.9" r="1.4" fill="#888"/><circle cx="535.8" cy="40.0" r="1.4" fill="#888"/><circle cx="536.3" cy="40.0" r="1.4" fill="#888"/><circle cx="536.8" cy="62.3" r="1.4" fill="#888"/><circle cx="537.2" cy="128.1" r="1.4" fill="#888"/><circle cx="537.8" cy="137.3" r="1.4" fill="#888"/><circle cx="538.4" cy="120.7" r="1.4" fill="#888"/><circle cx="539.0" cy="83.4" r="1.4" fill="#888"/><circle cx="539.4" cy="59.3" r="1.4" fill="#888"/><circle cx="539.8" cy="63.9" r="1.4" fill="#888"/><circle cx="540.2" cy="72.6" r="1.4" fill="#888"/><circle cx="540.6" cy="68.0" r="1.4" fill="#888"/><circle cx="541.0" cy="57.0" r="1.4" fill="#888"/><circle cx="541.5" cy="86.1" r="1.4" fill="#888"/><circle cx="541.9" cy="81.2" r="1.4" fill="#888"/><circle cx="542.4" cy="142.3" r="1.4" fill="#888"/><circle cx="543.0" cy="93.3" r="1.4" fill="#888"/><circle cx="543.4" cy="40.0" r="1.4" fill="#888"/><circle cx="543.9" cy="62.2" r="1.4" fill="#888"/><circle cx="544.2" cy="62.1" r="1.4" fill="#888"/><circle cx="544.6" cy="40.0" r="1.4" fill="#888"/><circle cx="545.1" cy="58.2" r="1.4" fill="#888"/><circle cx="545.6" cy="60.5" r="1.4" fill="#888"/><circle cx="546.2" cy="40.0" r="1.4" fill="#888"/><circle cx="546.7" cy="82.6" r="1.4" fill="#888"/><circle cx="547.4" cy="87.0" r="1.4" fill="#888"/><circle cx="548.1" cy="66.8" r="1.4" fill="#888"/><circle cx="548.5" cy="54.1" r="1.4" fill="#888"/><circle cx="548.9" cy="84.3" r="1.4" fill="#888"/><circle cx="549.4" cy="40.0" r="1.4" fill="#888"/><circle cx="549.9" cy="40.0" r="1.4" fill="#888"/><circle cx="550.4" cy="67.2" r="1.4" fill="#888"/><circle cx="550.9" cy="80.7" r="1.4" fill="#888"/><circle cx="551.5" cy="73.7" r="1.4" fill="#888"/><circle cx="552.2" cy="101.7" r="1.4" fill="#888"/><circle cx="552.9" cy="50.0" r="1.4" fill="#888"/><circle cx="553.4" cy="79.0" r="1.4" fill="#888"/><circle cx="553.8" cy="70.5" r="1.4" fill="#888"/><circle cx="554.3" cy="66.8" r="1.4" fill="#888"/><circle cx="554.7" cy="62.8" r="1.4" fill="#888"/><circle cx="555.1" cy="91.4" r="1.4" fill="#888"/><circle cx="555.6" cy="73.4" r="1.4" fill="#888"/><circle cx="556.0" cy="65.8" r="1.4" fill="#888"/><circle cx="556.4" cy="67.9" r="1.4" fill="#888"/><circle cx="556.8" cy="78.2" r="1.4" fill="#888"/><circle cx="557.2" cy="72.1" r="1.4" fill="#888"/><circle cx="557.6" cy="73.5" r="1.4" fill="#888"/><circle cx="558.0" cy="70.9" r="1.4" fill="#888"/><circle cx="558.4" cy="51.4" r="1.4" fill="#888"/><circle cx="558.9" cy="40.0" r="1.4" fill="#888"/><circle cx="559.4" cy="62.6" r="1.4" fill="#888"/><circle cx="559.8" cy="66.0" r="1.4" fill="#888"/><circle cx="560.2" cy="73.2" r="1.4" fill="#888"/><circle cx="560.6" cy="128.4" r="1.4" fill="#888"/><circle cx="561.2" cy="155.1" r="1.4" fill="#888"/><circle cx="561.8" cy="114.3" r="1.4" fill="#888"/><circle cx="562.4" cy="111.1" r="1.4" fill="#888"/><circle cx="562.9" cy="89.0" r="1.4" fill="#888"/><circle cx="563.3" cy="57.2" r="1.4" fill="#888"/><circle cx="563.7" cy="92.9" r="1.4" fill="#888"/><circle cx="564.2" cy="115.3" r="1.4" fill="#888"/><circle cx="564.7" cy="89.1" r="1.4" fill="#888"/><circle cx="565.2" cy="82.6" r="1.4" fill="#888"/><circle cx="565.6" cy="40.4" r="1.4" fill="#888"/><circle cx="566.1" cy="40.0" r="1.4" fill="#888"/><circle cx="566.6" cy="79.7" r="1.4" fill="#888"/><circle cx="567.1" cy="91.2" r="1.4" fill="#888"/><circle cx="567.5" cy="79.2" r="1.4" fill="#888"/><circle cx="568.0" cy="73.4" r="1.4" fill="#888"/><circle cx="568.4" cy="77.9" r="1.4" fill="#888"/><circle cx="568.8" cy="106.1" r="1.4" fill="#888"/><circle cx="569.3" cy="76.2" r="1.4" fill="#888"/><circle cx="569.7" cy="146.9" r="1.4" fill="#888"/><circle cx="570.4" cy="94.6" r="1.4" fill="#888"/><circle cx="570.9" cy="63.9" r="1.4" fill="#888"/><circle cx="571.3" cy="40.0" r="1.4" fill="#888"/><circle cx="571.8" cy="40.0" r="1.4" fill="#888"/><circle cx="572.3" cy="40.0" r="1.4" fill="#888"/><circle cx="572.8" cy="49.5" r="1.4" fill="#888"/><circle cx="573.3" cy="43.0" r="1.4" fill="#888"/><circle cx="573.7" cy="47.5" r="1.4" fill="#888"/><circle cx="574.3" cy="129.8" r="1.4" fill="#888"/><circle cx="575.0" cy="63.8" r="1.4" fill="#888"/><circle cx="575.4" cy="61.2" r="1.4" fill="#888"/><circle cx="575.8" cy="60.8" r="1.4" fill="#888"/><circle cx="576.1" cy="62.9" r="1.4" fill="#888"/><circle cx="576.5" cy="117.1" r="1.4" fill="#888"/><circle cx="577.0" cy="85.1" r="1.4" fill="#888"/><circle cx="577.5" cy="70.2" r="1.4" fill="#888"/><circle cx="577.9" cy="40.1" r="1.4" fill="#888"/><circle cx="578.5" cy="40.0" r="1.4" fill="#888"/><circle cx="578.9" cy="61.5" r="1.4" fill="#888"/><circle cx="579.3" cy="40.0" r="1.4" fill="#888"/><circle cx="579.8" cy="40.0" r="1.4" fill="#888"/><circle cx="580.3" cy="49.0" r="1.4" fill="#888"/><circle cx="580.9" cy="43.2" r="1.4" fill="#888"/><circle cx="581.4" cy="69.7" r="1.4" fill="#888"/><circle cx="582.0" cy="40.0" r="1.4" fill="#888"/><circle cx="582.4" cy="40.0" r="1.4" fill="#888"/><circle cx="582.9" cy="40.0" r="1.4" fill="#888"/><circle cx="583.3" cy="58.4" r="1.4" fill="#888"/><circle cx="583.8" cy="54.9" r="1.4" fill="#888"/><circle cx="584.4" cy="40.0" r="1.4" fill="#888"/><circle cx="584.9" cy="40.0" r="1.4" fill="#888"/><circle cx="585.4" cy="44.0" r="1.4" fill="#888"/><circle cx="585.9" cy="40.0" r="1.4" fill="#888"/><circle cx="586.4" cy="41.5" r="1.4" fill="#888"/><circle cx="586.9" cy="61.5" r="1.4" fill="#888"/><circle cx="587.3" cy="108.4" r="1.4" fill="#888"/><circle cx="587.8" cy="86.8" r="1.4" fill="#888"/><circle cx="588.3" cy="79.1" r="1.4" fill="#888"/><circle cx="588.7" cy="113.5" r="1.4" fill="#888"/><circle cx="589.3" cy="95.6" r="1.4" fill="#888"/><circle cx="589.8" cy="75.4" r="1.4" fill="#888"/><circle cx="590.2" cy="92.3" r="1.4" fill="#888"/><circle cx="590.6" cy="84.4" r="1.4" fill="#888"/><circle cx="591.1" cy="59.3" r="1.4" fill="#888"/><circle cx="591.4" cy="66.6" r="1.4" fill="#888"/><circle cx="591.8" cy="71.9" r="1.4" fill="#888"/><circle cx="592.2" cy="68.4" r="1.4" fill="#888"/><circle cx="592.6" cy="105.3" r="1.4" fill="#888"/><circle cx="593.1" cy="75.9" r="1.4" fill="#888"/><circle cx="593.6" cy="83.1" r="1.4" fill="#888"/><circle cx="594.0" cy="75.1" r="1.4" fill="#888"/><circle cx="594.4" cy="76.7" r="1.4" fill="#888"/><circle cx="594.8" cy="64.9" r="1.4" fill="#888"/><circle cx="595.2" cy="65.6" r="1.4" fill="#888"/><circle cx="595.6" cy="67.2" r="1.4" fill="#888"/><circle cx="596.0" cy="63.9" r="1.4" fill="#888"/><circle cx="596.3" cy="66.0" r="1.4" fill="#888"/><circle cx="596.7" cy="78.8" r="1.4" fill="#888"/><circle cx="597.2" cy="92.4" r="1.4" fill="#888"/><circle cx="597.6" cy="64.5" r="1.4" fill="#888"/><circle cx="598.0" cy="53.7" r="1.4" fill="#888"/><circle cx="598.4" cy="73.4" r="1.4" fill="#888"/><circle cx="598.8" cy="81.2" r="1.4" fill="#888"/><circle cx="599.3" cy="73.6" r="1.4" fill="#888"/><circle cx="599.7" cy="169.0" r="1.4" fill="#888"/><circle cx="600.4" cy="68.0" r="1.4" fill="#888"/><circle cx="600.8" cy="83.8" r="1.4" fill="#888"/><circle cx="601.3" cy="143.2" r="1.4" fill="#888"/><circle cx="601.9" cy="79.7" r="1.4" fill="#888"/><circle cx="602.4" cy="159.5" r="1.4" fill="#888"/><circle cx="603.2" cy="100.1" r="1.4" fill="#888"/><circle cx="603.8" cy="69.9" r="1.4" fill="#888"/><circle cx="604.4" cy="74.6" r="1.4" fill="#888"/><circle cx="604.8" cy="75.7" r="1.4" fill="#888"/><circle cx="605.2" cy="106.2" r="1.4" fill="#888"/><circle cx="605.7" cy="141.4" r="1.4" fill="#888"/><circle cx="606.4" cy="70.2" r="1.4" fill="#888"/><circle cx="606.8" cy="64.1" r="1.4" fill="#888"/><circle cx="607.2" cy="73.1" r="1.4" fill="#888"/><circle cx="607.6" cy="86.2" r="1.4" fill="#888"/><circle cx="608.0" cy="107.4" r="1.4" fill="#888"/><circle cx="608.5" cy="57.6" r="1.4" fill="#888"/><circle cx="609.1" cy="50.8" r="1.4" fill="#888"/><circle cx="609.6" cy="135.2" r="1.4" fill="#888"/><circle cx="610.2" cy="64.9" r="1.4" fill="#888"/><circle cx="610.5" cy="74.1" r="1.4" fill="#888"/><circle cx="610.9" cy="79.4" r="1.4" fill="#888"/><circle cx="611.4" cy="136.4" r="1.4" fill="#888"/><circle cx="612.0" cy="61.4" r="1.4" fill="#888"/><circle cx="612.4" cy="65.8" r="1.4" fill="#888"/><circle cx="612.7" cy="75.0" r="1.4" fill="#888"/><circle cx="613.2" cy="112.7" r="1.4" fill="#888"/><circle cx="613.7" cy="63.3" r="1.4" fill="#888"/><circle cx="614.1" cy="74.6" r="1.4" fill="#888"/><circle cx="614.5" cy="117.4" r="1.4" fill="#888"/><circle cx="615.1" cy="110.0" r="1.4" fill="#888"/><circle cx="615.6" cy="72.0" r="1.4" fill="#888"/><circle cx="616.0" cy="72.5" r="1.4" fill="#888"/><circle cx="616.5" cy="60.8" r="1.4" fill="#888"/><circle cx="616.9" cy="70.8" r="1.4" fill="#888"/><circle cx="617.3" cy="68.0" r="1.4" fill="#888"/><circle cx="617.8" cy="76.5" r="1.4" fill="#888"/><circle cx="618.2" cy="58.0" r="1.4" fill="#888"/><circle cx="618.6" cy="55.3" r="1.4" fill="#888"/><circle cx="619.0" cy="64.3" r="1.4" fill="#888"/><circle cx="619.5" cy="40.0" r="1.4" fill="#888"/><circle cx="619.9" cy="40.0" r="1.4" fill="#888"/><circle cx="620.4" cy="40.5" r="1.4" fill="#888"/><circle cx="620.9" cy="40.0" r="1.4" fill="#888"/><circle cx="621.4" cy="59.2" r="1.4" fill="#888"/><circle cx="622.0" cy="62.7" r="1.4" fill="#888"/><circle cx="622.3" cy="67.7" r="1.4" fill="#888"/><circle cx="622.7" cy="76.4" r="1.4" fill="#888"/><circle cx="623.2" cy="66.6" r="1.4" fill="#888"/><circle cx="623.6" cy="65.4" r="1.4" fill="#888"/><circle cx="624.0" cy="68.1" r="1.4" fill="#888"/><circle cx="624.4" cy="76.5" r="1.4" fill="#888"/><circle cx="624.8" cy="66.9" r="1.4" fill="#888"/><circle cx="625.2" cy="65.2" r="1.4" fill="#888"/><circle cx="625.6" cy="54.2" r="1.4" fill="#888"/><circle cx="626.0" cy="69.0" r="1.4" fill="#888"/><circle cx="626.4" cy="64.3" r="1.4" fill="#888"/><circle cx="626.8" cy="77.9" r="1.4" fill="#888"/><circle cx="627.2" cy="60.3" r="1.4" fill="#888"/><circle cx="627.6" cy="40.0" r="1.4" fill="#888"/><circle cx="628.0" cy="61.8" r="1.4" fill="#888"/><circle cx="628.4" cy="40.0" r="1.4" fill="#888"/><circle cx="628.9" cy="87.7" r="1.4" fill="#888"/><circle cx="629.4" cy="44.0" r="1.4" fill="#888"/><circle cx="630.0" cy="40.0" r="1.4" fill="#888"/><circle cx="630.4" cy="62.2" r="1.4" fill="#888"/><circle cx="630.8" cy="86.0" r="1.4" fill="#888"/><circle cx="631.2" cy="42.4" r="1.4" fill="#888"/><circle cx="631.7" cy="40.0" r="1.4" fill="#888"/><circle cx="632.2" cy="61.0" r="1.4" fill="#888"/><circle cx="632.6" cy="129.4" r="1.4" fill="#888"/><circle cx="633.2" cy="82.2" r="1.4" fill="#888"/><circle cx="633.6" cy="46.5" r="1.4" fill="#888"/><circle cx="634.1" cy="40.0" r="1.4" fill="#888"/><circle cx="634.6" cy="65.6" r="1.4" fill="#888"/><circle cx="635.1" cy="47.8" r="1.4" fill="#888"/><circle cx="635.6" cy="40.0" r="1.4" fill="#888"/><circle cx="636.1" cy="40.0" r="1.4" fill="#888"/><circle cx="636.7" cy="69.0" r="1.4" fill="#888"/><circle cx="637.1" cy="79.8" r="1.4" fill="#888"/><circle cx="637.5" cy="81.6" r="1.4" fill="#888"/><circle cx="637.9" cy="40.0" r="1.4" fill="#888"/><circle cx="638.4" cy="81.0" r="1.4" fill="#888"/><circle cx="639.0" cy="58.9" r="1.4" fill="#888"/><circle cx="639.6" cy="40.0" r="1.4" fill="#888"/><circle cx="640.1" cy="40.0" r="1.4" fill="#888"/><circle cx="640.6" cy="127.7" r="1.4" fill="#888"/><circle cx="641.3" cy="40.0" r="1.4" fill="#888"/><circle cx="641.8" cy="40.0" r="1.4" fill="#888"/><circle cx="642.3" cy="40.0" r="1.4" fill="#888"/><circle cx="642.8" cy="40.0" r="1.4" fill="#888"/><circle cx="643.3" cy="40.0" r="1.4" fill="#888"/><circle cx="643.8" cy="65.1" r="1.4" fill="#888"/><circle cx="644.4" cy="67.9" r="1.4" fill="#888"/><circle cx="645.0" cy="41.6" r="1.4" fill="#888"/><circle cx="645.5" cy="51.5" r="1.4" fill="#888"/><circle cx="646.1" cy="40.0" r="1.4" fill="#888"/><circle cx="646.6" cy="68.7" r="1.4" fill="#888"/><circle cx="647.3" cy="40.0" r="1.4" fill="#888"/><circle cx="647.8" cy="40.0" r="1.4" fill="#888"/><circle cx="648.3" cy="70.9" r="1.4" fill="#888"/><circle cx="649.2" cy="43.1" r="1.4" fill="#888"/><circle cx="649.6" cy="40.0" r="1.4" fill="#888"/><circle cx="650.1" cy="68.9" r="1.4" fill="#888"/><circle cx="650.5" cy="83.4" r="1.4" fill="#888"/><circle cx="651.0" cy="147.2" r="1.4" fill="#888"/><circle cx="651.6" cy="61.2" r="1.4" fill="#888"/><circle cx="652.1" cy="116.0" r="1.4" fill="#888"/><circle cx="652.6" cy="61.4" r="1.4" fill="#888"/><circle cx="653.0" cy="65.9" r="1.4" fill="#888"/><circle cx="653.4" cy="63.2" r="1.4" fill="#888"/><circle cx="653.8" cy="56.5" r="1.4" fill="#888"/><circle cx="654.2" cy="66.0" r="1.4" fill="#888"/><circle cx="654.7" cy="80.5" r="1.4" fill="#888"/><circle cx="655.1" cy="63.7" r="1.4" fill="#888"/><circle cx="655.5" cy="45.6" r="1.4" fill="#888"/><circle cx="656.0" cy="96.4" r="1.4" fill="#888"/><circle cx="656.5" cy="40.0" r="1.4" fill="#888"/><circle cx="657.0" cy="40.0" r="1.4" fill="#888"/><circle cx="657.5" cy="40.0" r="1.4" fill="#888"/><circle cx="657.9" cy="40.0" r="1.4" fill="#888"/><circle cx="658.5" cy="76.3" r="1.4" fill="#888"/><circle cx="659.1" cy="62.1" r="1.4" fill="#888"/><circle cx="659.5" cy="40.0" r="1.4" fill="#888"/><circle cx="660.0" cy="86.2" r="1.4" fill="#888"/>
<circle cx="70.5" cy="65.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="83.3" cy="61.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="98.3" cy="62.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="105.4" cy="63.8" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="116.2" cy="57.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="119.9" cy="66.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="130.9" cy="74.7" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="144.9" cy="51.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="160.0" cy="68.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="166.5" cy="68.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="183.8" cy="65.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="191.7" cy="40.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="202.4" cy="58.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="205.4" cy="67.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="221.6" cy="68.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="226.4" cy="57.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="242.2" cy="63.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="247.6" cy="65.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="261.7" cy="69.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="263.5" cy="69.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="280.0" cy="58.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="283.2" cy="59.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="290.1" cy="62.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="301.5" cy="61.7" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="314.9" cy="63.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="320.6" cy="60.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="332.3" cy="62.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="346.9" cy="58.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="351.8" cy="61.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="364.8" cy="59.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="376.0" cy="59.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="379.3" cy="62.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="395.7" cy="58.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="403.1" cy="70.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="416.6" cy="66.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="426.0" cy="69.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="432.5" cy="59.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="444.2" cy="67.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="450.7" cy="63.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="470.7" cy="50.7" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="474.9" cy="71.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="488.8" cy="60.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="492.9" cy="64.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="508.0" cy="63.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="517.8" cy="62.7" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="524.0" cy="61.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="530.6" cy="55.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="543.9" cy="62.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="556.0" cy="65.8" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="563.3" cy="57.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="575.0" cy="63.8" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="575.4" cy="61.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="591.1" cy="59.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="596.0" cy="63.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="610.2" cy="64.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="618.6" cy="55.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="622.0" cy="62.7" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="632.2" cy="61.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="650.1" cy="68.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="653.4" cy="63.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="80" y="28" font-size="12">offset − 직선 모델 (idle, 1200회). 회색: 각 교환, 파랑: 20개 중 최소 지연</text>
<text x="80" y="282" font-size="12">기울기(drift) = 100.1 ppm (참값 +100 ppm 시뮬레이션). 아래로 클립된 점은 -40 ms 이하</text></svg>
```

그림 8 — idle 조건의 offset 잔차(실측). 회색 점(모든 교환)은 아래로 수십 ms까지 흩어진다 — PONG이 돌아오는 길(MCU → tty → 앱 깨우기)이 가는 길보다 자주 늦기 때문이다(비대칭). 파랑(창마다 최소 delay)은 0 근처에 붙어 있다.

출력에서 볼 것:

- **최소 지연 필터를 쓰면 세 조건 모두 drift가 97.6~105.3 ppm** — 일부러 넣은 +100 ppm을 ±5% 안에서 맞혔다. 전 샘플을 쓰면 74.7 / 146.0 ppm으로 크게 틀린다(부하 조건에서 46% 오차).
- 잔차 중앙값은 0.2~0.7 ms, 최대 7~8 ms. 이것이 이 (에뮬레이터) 링크에서 메시지 교환으로 얻을 수 있는 시간 동기 정확도다. 이론 상한 "최소 delay/2"(0.9~1.4 ms)와 같은 자릿수다.
- 오류 주입 조건이 오히려 가장 좋았다 — 측정 시점의 호스트 부하가 달라서 delay 자체가 짧았다(p50 5 ms). 에뮬레이터 비교는 시점 효과를 늘 의심할 것. 손실 90개(7.5%)는 그냥 빠진 표본일 뿐 추정을 망가뜨리지 않는다 — **시간 동기는 손실에 강하고 지연 꼬리에 약하다.**
- 텔레메트리(신뢰성 없음)는 445개 구멍 = 4%. 프레임 손실률(드롭 2% + 비트 손상 2%)과 맞는다. 재전송하지 않는 스트림의 손실은 그대로 보인다 — 설계대로다.

### 7.5 제품에서는 — 계속 갱신되는 변환 함수

실제 데몬은 이 계산을 오프라인이 아니라 계속 한다.

1. 1~10 Hz로 PING. 최근 N개(예: 16) 중 delay 최소 교환을 "좋은 점"으로 채택. delay가 최근 최솟값의 몇 배를 넘으면 버림.
2. 좋은 점들로 `offset(t) = a + b × t` 를 갱신 — 짧은 창 직선 피팅, 또는 PI 제어 루프(`phc2sys`/chrony가 하는 방식). 위 "online predict"가 8점 창의 성능이다(p50 0.3~0.8 ms).
3. MCU 샘플 타임스탬프 변환: `t_linux = t_mcu − offset(t_mcu)`. MCU 카운터 랩어라운드(32비트 µs면 71.6분)는 MCU 쪽에서 64비트로 확장해 보내거나, Linux 쪽에서 직전 값과 비교해 확장한다.
4. 변환된 시각을 센서 메시지(ROS 2 `header.stamp`, Q6)에 넣는다. 원본 MCU 시각과 추정 오차도 같이 로그에 남긴다 — 나중에 융합 품질 문제를 디버깅할 유일한 단서다(Q7).
5. drift 추정값 자체를 모니터링한다. 크리스탈 drift는 온도에 따라 수 ppm씩 천천히 변한다. 갑자기 수백 ppm이 되면 위의 tick 손실 같은 버그다.

### 7.6 메시지로 부족하면 — HW 동기 펄스 (가이드, 이 랩에서 실행 안 함)

메시지 교환의 정확도는 링크 지연 꼬리가 정한다(위에서 ~1 ms). µs가 필요하면 **선 하나를 더 쓴다.**

- 한쪽(보통 MCU 또는 GNSS의 PPS)이 주기적으로 GPIO 펄스를 낸다. MCU는 그 순간을 타이머 **input capture**(하드웨어가 카운터 값을 래치)로, Linux는 GPIO 인터럽트 타임스탬프로 기록한다.
- Linux 쪽 수단: `pps-gpio`(DT에 `compatible = "pps-gpio"`, `/dev/ppsN`, `ppstest` — 이 랩 커널에 `CONFIG_PPS_CLIENT_GPIO=m`), 또는 GPIO 문자 장치의 에지 이벤트(`gpio_v2_line_event.timestamp_ns`, 기본은 CLOCK_MONOTONIC). 이 타임스탬프는 IRQ 핸들러 진입 시점이라 IRQ 지연(수 µs ~ 수십 µs)이 섞인다. SoC에 HW 타임스탬프 엔진이 있으면 더 정확하다 — 6.18 트리에는 `drivers/hte/`(NVIDIA Tegra194 드라이버)와 `GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE` 플래그가 있다.
- 펄스에는 번호가 없으므로 "몇 번째 펄스인가"는 메시지로 보낸다(펄스 직후 MCU가 "방금 펄스는 내 시각 T였다"를 송신). 메시지는 늦어도 된다 — 시각은 HW가 이미 래치했다.
- 더 나아가면: Ethernet으로 잇고 PTP(IEEE 1588)를 쓴다 — MAC이 프레임 송수신 순간을 HW로 찍어 수십 ns까지 간다(Q3, `ptp4l`/`phc2sys`).

| 방법 | 정확도 (일반적 범위) | 추가 HW | 이 노트 |
|---|---|---|---|
| 메시지 교환 + 최소 delay 필터 | 링크 지연 꼬리의 절반 — 이 랩 ~1 ms, 실제 921600 UART라면 수십~수백 µs (기억 기반 추정) | 없음 | 실측 |
| GPIO 펄스 + IRQ 타임스탬프 | 수 µs ~ 수십 µs (IRQ 지연) | 선 1개 | 가이드 |
| GPIO 펄스 + HW 캡처 양쪽 | 1 µs 이하 | 선 1개 + 캡처 지원 SoC | 가이드 |
| PTP (HW 타임스탬프) | 수십 ns ~ 1 µs | PTP MAC 양쪽 | Q3 |

---

## 8. MCU 펌웨어 업데이트 경로 — Linux가 MCU를 안전하게 갈아 끼우기

### 8.1 요구 사항 — 무엇이 일어나도 벽돌이 되면 안 된다

N4 §9가 "어떤 방식이 있나"(ROM 부트로더 / 자체 부트로더 + A/B / remoteproc)와 rootfs 번들과의 관계를 정리했다. 여기서는 **자체 부트로더 + A/B 방식을 실제로 짜서** 아래 실패를 하나씩 일으켜 본다.

| 실패 | 기대 동작 | 이 노트에서 |
|---|---|---|
| 전송 중 비트 오류 · 프레임 손실 | 재전송으로 메움, 최종 CRC로 확인 | §6.5 (실측) |
| 전송 중 MCU 전원 차단 | 재부팅 후 **끊긴 지점부터 이어받기** | §8.4 (실측) |
| 전송 중 Linux 쪽 프로세스 죽음 | 다시 실행하면 이어받기 (MCU가 진행도를 기억) | 같은 메커니즘 |
| 이미지 손상 (CRC 불일치) | 적용 거부, 기존 슬롯 유지 | FW_END → `ST_BADCRC` |
| 새 펌웨어가 부팅은 되지만 정상 동작 안 함 | 확정(COMMIT) 없이 다음 리셋 → **자동 롤백** | §8.5 (실측) |
| 저장된 슬롯이 나중에 손상 | 부팅 시 CRC 검사 → 다른 슬롯으로 | `boot_select()` |
| 위조 이미지 | 서명 검증 실패 → 거부 | §8.6 (가이드) |

### 8.2 A/B 슬롯 상태 기계

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="140" height="60" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="90" y="64" text-anchor="middle" font-size="13">정상 (A 실행)</text>
  <text x="90" y="84" text-anchor="middle" font-size="12">active=A trial=0</text>
  <rect x="270" y="40" width="140" height="60" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="64" text-anchor="middle" font-size="13">B에 쓰는 중</text>
  <text x="340" y="84" text-anchor="middle" font-size="12">upd_off 영속 저장</text>
  <rect x="520" y="40" width="140" height="60" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="590" y="64" text-anchor="middle" font-size="13">검증 완료</text>
  <text x="590" y="84" text-anchor="middle" font-size="12">trial=1 tries=0</text>
  <rect x="520" y="190" width="140" height="60" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="590" y="214" text-anchor="middle" font-size="13">B 시험 부팅</text>
  <text x="590" y="234" text-anchor="middle" font-size="12">tries=1</text>
  <rect x="270" y="190" width="140" height="60" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="214" text-anchor="middle" font-size="13">정상 (B 실행)</text>
  <text x="340" y="234" text-anchor="middle" font-size="12">active=B trial=0</text>
  <rect x="20" y="190" width="140" height="60" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="90" y="214" text-anchor="middle" font-size="13">롤백 (A 실행)</text>
  <text x="90" y="234" text-anchor="middle" font-size="12">trial=0</text>
  <line x1="160" y1="70" x2="268" y2="70" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="214" y="62" text-anchor="middle" font-size="12">FW_BEGIN</text>
  <line x1="410" y1="70" x2="518" y2="70" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="464" y="62" text-anchor="middle" font-size="12">FW_END 통과</text>
  <path d="M 330 100 C 300 140 380 140 350 102" fill="none" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="340" y="160" text-anchor="middle" font-size="12">전원 차단 → 이어받기</text>
  <line x1="590" y1="100" x2="590" y2="188" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="600" y="150" font-size="12">RESET</text>
  <line x1="518" y1="220" x2="412" y2="220" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="464" y="212" text-anchor="middle" font-size="12">COMMIT</text>
  <path d="M 590 250 C 590 290 90 290 90 252" fill="none" stroke="#d0564a" marker-end="url(#ah6)"/>
  <text x="340" y="280" text-anchor="middle" font-size="12">COMMIT 없이 리셋 (전원 차단 포함) → 부트로더가 롤백</text>
  <text x="20" y="24" font-size="12">"쓰기 대상은 항상 지금 실행 중이 아닌 슬롯" — 실행 중인 코드는 한 번도 지워지지 않는다</text>
</svg>
```

그림 9 — A/B 상태. 메타 블록(활성 슬롯, trial, tries, 각 슬롯의 버전·길이·CRC, 진행 중 업데이트의 len/crc/off)은 flash 앞 4 KB에 있다.

### 8.3 예제 14 — 부트로더와 업데이트 명령 `m_flash.c`

앞부분: 메타 블록과 부팅 결정. 모든 리셋에서 돈다.

```c
/* m_flash.c - "bootloader" + A/B slots on semihosted flash. Meta block at 0,
 * slot s at 4096 + s*128K. Data is written BEFORE the progress pointer (upd_off). */
#include "m_hw.h"
#define MAGIC 0x4F334D31u               /* "O3M1" */
#define SLOT_SZ (128u * 1024)
#define SLOT_OFF(s) (4096u + (s) * SLOT_SZ)
static struct meta {
    uint32_t magic; uint8_t active, trial, tries, upd_slot;
    uint16_t ver[2], upd_ver; uint32_t len[2], crc[2], upd_len, upd_crc, upd_off;
} M;
static int fd = -1, run;               /* run = slot we booted */
static void meta_save(void) { sh_pwrite(fd, &M, sizeof M, 0); }
static uint32_t slot_crc(int s, uint32_t len)
{
    uint8_t b[256]; uint32_t c = 0xFFFFFFFFu;
    for (uint32_t o = 0; o < len; o += sizeof b) {
        int k = len - o < sizeof b ? len - o : sizeof b;
        sh_pread(fd, b, k, SLOT_OFF(s) + o); c = o3_crc32_upd(c, b, k);
    }
    return ~c;
}
void boot_select(void)                 /* runs on every reset */
{
    if ((fd = sh_open("o3_flash.bin", 3)) < 0) fd = sh_open("o3_flash.bin", 7);
    if (sh_pread(fd, &M, sizeof M, 0) != sizeof M || M.magic != MAGIC) {
        memset(&M, 0, sizeof M); M.magic = MAGIC; M.ver[0] = 0x0100; meta_save();
    }
    if (M.trial && M.tries++ >= 1) { M.trial = 0; sh_puts("boot: trial not committed -> rollback\n"); }
    run = M.trial ? !M.active : M.active;
    if (M.len[run] && slot_crc(run, M.len[run]) != M.crc[run]) {
        sh_puts("boot: slot CRC bad -> other slot\n"); run = !run; M.trial = 0;
    }
    meta_save();
}
uint16_t fw_version(void) { return M.ver[run]; }
void boot_info(uint8_t *p)
{
    p[0] = run; p[1] = M.trial; put32(p + 2, M.upd_len); put32(p + 6, M.upd_off);
}
```

뒷부분: FW_BEGIN / DATA / END / COMMIT.

```c
int fw_cmd(const o3_msg *m, uint8_t *out, int *n)   /* returns ST_*, extra reply in out */
{
    const uint8_t *p = m->p;
    if (m->type == T_FW_BEGIN) {
        uint32_t len = get32(p), crc = get32(p + 4); uint16_t ver = get16(p + 8);
        if (len > SLOT_SZ) return ST_BAD;
        if (M.upd_len != len || M.upd_crc != crc || M.upd_ver != ver) {
            M.upd_slot = !run; M.upd_len = len; M.upd_crc = crc; M.upd_ver = ver; M.upd_off = 0; meta_save();
        }
        put32(out, M.upd_off); *n = 5;
        return M.upd_off ? ST_RESUMED : ST_OK;
    }
    if (m->type == T_FW_DATA) {
        uint32_t off = get32(p); int k = m->len - 4;
        if (off + k <= M.upd_off) return ST_OK;        /* already stored: idempotent */
        if (off != M.upd_off || off + k > M.upd_len) return ST_BADOFF;
        sh_pwrite(fd, p + 4, k, SLOT_OFF(M.upd_slot) + off);   /* 1. data */
        M.upd_off += k; meta_save();                            /* 2. progress */
        return ST_OK;
    }
    if (m->type == T_FW_END) {
        int s = M.upd_slot;
        if (!M.upd_len || M.upd_off != M.upd_len || slot_crc(s, M.upd_len) != M.upd_crc) return ST_BADCRC;
        M.ver[s] = M.upd_ver; M.len[s] = M.upd_len; M.crc[s] = M.upd_crc;
        M.upd_len = M.upd_off = 0; M.trial = 1; M.tries = 0; M.active = !s; meta_save();
        return ST_OK;
    }
    if (m->type == T_FW_COMMIT && M.trial && run != M.active) { M.active = run; M.trial = 0; meta_save(); }
    return (m->type == T_FW_COMMIT || m->type == T_RESET) ? ST_OK : ST_BAD;
}
```

코드에서 볼 것:

- **"데이터 먼저, 진행 포인터 나중"** (FW_DATA의 1·2 주석). 순서를 바꾸면 전원 차단 시 "포인터는 앞섰는데 데이터는 없는" 상태가 되고, 이어받기가 구멍을 건너뛴다. 최종 CRC가 잡긴 하지만 처음부터 다시 받아야 한다. 저널링 파일시스템의 순서 규칙과 같다(J 모듈).
- FW_BEGIN은 (len, crc, ver)가 진행 중인 업데이트와 같으면 `ST_RESUMED` + 오프셋을 돌려준다. 다른 이미지면 처음부터. 이미지 ID로 CRC를 쓰는 셈이다.
- FW_DATA는 이미 가진 범위면 `ST_OK`로 무시(멱등). Go-Back-N이 같은 조각을 몇 번 보내도 괜찮다.
- `boot_select()`의 `tries` 카운터: 시험 부팅은 단 한 번. 그 부팅에서 COMMIT을 못 받고 다시 리셋되면(워치독, 전원, 명시적 리셋 무엇이든) 롤백한다. MCUboot의 "test → confirm" 모델과 같은 구조다(MCUboot 문서 기준, 세부 플래그 이름은 다르다).
- 진짜 MCU라면 여기에 **flash 지우기 단위**(섹터 erase)와 **쓰기 정렬**(예: 8 B 또는 16 B 단위 프로그래밍)이 붙는다. 메타 블록은 지우고 다시 쓰는 사이 전원이 나가면 사라지므로, 실무에서는 메타를 두 벌(번갈아 쓰기 + 시퀀스 번호 + CRC)로 둔다. 이 랩의 semihosting 파일은 원자적 바이트 쓰기라 그 문제를 숨긴다 — 단순화한 부분이다.
- 부팅 = "슬롯 선택"까지만 구현했다. 실제로 그 슬롯의 코드로 점프(VTOR 설정, MSP 로드, Reset 벡터 호출)하지는 않고, 선택된 슬롯의 버전을 보고할 뿐이다. 이 점이 이 랩의 가장 큰 단순화다.

### 8.4 전원 차단 후 이어받기 (실측)

64 KiB 이미지를 W=2로 보내는 도중, 2초 뒤 VM 호스트에서 MCU QEMU를 `kill -9`(정리 없는 전원 차단). 감시 스크립트가 1초 뒤 MCU를 다시 켠다.

```sh
bash ctl.sh powerloss 2          # host: (sleep 2; pkill -9 -f "qemu-system-arm -M mps2") &
./o3_fw /dev/ttyS0 fw_v2.bin 2 512
```

```text
HELLO fw=3.0 slot=1 trial=0 upd=0/0
BEGIN len=65536 crc=e8528385 -> st=0 start_off=0
HELLO fw=3.0 slot=1 trial=0 upd=16472/65536
HELLO fw=3.0 slot=1 trial=0 upd=16472/65536
BEGIN len=65536 crc=e8528385 -> st=4 start_off=16472
HELLO fw=3.0 slot=1 trial=0 upd=16936/65536
BEGIN len=65536 crc=e8528385 -> st=4 start_off=16936
DATA 66000 B in 7.58 s = 8.5 KiB/s  W=2 retx=8 nak=0 timeout=4 host_crc_err=0
after reset: fw=2.0 slot=0 trial=1
COMMIT done
```

출력에서 볼 것:

- 전원 차단 시점에 MCU flash에는 16472 B(= 71 조각)가 확정돼 있었다. 다시 켜진 MCU의 HELLO가 `upd=16472/65536`을 알려 주고, FW_BEGIN이 `st=4`(ST_RESUMED), `start_off=16472`로 답했다. **처음부터가 아니라 25% 지점부터** 이어 보냈다.
- 그 뒤 세 번째 HELLO(16936)는 §4.7의 "HELLO 요청 중복이 세션을 한 번 더 리셋"한 흔적이다(재부팅이 아니다 — MCU 로그에 부팅 메시지가 한 번만 늘었다). 그래도 진행도가 영속이라 다시 이어받았다.
- `DATA 66000 B` = 65536 + 464: 끊긴 근처의 조각 2개가 두 번 전송됐다. MCU는 이미 가진 범위를 멱등하게 무시했다.
- 마지막에 CRC 검증 → trial 부팅 → COMMIT까지 정상. 끊김이 결과에 아무 흔적도 남기지 않았다.

### 8.5 확정 없이 리셋되면 — 자동 롤백 (실측)

96 KiB 이미지를 버전 5.0(`1280` = 0x0500)으로 올리되 COMMIT을 하지 않고(`nocommit`, "새 펌웨어가 Linux 쪽 자가 점검에 실패한" 상황), 그대로 MCU 전원을 껐다 켰다.

```sh
./o3_fw /dev/ttyS0 fw_v3.bin 8 1280 nocommit
bash ctl.sh powerloss 0
sleep 5; ./o3_ping /dev/ttyS0 1 0 > /dev/null
```

```text
HELLO fw=2.0 slot=0 trial=0 upd=0/0
BEGIN len=98304 crc=211a4a38 -> st=0 start_off=0
DATA 98304 B in 8.02 s = 12.0 KiB/s  W=8 retx=0 nak=0 timeout=0 host_crc_err=0
after reset: fw=5.0 slot=1 trial=1
HELLO proto=1 fw=2.0 window=8 slot=0 trial=0 upd=0/0 mcu_crc_err=0
pings=1 lost=0 host_crc_err=0 telem=0 telem_gap=0
```

MCU 쪽 semihosting 출력(그 부팅 구간):

```text
mcu: fw (updated) up
boot: trial not committed -> rollback
mcu: fw (updated) up
```

출력에서 볼 것: 업데이트 직후엔 `fw=5.0 slot=1 trial=1`(시험 부팅), 확정 없이 전원이 한 번 나갔다 오자 부트로더가 `trial not committed -> rollback`을 찍고 **2.0 / slot 0으로 돌아왔다.** 슬롯 1의 5.0 이미지는 flash에 그대로 있다(다음 업데이트가 덮어쓴다). (`fw (updated) up`은 "1.0이 아니다"라는 뜻의 단순 메시지라 2.0에도 찍힌다.)

Linux 쪽 오케스트레이션의 순서는 L4의 rootfs A/B와 맞물린다(N4 §9 5단계): 새 rootfs 부팅 → MCU HELLO 버전 비교 → 업데이트 → MCU trial 부팅 → 자가 점검(센서 응답, heartbeat 왕복, 모터 드라이버 상태) → MCU COMMIT → 그 다음에 rootfs `mark-good`. 순서를 거꾸로 하면 "rootfs는 확정, MCU는 롤백"인 조합이 생긴다.

### 8.6 서명 — CRC는 사고를 막고, 서명은 공격을 막는다 (가이드, 실행 안 함)

CRC-32는 우연한 손상만 잡는다. 공격자는 CRC를 다시 계산하면 그만이다. 국방·차량 장비는 **MCU 부트로더가 서명을 검증**해야 한다.

- 빌드 서버가 이미지 해시(SHA-256)에 개인키로 서명(ECDSA P-256 또는 Ed25519 — MCUboot가 지원하는 방식들, MCUboot 문서 기준). 이미지 뒤에 서명과 메타(버전, 보안 카운터)를 붙인다(MCUboot의 TLV 트레일러).
- MCU 부트로더 안의 공개키(OTP 또는 보호된 flash)로 **부팅할 때마다** 검증한다 — 업데이트 시에만 검증하면 나중에 flash를 직접 고치는 공격을 못 막는다. 이 노트의 `boot_select()`에서 `slot_crc()` 자리에 서명 검증이 들어간다.
- **anti-rollback**: 이미지의 보안 카운터가 MCU에 기록된 최소값보다 작으면 거부. 단, §8.5의 롤백(trial 실패 → 이전 슬롯)과 충돌하지 않도록 "확정(COMMIT)된 이미지 기준으로만 카운터를 올린다". L4 §13과 같은 규칙이다.
- Linux는 서명을 검증할 필요가 없다(MCU가 한다). Linux는 단지 전달자다 — 그래서 **Linux가 해킹당해도 MCU에 악성 펌웨어를 올릴 수 없다.** 이것이 "Linux = 신뢰하지 않는 계층"이라는 N4의 안전 분할과 짝을 이룬다.
- 대안: 칩 벤더 ROM 부트로더(예: STM32 시스템 부트로더, UART/CAN/USB DFU)는 BOOT 핀을 Linux GPIO로 잡고 리셋하면 진입한다. 개발이 필요 없지만 대부분 서명·A/B·이어받기가 없어서 "공장 복구 경로"로 남겨 두고, 필드 업데이트는 자체 부트로더로 하는 구성이 흔하다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

`write(fd, frame, 35)`와 그 응답의 `read()` 한 번이 6.18 커널에서 지나는 길이다(함수 위치는 `/var/lab/src/linux-6.18.55`에서 grep으로 확인).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="170" y="20" text-anchor="middle" font-size="13">송신 (TX)</text>
  <text x="510" y="20" text-anchor="middle" font-size="13">수신 (RX)</text>
  <rect x="25" y="32" width="290" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="54" text-anchor="middle" font-size="12">write() → tty_write() (tty_io.c)</text>
  <rect x="25" y="82" width="290" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="104" text-anchor="middle" font-size="12">n_tty_write() — raw 라 가공 없음</text>
  <rect x="25" y="132" width="290" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="154" text-anchor="middle" font-size="12">uart_write() → xmit kfifo (serial_core)</text>
  <rect x="25" y="182" width="290" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="204" text-anchor="middle" font-size="12">__uart_start() → serial8250_start_tx()</text>
  <rect x="25" y="232" width="290" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="254" text-anchor="middle" font-size="12">THRE IRQ → serial8250_tx_chars()</text>
  <line x1="170" y1="66" x2="170" y2="80" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="116" x2="170" y2="130" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="166" x2="170" y2="180" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="216" x2="170" y2="230" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="365" y="232" width="290" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="254" text-anchor="middle" font-size="12">RX IRQ → serial8250_handle_irq()</text>
  <rect x="365" y="182" width="290" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="204" text-anchor="middle" font-size="12">serial8250_rx_chars() → flip buffer</text>
  <rect x="365" y="132" width="290" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="154" text-anchor="middle" font-size="12">tty_flip_buffer_push() → queue_work</text>
  <rect x="365" y="82" width="290" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="104" text-anchor="middle" font-size="12">kworker: flush_to_ldisc → n_tty</text>
  <rect x="365" y="32" width="290" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="54" text-anchor="middle" font-size="12">wake_up → epoll 깨움 → read()</text>
  <line x1="510" y1="232" x2="510" y2="218" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="182" x2="510" y2="168" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="132" x2="510" y2="118" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="82" x2="510" y2="68" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="315" y1="249" x2="363" y2="249" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="340" y="290" text-anchor="middle" font-size="12">주황 = 스케줄링이 끼는 곳 (workqueue 스레드 + 앱 스레드 깨우기 = 지연 꼬리의 출처)</text>
  <text x="340" y="312" text-anchor="middle" font-size="12">점선 = 선 (이 랩에서는 QEMU chardev → 프록시 → MCU)</text>
</svg>
```

그림 10 — tty 송수신 경로. H8 §11의 그림을 이 노트의 측정과 연결한 것이다.

- **TX**: 앱의 바이트는 serial core의 `xmit` kfifo(4 KB 원형 버퍼)에 들어가고 `write()`는 바로 돌아온다. 실제 송신은 THRE 인터럽트마다 `serial8250_tx_chars()`가 FIFO(16 B, `tx_loadsz`)를 채우는 식이다. `write()`가 돌아온 시점 ≠ 선에 나간 시점이다. 그래서 t1을 `write()` 직전에 찍어도 TX 큐 대기가 offset 비대칭에 들어간다. `tcdrain()`은 실제로 다 나갈 때까지 기다리지만 그 자체가 느리다.
- **RX**: `serial8250_handle_irq()` → `serial8250_rx_chars()`가 LSR을 보며 바이트를 flip buffer에 넣고(`uart_insert_char`), `tty_flip_buffer_push()`가 **항상 workqueue로** 넘긴다(§5.3). kworker가 `flush_to_ldisc()` → `n_tty_receive_buf()`를 돌리고, 읽기 대기자를 깨운다. epoll은 `n_tty_poll()`이 등록한 wait queue로 깨어난다(A4, G6의 `poll_wait` 구조 그대로).
- **오류 정보**: 8250은 LSR의 parity/framing/overrun 비트를 `TTY_PARITY`/`TTY_FRAME`/`TTY_OVERRUN` 플래그로 flip buffer에 함께 넣는다. raw 모드 + `INPCK` 없이는 앱에 안 보인다. 대신 `/proc/tty/driver/serial`의 `fe:`, `pe:`, `oe:`, `brk:` 카운터와 `TIOCGICOUNT` ioctl(H8 예제 9)로 본다. **링크 데몬은 이 카운터를 주기적으로 읽어 텔레메트리에 넣는다** — 프로토콜 CRC 오류가 늘 때 "선 문제(framing/overrun)"와 "소프트웨어 문제"를 가르는 단서다.
- **드라이버를 쓰게 되면**: 외부 MCU를 serdev로 붙이면(§5.6) 위 RX 경로의 n_tty 대신 `serdev_device_ops.receive_buf`가 불린다 — 여전히 workqueue 문맥이다(H8 §8.2의 `serdev-ttyport.c`). SPI라면 `spi_sync()`가 컨트롤러 드라이버의 메시지 큐 스레드(`spi_pump_messages`)를 거친다(H7). 어느 쪽이든 "IRQ → 스레드 → 소비자"라는 2단 구조와 그 우선순위 설정이 지연의 핵심이다(N2).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 Scout형 장비 — Jetson + STM32 센서/모터 보드

- 링크: UART 2~3 Mbit/s + RTS/CTS + DMA(양쪽), 또는 모터 쪽만 CAN FD. IMU 1 kHz는 MCU가 묶어서 10 ms마다 10샘플 한 프레임(프레임 오버헤드 1/10, 인터럽트 1/10).
- 유저스페이스 데몬 하나(`mcu-linkd`)가 tty를 소유. 내부 구조는 epoll 루프 하나: tty fd + timerfd(재전송·PING·heartbeat) + Unix 소켓(다른 프로세스의 명령) + ROS 2 퍼블리셔. 데몬은 systemd 서비스로 `Restart=always`, 그리고 MCU는 데몬의 heartbeat가 끊기면 failsafe(N4 §8).
- 메트릭: CRC 오류 수, NAK 수, 재전송 수, 텔레메트리 구멍, UART `fe/oe` 카운터, offset 추정 잔차, drift. 전부 텔레메트리로 올린다. 필드에서 "가끔 IMU가 튄다"는 리포트가 오면 이 숫자들이 1차 증거다.

### 10.2 생산 라인과 버전 관리

- 공장에서는 SWD/JTAG로 부트로더 + 슬롯 A를 굽는다. 그 이후로는 **부트로더를 절대 필드에서 업데이트하지 않는 것**이 기본(벽돌 위험). 그래서 부트로더는 작고, 프로토콜은 최소한으로 하위 호환을 유지한다.
- rootfs 이미지(L4)에 `mcu_fw_<ver>.bin` + 기대 버전 파일을 같이 넣는다. "검증된 조합"을 번들 단위로 배포한다는 N4 §9 원칙.

### 10.3 링크 디버깅 순서

1. 선: 스코프로 TX/RX 파형, baud 오차(양쪽 클럭 오차 합 2% 이내), 접지. framing error 카운터가 오르면 여기다.
2. 바이트: 로직 분석기의 UART 디코더로 실제 바이트를 보고, Linux에서는 `strace -e read,write -x`로 앱이 받은 바이트를 본다 — 둘이 다르면 tty 설정(raw가 아님) 문제.
3. 프레임: 데몬에 "프레임 hex 덤프" 디버그 플래그. CRC 오류만 오르고 framing 오류는 0이면 소프트웨어(버퍼 넘침, 경쟁 — §2.5 같은).
4. 타이밍: MCU가 프레임 처리 시작/끝에 GPIO를 토글하고 로직 분석기로 TX 핀과 같이 찍는다. 에뮬레이터 숫자(§5.4)를 대신할 진짜 값이다.

### 10.4 "그냥 USB CDC 쓰면 안 되나요?"

MCU가 USB 디바이스면 Linux에 `/dev/ttyACM0`로 보이고, 이 노트의 Linux 코드는 그대로 동작한다(USB가 CRC + 재전송을 해 주므로 오류는 거의 없다). 단점: USB 열거·재연결이 Linux 쪽 상태 머신을 복잡하게 하고(장치가 사라졌다 나타남 → udev, L2), 호스트 폴링 주기(FS bulk는 1 ms 프레임)가 지연 바닥이 되며, ESD/케이블 분리에 MCU 쪽 리셋이 엮인다. 같은 보드 안이면 UART/SPI, 케이블로 나가면 USB/CAN/Ethernet이 보통이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| tty를 raw로 안 엶 | 가끔 프레임 CRC 오류, 특정 바이트(0x0D, 0x03, 0x11)에서만 | N_TTY가 CR→LF 변환, ^C 시그널, XON/XOFF를 처리 | `cfmakeraw()` + `CLOCAL CREAD`, 매번 열 때 설정 (§5.1) |
| 프레임 CRC 없이 parity만 | 드물게 엉뚱한 명령 실행 | 바이트 유실·삽입은 parity가 못 잡음 | 프레임 CRC-16/32 필수 (§3) |
| ISR에서 비운 뒤 인터럽트 클리어 | 부하·버스트 때만 링크 영구 정지 | 클리어 직전에 온 바이트의 사건을 지움 | 클리어 먼저, 빌 때까지 비우기 (§2.5) |
| SysTick 횟수로 시간 계산 | drift가 수천~수십만 ppm, 부하 때 더 나쁨 | 인터럽트를 못 받은 만큼 시간 증발 | 자유 계수 HW 카운터를 직접 읽기 (§7.3) |
| NAK을 구멍마다 보냄 | 오류 하나에 재전송이 윈도우 × 윈도우 | 뒤따르는 프레임마다 NAK | NAK은 구멍당 한 번 (`nak_sent`) (§4.3) |
| 중복 프레임을 다시 실행 | ACK 손실 후 명령이 두 번 적용 (모터 두 번 움직임) | seq 중복 검사 없음 / 비멱등 명령 | 중복은 재ACK만, 명령은 절대값으로 (§4.5) |
| RESET의 ACK만 기다림 | 업데이트 성공을 실패/옛 버전으로 보고 | ACK가 사라지면 재부팅 HELLO가 유일한 응답 | 재부팅 HELLO도 성공 응답으로 처리 (§4.7) |
| TCP/USB-시리얼 버퍼링 무시 | RTT가 정확히 40 ms 또는 16 ms 계단 | Nagle + delayed ACK / FTDI latency_timer | `TCP_NODELAY`, `latency_timer=1` (§5.5) |
| 진행 포인터를 데이터보다 먼저 저장 | 전원 차단 후 이어받기가 최종 CRC에서 실패 | 포인터가 가리키는 데이터가 flash에 없음 | 데이터 쓰기 → 포인터 쓰기 순서 (§8.3) |
| 텔레메트리를 부팅 시각 기준 주기로 | 켜는 순간 수천 프레임 폭주 | 밀린 주기를 따라잡음 | 켤 때 `next = now`, 밀리면 건너뛰기 (§7.3) |

---

## 12. 면접에서 이렇게 말한다

**Q.** How would you design the communication protocol between a Linux SoC and a microcontroller over UART?

**A.** 바이트 스트림이니 먼저 프레이밍 — COBS에 0x00 구분자를 쓰면 오버헤드가 고정되고 다음 0에서 바로 재동기된다. 그 안에 버전·type·seq·ack·len 헤더와 CRC-32. 메시지를 둘로 나눠 센서 스트림은 seq만 붙여 손실을 세고, 명령은 ACK/NAK + 재전송 + 중복 검사로 신뢰성 있게, 명령은 멱등하게 설계한다. 시작 시 HELLO로 버전·상태를 교환하고, 오류 주입으로 시험한다.

> I'd treat it like a tiny transport layer. Framing with COBS and a zero delimiter, so overhead is bounded and the receiver resyncs at the next zero. Each frame has a version nibble, type, sequence, cumulative ack, length and a CRC-32. I split traffic into two classes: sensor streams are unreliable — just a sequence number so we can count gaps — and commands are reliable with ACK, NAK and a small go-back-N window, with duplicate detection and idempotent commands. A HELLO handshake exchanges protocol and firmware versions. And I always test it behind a fault-injecting proxy — in my lab, that's what exposed an ISR race that never showed up on a clean link.

**꼬리 질문**: "Why not just use SLIP?" → SLIP은 escape 때문에 최악 길이가 2배라 버퍼·대역폭 예산이 데이터에 따라 바뀐다. COBS는 254바이트당 1바이트로 고정.

**Q.** UART, SPI or CAN to connect the MCU — how do you choose?

**A.** 누가 말을 시작할 수 있는지가 먼저다. SPI는 마스터만 클럭을 내니 MCU 이벤트에 IRQ GPIO가 더 필요하다. 그다음 대역폭 예산(오버헤드 포함, 50% 여유), 거리·노이즈, 노드 수. 보드 안 보조 MCU 하나면 고속 UART + DMA, 고속 센서 블록이면 SPI + data-ready, 여러 노드·케이블·노이즈면 CAN FD — HW가 CRC와 재전송을 해 준다.

> First, who can initiate: UART and CAN are symmetric, SPI needs a data-ready GPIO because only the master clocks. Then a bandwidth budget with framing overhead and fifty percent headroom — 115200 baud is only about 11 kilobytes per second. Then distance, noise and node count. One companion MCU on the same board: a fast UART with DMA and RTS/CTS. Bulk sensor data on a short trace: SPI with a ready line. Several motor controllers over cables: CAN FD, where the controller already gives you CRC, arbitration and retransmission.

**Q.** How do you timestamp MCU sensor data in Linux time?

**A.** 도착 시각은 링크 지연 꼬리 때문에 쓸 수 없다. MCU가 샘플 순간에 자기 HW 카운터로 찍고, Linux는 NTP식 4-타임스탬프 PING으로 offset과 drift를 추정해 변환한다. 핵심은 delay가 가장 작은 교환만 쓰는 필터 — 내 랩에서 전 샘플을 쓰면 drift가 46% 틀렸고, 필터를 쓰면 참값 100 ppm을 ±5% 안에서 맞혔다. µs가 필요하면 GPIO 펄스를 양쪽 HW로 캡처하거나 PTP.

> Never use arrival time — the link latency tail is milliseconds. The MCU stamps samples with a free-running hardware counter, and Linux runs an NTP-style four-timestamp exchange to estimate offset and drift, using only the minimum-delay exchanges because the error bound is half the round-trip delay. In my lab that filter recovered a simulated 100 ppm drift within five percent, while the naive fit was off by almost half under load. For microsecond accuracy I'd add a sync pulse captured in hardware on both sides, or move to PTP over Ethernet.

**꼬리 질문**: "MCU 쪽 시계는 어떻게 만드나?" → SysTick 인터럽트 횟수가 아니라 자유 계수 타이머 값을 직접 읽는다. 인터럽트를 놓치면 tick 시계는 시간을 잃는다 — 실제로 25% 잃는 것을 측정했다(에뮬레이터 부하 조건).

**Q.** How do you update the MCU firmware from Linux safely in the field?

**A.** MCU 부트로더 + A/B 슬롯. 쓰기는 항상 실행 중이 아닌 슬롯, 조각마다 데이터 먼저 쓰고 진행 포인터 나중 — 전원이 나가도 끊긴 곳부터 이어받는다. 끝에 전체 CRC, 서명 검증, 시험 부팅 한 번, Linux 쪽 자가 점검 후 COMMIT, 확정 없이 리셋되면 부트로더가 롤백. rootfs OTA와 순서를 맞춰 MCU가 확정된 뒤에 rootfs를 mark-good 한다.

> A small immutable bootloader with two slots. We always write the inactive slot, chunk by chunk, and persist the progress pointer only after the data — so after a power cut the MCU reports its offset in HELLO and we resume, which I demonstrated by killing the MCU at 25 percent. At the end: whole-image CRC and signature check, one trial boot, Linux runs a self-test and sends COMMIT; if the MCU resets without a commit, the bootloader rolls back. The MCU commit happens before the rootfs is marked good, so we never end up with a half-rolled-back pair.

**Q.** What's your retransmission timeout, and how do you pick the window size?

**A.** RTO는 측정한 RTT의 p99.9 × 2~3 — 너무 작으면 멀쩡한 응답을 잃었다고 착각해 중복 폭주, 크면 손실 복구가 느리다. 그래서 NAK으로 빠른 재전송을 같이 쓴다. 윈도우는 대역폭-지연 곱 ÷ 프레임 크기, 상한은 MCU 수신 버퍼 ÷ 최대 프레임. 내 랩에서 W=1 → 4로 처리량이 3배.

> I measure the RTT distribution first and set the RTO around two to three times the p99.9, then rely on NAKs for fast retransmit so the timeout is the rare path — with stop-and-wait at five percent loss, two-thirds of the transfer time was spent waiting on timeouts. The window is the bandwidth-delay product divided by the frame size, capped by the MCU's receive buffer divided by the maximum frame, and the MCU advertises that credit in HELLO.

**Q.** Would you write a kernel driver for the MCU link?

**A.** 기본은 유저스페이스 데몬이 tty/spidev를 소유. 커널 드라이버(serdev 또는 spi_driver)는 MCU 데이터를 IIO·input·watchdog 같은 커널 서브시스템으로 노출해야 하거나, 부팅 초기에 필요하거나, kHz급 data-ready IRQ 지연을 줄여야 할 때. 그리고 6.18에서 tty `low_latency` 플래그는 효과가 없다 — flip buffer는 항상 workqueue로 간다.

> By default a user-space daemon owns the tty, because it's easy to debug and restart. I'd move to a serdev or SPI kernel driver when the data has to appear through a kernel subsystem like IIO or a watchdog, when it's needed before user space is up, or when a kilohertz data-ready interrupt needs tighter latency. One detail I checked in the 6.18 source: the old low_latency serial flag is accepted but unused — the flip buffer always goes through a workqueue, so the real knobs are thread priorities and FIFO trigger levels.

**Q.** How do you test such a link before hardware exists?

**A.** 양쪽 코드를 같은 헤더로 공유하고, MCU 펌웨어를 QEMU Cortex-M에서, Linux를 QEMU에서 돌려 소켓으로 잇고, 중간 프록시로 비트 오류·드롭·전원 차단을 주입한다. 시드를 고정해 재현. CI에 넣는다. 단, 타이밍은 에뮬레이터로 못 잰다 — baud 페이싱을 안 하니까 — 그건 실제 보드에서 로직 분석기로.

> Share the framing code between both sides, run the MCU firmware on a Cortex-M QEMU machine and Linux on another QEMU, connect them through a socket with a fault-injecting proxy — bit flips, drops, power cuts — with fixed seeds so failures replay, and put it in CI. What emulation can't give you is timing: the UART models don't pace by baud rate, so latency numbers come from real boards and a logic analyzer.

---

## 13. 직접 해보기

1. **(개념)** 115200 baud 8N1 링크에 1 kHz × 24 B IMU 샘플을 이 노트의 프레임(헤더 5 + CRC 4 + COBS 2)으로 보낼 수 있나? 10샘플을 한 프레임으로 묶으면? 정답/힌트: 개별 35 B × 1000 = 35 kB/s > 11.5 kB/s로 불가. 묶으면 (240 + 11) × 100 = 25 kB/s로 여전히 불가 — baud를 올려야 한다(921600이면 92 kB/s).
2. **(코드)** `t_frame.c`를 고쳐 CRC-32 대신 CRC-16-CCITT(0x1021, init 0xFFFF)를 쓰고, 2비트 오류 전수(모든 비트 쌍)와 1e6 버스트에서 미검출 수를 비교하라. 정답/힌트: 짧은 프레임에선 2비트 전수 미검출 0이 나와야 한다(해밍 거리 4). 버스트에서는 기대 미검출 ≈ 1e6 × (CRC까지 간 비율) × 2^-16 ≈ 수 개.
3. **(코드)** `o3_fw.c`의 RTO를 30 ms로 줄이고 깨끗한 링크에서 돌려 `retx`가 0이 아닌지 확인하라. 정답/힌트: 게스트 RTT p99가 50~70 ms이므로 정상 응답보다 타이머가 먼저 터져 "가짜 재전송"이 생긴다. RTO는 측정에서 나온다.
4. **(설계)** §4.7의 HELLO 중복 문제를 nonce로 고쳐라 — HELLO 요청에 4 B nonce, MCU는 같은 nonce면 rx seq를 리셋하지 않고 응답만, Linux는 현재 nonce가 아닌 응답을 버림. 정답/힌트: MCU에 `last_nonce` 하나, `link_hello()`에 nonce 생성과 비교. 전원 차단 실험(§8.4)에서 HELLO가 두 번(재부팅 1 + 핸드셰이크 1)만 찍히면 성공.
5. **(코드)** MCU의 텔레메트리 따라잡기 폭주(§7.3)를 고치고 `o3_tl`의 첫 5초 구간 `timer-linux`가 ±수십 ms로 들어오는지 보라. 정답/힌트: `telem_on`을 켤 때 `next = now_us()`, 루프에서 `if (now >= next) { send; do next += 10000; while (next <= now); }`.
6. **(관찰)** 게스트에서 링크를 돌리는 중에 `cat /proc/tty/driver/serial`을 여러 번 보고 `tx:`/`rx:` 카운터 증가량으로 처리량을 계산하라. 프록시로 비트 오류를 넣으면 `fe:`/`pe:`가 오르나? 정답/힌트: 오르지 않는다 — 프록시는 바이트 내용을 바꿀 뿐 UART 전기 신호(framing/parity)를 건드리지 않는다. 실제 선의 노이즈와 프로토콜 CRC 오류를 구분하는 이유다.
7. **(관찰)** 게스트에서 `trace-cmd record -e workqueue:workqueue_execute_start -e irq:irq_handler_entry`(rootfs-dom에 있음)로 PING 하나의 수신 경로를 찍어 8250 IRQ → `flush_to_ldisc` kworker → 앱 깨우기 사이 간격을 재라. 정답/힌트: §9 그림의 주황 상자 두 개가 실제로 보인다. tracefs는 M 모듈 노트 방식으로 마운트.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| COBS | Consistent Overhead Byte Stuffing | 0x00이 없도록 인코딩해 0x00을 프레임 구분자로 쓰는 방식, 오버헤드 ≤ 254 B당 1 B |
| SLIP | Serial Line IP (RFC 1055) | 0xC0 구분자 + escape. 최악 2배 길이 |
| CRC-32 (IEEE) | 다항식 0x04C11DB7 (반사 0xEDB88320) | Ethernet FCS와 같은 오류 검출 코드, 무작위 미검출 확률 2^-32 |
| seq / ack | 송신 번호 / 누적 확인 번호 | "여기까지 받았다"를 한 숫자로 알림 |
| NAK | negative acknowledgement | "구멍이 있다, 지금 다시 보내라" — 타임아웃보다 빠른 재전송 |
| Go-Back-N | 구멍 이후 전부 재전송하는 윈도우 방식 | 수신 측 버퍼가 하나면 이것 |
| Selective Repeat | 빠진 것만 재전송 | 수신 측이 순서 밖 프레임을 보관해야 함 |
| RTO | retransmission timeout | 응답이 없을 때 재전송까지 기다리는 시간 |
| 멱등 (idempotent) | 여러 번 해도 한 번 한 것과 같음 | 재전송 설계를 단순하게 만드는 명령 성질 |
| 대역폭-지연 곱 | 링크 속도 × RTT | 그만큼 데이터가 "날아가는 중"이어야 링크가 꽉 찬다 → 윈도우 크기 |
| 백프레셔 | 받는 쪽이 보내는 쪽을 늦춤 | 크레딧(윈도우), RTS/CTS, tty throttle |
| termios raw | `cfmakeraw()` 설정 | 줄 편집·변환·시그널 없는 8비트 투명 tty |
| VMIN / VTIME | non-canonical read 반환 조건 | n바이트 또는 바이트 간 시간 초과 |
| flip buffer | tty 드라이버 → line discipline 사이 버퍼 | 6.18에선 항상 workqueue로 넘어감 |
| RX trigger level | UART FIFO 인터럽트 임계값 | 낮으면 지연↓ 인터럽트↑ (`rx_trig_bytes`) |
| serdev | serial device bus | UART 끝의 장치를 DT로 기술하고 커널 드라이버가 소유 |
| spidev | `/dev/spidevB.C` | 유저스페이스에서 SPI 트랜잭션 |
| offset / drift | 두 시계의 차이 / 그 차이의 변화율 | drift 단위 ppm = µs/s |
| PPS | pulse per second | 1초 펄스로 시각 맞춤, Linux `pps-gpio` |
| input capture | MCU 타이머가 에지 순간 카운터 값을 래치 | µs 이하 HW 타임스탬프 |
| A/B 슬롯 | 펌웨어 두 벌 | 실행 중이 아닌 쪽에 쓰고 시험 부팅 후 확정 |
| trial / commit | 시험 부팅 / 확정 | 확정 없이 리셋되면 롤백 |
| anti-rollback | 옛 (취약) 버전 설치 거부 | 보안 카운터 |
| semihosting | 타깃이 `bkpt 0xAB`로 호스트 서비스 호출 | 이 랩에서 MCU의 "flash 파일" |
| Nagle | 작은 TCP 쓰기를 모아 보내는 알고리즘 | delayed ACK과 만나면 40 ms 지연, `TCP_NODELAY`로 끔 |

---

## 15. 요약 & 체크리스트

Linux ↔ 외부 MCU 링크는 작은 전송 계층이다. 먼저 누가 말을 시작할 수 있는지와 대역폭 예산으로 링크를 고르고(보조 MCU 하나면 고속 UART + DMA), 바이트 스트림 위에 COBS 프레임 + CRC-32를 얹고, 메시지를 "버려도 되는 스트림"과 "반드시 가야 하는 명령"으로 나눠 명령에만 seq/ACK/NAK/윈도우 재전송을 붙인다. Linux 쪽은 tty를 raw로 열고 epoll로 기다리며, 지연 꼬리는 IRQ → workqueue → 앱의 두 번 스케줄링에서 온다(6.18의 `low_latency`는 무효). 시간은 MCU의 자유 계수 HW 카운터로 찍고, 4-타임스탬프 교환 + 최소 지연 필터로 offset/drift를 추정한다. MCU 펌웨어는 A/B 슬롯 부트로더에 "데이터 먼저, 포인터 나중"으로 쓰고, 시험 부팅 → 자가 점검 → COMMIT, 확정 없는 리셋은 롤백한다. 이 모든 것은 오류 주입 없이는 검증할 수 없다 — 이 노트의 실제 버그 다섯 개(ISR 클리어 순서, tick 시계, RESET 응답, HELLO 중복, 텔레메트리 폭주) 중 네 개가 오류·부하 조건에서만 드러났다.

- [ ] UART/SPI/CAN/USB/Ethernet을 "누가 시작하나 · 대역폭 · 거리 · HW 오류 처리"로 비교할 수 있다
- [ ] baud에서 바이트 시간과 최대 처리량을 계산하고 대역폭 예산을 세울 수 있다
- [ ] COBS 인코딩/디코딩을 손으로 할 수 있고, SLIP·SOF+len과의 차이를 설명할 수 있다
- [ ] 프레임 헤더(버전, type, seq, ack, len)와 CRC 범위를 설계할 수 있다
- [ ] Go-Back-N 송신기와 수신 규칙(중복 재ACK, 구멍 NAK 한 번)을 C로 짤 수 있다
- [ ] RTO와 윈도우 크기를 측정값(RTT 분포, 대역폭-지연 곱, MCU 버퍼)으로 정할 수 있다
- [ ] tty를 raw + epoll로 여는 코드를 쓰고 기본 termios의 위험을 설명할 수 있다
- [ ] RX FIFO 트리거·DMA·`low_latency`가 지연에 미치는 영향을 설명할 수 있다
- [ ] 래치형 인터럽트 ISR에서 "클리어 먼저, 그다음 비우기"인 이유를 설명할 수 있다
- [ ] 4-타임스탬프로 offset/delay를 계산하고 최소 지연 필터가 왜 필요한지 설명할 수 있다
- [ ] MCU 타임스탬프를 tick 횟수가 아니라 HW 카운터로 만들어야 하는 이유를 말할 수 있다
- [ ] A/B 슬롯 업데이트의 이어받기·시험 부팅·롤백·서명 흐름과 rootfs OTA와의 순서를 설명할 수 있다

---

## 참고 자료

- termios(3), tty_ioctl(4), epoll(7), cfmakeraw — man7.org: https://man7.org/linux/man-pages/man3/termios.3.html , https://man7.org/linux/man-pages/man7/epoll.7.html
- Michael Kerrisk, "The Linux Programming Interface", 62장 Terminals, 63장 Alternative I/O Models
- docs.kernel.org — Serial: https://docs.kernel.org/driver-api/serial/index.html , TTY: https://docs.kernel.org/driver-api/tty/index.html , PPS: https://docs.kernel.org/driver-api/pps.html , GPIO 문자 장치 uAPI: https://docs.kernel.org/userspace-api/gpio/chardev.html
- 소스 (6.18.55): `drivers/tty/tty_buffer.c`(`tty_flip_buffer_push`), `drivers/tty/serial/8250/8250_port.c`(`uart_config[]`, `serial8250_rx_chars`), `drivers/tty/serial/serial_core.c`(`uart_write`), `drivers/tty/n_tty.c`, `drivers/tty/serdev/`, `drivers/pps/clients/pps-gpio.c`, `drivers/hte/` — https://elixir.bootlin.com/linux/v6.18.55/source
- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM Transactions on Networking, 1999
- RFC 1055 (SLIP): https://www.rfc-editor.org/rfc/rfc1055 , RFC 5905 (NTPv4 — offset/delay 공식): https://www.rfc-editor.org/rfc/rfc5905
- Philip Koopman, CRC 다항식 해밍 거리 표: https://users.ece.cmu.edu/~koopman/crc/
- MCUboot 문서 (A/B, test/confirm, 서명, TLV): https://docs.mcuboot.com/
- ARM, "ARMv7-M Architecture Reference Manual" (WFI wake-up, PRIMASK, SysTick), ARM CMSDK APB UART/Timer TRM (Cortex-M System Design Kit)
- QEMU 문서 — chardev 옵션(`nodelay`), semihosting, mps2 머신: https://www.qemu.org/docs/master/system/invocation.html , https://www.qemu.org/docs/master/system/arm/mps2.html
- 이 저장소 관련 노트: N4(분담·heartbeat), H8(UART/tty/serdev), H7(SPI), L4(OTA), O2(rpmsg), Q3(PPS/PTP), Q4(CAN)
