"""M8 · 재시도 전에 입력 버퍼를 비우지 않음 (늦게 온 이전 응답을 새 응답으로 착각할 수 있음)."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base, SensorTimeout


class SensorDriver(_Base):
    def _transact(self, cmd):
        for attempt in range(self.retries + 1):
            if attempt:
                self.sleep(self.backoff * 2 ** (attempt - 1))
            self.port.write(cmd + b"\n")
            line = self.port.readline()
            if line:
                return line
        raise SensorTimeout(cmd)
