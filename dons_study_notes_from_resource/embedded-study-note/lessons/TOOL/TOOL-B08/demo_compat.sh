#!/usr/bin/env bash
# TOOL-B08: which newer GNU Make features exist in this make? (run on the Mac 3.81 and in the VM 4.4.1)
set -u
T=/tmp/TOOL-B08_compat; rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 2
echo "== $(make --version | head -1) =="
try() {  # try <name> <makefile text> [make args]
	local name=$1 text=$2; shift 2
	printf '%b' "$text" > Makefile
	local out; out=$(make -s "$@" 2>&1 | tr '\n' '|' | cut -c1-90)
	printf '%-26s %s\n' "$name" "$out"
}
try ".ONESHELL (3.82)"       '.ONESHELL:\nall:\n\t@cd /tmp\n\t@pwd\n'
try "!= shell assign (4.0)"  'X != echo hi\nall:\n\t@echo "X=[$(X)]"\n'
try "\$(file ...) (4.0)"     'all:\n\t@echo "[$(file >f.txt,hello)]"; cat f.txt 2>/dev/null || echo "no f.txt"\n'
try "grouped &: (4.3)"       'a b &: ; @echo made a and b once\nall: a b\n' all
try "--output-sync (4.0)"    'all:\n\t@echo ok\n' --output-sync=target
try "--trace (4.0)"          'all:\n\t@echo ok\n' --trace
try "\$(wildcard) sorted?"   'all:\n\t@echo $(wildcard z.c a.c m.c)\n'
touch z.c a.c m.c
try "\$(wildcard) sorted? (2)" 'all:\n\t@echo $(wildcard z.c a.c m.c) / $(wildcard *.c)\n'
try "else ifeq (3.81 ok)"    'X=2\nifeq ($(X),1)\nY=one\nelse ifeq ($(X),2)\nY=two\nendif\nall:\n\t@echo Y=$(Y)\n'
try "order-only | (3.80)"    'all: o | d\no:\n\t@echo built o\nd:\n\t@echo made d\n'
