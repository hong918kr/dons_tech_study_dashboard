#!/usr/bin/env python3
"""SIV-M12 추가 연습 — 시퀀서 결과 DB 위에 올리는 도구들

  연습 1  rerun_yaml(db, run_id, plan)     : 실패/FLAKY/TIMEOUT/CRASH 만 (+ 그 requires) 다시 돌리는 YAML 생성
  연습 2  compare_runs(db, base, new)      : 두 run 비교 → NEW_FAIL / FIXED / STILL_BAD / NEW_FLAKY
  연습 3  to_junit(db, run_id)             : CI(Jenkins/GitLab)가 읽는 JUnit XML
  연습 4  lint_requires_corners(plan)      : 영원히 SKIP 될 수밖에 없는 requires 를 리스트 단계에서 잡기

  python3 -I practice.py
본문 코드는 solution.py 를 그대로 불러 쓴다 (같은 레슨 디렉터리).
"""
import importlib.util
import os
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("siv_m12_solution", os.path.join(HERE, "solution.py"))
S = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(S)

RERUN = ("FAIL", "FLAKY", "TIMEOUT", "CRASH", "ERROR")


# ---------------------------------------------------------------- 연습 1
def _yaml_scalar(v):
    if isinstance(v, str):
        return '"%s"' % v.replace("\\", "\\\\").replace('"', '\\"')
    if isinstance(v, int):
        return "0x%X" % v if v > 255 else str(v)
    return repr(v)


def rerun_yaml(db, run_id, plan):
    """나빴던 (test, corner) 와 그 requires(전이적)만 담은 테스트 리스트. 코너도 필요한 것만."""
    bad = db.con.execute("SELECT test, corner FROM result WHERE run_id=? AND status IN (%s)"
                         % ",".join("?" * len(RERUN)), (run_id,) + RERUN).fetchall()
    by_name = {t.name: t for t in plan.tests}
    need = {}  # test -> set(corners)
    stack = list(bad)
    while stack:
        t, k = stack.pop()
        if k in need.setdefault(t, set()):
            continue
        need[t].add(k)
        stack.extend((r, k) for r in by_name[t].requires)
    corners = [c for c in plan.corners if any(c.name in ks for ks in need.values())]
    run = dict(plan.run, name=plan.run["name"] + "-rerun")
    out = ["# generated rerun list for run %d" % run_id, "run:"]
    out += ["  %s: %s" % (k, _yaml_scalar(run[k])) for k in ("name", "board", "fw", "seed")]
    out += ["limits:"] + ["  %s: %d" % (k, v) for k, v in plan.limits.items()]
    out += ["corners:"]
    for c in corners:
        out += ["  - name: %s" % c.name, "    vdd_mv: %d" % c.vdd_mv, "    temp_c: %d" % c.temp_c]
    out += ["tests:"]
    for t in plan.tests:  # 원래 순서 유지 → requires 가 항상 앞에 있다
        if t.name not in need:
            continue
        out += ["  - name: %s" % t.name, "    func: %s" % t.func, "    runner: %s" % t.runner,
                "    timeout_s: %r" % t.timeout_s, "    retries: %d" % t.retries]
        if t.requires:
            out.append("    requires: [%s]" % ", ".join(t.requires))
        out.append("    corners: [%s]" % ", ".join(c.name for c in corners if c.name in need[t.name]))
        if t.params:
            out.append("    params:")
            out += ["      %s: %s" % (k, _yaml_scalar(v)) for k, v in sorted(t.params.items())]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- 연습 2
def compare_runs(db, base, new):
    """→ {분류: [(test, corner, old, new)]}. 같은 test seed 이므로 차이는 '샘플/펌웨어/조건' 차이다."""
    q = "SELECT test, corner, status FROM result WHERE run_id=?"
    old = {(t, k): s for t, k, s in db.con.execute(q, (base,))}
    cur = {(t, k): s for t, k, s in db.con.execute(q, (new,))}
    bad = set(S.BAD)
    out = {"NEW_FAIL": [], "FIXED": [], "STILL_BAD": [], "NEW_FLAKY": [], "NOT_RUN": []}
    for key in sorted(set(old) | set(cur)):
        o, n = old.get(key, "-"), cur.get(key, "-")
        row = key + (o, n)
        if n in bad and o not in bad:
            out["NEW_FAIL"].append(row)
        elif n in bad and o in bad:
            out["STILL_BAD"].append(row)
        elif o in bad and n not in bad:
            out["FIXED"].append(row)
        elif n == "FLAKY" and o != "FLAKY":
            out["NEW_FLAKY"].append(row)
        elif n in ("SKIP", "-") and o not in ("SKIP", "-"):
            out["NOT_RUN"].append(row)
    return out


# ---------------------------------------------------------------- 연습 3
def to_junit(db, run_id):
    """코너 = testsuite, 테스트 = testcase. FAIL → <failure>, TIMEOUT/CRASH/ERROR → <error>, SKIP → <skipped>,
    FLAKY → 통과지만 <system-out> 에 첫 실패를 남긴다 (CI 대시보드에서 사라지지 않게)."""
    root = ET.Element("testsuites", name="siv-run-%d" % run_id)
    rows = db.con.execute("SELECT corner, test, status, attempts, reg, addr, expected, actual, msg, dur_s "
                          "FROM result WHERE run_id=? ORDER BY ord", (run_id,)).fetchall()
    suites = {}
    for corner, test, st, att, reg, addr, exp, act, msg, dur in rows:
        if corner not in suites:
            suites[corner] = ET.SubElement(root, "testsuite", name=corner)
        tc = ET.SubElement(suites[corner], "testcase", classname=corner, name=test, time="%.6f" % (dur or 0))
        detail = msg if not reg else "%s reg=%s addr=0x%08X exp=0x%08X act=0x%08X" % (msg, reg, addr, exp, act)
        if st == "FAIL":
            ET.SubElement(tc, "failure", message=detail, type="FAIL")
        elif st in ("TIMEOUT", "CRASH", "ERROR"):
            ET.SubElement(tc, "error", message=detail, type=st)
        elif st == "SKIP":
            ET.SubElement(tc, "skipped", message=msg)
        elif st == "FLAKY":
            ET.SubElement(tc, "system-out").text = "FLAKY after %d attempts: %s" % (att, detail)
    for s in suites.values():
        cases = list(s)
        s.set("tests", str(len(cases)))
        for tag, attr in (("failure", "failures"), ("error", "errors"), ("skipped", "skipped")):
            s.set(attr, str(sum(1 for c in cases if c.find(tag) is not None)))
    return ET.tostring(root, encoding="unicode")


# ---------------------------------------------------------------- 연습 4
def lint_requires_corners(plan):
    """테스트 T 가 코너 K 에서 돌고 T.requires 의 R 이 K 에서 안 돈다면 T@K 는 항상 SKIP 이다."""
    allc = [c.name for c in plan.corners]
    where = {t.name: set(t.corners or allc) for t in plan.tests}
    errs = []
    for t in plan.tests:
        for r in t.requires:
            missing = sorted(where[t.name] - where.get(r, set()), key=allc.index)
            if missing:
                errs.append("E_REQCORNER %s requires %s, which does not run at %s" % (t.name, r, ",".join(missing)))
    return errs


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok, why = bool(fn()), ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    g_pass += ok
    print("%s  %s%s" % ("PASS" if ok else "FAIL", name, why))


def main():
    text = S.read("testlist.yaml")
    with tempfile.TemporaryDirectory() as td:
        lib, exe = S.build_c(td)
        runners = {"ctypes": S.CTypesRunner(lib), "binary": S.BinaryRunner(exe)}
        plan, errs = S.validate_plan(S.parse_yaml(text), S.FUNCS)
        assert not errs, errs
        db = S.ResultDB()
        g, _, gid, _ = S.sequence(text, 0, db, runners, "2026-11-08T01:00:00Z")
        code, _, rid, _ = S.sequence(text, 0xF, db, runners, "2026-11-08T02:00:00Z")

        print("-- 연습 1: rerun list --")
        y = rerun_yaml(db, rid, plan)
        p2, e2 = S.validate_plan(S.parse_yaml(y), S.FUNCS)
        check("rerun yaml parses + validates", lambda: e2 == [] and p2 is not None)
        check("rerun tests = reg.id(+dep) sram i2c pll dma, in original order",
              lambda: [t.name for t in p2.tests] == ["reg.id", "sram.march", "i2c.loopback", "pll.lock", "dma.ring"])
        check("sram.march only @LV_HOT, pll.lock @NOM_25C,LV_HOT,NOM_COLD (dep of dma + own TIMEOUT)",
              lambda: p2.tests[1].corners == ["LV_HOT"] and p2.tests[3].corners == ["NOM_25C", "LV_HOT", "NOM_COLD"])
        check("dma.ring rerun only where it CRASHed (not the SKIP corner)",
              lambda: p2.tests[4].corners == ["NOM_25C", "LV_HOT"])
        c3, rep3, _, _ = S.sequence(y, 0xF, db, runners, "2026-11-08T07:00:00Z")
        check("rerun on same sample reproduces: exit 1, counts FAIL=1 TIMEOUT=1 CRASH=2",
              lambda: c3 == 1 and "FAIL=1 TIMEOUT=1 CRASH=2" in rep3)

        print("-- 연습 2: compare runs --")
        d = compare_runs(db, gid, rid)
        check("golden -> defective: 4 NEW_FAIL",
              lambda: sorted((t, k) for t, k, _, _ in d["NEW_FAIL"]) == [
                  ("dma.ring", "LV_HOT"), ("dma.ring", "NOM_25C"), ("pll.lock", "NOM_COLD"), ("sram.march", "LV_HOT")])
        check("1 NEW_FLAKY (i2c @NOM_25C), 1 NOT_RUN (dma @NOM_COLD)",
              lambda: [r[:2] for r in d["NEW_FLAKY"]] == [("i2c.loopback", "NOM_25C")]
              and [r[:2] for r in d["NOT_RUN"]] == [("dma.ring", "NOM_COLD")])
        d2 = compare_runs(db, rid, gid)
        check("reverse direction: 4 FIXED, nothing new", lambda: len(d2["FIXED"]) == 4 and not d2["NEW_FAIL"])

        print("-- 연습 3: JUnit XML --")
        x = ET.fromstring(to_junit(db, rid))
        suites = {s.get("name"): s for s in x.findall("testsuite")}
        check("3 suites x 6 testcases", lambda: list(suites) == ["NOM_25C", "LV_HOT", "NOM_COLD"]
              and all(s.get("tests") == "6" for s in suites.values()))
        check("LV_HOT: failures=1 errors=1 (CRASH); NOM_COLD: errors=1 skipped=1",
              lambda: (suites["LV_HOT"].get("failures"), suites["LV_HOT"].get("errors")) == ("1", "1")
              and (suites["NOM_COLD"].get("errors"), suites["NOM_COLD"].get("skipped")) == ("1", "1"))
        fl = suites["NOM_25C"].find("testcase[@name='i2c.loopback']/system-out")
        check("FLAKY kept as system-out text", lambda: fl is not None and "FLAKY after 2 attempts" in fl.text)
        sf = suites["LV_HOT"].find("testcase[@name='sram.march']/failure")
        check("failure message carries addr/exp/act",
              lambda: "addr=0x20000A9C exp=0xFFFFFFFF act=0xFFFFDFFF" in sf.get("message"))

        print("-- 연습 4: requires vs corners --")
        check("testlist.yaml is clean", lambda: lint_requires_corners(plan) == [])
        bad = S.Plan(plan.run, plan.limits, plan.corners,
                     [S.TestSpec("reg.id", "id_check", corners=["NOM_25C"]),
                      S.TestSpec("sram.march", "sram_march", requires=["reg.id"], corners=["NOM_25C", "LV_HOT"])])
        check("reg.id only @NOM_25C, sram.march @LV_HOT too -> E_REQCORNER LV_HOT",
              lambda: lint_requires_corners(bad) == ["E_REQCORNER sram.march requires reg.id, which does not run at LV_HOT"])
        db.close()
        check("setup runs: golden exit 0, defective exit 1", lambda: (g, code) == (0, 1))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
