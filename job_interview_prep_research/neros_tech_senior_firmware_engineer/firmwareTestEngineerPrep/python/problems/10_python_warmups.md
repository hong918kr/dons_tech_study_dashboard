# 🐍 10 · Python warm-ups — 5분짜리 클래식 8개

> two sum · 괄호 검사 · 구간 합치기 · top-k · 비트 뒤집기 · moving average generator · 순서 유지 중복 제거 · 패턴 그룹핑. 전부 로그·테스트 맥락으로 바꿨다. dict · set · stack · Counter · heapq · deque · generator를 손에 익히는 용도.

## 왜 나오나

- 리크루터가 "Python을 물어본다"고 했다. 30분 HM 콜에서 라이브 코딩이 나오면 **짧은 클래식 한 문제**일 가능성이 크다 [추정].
- 여기서 보는 건 알고리즘보다 **Python다운 도구 선택**이다. C처럼 인덱스 루프를 돌리는지, `dict`/`Counter`/`deque`/generator를 자연스럽게 쓰는지.
- 각 문제는 "코드 → 복잡도 한 문장 → 엣지 케이스 한 문장" 순서로 말하는 연습을 한다.

## 문제

> "Quick one in Python: …" — 아래 8개 중 하나가 이런 식으로 나온다고 가정하고, 문제당 5~8분 안에 말하면서 푼다.

| # | 함수 | 한 줄 설명 | 핵심 도구 | 복잡도 |
|---|---|---|---|---|
| 1 | `two_sum(nums, target)` | 합이 target인 두 인덱스 | dict (값→인덱스) | O(n) / O(n) |
| 2 | `valid_brackets(cmd)` | 명령 문자열 괄호 짝 검사 | list를 stack으로 | O(n) / O(n) |
| 3 | `merge_intervals(windows)` | fault 구간 합치기 | `sorted` + 마지막 구간 비교 | O(n log n) |
| 4 | `top_k_frequent_errors(codes, k)` | 가장 흔한 에러 코드 k개 | `Counter` + `heapq.nsmallest` | O(n + m log k) |
| 5 | `reverse_bits_32(x)` | 32비트 뒤집기 | `<<`, `>>`, `&` | O(32) |
| 6 | `moving_average(samples, w)` | 이동 평균 generator | `deque` + 누적합 + `yield` | 샘플당 O(1) |
| 7 | `dedupe_keep_order(items)` | 순서 유지 중복 제거 | `set` + list | O(n) |
| 8 | `group_log_keys(keys)` | 숫자 뺀 패턴으로 로그 키 묶기 | `defaultdict(list)` | O(n·L) |

## 예시

```python
two_sum([2, 7, 11, 15], 9)                     # (0, 1)
valid_brackets("set(motor[1], {pwm: 1500})")   # True
merge_intervals([(5, 7), (1, 3), (2, 4), (7, 8)])   # [(1, 4), (5, 8)]
top_k_frequent_errors(["E_RX", "E_GYRO", "E_RX"], 1)  # ["E_RX"]
reverse_bits_32(1)                             # 0x80000000
list(moving_average([1, 2, 3, 4, 5], 3))       # [2.0, 3.0, 4.0]
dedupe_keep_order(["b", "a", "b"])             # ["b", "a"]
group_log_keys(["MOTOR1_FAULT", "MOTOR12_FAULT"])  # {"MOTOR#_FAULT": [...]}
```

## 엣지 케이스

- two_sum: 같은 값 두 개 `[3, 3]` → 넣기 전에 먼저 찾아야 자기 자신과 짝이 안 된다
- brackets: `")"`로 시작 (빈 stack에서 pop), 끝에 남은 `"(("`
- merge: 빈 입력, 완전히 포함되는 구간 `(1,10),(2,3)`, 맞닿는 구간 `(5,7),(7,8)` (합칠지 여부를 먼저 물어볼 것)
- top-k: 동률 순서를 정해야 테스트가 결정적이다 → `key=(-count, code)`
- reverse_bits: Python int는 무한 비트 → 입력을 `& 0xFFFFFFFF`로 자르고 32번만 돈다
- moving_average: generator 안의 `raise`는 **호출할 때가 아니라 첫 `next()` 때** 터진다. 창보다 샘플이 적으면 아무것도 yield 안 함

## 힌트

1. dict/set 조회는 평균 O(1)이다. "이미 본 것"을 기억하는 문제는 대부분 dict/set 하나로 끝난다.
2. "정렬하면 쉬워지나?"를 먼저 묻는다 (merge intervals). 정렬 비용 O(n log n)을 말할 것.
3. 스트림(끝이 없을 수도 있는 입력)은 generator로. `sum(buf)`를 매번 하면 O(w)라서 누적합을 유지한다.

## 풀이 해설

- **two_sum**: `seen[x] = j`를 검사 **뒤**에 넣는 게 포인트. 정렬 + two-pointer는 O(n log n)이고 원래 인덱스를 잃는다.
- **valid_brackets**: 닫는 괄호 → 여는 괄호 dict로 매핑. 마지막에 `not stack`.
- **merge_intervals**: `sorted(windows)`는 튜플을 첫 원소 → 둘째 원소 순으로 정렬한다. 결과를 수정하려고 내부는 list로 만든다.
- **top_k**: `Counter.most_common(k)`도 되지만 동률 순서가 입력 순서라 비결정적이다. `heapq.nsmallest(k, items, key=(-cnt, code))`는 O(m log k).
- **reverse_bits**: C와 같은 루프. Python은 `int(format(x, "032b")[::-1], 2)` 한 줄도 가능하다 — 두 가지를 다 말하면 좋다.
- **moving_average**: `deque.popleft()`는 O(1), `list.pop(0)`은 O(n). 이 차이를 말할 것.
- **dedupe**: 3.7+부터 dict는 삽입 순서를 보장하므로 `list(dict.fromkeys(items))` 한 줄도 된다.
- **group_log_keys**: group anagrams의 변형. 키 정규화 함수를 바꾸면 "같은 종류의 에러"를 묶는 로그 분석 도구가 된다. `re.sub(r"\d+", "#", k)`로 한 줄도 된다.

## 말하면서 풀기

- "Let me clarify the edge cases first — can the list be empty, and should touching intervals be merged?"
- "I'll trade memory for time: a dict of values I've already seen gives me O(n)."
- "Since this is a stream, I'll write it as a generator so it works on a live serial feed without loading everything into memory."
- "deque gives me O(1) pops from the left; list.pop(0) would be O(n)."
- "For deterministic test output I break ties by the error code name."

## 꼬리 질문

- list와 tuple 차이? → mutable vs immutable, tuple은 hashable이라 dict 키/set 원소가 될 수 있다.
- dict 조회가 O(1)인 이유와 최악은? → 해시 테이블. 충돌이 몰리면 O(n).
- generator와 list comprehension 차이? → lazy vs eager. 메모리, 한 번만 순회 가능.
- `is` vs `==`? → identity vs equality. `None` 비교는 `is None`.
- mutable default argument 함정 (`def f(x=[])`)? → 기본값은 정의 시점에 한 번만 만들어진다 → `None`으로 받고 안에서 새로 만든다.
- GIL이 테스트 자동화에 문제가 되나? → I/O 대기(시리얼, 소켓)는 GIL을 놓으므로 threading으로 충분한 경우가 많다. CPU 연산이 무거우면 multiprocessing.

## 파일

- [starter](../starters/10_python_warmups.py) · [모범답안](../solutions/10_python_warmups.py)
- 채점: `python3 python/run.py 10` (내 풀이) · `python3 python/run.py 10 --sol` (답안)
