# fteResearch — 작성 규칙 (리서치 노트 · 인터뷰 준비 아님)

목적: Don이 **Firmware Test Engineering (FTE)을 드론/방산 맥락에서 제대로 이해**하기 위한 연구 노트. 데이터센터 SSD FW 검증 경험과 무엇이 같고 다른지, Neros가 쓰는 오픈소스가 어떻게 쓰이고 어떻게 테스트하는지, 스케일러블하고 재사용 가능한 테스트 플랫폼은 어떻게 짓는지.

출발 자료: `/Users/donh/Downloads/Neros 오픈소스 및 테스트 분석_prompt_gemini.pdf` (Gemini 대화 22쪽). **Gemini 내용은 그대로 믿지 말고 확인한다.** 틀린 건 본문에서 바로잡고 `> ⚠️ Gemini 정정:` 인용으로 표시.
공고 근거: `../research/job_board_snapshots/2026-10-23_neros_jobs.json` (Neros Greenhouse 109개), `../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md` (JD 스캔 표 · latency 파이프라인 · Betaflight 읽을 곳).

## 폴더
- `chapters/2026-10-24_RNN_topic.md` — 단원 (R00 개요 ~ R10 로드맵)
- `code/` — 단원에서 쓰는 실행 가능한 예제 (Python 3.9+, 표준 라이브러리만, 파일 끝에 자체 테스트)
- `build_site.py` → `site/` (직접 수정 금지, md 고치고 다시 빌드)

## 단원 목록 (파일 이름 고정 — 서로 링크할 때 이 이름 사용)
| # | 파일 | 주제 |
|---|---|---|
| R00 | `2026-10-24_R00_overview_and_gemini_corrections.md` | 개요 · 읽는 순서 · Gemini 정정 목록 |
| R01 | `2026-10-24_R01_fte_vs_ssd_validation.md` | FTE란 무엇인가 — 데이터센터 SSD 검증과 무엇이 같고 다른가 |
| R02 | `2026-10-24_R02_drone_system_and_open_source_map.md` | 드론 시스템 구조 · 보드 · 버스 · 오픈소스 배치도 · 신호 경로 |
| R03 | `2026-10-24_R03_betaflight_inav_internals_and_testing.md` | Betaflight / INAV 내부 구조와 테스트 포인트 |
| R04 | `2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md` | PX4 / ArduPilot 구조와 그들의 자체 테스트 생태계 |
| R05 | `2026-10-24_R05_rc_link_protocols_elrs_edgetx.md` | ELRS · EdgeTX · CRSF · SBUS · DShot · MSP · MAVLink 와 테스트 |
| R06 | `2026-10-24_R06_companion_video_linux_rtos.md` | Companion(Jetson/ROS 2) · 영상 · 임베디드 Linux · RTOS 와 테스트 |
| R07 | `2026-10-24_R07_test_methodology_pyramid.md` | 테스트 방법론 — 피라미드(unit→SITL→HITL→HIL→비행→공장)와 기법 카탈로그 |
| R08 | `2026-10-24_R08_hil_bench_design_instruments.md` | HIL 벤치 설계 — 계측기 · 배선 · 안전 · Python 드라이버 |
| R09 | `2026-10-24_R09_scalable_reusable_test_platform.md` | 스케일러블 · 재사용 가능한 테스트 플랫폼 아키텍처 |
| R10 | `2026-10-24_R10_roadmap_labs_references.md` | Don의 학습 로드맵 · 실습 랩 · 참고 자료 |

## 마크다운 (렌더러가 지원하는 부분집합만)
- 첫 줄 `# <이모지> RNN 제목`. 바로 다음 줄에 `> 한두 줄 요약` (카드 요약으로 쓰임)
- `##` / `###` 제목, 문단, `-` / `1.` 목록 (**중첩 목록 금지** — 들여쓴 하위 목록 쓰지 말 것), `- [ ]` 체크리스트
- 표는 한 행 = 한 줄. 셀 안 줄바꿈 금지. 셀 안의 `|` 는 `\|`
- 인용 `>`, 코드펜스 (```` ```text ````, ```` ```python ````, ```` ```c ````), `---`, 인라인 `code` / **굵게** / [링크](상대경로.md)
- 다이어그램은 ```` ```text ```` 안에 ASCII 박스로
- 단원끼리 링크: `[R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md)` 처럼 같은 폴더 상대경로

## 내용 규칙
- 한국어 기본, 기술 용어는 영어 그대로. 문장은 짧고 쉽게. 한 문단 = 한 생각
- 각 단원 구조: 요약 → 왜 중요한가(FTE 관점) → 본문(개념 → 구조 그림 → 표 → 예시) → **"SSD 검증에서는 이랬다 → 드론에서는 이렇다"** 비교 칸 → 테스트 아이디어/체크리스트 → 직접 해 볼 것 → 확인 안 된 것 목록 → 참고 링크
- **사실 확인**: 오픈소스 저장소 경로, 함수 이름, 명령어, 숫자(baud, 주기, 프레임 길이)는 가능하면 GitHub/공식 문서를 WebFetch 해서 확인. 확인한 건 끝에 출처 링크. 확인 못 한 건 `[확인 필요]`
- **Neros 내부**는 공고 문구 외엔 전부 `[추정]`. Gemini가 말한 제품 이름(Archer, Archer AI, Crossbow GCS, APEX 등)은 `[Gemini 주장 · 미확인]`으로 표시하고 단정하지 않는다
- **Don의 경험은 아래 범위에서만** 인용. 숫자 · 세부를 지어내지 않는다. 회사 내부 제품명 · 툴 이름 · 고객 이름 쓰지 않는다
  - 최근 (2025-12~): RF 하드웨어 칩셋 통합, PCIe/I2C/SPMI/RFFE 루트코즈, factory test-node 아키텍처, stress 기반 latent defect 발굴, DSO · protocol analyzer
  - 이전 (2018-10~2025-11, 데이터센터 SSD): 챔버 테스트 플랫폼 SDK(온도 제어 · 스케줄링 · eSSD 상태 모니터링 · UART 테스트 시퀀스 자동화, 다른 엔지니어가 사용), web 기반 test automation, chip reliability system (NAND, PCIe 3/4/5 커버리지), NVMe telemetry 디버그 기능, shmoo · health monitoring debug FW, Cortex-R/M · Xtensa FW, error reporting/handling scheme
  - 경험 **없는 것**: GitLab CI/Jenkins, pytest 실무, Betaflight/ELRS, Bazel, 드론 비행
- 하드웨어 구매가 필요한 실습은 "사기 전에 Don과 예산 확인"이라고 쓴다. 프롭 장착 모터 회전 · LiPo 충전 같은 위험 작업엔 안전 문구
- 길이: **최대한 자세하게**. 단원당 대략 500~1000줄 md. 표와 그림을 아끼지 않는다
