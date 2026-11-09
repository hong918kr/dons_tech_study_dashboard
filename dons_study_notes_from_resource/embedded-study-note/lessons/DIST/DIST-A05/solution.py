#!/usr/bin/env python3
"""DIST-A05 선형성(Linearizability) 검사기

동시 클라이언트의 invoke/return 기록(history)을 받아 "이 답들을 설명하는 한 줄 순서가 존재하는가"를 판정한다.
  Part A  Wing-Gong 백트래킹 + (선형화 집합, 상태) 메모 (Lowe) + 미완료 연산(pending) 처리
  Part B  키별 분할(locality) + 최소 반례 축소 + 타임라인 출력
  Part C  결함 주입 quorum 저장소 시뮬레이터에서 위반 히스토리 찾기, 같은 seed 로 고친 설정 검증

  python3 -I solution.py     # 테스트 ("N/M passed", 실패 시 exit 1)

Python 3.9 표준 라이브러리만 쓴다. 벽시계를 쓰지 않으므로 같은 seed 는 같은 히스토리를 만든다.
"""
import heapq
import itertools
import random
import sys
from collections import namedtuple

# t_ret 이 None 이면 미완료(pending): 호출은 했는데 답을 못 받았다 (timeout, 클라이언트 사망).
Op = namedtuple("Op", "client key kind val t_inv t_ret")
# ok: 판정, order: 성공 시 선형화 순서(ops 인덱스), best: 가장 멀리 간 부분 순서, nodes: 시도한 step 수
Result = namedtuple("Result", "ok order best nodes")


class RegisterModel:
    """읽기/쓰기 레지스터의 순차 명세. step(상태, 연산) → (이 답이 가능한가, 다음 상태)"""
    init = None

    @staticmethod
    def step(state, op):
        if op.kind == "w":
            return True, op.val
        return op.val == state, state


class KvModel:
    """키 전체를 한 상태로 보는 명세 (locality 교차검증용). 상태 = 정렬된 (키, 값) 튜플"""
    init = ()

    @staticmethod
    def step(state, op):
        d = dict(state)
        if op.kind == "w":
            d[op.key] = op.val
            return True, tuple(sorted(d.items()))
        return op.val == d.get(op.key), state


class SearchBudget(Exception):
    """max_nodes 를 넘었다 (데모가 멈추지 않게 하는 안전장치)"""


# ---------------------------------------------------------------- Part A: 검사기
def build_events(ops):
    """이벤트를 시각순으로 정렬한다. 같은 시각이면 invoke 가 먼저 → 두 연산은 겹친(concurrent) 것으로 본다.
    A 가 B 앞이라는 실시간 순서는 A.t_ret < B.t_inv (엄격한 부등호) 일 때만 성립한다."""
    ev = []
    for i, op in enumerate(ops):
        ev.append((op.t_inv, 0, i))
        if op.t_ret is not None:
            ev.append((op.t_ret, 1, i))
    ev.sort()
    return ev


def check_linearizable(ops, model=RegisterModel, memo=True, max_nodes=None):
    """Wing-Gong + Lowe 메모. 반환: Result(ok, order, best, nodes).

    완료된 연산은 전부 선형화해야 한다. 미완료 연산은 호출 이후 어느 시점에 적용됐거나 아예 적용 안 됐을 수 있다.
    """
    ev = build_events(ops)
    m = len(ev)
    HEAD, END = 0, m + 1
    nxt = list(range(1, m + 2)) + [END]      # 이벤트 이중 연결 리스트 (노드 1..m)
    prv = [HEAD] + list(range(0, m + 1))
    is_call = [False] * (m + 2)
    op_of = [-1] * (m + 2)
    ret_of = {}                              # op 인덱스 → 그 return 노드
    for node, (_, kind, i) in enumerate(ev, 1):
        is_call[node] = kind == 0
        op_of[node] = i
        if kind == 1:
            ret_of[i] = node

    def unlink(node):
        nxt[prv[node]] = nxt[node]
        prv[nxt[node]] = prv[node]

    def relink(node):
        nxt[prv[node]] = node
        prv[nxt[node]] = node

    remaining = len(ret_of)                  # 아직 선형화 안 한 완료 연산 수
    state, linearized = model.init, 0
    stack, cache, best, nodes = [], set(), [], 0
    entry = nxt[HEAD]
    while remaining > 0:
        if is_call[entry]:
            i = op_of[entry]
            ok, new_state = model.step(state, ops[i])
            nodes += 1
            if max_nodes is not None and nodes > max_nodes:
                raise SearchBudget()
            key = (linearized | (1 << i), new_state)
            if ok and (not memo or key not in cache):
                cache.add(key)
                stack.append((entry, state))
                linearized, state = key
                unlink(entry)
                if i in ret_of:
                    unlink(ret_of[i])
                    remaining -= 1
                if len(stack) > len(best):
                    best = [op_of[e] for e, _ in stack]
                entry = nxt[HEAD]            # 처음부터 다시 후보를 본다
            else:
                entry = nxt[entry]           # 이 후보는 안 된다 → 다음 호출
        else:                                # return 을 만남: 이 연산을 선형화 못 했으니 되돌아간다
            if not stack:
                return Result(False, None, best, nodes)
            entry, state = stack.pop()
            i = op_of[entry]
            linearized &= ~(1 << i)
            if i in ret_of:
                relink(ret_of[i])            # unlink 의 역순으로 복원
                remaining += 1
            relink(entry)
            entry = nxt[entry]
    return Result(True, [op_of[e] for e, _ in stack], best, nodes)


# ---------------------------------------------------------------- Part B: 분할, 축소, 출력
def check_kv(ops, model=RegisterModel):
    """키별로 나눠 검사한다. 반환: (전체 ok, 첫 위반 키 또는 None, {키: Result}). 키는 정렬 순서로 본다."""
    by_key = {}
    for op in ops:
        by_key.setdefault(op.key, []).append(op)
    results, bad = {}, None
    for key in sorted(by_key):
        results[key] = check_linearizable(by_key[key], model)
        if bad is None and not results[key].ok:
            bad = key
    return bad is None, bad, results


def well_formed(ops):
    """완료된 읽기가 돌려준 값마다 그 값을 쓴 연산이 히스토리에 있어야 한다 (초기값 None 은 예외)."""
    written = {op.val for op in ops if op.kind == "w"}
    return all(op.val is None or op.val in written
               for op in ops if op.kind == "r" and op.t_ret is not None)


def normalize(ops):
    """시각을 이벤트 순위 0,1,2.. 로 바꾼다 (겹침 관계는 그대로, 값이 작아져 읽기 쉽다)."""
    rank = {}
    for r, (_, kind, i) in enumerate(build_events(ops)):
        rank[(i, kind)] = r
    return [op._replace(t_inv=rank[(i, 0)],
                        t_ret=None if op.t_ret is None else rank[(i, 1)])
            for i, op in enumerate(ops)]


def shrink(ops, model=RegisterModel):
    """비선형 히스토리에서 연산을 하나씩 지워 1-최소(더 지우면 위반이 사라지거나 형식이 깨짐)로 줄인다.
    입력은 쓰기 값이 유일하다고 가정. 결과는 normalize 해서 돌려준다."""
    cur = list(ops)
    changed = True
    while changed:
        changed = False
        for i in range(len(cur) - 1, -1, -1):
            cand = cur[:i] + cur[i + 1:]
            if well_formed(cand) and not check_linearizable(cand, model).ok:
                cur = cand
                changed = True
    return normalize(cur)


def label(op):
    if op.kind == "w":
        return "w(%s)" % op.val
    return "r->%s" % op.val if op.t_ret is not None else "r->?"


def render(ops):
    """연산마다 한 줄의 ASCII 타임라인. 미완료는 >? 로 끝난다."""
    ops = normalize(ops)
    last = max([op.t_ret if op.t_ret is not None else op.t_inv for op in ops] + [0])
    lines = []
    for op in sorted(ops, key=lambda o: o.t_inv):
        head = "c%-2s %-8s " % (op.client, label(op))
        if op.t_ret is None:
            bar = " " * (3 * op.t_inv) + "|" + "-" * (3 * (last - op.t_inv)) + ">?"
        else:
            bar = " " * (3 * op.t_inv) + "|" + "-" * (3 * (op.t_ret - op.t_inv) - 1) + "|"
        lines.append(head + bar)
    return "\n".join(lines)


# ---------------------------------------------------------------- Part C: 결함 주입 quorum 저장소
class Cfg:
    """N/R/W + 프로토콜 선택 + 결함. writeback: none|async|sync, ts_mode: query|local, tie: op|client"""
    def __init__(self, name, N=3, R=2, W=2, writeback="none", ts_mode="query", tie="op",
                 clients=4, keys=2, ops_per_client=12, think=20, timeout=150,
                 delay=(1, 10), slow_p=0.15, slow_mult=12, drop_p=0.02, lag_p=0.3,
                 crash_at=300, crash_replica=2, amnesia_at=None, amnesia_replica=0, skew=0):
        self.__dict__.update(locals())
        del self.__dict__["self"]


ZERO = ((0, 0), None)            # 복제본 초기 상태: 타임스탬프 (0,0), 값 None


class Call:
    """코디네이터가 진행 중인 연산 하나"""
    def __init__(self, op_id, client, key, kind, val):
        self.id, self.client, self.key, self.kind, self.val = op_id, client, key, kind, val
        self.t_inv = 0
        self.phase = 0
        self.replies = []
        self.done = False
        self.result = None


class QuorumStore:
    """이벤트 큐 + 가상 시계로 도는 N 복제본 KV. rng 는 seed 하나에서만 나온다."""
    def __init__(self, cfg, seed):
        self.cfg, self.rng = cfg, random.Random(seed)
        self.now, self.q, self.seq = 0, [], 0
        self.replicas = [{} for _ in range(cfg.N)]
        self.down = set()
        self.history, self.next_val, self.next_id = [], 1, 1
        self.msgs = 0
        self.skew = [self.rng.randrange(-cfg.skew, cfg.skew + 1) for _ in range(cfg.clients)]

    # 이벤트 엔진 ------------------------------------------------------
    def at(self, t, fn, *args):
        self.seq += 1
        heapq.heappush(self.q, (t, self.seq, fn, args))

    def delay(self, lagged=False):
        lo, hi = self.cfg.delay
        d = self.rng.randrange(lo, hi + 1)
        return d * self.cfg.slow_mult if lagged or self.rng.random() < self.cfg.slow_p else d

    def rpc(self, r, handler, on_reply, lagged=False):
        """복제본 r 에 요청. 요청/응답 각각 지연되거나 유실될 수 있고, 죽은 복제본은 답하지 않는다."""
        self.msgs += 1
        if self.rng.random() >= self.cfg.drop_p:
            self.at(self.now + self.delay(lagged), self._deliver, r, handler, on_reply)

    def _deliver(self, r, handler, on_reply):
        if r in self.down:
            return
        result = handler(self.replicas[r])
        if self.rng.random() >= self.cfg.drop_p:
            self.at(self.now + self.delay(), on_reply, result)

    def _crash(self, r):
        self.down.add(r)

    def _amnesia(self, r):
        self.replicas[r] = {}        # 재부팅: ack 는 했는데 영속화 안 된 데이터가 사라진다

    # 복제본 쪽 동작 -----------------------------------------------------
    @staticmethod
    def h_get(key):
        return lambda store: store.get(key, ZERO)

    @staticmethod
    def h_put(key, ts, val):
        def apply(store):
            if ts > store.get(key, ZERO)[0]:
                store[key] = (ts, val)
            return True
        return apply

    # 코디네이터 ---------------------------------------------------------
    def broadcast(self, call, handler, need, then, lag_all_but=None):
        """N 복제본 전부에 보내고 먼저 온 need 개로 then(call) 호출. phase 로 늦은 응답을 버린다."""
        call.phase += 1
        call.replies = []
        phase = call.phase

        def on_reply(result):
            if call.done or call.phase != phase:
                return
            call.replies.append(result)
            if len(call.replies) == need:
                then(call)
        for r in range(self.cfg.N):
            self.rpc(r, handler, on_reply, lag_all_but is not None and r != lag_all_but)

    def put_all(self, call, ts, val, need, then):
        """쓰기를 전파한다. lag_p 확률로 복제본 하나만 빨리 받고 나머지는 늦는다 (결함 주입기의 표적)."""
        fast = self.rng.randrange(self.cfg.N) if self.rng.random() < self.cfg.lag_p else None
        self.broadcast(call, self.h_put(call.key, ts, val), need, then, fast)

    def start(self, client, left):
        cfg, rng = self.cfg, self.rng
        key = "k%d" % rng.randrange(cfg.keys)
        if rng.random() < 0.5:
            call = Call(self.next_id, client, key, "w", self.next_val)
            self.next_val += 1
        else:
            call = Call(self.next_id, client, key, "r", None)
        self.next_id += 1
        call.t_inv, call.left = self.now, left
        self.at(self.now + cfg.timeout, self.on_timeout, call)
        if call.kind == "r":
            self.broadcast(call, self.h_get(key), cfg.R, self.on_read_quorum)
        elif cfg.ts_mode == "query":
            self.broadcast(call, self.h_get(key), cfg.R, self.on_write_query_quorum)
        else:
            ts = (self.now + self.skew[client], call.id)
            self.put_all(call, ts, call.val, cfg.W, self.finish)

    def on_read_quorum(self, call):
        """읽기 첫 단계가 R 개 응답을 모았다. 최대 타임스탬프의 값을 고르고 writeback 설정대로 끝낸다."""
        ts, val = max(call.replies, key=lambda rv: rv[0])
        call.result = val
        wb = self.cfg.writeback
        if wb == "none":
            self.finish(call)
        elif wb == "async":
            self.put_all(call, ts, val, 1, lambda c: None)   # 보내기만 하고 ack 를 기다리지 않는다
            self.finish(call)
        else:
            self.put_all(call, ts, val, self.cfg.W, self.finish)

    def on_write_query_quorum(self, call):
        """쓰기 첫 단계(ts 조회)가 R 개 응답을 모았다. 그보다 큰 타임스탬프로 쓴다."""
        counter = max(rv[0] for rv in call.replies)[0] + 1
        ts = (counter, call.id if self.cfg.tie == "op" else call.client + 1)
        self.put_all(call, ts, call.val, self.cfg.W, self.finish)

    def finish(self, call):
        if call.done:
            return
        call.done = True
        val = call.result if call.kind == "r" else call.val
        self.history.append(Op(call.client, call.key, call.kind, val, call.t_inv, self.now))
        self.next_op(call)

    def on_timeout(self, call):
        if call.done:
            return
        call.done = True                 # 코디네이터는 포기하지만 이미 보낸 메시지는 계속 날아간다
        self.history.append(Op(call.client, call.key, call.kind, call.val, call.t_inv, None))
        self.next_op(call)

    def next_op(self, call):
        if call.left > 1:
            self.at(self.now + self.rng.randrange(self.cfg.think + 1),
                    self.start, call.client, call.left - 1)

    def run(self):
        cfg = self.cfg
        for c in range(cfg.clients):
            self.at(self.rng.randrange(cfg.think + 1), self.start, c, cfg.ops_per_client)
        if cfg.crash_at is not None:
            self.at(cfg.crash_at, self._crash, cfg.crash_replica)
        if cfg.amnesia_at is not None:
            self.at(cfg.amnesia_at, self._amnesia, cfg.amnesia_replica)
        while self.q:
            self.now, _, fn, args = heapq.heappop(self.q)
            fn(*args)
        return self.history


Sweep = namedtuple("Sweep", "violations seeds completed total first_seed first_key minimal")
SHRINK_LIMIT = 20


def sweep(cfg, seeds, store_cls=QuorumStore):
    """seed 마다 시뮬레이션 → check_kv. 위반 seed 목록, 완료/전체 연산 수, 첫 위반의 최소 반례를 돌려준다."""
    bad, completed, total, first_seed, first_key, minimal = [], 0, 0, None, None, None
    for seed in seeds:
        hist = store_cls(cfg, seed).run()
        total += len(hist)
        completed += sum(1 for op in hist if op.t_ret is not None)
        ok, key, _ = check_kv(hist)
        if not ok:
            bad.append(seed)
            if len(bad) <= SHRINK_LIMIT:                 # 처음 몇 개만 줄여서 가장 작은 것을 고른다
                m = shrink([op for op in hist if op.key == key])
                if minimal is None or len(m) < len(minimal):
                    first_seed, first_key, minimal = seed, key, m
    return Sweep(bad, len(list(seeds)), completed, total, first_seed, first_key, minimal)


# 설정 표: demo_sweep.py 와 practice.py 도 이것을 쓴다
CONFIGS = [
    Cfg("R1W1 none", R=1, W=1),
    Cfg("R2W1 none", R=2, W=1),
    Cfg("R1W3 none", R=1, W=3),
    Cfg("R2W2 none", R=2, W=2),
    Cfg("R3W1 none", R=3, W=1),
    Cfg("R2W2 async", R=2, W=2, writeback="async"),
    Cfg("R2W2 sync/local-ts", R=2, W=2, writeback="sync", ts_mode="local", skew=30),
    Cfg("R2W2 sync/amnesia", R=2, W=2, writeback="sync", amnesia_at=200, crash_at=None),
    Cfg("R2W2 sync (ABD)", R=2, W=2, writeback="sync"),
    Cfg("N5 R3W3 sync (ABD)", N=5, R=3, W=3, writeback="sync"),
]


def cfg_named(name):
    return next(c for c in CONFIGS if c.name == name)


# ---------------------------------------------------------------- 테스트 하네스
def W(val, a, b, client=0, key="x"):
    return Op(client, key, "w", val, a, b)


def R(val, a, b, client=1, key="x"):
    return Op(client, key, "r", val, a, b)


def brute_force(ops, model=RegisterModel):
    """정답지: 미완료 연산의 부분집합 × 모든 순열을 그대로 시도한다 (연산 7개 이하)."""
    done = [op for op in ops if op.t_ret is not None]
    pend = [op for op in ops if op.t_ret is None and op.kind == "w"]
    for r in range(len(pend) + 1):
        for extra in itertools.combinations(pend, r):
            for perm in itertools.permutations(done + list(extra)):
                state, good = model.init, True
                for pos, op in enumerate(perm):
                    # 뒤에 오는 연산이 이 연산보다 실시간으로 앞서면 안 된다
                    if any(later.t_ret is not None and later.t_ret < op.t_inv for later in perm[pos + 1:]):
                        good = False
                        break
                    ok, state = model.step(state, op)
                    if not ok:
                        good = False
                        break
                if good:
                    return True
    return False


def random_history(rng, n, keys=("x",), p_pending=0.2):
    ops, written = [], {k: [None] for k in keys}
    next_val = 1
    for i in range(n):
        a = rng.randrange(0, 8)
        b = a + 1 + rng.randrange(0, 5)
        key = rng.choice(keys)
        if rng.random() < 0.5:
            ops.append(Op(i, key, "w", next_val, a, None if rng.random() < p_pending else b))
            written[key].append(next_val)
            next_val += 1
    for i in range(n - len(ops)):
        a = rng.randrange(0, 8)
        b = a + 1 + rng.randrange(0, 5)
        key = rng.choice(keys)
        ops.append(Op(100 + i, key, "r", rng.choice(written[key]), a, b))
    rng.shuffle(ops)
    return ops


def replay_ok(ops, order, model=RegisterModel):
    state = model.init
    for k, i in enumerate(order):
        ok, state = model.step(state, ops[i])
        if not ok:
            return False
        if any(ops[j].t_ret is not None and ops[j].t_ret < ops[i].t_inv for j in order[k + 1:]):
            return False
    return True


def worst_case(n):
    """n 개의 쓰기가 전부 겹치고 마지막 읽기는 아무도 안 쓴 값을 돌려준다 → 모든 순서를 시험해야 실패를 안다"""
    ops = [W(i + 1, i, n + i, client=i) for i in range(n)]
    return ops + [R(999, 2 * n + 1, 2 * n + 2, client=n)]


passed, total_checks = 0, 0


def check(name, fn):
    global passed, total_checks
    total_checks += 1
    try:
        res = fn()
    except NotImplementedError:
        print("FAIL %s: TODO" % name)
        return
    except Exception as e:                        # 하네스는 끝까지 돈다
        print("FAIL %s: %s: %s" % (name, type(e).__name__, e))
        return
    if res is True:
        passed += 1
        print("PASS %s" % name)
    else:
        print("FAIL %s: %r" % (name, res))


def verdict(ops):
    return check_linearizable(ops).ok


def test_part_a():
    stale = [W(1, 0, 1), R(None, 2, 3)]
    inversion = [W(1, 0, 10), R(1, 1, 2), R(None, 3, 4, client=2)]
    check("A empty history", lambda: verdict([]) is True)
    check("A sequential write then read", lambda: verdict([W(1, 0, 1), R(1, 2, 3)]) is True)
    check("A stale read after completed write rejected", lambda: verdict(stale) is False)
    check("A read during write may see old then new",
          lambda: verdict([W(1, 0, 10), R(None, 1, 2), R(1, 3, 4, client=2)]) is True)
    check("A new-old inversion rejected", lambda: verdict(inversion) is False)
    check("A equal timestamps count as concurrent", lambda: verdict([W(1, 0, 5), R(None, 5, 6)]) is True)
    check("A one tick later is real-time ordered", lambda: verdict([W(1, 0, 5), R(None, 6, 7)]) is False)
    check("A pending write may have taken effect", lambda: verdict([W(1, 0, None), R(1, 5, 6)]) is True)
    check("A pending write may never take effect", lambda: verdict([W(1, 0, None), R(None, 5, 6)]) is True)
    check("A pending write cannot un-happen",
          lambda: verdict([W(1, 0, None), R(1, 1, 2), R(None, 3, 4, client=2)]) is False)
    check("A two writes then impossible read order",
          lambda: verdict([W(1, 0, 10), W(2, 0, 10, client=2), R(1, 11, 12), R(2, 13, 14)]) is False)

    def witness():
        ops = [W(1, 0, 10), W(2, 1, 9, client=2), R(1, 11, 12), R(1, 13, 14)]
        res = check_linearizable(ops)
        return res.ok and len(res.order) == 4 and replay_ok(ops, res.order)
    check("A witness order replays and respects real time", witness)

    def cross_check():
        rng = random.Random(5)
        yes = no = 0
        for _ in range(500):
            ops = random_history(rng, rng.randrange(2, 7))
            got, truth = verdict(ops), brute_force(ops)
            if got != truth:
                return "mismatch on %r: wgl=%s brute=%s" % (ops, got, truth)
            yes, no = yes + truth, no + (not truth)
        return True if yes >= 30 and no >= 30 else "unbalanced yes=%d no=%d" % (yes, no)
    check("A agrees with brute force on 500 random histories", cross_check)

    def memo_helps():
        ops = worst_case(8)
        with_memo = check_linearizable(ops)
        without = check_linearizable(ops, memo=False)
        ok = with_memo.ok is False and without.ok is False and with_memo.nodes * 20 < without.nodes
        return ok or "memo=%d nomemo=%d" % (with_memo.nodes, without.nodes)
    check("A memo cuts search at least 20x on worst case", memo_helps)


def test_part_b():
    two_keys = [W(1, 0, 1, key="a"), R(1, 2, 3, key="a"),
                W(2, 0, 1, key="b"), R(None, 2, 3, key="b")]

    def partition():
        ok, bad, res = check_kv(two_keys)
        return ok is False and bad == "b" and res["a"].ok is True
    check("B partition names the violating key", partition)

    def locality():
        rng = random.Random(9)
        for _ in range(300):
            ops = random_history(rng, rng.randrange(2, 9), keys=("a", "b"))
            ok, _, _ = check_kv(ops)
            whole = check_linearizable(ops, KvModel).ok
            if ok != whole:
                return "per-key=%s whole=%s on %r" % (ok, whole, ops)
        return True
    check("B per-key verdict equals whole-store verdict (locality)", locality)

    noisy_stale = [W(1, 0, 1), R(1, 2, 3), W(2, 4, 5, client=2), R(2, 6, 7), R(None, 8, 9), R(2, 10, 11)]
    check("B shrink stale read to 2 ops", lambda: len(shrink(noisy_stale)) == 2)

    inversion = [W(7, 0, 20), R(7, 1, 2), R(None, 3, 4, client=2), R(7, 5, 6), W(8, 21, 22)]

    def shrink_inv():
        s = shrink(inversion)
        return len(s) == 3 and sorted(op.kind for op in s) == ["r", "r", "w"] and verdict(s) is False
    check("B shrink new-old inversion to 3 ops", shrink_inv)

    def guard():
        # r->1 이 있는데 W(1) 을 지우면 "쓴 적 없는 값" 이라는 가짜 위반이 되어 더 작아질 수 있다
        ops = [R(1, 2, 3), R(None, 4, 5, client=2), W(1, 0, 1)]   # 뒤에서부터 지우므로 W(1) 을 먼저 시도한다
        s = shrink(ops)
        return well_formed(s) and len(s) == 2 and any(op.kind == "w" for op in s)
    check("B shrink keeps the write a remaining read needs", guard)

    def one_minimal():
        rng = random.Random(21)
        seen = 0
        for _ in range(400):
            ops = random_history(rng, rng.randrange(3, 9), p_pending=0.1)
            if verdict(ops) or not well_formed(ops):
                continue
            s = shrink(ops)
            seen += 1
            if verdict(s) or not well_formed(s):
                return "not a violation: %r" % s
            for i in range(len(s)):
                cand = s[:i] + s[i + 1:]
                if well_formed(cand) and not verdict(cand):
                    return "not 1-minimal: %r" % s
        return True if seen >= 50 else "only %d samples" % seen
    check("B shrink result is a 1-minimal violation", one_minimal)

    def sound():
        # 유일 값이면: H 가 선형화 가능 → 읽기 또는 아무도 안 읽은 쓰기를 지워도 선형화 가능
        rng = random.Random(33)
        tried = 0
        for _ in range(400):
            ops = random_history(rng, rng.randrange(3, 7))
            if not verdict(ops):
                continue
            for i in range(len(ops)):
                cand = ops[:i] + ops[i + 1:]
                if well_formed(cand):
                    tried += 1
                    if not verdict(cand):
                        return "removing %r broke %r" % (ops[i], ops)
        return True if tried >= 100 else "only %d" % tried
    check("B allowed removals never create a violation", sound)

    want = "c0  w(1)     |--------------|\nc1  r->1        |--|\nc2  r->None           |--|"
    check("B render timeline", lambda: render([W(1, 0, 10), R(1, 1, 2), R(None, 3, 4, client=2)]) == want)


def test_part_c():
    seeds = range(1000)

    def replay():
        cfg = cfg_named("R2W2 none")
        return QuorumStore(cfg, 7).run() == QuorumStore(cfg, 7).run()
    check("C same seed replays the identical history", replay)

    def finds(name):
        return lambda: len(sweep(cfg_named(name), seeds).violations) > 0
    check("C R1W1 no write-back violates", finds("R1W1 none"))
    check("C R2W1 no write-back violates", finds("R2W1 none"))
    check("C R1W3 no write-back violates", finds("R1W3 none"))
    check("C R2W2 quorum alone violates", finds("R2W2 none"))
    check("C R2W2 async write-back violates", finds("R2W2 async"))
    check("C R2W2 skewed local timestamps violate", finds("R2W2 sync/local-ts"))
    check("C R2W2 amnesia after ack violates", finds("R2W2 sync/amnesia"))

    def passes(name):
        def run():
            s = sweep(cfg_named(name), seeds)
            return (len(s.violations) == 0 and s.completed > 0) or s.violations[:5]
        return run
    check("C R3W1 passes the same seeds", passes("R3W1 none"))
    check("C ABD N=3 passes the same seeds", passes("R2W2 sync (ABD)"))
    check("C ABD N=5 passes the same seeds", passes("N5 R3W3 sync (ABD)"))

    def stale_shape():
        m = sweep(cfg_named("R1W1 none"), seeds).minimal
        kinds = sorted(op.kind for op in m)
        return (kinds == ["r", "w"] and well_formed(m) and verdict(m) is False) or m
    check("C R1W1 minimal counterexample is a 2-op stale read", stale_shape)

    def inversion_shape():
        m = sweep(cfg_named("R2W2 none"), seeds).minimal
        reads = sorted((op for op in m if op.kind == "r"), key=lambda o: o.t_inv)
        return (len(m) == 3 and len(reads) == 2 and reads[0].t_ret < reads[1].t_inv
                and reads[0].val is not None and well_formed(m) and verdict(m) is False) or m
    check("C R2W2 minimal counterexample is 1 write + 2 ordered reads", inversion_shape)


def main():
    test_part_a()
    test_part_b()
    test_part_c()
    print("%d/%d passed" % (passed, total_checks))
    return 0 if passed == total_checks else 1


if __name__ == "__main__":
    sys.exit(main())
