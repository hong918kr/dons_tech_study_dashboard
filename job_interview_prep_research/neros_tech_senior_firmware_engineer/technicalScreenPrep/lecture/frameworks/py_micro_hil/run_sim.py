"""py-micro-hil 0.1.8 를 Raspberry Pi 가 아닌 PC(macOS/Linux)에서 시뮬레이션 모드로 돌리는 실행기.

0.1.8 의 RPiPeripherals.py 는 'import RPi.GPIO' 를 먼저 하고 그 다음에 가짜 모듈로 바꿔 끼우기 때문에
RPi.GPIO 가 없는 PC 에서는 import 단계에서 죽는다. 여기서 가짜 모듈을 먼저 등록한 뒤 hiltest CLI 를 부른다.

  cd lecture/frameworks/py_micro_hil && ../../../.venv-fw/bin/python run_sim.py
"""
import importlib.util
import sys
import types
from pathlib import Path

spec = importlib.util.find_spec("py_micro_hil")
mock_path = Path(spec.origin).parent / "peripherals" / "dummyRPiPeripherals.py"
mock_spec = importlib.util.spec_from_file_location("_pmh_dummy", mock_path)
mock = importlib.util.module_from_spec(mock_spec)
mock_spec.loader.exec_module(mock)

# 기본 dummy GPIO 는 상태가 없다 (input() 이 항상 0). 핀 값을 기억하고, 배선(LOOPBACK)을 흉내 내는 fake 로 바꾼다.
LOOPBACK = {17: 27}          # 리그 배선: 출력 17 → DUT → 입력 27 이 따라온다고 가정
_pins = {}


def _output(pin, value):
    _pins[pin] = int(bool(value))
    if pin in LOOPBACK:
        _pins[LOOPBACK[pin]] = _pins[pin]


fake_gpio = types.SimpleNamespace(**vars(mock.GPIO))
fake_gpio.output = _output
fake_gpio.input = lambda pin: _pins.get(pin, 0)
fake_gpio.setup = lambda pin, mode, **kw: _pins.setdefault(pin, int(kw.get("initial", 0) or 0))

rpi = types.ModuleType("RPi")
rpi.GPIO = fake_gpio
sys.modules.update({"RPi": rpi, "RPi.GPIO": fake_gpio, "spidev": mock.spidev})

from py_micro_hil.cli import main  # noqa: E402

sys.exit(main())
