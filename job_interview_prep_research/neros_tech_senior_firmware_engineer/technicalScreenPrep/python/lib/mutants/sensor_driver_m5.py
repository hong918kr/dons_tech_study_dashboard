"""M5 · 요청 끝을 b"\\r\\n" 으로 보냄 (프로토콜 위반)."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base, SensorTimeout


class SensorDriver(_Base):
    def _transact(self, cmd):
        for attempt in range(self.retries + 1):
            if attempt:
                self.sleep(self.backoff * 2 ** (attempt - 1))
            self.port.reset_input_buffer()
            self.port.write(cmd + b"\r\n")
            line = self.port.readline()
            if line:
                return line
        raise SensorTimeout(cmd)
