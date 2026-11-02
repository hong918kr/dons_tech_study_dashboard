#!/usr/bin/env python3
"""bringup_checklist.py - turn a schematic-derived power-tree CSV into a bring-up checklist.

Why: when the schematic arrives a week before the boards, a validation SW engineer
reads it once and writes down every rail / control signal / assembly option. Keeping
that as data (CSV) instead of prose lets a script
  1. sanity-check the schematic reading (unknown parents, dependency cycles,
     feedback-divider math that does not give the nominal voltage, rails with no
     test point or no current budget -> "design-for-debug" asks to the HW team),
  2. print the Day-1 checklist in power-up order (Markdown table), and
  3. optionally grade measured values (PASS/FAIL) so the same file drives automation.

Input CSV columns (lines starting with '#' are comments):
  id, kind(rail|signal|option), nominal_v, tol_pct, source, parent, after,
  min_delay_ms, tp, vref, r_top_k, r_bot_k, max_ma, notes
    parent : rail that powers this rail (power tree edge)
    after  : rail/signal that must be valid before this one (sequencing edge)
    vref, r_top_k, r_bot_k : regulator feedback divider -> Vout = vref*(1+Rtop/Rbot)
Measured CSV (optional): id, value   (volts for rails, ms delay for signals)

Usage:
  python3 -I bringup_checklist.py power.csv [--measured meas.csv]
Exit code: 0 = no ERROR findings (and no FAIL), 1 = otherwise.  stdlib only.
"""
import argparse
import csv
import sys

COLS = ["id", "kind", "nominal_v", "tol_pct", "source", "parent", "after",
        "min_delay_ms", "tp", "vref", "r_top_k", "r_bot_k", "max_ma", "notes"]


def fnum(s):
    s = (s or "").strip()
    return float(s) if s else None


def load(path):
    with open(path, newline="") as f:
        lines = [ln for ln in f if ln.strip() and not ln.lstrip().startswith("#")]
    rows = []
    for r in csv.DictReader(lines):
        r = {k: (r.get(k) or "").strip() for k in COLS}
        rows.append(r)
    return rows


def topo_order(rows, findings):
    """Kahn's algorithm over parent+after edges; ties keep file order."""
    ids = [r["id"] for r in rows if r["kind"] in ("rail", "signal")]
    known = set(r["id"] for r in rows)
    deps = {i: [] for i in ids}
    for r in rows:
        if r["id"] not in deps:
            continue
        for key in ("parent", "after"):
            d = r[key]
            if not d:
                continue
            if d not in known:
                findings.append(("ERROR", r["id"], f"{key} '{d}' is not defined in the CSV"))
            elif d in deps:
                deps[r["id"]].append(d)
    indeg = {i: len(set(deps[i])) for i in ids}
    users = {i: [] for i in ids}
    for i in ids:
        for d in set(deps[i]):
            users[d].append(i)
    order, ready = [], [i for i in ids if indeg[i] == 0]
    while ready:
        ready.sort(key=ids.index)
        n = ready.pop(0)
        order.append(n)
        for u in users[n]:
            indeg[u] -= 1
            if indeg[u] == 0:
                ready.append(u)
    stuck = [i for i in ids if i not in order]
    if stuck:
        findings.append(("ERROR", ",".join(stuck),
                         "dependency cycle in parent/after edges - sequencing cannot be satisfied"))
    return order


def check_rows(rows, findings):
    for r in rows:
        if r["kind"] != "rail":
            continue
        nom, tol = fnum(r["nominal_v"]), fnum(r["tol_pct"]) or 5.0
        vref, rt, rb = fnum(r["vref"]), fnum(r["r_top_k"]), fnum(r["r_bot_k"])
        if None not in (nom, vref, rt, rb):
            vout = vref * (1 + rt / rb)
            err = (vout - nom) / nom * 100
            sev = "ERROR" if abs(err) > tol else "INFO"
            findings.append((sev, r["id"],
                             f"FB divider {vref} V x (1 + {rt:g}k/{rb:g}k) = {vout:.3f} V "
                             f"({err:+.1f}% vs nominal {nom} V, tol +/-{tol:g}%)"))
        if not r["tp"]:
            findings.append(("WARN", r["id"], "no test point - DFD ask: add TP (or name a probe-able pad/pin)"))
        if not r["max_ma"]:
            findings.append(("WARN", r["id"], "no current budget - needed to set PSU current limit / spot shorts"))


def window(r):
    nom, tol = fnum(r["nominal_v"]), fnum(r["tol_pct"]) or 5.0
    return nom * (1 - tol / 100), nom * (1 + tol / 100)


def build_checklist(rows, order, meas):
    byid = {r["id"]: r for r in rows}
    out = []  # (phase, item, where, expect, result)

    def grade(rid, lo, hi):
        if rid not in meas:
            return ""
        v = meas[rid]
        return f"{'PASS' if lo <= v <= hi else 'FAIL'} ({v:g})"

    # Phase A: before power - resistance of every rail to GND
    for rid in order:
        r = byid[rid]
        if r["kind"] == "rail":
            out.append(("A pre-power", f"{rid}: resistance to GND (power OFF)", r["tp"] or "(no TP!)",
                        "not a short; compare with golden board", ""))
    # Phase B: assembly options straight from BOM / schematic notes
    for r in rows:
        if r["kind"] == "option":
            out.append(("B BOM/option", f"{r['id']}: {r['notes']}", r["tp"] or "visual", r["source"], ""))
    # Phase C: power-up in dependency order
    for rid in order:
        r = byid[rid]
        if r["kind"] == "rail":
            lo, hi = window(r)
            src = r["source"] + (f", from {r['parent']}" if r["parent"] else "")
            out.append(("C power-up", f"{rid} = {r['nominal_v']} V ({src})", r["tp"] or "(no TP!)",
                        f"{lo:.3f}..{hi:.3f} V" + (f", <= {r['max_ma']} mA" if r["max_ma"] else ""),
                        grade(rid, lo, hi)))
        else:
            dly = fnum(r["min_delay_ms"])
            exp = f"high >= {dly:g} ms after {r['after']}" if dly is not None and r["after"] else "goes high"
            res = ""
            if rid in meas and dly is not None:
                res = f"{'PASS' if meas[rid] >= dly else 'FAIL'} ({meas[rid]:g} ms)"
            out.append(("C power-up", f"{rid} ({r['source']})", r["tp"] or "(no TP!)", exp, res))
    return out


def md_table(rows, header):
    s = "| " + " | ".join(header) + " |\n|" + "---|" * len(header) + "\n"
    for i, r in enumerate(rows, 1):
        s += "| " + " | ".join([str(i)] + [c.replace("|", "/") for c in r]) + " |\n"
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("power_csv")
    ap.add_argument("--measured")
    a = ap.parse_args()
    rows = load(a.power_csv)
    meas = {}
    if a.measured:
        with open(a.measured, newline="") as f:
            for r in csv.DictReader(ln for ln in f if not ln.lstrip().startswith("#")):
                meas[r["id"].strip()] = float(r["value"])
    findings = []
    order = topo_order(rows, findings)
    check_rows(rows, findings)
    print(f"# Bring-up checklist from {a.power_csv}\n")
    print("## Schematic-reading findings\n")
    for sev, rid, msg in findings:
        print(f"- [{sev}] {rid}: {msg}")
    print("\n## Power-up order (parent + after edges)\n")
    print(" -> ".join(order) if order else "(none)")
    print("\n## Checklist\n")
    table = build_checklist(rows, order, meas)
    print(md_table(table, ["#", "Phase", "Item", "Where", "Expect", "Result"]))
    n_err = sum(1 for f in findings if f[0] == "ERROR")
    n_fail = sum(1 for t in table if t[4].startswith("FAIL"))
    print(f"Summary: {n_err} ERROR, {sum(1 for f in findings if f[0] == 'WARN')} WARN, "
          f"{n_fail} FAIL, {len(table)} checklist items")
    return 1 if (n_err or n_fail) else 0


if __name__ == "__main__":
    sys.exit(main())
