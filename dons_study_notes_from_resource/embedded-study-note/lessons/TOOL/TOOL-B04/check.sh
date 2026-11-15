#!/usr/bin/env bash
# TOOL-B04 check: build anatomy.c as Mach-O, ELF (aarch64) and ELF (Cortex-M4),
# read the REAL nm / objdump output, and compare it with the prediction table
# in <variant>.txt.
#   bash check.sh starter|solution|practice
#   SHOW_ACTUAL=1 bash check.sh starter     -> also print the real value on FAIL (spoiler)
# Mac only (Apple clang 21 + /usr/bin/objdump). Writes only to /tmp/TOOL-B04_work/<variant>.
set -u
cd "$(dirname "$0")" || exit 2
SRC=$PWD
V=${1:-}
case "$V" in
starter|solution|practice) ;;
*) echo "usage: check.sh starter|solution|practice"; exit 2 ;;
esac
[ -f "$SRC/$V.txt" ] || { echo "missing $V.txt"; exit 2; }

export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
OBJDUMP=/usr/bin/objdump                                   # Apple LLVM 21: reads Mach-O and ELF
LLVM_NM=/Library/Developer/CommandLineTools/usr/bin/llvm-nm
NM=/usr/bin/nm                                             # never /usr/local/bin/llvm-* (LLVM 3.2)
W=/tmp/TOOL-B04_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
cp "$SRC/anatomy.c" "$W/"
cd "$W" || exit 2

# ---- 1. build ------------------------------------------------------------------
# build <name> <macho|elf> <compiler and flags...>
build() {
	local name=$1 fmt=$2
	shift 2
	if ! "$@" -c anatomy.c -o "$name.o" 2>"$name.err"; then
		echo "FAIL build $name: $*"; cat "$name.err"; exit 1
	fi
	dump "$name" "$fmt"
}

# dump -> <name>.sym : "<symbol> <nm letter> <section>"   (leading '_' of Mach-O names removed)
#         <name>.rel : "<section> <reloc type> <target>"
#         <name>.sec : section names (objdump -h)
dump() {
	local name=$1 fmt=$2
	if [ "$fmt" = macho ]; then
		"$NM" "$name.o" | awk '{ if (NF == 3) print $3, $2; else print $2, $1 }' >"$name.letters"
		"$NM" -m "$name.o" | awk '{
			s = $0; sub(/^[^(]*\(/, "", s); sub(/\).*/, "", s); print $NF, s }' >"$name.sects"
	else
		"$LLVM_NM" "$name.o" | awk '{ if (NF == 3) print $3, $2; else print $2, $1 }' >"$name.letters"
		"$OBJDUMP" -t "$name.o" | awk -F'\t' '/^[0-9a-f]+ / {
			n = split($1, a, " "); m = split($2, b, " "); print b[m], a[n] }' >"$name.sects"
	fi
	awk 'NR == FNR { sect[$1] = $2; next }
	     { s = $1; sub(/^_/, "", s); sec = ($1 in sect) ? sect[$1] : "?"; print s, $2, sec }' \
		"$name.sects" "$name.letters" >"$name.sym"
	"$OBJDUMP" -r "$name.o" | awk '
		/^RELOCATION RECORDS FOR \[/ { s = $0; sub(/.*\[/, "", s); sub(/\].*/, "", s); next }
		/^[0-9a-f]+ / && s != "" { print s, $2, $3 }' >"$name.rel"
	"$OBJDUMP" -h "$name.o" | awk '/^ *[0-9]+ / { print $2 }' >"$name.sec"
}

case "$V" in
starter|solution)
	build macho macho cc -O2
	build elf   elf   clang --target=aarch64-linux-gnu -O2
	build m4    elf   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2
	;;
practice)
	build macho_nocommon macho cc -O2 -fno-common
	build elf_common     elf   clang --target=aarch64-linux-gnu -O2 -fcommon
	build elf_O0         elf   clang --target=aarch64-linux-gnu -O0
	build elf_fsect      elf   clang --target=aarch64-linux-gnu -O2 -ffunction-sections -fdata-sections
	build elf_nopic      elf   clang --target=aarch64-linux-gnu -O2 -fno-pic
	;;
esac

# ---- 2. actual answers ---------------------------------------------------------
# act_sym <build> <symbol>  -> "<letter> <section>"  or "- -" when the symbol is absent
act_sym() {
	awk -v n="$2" '$1 == n { print $2, $3; f = 1; exit } END { if (!f) print "- -" }' "$1.sym"
}
# act_rel <build> <key>  -> comma-joined sorted unique reloc types, or "-"
act_rel() {
	local b=$1 key=$2 secre valre name r
	name=${key#*:}
	secre='^(__text|\.text)$'
	case "$key" in
	ref:s_local)       valre='^_?counter_next\.s_local$|^\.bss' ;;
	ref:str_api_entry) valre='^l_\.str|^\.L\.str|^\.rodata\.str' ;;
	data:*)            secre='^(__data|\.data)$'; valre='.' ;;
	relsec:text)
		r=$(grep -E '^\.rela?\.text$' "$b.sec" | head -1)
		echo "${r:--}"; return ;;
	*)                 valre="^_?${name}\$" ;;
	esac
	r=$(awk -v sr="$secre" -v vr="$valre" '$1 ~ sr && $3 ~ vr { print $2 }' "$b.rel" |
		sort -u | paste -sd, -)
	echo "${r:--}"
}
norm() { echo "$1" | tr ',' '\n' | sed '/^$/d' | sort -u | paste -sd, -; }

# ---- 3. predictions ------------------------------------------------------------
# pred <section> <key> -> the prediction row's fields after the key ("?" if row missing)
pred() {
	awk -v want="[$1]" -v k="$2" '
		{ sub(/#.*/, "") }
		/^[ \t]*\[/ { cur = $1; next }
		cur == want && $1 == k { $1 = ""; sub(/^ +/, ""); print; f = 1; exit }
		END { if (!f) print "?" }' "$SRC/$V.txt"
}
# pred_p <build> <kind> <key> -> practice row "<kind> <build> <key> fields..."
pred_p() {
	awk -v b="$1" -v kind="$2" -v k="$3" '
		{ sub(/#.*/, "") }
		$1 == kind && $2 == b && $3 == k { $1 = $2 = $3 = ""; sub(/^ +/, ""); print; f = 1; exit }
		END { if (!f) print "?" }' "$SRC/$V.txt"
}

P=0; T=0
report() { # report <ok 0/1> <label> <predicted> <actual>
	T=$((T + 1))
	if [ "$1" = 1 ]; then
		P=$((P + 1)); echo "PASS $2"
	else
		if [ "${SHOW_ACTUAL:-0}" = 1 ]; then
			echo "FAIL $2   predicted: $3   actual: $4"
		else
			echo "FAIL $2   predicted: $3"
		fi
	fi
}
cmp_fields() { # cmp_fields "<pred fields>" "<actual fields>" ; reloc cells are order-free lists
	local p a i n ok=1
	set -f
	# shellcheck disable=SC2206
	p=($1); a=($2)
	set +f
	[ "${#p[@]}" -eq "${#a[@]}" ] || { echo 0; return; }
	n=${#a[@]}
	i=0
	while [ $i -lt $n ]; do
		[ "$(norm "${p[$i]}")" = "$(norm "${a[$i]}")" ] || ok=0
		i=$((i + 1))
	done
	echo $ok
}

SYMS="api_entry board_init counter_next helper kept_static unused_static counter_next.s_local
ext_log ext_counter g_init g_zero g_tent g_const g_msg s_file s_file_zero"
RELS="call:ext_log call:board_init call:helper ref:g_init ref:g_tent ref:g_const ref:ext_counter
ref:s_local ref:str_api_entry data:g_msg relsec:text"
M4RELS="call:ext_log call:helper ref:ext_counter ref:g_init ref:str_api_entry data:g_msg relsec:text"

if [ "$V" != practice ]; then
	echo "== [symbols]  symbol | macho_nm macho_section | elf_nm elf_section"
	for s in $SYMS; do
		a="$(act_sym macho "$s") $(act_sym elf "$s")"
		p=$(pred symbols "$s")
		report "$(cmp_fields "$p" "$a")" "[symbols] $s" "$p" "$a"
	done
	echo "== [relocs]  ref | macho_reloc | elf_reloc"
	for r in $RELS; do
		if [ "$r" = relsec:text ]; then
			a=$(grep -E '^(__rel|\.rel)' macho.sec | head -1)   # Mach-O keeps relocs per section, no .rel* section
			a="${a:--} $(act_rel elf "$r")"
		else
			a="$(act_rel macho "$r") $(act_rel elf "$r")"
		fi
		p=$(pred relocs "$r")
		report "$(cmp_fields "$p" "$a")" "[relocs] $r" "$p" "$a"
	done
	echo "== [cortex-m4]  ref | m4_reloc (clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2)"
	for r in $M4RELS; do
		a=$(act_rel m4 "$r")
		p=$(pred cortex-m4 "$r")
		report "$(cmp_fields "$p" "$a")" "[cortex-m4] $r" "$p" "$a"
	done
else
	echo "== [practice]  sym <build> <symbol> <nm> <section>  /  rel <build> <ref> <types>"
	PSYM="macho_nocommon:g_tent elf_common:g_tent elf_O0:s_file elf_O0:s_file_zero elf_O0:unused_static
elf_fsect:api_entry elf_fsect:board_init elf_fsect:g_init elf_fsect:g_zero elf_fsect:g_tent
elf_fsect:g_const elf_fsect:counter_next.s_local"
	PREL="macho_nocommon:ref:g_tent elf_common:ref:g_tent elf_O0:ref:g_const elf_nopic:ref:ext_counter"
	for x in $PSYM; do
		b=${x%%:*}; s=${x#*:}
		a=$(act_sym "$b" "$s"); p=$(pred_p "$b" sym "$s")
		report "$(cmp_fields "$p" "$a")" "[practice] sym $b $s" "$p" "$a"
	done
	for x in $PREL; do
		b=${x%%:*}; r=${x#*:}
		a=$(act_rel "$b" "$r"); p=$(pred_p "$b" rel "$r")
		report "$(cmp_fields "$p" "$a")" "[practice] rel $b $r" "$p" "$a"
	done
fi

echo "$P/$T passed"
[ "$P" -eq "$T" ] && exit 0
exit 1
