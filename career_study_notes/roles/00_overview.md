# Target Roles — Overview & 배경 매핑

> 내 배경([`../context.md`](../context.md))을 실제 직무로 매핑한 개요.
> 회사별 상세: [nvidia](./nvidia.md) · [google](./google.md) · [openai](./openai.md) · [anthropic](./anthropic.md)
> _리서치 기준일: 2026-07-12 (채용공고는 수시 변동 — 지원 전 원문 재확인)_

---

## A. 내 배경에 맞는 직무 "패밀리" (회사 무관)

내 강점 = **디지털 인터페이스 브링업 + 펌웨어 + 실리콘/시스템 통합**.
이걸 축으로 fit이 높은 순서:

| Fit | 직무 패밀리 | 왜 나에게 맞나 | 보완 갭 |
|-----|------------|---------------|---------|
| ⭐⭐⭐ | **Post-Silicon / HSIO Validation & Bring-up** | PCIe·SerDes·I2C·SPI 브링업/랩 디버그가 내 현업 그 자체 | 신호무결성(SI) 심화, 랩장비(BERT/DSO) |
| ⭐⭐⭐ | **Firmware / Embedded FW (accelerator·platform)** | SSD FW 7년 + 현 임베디드 | 가속기 도메인 지식, RTOS/드라이버 |
| ⭐⭐⭐ | **Silicon System Integration / SoC Integration** | 실리콘-보드-시스템 통합 경험 직결 | SoC 아키텍처, 버스/NoC, RTL 리터러시 |
| ⭐⭐ | **Hardware/Software Co-Design** | FW+실리콘 양쪽 접점, 브리지 역할 | ML 워크로드/inference 내부, 성능모델링 |
| ⭐⭐ | **Reliability / DFX** | 데이터센터급 신뢰성 감각 | DFT/DFX 방법론 |
| ⭐⭐ | **Board / Platform HW Design & Bring-up** | 보드레벨 통합·라인 설계 | 스키매틱/PCB, 파워/클럭 |
| ⭐⭐ | **Robotics Firmware / Embedded (Physical AI)** | 임베디드 FW + 방향성 부합 | 실시간 제어, ROS, 센서융합 |
| ⭐ | **Edge / On-device Inference Enablement (NPU)** | on-device 방향 부합 | ML/양자화/컴파일러(LiteRT), Python |
| ⭐ | **Accelerator Platform / Compute Infra SW** | 데이터센터 시스템 감각 | 강한 C/C++·시스템 SW |

> **전략 요약**: 지금 당장은 ⭐⭐⭐ 3개(validation/bring-up · firmware · silicon integration)로
> 리버레지가 최대. ML/systems-SW를 쌓으면서 ⭐⭐ → co-design/robotics/on-device로 확장.

---

## B. 회사 × 직무 매트릭스 (내 fit 관점 대표 직무)

| 방향 | NVIDIA | Google | OpenAI | Anthropic |
|------|--------|--------|--------|-----------|
| **Post-Si / Validation / Bring-up** | ⭐⭐⭐ PCIe/HSIO Post-Si Validation, Firmware SerDes Verif | ⭐⭐ TPU/Tensor Si validation·integration | ⭐⭐ Design Verification, Reliability/DFX | ⭐⭐⭐ Accelerator **Platform bring-up** (TPU/Trainium/GPU) |
| **Firmware / Embedded** | ⭐⭐⭐ Firmware Eng (silicon bring-up), Jetson/robotics FW | ⭐⭐ Tensor/Platform FW | ⭐⭐⭐ **Firmware Engineer, Robotics** | ⭐ (주로 systems SW) |
| **Silicon / SoC Integration** | ⭐⭐ Silicon Solutions Eng | ⭐⭐⭐ **Silicon System Integration/Arch, TPU** | ⭐⭐ HW/SW Co-Design | — |
| **HW/SW Co-Design** | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ **HW/SW Co-Design**, Research-HW Codesign | ⭐⭐ Accel Build Infra (HW-SW interface) |
| **Robotics / Physical AI** | ⭐⭐⭐ Isaac/Jetson robotics | ⭐ | ⭐⭐⭐ Robotics (infra buildout) | — |
| **On-device / Edge NPU** | ⭐⭐ Jetson edge | ⭐⭐⭐ Tensor/LiteRT NPU enablement | ⭐ | — |
| **Compute / Platform Infra SW** | ⭐ | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ Accel Platform, Compute Capacity, Inference Deploy |

(⭐ 개수 = 지금 내 배경 기준 접근성/적합도 주관 평가)

---

## C. 회사별 "결" 요약

- **NVIDIA** — 순수 실리콘/HW 조직이 가장 크고 성숙. 내 인터페이스/브링업/FW 강점이 **가장 직접적으로** 매칭. 로보틱스(Isaac/Jetson)·edge까지 방향 확장도 한 지붕 아래.
- **Google** — TPU 실리콘(설계~통합~검증) + Tensor 소비자 SoC(on-device AI). **Silicon System Integration**이 내 통합 경험과 정확히 겹침. 단, 설계 직무는 RTL 요구.
- **OpenAI** — 커스텀 AI 실리콘(코드명 *Jalapeño*) + **로보틱스 재가동**(데이터센터/전력망 buildout용 로봇). HW/SW co-design과 로보틱스 **펌웨어**가 내 프로필과 강하게 공명. 스타트업 결 → 폭넓은 role.
- **Anthropic** — 자체 칩 설계보다 **가속기 플랫폼 브링업/정규화(TPU·Trainium·GPU) + 인프라 시스템 SW** 중심. 브링업 감각은 통하지만 **C/C++ 시스템 SW** 비중 높음 → SW 보강 필요.

---

## D. 다음 액션 (이 저장소에서)

- [ ] 회사별 파일에서 **관심 직무 5개** 골라 ⭐ 표시 → shortlist
- [ ] shortlist 직무의 **요구역량 → 내 갭**을 [`../study/00_study_plan.md`](../study/00_study_plan.md)로 이관
- [ ] 갭별로 `study/` 하위에 주제 노트 + 연습문제 채우기
- [ ] [`../interview/00_interview_prep.md`](../interview/00_interview_prep.md)에 라운드별 준비 시작
