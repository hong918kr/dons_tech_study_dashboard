# 🧭 Neros 온사이트 키트 — 10-22(목) 확정

> **Firmware Test Engineer 온사이트 · 10-22(목) 10:30~14:30 PDT** · 1100 Knox Street, Torrance. 투어(Michael Honar) → **패널 발표 1시간 (6명)** → 점심(Michael Honar) → 1:1 **Adam 30분 · Michael Honar 45분 · Jon Kotowski 45분**. ⚠️ **발표 PDF 48시간 전 제출 필수 (마감 10-20 11:00 PDT → 10-19 제출)**. 노트북은 현장 지급.

## 0. 하루 일정 한눈에

| 시간 | 세션 | 준비 |
|---|---|---|
| ~10:05 | 도착 · 키오스크 체크인 (신분증 · 법적 이름) | [K01](plan/2026-10-14_K01_onsite_day_schedule_panel.md) 3절 |
| 10:30 | Factory Tour — Michael Honar | K01 4절 투어 질문 |
| 11:00 | **패널 발표** — Elango · Aguayo · Kibit · Honar · Vargas · Kotowski | [M0](modules/2026-10-12_M0_presentation_40min.md) |
| 12:00 | 점심 — Michael Honar | K01 5절 |
| 12:30 | 1:1 Adam Kibit (30분) — system design 압축판 또는 디렉터 대화 [추정] | [M1](modules/2026-10-12_M1_embedded_system_design.md) 11절 |
| 13:00 | 1:1 Michael Honar (45분) — HW/test design [추정] | [M3](modules/2026-10-12_M3_hw_test_design.md) |
| 13:45 | 1:1 Jon Kotowski (45분) — Python 코딩 [추정] | [M2](modules/2026-10-12_M2_coding_python.md) + C01~C07 |

## 1. 오늘 할 것 (10-14)

1. [K01 확정 일정 · 패널](plan/2026-10-14_K01_onsite_day_schedule_panel.md) — 7절 **답장 초안**(식이 제한) 보내기
2. [M0 발표](modules/2026-10-12_M0_presentation_40min.md) — 슬라이드 16장 제목 정하기 (제출까지 5일)
3. [K00 계획](plan/2026-10-12_K00_onsite_kit_plan.md) 3절 — 다시 짠 날짜별 일정
4. 코딩: [C02](coding/2026-10-12_C02_fixed_fifo.md) 재풀이 → [C04](coding/2026-10-12_C04_retry_queue.md)

## 2. 네 가지 주제

| # | 주제 | 시간 | 페이지 | 한 줄 |
|---|---|---|---|---|
| 0 | 발표 | 40분 + Q&A | [M0](modules/2026-10-12_M0_presentation_40min.md) | 자동화 · SW test · system test · 아티팩트 · 대시보드 · 짧은 스토리 · 문제 해결 전략 |
| 1 | embedded system design | 45분 | [M1](modules/2026-10-12_M1_embedded_system_design.md) | scheduler · task 수 · stack 증명 · PSP/MSP · rx/tx · motor |
| 2 | 코딩 (Python) | 45분 | [M2](modules/2026-10-12_M2_coding_python.md) | 힌트 없음. overflow · 잘린 패킷 · timeout · wrap |
| 3 | HW / test design | 45분 | [M3](modules/2026-10-12_M3_hw_test_design.md) | 고장 난 PDB 접근 순서 · 테스트 안 된 서브시스템 통합 · 결과 정리 |

## 3. 코딩 문제 (Python, 전부 테스트 통과)

| # | 문제 | 우선 |
|---|---|---|
| [C01](coding/2026-10-12_C01_packet_stream.md) | 바이트 스트림 → 패킷 · resync · 버퍼 상한 | S |
| [C02](coding/2026-10-12_C02_fixed_fifo.md) | 고정 크기 FIFO · 꽉 참 정책 | S |
| [C03](coding/2026-10-12_C03_sliding_window.md) | 이동 평균 · K-of-N 알람 · None/NaN | A |
| [C04](coding/2026-10-12_C04_retry_queue.md) | ACK · timeout · retry · 8비트 id wrap | S |
| [C05](coding/2026-10-12_C05_seq_gap_log.md) | 로그 유실 · 중복 · 순서 · 16비트 wrap | A |
| [C06](coding/2026-10-12_C06_failsafe_fsm.md) | arm/failsafe 상태 머신 | A |
| [C07](coding/2026-10-12_C07_binary_telemetry.md) | struct 디코드 + CRC-8 | B |

```text
cd onsiteKit/code
python3 run_all.py                   # 정답 7/7
python3 run_all.py --practice c02    # 내 풀이 (practice/ 의 TODO 채운 것)
python3 make_practice.py c02 --reset # 연습 파일 다시 비우기
```

## 4. 공부하다 생긴 질문

- [Q00 질문 로그](qa/2026-10-12_Q00_qa_log_how_to.md) — 물어본 것은 `qa/QNN` 하위 페이지로 쌓인다

## 5. 참고 (필요할 때만)

- 지난 스크린 회고 (C ring buffer · extract_packet · CRC): [L09](../../technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html)
- 이전 온사이트 자료 (형식 확정 전): [onsitePrep](../../onsitePrep/index.html)
- 컨텍스트: [neros_tech_senior_firmware_engineer_context](../neros_tech_senior_firmware_engineer_context.md)
