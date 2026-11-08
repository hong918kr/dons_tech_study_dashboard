#!/usr/bin/env python3
"""HWLAB-M10 추가 연습 — 본문 분해 라이브러리(solution.py)를 확장한다

  연습 1  amp_noise_jitter     : 진폭 잡음 σv 가 보간 교차에서 타이밍 지터가 되는 양 √(2/3)·σv/slew (몬테카를로)
  연습 2  golden_pll_residual  : 1차 golden PLL(BW = baud/1667) 기준 TIE — PJ 가 |H| = f/√(f²+fc²) 만큼 남는다
  연습 3  q_ber                : Q(BER) 표와 전이 밀도 ρT 약속, dual-Dirac 로 다른 BER 의 TJ 외삽
  연습 4  dcd_split            : 수신 임계 오프셋 → DCD(상승/하강 평균 차) 를 DDJ 에서 분리

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import gen_capture as G             # noqa: E402
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)

check, near = S.check, S.near


# ---------------------------------------------------------------- 연습 1
def amp_noise_jitter(sigma_v, amp=0.4, tau_ps=55.0, dt_ps=6.25, trials=20000, seed=11):
    """정착된 RC 상승 에지 v(t) = amp - 2·amp·e^(-t/τ) 의 0 V 교차를, 임의 샘플 위상 + 샘플마다 가우스 잡음으로
    선형 보간해 잰 시각 오차의 표준편차 (ps). 잡음 없는 보간 오차(곡률)는 빼고 본다."""
    rng = random.Random(seed)
    t_true = tau_ps * math.log(2.0)

    def v(t):
        return -amp if t < 0 else amp - 2 * amp * math.exp(-t / tau_ps)

    errs = []
    for _ in range(trials):
        ph = rng.random() * dt_ps
        i0 = int(math.floor((t_true - ph) / dt_ps))          # t_true 바로 앞 샘플
        ta, tb = ph + i0 * dt_ps, ph + (i0 + 1) * dt_ps
        va0, vb0 = v(ta), v(tb)
        va, vb = va0 + rng.gauss(0.0, sigma_v), vb0 + rng.gauss(0.0, sigma_v)
        clean = ta + (0.0 - va0) / (vb0 - va0) * dt_ps
        noisy = ta + (0.0 - va) / (vb - va) * dt_ps
        errs.append(noisy - clean)
    m = sum(errs) / len(errs)
    return math.sqrt(sum((e - m) ** 2 for e in errs) / (len(errs) - 1))


# ---------------------------------------------------------------- 연습 2
def golden_pll_residual(x, fc_over_fbaud):
    """1차 PLL: φ[n+1] = φ[n] + α·(x[n] - φ[n]), α = 1 - e^(-2π·fc/fbaud). 반환 e = x - φ (golden PLL 기준 TIE).
    전달함수 E/X = (1 - z⁻¹)/(1 - (1-α)z⁻¹) ≈ s/(s + ωc) — 저주파 지터는 클럭이 따라가서 지워진다."""
    a = 1.0 - math.exp(-2 * math.pi * fc_over_fbaud)
    ph, out = 0.0, []
    for v in x:
        out.append(v - ph)
        ph += a * (v - ph)
    return out


# ---------------------------------------------------------------- 연습 3
def q_ber(ber, rho_t=1.0):
    """Q(BER) = Φ⁻¹(1 - BER/ρT) = √2·erfc⁻¹(2·BER/ρT). ρT = 1: '에지당' 확률(7.034 @1e-12 의 약속).
    BER 을 '비트당' 으로 두고 전이 밀도 ρT(PRBS ≈ 0.5)를 넣으면 에지당 확률이 BER/ρT 로 커져 Q 가 작아진다."""
    return -S.inv_phi(ber / rho_t)


# ---------------------------------------------------------------- 연습 4
def dcd_split(dec, plen=G.PLEN):
    """위치별 DDJ 를 에지 방향으로 나눠: DCD = mean(DDJ 상승) - mean(DDJ 하강), ISI pk-pk = 방향별 평균을 뺀 뒤의 pk-pk."""
    dir_at = {}
    for (t, d), k in zip(dec["edges"], dec["n"]):
        dir_at[k % plen] = d
    rise = [dec["ddj"][p] for p, d in dir_at.items() if d > 0]
    fall = [dec["ddj"][p] for p, d in dir_at.items() if d < 0]
    mr, mf = sum(rise) / len(rise), sum(fall) / len(fall)
    isi = [x - mr for x in rise] + [x - mf for x in fall]
    return dict(dcd=mr - mf, isi_pp=max(isi) - min(isi), n_rise=len(rise), n_fall=len(fall))


def true_dcd(cfg, thr):
    bits = G.prbs7()
    ddj = G.true_ddj(cfg, thr)
    rise = [ddj[p] for p in range(G.PLEN) if ddj[p] is not None and bits[p] == 1]
    fall = [ddj[p] for p in range(G.PLEN) if ddj[p] is not None and bits[p] == 0]
    return sum(rise) / len(rise) - sum(fall) / len(fall)


def run():
    print("-- 연습 1: amplitude noise -> timing jitter (linear-interp crossing) --")
    slew = 0.4 / 55.0                                   # V/ps, 0 V 교차에서 amp/τ (샘플 간 45 mV)
    for sv in (0.001, 0.002):
        meas = amp_noise_jitter(sv)
        pred = math.sqrt(2.0 / 3.0) * sv / slew
        check("sigma_v=%.0fmV: MC jitter within 3%% of sqrt(2/3)*sigma_v/slew" % (sv * 1e3),
              lambda: (abs(meas / pred - 1) < 0.03, "MC=%.4fps pred=%.4fps (slew=%.2fmV/ps)" % (meas, pred, slew * 1e3)))
    meas10, pred10 = amp_noise_jitter(0.010), math.sqrt(2.0 / 3.0) * 0.010 / slew
    check("sigma_v=10mV: linear formula breaks (noise ~ 1/3 of the 45mV sample step) -> MC > 1.5x formula",
          lambda: (meas10 > 1.5 * pred10, "MC=%.3fps pred=%.3fps" % (meas10, pred10)))
    cfg = G.PART_A
    cap = G.make_capture(cfg)
    base = S.decompose(cap)
    rj_obs = math.sqrt(cfg["rj_ps"] ** 2 + (math.sqrt(2.0 / 3.0) * cfg["noise_mv"] * 1e-3 / slew) ** 2)
    check("Part A capture (1mV noise): measured RJ matches sqrt(RJ^2 + noise-jitter^2) within 0.03ps",
          lambda: (near(base["rj"], rj_obs, 0.03), "measured=%.4fps predicted=%.4fps" % (base["rj"], rj_obs)))

    print("-- 연습 2: golden PLL (1st order, BW = baud/1667) jitter transfer --")
    fb = 10e9
    fc = fb / 1667
    for f_pj in (2e6, 25e6, 50.97e6):
        x = [6.0 * math.sin(2 * math.pi * f_pj / fb * n) for n in range(65536)]
        e = golden_pll_residual(x, fc / fb)
        n_keep = list(range(4000, 65536))
        a, b = S.ls_sine(n_keep, [e[k] for k in n_keep], f_pj / fb)
        ratio = math.hypot(a, b) / 6.0
        h = f_pj / math.hypot(f_pj, fc)
        check("PJ %.2fMHz: residual/injected = %.3f matches |H|=f/sqrt(f^2+fc^2) within 0.005" % (f_pj / 1e6, ratio),
              lambda: (abs(ratio - h) < 0.005, "|H|=%.4f fc=%.3fMHz" % (h, fc / 1e6)))

    print("-- 연습 3: Q(BER), transition density, TJ at other BERs --")
    table = {1e-6: 4.753, 1e-9: 5.998, 1e-12: 7.034, 1e-15: 7.941}
    check("Q table 1e-6/1e-9/1e-12/1e-15 = 4.753/5.998/7.034/7.941 (+-0.001)",
          lambda: (all(near(q_ber(b), q, 1e-3) for b, q in table.items()),
                   " ".join("%.0e:%.4f" % (b, q_ber(b)) for b in table)))
    check("per-bit BER with rho_T=0.5: Q(1e-12) = 6.937 (14.07 -> 13.87 multiplier)",
          lambda: (near(q_ber(1e-12, 0.5), 6.937, 1e-3), "Q=%.4f" % q_ber(1e-12, 0.5)))
    tv = [x for x in G.true_ddj(cfg) if x is not None]
    _, tie = G.tie_record(cfg, G.PLEN * 4000, seed=4242)
    fit = S.tail_fit(tie, *S.FIT_LONG)
    for ber in (1e-9, 1e-15):
        dd = S.dual_dirac(fit, ber)
        tj = G.tj_convolution(tv, cfg["pj_ps"], cfg["rj_ps"], ber)
        check("dual-Dirac fit (long record) extrapolated to BER=%.0e within 3%% of truth" % ber,
              lambda: (abs(dd["tj"] / tj - 1) < 0.03, "TJdd=%.2fps truth=%.2fps (%+.1f%%)" % (dd["tj"], tj, (dd["tj"] / tj - 1) * 100)))

    print("-- 연습 4: DCD from a receiver threshold offset --")
    thr = 0.040

    def dec_thr():
        edges = S.find_edges(cap["dt_ps"], cap["v"], thr)
        n, tie, T = S.tie_from_edges(edges, cap["ui_ps"])
        dd = S.ddj_by_pattern(n, tie)
        return dict(edges=edges, n=n, ddj=dd["ddj"], ddj_pp=dd["pp"])
    d40 = dec_thr()
    s0, s40 = dcd_split(base), dcd_split(d40)
    want = true_dcd(cfg, thr)
    settled = cfg["tau_ui"] * cfg["ui_ps"] * math.log((0.4 + thr) / (0.4 - thr))
    check("thr=0: DCD ~ 0 (symmetric RC + symmetric levels), |DCD| < 0.2ps",
          lambda: (abs(s0["dcd"]) < 0.2, "DCD=%+.3fps" % s0["dcd"]))
    check("thr=+40mV: DCD within 0.2ps of noise-free truth",
          lambda: (near(s40["dcd"], want, 0.2), "DCD=%.3fps truth=%.3fps (settled-edge formula %.2fps)"
                   % (s40["dcd"], want, settled)))
    check("thr=+40mV: DDJ pk-pk grows by roughly the DCD, ISI part ~unchanged (+-0.6ps)",
          lambda: (d40["ddj_pp"] > base["ddj_pp"] + 0.5 * want and near(s40["isi_pp"], s0["isi_pp"], 0.6),
                   "DDJpp %.2f -> %.2fps, ISI pp %.2f -> %.2fps" % (base["ddj_pp"], d40["ddj_pp"], s0["isi_pp"], s40["isi_pp"])))

    print("%d/%d passed" % (S.g_pass, S.g_total))
    return S.g_pass == S.g_total


if __name__ == "__main__":
    sys.exit(0 if run() else 1)
