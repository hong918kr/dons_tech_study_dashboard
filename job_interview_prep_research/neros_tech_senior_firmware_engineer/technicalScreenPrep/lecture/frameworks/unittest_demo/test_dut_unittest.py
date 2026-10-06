"""unittest (= PyUnit, 표준 라이브러리): xUnit 스타일 — TestCase 클래스, setUp/tearDown, self.assert*.

  cd lecture/frameworks && python3 -m unittest -v unittest_demo.test_dut_unittest
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, DutTimeout, FakeFirmware, FakeSerial  # noqa: E402


class DutCliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):                       # 리그 연결처럼 비싼 것: 클래스당 한 번
        cls.fw = FakeFirmware()

    def setUp(self):                           # 테스트마다: 깨끗한 상태로
        self.fw.state, self.fw.mute = "DISARMED", False
        self.dut = DutClient(FakeSerial(self.fw))

    def tearDown(self):
        self.dut.send("disarm")                # 항상 안전 상태로 되돌리기

    def test_version(self):
        self.assertEqual(self.dut.send("ver"), "1.4.2")

    def test_arm_then_status(self):
        self.assertEqual(self.dut.send("arm"), "OK")
        self.assertEqual(self.dut.send("status"), "ARMED")

    def test_double_arm_rejected(self):
        self.dut.send("arm")
        self.assertTrue(self.dut.send("arm").startswith("ERR"))

    def test_temperature_in_range(self):
        for temp in (-40.0, 23.5, 85.0):       # subTest = 표 하나에서 여러 케이스, 실패도 각각 보고
            with self.subTest(temp=temp):
                self.fw.temp_c = temp
                self.assertAlmostEqual(self.dut.temperature(), temp, places=1)

    def test_no_reply_raises(self):
        self.fw.mute = True
        with self.assertRaises(DutTimeout):
            self.dut.send("ver")
        self.fw.mute = False


if __name__ == "__main__":
    unittest.main(verbosity=2)
