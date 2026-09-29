# firmwareTestEngineerPrep — 작성 규칙

Neros **Firmware Test Engineer** HM 45분 Teams 인터뷰 (2026-10-01 목) 대비. 리크루터가 **Python을 물어본다**고 함.
JD: https://job-boards.greenhouse.io/nerostechnologies/jobs/4941340007 (원문은 ../neros_tech_senior_firmware_engineer_context.md 부록 B)

## 폴더
- `plan/`  게임 플랜 (P00)
- `notes/` 스터디 노트 `2026-09-28_N0X_topic.md`
- `python/problems/NN_topic.md` 문제 설명 · `python/starters/NN_topic.py` TODO 뼈대 · `python/solutions/NN_topic.py` 모범답안
- `python/run.py` 채점기: `python3 python/run.py NN [--sol]`, `python3 python/run.py all --sol`
- `build_notes_site.py` → `site/` (직접 수정 금지, md 고치고 다시 빌드)

## 마크다운 (렌더러가 지원하는 부분집합만)
- 첫 줄 `# <이모지> 제목`. 바로 다음에 `> 한두 줄 요약` (카드 요약으로 쓰임)
- `##`/`###` 제목, 문단, `-`/`1.` 목록(**중첩 금지** — 들여쓴 하위 목록 쓰지 말 것), `- [ ]` 체크리스트
- 표는 한 행 = 한 줄. 셀 안 줄바꿈 금지
- 인용 `>`, 코드펜스 ```` ```python ````, `---`, 인라인 `code` / **굵게** / [링크](상대경로.md)
- 노트끼리 링크는 `.md` 상대경로로 쓴다 (빌더가 `.html`로 바꿈). 코드 링크는 `../starters/NN_topic.py`, `../../python/solutions/NN_topic.py` 식으로 쓰면 코드 페이지로 바뀐다

## 톤
- 한국어 기본, 기술 용어는 영어 그대로. **면접에서 말할 문장은 영어**로
- 사실 주장 중 확실하지 않은 건 `[추정]`. Neros 내부 스택은 공고 문구 외엔 전부 추정
- **Don의 경험은 레쥬메 문장 범위에서만** 인용. 숫자·세부를 지어내지 말고 `(Don: 실제 수치 채우기)`로 비워 둔다
  - Apple (2025-12~): RF-Hardware Chipset Integration, PCIe/I2C/SPMI/RFFE 루트코즈, factory test-node 아키텍처, stress 기반 latent defect 발굴, DSO·protocol analyzer
  - SK hynix/Solidigm (2018-10~2025-11): 챔버 테스트 플랫폼 SDK(온도 제어·스케줄링·eSSD 상태 모니터링·UART 테스트 시퀀스 자동화, 다른 엔지니어가 사용), web 기반 test automation script, chip reliability system (NAND V6/7, PCIe 3/4/5 커버리지), NVMe telemetry 디버그 기능 (MS/Dell/HPE), shmoo·health monitoring debug FW, Cortex R8/R82/M0+ · Xtensa FW, error reporting/handling scheme
  - Skills: Embedded C/C++, Python, Linux, Git/Perforce/Jira, Trace32, JTAG, 오실로스코프, LA
  - 레쥬메에 **없는 것**: GitLab CI/Jenkins, pytest(명시 없음), Betaflight/ELRS, Bazel

## Python 코드 규칙
- **Python 3.9 호환**, 표준 라이브러리만 (pytest 없이도 돈다)
- 각 파일: 모듈 docstring(문제 요약) → 구현 → `test_*` 함수들(assert) → `if __name__ == "__main__":` 에서 test_* 전부 실행하고 PASS 출력
- starter는 solution과 **같은 시그니처 + 같은 테스트**, 본문만 `raise NotImplementedError` (+ `# TODO:` 힌트 주석)
- 하드웨어는 전부 Fake 객체(FakeSerial, FakeI2CBus, FakeClock 등)로 흉내 낸다. 시간 의존 코드는 clock/sleep을 주입받게 해서 테스트가 즉시 끝나게
- 검증: `python3 python/run.py all --sol` 전부 PASS, `python3 python/run.py all` 은 TODO로 끝나고 크래시 없음

## 문제 md 구조 (`python/problems/NN_topic.md`)
`# 🐍 NN · 제목` → `> 요약` → `## 왜 나오나` (JD 근거) → `## 문제` (면접관이 영어로 말하는 식 + 한국어 설명) → `## 예시` → `## 엣지 케이스` → `## 힌트` (1→3단계) → `## 풀이 해설` (접근·복잡도·흔한 실수) → `## 말하면서 풀기` (영어 문장) → `## 꼬리 질문` → `## 파일` ([starter](../starters/NN_topic.py) · [모범답안](../solutions/NN_topic.py) · 채점 명령)
