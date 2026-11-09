#!/usr/bin/env python3
"""DIST-A05 추가 연습 답. solution.py 의 검사기/시뮬레이터를 파일 경로로 불러 쓴다.

  P1 FIFO 큐 명세 + DS-A03 의 "빈 큐" 답    P2 CAS 레지스터 명세    P3 순차 일관성 검사기
  P4 write-back 생략 fast path (조건 R >= W)  P5 축소에서 well_formed 를 빼면 생기는 가짜 위반

  python3 -I practice.py
"""
import importlib.util
import os
import sys

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "solution.py")
spec = importlib.util.spec_from_file_location("dist_a05", path)
s = importlib.util.module_from_spec(spec)
sys.modules["dist_a05"] = s
spec.loader.exec_module(s)
Op = s.Op

passed, total = 0, 0


def check(name, cond, detail=""):
    global passed, total
    total += 1
    if cond is True:
        passed += 1
        print("PASS %s" % name)
    else:
        print("FAIL %s: %s" % (name, detail if detail else cond))


# ---------------------------------------------------------------- P1
class QueueModel:
    """FIFO. e=enqueue(val), d=dequeue → val (None 이면 '비었음' 답)"""
    init = ()

    @staticmethod
    def step(state, op):
        if op.kind == "e":
            return True, state + (op.val,)
        if op.val is None:
            return state == (), state
        return bool(state) and state[0] == op.val, state[1:]


def E(v, a, b, c=0):
    return Op(c, "q", "e", v, a, b)


def D(v, a, b, c=9):
    return Op(c, "q", "d", v, a, b)


def lin(ops, model):
    return s.check_linearizable(ops, model).ok


def p1():
    check("P1 FIFO order accepted", lin([E(1, 0, 1), E(2, 2, 3), D(1, 4, 5), D(2, 6, 7)], QueueModel) is True)
    check("P1 FIFO order violated", lin([E(1, 0, 1), E(2, 2, 3), D(2, 4, 5)], QueueModel) is False)
    check("P1 empty after completed enqueue rejected", lin([E(1, 0, 1), D(None, 2, 3)], QueueModel) is False)
    # DS-A03 강제 스케줄: A(100) 은 슬롯만 잡고 멈춤, B(201) 은 완료, 소비자는 '비었음'
    ds_a03 = [E(100, 0, 20, c=1), E(201, 1, 2, c=2), D(None, 3, 4)]
    check("P1 DS-A03 forced schedule is not linearizable", lin(ds_a03, QueueModel) is False)
    check("P1 overlapping enqueues may dequeue either way",
          lin([E(100, 0, 20, c=1), E(201, 1, 2, c=2), D(201, 21, 22), D(100, 23, 24)], QueueModel) is True)


# ---------------------------------------------------------------- P2
class CasModel:
    """c: val=(기대값, 새값, 성공여부). 미완료 CAS 는 '적용됐다면 성공' 으로 본다."""
    init = 0

    @staticmethod
    def step(state, op):
        if op.kind == "w":
            return True, op.val
        if op.kind == "r":
            return op.val == state, state
        exp, new, succeeded = op.val
        if op.t_ret is None:
            return state == exp, new
        if succeeded:
            return state == exp, new
        return state != exp, state


def C(exp, new, ok, a, b, c=0):
    return Op(c, "x", "c", (exp, new, ok), a, b)


def RD(v, a, b, c=9):
    return Op(c, "x", "r", v, a, b)


def p2():
    check("P2 two concurrent CAS both succeeding rejected",
          lin([C(0, 1, True, 0, 5), C(0, 2, True, 1, 6, c=1)], CasModel) is False)
    check("P2 one wins one fails accepted",
          lin([C(0, 1, True, 0, 5), C(0, 2, False, 1, 6, c=1)], CasModel) is True)
    check("P2 failed CAS on matching value rejected", lin([C(0, 1, False, 0, 1)], CasModel) is False)
    check("P2 pending CAS may apply", lin([C(0, 1, True, 0, None), RD(1, 2, 3)], CasModel) is True)
    check("P2 pending CAS may not apply", lin([C(0, 1, True, 0, None), RD(0, 2, 3)], CasModel) is True)
    check("P2 pending CAS cannot apply when value differs",
          lin([Op(1, "x", "w", 5, 0, 1), C(0, 1, True, 2, None), RD(1, 3, 4)], CasModel) is False)


# ---------------------------------------------------------------- P3
def check_sc(ops, model=s.RegisterModel):
    """순차 일관성: 클라이언트별 순서만 지키면 된다 (실시간 순서는 안 본다).
    미완료 쓰기는 '혼자인 클라이언트' 로 넣고, 건너뛰어도 되게 한다 (Part A 와 같은 의미)."""
    per = {}
    for op in sorted((o for o in ops if o.t_ret is not None), key=lambda o: o.t_inv):
        per.setdefault(op.client, []).append(op)
    lists = [per[c] for c in sorted(per)]
    must = len(lists)                                    # 앞의 must 개만 끝까지 가야 한다
    lists += [[o] for o in ops if o.t_ret is None and o.kind == "w"]
    seen = set()

    def go(pos, state):
        if all(p == len(l) for p, l in zip(pos[:must], lists)):
            return True
        if (pos, state) in seen:
            return False
        seen.add((pos, state))
        for i, l in enumerate(lists):
            if pos[i] < len(l):
                ok, new_state = model.step(state, l[pos[i]])
                if ok and go(pos[:i] + (pos[i] + 1,) + pos[i + 1:], new_state):
                    return True
        return False
    return go(tuple(0 for _ in lists), model.init)


def p3():
    stale = [s.W(1, 0, 1, client=0), s.R(None, 2, 3, client=1)]
    check("P3 stale read across clients: SC yes", check_sc(stale) is True)
    check("P3 stale read across clients: linearizable no", s.verdict(stale) is False)
    same = [s.W(1, 0, 1, client=0), s.R(None, 2, 3, client=0)]
    check("P3 stale read on the same client: SC no", check_sc(same) is False)
    inv2 = [s.W(1, 0, 10, client=0), s.R(1, 1, 2, client=1), s.R(None, 3, 4, client=2)]
    check("P3 new-old inversion across clients: SC yes", check_sc(inv2) is True)
    inv1 = [s.W(1, 0, 10, client=0), s.R(1, 1, 2, client=1), s.R(None, 3, 4, client=1)]
    check("P3 new-old inversion on one client: SC no", check_sc(inv1) is False)
    cfg = s.cfg_named("R2W1 none")
    lin_bad, sc_bad = set(), set()
    for seed in range(300):
        hist = [op for op in s.QuorumStore(cfg, seed).run() if op.key == "k0"]
        if not s.verdict(hist):
            lin_bad.add(seed)
        if not check_sc(hist):
            sc_bad.add(seed)
    print("     R2W1 none, key k0, 300 seeds: 선형성 위반 %d, 순차 일관성 위반 %d" % (len(lin_bad), len(sc_bad)))
    check("P3 every SC violation is also a linearizability violation", sc_bad <= lin_bad, sc_bad - lin_bad)
    check("P3 linearizability is the stricter check here", len(lin_bad) > len(sc_bad))


# ---------------------------------------------------------------- P4
class FastPath(s.QuorumStore):
    """R 개 응답이 전부 같은 ts 이고 R >= W 면 그 값은 이미 쓰기 quorum 에 있다 → write-back 생략"""
    unsafe = False

    def on_read_quorum(self, call):
        ts, val = max(call.replies, key=lambda rv: rv[0])
        call.result = val
        same = all(rv[0] == ts for rv in call.replies)
        if same and (self.cfg.R >= self.cfg.W or self.unsafe):
            self.finish(call)
        else:
            self.put_all(call, ts, val, self.cfg.W, self.finish)


class Unsafe(FastPath):
    unsafe = True


def msgs_per_op(cfg, cls, seeds):
    msgs = ops = 0
    for seed in seeds:
        st = cls(cfg, seed)
        ops += len(st.run())
        msgs += st.msgs
    return msgs / ops


def p4():
    abd = s.cfg_named("R2W2 sync (ABD)")
    seeds = range(1000)
    check("P4 fast path keeps ABD linearizable", len(s.sweep(abd, seeds, FastPath).violations) == 0)
    full, fast = msgs_per_op(abd, s.QuorumStore, range(200)), msgs_per_op(abd, FastPath, range(200))
    print("     messages/op: always write-back %.2f, fast path %.2f" % (full, fast))
    check("P4 fast path sends fewer messages", fast < full)
    r1w3 = s.Cfg("R1W3 sync", R=1, W=3, writeback="sync")
    bad = len(s.sweep(r1w3, seeds, Unsafe).violations)
    good = len(s.sweep(r1w3, seeds, FastPath).violations)
    print("     R1W3 1000 seeds: no R>=W guard %d violations, with guard %d" % (bad, good))
    check("P4 skipping the R>=W condition violates", bad > 0)
    check("P4 with the condition R1W3 is clean", good == 0)


# ---------------------------------------------------------------- P5
def naive_shrink(ops):
    cur = list(ops)
    changed = True
    while changed:
        changed = False
        for i in range(len(cur) - 1, -1, -1):
            cand = cur[:i] + cur[i + 1:]
            if not s.check_linearizable(cand).ok:
                cur, changed = cand, True
    return s.normalize(cur)


def p5():
    ops = [s.R(1, 2, 3), s.R(None, 4, 5, client=2), s.W(1, 0, 1)]
    naive, good = naive_shrink(ops), s.shrink(ops)
    print("     naive: %s" % [s.label(o) for o in naive])
    print("     guard: %s" % [s.label(o) for o in good])
    check("P5 naive shrink ends in a read of a value nobody wrote",
          len(naive) == 1 and naive[0].kind == "r" and not s.well_formed(naive))
    check("P5 guarded shrink keeps the real stale read", len(good) == 2 and s.well_formed(good))


def main():
    p1()
    p2()
    p3()
    p4()
    p5()
    print("%d/%d passed" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
