#!/usr/bin/env python3
"""SIV-M11 추가 연습 — bisect 결과를 믿을 수 있게 만드는 도구들

  연습 1  runs_to_confirm(q, alpha)       : 간헐 실패율 q 인 범인을 '안 고쳐졌다'고 놓치지 않으려면 몇 번 돌려야 하나
  연습 2  find_window(...)                : 통과 구간이 [a, b] 인 창(window) — 양쪽 가장자리를 bisect 두 번으로
  연습 3  culprit_candidates(status)      : skip 이 경계에 붙으면 범인 후보는 여러 개 — 진짜 git 출력과 비교
  연습 4  prob_bisect(...)                : 확률적 bisection(사후분포 + 정보량 최대 질의) vs SPRT, 같은 모델에서 오답률

  python3 -I practice.py      # 테스트 ("N/M passed", 실패 시 exit 1)
"""
import json
import math
import os
import random
import re
import subprocess
import sys
import tempfile

SELF = os.path.abspath(__file__)


# ---- 본문(solution.py)의 find_boundary / sprt_vote / git 실험실을 축약해 넣었다 (레슨끼리 import 하지 않는다) ----
class NoBoundary(ValueError):
    pass


def find_boundary(lo, hi, is_fail, step=1, check_ends=True):
    span = hi - lo
    if step <= 0 or span == 0 or span % step:
        raise ValueError("bad range/step")
    d, n, evals = (1 if span > 0 else -1), abs(span) // step, 0
    if check_ends:
        evals += 2
        if is_fail(lo):
            raise ValueError("lo fails")
        if not is_fail(hi):
            raise NoBoundary("hi passes")
    g, b = 0, n
    while b - g > 1:
        m = (g + b) // 2
        evals += 1
        if is_fail(lo + d * m * step):
            b = m
        else:
            g = m
    return lo + d * g * step, lo + d * b * step, evals


def sprt_vote(run_once, p0, p1, alpha=0.01, beta=0.01, max_n=30):
    up, dn = math.log((1 - beta) / alpha), math.log(beta / (1 - alpha))
    wf, wp = math.log(p1 / p0), math.log((1 - p1) / (1 - p0))
    llr, p, f = 0.0, 0, 0
    for i in range(max_n):
        if run_once(i):
            p, llr = p + 1, llr + wp
        else:
            f, llr = f + 1, llr + wf
        if llr >= up:
            return False, p, f
        if llr <= dn:
            return True, p, f
    return llr < 0, p, f


def git_env():
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1", GIT_TERMINAL_PROMPT="0",
               GIT_AUTHOR_NAME="SIV Lab", GIT_AUTHOR_EMAIL="lab@example.invalid",
               GIT_COMMITTER_NAME="SIV Lab", GIT_COMMITTER_EMAIL="lab@example.invalid", LC_ALL="C")
    return env


def git(repo, *args, check=True, extra_env=None):
    env = git_env()
    env.update(extra_env or {})
    r = subprocess.run(["git", "-C", repo, "-c", "init.defaultBranch=main", "-c", "commit.gpgsign=false",
                        "-c", "advice.detachedHead=false"] + list(args), env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True, timeout=300)
    if check and r.returncode != 0:
        raise RuntimeError("git %s: %s" % (" ".join(args), r.stdout))
    return r


def runner_main(argv):
    """연습 3 러너: BROKEN 이면 125, BUG 면 1, 아니면 0. 상태를 로그에 남긴다."""
    with open("fw.py", encoding="utf-8") as f:
        src = f.read()
    build = int(re.search(r"^FW_BUILD = (\d+)", src, re.M).group(1))
    code = 125 if "BROKEN" in src else (1 if "BUG = True" in src else 0)
    with open(argv[0], "a", encoding="utf-8") as f:
        f.write(json.dumps({"build": build, "exit": code}) + "\n")
    return code


# ================================================================ 연습 1
def runs_to_confirm(q, alpha):
    """실패율 q 인 커밋이 N 번 연속 PASS 할 확률 (1-q)^N <= alpha 가 되는 최소 N"""
    if not (0.0 < q <= 1.0) or not (0.0 < alpha < 1.0):
        raise ValueError("need 0<q<=1, 0<alpha<1")
    if q == 1.0:
        return 1
    x = math.log(alpha) / math.log(1.0 - q)
    return max(1, math.ceil(x - 1e-9))      # 2.0000000000000004 같은 부동소수 꼬리 방지


# ================================================================ 연습 2
def find_window(lo, hi, known_pass, is_fail, step=1):
    """known_pass 에서 양쪽으로 bisect. 반환 (low_edge, high_edge, evals): 통과하는 가장 바깥 값.
    한쪽 끝까지 통과하면 그 끝값 그대로(창이 범위 밖으로 열려 있음)."""
    if is_fail(known_pass):
        raise ValueError("known_pass fails")
    evals = 1
    edges = []
    for end in (lo, hi):
        try:
            last_pass, _, e = find_boundary(known_pass, end, is_fail, step)
        except NoBoundary:
            last_pass, e = end, 2
        edges.append(last_pass)
        evals += e - 1          # find_boundary 가 known_pass 를 다시 평가한 1회는 빼고 센다
    return edges[0], edges[1], evals


# ================================================================ 연습 3
def culprit_candidates(status):
    """status: {커밋 인덱스: 'good'|'bad'|'skip'} (선형 이력). 범인 후보 목록 (마지막 good 다음 ~ 첫 bad).
    bad 뒤에 good 이 있으면 flaky 또는 비단조 → ValueError."""
    goods = [i for i, s in status.items() if s == "good"]
    bads = [i for i, s in status.items() if s == "bad"]
    if not goods or not bads:
        raise ValueError("need at least one good and one bad")
    last_good, first_bad = max(goods), min(bads)
    if last_good > first_bad:
        raise ValueError("inconsistent: c%02d good after c%02d bad (flaky or non-monotonic)" % (last_good, first_bad))
    return list(range(last_good + 1, first_bad + 1))


def real_git_skip_case():
    """c09, c10 빌드 실패 + c11 회귀 → git 이 '후보 여러 개'로 끝나는지, 우리 함수와 같은지"""
    with tempfile.TemporaryDirectory(prefix="siv_m11_p_") as tmp:
        repo = os.path.join(tmp, "orca-fw")
        os.makedirs(repo)
        git(repo, "init", "-q")
        for i in range(16):
            body = "FW_BUILD = %d\nBUG = %s\n%s" % (i, i >= 11, "BROKEN = (\n" if i in (9, 10) else "")
            with open(os.path.join(repo, "fw.py"), "w", encoding="utf-8") as f:
                f.write(body)
            git(repo, "add", "fw.py")
            d = "2026-11-01T09:%02d:00+0000" % i
            git(repo, "commit", "-q", "-m", "c%02d: change %d" % (i, i),
                extra_env={"GIT_AUTHOR_DATE": d, "GIT_COMMITTER_DATE": d})
        log = os.path.join(tmp, "runner.jsonl")
        git(repo, "bisect", "start", "HEAD", "HEAD~15")
        r = git(repo, "bisect", "run", sys.executable, "-I", SELF, "--runner", log, check=False)
        shas = re.findall(r"^([0-9a-f]{40})$", r.stdout.split("could be any of:")[-1], re.M) \
            if "could be any of:" in r.stdout else []
        git_set = sorted(int(git(repo, "log", "-1", "--format=%s", s).stdout[1:3]) for s in shas)
        git(repo, "bisect", "reset")
        with open(log, encoding="utf-8") as f:
            steps = [json.loads(x) for x in f]
    status = {0: "good", 15: "bad"}
    for s in steps:
        status[s["build"]] = {0: "good", 1: "bad", 125: "skip"}[s["exit"]]
    return git_set, culprit_candidates(status), r.stdout


# ================================================================ 연습 4
def h2(p):
    """이진 엔트로피 (bit)"""
    return 0.0 if p <= 0.0 or p >= 1.0 else -(p * math.log2(p) + (1 - p) * math.log2(1 - p))


def prob_bisect(n, sample, p0, p1, budget):
    """경계 b ∈ {1..n} (인덱스 >= b 가 bad) 의 사후분포를 들고, 기대 정보량이 가장 큰 x 를 질의한다.
    sample(x) -> True(PASS). FAIL 관측 시 b<=x 이면 우도 p1, 아니면 p0 (PASS 는 1-p1 / 1-p0).
    budget 번 질의 후 최대 사후 확률(MAP) 의 b 를 낸다."""
    post = [1.0 / n] * n                      # post[k] = P(b = k+1)
    for _ in range(budget):
        best_x, best_i, cdf = 1, -1.0, 0.0
        for x in range(1, n):                 # 인덱스 n 은 bad 로 알려져 있으니 질의하지 않는다
            cdf += post[x - 1]                # F(x) = P(b <= x) = P(x 가 bad)
            pf = p1 * cdf + p0 * (1.0 - cdf)
            info = h2(pf) - (cdf * h2(p1) + (1.0 - cdf) * h2(p0))
            if info > best_i:
                best_x, best_i = x, info
        ok = sample(best_x)
        for k in range(n):
            pf = p1 if best_x >= k + 1 else p0
            post[k] *= (1.0 - pf) if ok else pf
        s = sum(post)
        post = [v / s for v in post]
    return 1 + max(range(n), key=lambda k: post[k])


def compare_prob_vs_sprt(budget=40, trials=1000, n=64, pg=0.05, pb=0.65, seed=5):
    rng = random.Random(seed)
    wrong_pb = wrong_sp = runs_sp = 0
    for _ in range(trials):
        b = rng.randint(1, n)

        def sample(x):
            return rng.random() >= (pb if x >= b else pg)
        wrong_pb += prob_bisect(n, sample, 0.10, 0.50, budget) != b

        def is_fail(x):
            nonlocal runs_sp
            g, p, f = sprt_vote(lambda _i: sample(x), 0.10, 0.50)
            runs_sp += p + f
            return not g
        wrong_sp += find_boundary(0, n, is_fail, check_ends=False)[1] != b
    return wrong_pb / trials, wrong_sp / trials, runs_sp / trials


# ================================================================ 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok, why = bool(fn()), ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    g_pass += ok
    print("%s  %s%s" % ("PASS" if ok else "FAIL", name, "" if ok else why))


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def run_tests():
    print("-- 연습 1: runs_to_confirm --")
    for q, a, exp in [(0.3, 0.01, 13), (0.65, 0.01, 5), (0.05, 0.01, 90), (0.5, 0.25, 2), (0.9, 0.01, 2),
                      (1.0, 0.01, 1)]:
        check("runs_to_confirm(%g, %g) == %d" % (q, a, exp), lambda q=q, a=a, exp=exp: runs_to_confirm(q, a) == exp)
    check("q=0 -> ValueError (never fails = nothing to confirm)", lambda: raises(ValueError, lambda: runs_to_confirm(0, .01)))

    print("-- 연습 2: find_window --")
    eye = lambda x: not (37 <= x <= 81)
    check("window 0..127 from 64 -> (37, 81)", lambda: find_window(0, 127, 64, eye)[:2] == (37, 81))
    check("evals <= 2*7+1", lambda: find_window(0, 127, 64, eye)[2] <= 15)
    check("open low edge -> (0, 81)", lambda: find_window(0, 127, 64, lambda x: x > 81)[:2] == (0, 81))
    check("PLL 400..1600 step 25 from 1000 -> (650, 1350)",
          lambda: find_window(400, 1600, 1000, lambda f: not (640 <= f <= 1360), 25)[:2] == (650, 1350))
    check("known_pass fails -> ValueError", lambda: raises(ValueError, lambda: find_window(0, 127, 10, eye)))

    print("-- 연습 3: skip 이 붙은 경계 --")
    check("clean -> [11]", lambda: culprit_candidates({0: "good", 7: "good", 9: "good", 10: "good",
                                                      11: "bad", 15: "bad"}) == [11])
    check("skips 9,10 -> [9, 10, 11]",
          lambda: culprit_candidates({0: "good", 8: "good", 9: "skip", 10: "skip", 11: "bad"}) == [9, 10, 11])
    check("good after bad -> ValueError",
          lambda: raises(ValueError, lambda: culprit_candidates({0: "good", 5: "bad", 7: "good", 15: "bad"})))
    git_set, ours, out = real_git_skip_case()
    print("   git says: %s | ours: %s" % (git_set, ours))
    check("real git: 'could be any of' c09 c10 c11", lambda: git_set == [9, 10, 11])
    check("culprit_candidates matches git", lambda: ours == git_set)

    print("-- 연습 4: probabilistic bisection vs SPRT (intermittent 5%/65%, 1000 trials) --")
    wpb, wsp, rsp = compare_prob_vs_sprt(40)
    wpb20 = compare_prob_vs_sprt(20)[0]
    print("   prob-bisect budget=20: wrong=%.1f%% | budget=40: wrong=%.1f%% | sprt: wrong=%.1f%% runs=%.1f"
          % (100 * wpb20, 100 * wpb, 100 * wsp, rsp))
    check("prob-bisect (40 runs) wrong <= 2%", lambda: wpb <= 0.02)
    check("prob-bisect budget 20 clearly worse (>= 10%)", lambda: wpb20 >= 0.10)
    check("prob-bisect uses fewer runs than SPRT here", lambda: 40 < rsp)
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--runner":
        sys.exit(runner_main(sys.argv[2:]))
    sys.exit(0 if run_tests() else 1)
