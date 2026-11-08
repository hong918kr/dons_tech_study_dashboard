#!/usr/bin/env python3
"""HWLAB-M11 데모 — md 의 표들을 만드는 실제 출력

  1  DD 모델 1e7 비트 bathtub 왼쪽 꼬리: 측정 BER · 해석 BER · Q(ρT/2) · 피팅 직선 Q
  2  측정 깊이(비트 수)별 외삽 EO(1e-12) 오차와 표준오차
  3  시드 8개로 반복 → EO 의 실제 흩어짐 vs 피팅 표준오차(se)
  4  '긴 직접 측정' 대용: 4e7 비트 한 레코드를 1e-7 깊이까지 직접 세어 해석식과 비교
  5  lane3 (bert_sweep.csv): 피팅 기대 오류 vs 실제 오류 vs 정답 — 외톨이 오류의 모양
  6  측정 시간·신뢰도 trade-off 표

  python3 -I demo_bathtub.py      # ~15 s
"""
import math
import os
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import solution as S                # noqa: E402

P = 256
FS = dict(ber_max=1e-3, min_err=5)


def fit_eo(pts, ber=1e-12):
    fl = S.qscale_fit(pts, "L", S.RHO_DD, **FS)
    fr = S.qscale_fit(pts, "R", S.RHO_DD, **FS)
    return fl, fr, S.extrapolate(fl, fr, ber, S.RHO_DD)


def main():
    t0 = time.time()
    truth = S.eye_analytic(S.DD, 1e-12)[2]

    print("== 1. DD (DJdd 0.20, RJ 0.020 UI) 1e7 bits, left tail, seed 2026 ==")
    pts = S.measure_bathtub(S.DD, 10_000_000, P, 2026)
    fl, fr, ex = fit_eo(pts)
    print("%7s %8s %10s %10s %7s %7s %s" % ("x[UI]", "errors", "BER meas", "BER true", "Q meas", "Q fit", "used"))
    for x, n, e in pts:
        if 0.11 <= x <= 0.20 and round(x * P) % 2 == 0:
            q = S.q_of_ber(e / n, S.RHO_DD) if e else float("nan")
            print("%7.4f %8d %10.2e %10.2e %7.3f %7.3f %s" % (x, e, e / n, S.ber_analytic(S.DD, x), q,
                                                              fl["m"] * x + fl["c"], "*" if x in fl["xs"] else ""))
    print("fit L: sigma=%.4f mu=%.4f | R: sigma=%.4f mu=%.4f | EO(1e-12)=%.4f +-%.4f truth=%.4f"
          % (fl["sigma"], fl["mu"], fr["sigma"], fr["mu"], ex["eo"], ex["se"], truth))

    print("\n== 2. measurement depth vs extrapolation (DD, seed 7) ==")
    print("%10s %10s %8s %8s %8s %8s" % ("bits/pt", "floor", "pts L/R", "EO", "err", "se"))
    for nb in (100_000, 1_000_000, 10_000_000):
        p2 = pts if nb == 10_000_000 else S.measure_bathtub(S.DD, nb, P, 7)
        a, b, e2 = fit_eo(p2)
        if e2 is None:
            print("%10.0e %10.1e %8s  (fewer than 3 tail points)" % (nb, 5 / nb, "-"))
        else:
            print("%10.0e %10.1e %4d/%-3d %8.4f %+8.4f %8.4f" % (nb, 5 / nb, a["n"], b["n"], e2["eo"], e2["eo"] - truth, e2["se"]))

    print("\n== 3. repeatability: 8 seeds x 1e6 bits (DD) ==")
    eos, ses = [], []
    for sd in range(100, 108):
        _, _, e3 = fit_eo(S.measure_bathtub(S.DD, 1_000_000, P, sd))
        eos.append(e3["eo"])
        ses.append(e3["se"])
    print("EO per seed: " + " ".join("%.4f" % v for v in eos))
    print("mean=%.4f (truth %.4f) std across seeds=%.4f  mean fit se=%.4f"
          % (statistics.mean(eos), truth, statistics.stdev(eos), statistics.mean(ses)))

    print("\n== 4. direct long count, 4e7 bits one record (DD, seed 99) vs analytic ==")
    nbits = 40_000_000
    errs = S.bathtub_counts(S.edge_jitters(S.DD, int(round(S.RHO_T * nbits)), 99), P)
    print("%7s %8s %10s %10s %9s" % ("x[UI]", "errors", "BER meas", "BER true", "expected"))
    for k in range(44, 54):
        x = k / P
        bt = S.ber_analytic(S.DD, x)
        print("%7.4f %8d %10.2e %10.2e %9.2f" % (x, errs[k - 1], errs[k - 1] / nbits, bt, bt * nbits))

    print("\n== 5. lane L3 from bert_sweep.csv: fit-expected vs observed vs truth ==")
    spec = S.load_spec()
    data = S.load_sweep(os.path.join(HERE, "bert_sweep.csv"))["L3"]
    model = spec["lanes"]["L3"]["injected"]
    fl3, fr3, ex3 = fit_eo(data["sweep"])
    print("%7s %8s %12s %12s" % ("x[UI]", "errors", "fit expect", "true expect"))
    for x, n, e in data["sweep"]:
        if 0.17 <= x <= 0.34 and round(x * P) % 4 == 0:
            q = fl3["m"] * x + fl3["c"]
            print("%7.4f %8d %12.2e %12.2e" % (x, e, n * S.RHO_DD * S.tail(q), n * S.ber_analytic(model, x)))
    for x, n, e in data["long"]:
        print("long x=%.2f: %d err / %.1e bits, truth expects %.2f" % (x, e, n, n * S.ber_analytic(model, x)))
    tr3 = S.eye_analytic(model, 1e-12)
    print("L3 EO(1e-12): fit %.4f +-%.4f, truth %.4f (xL=%.4f xR=%.4f)" % (ex3["eo"], ex3["se"], tr3[2], tr3[0], tr3[1]))

    print("\n== 6. time / confidence trade-off ==")
    print(S.tradeoff_table())
    print("\n(runtime %.1f s)" % (time.time() - t0))


if __name__ == "__main__":
    main()
