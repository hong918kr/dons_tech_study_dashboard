#!/usr/bin/env python3
"""HWLAB-M08 데모 — §5 풀이의 표들을 실제로 계산해 찍는다 (solution.py 의 함수를 그대로 사용)

  1) 적분 방법별 RMS 지터 (닫힌식 vs 순진한 3종)
  2) 적분 구간에 따른 RMS 지터 (Y2 / Y3)
  3) Y3 의 PCIe 형 필터 16 조합별 지터 (ζ 포함)
  4) 최악 조합의 |H(f)|² — 어느 오프셋 대역을 통과시키는가

  python3 -I demo_bands.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True
import solution as S                # noqa: E402


def ps(var, f0):
    return math.sqrt(var) / (S.TWO_PI * f0) * 1e12


def main():
    spec = S.load_json("refclk_spec.json")
    oscs = [S.load_json("osc_Y2.json"), S.load_json("osc_Y3.json")]
    f0 = 100e6

    print("1) integration method, 12k-20M random part only [ps rms]  (error vs closed form)")
    print("   %-28s %9s %17s %17s %17s" % ("table", "closed", "trapz", "db_linf", "rect"))
    rows = [("-20dB/dec S=1e-2/f^2 (12k-10M)", S.SLOPE20, 12e3, 10e6)]
    rows += [("%s %s" % (o["ref"], o["desc"][:20]), o["table"], 12e3, 20e6) for o in oscs]
    for name, t, a, b in rows:
        ref = S.band_integral(t, a, b)
        cells = []
        for m in ("trapz", "db_linf", "rect"):
            v = S.integrate_naive(t, a, b, m)
            cells.append("%7.3f (%+6.0f%%)" % (ps(2 * v, f0), (math.sqrt(v / ref) - 1) * 100))
        print("   %-28s %9.3f %s" % (name, ps(2 * ref, f0), " ".join(cells)))

    print()
    print("2) integration band vs RMS jitter [ps]  (random + spurs in band, no filter)")
    bands = [(12e3, 20e6), (1e3, 20e6), (100e3, 20e6), (12e3, 5e6), (12e3, 50e6), (1e6, 50e6)]
    print("   %-6s" % "osc" + "".join("%13s" % ("%s-%gM" % (("%gk" % (a / 1e3)) if a < 1e6 else ("%gM" % (a / 1e6)), b / 1e6)) for a, b in bands) + "   PCIe filt")
    for o in oscs:
        cells = []
        for a, b in bands:
            var = 2 * S.band_integral(o["table"], a, b) + S.spur_phase_var(o["spurs"], a, b)
            cells.append("%13.3f" % ps(var, f0))
        print("   %-6s" % o["ref"] + "".join(cells) + "%12.3f" % (S.filtered_jitter(o, spec)["worst_s"] * 1e12))

    print()
    print("3) Y3 per PCIe-filter combo [ps]  (z = zeta from peaking)")
    fj = S.filtered_jitter(oscs[1], spec)
    print("   %-8s %-8s %-7s %-8s %-8s %-7s %8s" % ("PLL1 BW", "pk1 dB", "z1", "PLL2 BW", "pk2 dB", "z2", "sigma"))
    for c, s in sorted(fj["per_combo"], key=lambda p: -p[1]):
        print("   %-8s %-8g %-7.3f %-8s %-8g %-7.3f %8.3f" % ("%gMHz" % (c["bw1"] / 1e6), c["pk1"], c["z1"],
                                                       "%gMHz" % (c["bw2"] / 1e6), c["pk2"], c["z2"], s * 1e12))

    print()
    c = fj["worst_combo"]
    print("4) |H(f)|^2 of Y3 worst combo (PLL1 %gMHz/%gdB, PLL2 %gMHz/%gdB, T=%gns, CDR %gMHz)"
          % (c["bw1"] / 1e6, c["pk1"], c["bw2"] / 1e6, c["pk2"], c["T"] * 1e9, c["fcdr"] / 1e6))
    fs = [1e4, 1e5, 1e6, 2e6, 5e6, 1e7, 2e7, 5e7]
    print("   f     " + "".join("%10s" % ("%gk" % (f / 1e3) if f < 1e6 else "%gM" % (f / 1e6)) for f in fs))
    print("   |H|^2 " + "".join("%10.2e" % S.pcie_mag2(f, c) for f in fs))
    print("   dB    " + "".join("%10.1f" % (10 * math.log10(S.pcie_mag2(f, c))) for f in fs))


if __name__ == "__main__":
    main()
