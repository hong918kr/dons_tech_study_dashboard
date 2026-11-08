#!/usr/bin/env python3
"""HWLAB-M02 Load Transient 응답: Droop·Recovery·위상 마진 추정

부하 스텝(0.5 → 3 A, 1 A/µs) 캡처(time, vout, iload)에서 레귤레이터 안정성을 판정한다.
  Part A  전류 채널로 스텝 시작/slew → droop · 허용 밴드 복귀 시간 · 링잉 주기 · 로그 감쇠율 → ζ → PM
          (2차 모델 해석해로 만든 깨끗한 캡처에서 주입한 ζ·주기·droop·recovery 와 비교)
  Part B  스위칭 리플·잡음(1 fsw 박스카), load-line 기준선, 느린 e-load slew(강제 응답 구간 제외),
          ζ 가 큰 경우(하한만 가능), PM≈100·ζ 근사의 유효 범위
  Part C  VALBRD-A U12 VDD_CORE_0V8 보상 변경 전/후 두 캡처(CSV)로 PASS/FAIL 리포트, 어느 쪽이 안정한지 판정

  python3 -I solution.py                         # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --report U12_before_comp.csv   # 캡처 하나에 대한 리포트만

Python 3.9+, 표준 라이브러리만 (math, json, os, sys). 모델·합성기는 gen_capture.py.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # python3 -I 는 스크립트 디렉터리를 sys.path 에 넣지 않는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import gen_capture as gen           # noqa: E402  (2차 Zout 모델 · 합성기 · 정답 계산)


# ---------------------------------------------------------------- 주어진 도구
def load_csv(path):
    """'time_us,vout_V,iload_A' CSV ('#' 주석, 헤더 1줄) → (t[s], v[V], i[A])."""
    t, v, i = [], [], []
    with open(path, encoding="ascii") as f:
        for line in f:
            s = line.strip()
            if not s or s[0] == "#" or s[0].isalpha():
                continue
            a, b, c = s.split(",")
            t.append(float(a) * 1e-6)
            v.append(float(b))
            i.append(float(c))
    return t, v, i


def mean(xs):
    return math.fsum(xs) / len(xs)


def stdev(xs):
    m = mean(xs)
    return math.sqrt(math.fsum((x - m) ** 2 for x in xs) / len(xs))


def cross_time(t, y, j, level):
    """y[j], y[j+1] 사이에서 level 을 지나는 시각 (선형 보간). 두 점이 같으면 t[j]."""
    dy = y[j + 1] - y[j]
    if dy == 0.0:
        return t[j]
    return t[j] + (level - y[j]) / dy * (t[j + 1] - t[j])


def lsq_line(xs, ys, ws=None):
    """가중 최소제곱 직선 y = a·x + b → (a, b). ws=None 이면 가중치 1."""
    if ws is None:
        ws = [1.0] * len(xs)
    W = math.fsum(ws)
    xb = math.fsum(w * x for w, x in zip(ws, xs)) / W
    yb = math.fsum(w * y for w, y in zip(ws, ys)) / W
    sxx = math.fsum(w * (x - xb) ** 2 for w, x in zip(ws, xs))
    sxy = math.fsum(w * (x - xb) * (y - yb) for w, x, y in zip(ws, xs, ys))
    a = sxy / sxx
    return a, yb - a * xb


def quad_vertex(xs, ys):
    """y = c0 + c1·x + c2·x² 최소제곱(정규방정식 3×3, 크라메르) → 꼭짓점 (x*, y*)."""
    S = [math.fsum(x ** k for x in xs) for k in range(5)]
    T = [math.fsum(y * x ** k for x, y in zip(xs, ys)) for k in range(3)]
    A = [[S[0], S[1], S[2]], [S[1], S[2], S[3]], [S[2], S[3], S[4]]]

    def det(M):
        return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
                - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]))
    D = det(A)
    c = []
    for col in range(3):
        M = [row[:] for row in A]
        for r in range(3):
            M[r][col] = T[r]
        c.append(det(M) / D)
    c0, c1, c2 = c
    xv = -c1 / (2.0 * c2)
    return xv, c0 + c1 * xv + c2 * xv * xv


# ---------------------------------------------------------------- Part A: 스텝·로브·감쇠·PM·복귀
def step_edges(t, i, pre_frac=0.05, post_frac=0.25):
    """전류 채널 → dict(i0, i1, t10, t90, t_start, t_end, slew[A/s]).

    i0 = 앞 pre_frac 구간 평균, i1 = 뒤 post_frac 구간 평균, 10/90 % 레벨.
    t90 = 90 % 에 '처음' 닿은 시각, t10 = 그 전에 10 % 아래를 '마지막으로' 떠난 시각 (HWLAB-B04 와 같은 규칙).
    사다리꼴이라 10→90 % 는 램프의 80 % → slew = 0.8·|Δi|/(t90−t10),
    램프 시작/끝은 10 %/90 % 에서 (t90−t10)/8 만큼 바깥으로 외삽 (0 % → 10 % 는 램프의 1/8).
    부하 해제(i1 < i0)도 같은 함수로 처리한다 (부호 s).
    """
    # TODO ① i0 = 앞 pre_frac 평균, i1 = 뒤 post_frac 평균, d = i1 − i0, s = d 의 부호, lo/hi = 10 %/90 % 레벨
    # TODO ② j_hi = s·(i − hi) >= 0 인 첫 샘플, j_lo = 그 전에서 s·(i − lo) < 0 인 마지막 샘플
    # TODO ③ t10 = cross_time(t, i, j_lo, lo), t90 = cross_time(t, i, j_hi − 1, hi), dur = t90 − t10
    #        slew = 0.8·|d|/dur, t_start = t10 − dur/8, t_end = t90 + dur/8
    raise NotImplementedError("TODO step_edges")


def lobes(t, e, j0, h, frac=0.5):
    """편차 e(t) = v − 기준선 에서 j0 이후의 링잉 '로브'(반주기 덩어리)를 찾는다.

    히스테리시스 상태기계: e > +h 면 + 상태, e < −h 면 − 상태. 반대쪽 ∓h 를 넘어야 다음 로브.
    로브마다:
      t_pk, a_pk : |e| 최대 샘플 주변에서 s·e ≥ frac·(최대값) 인 연속 구간을 2차 최소제곱 → 꼭짓점
                   (구간 샘플이 5개 미만이면 최대 샘플 그대로)
      t_zc       : 이 로브를 '시작한' 0 교차 시각 (직전 로브 쪽 부호의 마지막 샘플과 다음 샘플 사이 보간).
                   첫 로브는 None
    마지막 로브는 그 꼭짓점이 레코드 끝 샘플이 아닐 때만 넣는다.
    반환: [dict(sign, t_pk, a_pk, t_zc), ...]
    """
    # TODO ① state(0/+1/−1), jp(현재 로브의 |e| 최대 샘플), t_zc 를 들고 j0 부터 훑는다
    #   state == 0 : e > h 면 +1, e < −h 면 −1 로 시작 (jp = j)
    #   state != 0 : state·e 가 더 크면 jp = j.  state·e < −h 면 로브 종료 →
    #                close(jp, state, t_zc), 이전 부호의 마지막 샘플 m 과 m+1 사이 0 교차 → 다음 로브의 t_zc
    # TODO ② close(): jp 양옆으로 s·e >= frac·s·e[jp] 인 연속 구간 [a, b] → b − a >= 4 면 quad_vertex, 아니면 샘플 그대로
    # TODO ③ 루프가 끝나면 마지막 로브는 jp < n − 1 일 때만 close
    return []


def zeta_from_lambda(lam):
    """반주기 로그 감쇠율 λ = ln(|a_k| / |a_k+1|) = πζ/√(1−ζ²)  →  ζ = λ/√(π² + λ²).
    (한 주기 감쇠율 δ = 2λ 를 쓰면 ζ = δ/√(4π² + δ²) — 같은 식)"""
    # TODO: 한 줄 — λ = πζ/√(1−ζ²) 를 ζ 에 대해 푼다
    raise NotImplementedError("TODO zeta_from_lambda")


def fit_decay(sel, floor=None):
    """연속된 자유감쇠 로브들 → dict(n, lam, zeta, Td, Td_src, zeta_min).

    λ  : ln|a_k| 를 반주기 번호 k 에 대해 가중 최소제곱 (가중치 a_k², var(ln a) ≈ σ²/a² 이므로) → 기울기 = −λ
    Td : 0 교차가 2개 이상이면 t_zc 를 k 에 대한 직선으로 맞춘 기울기 × 2  (Td_src="zc")
         아니면 꼭짓점 시각 t_pk 의 직선 기울기 × 2                      (Td_src="pk")
    로브가 1개뿐이면 ζ 는 측정 불가 → floor(잡음 문턱)가 있으면 λ ≥ ln(|a0|/floor) 로 하한 zeta_min.
    로브가 0개면 전부 None.
    """
    res = {"n": len(sel), "lam": None, "zeta": None, "Td": None, "Td_src": None, "zeta_min": None}
    # TODO ① 로브 2개 이상: lsq_line(k, ln|a_k|, 가중치 a_k²) 기울기 = −λ → zeta_from_lambda
    # TODO ② Td: k > 0 이고 t_zc 가 있는 로브가 2개 이상이면 t_zc 직선 기울기 × 2 ("zc"), 아니면 t_pk 로 ("pk")
    # TODO ③ 로브 1개 + floor: zeta_min = zeta_from_lambda(ln(|a0| / floor))
    return res


def phase_margin(zeta):
    """감쇠비 → (PM_exact[deg], PM_rule[deg]).

    exact: 개루프 L(s) = ωn² / (s·(s + 2ζωn)) (단위 피드백 2차) 의 위상 마진
           ωc = ωn·√(√(1+4ζ⁴) − 2ζ²),  PM = atan(2ζωn/ωc) = atan(2ζ / √(√(1+4ζ⁴) − 2ζ²))
    rule : PM ≈ 100·ζ  (ζ ≲ 0.6 에서 ±3° 정도)
    """
    # TODO: exact = degrees(atan(2ζ / √(√(1+4ζ⁴) − 2ζ²))), rule = 100·ζ
    raise NotImplementedError("TODO phase_margin")


def recovery_time(t, vf, t_start, v_final, band):
    """스텝 시작 t_start 부터 |vf − v_final| ≤ band 에 '계속 머무르기 시작'하는 시각까지 [s].

    t_start 이후 밴드 밖인 마지막 샘플 j 를 찾고 j, j+1 사이에서 밴드 경계(v_final ± band, j 쪽)를 보간.
    한 번도 밖으로 안 나갔으면 0.0, 레코드 끝까지 밖이면 None.
    """
    # TODO ① t >= t_start 이면서 |vf − v_final| > band 인 마지막 인덱스 last
    # TODO ② last 가 없으면 0.0, 레코드 끝이면 None
    # TODO ③ 경계 edge = v_final ± band (vf[last] 쪽), cross_time(t, vf, last, edge) − t_start
    return None


# ---------------------------------------------------------------- Part B: 리플·기준선·자유감쇠 구간
def boxcar(t, v, n):
    """길이 n 이동평균 (유효 구간만) → (tf, vf), len = len(v) − n + 1.
    tf[k] = 창의 가운데 시각 (t[k] + t[k+n−1]) / 2 → 지연 없음. n = 1/(fsw·dt) 이면 fsw 와 모든 고조파가 0.
    누적합(prefix sum)으로 O(N)."""
    # TODO: 누적합 ps 를 만들고 vf[k] = (ps[k+n] − ps[k]) / n,  tf[k] = (t[k] + t[k+n−1]) / 2,  k = 0 … N−n
    return list(t), list(v)  # 지금은 필터 없음 — 리플이 그대로 남는다


def select_free_decay(lbs, t_end, thr):
    """램프가 끝난 뒤(t_pk > t_end)의 로브 중 |a| ≥ thr 인 것을 '연속으로' 고른다.
    첫 번째로 thr 아래인 로브에서 멈춘다 (그 뒤는 잡음 로브일 수 있음)."""
    # TODO: t_pk <= t_end 인 로브는 건너뛰고, |a_pk| < thr 인 첫 로브에서 멈춘다
    return list(lbs)  # 지금은 '모든 로브' = 순진한 방법


def analyze(t, v, i, fsw=None, band=8e-3, filt=True, baseline="settled", free_decay=True, hyst=True,
            k_h=3.0, k_thr=6.0):
    """캡처 하나 분석 (주어진 오케스트레이터). 각 단계 스위치를 끄면 '순진한' 방법이 된다.
      filt       : 1 fsw 주기 박스카로 리플 제거 (fsw 필요)
      baseline   : "settled" = 기록 뒤 25 % 평균(최종값) / "pre" = 스텝 전 평균
      free_decay : 램프 끝난 뒤 로브만 + 잡음 문턱 thr = max(k_thr·σ, 1e-3·droop)
      hyst       : 로브 히스테리시스 h = k_h·σ (σ = 스텝 전 필터 출력의 표준편차). False 면 h = 0
    """
    ed = step_edges(t, i)
    dt = t[1] - t[0]
    if filt and fsw:
        tf, vf = boxcar(t, v, int(round(1.0 / (fsw * dt))))
    else:
        tf, vf = list(t), list(v)
    pre = [x for tk, x in zip(tf, vf) if tk < ed["t_start"] - 1e-6]
    v_pre, sd = mean(pre), stdev(pre)
    v_final = mean(vf[-(len(vf) // 4):]) if baseline == "settled" else v_pre
    e = [x - v_final for x in vf]
    j0 = next(j for j in range(len(tf)) if tf[j] >= ed["t_start"])
    sgn = 1.0 if ed["i1"] > ed["i0"] else -1.0                       # 부하 증가 → 전압 하강(droop)
    post = vf[j0:]
    droop = sgn * (v_pre - (min(post) if sgn > 0 else max(post)))
    jr = next(j for j in range(len(t)) if t[j] >= ed["t_start"])
    h = k_h * sd if hyst else 0.0
    thr = max(k_thr * sd, 1e-3 * abs(droop))
    lbs = lobes(tf, e, j0, h)
    sel = select_free_decay(lbs, ed["t_end"], thr) if free_decay else lbs
    fit = fit_decay(sel, floor=thr if free_decay else None)
    rec = recovery_time(tf, vf, ed["t_start"], v_final, band)
    m = {"edges": ed, "v_pre": v_pre, "v_final": v_final, "sigma": sd, "h": h, "thr": thr, "droop": droop,
         "vmin_raw": min(v[jr:]), "vmax_raw": max(v[jr:]), "n_lobes": len(lbs), "recovery": rec}
    m.update(fit)
    if fit["zeta"] is not None:
        m["pm"], m["pm_rule"] = phase_margin(fit["zeta"])
    else:
        m["pm"] = m["pm_rule"] = None
    m["pm_min"] = phase_margin(fit["zeta_min"])[0] if fit["zeta_min"] is not None else None
    return m


# ---------------------------------------------------------------- Part C: 판정과 리포트
def load_spec(path=os.path.join(HERE, "spec_vdd_core_0v8.json")):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def judge(m, spec):
    """측정 dict + 스펙 → (items, verdict).
    items = [(이름, 값 문자열, 한계 문자열, ok)] 순서: slew, Vmin, Vmax, recovery, PM
      slew     : 측정 slew 가 스펙 slew ± slew_tol_pct 안 (시험 조건 검증 — 아니면 verdict "INVALID")
      Vmin/Vmax: 원시 캡처 최소/최대가 vnom·(1 ∓ window_pct/100) 안
      recovery : None 이 아니고 ≤ recovery_max_us
      PM       : ζ 가 있으면 PM_exact ≥ pm_min_deg; ζ 가 없고 하한 PM_min 만 있으면 PM_min ≥ pm_min_deg 일 때 ok
    verdict: slew 가 틀리면 "INVALID", 아니면 나머지 하나라도 틀리면 "FAIL", 전부 ok 면 "PASS".
    """
    # TODO ① slew(A/µs) 가 spec 의 ±slew_tol_pct 안인지 (시험 조건 검증)
    # TODO ② Vmin/Vmax 가 vnom·(1 ∓ window) 안인지, recovery 가 None 이 아니고 ≤ recovery_max_us 인지
    # TODO ③ PM: m["pm"] 이 있으면 그것으로, 없고 m["pm_min"] 이 있으면 하한으로 판정, 둘 다 없으면 "n/a" + False
    # TODO ④ items 를 위 순서로 만들고 verdict: slew 실패 → "INVALID", 하나라도 실패 → "FAIL", 아니면 "PASS"
    return [], "TODO"


def report(name, m, spec):
    """사람이 읽는 리포트 (여러 줄) + 한 줄 요약 + verdict."""
    items, verdict = judge(m, spec)
    lines = ["%s %s -> %s  droop=%.1fmV ring=%s Td_src=%s lobes=%d sigma=%.2fmV"
             % (spec["rail"], name, verdict, m["droop"] * 1e3,
                "n/a" if m["Td"] is None else "%.1fkHz" % (1e-3 / m["Td"]), m["Td_src"], m["n"], m["sigma"] * 1e3)]
    for nm, val, lim, ok in items:
        lines.append("  %-4s %-9s = %-22s %s" % ("ok" if ok else "BAD", nm, val, lim))
    bad = [nm for nm, _, _, ok in items if not ok]
    if m["zeta"] is not None:
        zs = "zeta=%.3f PM=%.0fdeg(100z:%.0f)" % (m["zeta"], m["pm"], m["pm_rule"])
    elif m["zeta_min"] is not None:
        zs = "zeta>=%.2f PM>=%.0fdeg" % (m["zeta_min"], m["pm_min"])
    else:
        zs = "zeta=n/a"
    one = "%s %s %s: %s droop=%.1fmV rec=%s%s" % (
        verdict, spec["rail"], name, zs, m["droop"] * 1e3,
        "None" if m["recovery"] is None else "%.1fus" % (m["recovery"] * 1e6),
        (" BAD=" + ",".join(bad)) if bad else "")
    return "\n".join(lines), one, verdict


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() → bool 또는 (bool, 설명). 예외도 FAIL (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    detail = ""
    try:
        r = fn()
        ok, detail = (r if isinstance(r, tuple) else (r, ""))
        ok = bool(ok)
    except Exception as ex:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(ex).__name__, ex)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def safe(fn, default=None):
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as ex:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(ex).__name__, ex))
        return default


def us(x):
    return "None" if x is None else "%.3fus" % (x * 1e6)


def capture(p, noisy=True, seed=7):
    """Part A/B 합성 캡처: 20 MS/s, −20 … +230 µs. noisy=True 면 Part C 와 같은 리플·잡음·양자화."""
    if noisy:
        return gen.synth(p, gen.CAP_DT, gen.CAP_T0, gen.CAP_N, ripple_pp=gen.CAP_RIPPLE_PP, fsw=gen.CAP_FSW,
                         noise_v=gen.CAP_NOISE_V, noise_i=gen.CAP_NOISE_I, lsb_v=gen.CAP_LSB, seed=seed)
    return gen.synth(p, gen.CAP_DT, gen.CAP_T0, gen.CAP_N)


def pm_numeric(zeta):
    """독립 검산: L(jω) = ωn²/(jω(jω+2ζωn)) 에서 |L| = 1 인 ω 를 이분법으로 찾고 PM = 180° + ∠L."""
    a, b = 1e-6, 10.0                                  # ωn = 1 로 정규화
    for _ in range(200):
        m = 0.5 * (a + b)
        if abs(1.0 / (1j * m * (1j * m + 2 * zeta))) > 1.0:
            a = m
        else:
            b = m
    w = 0.5 * (a + b)
    ang = math.degrees(math.atan2((1.0 / (1j * w * (1j * w + 2 * zeta))).imag,
                                  (1.0 / (1j * w * (1j * w + 2 * zeta))).real))
    return 180.0 + ang


def run_tests():
    base = dict(gen.BOARD_BASE, fn=48e3)
    band = 8e-3

    # ---- Part A: 깨끗한 2차 모델 캡처 (리플·잡음 없음) — 주입한 참값과 비교
    print("-- Part A: clean 2nd-order captures, measured vs injected truth --")
    tA, vA, iA = capture(dict(base, zeta=0.25), noisy=False)

    def edges_ok():
        ed = step_edges(tA, iA)
        return (near(ed["slew"], 1.0e6, 0.005e6) and near(ed["t_start"], -0.25e-6, 10e-9)
                and near(ed["t_end"], 2.25e-6, 10e-9),
                "slew=%.4fA/us t_start=%s t_end=%s (truth 1.0, -0.250, 2.250us)"
                % (ed["slew"] * 1e-6, us(ed["t_start"]), us(ed["t_end"])))
    check("step_edges: slew 1A/us +-0.5%, ramp start/end +-10ns", edges_ok)
    check("zeta_from_lambda inverts lambda = pi*z/sqrt(1-z^2) (z=0.05..0.9)",
          lambda: all(near(zeta_from_lambda(math.pi * z / math.sqrt(1 - z * z)), z, 1e-12)
                      for z in (0.05, 0.15, 0.3, 0.55, 0.9)))
    for z in (0.10, 0.25, 0.40, 0.55):
        p = dict(base, zeta=z)
        tr = gen.truth(p, band)
        t, v, i = capture(p, noisy=False)
        m = safe(lambda: analyze(t, v, i, fsw=None, band=band), {})

        def row(m=m, tr=tr, z=z):
            ok = (near(m["zeta"], z, 0.003) and near(m["Td"], tr["Td"], 0.005 * tr["Td"])
                  and near(m["droop"], tr["droop"], 0.15e-3) and near(m["recovery"], tr["recovery"], 0.1e-6))
            return ok, ("zeta %.4f/%.2f | Td %s/%s (%s) | droop %.2f/%.2fmV | rec %s/%s | lobes used %d"
                        % (m["zeta"], z, us(m["Td"]), us(tr["Td"]), m["Td_src"], m["droop"] * 1e3,
                           tr["droop"] * 1e3, us(m["recovery"]), us(tr["recovery"]), m["n"]))
        check("zeta=%.2f: zeta+-0.003, Td+-0.5%%, droop+-0.15mV, recovery+-0.1us" % z, row)

    def pm_table():
        rows = [(z, phase_margin(z)[0], pm_numeric(z)) for z in (0.1, 0.3, 0.5, 0.7, 0.9)]
        return (all(near(a, b, 0.01) for _, a, b in rows),
                " ".join("z%.1f:%.2f/%.2f" % r for r in rows))
    check("phase_margin exact == numeric |L(jw)|=1 bisection (+-0.01deg)", pm_table)
    check("phase_margin(0.5) = 51.83deg, rule 50deg",
          lambda: (near(phase_margin(0.5)[0], 51.827, 0.01) and phase_margin(0.5)[1] == 50.0,
                   "%.3f / %.1f" % phase_margin(0.5)))

    # ---- Part B: 측정 함정
    print("-- Part B: ripple/noise, load-line baseline, slow e-load, high zeta, 100*zeta rule --")
    tr0 = [k * 50e-9 for k in range(400)]
    rip = [8e-3 * (2.0 * abs((tk * 1e6 + 0.3) % 1.0 - 0.5) - 0.5) for tk in tr0]
    check("boxcar(n=20 = 1/(fsw*dt)) nulls 8mVpp triangle ripple; n=19 leaves residual",
          lambda: (lambda a, b: (a < 1e-12 and b > 0.1e-3, "n=20 max|res|=%.1e V, n=19 %.3fmV" % (a, b * 1e3)))(
              max(abs(x) for x in boxcar(tr0, rip, 20)[1]), max(abs(x) for x in boxcar(tr0, rip, 19)[1])))
    check("boxcar keeps time centered: tf[0] == (t[0]+t[n-1])/2, len = N-n+1",
          lambda: (lambda r: near(r[0][0], 0.5 * (tr0[0] + tr0[19]), 1e-15) and len(r[1]) == 381)(
              boxcar(tr0, rip, 20)))

    for z, fn_, seed, tol in ((0.15, 48e3, 11, 0.005), (0.55, 36e3, 12, 0.03)):   # tol ≈ 5σ (demo 표 3)
        p = dict(base, zeta=z, fn=fn_)
        t, v, i = capture(p, seed=seed)
        mr = safe(lambda: analyze(t, v, i, fsw=1e6, band=band), {})
        mn = safe(lambda: analyze(t, v, i, fsw=1e6, band=band, filt=False, free_decay=False, hyst=False), {})
        check("ripple+noise zeta=%.2f: robust zeta +-%.3f" % (z, tol),
              lambda: (near(mr["zeta"], z, tol), "robust zeta=%.4f (lobes used %d, sigma=%.3fmV, thr=%.2fmV)"
                       % (mr["zeta"], mr["n"], mr["sigma"] * 1e3, mr["thr"] * 1e3)))
        check("ripple+noise zeta=%.2f: naive (raw, h=0, all lobes) finds ripple lobes, zeta off" % z,
              lambda: (mn["n_lobes"] > 50 and not near(mn["zeta"], z, 0.05),
                       "naive lobes=%d zeta=%s" % (mn["n_lobes"], None if mn.get("zeta") is None
                                                   else "%.3f" % mn["zeta"])))

    pL = dict(base, zeta=0.25, r_ll=2e-3)                      # AVP load-line 2 mΩ → 최종값 5 mV 아래
    tL, vL, iL = capture(pL, seed=21)
    mLs = safe(lambda: analyze(tL, vL, iL, fsw=1e6, band=band), {})
    mLp = safe(lambda: analyze(tL, vL, iL, fsw=1e6, band=band, baseline="pre"), {})
    check("load-line 2mOhm: settled baseline -> zeta +-0.02, v_final 5mV below v_pre",
          lambda: (near(mLs["zeta"], 0.25, 0.02) and near(mLs["v_pre"] - mLs["v_final"], 5e-3, 0.3e-3),
                   "zeta=%.4f dV=%.2fmV" % (mLs["zeta"], (mLs["v_pre"] - mLs["v_final"]) * 1e3)))
    check("load-line 2mOhm: pre-step baseline biases zeta by > 0.03",
          lambda: (mLp["zeta"] is None or abs(mLp["zeta"] - 0.25) > 0.03,
                   "pre-baseline zeta=%s lobes used %d" % (None if mLp["zeta"] is None else "%.3f" % mLp["zeta"],
                                                           mLp["n"])))

    pS = dict(base, zeta=0.20, slew=0.1e6)                     # 느린 e-load 0.1 A/µs → 램프 25 µs > Td
    d_slow, d_fast = gen.truth(pS, band)["droop"], gen.truth(dict(pS, slew=1e6), band)["droop"]
    check("slow e-load 0.1A/us: true droop < 35% of the 1A/us droop (slow load hides the problem)",
          lambda: (d_slow < 0.35 * d_fast, "%.2fmV vs %.2fmV" % (d_slow * 1e3, d_fast * 1e3)))
    tS, vS, iS = capture(pS, noisy=False)
    mSf = safe(lambda: analyze(tS, vS, iS, band=band), {})
    mSa = safe(lambda: analyze(tS, vS, iS, band=band, free_decay=False), {})
    check("slow e-load (clean): free-decay lobes only -> zeta +-0.005; slew measured 0.1A/us",
          lambda: (near(mSf["zeta"], 0.20, 0.005) and near(mSf["edges"]["slew"], 0.1e6, 0.001e6),
                   "zeta=%.4f slew=%.4fA/us lobes used %d" % (mSf["zeta"], mSf["edges"]["slew"] * 1e-6, mSf["n"])))
    check("slow e-load (clean): including forced (during-ramp) lobes biases zeta by > 0.05",
          lambda: (abs(mSa["zeta"] - 0.20) > 0.05, "all-lobes zeta=%.3f" % mSa["zeta"]))
    tS2, vS2, iS2 = capture(pS, seed=31)
    tF2, vF2, iF2 = capture(dict(pS, slew=1e6), seed=31)
    nS = safe(lambda: analyze(tS2, vS2, iS2, fsw=1e6, band=band)["n"], 0)
    nF = safe(lambda: analyze(tF2, vF2, iF2, fsw=1e6, band=band)["n"], 0)
    check("same noise: usable free-decay lobes at 0.1A/us <= half of those at 1A/us (less excitation)",
          lambda: (nF >= 4 and nS <= nF // 2, "0.1A/us: %d lobes, 1A/us: %d lobes" % (nS, nF)))

    pH = dict(base, zeta=0.80, fn=36e3)
    tH, vH, iH = capture(pH, seed=41)
    mH = safe(lambda: analyze(tH, vH, iH, fsw=1e6, band=band), {})
    check("zeta=0.8: one lobe above noise -> zeta=None, lower bound 0.6 <= zeta_min <= 0.8, PM_min >= 55deg",
          lambda: (mH["zeta"] is None and mH["zeta_min"] is not None and 0.6 <= mH["zeta_min"] <= 0.8
                   and mH["pm_min"] >= 55.0,
                   "zeta_min=%.3f PM_min=%.1fdeg (truth zeta 0.8, PM %.1f)"
                   % (mH["zeta_min"], mH["pm_min"], phase_margin(0.8)[0])))

    def rule_table():
        rows = [(z,) + phase_margin(z) for z in (0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8)]
        good = all(abs(b - a) <= 3.5 for z, a, b in rows if z <= 0.6)
        bad = all(abs(b - a) > 4.5 for z, a, b in rows if z >= 0.7)
        return good and bad, " ".join("z%.1f:%.1f/%.0f" % r for r in rows)
    check("100*zeta within 3.5deg of exact for zeta<=0.6, off by >4.5deg for zeta>=0.7", rule_table)

    # ---- Part C: VALBRD-A U12 VDD_CORE_0V8, 보상 변경 전/후
    print("-- Part C: VALBRD-A U12 VDD_CORE_0V8 load-step report (before/after compensation) --")
    spec = load_spec()
    res = {}
    for name in ("U12_before_comp.csv", "U12_after_comp.csv"):
        cap = safe(lambda: load_csv(os.path.join(HERE, name)))
        if cap is None:
            continue
        m = safe(lambda: analyze(*cap, fsw=spec["fsw_Hz"], band=spec["vnom_V"] * spec["recovery_band_pct"] / 100.0))
        r = safe(lambda: report(name[:-4], m, spec)) if m else None
        if r:
            print(r[0])
        res[name] = (cap, m, r)
    check("captures loaded: 5000 samples @ 50ns, 3 columns",
          lambda: all(len(c[0][1]) == 5000 and near(c[0][0][1] - c[0][0][0], 50e-9, 1e-12) for c in res.values())
          and len(res) == 2)
    tb, ta = (gen.CAPTURES["U12_before_comp.csv"], gen.CAPTURES["U12_after_comp.csv"])
    check("before: zeta within +-0.005 of injected 0.15, ring period within 2%",
          lambda: (lambda m, tr: (near(m["zeta"], 0.15, 0.005) and near(m["Td"], tr["Td"], 0.02 * tr["Td"]),
                                  "zeta=%.4f Td=%s/%s" % (m["zeta"], us(m["Td"]), us(tr["Td"]))))(
              res["U12_before_comp.csv"][1], gen.truth(tb, 8e-3)))
    check("after: zeta within +-0.03 of injected 0.55, ring period within 6% (only 2 lobes)",
          lambda: (lambda m, tr: (near(m["zeta"], 0.55, 0.03) and near(m["Td"], tr["Td"], 0.06 * tr["Td"]),
                                  "zeta=%.4f Td=%s/%s (%s)" % (m["zeta"], us(m["Td"]), us(tr["Td"]), m["Td_src"])))(
              res["U12_after_comp.csv"][1], gen.truth(ta, 8e-3)))
    check("recovery within +-0.5us of model truth (both)",
          lambda: (all(near(res[n][1]["recovery"], gen.truth(gen.CAPTURES[n], 8e-3)["recovery"], 0.5e-6)
                       for n in res), " ".join("%s/%s" % (us(res[n][1]["recovery"]),
                                                          us(gen.truth(gen.CAPTURES[n], 8e-3)["recovery"]))
                                               for n in res)))

    def verdicts():
        items, vb = judge(res["U12_before_comp.csv"][1], spec)
        _, va = judge(res["U12_after_comp.csv"][1], spec)
        bad = [nm for nm, _, _, ok in items if not ok]
        return vb == "FAIL" and va == "PASS" and bad == ["recovery", "PM"], "before=%s BAD=%s after=%s" % (
            vb, bad, va)
    check("before -> FAIL (recovery, PM) ; after -> PASS (window OK on both)", verdicts)

    def invalid():
        m = dict(res["U12_after_comp.csv"][1])
        m["edges"] = dict(m["edges"], slew=0.1e6)
        return judge(m, spec)[1] == "INVALID"
    check("e-load slew 0.1A/us vs spec 1A/us -> verdict INVALID (test condition not met)", invalid)
    for n in res:
        if res[n][2]:
            print("      " + res[n][2][1])
    ok_names = [n[:-4] for n in res if res[n][2] and res[n][2][2] == "PASS"]
    print("      DECISION: adopt %s" % (", ".join(ok_names) or "none (re-tune compensation)"))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 3 and argv[1] == "--report":
        spec = load_spec()
        t, v, i = load_csv(argv[2])
        m = analyze(t, v, i, fsw=spec["fsw_Hz"], band=spec["vnom_V"] * spec["recovery_band_pct"] / 100.0)
        text, one, _ = report(os.path.basename(argv[2])[:-4], m, spec)
        print(text)
        print(one)
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
