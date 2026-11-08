#!/usr/bin/env python3
"""HWLAB-M01 추가 연습 — 본문 라이브러리(solution.py)를 응용한다

  연습 1  PVT 코너에 강한 직렬 R: 드라이버 10–25 Ω 에서 OS ≤ 10 % 와 첫 입사 ≥ 0.9·VDD 를 동시에
  연습 2  TDR 해상도: 3x mm 35 Ω 패드를 1 Ω 이내로 읽으려면 몇 mm 이상이어야 하나 (erf 모델 + 시뮬레이션)
  연습 3  golden TDR 의 비아 dip 면적 → 등가 shunt C (C_eq = −2∫(v − vinc)dt / (Z0·vinc))
  연습 4  bounce 정착 시간: |ρS·ρL|^n ≤ ε 인 왕복 수 n (해석식) vs bounce_diagram

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)

E24 = [1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2,
       6.8, 7.5, 8.2, 9.1]


# ---------------------------------------------------------------- 연습 1
def series_r_window(z0, rdrv_fast, rdrv_slow, os_max, first_min):
    """열린 끝 저항 모델: 첫 입사 = 2·Z0/(Rsum+Z0)·VDD.
    빠른 코너(작은 Rdrv)에서 OS = 2Z0/(Rsum+Z0) − 1 ≤ os_max  →  R ≥ 2Z0/(1+os_max) − Z0 − rdrv_fast
    느린 코너(큰 Rdrv)에서 첫 입사 ≥ first_min·VDD          →  R ≤ 2Z0/first_min − Z0 − rdrv_slow"""
    lo = 2 * z0 / (1 + os_max) - z0 - rdrv_fast
    hi = 2 * z0 / first_min - z0 - rdrv_slow
    return lo, hi


def e24_in(lo, hi):
    vals = [m * 10 ** d for d in range(0, 3) for m in E24]
    return [x for x in vals if lo - 1e-9 <= x <= hi + 1e-9]


def first_incidence(net, r, t_probe=2.6e-9):
    """3·td 전(첫 반사가 돌아오기 전) 수신단 전압. 램프 0.1–0.6 ns + td 1.0 ns + Cin 충전 후."""
    z, dt = S.net_line(net)
    n = int(4e-9 / dt)
    _, vl = S.simulate(z, dt, S.ramp_fn(net["vdd"], 0.1e-9, net["tr_drv"]), net["rdrv"] + r,
                       S.Load(cin=net["cin"]), n)
    return vl[int(round(t_probe / dt))]


# ---------------------------------------------------------------- 연습 2
def erfinv(y):
    lo, hi = 0.0, 6.0
    for _ in range(200):
        m = 0.5 * (lo + hi)
        if math.erf(m) < y:
            lo = m
        else:
            hi = m
    return 0.5 * (lo + hi)


def min_resolvable_length(z_feat, z0, tr, eps_eff, z_err):
    """겉보기 ρ = ρ·erf(T_rt/(2√2·σ)), σ = tr/2.563.  |Z_app − z_feat| ≤ z_err 가 되는 최소 길이 (m)."""
    r = S.rho(z_feat, z0)
    z_lim = z_feat + z_err if z_feat < z0 else z_feat - z_err
    frac = S.rho(z_lim, z0) / r
    sigma = tr / 2.5631031311
    t_rt = erfinv(frac) * 2 * math.sqrt(2) * sigma
    return S.v_prop(eps_eff) * t_rt / 2


def pad_apparent_z(length_m, z_feat, tr, eps_eff=4.0, cell=0.5e-3):
    dt = cell / S.v_prop(eps_eff)
    n = int(round(length_m / cell))
    z = [50.0] * 40 + [z_feat] * n + [50.0] * 60
    t, v = S.tdr_capture(z, dt, 0.25, S.Load(shunts=[(50.0, 0.0)]), 2 * len(z) + 10)
    zz = S.z_profile(S.gauss_filter(v, tr, dt), 0.25)
    return min(zi for ti, zi in zip(t, zz) if ti > 40 * dt) if z_feat < 50 else \
        max(zi for ti, zi in zip(t, zz) if ti > 40 * dt)


# ---------------------------------------------------------------- 연습 3
def via_ceq(t, v, vinc, x_lo, x_hi, eps_eff=4.0, z0=50.0):
    """[x_lo, x_hi] 구간 반사 면적 → 등가 shunt C.  ∫ v_refl dt = vinc·(L_ex/Z0 − Z0·C_ex)/2 이므로
    C_eq ≡ C_ex − L_ex/Z0² = −2·∫(v − vinc)dt / (Z0·vinc)."""
    vp = S.v_prop(eps_eff)
    t_lo, t_hi = 2 * x_lo / vp, 2 * x_hi / vp
    area = sum((v[k] - vinc) * (t[k + 1] - t[k]) for k in range(len(t) - 1) if t_lo <= t[k] < t_hi)
    return -2.0 * area / (z0 * vinc)


def ceq_truth(z_via, z0, length, eps_eff=4.0):
    """짧은 선로 구간의 정확한 저주파 등가: Γ(s) ≈ s·Td·(Zv − Z0²/Zv)/(2Z0) → C_eq = Td·(1/Zv − Zv/Z0²)."""
    td = length / S.v_prop(eps_eff)
    return td * (1.0 / z_via - z_via / z0 ** 2)


# ---------------------------------------------------------------- 연습 4
def settle_round_trips(rho_s, rho_l, eps):
    """열린 끝(ρL=1)·저항 소스: 부하 오차 = Vfinal·|ρS·ρL|^(n) (n = 부하 도착 횟수).  |ρSρL|^n ≤ ε 인 최소 n."""
    return math.ceil(math.log(eps) / math.log(abs(rho_s * rho_l)))


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    detail = ""
    try:
        r = fn()
        ok, detail = (r if isinstance(r, tuple) else (r, ""))
        ok = bool(ok)
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def main():
    net = S.NET
    print("-- 연습 1: PVT-robust series R (Rdrv 10/15/25 ohm) --")
    lo, hi = series_r_window(50.0, 10.0, 25.0, 0.10, 0.90)
    cands = e24_in(lo, hi)
    pick = min(cands, key=lambda x: abs(x - 0.5 * (lo + hi)))
    check("window R in [30.9, 36.1] ohm, E24 candidates 33/36, center pick 33",
          lambda: (abs(lo - 30.91) < 0.01 and abs(hi - 36.11) < 0.01 and cands == [33.0, 36.0] and pick == 33.0,
                   "window=[%.2f, %.2f] E24=%s pick=%.0f" % (lo, hi, cands, pick)))

    def corners():
        out = []
        ok = True
        for rd in (10.0, 15.0, 25.0):
            os_ = S.series_sweep(dict(net, rdrv=rd), [pick])[0]["os"]
            fi = first_incidence(dict(net, rdrv=rd), pick) / net["vdd"]
            out.append("Rdrv=%.0f: OS=%.2f%% first=%.3f" % (rd, os_, fi))
            ok = ok and os_ <= 10.0 and fi >= 0.90
        return ok, " | ".join(out)
    check("sim with Cin=4pF: R=33 meets OS<=10% and first incidence>=0.9*VDD at all corners", corners)

    print("-- 연습 2: TDR resolution (35-ohm pad, error <= 1 ohm) --")
    lmin35 = min_resolvable_length(35.0, 50.0, 35e-12, 4.0, 1.0)
    lmin100 = min_resolvable_length(35.0, 50.0, 100e-12, 4.0, 1.0)
    check("erf model: tr=35ps -> L_min ~3.6mm, tr=100ps -> ~10mm (scales with tr)",
          lambda: (3.4e-3 < lmin35 < 3.8e-3 and abs(lmin100 / lmin35 - 100 / 35) < 0.01,
                   "L_min(35ps)=%.2fmm L_min(100ps)=%.2fmm" % (lmin35 * 1e3, lmin100 * 1e3)))

    def sim_res():
        z_short = pad_apparent_z(3.0e-3, 35.0, 35e-12)
        z_long = pad_apparent_z(4.0e-3, 35.0, 35e-12)
        return z_short > 36.0 and z_long <= 36.0, "3.0mm -> %.2f ohm, 4.0mm -> %.2f ohm" % (z_short, z_long)
    check("sim: 3.0mm pad reads > 36 ohm, 4.0mm pad reads <= 36 ohm (tr=35ps)", sim_res)

    print("-- 연습 3: via equivalent C from the golden TDR dip area --")
    t, v = S.load_csv(os.path.join(HERE, "SN0041_fpga_clk_tdr.csv"))
    vinc = S.estimate_vinc(t, v)
    c_meas = via_ceq(t, v, vinc, 0.055, 0.066)
    c_true = ceq_truth(32.0, 50.0, 1e-3)
    check("C_eq(via) from dip area within 5% of Td*(1/Zv - Zv/Z0^2) = 0.123 pF",
          lambda: (abs(c_meas / c_true - 1) < 0.05, "meas=%.4fpF truth=%.4fpF (%+.1f%%)"
                   % (c_meas * 1e12, c_true * 1e12, (c_meas / c_true - 1) * 100)))

    print("-- 연습 4: bounce settling (Rdrv=15, open end, +-5%) --")
    n = settle_round_trips(S.rho(15, 50), 1.0, 0.05)
    ev = [e for e in S.bounce_diagram(3.3, 15.0, 50.0, math.inf, 1e-9, 21e-9) if e["end"] == "load"]

    def settle():
        errs = [abs(e["v"] - 3.3) / 3.3 for e in ev]
        k_first_ok = next(i for i in range(len(errs)) if all(x <= 0.05 for x in errs[i:]))
        t_formula = (2 * n - 1) * 1e-9
        return (n == 5 and abs(ev[k_first_ok]["t"] - t_formula) < 1e-15,
                "n=%d arrivals -> settled at %.0f ns (formula (2n-1)td = %.0f ns), errs=%s"
                % (n, ev[k_first_ok]["t"] * 1e9, t_formula * 1e9, ["%.3f" % x for x in errs[:6]]))
    check("|rhoS|^n <= 5%: n=5 load arrivals -> settled at 9 ns = (2n-1)*td", settle)

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
