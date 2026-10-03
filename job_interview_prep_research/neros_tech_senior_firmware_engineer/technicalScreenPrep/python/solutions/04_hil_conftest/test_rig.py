"""04 · 위 conftest 를 쓰는 예시 테스트. `pytest --rig=sim` 으로 돌린다."""
import pytest


@pytest.mark.hw
def test_version(dut):
    assert dut.version() == "1.4.2"


@pytest.mark.hw
def test_arm_disarm(dut):
    dut.arm()
    assert dut.state == "ARMED"
    dut.disarm()
    assert dut.state == "DISARMED"


@pytest.mark.hw
def test_each_test_starts_disarmed(dut):
    assert dut.state == "DISARMED"              # 앞 테스트가 ARMED 로 남겨도 reset 덕분에 통과


def test_pure_logic_runs_without_rig():
    assert int("0x1F", 16) == 31
