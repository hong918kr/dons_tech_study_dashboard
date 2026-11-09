#!/usr/bin/env python3
"""SOCX-M05 NoC 라우팅 데드락: 채널 의존 그래프

  Part A  토폴로지(k×k mesh / k-ary 2-cube torus / k-ring) + 채널 = 방향 링크 × VC.
          라우팅 함수 R(src, cur, dst, in_ch) → 다음 채널 후보. CDG 를 "도달 가능한 상태"로만 만들고
          DFS 로 사이클을 찾는다. mesh XY → 비순환 (위상 순서 출력).
  Part B  무제한 최소 경로(완전 적응, VC 없음) → 사이클. torus DOR → 링마다 사이클.
          dateline VC 2개 → 비순환. 토폴로지만 보고 그린 naive CDG 는 거짓 사이클을 만든다.
  Part C  사이클 단위 wormhole 시뮬레이터 (입력 VC 버퍼, credit = 빈 칸, VC 소유권).
          심어 둔 트래픽이 VC 없이 데드락 → 대기 그래프의 순환 대기를 보고. dateline VC 로 해소.

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1)
Python 3 표준 라이브러리만 쓴다. 시뮬레이터는 max_cycles 로 끝나므로 데드락이어도 멈추지 않는다.
"""
import collections
import heapq
import sys

# ---------------------------------------------------------------- 토폴로지 (주어짐)
# 방향: E = +x, W = −x, N = +y, S = −y.  노드 = (x, y).  1-D ring 은 y = 0 만 쓴다.
DIRS = {"E": (1, 0), "W": (-1, 0), "N": (0, 1), "S": (0, -1)}
AXIS = {"E": 0, "W": 0, "N": 1, "S": 1}

# 채널 = 방향 링크 하나의 VC 하나 = 하류 라우터의 입력 VC 버퍼 하나
Channel = collections.namedtuple("Channel", "src dst dir vc")


def cname(c):
    return "(%d,%d)->(%d,%d).v%d" % (c.src[0], c.src[1], c.dst[0], c.dst[1], c.vc)


class Topo:
    """k×k mesh (wrap=False), k-ary 2-cube torus (wrap=True), dims=1 이면 k-노드 ring/line."""

    def __init__(self, k, wrap, nvc=1, dims=2):
        self.k, self.wrap, self.nvc, self.dims = k, wrap, nvc, dims
        ys = range(k) if dims == 2 else range(1)
        self.nodes = [(x, y) for y in ys for x in range(k)]
        self.dirs = ["E", "W", "N", "S"] if dims == 2 else ["E", "W"]

    def hop(self, n, d):
        """n 에서 d 방향 이웃 (없으면 None). torus 는 끝에서 반대편으로 감긴다 (wraparound)."""
        if d not in self.dirs:
            return None
        dx, dy = DIRS[d]
        x, y = n[0] + dx, n[1] + dy
        if self.wrap:
            return (x % self.k, y % self.k)
        if 0 <= x < self.k and 0 <= y < self.k:
            return (x, y)
        return None

    def ch(self, n, d, vc=0):
        m = self.hop(n, d)
        assert m is not None and 0 <= vc < self.nvc, (n, d, vc)
        return Channel(n, m, d, vc)

    def channels(self):
        out = []
        for n in self.nodes:
            for d in self.dirs:
                if self.hop(n, d) is not None:
                    for v in range(self.nvc):
                        out.append(Channel(n, self.hop(n, d), d, v))
        return sorted(out)

    def is_wrap(self, n, d):
        """dateline = 각 링의 wraparound 링크 (E: x=k-1→0, W: x=0→k-1, N: y=k-1→0, S: y=0→k-1)"""
        if not self.wrap:
            return False
        i = AXIS[d]
        return (DIRS[d][i] > 0 and n[i] == self.k - 1) or (DIRS[d][i] < 0 and n[i] == 0)


def ring_dir(k, a, b):
    """링 위 a → b 최단 방향: +1 / −1 / 0. 거리가 같으면(k 짝수, k/2) + 로 정한다 (결정적)."""
    d = (b - a) % k
    if d == 0:
        return 0
    return 1 if d <= k - d else -1


# ---------------------------------------------------------------- Part A: 라우팅 · CDG · 사이클
def route_xy(topo, src, cur, dst, in_ch):
    """mesh 차원 순서 라우팅(DOR, XY): X 를 먼저 끝까지, 그다음 Y. 후보는 항상 1개."""
    # TODO: dx = dst.x − cur.x 가 0 이 아니면 E/W 채널 1개, 아니면 N/S 채널 1개 (topo.ch 사용)
    raise NotImplementedError("route_xy")


def build_cdg(topo, route):
    """채널 의존 그래프: c1 → c2 ⇔ 어떤 패킷이 c1 을 잡은 채 c2 를 요청할 수 있다.
    모든 (src, dst) 쌍에 대해 패킷이 실제로 갈 수 있는 채널만 따라가며(도달 가능 상태) 간선을 모은다.
    반환: {channel: set(channel)} — 모든 채널이 키로 있다 (안 쓰이는 채널은 빈 집합)."""
    # TODO: 모든 (s, d) 쌍마다: 첫 채널 = route(topo, s, s, d, None).
    #       스택으로 채널 c 를 꺼내 c.dst == d 면 소비(끝), 아니면 route(topo, s, c.dst, d, c) 의
    #       후보 c2 마다 간선 c → c2 를 넣고 처음 본 c2 는 스택에. 후보가 비면 ValueError (막다른 라우팅).
    raise NotImplementedError("build_cdg")


def find_cycle(g):
    """방향 그래프 g({v: iterable}) 에서 사이클 하나를 [v0, v1, ..., vm-1] 로 (vm-1 → v0 로 닫힘), 없으면 None.
    색 3개 DFS: WHITE 미방문, GRAY 스택 위(조상), BLACK 끝남. GRAY 로 가는 간선 = back edge = 사이클.
    재귀 대신 명시적 스택 (채널 수천 개도 recursion limit 에 안 걸리게). 순회 순서는 정렬로 결정적."""
    # TODO: WHITE/GRAY/BLACK 색 + 명시적 스택 DFS. 루트와 이웃은 sorted() 순서로.
    #       GRAY 정점으로 가는 간선을 만나면 path 에서 그 정점부터 끝까지를 반환. 다 돌면 None.
    raise NotImplementedError("find_cycle")


def topo_order(g):
    """Kahn 알고리즘 위상 순서 (같은 단계에서는 작은 것부터 → 결정적). 사이클이면 None."""
    # TODO: 진입 차수 0 인 정점을 heapq 에 넣고 꺼낼 때마다 이웃의 차수를 깎는다.
    #       꺼낸 정점 수 < 전체 정점 수 이면 사이클 → None.
    raise NotImplementedError("topo_order")


def naive_cdg(topo):
    """(주어짐 — 비교용) 라우팅을 무시하고 '노드로 들어오는 모든 채널 → 나가는 모든 채널(U턴 제외)'.
    토폴로지만 보고 그린 그래프라서 거의 항상 순환이다 — CDG 는 라우팅 함수의 성질이다."""
    rev = {"E": "W", "W": "E", "N": "S", "S": "N"}
    g = {c: set() for c in topo.channels()}
    for c in g:
        for c2 in g:
            if c2.src == c.dst and c2.dir != rev[c.dir]:
                g[c].add(c2)
    return g


def edges(g):
    return sum(len(v) for v in g.values())


def used_channels(topo, route):
    """(주어짐) 어떤 (src, dst) 경로에라도 나오는 채널 집합 — 경로를 직접 따라간다 (CDG 와 독립)."""
    used = set()
    for s in topo.nodes:
        for d in topo.nodes:
            stack = list(route(topo, s, s, d, None)) if s != d else []
            seen = set(stack)
            while stack:
                c = stack.pop()
                used.add(c)
                if c.dst != d:
                    for c2 in route(topo, s, c.dst, d, c):
                        if c2 not in seen:
                            seen.add(c2)
                            stack.append(c2)
    return used


def is_cycle(g, cyc):
    return bool(cyc) and all(cyc[(i + 1) % len(cyc)] in g[cyc[i]] for i in range(len(cyc)))


# ---------------------------------------------------------------- Part B: 적응 · torus · dateline
def route_minimal_adaptive(topo, src, cur, dst, in_ch):
    """mesh 무제한 최소 경로 (완전 적응, VC 없음): 거리를 줄이는 방향이면 모두 후보."""
    # TODO: dx ≠ 0 이면 E/W 하나, dy ≠ 0 이면 N/S 하나 — 둘 다 후보 리스트에.
    raise NotImplementedError("route_minimal_adaptive")


def route_torus_dor(topo, src, cur, dst, in_ch):
    """torus 기본 DOR: X 링에서 최단 방향(동률이면 +)으로 끝까지, 그다음 Y 링. VC 0 하나."""
    # TODO: ring_dir(k, cur.x, dst.x) 가 0 이 아니면 E(+)/W(−), 아니면 ring_dir 로 N(+)/S(−). VC 0.
    raise NotImplementedError("route_torus_dor")


def route_torus_dateline(topo, src, cur, dst, in_ch):
    """DOR + dateline VC 2개: 한 차원 안에서 VC0 으로 가다가 wraparound(dateline) 링크를 건너는 순간 VC1,
    그 차원이 끝날 때까지 VC1 유지. 차원을 바꾸면(X→Y) 다시 VC0 부터."""
    # TODO: route_torus_dor 로 방향을 정하고 VC 를 고른다:
    #       같은 축에서 VC1 로 들어왔으면 VC1 유지, topo.is_wrap(cur, dir) 이면 VC1, 나머지 VC0.
    raise NotImplementedError("route_torus_dateline")


# ---------------------------------------------------------------- Part C: wormhole 시뮬레이터
Packet = collections.namedtuple("Packet", "pid src dst length")
EJECT = "EJECT"


class WormholeSim:
    """사이클 단위 wormhole 라우터 네트워크.
    - 채널 c 마다 하류 라우터(c.dst)에 입력 VC 버퍼 depth 칸. 한 VC 버퍼는 한 번에 한 패킷만 (owner).
    - head flit 이 버퍼 앞에 오면 route() 후보 중 owner 가 없는 첫 VC 를 잡는다(VC 할당). tail 이 그 버퍼를
      떠나야 owner 해제 (보수적·atomic VC 재할당).
    - 물리 링크 하나 = 사이클당 flit 1개 (VC 끼리 라운드로빈). credit = 사이클 시작 때 하류 버퍼의 빈 칸.
    - 목적지에서는 항상 소비 (ejection 무제한) — Dally–Seitz 의 consumption assumption.
    - 매 사이클 flit 이 하나도 안 움직였거나 어떤 버퍼 앞 flit 이 stall_window 사이클 넘게 멈췄으면
      wait_for_graph() 에서 순환 대기를 찾아 데드락으로 보고하고 멈춘다. max_cycles 에서 무조건 끝."""

    def __init__(self, topo, route, depth=2, stall_window=16, max_cycles=5000):
        self.topo, self.route, self.depth = topo, route, depth
        self.W, self.max_cycles = stall_window, max_cycles
        self.chs = topo.channels()
        self.buf = {c: collections.deque() for c in self.chs}     # flit = (pid, is_head, is_tail)
        self.owner = {c: None for c in self.chs}
        self.nxt = {}            # 입력(채널 또는 ("INJ", node)) → 앞 패킷에 할당된 출력 채널 / EJECT
        self.age = {}            # 입력 → 앞 flit 이 멈춰 있던 사이클 수
        self.inj = {n: collections.deque() for n in topo.nodes}   # 주입 대기 패킷
        self.sent = {}           # pid → 주입된 flit 수
        self.pk = {}
        self.delivered = 0
        self.total = 0
        self.t = 0
        self.arrivals = collections.defaultdict(list)   # 사이클 → 그때 생기는 패킷들 (demo 의 랜덤 트래픽)

    def add(self, pid, src, dst, length, at=0):
        if at > 0:
            self.arrivals[at].append((pid, src, dst, length))
            return
        p = Packet(pid, src, dst, length)
        self.pk[pid] = p
        self.inj[src].append(p)
        self.sent[pid] = 0
        self.total += length

    # 입력 = 채널 버퍼들 (정렬) + 노드별 주입 큐
    def inputs(self):
        return list(self.chs) + [("INJ", n) for n in self.topo.nodes]

    def front(self, i):
        """입력 i 의 앞 flit (pid, is_head, is_tail), 현재 노드, 들어온 채널(주입이면 None)"""
        if isinstance(i, Channel):
            return (self.buf[i][0] if self.buf[i] else None), i.dst, i
        q = self.inj[i[1]]
        if not q:
            return None, i[1], None
        p = q[0]
        k = self.sent[p.pid]
        return (p.pid, k == 0, k == p.length - 1), i[1], None

    def candidates(self, i):
        f, cur, in_ch = self.front(i)
        p = self.pk[f[0]]
        if cur == p.dst:
            return [EJECT]
        return self.route(self.topo, p.src, cur, p.dst, in_ch)

    def step(self):
        ins = self.inputs()
        # 1) head flit 의 라우팅 + VC 할당 (고정 순서 → 결정적)
        for i in ins:
            f, cur, in_ch = self.front(i)
            if f is None or i in self.nxt or not f[1]:
                continue
            for c in self.candidates(i):
                if c == EJECT or self.owner[c] is None:
                    if c != EJECT:
                        self.owner[c] = f[0]
                    self.nxt[i] = c
                    break
        # 2) 스위치: 링크당 1 flit, 하류 빈 칸은 사이클 시작 기준 (= 1 사이클 credit 지연)
        space = {c: self.depth - len(self.buf[c]) for c in self.chs}
        busy, moves = set(), []
        n = len(ins)
        for j in range(n):
            i = ins[(j + self.t) % n]                 # 라운드로빈 시작점
            f = self.front(i)[0]
            if f is None or i not in self.nxt:
                continue
            o = self.nxt[i]
            if o == EJECT:
                moves.append((i, o))
                continue
            link = (o.src, o.dst)
            if link in busy or space[o] <= 0:
                continue
            busy.add(link)
            space[o] -= 1
            moves.append((i, o))
        # 3) 적용
        moved = set()
        for i, o in moves:
            if isinstance(i, Channel):
                f = self.buf[i].popleft()
            else:
                p = self.inj[i[1]][0]
                k = self.sent[p.pid]
                f = (p.pid, k == 0, k == p.length - 1)
                self.sent[p.pid] = k + 1
                if f[2]:
                    self.inj[i[1]].popleft()
            if o == EJECT:
                self.delivered += 1
            else:
                self.buf[o].append(f)
            if f[2]:                                  # tail 이 이 입력을 떠남 → 할당·소유 해제
                del self.nxt[i]
                if isinstance(i, Channel):
                    self.owner[i] = None
            moved.add(i)
        for i in ins:
            if i in moved or self.front(i)[0] is None:
                self.age.pop(i, None)
            else:
                self.age[i] = self.age.get(i, 0) + 1
        self.t += 1
        return len(moves)

    def wait_for_graph(self):
        """대기 그래프: 막힌 VC 버퍼 b → b 의 앞 flit 이 기다리는 VC 버퍼.
        - head 인데 VC 를 못 잡았다: 후보 채널 전부 (다른 패킷이 소유)
        - 출력이 할당됐는데 하류 버퍼가 꽉 찼다: 그 하류 버퍼
        링크 경합으로 잠깐 진 경우는 간선이 아니다 (다음 사이클에 풀린다). 키는 앞 flit 이 있는 채널 버퍼만."""
        # TODO: 앞 flit 이 있는 채널 버퍼 c 마다 집합 waits 를 만든다:
        #       c 가 self.nxt 에 없고 앞 flit 이 head 면 → candidates(c) 중 EJECT 가 아니고 owner 가 있는 채널들
        #       c 가 self.nxt 에 있고 그 출력 o 가 EJECT 가 아니며 len(buf[o]) >= depth 면 → {o}
        #       g[c] = waits. 반환 g.
        raise NotImplementedError("wait_for_graph")

    def run(self):
        """끝나면 {"done", "deadlock"(순환 대기 채널 목록 또는 None), "cycle", "delivered", "total"}"""
        while self.t < self.max_cycles:
            for a in self.arrivals.pop(self.t, ()):
                self.add(*a)
            if self.delivered == self.total and not self.arrivals:
                return {"done": True, "deadlock": None, "cycle": self.t,
                        "delivered": self.delivered, "total": self.total}
            m = self.step()
            if m == 0 or (self.age and max(self.age.values()) >= self.W):
                cyc = find_cycle(self.wait_for_graph())
                if cyc:
                    return {"done": False, "deadlock": cyc, "cycle": self.t,
                            "delivered": self.delivered, "total": self.total}
        return {"done": False, "deadlock": None, "cycle": self.t,
                "delivered": self.delivered, "total": self.total}


def report(res, sim):
    if res["done"]:
        return "DONE @cycle %d: %d/%d flits delivered" % (res["cycle"], res["delivered"], res["total"])
    if res["deadlock"]:
        parts = ["%s[P%s]" % (cname(c), sim.owner[c]) for c in res["deadlock"]]
        return "DEADLOCK @cycle %d (%d/%d delivered): %s -> back" % (
            res["cycle"], res["delivered"], res["total"], " -> ".join(parts))
    return "TIMEOUT @cycle %d (%d/%d delivered)" % (res["cycle"], res["delivered"], res["total"])


def planted_shift(topo, route, shift=2, length=4, depth=2, **kw):
    """심어 둔 패턴: 모든 노드가 같은 사이클 0 에 (x+shift, y) 로 length-flit 패킷 1개씩."""
    sim = WormholeSim(topo, route, depth=depth, **kw)
    for pid, n in enumerate(topo.nodes):
        sim.add(pid, n, ((n[0] + shift) % topo.k, n[1]), length)
    return sim


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


def safe(fn, *a, **kw):
    try:
        return fn(*a, **kw)
    except Exception:  # noqa: BLE001
        return None


def fmt_cycle(cyc):
    return "None" if cyc is None else " -> ".join(cname(c) for c in cyc) + " -> back"


def ring_has_cycle(g, chans):
    s = set(chans)
    return find_cycle({c: {w for w in g[c] if w in s} for c in s}) is not None


def run_tests():
    k = 4
    mesh = Topo(k, wrap=False)
    torus = Topo(k, wrap=True)
    torus2 = Topo(k, wrap=True, nvc=2)

    print("-- Part A: XY on 4x4 mesh --")
    check("XY (0,0)->(2,1) first hop = E",
          lambda: route_xy(mesh, (0, 0), (0, 0), (2, 1), None) == [mesh.ch((0, 0), "E")])
    check("XY at (2,0) toward (2,1) turns N",
          lambda: route_xy(mesh, (0, 0), (2, 0), (2, 1), mesh.ch((1, 0), "E")) == [mesh.ch((2, 0), "N")])
    toy = {"a": {"b"}, "b": {"c"}, "c": {"a"}, "d": {"a"}}
    cy = safe(find_cycle, toy)
    check("find_cycle on a->b->c->a: %s" % cy, lambda: cy is not None and is_cycle(toy, cy) and len(cy) == 3)
    diamond = {"a": {"b", "c"}, "b": {"d"}, "c": {"d"}, "d": set()}
    check("find_cycle on diamond DAG = None (cross edge is not a cycle)",
          lambda: safe(find_cycle, diamond) is None and safe(topo_order, diamond) == ["a", "b", "c", "d"])
    check("find_cycle self-loop", lambda: safe(find_cycle, {"x": {"x"}}) == ["x"])
    gxy = safe(build_cdg, mesh, route_xy)
    n_ch = len(mesh.channels())
    # 직진 X: 2방향·k행·(k-2), 직진 Y 도 같다, X→Y 턴: 4종류·(k-1)^2
    want = 2 * 2 * k * (k - 2) + 4 * (k - 1) ** 2
    check("mesh 4x4: %d channels, XY CDG edges = %s (expect %d)" % (n_ch, gxy and edges(gxy), want),
          lambda: n_ch == 48 and edges(gxy) == want)
    check("XY CDG has no cycle", lambda: gxy is not None and find_cycle(gxy) is None)
    order = safe(topo_order, gxy) if gxy else None
    pos = {c: i for i, c in enumerate(order or [])}
    check("XY topological order covers all %d channels; every edge goes forward" % n_ch,
          lambda: order is not None and len(order) == n_ch
          and all(pos[a] < pos[b] for a in gxy for b in gxy[a]))
    if order:
        print("      order[:6] = %s ..." % ", ".join(cname(c) for c in order[:6]))

    print("-- Part B: adaptive / torus / dateline --")
    ad = safe(route_minimal_adaptive, mesh, (0, 0), (0, 0), (2, 1), None)
    check("adaptive (0,0)->(2,1) offers E and N", lambda: ad is not None and sorted(c.dir for c in ad) == ["E", "N"])
    gad = safe(build_cdg, mesh, route_minimal_adaptive)
    cad = find_cycle(gad) if gad else None
    check("fully adaptive minimal mesh CDG has a cycle: %s" % fmt_cycle(cad), lambda: is_cycle(gad, cad))
    sq = [mesh.ch((0, 0), "E"), mesh.ch((1, 0), "N"), mesh.ch((1, 1), "W"), mesh.ch((0, 1), "S")]
    check("unit-square turn cycle E->N->W->S is in the adaptive CDG", lambda: is_cycle(gad, sq))
    gnv = naive_cdg(mesh)
    check("naive (topology-only) CDG: %d edges, cyclic; true XY CDG: %s edges, acyclic"
          % (edges(gnv), gxy and edges(gxy)),
          lambda: find_cycle(gnv) is not None and gxy is not None and find_cycle(gxy) is None
          and all(b in gnv[a] for a in gxy for b in gxy[a]))
    check("torus DOR (2,0)->(0,0): tie distance 2 goes + (E over wrap)",
          lambda: route_torus_dor(torus, (2, 0), (2, 0), (0, 0), None) == [torus.ch((2, 0), "E")])
    gt = safe(build_cdg, torus, route_torus_dor)
    ct = find_cycle(gt) if gt else None
    check("torus 4x4 DOR CDG has a cycle: %s" % fmt_cycle(ct), lambda: is_cycle(gt, ct))

    def ring(t, d, idx, vc=0):
        i = AXIS[d]
        return [t.ch(n, d, vc) for n in t.nodes if n[1 - i] == idx]

    cyc_rings = [(d, j) for d in "EWNS" for j in range(k) if gt and ring_has_cycle(gt, ring(torus, d, j))]
    check("k=4: every E and N ring is cyclic (8), W/S rings are not (ties go +): %d" % len(cyc_rings),
          lambda: sorted(cyc_rings) == sorted([(d, j) for d in "EN" for j in range(k)]))
    t5 = Topo(5, wrap=True)
    g5 = safe(build_cdg, t5, route_torus_dor)
    n5 = sum(1 for d in "EWNS" for j in range(5) if g5 and ring_has_cycle(g5, ring(t5, d, j)))
    check("k=5: all 20 directed rings cyclic (no ties): %d" % n5, lambda: n5 == 20)
    r = safe(route_torus_dateline, torus2, (2, 0), (3, 0), (1, 0), torus2.ch((2, 0), "E", 0))
    check("dateline: crossing (3,0)->(0,0) uses VC1", lambda: r == [torus2.ch((3, 0), "E", 1)])
    r = safe(route_torus_dateline, torus2, (3, 0), (0, 0), (1, 0), torus2.ch((3, 0), "E", 1))
    check("dateline: after crossing, stay on VC1", lambda: r == [torus2.ch((0, 0), "E", 1)])
    r = safe(route_torus_dateline, torus2, (3, 0), (0, 0), (0, 2), torus2.ch((3, 0), "E", 1))
    check("dateline: X->Y dimension change resets to VC0", lambda: r == [torus2.ch((0, 0), "N", 0)])
    gd = safe(build_cdg, torus2, route_torus_dateline)
    check("torus 4x4 + dateline 2 VCs: CDG acyclic (%s edges)" % (gd and edges(gd)),
          lambda: gd is not None and find_cycle(gd) is None and topo_order(gd) is not None)
    used = safe(used_channels, torus2, route_torus_dateline) or set()
    wrap0 = [c for c in torus2.channels() if c.vc == 0 and torus2.is_wrap(c.src, c.dir)]
    check("VC0 of all 16 wrap links is never used; %d/%d channels used at all"
          % (len(used), len(torus2.channels())),
          lambda: len(wrap0) == 16 and len(used) == 72 and not (set(wrap0) & used))

    print("-- Part C: wormhole simulator --")
    ring1 = Topo(4, wrap=True, dims=1)
    ring2 = Topo(4, wrap=True, nvc=2, dims=1)
    s = planted_shift(ring1, route_torus_dor)
    res = safe(s.run)
    print("      ring k=4, 1 VC : %s" % (report(res, s) if res else res))
    check("ring 1 VC: planted i->i+2 (4 flits, depth 2) deadlocks; circular wait = 4 E channels",
          lambda: res and not res["done"] and res["deadlock"] is not None and len(res["deadlock"]) == 4
          and all(c.dir == "E" for c in res["deadlock"]))
    check("ring 1 VC: detection is bounded (cycle %s < 100) and some flits never arrive"
          % (res and res["cycle"]), lambda: res and res["cycle"] < 100 and res["delivered"] < res["total"])
    later = sum(s.step() for _ in range(500)) if res else None
    check("ring 1 VC: 500 more cycles move %s flits (deadlock is permanent, congestion is not)" % later,
          lambda: later == 0)
    s = planted_shift(ring2, route_torus_dateline)
    res = safe(s.run)
    print("      ring k=4, 2 VC : %s" % (report(res, s) if res else res))
    check("ring dateline 2 VCs: same traffic completes 16/16",
          lambda: res and res["done"] and res["delivered"] == 16)
    s = planted_shift(ring1, route_torus_dor, depth=8)
    res = safe(s.run)
    check("ring 1 VC, depth 8 >= packet: still deadlocks (VC ownership is the held resource)",
          lambda: res and res["deadlock"] is not None)
    s = planted_shift(torus, route_torus_dor)
    res = safe(s.run)
    print("      torus 4x4, 1 VC: %s" % (report(res, s) if res else res))
    check("torus 4x4 1 VC: planted (x+2,y) deadlocks", lambda: res and res["deadlock"] is not None)
    s = planted_shift(torus2, route_torus_dateline)
    res = safe(s.run)
    print("      torus 4x4, 2 VC: %s" % (report(res, s) if res else res))
    check("torus 4x4 dateline: completes 64/64", lambda: res and res["done"] and res["delivered"] == 64)
    s = planted_shift(mesh, route_xy, shift=1)
    res = safe(s.run)
    check("mesh XY, (x+1,y) for x<3 wraps to x=0 (westbound): completes",
          lambda: res and res["done"] and res["delivered"] == 64)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
