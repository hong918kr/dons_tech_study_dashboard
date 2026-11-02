#!/usr/bin/env python3
"""BV11 - instrument abstraction + automated rail-measurement sweep (mock lab).

What it shows
  * A thin driver layer (PSU / Scope / Mux) that only talks SCPI strings via
    .write() / .query() -- the same shape as a PyVISA resource. If pyvisa is
    importable and a real VISA address is given, the same drivers would run on
    real instruments; otherwise a mock backend is used (default here).
  * Mock instruments with an SCPI error queue (SYST:ERR?), a mock bench PSU
    (voltage setpoint, current limit, CC detection), a 4-channel mock scope
    (trigger on CH1, single capture, BW limit, AC coupling, waveform dump) and
    a relay mux that routes 3 of 6 rails to CH2-CH4 (CH1 = trigger reference).
  * The "6 rails on a 4-channel scope" answer: two captures, same trigger
    (VDD_CORE rising on CH1), so timings from both captures share one time axis.
  * Host-side measurement math: steady-state mean, 10-90 % rise time, 50 %
    crossing time, sequencing order/gap check, AC-coupled ripple Vpp with the
    20 MHz BW limit; probe ground-lead artifact demo.
  * Sweep across N mock boards -> PASS/FAIL table + CSV.

Run:  python3 -I rail_sweep.py [--boards N] [--scpi-log]
The SCPI command spelling is generic/illustrative; real vendors differ [추정].
"""
import csv
import math
import os
import random
import sys

# ----------------------------------------------------------------- spec ----
# (name, nominal V, tol fraction, ripple limit mVpp)  -- example spec, not a datasheet
RAILS = [
    ("VDD_CORE_0V75", 0.75, 0.03, 25.0),
    ("VDD_PHY_0V8",   0.80, 0.03, 25.0),
    ("VDD2_DDR_1V1",  1.10, 0.03, 40.0),
    ("VDDQ_DDR_0V6",  0.60, 0.03, 30.0),
    ("VDD_IO_1V8",    1.80, 0.05, 50.0),
    ("VDD_3V3",       3.30, 0.05, 60.0),
]
SEQ_ORDER = [r[0] for r in RAILS]     # required power-up order
MIN_GAP_MS = 0.20                     # min t50 gap between consecutive rails
PGOOD = "PGOOD_ALL"                   # 3.3 V logic, must rise after last rail t90
INPUT_V, INPUT_ILIM = 12.0, 2.0       # bench PSU setpoint / current limit

# ------------------------------------------------------------ board model --
class BoardModel:
    """Analog behaviour of one DUT board (what the probes 'see')."""

    def __init__(self, serial, seed, fault=None):
        self.serial, self.fault = serial, fault
        self.rng = random.Random(seed)
        self.rails = {}
        t = 0.0
        for name, nom, _tol, _rl in RAILS:
            self.rails[name] = {
                "v": nom * (1 + self.rng.uniform(-0.006, 0.006)),
                "start_ms": t,
                "ramp_ms": self.rng.uniform(0.25, 0.6),
                "ripple_mv": self.rng.uniform(3.0, 7.0),  # 1 MHz buck ripple, peak
            }
            t += self.rng.uniform(0.6, 0.9)
        last_end = max(r["start_ms"] + r["ramp_ms"] for r in self.rails.values())
        self.rails[PGOOD] = {"v": 3.3, "start_ms": last_end + 0.8,
                             "ramp_ms": 0.01, "ripple_mv": 0.0}
        self.load_a = self.rng.uniform(0.9, 1.2)          # input current @12 V
        if fault == "ddr_low":
            self.rails["VDD2_DDR_1V1"]["v"] = 1.10 * 0.94
        elif fault == "io_early":
            self.rails["VDD_IO_1V8"]["start_ms"] = -0.5
        elif fault == "core_ripple":
            self.rails["VDD_CORE_0V75"]["ripple_mv"] = 20.0
        self.powered = False
        self.power_events = 0

    def input_current(self, vset, ilim):
        """Returns (V_out, I_out) seen at the PSU terminals."""
        if not self.powered:
            return vset, 0.0
        if self.fault == "input_short":            # ~0.05 ohm short
            i = min(vset / 0.05, ilim)
            return i * 0.05, i                      # CC mode: voltage collapses
        i = self.load_a * (12.0 / vset)
        return (vset, i) if i < ilim else (vset * ilim / i, ilim)

    def ramp_wave(self, rail, t_ms, jitter):
        r = self.rails[rail]
        t0 = r["start_ms"] + jitter
        if t_ms <= t0:
            return 0.0
        x = min((t_ms - t0) / r["ramp_ms"], 1.0)
        return r["v"] * (0.5 - 0.5 * math.cos(math.pi * x))   # smooth S-ramp


# -------------------------------------------------------- mock instruments --
class MockInstrument:
    idn = "MOCK,GENERIC,0,0.0"

    def __init__(self, fixture):
        self.fx, self.errq, self.log = fixture, [], []

    def write(self, cmd):
        self.log.append(">> " + cmd)
        self.handle(cmd.strip(), query=False)

    def query(self, cmd):
        self.log.append(">> " + cmd)
        ans = self.handle(cmd.strip(), query=True)
        self.log.append("<< " + (ans if len(ans) < 60 else ans[:57] + "..."))
        return ans

    def handle(self, cmd, query):
        u = cmd.upper()
        if u == "*IDN?":
            return self.idn
        if u in ("*RST", "*CLS"):
            self.errq.clear()
            return ""
        if u == "*OPC?":
            return "1"
        if u == "SYST:ERR?":
            return self.errq.pop(0) if self.errq else '+0,"No error"'
        ans = self.dispatch(u)
        if ans is None:
            self.errq.append('-113,"Undefined header"')
            return "" if not query else "9.9E37"
        return ans

    def dispatch(self, u):
        return None


class MockPSU(MockInstrument):
    idn = "MOCKLAB,PSU-3CH,SN0001,1.0"

    def __init__(self, fx):
        super().__init__(fx)
        self.vset, self.ilim, self.on = 0.0, 0.1, False

    def dispatch(self, u):
        b = self.fx.board
        if u.startswith("VOLT "):
            self.vset = float(u.split()[1]); return ""
        if u.startswith("CURR "):
            self.ilim = float(u.split()[1]); return ""
        if u in ("OUTP ON", "OUTP OFF"):
            self.on = u.endswith("ON")
            b.powered = self.on
            if self.on:
                b.power_events += 1
                self.fx.on_power_up()
            return ""
        if u == "MEAS:VOLT?":
            return "%.4f" % (b.input_current(self.vset, self.ilim)[0] if self.on else 0.0)
        if u == "MEAS:CURR?":
            return "%.4f" % (b.input_current(self.vset, self.ilim)[1] if self.on else 0.0)
        return None


class MockMux(MockInstrument):
    """Relay mux: bank 1 -> CH2..4 = PHY, DDR, VDDQ ; bank 2 -> IO, 3V3, PGOOD."""
    idn = "MOCKLAB,RELAY-MUX,SN0002,1.0"
    BANKS = {1: ["VDD_PHY_0V8", "VDD2_DDR_1V1", "VDDQ_DDR_0V6"],
             2: ["VDD_IO_1V8", "VDD_3V3", PGOOD]}

    def __init__(self, fx):
        super().__init__(fx)
        self.bank = 1

    def dispatch(self, u):
        if u.startswith("ROUT:CLOS (@") and u.endswith(")"):
            bank = int(u[len("ROUT:CLOS (@"):-1])
            if bank not in self.BANKS:
                self.errq.append('-222,"Data out of range"'); return ""
            self.bank = bank; return ""
        if u == "ROUT:CLOS?":
            return str(self.bank)
        return None

    def channel_map(self):
        return {1: "VDD_CORE_0V75", **{i + 2: r for i, r in enumerate(self.BANKS[self.bank])}}


class MockScope(MockInstrument):
    idn = "MOCKLAB,DSO-4CH-1GHZ,SN0003,1.0"
    NPTS_SLOW, NPTS_FAST = 2000, 20000   # 5 us/pt power-up, 1 ns/pt ripple

    def __init__(self, fx):
        super().__init__(fx)
        self.tdiv, self.armed, self.captured = 1e-3, False, None
        self.npts = self.NPTS_SLOW
        self.bwl = {c: False for c in range(1, 5)}
        self.coup = {c: "DC" for c in range(1, 5)}
        self.src = 1

    def dispatch(self, u):
        if u.startswith(":TIM:SCAL "):
            self.tdiv = float(u.split()[1])
            self.npts = self.NPTS_SLOW if self.tdiv >= 1e-4 else self.NPTS_FAST
            return ""
        for c in range(1, 5):
            if u.startswith(":CHAN%d:BWL " % c):
                self.bwl[c] = u.endswith("ON"); return ""
            if u.startswith(":CHAN%d:COUP " % c):
                self.coup[c] = u.split()[1]; return ""
        if u.startswith(":TRIG:SOUR CHAN1") or u.startswith(":TRIG:LEV ") \
                or u.startswith(":TRIG:SLOP "):
            return ""
        if u == ":SING":
            self.armed, self.captured = True, None
            if self.tdiv < 1e-4:          # steady-state ripple capture: free-run
                self.captured = self.fx.capture(self, steady=True)
                self.armed = False
            return ""
        if u == ":TER?":                  # triggered event register
            return "1" if self.captured is not None else "0"
        if u.startswith(":WAV:SOUR CHAN"):
            self.src = int(u[-1]); return ""
        if u == ":WAV:XINC?":
            return "%.6e" % (self.tdiv * 10 / self.npts)
        if u == ":WAV:XOR?":
            return "%.6e" % (-self.tdiv if self.tdiv >= 1e-4 else 0.0)
        if u == ":WAV:DATA?":
            if self.captured is None:
                self.errq.append('-230,"Data corrupt or stale"'); return ""
            return ",".join("%.5f" % v for v in self.captured[self.src])
        return None


class Fixture:
    """Wires the mock instruments to the board currently in the socket."""

    def __init__(self, ground_lead="spring"):
        self.board = None
        self.ground_lead = ground_lead
        self.psu, self.mux, self.scope = MockPSU(self), MockMux(self), MockScope(self)

    def on_power_up(self):
        if self.scope.armed:
            self.scope.captured = self.capture(self.scope, steady=False)
            self.scope.armed = False

    def capture(self, sc, steady):
        b, rng = self.board, self.board.rng
        cmap = self.mux.channel_map()
        n, xinc = sc.npts, sc.tdiv * 10 / sc.npts
        out = {}
        if not steady:
            # trigger = CH1 (VDD_CORE) crossing 50 %; 1 div pre-trigger
            core = b.rails["VDD_CORE_0V75"]
            t_trig = core["start_ms"] + core["ramp_ms"] / 2
            for ch, rail in cmap.items():
                jit = 0.0 if ch == 1 else rng.gauss(0, 0.003)   # ms
                out[ch] = [b.ramp_wave(rail, t_trig + (-sc.tdiv + i * xinc) * 1e3, jit)
                           + rng.gauss(0, 0.002) for i in range(n)]
            return out
        fs = 1.0 / xinc
        alpha = 1 - math.exp(-2 * math.pi * 20e6 / fs)          # 1-pole 20 MHz
        for ch, rail in cmap.items():
            r = b.rails[rail]
            raw = []
            for i in range(n):
                t = i * xinc
                v = r["v"] + r["ripple_mv"] * 1e-3 * math.sin(2 * math.pi * 1e6 * t)
                v += rng.gauss(0, 0.0012)                        # scope+probe noise
                if self.ground_lead == "long":                   # ringing at buck edges
                    ph = (t * 1e6) % 1.0
                    v += 0.045 * math.exp(-ph / 0.02) * math.sin(2 * math.pi * 150e6 * t)
                raw.append(v)
            if sc.bwl[ch]:
                y, filt = raw[0], []
                for v in raw:
                    y += alpha * (v - y)
                    filt.append(y)
                raw = filt
            if sc.coup[ch] == "AC":
                m = sum(raw) / len(raw)
                raw = [v - m for v in raw]
            out[ch] = raw
        return out


# --------------------------------------------------------------- backend ----
def open_resource(addr, kind, fixture):
    """PyVISA if present and address is real; mock otherwise."""
    if not addr.startswith("MOCK::"):
        try:
            import pyvisa  # noqa: F401  (not used in this demo)
            rm = pyvisa.ResourceManager()
            return rm.open_resource(addr)
        except Exception as e:  # pyvisa missing or no VISA library
            print("  [backend] %s -> pyvisa unavailable (%s), using mock" %
                  (addr, type(e).__name__))
    return {"PSU": fixture.psu, "MUX": fixture.mux, "SCOPE": fixture.scope}[kind]


# --------------------------------------------------------------- drivers ----
class Instrument:
    def __init__(self, res, name):
        self.res, self.name = res, name

    def check_errors(self):
        errs = []
        while True:
            e = self.res.query("SYST:ERR?")
            if e.startswith("+0") or e.startswith("0"):
                return errs
            errs.append(e)


class PSU(Instrument):
    def setup(self, v, ilim):
        self.res.write("VOLT %.3f" % v)
        self.res.write("CURR %.3f" % ilim)

    def output(self, on):
        self.res.write("OUTP " + ("ON" if on else "OFF"))

    def measure(self):
        return float(self.res.query("MEAS:VOLT?")), float(self.res.query("MEAS:CURR?"))


class Mux(Instrument):
    def select(self, bank):
        self.res.write("ROUT:CLOS (@%d)" % bank)


class Scope(Instrument):
    def setup_powerup(self):
        self.res.write(":TIM:SCAL 1E-3")
        for c in range(1, 5):
            self.res.write(":CHAN%d:COUP DC" % c)
            self.res.write(":CHAN%d:BWL OFF" % c)
        self.res.write(":TRIG:SOUR CHAN1")
        self.res.write(":TRIG:LEV 0.375")
        self.res.write(":TRIG:SLOP POS")

    def setup_ripple(self, bwl=True):
        self.res.write(":TIM:SCAL 2E-6")
        for c in range(1, 5):
            self.res.write(":CHAN%d:COUP AC" % c)
            self.res.write(":CHAN%d:BWL %s" % (c, "ON" if bwl else "OFF"))

    def single(self):
        self.res.write(":SING")

    def triggered(self):
        return self.res.query(":TER?") == "1"

    def fetch(self, ch):
        self.res.write(":WAV:SOUR CHAN%d" % ch)
        xinc = float(self.res.query(":WAV:XINC?"))
        x0 = float(self.res.query(":WAV:XOR?"))
        y = [float(v) for v in self.res.query(":WAV:DATA?").split(",")]
        return x0, xinc, y


# ------------------------------------------------------ measurement math ---
def crossing(x0, xinc, y, level):
    for i in range(1, len(y)):
        if y[i - 1] < level <= y[i]:
            f = (level - y[i - 1]) / (y[i] - y[i - 1])
            return x0 + (i - 1 + f) * xinc
    return None


def ramp_metrics(x0, xinc, y):
    vfin = sum(y[-len(y) // 5:]) / (len(y) // 5)
    t10, t50, t90 = (crossing(x0, xinc, y, vfin * k) for k in (0.1, 0.5, 0.9))
    rise = (t90 - t10) if (t10 is not None and t90 is not None) else None
    return {"v": vfin, "t50": t50, "t90": t90, "rise": rise}


def vpp(y):
    return (max(y) - min(y)) * 1e3


# ------------------------------------------------------------ test flow ----
def test_board(fx, psu, mux, scope, board):
    fx.board = board
    res = {"serial": board.serial, "rows": [], "verdict": "PASS", "notes": []}
    psu.output(False)
    psu.setup(INPUT_V, INPUT_ILIM)
    # --- 1. power-up captures (2 x 4 channels, shared CH1 trigger) ---------
    scope.setup_powerup()
    ramps = {}
    for bank in (1, 2):
        mux.select(bank)
        scope.single()
        psu.output(True)
        vin, iin = psu.measure()
        if iin >= 0.98 * INPUT_ILIM and vin < 0.9 * INPUT_V:
            psu.output(False)
            res["verdict"] = "FAIL"
            res["notes"].append("input in CC: %.2f V / %.2f A -> abort (short?)" % (vin, iin))
            res["iin"] = iin
            return res
        res["iin"] = iin
        if not scope.triggered():
            res["verdict"] = "FAIL"; res["notes"].append("no trigger"); return res
        for ch, rail in fx.mux.channel_map().items():   # (operator knows the map)
            if ch == 1 and bank == 2:
                continue
            ramps[rail] = ramp_metrics(*scope.fetch(ch))
        psu.output(False)
    # --- 2. steady-state ripple (AC coupled, 20 MHz BWL) -------------------
    psu.output(True)
    scope.setup_ripple(bwl=True)
    ripple = {}
    for bank in (1, 2):
        mux.select(bank)
        scope.single()
        for ch, rail in fx.mux.channel_map().items():
            if rail != PGOOD:
                ripple[rail] = vpp(scope.fetch(ch)[2])
    psu.output(False)
    errs = scope.check_errors() + psu.check_errors() + mux.check_errors()
    if errs:
        res["notes"].append("SCPI errors: %s" % errs)
    # --- 3. judge -----------------------------------------------------------
    for name, nom, tol, rlim in RAILS:
        m = ramps[name]
        ok_v = abs(m["v"] - nom) <= nom * tol
        ok_r = ripple[name] <= rlim
        res["rows"].append({
            "board": board.serial, "rail": name, "nominal_v": nom,
            "meas_v": round(m["v"], 4), "err_pct": round((m["v"] / nom - 1) * 100, 2),
            "t50_ms": round(m["t50"] * 1e3, 3) or 0.0, "rise_us": round(m["rise"] * 1e6, 1),
            "ripple_mvpp": round(ripple[name], 1), "ripple_lim": rlim,
            "v_ok": ok_v, "ripple_ok": ok_r})
        if not ok_v:
            res["notes"].append("%s %.3f V (%.1f%%) out of +/-%.0f%%" %
                                (name, m["v"], (m["v"] / nom - 1) * 100, tol * 100))
        if not ok_r:
            res["notes"].append("%s ripple %.1f mVpp > %.0f" % (name, ripple[name], rlim))
    order = sorted(SEQ_ORDER, key=lambda r: ramps[r]["t50"])
    if order != SEQ_ORDER:
        bad = [r for r, s in zip(order, SEQ_ORDER) if r != s][0]
        res["notes"].append("sequence violation: order=%s (first wrong: %s)" %
                            (" > ".join(order), bad))
    else:
        for a, b in zip(SEQ_ORDER, SEQ_ORDER[1:]):
            gap = (ramps[b]["t50"] - ramps[a]["t50"]) * 1e3
            if gap < MIN_GAP_MS:
                res["notes"].append("gap %s->%s %.2f ms < %.2f" % (a, b, gap, MIN_GAP_MS))
    last_t90 = max(ramps[r]["t90"] for r in SEQ_ORDER)
    pg = ramps[PGOOD]["t50"]
    res["pgood_ms"] = round((pg - last_t90) * 1e3, 3)
    if pg <= last_t90:
        res["notes"].append("PGOOD before last rail settled")
    if res["notes"]:
        res["verdict"] = "FAIL"
    return res


def ground_lead_demo(fx, psu, mux, scope, board):
    fx.board = board
    psu.setup(INPUT_V, INPUT_ILIM)
    psu.output(True)
    mux.select(1)
    rows = []
    for lead, bwl in (("long", False), ("long", True), ("spring", False), ("spring", True)):
        fx.ground_lead = lead
        scope.setup_ripple(bwl=bwl)
        scope.single()
        rows.append((lead, "ON " if bwl else "OFF", vpp(scope.fetch(1)[2])))
    psu.output(False)
    fx.ground_lead = "spring"
    return rows


def main(argv):
    nboards = 6
    if "--boards" in argv:
        nboards = int(argv[argv.index("--boards") + 1])
    show_log = "--scpi-log" in argv
    faults = [None, None, "ddr_low", "io_early", "core_ripple", "input_short"]
    boards = [BoardModel("B%02d" % (i + 1), 1000 + i,
                         faults[i] if i < len(faults) else None) for i in range(nboards)]

    fx = Fixture()
    print("BV11 rail sweep -- backend selection")
    psu = PSU(open_resource("MOCK::PSU::INSTR", "PSU", fx), "psu")
    mux = Mux(open_resource("MOCK::MUX::INSTR", "MUX", fx), "mux")
    scope = Scope(open_resource("TCPIP0::192.168.0.50::inst0::INSTR", "SCOPE", fx), "scope")
    for inst in (psu, mux, scope):
        print("  %-5s *IDN? -> %s" % (inst.name, inst.res.query("*IDN?")))

    print("\nProbe/ground-lead demo on %s VDD_CORE ripple (AC, 2 us/div):" % boards[0].serial)
    for lead, bwl, v in ground_lead_demo(fx, psu, mux, scope, boards[0]):
        print("  ground=%-6s BWL20M=%s -> %6.1f mVpp" % (lead, bwl, v))

    results = []
    for b in boards:
        for inst in (psu, mux, scope):
            inst.res.log.clear()
        results.append(test_board(fx, psu, mux, scope, b))
        if show_log and b is boards[0]:
            print("\nSCPI log, scope, board %s (first 18 lines):" % b.serial)
            for line in scope.res.log[:18]:
                print("   ", line)

    print("\nPer-rail results (board B01):")
    print("  %-14s %7s %8s %7s %7s %8s %9s" %
          ("rail", "nom V", "meas V", "err %", "t50 ms", "rise us", "rip mVpp"))
    for r in results[0]["rows"]:
        print("  %-14s %7.3f %8.4f %7.2f %7.3f %8.1f %9.1f" %
              (r["rail"], r["nominal_v"], r["meas_v"], r["err_pct"], r["t50_ms"],
               r["rise_us"], r["ripple_mvpp"]))
    print("  PGOOD after last rail t90: %.3f ms" % results[0]["pgood_ms"])

    print("\nBoard summary:")
    print("  %-5s %-7s %-6s %s" % ("board", "verdict", "Iin A", "notes"))
    for r in results:
        print("  %-5s %-7s %-6.3f %s" % (r["serial"], r["verdict"], r.get("iin", 0.0),
                                         "; ".join(r["notes"]) or "-"))

    outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "rail_sweep_results.csv")
    fields = ["board", "rail", "nominal_v", "meas_v", "err_pct", "t50_ms", "rise_us",
              "ripple_mvpp", "ripple_lim", "v_ok", "ripple_ok"]
    nrows = 0
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in results:
            for row in r["rows"]:
                w.writerow(row); nrows += 1
    npass = sum(r["verdict"] == "PASS" for r in results)
    print("\nwrote %s (%d rows); %d/%d boards PASS" %
          (os.path.relpath(path, os.path.dirname(os.path.abspath(__file__))),
           nrows, npass, len(results)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
