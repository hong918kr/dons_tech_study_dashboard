"""M1 · 타임아웃에서 재시도하지 않음."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base, SensorTimeout


class SensorDriver(_Base):
    def _transact(self, cmd):
        self.port.reset_input_buffer()
        self.port.write(cmd + b"\n")
        line = self.port.readline()
        if not line:
            raise SensorTimeout(cmd)
        return line
