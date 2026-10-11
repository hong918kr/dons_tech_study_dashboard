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
