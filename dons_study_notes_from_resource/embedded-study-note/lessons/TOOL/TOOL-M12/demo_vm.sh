#!/usr/bin/env bash
# demo_vm.sh - the Linux/GNU side of the matrix, run in the Lima VM "elinux".
#   bash demo_vm.sh > vm.txt          (~20 s; Mac home is read-only in the VM)
# Columns: gcc 15 -Wall -Wextra, gcc 15 -fanalyzer, scan-build-21 (make wrapper,
# --status-bugs), clang-tidy-21 with solution.clang-tidy, cppcheck 2.19 with
# solution.cppcheck.  Same tagging rule as demo_matrix.sh.
set -u
cd "$(dirname "$0")" || exit 2
SRC=$(pwd)
limactl shell elinux -- bash -s "$SRC" <<'VM'
set -u
SRC=$1
W=/tmp/TOOL-M12_work/vm
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
cp -R "$SRC"/fw_bugs.c "$SRC"/fw_clean.c "$SRC"/include "$SRC"/third_party "$SRC"/solution.* .
INC="-Iinclude -Ithird_party"
TAGS=$(grep -oE '// BUG[0-9][0-9]' fw_bugs.c | sed 's|// ||' | sort -u)
BUGIDS='(clang-analyzer|clang-diagnostic|bugprone|cert|concurrency)-|\[-W|\[(core|unix|deadcode|security|optin|alpha)\.|\[[a-z][A-Za-z0-9_]*\]$|\[CWE-'
norm() { sed -e "s|^$W/||" | grep -E '^[^ :]+:[0-9]+:[0-9]+: (error|warning|style|performance|portability): '; }
printf '#makefile for scan-build\nall: fw_bugs.o fw_clean.o\n%%.o: %%.c\n\t$(CC) -std=c11 -Wall -Wextra %s -c $< -o $@\n' "$INC" >Makefile

NAMES=()
col() {
	local name=$1 i=${#NAMES[@]}
	shift
	NAMES+=("$name")
	for f in fw_bugs fw_clean; do
		local cmd=${*//%F/$f.c}
		bash -c "$cmd" 2>&1 | norm >"$i.$f.txt"
	done
}
col "gcc-15 -Wall -Wextra"           "gcc -std=c11 -Wall -Wextra $INC -fsyntax-only %F"
col "gcc-15 -fanalyzer"              "gcc -std=c11 -fanalyzer $INC -c %F -o /dev/null"
col "clang-tidy-21 solution"         "clang-tidy-21 --config-file=solution.clang-tidy %F -- -std=c11 -Wall -Wextra -D_FORTIFY_SOURCE=0 $INC"
col "cppcheck-2.19 solution"         "cppcheck \$(sed -e 's/#.*//' solution.cppcheck | tr '\n' ' ') --template='{file}:{line}:{column}: {severity}: {message} [{id}]' $INC %F"

echo "# TOOL-M12 bug x tool matrix (Lima VM elinux, aarch64 Ubuntu) - $(date +%Y-%m-%d)"
echo "# $(gcc --version | head -1); $(clang-tidy-21 --version | grep -m1 version | sed 's/^ *//'); $(cppcheck --version)"
echo
echo "== scan-build-21 -o out --status-bugs make  (wraps the real build: CC=ccc-analyzer)"
scan-build-21 -o "$W/out" --status-bugs make -B >"$W/sb.log" 2>&1
sb_rc=$?
sed "s|$W/||g" "$W/sb.log" | grep -vE '^ *[0-9]* *\|' | grep -vE "^fw_[a-z]+\.c: In function"
echo "scan-build exit status: $sb_rc   (HTML reports: $(ls "$W"/out/*/report-*.html | wc -l))"
# re-run per file for the matrix (plain text diagnostics)
NAMES+=("scan-build-21 (default)")
i=$((${#NAMES[@]} - 1))
for f in fw_bugs fw_clean; do
	scan-build-21 -o "$W/out_$f" clang-21 -std=c11 $INC -c $f.c -o /dev/null 2>&1 | norm >"$i.$f.txt"
done
echo

printf '%-28s' "tool / config"
for t in $TAGS; do printf ' %s' "${t#BUG}"; done
printf '  found  fw_clean\n'
for i in "${!NAMES[@]}"; do
	printf '%-28s' "${NAMES[$i]}"
	n=0
	for t in $TAGS; do
		hit=0
		for l in $(grep -n "// $t" fw_bugs.c | cut -d: -f1); do
			if grep "^fw_bugs.c:$l:" "$i.fw_bugs.txt" | grep -qE "$BUGIDS"; then hit=1; fi
		done
		if [ $hit = 1 ]; then printf '  o'; n=$((n + 1)); else printf '  .'; fi
	done
	printf '  %2d/10  %4s\n' "$n" "$(wc -l <"$i.fw_clean.txt" | tr -d ' ')"
done
echo
for i in "${!NAMES[@]}"; do
	echo "== ${NAMES[$i]}: findings on BUG lines of fw_bugs.c"
	for t in $TAGS; do
		for l in $(grep -n "// $t" fw_bugs.c | cut -d: -f1); do
			grep "^fw_bugs.c:$l:" "$i.fw_bugs.txt" | grep -E "$BUGIDS" | sed "s/^/  $t  /; s/,-warnings-as-errors//"
		done
	done
	echo "  fw_clean.c findings:"
	sed 's/^/    /; s/,-warnings-as-errors//' "$i.fw_clean.txt" | head -5
done
echo
echo "== clang-tidy-21 --verify-config on solution.clang-tidy"
clang-tidy-21 --verify-config --config-file=solution.clang-tidy 2>&1 | head -5
echo "== clang-tidy-21 default HeaderFilterRegex"
clang-tidy-21 --dump-config 2>/dev/null | grep -E 'HeaderFilterRegex|SystemHeaders'
VM
