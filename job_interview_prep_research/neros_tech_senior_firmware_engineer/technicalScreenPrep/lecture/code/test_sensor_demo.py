# --- tests ---
from unittest.mock import Mock, call

import pytest

from sensor_demo import SensorTimeout, read_temperature


@pytest.fixture
def port():
    return Mock(spec=["write", "readline"])   # 하드웨어 대신: 호출을 기록하는 가짜 포트


def test_reads_value(port):
    port.readline.return_value = b"TEMP=23.5\r\n"
    assert read_temperature(port, sleep=Mock()) == 23.5
    port.write.assert_called_once_with(b"TEMP?\n")       # 선로에 나간 바이트까지 검사


def test_retries_then_succeeds(port):
    port.readline.side_effect = [b"", b"", b"TEMP=21.0\r\n"]   # 호출마다 다음 응답
    sleep = Mock()
    assert read_temperature(port, sleep=sleep) == 21.0
    assert port.write.call_count == 3
    assert sleep.call_args_list == [call(0.1), call(0.2)]       # 백오프 값까지


def test_gives_up(port):
    port.readline.return_value = b""
    with pytest.raises(SensorTimeout):
        read_temperature(port, sleep=Mock())
# --- end ---
