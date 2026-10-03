"""M2 · ERR 응답에도 재시도함 (고장 센서를 계속 두드림)."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base


class SensorDriver(_Base):
    def _transact(self, cmd):
        line = super()._transact(cmd)
        for _ in range(self.retries):
            if not line.startswith(b"ERR"):
                break
            line = super()._transact(cmd)
        return line
