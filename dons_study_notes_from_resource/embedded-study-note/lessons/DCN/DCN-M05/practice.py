#!/usr/bin/env python3
"""DCN-M05 추가 연습 — 같은 시뮬레이터로 한 걸음 더

  연습 1  buffer_sweep()        : 버퍼(XOFF)를 16배로 키우면 데드락이 사라지나? (아니다 — 늦어지고 더 많이 갇힌다)
  연습 2  load_threshold()      : bounce 경로에 호스트 2대씩, 호스트당 rate r. 데드락 문턱 r 은? 호스트 1대면?
  연습 3  classes_needed() + random_bounce_paths() : k-bounce 경로에 필요한 무손실 클래스 수,
                                  시드 고정 무작위 1-bounce 경로 집합 — 그대로면 사이클, 클래스 상승이면 비순환
  연습 4  watchdog_false_positive() : 그냥 혼잡(incast)에서 T 를 너무 작게 잡으면 데드락이 없는데도 버린다
  연습 5  tarjan_scc()          : 사이클 하나가 아니라 "사이클에 걸린 버퍼 전부" = 크기 > 1 인 SCC

  python3 -I practice.py
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True      # 레슨 폴더에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def buffer_sweep(xoffs=(8, 32, 128)):
    """XON = XOFF/2, CAP = XOFF + 4 로 bounce 시나리오 → [(xoff, deadlock_at, 갇힌 패킷)]"""
    out = []
    for x in xoffs:
        r = S.experiment(S.bounce_flows(), xoff=x, xon=x // 2, cap=x + 4)
        out.append((x, r["deadlock_at"], r["residual"]))
    return out


# ---------------------------------------------------------------- 연습 2
def load_threshold(n_hosts=2, rates=None, on=2000):
    """가장 작은 데드락 rate (없으면 None). rates 는 0.01 단위로 훑는다."""
    rates = rates or [i / 100.0 for i in range(10, 101)]
    for r in rates:
        if S.experiment(S.bounce_flows(n_hosts, r), on=on, drain=600)["deadlock_at"] is not None:
            return r
    return None


# ---------------------------------------------------------------- 연습 3
def classes_needed(path):
    """bounce 횟수 k → 무손실 클래스 k+1 개가 필요 (클래스는 bounce 마다 +1, 내려가지 않는다)."""
    k = sum(1 for i, n in enumerate(path) if 0 < i < len(path) - 1 and S.is_leaf(n))
    return k + 1


def random_bounce_paths(n, seed=7):
    """4 leaf × 2 spine (장애 없음)에서 무작위 1-bounce 경로 n 개: leaf a → spine → leaf b → spine → leaf c.
    a≠b, b≠c, 두 spine 은 서로 다르게. random.Random(seed) 로 결정적."""
    rnd = random.Random(seed)
    out = []
    for _ in range(n):
        a, b, c = rnd.sample(S.LEAVES, 3)
        s1, s2 = rnd.sample(S.SPINES, 2)
        out.append([a, s1, b, s2, c])
    return out


# ---------------------------------------------------------------- 연습 4
def watchdog_false_positive(ts=(10, 20, 27, 28, 40)):
    """incast(데드락 없음)에서 T 별 watchdog drop 수 + watchdog 없이 본 최대 연속 PAUSE 막힘."""
    base = S.experiment(S.incast_flows())
    return base["max_age"], {t: S.experiment(S.incast_flows(), watchdog=t)["drop_wd"] for t in ts}


# ---------------------------------------------------------------- 연습 5
def tarjan_scc(g):
    """Tarjan 1972. 재귀 대신 명시 스택 (반복형). 결과: SCC 목록 (각 SCC 는 정렬된 노드 목록), 정렬."""
    index, low, on, st, out = {}, {}, set(), [], []
    counter = [0]
    for root in sorted(g):
        if root in index:
            continue
        work = [(root, iter(g[root]))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        st.append(root)
        on.add(root)
        while work:
            v, it = work[-1]
            w = next(it, None)
            if w is not None:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    st.append(w)
                    on.add(w)
                    work.append((w, iter(g.get(w, ()))))
                elif w in on:
                    low[v] = min(low[v], index[w])
                continue
            work.pop()
            if work:
                u = work[-1][0]
                low[u] = min(low[u], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    x = st.pop()
                    on.discard(x)
                    comp.append(x)
                    if x == v:
                        break
                out.append(sorted(comp))
    return sorted(out)


# ---------------------------------------------------------------- 하네스
g_pass = g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def main():
    print("-- 연습 1: bigger buffers --")
    bs = buffer_sweep()
    check("XOFF 8/32/128 all deadlock, later and with more trapped packets: %s" % bs,
          lambda: all(d is not None for _, d, _ in bs)
          and bs[0][1] < bs[1][1] < bs[2][1] and bs[0][2] < bs[1][2] < bs[2][2])
    check("trapped packets grow ~linearly with XOFF (64 -> 908 for 8 -> 128)",
          lambda: bs[0][2] == 64 and bs[2][2] == 908)

    print("-- 연습 2: load threshold --")
    th2 = load_threshold(2)
    check("2 hosts/path: first deadlocking rate = %s (shared-link demand 4r crosses 1.0)" % th2,
          lambda: th2 == 0.26)
    r1 = S.experiment(S.bounce_flows(1, 1.0), on=2000, drain=600)
    check("1 host/path at line rate (demand 2.0!): PAUSE fires %d times but no deadlock"
          % r1["pause_events"], lambda: r1["deadlock_at"] is None and r1["pause_events"] > 0
          and r1["residual"] == 0)
    ever, peak = pause_profile(S.bounce_flows(1, 1.0), S.EXPECT_CYCLE)
    print("      buffers that ever asserted PAUSE: %s; peak occupancy of cycle buffers: %d"
          % (", ".join(S.fmt_node(n) for n in ever), peak))
    check("... because only HOST-side buffers ever pause; the cycle buffers never reach XOFF",
          lambda: ever and all(n[1].startswith("h") for n in ever) and peak < S.XOFF)

    print("-- 연습 3: finite lossless classes --")
    p2 = ["L2", "S0", "L1", "S1", "L0", "S0", "L3"]
    check("classes_needed: 0-bounce 1, 1-bounce 2, 2-bounce 3",
          lambda: classes_needed(["L0", "S0", "L1"]) == 1
          and classes_needed(S.F1_PATH) == 2 and classes_needed(p2) == 3)
    check("2-bounce with 3 lossless classes: [3,3,3,4,4,5,5], no LOSSY",
          lambda: S.bump_classes(p2, 3, 3) == [3, 3, 3, 4, 4, 5, 5])
    paths = random_bounce_paths(12)
    raw = [S.Flow("R%d" % i, p) for i, p in enumerate(paths)]
    bumped = [S.Flow("R%d" % i, p, classes=S.bump_classes(p)) for i, p in enumerate(paths)]
    cyc = S.find_cycle(S.build_bdg(raw))
    print("      12 random 1-bounce paths, one class: cycle %s" % S.fmt_cycle(cyc))
    check("12 random 1-bounce paths: one class -> cycle; bumped classes -> acyclic",
          lambda: cyc is not None and S.find_cycle(S.build_bdg(bumped)) is None)

    print("-- 연습 4: watchdog false positive --")
    max_age, drops = watchdog_false_positive()
    print("      incast max continuous PAUSE-blocked age = %d, drops by T = %s" % (max_age, drops))
    check("T <= max_age drops packets in pure congestion; T > max_age drops none",
          lambda: max_age == 27 and all(drops[t] > 0 for t in (10, 20, 27))
          and drops[28] == 0 and drops[40] == 0)

    print("-- 연습 5: Tarjan SCC --")
    healthy = S.Topo()
    allp = [S.Flow("P%d" % i, S.ecmp_path(healthy, s, d, h)) for i, (s, d, h) in enumerate(
        (s, d, h) for s in S.LEAVES for d in S.LEAVES if s != d for h in (0, 1))]
    g_h = S.build_bdg(allp)
    g_b = S.build_bdg(S.bounce_flows())
    g_u = S.build_bdg(allp + S.bounce_flows())
    big = lambda g: [c for c in tarjan_scc(g) if len(c) > 1 or c[0] in g[c[0]]]  # noqa: E731
    check("healthy BDG: 20 SCCs, all trivial", lambda: len(tarjan_scc(g_h)) == 20 and big(g_h) == [])
    check("bounce BDG: exactly one non-trivial SCC = the 4 cycle buffers",
          lambda: big(g_b) == [sorted(S.EXPECT_CYCLE)])
    check("healthy routes + bounce routes together: still that one SCC (only bounce edges close loops)",
          lambda: big(g_u) == [sorted(S.EXPECT_CYCLE)])
    check("Tarjan on 3-ring + tail + self-loop: [[a,b,c],[t],[x]] with x self-loop flagged",
          lambda: tarjan_scc({"a": ["b"], "b": ["c"], "c": ["a"], "t": ["a"], "x": ["x"]})
          == [["a", "b", "c"], ["t"], ["x"]] and big({"x": ["x"]}) == [["x"]])

    print("")
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def pause_profile(flows, nodes, on=2000, drain=600):
    """(실행 중 한 번이라도 PAUSE 를 건 버퍼 목록, nodes 의 최대 점유) — 연습 2 확인용."""
    for f in flows:
        f.stop = on
    sim = S.Sim(flows)
    ever, peak = set(), 0
    for _ in range(on + drain):
        sim.step()
        ever |= {n for n, p in sim.paused.items() if p}
        peak = max([peak] + [len(sim.buf[n]) for n in nodes])
    return sorted(ever), peak


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
