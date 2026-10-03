"""M7 · 파서가 부호를 무시함 (TEMP=-12.5 → 12.5)."""
import re

from lib.sensor_driver import *            # noqa: F401,F403
from lib import sensor_driver as _sd


class SensorDriver(_sd.SensorDriver):
    def read_temperature(self):
        line = self._transact(b"TEMP?")
        if line.startswith(b"ERR"):
            raise _sd.SensorError(line)
        m = re.search(rb"(\d+(?:\.\d+)?)", line)
        if not m:
            raise _sd.SensorError(line)
        t = float(m.group(1))
        if not _sd.TEMP_MIN <= t <= _sd.TEMP_MAX:
            raise _sd.SensorError(t)
        return t
