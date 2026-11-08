#!/usr/bin/env python3
"""HWLAB-M11 추가 연습 — 본문 라이브러리(solution.py)를 확장한다

  연습 1  selfsync_errors   : 자기동기(self-synchronizing) PRBS7 checker 의 오류 증식 ×3 (locked 는 ×1)
  연습 2  fit_rho           : 정규화 ρ 를 자유 변수로 — 넓은 창에서만 식별되고, PJ 에선 ρT/2 가 아닌 '유효 ρ'
  연습 3  poisson_bounds    : k 개 오류 허용 시 필요한 비트 수, 관측 k 개의 BER 신뢰구간 (lane3 FAIL 의 통계적 확실성)
  연습 4  rj_budget         : EO 스펙과 DJδδ 가 주어졌을 때 허용 RJ 상한 (UI / ps)

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1  (~8 s)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def selfsync_errors(rx):
    """예측 = rx[t-7] XOR rx[t-6] (받은 비트로만). t = 7..n-1 에서 rx[t] 와 다르면 오류."""
    return sum(1 for t in range(7, len(rx)) if rx[t] != rx[t - 7] ^ rx[t - 6])


# ---------------------------------------------------------------- 연습 2
def weighted_chi2(points, side, rho, bmax, min_err=5):
    """qscale_fit 직선에 대한 가중 잔차 제곱합과 점 수."""
    f = S.qscale_fit(points, side, rho, bmax, min_err)
    if f is None:
        return None, 0, None
    r = 0.0
    for x, n, e in points:
        if (x < 0.5) != (side == "L") or e < min_err or e / n > bmax or e / n / rho >= 0.5:
            continue
        q = S.q_of_ber(e / n, rho)
        w = e * (S.pdf(q) / S.tail(q)) ** 2
        r += w * (q - (f["m"] * x + f["c"])) ** 2
    return r, f["n"], f


def fit_rho(points, side, bmax, lo=0.05, hi=1.0, steps=41):
    """log 격자에서 χ² 최소인 ρ. 반환 (rho, chi2, fit)."""
    best = None
    for i in range(steps):
        rho = lo * (hi / lo) ** (i / (steps - 1))
        c, n, f = weighted_chi2(points, side, rho, bmax)
        if c is not None and (best is None or c < best[1]):
            best = (rho, c, f)
    return best


# ---------------------------------------------------------------- 연습 3
def poisson_cdf(k, lam):
    """P(X <= k), X ~ Poisson(lam)."""
    term = math.exp(-lam)
    s = term
    for i in range(1, k + 1):
        term *= lam / i
        s += term
    return s


def bits_for_cl_k(ber, cl, k):
    """오류 k 개 이하를 허용하고도 'BER < ber' 를 신뢰수준 cl 로 주장하려면: P(X<=k | λ) = 1−cl 인 λ / ber."""
    a, b = 0.0, 100.0 + 10 * k
    for _ in range(200):
        m = 0.5 * (a + b)
        if poisson_cdf(k, m) > 1.0 - cl:
            a = m
        else:
            b = m
    return 0.5 * (a + b) / ber


def poisson_bounds(k, n, cl=0.95):
    """관측 k 오류 / n 비트 → 양측 cl 구간 (BER_lo, BER_hi). 정확(Garwood) 구간을 이분법으로.
    lo: P(X >= k | λ) = (1−cl)/2,  hi: P(X <= k | λ) = (1−cl)/2."""
    alpha = (1.0 - cl) / 2.0

    def solve(f, target):
        a, b = 0.0, 200.0 + 10 * k
        for _ in range(200):
            m = 0.5 * (a + b)
            if f(m) < target:
                a = m
            else:
                b = m
        return 0.5 * (a + b)
    lo = 0.0 if k == 0 else solve(lambda lam: 1.0 - poisson_cdf(k - 1, lam), alpha)   # 증가 함수
    hi = solve(lambda lam: -poisson_cdf(k, lam), -alpha)                               # P(X<=k) 감소 → 부호 반전
    return lo / n, hi / n


# ---------------------------------------------------------------- 연습 4
def rj_budget(eo_min, djdd, ber, rho=S.RHO_DD):
    """EO = 1 − DJδδ − 2·Q·RJ ≥ eo_min  →  RJ_max = (1 − eo_min − DJδδ) / (2·Q)."""
    return (1.0 - eo_min - djdd) / (2.0 * S.q_of_ber(ber, rho))


# ---------------------------------------------------------------- 하네스
def main():
    S.g_pass = S.g_total = 0
    check = S.check

    print("-- 연습 1: self-sync checker error multiplication --")
    rx = S.prbs7(2000)
    for t in (100, 400, 900, 1500):            # 서로 7 비트 이상 떨어진 단일 오류 4 개
        rx[t] ^= 1
    a = S.prbs7_align(rx)
    check("locked checker counts 4, self-sync counts 12 (= 3 x 4, 2-tap polynomial)",
          lambda: (S.prbs7_errors(rx, a) == 4 and selfsync_errors(rx) == 12,
                   "locked=%d selfsync=%d" % (S.prbs7_errors(rx, a), selfsync_errors(rx))))
    rx2 = S.prbs7(2000)
    rx2[500] ^= 1
    rx2[506] ^= 1                              # 6 비트 간격 두 오류: t=506 예측에서 서로 상쇄
    check("errors 6 bits apart: self-sync counts 4 (not 6) -> multiplication factor is pattern dependent",
          lambda: (selfsync_errors(rx2) == 4, "selfsync=%d" % selfsync_errors(rx2)))

    print("-- 연습 2: rho as a free parameter --")
    dd = S.measure_bathtub(S.DD, 10_000_000, 256, 5)
    pj = S.measure_bathtub(S.PJ, 10_000_000, 256, 5)
    r_wide = fit_rho(dd, "L", 0.05)
    c_dd, _, _ = weighted_chi2(dd, "L", S.RHO_DD, 1e-3)
    c_one, _, _ = weighted_chi2(dd, "L", 1.0, 1e-3)
    print("      DD window 5e-2: best rho=%.4f (rho_T/2=%.4f) | window 1e-3: chi2(rho_T/2)=%.2f chi2(1)=%.2f"
          % (r_wide[0], S.RHO_DD, c_dd, c_one))
    check("DD, wide window (BER<=5e-2): fitted rho within 10% of rho_T/2",
          lambda: (abs(r_wide[0] / S.RHO_DD - 1) < 0.10, "rho=%.4f" % r_wide[0]))
    check("DD, tail window only (BER<=1e-3): chi2 barely changes from rho_T/2 to 1 -> rho not identifiable",
          lambda: (0.5 < c_one / c_dd < 2.0, "chi2 %.2f vs %.2f" % (c_dd, c_one)))
    tpj = S.eye_analytic(S.PJ, 1e-12)[2]
    rl, rr = fit_rho(pj, "L", 0.05), fit_rho(pj, "R", 0.05)
    ex_fit = S.extrapolate(rl[2], rr[2], 1e-12, 0.5 * (rl[0] + rr[0]))   # BER = ρ_eff·Q̄(Q) → Q_t = Q̄⁻¹(1e-12/ρ_eff)
    ex_fix = S.extrapolate(S.qscale_fit(pj, "L", S.RHO_DD, 0.05, 5), S.qscale_fit(pj, "R", S.RHO_DD, 0.05, 5),
                           1e-12, S.RHO_DD)
    print("      PJ window 5e-2: rho_L=%.4f rho_R=%.4f | EO fitted-rho=%.4f fixed-rho=%.4f truth=%.4f"
          % (rl[0], rr[0], ex_fit["eo"], ex_fix["eo"], tpj))
    check("PJ, wide window: effective rho != rho_T/2 and fitting it cuts EO error by > 3x vs fixed rho",
          lambda: (abs(rl[0] / S.RHO_DD - 1) > 0.2 and abs(ex_fit["eo"] - tpj) * 3 < abs(ex_fix["eo"] - tpj),
                   "err fitted=%+.4f fixed=%+.4f" % (ex_fit["eo"] - tpj, ex_fix["eo"] - tpj)))

    print("-- 연습 3: Poisson confidence --")
    lams = [bits_for_cl_k(1.0, 0.95, k) for k in (0, 1, 2)]
    check("95% CL multipliers for 0/1/2 allowed errors = 2.996 / 4.744 / 6.296",
          lambda: (all(abs(a - b) < 2e-3 for a, b in zip(lams, (2.996, 4.744, 6.296))),
                   "%.3f / %.3f / %.3f" % tuple(lams)))
    lo, hi = poisson_bounds(6, 50_000_000)
    check("lane3 mask: 6 err / 5e7 bits -> 95% interval excludes 1e-12 by > 4 decades",
          lambda: (lo > 1e-8 and lo < 6 / 5e7 < hi, "BER in [%.2e, %.2e]" % (lo, hi)))
    lo0, hi0 = poisson_bounds(0, 50_000_000)
    check("lane0 mask: 0 err / 5e7 bits -> upper bound ~ 7.4e-8 (two-sided 95%), far from proving 1e-12",
          lambda: (lo0 == 0.0 and abs(hi0 - 3.689 / 5e7) < 1e-10, "hi=%.3e" % hi0))

    print("-- 연습 4: RJ budget --")
    rmax = rj_budget(0.40, 0.18, 1e-12)
    check("EO>=0.40 UI @1e-12 with DJdd 0.18 -> RJ_max = 0.0307 UI (3.07 ps @10G, 1.23 ps @25G)",
          lambda: (abs(rmax - 0.42 / (2 * 6.8397)) < 1e-4, "RJ_max=%.4f UI = %.2f ps @10G, %.2f ps @25G"
                   % (rmax, rmax * 100, rmax * 40)))

    print("%d/%d passed" % (S.g_pass, S.g_total))
    return 0 if S.g_pass == S.g_total else 1


if __name__ == "__main__":
    sys.exit(main())
