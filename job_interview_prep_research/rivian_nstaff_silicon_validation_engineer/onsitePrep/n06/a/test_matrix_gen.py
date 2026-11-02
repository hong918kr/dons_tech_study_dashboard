#!/usr/bin/env python3
"""N06A test-matrix generator (stdlib only).

feature list (JSON) x 12 categories x conditions  ->  test-ID table (markdown)

What it prints:
  1. feature x category coverage grid      (where are the holes?)
  2. test table: ID, feature, category, conditions, full vs pairwise case count,
     platform, priority score, P0/P1/P2, pass criterion template
  3. summary counts (priority x platform) and a plan "lint":
       - every one of the 12 categories appears somewhere in the plan
       - every feature has an ERR (negative) row or an explicit waiver
       - every P0 row runs on FPGA too (so A0 day-1 runs a pre-validated binary)

Usage:  python3 -I test_matrix_gen.py [features.json]
Exit 0 = plan lint clean, 1 = lint findings (so CI can gate a plan review).
"""
import itertools
import json
import os
import sys

# code, name, bring-up relevant?, inherent DV gap 0-3, platform, pass template
CATS = [
    ("REG",  "register",          1, 0, "both",           "reset value == spec for every field; RW/RO/W1C behave; reserved stay 0"),
    ("FUN",  "basic function",    1, 0, "both",           "data == expected (byte compare/CRC), 0 mismatches in N=1000 transfers"),
    ("CFG",  "config combos",     0, 1, "both",           "every mode combination passes the FUN check (pairwise set)"),
    ("BND",  "boundary/corner",   0, 0, "both",           "min/max/0/wrap cases pass; no hang; status bits correct"),
    ("INT",  "interrupt",         1, 1, "both",           "each source fires once per event, mask honored, W1C clears, 0 lost/spurious"),
    ("ERR",  "error/negative",    0, 1, "both",           "error detected+reported (status/IRQ), block recovers, next transfer passes"),
    ("RST",  "reset/power state", 1, 2, "both",           "after reset/resume: regs at reset value, next transfer passes, N=1000 cycles 0 fail"),
    ("STR",  "concurrency/stress",0, 2, "both",           "0 data errors, 0 hangs under max concurrent load for T minutes"),
    ("PRF",  "performance",       0, 2, "Si (FPGA smoke)","throughput/latency >= target (p99 reported), measured on Si"),
    ("PVT",  "PVT/margin",        0, 3, "Si",             "passes at all V x T corners; margin to spec timing >= target"),
    ("SOAK", "soak/reliability",  0, 3, "Si",             "0 fails in T device-hours (rule of three: rate < 3/T @95%)"),
    ("SYS",  "system/interop",    0, 3, "both",           "real driver + real partner devices pass end-to-end use case"),
]
CAT = {c[0]: c for c in CATS}

# Default condition axes per category (merged with the feature's own axes for
# FUN/CFG/BND; replaced for categories whose conditions are not feature-specific).
CAT_AXES = {
    "RST":  {"trigger": ["soft reset", "block reset", "SoC warm reset"], "when": ["idle", "mid-transfer"]},
    "STR":  {"load": ["DMA+DDR traffic", "all I2C ports", "IRQ storm"]},
    "PRF":  {"metric": ["throughput", "latency"]},
    "PVT":  {"V": ["Vmin", "Vnom", "Vmax"], "T": ["-40C", "25C", "125C"]},
    "SOAK": {"hours": ["24h", "72h"]},
    "SYS":  {"stack": ["Linux i2c-dev", "bare-metal"], "partner": ["EEPROM", "sensor", "PMIC"]},
}
FEATURE_AXES_CATS = {"FUN", "CFG", "BND", "ERR", "INT"}

# Priority = 5*B + 4*I + 3*C + 2*G + 1*E   (criteria in decreasing importance)
#   B bring-up blocker (feature.blocker, only if category is bring-up relevant)
#   I customer impact, C RTL changed vs last chip, G DV gap (max of feature/category),
#   E errata history.   Max = 15+12+9+6+3 = 45.
P0_MIN, P1_MIN = 22, 15


def score(feat, cat):
    r = feat["risk"]
    b = r["blocker"] if CAT[cat][2] else 0
    g = max(r["dv_gap"], CAT[cat][3])
    return 5 * b + 4 * r["impact"] + 3 * r["changed"] + 2 * g + r["errata"]


def prio(s, cat):
    p = "P0" if s >= P0_MIN else "P1" if s >= P1_MIN else "P2"
    if cat in ("PVT", "SOAK") and p == "P0":   # characterization never blocks A0 bring-up
        p = "P1"
    return p


def pairwise(axes):
    """Greedy all-pairs: pick cases from the full product until every value pair
    of every two axes is covered. Returns (#full, #pairwise)."""
    names = list(axes)
    full = list(itertools.product(*(axes[n] for n in names)))
    if len(names) < 3:
        return len(full), len(full)     # with <=2 axes pairwise == full product
    need = set()
    for i, j in itertools.combinations(range(len(names)), 2):
        for a in axes[names[i]]:
            for b in axes[names[j]]:
                need.add((i, a, j, b))
    chosen = 0
    while need:
        best, gain = None, -1
        for case in full:
            g = sum((i, case[i], j, case[j]) in need
                    for i, j in itertools.combinations(range(len(names)), 2))
            if g > gain:
                best, gain = case, g
        for i, j in itertools.combinations(range(len(names)), 2):
            need.discard((i, best[i], j, best[j]))
        chosen += 1
    return len(full), chosen


def cond_axes(feat, cat):
    if cat in FEATURE_AXES_CATS:
        return dict(feat.get("axes", {}))
    return dict(CAT_AXES.get(cat, {}))


def fmt_axes(axes):
    if not axes:
        return "-"
    return " x ".join("%s{%s}" % (k, ",".join(v)) for k, v in axes.items())


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "i2c_features.json")
    with open(path, encoding="utf-8") as f:
        plan = json.load(f)
    blk, feats, waivers = plan["block"], plan["features"], plan.get("waivers", {})

    rows = []
    for ft in feats:
        n = {}
        for c in [c[0] for c in CATS]:          # canonical category order
            if c not in ft["cats"]:
                continue
            n[c] = n.get(c, 0) + 1
            axes = cond_axes(ft, c)
            full, pw = pairwise(axes)
            s = score(ft, c)
            rows.append({
                "id": "%s-%s-%s-%02d" % (blk, ft["id"], c, n[c]),
                "feat": ft["id"], "cat": c, "cond": fmt_axes(axes),
                "full": full, "pw": pw, "plat": CAT[c][4], "score": s,
                "prio": prio(s, c), "pass": CAT[c][5], "req": ft["req"],
            })

    # 1. coverage grid
    print("#### 1. %s feature x category grid (X = test row, w = waived, . = none)\n" % blk)
    print("| feature | " + " | ".join(c[0] for c in CATS) + " |")
    print("|---|" + "---|" * len(CATS))
    have = {(r["feat"], r["cat"]) for r in rows}
    for ft in feats:
        cells = []
        for c in CATS:
            k = c[0]
            cells.append("X" if (ft["id"], k) in have else "w" if k in waivers.get(ft["id"], {}) else ".")
        print("| %s %s | %s |" % (ft["id"], ft["name"], " | ".join(cells)))

    # 2. test table (sorted by priority then score)
    print("\n#### 2. test table (%d rows, sorted P0 first)\n" % len(rows))
    print("| test ID | req | conditions | cases full/pairwise | platform | score | pri |")
    print("|---|---|---|---|---|---|---|")
    for r in sorted(rows, key=lambda r: (r["prio"], -r["score"], r["id"])):
        print("| %s | %s | %s | %d/%d | %s | %d | %s |" % (
            r["id"], r["req"], r["cond"], r["full"], r["pw"], r["plat"], r["score"], r["prio"]))

    # 3. pass-criterion templates (one per category used)
    print("\n#### 3. pass-criterion template per category\n")
    print("| cat | name | pass criterion (fill numbers per block) |")
    print("|---|---|---|")
    used = {r["cat"] for r in rows}
    for c in CATS:
        if c[0] in used:
            print("| %s | %s | %s |" % (c[0], c[1], c[5]))

    # 4. summary
    print("\n#### 4. summary\n")
    print("```text")
    tf = sum(r["full"] for r in rows)
    tp = sum(r["pw"] for r in rows)
    print("rows=%d  executable cases: full=%d  pairwise=%d  (%.0f%% of full)" % (
        len(rows), tf, tp, 100.0 * tp / tf))
    for p in ("P0", "P1", "P2"):
        sel = [r for r in rows if r["prio"] == p]
        plats = {}
        for r in sel:
            plats[r["plat"]] = plats.get(r["plat"], 0) + 1
        print("%s: %2d rows  %s" % (p, len(sel), "  ".join("%s=%d" % kv for kv in sorted(plats.items()))))
    print("score = 5*blocker + 4*impact + 3*changed + 2*dv_gap + 1*errata ; P0>=%d P1>=%d" % (P0_MIN, P1_MIN))

    # lint
    findings = []
    for c in CATS:
        if c[0] not in used:
            findings.append("plan has no %s (%s) row at all" % (c[0], c[1]))
    for ft in feats:
        if "ERR" not in ft["cats"] and "ERR" not in waivers.get(ft["id"], {}):
            findings.append("%s %s: no ERR/negative row and no waiver" % (ft["id"], ft["name"]))
    for r in rows:
        if r["prio"] == "P0" and r["plat"] == "Si":
            findings.append("%s is P0 but Si-only (not pre-validated on FPGA)" % r["id"])
    nw = sum(len(v) for v in waivers.values())
    print("waivers=%d (each must have a reason + owner in the plan review)" % nw)
    for msg in findings:
        print("LINT: " + msg)
    print("RESULT plan_lint %s findings=%d" % ("PASS" if not findings else "FAIL", len(findings)))
    print("```")
    return 0 if not findings else 1


if __name__ == "__main__":
    sys.exit(main())
