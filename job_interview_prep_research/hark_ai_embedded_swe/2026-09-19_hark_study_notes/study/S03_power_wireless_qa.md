# S03. 저전력·발열·BLE/Wi-Fi/Thread 면접 문항 — 23문항 + 계산 연습 4개

> **목표**: always-on 배터리 기기의 전력·발열 질문과 BLE/Wi-Fi/Thread 질문에 원리 + 숫자로 답하고, 배터리 수명·평균 전류를 면접관 앞에서 화이트보드로 계산한다
> **선행**: C05(저전력·발열), C06(BLE·Wi-Fi·Thread), C04(RTOS의 tickless idle)
> **사용법**: 질문을 먼저 소리 내어 답해 보고, 그다음 모범 답안을 읽는다. 계산 연습은 답을 가리고 직접 푼다.

---

## 0. 이 노트 쓰는 법

- 저전력 질문의 모범 답 구조는 항상 **측정 → 예산(분해) → 가장 큰 항목부터 → 다시 측정**이다. 숫자 없이 "sleep을 많이 쓴다"로 끝내지 않는다.
- 무선 질문은 Don의 약점이다. 스펙 숫자(interval 범위, MTU, PHY)를 정확히 외우고, Apple 무선 칩셋 통합 경험(호스트 인터페이스, 공존, bring-up)으로 연결한다.
- 전류 값(µA, mA)은 **예시값**이다. 실제 값은 칩 데이터시트마다 다르다(벤더마다 다름). 면접에서도 "assume"으로 시작한다.
- 단위 약속: 1 mAh = 3.6 C, 전하 Q(µC) = 전류(mA) × 시간(ms). 평균 전류 = 초당 전하(µC/s = µA).

| 절 | 문항 | 핵심 키워드 |
|---|---|---|
| 1. 저전력 | Q01~Q08 | 전력 예산, WFI/WFE, SLEEPDEEP, leakage, wake source, 측정, sleep 전류 디버깅, race-to-idle |
| 2. 배터리·발열 | Q09~Q10 | fuel gauge, JEITA, brownout, throttling, skin temp |
| 3. BLE | Q11~Q17 | 스택, advertising, connection 파라미터, throughput, 보안, GATT, provisioning |
| 4. Wi-Fi·Thread·통합 | Q18~Q23 | PS/DTIM/TWT, 연결 과정, Thread·Matter, HCI, 공존, 필드 디버깅 |
| 5. 계산 연습 | E1~E4 | 배터리 수명, BLE 평균 전류, 센서 duty cycle, Wi-Fi DTIM |

---

## 1. 저전력

### Q01. Our always-on device averages 2 mA and we need to get it under 1 mA. How do you approach it?

**왜 묻나**: JD "Optimize power consumption and thermal performance for always-on, battery-powered devices". 방법론을 본다.

**30초 답변**: 추측하지 않고 먼저 측정한다. 전류 프로파일러로 시간 축 파형을 떠서 **상태별(sleep, sensor, 오디오, 라디오, SoC) 전하량 예산표**를 만든다. 평균 전류 = 합(상태 전류 × 시간 비율)이므로, 가장 큰 항목부터 줄인다. 보통 순서는 ① sleep baseline(누설, 켜진 주변장치, floating 핀) ② 불필요한 wakeup 제거(폴링 → 인터럽트, tickless, 센서 FIFO batching) ③ 라디오 duty cycle(connection interval, DTIM) ④ active 시간 단축(DMA, 빠른 클럭으로 끝내고 잠들기) ⑤ SoC를 깨우는 횟수 줄이기다.

**English answer**: I'd start by measuring, not guessing: capture a current trace over a realistic use cycle and break it into a budget of charge per state — sleep floor, sensor wakeups, audio front end, radio events, and SoC sessions. Average current is just the sum of each state's current times its duty cycle, so the budget tells me which line item to attack first. Typically the order is the sleep floor, then the number of wakeups — interrupts instead of polling, tickless idle, sensor FIFOs — then radio duty cycle like connection interval or DTIM, then shortening active time with DMA and race-to-idle. After each change I re-measure against the budget, and I add a power regression test so a later firmware change can't silently undo it.

**꼬리질문**:
- Q: 1 mA를 달성해도 필드에서 배터리가 빨리 닳는다면? → A: 실험실 사용 시나리오와 실제가 다르다. 필드 텔레메트리(상태별 체류 시간, wakeup 원인 카운터, 라디오 재연결 횟수)로 실제 duty cycle을 알아야 한다.
- Q: 가장 흔한 "숨은" 소모원은? → A: 연결 끊김 후 공격적 재스캔/재연결, 안 꺼진 디버그 UART/SWD, 센서가 active 모드로 남음, 풀업 전류.

**Don 스토리 연결**: Apple에서 신뢰성 vs 성능/전력 margin sign-off, Power Analyzer 사용 경험. "측정 장비를 먼저 세팅하고 상태별로 쪼갰다"는 방법론을 강조한다.

### Q02. Explain the Cortex-M sleep modes. What are WFI, WFE, SLEEPDEEP and SLEEPONEXIT?

**왜 묻나**: 저전력의 하드웨어 기본.

**30초 답변**: `WFI`는 인터럽트가 올 때까지 잠들고, `WFE`는 이벤트(인터럽트, `SEV`, event register)로 깬다. SCR(System Control Register)의 **SLEEPDEEP** 비트가 0이면 sleep(코어 클럭만 정지), 1이면 deep sleep이며 실제로 무엇이 꺼지는지(PLL, flash, SRAM 리텐션, 주변장치)는 **벤더가 정의**한다. **SLEEPONEXIT**를 켜면 ISR이 끝나고 thread 모드로 돌아가지 않고 바로 다시 잠들어서, 인터럽트만으로 동작하는 시스템의 복귀 오버헤드를 줄인다.

**English answer**: WFI puts the core to sleep until an interrupt is pending, and WFE sleeps until an event, which can be an interrupt, a SEV from another core, or a latched event. The SLEEPDEEP bit in the System Control Register selects between normal sleep, where basically only the core clock stops, and deep sleep, where the vendor's power controller can shut down PLLs, flash and peripherals — what exactly is off and what's retained is vendor-defined. SLEEPONEXIT makes the core go straight back to sleep when an ISR returns instead of returning to thread mode, which is ideal for purely interrupt-driven designs. Before WFI I issue a DSB so pending writes, like clearing a peripheral flag, have completed.

**꼬리질문**:
- Q: PRIMASK=1 상태에서 WFI를 하면? → A: 인터럽트가 pending되면 깨어나지만 핸들러는 실행되지 않는다. 그래서 "조건 검사와 sleep 사이 race"를 막는 패턴으로 쓴다(인터럽트 끄고 → 조건 확인 → WFI → 인터럽트 켜서 핸들러 실행).
- Q: deep sleep에서 디버거가 끊기는 이유는? → A: 디버그 도메인 전원/클럭이 꺼지기 때문. 벤더의 debug-in-low-power 설정(예: STM32 `DBGMCU` 비트)을 켜면 유지되지만 전류가 늘어난다(벤더마다 다름).

```c
#include "cmsis_compiler.h"
#include "core_cm4.h"          /* 실제로는 device header를 include (벤더마다 다름) */

extern volatile int g_work_pending;

void idle_sleep(void)
{
    __disable_irq();                    /* race 방지: 검사와 sleep을 원자적으로 */
    if (!g_work_pending) {
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        __DSB();
        __WFI();                        /* pending IRQ가 있으면 바로 깨어남 */
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    }
    __enable_irq();                     /* 여기서 ISR 실행 */
}
```

### Q03. Where does power go in CMOS logic? Why does lowering voltage matter more than lowering frequency?

**왜 묻나**: 전력 물리 이해. DVFS 설명 능력.

**30초 답변**: 동적 전력은 P = α·C·V²·f(α 활동률, C 스위칭 정전용량)라 전압에 제곱으로 비례한다. 정적 전력은 누설 전류 × 전압으로 클럭과 무관하게 흐르고, 온도가 오르면 크게 늘어난다. 주파수만 낮추면 일을 끝내는 시간이 늘어나 에너지(= 전력 × 시간)는 거의 그대로지만, 주파수를 낮추면서 **전압도 낮출 수 있으면**(DVFS) 연산당 에너지가 V²로 줄어든다. deep sleep에서는 누설이 지배하므로 power gating과 SRAM 리텐션 범위 축소가 핵심이다.

**English answer**: Dynamic power scales with activity, switched capacitance, the square of voltage, and frequency, while static power is leakage times voltage and flows whether the clock runs or not, growing quickly with temperature. Lowering frequency alone mostly stretches the work out, so energy per task barely changes; the win comes when the lower frequency lets you drop the supply voltage, because energy per operation falls with the square of voltage — that's DVFS. In deep sleep, leakage dominates, so what matters is power-gating domains and retaining only the SRAM you actually need.

**꼬리질문**:
- Q: SRAM 리텐션을 줄이면 무엇을 잃나? → A: 꺼진 뱅크의 데이터. 깨어날 때 다시 로드해야 하므로 wake latency와 에너지가 늘어난다. 자주 깨는 시스템은 리텐션이 유리하다.
- Q: 뜨거운 기기의 sleep 전류가 높은 이유는? → A: 누설이 온도에 대해 지수적으로 늘어난다. 그래서 sleep 전류는 온도별로 측정한다.

### Q04. What can wake an MCU from its deepest sleep state, and what do you lose?

**왜 묻나**: always-on 센서 허브 설계 이해.

**30초 답변**: 가장 깊은 모드에서는 보통 always-on 도메인에 있는 것만 깨울 수 있다. RTC/low-power 타이머(32.768 kHz), wake-up 핀/GPIO 에지, 비교기, 일부 low-power UART, 리셋이다. 어떤 모드는 SRAM과 레지스터를 유지(retention)하고 인터럽트처럼 이어서 실행하지만, 가장 깊은 모드(예: 일부 칩의 system OFF/standby)는 SRAM을 잃고 **리셋으로 깨어나서** 다시 부팅한다(벤더마다 다름). 설계할 때는 모드별 전류, wake latency, 유지되는 것, 깨울 수 있는 소스를 표로 비교해 고른다.

**English answer**: In the deepest modes only the always-on domain can wake you — typically the RTC or a low-power timer on the 32 kHz crystal, wake-up pins or GPIO edges, a comparator, sometimes a low-power UART, and reset. The lighter deep-sleep modes retain SRAM and registers and resume like an interrupt, while the very deepest modes on many parts lose RAM and wake through a reset, so the firmware effectively reboots and must restore state from retained registers or flash. So I build a table per chip of current, wake latency, what's retained and which sources can wake it, and pick the mode per use case — for example, a sensor-hub idle mode with RAM retention versus a shipping mode that's basically off.

**꼬리질문**:
- Q: 센서가 MCU를 깨우게 하려면? → A: 센서의 인터럽트 핀(FIFO watermark, wake-on-motion)을 wake 가능한 GPIO에 연결한다. 회로도 단계에서 확인해야 하는 항목이다.
- Q: 리셋으로 깨는 모드에서 깨어난 원인을 어떻게 아나? → A: 리셋 원인 레지스터(벤더마다 이름 다름, 예: nRF `RESETREAS`)와 retention 레지스터를 읽는다.

**Don 스토리 연결**: 회로도 리뷰에서 wake 핀 배정을 확인한 경험이 있으면 좋고, 없으면 bring-up 체크리스트 항목으로 말한다.

### Q05. How do you measure current on a device that sleeps at a few microamps but bursts to 100 milliamps?

**왜 묻나**: 측정 실무. 멀티미터 평균값의 함정을 아는지.

**30초 답변**: 동적 범위가 5자리 이상이라 일반 멀티미터나 고정 shunt로는 sleep과 burst를 동시에 정확히 못 본다. **auto-ranging 전류 프로파일러**(Nordic PPK2, Joulescope, Keysight N6705 + N6781A 같은 SMU/power analyzer)를 배터리 대신 공급원으로 또는 직렬로 넣고, 충분히 높은 샘플레이트로 시간 파형을 본다. 평균은 긴 구간(수 분 이상)의 적분으로 구한다. 디버거·UART 케이블은 분리하고(누설 경로), 레일별 전류가 필요하면 보드의 0 Ω 저항/전류 측정 점퍼를 쓴다.

**English answer**: The dynamic range is the problem — five orders of magnitude between sleep and a radio burst — so I use an auto-ranging profiler like a Nordic PPK2, a Joulescope, or a source-measure unit in a power analyzer, either sourcing the device in place of the battery or in series with it. I look at the time-domain trace to identify each state, and compute the average by integrating over minutes, not by reading a meter. I disconnect the debug probe and UART adapters because they back-power the chip through I/O pins, and for per-rail numbers I use the board's current-sense resistors or jumpers. And I measure at temperature, since sleep current is dominated by leakage.

**꼬리질문**:
- Q: 소스 모드로 측정할 때 주의점은? → A: 배터리 내부 저항이 없으므로 burst 시 전압 강하(brownout) 거동이 실제와 다르다. 최종 검증은 실제 배터리로도 한다.
- Q: 파형에 주기적 스파이크가 보이면? → A: 주기로 원인을 역추적한다(1 ms면 RTOS tick, 102.4 ms 배수면 Wi-Fi beacon, connection interval이면 BLE).

**Don 스토리 연결**: 레쥬메의 Power Analyzer, DSO 사용 경험. "Apple에서 power analyzer로 레일별 전류를 보고 margin을 sign-off했다"로 직접 연결한다.

### Q06. The sleep current on DVT boards is 80 µA but the target is 10 µA. How do you find the leak?

**왜 묻나**: 실전 디버깅 시나리오. 체계성을 본다.

**30초 답변**: 이분 탐색이다. ① 펌웨어를 최소 sleep 이미지로 바꿔 HW baseline을 확인한다(이 값이 높으면 하드웨어·부품 문제). ② 레일별로 전류를 나눠 어느 레일인지 찾는다. ③ 그 레일의 부품(센서, PMIC LDO 자체 소모, 레벨 시프터)을 하나씩 sleep/disable한다. 흔한 원인은 floating 입력 핀(입력 버퍼 관통 전류), pull-up에 low로 구동된 핀, 꺼진 도메인에 연결된 핀으로의 back-powering, sleep 명령을 못 받은 센서, 켜진 채 남은 주변장치 클럭, 디버그 연결이다. EVT와 DVT 차이면 BOM/회로 변경을 먼저 diff한다.

**English answer**: I bisect. First I flash a minimal image that just configures pins and enters the deepest sleep; if that's already high it's hardware or a part choice, otherwise it's firmware. Next I measure per rail to localize it, then disable devices on that rail one by one. The usual suspects are floating inputs drawing shoot-through current, a GPIO driving low against a pull-up, back-powering a switched-off device through its I/O pins, a sensor that never received its sleep command, a peripheral clock left running, and an attached debugger. If EVT was fine and DVT isn't, I diff the schematic and BOM first, because a changed pull-up value or a new part often explains it immediately.

**꼬리질문**:
- Q: pull-up 하나가 얼마를 먹나? → A: 3.3 V, 10 kΩ에 low로 잡혀 있으면 330 µA다. 목표 10 µA와 비교하면 치명적이다.
- Q: 펌웨어로 모든 미사용 핀을 어떻게 처리하나? → A: 칩 권장대로 설정한다. 보통 입력 버퍼를 끈 analog/disconnect 상태나 정해진 레벨의 출력. 외부 회로와 충돌하지 않게 회로도로 확인한다.

**Don 스토리 연결**: "EVT 보드 일부만 실패"하는 Apple 인터페이스 root cause 방식(가설 → 측정 → 좁히기)을 그대로 전력 문제에 적용한다고 말한다.

### Q07. Race-to-idle or run slow — which is better for energy?

**왜 묻나**: 트레이드오프 사고.

**30초 답변**: 정적 전력(누설과 켜진 주변장치, 레귤레이터, 클럭 트리의 고정 소모)이 크면 빨리 끝내고 깊이 자는 race-to-idle이 유리하다. 반대로 빠른 클럭을 위해 전압을 올려야 하면(DVFS 레벨), V² 때문에 느리게 낮은 전압으로 도는 것이 유리할 수 있다. 결국 칩마다 연산당 에너지를 측정해서 정한다. MCU는 보통 race-to-idle이 맞고, SoC는 DVFS 거버너가 판단한다.

**English answer**: It depends on the ratio of fixed to dynamic power. If the fixed overhead while awake — leakage, regulators, clock trees, peripherals that must stay on — is significant, finishing fast and dropping into deep sleep wins, which is the usual case on MCUs. If running faster requires a higher voltage level, the square-law cost can make it better to run slower at a lower voltage. So I measure energy per task at a couple of operating points on the real chip rather than assume; on an application processor, the DVFS governor makes that decision continuously.

**꼬리질문**:
- Q: DMA는 race-to-idle과 어떻게 연결되나? → A: 데이터 이동 중 CPU를 sleep에 두므로, "CPU active 시간"을 줄이는 또 다른 방법이다.

### Q08. How would you split power management between an always-on MCU and an application SoC?

**왜 묻나**: Hark 추정 구조(SoC + always-on MCU). 시스템 관점.

**30초 답변**: 원칙은 "SoC는 필요할 때만 깬다". MCU가 always-on 일을 맡는다. 마이크 VAD/wake word, 센서 수집·batching, 버튼, 배터리 감시, BLE 연결 유지(칩 구성에 따라). SoC는 사용자 세션이나 무거운 작업(큰 모델, 셀룰러/Wi-Fi 업로드)이 있을 때만 깨우고, 끝나면 suspend로 돌려보낸다. 인터페이스는 wake GPIO(양방향), IPC(SPI/UART/공유 메모리) 프로토콜, 그리고 누가 어떤 레일을 켜고 끄는지 정의한 전원 상태 머신이다. 가장 큰 위험은 SoC wake 횟수와 wake latency, 그리고 IPC 경합이다.

**English answer**: The principle is that the application processor sleeps unless there is a user session or heavy work. The always-on MCU owns the continuous jobs — microphone voice activity and wake word, sensor batching, buttons, battery monitoring — and wakes the SoC with a dedicated GPIO plus a message over SPI or UART saying why. The SoC does the heavy lifting, then hands state back and suspends. I'd define a system power state machine that says which rails and radios are on in each state and who owns the transitions, and I'd instrument the number of SoC wakeups and their reasons, because false wakes are usually the biggest avoidable drain.

**꼬리질문**:
- Q: 잘못된 wake(false wake)를 어떻게 줄이나? → A: MCU에서 2단계 검출(저전력 VAD → wake word), 임계값 튜닝, SoC 깨우기 전 확인, 그리고 wake 원인별 카운터 텔레메트리.
- Q: SoC가 깨어나는 동안 오디오를 잃지 않으려면? → A: MCU가 링버퍼로 수백 ms~수 초 오디오를 보관하고, SoC가 준비되면 wake word 이전 구간부터 넘긴다.

**Don 스토리 연결**: Apple에서 무선 칩과 호스트 AP 사이 인터페이스(PCIe 전원 상태, 사이드밴드 GPIO)를 디버깅한 경험이 이 구조와 닮았다.

---

## 2. 배터리·발열

### Q09. How does firmware estimate battery state of charge, and what battery protections does it enforce?

**왜 묻나**: 배터리 기기 필수 지식. PMIC·fuel gauge 드라이버 작성과 연결.

**30초 답변**: 전압만으로는 부정확하다(Li-ion 방전 곡선이 평탄하고 부하·온도에 따라 전압이 흔들림). 그래서 fuel gauge IC가 **coulomb counting**(shunt로 전하 적분)과 **전압/모델 기반 보정**(예: TI Impedance Track, Maxim/ADI ModelGauge 같은 방식)을 결합한다. 펌웨어는 I2C로 SoC%, 전압, 전류, 온도를 읽고 표시·정책에 쓴다. 보호 측면에서는 저전압 컷오프 전에 안전 종료, burst 부하 시 brownout 방지(라디오 TX와 SoC 부팅 동시 발생 피하기), 그리고 JEITA 방식 온도별 충전 제어(저온·고온에서 충전 전류/전압을 줄이거나 중단, 구체 경계는 셀 스펙마다 다름)를 한다.

**English answer**: Voltage alone is a poor estimate because the lithium-ion discharge curve is flat and voltage sags with load and temperature. A fuel gauge combines coulomb counting through a sense resistor with voltage- and model-based correction, and firmware reads state of charge, current, voltage and temperature over I2C. On the protection side, firmware triggers a clean shutdown before the hard undervoltage cutoff, avoids stacking big loads — say a radio transmit burst while the SoC boots — at low charge to prevent brownout, and applies temperature-dependent charging, reducing or stopping charge current in the cold and hot ranges the cell datasheet specifies, which is what JEITA guidelines describe.

**꼬리질문**:
- Q: 배터리가 20%에서 갑자기 꺼진다면? → A: 저온이나 노화로 내부 저항이 커져 burst 부하에서 전압이 컷오프 아래로 떨어진 것. 게이지 모델 학습 상태, 피크 부하 조정, 저전압 경고 임계값 조정을 본다.
- Q: 충전이 가장 큰 발열원이 될 수 있나? → A: 그렇다. 소형 웨어러블에서는 충전 중 발열 때문에 충전 전류를 줄이는 thermal 정책이 흔하다.

### Q10. The device gets uncomfortably warm during long voice sessions. What does the firmware do?

**왜 묻나**: JD "thermal performance". 제어 루프 설계와 사용자 경험의 균형.

**30초 답변**: 먼저 열원을 분해한다(SoC 연산, 셀룰러/Wi-Fi TX, 충전, 디스플레이 등). 제어는 여러 온도 센서(SoC 내부 junction 센서, 보드 thermistor)로 **표면(skin) 온도를 추정**하고, 단계적 throttling을 건다. 예: 1단계 SoC 최대 주파수 제한, 2단계 라디오 전송 batching/속도 제한, 3단계 충전 전류 감소, 최후에 기능 제한과 사용자 알림. 각 단계에 **히스테리시스**를 둬서 진동을 막고, 표면 온도 한계는 제품 정책과 안전 규격(예: IEC 62368-1의 접촉 온도 요구)에서 온다. 실제 효과는 열 챔버에서 장시간 시나리오로 검증한다.

**English answer**: I'd first break down where the heat comes from — SoC compute, cellular or Wi-Fi transmit, charging — using power data per rail. Firmware can't measure skin temperature directly, so it estimates it from internal junction sensors and board thermistors with a model calibrated in thermal chamber tests. Then it applies staged mitigation with hysteresis: cap the SoC's frequency, batch or rate-limit radio transmissions, reduce charge current, and only as a last resort limit features and tell the user. The limits come from product policy and safety standards, and I'd validate the control loop with long real-use scenarios, because thermal time constants are minutes, not milliseconds.

**꼬리질문**:
- Q: 왜 히스테리시스가 필요한가? → A: 임계값 근처에서 throttling on/off가 반복되면 성능이 출렁이고 사용자 경험이 나빠진다. 해제 온도를 진입 온도보다 낮게 둔다.
- Q: on-device 모델 추론과 발열은? → A: 추론을 NPU/DSP로 옮기면 같은 작업의 에너지가 줄어 발열도 준다. 긴 세션에서는 모델 크기/빈도 조정도 thermal 레버다.

**Don 스토리 연결**: SK hynix에서 SSD thermal throttling(엔터프라이즈 SSD는 온도 기반 성능 제한이 표준 기능) 경험이 있으면 "SSD에서 컨트롤러 온도 기반 throttling을 다뤘다"로 직접 연결한다.

---

## 3. BLE

### Q11. Walk me through the Bluetooth LE stack layers.

**왜 묻나**: JD "Familiarity with wireless protocols (BLE ...)". 전체 그림을 아는지.

**30초 답변**: 아래부터 **Controller**: PHY(2.4 GHz, 40채널 2 MHz 간격, LE 1M/2M/Coded) → Link Layer(advertising, connection, 채널 호핑, ACK/재전송, 암호화). 그 위 **HCI**가 controller와 host를 나눈다(같은 칩이면 내부 함수 호출, 분리 칩이면 UART/USB/SDIO). **Host**: L2CAP(다중화, 분할) → ATT(속성 읽기/쓰기) → GATT(서비스·characteristic 구조), SM(pairing, 키 생성), GAP(역할·검색·연결 절차). 앱은 GATT와 GAP API를 쓴다.

**English answer**: At the bottom is the controller: the PHY on forty 2-megahertz channels in the 2.4 gigahertz band, with the LE 1M, 2M and Coded PHYs, and the link layer that handles advertising, connections, channel hopping, acknowledgements and encryption. HCI is the boundary between controller and host — a function call on a single-chip SoC, or a UART, USB or SDIO transport when the radio is a separate chip. The host has L2CAP for multiplexing, ATT for reading and writing attributes, GATT which organizes attributes into services and characteristics, the Security Manager for pairing and key distribution, and GAP for roles, discovery and connection procedures.

**꼬리질문**:
- Q: advertising 채널은? → A: primary advertising 채널 37, 38, 39 세 개. 나머지 37개는 데이터(및 extended advertising의 secondary) 채널이다.
- Q: LE Coded PHY는 왜 쓰나? → A: FEC로 도달 거리를 늘린다. S=2는 500 kbps, S=8은 125 kbps. 대신 air time과 전력이 늘어난다.

**Don 스토리 연결**: "Apple에서는 이 스택 중 HCI 아래 — 라디오 칩과 호스트 인터페이스, 전원·클럭·공존 신호 — 를 매일 디버깅했다. 이제 그 위 host 계층을 공부하고 있다"로 정직하게 연결한다.

### Q12. Explain advertising versus connections. What advertising interval would you choose?

**왜 묻나**: 전력과 발견 속도의 트레이드오프.

**30초 답변**: advertising은 연결 없이 3개 채널에 주기적으로 패킷을 방송하는 것(발견, beacon, 연결 전 단계), connection은 두 기기가 합의한 스케줄에 따라 채널 호핑하며 양방향 데이터를 교환하는 것이다. legacy advertising interval은 **20 ms ~ 10.24 s**(0.625 ms 단위)이고, 충돌 방지를 위해 매 이벤트마다 0~10 ms 랜덤 advDelay가 더해진다. 선택 전략은 "처음 수십 초는 빠르게(예: 수십 ms), 그 후 느리게(예: 1 s 이상)"로 발견 속도와 전력을 절충한다. 폰 쪽 스캔 duty cycle도 발견 시간에 영향을 준다.

**English answer**: Advertising is connectionless broadcasting on the three primary channels — used for discovery, beacons, and as the step before a connection. A connection is a scheduled exchange where both sides hop channels together at an agreed interval and can send data both ways. The legacy advertising interval is 20 milliseconds to 10.24 seconds in 0.625 millisecond steps, plus a random zero to ten millisecond delay each event to avoid repeated collisions. I'd advertise fast for the first thirty seconds or so after a user action, when they're waiting to pair, and then back off to a second or more, because discovery time also depends on the phone's scan duty cycle, which we don't control.

**꼬리질문**:
- Q: extended advertising(BT 5.0)은? → A: primary 채널에는 짧은 포인터만 보내고 실제 데이터(PDU당 최대 약 250바이트, 체인으로 최대 1,650바이트)는 secondary 채널에 보낸다. 2M/Coded PHY도 쓸 수 있다.
- Q: 광고에 연결 없이 데이터를 넣을 수 있나? → A: manufacturer specific data나 service data AD type으로 가능. legacy advertising payload는 31바이트다.

### Q13. How do connection interval, peripheral latency and supervision timeout affect power and responsiveness?

**왜 묻나**: BLE 전력 질문의 핵심. 계산 연습 E2와 연결.

**30초 답변**: connection interval은 **7.5 ms ~ 4 s**(1.25 ms 단위)로, 이 주기마다 connection event가 생겨 라디오가 깬다. 평균 전류는 대략 이벤트당 전하 × 이벤트 빈도 + sleep 전류라서, interval을 늘리면 거의 반비례로 줄어든다. peripheral latency(0~499)는 보낼 데이터가 없을 때 peripheral이 그만큼의 이벤트를 건너뛰게 해서, 평상시 전력은 긴 interval처럼, 보낼 때 응답성은 짧은 interval처럼 만든다. supervision timeout(100 ms ~ 32 s)은 이 시간 동안 패킷을 못 받으면 연결이 끊긴 것으로 보며, (1 + latency) × interval × 2보다 커야 한다. 파라미터는 **central이 결정**하고, peripheral은 요청만 할 수 있다(L2CAP Connection Parameter Update Request 또는 LL Connection Parameters Request 절차).

**English answer**: The connection interval, 7.5 milliseconds to 4 seconds in 1.25 millisecond steps, sets how often the radio wakes for a connection event, so average current is roughly charge per event times event rate plus the sleep floor — doubling the interval nearly halves the radio's share. Peripheral latency lets the peripheral skip up to that many events when it has nothing to send, so idle power looks like a long interval while it can still send promptly. Note it doesn't help the other direction: data from the phone waits until the peripheral next listens. The supervision timeout, 100 milliseconds to 32 seconds, must exceed twice the effective interval including latency. The central decides the parameters; the peripheral can only request them, and phones apply their own policies, so firmware must handle getting something different from what it asked for.

**꼬리질문**:
- Q: 파라미터가 조건을 어기면? → A: 거절되거나 연결 수립 실패/끊김이 생긴다(예: 0x3B Unacceptable Connection Parameters).
- Q: 보통 어떻게 운영하나? → A: 페어링·동기화·OTA 중에는 짧은 interval, 평상시 연결 유지에는 긴 interval + latency로 전환한다.

**Don 스토리 연결**: 폰 쪽(central) 정책은 벤더마다 다르다는 점을 알고 있다는 것을 보여 주되, Apple 내부 정보는 말하지 않는다.

### Q14. How do you maximize BLE throughput, for example to upload audio or logs?

**왜 묻나**: OTA, 로그 업로드, 오디오 전송 설계.

**30초 답변**: 네 가지를 같이 올린다. ① **ATT MTU** 교환(기본 23 → 최대 517, notification payload = MTU − 3) ② **Data Length Extension**(BT 4.2, LL payload 27 → 최대 251바이트)으로 L2CAP 분할 오버헤드 감소 ③ **LE 2M PHY**(BT 5.0)로 air time 절반 ④ connection event당 여러 패킷 보내기(event length 확장, 짧은 interval, 큐가 비지 않게 notification 연속 전송). write with response/indication은 왕복 대기 때문에 느리므로 대량 전송은 notification과 write without response를 쓴다. 이상적인 조건에서 앱 처리량은 1 Mbps를 넘지만 실제 값은 폰 구현에 크게 좌우된다.

**English answer**: I'd negotiate a larger ATT MTU — the default is 23 bytes, so a notification carries only 20 bytes of payload — enable Data Length Extension so each link-layer packet carries up to 251 bytes instead of 27, switch to the LE 2M PHY to halve air time, and keep the transmit queue full so several packets go out per connection event. For bulk data I use notifications or write-without-response, since indications and writes with response wait for an acknowledgement round trip. In good conditions that gets application throughput above a megabit per second, but the phone's stack decides the event length and parameters, so I measure on the actual target phones.

**꼬리질문**:
- Q: MTU가 커도 처리량이 안 오르면? → A: DLE가 꺼져 있으면 큰 ATT PDU가 27바이트 LL 패킷으로 쪼개질 뿐이다. 둘 다 확인한다. 또 central이 connection event 길이를 짧게 제한할 수 있다.
- Q: 오디오 스트리밍은 GATT로 하나? → A: 표준 방법은 BT 5.2의 LE Audio(Isochronous Channels + LC3 코덱)다. 독자 방식은 GATT로도 가능하지만 타이밍 보장이 없다.

### Q15. How does BLE pairing and bonding work, and how would you secure a consumer device?

**왜 묻나**: 보안 기본과 사용자 경험.

**30초 답변**: pairing은 키를 만드는 과정, bonding은 그 키(LTK, IRK 등)를 저장해 다음 연결에서 재사용하는 것이다. **LE Secure Connections**(BT 4.2)는 ECDH P-256으로 키를 합의해 수동 도청에 안전하다. legacy pairing의 Just Works는 도청에 취약하다. 인증 방식(association model)은 Just Works, Passkey Entry, Numeric Comparison, Out of Band이며, 기기의 입출력 능력으로 결정된다. 화면·키패드가 없는 웨어러블은 Just Works가 되기 쉬워 MITM 보호가 없으므로, 앱 레벨에서 기기 인증(공장 provisioning된 인증서/키로 challenge-response)을 추가한다. privacy는 IRK 기반 Resolvable Private Address로 추적을 막는다.

**English answer**: Pairing generates keys and bonding stores them — the long-term key and identity resolving key — so later reconnections are encrypted immediately. I'd require LE Secure Connections, which uses ECDH on P-256 and resists passive eavesdropping, unlike legacy Just Works pairing. The association model — Just Works, Passkey Entry, Numeric Comparison or out-of-band — follows from the I/O capabilities, and a screenless wearable often ends up with Just Works, which has no man-in-the-middle protection. So I'd add application-level device authentication using a key or certificate provisioned in the factory, and use resolvable private addresses so the device can't be tracked by its address.

**꼬리질문**:
- Q: bond 정보는 어디 저장하나? → A: 보호된 flash 영역(Zephyr는 settings 서브시스템). 공장 초기화와 폰 쪽 bond 삭제 시의 불일치 처리를 설계해야 한다.
- Q: OOB pairing 예는? → A: NFC 탭으로 키 재료를 교환. Hark 기기처럼 NFC가 있다면 후보다.

### Q16. Design the GATT interface for a wearable that streams sensor events and receives commands.

**왜 묻나**: 프로토콜 설계 감각.

**30초 답변**: 표준 서비스는 표준을 쓴다(Battery Service, Device Information Service). 독자 기능은 128비트 UUID 커스텀 서비스 하나에 characteristic을 목적별로 둔다. 예: `Command`(write/write without response, 앱 → 기기), `Event`(notify, 기기 → 앱, CCCD로 구독), `Status`(read + notify). 페이로드에 버전 필드와 TLV 구조를 넣어 펌웨어·앱을 독립적으로 업데이트할 수 있게 한다. 신뢰가 필요한 소수 메시지는 indication 또는 앱 레벨 ACK, 대량 스트림은 notification이다. 민감한 characteristic은 암호화된 링크를 요구하는 permission을 준다.

**English answer**: I'd use the standard Battery and Device Information services, and one custom 128-bit service for the product. Inside it: a Command characteristic the phone writes to, an Event characteristic the device notifies on — the phone subscribes through its CCCD — and a readable Status characteristic that also notifies on change. The payload is versioned and type-length-value encoded so firmware and app can evolve independently. Bulk streams use notifications, the few messages that must not be lost get an application-level acknowledgement, and sensitive characteristics require an encrypted, bonded link through their permissions.

**꼬리질문**:
- Q: notify와 indicate 차이는? → A: indication은 클라이언트가 ATT 레벨 confirmation을 보내야 다음 indication이 가능하다. notification은 확인이 없지만 LL 수준에서는 ACK/재전송된다(연결이 유지되는 한 손실은 주로 버퍼 오버플로에서 생김).
- Q: Service Changed characteristic은? → A: 펌웨어 업데이트로 GATT 구조가 바뀌면 bond된 클라이언트에 캐시 무효화를 알린다.

### Q17. How would you provision Wi-Fi credentials onto a headless device using BLE?

**왜 묻나**: 컨슈머 기기 첫 설정(out-of-box) 흐름. BLE와 Wi-Fi의 결합.

**30초 답변**: ① 기기가 설정 모드에서 provisioning 서비스를 advertising ② 앱이 연결·pairing(Secure Connections) 후 앱 레벨로 기기 인증 ③ 기기가 주변 AP를 스캔해 목록을 notify ④ 앱이 SSID/비밀번호를 **암호화된 링크 + 앱 레벨 암호화**로 write ⑤ 기기가 Wi-Fi 연결을 시도하고 단계별 상태(인증 실패, DHCP 실패, 인터넷 불가)를 notify ⑥ 성공하면 credential을 보호된 저장소에 커밋하고 BLE 설정 모드를 종료한다. 실패 원인별 에러 코드를 앱에 보여 주는 것이 사용자 경험의 핵심이다. 참고로 Espressif는 이 흐름을 구현한 공개 provisioning 프레임워크가 있고, Matter도 BLE로 commissioning한다.

**English answer**: The device advertises a provisioning service while it's in setup mode. The app connects and pairs with Secure Connections, then authenticates the device at the application level. The device scans for access points and notifies the list, the app writes the chosen SSID and passphrase — encrypted by the link and ideally again at the application layer — and the device attempts to join, reporting each step: authentication, DHCP, and reaching our cloud. Only after success does it commit the credentials to protected storage and leave setup mode. The part people underestimate is error reporting, because "wrong password" versus "no internet" versus "5 GHz-only network" needs a different message for the user.

**꼬리질문**:
- Q: 기기가 2.4 GHz만 지원하는데 사용자 AP가 5 GHz만 켜져 있으면? → A: 스캔 결과로 사전에 걸러 표시하고 명확한 에러를 준다.
- Q: 설정 모드를 영원히 열어 두면? → A: 공격 표면. 버튼·시간 제한으로만 열고, 이미 설정된 기기는 재설정에 사용자 행동을 요구한다.

---

## 4. Wi-Fi·Thread·통합

### Q18. How does a Wi-Fi station save power? Explain DTIM and TWT.

**왜 묻나**: Wi-Fi는 기기에서 가장 큰 전력 소비원 중 하나.

**30초 답변**: AP는 보통 **102.4 ms(100 TU)**마다 beacon을 보내고, beacon의 TIM에 "이 station에 버퍼된 unicast가 있다"를 표시한다. power save 모드의 station은 대부분 잠들고 beacon 때만 깨서 TIM을 확인한 뒤, 데이터가 있으면 PS-Poll 또는 U-APSD(WMM Power Save)로 받는다. **DTIM**은 브로드캐스트/멀티캐스트가 전달되는 beacon이며 주기는 AP가 정한다(DTIM period). station은 listen interval로 매 beacon 대신 DTIM마다 깨어 전력을 줄인다. **TWT**(802.11ax/Wi-Fi 6)는 AP와 station이 깨어날 시간을 협상해서 beacon과 무관하게 긴 sleep을 가능하게 한다. 대신 깨어나는 간격이 길수록 수신 레이턴시가 늘어난다.

**English answer**: The access point sends beacons, typically every 102.4 milliseconds, and each beacon's traffic indication map says whether it has buffered frames for a sleeping station. In power-save mode the station sleeps between beacons, wakes to read the TIM, and fetches its data with PS-Poll or U-APSD. Broadcast and multicast are delivered after DTIM beacons, whose period the AP sets, so a station that wakes only every DTIM, or on a longer listen interval, saves a lot of energy at the cost of latency. Wi-Fi 6 adds Target Wake Time, where the station and AP negotiate explicit wake schedules, allowing much longer sleep independent of beacons — if the AP supports it, which in a consumer's home is not guaranteed.

**꼬리질문**:
- Q: 멀티캐스트 트래픽이 많은 네트워크에서 전력이 늘어나는 이유는? → A: DTIM마다 수신해야 하는 브로드캐스트(ARP, mDNS 등)가 많아서. 칩 펌웨어의 ARP offload, 멀티캐스트 필터가 도움이 된다(벤더마다 다름).
- Q: TCP keepalive와 Wi-Fi 전력은? → A: 서버 keepalive 간격이 짧으면 계속 깨어난다. NAT 타임아웃과 전력 사이 균형을 잡는다.

### Q19. What happens between "connect to this SSID" and having an IP address? Where do failures occur?

**왜 묻나**: 필드 연결 문제 디버깅 능력.

**30초 답변**: ① 스캔(passive로 beacon 수신 또는 active probe request) ② 802.11 authentication(Open System, WPA3에서는 SAE 핸드셰이크) ③ association(능력 협상, listen interval 전달) ④ WPA2/WPA3의 **4-way handshake**로 PMK에서 PTK(unicast 키)를 유도하고 GTK(그룹 키)를 전달 ⑤ DHCP로 IP 획득 ⑥ DNS/클라우드 연결. 실패 지점별 원인: 4-way handshake 실패는 대부분 비밀번호 오류, association 거절은 능력 불일치나 AP 정책, DHCP 타임아웃은 네트워크/전력 절약 중 패킷 손실. WPA3는 PMF(Protected Management Frames)가 필수다.

**English answer**: The station scans, either passively for beacons or actively with probe requests, then does 802.11 authentication — open system for WPA2, or the SAE exchange for WPA3 — then association, where capabilities and the listen interval are agreed. The four-way handshake derives the pairwise key from the PMK and delivers the group key, and then DHCP gives it an address, followed by DNS and the cloud connection. Each step fails differently: a four-way handshake failure is almost always a wrong passphrase, association rejections point at capability or policy mismatches, and DHCP timeouts are often packet loss or power-save interactions. So I make the connection manager report which step failed, with the reason codes, into telemetry.

**꼬리질문**:
- Q: 로밍이 잦은 환경에서는? → A: 802.11r(fast transition), 802.11k/v(neighbor report, BSS transition) 지원 여부가 재연결 시간과 전력에 영향을 준다.
- Q: WPA3-SAE가 WPA2-PSK보다 나은 점은? → A: 오프라인 사전 공격에 강하고 forward secrecy를 제공한다.

### Q20. What is Thread, how is it different from BLE and Wi-Fi, and how does Matter relate?

**왜 묻나**: JD에 Thread 명시. 개념 정확성.

**30초 답변**: Thread는 **IEEE 802.15.4**(2.4 GHz, 250 kbps, 채널 11~26) 위의 저전력 **IPv6 메시** 네트워크다. 6LoWPAN으로 IPv6를 127바이트 프레임에 압축·분할한다. 역할은 Leader, Router, REED(Router-eligible End Device), End Device(MED, SED, Thread 1.2의 SSED)이고, Border Router가 Thread와 Wi-Fi/Ethernet IP 네트워크를 잇는다. 단일 장애점이 없는 self-healing 메시다. BLE는 점대점/소규모 저전력, Wi-Fi는 고대역폭·고전력이라면 Thread는 저전력 다중 홉 IP 네트워크다. **Matter**는 그 위의 애플리케이션 계층 표준으로, Thread·Wi-Fi·Ethernet 위에서 돌고 설정(commissioning)에 BLE를 쓴다. 대표 오픈소스 구현은 OpenThread다.

**English answer**: Thread is a low-power IPv6 mesh network on IEEE 802.15.4 — 2.4 gigahertz, 250 kilobits per second — using 6LoWPAN to compress and fragment IPv6 into 127-byte frames. Devices take roles as leader, routers, router-eligible end devices, and end devices, including sleepy end devices that poll their parent, and a border router connects the mesh to Wi-Fi or Ethernet. Compared with BLE, it's a multi-hop IP network with no single point of failure; compared with Wi-Fi, it's much lower power and bandwidth. Matter is the application layer on top: it runs over Thread, Wi-Fi or Ethernet and uses BLE for commissioning. The common open-source stack is OpenThread, which Zephyr and several vendor SDKs integrate.

**꼬리질문**:
- Q: sleepy end device는 어떻게 메시지를 받나? → A: 부모 라우터가 버퍼링하고 SED가 주기적으로 data poll을 보내 가져간다. Thread 1.2의 CSL(Coordinated Sampled Listening)은 SSED가 짧은 주기로 샘플링 수신한다.
- Q: 웨어러블에 Thread가 필요할까? → A: 스마트홈 제어(Matter 기기)가 핵심 기능이면 의미가 있지만, 폰 동반 웨어러블은 보통 BLE + Wi-Fi가 중심이다. 제품 요구에 따라 판단한다.

### Q21. The BLE/Wi-Fi radio is a separate combo chip. What does the host firmware have to do, and what typically breaks at bring-up?

**왜 묻나**: Don의 Apple 경험을 가장 직접적으로 쓸 수 있는 질문.

**30초 답변**: 호스트는 ① 전원·리셋·enable 핀 시퀀스와 32 kHz sleep clock 공급 ② 칩 펌웨어/패치 다운로드(BT는 HCI vendor-specific 명령, Wi-Fi는 SDIO/PCIe/SPI 경로, 방식은 벤더마다 다름) ③ 캘리브레이션·보드 설정 데이터(안테나, TX power 테이블, 국가 코드) 로드 ④ HCI 전송(UART H4면 packet indicator 0x01 command, 0x02 ACL, 0x04 event 등, RTS/CTS 필수) ⑤ host wake/device wake 사이드밴드 GPIO로 저전력 협조를 한다. bring-up에서 흔한 문제: 전원 시퀀스 타이밍, sleep clock 누락/정확도, UART baud 전환 후 동기 상실, flow control 미연결, 펌웨어 다운로드 중 타임아웃, wake 핀 극성.

**English answer**: The host is responsible for the power, reset and enable sequencing and for supplying the 32 kilohertz sleep clock; for downloading the radio firmware or patches — over HCI vendor commands for Bluetooth and over SDIO, PCIe or SPI for Wi-Fi, depending on the vendor; for loading board-specific calibration like antenna configuration, TX power tables and country code; for running the HCI transport, which on UART is H4 with hardware flow control; and for the host-wake and device-wake sideband signals that let both sides sleep. At bring-up the classic failures are sequencing timing, a missing or inaccurate sleep clock, losing sync after switching to a higher baud rate, flow-control lines not wired, and wake-line polarity. I debug them the same way: capture the bus and GPIOs together on a logic analyzer and compare with the vendor's sequence diagram.

**꼬리질문**:
- Q: HCI 트래픽을 어떻게 보나? → A: 호스트에서 HCI 로그를 btsnoop 포맷으로 남겨 Wireshark로 보거나, UART를 LA로 캡처해 디코드한다.
- Q: 칩이 가끔 응답하지 않으면? → A: 명령 타임아웃 → 로그 수집 → 칩 리셋·재다운로드 복구 경로를 구현하고 발생 횟수를 텔레메트리로 센다.

**Don 스토리 연결**: 핵심 어필 질문이다. Apple에서 새 무선 칩을 플랫폼에 올리며 PCIe/I2C/SPMI/RFFE 인터페이스 장애를 root cause한 사례를 STAR로 말한다(기밀 세부사항은 일반화해서).

### Q22. Wi-Fi and Bluetooth share a 2.4 GHz antenna. How does coexistence work, and what does firmware care about?

**왜 묻나**: 멀티 라디오 기기(Hark: Cellular, Wi-Fi, BT, GNSS, NFC, UWB)의 통합 이슈.

**30초 답변**: 같은 대역·같은 안테나라 동시에 송수신하면 서로 방해한다. 콤보칩 내부에서는 공존 중재기가 시간 분할로 우선순위를 정하고, 칩이 분리돼 있으면 **PTA**(Packet Traffic Arbitration) 신호선(REQUEST, PRIORITY, GRANT 같은 신호, 이름과 방식은 벤더마다 다름)으로 중재한다. BLE는 적응형 주파수 호핑(AFH, channel map)으로 Wi-Fi 채널을 피한다. 셀룰러 일부 대역(예: 2.4 GHz 근처 LTE 대역)과도 간섭이 있어 필터와 공존 인터페이스가 필요하다. 펌웨어는 우선순위 정책(오디오·연결 유지 이벤트 우선), 공존 통계, 그리고 기능 시나리오별(스트리밍 + Wi-Fi 업로드 동시) 처리량/끊김 테스트를 챙긴다.

**English answer**: Transmitting on one radio while the other receives on the same band and antenna desensitizes or blocks it, so traffic has to be arbitrated. Inside a combo chip the vendor's coexistence engine time-shares the antenna; between separate chips you wire packet traffic arbitration signals — request, priority and grant, with vendor-specific naming — so one side can ask for the medium. Bluetooth also uses adaptive frequency hopping to avoid the channels Wi-Fi occupies, and some cellular bands near 2.4 gigahertz need filtering and their own coexistence signaling. Firmware's job is the priority policy — for example, protecting connection events and audio over bulk Wi-Fi — plus exposing coexistence statistics and testing the concurrent scenarios, since most field complaints come from combinations like streaming while uploading.

**꼬리질문**:
- Q: 공존 문제의 전형적 증상은? → A: Wi-Fi 트래픽이 많을 때만 BLE 연결 끊김(supervision timeout), 오디오 끊김, Wi-Fi 처리량 급락.
- Q: 어떻게 증명하나? → A: 한 라디오를 끄거나 트래픽을 제어하면서 재현하고, PTA 신호와 라디오 활동을 LA로 같이 캡처한다.

**Don 스토리 연결**: Apple 무선 칩셋 통합 그룹에서 본 멀티 라디오 통합 이슈(직접 다룬 사례가 있을 때만, 세부 내용은 일반화). "multi-radio 제품에서 공존은 칩 하나가 아니라 시스템 문제"라는 관점을 말한다.

### Q23. Users report the device randomly disconnects from their phone. How do you debug it?

**왜 묻나**: 필드 무선 디버깅 = JD의 HW-SW 디버깅 + 무선.

**30초 답변**: 먼저 데이터를 모은다. 펌웨어가 disconnect reason 코드, RSSI, 연결 파라미터, 재시도 횟수, 공존 상태를 텔레메트리로 기록하게 한다. reason이 **0x08 Connection Timeout**이면 RF 링크 문제(거리, 몸에 의한 감쇠, 안테나, 공존, 클럭 정확도), **0x13 Remote User Terminated**면 폰 쪽이 끊은 것(앱/OS 정책), **0x3E Connection Failed to be Established**면 연결 직후 동기 실패, **0x3D MIC Failure**면 암호화 키 불일치다. 재현은 에어 스니퍼(nRF Sniffer, Ellisys 등)와 HCI 로그를 같이 본다. sleep clock(32 kHz) 정확도가 나쁘면 window widening이 부족해 timeout이 날 수 있다.

**English answer**: I'd make sure the firmware records disconnect reason codes along with RSSI, connection parameters and coexistence state, and bucket the field data by reason. Connection timeout, 0x08, means the link was lost — range, body blocking, antenna, coexistence, or a sleep clock outside its accuracy spec so the receive window misses the anchor. Remote user terminated, 0x13, means the phone chose to disconnect, so we look at the OS and app side. 0x3E means it failed right after establishment, and a MIC failure points at encryption keys. Then I reproduce with an air sniffer and HCI logs in the same scenario — on-body, in a pocket, with Wi-Fi busy — rather than on a bench with a clear line of sight.

**꼬리질문**:
- Q: sleep clock 정확도는 왜 중요한가? → A: 링크 레이어가 SCA(sleep clock accuracy, ppm)를 교환하고 그만큼 수신 윈도를 넓힌다. 실제 오차가 선언값보다 크면 anchor를 놓친다.
- Q: 끊긴 뒤 재연결 전략은? → A: 빠른 advertising으로 잠시 재연결을 시도하고 점차 간격을 늘린다. 무한 고속 재시도는 배터리를 소모한다.

**Don 스토리 연결**: "재현 안 되는 필드 이슈는 텔레메트리 설계부터"라는 SSD NVMe telemetry 경험과 Apple의 통합 root cause 경험을 결합해 답한다.

---

## 5. 계산 연습

모든 전류 값은 **가정값**이다. 면접에서는 "Let me assume ..."로 시작하고, 가정을 적고, 단위를 끝까지 들고 간다.

### E1. Battery life of an always-on voice wearable

**문제**: 400 mAh Li-ion 배터리의 웨어러블. 하루 동안 다음이 일어난다. 배터리 수명(일)을 추정하고, 가장 효과적인 개선 레버를 고르라.

| 항목 | 가정 전류 | 시간 비율/사용량 |
|---|---|---|
| MCU sleep + PDM 마이크 + 저전력 VAD | 0.5 mA | 항상(100%) |
| wake word 추론 (VAD가 소리 감지 시) | +3 mA | 하루의 10% |
| BLE 연결 유지 (E2 결과 사용) | 0.1 mA | 항상 |
| SoC 세션 (음성 질의, Wi-Fi/셀룰러 포함) | 150 mA | 하루 20회 × 30초 |
| PMIC/fuel gauge/센서 대기 | 0.03 mA | 항상 |

**풀이**:

1. 사용 가능 용량: 노화·저온·컷오프 여유로 85%만 쓴다고 가정 → 400 × 0.85 = **340 mAh**.
2. 항상 켜진 항목: 0.5 + 0.1 + 0.03 = **0.63 mA**.
3. wake word 추론 평균: 3 mA × 0.10 = **0.30 mA**.
4. SoC 평균: 하루 SoC 시간 = 20 × 30 s = 600 s. 하루 = 86,400 s. 비율 = 600 / 86,400 = 0.00694. 평균 = 150 × 0.00694 = **1.04 mA**.
5. 총 평균 전류 = 0.63 + 0.30 + 1.04 = **1.97 mA**.
6. 수명 = 340 / 1.97 = 172.6 h → **약 7.2일**.

```
평균 전류 구성 (총 1.97 mA)
SoC 세션        ██████████████████████████  1.04 mA (53%)
always-on 합계  ████████████████            0.63 mA (32%)
wake word 추론  ████████                    0.30 mA (15%)
```

**해석**: SoC가 절반 이상이다. 레버 순서는 ① SoC 세션 에너지(세션 길이 단축, 빠른 suspend, 가벼운 요청은 MCU/BLE로 처리) ② false wake 감소(VAD 트리거 비율 10% → 5%면 0.15 mA 절약) ③ always-on floor. 예를 들어 SoC 세션을 20초로 줄이면 1.04 → 0.69 mA, 총 1.62 mA → 340 / 1.62 = 210 h(약 8.7일)이다.

**English (말하기)**: "Average current is the sum of each state's current times its duty cycle. The SoC is on for 600 seconds a day at 150 mA, which averages to about 1 mA — more than half of the 2 mA total — so SoC session energy is the first lever, then false wake-ups, then the sleep floor."

### E2. BLE average current from the connection interval

**문제**: peripheral이 연결을 유지하며 보낼 데이터가 없다(빈 패킷 교환). connection event 하나의 전하와 sleep 전류를 가정하고, interval 7.5 ms, 30 ms, 100 ms, 1 s, 그리고 100 ms + peripheral latency 9에서 평균 전류를 구하라.

**가정**(칩마다 다름):

| 단계 | 전류 | 시간 | 전하 |
|---|---|---|---|
| 크리스털(HFXO) 기동 + CPU/스택 처리 | 2.0 mA | 0.5 ms | 1.0 µC |
| RX (central 패킷 수신 대기·수신) | 5.0 mA | 0.2 ms | 1.0 µC |
| TX (응답 패킷) | 5.0 mA | 0.2 ms | 1.0 µC |
| 합계 Q_event | | | **3.0 µC** |

sleep 전류(RTC + RAM 리텐션): **2 µA**.

**풀이**: 평균 전류 = Q_event × (초당 이벤트 수) + I_sleep. (µC × 1/s = µA. sleep 중 비율은 거의 100%라 근사한다.)

| interval | 이벤트/s | 라디오 평균 | 총 평균 |
|---|---|---|---|
| 7.5 ms | 133.3 | 3.0 × 133.3 = 400 µA | **402 µA** |
| 30 ms | 33.3 | 100 µA | **102 µA** |
| 100 ms | 10 | 30 µA | **32 µA** |
| 1 s | 1 | 3 µA | **5 µA** |
| 100 ms, latency 9 | 1 (idle 시) | 3 µA | **5 µA** |

**검산 — 패킷 air time(LE 1M PHY, 1 µs/bit)**: 빈 패킷 = preamble 1 + access address 4 + header 2 + payload 0 + CRC 3 = 10바이트 = **80 µs**. 20바이트 notification(ATT 3 + L2CAP 4 + 데이터 20 = LL payload 27바이트) = 37바이트 = **296 µs**. 그래서 데이터를 보내면 TX 시간이 약 0.3 ms로 늘어 Q_event가 조금 커진다(여기에 IFS 150 µs 등 추가).

**해석**: interval을 7.5 ms → 1 s로 늘리면 약 80배 줄어든다. 하지만 latency 9를 쓰면 **peripheral → central** 방향은 100 ms 응답성을 유지하면서 idle 전력은 1 s interval 수준이 된다. 반대로 central → peripheral 명령은 최대 약 1 s 늦을 수 있다. 또 interval이 길면 sleep floor(2 µA)가 총합에서 무시할 수 없는 비중이 된다(1 s일 때 40%).

**English (말하기)**: "Each connection event costs a roughly fixed charge — say 3 microcoulombs for crystal start-up, receive and transmit — so the radio's average is charge times event rate: 30 microamps at 100 milliseconds, 3 at one second. Peripheral latency gets me the one-second idle cost while keeping 100-millisecond response when the device has data to send."

### E3. Duty-cycled sensor sampling — per-sample wake versus FIFO batching

**문제**: 가속도계를 50 Hz로 샘플링(샘플당 6바이트, X/Y/Z 16비트). I2C 400 kHz. MCU active 2 mA, MCU sleep 3 µA, CPU가 sleep하고 DMA만 동작하는 모드 0.5 mA, wake + ISR 진입/처리 오버헤드 50 µs(active). 센서 자체 전류는 두 방식이 같으므로 제외한다. 다음 세 방식의 MCU 평균 전류를 비교하라.

**I2C 전송 시간 계산**: I2C는 바이트당 9비트(8 데이터 + ACK). 레지스터 읽기 트랜잭션 = 주소(W) 1 + 레지스터 1 + 주소(R) 1 + 데이터 N바이트. START/STOP 등은 무시하는 근사.

**방식 A — 샘플마다 깨어남**

1. 전송 바이트 = 3 + 6 = 9바이트 → 81비트 / 400 kHz = **203 µs**.
2. 샘플당 active 시간 = 50 + 203 = 253 µs, 전하 = 2 mA × 0.253 ms = **0.506 µC**.
3. 초당 50번 → 25.3 µA. sleep 3 µA 더하면 **약 28 µA**.

**방식 B — 센서 FIFO watermark 25샘플, CPU가 폴링으로 버스트 읽기**

1. 초당 2번 깸. 한 번에 3 + 25 × 6 = 153바이트 → 1,377비트 / 400 kHz = **3.44 ms**.
2. 버스트당 전하 = 2 mA × (0.05 + 3.44) ms = **6.98 µC**.
3. 초당 2번 → 14.0 µA. sleep 더해 총 **약 17 µA**.

**방식 C — FIFO batching + DMA로 읽고 CPU는 sleep**

1. 버스트당 전하 = 2 mA × 0.05 ms(깨어나 DMA 설정·완료 처리) + 0.5 mA × 3.44 ms = 0.1 + 1.72 = **1.82 µC**.
2. 초당 2번 → 3.64 µA. sleep 더해 총 **약 6.6 µA**.

| 방식 | 초당 wake | MCU 평균 | 개선 |
|---|---|---|---|
| A 샘플마다 | 50 | 28 µA | 기준 |
| B FIFO + CPU 폴링 | 2 | 17 µA | 1.6배 |
| C FIFO + DMA | 2 | 6.6 µA | 4.2배 |

**해석**: batching은 wake 오버헤드(50 µs × 50번)를 없애지만, 버스 전송 시간 자체는 그대로라 B만으로는 개선이 제한적이다. 전송 중 CPU를 재우는 DMA(C)가 결정적이다. 추가 레버: SPI(예: 8 MHz면 153바이트 × 8비트 / 8 MHz = 153 µs), 필요 없는 축·해상도 줄이기, 센서 내장 기능(wake-on-motion, 스텝 카운터)으로 MCU가 아예 안 깨게 하기. 트레이드오프는 **레이턴시**다. 25샘플 batching은 데이터가 최대 500 ms 늦게 도착한다.

**English (말하기)**: "Per-sample wake-ups cost 50 fixed overheads a second; batching with the sensor FIFO removes those, but the I2C transfer time stays the same, so the real win comes from letting DMA move the burst while the CPU sleeps — about four times lower here. The price is up to half a second of latency, which is fine for activity tracking but not for gesture detection."

### E4. Wi-Fi power-save — DTIM period versus average current

**문제**: 연결 유지 중인 Wi-Fi station. beacon 간격 102.4 ms. 한 번 깨어나 beacon을 받고 다시 잘 때까지의 전하를 "평균 60 mA × 2.5 ms = 150 µC"로 가정한다(칩마다 크게 다름). sleep 전류 15 µA. DTIM 1, 3, 10마다 깨는 경우 평균 전류와, 1,000 mAh 배터리 기준 Wi-Fi 유지만의 수명을 구하라.

**풀이**: 깨는 주기 = 102.4 ms × N. 평균 = 150 µC / 주기 + 15 µA.

| 깨는 주기 | 초당 wake | Wi-Fi 평균 | 1,000 mAh 기준 |
|---|---|---|---|
| DTIM 1 (102.4 ms) | 9.77 | 1,465 + 15 = **1.48 mA** | 676 h ≈ 28일 |
| DTIM 3 (307.2 ms) | 3.26 | 488 + 15 = **0.50 mA** | 2,000 h ≈ 83일 |
| DTIM 10 (1,024 ms) | 0.98 | 146 + 15 = **0.16 mA** | 6,250 h ≈ 260일 |

계산 예: 150 µC / 0.1024 s = 1,465 µA = 1.465 mA.

**해석**: 이상적 조건 계산이다. 실제로는 브로드캐스트 트래픽, keepalive, 재연결, 스캔이 더해져 훨씬 커진다. 긴 주기는 수신 레이턴시(푸시 명령이 최대 약 1 s 지연)와 AP 호환성(버퍼 보관 한계로 일부 AP는 긴 listen interval에서 패킷을 버림) 문제가 있다. 그래서 "SoC가 자는 동안 Wi-Fi를 유지할지, 끊고 BLE로 폰에 기대고 필요할 때 재연결할지"가 시스템 설계 질문이 된다(재연결 에너지 vs 유지 에너지 비교).

**English (말하기)**: "Staying associated costs one beacon reception per wake-up — about 150 microcoulombs here — so waking every DTIM of 3 instead of every beacon cuts the average from about 1.5 to 0.5 milliamps. The real design question is whether to keep Wi-Fi associated at all while the SoC sleeps, or rely on BLE and pay the reconnection cost only when needed."

---

## 6. 최종 점검

- [ ] "측정 → 예산 → 큰 항목부터 → 재측정" 방법론을 영어로 30초에 말할 수 있다
- [ ] WFI/WFE, SLEEPDEEP, SLEEPONEXIT, WFI 전 DSB의 이유를 설명할 수 있다
- [ ] P = α·C·V²·f와 누설 전력의 온도 의존성으로 DVFS와 race-to-idle을 설명할 수 있다
- [ ] sleep 전류 누설 원인 5가지 이상을 말하고, pull-up 전류를 즉석 계산할 수 있다
- [ ] BLE 스택 계층을 PHY부터 GAP까지 화이트보드에 그릴 수 있다
- [ ] advertising interval(20 ms ~ 10.24 s), connection interval(7.5 ms ~ 4 s), latency(0~499), supervision timeout(100 ms ~ 32 s)을 외워 말할 수 있다
- [ ] MTU(23 → 517), DLE(27 → 251), 2M PHY로 처리량을 올리는 방법을 설명할 수 있다
- [ ] Wi-Fi 연결 6단계와 단계별 실패 원인, DTIM/TWT를 설명할 수 있다
- [ ] Thread 역할과 Matter와의 관계를 1분 안에 말할 수 있다
- [ ] E1~E4를 노트 없이 다시 계산할 수 있다

| 자주 틀리는 포인트 | 올바른 내용 |
|---|---|
| "peripheral이 connection interval을 정한다" | central이 정한다. peripheral은 요청만 |
| "peripheral latency면 양방향 모두 저전력 + 빠른 응답" | central → peripheral 방향은 늦어진다 |
| "MTU만 늘리면 처리량이 오른다" | DLE가 없으면 27바이트 LL 패킷으로 쪼개진다 |
| "Just Works도 안전하다" | MITM 보호 없음. legacy Just Works는 도청에도 취약 |
| "deep sleep이 무엇을 끄는지는 ARM이 정한다" | SLEEPDEEP 비트만 ARM, 실제 동작은 벤더 정의 |
| "멀티미터로 평균 전류 측정" | 동적 범위와 샘플링 문제. auto-ranging 프로파일러로 적분 |
| "주파수를 낮추면 에너지가 준다" | 전압을 같이 낮출 때만 크게 준다 |
| "Thread = 저전력 BLE 메시" | 802.15.4 기반 IPv6 메시. BLE와 다른 PHY/MAC |
| "DTIM이 길수록 항상 좋다" | 레이턴시 증가, AP 버퍼 한계로 패킷 손실 가능 |

## 참고 자료

- [Bluetooth Core Specification](https://www.bluetooth.com/specifications/specs/core-specification/)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/)
- [Zephyr Bluetooth documentation](https://docs.zephyrproject.org/latest/connectivity/bluetooth/index.html)
- [OpenThread](https://openthread.io/)
- [Nordic Power Profiler Kit II](https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2)
