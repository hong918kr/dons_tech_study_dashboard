#!/bin/bash
# TOOL-M08 bisect run script for the fw repo (gen_repo.sh). Keep it OUTSIDE the repo:
#   git bisect start main v1.0 && git bisect run /path/to/this_script.sh
# git runs it once per step with cwd = top of the work tree, the step's commit checked out.
# Exit-code contract of `git bisect run`:
#   0 = good   1..124, 126, 127 = bad   125 = skip (cannot test)   128..255 = abort the bisect
set -u

# Oracle = what a correct build prints. Frozen here, NOT read from the checkout.
EXPECTED='crc16 "123456789" = 0x29B1
rb order = 4 5 6 7 8 21 22 23'

# The rig itself is broken (no make / no compiler) -> abort instead of skipping every commit.
command -v make >/dev/null 2>&1 && command -v "${CC:-cc}" >/dev/null 2>&1 || exit 255

# 1) every step starts from a pristine tree: no objects/binaries from the previous checkout
git clean -fdxq

# 2) a commit that does not build cannot be judged -> skip it, never call it bad
make -s app >/dev/null 2>&1 || exit 125

# 3) run; a crash (rc >= 128, e.g. 134 = SIGABRT) is a BAD commit, not a reason to abort
out=$(./build/app 2>/dev/null)
rc=$?
if [ "$rc" -ne 0 ]; then echo "bisect-test: app rc=$rc -> bad" >&2; exit 1; fi

# 4) compare with the frozen oracle
got=$(printf '%s\n' "$out" | grep -v '^fw version' | head -n 2)
if [ "$got" = "$EXPECTED" ]; then exit 0; fi
echo "bisect-test: output differs -> bad" >&2
exit 1
