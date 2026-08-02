# Interview Prep — 라운드별 준비 (틀)

> 4개 타깃 회사 인터뷰를 라운드 유형별로 준비. 회사·직무별 디테일은 하위 노트로 확장.
> _생성 2026-07-12 — 채워나갈 예정_

---

## A. 라운드 유형 (HW/실리콘/FW 직군 공통)

| 라운드 | 내용 | 내 준비 상태 |
|--------|------|-------------|
| **Recruiter / Behavioral** | 경력 스토리, 왜 이 회사/직무, 임팩트 | ⬜ |
| **Domain deep-dive** | 인터페이스/브링업/FW/실리콘 기술 심화 Q&A | ⬜ |
| **Coding** | C/알고리즘, 임베디드 스타일 | ⬜ |
| **System / Architecture** | SoC/가속기/시스템 설계 토론 | ⬜ |
| **Debugging / scenario** | "이 신호가 안 뜬다, 어떻게 디버그?" 실전 | ⬜ |

---

## B. Behavioral — STAR 스토리 뱅크 (채울 것)
경력에서 임팩트 스토리 5~7개를 STAR(Situation/Task/Action/Result)로 정리:
- [ ] ⬜ RF 칩 integration에서 seamless integration 문제 해결 (라인 이슈 등)
- [ ] ⬜ PCIe/인터페이스 브링업 난제 디버그
- [ ] ⬜ SSD 펌웨어에서 신뢰성/성능 개선 (데이터센터 임팩트)
- [ ] ⬜ 팀/부서 간 협업으로 통합 성공시킨 사례
- [ ] ⬜ 실패/배움 스토리
- [ ] ⬜ "왜 지금 AI 가속기/로보틱스로 전환하는가" 내러티브 (수학+시스템 자산 연결)

## C. Domain Deep-dive — 예상 질문 (트랙 연계 → [`../study/00_study_plan.md`](../study/00_study_plan.md))
- [ ] ⬜ PCIe LTSSM/equalization 설명, 브링업 시 안 링크되면?
- [ ] ⬜ SerDes eye/jitter/BER, SI 관점 디버그
- [ ] ⬜ I2C vs SPI vs SPMI vs RFFE 트레이드오프·용도
- [ ] ⬜ SSD FW 아키텍처, FTL/신뢰성, 데이터센터 요구
- [ ] ⬜ AI 가속기 데이터플로우, 메모리 대역폭 병목
- [ ] ⬜ HW-SW co-design에서 FW의 역할

## D. Coding — 준비
- [ ] ⬜ C 자료구조/알고리즘 (루트 `../../lecture_notes` 코딩탭 연계)
- [ ] ⬜ 임베디드 문제 (비트 조작, 링버퍼, 레지스터 R/W, 상태머신)

## E. System / Architecture — 준비
- [ ] ⬜ "가속기 카드 브링업 계획을 세워보라" 류
- [ ] ⬜ SoC 통합/인터커넥트 설계 토론
- [ ] ⬜ 신뢰성/DFX 관점

## F. 회사별 준비 (하위 노트로 확장 예정)
- [ ] ⬜ `interview/nvidia.md` — post-si/FW 심화, 랩 시나리오
- [ ] ⬜ `interview/google.md` — TPU 통합/아키텍처
- [ ] ⬜ `interview/openai.md` — robotics FW / co-design, 넓은 오너십
- [ ] ⬜ `interview/anthropic.md` — 시스템 SW / C++, 플랫폼 브링업

> 다음: "NVIDIA 도메인 예상질문 만들어줘" 처럼 요청하면 회사별/주제별로 문제·모범답안 채움.
