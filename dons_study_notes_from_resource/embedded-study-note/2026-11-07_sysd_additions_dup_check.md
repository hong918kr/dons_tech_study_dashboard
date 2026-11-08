# SYSD 시스템 설계: 주제 추가와 중복 체크 (2026-11-07)

Don이 제안한 23개 주제를 기존 68트랙 2121개 항목과 비교했다. 그중 중복이 아닌 주제와 Claude가 추가로 제안한 8개 주제를 `curriculum/tracks/SYSD.json`에 넣었다. SYSD는 24개에서 52개 항목이 됐다. 전체 커리큘럼은 2149개 항목이다.

원칙은 하나다. SYSD 항목은 **설계 결정만** 다룬다. 다른 트랙에서 이미 구현하는 내용은 `see:` 태그와 task 본문에서 해당 항목 ID를 가리킨다.

판정 기호:

- 🔴 **완전 중복**: 새 항목을 만들지 않고 기존 항목을 쓰거나 넓혔다.
- 🟡 **부분 중복**: 구현은 다른 트랙에 있다. SYSD에는 설계 관점의 항목으로 추가했다.
- 🟢 **신규**: 기존 커리큘럼에 없던 주제다.

## 1. Don이 제안한 주제

### Basic

| 제안 | 판정 | 기존 항목 | 처리 |
|---|---|---|---|
| 설계 문서의 뼈대: 요구사항에서 위험까지 | 🟡 | SYSD-B01 면접 프레임워크, B02 요구사항, B04 ADR, B06 FMEA | **SYSD-B09** 추가. 이 네 항목을 한 장의 설계 문서 템플릿으로 묶는다 |
| 코어 선택 Cortex-M/R/A | 🟢 | (ARM 트랙은 Cortex-M 내부만 다룸) | **SYSD-B10** |
| 인터럽트 구조와 설계: NVIC vs GIC | 🟡 | BM-M01/M02 NVIC, SOCX-B10/M13 GICv3, ARCH-M09, VIRT-M05 | **SYSD-B11**: 비교와 시스템 IRQ 계획만 다룸 |
| 격리 설계: MPU / MMU / 하이퍼바이저 | 🟡 | SEC-M08, BM-A08, ARCH-M15, VIRT-A08, AERO-M01 | **SYSD-B12**: 세 방식의 결정 매트릭스 |
| 센싱 시스템 설계 | 🟡 | SENS 트랙(구현), SENS-A08 로드셀 end-to-end | **SYSD-B13**: B02의 진동 센서를 끝까지 설계 |
| 오디오 시스템 설계 | 🟡 | DSP-A03, MEDIA-B10, MEDIA-A03 | **SYSD-B14** |
| USB 장치 설계: 클래스·엔드포인트·전원 | 🟡 | COMM-M15, COMM-A01 | **SYSD-B15**: 복합 장치, 대역폭, USB-PD |

### Mid

| 제안 | 판정 | 기존 항목 | 처리 |
|---|---|---|---|
| 카메라 시스템 설계 | 🔴 | **SYSD-M05** 카메라 파이프라인, CAM-A05 | 새 항목 없음. **SYSD-M05를 넓힘**(3A, 다중 카메라, 대역폭 추가). MEDIA 트랙이 이 ID를 참조하므로 ID는 유지 |
| 드론 설계 | 🟡 | DRONE-A08 FC+컴패니언 SW 설계, SYSD-M04 텔레메트리 | **SYSD-M10**: 기체 사이징(추력·배터리·컴퓨트·링크 예산). SW 분할은 DRONE-A08 |
| 로봇 팔 설계: 관절 모듈과 실시간 버스 | 🟡 | RCTL-M09 FOC, RCTL-A07 CiA402, ROS-M09 (EtherCAT은 기존 0건) | **SYSD-M11** |
| 메모리 시스템 설계: SoC 메모리 계층 | 🟡 | ARCH 캐시 시리즈, MEM-B04/A03, SOCX-A07, AIPERF-M07 | **SYSD-M12** |
| NAND 저장장치 설계: SSD, UFS 컨트롤러 | 🟡 | MEM-M10~A09 FTL, CAP-M05, SIM-M02, PCIE-A05 | **SYSD-M13**: 컨트롤러 아키텍처 |
| Wi-Fi / BT 콤보칩 펌웨어 | 🟡 | RF-A02 공존, RF-M09, WCONN-B02/A04 | **SYSD-M14** |
| 제조(MFG) 펌웨어 | 🟡 | PLAT-A04, INFRA-A05, RF-A05, SIV-A08 | **SYSD-M15**: 칩 쪽 펌웨어 설계. 스테이션 SW는 INFRA |
| Light-Weight Test Firmware | 🟡 | SIV-B08/M12, EMU-A07, TEST-M11 | **SYSD-M16** |
| 이더넷 스위치: 데이터 평면 / 제어 평면 | 🟡 | NETHW-B10/M07/A09, NETHW-A12 | **SYSD-M17** |

### Advanced

| 제안 | 판정 | 기존 항목 | 처리 |
|---|---|---|---|
| 셀룰러 베이스밴드 펌웨어 | 🟢 | (RF-M08 AT, WCONN-M11 QMI는 호스트 쪽) | **SYSD-A07** |
| CXL 시스템 설계: 메모리 확장과 폴링 | 🟡 | **SOCX-A06 CXL 패브릭·풀링·티어링**, PCIE-A10 | **SYSD-A08**: OS 티어링, 장애 도메인, 용량 계획. "폴링"은 pooling(풀링)으로 해석 |
| 데이터센터 AI 가속기 비교 | 🟡 | AIPERF-A01, AIPERF-A09(엣지), DCN-A05, NPU-B09 | **SYSD-A09** |
| SRAM 중심 LLM 추론칩 | 🟢 | (NPU-M01은 타일링만) | **SYSD-A10** |
| LLM 서빙 시스템 설계 | 🔴 | **AISYS-A10** 설계 면접: LLM 서빙 (용량·비용·SLO) | 추가 안 함. AISYS-A10 사용 |
| 대규모 학습 시스템 설계 | 🔴 | **AISYS-A11** 학습 런 계획, DCN-A11 클러스터 패브릭 | 추가 안 함. AISYS-A11 사용 |
| 자동차 충돌 시뮬레이션 시스템 | 🟢 | (PHYS는 게임용 강체만) | **SYSD-A11**: FEM/HPC 해석 플랫폼으로 해석 |

## 2. Claude가 추가로 제안한 주제

| 항목 | 레벨 | 판정 | 이유와 기존 연결 |
|---|---|---|---|
| SYSD-B16 저전력 배터리 IoT 노드: 10년 전력 예산 | B | 🟡 | 면접 단골 주제. RF-A04, BM-M11은 부품 수준만 다룸 |
| SYSD-M18 시간 동기 아키텍처: 다중 센서·다중 컴퓨트 | M | 🟡 | DIST-M01/M02, SENS-M07/M08, CAM-M12를 시스템으로 묶음 |
| SYSD-M19 웨어러블: 센서 허브·Always-on·BLE | M | 🟢 | 기존 0건 |
| SYSD-M20 디버그·트레이스·필드 진단 인프라 | M | 🟡 | TOOL-A04/A05, BM-A09, INFRA-M09를 설계 관점에서 묶음 |
| SYSD-A12 차량 Zonal E/E 아키텍처 | A | 🟢 | Rivian 등 차량 직무. AUTO-M08/M09와 연결 |
| SYSD-A13 FMCW 레이더 시스템 | A | 🟢 | Don의 RF 배경과 연결. 기존 0건 |
| SYSD-A14 EV 트랙션 인버터 제어기 | A | 🟢 | 기능안전과 모터 제어의 종합. 기존 0건 |
| SYSD-A15 엣지 AI SoC 아키텍처 정의: 요구 → IP·PPA | A | 🟡 | ARCH-A12, SOCX-A10, NPU-B09는 일부만 다룸. 제품 요구에서 스펙을 도출하는 과정이 빠져 있었음 |

후보였지만 넣지 않은 주제가 있다. 이미 다른 항목이 다루기 때문이다.

- 전원·클럭·리셋 도메인: ARCH-A09
- 보안 부트: SYSD-M01, SEC
- BMC 플릿: BMC-A08, SYSD-M06
- 칩렛 인터커넥트: SOCX-A10
- DPU: NETHW-A12
- 위성 store-and-forward: SYSD-A06

## 3. 구조 변경

- 모의 면접 항목 `SYSD-A07`은 맨 뒤로 옮겨 **`SYSD-A16`**이 됐다. 외부 참조가 없는 것을 확인했다. 연습 범위에 신규 주제 3개를 추가했다.
- 트랙 intro와 goals에 아키텍처 결정과 도메인별 설계 목표를 추가했다.
- `see:` 태그와 본문의 항목 ID는 모두 실제로 존재하는지 확인했다.
- `review_items.py` 결과: problems 0. `--dups`에서 SYSD 신규 항목과 유사도가 높은 쌍은 없었다. 기존 쌍 SYSD-A02 ↔ TEST-M05 하나만 나왔다.
