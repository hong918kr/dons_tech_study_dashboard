# C00. 여기서 시작 — JD ↔ 노트 지도와 2주 학습 계획

> **이 노트를 다 읽으면**: JD 문장마다 어떤 노트를 읽어야 하는지 안다 · 내 강점과 갭이 어디인지 한눈에 본다 · 2주 동안 무엇을 어떤 순서로 공부할지 정한다
> **JD 연결**: Hark Embedded Software Engineer — Responsibilities 7개 + Requirements 7개 전체
> **Don 기준 난이도**: 지도 역할. 읽는 데 10분

---

## 0. 큰 그림 — 이 역할이 만지는 곳

Hark 1세대 기기를 예로 들어 펌웨어 엔지니어가 만지는 부분을 그리면 이렇다. 아래 구조는 채용 공고를 보고 **추정**한 것이다(context 파일 2.2절).

```
                 ┌──────────────── 배터리 기기 (always-on) ────────────────┐
                 │                                                          │
   마이크 ──I2S/PDM──┐                                                       │
   IMU/센서 ──I2C/SPI─┤   ┌─────────────────────┐    IPC     ┌─────────────┐ │
   햅틱 ─────PWM/I2C──┼──▶│  Always-on MCU       │◀─(SPI/UART─▶│ App SoC      │ │
   버튼/LED ──GPIO────┘   │  Cortex-M + RTOS     │   /GPIO)   │ Cortex-A     │ │
                          │  wake word, 센서 허브│            │ Android      │ │
                          │  전력 상태 관리       │            │ 큰 모델/앱    │ │
                          └────────┬─────────────┘            └──────┬──────┘ │
                                   │ HCI(UART/SDIO/PCIe)             │        │
                          ┌────────▼─────────┐     PMIC · Fuel gauge · 충전   │
                          │ Wi-Fi/BT 콤보칩   │                                │
                          └──────────────────┘                                │
                 └──────────────────────────────────────────────────────────┘
   부트로더 · secure boot · OTA ──── 모든 칩의 이미지를 안전하게 교체
   Factory test · calibration ────── 양산 라인에서 모든 부품을 검사하고 보정값 저장
   JTAG · LA · 스코프 ────────────── 위 전부를 디버깅
```

이 그림의 각 상자와 화살표가 노트 하나씩에 대응한다.

## 1. JD 문장 ↔ 노트 지도

### 1.1 Responsibilities
| JD 문장 | 개념 노트 | 스터디 노트 | Don 현재 수준 |
|---|---|---|---|
| Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers | C01, C02 | S01, S02 | ✅ 강함 (Cortex-R/M bare-metal) |
| Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling | C03, C04 | S01, S02 | 🟡 드라이버 강함 · I2S와 RTOS는 갭 |
| Optimize power consumption and thermal performance for always-on, battery-powered operation | C05 | S03 | 🟡 측정 경험은 있음 · 저전력 설계는 갭 |
| Build and maintain OTA update infrastructure for reliable field updates | C07 | S04 | 🟡 개념 갭 |
| Collaborate with the on-device AI team to support model inference within memory and latency budgets | C08 | S05 | ❌ 갭 |
| Develop factory test and calibration firmware for manufacturing | C09 | S04 | ✅ 강함 (Apple factory test-node) |
| Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG | C10 | S06 | ✅ 강함 |

### 1.2 Requirements
| JD 문장 | 개념 노트 | 스터디 노트 | Don 현재 수준 |
|---|---|---|---|
| 3+ years of professional firmware or embedded systems development | — | S06 (스토리) | ✅ |
| Strong proficiency in C and/or C++ in resource-constrained environments | C02 | S01 | ✅ C 강함 · 임베디드 C++ 관용구 복습 |
| Experience with ARM Cortex-M or Cortex-A processors and associated toolchains | C01 | S02 | ✅ M/R 강함 · A는 개념 정리 |
| Hands-on experience with RTOS (FreeRTOS, Zephyr, or similar) | C04 | S02 | 🟡 **최대 갭 1순위** |
| Familiarity with wireless protocols (BLE, Wi-Fi, or Thread) | C06 | S03 | 🟡 **갭 2순위** (HW 통합은 강함) |
| Comfort reading schematics and working alongside hardware engineers during board bring-up | C10 | S06 | ✅ |
| Experience with embedded debugging tools and workflows | C10 | S06 | ✅ |

## 2. 두 종류의 노트

### 2.1 개념 노트 (C01~C10)
원리를 처음부터 쌓는 글이다. 하드웨어가 어떻게 동작하는지 → 소프트웨어가 왜 그렇게 짜이는지 → 코드 → Don이 이미 아는 것과의 연결 순서로 쓰여 있다. 천천히 읽고, 각 노트 끝의 체크리스트를 "화이트보드에 그릴 수 있다" 수준으로 채우는 게 목표다.

### 2.2 스터디 노트 (S01~S06)
면접 훈련용이다. 질문을 먼저 **소리 내어 영어로** 답해 보고, 그다음 모범 답안을 읽는다. S01은 코딩 문제이므로 손으로(또는 빈 에디터에) 먼저 짜 본다.

| 노트 | 내용 | 언제 |
|---|---|---|
| S01 | 임베디드 C 코딩 문제와 풀이 | 매일 1~2문제 |
| S02 | 드라이버·RTOS·Cortex-M Q&A | C01~C04 읽은 뒤 |
| S03 | 저전력·무선 Q&A + 계산 연습 | C05~C06 읽은 뒤 |
| S04 | OTA·secure boot·factory Q&A + 설계 연습 | C07, C09 읽은 뒤 |
| S05 | 시스템 설계: always-on 음성 AI 기기 | C08 읽은 뒤, 마지막 주 |
| S06 | 디버깅 시나리오 + STAR 스토리 + 영어 표현 | 마지막 주, 면접 전날 다시 |

## 3. 읽는 순서

갭이 큰 것부터가 아니라 **의존 관계 순서**로 읽는다. RTOS를 이해하려면 Cortex-M 예외 모델이 먼저 필요하고, 저전력을 이해하려면 RTOS의 tickless idle이 먼저 필요하기 때문이다.

```
C01 Cortex-M/부트/툴체인 ──▶ C02 제약 환경 C/C++ ──▶ C03 BSP·드라이버(I2S 포함)
                                                          │
                                                          ▼
                     C05 저전력·발열 ◀── C04 RTOS (FreeRTOS · Zephyr)  ★ 1순위 갭
                          │
                          ▼
                     C06 무선 BLE/Wi-Fi/Thread  ★ 2순위 갭
                          │
          ┌───────────────┼────────────────┐
          ▼               ▼                ▼
   C07 OTA·secure boot  C08 on-device ML  C09 factory test
          └───────────────┴────────────────┘
                          ▼
              C10 디버깅·bring-up·회로도 (강점 — 스토리로 정리)
```

C01, C02, C10은 Don이 이미 잘 아는 영역이라 빠르게 읽어도 된다. 대신 **말로 설명하는 연습**(영어 답변)에 시간을 쓴다.

## 4. 2주 학습 계획

하루 2~3시간 기준이다. 주말에 실습을 몰아서 한다.

| 날 | 읽기 | 연습 | 실습 |
|---|---|---|---|
| 1일 | C00, C01 | S01 문제 1~2 | arm-none-eabi-gcc로 map 파일 읽기 |
| 2일 | C02 | S01 문제 3~4 | — |
| 3일 | C03 (UART/SPI/I2C) | S01 문제 5~6 | — |
| 4일 | C03 (I2S/DMA/드라이버 계층) | S02 전반부 | — |
| 5일 | C04 (FreeRTOS) | S02 RTOS 문항 | FreeRTOS를 QEMU에서 실행 |
| 6일 (주말) | C04 (Zephyr) | S01 문제 7~8 | nRF52840 DK + Zephyr blinky → 센서 읽기 |
| 7일 (주말) | C05 | S03 계산 연습 | PPK2로 sleep 전류 측정 |
| 8일 | C06 (BLE) | S03 BLE 문항 | Zephyr BLE peripheral 샘플 |
| 9일 | C06 (Wi-Fi/Thread/공존) | S03 나머지 | — |
| 10일 | C07 | S04 OTA 문항 | MCUboot + imgtool 서명 |
| 11일 | C08 | S05 AI 문항 | TFLite Micro 예제 빌드 |
| 12일 | C09 | S04 factory 문항 + 설계 연습 | — |
| 13일 (주말) | C10 | S06 시나리오 | 미니 프로젝트 마무리 (BLE + sleep + OTA) |
| 14일 (주말) | 전체 체크리스트 복습 | S05 설계 워크스루 45분 모의 · S06 스토리 | 모의 면접 녹음 |

## 5. 미니 프로젝트 — 갭 세 개를 한 번에

레쥬메에 RTOS와 BLE를 쓰려면 실제로 해 본 것이 있어야 한다. 아래 프로젝트 하나로 RTOS, BLE, 저전력, OTA를 모두 경험할 수 있다.

```
nRF52840 DK (약 $40) + Nordic PPK2 (약 $100, 선택)
 ├─ Zephyr 기반 앱
 │   ├─ I2C로 온도/IMU 센서 읽기 (thread + k_timer)
 │   ├─ BLE peripheral: 센서 값을 GATT notify로 전송
 │   ├─ 연결 안 됐을 때 deep sleep, 버튼(GPIO)으로 wake
 │   └─ PPK2로 평균 전류 측정 → 연결 interval 바꿔 가며 비교
 └─ MCUboot + imgtool 서명 → BLE(SMP)로 OTA 업데이트 → confirm/revert 확인
```

이 프로젝트를 끝내면 면접에서 "RTOS 실무 경험이 있나?"라는 질문에 **구체적인 숫자**(평균 전류 몇 µA, 이미지 크기, 부팅 시간)로 답할 수 있다.

## 6. 공부할 때 원칙

1. **Don이 아는 것에 붙여라.** 새 개념이 나오면 "SSD FW에서는 이게 뭐였지?"를 먼저 떠올린다. 예: RTOS queue ↔ NVMe submission queue, OTA A/B ↔ SSD FW slot, calibration 저장 ↔ SSD의 factory 영역.
2. **그림으로 설명할 수 있어야 안 것이다.** 각 노트의 ASCII 그림을 빈 종이에 다시 그려 본다.
3. **영어로 말해 본다.** 개념 노트의 "면접에서 이렇게 말한다" 절을 소리 내어 읽고, 다음 날 안 보고 말해 본다.
4. **숫자를 외운다.** I2C 속도(100k/400k/1M), BLE connection interval 범위, Cortex-M exception stack frame 크기 같은 숫자는 면접에서 신뢰를 준다.
5. **모르는 건 모른다고 말하고, 어떻게 알아낼지 말한다.** "Zephyr의 그 API는 정확히 기억 안 나지만, 이런 구조라서 이렇게 찾아본다"는 좋은 답이다.

## 7. 요약 & 체크리스트

JD 14개 문장 중 Don은 절반 이상이 이미 강하다. 공부 시간은 **RTOS(C04) → 무선(C06) → 저전력(C05) → OTA(C07) → ML(C08)** 순서로 갭에 집중하고, 강점(C01, C09, C10)은 영어로 말하는 연습에 쓴다.

- [ ] JD 문장마다 대응하는 노트를 말할 수 있다
- [ ] 읽는 순서(의존 관계)를 이해했다
- [ ] 2주 계획을 캘린더에 옮겼다
- [ ] nRF52840 DK(+ PPK2)를 주문했다
- [ ] 미니 프로젝트 목표를 정했다
- [ ] 매일 S01 코딩 1~2문제를 푼다

## 참고 자료
- Hark JD 원문: https://job-boards.greenhouse.io/hark/jobs/4186968009
- 회사·적합도 분석: `../hark_ai_embedded_swe_context.md`
- Zephyr 시작하기: https://docs.zephyrproject.org/latest/develop/getting_started/index.html
- FreeRTOS 문서: https://www.freertos.org/Documentation/00-Overview
- nRF52840 DK: https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK
