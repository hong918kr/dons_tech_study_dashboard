"""가짜 HIL 리그 — 문제 04(conftest 만들기)용. 실제 리그(PSU 릴레이 · debug probe · DUT UART)를 흉내낸다.

모든 동작을 FakeRig.events 에 기록한다 → 채점기가 "세션당 한 번 열었나", "테스트마다 reset 했나",
"실패한 테스트에서만 로그를 저장했나", "끝나면 전원을 껐나"를 확인한다.
"""


class FakeDut:
    def __init__(self, rig):
        self.rig = rig
        self.state = "DISARMED"

    def version(self):
        return "1.4.2"

    def arm(self):
        if not self.rig.powered:
            raise RuntimeError("DUT is not powered")
        self.state = "ARMED"

    def disarm(self):
        self.state = "DISARMED"


class FakeRig:
    events = []                      # 클래스 전체 기록 (채점용)

    def __init__(self, name="sim"):
        self.name = name
        self.powered = False
        self.closed = False
        self.dut = FakeDut(self)
        FakeRig.events.append(("open", name))

    def power_on(self):
        self.powered = True
        FakeRig.events.append(("power_on",))

    def power_off(self):
        self.powered = False
        FakeRig.events.append(("power_off",))

    def reset_dut(self):
        self.dut.state = "DISARMED"
        FakeRig.events.append(("reset",))

    def save_log(self, path):
        with open(path, "w") as f:
            f.write(f"rig={self.name} state={self.dut.state}\n")
        FakeRig.events.append(("save_log", str(path)))

    def close(self):
        self.closed = True
        FakeRig.events.append(("close",))
