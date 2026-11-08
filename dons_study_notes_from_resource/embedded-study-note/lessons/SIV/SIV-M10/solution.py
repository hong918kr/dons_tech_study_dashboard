#!/usr/bin/env python3
"""SIV-M10 실패 로그 Triage와 시그니처 클러스터링

야간 회귀(nightly regression) JSONL 로그 수천 줄 → 고유 원인별 클러스터 → 보드/칩/온도/FW 상관 → 분류.
  Part A  실패 레코드를 정규화해 시그니처(테스트명 + 레지스터 + 에러코드 + 비트 패턴)로 묶는다
  Part B  클러스터마다 "어떤 조건에서만 나는가"를 Fisher exact test + 다중 비교 보정으로 판정한다
          (BOARD / CHIP / TEMP_HIGH / FW / SPREAD / AMBIGUOUS / INSUFFICIENT)
  Part C  nightly_regress.jsonl 전체를 triage 해서 Markdown 리포트와 다음 액션을 만든다

  python3 -I solution.py                        # 테스트 실행 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report               # nightly_regress.jsonl 의 Markdown 리포트
  python3 -I solution.py --report x.jsonl --naive   # 에러코드 정규화 없이 (함정 재현용)

Python 3.9 표준 라이브러리만 쓴다 (json, re, math, hashlib, os, sys).
"""
import hashlib
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = "nightly_regress.jsonl"
DIMS = ("board", "chip", "temp_c", "fw")            # 런 헤더에서 테스트 레코드로 붙이는 속성
FAIL_STATUSES = ("FAIL", "ERROR")
RUN_STATUSES = ("PASS", "FAIL", "ERROR")           # 실제로 "실행된" 시도 (SKIP 은 분모에서 뺀다)
KEEP_PARAMS = ("lane", "ch", "port")                # 테스트 이름 파라미터 중 원인을 가르는 것만 남긴다
MAX_BITS = 4                                        # 이보다 많은 비트가 틀리면 데이터 의존 → "multi"
# FW 버전·드라이버마다 같은 에러를 다른 이름으로 찍는다. "E_" 접두사를 뗀 뒤 이 표로 정규화한다.
CODE_ALIAS = {
    "ETIMEDOUT": "TIMEOUT", "TIMEDOUT": "TIMEOUT", "TMO": "TIMEOUT", "TIME_OUT": "TIMEOUT",
    "MISCMP": "MISCOMPARE", "DATA_MISMATCH": "MISCOMPARE",
    "LINK_DEGRADE": "LINK_DEGRADED", "LINK_DOWNGRADE": "LINK_DEGRADED",
}


# ---------------------------------------------------------------- 주어진 코드: 로더
def load_jsonl(path):
    """JSONL → (runs, tests, stats). 테스트 레코드에 런 헤더의 board/chip/temp_c/fw/night 를 붙이고
    "_i"(전역 번호)를 단다. 깨진 줄은 줄 번호만 모으고 계속 읽는다 (SIV-B14 의 원칙)."""
    runs, pending, bad = {}, [], []
    with open(path, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                d = json.loads(line)
            except ValueError:
                bad.append(ln)
                continue
            if not isinstance(d, dict):
                bad.append(ln)
            elif d.get("type") == "run":
                runs[d["run"]] = d
            elif d.get("type") == "test":
                pending.append(d)
    tests, orphans = [], 0
    for d in pending:
        h = runs.get(d.get("run"))
        if h is None:
            orphans += 1
            continue
        rec = dict(d)
        for k in DIMS + ("night",):
            rec[k] = h.get(k)
        rec["_i"] = len(tests)
        tests.append(rec)
    return runs, tests, {"bad_lines": bad, "orphans": orphans}


def sig_id(sig):
    """시그니처 문자열 → 8자리 ID. 입력만으로 정해지므로 밤이 바뀌어도 같은 버그는 같은 ID (버그 트래커 연결용)."""
    return hashlib.sha1(sig.encode("utf-8")).hexdigest()[:8]


# ---------------------------------------------------------------- Part A: 정규화와 시그니처
def norm_code(code):
    """에러코드 정규화: None/"" → "NONE", 대문자화, '-' → '_', 앞의 "E_" 제거, CODE_ALIAS 적용.
    "E_TIMEOUT" / "ETIMEDOUT" / "timeout" → "TIMEOUT",  "E_LINK_DEGRADED" → "LINK_DEGRADED" """
    if not code:
        return "NONE"
    c = str(code).strip().upper().replace("-", "_")
    if c.startswith("E_"):
        c = c[2:]
    return CODE_ALIAS.get(c, c)


def norm_test(name):
    """파라미터 붙은 테스트 이름 정규화. KEEP_PARAMS 에 든 키만 원래 순서대로 남긴다.
    "ddr.stress.march[seed=0x1A2B]" → "ddr.stress.march"
    "pcie.eye[lane=3,seed=7]"       → "pcie.eye[lane=3]" """
    m = re.match(r"^([^\[]*)\[(.*)\]\s*$", name)
    if not m:
        return name.strip()
    keep = []
    for kv in m.group(2).split(","):
        k = kv.split("=", 1)[0].strip()
        if k in KEEP_PARAMS:
            keep.append(kv.strip())
    base = m.group(1).strip()
    return base + ("[%s]" % ",".join(keep) if keep else "")


def bit_pattern(exp, act):
    """기대값/실제값 → 틀린 비트 패턴.
    둘 중 하나라도 없거나 숫자가 아니면 "-", 같으면 "none", 틀린 비트가 MAX_BITS 이하면 "b5+b6" (오름차순),
    그보다 많으면 "multi" (데이터 패턴에 따라 개수가 달라지므로 개수는 시그니처에 넣지 않는다)."""
    try:
        e, a = int(str(exp), 0), int(str(act), 0)
    except (TypeError, ValueError):
        return "-"
    diff = e ^ a
    if diff == 0:
        return "none"
    bits = [i for i in range(diff.bit_length()) if diff >> i & 1]
    if len(bits) > MAX_BITS:
        return "multi"
    return "+".join("b%d" % b for b in bits)


def norm_msg(msg):
    """메시지 템플릿: 16진수(0x..) → <HEX>, 홀로 선 10진수 → <N>. 'DQ13', 'Gen4', 'x2' 처럼
    글자에 붙은 숫자는 신호 이름이라 남긴다 (\\b 경계)."""
    if not msg:
        return ""
    s = re.sub(r"\b0[xX][0-9a-fA-F]+\b", "<HEX>", msg)
    return re.sub(r"\b\d+\b", "<N>", s)


def signature(rec):
    """ "test|reg|code|bits" — 테스트명·코드는 정규화, reg 없으면 "-". 메시지는 넣지 않는다."""
    return "%s|%s|%s|%s" % (norm_test(rec.get("test", "")), rec.get("reg") or "-",
                            norm_code(rec.get("code")), bit_pattern(rec.get("exp"), rec.get("act")))


def cluster(tests, sig_fn=signature):
    """FAIL/ERROR 레코드를 sig_fn 으로 묶는다 → [{"sig", "id", "recs"}], 개수 내림차순 → sig 오름차순."""
    groups = {}
    for r in tests:
        if r.get("status") in FAIL_STATUSES:
            groups.setdefault(sig_fn(r), []).append(r)
    out = [{"sig": s, "id": sig_id(s), "recs": rs} for s, rs in groups.items()]
    out.sort(key=lambda c: (-len(c["recs"]), c["sig"]))
    return out


# ---------------------------------------------------------------- Part B: 상관과 판정
def ver_key(v):
    """ "1.10.0" → (1, 10, 0). 문자열 비교면 "1.10.0" < "1.9.2" 가 되어 버린다."""
    return tuple(int(x) for x in str(v).split("."))


def fisher_greater(a, n_in, k, n):
    """단측 Fisher exact test. 전체 n 번 시도 중 실패 k 번일 때, 크기 n_in 인 그룹에 실패가
    우연히 a 번 이상 몰릴 확률 P(X >= a), X ~ Hypergeometric(n, k, n_in)."""
    if a <= 0:
        return 1.0
    total = math.comb(n, n_in)
    hit = sum(math.comb(k, x) * math.comb(n - k, n_in - x) for x in range(a, min(k, n_in) + 1))
    return hit / total


def exposure(tests, test_name):
    """분모: 이 (정규화된) 테스트가 실제로 실행된 레코드들 (SKIP·유실 제외)."""
    return [r for r in tests if r.get("status") in RUN_STATUSES and norm_test(r.get("test", "")) == test_name]


def candidates(trials):
    """후보 조건(술어) 목록 [(class, text, fn)]. 모든 시도가 만족하는 자명한 조건은 뺀다."""
    out = []
    for d, cls in (("board", "BOARD"), ("chip", "CHIP")):
        for v in sorted({r[d] for r in trials}):
            out.append((cls, "%s == %s" % (d, v), lambda r, d=d, v=v: r[d] == v))
    temps = sorted({r["temp_c"] for r in trials})
    for t in temps[1:]:
        out.append(("TEMP_HIGH", "temp_c >= %d" % t, lambda r, t=t: r["temp_c"] >= t))
    fws = sorted({r["fw"] for r in trials}, key=ver_key)
    for v in fws:
        out.append(("FW", "fw == %s" % v, lambda r, v=v: r["fw"] == v))
    for v in fws[1:]:
        out.append(("FW", "fw >= %s" % v, lambda r, v=v: ver_key(r["fw"]) >= ver_key(v)))
    return out


def evaluate(trials, fail_is, fn):
    """술어 하나의 2x2 표 → a(그룹 안 실패), n_in, k, n, purity(a/k), rate_in/out, p."""
    n, k = len(trials), len(fail_is)
    ins = [r for r in trials if fn(r)]
    a = sum(1 for r in ins if r["_i"] in fail_is)
    n_in = len(ins)
    return {"a": a, "n_in": n_in, "k": k, "n": n, "purity": a / k if k else 0.0,
            "rate_in": a / n_in if n_in else 0.0,
            "rate_out": (k - a) / (n - n_in) if n > n_in else 0.0,
            "p": fisher_greater(a, n_in, k, n)}


def classify(trials, fail_is, alpha=0.01, purity_min=0.95, min_fails=3, cands_fn=candidates):
    """클러스터 하나 판정.
    - 실패 < min_fails → INSUFFICIENT
    - 후보 m 개 중 purity >= purity_min 이고 p < alpha/m (Bonferroni) 인 것만 "설명"으로 인정
    - 설명 없음 → SPREAD
    - class 마다 가장 좁은 설명(n_in 최소 → p 최소 → text) 하나씩 → class 가 하나면 그 class, 둘 이상이면 AMBIGUOUS
    반환: {"class", "pred", "p", "purity", "a", "n_in", "k", "n", "m", "alts": [(class, text, p)]}"""
    k, n = len(fail_is), len(trials)
    base = {"class": None, "pred": "", "p": 1.0, "purity": 0.0, "a": 0, "n_in": 0,
            "k": k, "n": n, "m": 0, "alts": []}
    if k < min_fails:
        base["class"] = "INSUFFICIENT"
        return base
    cands = cands_fn(trials)
    m = len(cands)
    base["m"] = m
    best = {}
    for cls, text, fn in cands:
        st = evaluate(trials, fail_is, fn)
        if st["purity"] >= purity_min and st["p"] < alpha / m:
            key = (st["n_in"], st["p"], text)
            if cls not in best or key < best[cls][0]:
                best[cls] = (key, text, st)
    if not best:
        base["class"] = "SPREAD"
        return base
    ranked = sorted(best.items(), key=lambda kv: (kv[1][2]["p"], kv[1][0]))
    cls, (_, text, st) = ranked[0]
    base.update(pred=text, p=st["p"], purity=st["purity"], a=st["a"], n_in=st["n_in"],
                alts=[(c, t, s["p"]) for c, (_, t, s) in ranked])
    base["class"] = cls if len(ranked) == 1 else "AMBIGUOUS"
    return base


# ---------------------------------------------------------------- Part C: 야간 회귀 triage
def corr_table(trials, fail_is, dim):
    """dim 값별 [(값, 실패수, 시도수)] — temp_c 는 숫자 순, fw 는 ver_key 순, 나머지는 문자열 순."""
    agg = {}
    for r in trials:
        f, t = agg.get(r[dim], (0, 0))
        agg[r[dim]] = (f + (1 if r["_i"] in fail_is else 0), t + 1)
    key = ver_key if dim == "fw" else None
    return [(v, agg[v][0], agg[v][1]) for v in sorted(agg, key=key)]


def triage(tests, sig_fn=signature, **kw):
    """cluster → 클러스터마다 분모(exposure) → classify. 클러스터 순서 그대로 요약 dict 목록을 돌려준다.
    요약: id sig test reg code bits count fail_is trials cls nights(정렬) boards(정렬) msg(가장 흔한 템플릿)"""
    out = []
    for c in cluster(tests, sig_fn):
        recs = c["recs"]
        test = norm_test(recs[0]["test"])
        trials = exposure(tests, test)
        fail_is = {r["_i"] for r in recs}
        test_, reg, code, bits = c["sig"].split("|")
        tmpl = {}
        for r in recs:
            t = norm_msg(r.get("msg"))
            tmpl[t] = tmpl.get(t, 0) + 1
        out.append({"id": c["id"], "sig": c["sig"], "test": test_, "reg": reg, "code": code,
                    "bits": bits, "count": len(recs), "fail_is": fail_is, "trials": trials,
                    "cls": classify(trials, fail_is, **kw),
                    "nights": sorted({r["night"] for r in recs}),
                    "boards": sorted({r["board"] for r in recs}),
                    "msg": sorted(tmpl.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]})
    return out


def next_action(s):
    """요약 하나 → 디버그 엔지니어가 내일 아침 할 일 한 줄."""
    c = s["cls"]
    cls, pred = c["class"], c["pred"]
    if cls == "BOARD":
        return "%s 에서만: 이 보드의 칩을 다른 보드로 옮겨(swap) 재현 확인, 보드 HW(레인·커넥터·전원) 점검" % pred
    if cls == "CHIP":
        return "%s 를 따라다님: 칩 불량 의심 — 다른 보드로 swap 재확인, ATE/FA 데이터 조회" % pred
    if cls == "TEMP_HIGH":
        return "%s 에서만: 그 온도에서 V×F shmoo (SIV-M07), 온도 경계 세분화" % pred
    if cls == "FW":
        fws = sorted({r["fw"] for r in s["trials"]}, key=ver_key)
        first_bad = pred.split()[-1]
        i = fws.index(first_bad)
        if pred.startswith("fw >=") and i > 0:
            return "FW 회귀: bisect %s(good)..%s(bad) (SIV-M11)" % (fws[i - 1], first_bad)
        return "%s 에서만: 해당 빌드 diff 리뷰" % pred
    if cls == "AMBIGUOUS":
        return "교란(confounded): %s — 조건을 교차시키는 실험 필요" % " / ".join(t for _, t, _ in c["alts"])
    if cls == "INSUFFICIENT":
        return "표본 부족 (%d건): 반복 실행으로 재현율부터 확보" % c["k"]
    return "퍼진 간헐 실패: 재현율 측정·동시성 stress (SIV-M09), flaky 목록에 등록"


# ---------------------------------------------------------------- 주어진 코드: Markdown 리포트
def render_md(summaries, tests, stats, runs, top=6):
    w = []
    nfail = sum(1 for r in tests if r.get("status") in FAIL_STATUSES)
    nights = sorted({h.get("night") for h in runs.values()})
    w.append("# Nightly regression triage %s .. %s" % (nights[0], nights[-1]))
    w.append("")
    w.append("runs=%d test_records=%d failures=%d clusters=%d bad_lines=%s orphans=%d"
             % (len(runs), len(tests), nfail, len(summaries),
                ",".join(map(str, stats["bad_lines"])) or "0", stats["orphans"]))
    w.append("")
    w.append("## Clusters")
    w.append("")
    w.append("| id | count | test | reg | code | bits | class | condition | p | rate in/out |")
    w.append("|---|---:|---|---|---|---|---|---|---:|---|")
    for s in summaries:
        c = s["cls"]
        rio = ("%d/%d vs %d/%d" % (c["a"], c["n_in"], c["k"] - c["a"], c["n"] - c["n_in"])
               if c["pred"] else "%d/%d" % (c["k"], c["n"]))
        w.append("| %s | %d | %s | %s | %s | %s | %s | %s | %s | %s |"
                 % (s["id"], s["count"], s["test"], s["reg"], s["code"], s["bits"], c["class"],
                    c["pred"] or "-", "%.1e" % c["p"] if c["pred"] else "-", rio))
    shown = summaries[:top]
    for dim in DIMS:
        w.append("")
        w.append("## Fail rate by %s (fails/runs)" % dim)
        w.append("")
        w.append("| %s | %s |" % (dim, " | ".join(s["id"] for s in shown)))
        w.append("|---|%s|" % "|".join("---:" for _ in shown))
        tabs = [dict((v, (f, t)) for v, f, t in corr_table(s["trials"], s["fail_is"], dim)) for s in shown]
        vals = sorted({v for tb in tabs for v in tb}, key=ver_key if dim == "fw" else None)
        for v in vals:
            cells = []
            for tb in tabs:
                f, t = tb.get(v, (0, 0))
                cells.append("**%d/%d**" % (f, t) if f else "%d/%d" % (f, t))
            w.append("| %s | %s |" % (v, " | ".join(cells)))
    w.append("")
    w.append("## Next actions")
    w.append("")
    for s in summaries:
        w.append("- `%s` %s — %s  (e.g. \"%s\")" % (s["id"], s["test"], next_action(s), s["msg"]))
    return "\n".join(w) + "\n"


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


def synth(rows, test="t.x"):
    """합성 시도 목록: rows = [(board, chip, temp_c, fw, failed)] → (trials, fail_is)"""
    trials, fail_is = [], set()
    for i, (b, c, t, v, failed) in enumerate(rows):
        trials.append({"_i": i, "test": test, "status": "FAIL" if failed else "PASS",
                       "board": b, "chip": c, "temp_c": t, "fw": v, "night": "n"})
        if failed:
            fail_is.add(i)
    return trials, fail_is


def grid(boards, temps, reps, chip_fn, fail_fn, fw="1.0.0"):
    rows = []
    for rep in range(reps):
        for t in temps:
            for b in boards:
                rows.append((b, chip_fn(b, rep), t, fw, fail_fn(b, t, rep)))
    return synth(rows)


def by_test(sums, name):
    return [s for s in sums if s["test"] == name]


def first(sums, name):
    got = by_test(sums, name)
    return got[0] if got else None


def run_tests():
    print("-- Part A: normalize + signature --")
    check("norm_code: E_TIMEOUT / ETIMEDOUT / timeout -> TIMEOUT",
          lambda: [norm_code(x) for x in ("E_TIMEOUT", "ETIMEDOUT", "timeout")] == ["TIMEOUT"] * 3)
    check("norm_code: E_LINK_DEGRADED -> LINK_DEGRADED, e-miscmp -> MISCOMPARE, None -> NONE",
          lambda: (norm_code("E_LINK_DEGRADED"), norm_code("e-miscmp"), norm_code(None))
          == ("LINK_DEGRADED", "MISCOMPARE", "NONE"))
    check("norm_test: drop seed param", lambda: norm_test("ddr.stress.march[seed=0x1A2B]") == "ddr.stress.march")
    check("norm_test: keep lane, drop seed", lambda: norm_test("pcie.eye[lane=3,seed=7]") == "pcie.eye[lane=3]")
    check("norm_test: plain name unchanged", lambda: norm_test("irq.dma.done") == "irq.dma.done")
    check("bit_pattern(0x44, 0x24) == 'b5+b6' (x4 -> x2 in LNKSTA[9:4])",
          lambda: bit_pattern("0x00000044", "0x00000024") == "b5+b6")
    check("bit_pattern: DQ13 for two data patterns is the same 'b13'",
          lambda: bit_pattern("0xAAAAAAAA", "0xAAAA8AAA") == bit_pattern("0x3C3C3C3C", "0x3C3C1C3C") == "b13")
    check("bit_pattern: 6 bits -> 'multi', equal -> 'none', missing -> '-'",
          lambda: (bit_pattern("0xFF", "0xC0"), bit_pattern("0x1", "0x1"), bit_pattern(None, "0x1"),
                   bit_pattern("zz", "0x1")) == ("multi", "none", "-", "-"))
    check("norm_msg masks hex/numbers, keeps DQ13 / Gen4 / x2",
          lambda: norm_msg("miscompare at 0x822DCC4C iter 154 after 3431 ms (DQ13)")
          == "miscompare at <HEX> iter <N> after <N> ms (DQ13)"
          and norm_msg("link up Gen4 x2, expected x4 (retrain 3)") == "link up Gen4 x2, expected x4 (retrain <N>)")
    r1 = {"test": "ddr.stress.march[seed=0x0001]", "reg": "DDR_RDATA", "code": "E_MISCOMPARE",
          "exp": "0xAAAAAAAA", "act": "0xAAAA8AAA", "addr": "0x80000010", "msg": "at 0x80000010 iter 3"}
    r2 = dict(r1, test="ddr.stress.march[seed=0xBEEF]", exp="0xFFFFFFFF", act="0xFFFFDFFF",
              addr="0x8123_0000", msg="at 0x81230000 iter 99")
    r3 = dict(r1, act="0xAAAAAA8A")                       # 다른 비트(b5) = 다른 DQ = 다른 버그
    check("signature: volatile fields differ -> same signature",
          lambda: signature(r1) == signature(r2) == "ddr.stress.march|DDR_RDATA|MISCOMPARE|b13")
    check("signature: different failing bit -> different signature", lambda: signature(r1) != signature(r3))
    check("signature: missing reg/code -> '-' and NONE",
          lambda: signature({"test": "boot.rom.id", "status": "FAIL"}) == "boot.rom.id|-|NONE|-")
    toy = [dict(r1, status="FAIL"), dict(r2, status="FAIL"), dict(r3, status="FAIL"),
           dict(r1, status="PASS"), dict(r1, status="ERROR", code="DATA_MISMATCH")]
    cl = cluster(toy)
    check("cluster: 3 FAIL/ERROR of one sig + 1 other, PASS ignored, sorted by count",
          lambda: [len(c["recs"]) for c in cl] == [3, 1] and cl[0]["id"] == sig_id(cl[0]["sig"]))

    runs, tests, stats = load_jsonl(os.path.join(HERE, DATA))
    print("-- Part B: correlation + classification --")
    check("ver_key: 1.10.0 > 1.9.2 (string compare gets this wrong)",
          lambda: ver_key("1.10.0") > ver_key("1.9.2") and "1.10.0" < "1.9.2")
    check("fisher_greater(3, 3, 3, 10) == 1/120", lambda: abs(fisher_greater(3, 3, 3, 10) - 1 / 120) < 1e-12)
    check("fisher_greater(1, 2, 2, 4) == 5/6, a=0 -> 1.0",
          lambda: abs(fisher_greater(1, 2, 2, 4) - 5 / 6) < 1e-12 and fisher_greater(0, 5, 3, 10) == 1.0)
    B4 = ["B1", "B2", "B3", "B4"]
    fixed = lambda b, rep: "C" + b[1:]                                      # noqa: E731
    tr, fi = grid(B4, [25, 85], 5, fixed, lambda b, t, rep: b == "B3" and rep % 5 != 1)
    c1 = classify(tr, fi)
    check("board fails, chip never moved -> AMBIGUOUS (BOARD + CHIP)",
          lambda: c1["class"] == "AMBIGUOUS" and {x[0] for x in c1["alts"]} == {"BOARD", "CHIP"})
    swap = lambda b, rep: "C9" if (b == "B3" and rep >= 3) else "C" + b[1:]  # noqa: E731
    tr, fi = grid(B4, [25, 85], 5, swap, lambda b, t, rep: b == "B3" and rep % 5 != 1)
    c2 = classify(tr, fi)
    check("socket swap on B3, fails stay with board -> BOARD 'board == B3'",
          lambda: c2["class"] == "BOARD" and c2["pred"] == "board == B3")
    move = lambda b, rep: ("C3" if b == "B1" else "C1") if (rep >= 3 and b in ("B1", "B3")) else "C" + b[1:]  # noqa: E731
    tr, fi = grid(B4, [25, 85], 5, move, lambda b, t, rep: move(b, rep) == "C3" and rep % 5 != 1)
    c3 = classify(tr, fi)
    check("chip C3 moved B3 -> B1, fails follow chip -> CHIP 'chip == C3'",
          lambda: c3["class"] == "CHIP" and c3["pred"] == "chip == C3")
    tr, fi = grid(B4, [-40, 25, 60, 85, 105], 3, fixed, lambda b, t, rep: t >= 85 and (rep + int(b[1])) % 3 != 0)
    c4 = classify(tr, fi)
    check("fails only at >= 85 on every board -> TEMP_HIGH 'temp_c >= 85' (tightest)",
          lambda: c4["class"] == "TEMP_HIGH" and c4["pred"] == "temp_c >= 85")
    rows = [("B%d" % (i % 4 + 1), "C%d" % (i % 4 + 1), 105 if i % 4 == 2 else 25, "1.0.0", i % 4 == 2 and i % 3 != 0)
            for i in range(48)]
    tr, fi = synth(rows)
    c5 = classify(tr, fi)
    check("B3 only ever ran at 105C -> AMBIGUOUS incl. BOARD and TEMP_HIGH",
          lambda: c5["class"] == "AMBIGUOUS" and {"BOARD", "TEMP_HIGH"} <= {x[0] for x in c5["alts"]})
    tr, fi = grid(B4, [25, 85], 5, swap, lambda b, t, rep: b == "B3" and t == 85 and rep < 1)
    check("1 failure -> INSUFFICIENT", lambda: classify(tr, fi)["class"] == "INSUFFICIENT")
    tr, fi = grid(B4, [25, 85], 5, swap, lambda b, t, rep: b == "B2" and t == 85 and rep < 4)
    c7 = classify(tr, fi)
    st7 = evaluate(tr, fi, lambda r: r["board"] == "B2")
    check("4/4 fails on B2 of 40 runs: raw p<alpha but not after Bonferroni -> SPREAD",
          lambda: c7["class"] == "SPREAD" and st7["p"] < 0.01 and st7["p"] >= 0.01 / c7["m"])

    print("-- Part C: nightly triage --")
    check("loader: 240 runs, 1918 test records, bad lines [778, 1501]",
          lambda: (len(runs), len(tests), stats["bad_lines"], stats["orphans"]) == (240, 1918, [778, 1501], 0))
    check("exposure excludes SKIP (EVB-04 has no USB PHY) -> usb.hs.enum 210 runs",
          lambda: len(exposure(tests, "usb.hs.enum")) == 210)
    sums = triage(tests) or []
    cls = {s["test"]: s["cls"] for s in sums}
    check("4 unique clusters (one per root cause)", lambda: len(sums) == 4)
    check("pcie.link.width -> BOARD 'board == EVB-07' (chip swap ruled out CHIP)",
          lambda: cls["pcie.link.width"]["class"] == "BOARD" and cls["pcie.link.width"]["pred"] == "board == EVB-07")
    check("ddr.stress.march -> TEMP_HIGH 'temp_c >= 85'",
          lambda: cls["ddr.stress.march"]["class"] == "TEMP_HIGH" and cls["ddr.stress.march"]["pred"] == "temp_c >= 85")
    check("irq.dma.done -> FW 'fw >= 1.5.0'",
          lambda: cls["irq.dma.done"]["class"] == "FW" and cls["irq.dma.done"]["pred"] == "fw >= 1.5.0")
    check("spi.flash.read -> one cluster, SPREAD",
          lambda: len(by_test(sums, "spi.flash.read")) == 1 and cls["spi.flash.read"]["class"] == "SPREAD")
    raw_sig = lambda r: "%s|%s|%s|%s" % (norm_test(r["test"]), r.get("reg") or "-", r.get("code"),  # noqa: E731
                                         bit_pattern(r.get("exp"), r.get("act")))
    naive = by_test(triage(tests, sig_fn=raw_sig), "spi.flash.read")
    check("trap: without code normalization spi splits in 2 and one looks like 'fw == 1.4.2'",
          lambda: len(naive) == 2 and any(s["cls"]["class"] == "FW" and s["cls"]["pred"] == "fw == 1.4.2"
                                          for s in naive))
    pc = first(sums, "pcie.link.width")
    check("corr_table(board) for pcie: EVB-07 has all fails, others 0/30",
          lambda: [(v, f) for v, f, t in corr_table(pc["trials"], pc["fail_is"], "board") if f]
          == [("EVB-07", pc["count"])] and all(t == 30 for _, _, t in corr_table(pc["trials"], pc["fail_is"], "board")))
    check("corr_table(fw) sorted by version", lambda: [v for v, _, _ in corr_table(
        pc["trials"], pc["fail_is"], "fw")] == ["1.4.2", "1.5.0", "1.5.1"])
    irq = first(sums, "irq.dma.done")
    check("next_action(FW) suggests bisect 1.4.2..1.5.0",
          lambda: "bisect 1.4.2(good)..1.5.0(bad)" in next_action(irq))
    md = render_md(sums, tests, stats, runs)
    check("report: cluster table row for pcie with BOARD",
          lambda: pc and re.search(r"^\| %s \| \d+ \| pcie\.link\.width \| LNKSTA \| LINK_DEGRADED \| b5\+b6 \| BOARD \|"
                            % pc["id"], md, re.M))
    check("report: ids are deterministic (sha1 of signature)",
          lambda: pc["id"] == sig_id("pcie.link.width|LNKSTA|LINK_DEGRADED|b5+b6"))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) > 1 and argv[1] == "--report":
        path = argv[2] if len(argv) > 2 and not argv[2].startswith("--") else os.path.join(HERE, DATA)
        runs, tests, stats = load_jsonl(path)
        sig_fn = signature
        if "--naive" in argv:
            def sig_fn(r):
                return "%s|%s|%s|%s" % (norm_test(r["test"]), r.get("reg") or "-", r.get("code"),
                                        bit_pattern(r.get("exp"), r.get("act")))
        sys.stdout.write(render_md(triage(tests, sig_fn=sig_fn), tests, stats, runs))
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
