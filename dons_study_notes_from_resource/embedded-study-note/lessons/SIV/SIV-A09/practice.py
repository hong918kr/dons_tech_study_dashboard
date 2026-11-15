#!/usr/bin/env python3
"""SIV-A09 추가 연습 — Day-0 재실행 범위, 웨이버, 일정 slack, 밤 비교, 게이팅 lint (모두 자립형)

  python3 -I practice.py   # "N/M passed", 실패 시 exit 1
"""
import sys

# 칩의 물리 의존과 스크립트가 선언한 의존 (solution.py 의 축약)
PHYS = {"power": (), "clock": ("power",), "jtag_idcode": ("power",), "boot_rom": ("clock",),
        "sram": ("boot_rom",), "ddr_training": ("sram",), "pcie_link": ("sram",),
        "csi2_loopback": ("ddr_training",), "serdes_loopback": ("sram",)}
DAG = dict(PHYS, boot_rom=("clock", "jtag_idcode"))
ORDER = list(PHYS)
CHAIN = {n: ((ORDER[i - 1],) if i else ()) for i, n in enumerate(ORDER)}
SCHEDULE = {
    "silicon_fab": (20, ()), "board_design_fab": (16, ()), "day0_scripts_on_emu": (14, ()), "lab_automation": (18, ()),
    "day0_bringup": (2, ("silicon_fab", "board_design_fab", "day0_scripts_on_emu")),
    "ddr_feature": (6, ("day0_bringup",)), "pcie_feature": (5, ("day0_bringup",)),
    "csi2_feature": (5, ("day0_bringup",)), "serdes_feature": (6, ("day0_bringup",)),
    "pvt_char": (8, ("ddr_feature", "pcie_feature", "csi2_feature", "serdes_feature", "lab_automation")),
    "stress_soak": (7, ("ddr_feature", "pcie_feature", "csi2_feature", "serdes_feature", "lab_automation")),
    "a1_metal_spin": (12, ("ddr_feature", "pcie_feature", "csi2_feature", "serdes_feature")),
    "a1_regression": (4, ("a1_metal_spin",)),
    "production_handoff": (3, ("pvt_char", "stress_soak", "a1_regression")),
}


# 연습 1 ─ 고친 뒤 무엇을 다시 돌릴까
def descendants(deps, roots):
    out, changed = set(roots), True
    while changed:
        changed = False
        for n, ds in deps.items():
            if n not in out and any(d in out for d in ds):
                out.add(n)
                changed = True
    return out


def resume_plan(deps, prev_status, changed=(), power_cycled=False):
    """다시 돌릴 스테이지 (선언 순서). 전원을 껐다 켰으면 칩 상태가 사라졌으니 전부."""
    if power_cycled:
        return list(deps)
    dirty = {n for n, st in prev_status.items() if st != "PASS"} | set(changed)
    rerun = descendants(deps, dirty)
    return [n for n in deps if n in rerun]


# 연습 2 ─ 웨이버는 정확히 일치하고 만료되지 않아야 한다
def apply_waivers(results, waivers, today):
    """(verdict, applied, rejected). results = {stage: {"status","code"}}."""
    applied, rejected = [], []
    open_fails = {n for n, r in results.items() if r["status"] == "FAIL"}
    for w in waivers:
        r = results.get(w["stage"])
        if not w.get("ticket"):
            rejected.append((w["stage"], "no_ticket"))
        elif today > w["expires_day"]:
            rejected.append((w["stage"], "expired"))
        elif r is None or r["status"] != "FAIL":
            rejected.append((w["stage"], "not_failing"))
        elif r["code"] != w["code"]:
            rejected.append((w["stage"], "code_mismatch"))
        else:
            applied.append(w["stage"])
            open_fails.discard(w["stage"])
    if open_fails:
        return "FAIL", applied, rejected
    return ("PASS_WITH_WAIVER" if applied else "PASS"), applied, rejected


# 연습 3 ─ slack: 어디를 줄여야 끝이 당겨지나
def finish_times(tasks):
    finish = {}

    def f(t):
        if t not in finish:
            finish[t] = tasks[t][0] + max((f(d) for d in tasks[t][1]), default=0)
        return finish[t]

    for t in tasks:
        f(t)
    return finish


def slack(tasks):
    """{작업: 전체 slack}. 끝 일정을 늦추지 않고 그만큼 늦어질 수 있다."""
    ef = finish_times(tasks)
    end = max(ef.values())
    kids = {t: [k for k, v in tasks.items() if t in v[1]] for t in tasks}
    lf = {}

    def latest(t):
        if t not in lf:
            lf[t] = min((latest(k) - tasks[k][0] for k in kids[t]), default=end)
        return lf[t]

    return {t: latest(t) - ef[t] for t in tasks}


def slip_effect(tasks, name, delta):
    t2 = dict(tasks)
    t2[name] = (tasks[name][0] + delta, tasks[name][1])
    return max(finish_times(t2).values()) - max(finish_times(tasks).values())


# 연습 4 ─ 두 밤 비교: 안 돌았거나 인프라로 죽은 보드는 FIXED 가 아니다
def diff_nights(prev, cur):
    """prev/cur = {board: {"verdict","signature"}} → {"NEW":[],"FIXED":[],"PERSISTING":[],"UNKNOWN":[]}."""
    out = {"NEW": [], "FIXED": [], "PERSISTING": [], "UNKNOWN": []}
    good = "PASS"
    for board in sorted(set(prev) | set(cur)):
        a, b = prev.get(board), cur.get(board)
        if b is None or b["verdict"] == "INCONCLUSIVE":
            if a is not None and a["verdict"] not in (good, "INCONCLUSIVE"):
                out["UNKNOWN"].append((board, a["signature"]))
            continue
        a_bad = a is not None and a["verdict"] not in (good, "INCONCLUSIVE")
        b_bad = b["verdict"] != good
        if a_bad and b_bad and a["signature"] == b["signature"]:
            out["PERSISTING"].append((board, b["signature"]))
            continue
        if a_bad:
            out["FIXED"].append((board, a["signature"]))
        if b_bad:
            out["NEW"].append((board, b["signature"]))
    return out


# 연습 5 ─ 선언한 의존 vs 물리 의존
def ancestors(deps, n):
    seen = set()
    stack = list(deps[n])
    while stack:
        d = stack.pop()
        if d not in seen:
            seen.add(d)
            stack.extend(deps[d])
    return seen


def gating_lint(declared, phys, observe=()):
    """[("UNDER"|"OVER", stage, ancestor)]. observe 는 측정 경로로 일부러 건 의존."""
    out = []
    for n in declared:
        da, pa = ancestors(declared, n), ancestors(phys, n)
        out += [("UNDER", n, a) for a in sorted(pa - da)]
        out += [("OVER", n, a) for a in sorted(da - pa - set(observe)) if a != n]
    return out


PASSED, TOTAL = 0, 0


def check(name, cond, detail=""):
    global PASSED, TOTAL
    TOTAL += 1
    PASSED += bool(cond)
    print("%s  %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "  (%s)" % detail))


def main():
    print("-- 1. resume plan --")
    st = {n: "PASS" for n in ORDER}
    st.update(ddr_training="FAIL", csi2_loopback="BLOCKED")
    check("failed + blocked only, independent PASS kept", resume_plan(DAG, st) == ["ddr_training", "csi2_loopback"])
    st2 = {n: "PASS" for n in ORDER}
    st2["serdes_loopback"] = "FLAKY"
    check("FLAKY re-runs too (PASS is not confirmed)", resume_plan(DAG, st2) == ["serdes_loopback"])
    check("changed sram firmware pulls all descendants",
          resume_plan(DAG, {n: "PASS" for n in ORDER}, changed=("sram",))
          == ["sram", "ddr_training", "pcie_link", "csi2_loopback", "serdes_loopback"])
    check("power cycle -> everything", resume_plan(DAG, st, power_cycled=True) == ORDER)

    print("-- 2. waivers --")
    res = {"pcie_link": {"status": "FAIL", "code": "E_PCIE_STUCK_POLLING"}, "ddr_training": {"status": "PASS", "code": ""}}
    w = {"stage": "pcie_link", "code": "E_PCIE_STUCK_POLLING", "ticket": "VAL-123", "expires_day": 30}
    check("exact valid waiver", apply_waivers(res, [w], 10) == ("PASS_WITH_WAIVER", ["pcie_link"], []))
    check("expired waiver does not apply", apply_waivers(res, [w], 31) == ("FAIL", [], [("pcie_link", "expired")]))
    check("different code on same stage not covered",
          apply_waivers(res, [dict(w, code="E_PCIE_STUCK_DETECT")], 10) == ("FAIL", [], [("pcie_link", "code_mismatch")]))
    check("waiver with no ticket rejected", apply_waivers(res, [dict(w, ticket="")], 10)[2] == [("pcie_link", "no_ticket")])
    check("waiver for a stage that passes is flagged stale",
          apply_waivers(res, [dict(w, stage="ddr_training")], 10)[2] == [("ddr_training", "not_failing")])

    print("-- 3. slack --")
    s = slack(SCHEDULE)
    crit = sorted(t for t, v in s.items() if v == 0)
    print("      slack:", {t: s[t] for t in sorted(s)})
    check("zero-slack set is the respin path + tied SerDes",
          crit == ["a1_metal_spin", "a1_regression", "day0_bringup", "ddr_feature", "production_handoff", "serdes_feature",
                   "silicon_fab"], str(crit))
    check("csi2_feature has 1 week of float", s["csi2_feature"] == 1, str(s["csi2_feature"]))
    bad = [t for t in SCHEDULE if slip_effect(SCHEDULE, t, s[t]) != 0 or slip_effect(SCHEDULE, t, s[t] + 1) != 1]
    check("definition: slipping by slack costs 0, by slack+1 costs 1 (all tasks)", bad == [], str(bad))
    check("cutting 3 weeks of PVT does not move the end date (slack, not critical)",
          slip_effect(SCHEDULE, "pvt_char", -3) == 0, str(slip_effect(SCHEDULE, "pvt_char", -3)))
    check("cutting 3 weeks of the metal spin moves it 3 weeks", slip_effect(SCHEDULE, "a1_metal_spin", -3) == -3)

    print("-- 4. night diff --")
    p = {"B1": {"verdict": "FAIL", "signature": "aaa"}, "B2": {"verdict": "FAIL", "signature": "bbb"},
         "B3": {"verdict": "FAIL", "signature": "ccc"}, "B4": {"verdict": "PASS", "signature": "00000000"},
         "B5": {"verdict": "FAIL", "signature": "ddd"}}
    c = {"B1": {"verdict": "FAIL", "signature": "aaa"}, "B2": {"verdict": "PASS", "signature": "00000000"},
         "B3": {"verdict": "INCONCLUSIVE", "signature": "eee"}, "B4": {"verdict": "FAIL", "signature": "fff"},
         "B6": {"verdict": "PASS", "signature": "00000000"}}
    d = diff_nights(p, c)
    check("persisting", d["PERSISTING"] == [("B1", "aaa")], str(d))
    check("fixed only when actually run and green", d["FIXED"] == [("B2", "bbb")], str(d))
    check("new on previously good board", d["NEW"] == [("B4", "fff")], str(d))
    check("infra night or missing board -> UNKNOWN, not FIXED", d["UNKNOWN"] == [("B3", "ccc"), ("B5", "ddd")], str(d))

    print("-- 5. gating lint --")
    check("declared DAG matches physics (JTAG observe path exempt)", gating_lint(DAG, PHYS, observe=("jtag_idcode",)) == [])
    lint = gating_lint(CHAIN, PHYS, observe=("jtag_idcode",))
    over = [x for x in lint if x[0] == "OVER"]
    print("      chain over-gates %d ancestor edges" % len(over))
    check("chain: over-gates PCIe on DDR", ("OVER", "pcie_link", "ddr_training") in lint, str(lint))
    check("chain: over-gates JTAG on clock", ("OVER", "jtag_idcode", "clock") in lint, str(lint))
    check("chain has no under-gating", not [x for x in lint if x[0] == "UNDER"], str(lint))
    under = dict(DAG, csi2_loopback=("sram",))
    check("removing the DDR edge is reported as UNDER",
          ("UNDER", "csi2_loopback", "ddr_training") in gating_lint(under, PHYS, observe=("jtag_idcode",)))

    print("%d/%d passed" % (PASSED, TOTAL))
    return PASSED == TOTAL


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
