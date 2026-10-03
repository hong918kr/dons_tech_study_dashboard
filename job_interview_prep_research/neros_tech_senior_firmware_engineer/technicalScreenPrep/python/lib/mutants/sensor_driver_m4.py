"""M4 · set_rate 가 검증 전에 먼저 써 버림 (잘못된 명령이 선로에 나감)."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base


class SensorDriver(_Base):
    def set_rate(self, hz):
        self.port.write(b"RATE %d\n" % hz)
        return super().set_rate(hz)
