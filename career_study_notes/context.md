# Context — Don (내 소개 / 커리어 컨텍스트)

> 이 파일은 커리어 스터디 전체의 **기준점(context)** 입니다.
> 인터뷰 준비, 직무 리서치, 공부 계획을 세울 때 항상 이 배경에 맞춰 진행합니다.
> _최종 업데이트: 2026-07-12_

---

## 1. 한 줄 요약

> **디지털 인터페이스 · 실리콘 브링업 · 펌웨어에 강한 임베디드 시스템 엔지니어.**
> RF 칩 integration과 SSD 펌웨어(데이터센터향) 경력을 기반으로,
> **차세대 AI 가속기 · physical AI · on-device LLM · 로보틱스용 칩셋/시스템** 개발로 방향 전환 중.

---

## 2. 현재 (Current Role)

- **직무**: Embedded System Engineer — **RF chip integration**
- **핵심 업무**
  - RF 칩을 시스템에 integration, factory production line 설계
  - seamless integration을 위한 브링업 · 검증 · 디버그
- **강점 기술 (digital interfaces)**
  - `PCIe` · `I2C` · `SPMI` · `RFFE` · `SPI`
  - 인터페이스 브링업, 프로토콜 레벨 디버그, 보드/시스템 레벨 통합
- **환경**: 실리콘 ↔ 보드 ↔ 펌웨어 ↔ 프로덕션 라인을 잇는 시스템 통합 관점

## 3. 과거 (Previous Role — 7년)

- **직무**: SSD Firmware Engineer
- **도메인**: **데이터센터향** 칩 & 시스템 개발
- **의미**: 대규모 시스템 신뢰성/성능, 펌웨어-실리콘 co-design, 데이터센터 인프라 이해

## 4. 학력

- **UC Berkeley — Mathematics (수학) 전공**
- 의미: ML/최적화/선형대수 등 **수학적 기반**이 탄탄 → AI 도메인 전환에 유리한 자산

---

## 5. 가고 싶은 방향 (Target Direction)

다음 세대를 위한 **칩셋 / 시스템 개발**. 관심 축:

1. **AI Accelerator** — TPU/GPU/커스텀 가속기 실리콘·시스템
2. **Physical AI** — 로보틱스 · 자율 시스템용 컴퓨트
3. **On-device / Local LLM running** — edge NPU, on-device inference 실리콘/enablement
4. **Robotics** — 임베디드 펌웨어 · 컨트롤 · 하드웨어 통합
5. **차세대 칩셋/시스템 아키텍처** 전반

## 6. 타깃 회사 (우선순위 4곳)

| # | 회사 | 왜 (내 배경과의 접점) |
|---|------|----------------------|
| 1 | **NVIDIA** | GPU/NVLink/PCIe HSIO 브링업·검증, Jetson/로보틱스 → 인터페이스+펌웨어 강점 직결 |
| 2 | **Google** | TPU 실리콘 통합/브링업, Tensor SoC on-device AI → integration 경험 직결 |
| 3 | **OpenAI** | 커스텀 AI 실리콘(Jalapeño), 로보틱스 펌웨어 부활 → HW/SW co-design + FW 직결 |
| 4 | **Anthropic** | Accelerator Platform 브링업(TPU/Trainium/GPU), 인프라 시스템 SW |

→ 회사별 상세 직무는 [`roles/`](./roles/) 참고.

---

## 7. 나의 자산 vs 보완할 갭 (요약)

**강점 (즉시 통하는 것)**
- 디지털 인터페이스 브링업/검증 (PCIe·SerDes·I2C·SPI·RFFE·SPMI)
- 펌웨어 (SSD/데이터센터급 신뢰성·성능)
- 실리콘-보드-시스템 integration, 프로덕션 라인
- 수학 기반 (ML fundamentals 흡수 유리)

**보완할 갭 (직무별로 다름 — [`roles/00_overview.md`](./roles/00_overview.md)에서 상세)**
- 🔧 **RTL/설계 직무**: SystemVerilog/Verilog, 아키텍처, DV
- 🤖 **ML/co-design 직무**: PyTorch, transformer/inference 내부, 양자화, NPU 컴파일러(LiteRT 등)
- 🖥 **인프라 SW 직무 (Anthropic류)**: 강한 C/C++·시스템 프로그래밍, 빌드/런타임
- 🦾 **로보틱스**: 실시간 제어, ROS, 센서/액추에이터 통합

---

## 8. 이 스터디 저장소를 쓰는 법

- 이 `context.md` = 항상 첫 기준점
- [`roles/`](./roles/) = 회사·직무별 타깃 + 요구역량 + 내 갭
- [`study/`](./study/) = 직무에서 요구하는 지식을 주제별로 공부 (연습문제·정리)
- [`interview/`](./interview/) = 라운드별 인터뷰 준비 (behavioral/system/coding/domain)
- [`dashboard.html`](./dashboard.html) = 위 내용을 웹앱처럼 보는 네비게이션 허브
- 자세한 프레임워크 설명은 [`README.md`](./README.md)
