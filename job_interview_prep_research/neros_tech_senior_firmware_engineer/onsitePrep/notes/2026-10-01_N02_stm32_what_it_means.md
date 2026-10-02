# 🔩 N02 · "STM을 쓴다"는 것의 의미 — 자체 실리콘 없음, STM32 위의 펌웨어

> Michael이 "STM을 사용한다"고 했다. STMicroelectronics의 **STM32 MCU(ARM Cortex-M)** 를 쓴다는 뜻이고, Neros는 **자체 칩을 만들지 않는다**. 이게 회사 스택과 면접에 주는 의미, Don의 실리콘 경험을 어떻게 번역할지, 온사이트 전에 알아 둘 STM32 기본기를 정리했다.

## 1. 한 문장 정리

**Neros는 칩 회사가 아니라 상용 부품(COTS)을 골라 보드를 만들고, 그 위에서 펌웨어로 차별화하는 회사다.** FC·라디오·핸드셋은 STM32, 자율비행은 상용 embedded Linux 모듈(Jetson급 [추정]), 필요한 곳에 FPGA.

- 근거: Michael 10-01 [확인됨], Tennessee FW 공고 "STM32 family of microcontrollers" 필수 [확인됨], Ukraine Senior FW 공고 "ARM Cortex-M processors - e.g. STM32" 우대 [확인됨], Autonomy 공고 "NVIDIA Jetson … Qualcomm … TI … Hailo or equivalent" [확인됨: 후보 목록이지 확정 아님]
- 정확히 어떤 STM32 패밀리(F4, F7, H7, G4 …)인지는 모른다 [미확인]. 온사이트에서 물어볼 좋은 질문이다

## 2. STM32가 무엇인가

| 항목 | 내용 |
|---|---|
| 제조사 | STMicroelectronics (유럽, 프랑스·이탈리아 합작) |
| 코어 | ARM Cortex-M0/M0+ (저가), M3, **M4F**(FPU+DSP), **M7**(캐시, 고성능), M33(보안 TrustZone), H7 일부는 M7+M4 듀얼 코어 |
| 대표 시리즈 | F0/G0(저가) · F1 · **F4**(가장 흔함) · **F7** · **H7**(최고 성능) · **G4**(모터 제어·아날로그) · L/U(저전력) |
| FC에서 흔한 칩 | Betaflight 타깃 기준 **STM32F405, F411, F722, F745, H743, H750, G473** 등. 커뮤니티에는 중국 Artery의 AT32F435도 있다 |
| 개발 도구 | STM32CubeMX(핀·클럭 설정 생성), STM32CubeIDE, HAL(고수준) / LL(저수준) 라이브러리, CMSIS, ST-LINK·J-Link 디버거(SWD) |

- **"중국산 부품 없음"과 연결** [추정]: Neros는 중국산 부품이 없다는 걸 내세운다(Blue UAS). FC 커뮤니티에서 흔한 AT32(중국 Artery) 대신 ST(유럽)를 쓰는 것도 이 공급망 정책과 맞는다

## 3. 자체 실리콘이 없다는 것의 의미

### 회사 쪽

- 하드웨어 차별화는 **보드 설계, RF 설계, 부품 선택, 제조**에서 나오고, 칩 안의 차별화는 없다
- 펌웨어가 차별화의 큰 축이다: 비행 성능, 재밍 내성 링크, 자율비행, 제조 효율
- **부품 공급과 second-source가 핵심 리스크**다. 같은 기능을 다른 MCU나 센서로 바꾼 보드 변형이 계속 생긴다 → HAL 추상화, 보드 변형 테스트, factory test가 중요해진다
- 칩 버그(errata)는 **고칠 수 없고 피해 가야** 한다 → ST errata sheet를 읽고 펌웨어로 우회하는 능력

### Don 쪽 — 실리콘 경험을 어떻게 번역하나

| Don이 해 온 것 | Neros에서의 형태 |
|---|---|
| 새 실리콘 bring-up (FPGA → ASIC) | **새 보드 revision bring-up**: 클럭, 전원, 각 버스, 주변장치를 하나씩 살리기 |
| 칩 validation이 놓친 결함을 시스템에서 찾기 (Apple) | 상용 칩 + 우리 보드 + 우리 FW 조합에서만 나는 문제: errata, 타이밍, 전원, 노이즈 |
| PCIe, SerDes, 고속 인터페이스 | MCU 쪽에선 거의 안 쓴다. **Jetson급 컴패니언(PCIe·NVMe), FPGA 비디오, 고속 MCU↔FPGA 링크**에서 쓸 수 있다 [추정] |
| SoC verification (I2C, SPI, DMA, SRAM/DRAM) | STM32 주변장치 드라이버와 그 검증. **개념이 그대로 같다** |
| HW safety margin sign-off | 상용 부품의 마진 확인: 전압 범위, 온도, 진동, 모터 노이즈 |
| factory test-node (Apple), SSD MFG FW | **factory test FW가 아직 없다** [확인됨 Michael] → 가장 직접적인 기여 지점 |

> **영어 브리지**: "I've spent my career on the other side — bringing up new silicon and finding where it breaks in a real system. With COTS parts like STM32 you can't fix the chip, so the same skills turn into board bring-up, working around errata, qualifying second-source parts, and building the factory and HIL tests that catch board-level defects before they ship."

### "STM32 써 봤어요?"라는 질문이 오면

- 레쥬메에 STM32는 없다. **정직하게, 그리고 바로 가까운 경험으로**:

> "Not STM32 specifically in production — my Cortex-M work was the M0+ inside our SSD controller, alongside Cortex-R8 and R82, plus Xtensa. But the concepts are the same: NVIC priorities, DMA, timers, UART, SPI, I2C, linker scripts, startup code. On STM32 I'd expect to ramp on the HAL and LL libraries and the DMA stream mapping quickly."

- (Don: 개인적으로 STM32 Nucleo/Discovery 보드나 Blue Pill을 써 본 적이 있다면 여기에 한 줄. 없으면 말하지 않는다)
- 온사이트 전에 할 수 있다면: **Nucleo 보드 하나로 UART DMA 수신 + 타이머 PWM**을 한 번 해 보는 것만으로도 "써 봤다"고 말할 수 있다. 시간이 없으면 아래 4절 개념만

## 4. 온사이트 전에 알아 둘 STM32 기본기

### 4.1 소프트웨어 계층

| 계층 | 무엇 | 언제 |
|---|---|---|
| 레지스터 직접 (CMSIS 헤더) | `USART1->DR`, `GPIOA->BSRR` | 가장 빠르고 작다. Betaflight 일부, 타이밍 critical |
| **LL** (Low-Layer) | 레지스터를 얇게 감싼 inline 함수 | 성능과 가독성의 중간 |
| **HAL** | 상태 머신, 콜백, 타임아웃이 있는 고수준 API | 빠른 개발, 오버헤드와 숨은 동작이 있다 |
| CubeMX 생성 코드 | 핀·클럭·주변장치 초기화 자동 생성 | 시작은 빠르지만 생성 코드 관리가 골칫거리 |

- 면접 포인트: "HAL vs LL?" → "HAL for bring-up speed, LL or registers for hot paths like the gyro loop and DShot, because HAL adds locking, state checks, and callbacks that cost cycles and hide timing."

### 4.2 부팅과 메모리

- 리셋 → 벡터 테이블(0x0800_0000 플래시) → `Reset_Handler`(startup 파일) → `.data` 복사, `.bss` 0 초기화 → `SystemInit`(클럭) → `main`
- **링커 스크립트**: FLASH, RAM, (F4/F7/H7은) CCM RAM·DTCM 같은 특수 RAM. 빠른 데이터를 어디에 둘지
- **F4 플래시 섹터는 크기가 다르다** (F405: 16K×4, 64K×1, 128K×7) → 설정 저장 영역을 작은 섹터에 둔다. erase 단위가 크다는 걸 설계에 반영
- **시스템 부트로더(ROM)**: BOOT0 핀으로 진입, UART나 USB **DFU**로 플래싱. Betaflight도 DFU로 플래싱한다 → 공장 플래싱 방식과 연결
- **Option bytes**와 **RDP(Read-out Protection) 레벨 0/1/2**: 레벨 1은 디버거로 플래시 읽기 차단(해제 시 전체 삭제), 레벨 2는 영구 잠금(디버그 포트 비활성) → **방산 제품의 펌웨어 보호와 factory test 순서에 직결**: RDP를 올리기 전에 테스트와 provisioning을 끝내야 한다

### 4.3 인터럽트와 DMA

- **NVIC**: 우선순위 그룹(preemption / sub-priority). 자이로 데이터 ready 같은 critical 인터럽트를 가장 높게
- **DMA**: F4/F7은 DMA1·DMA2에 **스트림과 채널** 매핑이 고정 표로 정해져 있다 (어떤 주변장치가 어떤 스트림을 쓸 수 있는지). H7은 **DMAMUX**로 유연해졌다 → "DMA 스트림 충돌"이 FC 보드 설계의 흔한 제약
- **UART 수신 패턴**: DMA circular 모드 + **IDLE line 인터럽트** + half/full transfer 인터럽트 → 가변 길이 프레임(CRSF 등)을 CPU 부담 없이 받기 (`../code/dma_circular_rx.c`)
- **Cortex-M7(F7/H7) 캐시와 DMA**: D-cache가 켜져 있으면 DMA 버퍼가 캐시와 어긋난다 → 캐시 clean/invalidate, 또는 non-cacheable 영역(MPU 설정)에 DMA 버퍼 배치. **면접 단골**

### 4.4 타이머 — 드론에서 가장 바쁜 주변장치

- **PWM**: 서보, 아날로그 ESC, LED
- **DShot 출력**: 타이머 + DMA로 비트마다 duty를 바꿔 디지털 프레임을 만든다. Betaflight는 **GPIO bitbang + DMA 방식**도 쓴다 (DShot 600 = 비트당 약 1.67 µs)
- **Input capture**: RC 입력, bidirectional DShot의 eRPM 텔레메트리 수신
- **스케줄러 tick**과 루프 타이밍

### 4.5 그 밖에

- **RCC 클럭 트리**: HSE(외부 크리스털) → PLL → SYSCLK. HSE 불량이면 HSI로 떨어지고 **UART baud가 틀어진다** → 디버깅 단골
- **워치독**: IWDG(독립 클럭, 끌 수 없음), WWDG(윈도우)
- **리셋 원인**: `RCC_CSR` 플래그 (brownout, pin, POR, software, IWDG, WWDG, low-power) → 부팅마다 기록
- **Backup SRAM / RTC backup 레지스터**: 리셋을 넘어 남는 작은 저장소 → 크래시 정보 보관
- **디버그**: SWD(2선), ST-LINK, J-Link, **SWO/ITM 트레이스**(printf보다 훨씬 덜 침습적), Trace32도 지원 → Don의 Trace32 경험이 그대로 쓰인다

## 5. 면접에서 나올 수 있는 STM32 질문과 답

| 질문 | 30초 답 방향 |
|---|---|
| "How would you receive variable-length UART frames on STM32?" | DMA circular + IDLE line interrupt + half/full callbacks, 소비자는 DMA 위치(NDTR)로 새 데이터 범위 계산, wrap 처리 |
| "HAL or LL?" | bring-up은 HAL, 핫패스는 LL·레지스터. HAL의 잠금·콜백·타임아웃 비용 |
| "DMA buffer gives stale data on an M7" | D-cache 일관성. clean before TX DMA, invalidate after RX DMA, 32바이트 정렬, 또는 MPU로 non-cacheable |
| "UART baud is slightly off on some boards" | 클럭 소스 확인: HSE 실패로 HSI fallback, PLL 설정, 크리스털 부하 커패시터 |
| "How do you protect firmware on a fielded drone?" | RDP 레벨(1 vs 2의 trade-off), 서명된 업데이트, 디버그 포트 정책, provisioning 순서 |
| "Where do you store parameters?" | 작은 플래시 섹터 두 개로 A/B + CRC + 시퀀스 번호, 또는 외부 EEPROM/플래시. erase 시간 동안 비행 루프에 영향이 없어야 한다 |
| "A board revision swapped the IMU" | HAL을 기능 단위로, 보드 설정을 데이터로, 런타임 감지(WHO_AM_I), 두 보드를 같은 HIL에서 회귀 |

## 6. 온사이트에서 물어볼 것

- "Which STM32 families are you standardizing on across the FC, radio, and handset, and how do you handle second-sourcing?"
- "Do you use HAL, LL, or your own drivers — and is that shared across products through the platform team?"
- "How is firmware protected in the field — RDP level, signed updates — and where in the factory flow does that happen?"

## 체크

- [ ] "자체 실리콘이 없다 → 내 실리콘 경험이 어떻게 번역되나"를 영어 브리지 문장으로 말할 수 있다
- [ ] "STM32 써 봤나?"에 정직하고 짧게 답하고 가까운 경험으로 넘어갈 수 있다
- [ ] UART DMA + IDLE line 패턴을 그림으로 설명할 수 있다
- [ ] Cortex-M7 캐시와 DMA 일관성 문제를 설명할 수 있다
- [ ] RDP 레벨과 factory test·provisioning 순서의 관계를 설명할 수 있다
- [ ] 리셋 원인 레지스터, 워치독, backup SRAM을 크래시 디버깅과 연결해 말할 수 있다
