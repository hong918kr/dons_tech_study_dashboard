# P06. 7일 계획 — 실습까지 넣는 버전 (2026-09-30 시작)

> **상황**: 지원 완료(9/21) · 리크루터 "이번 주 안에 회신" 확약(9/29) · 팀 OOO 복귀 대기 → **면접은 다음 주 초 가능성**
> **전제**: 하루 **2~2.5시간**. 주말 하루는 3~4시간 확보
> **기준 문서**: [JD 스냅샷](../../JD_SNAPSHOT_2026-09-29.md) — 모든 인용은 여기서만
> **4일밖에 없으면** [P04 압축판](P04_4day_sprint.html)으로, **하루면** [P04 §4 60분판](P04_4day_sprint.html)으로 내려간다

---

## 0. 7일로 늘리면 무엇이 달라지나

4일 계획과의 차이는 **실습 하나와 갭 방어 하나**다.

| 추가된 것 | 왜 |
|---|---|
| **Zephyr 보드 실습 반나절** (D5) | "RTOS 해봤나"에 "지난주부터 하고 있다"로 답할 수 있게 된다. 레쥬메에 쓸 근거도 생긴다 |
| **B06 OS 갭 방어** (D6) | 9/23 요건이 eLinux·AOSP·VxWorks로 넓어졌다. 4일 계획에서는 스크립트만 외웠지만 7일이면 이해하고 말할 수 있다 |
| **B05 벤더·FPGA** (D3) | JD 인트로에 새로 들어온 축. 스토리 재료가 여기서 나온다 |
| **J00 확인 필요 답 정하기** (D1) | 노트 6곳의 과잉 주장이 이 답에 걸려 있다. 먼저 정리해야 나머지가 정확해진다 |

---

## 1. 7일 일정표

### D1 — 오늘 (9/30 수) · 기준 맞추기
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 40분 | [P02 JD 개정](P02_jd_change_2026-09-23.html) + [P05 감사 §7](P05_note_audit.html) — **말해도 되는 JD 인용 6문장, 하면 안 되는 인용 7개** | 인용 카드 1장 |
| 60분 | [J00 확인 필요](../jd/J00_open_questions.html) **우선 3건만** 답 정하기 — ① SSD 스케줄러 성격([B06 §3 판별표](../bsp/B06_embedded_os_landscape.html)) ② Apple test-node가 DUT 펌웨어였나 ③ SSD 이미지 서명·검증 관여 여부 | 세 줄 메모 |
| 30분 | [B00 §3 좌표표](../bsp/B00_bsp_overview.html) — 내 강점·갭을 한 문장으로 | "BSP의 가장 어려운 절반" 문장 암기 |

> D1이 가장 중요하다. **기준이 틀리면 나머지 6일이 틀린 방향으로 쌓인다.**

### D2 — 10/1 목 · 최강점 스토리 + 코딩
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 40분 | [J07 디버깅 업무](../jd/J07_debug_hw_sw_tools.html) §2~3 · [S06](../study/S06_debug_scenarios_stories.html) 시나리오 틀 | 디버깅 5단계 암기 |
| 90분 | **스토리 A 완성** — [S07 §2 예시](../study/S07_star_story_examples.html)를 먼저 읽고, 같은 6단 뼈대에 내 사건을 넣는다 | 4분, 소리 내어 3회 |
| 30분 | [코딩 01 링버퍼](../problems/01_spsc_ring_isr.html) 손으로 → `make run N=01` | 통과 |

### D3 — 10/2 금 · 벤더·FPGA (JD 신설 축)
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 50분 | [B05 §3 벤더 협업](../bsp/B05_vendor_fpga_integration.html) + §4 하드웨어 스펙 검증 | 벤더 이슈 3분류 이해 |
| 40분 | [B05 §5](../bsp/B05_vendor_fpga_integration.html) FPGA 두 의미 + **§5.4 되묻기 스크립트** | 되묻기 문장 암기 |
| 60분 | **스토리 C 완성** — [S07 §4 예시](../study/S07_star_story_examples.html) 참고. 되묻기 문장으로 시작 | 3분 |

### D4 — 10/3 토 · BSP 본체 + 코딩
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 60분 | [B01 BSP 해부](../bsp/B01_bsp_anatomy.html) §0~§4 — 부트 체인, 전원·클럭, 핀 먹싱, 메모리 맵 | **BSP 계층도·bring-up 순서 백지에서** |
| 60분 | [B01 §5~§8](../bsp/B01_bsp_anatomy.html) — 초기화 의존성, 보드 리비전, 첫 전원 인가 | 리비전 3방식 설명 가능 |
| 40분 | [코딩 03 레지스터](../problems/03_reg_bitfield.html) → `make run N=03` | 15분 안에 통과 |

### D5 — 10/4 일 · **Zephyr 실습** (반나절, 이번 주의 승부처)
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 60분 | [B02 §1~§3](../bsp/B02_zephyr_board_port.html) 읽으며 설치 — `west init` → `west build -b native_sim` | 첫 빌드 성공 |
| 90분 | blinky → **devicetree overlay로 핀 바꾸기** → 센서 노드 추가 시도 | 오버레이 수정 경험 |
| 60분 | 안 되는 것 하나 일부러 만들고 디버깅 (`build/zephyr/zephyr.dts` 확인) | **실패담 1개** |

> 보드가 없으면 `native_sim`으로 전부 가능하다. 목표는 blinky가 아니라 **devicetree를 직접 고쳐본 경험**이다. 그래야 "board file을 만져봤다"가 사실이 된다. 실패담은 면접에서 가장 잘 먹히는 재료다.

### D6 — 10/5 월 · 갭 방어
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 50분 | [B06 §9 자기배치](../bsp/B06_embedded_os_landscape.html) + §10 번역표 + **§11 정직 스크립트** | OS 갭 스크립트 암기 |
| 40분 | [B03 §8.2 A 목록](../bsp/B03_linux_android_bsp.html) — 부트 체인, DT, probe 경로, HAL/AIDL | Linux 질문 방어선 |
| 60분 | [B04 면접 41문항](../bsp/B04_bsp_interview.html) Q01~Q20 — 답 보기 전에 먼저 말하기 | 막힌 문항 표시 |

### D7 — 10/6 화 · 리허설
| 블록 | 할 일 | 산출물 |
|---|---|---|
| 40분 | [B04](../bsp/B04_bsp_interview.html) Q21~Q41 훑기 + [P04 §5 금지 표현](P04_4day_sprint.html) | 하지 말 것 재확인 |
| 90분 | **전체 리허설 녹음 2회** — 자기소개 90초 → 스토리 A → 스토리 C → BSP 스크립트 → OS 스크립트 → 역질문 5개 | 녹음 듣고 1회 수정 |
| 20분 | [P01 §7 행동 규칙](P01_tech_session_master_plan.html) + 당일 체크리스트 | 준비 완료 |

---

## 2. 매일 10분 (자투리)

- 출퇴근길 **영어로 소리 내어** 자기소개 90초
- 전날 만든 스토리를 안 보고 말하기
- [B04](../bsp/B04_bsp_interview.html) 문항 3개 머릿속으로 답하기

---

## 3. 7일 뒤 손에 있어야 할 것 8개

| # | 산출물 | 완료 기준 | 언제 |
|---|---|---|---|
| 1 | 말해도 되는 JD 인용 6문장 | 카드 1장, 외움 | D1 |
| 2 | J00 우선 3건 답 | 세 줄 메모 | D1 |
| 3 | 자기소개 90초 (영어) | 안 보고 90초 | 매일 |
| 4 | 스토리 A — 인터페이스 디버깅 | 4분, 측정 근거 포함 | D2 |
| 5 | 스토리 C — FPGA·벤더 | 3분, 되묻기로 시작 | D3 |
| 6 | BSP 계층도 + bring-up 순서 | 백지에 그리기 | D4 |
| 7 | **Zephyr 실습 + 실패담** | devicetree 고쳐봄 | D5 |
| 8 | OS 갭 스크립트 (RTOS·eLinux·AOSP) | 안 보고 말하기 | D6 |

---

## 4. 버리는 것 (7일이어도 안 한다)

JD에서 삭제됐거나 이 자리와 거리가 먼 것들이다.

- OTA 인프라 — [C07](../concepts/C07_ota_secure_boot.html) · [J04](../jd/J04_ota_infrastructure.html)
- 온디바이스 AI — [C08](../concepts/C08_on_device_ml_inference.html) · [J05](../jd/J05_on_device_ai_budgets.html) (별도 워크스페이스에서 진행 중)
- 공장 테스트 심화 — [C09](../concepts/C09_factory_test_calibration.html) · [J06](../jd/J06_factory_test_calibration.html) (스토리로만 재사용)
- 무선 프로토콜 심화 — [C06](../concepts/C06_wireless_ble_wifi_thread.html)
- 코딩 02·04·05 — 시간 남을 때만
- 레퍼런스 뱅크 06~21 ([코딩 세션](../coding.html)) — 면접 뒤 장기 학습용. 단 **기초가 불안한 항목만** 골라서 지금 풀어도 좋다 (예: [07 popcount·비트반전](../problems/07_bit_count_reverse.html), [12 volatile](../problems/12_volatile_const.html))

---

## 5. 중간에 연락이 오면

| 남은 시간 | 무엇을 하나 |
|---|---|
| 3~4일 | D1 → D2 → D3 → D7 (실습·OS 방어 생략) |
| 2일 | D1 → D2 → D7 |
| 하루 | [P04 §4 60분 압축판](P04_4day_sprint.html) |
| 반나절 | 스토리 A 2회 + [B00 §3.1·§3.2](../bsp/B00_bsp_overview.html) + [P01 §7](P01_tech_session_master_plan.html) |

**일정 조율 답장은 준비 상태와 무관하게 즉시 한다.** "이번 주도 가능합니다"가 먼저고, 준비는 그 사이에 한다.

---

## 6. 체크리스트

- [ ] D1 — JD 인용 카드, J00 3건 답, 좌표 문장
- [ ] D2 — 스토리 A, 링버퍼 통과
- [ ] D3 — 스토리 C, FPGA 되묻기 문장
- [ ] D4 — BSP 계층도 백지, 레지스터 문제 통과
- [ ] D5 — Zephyr 빌드, devicetree 수정, 실패담 1개
- [ ] D6 — OS 스크립트, Linux 방어선, B04 Q01~Q20
- [ ] D7 — 리허설 녹음 2회, 역질문 5개
- [ ] 매일 — 자기소개 90초 영어로
