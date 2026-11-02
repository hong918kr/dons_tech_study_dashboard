#!/usr/bin/env python3
"""gen_captures.py -- N07B: synthesize mock I2C captures (stdlib only).

Each capture is what a 2-threshold digitizer (scope/MSO export, or an LA with two
comparator levels) would give you: every line is sampled against 0.3*VDD and 0.7*VDD,
so each line is in state 0 (< 30 %), m (between 30 and 70 %) or 1 (> 70 %).
Rows are written only when either line changes:  "<t_ns> <scl> <sda>".

Bus model (simplified, documented in the N07B note section 8):
  - falling edge: line crosses 70 % at t, 30 % at t + tf            (fast, driver pulls down)
  - rising edge : released at t, RC charge: 30 % at t + 0.421*tr, 70 % at t + 1.421*tr
                  (0.3566*RC and 1.2040*RC, tr = 0.8473*RC per UM10204 section 7.1)
  - controller with clock synchronisation: it releases SCL after LCNT, then counts HCNT only
    after it SEES SCL above 70 % -> slow rise lowers fSCL instead of shortening tHIGH.
  - a target's data/ACK output changes `tvd` after SCL crossed 30 % going low.
Cases: golden, B3 slow rise, B4 STOP between write and read, B5 SCL glitch -> arbitration
lost, B7 clock stretch longer than controller timeout.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
RISE30, RISE70 = 0.421, 1.421


class Bus:
    def __init__(self, p):
        self.p = p
        self.ev = {"scl": [(0, "1")], "sda": [(0, "1")]}
        self.lvl = {"scl": 1, "sda": 1}
        self.t_fall = None        # time SCL started falling (70 % crossing) for current low phase
        self.t = 2000             # idle lead-in

    def _add(self, line, t, s):
        assert t >= self.ev[line][-1][0], (line, t, self.ev[line][-1])
        self.ev[line].append((int(round(t)), s))

    def drive(self, line, v, t):
        """v=0: pull low starting at t; v=1: release at t (RC rise)."""
        if v == self.lvl[line]:
            return
        if v == 0:
            self._add(line, t, "m")
            self._add(line, t + self.p["tf"], "0")
        else:
            self._add(line, t + RISE30 * self.p["tr"], "m")
            self._add(line, t + RISE70 * self.p["tr"], "1")
        self.lvl[line] = v

    def glitch_scl(self, t, w30=60):
        """crosstalk dip on SCL while it is high: below 30 % for w30 ns."""
        self._add("scl", t, "m")
        self._add("scl", t + 5, "0")
        self._add("scl", t + 5 + w30, "m")
        self._add("scl", t + 5 + w30 + 45, "1")

    # --- SCL low-phase helpers -------------------------------------------------
    def sda_in_low(self, v, delay):
        self.drive("sda", v, self.t_fall + self.p["tf"] + delay)

    def clock(self, stretch_until=None, glitch_at=None, last=False):
        p = self.p
        rel = self.t_fall + p["lcnt"]
        if stretch_until is not None:
            rel = max(rel, stretch_until)
        self.drive("scl", 1, rel)
        t_hi = rel + RISE70 * p["tr"]
        if glitch_at is not None:
            self.glitch_scl(t_hi + glitch_at)
        if last:                       # controller stops clocking (abort): SCL stays high
            self.t = t_hi
            return
        self.t_fall = t_hi + p["hcnt"]
        self.drive("scl", 0, self.t_fall)

    # --- protocol pieces --------------------------------------------------------
    def start(self):
        p = self.p
        self.drive("sda", 0, self.t)
        self.t_fall = self.t + p["tf"] + p["thdsta"]
        self.drive("scl", 0, self.t_fall)

    def rep_start(self):
        p = self.p
        self.sda_in_low(1, p["hold"])
        rel = self.t_fall + p["lcnt"]
        self.drive("scl", 1, rel)
        t_hi = rel + RISE70 * p["tr"]
        t_sda = t_hi + p["tsusta"]
        self.drive("sda", 0, t_sda)
        self.t_fall = t_sda + p["tf"] + p["thdsta"]
        self.drive("scl", 0, self.t_fall)

    def stop(self):
        p = self.p
        self.sda_in_low(0, p["hold"])
        rel = self.t_fall + p["lcnt"]
        self.drive("scl", 1, rel)
        t_hi = rel + RISE70 * p["tr"]
        self.drive("sda", 1, t_hi + p["tsusto"])
        self.t = t_hi + p["tsusto"] + RISE70 * p["tr"] + p["tbuf"]

    def byte(self, val, master_sends, ack, stretch_after_ack=None):
        """master_sends: True = controller drives data (target ACKs), False = target drives data."""
        p = self.p
        d_data = p["hold"] if master_sends else p["tvd"]
        d_ack = p["tvd"] if master_sends else p["hold"]
        for i in range(7, -1, -1):
            self.sda_in_low((val >> i) & 1, d_data)
            self.clock()
        self.sda_in_low(ack, d_ack)
        self.clock()


def write_capture(name, bus, header):
    rows = []
    times = sorted({t for line in ("scl", "sda") for t, _ in bus.ev[line]})
    times.append(int(bus.t) + bus.p.get("tail", 20000))
    state = {"scl": "1", "sda": "1"}
    idx = {"scl": 0, "sda": 0}
    for t in times:
        for line in ("scl", "sda"):
            evs = bus.ev[line]
            while idx[line] < len(evs) and evs[idx[line]][0] <= t:
                state[line] = evs[idx[line]][1]
                idx[line] += 1
        row = (t, state["scl"], state["sda"])
        if not rows or rows[-1][1:] != row[1:] or t == times[-1]:
            rows.append(row)
    path = os.path.join(HERE, name)
    with open(path, "w") as f:
        f.write("# I2C mock capture -- 2-threshold export: 0 = below 0.3*VDD, m = between, 1 = above 0.7*VDD\n")
        for k, v in header.items():
            f.write(f"# {k}: {v}\n")
        f.write("# t_ns scl sda\n")
        for t, a, b in rows:
            f.write(f"{t} {a} {b}\n")
    return path, len(rows)


# Fast-mode controller: LCNT/HCNT chosen so the golden bus runs at ~397 kHz
FM = dict(lcnt=1550, hcnt=800, tr=120, tf=25, hold=300, tvd=850,
          thdsta=700, tsusta=700, tsusto=700, tbuf=1500)
SM = dict(lcnt=5400, hcnt=4200, tr=300, tf=30, hold=300, tvd=300,
          thdsta=4500, tsusta=5000, tsusto=4500, tbuf=5000)


def reg_read(bus, addr, reg, val, use_sr=True, expect_val=None):
    bus.start()
    bus.byte(addr << 1, True, 0)
    bus.byte(reg, True, 0)
    if use_sr:
        bus.rep_start()
    else:
        bus.stop()
        bus.start()
    bus.byte((addr << 1) | 1, True, 0)
    bus.byte(val, False, 1)            # controller NACKs the last byte
    bus.stop()


def main():
    out = []
    # golden: 2.2 kOhm-class pull-up, tr 120 ns, Sr register read -> 0xB5
    b = Bus(dict(FM))
    reg_read(b, 0x48, 0x0F, 0xB5)
    out.append(write_capture("cap_golden.txt", b, {
        "case": "golden -- Fm register read with repeated START, healthy edges",
        "mode": "fm", "expect": "OK", "expect_read": "0xB5"}))

    # B3: same parts, weak pull-up / heavy bus -> tr 700 ns (e.g. 4.7 kOhm x 175 pF)
    b = Bus(dict(FM, tr=700))
    reg_read(b, 0x48, 0x0F, 0xB5)
    out.append(write_capture("cap_b3_slow_rise.txt", b, {
        "case": "B3 -- fails only at 400 kHz: tr 700 ns, target data valid late",
        "mode": "fm", "expect": "SLOW_RISE", "expect_read": "0xB5"}))

    # B4: driver splits write-pointer / read with STOP; device resets pointer -> returns reg 0x00
    b = Bus(dict(FM))
    reg_read(b, 0x48, 0x0F, 0x00, use_sr=False)
    out.append(write_capture("cap_b4_stop_between.txt", b, {
        "case": "B4 -- read returns another register: STOP+START instead of Sr",
        "mode": "fm", "expect": "WRITE_STOP_READ", "expect_read": "0xB5"}))

    # B5: single controller, crosstalk glitch on SCL during bit b4 of the register byte.
    # The target counts the glitch as a clock, ACKs one bit early (during b0), the
    # controller sends b0=1, sees SDA low -> arbitration lost, stops clocking.
    p = dict(FM)
    b = Bus(p)
    b.start()
    b.byte(0x48 << 1, True, 0)
    reg = 0x0F
    for n, i in enumerate(range(7, -1, -1)):
        bit = (reg >> i) & 1
        if i == 0:
            b.sda_in_low(0, p["tvd"])          # target's early ACK pulls SDA low
            b.clock(last=True)                 # controller samples 0 != 1 -> ARB lost
            break
        b.sda_in_low(bit, p["hold"])
        b.clock(glitch_at=300 if i == 4 else None)
    out.append(write_capture("cap_b5_glitch_arb.txt", b, {
        "case": "B5 -- single controller reports arbitration lost",
        "mode": "fm", "expect": "ARB_LOST_GLITCH"}))

    # B7: Sm sensor stretches SCL 12 ms after ACKing the read address (conversion);
    # controller SCL-low timeout is 10 ms -> it aborts; target then releases SCL and drives b7=0.
    p = dict(SM, tail=5_000_000)
    b = Bus(p)
    b.start()
    b.byte(0x40 << 1, True, 0)
    b.byte(0xE3, True, 0)
    b.rep_start()
    b.byte((0x40 << 1) | 1, True, 0)      # target ACK; SDA stays low for data b7 = 0
    t_release = b.t_fall + 12_000_000
    b.sda_in_low(0, p["tvd"])
    b.clock(stretch_until=t_release, last=True)
    out.append(write_capture("cap_b7_stretch_timeout.txt", b, {
        "case": "B7 -- sensor clock-stretches 12 ms, controller timeout 10 ms",
        "mode": "sm", "expect": "STRETCH_TIMEOUT", "ctrl_timeout_us": "10000"}))

    for path, n in out:
        print(f"wrote {os.path.basename(path)} ({n} rows)")


if __name__ == "__main__":
    main()
