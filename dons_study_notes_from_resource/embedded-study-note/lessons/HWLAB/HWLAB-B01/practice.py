#!/usr/bin/env python3
"""HWLAB-B01 추가 연습 — 같은 넷리스트로 bring-up 도구 넓히기

  연습 1  domain_mismatch(nl)        : 풀업 레일 전압 > 그 넷에 붙은 IC 의 최대 전원 → 역전원(back-power) 위험
  연습 2  diff_netlists(a, b)        : rev A → rev B 변경 목록 (ECO 리뷰 / 수정 확인)
  연습 3  split_net_candidates(nl)   : 단일 노드 넷 중 이름이 비슷한 쌍 → 라벨 오타 후보
  연습 4  bringup_plan(nl)           : 첫 전원 인가 측정 순서표 (어디를 프로브, 무엇을 기대)

  python3 -I practice.py      # "N/M passed", 실패 시 exit 1

본문 함수(parse_sexpr, load_netlist, find_pulls, ...)는 같은 디렉터리의 solution.py 를 불러 쓴다.
"""
import copy
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("hwlab_b01_solution", os.path.join(HERE, "solution.py"))
S = importlib.util.module_from_spec(spec)
spec.loader.exec_module(S)


# ---------------------------------------------------------------- 연습 1
def ic_max_supply(nl, ref):
    vs = [S.rail_voltage(net) for pin, net in nl["pins"].get(ref, {}).items()
          if S.is_rail(net) and S.rail_voltage(net) is not None]
    return max(vs) if vs else None


def domain_mismatch(nl):
    """→ [(net, ic_ref, pullup_rail, v_pull, v_ic_max)] : 풀업 전압이 IC 의 가장 높은 전원보다 높은 경우"""
    out = []
    for net in sorted(nl["nets"]):
        ups = S.find_pulls(nl, net)["up"] if not (S.is_rail(net) or S.is_gnd(net)) else []
        for _ref, _ohm, rail in ups:
            vp = S.rail_voltage(rail)
            for nd in nl["nets"][net]:
                if not nd["ref"].startswith("U"):
                    continue
                vmax = ic_max_supply(nl, nd["ref"])
                if vp is not None and vmax is not None and vp > vmax + 1e-9:
                    out.append((net, nd["ref"], rail, vp, vmax))
    return sorted(set(out))


# ---------------------------------------------------------------- 연습 2
def diff_netlists(a, b):
    out = []
    for ref in sorted(set(a["comps"]) | set(b["comps"]), key=S.natkey):
        ca, cb = a["comps"].get(ref), b["comps"].get(ref)
        if ca is None:
            out.append("+COMP %s %s" % (ref, cb["value"]))
        elif cb is None:
            out.append("-COMP %s %s" % (ref, ca["value"]))
        else:
            if ca["value"] != cb["value"]:
                out.append("VALUE %s %s -> %s" % (ref, ca["value"], cb["value"]))
            if ca["dnp"] != cb["dnp"]:
                out.append("DNP %s %s -> %s" % (ref, "yes" if ca["dnp"] else "no", "yes" if cb["dnp"] else "no"))

    def nodes(nl, net):
        return {"%s.%s" % (nd["ref"], nd["pin"]) for nd in nl["nets"].get(net, [])}
    for net in sorted(set(a["nets"]) | set(b["nets"])):
        if net not in b["nets"]:
            out.append("-NET %s" % net)
        elif net not in a["nets"]:
            out.append("+NET %s" % net)
        else:
            na, nb = nodes(a, net), nodes(b, net)
            ch = ["+" + x for x in sorted(nb - na)] + ["-" + x for x in sorted(na - nb)]
            if ch:
                out.append("NET %s %s" % (net, " ".join(ch)))
    return out


# ---------------------------------------------------------------- 연습 3
def lev(a, b):
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cbb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cbb)))
        prev = cur
    return prev[-1]


def split_net_candidates(nl, maxd=2):
    ones = sorted(n for n, nodes in nl["nets"].items() if len(nodes) == 1 and not n.startswith("unconnected-"))
    out = []
    for i, x in enumerate(ones):
        for y in ones[i + 1:]:
            if lev(S.net_base(x).upper(), S.net_base(y).upper()) <= maxd:
                out.append((x, y))
    return out


# ---------------------------------------------------------------- 연습 4
def bringup_plan(nl, tol=0.05):
    rows = ["step,what,where,expect"]
    gtp = sorted({nd["ref"] for nd in nl["nets"].get("GND", []) if nd["ref"].startswith("TP")}, key=S.natkey)
    rows.append("1,GND reference,%s,0 V (ground spring here)" % (gtp[0] if gtp else "?"))
    rails = [n for n in nl["nets"] if S.is_rail(n) and S.rail_voltage(n) is not None]
    rails.sort(key=lambda n: -S.rail_voltage(n))
    hints = {f[2]: f[3] for f in S.audit_rails(nl)}
    for net in rails:
        v = S.rail_voltage(net)
        tps = sorted({nd["ref"] for nd in nl["nets"][net] if nd["ref"].startswith("TP")}, key=S.natkey)
        where = tps[0] if tps else hints[net].split("probe ")[1].split(" ")[0] + " (no TP)"
        rows.append("%d,%s,%s,%.3f..%.3f V" % (len(rows), net, where, v * (1 - tol), v * (1 + tol)))
    for net in sorted(nl["nets"]):
        if "reset" in S.categories(nl, net):
            up = S.find_pulls(nl, net)["up"]
            tps = sorted({nd["ref"] for nd in nl["nets"][net] if nd["ref"].startswith("TP")}, key=S.natkey)
            if up:
                v = S.rail_voltage(up[0][2])
                rows.append("%d,%s,%s,high >= 0.7*%.2f = %.2f V after release" % (
                    len(rows), net, tps[0] if tps else "?", v, 0.7 * v))
    return rows


# ---------------------------------------------------------------- 테스트
g_pass = g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok, why = bool(fn()), ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    g_pass += ok
    print(("PASS  %s" if ok else "FAIL  %s") % name + ("" if ok else why))


def move_pin(nl, ref, pin, new):
    n = copy.deepcopy(nl)
    old = n["pins"][ref][pin]
    nd = next(x for x in n["nets"][old] if x["ref"] == ref and x["pin"] == pin)
    n["nets"][old].remove(nd)
    n["nets"][new].append(nd)
    n["pins"][ref][pin] = new
    return n


def main():
    A = S.load_netlist(S.read("hwb01_revA.net"))
    B = S.load_netlist(S.read("hwb01_revB.net"))

    print("-- 연습 1: domain mismatch --")
    check("revB: no mismatch", lambda: domain_mismatch(B) == [])
    bad = move_pin(B, "R3", "2", "+3V3")
    check("R3 -> +3V3: U7 (1.8V-only) on /I2C2_SCL flagged",
          lambda: domain_mismatch(bad) == [("/I2C2_SCL", "U7", "+3V3", 3.3, 1.8)])
    check("U1 (has 3.3V supply) not flagged by this simple rule",
          lambda: all(x[1] != "U1" for x in domain_mismatch(bad)))

    print("-- 연습 2: rev diff --")
    d = diff_netlists(A, B)
    for line in d:
        print("      " + line)
    check("comp changes", lambda: [x for x in d if "COMP" in x or x.startswith("DNP")] ==
          ["DNP R4 yes -> no", "+COMP R7 10k", "-COMP R15 4k7", "+COMP TP6 TP_1V2"])
    check("net changes", lambda: [x for x in d if "NET" in x] == [
        "NET +3V3 -R15.2", "NET /BOOT_SEL +R7.1", "NET /I2C1_SDA -R15.1", "NET /IMU_INT +U7.6",
        "-NET /IMU_INT1", "NET GND +R7.2 +U6.7", "NET VDD_1V2 +TP6.1", "-NET unconnected-(U6-WP-Pad7)"])
    check("diff(B, B) == []", lambda: diff_netlists(B, B) == [])

    print("-- 연습 3: split-net candidates --")
    check("revA -> [(/IMU_INT, /IMU_INT1)]", lambda: split_net_candidates(A) == [("/IMU_INT", "/IMU_INT1")])
    check("revB -> []", lambda: split_net_candidates(B) == [])
    check("lev('IMU_INT','IMU_INT1') == 1", lambda: lev("IMU_INT", "IMU_INT1") == 1)

    print("-- 연습 4: bring-up plan --")
    pa = bringup_plan(A)
    for r in pa:
        print("      " + r)
    check("revA plan rows", lambda: pa == [
        "step,what,where,expect", "1,GND reference,TP7,0 V (ground spring here)",
        "2,+5V,TP1,4.750..5.250 V", "3,+3V3,TP2,3.135..3.465 V", "4,VDD_1V8,TP3,1.710..1.890 V",
        "5,VDD_1V2,C4.1 (no TP),1.140..1.260 V", "6,/NRST,TP4,high >= 0.7*3.30 = 2.31 V after release"])
    check("revB uses TP6 for VDD_1V2", lambda: "5,VDD_1V2,TP6,1.140..1.260 V" in bringup_plan(B))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
