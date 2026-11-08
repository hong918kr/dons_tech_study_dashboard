#!/usr/bin/env python3
"""HWLAB-M07 추가 연습 — 본문 지터 라이브러리(solution.py)를 확장한다

  연습 1  rj_from_c2c          : c2c rms / √6 로 RJ 추정 — SSC·저주파 wander 에 둔감 (TIE 는 SSC 에 묻힌다)
  연습 2  jitter_color         : c2c/period 비율로 지터의 '색' 판별 — √3(백색 TIE, PLL 출력) vs √2(백색 주기, 자유 발진)
  연습 3  terr_rms             : DDR 식 누적 오차 tERR(n) = e[k+n] − e[k] − n·T  — 백색 TIE 는 √2·σ 로 평평, 랜덤워크는 σ_p·√n
  연습 4  detrend_blocks_rms   : SSC 가 있는 TIE 를 W 에지 블록마다 2차 다항식으로 걷어내고 블록 rms 의 중앙값으로 RJ 복원

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def rj_from_c2c(edges):
    """백색 TIE 가정에서 c2c = TIE 의 2차 차분 → var = (1+4+1)σ² → σ = c2c_rms/√6.
    저주파 성분(SSC, wander)은 2차 차분에서 4·sin²(πν) ≈ (2πν)² 로 거의 사라진다."""
    return S.stats(S.c2c_direct(edges))[1] / math.sqrt(6.0)


# ---------------------------------------------------------------- 연습 2
def free_running_edges(n, f0, sigma_p, t_first=3.1e-9, seed=1):
    """자유 발진(링 오실레이터 류): 주기마다 독립 잡음 σ_p → 에지 = 주기의 누적합 (TIE 는 랜덤워크)."""
    rng = random.Random(seed)
    T = 1.0 / f0
    out = [t_first]
    for _ in range(n - 1):
        out.append(out[-1] + T + rng.gauss(0.0, sigma_p))
    return out


def jitter_color(edges):
    """c2c_rms / period_rms. 백색 TIE: √6/√2 = √3 ≈ 1.732, 백색 주기(랜덤워크 TIE): √2 ≈ 1.414."""
    m = S.jitter_metrics(edges)
    return m["c2c_rms"] / m["period_rms"]


# ---------------------------------------------------------------- 연습 3
def terr_rms(edges, n):
    """tERR(n)[k] = e[k+n] − e[k] − n·T_fit  (= TIE[k+n] − TIE[k]) 의 rms. 겹치는 창 전부 사용."""
    _, T, _ = S.tie_ls(edges)
    d = [edges[k + n] - edges[k] - n * T for k in range(len(edges) - n)]
    return math.sqrt(sum(x * x for x in d) / len(d))


# ---------------------------------------------------------------- 연습 4
def quad_residual_rms(y):
    """y[k] ≈ c0 + c1·u + c2·u² (u = k − 중앙) 최소제곱 → 잔차 rms × √(W/(W−3)) (자유도 보정)."""
    W = len(y)
    c = (W - 1) / 2.0
    us = [k - c for k in range(W)]
    s = [sum(u ** p for u in us) for p in range(5)]              # Σu^0..Σu^4
    b = [sum(yv * u ** p for yv, u in zip(y, us)) for p in range(3)]
    A = [[s[0], s[1], s[2]], [s[1], s[2], s[3]], [s[2], s[3], s[4]]]
    # 3×3 가우스 소거 (피벗 없이도 이 행렬은 양의 정부호)
    for i in range(3):
        for r in range(i + 1, 3):
            f = A[r][i] / A[i][i]
            for col in range(i, 3):
                A[r][col] -= f * A[i][col]
            b[r] -= f * b[i]
    x = [0.0, 0.0, 0.0]
    for i in (2, 1, 0):
        x[i] = (b[i] - sum(A[i][col] * x[col] for col in range(i + 1, 3))) / A[i][i]
    res = [yv - (x[0] + x[1] * u + x[2] * u * u) for yv, u in zip(y, us)]
    return math.sqrt(sum(r * r for r in res) / W * W / (W - 3))


def detrend_blocks_rms(tie, W=64):
    """W 에지 블록마다 2차 detrend → 블록 rms 의 중앙값 (삼각파 꼭짓점이 든 몇 블록은 이상치 → 중앙값으로 무시)."""
    rs = sorted(quad_residual_rms(tie[i:i + W]) for i in range(0, len(tie) - W + 1, W))
    k = len(rs)
    return rs[k // 2] if k % 2 else 0.5 * (rs[k // 2 - 1] + rs[k // 2])


# ---------------------------------------------------------------- 테스트
def main():
    S.g_pass = S.g_total = 0
    F0 = S.F0
    delta, fm = 0.005, 31.5e3
    n_ssc = int(round(2 * F0 * (1 - delta / 2) / fm)) + 1
    eS = S.capture(S.clock_edges(n_ssc, F0, rj=2e-12, ssc=(delta, fm, 0.25), seed=41))
    tieS = S.tie_ls(eS)[0]

    print("-- 연습 1: RJ from c2c (SSC-blind) --")
    S.check("SSC capture: rj_from_c2c == 2ps (+-5%) while TIE rms is ~7ns",
            lambda: (S.rel(rj_from_c2c(eS), 2e-12, 0.05),
                     "c2c/sqrt6=%s  tie_rms=%.3fns" % (S.ps(rj_from_c2c(eS)), S.stats(tieS)[1] * 1e9)))
    eA = S.capture(S.clock_edges(2048, F0, ppm=25.0, rj=2e-12, seed=11))
    S.check("no SSC: rj_from_c2c agrees with tie_rms (+-5%)",
            lambda: (S.rel(rj_from_c2c(eA), S.jitter_metrics(eA)["tie_rms"], 0.05),
                     "c2c/sqrt6=%s tie_rms=%s" % (S.ps(rj_from_c2c(eA)), S.ps(S.jitter_metrics(eA)["tie_rms"]))))

    print("-- 연습 2: jitter color --")
    eR = S.capture(free_running_edges(4096, F0, 2e-12, seed=51))
    S.check("white TIE (PLL-locked): c2c/period == sqrt3 (+-5%)",
            lambda: (S.rel(jitter_color(eA), math.sqrt(3), 0.05), "%.3f" % jitter_color(eA)))
    S.check("white period (free-running): c2c/period == sqrt2 (+-5%), period_rms == sigma_p 2ps (+-5%)",
            lambda: (S.rel(jitter_color(eR), math.sqrt(2), 0.05) and
                     S.rel(S.jitter_metrics(eR)["period_rms"], 2e-12, 0.05),
                     "%.3f, period_rms=%s" % (jitter_color(eR), S.ps(S.jitter_metrics(eR)["period_rms"]))))

    print("-- 연습 3: tERR(n) --")
    rows = [(n, terr_rms(eA, n), terr_rms(eR, n)) for n in (1, 4, 16, 64)]
    for n, a, r in rows:
        print("      n=%-3d white-TIE tERR rms=%s (sqrt2*2ps=2.828)   random-walk tERR rms=%s (2ps*sqrt n=%s)"
              % (n, S.ps(a), S.ps(r), S.ps(2e-12 * math.sqrt(n))))
    S.check("white TIE: tERR(n) rms flat at sqrt2*sigma for n=1..64 (+-6%)",
            lambda: all(S.rel(a, math.sqrt(2) * 2e-12, 0.06) for _, a, _ in rows))
    S.check("random walk: tERR(n) rms == sigma_p*sqrt(n) for n=1,4,16 (+-10%)",
            lambda: all(S.rel(r, 2e-12 * math.sqrt(n), 0.10) for n, _, r in rows[:3]))

    print("-- 연습 4: block quadratic detrend under SSC --")
    S.check("SSC capture: median block (W=64) quad-detrended rms == RJ 2ps (+-10%)",
            lambda: (S.rel(detrend_blocks_rms(tieS, 64), 2e-12, 0.10), S.ps(detrend_blocks_rms(tieS, 64))))
    S.check("W=1024 blocks are too long: SSC curvature leaks in (> 3x RJ)",
            lambda: (detrend_blocks_rms(tieS, 1024) > 3 * 2e-12, S.ps(detrend_blocks_rms(tieS, 1024))))

    print("%d/%d passed" % (S.g_pass, S.g_total))
    return 0 if S.g_pass == S.g_total else 1


if __name__ == "__main__":
    sys.exit(main())
