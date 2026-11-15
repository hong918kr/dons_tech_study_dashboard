#!/usr/bin/env python3
"""SIV-A02 추가 연습 — 한 번에 검증 (자기완결: python3 -I practice.py, 데이터 파일 불필요)

연습 1 등중 코드(constant-weight)로 aliasing 없애기 · 연습 2 TCK 시간과 구동 충돌 지속시간
연습 3 BYPASS 로 체인 줄이기 · 연습 4 confounding 쌍 세기 · 연습 5 커버리지 요약
"""
import itertools
import sys

PASSED = [0]
TOTAL = [0]


def CHECK(name, cond, detail=""):
    TOTAL[0] += 1
    if cond:
        PASSED[0] += 1
        print("PASS %s" % name)
    else:
        print("FAIL %s%s" % (name, (": " + str(detail)) if detail != "" else ""))


def comb(n, r):
    out = 1
    for i in range(r):
        out = out * (n - i) // (i + 1)
    return out


def ceil_log2(n):
    k = 0
    while (1 << k) < n:
        k += 1
    return k


def gen_counting_modified(n):
    k = ceil_log2(n + 2)
    return list(range(1, n + 1)), k


def gen_walking_1(n):
    return [1 << i for i in range(n)], n


# 연습 1 — 등중 코드(constant-weight)
def gen_constant_weight(n):
    """같은 무게 w = k//2 의 코드 n 개, 가장 짧은 k (C(k, k//2) >= n). k >= 2 면 w 가 0 도 k 도 아니라 상수 코드가 없다"""
    k = 1
    while comb(k, k // 2) < n:
        k += 1
    w = k // 2
    codes = [c for c in range(1 << k) if bin(c).count("1") == w]
    return codes[:n], k


def alias_count(codes, kind):
    """단락 두 넷의 응답이 제3의 넷의 코드와 같아지는 쌍의 수 (진짜 aliasing). 부분집합 alias(응답이 자기 짝의 코드)는 제외"""
    cset = set(codes)
    count = 0
    for a, b in itertools.combinations(codes, 2):
        r = a & b if kind == "and" else a | b
        if r in cset and r != a and r != b:
            count += 1
    return count


def t1():
    codes, k = gen_constant_weight(14)
    CHECK("E1 constant-weight n=14 needs k=6", k == 6 and len(set(codes)) == 14, k)
    CHECK("E1 no three-net aliasing on AND", alias_count(codes, "and") == 0, alias_count(codes, "and"))
    CHECK("E1 no three-net aliasing on OR", alias_count(codes, "or") == 0, alias_count(codes, "or"))
    mc, _ = gen_counting_modified(14)
    CHECK("E1 modified counting does alias", alias_count(mc, "and") > 0 and alias_count(mc, "or") > 0,
          (alias_count(mc, "and"), alias_count(mc, "or")))
    CHECK("E1 cost: 6 vectors vs 4 (modified counting) vs 14 (walking)", k > gen_counting_modified(14)[1] and k < 14)


# 연습 2 — TCK 시간과 구동 충돌 지속시간
def tcks_per_vector(length):
    """Update-DR → Select-DR → Capture-DR → Shift-DR → (L 클록) → Exit1-DR → Update-DR = L + 4 TCK (TAP 전이는 TOOL-A01)"""
    return length + 4


def fight_duration_us(length, f_hz):
    """이웃한 Update-DR 사이 시간 = 한 벡터 동안 핀이 그 값으로 구동되는 시간 (단락이면 그 시간만큼 드라이버끼리 싸운다)"""
    return tcks_per_vector(length) / f_hz * 1e6


def max_chain_for_limit(limit_us, f_hz):
    """벡터당 구동 시간을 limit_us 이하로 두는 최대 체인 길이"""
    return int(limit_us / 1e6 * f_hz) - 4


def t2():
    CHECK("E2 ticks per vector", tcks_per_vector(132) == 136)
    d = fight_duration_us(132, 10e6)
    CHECK("E2 13.6 us per vector at 10 MHz (L=132)", abs(d - 13.6) <= 1e-6, d)
    d1 = fight_duration_us(132, 1e6)
    CHECK("E2 136 us per vector at 1 MHz", abs(d1 - 136.0) <= 1e-6, d1)
    CHECK("E2 chain budget for 50 us at 10 MHz", max_chain_for_limit(50.0, 10e6) == 496, max_chain_for_limit(50.0, 10e6))


# 연습 3 — BYPASS 로 체인 줄이기
def dr_length(chain, active):
    """chain = [(ref, BSR 길이)...], active = 시험 대상 ref 집합. 나머지는 BYPASS 1비트"""
    return sum(length if ref in active else 1 for ref, length in chain)


def t3():
    chain = [("U1", 52), ("U2", 63), ("U3", 17)]
    CHECK("E3 all active", dr_length(chain, {"U1", "U2", "U3"}) == 132)
    CHECK("E3 U1-U2 only: U3 in BYPASS", dr_length(chain, {"U1", "U2"}) == 116)
    CHECK("E3 U3 only", dr_length(chain, {"U3"}) == 1 + 1 + 17)


# 연습 4 — confounding 쌍 세기
def confounded_pairs(codes, kind, n):
    """서로소인 두 쌍 {a,b},{c,d} 의 응답이 같은 경우 수: 두 단락이 하나로 보인다"""
    def f(x, y):
        return x & y if kind == "and" else x | y
    count = 0
    for (a, b), (c, d) in itertools.combinations(list(itertools.combinations(range(n), 2)), 2):
        if len({a, b, c, d}) == 4 and f(codes[a], codes[b]) == f(codes[c], codes[d]):
            count += 1
    return count


def t4():
    wcodes, _ = gen_walking_1(6)
    total = comb(6, 2) * comb(4, 2) // 2
    CHECK("E4 walking-1 + AND: every disjoint pair-of-pairs confounds (all read 0)", confounded_pairs(wcodes, "and", 6) == total,
          confounded_pairs(wcodes, "and", 6))
    CHECK("E4 walking-1 + OR: none confound", confounded_pairs(wcodes, "or", 6) == 0)
    mc, _ = gen_counting_modified(6)
    CHECK("E4 modified counting n=6 confounds some under AND and OR", confounded_pairs(mc, "and", 6) > 0 and confounded_pairs(mc, "or", 6) > 0,
          (confounded_pairs(mc, "and", 6), confounded_pairs(mc, "or", 6)))


# 연습 5 — 커버리지 요약
def coverage(net_kinds):
    """net_kinds: 넷 → testable/observe_only/drive_only/unscannable. (시험 비율 %, 못 잰 넷 목록)"""
    test = [n for n, k in net_kinds.items() if k == "testable"]
    pct = 100.0 * len(test) / len(net_kinds)
    return pct, sorted(n for n, k in net_kinds.items() if k != "testable")


def t5():
    kinds = {"A": "testable", "B": "testable", "C": "testable", "CLK": "observe_only", "EN": "drive_only", "TP": "unscannable", "D": "testable", "E": "testable"}
    pct, missed = coverage(kinds)
    CHECK("E5 coverage percent", abs(pct - 62.5) <= 1e-9, pct)
    CHECK("E5 untested list for the report", missed == ["CLK", "EN", "TP"], missed)


def main():
    for t in (t1, t2, t3, t4, t5):
        t()
    print("%d/%d passed" % (PASSED[0], TOTAL[0]))
    return 0 if PASSED[0] == TOTAL[0] else 1


if __name__ == "__main__":
    sys.exit(main())
