# P05. 노트 감사 (2026-09-29) — 현행 JD 기준으로 53개 노트를 다시 채점한다

> **기준 원문**: [JD 스냅샷 2026-09-29](../../JD_SNAPSHOT_2026-09-29.md) — Embedded Software 부서 7개 공고 전문. 이 문서의 모든 판정은 그 파일의 문장만 근거로 한다
> **감사 대상**: `bsp/` 5 · `jd/` 15 · `concepts/` 11 · `study/` 6 · `coding/` 11 · `plan/` 5 = 53개 노트
> **결론 한 줄**: **노트의 지식은 거의 다 살아 있고, 문제는 인용과 우선순위다.** 22개 JD 줄 중 13개가 강함, 5개가 부분, **4개가 약함·없음**이고, 낡은 JD 문장을 근거로 단 노트가 **20개 파일 34곳**이다
> **가장 급한 것 한 개**: [S06 §5.1](../study/S06_debug_scenarios_stories.html)의 Why Hark 답변이 **현재 JD에 존재하지 않는 문장**("work with the agent team on model execution and memory constraints")을 인용한다. 면접에서 소리 내어 말할 문장이라 여기부터 고친다
> **이 노트를 다 읽으면**: 현행 JD 한 줄마다 어느 노트를 열어야 하는지 안다 · 어느 노트가 낡은 JD를 인용하고 있어 그 말을 하면 안 되는지 안다 · 남은 며칠을 어디에 쓸지 순서대로 안다

---

## 0. 감사 결과 한눈에

```
[커버리지 — 현행 BSP JD 22줄]
  강함 13 ████████████████████████░░░░░░░░░░░░░░░░  59%
  부분  5 █████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  23%
  약함  4 ███████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  18%   ← 전부 9/23에 새로 들어온 줄

[약함 4줄 = 공부 우선순위 그대로]
  ① Working closely with vendors on system integration and validation      전용 노트 0개
  ② Working with HW and SW team to ensure our hardware are built ...       전용 노트 0개
  ③ FPGA-based system integrations                                         스토리만, 기술 노트 0개
  ④ ... and system integration (R2 꼬리)                                   본문 검색 결과 0건

[낡은 인용]
  삭제된 JD 문장을 "JD 근거"로 단 노트         13개 파일
  문구가 바뀐 JD를 그대로 인용한 노트           9개 파일
  구조가 낡은 것(n/7 라벨, 매트릭스, 스펙 파일)  5개 파일
  결론 자체가 낡아 잘못 안내하는 절              1개 (B03 §8.1) ← 위험도 높음

[내 경험 과잉 주장]
  6건. 그중 2건은 J00에서 "미확인"으로 남겨 둔 것을 노트가 "✅ 강함"으로 확정했다
```

현행 JD에서 **없어진 항목을 다루는 노트가 6개**(J04·J05·J06·C07·C08·C09, 합계 약 4,900줄)다. 이 감사와 함께 그 여섯 개에 우선순위 하향 배너를 달았다. 내용은 지우지 않았다. Bonus에 secure boot·ML 런타임·EVT/DVT/PVT가 남아 있어 개념 수준으로는 여전히 쓸모가 있다.

---

## 1. 현행 BSP JD ↔ 노트 커버리지 매트릭스

판정 기준은 셋이다.

- **강함** = 그 줄만 다루는 절이 있고, 면접 문답과 Don 경험 매핑까지 있다
- **부분** = 지식은 있으나 그 줄의 무게중심이 빠졌거나 흩어져 있다
- **약함** = 언급만 있거나 전용 절이 없다. 또는 본문 검색 결과가 0건이다

### 1.1 인트로 문장 (About the Role)

원문: "from board bring-up, vendor code integrations, to custom peripheral drivers and integrations, FPGA-based system integrations, and system power and performance improvements."

| JD 요소 | 커버하는 노트 | 판정 | 비고 |
|---|---|---|---|
| board bring-up | [J13](../jd/J13_schematics_bringup_collab.html) 전체 · [C10](../concepts/C10_debugging_bringup_schematics.html) §9 · [B01](../bsp/B01_bsp_anatomy.html) §2~§3 · [B04](../bsp/B04_bsp_interview.html) Q10 | 강함 | 노트 세트에서 가장 두꺼운 축이다 |
| vendor code integrations | [J01](../jd/J01_firmware_c_cpp_arm.html) §3.4 · [J02](../jd/J02_bsp_drivers_rtos_scheduling.html) §2.10 · [B03](../bsp/B03_linux_android_bsp.html) §7 · [B04](../bsp/B04_bsp_interview.html) Q17·Q40 · [C03](../concepts/C03_bsp_peripheral_drivers.html) §10 | 부분 | 기술 경로(SDK를 리포에 넣기)는 있다. 벤더와 **일하는 방식**이 없다 |
| custom peripheral drivers and integrations | [C03](../concepts/C03_bsp_peripheral_drivers.html) 전체 · [J02](../jd/J02_bsp_drivers_rtos_scheduling.html) · [B01](../bsp/B01_bsp_anatomy.html) §5 | 강함 | I2S만 학습 항목으로 남아 있다 |
| FPGA-based system integrations | [S06](../study/S06_debug_scenarios_stories.html) ST4 · [J13](../jd/J13_schematics_bringup_collab.html) §6 · [B04](../bsp/B04_bsp_interview.html) Q10 | 약함 | 전부 "내 경험 스토리"다. FPGA 자체를 설명하는 절이 한 곳도 없다 |
| system power and performance improvements | [C05](../concepts/C05_low_power_thermal.html) · [J03](../jd/J03_power_thermal_always_on.html) · [S03](../study/S03_power_wireless_qa.html) | 부분 | power는 강하고 performance(프로파일링·회귀)는 [J14](../jd/J14_debug_tools_workflows.html) §2.6뿐이다 |

### 1.2 Responsibilities (6줄)

| # | JD 원문 | 커버하는 노트 | 판정 |
|---|---|---|---|
| R1 | Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers | [J01](../jd/J01_firmware_c_cpp_arm.html) · [C01](../concepts/C01_arm_cortex_boot_toolchain.html) · [C02](../concepts/C02_c_cpp_constrained.html) · [S01](../study/S01_c_coding_drills.html) · [S02](../study/S02_drivers_rtos_qa.html) | 강함 |
| R2a | Own BSP development | [B00](../bsp/B00_bsp_overview.html)~[B04](../bsp/B04_bsp_interview.html) 시리즈 · [J02](../jd/J02_bsp_drivers_rtos_scheduling.html) | 강함 |
| R2b | board bring up | [J13](../jd/J13_schematics_bringup_collab.html) · [C10](../concepts/C10_debugging_bringup_schematics.html) §9 · [B01](../bsp/B01_bsp_anatomy.html) | 강함 |
| R2c | peripheral drivers (SPI, I2C, UART, I2S etc.) | [C03](../concepts/C03_bsp_peripheral_drivers.html) · [S02](../study/S02_drivers_rtos_qa.html) · 코딩 02·03 | 강함 |
| R2d | and system integration | 없음 — 본문에서 이 표현을 쓰는 노트가 0개 | **약함** |
| R3 | Working closely with vendors on system integration and validation | 전용 노트 없음. [B04](../bsp/B04_bsp_interview.html) Q17·Q40이 가장 가깝다 | **약함** |
| R4 | Working with Hardware and Software team to ensure our hardware are built and functioning to the specifications | [J13](../jd/J13_schematics_bringup_collab.html) §3.4(HW 버그 리포트) · [J03](../jd/J03_power_thermal_always_on.html) §6.2(margin sign-off) | **약함** |
| R5 | Optimize power consumption and thermal performance for always-on, battery-powered operation | [C05](../concepts/C05_low_power_thermal.html) · [J03](../jd/J03_power_thermal_always_on.html) · [S03](../study/S03_power_wireless_qa.html) | 강함 |
| R6 | Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG | [C10](../concepts/C10_debugging_bringup_schematics.html) · [J07](../jd/J07_debug_hw_sw_tools.html) · [J14](../jd/J14_debug_tools_workflows.html) · [S06](../study/S06_debug_scenarios_stories.html) | 강함 (최강) |

R2d와 R3의 "system integration"은 같은 단어가 두 줄에 걸쳐 나온다. Hark가 이 단어에 무게를 두고 있다는 신호다. 그런데 노트 53개 본문에서 이 표현을 정의하거나 설명하는 곳이 없다. 대신 [P02](P02_jd_change_2026-09-23.html)·[P03](P03_embedded_role_map.html)·[P04](P04_4day_sprint.html) 같은 **플랜 노트에만** 나온다. 즉 "중요하다"는 것은 알고 있고 "무엇인지"는 아직 쓰지 않았다.

### 1.3 Requirements (7줄)

| # | JD 원문 | 커버하는 노트 | 판정 |
|---|---|---|---|
| Q1 | 5+ years of professional firmware or embedded systems development | [J08](../jd/J08_three_years_experience.html) · [S06](../study/S06_debug_scenarios_stories.html) §3 | 강함 (단 노트가 3+ 기준으로 쓰여 있음 → §2.2) |
| Q2 | Strong proficiency in C and/or C++ in resource-constrained environments | [C02](../concepts/C02_c_cpp_constrained.html) · [J09](../jd/J09_c_cpp_resource_constrained.html) · [S01](../study/S01_c_coding_drills.html) 14문제 | 강함 |
| Q3 | Experience with ARM Cortex-M or Cortex-A processors | [C01](../concepts/C01_arm_cortex_boot_toolchain.html) · [J10](../jd/J10_arm_cortex_toolchains.html) · [B03](../bsp/B03_linux_android_bsp.html) §1 | 강함 (Cortex-A는 개념) |
| Q4 | Hands-on experience with embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes | [C04](../concepts/C04_rtos_freertos_zephyr.html) · [J11](../jd/J11_rtos_handson.html) · [B02](../bsp/B02_zephyr_board_port.html) · [B03](../bsp/B03_linux_android_bsp.html) | **부분** |
| Q5 | Familiarity with wireless protocols (BLE, Wi-Fi, or Thread) | [C06](../concepts/C06_wireless_ble_wifi_thread.html) · [J12](../jd/J12_wireless_ble_wifi_thread.html) · [S03](../study/S03_power_wireless_qa.html) §3~§4 | 부분 (JD가 familiarity만 요구하므로 충분) |
| Q6 | Comfort reading schematics and working alongside hardware engineers during board bring-up | [J13](../jd/J13_schematics_bringup_collab.html) · [C10](../concepts/C10_debugging_bringup_schematics.html) §8 | 강함 |
| Q7 | Experience with embedded debugging tools and workflows | [J14](../jd/J14_debug_tools_workflows.html) · [C10](../concepts/C10_debugging_bringup_schematics.html) | 강함 |

Q4를 **부분**으로 내린 근거는 검색 결과다.

| 검색어 | 내용 노트(bsp·jd·concepts·study) 등장 횟수 |
|---|---|
| eLinux | **0회** (플랜 노트에만 12회) |
| AOSP | 14회 — 그중 9회가 [B03](../bsp/B03_linux_android_bsp.html) 한 곳 |
| VxWorks | **2회** — 둘 다 Mars Pathfinder priority inversion 일화로 우연히 나온 것 |
| FreeRTOS / Zephyr | 수백 회 |

즉 노트는 구 JD가 지목한 **RTOS 두 개에 최적화**되어 있고, 신 JD가 새로 지목한 세 개(eLinux, AOSP, VxWorks)는 [B03](../bsp/B03_linux_android_bsp.html) 하나가 Linux·Android만 받치고 있다. "네 종류 임베디드 OS가 어떻게 다르고 우리 기기에서는 무엇이 어디에 쓰이나"를 30초에 정리한 절이 없다.

### 1.4 Bonus Qualifications (4줄)

| JD 원문 | 커버하는 노트 | 판정 |
|---|---|---|
| Experience with power optimization for battery-powered consumer devices | [C05](../concepts/C05_low_power_thermal.html) · [J03](../jd/J03_power_thermal_always_on.html) | 강함 |
| Familiarity with secure boot, firmware signing, or hardware root of trust | [C07](../concepts/C07_ota_secure_boot.html) §5~§7 · [S04](../study/S04_ota_secure_factory_qa.html) | 강함 — C07이 배너를 달았어도 이 절은 살아 있다 |
| Exposure to ML inference runtimes on embedded platforms | [C08](../concepts/C08_on_device_ml_inference.html) · [S05](../study/S05_system_design_ai.html) | 개념 수준으로 충분 |
| Experience shipping consumer electronics through EVT/DVT/PVT milestones | [C09](../concepts/C09_factory_test_calibration.html) §1 · [J06](../jd/J06_factory_test_calibration.html) · [J08](../jd/J08_three_years_experience.html) | 강함 — 스토리 각도만 바꿔 유지 |

### 1.5 집계

| 구간 | 강함 | 부분 | 약함 |
|---|---|---|---|
| 인트로 5줄 | 2 | 2 | 1 |
| Responsibilities 8줄(R2 분해) | 6 | 0 | 2 |
| Requirements 7줄 | 5 | 2 | 0 |
| Bonus 4줄 | 3 | 1 | 0 |
| **합계 24줄** | **16** | **5** | **3** |

R2d를 별도로 세면 약함 4줄이다. 결론은 단순하다. **9/23에 새로 들어온 줄만 비어 있다.** 노트 세트는 구 JD를 아주 성실하게 커버했고, 개정 이후 추가된 축을 아직 따라가지 못했다.

### 1.6 노트 53개 인벤토리 — 유지·갱신·하향

`유지`는 손대지 않고 그대로 쓴다. `갱신`은 지식은 유효하고 JD 인용·우선순위만 고친다. `하향`은 이번 준비 기간에 열지 않는다.

| 노트 | 줄수 | 현행 JD와의 관계 | 판정 |
|---|---|---|---|
| [B00 BSP 개요](../bsp/B00_bsp_overview.html) | 159 | 좌표표와 금지 표현이 핵심. 헤더 인용만 낡음 | 갱신 |
| [B01 BSP 해부](../bsp/B01_bsp_anatomy.html) | 700 | R2a·R2b의 주력. §2.3 두 프로세서 부트가 R2d에도 쓰인다 | 갱신 |
| [B02 Zephyr 보드 포팅](../bsp/B02_zephyr_board_port.html) | 700 | Q4의 RTOS 절반. 실습 근거를 만드는 노트 | 갱신 |
| [B03 Linux·Android BSP](../bsp/B03_linux_android_bsp.html) | 707 | **Q4의 주력으로 격상.** §8.1 결론이 낡아 위험 | 갱신 (최우선) |
| [B04 BSP 면접 41문항](../bsp/B04_bsp_interview.html) | 590 | Q17·Q40·Q41이 R3·R2d에 직접 쓰인다 | 갱신 |
| [J00 확인 필요 72건](../jd/J00_open_questions.html) | 159 | 과잉 주장 감사의 기준 문서. 그대로 유효 | 유지 |
| [J01 C/C++ 펌웨어](../jd/J01_firmware_c_cpp_arm.html) | 700 | R1. §3.4가 R3에도 쓰인다 | 갱신 |
| [J02 BSP·드라이버](../jd/J02_bsp_drivers_rtos_scheduling.html) | 700 | R2. 제목에 삭제된 표현이 박혀 있어 손볼 곳이 가장 많다 | 갱신 |
| [J03 전력·발열](../jd/J03_power_thermal_always_on.html) | 700 | R5. §6.2가 R4의 재료다 | 유지 |
| [J04 OTA 인프라](../jd/J04_ota_infrastructure.html) | 693 | 해당 Responsibility 삭제 | 하향 (배너) |
| [J05 온디바이스 AI 예산](../jd/J05_on_device_ai_budgets.html) | 615 | 해당 Responsibility 삭제 | 하향 (배너) |
| [J06 공장 테스트](../jd/J06_factory_test_calibration.html) | 689 | 해당 Responsibility 삭제. Bonus EVT/DVT/PVT만 | 하향 (배너) |
| [J07 디버깅 업무](../jd/J07_debug_hw_sw_tools.html) | 697 | R6. 배점 최대 축 | 유지 |
| [J08 연차](../jd/J08_three_years_experience.html) | 540 | Q1. 제목과 본문이 3+ 기준 | 갱신 |
| [J09 제약 환경 C/C++](../jd/J09_c_cpp_resource_constrained.html) | 636 | Q2. 18행 인용 한 줄만 낡음 | 유지 |
| [J10 Cortex·툴체인](../jd/J10_arm_cortex_toolchains.html) | 682 | Q3. 툴체인 구절이 요건에서 빠짐 | 갱신 |
| [J11 RTOS hands-on](../jd/J11_rtos_handson.html) | 700 | Q4. 제목과 §1 논리가 구 요건 기준 | 갱신 |
| [J12 무선](../jd/J12_wireless_ble_wifi_thread.html) | 653 | Q5. §6 답변 문장만 있으면 충분 | 유지 |
| [J13 회로도·bring-up](../jd/J13_schematics_bringup_collab.html) | 651 | Q6·R2b. §3.4가 R4의 재료다 | 유지 |
| [J14 디버깅 툴](../jd/J14_debug_tools_workflows.html) | 700 | Q7. §2.6이 인트로의 performance 항목을 받친다 | 유지 |
| [C00 로드맵](../concepts/C00_roadmap.html) | 160 | **JD 지도 전체가 구 JD.** 입구 문서라 파급이 크다 | 갱신 (최우선) |
| [C01 Cortex·부트·툴체인](../concepts/C01_arm_cortex_boot_toolchain.html) | 1084 | Q3·R1. 4행 인용만 낡음 | 유지 |
| [C02 제약 환경 C/C++](../concepts/C02_c_cpp_constrained.html) | 967 | Q2. 4행에 삭제된 인용 하나 | 유지 |
| [C03 BSP·주변장치](../concepts/C03_bsp_peripheral_drivers.html) | 1085 | R2c. 4행 인용 두 개가 모두 현행 JD에 없다 | 갱신 |
| [C04 RTOS](../concepts/C04_rtos_freertos_zephyr.html) | 1265 | Q4의 절반. 전체 정독은 이번에 하지 않는다 | 갱신 |
| [C05 저전력·발열](../concepts/C05_low_power_thermal.html) | 1043 | R5·Bonus 1. 4행 단어 하나만 | 유지 |
| [C06 무선](../concepts/C06_wireless_ble_wifi_thread.html) | 1057 | Q5. 심화 절은 이번에 열지 않는다 | 갱신 |
| [C07 OTA·secure boot](../concepts/C07_ota_secure_boot.html) | 916 | OTA 삭제, Bonus secure boot 유지 | 하향 (배너) |
| [C08 온디바이스 ML](../concepts/C08_on_device_ml_inference.html) | 991 | 삭제, Bonus ML 런타임만 | 하향 (배너) |
| [C09 공장 테스트](../concepts/C09_factory_test_calibration.html) | 1039 | 삭제, Bonus EVT/DVT/PVT만 | 하향 (배너) |
| [C10 디버깅·bring-up·회로도](../concepts/C10_debugging_bringup_schematics.html) | 831 | R6·Q6·Q7. 이 세트의 최강 노트 | 유지 |
| [S01 C 코딩 14문제](../study/S01_c_coding_drills.html) | 1327 | Q2. 24행 근거만 낡음 | 유지 |
| [S02 드라이버·RTOS 29문항](../study/S02_drivers_rtos_qa.html) | 634 | R2c·Q4. 338행 근거만 | 유지 |
| [S03 전력·무선 23문항](../study/S03_power_wireless_qa.html) | 509 | R5·Q5. 30행 단어만 | 유지 |
| [S04 OTA·공장 문답](../study/S04_ota_secure_factory_qa.html) | 589 | §1 도입이 삭제된 두 문장을 근거로 제시 | 갱신 (Bonus용으로 축소) |
| [S05 시스템 설계](../study/S05_system_design_ai.html) | 587 | **배너 대상 아님.** MCU와 SoC IPC 절이 R2d의 재료다 | 갱신 (409행만) |
| [S06 디버깅·STAR·영어](../study/S06_debug_scenarios_stories.html) | 546 | R6 + 모든 스토리의 집합소. §5.1에 존재하지 않는 인용 | 갱신 (최우선) |
| 코딩 5문제 + 해설 5 + 사용법 | 약 2600 | 문제 선정은 여전히 타당. 근거 문구만 낡음 | 유지 |
| [P00 인덱스](P00_index.html) | 53 | 이미 하향 항목을 명시하고 있다 | 유지 |
| [P01 마스터 플랜](P01_tech_session_master_plan.html) | 228 | §5 빈출 표에 하향 항목 3행이 남아 있다 | 갱신 |
| [P02 JD 개정 분석](P02_jd_change_2026-09-23.html) | 139 | 이 감사의 근거 문서. 정확하다 | 유지 |
| [P03 자리 지도](P03_embedded_role_map.html) | 138 | 7자리 비교가 9/29 스냅샷과 일치한다 | 유지 |
| [P04 4일 스프린트](P04_4day_sprint.html) | 111 | §4의 P1~P3을 끼워 넣으면 된다 | 갱신 |

집계하면 유지 14, 갱신 16, 하향 6, 코딩·플랜 묶음이 나머지다. **버려야 하는 노트는 없다.** 4,900줄을 하향했지만 그 내용은 Bonus 세 줄과 인접 자리(DevOps의 OTA, AI 팀의 추론)를 이해하는 데 그대로 쓰인다.

---

## 2. 낡은 인용 목록 — 파일·절·무엇이 틀렸나·어떻게 고치나

### 2.1 삭제된 JD 문장을 "JD 근거"로 달고 있는 노트

현행 JD에 **존재하지 않는 문장**을 근거로 인용한 곳이다. 노트를 읽다가 이 줄을 보면 "이게 JD에 있다"고 착각하게 되고, 면접에서 인용하면 면접관이 모르는 문장을 말하게 된다.

| 파일 | 위치 | 지금 무엇이 틀렸나 | 고치는 법 |
|---|---|---|---|
| [B00](../bsp/B00_bsp_overview.html) | 3행 헤더 "JD 근거" | "peripheral driver integration ... and **RTOS task scheduling**" 인용. 두 표현 모두 삭제됨 | "Own BSP development, board bring up, peripheral drivers (SPI, I2C, UART, I2S etc.), and system integration"으로 교체 |
| [B01](../bsp/B01_bsp_anatomy.html) | 3행 헤더 | 같음 | 같음 |
| [B02](../bsp/B02_zephyr_board_port.html) | 3행 헤더 | 같음 | 같음. 덧붙여 "이 노트는 RTOS 쪽 OS 요건을 받친다"고 범위를 명시 |
| [B03](../bsp/B03_linux_android_bsp.html) | 3행 헤더 | 같음 + "Cortex-M or Cortex-A processors **and associated toolchains**" — 뒷구절 삭제됨 | 헤더를 신 요건 "embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes"로 바꾼다. 이 노트는 그 요건의 주력이 되었다 |
| [B04](../bsp/B04_bsp_interview.html) | 3행 헤더 | 같음 | 같음 |
| [C03](../concepts/C03_bsp_peripheral_drivers.html) | 4행 "JD 연결" | "Develop board support packages (BSPs) and integrate peripheral drivers" + "Work directly with the hardware team on new silicon and sensor integrations" — **두 문장 모두 어느 버전 JD에도 지금 없다** | 신 R2 문장으로 교체. 두 번째 문장은 신 R4("ensure our hardware are built and functioning to the specifications")로 갈아타면 오히려 유리하다 |
| [C04](../concepts/C04_rtos_freertos_zephyr.html) | 4행 "JD 연결" | "own RTOS task scheduling" + "RTOS (FreeRTOS, Zephyr, or similar)" 둘 다 삭제 | "embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes"로 교체하고, 노트 위치를 "OS 요건의 RTOS 절반"으로 재정의 |
| [C07](../concepts/C07_ota_secure_boot.html) | 4행 "JD 연결" | "Build and maintain OTA update infrastructure" 인용 | 배너 적용 완료. 헤더는 Bonus "secure boot, firmware signing, or hardware root of trust"만 남기는 것이 맞다 |
| [C08](../concepts/C08_on_device_ml_inference.html) | 4행 "JD 연결" | 삭제된 on-device AI 문장 + **"the runtime environment that hosts on-device intelligence"** — 이 문장은 어느 JD 버전에도 없다 | 배너 적용 완료. 헤더는 Bonus "Exposure to ML inference runtimes"만 남긴다 |
| [C09](../concepts/C09_factory_test_calibration.html) | 4행 "JD 연결" | "Develop factory test and calibration firmware for manufacturing" 인용 | 배너 적용 완료. 헤더는 Bonus "EVT/DVT/PVT milestones"만 남긴다 |
| [J04](../jd/J04_ota_infrastructure.html) | 1행 제목 · 3행 "Responsibility 4/7" | 제목 자체가 삭제된 JD 줄이다 | 배너 적용 완료. 제목은 유지하고 "구 JD(8/26판) Responsibility"임을 분류에 명시 |
| [J05](../jd/J05_on_device_ai_budgets.html) | 1행 제목 · 3행 "Responsibility 5/7" | 같음 | 같음 |
| [J06](../jd/J06_factory_test_calibration.html) | 1행 제목 · 3행 "Responsibility 6/7" · 4행 | 같음 + "JD 7개 Responsibility 중 가장 확실하게 이기는 항목"이라는 판정이 남아 있다 | 배너 적용 완료. 4행 판정 문장은 §3에서 별도로 다룬다 |
| [S04](../study/S04_ota_secure_factory_qa.html) | 11행 §1 도입 | "JD 문장은 세 개다"로 시작해 삭제된 두 문장을 이 노트의 존재 이유로 제시 | "이 노트의 JD 근거는 Bonus 두 줄(secure boot·EVT/DVT/PVT)이다"로 다시 쓴다 |
| [S05](../study/S05_system_design_ai.html) | 409행 | 삭제된 on-device AI 문장을 §의 근거로 인용 | Bonus "ML inference runtimes"로 교체하거나 절을 개념 연습으로 강등 |
| [C02](../concepts/C02_c_cpp_constrained.html) | 4행 | "support model inference within memory and latency budgets" 인용 | 그 인용만 삭제. 나머지 두 인용은 유효하다 |
| [J09](../jd/J09_c_cpp_resource_constrained.html) | 18행 | 같은 문장을 "같이 읽어야 할 JD 문장"으로 제시 | 같음 |
| [J03](../jd/J03_power_thermal_always_on.html) | 128행 | "이게 JD의 Collaborate with the on-device AI team"이라는 근거 제시 | 논리 자체는 유효하다. "구 JD에 있었고 지금은 빠졌지만 물리는 그대로다"로 한 줄 보강 |

### 2.2 문구가 바뀐 JD를 옛 표현으로 인용한 노트

문장이 사라진 것은 아니고 표현이 바뀐 경우다. 위험도는 낮지만 면접에서 JD를 인용할 때 틀린 단어를 쓰게 된다.

| 파일 | 위치 | 옛 인용 | 현행 문장 |
|---|---|---|---|
| [J08](../jd/J08_three_years_experience.html) | 제목 · §1 표 · §2.5 · §6.1 표 | "3+ years" | **5+ years**. 제목·본문 전부 5+로 바꿔야 한다 |
| [J10](../jd/J10_arm_cortex_toolchains.html) | 제목 · §1 표 · Q11 앞 | "... processors **and associated toolchains**" | 뒷구절 삭제. 툴체인 내용은 여전히 좋은 준비이므로 "요건에서는 빠졌지만 실무 필터"라고 표기 |
| [J11](../jd/J11_rtos_handson.html) | 제목 · §1 표 · 700행 | "RTOS (FreeRTOS, Zephyr, or similar)" · "Responsibility 2번의 RTOS task scheduling과 짝이다" | "embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes". 짝이 되는 Responsibility가 없어졌으므로 §1 해설의 논리를 다시 쓴다 |
| [J02](../jd/J02_bsp_drivers_rtos_scheduling.html) | 제목 · §0 · §1 표 · §3.2 · 523행 · §6 표 | "RTOS task scheduling" | 그 자리에 **board bring up**과 **system integration**이 들어왔다. 노트에서 가장 많이 손봐야 할 파일이다 |
| [C01](../concepts/C01_arm_cortex_boot_toolchain.html) | 4행 | "Cortex-M or Cortex-A **architectures** and associated toolchains" | 원문은 processors이고 뒷구절은 삭제됨 |
| [C05](../concepts/C05_low_power_thermal.html) | 4행 | "always-on, battery-powered **devices**" | 원문은 operation |
| [S03](../study/S03_power_wireless_qa.html) | 30행 | 같음 | 같음 |
| [S02](../study/S02_drivers_rtos_qa.html) | 338행 | "왜 묻나: JD RTOS task scheduling" | 문항 자체는 유효하다. 근거를 OS 요건으로 바꾼다 |
| [S01](../study/S01_c_coding_drills.html) | 24행 매핑 표 | "RTOS task scheduling, ML 추론 메모리 예산"을 문제 선정 근거로 | 근거만 교체. 문제 세트는 그대로 좋다 |

### 2.3 구조가 낡은 것

| 파일 | 무엇이 틀렸나 | 고치는 법 |
|---|---|---|
| [C00 로드맵](../concepts/C00_roadmap.html) §1.1·§1.2 | **JD ↔ 노트 지도 전체가 구 JD 14줄로 되어 있다.** 삭제된 3줄이 여전히 표에 있고, 신설 2줄(vendor, HW 스펙 검증)과 FPGA·system integration 행이 없다. "3+ years", "RTOS (FreeRTOS, Zephyr)" 행도 그대로다 | 이 감사의 §1.1~§1.4 표로 통째 교체한다. 이 노트가 입구이므로 여기가 틀리면 전체가 틀린 순서로 읽힌다 |
| [J01](../jd/J01_firmware_c_cpp_arm.html)~[J07](../jd/J07_debug_hw_sw_tools.html) 3행 | "Responsibility n/7" — 현행 Responsibilities는 **6줄**이고 그중 3줄이 삭제·2줄이 신설이라 번호가 전부 틀렸다 | J01=R1, J02=R2, J03=R5, J07=R6으로 다시 매기고, 신설 R3·R4에는 번호를 비워 둔다(§5의 새 노트 자리) |
| `JD_NOTES_SPEC.md` 61~70행 | 파일 배정표가 구 JD 14문장으로 고정되어 있다. 새 노트를 쓸 때 이 표를 보면 또 구 JD를 쓰게 된다 | 배정표를 현행 JD로 갱신하고, 삭제 항목 3개는 "구 JD 아카이브"로 표시 |
| `BSP_NOTES_SPEC.md` 11행 | "JD 원문의 해당 문장"이 구 R2다 | 신 R2로 교체 |
| [P01 마스터 플랜](P01_tech_session_master_plan.html) §5 개념 빈출 체크 | 표에 OTA·공장·온디바이스 AI 행이 "30초 안에 답해야 하는 것"으로 남아 있다. §1 채점축 표에는 "JD에서 빠짐"이라고 이미 적혀 있어 같은 문서 안에서 충돌한다 | §5 세 행을 "Bonus 대비, 물으면 답하는 수준"으로 내리고 vendor·FPGA·OS 폭 행을 추가 |

### 2.4 결론 자체가 낡아서 방향을 잘못 안내하는 절 — 위험도 최상

| 파일 | 위치 | 무엇이 문제인가 |
|---|---|---|
| [B03](../bsp/B03_linux_android_bsp.html) | §8.1 현실적인 비중 추정 | "같은 회사의 다른 공고(Embedded Application = Android 앱, System Test = Qualcomm/Android)가 따로 있다는 사실은, 이 자리의 무게중심이 MCU/RTOS 쪽이라는 신호다"라고 추론한 뒤, **Linux/Android BSP는 소유할 영역이 아니라 대화만 통하면 되는 영역**이라고 준비 깊이를 정했다 |

이 추론의 전제가 두 번 깨졌다.

첫째, "Embedded Application Engineer"는 9/21에 이름이 `Embedded Software Engineer`로 바뀌고 9/22에 본문이 전면 재작성되어 **Android 앱이 아니라 커널·유저스페이스·MCU를 가로지르는 시스템 소프트웨어 자리**가 되었다([P03](P03_embedded_role_map.html) §0). 둘째, BSP JD 자신이 9/23에 필수 요건에서 **eLinux와 AOSP를 이름으로 지목**했다. 즉 "Linux는 남의 일"이라는 결론이 정확히 반대 방향으로 뒤집혔다.

고치는 법은 §8.1 두 문단을 다시 쓰는 것이다. 깊이 3단계(§8.2 A·B·C)와 정직 스크립트(§8.3)는 **그대로 유효하고 오히려 더 중요해졌다.** 바꿀 것은 "왜 이만큼만 하면 되나"의 근거 문단뿐이다. 그리고 A 목록(반드시 할 수 있어야 하는 것)은 요건이 격상된 만큼 한 단계 더 올려야 한다. 최소한 GKI·vendor module·dtbo·AIDL 정도는 B에서 A로 옮긴다.

---

## 3. 내 경험에 대한 과잉 주장 — 근거 대조

대조 기준은 [컨텍스트 §3.1 요구사항 매칭표](../../../hark_ai_embedded_swe_context.html)의 레쥬메 근거와 [J00 확인 필요 72건](../jd/J00_open_questions.html)이다. 규칙은 하나다. **J00에 "미확인"으로 남아 있는 항목을 다른 노트가 "✅ 강함"으로 확정하면 그것이 과잉 주장이다.**

| # | 파일 | 그 노트의 주장 | 근거 상태 | 어떻게 고치나 |
|---|---|---|---|---|
| O1 | [C07](../concepts/C07_ota_secure_boot.html) 5행 | "SSD FW의 firmware download/commit(NVMe Firmware Image Download·Firmware Commit), flash 파티션, CRC는 **이미 안다**" | [J00](../jd/J00_open_questions.html) 우선순위 **3번**이 바로 이것을 "관여했는가 — 미확인"으로 남겨 두었다. 같은 사실을 한 노트는 미확인, 한 노트는 기지(旣知)로 적었다 | "SSD의 firmware download/commit 흐름은 **개념으로는** 안다. 내가 직접 구현·검증했는지는 확인 필요"로 바꾼다. Bonus(secure boot) 답변의 출발점이 여기라 정확도가 중요하다 |
| O2 | [J06](../jd/J06_factory_test_calibration.html) 4행 | "✅ 강함 — ... **JD 7개 Responsibility 중 Don이 가장 확실하게 이기는 항목이다**" | 두 겹으로 틀렸다. ① 그 Responsibility는 9/23에 삭제됐다 ② [J00](../jd/J00_open_questions.html) 우선순위 **1번**이 "test-node가 DUT 펌웨어였나 스테이션 아키텍처였나"를 미확인으로 남겼는데, JD는 firmware를 요구했다. 즉 "가장 확실하게 이긴다"의 근거가 확인되지 않았다 | 배너는 달았다. 4행을 "제조 흐름 경험은 강하다. 다만 DUT 펌웨어를 직접 썼는지는 확인 전까지 'test-node 아키텍처를 설계하고 리드했다'까지만 말한다"로 바꾼다 |
| O3 | [C09](../concepts/C09_factory_test_calibration.html) 5행 | "bring-up → NPI → MP 흐름, factory test-node 설계, 스트레스 시나리오, **yield 논의는 이미 강점**" | [J00](../jd/J00_open_questions.html) J06 항목에 "limit 설정이나 수율 분석에 직접 관여했는지, 아니면 품질/PE 팀 담당이었는지"가 미확인으로 있다 | "yield 논의"를 "yield가 왜 움직이는지 대화가 통한다"로 낮춘다 |
| O4 | [J08](../jd/J08_three_years_experience.html) 5행 · 474행 | "요건 하한의 **2배 이상**", "3+ years professional ✅ **크게 상회**" | 요건이 5+로 올랐다. 2021년부터를 펌웨어 경력으로 세면 약 5년으로 **하한에 걸친다.** 2021년 이전 약 3년의 성격은 [J00](../jd/J00_open_questions.html) J08 첫 항목에서 "임베디드였나 웹·앱이었나 미확인"이다. 그 3년이 웹·앱이면 "8년"을 펌웨어 연차로 말하는 순간 꼬리질문에서 깎인다 | 제목과 본문을 5+로 고치고, 연차 문장을 "eight years in software, five in firmware" 형태로 **구성을 밝히는** 버전으로 바꾼다. J00 항목에 답이 정해지기 전에는 "약 8년 펌웨어"라고 말하지 않는다 |
| O5 | [B04](../bsp/B04_bsp_interview.html) Q40 | "벤더 플랫폼 위에서 일해 본 감각이 있는지. **Don은 Apple 경험으로 답할 수 있다**" | [P02](P02_jd_change_2026-09-23.html) §4에 "Apple에서 외부 벤더와 직접 소통하는 위치인지, 사내 실리콘 팀과만 일하는지"가 미확인으로 남아 있다. 벤더 BSP drop을 직접 받아 통합한 경험이 없을 수 있다. 하필 vendor가 신 JD의 Responsibility로 올라온 항목이다 | Q40의 답은 지식으로는 정확하니 유지하고, "Don 연결"을 "사내 실리콘 팀·IP 벤더와 주고받은 경험으로 답한다. '벤더 BSP drop을 받아 본 경험'은 사실 확인 후에만 주장한다"로 바꾼다 |
| O6 | [C06](../concepts/C06_wireless_ble_wifi_thread.html) 6행 | "라디오 칩을 호스트에 붙이는 HW/인터페이스(PCIe, **UART**, SPMI, RFFE)와 bring-up은 이미 안다" | 레쥬메 근거 버스는 I2C·SPI·DMA·PCIe·SPMI·RFFE다. UART는 근거가 없다(HCI 전송으로 흔하긴 하다) | 목록에서 UART를 빼거나 "HCI over UART는 개념"으로 분리한다. 사소하지만 버스 이름은 면접관이 바로 파고드는 지점이다 |

확인 결과 **문제가 없었던 것**도 적어 둔다. 이쪽은 손대지 않는다.

- [B00](../bsp/B00_bsp_overview.html) §3 좌표표와 §3.2 금지 표현, [B01](../bsp/B01_bsp_anatomy.html)·[B04](../bsp/B04_bsp_interview.html)의 "BSP를 소유한 적 없다" 스크립트는 사실만 담고 있다. 이 노트 세트의 가장 잘 된 부분이다
- [B02](../bsp/B02_zephyr_board_port.html) 헤더가 "Zephyr 보드를 포팅해 본 적이 없다"를 먼저 쓴 것, [J11](../jd/J11_rtos_handson.html)이 "상용 RTOS 이름이 레쥬메에 하나도 없다"를 첫 줄에 쓴 것도 정확하다
- [S06](../study/S06_debug_scenarios_stories.html) §3 STAR 7개는 숫자와 상황을 전부 `<채우기>`로 비워 두었다. 지어낸 사실이 없다
- [P01](P01_tech_session_master_plan.html) §6.1의 "Zephyr를 nRF52840에서 돌리고 있다" 문장에 "실제로 했을 때만 말한다"는 조건이 붙어 있다

한 건은 경험 주장이 아니라 **JD 사실 오류**이지만 위험도가 가장 높아 여기 함께 적는다.

[S06 §5.1 Why Hark](../study/S06_debug_scenarios_stories.html)의 한국어 요점과 English 답변이 JD 인용으로 `"work with the agent team on model execution and memory constraints"`를 쓴다. **이 문장은 현행 JD에도, 9/23 이전 JD에도 없다.** 컨텍스트 파일의 초기 추정("Agent 팀과 협업")이 인용문 형태로 굳어진 것으로 보인다. Why Hark는 거의 확실히 나오는 질문이고 그대로 소리 내어 말하게 설계된 문장이라, 면접관이 "그 문장은 우리 공고에 없는데요"라고 되물을 수 있다. 두 번째 이유를 다음 취지로 바꾼다. "모델·하드웨어·펌웨어를 한 회사가 같이 설계한다"는 **관찰**은 유지하고, JD 인용은 신 JD에 실제로 있는 "vendor code integrations"와 "FPGA-based system integrations", 그리고 인접 공고에 있는 NPU·추론 런타임 항목으로 바꾼다.

---

## 4. 지금 공부할 것, 어디서 — 바쁜 사람용 우선순위

전제는 [P04](P04_4day_sprint.html)와 같다. 하루 2시간, 연락은 이번 주 안에 온다. 아래 순서는 **배점(JD에 몇 번 나오나) x 현재 공백 x 준비 비용**으로 매겼다. 새로 읽을 것은 최소화하고 이미 있는 절로 보낸다.

### P1. 벤더 협업 스토리와 답변 (90분) — 배점 최상, 공백 최대

근거는 JD에 두 줄이다. 인트로의 "vendor code integrations"와 Responsibility의 "Working closely with vendors on system integration and validation".

읽을 곳은 [J02 §2.10 드라이버 통합 워크플로](../jd/J02_bsp_drivers_rtos_scheduling.html), [J01 §3.4 벤더 SDK를 리포에 어떻게 넣나](../jd/J01_firmware_c_cpp_arm.html), [B04 Q17·Q40](../bsp/B04_bsp_interview.html), [B03 §7 BSP drop이 담고 있는 것](../bsp/B03_linux_android_bsp.html) 네 곳이다. 30분이면 충분하다.

남은 60분으로 만들 산출물은 셋이다. 첫째, 3분짜리 벤더 스토리 하나. 벤더 또는 IP 제공 팀이 준 코드·SDK·레퍼런스 설계를 우리 시스템에 맞추면서 무엇이 충돌했고 어떻게 합의했는지. 둘째, "벤더가 우리 문제를 인정하지 않을 때 어떻게 하나"에 대한 답. 증거를 어떤 형식으로 만들어 보내는지가 핵심이고, 이건 Don이 매일 하는 일이다. 셋째, [P02 §4](P02_jd_change_2026-09-23.html)의 미확인 항목에 답 정하기. 외부 벤더와 직접 소통하는 위치인지 아닌지에 따라 O5의 주장 강도가 달라진다.

### P2. FPGA 되묻기와 스토리 C (60분) — 인트로에 박힌 단어, 기술 노트 0개

먼저 되묻는다는 원칙은 [P04 Day 3](P04_4day_sprint.html)에 이미 있다. Don의 FPGA 경험은 pre-silicon 검증용이고, Hark의 "FPGA-based system integrations"가 **제품 안의 FPGA**를 뜻할 가능성이 있다.

그 가능성을 뒷받침하는 근거가 이번 감사에서 하나 나왔다. Embedded DevOps 공고의 Bonus에 "Hands-on knowledge of hardware-in-the-loop (HIL) testing or **FPGA simulation and regression testing frameworks**"와 "Experience with **traceable FPGA release management**"가 있다. 릴리스 관리 대상이라는 것은 FPGA 이미지가 **기기 또는 랩 장비에 실제로 배포된다**는 뜻이다. 즉 일회성 검증 보드가 아니다.

산출물은 둘이다. 첫째, [S06 ST4](../study/S06_debug_scenarios_stories.html)를 3분 버전으로 채우기. 첫 부팅 실패 사례 한 장면과 Trace32로 무엇을 봤는지를 구체적으로. 둘째, 되묻는 문장 세 개. "제품에 FPGA가 들어가나요, 아니면 pre-silicon·랩 검증용인가요", "FPGA 이미지와 펌웨어의 버전을 어떻게 묶어 관리하시나요", "FPGA에 붙은 인터페이스는 무엇인가요". 이 세 질문은 그 자체로 경험자만 할 수 있는 질문이라 점수가 된다.

### P3. 하드웨어가 스펙대로인지 검증하는 이야기 (60분) — 신설 Responsibility, Don의 실제 업무

JD 문장은 "Working with Hardware and Software team to ensure our hardware are built and functioning to the specifications"다. 레쥬메의 "sign off on hardware safety margins (reliability vs performance/power)"가 정확히 이 줄이다.

읽을 곳은 [J13 §3.4 하드웨어 버그를 리포트하는 법](../jd/J13_schematics_bringup_collab.html)과 [J03 §6.2](../jd/J03_power_thermal_always_on.html)다. 산출물은 3분짜리 margin sign-off 스토리와, "HW 문제인지 FW 문제인지 어떻게 증명하나"의 5단 절차다. 후자는 [J07](../jd/J07_debug_hw_sw_tools.html)과 [S06 §0](../study/S06_debug_scenarios_stories.html)에 틀이 있으니 그걸 쓴다. [J00](../jd/J00_open_questions.html) J13 항목의 "sign-off에서 HW 엔지니어와 의견이 갈린 적이 있는지"에 답을 정해 두면 행동 질문 하나가 같이 해결된다.

### P4. 임베디드 OS 네 종류를 한 표로 (60분) — 요건 격상, 커버리지 부분

목표는 "eLinux, AOSP, VxWorks, RTOS 중 무엇을 해 보셨나"에 정직하게 답하고 **네 개의 차이를 설명하는** 것이다. 답 스크립트는 [P02 §5.2](P02_jd_change_2026-09-23.html)에 이미 있다. 여기에 얹을 것은 지도 한 장이다.

읽을 곳은 [B03 §8 방어 범위](../bsp/B03_linux_android_bsp.html)와 [C04 §15 FreeRTOS vs Zephyr 비교](../concepts/C04_rtos_freertos_zephyr.html)다. 표의 축은 이렇게 잡는다. 스케줄링 모델, 메모리 보호(MPU/MMU), 부트 체인 길이, 드라이버 모델(devicetree 유무), 인증·실시간 보증(VxWorks가 여기서 갈린다), 그리고 "Hark 기기에서 이게 어디에 쓰일까"라는 추정 한 칸. eLinux는 특정 배포가 아니라 임베디드용 Linux 일반을 가리키는 말이라는 점만 알아도 그 질문에서 당황하지 않는다.

### P5. "system integration"을 내 말로 정의하기 (45분) — JD에 두 번 나오고 노트에 0번 나온다

Hark가 이 단어를 R2 꼬리와 R3에 각각 넣었다. "드라이버는 됐는데 시스템으로는 안 되는" 문제를 맡기려는 뜻으로 읽는 게 맞다. 두 프로세서 사이의 경계, 부팅 순서와 의존성, 전원 상태 전환, 리셋 복구, 클럭·공유 버스 경쟁 같은 것들이다.

이미 있는 재료로 충분하다. [B04 Q41 MCU와 Android 경계](../bsp/B04_bsp_interview.html), [B01 §2.3 두 프로세서 부트](../bsp/B01_bsp_anatomy.html), [J01 Q14 무엇을 어디서 돌릴까](../jd/J01_firmware_c_cpp_arm.html), [S05 MCU와 SoC IPC 절](../study/S05_system_design_ai.html)이다. S05는 배너 대상이 아니므로 그대로 쓴다. 산출물은 60초 정의 한 개다. "시스템 통합이란 각 블록이 다 동작하는데 합치면 안 되는 문제를 맡는 일이고, 그건 대개 경계 세 곳에서 생긴다 — 전원·클럭 순서, 두 프로세서 사이 프로토콜, 그리고 리셋과 복구다."

### P6. 최강점 유지 (30분)

[C10](../concepts/C10_debugging_bringup_schematics.html)과 [S06 D01·D08·D11](../study/S06_debug_scenarios_stories.html) 세 시나리오만 소리 내어 답한다. 이미 강한 영역에 시간을 더 쓰지 않는다. 다만 이 축이 배점 최대이므로 **입으로 나오는 상태**는 유지한다.

### P7. 코딩 유지 (25분)

[코딩 01 링버퍼](../problems/01_spsc_ring_isr.html) 한 문제만 25분 안에. [P04](P04_4day_sprint.html)와 같다.

### 버릴 것 — 이번 준비 기간에 열지 않는다

| 노트 | 이유 |
|---|---|
| [J04](../jd/J04_ota_infrastructure.html) · [C07](../concepts/C07_ota_secure_boot.html) 본문 | OTA가 Responsibility에서 삭제. Bonus의 secure boot만 30초 답변으로 준비 |
| [J05](../jd/J05_on_device_ai_budgets.html) · [C08](../concepts/C08_on_device_ml_inference.html) | 삭제. Bonus의 "exposure" 수준이면 충분 |
| [J06](../jd/J06_factory_test_calibration.html) · [C09](../concepts/C09_factory_test_calibration.html) 본문 | 삭제. EVT/DVT/PVT 스토리로만 유지 |
| [C06](../concepts/C06_wireless_ble_wifi_thread.html) 심화 절 | JD가 familiarity만 요구. [J12 §6](../jd/J12_wireless_ble_wifi_thread.html) 답변 문장만 |
| [C04](../concepts/C04_rtos_freertos_zephyr.html) 전체 정독 | API 암기 경쟁에서는 진다. §15 비교표와 §7 ISR 규칙만 |

합계 약 5시간 45분이다. 하루 2시간이면 3일, [P04](P04_4day_sprint.html)의 4일 스프린트에 P1~P3를 끼워 넣는 형태가 현실적이다.

---

## 5. 비어 있어서 새로 써야 하는 노트

이 감사와 병행해 `bsp/B05`·`bsp/B06` 두 노트가 작성 중이고, 감사 시점에 [B05 벤더·FPGA 통합](../bsp/B05_vendor_fpga_integration.html)이 먼저 올라왔다. 아래 세 자리 중 앞의 둘이 그쪽에서 메워지는 것이 전제다. 중복을 피하기 위해 여기서는 **자리와 범위만** 적는다.

| 자리 | 다뤄야 하는 JD 줄 | 최소 내용 |
|---|---|---|
| N1. 벤더 협업·시스템 통합·검증 | R3 전체, 인트로의 vendor code integrations | 벤더 산출물의 종류(SDK·BSP drop·IP·펌웨어 블롭·레퍼런스 설계), 우리 트리에 넣는 방식과 업스트림 추적, 버전 고정과 패치 관리, 이슈를 증거와 함께 올리는 절차, 벤더 일정에 물린 릴리스를 관리하는 법, 검증 책임 분담 |
| N2. FPGA 기반 시스템 통합 | 인트로의 FPGA-based system integrations | 제품 FPGA와 검증 FPGA의 차이, prototyping과 emulation, 클럭 비율이 달라서 생기는 함정, 비트스트림 로딩과 부팅 순서, 레지스터·AXI 인터페이스 계약, FPGA 이미지와 펌웨어의 버전 묶기, 실리콘으로 넘어갈 때 달라지는 것 |
| N3. 임베디드 OS 스펙트럼 | Q4 embedded operating systems | eLinux·AOSP·VxWorks·RTOS 네 축 비교표, 각 축에서 BSP라는 단어가 가리키는 것, Don의 좌표와 방어선. §4의 P4 산출물을 그대로 노트화하면 된다 |

그 밖에 고칠 것은 §2의 표가 곧 작업 목록이다. 순서는 이렇다.

- [ ] 1순위: [S06 §5.1](../study/S06_debug_scenarios_stories.html)의 존재하지 않는 JD 인용 교체 (§3 마지막)
- [ ] 2순위: [C00 로드맵](../concepts/C00_roadmap.html) §1.1·§1.2 매트릭스를 이 노트 §1로 교체
- [ ] 3순위: [B03 §8.1](../bsp/B03_linux_android_bsp.html) 비중 추정 문단 재작성 (§2.4)
- [ ] 4순위: [J08](../jd/J08_three_years_experience.html) 3+ → 5+ 및 연차 구성 문장 (§3 O4)
- [ ] 5순위: bsp 5개와 concepts 4개의 헤더 "JD 근거" 한 줄씩 (§2.1)
- [ ] 6순위: `JD_NOTES_SPEC.md`·`BSP_NOTES_SPEC.md` 배정표 갱신

---

## 6. 나머지 여섯 공고가 말해 주는 팀의 실체

같은 부서 공고 여섯 개를 함께 읽으면 BSP 자리 혼자서는 안 보이는 것이 보인다. 자리 적합도 판단은 [P03](P03_embedded_role_map.html)에 있으니 여기서는 **제품과 팀에 대해 알 수 있는 것만** 한 단락씩 적는다. 역질문 재료로 쓴다.

**Embedded Software Engineer (시스템).** BSP 바로 옆자리이고, Bonus 목록이 제품의 하드웨어 구성을 거의 다 흘린다. DSP 오디오, 카메라 ISP, NPU와 하드웨어 액셀러레이터, 멀티코어 SoC, AOSP, 그리고 "leading commercial silicon SoCs and their **vendor toolchains**"다. 즉 기기에는 마이크만이 아니라 **카메라**가 있고, 상용 SoC 벤더의 툴체인 위에서 일한다. 본문은 "RTOS, bare metal, Linux 환경을 같은 제품에서" 다룬다고 명시한다. BSP가 올린 드라이버를 받아 커널과 유저스페이스까지 잇는 사람이 이 자리이고, 면접에서 "제 드라이버를 누가 받나요"의 답이 여기다.

**Embedded Software Engineer, Platform.** 요구 OS가 "Unix or BSD Linux, VxWorks, QNX"이고 Responsibility에 "**virtualization frameworks and multi-OS system design**"이 들어 있다. 이것이 BSP JD가 갑자기 eLinux·AOSP·VxWorks·RTOS를 나열한 이유를 설명한다. 이 회사는 하나의 멀티코어 SoC 위에 **여러 OS를 동시에 올릴 계획**이고, 그래서 BSP 엔지니어에게도 OS 한 종류가 아니라 폭을 요구한다. MQTT·RabbitMQ·CoAP가 요건에 있는 것으로 보아 기기는 백엔드와 상시 대화한다. 역질문으로 "하이퍼바이저를 쓰시나요, 어느 도메인이 어느 OS인가요"는 아주 좋은 질문이다.

**Embedded DevOps Engineer.** BSP JD에서 OTA가 사라진 이유가 여기에 있다. 이 공고의 Responsibility에 "Manages device OTA, scheduled rollout, and fleet management"가 있고 요건에 "modern OTA systems including A/B image management, compression and delta update"가 있다. **OTA는 없어진 게 아니라 이 자리로 옮겨 갔다.** 그래서 [C07](../concepts/C07_ota_secure_boot.html)을 버리는 게 아니라 "옆자리가 소유하는 것을 아는 사람" 수준으로 유지하는 것이 맞다. 또 Bonus의 HIL·FPGA 시뮬레이션·traceable FPGA release management가 §4 P2의 근거다. 빌드와 릴리스가 이미 체계화되는 중이라는 뜻이기도 해서, BSP 엔지니어는 그 CI의 소비자가 된다.

**Graphics Engineer.** 7년 이상 Vulkan·OpenGL ES 경력에 커널 GPU 드라이버와 유저스페이스 그래픽 스택을 함께 소유한다. 기기에 **진짜 디스플레이와 GPU가 있다**는 확정 신호다. Bonus에 "Serializer/Deserializer, video links and display interfaces", "MIPI DSI, HDMI ... display driver integration on embedded Linux", 그리고 "**display virtualization**"이 있다. 앞 둘은 BSP의 일이다. 패널·SerDes·MIPI 링크를 처음 켜는 것은 보드 bring-up이고, 그 경계가 Don의 업무 범위와 겹친다. display virtualization은 Platform 공고의 multi-OS와 짝을 이룬다.

**Frontier UX Engineer.** 8년 이상 UI 경력, "Design과 함께 일한 경험", "이 일의 상당 부분은 선례가 없다"는 문장이 본문에 있다. 제품이 **디자인 주도**이고 UI 느낌에 회사가 큰 값을 매긴다는 뜻이다(밴드도 $150–350K다). BSP에 주는 함의는 간접적이지만 분명하다. 화면·햅틱·오디오의 **지연과 끊김**이 제품 품질로 직결되고, 그 예산의 바닥을 깔아 주는 사람이 BSP다. Bonus에 "system daemons, IPC, HAL, drivers, firmware" 노출이 있는 것도 이 팀이 위아래로 붙어 일한다는 신호다.

**Embedded AI Engineer.** 필수 요건에 "IMUs and other sensor types including accelerometers, gyroscopes, and **microphones**", "sensor data collection pipelines", "TFLite, llamacpp, **QNN**", "ARM Cortex-M/A, RISC-V, and DSPs", NPU 배포가 있다. QNN은 Qualcomm 런타임이므로 SoC 추정에 힘이 실린다([컨텍스트 2.7](../../../hark_ai_embedded_swe_context.html)의 추정과 일치). Bonus에 경량 LLM과 KV-cache, 그리고 "wearables, robotics, industrial sensing, or IoT" 제품 경험이 있다. BSP에 주는 함의는 **센서와 마이크 드라이버, 그 데이터 경로의 타임스탬프와 버퍼가 곧 AI 팀의 입력**이라는 것이다. on-device AI 협업이 JD에서 빠졌어도 조직에는 그대로 있다. 다만 그 협업은 이제 "예산을 협상하는 일"이 아니라 "데이터 경로를 제대로 만들어 주는 일"로 읽는 게 맞다.

**Technical Lead, On-Device AI Inference.** 이 공고는 9/29 스냅샷의 Embedded Software 부서 7개에는 없고 [P03](P03_embedded_role_map.html) §1의 7자리 표에 인접 팀으로 들어 있다. 추론 엔진과 ML 컴파일러를 설계하는 리드 자리가 별도로 열려 있다는 사실만 기억한다. 온디바이스 추론을 **전담 조직**이 맡는다는 뜻이고, BSP가 그 일을 겸하지 않는다는 뜻이기도 하다.

---

## 7. 면접에서 그대로 인용할 현행 JD 문장

노트의 낡은 인용을 다 고치기 전에도 이 여섯 문장만 정확히 외우면 위험은 사라진다. JD를 인용할 때는 **이 표에 있는 문장만** 쓴다.

| 쓸 자리 | 그대로 말할 문장 |
|---|---|
| 자기소개 끝, Why Hark | "The posting talks about owning firmware across the stack — board bring-up, vendor code integrations, custom peripheral drivers, FPGA-based system integrations, and system power and performance. That list is unusually close to what I've actually been doing." |
| 벤더 질문 | "Working closely with vendors on system integration and validation" |
| 하드웨어 검증 질문 | "Working with Hardware and Software team to ensure our hardware are built and functioning to the specifications" |
| BSP 범위 질문 | "Own BSP development, board bring up, peripheral drivers, and system integration" |
| OS 질문 | "embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes" |
| 디버깅 질문 | "Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG" |

쓰면 안 되는 인용도 같이 외운다. "OTA update infrastructure", "the on-device AI team", "factory test and calibration firmware", "RTOS task scheduling", "and associated toolchains", "3+ years", "the agent team". **일곱 개 모두 현행 JD에 없다.** 특히 마지막 것은 어느 버전에도 없었다.

---

## 8. 이 감사를 어떻게 했나 (재현용)

다음에 JD가 또 바뀌면 같은 절차를 반복한다. 30분이면 된다.

- [JD 스냅샷](../../JD_SNAPSHOT_2026-09-29.md)을 먼저 갱신한다. 이 파일이 기준이 아니면 감사는 의미가 없다
- 모든 노트의 첫 6줄(제목과 `>` 헤더)을 뽑아 "JD 근거"·"JD 연결" 줄만 훑는다. 낡은 인용은 거의 전부 여기에 있다
- 삭제된 표현을 문자열로 검색한다. 이번에는 "RTOS task scheduling", "associated toolchains", "OTA update infrastructure", "on-device AI team", "factory test and calibration firmware", "3+ years", "FreeRTOS, Zephyr, or similar" 일곱 개였다
- 신설된 표현을 파일별 등장 횟수로 센다. 0회 또는 플랜 노트에만 있으면 그게 공백이다. 이번에는 eLinux 0회, system integration 0회가 그렇게 잡혔다
- 과잉 주장은 [J00](../jd/J00_open_questions.html)의 미확인 항목과 다른 노트의 "✅ 강함" 판정을 교차 대조해서 찾는다. 사람이 읽어서 찾는 것보다 훨씬 빠르다
- 마지막으로 **결론 문단**을 의심한다. 인용은 검색으로 잡히지만, "그래서 이건 안 해도 된다"는 판단은 검색에 걸리지 않는다. [B03 §8.1](../bsp/B03_linux_android_bsp.html)이 그 예다

---

## 9. 체크리스트

- [ ] §3 마지막의 [S06 §5.1](../study/S06_debug_scenarios_stories.html) 존재하지 않는 JD 인용을 고쳤다
- [ ] §4 P1 — 벤더 스토리 3분을 소리 내어 3회 했다
- [ ] §4 P1 — 외부 벤더 소통 여부([P02 §4](P02_jd_change_2026-09-23.html) 미확인 항목)에 답을 정했다
- [ ] §4 P2 — FPGA 되묻기 세 문장을 외웠다
- [ ] §4 P2 — [S06 ST4](../study/S06_debug_scenarios_stories.html)의 빈칸을 채웠다
- [ ] §4 P3 — margin sign-off 스토리 3분을 만들었다
- [ ] §4 P4 — 임베디드 OS 네 종류 비교표를 백지에 그렸다
- [ ] §4 P5 — system integration 60초 정의를 말할 수 있다
- [ ] §3 O2·O3·O4 — 미확인 항목 3건([J00](../jd/J00_open_questions.html) 우선순위 1·3, J08 첫 항목)에 답을 정했다
- [ ] 연차를 말할 때 "8년 펌웨어"가 아니라 구성을 밝히는 문장을 쓴다
- [ ] [C00 로드맵](../concepts/C00_roadmap.html) 매트릭스를 §1로 교체했다
- [ ] [B03 §8.1](../bsp/B03_linux_android_bsp.html)을 다시 썼고, A 목록을 한 단계 올렸다
- [ ] 배너가 달린 여섯 노트는 이번 준비 기간에 열지 않는다
