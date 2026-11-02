#!/usr/bin/env python3
"""N06B: per-block requirement coverage, pass rate, rule-of-three / Clopper-Pearson
bound and exit-criteria verdict from a validation results export.

Inputs (CSV, see gen_mock.py):
  requirements.csv  the PLAN: every requirement, priority, required corners,
                    max failure rate (p_max), waiver, negative-control flag
  results.csv       the EVIDENCE: one row per test session
  bugs.csv          bug tracker export
Only rows on the latest build count as evidence; older builds feed the trend.
Python 3 stdlib only.
"""
import argparse
import csv
import math
from collections import defaultdict

NOM = "NV-Fnom-25C"
PVT = [NOM, "LV-Fmax-125C", "LV-Fmax-m40C", "HV-Fmax-125C", "HV-Fmax-m40C"]
PROC = ["TT", "SS", "FF"]


# ---------------------------------------------------------------- statistics
def binom_cdf(k, n, p):
    """P(X <= k) for X ~ Binomial(n, p), via log-space terms."""
    if p <= 0.0:
        return 1.0
    if p >= 1.0:
        return 1.0 if k >= n else 0.0
    lp, lq = math.log(p), math.log1p(-p)
    s = 0.0
    for i in range(k + 1):
        s += math.exp(math.lgamma(n + 1) - math.lgamma(i + 1) - math.lgamma(n - i + 1)
                      + i * lp + (n - i) * lq)
    return min(s, 1.0)


def upper_bound(k, n, alpha=0.05):
    """One-sided (1-alpha) Clopper-Pearson upper bound on the failure rate.
    k == 0 reduces to 1 - alpha**(1/n) ~= -ln(alpha)/n (rule of three for 95%)."""
    if n == 0:
        return 1.0
    if k == 0:
        return 1.0 - alpha ** (1.0 / n)
    if k >= n:
        return 1.0
    lo, hi = k / n, 1.0
    for _ in range(100):                       # bisection: cdf(k; n, p) = alpha
        mid = (lo + hi) / 2
        if binom_cdf(k, n, mid) > alpha:
            lo = mid
        else:
            hi = mid
    return hi


def runs_needed(k, p_max, alpha=0.05):
    """Smallest total n such that k failures in n runs gives upper bound < p_max."""
    if k == 0:
        return math.floor(math.log(alpha) / math.log1p(-p_max)) + 1
    lo, hi = k + 1, k + 1
    while binom_cdf(k, hi, p_max) >= alpha:
        hi *= 2
    while lo < hi:                             # binary search on monotone cdf(n)
        mid = (lo + hi) // 2
        if binom_cdf(k, mid, p_max) < alpha:
            hi = mid
        else:
            lo = mid + 1
    return lo


# ---------------------------------------------------------------- helpers
def read(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def proc_of(chip):                              # "A0-SS-02" -> "SS"
    return chip.split("-")[1]


def required_cells(req):
    if req["corners"] == "PVT":
        return {(v, p) for v in PVT for p in PROC}
    return {(NOM, "*")}                         # nominal, any process corner


def cell_of(req, row):
    return (row["vft_corner"], proc_of(row["chip"]) if req["corners"] == "PVT" else "*")


def pct(a, b):
    return 100.0 * a / b if b else 0.0


# ---------------------------------------------------------------- report
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reqs", default="requirements.csv")
    ap.add_argument("--results", default="results.csv")
    ap.add_argument("--bugs", default="bugs.csv")
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--p1-target", type=float, default=90.0)
    ap.add_argument("--min-chips", type=int, default=3)
    ap.add_argument("--min-boards", type=int, default=2)
    a = ap.parse_args()

    reqs, results, bugs = read(a.reqs), read(a.results), read(a.bugs)
    builds = sorted({r["build"] for r in results})
    latest = builds[-1]
    conf = round(100 * (1 - a.alpha))
    print(f"== N06B exit-criteria report: {len(reqs)} reqs, {len(results)} result rows, "
          f"{len(bugs)} bugs; evidence build = {latest} ==")
    print(f"   rules: {conf}% one-sided bound, P1 target >= {a.p1_target:.0f}%, "
          f">= {a.min_chips} chips and >= {a.min_boards} boards on P0 runs")

    by_req = defaultdict(list)
    for r in results:
        by_req[r["req_id"]].append(r)
    summary = []

    for block in dict.fromkeys(r["block"] for r in reqs):
        breqs = [r for r in reqs if r["block"] == block]
        print(f"\n---- block {block} ----")
        print(f"{'REQ':8} {'PRI':3} {'STATUS':8} {'CELLS':>5} {'RUNS':>4} {'ITER':>5} "
              f"{'FAIL':>4} {'UB' + str(conf):>8} {'P_MAX':>6}  NOTE")
        crit = {}
        st = {}
        notes_e4, notes_e6, holes, faillocs = [], [], [], []
        cells_hit = cells_req = 0
        p0_chips, p0_boards = set(), set()
        for q in breqs:
            rows = [r for r in by_req[q["req_id"]] if r["build"] == latest]
            execd = [r for r in rows if r["result"] in ("PASS", "FAIL")]
            need = required_cells(q)
            passed = {cell_of(q, r) for r in execd if r["result"] == "PASS"}
            failed = [r for r in execd if r["result"] == "FAIL"]
            hit = need & passed
            cells_req += len(need)
            cells_hit += len(hit)
            iters = sum(int(r["iterations"]) for r in execd)
            fails = sum(int(r["fails"]) for r in execd)
            if not execd:
                s = "NOT_RUN"
            elif failed:
                s = "FAIL"
            elif hit != need:
                s = "PARTIAL"
            else:
                s = "PASS"
            if s != "PASS" and q["waiver"]:
                s = "WAIVED"
            st[q["req_id"]] = s
            if q["priority"] == "P0":
                for r in execd:
                    p0_chips.add(r["chip"])
                    p0_boards.add(r["board"])
            for c in sorted(need - hit):
                if q["priority"] == "P0":
                    blk = [r["result"] for r in rows if cell_of(q, r) == c]
                    holes.append(f"{q['req_id']}@{c[1]}/{c[0]}" + (f"({blk[0]})" if blk else ""))
            for r in failed:
                faillocs.append(f"{q['req_id']} {r['fails']}/{r['iterations']} on "
                                f"{r['chip']} {r['board']} {r['vft_corner']}")
            note = ""
            ub = ""
            pm = ""
            if q["p_max"]:
                p_max = float(q["p_max"])
                u = upper_bound(fails, iters, a.alpha)
                ub, pm = f"{u:.5f}", q["p_max"]
                if u < p_max:
                    note = f"bound OK (3/n={3 / iters:.5f})" if fails == 0 else "bound OK"
                else:
                    more = runs_needed(fails, p_max, a.alpha) - iters
                    note = f"bound > p_max: need +{more} fail-free runs"
                    notes_e4.append(f"{q['req_id']} UB={u:.5f} >= {p_max} (+{more} runs)")
            if q["waiver"]:
                note = (note + " " if note else "") + f"waiver {q['waiver']}"
            if q["priority"] == "P0" and q["neg_control"] != "Y" and s == "PASS":
                notes_e6.append(q["req_id"])
                note = (note + " " if note else "") + "no negative control"
            print(f"{q['req_id']:8} {q['priority']:3} {s:8} {len(hit):>2}/{len(need):<2} "
                  f"{len(execd):>4} {iters:>5} {fails:>4} {ub:>8} {pm:>6}  {note}")

        # coverage + pass rate
        def cov(pri):
            qs = [q for q in breqs if q["priority"] == pri]
            ok = sum(st[q["req_id"]] in ("PASS", "WAIVED") for q in qs)
            wv = sum(st[q["req_id"]] == "WAIVED" for q in qs)
            ex = sum(st[q["req_id"]] != "NOT_RUN" for q in qs)
            return ok, wv, ex, len(qs)
        p0 = cov("P0")
        p1 = cov("P1")
        print(f"coverage   P0 {p0[0]}/{p0[3]} = {pct(p0[0], p0[3]):5.1f}% (waived {p0[1]}, "
              f"executed {p0[2]}/{p0[3]})   P1 {p1[0]}/{p1[3]} = {pct(p1[0], p1[3]):5.1f}% "
              f"(waived {p1[1]}, executed {p1[2]}/{p1[3]})")
        print(f"cross cov  {cells_hit}/{cells_req} req x V/F/T x process cells = "
              f"{pct(cells_hit, cells_req):5.1f}%")
        trend = []
        for b in builds:
            rr = [r for r in results if r["block"] == block and r["build"] == b
                  and r["result"] in ("PASS", "FAIL")]
            if rr:
                np_ = sum(r["result"] == "PASS" for r in rr)
                trend.append(f"{b} {np_}/{len(rr)} = {pct(np_, len(rr)):5.1f}%")
        blocked = sum(1 for r in results if r["block"] == block and r["build"] == latest
                      and r["result"] == "BLOCKED")
        print(f"pass rate  (sessions) " + " -> ".join(trend) + f"; blocked {blocked}")
        procs = sorted({proc_of(c) for c in p0_chips})
        print(f"samples    P0 chips={len(p0_chips)} ({'/'.join(procs)}) boards={len(p0_boards)}")
        open_hi = [b["bug_id"] for b in bugs if b["block"] == block and b["state"] == "open"
                   and b["severity"] in ("S1", "S2")]
        open_lo = [b["bug_id"] for b in bugs if b["block"] == block and b["state"] == "open"
                   and b["severity"] not in ("S1", "S2")]
        print(f"bugs       open S1/S2: {', '.join(open_hi) or 'none'}; open S3+: "
              f"{', '.join(open_lo) or 'none'}")
        for fl in faillocs:
            print(f"fail at    {fl}")

        # exit criteria
        bad_p0 = [q["req_id"] + ":" + st[q["req_id"]] for q in breqs
                  if q["priority"] == "P0" and st[q["req_id"]] not in ("PASS", "WAIVED")]
        crit["E1 P0 requirements 100% pass or waived"] = (
            "FAIL" if bad_p0 else "PASS", ", ".join(bad_p0))
        crit[f"E2 P1 requirements >= {a.p1_target:.0f}%"] = (
            "PASS" if pct(p1[0], p1[3]) >= a.p1_target else "FAIL",
            f"{pct(p1[0], p1[3]):.1f}%")
        crit["E3 P0 cross-coverage cells all hit"] = (
            "FAIL" if holes else "PASS", ", ".join(holes))
        crit[f"E4 failure-rate bound < p_max ({conf}%)"] = (
            "FAIL" if notes_e4 else "PASS", "; ".join(notes_e4))
        crit["E5 open S1/S2 bugs = 0"] = ("FAIL" if open_hi else "PASS", ", ".join(open_hi))
        crit["E6 P0 tests proven to fail (neg ctrl)"] = (
            "WARN" if notes_e6 else "PASS", ", ".join(notes_e6))
        ok7 = len(p0_chips) >= a.min_chips and len(p0_boards) >= a.min_boards
        crit["E7 sample spread chips/boards"] = (
            "PASS" if ok7 else "FAIL", f"chips {len(p0_chips)}, boards {len(p0_boards)}")
        for name, (v, why) in crit.items():
            print(f"  {name:42} {v:4}  {why}")
        vals = [v for v, _ in crit.values()]
        waived = p0[1] + p1[1]
        if "FAIL" in vals:
            verdict = "NOT DONE"
        elif "WARN" in vals or waived:
            verdict = "CONDITIONAL"
        else:
            verdict = "DONE"
        nfail = vals.count("FAIL")
        print(f"VERDICT {block}: {verdict}" + (f" ({nfail} criteria failing)" if nfail else "")
              + (f" (waivers {waived}, warnings {vals.count('WARN')})"
                 if verdict == "CONDITIONAL" else ""))
        summary.append((block, pct(p0[0], p0[3]), pct(p1[0], p1[3]),
                        pct(cells_hit, cells_req), verdict))

    print("\n==== summary ====")
    print(f"{'BLOCK':5} {'P0 cov':>7} {'P1 cov':>7} {'cells':>7}  VERDICT")
    for b, c0, c1, cc, v in summary:
        print(f"{b:5} {c0:6.1f}% {c1:6.1f}% {cc:6.1f}%  {v}")


if __name__ == "__main__":
    main()
