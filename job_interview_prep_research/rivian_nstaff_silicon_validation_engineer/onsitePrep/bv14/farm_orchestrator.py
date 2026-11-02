#!/usr/bin/env python3
"""BV14 board-farm orchestrator skeleton (stdlib only, Python 3.9+).

Everything below the "MOCK HARDWARE" line is a stand-in for real lab gear:
  MockPDU      -> network PDU / relay board (real: SNMP/HTTP/SCPI, labgrid NetworkPowerPort)
  MockChamber  -> thermal chamber / thermostream (real: SCPI over LAN via PyVISA)
  MockUART     -> ser2net / console server port (real: pyserial or telnet socket)
  MockBoard    -> the DUT running bring-up test firmware; emits a UART log
                  according to a fault model (only the mock knows the fault).
Everything above it (log parser, hang detection, retry/flaky classification,
triage hints, JSON/text reports, repro packages) is the part you would keep.

Time is SIMULATED (SimClock) so a 128-test farm run finishes instantly and is
deterministic for a given --seed. Hang detection is real logic: a test is a
HANG if no UART line arrives within idle_timeout, or the test exceeds
test_timeout without a verdict line.

Usage:
  python3 -I farm_orchestrator.py                 # full mock farm run
  python3 -I farm_orchestrator.py --selftest      # parser / classifier controls
  python3 -I farm_orchestrator.py --seed 3 --retries 2 --out outputs
"""
import argparse
import json
import os
import random
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

TESTS = [  # (name, nominal duration s, test_timeout s)
    ("pmic_rails", 0.4, 5.0),
    ("pll_lock", 0.2, 5.0),
    ("ddr_march_c", 6.0, 30.0),
    ("i2c_scan", 0.5, 5.0),
    ("spi_flash_id", 0.3, 5.0),
    ("eth_phy_loopback", 1.5, 10.0),
    ("pcie_link", 1.0, 10.0),
    ("csi2_test_pattern", 2.0, 10.0),
]
CONDITIONS = [25, 85]          # chamber set points, deg C
IDLE_TIMEOUT = 3.0             # s without any UART line -> hang
BOOT_TIMEOUT = 8.0             # s to reach "SHELL READY"
HEARTBEAT_PERIOD = 1.0         # mock FW progress-line period (must be < IDLE_TIMEOUT)

# ============================================================================
# LOG PARSER  (the contract with the test firmware: one machine-readable line
# per verdict, anchored, so "BYPASS" or a printf in a driver never matches)
# ============================================================================
RE_TS = r"^\[\s*(?P<ts>\d+\.\d+)\]\s+"
RE_START = re.compile(RE_TS + r"T:(?P<test>[a-z0-9_]+) START$")
RE_PASS = re.compile(RE_TS + r"T:(?P<test>[a-z0-9_]+) PASS(?: (?P<kv>.*))?$")
RE_FAIL = re.compile(RE_TS + r"T:(?P<test>[a-z0-9_]+) FAIL code=(?P<code>0x[0-9A-Fa-f]{4})(?: (?P<kv>.*))?$")
RE_BOOT_OK = re.compile(RE_TS + r"SHELL READY$")
RE_RESET = re.compile(RE_TS + r"RESET_REASON=(?P<why>[A-Z_]+)$")
RE_FATAL = re.compile(RE_TS + r"(?:PANIC|EXCEPTION|DATA ABORT)\b(?P<rest>.*)$")


def parse_line(line):
    """Return (kind, fields) for one UART line. kind in START/PASS/FAIL/BOOT_OK/RESET/FATAL/OTHER."""
    for kind, rx in (("START", RE_START), ("PASS", RE_PASS), ("FAIL", RE_FAIL),
                     ("BOOT_OK", RE_BOOT_OK), ("RESET", RE_RESET), ("FATAL", RE_FATAL)):
        m = rx.match(line)
        if m:
            return kind, m.groupdict()
    return "OTHER", {}


def kv_to_dict(kv):
    out = {}
    for tok in (kv or "").split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def run_one_test(uart, clock, test, test_timeout, idle_timeout=IDLE_TIMEOUT):
    """Drive one test over the console and return an attempt record.

    verdict: PASS / FAIL / HANG.  signature: stable string used for dedup
    (test + code, or test + 'HANG@<last line kind>') - never includes
    timestamps or addresses, so the same bug dedups across boards.
    """
    uart.write("run %s\n" % test)
    t0 = clock.now
    lines = []
    started = False
    while True:
        if clock.now - t0 > test_timeout:
            return _attempt("HANG", "%s:HANG(test_timeout)" % test, lines, clock.now - t0,
                            "no verdict within %.1fs" % test_timeout)
        line = uart.readline(timeout=idle_timeout)
        if line is None:
            where = "after_START" if started else "before_START"
            return _attempt("HANG", "%s:HANG(%s)" % (test, where), lines, clock.now - t0,
                            "UART silent > %.1fs (%s)" % (idle_timeout, where))
        lines.append(line)
        kind, f = parse_line(line)
        if kind == "START" and f["test"] == test:
            started = True
        elif kind == "PASS" and f["test"] == test:
            return _attempt("PASS", "", lines, clock.now - t0, kv_to_dict(f.get("kv")))
        elif kind == "FAIL" and f["test"] == test:
            return _attempt("FAIL", "%s:%s" % (test, f["code"]), lines, clock.now - t0,
                            kv_to_dict(f.get("kv")))
        elif kind in ("FATAL", "RESET"):
            return _attempt("HANG", "%s:%s" % (test, kind), lines, clock.now - t0, line.strip())
        # OTHER / progress lines: keep reading


def _attempt(verdict, signature, lines, dur, detail):
    return {"verdict": verdict, "signature": signature, "dur_s": round(dur, 3),
            "detail": detail, "log": list(lines)}


def classify(attempts):
    """Retry/flaky policy. A retry may turn red into yellow, never into green.

    PASS  : first attempt passed
    FLAKY : at least one non-pass, then a pass (reported, counted, never hidden)
    FAIL  : every attempt FAIL with the same signature  (deterministic)
    HANG  : every attempt HANG with the same signature
    FAIL_INCONSISTENT : all attempts bad, signatures differ (two bugs, or noise)
    """
    v = [a["verdict"] for a in attempts]
    if v[0] == "PASS":
        return "PASS"
    if "PASS" in v:
        return "FLAKY"
    sigs = {a["signature"] for a in attempts}
    if len(sigs) == 1:
        return "HANG" if v[0] == "HANG" else "FAIL"
    return "FAIL_INCONSISTENT"


# ============================================================================
# MOCK HARDWARE
# ============================================================================
class SimClock:
    def __init__(self):
        self.now = 0.0

    def advance(self, dt):
        self.now += dt


class MockPDU:
    def __init__(self, clock):
        self.clock, self.state, self.events = clock, {}, []

    def set(self, outlet, on):
        self.state[outlet] = on
        self.events.append((round(self.clock.now, 3), outlet, "ON" if on else "OFF"))

    def cycle(self, outlet, off_time=2.0):
        self.set(outlet, False)
        self.clock.advance(off_time)       # let rails fully discharge
        self.set(outlet, True)


class MockChamber:
    def __init__(self, clock):
        self.clock, self.temp_c = clock, 25

    def set_and_soak(self, temp_c, soak_s=600.0):
        self.temp_c = temp_c
        self.clock.advance(soak_s)          # real: poll until stable, then soak


class MockUART:
    """Console port. readline(timeout) returns a line, or None if the board is
    silent for longer than timeout (clock advances by the timeout)."""

    def __init__(self, clock):
        self.clock, self.board, self.pending = clock, None, []

    def attach(self, board):
        self.board = board

    def feed(self, items):                  # items: list of (delay_s, text)
        self.pending = list(items)

    def write(self, cmd):
        self.feed(self.board.handle_command(cmd.strip()))

    def readline(self, timeout):
        if not self.pending:
            self.clock.advance(timeout)
            return None
        delay, text = self.pending[0]
        if delay > timeout:
            self.clock.advance(timeout)
            self.pending[0] = (delay - timeout, text)
            return None
        self.clock.advance(delay)
        self.pending.pop(0)
        return "[%9.3f] %s" % (self.clock.now, text)


class MockBoard:
    """DUT running bring-up FW. Behaviour comes from inventory fault_model."""

    def __init__(self, inv, rng, chamber):
        self.inv, self.rng, self.chamber = inv, rng, chamber
        self.fm = inv.get("fault_model", {})

    def boot_log(self):
        if self.fm.get("no_boot"):
            # BootROM prints, then nothing: e.g. PMIC fault before DDR init
            return [(0.05, "BOOTROM v2.1 mode=QSPI"), (0.2, "FSBL: PMIC init"),
                    (0.1, "FSBL: PMIC FAULT reg=0x40 (VDD_DDR UV)")]
        return [(0.05, "BOOTROM v2.1 mode=QSPI"), (0.2, "FSBL: PMIC init ok"),
                (0.3, "FSBL: PLL lock ok"), (0.9, "FSBL: DDR training PASS rank0 rank1"),
                (0.1, "RESET_REASON_LAST=POR"), (0.1, "SHELL READY")]

    def handle_command(self, cmd):
        test = cmd.split()[1]
        dur = dict((n, d) for n, d, _ in TESTS)[test]
        out = [(0.01, "T:%s START" % test)]
        hot = self.chamber.temp_c
        if self.fm.get("hang", {}).get("test") == test:
            out.append((0.2, "T:%s waiting for LTSSM L0" % test))
            return out                                   # then silence -> HANG
        if test == "i2c_scan" and self.fm.get("rev_b_eco_i2c"):
            out.append((dur, "T:i2c_scan FAIL code=0x0102 bus=3 addr=0x50 NACK"))
            return out
        hf = self.fm.get("hot_fail", {})
        im = self.fm.get("intermittent", {})
        if hf.get("test") == test and hot >= hf.get("temp_c", 999):
            return out + self._heartbeat(test, dur * 0.6) + [
                (0.1, "T:%s FAIL code=0x0003 addr=0x%08X exp=0xAAAAAAAA got=0xAAAA2AAA"
                 % (test, 0x80000000 + self.rng.randrange(0, 1 << 24, 4)))]
        if im.get("test") == test and self.rng.random() < im.get("p_fail", 0):
            return out + self._heartbeat(test, dur * 0.8) + [
                (0.1, "T:%s FAIL code=0x0003 addr=0x%08X exp=0x55555555 got=0x55555455"
                 % (test, 0x80000000 + self.rng.randrange(0, 1 << 24, 4)))]
        tail = " bytes=0x40000000 errors=0" if test == "ddr_march_c" else ""
        return out + self._heartbeat(test, dur) + [(0.05, "T:%s PASS%s" % (test, tail))]

    @staticmethod
    def _heartbeat(test, dur, period=None):
        """Long tests MUST print progress more often than the orchestrator's
        idle timeout, otherwise a slow-but-alive test is misread as a hang."""
        period = period or HEARTBEAT_PERIOD
        out, t = [], 0.0
        while dur - t > period:
            t += period
            out.append((period, "T:%s progress t=%.0fs" % (test, t)))
        out.append((max(dur - t, 0.0), "T:%s progress done" % test))
        return out


# ============================================================================
# ORCHESTRATOR
# ============================================================================
class Farm:
    def __init__(self, inventory, seed, retries, outdir):
        self.inv = inventory
        self.clock = SimClock()
        self.pdu = MockPDU(self.clock)
        self.chamber = MockChamber(self.clock)
        self.rng = random.Random(seed)
        self.retries = retries
        self.outdir = outdir
        self.seed = seed
        self.results = []                    # one row per (board, temp, test)
        self.board_health = {}               # sn -> {"status", "boot_failures", "power_cycles"}

    # --- board-level primitives ---------------------------------------------
    def power_cycle_and_boot(self, b, uart):
        self.pdu.cycle(b["sn"] + "@" + b["pdu"])
        self.board_health[b["sn"]]["power_cycles"] += 1
        uart.feed(MockBoard(b, self.rng, self.chamber).boot_log())
        lines, t0 = [], self.clock.now
        while self.clock.now - t0 < BOOT_TIMEOUT:
            line = uart.readline(timeout=IDLE_TIMEOUT)
            if line is None:
                break
            lines.append(line)
            if parse_line(line)[0] == "BOOT_OK":
                return True, lines
        return False, lines

    def health_check(self, b, uart):
        """Boot twice at most; a board that can't reach the shell is an INFRA
        problem (quarantine), not a test failure - it must not pollute pass rates."""
        h = self.board_health[b["sn"]]
        for _ in range(2):
            ok, lines = self.power_cycle_and_boot(b, uart)
            if ok:
                return True, lines
            h["boot_failures"] += 1
        h["status"] = "QUARANTINED"
        return False, lines

    # --- main loop ------------------------------------------------------------
    def run(self):
        for b in self.inv["boards"]:
            self.board_health[b["sn"]] = {"status": "ACTIVE", "boot_failures": 0, "power_cycles": 0}
        for temp in CONDITIONS:
            self.chamber.set_and_soak(temp)
            for b in self.inv["boards"]:
                if self.board_health[b["sn"]]["status"] != "ACTIVE":
                    for name, _, _ in TESTS:
                        self._row(b, temp, name, "NOT_RUN", [], "board quarantined")
                    continue
                board = MockBoard(b, self.rng, self.chamber)
                uart = MockUART(self.clock)
                uart.attach(board)
                ok, boot_lines = self.health_check(b, uart)
                if not ok:
                    self._write_repro(b, temp, "health_check",
                                      [{"verdict": "INFRA", "signature": "boot:no SHELL READY",
                                        "log": boot_lines, "detail": "", "dur_s": 0}])
                    for name, _, _ in TESTS:
                        self._row(b, temp, name, "NOT_RUN", [], "health check failed -> quarantined")
                    continue
                for name, _, tmo in TESTS:
                    attempts = []
                    for k in range(1 + self.retries):
                        a = run_one_test(uart, self.clock, name, tmo)
                        attempts.append(a)
                        if a["verdict"] == "PASS":
                            break
                        # bad attempt: recover to a known state before retrying
                        ok, _ = self.power_cycle_and_boot(b, uart)
                        if not ok:
                            break
                    cls = classify(attempts)
                    self._row(b, temp, name, cls, attempts, "")
                    if cls != "PASS":
                        self._write_repro(b, temp, name, attempts)
        return self.results

    def _row(self, b, temp, test, cls, attempts, note):
        sig = next((a["signature"] for a in attempts if a["verdict"] != "PASS"), "")
        self.results.append({
            "sn": b["sn"], "rev": b["rev"], "corner": b["corner"], "soc_lot": b["soc_lot"],
            "temp_c": temp, "test": test, "class": cls, "attempts": len(attempts),
            "verdicts": [a["verdict"] for a in attempts], "signature": sig,
            "detail": next((a["detail"] for a in attempts if a["verdict"] != "PASS"), ""),
            "note": note})

    def _write_repro(self, b, temp, test, attempts):
        """Repro package = everything someone else needs to reproduce without asking you."""
        d = os.path.join(self.outdir, "repro", "%s_%dC_%s" % (b["sn"], temp, test))
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "uart.log"), "w") as f:
            for i, a in enumerate(attempts):
                f.write("### attempt %d verdict=%s sig=%s\n" % (i + 1, a["verdict"], a["signature"]))
                f.write("\n".join(a["log"]) + "\n")
        meta = {k: b[k] for k in ("sn", "rev", "soc_lot", "corner", "rework", "pdu", "uart", "jtag")}
        meta.update({"temp_c": temp, "test": test, "fw_build": self.inv["fw_build"], "seed": self.seed,
                     "attempts": [{k: a[k] for k in ("verdict", "signature", "dur_s", "detail")}
                                  for a in attempts],
                     "repro_cmd": "python3 -I farm_orchestrator.py --loop %s %s 20 --temp %d --seed %d"
                                  % (b["sn"], test, temp, self.seed)})
        with open(os.path.join(d, "meta.json"), "w") as f:
            json.dump(meta, f, indent=2)


# ============================================================================
# TRIAGE + REPORTS
# ============================================================================
def triage_hints(results, boards):
    """Correlate each failure signature with board attributes. This is the
    'which variable does the failure follow?' question, automated."""
    bad = [r for r in results if r["class"] not in ("PASS", "NOT_RUN")]
    by_sig = {}
    for r in bad:
        # a flaky mismatch and a deterministic mismatch with the same code are
        # treated as different bugs until data says otherwise
        mode = "intermittent" if r["class"] == "FLAKY" else "deterministic"
        by_sig.setdefault((r["signature"], mode), []).append(r)
    ran = [r for r in results if r["class"] != "NOT_RUN"]
    hints = []
    for (sig, mode), rows in sorted(by_sig.items()):
        test = rows[0]["test"]
        boards_hit = sorted({r["sn"] for r in rows})
        temps_hit = sorted({r["temp_c"] for r in rows})
        ran_t = [r for r in ran if r["test"] == test]
        revs_hit = sorted({r["rev"] for r in rows})
        revs_ran = sorted({r["rev"] for r in ran_t})
        boards_ran = sorted({r["sn"] for r in ran_t})
        temps_ran = sorted({r["temp_c"] for r in ran_t})
        classes = sorted({r["class"] for r in rows})
        h = []
        if len(boards_hit) == len(boards_ran):
            h.append("all boards -> systemic: FW/silicon; bisect FW builds")
        else:
            rev_all = [s for s in boards_ran if any(r["sn"] == s and r["rev"] in revs_hit for r in ran_t)]
            if len(revs_hit) < len(revs_ran) and sorted(rev_all) == boards_hit:
                h.append("every rev %s board, no other rev -> follows board revision: diff ECO/BOM %s"
                         % ("/".join(revs_hit), "/".join(revs_ran)))
            elif len(boards_hit) == 1:
                bi = next(x for x in boards if x["sn"] == boards_hit[0])
                h.append("single board %s (rev %s, %s corner, %s, rework=%s) -> board-specific: "
                         "swap SoC / move to golden board" % (bi["sn"], bi["rev"], bi["corner"], bi["soc_lot"],
                                                             bi["rework"] or "none"))
        if mode == "intermittent" and len(rows) < 3:
            h.append("seen %d time(s) -> too few events to correlate with temperature; loop it first" % len(rows))
        elif len(temps_hit) < len(temps_ran):
            h.append("only at %s C -> temperature-correlated: margin (shmoo V x T, training eye)"
                     % "/".join(map(str, temps_hit)))
        if "FLAKY" in classes:
            h.append("intermittent -> measure rate (rule of three), stress to raise rate, keep in flaky list")
        if "HANG" in classes:
            h.append("hang -> JTAG halt at timeout to read PC/regs; check reset/strap of that block")
        hints.append({"signature": sig, "mode": mode, "test": test, "classes": classes, "boards": boards_hit,
                      "temps_c": temps_hit, "hints": h})
    return hints


SYM = {"PASS": "P", "FAIL": "F", "HANG": "H", "FLAKY": "K", "FAIL_INCONSISTENT": "X", "NOT_RUN": "-"}


def text_report(farm, hints):
    res, boards = farm.results, farm.inv["boards"]
    out = []
    out.append("BOARD FARM RUN  fw=%s  seed=%d  retries=%d  sim_time=%.0fs"
               % (farm.inv["fw_build"], farm.seed, farm.retries, farm.clock.now))
    out.append("cell = 25C/85C   P=pass  K=flaky(pass on retry)  F=fail  H=hang  X=inconsistent  -=not run")
    short = [n[:10] for n, _, _ in TESTS]
    out.append("%-4s %-3s %-2s | " % ("SN", "rev", "cn") + " ".join("%-10s" % s for s in short) + " | health")
    out.append("-" * (14 + 11 * len(TESTS) + 14))
    idx = {(r["sn"], r["temp_c"], r["test"]): r for r in res}
    for b in boards:
        cells = []
        for n, _, _ in TESTS:
            cells.append("/".join(SYM[idx[(b["sn"], t, n)]["class"]] for t in CONDITIONS))
        h = farm.board_health[b["sn"]]
        out.append("%-4s %-3s %-2s | " % (b["sn"], b["rev"], b["corner"]) + " ".join("%-10s" % c for c in cells)
                   + " | %s pc=%d" % (h["status"], h["power_cycles"]))
    out.append("")
    out.append("PER-TEST (excluding NOT_RUN)")
    out.append("%-18s %4s %4s %5s %4s %4s  %s" % ("test", "run", "pass", "flaky", "fail", "hang", "first-pass rate"))
    for n, _, _ in TESTS:
        rows = [r for r in res if r["test"] == n and r["class"] != "NOT_RUN"]
        c = lambda k: sum(1 for r in rows if r["class"] == k)
        fp = c("PASS") / len(rows) if rows else 0
        out.append("%-18s %4d %4d %5d %4d %4d  %5.1f%%" % (n, len(rows), c("PASS"), c("FLAKY"),
                                                           c("FAIL") + c("FAIL_INCONSISTENT"), c("HANG"), 100 * fp))
    out.append("")
    out.append("FAILURES (non-PASS rows, retries shown)")
    for r in res:
        if r["class"] not in ("PASS", "NOT_RUN"):
            out.append("  %s %2dC %-18s %-6s %-22s attempts=%s  %s"
                       % (r["sn"], r["temp_c"], r["test"], r["class"], r["signature"],
                          ",".join(r["verdicts"]), r["detail"] if isinstance(r["detail"], str) else
                          " ".join("%s=%s" % kv for kv in sorted(r["detail"].items()))))
    q = [sn for sn, h in farm.board_health.items() if h["status"] != "ACTIVE"]
    out.append("")
    out.append("FARM HEALTH: quarantined=%s (INFRA, excluded from pass rates)" % (",".join(q) or "none"))
    out.append("")
    out.append("TRIAGE HINTS (signature -> which variable it follows)")
    for h in hints:
        out.append("  %-26s %-13s boards=%s temps=%s classes=%s" % (h["signature"], h["mode"], ",".join(h["boards"]),
                                                              "/".join(map(str, h["temps_c"])), ",".join(h["classes"])))
        for line in h["hints"]:
            out.append("      - " + line)
    return "\n".join(out)


# ============================================================================
# LOOP MODE: turn "it failed once" into a rate with a confidence bound
# ============================================================================
def binom_cdf(k, n, p):
    from math import lgamma, log, exp
    if p <= 0.0:
        return 1.0
    if p >= 1.0:
        return 0.0 if k < n else 1.0
    tot = 0.0
    for i in range(k + 1):
        tot += exp(lgamma(n + 1) - lgamma(i + 1) - lgamma(n - i + 1) + i * log(p) + (n - i) * log(1 - p))
    return min(tot, 1.0)


def upper_bound(k, n, conf=0.95):
    """Clopper-Pearson one-sided upper bound; k=0 gives 1-(1-conf)^(1/n) ~ 3/n."""
    lo, hi = 0.0, 1.0
    for _ in range(100):
        mid = (lo + hi) / 2
        if binom_cdf(k, n, mid) > 1 - conf:
            lo = mid
        else:
            hi = mid
    return hi


def loop_mode(inv, sn, test, n, temp, seed):
    b = next(x for x in inv["boards"] if x["sn"] == sn)
    farm = Farm(inv, seed, 0, os.path.join(HERE, "outputs", "loop_scratch"))
    farm.board_health[sn] = {"status": "ACTIVE", "boot_failures": 0, "power_cycles": 0}
    farm.chamber.set_and_soak(temp, soak_s=0)
    uart = MockUART(farm.clock)
    uart.attach(MockBoard(b, farm.rng, farm.chamber))
    ok, _ = farm.power_cycle_and_boot(b, uart)
    if not ok:
        print("%s does not boot -> fix INFRA first" % sn)
        return 2
    tmo = dict((x, t) for x, _, t in TESTS)[test]
    fails, first = 0, None
    for i in range(1, n + 1):
        a = run_one_test(uart, farm.clock, test, tmo)
        if a["verdict"] != "PASS":
            fails += 1
            first = first or i
            farm.power_cycle_and_boot(b, uart)       # back to known state
    ub = upper_bound(fails, n)
    print("loop %s %s @%dC: %d fail / %d runs = %.2f%%  first_fail=%s  95%% upper bound=%.4f (1 in %.0f)"
          % (sn, test, temp, fails, n, 100.0 * fails / n, first, ub, 1 / ub))
    return 0


# ============================================================================
# SELF-TEST: negative/positive controls for the parser and classifier
# ============================================================================
def selftest():
    ok = True

    def check(name, cond):
        nonlocal ok
        print("  %-58s %s" % (name, "ok" if cond else "FAILED"))
        ok = ok and cond

    print("parser controls")
    check("PASS line parsed", parse_line("[    1.000] T:pll_lock PASS")[0] == "PASS")
    check("FAIL line + code parsed", parse_line("[    1.000] T:i2c_scan FAIL code=0x0102 addr=0x50")[1].get("code") == "0x0102")
    check("'BYPASS' inside text is NOT a pass", parse_line("[    1.000] clk: PLL BYPASS mode PASS")[0] == "OTHER")
    check("unanchored 'T:x PASS' mid-line is NOT a pass", parse_line("[    1.000] echo T:x PASS")[0] == "OTHER")
    check("PANIC detected as fatal", parse_line("[    2.000] PANIC: data abort at 0x0")[0] == "FATAL")

    print("hang detection controls")
    clk = SimClock()

    class Silent(object):
        def handle_command(self, cmd):
            return [(0.01, "T:pcie_link START")]
    u = MockUART(clk)
    u.attach(Silent())
    a = run_one_test(u, clk, "pcie_link", 10.0)
    check("START then silence -> HANG(after_START)", a["verdict"] == "HANG" and "after_START" in a["signature"])

    class Chatty(object):          # keeps printing progress forever, never a verdict
        def handle_command(self, cmd):
            return [(0.5, "T:ddr_march_c progress") for _ in range(100)]
    u.attach(Chatty())
    a = run_one_test(u, clk, "ddr_march_c", 5.0)
    check("chatty but no verdict -> HANG(test_timeout)", a["verdict"] == "HANG" and "test_timeout" in a["signature"])

    class WrongTest(object):       # verdict for a DIFFERENT test must not count
        def handle_command(self, cmd):
            return [(0.1, "T:other PASS")]
    u.attach(WrongTest())
    check("PASS for another test name is ignored", run_one_test(u, clk, "pll_lock", 5.0)["verdict"] == "HANG")

    print("classifier controls")
    P = {"verdict": "PASS", "signature": ""}
    F1 = {"verdict": "FAIL", "signature": "t:0x0003"}
    F2 = {"verdict": "FAIL", "signature": "t:0x0004"}
    H = {"verdict": "HANG", "signature": "t:HANG(after_START)"}
    check("P -> PASS", classify([P]) == "PASS")
    check("F,P -> FLAKY (retry never yields clean PASS)", classify([F1, P]) == "FLAKY")
    check("F,F,F same sig -> FAIL", classify([F1, F1, F1]) == "FAIL")
    check("H,H,H -> HANG", classify([H, H, H]) == "HANG")
    check("F(0x3),F(0x4),F(0x3) -> FAIL_INCONSISTENT", classify([F1, F2, F1]) == "FAIL_INCONSISTENT")
    print("SELFTEST %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--inventory", default=os.path.join(HERE, "inventory.json"))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--retries", type=int, default=2)
    ap.add_argument("--out", default=os.path.join(HERE, "outputs"))
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--loop", nargs=3, metavar=("SN", "TEST", "N"),
                    help="repeat one test N times on one board (no retries) and report rate + 95%% bound")
    ap.add_argument("--temp", type=int, default=25, help="chamber temp for --loop")
    ap.add_argument("--heartbeat", type=float, default=HEARTBEAT_PERIOD,
                    help="mock FW progress period; >%.0fs reproduces false HANGs" % IDLE_TIMEOUT)
    args = ap.parse_args()
    globals()["HEARTBEAT_PERIOD"] = args.heartbeat
    if args.selftest:
        return selftest()
    with open(args.inventory) as f:
        inv = json.load(f)
    if args.loop:
        return loop_mode(inv, args.loop[0], args.loop[1], int(args.loop[2]), args.temp, args.seed)
    if os.path.isdir(os.path.join(args.out, "repro")):
        shutil.rmtree(os.path.join(args.out, "repro"))
    os.makedirs(args.out, exist_ok=True)
    farm = Farm(inv, args.seed, args.retries, args.out)
    farm.run()
    hints = triage_hints(farm.results, inv["boards"])
    report = text_report(farm, hints)
    print(report)
    doc = {"fw_build": inv["fw_build"], "seed": args.seed, "retries": args.retries,
           "sim_time_s": round(farm.clock.now, 1), "conditions_c": CONDITIONS,
           "tests": [n for n, _, _ in TESTS], "board_health": farm.board_health,
           "pdu_events": len(farm.pdu.events), "results": farm.results, "triage": hints}
    with open(os.path.join(args.out, "results.json"), "w") as f:
        json.dump(doc, f, indent=2)
    with open(os.path.join(args.out, "summary.txt"), "w") as f:
        f.write(report + "\n")
    n_repro = len(os.listdir(os.path.join(args.out, "repro"))) if os.path.isdir(os.path.join(args.out, "repro")) else 0
    print("\nwrote %s, %s, %d repro packages under %s"
          % (os.path.relpath(os.path.join(args.out, "results.json"), HERE),
             os.path.relpath(os.path.join(args.out, "summary.txt"), HERE), n_repro,
             os.path.relpath(os.path.join(args.out, "repro"), HERE)))
    bad = sum(1 for r in farm.results if r["class"] not in ("PASS", "NOT_RUN"))
    return 1 if bad else 0           # CI-friendly exit code


if __name__ == "__main__":
    sys.exit(main())
