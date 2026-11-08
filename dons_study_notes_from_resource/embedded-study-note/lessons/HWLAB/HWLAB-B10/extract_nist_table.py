#!/usr/bin/env python3
"""HWLAB-B10 — NIST ITS-90 type K 기준표(type_k.tab) → nist_type_k_table.csv

NIST SRD 60 (ITS-90 Thermocouple Database) 의 type_k.tab 은 1 °C 간격 기준표(mV, 소수 3자리)와
계수 표를 담은 텍스트 파일이다. 이 스크립트는 기준표 부분만 뽑아 t_C,E_mV CSV 로 쓴다.
(계수는 solution.py 에 손으로 옮겼고, 이 CSV 의 1643 점 전체로 검증한다.)

  curl -sSL -o /some/new/dir/type_k.tab \
    "https://web.archive.org/web/2019id_/https://srdata.nist.gov/its90/download/type_k.tab"
  python3 -I extract_nist_table.py /some/new/dir/type_k.tab      # → nist_type_k_table.csv

표 형식: 줄 머리의 t0 와 11개 열. 0 °C 아래 표는 열이 0,-1,...,-10 (t = t0 - k),
위 표는 0,1,...,10 (t = t0 + k). 경계값(예: -10 행의 -10 열 = -20 행의 0 열)은 중복이므로 한 번만 쓴다.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROW = re.compile(r"^\s*(-?\d+)((?:\s+-?\d+\.\d{3})+)\s*$")


def extract(text):
    pts = {}
    sign = None
    for line in text.splitlines():
        if "0     -1     -2" in line:
            sign = -1
        elif "0      1      2" in line:
            sign = +1
        m = ROW.match(line)
        if not m or sign is None:
            continue
        t0 = int(m.group(1))
        vals = m.group(2).split()
        for k, v in enumerate(vals):
            t = t0 + sign * k
            e = float(v)
            if t in pts and abs(pts[t] - e) > 1e-9:
                raise ValueError("inconsistent duplicate at %d C: %r vs %r" % (t, pts[t], e))
            pts[t] = e
    return pts


def main(argv):
    if len(argv) != 2:
        print("usage: %s type_k.tab" % argv[0])
        return 2
    with open(argv[1], encoding="latin-1") as f:
        pts = extract(f.read())
    out = os.path.join(HERE, "nist_type_k_table.csv")
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("# NIST SRD 60 ITS-90 type K reference table (type_k.tab), t_C integer, E_mV rounded to 0.001\n")
        f.write("t_C,E_mV\n")
        for t in sorted(pts):
            f.write("%d,%.3f\n" % (t, pts[t]))
    print("wrote %s: %d points, %d..%d C" % (out, len(pts), min(pts), max(pts)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
