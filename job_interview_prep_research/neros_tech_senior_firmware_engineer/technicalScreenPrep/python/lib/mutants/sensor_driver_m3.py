"""M3 · 범위 검사 없음 (999.0 도 통과)."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib import sensor_driver as _sd


class SensorDriver(_sd.SensorDriver):
    def read_temperature(self):
        old = _sd.TEMP_MIN, _sd.TEMP_MAX
        _sd.TEMP_MIN, _sd.TEMP_MAX = float("-inf"), float("inf")
        try:
            return super().read_temperature()
        finally:
            _sd.TEMP_MIN, _sd.TEMP_MAX = old
