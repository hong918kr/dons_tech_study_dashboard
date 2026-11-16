#!/usr/bin/env bash
# TOOL-M08 demo — bisect across a merged feature branch, with and without --first-parent.
#   bash demo_first_parent.sh > demo_first_parent_out.txt 2>&1
# main: m0 m1 m2 m3 m4 m5 M m6 m7 m8      feature (from m3): f1 f2(bug) f3(does not build) f4(build fixed)
set -u
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C TZ=UTC
export GIT_AUTHOR_NAME=demo GIT_AUTHOR_EMAIL=demo@example.com GIT_COMMITTER_NAME=demo GIT_COMMITTER_EMAIL=demo@example.com
D=/tmp/TOOL-M08_work/demo_fp
rm -rf "$D" && mkdir -p "$D/repo" && cd "$D/repo" || exit 2
git -c init.defaultBranch=main init -q .
n=0
c() {  # c <subject> : commit everything with a fixed, increasing date
	n=$((n + 1)); git add -A
	GIT_AUTHOR_DATE="@$((1767603600 + n * 60)) +0000" GIT_COMMITTER_DATE="@$((1767603600 + n * 60)) +0000" git commit -q -m "$1"
}
echo 0 > main.txt; c m0
for i in 1 2 3; do echo $i >> main.txt; c m$i; done
git checkout -q -b feature
echo a > feat.txt; c f1
echo bug > bug; c "f2 (introduces the bug)"
echo x > broken; c "f3 (does not build)"
rm broken; c "f4 (build fixed)"
git checkout -q main
for i in 4 5; do echo $i >> main.txt; c m$i; done
n=$((n + 1))
GIT_AUTHOR_DATE="@$((1767603600 + n * 60)) +0000" GIT_COMMITTER_DATE="@$((1767603600 + n * 60)) +0000" \
	git merge -q --no-ff -m "M (merge feature)" feature
for i in 6 7 8; do echo $i >> main.txt; c m$i; done
git log --graph --format='%s' | sed 's/^/  /'

cat > "$D/t.sh" <<'EOF'
#!/bin/sh
[ -e broken ] && exit 125
[ -e bug ] && exit 1
exit 0
EOF
chmod +x "$D/t.sh"

for mode in "" "--first-parent"; do
	echo; echo "=== git bisect start${mode:+ $mode} main m0"
	git bisect start $mode main "$(git rev-list -1 --grep '^m0$' main)" | grep -o 'Bisecting.*'
	git bisect run "$D/t.sh" >"$D/out.txt" 2>&1
	grep -E 'is the first bad commit|could be any' "$D/out.txt" | sed 's/[0-9a-f]\{40\}/<sha>/'
	echo "tested: $(git bisect log | sed -nE 's/^# (good|bad|skip): \[[0-9a-f]*\] (.*)$/\2=\1/p' | tr '\n' ';')"
	echo "first bad: $(git log -1 --format=%s refs/bisect/bad)"
	git bisect reset -q >/dev/null 2>&1 || git bisect reset >/dev/null 2>&1
done
