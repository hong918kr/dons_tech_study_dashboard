#!/usr/bin/env python3
"""SIV-M08 픽스처 생성기 — 동료가 스프레드시트로 짠 PVT 계획(handmade_plan.csv)을 결정적으로 만든다.

동료의 규칙: "중요한 13개 셀(P1 5개 + VNOM 8개) x serdes_ber 를 뺀 6개 스위트, 여유 있으니 P1 셀엔 serdes 도"
루프 순서: for chip: for temp: for volt: for suite  (사람이 보기 편한 '칩별' 시트)
심어 둔 결함 (solution.py 의 audit_plan 이 잡아야 함):
  1. FF-11/VMAX/-40 의 cpu_stress 행 누락             → E_MUST_MISSING
  2. SS-07/VNOM/25 의 ddr_stress 를 'ddr_strss' 로 오타  → E_UNKNOWN
  3. TT-03/VNOM/25 smoke 를 맨 끝에 한 번 더 (보드 재장착 후 재확인) → W_DUP
  4. 칩이 바깥 루프 → 온도를 칩마다 다시 방문         → E_TEMP_REVISIT x3, W_SUBOPTIMAL
  5. claimed 합계 = 스위트 시간 + DUT 교체만 (램프/soak/전압 변경 누락) → E_CLAIM_MISMATCH
  6. 그 결과 실제 시간이 24h 예산 초과                  → E_OVER_BUDGET

  python3 -I gen_handmade_plan.py [--out PATH]   (기본: 이 디렉터리의 handmade_plan.csv)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CHIPS = [("TT-03", "TT"), ("SS-07", "SS"), ("FF-11", "FF")]
TEMPS = [-40, 25, 125]
VOLTS = ["VNOM", "VMIN", "VMAX"]
SUITES = [("smoke", 3), ("mem_march", 12), ("cpu_stress", 20), ("pcie_link", 8),
          ("ddr_stress", 25), ("idle_power", 5), ("serdes_ber", 15)]
P1_CELLS = {("TT", "VNOM", 25), ("SS", "VMIN", -40), ("SS", "VMIN", 125),
            ("FF", "VMAX", -40), ("FF", "VMAX", 125)}
DUT_SWITCH_MIN = 4


def rows():
    out = []
    for chip, corner in CHIPS:
        for t in TEMPS:
            for v in VOLTS:
                p1 = (corner, v, t) in P1_CELLS
                if not p1 and v != "VNOM":
                    continue
                for s, _ in SUITES:
                    if s == "serdes_ber" and not p1:
                        continue
                    if (chip, v, t, s) == ("FF-11", "VMAX", -40, "cpu_stress"):
                        continue                                  # 결함 1
                    if (chip, v, t, s) == ("SS-07", "VNOM", 25, "ddr_stress"):
                        s = "ddr_strss"                           # 결함 2
                    out.append((t, chip, v, s))
    out.append((25, "TT-03", "VNOM", "smoke"))                    # 결함 3
    return out


def main(argv):
    path = os.path.join(HERE, "handmade_plan.csv")
    if len(argv) == 3 and argv[1] == "--out":
        path = argv[2]
    elif len(argv) != 1:
        print("usage: gen_handmade_plan.py [--out PATH]")
        return 2
    rs = rows()
    smin = dict(SUITES)
    claimed = sum(smin.get(s, 25) for (_, _, _, s) in rs) + DUT_SWITCH_MIN * len(CHIPS)   # 결함 5
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# SOC-A0 PVT plan v2 - exported from team spreadsheet (sheet 'by chip')\n")
        f.write("# claimed_total_min=%d  (sum of suite times + DUT swaps)\n" % claimed)
        f.write("step,temp_c,chip,volt,suite\n")
        for i, (t, c, v, s) in enumerate(rs, 1):
            f.write("%d,%d,%s,%s,%s\n" % (i, t, c, v, s))
    print("wrote %s (%d steps, claimed %d min)" % (path, len(rs), claimed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
