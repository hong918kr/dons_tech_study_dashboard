"""pytest: 같은 DUT 시나리오를 pytest 로 — 다른 프레임워크와 1:1 비교용.

  cd lecture/frameworks && ../../.venv-fw/bin/python -m pytest -q pytest_demo
"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, DutTimeout, FakeFirmware, FakeSerial  # noqa: E402


@pytest.fixture
def fw():
    return FakeFirmware()


@pytest.fixture
def dut(fw):
    d = DutClient(FakeSerial(fw))
    yield d
    fw.mute = False
    d.send("disarm")                              # 항상 안전 상태로


def test_version(dut):
    assert dut.send("ver") == "1.4.2"


def test_arm_then_status(dut):
    assert dut.send("arm") == "OK"
    assert dut.send("status") == "ARMED"


def test_double_arm_rejected(dut):
    dut.send("arm")
    assert dut.send("arm").startswith("ERR")


@pytest.mark.parametrize("temp", [-40.0, 23.5, 85.0])
def test_temperature(fw, dut, temp):
    fw.temp_c = temp
    assert dut.temperature() == pytest.approx(temp, abs=0.05)


def test_silent_dut_times_out(fw, dut):
    fw.mute = True
    with pytest.raises(DutTimeout):
        dut.send("ver")
