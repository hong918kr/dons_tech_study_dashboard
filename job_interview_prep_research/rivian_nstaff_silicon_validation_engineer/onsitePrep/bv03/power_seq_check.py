#!/usr/bin/env python3
"""BV03 power-up sequence checker (stdlib only).

Reads a multi-channel scope/DAQ export (CSV: time_s, <rail1>, <rail2>, ..., RESET_N)
and checks every rail against a spec table:
  - 10% / 90% crossing times (linear interpolation between samples)
  - power-up ORDER (by 10% crossing) vs spec order
  - GAP: this rail's 10% crossing minus previous rail's 90% crossing
  - RAMP time (10% -> 90%)
  - MONOTONIC ramp (no dip > MONO_DIP_PCT of nominal below the running max)
  - OVERSHOOT (peak above nominal, % of nominal)
  - FINAL value (mean of the last 1 ms) within +/- tolerance
  - RESET_N released (50% rising) >= reset_delay_min_ms after the LAST rail hit 90%

The spec numbers below are MOCK values for an imaginary automotive AI SoC [추정].
On a real board they come from the SoC datasheet "power sequencing" section and
the PMIC NVM/OTP sequence table.

Usage:
  python3 -I power_seq_check.py                 # generate mock_good.csv + mock_bad.csv, check both
  python3 -I power_seq_check.py check FILE.csv  # check one capture
Exit code: 0 = all PASS, 1 = at least one FAIL.
"""
import csv
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# --- spec table (mock) -------------------------------------------------------
# order = expected power-up position. gap = prev rail t90 -> this rail t10 (ms).
SPEC = [
    # name            Vnom  tol%  ramp_ms(min,max)  gap_ms(min,max)  ovs%max
    dict(name="VDD_AO_0V8",    vnom=0.80, tol=3.0, ramp=(0.2, 3.0), gap=(None, None), ovs=5.0),
    dict(name="VDD_CORE_0V75", vnom=0.75, tol=3.0, ramp=(0.2, 3.0), gap=(0.1, 5.0),  ovs=5.0),
    dict(name="VDDA_PHY_0V85", vnom=0.85, tol=3.0, ramp=(0.2, 3.0), gap=(0.1, 5.0),  ovs=5.0),
    dict(name="VDD_IO_1V8",    vnom=1.80, tol=5.0, ramp=(0.2, 3.0), gap=(0.1, 5.0),  ovs=8.0),
    dict(name="VDD_DDR_1V05",  vnom=1.05, tol=3.0, ramp=(0.2, 3.0), gap=(0.1, 5.0),  ovs=5.0),
    dict(name="VDD_IO_3V3",    vnom=3.30, tol=5.0, ramp=(0.2, 3.0), gap=(0.1, 5.0),  ovs=8.0),
]
RESET_NAME = "RESET_N"
RESET_VIO = 3.3            # RESET_N is a 3.3 V open-drain line in this mock
RESET_DELAY_MIN_MS = 2.0   # reset release >= 2 ms after last rail 90%
MONO_DIP_PCT = 3.0         # a dip > 3% of Vnom during the ramp = non-monotonic
SAMPLE_US = 10.0           # 100 kS/s mock capture
WINDOW_MS = 25.0


# --- mock waveform generator -------------------------------------------------
def rail_wave(t_ms, t0, tr, vnom, ovs_pct=0.0, dip_at=None, dip_pct=0.0):
    """Soft-start ramp starting at t0 lasting tr ms, optional overshoot ring and mid-ramp dip."""
    if t_ms < t0:
        return 0.0
    x = (t_ms - t0) / tr
    w = 0.25 if dip_at is not None else 0.0     # sag lasts 25% of the ramp time
    if dip_at is not None and dip_at <= x < dip_at + w:  # ramp stalls and sags, then resumes
        return vnom * dip_at - vnom * dip_pct / 100.0 * math.sin(math.pi * (x - dip_at) / w)
    if dip_at is not None and x >= dip_at + w:
        x -= w
    if x < 1.0:
        return max(vnom * x, 0.0)
    dt = t_ms - (t0 + tr * (1 + w))
    ring = vnom * ovs_pct / 100.0 * math.exp(-dt / 0.15) * math.cos(2 * math.pi * dt / 0.4)
    return vnom + ring


def reset_wave(t_ms, t_rel):
    if t_ms < t_rel:
        return 0.0
    return RESET_VIO * (1 - math.exp(-(t_ms - t_rel) / 0.05))   # RC rise via pull-up


def gen_capture(path, plan, t_reset, seed):
    rnd = random.Random(seed)
    names = [s["name"] for s in SPEC] + [RESET_NAME]
    n = int(WINDOW_MS * 1000 / SAMPLE_US)
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time_s"] + names)
        for i in range(n):
            t_ms = i * SAMPLE_US / 1000.0
            row = [f"{t_ms / 1000.0:.6f}"]
            for s in SPEC:
                p = plan[s["name"]]
                v = rail_wave(t_ms, p["t0"], p["tr"], s["vnom"], p.get("ovs", 1.0),
                              p.get("dip_at"), p.get("dip", 0.0))
                v += rnd.gauss(0.0, 0.002 * s["vnom"])          # 0.2% rms noise
                row.append(f"{v:.5f}")
            row.append(f"{reset_wave(t_ms, t_reset) + rnd.gauss(0, 0.005):.4f}")
            w.writerow(row)


GOOD_PLAN = {
    "VDD_AO_0V8":    dict(t0=1.0,  tr=1.0),
    "VDD_CORE_0V75": dict(t0=2.5,  tr=1.2),
    "VDDA_PHY_0V85": dict(t0=4.3,  tr=1.0),
    "VDD_IO_1V8":    dict(t0=6.0,  tr=1.5),
    "VDD_DDR_1V05":  dict(t0=8.2,  tr=1.0),
    "VDD_IO_3V3":    dict(t0=9.9,  tr=2.0, ovs=3.0),
}
# Deliberately broken board: 3V3 IO enable tied to the wrong PGOOD (comes up 2nd),
# DDR rail overshoots 14% (compensation / missing output cap), PHY rail sags mid-ramp
# (inrush hits an upstream rail), and reset is released only 0.6 ms after the last rail.
BAD_PLAN = {
    "VDD_AO_0V8":    dict(t0=1.0,  tr=1.0),
    "VDD_CORE_0V75": dict(t0=4.6,  tr=1.2),
    "VDDA_PHY_0V85": dict(t0=6.4,  tr=1.0, dip_at=0.55, dip=9.0),
    "VDD_IO_1V8":    dict(t0=8.1,  tr=1.5),
    "VDD_DDR_1V05":  dict(t0=10.3, tr=0.6, ovs=14.0),
    "VDD_IO_3V3":    dict(t0=2.4,  tr=2.0, ovs=3.0),
}


# --- analysis ----------------------------------------------------------------
def load_csv(path):
    with open(path, newline="") as f:
        r = csv.reader(f)
        hdr = next(r)
        cols = {h: [] for h in hdr}
        for row in r:
            for h, v in zip(hdr, row):
                cols[h].append(float(v))
    t_ms = [x * 1000.0 for x in cols.pop("time_s")]
    return t_ms, cols


def crossing(t, v, th):
    """First time v rises through th, linearly interpolated. None if never."""
    for i in range(1, len(v)):
        if v[i] >= th and v[i - 1] < th:
            return t[i - 1] + (th - v[i - 1]) * (t[i] - t[i - 1]) / (v[i] - v[i - 1])
    return t[0] if v and v[0] >= th else None


def analyze(path):
    t, cols = load_csv(path)
    res, fails = [], []
    for s in SPEC:
        v = cols[s["name"]]
        vn = s["vnom"]
        t10, t90 = crossing(t, v, 0.1 * vn), crossing(t, v, 0.9 * vn)
        r = dict(name=s["name"], vnom=vn, t10=t10, t90=t90)
        last = [x for tt, x in zip(t, v) if tt >= t[-1] - 1.0]
        r["final"] = sum(last) / len(last)
        r["peak"] = max(v)
        r["ovs"] = (r["peak"] - vn) / vn * 100.0
        # monotonic: largest drop below running max between t10 and t90
        dip, runmax = 0.0, -1e9
        if t10 is not None and t90 is not None:
            for tt, x in zip(t, v):
                if t10 <= tt <= t90:
                    runmax = max(runmax, x)
                    dip = max(dip, runmax - x)
        r["dip_pct"] = dip / vn * 100.0
        res.append(r)

    # per-rail checks
    for i, (s, r) in enumerate(zip(SPEC, res)):
        n = s["name"]
        if r["t10"] is None or r["t90"] is None:
            fails.append(f"{n}: never reached 90% of {s['vnom']} V")
            r["ramp"] = r["gap"] = None
            continue
        r["ramp"] = r["t90"] - r["t10"]
        lo, hi = s["ramp"]
        if not lo <= r["ramp"] <= hi:
            fails.append(f"{n}: ramp {r['ramp']:.2f} ms outside [{lo}, {hi}] ms")
        if r["ovs"] > s["ovs"]:
            fails.append(f"{n}: overshoot {r['ovs']:.1f}% > {s['ovs']}% (peak {r['peak']:.3f} V)")
        err = (r["final"] - s["vnom"]) / s["vnom"] * 100.0
        if abs(err) > s["tol"]:
            fails.append(f"{n}: final {r['final']:.3f} V ({err:+.1f}%) outside +/-{s['tol']}%")
        if r["dip_pct"] > MONO_DIP_PCT:
            fails.append(f"{n}: non-monotonic ramp, dip {r['dip_pct']:.1f}% of Vnom")
        r["gap"] = None
        if i > 0 and res[i - 1]["t90"] is not None:
            r["gap"] = r["t10"] - res[i - 1]["t90"]
            glo, ghi = s["gap"]
            if r["gap"] < glo or r["gap"] > ghi:
                fails.append(f"{n}: gap from {SPEC[i-1]['name']} 90% = {r['gap']:.2f} ms "
                             f"outside [{glo}, {ghi}] ms")

    # order check (by 10% crossing)
    seen = sorted([r for r in res if r["t10"] is not None], key=lambda r: r["t10"])
    actual = [r["name"] for r in seen]
    expected = [s["name"] for s in SPEC]
    if actual != expected:
        for pos, (a, e) in enumerate(zip(actual, expected)):
            if a != e:
                fails.append(f"ORDER: position {pos+1} is {a}, spec says {e}")
                break

    # reset check
    t90s = [r["t90"] for r in res if r["t90"] is not None]
    t_rst = crossing(t, cols[RESET_NAME], 0.5 * RESET_VIO)
    if t_rst is None:
        fails.append("RESET_N never released")
        rst_delay = None
    else:
        rst_delay = t_rst - max(t90s)
        if rst_delay < RESET_DELAY_MIN_MS:
            fails.append(f"RESET_N released {rst_delay:.2f} ms after last rail 90% "
                         f"(< {RESET_DELAY_MIN_MS} ms)")
    return t, res, actual, t_rst, rst_delay, fails


def fmt(x, w=6, p=2):
    return f"{x:{w}.{p}f}" if x is not None else " " * (w - 1) + "-"


def timeline(res, t_rst, t_end, cols=50):
    """ASCII timeline: '.' off, '/' ramping (10..90%), '=' up; '|' = RESET_N release."""
    out = []
    scale = cols / t_end
    for r in sorted(res, key=lambda r: (r["t10"] is None, r["t10"] or 0)):
        line = ""
        for c in range(cols):
            tc = (c + 0.5) / scale
            if r["t10"] is None or tc < r["t10"]:
                line += "."
            elif r["t90"] is None or tc < r["t90"]:
                line += "/"
            else:
                line += "="
        out.append(f"  {r['name']:<14} {line}")
    if t_rst is not None:
        pos = min(int(t_rst * scale), cols - 1)
        out.append(f"  {RESET_NAME:<14} " + "." * pos + "|" + "^" * (cols - pos - 1))
    out.append(f"  {'':<14} 0 ms" + " " * (cols - 9) + f"{t_end:.0f} ms")
    return out


def report(path):
    t, res, actual, t_rst, rst_delay, fails = analyze(path)
    print(f"=== {os.path.basename(path)}  ({len(t)} samples, {t[1]-t[0]:.3f} ms/sample) ===")
    print(f"  {'rail':<14} {'Vnom':>5} {'t10ms':>6} {'t90ms':>6} {'ramp':>5} {'gap':>6} "
          f"{'final':>6} {'ovs%':>5} {'dip%':>5}")
    for r in res:
        print(f"  {r['name']:<14} {r['vnom']:5.2f} {fmt(r['t10'])} {fmt(r['t90'])} "
              f"{fmt(r.get('ramp'), 5)} {fmt(r.get('gap'))} {r['final']:6.3f} "
              f"{r['ovs']:5.1f} {r['dip_pct']:5.1f}")
    print(f"  order seen : {' > '.join(a.split('_')[1] + '_' + a.split('_')[-1] for a in actual)}")
    print(f"  RESET_N 50%: {fmt(t_rst)} ms  (delay after last rail 90% = {fmt(rst_delay)} ms)")
    for line in timeline(res, t_rst, t[-1]):
        print(line)
    if fails:
        for f in fails:
            print(f"  FAIL  {f}")
        print(f"  RESULT: FAIL ({len(fails)} violations)\n")
    else:
        print("  RESULT: PASS\n")
    return not fails


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return 0 if report(argv[2]) else 1
    good = os.path.join(HERE, "mock_good.csv")
    bad = os.path.join(HERE, "mock_bad.csv")
    gen_capture(good, GOOD_PLAN, t_reset=14.5, seed=1)
    gen_capture(bad, BAD_PLAN, t_reset=11.5, seed=2)
    ok = [report(good), report(bad)]
    print(f"summary: {sum(ok)}/{len(ok)} captures PASS")
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
