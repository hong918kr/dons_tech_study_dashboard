#!/bin/bash
# TOOL-M08 bisect run script for the fw repo (gen_repo.sh). Keep it OUTSIDE the repo:
#   git bisect start main v1.0 && git bisect run /path/to/this_script.sh
# git runs it once per step with cwd = top of the work tree, the step's commit checked out.
# Exit-code contract of `git bisect run`:
#   0 = good   1..124, 126, 127 = bad   125 = skip (cannot test)   128..255 = abort the bisect
set -u

# TODO(4): the oracle lives in the checkout (tests/check.sh + tests/expected.txt),
#          so every commit is judged by ITS OWN version of the test.
#
# The rig itself is broken (no make / no compiler) -> abort instead of skipping every commit.
command -v make >/dev/null 2>&1 && command -v "${CC:-cc}" >/dev/null 2>&1 || exit 255

# TODO(3): incremental build in the old build/ dir -- objects from the previous checkout are reused
#

# TODO(1): a commit that does not build is reported as bad
make -s app >/dev/null 2>&1 || exit 1

# TODO(2): the test's raw exit status is passed through (crash -> 134 -> bisect aborts)
sh tests/check.sh >/dev/null 2>&1
rc=$?
exit "$rc"

# (expected: run the app, compare with a frozen oracle, crash/mismatch -> 1, match -> 0)


