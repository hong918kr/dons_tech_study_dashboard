# C05. 저전력 설계와 발열 — always-on 배터리 기기에서 µA를 잡고 온도를 지키는 법

> **이 노트를 다 읽으면**: 전력이 어디서 새는지 물리 수준에서 설명할 수 있다 · Cortex-M sleep(WFI/WFE, SLEEPDEEP, SLEEPONEXIT)과 FreeRTOS tickless / Zephyr PM 코드를 읽고 쓸 수 있다 · 전류 프로파일로 배터리 수명을 계산하고 PPK2로 검증할 수 있다 · 발열 throttling과 PMIC/fuel gauge 역할을 설명할 수 있다
> **JD 연결**: "Optimize power consumption and thermal performance for always-on, battery-powered devices" / 우대: "Experience optimizing power on battery-powered consumer devices"
> **Don 기준 난이도**: 레지스터·클럭·Power Analyzer 사용은 이미 안다 / RTOS와 엮인 sleep 정책, µA 단위 누설 사냥, 배터리 수명 산정, 열 관리 정책은 새로 배운다

---

## 0. 큰 그림

SSD 펌웨어에서 전력은 "성능을 얼마나 깎지 않고 PS0→PS3/PS4(NVMe power state)로 내려가나"의 문제였다. 벽 전원이 있고, 몇 W 단위였다. 웨어러블급 always-on 기기는 단위가 다르다. **평균 전류를 µA 단위로** 본다. 배터리가 300~500 mAh 정도라면, 평균 1 mA 차이가 하루 24 mAh, 즉 배터리 수명의 5~8%다.

예를 들어 Hark 같은 기기(추정 구조: 큰 SoC + 상시 켜진 저전력 MCU)라면 전원 도메인은 대략 이렇게 나뉜다.

```
                     ┌──────────────────────────── PMIC ─────────────────────────────┐
 USB-C / 무선충전 ──▶ │ Charger(CC/CV, JEITA) ── Li-ion 3.0~4.4V ── Fuel gauge        │
                     │   │                                                           │
                     │   ├─ Buck1 (SoC core, DVFS)      ──▶ SoC (Cortex-A, Android)  │  대부분 꺼져 있어야 함
                     │   ├─ Buck2 (DDR / IO)            ──▶ LPDDR, 모뎀, Wi-Fi/BT      │
                     │   ├─ Buck3 / LDO (always-on 1.8V)──▶ MCU + 센서 + 마이크        │  항상 켜짐 (µA 예산)
                     │   └─ Load switch                 ──▶ 디스플레이/햅틱/카메라     │
                     └───────────────────────────────────────────────────────────────┘

  always-on 도메인 (µA ~ 수백 µA)            on-demand 도메인 (수십 ~ 수천 mA)
  ┌────────────────────────────┐   IRQ/GPIO  ┌────────────────────────────────┐
  │ MCU (Cortex-M, RTOS)        │ ──────────▶ │ SoC: 음성 세션, 네트워크, 앱     │
  │  ├ IMU FIFO + wake-on-motion│  SPI/UART   │  → 일 끝나면 다시 suspend        │
  │  ├ PDM mic + VAD/wake word  │ ◀────────── │                                │
  │  ├ BLE (폰 연결 유지)        │    IPC      └────────────────────────────────┘
  │  └ 배터리/온도 모니터        │
  └────────────────────────────┘
```

핵심 원리는 한 문장이다: **"가장 싼 도메인이 가장 오래 깨어 있고, 비싼 도메인은 꼭 필요할 때만 깨운다."** 저전력 펌웨어는 결국 세 가지를 반복한다.

1. 할 일이 없으면 가능한 한 깊이 잔다 (sleep state 선택).
2. 깨어나는 이유(wake source)를 하드웨어가 걸러 주게 한다 (FIFO, threshold, VAD, 비교기).
3. 깨어 있는 시간을 짧게, 깨어 있을 때는 빠르게 끝낸다 (race-to-idle, DMA, batching).

---

## 1. 전력의 물리 — 왜 전류가 흐르나

### 1.1 동적 전력 (switching power)

CMOS 게이트가 0↔1로 바뀔 때마다 부하 커패시턴스를 충전·방전한다. 그 에너지가 동적 전력이다.

```
P_dyn = α · C · V² · f

 α : activity factor (한 클럭에 실제로 토글하는 비율, 0~1)
 C : 스위칭되는 총 커패시턴스 (게이트 수·배선 길이에 비례)
 V : 공급 전압
 f : 클럭 주파수
```

여기서 읽어야 할 것:

- **V가 제곱**이다. 전압을 1.2 V → 0.9 V로 낮추면 동적 전력은 (0.9/1.2)² = 0.56배. 그래서 DVFS가 효과가 크다.
- **f에 비례**한다. 클럭을 반으로 줄이면 전력(W)은 반이 되지만, 같은 일을 하는 데 시간은 두 배 걸린다. 그래서 **에너지(J = W × s)는 f만 줄여서는 거의 안 줄어든다.** 에너지를 줄이려면 V를 같이 내려야 한다.
- **α와 C는 clock gating으로** 줄인다. 쓰지 않는 블록의 클럭을 끊으면 그 블록의 α가 0이 된다.

### 1.2 정적 전력 (leakage)

트랜지스터가 꺼져 있어도 subthreshold leakage, gate leakage, junction leakage로 전류가 샌다. 특징:

- 클럭과 무관하다. **전원이 붙어 있는 한** 흐른다. 그래서 clock gating으로는 못 줄이고 **power gating(전원 자체를 끊기)**이 필요하다.
- **온도에 지수적으로** 증가한다. 대략 "10°C 오를 때마다 크게(흔히 두 배 가까이) 는다"는 경험칙이 있지만 공정마다 다르다. 이것이 발열과 전력이 서로를 키우는 **thermal runaway 고리**의 원인이다.
- 공정이 미세할수록(고성능 공정) leakage가 크다. 그래서 always-on MCU는 일부러 누설이 작은 공정/설계를 쓴다. Ambiq Apollo 계열은 subthreshold/near-threshold 전압 동작으로 µA/MHz를 크게 낮춘 사례로 유명하다(수치는 세대별 데이터시트 참조).
- SRAM도 leakage가 있다. **RAM retention**(sleep 중 RAM 내용을 유지)을 켜면 뱅크당 전류가 더해진다. 필요한 뱅크만 retention하는 것이 µA 튜닝의 흔한 수단이다.

### 1.3 전류 관점으로 다시 보기

배터리 기기에서는 W보다 **전류(A)와 전하(C = A·s, mAh)**로 생각하는 게 편하다. 배터리 용량이 mAh로 표시되기 때문이다. 단, 레귤레이터가 끼면 전압 변환이 있으므로:

```
 배터리 전류 ≈ (부하 전압 × 부하 전류) / (배터리 전압 × 레귤레이터 효율)      ← buck(스위칭)
 배터리 전류 ≈ 부하 전류 + LDO 자체 Iq                                         ← LDO(선형)
```

- **LDO**는 입력 전류 ≈ 출력 전류다. 3.8 V 배터리에서 1.8 V를 LDO로 만들면 절반 이상이 열로 사라진다. 대신 노이즈가 적고 Iq(quiescent current)가 작은 제품이 많다.
- **Buck**은 효율이 높지만(중부하에서 흔히 85~95%), **경부하(µA)에서 효율이 떨어진다**. 그래서 PMIC는 경부하용 PFM/burst 모드를 둔다. nRF52 계열도 내부에 LDO와 DC/DC를 둘 다 갖고 있고, 펌웨어가 DC/DC를 켜 주면 라디오 동작 중 전류가 눈에 띄게 줄어든다(Zephyr에서는 보드 설정/devicetree로 켠다, 버전마다 방식이 다름).

> **Don 경험과 연결**: SSD에서 NVMe power state를 내릴 때 "entry/exit latency와 절약 전력의 trade-off"를 표로 관리했을 것이다. MCU sleep state 선택도 똑같다. 다만 단위가 W/ms가 아니라 µA/µs다.

---

## 2. Cortex-M sleep 메커니즘

### 2.1 WFI, WFE, 그리고 SCR

Cortex-M 코어 입장에서 sleep 진입 명령은 두 개뿐이다.

| 명령 | 깨어나는 조건 | 주 용도 |
|---|---|---|
| `WFI` (Wait For Interrupt) | pending 인터럽트가 생기면 (PRIMASK로 마스크돼 있어도 깨어남, 핸들러 실행은 PRIMASK 해제 후) | RTOS idle, 일반 sleep |
| `WFE` (Wait For Event) | event register가 set돼 있으면 즉시 통과, 아니면 이벤트(SEV, 인터럽트, SEVONPEND 설정 시 pending 등)가 올 때까지 대기 | spin-lock 대기, 멀티코어 동기화, 짧은 대기 |

어떤 깊이로 잘지는 **System Control Register (SCB->SCR)**가 정한다.

| SCR 비트 | 이름 | 의미 |
|---|---|---|
| bit 1 | `SLEEPONEXIT` | 예외 핸들러에서 Thread mode로 돌아갈 때 main으로 복귀하지 않고 바로 다시 sleep |
| bit 2 | `SLEEPDEEP` | 0이면 sleep, 1이면 deep sleep 요청. **deep sleep이 실제로 무엇을 끄는지는 칩 벤더가 정한다** |
| bit 4 | `SEVONPEND` | 1이면 마스크된 인터럽트가 pending 되는 것도 WFE의 wake event가 됨 |

중요한 점: 코어는 "sleep"과 "deep sleep" 두 신호만 SoC에 내보낸다. **실제 전원 상태(어떤 클럭이 꺼지고, 어떤 RAM이 유지되고, 어떤 주변장치가 살아 있는지)는 벤더의 Power Management Unit이 정한다.** STM32의 Stop/Standby, nRF의 System ON/OFF, Ambiq의 deep sleep이 모두 그 위에 올린 벤더 정의다.

```
 전류 ▲
      │ ██████                                    Run  (CPU + 클럭 + 주변장치)
      │ ██████
      │ ██████   ▄▄▄▄▄                            Sleep (WFI, SLEEPDEEP=0): CPU 클럭만 멈춤
      │ ██████   █████   ▂▂▂▂▂                    Deep sleep: 고속 클럭 OFF, 32 kHz + RTC만
      │ ██████   █████   █████   ▁▁▁▁▁            Deep sleep + RAM 일부만 retention
      │ ██████   █████   █████   █████   ___      System OFF / shutdown: RAM 소실, 리셋으로 깸
      └──────────────────────────────────────────▶
          수 mA    ~1 mA    수 µA     ~1~2 µA   <1 µA        (대략적인 자릿수, 칩마다 다름)
 wake     -        수 µs    수십 µs   수십~수백 µs  리셋 수준(ms)
 latency
```

### 2.2 가장 기본적인 sleep 코드 (bare-metal, CMSIS)

```c
#include <stdint.h>
#include "device.h"   /* 벤더 CMSIS 디바이스 헤더 (예: nrf52840.h, stm32l4xx.h) */

static void enter_deep_sleep(void)
{
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;   /* (1) deep sleep 요청 */
    __DSB();                              /* (2) 앞선 메모리 쓰기(레지스터 설정 포함) 완료 보장 */
    __WFI();                              /* (3) 인터럽트가 올 때까지 잔다 */
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;   /* (4) 깬 뒤 일반 sleep으로 복귀 */
}
```

- (1) `SCB_SCR_SLEEPDEEP_Msk`는 CMSIS가 정의한 `(1UL << 2)`다.
- (2) 주변장치 끄기 같은 레지스터 쓰기가 write buffer에 남아 있는 상태로 자면 안 되므로 `__DSB()`로 비운다. ARM은 WFI 앞에 DSB를 권장한다.
- (3) 여기서 코어가 멈춘다. 인터럽트가 pending 되면 깨어나서 (PRIMASK가 0이면) 핸들러를 먼저 실행하고 다음 줄로 온다.
- (4) 벤더에 따라 deep sleep 복귀 후 고속 클럭(HFXO, PLL)을 다시 켜야 하는 경우가 있다. STM32 Stop 모드가 대표적으로, 깨어나면 시스템 클럭이 기본 내부 오실레이터로 돌아와 있어 PLL을 재설정해야 한다.

### 2.3 레이스 없는 sleep: "확인하고 자기"의 함정

가장 흔한 버그는 이것이다.

```c
if (!event_pending) {   /* (A) 확인 */
    __WFI();            /* (B) 잠 */
}
```

(A)와 (B) 사이에 인터럽트가 와서 `event_pending = 1`로 만들고 끝나면, 코어는 그 사실을 모른 채 잠들어 다음 인터럽트까지 이벤트 처리가 밀린다. 해결은 **인터럽트를 막고 확인한 뒤 WFI** 하는 것이다. Cortex-M에서 PRIMASK=1이어도 WFI는 pending 인터럽트로 깨어나기 때문에 가능하다.

```c
#include <stdbool.h>
#include "device.h"

extern volatile bool event_pending;   /* ISR이 true로 설정 */

void idle_once(void)
{
    __disable_irq();          /* PRIMASK = 1 : 핸들러 실행 보류 */
    if (!event_pending) {
        __DSB();
        __WFI();              /* pending IRQ가 있으면 즉시 반환, 없으면 잠 */
    }
    __enable_irq();           /* PRIMASK = 0 : 보류된 핸들러가 여기서 실행됨 */
}
```

RTOS의 idle/tickless 구현이 전부 이 패턴을 쓴다(4절).

### 2.4 SLEEPONEXIT — 인터럽트 구동형 기기

메인 루프가 할 일이 없고 모든 일이 ISR에서 끝나는 설계라면, ISR이 끝날 때마다 Thread mode로 돌아와 다시 WFI를 부르는 것도 낭비다(스택 unstack/stack 비용).

```c
int main(void)
{
    board_init();
    SCB->SCR |= SCB_SCR_SLEEPONEXIT_Msk;  /* ISR 종료 → 곧바로 sleep */
    __DSB();
    __WFI();                               /* 첫 sleep. 이후로는 main에 돌아오지 않음 */
    for (;;) { }                           /* SLEEPONEXIT를 해제하면 여기로 */
}
```

RTOS를 쓰면 이 방식은 거의 안 쓴다(스케줄러가 idle을 관리). 하지만 초소형 센서 노드나 부트로더, 인터뷰 질문에서 나온다.

### 2.5 WFE와 이벤트 레지스터

WFE는 1비트 event register를 본다. SEV 명령, 예외 진입/복귀, (SEVONPEND=1이면) 새 pending 인터럽트가 이 비트를 set 한다. WFE는 비트가 set돼 있으면 **비트를 지우고 즉시 통과**한다. 그래서 "flag 확인 → WFE" 사이에 이벤트가 와도 놓치지 않는다. 대신 한 번은 헛돌 수 있어서 루프로 감싼다.

```c
while (!flag_ready) {
    __WFE();      /* 이벤트 올 때까지 저전력 대기. spurious wake 가능 → while로 재확인 */
}
```

### 2.6 Wake source 설계

깨어나는 원인은 하드웨어가 제공하는 것만 가능하다. deep sleep에서도 살아 있는 블록이 무엇인지 데이터시트의 "power domain" 표를 꼭 확인한다.

| Wake source | 예 | 비고 |
|---|---|---|
| RTC / low-power timer | 32.768 kHz 기반 RTC compare | RTOS tick 대체, 주기 작업 |
| GPIO (edge/level, sense) | 버튼, IMU INT 핀, PMIC IRQ, SoC→MCU 요청 | System OFF에서도 level sense로 깨는 칩이 많음(nRF52의 GPIO SENSE) |
| 비교기 (LPCOMP) | 아날로그 임계 넘으면 wake | 배터리 전압 급강하, 아날로그 센서 |
| 통신 주변장치 address match | I2C/SPI slave address match, UART start bit | 칩마다 지원 여부 다름 |
| 라디오 | BLE 스택의 connection event 타이머 | 사실상 RTC 기반 |
| 오디오 VAD | PDM 마이크/코덱의 voice activity detect 출력 | 오디오를 MCU가 계속 처리하지 않게 |

**좋은 wake source 설계 원칙**: 센서가 스스로 판단해서 "의미 있는 일"이 있을 때만 MCU를 깨우게 한다. 예: IMU FIFO watermark(샘플 32개 차면 INT), wake-on-motion threshold, 마이크 VAD.

---

## 3. 클럭/전원 제어 기법

### 3.1 기법 한눈에 보기

| 기법 | 무엇을 끄나 | 줄이는 것 | 비용 |
|---|---|---|---|
| Clock gating | 블록의 클럭 | 동적 전력 (α) | 거의 없음, 즉시 복귀 |
| Power gating | 블록의 전원 | 동적 + leakage | 상태 소실(재초기화 필요), 복귀 지연 |
| State retention | 전원은 끄되 retention flop/RAM만 유지 | 대부분의 leakage | retention 전류, 설계 복잡도 |
| DVFS | 전압과 주파수를 같이 낮춤 | V² 효과 | 전환 시간, 레귤레이터 settle |
| 클럭 소스 전환 | HFXO/PLL → 내부 RC 또는 32 kHz | 오실레이터 자체 전류 | 정확도 저하, 시작 시간 |
| Duty cycling | 주변장치/라디오를 주기적으로 on/off | 평균 전류 | 지연, 놓치는 이벤트 |

### 3.2 DVFS

SoC(Cortex-A)에서는 Linux cpufreq가 OPP(operating performance point: 주파수-전압 쌍) 표를 두고 governor(schedutil 등)가 부하에 따라 고른다. MCU에서는 DVFS가 단순하다. 보통 두세 개의 "performance level"(예: 고속 모드 1.1 V / 저속 모드 0.9 V)을 펌웨어가 명시적으로 바꾼다.

전환 순서가 중요하다.

```
 주파수 올릴 때 :  전압 먼저 올림 → settle 대기 → 주파수 올림
 주파수 내릴 때 :  주파수 먼저 내림 → 전압 내림
 (반대로 하면 잠깐이라도 "높은 f + 낮은 V" 구간이 생겨 타이밍 위반 → 오동작)
```

### 3.3 Race-to-idle vs 느리게 오래

"빠르게 끝내고 깊이 자기(race-to-idle)"가 거의 항상 이긴다. 이유는 **고정 비용(baseline)**이다. 깨어 있는 동안 CPU 외에도 고속 오실레이터, 레귤레이터 모드, 버스, flash가 전류를 먹는다. 느리게 돌리면 그 고정 비용을 오래 낸다. 예외는 DVFS로 전압까지 크게 내려가는 경우와, 작업이 I/O 대기로 묶여서 빨리 돌려도 시간이 줄지 않는 경우다.

```
 race-to-idle                         느리게 오래
 전류 ▲                                전류 ▲
  8mA │██                               3mA │████████
      │██                                   │████████
      │██▁▁▁▁▁▁▁▁▁▁  (2µA sleep)            │████████▁▁▁▁
      └──────────────▶ t                    └──────────────▶ t
       1ms                                   4ms
  Q = 8mA×1ms = 8 µC                    Q = 3mA×4ms = 12 µC   ← baseline 때문에 더 큼
```

### 3.4 주변장치 전력 관리의 실제

- **DMA를 쓰면 CPU가 잘 수 있다.** 예: UART 수신을 바이트마다 인터럽트로 받으면 코어가 계속 깨어난다. DMA + idle line 감지(또는 타임아웃)로 받으면 패킷당 한 번만 깬다. SSD 데이터 경로에서 DMA descriptor를 쓰던 것과 같은 논리다.
- **UART 수신기는 비싸다.** nRF52의 UARTE는 RX가 켜져 있으면 고속 클럭을 붙잡고 있어 sleep 전류가 수백 µA로 뛴다. 로그용 UART를 켜 두고 "sleep 전류가 이상하게 높다"는 것이 가장 흔한 첫 실수다.
- **센서는 자체 저전력 모드로.** IMU는 low-power ODR + FIFO batching, 환경 센서는 one-shot 모드, SPI NOR flash는 Deep Power-Down 명령(많은 제품에서 0xB9, 해제 0xAB; 벤더마다 확인)을 쓴다.
- **GPIO를 방치하지 않는다.** floating 입력은 입력 버퍼가 중간 전압에서 관통 전류를 흘린다. 쓰지 않는 핀은 데이터시트 권장대로(보통 입력 disconnect 또는 pull 고정) 둔다.

---

## 4. RTOS와 sleep — tickless idle과 PM 프레임워크

### 4.1 왜 tickless가 필요한가

일반 RTOS는 SysTick을 1 kHz로 돌린다(`configTICK_RATE_HZ = 1000`). 할 일이 없어도 1 ms마다 깨어나 tick을 센다. 깨어나는 것 자체가 수 µA~수십 µA 평균 전류를 만든다. 게다가 SysTick은 코어 클럭으로 돌기 때문에 deep sleep(고속 클럭 OFF)에서는 멈춘다.

**tickless idle**: idle task가 "다음에 깨어날 일(가장 가까운 timeout)까지 몇 tick인가"를 계산하고, 그만큼 저전력 타이머(RTC, LPTIM)를 걸고 잔다. 깨어나면 실제로 잔 만큼 tick count를 한꺼번에 보정한다.

```
 tick 방식:     |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  (1ms마다 깸)
 tickless:      |                                            |     (다음 timeout 때만 깸)
                ^ idle 진입: "다음 task 깨울 시각 = +45 tick"   ^ RTC compare → vTaskStepTick(45)
```

### 4.2 FreeRTOS tickless 설정과 hook

```c
/* FreeRTOSConfig.h (발췌) */
#define configUSE_TICKLESS_IDLE                 1   /* 1: 포트 기본 구현(SysTick 기반), 2: 직접 구현 */
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   5   /* 5 tick보다 짧은 idle이면 tickless 진입 안 함 */

#ifndef __ASSEMBLER__
#include <stdint.h>
void app_pre_sleep(uint32_t *expected_idle_ticks);
void app_post_sleep(uint32_t *expected_idle_ticks);
#endif
#define configPRE_SLEEP_PROCESSING(x)    app_pre_sleep(&(x))
#define configPOST_SLEEP_PROCESSING(x)   app_post_sleep(&(x))
```

- `configUSE_TICKLESS_IDLE 1`이면 Cortex-M 포트(`port.c`)에 들어 있는 기본 `vPortSuppressTicksAndSleep()`가 쓰인다. 이 기본 구현은 SysTick reload 값을 늘려서 오래 자는 방식이라 **SysTick이 멈추는 deep sleep에는 부적합**하다. 칩 벤더 SDK나 직접 구현(`= 2`)으로 RTC 기반 구현을 쓰는 게 보통이다.
- `configEXPECTED_IDLE_TIME_BEFORE_SLEEP`는 최소 2 이상이어야 한다. sleep 진입/복귀 오버헤드보다 짧은 idle에 들어가면 오히려 손해다.
- `configPRE_SLEEP_PROCESSING(x)`에서 x를 0으로 만들면 포트 구현이 WFI를 건너뛴다. 즉 "지금은 자면 안 된다(DMA 진행 중 등)"를 여기서 표현할 수 있다.
- FreeRTOSConfig.h는 일부 포트에서 어셈블리 파일에도 include되므로 C 선언을 `__ASSEMBLER__` 가드로 감쌌다(툴체인에 따라 매크로 이름 다름).

```c
/* app_power.c */
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"

extern volatile bool dma_busy;          /* 오디오 DMA 진행 여부 (가상 예시) */
void bsp_peripherals_suspend(void);     /* 가상 BSP 함수 */
void bsp_peripherals_resume(void);

void app_pre_sleep(uint32_t *expected_idle_ticks)
{
    if (dma_busy) {
        *expected_idle_ticks = 0;       /* (1) 0이면 포트가 WFI를 생략 */
        return;
    }
    bsp_peripherals_suspend();          /* (2) UART RX off, 센서 클럭 off 등 */
}

void app_post_sleep(uint32_t *expected_idle_ticks)
{
    (void)expected_idle_ticks;
    bsp_peripherals_resume();           /* (3) 깨어난 직후, 스케줄러 재개 전 */
}
```

### 4.3 RTC 기반 `vPortSuppressTicksAndSleep` 뼈대 (`configUSE_TICKLESS_IDLE 2`)

```c
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "device.h"

/* 가상 BSP: 32.768 kHz RTC를 RTOS tick 단위로 다루는 함수들 */
void     lptick_stop_periodic(void);
void     lptick_start_periodic(void);
void     lptick_arm_wakeup(uint32_t ticks);
uint32_t lptick_elapsed_ticks(void);
#define  LPTICK_MAX_SLEEP_TICKS  (60000u)

void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime)
{
    TickType_t ticks = xExpectedIdleTime;
    if (ticks > LPTICK_MAX_SLEEP_TICKS) {
        ticks = LPTICK_MAX_SLEEP_TICKS;                 /* (1) 타이머 폭 제한 */
    }

    __disable_irq();                                    /* (2) 레이스 방지: 확인~WFI 사이 보호 */
    __DSB();
    __ISB();

    if (eTaskConfirmSleepModeStatus() == eAbortSleep) { /* (3) 그 사이 task가 ready 됐나? */
        __enable_irq();
        return;
    }

    lptick_stop_periodic();                             /* (4) 주기 tick 중단 */
    lptick_arm_wakeup(ticks);                           /* (5) ticks 뒤에 RTC compare 인터럽트 */

    TickType_t x = ticks;
    configPRE_SLEEP_PROCESSING(x);
    if (x > 0) {
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        __DSB();
        __WFI();                                        /* (6) RTC 또는 다른 IRQ로 깸 */
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    }
    configPOST_SLEEP_PROCESSING(x);

    uint32_t slept = lptick_elapsed_ticks();            /* (7) 실제로 잔 tick 수 */
    if (slept > ticks) {
        slept = ticks;
    }
    vTaskStepTick(slept);                               /* (8) 커널 tick count 보정 */

    lptick_start_periodic();                            /* (9) 주기 tick 재개 */
    __enable_irq();                                     /* (10) 보류된 IRQ 핸들러 실행 */
}
```

- (3) `eTaskConfirmSleepModeStatus()`는 `eAbortSleep`(인터럽트가 task를 깨웠거나 yield가 pending), `eStandardSleep`, `eNoTasksWaitingTimeout`(모든 task가 무기한 block — 외부 인터럽트로만 깰 수 있으니 더 깊은 모드 가능)을 돌려준다. 마지막 값을 쓰려면 `INCLUDE_vTaskSuspend`가 1이어야 한다.
- (6) 버튼 인터럽트로 일찍 깰 수 있다. 그래서 (7)에서 "예정 시간"이 아니라 "실제 경과"를 읽는다.
- (8) `vTaskStepTick()`에 너무 큰 값을 주면 assert가 걸린다(다음 unblock 시각을 넘으면 안 됨). 그래서 clamp 했다.
- 32.768 kHz와 1 kHz tick은 정수배가 아니다(32.768 tick/ms). 남는 분수를 누적 보정하지 않으면 시간이 조금씩 틀어진다. 실제 벤더 구현은 이 나머지를 추적한다.

### 4.4 Zephyr의 전원 관리

Zephyr는 두 층으로 나뉜다.

| 층 | 무엇 | 핵심 Kconfig / API |
|---|---|---|
| System PM | idle thread가 CPU/SoC 전원 상태를 고름 | `CONFIG_PM`, devicetree `power-states`, policy (`pm_policy_state_lock_get()`) |
| Device PM | 주변장치 드라이버를 suspend/resume | `CONFIG_PM_DEVICE`, `CONFIG_PM_DEVICE_RUNTIME`, `pm_device_runtime_get()/put()` |
| System off | 전원 대부분 차단, 리셋으로 깨어남 | `CONFIG_POWEROFF`, `sys_poweroff()` |

Zephyr 커널은 원래 tickless다(`CONFIG_TICKLESS_KERNEL`, 대부분 SoC에서 기본). idle thread는 다음 timeout까지의 시간을 보고, devicetree에 선언된 power state 중 `min-residency-us`(이 시간 이상 잘 때만 이득)와 `exit-latency-us`를 만족하는 가장 깊은 상태를 고른다.

```
/* devicetree 발췌 (SoC .dtsi에 보통 이미 있음; 값은 예시) */
cpus {
    cpu@0 {
        cpu-power-states = <&idle &stop>;
    };
    power-states {
        idle: idle {
            compatible = "zephyr,power-state";
            power-state-name = "suspend-to-idle";
            min-residency-us = <100>;
            exit-latency-us = <10>;
        };
        stop: stop {
            compatible = "zephyr,power-state";
            power-state-name = "standby";
            min-residency-us = <2000>;
            exit-latency-us = <300>;
        };
    };
};
```

상태 이름은 `enum pm_state`에 대응한다: `PM_STATE_ACTIVE`, `PM_STATE_RUNTIME_IDLE`, `PM_STATE_SUSPEND_TO_IDLE`, `PM_STATE_STANDBY`, `PM_STATE_SUSPEND_TO_RAM`, `PM_STATE_SUSPEND_TO_DISK`, `PM_STATE_SOFT_OFF`. 각 상태에서 실제로 무엇을 끄는지는 SoC 포팅 코드(`pm_state_set()`)가 구현한다. nRF52는 System ON 안에서 자동으로 필요한 클럭만 켜는 구조라 SoC 쪽 상태 목록이 짧고, STM32 같은 칩은 Stop 모드들이 power state로 노출된다.

**오디오 캡처 중 깊은 sleep 금지** 같은 제약은 policy lock으로 건다.

```c
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>

void audio_capture_start(void)
{
    /* STANDBY 이상 깊은 상태로 못 가게 잠금: DMA 클럭이 꺼지면 안 되므로 */
    pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
    /* ... I2S/PDM DMA 시작 ... */
}

void audio_capture_stop(void)
{
    /* ... DMA 정지 ... */
    pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);  /* get/put은 반드시 짝 */
}
```

(옛 Zephyr 버전은 `pm_constraint_set()`이라는 이름을 썼다. 버전에 따라 API 이름이 바뀌므로 쓰는 버전의 문서를 확인한다.)

### 4.5 Zephyr Device runtime PM — 드라이버 쪽

```c
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

/* 가상 센서 드라이버의 PM action 콜백 */
static int mysensor_pm_action(const struct device *dev, enum pm_device_action action)
{
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        /* 센서에 sleep 명령 전송, 버스 핀 low-power 상태로 */
        return 0;
    case PM_DEVICE_ACTION_RESUME:
        /* 센서 wake 명령, 레지스터 재설정 */
        return 0;
    default:
        return -ENOTSUP;
    }
}

/* 드라이버 정의부에서 (인스턴스 0 예시):
 * PM_DEVICE_DT_INST_DEFINE(0, mysensor_pm_action);
 * DEVICE_DT_INST_DEFINE(0, mysensor_init, PM_DEVICE_DT_INST_GET(0),
 *                       &data0, &cfg0, POST_KERNEL,
 *                       CONFIG_SENSOR_INIT_PRIORITY, &mysensor_api);
 */
```

사용하는 쪽(애플리케이션 또는 상위 드라이버)은 참조 카운트로 켜고 끈다.

```c
int read_once(const struct device *sensor)
{
    int ret = pm_device_runtime_get(sensor);   /* (1) usage count 0→1이면 RESUME 호출 */
    if (ret < 0) {
        return ret;
    }
    /* (2) sensor_sample_fetch(sensor); ... */
    return pm_device_runtime_put(sensor);      /* (3) 1→0이면 SUSPEND 호출 */
}
```

- 드라이버 init에서 `pm_device_runtime_enable(dev)`를 호출하거나 devicetree에 `zephyr,pm-device-runtime-auto;` 속성을 넣어 runtime PM을 켠다.
- `pm_device_runtime_put_async()`를 쓰면 바로 끄지 않고 약간 뒤에 끈다. 짧은 간격으로 반복 접근할 때 on/off 왕복 비용을 줄인다.
- **Linux runtime PM(`pm_runtime_get_sync/put`)과 개념이 같다.** Cortex-A BSP 쪽에서도 그대로 통한다.

### 4.6 System OFF로 가기 (nRF 예시)

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/poweroff.h>

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

void go_ship_mode(void)
{
    gpio_pin_configure_dt(&btn, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_LEVEL_ACTIVE); /* (1) nRF에선 SENSE로 매핑 → OFF에서 wake */
    sys_poweroff();                                               /* (2) 돌아오지 않음. 깨면 리셋부터 */
}
```

- (1) nRF52의 System OFF에서 깨울 수 있는 것은 GPIO DETECT(SENSE), NFC field, LPCOMP, reset 등이다. RTC로는 못 깬다(칩 데이터시트 확인).
- (2) 깨어나면 **리셋**이다. RAM 대부분이 날아가고 부트부터 다시 시작한다. 그래서 "잠시 대기"가 아니라 **ship mode / 사용자가 끈 상태**에 쓴다. 깨어난 원인은 RESETREAS 레지스터(nRF) 또는 Zephyr `hwinfo_get_reset_cause()`로 확인한다.
- Zephyr 샘플: `samples/boards/nordic/system_off` (옛 버전 경로는 `samples/boards/nrf/system_off`).

---

## 5. 라디오와 주변장치 duty cycling

라디오는 MCU 코어보다 수 배 비싸다(BLE 칩의 TX/RX는 대략 수 mA, Wi-Fi는 수십~수백 mA). 그래서 라디오 전력은 **얼마나 자주, 얼마나 오래 켜느냐**로 결정된다. 상세는 C06에서 다루고, 여기서는 전력 관점의 숫자 감각만 잡는다.

| 무선 | 저전력 수단 | 주기 파라미터 | 전형적 평균 전류 자릿수 |
|---|---|---|---|
| BLE advertising | adv interval 늘리기 | 20 ms ~ 10.24 s | 1 s 간격이면 µA 단위 |
| BLE connection | connection interval, peripheral latency | 7.5 ms ~ 4 s | 수 µA ~ 수백 µA |
| Wi-Fi STA | PS mode, DTIM listen, TWT | beacon 102.4 ms × DTIM | 연결 유지 수백 µA ~ mA (칩마다 큼) |
| Thread SED | data poll 주기 | 수백 ms ~ 수십 s | µA 단위 |
| Cellular | PSM, eDRX | 수 초 ~ 수 시간 | 모뎀·네트워크 설정 의존 |

펌웨어가 실제로 할 수 있는 것:

- **Batching**: 센서 데이터를 1초마다 보내지 말고 30초 모아서 한 번 보낸다. 라디오 켜는 고정 비용(오실레이터 기동, ramp-up, 프로토콜 overhead)을 나눠 낸다.
- **동적 파라미터**: 사용자와 상호작용할 때만 BLE connection interval을 15 ms로 줄이고, 평소엔 500 ms~1 s + peripheral latency로 둔다.
- **라디오 선택**: 작은 데이터는 BLE로 폰에 보내고, 큰 데이터(모델 업데이트, 오디오 업로드)만 Wi-Fi/Cellular를 깨운다.

---

## 6. Always-on sensor hub 구조

### 6.1 왜 MCU를 따로 두나

Android SoC는 깨어나면 수십~수백 mA, suspend 상태도 모뎀 idle과 DRAM self-refresh로 mA 단위다. 반면 always-on MCU는 대기 수 µA, 동작 중에도 수 mA 이하다. 그래서 "항상 들어야 하는 일"(움직임, 소리, 버튼, 배터리 감시)을 MCU가 맡고 SoC는 **정말 필요할 때만** 깨운다. 스마트폰의 sensor hub(예: Apple M 시리즈 모션 코프로세서, Android CHRE)가 같은 구조다.

```
 이벤트 흐름 (예: "Hey ___" 호출어)

 PDM mic ──▶ [VAD: 소리 있음?] ──no──▶ (MCU는 계속 잠)
                 │yes
                 ▼
         MCU 깨어남: 오디오 DMA 링버퍼에 쌓기 (pre-roll 1~2 s 보관)
                 │
                 ▼
         [wake word 모델 추론 (INT8, CMSIS-NN / NPU)] ──no──▶ 다시 sleep
                 │yes
                 ▼
         GPIO로 SoC wake 요청 ──▶ SoC resume (수백 ms) ──▶ IPC로 pre-roll 오디오 전달
                                                          ──▶ 음성 세션 / 클라우드
```

### 6.2 설계 포인트

- **Pre-roll 버퍼**: SoC가 깨어나는 동안(수백 ms) 사용자는 계속 말한다. MCU가 호출어 앞뒤 오디오를 링버퍼에 보관했다가 넘긴다. SRAM 예산: 16 kHz × 16 bit × 2 s = 64 KB. 작지 않다.
- **Wake 경로의 오탐 비용**: wake word 오탐 1회 = SoC resume 1회 = 수 mAh 단위 손실도 가능. 그래서 MCU 1차 검출 → SoC 2차 검증 같은 **cascade** 구조가 흔하다.
- **IPC 설계**: SPI(MCU master 또는 slave) + 인터럽트 라인 2개(MCU→SoC wake, SoC→MCU ready)가 흔하다. SoC가 suspend 상태일 때 MCU가 보내는 메시지를 어떻게 버퍼링하고 순서를 보장하는지가 면접 설계 문제로 나온다.
- **센서 batching**: IMU는 FIFO에 샘플을 쌓고 watermark에서 INT. MCU는 FIFO를 SPI/I2C DMA 한 번에 읽고 다시 잔다.

> **Don 경험과 연결**: SSD 컨트롤러에서 Cortex-M0+ 같은 작은 코어가 전원/온도 관리를 맡고 큰 코어가 데이터 경로를 맡는 구조를 봤을 것이다. always-on MCU는 그 "관리 코어"를 기기 전체 수준으로 키운 것이다.

---

## 7. 배터리 수명 계산

### 7.1 기본 공식

```
 평균 전류  I_avg = Σ (I_k × t_k) / T          (T = 한 주기, k = 각 구간)
 배터리 수명 (h) = (용량 mAh × 사용 가능 비율) / I_avg (mA)
```

"사용 가능 비율"(derating)에 들어가는 것들:

- **Cut-off 전압**: 기기가 3.3 V에서 꺼진다면 그 아래 남은 용량은 못 쓴다.
- **온도**: 저온에서 내부 저항이 커져 전압이 빨리 떨어진다.
- **노화**: 수백 사이클 후 용량 80% 수준이 흔한 보증 기준.
- **자가 방전 + 보호 회로 전류**: Li-ion 팩의 보호 IC와 fuel gauge 자체도 µA 단위를 먹는다.
- **Peak 전류**: 코인셀(CR2032)은 큰 펄스 전류에 약해서 공칭 용량을 다 못 쓴다.

실무에서는 보수적으로 70~85%를 쓰고, 근거를 밝힌다.

### 7.2 예제 1 — 코인셀 BLE 센서

조건: CR2032 공칭 220 mAh(제조사마다 다름). 1초마다 깨어나 2 ms 동안 평균 5 mA(센서 읽기 + BLE advertising), 나머지는 3 µA로 잔다.

```
 한 주기 T = 1 s
 활성 전하  = 5 mA × 2 ms   = 10    µC   (mA × ms = µC)
 sleep 전하 = 3 µA × 998 ms = 2.994 µC   (µA × s = µC → 0.003 mA × 998 ms)
 합계       = 12.994 µC / 1 s  →  I_avg ≈ 13.0 µA

 수명 = 220 mAh / 0.0130 mA ≈ 16,900 h ≈ 705 일 ≈ 1.9 년
 derating 80% 적용 → 약 1.5 년
```

여기서 배울 점: **sleep 전류(3 µA)가 평균의 23%**다. 활성 구간을 아무리 줄여도 sleep 전류가 바닥을 정한다. 반대로 sleep 전류가 30 µA로 새면(로그 UART를 켜 둔 경우 등) 평균은 40 µA가 되어 수명이 1/3로 줄어든다.

### 7.3 예제 2 — SoC + MCU 웨어러블 (가상 예산)

아래 숫자는 **설명용 가상 값**이다. 실제 기기에서는 측정으로 채워야 한다.

| 항목 | 전류 | 하루 시간 | 하루 소비 (mAh) |
|---|---|---|---|
| MCU sleep + RTC + RAM retention | 5 µA | 24 h | 0.12 |
| IMU low-power + FIFO | 20 µA | 24 h | 0.48 |
| PDM mic + VAD | 150 µA | 24 h | 3.6 |
| MCU wake word 추론 (1 mA × 10% duty) | 100 µA 평균 | 24 h | 2.4 |
| BLE connection 유지 (폰) | 30 µA | 24 h | 0.72 |
| SoC suspend (DRAM self-refresh, 모뎀 idle) | 3 mA | 23 h | 69 |
| SoC active (음성 세션, 네트워크) | 250 mA | 1 h | 250 |
| **합계** | | | **약 326** |

배터리 500 mAh, 사용 가능 85%라면 425 mAh / 326 mAh ≈ **1.3일**.

이 표가 말해 주는 것:

- always-on MCU 도메인 전체(약 7.3 mAh/일)는 **2%**밖에 안 된다. MCU에서 µA를 더 깎아도 큰 차이가 없다.
- **SoC active 시간과 SoC suspend 전류가 지배적**이다. 가장 큰 레버는 "SoC를 덜 깨우기"(wake word 오탐 줄이기, MCU에서 처리 가능한 요청은 MCU가 처리) 그리고 "SoC suspend 전류 줄이기"(모뎀 PSM/eDRX, 불필요한 wakelock 제거)다.
- 면접에서 "전력을 줄여라" 질문을 받으면 **먼저 이런 예산표(power budget)를 만들고 가장 큰 항목부터 공략한다**고 답한다. 이게 시니어다운 답이다.

### 7.4 에너지 단위 변환 요령

| 변환 | 값 |
|---|---|
| 1 mAh | 3.6 C (쿨롱) |
| 1 µA 연속 1년 | 약 8.76 mAh |
| 1 mA 연속 1일 | 24 mAh |
| mA × ms | µC |
| Wh | mAh × 공칭 전압(V) / 1000 |

"1 µA 1년 ≈ 8.8 mAh"는 외워 두면 좋다. 코인셀 기기에서 누설 10 µA가 1년에 88 mAh, 즉 CR2032의 40%라는 계산이 바로 나온다.

---

## 8. 측정 — PPK2, Joulescope, DC power analyzer

### 8.1 왜 멀티미터로는 안 되나

전류가 3 µA(sleep)와 10 mA(라디오 TX)를 수 µs 단위로 오간다. 동적 범위 1:10,000 이상, 대역폭 수십 kHz 이상이 필요하다. 일반 멀티미터는 평균도 틀리게 잡고(샘플링이 느림), 레인지 전환 순간 **burden voltage**(션트 저항 전압 강하) 때문에 DUT가 brown-out 되기도 한다.

### 8.2 도구 비교

| 도구 | 특징 | 쓰는 곳 |
|---|---|---|
| Nordic Power Profiler Kit II (PPK2) | 저가(약 100달러대), source meter/ampere meter 모드, 자동 range 전환, 최대 100 kS/s, nRF Connect for Desktop의 Power Profiler 앱 | 개발자 책상, nRF DK 측정 |
| Joulescope (JS110/JS220) | 넓은 동적 범위, 빠른 range 전환, 에너지/전하 적산, 파이썬 API | 정밀 프로파일링, 자동화 |
| Qoitech Otii Arc/Ace | 전원+측정, 배터리 에뮬레이션, 스크립트 | 배터리 모델 기반 검증 |
| Keysight N6705 + N6781A 등 SMU | 랩 장비, seamless ranging, 높은 정확도 | 인증급 측정, 팩토리 상관 |
| 스코프 + 전류 프로브/션트 앰프 | 고대역 과도 현상 | inrush, 레귤레이터 과도 |

Don은 Apple에서 Power Analyzer를 썼으므로 원리(션트, 레인지, 적분)는 이미 안다. 새로 익힐 것은 **nRF DK 같은 개발 보드에서 DUT 전원만 분리해서 재는 방법**과 **프로파일 모양을 읽는 법**이다.

### 8.3 전류 프로파일 읽기

```
 전류 (mA, log축 느낌)
  10 ┤          ┌┐  ┌┐
     │          ││  ││           ← RX / TX (radio)
   5 ┤        ┌─┘└──┘└┐
     │        │       │
   1 ┤      ┌─┘       └──┐       ← CPU 처리 (스택, 앱 콜백)
     │      │             │
 0.5 ┤    ┌─┘             └┐     ← HFXO 기동 (크리스털 안정화 대기)
     │    │                │
0.003┤────┘                └──────────────────────  ← sleep floor (3 µA)
     └────┬──┬─┬──┬──┬─────┬──────────────────────▶ t
          0 0.4 0.6    1.2  1.5 ms
          wake ramp RX T_IFS TX  post
```

체크 포인트:

- **바닥(floor)이 데이터시트 값과 맞나?** 안 맞으면 누설 사냥(9절).
- **스파이크 사이 간격이 설정과 맞나?** connection interval 100 ms인데 10 ms마다 뭔가 깬다면 다른 타이머(로그 flush, 센서 polling)가 있다.
- **활성 구간 꼬리가 긴가?** 라디오가 끝났는데 1 mA가 수 ms 남아 있다면 CPU가 일을 오래 하거나 고속 클럭을 안 놓고 있다.
- **적분값(µC/event)**을 도구에서 바로 읽어 배터리 수명 계산에 넣는다.

### 8.4 측정 함정

- **디버거 연결**: SWD 디버거가 붙어 있거나 한 번이라도 debug session이 열리면 디버그 전원 도메인이 켜져 수백 µA~mA가 더해진다(nRF52는 debug interface 활성 시 전원을 끄지 않음). 측정 전 디버거를 떼고 **전원을 완전히 재인가**한다.
- **보드 주변 회로**: DK의 인터페이스 MCU, LED, 레벨 시프터가 같이 측정되지 않게 DUT 전원만 분리한다. nRF52840 DK는 전용 측정 헤더와 끊어야 할 solder bridge가 있다(DK 사용자 가이드의 current measurement 절 참조).
- **Burden voltage**: 션트 때문에 DUT 전압이 떨어진다. PPK2 source meter 모드처럼 전원 공급과 측정을 같은 장비가 하면 보정된다.
- **샘플링 aliasing**: 100 kS/s는 10 µs 해상도다. 수 µs 스파이크의 모양은 뭉개지지만 적분(전하)은 대체로 맞다.

---

## 9. 흔한 누설 원인 (µA 사냥 체크리스트)

| 누설 | 대략 크기 | 확인 방법 |
|---|---|---|
| Floating 입력 핀 | 핀당 수 µA ~ 수백 µA (중간 전압에서 관통 전류) | 모든 핀 설정 덤프, 쓰지 않는 핀 disconnect |
| Pull-up과 반대로 구동되는 핀 | 3.3 V / 10 kΩ = 330 µA | 회로도의 pull 저항과 GPIO 출력 상태 대조 |
| 배터리 전압 측정용 저항 분배기 | 4.2 V / 1 MΩ 합 = 4.2 µA (상시) | 분배기를 load switch/GPIO로 끊기 |
| 로그 UART RX 활성 | 수백 µA (고속 클럭 유지) | `CONFIG_SERIAL=n`, `CONFIG_LOG=n`로 비교 측정 |
| 디버거/trace 활성 | 수백 µA ~ mA | 전원 재인가 후 측정 |
| 고속 오실레이터(HFXO, PLL) 미해제 | 수백 µA | clock request 카운트 확인 |
| 외부 센서/flash가 sleep 명령 못 받음 | 수 µA ~ mA | 센서별 전류 분리 측정, 명령 로그 |
| LED/전원 표시 | mA | 양산 보드에서는 제거 또는 PWM |
| 레벨 시프터·I2C 버스 back-powering | 수십 µA ~ | 꺼진 도메인으로 IO 핀이 전류를 흘려 넣는지 확인 |
| RAM retention 과다 | 뱅크당 수백 nA ~ | 필요한 뱅크만 retention |

**Back-powering**은 특히 교묘하다. SoC 전원을 끈 상태에서 MCU가 SoC 쪽 GPIO를 high로 두면, 그 전류가 SoC IO의 ESD 다이오드를 통해 꺼진 전원 레일로 흘러 들어간다. 전류가 새는 것도 문제지만 SoC가 반쯤 켜진 상태가 되어 다음 부팅이 이상해질 수 있다. 전원 도메인이 다른 칩 사이의 신호는 **꺼진 쪽을 향해 low 또는 high-Z**로 두는 것이 원칙이다.

> **Don 경험과 연결**: Apple에서 SPMI/RFFE 라인과 전원 시퀀스 문제를 봤다면, back-powering은 익숙한 종류의 버그다. "전원 도메인 경계에서 IO 상태"라는 같은 질문이다.

### 9.1 누설 사냥 절차

1. **최소 펌웨어**로 바닥 측정: `main()`에서 모든 걸 끄고 System OFF 또는 가장 깊은 sleep. 데이터시트 값과 비교 → 보드 문제인지 FW 문제인지 분리.
2. **하나씩 켜기(bisect)**: 주변장치/드라이버를 한 개씩 활성화하며 floor가 튀는 지점을 찾는다. Kconfig로 기능을 끄고 켜기 쉬운 Zephyr가 편하다.
3. **보드 레벨 분리**: 0 Ω 저항/jumper로 레일별 전류 측정. 회로도에서 always-on 레일에 붙은 모든 부품을 나열한다.
4. **온도 반복**: 상온에서 괜찮아도 45°C에서 leakage가 크게 늘 수 있다.

---

## 10. 발열 관리

### 10.1 왜 웨어러블에서 발열이 중요한가

- **피부 접촉 온도(skin temperature)**: 몸에 닿는 기기는 표면 온도 한계가 있다. 안전 표준(IEC 62368-1의 접촉 온도 한계 등)과 사용자 편안함 기준으로 제품마다 목표를 정한다. 오래 닿는 웨어러블은 대략 40°C 초반을 목표로 하는 경우가 많지만 수치는 제품/규격 해석마다 다르다.
- **배터리 안전**: Li-ion 충전은 대략 0~45°C 범위에서만 허용하고, 그 근처에서는 충전 전류/전압을 줄인다(JEITA 가이드라인). 방전도 고온에서 제한한다.
- **성능**: SoC는 온도가 오르면 스스로 클럭을 낮춘다(throttling). 음성 세션 중 응답 지연으로 체감된다.

### 10.2 열의 물리 — 시간 상수

```
 온도 ▲
      │                          ___________  정상 상태 = 주변 + P × Rth
      │                    ____/
      │               __/
      │           _/
      │        /        ← 시상수 τ = Rth × Cth (작은 기기는 수십 초 ~ 수 분)
      │     /
      │  /
      └──────────────────────────────────▶ t
       SoC 부하 시작
```

- **Rth(열저항, °C/W)**: 같은 전력에서 얼마나 뜨거워지나. 기기가 작을수록 크다.
- **Cth(열용량)**: 얼마나 천천히 데워지나. 작은 기기는 작다.
- 결론: 짧은 버스트(수 초)는 열용량이 흡수하지만, 수 분짜리 지속 부하는 정상 상태 온도까지 간다. **"피크 전력"이 아니라 "지속 평균 전력"이 표면 온도를 정한다.** 그래서 throttling 정책은 순간값이 아니라 추세로 판단한다.

### 10.3 Throttling 구조

```
 온도 센서 (SoC 내부 TSENS, 보드 NTC, 배터리 NTC, 표면 추정)
        │
        ▼
 Thermal zone ── trip points (passive: 38°C, hot: 42°C, critical: 50°C 예시)
        │
        ▼
 Governor (step_wise, power_allocator(IPA) 등)
        │
        ▼
 Cooling devices: CPU/GPU freq cap, 모뎀 전송 전력 제한, 충전 전류 감소,
                  디스플레이 밝기, 기능 제한(카메라 끄기)
```

- **Linux thermal framework**가 SoC 쪽 표준 구조다: thermal zone(센서) ↔ trip point ↔ cooling device(cpufreq 등)를 devicetree로 묶는다.
- **Zephyr**에도 온도 센서는 sensor API(`SENSOR_CHAN_DIE_TEMP`, `SENSOR_CHAN_AMBIENT_TEMP`)로 읽을 수 있고, 정책은 보통 애플리케이션이 짠다.
- **Skin temperature는 직접 못 잰다.** 보드 NTC 여러 개의 가중합 + 모델로 추정하는 "virtual sensor"를 쓴다. 이 모델의 계수를 factory/DVT에서 열화상 카메라와 상관시켜 정한다(C09 calibration과 연결).
- **Hysteresis**: 42°C에서 throttle 걸고 41.9°C에서 바로 풀면 진동한다. 해제는 39°C처럼 간격을 둔다.

### 10.4 MCU 펌웨어가 하는 일 (예시)

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

#define THROTTLE_ON_MC    42000   /* milli-°C, 예시 값 */
#define THROTTLE_OFF_MC   39000

extern void soc_request_throttle(bool on);   /* 가상: IPC로 SoC에 알림 */
extern void charger_limit_current(bool on);  /* 가상: PMIC 충전 전류 제한 */

static bool throttled;

void thermal_poll(const struct device *ntc)
{
    struct sensor_value v;
    if (sensor_sample_fetch(ntc) < 0 ||
        sensor_channel_get(ntc, SENSOR_CHAN_AMBIENT_TEMP, &v) < 0) {
        return;                                          /* 센서 실패 시 정책 유지 */
    }
    int32_t mc = v.val1 * 1000 + v.val2 / 1000;          /* val2는 백만분의 1 단위 */

    if (!throttled && mc >= THROTTLE_ON_MC) {
        throttled = true;
        soc_request_throttle(true);
        charger_limit_current(true);
    } else if (throttled && mc <= THROTTLE_OFF_MC) {     /* hysteresis */
        throttled = false;
        soc_request_throttle(false);
        charger_limit_current(false);
    }
}
```

- `struct sensor_value`는 정수부 `val1`과 백만분의 1 단위 `val2`로 값을 담는다. 부동소수점 없이 milli-°C로 바꿨다.
- 센서 읽기 실패 시 "안전한 쪽"으로 갈지(throttle 유지) 결정해야 한다. 여기서는 현상 유지를 택했다. 제품에 따라 실패가 연속되면 fail-safe로 throttle을 거는 것이 맞다.

---

## 11. PMIC, 충전기, fuel gauge

### 11.1 PMIC가 하는 일

| 기능 | 설명 | 펌웨어 관점 |
|---|---|---|
| 레귤레이터 (Buck/LDO) | 레일별 전압 생성, 모드(PWM/PFM) | I2C로 전압·모드 설정, 시퀀스 |
| 전원 시퀀싱 | 레일 켜는 순서·간격 | OTP 기본값 + FW override. 순서 틀리면 latch-up, back-power |
| 충전기 | CC/CV 충전, 종료 전류, 타이머, JEITA 온도 구간 | 충전 상태 인터럽트, 전류 한도 설정 |
| Load switch | 주변 블록 전원 차단 | GPIO/I2C로 on/off |
| Power button / ship mode | 장시간 누름 리셋, 배터리 분리 수준 대기 | 출하 전 ship mode 진입 명령(C09) |
| Watchdog / reset | 외부 watchdog, 저전압 리셋 | MCU 멈춤 대비 |
| ADC / 인터럽트 | 배터리 전압, 온도(NTC), VBUS 감지 | IRQ 핀으로 MCU에 이벤트 |

Hark 관련 공고에 Qualcomm/TI/Nordic/Maxim PMIC가 언급된다. 예를 들어 Nordic nPM1300 같은 웨어러블용 PMIC는 충전기, buck, load switch, fuel gauge 지원이 한 칩에 있고 Zephyr에 드라이버(regulator, charger, mfd)가 들어 있다.

### 11.2 Li-ion 충전 곡선 (CC/CV)

```
 전압/전류 ▲
  4.2V ─ ─ ─ ─ ─ ─ ─ ─ ─ ┌──────────────────  전압 (CV 구간에서 고정)
        │           ___/│
        │       ___/    │
        │   ___/        │
 I_chg ─┼───────────────┐
        │   전류(CC)     │\
        │               │ \___
        │               │     \_____  ← 전류가 종료 전류(예: 0.1C 이하)로 떨어지면 종료
        └──────────────────────────────────▶ t
          pre-charge   CC 구간        CV 구간
```

- 만충 전압은 셀 화학에 따라 4.2 V, 4.35 V, 4.4 V 등이 있다. 셀 스펙을 따라야 하며 FW에서 임의로 올리면 안 된다(안전 문제).
- **JEITA**: 저온(예: 0~10°C)에서는 충전 전류를 줄이고, 고온(예: 45°C 근처)에서는 만충 전압을 낮추거나 충전을 멈추는 식으로 온도 구간별 규칙을 둔다. 정확한 구간은 셀 제조사 스펙을 따른다.

### 11.3 Fuel gauge 방식

| 방식 | 원리 | 장점 | 약점 |
|---|---|---|---|
| 전압 기반 | 개방 전압(OCV) 곡선으로 SoC 추정 | 간단, 추가 부품 적음 | 부하 중 전압 강하, Li-ion의 평탄 구간에서 부정확 |
| Coulomb counting | 션트로 전류를 적분 | 단기 정확 | 오프셋 누적(drift), 초기값 필요 |
| 모델 기반 (혼합) | 전압 + 전류 + 셀 모델(임피던스, 온도) | 정확, 노화 반영 | 셀별 characterization 필요 |

상용 예: TI Impedance Track 계열, Analog Devices(Maxim) ModelGauge m5 계열(예: MAX17055). 전용 gauge 칩 없이 PMIC의 전압/전류 ADC + 호스트 알고리즘으로 하는 방식도 있다(nPM1300 + Nordic fuel gauge 라이브러리처럼).

펌웨어가 챙길 것:

- **셀 모델/프로파일 로딩**: 전원 인가 시 gauge에 셀 파라미터를 써 줘야 하는 칩이 많다.
- **SoC(%) 표시 정책**: 0%에서 꺼지는 게 아니라 안전 마진을 둔 "UI 0%"를 정의한다. 급격한 % 점프를 막는 필터링.
- **캘리브레이션**: 전류 측정 offset/gain을 factory에서 보정(C09).
- **로그**: 필드 반품 분석을 위해 사이클 수, 최고 온도, 최저 전압 이력을 남긴다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 로그 UART/RTT를 켠 채 측정 | sleep 전류가 데이터시트보다 수백 µA 높음 | UART RX가 고속 클럭 유지 | 측정 빌드에서 `CONFIG_SERIAL`/`CONFIG_LOG` 끄고 비교 |
| 디버거 연결 상태로 측정 | 값이 mA 단위로 튐, 재현성 없음 | debug power domain 활성 | 디버거 분리 + 전원 재인가 |
| "확인 후 WFI" 레이스 | 가끔 이벤트 처리가 한 주기 늦음 | 확인과 WFI 사이 IRQ | PRIMASK로 막고 확인 후 WFI |
| deep sleep 후 클럭 재설정 누락 | 깨어난 뒤 UART baud 깨짐, 타이밍 틀어짐 | 벤더 deep sleep이 PLL을 끔 | wake 경로에서 클럭 트리 복원 |
| SysTick 기반 tickless를 deep sleep과 사용 | 시간이 멈추거나 느려짐 | deep sleep에서 SysTick 정지 | RTC/LPTIM 기반 tickless 구현 |
| 1 kHz tick과 32.768 kHz RTC 나머지 무시 | 장시간 후 시계가 틀어짐 | 분수 tick 누적 오차 | 나머지 누적 보정 |
| floating GPIO | 보드마다 sleep 전류가 다름 | 입력 버퍼 관통 전류 | 미사용 핀 disconnect/pull 고정 |
| 전원 꺼진 칩으로 IO high 출력 | 누설 + 다음 부팅 이상 | back-powering | 도메인 경계 IO를 low/high-Z |
| throttling hysteresis 없음 | 성능이 진동, 사용자 체감 끊김 | 단일 임계값 | on/off 임계 분리, 시간 필터 |
| 배터리 derating 없이 수명 약속 | 필드에서 수명 미달 | 저온/노화/cut-off 무시 | 70~85% 사용 가능 용량으로 계산 |

---

## 13. 면접에서 이렇게 말한다

**Q.** How would you reduce the average current of an always-on device by 1 mA?

**A.** 먼저 측정해서 power budget 표를 만든다: 각 도메인·상태별 전류와 시간 비율. 가장 큰 항목부터 공략한다. 흔히 SoC wake 횟수와 SoC suspend 전류가 지배적이다. 그다음 MCU 쪽: sleep floor 확인(누설), polling을 인터럽트/FIFO로, UART 바이트 인터럽트를 DMA로, 라디오 파라미터(connection interval, latency, batching) 조정. 매 변경마다 PPK2로 전후 µC/event를 비교한다.

"I'd start by measuring, not guessing. I'd build a power budget — current per state times time in that state — and attack the biggest line first, which in a SoC-plus-MCU device is usually how often the SoC wakes and what it draws in suspend. On the MCU side I'd check the sleep floor against the datasheet, replace polling with interrupts and sensor FIFOs, move byte-wise UART to DMA, and tune radio parameters like connection interval and peripheral latency. Every change gets verified with a PPK2 or Joulescope by comparing charge per event."

**Q.** What's the difference between WFI and WFE, and what does SLEEPDEEP do?

**A.** WFI는 인터럽트가 pending 될 때까지 잔다(PRIMASK로 막혀 있어도 깨어남). WFE는 event register를 보고, 이미 set이면 즉시 통과하므로 flag 대기나 멀티코어 동기화에 쓴다. SLEEPDEEP은 코어가 SoC에 "깊게 자겠다"는 신호를 주는 비트이고, 실제로 무엇이 꺼지는지는 벤더가 정한다.

"WFI sleeps until an interrupt is pending — even one masked by PRIMASK, which is what lets you check a condition with interrupts disabled and then sleep without a race. WFE sleeps until the event register is set; if it's already set, it clears it and returns immediately, so it's good for waiting on flags or cross-core signaling with SEV. SLEEPDEEP in the SCR just tells the SoC's power controller that the core wants deep sleep; what actually gets gated — clocks, RAM retention, which peripherals stay alive — is vendor-defined."

**Q.** Explain tickless idle. What can go wrong?

**A.** idle 시 다음 timeout까지 주기 tick을 멈추고 저전력 타이머로 한 번에 깨는 방식. 문제: SysTick이 deep sleep에서 멈춤 → RTC 기반 필요, 예정보다 일찍 깼을 때 실제 경과 시간으로 보정해야 함, RTC와 tick 주파수의 나머지 누적, 확인~WFI 레이스.

"Instead of waking every millisecond for the tick, the idle task computes how long until the next timeout, programs a low-power timer like the RTC for that time, and sleeps. On wake it tells the kernel how many ticks elapsed — vTaskStepTick in FreeRTOS. The pitfalls are using SysTick, which stops in deep sleep; waking early from another interrupt and not measuring the actual elapsed time; accumulating rounding error between a 32.768 kHz RTC and a 1 kHz tick; and the race between deciding to sleep and executing WFI, which you close by disabling interrupts first and calling eTaskConfirmSleepModeStatus."

**Q.** Walk me through a battery life estimate.

**A.** 한 주기의 전류 프로파일을 구간으로 나눠 전하를 적분 → 평균 전류 → 사용 가능 용량(derating: cut-off, 온도, 노화, 자가방전)으로 나눈다. 예: 1초마다 5 mA 2 ms + 3 µA sleep이면 평균 13 µA, CR2032 220 mAh × 0.8 / 0.013 mA ≈ 1.5년.

"I break one period of the current profile into segments and integrate charge — current times duration — then divide by the period to get average current. For example, 5 mA for 2 ms every second plus a 3 µA sleep floor gives about 13 µA average. Then I divide usable capacity by that: a 220 mAh coin cell derated to 80 percent for cut-off voltage, temperature and aging gives roughly 1.5 years. I also point out that the sleep floor is almost a quarter of the budget, so leakage matters as much as the active burst."

**Q.** How would you design thermal management for a wearable?

**A.** 온도 센서(SoC 내부, 보드 NTC, 배터리 NTC)로 skin temperature를 추정하는 가상 센서를 만들고, trip point와 hysteresis를 둔 단계적 cooling(CPU 주파수 상한, 라디오 TX 전력/duty 제한, 충전 전류 감소, 기능 제한). 배터리는 JEITA 구간 준수. 모델 계수는 DVT에서 열화상으로 상관시킨다.

"Skin temperature is the real constraint, and you can't measure it directly, so I'd build a virtual sensor from the SoC die sensor, board and battery NTCs, correlated against thermal camera data during DVT. Then staged mitigation with trip points and hysteresis: cap CPU frequency, limit radio duty or transmit power, reduce charge current — charging adds a lot of heat — and finally disable features. Because the device has a thermal time constant of minutes, the policy should react to sustained power, not short bursts. The battery side follows JEITA rules from the cell spec."

**Q.** Why a separate always-on MCU instead of just letting the SoC sleep?

**A.** SoC는 suspend에서도 DRAM self-refresh·모뎀 등으로 mA 단위, 깨어나는 데 수백 ms와 큰 에너지. MCU는 µA 대기, µs 단위 wake. 항상 들어야 하는 일(VAD, 호출어, IMU, 배터리)을 MCU가 거르고 진짜 필요할 때만 SoC를 깨운다. 트레이드오프는 IPC 복잡도, 펌웨어 두 개(업데이트, 버전 호환).

"A big application SoC is expensive both asleep and waking: suspend is still milliamps with DRAM in self-refresh, and each resume costs hundreds of milliseconds and real energy. An always-on MCU idles in microamps and wakes in microseconds, so it filters the always-listening work — voice activity, wake word, motion, battery monitoring — and wakes the SoC only when there's real work. The costs are an IPC protocol, buffering while the SoC resumes, and two firmware images to update and keep version-compatible."

**Q.** A board draws 300 µA in sleep instead of the expected 5 µA. How do you debug it?

**A.** 디버거 분리·전원 재인가로 측정 오류 제거 → 최소 펌웨어로 바닥 측정(보드 vs FW 분리) → 기능을 하나씩 켜는 bisect → 회로도에서 always-on 레일 부품 나열, pull-up과 GPIO 상태 대조, back-powering 확인 → 레일별 jumper 측정. 300 µA면 UART RX나 고속 클럭 유지, 또는 3.3 V/10 kΩ 풀업 충돌이 유력.

"First I rule out the measurement: disconnect the debugger and power-cycle, since an active debug session alone can add that much. Then I flash a minimal image that just enters the deepest sleep to separate board leakage from firmware. If the floor is fine, I bisect by enabling drivers one at a time. 300 µA smells like a UART receiver holding the high-frequency clock, or a GPIO driving against a 10 k pull-up — 3.3 V over 10 k is 330 µA. On the hardware side I list every part on the always-on rail from the schematic and check for back-powering into unpowered domains."

---

## 14. 직접 해보기

### 실습 1 — nRF52840 DK + PPK2로 sleep floor 재기

준비: nRF52840 DK, PPK2, nRF Connect for Desktop(Power Profiler 앱), Zephyr(west) 또는 nRF Connect SDK.

```sh
# Zephyr 워크스페이스 (처음 한 번)
west init ~/zephyrproject && cd ~/zephyrproject && west update
west zephyr-export
# (Python 의존성과 Zephyr SDK 설치는 Getting Started Guide 참조)

# System OFF 샘플 빌드/플래시 (경로는 Zephyr 버전에 따라 samples/boards/nrf/system_off)
west build -p -b nrf52840dk/nrf52840 zephyr/samples/boards/nordic/system_off
west flash
```

1. DK 사용자 가이드의 current measurement 절대로 DUT 전원을 분리하고 PPK2를 연결한다(ampere meter 모드로 DK 자체 전원을 쓰거나, source meter 모드로 PPK2가 공급).
2. 디버거 USB를 뽑고 전원을 재인가한 뒤 측정한다.
3. System ON idle 구간과 System OFF 구간의 floor를 기록하고 데이터시트 값과 비교한다.
4. `prj.conf`에 `CONFIG_SERIAL=y`, `CONFIG_LOG=y`를 넣은 빌드와 비교해 UART 비용을 직접 확인한다.

### 실습 2 — BLE advertising 간격별 평균 전류

```sh
west build -p -b nrf52840dk/nrf52840 zephyr/samples/bluetooth/beacon
west flash
```

- 샘플의 `bt_le_adv_start()` 인자를 바꿔 advertising interval을 100 ms, 1 s, 5 s로 바꾸며 PPK2로 평균 전류와 이벤트당 µC를 기록한다(C06의 API 설명 참고).
- 측정한 µC/event로 7절 공식을 써서 CR2032 수명을 계산하고, Nordic Online Power Profiler 추정치와 비교한다.

### 실습 3 — FreeRTOS tickless를 QEMU 없이 코드 읽기로

- FreeRTOS-Kernel 저장소의 `portable/GCC/ARM_CM4F/port.c`에서 `vPortSuppressTicksAndSleep`를 찾아 4.3절 뼈대와 줄 단위로 대조한다(`eTaskConfirmSleepModeStatus`, `configPRE_SLEEP_PROCESSING`, `vTaskStepTick` 위치).
- 확인할 질문: 기본 구현은 어떤 타이머로 긴 sleep을 만드나? 깬 뒤 어떻게 경과 tick을 계산하나?

```sh
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
grep -n "vPortSuppressTicksAndSleep" -A 40 FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c | head -120
```

### 실습 4 — Zephyr device runtime PM 추적

- `CONFIG_PM_DEVICE=y`, `CONFIG_PM_DEVICE_RUNTIME=y`로 센서 샘플(예: DK에 I2C 센서 연결)을 빌드한다.
- `CONFIG_PM_DEVICE_SHELL=y` + `CONFIG_SHELL=y`로 shell에서 `pm` 명령으로 장치 상태를 확인할 수 있다(버전별 명령 확인).
- 로직 분석기로 I2C 라인을 보면서 `pm_device_runtime_get/put` 시점에 센서 sleep/wake 명령이 나가는지 확인한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Dynamic power | 스위칭 전력 | α·C·V²·f, clock gating과 DVFS로 줄임 |
| Leakage | 누설 전류 | 전원이 있으면 흐름, 온도에 지수적, power gating으로 줄임 |
| Clock gating | 클럭 차단 | 블록 상태 유지, 즉시 복귀 |
| Power gating | 전원 차단 | 상태 소실, leakage까지 제거 |
| Retention | 상태 유지 | 전원 끄되 RAM/레지스터 일부 유지 |
| DVFS | 전압·주파수 동시 조절 | V² 때문에 에너지 절감 |
| WFI / WFE | 대기 명령 | 인터럽트 대기 / 이벤트 대기 |
| SLEEPDEEP | SCR bit 2 | deep sleep 요청, 의미는 벤더 정의 |
| SLEEPONEXIT | SCR bit 1 | ISR 끝나면 main 복귀 없이 sleep |
| Tickless idle | 주기 tick 중단 | 다음 timeout까지 RTC로 한 번에 잠 |
| Wake source | 깨우는 원인 | RTC, GPIO, 비교기, VAD 등 |
| Race-to-idle | 빨리 끝내고 자기 | baseline 전류 때문에 보통 유리 |
| Duty cycle | 켜진 시간 비율 | 평균 전류 = Σ I×t / T |
| Burden voltage | 측정 전압 강하 | 션트 때문에 DUT 전압 하락 |
| Back-powering | 역전원 | 꺼진 칩으로 IO가 전류 공급 |
| PMIC | 전원 관리 IC | 레귤레이터·충전·시퀀싱·ship mode |
| LDO / Buck | 선형 / 스위칭 레귤레이터 | 저노이즈 / 고효율 |
| Iq | quiescent current | 레귤레이터 자체 소비 전류 |
| CC/CV | 정전류/정전압 충전 | Li-ion 표준 충전 방식 |
| JEITA | 온도 구간 충전 규칙 | 저온·고온에서 전류/전압 제한 |
| Fuel gauge | 잔량 추정 | 전압/쿨롱 카운팅/모델 기반 |
| Thermal zone / trip point | 열 관리 단위/임계 | Linux thermal framework 용어 |
| Throttling | 성능 제한 | 온도 초과 시 클럭·전력 상한 |
| Skin temperature | 표면 온도 | 웨어러블의 실제 발열 한계 |
| Sensor hub | 상시 센서 처리 코어 | SoC 대신 µA 단위로 감시 |

---

## 16. 요약 & 체크리스트

전력은 동적(α·C·V²·f)과 누설(온도에 지수적) 두 가지이고, 각각 clock gating/DVFS와 power gating/retention으로 줄인다. Cortex-M은 WFI/WFE와 SCR(SLEEPDEEP, SLEEPONEXIT)만 제공하고 실제 전원 상태는 벤더가 정의한다. RTOS에서는 tickless idle(FreeRTOS `vPortSuppressTicksAndSleep`, Zephyr PM power states + device runtime PM)이 sleep 정책의 중심이다. 배터리 수명은 전류 프로파일 적분 → 평균 전류 → derating된 용량으로 계산하고, SoC+MCU 기기에서는 SoC wake 횟수와 suspend 전류가 대개 지배적이다. 측정은 PPK2/Joulescope로 하며 디버거·로그 UART·floating 핀·back-powering이 흔한 누설 원인이다. 발열은 skin temperature와 배터리 안전이 한계이며, 가상 센서 + trip point + hysteresis + 단계적 cooling으로 관리한다.

- [ ] P = α·C·V²·f와 leakage의 차이, 각각을 줄이는 기법을 화이트보드에 쓸 수 있다
- [ ] WFI/WFE 차이와 SCR의 SLEEPDEEP/SLEEPONEXIT/SEVONPEND 역할을 설명할 수 있다
- [ ] "PRIMASK로 막고 확인 후 WFI" 패턴이 왜 레이스를 막는지 설명할 수 있다
- [ ] RTC 기반 tickless idle 흐름(eTaskConfirmSleepModeStatus → RTC arm → WFI → vTaskStepTick)을 그릴 수 있다
- [ ] Zephyr의 system PM(power-states, policy lock)과 device runtime PM(get/put)을 구분해 설명할 수 있다
- [ ] 전류 프로파일에서 평균 전류와 배터리 수명을 derating 포함해 계산할 수 있다
- [ ] SoC+MCU 기기의 power budget 표를 만들고 가장 큰 레버를 짚을 수 있다
- [ ] sleep 전류 이상 시 측정 오류 → 최소 FW → bisect → 회로도 순서로 디버깅 절차를 말할 수 있다
- [ ] thermal zone, trip point, hysteresis, skin temperature 가상 센서를 설명할 수 있다
- [ ] PMIC 기능(시퀀싱, 충전 CC/CV, JEITA, ship mode)과 fuel gauge 3방식을 비교할 수 있다

---

## 참고 자료

- [ARM Cortex-M4 Devices Generic User Guide — System Control Register](https://developer.arm.com/documentation/dui0553/latest/)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/)
- [FreeRTOS — Low Power Support (tickless idle)](https://www.freertos.org/low-power-tickless-rtos.html)
- [FreeRTOS-Kernel GitHub](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- [Zephyr — Power Management](https://docs.zephyrproject.org/latest/services/pm/index.html)
- [Zephyr — Device Runtime Power Management](https://docs.zephyrproject.org/latest/services/pm/device_runtime.html)
- [Nordic — Power Profiler Kit II](https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2)
- [Nordic — Online Power Profiler for Bluetooth LE](https://devzone.nordicsemi.com/power/w/opp/2/online-power-profiler-for-bluetooth-le)
- [Nordic — nRF52840 Product Specification](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html)
- [Joulescope](https://www.joulescope.com/)
- [Linux kernel — Thermal framework (sysfs API)](https://docs.kernel.org/driver-api/thermal/sysfs-api.html)
- [Linux kernel — Runtime Power Management](https://docs.kernel.org/power/runtime_pm.html)
- [Nordic — nPM1300 PMIC](https://www.nordicsemi.com/Products/nPM1300)
