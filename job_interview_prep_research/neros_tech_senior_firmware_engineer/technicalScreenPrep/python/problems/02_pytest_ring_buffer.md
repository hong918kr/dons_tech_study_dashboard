# 🧪 02 · pytest로 남의 ring buffer 테스트하기 — fixture · parametrize · mutant 9개

> "Here's a ring buffer someone wrote. Write tests for it." 테스트 조직 면접에서 가장 자연스러운 pytest 문제다. 구현은 보지 않고 **스펙만 보고** 테스트를 쓴다. 채점기가 버그를 하나씩 심은 mutant 9개를 끼워 보고, 내 테스트가 몇 개를 잡는지 알려 준다.

## 왜 나오나

- 리크루터 주제 1번이 **pytest** [확인됨 2026-10-02]. Neros 테스트 팀(Michael Honor)이 Python/pytest 기반 HIL을 만든다는 정황 [추정, JD "Python" + "automated testing frameworks"]
- Full Stack 엔지니어는 매일 테스트를 쓴다(웹 백엔드에서 pytest는 표준). 그 사람이 보는 건 **테스트 설계 감각**: 무엇을 경계로 보는가, fixture를 어떻게 나누는가, 테스트 이름이 스펙처럼 읽히는가
- 문제 01을 짠 직후 "Now test it" 으로 이어지기 쉽다

## 문제

> "Here's a `RingBuffer` class and its docstring. Don't read the implementation — write a pytest suite that would catch bugs in it."

- 피검 코드: [lib/ringbuf.py](../lib/ringbuf.py) 의 docstring 스펙 (문제 01과 같은 스펙)
- 테스트 파일은 `RingBuffer = load("ringbuf").RingBuffer` 로 가져온다 → 채점기가 환경변수로 mutant를 바꿔 끼운다
- 목표: 정상 구현에서 전부 통과 + **mutant 9/9 killed**, 25분

## 채점 방식 — mutation testing

| 단계 | 기준 |
|---|---|
| 1 | 정상 구현(`lib/ringbuf.py`)에서 전부 통과. 실패하면 **테스트가 틀린 것** |
| 2 | `lib/mutants/ringbuf_m1.py` … `m9.py`를 하나씩 끼워 실행. **하나라도 실패하면 killed** |
| 결과 | `mutants: 7/9 killed · survived: m4 m6` 처럼 나온다 |

이 방식 자체가 면접 이야깃거리다: "How do you know your tests are good?" → "Coverage tells me what ran, not what was checked. Mutation testing — injecting small bugs and seeing whether the suite fails — tells me whether the assertions are strong. In Python, `mutmut` does that."

## 엣지 케이스 (테스트해야 할 것)

- 비어 있을 때 `pop`/`peek` → `IndexError` (None을 돌려주는 버그)
- capacity개를 **전부** 쓸 수 있나 (한 칸 덜 쓰는 off-by-one)
- drop-new: 반환값 `False`, 기존 내용 유지 (True를 돌려주는 버그)
- overwrite: **가장 오래된** 것이 빠지나, `dropped`가 정확한가
- wraparound 뒤의 `iter` 순서와 `pop` 순서
- `peek`이 소비하지 않나, `clear`가 개수까지 리셋하나
- 잘못된 capacity (0, 음수, float, None)

## 힌트

1. fixture 셋: `rb`(빈 버퍼) → `full_rb(rb)`(fixture가 fixture를 받음) → `wrapped_rb`(push 4 → pop 2 → push 2). 상태별 fixture가 있으면 각 테스트가 한 줄짜리가 된다
2. `@pytest.mark.parametrize("method", ["pop", "peek"])` + `getattr(rb, method)()` 로 같은 검사 두 번
3. `pytest.raises(IndexError, match="empty")` — 예외 종류와 메시지까지
4. 보너스 **model-based test**: `collections.deque`를 정답 모델로 두고 무작위 연산 1000번을 나란히 실행, 매 단계 `list(rb) == list(model)`. `random.Random(seed)`로 재현 가능하게. 손으로 못 찾는 경계를 기계가 찾는다 (Hypothesis의 stateful testing이 이것의 라이브러리판)

## 풀이 해설

- **테스트 이름 = 스펙 문장**: `test_drop_new_returns_false_and_keeps_old` 처럼 실패 리포트만 보고 무엇이 깨졌는지 알게
- **assert 하나에 한 사실**이 이상적이지만, 상태 검사는 `len · 내용 · dropped`를 한 테스트에서 같이 보는 게 실용적이다. 판단 기준은 "실패했을 때 메시지로 원인을 알 수 있나"
- **fixture가 상태를 만든다**: 테스트 본문은 "행동 → 검증"만 남는다 (Arrange를 fixture로)
- **parametrize ids**: `ids=lambda n: f"{n}_extra"` → 리포트가 `test_overwrite[3_extra]`처럼 읽힌다
- **seed 고정 무작위 테스트**: flaky가 되지 않게 seed를 고정하고, 실패 메시지에 step 번호를 넣는다. CI에서 seed를 바꿔 가며 돌리는 건 nightly로

## 말하면서 풀기

- "I'll start from the spec, not the code — I want tests that would catch a wrong implementation, not tests that mirror this one."
- "First the boundaries: empty, one element, exactly full, one past full, and wraparound. Then the two full-buffer policies. Then a randomized test against a reference model, with a fixed seed so it's reproducible."
- "I'm putting the setup in fixtures so each test reads as one behavior."

## 꼬리 질문

- "How would you measure test quality?" → coverage(line/branch)는 '실행됐나'만 안다. mutation score가 '검증했나'를 안다. `pytest --cov`, `mutmut`
- "What's the difference between a fixture and setUp?" → fixture는 필요한 테스트만 이름으로 요청, scope(function/module/session), yield로 정리, 다른 fixture를 조합 → [FTE N04 4절](../../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html)
- "Would you use Hypothesis?" → 예. `RuleBasedStateMachine`으로 push/pop 규칙과 불변식(`0 <= len <= capacity`)만 정의하면 반례를 최소화해서 보여 준다
- "This test is slow in CI." → model-based는 연산 횟수를 줄여 PR에, 큰 버전은 `@pytest.mark.slow`로 nightly

## 파일

- [starter](../starters/02_pytest_ring_buffer.py) · [모범답안](../solutions/02_pytest_ring_buffer.py) · 피검 코드 [lib/ringbuf.py](../lib/ringbuf.py)
- 채점: `python3 python/run.py 02` · `python3 python/run.py 02 --sol`
- mutant 파일(`lib/mutants/ringbuf_m*.py`)은 **다 푼 뒤에** 열어 볼 것
