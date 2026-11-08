#!/usr/bin/env python3
"""HWLAB-B10 추가 연습 — 열전대 교정기 · 정밀 역변환 · 유효 열저항/전력 한도 · 히트싱크 선택

  연습 1  sim_output_mV(t_hot, t_term)      : 열전대 교정기(simulator)가 내야 할 전압, 고정 mV 모드의 함정
  연습 2  temp_C_exact(e_mV)                : 기준 함수를 Newton+이분법으로 풀어 역함수 오차(±0.06 °C)를 없앤다
  연습 3  theta_eff / p_max_W               : 로그에서 '이 보드의' 유효 열저항과 Tj_max 까지의 허용 전력
  연습 4  theta_sa_required                 : 히트싱크를 붙일 때 필요한 θSA (여기서는 θJC 가 맞는 도구)

  python3 -I practice.py
"""
import csv
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 본문(solution.py)의 Part A 를 그대로 넣었다 (레슨 디렉터리끼리 import 하지 않는다) ----
K_NEG = (0.000000000000E+00, 0.394501280250E-01, 0.236223735980E-04, -0.328589067840E-06,
         -0.499048287770E-08, -0.675090591730E-10, -0.574103274280E-12, -0.310888728940E-14,
         -0.104516093650E-16, -0.198892668780E-19, -0.163226974860E-22)
K_POS = (-0.176004136860E-01, 0.389212049750E-01, 0.185587700320E-04, -0.994575928740E-07,
         0.318409457190E-09, -0.560728448890E-12, 0.560750590590E-15, -0.320207200030E-18,
         0.971511471520E-22, -0.121047212750E-25)
K_A0, K_A1, K_A2 = 0.118597600000E+00, -0.118343200000E-03, 0.126968600000E+03
K_INV_NEG = (0.0000000E+00, 2.5173462E+01, -1.1662878E+00, -1.0833638E+00, -8.9773540E-01,
             -3.7342377E-01, -8.6632643E-02, -1.0450598E-02, -5.1920577E-04)
K_INV_MID = (0.000000E+00, 2.508355E+01, 7.860106E-02, -2.503131E-01, 8.315270E-02,
             -1.228034E-02, 9.804036E-04, -4.413030E-05, 1.057734E-06, -1.052755E-08)
K_INV_HIGH = (-1.318058E+02, 4.830222E+01, -1.646031E+00, 5.464731E-02, -9.650715E-04,
              8.802193E-06, -3.110810E-08)


def horner(coefs, x):
    s = 0.0
    for c in reversed(coefs):
        s = s * x + c
    return s


def emf_mV(t):
    if not (-270.0 <= t <= 1372.0):
        raise ValueError(t)
    if t < 0.0:
        return horner(K_NEG, t)
    return horner(K_POS, t) + K_A0 * math.exp(K_A1 * (t - K_A2) ** 2)


def temp_C(e):
    if not (emf_mV(-200.0) <= e <= emf_mV(1372.0)):
        raise ValueError(e)
    if e < 0.0:
        return horner(K_INV_NEG, e)
    return horner(K_INV_MID if e < 20.644 else K_INV_HIGH, e)


def seebeck_mV_per_C(t):
    coefs = K_NEG if t < 0.0 else K_POS
    d = 0.0
    for i in range(len(coefs) - 1, 0, -1):
        d = d * t + i * coefs[i]
    if t >= 0.0:
        d += K_A0 * math.exp(K_A1 * (t - K_A2) ** 2) * 2.0 * K_A1 * (t - K_A2)
    return d


def tc_temperature(v, t_cj):
    return temp_C(v + emf_mV(t_cj))


# ---------------------------------------------------------------- 연습 1
def sim_output_mV(t_hot_C, t_terminal_C):
    """교정기가 '측정 접점이 t_hot 인 열전대' 를 흉내 낼 때 출력 단자에 내야 할 전압.
    출력 단자(=DAQ 냉접점)가 t_terminal 이므로 E(t_hot) - E(t_terminal)."""
    return emf_mV(t_hot_C) - emf_mV(t_terminal_C)


# ---------------------------------------------------------------- 연습 2
def temp_C_exact(e_mV, tol_mV=1e-12):
    """E(t) = e_mV 를 -270..1372 °C 에서 푼다. Newton 이 구간을 벗어나면 이분법으로 대체."""
    lo, hi = -270.0, 1372.0
    if not (emf_mV(lo) <= e_mV <= emf_mV(hi)):
        raise ValueError(e_mV)
    t = temp_C(e_mV) if e_mV >= emf_mV(-200.0) else -235.0   # 좋은 초기값
    t = min(max(t, lo), hi)                                    # 근사 역함수가 1372.05 를 줄 수도 있다
    for _ in range(100):
        f = emf_mV(t) - e_mV
        if abs(f) <= tol_mV:
            return t
        if f > 0:
            hi = t
        else:
            lo = t
        s = seebeck_mV_per_C(t)
        tn = t - f / s if s > 0 else lo - 1.0
        t = tn if lo < tn < hi else 0.5 * (lo + hi)
    return t


# ---------------------------------------------------------------- 연습 3
def load_log():
    out = {}
    with open(os.path.join(HERE, "soak_log.csv"), encoding="ascii", newline="") as f:
        for r in csv.DictReader(f):
            row = {k: (v if k == "scenario" else float(v)) for k, v in r.items()}
            out.setdefault(row["scenario"], []).append(row)
    return out


def load_pkg():
    with open(os.path.join(HERE, "soc0_thermal.json"), encoding="utf-8") as f:
        return json.load(f)


def steady_means(rows, window_s=120.0):
    t_end = rows[-1]["t_s"]
    win = [r for r in rows if r["t_s"] >= t_end - window_s]
    n = float(len(win))
    tc = sum(tc_temperature(r["tc_uV"] / 1000.0, r["cj_C"]) for r in win) / n
    p = sum(r["vdd_core_V"] * r["idd_core_A"] + r["vdd_io_V"] * r["idd_io_A"] for r in win) / n
    tlocal = sum(r["tlocal_C"] for r in win) / n
    return tc, p, tlocal


def theta_eff(tj_C, tlocal_C, p_W):
    """이 보드·이 환경의 유효 접합→국소공기 열저항 [°C/W] (데이터시트 θJA 가 아니다)"""
    return (tj_C - tlocal_C) / p_W


def p_max_W(tj_max_C, tlocal_C, theta):
    """국소 공기 온도가 그대로라고 가정할 때 Tj_max 에 닿는 전력"""
    return (tj_max_C - tlocal_C) / theta


# ---------------------------------------------------------------- 연습 4
def theta_sa_required(tj_target_C, ta_C, p_W, theta_jc, theta_cs):
    """히트싱크 경로 j→c→s→a 직렬: θSA ≤ (Tj_target - Ta)/P - θJC - θCS.
    (열 대부분이 케이스 윗면→히트싱크로 흐를 때만 θJC 를 직렬 저항으로 쓸 수 있다.)"""
    return (tj_target_C - ta_C) / p_W - theta_jc - theta_cs


# ---------------------------------------------------------------- 테스트
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def near(x, ref, tol, label=""):
    if x is None or abs(x - ref) > tol:
        print("      %s measured=%r expected=%r tol=%r" % (label, x, ref, tol))
        return False
    return True


def main():
    print("-- 연습 1: thermocouple simulator --")
    check("simulate 100 C into DAQ terminals at 28 C -> 2.974 mV",
          lambda: near(sim_output_mV(100.0, 28.0), 2.974, 0.0005, "V"))
    check("DAQ with correct CJC reads 100 C from that voltage",
          lambda: near(tc_temperature(sim_output_mV(100.0, 28.0), 28.0), 100.0, 0.06, "T"))
    check("fixed-mV mode 4.096 mV (table value) -> DAQ reads ~127.3 C (CJC added twice)",
          lambda: near(tc_temperature(4.096, 28.0), 127.29, 0.02, "T"))

    print("-- 연습 2: exact inverse --")

    def exact_rt():
        worst = max(abs(temp_C_exact(emf_mV(k / 2.0)) - k / 2.0) for k in range(-540, 2745))
        print("      max |T_exact(E(t)) - t| over -270..1372 C = %.2e C" % worst)
        return worst < 1e-6
    check("Newton inverse round-trips to < 1e-6 C over -270..1372 C (incl. below -200)", exact_rt)
    check("temp_C_exact(4.096) = 99.994 C: the residual is table rounding (E(100)=4.0962), not inverse error",
          lambda: near(temp_C_exact(4.096), 99.9944, 0.0005, "T"))

    print("-- 연습 3: effective theta and power limit --")
    pkg = load_pkg()
    log = load_log()
    res = {}
    for sc in ("bench_fan", "enclosure"):
        tc, p, tl = steady_means(log[sc])
        tj = tc + p * pkg["psi_jt_C_per_W"]
        th = theta_eff(tj, tl, p)
        res[sc] = (tj, tl, p, th, p_max_W(pkg["tj_max_C"], tl, th))
        print("      %-9s Tj=%.1fC Tlocal=%.1fC P=%.2fW theta_eff=%.2fC/W P_max=%.2fW" % ((sc,) + res[sc]))
    check("bench_fan theta_eff ~ 10.9 C/W (datasheet thetaJA 20)", lambda: near(res["bench_fan"][3], 10.85, 0.2, "th"))
    check("enclosure theta_eff ~ 15.9 C/W", lambda: near(res["enclosure"][3], 15.95, 0.2, "th"))
    check("enclosure P_max ~ 3.2 W -> throttle ~10 % needed (see SIV-A04)",
          lambda: near(res["enclosure"][4], 3.22, 0.05, "Pmax") and res["enclosure"][4] < res["enclosure"][2])

    print("-- 연습 4: heatsink theta_SA --")
    check("theta_SA <= 3.95 C/W for Tj 95 C, Ta 54 C, 3.58 W, thetaJC 7, thetaCS 0.5",
          lambda: near(theta_sa_required(95.0, 54.0, 3.58, pkg["theta_jc_top_C_per_W"], 0.5), 3.953, 0.001, "th"))
    check("same with datasheet psiJT used as thetaJC would wrongly allow 10.45 C/W",
          lambda: near(theta_sa_required(95.0, 54.0, 3.58, pkg["psi_jt_C_per_W"], 0.5), 10.453, 0.001, "th"))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
