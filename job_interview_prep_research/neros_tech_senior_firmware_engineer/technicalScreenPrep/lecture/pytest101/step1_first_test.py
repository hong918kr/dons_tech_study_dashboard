# 1단계: 테스트 = "test_ 로 시작하는 함수" + 그 안의 assert

def add(a, b):              # 테스트할 대상 (내가 만든 코드)
    return a + b


def test_add():             # 테스트 함수: 이름이 test_ 로 시작하면 pytest 가 알아서 찾아 실행
    assert add(2, 3) == 5   # assert 조건 — 참이면 통과, 거짓이면 실패
    assert add(-1, 1) == 0
