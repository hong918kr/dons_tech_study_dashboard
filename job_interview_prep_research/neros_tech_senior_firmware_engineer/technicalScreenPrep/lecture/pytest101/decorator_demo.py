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
