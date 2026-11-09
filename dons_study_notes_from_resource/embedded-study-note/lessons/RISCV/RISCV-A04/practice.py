#!/usr/bin/env python3
"""RISCV-A04 추가 연습 — 한 번에 검증

본문(solution.py)의 열거기 코어(이벤트 · 후보 실행 · ppo · 모델 4개 · 매핑)를 그대로 복사해 넣고
(레슨 파일끼리 import 하지 않는다 — python3 -I 는 스크립트 디렉터리를 sys.path 에 넣지 않음) 연습 5개를 확인한다.
  연습 1  IRIW · 2+2W — 다중 복사 원자성(MCA)과 store→store 순서
  연습 2  최소 fence 처방 찾기 — 10가지 fence 조합 전수 탐색
  연습 3  스펙의 TSO → RVWMO 매핑 표가 x86-TSO 를 정확히 재현하는지
  연습 4  Ztso 로 컴파일한 바이너리를 RVWMO 칩에서 돌리면
  연습 5  Linux smp_store_release = fence.tso; s 는 fence rw,w 보다 무엇이 더 강한가

  python3 -I practice.py
"""
import itertools
import sys


# ============================================================== 이벤트 · 미니 어셈블리 파서
class Ev:
    """메모리 이벤트 하나. kind: R(load) W(store) A(AMO = R 이면서 W) F(fence)"""
    __slots__ = ("tid", "idx", "kind", "loc", "val", "reg", "op", "acq", "rel",
                 "pred", "succ", "tso", "text")

    def __init__(self, kind, text):
        self.kind, self.text = kind, text
        self.tid, self.idx, self.loc, self.val, self.reg, self.op = -1, -1, None, 0, None, None
        self.acq = self.rel = None        # None / "pc" (RCpc) / "sc" (RCsc)
        self.pred, self.succ, self.tso = "", "", False

    def is_r(self):
        return self.kind in ("R", "A")

    def is_w(self):
        return self.kind in ("W", "A")

    def name(self):
        return "init %s=0" % self.loc if self.tid < 0 else "T%d:%s" % (self.tid, self.text)


SUFFIX = {"aq": ("sc", None), "rl": (None, "sc"), "aqrl": ("sc", "sc"),
          "aqpc": ("pc", None), "rlpc": (None, "pc")}   # *pc = RCpc (ARM LDAPR, Ztso 의 암묵 주석)


def parse_insn(text):
    """미니 문법:  lw[.aq] r0 x | sw[.rl] x 1 | amoswap[.aq|.rl|.aqrl] r0 x 1 | amoadd... | fence P,S | fence.tso"""
    tok = text.split()
    parts = tok[0].split(".")
    base, sufs = parts[0], [s for s in parts[1:] if s not in ("w", "d")]
    if tok[0] == "fence.tso":
        e = Ev("F", text)
        e.pred, e.succ, e.tso = "rw", "rw", True
        return e
    if base == "fence":
        e = Ev("F", text)
        e.pred, e.succ = tok[1].split(",")
        return e
    kind = {"lw": "R", "sw": "W", "amoswap": "A", "amoadd": "A"}[base]
    e = Ev(kind, text)
    for s in sufs:
        a, r = SUFFIX[s]
        e.acq, e.rel = a or e.acq, r or e.rel
    if kind == "R":
        e.reg, e.loc = tok[1], tok[2]
    elif kind == "W":
        e.loc, e.val = tok[1], int(tok[2])
    else:
        e.reg, e.loc, e.val, e.op = tok[1], tok[2], int(tok[3]), base[3:]
    return e


def build(threads):
    evs = []
    for t, insns in enumerate(threads):
        for i, txt in enumerate(insns):
            e = parse_insn(txt)
            e.tid, e.idx = t, i
            evs.append(e)
    return evs


# ============================================================== 후보 실행 열거
class Execution:
    def __init__(self, evs, mem, co, rf, val):
        self.evs, self.mem, self.co, self.rf, self.val = evs, mem, co, rf, val
        self.po = [(a, b) for a in mem for b in mem if a.tid == b.tid and a.idx < b.idx]

    def between(self, a, b):
        return [e for e in self.evs if e.tid == a.tid and a.idx < e.idx < b.idx]

    def outcome(self):
        regs = [("%d:%s" % (e.tid, e.reg), self.val[self.rf[e]]) for e in self.mem if e.reg]
        final = [(loc, self.val[ws[-1]]) for loc, ws in self.co]
        return tuple(sorted(regs) + final)


def executions(evs):
    mem = [e for e in evs if e.kind != "F"]
    locs = sorted({e.loc for e in mem})
    init = {}
    for loc in locs:
        w = Ev("W", "init")
        w.loc = loc
        init[loc] = w
    writes = [[e for e in mem if e.is_w() and e.loc == loc] for loc in locs]
    reads = [e for e in mem if e.kind == "R"]
    for perms in itertools.product(*[itertools.permutations(ws) for ws in writes]):
        co = [(loc, [init[loc]] + list(p)) for loc, p in zip(locs, perms)]
        amo_rf = {}
        for _, ws in co:
            for k, w in enumerate(ws):
                if w.kind == "A":          # 원자성 공리: AMO 는 co 바로 앞의 store 를 읽는다
                    amo_rf[w] = ws[k - 1]
        cos = dict(co)
        for srcs in itertools.product(*[cos[r.loc] for r in reads]):
            rf = dict(amo_rf)
            rf.update(zip(reads, srcs))
            val = {}
            for _, ws in co:
                for w in ws:
                    if w.kind == "A":
                        old = val[rf[w]]
                        val[w] = w.val if w.op == "swap" else old + w.val
                    else:
                        val[w] = w.val
            yield Execution(evs, mem, co, rf, val)


# ============================================================== 기본 관계 rf / co / fr / po-loc
def rel_rf(x):
    return [(w, r, "rfe" if w.tid != r.tid else "rfi") for r, w in x.rf.items()]


def rel_co(x):
    out = []
    for _, ws in x.co:
        for i in range(len(ws)):
            for j in range(i + 1, len(ws)):
                out.append((ws[i], ws[j], "co"))
    return out


def rel_fr(x):
    out = []
    for r, w in x.rf.items():
        ws = dict(x.co)[r.loc]
        for w2 in ws[ws.index(w) + 1:]:
            if w2 is not r:
                out.append((r, w2, "fr"))
    return out


def ext(edges):
    return [(a, b, l) for a, b, l in edges if a.tid != b.tid]


def find_cycle(edges):
    """edges 에 cycle 이 있으면 [(a, label, b), ...] 반환, 없으면 None. 순서는 결정적."""
    succ = {}
    for a, b, l in edges:
        succ.setdefault(a, []).append((b, l))
    state, path = {}, []

    def dfs(u):
        state[u] = 1
        for v, l in succ.get(u, []):
            path.append((u, l, v))
            if state.get(v) == 1:
                k = next(i for i, (p, _, _) in enumerate(path) if p is v)
                return path[k:]
            if v not in state:
                c = dfs(v)
                if c:
                    return c
            path.pop()
        state[u] = 2
        return None

    for a, _, _ in edges:
        if a not in state:
            c = dfs(a)
            if c:
                return c
    return None


def coherence(x):   # SC-per-location: acyclic(po-loc | rf | co | fr)
    poloc = [(a, b, "po-loc") for a, b in x.po if a.loc == b.loc]
    return poloc + rel_rf(x) + rel_co(x) + rel_fr(x)


# ============================================================== Part B: RVWMO ppo 규칙
def fence_orders(f, a, b):
    """FENCE f 가 po 앞의 a 를 po 뒤의 b 보다 먼저 전역 순서에 두게 하나?
    pred = PR/PW (앞쪽 load/store), succ = SR/SW (뒤쪽 load/store). AMO 는 load 이자 store.
    fence.tso: 앞쪽 load → 뒤쪽 전부, 앞쪽 store → 뒤쪽 store (store → load 는 풀어 둔다)."""
    if f.tso:
        return a.is_r() or (a.is_w() and b.is_w())
    before = ("r" in f.pred and a.is_r()) or ("w" in f.pred and a.is_w())
    after = ("r" in f.succ and b.is_r()) or ("w" in f.succ and b.is_w())
    return before and after


def ann_rvwmo(e):
    return e.acq, e.rel


def ann_ztso(e):
    """Ztso = RVWMO + 모든 load 는 acquire-RCpc, 모든 store 는 release-RCpc, AMO 는 acquire+release RCsc"""
    if e.kind == "A":
        return "sc", "sc"
    acq, rel = e.acq, e.rel
    if e.kind == "R" and acq is None:
        acq = "pc"
    if e.kind == "W" and rel is None:
        rel = "pc"
    return acq, rel


def annot_rule(a, b, ann):
    """r5: a 에 acquire 주석 / r6: b 에 release 주석 / r7: a 와 b 모두 RCsc 주석"""
    aa, ar = ann(a)
    ba, br = ann(b)
    if aa:
        return "r5 aq"
    if br:
        return "r6 rl"
    if "sc" in (aa, ar) and "sc" in (ba, br):
        return "r7 RCsc"
    return None


def ppo_rule(x, a, b, ann):
    """po 에서 a 가 b 앞일 때, RVWMO 가 그 순서를 보존하면 규칙 이름, 아니면 None"""
    same = a.loc == b.loc
    if same and b.is_w():
        return "r1 po-loc;W"
    if same and a.is_r() and b.is_r() and x.rf[a] is not x.rf[b] and \
            not any(m.is_w() and m.loc == a.loc for m in x.between(a, b)):
        return "r2 R;po-loc;R"
    if a.kind == "A" and b.is_r() and x.rf.get(b) is a:
        return "r3 AMO;rfi;R"
    for f in x.between(a, b):
        if f.kind == "F" and fence_orders(f, a, b):
            return "r4 " + f.text
    return annot_rule(a, b, ann)


def ppo(x, ann):
    out = []
    for a, b in x.po:
        why = ppo_rule(x, a, b, ann)
        if why:
            out.append((a, b, why))
    return out


# ============================================================== 모델 4개 (+ 독립 x86-TSO)
def check_sc(x):
    rel = [(a, b, "po") for a, b in x.po] + rel_rf(x) + rel_co(x) + rel_fr(x)
    return find_cycle(rel)


def check_rvwmo(x, ann=ann_rvwmo):
    c = find_cycle(coherence(x))
    if c:
        return c
    model = rel_co(x) + ext(rel_rf(x)) + rel_fr(x) + ppo(x, ann)
    return find_cycle(model)


def check_ztso(x):
    return check_rvwmo(x, ann_ztso)


def check_x86(x):
    """독립 정의 x86-TSO (Sewell et al. 2010 / herding cats): ppo = po 에서 store→load 만 뺀 것,
    store→load 사이에 PW 와 SR 이 둘 다 있는 fence(= MFENCE 역할)가 있으면 다시 보존. AMO = LOCK 접두 = 전부 보존."""
    c = find_cycle(coherence(x))
    if c:
        return c
    keep = []
    for a, b in x.po:
        if a.kind == "W" and b.kind == "R":
            if not any(f.kind == "F" and not f.tso and "w" in f.pred and "r" in f.succ
                       for f in x.between(a, b)):
                continue
        keep.append((a, b, "tso-ppo"))
    return find_cycle(rel_co(x) + ext(rel_rf(x)) + rel_fr(x) + keep)


def arm_barrier(f):
    """RISC-V fence → 그 순서를 덮는 가장 싼 AArch64 DMB (포팅할 때 고르는 것)"""
    if f.tso:
        return ("ld", "st")                # DMB ISHLD + DMB ISHST
    if f.pred == "r":
        return ("ld",)                     # R→R / R→W / R→RW ⊆ DMB ISHLD
    if f.pred == "w" and f.succ == "w":
        return ("st",)                     # DMB ISHST
    return ("full",)                       # W→R 가 끼면 DMB ISH 뿐


def check_arm(x):
    """ARMv8-lite (aarch64.cat 단순화): internal = Coherence, external = acyclic(obs | bob).
    obs = rfe | coe | fre, bob = DMB · [A|Q];po · po;[L] · [L];po;[A]. dob/aob(의존성) 는 뺐다."""
    c = find_cycle(coherence(x))
    if c:
        return c
    bob = []
    for a, b in x.po:
        why = None
        for f in x.between(a, b):
            if f.kind != "F":
                continue
            ks = arm_barrier(f)
            if "full" in ks or ("ld" in ks and a.is_r()) or ("st" in ks and a.is_w() and b.is_w()):
                why = "bob dmb"
        if why is None and a.acq:
            why = "bob acq;po"
        if why is None and b.rel:
            why = "bob po;rel"
        if why is None and a.rel == "sc" and b.acq == "sc":
            why = "bob L;po;A"
        if why:
            bob.append((a, b, why))
    obs = ext(rel_rf(x)) + ext(rel_co(x)) + ext(rel_fr(x))
    return find_cycle(obs + bob)


MODELS = [("SC", check_sc), ("TSO", check_ztso), ("RVWMO", check_rvwmo), ("ARMv8", check_arm)]


def allowed(threads, check):
    """모델이 허용하는 최종 결과 집합 (정렬된 리스트)"""
    out = {x.outcome() for x in executions(build(threads)) if check(x) is None}
    return sorted(out)


def matches(outcome, weird):
    d = dict(outcome)
    return all(d.get(k) == v for k, v in weird.items())


def weird_allowed(threads, weird, check):
    return any(matches(o, weird) for o in allowed(threads, check))


def explain(threads, weird, check):
    """weird 결과를 내는 후보 실행마다 모델이 찾은 cycle 하나 (첫 후보)"""
    for x in executions(build(threads)):
        if matches(x.outcome(), weird):
            c = check(x)
            if c:
                return c
    return None


def fmt_cycle(c):
    return c[0][0].name() + "".join(" -%s-> %s" % (l, b.name()) for _, l, b in c)


# ============================================================== 리트머스 모음
def shape(kind, f0=None, f1=None):
    """MP / SB / LB 기본 모양에 fence 를 끼운다 (None = 없음)"""
    mid0 = [f0] if f0 else []
    mid1 = [f1] if f1 else []
    if kind == "MP":
        return [["sw x 1"] + mid0 + ["sw y 1"], ["lw r0 y"] + mid1 + ["lw r1 x"]], {"1:r0": 1, "1:r1": 0}
    if kind == "SB":
        return [["sw x 1"] + mid0 + ["lw r0 y"], ["sw y 1"] + mid1 + ["lw r1 x"]], {"0:r0": 0, "1:r1": 0}
    return [["lw r0 x"] + mid0 + ["sw y 1"], ["lw r1 y"] + mid1 + ["sw x 1"]], {"0:r0": 1, "1:r1": 1}



SB_W = {"0:r0": 0, "1:r1": 0}


# ============================================================== Part A: C11 → RISC-V 매핑
ORDERS = ("relaxed", "acquire", "release", "acq_rel", "seq_cst")
OPS = {"load": ("relaxed", "acquire", "seq_cst"), "store": ("relaxed", "release", "seq_cst"),
       "rmw": ORDERS, "cas": ORDERS, "fence": ("acquire", "release", "acq_rel", "seq_cst")}


def lower_spec(op, order):
    """스펙 표(RVWMO Explanatory Material, 'Mappings from C/C++ primitives to RISC-V primitives')를 코드로.
    템플릿: l = l{b|h|w|d}, s = s{b|h|w|d}, amo[.aq|.rl|.aqrl] = amo<op>, lr/sc[.aq|.rl|.aqrl] = LR/SC 루프"""
    sfx = {"relaxed": "", "acquire": ".aq", "release": ".rl", "acq_rel": ".aqrl", "seq_cst": ".aqrl"}
    if op == "load":
        return {"relaxed": ["l"], "acquire": ["l", "fence r,rw"],
                "seq_cst": ["fence rw,rw", "l", "fence r,rw"]}[order]
    if op == "store":
        return {"relaxed": ["s"], "release": ["fence rw,w", "s"], "seq_cst": ["fence rw,w", "s"]}[order]
    if op == "rmw":
        return ["amo" + sfx[order]]
    if op == "cas":
        return {"relaxed": ["lr", "sc"], "acquire": ["lr.aq", "sc"], "release": ["lr", "sc.rl"],
                "acq_rel": ["lr.aq", "sc.rl"], "seq_cst": ["lr.aqrl", "sc.rl"]}[order]
    return {"acquire": ["fence r,rw"], "release": ["fence rw,w"], "acq_rel": ["fence.tso"],
            "seq_cst": ["fence rw,rw"]}[order]


def lower_zalasr(op, order):
    """부록 A 의 '가상(hypothetical)' 표 — load-acquire / store-release 명령이 있을 때 (Zalasr)"""
    if op == "load" and order != "relaxed":
        return ["l.aq"]
    if op == "store" and order != "relaxed":
        return ["s.rl"]
    return lower_spec(op, order)


def _obs(rv, ztso):
    t = {}
    for op, orders in OPS.items():
        for o in orders:
            t[(op, o)] = list(rv[(op, o)]) if (op, o) in rv else None
    for k, v in ztso.items():
        t[k] = v
    return t


# VM (Ubuntu clang 21.1.8 / riscv64-linux-gnu-gcc 15.2.0, -O2) 에서 demo_map.c 를 -S 한 결과를 정규화.
# fetch_add 와 exchange 는 같은 모양 (amoadd / amoswap) 이라 "rmw" 하나로 묶었다. clang == gcc (한 줄도 안 다름).
_RV64GC = {
    ("load", "relaxed"): ["l"], ("load", "acquire"): ["l", "fence r,rw"],
    ("load", "seq_cst"): ["fence rw,rw", "l", "fence r,rw"],
    ("store", "relaxed"): ["s"], ("store", "release"): ["fence rw,w", "s"],
    ("store", "seq_cst"): ["fence rw,w", "s", "fence rw,rw"],
    ("rmw", "relaxed"): ["amo"], ("rmw", "acquire"): ["amo.aq"], ("rmw", "release"): ["amo.rl"],
    ("rmw", "acq_rel"): ["amo.aqrl"], ("rmw", "seq_cst"): ["amo.aqrl"],
    ("cas", "relaxed"): ["lr", "sc"], ("cas", "acquire"): ["lr.aq", "sc"], ("cas", "release"): ["lr", "sc.rl"],
    ("cas", "acq_rel"): ["lr.aq", "sc.rl"], ("cas", "seq_cst"): ["lr.aqrl", "sc.rl"],
    ("fence", "acquire"): ["fence r,rw"], ("fence", "release"): ["fence rw,w"],
    ("fence", "acq_rel"): ["fence.tso"], ("fence", "seq_cst"): ["fence rw,rw"],
}
_ZTSO = {
    ("load", "relaxed"): ["l"], ("load", "acquire"): ["l"], ("load", "seq_cst"): ["fence rw,rw", "l"],
    ("store", "relaxed"): ["s"], ("store", "release"): ["s"], ("store", "seq_cst"): ["s", "fence rw,rw"],
    ("rmw", "relaxed"): ["amo"], ("rmw", "acquire"): ["amo"], ("rmw", "release"): ["amo"],
    ("rmw", "acq_rel"): ["amo"], ("rmw", "seq_cst"): ["amo"],
    ("cas", "relaxed"): ["lr", "sc"], ("cas", "acquire"): ["lr", "sc"], ("cas", "release"): ["lr", "sc"],
    ("cas", "acq_rel"): ["lr", "sc"], ("cas", "seq_cst"): ["lr.aqrl", "sc.rl"],
    ("fence", "acquire"): [], ("fence", "release"): [], ("fence", "acq_rel"): [],
    ("fence", "seq_cst"): ["fence rw,rw"],
}
OBSERVED = {"clang21 rv64gc": _obs(_RV64GC, {}), "gcc15 rv64gc": _obs(_RV64GC, {}),
            "clang21 rv64gc_ztso": _obs(_ZTSO, {}), "gcc15 rv64gc_ztso": _obs(_ZTSO, {})}


def mapping_diff(table_fn, observed):
    """스펙 표와 관찰이 다른 (op, order) 목록"""
    return [k for k in sorted(observed) if table_fn(*k) != observed[k]]


def lower(c11_threads, table_fns):
    """C11 리트머스 → 미니 RISC-V. 연산: ("st", loc, val, order) ("ld", reg, loc, order)
    ("xchg", reg, loc, val, order) ("fence", order). table_fns[t] = 스레드 t 를 컴파일한 매핑 함수
    (스레드마다 다른 매핑 = 다른 컴파일러/ABI 로 만든 코드를 링크한 상황)."""
    out = []
    for t, ops in enumerate(c11_threads):
        fn = table_fns[t] if isinstance(table_fns, (list, tuple)) else table_fns
        insns = []
        for op in ops:
            kind, order = op[0], op[-1]
            key = {"st": "store", "ld": "load", "xchg": "rmw", "fence": "fence"}[kind]
            for tpl in fn(key, order):
                base, _, sfx = tpl.partition(".")
                dot = "." + sfx if sfx else ""
                if tpl.startswith("fence"):
                    insns.append(tpl)
                elif base == "l":
                    insns.append("lw%s %s %s" % (dot, op[1], op[2]))
                elif base == "s":
                    insns.append("sw%s %s %d" % (dot, op[1], op[2]))
                elif base == "amo":
                    insns.append("amoswap%s %s %s %d" % (dot, op[1], op[2], op[3]))
                else:
                    raise ValueError("LR/SC 는 이 열거기에 없음: " + tpl)
        out.append(insns)
    return out


def table_observed(name):
    return lambda op, order: OBSERVED[name][(op, order)]


C11_MP = lambda w, r: [[("st", "x", 1, "relaxed"), ("st", "y", 1, w)],
                       [("ld", "r0", "y", r), ("ld", "r1", "x", "relaxed")]]
C11_SB = lambda w, r: [[("st", "x", 1, w), ("ld", "r0", "y", r)], [("st", "y", 1, w), ("ld", "r1", "x", r)]]
MP_W = {"1:r0": 1, "1:r1": 0}


# ============================================================== 테스트 하네스
g_pass = g_total = 0


def check(name, cond, detail=""):
    global g_pass, g_total
    g_total += 1
    if cond:
        g_pass += 1
        print("PASS " + name)
    else:
        print("FAIL " + name + (": " + detail if detail else ""))


def run(name, fn):
    try:
        fn()
    except Exception as ex:          # 시작 코드의 스텁이 터져도 하네스는 끝까지 간다
        check(name, False, "exception %s: %s" % (type(ex).__name__, ex))



# ============================================================== 연습 1: IRIW · 2+2W
def iriw(f=None):
    mid = [f] if f else []
    return ([["sw x 1"], ["sw y 1"], ["lw r0 x"] + mid + ["lw r1 y"], ["lw r2 y"] + mid + ["lw r3 x"]],
            {"2:r0": 1, "2:r1": 0, "3:r2": 1, "3:r3": 0})


def two_plus_two_w(f=None):
    mid = [f] if f else []
    return [["sw x 1"] + mid + ["sw y 2"], ["sw y 1"] + mid + ["sw x 2"]], {"x": 1, "y": 1}


def models_string(threads, weird):
    return "".join("A" if weird_allowed(threads, weird, fn) else "F" for _, fn in MODELS)


def ex1():
    check("ex1 IRIW            SC/TSO/RVWMO/ARMv8 = FFAA", models_string(*iriw()) == "FFAA", models_string(*iriw()))
    th, w = iriw("fence r,r")
    check("ex1 IRIW+fence r,r  = FFFF (RVWMO is multi-copy atomic)", models_string(th, w) == "FFFF")
    check("ex1 2+2W            = FFAA", models_string(*two_plus_two_w()) == "FFAA")
    th, w = two_plus_two_w("fence w,w")
    check("ex1 2+2W+fence w,w  = FFFF", models_string(th, w) == "FFFF")
    c = explain(th, w, check_rvwmo)
    check("ex1 2+2W+fence w,w cycle = r4, co, r4, co",
          sorted(l for _, l, _ in c) == ["co", "co", "r4 fence w,w", "r4 fence w,w"], fmt_cycle(c) if c else "-")


# ============================================================== 연습 2: 최소 fence 처방
FENCE_OPTS = [None, "fence.tso"] + ["fence %s,%s" % (p, s) for p in ("r", "w", "rw") for s in ("r", "w", "rw")]


def order_pairs(ftxt):
    """fence 가 순서를 지키게 하는 (앞 종류, 뒤 종류) 집합. None = 빈 집합"""
    if ftxt is None:
        return frozenset()
    f, out = parse_insn(ftxt), set()
    for ak in "RW":
        for bk in "RW":
            if fence_orders(f, Ev(ak, "a"), Ev(bk, "b")):
                out.add(ak + bk)
    return frozenset(out)


def minimal_fixes(kind):
    fixes = []
    for f0 in FENCE_OPTS:
        for f1 in FENCE_OPTS:
            th, w = shape(kind, f0, f1)
            if not weird_allowed(th, w, check_rvwmo):
                fixes.append((f0, f1))
    mins = []
    for f0, f1 in fixes:
        p0, p1 = order_pairs(f0), order_pairs(f1)
        weaker = [g for g in fixes if g != (f0, f1) and order_pairs(g[0]) <= p0 and order_pairs(g[1]) <= p1]
        if not weaker:
            mins.append((f0, f1))
    return mins, len(fixes)


def ex2():
    check("ex2 fence.tso orders RR RW WW (not WR)", order_pairs("fence.tso") == {"RR", "RW", "WW"})
    check("ex2 10 distinct non-trivial fences", len({order_pairs(f) for f in FENCE_OPTS[1:]}) == 10)
    for kind, exp in (("MP", ("fence w,w", "fence r,r")), ("SB", ("fence w,r", "fence w,r")),
                      ("LB", ("fence r,w", "fence r,w"))):
        mins, n = minimal_fixes(kind)
        check("ex2 %s minimal fix = %s / %s (of %d fixing pairs)" % (kind, exp[0], exp[1], n), mins == [exp], str(mins))


# ============================================================== 연습 3: TSO → RVWMO 매핑 표 (스펙 'Mappings from TSO operations')
def lower_tso(threads):
    out = []
    for t in threads:
        insns = []
        for i in t:
            if i.startswith("lw"):
                insns += [i, "fence r,rw"]
            elif i.startswith("sw"):
                insns += ["fence rw,w", i]
            else:                       # mfence (여기서는 fence rw,rw 로 씀)
                insns += ["fence rw,rw"]
        out.append(insns)
    return out


def ex3():
    progs = [("MP", shape("MP")[0]), ("SB", shape("SB")[0]),
             ("SB+mfence", shape("SB", "fence rw,rw", "fence rw,rw")[0]), ("LB", shape("LB")[0]),
             ("2+2W", two_plus_two_w()[0]), ("IRIW", iriw()[0])]
    for name, th in progs:
        a, b = allowed(lower_tso(th), check_rvwmo), allowed(th, check_x86)
        check("ex3 %-9s RVWMO(tso-mapped) == x86-TSO (%d outcomes)" % (name, len(b)), a == b)


# ============================================================== 연습 4: Ztso 바이너리를 RVWMO 칩에서
def ex4():
    zt = table_observed("clang21 rv64gc_ztso")
    mp = lower(C11_MP("release", "acquire"), zt)
    check("ex4 Ztso-compiled MP rel/acq: forbidden on Ztso", not weird_allowed(mp, MP_W, check_ztso))
    check("ex4 Ztso-compiled MP rel/acq: ALLOWED on RVWMO (must refuse to run)", weird_allowed(mp, MP_W, check_rvwmo))
    sb = lower(C11_SB("seq_cst", "seq_cst"), zt)
    check("ex4 Ztso-compiled SB seq_cst: still forbidden on RVWMO (fence rw,rw kept)",
          not weird_allowed(sb, SB_W, check_rvwmo))
    check("ex4 Ztso-compiled MP lowered text has no fences", all(not i.startswith("fence") for t in mp for i in t), str(mp))


# ============================================================== 연습 5: Linux smp_store_release = fence.tso; s
def ex5():
    th = [["sw x 1", "fence.tso", "sw y 1"], ["lw r0 y", "fence r,rw", "lw r1 x"]]
    check("ex5 MP with smp_store_release/smp_load_acquire forbidden on RVWMO", not weird_allowed(th, MP_W, check_rvwmo))
    extra = order_pairs("fence.tso") - order_pairs("fence rw,w")
    check("ex5 fence.tso - fence rw,w = {RR} (earlier loads before later loads)", extra == {"RR"}, str(sorted(extra)))
    # 스펙의 Linux 예: 임계 구역 안 load (a) → unlock → 같은 hart 의 다음 lock 뒤 load (e).
    # unlock 의 fence 가 fence.tso 면 (a) 가 po 뒤의 모든 접근, 즉 (e) 보다도 먼저 — fence rw,w 로는 안 된다.
    t0 = [["lw r0 x", "fence rw,w", "lw r1 y"]]
    t1 = [["lw r0 x", "fence.tso", "lw r1 y"]]
    w0 = executions(build(t0)).__next__()
    w1 = executions(build(t1)).__next__()
    a0, b0 = [e for e in w0.mem]
    a1, b1 = [e for e in w1.mem]
    check("ex5 fence rw,w does NOT order load->load", ppo_rule(w0, a0, b0, ann_rvwmo) is None)
    check("ex5 fence.tso orders load->load", ppo_rule(w1, a1, b1, ann_rvwmo) == "r4 fence.tso")


def main():
    for name, fn in (("ex1", ex1), ("ex2", ex2), ("ex3", ex3), ("ex4", ex4), ("ex5", ex5)):
        run(name, fn)
    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
