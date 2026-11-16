#!/usr/bin/env bash
# demo_extra.sh - side experiments quoted in the lesson (Mac, real runs).
#   bash demo_extra.sh > extra.txt     (~40 s)
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=$(pwd)
CT=$HOME/.venvs/clangtidy/bin/clang-tidy
W=/tmp/TOOL-M12_work/extra
rm -rf "$W" && mkdir -p "$W"
INC="-Iinclude -Ithird_party"
CF="-std=c11 -Wall -Wextra -D_FORTIFY_SOURCE=0 $INC"
find_only() { sed -e "s|^$D/||" -e "s|^$W/||" | grep -E '^[^ :]+:[0-9]+:[0-9]+: (error|warning|style|performance|portability|information): ' | sed 's/,-warnings-as-errors//'; }

echo "== E1. clang-tidy 23 built-in defaults (no .clang-tidy anywhere above /tmp)"
(cd /tmp && "$CT" --dump-config 2>/dev/null | grep -E '^(Checks|WarningsAsErrors|HeaderFilterRegex|SystemHeaders):')
echo
echo "== E2. 'clang-diagnostic-*' alone (compiler warnings only)"
"$CT" --checks='-*,clang-diagnostic-*' fw_bugs.c -- $CF 2>&1 | grep -vE '^ *[0-9]* *\|' | head -3
echo
echo "== E3. HeaderFilterRegex forms vs how -I is spelled (canary: include/canary.h, third_party/vendor_canary.h)"
C=$W/canary
mkdir -p "$C/include" "$C/third_party"
printf '#define CANARY_TWICE(x) x * 2\n' >"$C/include/canary.h"
printf '#define VENDOR_TWICE(x) x * 2\n' >"$C/third_party/vendor_canary.h"
printf '#include "canary.h"\n#include "vendor_canary.h"\nint canary(int a);\nint canary(int a) { return CANARY_TWICE(a) + VENDOR_TWICE(a); }\n' >"$C/canary.c"
for re in '<unset: 23 default .*>' '' '.*/include/.*' '(^|/)include/'; do
	for spell in rel abs; do
		if [ "$spell" = rel ]; then I="-Iinclude -Ithird_party"; else I="-I$C/include -I$C/third_party"; fi
		if [ "${re:0:1}" = '<' ]; then hf=(); else hf=(--header-filter="$re"); fi
		got=$(cd "$C" && "$CT" --checks='-*,bugprone-macro-parentheses' ${hf[@]+"${hf[@]}"} canary.c -- -std=c11 $I 2>/dev/null |
			grep -oE '(include|third_party)/[a-z_]+\.h:[0-9]+:[0-9]+: warning' | sed -E 's/:[0-9]+:[0-9]+: warning//' | tr '\n' ' ')
		printf '  %-26s -I %-4s -> reported: %s\n' "'$re'" "$spell" "${got:-(nothing)}"
	done
done
echo
echo "== E4. analyzer knobs one at a time on fw_bugs.c (clang --analyze)"
for knob in "" "-Xanalyzer -analyzer-max-loop -Xanalyzer 16" \
	"-Xanalyzer -analyzer-config -Xanalyzer suppress-null-return-paths=false"; do
	echo "  knobs: [${knob:-default}]"
	clang --analyze -std=c11 $INC $knob fw_bugs.c -o /dev/null 2>&1 | find_only | sed 's/^/    /'
done
echo
echo "== E5. analyzer options via CheckOptions (clang-analyzer-<option>) vs ExtraArgs"
cat >"$W/opt.yaml" <<'EOF'
Checks: '-*,clang-analyzer-*'
CheckOptions:
  clang-analyzer-suppress-null-return-paths: false
EOF
echo "  --dump-config CheckOptions: $("$CT" --config-file="$W/opt.yaml" --dump-config 2>/dev/null | grep CheckOptions)"
echo "  CheckOptions route, findings on line 217 (BUG01): $("$CT" --config-file="$W/opt.yaml" fw_bugs.c -- $CF 2>/dev/null | find_only | grep -c '^fw_bugs.c:217:')"
echo "  ExtraArgs route (practice.clang-tidy), findings on line 217: $("$CT" --config-file=practice.clang-tidy fw_bugs.c -- $CF 2>/dev/null | find_only | grep -c '^fw_bugs.c:217:')"
echo
echo "== E6. insecureAPI.DeprecatedOrUnsafeBufferHandling on fw_clean.c: macOS fortify headers hide the calls"
for fort in "" "-D_FORTIFY_SOURCE=0"; do
	n=$("$CT" --checks='-*,clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' fw_clean.c -- -std=c11 $INC $fort 2>/dev/null | find_only | tee "$W/e6.txt" | wc -l | tr -d ' ')
	echo "  [${fort:-default macOS SDK}] findings: $n"
	sed 's/^/    /' "$W/e6.txt" | cut -c1-150
done
echo "  preprocessed memcpy on the Mac:  $(cc -std=c11 $INC -E fw_clean.c | grep -m1 -oE '__builtin___memcpy_chk \([^,]+')"
echo
echo "== E7. solution.clang-tidy WITHOUT that exclusion, fw_clean.c (target-like flags)"
grep -v 'DeprecatedOrUnsafeBufferHandling,' solution.clang-tidy >"$W/sol_noexcl.yaml"
"$CT" --config-file="$W/sol_noexcl.yaml" fw_clean.c -- $CF 2>/dev/null | find_only | cut -c1-140
echo
echo "== E8. rewrite the HAL idiom instead of suppressing it: (void)UART1->SR; (void)UART1->DR;"
mkdir -p "$W/rw"
cp -R include third_party "$W/rw/"
python3 -I - "$D/fw_clean.c" "$W/rw/fw_clean.c" <<'EOF'
import sys
s = open(sys.argv[1]).read()
old = """        uint32_t tmp = UART1->SR; // NOLINT(clang-analyzer-deadcode.DeadStores)
        tmp = UART1->DR;          // cppcheck-suppress redundantInitialization
        (void)tmp;
"""
assert old in s
s = s.replace(old, """        (void)UART1->SR;
        (void)UART1->DR;
""")
open(sys.argv[2], "w").write(s)
EOF
(cd "$W/rw" && grep -n '(void)UART1' fw_clean.c | sed 's/^/  /')
(cd "$W/rw" && echo "  clang --analyze : $(clang --analyze -std=c11 $INC fw_clean.c -o /dev/null 2>&1 | find_only | wc -l | tr -d ' ') findings")
(cd "$W/rw" && echo "  clang-tidy sol  : $("$CT" --config-file="$D/solution.clang-tidy" fw_clean.c -- $CF 2>/dev/null | find_only | wc -l | tr -d ' ') findings")
(cd "$W/rw" && echo "  cppcheck sol    : $(cppcheck $(sed -e 's/#.*//' "$D/solution.cppcheck" | tr '\n' ' ') $INC --template='{file}:{line}:{column}: {severity}: {message} [{id}]' fw_clean.c 2>&1 | find_only | wc -l | tr -d ' ') findings")
(cd "$W/rw" && cc -std=c11 -O2 -S $INC fw_clean.c -o - 2>/dev/null | awk '/^_uart1_rx_poll:/,/ret$/' | grep -nE 'ldr|ret' | head -6 | sed 's/^/  asm: /')
echo
echo "== E9. cppcheck MISRA C:2012 addon on fw_clean.c (no rule texts installed: ids only; addon findings are 'style')"
cppcheck --addon=misra --enable=style $INC --template='{file}:{line}: {id}' fw_clean.c 2>&1 | grep -E '^fw_clean.c:[0-9]+: misra' >"$W/misra.txt"
echo "  findings: $(wc -l <"$W/misra.txt" | tr -d ' ')  (distinct rules: $(cut -d' ' -f2 "$W/misra.txt" | sort -u | wc -l | tr -d ' '))"
cut -d' ' -f2 "$W/misra.txt" | sort | uniq -c | sort -rn | head -8 | sed 's/^/   /'
echo
echo "== E10. cost: wall time per tool on fw_bugs.c (median of 5 runs, parallel jobs on this Mac -> noisy)"
med() {
	local ts=() t0 t1
	for _ in 1 2 3 4 5; do
		t0=$(python3 -c 'import time; print(time.time())')
		bash -c "$1" >/dev/null 2>&1
		t1=$(python3 -c 'import time; print(time.time())')
		ts+=("$(python3 -c "print(round(($t1-$t0)*1000))")")
	done
	printf '%s\n' "${ts[@]}" | sort -n | sed -n 3p
}
printf '  %-40s %6s ms\n' "cc -fsyntax-only -Wall -Wextra" "$(med "cc $CF -fsyntax-only fw_bugs.c")"
printf '  %-40s %6s ms\n' "clang --analyze (default)" "$(med "clang --analyze -std=c11 $INC fw_bugs.c -o /dev/null")"
printf '  %-40s %6s ms\n' "clang-tidy solution.clang-tidy" "$(med "$CT --config-file=solution.clang-tidy fw_bugs.c -- $CF")"
printf '  %-40s %6s ms\n' "clang-tidy practice.clang-tidy" "$(med "$CT --config-file=practice.clang-tidy fw_bugs.c -- $CF")"
printf '  %-40s %6s ms\n' "clang-tidy '*'" "$(med "$CT --checks='*' fw_bugs.c -- $CF")"
printf '  %-40s %6s ms\n' "cppcheck solution.cppcheck" "$(med "cppcheck $(sed -e 's/#.*//' solution.cppcheck | tr '\n' ' ') $INC fw_bugs.c")"
printf '  %-40s %6s ms\n' "cppcheck --check-level=exhaustive" "$(med "cppcheck $(sed -e 's/#.*//' practice.cppcheck | tr '\n' ' ') $INC fw_bugs.c")"
