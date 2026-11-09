#!/usr/bin/env python3
"""MEM-M07 추가 연습 — 한 번에 검증 (solution.py 의 열거기를 그대로 담고 연습 1–5 를 더한다)

  연습 1  WRC 3종: multi-copy atomicity 를 모델로 확인
  연습 2  coherence 테스트 4종 + internal 공리를 뺀 돌연변이 모델
  연습 3  TSO 에서 rfi 를 빼는 이유 (store forwarding) — 돌연변이 vs store buffer 기계
  연습 4  ARMv8/TSO 최소 비용 수정 탐색 (rel/acq/apc = 1, Fst/Fld = 2, F = 3)
  연습 5  C11 memory_order → ARMv8 / x86 매핑을 모델에 넣어 SB·MP 판정

  python3 -I practice.py          # 테스트
  python3 -I practice.py --fixes  # 최소 수정 목록 출력

python3 -I 로 돌리면 스크립트 폴더가 sys.path 에 없으므로 solution.py 를 import 하지 않고 핵심 코드를 복사해 둔다.
"""
import os
import sys
from collections import deque
from itertools import permutations, product

HERE = os.path.dirname(os.path.abspath(__file__))

LITMUS_SRC = r"""
MP
{ x=0; y=0; }
 P0       | P1       ;
 W x 1    | R y r0   ;
 W y 1    | R x r1   ;
exists (1:r0=1 /\ 1:r1=0)

MP+F+po
 P0       | P1       ;
 W x 1    | R y r0   ;
 F        | R x r1   ;
 W y 1    |          ;
exists (1:r0=1 /\ 1:r1=0)

MP+F+F
 P0       | P1       ;
 W x 1    | R y r0   ;
 F        | F        ;
 W y 1    | R x r1   ;
exists (1:r0=1 /\ 1:r1=0)

MP+Fst+Fld
 P0       | P1       ;
 W x 1    | R y r0   ;
 Fst      | Fld      ;
 W y 1    | R x r1   ;
exists (1:r0=1 /\ 1:r1=0)

MP+rel+po
 P0        | P1      ;
 W x 1     | R y r0  ;
 Wrel y 1  | R x r1  ;
exists (1:r0=1 /\ 1:r1=0)

MP+rel+acq
 P0        | P1         ;
 W x 1     | Racq y r0  ;
 Wrel y 1  | R x r1     ;
exists (1:r0=1 /\ 1:r1=0)

MP+rel+apc
 P0        | P1         ;
 W x 1     | Rapc y r0  ;
 Wrel y 1  | R x r1     ;
exists (1:r0=1 /\ 1:r1=0)

SB
 P0       | P1       ;
 W x 1    | W y 1    ;
 R y r0   | R x r1   ;
exists (0:r0=0 /\ 1:r1=0)

SB+F+F
 P0       | P1       ;
 W x 1    | W y 1    ;
 F        | F        ;
 R y r0   | R x r1   ;
exists (0:r0=0 /\ 1:r1=0)

SB+Fst+Fst
 P0       | P1       ;
 W x 1    | W y 1    ;
 Fst      | Fst      ;
 R y r0   | R x r1   ;
exists (0:r0=0 /\ 1:r1=0)

SB+rel+acq
 P0         | P1         ;
 Wrel x 1   | Wrel y 1   ;
 Racq y r0  | Racq x r1  ;
exists (0:r0=0 /\ 1:r1=0)

SB+rel+apc
 P0         | P1         ;
 Wrel x 1   | Wrel y 1   ;
 Rapc y r0  | Rapc x r1  ;
exists (0:r0=0 /\ 1:r1=0)

SB+rfi-pos
 P0       | P1       ;
 W x 1    | W y 1    ;
 R x r0   | R y r2   ;
 R y r1   | R x r3   ;
exists (0:r0=1 /\ 0:r1=0 /\ 1:r2=1 /\ 1:r3=0)

LB
 P0       | P1       ;
 R x r0   | R y r1   ;
 W y 1    | W x 1    ;
exists (0:r0=1 /\ 1:r1=1)

LB+F+F
 P0       | P1       ;
 R x r0   | R y r1   ;
 F        | F        ;
 W y 1    | W x 1    ;
exists (0:r0=1 /\ 1:r1=1)

LB+rel+rel
 P0        | P1        ;
 R x r0    | R y r1    ;
 Wrel y 1  | Wrel x 1  ;
exists (0:r0=1 /\ 1:r1=1)

IRIW
 P0     | P1     | P2      | P3      ;
 W x 1  | W y 1  | R x r0  | R y r2  ;
        |        | R y r1  | R x r3  ;
exists (2:r0=1 /\ 2:r1=0 /\ 3:r2=1 /\ 3:r3=0)

IRIW+F+F
 P0     | P1     | P2      | P3      ;
 W x 1  | W y 1  | R x r0  | R y r2  ;
        |        | F       | F       ;
        |        | R y r1  | R x r3  ;
exists (2:r0=1 /\ 2:r1=0 /\ 3:r2=1 /\ 3:r3=0)

IRIW+acq+acq
 P0     | P1     | P2         | P3         ;
 W x 1  | W y 1  | Racq x r0  | Racq y r2  ;
        |        | R y r1     | R x r3     ;
exists (2:r0=1 /\ 2:r1=0 /\ 3:r2=1 /\ 3:r3=0)

2+2W
 P0       | P1       ;
 W x 1    | W y 1    ;
 W y 2    | W x 2    ;
exists (x=1 /\ y=1)

2+2W+Fst+Fst
 P0       | P1       ;
 W x 1    | W y 1    ;
 Fst      | Fst      ;
 W y 2    | W x 2    ;
exists (x=1 /\ y=1)

2+2W+rel+rel
 P0        | P1        ;
 W x 1     | W y 1     ;
 Wrel y 2  | Wrel x 2  ;
exists (x=1 /\ y=1)

CoRR
 P0       | P1       ;
 W x 1    | R x r0   ;
          | R x r1   ;
exists (1:r0=1 /\ 1:r1=0)
"""

OPS = ("W", "Wrel", "R", "Racq", "Rapc", "F", "Fst", "Fld")
ANN = {"W": "", "Wrel": "rel", "R": "", "Racq": "acq", "Rapc": "apc",
       "F": "full", "Fst": "st", "Fld": "ld"}


class Ev(object):
    """이벤트 하나. init write 는 tid = -1."""
    __slots__ = ("id", "tid", "idx", "k", "op", "ann", "loc", "val", "reg")

    def __init__(self, id_, tid, idx, op, loc=None, arg=None):
        self.id, self.tid, self.idx, self.op = id_, tid, idx, op
        self.k = op[0]                     # 'W' / 'R' / 'F'
        self.ann = ANN[op]
        self.loc = loc
        self.val = arg if self.k == "W" else None
        self.reg = arg if self.k == "R" else None


class Litmus(object):
    def __init__(self, name, init, prog, cond):
        self.name, self.init, self.prog, self.cond = name, init, prog, cond
        locs = set(init)
        for ins in prog:
            for op, loc, _ in ins:
                if loc:
                    locs.add(loc)
        self.locs = sorted(locs)
        self.ev = []
        for loc in self.locs:                                   # init writes 먼저
            self.ev.append(Ev(len(self.ev), -1, -1, "W", loc, init.get(loc, 0)))
        for tid, ins in enumerate(prog):
            for idx, (op, loc, arg) in enumerate(ins):
                self.ev.append(Ev(len(self.ev), tid, idx, op, loc, arg))
        self.reads = [e for e in self.ev if e.k == "R"]
        self.mem = [e for e in self.ev if e.k != "F"]
        self.writes = {loc: [e.id for e in self.ev if e.k == "W" and e.loc == loc] for loc in self.locs}
        self.obs_locs = [k for k, _ in cond if ":" not in k]    # exists 에 나온 메모리 위치


# ---------------------------------------------------------------- 파서 (주어진 코드)
def parse_instr(cell):
    tok = cell.split()
    op = tok[0]
    if op not in OPS:
        raise ValueError("unknown op %r" % cell)
    if op[0] == "F":
        if len(tok) != 1:
            raise ValueError("fence takes no operand: %r" % cell)
        return (op, None, None)
    if len(tok) != 3:
        raise ValueError("expected '%s loc arg': %r" % (op, cell))
    if op[0] == "W":
        return (op, tok[1], int(tok[2]))
    return (op, tok[1], tok[2])


def parse_cond(line):
    body = line[len("exists"):].strip().strip("()")
    cond = []
    for part in body.split("/\\"):
        k, v = part.split("=")
        cond.append((k.strip(), int(v)))
    return tuple(cond)


def parse_litmus(text):
    lines = [l.strip() for l in text.strip().splitlines()]
    lines = [l for l in lines if l and not l.startswith("#")]
    name, init, i = lines[0].split()[0], {}, 1
    if lines[i].startswith("{"):
        for part in lines[i].strip("{} ").split(";"):
            if part.strip():
                k, v = part.split("=")
                init[k.strip()] = int(v)
        i += 1
    nthr = len(lines[i].rstrip(";").split("|"))
    i += 1
    prog = [[] for _ in range(nthr)]
    while not lines[i].startswith("exists"):
        cells = lines[i].rstrip(";").split("|")
        if len(cells) != nthr:
            raise ValueError("%s: row has %d columns, header %d" % (name, len(cells), nthr))
        for t, c in enumerate(cells):
            if c.strip():
                prog[t].append(parse_instr(c.strip()))
        i += 1
    return Litmus(name, init, prog, parse_cond(lines[i]))


def load_all(src=LITMUS_SRC):
    tests = {}
    for block in src.strip().split("\n\n"):
        t = parse_litmus(block)
        tests[t.name] = t
    return tests


# ---------------------------------------------------------------- Part A: 후보 실행 + 관계
def candidates(t):
    """모든 후보 실행 (rf, co).
    rf: {read id: write id} — 같은 위치의 write (init 포함) 중 하나
    co: {loc: (init id, w1, w2, ...)} — init 이 항상 맨 앞, 나머지는 모든 순열"""
    out = []
    rf_choices = [t.writes[r.loc] for r in t.reads]
    co_choices = []
    for loc in t.locs:
        init_w, rest = t.writes[loc][0], t.writes[loc][1:]
        co_choices.append([(init_w,) + p for p in permutations(rest)])
    for rf_pick in product(*rf_choices):
        rf = {r.id: w for r, w in zip(t.reads, rf_pick)}
        for co_pick in product(*co_choices):
            out.append((rf, dict(zip(t.locs, co_pick))))
    return out


def relations(t, ex):
    """기본 관계 (이벤트 id 쌍의 집합). fence 는 노드가 아니다 — fence_between 으로만 쓴다."""
    rf_map, co_map = ex
    E = t.ev
    R = {}
    R["po"] = {(a.id, b.id) for a in t.mem for b in t.mem
               if a.tid == b.tid and a.tid >= 0 and a.idx < b.idx}
    R["po-loc"] = {(a, b) for a, b in R["po"] if E[a].loc == E[b].loc}
    R["rf"] = {(w, r) for r, w in rf_map.items()}
    R["co"] = {(order[i], order[j]) for order in co_map.values()
               for i in range(len(order)) for j in range(i + 1, len(order))}
    R["fr"] = {(r, w2) for r, w in rf_map.items() for (w1, w2) in R["co"] if w1 == w}
    for name in ("rf", "co", "fr"):                     # 외부(e) / 내부(i): 스레드가 다른가
        R[name + "e"] = {(a, b) for a, b in R[name] if E[a].tid != E[b].tid}
        R[name + "i"] = R[name] - R[name + "e"]
    return R


def fence_between(t, a, b, kinds):
    """같은 스레드에서 a 와 b 사이(po)에 kinds 종류의 fence 가 있나"""
    A, B = t.ev[a], t.ev[b]
    return any(f.k == "F" and f.tid == A.tid and A.idx < f.idx < B.idx and f.ann in kinds
               for f in t.ev)


def lab(R, *names):
    """관계 여러 개 → {(a,b): 라벨}. 같은 간선이 여러 관계에 있으면 앞의 이름을 쓴다."""
    out = {}
    for n in names:
        for e in R[n]:
            out.setdefault(e, n)
    return out


def sc_axioms(t, R):
    """SC (Lamport): po ∪ rf ∪ co ∪ fr 비순환"""
    return [("sc", lab(R, "po", "rf", "co", "fr"))]


# ---------------------------------------------------------------- Part B: TSO / ARMv8
def tso_axioms(t, R):
    """x86-TSO (herd x86tso.cat 형태)
    uniproc: po-loc ∪ rf ∪ co ∪ fr 비순환
    tso    : ppo ∪ rfe ∪ co ∪ fr ∪ mfence 비순환,  ppo = po − (W×R),  rfi 는 빠짐 (store forwarding)
    acq/rel 표시는 TSO 에서 의미 없음 (x86 mov 가 이미 acquire/release). Fst/Fld 도 W→R 을 못 막는다."""
    E = t.ev
    R = dict(R)
    R["ppo"] = {(a, b) for a, b in R["po"] if not (E[a].k == "W" and E[b].k == "R")}
    R["mfence"] = {(a, b) for a, b in R["po"] if fence_between(t, a, b, ("full",))}
    return [("uniproc", lab(R, "po-loc", "rf", "co", "fr")),
            ("tso", lab(R, "ppo", "rfe", "co", "fr", "mfence"))]


def arm_bob(t, R):
    """Barrier-ordered-before (aarch64.cat 의 bob, 의존성·RMW 제외)"""
    E = t.ev
    bob = set()
    for a, b in R["po"]:
        A, B = E[a], E[b]
        if (fence_between(t, a, b, ("full",))                                 # po; [DMB.FULL]; po
                or (A.k == "R" and fence_between(t, a, b, ("ld",)))           # [R]; po; [DMB.LD]; po
                or (A.k == "W" and B.k == "W" and fence_between(t, a, b, ("st",)))  # [W];po;[DMB.ST];po;[W]
                or (A.ann == "rel" and B.ann == "acq")                        # [L]; po; [A]  (STLR→LDAR)
                or A.ann in ("acq", "apc")                                    # [A|Q]; po
                or B.ann == "rel"):                                           # po; [L]
            bob.add((a, b))
    return bob


def arm_axioms(t, R):
    """ARMv8 (Pulte et al. POPL 2018 / aarch64.cat 단순화)
    internal: po-loc ∪ rf ∪ co ∪ fr 비순환   (위치별 SC = coherence)
    external: ob = obs ∪ bob 비순환,  obs = rfe ∪ coe ∪ fre   (dob/aob 는 이 레슨 범위 밖)"""
    R = dict(R)
    R["bob"] = arm_bob(t, R)
    return [("internal", lab(R, "po-loc", "rf", "co", "fr")),
            ("external", lab(R, "rfe", "coe", "fre", "bob"))]


MODELS = (("SC", sc_axioms), ("TSO", tso_axioms), ("ARMv8", arm_axioms))
MODEL = dict(MODELS)


# ---------------------------------------------------------------- 공통: 사이클 · 허용 집합 (주어진 코드)
def find_cycle(edges):
    """가장 짧은 사이클 [n0, n1, ..., n0] (없으면 None). 결정적: 노드·이웃 정렬 순회."""
    succ = {}
    for a, b in sorted(edges):
        succ.setdefault(a, []).append(b)
    best = None
    for s in sorted(succ):
        prev, q = {s: None}, deque([s])
        found = None
        while q and found is None:
            u = q.popleft()
            for v in succ.get(u, ()):
                if v == s:
                    found = u
                    break
                if v not in prev:
                    prev[v] = u
                    q.append(v)
        if found is None:
            continue
        path, u = [], found
        while u is not None:
            path.append(u)
            u = prev[u]
        cyc = path[::-1] + [s]
        if best is None or len(cyc) < len(best):
            best = cyc
    return best


def consistent(axioms):
    return all(find_cycle(edges) is None for _, edges in axioms)


def outcome(t, ex):
    rf_map, co_map = ex
    res = [("%d:%s" % (t.ev[r].tid, t.ev[r].reg), t.ev[w].val) for r, w in rf_map.items()]
    res += [(loc, t.ev[co_map[loc][-1]].val) for loc in t.obs_locs]
    return tuple(sorted(res))


def allowed(t, model):
    fn = MODEL[model] if isinstance(model, str) else model
    out = set()
    for ex in candidates(t):
        if consistent(fn(t, relations(t, ex))):
            out.add(outcome(t, ex))
    return out


def matches(outc, cond):
    d = dict(outc)
    return all(d.get(k) == v for k, v in cond)


def weird_allowed(t, model):
    return any(matches(o, t.cond) for o in allowed(t, model))


def fmt_outcome(o):
    return " ".join("%s=%d" % kv for kv in o)


# ---------------------------------------------------------------- 운영 모델 (교차 검증용, 주어진 코드)
def operational(t, store_buffer):
    """SC = 인터리빙 전수 / TSO = 스레드별 FIFO store buffer + 임의 시점 drain + F 는 버퍼가 빌 때만.
    (Owens–Sarkar–Sewell: 이 기계와 x86-TSO 공리 모델은 같은 결과를 허용한다.)"""
    n = len(t.prog)
    start = (tuple([0] * n), tuple([()] * n), tuple(sorted(t.init.items())), ())
    seen, stack, finals = set(), [start], set()
    while stack:
        st = stack.pop()
        if st in seen:
            continue
        seen.add(st)
        pcs, bufs, mem, regs = st
        memd = dict(mem)
        moved = False
        for tid in range(n):
            if store_buffer and bufs[tid]:                       # drain 가장 오래된 store
                (loc, v), rest = bufs[tid][0], bufs[tid][1:]
                memd2 = dict(memd)
                memd2[loc] = v
                stack.append((pcs, bufs[:tid] + (rest,) + bufs[tid + 1:],
                              tuple(sorted(memd2.items())), regs))
                moved = True
            if pcs[tid] >= len(t.prog[tid]):
                continue
            op, loc, arg = t.prog[tid][pcs[tid]]
            npcs = pcs[:tid] + (pcs[tid] + 1,) + pcs[tid + 1:]
            if op[0] == "F":
                if op == "F" and store_buffer and bufs[tid]:
                    continue                                     # mfence: 버퍼가 빌 때까지 대기
                stack.append((npcs, bufs, mem, regs))
            elif op[0] == "W":
                if store_buffer:
                    nb = bufs[:tid] + (bufs[tid] + ((loc, arg),),) + bufs[tid + 1:]
                    stack.append((npcs, nb, mem, regs))
                else:
                    memd2 = dict(memd)
                    memd2[loc] = arg
                    stack.append((npcs, bufs, tuple(sorted(memd2.items())), regs))
            else:
                v = memd.get(loc, 0)
                for bl, bv in bufs[tid]:                         # 자기 버퍼의 최신 값 (forwarding)
                    if bl == loc:
                        v = bv
                stack.append((npcs, bufs, mem, tuple(sorted(regs + (("%d:%s" % (tid, arg), v),)))))
            moved = True
        if not moved:
            res = list(regs) + [(loc, memd.get(loc, 0)) for loc in t.obs_locs]
            finals.add(tuple(sorted(res)))
    return finals


# ---------------------------------------------------------------- Part B: 설명 (주어진 코드)
def ev_name(t, ex, i):
    e = t.ev[i]
    who = "init" if e.tid < 0 else "P%d" % e.tid
    val = e.val if e.k == "W" else t.ev[ex[0][i]].val
    return "%s:%s%s=%d" % (who, e.op, e.loc, val) if len(e.op) == 1 else \
        "%s:%s %s=%d" % (who, e.op, e.loc, val)


def fmt_cycle(t, ex, cyc, edges):
    s = ev_name(t, ex, cyc[0])
    for a, b in zip(cyc, cyc[1:]):
        s += " -%s-> %s" % (edges[(a, b)], ev_name(t, ex, b))
    return s


def explain(t, model):
    """exists 결과를 내는 후보 실행마다: 금지면 깨진 공리와 사이클, 허용이면 SC 사이클 중 이 모델이 버린 간선"""
    lines = []
    for ex in candidates(t):
        if not matches(outcome(t, ex), t.cond):
            continue
        R = relations(t, ex)
        axioms = MODEL[model](t, R)
        broken = [(n, e, find_cycle(e)) for n, e in axioms]
        broken = [(n, e, c) for n, e, c in broken if c]
        if broken:
            n, e, c = broken[0]
            lines.append("%s forbids (%s): %s" % (model, n, fmt_cycle(t, ex, c, e)))
            continue
        kept = set()
        for _, e in axioms:
            kept |= set(e)
        sc = sc_axioms(t, R)[0][1]
        c = find_cycle(sc)
        if c is None:
            lines.append("%s allows: SC also allows this execution" % model)
        else:
            dropped = ["%s -%s-> %s" % (ev_name(t, ex, a), sc[(a, b)], ev_name(t, ex, b))
                       for a, b in zip(c, c[1:]) if (a, b) not in kept]
            lines.append("%s allows: SC cycle %s is broken because %s does not order: %s"
                         % (model, fmt_cycle(t, ex, c, sc), model, "; ".join(dropped)))
    return lines



# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL (starter 의 미구현이 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    try:
        ok, why = bool(fn()), ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


# ================================================================ 추가 연습
PRACTICE_SRC = r"""
WRC
 P0     | P1       | P2      ;
 W x 1  | R x r0   | R y r1  ;
        | W y 1    | R x r2  ;
exists (1:r0=1 /\ 2:r1=1 /\ 2:r2=0)

WRC+po+acq
 P0     | P1       | P2         ;
 W x 1  | R x r0   | Racq y r1  ;
        | W y 1    | R x r2     ;
exists (1:r0=1 /\ 2:r1=1 /\ 2:r2=0)

WRC+rel+acq
 P0     | P1        | P2         ;
 W x 1  | R x r0    | Racq y r1  ;
        | Wrel y 1  | R x r2     ;
exists (1:r0=1 /\ 2:r1=1 /\ 2:r2=0)

CoWW
 P0     ;
 W x 1  ;
 W x 2  ;
exists (x=1)

CoRW1
 P0      ;
 R x r0  ;
 W x 1   ;
exists (0:r0=1)

CoWR
 P0      | P1     ;
 W x 1   | W x 2  ;
 R x r0  |        ;
exists (0:r0=2 /\ x=1)

CoRW2
 P0      | P1     ;
 R x r0  | W x 1  ;
 W x 2   |        ;
exists (0:r0=1 /\ x=1)
"""


# ---- 연습 2: 돌연변이 모델 — 공리를 하나 빼면 무엇이 새나
def arm_external_only(t, R):
    """internal(coherence) 공리를 뺀 ARMv8 — 위치별 SC 가 없다"""
    return [a for a in arm_axioms(t, R) if a[0] == "external"]


# ---- 연습 3: rfi 를 전역 순서에 넣은 (틀린) TSO
def tso_with_rfi(t, R):
    E = t.ev
    R = dict(R)
    R["ppo"] = {(a, b) for a, b in R["po"] if not (E[a].k == "W" and E[b].k == "R")}
    R["mfence"] = {(a, b) for a, b in R["po"] if fence_between(t, a, b, ("full",))}
    return [("uniproc", lab(R, "po-loc", "rf", "co", "fr")),
            ("tso", lab(R, "ppo", "rf", "co", "fr", "mfence"))]      # rfe → rf (버그)


# ---- 연습 4: 최소 비용 수정 탐색
COST = {"Wrel": 1, "Racq": 1, "Rapc": 1, "Fst": 2, "Fld": 2, "F": 3}


def variants(t):
    """각 명령을 강화(W→Wrel, R→Racq/Rapc)하거나 스레드 안 명령 사이에 F/Fst/Fld 를 넣은 모든 변형.
    yield (비용, 바뀐 것 목록, 새 prog)"""
    per_thread = []
    for tid, ins in enumerate(t.prog):
        slots = []
        for i, (op, loc, arg) in enumerate(ins):
            opts = [((op, loc, arg), None)]
            ups = ("Wrel",) if op == "W" else ("Racq", "Rapc") if op == "R" else ()
            opts += [((u, loc, arg), "P%d:%s%s->%s" % (tid, op, loc, u)) for u in ups]
            slots.append(opts)
            if i + 1 < len(ins):
                slots.append([(None, None)] + [((f, None, None), "P%d:%s after %s%s" % (tid, f, op, loc))
                                               for f in ("F", "Fst", "Fld")])
        per_thread.append(list(product(*slots)))
    for combo in product(*per_thread):
        prog, changes, cost = [], [], 0
        for th in combo:
            prog.append([ins for ins, _ in th if ins is not None])
            for ins, ch in th:
                if ch:
                    changes.append(ch)
                    cost += COST[ins[0]]
        yield cost, tuple(sorted(changes)), prog


def min_fixes(t, model="ARMv8"):
    best, fixes = None, []
    for cost, changes, prog in variants(t):
        if best is not None and cost > best:
            continue
        v = Litmus(t.name + "*", t.init, prog, t.cond)
        if weird_allowed(v, model):
            continue
        if best is None or cost < best:
            best, fixes = cost, []
        fixes.append(changes)
    return best, sorted(fixes)


# ---- 연습 5: C11 → 명령 매핑을 모델에 넣어 보기
# Apple clang arm64 (C-A03 / ARM-M05 의 -S 실측): relaxed = ldr/str, acquire load = ldapr,
# seq_cst load = ldar, release·seq_cst store = stlr, fence(acquire) = dmb ishld, fence(release/seq_cst) = dmb ish
# x86-64 (표준 매핑, Batty et al. 2011 / cpp0xmappings): 모든 load = mov, seq_cst store = xchg (= mov + mfence 효과),
# 그 외 store = mov, fence(seq_cst) = mfence, 그 외 fence = 명령 없음
def c11_map(isa, c11prog):
    out = []
    for th in c11prog:
        ins = []
        for a in th:
            kind, mo = a[0], a[-1]
            if kind == "st":
                if isa == "arm":
                    ins.append(("Wrel" if mo in ("rel", "sc") else "W", a[1], a[2]))
                else:
                    ins.append(("W", a[1], a[2]))
                    if mo == "sc":
                        ins.append(("F", None, None))
            elif kind == "ld":
                op = "R"
                if isa == "arm":
                    op = {"rlx": "R", "acq": "Rapc", "sc": "Racq"}[mo]
                ins.append((op, a[1], a[2]))
            else:
                if isa == "arm":
                    ins.append(("Fld" if mo == "acq" else "F", None, None))
                elif mo == "sc":
                    ins.append(("F", None, None))
        out.append(ins)
    return out


def c11_sb(mo_st, mo_ld, fence=None):
    th = lambda me, other, reg: [("st", me, 1, mo_st)] + ([("fence", fence)] if fence else []) + \
        [("ld", other, reg, mo_ld)]
    return [th("x", "y", "r0"), th("y", "x", "r1")], (("0:r0", 0), ("1:r1", 0))


def c11_mp(mo_st, mo_ld, wfence=None, rfence=None):
    w = [("st", "x", 1, "rlx")] + ([("fence", wfence)] if wfence else []) + [("st", "y", 1, mo_st)]
    r = [("ld", "y", "r0", mo_ld)] + ([("fence", rfence)] if rfence else []) + [("ld", "x", "r1", "rlx")]
    return [w, r], (("1:r0", 1), ("1:r1", 0))


def c11_weird(isa, prog_cond):
    prog, cond = prog_cond
    model = "ARMv8" if isa == "arm" else "TSO"
    return weird_allowed(Litmus("c11", {}, c11_map(isa, prog), cond), model)


def run_practice():
    T = load_all()
    P = load_all(PRACTICE_SRC)

    print("== 연습 1: WRC 와 multi-copy atomicity")
    check("WRC          SC/TSO/ARMv8 = forbid/forbid/allow",
          lambda: [weird_allowed(P["WRC"], m) for m, _ in MODELS] == [False, False, True])
    check("WRC+po+acq   ARMv8 allow (P1 의 R→W 순서 없음)", lambda: weird_allowed(P["WRC+po+acq"], "ARMv8"))
    check("WRC+rel+acq  ARMv8 forbid (MCA: P1 이 본 x 는 P2 에게도 보인다)",
          lambda: not weird_allowed(P["WRC+rel+acq"], "ARMv8"))
    check("WRC+rel+acq  cycle printout",
          lambda: explain(P["WRC+rel+acq"], "ARMv8") == [
              "ARMv8 forbids (external): P0:Wx=1 -rfe-> P1:Rx=1 -bob-> P1:Wrel y=1 -rfe-> P2:Racq y=1 "
              "-bob-> P2:Rx=0 -fre-> P0:Wx=1"])

    print("== 연습 2: coherence 테스트와 internal 공리")
    for n in ("CoWW", "CoRW1", "CoWR", "CoRW2"):
        check("%-6s forbidden in SC, TSO, ARMv8" % n,
              lambda n=n: not any(weird_allowed(P[n], m) for m, _ in MODELS))
    check("ARMv8 without internal axiom: CoRR and CoWW become allowed",
          lambda: weird_allowed(T["CoRR"], arm_external_only) and weird_allowed(P["CoWW"], arm_external_only))
    check("... but MP+rel+acq is still forbidden (external alone does that)",
          lambda: not weird_allowed(T["MP+rel+acq"], arm_external_only))

    print("== 연습 3: TSO 의 rfi (store forwarding)")
    check("real TSO + store-buffer machine allow SB+rfi-pos",
          lambda: weird_allowed(T["SB+rfi-pos"], "TSO") and
          any(matches(o, T["SB+rfi-pos"].cond) for o in operational(T["SB+rfi-pos"], True)))
    check("mutant TSO (rf instead of rfe in ghb) forbids it -> mutant != machine",
          lambda: not weird_allowed(T["SB+rfi-pos"], tso_with_rfi))
    check("mutant agrees with TSO on every test without a same-thread read-after-write",
          lambda: [n for n, t in T.items() if allowed(t, tso_with_rfi) != allowed(t, "TSO")] == ["SB+rfi-pos"])

    print("== 연습 4: ARMv8 최소 수정")
    check("MP  : cost 2, {Wrel y + Racq y} or {Wrel y + Rapc y}",
          lambda: min_fixes(T["MP"]) == (2, [("P0:Wy->Wrel", "P1:Ry->Racq"), ("P0:Wy->Wrel", "P1:Ry->Rapc")]))
    check("SB  : cost 4, unique = rel+acq(LDAR) on both threads (= C11 seq_cst mapping)",
          lambda: min_fixes(T["SB"]) == (4, [("P0:Ry->Racq", "P0:Wx->Wrel", "P1:Rx->Racq", "P1:Wy->Wrel")]))
    check("LB  : cost 2, 3 x 3 = 9 fixes (Racq / Rapc / Wrel per thread)",
          lambda: min_fixes(T["LB"])[0] == 2 and len(min_fixes(T["LB"])[1]) == 9)
    check("2+2W: cost 2, unique = Wrel on the second store of each thread",
          lambda: min_fixes(T["2+2W"]) == (2, [("P0:Wy->Wrel", "P1:Wx->Wrel")]))
    check("SB under TSO: cost 6, unique = F on both threads (acq/rel are free on x86 and useless)",
          lambda: min_fixes(T["SB"], "TSO") == (6, [("P0:F after Wx", "P1:F after Wy")]))

    print("== 연습 5: C11 매핑 → 모델")
    check("SB seq_cst: forbidden on ARMv8 mapping and x86 mapping",
          lambda: not c11_weird("arm", c11_sb("sc", "sc")) and not c11_weird("x86", c11_sb("sc", "sc")))
    check("SB release/acquire: allowed on both (C11 also allows it)",
          lambda: c11_weird("arm", c11_sb("rel", "acq")) and c11_weird("x86", c11_sb("rel", "acq")))
    check("SB relaxed + seq_cst fences: forbidden on both",
          lambda: not c11_weird("arm", c11_sb("rlx", "rlx", "sc")) and not c11_weird("x86", c11_sb("rlx", "rlx", "sc")))
    check("MP release/acquire: forbidden on both ; MP relaxed: allowed on ARMv8 only",
          lambda: not c11_weird("arm", c11_mp("rel", "acq")) and not c11_weird("x86", c11_mp("rel", "acq"))
          and c11_weird("arm", c11_mp("rlx", "rlx")) and not c11_weird("x86", c11_mp("rlx", "rlx")))
    check("MP relaxed + release fence / acquire fence (dmb ish / dmb ishld): forbidden on ARMv8",
          lambda: not c11_weird("arm", c11_mp("rlx", "rlx", "rel", "acq")))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--fixes":
        T = load_all()
        for n in ("MP", "SB", "LB", "2+2W"):
            for m in ("ARMv8", "TSO"):
                c, fx = min_fixes(T[n], m)
                print("%-5s %-5s cost %d: %s" % (n, m, c, " | ".join(" + ".join(f) for f in fx if f)
                                                  or "(이미 금지 — 고칠 것 없음)"))
        sys.exit(0)
    sys.exit(0 if run_practice() else 1)
