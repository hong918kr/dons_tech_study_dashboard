# C03. BSP와 주변장치 드라이버 — 클럭·GPIO·UART·SPI·I2C·I2S/PDM·DMA를 핀 레벨부터 드라이버 계층까지

> **이 노트를 다 읽으면**: 새 보드의 BSP가 무엇으로 이루어지는지 설명한다 · UART/SPI/I2C/I2S 파형을 화이트보드에 그리고 설정 실수를 파형에서 찾는다 · I2C 버스 hang을 복구하고 오디오 DMA ping-pong 버퍼를 설계한다 · Zephyr devicetree + device model로 드라이버를 쓴다
> **JD 연결**: "Develop board support packages (BSPs) and integrate peripheral drivers (SPI, I2C, UART, I2S)" · "Work directly with the hardware team on new silicon and sensor integrations"
> **Don 기준 난이도**: I2C/SPI/UART/DMA·레지스터·bring-up은 이미 강함 / **I2S·PDM·TDM 오디오 경로, Zephyr devicetree·device model**은 새로 배울 부분

---

## 0. 큰 그림

BSP(Board Support Package)는 "이 보드에서 OS와 애플리케이션이 돌아가게 만드는 보드 전용 코드 묶음"이다. 칩(SoC/MCU) 벤더가 주는 코드는 **칩**까지만 안다. 어떤 핀에 어떤 센서가 붙었는지, 어떤 수정 발진기가 달렸는지, 전원 레일을 어떤 순서로 켜는지는 **보드**가 정한다. 그 차이를 메우는 것이 BSP다.

예를 들어 Hark 같은 always-on 기기라면(구조는 추정) 저전력 MCU 쪽은 대략 이렇게 생겼을 것이다.

```
                 +-------------------------------------------------------+
                 |                 Always-on MCU (Cortex-M)              |
                 |                                                       |
  32.768kHz XTAL-+-> LFCLK --> RTC / sys tick (sleep 중에도 동작)         |
  32MHz XTAL ----+-> HFCLK --> PLL --> CPU / AHB / APB / audio clock      |
                 |                                                       |
                 |  GPIO ---- 버튼, LED, 센서 INT, SoC wake 라인          |
                 |  UART ---- 디버그 콘솔 / SoC 와의 IPC / BT HCI         |
                 |  SPI  ---- 외부 NOR flash, 디스플레이, IMU             |
                 |  I2C  ---- PMIC, fuel gauge, 온도, 햅틱 드라이버       |
                 |  I2S  ---- 오디오 코덱 / 스피커 앰프 (PCM)              |
                 |  PDM  ---- 디지털 MEMS 마이크 2개 (wake word 입력)      |
                 |  DMA  ---- 위 주변장치 <-> SRAM 을 CPU 없이 복사        |
                 +-------------------------------------------------------+
```

소프트웨어 쪽에서 보면 드라이버는 층으로 나뉜다. 이 노트는 아래 그림의 아래 세 층을 다룬다. RTOS 층은 C04에서 다룬다.

```
  +--------------------------------------------------+
  |  Application (wake word, sensor fusion, UI)      |
  +--------------------------------------------------+
  |  Subsystem / Service (audio pipeline, sensor fw) |
  +--------------------------------------------------+
  |  Driver API (i2c_transfer, spi_transceive, ...)  |  <- 이식 가능한 인터페이스
  +--------------------------------------------------+
  |  Device driver (칩별: nrfx TWIM, STM32 I2C ...)  |  <- 레지스터를 만지는 곳
  +--------------------------------------------------+
  |  BSP: clock, pinmux, power rail, board config    |  <- 보드마다 다른 곳
  +--------------------------------------------------+
  |  Hardware (SoC 레지스터, 핀, 외부 칩)             |
  +--------------------------------------------------+
```

---

## 1. BSP는 무엇으로 이루어지나

### 1.1 구성 요소

| 구성 요소 | 하는 일 | bare-metal에서 | Zephyr에서 |
|---|---|---|---|
| Startup code | reset vector, 스택 설정, .data 복사, .bss 0 초기화 | `startup_xxx.s`, `SystemInit()` | arch/soc 코드가 제공 |
| Clock 설정 | 발진기 선택, PLL, 버스 분주 | `SystemClock_Config()` | devicetree clock 노드 + SoC 초기화 |
| Pin mux | 핀을 GPIO/UART/I2C 등 기능에 연결 | AF 레지스터 직접 설정 | `pinctrl` 노드 (`pinctrl-0`) |
| 전원 시퀀스 | 레일 enable 순서, 센서 전원 GPIO | board init 함수 | `regulator` 노드, `SYS_INIT()` |
| 메모리 맵 | flash/SRAM/외부 메모리 영역 | 링커 스크립트 | devicetree `chosen` + 파티션 |
| 드라이버 인스턴스 | 어떤 버스에 어떤 칩이 몇 번 주소로 | 하드코딩 헤더 | devicetree 자식 노드 |
| 기능 선택 | 무엇을 빌드에 넣나 | `#define` | Kconfig (`prj.conf`, board defconfig) |

### 1.2 Zephyr 보드 디렉터리

Zephyr에서는 BSP가 "보드 정의"라는 형태로 정리되어 있다. Hardware Model v2(Zephyr 3.7부터) 기준으로 대략 이렇게 생겼다.

```
boards/<vendor>/<board>/
  board.yml                 # 보드 이름, SoC, 변형(variant)
  <board>.dts               # devicetree: 핀, 버스, 외부 칩, 파티션
  <board>-pinctrl.dtsi      # 핀 기능 배치
  <board>_defconfig         # 기본 Kconfig 값
  Kconfig.<board>           # 보드 Kconfig 심볼
  board.cmake               # west flash/debug 러너 설정 (J-Link, OpenOCD ...)
```

### 1.3 bring-up 순서

새 보드(EVT)가 왔을 때 BSP를 올리는 순서는 "가장 적게 가정하는 것부터"다.

1. 전원 레일과 reset 라인을 스코프로 확인한다. 레일 ramp, reset 해제 타이밍.
2. SWD로 붙는지 확인한다. `J-Link Commander`나 OpenOCD로 IDCODE를 읽는다.
3. 내부 RC 발진기로만 부팅해서 LED 토글(GPIO)을 본다. 클럭 트리를 의심하지 않아도 되는 최소 상태다.
4. UART 콘솔을 띄운다. 이후 모든 디버깅의 기반이다.
5. 외부 크리스털(HFXO/LFXO)과 PLL로 전환한다. 전환 후 UART baud가 맞는지로 클럭을 검증한다.
6. I2C로 PMIC·센서의 WHO_AM_I 레지스터를 읽는다. SPI flash의 JEDEC ID(명령 `0x9F`)를 읽는다.
7. DMA, 인터럽트 기반 드라이버, 오디오(I2S/PDM), 저전력 모드 순으로 올린다.

> **Don 경험과 연결**: SSD 컨트롤러 FPGA bring-up에서 "JTAG 연결 → SRAM에서 코드 실행 → UART 로그 → DRAM/PCIe"로 올렸던 것과 순서가 같다. 차이는 컨슈머 기기라서 오디오·센서·PMIC 같은 **외부 칩 종류가 훨씬 많다**는 것이다.

---

## 2. 클럭 트리

### 2.1 왜 클럭 트리가 BSP의 핵심인가

모든 주변장치의 타이밍(UART baud, SPI SCK, I2C SCL, I2S BCLK, 타이머)은 결국 한 개의 소스 클럭을 나누어 만든다. 소스가 틀리면 **모든 것이 조금씩 틀린다**. 또 클럭은 전력과 직결된다. 동적 전력은 주파수에 비례하므로(C05) 필요 없는 가지는 꺼야 한다.

```
  HFXO 32MHz ---+
                +--[MUX]--> SYSCLK --[/1]--> HCLK (CPU, AHB, DMA)
  HFINT (RC) ---+     |                  +--[/2]--> APB1 (I2C, UART)
                      |                  +--[/1]--> APB2 (SPI, timer)
                      +--> PLL --> audio PLL --> MCLK 12.288MHz --> I2S
  LFXO 32.768kHz ------> LFCLK --> RTC (tick, wake timer)  <- deep sleep 중에도 동작
```

### 2.2 알아야 할 개념

- **RC 발진기 vs 크리스털**: 내부 RC는 빨리 켜지고 부품이 필요 없지만 정확도가 수 %다. 크리스털은 수십 ppm이다. UART는 수 % 오차를 버티지만 BLE 라디오는 ±50ppm 이내의 sleep clock이 필요하다(BLE Core Spec의 sleep clock accuracy 요구).
- **Peripheral clock gating**: 대부분의 MCU는 주변장치마다 클럭 enable 비트가 있다(STM32의 `RCC->APB1ENR` 등). enable하지 않고 레지스터에 쓰면 **쓰기가 무시**되거나 bus fault가 난다.
- **오디오 클럭**: 48kHz 계열은 12.288MHz(= 256 × 48k), 44.1kHz 계열은 11.2896MHz(= 256 × 44.1k)가 정확히 나와야 한다. 일반 시스템 클럭(예: 64MHz)을 정수로 나누면 정확한 값이 안 나오기 때문에 별도 audio PLL이나 분수 분주기를 쓴다. 정확히 안 나오면 샘플레이트가 조금 틀어지고, 긴 시간 동안 버퍼 overflow/underflow가 생긴다.
- **클럭 전환 순서**: 새 소스를 켜고 → 안정화(ready 플래그) 대기 → flash wait state를 **먼저** 늘리고 → MUX 전환. 반대로 하면 flash가 너무 빠른 클럭에서 읽혀 HardFault가 난다.

### 2.3 흔한 함정

- PLL 전환 후 UART가 깨진 글자를 찍는다 → APB 분주가 바뀌었는데 baud 계산에 옛 클럭 값을 쓴 경우다.
- nRF52 계열은 라디오·정확한 I2S 클럭이 필요할 때 HFXO를 요청해야 한다. 요청하지 않으면 HFINT로 돈다. Zephyr에서는 `onoff` 기반 clock control API(`z_nrf_clock_control_get_onoff()` 등, 벤더 전용)로 요청한다(벤더마다 다름).

---

## 3. GPIO

### 3.1 하드웨어 구조

```
            VDD
             |
          [P-FET]  <- push-pull 일 때만 사용
             |
  PAD -------+------------> 입력 버퍼 --> IN 레지스터 --> 엣지 검출 --> IRQ
             |
          [N-FET]  <- push-pull, open-drain 모두 사용
             |
            GND
   (내부 pull-up / pull-down 저항은 약 수십 kΩ, 벤더마다 다름)
```

| 설정 | 의미 | 쓰는 곳 |
|---|---|---|
| Push-pull 출력 | High/Low 둘 다 능동 구동 | LED, chip select, enable 핀 |
| Open-drain 출력 | Low만 구동, High는 pull-up이 끌어올림 | I2C, wired-OR 인터럽트 라인, 전압이 다른 칩과 연결 |
| 입력 + pull-up | 버튼(눌리면 GND) | 버튼, active-low INT |
| Drive strength | 출력 전류 능력/엣지 속도 | 빠른 SPI는 high drive, 나머지는 EMI 때문에 standard |
| Analog/disconnect | 입력 버퍼 끔 | **저전력**: 안 쓰는 핀의 떠 있는 입력은 누설 전류의 흔한 원인 |

### 3.2 Zephyr에서 GPIO 쓰기 (버튼 인터럽트 → LED)

devicetree에 이미 `led0`, `sw0` alias가 있는 보드(nRF52840 DK 등) 기준이다.

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback btn_cb;

static void btn_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    gpio_pin_toggle_dt(&led);          /* ISR 안: 짧은 일만 */
}

int main(void)
{
    if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&btn)) {
        return -ENODEV;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&btn, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&btn_cb, btn_isr, BIT(btn.pin));
    gpio_add_callback(btn.port, &btn_cb);

    while (1) {
        k_sleep(K_FOREVER);            /* 할 일 없음: idle -> 저전력 */
    }
    return 0;
}
```

한 줄씩 보면:

- `GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios)`: devicetree의 `led0` 노드에서 **포트 장치 + 핀 번호 + 플래그(active-low 등)**를 컴파일 타임에 꺼내 구조체로 만든다. 핀 번호를 코드에 하드코딩하지 않는다.
- `gpio_is_ready_dt()`: 포트 드라이버 초기화가 성공했는지 확인한다.
- `GPIO_OUTPUT_INACTIVE`: "논리적으로 비활성"으로 초기화한다. LED가 active-low로 배선되어 있으면 devicetree 플래그 덕분에 드라이버가 알아서 High를 출력한다. **논리 레벨과 물리 레벨을 분리**하는 것이 Zephyr GPIO의 핵심이다.
- `GPIO_INT_EDGE_TO_ACTIVE`: 비활성 → 활성으로 바뀌는 엣지에서 인터럽트. active-low 버튼이면 물리적으로는 falling edge다.
- `gpio_init_callback` / `gpio_add_callback`: 포트 하나에 콜백 여러 개를 연결 리스트로 붙이는 구조다.

### 3.3 흔한 함정

- 버튼 바운스: 기계식 스위치는 수 ms 동안 수십 번 튄다. ISR에서 바로 동작하지 말고 타이머(예: `k_work_delayable` 20ms)로 debounce한다(C04).
- 레벨 인터럽트를 ISR에서 원인 해제 없이 끝내면 **인터럽트 폭풍**(ISR이 끝나자마자 다시 진입)이 생긴다.
- sleep 전에 안 쓰는 핀을 disconnect/analog로 두지 않으면 떠 있는 입력이 중간 전압에서 흔들리며 수십~수백 µA를 먹는다.

---

## 4. 데이터를 옮기는 세 가지 방법: polling, interrupt, DMA

| 방식 | 동작 | CPU 부하 | 레이턴시 | 전력 | 쓰는 곳 |
|---|---|---|---|---|---|
| Polling | CPU가 상태 비트를 계속 읽음 | 100% (기다리는 동안) | 가장 낮고 예측 가능 | 나쁨 (sleep 불가) | 부트 초기, 아주 짧은 전송, 패닉 핸들러 |
| Interrupt (바이트당) | 바이트마다 ISR 진입 | 바이트 수 × ISR 비용 | 낮음 | 중간 | 저속 UART, 간헐적 I2C |
| DMA | 컨트롤러가 버스 마스터로 복사, 끝나면 IRQ 1번 | 거의 0 | 블록 단위 | 좋음 (CPU sleep 가능) | 오디오, SPI flash, 고속 UART, ADC 스트림 |

### 4.1 계산으로 감 잡기

1Mbaud UART(8N1, 10비트/바이트)는 초당 10만 바이트다. 바이트당 ISR이 진입·복귀 포함 200 사이클이라면 64MHz CPU에서 초당 2천만 사이클, 즉 **CPU의 약 31%**가 ISR에 쓰인다. DMA + idle-line 인터럽트로 바꾸면 수 %로 떨어진다.

16kHz 16-bit 모노 오디오는 초당 32KB다. 샘플마다 인터럽트를 받으면 초당 1만6천 번 깨어난다. DMA로 10ms(160샘플) 블록을 받으면 초당 100번만 깨어난다. **CPU가 깨어나는 횟수가 곧 전력**이다.

### 4.2 DMA와 캐시

Cortex-M7(및 Cortex-A)처럼 D-cache가 있는 코어에서는 DMA가 쓴 SRAM 내용을 CPU가 캐시에서 옛 값으로 읽을 수 있다. 수신 버퍼는 DMA 완료 후 invalidate(CMSIS `SCB_InvalidateDCache_by_Addr()`), 송신 버퍼는 DMA 시작 전 clean(`SCB_CleanDCache_by_Addr()`)해야 한다. 버퍼는 캐시 라인(32바이트) 단위로 정렬하고 크기도 라인의 배수로 한다. 아니면 옆 변수까지 invalidate되어 데이터가 사라진다. 대안은 MPU로 DMA 영역을 non-cacheable로 두는 것이다. Cortex-M4/M33처럼 D-cache가 없는 코어는 이 문제가 없다.

> **Don 경험과 연결**: SSD 컨트롤러에서 host DMA 버퍼와 캐시 일관성(coherency)을 다룬 경험이 그대로 쓰인다. 면접에서 "DMA 버퍼를 왜 32바이트 정렬했나?"에 바로 답할 수 있어야 한다.

---

## 5. UART

### 5.1 프레임 파형 (8N1, 문자 'A' = 0x41 = 0b0100_0001)

```
 idle  start  b0  b1  b2  b3  b4  b5  b6  b7  stop  idle
 ----+     +---+                       +---+   +--------
     |     |   |                       |   |   |
     +-----+   +-----------------------+   +---+
       0     1   0   0   0   0   0   1   0    1
            LSB first  ------------------>  MSB
```

- idle 상태는 High(mark). start bit는 Low 1비트, 데이터는 **LSB first**, stop bit는 High.
- 클럭 선이 없다(비동기). 수신기는 start bit의 falling edge로 동기를 잡고, 보통 비트당 16배(또는 8배) 오버샘플링해 비트 가운데서 샘플한다.
- 양쪽 baud 오차 합이 대략 수 % 이내여야 한다. 한 프레임(10비트) 끝에서 샘플 위치가 반 비트 이상 밀리면 framing error가 난다(허용치는 오버샘플링 방식에 따라 다름).

### 5.2 알아야 할 기능

| 기능 | 설명 | 왜 중요한가 |
|---|---|---|
| RTS/CTS hardware flow control | 수신 측 버퍼가 차면 CTS를 내려 송신 중지 | BT/Wi-Fi 콤보칩 HCI UART(수 Mbaud)에서 사실상 필수 |
| Idle line detect | 1프레임 이상 라인이 쉬면 인터럽트 | 길이를 모르는 패킷을 DMA로 받을 때 "패킷 끝" 신호 |
| Break | 1프레임 이상 Low 유지 | LIN, 일부 부트로더 진입 신호 |
| Framing/parity/overrun error | 수신 오류 플래그 | overrun = ISR/DMA가 늦어 데이터를 잃음 |

### 5.3 bare-metal RX 인터럽트 + 링버퍼

아래 레지스터 주소와 비트는 **가상의 UART IP**다. 구조는 대부분의 MCU와 같다.

```c
#include <stdint.h>
#include <stdbool.h>

/* 가상의 UART 레지스터 맵 (설명용, 실제 칩과 무관) */
#define UART0_BASE   0x40002000u
#define UART_DATA    (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_STATUS  (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_STATUS_RXNE   (1u << 0)   /* 수신 데이터 있음 */
#define UART_STATUS_ORE    (1u << 3)   /* overrun */

#define RX_BUF_SIZE 256u               /* 2의 거듭제곱: 나머지 연산을 마스크로 */
static uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head;      /* ISR만 쓴다 */
static volatile uint32_t rx_tail;      /* task만 쓴다 */
static volatile uint32_t rx_dropped;

void UART0_IRQHandler(void)
{
    uint32_t status = UART_STATUS;
    while (status & UART_STATUS_RXNE) {
        uint8_t byte = (uint8_t)UART_DATA;          /* 읽으면 RXNE 클리어 */
        uint32_t next = (rx_head + 1u) & (RX_BUF_SIZE - 1u);
        if (next != rx_tail) {
            rx_buf[rx_head] = byte;
            __asm volatile ("dmb" ::: "memory");    /* 데이터 쓰기 후 head 갱신 */
            rx_head = next;
        } else {
            rx_dropped++;                           /* 가득 참: 버림 + 카운트 */
        }
        status = UART_STATUS;
    }
    if (status & UART_STATUS_ORE) {
        rx_dropped++;                               /* 하드웨어 overrun (클리어 방법은 IP마다 다름) */
    }
}

bool uart_getc(uint8_t *out)
{
    if (rx_tail == rx_head) {
        return false;
    }
    *out = rx_buf[rx_tail];
    __asm volatile ("dmb" ::: "memory");
    rx_tail = (rx_tail + 1u) & (RX_BUF_SIZE - 1u);
    return true;
}
```

- **SPSC(single producer, single consumer)** 구조라 lock이 필요 없다. ISR만 `rx_head`를, task만 `rx_tail`을 쓴다.
- 한 칸을 비워 두어 "가득 참"과 "비어 있음"을 구분한다(`next != rx_tail`).
- `dmb`는 단일 코어 Cortex-M에서는 사실상 필요 없는 경우가 많지만, 데이터 쓰기와 인덱스 갱신 순서를 컴파일러·CPU 모두에게 명시하는 습관이다. 핵심은 `"memory"` clobber로 컴파일러 재배치를 막는 것이다.
- RTOS 위에서는 ISR 끝에 task를 깨우는 신호(`vTaskNotifyGiveFromISR`, `k_sem_give`)를 추가한다(C04).

### 5.4 Zephyr의 UART API 세 종류

| API | 함수 예 | 용도 |
|---|---|---|
| Polling | `uart_poll_out()`, `uart_poll_in()` | 패닉 출력, 아주 단순한 콘솔 |
| Interrupt-driven | `uart_irq_callback_user_data_set()`, `uart_irq_rx_enable()`, `uart_fifo_read()` | 바이트 단위 처리 |
| Async (DMA) | `uart_callback_set()`, `uart_rx_enable()`, `uart_tx()`, 이벤트 `UART_RX_RDY`, `UART_RX_BUF_REQUEST` | 고속·저전력 스트림. idle-line 타임아웃 포함 |

---

## 6. SPI

### 6.1 신호와 모드

SPI는 4선 동기 full-duplex 버스다. SCLK(클럭), MOSI(=SDO/PICO, 컨트롤러 → 주변), MISO(=SDI/POCI), CS(active-low chip select). 클럭 한 번에 양방향으로 1비트씩 동시에 교환된다. 내부적으로는 **두 개의 시프트 레지스터가 고리로 연결된 구조**다.

| Mode | CPOL | CPHA | 클럭 idle | 샘플 엣지 | 데이터 변경 엣지 |
|---|---|---|---|---|---|
| 0 | 0 | 0 | Low | rising (첫 엣지) | falling |
| 1 | 0 | 1 | Low | falling (둘째 엣지) | rising |
| 2 | 1 | 0 | High | falling (첫 엣지) | rising |
| 3 | 1 | 1 | High | rising (둘째 엣지) | falling |

```
 Mode 0 (CPOL=0, CPHA=0), MSB first, 1 byte

 CS   ----+                                               +----
          |_______________________________________________|
 SCLK  _______/‾‾\__/‾‾\__/‾‾\__/‾‾\__/‾‾\__/‾‾\__/‾‾\__/‾‾\________
 MOSI  ----< b7 >< b6 >< b5 >< b4 >< b3 >< b2 >< b1 >< b0 >----
               ^     ^     ^     ^     ^     ^     ^     ^
               샘플(rising). 데이터는 CS 하강 직후와 falling 에서 바뀐다
```

### 6.2 SPI를 쓸 때 체크할 것

- 데이터시트의 모드와 최대 SCK, **CS setup/hold 시간**(CS 하강 후 첫 엣지까지 최소 시간)을 확인한다.
- 많은 센서는 "첫 바이트 = 레지스터 주소 + R/W 비트(MSB가 1이면 read)" 규칙을 쓴다(칩마다 다름). read 때는 주소를 보낸 뒤 dummy 바이트를 보내야 데이터가 돌아온다. full-duplex라서 **읽으려면 반드시 무언가를 써야 한다**.
- 고속(수십 MHz)에서는 MISO 경로 지연(보드 트레이스 + 주변장치 출력 지연)이 반 주기를 넘으면 잘못된 비트를 샘플한다. 일부 컨트롤러는 샘플 지연 설정이 있다(벤더마다 다름).
- NOR flash는 Dual/Quad SPI(QSPI)로 데이터 선을 2/4개 쓴다. XIP(execute in place)로 코드를 flash에서 바로 실행하기도 한다(C08의 모델 가중치 배치와 연결).

### 6.3 Zephyr SPI: 레지스터 읽기

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>

/* devicetree: &spi1 아래 imu@0 { compatible = "..."; reg = <0>; spi-max-frequency = <8000000>; }; */
#define IMU_NODE DT_NODELABEL(imu)

static const struct spi_dt_spec imu =
    SPI_DT_SPEC_GET(IMU_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);

int imu_read_reg(uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80u), 0x00u };  /* MSB=1: read (이 IMU 의 규칙) */
    uint8_t rx[2] = { 0 };
    const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
    const struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive_dt(&imu, &tx_set, &rx_set);
    if (ret == 0) {
        *val = rx[1];               /* rx[0] 은 주소를 보내는 동안 들어온 쓰레기 */
    }
    return ret;
}
```

- `SPI_DT_SPEC_GET`는 버스 장치, CS GPIO, 최대 주파수를 devicetree에서 가져온다. 마지막 인자는 CS 해제 지연(µs)이다. 최신 Zephyr에서는 이 인자가 없어지거나 devicetree 속성으로 옮겨지는 중이니 사용하는 버전의 헤더를 확인한다.
- 모드(CPOL/CPHA)는 `SPI_MODE_CPOL`, `SPI_MODE_CPHA` 플래그를 operation에 OR해서 지정한다. 위 코드는 mode 0이다.
- `spi_buf_set`은 scatter-gather 구조다. 헤더와 페이로드 버퍼를 따로 넘길 수 있다.

---

## 7. I2C

### 7.1 전기적 구조: open-drain + pull-up

```
  VDD ----+---------+
          |         |
         [Rp]      [Rp]            Rp: pull-up (보통 1k~10k)
          |         |
  SDA ----+----+----+-----+-----
  SCL ----+----|----+-----|--+--
          |    |          |  |
       [MCU]  [PMIC 0x6B] [Fuel gauge 0x55]  ... (모두 Low 만 당길 수 있음)
```

- 모든 장치가 **Low만 구동**한다. 아무도 당기지 않으면 pull-up이 High로 올린다. 그래서 "wired-AND"다. 누군가 Low면 선은 Low다.
- 이 구조 덕분에 clock stretching(주변장치가 SCL을 Low로 붙잡아 컨트롤러를 기다리게 함)과 multi-controller arbitration이 가능하다.
- 상승 시간은 Rp와 버스 용량 Cb의 RC로 정해진다. 하강은 트랜지스터가 빠르게 당긴다. 그래서 스코프에서 I2C 파형은 **느린 상승, 빠른 하강**의 톱니 모양이다.

### 7.2 속도 모드와 pull-up 계산 (NXP UM10204 기준)

| 모드 | 최대 속도 | 최대 상승 시간 tr | 최대 버스 용량 Cb |
|---|---|---|---|
| Standard-mode | 100 kbit/s | 1000 ns | 400 pF |
| Fast-mode | 400 kbit/s | 300 ns | 400 pF |
| Fast-mode Plus | 1 Mbit/s | 120 ns | 550 pF |
| High-speed mode | 3.4 Mbit/s | (별도 규정) | (별도 규정) |
| Ultra Fast-mode | 5 Mbit/s (단방향, push-pull) | - | - |

- `Rp(min) = (VDD − VOL(max)) / IOL`. 3.3V, VOL 0.4V, IOL 3mA면 약 967Ω. 이보다 작으면 장치가 Low를 충분히 못 당긴다.
- `Rp(max) = tr / (0.8473 × Cb)`. Fast-mode(300ns), Cb 100pF면 약 3.5kΩ. 이보다 크면 상승이 느려 규격을 넘는다.
- 1.8V 기기에서 3.3V 센서를 붙이면 level shifter가 필요하다. MOSFET 기반 양방향 shifter는 open-drain이라 I2C와 잘 맞는다.

### 7.3 프로토콜 파형: 레지스터 읽기 (repeated start)

```
 START: SCL High 동안 SDA High->Low     STOP: SCL High 동안 SDA Low->High
 데이터: SCL Low 동안만 SDA 변경, SCL High 동안 안정

  S | addr(7) | W | A | reg(8) | A | Sr | addr(7) | R | A | data(8) | N | P
  컨트롤러 ---------->  <-주변  ------>  <-  ------------>  <-  <---주변--  컨트롤러->
  S=START  Sr=repeated START  A=ACK(SDA Low)  N=NACK(SDA High)  P=STOP

  SDA ‾‾\___X a6 X a5 X ... X a0 X W=0 X  ACK  X ...
  SCL ‾‾‾‾\__/‾\__/‾\__ ... __/‾\__/‾\__/‾‾‾\__ ...
         START          9번째 클럭 = ACK 슬롯
```

- 8비트마다 **9번째 클럭**에 수신 측이 ACK(Low)를 준다.
- 읽기 마지막 바이트에서 컨트롤러가 **NACK**을 주는 것은 오류가 아니라 "이제 그만 보내라"는 정상 신호다.
- repeated START(Sr)를 쓰는 이유: STOP을 넣으면 다른 컨트롤러가 버스를 가져가거나 일부 장치가 레지스터 포인터를 리셋할 수 있다.
- 7비트 주소 0x68을 쓰면 첫 바이트는 write 때 0xD0, read 때 0xD1이다. 데이터시트가 8비트 표기(0xD0)로 적어 둔 경우가 있어 주소 착오의 단골 원인이다.

### 7.4 버스가 멈추는 이유와 복구 (bus clear)

컨트롤러가 전송 도중 reset되면(watchdog, 디버거 halt, 전원 글리치) 주변장치는 아직 바이트 중간이라 생각하고 SDA를 Low로 잡고 있을 수 있다. 컨트롤러는 SDA가 Low라서 START를 못 만든다. 이것이 **I2C bus hang**이다.

UM10204의 "bus clear" 절차: SDA가 Low로 붙잡혀 있으면 컨트롤러가 SCL을 최대 9번 토글한다. 주변장치가 남은 비트를 다 내보내고 ACK 슬롯에서 SDA를 놓으면 SDA가 High가 된다. 그다음 STOP을 만든다. 그래도 안 되면 주변장치 reset 핀이나 전원 재인가(power cycle)가 필요하다.

```c
#include <stdint.h>
#include <stdbool.h>

/* BSP 가 제공한다고 가정하는 GPIO 함수 (open-drain 으로 설정된 핀) */
extern void i2c_pins_to_gpio(void);        /* 핀 mux 를 I2C -> GPIO(open-drain) */
extern void i2c_pins_to_periph(void);      /* 다시 I2C 기능으로 */
extern void scl_write(bool high);          /* high=release(pull-up), low=drive low */
extern void sda_write(bool high);
extern bool sda_read(void);
extern bool scl_read(void);
extern void delay_us(uint32_t us);

/* 100kHz 기준 반 주기 5us */
#define HALF_PERIOD_US 5u

int i2c_bus_clear(void)
{
    i2c_pins_to_gpio();
    sda_write(true);                            /* SDA 를 놓는다 */
    for (int i = 0; i < 9 && !sda_read(); i++) {
        scl_write(false);
        delay_us(HALF_PERIOD_US);
        scl_write(true);
        uint32_t guard = 1000u;                 /* clock stretching 대기, 무한루프 방지 */
        while (!scl_read() && guard--) {
            delay_us(1);
        }
        delay_us(HALF_PERIOD_US);
    }
    if (!sda_read()) {
        i2c_pins_to_periph();
        return -1;                              /* 복구 실패: 주변장치 reset/power cycle 필요 */
    }
    /* STOP 생성: SCL High 동안 SDA Low -> High */
    sda_write(false);
    delay_us(HALF_PERIOD_US);
    scl_write(true);
    delay_us(HALF_PERIOD_US);
    sda_write(true);
    delay_us(HALF_PERIOD_US);
    i2c_pins_to_periph();
    return 0;
}
```

- 9번인 이유: 주변장치가 최악의 경우 8비트 데이터 + ACK 슬롯 중간에 있을 수 있기 때문이다.
- 이 함수는 **부팅 시 I2C 초기화 전에 한 번**, 그리고 드라이버가 타임아웃을 감지했을 때 부른다.
- Zephyr에는 `i2c_recover_bus(dev)` API가 있다. 드라이버가 구현한 경우에만 동작한다(nRF TWIM 드라이버는 구현함, 벤더마다 다름).

### 7.5 Zephyr I2C 레지스터 읽기

```c
#include <zephyr/drivers/i2c.h>

/* devicetree: &i2c0 { fuel_gauge: fg@55 { compatible = "..."; reg = <0x55>; }; }; */
static const struct i2c_dt_spec fg = I2C_DT_SPEC_GET(DT_NODELABEL(fuel_gauge));

int fg_read_u16(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    int ret = i2c_write_read_dt(&fg, &reg, 1, buf, sizeof(buf));  /* S W reg Sr R d0 d1 P */
    if (ret < 0) {
        return ret;                      /* -EIO: NACK 등 */
    }
    *out = (uint16_t)(buf[0] | (buf[1] << 8));   /* 이 칩은 little-endian (칩마다 다름) */
    return 0;
}
```

단일 바이트는 `i2c_reg_read_byte_dt()`, `i2c_reg_write_byte_dt()`, 비트 필드 변경은 `i2c_reg_update_byte_dt()`를 쓴다.

### 7.6 I2C vs SPI vs UART vs SPMI/RFFE

| 항목 | UART | SPI | I2C | SPMI / RFFE (MIPI) |
|---|---|---|---|---|
| 선 수 | 2 (+RTS/CTS) | 4 + 장치당 CS | 2 | 2 (SCLK, SDATA) |
| 클럭 | 없음 (비동기) | 컨트롤러가 공급 | 컨트롤러 (stretching 가능) | 컨트롤러 |
| 이중성 | full-duplex | full-duplex | half-duplex | half-duplex |
| 주소 | 없음 (점대점) | CS 선 | 7/10비트 주소 | USID 등 슬레이브 ID |
| 구동 | push-pull | push-pull | open-drain | push-pull (bus keeper) |
| 전형적 속도 | 115.2k ~ 수 Mbaud | 수~수십 MHz | 100k ~ 1M | RFFE 최대 수십 MHz급, SPMI 최대 26MHz급 |
| ACK | 없음 | 없음 | 바이트마다 | parity/특정 명령에 ACK |
| 쓰는 곳 | 콘솔, HCI, GNSS | flash, IMU, 디스플레이 | PMIC, 센서, 코덱 제어 | PMIC 제어(SPMI), RF front-end 제어(RFFE) |

> **Don 경험과 연결**: Apple에서 본 SPMI/RFFE는 "I2C를 모바일 PMIC·RF용으로 push-pull로 빠르게 만든 친척"이라고 설명하면 된다. 면접에서 "I2C 장애를 어떻게 좁히나?"에는 RFFE/SPMI root cause 경험(파형 캡처 → ACK/parity 확인 → 전원·타이밍)을 그대로 쓴다.

---

## 8. I2S / PDM / TDM — 오디오 인터페이스

Don의 갭이 가장 큰 부분이라 기초부터 쌓는다.

### 8.1 디지털 오디오 기초

- **PCM(Pulse Code Modulation)**: 소리를 일정 간격(sample rate, fs)으로 재고 각 샘플을 정수(bit depth)로 저장한 것. 16kHz/16-bit는 음성(wake word, ASR)의 표준 입력이고, 48kHz/24-bit는 음악·통화 고품질이다.
- **Nyquist**: fs의 절반까지의 주파수만 표현할 수 있다. 16kHz면 8kHz까지. 사람 음성의 핵심 대역(약 300Hz~4kHz)은 충분히 들어간다.
- **데이터율**: fs × bit depth × 채널. 16k × 16 × 1 = 256kbit/s = 32KB/s. 1초 링버퍼는 32KB SRAM이다. MCU에서 SRAM 예산을 짤 때 이 숫자를 먼저 계산한다(C08).
- **frame**: I2S에서 한 샘플 시점의 모든 채널을 합친 단위(L+R). DMA 블록은 보통 "10ms = 160 frame(16kHz)"처럼 시간으로 정한다.

### 8.2 I2S 신호와 파형 (Philips I2S 규격)

| 신호 | 다른 이름 | 의미 |
|---|---|---|
| SCK | BCLK, bit clock | 비트마다 한 번. BCLK = fs × slot 비트 수 × 채널 수 |
| WS | LRCLK, FS, word select | 어떤 채널인지. **Low = Left, High = Right**. 주파수 = fs |
| SD | SDATA, DIN/DOUT | 데이터. 2의 보수, **MSB first** |
| MCLK | master clock, SYSCLK | 코덱 내부 ADC/DAC(delta-sigma) 동작용. 보통 256 × fs (선택적 신호) |

```
 I2S (Philips): 32-bit slot, 24-bit data, 16kHz 기준 BCLK = 16k x 32 x 2 = 1.024MHz

 WS    ‾‾\______________________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
          |<------------- Left slot 32 BCLK ---->|<------- Right slot ------
 BCLK  _/‾\_/‾\_/‾\_/‾\_/‾\_ ... _/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_ ...
 SD    ... X R0 X L23 X L22 X ... X L0 X 0 X...X 0 X R23 X R22 ...
             ^
             WS 가 바뀐 뒤 1 BCLK 늦게 MSB 가 나온다 (이것이 I2S 의 특징)
             24비트 데이터 뒤 남은 8비트는 0 (padding)
 수신기는 BCLK rising edge 에서 샘플 (송신기는 보통 falling edge 에서 데이터 변경)
```

### 8.3 포맷 비교: I2S, Left-justified, TDM, PDM

| 포맷 | 선 | 프레임 표시 | MSB 위치 | 채널 수 | 특징 |
|---|---|---|---|---|---|
| I2S (Philips) | BCLK, WS, SD | WS 50% duty, Low=L | WS 변화 후 1 BCLK 지연 | 2 | 가장 흔한 코덱 인터페이스 |
| Left-justified | BCLK, WS, SD | WS 50% duty (극성은 칩마다 다름) | WS 변화와 같은 BCLK | 2 | 지연 없음. 극성 확인 필수 |
| Right-justified | BCLK, WS, SD | WS 50% duty | LSB가 slot 끝에 맞춤 | 2 | 데이터 길이를 양쪽이 알아야 함 |
| TDM (DSP mode) | BCLK, FS, SD | FS가 짧은 펄스(1 BCLK) 또는 slot 폭 | 칩마다 다름 | 4, 8, 16 | 한 선에 여러 마이크/채널. BCLK = fs × slot 비트 × slot 수 |
| PDM | CLK, DATA | 없음 | 해당 없음 (1-bit 스트림) | 선 하나에 2 (엣지로 구분) | MEMS 마이크 표준. decimation 필요 |

TDM 예: 8 slot × 32비트 × 48kHz = BCLK 12.288MHz. 마이크 어레이(빔포밍용 4~8개)나 다채널 코덱에 쓴다.

### 8.4 PDM 마이크와 decimation

디지털 MEMS 마이크 대부분은 내부에 sigma-delta modulator가 있고, 결과를 **1비트 PDM(Pulse Density Modulation)** 스트림으로 내보낸다. 소리가 클수록(양의 방향) 1이 빽빽하다.

```
 CLK   _/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_     (예: 1.024MHz ~ 3.072MHz, 마이크 규격 참조)
 DATA  L R L R L R L R ...                    SEL(L/R) 핀으로 어느 엣지에 출력할지 선택
        ^ 마이크 A: 한 엣지에서 출력, 다른 엣지에서 hi-Z
          ^ 마이크 B: 반대 엣지에서 출력
 -> 한 쌍의 선(CLK, DATA)으로 스테레오 2 마이크
```

PDM을 PCM으로 바꾸려면 **decimation filter**(보통 CIC + 보정 FIR + 저역 통과)를 거쳐 샘플레이트를 낮춘다. decimation 비율 64라면 1.024MHz → 16kHz, 3.072MHz → 48kHz다. 이 필터를 누가 하나가 설계 포인트다.

- MCU에 PDM 주변장치가 있으면 하드웨어가 한다(nRF52840의 PDM, STM32의 DFSDM/MDF, Ambiq Apollo의 PDM 등). CPU 부하 0에 가깝다.
- 없으면 I2S/SPI로 1비트 스트림을 받아 CPU가 필터를 돌린다. 구현은 쉽지만 전력·CPU 부하가 크다.
- 마이크의 PDM 클럭을 낮추면(low-power 모드, 마이크 규격에 따라 다름) 마이크 전류가 줄어든다. always-on wake word에서는 이것이 큰 절약이다(C05).

### 8.5 클럭 master는 누구인가

I2S에서 BCLK/WS를 만드는 쪽이 **clock master**(요즘 용어로 controller)다.

- MCU가 master: MCU의 audio PLL 품질이 음질을 결정한다. 정확한 fs가 안 나오면(분주 한계) 조금 틀린 fs로 돈다. 음성 인식에는 보통 문제없지만 다른 장치와 합칠 때 drift가 쌓인다.
- 코덱이 master: 코덱이 자기 크리스털/PLL로 정확한 fs를 만든다. MCU는 slave로 받는다.
- 두 클럭 도메인이 만나면(예: 마이크 16kHz를 MCU 클럭으로, 스피커를 SoC 클럭으로) **ASRC(asynchronous sample rate conversion)**나 버퍼 수위 기반 보정이 필요하다.
- jitter(클럭 엣지 흔들림)는 ADC/DAC 성능을 떨어뜨린다. 그래서 MCLK는 깨끗한 소스에서 만들고 배선을 짧게 한다.

### 8.6 Zephyr I2S 수신 코드

Zephyr I2S API는 **memory slab 기반 블록 큐**다. 드라이버가 slab에서 블록을 할당해 DMA로 채우고, 애플리케이션이 `i2s_read()`로 받아 처리한 뒤 반납한다.

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>

#define SAMPLE_RATE    16000
#define CHANNELS       2
#define WORD_SIZE      16
#define FRAMES_PER_BLK 160                                   /* 10 ms */
#define BLOCK_SIZE     (FRAMES_PER_BLK * CHANNELS * sizeof(int16_t))  /* 640 B */
#define BLOCK_COUNT    4

K_MEM_SLAB_DEFINE_STATIC(rx_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s0));

extern void audio_frontend_process(const int16_t *pcm, size_t frames);  /* 앱이 구현 */

int main(void)
{
    struct i2s_config cfg = {
        .word_size      = WORD_SIZE,
        .channels       = CHANNELS,
        .format         = I2S_FMT_DATA_FORMAT_I2S,
        .options        = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
        .frame_clk_freq = SAMPLE_RATE,
        .mem_slab       = &rx_slab,
        .block_size     = BLOCK_SIZE,
        .timeout        = 100,                             /* ms */
    };

    if (!device_is_ready(i2s_dev)) {
        return -ENODEV;
    }
    if (i2s_configure(i2s_dev, I2S_DIR_RX, &cfg) < 0) {
        return -EIO;
    }
    if (i2s_trigger(i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START) < 0) {
        return -EIO;
    }

    while (1) {
        void *blk;
        size_t size;
        int ret = i2s_read(i2s_dev, &blk, &size);          /* 블록이 찰 때까지 block */
        if (ret < 0) {
            /* -EIO: overrun 등. DROP 후 재시작 */
            i2s_trigger(i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
            i2s_trigger(i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
            continue;
        }
        audio_frontend_process((const int16_t *)blk, size / (CHANNELS * sizeof(int16_t)));
        k_mem_slab_free(&rx_slab, blk);                    /* 반드시 반납 */
    }
    return 0;
}
```

- `K_MEM_SLAB_DEFINE_STATIC`: 640바이트 블록 4개짜리 고정 풀. malloc이 없다. 블록 수가 곧 **허용 가능한 처리 지연**이다(4블록 = 40ms).
- `I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER`: MCU가 BCLK와 WS를 만든다. 코덱이 master면 `I2S_OPT_BIT_CLK_SLAVE | I2S_OPT_FRAME_CLK_SLAVE`를 쓴다(버전에 따라 CONTROLLER/TARGET 이름이 추가될 수 있음).
- `k_mem_slab_free()`를 빼먹으면 4블록 뒤 slab이 바닥나 드라이버가 overrun을 낸다. 가장 흔한 버그다. (Zephyr 3.5 이전에는 두 번째 인자가 `void **`였다.)
- nRF52840의 I2S는 MCK 분주로 fs를 만들기 때문에 16kHz가 정확히 안 나오고 근사값이 된다(데이터시트 표 참조).
- PDM 마이크는 Zephyr의 별도 API인 `dmic_configure()`, `dmic_trigger()`, `dmic_read()`(`<zephyr/audio/dmic.h>`)로 같은 slab 패턴을 쓴다. 예제는 `samples/drivers/audio/dmic`.

---

## 9. DMA ping-pong (double buffering)

### 9.1 왜 필요한가

오디오는 **멈추지 않는 스트림**이다. DMA가 버퍼 하나를 다 채운 순간 CPU가 그 버퍼를 처리하는 동안에도 다음 샘플이 들어온다. 그래서 버퍼를 둘로 나눠 DMA가 한쪽을 채우는 동안 CPU가 다른 쪽을 처리한다.

```
 DMA circular buffer  [ half A (160 frame) | half B (160 frame) ]

 시간 --->   0ms        10ms        20ms        30ms        40ms
 DMA 쓰기   [== A ==]  [== B ==]  [== A ==]  [== B ==]  [== A ==]
 HT IRQ               ^ (A 가 참)             ^
 TC IRQ                          ^ (B 가 참)             ^
 CPU 처리             [proc A]   [proc B]    [proc A]   [proc B]
                      |<- 10ms 안에 끝나야 한다 (deadline) ->|
```

- HT = half-transfer 인터럽트, TC = transfer-complete 인터럽트. circular 모드 DMA는 TC 후 자동으로 처음부터 다시 쓴다.
- **deadline**: 처리는 반 버퍼 시간(여기서는 10ms) 안에 끝나야 한다. 넘으면 DMA가 아직 처리 중인 반쪽을 덮어쓴다(overrun). 소리가 뚝뚝 끊기거나 모델 입력이 깨진다.
- 처리 시간이 들쭉날쭉하면(ML 추론) 반 버퍼 두 개가 아니라 **N개 블록 큐**(Zephyr slab처럼)로 여유를 둔다. 대가는 레이턴시와 SRAM이다.

### 9.2 STM32 HAL 예: ISR에서 task로 넘기기

```c
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define FRAMES_PER_HALF 160
#define CHANNELS        2
static int16_t i2s_dma_buf[2 * FRAMES_PER_HALF * CHANNELS];   /* ping + pong */

extern I2S_HandleTypeDef hi2s2;       /* CubeMX 가 생성 */
static TaskHandle_t audio_task_handle;

#define EVT_HALF_A (1u << 0)
#define EVT_HALF_B (1u << 1)

void audio_start(TaskHandle_t task)
{
    audio_task_handle = task;
    /* Size 단위는 데이터 포맷에 따라 다르다(16-bit 는 half-word 개수). RM/HAL 주석 확인 */
    HAL_I2S_Receive_DMA(&hi2s2, (uint16_t *)i2s_dma_buf, (uint16_t)(sizeof(i2s_dma_buf) / 2));
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    BaseType_t woken = pdFALSE;
    if (hi2s == &hi2s2) {
        xTaskNotifyFromISR(audio_task_handle, EVT_HALF_A, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    BaseType_t woken = pdFALSE;
    if (hi2s == &hi2s2) {
        xTaskNotifyFromISR(audio_task_handle, EVT_HALF_B, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

extern void audio_frontend_process(const int16_t *pcm, size_t frames);

void audio_task(void *arg)
{
    (void)arg;
    audio_start(xTaskGetCurrentTaskHandle());
    for (;;) {
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);
        if ((events & EVT_HALF_A) && (events & EVT_HALF_B)) {
            /* 두 반쪽이 동시에 쌓였다 = 처리가 늦었다. 카운트하고 최신만 처리 */
        }
        if (events & EVT_HALF_A) {
            audio_frontend_process(&i2s_dma_buf[0], FRAMES_PER_HALF);
        }
        if (events & EVT_HALF_B) {
            audio_frontend_process(&i2s_dma_buf[FRAMES_PER_HALF * CHANNELS], FRAMES_PER_HALF);
        }
    }
}
```

- 콜백은 DMA ISR 안에서 불린다. 처리는 하지 않고 **task notification으로 신호만** 보낸다(C04의 deferred interrupt processing).
- `eSetBits`로 반쪽 A/B를 비트로 구분한다. 두 비트가 동시에 켜져 있으면 task가 늦었다는 증거다. 이 카운터를 telemetry로 남기면 필드에서 오디오 끊김을 진단할 수 있다.
- `portYIELD_FROM_ISR(woken)`: 깨운 task가 현재 task보다 우선순위가 높으면 ISR 복귀 직후 바로 그 task로 전환한다.
- D-cache가 있는 STM32F7/H7이라면 각 반쪽 처리 전에 `SCB_InvalidateDCache_by_Addr()`가 필요하다(4.2절).

> **Don 경험과 연결**: SSD의 NAND 채널 데이터 경로에서 버퍼 두 개를 번갈아 쓰던 것과 원리가 같다. 차이는 오디오는 **실시간 deadline이 물리적으로 고정**(샘플레이트)되어 있어 늦으면 데이터가 그냥 사라진다는 점이다.

---

## 10. 드라이버 계층: vendor HAL, CMSIS, Zephyr device model

### 10.1 선택지 비교

| 층 | 예 | 장점 | 단점 |
|---|---|---|---|
| 레지스터 직접 | CMSIS device header(`NRF_TWIM0->ADDRESS`) | 최소 오버헤드, 모든 기능 | 이식성 0, 실수 쉬움 |
| Vendor HAL/LL | STM32 HAL/LL, nrfx, Ambiq HAL | 칩 기능 대부분 지원, 벤더 예제 | 벤더 종속, HAL마다 스타일 다름 |
| CMSIS-Driver | `ARM_DRIVER_I2C`, `ARM_DRIVER_SPI` | Arm 표준 인터페이스 | 채택 벤더 제한적 |
| RTOS driver model | Zephyr `i2c_transfer()`, `spi_transceive()` | 보드·칩 바꿔도 앱 코드 유지, devicetree 연동, PM 통합 | 추상화 비용, 벤더 특수 기능 접근이 불편 |

실제 Zephyr의 nRF 드라이버는 nrfx(Nordic HAL) 위에 Zephyr API를 얹은 구조다. 즉 층이 쌓인다.

### 10.2 Zephyr device model 핵심

- 모든 드라이버 인스턴스는 `struct device`다. 필드: `name`, `config`(ROM, 읽기 전용: 버스 spec, 핀, 주소), `data`(RAM, 런타임 상태), `api`(함수 포인터 테이블).
- 인스턴스는 빌드 타임에 devicetree로부터 **정적으로** 만들어진다. 런타임 등록·malloc이 없다.
- 초기화는 레벨(`PRE_KERNEL_1`, `PRE_KERNEL_2`, `POST_KERNEL`, `APPLICATION`)과 레벨 안 우선순위 숫자로 순서가 정해진다. I2C 버스 드라이버가 센서 드라이버보다 먼저 초기화되어야 한다.
- 앱은 `DEVICE_DT_GET(node_id)`로 포인터를 얻고 `device_is_ready()`로 확인한다. `device_get_binding("name")`은 런타임 문자열 검색이라 새 코드에서는 쓰지 않는다.

### 10.3 Devicetree와 binding

devicetree는 "하드웨어 설명서"다. 코드가 아니라 데이터다. 빌드 때 C 매크로(`devicetree_generated.h`)로 변환된다.

```
/* app.overlay 또는 boards/nrf52840dk_nrf52840.overlay */
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;        /* 400 kHz */
    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";

    temp_sensor: temp@48 {
        compatible = "acme,tmp100x";               /* binding 과 연결되는 키 */
        reg = <0x48>;                              /* I2C 7비트 주소 */
        int-gpios = <&gpio0 11 GPIO_ACTIVE_LOW>;
    };
};
```

binding(YAML)은 이 노드에 어떤 속성이 필요한지 정의한다.

```
# dts/bindings/sensor/acme,tmp100x.yaml
description: ACME TMP100X I2C temperature sensor
compatible: "acme,tmp100x"
include: i2c-device.yaml
properties:
  int-gpios:
    type: phandle-array
    description: Alert/interrupt pin
```

Kconfig는 "빌드에 무엇을 넣나"를 정한다. devicetree = 하드웨어가 무엇인가, Kconfig = 소프트웨어 기능을 켜나. 이 구분을 면접에서 한 문장으로 말할 수 있어야 한다.

```
# prj.conf
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_ACME_TMP100X=y
CONFIG_LOG=y
```

---

## 11. 드라이버 작성 예: Zephyr I2C 온도 센서 드라이버

가상의 센서 "ACME TMP100X": 레지스터 0x00이 12비트 온도(left-justified, big-endian, 0.0625°C/LSB)라고 하자. TI TMP102와 비슷한 형태다.

```c
#define DT_DRV_COMPAT acme_tmp100x

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(acme_tmp100x, CONFIG_SENSOR_LOG_LEVEL);

#define REG_TEMP 0x00

struct tmp_config {
    struct i2c_dt_spec i2c;          /* ROM: 버스 + 주소 */
};

struct tmp_data {
    int16_t raw;                     /* RAM: 마지막 측정값 */
};

static int tmp_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    const struct tmp_config *cfg = dev->config;
    struct tmp_data *data = dev->data;
    uint8_t reg = REG_TEMP;
    uint8_t buf[2];

    if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }
    int ret = i2c_write_read_dt(&cfg->i2c, &reg, 1, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("read failed (%d)", ret);
        return ret;
    }
    data->raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]) / 16;   /* 12비트로, 부호 유지 */
    return 0;
}

static int tmp_channel_get(const struct device *dev, enum sensor_channel chan,
                           struct sensor_value *val)
{
    const struct tmp_data *data = dev->data;

    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }
    int32_t micro_c = (int32_t)data->raw * 62500;    /* 0.0625 C = 62500 uC */
    val->val1 = micro_c / 1000000;
    val->val2 = micro_c % 1000000;
    return 0;
}

static const struct sensor_driver_api tmp_api = {
    .sample_fetch = tmp_sample_fetch,
    .channel_get  = tmp_channel_get,
};

static int tmp_init(const struct device *dev)
{
    const struct tmp_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }
    return 0;
}

#define TMP_DEFINE(inst)                                                  \
    static struct tmp_data tmp_data_##inst;                               \
    static const struct tmp_config tmp_cfg_##inst = {                     \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                \
    };                                                                    \
    SENSOR_DEVICE_DT_INST_DEFINE(inst, tmp_init, NULL,                    \
                                 &tmp_data_##inst, &tmp_cfg_##inst,       \
                                 POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
                                 &tmp_api);

DT_INST_FOREACH_STATUS_OKAY(TMP_DEFINE)
```

한 줄씩 핵심:

- `DT_DRV_COMPAT acme_tmp100x`: compatible 문자열 `"acme,tmp100x"`를 매크로 이름 규칙(쉼표·하이픈 → 밑줄)으로 쓴 것. 이후 `DT_INST_...` 매크로가 이 compatible의 인스턴스를 가리킨다.
- `config`는 `const`로 flash에, `data`는 RAM에 둔다. 인스턴스가 여러 개(센서 2개)여도 코드 하나로 처리된다.
- 나눗셈 `/ 16`을 쓴 이유: 음수의 오른쪽 시프트는 C에서 implementation-defined이기 때문이다(C02).
- `SENSOR_DEVICE_DT_INST_DEFINE`: `DEVICE_DT_INST_DEFINE`의 센서 버전(센서 subsystem 부가 기능 포함). 인자 순서는 init 함수, PM 장치(NULL), data, config, 초기화 레벨, 우선순위, api.
- `DT_INST_FOREACH_STATUS_OKAY`: devicetree에서 `status = "okay"`인 인스턴스마다 매크로를 펼친다.
- Zephyr 4.1부터는 API 구조체를 `static DEVICE_API(sensor, tmp_api) = { ... };`로 쓰는 것이 권장된다. 이전 버전은 위처럼 `struct sensor_driver_api`.
- 앱 쪽은 `sensor_sample_fetch(dev)` → `sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &val)`만 알면 된다. 센서를 바꿔도 앱 코드가 그대로다.

out-of-tree 드라이버는 Zephyr module(`zephyr/module.yml`, `CMakeLists.txt`, `Kconfig`, `dts/bindings/`)로 묶어 west manifest에 추가한다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 주변장치 클럭 enable 누락 | 레지스터 쓰기가 무시됨, 읽으면 0 | RCC/CLOCK 게이트 꺼짐 | 초기화 첫 줄에 클럭 enable, 디버거로 레지스터 확인 |
| I2C 7비트/8비트 주소 혼동 | 모든 전송 NACK | 데이터시트의 0xD0을 그대로 7비트 자리에 씀 | 7비트 = 8비트 >> 1. LA로 첫 바이트 확인 |
| I2C pull-up 과대 또는 누락 | 400kHz에서 간헐 오류, 스코프에 둥근 상승 | RC 상승 시간 초과 | Rp 재계산, 속도 낮추기, 버스 용량 줄이기 |
| 리셋 후 I2C hang | 부팅 후 센서가 영영 응답 없음, SDA Low 고정 | 전송 중 컨트롤러 reset | 부팅 시 9-clock bus clear, 타임아웃 + `i2c_recover_bus()` |
| SPI 모드 불일치 | 값이 1비트 밀리거나 가끔 맞음 | CPOL/CPHA 틀림 | 데이터시트 타이밍도 확인, LA 디코더를 모드별로 바꿔 보기 |
| I2S 포맷 불일치(I2S vs LJ) | 소리가 매우 작거나 왜곡, 값이 2배/절반 | 1 BCLK 지연 차이로 1비트 밀림 | 양쪽 포맷 통일, 사인파 입력으로 샘플 값 확인 |
| DMA 버퍼 캐시 미처리 | 가끔 옛 데이터, 디버거에서 보면 정상 | D-cache 일관성 | invalidate/clean 또는 non-cacheable 영역 |
| 오디오 처리 deadline 초과 | 주기적 클릭음, 모델 정확도 저하 | 처리 시간 > 반 버퍼 시간 | 블록 수 증가, 처리 task 우선순위 조정, 처리 최적화 |
| slab/버퍼 반납 누락 | 몇 블록 뒤 I2S overrun, 스트림 정지 | `k_mem_slab_free()` 빠짐 | 모든 경로(에러 포함)에서 반납 |
| 안 쓰는 핀 floating | sleep 전류가 수백 µA 높음 | 입력 버퍼가 중간 전압에서 발진 | disconnect/analog 또는 pull 설정 |

---

## 13. 면접에서 이렇게 말한다

**Q.** What goes into a BSP for a new board?

**A.** BSP는 칩 벤더 코드와 애플리케이션 사이에서 보드 고유 사실을 담는다. startup과 링커 스크립트, 클럭 트리, 핀 mux, 전원 레일 시퀀스, 그리고 어떤 버스에 어떤 칩이 있는지다. Zephyr에서는 이것이 board devicetree, pinctrl, defconfig로 정리된다. bring-up은 SWD → GPIO → UART → 크리스털/PLL → I2C/SPI ID 읽기 → DMA/오디오 → 저전력 순으로 한다.

> "A BSP captures everything that's specific to the board rather than the chip: startup and linker script, clock tree, pin muxing, power-rail sequencing, and which devices sit on which bus. In Zephyr that's the board devicetree, pinctrl and defconfig. I bring it up in order of fewest assumptions: SWD, GPIO, UART console, then crystals and PLL, then read ID registers over I2C and SPI, then DMA, audio, and finally low-power modes."

**Q.** An I2C sensor stops responding after a watchdog reset. What happened and how do you fix it?

**A.** 전송 도중 MCU만 reset되어 센서가 SDA를 Low로 잡고 있는 bus hang일 가능성이 크다. 확인은 스코프로 SDA 레벨을 보면 된다. 복구는 핀을 GPIO로 바꿔 SCL을 최대 9번 토글하고 STOP을 만든 뒤 I2C로 되돌린다. 부팅마다 한 번 하고, 드라이버 타임아웃 때도 호출한다. 안 되면 센서 전원이나 reset 핀을 제어한다.

> "Most likely the MCU reset mid-transfer and the sensor is still holding SDA low, waiting to finish its byte. I'd confirm SDA stuck low on a scope, then do a bus clear: switch the pins to GPIO, clock SCL up to nine times until SDA releases, generate a STOP, and hand the pins back to the I2C peripheral. I run that at every boot and on any transfer timeout, and if it still fails I power-cycle the sensor through its rail or reset pin."

**Q.** Explain I2S. How do you compute BCLK?

**A.** I2S는 BCLK, WS(LRCLK), SD 세 선의 동기 직렬 오디오 버스다. WS가 Low면 left, High면 right이고, MSB는 WS가 바뀐 뒤 한 BCLK 늦게 나온다. BCLK는 샘플레이트 × slot 비트 × 채널 수다. 48kHz, 32비트 slot, 스테레오면 3.072MHz다. 코덱이 필요로 하면 보통 256 × fs의 MCLK도 준다.

> "I2S carries PCM audio on three wires: bit clock, word select, and data. Word select low is the left channel, high is the right, and the MSB comes one bit clock after the word-select edge. BCLK equals sample rate times slot width times channels, so 48 kHz, 32-bit slots, stereo is 3.072 MHz. Codecs often also want an MCLK at 256 times fs."

**Q.** How would you capture microphone audio continuously for a wake-word model without dropping samples?

**A.** PDM 마이크를 하드웨어 PDM 주변장치로 받아 decimation을 하드웨어에 맡기고, circular DMA로 10ms 블록을 채운다. half/complete 인터럽트에서는 task notification만 보내고 처리는 task에서 한다. 처리 시간이 들쭉날쭉한 추론이 있으므로 블록 여러 개 큐로 여유를 두고, overrun 카운터를 telemetry로 남긴다. 버퍼 크기는 레이턴시와 SRAM의 trade-off다.

> "I'd use the hardware PDM peripheral so decimation costs no CPU, and run circular DMA into 10-millisecond blocks. The half- and full-transfer interrupts only notify the audio task; all processing happens at task level. Because inference time varies, I'd queue a few blocks rather than a strict ping-pong, and I'd count overruns so we can see drops in the field. Block count is the trade-off between latency, SRAM and robustness."

**Q.** Polling, interrupts, or DMA — how do you choose?

**A.** 데이터율과 전력으로 정한다. 부트 초기나 아주 짧은 레지스터 접근은 polling, 드문 저속 이벤트는 인터럽트, 연속 스트림이나 큰 블록은 DMA다. 배터리 기기에서는 CPU가 깨어나는 횟수가 전력이라서 DMA와 idle-line 같은 하드웨어 기능으로 wake 횟수를 줄이는 쪽을 택한다.

> "It's about data rate and power. Polling for early boot or a few register accesses, interrupts for low-rate sporadic events, and DMA for streams and large blocks. On a battery device every CPU wake-up costs energy, so I lean on DMA plus features like UART idle-line detection to cut the number of wake-ups."

**Q.** What's the difference between devicetree and Kconfig in Zephyr?

**A.** devicetree는 하드웨어가 무엇인지(어떤 버스에 어떤 칩, 핀, 주소)를 설명하고, Kconfig는 빌드에 어떤 소프트웨어 기능을 넣을지 정한다. 둘 다 빌드 타임에 헤더로 변환되어 런타임 비용이 없다. 드라이버는 devicetree compatible로 인스턴스를 만들고 Kconfig로 코드 포함 여부를 정한다.

> "Devicetree describes the hardware — which chips are on which bus, at what address, on which pins. Kconfig selects which software features get compiled in. Both are resolved at build time into generated headers, so there's no runtime cost. A driver instantiates from devicetree compatibles and is compiled in based on Kconfig."

---

## 14. 직접 해보기

### 14.1 nRF52840 DK + Zephyr: GPIO와 I2C 스캔

```sh
# Zephyr 환경이 이미 있다고 가정 (west init/update 는 C04 참조)
cd ~/zephyrproject/zephyr
west build -b nrf52840dk/nrf52840 samples/basic/button -p always
west flash
# 시리얼 콘솔
screen /dev/tty.usbmodem* 115200
```

I2C 스캔은 Zephyr shell의 I2C 명령을 쓰면 가장 빠르다. `prj.conf`에 `CONFIG_SHELL=y`, `CONFIG_I2C=y`, `CONFIG_I2C_SHELL=y`를 넣고 빌드한 뒤 쉘에서 `i2c scan i2c@40003000`(노드 이름은 보드마다 다름, `device list`로 확인)을 친다. 센서 보드(예: BME280 breakout)를 붙여서 주소가 뜨는지 본다.

### 14.2 LA로 I2C/SPI 파형 확인

Saleae 같은 로직 분석기(또는 저가 fx2 기반 LA + PulseView/sigrok)로 SCL/SDA를 캡처하고 I2C 디코더를 붙인다. 확인할 것: START, 주소 바이트(7비트 + R/W), ACK 슬롯, repeated START, 마지막 NACK. SPI는 모드를 일부러 틀리게 디코드해서 값이 어떻게 밀리는지 본다.

### 14.3 I2S / PDM 마이크 (nRF52840 DK)

```sh
west build -b nrf52840dk/nrf52840 samples/drivers/audio/dmic -p always
west flash
```

PDM MEMS 마이크 breakout(예: Adafruit PDM MEMS microphone breakout)을 연결한다. 샘플은 `dmic_dev`라는 node label을 찾으므로 overlay에서 PDM 노드(`&pdm0`)에 이 라벨과 pinctrl을 붙여야 한다. 샘플 README와 `boards/` 디렉터리의 기존 overlay를 참고한다. LA로 PDM CLK 주파수를 측정해 decimation 비율과 fs 관계를 직접 계산해 본다.

### 14.4 bus clear 실습

I2C 센서 read 루프 도중 디버거로 MCU를 halt/reset해서 bus hang을 일부러 만든다(항상 재현되지는 않는다). 스코프로 SDA Low 고정을 확인하고 7.4절 함수로 복구되는 것을 LA로 캡처한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| BSP | Board Support Package | 보드 고유의 startup·클럭·핀·전원·장치 설정 묶음 |
| Pinctrl / pin mux | 핀 기능 선택 | 한 물리 핀을 GPIO/UART/I2C 중 무엇으로 쓸지 |
| HFXO / LFXO | 고속/저속 크리스털 발진기 | 정확한 클럭 소스. LFXO 32.768kHz는 sleep 중 RTC |
| PLL | Phase-Locked Loop | 기준 클럭을 곱해 고속 클럭 생성 |
| Open-drain | Low만 구동하는 출력 | I2C, wired-OR 라인 |
| Clock stretching | 주변장치가 SCL을 Low로 붙잡기 | "아직 준비 안 됨" 신호 |
| Repeated START | STOP 없이 다시 START | 레지스터 주소 쓰기 후 읽기로 전환 |
| Bus clear | SCL 9펄스 + STOP | I2C hang 복구 절차 |
| CPOL / CPHA | SPI 클럭 극성/위상 | 4가지 SPI 모드 결정 |
| PCM | Pulse Code Modulation | 샘플레이트 × 비트 깊이로 표현한 오디오 |
| BCLK / LRCLK / MCLK | 비트/채널/마스터 클럭 | I2S 클럭 3종 |
| TDM | Time Division Multiplexing | 한 데이터 선에 여러 채널 slot |
| PDM | Pulse Density Modulation | MEMS 마이크의 1비트 출력 |
| Decimation | 샘플레이트 낮추기 + 필터 | PDM → PCM 변환 (CIC + FIR) |
| ASRC | Asynchronous Sample Rate Converter | 서로 다른 클럭 도메인 오디오 맞추기 |
| Ping-pong / double buffer | 두 버퍼 교대 사용 | DMA가 채우는 동안 CPU가 다른 쪽 처리 |
| HT / TC | Half-Transfer / Transfer-Complete | circular DMA의 두 인터럽트 |
| Devicetree | 하드웨어 기술 데이터 | 빌드 타임에 C 매크로로 변환 |
| Binding | devicetree 스키마 | compatible별 필수 속성 정의(YAML) |
| Kconfig | 빌드 설정 시스템 | 어떤 코드를 넣을지 결정 |
| `struct device` | Zephyr 드라이버 인스턴스 | config(ROM) + data(RAM) + api |

---

## 16. 요약 & 체크리스트

BSP는 칩이 아니라 **보드**에 관한 사실(클럭, 핀, 전원, 장치 배치)을 코드와 설정으로 옮긴 것이다. 주변장치 드라이버는 결국 "어떤 선에서 어떤 파형이 나와야 하는가"를 레지스터로 표현하는 일이라서, 파형(UART 프레임, SPI 모드, I2C START/ACK/STOP, I2S WS와 1 BCLK 지연)을 그릴 수 있으면 대부분의 버그를 LA 한 장으로 좁힐 수 있다. 오디오는 샘플레이트가 정한 **고정 deadline**이 있는 스트림이라 DMA ping-pong(또는 블록 큐)과 ISR→task 신호가 기본 구조다. Zephyr는 이 모든 것을 devicetree(하드웨어), Kconfig(기능), device model(`struct device` + API)로 정리한다.

- [ ] 보드 bring-up 순서 7단계를 이유와 함께 말할 수 있다
- [ ] 클럭 트리를 그리고 오디오 클럭(12.288MHz = 256 × 48k)이 왜 따로 필요한지 설명할 수 있다
- [ ] UART 8N1 프레임, SPI mode 0~3, I2C 레지스터 읽기(Sr 포함) 파형을 화이트보드에 그릴 수 있다
- [ ] I2C pull-up 최소/최대 값을 계산할 수 있다
- [ ] I2C bus clear 절차와 9펄스인 이유를 설명하고 코드로 쓸 수 있다
- [ ] I2S 파형(WS 극성, 1 BCLK 지연)과 BCLK 계산, I2S/LJ/TDM/PDM 차이를 설명할 수 있다
- [ ] PDM decimation 비율로 샘플레이트를 계산할 수 있다
- [ ] DMA ping-pong 타임라인과 deadline, 캐시 처리를 설명할 수 있다
- [ ] devicetree vs Kconfig vs device model의 역할을 한 문장씩 말할 수 있다
- [ ] Zephyr I2C 센서 드라이버의 뼈대(`DT_DRV_COMPAT`, config/data, API, `DEVICE_DT_INST_DEFINE`)를 쓸 수 있다

## 참고 자료

- [NXP UM10204 I2C-bus specification and user manual](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)
- [Philips I2S bus specification (1996, 사본)](https://www.sparkfun.com/datasheets/BreakoutBoards/I2SBUS.pdf)
- [Zephyr Devicetree guide](https://docs.zephyrproject.org/latest/build/dts/index.html)
- [Zephyr Device Driver Model](https://docs.zephyrproject.org/latest/kernel/drivers/index.html)
- [Zephyr I2C API](https://docs.zephyrproject.org/latest/hardware/peripherals/i2c.html)
- [Zephyr SPI API](https://docs.zephyrproject.org/latest/hardware/peripherals/spi.html)
- [Zephyr I2S API](https://docs.zephyrproject.org/latest/hardware/peripherals/audio/i2s.html)
- [Zephyr GPIO API](https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html)
- [Zephyr UART API](https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html)
- [Zephyr Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [Nordic nRF52840 Product Specification](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html)
- [STM32 HAL I2S (STM32CubeF4 repository)](https://github.com/STMicroelectronics/STM32CubeF4)
- [Arm CMSIS-Driver](https://arm-software.github.io/CMSIS_6/latest/Driver/index.html)
- [Arm CMSIS-Core (cache maintenance 함수 포함)](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
