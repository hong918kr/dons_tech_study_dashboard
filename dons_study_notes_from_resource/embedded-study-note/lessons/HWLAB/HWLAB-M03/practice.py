#!/usr/bin/env python3
"""HWLAB-M03 추가 연습 — 본문 PDN 도구(solution.py)를 확장한다

  연습 1  derate_parts / corner : MLCC 공차·온도·DC bias 코너에서 rev C 가 아직 Ztarget 을 지키는가, 아니면 BOM 을 다시
  연습 2  z_from_s11 / 감도     : 10 mΩ DUT 를 1-port 반사(S11)로 재면 왜 안 되는가 — 0.01 dB 오차의 결과
  연습 3  bulk ESR 스윕         : VRM–벌크 반공진을 ESR 로 감쇠 (controlled-ESR / 폴리머 벌크) — 최적 ESR ≈ √(L/C)
  연습 4  봉투 뒷면 하한         : C_bulk ≥ L_vrm/Zt², Σ n/ESL ≥ 2π·f_hi/Zt — 필요조건이지 충분조건이 아니다

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import copy
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 도구)

# ---------------------------------------------------------------- 연습 1
# 코너 계수 (대표값 — 실제는 제조사 DC-bias 곡선/시뮬레이터로 확인 필요)
TOL_LOW = 0.90                      # K 등급 ±10 % 의 하한
X5R_TEMP_LOW = 0.85                 # X5R: −55…85 °C 에서 ±15 %
DC_BIAS_AT_0V9 = {"C100u_1206": 0.80, "C10u_0603": 0.85, "C1u_0402": 0.92}   # 6.3 V 정격 부품에 0.9 V 인가


def derate_parts(parts):
    """최악 코너 라이브러리: C × 공차 하한 × 온도 하한 × DC bias 계수 (ESR/ESL 은 그대로)."""
    out = copy.deepcopy(parts)
    for k, p in out.items():
        p["c_f"] *= TOL_LOW * X5R_TEMP_LOW * DC_BIAS_AT_0V9[k]
    return out


# ---------------------------------------------------------------- 연습 2
def z_from_s11(s11, z0=50.0):
    """1-port 반사: Z = Z0 (1 + S11)/(1 − S11)."""
    return z0 * (1 + s11) / (1 - s11)


def scale_db(s, db):
    return s * 10 ** (db / 20.0)


# ---------------------------------------------------------------- 연습 3
def bulk_esr_sweep(spec, bom, esrs, f1=1e4, f2=1e6):
    """벌크(100 µF 자리) 부품의 ESR 만 바꿔 가며 f1..f2 최악 |Z|. 반환 [(esr, zmax, fmax)]."""
    out = []
    for r in esrs:
        parts = copy.deepcopy(spec["parts"])
        parts["C100u_1206"]["esr_ohm"] = r
        f, z = S.max_z_in_band(lambda x: S.pdn_z(x, bom, parts, spec["vrm"]), f1, f2, 20)
        out.append((r, z, f))
    return out


# ---------------------------------------------------------------- 연습 4
def lower_bounds(spec):
    """(C_bulk_min [F], Σn/ESL 최소 [1/H]) — VRM 이 Ztarget 을 넘기는 지점부터 커패시터가 받아야 하고(저주파),
    f_hi 에서 병렬 ESL 이 Ztarget 을 넘지 않아야 한다(고주파)."""
    rail, vrm = spec["rail"], spec["vrm"]
    zt = S.z_target(rail["v_nom"], rail["ripple_pct"], rail["delta_i_a"])
    c_min = vrm["l_h"] / zt ** 2
    inv_l_min = 2 * math.pi * rail["band_hz"][1] / zt
    return c_min, inv_l_min


def bom_totals(bom, parts):
    c = sum(n * parts[k]["c_f"] for k, n in bom.items())
    inv_l = sum(n / S.part_esl(parts[k]) for k, n in bom.items())
    return c, inv_l


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
    spec = S.load_spec()
    rail, parts, vrm = spec["rail"], spec["parts"], spec["vrm"]
    zt = S.z_target(rail["v_nom"], rail["ripple_pct"], rail["delta_i_a"])
    f1, f2 = rail["band_hz"]
    bom = spec["bom_rev_c"]

    print("-- 연습 1: worst-case corner (tolerance x temperature x DC bias) --")
    dparts = derate_parts(parts)
    fn, zn = S.max_z_in_band(lambda f: S.pdn_z(f, bom, parts, vrm), f1, f2, 20)
    fc, zc = S.max_z_in_band(lambda f: S.pdn_z(f, bom, dparts, vrm), f1, f2, 20)
    print("      rev C nominal %.2f mOhm @ %.3f MHz | corner %.2f mOhm @ %.3f MHz (Zt %.2f)"
          % (zn * 1e3, fn / 1e6, zc * 1e3, fc / 1e6, zt * 1e3))
    check("rev C passes at nominal but FAILS at the derated corner", lambda: zn <= zt < zc)
    cspec = copy.deepcopy(spec)
    cspec["parts"] = dparts
    rc = S.min_bom(cspec, "cost")
    print("      corner min-cost BOM: %s $%.2f %d parts, max %.2f mOhm @ %.3f MHz"
          % (S.bom_str(rc["bom"]), rc["cost"], rc["count"], rc["zmax"] * 1e3, rc["fmax"] / 1e6))
    check("corner-designed BOM meets Ztarget with derated C and costs more than rev C",
          lambda: rc["zmax"] <= zt and rc["cost"] > S.bom_cost(bom, parts))
    check("corner BOM also passes with nominal C (designing at the corner covers nominal)",
          lambda: S.max_z_in_band(lambda f: S.pdn_z(f, rc["bom"], parts, vrm), f1, f2, 20)[1] <= zt)

    print("-- 연습 2: 1-port S11 vs 2-port shunt-thru for a 10 mOhm DUT --")
    zd = 0.010
    s11 = (zd - 50) / (zd + 50)
    s21 = 2 * zd / (2 * zd + 50)
    z1 = z_from_s11(scale_db(s11, 0.01))
    z2 = S.s21_to_z(scale_db(s21, 0.01))
    print("      S11=%.6f (%.5f dB)  S21=%.3e (%.2f dB)" % (s11, 20 * math.log10(abs(s11)), s21, 20 * math.log10(s21)))
    print("      +0.01 dB magnitude error -> 1-port Z = %.2f mOhm, 2-port Z = %.4f mOhm" % (z1.real * 1e3, z2.real * 1e3))
    check("1-port: 0.01 dB error gives > 100% Z error (even the sign flips)",
          lambda: (abs(z1 - zd) / zd > 1.0, "%.0f%% error" % (abs(z1 - zd) / zd * 100)))
    check("2-port shunt-thru: same 0.01 dB error gives < 0.2% Z error",
          lambda: (abs(z2 - zd) / zd < 0.002, "%.3f%% error" % (abs(z2 - zd) / zd * 100)))

    print("-- 연습 3: damping the VRM-bulk anti-resonance with bulk ESR --")
    esrs = [0.001 * 10 ** (k / 10) for k in range(0, 21)]          # 1 mΩ … 100 mΩ
    sw = bulk_esr_sweep(spec, bom, esrs)
    best = min(sw, key=lambda x: x[1])
    ctot = sum(n * parts[k]["c_f"] for k, n in bom.items())
    z0c = math.sqrt(vrm["l_h"] / ctot)
    for r, z, f in sw[::4] + [best]:
        print("      bulk ESR %6.1f mOhm/part -> worst |Z| (10k..1M) %6.2f mOhm @ %.0f kHz" % (r * 1e3, z * 1e3, f / 1e3))
    print("      sqrt(L_vrm/C_total) = %.2f mOhm  (C_total = %.0f uF);  best ESR/part / 2 parts = %.2f mOhm"
          % (z0c * 1e3, ctot * 1e6, best[0] / 2 * 1e3))
    check("an optimum bulk ESR exists that lowers the peak below the MLCC (2.5 mOhm) case",
          lambda: best[1] < sw[4][1] and 0.0025 < best[0] < 0.1)
    check("optimum (parallel) bulk ESR within 2x of sqrt(L_vrm/C_total)",
          lambda: (0.5 < (best[0] / 2) / z0c < 2.0, "ratio %.2f" % ((best[0] / 2) / z0c)))

    print("-- 연습 4: back-of-envelope lower bounds --")
    c_min, il_min = lower_bounds(spec)
    c_rc, il_rc = bom_totals(bom, parts)
    print("      C_bulk >= L_vrm/Zt^2 = %.1f uF (rev C has %.0f uF);  sum(n/ESL) >= 2*pi*f_hi/Zt = %.2f /nH (rev C %.2f /nH)"
          % (c_min * 1e6, c_rc * 1e6, il_min * 1e-9, il_rc * 1e-9))
    check("rev C satisfies both necessary bounds", lambda: c_rc >= c_min and il_rc >= il_min)
    trap = {"C100u_1206": 1, "C10u_0603": 0, "C1u_0402": 9}
    c_t, il_t = bom_totals(trap, parts)
    ft, ztr = S.max_z_in_band(lambda f: S.pdn_z(f, trap, parts, vrm), f1, f2, 20)
    print("      trap BOM %s: C=%.0f uF, sum(n/ESL)=%.2f /nH -> bounds OK, true max %.2f mOhm @ %.3f MHz"
          % (S.bom_str(trap), c_t * 1e6, il_t * 1e-9, ztr * 1e3, ft / 1e6))
    check("bounds are necessary, not sufficient: a BOM meeting both still fails (anti-resonance)",
          lambda: c_t >= c_min and il_t >= il_min and ztr > zt)

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
