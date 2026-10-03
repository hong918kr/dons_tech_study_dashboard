# 🎙️ N05 · 말하면서 코딩 — 영어 대사 · C 개발자의 Python 습관 · 역질문 · 당일 체크

> 1시간 코딩 스크린의 점수 절반은 **말**에서 나온다: 질문으로 시작하고, 계획을 먼저 말하고, 테스트로 끝내고, 막히면 소리 내어 좁혀 가기. Don의 강점(디버깅·루트코즈)을 그대로 코딩 습관으로 보여 주는 대사 모음이다. 일반 Python 개념 30초 답은 [FTE N03 12절](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N03_python_for_test_automation.html), 경험 스토리는 [FTE N09 레쥬메 스토리라인](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html)에 있다.

## 1. 첫 5분 — 45초 자기소개 (스크린용)

> "Hi Jon, thanks for making the time. Quick background: I've spent about seven years writing production firmware in C — SSD controllers at SK hynix and Solidigm, from FPGA bring-up to shipped PCIe drives — and since last December I've been at Apple on RF chipset integration, root-causing failures where new silicon meets the full system.
>
> The part that's most relevant today: at SK hynix I built a web-based test platform and Python SDK for chamber reliability testing — scheduling, temperature control, UART test sequences — that other engineering teams used every day. So I've been on both sides: writing the firmware, and building the tooling that proves it works. That's exactly what drew me to the firmware test role at Neros."

- "about seven years" — 이전 라운드와 같은 표현 유지 [컨텍스트 파일 진행 로그 09-18]
- 면접관이 Full Stack이면 **web 플랫폼 + SDK**를 앞에 둔다. 웹 스택 세부는 (Don: 실제 스택 — 묻기 전엔 말하지 않기)

## 2. 코딩 단계별 대사

| 단계 | 대사 |
|---|---|
| 문제 받음 | "Let me restate it to make sure I've got it: …. A few questions before I code." |
| 질문 | "What's the element type? Fixed capacity? What should happen when it's full — reject, overwrite, or block? Single-threaded to start?" |
| 언어 | "I'm comfortable in both Python and C. I'll start in Python so we can test it quickly — happy to switch to C if you'd like to see the embedded version." |
| 계획 | "Plan: data layout first, then push and pop, walk through a small example by hand, then tests." |
| 상태 정의 | "Head is the next write slot, count tells full from empty, and tail is derived." |
| 손 시뮬레이션 | "Let me trace capacity three: push 1, 2, 3 — full. Pop gives 1. Push 4 goes into slot zero. Iteration gives 2, 3, 4. Good." |
| 테스트 전환 | "Before I call this done, I'd like to write a few tests — empty, exactly full, wraparound, and the overwrite policy." |
| 시간 부족 | "I'm going to skip the bulk API and make sure the core is correct and tested — I can describe the bulk version." |
| 마무리 | "Complexity is O(1) per operation, fixed memory. Things I didn't cover: thread safety and a byte-optimized version — want me to go into either?" |

## 3. 막혔을 때 · 틀렸을 때 — 디버깅을 보여 주는 대사

- 테스트가 실패: "Good — the test caught something. Expected [2, 3, 4], got [4, 2, 3], so iteration is in storage order, not logical order. The fix is to start from tail."
- 기억이 안 남: "I don't remember the exact signature of `Condition.wait` — I believe it takes a timeout and returns a bool. I'll write it that way and verify in the docs."
- 접근을 바꿈: "This index arithmetic is getting fiddly. Let me step back — if I track count instead of two indices, full and empty are trivial."
- 힌트를 받음: "That's a good point — so the case I'm missing is …" (힌트를 **반복해 확인**하고 바로 반영)
- 30초 이상 침묵 금지: 생각 중이면 "I'm thinking about whether pop should raise or return None — raising is more Pythonic, and returning None is ambiguous if None can be stored. I'll raise."

## 4. C 개발자의 Python 습관 — 면접관 눈에 띄는 것

| C 습관 | Python답게 | 왜 |
|---|---|---|
| `for i in range(len(xs)): x = xs[i]` | `for x in xs:` / `for i, x in enumerate(xs):` | 가장 먼저 눈에 띄는 신호 |
| 두 리스트 인덱스 | `for a, b in zip(xs, ys):` | |
| 에러 코드 반환 `-1` | 예외(`ValueError`, `IndexError`), 커스텀 예외 계층 | 호출자가 무시할 수 없다 |
| 수동 `open/close` | `with` (context manager) | 예외에도 정리 |
| 구조체 = dict 남발 | `@dataclass(frozen=True)` | 필드 · 기본값 · `__eq__` |
| 비트 연산 후 마스킹 잊음 | `& 0xFF` 명시, `int.from_bytes(b, "little")`, `struct.unpack("<HB", …)` | Python int는 무한 정밀도 |
| 문자열 누적 `s += …` | `"".join(parts)`, f-string | |
| 바이트를 `str`로 | `bytes`/`bytearray`, `.hex()`, `b"\xAA"` | 시리얼 데이터는 bytes |
| 매직 넘버 | 모듈 상수 `SYNC = 0xAA` | |
| `if x == True` / `== None` | `if x:` / `is None` | |
| 전역 상태 | 클래스 + 주입(DI) | 테스트 가능성 |

- 더 많은 함정: [FTE N03 11절 C 개발자가 Python에서 자주 틀리는 것](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N03_python_for_test_automation.html)
- 손풀기 문제: [FTE 문제 12 Python 기초](../../firmwareTestEngineerPrep/site/problems/12_python_basics.html) · [FTE 문제 10 워밍업](../../firmwareTestEngineerPrep/site/problems/10_python_warmups.html)

## 5. 환경 대비

- 공유 에디터(CoderPad, HackerRank, CodeSignal 등) [추정] — 실행 가능하면 **실제로 테스트를 돌린다**. pytest가 없으면 `assert` 함수 몇 개 + `if __name__ == "__main__":`
- 실행 불가(Google Doc 등) — "I'd run this with pytest; I expect these three to pass and this one to fail before the fix"
- 화면 공유로 **내 IDE**를 쓰라고 하면: `technicalScreenPrep/.venv` + VS Code, 폰트 크게, 알림 끄기
- 시작 전: 카메라 · 마이크 · 이어폰, 물, 종이와 펜(손 시뮬레이션), 이 노트 1·2절을 옆 화면에

## 6. 역질문 (마지막 5분) — Full Stack 면접관용

1. "How does test data flow to the software side today — is there a results database or dashboard the test team and your team share?"
2. "What does the boundary between the test organization and application/backend engineering look like — who owns test infrastructure services?"
3. "What does a typical week look like for you, and how often do you work with firmware or test engineers?"
4. "If I joined, what would you hope the test team builds first that would make your work easier?"
5. (시간 남으면) "What's the stack for internal tools here — I saw React, Python and Grafana in the postings?" [확인됨 공고 문구]

## 7. 당일 체크리스트 (10-08 목 11:00)

- [ ] 09:30 — [P00](../plan/2026-10-02_P00_tech_screen_game_plan.md) "기억할 다섯 줄" 읽기
- [ ] 09:40 — [문제 01](../python/problems/01_ring_buffer.md)을 빈 파일에 12분 (손풀기)
- [ ] 10:00 — [N02](2026-10-02_N02_pytest_in_practice.md) 1절 문법 블록 한 번
- [ ] 10:15 — [N04](2026-10-02_N04_system_design_test_tooling.md) 문제 C 영어 요약 소리 내기
- [ ] 10:30 — 1절 자기소개 소리 내어 2번, 6절 역질문 3개 메모
- [ ] 10:50 — 환경 확인 (링크 · 카메라 · 마이크 · 에디터 · 물)
- [ ] 종료 후 — 받은 질문 원문 · 내 답 · 아쉬운 점을 바로 메모 → 컨텍스트 파일 §6
