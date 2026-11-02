#!/usr/bin/env python3
"""N06B section 2.4: check the four statistics practice problems (stdlib only)."""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # works under python3 -I
from coverage_report import upper_bound, runs_needed  # noqa: E402

print("P1 rule of three: 0 failures in 600 boot cycles")
u = upper_bound(0, 600)
print(f"   exact 95% UB = {u:.6f} (1 in {1 / u:.0f}); 3/n = {3 / 600:.6f}")
for c in (0.90, 0.95, 0.99):
    print(f"   {c:.0%} confidence: -ln(alpha) = {-math.log(1 - c):.3f}"
          f" -> UB ~ {-math.log(1 - c) / 600:.5f}")

print("P2 failure-rate upper bound: 3 failures in 2000 cycles")
u = upper_bound(3, 2000)
print(f"   observed {3 / 2000:.4%}; exact Clopper-Pearson 95% UB = {u:.6f} (1 in {1 / u:.0f})")
print("   Poisson approx 7.754/n = %.6f" % (7.754 / 2000))
print("   soak: 0 failures in 5000 device-hours -> rate UB = %.6f /h, MTBF >= %.0f h"
      % (3 / 5000, 5000 / 3))

print("P3 claim a 1-in-500 bug is fixed")
n = runs_needed(0, 1 / 500)
print(f"   0-fail runs needed for UB < 1/500: {n}; P(0 fails in {n} | still 1/500) = "
      f"{(1 - 1 / 500) ** n:.4f}")
print(f"   if 1 failure appears: need {runs_needed(1, 1 / 500)} total runs with no more")
print(f"   claim 10x better (UB < 1/5000): {runs_needed(0, 1 / 5000)} runs")
print(f"   at a stress condition where rate is 1/20: {runs_needed(0, 1 / 20)} runs")

print("P4 how many chips / boards")
for r, c in ((0.99, 0.95), (0.99, 0.90), (0.999, 0.95)):
    print(f"   success-run R={r} C={c}: n = {math.ceil(math.log(1 - c) / math.log(r))} parts, 0 fail")
print(f"   AEC-Q100 77/lot: 90% UB = {upper_bound(0, 77, 0.10):.4f}; "
      f"231 pooled: {upper_bound(0, 231, 0.10):.4f}")
for n in (3, 9, 22):
    print(f"   P(see >=1 bad part | 10% bad, n={n}) = {1 - 0.9 ** n:.3f}")
print(f"   chips for 90% chance to see a 10% defect: {math.ceil(math.log(0.1) / math.log(0.9))}")
runs = 3 * 3 * 5
print(f"   matrix 3 process x 3 chips x 5 V/F/T = {runs} sessions x 15 min = {runs * 15 / 60:.2f} h;"
      f" 3 boards parallel = {runs * 15 / 60 / 3:.2f} h (+ chamber soak per T step)")
