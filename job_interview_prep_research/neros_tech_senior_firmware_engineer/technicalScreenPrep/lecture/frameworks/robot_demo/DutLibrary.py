"""Robot Framework keyword 라이브러리: Python 메서드 하나 = .robot 파일의 keyword 하나.
메서드 이름 'arm_should_succeed' → keyword 'Arm Should Succeed'.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, DutTimeout, FakeFirmware, FakeSerial  # noqa: E402


class DutLibrary:
    ROBOT_LIBRARY_SCOPE = "SUITE"             # 스위트 동안 같은 인스턴스 (리그 연결 재사용)

    def __init__(self):
        self.fw = FakeFirmware()
        self.dut = None

    def open_dut(self, port="sim"):
        """실제 리그라면 serial.Serial(port, 115200, timeout=0.5)."""
        self.dut = DutClient(FakeSerial(self.fw))

    def close_dut(self):
        if self.dut:
            self.dut.send("disarm")
            self.dut = None

    def send_command(self, cmd):
        return self.dut.send(cmd)

    def firmware_version_should_be(self, expected):
        got = self.dut.send("ver")
        if got != expected:
            raise AssertionError(f"version {got} != {expected}")

    def set_simulated_temperature(self, celsius):
        self.fw.temp_c = float(celsius)

    def temperature_should_be_between(self, low, high):
        t = self.dut.temperature()
        if not float(low) <= t <= float(high):
            raise AssertionError(f"temperature {t} not in [{low}, {high}]")
        return t

    def make_dut_silent(self):
        self.fw.mute = True

    def make_dut_responsive(self):
        self.fw.mute = False

    def command_should_time_out(self, cmd):
        try:
            self.dut.send(cmd)
        except DutTimeout:
            return
        raise AssertionError(f"{cmd!r} unexpectedly got a reply")
