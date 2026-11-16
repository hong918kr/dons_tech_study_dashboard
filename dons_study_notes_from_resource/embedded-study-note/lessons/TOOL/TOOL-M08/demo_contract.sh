#!/usr/bin/env bash
# TOOL-M08 demo — what `git bisect run` really does with each exit code (real git, tiny repo).
#   bash demo_contract.sh > demo_contract_out.txt 2>&1
# Repo: 16 commits t00..t15, file n holds the index; the "bug" appears at t09.
set -u
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C TZ=UTC
export GIT_AUTHOR_NAME=demo GIT_AUTHOR_EMAIL=demo@example.com GIT_COMMITTER_NAME=demo GIT_COMMITTER_EMAIL=demo@example.com
D=/tmp/TOOL-M08_work/demo_contract
rm -rf "$D" && mkdir -p "$D/repo" "$D/s" && cd "$D/repo" || exit 2
git -c init.defaultBranch=main init -q .
for i in $(seq 0 15); do
	printf '%d\n' "$i" > n; git add n
	GIT_AUTHOR_DATE="@$((1767603600 + i * 60)) +0000" GIT_COMMITTER_DATE="@$((1767603600 + i * 60)) +0000" \
		git commit -q -m "$(printf 't%02d' "$i")"
done
echo "git: $(git --version)"

mk() {  # mk <name> <body...>: script that knows i = contents of n
	local name=$1; shift
	{ echo '#!/bin/bash'; echo 'i=$(cat n)'; printf '%s\n' "$@"; } > "$D/s/$name"; chmod +x "$D/s/$name"
}
run() {  # run <title> <bisect-run args...>
	echo; echo "=== $1"; shift
	git bisect reset -q >/dev/null 2>&1; git checkout -q main
	git bisect start main main~15 >/dev/null
	git bisect run "$@" >"$D/out.txt" 2>&1; local rc=$?
	grep -v '^running\|^Bisecting\|^\[\|^commit\|^Author\|^Date\|^ *$\|^ *t[0-9]*$\|^ n \|changed' "$D/out.txt"
	echo "-> git bisect run exit $rc; tested: $(git bisect log | sed -n 's/^# \([a-z]*\): \[[0-9a-f]*\] \(t[0-9]*\)$/\2=\1/p' | tr '\n' ' ')"
	git bisect reset -q >/dev/null 2>&1 || git bisect reset >/dev/null 2>&1
}

mk ok.sh       '[ "$i" -ge 9 ] && exit 1; exit 0'
mk skip56.sh   'case $i in 5|6) exit 125;; esac' '[ "$i" -ge 9 ] && exit 1; exit 0'
mk skip78.sh   'case $i in 7|8) exit 125;; esac' '[ "$i" -ge 9 ] && exit 1; exit 0'
mk rc126.sh    '[ "$i" -ge 9 ] && exit 126; exit 0'
mk rc127.sh    '[ "$i" -ge 9 ] && exit 127; exit 0'
mk rc134.sh    '[ "$i" -ge 9 ] && kill -ABRT $$; exit 0'
mk rcm1.sh     '[ "$i" -ge 9 ] && exit -1; exit 0'
mk rc128.sh    '[ "$i" -ge 9 ] && exit 128; exit 0'
mk never.sh    'exit 0'

run "baseline: 0 = good, 1 = bad"                        "$D/s/ok.sh"
run "125 on t05,t06 (far from the bug) -> skipped, still exact" "$D/s/skip56.sh"
run "125 on t07,t08 (right before the bug) -> ambiguous" "$D/s/skip78.sh"
run "126 (found, not executable) counts as bad"         "$D/s/rc126.sh"
run "127 (command not found) counts as bad"             "$D/s/rc127.sh"
run "killed by SIGABRT -> shell status 134"             "$D/s/rc134.sh"
run "exit -1 -> status 255"                             "$D/s/rcm1.sh"
run "exit 128"                                          "$D/s/rc128.sh"
echo; echo "=== 127 again, verbose: which commits does git actually run the script on?"
printf '#!/bin/bash\ni=$(cat n); echo "  [script ran at t$i]" >&2\n[ "$i" -ge 9 ] && exit 127; exit 0\n' > "$D/s/rc127v.sh"; chmod +x "$D/s/rc127v.sh"
git checkout -q main; git bisect start main main~15 >/dev/null
git bisect run "$D/s/rc127v.sh" 2>&1 | grep -E '^\[|script ran|first bad'
git bisect reset -q >/dev/null 2>&1 || git bisect reset >/dev/null 2>&1
run "script path does not exist"                        "$D/s/no_such_script.sh"
run "test never reproduces the bug (always 0)"          "$D/s/never.sh"

echo; echo "=== script stored INSIDE the repo (untracked) + git clean -fdx in the script"
git checkout -q main
printf '#!/bin/bash\ngit clean -fdxq\n[ "$(cat n)" -ge 9 ] && exit 1; exit 0\n' > inrepo.sh; chmod +x inrepo.sh
git bisect start main main~15 >/dev/null
git bisect run ./inrepo.sh >"$D/out.txt" 2>&1; echo "git bisect run exit $?"
grep -v '^Bisecting\|^\[\|^commit\|^Author\|^Date\|^ *$\|^ *t[0-9]*$\|^ n \|changed' "$D/out.txt"
echo "tested: $(git bisect log | sed -n 's/^# \([a-z]*\): \[[0-9a-f]*\] \(t[0-9]*\)$/\2=\1/p' | tr '\n' ' ')"
echo "inrepo.sh still exists? $( [ -e inrepo.sh ] && echo yes || echo no )"
git bisect reset -q >/dev/null 2>&1 || git bisect reset >/dev/null 2>&1
