#!/usr/bin/env python3
"""HWLAB-M02 데모 — §5 의 표들을 실제로 만든다 (solution.py 의 함수 사용)

  표 1  ζ → 반주기 감쇠비 · λ · Q · PM(정확) · 100ζ
  표 2  e-load slew 스윕: droop(모델 참값) · 쓸 수 있는 로브 수 · 측정 ζ  (보상 전/후 파라미터)
  표 3  잡음 시드 20개 몬테카를로: ζ · Td · recovery 오차 분포 → 테스트 허용오차의 근거
  표 4  Part C 두 캡처에서 단계별 스위치를 하나씩 끈 결과 (ablation)

  python3 -I demo_sweep.py
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import gen_capture as gen   # noqa: E402
import solution as S        # noqa: E402


def fmt(x, f="%.3f"):
    return "None" if x is None else f % x


def main():
    t_begin = time.time()
    print("표 1  zeta -> 반주기 감쇠 |a_k+1/a_k| , lambda, Q=1/(2zeta), PM exact, 100*zeta")
    print("  zeta  ratio   lambda   Q      PM_exact  100z   diff")
    for z in (0.05, 0.10, 0.15, 0.20, 0.30, 0.40, 0.50, 0.55, 0.60, 0.70, 0.80):
        lam = math.pi * z / math.sqrt(1 - z * z)
        pm, rule = S.phase_margin(z)
        print("  %.2f  %.3f  %6.3f  %5.2f  %7.1f  %5.0f  %+5.1f" % (z, math.exp(-lam), lam, 1 / (2 * z), pm, rule,
                                                                 rule - pm))

    print("\n표 2  e-load slew sweep (0.5->3A), droop = model truth, lobes/zeta = noisy capture seed 7")
    for name in ("U12_before_comp.csv", "U12_after_comp.csv"):
        p0 = gen.CAPTURES[name]
        print("  %s (injected zeta=%.2f fn=%.0fkHz)" % (name[:-4], p0["zeta"], p0["fn"] / 1e3))
        print("    slew[A/us]  ramp[us]  droop[mV]  t_droop[us]  lobes  zeta_meas")
        for sl in (0.1, 0.2, 0.5, 1.0, 3.0, 10.0):
            p = dict(p0, slew=sl * 1e6)
            tr = gen.truth(p, 8e-3)
            t, v, i = S.capture(p, seed=7)
            m = S.analyze(t, v, i, fsw=1e6)
            zs = fmt(m["zeta"]) if m["zeta"] is not None else ">=" + fmt(m["zeta_min"], "%.2f")
            print("    %6.1f     %6.2f    %7.2f     %6.2f      %3d    %s"
                  % (sl, 2.5 / sl, tr["droop"] * 1e3, tr["t_droop"] * 1e6, m["n"], zs))

    print("\n표 3  Monte Carlo, 20 noise seeds (ripple 8mVpp + 0.8mV rms + 0.1mV LSB), robust method")
    for name in ("U12_before_comp.csv", "U12_after_comp.csv"):
        p = gen.CAPTURES[name]
        tr = gen.truth(p, 8e-3)
        zs, tds, recs, ns = [], [], [], []
        for seed in range(100, 120):
            t, v, i = S.capture(p, seed=seed)
            m = S.analyze(t, v, i, fsw=1e6)
            zs.append(m["zeta"])
            tds.append(m["Td"] / tr["Td"] - 1)
            recs.append((m["recovery"] - tr["recovery"]) * 1e6)
            ns.append(m["n"])
        print("  %s truth zeta=%.2f Td=%.2fus rec=%.2fus" % (name[:-4], p["zeta"], tr["Td"] * 1e6,
                                                            tr["recovery"] * 1e6))
        print("    zeta  mean %.4f  sd %.4f  min %.4f  max %.4f" % (S.mean(zs), S.stdev(zs), min(zs), max(zs)))
        print("    Td    err mean %+.2f%%  sd %.2f%%  worst %+.2f%%" % (S.mean(tds) * 100, S.stdev(tds) * 100,
                                                                     max(tds, key=abs) * 100))
        print("    rec   err mean %+.3fus  sd %.3fus  worst %+.3fus" % (S.mean(recs), S.stdev(recs),
                                                                     max(recs, key=abs)))
        print("    lobes used  min %d  max %d" % (min(ns), max(ns)))

    print("\n표 4  ablation on Part C captures (one switch off at a time)")
    spec = S.load_spec()
    variants = [("robust (all on)", {}), ("filt=False", {"filt": False}), ("hyst=False", {"hyst": False}),
                ("free_decay=False", {"free_decay": False}), ("baseline=pre", {"baseline": "pre"}),
                ("naive (all off)", {"filt": False, "hyst": False, "free_decay": False, "baseline": "pre"})]
    for name in ("U12_before_comp.csv", "U12_after_comp.csv"):
        t, v, i = S.load_csv(os.path.join(HERE, name))
        print("  %s" % name[:-4])
        print("    %-18s lobes  used  zeta     PM[deg]  Td[us]   rec[us]  verdict" % "variant")
        for label, kw in variants:
            m = S.analyze(t, v, i, fsw=1e6, band=8e-3, **kw)
            verdict = S.judge(m, spec)[1]
            print("    %-18s %5d  %4d  %-7s  %-7s  %-7s  %-7s  %s"
                  % (label, m["n_lobes"], m["n"], fmt(m["zeta"]), fmt(m["pm"], "%.1f"),
                     fmt(None if m["Td"] is None else m["Td"] * 1e6, "%.2f"),
                     fmt(None if m["recovery"] is None else m["recovery"] * 1e6, "%.1f"), verdict))
    print("\nruntime %.1fs" % (time.time() - t_begin))
    return 0


if __name__ == "__main__":
    sys.exit(main())
