#!/usr/bin/env python3
"""HWLAB-M12 채널 손실·ISI와 등화: TX FFE·CTLE·DFE

8 GT/s NRZ (PCIe Gen3 급) 링크의 손실 채널을 FIR 로 모델링하고, 등화기를 단계별로 붙이며 eye 가 어떻게 열리는지 잰다.
  Part A  채널 step → FIR, Nyquist 삽입손실(−15 dB) 확인, pulse response 의 pre/main/post-cursor,
          축약 eye 도구(PRBS7 중첩 eye + 최악 패턴 PDA eye)로 eye height/width
  Part B  3-tap TX FFE(zero-forcing), 1-zero/2-pole CTLE(bilinear biquad), 1~3-tap DFE 를 단계별로 붙인 표,
          CTLE 과등화·상대 잡음 증가, DFE 오류 전파(genie vs 실제 결정) 측정
  Part C  VALBRD-S PCIe Gen3 두 레인의 TDT step 응답(CSV)으로 preset×CTLE×DFE1 스윕 → PASS/FAIL,
          실패 레인의 echo 를 찾아 스텁 길이·노치 주파수로 바꾼 한 줄 리포트

  python3 -I solution.py        # "N/M passed", 실패 있으면 exit 1

eye 도구는 HWLAB-M09(골든 PLL 폴딩·히스토그램·마스크)를 이 레슨에 맞게 축약한 것이다:
잡음·지터 없는 선형 채널이므로 '이상 클럭 + pulse response 중첩'으로 eye 를 직접 계산한다.
Python 3.9+, 표준 라이브러리만 (cmath, json, math, os, random, sys, time).
"""
import cmath
import json
import math
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- 시스템 상수
BITRATE = 8e9                 # 8 GT/s NRZ (PCIe Gen3 급)
UI = 1.0 / BITRATE            # 125 ps
SPS = 32                      # UI 당 샘플 → dt = 3.90625 ps
DT = UI / SPS
F_NYQ = BITRATE / 2           # 4 GHz
SWING_MV = 400.0              # TX ±400 mV (800 mVppd 차동, FFE 계수 합 |c| = 1 로 정규화)
Q_1E12 = 7.034                # BER 1e-12 의 Q (가우스 꼬리 한쪽)
SIGMA_IN_MV = 2.0             # RX 입력 환산 잡음+크로스토크 rms (가정: 0–12 GHz 평탄)
NOISE_BW = 12e9


# ---------------------------------------------------------------- 채널 모델 (주어짐)
def channel_step(loss_db, n_ui=48, skin_frac=0.4, npole=2):
    """손실 채널의 단위 step 응답 샘플 (dt = DT, 길이 n_ui·SPS).

    H(s) = exp(-k·√s) · 1/(1 + s/ωp)^npole
      · exp(-k√s) : 표피효과(skin effect) 선로. step 응답이 닫힌 꼴 erfc(k / (2√t)) 이고 인과적이다.
                    |H(j2πf)| = exp(-k√(πf))  →  손실[dB] = 8.686·k·√(πf)  (√f 에 비례)
      · 극 npole 개 : 패키지·패드 C 와 유전 손실을 뭉뚱그린 저역통과 (가정). step-invariant 1차 IIR 로 적용
    Nyquist 손실 loss_db 중 skin_frac 를 √f 항이, 나머지를 극이 맡도록 k, fp 를 정한다.
    """
    skin_db = loss_db * skin_frac
    pole_db = loss_db - skin_db
    fp = F_NYQ / math.sqrt(10 ** (pole_db / (10.0 * npole)) - 1.0)
    k = skin_db / (20 * math.log10(math.e) * math.sqrt(math.pi * F_NYQ))
    alpha = 1.0 - math.exp(-DT * 2 * math.pi * fp)
    ys = [0.0] * npole
    out = []
    for i in range(n_ui * SPS):
        t = i * DT
        x = math.erfc(k / (2 * math.sqrt(t))) if t > 0 else 0.0
        for j in range(npole):
            ys[j] += alpha * (x - ys[j])
            x = ys[j]
        out.append(x)
    return out


def tee_stub(x, tau_samples, n_echo=8):
    """같은 임피던스 선로에 열린(open) 스텁이 T 로 붙은 경우의 통과 응답 (x 는 어떤 응답이든 선형이면 OK).

    분기점 Γ = -1/3, 스텁 끝 Γ = +1, 통과 1+Γ = 2/3, 스텁 왕복 지연 τ:
      H = (2/3)(1 + z) / (1 + z/3),  z = e^{-sτ}
        = 2/3 + (4/9) z - (4/27) z^2 + (4/81) z^3 - ...   → echo 가 τ 마다, DC 이득은 1
    z = -1 (f = 1/(2τ), 1/4 파장 공진)에서 0 → 노치.
    """
    coef = [2.0 / 3.0] + [(4.0 / 9.0) * (-1.0 / 3.0) ** (m - 1) for m in range(1, n_echo + 1)]
    y = [0.0] * len(x)
    for m, c in enumerate(coef):
        d = m * tau_samples
        for i in range(len(x) - d):
            y[i + d] += c * x[i]
    return y


def prbs7():
    """PRBS7 (x^7 + x^6 + 1) 한 주기 127 비트, ±1."""
    r = 0x7F
    out = []
    for _ in range(127):
        b = ((r >> 6) ^ (r >> 5)) & 1
        r = ((r << 1) | b) & 0x7F
        out.append(1 if b else -1)
    return out


def ctle_mag(f, adc_db, fp1=2e9, fp2=8e9):
    """CTLE 해석 응답 |H(j2πf)|,  H(s) = ωp2·(s + A·ωp1) / ((s + ωp1)(s + ωp2)),  A = 10^(adc_db/20).
    zero 1개(fz = A·fp1), pole 2개. DC 이득 = A, 중간 대역(fp1 ≪ f ≪ fp2) 이득 ≈ 1."""
    a = 10 ** (adc_db / 20.0)
    s = 2j * math.pi * f
    w1, w2 = 2 * math.pi * fp1, 2 * math.pi * fp2
    return abs(w2 * (s + a * w1) / ((s + w1) * (s + w2)))


def iir2(x, b, a):
    """2차 IIR (direct form I). b = [b0, b1, b2], a = [1, a1, a2]."""
    y = []
    x1 = x2 = y1 = y2 = 0.0
    for xi in x:
        yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, xi, y1, yi
        y.append(yi)
    return y


def noise_out_rms(adc_db, sigma_in=SIGMA_IN_MV, bw=NOISE_BW, n=600):
    """평탄(0..bw) 입력 잡음 sigma_in 이 CTLE 를 지난 뒤의 rms: σ_out² = σ_in² · (1/bw)∫|H(f)|² df.
    adc_db=None 이면 CTLE 없음 → σ_in 그대로."""
    if adc_db is None:
        return sigma_in
    acc = sum(ctle_mag((i + 0.5) * bw / n, adc_db) ** 2 for i in range(n)) / n
    return sigma_in * math.sqrt(acc)


def load_steps(path):
    """CSV(t_ps, 레인1, 레인2, ...) → {레인 이름: step 샘플 리스트}. '#' 줄은 주석."""
    cols, names = None, None
    with open(path, encoding="ascii") as f:
        for line in f:
            s = line.strip()
            if not s or s[0] == "#":
                continue
            parts = s.split(",")
            if names is None:
                names = parts[1:]
                cols = [[] for _ in names]
                continue
            for c, v in zip(cols, parts[1:]):
                c.append(float(v))
    return dict(zip(names, cols))


def main_index(p):
    """main cursor = pulse response 의 최댓값 위치 (peak 샘플링 가정)."""
    return max(range(len(p)), key=p.__getitem__)


# ---------------------------------------------------------------- Part A: 채널 FIR · pulse response · eye
def fir_from_step(s):
    """step 응답 → 샘플 간격 FIR (단위 샘플 펄스 응답): h[0] = s[0], h[n] = s[n] - s[n-1]."""
    return [s[0]] + [s[i] - s[i - 1] for i in range(1, len(s))]


def il_db(h, f, dt=DT):
    """FIR h 의 주파수 f 에서 이득 [dB] — 한 주파수짜리 DFT: H(f) = Σ h[n]·e^{-j2πf·n·dt}."""
    acc = 0j
    w = -2j * math.pi * f * dt
    for n, hn in enumerate(h):
        acc += hn * cmath.exp(w * n)
    return 20 * math.log10(abs(acc))


def pulse_from_step(s, sps=SPS, amp=SWING_MV):
    """1 UI 폭 펄스(진폭 amp)의 응답: p[n] = amp·(S[n] - S[n - sps]),  (= FIR h 와 UI 폭 사각형의 컨볼루션)
    S[n] = s[n] (n < len), s[-1] (n >= len: FIR 이 끝났으니 step 은 그대로), 0 (n < 0).
    길이 len(s) + sps - 1 — 마지막 펄스 꼬리까지 담아야 컨볼루션과 정확히 같다."""
    last = len(s) - 1

    def S_(n):
        return 0.0 if n < 0 else s[min(n, last)]
    return [amp * (S_(i) - S_(i - sps)) for i in range(len(s) + sps - 1)]


def cursors(p, npre=2, npost=10, sps=SPS):
    """main cursor(peak) 기준 UI 간격 샘플 {k: h_k}, k = -npre..npost. 범위 밖은 0."""
    i0 = main_index(p)
    out = {}
    for k in range(-npre, npost + 1):
        i = i0 + k * sps
        out[k] = p[i] if 0 <= i < len(p) else 0.0
    return out


def _phase_taps(p, o, ph, sps):
    """위상 ph 에서 쓰는 (k, p[o+ph+k·sps]) 목록 (인덱스가 범위 안인 것만)."""
    out = []
    for k in range(-(o + ph) // sps - 1, len(p) // sps + 2):
        i = o + ph + k * sps
        if 0 <= i < len(p):
            out.append((k, p[i]))
    return out


def eye_curve(p, bits, taps=(), sps=SPS):
    """주기 비트열 bits(±1)를 보냈을 때 위상별 inner eye 높이 EH(φ), φ = 0..sps-1.

    o = main - sps/2 로 잡아 φ = sps/2 가 main cursor 위치. 비트 m 의 위상 φ 샘플:
      y_m(φ) = Σ_k bits[(m-k) mod N] · p[o + φ + k·sps]   (중첩 — 선형 채널이니까)
      DFE taps w_1..w_N 이 있으면  y_m(φ) -= Σ_j w_j · bits[m-j]   (올바른 결정 가정)
    EH(φ) = min(y | bit=+1) - max(y | bit=-1)   (음수면 닫힌 eye)
    """
    n = len(bits)
    o = main_index(p) - sps // 2
    eh = []
    for ph in range(sps):
        ks = _phase_taps(p, o, ph, sps)
        one, zero = float("inf"), float("-inf")
        for m in range(n):
            y = 0.0
            for k, c in ks:
                y += bits[(m - k) % n] * c
            for j, w in enumerate(taps, 1):
                y -= w * bits[(m - j) % n]
            if bits[m] > 0:
                one = min(one, y)
            else:
                zero = max(zero, y)
        eh.append(one - zero)
    return eh


def pda_curve(p, taps=(), sps=SPS):
    """최악 패턴(peak distortion analysis) eye 높이: EH(φ) = 2·(h_0(φ) - Σ_{k≠0} |h_k(φ) - w_k|),
    w_k = DFE tap (k = 1..N), 나머지 k 는 w_k = 0. ISI 를 모두 '나쁜 부호'로 맞춘 비트열의 eye."""
    o = main_index(p) - sps // 2
    eh = []
    for ph in range(sps):
        main, isi = 0.0, 0.0
        for k, c in _phase_taps(p, o, ph, sps):
            if k == 0:
                main = c
            else:
                if 1 <= k <= len(taps):
                    c -= taps[k - 1]
                isi += abs(c)
        eh.append(2 * (main - isi))
    return eh


def eye_metrics(eh, thr=0.0, sps=SPS):
    """EH(φ) 곡선 → (eye height - thr, eye width [UI], best φ).

    best φ = argmax EH. 거기서 좌우로 EH > thr 인 동안 걸어가고, 마지막 칸 사이를 선형 보간해 thr 교차점을 잡는다.
    EH(best) <= thr 이면 (EH - thr, 0.0, best). thr = 2·Q·σ 로 주면 'BER 에서의' eye.
    """
    b = max(range(sps), key=eh.__getitem__)
    if eh[b] <= thr:
        return eh[b] - thr, 0.0, b
    lft = b
    while lft > 0 and eh[lft - 1] > thr:
        lft -= 1
    rgt = b
    while rgt < sps - 1 and eh[rgt + 1] > thr:
        rgt += 1
    lx = lft - (eh[lft] - thr) / (eh[lft] - eh[lft - 1]) if lft > 0 else 0.0
    rx = rgt + (eh[rgt] - thr) / (eh[rgt] - eh[rgt + 1]) if rgt < sps - 1 else float(sps - 1)
    return eh[b] - thr, (rx - lx) / sps, b


# ---------------------------------------------------------------- Part B: TX FFE · CTLE · DFE
def ffe_apply(p, c, sps=SPS):
    """3-tap TX FFE (c-1, c0, c+1) 를 pulse 에 적용. 인과적으로 쓰려고 1 UI 늦춘다:
    q[n] = c-1·p[n] + c0·p[n - sps] + c+1·p[n - 2·sps]   (길이 len(p) + 2·sps)."""
    q = [0.0] * (len(p) + 2 * sps)
    for j, cj in enumerate(c):
        if cj == 0.0:
            continue
        off = j * sps
        for i, v in enumerate(p):
            q[i + off] += cj * v
    return q


def ffe_zf(cur):
    """3-tap zero-forcing: 등화 후 g_-1 = g_+1 = 0.  g_m = c-1·h_{m+1} + c0·h_m + c+1·h_{m-1}.
    c0 = 1 로 두면  [h0  h-2; h2  h0]·[c-1; c+1] = [-h-1; -h1]  → 풀고 |c-1| + |c0| + |c+1| = 1 로 정규화."""
    hm2, hm1, h0, h1, h2 = cur[-2], cur[-1], cur[0], cur[1], cur[2]
    det = h0 * h0 - hm2 * h2
    cm1 = (-hm1 * h0 + hm2 * h1) / det
    cp1 = (-h1 * h0 + hm1 * h2) / det
    tot = abs(cm1) + 1.0 + abs(cp1)
    return (cm1 / tot, 1.0 / tot, cp1 / tot)


def ctle_biquad(adc_db, fp1=2e9, fp2=8e9, dt=DT):
    """CTLE H(s) = ωp2(s + A·ωp1) / (s² + (ωp1+ωp2)s + ωp1·ωp2) 를 bilinear 변환 s = K(1-z⁻¹)/(1+z⁻¹), K = 2/dt.
    H(s) = (B0 s² + B1 s + B2)/(A0 s² + A1 s + A2) 일반식:
      b0 = B0K² + B1K + B2,  b1 = 2B2 - 2B0K²,  b2 = B0K² - B1K + B2   (a 도 같은 꼴), a0 로 정규화.
    반환 (b, a), a[0] = 1."""
    amp = 10 ** (adc_db / 20.0)
    w1, w2 = 2 * math.pi * fp1, 2 * math.pi * fp2
    K = 2.0 / dt
    B0, B1, B2 = 0.0, w2, w2 * amp * w1
    A0, A1, A2 = 1.0, w1 + w2, w1 * w2
    b = [B0 * K * K + B1 * K + B2, 2 * B2 - 2 * B0 * K * K, B0 * K * K - B1 * K + B2]
    a = [A0 * K * K + A1 * K + A2, 2 * A2 - 2 * A0 * K * K, A0 * K * K - A1 * K + A2]
    return [x / a[0] for x in b], [x / a[0] for x in a]


def ctle_apply(p, adc_db, fp1=2e9, fp2=8e9):
    """pulse 에 CTLE 적용 (뒤에 8 UI 0 을 붙여 IIR 꼬리를 담는다)."""
    b, a = ctle_biquad(adc_db, fp1, fp2)
    return iir2(list(p) + [0.0] * (8 * SPS), b, a)


def dfe_taps(p, ntaps, limits=None, sps=SPS):
    """DFE tap = main 위상에서의 post-cursor h_1..h_N (zero-forcing 적응이 수렴한 값). limits[k] 로 ±클립."""
    cur = cursors(p, 0, ntaps, sps)
    w = []
    for k in range(1, ntaps + 1):
        v = cur[k]
        if limits is not None:
            lim = limits[k - 1]
            v = max(-lim, min(lim, v))
        w.append(v)
    return w


def eye_report(p, taps=(), adc_db=None, sigma_in=SIGMA_IN_MV, bits=None):
    """한 등화 설정의 eye 묶음: PRBS7 eye, 최악 패턴(PDA) eye, PDA + 가우스 잡음 @BER 1e-12."""
    bits = bits or prbs7()
    sig = noise_out_rms(adc_db, sigma_in)
    e7, w7, _ = eye_metrics(eye_curve(p, bits, taps))
    pda = pda_curve(p, taps)
    ep, wp, _ = eye_metrics(pda)
    eb, wb, _ = eye_metrics(pda, 2 * Q_1E12 * sig)
    cur = cursors(p, 2, 3)
    return dict(cur=cur, sigma=sig, prbs_eh=e7, prbs_ew=w7, pda_eh=ep, pda_ew=wp, ber_eh=eb, ber_ew=wb)


def dfe_sim(cur, ntaps, sigma, nbits=100000, seed=12, genie=False):
    """UI 단위 DFE 시뮬레이션 → 오류 난 비트 번호 목록.

    r_n = Σ_k h_k·b_{n-k} + N(0, σ²),  w_j = h_j (j = 1..ntaps)
    결정 d_n = sign(r_n - Σ_j w_j·x_{n-j}),  x = 실제 결정 d (genie=False) 또는 참 비트 b (genie=True)
    같은 seed 면 비트·잡음이 같아서 두 경우를 짝지어 비교할 수 있다. 시작 전 이력은 맞게 결정됐다고 가정.
    """
    rng = random.Random(seed)
    ks = sorted(cur)
    pad, fut = max(ks) + 1, max(0, -min(ks))
    bits = [rng.choice((-1, 1)) for _ in range(pad + nbits + fut)]
    w = [cur[j] for j in range(1, ntaps + 1)]
    hk = [(k, cur[k]) for k in ks]
    d = bits[:pad]
    errs = []
    for n in range(pad, pad + nbits):
        r = rng.gauss(0.0, sigma)
        for k, h in hk:
            r += h * bits[n - k]
        hist = bits if genie else d
        for j in range(1, ntaps + 1):
            r -= w[j - 1] * hist[n - j]
        dn = 1 if r >= 0 else -1
        d.append(dn)
        if dn != bits[n]:
            errs.append(n - pad)
    return errs


def bursts(errs, gap):
    """오류 위치 목록 → 버스트 길이 목록. 이웃 오류 간격이 gap 이하이면 같은 버스트 (DFE 기억 길이 = tap 수)."""
    if not errs:
        return []
    out = [1]
    for a, b in zip(errs, errs[1:]):
        if b - a <= gap:
            out[-1] += 1
        else:
            out.append(1)
    return out


# ---------------------------------------------------------------- Part C: PCIe Gen3 레인 판정
def preset_taps(ps_db, de_db):
    """(preshoot dB ≥ 0, de-emphasis dB ≤ 0) → (c-1, c0, c+1),  |c-1| + c0 + |c+1| = 1,  c±1 ≤ 0.

    a = |c-1|, b = |c+1|, c0 = 1 - a - b 라 두면 레벨들은
      Vd(고립 비트) = 1,  Vb(정상 상태) = 1 - 2a - 2b,  Va(전이 직후) = 1 - 2a,  Vc(전이 직전) = 1 - 2b
      de-emphasis = 20·log10(Vb/Va),  preshoot = 20·log10(Vc/Vb)
    → rd = 10^(de/20), rp = 10^(ps/20) 로 두 일차식:
      (2rd - 2)·a - 2·b = rd - 1
      2rp·a + (2rp - 2)·b = rp - 1
    """
    rd, rp = 10 ** (de_db / 20.0), 10 ** (ps_db / 20.0)
    a11, a12, y1 = 2 * rd - 2, -2.0, rd - 1
    a21, a22, y2 = 2 * rp, 2 * rp - 2, rp - 1
    det = a11 * a22 - a12 * a21
    a = (y1 * a22 - a12 * y2) / det
    b = (a11 * y2 - y1 * a21) / det
    return (-a + 0.0, 1.0 - a - b, -b + 0.0)


def solve3(m, v):
    """3×3 선형계 m·x = v (가우스 소거, 부분 피벗)."""
    a = [list(row) + [vi] for row, vi in zip(m, v)]
    for i in range(3):
        piv = max(range(i, 3), key=lambda r: abs(a[r][i]))
        a[i], a[piv] = a[piv], a[i]
        for r in range(i + 1, 3):
            f = a[r][i] / a[i][i]
            for c in range(i, 4):
                a[r][c] -= f * a[i][c]
    x = [0.0] * 3
    for i in (2, 1, 0):
        x[i] = (a[i][3] - sum(a[i][c] * x[c] for c in range(i + 1, 3))) / a[i][i]
    return x


def find_echo(p_ref, p_dut, tau_min=4, tau_max=96):
    """스텁 하나의 통과 응답은 z = e^{-sτ} 에 대해 H = (a + b·z)/(1 + c·z)  (다중 반사가 등비급수로 접힌 꼴).
    시간 영역: y[n] + c·y[n-τ] = a·x[n] + b·x[n-τ]  →  τ 를 고정하면 (a, b, c) 에 대해 선형!
    τ 마다 열 [x, x(τ), -y(τ)] 로 최소제곱(3×3 정규방정식)을 풀고 잔차가 가장 작은 τ 를 고른다.
    x = 골든 레인 pulse, y = 의심 레인 pulse.  첫 echo/통과 = (b - a·c)/a,  분기점에서 되돌아 본 Γ = -c,
    노치: a + b·z = 0 → z = -a/b (a = b 면 f = 1/(2τ)).
    반환 (τ[샘플], a, b, c, 잔차/에너지)."""
    n = min(len(p_ref), len(p_dut))
    x, y = p_ref[:n], p_dut[:n]
    syy = sum(v * v for v in y)
    best = None
    for tau in range(tau_min, tau_max + 1):
        cols = [x, [0.0] * tau + x[:n - tau], [0.0] * tau + [-v for v in y[:n - tau]]]
        m = [[sum(u * v for u, v in zip(ci, cj)) for cj in cols] for ci in cols]
        rhs = [sum(u * v for u, v in zip(ci, y)) for ci in cols]
        coef = solve3(m, rhs)
        sse = syy - sum(ci * ri for ci, ri in zip(coef, rhs))     # 최소제곱 해에서의 잔차 제곱합
        if best is None or sse < best[4]:
            best = (tau, coef[0], coef[1], coef[2], sse)
    tau, a, b, c, sse = best
    return tau, a, b, c, sse / syy


def load_spec():
    with open(os.path.join(HERE, "valbrd_s_pcie_gen3.json"), encoding="utf-8") as f:
        return json.load(f)


def sweep_eq(p, spec):
    """TX preset × CTLE ADC × DFE(1 tap, ±limit) 전수 스윕. PDA eye + 잡음 @1e-12 로 최선을 고른다.
    반환: 정렬된 [(EH_ber, EW_ber, preset, adc_db, d1), ...] (EH 큰 순, 같으면 EW 큰 순)."""
    rx = spec["rx"]
    res = []
    for adc in range(rx["ctle_adc_db"][0], rx["ctle_adc_db"][1] - 1, -1):
        q0 = ctle_apply(p, adc)
        thr = 2 * Q_1E12 * noise_out_rms(adc, spec["noise"]["sigma_in_mv"])
        for name, (ps, de) in spec["tx_presets"].items():
            q = ffe_apply(q0, preset_taps(ps, de))
            w = dfe_taps(q, rx["dfe_taps"], rx["dfe_limit_mv"])
            eh, ew, _ = eye_metrics(pda_curve(q, w), thr)
            res.append((eh, ew, name, adc, w[0] if w else 0.0))
    res.sort(key=lambda r: (r[0], r[1]), reverse=True)
    return res


def lane_verdict(best, spec):
    eh, ew = best[0], best[1]
    ok = eh >= spec["eye_spec"]["eh_mv"] and ew >= spec["eye_spec"]["ew_ui"]
    return "PASS" if ok else "FAIL"


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


def safe(fn, default=None, quiet=False):
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        if not quiet:
            print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def fmt_stage(name, r):
    c = r["cur"]
    return ("%-18s h-1=%6.1f h0=%6.1f h1=%6.1f h2=%6.1f | PRBS7 %6.1fmV %.2fUI | PDA %6.1fmV %.2fUI"
            " | @1e-12 %6.1fmV %.2fUI (σ=%.2fmV)"
            % (name, c[-1], c[0], c[1], c[2], r["prbs_eh"], r["prbs_ew"], r["pda_eh"], r["pda_ew"],
               r["ber_eh"], r["ber_ew"], r["sigma"]))


def run_tests():
    t_start = time.time()
    # ---- Part A: −15 dB @ 4 GHz 채널 → FIR, pulse response, eye
    print("-- Part A: channel FIR / pulse response / eye (8 GT/s, -15 dB @ 4 GHz) --")
    s15 = channel_step(15.0)
    h = safe(lambda: fir_from_step(s15), [])
    check("FIR = diff(step): sum(h) == step[-1] (telescoping)",
          lambda: (near(sum(h), s15[-1], 1e-12), "sum(h)=%.6f step_end=%.6f" % (sum(h), s15[-1])))
    il = safe(lambda: [il_db(h, f) for f in (1e9, 2e9, F_NYQ)], [None] * 3)
    check("insertion loss @4GHz from FIR == -15.0 dB (+-0.3)",
          lambda: (near(il[2], -15.0, 0.3), "IL(1/2/4 GHz) = %.2f / %.2f / %.2f dB" % tuple(il)))
    check("loss grows with f (1 < 2 < 4 GHz)", lambda: il[0] > il[1] > il[2])
    p15 = safe(lambda: pulse_from_step(s15), [])
    check("pulse = amp*(s[n]-s[n-SPS]) == amp * conv(h, rect_UI) at 3 points",
          lambda: all(near(p15[i], SWING_MV * sum(h[i - m] for m in range(SPS) if i - m >= 0), 1e-9)
                      for i in (5, 40, 700)))
    cur = safe(lambda: cursors(p15, 2, 12), {})
    safe(lambda: print("      cursors[mV]: " + " ".join("h%+d=%.1f" % (k, cur[k]) for k in range(-2, 7))))
    check("main cursor h0 ~ 177 mV of 400 (peak of pulse)",
          lambda: (near(cur[0], 177.3, 1.0) and cur[0] == max(p15), "h0=%.2f" % cur[0]))
    check("pre-cursor h-1/h0 in 0.10..0.16, post h1/h0 in 0.45..0.53",
          lambda: (0.10 <= cur[-1] / cur[0] <= 0.16 and 0.45 <= cur[1] / cur[0] <= 0.53,
                   "h-1/h0=%.3f h1/h0=%.3f" % (cur[-1] / cur[0], cur[1] / cur[0])))
    allk = safe(lambda: cursors(p15, 2, 60), {})
    check("sum of UI-spaced pulse samples == amp * step_end (DC gain identity)",
          lambda: (near(sum(allk.values()), SWING_MV * s15[-1], 1e-6),
                   "sum=%.4f amp*s_end=%.4f" % (sum(allk.values()), SWING_MV * s15[-1])))
    bits = prbs7()
    eh7 = safe(lambda: eye_curve(p15, bits), [])

    def brute():
        o = main_index(p15) - SPS // 2
        ph = SPS // 2
        worst = 0.0
        for m in range(0, 127, 9):                 # 비트 m, 위상 φ 를 FIR 컨볼루션으로 직접 계산
            i = 3 * 127 * SPS + m * SPS + o + ph
            y = 0.0
            for j in range(len(h)):                # y[i] = Σ_j x[i-j]·h[j], x = 3주기 NRZ 파형
                idx = i - j
                y += SWING_MV * bits[(idx // SPS) % 127] * h[j]
            sup = sum(bits[(m - k) % 127] * c for k, c in _phase_taps(p15, o, ph, SPS))
            worst = max(worst, abs(y - sup))
        return worst < 1e-6, "max |conv - superposition| = %.2e mV" % worst
    check("superposition eye sample == brute-force FIR convolution of NRZ waveform", brute)
    m7 = safe(lambda: eye_metrics(eh7), (None, None, None))
    pda = safe(lambda: eye_metrics(pda_curve(p15)), (None, None, None))
    safe(lambda: print("      no EQ: PRBS7 eye EH=%.1fmV EW=%.3fUI | worst-case(PDA) EH=%.1fmV EW=%.3fUI"
                       % (m7[0], m7[1], pda[0], pda[1])))
    check("no-EQ PRBS7 eye barely open: 15..30 mV, EW 0.25..0.35 UI",
          lambda: (15 <= m7[0] <= 30 and 0.25 <= m7[1] <= 0.35, "EH=%.1f EW=%.3f" % (m7[0], m7[1])))
    check("worst-case (PDA) eye closed and below PRBS7 eye (PRBS7 is optimistic)",
          lambda: (pda[0] < 0 < m7[0], "PDA=%.1f PRBS7=%.1f" % (pda[0], m7[0])))
    check("eye_metrics: triangle EH(φ) peak 10 @ φ=16, zero at 11/21 -> EW = 10/32 UI",
          lambda: (lambda r: (near(r[0], 10.0, 1e-9) and near(r[1], 10 / 32, 1e-9) and r[2] == 16, str(r)))(
              eye_metrics([10.0 - 2.0 * abs(i - 16) for i in range(SPS)])))

    # ---- Part B: 등화기 단계별
    print("-- Part B: TX FFE / CTLE / DFE stages (sigma_in = %.1f mV rms flat to 12 GHz) --" % SIGMA_IN_MV)
    zf = safe(lambda: ffe_zf(cursors(p15, 2, 2)), (0.0, 1.0, 0.0))
    safe(lambda: print("      ZF FFE taps: c-1=%.3f c0=%.3f c+1=%.3f" % zf))
    pz = safe(lambda: ffe_apply(p15, zf), p15)
    i0z = safe(lambda: main_index(p15) + SPS, 0)     # ZF 를 설계한 위상 = 원래 main 위치 + FFE 지연 1 UI
    cz = safe(lambda: {k: pz[i0z + k * SPS] for k in (-1, 0, 1)}, {})
    check("ZF FFE: |c-1|+|c0|+|c+1| == 1, c-1<0, c+1<0",
          lambda: (near(sum(abs(c) for c in zf), 1.0, 1e-12) and zf[0] < 0 and zf[2] < 0, "%s" % (zf,)))
    check("ZF FFE nulls h-1 and h+1 at the phase it was designed for (|g+-1| < 1e-9 * h0)",
          lambda: (abs(cz[-1]) < 1e-9 * cz[0] and abs(cz[1]) < 1e-9 * cz[0],
                   "g-1=%.2e g1=%.2e" % (cz[-1], cz[1])))
    b, a = safe(lambda: ctle_biquad(-7), ([0, 0, 0], [1, 0, 0]))

    def biquad_vs_analytic():
        imp = iir2([1.0] + [0.0] * 4095, b, a)
        errs = [abs(il_db(imp, f) - 20 * math.log10(ctle_mag(f, -7))) for f in (0.5e9, 2e9, 4e9)]
        dc = 20 * math.log10(abs(sum(imp)))
        return max(errs) < 0.05 and near(dc, -7.0, 0.02), "max err=%.4f dB, DC=%.3f dB" % (max(errs), dc)
    check("CTLE bilinear biquad matches analytic |H| (0.5/2/4 GHz) and DC = ADC = -7 dB", biquad_vs_analytic)

    stages = {}
    stages["no EQ"] = safe(lambda: eye_report(p15))
    stages["TX FFE (ZF)"] = safe(lambda: eye_report(pz))
    sweep = []
    for adc in range(0, -13, -1):
        q = safe(lambda: ctle_apply(p15, adc), p15, quiet=adc != 0)
        sweep.append((adc, q, safe(lambda: eye_metrics(pda_curve(q))[0], quiet=adc != 0)))
    best_adc, q_ctle, _ = max(sweep, key=lambda r: r[2] if r[2] is not None else -1e9)
    stages["CTLE %ddB" % best_adc] = safe(lambda: eye_report(q_ctle, (), best_adc))
    for n in (1, 2, 3):
        stages["CTLE+DFE%d" % n] = safe(lambda: eye_report(q_ctle, dfe_taps(q_ctle, n), best_adc))
    stages["DFE3 only"] = safe(lambda: eye_report(p15, dfe_taps(p15, 3)))
    q_all = safe(lambda: ffe_apply(q_ctle, zf), q_ctle)
    stages["ZF+CTLE+DFE3"] = safe(lambda: eye_report(q_all, dfe_taps(q_all, 3), best_adc))
    for name, r in stages.items():
        if r:
            print("      " + fmt_stage(name, r))
    st = stages
    check("CTLE sweep 0..-12 dB: best PDA eye at an interior ADC (-5..-9 dB), deeper = over-equalized",
          lambda: (-9 <= best_adc <= -5 and sweep[-1][2] < sweep[best_adc * -1][2],
                   "best=%d dB EH=%.1f, -12 dB EH=%.1f" % (best_adc, sweep[-best_adc][2], sweep[-1][2])))
    check("over-equalized (-12 dB) shrinks h1 toward 0 and costs h0 (h1(-12) < h1(best)/2)",
          lambda: (lambda c12, cb: (c12[1] < cb[1] / 2 and c12[0] < cb[0],
                                    "h0 %.1f->%.1f h1 %.1f->%.1f" % (cb[0], c12[0], cb[1], c12[1])))(
              cursors(sweep[-1][1], 1, 1), cursors(q_ctle, 1, 1)))

    def noise_enh():
        r = []
        for adc, q, _ in sweep:
            r.append(noise_out_rms(adc) / cursors(q, 0, 0)[0])
        mono = all(r[i + 1] > r[i] for i in range(len(r) - 1))
        return mono, "sigma_out/h0: %.4f (0 dB) -> %.4f (-12 dB)" % (r[0], r[-1])
    check("CTLE relative noise enhancement: sigma_out/h0 rises monotonically with peaking", noise_enh)
    chain = ["no EQ", "CTLE %ddB" % best_adc, "CTLE+DFE1", "CTLE+DFE2", "CTLE+DFE3"]
    check("stage chain improves @1e-12: noEQ < CTLE < +DFE1 < +DFE2 < +DFE3",
          lambda: (all(st[chain[i]]["ber_eh"] < st[chain[i + 1]]["ber_eh"] for i in range(len(chain) - 1)),
                   " < ".join("%.1f" % st[c]["ber_eh"] for c in chain)))
    check("PRBS7 eye >= PDA eye for every stage (PRBS7 misses the worst pattern)",
          lambda: all(r["prbs_eh"] >= r["pda_eh"] - 1e-9 for r in st.values()))
    check("stacking ZF FFE on top of CTLE+DFE3 over-equalizes: worse than CTLE+DFE3",
          lambda: (st["ZF+CTLE+DFE3"]["ber_eh"] < st["CTLE+DFE3"]["ber_eh"],
                   "%.1f < %.1f" % (st["ZF+CTLE+DFE3"]["ber_eh"], st["CTLE+DFE3"]["ber_eh"])))

    # DFE 오류 전파: 같은 비트·잡음으로 genie(참 비트 feedback) vs 실제 결정 feedback
    prop = {}
    for name, q in (("DFE3 only", p15), ("CTLE+DFE3", q_ctle)):
        c = safe(lambda: cursors(q, 2, 24), {0: 1.0})
        sig = c[0] / 3.2
        g = safe(lambda: dfe_sim(c, 3, sig, 100000, 7, genie=True), [])
        e = safe(lambda: dfe_sim(c, 3, sig, 100000, 7, genie=False), [])
        bg, be = safe(lambda: bursts(g, 3), []), safe(lambda: bursts(e, 3), [])
        prop[name] = dict(w1=c.get(1, 0) / c[0], g=len(g), e=len(e),
                          bg=sum(bg) / max(1, len(bg)), be=sum(be) / max(1, len(be)), mx=max(be) if be else 0)
        print("      error propagation %-10s w1/h0=%.2f sigma=h0/3.2: genie BER=%.2e  actual BER=%.2e (x%.2f)"
              "  mean burst %.2f -> %.2f, max %d"
              % (name, prop[name]["w1"], len(g) / 1e5, len(e) / 1e5, len(e) / max(1, len(g)),
                 prop[name]["bg"], prop[name]["be"], prop[name]["mx"]))
    pr = prop
    check("bursts(): [3,4,9,20,21,22,30] gap 3 -> [2,1,3,1]",
          lambda: bursts([3, 4, 9, 20, 21, 22, 30], 3) == [2, 1, 3, 1])
    check("genie DFE errors are isolated (mean burst < 1.05) and BER ~ 1e-3 (5e-4..5e-3)",
          lambda: (pr["DFE3 only"]["bg"] < 1.05 and 50 <= pr["DFE3 only"]["g"] <= 500,
                   "mean burst %.3f, errors %d" % (pr["DFE3 only"]["bg"], pr["DFE3 only"]["g"])))
    check("DFE3-only (w1/h0~0.49): error propagation x>=1.3 BER and mean burst >= 1.3",
          lambda: (pr["DFE3 only"]["e"] >= 1.3 * pr["DFE3 only"]["g"] and pr["DFE3 only"]["be"] >= 1.3,
                   "x%.2f, burst %.2f" % (pr["DFE3 only"]["e"] / max(1, pr["DFE3 only"]["g"]), pr["DFE3 only"]["be"])))
    check("CTLE first (smaller w1/h0) -> much weaker propagation (x < 1.2)",
          lambda: (pr["CTLE+DFE3"]["e"] < 1.2 * pr["CTLE+DFE3"]["g"],
                   "x%.2f" % (pr["CTLE+DFE3"]["e"] / max(1, pr["CTLE+DFE3"]["g"]))))

    # ---- Part C: VALBRD-S PCIe Gen3 레인 판정
    print("-- Part C: VALBRD-S rev A PCIe Gen3 lanes (TDT step -> pulse -> EQ sweep -> report) --")
    spec = load_spec()
    check("preset P7 (3.5 dB PS, -6.0 dB DE) -> (-0.100, 0.700, -0.200) +-0.002",
          lambda: (lambda c: (all(near(x, y, 0.002) for x, y in zip(c, (-0.1, 0.7, -0.2))), "%.4f %.4f %.4f" % c))(
              preset_taps(3.5, -6.0)))
    check("preset P4 -> (0, 1, 0); P1 (-3.5 dB DE) -> c+1 ~ -0.166",
          lambda: (lambda c4, c1: (all(near(x, y, 1e-12) for x, y in zip(c4, (0, 1, 0))) and near(c1[2], -0.166, 0.002),
                                   "P1=%s" % ("%.4f %.4f %.4f" % c1)))(preset_taps(0, 0), preset_taps(0, -3.5)))
    steps = load_steps(os.path.join(HERE, "valbrd_s_tdt_steps.csv"))
    check("TDT CSV loaded: lanes L0,L2, 1536 samples @ 3.90625 ps",
          lambda: sorted(steps) == ["L0", "L2"] and all(len(v) == 48 * SPS for v in steps.values()))
    pulses = {ln: safe(lambda: pulse_from_step(steps[ln]), []) for ln in ("L0", "L2")}
    best = {}
    for ln in ("L0", "L2"):
        best[ln] = safe(lambda: sweep_eq(pulses[ln], spec), [(None, None, "-", 0, 0.0)])
    echo = safe(lambda: find_echo(pulses["L0"], pulses["L2"]), (None,) * 5)
    v_cm_ns = spec["board"]["v_cm_per_ns"]
    lines = []
    for ln in ("L0", "L2"):
        r = best[ln][0]
        try:
            verdict = lane_verdict(r, spec)
            q = ffe_apply(ctle_apply(pulses[ln], r[3]), preset_taps(*spec["tx_presets"][r[2]]))
            lab = eye_metrics(eye_curve(q, bits, dfe_taps(q, 1, spec["rx"]["dfe_limit_mv"])),
                              2 * Q_1E12 * noise_out_rms(r[3], spec["noise"]["sigma_in_mv"]))
            line = ("%s VALBRD-S %s(%s): best %s+CTLE%ddB+DFE1(%.1fmV) -> EH=%.1fmV EW=%.2fUI @1e-12 (spec >=%gmV/%gUI)"
                    " | PRBS7 lab eye %.1fmV" % (verdict, spec["lanes"][ln]["net"], ln, r[2], r[3], r[4], r[0], r[1],
                                                spec["eye_spec"]["eh_mv"], spec["eye_spec"]["ew_ui"], lab[0]))
            if verdict == "FAIL" and echo[0] is not None:
                tau_ps = echo[0] * DT * 1e12
                line += ("\n     echo %+.2fx @ +%.1fps vs L0 (junction G=%+.2f) -> open stub ~ %.1f mm (v=%gcm/ns),"
                         " notch ~ %.2f GHz (F_nyq %.0f GHz)\n     -> %s"
                         % ((echo[2] - echo[1] * echo[3]) / echo[1], tau_ps, -echo[3],
                            v_cm_ns * 10 * tau_ps * 1e-3 / 2, v_cm_ns, 1e3 / (2 * tau_ps), F_NYQ / 1e9,
                            spec["lanes"][ln]["suspect"]))
            lines.append(line)
        except Exception as e:  # noqa: BLE001
            lines.append("%s: report failed (%s: %s)" % (ln, type(e).__name__, e))
    for line in lines:
        print("   " + line)
    for ln in ("L0", "L2"):
        safe(lambda: print("      %s top3: %s" % (ln, ", ".join("%s/%ddB %.1fmV %.2fUI" % (r[2], r[3], r[0], r[1])
                                                                for r in best[ln][:3]))))
    check("L0 (golden, -20 dB) -> PASS with margin (EH >= 60 mV, EW >= 0.6 UI)",
          lambda: (lane_verdict(best["L0"][0], spec) == "PASS" and best["L0"][0][0] >= 60 and best["L0"][0][1] >= 0.6,
                   "EH=%.1f EW=%.3f" % best["L0"][0][:2]))
    check("L2 (planted stub) -> FAIL even with the best of 70 EQ settings",
          lambda: (lane_verdict(best["L2"][0], spec) == "FAIL", "EH=%.1f EW=%.3f" % best["L2"][0][:2]))
    check("find_echo recovers the planted stub round trip: 30 samples = 117.19 ps (exact)",
          lambda: (echo[0] == 30, "tau=%s samples" % echo[0]))
    check("first echo/through = (4/9)/(2/3) = 0.667 and junction G = -1/3 (+-0.01), residual < 1e-6",
          lambda: (lambda e1: (near(e1, 2 / 3, 0.01) and near(-echo[3], -1 / 3, 0.01) and echo[4] < 1e-6,
                               "echo/through=%.4f G=%.4f resid=%.1e" % (e1, -echo[3], echo[4])))(
              (echo[2] - echo[1] * echo[3]) / echo[1]))
    print("%d/%d passed  (%.1f s)" % (g_pass, g_total, time.time() - t_start))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
