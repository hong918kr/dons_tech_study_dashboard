# 🧭 START HERE — 어디서부터 볼까

> Neros **Firmware Test Engineer** HM 인터뷰 (Teams 45분, 2026-10-01 목) 준비 자료 22개의 읽는 순서. **남은 시간에 맞는 코스 하나만** 골라서 위에서부터 내려간다. 다 볼 필요 없다.

## ⏱️ 인터뷰까지 30분 남았다면 — 당일 직전 코스

| 순서 | 시간 | 볼 것 | 왜 |
|---|---|---|---|
| 1 | 5분 | [N07 스토리와 영어](notes/2026-09-28_N07_stories_and_english.md) — 1절 자기소개 60초, 5절 "Why test role?" | 첫 5분이 첫인상이다. **소리 내어 한 번** |
| 2 | 10분 | [문제 12 Python 기초](problems/12_python_basics.md) — 6 → 7 → 8 → 9번만 손으로 | 손 풀기. bytes · hex · 비트 |
| 3 | 10분 | [N08 시나리오](notes/2026-09-29_N08_test_and_debug_scenarios.md) — 0절 "답의 뼈대" 두 개만 | 어떤 테스트·디버깅 질문이 와도 이 틀로 말한다 |
| 4 | 5분 | [N07](notes/2026-09-28_N07_stories_and_english.md) 6절 역질문 3개 골라 메모 | 마지막 5분 |

## ⏱️ 1~2시간 남았다면 — 핵심 코스

| 순서 | 시간 | 볼 것 | 왜 |
|---|---|---|---|
| 1 | 10분 | [P00 게임 플랜](plan/2026-09-28_P00_game_plan.md) — "이 인터뷰가 무엇인가", "핵심 메시지" | 전체 그림과 한 문장 메시지 |
| 2 | 15분 | [N07 스토리와 영어](notes/2026-09-28_N07_stories_and_english.md) — 자기소개, 스토리 A·B·C, 확실히 나오는 질문 | 45분 중 20분 이상이 말이다 |
| 3 | 20분 | [N09 스토리라인 2탄](notes/2026-09-29_N09_resume_storylines.md) — 0절 줄거리, 질문→스토리 매핑표, **S12 · S2 · S1** | 경험 질문에 어떤 이야기로 답할지 |
| 4 | 20분 | [문제 12 Python 기초](problems/12_python_basics.md) → [문제 01 로그 파서](problems/01_log_parser.md) | 라이브 코딩 대비 손풀기 |
| 5 | 15분 | [N08 시나리오](notes/2026-09-29_N08_test_and_debug_scenarios.md) — 0절 뼈대, T1 UART, T2 I2C, D1 I2C stuck, D6 flaky | "어떻게 테스트/디버그하나" 질문 |
| 6 | 10분 | [N02 HIL](notes/2026-09-28_N02_hil_for_drones.md) — 리그 구성도와 "HIL 리그를 설계해 봐" 모범 답안 | 테스트 HM이 가장 묻고 싶은 질문 [추정] |

## ⏱️ 반나절 이상 — 전체 코스

1. **큰 그림**: [P00 게임 플랜](plan/2026-09-28_P00_game_plan.md) → [N01 JD 한 줄씩](notes/2026-09-28_N01_jd_line_by_line.md)
2. **말하기**: [N07 스토리와 영어](notes/2026-09-28_N07_stories_and_english.md) → [N09 레쥬메 스토리라인](notes/2026-09-29_N09_resume_storylines.md) → [N08 테스트·디버깅 시나리오](notes/2026-09-29_N08_test_and_debug_scenarios.md)
3. **Python 개념**: [N03 Python for test automation](notes/2026-09-28_N03_python_for_test_automation.md) — "30초 영어 답변" 8개 먼저
4. **Python 코딩**: [12 기초](problems/12_python_basics.md) → [01 로그 파서](problems/01_log_parser.md) → [04 시리얼 드라이버](problems/04_serial_dut_driver.md) → [08 retry](problems/08_retry_and_wait.md) → [02 CRSF](problems/02_crsf_frame_parser.md) → [05 레지스터](problems/05_register_bitfields.md)
5. **테스트 실무**: [N02 HIL](notes/2026-09-28_N02_hil_for_drones.md) → [N04 pytest](notes/2026-09-28_N04_pytest_framework.md) → [N05 CI/CD와 Git](notes/2026-09-28_N05_ci_cd_git.md)
6. **도메인**: [N06 프로토콜과 드론 스택](notes/2026-09-28_N06_protocols_and_drone_stack.md)
7. **여유가 있으면**: [11 HIL 테스트 설계](problems/11_hil_test_design.md) · [10 워밍업](problems/10_python_warmups.md) · [07 CI 결과 분석](problems/07_ci_results_analyzer.md) · [06 rate 체크](problems/06_telemetry_rate_check.md) · [03 MSP](problems/03_msp_codec.md) · [09 DShot](problems/09_dshot_codec.md)

## 🗂️ 자료 지도 — 무엇이 어디에

| 묻는 것 | 자료 |
|---|---|
| 자기소개, Why test, Why Neros, 역질문 | [N07](notes/2026-09-28_N07_stories_and_english.md) |
| "Tell me about a test system you built" | [N09 S12](notes/2026-09-29_N09_resume_storylines.md) · [N07 스토리 A](notes/2026-09-28_N07_stories_and_english.md) |
| "Hardest bug" · 루트코즈 | [N09 S1](notes/2026-09-29_N09_resume_storylines.md) · [N07 스토리 C](notes/2026-09-28_N07_stories_and_english.md) |
| "UART/I2C를 어떻게 테스트하나" | [N08 Part A](notes/2026-09-29_N08_test_and_debug_scenarios.md) · [N06](notes/2026-09-28_N06_protocols_and_drone_stack.md) |
| "X가 실패한다, 어떻게 디버그하나" | [N08 Part B](notes/2026-09-29_N08_test_and_debug_scenarios.md) |
| HIL 리그 설계 | [N02](notes/2026-09-28_N02_hil_for_drones.md) |
| Python 개념 (decorator, generator, GIL…) | [N03](notes/2026-09-28_N03_python_for_test_automation.md) |
| Python 라이브 코딩 | [코딩 세션 전체](coding.html) |
| pytest, fixture, 테스트 프레임워크 | [N04](notes/2026-09-28_N04_pytest_framework.md) |
| GitLab CI, Jenkins, flaky, git bisect | [N05](notes/2026-09-28_N05_ci_cd_git.md) |
| JD 요구사항별 내 경험 | [N01](notes/2026-09-28_N01_jd_line_by_line.md) |
| 회사·연봉·지원 경과 | [Neros 컨텍스트 파일](../../neros_tech_senior_firmware_engineer_context.md) §8 |

## 💻 코딩 채점 명령

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/neros_tech_senior_firmware_engineer/firmwareTestEngineerPrep
python3 python/run.py 12          # 내 풀이 채점 (python/starters/12_python_basics.py)
python3 python/run.py 12 --sol    # 모범답안
```

## 📌 인터뷰 직전 기억할 다섯 줄

- 핵심 메시지: **"I'm a firmware engineer who builds test infrastructure around real hardware — I can read the code under test, not just the test results."**
- 테스트 질문 → 정상 · 경계 · 고장 주입 · 스트레스 · CI 배치 · 합격 기준
- 디버깅 질문 → 재현 · FW/테스트/리그 가르기 · 캡처 · 변수 하나씩 · 수정 · **회귀 테스트 추가**
- 코딩 → 입력·출력과 엣지 케이스를 먼저 말하고, 마지막에 테스트 하나 추가
- CI 갭 → 정직하게 인정하고 챔버 플랫폼의 스케줄링 경험으로 연결
