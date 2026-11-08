#!/usr/bin/env python3
"""HWLAB-B06 트리거 로직 구현: Edge · Pulse-width · Runt · Timeout

스코프 트리거를 '샘플 스트림 상태기계'로 구현하고, 10M 샘플(10 ms @ 1 GSa/s) 합성 캡처에 심어 둔
드문 이상 파형(runt+, 글리치, 클록 정지, runt-)을 찾아 pre/post-trigger 윈도우로 잘라 저장한다.

  Part A  Schmitt 비교기 + Edge / PulseWidth / Runt / Timeout 트리거 (청크 단위 process, 상태 유지)
  Part B  히스테리시스 유무, 청크 경계, pulse '>' vs timeout 지연, pre/post 윈도우·holdoff
  Part C  10M 샘플 스캔 → 이벤트 4개 리포트 + 윈도우 CSV (captures/)

  python3 -I solution.py                 # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --save captures # 테스트 + 트리거 윈도우 CSV 저장

입력은 8비트 ADC 코드(int 0..255) 스트림. 임계값도 코드 단위(float 가능). Python 3 표준 라이브러리만.
"""
import hashlib
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True     # 레슨 폴더에 __pycache__ 를 만들지 않는다
sys.path.insert(0, HERE)            # -I 모드에서도 같은 폴더의 gen_capture 를 import
import gen_capture as gc            # noqa: E402

CAPTURE_SHA256 = "dbccc732edc97cb49f68f90dc18dc40bf4eb90876a52ad8f7f16e9b4822b5432"

# Part C 트리거 설정 (전압 → ADC 코드). 3.3 V LVCMOS 클록, VIL 0.8 V / VIH 2.0 V
EDGE_LO, EDGE_HI = gc.v2code(1.55), gc.v2code(1.75)   # edge 레벨 1.75 V, 히스테리시스 0.2 V
RUNT_L, RUNT_H = gc.v2code(0.8), gc.v2code(2.0)       # runt 두 임계 = VIL / VIH
RUNT_HYS = 0.2 / gc.LSB                               # 각 runt 비교기 히스테리시스 0.2 V (= 10 코드)
GLITCH_MAX = 10                                       # 양(+) 펄스 폭 < 10 ns → 글리치
TIMEOUT = 200                                         # low 가 200 ns 이상 지속 → 클록 정지
PRE, POST = 200, 600                                  # 윈도우: 트리거 앞 200 ns, 뒤 600 ns


# ---------------------------------------------------------------- Part A: 트리거 상태기계
class Schmitt:
    """히스테리시스 비교기. x >= hi 이면 1, x < lo 이면 0, 그 사이(밴드)면 이전 상태 유지.
    초기 상태 None(모름): 처음으로 밴드 밖 샘플이 오면 상태만 정하고 에지는 내지 않는다."""

    def __init__(self, lo, hi):
        assert lo <= hi
        self.lo, self.hi, self.state = lo, hi, None

    def step(self, x):
        """→ +1(상승 전이) / -1(하강 전이) / 0(변화 없음)"""
        # TODO: state 0 이고 x >= hi → state 1, return +1
        #       state 1 이고 x < lo  → state 0, return -1
        #       state None(모름) 이면 밴드 밖일 때 상태만 정하고 0
        return 0


# 아래 process() 들은 속도 때문에 Schmitt.step 을 부르지 않고 같은 규칙을 루프 안에 인라인한다
# (함수 호출 1회 ≈ 샘플 처리 전체 비용). 모든 트리거는 process(chunk, base) 를 여러 번 불러도
# 한 번에 부른 것과 결과가 같아야 한다 → 상태(state, 시작 시각, peak ...)를 self 에 저장.

class EdgeTrigger:
    """slope = 'rise' | 'fall' | 'either'. 트리거 지점 = Schmitt 가 전이한 샘플 인덱스"""

    def __init__(self, lo, hi, slope="rise"):
        self.lo, self.hi, self.slope, self.state = lo, hi, slope, None

    def process(self, chunk, base=0):
        lo, hi, st = self.lo, self.hi, self.state
        want_r = self.slope in ("rise", "either")
        want_f = self.slope in ("fall", "either")
        out = []
        j = base
        for x in chunk:
            pass  # TODO: Schmitt 규칙(st 0/1/None)으로 전이를 찾고, slope 에 맞으면 out.append(j)
            j += 1
        self.state = st
        return out


class PulseWidthTrigger:
    """polarity 'pos' = 상승→하강 펄스, 'neg' = 하강→상승 펄스. 폭 = 두 Schmitt 전이 사이 샘플 수.
    less=T → 폭 < T, greater=T → 폭 > T, 둘 다 → greater < 폭 < less.
    트리거 지점 = 펄스 뒷에지(trailing edge). 앞에지를 못 본 첫 펄스는 판정하지 않는다.
    → [(trigger_index, width)]"""

    def __init__(self, lo, hi, polarity="pos", less=None, greater=None):
        self.lo, self.hi, self.pol = lo, hi, polarity
        self.less, self.greater = less, greater
        self.state, self.t0 = None, None

    def _ok(self, w):
        return (self.less is None or w < self.less) and (self.greater is None or w > self.greater)

    def process(self, chunk, base=0):
        lo, hi, st, t0 = self.lo, self.hi, self.state, self.t0
        pos = self.pol == "pos"
        out = []
        j = base
        for x in chunk:
            pass  # TODO: 앞에지에서 t0 = j, 뒷에지에서 w = j - t0 → self._ok(w) 면 out.append((j, w))
            #       pos: 앞=상승, 뒤=하강 / neg: 앞=하강, 뒤=상승. t0 가 None 이면 판정하지 않는다
            j += 1
        self.state, self.t0 = st, t0
        return out


class RuntTrigger:
    """두 임계 L < H. 비교기 2개(각각 히스테리시스 hys: x >= thr 이면 1, x < thr-hys 이면 0).
    runt+ : L 을 위로 넘었다가 H 에 못 닿고 다시 L 아래로 → L 하강 지점에서 트리거, info = 최고 코드
    runt- : H 를 아래로 넘었다가 L 에 못 닿고 다시 H 위로 → H 상승 지점에서 트리거, info = 최저 코드
    polarity 'pos' | 'neg' | 'either'.  → [(trigger_index, '+'|'-', extreme_code)]"""

    def __init__(self, low, high, hys=0.0, polarity="either"):
        assert high - hys > low, "H-hys 가 L 보다 커야 두 비교기 상태가 모순되지 않는다"
        self.L, self.H, self.hys, self.pol = low, high, hys, polarity
        self.cL = self.cH = -1          # -1 = 모름
        self.ap = self.an = False       # +runt / -runt 후보 진행 중
        self.pk = 0

    def process(self, chunk, base=0):
        L, H = self.L, self.H
        Llo, Hlo = L - self.hys, H - self.hys
        cL, cH, ap, an, pk = self.cL, self.cH, self.ap, self.an, self.pk
        want_p = self.pol in ("pos", "either")
        want_n = self.pol in ("neg", "either")
        out = []
        j = base
        for x in chunk:
            pass  # TODO: 상태 = (cL, cH). 바닥(cL 0) → x >= L 이면 가운데로 (+후보 ap, pk = x)
            #       꼭대기(cH 1) → x < Hlo 이면 가운데로 (-후보 an, pk = x)
            #       가운데 → x >= H: an 이면 runt- / x < Llo: ap 이면 runt+ / 그 외 pk 갱신
            #       한 샘플에 풀스윙(바닥→꼭대기, 꼭대기→바닥) 하는 경우도 처리. 첫 샘플(-1)은 상태만
            j += 1
        self.cL, self.cH, self.ap, self.an, self.pk = cL, cH, ap, an, pk
        return out


class TimeoutTrigger:
    """state = 'low' | 'high' | 'either'. 마지막 Schmitt 전이(또는 상태가 처음 정해진 시점)부터
    timeout 샘플 동안 전이가 없으면 '그 순간'(전이 + timeout) 에 트리거. 한 구간에 한 번만.
    → [(trigger_index, 'low'|'high')]"""

    def __init__(self, lo, hi, state="low", timeout=100):
        self.lo, self.hi, self.watch, self.T = lo, hi, state, int(timeout)
        self.state, self.deadline = None, -1

    def process(self, chunk, base=0):
        lo, hi, T, st, dl = self.lo, self.hi, self.T, self.state, self.deadline
        w_lo = self.watch in ("low", "either")
        w_hi = self.watch in ("high", "either")
        out = []
        j = base
        for x in chunk:
            pass  # TODO: Schmitt 전이(또는 처음 상태 결정) 때 감시 상태면 dl = j + T, 아니면 dl = -1
            #       j == dl 이면 out.append((j, "low"/"high")) 후 dl = -1 (한 구간에 한 번)
            j += 1
        self.state, self.deadline = st, dl
        return out


# ---------------------------------------------------------------- Part B: 윈도우 · holdoff
def run_chunked(trig, buf, chunk):
    """같은 트리거 객체에 buf 를 chunk 크기로 잘라 먹인다 (주어진 코드)"""
    out = []
    for a in range(0, len(buf), chunk):
        out.extend(trig.process(buf[a:a + chunk], a))
    return out


def extract_windows(buf, trigs, pre, post, holdoff=None):
    """트리거 인덱스 목록 → (accepted, dropped)
    accepted = [(t, buf[t-pre : t+post])],  dropped = [(t, reason)]
    reason: 'pretrig'  (t < pre: 프리트리거 버퍼가 아직 안 찼다)
            'holdoff'  (직전 채택 트리거 t0 에 대해 t < t0 + holdoff: 아직 재무장 전)
            'end'      (t + post > len(buf): 레코드 끝이라 포스트트리거를 못 채움)
    holdoff 기본값 = pre + post (포스트를 채우고 다시 프리를 채워야 재무장)."""
    acc, drop = [], []
    # TODO: holdoff 가 None 이면 pre + post. 트리거를 정렬해 순서대로:
    #       t < pre → 'pretrig' / 직전 채택 + holdoff 전 → 'holdoff' / t + post > len → 'end'
    #       나머지는 (t, buf[t-pre:t+post]) 채택하고 다음 허용 시각 = t + holdoff
    return acc, drop


# ---------------------------------------------------------------- Part C: 10M 샘플 스캔 · 리포트
def scan_capture(buf, chunk=1 << 20):
    """4개 트리거를 청크 단위로 한 번에 돌린다.
    → (events, n_rise)  events = [{'kind','index', ...}] 인덱스 순 정렬
      kind: 'runt+'(peak) / 'runt-'(trough) / 'glitch'(width) / 'timeout'(level)
      n_rise = 상승 에지 개수 (클록 주기 수 교차 확인용)"""
    events, n_rise = [], 0
    # TODO: EdgeTrigger(EDGE_LO, EDGE_HI, 'rise'), PulseWidthTrigger(.., 'pos', less=GLITCH_MAX),
    #       RuntTrigger(RUNT_L, RUNT_H, RUNT_HYS), TimeoutTrigger(.., 'low', TIMEOUT) 를 한 번씩 만들고
    #       buf 를 chunk 단위로 잘라 네 트리거에 같은 청크를 먹인다 (base = 청크 시작 인덱스).
    #       결과를 dict 로: glitch(width) / runt+(peak) / runt-(trough) / timeout(level), 인덱스 순 정렬
    return events, n_rise


def fmt_event(n, e):
    """이벤트 → 하드웨어 엔지니어가 읽을 한 줄. 시각은 ms(소수 6자리 = 1 ns), 전압은 V 2자리.
    EVT1 RUNT+    @1.357916 ms  peak=1.26 V  (>L 0.80 V, <H 2.00 V)
    EVT2 GLITCH   @4.123415 ms  width=4 ns  (< 10 ns)
    EVT3 TIMEOUT  @5.800176 ms  low >= 200 ns  (clock stalled)
    EVT4 RUNT-    @7.777753 ms  trough=1.32 V  (<H 2.00 V, >L 0.80 V)"""
    # TODO: t = index / FS → ms 6자리, 전압은 gc.code2v(), kind 는 대문자로 %-8s 정렬
    return ""


def window_csv(t, win, pre):
    """윈도우 → CSV 텍스트 (주어진 코드). t_ns 는 트리거 기준 상대 시각 (스코프 export 와 같은 관례)"""
    lines = ["t_ns,code,volts"]
    for k, c in enumerate(win):
        lines.append("%d,%d,%.2f" % (k - pre, c, gc.code2v(c)))
    return "\n".join(lines) + "\n"


def window_name(n, e):
    return "evt%d_%s.csv" % (n, e["kind"].replace("+", "_pos").replace("-", "_neg"))


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


def near(name, got, ref, tol):
    """수치 비교: 측정값과 기대값을 같이 찍는다"""
    def f():
        print("      %s: got=%s ref=%s tol=%s" % (name, got() if callable(got) else got, ref, tol))
        g = got() if callable(got) else got
        return g is not None and abs(g - ref) <= tol
    check(name, f)


def slow_edges(n_pairs, ramp, noise, seed):
    """느린 에지(ramp 샘플 동안 25→190 코드) + 가우시안 잡음(코드 rms) 을 n_pairs 번 상승·하강"""
    rng = random.Random(seed)
    g = gc._Gauss(rng)
    out = []
    for _ in range(n_pairs):
        seg = [25] * 200 + [25 + 165 * k / ramp for k in range(ramp)] + [190] * 200 \
            + [190 - 165 * k / ramp for k in range(ramp)]
        out.extend(int(round(v + noise * g())) for v in seg)
    return out


def mini_capture():
    """정상 3주기 + runt+ / glitch / runt- 주기 + stall 5주기 + 정상 2주기 (청크 경계 테스트용)"""
    rng = random.Random(7)
    g = gc._Gauss(rng)
    kinds = ["normal"] * 3 + ["runt_pos", "normal", "glitch", "normal", "runt_neg"] + ["stall"] * 5 \
        + ["normal"] * 2
    return b"".join(gc._period(rng, g, k) for k in kinds)


def run_tests(save_dir=None):
    print("-- Part A: trigger state machines --")
    s = Schmitt(40, 60)
    check("Schmitt: [0,50,61,55,45,39,50,61] -> 0,0,+1,0,0,-1,0,+1",
          lambda: [s.step(x) for x in [0, 50, 61, 55, 45, 39, 50, 61]] == [0, 0, 1, 0, 0, -1, 0, 1])
    s2 = Schmitt(40, 60)
    check("Schmitt: starts in band -> no edge until state known",
          lambda: [s2.step(x) for x in [50, 55, 70, 30]] == [0, 0, 0, -1])
    sq = [0] * 5 + [100] * 5 + [0] * 5 + [100] * 5
    check("Edge rise on square -> [5, 15]", lambda: EdgeTrigger(40, 60, "rise").process(sq) == [5, 15])
    check("Edge fall on square -> [10]", lambda: EdgeTrigger(40, 60, "fall").process(sq) == [10])
    check("Edge either -> [5, 10, 15]", lambda: EdgeTrigger(40, 60, "either").process(sq) == [5, 10, 15])
    pulses = [0] * 10 + [100] * 3 + [0] * 10 + [100] * 10 + [0] * 10 + [100] * 30 + [0] * 10
    check("PulseWidth pos < 5 -> [(13, 3)]",
          lambda: PulseWidthTrigger(40, 60, "pos", less=5).process(pulses) == [(13, 3)])
    check("PulseWidth pos > 20 -> [(73, 30)]",
          lambda: PulseWidthTrigger(40, 60, "pos", greater=20).process(pulses) == [(73, 30)])
    check("PulseWidth pos 5 < w < 20 -> [(33, 10)]",
          lambda: PulseWidthTrigger(40, 60, "pos", less=20, greater=5).process(pulses) == [(33, 10)])
    check("PulseWidth neg < 11 -> low gaps of 10 (first low has no leading edge)",
          lambda: PulseWidthTrigger(40, 60, "neg", less=11).process(pulses) == [(23, 10), (43, 10)])
    r = [10] * 5 + [50] * 3 + [10] * 5 + [90] * 5 + [50] * 3 + [90] * 5 + [10] * 5 + [90] * 4 + [10] * 3
    check("Runt L=30 H=70: +runt (peak 50) and -runt (trough 50), full pulses ignored",
          lambda: RuntTrigger(30, 70).process(r) == [(8, "+", 50), (21, "-", 50)])
    check("Runt polarity='pos' -> only +",
          lambda: RuntTrigger(30, 70, polarity="pos").process(r) == [(8, "+", 50)])
    t = [100] * 10 + [0] * 100 + [100] * 10 + [0] * 30
    check("Timeout low 40 -> fires at fall(10)+40 = 50, once",
          lambda: TimeoutTrigger(40, 60, "low", 40).process(t) == [(50, "low")])
    check("Timeout either 15 -> [(25,low),(135,low)] (high runs of 10 never time out)",
          lambda: TimeoutTrigger(40, 60, "either", 15).process(t) == [(25, "low"), (135, "low")])
    check("Timeout either 8 -> [(8,high),(18,low),(118,high),(128,low)]",
          lambda: TimeoutTrigger(40, 60, "either", 8).process(t)
          == [(8, "high"), (18, "low"), (118, "high"), (128, "low")])

    print("-- Part B: hysteresis, chunking, timeout vs width, windows --")
    se = slow_edges(5, 400, 2.0, seed=3)      # 400 ns 램프, 잡음 40 mV rms (= 2 코드)
    mid = gc.v2code(1.65)
    e0 = EdgeTrigger(mid, mid, "rise").process(se)                 # 히스테리시스 0 = 단일 임계 비교기
    e1 = EdgeTrigger(mid - 2.5, mid + 2.5, "rise").process(se)     # 0.1 V
    e2 = EdgeTrigger(EDGE_LO, EDGE_HI, "rise").process(se)         # 0.2 V
    print("      slow noisy edges x5: rising count hys 0 / 0.1 / 0.2 V = %d / %d / %d"
          % (len(e0), len(e1), len(e2)))
    check("no hysteresis: single-threshold edge multi-triggers on slow noisy edge (> 5)", lambda: len(e0) > 5)
    check("0.1 V hysteresis < noise p-p (~0.24 V) -> still > 5", lambda: len(e1) > 5)
    check("0.2 V hysteresis -> exactly 5 rising", lambda: len(e2) == 5)
    r0 = RuntTrigger(RUNT_L, RUNT_H, 0).process(se)
    r1 = RuntTrigger(RUNT_L, RUNT_H, 5).process(se)
    r2 = RuntTrigger(RUNT_L, RUNT_H, RUNT_HYS).process(se)
    print("      slow noisy edges x5: false runts hys 0 / 0.1 / 0.2 V = %d / %d / %d" % (len(r0), len(r1), len(r2)))
    check("runt trigger hys 0 and 0.1 V -> false runts on slow edge", lambda: len(r0) > len(r1) > 0)
    check("runt trigger hys 0.2 V -> 0 false runts", lambda: r2 == [])

    mc = mini_capture()

    def all_trigs():
        return [EdgeTrigger(EDGE_LO, EDGE_HI, "either"),
                PulseWidthTrigger(EDGE_LO, EDGE_HI, "pos", less=GLITCH_MAX),
                RuntTrigger(RUNT_L, RUNT_H, RUNT_HYS),
                TimeoutTrigger(EDGE_LO, EDGE_HI, "low", TIMEOUT)]
    ref = [tr.process(mc) for tr in all_trigs()]
    print("      mini capture: %d edges, glitch=%s, runts=%s, timeout=%s" % (len(ref[0]), ref[1], ref[2], ref[3]))
    check("mini capture: 1 glitch, 2 runts, 1 timeout",
          lambda: len(ref[1]) == 1 and [x[1] for x in ref[2]] == ["+", "-"] and len(ref[3]) == 1)
    for cs in (1, 7, 100, 333):
        check("chunk=%d gives identical results to one-shot (state carried)" % cs,
              lambda cs=cs: [run_chunked(tr, mc, cs) for tr in all_trigs()] == ref)

    stall = [0] * 50 + [100] * 50 + [0] * 500 + [100] * 50 + [0] * 50
    pw_gt = PulseWidthTrigger(40, 60, "neg", greater=200).process(stall)
    to = TimeoutTrigger(40, 60, "low", 200).process(stall)
    print("      stall: pulse-width neg>200 fires at %s, timeout low 200 fires at %s" % (pw_gt, to))
    check("pulse '>' fires at trailing edge (600), timeout at fall+200 (300)",
          lambda: pw_gt == [(600, 500)] and to == [(300, "low")])
    stuck = [0] * 50 + [100] * 50 + [0] * 1000
    check("line stuck low forever: pulse-width never fires, timeout does",
          lambda: PulseWidthTrigger(40, 60, "neg", greater=200).process(stuck) == []
          and TimeoutTrigger(40, 60, "low", 200).process(stuck) == [(300, "low")])

    buf = bytes(range(250)) * 40          # 길이 10000
    acc, drop = extract_windows(buf, [9990, 500, 10, 700, 2000], 100, 300)
    check("windows: accepted t = [500, 2000]", lambda: [a[0] for a in acc] == [500, 2000])
    check("windows: dropped 10 pretrig, 700 holdoff, 9990 end",
          lambda: drop == [(10, "pretrig"), (700, "holdoff"), (9990, "end")])
    check("window = buf[t-pre : t+post], len 400, trigger sample at offset pre",
          lambda: len(acc[0][1]) == 400 and acc[0][1][100] == buf[500] and acc[1][1][0] == buf[1900])
    acc2, drop2 = extract_windows(buf, [500, 700], 100, 300, holdoff=150)
    check("holdoff=150 -> 700 accepted", lambda: [a[0] for a in acc2] == [500, 700] and drop2 == [])

    print("-- Part C: 10M-sample capture scan --")
    t0 = time.perf_counter()
    cap, planted = gc.build_capture()
    t1 = time.perf_counter()
    check("capture is deterministic (sha256)", lambda: hashlib.sha256(cap).hexdigest() == CAPTURE_SHA256)
    events, n_rise = scan_capture(cap)
    t2 = time.perf_counter()
    print("      build %.2f s, scan %d samples x 4 triggers %.2f s (%.0f ns/sample)"
          % (t1 - t0, len(cap), t2 - t1, (t2 - t1) / len(cap) * 1e9))
    kinds = [e["kind"] for e in events]
    print("      events: %s" % kinds)
    check("exactly 4 events: runt+, glitch, timeout, runt-",
          lambda: kinds == ["runt+", "glitch", "timeout", "runt-"])
    pl = {p["kind"]: p for p in planted}
    ev = {e["kind"]: e for e in events}
    near("runt+ trigger index - planted center", lambda: ev["runt+"]["index"] - pl["runt_pos"]["index"], 4, 4)
    near("runt+ peak [V]", lambda: gc.code2v(ev["runt+"]["peak"]), 1.30, 0.08)
    near("glitch trigger index - planted center", lambda: ev["glitch"]["index"] - pl["glitch"]["index"], 2, 3)
    near("glitch width [ns]", lambda: ev["glitch"]["width"], 4, 1)
    near("timeout index - (last fall before stall + 200)",
         lambda: ev["timeout"]["index"] - (pl["stall"]["index"] - gc.N_PERIOD + gc.FALL_AT + TIMEOUT), 0, 1)
    near("runt- trough [V]", lambda: gc.code2v(ev["runt-"]["trough"]), 1.30, 0.08)
    check("rising edges = 100000 periods - 20 stalled + 1 glitch + 1 after runt- dip = 99982",
          lambda: n_rise == 99982)
    ev2, nr2 = scan_capture(cap, chunk=999983)
    check("chunk 999983 (prime) on 10M -> same events", lambda: ev2 == events and nr2 == n_rise)

    acc, drop = extract_windows(cap, [e["index"] for e in events], PRE, POST)
    check("all 4 windows complete (%d+%d samples), none dropped" % (PRE, POST),
          lambda: len(acc) == 4 and drop == [] and all(len(w) == PRE + POST for _, w in acc))
    report = [fmt_event(n, e) for n, e in enumerate(events, 1)]
    for line in report:
        print("      " + line)
    check("fmt_event runt+ exact",
          lambda: fmt_event(1, {"kind": "runt+", "index": 1357915, "peak": 90})
          == "EVT1 RUNT+    @1.357915 ms  peak=1.30 V  (>L 0.80 V, <H 2.00 V)")
    check("fmt_event glitch / timeout / runt- exact",
          lambda: fmt_event(2, {"kind": "glitch", "index": 4123414, "width": 4})
          == "EVT2 GLITCH   @4.123414 ms  width=4 ns  (< 10 ns)"
          and fmt_event(3, {"kind": "timeout", "index": 5800175, "level": "low"})
          == "EVT3 TIMEOUT  @5.800175 ms  low >= 200 ns  (clock stalled)"
          and fmt_event(4, {"kind": "runt-", "index": 7777757, "trough": 91})
          == "EVT4 RUNT-    @7.777757 ms  trough=1.32 V  (<H 2.00 V, >L 0.80 V)")

    files = [(window_name(n, e), window_csv(t, w, PRE)) for n, (e, (t, w)) in enumerate(zip(events, acc), 1)]
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        for name, text in files:
            with open(os.path.join(save_dir, name), "w", encoding="utf-8") as f:
                f.write(text)
        print("      saved %d windows to %s" % (len(files), save_dir))

    def committed_match():
        for name, text in files:
            p = os.path.join(HERE, "captures", name)
            with open(p, encoding="utf-8") as f:
                if f.read() != text:
                    return False
        return len(files) == 4
    check("committed captures/*.csv match regenerated windows (stale check)", committed_match)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    save = None
    if len(argv) == 3 and argv[1] == "--save":
        save = argv[2]
    elif len(argv) != 1:
        print("usage: %s [--save DIR]" % argv[0])
        return 2
    return 0 if run_tests(save) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
