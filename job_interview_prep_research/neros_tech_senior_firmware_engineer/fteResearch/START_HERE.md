# 🧭 START HERE — 드론 FTE 리서치 노트

> 2026-10-24 시작. Gemini 대화 PDF(22쪽)를 출발점으로, 오픈소스 소스 · 공식 문서로 확인해서 11단원으로 다시 쓴 연구 노트. 인터뷰 준비가 아니라 **드론 Firmware Test Engineering을 제대로 이해**하기 위한 것.

## 1. 먼저 볼 것

1. [R00 개요 · 읽는 순서 · Gemini 정정 목록](chapters/2026-10-24_R00_overview_and_gemini_corrections.md) — 4절 "한 장 요약"부터
2. [R01 FTE란? SSD 검증과 무엇이 다른가](chapters/2026-10-24_R01_fte_vs_ssd_validation.md)
3. [R09 스케일 · 재사용 테스트 플랫폼](chapters/2026-10-24_R09_scalable_reusable_test_platform.md) — 가장 묻고 싶었던 질문

## 2. 전체 단원

| # | 단원 | 한 줄 |
|---|---|---|
| R00 | [개요 · Gemini 정정](chapters/2026-10-24_R00_overview_and_gemini_corrections.md) | 지도 · 코스 · 정정 표 |
| R01 | [FTE vs SSD 검증](chapters/2026-10-24_R01_fte_vs_ssd_validation.md) | 오라클 · 실시간 · RF · 릴리스 속도가 새로운 것 |
| R02 | [드론 시스템 · 오픈소스 배치도](chapters/2026-10-24_R02_drone_system_and_open_source_map.md) | 보드 · 버스 · 라이선스 · 신호 경로 시간 예산 |
| R03 | [Betaflight / INAV](chapters/2026-10-24_R03_betaflight_inav_internals_and_testing.md) | scheduler · PID 경로 · CRSF · failsafe · MSP · unit test · SITL |
| R04 | [PX4 / ArduPilot](chapters/2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) | MAVSDK 테스트 · autotest · SIH · ULog · Replay |
| R05 | [RC 링크 · 프로토콜](chapters/2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) | ELRS · EdgeTX · CRSF · SBUS · DShot · MSP · MAVLink 테스트 |
| R06 | [Companion · 영상 · Linux · RTOS](chapters/2026-10-24_R06_companion_video_linux_rtos.md) | uXRCE-DDS · glass-to-glass · OTA · PREEMPT_RT · twister |
| R07 | [테스트 방법론](chapters/2026-10-24_R07_test_methodology_pyramid.md) | 10층 피라미드 · 기법 카탈로그 · 통계 판정 · 템플릿 |
| R08 | [HIL 벤치 설계](chapters/2026-10-24_R08_hil_bench_design_instruments.md) | 계측기 · 배선 · 안전 · 무인 복구 · 랙 확장 |
| R09 | [스케일 · 재사용 플랫폼](chapters/2026-10-24_R09_scalable_reusable_test_platform.md) | capability 모델 · CI 게이트 · 데이터 · 벤치 운영 |
| R10 | [로드맵 · 랩 · 참고](chapters/2026-10-24_R10_roadmap_labs_references.md) | 8주 계획 · 랩 13개 · 용어집 |

## 3. 예제 코드

```text
cd fteResearch/code
for f in *.py; do python3 -I "$f" | tail -1; done   # 7개 전부 PASS
```

## 4. 고치는 법

md를 고치고 `python3 build_site.py` → `site/` 다시 생성. 작성 규칙은 `NOTES_SPEC.md`.
