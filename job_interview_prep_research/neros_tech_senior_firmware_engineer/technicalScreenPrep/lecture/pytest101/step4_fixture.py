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
