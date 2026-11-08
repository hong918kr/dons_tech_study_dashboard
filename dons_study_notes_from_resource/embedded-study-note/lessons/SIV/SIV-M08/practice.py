#!/usr/bin/env python3
"""SIV-M08 추가 연습 — 같은 pvt_matrix.json 으로 스케줄러를 넓힌다

  연습 1  best_temp_order_dp     : Held-Karp DP (O(2^K K^2)) — 완전 탐색(K!)과 200개 랜덤 사례에서 비용 일치
  연습 2  best_nesting           : 루프 중첩 순서 6가지를 모두 계산 → 챔버면 온도 최외곽, 열 헤드(ThermoStream류)면 칩 최외곽
  연습 3  split_two_chambers     : 챔버 2대에 칩을 나눠 makespan(늦게 끝나는 쪽) 최소화
  연습 4  wall_clock_sheet       : 런 시트에 실제 시각(금 18:00 시작)을 붙이고 예약 종료 이후 블록 표시

  python3 -I practice.py
"""
import datetime
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


# ---- 본문(solution.py)의 Part A 를 축약해 넣었다 (레슨 디렉터리끼리 import 하지 않는다) ----
def chip_ids(cfg):
    return [c["id"] for c in cfg["chips"]]


def volt_names(cfg):
    return [v["name"] for v in cfg["volts"]]


def suite_min(cfg):
    return {s["name"]: s["min"] for s in cfg["suites"]}


def full_items(cfg):
    return [(t, c, v, s["name"]) for t in cfg["temps_c"] for c in chip_ids(cfg)
            for v in volt_names(cfg) for s in cfg["suites"]]


def soak_min(ch, t):
    return ch["soak_min"].get(str(t), ch["default_soak_min"])


def transition_min(ch, a, b, soak=True):
    if a == b:
        return 0.0
    rate = ch["heat_c_per_min"] if b > a else ch["cool_c_per_min"]
    return abs(b - a) / rate + (soak_min(ch, b) if soak else 0.0)


def tour_cost(ch, order):
    cur, total = ch["start_c"], 0.0
    for t in order:
        total += transition_min(ch, cur, t)
        cur = t
    return total + transition_min(ch, cur, ch["end_c"], soak=False)


def best_temp_order(ch, temps, max_k=8):
    ts = sorted(set(temps))
    if len(ts) > max_k:
        raise ValueError("too many temperatures")
    best = None
    for p in itertools.permutations(ts):
        c = tour_cost(ch, p)
        if best is None or c < best[0] - EPS:
            best = (c, p)
    return list(best[1])


def order_items(cfg, items):
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
    ch, ov, smin = cfg["chamber"], cfg["overhead"], suite_min(cfg)
    total, cur_t, cur_c, cur_v = 0.0, ch["start_c"], None, None
    for (t, c, v, s) in seq:
        if t != cur_t:
            total += transition_min(ch, cur_t, t)
            cur_t = t
        if c != cur_c:
            total += ov["dut_switch_min"]
            cur_c = c
        if v != cur_v:
            total += ov["volt_change_min"]
            cur_v = v
        total += smin[s]
    return total + transition_min(ch, cur_t, ch["end_c"], soak=False)


def plan_cost(cfg, items):
    return sequence_cost(cfg, order_items(cfg, items))


# ---------------------------------------------------------------- 연습 1: Held-Karp DP
def best_temp_order_dp(ch, temps):
    """dp[mask][j] = start 에서 출발해 mask 의 온도를 모두 방문하고 j 에서 끝나는 최소 비용"""
    ts = sorted(set(temps))
    k = len(ts)
    if k == 0:
        return []
    inf = float("inf")
    dp = [[inf] * k for _ in range(1 << k)]
    par = [[-1] * k for _ in range(1 << k)]
    for j in range(k):
        dp[1 << j][j] = transition_min(ch, ch["start_c"], ts[j])
    for mask in range(1, 1 << k):
        for j in range(k):
            cur = dp[mask][j]
            if not (mask >> j) & 1 or cur == inf:
                continue
            for n in range(k):
                if (mask >> n) & 1:
                    continue
                nm, c = mask | (1 << n), cur + transition_min(ch, ts[j], ts[n])
                if c < dp[nm][n] - EPS:
                    dp[nm][n], par[nm][n] = c, j
    full = (1 << k) - 1
    j = min(range(k), key=lambda x: dp[full][x] + transition_min(ch, ts[x], ch["end_c"], soak=False))
    order, mask = [], full
    while j != -1:
        order.append(ts[j])
        j, mask = par[mask][j], mask ^ (1 << j)
    return order[::-1]


# ---------------------------------------------------------------- 연습 2: 루프 중첩 순서
def snake(levels):
    """mixed-radix reflected Gray 순서: 레벨 d 의 목록은 들어갈 때마다 방향을 뒤집는다"""
    out, flips = [], [False] * len(levels)

    def rec(d, prefix):
        if d == len(levels):
            out.append(prefix)
            return
        lst = levels[d][::-1] if flips[d] else levels[d]
        flips[d] = not flips[d]
        for x in lst:
            rec(d + 1, prefix + (x,))
    rec(0, ())
    return out


def nesting_costs(cfg):
    """6가지 중첩(바깥→안) 각각의 총 시간, 오름차순 [(cost, nest)]"""
    lists = {"temp": best_temp_order(cfg["chamber"], cfg["temps_c"]),
             "chip": chip_ids(cfg), "volt": volt_names(cfg)}
    sorder = [s["name"] for s in cfg["suites"]]
    res = []
    for nest in itertools.permutations(("temp", "chip", "volt")):
        seq = []
        for combo in snake([lists[n] for n in nest]):
            d = dict(zip(nest, combo))
            seq.extend((d["temp"], d["chip"], d["volt"], s) for s in sorder)
        res.append((sequence_cost(cfg, seq), nest))
    return sorted(res)


def best_nesting(cfg):
    return nesting_costs(cfg)[0]


# ---------------------------------------------------------------- 연습 3: 챔버 2대
def split_two_chambers(cfg):
    """칩을 챔버 A/B 에 나눈다 (칩 0 은 A 고정 — 대칭 제거). makespan 최소, 동률이면 먼저 찾은 것.
    반환 (makespan, A 칩 목록, B 칩 목록)"""
    chips = chip_ids(cfg)
    best = None
    for mask in range(1 << (len(chips) - 1)):
        a = [chips[0]] + [c for i, c in enumerate(chips[1:]) if not (mask >> i) & 1]
        b = [c for c in chips if c not in a]
        span = max(plan_cost(cfg, [it for it in full_items(cfg) if it[1] in grp]) for grp in (a, b))
        if best is None or span < best[0] - EPS:
            best = (span, a, b)
    return best


# ---------------------------------------------------------------- 연습 4: 실제 시각 런 시트
def wall_clock_sheet(cfg, seq, start, booking_end):
    """블록(온도,칩,전압)마다 'Fri 18:00  25C TT-03 VNOM  78.0 min' 형식. 예약 종료 이후 시작 블록엔 ' OVER' 표시.
    반환 (rows, 끝 시각 문자열)"""
    ch, ov, smin = cfg["chamber"], cfg["overhead"], suite_min(cfg)
    blocks, cur = [], (ch["start_c"], None, None)
    for (t, c, v, s) in seq:
        if not blocks or blocks[-1][1:4] != [t, c, v]:
            over = (transition_min(ch, cur[0], t) + (ov["dut_switch_min"] if c != cur[1] else 0)
                    + (ov["volt_change_min"] if v != cur[2] else 0))
            blocks.append([over, t, c, v])
            cur = (t, c, v)
        blocks[-1][0] += smin[s]
    rows, clock = [], start
    for dur, t, c, v in blocks:
        rows.append("%s  %4dC %-6s %-5s %6.1f min%s" % (clock.strftime("%a %H:%M"), t, c, v, dur,
                                                     " OVER" if clock >= booking_end else ""))
        clock += datetime.timedelta(minutes=dur)
    clock += datetime.timedelta(minutes=transition_min(ch, cur[0], ch["end_c"], soak=False))
    return rows, clock.strftime("%a %H:%M")


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
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


def random_chamber(rng):
    grid = list(range(-55, 151, 5))
    return {"start_c": rng.choice(grid), "end_c": rng.choice(grid),
            "heat_c_per_min": rng.choice([1.0, 2.5, 5.0, 8.0, 10.0]),
            "cool_c_per_min": rng.choice([1.0, 2.0, 2.5, 4.0, 6.0]),
            "soak_min": {str(t): rng.randint(5, 40) for t in grid if rng.random() < 0.5},
            "default_soak_min": rng.randint(5, 40)}, grid


def main():
    cfg = load_json("pvt_matrix.json")

    print("-- 연습 1: Held-Karp DP vs brute force --")

    def dp_matches():
        rng = random.Random(808)
        for _ in range(200):
            ch, grid = random_chamber(rng)
            temps = rng.sample(grid, rng.randint(1, 7))
            if abs(tour_cost(ch, best_temp_order_dp(ch, temps)) - tour_cost(ch, best_temp_order(ch, temps))) > 1e-6:
                return False
        return True
    check("200 random chambers (seed 808, K=1..7): DP cost == brute-force cost", dp_matches)
    check("matrix temps: DP order cost 149 like brute force",
          lambda: abs(tour_cost(cfg["chamber"], best_temp_order_dp(cfg["chamber"], cfg["temps_c"])) - 149) < 1e-6)
    t10 = list(range(-40, 126, 18))

    def dp_k10():
        ch = cfg["chamber"]
        o = best_temp_order_dp(ch, t10)
        return (sorted(o) == t10 and tour_cost(ch, o) <= tour_cost(ch, t10) + EPS
                and tour_cost(ch, o) <= tour_cost(ch, t10[::-1]) + EPS)
    check("K=10 (brute force would be 3,628,800 orders): DP returns a permutation <= both sweeps", dp_k10)

    print("-- 연습 2: loop nesting order --")
    check("chamber: best nesting = temp > chip > volt, 2572 min",
          lambda: best_nesting(cfg) == (2572.0, ("temp", "chip", "volt")))
    ts_cfg = json.loads(json.dumps(cfg))
    ts_cfg["chamber"].update(heat_c_per_min=20.0, cool_c_per_min=20.0, soak_min={}, default_soak_min=3)
    ts_cfg["overhead"]["dut_switch_min"] = 15
    check("thermal head + manual socket swap (15 min): best nesting = chip > temp > volt",
          lambda: best_nesting(ts_cfg)[1] == ("chip", "temp", "volt"))
    check("volt is innermost in the best order for both setups",
          lambda: best_nesting(cfg)[1][2] == "volt" and best_nesting(ts_cfg)[1][2] == "volt")

    print("-- 연습 3: two chambers --")
    span, a, b = split_two_chambers(cfg)
    check("two chambers: makespan 1762 (vs 2572 single)", lambda: abs(span - 1762) < 1e-6)
    check("two chambers: split is 2 chips + 1 chip", lambda: sorted([len(a), len(b)]) == [1, 2])

    print("-- 연습 4: wall-clock run sheet --")
    start = datetime.datetime(2026, 11, 13, 18, 0)          # 금요일 18:00
    end = start + datetime.timedelta(hours=24)
    rows, fin = wall_clock_sheet(cfg, order_items(cfg, full_items(cfg)), start, end)
    check("full plan: 27 blocks, first 'Fri 18:00 25C TT-03 VNOM'",
          lambda: len(rows) == 27 and rows[0].startswith("Fri 18:00    25C TT-03  VNOM"))
    check("full plan ends Sun 12:52 (2572 min after Fri 18:00)", lambda: fin == "Sun 12:52")
    check("blocks starting after the 24h booking are marked OVER (and only those)",
          lambda: any(r.endswith(" OVER") for r in rows) and not rows[0].endswith(" OVER")
          and all(r.endswith(" OVER") for r in rows[[r.endswith(" OVER") for r in rows].index(True):]))
    for r in rows:
        print("      " + r)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
