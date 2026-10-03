"""03 · 시리얼 센서 드라이버를 하드웨어 없이 테스트하기 (unittest.mock) — starter
# mutants: sensor_driver

피검 코드: lib/sensor_driver.py (프로토콜과 동작은 그 파일 docstring 과 problems/03 참고).
채점: python3 python/run.py 03 → 정상 구현 통과 + mutant 9개(lib/mutants/sensor_driver_m*.py) 각각 잡기.
"""
from unittest.mock import Mock, call

import pytest

from lib import load

sd = load("sensor_driver")


# ------------------------------------------------------------------ fixtures (완성)
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


# ------------------------------------------------------------------ 예시 (완성)
def test_read_simple(drv, port):
    assert drv.read_temperature() == 25.0
    port.write.assert_called_once_with(b"TEMP?\n")


# ------------------------------------------------------------------ TODO
@pytest.mark.parametrize("raw, expected", [
    (b"TEMP=23.50\r\n", 23.5),
    # TODO: 음수, 0, 경계값 -40 / 125
])
def test_read_temperature_parses(drv, port, raw, expected):
    pytest.skip("TODO")


def test_out_of_range_is_error(drv, port):
    pytest.skip("TODO: 125.1, -40.5 → SensorError")


def test_retries_after_timeout_then_succeeds(drv, port, sleep):
    pytest.skip("TODO: readline.side_effect = [b'', b'', b'TEMP=21.00\\r\\n'] → write 3번, sleep(0.1), sleep(0.2)")


def test_gives_up_after_retries(drv, port, sleep):
    pytest.skip("TODO: 계속 b'' → SensorTimeout, write 몇 번?")


def test_sensor_error_is_not_retried(drv, port, sleep):
    pytest.skip("TODO: b'ERR 3\\r\\n' → SensorError, write 1번, sleep 안 함")


def test_flushes_input_before_every_write(port, sleep):
    pytest.skip("TODO (보너스): Mock().attach_mock 으로 flush → write → readline 순서 검증")


def test_set_rate_invalid_never_touches_the_wire(drv, port):
    pytest.skip("TODO: set_rate(5) → ValueError, write.assert_not_called()")


def test_context_manager_closes_port(port, sleep):
    pytest.skip("TODO: with 블록 → close 1번 (예외가 나도)")
