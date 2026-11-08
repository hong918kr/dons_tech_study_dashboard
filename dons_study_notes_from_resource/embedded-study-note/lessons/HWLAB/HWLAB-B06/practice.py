#!/usr/bin/env python3
"""HWLAB-B06 추가 연습 — 트리거를 둘러싼 스코프의 나머지 절반

  연습 1  decimate_plain / decimate_peak : 단순 솎아내기 vs 피크 검출(min/max) — 글리치가 사라지나?
  연습 2  interp_crossing                : 트리거 시각을 샘플 사이로 보간 (트리거 지터 ±0.5 샘플 → 수 ps)
  연습 3  live_fraction / p_catch / t_for: 화면만 보고 기다릴 때 드문 글리치를 볼 확률 (dead time)
  연습 4  RuntWidthTrigger               : runt + 폭 조건 (L 상승 ~ L 복귀 시간 < max_w)

  python3 -I practice.py
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True     # 레슨 폴더에 __pycache__ 를 만들지 않는다
sys.path.insert(0, HERE)
import gen_capture as gc            # noqa: E402
import solution as sol              # noqa: E402  (테스트는 main 에서만 돈다)


# ---------------------------------------------------------------- 연습 1
def decimate_plain(buf, k):
    """k 샘플마다 1개 (메모리가 모자라 샘플레이트를 낮춘 스코프의 'normal' 수집)"""
    return list(buf[::k])


def decimate_peak(buf, k):
    """k 샘플 블록마다 (min, max) 한 쌍 (스코프의 'peak detect' 수집 모드)"""
    out = []
    for a in range(0, len(buf), k):
        blk = buf[a:a + k]
        out.append((min(blk), max(blk)))
    return out


def load_window(name):
    """captures/*.csv → 코드 리스트"""
    with open(os.path.join(HERE, "captures", name), encoding="utf-8") as f:
        next(f)
        return [int(line.split(",")[1]) for line in f]


# ---------------------------------------------------------------- 연습 2
def interp_crossing(x, j, level):
    """x[j-1] < level <= x[j] 인 상승 트리거 샘플 j → 선형 보간한 교차 시각 (샘플 단위 실수)"""
    a, b = x[j - 1], x[j]
    return (j - 1) + (level - a) / (b - a)


# ---------------------------------------------------------------- 연습 3
def live_fraction(wfm_per_s, window_s):
    """1초 중 실제로 '보고 있는' 시간 비율 (나머지는 처리·표시하느라 못 보는 dead time)"""
    return min(1.0, wfm_per_s * window_s)


def p_catch(event_rate, live, t):
    """포아송 사건(초당 event_rate)이 t 초 동안 적어도 한 번 수집 창에 들어올 확률"""
    return 1.0 - math.exp(-event_rate * live * t)


def t_for(p, event_rate, live):
    """확률 p 로 한 번은 보려면 기다려야 하는 시간 [s]"""
    return -math.log(1.0 - p) / (event_rate * live)


# ---------------------------------------------------------------- 연습 4
class RuntWidthTrigger(sol.RuntTrigger):
    """runt+ 만, 폭 = L 상승 교차 ~ (L - hys) 복귀 샘플 수. width < max_w 일 때만 트리거.
    → [(trigger_index, width, peak_code)]"""

    def __init__(self, low, high, hys, max_w):
        super().__init__(low, high, hys, "pos")
        self.max_w, self.t0 = max_w, None

    def process(self, chunk, base=0):
        L, H, Llo, Hlo = self.L, self.H, self.L - self.hys, self.H - self.hys
        cL, cH, ap, pk, t0 = self.cL, self.cH, self.ap, self.pk, self.t0
        out = []
        j = base
        for x in chunk:
            if cL == 0:
                if x >= L:
                    cL = 1
                    if x >= H:
                        cH = 1
                    else:
                        ap, pk, t0 = True, x, j
            elif cH == 1:
                if x < Hlo:
                    cH = 0
                    if x < Llo:
                        cL = 0
            elif cL == 1:
                if x >= H:
                    cH, ap = 1, False
                elif x < Llo:
                    cL = 0
                    if ap and j - t0 < self.max_w:
                        out.append((j, j - t0, pk))
                    ap = False
                elif ap and x > pk:
                    pk = x
            else:
                cL = 1 if x >= L else 0
                cH = 1 if x >= H else 0
            j += 1
        self.cL, self.cH, self.ap, self.pk, self.t0 = cL, cH, ap, pk, t0
        return out


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


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
    print("-- 연습 1: plain vs peak-detect decimation --")
    w = load_window("evt2_glitch.csv")           # 800 샘플, 글리치 = 인덱스 195..199 (t = -5..-1 ns)
    seg = w[170:210]                              # 글리치가 든 low 구간 40 ns (클록 에지 제외)
    vmax_plain = gc.code2v(max(decimate_plain(seg, 10)))     # 1 GSa/s → 100 MSa/s
    vmax_peak = gc.code2v(max(hi for _, hi in decimate_peak(seg, 10)))
    print("      low phase around glitch /10: plain max = %.2f V, peak-detect max = %.2f V" % (vmax_plain, vmax_peak))
    check("plain decimation: glitch invisible (max < VIL 0.8 V)", lambda: vmax_plain < 0.8)
    check("peak detect: glitch kept (max > 3.0 V)", lambda: vmax_peak > 3.0)
    r_plain = sol.EdgeTrigger(sol.EDGE_LO, sol.EDGE_HI).process(decimate_plain(w, 10))
    r_peak = sol.EdgeTrigger(sol.EDGE_LO, sol.EDGE_HI).process([hi for _, hi in decimate_peak(w, 10)])
    print("      whole window /10 rising edges: plain %s, peak-detect max %s" % (r_plain, r_peak))
    check("plain: 8 clock edges only; peak-detect: 9 (extra = glitch block 19)",
          lambda: len(r_plain) == 8 and len(r_peak) == 9 and 19 in r_peak and 19 not in r_plain)

    print("-- 연습 2: sub-sample trigger interpolation --")
    rng = random.Random(11)
    lvl = 0.5
    e_int, e_lin = [], []
    for _ in range(200):
        phi = 20 + rng.random()                   # 실제 50% 교차 시각 (샘플 단위)
        x = [gc.step(k - phi) for k in range(40)]
        j = next(k for k in range(1, 40) if x[k - 1] < lvl <= x[k])
        e_int.append(j - phi)
        e_lin.append(interp_crossing(x, j, lvl) - phi)
    pp_int = max(e_int) - min(e_int)
    worst_lin = max(abs(v) for v in e_lin)
    print("      integer trigger stamp error p-p = %.3f ns, interpolated |err| max = %.4f ns" % (pp_int, worst_lin))
    check("integer stamp error spans ~1 sample (p-p > 0.9 ns)", lambda: pp_int > 0.9)
    check("linear interpolation error < 0.05 ns on a 2 ns (10-90) tanh edge", lambda: worst_lin < 0.05)

    print("-- 연습 3: live time & catch probability --")
    live = live_fraction(1000, 10e-6)             # 1000 wfm/s, 화면 10 us
    p10 = p_catch(1.0, live, 10.0)                # 초당 1회 글리치, 10초 지켜봄
    t95 = t_for(0.95, 1.0, live)
    print("      live = %.3f, P(see in 10 s) = %.3f, t(95%%) = %.0f s" % (live, p10, t95))
    check("live fraction 1000 wfm/s x 10 us = 0.01", lambda: abs(live - 0.01) < 1e-12)
    check("P(10 s) = 1 - e^-0.1 = 0.095", lambda: abs(p10 - 0.0952) < 5e-4)
    check("t(95%) = ln(20)/0.01 = 300 s", lambda: abs(t95 - 299.57) < 0.1)
    check("with a glitch trigger (live ~ 1 for isolated events): t(95%) = 3 s",
          lambda: abs(t_for(0.95, 1.0, 1.0) - 2.996) < 0.01)

    print("-- 연습 4: runt with width qualifier --")
    s = [10] * 10 + [50] * 5 + [10] * 10 + [50] * 40 + [10] * 10 + [90] * 20 + [10] * 10
    rw = RuntWidthTrigger(30, 70, 2, max_w=20).process(s)
    print("      runts with width < 20: %s" % rw)
    check("narrow runt (5) caught, wide runt (40) and full pulse ignored", lambda: rw == [(15, 5, 50)])
    check("max_w=100 -> both runts", lambda: [r[1] for r in RuntWidthTrigger(30, 70, 2, 100).process(s)] == [5, 40])
    check("chunked (chunk=3) == one-shot",
          lambda: sol.run_chunked(RuntWidthTrigger(30, 70, 2, 100), s, 3) == RuntWidthTrigger(30, 70, 2, 100).process(s))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
