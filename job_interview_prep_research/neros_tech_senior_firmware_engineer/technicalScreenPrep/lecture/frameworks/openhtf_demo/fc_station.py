"""OpenHTF: 공장 테스트 스테이션 스타일. phase(단계) → measurement(측정값 + 합격 범위) → test record(JSON).

  cd lecture/frameworks && ../../.venv-fw/bin/python openhtf_demo/fc_station.py SN0001
  ../../.venv-fw/bin/python openhtf_demo/fc_station.py SN0002 --hot     # 온도 한계 초과 → FAIL 기록

pytest 와 다른 점: '통과/실패'만이 아니라 **모든 측정값과 한계, 단위, 시리얼 번호**가 기록으로 남는다 → 수율·추적성.
"""
import sys
from pathlib import Path

import openhtf as htf
from openhtf.output.callbacks import json_factory
from openhtf.plugs import BasePlug
from openhtf.util import units

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, FakeFirmware, FakeSerial  # noqa: E402

HOT = "--hot" in sys.argv


class DutPlug(BasePlug):
    """plug = 장비/DUT 연결. OpenHTF 가 테스트 시작 때 만들고 끝나면 tearDown 을 부른다."""

    def __init__(self):
        self.fw = FakeFirmware(temp_c=71.0 if HOT else 24.0)
        self.dut = DutClient(FakeSerial(self.fw))

    def send(self, cmd):
        return self.dut.send(cmd)

    def tearDown(self):
        self.dut.send("disarm")


@htf.plug(dut=DutPlug)
@htf.measures(htf.Measurement("fw_version").equals("1.4.2"))
def check_firmware(test, dut):
    test.measurements.fw_version = dut.send("ver")


@htf.plug(dut=DutPlug)
@htf.measures(
    htf.Measurement("board_temp").in_range(0, 60).with_units(units.DEGREE_CELSIUS)
    .doc("Board temperature right after power-up"))
def check_temperature(test, dut):
    test.measurements.board_temp = float(dut.send("temp").split("=")[1])


@htf.plug(dut=DutPlug)
@htf.measures(htf.Measurement("arm_reply").equals("OK"),
              htf.Measurement("state_after_arm").equals("ARMED"))
def check_arming(test, dut):
    test.measurements.arm_reply = dut.send("arm")
    test.measurements.state_after_arm = dut.send("status")


def main():
    serial = next((a for a in sys.argv[1:] if not a.startswith("--")), "SN0001")
    out = Path(__file__).resolve().parent / "out"
    out.mkdir(exist_ok=True)
    test = htf.Test(check_firmware, check_temperature, check_arming,
                    test_name="fc_board_functional", station_id="FCT-01")
    test.add_output_callbacks(json_factory.OutputToJSON(str(out / "{dut_id}.json"), indent=2))
    passed = test.execute(test_start=lambda: serial)
    print(f"{serial}: {'PASS' if passed else 'FAIL'} → {out / (serial + '.json')}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
