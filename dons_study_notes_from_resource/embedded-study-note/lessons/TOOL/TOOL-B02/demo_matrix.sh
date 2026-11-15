#!/usr/bin/env bash
# TOOL-B02 demo: build the "bug x flag" tables from REAL compiler runs.
#   bash demo_matrix.sh > matrix.txt
# Mac: Apple clang 21.  VM (Lima `elinux`, if limactl exists): gcc 15.2.
# Each flag is tried ALONE (plus the compiler's default-on warnings), -O2 -c.
# Cell: ● = this flag flags the bug and the compiler's default does not,
#       ○ = flagged, but already flagged with no -W flag at all, · = not flagged.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B02_work/matrix
rm -rf "$W"
mkdir -p "$W"
TAGS="BUG01 BUG02 BUG03 BUG04 BUG05 BUG06 BUG07 BUG08 BUG09 BUG10 BUG11 BUG12"

# caught <diag file> -> space separated list of tags with a diagnostic on a tagged line
caught() {
	local t l res=""
	for t in $TAGS; do
		for l in $(grep -n "// $t" bugs12.c | cut -d: -f1); do
			if grep -qE "^bugs12.c:$l:[0-9]+: (warning|error):" "$1"; then
				res="$res $t"
				break
			fi
		done
	done
	echo "$res"
}
has() { case " $1 " in *" $2 "*) return 0 ;; esac; return 1; }
count() { set -- $1; echo $#; }

row() { # row <label> <diag file> <default-caught list>
	local label=$1 got base=$3 t line="" new=0
	got=$(caught "$2")
	for t in $TAGS; do
		if has "$got" "$t"; then
			if [ "$label" != "(no -W flag)" ] && has "$base" "$t"; then line="$line ○ |"; else line="$line ● |"; new=$((new + 1)); fi
		else
			line="$line · |"
		fi
	done
	echo "| \`$label\` |$line $new |"
}
header() {
	echo "| $1 | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 | new |"
	echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"
}

CLANG_FLAGS="-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Wimplicit-fallthrough
 -Wswitch -Wswitch-enum -Wcast-align -Wpedantic -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference
 -Wvla -Wdouble-promotion -Wundef -Wconditional-uninitialized"
GCC_FLAGS="-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Wimplicit-fallthrough
 -Wswitch-enum -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init"
STEPS="-Wall|-Wextra|-Wshadow|-Wconversion|-Wimplicit-fallthrough|-Wswitch-enum|-Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Wundef"

# ---------------- Mac clang ----------------
cc -std=c11 -O2 -c bugs12.c -o "$W/b.o" 2>"$W/c_none.diag"
BASE=$(caught "$W/c_none.diag")
echo "### Mac clang ($(cc --version | head -1)) — each flag alone, -O2"
echo
header "clang flag"
row "(no -W flag)" "$W/c_none.diag" ""
i=0
for f in $CLANG_FLAGS; do
	i=$((i + 1))
	cc -std=c11 -O2 $f -c bugs12.c -o "$W/b.o" 2>"$W/c_$i.diag"
	row "$f" "$W/c_$i.diag" "$BASE"
done
echo

# ---------------- VM gcc ----------------
HAVE_VM=0
if command -v limactl >/dev/null 2>&1; then
	{
		echo 'set -u; cd /tmp/TOOL-B02_work/matrix || exit 1'
		echo 'g() { echo "@@$1"; shift; gcc -std=c11 "$@" 2>&1 >/dev/null; }'
		echo 'g none -O2 -c bugs12.c -o b.o'
		i=0
		for f in $GCC_FLAGS; do
			i=$((i + 1))
			echo "g f$i -O2 $f -c bugs12.c -o b.o"
		done
		acc=""
		j=0
		OLDIFS=$IFS
		IFS='|'
		for s in $STEPS; do
			IFS=$OLDIFS
			j=$((j + 1))
			acc="$acc $s"
			echo "g s${j}_o2 -O2 $acc -c bugs12.c -o b.o"
			echo "g s${j}_o0 -O0 $acc -c bugs12.c -o b.o"
			echo "g s${j}_good -O2 $acc -isystem third_party -Werror good.c -o good"
			IFS='|'
		done
		IFS=$OLDIFS
		echo 'echo "@@version"; gcc --version | head -1'
	} >"$W/vm.sh"
	tar cf "$W/src.tar" bugs12.c good.c third_party
	# Mac home is read-only in the VM and /tmp is not shared: stream files in.
	limactl shell elinux -- bash -lc "rm -rf /tmp/TOOL-B02_work/matrix && mkdir -p /tmp/TOOL-B02_work/matrix && cd /tmp/TOOL-B02_work/matrix && tar xf -" <"$W/src.tar" &&
		limactl shell elinux -- bash -lc "bash -s" <"$W/vm.sh" >"$W/vm.out" 2>&1 &&
		HAVE_VM=1
fi

if [ $HAVE_VM = 1 ]; then
	# split vm.out into one diag file per @@label
	awk -v dir="$W" '/^@@/ { f = dir "/g_" substr($0, 3) ".diag"; printf "" > f; next } { print >> f }' "$W/vm.out"
	GBASE=$(caught "$W/g_none.diag")
	echo "### Lima VM gcc ($(cat "$W/g_version.diag")) — each flag alone, -O2"
	echo
	header "gcc flag"
	row "(no -W flag)" "$W/g_none.diag" ""
	i=0
	for f in $GCC_FLAGS; do
		i=$((i + 1))
		row "$f" "$W/g_f$i.diag" "$GBASE"
	done
	echo
fi

# ---------------- cumulative: add one flag at a time ----------------
echo "### Cumulative — add flags one step at a time (bugs flagged out of 12; good.c diagnostics)"
echo
echo "| step | flags added | clang -O2 | gcc -O2 | gcc -O0 | good.c clang \`-I\` | good.c clang \`-isystem\` | good.c gcc \`-isystem -Werror\` |"
echo "|---|---|---|---|---|---|---|---|"
acc=""
j=0
OLDIFS=$IFS
IFS='|'
for s in $STEPS; do
	IFS=$OLDIFS
	j=$((j + 1))
	acc="$acc $s"
	cc -std=c11 -O2 $acc -c bugs12.c -o "$W/b.o" 2>"$W/cum_$j.diag"
	n_c=$(count "$(caught "$W/cum_$j.diag")")
	cc -std=c11 -O2 $acc -I third_party good.c -o "$W/good" 2>"$W/gi_$j.diag"
	cc -std=c11 -O2 $acc -isystem third_party good.c -o "$W/good" 2>"$W/gs_$j.diag"
	n_gi=$(grep -cE '(warning|error):' "$W/gi_$j.diag")
	n_gs=$(grep -cE '(warning|error):' "$W/gs_$j.diag")
	if [ $HAVE_VM = 1 ]; then
		n_g2=$(count "$(caught "$W/g_s${j}_o2.diag")")
		n_g0=$(count "$(caught "$W/g_s${j}_o0.diag")")
		n_gg=$(grep -cE '(warning|error):' "$W/g_s${j}_good.diag")
	else
		n_g2=- n_g0=- n_gg=-
	fi
	echo "| $j | \`$s\` | $n_c/12 | $n_g2/12 | $n_g0/12 | $n_gi | $n_gs | $n_gg |"
	IFS='|'
done
IFS=$OLDIFS
echo

# ---------------- warning names per bug (full baseline) ----------------
echo "### Warning name per bug with the full baseline (solution.flags), clang vs gcc -O2"
echo
echo "| bug | line(s) | clang | gcc |"
echo "|---|---|---|---|"
j=$(echo "$STEPS" | tr '|' '\n' | wc -l | tr -d ' ')
for t in $TAGS; do
	lines=$(grep -n "// $t" bugs12.c | cut -d: -f1 | tr '\n' ',' | sed 's/,$//')
	cn="" gn=""
	for l in $(grep -n "// $t" bugs12.c | cut -d: -f1); do
		[ -z "$cn" ] && cn=$(grep -E "^bugs12.c:$l:[0-9]+: warning:" "$W/cum_$j.diag" | head -1 | grep -oE '\[-W[^]]*\]' | sed "s/^/$l /")
		[ $HAVE_VM = 1 ] && [ -z "$gn" ] && gn=$(grep -E "^bugs12.c:$l:[0-9]+: warning:" "$W/g_s${j}_o2.diag" | head -1 | grep -oE '\[-W[^]]*\]' | sed "s/^/$l /")
	done
	echo "| $t | $lines | ${cn:--} | ${gn:--} |"
done
