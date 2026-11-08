# 레슨 작성 규칙 (NOTES_SPEC)

임베디드 스터디 노트의 **레슨 1개 = 아이템 1개 = 문제 1개**다. 이 문서는 레슨을 쓰는 사람(Don 또는 작성 에이전트)이 따르는 규칙이다.
목표: 빠르게 *Learning by doing* — 아는 내용은 직접 풀며 재검토하고, 새 분야는 Basic → Mid → Advanced 순서로 설명 + 실전 인터뷰 코딩 문제로 익힌다.

## 0. 파일 위치와 이름

```text
curriculum/stages.json              단계(1~10) · 트랙 목록 · env 종류
curriculum/tracks/<TRACK>.json      트랙 소개 · 목표 · 아이템 목록 (id, level, title, goal, task, env)
lessons/<TRACK>/<ID>/
    YYYY-MM-DD_<ID>_<slug>.md       레슨 본문 (작성일 접두사)
    starter.c                       시작 코드 (TODO + 테스트 하네스)
    solution.c                      정답 코드 (TODO를 채운 전체 코드, 하네스는 starter와 동일)
    practice.c                      추가 연습 한 번에 검증하는 코드
```

- 언어에 따라 `.cpp` / `.py` / `.s` 도 된다. 파일 이름(starter/solution/practice)은 고정.
- 레슨 md가 생기면 그 아이템은 자동으로 **학습가능**이 된다. md가 없으면 **준비중**.
- `python3 build_site.py` → `site/` 재생성. `python3 verify_lessons.py` → 모든 레슨 코드 컴파일·실행 검증. 생성된 HTML은 손으로 고치지 않는다.

## 1. 머리말 (front matter)

```text
---
id: C-B01
time: 40분
updated: 2026-11-07
---
```

제목·레벨·트랙은 `curriculum/tracks/<TRACK>.json`에서 가져오므로 md에는 `# <ID> <제목>` 한 줄만 둔다.

## 2. 본문 구성 (순서 고정, `##` 제목 문자열도 그대로)

| # | 제목 | 내용 |
|---|---|---|
| — | `## 학습 목표` | 이 레슨이 끝나면 할 수 있는 것 3~5개 |
| 1 | `## 1. 문제` | 한국어 설명 + **그림(```svg 펜스) 최소 1개**. 과제를 **Part A / Part B / Part C** 로 구체적으로 나눈다 (`### Part A — ...`) |
| 2 | `## 2. Problem (English)` | 실제 면접 문제처럼 영어로. 함수 시그니처, 제약, 예시 입출력 |
| 3 | `## 3. 시작 코드` | `<!-- code: starter.c -->` + 빌드/실행 명령. TODO 자리 표시, 테스트 하네스 포함 |
| 4 | `## 4. 힌트` | 단계별 `<details><summary>힌트 1 — ...</summary>` 접기. 답을 바로 주지 않는다 |
| 5 | `## 5. 풀이` | 한국어로 최대한 자세히: 그림 · 플로우차트 · 표 · 단계별 추론. Part별로 |
| 6 | `## 6. 정답 코드` | `<!-- code: solution.c diff=starter.c -->` — 시작 코드 대비 바뀐 줄이 자동 강조됨 |
| 7 | `## 7. 자주 하는 실수` | 실수 → 왜 틀렸나 → 고친 코드 |
| 8 | `## 8. 면접 Q&A` | 꼬리질문 포함 5개 이상. 질문은 `<details><summary>Q. ...</summary>` 로 접기 |
| 9 | `## 9. 추가 연습` | 본문 문제를 응용한 과제 3~5개. 각 답은 `<details>` 로 접기. 마지막에 `<!-- code: practice.c -->` 로 한 번에 검증 |
| 10 | `## 10. 핵심 정리` | 5~8줄 요약 (암기용) |
| — | `## 참고자료` | 스펙 · 책 · 공식 문서 링크 |

## 3. 코드 규칙

- C: `cc -std=c11 -Wall -Wextra -Werror -O2 file.c && ./a.out` 가 **경고 0개**로 통과해야 한다. C++: `c++ -std=c++20 ...`.
- 테스트 하네스: `CHECK(expr)` 매크로로 PASS/FAIL 줄을 찍고 마지막에 `N/M passed` 를 출력, 실패가 있으면 `return 1`.
- **starter.c**: 컴파일은 되어야 한다(스텁은 `(void)x;` 로 경고 제거). 실행하면 TODO 부분 테스트가 FAIL.
- **solution.c**: starter.c와 하네스·구조가 같고 TODO 부분만 채운다 → 빌더가 diff로 바뀐 줄을 강조한다. 전부 PASS, exit 0.
- **practice.c**: 추가 연습 답을 모두 담고 전부 PASS, exit 0.
- 맥에서 돌릴 수 없는 레슨(env ≠ mac)은 코드에 `/* ENV: linux */` 같은 첫 줄 주석을 두고, verify는 건너뛴다.

## 4. 그림 규칙

- ```svg 펜스 안에 손으로 그린 인라인 SVG. `viewBox` 필수, 폭 ≤ 760. 내용이 viewBox 밖으로 나가지 않게.
- 색은 `currentColor` 와 아래 클래스만 사용 (다크 모드 자동 대응):
  `class="f-acc"`(강조 채움) `f-ok` `f-warn` `f-mut` / 선은 `stroke="currentColor"`.
- 글꼴은 지정하지 않는다 (페이지 글꼴을 따름). 글자 크기 12~14.

## 5. 톤

- 한국어, 쉬운 말. 영어 용어는 처음 나올 때 괄호로 병기.
- "왜 이게 임베디드/면접에서 중요한가"를 1. 문제 첫 문단에 한 줄로.
- 펌웨어 관점 연결: 레지스터, ISR, 메모리 제약, 실제 칩(STM32, nRF, Cortex-M) 예.
