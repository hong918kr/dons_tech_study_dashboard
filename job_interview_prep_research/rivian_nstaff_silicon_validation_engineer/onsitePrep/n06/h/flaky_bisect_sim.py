#!/usr/bin/env python3
"""N06H flaky-test build bisect simulator (stdlib only).

Question it answers: a regression appeared somewhere in N builds, but the failing
test is FLAKY -- on a bad build it fails only with probability p per run. How many
reruns per bisect step do we need so the bisect lands on the right first-bad build
with ~95% confidence? And after the fix, how many clean runs prove it?

Model:
  builds 0..N-1, builds < k are good, builds >= k are bad (k = first bad build).
  bad build : each run fails with prob p_bad, failure signature "SIG_REGRESSION".
  good build: each run fails with prob p_noise (unrelated flake, e.g. board/infra),
              failure signature "SIG_NOISE".
  verdict per step: run up to r times, stop at the first *counted* failure -> BAD;
              no counted failure in r runs -> GOOD.
  'counted' = any failure (naive) or only failures matching the regression
              signature (signature filter).
"""
import math
import random

def runs_needed_per_step(p, steps, overall_conf=0.95):
    """Smallest r so that P(detect bad at every step) >= overall_conf (worst case:
    every step lands on a bad build)."""
    per_step = overall_conf ** (1.0 / steps)          # required detection prob per step
    miss = 1.0 - per_step                              # allowed miss prob per step
    return math.ceil(math.log(miss) / math.log(1.0 - p))

def run_build(rng, is_bad, p_bad, p_noise, r, sig_filter):
    """Return (verdict_bad, runs_used)."""
    for i in range(1, r + 1):
        if is_bad and rng.random() < p_bad:
            return True, i                             # regression signature
        if rng.random() < p_noise:
            if not sig_filter:
                return True, i                         # noise counted as BAD
        # else pass, keep looping
    return False, r

def bisect_once(rng, n, k, p_bad, p_noise, r, sig_filter):
    lo, hi = 0, n - 1                                  # invariant: lo known good, hi known bad
    total = 0
    while hi - lo > 1:
        mid = (lo + hi) // 2
        bad, used = run_build(rng, mid >= k, p_bad, p_noise, r, sig_filter)
        total += used
        if bad:
            hi = mid
        else:
            lo = mid
    return hi, total                                   # hi = claimed first bad build

def monte_carlo(label, trials, n, p_bad, p_noise, r, sig_filter, seed):
    rng = random.Random(seed)
    ok = 0
    runs = 0
    off = 0
    for _ in range(trials):
        k = rng.randint(1, n - 1)                      # build 0 = known good, n-1 = known bad
        found, used = bisect_once(rng, n, k, p_bad, p_noise, r, sig_filter)
        ok += (found == k)
        runs += used
        off += abs(found - k)
    print(f"  {label:<44} r={r:<4} correct={100.0*ok/trials:6.2f}%  "
          f"mean_runs={runs/trials:8.1f}  mean_|offset|={off/trials:5.2f}")
    return ok / trials

def clean_runs_to_prove_fix(p_old, conf=0.95):
    """Zero failures in n runs -> upper bound on p < p_old with confidence conf."""
    return math.ceil(math.log(1.0 - conf) / math.log(1.0 - p_old))

def poisson_upper(k, conf=0.95):
    """One-sided upper bound on the expected count lambda given k observed events:
    smallest lambda with P(X <= k; lambda) <= 1-conf (bisection on the Poisson CDF)."""
    def cdf(lam):
        term, total = math.exp(-lam), math.exp(-lam)
        for i in range(1, k + 1):
            term *= lam / i
            total += term
        return total
    lo, hi = 0.0, 100.0
    for _ in range(200):
        mid = (lo + hi) / 2
        if cdf(mid) > 1 - conf:
            lo = mid
        else:
            hi = mid
    return hi

def main():
    n = 128
    steps = math.ceil(math.log2(n - 1))  # interval (0, n-1] has n-1 candidates
    print(f"=== N06H flaky bisect simulator: N={n} builds, bisect steps={steps} ===\n")

    print("[1] Analytic reruns per step for 95% overall confidence (worst case: every step on a bad build)")
    print(f"  {'p_fail(bad build)':>18} {'r/step':>7} {'naive r=1 detect/step':>22} {'1/p rule of thumb':>18}")
    for p in (0.5, 0.2, 0.1, 0.05, 0.01):
        r = runs_needed_per_step(p, steps)
        print(f"  {p:>18.2f} {r:>7d} {p:>22.2f} {math.ceil(1/p):>18d}")
    print()

    trials = 4000
    p_bad = 0.05
    r95 = runs_needed_per_step(p_bad, steps)
    print(f"[2] Monte Carlo, p_bad={p_bad} (1-in-20), no noise, {trials} bisects each")
    monte_carlo("naive: 1 run per step", trials, n, p_bad, 0.0, 1, False, 1)
    monte_carlo("1/p rule of thumb (20 runs)", trials, n, p_bad, 0.0, 20, False, 2)
    monte_carlo("3/p rule of three (60 runs)", trials, n, p_bad, 0.0, 60, False, 3)
    monte_carlo(f"analytic 95% (r={r95}), early stop on fail", trials, n, p_bad, 0.0, r95, False, 4)
    print()

    p_noise = 0.005
    print(f"[3] Same, but good builds also flake: p_noise={p_noise} (1-in-200, different signature)")
    monte_carlo(f"r={r95}, count ANY failure as bad", trials, n, p_bad, p_noise, r95, False, 5)
    monte_carlo(f"r={r95}, count only matching signature", trials, n, p_bad, p_noise, r95, True, 6)
    print()

    print("[4] Stress first: if a stress condition raises p_bad 0.05 -> 0.30")
    r_s = runs_needed_per_step(0.30, steps)
    monte_carlo(f"stressed, analytic 95% (r={r_s})", trials, n, 0.30, 0.0, r_s, False, 7)
    print()

    print("[5] Proving the fix: clean runs needed so that p_new < p_old at 95% (rule of three)")
    for p_old in (0.05, 0.01, 0.001):
        nclean = clean_runs_to_prove_fix(p_old)
        print(f"  p_old={p_old:<6} -> {nclean:>5} clean runs   (3/p approx = {3/p_old:7.0f})")
    # sanity: if the 'fix' did nothing, how often would we wrongly pass n clean runs?
    rng = random.Random(8)
    p_old = 0.05
    nclean = clean_runs_to_prove_fix(p_old)
    fooled = sum(all(rng.random() >= p_old for _ in range(nclean)) for _ in range(20000))
    print(f"  sanity: fix that does NOTHING (p stays {p_old}) still passes {nclean} clean runs "
          f"in {100.0*fooled/20000:.2f}% of 20000 tries (expect ~5%)")
    print()

    print("[6] Rate estimate with failures seen: 95% one-sided upper bound (Poisson approx)")
    for k, nrun in ((0, 3000), (1, 3000), (3, 3000), (3, 1000), (10, 10000)):
        ub = poisson_upper(k) / nrun
        print(f"  {k:>2} fails / {nrun:>5} runs -> point={k/nrun:.5f}  upper95={ub:.5f}  "
              f"(lambda_upper={poisson_upper(k):.2f})")

if __name__ == "__main__":
    main()
