# technicalScreenPrep — 작성 규칙

Neros technical screen 대비. [확인됨 2026-10-02 리크루터]:
- 2026-10-08(목) 11:00, 1시간, 코딩 위주 예상. 온사이트 **전에** 추가된 1세션
- 주제: pytest · ring buffer · Python/C 둘 다 가능 · system design
- 면접관: Jon Kotowski — Neros Full Stack Engineer [추정, Datanyze 단일 출처 + ZoomInfo 스니펫]. LinkedIn 로그인 조회 · 친구 신청 금지
- 이전 라운드: Adam Kibit (09-18), Michael Honor (10-01, 퀴즈형, Python 미출제). 다음: 온사이트 (`../onsitePrep/`)
- 컨텍스트: `../neros_tech_senior_firmware_engineer_context.md`

## 폴더
- `plan/` 게임 플랜 · `notes/` 노트 `2026-10-02_N0X_topic.md`
- `python/problems/NN_*.md` 문제 · `python/starters/` 내 풀이 · `python/solutions/` 모범답안 · `python/run.py` 채점
- `python/lib/` 피검 코드(code under test) · `python/lib/mutants/` 버그 심은 버전 · `python/graders/` 문제 04 채점기
- `c/rb_shim.c` — `../onsitePrep/code/ring_buffer.c`를 `#include`해서 ctypes용 심볼을 export (원본 수정 금지)
- `.venv/` pytest 설치된 가상환경 (git 무시)
- `START_HERE.md` 읽는 순서 · `build_notes_site.py` → `site/` + 루트 `index.html` (생성물, 직접 수정 금지)

## 재사용 (복제 금지, 링크만)
- FTE 준비: `../firmwareTestEngineerPrep/site/` — N03 Python, N04 pytest, N05 CI/Git, N02 HIL, N07·N09 스토리, Python 문제 12개
- 온사이트 준비: `../onsitePrep/site/` — N05 ring buffer 플레이북, N06 system design(9절 HIL 팜), C 코드 5종
- 링버퍼 7레벨: `../research/ringbuffer_research/html/index.html` · Neros C 연습: `../practice/html/index.html`

## 링크 쓰는 법
- md 파일 **자기 위치 기준** 상대 경로로 쓴다. 빌더가 HTML 위치에 맞게 다시 계산한다
- 이 폴더 안: `.md` · `.py` · `.c` 그대로 링크 → 사이트 페이지로 바뀜
- 바깥 자료: 생성된 `.html`을 직접 링크 (예: `../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html`)

## 마크다운 (렌더러 지원 부분집합)
- 첫 줄 `# <이모지> 제목`, 바로 다음 `> 요약`
- `##`/`###`, 문단, `-`/`1.` 목록(**중첩 금지**), `- [ ]`, 표(한 행 한 줄), 인용, 코드펜스, `---`, 인라인 `code`/**굵게**/[링크]

## 톤과 사실
- 한국어 기본, 기술 용어는 영어. **면접에서 말할 문장은 영어**
- 확실하지 않은 건 `[추정]`. Neros 사실은 공고 문구 · 리크루터 · 면접에서 들은 것만 `[확인됨]`
- **Don 경험은 레쥬메 문장 범위만.** 구체 디테일은 `(Don: …)`로 비우고 지어내지 않는다 (규칙 상세: `../onsitePrep/NOTES_SPEC.md`)

## Python 코드 규칙
- Python 3.9+ (로컬 `/usr/bin/python3` 3.9.6), pytest만 외부 의존
- 문제 파일 = 구현 + `test_*` 함수가 한 파일에. 테스트 작성 문제는 첫 docstring에 `# mutants: <lib 모듈명>`
- `python3 python/run.py all --sol`이 전부 ✓ 이어야 한다 (mutant 9/9 포함)
