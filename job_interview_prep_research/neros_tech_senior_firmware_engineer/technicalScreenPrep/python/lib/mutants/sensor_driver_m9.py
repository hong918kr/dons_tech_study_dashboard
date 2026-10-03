"""M9 · with 블록을 빠져나갈 때 포트를 닫지 않음."""
from lib.sensor_driver import *            # noqa: F401,F403
from lib.sensor_driver import SensorDriver as _Base


class SensorDriver(_Base):
    def __exit__(self, *exc):
        return False
