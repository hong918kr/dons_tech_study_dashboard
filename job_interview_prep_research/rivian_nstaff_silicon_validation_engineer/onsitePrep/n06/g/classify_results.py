#!/usr/bin/env python3
"""N06G result parser + flaky classifier (stdlib only).

Reads one or more UART logs produced by test_runner (RESULT lines), groups
observations by (platform, test) and classifies each as
  stable-pass / stable-fail / flaky / timeout / not-run
then dedups failures by a *signature* that drops volatile fields (seed, time,
offsets, measured values) so 40 failures of the same bug become 1 ticket.

Also reports:
  * FPGA-pass / SILICON-not-pass pairs (the "FPGA passes, silicon fails" queue)
  * stub results (ran against a stub driver -> no coverage credit)
  * rule-of-three 95% upper bound on failure rate for stable-pass (3/n)
  * repro command for every non-pass class (seed + --only)
  * negative-control check: --negctrl LOG --expect-fail ID[,ID]  (exit 1 if escaped)

usage: python3 -I classify_results.py LOG [LOG...] [--negctrl LOG --expect-fail A,B]
"""
import re
import sys
from collections import defaultdict, OrderedDict

RESULT_RE = re.compile(r"^RESULT (\S+) (PASS|FAIL|TIMEOUT|SKIP)((?: \S+=\S+)*)\s*$")
# keys that identify *which* bug; everything else is volatile per run
STABLE_KEYS = ("code", "reg", "exp", "reason")


def parse_file(path):
    obs, malformed = [], 0
    with open(path, errors="replace") as f:
        for ln in f:
            ln = ln.rstrip("\r\n")
            if not ln.startswith("RESULT"):
                continue                      # boot noise, printf debug, etc.
            m = RESULT_RE.match(ln)
            if not m:
                malformed += 1                # corrupted UART line: count, never guess
                continue
            kv = dict(p.split("=", 1) for p in m.group(3).split())
            obs.append({"test": m.group(1), "status": m.group(2), "file": path, **kv})
    return obs, malformed


def signature(o):
    parts = [o["test"], o["status"]] + [f"{k}={o[k]}" for k in STABLE_KEYS if k in o]
    return "|".join(parts)


def classify(rows):
    st = [r["status"] for r in rows]
    ran = [s for s in st if s != "SKIP"]
    if not ran:
        return "not-run"
    if all(s == "PASS" for s in ran):
        return "stable-pass"
    if "PASS" in ran:
        return "flaky"
    if all(s == "TIMEOUT" for s in ran):
        return "timeout"
    return "stable-fail"


def main(argv):
    logs, negctrl, expect = [], None, []
    i = 0
    while i < len(argv):
        if argv[i] == "--negctrl":
            negctrl = argv[i + 1]; i += 2
        elif argv[i] == "--expect-fail":
            expect = [x for x in argv[i + 1].split(",") if x]; i += 2
        else:
            logs.append(argv[i]); i += 1
    if not logs and not negctrl:
        print(__doc__); return 2

    obs, bad_lines = [], 0
    for p in logs:
        o, m = parse_file(p)
        obs += o; bad_lines += m

    rc = 0
    if logs:
        report(obs, bad_lines, len(logs))
    if negctrl:
        rc = check_negctrl(negctrl, expect)
    return rc


def report(obs, bad_lines, nlogs):
    groups = OrderedDict()
    for o in obs:
        groups.setdefault((o.get("plat", "?"), o["test"]), []).append(o)

    print(f"== {len(obs)} RESULT lines from {nlogs} logs, malformed={bad_lines}")
    print(f"{'PLAT':8} {'TEST':18} {'CLASS':12} {'n':>3} {'pass':>4} {'fail':>4} {'tmo':>3} "
          f"{'skip':>4}  note")
    cls_of = {}
    for (plat, test), rows in groups.items():
        c = classify(rows)
        cls_of[(plat, test)] = c
        cnt = defaultdict(int)
        for r in rows:
            cnt[r["status"]] += 1
        n_ran = len(rows) - cnt["SKIP"]
        note = ""
        if c == "stable-pass":
            note = f"95% UB fail rate <= 3/{n_ran} = {3 / n_ran:.3f}"
        elif c == "flaky":
            k = n_ran - cnt["PASS"]
            note = f"fail rate {k}/{n_ran} = {k / n_ran:.0%} (NOT a pass; gate=quarantine+ticket)"
        elif c == "not-run":
            note = rows[0].get("reason", "")
        if any(r.get("stub") == "1" for r in rows):
            note += " [stub driver: no coverage credit]"
        print(f"{plat:8} {test:18} {c:12} {len(rows):>3} {cnt['PASS']:>4} {cnt['FAIL']:>4} "
              f"{cnt['TIMEOUT']:>3} {cnt['SKIP']:>4}  {note}")

    # ---- dedup by signature ---------------------------------------------
    sigs = OrderedDict()
    for o in obs:
        if o["status"] in ("FAIL", "TIMEOUT"):
            s = sigs.setdefault(signature(o), {"n": 0, "plats": set(), "files": set(), "first": o})
            s["n"] += 1; s["plats"].add(o.get("plat", "?")); s["files"].add(o["file"])
    n_fail = sum(s["n"] for s in sigs.values())
    print(f"\n== dedup: {n_fail} failing observations -> {len(sigs)} unique signatures (tickets)")
    for sig, s in sigs.items():
        f = s["first"]
        repro = (f"test_runner --plat {f.get('plat', '?').lower()} --only {f['test']} "
                 f"--seed {f.get('seed', '?')} --repeat 1")
        print(f"  [{s['n']:>2}x] {sig}")
        print(f"        plats={','.join(sorted(s['plats']))} logs={len(s['files'])} repro: {repro}")

    # ---- FPGA passes, silicon does not ------------------------------------
    print("\n== FPGA stable-pass but SILICON not: (triage queue, see N06G 2.8 order)")
    any_diff = False
    for (plat, test), c in cls_of.items():
        if plat == "FPGA" and c == "stable-pass":
            sc = cls_of.get(("SILICON", test))
            if sc and sc not in ("stable-pass", "not-run"):
                any_diff = True
                print(f"  {test:18} FPGA=stable-pass  SILICON={sc}")
    if not any_diff:
        print("  (none)")

    # ---- gate ------------------------------------------------------------
    red = [k for k, c in cls_of.items() if c in ("stable-fail", "timeout")]
    flaky = [k for k, c in cls_of.items() if c == "flaky"]
    print(f"\n== GATE: {'RED' if red else 'GREEN'}  stable-fail/timeout={len(red)} "
          f"flaky(quarantined, tracked)={len(flaky)}")


def check_negctrl(negctrl, expect):
    rc = 0
    nobs, _ = parse_file(negctrl)
    print(f"\n== negative control: {negctrl}  expect FAIL for {','.join(expect)}")
    for t in expect:
        rows = [o for o in nobs if o["test"] == t]
        if not rows:
            print(f"  {t:18} NO RESULT  -> ESCAPED (test did not even run)"); rc = 1
            continue
        caught = all(r["status"] == "FAIL" for r in rows)
        print(f"  {t:18} {'CAUGHT ' if caught else 'ESCAPED'} "
              f"({sum(r['status'] == 'FAIL' for r in rows)}/{len(rows)} FAIL)")
        if not caught:
            rc = 1
    self_rows = [o for o in nobs if o["test"] == "SELFTEST_CHECKER"]
    if self_rows and any(r["status"] != "PASS" for r in self_rows):
        print("  SELFTEST_CHECKER   FAIL -> checker itself is blind; every PASS in this run is suspect")
    print(f"  verdict: {'OK - injected fault detected' if rc == 0 else 'NEGATIVE CONTROL ESCAPED'}")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
