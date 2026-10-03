"""M6 · 백오프가 지수가 아니라 고정 (0.1, 0.1, ...)."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base, SensorTimeout


class SensorDriver(_Base):
    def _transact(self, cmd):
        for attempt in range(self.retries + 1):
            if attempt:
                self.sleep(self.backoff)
            self.port.reset_input_buffer()
            self.port.write(cmd + b"\n")
            line = self.port.readline()
            if line:
                return line
        raise SensorTimeout(cmd)
