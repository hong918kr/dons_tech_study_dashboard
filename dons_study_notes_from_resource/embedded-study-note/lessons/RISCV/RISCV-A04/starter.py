#!/usr/bin/env python3
"""RISCV-A04 RVWMO 메모리 모델: FENCE 종류 · .aq/.rl 과 C11 매핑 — axiomatic 리트머스 열거기

  Part A  C11 → RISC-V 매핑 (Unpriv ISA 부록 A "Mappings from C/C++ primitives to RISC-V primitives")를
          코드로 쓰고, VM 에서 실제로 뽑은 clang 21 / gcc 15.2 출력(demo_map.c)과 비교한다.
  Part B  RVWMO 보존 프로그램 순서(ppo) 규칙 부분집합 — r1~r3 같은 주소, r4 FENCE pred/succ, r5~r7 aq/rl/RCsc —
          로 MP · SB · LB 의 허용 결과를 계산하고 SC · TSO(Ztso) · ARMv8-lite 와 비교한다. 금지 이유는 cycle 로 출력.
  Part C  C11 리트머스를 매핑으로 내린 뒤 RVWMO 위에서 C11 의 보장이 지켜지는지, 매핑을 섞으면 깨지는지 확인한다.

  python3 -I solution.py            # 테스트만 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --table    # + 비교 표 · cycle 설명 · 매핑 표 출력

모델 = herd 식 "후보 실행(rf, co 선택) 전수 열거 + 관계 합집합의 비순환성(acyclicity)". 스펙의 riscv.cat:
  acyclic co|rf|fr|po-loc as Coherence     acyclic co|rfe|fr|ppo as Model
여기서 빠진 것 (정직하게): 의존성 규칙 r9~r13 (addr/data/ctrl), LR/SC 짝 r8, I/O 영역, 혼합 크기 접근.
Python 3 표준 라이브러리만 (itertools, sys). Python 3.9 호환.
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
    # TODO(Part B-1): f.tso 이면 스펙의 fence.tso 정의, 아니면 f.pred / f.succ 문자열("r" "w" "rw")로
    #   a 가 앞쪽 집합에, b 가 뒤쪽 집합에 들어가는지 본다. a.is_r() / a.is_w() 를 쓴다 (AMO 는 둘 다 True).
    (f, a, b)
    return False


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
    # TODO(Part B-2): r5 (a 에 acquire — RCpc 든 RCsc 든) → "r5 aq"
    #                 r6 (b 에 release)                    → "r6 rl"
    #                 r7 (a, b 둘 다 RCsc 주석이 하나 이상)  → "r7 RCsc"
    (aa, ar, ba, br)
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
VARIANTS = [   # (이름, (threads, weird), 기대: SC TSO RVWMO ARMv8 의 weird 허용 여부 A/F)
    ("MP", shape("MP"), "FFAA"),
    ("MP+fence rw,rw", shape("MP", "fence rw,rw", "fence rw,rw"), "FFFF"),
    ("MP+fence w,w/r,r", shape("MP", "fence w,w", "fence r,r"), "FFFF"),
    ("MP+fence.tso", shape("MP", "fence.tso", "fence.tso"), "FFFF"),
    ("MP+w,w only", shape("MP", "fence w,w", None), "FFAA"),
    ("MP+r,r only", shape("MP", None, "fence r,r"), "FFAA"),
    ("MP+amo.rl/amo.aq", ([["sw x 1", "amoswap.rl r9 y 1"], ["amoadd.aq r0 y 0", "lw r1 x"]],
                          {"1:r0": 1, "1:r1": 0}), "FFFF"),
    ("MP+amo.rl only", ([["sw x 1", "amoswap.rl r9 y 1"], ["lw r0 y", "lw r1 x"]],
                        {"1:r0": 1, "1:r1": 0}), "FFAA"),
    ("SB", shape("SB"), "FAAA"),
    ("SB+fence rw,rw", shape("SB", "fence rw,rw", "fence rw,rw"), "FFFF"),
    ("SB+fence w,r", shape("SB", "fence w,r", "fence w,r"), "FFFF"),
    ("SB+fence.tso", shape("SB", "fence.tso", "fence.tso"), "FAAA"),
    ("SB+fence r,rw", shape("SB", "fence r,rw", "fence r,rw"), "FAAA"),
    ("SB+fence rw,w", shape("SB", "fence rw,w", "fence rw,w"), "FAAF"),   # ARM 엔 rw,w 가 없어 DMB ISH 로 올림
    ("SB+amoswap.rl", ([["amoswap.rl r8 x 1", "lw r0 y"], ["amoswap.rl r9 y 1", "lw r1 x"]], SB_W), "FFAA"),
    ("SB+amoswap.aqrl", ([["amoswap.aqrl r8 x 1", "lw r0 y"], ["amoswap.aqrl r9 y 1", "lw r1 x"]], SB_W), "FFFF"),
    ("SB+sw.rl/lw.aq", ([["sw.rl x 1", "lw.aq r0 y"], ["sw.rl y 1", "lw.aq r1 x"]], SB_W), "FFFF"),
    ("SB+sw.rl/lw.aqpc", ([["sw.rl x 1", "lw.aqpc r0 y"], ["sw.rl y 1", "lw.aqpc r1 x"]], SB_W), "FAAA"),
    ("LB", shape("LB"), "FFAA"),
    ("LB+fence rw,rw", shape("LB", "fence rw,rw", "fence rw,rw"), "FFFF"),
    ("LB+fence r,w", shape("LB", "fence r,w", "fence r,w"), "FFFF"),
    ("LB+fence.tso", shape("LB", "fence.tso", "fence.tso"), "FFFF"),
]


# ============================================================== Part A: C11 → RISC-V 매핑
ORDERS = ("relaxed", "acquire", "release", "acq_rel", "seq_cst")
OPS = {"load": ("relaxed", "acquire", "seq_cst"), "store": ("relaxed", "release", "seq_cst"),
       "rmw": ORDERS, "cas": ORDERS, "fence": ("acquire", "release", "acq_rel", "seq_cst")}


def lower_spec(op, order):
    """스펙 표(RVWMO Explanatory Material, 'Mappings from C/C++ primitives to RISC-V primitives')를 코드로.
    템플릿: l = l{b|h|w|d}, s = s{b|h|w|d}, amo[.aq|.rl|.aqrl] = amo<op>, lr/sc[.aq|.rl|.aqrl] = LR/SC 루프"""
    # TODO(Part A): 스펙 표를 그대로 옮긴다. 지금은 "순서를 무시한 relaxed 매핑" 이라 틀렸다.
    #   load: relaxed / acquire / seq_cst,  store: relaxed / release / seq_cst,
    #   rmw: amo + 접미사, cas: lr/sc + 접미사, fence: acquire / release / acq_rel / seq_cst
    sfx = {"relaxed": "", "acquire": "", "release": "", "acq_rel": "", "seq_cst": ""}
    if op == "load":
        return ["l"]
    if op == "store":
        return ["s"]
    if op == "rmw":
        return ["amo" + sfx[order]]
    if op == "cas":
        return ["lr", "sc"]
    return []


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


def part_a():
    for name in ("clang21 rv64gc", "gcc15 rv64gc"):
        d = mapping_diff(lower_spec, OBSERVED[name])
        check("A %s vs spec table: only seq_cst store differs" % name, d == [("store", "seq_cst")], str(d))
        ob = OBSERVED[name][("store", "seq_cst")]
        check("A %s seq_cst store = spec + trailing fence rw,rw" % name,
              ob == lower_spec("store", "seq_cst") + ["fence rw,rw"], str(ob))
    for name in ("clang21 rv64gc_ztso", "gcc15 rv64gc_ztso"):
        fences = [i for k, v in OBSERVED[name].items() for i in v if i.startswith("fence")]
        check("A %s keeps only fences with PW and SR" % name,
              all(i == "fence rw,rw" for i in fences) and len(fences) == 3, str(fences))
    check("A spec: acq_rel fence = fence.tso", lower_spec("fence", "acq_rel") == ["fence.tso"])
    check("A spec: seq_cst CAS = lr.aqrl + sc.rl", lower_spec("cas", "seq_cst") == ["lr.aqrl", "sc.rl"])


def part_b():
    names = [m for m, _ in MODELS]
    for vname, (threads, weird), exp in VARIANTS:
        got = "".join("A" if weird_allowed(threads, weird, fn) else "F" for _, fn in MODELS)
        check("B %-18s %s = %s" % (vname, "/".join(names), exp), got == exp, "got " + got)
    # Ztso(스펙 정의: RVWMO + 암묵 주석) == 독립 x86-TSO — 주석(aq/rl) 없는 변형 전부, 결과 집합 전체 비교
    same = [v for v, (th, _), _ in VARIANTS
            if not any("." in i.split()[0] and not i.startswith("fence") for t in th for i in t)
            and allowed(th, check_ztso) == allowed(th, check_x86)]
    plain = [v for v, (th, _), _ in VARIANTS
             if not any("." in i.split()[0] and not i.startswith("fence") for t in th for i in t)]
    check("B Ztso == x86-TSO on all %d annotation-free variants" % len(plain), same == plain,
          str(sorted(set(plain) - set(same))))
    th, w = shape("SB", "fence rw,rw", "fence rw,rw")
    c = explain(th, w, check_rvwmo)
    labels = [l for _, l, _ in c] if c else []
    check("B SB+fence rw,rw forbidden by cycle r4-fr-r4-fr",
          sorted(labels) == ["fr", "fr", "r4 fence rw,rw", "r4 fence rw,rw"], str(labels))
    th, w = shape("MP", "fence w,w", "fence r,r")
    c = explain(th, w, check_rvwmo)
    labels = sorted(l for _, l, _ in c) if c else []
    check("B MP+w,w/r,r cycle uses fence w,w, rfe, fence r,r, fr",
          labels == ["fr", "r4 fence r,r", "r4 fence w,w", "rfe"], str(labels))
    check("B SC allows exactly 3 SB outcomes", len(allowed(shape("SB")[0], check_sc)) == 3)
    check("B RVWMO allows all 4 SB outcomes", len(allowed(shape("SB")[0], check_rvwmo)) == 4)


def part_c():
    spec, obs = lower_spec, table_observed("clang21 rv64gc")
    zt = table_observed("clang21 rv64gc_ztso")
    check("C MP release/acquire (spec map) forbidden on RVWMO",
          not weird_allowed(lower(C11_MP("release", "acquire"), spec), MP_W, check_rvwmo))
    check("C MP relaxed/relaxed allowed on RVWMO (C11 allows it too)",
          weird_allowed(lower(C11_MP("relaxed", "relaxed"), spec), MP_W, check_rvwmo))
    check("C SB seq_cst (spec map) forbidden on RVWMO",
          not weird_allowed(lower(C11_SB("seq_cst", "seq_cst"), spec), SB_W, check_rvwmo))
    check("C SB release/acquire allowed on RVWMO (C11 allows it too)",
          weird_allowed(lower(C11_SB("release", "acquire"), spec), SB_W, check_rvwmo))
    # 매핑 섞기: store 는 A.6 표, load 는 가상(Zalasr) 표 — 스펙 본문이 경고하는 바로 그 경우
    mix = lambda op, o: lower_spec(op, o) if op == "store" else lower_zalasr(op, o)
    mix_fixed = lambda op, o: obs(op, o) if op == "store" else lower_zalasr(op, o)
    check("C SB seq_cst: spec store + Zalasr load -> weird ALLOWED (mixing bug)",
          weird_allowed(lower(C11_SB("seq_cst", "seq_cst"), mix), SB_W, check_rvwmo))
    check("C SB seq_cst: compiler store (trailing fence) + Zalasr load -> forbidden",
          not weird_allowed(lower(C11_SB("seq_cst", "seq_cst"), mix_fixed), SB_W, check_rvwmo))
    check("C SB seq_cst: pure Zalasr (sw.rl / lw.aq, RCsc) -> forbidden",
          not weird_allowed(lower(C11_SB("seq_cst", "seq_cst"), lower_zalasr), SB_W, check_rvwmo))
    check("C Ztso compiler map: MP rel/acq forbidden on Ztso",
          not weird_allowed(lower(C11_MP("release", "acquire"), zt), MP_W, check_ztso))
    check("C Ztso compiler map: SB seq_cst forbidden on Ztso",
          not weird_allowed(lower(C11_SB("seq_cst", "seq_cst"), zt), SB_W, check_ztso))
    check("C compiler map: xchg seq_cst as SB store -> forbidden on RVWMO",
          not weird_allowed(lower([[("xchg", "r8", "x", 1, "seq_cst"), ("ld", "r0", "y", "relaxed")],
                                   [("xchg", "r9", "y", 1, "seq_cst"), ("ld", "r1", "x", "relaxed")]], obs),
                            SB_W, check_rvwmo))


def print_tables():
    print("\n== weird 결과 허용(A) / 금지(F), 괄호 = 허용 결과 개수")
    print("%-20s %-10s %-10s %-10s %-10s" % ("variant", "SC", "TSO(Ztso)", "RVWMO", "ARMv8-lite"))
    for vname, (threads, weird), _ in VARIANTS:
        cells = []
        for _, fn in MODELS:
            al = allowed(threads, fn)
            cells.append("%s(%d)" % ("A" if any(matches(o, weird) for o in al) else "F", len(al)))
        print("%-20s %-10s %-10s %-10s %-10s" % tuple([vname] + cells))
    print("\n== 금지 이유 (RVWMO, weird 결과의 첫 후보 실행에서 찾은 cycle)")
    for vname in ("MP+fence w,w/r,r", "SB+fence rw,rw", "SB+sw.rl/lw.aq", "LB+fence.tso"):
        threads, weird = dict((v, tw) for v, tw, _ in VARIANTS)[vname]
        print("%-18s %s" % (vname, fmt_cycle(explain(threads, weird, check_rvwmo))))
    print("\n== 허용 이유 (RVWMO): SB+fence.tso 의 weird 실행 하나 — cycle 이 없다")
    th, w = shape("SB", "fence.tso", "fence.tso")
    for x in executions(build(th)):
        if matches(x.outcome(), w) and check_rvwmo(x) is None:
            print("  ppo =", ", ".join("%s->%s [%s]" % (a.name(), b.name(), l) for a, b, l in ppo(x, ann_rvwmo))
                  or "(없음 — fence.tso 는 store→load 를 잡지 않는다)")
            print("  fr  =", ", ".join("%s->%s" % (a.name(), b.name()) for a, b, _ in rel_fr(x)))
            break
    print("\n== C11 매핑: spec 표 vs clang21/gcc15 rv64gc vs rv64gc_ztso")
    for k in sorted(OBSERVED["clang21 rv64gc"]):
        print("%-18s spec=%-34s cc=%-34s ztso=%s" % (
            "%s.%s" % k, "; ".join(lower_spec(*k)), "; ".join(OBSERVED["clang21 rv64gc"][k]),
            "; ".join(OBSERVED["clang21 rv64gc_ztso"][k]) or "(없음)"))


def main():
    run("Part A", part_a)
    run("Part B", part_b)
    run("Part C", part_c)
    if "--table" in sys.argv[1:]:
        print_tables()
    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
