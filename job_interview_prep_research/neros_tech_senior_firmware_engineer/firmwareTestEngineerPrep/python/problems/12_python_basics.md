# 🐍 12 · Python 기초 몸풀기 12문제

> 인터뷰 직전 손풀기. 문제당 3~5분, 전부 합쳐 40분 안쪽. 문자열 · 리스트 · dict · bytes/hex · 비트 연산 · 작은 클래스(링 버퍼)까지. 막히면 바로 아래 힌트를 보고, 손이 기억하게 한 번 더 친다.

## 왜 나오나

- 라이브 코딩 첫 문제는 대개 **몸풀기 수준**이다. 여기서 막히면 뒤가 흔들린다
- 테스트 자동화 코드의 80%는 이런 기본기다: 문자열 파싱, dict로 집계, bytes ↔ hex, 체크섬, 비트 확인
- C 개발자가 Python에서 자주 막히는 지점(슬라이싱, comprehension, `Counter`, f-string 포맷, 정수 무한 크기)을 골고루 넣었다

## 문제

| # | 함수 | 입력 → 출력 | 시간 |
|---|---|---|---|
| 1 | `count_words(text)` | `"Hi hi there"` → `{"hi": 2, "there": 1}` | 3분 |
| 2 | `is_palindrome(s)` | `"A man, a plan, a canal: Panama"` → `True` (영숫자만, 대소문자 무시) | 3분 |
| 3 | `find_duplicates(items)` | `[3, 1, 3, 2, 1]` → `[1, 3]` (정렬) | 3분 |
| 4 | `flatten(nested)` | `[[1, 2], [3], []]` → `[1, 2, 3]` (한 단계) | 2분 |
| 5 | `second_largest(nums)` | `[5, 1, 5, 3]` → `3`, 없으면 `None` (정렬 없이 한 번 순회) | 5분 |
| 6 | `hex_dump(data)` | `b"\x01\xa2\xff"` → `"01 A2 FF"` | 2분 |
| 7 | `parse_hex(text)` | `"01 a2  ff"` → `b"\x01\xa2\xff"` | 2분 |
| 8 | `checksums(data)` | `b"\x01\x02\xff"` → `(0xFC, 0x02)` = (XOR8, SUM8) | 3분 |
| 9 | `bit_tools(n, k)` | popcount, 2의 거듭제곱 여부, k번 비트 get/set/clear/toggle을 dict로 | 5분 |
| 10 | `parse_kv(text)` | `"a=1, b = 2,name=imu"` → `{"a": 1, "b": 2, "name": "imu"}` (숫자면 int) | 4분 |
| 11 | `voltage_stats(lines)` | CSV `"time,voltage"` 줄들 → `(min, max, avg)`. 헤더·깨진 줄은 건너뜀 | 5분 |
| 12 | `class RingBuffer` | 고정 용량 FIFO. 가득 차면 가장 오래된 값을 덮어쓰고 `dropped`를 센다. `pop()`이 비었으면 `IndexError` | 8분 |

## 순서 추천

- **시간이 15분뿐이면**: 6 → 7 → 8 → 9 → 12 (bytes, 비트, 링 버퍼 = 임베디드와 Python의 교집합)
- **30분 이상이면**: 1부터 차례로

## 힌트

1. `text.lower().split()` 다음 `Counter`. `dict(Counter(...))`로 돌려준다
2. `[c.lower() for c in s if c.isalnum()]`를 만들고 `[::-1]`과 비교한다
3. `Counter(items).items()`에서 `v > 1`만 골라 `sorted`
4. 이중 comprehension: `[x for sub in nested for x in sub]`. 순서는 바깥 for가 먼저다
5. `first`, `second`를 `None`으로 시작한다. 새 최댓값이면 `first, second = n, first`. **같은 값이 두 번 나올 때**를 조심한다
6. `f"{b:02X}"`. bytes를 순회하면 **int가 나온다** (C의 `uint8_t`처럼)
7. `int(tok, 16)`을 모아서 `bytes(...)`. `split()`은 인자 없이 쓰면 여러 칸 공백을 알아서 처리한다
8. XOR은 `^=` 누적, SUM8은 `sum(data) & 0xFF`. **Python int는 오버플로가 없어서** 마스킹을 직접 해야 한다
9. `bin(n).count("1")`, `n & (n - 1) == 0` (단 `n > 0`), `(n >> k) & 1`, `n | (1 << k)`, `n & ~(1 << k)`, `n ^ (1 << k)`
10. `split(",")` → 각 조각에서 `split("=", 1)` → `strip()`. 숫자 판정은 `val.lstrip("-").isdigit()`. `=`이 없는 조각은 건너뛴다
11. `try: float(...) except ValueError: continue`. 유효값이 없으면 `None`. avg는 `round(..., 2)`
12. 리스트 + `head` + `count`. tail 위치는 `(head + count) % capacity`. 가득 찼을 때 push하면 **head를 한 칸 민다**

## 풀이 해설 — 흔한 실수

| 문제 | 실수 | 바른 방법 |
|---|---|---|
| 5 | `sorted(set(nums))[-2]` | 동작은 하지만 O(n log n)이고, 원소가 하나뿐이면 IndexError. 면접관은 한 번 순회를 원한다 |
| 6 | `hex(b)` 사용 | `0x1`처럼 나와서 자릿수가 안 맞는다. `02X` 포맷을 쓴다 |
| 8 | `~` 결과를 마스킹 안 함 | Python에서 `~0x0F`는 `-16`이다. 8비트가 필요하면 `& 0xFF` |
| 9 | `n == 0`일 때 True가 나옴 | `0 & -1 == 0`이라 0도 거듭제곱으로 판정된다. `n > 0 and ...`을 앞에 붙인다 |
| 10 | `int(val)`을 try 없이 | `"imu"`에서 ValueError. 판정 후 변환하거나 try/except |
| 12 | 가득 찼을 때 count를 또 증가 | count는 capacity에서 멈추고 head가 움직인다 |

- **연산자 우선순위는 C와 Python이 반대다**: C에서 `n & (n - 1) == 0`은 `n & ((n - 1) == 0)`으로 해석되는 유명한 버그다. Python은 비트 연산자가 비교보다 먼저 계산돼서 의도대로 동작한다. 그래도 두 언어를 오가니까 **비트 연산에는 항상 괄호를 친다**. 면접에서 이 차이를 말하면 C를 아는 사람이라는 신호가 된다

## 말하면서 풀기

> "Let me restate the input and output first, and check an edge case — empty input."

> "In Python, iterating over bytes gives me ints, so I can format each one with 02X."

> "Python ints don't overflow, so for an 8-bit checksum I mask with 0xFF explicitly."

> "I'll keep a head index and a count rather than head and tail, so full and empty aren't ambiguous."

> "Let me add one quick test for the edge case before I call it done."

## 꼬리 질문

- 12번을 `collections.deque(maxlen=n)`으로 하면? → 한 줄로 끝난다: `deque(maxlen=3)`은 가득 차면 반대쪽 끝을 버린다. 실무에서는 deque를 쓰고, 인터뷰에서는 직접 구현을 보여 준 다음 "실제로는 deque를 쓰겠다"고 말한다
- 12번을 스레드 두 개(시리얼 읽기 스레드, 테스트 스레드)가 같이 쓰면? → `threading.Lock`으로 감싸거나 `queue.Queue`를 쓴다 ([N03](../../notes/2026-09-28_N03_python_for_test_automation.md))
- 11번 파일이 10 GB라면? → 리스트에 모으지 말고 한 줄씩 읽으면서 min, max, 합계, 개수만 유지한다 (generator)
- 1번에서 상위 3개 단어만? → `Counter.most_common(3)`

## 파일

- [starter](../starters/12_python_basics.py) · [모범답안](../solutions/12_python_basics.py)
- 채점: `python3 python/run.py 12` (내 풀이) · `python3 python/run.py 12 --sol` (모범답안)
- 다음 단계: [문제 10 워밍업](10_python_warmups.md) (two_sum, 괄호 검사, 구간 병합 등 LeetCode easy급)
