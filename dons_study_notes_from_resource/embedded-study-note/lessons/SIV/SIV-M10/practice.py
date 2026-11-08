#!/usr/bin/env python3
"""SIV-M10 추가 연습 — 한 번에 검증

  연습 1  저온 쪽 임계 (TEMP_LOW: temp_c <= T)
  연습 2  상호작용: "B3 이면서 85°C 이상에서만" (BOARD × TEMP)
  연습 3  베이스라인 대비 NEW / KNOWN / GONE (시그니처 ID 로 버그 티켓 연결)
  연습 4  실패 0건일 때 실패율 상한 (rule of three) 과 필요한 런 수

  python3 -I practice.py      # "N/M passed", 실패 시 exit 1

solution.py 의 핵심(정규화·시그니처·Fisher·classify)을 축약해 넣었다 (레슨 폴더 하나로 독립 실행).
"""
import hashlib
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DIMS = ("board", "chip", "temp_c", "fw")
CODE_ALIAS = {"ETIMEDOUT": "TIMEOUT", "TIMEDOUT": "TIMEOUT", "TMO": "TIMEOUT", "MISCMP": "MISCOMPARE"}


# ---------------------------------------------------------------- solution.py 축약본
def load_jsonl(path):
    runs, pending = {}, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                d = json.loads(line)
            except ValueError:
                continue
            if d.get("type") == "run":
                runs[d["run"]] = d
            elif d.get("type") == "test":
                pending.append(d)
    tests = []
    for d in pending:
        h = runs.get(d.get("run"))
        if h:
            rec = dict(d, **{k: h.get(k) for k in DIMS + ("night",)})
            rec["_i"] = len(tests)
            tests.append(rec)
    return runs, tests


def norm_code(c):
    if not c:
        return "NONE"
    c = str(c).strip().upper().replace("-", "_")
    c = c[2:] if c.startswith("E_") else c
    return CODE_ALIAS.get(c, c)


def norm_test(name):
    return re.sub(r"\[.*\]$", "", name)            # 이 데이터에는 KEEP_PARAMS 가 없다


def bit_pattern(exp, act):
    try:
        diff = int(str(exp), 0) ^ int(str(act), 0)
    except ValueError:
        return "-"
    bits = [i for i in range(diff.bit_length()) if diff >> i & 1]
    return "none" if not bits else "multi" if len(bits) > 4 else "+".join("b%d" % b for b in bits)


def signature(r):
    return "%s|%s|%s|%s" % (norm_test(r["test"]), r.get("reg") or "-", norm_code(r.get("code")),
                            bit_pattern(r.get("exp"), r.get("act")))


def sig_id(sig):
    return hashlib.sha1(sig.encode("utf-8")).hexdigest()[:8]


def ver_key(v):
    return tuple(int(x) for x in str(v).split("."))


def fisher_greater(a, n_in, k, n):
    if a <= 0:
        return 1.0
    return sum(math.comb(k, x) * math.comb(n - k, n_in - x)
               for x in range(a, min(k, n_in) + 1)) / math.comb(n, n_in)


def candidates(trials):
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
    n, k = len(trials), len(fail_is)
    ins = [r for r in trials if fn(r)]
    a = sum(1 for r in ins if r["_i"] in fail_is)
    return {"a": a, "n_in": len(ins), "k": k, "n": n, "purity": a / k if k else 0.0,
            "p": fisher_greater(a, len(ins), k, n), "fn": fn}


def classify(trials, fail_is, alpha=0.01, purity_min=0.95, min_fails=3, cands_fn=candidates):
    k = len(fail_is)
    base = {"class": "INSUFFICIENT", "pred": "", "p": 1.0, "k": k, "n": len(trials), "alts": []}
    if k < min_fails:
        return base
    cands = cands_fn(trials)
    best = {}
    for cls, text, fn in cands:
        st = evaluate(trials, fail_is, fn)
        if st["purity"] >= purity_min and st["p"] < alpha / len(cands):
            key = (st["n_in"], st["p"], text)
            if cls not in best or key < best[cls][0]:
                best[cls] = (key, text, st)
    if not best:
        base["class"] = "SPREAD"
        return base
    ranked = sorted(best.items(), key=lambda kv: (kv[1][2]["p"], kv[1][0]))
    base.update(pred=ranked[0][1][1], p=ranked[0][1][2]["p"],
                alts=[(c, t, s) for c, (_, t, s) in ranked],
                **{"class": ranked[0][0] if len(ranked) == 1 else "AMBIGUOUS"})
    return base


def triage(tests, cands_fn=candidates):
    groups = {}
    for r in tests:
        if r["status"] in ("FAIL", "ERROR"):
            groups.setdefault(signature(r), []).append(r)
    out = []
    for sig, recs in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        test = norm_test(recs[0]["test"])
        trials = [r for r in tests if r["status"] in ("PASS", "FAIL", "ERROR") and norm_test(r["test"]) == test]
        out.append({"id": sig_id(sig), "test": test, "trials": trials, "fail_is": {r["_i"] for r in recs},
                    "cls": classify(trials, {r["_i"] for r in recs}, cands_fn=cands_fn)})
    return out


def synth(rows):
    trials, fail_is = [], set()
    for i, (b, c, t, v, failed) in enumerate(rows):
        trials.append({"_i": i, "test": "t.x", "status": "FAIL" if failed else "PASS",
                       "board": b, "chip": c, "temp_c": t, "fw": v})
        if failed:
            fail_is.add(i)
    return trials, fail_is


def grid(boards, temps, reps, fail_fn):
    return synth([(b, "C" + b[1:], t, "1.0.0", fail_fn(b, t, rep))
                  for rep in range(reps) for t in temps for b in boards])


# ---------------------------------------------------------------- 연습 1: TEMP_LOW
def candidates_cold(trials):
    """기존 후보 + 'temp_c <= T' (가장 높은 온도는 자명하므로 뺀다)."""
    out = candidates(trials)
    temps = sorted({r["temp_c"] for r in trials})
    for t in temps[:-1]:
        out.append(("TEMP_LOW", "temp_c <= %d" % t, lambda r, t=t: r["temp_c"] <= t))
    return out


# ---------------------------------------------------------------- 연습 2: 상호작용
def classify2(trials, fail_is, alpha=0.01, purity_min=0.95, **kw):
    """드릴다운: 1차 설명 A(BOARD 등)를 찾은 뒤, A 에 속한 시도만 놓고 '다른 차원' 후보 B 가
    실패를 다시 가르는지 본다 (A 안에서의 조건부 Fisher + A 안 후보 수로 Bonferroni).
    찾으면 INTERACTION "A and B". A 안에서 B 가 상수면(교란) 후보가 자명해 빠지므로 그대로 둔다."""
    c = classify(trials, fail_is, alpha=alpha, purity_min=purity_min, **kw)
    if not c["alts"]:
        return c
    cls_a, text_a, st_a = c["alts"][0]
    A = [r for r in trials if st_a["fn"](r)]
    fa = {r["_i"] for r in A} & fail_is
    cands = [x for x in candidates(A) if x[0] != cls_a]
    best = None
    for cls_b, text_b, fn in cands:
        st = evaluate(A, fa, fn)
        if st["purity"] >= purity_min and st["p"] < alpha / len(cands):
            key = (st["n_in"], st["p"], text_b)
            if best is None or key < best[0]:
                best = (key, text_b, cls_b)
    if best:
        c = dict(c, pred="%s and %s" % (text_a, best[1]),
                 **{"class": "INTERACTION", "dims": "%s×%s" % (cls_a, best[2])})
    return c


# ---------------------------------------------------------------- 연습 3: 베이스라인 비교
def diff_baseline(tonight_ids, baseline):
    """tonight_ids: 오늘 밤 클러스터 ID 목록, baseline: {id: 티켓}
    → {id: "NEW" | "KNOWN <티켓>" | "GONE <티켓>"}"""
    out = {}
    for i in tonight_ids:
        out[i] = "KNOWN %s" % baseline[i] if i in baseline else "NEW"
    for i, t in baseline.items():
        if i not in out:
            out[i] = "GONE %s" % t
    return out


# ---------------------------------------------------------------- 연습 4: 0건일 때 상한
def upper_zero(n, cl=0.95):
    """n 번 시도 0 실패 → 실패율 p 의 단측 상한: (1-p)^n = 1-cl → p = 1 - (1-cl)^(1/n)."""
    return 1.0 - (1.0 - cl) ** (1.0 / n)


def runs_needed(p, cl=0.95):
    """실패 0건으로 '실패율 < p' 를 cl 신뢰도로 말하려면 필요한 최소 런 수."""
    return math.ceil(math.log(1.0 - cl) / math.log(1.0 - p))


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
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


def main():
    runs, tests = load_jsonl(os.path.join(HERE, "nightly_regress.jsonl"))
    B8 = ["B%d" % i for i in range(1, 9)]
    T6 = [-40, 0, 25, 60, 85, 105]

    print("-- 연습 1: TEMP_LOW --")
    tr, fi = grid(B8, T6, 3, lambda b, t, rep: t <= 0 and (rep + int(b[1:])) % 2 == 0)
    c = classify(tr, fi, cands_fn=candidates_cold)
    check("cold-only fails -> TEMP_LOW 'temp_c <= 0'", lambda: (c["class"], c["pred"]) == ("TEMP_LOW", "temp_c <= 0"))
    check("same data, old candidates -> SPREAD (blind to cold side)",
          lambda: classify(tr, fi)["class"] == "SPREAD")
    s2 = {s["test"]: s["cls"]["class"] for s in triage(tests, cands_fn=candidates_cold)}
    check("nightly data unchanged with extra candidates",
          lambda: s2 == {"ddr.stress.march": "TEMP_HIGH", "irq.dma.done": "FW",
                         "spi.flash.read": "SPREAD", "pcie.link.width": "BOARD"})

    print("-- 연습 2: BOARD x TEMP interaction --")
    tr, fi = grid(B8, T6, 5, lambda b, t, rep: b == "B3" and t >= 85 and rep != 1)
    c = classify2(tr, fi)
    print("      classify2 -> %s %s (dims %s)" % (c["class"], c["pred"], c.get("dims")))
    check("fails only on B3 at >= 85 -> INTERACTION 'board == B3 and temp_c >= 85'",
          lambda: c["class"] == "INTERACTION" and c["pred"] == "board == B3 and temp_c >= 85")
    check("single-dimension classify alone says AMBIGUOUS", lambda: classify(tr, fi)["class"] == "AMBIGUOUS")
    rows = [("B%d" % (i % 4 + 1), "C%d" % (i % 4 + 1), 105 if i % 4 == 2 else 25, "1.0.0", i % 4 == 2 and i % 3 != 0)
            for i in range(48)]
    tr, fi = synth(rows)
    check("B3 only ever ran at 105C -> stays AMBIGUOUS (cannot separate)",
          lambda: classify2(tr, fi)["class"] == "AMBIGUOUS")
    s3 = {s["test"]: classify2(s["trials"], s["fail_is"])["class"] for s in triage(tests)}
    check("nightly clusters: drill-down finds no false interaction",
          lambda: s3 == {"ddr.stress.march": "TEMP_HIGH", "irq.dma.done": "FW",
                         "spi.flash.read": "SPREAD", "pcie.link.width": "BOARD"})

    print("-- 연습 3: baseline diff --")
    ids = [s["id"] for s in triage(tests)]
    base = {"3bff4eb5": "HW-1201", "c0a72c69": "SW-88", "0badc0de": "HW-999"}
    d = diff_baseline(ids, base)
    check("tonight ids = 4 deterministic ids", lambda: sorted(ids) == ["3bff4eb5", "3f741e14", "c0a72c69", "d9359f37"])
    check("KNOWN/NEW/GONE",
          lambda: d == {"3bff4eb5": "KNOWN HW-1201", "c0a72c69": "KNOWN SW-88", "d9359f37": "NEW",
                        "3f741e14": "NEW", "0badc0de": "GONE HW-999"})

    print("-- 연습 4: zero-failure upper bound --")
    check("upper_zero(210) ~= 0.0142 < rule of three 3/210 = 0.0143",
          lambda: abs(upper_zero(210) - 0.014164) < 1e-5 and upper_zero(210) < 3 / 210)
    check("runs_needed(1%, 95%) == 299, runs_needed(0.1%) == 2995",
          lambda: runs_needed(0.01) == 299 and runs_needed(0.001) == 2995)
    check("BER link: zero errors, 1e-12 @95% -> ~3.0e12 bits (-ln(0.05)/1e-12)",
          lambda: abs(-math.log(0.05) / 1e-12 - 2.9957e12) < 1e8)

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
