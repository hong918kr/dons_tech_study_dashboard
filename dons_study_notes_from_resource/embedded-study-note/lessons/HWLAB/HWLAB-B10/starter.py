#!/usr/bin/env python3
"""HWLAB-B10 열 측정 기초: 열전대 냉접점 보상과 Tj 추정

  Part A  NIST ITS-90 K형 기준 함수(°C → mV)와 근사 역함수(mV → °C), 전압 영역 냉접점 보상(CJC)
  Part B  CJC 를 빼먹거나 온도 영역에서 더하거나 41.276 µV/°C 선형으로 근사하면 얼마나 틀리나
          + 냉접점 센서 오차가 결과로 몇 배 증폭되나 (Seebeck 계수 비)
  Part C  soak 로그(soak_log.csv) → 케이스 온도·패키지 전력 → Tj = Tc + P·ΨJT vs Tj = Ta + P·θJA,
          온다이 센서와 비교, 판정 리포트 (CJC 누락이라는 심어 둔 결함 포함)

  python3 -I solution.py        # 테스트 ("N/M passed", 실패 시 exit 1)

계수 출처: NIST SRD 60 ITS-90 Thermocouple Database, type_k.tab (NIST Monograph 175, 1993).
Python 3 표준 라이브러리만 쓴다 (csv, json, math, os, sys).
"""
import csv
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- NIST ITS-90 type K 계수
# 기준 함수 E(t) [mV], t [°C]. -270..0 °C: E = Σ c_i t^i (i=0..10)
K_NEG = (0.000000000000E+00, 0.394501280250E-01, 0.236223735980E-04, -0.328589067840E-06,
         -0.499048287770E-08, -0.675090591730E-10, -0.574103274280E-12, -0.310888728940E-14,
         -0.104516093650E-16, -0.198892668780E-19, -0.163226974860E-22)
# 0..1372 °C: E = Σ c_i t^i (i=0..9) + a0·exp(a1·(t - a2)^2)
K_POS = (-0.176004136860E-01, 0.389212049750E-01, 0.185587700320E-04, -0.994575928740E-07,
         0.318409457190E-09, -0.560728448890E-12, 0.560750590590E-15, -0.320207200030E-18,
         0.971511471520E-22, -0.121047212750E-25)
K_A0, K_A1, K_A2 = 0.118597600000E+00, -0.118343200000E-03, 0.126968600000E+03
# 근사 역함수 t = Σ d_i E^i. 구간: -200..0 °C (-5.891..0 mV, 오차 -0.02..0.04 °C)
#                              0..500 °C (0..20.644 mV, 오차 -0.05..0.04 °C)
#                           500..1372 °C (20.644..54.886 mV, 오차 -0.05..0.06 °C)
K_INV_NEG = (0.0000000E+00, 2.5173462E+01, -1.1662878E+00, -1.0833638E+00, -8.9773540E-01,
             -3.7342377E-01, -8.6632643E-02, -1.0450598E-02, -5.1920577E-04)
K_INV_MID = (0.000000E+00, 2.508355E+01, 7.860106E-02, -2.503131E-01, 8.315270E-02,
             -1.228034E-02, 9.804036E-04, -4.413030E-05, 1.057734E-06, -1.052755E-08)
K_INV_HIGH = (-1.318058E+02, 4.830222E+01, -1.646031E+00, 5.464731E-02, -9.650715E-04,
              8.802193E-06, -3.110810E-08)
K_INV_SPLIT_mV = 20.644          # 500 °C 경계 (NIST 표의 전압 구간 경계)
T_MIN, T_MAX = -270.0, 1372.0    # 기준 함수 정의역
T_INV_MIN = -200.0               # 역함수는 -200 °C 부터만 정의된다
K_LINEAR_mV_PER_C = 0.041276     # MAX31855 류의 선형 근사 (= E(1000 °C)/1000)


def horner(coefs, x):
    """Σ coefs[i]·x^i  (coefs[0] 이 상수항)"""
    s = 0.0
    for c in reversed(coefs):
        s = s * x + c
    return s


# ---------------------------------------------------------------- Part A: 변환 + CJC
def emf_mV(t_C):
    """K형 열전대 기준 함수: 기준 접점 0 °C 일 때 측정 접점 t_C 의 열기전력 [mV]"""
    # TODO: 정의역(-270..1372 °C) 밖이면 ValueError.
    #       t < 0 → horner(K_NEG, t),  t >= 0 → horner(K_POS, t) + a0·exp(a1·(t - a2)^2)
    return None


def temp_C(e_mV):
    """K형 근사 역함수: 기준 접점 0 °C 기준 열기전력 e_mV → 온도 [°C] (-200..1372 °C)"""
    # TODO: 유효 전압 범위 emf_mV(-200)..emf_mV(1372) 밖이면 ValueError.
    #       e < 0 → K_INV_NEG, e < 20.644 → K_INV_MID, 그 외 → K_INV_HIGH 로 horner.
    return None


def tc_temperature(v_meas_mV, t_cj_C):
    """측정 전압(단자에서 잰 값) + 냉접점 온도 → 측정 접점 온도. 보상은 전압 영역에서 한다."""
    # TODO: 냉접점 온도를 '전압'으로 바꿔 더한 뒤 역변환한다 (온도끼리 더하지 않는다).
    return None


# ---------------------------------------------------------------- Part B: 틀리는 방법들
def naive_no_cjc(v_meas_mV, t_cj_C=None):
    """(레거시 스크립트) 냉접점이 0 °C 라고 가정 — CJC 생략"""
    return temp_C(v_meas_mV)


def naive_add_temp(v_meas_mV, t_cj_C):
    """온도 영역에서 더하기: T(V) + Tcj — 열전대가 비선형이라 틀린다"""
    return temp_C(v_meas_mV) + t_cj_C


def linear_k(v_meas_mV, t_cj_C, s_mV_per_C=K_LINEAR_mV_PER_C):
    """단일 기울기 근사: V/S + Tcj (선형화 안 하는 변환 IC 의 방식)"""
    return v_meas_mV / s_mV_per_C + t_cj_C


def seebeck_uV_per_C(t_C):
    """dE/dt [µV/°C] — 기준 함수를 해석적으로 미분 (지수항 포함)"""
    # TODO: Σ i·c_i·t^(i-1)  (+ t >= 0 이면 지수항의 미분 a0·exp(a1(t-a2)^2)·2a1(t-a2)), mV→µV
    raise NotImplementedError("seebeck_uV_per_C")


def cj_error_gain(t_hot_C, t_cj_C):
    """냉접점 센서가 1 °C 틀리면 측정 접점 결과가 몇 °C 틀리나 = S(Tcj)/S(Thot)"""
    # TODO: 냉접점에서의 Seebeck 계수 / 측정 접점에서의 Seebeck 계수
    raise NotImplementedError("cj_error_gain")


def method_errors(t_hot_C, t_cj_C):
    """단자 전압을 물리대로 만든 뒤 각 변환 방법의 오차(읽은 값 - 참값) [°C]"""
    # TODO: v = E(t_hot) - E(t_cj) 를 만들고 naive_no_cjc / naive_add_temp / linear_k / tc_temperature
    #       각각의 (읽은 값 - t_hot) 을 dict 로 돌려준다 (키: none, add_temp, linear, nist)
    raise NotImplementedError("method_errors")


# ---------------------------------------------------------------- Part C: soak 로그 → Tj
def load_pkg(name="soc0_thermal.json"):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


def load_log(name="soak_log.csv"):
    """scenario → 행 목록 (숫자 열은 float)"""
    out = {}
    with open(os.path.join(HERE, name), encoding="ascii", newline="") as f:
        for r in csv.DictReader(f):
            row = {k: (v if k == "scenario" else float(v)) for k, v in r.items()}
            out.setdefault(row["scenario"], []).append(row)
    return out


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs)


def package_power_W(row):
    """패키지 전체 소비 전력 = 모든 레일 V·I 합 (ΨJT/θJA 의 P 는 '총' 전력)"""
    # TODO: 로그의 레일 전압·전류 열 (vdd_core_V·idd_core_A + vdd_io_V·idd_io_A)
    return None


def is_steady(ts, ys, max_slope_C_per_s=0.005):
    """최소제곱 기울기 |dy/dt| 가 기준 이하면 정상 상태(steady state)"""
    # TODO: 최소제곱 기울기 = Σ(t-t̄)(y-ȳ) / Σ(t-t̄)^2, |기울기| <= max_slope_C_per_s 이면 True
    return None


def estimate_tj(tc_C, ta_C, p_W, pkg):
    """세 가지 Tj 추정: ΨJT(케이스 윗면 기준), θJA(주변 공기 기준), θJC(케이스 기준, 오용 예)"""
    # TODO: psi_jt = Tc + P·ΨJT, theta_ja = Ta + P·θJA, theta_jc = Tc + P·θJC (pkg 키 이름은 json 참고)
    return None


def analyze(rows, pkg, window_s=120.0, use_cjc=True):
    """마지막 window_s 초(정상 상태 구간)를 평균해 Tj 추정치·플래그·판정을 낸다.
    처음 30 s(부하 전, idle)는 열전대 영점 확인에 쓴다: 전력을 쓰는 칩 윗면이 주변 공기보다 차가울 수 없다."""
    # TODO: 마지막 window_s 초 행만 골라 Tc(use_cjc 면 tc_temperature, 아니면 naive_no_cjc; tc_uV 는 µV!),
    #       P, Ta(ta_C), Tlocal(tlocal_C), TSENS 평균 → estimate_tj (Ta 기준, Tlocal 기준 두 번).
    #       flags: NOT_STEADY / TC_BELOW_AMBIENT(Tc < Tlocal, 또는 처음 30 s idle 의 Tc < Tlocal - 1)
    #              / TSENS_MISMATCH(|Tj_psi - TSENS| > tsens_accuracy_C)
    #       반환 키: scenario use_cjc tc p ta tlocal tsens tj_psi tj_theta_ja tj_theta_ja_local tj_theta_jc
    #                flags verdict(플래그가 있으면 INVALID, 아니면 Tj_psi <= tj_max_C 면 PASS / FAIL)
    return None


def report_line(res, pkg):
    """하드웨어 엔지니어가 바로 읽을 한 줄 (ASCII)"""
    tmax = pkg["tj_max_C"]
    ja = res["tj_theta_ja"]
    ja_v = "PASS" if ja <= tmax else "FAIL"
    return ("%s %s scenario=%s cjc=%s Tj(psiJT)=%.1fC %s Tjmax=%.1fC | Tc=%.1fC P=%.2fW "
            "TSENS=%.1fC | thetaJA(Ta=%.1fC)=%.1fC says %s (off by %+.1fC) | flags=%s" % (
                res["verdict"], pkg["part"], res["scenario"], "on" if res["use_cjc"] else "OFF",
                res["tj_psi"], ">" if res["tj_psi"] > tmax else "<=", tmax, res["tc"], res["p"],
                res["tsens"], res["ta"], ja, ja_v, ja - res["tsens"],
                ",".join(res["flags"]) or "-"))


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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
    """|x - ref| <= tol 이면 True, 아니면 측정값/기대값을 찍고 False"""
    if x is None or abs(x - ref) > tol:
        print("      %s measured=%r expected=%r tol=%r" % (label, x, ref, tol))
        return False
    return True


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def load_nist_table():
    pts = {}
    with open(os.path.join(HERE, "nist_type_k_table.csv"), encoding="ascii") as f:
        for line in f:
            if line.startswith("#") or line.startswith("t_C"):
                continue
            t, e = line.strip().split(",")
            pts[int(t)] = float(e)
    return pts


def run_tests():
    tab = load_nist_table()
    pkg = load_pkg()
    log = load_log()

    print("-- data --")
    check("NIST table: 1643 points -270..1372 C", lambda: len(tab) == 1643 and min(tab) == -270 and max(tab) == 1372)
    check("soak log: 2 scenarios x 601 rows", lambda: sorted(log) == ["bench_fan", "enclosure"]
          and all(len(v) == 601 for v in log.values()))

    print("-- Part A: ITS-90 type K + CJC --")
    for t in (0, 25, 100, -100, 500, 1000, 1372, -270):
        check("emf_mV(%d) == NIST %.3f mV (+-0.0005)" % (t, tab[t]),
              lambda t=t: near(emf_mV(float(t)), tab[t], 0.0005 + 1e-12, "E(%d)" % t))

    def full_table():
        worst = max(abs(emf_mV(float(t)) - e) for t, e in tab.items())
        print("      max |E(t) - table| over 1643 points = %.6f mV" % worst)
        return worst <= 0.0005 + 1e-12
    check("emf_mV matches all 1643 NIST table points to table rounding", full_table)
    check("emf_mV out of range -> ValueError", lambda: raises(ValueError, lambda: emf_mV(1400.0))
          and raises(ValueError, lambda: emf_mV(-280.0)))
    check("temp_C(4.096 mV) == 100 C (+-0.06)", lambda: near(temp_C(4.096), 100.0, 0.06, "T(4.096)"))
    check("temp_C(0) == 0", lambda: near(temp_C(0.0), 0.0, 1e-12, "T(0)"))
    check("temp_C(20.644 mV) == 500 C (+-0.06)", lambda: near(temp_C(20.644), 500.0, 0.06, "T(20.644)"))
    check("temp_C(41.276 mV) == 1000 C (+-0.06)", lambda: near(temp_C(41.276), 1000.0, 0.06, "T(41.276)"))

    def roundtrip():
        worst = max(abs(temp_C(emf_mV(k / 2.0)) - k / 2.0) for k in range(-400, 2745))
        print("      max |T(E(t)) - t| over -200..1372 C step 0.5 = %.4f C" % worst)
        return worst <= 0.06
    check("roundtrip T(E(t)) within NIST inverse error band (0.06 C)", roundtrip)

    def inverse_vs_table():
        worst = 0.0
        for t in range(-199, 1372):
            s = (tab[t + 1] - tab[t - 1]) / 2.0           # mV/°C, 표에서 구한 기울기
            tol = 0.06 + 0.0005 / s                      # 역함수 오차 + 표 반올림(0.0005 mV)
            err = abs(temp_C(tab[t]) - t)
            worst = max(worst, err)
            if err > tol:
                print("      t=%d err=%.4f tol=%.4f" % (t, err, tol))
                return False
        print("      max |T(E_table) - t| = %.4f C" % worst)
        return True
    check("temp_C(table E) == t within error band + table rounding", inverse_vs_table)
    check("temp_C out of range -> ValueError", lambda: raises(ValueError, lambda: temp_C(55.0))
          and raises(ValueError, lambda: temp_C(-6.0)))
    check("tc_temperature(E(85)-E(35), 35) == 85 C",
          lambda: near(tc_temperature(emf_mV(85.0) - emf_mV(35.0), 35.0), 85.0, 0.06, "85/35"))
    check("tc_temperature(E(-150)-E(25), 25) == -150 C",
          lambda: near(tc_temperature(emf_mV(-150.0) - emf_mV(25.0), 25.0), -150.0, 0.06, "-150/25"))
    check("tc_temperature: hot == cold -> V=0 -> reads Tcj",
          lambda: near(tc_temperature(0.0, 31.0), 31.0, 0.06, "0V/31"))

    print("-- Part B: what CJC mistakes cost --")
    for t, ref in ((0.0, 39.451), (25.0, 40.518), (-150.0, 23.622), (1000.0, 38.981)):
        check("seebeck(%g) == %.3f uV/C" % (t, ref),
              lambda t=t, ref=ref: near(seebeck_uV_per_C(t), ref, 0.002, "S(%g)" % t))

    def seebeck_vs_numeric():
        worst = 0.0
        for k in range(-260, 1370, 7):
            h = 1e-3
            num = (emf_mV(k + h) - emf_mV(k - h)) / (2 * h) * 1000.0
            worst = max(worst, abs(seebeck_uV_per_C(float(k)) - num))
        print("      max |analytic - central difference| = %.2e uV/C" % worst)
        return worst < 1e-3
    check("seebeck analytic == central difference of emf_mV", seebeck_vs_numeric)
    check("seebeck continuous at 0 C (both branches agree)",
          lambda: near(seebeck_uV_per_C(-1e-9), seebeck_uV_per_C(0.0), 1e-3, "S(0-) vs S(0+)"))
    check("cj_error_gain(-150, 25) == 1.715", lambda: near(cj_error_gain(-150.0, 25.0), 1.715, 0.002, "gain"))
    check("cj_error_gain(85, 35) == 0.984", lambda: near(cj_error_gain(85.0, 35.0), 0.984, 0.002, "gain"))
    for th, tcj, exp in ((85.0, 35.0, {"none": -33.899, "add_temp": 1.101, "linear": 0.082}),
                         (-40.0, 25.0, {"none": -28.258, "add_temp": -3.258, "linear": 3.773}),
                         (1000.0, 35.0, {"none": -35.939, "add_temp": -0.939, "linear": 0.899})):
        for m, ref in sorted(exp.items()):
            check("method_errors(%g, cj=%g)[%s] == %+.3f C" % (th, tcj, m, ref),
                  lambda th=th, tcj=tcj, m=m, ref=ref: near(method_errors(th, tcj)[m], ref, 0.002, m))
        check("method_errors(%g, cj=%g)[nist] within 0.06 C" % (th, tcj),
              lambda th=th, tcj=tcj: near(method_errors(th, tcj)["nist"], 0.0, 0.06, "nist"))

    check("no-CJC inverse out of range at Thot=-150, Tcj=25 (V < E(-200)) -> ValueError",
          lambda: raises(ValueError, lambda: naive_no_cjc(emf_mV(-150.0) - emf_mV(25.0))))

    def linear_worst():
        worst = max((abs(method_errors(float(t), 25.0)["linear"]), t) for t in range(0, 1001))
        print("      linear 41.276 uV/C, Tcj=25: worst |err| = %.2f C at %d C" % worst)
        return near(worst[0], 7.03, 0.01, "worst") and worst[1] == 771
    check("linear model worst error over 0..1000 C (Tcj 25) = 7.03 C at 771 C", linear_worst)

    def cj_sensor_offset():
        # 냉접점 센서가 +2 °C 틀릴 때 (서미스터가 단자대에서 떨어져 있음): 결과 오차 = 2 × gain
        v = emf_mV(-150.0) - emf_mV(25.0)
        err = tc_temperature(v, 27.0) - (-150.0)
        return near(err, 2.0 * cj_error_gain(-150.0, 25.0), 0.07, "Tcj+2 err")
    check("CJ sensor +2 C at Thot=-150 -> ~+3.4 C error (gain 1.7x)", cj_sensor_offset)

    print("-- Part C: soak log -> Tj --")
    r0 = [r for r in log["enclosure"] if r["t_s"] == 600.0][0]
    check("package_power_W(row@600s) == 3.58 W (+-0.1)",
          lambda: near(package_power_W(r0), 3.58, 0.1, "P"))
    check("estimate_tj(60, 25, 3.6 W): psi 61.8 / thetaJA 97.0 / thetaJC 85.2", lambda: all((
        near(estimate_tj(60.0, 25.0, 3.6, pkg)["psi_jt"], 61.8, 1e-9, "psi"),
        near(estimate_tj(60.0, 25.0, 3.6, pkg)["theta_ja"], 97.0, 1e-9, "ja"),
        near(estimate_tj(60.0, 25.0, 3.6, pkg)["theta_jc"], 85.2, 1e-9, "jc"))))

    def steady(sc, lo, hi):
        rows = [r for r in log[sc] if lo <= r["t_s"] <= hi]
        tcs = [tc_temperature(r["tc_uV"] / 1000.0, r["cj_C"]) for r in rows]
        return is_steady([r["t_s"] for r in rows], tcs)
    check("is_steady: enclosure 60..180 s (heating) -> False", lambda: steady("enclosure", 60, 180) is False)
    check("is_steady: enclosure 1080..1200 s -> True", lambda: steady("enclosure", 1080, 1200) is True)
    check("is_steady: bench_fan 1080..1200 s -> True", lambda: steady("bench_fan", 1080, 1200) is True)

    res = {}
    for sc in ("bench_fan", "enclosure"):
        for cjc in (True, False):
            try:
                res[(sc, cjc)] = analyze(log[sc], pkg, use_cjc=cjc)
            except Exception as e:  # noqa: BLE001  (starter: TODO 가 비어 있으면 None)
                print("      analyze(%s, cjc=%s) raised %s: %s" % (sc, cjc, type(e).__name__, e))
                res[(sc, cjc)] = None

    def get(sc, cjc, k):
        return res[(sc, cjc)][k]
    check("bench_fan: Tj(psiJT) agrees with TSENS (+-1.5 C)",
          lambda: near(get("bench_fan", True, "tj_psi"), get("bench_fan", True, "tsens"), 1.5, "psi vs tsens"))
    check("bench_fan: thetaJA over-estimates Tj by > 25 C (big board + fan, not JEDEC still air)",
          lambda: get("bench_fan", True, "tj_theta_ja") - get("bench_fan", True, "tsens") > 25.0)
    check("bench_fan: verdict PASS, no flags",
          lambda: get("bench_fan", True, "verdict") == "PASS" and get("bench_fan", True, "flags") == [])
    check("enclosure: Tj(psiJT) agrees with TSENS (+-1.5 C)",
          lambda: near(get("enclosure", True, "tj_psi"), get("enclosure", True, "tsens"), 1.5, "psi vs tsens"))
    check("enclosure: verdict FAIL (Tj > 105 C)", lambda: get("enclosure", True, "verdict") == "FAIL")
    check("enclosure: thetaJA with lab Ta says <= 105 C -> false PASS",
          lambda: get("enclosure", True, "tj_theta_ja") <= pkg["tj_max_C"])
    check("enclosure: thetaJA with local air over-estimates (> +10 C) -- still not a predictor",
          lambda: get("enclosure", True, "tj_theta_ja_local") - get("enclosure", True, "tsens") > 10.0)
    check("enclosure: Tc + P*thetaJC over-estimates (> +15 C) -- thetaJC is not psiJT",
          lambda: get("enclosure", True, "tj_theta_jc") - get("enclosure", True, "tsens") > 15.0)
    check("planted defect: enclosure w/o CJC reads ~50 C low -> Tj number alone would PASS",
          lambda: get("enclosure", False, "tj_psi") <= pkg["tj_max_C"]
          and get("enclosure", True, "tc") - get("enclosure", False, "tc") > 45.0)
    check("planted defect: ...but sanity flags turn the verdict into INVALID (both scenarios)",
          lambda: get("enclosure", False, "verdict") == "INVALID" and get("bench_fan", False, "verdict") == "INVALID")
    check("planted defect caught: enclosure w/o CJC -> TSENS_MISMATCH",
          lambda: "TSENS_MISMATCH" in get("enclosure", False, "flags"))
    check("planted defect caught: bench_fan w/o CJC -> TC_BELOW_AMBIENT (idle zero check)",
          lambda: "TC_BELOW_AMBIENT" in get("bench_fan", False, "flags"))

    print("-- report --")
    for key in (("bench_fan", True), ("enclosure", True), ("bench_fan", False), ("enclosure", False)):
        if res[key] is not None:
            print(report_line(res[key], pkg))
    check("report line starts with verdict and names thetaJA verdict", lambda: report_line(
        res[("enclosure", True)], pkg).startswith("FAIL SOC0 scenario=enclosure")
        and "says PASS" in report_line(res[("enclosure", True)], pkg))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
