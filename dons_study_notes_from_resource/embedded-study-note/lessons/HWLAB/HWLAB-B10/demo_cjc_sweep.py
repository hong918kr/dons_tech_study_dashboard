#!/usr/bin/env python3
"""HWLAB-B10 데모 — CJC 방법별 오차 표 + soak 로그 시간축 요약 (solution.py 를 import 해서 쓴다)

  python3 -I demo_cjc_sweep.py
"""
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import solution as s  # noqa: E402


def main():
    print("Tcj = 25 C : reading - truth [C]")
    print("%8s %9s %9s %9s %9s %8s %8s" % ("Thot", "V[mV]", "none", "add_T", "linear", "nist", "gain"))
    for th in (-200, -100, -40, 0, 50, 85, 125, 300, 600, 771, 1000, 1300):
        v = s.emf_mV(float(th)) - s.emf_mV(25.0)
        errs = {}
        for k, fn in (("none", lambda: s.naive_no_cjc(v)), ("add_temp", lambda: s.naive_add_temp(v, 25.0)),
                      ("linear", lambda: s.linear_k(v, 25.0)), ("nist", lambda: s.tc_temperature(v, 25.0))):
            try:
                errs[k] = "%+9.3f" % (fn() - th)
            except ValueError:
                errs[k] = "%9s" % "range!"
        print("%8d %9.3f %s %s %s %s %8.3f" % (th, v, errs["none"], errs["add_temp"], errs["linear"],
                                               errs["nist"], s.cj_error_gain(float(th), 25.0)))
    print()
    pkg = s.load_pkg()
    log = s.load_log()
    print("enclosure, CJC on: t[s]  P[W]   Tc[C]  Tj_psi  TSENS  Tlocal")
    for r in log["enclosure"]:
        if r["t_s"] in (0, 58, 62, 120, 240, 480, 720, 960, 1200):
            tc = s.tc_temperature(r["tc_uV"] / 1000.0, r["cj_C"])
            p = s.package_power_W(r)
            print("                 %5d  %4.2f  %6.1f  %6.1f  %5.1f  %6.1f" % (
                r["t_s"], p, tc, tc + p * pkg["psi_jt_C_per_W"], r["tsens_C"], r["tlocal_C"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
