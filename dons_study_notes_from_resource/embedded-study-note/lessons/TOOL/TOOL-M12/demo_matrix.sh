#!/usr/bin/env bash
# demo_matrix.sh - bug x tool matrix on the Mac (real runs).
#   bash demo_matrix.sh > matrix.txt
# Each column runs one tool/config on fw_bugs.c and fw_clean.c.  A BUG counts
# as found when the tool reports a warning/error on one of its tagged lines
# (style-only checks that merely land on the line are not counted).
# The VM columns (scan-build-21, cppcheck 2.19, clang-tidy-21, gcc -fanalyzer)
# come from demo_vm.sh -> vm.txt.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=$(pwd)
CT=$HOME/.venvs/clangtidy/bin/clang-tidy
W=/tmp/TOOL-M12_work/matrix
rm -rf "$W" && mkdir -p "$W"
INC="-Iinclude -Ithird_party"
CF="-std=c11 -Wall -Wextra -D_FORTIFY_SOURCE=0 $INC"   # same flags as check.sh compile DB
TPL='{file}:{line}:{column}: {severity}: {message} [{id}]'
TAGS=$(grep -oE '// BUG[0-9][0-9]' fw_bugs.c | sed 's|// ||' | sort -u)
# bug-finding ids: tidy bug families, compiler -W, analyzer core./unix./..., cppcheck camelCase ids
BUGIDS='(clang-analyzer|clang-diagnostic|bugprone|cert|concurrency)-|\[-W|\[(core|unix|deadcode|security|optin|alpha)\.|\[[a-z][A-Za-z0-9_]*\]$'

norm() { sed -e "s|^$D/||" | grep -E '^[^ :]+:[0-9]+:[0-9]+: (error|warning|style|performance|portability): '; }

# col <name> <command...>: run on both files ("%F" = file placeholder)
NAMES=()
col() {
	local name=$1 i=${#NAMES[@]}
	shift
	NAMES+=("$name")
	for f in fw_bugs fw_clean; do
		local cmd=${*//%F/$f.c}
		bash -c "$cmd" 2>&1 | norm >"$W/$i.$f.txt"
	done
}

col "cc -Wall -Wextra"             "cc $CF -fsyntax-only %F"
col "cc ... -Wthread-safety"       "cc $CF -Wthread-safety -fsyntax-only %F"
col "clang --analyze (default)"    "clang --analyze $INC -std=c11 %F -o /dev/null"
col "clang --analyze (tuned)"      "clang --analyze $INC -std=c11 -Xanalyzer -analyzer-max-loop -Xanalyzer 16 -Xanalyzer -analyzer-config -Xanalyzer suppress-null-return-paths=false %F -o /dev/null"
col "clang-tidy '*'"               "$CT --checks='*' %F -- $CF"
col "clang-tidy starter.clang-tidy" "$CT --config-file=starter.clang-tidy %F -- $CF"
col "clang-tidy solution.clang-tidy" "$CT --config-file=solution.clang-tidy %F -- $CF"
col "clang-tidy practice.clang-tidy" "$CT --config-file=practice.clang-tidy %F -- $CF"
col "cppcheck (no --enable)"       "cppcheck --template='$TPL' $INC %F"
col "cppcheck --enable=all"        "cppcheck --enable=all --template='$TPL' $INC %F"
col "cppcheck solution.cppcheck"   "cppcheck $(sed -e 's/#.*//' solution.cppcheck | tr '\n' ' ') --template='$TPL' $INC %F"

echo "# TOOL-M12 bug x tool matrix (Mac) - $(date +%Y-%m-%d)"
echo "# $($CT --version | grep -m1 version | sed 's/^ *//'), $(cppcheck --version), $(cc --version | head -1)"
echo
printf '%-32s' "tool / config"
for t in $TAGS; do printf ' %s' "${t#BUG}"; done
printf '  found  fw_clean\n'
for i in "${!NAMES[@]}"; do
	printf '%-32s' "${NAMES[$i]}"
	n=0
	for t in $TAGS; do
		hit=0
		for l in $(grep -n "// $t" fw_bugs.c | cut -d: -f1); do
			if grep "^fw_bugs.c:$l:" "$W/$i.fw_bugs.txt" | grep -qE "$BUGIDS"; then hit=1; fi
		done
		if [ $hit = 1 ]; then printf '  o'; n=$((n + 1)); else printf '  .'; fi
	done
	printf '  %2d/10  %4s\n' "$n" "$(wc -l <"$W/$i.fw_clean.txt" | tr -d ' ')"
done
echo
echo "(o = finding on a tagged line, . = none; fw_clean = number of findings on the fixed file, any file/check)"
echo
for i in "${!NAMES[@]}"; do
	echo "== ${NAMES[$i]}: findings on BUG lines of fw_bugs.c"
	for t in $TAGS; do
		for l in $(grep -n "// $t" fw_bugs.c | cut -d: -f1); do
			grep "^fw_bugs.c:$l:" "$W/$i.fw_bugs.txt" | grep -E "$BUGIDS" | sed "s/^/  $t  /; s/,-warnings-as-errors//"
		done
	done
	echo "  fw_clean.c findings by id:"
	grep -oE '\[[^]]+\]$' "$W/$i.fw_clean.txt" | sed 's/,-warnings-as-errors//' | sort | uniq -c | sort -rn | head -8 | sed 's/^/   /'
done
