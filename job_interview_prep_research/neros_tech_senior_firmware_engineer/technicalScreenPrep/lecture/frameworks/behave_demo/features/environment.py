"""behave 훅: pytest 의 fixture / conftest 역할. 시나리오마다 새 DUT, 끝나면 안전 상태로."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common"))
from fake_dut import DutClient, FakeFirmware, FakeSerial  # noqa: E402


def before_scenario(context, scenario):
    context.fw = FakeFirmware()
    context.dut = DutClient(FakeSerial(context.fw))


def after_scenario(context, scenario):
    context.fw.mute = False
    context.dut.send("disarm")
