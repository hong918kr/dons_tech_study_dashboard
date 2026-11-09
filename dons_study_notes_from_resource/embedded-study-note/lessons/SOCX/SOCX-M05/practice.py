#!/usr/bin/env python3
"""SOCX-M05 추가 연습 — 한 번에 검증

  연습 1  턴 모델(turn model): 16가지 금지 조합 중 비순환 개수, west-first / north-last / negative-first → CDG
          비순환 + 적응성 비교
  연습 2  odd-even 턴 모델 (Chiu): 열 홀짝에 따른 턴 금지 → 비순환 · 연결성 · 규칙 준수
  연습 3  XY 와 YX 를 섞으면 (VC 없이) 사이클 → 2×2 mesh 시뮬레이터 데드락. VC 를 나누면(O1TURN 식) 해소
  연습 4  dateline 실수 두 가지: X 에만 dateline / 건넌 뒤 VC0 으로 되돌아감 → 사이클이 남는다. k=5 는 정상
  연습 5  Dally–Seitz 증명 방식: XY 의 채널 번호 함수 — 모든 의존 간선에서 번호가 엄격히 증가

  python3 -I practice.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 도구)

check = S.check


def _dx_dy(cur, dst):
    return dst[0] - cur[0], dst[1] - cur[1]


def _h(dx):
    return "E" if dx > 0 else "W"


def _v(dy):
    return "N" if dy > 0 else "S"


# ---------------------------------------------------------------- 연습 1: 턴 모델 3종 (Glass & Ni)
def route_west_first(topo, src, cur, dst, in_ch):
    """서쪽(W)으로 갈 일이 있으면 W 를 먼저 전부. 그 뒤에는 E/N/S 중 거리를 줄이는 방향 아무거나 (적응)."""
    dx, dy = _dx_dy(cur, dst)
    if dx < 0:
        return [topo.ch(cur, "W")]
    out = []
    if dx > 0:
        out.append(topo.ch(cur, "E"))
    if dy:
        out.append(topo.ch(cur, _v(dy)))
    return out


def route_north_last(topo, src, cur, dst, in_ch):
    """북쪽(N)은 마지막에만. N 이 아닌 이동이 남아 있으면 그것들 중에서 적응, 다 끝나면 N."""
    dx, dy = _dx_dy(cur, dst)
    out = []
    if dx:
        out.append(topo.ch(cur, _h(dx)))
    if dy < 0:
        out.append(topo.ch(cur, "S"))
    return out or [topo.ch(cur, "N")]


def route_negative_first(topo, src, cur, dst, in_ch):
    """음의 방향(W, S)을 먼저 적응적으로 다 끝내고, 그다음 양의 방향(E, N)을 적응적으로."""
    dx, dy = _dx_dy(cur, dst)
    neg = ([topo.ch(cur, "W")] if dx < 0 else []) + ([topo.ch(cur, "S")] if dy < 0 else [])
    if neg:
        return neg
    return ([topo.ch(cur, "E")] if dx > 0 else []) + ([topo.ch(cur, "N")] if dy > 0 else [])


CCW = ("EN", "NW", "WS", "SE")      # 반시계 추상 사이클의 턴 ("AB" = A 로 오다가 B 로 꺾음)
CW = ("ES", "SW", "WN", "NE")       # 시계


def turn_graph(topo, forbidden):
    """라우팅과 무관한 '턴 그래프': naive CDG 에서 금지 턴 간선만 뺀 것 (비최소 경로까지 포함).
    이게 비순환이면 허용된 턴만 쓰는 어떤 라우팅도 데드락 자유다."""
    g = S.naive_cdg(topo)
    return {c: {c2 for c2 in g[c] if c.dir + c2.dir not in forbidden} for c in g}


def adaptive_states(topo, route):
    """(src, dst, 현재 채널) 도달 상태 중 후보가 2개 이상인 상태 수 = 라우팅의 적응성(대략)"""
    n = 0
    for s in topo.nodes:
        for d in topo.nodes:
            if s == d:
                continue
            first = route(topo, s, s, d, None)
            n += len(first) > 1
            stack, seen = list(first), set(first)
            while stack:
                c = stack.pop()
                if c.dst == d:
                    continue
                nxt = route(topo, s, c.dst, d, c)
                n += len(nxt) > 1
                for c2 in nxt:
                    if c2 not in seen:
                        seen.add(c2)
                        stack.append(c2)
    return n


# ---------------------------------------------------------------- 연습 2: odd-even (Chiu 2000)
def route_odd_even(topo, src, cur, dst, in_ch):
    """규칙 1: 짝수 열에서 EN·ES 턴 금지.  규칙 2: 홀수 열에서 NW·SW 턴 금지.
    최소 경로 의사코드 (Chiu 논문의 ROUTE 를 옮김 — 테스트가 규칙·연결성·비순환을 확인한다)."""
    dx, dy = _dx_dy(cur, dst)
    xc, xs, xd = cur[0], src[0], dst[0]
    out = []
    if dx == 0:
        return [topo.ch(cur, _v(dy))]
    if dx > 0:
        if dy == 0:
            return [topo.ch(cur, "E")]
        if xc % 2 == 1 or xc == xs:               # 홀수 열이거나 출발 열(아직 E 로 들어온 적 없음)이면 N/S 가능
            out.append(topo.ch(cur, _v(dy)))
        if xd % 2 == 1 or dx != 1:                # 목적지가 짝수 열이고 한 칸 앞이면 E 로 들어가면 안 된다
            out.append(topo.ch(cur, "E"))         #   (짝수 열에서는 EN/ES 로 꺾을 수 없으므로)
        return out
    out.append(topo.ch(cur, "W"))
    if xc % 2 == 0 and dy:                        # 서쪽으로 가는 중 N/S 는 짝수 열에서만 (NW/SW 를 홀수 열에서 막기)
        out.append(topo.ch(cur, _v(dy)))
    return out


def turn_violations(g):
    bad = []
    for c1 in g:
        for c2 in g[c1]:
            x = c1.dst[0]
            if c1.dir == "E" and c2.dir in "NS" and x % 2 == 0:
                bad.append((c1, c2))
            if c1.dir in "NS" and c2.dir == "W" and x % 2 == 1:
                bad.append((c1, c2))
    return bad


# ---------------------------------------------------------------- 연습 3: XY + YX 혼합
def _xy_yx(topo, src, cur, dst, vc_for_yx):
    dx, dy = _dx_dy(cur, dst)
    if (src[0] + src[1]) % 2 == 0:                # 출발 노드 홀짝으로 XY / YX 를 고른다 (결정적)
        d = _h(dx) if dx else _v(dy)
        return [topo.ch(cur, d, 0)]
    d = _v(dy) if dy else _h(dx)
    return [topo.ch(cur, d, vc_for_yx)]


def route_mixed_xy_yx(topo, src, cur, dst, in_ch):
    """VC 하나에서 XY 패킷과 YX 패킷이 섞인다 → 두 순서가 서로의 금지 턴을 쓴다."""
    return _xy_yx(topo, src, cur, dst, 0)


def route_o1turn_vc(topo, src, cur, dst, in_ch):
    """XY 는 VC0, YX 는 VC1 — 두 개의 비순환 가상 네트워크 (O1TURN 은 출발 시 랜덤 선택, 여기서는 홀짝)."""
    return _xy_yx(topo, src, cur, dst, 1)


# ---------------------------------------------------------------- 연습 4: dateline 실수
def route_dateline_x_only(topo, src, cur, dst, in_ch):
    """X 링에만 dateline, Y 는 늘 VC0 — 실수"""
    (c,) = S.route_torus_dateline(topo, src, cur, dst, in_ch)
    return [topo.ch(cur, c.dir, 0)] if c.dir in "NS" else [c]


def route_dateline_reset(topo, src, cur, dst, in_ch):
    """wrap 링크만 VC1, 건넌 다음 홉은 다시 VC0 — 실수 ('유지'를 빼먹음)"""
    (c,) = S.route_torus_dor(topo, src, cur, dst, in_ch)
    return [topo.ch(cur, c.dir, 1 if topo.is_wrap(cur, c.dir) else 0)]


# ---------------------------------------------------------------- 연습 5: 채널 번호 함수
def xy_rank(topo, c):
    """X 채널은 0..k-2, Y 채널은 k..2k-2. 같은 차원 안에서는 진행 방향으로 증가."""
    k = topo.k
    x, y = c.src
    return {"E": x, "W": k - 1 - x, "N": k + y, "S": k + (k - 1 - y)}[c.dir]


def main():
    k = 4
    mesh = S.Topo(k, wrap=False)

    print("-- 연습 1: turn models --")
    gxy = S.build_cdg(mesh, S.route_xy)
    for name, r in (("west-first", route_west_first), ("north-last", route_north_last),
                    ("negative-first", route_negative_first)):
        g = S.build_cdg(mesh, r)
        a = adaptive_states(mesh, r)
        check("%-14s: CDG acyclic, %d edges, %d adaptive states" % (name, S.edges(g), a),
              lambda g=g, a=a: S.find_cycle(g) is None and a > 0)
    res16 = [(a, b, S.find_cycle(turn_graph(mesh, (a, b)))) for a in CCW for b in CW]
    bad16 = ["%s+%s(len %d)" % (a, b, len(c)) for a, b, c in res16 if c]
    check("Glass-Ni: forbid 1 turn per abstract cycle -> %d/16 acyclic; cyclic: %s"
          % (16 - len(bad16), ", ".join(bad16)), lambda: len(bad16) == 4)
    a_xy, a_full = adaptive_states(mesh, S.route_xy), adaptive_states(mesh, S.route_minimal_adaptive)
    check("XY has %d adaptive states, fully adaptive %d (but cyclic)" % (a_xy, a_full),
          lambda: a_xy == 0 and a_full > adaptive_states(mesh, route_west_first)
          and S.find_cycle(gxy) is None)

    print("-- 연습 2: odd-even --")
    for kk in (4, 5, 6):
        m = S.Topo(kk, wrap=False)
        g = S.build_cdg(m, route_odd_even)     # 막다른 상태가 있으면 ValueError → FAIL
        bad = turn_violations(g)
        check("odd-even %dx%d: connected, acyclic, %d forbidden turns used, %d adaptive states"
              % (kk, kk, len(bad), adaptive_states(m, route_odd_even)),
              lambda g=g, bad=bad: S.find_cycle(g) is None and not bad)

    print("-- 연습 3: XY + YX mixed --")
    gm = S.build_cdg(mesh, route_mixed_xy_yx)
    cm = S.find_cycle(gm)
    check("mixed XY/YX on 1 VC: cycle %s" % S.fmt_cycle(cm), lambda: S.is_cycle(gm, cm))
    m2 = S.Topo(k, wrap=False, nvc=2)
    check("XY on VC0 + YX on VC1: acyclic", lambda: S.find_cycle(S.build_cdg(m2, route_o1turn_vc)) is None)
    sq = [((0, 0), (1, 1)), ((1, 0), (0, 1)), ((1, 1), (0, 0)), ((0, 1), (1, 0))]
    for nvc, r, want_dl in ((1, route_mixed_xy_yx, True), (2, route_o1turn_vc, False)):
        t = S.Topo(2, wrap=False, nvc=nvc)
        sim = S.WormholeSim(t, r, depth=2)
        for pid, (s, d) in enumerate(sq):
            sim.add(pid, s, d, 4)
        res = sim.run()
        print("      2x2 mesh, %d VC: %s" % (nvc, S.report(res, sim)))
        check("2x2 diagonal swap, %d VC: %s" % (nvc, "deadlock" if want_dl else "completes 16/16"),
              lambda res=res, want_dl=want_dl: (res["deadlock"] is not None) if want_dl
              else (res["done"] and res["delivered"] == 16))

    print("-- 연습 4: dateline mistakes --")
    t2 = S.Topo(k, wrap=True, nvc=2)
    for name, r in (("dateline on X only", route_dateline_x_only), ("VC1 only on the wrap hop", route_dateline_reset)):
        g = S.build_cdg(t2, r)
        c = S.find_cycle(g)
        check("%s: still cyclic: %s" % (name, S.fmt_cycle(c)), lambda g=g, c=c: S.is_cycle(g, c))
    for kk in (5, 8):
        t = S.Topo(kk, wrap=True, nvc=2)
        g0 = S.build_cdg(S.Topo(kk, wrap=True), S.route_torus_dor)
        g = S.build_cdg(t, S.route_torus_dateline)
        check("k=%d torus: DOR cyclic, dateline acyclic (%d channels, %d edges)"
              % (kk, len(t.channels()), S.edges(g)),
              lambda g=g, g0=g0: S.find_cycle(g0) is not None and S.find_cycle(g) is None)

    print("-- 연습 5: Dally-Seitz numbering for XY --")
    viol = [(a, b) for a in gxy for b in gxy[a] if not xy_rank(mesh, a) < xy_rank(mesh, b)]
    check("XY: rank strictly increases on all %d dependency edges (violations %d)" % (S.edges(gxy), len(viol)),
          lambda: not viol)
    gad = S.build_cdg(mesh, S.route_minimal_adaptive)
    viol = [(a, b) for a in gad for b in gad[a] if not xy_rank(mesh, a) < xy_rank(mesh, b)]
    check("same rank on fully adaptive CDG: %d edges go down (Y->X turns); CDG is cyclic so NO rank can work" % len(viol),
          lambda: viol and all(a.dir in "NS" and b.dir in "EW" for a, b in viol))

    print("%d/%d passed" % (S.g_pass, S.g_total))
    return S.g_pass == S.g_total


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
