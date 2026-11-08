#!/usr/bin/env python3
"""HWLAB-M12 데모 — §5 의 표 두 개를 찍는다 (solution.py 라이브러리 사용).

  1) −15 dB 채널에서 CTLE ADC 0 … −12 dB 스윕: h0/h1, σ_out/h0, PDA eye, @1e-12 eye
  2) DFE 오류 전파: 잡음 σ 를 바꿔 가며 genie(참 비트 feedback) vs 실제 결정 feedback

  python3 -I demo_sweeps.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import solution as S  # noqa: E402

p = S.pulse_from_step(S.channel_step(15.0))
print("ADC[dB]   h0[mV]  h1[mV]  h1/h0   sigma_out  sigma/h0   PDA EH   @1e-12 EH  @1e-12 EW")
for adc in range(0, -13, -1):
    q = S.ctle_apply(p, adc)
    c = S.cursors(q, 0, 1)
    sig = S.noise_out_rms(adc)
    e0 = S.eye_metrics(S.pda_curve(q))[0]
    eb, wb, _ = S.eye_metrics(S.pda_curve(q), 2 * S.Q_1E12 * sig)
    print("%5d    %7.1f %7.1f  %6.3f   %6.2f    %.4f   %7.1f   %7.1f    %.3f"
          % (adc, c[0], c[1], c[1] / c[0], sig, sig / c[0], e0, eb, wb))

print()
print("config       w1/h0  sigma/h0  genie BER   actual BER  ratio  burst(mean/max)")
q7 = S.ctle_apply(p, -7)
for name, q in (("DFE3 only", p), ("CTLE+DFE3", q7)):
    c = S.cursors(q, 2, 24)
    for k in (3.6, 3.2, 2.8):
        sig = c[0] / k
        g = S.dfe_sim(c, 3, sig, 100000, 7, genie=True)
        e = S.dfe_sim(c, 3, sig, 100000, 7, genie=False)
        b = S.bursts(e, 3)
        print("%-11s  %.2f   1/%.1f    %.2e    %.2e    x%.2f   %.2f / %d"
              % (name, c[1] / c[0], k, len(g) / 1e5, len(e) / 1e5, len(e) / max(1, len(g)),
                 sum(b) / max(1, len(b)), max(b) if b else 0))
