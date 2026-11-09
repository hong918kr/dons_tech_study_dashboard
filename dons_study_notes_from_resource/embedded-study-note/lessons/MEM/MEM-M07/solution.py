#!/usr/bin/env python3
"""MEM-M07 메모리 일관성 모델 시뮬레이터: SC / TSO / ARMv8 허용 결과 전수 열거

작은 litmus 프로그램을 읽어 "후보 실행(candidate execution)" 을 전부 만들고
(각 read 가 어느 write 를 읽나 = rf, 위치별 write 의 전체 순서 = co),
모델의 공리(axiom: 어떤 관계 그래프가 사이클이 없어야 한다)로 걸러 허용 결과 집합을 계산한다.

  Part A  후보 실행 열거 + fr 유도 + SC (po ∪ rf ∪ co ∪ fr 비순환)
  Part B  x86-TSO (ppo = po − W→R, rfi 제외, mfence) / ARMv8 (internal + external ob = obs ∪ bob)
          + "왜 금지인가" 사이클 출력 / "왜 허용인가" SC 사이클에서 빠진 간선
  Part C  모델 예측 vs Apple M6 실측(다른 레슨 숫자) 대조 — 금지인데 관찰되면 VIOLATION

  python3 -I solution.py                 # 테스트 (N/M passed, 실패 시 exit 1)
  python3 -I solution.py --table         # 리트머스 × 모델 표
  python3 -I solution.py --sets MP       # 모델별 허용 결과 집합
  python3 -I solution.py --explain SB    # 사이클/증인 설명
  python3 -I solution.py --file example.litmus   # 파일의 리트머스 열거

Python 3 표준 라이브러리만 (3.9 호환).
"""
import os
import sys
from collections import deque
from itertools import permutations, product

HERE = os.path.dirname(os.path.abspath(__file__))

# 명령: W x 1 / Wrel x 1 (STLR) / R x r0 / Racq x r0 (LDAR, RCsc) / Rapc x r0 (LDAPR, RCpc)
#       F (DMB ISH, MFENCE, seq_cst fence) / Fst (DMB ISHST) / Fld (DMB ISHLD)
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


# ---------------------------------------------------------------- Part C: 실측 대조
# Apple M6 실측 (다른 레슨에서 측정한 숫자 — 여기서는 다시 재지 않는다). runs = 실행별 이상 결과 횟수.
OBSERVED = [
    # (litmus, 출처, 반복/실행, 실행별 관찰 횟수)
    ("MP", "ARM-M05 plain+plain", "20M", [4306529, 3667459, 6307666, 6631989, 6250475]),
    ("MP", "C-A02 demo_litmus mode0", "flag=1 ~4.3-5.1e7", [3, 35, 5911]),
    ("MP+F+po", "ARM-M05 dmb ish + plain", "20M", [3560, 309, 583, 450, 940]),
    ("MP+rel+po", "ARM-M05 stlr + plain", "20M", [28, 849, 695, 110, 1004]),
    ("MP+F+F", "ARM-M05 dmb ish + dmb ish", "20M", [0, 0, 0, 0, 0]),
    ("MP+Fst+Fld", "ARM-M05 dmb ishst + dmb ishld", "20M", [0, 0, 0, 0, 0]),
    ("MP+rel+acq", "ARM-M05 stlr + ldar", "20M", [0, 0, 0, 0, 0]),
    ("MP+rel+apc", "ARM-M05 stlr + ldapr", "20M", [0, 0, 0, 0, 0]),
    ("SB", "ARM-M05 str;ldr", "20M", [646, 1506, 337, 1717, 1, 0, 1, 0, 37, 1, 1591, 1, 63]),
    ("SB", "C-A03 relaxed", "5M", [91, 87, 0, 5436, 0, 137, 0, 315]),
    ("SB+Fst+Fst", "ARM-M05 str;dmb ishst;ldr", "20M",
     [2014, 789, 651, 487, 58, 363, 762, 465, 3034, 11, 596, 628, 680]),
    ("SB+rel+apc", "ARM-M05 stlr;ldapr", "20M", [0, 0, 0, 0, 13, 1, 106, 112, 92, 0, 751, 24, 0]),
    ("SB+rel+apc", "C-A03 release/acquire", "5M", [0, 18, 0, 0, 0, 0, 0, 0]),
    ("SB+F+F", "ARM-M05 str;dmb ish;ldr", "20M", [0] * 13),
    ("SB+rel+acq", "ARM-M05 stlr;ldar", "20M", [0] * 13),
    ("SB+rel+acq", "C-A03 seq_cst", "5M", [0] * 8),
    ("LB", "ARCH-A05 측정값 참조", "-", None),
    ("IRIW", "ARCH-A05 측정값 참조 (선택)", "-", None),
    ("2+2W", "ARCH-A05 측정값 참조 (선택)", "-", None),
]


def classify(model_allows, runs):
    """모델 판정 × 관찰 → 분류
    runs None            → "no-data"
    허용 + 한 번이라도 관찰 → "seen"            (허용의 실례)
    허용 + 전부 0         → "allowed-unseen"   (허용 ≠ 관찰: 버그 아님, 단 이 칩이 더 강하다는 신호)
    금지 + 전부 0         → "ok"
    금지 + 한 번이라도 관찰 → "VIOLATION"       (칩 버그 / 모델 오류 / 테스트 하네스 버그)"""
    if runs is None:
        return "no-data"
    seen = any(n > 0 for n in runs)
    if model_allows:
        return "seen" if seen else "allowed-unseen"
    return "VIOLATION" if seen else "ok"


def compare_table(tests, model="ARMv8"):
    rows = []
    for name, src, iters, runs in OBSERVED:
        a = weird_allowed(tests[name], model)
        rows.append((name, src, iters, "allow" if a else "forbid", runs, classify(a, runs)))
    return rows


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


def S(*rows):
    """S((0,0),(0,1)) → 결과 집합 (키는 검사 대상 테스트의 레지스터 순서)"""
    return set(rows)


def as_vals(outs):
    return {tuple(v for _, v in o) for o in outs}


# 위키/논문의 알려진 결과 (Alglave–Maranget–Tautschnig 2014, Pulte et al. 2018, Sewell et al. 2010):
# 이상 결과(exists) 허용 여부  SC / TSO / ARMv8
EXPECTED_WEIRD = {
    "MP": (0, 0, 1), "MP+F+po": (0, 0, 1), "MP+F+F": (0, 0, 0), "MP+Fst+Fld": (0, 0, 0),
    "MP+rel+po": (0, 0, 1), "MP+rel+acq": (0, 0, 0), "MP+rel+apc": (0, 0, 0),
    "SB": (0, 1, 1), "SB+F+F": (0, 0, 0), "SB+Fst+Fst": (0, 1, 1),
    "SB+rel+acq": (0, 1, 0), "SB+rel+apc": (0, 1, 1), "SB+rfi-pos": (0, 1, 1),
    "LB": (0, 0, 1), "LB+F+F": (0, 0, 0), "LB+rel+rel": (0, 0, 0),
    "IRIW": (0, 0, 1), "IRIW+F+F": (0, 0, 0), "IRIW+acq+acq": (0, 0, 0),
    "2+2W": (0, 0, 1), "2+2W+Fst+Fst": (0, 0, 0), "2+2W+rel+rel": (0, 0, 0),
    "CoRR": (0, 0, 0),
}


def run_tests():
    T = load_all()
    print("== 파서")
    check("23 litmus tests parsed", lambda: len(T) == 23)
    check("IRIW has 4 threads, 6 memory events + 2 init writes",
          lambda: len(T["IRIW"].prog) == 4 and len(T["IRIW"].mem) == 8)
    check("cond parsed: 2+2W exists (x=1 /\\ y=1)", lambda: T["2+2W"].cond == (("x", 1), ("y", 1)))

    print("== Part A: 후보 실행과 SC")
    check("candidate counts MP/SB/LB/IRIW/2+2W = 4/4/4/16/4",
          lambda: [len(candidates(T[n])) for n in ("MP", "SB", "LB", "IRIW", "2+2W")] == [4, 4, 4, 16, 4])
    check("candidate count SB+rfi-pos = 2^4 = 16 (each read: init or the one write)",
          lambda: len(candidates(T["SB+rfi-pos"])) == 16)

    def fr_mp():
        t = T["MP"]
        ex = [e for e in candidates(t) if outcome(t, e) == (("1:r0", 1), ("1:r1", 0))][0]
        R = relations(t, ex)
        wx1 = [e.id for e in t.ev if e.tid == 0 and e.loc == "x"][0]
        rx = [e.id for e in t.ev if e.tid == 1 and e.loc == "x"][0]
        return R["fr"] == {(rx, wx1)} and R["fre"] == {(rx, wx1)}
    check("MP weird execution: fr = {P1:Rx=0 -> P0:Wx=1} (read init, co-later write)", fr_mp)
    check("MP  SC  = {00,01,11}", lambda: as_vals(allowed(T["MP"], "SC")) == S((0, 0), (0, 1), (1, 1)))
    check("SB  SC  = {01,10,11}", lambda: as_vals(allowed(T["SB"], "SC")) == S((0, 1), (1, 0), (1, 1)))
    check("LB  SC  = {00,01,10}", lambda: as_vals(allowed(T["LB"], "SC")) == S((0, 0), (0, 1), (1, 0)))
    check("IRIW SC = 15 of 16 (only 1010 forbidden)",
          lambda: len(allowed(T["IRIW"], "SC")) == 15 and
          (1, 0, 1, 0) not in as_vals(allowed(T["IRIW"], "SC")))
    check("2+2W SC final (x,y) = {12,21,22}",
          lambda: as_vals(allowed(T["2+2W"], "SC")) == S((1, 2), (2, 1), (2, 2)))
    check("axiomatic SC == operational SC (all interleavings) on all 23 tests",
          lambda: all(allowed(t, "SC") == operational(t, False) for t in T.values()))

    print("== Part B: TSO 와 ARMv8")
    check("MP  TSO = SC set ; MP ARMv8 = all 4",
          lambda: allowed(T["MP"], "TSO") == allowed(T["MP"], "SC") and len(allowed(T["MP"], "ARMv8")) == 4)
    check("SB  TSO = all 4 (store buffer) ; SB ARMv8 = all 4",
          lambda: len(allowed(T["SB"], "TSO")) == 4 and len(allowed(T["SB"], "ARMv8")) == 4)
    check("LB  TSO = SC set ; LB ARMv8 = all 4",
          lambda: allowed(T["LB"], "TSO") == allowed(T["LB"], "SC") and len(allowed(T["LB"], "ARMv8")) == 4)
    check("IRIW TSO = 15 ; ARMv8 = 16 (plain loads reorder)",
          lambda: len(allowed(T["IRIW"], "TSO")) == 15 and len(allowed(T["IRIW"], "ARMv8")) == 16)
    check("axiomatic TSO == store-buffer machine on all 23 tests",
          lambda: all(allowed(t, "TSO") == operational(t, True) for t in T.values()))
    for name in sorted(EXPECTED_WEIRD):
        exp = EXPECTED_WEIRD[name]
        check("%-13s weird allowed SC/TSO/ARMv8 = %d/%d/%d" % ((name,) + exp),
              lambda name=name, exp=exp: tuple(int(weird_allowed(T[name], m)) for m, _ in MODELS) == exp)
    check("model strength SC ⊆ TSO ⊆ ARMv8 holds on every test except SB+rel+acq (STLR→LDAR orders W→R)",
          lambda: [t.name for t in T.values()
                   if not allowed(t, "SC") <= allowed(t, "TSO") <= allowed(t, "ARMv8")] == ["SB+rel+acq"])

    print("== Part B: 왜? (사이클 출력)")
    check("MP/SC cycle = po -> rf -> po -> fr",
          lambda: explain(T["MP"], "SC") == [
              "SC forbids (sc): P0:Wx=1 -po-> P0:Wy=1 -rf-> P1:Ry=1 -po-> P1:Rx=0 -fr-> P0:Wx=1"])
    check("SB/TSO allowed: SC cycle broken because W->R po is not in ppo",
          lambda: explain(T["SB"], "TSO") == [
              "TSO allows: SC cycle P0:Wx=1 -po-> P0:Ry=0 -fr-> P1:Wy=1 -po-> P1:Rx=0 -fr-> P0:Wx=1 "
              "is broken because TSO does not order: P0:Wx=1 -po-> P0:Ry=0; P1:Wy=1 -po-> P1:Rx=0"])
    check("MP+rel+acq/ARMv8 cycle uses bob twice",
          lambda: explain(T["MP+rel+acq"], "ARMv8") == [
              "ARMv8 forbids (external): P0:Wx=1 -bob-> P0:Wrel y=1 -rfe-> P1:Racq y=1 -bob-> "
              "P1:Rx=0 -fre-> P0:Wx=1"])
    check("CoRR/ARMv8 is caught by the internal (coherence) axiom",
          lambda: explain(T["CoRR"], "ARMv8")[0].startswith("ARMv8 forbids (internal):"))

    print("== Part C: 실측 대조")
    rows = compare_table(T)
    check("classify: forbidden + seen = VIOLATION", lambda: classify(False, [0, 0, 3]) == "VIOLATION")
    check("classify: allowed + all zero = allowed-unseen", lambda: classify(True, [0, 0]) == "allowed-unseen")
    check("classify: allowed + seen = seen ; forbidden + 0 = ok ; None = no-data",
          lambda: (classify(True, [0, 5]), classify(False, [0]), classify(True, None)) ==
          ("seen", "ok", "no-data"))
    check("M6 data vs ARMv8: no VIOLATION in %d rows" % len(rows),
          lambda: all(r[5] != "VIOLATION" for r in rows))
    check("M6 data: every ARMv8-allowed shape with data was seen at least once",
          lambda: all(r[5] == "seen" for r in rows if r[3] == "allow" and r[4] is not None))
    check("same M6 data judged by TSO: exactly the MP-shaped rows are VIOLATION (M6 is not TSO)",
          lambda: {r[0] for r in compare_table(T, "TSO") if r[5] == "VIOLATION"} ==
          {"MP", "MP+F+po", "MP+rel+po"})

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


# ---------------------------------------------------------------- CLI
def print_table(T):
    names = list(T)
    print("%-14s %-6s %-6s %-6s   %s" % ("test", "SC", "TSO", "ARMv8", "exists"))
    for n in names:
        t = T[n]
        cells = ["allow" if weird_allowed(t, m) else "forbid" for m, _ in MODELS]
        print("%-14s %-6s %-6s %-6s   %s" % ((n,) + tuple(cells) + (fmt_outcome(t.cond),)))


def print_sets(t):
    print("%s: %d candidate executions" % (t.name, len(candidates(t))))
    for m, _ in MODELS:
        outs = sorted(allowed(t, m))
        print("  %-5s %2d: %s" % (m, len(outs), " | ".join(fmt_outcome(o) for o in outs)))


def main(argv):
    T = load_all()
    if len(argv) == 1:
        return 0 if run_tests() else 1
    if argv[1] == "--table":
        print_table(T)
    elif argv[1] == "--sets" and len(argv) == 3:
        print_sets(T[argv[2]])
    elif argv[1] == "--explain" and len(argv) == 3:
        for m, _ in MODELS:
            for line in explain(T[argv[2]], m):
                print(line)
    elif argv[1] == "--compare":
        for model in ("ARMv8", "TSO"):
            print("-- model %s" % model)
            for name, src, iters, a, runs, cls in compare_table(T, model):
                shown = "-" if runs is None else ",".join(str(x) for x in runs)
                print("%-11s %-6s %-15s %-33s %s" % (name, a, cls, src, shown))
    elif argv[1] == "--file" and len(argv) == 3:
        with open(argv[2], encoding="utf-8") as f:
            t = parse_litmus(f.read())
        print_sets(t)
        for m, _ in MODELS:
            for line in explain(t, m):
                print(line)
    else:
        print("usage: %s [--table | --sets NAME | --explain NAME | --compare | --file F.litmus]" % argv[0])
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
