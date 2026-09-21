# Hark 스터디 노트 — 작성 스펙

## 목적
Hark **Embedded Software Engineer** JD의 Responsibilities·Requirements 전체를 공부하는 노트 세트다.
Don은 이 노트를 **처음부터 하나씩 읽으면서 공부**한다. 그래서 요약이 아니라 **교과서처럼 자세한 설명**이 필요하다.

- 회사·역할 맥락: `../hark_ai_embedded_swe_context.md` (먼저 읽을 것 — 특히 0, 1, 2.2, 2.7, 3.1, 3.3, 4.3절)
- 출력: `concepts/*.md` (개념 노트), `study/*.md` (스터디 노트 = 면접 드릴)
- HTML 생성: `python3 build_notes_site.py` → `site/index.html` (노트 작성자는 HTML을 만들 필요 없음)

## 독자 — Don
- 임베디드 펌웨어 약 7~8년. SK hynix/Solidigm에서 **엔터프라이즈 SSD 양산 펌웨어**(bare-metal C/C++, ARM Cortex-R8/R82/M0+, Xtensa, FPGA pre-silicon bring-up, I2C/SPI/DMA/PCIe, NVMe, telemetry, 에러 처리), 지금은 Apple **RF(Wireless) 칩셋 통합 그룹**(새 무선 칩을 제품에 통합, PCIe/I2C/SPMI/RFFE 장애 root cause, bring-up → NPI → MP, factory test-node 아키텍처, DSO/프로토콜 분석기).
- 잘 아는 것: bare-metal, ISR, DMA, 레지스터, 링버퍼, JTAG/Trace32, 스코프/LA, 고속 인터페이스, 양산 흐름.
- **약한 것(이 노트가 메워야 할 것)**: 상용 RTOS(FreeRTOS/Zephyr) 실무, BLE/Wi-Fi/Thread 프로토콜 스택, I2S/오디오, 배터리 기기 저전력 설계, OTA/secure boot(MCUboot 등), 임베디드 ML 추론(TFLite Micro, CMSIS-NN, NPU), Cortex-A/Linux 쪽 BSP.
- 그래서 **Don이 아는 것과 연결해서** 설명한다. 예: "SSD FW의 command queue ↔ RTOS queue", "Apple에서 본 RFFE/SPMI ↔ I2C와 비교", "Trace32 ↔ J-Link+GDB".

## Hark 제품 맥락 (노트에 예시로 쓸 것)
- 1세대 컨슈머 기기: Cellular·Wi-Fi·BT·GNSS·NFC·UWB가 들어가는 웨어러블급 기기. 배터리, always-on, 마이크·센서·햅틱.
- 추정 구조: Qualcomm SoC(Cortex-A, Android) + 저전력 always-on MCU(Ambiq Apollo 계열 Cortex-M 등, RTOS). on-device AI는 Hexagon DSP / MCU에서 wake word·speech 추론.
- 이 구조는 **추정**이다. 노트에서 쓸 때 "예를 들어 Hark 같은 기기라면" 식으로 쓰고 사실처럼 단정하지 않는다.

## 공통 규칙
1. **한국어 기본**, 기술 용어는 영어 그대로(NVIC, priority inversion, GATT…). 설명체(~다). 존댓말 금지.
2. **마크다운 문법은 다음만 사용**: `#`~`###` 제목, 문단, `-`/`1.` 목록(**중첩 금지** — 들여쓴 하위 목록 쓰지 말 것), 표(한 행 한 줄), `> ` 인용, 코드펜스, `---` 구분선, `- [ ]` 체크박스, 인라인 `` `코드` `` `**굵게**` `[링크](url)`. **raw HTML 금지.**
3. 코드 블록 언어: ```` ```c ````, ```` ```cpp ````, ```` ```sh ````, ```` ```ld ````(링커 스크립트), 다이어그램/타임라인은 언어 없는 ```` ``` ````. ASCII 다이어그램을 적극적으로 쓴다(버스 파형, 메모리 맵, 상태도, 타임라인).
4. 본문에 별표(`*`)를 그대로 쓰지 말 것(이탤릭으로 해석됨). 포인터 표기 `*p`, `uint8_t *buf`는 반드시 인라인 코드나 코드 블록 안에 쓴다.
5. **정확성 최우선**. 레지스터 이름·API 이름·프로토콜 수치(예: BLE advertising interval 범위, I2C 속도 모드)는 표준/공식 문서 기준으로. 확실하지 않으면 쓰지 말거나 "(벤더마다 다름)"이라고 쓴다. 지어낸 API 금지. FreeRTOS/Zephyr/MCUboot/TFLM API는 실제 이름만.
6. 코드는 **컴파일 가능한 수준**으로 쓴다(필요한 헤더, 타입). 레지스터 주소가 필요하면 가상의 `#define`임을 주석으로 밝힌다.
7. 한 노트 = 한 파일. 지정된 파일만 만든다. 다른 파일(특히 `../*_context.md`, `build_notes_site.py`)은 **수정 금지**.

## 개념 노트 구조 (`concepts/CNN_topic.md`, 목표 500~800줄)
```markdown
# CNN. <제목> — <한 줄 요약>

> **이 노트를 다 읽으면**: <할 수 있게 되는 것 3~4개를 · 로 이어서 한 줄>
> **JD 연결**: <이 노트가 커버하는 JD 문장을 그대로 인용>
> **Don 기준 난이도**: <이미 아는 부분 / 새로 배울 부분 한 줄>

---

## 0. 큰 그림
<이 주제가 전체 기기에서 어디에 위치하는지. ASCII 블록도 1개 이상.>

## 1. ~ N. <주제별 절>
<각 절: 개념 정의 → 왜 존재하나(하드웨어/물리 이유) → 동작 원리(다이어그램/파형/타임라인)
 → 코드 예시(한 줄씩 설명) → 흔한 함정 → "Don 경험과 연결" 한두 문장.
 ### 소제목을 적극 사용. 표로 비교(예: FreeRTOS vs Zephyr, SPI vs I2C vs UART).>

## N+1. 흔한 실수와 증상
| 실수 | 증상 | 원인 | 고치는 법 |   (6~10행)

## N+2. 면접에서 이렇게 말한다
<핵심 질문 5~8개: "**Q.** 질문" 문단 다음에 "**A.** 30초 답변(한국어)" 문단, 그리고 그대로 쓸 수 있는 영어 문장.>

## N+3. 직접 해보기
<보드/툴로 해볼 실습 2~4개. nRF52840 DK, Zephyr, FreeRTOS, QEMU, Renode 등 실제로 가능한 것만. 명령어 포함.>

## N+4. 용어 사전
| 용어 | 뜻 | 한 줄 설명 |

## N+5. 요약 & 체크리스트
<한 문단 요약 + `- [ ]` 6~10개 ("~를 화이트보드에 그릴 수 있다" 식)>

## 참고 자료
<공식 문서·스펙 링크 (ARM 아키텍처 레퍼런스, FreeRTOS.org, docs.zephyrproject.org, Bluetooth Core Spec, MCUboot docs, TFLM repo 등). 실제로 존재하는 URL만.>
```

## 스터디 노트 구조 (`study/SNN_topic.md`, 목표 400~700줄)
```markdown
# SNN. <제목> — <한 줄 요약>

> **목표**: <이 노트로 연습할 것>
> **선행**: <먼저 읽을 개념 노트, 예: C03, C04>
> **사용법**: 질문을 먼저 소리 내어 답해 보고, 그다음 모범 답안을 읽는다.

---

## 1. ~ N. <카테고리별 절>
<각 문항:
 ### QNN. <질문 원문 — 면접관이 말하는 영어 문장> 
 **왜 묻나**: JD 근거 / 면접관이 보려는 것
 **30초 답변**: 한국어 핵심
 **English answer**: 그대로 말할 수 있는 영어 3~6문장
 **꼬리질문**: 다음에 올 질문 2~3개와 짧은 답
 **Don 스토리 연결**: 어떤 경험으로 답할지 (없으면 생략)
 코딩 문항은: 문제 → 접근 → 전체 C 코드 → 복잡도/메모리 → 엣지 케이스 → follow-up>

## N+1. 최종 점검
<`- [ ]` 체크리스트 + 자주 틀리는 포인트 표>
```

## 파일 목록 (담당 배정)
| 파일 | 내용 |
|---|---|
| `concepts/C00_roadmap.md` | JD ↔ 노트 지도, 읽는 순서, 2주 학습 계획 (메인 작성) |
| `concepts/C01_arm_cortex_boot_toolchain.md` | Cortex-M 프로그래머 모델, 예외/NVIC, 부트(reset→startup→main), 링커 스크립트, 메모리 맵, MPU/캐시, Cortex-A 차이(MMU, EL, GIC, TF-A/U-Boot), 툴체인(arm-none-eabi-gcc 옵션, map, objdump, size), CMSIS |
| `concepts/C02_c_cpp_constrained.md` | volatile/const/섹션 배치, 스택·힙·정적 할당·memory pool, 정렬/패킹/엔디언, 정수 승격 함정, fixed-point, Cortex-M 원자성(LDREX/STREX, critical section), 임베디드 C++(예외/RTTI 끄기, RAII, constexpr, 템플릿 비용, placement new, 정적 초기화 순서), MISRA 개요 |
| `concepts/C03_bsp_peripheral_drivers.md` | BSP 구성, 클럭 트리, GPIO, polling/인터럽트/DMA, UART, SPI, I2C(버스 복구 포함), **I2S/PDM/TDM(오디오)**, DMA ping-pong, 드라이버 계층(HAL, Zephyr device model·devicetree), 드라이버 작성 예 |
| `concepts/C04_rtos_freertos_zephyr.md` | 스케줄러, task 상태, Cortex-M 컨텍스트 스위치(PendSV/SysTick), queue/semaphore/mutex/event group/task notification, priority inversion·inheritance, ISR→task(FromISR), software timer, 메모리(heap_1~5, static), stack overflow 감지, watchdog 전략, tickless idle, Zephyr(커널 객체, devicetree, Kconfig, west, workqueue), FreeRTOS vs Zephyr 비교, RMS 기초 |
| `concepts/C05_low_power_thermal.md` | 전력 물리(CV²f, leakage), Cortex-M sleep(WFI/WFE, SLEEPDEEP, SLEEPONEXIT), wake source, clock/power gating, DVFS, 주변장치·라디오 duty cycling, always-on sensor hub 구조, 배터리 수명 계산, 측정(PPK2, Joulescope, power analyzer), 흔한 누설, 발열(throttling, skin temp), PMIC·fuel gauge |
| `concepts/C06_wireless_ble_wifi_thread.md` | BLE 스택 계층, PHY, advertising/scan/connection, GAP/GATT, MTU/DLE, 보안(pairing/bonding), 연결 파라미터와 전력, Wi-Fi(STA, association, WPA2/3, PS/DTIM, TWT, BLE provisioning), Thread(802.15.4, 6LoWPAN, mesh 역할, Matter), host–controller 분리(HCI over UART/SDIO/PCIe, 콤보칩 FW 다운로드), 2.4GHz 공존(PTA), Don의 Apple 칩셋 통합 경험과 연결 |
| `concepts/C07_ota_secure_boot.md` | OTA 구조(A/B, single-slot+recovery, MCUboot swap 방식), 이미지 헤더/버전, 서명(ECDSA P-256/Ed25519)·해시, anti-rollback, root of trust(ROM, OTP 키 해시, secure element), chain of trust, 이미지 암호화, delta update, 전원 차단 안전성, confirm/revert, 다중 프로세서(SoC+MCU+라디오 FW) 업데이트, 단계적 롤아웃, 디버그 포트 잠금, TrustZone-M 기초 |
| `concepts/C08_on_device_ml_inference.md` | wake word/speech 파이프라인, INT8 양자화, TFLite Micro(tensor arena, op resolver, interpreter), CMSIS-NN, NPU(Ethos-U), DSP, 메모리 예산(가중치 flash/XIP vs SRAM, arena), 레이턴시 예산, 오디오 DMA double buffering, 캐시, 모델 residency, 프로파일링(DWT CYCCNT), SoC+MCU 분담(MCU가 SoC 깨우기), 펌웨어 엔지니어 vs ML 엔지니어 역할 경계 |
| `concepts/C09_factory_test_calibration.md` | 제조 흐름(SMT → ICT/FCT → FATP), EVT/DVT/PVT/MP 정의, DUT 테스트 모드, 테스트 명령 프로토콜(UART/USB), fixture, 캘리브레이션 종류(센서 offset/gain, IMU, 마이크 감도, 배터리 게이지, RF), cal 데이터 저장(OTP, 보호 파티션, CRC, 버전), provisioning(시리얼, 키, 기기 인증서), 테스트 시간·yield, 추적성(MES), 출하 전 잠금 — Don의 Apple factory test-node 경험과 연결 |
| `concepts/C10_debugging_bringup_schematics.md` | JTAG vs SWD, 디버그 프로브(J-Link, ST-Link, Trace32), GDB/OpenOCD 흐름, breakpoint/watchpoint(FPB/DWT), ITM/SWO, SEGGER RTT, HardFault 해석(CFSR/HFSR/MMFAR/BFAR, stacked PC), 로직 분석기(프로토콜 디코드, 트리거), 오실로스코프(프로빙, 그라운드, 전원 램프), **회로도 읽기**(기호, net 이름, pull-up, level shifter, power tree, test point), 보드 bring-up 체크리스트, 흔한 고장 패턴 |
| `study/S01_c_coding_drills.md` | 임베디드 C 코딩 문제 12~15개 + 전체 풀이(ISR-safe SPSC ring buffer, 레지스터 비트 매크로, bit count/reverse, endian swap, memory pool, debounce, fixed-point moving average, CRC-16, UART 패킷 파서 FSM, 명령 디스패처, aligned alloc, 이벤트 큐, 타이머 휠 등) |
| `study/S02_drivers_rtos_qa.md` | 드라이버·BSP·RTOS·Cortex-M 면접 문항 25개 이상 |
| `study/S03_power_wireless_qa.md` | 저전력·발열·BLE/Wi-Fi/Thread 문항 20개 이상 + 계산 연습(배터리 수명, BLE 평균 전류) |
| `study/S04_ota_secure_factory_qa.md` | OTA·secure boot·factory test/calibration 문항 20개 이상 + 설계 연습 "SoC+MCU 기기 OTA 설계" |
| `study/S05_system_design_ai.md` | 시스템 설계 연습: always-on 음성 AI 기기(MCU wake word → SoC 깨우기 → 클라우드/온디바이스 모델) 전체 설계 워크스루 + on-device AI 협업 문항 |
| `study/S06_debug_scenarios_stories.md` | 디버깅 시나리오 10개 이상(구조화된 답) + Don STAR 스토리 매핑 + 행동 질문 + 영어 표현 모음 |
