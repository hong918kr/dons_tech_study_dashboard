# Career Study Notes

> **커리어 전환 준비 저장소** — JD를 모으고, 나만의 인터뷰 준비와 필요한 지식 스터디를 정리하는 곳.
> 루트의 [`lecture_notes`](../lecture_notes) 웹앱처럼 **차곡차곡 쌓아가는** 구조입니다.

## 목표
Embedded/RF integration + SSD firmware 배경 → **AI accelerator · physical AI · on-device LLM · robotics** 방향의
차세대 칩셋/시스템 직무로 전환. 타깃 4사: **NVIDIA · Google · OpenAI · Anthropic**.
(내 배경 상세: [`context.md`](./context.md))

## 디렉토리 구조
```
career_study_notes/
├─ README.md              # 이 파일 (프레임워크 안내)
├─ context.md             # ★ 내 소개 / 커리어 기준점 (항상 첫 참조)
├─ dashboard.html         # 웹앱 스타일 네비게이션 허브 (더블클릭 → 브라우저)
├─ roles/                 # 회사·직무별 타깃 + 요구역량 + 내 갭
│  ├─ 00_overview.md      #   배경→직무 매핑, fit 매트릭스, 전략
│  ├─ nvidia.md
│  ├─ google.md
│  ├─ openai.md
│  └─ anthropic.md
├─ study/                 # 직무가 요구하는 지식 (개념+연습문제)
│  └─ 00_study_plan.md    #   주제 트리(틀) — 여기서 주제 뽑아 노트로 분리
└─ interview/             # 라운드별 인터뷰 준비
   └─ 00_interview_prep.md#   유형별 준비(틀) — 회사별로 확장
```

## 워크플로우 (권장 순서)
1. **[`context.md`](./context.md)** 확인/갱신 — 모든 준비의 기준점
2. **[`roles/`](./roles/)** 에서 관심 직무 shortlist(⭐ 체크)
3. shortlist의 **갭 → [`study/00_study_plan.md`](./study/00_study_plan.md)** 로 이관
4. 주제별로 `study/<주제>.md` 채우기 (개념·연습문제) — 나한테 "이 주제 정리해줘" 요청
5. **[`interview/`](./interview/)** 라운드별 준비 (STAR 스토리, 예상질문)
6. **[`dashboard.html`](./dashboard.html)** 로 전체 조망 (메모는 localStorage 자동저장)

## 이 저장소를 나(Claude)와 함께 쓰는 법
- "NVIDIA post-si 예상질문 만들어줘" → `interview/nvidia.md` 생성/추가
- "PCIe Gen6 정리 + 연습문제" → `study/pcie.md` 생성
- "이 JD 붙여넣을게, roles에 정리해줘" → 해당 회사 파일에 role 추가
- "context 업데이트: ~" → `context.md` 갱신

## 참고
- 리서치 기준일 **2026-07-12**. 채용공고는 수시 변동 → 지원 전 원문 재확인.
- 코딩 연습은 루트 [`lecture_notes`](../lecture_notes) 웹앱의 코딩 탭도 활용.
