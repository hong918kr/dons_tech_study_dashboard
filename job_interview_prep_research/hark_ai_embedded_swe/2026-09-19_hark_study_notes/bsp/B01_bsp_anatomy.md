# B01. BSP 해부 — reset이 풀리는 순간부터 앱의 첫 줄까지

> **시리즈**: BSP 집중 1/4 · **선행**: C01(Cortex 부트·링커), C03(BSP·주변장치 드라이버), J02(JD 문장 해설) · **JD 근거**: "Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling"
> **Don 상태**: 🟡 절반. 제품 보드의 BSP를 오너로 만들어 본 적은 없다. 그러나 BSP의 아래 절반(코어 부팅, 클럭·메모리·버스 bring-up, 새 실리콘이 처음 깨어날 때의 디버깅)은 FPGA pre-silicon과 Apple 실리콘 통합에서 직접 했다. 이 노트의 목적은 "해봤다"고 말하는 게 아니라 **지도를 전부 그린 뒤 내 좌표를 정확히 찍는 것**이다.
> **이 노트를 다 읽으면**: ① BSP의 경계(무엇이 BSP이고 무엇이 아닌지)를 표로 말할 수 있다 ② reset → 클럭 → 핀 → 메모리 → 디바이스 init → 앱 순서를 화이트보드에 그리고 각 단계가 틀렸을 때의 증상을 말할 수 있다 ③ 보드 리비전이 셋으로 늘었을 때의 관리 설계안을 제시할 수 있다.

---

## 0. 큰 그림

BSP(Board Support Package)는 **"이 실리콘"과 "이 PCB"를 소프트웨어가 일반적인 API로 다룰 수 있게 만드는 층**이다. 위쪽은 이식 가능한 코드고, 아래쪽은 이 보드에만 해당하는 사실이다.

```
 ┌──────────────────────────────────────────────────────────────┐
 │  애플리케이션 (wake word 파이프라인, 상태 머신, BLE 로직)      │  이식 가능
 │  RTOS / 서브시스템 API (k_thread, i2c_transfer, sensor API)   │  이식 가능
 ├───────────────────────── BSP 경계선 ──────────────────────────┤
 │  디바이스 인스턴스 (이 보드에 실제로 달린 것들)                │
 │  클럭·전원 정책, 핀 먹싱 테이블, 메모리 맵·링커 스크립트       │  이 보드 전용
 │  부트 코드(vector table, reset handler, SystemInit)           │
 │  보드 리비전 분기, 보드 자가 테스트, 플래시·디버그 설정        │
 ├──────────────────────────────────────────────────────────────┤
 │  실리콘 (코어, 클럭 컨트롤러, GPIO 매트릭스, 주변장치 IP)      │  고정
 │  PCB (PMIC, 레귤레이터, 크리스털, 센서, 마이크, 풀업 저항)     │  고정
 └──────────────────────────────────────────────────────────────┘
```

BSP 오너가 답해야 하는 질문은 결국 넷이고, 이 노트는 이 순서로 판다. ① 전원이 들어온 뒤 무엇을 어떤 순서로 켜는가(2~3절) ② 각 핀은 어느 기능에 연결되고 부팅 중에는 어떤 상태인가(4절) ③ 코드·데이터·스택·DMA 버퍼는 어느 주소에 놓이는가(5절) ④ 디바이스는 어떤 의존 순서로 초기화되고 하나가 실패하면 어떻게 되는가(6절).

---

## 1. BSP는 무엇이고 무엇이 아닌가

### 1.1 정의

BSP는 **보드에 종속된 사실을 한곳에 모아, 그 위층이 보드를 몰라도 되게 만드는 코드와 설정의 묶음**이다. 핵심은 "모은다"이다. 같은 정보가 앱 코드 곳곳에 흩어져 있으면 그건 BSP가 없는 것이다. 담는 사실의 예: 메인 크리스털이 32 MHz인가 38.4 MHz인가, 32.768 kHz 크리스털이 실제로 실장됐는가, 마이크가 I2S인가 PDM인가, 센서 전원이 PMIC의 어느 레일에서 나오고 안정에 몇 ms가 걸리는가, I2C 풀업이 4.7k인가 2.2k인가(= 400 kHz가 가능한가), 부트로더가 차지하는 flash 영역은 어디까지인가.

### 1.2 BSP가 아닌 것

| 항목 | BSP인가 | 이유 |
|---|---|---|
| I2C 컨트롤러 드라이버, 센서 칩 드라이버 | ❌ 벤더·디바이스 드라이버 | 같은 SoC나 같은 칩을 쓰는 모든 보드가 공유한다 |
| i2c1을 400 kHz로 올릴지 | ✅ | 풀업 저항과 버스 캐패시턴스가 PCB 사실이다 |
| RTOS 스케줄러, task 우선순위 설계 | ❌ 앱·시스템 설계 | JD에서 BSP와 나란히 적힌 별개 항목이다 |
| 링커 스크립트, flash 파티션 | ✅ | 메모리 부품과 OTA 정책이 결정한다 |
| BLE 스택 | ❌ | 단, "라디오가 쓰는 32.768 kHz 소스"는 ✅ |
| 공장 테스트 명령 핸들러 | 🟡 | 프로토콜은 앱, "테스트 모드 진입 strap"은 BSP |

면접에서 "BSP를 소유한다는 게 무슨 뜻이냐"는 질문의 정답은 이 경계선이다. 범위를 넓게 부르는 사람이 아니라 **경계를 정확히 긋는 사람**을 찾는 질문이다.

### 1.3 세 세계에서 BSP가 놓이는 자리

| 세계 | BSP의 실체 | 대표 산출물 |
|---|---|---|
| bare-metal MCU (벤더 SDK) | `board/` + `board.h` + `clock_config.c` + `pin_mux.c` + 링커 스크립트 | 보드별 C 파일 몇 개 |
| Zephyr / RTOS | `boards/<vendor>/<board>/` (devicetree + Kconfig) | `board.yml`, `<board>.dts`, `<board>_defconfig` (B02) |
| Linux / Android (Cortex-A) | 부트로더 보드 파일 + 커널 device tree + 벤더 드라이버 + HAL | `.dts`, defconfig, TF-A/U-Boot 보드 포트 (B03) |

이름은 달라도 **담는 정보는 같다**: 클럭, 핀, 메모리, 디바이스 목록, 초기화 순서. 한 세계에서 이해하면 나머지는 문법 차이다. Zephyr 경험이 얇을 때 쓸 수 있는 진짜 근거가 이것이다.

---

## 2. reset에서 application까지 — 부트 체인

### 2.1 단일 이미지 Cortex-M

```
 [전원 인가] VDD가 POR 임계값을 넘고 레귤레이터 안정 → POR/BOR 회로가 코어 reset 해제
     ▼
 [코어가 0x0000_0000에서 두 워드를 읽는다]  word0 = 초기 MSP, word1 = Reset_Handler
     ▼
 [Reset_Handler] ① SystemInit(클럭·flash wait state) ② .data를 flash(LMA)→SRAM(VMA)
     │           ③ .bss를 0으로  ④ C++ 정적 생성자(__libc_init_array)
     ▼
 [main]          ⑤ 핀 먹싱, PMIC 레일 on, 리비전 읽기  ⑥ 디바이스 초기화(레벨 순서)
     │           ⑦ RTOS 시작
     ▼
 [애플리케이션 task]
```

Cortex-M은 벡터 테이블 첫 워드가 **스택 포인터**라 하드웨어가 SP를 세팅해 준다. 그래서 Reset_Handler를 처음부터 C로 쓸 수 있다. Cortex-A/R은 각 모드의 SP를 어셈블리로 직접 세팅해야 한다 — Don이 Cortex-R8/R82에서 봤던 차이다.

```c
#include <stdint.h>
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;  /* 링커가 정의 */
extern void SystemInit(void), __libc_init_array(void);
extern int  main(void);
void Reset_Handler(void);
void Default_Handler(void) { for (;;) { } }

/* 링커 스크립트가 .isr_vector를 flash 맨 앞에 배치한다 */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void))&_estack,   /* [0] 초기 MSP — 하드웨어가 읽어 SP에 넣는다 */
    Reset_Handler,              /* [1] reset */
    Default_Handler,            /* [2] NMI, [3] HardFault, ... 이하 IRQ */
};

void Reset_Handler(void)
{
    SystemInit();     /* ① 클럭·flash 타이밍이 먼저다. 어기면 아래 복사 루프가
                       *    잘못된 wait state로 flash를 읽다가 랜덤하게 죽는다. */
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) { *dst++ = *src++; }               /* ② .data */
    for (dst = &_sbss; dst < &_ebss; dst++) { *dst = 0U; }   /* ③ .bss  */
    __libc_init_array();                                     /* ④ 생성자 */
    (void)main();
    for (;;) { }
}
```

세 줄 요약: **클럭 먼저, 그다음 메모리, 그다음 C.** 이것이 BSP 부트 코드의 유일한 불변식이다.

### 2.2 부트로더가 끼면

```
 reset ─► [ROM bootloader] 부트 소스 선택(strap/eFuse), 1차 서명 검증
       ─► [MCUboot 등 부트로더] flash 0x00000~ : 슬롯 선택, 헤더·서명 검증, swap
       ─► [애플리케이션] flash 0x10000~ [예시] : SCB->VTOR 재배치 → main()
```

여기서 BSP가 추가로 소유하는 것이 둘이다. 첫째는 **flash 파티션 테이블**(부트로더 / slot0 / slot1 / storage / factory의 주소·크기). Zephyr에서는 devicetree의 `fixed-partitions`가 이걸 담는다. 둘째는 **VTOR 재배치**다. 빠뜨리면 "부팅은 되는데 첫 인터럽트에서 부트로더 핸들러로 점프해 죽는다"는 전형적 증상이 나온다.

### 2.3 SoC + always-on MCU 두 프로세서 (Hark 같은 기기 [추정])

context 2.2절의 추정 구조(Qualcomm SoC + Ambiq급 always-on MCU)를 가정하면 부트 체인이 둘이고 서로 의존한다.

```
 [배터리/USB-C] ─► [PMIC] ─┬─ VDD_MCU (먼저) ─► always-on MCU (Cortex-M, RTOS)
                           │                     · PMIC 제어, wake word, 센서 허브
                           │                     · SoC 전원/reset 제어 ◄──IPC──┐
                           ├─ VDD_SOC (나중) ─► 앱 SoC (Cortex-A, Android) ────┘
                           │                     · BootROM → TF-A → U-Boot → kernel
                           └─ VDD_SENSORS ───► 센서 / 마이크
```

이 구조에서 MCU의 BSP는 자기 보드뿐 아니라 **SoC를 깨우고 재우는 정책**까지 소유한다. "이 기기의 부트 순서를 설계해 보라"가 나오면 이 그림을 그리고 셋을 말한다. MCU가 먼저 뜬다(전력이 훨씬 적고 wake 조건을 감시해야 하므로). SoC는 필요할 때만 MCU가 켠다. 둘 사이 IPC는 **양쪽이 서로 다른 시점에 리셋될 수 있다**고 가정해야 하므로 sequence number와 재동기화 경로가 필요하다.

### 2.4 각 단계에서 BSP가 책임지는 것

| 단계 | BSP 산출물 | 틀렸을 때 증상 |
|---|---|---|
| POR / 브라운아웃 | BOR 임계값, 레일 순서 | 저전압에서 부팅 반복, 배터리 소진 시 벽돌화 |
| 벡터 테이블 | `.isr_vector` 배치, VTOR | 즉시 HardFault, 또는 인터럽트에서만 죽음 |
| SystemInit, .data/.bss | 클럭·PLL·wait state, 링커 심볼 | 랜덤 하드폴트, 전역변수가 쓰레기값 |
| 핀 먹싱 | pinmux 테이블 | 버스가 아예 안 움직임, 두 기능이 한 핀 충돌 |
| 디바이스 init | 초기화 순서·레벨 | "센서 없음" 오류, 레일 대기 시간 부족 |
| RTOS 시작 | 스택 크기, 힙, tick 소스 | 스택 오버플로, tick 미도착으로 스케줄러 정지 |

### Don 경험과의 접점

"FPGA pre-silicon에서 Cortex-R8/R82/M0+를 처음 부팅시켰다"는 위 표의 **윗 세 행 전부**에 해당한다. pre-silicon은 더 벌거벗은 상태라 벡터 테이블과 메모리 초기화가 틀리면 로그 한 줄 없이 멈춘다. 반면 아래 세 행(보드 레벨 핀 먹싱, devicetree 기반 init 순서, RTOS 통합)은 제품 BSP 오너의 일이고 나는 그 경험이 없다. 면접에서는 이 선을 그대로 말하는 편이 낫다.

---

## 3. 전원과 클럭 bring-up 순서

### 3.1 왜 순서가 문제인가

칩은 "전원이 들어왔다"가 아니라 **"모든 레일이 규정된 순서와 시간 안에 규정된 전압에 도달했다"**를 요구한다. 어기면 동작하지 않거나, 더 나쁘게는 **대부분의 보드에서는 동작하고 일부에서만 실패**한다. Don이 Apple에서 본 "EVT 100대 중 5대만 실패" 부류의 원인 1순위가 여기다.

```
 VDD_CORE   ___/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   t1: core 먼저
 VDD_IO     ______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   t2: IO (래치업 방지 규칙은 칩마다 다름)
 VDD_ANALOG ____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   t3: 아날로그(마이크 바이어스 등) 마지막
 nRESET     ________________________/‾‾‾‾‾‾‾‾   모든 레일 안정 + t_reset 후 해제
```

규칙 자체는 칩마다 다르다. BSP 오너의 일은 규칙을 만드는 게 아니라 **데이터시트의 규칙을 PMIC 설정과 코드에 정확히 옮기고 측정으로 확인하는 것**이다. 확인 도구는 4채널 스코프다(레일 3 + reset 1).

### 3.2 클럭 bring-up의 표준 순서

1. 안전한 내부 RC로 동작 중임을 확인한다(리셋 직후 기본값이 보통 이것이다).
2. **flash wait state를 목표 주파수에 맞게 먼저 올린다.** 필요하면 전압 스케일링도 함께.
3. 외부 크리스털(HFXO)을 켜고 **타임아웃과 함께** ready를 기다린다.
4. PLL 분주·체배를 설정하고 PLL을 켠 뒤 lock을 기다린다.
5. 시스템 클럭 소스를 PLL로 전환하고 status 비트로 실제 전환을 확인한다.
6. AHB/APB 프리스케일러를 데이터시트 최대값 이하로 맞춘 뒤, 주변장치 클럭을 개별로 켠다.

2번이 1순위 함정이다. wait state보다 주파수를 먼저 올리면 flash에서 명령어를 잘못 읽어 **어디서 죽었는지도 모르는 하드폴트**가 난다. 반대로 주파수를 내릴 때는 순서가 뒤집힌다 — 먼저 클럭을 내리고 그다음 wait state를 내린다.

```c
#include <stdint.h>
#include <stdbool.h>
/* [예시] 가상의 레지스터. 실제로는 벤더 CMSIS 디바이스 헤더를 쓴다. */
#define FLASH_ACR      (*(volatile uint32_t *)0x40022000UL)
#define RCC_CR         (*(volatile uint32_t *)0x40021000UL)
#define RCC_CR_HSEON   (1U << 16)
#define RCC_CR_HSERDY  (1U << 17)

static bool wait_flag(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t i = 0; i < 100000U /* [예시] 타임아웃 */; i++) {
        if ((*reg & mask) != 0U) { return true; }
    }
    return false;                                /* 크리스털 미실장/불량 */
}

int board_clock_init(void)
{
    FLASH_ACR = (FLASH_ACR & ~0x7U) | 4U;        /* ① [예시] 4 wait states 먼저 */
    RCC_CR |= RCC_CR_HSEON;                      /* ② 외부 크리스털 기동 */
    if (!wait_flag(&RCC_CR, RCC_CR_HSERDY)) {
        return -1;                               /* 내부 RC로 degraded + 에러 로깅 */
    }
    return 0;  /* ③ PLL은 꺼진 상태에서만 설정 → lock 대기 → 전환 확인 (생략) */
}
```

크리스털이 안 뜰 때 **무한 루프로 기다리는 코드는 BSP에서 금지**다. 공장에서 미실장 보드가 한 대 나오면 그 보드는 영원히 조용히 멈춰 있고, 테스터는 "왜 죽었는지 모르는 보드"를 받는다. 타임아웃 후 내부 RC로 degraded 부팅하고 에러 코드를 남기면 같은 보드가 UART로 "HFXO fail"이라고 말해 준다. 이 차이가 공장에서 시간당 수십 분이다.

### 3.3 always-on 기기의 클럭 정책

| 결정 | 선택지 | trade-off |
|---|---|---|
| 저속 클럭 소스 | 32.768 kHz 크리스털(LFXO) vs 내부 RC(LFRC) | LFXO는 정확도가 높아 BLE 연결 유지에 유리하지만 부품비와 기동 시간이 든다. LFRC는 오차가 커서 라디오 수신 윈도우를 넓게 잡아야 하고 그만큼 평균 전류가 오른다 |
| 고속 클럭 상시 여부 | 항상 PLL vs 필요할 때만 | always-on MCU는 저속으로 자다가 이벤트에 PLL을 올린다. 올리는 시간이 레이턴시 예산에 들어간다 |
| 주변장치 클럭 게이팅 | 개별 on/off | 안 쓰는 블록의 클럭을 끄지 않으면 sleep 전류가 수백 µA 남는다 |

LFXO 기동 시간은 부품 스펙 사항이고 길면 수백 ms다. 부트 경로에서 동기적으로 기다리면 전원 버튼부터 반응까지의 시간에 그대로 더해지므로, 보통 LFRC로 먼저 동작하다 준비되면 전환한다.

### 3.4 흔한 함정

| 함정 | 증상 |
|---|---|
| wait state를 나중에 올림 | 클럭 함수 안에서 하드폴트, PC가 이상한 값 |
| 크리스털 ready 무한 대기 | 일부 보드에서 로그 없이 정지 |
| 주변장치 클럭 켜기 전 레지스터 접근 | 버스 폴트, 또는 write가 조용히 무시됨 |
| 클럭 변경 후 보레이트·타이머 재계산 누락 | UART 깨짐, 타이머 주기가 배수만큼 어긋남 |
| 부하 커패시터가 크리스털 스펙과 불일치 | 주파수 오프셋 → BLE 연결 불안정, 온도에 취약 |
| 주변장치 클럭 게이팅 미정리 | sleep 전류가 스펙보다 수백 µA 높음 |

### Don 경험과의 접점

pre-silicon FPGA에서는 PLL 대신 FPGA 클럭 생성기를 썼지만 **"클럭이 제대로 잡혔는지 확인하기 전에는 아무것도 믿지 않는다"**는 순서 감각은 같다. Apple에서 다룬 RFFE/SPMI 이슈도 상당수가 전원·클럭 시퀀스와 reset 타이밍 문제였다. 이 절은 과장 없이 "내가 자주 서 있던 자리"라고 말해도 되는 곳이다.

---

## 4. 핀 먹싱(pin muxing)

### 4.1 왜 존재하나

SoC 안에는 UART 4개, SPI 4개, I2C 3개, PWM 8개가 들어 있는데 패키지 핀은 48개다. 모든 기능에 전용 핀을 줄 수 없으므로 각 물리 핀은 **여러 기능 중 하나를 고르는 먹스**를 갖는다.

```
   UART0_TX ─┐
   SPI1_SCK ─┤   ┌───────────┐      ┌───────────┐
   PWM2_CH0 ─┼──►│  4:1 MUX  │─────►│  패드 셀  │═══ P0.06 → PCB net
   GPIO out ─┘   └─────▲─────┘      └─────▲─────┘
        먹스 선택 레지스터(BSP)            └ pull up/down, 드라이브 강도, slew rate
```

BSP의 핀 테이블은 회로도와 **1:1로 대응해야 하는 유일한 문서**다. 회로도 net 이름을 코드 주석에 그대로 쓰는 습관이 디버깅 시간을 가장 크게 줄인다.

### 4.2 부팅 중 핀 상태 — 잊기 쉬운 함정

리셋 직후 대부분의 핀은 **입력 + 하이 임피던스**이고 이 상태가 수 ms에서 수십 ms 이어진다. 문제가 되는 경우가 셋이다. 외부 회로가 그 핀을 enable로 해석하는 경우(파워 스위치 EN에 풀업이 있으면 펌웨어가 끄기 전에 이미 켜진다 — 대응은 하드웨어 풀다운이고 소프트웨어로는 이길 수 없다). I2C SDA/SCL이 플로팅이면 슬레이브가 반쪽 트랜잭션 상태로 갈 수 있어서 bring-up 초기에 버스 클리어(SCL 9펄스)를 먼저 하는 BSP가 많다(C03 7.4절). 코덱/마이크 enable 핀이 잠시 토글되면 pop noise가 나는데, 오디오 기기에서 실제로 문제가 된다.

미사용 핀을 플로팅으로 두면 입력 버퍼가 임계 전압 근처에서 발진해 **수십 µA의 누설**이 생긴다. always-on 기기에서는 배터리 수명 숫자에 바로 보인다. 정석은 미사용 핀을 "입력 + 풀다운" 또는 "출력 low"로 명시 고정하고 그 목록을 핀 테이블에 남기는 것이다.

### 4.3 코드: 데이터 주도 핀 테이블

함수 호출 나열로 쓰면 리비전이 늘 때 관리가 불가능해진다. 테이블로 쓰고 리비전별로 테이블만 고른다.

```c
#include <stdint.h>
#include <stddef.h>
typedef enum { PIN_FUNC_GPIO = 0, PIN_FUNC_UART, PIN_FUNC_I2C, PIN_FUNC_I2S } pin_func_t;
typedef enum { PIN_PULL_NONE, PIN_PULL_UP, PIN_PULL_DOWN } pin_pull_t;

typedef struct {
    uint8_t     pin;        /* 포트*32 + 핀번호 */
    pin_func_t  func;
    pin_pull_t  pull;
    uint8_t     drive_ma;   /* [예시] 드라이브 강도 */
    const char *net;        /* 회로도 net 이름 — 디버깅용 */
} pin_cfg_t;

static const pin_cfg_t board_pins_revb[] = {
    { 0*32 +  6, PIN_FUNC_UART, PIN_PULL_NONE, 2, "DBG_UART_TX" },
    { 0*32 + 26, PIN_FUNC_I2C,  PIN_PULL_NONE, 2, "SENSOR_SDA" }, /* 외부 2.2k 풀업 */
    { 1*32 +  0, PIN_FUNC_I2S,  PIN_PULL_NONE, 4, "MIC_BCLK"   },
    { 1*32 +  5, PIN_FUNC_GPIO, PIN_PULL_DOWN, 2, "NC"         }, /* 미사용 고정 */
};

extern void soc_pin_configure(uint8_t pin, pin_func_t f, pin_pull_t p, uint8_t drive);

void board_pinmux_apply(const pin_cfg_t *t, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        soc_pin_configure(t[i].pin, t[i].func, t[i].pull, t[i].drive_ma);
    }
}
```

Zephyr에서는 이 테이블이 C가 아니라 devicetree의 pinctrl 노드가 된다. 형태만 다르고 담는 정보는 같다(B02 6절).

### 4.4 흔한 함정

| 함정 | 증상 |
|---|---|
| 두 기능이 같은 핀에 배정 | 나중에 설정한 쪽만 동작, 다른 하나는 조용히 죽음 |
| 회로도 net과 코드 핀 번호 불일치 | 신호가 엉뚱한 테스트 포인트에 나옴. LA로는 5분, 코드만 보면 하루 |
| 리비전에서 핀이 이동했는데 테이블은 그대로 | 신규 보드만 동작 안 함 |
| I2C에 내부 + 외부 풀업 중복, 미사용 핀 플로팅 | 레벨 위반 또는 sleep 전류가 수십 µA 높고 보드마다 편차 |

---

## 5. 메모리 맵과 링커 / 스캐터 레이아웃

### 5.1 보드의 메모리 맵

```
 0xE000_0000 ┌────────────────────────┐ 시스템 (SCB, NVIC, DWT)
 0x6000_0000 ├────────────────────────┤ 외부 QSPI flash (XIP 가능 — 모델 가중치 후보)
 0x4000_0000 ├────────────────────────┤ 주변장치 레지스터
 0x2004_0000 ├────────────────────────┤
             │ SRAM 256KB [예시]       │  .data/.bss → heap(↓) … stack(↑)
             │                        │  ├ DMA 전용 영역 (정렬·캐시 속성 별도)
             │                        │  └ retained RAM (deep sleep에서 유지)
 0x2000_0000 ├────────────────────────┤
             │ 내부 flash 1MB [예시]   │  MCUboot 0x00000 / slot0 0x10000
             │                        │  slot1 0x80000 / storage 0xF0000
 0x0000_0000 └────────────────────────┘
```

이 그림에서 BSP가 **결정**하는 것은 flash 파티션 경계, SRAM 안의 특수 영역(DMA, retained), 스택·힙 크기다. 나머지는 실리콘이 정한다.

### 5.2 GNU ld 링커 스크립트 골격

```ld
MEMORY
{
  FLASH (rx)   : ORIGIN = 0x00010000, LENGTH = 448K  /* slot0 [예시] */
  RAM   (rwx)  : ORIGIN = 0x20000000, LENGTH = 248K
  RAM_DMA (rw) : ORIGIN = 0x2003E000, LENGTH = 6K    /* [예시] */
  RAM_RET (rw) : ORIGIN = 0x2003F800, LENGTH = 2K    /* [예시] */
}
_stack_size = 4K;
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH      /* 반드시 맨 앞, KEEP 필수 */
  .text       : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH

  _sidata = LOADADDR(.data);                          /* LMA는 FLASH, VMA는 RAM */
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM

  .dma_buf (NOLOAD)       : { . = ALIGN(32); KEEP(*(.dma_buf)) } > RAM_DMA
  .noinit_retain (NOLOAD) : { KEEP(*(.noinit_retain)) } > RAM_RET

  ._stack (NOLOAD) : { . = ALIGN(8); . = . + _stack_size; _estack = .; } > RAM

  ASSERT(_estack <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow")  /* 런타임에 발견하지 말 것 */
}
```

```c
__attribute__((section(".dma_buf"), aligned(32))) static uint8_t g_i2s_rx[2][512];
__attribute__((section(".noinit_retain")))       static volatile uint32_t g_boot_count;
```

`.noinit_retain`을 `.bss`에 두면 매 부팅마다 0이 되어 크래시 로그가 사라진다. 링커에서 `NOLOAD`로 분리하고 **startup의 bss 클리어 범위에서 빼는 것**까지가 한 세트다.

ARM armlink의 스캐터 파일(`.scat`)은 문법만 다르다. `MEMORY` 대신 load region / execution region으로 쓰고, LMA/VMA는 execution region 주소를 따로 적어 분리하며, 경계 심볼은 `Image$$REGION$$Base` 같은 이름이 자동 생성되고, 오버플로 검사는 링커가 기본 제공한다. Don이 SSD 펌웨어에서 armlink를 썼다면 개념은 그대로 옮겨진다.

### 5.3 흔한 함정

| 함정 | 증상 |
|---|---|
| `.isr_vector`에 `KEEP()` 누락 | LTO/gc-sections가 벡터 테이블을 제거해 부팅 즉시 폴트 |
| 힙과 스택이 서로 침범 | 깊은 호출에서 malloc 데이터 손상. 재현이 매우 어렵다 |
| DMA 버퍼가 캐시 영역에(M7/M55) | 오래된 데이터. 클린/무효화 누락 증상 |
| 파티션 정의가 부트로더와 불일치, 링커 ASSERT 없음 | OTA 후 부팅 실패, RAM 초과를 런타임에 발견 |

---

## 6. 디바이스 초기화 순서와 의존성 레벨

### 6.1 의존성은 그래프다

```
 [클럭]
   ├──► [GPIO] ──► [PMIC 레일 enable] ──┬──► [I2C 버스] ──► [IMU, 연료계]
   ├──► [타이머]                        ├──► [SPI 버스] ──► [외부 flash]
   └──► [UART 콘솔] ──► [로깅 가능]      └──► [I2S 클럭] ──► [마이크 코덱]
```

중요한 성질은 **시간 의존성도 섞여 있다**는 것이다. PMIC 레일을 켠 뒤 센서가 I2C에 응답하기까지 수 ms가 필요하다. "순서대로 호출"만으로 부족하고 **레일 안정 대기**가 들어가야 한다. 그 대기 시간을 데이터시트에서 찾아 근거를 주석에 남기는 게 BSP 오너의 기본 위생이다.

### 6.2 레벨로 표현하기

RTOS마다 이름은 달라도 아이디어는 같다. Zephyr는 `EARLY`, `PRE_KERNEL_1`, `PRE_KERNEL_2`, `POST_KERNEL`, `APPLICATION`, `SMP` 여섯 레벨과 0~999 우선순위를 쓴다(공식 문서 확인, B02 5절). bare-metal에서는 직접 만들어야 한다.

```c
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    int       (*fn)(void);
    uint8_t     level;      /* 0:클럭·코어 1:버스 2:디바이스 3:서브시스템 */
    uint8_t     prio;       /* 같은 레벨 안의 순서, 작을수록 먼저 */
    const char *name;       /* 실패 로그용 */
    bool        critical;   /* true면 실패가 곧 부팅 실패 */
} init_entry_t;

extern const init_entry_t __init_start[], __init_end[];  /* 링커가 .init_table에 모은다 */
extern void board_log_init_failure(const char *name, int rc);

int board_run_init(void)
{
    for (uint8_t lvl = 0; lvl <= 3; lvl++) {
        for (const init_entry_t *e = __init_start; e < __init_end; e++) {
            if (e->level != lvl) { continue; }           /* 실제로는 prio까지 정렬 */
            int rc = e->fn();
            if (rc != 0) {
                board_log_init_failure(e->name, rc);     /* 여기가 설계 결정 지점 */
                if (e->critical) { return rc; }          /* 중단 → 안전 모드 */
            }                                            /* 아니면 degraded로 계속 */
        }
    }
    return 0;
}
```

### 6.3 실패 정책이 진짜 설계다

| 디바이스 | 실패 시 | 이유 |
|---|---|---|
| 클럭(HFXO) | 내부 RC로 degraded + 에러 플래그 | 부팅해서 상태를 보고해야 공장에서 판정할 수 있다 |
| 콘솔 UART | 무시하고 계속 | 로그가 없다고 제품이 죽으면 안 된다 |
| PMIC | 중단 + 안전 모드 | 이후가 전부 의미 없다 |
| IMU | 기능 degraded, 앱에 통보 | 음성 기능은 계속 동작해야 한다 |
| 마이크/코덱 | 재시도 후 degraded | 핵심 기능이라 재시도 가치가 있다 |
| 캘리브레이션 CRC 실패 | 기본값 + 경고 | 벽돌보다 낫다. 단 공장 테스트는 실패 처리 |

이 표를 면접에서 즉석에서 만들 수 있으면 "BSP를 시스템으로 본다"는 인상을 준다. 실패 처리는 경험 유무가 가장 크게 갈리는 지점이고, Don이 SSD 펌웨어에서 설계한 error reporting/handling 체계가 정확히 이 사고방식이다 — 정직하게 강조해도 되는 접점이다.

---

## 7. HAL과 드라이버 계층

```
 앱 / 서브시스템        sensor_sample_fetch()    ▲  BSP는 이 스택을 관통한다:
 서브시스템 API         i2c_transfer(dev, …)     │   · 어느 컨트롤러 인스턴스를 쓰나
 디바이스 드라이버      bme280.c, codec.c        │   · 어느 칩이 어느 주소로 붙었나
 컨트롤러 드라이버      i2c_nrfx_twim.c          │   · 어떤 속도·모드로 쓰나
 LL / 레지스터 헤더     NRF_TWIM0->TASKS_…       │
 하드웨어                                        ┘
```

흔한 실수는 board API를 너무 두껍게 만드는 것이다. `board_led_set(LED_STATUS, true)` 같은 함수를 수십 개 만들면 새 보드마다 전부 다시 구현해야 한다. 반대로 너무 얇으면 앱이 핀 번호를 알게 된다. 실무 기준선은 이렇다. 앱은 **역할 이름**으로 디바이스를 얻고(`상태 LED`, `마이크`, `연료계`), 역할 → 인스턴스 매핑이 BSP이며(Zephyr의 `aliases`/`chosen`이 정확히 이 역할이다), 동작 자체는 표준 서브시스템 API로 한다.

```c
typedef enum { BOARD_DEV_MIC, BOARD_DEV_IMU, BOARD_DEV_FUEL_GAUGE } board_dev_t;
struct device;                                   /* RTOS 쪽 타입 */
const struct device *board_device(board_dev_t which);
bool                 board_has(board_dev_t which);  /* 리비전에 따라 없을 수 있다 */
```

`board_has()`가 필요한 이유는 8절과 이어진다 — EVT에는 있고 DVT에는 빠진 부품이 생기기 때문이다.

JD가 "driver development"가 아니라 **"driver integration"**이라고 쓴 이유도 이 그림에 있다(J02 0절). 실무 대부분은 남이 쓴 드라이버를 우리 보드에 맞게 붙이는 일이고, 정책이 없으면 6개월 뒤 업그레이드가 불가능해진다. 최소 원칙 넷: 벤더 코드를 직접 고치지 않고 고쳐야 하면 패치 파일로 분리한다, 벤더 API를 우리 래퍼로 감싸 교체 시 수정 범위를 한정한다, SDK 버전·커밋 해시를 빌드 산출물에 찍는다, 알려진 errata와 우회 코드를 같은 자리에 주석으로 남긴다.

---

## 8. 보드 리비전과 variant 관리

### 8.1 리비전은 반드시 생긴다

```
 P1/proto ──► EVT ──► EVT2 ──► DVT ──► PVT ──► MP
  손납땜      최초     핀 이동   기구     양산    양산
  점퍼 다수   PCB     2nd source 최종    검증
```

각 단계에서 핀이 움직이고 부품이 바뀌고 풀업 값이 조정된다. **그리고 이전 보드는 사라지지 않는다** — 팀 전체에 EVT가 20대, DVT가 5대 흩어져 있고 다들 최신 펌웨어를 올리고 싶어 한다. 그래서 "하나의 이미지가 여러 리비전에서 동작"하거나 최소한 "하나의 코드베이스가 여러 리비전 이미지를 만든다"가 요구사항이 된다.

### 8.2 세 가지 관리 방식

| 방식 | 구현 | 장점 | 단점 |
|---|---|---|---|
| 빌드 타임 분기 | 리비전별 defconfig/overlay, `-DBOARD_REV=2` | 코드·플래시 최소, 명확 | 이미지가 리비전 수만큼. 잘못 플래시하면 조용히 오동작 |
| 런타임 감지 | strap GPIO 또는 ADC 분압 저항으로 ID 읽기 | 이미지 하나로 전 보드 커버, 랩·공장 실수 감소 | 분기 증가, 양쪽 드라이버를 다 포함해야 함 |
| 하이브리드 | 메모리 맵·핀은 빌드 타임, 부품 차이는 런타임 | 실무에서 가장 흔함 | 규칙을 문서화하지 않으면 혼란 |

Zephyr는 첫 번째를 정식 지원한다(`board.yml`의 `revision:`과 `<board>_<revision>.overlay`, B02 8절). 다만 **런타임 ID는 어떤 방식을 쓰든 넣는 게 좋다** — 최소한 "이 이미지가 이 보드용인지" 확인해 틀리면 크게 경고할 수 있다.

### 8.3 런타임 보드 ID 읽기

```c
#include <stdint.h>
/* 저항 분압 + ADC 방식. GPIO strap보다 핀을 적게 쓰고, 값이 연속이라
 * 중간 리비전을 끼워 넣기 쉽다. */
typedef enum { BOARD_REV_EVT1 = 0, BOARD_REV_EVT2, BOARD_REV_DVT,
               BOARD_REV_UNKNOWN } board_rev_t;

/* [예시] 리비전별 기대 전압(mV)과 허용 오차. 저항 1% 오차를 고려해 넉넉히. */
static const struct { uint16_t mv, tol; board_rev_t rev; } id_map[] = {
    { 165, 80, BOARD_REV_EVT1 }, { 825, 80, BOARD_REV_EVT2 },
    { 1650, 80, BOARD_REV_DVT },
};

extern int adc_read_mv(uint8_t channel, uint16_t *out_mv);

board_rev_t board_detect_revision(void)
{
    uint16_t mv;

    if (adc_read_mv(0 /* [예시] ch0 = BOARD_ID */, &mv) != 0) {
        return BOARD_REV_UNKNOWN;
    }
    for (unsigned i = 0; i < sizeof(id_map) / sizeof(id_map[0]); i++) {
        uint16_t lo = (id_map[i].mv > id_map[i].tol) ? id_map[i].mv - id_map[i].tol : 0;
        if (mv >= lo && mv <= (uint16_t)(id_map[i].mv + id_map[i].tol)) {
            return id_map[i].rev;
        }
    }
    return BOARD_REV_UNKNOWN;    /* 새 리비전이 나왔는데 펌웨어가 모른다는 뜻 */
}
```

설계 포인트 둘. 구간을 **겹치지 않게** 나누고 미지 값은 반드시 `UNKNOWN`으로 처리한다("모르면 최신으로 가정"은 나중에 필드 사고가 된다). 그리고 읽은 리비전을 **부팅 배너에 찍는다** — 랩에서 "이 보드가 뭐였더라" 문제의 90%가 사라진다.

리비전 말고도 축은 더 갈라진다. 지역 SKU(라디오 밴드, 규제 전력)는 공장에서 프로비저닝하는 런타임 설정 데이터로, 부품 2nd source는 `WHO_AM_I` 레지스터 런타임 probe로, 개발용 vs 양산은 빌드 타입 + eFuse/OTP 잠금으로, 기구 조립 차이는 캘리브레이션 데이터로 다룬다. "리비전 분기"와 "SKU 분기"를 같은 매크로에 섞기 시작하면 6개월 뒤 아무도 못 읽는다. 축을 분리해 이름 짓는 게 유일한 방어다.

### Don 경험과의 접점

Apple의 bring-up → NPI → MP 사이클에서 EVT/DVT/PVT마다 보드가 바뀌는 상황은 직접 겪었고, factory test-node 설계는 "여러 보드 버전을 같은 흐름에서 판정하는" 문제 자체다. BSP 코드로 리비전 분기를 운영해 본 것은 아니지만, **왜 이 문제가 생기고 공장에서 무엇이 잘못되는지는 현장에서 본 사람**이다. 이 층위로 말하면 과장이 아니다.

---

## 9. BSP를 어떻게 테스트하고 앱팀에 넘기나

### 9.1 첫 전원 인가 순서

"새 EVT 보드가 왔다, 뭐부터 하나?"는 단골 질문이다. 원칙은 **한 번에 한 가지 미지수만 남기는 것**이다.

1. 전원 인가 전, 각 레일과 GND 사이 저항으로 단락 확인. 걸리면 전원을 넣지 않는다.
2. 전류 제한을 건 전원으로 인가하고 **전류를 먼저 본다**. 예상보다 크면 즉시 차단.
3. 스코프로 레일 순서·램프·reset 해제 시점, 그다음 클럭을 확인한다.
4. 디버거 연결 — 여기까지 오면 코어가 살아 있는지 안다.
5. 가장 단순한 이미지로 GPIO 하트비트. "코드가 실행된다"의 첫 증거.
6. 콘솔 UART. 여기서부터 디버깅 속도가 10배가 된다.
7. 버스를 하나씩: I2C 스캔 → SPI ID 읽기 → I2S 클럭 파형.
8. 전원·클럭 정책 적용 후 sleep 전류 측정, 리비전 ID·POST·부팅 배너 정리.

### 9.2 보드 자가 테스트(POST)

BSP가 앱팀에 넘겨야 할 산출물 중 가장 저평가되는 것이 **"보드가 정상인지 스스로 말하는 기능"**이다.

```c
#include <stdint.h>
typedef struct { const char *name; int (*run)(void); } post_test_t;

/* 각 테스트: 클럭(HFXO/LFXO ready와 주파수), PMIC(레일 전압을 ADC로 읽어 범위 확인),
 * I2C(기대 주소가 전부 ACK 하는가), SPI(flash JEDEC ID가 기대값인가) */
extern const post_test_t post_tests[];
extern const unsigned    post_test_count;
extern void board_log_post_failure(const char *name, int rc);

uint32_t board_post_run(void)     /* 0이면 정상. 공장 테스터가 이 값을 읽는다 */
{
    uint32_t fail_mask = 0;

    for (unsigned i = 0; i < post_test_count; i++) {
        int rc = post_tests[i].run();
        if (rc != 0) {
            fail_mask |= (1U << i);
            board_log_post_failure(post_tests[i].name, rc);
        }
    }
    return fail_mask;
}
```

비트마스크 하나가 공장에서 "어느 서브시스템 불량인가"를 바로 알려 준다. Don의 factory test-node 경험이 자연스럽게 이어지는 지점이다.

### 9.3 회귀 테스트와 인수 조건

| 층 | 방법 | 비고 |
|---|---|---|
| 빌드 | 모든 보드 x 리비전 조합을 CI에서 빌드 | Zephyr는 `twister`가 한다 |
| 시뮬레이터 / HIL | QEMU·Renode·native_sim, 실제 보드 + 자동 플래시 + 시리얼 판정 | 시뮬은 로직 일부만. HIL이 BSP 회귀의 유일한 진짜 방법 |
| 전류 회귀 | 전력 측정기로 sleep 전류를 CI에서 추적 | always-on 기기 필수. 한 번 새면 끝까지 샌다 |

마지막 행이 배터리 기기 BSP의 차별점이고, 면접에서 던지기 좋은 역질문이기도 하다. 그리고 BSP는 코드만 넘기는 게 아니다. 아래가 갖춰져야 앱팀이 BSP 오너를 매일 부르지 않는다.

- 핀 맵 표(회로도 net ↔ 논리 이름 ↔ 코드 심볼), 메모리·파티션 표(남은 여유 공간 포함)
- 각 디바이스의 초기화 레벨과 실패 시 동작 표, 알려진 errata와 우회 코드 위치
- 리비전 매트릭스, 정상 보드의 기준 부팅 로그, 각 전원 상태의 전류 기준선

이 목록을 말할 수 있으면 "BSP를 만들어 본 적은 없지만 무엇을 만들어야 하는지는 안다"가 설득력을 갖는다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| flash wait state를 클럭 뒤에 설정 | 클럭 초기화 함수 안에서 하드폴트 | 고속에서 flash 읽기 실패 | 순서를 뒤집는다. 내릴 때는 반대 순서 |
| 크리스털 ready 무한 대기 | 일부 보드만 로그 없이 멈춤 | 미실장·불량·부하 커패시터 오류 | 타임아웃 + 내부 RC degraded + 에러 코드 |
| 벡터 테이블 `KEEP()` 누락 | 부팅 즉시 폴트, 빌드 옵션 바꾸면 재현 | gc-sections가 제거 | `KEEP` 추가 후 map 파일로 확인 |
| PMIC 레일 대기 없이 센서 접근 | 부팅 때만 NACK, 재시도하면 성공 | 레일 안정 시간 미준수 | 데이터시트 값 + 여유 대기, 또는 준비 신호 폴링 |
| `.noinit` 데이터를 bss에 배치 | 리셋 후 크래시 로그가 매번 0 | startup이 클리어 | 별도 NOLOAD 섹션 + 클리어 범위 제외 |
| 부트로더와 앱의 파티션 불일치 | OTA 후 부팅 실패 또는 설정 소실 | 두 곳에 하드코딩 | 파티션 정의를 단일 소스(devicetree)로 공유 |
| VTOR 재배치 누락 | 부팅은 되나 첫 인터럽트에서 죽음 | 벡터 테이블이 부트로더 것 | 앱 시작 시 `SCB->VTOR` 설정 |
| 리비전 `#ifdef`를 코드 전역에 살포 | 새 리비전 추가에 며칠 | 경계 없음 | 리비전 차이를 데이터 테이블 한곳으로 |

---

## 11. 면접에서 이렇게 말한다

**Q. What does a BSP actually contain?**

**A.** 넷이다. ① 부트 코드(벡터 테이블, reset handler, 클럭·flash 타이밍) ② 보드 배선 정보(핀 먹싱, 어느 버스에 어느 칩이 몇 번 주소로) ③ 메모리 레이아웃(링커 스크립트, flash 파티션, DMA·retained 영역) ④ 디바이스 초기화 순서와 실패 정책. 그 위층(RTOS, 서브시스템 API, 칩 드라이버)은 BSP가 아니다. BSP의 가치는 범위를 넓히는 게 아니라 **보드 고유 사실을 한곳에 가두는 것**이다.

> A BSP is the layer that turns "this silicon on this PCB" into a generic API. It owns four things: boot code and clock bring-up, the pin mux table and which device sits on which bus, the memory map and linker layout including flash partitions, and device init order with its failure policy. Everything above — the RTOS, the subsystem APIs, the chip drivers — is portable, and my job is to keep board-specific facts from leaking into it.

**Q. Walk me through boot from reset to main.**

**A.** 코어가 벡터 테이블의 첫 두 워드에서 초기 스택 포인터와 reset handler 주소를 읽는다. reset handler는 먼저 클럭과 flash wait state를 세팅하고 — 순서가 반대면 그 함수 안에서 하드폴트가 난다 — `.data`를 복사하고 `.bss`를 지우고 생성자를 돌린 뒤 main으로 간다. 부트로더가 있으면 앞에 ROM → 서명 검증 → 슬롯 선택 → VTOR 재배치가 붙는다.

> The core loads the initial stack pointer and the reset vector from the first two words of the vector table. The reset handler sets up clocks and flash wait states first — raise the clock before the wait states and you fault inside that very function — then copies .data from flash to RAM, zeroes .bss, runs static constructors, and calls main. With a bootloader in front, add ROM boot, signature check, slot selection, and setting VTOR to the application's vector table.

**Q. A new EVT board just arrived. What do you do first?**

**A.** 전원을 넣기 전에 레일 대 GND 저항으로 단락을 본다. 전류 제한을 건 채 인가해 전류부터 본다. 스코프로 레일 순서·reset 해제·클럭을 확인하고, 디버거를 붙이고, GPIO 하트비트로 "코드가 돈다"를 증명하고, 콘솔 UART를 올린다. 그 뒤에야 버스를 하나씩 본다. 원칙은 한 번에 한 가지 미지수만 남기는 것이다.

> Before power: check each rail to ground for shorts. Then bring it up current-limited and watch the current first. Scope the rail sequence, the reset release and the clock. Attach the debugger, prove code runs with a GPIO heartbeat, then get the console UART alive — from there debugging is ten times faster. Only then go bus by bus: I2C scan, SPI device ID, I2S clocks. One unknown at a time.

**Q. How do you handle three board revisions in one codebase?**

**A.** 두 축을 분리한다. 핀이나 메모리 맵처럼 구조가 바뀌는 건 빌드 타임(리비전별 overlay/defconfig), 부품이 있냐 없냐는 런타임(WHO_AM_I probe)으로 처리한다. 어느 쪽이든 **보드 ID를 런타임에 읽어 부팅 배너에 찍는다** — 틀린 이미지를 플래시했을 때 조용히 이상하게 도는 대신 크게 경고하기 위해서다. 리비전 분기는 `#ifdef`로 뿌리지 않고 데이터 테이블 한곳에 모은다.

> I separate two axes. Structural differences — pin moves, memory map, partitions — go in build-time revision overlays. Presence or absence of a part goes runtime, usually by probing a WHO_AM_I register. Either way I read a board ID at runtime from straps or a resistor divider and print it in the boot banner, so flashing the wrong image is loud instead of silent. And revision differences live in one data table, not ifdefs across the tree.

**Q. One device fails to initialize at boot. What should happen?**

**A.** 디바이스마다 다르고, 그 "다름"을 표로 정해 두는 게 BSP 설계다(6.3절). PMIC 실패는 중단 + 안전 모드, 콘솔 UART 실패는 무시하고 계속, 센서 실패는 기능 degraded로 앱에 통보. 모든 실패는 **재부팅 후에도 읽을 수 있는 곳**(retained RAM 또는 storage)에 남겨야 공장과 필드에서 판정할 수 있다.

> It depends on the device, and deciding that per device is the actual design work. A PMIC failure stops the boot and enters a safe mode. A console UART failure is ignored — losing logs must not brick the product. A sensor failure degrades a feature and is reported up to the app. In every case the failure is recorded somewhere that survives a reset, so the factory tester and field telemetry can act on it.

**Q. What's your honest experience with BSPs?** (정직 스크립트)

**A.** 제품 보드의 BSP를 오너로 만들어 본 적은 없다. 내가 한 건 그 아래층이다 — FPGA pre-silicon에서 Cortex-R8/R82/M0+를 처음 부팅시키고, I2C/SPI/DMA/SRAM/DRAM을 처음 동작시키고, Apple에서 새 무선 실리콘이 전체 시스템에 붙을 때 PCIe/I2C/SPMI/RFFE 장애를 root cause 했다. BSP에서 가장 어려운 부분 — 클럭·전원 시퀀스, 버스가 왜 안 움직이는지, 보드마다 왜 다른지 — 이 내가 매일 하던 일이다. 안 해 본 건 Zephyr 보드 디렉터리를 처음부터 작성하는 것과 리비전 매트릭스를 코드로 운영하는 것이고, 그건 지금 nRF52840 DK로 연습 중이다.

> I haven't owned a product BSP end to end, and I'd rather be precise about that. What I have done is the layer underneath: bringing up Cortex-R8, R82 and M0+ cores on FPGA before silicon, bringing up I2C, SPI, DMA and SRAM/DRAM from nothing, and at Apple root-causing PCIe, I2C, SPMI and RFFE failures when a new radio chip meets a full system. The hard parts of a BSP — clock and power sequencing, why a bus won't move, why five boards out of a hundred behave differently — are what I do every day. What I haven't done is author a Zephyr board directory from scratch or run a revision matrix in production, and that's exactly what I'm practicing now on an nRF52840 DK.

---

## 12. 직접 해보기

```sh
# 1) 부팅과 로그 (Zephyr 환경 준비는 B02 10절)
cd ~/zephyrproject/zephyr
west build -p auto -b nrf52840dk/nrf52840 samples/hello_world && west flash

# 2) 메모리 맵 확인 — 링커가 의도대로 동작했는지
arm-none-eabi-size build/zephyr/zephyr.elf
grep -nE '_sdata|_edata|_sbss|_ebss|__data|__bss' build/zephyr/zephyr.map | head
west build -t ram_report && west build -t rom_report

# 3) flash 파티션을 최종 devicetree에서 확인 (부트로더 설정과 일치하는가)
grep -A 30 'partitions' build/zephyr/zephyr.dts
```

- **실습 A (실패 경로)**: 크리스털 ready 대기 타임아웃 값을 1로 줄여 degraded 경로가 실제로 동작하고 로그가 남는지 확인한다. "실패 경로는 실행해 본 적 없으면 동작하지 않는다"를 몸으로 익히는 게 목적이다.
- **실습 B (리비전 흉내)**: 앱 디렉터리에 `boards/nrf52840dk_nrf52840.overlay`를 만들어 LED 핀을 옮긴다. 같은 소스가 오버레이만으로 다른 보드처럼 동작하면 그게 Zephyr 리비전 관리의 축소판이다(정식 문법은 B02 8절). 이어서 부팅 시 I2C 스캔 결과를 비트마스크로 출력하는 POST를 붙여 보면 공장 테스트 답변의 실물 근거가 된다.

---

## 13. 요약 & 체크리스트

BSP는 "보드에 대한 사실을 한곳에 가두는 층"이고, 그 층은 네 덩어리 — 부트 코드, 클럭·전원 순서, 핀과 메모리 레이아웃, 디바이스 초기화 순서와 실패 정책 — 로 이루어진다. 각 덩어리는 틀렸을 때 나오는 고유한 증상이 있고, BSP 오너의 실력은 새 기능을 만드는 속도가 아니라 **증상에서 층을 역추적하는 속도**로 드러난다. 제품 관점에서 최종 산출물은 코드가 아니라 "이 보드가 정상인지 스스로 말하게 만든 것"(POST, 부팅 배너, 전력 기준선, 리비전 매트릭스)이다.

- [ ] BSP의 경계(포함/불포함)를 표로 5개 이상 말하고, reset → main 경로와 각 단계의 실패 증상을 그릴 수 있다
- [ ] 클럭 bring-up 순서를 말하고 wait state가 왜 먼저인지 설명할 수 있다
- [ ] 전원 시퀀스 타이밍도를 그리고 무엇을 스코프로 확인할지 말할 수 있다
- [ ] 부팅 중 핀 플로팅이 왜 문제인지 세 가지 예를 들 수 있다
- [ ] LMA/VMA, `.data` 복사, DMA·retained 섹션 분리를 설명할 수 있다
- [ ] 디바이스 의존성 그래프를 그리고 실패 정책 표를 즉석에서 만들 수 있다
- [ ] 보드 리비전 관리 3가지 방식을 비교하고, EVT 보드 첫 전원 인가 8단계를 말할 수 있다
- [ ] 내 경험이 이 지도의 어디인지(아래 절반) 과장 없이 한 문단으로 말할 수 있다

---

## 참고 자료

| 자료 | 무엇을 확인했나 | URL |
|---|---|---|
| Zephyr — Board Porting Guide | 보드 디렉터리 필수 파일, 보드 qualifier 이름 규칙, 리비전 포맷 (2026-09-21 확인) | https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html |
| Zephyr — System initialization (`SYS_INIT`) | 초기화 레벨 6종(EARLY/PRE_KERNEL_1/PRE_KERNEL_2/POST_KERNEL/APPLICATION/SMP), 우선순위 0~999 (2026-09-21 확인) | https://docs.zephyrproject.org/latest/doxygen/html/group__sys__init.html |
| Zephyr — Device Driver Model | 초기화 레벨의 의미, init 함수는 성공 시 0 / 실패 시 errno 코드 반환 (2026-09-21 확인) | https://docs.zephyrproject.org/latest/kernel/drivers/index.html |
| Zephyr — Devicetree 문법과 구조 | `chosen`, `aliases`, `status`, phandle 등 보드 기술 문법 (2026-09-21 확인) | https://docs.zephyrproject.org/latest/build/dts/intro-syntax-structure.html |
| Zephyr — west build/flash/debug | `west build -p auto`, `-t ram_report` / `rom_report` (2026-09-21 확인) | https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html |
| GNU ld 매뉴얼 — Scripts | `MEMORY`, `AT >`, `KEEP`, `ASSERT` 문법 | https://sourceware.org/binutils/docs/ld/Scripts.html |
| ARM Developer 문서 | 벡터 테이블 구조, VTOR, 예외 진입은 각 코어 TRM 참조 | https://developer.arm.com/documentation |
| NXP UM10204 · MCUboot 문서 | 풀업·속도 모드 관계(C03), 부트로더 슬롯·서명 개념(C07) | https://www.nxp.com/docs/en/user-guide/UM10204.pdf · https://docs.mcuboot.com/ |

> **버전 주의**: 위 Zephyr 문서는 `latest`(2026-09-21 시점) 기준이다. Zephyr는 hardware model v2로 보드 구조가 크게 바뀌었으므로 회사 코드베이스의 Zephyr 버전을 먼저 확인하고 그 버전 문서를 봐야 한다. 벤더 SDK(Qualcomm, Ambiq)의 BSP 구조는 NDA·포털 배포라 공개 URL로 확인할 수 없어 이 노트에서는 다루지 않았다. 레지스터 이름과 주소가 붙은 예시는 전부 `[예시]`로 표기한 가상의 값이다.
