#!/usr/bin/env python3
"""HWLAB-M01 전송선로 반사: Bounce Diagram·TDR 해석·종단

손실 없는 전송선로를 반사계수 ρ=(ZL−Z0)/(ZL+Z0) 하나로 다룬다.
  Part A  bounce(lattice) 다이어그램(해석) + 선로를 칸(cell)으로 나눈 격자 시뮬레이터 → 스텝 응답
          (15 Ω 드라이버 + 50 Ω 선로 + 열린 끝: 53.8 % overshoot, 3.3 V 로 수렴)
  Part B  TDR 해석기: V→Z 변환, 거리 = v·t/2 (v = c/√εeff), 불연속 이벤트 찾기.
          순진한 Z(t)가 거짓말하는 곳 — 다중 반사(→ layer peeling), 상승시간 해상도, εeff 가정
  Part C  VALBRD-A FPGA_CLK_50M: golden/의심 보드 TDR 비교 리포트 + 직렬 종단 R sweep(overshoot 최소점)
          + 직렬/병렬/Thevenin/AC 종단의 전력·지연 trade-off 표

  python3 -I solution.py          # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --tables # Part C 의 sweep 표와 종단 비교 표만 출력

Python 3.9+, 표준 라이브러리만 (math, os, sys).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
C0 = 299792458.0                     # m/s


def v_prop(eps_eff):
    """선로 위 전파 속도 v = c/√εeff  (m/s)."""
    return C0 / math.sqrt(eps_eff)


# ---------------------------------------------------------------- 주어짐: 선로 끝(수신단) 회로
class Load:
    """선로 끝에 달린 회로. 선로는 '2·f 를 Z0 뒤에 둔 Thevenin 소스'로 보인다 (f = 막 도착한 순방향파).

    shunts : [(R, Vterm)]  — 병렬 종단(R, 0 V), Thevenin 종단 = 풀업(R1, VDD) + 풀다운(R2, 0 V)
    cin    : 수신 핀 입력 C [F]           — 사다리꼴(trapezoidal) companion model
    ac     : (R, C) 직렬 RC 를 GND 로      — AC 종단
    아무것도 없으면 열린 끝(open): V = 2f, ρ = +1.
    """

    def __init__(self, shunts=(), cin=0.0, ac=None):
        self.shunts = list(shunts)
        self.cin = cin
        self.ac = ac
        self.reset(1.0)

    def reset(self, dt):
        self.dt = dt
        self.v = 0.0          # 노드 전압 (= Cin 전압)
        self.icin = 0.0
        self.vc = 0.0         # AC 종단 C 전압
        self.iac = 0.0
        self.iac_hist = []

    def solve(self, f, z0):
        """도착한 순방향파 f → 이번 시각의 노드 전압 V. 반사파는 V − f."""
        g, rhs = 1.0 / z0, 2.0 * f / z0
        for r, vt in self.shunts:
            g += 1.0 / r
            rhs += vt / r
        if self.cin > 0:
            gc = 2.0 * self.cin / self.dt
            ieq = gc * self.v + self.icin
            g += gc
            rhs += ieq
        if self.ac:
            r, c = self.ac
            gc2 = 2.0 * c / self.dt
            ieq2 = gc2 * self.vc + self.iac
            gb, veq = 1.0 / (r + 1.0 / gc2), ieq2 / gc2
            g += gb
            rhs += gb * veq
        v = rhs / g
        if self.cin > 0:
            self.icin = gc * v - ieq
        if self.ac:
            self.iac = gb * (v - veq)
            self.vc = (self.iac + ieq2) / gc2
        self.iac_hist.append(self.iac)
        self.v = v
        return v


def dc_level(vs, rsrc, load):
    """정상상태(C 는 열림) 수신단 전압: 소스 vs/rsrc 와 shunt 들의 KCL."""
    g, rhs = 1.0 / rsrc, vs / rsrc
    for r, vt in load.shunts:
        g += 1.0 / r
        rhs += vt / r
    return rhs / g


def step_fn(v1, t0=0.0):
    return lambda t: v1 if t >= t0 else 0.0


def ramp_fn(v1, t0, tr):
    """t0 에서 시작해 tr 동안 0→v1 직선 상승 (0–100 %)."""
    return lambda t: 0.0 if t <= t0 else (v1 if t >= t0 + tr else v1 * (t - t0) / tr)


def clock_fn(vdd, period, t_first, tr, duty=0.5):
    """사다리꼴 클럭: t_first 에서 첫 상승 시작, 에지 0–100 % = tr."""
    hi = duty * period

    def f(t):
        if t < t_first:
            return 0.0
        x = (t - t_first) % period
        if x < tr:
            return vdd * x / tr
        if x < hi:
            return vdd
        if x < hi + tr:
            return vdd * (1.0 - (x - hi) / tr)
        return 0.0
    return f


def gauss_filter(x, tr, dt):
    """가우시안 저역통과(대칭, 지연 없음): 10–90 % 상승시간 tr = 2·1.2816·σ. 끝은 끝값으로 패딩."""
    s = tr / 2.5631031311 / dt
    m = int(math.ceil(4 * s))
    ker = [math.exp(-0.5 * (j / s) ** 2) for j in range(-m, m + 1)]
    tot = sum(ker)
    ker = [k / tot for k in ker]
    n = len(x)
    return [sum(ker[j + m] * x[min(n - 1, max(0, i + j))] for j in range(-m, m + 1)) for i in range(n)]


def cross_time(t, v, level, i_from=0, rising=True):
    """i_from 이후 처음으로 level 을 (rising 이면 위로, 아니면 아래로) 지나는 시각 — 선형 보간 (HWLAB-B04)."""
    for i in range(max(i_from, 0), len(v) - 1):
        a, b = v[i], v[i + 1]
        if (rising and a < level <= b) or (not rising and a > level >= b):
            return t[i] + (level - a) / (b - a) * (t[i + 1] - t[i])
    return None


def load_csv(path):
    t, v = [], []
    with open(path, encoding="ascii") as f:
        for line in f:
            s = line.strip()
            if not s or s[0] == "#":
                continue
            a, b = s.split(",")[:2]
            try:
                t.append(float(a))
                v.append(float(b))
            except ValueError:
                continue
    return t, v


# ---------------------------------------------------------------- Part A: ρ, bounce diagram, 격자 시뮬레이터
def rho(zl, z0):
    """반사계수 (ZL−Z0)/(ZL+Z0). ZL = inf(열림) → +1, 0(단락) → −1."""
    # TODO: 열림(math.isinf(zl))은 +1, 그 외 (zl − z0)/(zl + z0)
    return 0.0


def bounce_diagram(vs, rs, z0, rl, td, t_end):
    """균일 선로 1개(편도 지연 td), 저항 소스 rs·저항 부하 rl, t=0 에 vs 계단.

    반환: 시간순 이벤트 dict(t, end='src'|'load', wave=도착(또는 출발)한 파, refl=반사된 파, v=그 끝의 전압)
      t=0 : end='src', wave=launch = vs·z0/(rs+z0), refl=0, v=launch
      t=td, 3td, ... : 'load' 에 wave 도착 → refl = ρL·wave, v_load += wave + refl
      t=2td, 4td, ...: 'src'  에 wave 도착 → refl = ρS·wave, v_src  += wave + refl
    """
    # TODO ① ρS = rho(rs, z0), ρL = rho(rl, z0), launch w = vs·z0/(rs+z0) → t=0 'src' 이벤트
    # TODO ② k = 1, 2, ... (k·td ≤ t_end): 홀수 k 는 'load', 짝수 k 는 'src' 에 w 가 도착
    #        refl = ρ·w, 그 끝의 전압 += w + refl, 이벤트 추가, 다음 w = refl
    return []


def simulate(z, dt, vs_fn, rs, load, n_steps):
    """선로를 칸으로 나눈 격자(lattice) 시뮬레이터. z[i] = i 번째 칸의 임피던스, 칸마다 편도 지연 dt.

    상태: fwd[i] = i 칸에서 오른쪽으로 가는 파(다음 스텝에 i 칸 오른쪽 끝 도착), bwd[i] = 왼쪽으로 가는 파.
    매 스텝 k (t = k·dt):
      소스 노드 : g = bwd[0] 도착. V0 = (vs/rs + 2g/z0)/(1/rs + 1/z0), 새 fwd[0] = V0 − g
      접합 i|i+1: f = fwd[i], b = bwd[i+1], ρ = rho(z[i+1], z[i])
                  새 fwd[i+1] = (1+ρ)f − ρb,   새 bwd[i] = ρf + (1−ρ)b
      부하 노드 : f = fwd[-1] 도착. V = load.solve(f, z[-1]), 새 bwd[-1] = V − f
    반환 (v_src, v_load) — 각 스텝의 소스 노드·부하 노드 전압. 선로 지연 = len(z)·dt.
    """
    load.reset(dt)
    # TODO: fwd/bwd 를 0 으로 시작해 n_steps 번 — 소스 노드 → 모든 접합 → 부하 노드 순서로 새 fwd/bwd 를 만든다
    #       (docstring 의 식 그대로, 접합 ρ 는 루프 밖에서 미리 계산). 매 스텝 V0 와 부하 전압을 기록.
    return [0.0] * n_steps, [0.0] * n_steps


# ---------------------------------------------------------------- Part B: TDR 해석
def tdr_capture(z_cells, dt, vinc, load, n_steps, tr=None, pre=40):
    """정합(50 Ω) TDR 소스로 z_cells 를 찍는다. 반환 (t, v): 소스 노드 전압, t=0 은 입사 스텝의 50 % 시각.
    샘플 pre−1 → pre 사이에서 뛰는 계단은 (걸러 보면) 50 % 가 pre−0.5 칸에 오므로 t = (k − pre + 0.5)·dt.
    tr 이 있으면 가우시안으로 걸러 상승시간을 준다 (선형이라 출력에 걸러도 같다)."""
    vs, _ = simulate(z_cells, dt, step_fn(2.0 * vinc, pre * dt), z_cells[0], load, n_steps + pre)
    if tr:
        vs = gauss_filter(vs, tr, dt)
    return [(k - pre + 0.5) * dt for k in range(len(vs))], vs


def z_profile(v, vinc, z0=50.0):
    """TDR 전압 → '겉보기' 임피던스: ρ = v/vinc − 1, Z = z0·(1+ρ)/(1−ρ). v ≥ 2·vinc → inf, v ≤ 0 → 0."""
    # TODO: a = x/vinc → a ≥ 2 면 inf, a ≤ 0 이면 0.0, 아니면 z0·a/(2 − a)
    return [z0] * len(v)


def layer_peel(v_rt, vinc, z_ref):
    """다중 반사를 벗겨 내는 layer peeling (Goupillaud: 모든 칸의 왕복 지연이 같은 이상 스텝 TDR).

    v_rt[k] = 왕복 k 칸 시각의 TDR 전압 (simulate 의 v_src[2k]). 소스는 정합(z_ref)이라고 가정.
    h[k] = (v_rt[k] − v_rt[k−1])/vinc  (h[0] = v_rt[0]/vinc − 1)  — 반사 임펄스 응답
    D = [1, 0, 0, ...], U = h.  칸 j = 1, 2, ... 마다:
       U = U[1:]  (한 칸 내려가면 위로 가는 파는 왕복 1 만큼 일찍 보인다), D 도 같은 길이로 자름
       ρ = U[0]/D[0],   Z_j = Z_{j−1}·(1+ρ)/(1−ρ)
       접합 아래쪽 파: D' = (D − ρU)/(1−ρ),  U' = (U − ρD)/(1−ρ)
    반환: [z_ref, Z_1, Z_2, ...] — 칸 인덱스별 임피던스.
    """
    # TODO: h 를 만들고 D = [1, 0, ...], U = h 에서 시작 → U 가 2개 이상 남아 있는 동안 docstring 의 네 줄을 반복
    return [z_ref]


def tdr_distance(t_rt, eps_eff):
    """왕복 시간 → 기준면에서의 거리 = v·t/2."""
    # TODO: v_prop(eps_eff) · t_rt / 2
    return 0.0


def estimate_vinc(t, v, t_lo=-0.30e-9, t_hi=-0.05e-9):
    """기준면 앞(케이블 안, 50 Ω) 평탄 구간 평균 = 입사 스텝 Vinc."""
    xs = [x for ti, x in zip(t, v) if t_lo <= ti <= t_hi]
    return sum(xs) / len(xs)


def find_events(t, v, vinc, eps_eff, tr, z0=50.0, thr=4.0, t_start=-0.2e-9):
    """TDR 파형에서 임피던스 불연속 이벤트를 찾는다.

    |Z(t) − z0| > thr 인 연속 구간(run)마다 하나:
      'open'    : 극값 Z > 10·z0.  바로 앞(간격 < 2·tr)에 저Z run 이 붙어 있으면 둘을 합쳐 'end-C'
                  (끝의 수신 핀 C: 처음엔 단락처럼, 나중엔 열림처럼 보인다).
                  위치 = 'end-C' 는 앞 dip 의 반-편차 교차, 'open' 은 1.5·vinc 교차.
                  'end-C' 는 cin = ∫(2·vinc − v)dt / (2·vinc·z0)  (위치부터 기록 끝까지) 도 준다.
      'segment' : run 길이(시간) ≥ 2·tr → 평탄부(run 가운데 절반의 중앙값) 전압으로 Z.
                  시작/끝 = vinc 와 평탄 전압의 중간 레벨 교차, 길이 = v·(t_end − t_start)/2.
      'short'   : run 이 짧음(< 2·tr) → 극값 시각 = 중심 위치, Z 는 '겉보기' 값 (상승시간 때문에 실제보다 얕다).
    반환: dict(kind, t, x(거리 m), z, length(m, segment), z_reliable(bool), cin(F, end-C)) 목록.
    """
    zp = z_profile(v, vinc, z0)
    out_band = [ti >= t_start and abs(z - z0) > thr for ti, z in zip(t, zp)]
    runs = []
    i, n = 0, len(v)
    while i < n:
        if out_band[i]:
            j = i
            while j + 1 < n and out_band[j + 1]:
                j += 1
            if j > i:                                    # 1 샘플짜리 잡음 스파이크는 버린다
                runs.append((i, j))
            i = j + 1
        else:
            i += 1
    events = []
    # TODO: runs 의 (i0, i1) 마다 docstring 규칙으로 'open' / 'end-C' / 'segment' / 'short' 이벤트를 만든다.
    #   극값 kext = |Z − z0| 가 가장 큰 샘플(inf 가 가장 큼). 교차 시각은 cross_time(t, v, level, i0 − 20, rising=...)
    #   'end-C' 는 직전 이벤트(저Z, 간격 < 2·tr)를 pop 해서 합친다. 각 dict 에 '_i0', '_i1' 도 넣어 둔다.
    return events


# ---------------------------------------------------------------- Part C: 보드 판정 + 종단 설계
NET = dict(name="FPGA_CLK_50M", vdd=3.3, rdrv=15.0, z0=50.0, length=0.15, eps_eff=4.0,
           cin=4e-12, tr_drv=0.5e-9, f_clk=50e6, cell=2.5e-3, z_tol=0.10)


def net_line(net):
    """(z 칸 목록, dt) — net 의 선로를 cell 길이로 자른다."""
    n = int(round(net["length"] / net["cell"]))
    return [net["z0"]] * n, net["cell"] / v_prop(net["eps_eff"])


def compare_tdr(golden, dut, z0=50.0, z_tol=0.10, x_tol=3e-3):
    """golden 이벤트 목록과 DUT 이벤트 목록 비교.

    DUT 이벤트 중 golden 에 '같은 kind 이고 |Δx| ≤ x_tol' 인 짝이 없으면 새 이벤트(anomaly).
    segment 는 |Z − z0|/z0 > z_tol 이면 임피던스 스펙 위반.
    반환 (anomalies 목록, verdict 'PASS'|'FAIL').  anomaly = 짝 없는 이벤트 또는 스펙 위반 segment.
    """
    # TODO: dut 의 각 이벤트 — golden 에 짝(같은 kind, |Δx| ≤ x_tol)이 없거나, segment 인데 Z 가 z_tol 밖이면 anomaly
    return [], "TODO"


def series_sweep(net, r_values, t_sim=14e-9):
    """직렬 종단 R(드라이버 바로 뒤) sweep. 수신단(열림 + Cin)에서 스텝 응답을 재서
    행 dict(r, os=overshoot %, tpd=VDD/2 교차 지연 s, tsettle=±5 % 밴드에 들어와 머무는 시각 s) 목록.
    overshoot = (max − Vfinal)/Vfinal·100, Vfinal = dc_level (열린 끝이라 VDD). 지연은 소스 램프 50 % 기준."""
    z, dt = net_line(net)
    n_steps = int(t_sim / dt)
    t0 = 0.1e-9
    rows = []
    for r in r_values:
        load = Load(cin=net["cin"])
        rsum = net["rdrv"] + r
        _, vl = simulate(z, dt, ramp_fn(net["vdd"], t0, net["tr_drv"]), rsum, load, n_steps)
        vf = dc_level(net["vdd"], rsum, load)
        tt = [k * dt for k in range(n_steps)]
        tc = cross_time(tt, vl, 0.5 * vf)
        last_out = max((k for k in range(n_steps) if abs(vl[k] - vf) > 0.05 * vf), default=0)
        rows.append(dict(r=r, os=max(0.0, (max(vl) - vf) / vf * 100.0),
                         tpd=tc - (t0 + 0.5 * net["tr_drv"]), tsettle=tt[last_out] - t0))
    return rows


def os_knee(rows, floor_tol=0.5):
    """overshoot 가 바닥(최솟값 + floor_tol %p)에 처음 닿는 가장 작은 R."""
    # TODO: floor = 모든 행 os 의 최솟값 → os ≤ floor + floor_tol 인 행들 중 가장 작은 r
    return None


def nearest_e24_below(x):
    e24 = [1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2,
           6.8, 7.5, 8.2, 9.1]
    vals = [m * 10 ** d for d in range(0, 4) for m in e24]
    return max(x_ for x_ in vals if x_ <= x + 1e-9)


SCHEMES = [
    ("none", "-", 0.0, dict()),
    ("series", "Rs=33R @U12", 33.0, dict()),
    ("parallel", "50R->GND @U1", 0.0, dict(shunts=[(50.0, 0.0)])),
    ("thevenin", "100R->3V3 + 100R->GND @U1", 0.0, dict(shunts=[(100.0, 3.3), (100.0, 0.0)])),
    ("ac", "50R+100pF->GND @U1", 0.0, dict(ac=(50.0, 100e-12))),
]


def clock_metrics(net, rs_series, load_kw, periods=2.5):
    """50 MHz 클럭으로 한 종단 방식을 시뮬레이션해 측정 (두 번째 상승부터 한 주기를 잰다).

    반환 dict:
      vhi, vlo : 하이/로 구간 마지막 1 ns 평균 (수신단)
      os       : (하이 구간 max − vhi)/(vhi − vlo)·100
      tpd      : 소스 램프 50 % → 수신단 VDD/2 상승 교차 (s)
      p_supply : 측정 주기 평균 전원 전력 (W) = mean(Vs·(Vs − Vsrc)/Rsum) + Σ풀업 mean(Vt·(Vt − V)/R)
      p_term   : 종단 저항에서 소모 (W) = Σ mean((V − Vt)²/R) + mean(Rac·iac²) + mean(Rs_series·i_src²)
      idrv     : DC 드라이버 전류 최댓값 (A) = max((VDD − Vhi_dc)/Rsum, Vlo_dc/Rsum)
    """
    z, dt = net_line(net)
    per = 1.0 / net["f_clk"]
    t_first = 5e-9
    n_steps = int((t_first + periods * per) / dt)
    vs_fn = clock_fn(net["vdd"], per, t_first, net["tr_drv"])
    load = Load(cin=net["cin"], **load_kw)
    rsum = net["rdrv"] + rs_series
    vsrc, vl = simulate(z, dt, vs_fn, rsum, load, n_steps)
    tt = [k * dt for k in range(n_steps)]
    ta = t_first + per                                  # 측정할 상승 에지 시작
    th_hi, tb = ta + 0.5 * per, ta + per
    idx = [k for k in range(n_steps) if ta <= tt[k] < tb]
    hi_win = [vl[k] for k in idx if th_hi - 1e-9 <= tt[k] < th_hi]
    lo_win = [vl[k] for k in idx if tb - 1e-9 <= tt[k] < tb]
    vhi, vlo = sum(hi_win) / len(hi_win), sum(lo_win) / len(lo_win)
    vmax = max(vl[k] for k in idx if tt[k] < th_hi)
    tc = cross_time(tt, vl, 0.5 * net["vdd"], idx[0], rising=True)
    ps = 0.0
    pt = 0.0
    # TODO ① idx 의 각 샘플 k: vsk = vs_fn(tt[k]) → 소스 전력 vsk·(vsk − vsrc[k])/rsum, 풀업(vt > 0)의 전원 전력
    #        vt·(vt − V)/R, 종단 소모 Σ(V − vt)²/R + Rac·iac_hist[k]² + rs_series·i_src²  → 평균 내어 ps, pt
    # TODO ② dc_level(VDD), dc_level(0) 로 하이/로 DC 레벨 → 드라이버 DC 전류 최댓값 idrv
    idrv = 0.0
    return dict(vhi=vhi, vlo=vlo, os=(vmax - vhi) / (vhi - vlo) * 100.0,
                tpd=tc - (ta + 0.5 * net["tr_drv"]), p_supply=ps, p_term=pt, idrv=idrv)


def termination_table(net, schemes=SCHEMES):
    """SCHEMES 각각 clock_metrics → (name, parts, metrics) 목록."""
    return [(name, parts, clock_metrics(net, rs, kw)) for name, parts, rs, kw in schemes]


def tdr_report(sn, events, anomalies, verdict, golden_sn):
    """한 줄짜리 행동 가능한 리포트 (+ 이벤트 줄들)."""
    lines = ["%s %s TDR -> %s" % (sn, NET["name"], verdict)]
    for e in events:
        if e["kind"] == "segment":
            d = "Z=%.1fohm over %.2fcm" % (e["z"], e["length"] * 100)
        elif e["kind"] == "short":
            d = "Z_app=%.1fohm (short feature: depth limited by tr)" % e["z"]
        elif e["kind"] == "end-C":
            d = "line end, Cin~%.2fpF" % (e["cin"] * 1e12)
        else:
            d = "open"
        lines.append("  %-8s @ %6.2fcm  %s%s" % (e["kind"], e["x"] * 100, d, "   <-- NEW" if e in anomalies else ""))
    for e in anomalies:
        if e["kind"] == "segment":
            lines.append("  FAIL net=%s Z=%.1fohm (spec %.0f+-%.0f%%) @ %.2fcm len %.2fcm, not in golden %s "
                         "-> plane void/split or neck-down: check L2/L4 artwork + X-section there"
                         % (NET["name"], e["z"], NET["z0"], NET["z_tol"] * 100, e["x"] * 100,
                            e["length"] * 100, golden_sn))
        else:
            lines.append("  FAIL net=%s new %s event @ %.2fcm (not in golden %s)"
                         % (NET["name"], e["kind"], e["x"] * 100, golden_sn))
    return "\n".join(lines)


def z0f(x):
    """표 출력용: |x| < 5 mV 면 0.0 ('-0.00' 방지)."""
    return 0.0 if abs(x) < 5e-3 else x


def print_tables(net=NET):
    rows = series_sweep(net, list(range(0, 61)))
    print("series R sweep @%s (Rdrv=%.0fohm, Z0=%.0fohm, %.0fcm, Cin=%.0fpF, edge %.1fns):"
          % (net["name"], net["rdrv"], net["z0"], net["length"] * 100, net["cin"] * 1e12, net["tr_drv"] * 1e9))
    print("   R    OS%     tpd(ns)  tsettle5%(ns)")
    for r in rows:
        if r["r"] in (0, 10, 20, 25, 30, 33, 34, 35, 36, 39, 47, 60):
            print("  %3.0f  %6.2f   %6.3f   %6.2f" % (r["r"], r["os"], r["tpd"] * 1e9, r["tsettle"] * 1e9))
    knee = os_knee(rows)
    print("  knee (OS floor reached) R=%.0fohm (Z0-Rdrv=%.0f), E24 below -> %.0fohm"
          % (knee, net["z0"] - net["rdrv"], nearest_e24_below(knee)))
    print("termination trade-off @50MHz clock, 3.3V LVCMOS, measured on 2nd period:")
    print("  scheme    parts                         Vlo..Vhi(V)    OS%   tpd(ns)  P_supply(mW) P_term(mW) Idrv_dc(mA)")
    tab = termination_table(net)
    for name, parts, m in tab:
        print("  %-9s %-29s %4.2f..%4.2f  %6.1f   %5.3f   %8.1f    %7.1f    %6.1f"
              % (name, parts, z0f(m["vlo"]), m["vhi"], m["os"], m["tpd"] * 1e9, m["p_supply"] * 1e3,
                 m["p_term"] * 1e3, m["idrv"] * 1e3))
    print("AC termination C sweep (R=50ohm): RC vs 2*td = %.2fns" % (2 * net["length"] / v_prop(net["eps_eff"]) * 1e9))
    print("   C(pF)  RC(ns)  Vlo..Vhi(V)   OS%   P_supply(mW)")
    for c in (22e-12, 47e-12, 100e-12, 220e-12):
        m = clock_metrics(net, 0.0, dict(ac=(50.0, c)))
        print("  %5.0f   %5.2f   %4.2f..%4.2f  %5.1f   %6.1f" % (c * 1e12, 50 * c * 1e9, z0f(m["vlo"]), m["vhi"], m["os"],
                                                         m["p_supply"] * 1e3))
    return rows, tab


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
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def run_tests():
    # ---- Part A
    print("-- Part A: rho / bounce diagram / lattice simulator --")
    check("rho: open=+1, short=-1, 75 on 50 = +0.2, 15 on 50 = -0.5385",
          lambda: (near(rho(math.inf, 50), 1, 1e-12) and near(rho(0, 50), -1, 1e-12)
                   and near(rho(75, 50), 0.2, 1e-12) and near(rho(15, 50), -35 / 65, 1e-12)))
    ev = safe(lambda: bounce_diagram(3.3, 15.0, 50.0, math.inf, 1e-9, 9e-9), [])
    vl_b = [e["v"] for e in ev if e["end"] == "load"]
    check("bounce 15R drv/50R/open: load = 5.077, 2.343, 3.815, 3.023, 3.449 V",
          lambda: (len(vl_b) >= 5 and all(near(a, b, 1e-3) for a, b in
                                         zip(vl_b, [5.0769, 2.3432, 3.8152, 3.0227, 3.4494])),
                   "load=%s" % ["%.4f" % x for x in vl_b[:5]]))
    check("bounce: first overshoot 53.8% of 3.3V",
          lambda: (near((vl_b[0] - 3.3) / 3.3 * 100, 53.85, 0.05), "%.2f%%" % ((vl_b[0] - 3.3) / 3.3 * 100)))
    ev2 = safe(lambda: bounce_diagram(2.0, 25.0, 50.0, 100.0, 1e-9, 61e-9), [])
    check("bounce Rs=25/RL=100 converges to vs*RL/(Rs+RL) = 1.6 V",
          lambda: (near([e["v"] for e in ev2 if e["end"] == "load"][-1], 1.6, 1e-6),
                   "v_load(61ns)=%.7f" % [e["v"] for e in ev2 if e["end"] == "load"][-1]))
    N, td = 20, 1e-9
    simA = safe(lambda: simulate([50.0] * N, td / N, step_fn(3.3), 15.0, Load(), 9 * N + 1), ([], []))

    def sim_vs_lattice():
        got = [simA[1][k * N] for k in (1, 3, 5, 7, 9)]
        return (len(vl_b) >= 5 and all(near(a, b, 1e-9) for a, b in zip(got, vl_b)),
                "sim=%s" % ["%.4f" % x for x in got])
    check("simulate(20 cells) == bounce lattice at t = 1,3,5,7,9 ns", sim_vs_lattice)
    simM = safe(lambda: simulate([50.0] * N, td / N, step_fn(3.3), 50.0, Load(), 3 * N + 1), ([], []))
    check("source-matched: src node 'shelf' Vs/2 for 2td, load jumps to Vs at td",
          lambda: (near(simM[0][N], 1.65, 1e-12) and near(simM[0][2 * N], 3.3, 1e-12)
                   and near(simM[1][N - 1], 0.0, 1e-12) and near(simM[1][N], 3.3, 1e-12),
                   "src(td)=%.3f src(2td)=%.3f load(td)=%.3f" % (simM[0][N], simM[0][2 * N], simM[1][N])))
    cC, dtc = 4e-12, 5e-12

    def cap_load():
        _, vl = simulate([50.0] * 40, dtc, step_fn(3.3), 50.0, Load(cin=cC), 40 + 400)
        tau = 50.0 * cC
        # 사다리꼴 적분은 샘플 사이를 직선으로 본다 → 계단이 반 샘플 일찍 시작한 것과 같다 (t = (k+0.5)·dt)
        err = max(abs(vl[40 + k] - 3.3 * (1 - math.exp(-(k + 0.5) * dtc / tau))) for k in range(0, 400))
        return err < 1e-3, "max err vs 3.3(1-exp(-t/Z0C)) = %.3fmV" % (err * 1e3)
    check("cap load, matched source: v_load = Vs(1-exp(-(t-td)/(Z0*C))) within 1mV", cap_load)

    # ---- Part B
    print("-- Part B: TDR inversion (naive Z(t) vs layer peeling, rise-time resolution, eps_eff) --")
    eps = 4.0
    dt1 = 1e-3 / v_prop(eps)                               # 1 mm 칸
    zt = [50.0] * 20 + [30.0] * 30 + [75.0] * 30 + [50.0] * 30
    tP, vP = safe(lambda: tdr_capture(zt, dt1, 0.25, Load(shunts=[(50.0, 0.0)]), 2 * len(zt) + 2, pre=0),
                  ([], []))
    vrt = vP[0::2]
    zn = safe(lambda: z_profile(vrt, 0.25), [])
    zl = safe(lambda: layer_peel(vrt, 0.25, 50.0), [])

    def med(xs):
        s = sorted(xs)
        return s[len(s) // 2]
    check("z_profile: first segment reads 30 ohm exactly (k=22..48)",
          lambda: (all(near(zn[k], 30.0, 1e-9) for k in range(22, 49)), "Z=%.4f" % zn[30]))
    check("naive Z(t) on the 75-ohm segment reads 67.9 (masked by the 30-ohm step), not 75",
          lambda: (near(med(zn[52:78]), 67.89, 0.05), "naive=%.2f vs truth 75" % med(zn[52:78])))
    check("naive Z(t) on the last 50-ohm segment is wrong (ghost/multiple reflections)",
          lambda: (abs(med(zn[82:108]) - 50.0) > 3.0, "naive=%.2f vs truth 50" % med(zn[82:108])))
    check("layer_peel recovers every cell (30/75/50) within 0.01 ohm",
          lambda: (len(zl) >= len(zt) and max(abs(a - b) for a, b in zip(zl, zt)) < 0.01,
                   "max err=%.2e ohm, Z[30]=%.3f Z[60]=%.3f Z[95]=%.3f"
                   % (max(abs(a - b) for a, b in zip(zl, zt)), zl[30], zl[60], zl[95])))

    dt5 = 0.5e-3 / v_prop(eps)
    zv = [50.0] * 40 + [35.0] * 6 + [50.0] * 60                # 3 mm 35 Ω 패드
    tv0, vv0 = safe(lambda: tdr_capture(zv, dt5, 0.25, Load(shunts=[(50.0, 0.0)]), 260), ([], []))

    def apparent(tr):
        zz = z_profile(gauss_filter(vv0, tr, dt5), 0.25)
        return min(z for t_, z in zip(tv0, zz) if t_ > 40 * dt5)      # 입사 에지(t≈0) 이후만
    rho_v = rho(35.0, 50.0)
    sig = lambda tr: tr / 2.5631031311                       # noqa: E731
    pred = lambda tr: rho_v * math.erf(12 * dt5 / (2 * math.sqrt(2) * sig(tr)))  # noqa: E731

    def res_fast():
        zm = apparent(20e-12)
        return near(zm, 35.0, 1.5), "tr=20ps: min Z=%.2f (truth 35)" % zm

    def res_slow():
        zm = apparent(100e-12)
        return 42.0 < zm < 46.0, "tr=100ps: min Z=%.2f (truth 35)" % zm

    def res_model():
        out = []
        for tr in (20e-12, 35e-12, 60e-12, 100e-12, 150e-12):
            zm = apparent(tr)
            r_meas = (zm - 50.0) / (zm + 50.0)
            out.append((tr, r_meas, pred(tr)))
        ok = all(abs(a - b) < 0.01 and a < -0.01 for _, a, b in out)
        return ok, " ".join("%dps:%.3f/%.3f" % (tr * 1e12, a, b) for tr, a, b in out)
    check("short 3mm 35-ohm pad: tr=20ps sees ~35 ohm (+-1.5)", res_fast)
    check("same pad with tr=100ps reads 42..46 ohm (too shallow)", res_slow)
    check("apparent rho = rho*erf(T_rt/(2*sqrt2*sigma)) within 0.01 (meas/model)", res_model)

    zc = [50.0] * 40 + [50.0] * 200                            # 10.00 cm 쿠폰, 끝 열림
    tc_, vc_ = safe(lambda: tdr_capture(zc, dt5, 0.25, Load(), 520, tr=35e-12), ([], []))

    def coupon():
        tt = [x - 80 * dt5 for x in tc_]                     # 기준면 = 2 cm 케이블 끝 (왕복 80 칸)
        evs = find_events(tt, vc_, 0.25, eps, 35e-12)
        e = [x for x in evs if x["kind"] == "open"][0]
        eps_cal = (C0 * e["t"] / (2 * 0.100)) ** 2
        x3 = tdr_distance(e["t"], 3.0)
        return (near(e["x"], 0.100, 0.5e-3) and near(eps_cal, 4.0, 0.02) and x3 > 0.114,
                "open @%.2fcm, eps_cal=%.3f, if eps=3.0 assumed -> %.2fcm" % (e["x"] * 100, eps_cal, x3 * 100))
    check("10cm coupon: open @10.00cm, eps_eff calibrated 4.00, wrong eps 3.0 -> +15%", coupon)

    # ---- Part C
    print("-- Part C: VALBRD-A FPGA_CLK_50M: TDR golden vs suspect, series-R sweep, termination table --")
    caps = {sn: load_csv(os.path.join(HERE, "%s_fpga_clk_tdr.csv" % sn)) for sn in ("SN0041", "SN0042")}
    evs = {}
    for sn, (t, v) in caps.items():
        vi = estimate_vinc(t, v)
        evs[sn] = safe(lambda: find_events(t, v, vi, 4.0, 35e-12), [])
    check("vinc estimated 0.250 V +-1 mV from the cable plateau",
          lambda: (near(estimate_vinc(*caps["SN0041"]), 0.25, 1e-3), "%.4f V" % estimate_vinc(*caps["SN0041"])))

    def golden_events():
        k = [e["kind"] for e in evs["SN0041"]]
        via = evs["SN0041"][0]
        end = evs["SN0041"][-1]
        return (k == ["short", "end-C"] and near(via["x"], 0.0605, 1e-3) and near(end["x"], 0.150, 1e-3)
                and near(end["cin"], 4e-12, 0.4e-12),
                "kinds=%s via@%.2fcm Zapp=%.1f end@%.2fcm Cin=%.2fpF"
                % (k, via["x"] * 100, via["z"], end["x"] * 100, end["cin"] * 1e12))
    check("SN0041 golden: via (short) @6.05cm, line end @15.0cm with Cin 4pF +-10%", golden_events)

    def defect():
        segs = [e for e in evs["SN0042"] if e["kind"] == "segment"]
        e = segs[0]
        return (len(segs) == 1 and near(e["z"], 68.0, 1.0) and near(e["x"], 0.118, 2e-3)
                and near(e["length"], 0.015, 2e-3),
                "Z=%.2f @%.2fcm len %.2fcm (injected 68 @11.80 len 1.50)" % (e["z"], e["x"] * 100, e["length"] * 100))
    check("SN0042: one segment found, Z=68+-1 ohm @11.8+-0.2cm, length 1.5+-0.2cm", defect)
    an = safe(lambda: compare_tdr(evs["SN0041"], evs["SN0042"]), ([], "TODO"))
    an0 = safe(lambda: compare_tdr(evs["SN0041"], evs["SN0041"]), ([], "TODO"))
    check("compare_tdr: golden vs itself PASS, SN0042 FAIL with exactly the 68-ohm segment",
          lambda: (an0[1] == "PASS" and an[1] == "FAIL" and len(an[0]) == 1 and an[0][0]["kind"] == "segment",
                   "self=%s dut=%s anomalies=%d" % (an0[1], an[1], len(an[0]))))
    safe(lambda: print(tdr_report("SN0042", evs["SN0042"], an[0], an[1], "SN0041")))

    rows = safe(lambda: series_sweep(NET, list(range(0, 61))), [])
    knee = safe(lambda: os_knee(rows))
    os_r0 = safe(lambda: series_sweep(dict(NET, cin=0.0), [0])[0]["os"])
    check("series sweep Rs=0: 53.8% with pure open end, Cin=4pF makes it worse (> 55%)",
          lambda: (near(os_r0, 53.85, 0.3) and rows[0]["os"] > 55.0,
                   "OS(0, Cin=0)=%.2f%%  OS(0, Cin=4pF)=%.2f%%" % (os_r0, rows[0]["os"])))
    check("overshoot floor first reached at Rs = Z0 - Rdrv = 35 ohm (+-1)",
          lambda: (near(knee, 35, 1), "knee=%s ohm" % knee))
    check("E24 at/below knee = 33 ohm, its OS < 3%",
          lambda: (nearest_e24_below(knee) == 33.0 and rows[33]["os"] < 3.0,
                   "E24=%s OS(33)=%.2f%%" % (nearest_e24_below(knee), rows[33]["os"])))
    tab = safe(lambda: {n: m for n, _, m in termination_table(NET)}, {})
    check("parallel 50R: Vhi = 3.3*50/65 = 2.538 V, P_supply ~ 0.5*3.3^2/65 = 83.8 mW (+-10%)",
          lambda: (near(tab["parallel"]["vhi"], 2.538, 0.01) and near(tab["parallel"]["p_supply"], 0.0838, 0.0084),
                   "Vhi=%.3f P=%.1fmW" % (tab["parallel"]["vhi"], tab["parallel"]["p_supply"] * 1e3)))
    check("thevenin 100/100: Vhi 2.919 / Vlo 0.381 V, Idrv_dc halves vs parallel",
          lambda: (near(tab["thevenin"]["vhi"], 2.919, 0.01) and near(tab["thevenin"]["vlo"], 0.381, 0.01)
                   and near(tab["thevenin"]["idrv"], 0.5 * tab["parallel"]["idrv"], 1e-3),
                   "Vhi=%.3f Vlo=%.3f Idrv=%.1f vs %.1fmA" % (tab["thevenin"]["vhi"], tab["thevenin"]["vlo"],
                                                            tab["thevenin"]["idrv"] * 1e3,
                                                            tab["parallel"]["idrv"] * 1e3)))
    check("series: P < 25 mW & OS <= 5%; ac: OS <= 5% & P between series and parallel; none OS > 40%",
          lambda: (tab["series"]["p_supply"] < 0.025 and tab["series"]["os"] <= 5 and tab["ac"]["os"] <= 5
                   and tab["series"]["p_supply"] < tab["ac"]["p_supply"] < tab["parallel"]["p_supply"]
                   and tab["none"]["os"] > 40,
                   "series P=%.1fmW OS=%.1f%% | ac P=%.1fmW OS=%.1f%% | none OS=%.1f%%"
                   % (tab["series"]["p_supply"] * 1e3, tab["series"]["os"], tab["ac"]["p_supply"] * 1e3,
                      tab["ac"]["os"], tab["none"]["os"])))
    par0 = safe(lambda: clock_metrics(dict(NET, cin=0.0), 0.0, dict(shunts=[(50.0, 0.0)])), {})
    check("parallel OS is the Cin reflection re-reflected by the 15R driver: Cin=0 -> OS < 0.5%",
          lambda: (par0["os"] < 0.5 and tab["parallel"]["os"] > 5.0,
                   "OS(Cin=0)=%.2f%%  OS(Cin=4pF)=%.1f%%" % (par0["os"], tab["parallel"]["os"])))
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 2 and argv[1] == "--tables":
        print_tables()
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
