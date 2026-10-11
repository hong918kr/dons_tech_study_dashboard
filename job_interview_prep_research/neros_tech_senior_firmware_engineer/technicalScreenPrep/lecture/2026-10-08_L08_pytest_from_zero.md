# 🐣 L08 · pytest 처음부터 — 무엇이 어떤 역할인지 아주 쉽게

> pytest를 **처음 보는 사람 기준**으로, 단어 뜻(fixture · parametrize · `@`)부터 시작해서 6단계로 하나씩 쌓는다. 각 단계는 **아주 작은 예제 파일 하나 + 그 파일을 실제로 돌렸을 때 화면에 나온 것** 그대로다 (`lecture/pytest101/`, `bash lecture/pytest101/run_examples.sh`로 다시 돌릴 수 있다). 다 읽는 데 20분이면 된다.

## 0. 한 문장으로

> **pytest = "test_로 시작하는 함수를 전부 찾아서 실행하고, 그 안의 `assert`가 참인지 확인해 결과를 알려 주는 프로그램"**

비유하면:

| 실제 세계 | pytest |
|---|---|
| 검사할 제품 | 내가 만든 함수 · 클래스 (예: `RingBuffer`) |
| 검사 항목 하나 | 테스트 함수 하나 (`def test_…():`) |
| "이 값이어야 한다" 판정 | `assert 결과 == 기대값` |
| 검사관 | pytest — 검사 항목을 **알아서 찾아서** 전부 돌리고 합격 · 불합격을 보고 |
| 검사 전에 지그(fixture)에 제품 올리기, 끝나면 내리기 | **fixture** — 준비와 정리 |
| 같은 검사를 여러 입력으로 | **parametrize** |
| 진짜 장비 대신 쓰는 가짜 장비 | **Mock** |

## 1. 전체 그림 — 단어 6개

| 단어 | 역할 | 생김새 |
|---|---|---|
| **테스트 함수** | 검사 하나 | `def test_add():` — 이름이 **`test_`로 시작**해야 pytest가 찾는다 |
| **assert** | 판정 | `assert add(2, 3) == 5` — 참이면 통과, 거짓이면 실패 |
| **pytest.raises** | "에러가 나야 정상"인 검사 | `with pytest.raises(IndexError):` |
| **parametrize** | 같은 검사를 입력만 바꿔 여러 번 | `@pytest.mark.parametrize("n, expected", [...])` |
| **fixture** | 테스트 전 준비 · 후 정리 | `@pytest.fixture` 붙인 함수, 테스트는 **인자 이름**으로 받는다 |
| **Mock** | 가짜 장치 · 가짜 함수 | `Mock()` — 정해 준 대답을 하고, 어떻게 불렸는지 기록 |

실행 방법은 하나다:

```bash
pytest 파일이름.py          # 그 파일의 test_ 함수를 전부 실행
pytest -v 파일이름.py       # -v: 테스트 이름을 하나씩 보여 줌 (verbose)
pytest -s 파일이름.py       # -s: print 출력도 보여 줌
pytest -k fifo 파일이름.py  # 이름에 'fifo' 가 들어간 테스트만
```

## 1.5 단어 뜻부터 — fixture · parametrize · `@`

### fixture = "고정 장치", 테스트가 올라가는 받침대

- 영어 **fixture**는 원래 **"고정해 둔 장치 · 설비"**라는 뜻이다. 공장 테스트에서 보드를 꽂아 고정하는 **지그 · 픽스처**(bed-of-nails, pogo-pin fixture)가 바로 그 말이다
- 소프트웨어 테스트에서는 **"테스트가 시작될 때 항상 똑같이 갖춰져 있어야 하는 상태"**를 말한다 — 빈 버퍼, 열린 포트, 전원이 켜진 리그, 임시 폴더
- 그래서 pytest의 fixture 함수가 하는 일은 공장의 지그와 같다:

| 공장 지그 | pytest fixture |
|---|---|
| 보드를 지그에 꽂고 고정 | `yield` **앞**: 준비 (버퍼 만들기, 포트 열기, 리그 전원 켜기) |
| 검사 실행 | `yield` 자리에서 **테스트 함수가 실행**됨 |
| 검사 끝나면 보드 빼고 지그 원위치 | `yield` **뒤**: 정리 (포트 닫기, 전원 끄기) — 테스트가 실패해도 실행 |
| 보드마다 같은 지그 상태에서 시작 | 테스트마다 **새로** 준비 → 앞 테스트의 흔적이 안 남음 |

> 한 문장: **fixture = 테스트가 올라가는 '항상 같은 출발 상태'를 만들어 주고, 끝나면 치워 주는 함수.**

### parametrize = "매개변수화", 값을 빈칸으로 바꾸기

- **parameter** = 매개변수 (함수의 입력 칸). **parametrize** = 고정된 값을 **매개변수(빈칸)로 바꿔서 여러 값을 넣을 수 있게 한다**는 뜻
- 매개변수화하기 전 — 값이 박혀 있다:

```python
def test_is_even_2():
    assert is_even(2) == True     # 2 하나만 검사
```

- 매개변수화한 뒤 — `n`, `expected`가 빈칸이 되고, 표의 줄마다 채워서 실행된다:

```python
@pytest.mark.parametrize("n, expected", [(0, True), (1, False), (2, True)])
def test_is_even(n, expected):    # 빈칸 두 개
    assert is_even(n) == expected # → 표 3줄 = 테스트 3개
```

> 한 문장: **parametrize = 같은 검사를 '입력 칸'으로 만들어 두고, 표의 줄마다 한 번씩 돌리는 것.**

### `@` = 데코레이터, 함수 위에 붙이는 "꼬리표 / 포장지"

- `@무엇` 은 **바로 아래 함수를 그 '무엇'에 한 번 통과시키라**는 Python 문법이다. 아래 두 코드는 **완전히 같다**:

```python
@shout
def greet():
    return "hello"
```

```python
def greet():
    return "hello"
greet = shout(greet)     # @shout 는 이 한 줄을 짧게 쓴 것
```

- 데코레이터가 하는 일은 두 종류다: ① 함수를 **감싸서 동작을 바꾸거나**(위의 `shout`), ② 함수는 그대로 두고 **꼬리표만 붙이거나 명단에 등록**한다
- pytest의 `@`는 **②번 — 꼬리표**다:

| 붙인 것 | pytest에게 하는 말 |
|---|---|
| `@pytest.fixture` | "이 함수는 **테스트가 아니라 준비물(fixture)**이야. 누가 이 이름을 인자로 달라고 하면 실행해서 넣어 줘" |
| `@pytest.mark.parametrize("n, expected", [...])` | "이 테스트에는 **입력 표**가 붙어 있어. 줄마다 한 번씩 돌려 줘" |
| `@pytest.mark.skip(reason=...)` | "이건 건너뛰어" |

- pytest는 테스트를 모을 때 각 함수에 **어떤 꼬리표가 붙었는지 읽고** 그에 맞게 실행한다. 그래서 `@`를 안 붙이면 fixture가 그냥 평범한 함수가 되고, 테스트가 인자로 달라고 해도 pytest가 못 찾는다

직접 돌려서 확인한 것 (`python decorator_demo.py`):

```python
# @ (데코레이터) 가 하는 일을 눈으로 보기 — python 으로 그냥 실행한다 (pytest 아님)
import pytest


# 1) 데코레이터 = "함수를 받아서 (뭔가 하고) 함수를 돌려주는 함수"
def shout(func):
    def wrapper():
        return func().upper() + "!"
    return wrapper


@shout                      # 이 한 줄은 아래 def 가 끝난 뒤  greet = shout(greet)  와 똑같다
def greet():
    return "hello"


def greet2():
    return "hello"


greet2 = shout(greet2)      # @ 없이 같은 일을 손으로


print("1) greet()  =", greet())
print("   greet2() =", greet2())


# 2) 함수를 '바꾸지 않고' 꼬리표(표시)만 붙이는 데코레이터도 있다
REGISTRY = []


def register(func):
    REGISTRY.append(func.__name__)   # 명단에 이름만 적고
    return func                      # 함수는 그대로 돌려준다


@register
def test_a():
    pass


print("2) registry =", REGISTRY)


# 3) pytest 의 @ 도 같은 원리 — '이 함수는 fixture 다', '이 함수엔 입력 표가 있다' 라는 꼬리표를 붙인다
@pytest.fixture
def rb():
    return []


@pytest.mark.parametrize("n, expected", [(0, True), (1, False)])
def test_is_even(n, expected):
    assert (n % 2 == 0) == expected


mark = test_is_even.pytestmark[0]
print("3) test_is_even 에 붙은 꼬리표:", mark.name, mark.args)
print("   rb 는 이제 pytest 가 관리하는 fixture 객체:", type(rb).__name__)
```

```text
$ python decorator_demo.py
1) greet()  = HELLO!
   greet2() = HELLO!
2) registry = ['test_a']
3) test_is_even 에 붙은 꼬리표: parametrize ('n, expected', [(0, True), (1, False)])
   rb 는 이제 pytest 가 관리하는 fixture 객체: FixtureFunctionDefinition
```

- 1) `@shout` 과 `greet2 = shout(greet2)` 결과가 같다 → `@`는 줄임말
- 2) `@register` 는 함수를 바꾸지 않고 명단에만 적었다 → 꼬리표형 데코레이터
- 3) `@pytest.mark.parametrize` 를 붙이자 `test_is_even` 에 **`parametrize` 꼬리표와 입력 표가 붙었다**. `@pytest.fixture` 를 붙인 `rb` 는 pytest가 관리하는 **fixture 객체**로 바뀌었다 → pytest가 이걸 보고 동작한다

| 헷갈리는 질문 | 답 |
|---|---|
| `@`는 pytest 전용 문법인가? | 아니다. **Python 문법**이다. `@property`, `@dataclass`, `@staticmethod`도 같은 것 ([L01 9 · 10절](2026-10-04_L01_python_syntax.md)) |
| `@pytest.fixture` 뒤에 괄호를 붙여도 되나? | 된다. `@pytest.fixture` / `@pytest.fixture()` 둘 다 OK. 옵션을 줄 땐 `@pytest.fixture(scope="session")` |
| `@pytest.mark.parametrize`에서 첫 인자가 문자열인 이유? | 테스트 함수의 **인자 이름**을 알려 주는 것. `"n, expected"` ↔ `def test_is_even(n, expected)` 이름이 같아야 한다 |
| fixture와 parametrize를 같이 쓸 수 있나? | 된다. `def test_wraparound(rb, laps):` — `rb`는 fixture에서, `laps`는 표에서 (7절 예제) |

## 2. 1단계 — 첫 테스트: 함수 + assert

```python
# 1단계: 테스트 = "test_ 로 시작하는 함수" + 그 안의 assert

def add(a, b):              # 테스트할 대상 (내가 만든 코드)
    return a + b


def test_add():             # 테스트 함수: 이름이 test_ 로 시작하면 pytest 가 알아서 찾아 실행
    assert add(2, 3) == 5   # assert 조건 — 참이면 통과, 거짓이면 실패
    assert add(-1, 1) == 0
```

실행하면:

```text
$ pytest step1_first_test.py
============================= test session starts ==============================
collected 1 item

step1_first_test.py .                                                    [100%]

============================== 1 passed in 0.01s ===============================
```

출력 읽는 법:

| 출력 | 뜻 |
|---|---|
| `collected 1 item` | test_ 함수를 1개 찾았다 |
| `step1_first_test.py .` | 점(`.`) 하나 = 테스트 하나 **통과** |
| `F` | 실패 (Fail) |
| `E` | 테스트 코드 자체가 에러로 죽음 (Error) |
| `s` | 건너뜀 (skip) |
| `1 passed` | 요약 |

`-v`를 붙이면 이름이 보인다:

```text
$ pytest -v step1_first_test.py
============================= test session starts ==============================
collecting ... collected 1 item

step1_first_test.py::test_add PASSED                                     [100%]

============================== 1 passed in 0.01s ===============================
```

### 실패하면 어떻게 보이나 (가장 중요)

```python
# 1단계-b: 일부러 틀린 테스트 — 실패하면 pytest 가 무엇을 보여 주나

def add(a, b):
    return a + b


def test_add_wrong_expectation():
    assert add(2, 3) == 6   # 틀린 기대값 → 실패
```

```text
$ pytest step1b_failing.py
============================= test session starts ==============================
collected 1 item

step1b_failing.py F                                                      [100%]

=================================== FAILURES ===================================
__________________________ test_add_wrong_expectation __________________________

    def test_add_wrong_expectation():
>       assert add(2, 3) == 6   # 틀린 기대값 → 실패
E       assert 5 == 6
E        +  where 5 = add(2, 3)

step1b_failing.py:8: AssertionError
=========================== short test summary info ============================
FAILED step1b_failing.py::test_add_wrong_expectation - assert 5 == 6
============================== 1 failed in 0.01s ===============================
```

- `>` 표시 줄 = 실패한 assert
- `E  assert 5 == 6` = **실제 값 5, 기대 값 6** — pytest가 `add(2, 3)`이 5였다는 걸 알아서 펼쳐 보여 준다
- 그래서 `print`를 찍지 않아도 왜 틀렸는지 바로 보인다. 이것이 pytest가 그냥 `assert`보다 좋은 이유

## 3. 2단계 — "에러가 나야 맞는" 검사: pytest.raises

```python
# 2단계: "에러가 나야 맞는" 경우 — pytest.raises
import pytest


def pop_first(items):
    if not items:
        raise IndexError("empty")
    return items.pop(0)


def test_pop_first_returns_oldest():
    assert pop_first([10, 20]) == 10


def test_pop_first_on_empty_raises():
    with pytest.raises(IndexError):      # 이 블록 안에서 IndexError 가 '나야' 통과
        pop_first([])
```

```text
$ pytest -v step2_raises.py
============================= test session starts ==============================
collecting ... collected 2 items

step2_raises.py::test_pop_first_returns_oldest PASSED                    [ 50%]
step2_raises.py::test_pop_first_on_empty_raises PASSED                   [100%]

============================== 2 passed in 0.01s ===============================
```

- `with pytest.raises(IndexError):` 블록 안에서 **IndexError가 나면 통과**, 안 나면 실패
- 쓰는 곳: 빈 버퍼에서 pop, 잘못된 입력, 타임아웃 같은 **에러 경로**

## 4. 3단계 — 입력만 바꿔 여러 번: parametrize

```python
# 3단계: 같은 테스트를 입력만 바꿔 여러 번 — @pytest.mark.parametrize
import pytest


def is_even(n):
    return n % 2 == 0


@pytest.mark.parametrize("n, expected", [   # (입력, 기대값) 표
    (0, True),
    (1, False),
    (2, True),
    (-3, False),
])
def test_is_even(n, expected):              # 표의 한 줄마다 한 번씩 실행된다 (4번)
    assert is_even(n) == expected
```

```text
$ pytest -v step3_parametrize.py
============================= test session starts ==============================
collecting ... collected 4 items

step3_parametrize.py::test_is_even[0-True] PASSED                        [ 25%]
step3_parametrize.py::test_is_even[1-False] PASSED                       [ 50%]
step3_parametrize.py::test_is_even[2-True] PASSED                        [ 75%]
step3_parametrize.py::test_is_even[-3-False] PASSED                      [100%]

============================== 4 passed in 0.01s ===============================
```

- 위 표의 4줄이 **테스트 4개**가 된다: `test_is_even[0-True]`, `[1-False]` …
- for 문으로 돌리는 것과 차이: 하나가 실패해도 **나머지는 계속 돌고, 어느 입력이 틀렸는지 이름으로 보인다**

## 5. 4단계 — 준비와 정리: fixture

```python
# 4단계: fixture = 테스트 전에 '준비', 테스트 후에 '정리'를 해 주는 함수
import pytest


@pytest.fixture
def port():                      # 이름이 'port' 인 준비물
    print("\n  [준비] 포트 열기")
    p = {"open": True, "sent": []}
    yield p                      # ← 여기서 테스트가 실행된다 (테스트는 p 를 받는다)
    p["open"] = False
    print("  [정리] 포트 닫기")    # 테스트가 실패해도 반드시 실행


def test_send(port):             # 인자 이름을 'port' 라고 쓰기만 하면 pytest 가 fixture 를 꽂아 준다
    port["sent"].append(b"PING")
    assert port["sent"] == [b"PING"]


def test_starts_open(port):      # 테스트마다 '새' 준비물 — 앞 테스트의 PING 이 남아 있지 않다
    assert port["open"] and port["sent"] == []
```

`-s`로 print까지 보면 **순서**가 보인다:

```text
$ pytest -v -s step4_fixture.py
============================= test session starts ==============================
collecting ... collected 2 items

step4_fixture.py::test_send 
  [준비] 포트 열기
PASSED  [정리] 포트 닫기

step4_fixture.py::test_starts_open 
  [준비] 포트 열기
PASSED  [정리] 포트 닫기


============================== 2 passed in 0.01s ===============================
```

이해 포인트 세 개:

| 포인트 | 설명 |
|---|---|
| 어떻게 연결되나 | 테스트 함수 인자에 **fixture 이름(`port`)을 쓰기만 하면** pytest가 그 fixture를 실행해서 결과를 넣어 준다. 직접 부르지 않는다 |
| `yield` | `yield` **앞 = 준비**, **뒤 = 정리**. 테스트는 그 사이에 실행된다. 테스트가 실패해도 정리는 실행된다 |
| 테스트마다 새로 | 기본은 테스트 하나마다 새 준비물 → 앞 테스트의 흔적이 다음 테스트에 안 남는다 (`test_starts_open`이 통과한 이유) |

- 실무 연결: 리그 열기 · DUT 리셋 · 끝나면 **전원 끄기** = fixture. 비싼 준비(리그 연결)는 `@pytest.fixture(scope="session")`로 한 번만
- Python의 `with` 문, C++의 RAII와 같은 생각: **"정리는 반드시 일어난다"**

## 6. 5단계 — 진짜 장치 대신 가짜: Mock

```python
# 5단계: Mock = 진짜 장치 대신 쓰는 '가짜'. 정해 준 대답을 하고, 어떻게 불렸는지 기록한다
from unittest.mock import Mock


def read_temperature(port):              # 테스트 대상: 포트에 질문 보내고 답을 숫자로
    port.write(b"TEMP?\n")
    reply = port.readline()               # 예: b"TEMP=23.5\n"
    return float(reply.split(b"=")[1])


def test_read_temperature():
    fake = Mock()                                    # 가짜 포트 (하드웨어 없음)
    fake.readline.return_value = b"TEMP=23.5\n"      # readline() 이 불리면 이걸 돌려줘라
    assert read_temperature(fake) == 23.5            # 결과 확인
    fake.write.assert_called_once_with(b"TEMP?\n")   # 보낸 바이트도 확인 (한 번, 정확히 이 값)
```

```text
$ pytest -v step5_mock.py
============================= test session starts ==============================
collecting ... collected 1 item

step5_mock.py::test_read_temperature PASSED                              [100%]

============================== 1 passed in 0.01s ===============================
```

| 줄 | 뜻 |
|---|---|
| `fake = Mock()` | 무엇이든 받아 주는 가짜 객체. `fake.write(...)`, `fake.readline()` 다 된다 |
| `fake.readline.return_value = b"TEMP=23.5\n"` | "readline이 불리면 이걸 돌려줘라" = **가짜 장치의 대답 정하기** |
| `fake.write.assert_called_once_with(b"TEMP?\n")` | "write가 **딱 한 번, 정확히 이 바이트로** 불렸나" = **보낸 것 검사** |
| 응답을 여러 개 차례로 | `fake.readline.side_effect = [b"", b"TEMP=21\n"]` — 첫 번째는 무응답, 두 번째는 정상 (재시도 테스트) |

- 왜 쓰나: 하드웨어 없이(CI에서도) 드라이버 로직을 검사하려고. 단, **진짜 장치에서 한 번은** 확인해야 가짜가 현실과 어긋나지 않는다

## 7. 6단계 — 다 합치기: ring buffer 테스트

```python
# 6단계: 다 합치기 — ring buffer 를 fixture · parametrize · raises 로 테스트
import pytest


class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = self.tail = self.count = 0

    def push(self, x):
        if self.count == self.cap:
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def pop(self):
        if self.count == 0:
            raise IndexError("empty")
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x


@pytest.fixture
def rb():                                     # 준비: 용량 3짜리 빈 버퍼
    return RingBuffer(3)


def test_fifo(rb):                            # 넣은 순서대로 나오나
    rb.push(1)
    rb.push(2)
    assert rb.pop() == 1 and rb.pop() == 2


def test_full_rejects(rb):                    # 가득 차면 False
    assert rb.push(1) and rb.push(2) and rb.push(3)
    assert rb.push(4) is False


def test_pop_empty_raises(rb):                # 비었을 때 pop → IndexError
    with pytest.raises(IndexError):
        rb.pop()


@pytest.mark.parametrize("laps", [1, 5, 10])
def test_wraparound(rb, laps):                # 여러 바퀴 돌아도 순서 유지
    rb.push(0)
    for i in range(1, laps + 1):
        rb.push(i)
        assert rb.pop() == i - 1
```

```text
$ pytest -v step6_ring_buffer.py
============================= test session starts ==============================
collecting ... collected 6 items

step6_ring_buffer.py::test_fifo PASSED                                   [ 16%]
step6_ring_buffer.py::test_full_rejects PASSED                           [ 33%]
step6_ring_buffer.py::test_pop_empty_raises PASSED                       [ 50%]
step6_ring_buffer.py::test_wraparound[1] PASSED                          [ 66%]
step6_ring_buffer.py::test_wraparound[5] PASSED                          [ 83%]
step6_ring_buffer.py::test_wraparound[10] PASSED                         [100%]

============================== 6 passed in 0.01s ===============================
```

| 테스트 | 쓴 도구 | 검사하는 것 |
|---|---|---|
| `test_fifo` | fixture `rb` | 넣은 순서대로 나오나 |
| `test_full_rejects` | fixture | 가득 차면 False |
| `test_pop_empty_raises` | fixture + `pytest.raises` | 빈 상태 에러 경로 |
| `test_wraparound[1/5/10]` | fixture + `parametrize` | 여러 바퀴 돌아도 순서 유지 (인덱스가 끝에서 0으로 돌아가는 부분) |

> 면접에서 ring buffer를 만든 직후 이 모양으로 테스트 4~5개를 쓰면 된다. 이게 이번 스크린의 핵심 장면이다 ([L06 시나리오 A](2026-10-05_L06_screen_scenarios.md)).

## 8. 자주 하는 착각

| 착각 | 사실 |
|---|---|
| 테스트 함수를 내가 불러야 한다 | 아니다. pytest가 `test_`로 시작하는 함수를 **찾아서** 부른다 |
| fixture를 `rb()`로 불러야 한다 | 아니다. 테스트 인자에 **이름만** 쓴다 (`def test_fifo(rb):`) |
| assert에 메시지를 꼭 써야 한다 | 아니다. 실패하면 pytest가 실제 값 · 기대 값을 보여 준다 |
| 파일 이름은 아무거나 | `pytest` 만 치면 `test_*.py` (또는 `*_test.py`) 파일을 찾는다. **파일을 직접 지정**하면 이름은 상관없다 |
| unittest를 따로 배워야 한다 | pytest는 unittest로 쓴 테스트도 그대로 돌린다. 시작은 pytest 하나면 된다 |
| pytest가 없으면 못 돌린다 | `if __name__ == "__main__": test_fifo(...)` 처럼 직접 불러도 된다 (단, fixture는 직접 만들어 넘겨야 함) — [L05 형식](2026-10-04_L05_warmup.md) |

## 9. 면접에서 쓸 영어 (짧게)

| 질문 | 답 |
|---|---|
| "Have you used pytest?" | "Yes — plain test functions with asserts, fixtures for setup and cleanup, parametrize for tables of inputs, and unittest.mock to fake hardware." |
| "What's a fixture?" | "A function that prepares something a test needs and cleans it up afterwards. The test just asks for it by name. Code after `yield` runs even if the test fails." |
| "Why parametrize instead of a loop?" | "Each input becomes its own test, so one failure doesn't hide the others, and the report shows exactly which input failed." |
| "Why mock?" | "To test driver logic without hardware — I script the device's replies and check exactly what we sent. Then one real-hardware test keeps the mock honest." |

## 체크

- [ ] 0절 비유 표를 보고 "pytest가 하는 일"을 한 문장으로
- [ ] 1절 단어 6개의 역할을 가리고 말하기
- [ ] 2절 실패 출력에서 "실제 값 · 기대 값"이 어디 있는지 짚기
- [ ] 5절 fixture 출력에서 준비 → 테스트 → 정리 순서 확인
- [ ] 7절 ring buffer 테스트 4개를 빈 파일에 다시 써서 `pytest -v`
