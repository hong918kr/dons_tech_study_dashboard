"""03 · 시리얼 센서 드라이버를 하드웨어 없이 테스트하기 (unittest.mock) — 모범답안
# mutants: sensor_driver

피검 코드: lib/sensor_driver.py (load("sensor_driver")).

보여 주려는 것
- Mock 으로 serial 포트 흉내: readline.side_effect 에 응답 '대본'을 리스트로
- 호출 검증: write.call_args_list == [call(b"TEMP?\\n"), ...], assert_not_called
- sleep 주입 → 테스트는 0초, 백오프 값까지 검증
- 순서 검증: Mock 하나(manager)에 여러 메서드를 붙여 mock_calls 로 '비우고 → 쓰고 → 읽기' 순서 확인
- parametrize 로 정상 값 · 경계값 · 잘못된 응답 표
"""
from unittest.mock import Mock, call

import pytest

from lib import load

sd = load("sensor_driver")


# ------------------------------------------------------------------ fixtures
@pytest.fixture
def port():
    p = Mock(spec=["write", "readline", "reset_input_buffer", "close"])
    p.readline.return_value = b"TEMP=25.00\r\n"
    return p


@pytest.fixture
def sleep():
    return Mock()


@pytest.fixture
def drv(port, sleep):
    return sd.SensorDriver(port, retries=2, backoff=0.1, sleep=sleep)


# ------------------------------------------------------------------ 정상 경로 · 파싱
@pytest.mark.parametrize("raw, expected", [
    (b"TEMP=23.50\r\n", 23.5),
    (b"TEMP=-12.25\r\n", -12.25),
    (b"TEMP=0\r\n", 0.0),
    (b"TEMP=-40.0\r\n", -40.0),          # 경계값 포함
    (b"TEMP=125.0\r\n", 125.0),
])
def test_read_temperature_parses(drv, port, raw, expected):
    port.readline.return_value = raw
    assert drv.read_temperature() == pytest.approx(expected)
    port.write.assert_called_once_with(b"TEMP?\n")           # 프로토콜 바이트까지 정확히


@pytest.mark.parametrize("raw", [b"TEMP=125.1\r\n", b"TEMP=-40.5\r\n", b"TEMP=999\r\n"])
def test_out_of_range_is_error(drv, port, raw):
    port.readline.return_value = raw
    with pytest.raises(sd.SensorError, match="out of range"):
        drv.read_temperature()


@pytest.mark.parametrize("raw", [b"TEMP=\r\n", b"HELLO\r\n", b"TEMP=abc\r\n"])
def test_malformed_is_error(drv, port, raw):
    port.readline.return_value = raw
    with pytest.raises(sd.SensorError):
        drv.read_temperature()


# ------------------------------------------------------------------ 재시도 · 백오프
def test_retries_after_timeout_then_succeeds(drv, port, sleep):
    port.readline.side_effect = [b"", b"", b"TEMP=21.00\r\n"]
    assert drv.read_temperature() == 21.0
    assert port.write.call_args_list == [call(b"TEMP?\n")] * 3
    assert sleep.call_args_list == [call(pytest.approx(0.1)), call(pytest.approx(0.2))]


def test_gives_up_after_retries(drv, port, sleep):
    port.readline.return_value = b""
    with pytest.raises(sd.SensorTimeout):
        drv.read_temperature()
    assert port.write.call_count == 3                       # 1번 + 재시도 2번
    assert sleep.call_count == 2


def test_no_sleep_on_first_try(drv, sleep):
    drv.read_temperature()
    sleep.assert_not_called()


def test_sensor_error_is_not_retried(drv, port, sleep):
    port.readline.return_value = b"ERR 3\r\n"
    with pytest.raises(sd.SensorError, match="ERR 3"):
        drv.read_temperature()
    assert port.write.call_count == 1
    sleep.assert_not_called()


def test_timeout_is_a_sensor_error():
    assert issubclass(sd.SensorTimeout, sd.SensorError)    # 호출자가 하나로 잡을 수 있게


def test_flushes_input_before_every_write(port, sleep):
    """순서 검증: Mock 하나에 붙이면 mock_calls 에 호출 순서가 남는다."""
    manager = Mock()
    manager.attach_mock(port.reset_input_buffer, "flush")
    manager.attach_mock(port.write, "write")
    manager.attach_mock(port.readline, "readline")
    port.readline.side_effect = [b"", b"TEMP=20.0\r\n"]
    sd.SensorDriver(port, sleep=sleep).read_temperature()
    names = [c[0] for c in manager.mock_calls]
    assert names == ["flush", "write", "readline"] * 2


# ------------------------------------------------------------------ set_rate
@pytest.mark.parametrize("hz", [1, 10, 50, 100])
def test_set_rate_valid(drv, port, hz):
    port.readline.return_value = b"OK\r\n"
    drv.set_rate(hz)
    port.write.assert_called_once_with(b"RATE %d\n" % hz)


@pytest.mark.parametrize("hz", [0, 5, 200, -10])
def test_set_rate_invalid_never_touches_the_wire(drv, port, hz):
    with pytest.raises(ValueError):
        drv.set_rate(hz)
    port.write.assert_not_called()


def test_set_rate_rejected_by_device(drv, port):
    port.readline.return_value = b"NAK\r\n"
    with pytest.raises(sd.SensorError):
        drv.set_rate(10)


# ------------------------------------------------------------------ 자원 정리
def test_context_manager_closes_port(port, sleep):
    with sd.SensorDriver(port, sleep=sleep) as d:
        d.read_temperature()
    port.close.assert_called_once()


def test_context_manager_closes_port_on_exception(port, sleep):
    port.readline.return_value = b"ERR 1\r\n"
    with pytest.raises(sd.SensorError):
        with sd.SensorDriver(port, sleep=sleep) as d:
            d.read_temperature()
    port.close.assert_called_once()
