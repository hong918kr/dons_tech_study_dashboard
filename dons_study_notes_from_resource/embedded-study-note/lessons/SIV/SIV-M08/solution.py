#!/usr/bin/env python3
"""SIV-M08 PVT 코너 매트릭스와 테스트 시간 예산

입력 pvt_matrix.json = 칩(SS/TT/FF) x 전압 x 온도 x 테스트 스위트(소요시간·우선순위) + 챔버/오버헤드 모델.
  Part A  비용 모델(램프+soak, DUT 교체, 전압 변경)과 최적 실행 순서:
          온도는 최외곽 루프 + 온도 방문 순서 완전 탐색, 안쪽(칩·전압)은 뱀(snake) 순서
  Part B  어려운 부분: 시작≠종료 온도, 동률 처리, 셀 우선순위 규칙(first-match), 비가산(non-additive)
          비용을 고려한 예산 축소(우선순위 tier + 한계비용 최소 우선), 불가능(INFEASIBLE) 판정
  Part C  시나리오: 24시간 챔버 예약 → 축소 계획 + 런 시트, 동료가 스프레드시트로 만든 계획
          (handmade_plan.csv, 결함 심어 둠)을 감사(audit)해 결함을 시그니처로 보고

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report   # 테스트 + Part C 리포트 출력

Python 3.9 표준 라이브러리만 쓴다. 시간 단위는 전부 분(min).
"""
import csv
import itertools
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EPS = 1e-9


def load_json(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


def load_plan_csv(name):
    """스프레드시트에서 내보낸 계획 → (steps, claimed_min). '#' 줄의 claimed_total_min=... 을 읽는다."""
    claimed, lines = None, []
    with open(os.path.join(HERE, name), encoding="utf-8", newline="") as f:
        for line in f:
            if line.startswith("#"):
                if "claimed_total_min=" in line:
                    claimed = float(line.split("claimed_total_min=")[1].split()[0])
                continue
            lines.append(line)
    steps = [(int(r["temp_c"]), r["chip"], r["volt"], r["suite"]) for r in csv.DictReader(lines)]
    return steps, claimed


# ---------------------------------------------------------------- 매트릭스 헬퍼 (주어진 코드)
# item = (temp_c, chip_id, volt_name, suite_name)  — 실행 단위 하나
def chip_ids(cfg):
    return [c["id"] for c in cfg["chips"]]


def volt_names(cfg):
    return [v["name"] for v in cfg["volts"]]


def suite_min(cfg):
    return {s["name"]: s["min"] for s in cfg["suites"]}


def corner_of(cfg, chip):
    return next(c["corner"] for c in cfg["chips"] if c["id"] == chip)


def full_items(cfg):
    """전체 매트릭스. 순서 = temps_c x chips x volts x suites (cfg 순서) — 결정적 tie-break 키로도 쓴다."""
    return [(t, c, v, s["name"]) for t in cfg["temps_c"] for c in chip_ids(cfg)
            for v in volt_names(cfg) for s in cfg["suites"]]


def cell_label(cfg, t, c, v):
    return "%s/%s/%d" % (c, v, t)


def soak_min(ch, t):
    return ch["soak_min"].get(str(t), ch["default_soak_min"])


def zero_cost():
    return {"test": 0.0, "temp": 0.0, "dut": 0.0, "volt": 0.0, "total": 0.0,
            "temp_changes": 0, "dut_switches": 0, "volt_changes": 0}


# ---------------------------------------------------------------- Part A: 비용 모델과 순서
def transition_min(ch, a, b, soak=True):
    """챔버 a℃ → b℃: 램프(가열/냉각 속도 다름) + 도착 온도 soak. a == b 면 0 (이미 안정)"""
    if a == b:
        return 0.0
    rate = ch["heat_c_per_min"] if b > a else ch["cool_c_per_min"]
    return abs(b - a) / rate + (soak_min(ch, b) if soak else 0.0)


def tour_cost(ch, order):
    """start_c → order[0] → ... → order[-1] → end_c. 마지막 복귀는 램프만 (soak 없음)"""
    cur, total = ch["start_c"], 0.0
    for t in order:
        total += transition_min(ch, cur, t)
        cur = t
    return total + transition_min(ch, cur, ch["end_c"], soak=False)


def best_temp_order(ch, temps, max_k=8):
    """tour_cost 가 최소인 온도 방문 순서. 동률이면 사전순으로 가장 작은 튜플(= 차가운 쪽 먼저).
    len(set(temps)) > max_k 면 ValueError (K! 완전 탐색의 한계 — 추가 연습 1 의 DP 참고)"""
    ts = sorted(set(temps))
    if len(ts) > max_k:
        raise ValueError("%d temperatures > max_k=%d (use DP)" % (len(ts), max_k))
    best = None
    for p in itertools.permutations(ts):          # 정렬된 입력 → 사전순으로 나온다
        c = tour_cost(ch, p)
        if best is None or c < best[0] - EPS:     # 엄격히 더 좋을 때만 교체 → 동률은 먼저 나온 것
            best = (c, p)
    return list(best[1])


def order_items(cfg, items):
    """items → 실행 순서. 온도 = 최외곽(best_temp_order), 그 안에서 칩, 그 안에서 전압을
    뱀(snake, reflected Gray) 순서로: 칩 목록은 온도 블록마다, 전압 목록은 (온도,칩) 블록마다 방향을 뒤집는다.
    존재하는 셀만 방문하고, 셀 안의 suite 는 cfg 순서."""
    by_cell = {}
    for (t, c, v, s) in set(items):
        by_cell.setdefault((t, c, v), set()).add(s)
    sorder = [s["name"] for s in cfg["suites"]]
    seq, flip_c, flip_v = [], False, False
    for t in best_temp_order(cfg["chamber"], [k[0] for k in by_cell]):
        cs = [c for c in chip_ids(cfg) if any((t, c, v) in by_cell for v in volt_names(cfg))]
        if flip_c:
            cs.reverse()
        flip_c = not flip_c
        for c in cs:
            vs = [v for v in volt_names(cfg) if (t, c, v) in by_cell]
            if flip_v:
                vs.reverse()
            flip_v = not flip_v
            for v in vs:
                seq.extend((t, c, v, s) for s in sorder if s in by_cell[(t, c, v)])
    return seq


def sequence_cost(cfg, seq):
    """seq 를 그 순서대로 실행할 때의 시간 내역. 상태 = (챔버 온도, 전원 켠 칩, 전압).
    시작 상태 (start_c, 없음, 없음) — 첫 스텝은 DUT 전원 인가(dut_switch)와 전압 설정을 낸다.
    바뀐 좌표마다 비용: 온도 transition_min / 칩 dut_switch_min / 전압 volt_change_min. 끝나면 end_c 로 복귀."""
    ch, ov, smin = cfg["chamber"], cfg["overhead"], suite_min(cfg)
    b = zero_cost()
    cur_t, cur_c, cur_v = ch["start_c"], None, None
    for (t, c, v, s) in seq:
        if t != cur_t:
            b["temp"] += transition_min(ch, cur_t, t)
            b["temp_changes"] += 1
            cur_t = t
        if c != cur_c:
            b["dut"] += ov["dut_switch_min"]
            b["dut_switches"] += 1
            cur_c = c
        if v != cur_v:
            b["volt"] += ov["volt_change_min"]
            b["volt_changes"] += 1
            cur_v = v
        b["test"] += smin[s]
    b["temp"] += transition_min(ch, cur_t, ch["end_c"], soak=False)
    b["total"] = b["test"] + b["temp"] + b["dut"] + b["volt"]
    return b


def plan_cost(cfg, items):
    return sequence_cost(cfg, order_items(cfg, items))["total"]


def naive_sequence(cfg):
    """'for chip: for volt: for temp: for suite' — 처음 짜는 스크립트의 순서 (비교용, 주어진 코드)"""
    return [(t, c, v, s["name"]) for c in chip_ids(cfg) for v in volt_names(cfg)
            for t in cfg["temps_c"] for s in cfg["suites"]]


# ---------------------------------------------------------------- Part B: 우선순위와 예산 축소
def cell_prio(cfg, t, chip, v):
    """cfg["cell_prio"] 규칙을 위에서부터 보고 처음 맞는 규칙의 prio (first-match). "*" 는 와일드카드.
    규칙의 corner 는 칩의 corner 와 비교한다. 아무것도 안 맞으면 default_cell_prio."""
    corner = corner_of(cfg, chip)
    for r in cfg["cell_prio"]:
        if all(r[k] == "*" or r[k] == x for k, x in (("corner", corner), ("volt", v), ("temp", t))):
            return r["prio"]
    return cfg["default_cell_prio"]


def item_tier(cfg, item):
    """item 의 tier = max(셀 우선순위, 스위트 우선순위). P1 셀의 P3 스위트는 P3. (주어진 코드)"""
    t, c, v, s = item
    sp = next(x["prio"] for x in cfg["suites"] if x["name"] == s)
    return max(cell_prio(cfg, t, c, v), sp)


def reduce_plan(cfg, budget):
    """예산(분) 안에 들어가는 계획. 반환 dict:
      status  : "FULL"(전부 들어감) / "REDUCED" / "INFEASIBLE"(tier 1 만으로도 초과)
      items   : 선택된 item 목록 (full_items 순서)
      seq     : order_items(items),  cost: sequence_cost(seq)
      dropped : 빠진 item 목록 (full_items 순서),  deficit: INFEASIBLE 일 때 초과분(분), 아니면 0
    규칙: tier 1 → 2 → ... 순서로 tier 전체를 넣어 본다. 다 들어가면 다음 tier.
    안 들어가는 tier 는 '한계비용(추가했을 때 plan_cost 증가분)이 가장 작은 item' 을 하나씩 넣고
    (동률 → full_items 순서), 가장 싼 것도 안 들어가면 멈춘다. 그 아래 tier 는 넣지 않는다 (엄격한 우선순위)."""
    allit = full_items(cfg)
    key = {it: i for i, it in enumerate(allit)}

    def pack(status, sel, deficit=0.0):
        chosen = set(sel)
        items = [it for it in allit if it in chosen]
        seq = order_items(cfg, items)
        return {"status": status, "items": items, "seq": seq, "cost": sequence_cost(cfg, seq),
                "dropped": [it for it in allit if it not in chosen], "deficit": deficit}

    if plan_cost(cfg, allit) <= budget + EPS:
        return pack("FULL", allit)
    sel, base = [], plan_cost(cfg, [])
    for k, tier in enumerate(sorted({item_tier(cfg, it) for it in allit})):
        cands = [it for it in allit if item_tier(cfg, it) == tier]
        trial = plan_cost(cfg, sel + cands)
        if k == 0 and trial > budget + EPS:
            return pack("INFEASIBLE", cands, trial - budget)
        if trial <= budget + EPS:
            sel, base = sel + cands, trial
            continue
        while cands:
            (marg, _), it = min(((plan_cost(cfg, sel + [c]) - base, key[c]), c) for c in cands)
            if base + marg > budget + EPS:
                break
            sel.append(it)
            cands.remove(it)
            base += marg
        break
    return pack("REDUCED", sel)


# ---------------------------------------------------------------- Part C: 계획 감사(audit)
def audit_plan(cfg, steps, budget, claimed=None):
    """사람이 만든 계획(steps 순서 그대로)을 검사 → (findings, cost).
    findings 각 줄은 코드로 시작한다 (step 번호는 1부터):
      E_UNKNOWN step=N <field>=<값>         매트릭스에 없는 temp/chip/volt/suite (실행 불가 → 비용에서 제외)
      W_DUP step=N item=<cell>:<suite> first=M  같은 item 이 두 번 (두 번 다 실행·과금)
      E_MUST_MISSING cell=<cell> suites=a,b  tier 1 item 누락 (셀별로 묶어 full_items 순서)
      E_TEMP_REVISIT temp=T visits=K        같은 온도 블록이 K(>1)번 나뉘어 방문 (온도 오름차순)
      E_CLAIM_MISMATCH claimed=X computed=Y diff=+Z   |X-Y| > 1 분
      E_OVER_BUDGET total=Y budget=B over=Z
      W_SUBOPTIMAL total=Y optimal=O saving=S   같은 (유일) item 을 order_items 로 돌렸을 때 1 분 넘게 이득
    숫자는 %.1f 형식."""
    temps, chips_, volts_, smin = set(cfg["temps_c"]), set(chip_ids(cfg)), set(volt_names(cfg)), suite_min(cfg)
    f, valid, first = [], [], {}
    for i, it in enumerate(steps, 1):
        t, c, v, s = it
        bad = [(n, x) for n, x, ok in (("temp", t, t in temps), ("chip", c, c in chips_),
                                         ("volt", v, v in volts_), ("suite", s, s in smin)) if not ok]
        if bad:
            f.extend("E_UNKNOWN step=%d %s=%s" % (i, n, x) for n, x in bad)
            continue
        if it in first:
            f.append("W_DUP step=%d item=%s:%s first=%d" % (i, cell_label(cfg, t, c, v), s, first[it]))
        else:
            first[it] = i
        valid.append(it)
    missing = {}
    for it in full_items(cfg):
        if item_tier(cfg, it) == 1 and it not in first:
            missing.setdefault(it[:3], []).append(it[3])
    for (t, c, v), ss in missing.items():
        f.append("E_MUST_MISSING cell=%s suites=%s" % (cell_label(cfg, t, c, v), ",".join(ss)))
    visits, prev = {}, None
    for it in valid:
        if it[0] != prev:
            visits[it[0]] = visits.get(it[0], 0) + 1
            prev = it[0]
    for t in sorted(visits):
        if visits[t] > 1:
            f.append("E_TEMP_REVISIT temp=%d visits=%d" % (t, visits[t]))
    cost = sequence_cost(cfg, valid)
    tot = cost["total"]
    if claimed is not None and abs(claimed - tot) > 1.0:
        f.append("E_CLAIM_MISMATCH claimed=%.1f computed=%.1f diff=%+.1f" % (claimed, tot, tot - claimed))
    if tot > budget + EPS:
        f.append("E_OVER_BUDGET total=%.1f budget=%.1f over=%.1f" % (tot, budget, tot - budget))
    opt = plan_cost(cfg, list(first))
    if tot - opt > 1.0:
        f.append("W_SUBOPTIMAL total=%.1f optimal=%.1f saving=%.1f" % (tot, opt, tot - opt))
    return f, cost


# ---------------------------------------------------------------- 리포트 (주어진 코드)
def hm(minutes):
    m = int(round(minutes))
    return "%d:%02d" % (m // 60, m % 60)


def fmt_cost(b):
    return ("total %.1f min (%s) = test %.1f + temp %.1f (%d changes) + dut %.1f (%d) + volt %.1f (%d)"
            % (b["total"], hm(b["total"]), b["test"], b["temp"], b["temp_changes"],
               b["dut"], b["dut_switches"], b["volt"], b["volt_changes"]))


def run_sheet(cfg, seq):
    """(온도,칩,전압) 블록 단위 런 시트: [(시작 분, 블록 시간, temp, chip, volt, [suites])]. 블록 시간 = 진입 오버헤드 + 테스트"""
    rows, ch, ov, smin = [], cfg["chamber"], cfg["overhead"], suite_min(cfg)
    clock, cur = 0.0, (ch["start_c"], None, None)
    for it in seq:
        t, c, v, s = it
        if not rows or tuple(rows[-1][2:5]) != (t, c, v):
            over = (transition_min(ch, cur[0], t) + (ov["dut_switch_min"] if c != cur[1] else 0)
                    + (ov["volt_change_min"] if v != cur[2] else 0))
            rows.append([clock, over, t, c, v, []])
            clock += over
            cur = (t, c, v)
        rows[-1][5].append(s)
        rows[-1][1] += smin[s]
        clock += smin[s]
    return rows


def format_report(cfg, budget, res, audit=None):
    out = []
    w = out.append
    allit = full_items(cfg)
    full = sequence_cost(cfg, order_items(cfg, allit))
    naive = sequence_cost(cfg, naive_sequence(cfg))
    w("== %s ==" % cfg["name"])
    w("matrix : %d chips x %d volts x %d temps x %d suites = %d items, test-only %.1f min"
      % (len(cfg["chips"]), len(cfg["volts"]), len(cfg["temps_c"]), len(cfg["suites"]), len(allit),
         full["test"]))
    w("naive  (chip>volt>temp loops): " + fmt_cost(naive))
    w("full   (optimized order)     : " + fmt_cost(full))
    w("budget : %.1f min (%s)  -> status=%s" % (budget, hm(budget), res["status"]))
    w("plan   : " + fmt_cost(res["cost"]) + ("  slack %.1f" % (budget - res["cost"]["total"])))
    torder = []
    for it in res["seq"]:
        if not torder or torder[-1] != it[0]:
            torder.append(it[0])
    w("temps  : start %s -> %s -> end %s" % (cfg["chamber"]["start_c"], " -> ".join(map(str, torder)),
                                             cfg["chamber"]["end_c"]))
    tiers = {}
    for it in allit:
        k = item_tier(cfg, it)
        sel, tot = tiers.get(k, (0, 0))
        tiers[k] = (sel + (it in set(res["items"])), tot + 1)
    w("tiers  : " + "  ".join("P%d %d/%d" % (k, a, b) for k, (a, b) in sorted(tiers.items())))
    w("coverage (suites selected / %d) per cell:" % len(cfg["suites"]))
    chosen = set(res["items"])
    w("  %-12s" % "" + "".join("%8d" % t for t in cfg["temps_c"]))
    for c in chip_ids(cfg):
        for v in volt_names(cfg):
            cells = []
            for t in cfg["temps_c"]:
                n = sum((t, c, v, s["name"]) in chosen for s in cfg["suites"])
                cells.append("%8s" % ("%d/%d P%d" % (n, len(cfg["suites"]), cell_prio(cfg, t, c, v))
                                      if n else "-- P%d" % cell_prio(cfg, t, c, v)))
            w("  %-12s" % ("%s %s" % (c, v)) + "".join(cells))
    w("run sheet (block = one temp/chip/volt; start offset h:mm):")
    for (t0, dur, t, c, v, ss) in run_sheet(cfg, res["seq"]):
        w("  +%6s %5.1f min  %4dC %-6s %-5s %s" % (hm(t0), dur, t, c, v, ",".join(ss)))
    if audit is not None:
        name, findings, cost = audit
        w("audit of %s: %s" % (name, fmt_cost(cost)))
        for x in findings:
            w("  " + x)
    return "\n".join(out)


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    try:
        ok, why = bool(fn()), ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def close(a, b, tol=1e-6):
    return abs(a - b) <= tol


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def has(lines, code, *words):
    return any(x.startswith(code + " ") and all(wd in x for wd in words) for x in lines)


def chamber(**kw):
    ch = dict(load_json("pvt_matrix.json")["chamber"])
    ch.update(kw)
    return ch


def one_coord_steps(seq):
    """연속한 서로 다른 셀 사이에서 바뀐 좌표 수 목록"""
    cells = [it[:3] for i, it in enumerate(seq) if i == 0 or it[:3] != seq[i - 1][:3]]
    return [sum(a != b for a, b in zip(p, q)) for p, q in zip(cells, cells[1:])]


def run_tests(report=False):
    cfg = load_json("pvt_matrix.json")
    ch = cfg["chamber"]
    allit = full_items(cfg)

    print("-- Part A: cost model + order --")
    check("transition 25 -> -40 = 65/2.5 + soak 30 = 56", lambda: close(transition_min(ch, 25, -40), 56))
    check("transition -40 -> 125 = 165/5 + soak 20 = 53", lambda: close(transition_min(ch, -40, 125), 53))
    check("transition 125 -> 25 ramp only = 40", lambda: close(transition_min(ch, 125, 25, soak=False), 40))
    check("transition 25 -> 25 = 0 (already stable)", lambda: close(transition_min(ch, 25, 25), 0))
    check("tour [25,-40,125] = 0+56+53+40 = 149", lambda: close(tour_cost(ch, [25, -40, 125]), 149))
    check("tour [-40,25,125] = 56+23+40+40 = 159", lambda: close(tour_cost(ch, [-40, 25, 125]), 159))
    check("best order {-40,25,125} = [25,-40,125] (ambient first)",
          lambda: best_temp_order(ch, [125, -40, 25]) == [25, -40, 125])
    seq = order_items(cfg, allit)
    full = sequence_cost(cfg, seq)
    check("order_items is a permutation of all 189 items",
          lambda: len(seq) == 189 and sorted(seq) == sorted(allit))
    check("first step = TT-03/VNOM/25 smoke (fail fast)", lambda: seq[0] == (25, "TT-03", "VNOM", "smoke"))
    check("full plan: 2 temp changes, 7 DUT switches, 19 volt changes",
          lambda: (full["temp_changes"], full["dut_switches"], full["volt_changes"]) == (2, 7, 19))
    check("full plan total = 2376+149+28+19 = 2572", lambda: close(full["total"], 2572))
    check("snake: every cell boundary changes exactly 1 coordinate",
          lambda: set(one_coord_steps(seq)) == {1})
    nv = sequence_cost(cfg, naive_sequence(cfg))
    check("naive loops: 27 temp changes, total 3828", lambda: nv["temp_changes"] == 27 and close(nv["total"], 3828))

    print("-- Part B: hard parts --")
    ch5 = chamber()
    check("5 temps scrambled -> [25,-40,0,85,125] cost 189 (tie -> lexicographic)",
          lambda: best_temp_order(ch5, [125, -40, 85, 0, 25]) == [25, -40, 0, 85, 125]
          and close(tour_cost(ch5, [25, -40, 0, 85, 125]), 189)
          and close(tour_cost(ch5, [25, 85, 125, 0, -40]), 189))
    hot = chamber(start_c=85)
    check("chamber left at 85C -> [125,-40,25] cost 147 (not ambient first)",
          lambda: best_temp_order(hot, [-40, 25, 125]) == [125, -40, 25]
          and close(tour_cost(hot, [125, -40, 25]), 147) and close(tour_cost(hot, [25, -40, 125]), 183))
    check("9 temperatures > max_k -> ValueError",
          lambda: raises(ValueError, lambda: best_temp_order(ch, list(range(-40, 140, 20)))))
    check("cell_prio first-match: TT-03/VNOM/25 = 1 (not the */VNOM/* rule)",
          lambda: cell_prio(cfg, 25, "TT-03", "VNOM") == 1)
    check("cell_prio: SS-07/VMIN/-40 = 1, FF-11/VNOM/-40 = 2, SS-07/VMAX/25 = 3",
          lambda: (cell_prio(cfg, -40, "SS-07", "VMIN"), cell_prio(cfg, -40, "FF-11", "VNOM"),
                   cell_prio(cfg, 25, "SS-07", "VMAX")) == (1, 2, 3))
    check("tier sizes P1/P2/P3 = 15/63/111",
          lambda: [sum(item_tier(cfg, it) == k for it in allit) for k in (1, 2, 3)] == [15, 63, 111])
    a = (25, "TT-03", "VNOM", "smoke")
    b = (-40, "SS-07", "VMIN", "smoke")
    check("non-additive: 3-min smoke at a new temp costs 77 min more (8 -> 85)",
          lambda: close(plan_cost(cfg, [a]), 8) and close(plan_cost(cfg, [a, b]), 85))
    r = reduce_plan(cfg, 1440)
    tier_sel = lambda res, k: [it for it in res["items"] if item_tier(cfg, it) == k]  # noqa: E731
    check("budget 1440: REDUCED, total <= budget", lambda: r["status"] == "REDUCED" and r["cost"]["total"] <= 1440)
    check("budget 1440: all P1 (15) + P2 (63) kept, only P3 dropped",
          lambda: len(tier_sel(r, 1)) == 15 and len(tier_sel(r, 2)) == 63
          and r["dropped"] and all(item_tier(cfg, it) == 3 for it in r["dropped"]))
    check("budget 1440: seq == order_items(items), dropped + items = 189",
          lambda: r["seq"] == order_items(cfg, r["items"]) and len(r["items"]) + len(r["dropped"]) == 189)
    check("budget 1440: deterministic (2nd run identical)", lambda: reduce_plan(cfg, 1440)["seq"] == r["seq"])
    i300 = reduce_plan(cfg, 300)
    check("budget 300: INFEASIBLE, P1-only plan 344, deficit 44",
          lambda: i300["status"] == "INFEASIBLE" and close(i300["cost"]["total"], 344)
          and close(i300["deficit"], 44) and len(i300["items"]) == 15)
    check("budget 5000: FULL", lambda: reduce_plan(cfg, 5000)["status"] == "FULL")

    def random_budgets():
        rng = random.Random(808)
        for _ in range(12):
            bud = rng.randint(344, 2571)
            res = reduce_plan(cfg, bud)
            if res["status"] != "REDUCED" or res["cost"]["total"] > bud + EPS:
                return False
            tiers_dropped = {item_tier(cfg, it) for it in res["dropped"]}
            tiers_kept = {item_tier(cfg, it) for it in res["items"]}
            if 1 not in tiers_kept or len(tier_sel(res, 1)) != 15:
                return False
            if any(k > min(tiers_dropped) for k in tiers_kept):   # 엄격한 우선순위
                return False
        return True
    check("12 random budgets (seed 808): fits, all P1 kept, strict priority", random_budgets)

    print("-- Part C: 24h chamber booking + audit of handmade plan --")
    steps, claimed = load_plan_csv("handmade_plan.csv")
    fa, ca = audit_plan(cfg, steps, cfg["budget_min"], claimed)
    check("handmade: E_UNKNOWN suite=ddr_strss", lambda: has(fa, "E_UNKNOWN", "suite=ddr_strss"))
    check("handmade: W_DUP TT-03/VNOM/25:smoke", lambda: has(fa, "W_DUP", "step=83", "item=TT-03/VNOM/25:smoke", "first=7"))
    check("handmade: E_MUST_MISSING FF-11/VMAX/-40 cpu_stress",
          lambda: has(fa, "E_MUST_MISSING", "cell=FF-11/VMAX/-40", "suites=cpu_stress"))
    check("handmade: E_TEMP_REVISIT -40 x3, 25 x4 (dup at end), 125 x3",
          lambda: all(has(fa, "E_TEMP_REVISIT", "temp=%d " % t, "visits=%d" % k)
                      for t, k in ((-40, 3), (25, 4), (125, 3))))
    check("handmade: E_CLAIM_MISMATCH (spreadsheet forgot ramp/soak)",
          lambda: has(fa, "E_CLAIM_MISMATCH", "claimed=%.1f" % claimed))
    check("handmade: E_OVER_BUDGET", lambda: has(fa, "E_OVER_BUDGET", "budget=1440.0"))
    check("handmade: W_SUBOPTIMAL saving > 300 min",
          lambda: any(x.startswith("W_SUBOPTIMAL") and float(x.split("saving=")[1]) > 300 for x in fa))
    check("handmade: exactly 9 findings", lambda: len(fa) == 9)
    fr, _ = audit_plan(cfg, r["seq"], cfg["budget_min"], r["cost"]["total"])
    check("optimizer's 24h plan passes its own audit (no findings)", lambda: fr == [])
    rep = format_report(cfg, cfg["budget_min"], r, ("handmade_plan.csv", fa, ca))
    check("report: status line + temps line",
          lambda: "status=REDUCED" in rep and "temps  : start 25 -> 25 -> -40 -> 125 -> end 25" in rep)
    if report:
        print()
        print(rep)
        print()

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests("--report" in sys.argv[1:]) else 1)
