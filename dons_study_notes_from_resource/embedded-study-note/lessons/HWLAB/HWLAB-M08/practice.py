#!/usr/bin/env python3
"""HWLAB-M08 추가 연습 — 본문 라이브러리(solution.py)를 확장한다

  연습 1  scale_table      : 분주/체배 N → L + 20·log10(N), 분주기 플로어가 있으면 그 아래로 못 내려감
  연습 2  decade_budget    : 지터 예산을 decade 별로 쪼개 "어디를 고쳐야 하나" 를 숫자로
  연습 3  period_jitter    : 위상 잡음 → period jitter (4·sin²(πf/f0) 가중) — HWLAB-M07 의 period jitter 와 연결
  연습 4  units            : σ_φ 를 도(°)·UI·적분 위상 잡음(dBc) 으로 환산

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def scale_table(table, n, floor_db=None):
    """f0 → n·f0 (n<1 이면 분주). 위상 편차 φ 가 n 배 → L 은 20·log10(n) dB 이동.
    floor_db 가 있으면 (분주기·출력 버퍼의 자체 플로어) 결과 L 은 그 값 아래로 내려가지 않는다."""
    d = 20.0 * math.log10(n)
    out = []
    for f, L in table:
        L2 = L + d
        if floor_db is not None:
            L2 = max(L2, floor_db)
        out.append((f, L2))
    return out


# ---------------------------------------------------------------- 연습 2
def decade_budget(osc, f1, f2):
    """[f1, f2] 를 decade 경계(10^k)로 잘라 구간마다 σ_φ² (랜덤 + 그 구간 스퍼) 와 전체 대비 비율 [%].
    반환 [(fa, fb, var, pct), ...]. 분산은 더해지므로(서로 독립) 비율이 곧 '지터² 예산 점유율'."""
    edges = [f1]
    k = math.floor(math.log10(f1)) + 1
    while 10.0 ** k < f2:
        edges.append(10.0 ** k)
        k += 1
    edges.append(f2)
    rows = []
    for fa, fb in zip(edges, edges[1:]):
        sp = [(f, d) for f, d in osc["spurs"] if fa <= f < fb or f == fb == f2]   # 스퍼는 한 구간에만
        var = 2.0 * S.band_integral(osc["table"], fa, fb) + S.spur_phase_var(sp, fa, fb)
        rows.append([fa, fb, var, 0.0])
    tot = sum(r[2] for r in rows)
    for r in rows:
        r[3] = r[2] / tot * 100.0
    return [tuple(r) for r in rows]


# ---------------------------------------------------------------- 연습 3
def period_jitter(table, f0, f1, f2):
    """period jitter σ_per [s]: 한 주기 τ=1/f0 떨어진 두 에지의 TIE 차 x(t+τ) − x(t) 의 RMS.
    차분의 전달함수 |1 − e^{−j2πfτ}|² = 4·sin²(πf/f0) 를 위상 PSD 에 곱해 적분:
      σ_per = √(2·∫ L(f)·4sin²(πf/f0) df) / (2πf0)"""
    w = lambda f: 4.0 * math.sin(math.pi * f / f0) ** 2      # noqa: E731
    return math.sqrt(2.0 * S.integrate_weighted(table, f1, f2, w)) / (S.TWO_PI * f0)


# ---------------------------------------------------------------- 연습 4
def units(phi_rad, f0, bitrate=None):
    """σ_φ → dict(deg, sigma_s, ui (bitrate 가 있을 때 σ_t·bitrate), ipn_dbc = 10·log10(σ_φ²))."""
    out = dict(deg=math.degrees(phi_rad), sigma_s=phi_rad / (S.TWO_PI * f0),
               ipn_dbc=10.0 * math.log10(phi_rad ** 2))
    if bitrate:
        out["ui"] = out["sigma_s"] * bitrate
    return out


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn 은 () → bool/(bool, 설명) 함수이거나 이미 계산된 bool/(bool, 설명) 값."""
    global g_pass, g_total
    g_total += 1
    detail = ""
    try:
        r = fn() if callable(fn) else fn
        ok, detail = (r if isinstance(r, tuple) else (r, ""))
        ok = bool(ok)
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def main():
    y2, y3 = S.load_json("osc_Y2.json"), S.load_json("osc_Y3.json")
    f0 = 100e6

    print("-- 연습 1: scale_table --")
    t25 = scale_table(y2["table"], 0.25)
    a = S.rms_jitter(y2["table"], f0, 12e3, 20e6)
    b = S.rms_jitter(t25, 25e6, 12e3, 20e6)
    check("Y2 /4 -> 25MHz: L shifts -12.04dB, sigma_t unchanged",
          (abs(t25[0][1] - (y2["table"][0][1] - 20 * math.log10(4))) < 1e-12 and abs(b[1] / a[1] - 1) < 1e-12,
           "100M %.4fps / 25M %.4fps, sigma_phi ratio %.4f" % (a[1] * 1e12, b[1] * 1e12, a[0] / b[0])))
    c = S.rms_jitter(scale_table(y2["table"], 0.25, floor_db=-155.0), 25e6, 12e3, 20e6)
    check("with divider floor -155dBc/Hz: 25MHz sigma_t grows > 10% (floor no longer drops 12dB)",
          (c[1] > b[1] * 1.10, "%.4fps -> %.4fps (%+.1f%%)" % (b[1] * 1e12, c[1] * 1e12, (c[1] / b[1] - 1) * 100)))

    print("-- 연습 2: decade_budget --")
    for o in (y2, y3):
        print("      %s: " % o["ref"] + "  ".join("%s-%s %.1f%%" % (_hz(fa), _hz(fb), p)
                                                  for fa, fb, _, p in decade_budget(o, 12e3, 20e6)))
    b2 = decade_budget(y2, 12e3, 20e6)
    b3 = decade_budget(y3, 12e3, 20e6)
    check("budget sums back to the total (variances add)",
          abs(sum(r[2] for r in b3) - (2 * S.band_integral(y3["table"], 12e3, 20e6)
                                       + S.spur_phase_var(y3["spurs"], 12e3, 20e6))) < 1e-20)
    check("Y2 is close-in dominated: 12k-100k > 70% of jitter^2", (b2[0][3] > 70, "%.1f%%" % b2[0][3]))
    check("Y3 is floor/spur dominated: 1M-20M > 60% of jitter^2",
          (b3[2][3] + b3[3][3] > 60, "%.1f%%" % (b3[2][3] + b3[3][3])))

    print("-- 연습 3: period_jitter --")
    flat = [(1e3, -150.0), (1e8, -150.0)]
    pj = period_jitter(flat, f0, 12e3, 5e6)
    approx = math.sqrt(2 * 1e-15 * (S.TWO_PI / f0) ** 2 * (5e6 ** 3 - 12e3 ** 3) / 3) / (S.TWO_PI * f0)
    check("flat floor, f << f0: sigma_per ~ small-angle closed form within 1%",
          (abs(pj / approx - 1) < 0.01, "%.4f fs vs %.4f fs" % (pj * 1e15, approx * 1e15)))
    pj2, ph2 = period_jitter(y2["table"], f0, 12e3, 20e6), S.rms_jitter(y2["table"], f0, 12e3, 20e6)[1]
    pj3, ph3 = period_jitter(y3["table"], f0, 12e3, 20e6), S.rms_jitter(y3["table"], f0, 12e3, 20e6)[1]
    check("period jitter << phase jitter, and Y3 > Y2 (period jitter weights far-out noise)",
          (pj2 < ph2 / 10 and pj3 > pj2,
           "Y2 per %.1ffs vs phase %.0ffs | Y3 per %.1ffs vs phase %.0ffs" % (pj2 * 1e15, ph2 * 1e15, pj3 * 1e15, ph3 * 1e15)))

    print("-- 연습 4: units --")
    phi = S.rms_jitter(flat, f0, 12e3, 20e6)[0]
    u = units(phi, f0, bitrate=16e9)
    check("flat -150, 12k-20M: 0.01146 deg, -73.98 dBc, 0.00509 UI @16GT/s",
          (abs(u["deg"] - 0.011456) < 1e-5 and abs(u["ipn_dbc"] + 73.98) < 0.01 and abs(u["ui"] - 0.005091) < 1e-5,
           "deg=%.5f ipn=%.2fdBc ui=%.5f" % (u["deg"], u["ipn_dbc"], u["ui"])))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


def _hz(f):
    return ("%gk" % (f / 1e3)) if f < 1e6 else ("%gM" % (f / 1e6))


if __name__ == "__main__":
    sys.exit(main())
