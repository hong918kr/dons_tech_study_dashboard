#!/usr/bin/env python3
"""bringup_log.py -- Day-1 bring-up checklist evaluator + log generator (BV06)

Mock data only (no real board). Every number below is an [example] value for a
fictitious automotive AI SoC validation board, NOT from any real datasheet.

What it does
  * evaluates each board's Day-1 measurements step by step, in order
  * a failed step BLOCKS every later step (gate rule: never debug layer N+1
    before layer N is proven)
  * prints a bring-up log per board with pass criteria, measured value, verdict,
    and the "next action" hint for a failure
  * prints a cross-board comparison against the golden board (SN001)

Run:  python3 -I bringup_log.py
"""

# ---------------------------------------------------------------- board spec
# rail: nominal V, tolerance %, min expected rail-to-GND resistance (ohm) when
#       unpowered, power-up order index (1 = first), example idle current (A)
RAILS = [
    # name          V      tol%  min_ohm  order  idle_A
    ("VDD_AON_0V8", 0.80,  3.0,  20.0,    1,     0.02),
    ("VDD_CORE_0V75", 0.75, 3.0, 0.5,     2,     0.60),
    ("VDD_DDR_1V05", 1.05, 3.0,  5.0,     3,     0.15),
    ("VDD_IO_1V8",  1.80,  5.0,  50.0,    4,     0.05),
    ("VDD_IO_3V3",  3.30,  5.0,  100.0,   5,     0.03),
]
VIN = 12.0                       # bench supply voltage (V)
ILIMIT_STAGES = [0.10, 0.30, 1.0]  # staged current limits (A) at VIN
EXPECTED_VIN_A = 0.25            # expected VIN current with SoC held in reset (A), example
OSC_NOMINAL_HZ = 24_000_000
OSC_TOL_PPM = 100                # example budget
RESET_MIN_DELAY_MS = 10.0        # reset release >= 10 ms after last PGOOD (example)
EXPECTED_IDCODE = 0x4BA00477     # Arm JTAG-DP (real-world value), other TAPs omitted
UART_EXPECT = "hello from SRAM"

HINTS = {
    "visual": "rework before power: check polarity/pin-1, bridges under microscope",
    "rail_res": "short suspected: thermal camera at low current, lift 0-ohm/ferrite to split the rail, compare golden",
    "first_power": "current hit limit or >1.5x expected: power off, back to rail-to-GND, thermal image, check reversed parts",
    "rails": "check PMIC fault/status regs over I2C, enable/PGOOD chain, feedback resistors, load on that rail",
    "sequence": "compare scope capture vs SoC power-sequence spec; PMIC OTP/sequence config; enable RC delays",
    "clock": "probe oscillator OUTPUT (not crystal pins), check osc supply/enable, load caps; use clock-out pin",
    "reset": "scope reset vs last PGOOD; supervisor threshold; who holds reset (debugger SRST? watchdog?)",
    "jtag": "VTref present? TCK lower (100 kHz)? TDO on scope; chain/irlen; held in reset? debug power domain",
    "sram_uart": "JTAG: read PC/fault regs; UART pinmux, baud vs real clock, TX/RX swap, level (1.8 vs 3.3 V)",
}


def pct_err(meas, nom):
    return (meas - nom) / nom * 100.0


def evaluate(board, golden=None):
    """Return list of (step, criterion, measured, verdict, hint)."""
    rows = []
    blocked_by = None

    def add(step, crit, meas, ok, hint_key):
        nonlocal blocked_by
        if blocked_by:
            rows.append((step, crit, "-", f"SKIP (blocked by {blocked_by})", ""))
            return
        verdict = "PASS" if ok else "FAIL"
        rows.append((step, crit, meas, verdict, "" if ok else HINTS[hint_key]))
        if not ok:
            blocked_by = step.split()[0]

    m = board["meas"]
    # 1 visual
    add("S1 visual", "no missing/rotated parts, no bridges", m["visual"], m["visual"] == "clean", "visual")
    # 2 rail-to-GND resistance (unpowered)
    bad = []
    for name, _v, _t, min_ohm, _o, _i in RAILS:
        r = m["rail_ohm"][name]
        g = golden["meas"]["rail_ohm"][name] if golden else None
        if r < min_ohm or (g and abs(r - g) / g > 0.5):
            bad.append(f"{name}={r}ohm(min {min_ohm}, golden {g})")
    add("S2 rail-GND", "each rail >= min ohm and within 50% of golden", "; ".join(bad) or "all ok", not bad, "rail_res")
    # 3 staged current-limited power-on
    stage_txt, ok = [], True
    for lim, cur in zip(ILIMIT_STAGES, m["vin_current_A"]):
        cc = cur >= lim * 0.98           # supply went into constant-current mode
        stage_txt.append(f"{lim:.2f}A lim -> {cur:.3f}A{' CC!' if cc else ''}")
        if cc and lim == ILIMIT_STAGES[-1]:
            ok = False
        if not cc:
            break                        # current settled below the limit: stop raising it
    settled = m["vin_current_A"][len(stage_txt) - 1]
    if settled > EXPECTED_VIN_A * 1.5:
        ok = False
    add("S3 first power", f"settles below limit, <= 1.5x expected {EXPECTED_VIN_A}A", ", ".join(stage_txt), ok, "first_power")
    # 4 rail voltages
    bad = [f"{n}={m['rail_V'][n]:.3f}V({pct_err(m['rail_V'][n], v):+.1f}%)"
           for n, v, t, *_ in RAILS if abs(pct_err(m["rail_V"][n], v)) > t]
    add("S4 rail V", "every rail within tolerance (DMM at load-side test point)", "; ".join(bad) or "all within tol", not bad, "rails")
    # 5 sequence + PGOOD
    order = [n for n, _ in sorted(m["rail_on_ms"].items(), key=lambda kv: kv[1])]
    exp = [r[0] for r in sorted(RAILS, key=lambda r: r[4])]
    add("S5 sequence", "rail 90% points in spec order, all PGOOD high", " > ".join(o.replace("VDD_", "") for o in order)
        + f", PGOOD={'all' if m['pgood_all'] else 'NOT all'}", order == exp and m["pgood_all"], "sequence")
    # 6 clock
    ppm = (m["osc_hz"] - OSC_NOMINAL_HZ) / OSC_NOMINAL_HZ * 1e6
    add("S6 clock", f"24 MHz osc within +/-{OSC_TOL_PPM} ppm", f"{m['osc_hz']} Hz ({ppm:+.0f} ppm)", abs(ppm) <= OSC_TOL_PPM, "clock")
    # 7 reset
    d = m["reset_release_ms"] - max(m["rail_on_ms"].values())
    add("S7 reset", f"reset released >= {RESET_MIN_DELAY_MS} ms after last rail", f"{d:.1f} ms", d >= RESET_MIN_DELAY_MS, "reset")
    # 8 JTAG
    idc = m["idcode"]
    add("S8 JTAG", f"IDCODE == 0x{EXPECTED_IDCODE:08X}, halt ok, SRAM R/W ok",
        f"0x{idc:08X}, halt={m['halt_ok']}, sram_rw={m['sram_rw_ok']}",
        idc == EXPECTED_IDCODE and m["halt_ok"] and m["sram_rw_ok"], "jtag")
    # 9 SRAM code + UART
    add("S9 SRAM+UART", f"GPIO toggles, UART prints '{UART_EXPECT}'", repr(m["uart"]), m["uart"] == UART_EXPECT, "sram_uart")
    return rows


def print_log(board, rows):
    print(f"### Board {board['sn']}  rev {board['rev']}  SoC {board['soc']}  ({board['note']})")
    for step, crit, meas, verdict, hint in rows:
        print(f"  [{verdict:<20}] {step:<15} | {crit}")
        if meas != "-":
            print(f"  {'':22} {'':15} | measured: {meas}")
        if hint:
            print(f"  {'':22} {'':15} | NEXT: {hint}")
    first_fail = next((r[0] for r in rows if r[3] == "FAIL"), None)
    print(f"  => status: {'READY for boot-device/DDR' if not first_fail else 'STOPPED at ' + first_fail}\n")


def compare(boards, golden):
    print("### Cross-board diff vs golden (SN001) -- only values that differ by >10%")
    for b in boards:
        if b is golden:
            continue
        diffs = []
        for n, *_ in RAILS:
            for key, unit in (("rail_ohm", "ohm"), ("rail_V", "V")):
                g, v = golden["meas"][key][n], b["meas"][key][n]
                if g and abs(v - g) / g > 0.10:
                    diffs.append(f"{key}[{n}] {g}{unit}->{v}{unit}")
        print(f"  {b['sn']}: " + ("; ".join(diffs) if diffs else "no rail-level differences >10%"))
    print()


def mk(sn, note, **over):
    base = {
        "visual": "clean",
        "rail_ohm": {"VDD_AON_0V8": 85.0, "VDD_CORE_0V75": 1.8, "VDD_DDR_1V05": 22.0, "VDD_IO_1V8": 180.0, "VDD_IO_3V3": 420.0},
        "vin_current_A": [0.10, 0.231, 0.231],
        "rail_V": {"VDD_AON_0V8": 0.801, "VDD_CORE_0V75": 0.748, "VDD_DDR_1V05": 1.052, "VDD_IO_1V8": 1.797, "VDD_IO_3V3": 3.31},
        "rail_on_ms": {"VDD_AON_0V8": 0.8, "VDD_CORE_0V75": 2.1, "VDD_DDR_1V05": 3.4, "VDD_IO_1V8": 4.6, "VDD_IO_3V3": 5.9},
        "pgood_all": True,
        "osc_hz": 24_000_310,
        "reset_release_ms": 26.0,
        "idcode": 0x4BA00477, "halt_ok": True, "sram_rw_ok": True,
        "uart": UART_EXPECT,
    }
    for k, v in over.items():
        if isinstance(v, dict):
            base[k] = {**base[k], **v}
        else:
            base[k] = v
    return {"sn": sn, "rev": "A", "soc": "A0", "note": note, "meas": base}


def main():
    golden = mk("SN001", "golden: first board, best-looking solder")
    boards = [
        golden,
        mk("SN002", "IO 1.8 V rail reads 0.3 ohm", rail_ohm={"VDD_IO_1V8": 0.3}),
        mk("SN003", "IO rail comes up before core", rail_on_ms={"VDD_CORE_0V75": 4.9, "VDD_IO_1V8": 2.0}),
        mk("SN004", "JTAG reads all ones", idcode=0xFFFFFFFF, halt_ok=False, sram_rw_ok=False),
        mk("SN005", "UART garbage", uart="\x86\xf8\x1c?\x80"),
    ]
    print("## Day-1 bring-up log (mock data, [example] values)\n")
    for b in boards:
        print_log(b, evaluate(b, None if b is golden else golden))
    compare(boards, golden)
    summary = {b["sn"]: next((r[0] for r in evaluate(b, golden) if r[3] == "FAIL"), "all PASS") for b in boards}
    print("### Summary (first failing step per board)")
    for sn, s in summary.items():
        print(f"  {sn}: {s}")


if __name__ == "__main__":
    main()
