#!/usr/bin/env python3
"""HWLAB-B09 합성 캡처 생성기 — 전류 제한 벤치 전원으로 12 V 보드를 켤 때의 V/I 파형

모델 (레슨 §5 Part B 참고, 단순화된 1차 모델):
  벤치 전원  : CV/CC. 출력 기준전압 V_ref 가 0 → V_set 으로 t_rise 동안 선형 상승(출력 ON 슬루).
               I_s = clamp((V_ref - Vc) / R_s, 0, I_lim)   (R_s = 출력 임피던스 + 케이블, 전원은 sink 안 함)
  보드 입력  : C_in (벌크 + 세라믹 합) 하나, 블리더 R_bleed (LED·분압기·LDO Iq 등 저항성 부하)
  DC/DC 부하 : 정전력(constant power) 부하. Vc > UVLO_rise 에서 켜지고 Vc < UVLO_fall 에서 꺼진다.
               켜지면 soft-start 로 전력이 0 → P 까지 t_ss 동안 선형 증가, I_load = P(t) / Vc.
               (꺼졌다 다시 켜지면 soft-start 처음부터 → CC 에 걸리면 hiccup)
  부팅 활동  : 15–25 ms 동안 P 가 +12 % (SoC 부팅·DDR training 흉내)
  측정       : fs = 50 kS/s, 전류 프로브 노이즈 σ=4 mA, 전압 노이즈 σ=4 mV (random.Random 고정 시드)
               golden 캡처에는 12.34 ms 에 1-샘플 글리치(+4.6 A, 프로브 픽업 흉내)를 심는다.

  python3 -I gen_captures.py      # cap_golden_G1_3A0.csv, cap_dut_0A50.csv, cap_dut_0A90.csv 를 다시 쓴다
"""
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))

FS = 50_000             # 저장 샘플링률 [S/s]
DT_SIM = 1e-6           # 시뮬레이션 스텝 [s]  (R_s*C_in = 11 us 보다 작게)
T_PRE = 1e-3            # 프리트리거 [s]  (t<0 : 출력 OFF)
T_END = 40e-3           # 캡처 끝 [s]


def simulate(i_lim, c_in=220e-6, p_load=5.0, v_set=12.0, t_rise=1e-3, r_s=0.05, r_bleed=2000.0,
             uvlo_rise=4.5, uvlo_fall=4.0, t_ss=2e-3, seed=1, glitch=None):
    """(t_us, v, i) 리스트를 FS 로 돌려준다. glitch = (t_s, amp_A) 이면 그 샘플에 더한다."""
    rng = random.Random(seed)
    vc = 0.0
    on = False
    t_on = 0.0
    step_out = int(round(1.0 / (FS * DT_SIM)))      # 20 sim step 마다 1 샘플
    n_sim = int(round((T_PRE + T_END) / DT_SIM))
    rows = []
    g_idx = None
    if glitch is not None:
        g_idx = int(round((glitch[0] + T_PRE) * FS))
    for k in range(n_sim + 1):
        t = k * DT_SIM - T_PRE
        if t < 0:
            v_ref = 0.0
        elif t < t_rise:
            v_ref = v_set * t / t_rise
        else:
            v_ref = v_set
        i_s = (v_ref - vc) / r_s
        i_s = max(0.0, min(i_s, i_lim))
        # DC/DC UVLO 히스테리시스
        if not on and vc > uvlo_rise:
            on, t_on = True, t
        elif on and vc < uvlo_fall:
            on = False
        p = 0.0
        if on:
            p = p_load * min(1.0, (t - t_on) / t_ss)
            if 15e-3 <= t < 25e-3:
                p *= 1.12
        i_load = (p / vc if on else 0.0) + vc / r_bleed
        vc += (i_s - i_load) / c_in * DT_SIM
        if vc < 0.0:
            vc = 0.0
        if k % step_out == 0:
            j = len(rows)
            i_meas = i_s + rng.gauss(0.0, 0.004)
            v_meas = vc + rng.gauss(0.0, 0.004)
            if g_idx is not None and j == g_idx:
                i_meas += glitch[1]
            rows.append((int(round(t * 1e6)), v_meas, i_meas))
    return rows


def write_capture(name, rows, meta):
    path = os.path.join(HERE, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for k in sorted(meta):
            f.write("# %s=%s\n" % (k, meta[k]))
        f.write("t_us,v,i\n")
        for t_us, v, i in rows:
            f.write("%d,%.3f,%.4f\n" % (t_us, v, i))
    return path


CAPTURES = [
    # 파일 이름, 시뮬레이션 인자, 메타데이터 (SCPI 로 읽어 둔 전원 설정을 헤더에 남긴다는 가정)
    ("cap_golden_G1_3A0.csv",
     dict(i_lim=3.0, c_in=220e-6, p_load=5.0, seed=11, glitch=(12.34e-3, 4.6)),
     {"board": "ORCA-EVB_revB", "sn": "G1", "vset_V": "12.000", "ilim_A": "3.000", "fs_Hz": str(FS),
      "probe": "current_probe_DC_coupled"}),
    ("cap_dut_0A50.csv",
     dict(i_lim=0.50, c_in=215e-6, p_load=5.1, seed=22),
     {"board": "ORCA-EVB_revB", "sn": "0007", "vset_V": "12.000", "ilim_A": "0.500", "fs_Hz": str(FS),
      "probe": "current_probe_DC_coupled"}),
    ("cap_dut_0A90.csv",
     dict(i_lim=0.90, c_in=215e-6, p_load=5.1, seed=33),
     {"board": "ORCA-EVB_revB", "sn": "0007", "vset_V": "12.000", "ilim_A": "0.900", "fs_Hz": str(FS),
      "probe": "current_probe_DC_coupled"}),
]


def main():
    for name, kw, meta in CAPTURES:
        rows = simulate(**kw)
        p = write_capture(name, rows, meta)
        print("wrote %s (%d samples, %d bytes)" % (name, len(rows), os.path.getsize(p)))


if __name__ == "__main__":
    main()
