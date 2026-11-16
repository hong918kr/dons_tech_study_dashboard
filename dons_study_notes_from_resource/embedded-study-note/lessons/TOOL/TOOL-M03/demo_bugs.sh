#!/usr/bin/env bash
# TOOL-M03 demo (Mac): starter 의 함정을 "하나씩만" solution.c 에 다시 넣어 어떤 CHECK 가 잡는지 본다.
#   bash demo_bugs.sh > demo_bugs.txt 2>&1
set -u
L=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M03_work/bugs; rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1

inject() {   # inject <name> <python replace: old> <new>
	python3 -I - "$L/solution.c" "$1.c" "$2" "$3" <<'PY'
import sys
src, dst, old, new = sys.argv[1:5]
s = open(src).read()
assert s.count(old) == 1, old
open(dst, "w").write(s.replace(old, new))
PY
	echo "##### $1"
	cc -std=c11 -Wall -Wextra -Werror -O2 "$1.c" -o "$1" && ./"$1" | sed "s/^FAIL $1.c:/FAIL line /"
	echo "rc=${PIPESTATUS[0]}"
}
inject bug1_unused \
	'__attribute__((used, aligned(_Alignof(struct shell_cmd)), section(SHELL_SECTION))) SHELL_NO_ASAN' \
	'__attribute__((unused, section(SHELL_SECTION)))'
inject bug2_bytes 'return (size_t)(e - b); ' 'return (size_t)((const char *)e - (const char *)b); '
inject bug3_stride 'return b + i; ' 'return (const struct shell_cmd *)((const char *)b + i); '
inject bug4_typo \
	'#  define SHELL_SECTION   "__DATA," SHELL_SECT_NAME' \
	'#  define SHELL_SECTION   "__DATA,__shellcmds"
#  undef SHELL_SECT_NAME
#  define SHELL_SECT_NAME "__shellcmd"                       /* 복사-붙여넣기 오타 */'
inject bug5_nosort 'qsort(idx, n, sizeof idx[0], cmp_cmd);' '(void)cmp_cmd;'
