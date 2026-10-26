#!/usr/bin/env python3
"""bench_drivers.py -- HIL bench instrument interfaces + fake implementations.

R08 (HIL bench design) companion example. Python 3.9+, standard library only.

What this file shows
--------------------
1. Abstract instrument interfaces (PowerSupply, Attenuator, Debugger,
   SerialPort, LogicAnalyzer, RcInjector, PowerRelay). A real bench would
   implement them with PyVISA (SCPI PSU), Mini-Circuits HTTP/Telnet
   (attenuator), pylink/pyOCD/OpenOCD (debugger), pyserial (UART) and the
   Saleae logic2-automation API (logic analyzer). Those real drivers are NOT
   here: this file must run with no hardware and no pip installs.
2. Fake implementations that share ONE injected clock (FakeClock). Nothing
   calls time.sleep or time.monotonic, so tests are deterministic and run in
   milliseconds of wall time.
3. A tiny simulated DUT (SimFlightController) whose failsafe behaviour is
   modelled on the Betaflight docs: signal loss after 100 ms without valid
   frames, Stage 1 guard time = failsafe_delay counted from the last good
   frame, Stage 2 "Drop" = disarm and motors to zero. (Numbers are inputs,
   not truth -- see R03/R05 for the real firmware.)
4. Bench config as data (a dict that mirrors the YAML inventory in R08),
   a bench smoke test, and the sample test
   "RC link loss -> failsafe -> motors to zero within X ms".
5. Self-test at the bottom: good firmware PASSES, a buggy firmware build
   (failsafe never drops) FAILS, a dead instrument makes the smoke test fail
   BEFORE any DUT test runs.

Run:  python3 bench_drivers.py        (prints a report, exits 0 on success)
"""
from __future__ import annotations

import abc
import hashlib
import json
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# 0. Clock -- the single timebase every fake uses
# ---------------------------------------------------------------------------


class Clock(abc.ABC):
    """Injected clock. Real bench: wraps time.monotonic_ns()/time.sleep."""

    @abc.abstractmethod
    def now_us(self) -> int: ...

    @abc.abstractmethod
    def sleep_ms(self, ms: float) -> None: ...


class FakeClock(Clock):
    """Discrete-time clock. sleep_ms() advances time in 1 ms ticks and calls
    every registered tick callback, which is how the fake DUT and fake
    instruments 'run' while a test waits."""

    TICK_US = 1000

    def __init__(self) -> None:
        self._t_us = 0
        self._tickers: List[Callable[[int], None]] = []

    def now_us(self) -> int:
        return self._t_us

    def add_ticker(self, fn: Callable[[int], None]) -> None:
        self._tickers.append(fn)

    def sleep_ms(self, ms: float) -> None:
        end = self._t_us + int(ms * 1000)
        while self._t_us < end:
            self._t_us += self.TICK_US
            for fn in self._tickers:
                fn(self._t_us)


def wait_until(clock: Clock, cond: Callable[[], bool], timeout_ms: float,
               poll_ms: float = 1.0) -> bool:
    """Event-based wait with a timeout. Never a bare fixed sleep."""
    deadline = clock.now_us() + int(timeout_ms * 1000)
    while clock.now_us() <= deadline:
        if cond():
            return True
        clock.sleep_ms(poll_ms)
    return cond()


# ---------------------------------------------------------------------------
# 1. Instrument interfaces (what tests are allowed to touch)
# ---------------------------------------------------------------------------


class Instrument(abc.ABC):
    name: str = "instrument"

    @abc.abstractmethod
    def identify(self) -> str:
        """Like SCPI *IDN?  -- used by the bench smoke test."""


class PowerSupply(Instrument):
    @abc.abstractmethod
    def set_voltage(self, volts: float) -> None: ...

    @abc.abstractmethod
    def set_current_limit(self, amps: float) -> None: ...

    @abc.abstractmethod
    def output(self, on: bool) -> None: ...

    @abc.abstractmethod
    def measure_current(self) -> float: ...

    @abc.abstractmethod
    def measure_voltage(self) -> float: ...


class PowerRelay(Instrument):
    @abc.abstractmethod
    def set(self, closed: bool) -> None: ...


class Attenuator(Instrument):
    """Programmable step attenuator (e.g. a 0..90 dB, 0.25 dB-step part)."""
    min_db: float = 0.0
    max_db: float = 90.0
    step_db: float = 0.25

    @abc.abstractmethod
    def set_db(self, db: float) -> None: ...

    @abc.abstractmethod
    def get_db(self) -> float: ...


class Debugger(Instrument):
    """SWD probe: flash, reset, memory read, RTT (bidirectional)."""

    @abc.abstractmethod
    def flash(self, image: bytes) -> str:
        """Program + verify. Returns sha256 of what is in flash."""

    @abc.abstractmethod
    def reset(self) -> None: ...

    @abc.abstractmethod
    def read_build_id(self) -> str: ...

    @abc.abstractmethod
    def rtt_read(self) -> str: ...

    @abc.abstractmethod
    def rtt_write(self, text: str) -> None: ...


class SerialPort(Instrument):
    @abc.abstractmethod
    def write(self, data: bytes) -> None: ...

    @abc.abstractmethod
    def read_all(self) -> bytes: ...


class LogicAnalyzer(Instrument):
    """Records (t_us, channel, value) edges on the common timebase."""

    @abc.abstractmethod
    def start(self) -> None: ...

    @abc.abstractmethod
    def stop(self) -> List[Tuple[int, str, int]]: ...


class RcInjector(Instrument):
    """Sends RC frames (CRSF-like) at a fixed rate through the RF path."""

    @abc.abstractmethod
    def set_channels(self, throttle: int, arm: bool) -> None: ...

    @abc.abstractmethod
    def enable(self, on: bool) -> None: ...


# ---------------------------------------------------------------------------
# 2. Simulated DUT (stands in for the real FC on the bench)
# ---------------------------------------------------------------------------


@dataclass
class FirmwareImage:
    build_id: str
    failsafe_delay_ds: int = 15        # Stage 1 guard, 0.1 s units (BF 4.5 default 15)
    rx_loss_timeout_ms: int = 100      # "no valid data > 100 ms" = signal loss
    stage2_drops_motors: bool = True   # False = injected bug
    boot_ms: int = 300

    def to_bytes(self) -> bytes:
        return json.dumps(self.__dict__, sort_keys=True).encode()

    @staticmethod
    def from_bytes(raw: bytes) -> "FirmwareImage":
        return FirmwareImage(**json.loads(raw.decode()))


class SimFlightController:
    """Very small behavioural model. Not Betaflight -- just enough state to
    exercise the bench and the test."""

    IDLE_DSHOT = 100     # armed idle (airmode/dynamic idle keeps motors spinning)

    def __init__(self, clock: FakeClock) -> None:
        self.clock = clock
        self.flash_bytes: bytes = b""
        self.powered = False
        self.booted = False
        self._boot_at_us = 0
        self.fw: Optional[FirmwareImage] = None
        self.armed = False
        self.state = "OFF"
        self.motor = [0, 0, 0, 0]
        self.last_good_rx_us: Optional[int] = None
        self.rc_throttle = 0
        self.rc_arm = False
        self.rtt_up: List[str] = []
        self.rtt_down: List[str] = []
        self.uart_tx: bytearray = bytearray()
        self.listeners: List[Callable[[int, str, int], None]] = []
        self.boot_count = 0
        clock.add_ticker(self._tick)

    # --- electrical ---
    def set_power(self, on: bool) -> None:
        if on and not self.powered:
            self.powered = True
            self._start_boot()
        elif not on and self.powered:
            self.powered = False
            self.booted = False
            self.state = "OFF"
            self._set_motors(0)
            self.armed = False

    def current_draw_a(self) -> float:
        if not self.powered:
            return 0.0
        base = 0.12 if self.booted else 0.20          # boot inrush-ish
        return base + 0.002 * sum(self.motor) / 4      # no props: tiny

    def _start_boot(self) -> None:
        self.booted = False
        self.state = "BOOT"
        self._boot_at_us = self.clock.now_us()
        self.armed = False
        self.last_good_rx_us = None
        self._set_motors(0)
        try:
            self.fw = FirmwareImage.from_bytes(self.flash_bytes) if self.flash_bytes else None
        except (ValueError, TypeError):
            self.fw = None                                 # corrupt image = bricked

    def reset(self) -> None:
        if self.powered:
            self._start_boot()

    # --- RF side: called by the fake RX when a frame survives the RF path ---
    def on_rc_frame(self, throttle: int, arm: bool) -> None:
        if not self.booted:
            return
        self.last_good_rx_us = self.clock.now_us()
        self.rc_throttle, self.rc_arm = throttle, arm

    # --- main loop ---
    def _tick(self, t_us: int) -> None:
        if not self.powered or self.fw is None:
            return
        fw = self.fw
        if not self.booted:
            if t_us - self._boot_at_us >= fw.boot_ms * 1000:
                self.booted = True
                self.boot_count += 1
                self.state = "DISARMED"
                self.rtt_up.append(f"BOOT build={fw.build_id}\n")
                self.uart_tx += f"READY {fw.build_id}\r\n".encode()
            return
        self._serve_rtt()
        since_rx_ms = (t_us - self.last_good_rx_us) / 1000 if self.last_good_rx_us else 1e9
        rx_ok = since_rx_ms <= fw.rx_loss_timeout_ms

        if self.state in ("DISARMED",):
            if rx_ok and self.rc_arm and self.rc_throttle <= 1050:
                self.armed = True
                self.state = "ARMED"
                self.rtt_up.append("ARMED\n")
        elif self.state == "ARMED":
            if not rx_ok:
                self.state = "FAILSAFE_STAGE1"
                self.rtt_up.append(f"RXLOSS t={t_us}\n")
            elif not self.rc_arm:
                self._disarm("DISARMED")
            else:
                self._set_motors(self._throttle_to_dshot(self.rc_throttle))
        elif self.state == "FAILSAFE_STAGE1":
            # Stage 1 fallback: throttle low, but still armed (motors at idle)
            self._set_motors(self.IDLE_DSHOT)
            guard_ms = fw.failsafe_delay_ds * 100
            if since_rx_ms >= guard_ms:
                self.state = "FAILSAFE_STAGE2"
                self.rtt_up.append(f"FAILSAFE_STAGE2 t={t_us}\n")
                if fw.stage2_drops_motors:
                    self._disarm("FAILSAFE_DROPPED")
        elif self.state == "FAILSAFE_STAGE2" and not fw.stage2_drops_motors:
            self._set_motors(self.IDLE_DSHOT)              # bug: keeps spinning

    def _disarm(self, new_state: str) -> None:
        self.armed = False
        self.state = new_state
        self._set_motors(0)

    @staticmethod
    def _throttle_to_dshot(us: int) -> int:
        us = max(1000, min(2000, us))
        return 48 + int((us - 1000) * (2047 - 48) / 1000)  # DShot throttle 48..2047

    def _set_motors(self, value: int) -> None:
        for i in range(4):
            if self.motor[i] != value:
                self.motor[i] = value
                for fn in self.listeners:
                    fn(self.clock.now_us(), f"M{i+1}", value)

    def _serve_rtt(self) -> None:
        while self.rtt_down:
            cmd = self.rtt_down.pop(0).strip()
            if cmd == "status":
                self.rtt_up.append(f"STATE {self.state}\n")


# ---------------------------------------------------------------------------
# 3. Fake instruments
# ---------------------------------------------------------------------------


class FakePowerSupply(PowerSupply):
    def __init__(self, name: str, dut: SimFlightController, relay: "FakeRelay",
                 alive: bool = True) -> None:
        self.name, self.dut, self.relay, self.alive = name, dut, relay, alive
        self.v_set, self.i_lim, self.on = 0.0, 0.0, False

    def _check(self) -> None:
        if not self.alive:
            raise ConnectionError(f"{self.name}: no response (VISA timeout)")

    def identify(self) -> str:
        self._check()
        return "FAKE,PSU-1CH,SN0001,1.0"

    def set_voltage(self, volts: float) -> None:
        self._check()
        self.v_set = volts
        self._apply()

    def set_current_limit(self, amps: float) -> None:
        self._check()
        self.i_lim = amps

    def output(self, on: bool) -> None:
        self._check()
        self.on = on
        self._apply()

    def _apply(self) -> None:
        powered = self.on and self.relay.closed and self.v_set >= 6.0  # FC needs > ~6 V [assumed]
        self.dut.set_power(powered)

    def measure_current(self) -> float:
        self._check()
        return self.dut.current_draw_a() if (self.on and self.relay.closed) else 0.0

    def measure_voltage(self) -> float:
        self._check()
        return self.v_set if self.on else 0.0


class FakeRelay(PowerRelay):
    def __init__(self, name: str) -> None:
        self.name, self.closed = name, True
        self.psu: Optional[FakePowerSupply] = None

    def identify(self) -> str:
        return "FAKE,RELAY-8CH"

    def set(self, closed: bool) -> None:
        self.closed = closed
        if self.psu:
            self.psu._apply()


class FakeAttenuator(Attenuator):
    def __init__(self, name: str) -> None:
        self.name, self._db = name, 0.0

    def identify(self) -> str:
        return "FAKE,RCDAT-LIKE,0-90dB"

    def set_db(self, db: float) -> None:
        if not (self.min_db <= db <= self.max_db):
            raise ValueError(f"{db} dB outside {self.min_db}..{self.max_db}")
        self._db = round(db / self.step_db) * self.step_db

    def get_db(self) -> float:
        return self._db


class FakeDebugger(Debugger):
    def __init__(self, name: str, dut: SimFlightController, alive: bool = True) -> None:
        self.name, self.dut, self.alive = name, dut, alive

    def identify(self) -> str:
        if not self.alive:
            raise ConnectionError(f"{self.name}: probe not found on USB")
        return "FAKE,SWD-PROBE,SN5000"

    def flash(self, image: bytes) -> str:
        self.identify()
        self.dut.flash_bytes = image
        return hashlib.sha256(self.dut.flash_bytes).hexdigest()

    def reset(self) -> None:
        self.dut.reset()

    def read_build_id(self) -> str:
        fw = self.dut.fw
        return fw.build_id if (fw and self.dut.booted) else ""

    def rtt_read(self) -> str:
        out = "".join(self.dut.rtt_up)
        self.dut.rtt_up.clear()
        return out

    def rtt_write(self, text: str) -> None:            # RTT down-buffer: host -> target
        self.dut.rtt_down.append(text)


class FakeSerialPort(SerialPort):
    """FC debug/MSP UART. Loopback mode is used by the smoke test."""

    def __init__(self, name: str, dut: SimFlightController) -> None:
        self.name, self.dut, self.loopback = name, dut, False
        self._loop = bytearray()

    def identify(self) -> str:
        return "FAKE,FT232H-LIKE,SN-UART1"

    def write(self, data: bytes) -> None:
        if self.loopback:
            self._loop += data

    def read_all(self) -> bytes:
        if self.loopback:
            out, self._loop = bytes(self._loop), bytearray()
            return out
        out = bytes(self.dut.uart_tx)
        self.dut.uart_tx.clear()
        return out


class FakeLogicAnalyzer(LogicAnalyzer):
    def __init__(self, name: str, clock: Clock, dut: SimFlightController) -> None:
        self.name, self.clock = name, clock
        self._edges: List[Tuple[int, str, int]] = []
        self._running = False
        dut.listeners.append(self._on_edge)

    def identify(self) -> str:
        return "FAKE,LA-8CH,100MS/s"

    def _on_edge(self, t_us: int, ch: str, v: int) -> None:
        if self._running:
            self._edges.append((t_us, ch, v))

    def mark(self, ch: str, v: int) -> None:
        """GPIO marker from the host/test (e.g. 'link cut now')."""
        self._on_edge(self.clock.now_us(), ch, v)

    def start(self) -> None:
        self._edges, self._running = [], True

    def stop(self) -> List[Tuple[int, str, int]]:
        self._running = False
        return list(self._edges)


class FakeRcInjector(RcInjector):
    """TX module -> SMA cable -> fixed pad -> step attenuator -> RX.
    A frame reaches the FC only if received power >= RX sensitivity."""

    def __init__(self, name: str, clock: FakeClock, dut: SimFlightController,
                 att: Attenuator, tx_dbm: float, fixed_pad_db: float,
                 rx_sens_dbm: float, rate_hz: int) -> None:
        self.name, self.dut, self.att = name, dut, att
        self.tx_dbm, self.pad, self.sens = tx_dbm, fixed_pad_db, rx_sens_dbm
        self.period_us = int(1_000_000 / rate_hz)
        self._next_us = 0
        self.on, self.throttle, self.arm = False, 1000, False
        self.frames_sent = self.frames_delivered = 0
        clock.add_ticker(self._tick)

    def identify(self) -> str:
        return "FAKE,RC-TX,CRSF"

    def rx_power_dbm(self) -> float:
        return self.tx_dbm - self.pad - self.att.get_db()

    def set_channels(self, throttle: int, arm: bool) -> None:
        self.throttle, self.arm = throttle, arm

    def enable(self, on: bool) -> None:
        self.on = on

    def _tick(self, t_us: int) -> None:
        if not self.on or t_us < self._next_us:
            return
        self._next_us = t_us + self.period_us
        self.frames_sent += 1
        if self.rx_power_dbm() >= self.sens:
            self.frames_delivered += 1
            self.dut.on_rc_frame(self.throttle, self.arm)


# ---------------------------------------------------------------------------
# 4. Bench config as data (mirrors the YAML inventory in R08)
# ---------------------------------------------------------------------------

BENCH_CONFIG: Dict = {
    "bench_id": "hil-fc-01",
    "dut": {"board": "fc-rev-b", "serial": "FC-000123"},
    "instruments": {
        "psu":   {"driver": "fake", "visa": "TCPIP0::10.0.0.21::5025::SOCKET",
                  "volts": 16.8, "current_limit_a": 1.0},
        "relay": {"driver": "fake", "channel": 1},
        "atten": {"driver": "fake", "url": "http://10.0.0.22/", "fixed_pad_db": 40.0},
        "probe": {"driver": "fake", "serial": "000801234567"},
        "uart":  {"driver": "fake", "dev": "/dev/hil/fc-01-uart"},
        "la":    {"driver": "fake", "channels": {"M1": 0, "M2": 1, "M3": 2, "M4": 3, "MARK": 7}},
        "rc":    {"driver": "fake", "tx_dbm": 10.0, "rate_hz": 250, "rx_sens_dbm": -108.0},
    },
    "limits": {"idle_current_a": [0.05, 0.30], "boot_timeout_ms": 2000},
}


@dataclass
class Bench:
    clock: FakeClock
    dut: SimFlightController            # only fakes have this; tests must not use it
    psu: PowerSupply
    relay: PowerRelay
    atten: Attenuator
    probe: Debugger
    uart: SerialPort
    la: FakeLogicAnalyzer
    rc: FakeRcInjector
    config: Dict = field(default_factory=dict)

    def instruments(self) -> List[Instrument]:
        return [self.psu, self.relay, self.atten, self.probe, self.uart, self.la, self.rc]


def build_fake_bench(cfg: Dict, dead: Tuple[str, ...] = ()) -> Bench:
    """Factory: config dict -> instrument objects. A real factory would switch
    on cfg['driver'] ('visa', 'minicircuits_http', 'pylink', ...)."""
    ins = cfg["instruments"]
    clock = FakeClock()
    dut = SimFlightController(clock)
    relay = FakeRelay("relay")
    psu = FakePowerSupply("psu", dut, relay, alive="psu" not in dead)
    relay.psu = psu
    att = FakeAttenuator("atten")
    probe = FakeDebugger("probe", dut, alive="probe" not in dead)
    uart = FakeSerialPort("uart", dut)
    la = FakeLogicAnalyzer("la", clock, dut)
    rc = FakeRcInjector("rc", clock, dut, att, tx_dbm=ins["rc"]["tx_dbm"],
                        fixed_pad_db=ins["atten"]["fixed_pad_db"],
                        rx_sens_dbm=ins["rc"]["rx_sens_dbm"], rate_hz=ins["rc"]["rate_hz"])
    return Bench(clock, dut, psu, relay, att, probe, uart, la, rc, cfg)


# ---------------------------------------------------------------------------
# 5. Bench smoke test + recovery helpers (run BEFORE any DUT test)
# ---------------------------------------------------------------------------


class BenchError(Exception):
    """The bench is broken -- result is INFRA_FAIL, not a firmware FAIL."""


def bench_smoke_test(b: Bench) -> List[str]:
    log: List[str] = []
    for inst in b.instruments():
        try:
            log.append(f"identify {inst.name}: {inst.identify()}")
        except Exception as e:            # noqa: BLE001 -- any failure = infra
            raise BenchError(f"smoke: {inst.name} not responding: {e}") from e
    # UART loopback (jumper / loopback adapter) -- proves the USB-serial path
    uart = b.uart
    if isinstance(uart, FakeSerialPort):
        uart.loopback = True
    uart.write(b"\x55\xaa ping")
    ok = uart.read_all() == b"\x55\xaa ping"
    if isinstance(uart, FakeSerialPort):
        uart.loopback = False
    if not ok:
        raise BenchError("smoke: UART loopback mismatch")
    log.append("uart loopback: OK")
    # Attenuator round-trip, then park at a known value
    b.atten.set_db(30.0)
    if abs(b.atten.get_db() - 30.0) > 1e-9:
        raise BenchError("smoke: attenuator readback mismatch")
    b.atten.set_db(0.0)
    log.append("attenuator readback: OK")
    return log


def power_cycle(b: Bench, off_ms: int = 500) -> None:
    b.relay.set(False)
    b.clock.sleep_ms(off_ms)
    b.relay.set(True)


def bring_up(b: Bench, image: bytes, expected_build_id: str) -> Dict[str, float]:
    """Safe power-on + flash + verify. Raises BenchError for infra problems.
    expected_build_id comes from CI (the commit under test), never from the DUT."""
    lim = b.config["limits"]
    ps = b.config["instruments"]["psu"]
    b.psu.set_current_limit(ps["current_limit_a"])   # limit FIRST, then voltage
    b.psu.set_voltage(ps["volts"])
    b.psu.output(True)
    b.clock.sleep_ms(50)
    sha = b.probe.flash(image)
    if sha != hashlib.sha256(image).hexdigest():
        raise BenchError("flash verify mismatch")
    b.probe.reset()
    want = expected_build_id
    if not wait_until(b.clock, lambda: b.probe.read_build_id() == want,
                      timeout_ms=lim["boot_timeout_ms"]):
        # one recovery attempt: hard power cycle, then give up as 'bricked?'
        power_cycle(b)
        if not wait_until(b.clock, lambda: b.probe.read_build_id() == want,
                          timeout_ms=lim["boot_timeout_ms"]):
            raise BenchError("DUT did not report expected build id (bricked?)")
    idle = b.psu.measure_current()
    lo, hi = lim["idle_current_a"]
    if not (lo <= idle <= hi):
        raise BenchError(f"idle current {idle:.3f} A outside [{lo}, {hi}]")
    return {"idle_current_a": idle}


# ---------------------------------------------------------------------------
# 6. The sample test: RC link loss -> failsafe -> motors to zero within X ms
# ---------------------------------------------------------------------------


@dataclass
class TestResult:
    name: str
    verdict: str                     # PASS / FAIL / INFRA_FAIL
    details: Dict = field(default_factory=dict)


def test_rc_link_loss_failsafe(b: Bench, image: bytes, build_id: str,
                               deadline_ms: float) -> TestResult:
    """Measure from the LAST frame that reached the RX (Betaflight counts the
    Stage 1 guard from the last good packet) and also from the 'link cut'
    marker, to the first edge where all four motor lines read 0."""
    name = "rc_link_loss_failsafe"
    try:
        bench_smoke_test(b)
        info = bring_up(b, image, build_id)
    except BenchError as e:
        return TestResult(name, "INFRA_FAIL", {"reason": str(e)})

    # Arrange: RC link up, arm at low throttle, then throttle to 1300 us
    b.atten.set_db(0.0)
    b.rc.set_channels(throttle=1000, arm=False)
    b.rc.enable(True)
    b.clock.sleep_ms(200)
    b.rc.set_channels(throttle=1000, arm=True)
    b.probe.rtt_read()
    if not wait_until(b.clock, lambda: "ARMED" in b.probe.rtt_read(), timeout_ms=500):
        return TestResult(name, "FAIL", {"reason": "did not arm", **info})
    b.rc.set_channels(throttle=1300, arm=True)
    b.clock.sleep_ms(200)

    # Act: cut the RF link with the attenuator (no radiated anything)
    b.la.start()
    b.la.mark("MARK", 1)
    t_cut = b.clock.now_us()
    b.atten.set_db(b.atten.max_db)
    rx_dbm = b.rc.rx_power_dbm()
    last_good = b.dut.last_good_rx_us  # fake-only shortcut; real bench: RX-side LA channel
    b.clock.sleep_ms(deadline_ms + 500)
    edges = b.la.stop()
    b.atten.set_db(0.0)
    b.rc.enable(False)

    # Assert: find first time all motors are 0 after the cut
    level = {f"M{i}": None for i in range(1, 5)}
    t_zero = None
    for t, ch, v in edges:
        if ch in level:
            level[ch] = v
            if t >= t_cut and all(x == 0 for x in level.values()):
                t_zero = t
                break
    details = {
        **info, "rx_power_at_cut_dbm": rx_dbm,
        "frames_sent": b.rc.frames_sent, "frames_delivered": b.rc.frames_delivered,
        "deadline_ms": deadline_ms,
    }
    if t_zero is None:
        details["reason"] = "motors never reached 0"
        return TestResult(name, "FAIL", details)
    from_cut_ms = (t_zero - t_cut) / 1000
    from_last_ms = (t_zero - last_good) / 1000 if last_good is not None else from_cut_ms
    details.update(ms_from_cut=from_cut_ms, ms_from_last_good_frame=from_last_ms)
    verdict = "PASS" if from_last_ms <= deadline_ms else "FAIL"
    if verdict == "FAIL":
        details["reason"] = f"{from_last_ms:.1f} ms > {deadline_ms} ms"
    return TestResult(name, verdict, details)


# ---------------------------------------------------------------------------
# 7. Self-test
# ---------------------------------------------------------------------------


def _self_test() -> int:
    GOOD_ID, BAD_ID = "bf-4.5-test+abc123", "bf-4.5-test+bad999"
    good = FirmwareImage(build_id=GOOD_ID, failsafe_delay_ds=15).to_bytes()
    buggy = FirmwareImage(build_id=BAD_ID, failsafe_delay_ds=15,
                          stage2_drops_motors=False).to_bytes()
    # Spec for this example: drop within guard (1500 ms) + 50 ms margin.
    deadline = 1500 + 50
    failures = 0

    def check(label: str, cond: bool, res: TestResult) -> None:
        nonlocal failures
        status = "ok " if cond else "BAD"
        print(f"[{status}] {label}: verdict={res.verdict} {json.dumps(res.details, default=str)}")
        if not cond:
            failures += 1

    # 1) good firmware on a healthy bench -> PASS
    b = build_fake_bench(BENCH_CONFIG)
    r = test_rc_link_loss_failsafe(b, good, GOOD_ID, deadline)
    check("good fw, healthy bench", r.verdict == "PASS"
          and 1500 <= r.details["ms_from_last_good_frame"] <= deadline, r)

    # 2) buggy firmware (Stage 2 never drops) -> FAIL (test catches the bug)
    b = build_fake_bench(BENCH_CONFIG)
    r = test_rc_link_loss_failsafe(b, buggy, BAD_ID, deadline)
    check("buggy fw is caught", r.verdict == "FAIL", r)

    # 3) dead PSU -> INFRA_FAIL from smoke test, never blamed on firmware
    b = build_fake_bench(BENCH_CONFIG, dead=("psu",))
    r = test_rc_link_loss_failsafe(b, good, GOOD_ID, deadline)
    check("dead psu -> INFRA_FAIL", r.verdict == "INFRA_FAIL", r)

    # 4) corrupt image -> bricked detection after one power-cycle retry
    b = build_fake_bench(BENCH_CONFIG)
    r = test_rc_link_loss_failsafe(b, b"\x00garbage", GOOD_ID, deadline)
    check("corrupt image -> INFRA_FAIL (bricked?)", r.verdict == "INFRA_FAIL"
          and "build id" in r.details.get("reason", ""), r)

    # 5) RF budget sanity: 0 dB step -> link up; 90 dB step -> below sensitivity
    b = build_fake_bench(BENCH_CONFIG)
    b.atten.set_db(0.0)
    up = b.rc.rx_power_dbm()
    b.atten.set_db(90.0)
    down = b.rc.rx_power_dbm()
    ok = up >= -108.0 > down
    print(f"[{'ok ' if ok else 'BAD'}] rf budget: 0dB->{up} dBm, 90dB->{down} dBm (sens -108)")
    failures += 0 if ok else 1

    # 6) RTT is bidirectional: write a command down, read the answer up
    b = build_fake_bench(BENCH_CONFIG)
    bring_up(b, good, GOOD_ID)
    b.probe.rtt_read()
    b.probe.rtt_write("status\n")
    b.clock.sleep_ms(2)
    reply = b.probe.rtt_read()
    ok = "STATE DISARMED" in reply
    print(f"[{'ok ' if ok else 'BAD'}] rtt down->up: {reply.strip()!r}")
    failures += 0 if ok else 1

    print("SELF-TEST", "PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(_self_test())
