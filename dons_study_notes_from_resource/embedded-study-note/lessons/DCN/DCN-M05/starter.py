#!/usr/bin/env python3
"""DCN-M05 PFC 데드락: 순환 버퍼 의존성 탐지

  Part A  4 leaf × 2 spine 패브릭. 경로(path)에서 buffer dependency graph(BDG)를 만들고
          (노드 = (스위치, 입력 포트, 우선순위)), DFS 3색 탐색으로 사이클을 찾는다.
          정상 up-down 라우팅 → 비순환, 링크 2개 장애 후 bounce 경로 → 4-노드 사이클.
  Part B  사이클 단위(cycle-stepped) hop-by-hop PFC 시뮬레이터: 입력 버퍼별 XOFF/XON 히스테리시스,
          PAUSE 1사이클 지연 전파. 혼잡(incast)은 소스가 멈추면 빠지고, CBD 데드락은 소스가 멈춰도
          처리량 0 이 계속된다. 런타임 wait-for 사슬에서 순환 대기를 찾아 출력한다.
  Part C  대책 비교: (1) up-down 재라우팅 (2) 홉별 우선순위 상승(Tagger 아이디어) (3) PFC watchdog.

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1) + 비교 표
Python 3.9 표준 라이브러리만 쓴다. 시간 단위 = 사이클(링크 1개가 패킷 1개를 보내는 시간).
"""
import collections
import sys

LEAVES = ("L0", "L1", "L2", "L3")
SPINES = ("S0", "S1")
HOST = "h0"         # leaf 의 기본 호스트 포트 이름 (호스트마다 h0, h1, ... 입력 버퍼가 따로)
PRIO = 3            # 무손실 우선순위 (RoCE 배포에서 흔히 3 을 쓴다 — 설정 나름)
LOSSY = 0           # 무손실 클래스를 다 쓰면 내려가는 손실 허용 클래스 (PFC 안 함)

XOFF = 8            # 버퍼 점유 ≥ XOFF → 상류로 PAUSE
XON = 4             # PAUSE 중 점유 ≤ XON → RESUME (히스테리시스)
CAP = 12            # 버퍼 크기 = XOFF + headroom (PAUSE 반응 지연 동안 들어오는 패킷, DCN-M04)
LINK_DELAY = 1      # 보낸 패킷은 다음 사이클에 하류 버퍼에 도착
STUCK_W = 50        # 이만큼 연속으로 PAUSE 에 막힌 버퍼를 "멈춤" 으로 본다 (런타임 탐지)


def is_leaf(n):
    return n.startswith("L")


class Topo:
    """leaf-spine 2단 Clos. 모든 leaf 가 모든 spine 과 연결, failed 는 끊어진 링크 목록."""

    def __init__(self, leaves=LEAVES, spines=SPINES, failed=()):
        self.leaves, self.spines = tuple(leaves), tuple(spines)
        self.failed = {frozenset(l) for l in failed}

    def up(self, a, b):
        if is_leaf(a) == is_leaf(b):
            return False
        return frozenset((a, b)) not in self.failed


def path_valid(topo, path):
    return all(topo.up(a, b) for a, b in zip(path, path[1:]))


def ecmp_path(topo, src, dst, h):
    """정상 up-down 경로: src leaf → spine → dst leaf. 살아 있는 spine 중 h 번째 (ECMP 해시 대신)."""
    if src == dst:
        return [src]
    ok = [s for s in topo.spines if topo.up(src, s) and topo.up(s, dst)]
    return [src, ok[h % len(ok)], dst] if ok else None


def buffers_of(path, classes=None, host=HOST):
    """경로가 지나가는 입력 버퍼 목록. i 번째 버퍼 = (path[i], 들어온 포트, 우선순위).
    첫 버퍼의 입력 포트 = 송신 호스트가 붙은 leaf 포트 (h0, h1, ...)."""
    if classes is None:
        classes = [PRIO] * len(path)
    out = []
    for i, sw in enumerate(path):
        out.append((sw, host if i == 0 else path[i - 1], classes[i]))
    return out


class Flow:
    def __init__(self, fid, path, rate=1.0, start=0, stop=None, classes=None, host=HOST):
        self.fid, self.path, self.rate, self.host = fid, list(path), rate, host
        self.start, self.stop = start, stop
        self.bufs = buffers_of(self.path, classes, host)


# ---------------------------------------------------------------- Part A: BDG + 사이클 탐지
def is_up_down(path):
    """2단 Clos 의 up-down 규칙: 내려간 뒤 다시 올라가지 않는다 = 경로 중간에 leaf 가 없다.
    (일반형: tier 가 증가 → 감소만, 계곡(valley) 없음)"""
    # TODO: tier(leaf)=0, tier(spine)=1. 한 번이라도 내려간(b < a) 뒤 다시 올라가면(b > a) False
    return None


def build_bdg(flows):
    """노드 = 버퍼 (sw, in_port, prio). 같은 흐름에서 버퍼 i 다음이 버퍼 i+1 이면 간선 i → i+1
    (i 의 패킷이 i 의 자리를 잡은 채 i+1 의 자리를 기다릴 수 있다). 결과: {노드: 정렬된 후속 목록}."""
    # TODO: 모든 흐름의 f.bufs 에서 연속한 (a, b) 쌍마다 간선 a → b. 후속이 없는 노드도 키로 넣는다
    return {}


def find_cycle(g):
    """DFS 3색 (WHITE 미방문, GRAY 스택 위, BLACK 끝남). GRAY 로의 간선 = back edge = 사이클.
    재귀 대신 명시 스택. 찾으면 사이클 노드 목록(최소 노드부터 회전), 없으면 None."""
    # TODO: sorted(g) 순서로 루트를 고르고, (node, iterator) 스택 + 현재 경로 path 로 DFS.
    #       GRAY 노드로 가는 간선을 만나면 path 에서 그 노드부터 끝까지가 사이클 → 최소 노드부터 회전
    return None


def topo_order(g):
    """Kahn 위상 정렬 (비순환이면 전체 순서, 사이클이 있으면 None) — 비순환 증명 출력용."""
    indeg = {n: 0 for n in g}
    for n in g:
        for m in g[n]:
            indeg[m] += 1
    ready = sorted(n for n in g if indeg[n] == 0)
    out = []
    while ready:
        n = ready.pop(0)
        out.append(n)
        for m in g[n]:
            indeg[m] -= 1
            if indeg[m] == 0:
                ready.append(m)
        ready.sort()
    return out if len(out) == len(g) else None


# ---------------------------------------------------------------- Part B: PFC 시뮬레이터
class Sim:
    """사이클 단위 시뮬레이터. 한 사이클:
      1. 도착: 전 사이클에 보낸 패킷이 하류 입력 버퍼에 들어간다 (CAP 넘으면 overflow drop — 무손실이면 0 이어야)
      2. 전송: 출구 포트(스위치, 다음 홉)마다 1 패킷. 후보 = 그 출구로 가는 입력 버퍼의 맨 앞 패킷(FIFO)
               중 하류 버퍼가 PAUSE 아닌 것. 라운드 로빈. PAUSE 는 전 사이클 끝의 상태 = 1사이클 지연.
      3. 호스트 송신: 흐름별 rate 로 생성, 호스트 링크 1 패킷/사이클, 첫 버퍼가 PAUSE 면 대기
      4. watchdog (켜져 있으면)
      5. PAUSE 상태 갱신 (XOFF/XON 히스테리시스)
    """

    def __init__(self, flows, xoff=XOFF, xon=XON, cap=CAP, watchdog=None):
        self.flows = {f.fid: f for f in flows}
        self.xoff, self.xon, self.cap = xoff, xon, cap
        self.watchdog = watchdog            # None 또는 연속 PAUSE 사이클 임계값 T
        self.buf = collections.defaultdict(collections.deque)   # 노드 → deque of (fid, idx)
        self.paused = {}                    # 노드 → 이 버퍼가 상류에 PAUSE 를 걸고 있나
        self.age = collections.defaultdict(int)   # 맨 앞 패킷이 PAUSE 에 연속으로 막힌 사이클 수
        self.pipe = collections.deque()     # (도착 사이클, 노드, 패킷)
        self.rr = collections.defaultdict(int)
        self.credit = {f.fid: 0.0 for f in flows}
        self.hostq = {f.fid: 0 for f in flows}
        self.t = 0
        self.injected = self.delivered = 0
        self.drop_overflow = self.drop_wd = self.drop_lossy = 0
        self.pause_events = 0
        self.max_occ = 0
        self.max_age = 0                    # 관찰된 가장 긴 연속 PAUSE 막힘 (watchdog T 고를 때 본다)
        self.wd_fires = 0
        self.deliv_log = []                 # 사이클별 배달 수

    def next_node(self, pkt):
        fid, idx = pkt
        bufs = self.flows[fid].bufs
        return bufs[idx + 1] if idx + 1 < len(bufs) else None

    def occupancy(self):
        return sum(len(q) for q in self.buf.values()) + len(self.pipe)

    def _update_pause(self, node):
        """XOFF/XON 히스테리시스. LOSSY 클래스는 PAUSE 를 걸지 않는다."""
        # TODO: LOSSY 면 항상 False. 아니면 q = len(self.buf[node]);
        #       안 멈춘 상태에서 q >= xoff → True (pause_events += 1), 멈춘 상태에서 q <= xon → False
        pass

    def _arrive(self, node, pkt):
        if len(self.buf[node]) >= self.cap:
            if node[2] == LOSSY:
                self.drop_lossy += 1
            else:
                self.drop_overflow += 1
            return
        self.buf[node].append(pkt)
        self.max_occ = max(self.max_occ, len(self.buf[node]))

    def step(self):
        t = self.t
        while self.pipe and self.pipe[0][0] <= t:
            _, node, pkt = self.pipe.popleft()
            self._arrive(node, pkt)
        # 2. 전송 — 출구별 후보 모으기
        cand = collections.defaultdict(list)
        for node in sorted(n for n, q in self.buf.items() if q):
            nxt = self.next_node(self.buf[node][0])
            if nxt is not None and self.paused.get(nxt):
                self.age[node] += 1          # PAUSE 에 막힘
                self.max_age = max(self.max_age, self.age[node])
                continue
            self.age[node] = 0
            cand[(node[0], nxt[0] if nxt else HOST)].append(node)
        delivered = 0
        for egress in sorted(cand):
            nodes = cand[egress]
            pick = nodes[self.rr[egress] % len(nodes)]
            self.rr[egress] += 1
            fid, idx = self.buf[pick].popleft()
            nxt = self.next_node((fid, idx))
            if nxt is None:
                delivered += 1
            else:
                self.pipe.append((t + LINK_DELAY, nxt, (fid, idx + 1)))
        self.delivered += delivered
        self.deliv_log.append(delivered)
        # 3. 호스트 송신
        by_src = collections.defaultdict(list)
        for f in self.flows.values():
            if f.start <= t and (f.stop is None or t < f.stop):
                self.credit[f.fid] += f.rate
                while self.credit[f.fid] >= 1.0:
                    self.credit[f.fid] -= 1.0
                    self.hostq[f.fid] += 1
            if self.hostq[f.fid] and not self.paused.get(f.bufs[0]):
                by_src[(f.path[0], f.host)].append(f.fid)
        for src in sorted(by_src):
            fids = sorted(by_src[src])
            fid = fids[self.rr[("host", src)] % len(fids)]
            self.rr[("host", src)] += 1
            self.hostq[fid] -= 1
            self.injected += 1
            self.pipe.append((t + LINK_DELAY, self.flows[fid].bufs[0], (fid, 0)))
        # 4. watchdog
        if self.watchdog is not None:
            self._watchdog()
        # 5. PAUSE 갱신
        for node in list(self.buf):
            self._update_pause(node)
        self.t += 1

    def wait_cycle(self, w=STUCK_W):
        """런타임 순환 대기: PAUSE 에 w 사이클 이상 막힌 버퍼 b 에서 '맨 앞 패킷이 기다리는 버퍼' 를
        따라간다. 다시 만난 노드가 있으면 그 고리가 순환 대기. 없으면 None. (최소 노드부터 회전)"""
        # TODO: stuck = 비어 있지 않고 self.age[n] >= w 인 버퍼. 각 stuck 버퍼에서
        #       n → self.next_node(self.buf[n][0]) 를 stuck 안에서 따라가다 같은 노드를 다시 만나면 그 고리
        return None

    def _watchdog(self):
        """PFC watchdog: 맨 앞 패킷이 T 사이클 연속 PAUSE 에 막힌 큐는 내용을 버린다 (drop_wd).
        "빼앗을 수 없음(no preemption)" 을 깨는 대책 — 대가는 무손실 클래스에서의 손실."""
        # TODO: 비어 있지 않고 age >= self.watchdog 인 버퍼 → drop_wd += 길이, wd_fires += 1, 비우고 age = 0
        pass


def experiment(flows, on=600, drain=600, watchdog=None, xoff=XOFF, xon=XON, cap=CAP):
    """on 사이클 동안 소스를 켰다가 끄고 drain 사이클 더 돌린다. 데드락은 STUCK_W 사이클 지속 시 선언."""
    for f in flows:
        f.stop = on if f.stop is None else min(f.stop, on)
    sim = Sim(flows, xoff=xoff, xon=xon, cap=cap, watchdog=watchdog)
    first_dl, cyc, empty_at = None, None, None
    for _ in range(on + drain):
        sim.step()
        if sim.t == on:                     # 소스 정지: 아직 NIC 에서 못 나간 패킷은 보내지 않는다
            sim.hostq = {k: 0 for k in sim.hostq}
        if first_dl is None:
            c = sim.wait_cycle()
            if c:
                first_dl, cyc = sim.t, c
        if empty_at is None and sim.t >= on and sim.occupancy() == 0:
            empty_at = sim.t - on           # 소스 정지 후 패브릭이 비기까지 걸린 사이클
    residual = sim.occupancy()
    return {
        "sim": sim, "deadlock_at": first_dl, "cycle": cyc,
        "injected": sim.injected, "delivered": sim.delivered,
        "dropped": sim.drop_overflow + sim.drop_wd + sim.drop_lossy,
        "drop_wd": sim.drop_wd, "drop_overflow": sim.drop_overflow, "drop_lossy": sim.drop_lossy,
        "residual": residual, "empty_at": empty_at, "after_stop": sum(sim.deliv_log[on:]),
        "tput_on": sum(sim.deliv_log[on - 200:on]) / 200.0,
        "max_occ": sim.max_occ, "pause_events": sim.pause_events,
        "max_age": sim.max_age, "wd_fires": sim.wd_fires,
    }


# ---------------------------------------------------------------- 시나리오
FAILED = (("S0", "L3"), ("S1", "L0"))


F1_PATH = ["L2", "S0", "L1", "S1", "L3"]     # L2→L3, S0 에서 L1 로 bounce
F2_PATH = ["L1", "S1", "L2", "S0", "L0"]     # L1→L0, S1 에서 L2 로 bounce


def bounce_flows(n_hosts=2, rate=1.0):
    """링크 S0–L3, S1–L0 장애 직후. leaf 의 ECMP 는 아직 옛 spine 을 쓰고(수렴 전),
    spine 은 로컬 복구로 이웃 leaf 에 되튕겨 보낸다 (leaf → spine → leaf → spine → leaf).
    각 경로에 호스트 n_hosts 대가 rate 로 보낸다 (기본: 2대 × 선로 속도 = 링크 2배 수요)."""
    out = []
    for h in range(n_hosts):
        out.append(Flow("F1.%d" % h, F1_PATH, rate, host="h%d" % h))
        out.append(Flow("F2.%d" % h, F2_PATH, rate, host="h%d" % h))
    return out


def incast_flows():
    """정상 패브릭, 3 → 1 incast (L0, L1, L2 → L3): 혼잡이지 데드락이 아니다."""
    topo = Topo()
    return [Flow("I%d" % i, ecmp_path(topo, s, "L3", i)) for i, s in enumerate(("L0", "L1", "L2"))]


# ---------------------------------------------------------------- Part C: 대책
def updown_reroute(topo, flows):
    """대책 1: up-down 을 어기는 경로를 버리고, 살아 있는 링크로 된 up-down 경로로 다시 고른다.
    up-down 경로가 없는 목적지는 None (도달 불가 — 버리는 게 데드락보다 낫다)."""
    # TODO: 흐름 i 의 경로가 up-down 이고 살아 있으면 그대로, 아니면 ecmp_path(topo, src, dst, i).
    #       새 Flow 는 fid·rate·host 를 유지한다. 경로가 없으면 그 자리에 None
    return None


def bump_classes(path, base=PRIO, n_lossless=2):
    """대책 2 (Tagger 아이디어): bounce(경로 중간 leaf 에서 다시 올라감) 할 때마다 우선순위 +1.
    bounce 지점 leaf 의 버퍼는 옛 클래스, 그다음 버퍼부터 새 클래스. 무손실 클래스는 base..base+n-1,
    넘치면 LOSSY. 결과: 버퍼별 클래스 목록."""
    # TODO: c = base 로 시작. 버퍼 i 의 클래스 = c (c >= base + n_lossless 면 LOSSY).
    #       경로 중간(0 < i < len-1)의 leaf 를 지난 뒤에 c += 1
    return None


def bump_flows(flows, n_lossless=2):
    return [Flow(f.fid, f.path, f.rate, classes=bump_classes(f.path, PRIO, n_lossless), host=f.host)
            for f in flows]


# ---------------------------------------------------------------- 하네스
g_pass = g_total = 0


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


def safe(fn, *a, **kw):
    try:
        return fn(*a, **kw)
    except Exception:  # noqa: BLE001
        return None


def fmt_node(n):
    return "(%s,%s,p%d)" % n


def fmt_cycle(c):
    return " -> ".join(fmt_node(n) for n in c) + " -> " + fmt_node(c[0]) if c else "None"


def conserved(r):
    s = r["sim"]
    return r["injected"] == r["delivered"] + r["dropped"] + r["residual"] and s.drop_overflow == 0


EXPECT_CYCLE = [("L1", "S0", 3), ("S1", "L1", 3), ("L2", "S1", 3), ("S0", "L2", 3)]


def run_tests():
    healthy = Topo()
    failed = Topo(failed=FAILED)
    print("-- Part A: buffer dependency graph --")
    pairs = [(s, d) for s in LEAVES for d in LEAVES if s != d]
    allpaths = [ecmp_path(healthy, s, d, h) for s, d in pairs for h in (0, 1)]
    check("healthy fabric: 24 ECMP paths (12 pairs x 2 spines) all valid and up-down",
          lambda: len(allpaths) == 24 and all(path_valid(healthy, p) and is_up_down(p) for p in allpaths))
    check("is_up_down: bounce path L2-S0-L1-S1-L3 is NOT up-down; L0-S0-L1 and [L0] are",
          lambda: is_up_down(["L2", "S0", "L1", "S1", "L3"]) is False
          and is_up_down(["L0", "S0", "L1"]) is True and is_up_down(["L0"]) is True)
    check("find_cycle: diamond DAG a->b,a->c,b->d,c->d has NO cycle (visited != on-stack)",
          lambda: find_cycle({"a": ["b", "c"], "b": ["d"], "c": ["d"], "d": []}) is None)
    check("find_cycle: self-loop x->x and 3-ring found",
          lambda: find_cycle({"x": ["x"]}) == ["x"]
          and find_cycle({"a": ["b"], "b": ["c"], "c": ["a"], "z": ["a"]}) == ["a", "b", "c"])
    g_ok = safe(build_bdg, [Flow("P%d" % i, p) for i, p in enumerate(allpaths)]) or {}
    nedge = sum(len(v) for v in g_ok.values())
    order = topo_order(g_ok) if g_ok else None
    check("healthy BDG: 20 nodes, 32 edges, acyclic (find_cycle None, topo order exists)",
          lambda: len(g_ok) == 20 and nedge == 32 and find_cycle(g_ok) is None and order is not None)
    if order:
        print("      topo order starts: %s ..." % ", ".join(fmt_node(n) for n in order[:6]))
    bf = bounce_flows()
    check("after failures S0-L3, S1-L0: bounce paths valid, direct L2-S0-L3 / L1-S1-L0 invalid",
          lambda: all(path_valid(failed, f.path) for f in bf)
          and not path_valid(failed, ["L2", "S0", "L3"]) and not path_valid(failed, ["L1", "S1", "L0"]))
    g_bad = safe(build_bdg, bf) or {}
    cyc = safe(find_cycle, g_bad)
    print("      static CBD: %s" % fmt_cycle(cyc))
    check("bounce BDG: cycle (L1,S0)->(S1,L1)->(L2,S1)->(S0,L2) found and every edge exists",
          lambda: cyc == EXPECT_CYCLE and all(cyc[(i + 1) % 4] in g_bad[cyc[i]] for i in range(4)))
    f1_only = [f for f in bf if f.path == F1_PATH]
    check("bounce path F1 alone (2 hosts) is a chain, not a cycle: 6 nodes, 5 edges",
          lambda: find_cycle(build_bdg(f1_only)) is None and len(build_bdg(f1_only)) == 6
          and sum(len(v) for v in build_bdg(f1_only).values()) == 5)

    print("-- Part B: PFC simulator --")
    s = Sim([Flow("U", ["L0"])])
    node = ("L0", HOST, PRIO)
    trace = []
    for q in (0, 5, 7, 8, 9, 6, 5, 4, 3, 8):
        s.buf[node] = collections.deque([("U", 0)] * q)
        safe(s._update_pause, node)
        trace.append(1 if s.paused.get(node) else 0)
    check("XOFF=8/XON=4 hysteresis: q 0,5,7,8,9,6,5,4,3,8 -> pause 0,0,0,1,1,1,1,0,0,1  got %s" % trace,
          lambda: trace == [0, 0, 0, 1, 1, 1, 1, 0, 0, 1])
    r = experiment(incast_flows())
    print("      incast 3->1: injected %d delivered %d pause_events %d max_occ %d tput_on %.2f empty %s cyc after stop"
          % (r["injected"], r["delivered"], r["pause_events"], r["max_occ"], r["tput_on"], r["empty_at"]))
    check("incast (congestion): PAUSE fires, max occupancy <= CAP, zero drops, drains to 0, no deadlock",
          lambda: r["pause_events"] > 0 and r["max_occ"] <= CAP and r["dropped"] == 0
          and r["residual"] == 0 and r["deadlock_at"] is None and conserved(r))
    check("incast: bottleneck L3 host port runs at line rate (tput_on = 1.00)",
          lambda: r["tput_on"] == 1.0)
    dl = experiment(bounce_flows())
    print("      bounce: deadlock declared at t=%s, runtime wait cycle: %s"
          % (dl["deadlock_at"], fmt_cycle(dl["cycle"])))
    print("      bounce: injected %d delivered %d residual %d (in fabric) tput_on %.2f delivered_after_stop %d"
          % (dl["injected"], dl["delivered"], dl["residual"], dl["tput_on"], dl["after_stop"]))
    check("bounce: deadlock detected at runtime, cycle == static CBD cycle",
          lambda: dl["deadlock_at"] is not None and dl["cycle"] == EXPECT_CYCLE)
    check("bounce: symptom = throughput 0 AFTER sources stop, packets stuck, yet zero drops (lossless)",
          lambda: dl["after_stop"] == 0 and dl["empty_at"] is None and dl["residual"] > 0 and dl["dropped"] == 0 and conserved(dl))
    st = dl["sim"]
    occ = [len(st.buf[n]) for n in EXPECT_CYCLE]
    check("bounce: every cycle buffer asserts PAUSE and sits above XON (can never resume) occ=%s" % occ,
          lambda: all(st.paused.get(n) and len(st.buf[n]) > XON for n in EXPECT_CYCLE))
    one = experiment([f for f in bounce_flows() if f.path == F1_PATH])
    check("bounce path F1 alone, 2 hosts at line rate: no deadlock, drains (CBD needs the cycle)",
          lambda: one["deadlock_at"] is None and one["residual"] == 0 and conserved(one))

    print("-- Part C: mitigations --")
    rows = [("none (bounce routes)", dl)]
    ud = safe(updown_reroute, failed, bounce_flows())
    check("updown_reroute: F1 -> L2-S1-L3, F2 -> L1-S0-L0, BDG acyclic",
          lambda: [f.path for f in ud] == [["L2", "S1", "L3"], ["L1", "S0", "L0"]] * 2
          and find_cycle(build_bdg(ud)) is None)
    r1 = experiment(ud) if ud and all(ud) else None
    rows.append(("(1) up-down reroute", r1))
    check("(1) up-down: no deadlock, all delivered, 0 drops",
          lambda: r1["deadlock_at"] is None and r1["residual"] == 0 and r1["dropped"] == 0 and conserved(r1))
    cls = safe(bump_classes, ["L2", "S0", "L1", "S1", "L3"])
    check("bump_classes(F1 path) = [3,3,3,4,4]; 2-bounce with 2 classes -> last hops LOSSY",
          lambda: cls == [3, 3, 3, 4, 4]
          and bump_classes(["L2", "S0", "L1", "S1", "L0", "S0", "L3"]) == [3, 3, 3, 4, 4, 0, 0])
    bfl = bump_flows(bounce_flows()) if cls else []
    r2 = experiment(bfl) if cls else None
    rows.append(("(2) per-hop prio bump", r2))
    check("(2) prio bump: BDG acyclic on the SAME bounce paths, no deadlock, 0 drops",
          lambda: find_cycle(build_bdg(bfl)) is None and r2["deadlock_at"] is None
          and r2["residual"] == 0 and r2["dropped"] == 0 and conserved(r2))
    r3 = experiment(bounce_flows(), watchdog=100)
    rows.append(("(3) PFC watchdog T=100", r3))
    check("(3) watchdog T=100: deadlock broken (residual 0 after drain) at the cost of drops",
          lambda: r3["residual"] == 0 and r3["drop_wd"] > 0 and conserved(r3))
    check("(3) watchdog: lossless promise broken, (1)/(2) keep it (drops 0)",
          lambda: r3["dropped"] > 0 and all(r["dropped"] == 0 for name, r in rows[1:3]))
    rows = [(name, r) for name, r in rows if r is not None]

    print("")
    print("%-24s %8s %9s %7s %8s %7s %8s %9s %s" % ("mitigation", "injected", "delivered", "dropped",
                                                     "residual", "tput_on", "wd_fires", "deadlock@", "empty@"))
    for name, r in rows:
        print("%-24s %8d %9d %7d %8d %7.2f %8d %9s %s" % (
            name, r["injected"], r["delivered"], r["dropped"], r["residual"], r["tput_on"],
            r["wd_fires"], r["deadlock_at"], r["empty_at"]))
    print("")
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
