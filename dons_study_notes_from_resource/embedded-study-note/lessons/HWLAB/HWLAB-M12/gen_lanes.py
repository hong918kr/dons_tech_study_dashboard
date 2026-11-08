#!/usr/bin/env python3
"""HWLAB-M12 Part C 데이터 생성기 — VALBRD-S 두 레인의 TDT step 응답 CSV (결정적: 다시 돌려도 바이트 단위로 같다).

  L0 (PCIE_RX0) : -20 dB @ 4 GHz 채널 (골든 레인)
  L2 (PCIE_RX2) : 같은 라우팅 + 심어 둔 결함 — BOM 상 DNP 인 0 Ω(R_BYP2)이 실장되어 생긴 열린 스텁.
                  스텁 왕복 지연 30 샘플 = 117.1875 ps (≈ 8.8 mm @ 15 cm/ns), 분기점 Γ = -1/3.
실제로는 VNA 로 측정한 SDD21 (s4p) → IFFT → step 응답(TDT) 이 이 자리에 온다. 여기선 solution.py 의 모델로 만든다.

  python3 -I gen_lanes.py        # valbrd_s_tdt_steps.csv 재생성
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True
import solution as S                # noqa: E402

STUB_TAU_SAMPLES = 30               # 심어 둔 진실 (테스트가 이 값을 되찾아야 한다)


def main():
    s0 = S.channel_step(20.0)
    s2 = S.tee_stub(s0, STUB_TAU_SAMPLES)
    path = os.path.join(HERE, "valbrd_s_tdt_steps.csv")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("# VALBRD-S rev A PCIe Gen3 RX lanes, differential TDT step response (normalized 0->1)\n")
        f.write("# dt = %.5f ps (8 GT/s, 32 samples/UI), 48 UI\n" % (S.DT * 1e12))
        f.write("t_ps,L0,L2\n")
        for i, (a, b) in enumerate(zip(s0, s2)):
            f.write("%.5f,%.8f,%.8f\n" % (i * S.DT * 1e12, a, b))
    print("wrote %s (%d rows)" % (os.path.basename(path), len(s0)))


if __name__ == "__main__":
    main()
