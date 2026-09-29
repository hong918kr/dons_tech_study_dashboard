# 🐍 07 · CI results analyzer — JUnit XML 파싱과 flaky / regression 분류

> pytest가 뱉는 JUnit XML을 파싱해 요약하고, 커밋별 실행 이력에서 flaky 테스트와 regression을 구분한 뒤, bisect로 처음 깨진 커밋을 찾는다. xml.etree · defaultdict · 이진 탐색.

## 왜 나오나

- JD: "Integrate automated tests into CI/CD pipelines", "**Monitor the test results and ensure the stability of builds before releases**", "Experience with a CI/CD tools - Gitlab CI, Jenkins"
- 테스트 엔지니어의 실제 하루는 "어젯밤 nightly가 빨간데, 진짜 버그냐 flaky냐"를 가르는 일이다. HIL 테스트는 실제 하드웨어·RF·USB가 끼어서 **flaky가 소프트웨어 유닛 테스트보다 훨씬 많다**.
- GitLab CI는 `artifacts: reports: junit: report.xml`로 JUnit XML을 받아 MR 화면에 실패 테스트를 보여 준다. pytest는 `pytest --junitxml=report.xml`로 이 파일을 만든다.

## 문제

> "Our CI produces JUnit XML from pytest. (1) Parse it into a summary — totals, pass/fail/error/skip counts, total time, and the list of failing test IDs with messages. (2) Given the history of runs across commits as `(commit_index, test_name, 'pass' | 'fail')`, classify each test as stable, failing, flaky, regression, or fixed. For regressions, report the first bad commit. (3) If running the HIL suite costs ten minutes per commit, how do you find the first bad commit efficiently?"

한국어로:

- `parse_junit(xml_text)` → `JUnitSummary`. 루트가 `<testsuites>`든 `<testsuite>`든 동작
- `classify(history)` → `{test: (category, first_bad)}`
- `first_bad_commit(is_bad, good, bad)` → git bisect와 같은 이진 탐색

분류 규칙:

| category | 조건 | 의미 |
|---|---|---|
| stable | 전부 pass | 문제 없음 |
| failing | 전부 fail | 처음부터 깨짐 / 환경 문제 |
| flaky | **같은 커밋**에서 pass·fail 섞임, 또는 pass↔fail이 2번 이상 뒤집힘 | 코드가 같은데 결과가 다름 → 테스트나 리그 문제 |
| regression | pass…pass → fail…fail (뒤집힘 1번) | 어떤 커밋이 깨뜨림 → first_bad 보고 |
| fixed | fail…fail → pass…pass | 누군가 고침 |

## 예시

```python
s = parse_junit(open("report.xml").read())
s.total, s.passed, s.failed, s.errors, s.skipped   # (5, 2, 1, 1, 1)
s.failures[0]   # ("test_arming::test_arm_refused_throttle_high", "armed with throttle 1500")

classify([(0, "t", "pass"), (1, "t", "pass"), (2, "t", "fail"), (3, "t", "fail")])
# {"t": ("regression", 2)}

first_bad_commit(lambda c: c >= 737, good=0, bad=1000)   # 737, is_bad 호출 10번 이하
```

## 엣지 케이스

- `<failure>`와 `<error>`는 다르다: failure = assert 실패(제품 버그 후보), error = 테스트 자체가 예외로 죽음(시리얼 포트 없음 등 **리그 문제** 후보). 둘 다 빨간색이지만 원인 분류가 다르다.
- `<skipped>`는 total에는 들어가지만 pass도 fail도 아니다.
- `time` 속성이 없거나 빈 문자열인 경우 → 0으로 처리
- history가 커밋 순으로 정렬돼 있지 않을 수 있다
- 같은 커밋을 retry로 여러 번 돌린 기록 → 결과가 섞이면 그 자체가 flaky의 가장 강한 증거

## 힌트

1. `ET.fromstring(text).iter("testcase")`는 루트 종류와 상관없이 모든 하위 testcase를 순회한다. suite의 `tests=` 속성을 믿지 말고 직접 센다.
2. `defaultdict(lambda: defaultdict(set))`로 `{test: {commit: {results}}}`를 만들면 "같은 커밋에 결과 2종" 체크가 `len(set) > 1` 한 줄이다.
3. 커밋 순 결과열에서 `sum(a != b for a, b in zip(r, r[1:]))`가 뒤집힘 횟수다. 0이면 stable/failing, 1이면 regression/fixed, 2 이상이면 flaky.

## 풀이 해설

- **parse**: `find("failure")`는 없으면 `None`이다. `if elem:`으로 쓰면 **자식이 없는 Element는 False로 평가**되는 함정이 있다 → 반드시 `is not None`. (면접에서 이걸 말하면 Python을 실제로 써 본 티가 난다.)
- **classify**: 두 단계. 먼저 같은 커밋 안의 불일치(확실한 flaky)를 거르고, 그다음 커밋 순서의 패턴을 본다.
- **bisect**: 불변식은 "good은 항상 good, bad는 항상 bad". `mid`를 돌려서 경계를 좁힌다. 1000커밋이면 10번이다. HIL 1회가 10분이면 선형 탐색은 166시간, bisect는 100분이다. `git bisect run ./run_hil_smoke.sh`로 자동화할 수 있다.
- **복잡도**: parse O(케이스 수), classify O(n log n) (커밋 정렬), bisect O(log n)회 호출.
- **흔한 실수**: `if case.find("failure"):` / suite 속성 합산(중첩 suite에서 이중 계산) / flaky를 "최근 N번 중 한 번이라도 실패"로 정의해서 진짜 regression을 flaky로 묻어 버리기.

## 말하면서 풀기

- "I iterate over every testcase element instead of trusting the suite-level counters, because nested suites can double count."
- "I separate failure from error: a failure is an assertion — possibly a product bug — while an error means the test itself crashed, which on a HIL rig is often the rig: a missing serial port, a hung power relay."
- "A test that both passes and fails on the same commit is flaky by definition — the code didn't change, so the nondeterminism is in the test or the rig."
- "If each HIL run takes ten minutes, I bisect: the first bad commit in a thousand commits costs about ten runs. `git bisect run` can drive the rig script automatically."

## 꼬리 질문

- flaky 테스트를 발견하면 어떻게 처리하나? → 즉시 삭제하지 않는다. **quarantine**(별도 job, `allow_failure: true`)으로 옮겨 머지를 막지 않게 하고, 티켓을 만들고, 재현율을 측정한다. 원인은 보통 타이밍(고정 sleep), 공유 상태(이전 테스트가 남긴 설정), 리그(USB 재열거, 전원), RF 환경.
- retry를 걸면 되지 않나? → retry는 증상을 숨긴다. 걸더라도 "retry 후 통과"를 별도로 기록해서 flaky 지표로 쓴다.
- JUnit XML이 수백 MB라면? → `ET.iterparse`로 스트리밍하고 처리한 element는 `elem.clear()`.
- regression인데 bisect 중간 커밋이 빌드가 안 되면? → `git bisect skip`.
- Don 경험 연결: 챔버 테스트 플랫폼에서 여러 eSSD를 동시에 돌리고 상태를 모니터링했던 경험 → "fail이 DUT 문제인지 챔버/슬롯 문제인지 가르는 것"이 같은 문제다. (Don: 실제로 슬롯/지그 문제를 가려낸 사례가 있으면 채우기)

## 파일

- [starter](../starters/07_ci_results_analyzer.py) · [모범답안](../solutions/07_ci_results_analyzer.py)
- 채점: `python3 python/run.py 07` (내 풀이) · `python3 python/run.py 07 --sol` (답안)
