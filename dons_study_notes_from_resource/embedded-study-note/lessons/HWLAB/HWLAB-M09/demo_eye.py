#!/usr/bin/env python3
"""HWLAB-M09 데모: 2D 히스토그램 eye 를 ASCII 로 그린다 (마스크 폴리곤 겹쳐 그리기).

  빈 마스크 칸 'o', 히트가 있는 마스크 칸 'X', 밀도 ' .:-=+*#%@' (√ 스케일). 가로 1 UI(가운데가 eye 중심, 양 끝이 crossing),
  세로 −0.5 … +0.5 V.  python3 -I demo_eye.py [TX0|TX1|TX2] [pll|ideal|nominal]   (인자 없으면 TX0 ideal / TX0 pll / TX1 pll)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S  # noqa: E402


def show(lane, method, cap=None):
    cap = cap or S.lane_capture(lane)
    line, verdict, items, m, hits = S.lane_report(lane, cap, method)
    h = S.eye_hist(m["xs"], m["vs"], nx=60, ny=18, vlo=-0.5, vhi=0.5)
    print(line)
    print(S.render(h, S.SPEC["mask_ui_v"], vlo=-0.5, vhi=0.5))
    return cap


if __name__ == "__main__":
    if len(sys.argv) == 3:
        show(sys.argv[1], sys.argv[2])
    else:
        c0 = show("TX0", "ideal")
        show("TX0", "pll", c0)
        show("TX1", "pll")
