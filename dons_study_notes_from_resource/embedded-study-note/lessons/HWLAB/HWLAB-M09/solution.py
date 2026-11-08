#!/usr/bin/env python3
"""HWLAB-M09 Eye Diagram 생성: CDR 기준 폴딩·Eye Height/Width·마스크

스코프의 'Eye / Jitter' 앱이 하는 일을 직접 만든다.
  Part A  PRBS7 NRZ 캡처를 클럭 경계 기준으로 UI 단위 폴딩(folding) → 2D 히스토그램 eye,
          eye height(3σ·inner) / eye width(3σ·pp) / crossing % — 정답을 아는 파형(무ISI 램프, RJ만, 지수 채널 ISI, DCD)으로 확인
  Part B  클럭을 무엇으로 잡느냐: 공칭 클럭 vs 최소제곱 '이상 클럭' vs golden PLL(1차, BW = bitrate/1667)
          — 주입한 +100 ppm·500 kHz SJ 를 각 클럭이 얼마나 eye 에 남기는지, PLL 잔류 = |1-H(f)| 해석값과 비교
  Part C  VALBRD-S 세 레인(TX0/TX1/TX2) 폴리곤 마스크 판정 리포트 — 재반사 echo(TX1)·DCD(TX2) 결함을 잡는다

  python3 -I solution.py        # "N/M passed", 실패 있으면 exit 1

Python 3.9+, 표준 라이브러리만 (math, json, os, random, sys, time).
"""
import json
import math
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, "eye_spec.json"), encoding="utf-8") as _f:
    SPEC = json.load(_f)

UI = 1.0 / SPEC["bitrate"]          # 200 ps @ 5 Gb/s
SPU = 16                             # samples / UI  → dt = 12.5 ps (80 GS/s 실시간 스코프 상당)
PLL_BW = SPEC["bitrate"] / SPEC["pll_bw_div"]   # ≈ 3.0 MHz


# ---------------------------------------------------------------- 신호 생성 (주어짐)
def prbs7(n, seed=0x7F):
    """PRBS7 (x^7 + x^6 + 1, 피보나치 LFSR). 주기 127, 1 이 64개, 최장 런 1×7 / 0×6."""
    s, out = seed & 0x7F, []
    for _ in range(n):
        b = ((s >> 6) ^ (s >> 5)) & 1
        s = ((s << 1) | b) & 0x7F
        out.append(b)
    return out


def channel_step(dt, ui, kind="exp", tau_ui=0.28, ramp_ui=0.4, echo_ui=0.0, echo_gain=0.0, span_ui=5.0):
    """채널 step 응답 표 G[0..] (dt 간격, G[0]=0 → 1).

    kind='exp' : FIR h[m] = r^m (r = e^(-dt/τ), 1차 RC 손실을 dt 간격 탭으로) + 재반사 echo 탭
                 h ← h + g·h(m − d)  (d = echo_ui·UI), DC 이득 1 로 정규화. G = h 의 누적합.
    kind='ramp': 선형 램프 0→1 을 ramp_ui·UI 동안 (ISI 없는 이상 드라이버, 정답 확인용)
    """
    n = int(round(span_ui * ui / dt))
    if kind == "ramp":
        m = ramp_ui * ui / dt
        return [min(1.0, i / m) for i in range(n + 2)]
    r = math.exp(-dt / (tau_ui * ui))
    h = [r ** m for m in range(n)]
    if echo_gain:
        d = int(round(echo_ui * ui / dt))
        h0 = h[:]
        for m in range(n - d):
            h[m + d] += echo_gain * h0[m]
    s = sum(h)
    g, acc = [0.0], 0.0
    for x in h:
        acc += x / s
        g.append(acc)
    g.append(g[-1])
    return g


def synth_capture(n_bits, amp=0.4, chan=None, ppm=0.0, rj_ui=0.0, sj_ui=0.0, sj_hz=0.0, sj_phase=0.3,
                  dcd_ui=0.0, noise_v=0.0, seed=1, t_start_ui=3.0):
    """PRBS7 NRZ(±amp) → 채널 FIR → 잡음. 송신 에지에 RJ·SJ·DCD·주파수 오프셋(ppm) 주입.

    비트 경계 n 의 송신 시각 b_n = t_start + n·UI_tx + SJ·sin(2π f n UI_tx + φ) + RJ·N(0,1) ∓ DCD/2
      (상승 에지는 −DCD/2 빨리, 하강은 +DCD/2 늦게 → '1' 이 넓어짐)
    출력 = Σ_k Δ_k·G(t − b_k)  — NRZ 의 미분은 에지마다 Δ = ±2·amp 인 임펄스이므로 FIR 컨볼루션을
    에지 개수 × (G 길이) 만큼만 계산한다 (G 끝 이후는 차분 배열로 계단만 더함).
    반환 dict(v, dt, bits, tx_edges=[(시각, ±1)], ui_tx, cfg)
    """
    dt = UI / SPU
    chan = chan or channel_step(dt, UI)
    ui_tx = UI / (1.0 + ppm * 1e-6)
    rng = random.Random(seed)
    bits = prbs7(n_bits)
    t0 = t_start_ui * UI
    w = 2 * math.pi * sj_hz
    edges = []
    for n in range(1, n_bits):
        if bits[n] != bits[n - 1]:
            d = 1 if bits[n] else -1
            tn = n * ui_tx
            b = t0 + tn + sj_ui * UI * math.sin(w * tn + sj_phase) + rng.gauss(0.0, rj_ui * UI) - d * dcd_ui * UI / 2
            edges.append((b, d))
    N = int((t0 + n_bits * ui_tx) / dt) + 1
    span = len(chan) - 2
    y = [0.0] * N
    stepd = [0.0] * (N + 1)
    for b, d in edges:
        delta = 2.0 * amp * d
        n0 = int(b / dt) + 1
        n1 = n0 + span
        x0 = n0 - b / dt
        for n in range(n0, min(n1, N)):
            x = x0 + (n - n0)
            i = int(x)
            y[n] += delta * (chan[i] + (x - i) * (chan[i + 1] - chan[i]))
        if n1 < N:
            stepd[n1] += delta
    lvl = amp if bits[0] else -amp
    v = []
    for n in range(N):
        lvl += stepd[n]
        v.append(lvl + y[n] + (rng.gauss(0.0, noise_v) if noise_v else 0.0))
    return dict(v=v, dt=dt, bits=bits, tx_edges=edges, ui_tx=ui_tx,
                cfg=dict(ppm=ppm, rj_ui=rj_ui, sj_ui=sj_ui, sj_hz=sj_hz, dcd_ui=dcd_ui, noise_v=noise_v))


def edge_times(v, dt, level=0.0, hyst=0.05):
    """level 교차 시각(선형 보간) 목록 — 히스테리시스 ±hyst 로 잡음 이중 교차를 막는다 (HWLAB-B04 의 축약)."""
    out = []
    state = 1 if v[0] > level else -1
    for i in range(1, len(v)):
        x = v[i]
        if state < 0:
            if x >= level:
                a = v[i - 1]
                out.append(((i - 1) + (level - a) / (x - a)) * dt)
                state = 2                       # 교차는 했지만 level+hyst 를 넘기 전엔 다시 세지 않음
        elif state > 0:
            if x <= level:
                a = v[i - 1]
                out.append(((i - 1) + (level - a) / (x - a)) * dt)
                state = -2
        if state == 2 and x >= level + hyst:
            state = 1
        elif state == -2 and x <= level - hyst:
            state = -1
    return out


def nominal_clock(edges, ui, n_ui):
    """공칭 비트레이트 그대로 첫 에지에 맞춘 클럭 (주파수 오프셋·저주파 지터를 전혀 따라가지 않음)."""
    return [edges[0] + j * ui for j in range(n_ui + 1)]


def n_ui_of(cap, edges, ui):
    return int((len(cap["v"]) * cap["dt"] - edges[0]) / ui) - 1


# ---------------------------------------------------------------- Part A: 폴딩·히스토그램·eye 지표
def phases(times, clk, skip_ui=0):
    """정렬된 시각 목록 → 각 시각의 UI 내 위상 u ∈ [0,1) (클럭 경계 clk[j] ≤ t < clk[j+1] 일 때 (t−clk[j])/(clk[j+1]−clk[j])).
    clk 범위 밖이거나 j < skip_ui (PLL 락 전 구간) 이면 None. 포인터 하나로 O(N)."""
    out = []
    j, last = 0, len(clk) - 1
    for t in times:
        if t < clk[0]:
            out.append(None)
            continue
        while j < last and clk[j + 1] <= t:
            j += 1
        if j >= last or j < skip_ui:
            out.append(None)
        else:
            out.append((t - clk[j]) / (clk[j + 1] - clk[j]))
    return out


def eye_hist(xs, vs, nx=64, ny=24, vlo=-0.6, vhi=0.6):
    """eye 좌표 (x ∈ [−0.5, 0.5) UI, v [V]) 점들 → 2D 히스토그램 counts[row][col] (row 0 = 맨 위 = vhi).
    범위 밖 v 는 버린다."""
    h = [[0] * nx for _ in range(ny)]
    for x, v in zip(xs, vs):
        if v < vlo or v >= vhi:
            continue
        c = min(nx - 1, int((x + 0.5) * nx))
        r = ny - 1 - int((v - vlo) / (vhi - vlo) * ny)
        h[r][c] += 1
    return h


def _circ_mean(us):
    s = sum(math.sin(2 * math.pi * u) for u in us)
    c = sum(math.cos(2 * math.pi * u) for u in us)
    return (math.atan2(s, c) / (2 * math.pi)) % 1.0


def _mstd(a):
    m = sum(a) / len(a)
    return m, math.sqrt(sum((x - m) ** 2 for x in a) / max(1, len(a) - 1))


def eye_metrics(v, dt, clk, skip_ui=0, win=0.2):
    """폴딩한 eye 의 지표 (스코프 NRZ eye 정의).

    ① 샘플을 phases() 로 접고, 0 V 교차들의 원형 평균(circular mean) c 를 crossing 위치로 → eye 좌표
       x = ((u − c) mod 1) − 0.5  (x=0 이 eye 중심, ±0.5 가 crossing)
    ② 중심 창 |x| ≤ win/2 에서 '1'(v>0)/'0'(v<0) 레벨의 평균·σ:
       EH = (μ1 − 3σ1) − (μ0 + 3σ0),  EH_inner = min(1) − max(0),  amp = μ1 − μ0
    ③ crossing 레벨: |x| ≥ 0.48 이고 μ0+amp/4 < v < μ1−amp/4 인 점들의 평균 → cross% = (Vx − μ0)/amp·100
    ④ Vx 레벨 교차 시각을 접어 crossing 평균 기준 위치 xc (∈[−0.5,0.5) UI)의 σ, pp:
       EW = 1 − 6σ_x [UI] (= (X2μ − 3σ) − (X1μ + 3σ)),  EW_pp = 1 − pp
    반환 dict(eh, eh_inner, amp, mu1, mu0, s1, s0, vcross, cross_pct, ew, ew_pp, sx, ppx, c, xs, vs)
    """
    us = phases([i * dt for i in range(len(v))], clk, skip_ui)
    xc0 = [u for u in phases(edge_times(v, dt, 0.0), clk, skip_ui) if u is not None]
    c = _circ_mean(xc0)
    xs, vs = [], []
    for u, y in zip(us, v):
        if u is not None:
            xs.append(((u - c) % 1.0) - 0.5)
            vs.append(y)
    ones = [y for x, y in zip(xs, vs) if abs(x) <= win / 2 and y > 0]
    zeros = [y for x, y in zip(xs, vs) if abs(x) <= win / 2 and y <= 0]
    mu1, s1 = _mstd(ones)
    mu0, s0 = _mstd(zeros)
    amp = mu1 - mu0
    band = [y for x, y in zip(xs, vs) if abs(x) >= 0.48 and mu0 + amp / 4 < y < mu1 - amp / 4]
    vcross = sum(band) / len(band) if band else (mu1 + mu0) / 2      # eye 가 닫혀 crossing 띠가 없으면 중간값
    xr = [((u - c + 0.5) % 1.0) - 0.5 for u in phases(edge_times(v, dt, vcross), clk, skip_ui) if u is not None]
    mx, sx = _mstd(xr)
    ppx = max(xr) - min(xr)
    return dict(eh=(mu1 - 3 * s1) - (mu0 + 3 * s0), eh_inner=min(ones) - max(zeros), amp=amp,
                mu1=mu1, mu0=mu0, s1=s1, s0=s0, vcross=vcross, cross_pct=(vcross - mu0) / amp * 100,
                ew=1.0 - 6 * sx, ew_pp=1.0 - ppx, sx=sx, ppx=ppx, mx=mx, c=c, xs=xs, vs=vs)


def render(h, mask=None, vlo=-0.6, vhi=0.6):
    """2D 히스토그램 → ASCII eye (밀도 ' .:-=+*#%@'). mask 폴리곤 안의 빈 칸은 'o', 히트가 있는 칸은 'X'."""
    ny, nx = len(h), len(h[0])
    mx = max(max(r) for r in h) or 1
    ramp = " .:-=+*#%@"
    lines = []
    for r in range(ny):
        row = []
        for col in range(nx):
            k = h[r][col]
            x = (col + 0.5) / nx - 0.5
            y = vhi - (r + 0.5) / ny * (vhi - vlo)
            inside = mask is not None and point_in_poly(x, y, mask)
            if inside:
                row.append("X" if k else "o")
            else:
                row.append(" " if k == 0 else ramp[min(9, 1 + int(9 * math.sqrt(k / mx)))])
        lines.append("|" + "".join(row) + "|")
    return "\n".join(lines)


# ---------------------------------------------------------------- Part B: 클럭 복원
def ideal_clock_ls(edges, ui_nom):
    """'이상 클럭' = 레코드 전체에 맞춘 일정 주파수 클럭. 반환 (a, T): 경계 j 의 시각 = a + T·j.

    에지마다 UI 번호 n_k 를 붙인다: n_k = n_{k-1} + round((t_k − t_{k-1}) / ui_nom)
      (레코드 전체를 ui_nom 으로 나누면 ppm 오프셋이 누적돼 번호가 밀린다 — 인접 간격은 ≤ 7 UI 라 안전)
    최소제곱 t_k ≈ a + T·n_k : T = Σ(n−n̄)(t−t̄) / Σ(n−n̄)²,  a = t̄ − T·n̄
    """
    n = [0]
    for k in range(1, len(edges)):
        n.append(n[-1] + int(round((edges[k] - edges[k - 1]) / ui_nom)))
    nb = sum(n) / len(n)
    tb = sum(edges) / len(edges)
    sxy = sum((a - nb) * (b - tb) for a, b in zip(n, edges))
    sxx = sum((a - nb) ** 2 for a in n)
    T = sxy / sxx
    return tb - T * nb, T


def golden_pll(edges, ui, bw, n_ui):
    """1차 golden PLL (type-1, 비례 제어만). 반환 clk[0..n_ui] = 복원 클럭의 UI 경계 시각.

    D = 에지 밀도(에지 수 / UI 수), kp = 2π·bw·ui / D  (작은 이득 근사: 평균적으로 UI 당 kp·D = ω_L·UI)
    φ = 첫 에지에서 시작. 경계 j 마다: clk[j] = φ, 이 UI 에 속한 에지(t < φ + ui/2)들의 오차 e = t − φ 합을
    acc 로 모아 φ ← φ + ui + kp·acc  (인과적: UI j 의 에지는 다음 경계부터 반영)
    폐루프 H(s) = ω_L/(s+ω_L) → eye 에 남는 지터 = (1−H)·θ, |1−H(jf)| = f/√(f²+bw²)  (고역 통과)
    """
    D = (len(edges) - 1) / ((edges[-1] - edges[0]) / ui)
    kp = 2 * math.pi * bw * ui / D
    phi, k, clk = edges[0], 0, []
    for _ in range(n_ui + 1):
        clk.append(phi)
        acc = 0.0
        while k < len(edges) and edges[k] < phi + 0.5 * ui:
            acc += edges[k] - phi
            k += 1
        phi += ui + kp * acc
    return clk


def tie(edges, clk, skip_ui=0):
    """에지마다 가장 가까운 클럭 경계 대비 오차(TIE) [s]. skip_ui 이전 경계는 버림."""
    out, j, last = [], 0, len(clk) - 1
    for t in edges:
        while j < last and clk[j + 1] <= t:
            j += 1
        if j >= last:
            break
        jj = j if t - clk[j] < clk[j + 1] - t else j + 1
        if jj >= skip_ui:
            out.append((t, t - clk[jj]))
    return out


def sine_amp(pts, f):
    """(t, y) 점들에 y ≈ a·sin(2πft) + b·cos(2πft) + c 최소제곱 → 진폭 √(a²+b²)  (3×3 정규방정식)."""
    w = 2 * math.pi * f
    rows = [(math.sin(w * t), math.cos(w * t), 1.0, y) for t, y in pts]
    A = [[sum(r[i] * r[j] for r in rows) for j in range(3)] for i in range(3)]
    bvec = [sum(r[i] * r[3] for r in rows) for i in range(3)]
    for i in range(3):                       # 가우스 소거 (3x3, 피벗 불필요 — 대각 우세)
        for k in range(i + 1, 3):
            m = A[k][i] / A[i][i]
            for j in range(i, 3):
                A[k][j] -= m * A[i][j]
            bvec[k] -= m * bvec[i]
    x = [0.0] * 3
    for i in (2, 1, 0):
        x[i] = (bvec[i] - sum(A[i][j] * x[j] for j in range(i + 1, 3))) / A[i][i]
    return math.hypot(x[0], x[1])


def recover(cap, method):
    """캡처 → 0 V 교차 → 클럭 ('nominal' | 'ideal' | 'pll'). 반환 clk."""
    edges = edge_times(cap["v"], cap["dt"], 0.0)
    n_ui = n_ui_of(cap, edges, UI)
    if method == "nominal":
        return nominal_clock(edges, UI, n_ui)
    a, T = ideal_clock_ls(edges, UI)
    if method == "ideal":
        return [a + T * j for j in range(n_ui + 1)]
    return golden_pll(edges, T, PLL_BW, n_ui)


# ---------------------------------------------------------------- Part C: 마스크·판정
def point_in_poly(x, y, poly):
    """ray casting: (x, y) 에서 +x 방향 반직선이 다각형 변을 홀수 번 지나면 안쪽."""
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xi = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if x < xi:
                inside = not inside
    return inside


def mask_hits(xs, vs, poly):
    """eye 좌표 점들 중 마스크 폴리곤 안에 든 점 수 (바운딩 박스로 먼저 거른다)."""
    x0 = min(p[0] for p in poly)
    x1 = max(p[0] for p in poly)
    y0 = min(p[1] for p in poly)
    y1 = max(p[1] for p in poly)
    n = 0
    for x, y in zip(xs, vs):
        if x0 <= x <= x1 and y0 <= y <= y1 and point_in_poly(x, y, poly):
            n += 1
    return n


def judge(m, hits, spec):
    """eye 지표 + 마스크 히트 → 항목별 (이름, 측정값 문자열, 한계 문자열, ok) 와 verdict.
    항목 순서: EH, EW, cross%, mask."""
    lim = spec["limits"]
    lo, hi = lim["cross_pct"]
    items = [
        ("EH", "%.0fmV" % (m["eh"] * 1e3), ">= %.0fmV" % lim["eh_mv_min"], m["eh"] * 1e3 >= lim["eh_mv_min"]),
        ("EW", "%.3fUI" % m["ew"], ">= %.2fUI" % lim["ew_ui_min"], m["ew"] >= lim["ew_ui_min"]),
        ("cross%", "%.1f%%" % m["cross_pct"], "%.0f..%.0f%%" % (lo, hi), lo <= m["cross_pct"] <= hi),
        ("mask", "%d hits" % hits, "<= %d" % lim["mask_hits_max"], hits <= lim["mask_hits_max"]),
    ]
    return items, "PASS" if all(it[3] for it in items) else "FAIL"


LANES = {   # VALBRD-S rev A, SoC SerDes TX → J7. 공통: +100 ppm, RJ 1.5 ps, SJ 500 kHz 0.2 UI, 잡음 5 mV
    "TX0": dict(chan=dict(tau_ui=0.28), dcd_ui=0.0, seed=70, note="정상"),
    "TX1": dict(chan=dict(tau_ui=0.28, echo_ui=1.5, echo_gain=0.25), dcd_ui=0.0, seed=71,
                note="alt-BOM 패드로 가는 2.2cm T-스텁 → echo +25% @1.5UI"),
    "TX2": dict(chan=dict(tau_ui=0.28), dcd_ui=0.10, seed=72, note="TX 드라이버 트림 로드 누락 → 상승/하강 불균형 DCD 0.10UI"),
}
COMMON = dict(ppm=100.0, rj_ui=0.0075, sj_ui=0.20, sj_hz=500e3, noise_v=0.005)


def lane_capture(name, n_bits=16384):
    cfg = LANES[name]
    dt = UI / SPU
    return synth_capture(n_bits, chan=channel_step(dt, UI, **cfg["chan"]), dcd_ui=cfg["dcd_ui"],
                         seed=cfg["seed"], **COMMON)


def lane_report(name, cap, method="pll"):
    clk = recover(cap, method)
    m = eye_metrics(cap["v"], cap["dt"], clk, SPEC["skip_ui"], SPEC["eye_window_ui"])
    hits = mask_hits(m["xs"], m["vs"], SPEC["mask_ui_v"])
    items, verdict = judge(m, hits, SPEC)
    bad = [n for n, _, _, ok in items if not ok]
    line = "%s lane=%s clk=%s EH=%.0fmV EW=%.3fUI cross=%.1f%% mask=%d" % (
        verdict, name, method, m["eh"] * 1e3, m["ew"], m["cross_pct"], hits)
    if bad:
        line += "  BAD=" + ",".join(bad)
    return line, verdict, items, m, hits


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
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
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
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def truth_clock(cap):
    """송신기의 (지터 없는) 비트 경계 — 정답 클럭. Part A 는 이 클럭으로 접어 지표만 검증한다."""
    t0 = 3.0 * UI
    n = int((len(cap["v"]) * cap["dt"] - t0) / cap["ui_tx"])
    return [t0 + j * cap["ui_tx"] for j in range(n)]


def analytic_inner_eh(amp, tau_ui, s_ui):
    """1차 지수 채널, 샘플 위상 s (송신 경계 후 UI 단위)에서 최악 패턴 inner opening.
    pulse 응답 p(t) = G(t) − G(t−1), G = 1 − e^(−t/τ):  EH = 2A·(p(s) − Σ_{m≥1} p(s+m))  (PRBS7 은 모든 ≤6 비트 패턴 포함)"""
    def G(t):
        return 0.0 if t <= 0 else 1.0 - math.exp(-t / tau_ui)
    p = [G(s_ui + m) - G(s_ui + m - 1) for m in range(0, 40)]
    return 2 * amp * (p[0] - sum(abs(x) for x in p[1:]))


def run_tests():
    t_begin = time.perf_counter()
    dt = UI / SPU
    # ---- Part A: 정답 클럭(송신 경계)으로 접어 지표 정의를 검증
    print("-- Part A: folding / 2D histogram / eye metrics (truth clock) --")
    b = prbs7(254)
    runs, cur = [], 1
    for i in range(1, 127):
        if b[i] == b[i - 1]:
            cur += 1
        else:
            runs.append((b[i - 1], cur))
            cur = 1
    runs.append((b[126], cur))
    check("prbs7: period 127, 64 ones, longest run 1x7 / 0x6",
          lambda: (b[:127] == b[127:] and sum(b[:127]) == 64
                   and max(r for x, r in runs if x == 1) == 7 and max(r for x, r in runs if x == 0) == 6))
    check("phases(): clk [0,1,2,3], t=[-.5,0,.25,1.5,2.999,3.2] -> [None,0,.25,.5,.999,None]",
          lambda: [None if u is None else round(u, 3) for u in phases([-0.5, 0.0, 0.25, 1.5, 2.999, 3.2], [0, 1, 2, 3])]
          == [None, 0.0, 0.25, 0.5, 0.999, None])
    check("phases(skip_ui=2) drops UI 0,1",
          lambda: phases([0.5, 1.5, 2.5], [0, 1, 2, 3], skip_ui=2) == [None, None, 0.5])

    capA = synth_capture(2048, chan=channel_step(dt, UI, kind="ramp", ramp_ui=0.4))
    mA = safe(lambda: eye_metrics(capA["v"], dt, truth_clock(capA)), {})
    check("clean ramp eye: EH_inner == 2A = 0.800V, EW_pp == 1.000UI, cross 50%",
          lambda: (near(mA["eh_inner"], 0.8, 1e-3) and near(mA["ew_pp"], 1.0, 2e-3) and near(mA["cross_pct"], 50.0, 0.5),
                   "EH_inner=%.4fV EW_pp=%.4fUI cross=%.2f%%" % (mA["eh_inner"], mA["ew_pp"], mA["cross_pct"])))
    hA = safe(lambda: eye_hist(mA["xs"], mA["vs"]), None)
    check("eye_hist: 64x24, total count == folded points (all inside +-0.6V)",
          lambda: (len(hA) == 24 and len(hA[0]) == 64 and sum(map(sum, hA)) == len(mA["xs"]),
                   "points=%d" % len(mA["xs"])))

    capR = synth_capture(4096, chan=channel_step(dt, UI, kind="ramp", ramp_ui=0.4), rj_ui=0.01, seed=11)
    mR = safe(lambda: eye_metrics(capR["v"], dt, truth_clock(capR)), {})
    check("RJ only (0.010UI rms): crossing sigma within 8%, EW = 1 - 6sigma",
          lambda: (near(mR["sx"], 0.01, 0.0008) and near(mR["ew"], 1 - 6 * mR["sx"], 1e-12),
                   "sigma_x=%.5fUI EW=%.4fUI (truth 0.94)" % (mR["sx"], mR["ew"])))

    tau = 0.28
    capI = synth_capture(2048, chan=channel_step(dt, UI, tau_ui=tau))

    def isi():
        m = eye_metrics(capI["v"], dt, truth_clock(capI), win=0.02)
        s_ui = (m["c"] + 0.5) % 1.0                 # eye 중심의 송신 경계 기준 위상
        ks = [k for k in range(SPU) if abs(k / SPU - s_ui) <= 0.01]   # 창 안의 샘플 위상 (정답 클럭 → k/16 격자)
        ref = min(analytic_inner_eh(0.4, tau, k / SPU) for k in ks)
        return near(m["eh_inner"], ref, 0.002), "EH_inner=%.4fV analytic=%.4fV (center s=%.4fUI, sample phase %s/16) EH_3sigma=%.4fV" % (
            m["eh_inner"], ref, s_ui, ks, m["eh"])
    check("exp channel tau=0.28UI ISI: inner EH == analytic worst pattern (+-2mV)", isi)

    capD = synth_capture(2048, chan=channel_step(dt, UI, kind="ramp", ramp_ui=0.5), dcd_ui=0.05)
    mD = safe(lambda: eye_metrics(capD["v"], dt, truth_clock(capD)), {})
    check("DCD 0.05UI on 0.5UI ramp: cross% = 50(1 + dcd/ramp) = 55%",
          lambda: (near(mD["cross_pct"], 55.0, 0.5), "cross=%.2f%%" % mD["cross_pct"]))
    check("DCD measured at crossing level: EW_pp stays ~1 (DCD moved to vertical)",
          lambda: (near(mD["ew_pp"], 1.0, 0.01), "EW_pp=%.4f" % mD["ew_pp"]))

    # ---- Part B: 클럭 복원 비교
    print("-- Part B: nominal vs least-squares ideal clock vs golden PLL (bw = bitrate/1667) --")
    capS = synth_capture(16384, chan=channel_step(dt, UI, kind="ramp", ramp_ui=0.4), ppm=100.0,
                         sj_ui=0.2, sj_hz=500e3, seed=21)
    eS = [t for t, _ in capS["tx_edges"]]

    def ls():
        capP = synth_capture(16384, chan=channel_step(dt, UI, kind="ramp", ramp_ui=0.4), ppm=100.0, rj_ui=0.01, seed=20)
        a, T = ideal_clock_ls(edge_times(capP["v"], dt), UI)
        ppm = (UI / T - 1) * 1e6
        a2, T2 = ideal_clock_ls(eS, UI)
        return near(ppm, 100.0, 0.5), "RJ only: %+.2fppm (injected +100) | with 500kHz SJ: %+.2fppm" % (
            ppm, (UI / T2 - 1) * 1e6)
    check("ideal_clock_ls recovers +100ppm within 0.5ppm (from waveform crossings)", ls)

    def pll_sj():
        a, T = ideal_clock_ls(eS, UI)
        clk = golden_pll(eS, T, PLL_BW, int((eS[-1] - eS[0]) / T) + 2)
        amp = sine_amp(tie(eS, clk, SPEC["skip_ui"]), 500e3) / UI
        f = 500e3
        ref = 0.2 * f / math.hypot(f, PLL_BW)
        return near(amp, ref, 0.05 * ref), "residual SJ=%.4fUI analytic |1-H|*A=%.4fUI (%.1f%%)" % (
            amp, ref, (amp / ref - 1) * 100)
    check("golden PLL residual of 500kHz SJ == A*f/sqrt(f^2+BW^2) within 5%", pll_sj)

    def pll_hf():
        a, T = ideal_clock_ls(eS, UI)
        capH = synth_capture(16384, chan=channel_step(dt, UI, kind="ramp", ramp_ui=0.4), sj_ui=0.05,
                             sj_hz=30e6, seed=22)
        eH = [t for t, _ in capH["tx_edges"]]
        clk = golden_pll(eH, UI, PLL_BW, int((eH[-1] - eH[0]) / UI) + 2)
        amp = sine_amp(tie(eH, clk, SPEC["skip_ui"]), 30e6) / UI
        ref = 0.05 * 30e6 / math.hypot(30e6, PLL_BW)
        return near(amp, ref, 0.05 * ref), "30MHz SJ passes: %.4fUI analytic %.4fUI" % (amp, ref)
    check("golden PLL passes 30MHz SJ (10x BW) almost untouched", pll_hf)

    capB = synth_capture(16384, chan=channel_step(dt, UI, tau_ui=0.28), seed=23, **COMMON)
    res = {}
    for meth in ("nominal", "ideal", "pll"):
        res[meth] = safe(lambda: eye_metrics(capB["v"], dt, recover(capB, meth), SPEC["skip_ui"], 0.2))
    safe(lambda: print("      clock     EH(3s)    EH_inner  EW(6s)   EW_pp    sigma_x   pp_x    mask_hits\n" + "\n".join(
        "      %-8s %7.1fmV %7.1fmV %7.3fUI %7.3fUI %7.4fUI %6.3fUI %6d" % (
            k, m["eh"] * 1e3, m["eh_inner"] * 1e3, m["ew"], m["ew_pp"], m["sx"], m["ppx"],
            mask_hits(m["xs"], m["vs"], SPEC["mask_ui_v"])) for k, m in res.items())))
    check("nominal clock (+100ppm x 16k UI = 1.6UI drift): eye closed (EW_pp ~ 0, EW < 0)",
          lambda: (res["nominal"]["ew_pp"] <= 0.01 and res["nominal"]["ew"] < 0,
                   "EW_pp=%.3f EW=%.3f" % (res["nominal"]["ew_pp"], res["nominal"]["ew"])))
    check("LS ideal clock: SJ 0.4UIpp stays in eye -> EW_pp < 0.55UI",
          lambda: (res["ideal"]["ew_pp"] < 0.55, "EW_pp=%.3f" % res["ideal"]["ew_pp"]))
    check("golden PLL: EW_pp > 0.75UI and >= ideal + 0.25UI",
          lambda: (res["pll"]["ew_pp"] > 0.75 and res["pll"]["ew_pp"] >= res["ideal"]["ew_pp"] + 0.25,
                   "pll=%.3f ideal=%.3f" % (res["pll"]["ew_pp"], res["ideal"]["ew_pp"])))
    check("horizontal smear leaks into the center window: EH(pll) > EH(ideal) + 40mV",
          lambda: (res["pll"]["eh"] > res["ideal"]["eh"] + 0.040,
                   "pll=%.1fmV ideal=%.1fmV" % (res["pll"]["eh"] * 1e3, res["ideal"]["eh"] * 1e3)))

    # ---- Part C
    print("-- Part C: VALBRD-S rev A J7 lanes, golden-PLL eye + polygon mask --")
    check("point_in_poly: hexagon center in, (0.31,0) out, (0,0.16) out",
          lambda: point_in_poly(0, 0, SPEC["mask_ui_v"]) and not point_in_poly(0.31, 0, SPEC["mask_ui_v"])
          and not point_in_poly(0, 0.16, SPEC["mask_ui_v"]))
    caps = {n: lane_capture(n) for n in LANES}
    rep = {}
    for n, cap in caps.items():
        rep[n] = safe(lambda: lane_report(n, cap))
        if rep[n]:
            print("      " + rep[n][0] + "   (" + LANES[n]["note"] + ")")
    check("TX0 -> PASS", lambda: rep["TX0"][1] == "PASS")
    check("TX1 (echo) -> FAIL on EH, EW, mask (cross% OK)",
          lambda: ([n for n, _, _, ok in rep["TX1"][2] if not ok] == ["EH", "EW", "mask"],
                   "bad=%s" % [n for n, _, _, ok in rep["TX1"][2] if not ok]))
    check("TX2 (DCD) -> FAIL on cross% only",
          lambda: ([n for n, _, _, ok in rep["TX2"][2] if not ok] == ["cross%"],
                   "bad=%s" % [n for n, _, _, ok in rep["TX2"][2] if not ok]))
    r0i = safe(lambda: lane_report("TX0", caps["TX0"], "ideal"))
    if r0i:
        print("      " + r0i[0] + "   (같은 TX0 캡처, 이상 클럭으로 접으면)")
    check("TX0 folded with ideal clock -> false FAIL (SJ the receiver CDR would track)",
          lambda: r0i[1] == "FAIL")

    el = time.perf_counter() - t_begin
    print("%d/%d passed  (%.1f s)" % (g_pass, g_total, el))
    return g_pass == g_total


def main():
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main())
