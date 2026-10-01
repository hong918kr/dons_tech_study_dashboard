# P00. 준비 인덱스 — 여기서 시작

> **상태**: 📨 지원완료(9/21) · 리크루터 "이번 주 안에 회신" 확약(9/29) · 팀 OOO 복귀 대기
> **목표**: 1차 tech session 통과 · **지금은 [P06 7일 계획](P06_7day_plan.html) 진행 중** (9/30 시작)
> **쓰는 법**: 매일 이 페이지를 열고 오늘 할 것만 클릭한다

---

## 1. 지금 볼 것

| 순서 | 문서 | 언제 |
|---|---|---|
| ① | **[P06 7일 계획](P06_7day_plan.html)** | **매일 — 현재 진행 중** (하루 2~2.5시간, Zephyr 실습 포함) |
| ①-b | [P04 4일 압축판](P04_4day_sprint.html) | 시간이 부족해지면 이쪽으로 |
| ② | [P02 JD 개정 분석](P02_jd_change_2026-09-23.html) | 준비 시작 전 1회 — 무엇이 바뀌었나 |
| ③ | [P01 마스터 플랜](P01_tech_session_master_plan.html) | 시간이 더 생기면 — D-7 전체판 |
| ④ | [P03 자리 지도](P03_embedded_role_map.html) | 리크루터와 대화 전 — 7자리 중 내 위치 |
| ⑤ | [P05 노트 감사](P05_note_audit.html) | 노트가 현행 JD와 어긋나는 부분 · 낡은 인용 · 과잉 주장 |

## 2. 급할 때

| 상황 | 바로 갈 곳 |
|---|---|
| 면접까지 며칠 안 남았다 | [P06 §5 남은 시간별 대응](P06_7day_plan.html) |
| 내일 면접이 잡혔다 | [P04 §4 — 60분 압축판](P04_4day_sprint.html) |
| 무슨 말을 하면 안 되나 | [P04 §5](P04_4day_sprint.html) · [B00 §3.2](../bsp/B00_bsp_overview.html) |
| 세션 중 모르는 질문이 나오면 | [P01 §7 행동 규칙](P01_tech_session_master_plan.html) |
| 역질문 5개 | [P01 §7.4](P01_tech_session_master_plan.html) |
| BSP 경험 없다고 어떻게 말하나 | [B00 §3.1](../bsp/B00_bsp_overview.html) · [B04 §11](../bsp/B04_bsp_interview.html) |
| RTOS·Linux 갭 답변 | [P02 §5.2](P02_jd_change_2026-09-23.html) · [J11 §6](../jd/J11_rtos_handson.html) |

## 3. 주제별 입구

| 영역 | 개념 | 면접 |
|---|---|---|
| **BSP** (JD 제목) | [B00 개요](../bsp/B00_bsp_overview.html) · [B01 해부](../bsp/B01_bsp_anatomy.html) · [B02 Zephyr 실습](../bsp/B02_zephyr_board_port.html) · [B03 Linux/Android](../bsp/B03_linux_android_bsp.html) | [B04 41문항](../bsp/B04_bsp_interview.html) |
| **벤더·FPGA** (JD 신설) | [B05 벤더·FPGA 통합](../bsp/B05_vendor_fpga_integration.html) | [B05 §7](../bsp/B05_vendor_fpga_integration.html) |
| **임베디드 OS** (요건 확대) | [B06 OS 지형도](../bsp/B06_embedded_os_landscape.html) · [B03 Linux/AOSP](../bsp/B03_linux_android_bsp.html) | [B06 §14 17문항](../bsp/B06_embedded_os_landscape.html) |
| **디버깅** (최강점) | [C10 디버깅·bring-up](../concepts/C10_debugging_bringup_schematics.html) | [J07 업무](../jd/J07_debug_hw_sw_tools.html) · [J14 검증](../jd/J14_debug_tools_workflows.html) · [S06 시나리오](../study/S06_debug_scenarios_stories.html) |
| **회로도·bring-up** | [C03 드라이버](../concepts/C03_bsp_peripheral_drivers.html) | [J13](../jd/J13_schematics_bringup_collab.html) |
| **전력·발열** | [C05](../concepts/C05_low_power_thermal.html) | [J03](../jd/J03_power_thermal_always_on.html) · [S03](../study/S03_power_wireless_qa.html) |
| **스토리 (A·B·C)** | [S07 예시 3편](../study/S07_star_story_examples.html) | [S06 STAR·행동질문](../study/S06_debug_scenarios_stories.html) |
| **코딩 (드릴)** | — | [코딩 세션 인덱스](../coding.html) · [S01 드릴 14문제](../study/S01_c_coding_drills.html) |
| **코딩 (레퍼런스 뱅크)** | [L0 비트](../drills/L0_bit_basics.html) · [L1 메모리·포인터](../drills/L1_memory_pointers.html) · [L2 관용구](../drills/L2_embedded_idioms.html) · [L3 임베디드 C++](../drills/L3_embedded_cpp.html) | 문제 06~21 (`make run N=06`) |
| **Cortex-M·툴체인** | [C01](../concepts/C01_arm_cortex_boot_toolchain.html) · [C02](../concepts/C02_c_cpp_constrained.html) | [J10](../jd/J10_arm_cortex_toolchains.html) · [S02](../study/S02_drivers_rtos_qa.html) |

JD 개정으로 우선순위에서 내린 것: OTA([C07](../concepts/C07_ota_secure_boot.html)), 온디바이스 AI([C08](../concepts/C08_on_device_ml_inference.html)), 공장 테스트([C09](../concepts/C09_factory_test_calibration.html)), 무선 심화([C06](../concepts/C06_wireless_ble_wifi_thread.html)).

## 4. 내 경험 중 확인이 필요한 것

[J00 확인 필요 72건](../jd/J00_open_questions.html) — 우선 5건만 먼저 답을 정한다.

## 5. 회사·자리 맥락

- [Hark 컨텍스트 파일](../../../hark_ai_embedded_swe_context.html) — 회사 분석, 적합도, 보상, HM 추정, JD 신구 원문
- [전체 지원 현황판](../../../../dons_job_dashboard.html)

## 6. 오늘의 한 줄

연락은 이번 주 안에 온다. 그때 **"이번 주도 가능합니다"** 라고 답할 수 있으면 준비는 성공이다.
