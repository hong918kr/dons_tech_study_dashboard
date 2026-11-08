#!/usr/bin/env python3
"""SIV-M11 Bisect 자동화: 펌웨어 커밋과 파라미터

가상의 SoC "ORCA-A0" DMA 펌웨어에서 생긴 회귀(regression)를 이진 탐색으로 좁힌다.
  Part A  일회용 git 저장소(tempfile.TemporaryDirectory)를 만들고 진짜 `git bisect run` 을 돌린다.
          러너(runner)는 이 파일 자신(--runner 모드)이고, 빌드 실패 → 125(skip), 인프라 오류 → 255(abort),
          테스트는 n회 다수결(majority vote)로 판정한다. flaky 테스트에서 1회 판정이 엉뚱한 커밋을 지목하는 것을 재현.
  Part B  일반화된 파라미터 bisect(전압·주파수·버퍼 크기 — 단조 술어) + flaky 모델에서 정책별 오답률 측정
          (naive / 다수결 / retry-until-pass / SPRT).
  Part C  야간 회귀 시나리오: 40커밋, 간헐 회귀 + 빌드 깨진 커밋 + flaky 잡음 → SPRT 러너로 bisect,
          부모/범인 커밋 재실행으로 확정(이항 검정), 범인 커밋에서 버퍼 크기 경계 bisect, 리포트.

  python3 -I solution.py            # 테스트 실행 ("N/M passed", 실패 시 exit 1)
  (내부용) python3 -I solution.py --runner --policy sprt ...   # git bisect run 이 커밋마다 부른다

Python 3 표준 라이브러리만 쓴다. git 은 GIT_CONFIG_GLOBAL=/dev/null 로 격리해서 Don 의 설정·프로젝트 저장소를 건드리지 않는다.
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

# git bisect run 종료 코드 규약 (git help bisect, "Bisect run")
EXIT_GOOD, EXIT_BAD, EXIT_SKIP, EXIT_ABORT = 0, 1, 125, 255

HW_MAX_SEG = 4096          # 가상 ORCA-A0 DMA: 디스크립터 길이는 1..4096 B 만 합법 (0 은 불법)
FIXED_SIZES = (1, 512, 4095, 4096, 4097, 8192, 10000)
SPRT_P0, SPRT_P1 = 0.10, 0.50   # "좋은 커밋의 flaky 실패율 상한" / "회귀 커밋 실패율 하한"


# ================================================================ Part A: 판정 정책과 종료 코드
def majority_vote(run_once, n):
    """run_once(attempt) -> True(PASS) / False(FAIL) 를 최대 n 번(홀수) 부른다.
    과반이 확정되면 바로 멈춘다(조기 종료). 반환 (is_good, passes, fails)."""
    # TODO: n 이 양의 홀수가 아니면 ValueError. need = n//2 + 1.
    #       i = 0..n-1 동안 run_once(i) 결과로 passes/fails 를 세고, 어느 쪽이든 need 에 닿으면 바로 멈춘다.
    #       반환 (passes >= need, passes, fails)
    return True, 0, 0


def exit_code(status, is_good=None):
    """러너 상태 → git bisect run 종료 코드.
    'ok' + is_good → 0 / 1,  'build_fail' → 125(skip),  'infra'(보드·장비 문제) → 255(bisect 중단)"""
    # TODO: build_fail → 125(EXIT_SKIP), infra → 255(EXIT_ABORT), ok → 0/1, 그 밖 → ValueError
    return EXIT_GOOD


def retry_until_pass(run_once, n):
    """(주어진 코드) n 번 중 한 번이라도 PASS 면 good. CI 의 흔한 'retry' — 한쪽 방향 잡음에만 맞다."""
    for i in range(n):
        if run_once(i):
            return True, 1, i
    return False, 0, n


# ================================================================ Part B: 파라미터 bisect + 신뢰도 기반 판정
class NoBoundary(ValueError):
    """hi 끝점도 PASS — 범위 안에 실패 경계가 없다."""


def find_boundary(lo, hi, is_fail, step=1, check_ends=True):
    """lo(PASS) 에서 hi(FAIL) 쪽으로 step 격자 위의 첫 실패 지점을 이진 탐색한다.
    lo > hi 도 된다(예: Vmin — 전압을 내리면 실패). 술어는 단조(monotonic)라고 가정.
    반환 (last_pass, first_fail, evals).  step<=0, 격자에 안 맞는 범위 → ValueError,
    check_ends 이고 lo 가 이미 FAIL → ValueError, hi 가 PASS → NoBoundary."""
    # TODO: 1) step<=0 또는 (hi-lo) 가 0 이거나 step 의 배수가 아니면 ValueError
    #       2) 방향 d = ±1, 격자 칸 수 n = |hi-lo| // step,  x(i) = lo + d*i*step
    #       3) check_ends 면 끝점 2번 평가(evals += 2): lo 가 FAIL → ValueError, hi 가 PASS → NoBoundary
    #       4) 불변식 g=PASS, b=FAIL 을 지키며 b-g > 1 동안 가운데 m 을 평가 (evals += 1)
    #       반환 (x(g), x(b), evals)
    return lo, hi, 0


def sprt_vote(run_once, p0, p1, alpha=0.01, beta=0.01, max_n=30):
    """Wald SPRT: H0 '실패율 p0(좋은 커밋, flaky 만)' vs H1 '실패율 p1(회귀)'.
    로그 우도비(LLR)가 ln((1-beta)/alpha) 이상 → bad, ln(beta/(1-alpha)) 이하 → good.
    max_n 까지 결론이 안 나면 LLR 부호로 정한다. 반환 (is_good, passes, fails)."""
    # TODO: 0<p0<p1<1 아니면 ValueError.
    #       upper = ln((1-beta)/alpha), lower = ln(beta/(1-alpha))
    #       PASS 마다 llr += ln((1-p1)/(1-p0)), FAIL 마다 llr += ln(p1/p0)
    #       llr >= upper → (False, ...) bad,  llr <= lower → (True, ...) good,  max_n 소진 → llr < 0 이면 good
    return True, 0, 0


POLICIES = {
    "naive": lambda r: majority_vote(r, 1),
    "majority3": lambda r: majority_vote(r, 3),
    "majority5": lambda r: majority_vote(r, 5),
    "retry3": lambda r: retry_until_pass(r, 3),
    "sprt": lambda r: sprt_vote(r, SPRT_P0, SPRT_P1, 0.01, 0.01, 30),
}


def simulate(policy, p_good_fail, p_bad_fail, trials=2000, n=64, seed=11):
    """(주어진 코드) 0..n 위에 경계 b 를 무작위로 두고 잡음 있는 테스트로 bisect → (오답률, 평균 실행 횟수)"""
    rng = random.Random(seed)
    wrong = runs = 0
    for _ in range(trials):
        b = rng.randint(1, n)          # 인덱스 >= b 가 bad (첫 bad = b)

        def is_fail(x):
            def run_once(_attempt):
                nonlocal runs
                runs += 1
                return rng.random() >= (p_bad_fail if x >= b else p_good_fail)
            return not POLICIES[policy](run_once)[0]
        _, first_fail, _ = find_boundary(0, n, is_fail, check_ends=False)
        wrong += first_fail != b
    return wrong / trials, runs / trials


# ================================================================ Part C: 범인 확정 (이항 검정)
def binom_sf(k, n, p):
    """P(X >= k), X ~ Binomial(n, p)"""
    # TODO: k<=0 → 1.0, k>n → 0.0, 아니면 sum_{i=k..n} C(n,i) p^i (1-p)^(n-i)   (math.comb)
    return 0.0


def confirm_culprit(parent_fails, parent_n, culprit_fails, culprit_n, p0, alpha=0.01):
    """부모 커밋의 실패는 flaky(p0) 로 설명돼야 하고(p_parent >= alpha),
    범인 커밋의 실패는 flaky 로 설명되면 안 된다(p_culprit < alpha)."""
    # TODO: p_parent = binom_sf(parent_fails, parent_n, p0), p_culprit = binom_sf(culprit_fails, culprit_n, p0)
    #       confirmed = p_parent >= alpha and p_culprit < alpha
    return {"confirmed": False, "p_parent": 0.0, "p_culprit": 1.0}


# ================================================================ 실험실: 펌웨어 모델 + mock DUT + git (주어진 코드)
SUBJECTS = [
    "init: DMA split + descriptor ring", "ring: bump TX ring to 128", "log: add trace ids",
    "pcie: retrain link on LTSSM timeout", "ci: lint fixes", "power: gate DMA clock in idle",
    "dma: add per-channel stats", "docs: descriptor format", "irq: coalesce TX done",
    "boot: print ROM version", "i2c: PMIC retry on NAK", "dma: prefetch next descriptor",
    "thermal: throttle at 105C", "log: rate-limit warnings", "uart: 3M baud option",
    "spi: flash quad read", "pcie: ASPM L1 default off", "cfg: board EVB-07 straps",
    "dma: align ring to 64B", "test: hooks for loopback", "ecc: scrub interval 10s",
    "pm: DVFS table v3", "clk: PLL lock timeout 2ms",
]
REGRESS_SUBJECT = {"split": "dma: compute segment count up front (faster)",
                   "max_seg": "dma: raise MAX_SEG to 8192 for throughput"}
SPLIT_GOOD = ["    segs = []",
              "    while nbytes > 0:",
              "        n = min(nbytes, max_seg)",
              "        segs.append(n)",
              "        nbytes -= n",
              "    return segs"]
SPLIT_BUG = ["    count = nbytes // max_seg + 1",
             "    segs = [max_seg] * (count - 1)",
             "    segs.append(nbytes - max_seg * (count - 1))",
             "    return segs"]


def fw_source(build, changes, max_seg, buggy_split, broken):
    lines = ['"""ORCA DMA firmware model (bisect lab, generated)"""',
             "FW_BUILD = %d" % build,
             "MAX_SEG = %d  # bytes per DMA descriptor" % max_seg,
             "CHANGES = ["] + ["    %r," % c for c in changes] + ["]", ""]
    if broken:
        lines += ["def dbg_dump(:  # WIP: does not build", "    pass", ""]
    lines += ["", "def split_transfer(nbytes, max_seg=MAX_SEG):"]
    lines += SPLIT_BUG if buggy_split else SPLIT_GOOD
    return "\n".join(lines) + "\n"


def make_history(n, regress_at, kind, break_at=None):
    """[(subject, fw.py 내용)] — regress_at 부터 회귀, break_at 커밋은 빌드 실패(다음 커밋이 고침)"""
    hist = []
    for i in range(n):
        s = SUBJECTS[i % len(SUBJECTS)]
        if i == regress_at:
            s = REGRESS_SUBJECT[kind]
        elif break_at is not None and i == break_at:
            s = "debug: WIP dump helper"
        elif break_at is not None and i == break_at + 1:
            s = "debug: fix WIP build"
        subj = "c%02d: %s" % (i, s)
        bug = i >= regress_at
        src = fw_source(i, [h[0] for h in hist] + [subj], 8192 if (bug and kind == "max_seg") else 4096,
                        bug and kind == "split", broken=(i == break_at))
        hist.append((subj, src))
    return hist


def load_fw(src):
    """fw.py 소스 → 네임스페이스 dict. 문법 오류(= 빌드 실패)는 SyntaxError 로 올라온다."""
    ns = {}
    exec(compile(src, "fw.py", "exec"), ns)  # noqa: S102 — 실험실에서 우리가 생성한 소스만 실행
    return ns


def dut_dma(segs, nbytes):
    """mock DUT(ORCA-A0 DMA). 정상이면 None, 아니면 에러 문자열"""
    if sum(segs) != nbytes:
        return "len sum %d != %d" % (sum(segs), nbytes)
    for i, s in enumerate(segs):
        if s <= 0:
            return "desc[%d] len=%d illegal" % (i, s)
        if s > HW_MAX_SEG:
            return "desc[%d] len=%d > %d" % (i, s, HW_MAX_SEG)
    return None


def make_run_once(fw, workload, seed, flake, size=None):
    """한 번의 하드웨어 테스트. 잡음은 (seed, 빌드, 크기, 시도 번호)로 결정적."""
    def run_once(attempt):
        rng = random.Random(((seed * 1000003 + fw["FW_BUILD"]) * 1009 + (size or 0)) * 101 + attempt)
        if rng.random() < flake:            # 링크 재학습 글리치 같은 HW/인프라 잡음 (펌웨어와 무관)
            return False
        if workload == "fixed":
            sizes = FIXED_SIZES
        elif workload == "random":
            sizes = [rng.randint(64, 6000) for _ in range(3)]
        else:
            sizes = [size]
        return all(dut_dma(fw["split_transfer"](s), s) is None for s in sizes)
    return run_once


def git_env():
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}   # GIT_DIR 등 상속 차단
    env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1", GIT_TERMINAL_PROMPT="0",
               GIT_AUTHOR_NAME="SIV Lab", GIT_AUTHOR_EMAIL="lab@example.invalid",
               GIT_COMMITTER_NAME="SIV Lab", GIT_COMMITTER_EMAIL="lab@example.invalid", LC_ALL="C")
    return env


def git(repo, *args, check=True, extra_env=None):
    env = git_env()
    if extra_env:
        env.update(extra_env)
    r = subprocess.run(["git", "-C", repo, "-c", "init.defaultBranch=main", "-c", "commit.gpgsign=false",
                        "-c", "advice.detachedHead=false"] + list(args),
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, timeout=300)
    if check and r.returncode != 0:
        raise RuntimeError("git %s failed (%d): %s" % (" ".join(args), r.returncode, r.stdout))
    return r


def make_repo(root, hist, tags=None):
    """일회용 저장소 생성. 커밋 날짜를 고정해 해시까지 결정적으로 만든다. 반환 (repo, [sha...])"""
    repo = os.path.join(root, "orca-fw")
    os.makedirs(repo)
    git(repo, "init", "-q")
    shas = []
    for i, (subj, src) in enumerate(hist):
        with open(os.path.join(repo, "fw.py"), "w", encoding="utf-8") as f:
            f.write(src)
        git(repo, "add", "fw.py")
        date = "2026-11-01T%02d:%02d:00+0000" % (9 + i // 60, i % 60)
        git(repo, "commit", "-q", "-m", subj, extra_env={"GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date})
        shas.append(git(repo, "rev-parse", "HEAD").stdout.strip())
        if tags and i in tags:
            git(repo, "tag", tags[i])
    return repo, shas


def git_bisect(repo, good, bad, runner_args):
    """git bisect start/run/log/reset. 반환 dict(first='c23' 또는 None, sha, out, log, rc)"""
    git(repo, "bisect", "start", bad, good)
    r = git(repo, "bisect", "run", sys.executable, "-I", SELF, "--runner", *runner_args, check=False)
    m = re.search(r"^([0-9a-f]{40}) is the first bad commit", r.stdout, re.M)
    first = sha = None
    if m:
        sha = m.group(1)
        first = git(repo, "log", "-1", "--format=%s", sha).stdout.strip().split(":")[0]
    log = git(repo, "bisect", "log").stdout
    git(repo, "bisect", "reset")
    return {"first": first, "sha": sha, "out": r.stdout, "log": log, "rc": r.returncode}


def runner_main(argv):
    """git bisect run 이 커밋마다 부르는 러너. cwd = 체크아웃된 작업 트리."""
    o = {"--policy": "majority3", "--workload": "fixed", "--seed": "1", "--flake": "0",
         "--log": "", "--infra-at": "-1"}
    for i in range(0, len(argv) - 1, 2):
        o[argv[i]] = argv[i + 1]
    try:
        with open("fw.py", encoding="utf-8") as f:
            src = f.read()
    except OSError:
        src = ""
    m = re.search(r"^FW_BUILD = (\d+)", src, re.M)
    build = int(m.group(1)) if m else -1
    rec = {"build": build, "policy": o["--policy"], "passes": 0, "fails": 0}
    if build == int(o["--infra-at"]):
        rec["status"], code = "infra", exit_code("infra")     # 예: 보드가 JTAG 에 응답 없음
    else:
        try:
            fw = load_fw(src)
        except SyntaxError:
            fw = None
        if fw is None:
            rec["status"], code = "build_fail", exit_code("build_fail")
        else:
            good, rec["passes"], rec["fails"] = POLICIES[o["--policy"]](
                make_run_once(fw, o["--workload"], int(o["--seed"]), float(o["--flake"])))
            rec["status"], code = "ok", exit_code("ok", good)
    rec["exit"] = code
    if o["--log"]:
        with open(o["--log"], "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, sort_keys=True) + "\n")
    return code


def read_jsonl(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


# ---------------------------------------------------------------- Part C 시나리오 (주어진 코드)
SEED_A = 2           # Part A: naive 가 틀리는 재현용 seed (md 참고)
SEED_C = 7


def part_c(tmp):
    hist = make_history(40, 23, "max_seg", break_at=19)
    repo, shas = make_repo(tmp, hist, tags={0: "v1.4.0", 39: "v1.5.0"})
    log = os.path.join(tmp, "runner_c.jsonl")
    res = git_bisect(repo, "v1.4.0", "v1.5.0", ["--policy", "sprt", "--workload", "random", "--seed", str(SEED_C),
                                                "--flake", "0.05", "--log", log])
    steps = read_jsonl(log)
    out = {"bisect": res, "steps": steps}
    if res["first"] is None:
        return out
    ci = int(res["first"][1:])
    pi = ci - 1
    fws = {}
    for idx in (pi, ci):
        fws[idx] = load_fw(git(repo, "show", "%s:fw.py" % shas[idx]).stdout)    # 체크아웃 없이 읽기

    def fails_of(idx, n=20):
        r = make_run_once(fws[idx], "random", SEED_C + 1000, 0.05)
        return sum(1 for a in range(n) if not r(a))
    pf, cf = fails_of(pi), fails_of(ci)
    out["confirm"] = (pf, cf, confirm_culprit(pf, 20, cf, 20, SPRT_P0))

    def size_fail(idx, counter):
        def is_fail(s):
            g, p, f = sprt_vote(make_run_once(fws[idx], "size", SEED_C, 0.05, size=s), SPRT_P0, SPRT_P1)
            counter.append(p + f)
            return not g
        return is_fail
    runs_c, runs_p = [], []
    out["boundary"] = find_boundary(64, 65536, size_fail(ci, runs_c))
    out["boundary_runs"] = sum(runs_c)
    try:
        out["parent_boundary"] = find_boundary(64, 65536, size_fail(pi, runs_p))
    except NoBoundary:
        out["parent_boundary"] = None
    out["ci"], out["pi"], out["subject"] = ci, pi, hist[ci][0]
    out["report"] = format_report(out)
    return out


def format_report(o):
    b = o["bisect"]
    steps = o["steps"]
    skipped = ["c%02d" % s["build"] for s in steps if s["exit"] == EXIT_SKIP]
    pf, cf, conf = o["confirm"]
    lp, ff, ev = o["boundary"]
    lines = [
        "== BISECT REPORT: dma_loopback_random on EVB-07 (ORCA-A0) ==",
        "range     : v1.4.0 (c00, good) .. v1.5.0 (c39, bad)",
        "policy    : SPRT p0=%.2f p1=%.2f alpha=beta=0.01, max 30 runs/step" % (SPRT_P0, SPRT_P1),
        "first bad : %s %s \"%s\"" % ("c%02d" % o["ci"], b["sha"][:10], o["subject"].split(": ", 1)[1]),
        "skipped   : %s" % (", ".join(s + " (build failed -> exit 125)" for s in skipped) or "none"),
        "cost      : %d runner calls (git bisect steps), %d test runs" % (len(steps), sum(s["passes"] + s["fails"] for s in steps)),
        "confirm   : parent c%02d %d/20 fail (p=%.3g) | culprit c%02d %d/20 fail (p=%.2g) -> %s" % (
            o["pi"], pf, conf["p_parent"], o["ci"], cf, conf["p_culprit"],
            "CONFIRMED" if conf["confirmed"] else "NOT CONFIRMED"),
        "boundary  : culprit c%02d size 64..65536 B: last pass %d B, first fail %d B (%d probes, %d runs)" % (
            o["ci"], lp, ff, ev, o["boundary_runs"]),
        "            parent  c%02d size 64..65536 B: %s" % (
            o["pi"], "no fail up to 65536 B" if o["parent_boundary"] is None else "fails too: %r" % (o["parent_boundary"],)),
        "next      : DMA segments > %d B fail on A0 since c%02d; check MAX_SEG vs A0 descriptor limit" % (lp, o["ci"]),
    ]
    return "\n".join(lines)


# ================================================================ 테스트 하네스
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


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def scripted(seq):
    """'PFP' → run_once: 시도 i 의 결과를 문자열에서 읽는다. 호출 횟수도 센다."""
    calls = []

    def run_once(i):
        calls.append(i)
        return seq[i] == "P"
    return run_once, calls


def run_tests():
    print("-- Part A: vote / exit code --")
    for seq, n, exp in [("P", 1, (True, 1, 0)), ("F", 1, (False, 0, 1)), ("PP", 3, (True, 2, 0)),
                        ("FF", 3, (False, 0, 2)), ("PFP", 3, (True, 2, 1)), ("FPF", 3, (False, 1, 2)),
                        ("PFPFF", 5, (False, 2, 3)), ("FPPFP", 5, (True, 3, 2))]:
        r, calls = scripted(seq)
        check("majority_vote(%s, n=%d) == %r" % (seq, n, exp),
              lambda r=r, n=n, exp=exp, calls=calls: majority_vote(r, n) == exp and len(calls) == exp[1] + exp[2])
    check("majority_vote n=2 -> ValueError", lambda: raises(ValueError, lambda: majority_vote(lambda i: True, 2)))
    check("exit_code ok/good=0 ok/bad=1", lambda: exit_code("ok", True) == 0 and exit_code("ok", False) == 1)
    check("exit_code build_fail=125 (skip)", lambda: exit_code("build_fail") == 125)
    check("exit_code infra=255 (abort, never 'bad')", lambda: exit_code("infra") == 255)

    print("-- Part A: real git bisect run (throwaway repo) --")
    with tempfile.TemporaryDirectory(prefix="siv_m11_") as tmp:
        repo, shas = make_repo(tmp, make_history(16, 11, "split"), tags={0: "good0", 15: "bad15"})
        cfg = git(repo, "config", "--list", "--show-origin").stdout
        check("isolated git config (no ~/.gitconfig)",
              lambda: "file:" in cfg and all(l.startswith(("file:.git/config", "command line:"))
                                             for l in cfg.splitlines()))
        check("16 commits, deterministic hashes", lambda: len(shas) == 16 and len(set(shas)) == 16)
        logs = {k: os.path.join(tmp, "runner_%s.jsonl" % k) for k in ("clean", "naive", "maj", "infra")}
        clean = git_bisect(repo, "good0", "bad15", ["--policy", "naive", "--flake", "0", "--log", logs["clean"]])
        check("no flake, naive -> c11 (true culprit)", lambda: clean["first"] == "c11")
        naive = git_bisect(repo, "good0", "bad15", ["--policy", "naive", "--flake", "0.10",
                                                    "--seed", str(SEED_A), "--log", logs["naive"]])
        check("flake 10%%, naive, seed %d -> WRONG commit (reproduced)" % SEED_A,
              lambda: naive["first"] is not None and naive["first"] != "c11")
        maj = git_bisect(repo, "good0", "bad15", ["--policy", "majority3", "--flake", "0.10",
                                                  "--seed", str(SEED_A), "--log", logs["maj"]])
        check("flake 10%%, majority-of-3, seed %d -> c11" % SEED_A, lambda: maj["first"] == "c11")
        steps = read_jsonl(logs["maj"])
        check("majority runner: >=2 runs per step, <=3", lambda: steps and all(2 <= s["passes"] + s["fails"] <= 3
                                                                               for s in steps))
        infra = git_bisect(repo, "good0", "bad15", ["--policy", "naive", "--infra-at", "7", "--log", logs["infra"]])
        check("infra error at first probe -> bisect aborted, nobody blamed",
              lambda: infra["first"] is None and infra["rc"] != 0
              and "git bisect bad " not in infra["log"] and "git bisect good " not in infra["log"])
        print("   naive  -> %s | majority3 -> %s | clean -> %s" % (naive["first"], maj["first"], clean["first"]))
    check("temp repo removed", lambda: not os.path.exists(tmp))

    print("-- Part B: parameter bisect --")
    vmin = lambda v: v < 713                     # 713 mV 아래에서 실패
    check("Vmin 1000->600 step 5 -> (715, 710)", lambda: find_boundary(1000, 600, vmin, 5)[:2] == (715, 710))
    check("Fmax 800->2000 step 25 -> (1600, 1625)",
          lambda: find_boundary(800, 2000, lambda f: f > 1612, 25)[:2] == (1600, 1625))
    check("buffer 64->65536 -> (4096, 4097), evals <= 2+17",
          lambda: (lambda r: r[:2] == (4096, 4097) and r[2] <= 19)(find_boundary(64, 65536, lambda s: s > 4096)))
    check("adjacent ends (n=1) -> 2 evals", lambda: find_boundary(10, 11, lambda x: x >= 11) == (10, 11, 2))
    check("lo already fails -> ValueError", lambda: raises(ValueError, lambda: find_boundary(0, 64, lambda x: True)))
    check("hi passes -> NoBoundary", lambda: raises(NoBoundary, lambda: find_boundary(0, 64, lambda x: False)))
    check("span not multiple of step -> ValueError",
          lambda: raises(ValueError, lambda: find_boundary(1000, 603, vmin, 5)))
    calls = []
    find_boundary(0, 1024, lambda x: calls.append(x) or x >= 300, check_ends=False)
    check("1024 points -> exactly 10 probes", lambda: len(calls) == 10)
    r, c = scripted("FFF" + "P" * 27)
    check("sprt: FFF -> bad after 3 runs (p0=.1 p1=.5)", lambda: sprt_vote(r, .1, .5) == (False, 0, 3) and len(c) == 3)
    r, c = scripted("P" * 30)
    check("sprt: all pass -> good after 8 runs", lambda: sprt_vote(r, .1, .5) == (True, 8, 0))
    r, c = scripted("PFPPPPPPPPP" + "P" * 19)
    check("sprt: one fail delays 'good' to 12 runs", lambda: sprt_vote(r, .1, .5) == (True, 11, 1))
    check("sprt p0>=p1 -> ValueError", lambda: raises(ValueError, lambda: sprt_vote(lambda i: True, .5, .5)))

    print("-- Part B: flaky model, 2000 trials x 64 points --")
    rates = {}
    for scen, pg, pb in [("one-sided", 0.10, 1.00), ("intermittent", 0.05, 0.65)]:
        for pol in ("naive", "majority3", "majority5", "retry3", "sprt"):
            rates[scen, pol] = simulate(pol, pg, pb)
            print("   %-12s %-9s wrong=%5.1f%%  runs/bisect=%5.1f" % (scen, pol, 100 * rates[scen, pol][0],
                                                                    rates[scen, pol][1]))
    check("clean test (0%/100%): naive always right, 6 runs",
          lambda: simulate("naive", 0.0, 1.0, trials=300) == (0.0, 6.0))
    check("one-sided: naive wrong >= 20%", lambda: rates["one-sided", "naive"][0] >= 0.20)
    check("intermittent: naive wrong >= 50%", lambda: rates["intermittent", "naive"][0] >= 0.50)
    check("one-sided: retry3 wrong <= 1%", lambda: rates["one-sided", "retry3"][0] <= 0.01)
    check("intermittent: retry3 wrong >= 80% (retry hides the bug)",
          lambda: rates["intermittent", "retry3"][0] >= 0.80)
    check("intermittent: majority3 still wrong >= 50%", lambda: rates["intermittent", "majority3"][0] >= 0.50)
    check("sprt wrong <= 6% in both scenarios",
          lambda: rates["one-sided", "sprt"][0] <= 0.06 and rates["intermittent", "sprt"][0] <= 0.06)

    print("-- Part C: nightly regression (40 commits, intermittent + build break) --")
    check("binom_sf(0, 20, .1) == 1", lambda: binom_sf(0, 20, 0.1) == 1.0)
    check("binom_sf(1, 20, .1) == 1-0.9^20", lambda: abs(binom_sf(1, 20, 0.1) - (1 - 0.9 ** 20)) < 1e-12)
    check("binom_sf(3, 5, .5) == 0.5", lambda: abs(binom_sf(3, 5, 0.5) - 0.5) < 1e-12)
    check("confirm(1/20, 14/20, p0=.1) -> confirmed", lambda: confirm_culprit(1, 20, 14, 20, 0.1)["confirmed"])
    check("confirm(7/20, 14/20) -> NOT (parent too flaky)",
          lambda: not confirm_culprit(7, 20, 14, 20, 0.1)["confirmed"])
    check("confirm(1/20, 4/20) -> NOT (culprit within flake)",
          lambda: not confirm_culprit(1, 20, 4, 20, 0.1)["confirmed"])
    res = {}
    with tempfile.TemporaryDirectory(prefix="siv_m11_c_") as tmp:
        try:
            res = part_c(tmp)
        except Exception as e:  # noqa: BLE001
            res = {"error": "%s: %s" % (type(e).__name__, e)}
    if "error" in res:
        print("   part_c error: " + res["error"])
    check("SPRT bisect -> c23", lambda: res["bisect"]["first"] == "c23")
    check("build-broken c19 skipped (exit 125)",
          lambda: any(s["build"] == 19 and s["exit"] == 125 for s in res["steps"]) and "skip" in res["bisect"]["log"])
    check("culprit confirmed (parent ~ flake, culprit not)", lambda: res["confirm"][2]["confirmed"])
    check("culprit size boundary: last pass 4096, first fail 4097", lambda: res["boundary"][:2] == (4096, 4097))
    check("parent passes whole size range (NoBoundary)", lambda: res["parent_boundary"] is None)
    if "report" in res:
        print(res["report"])
    check("report names culprit + boundary",
          lambda: "first bad : c23" in res["report"] and "last pass 4096 B, first fail 4097 B" in res["report"])

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) > 1 and argv[1] == "--runner":
        return runner_main(argv[2:])
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
