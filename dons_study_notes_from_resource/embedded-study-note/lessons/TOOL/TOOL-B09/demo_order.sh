#!/usr/bin/env bash
# TOOL-B09 demo: Part A link-order matrix + "which member was pulled and why" evidence.
#   bash demo_order.sh > order.txt 2>&1
set -u
LESSON=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B09_demo_order; rm -rf "$D"; mkdir -p "$D"; cd "$D" || exit 1
for f in "$LESSON"/src/a/*.c; do cc -O2 -c "$f"; done
ar rcs libfoo.a foo_a.o foo_b.o foo_unused.o; ar rcs libbar.a bar_a.o bar_unused.o
echo '##### Mac ld64: order matrix'
for line in "main.o -L. -lfoo" "main.o -L. -lfoo -lbar" "main.o -L. -lbar -lfoo" "-L. -lfoo -lbar main.o" "main.o -L. -lfoo -lbar -lfoo"; do
	echo "== cc $line"
	# shellcheck disable=SC2086
	cc $line -o app 2>&1 && ./app; rm -f app
done
echo '##### Mac ld64: -why_load + -map'
cc main.o -L. -lfoo -lbar -Wl,-why_load -Wl,-map,a.map -o app
sed -n '/^# Object files:/,/^# Sections:/p' a.map
nm app | grep -E ' T _(foo|bar)'
echo '##### Mac ld64: -t lists every member it READ, not what it kept'
cc main.o -L. -lfoo -lbar -Wl,-t -o app | grep -E 'lib(foo|bar)\.a|main\.o'

echo '##### Mac: dependency order with lorder | tsort (member level, then library level)'
lorder main.o foo_a.o foo_b.o foo_unused.o bar_a.o bar_unused.o | grep -v '^\(.*\) \1$'
echo '-- tsort (members):'; lorder main.o foo_a.o foo_b.o foo_unused.o bar_a.o bar_unused.o | tsort | tr '\n' ' '; echo
echo '-- tsort (libraries):'; printf 'main.o libfoo\nlibfoo libbar\nlibbar libfoo\n' | tsort 2>&1 | tr '\n' ' '; echo
echo '##### Lima VM'
limactl shell elinux -- bash -s <<VM
set -u
D=/tmp/TOOL-B09_demo_order; rm -rf \$D; mkdir -p \$D; cd \$D
for f in $LESSON/src/a/*.c; do gcc -O2 -c \$f; done
ar rcs libfoo.a foo_a.o foo_b.o foo_unused.o; ar rcs libbar.a bar_a.o bar_unused.o
for ld in bfd lld; do
	for line in "main.o -L. -lfoo -lbar" "main.o -L. -lbar -lfoo" "main.o -L. -lbar -lfoo -lbar" "-L. -lfoo -lbar main.o" \
		"main.o -L. -lfoo -lbar -lfoo" "main.o -L. -Wl,--start-group -lfoo -lbar -Wl,--end-group"; do
		echo "== [\$ld] gcc \$line"
		gcc -fuse-ld=\$ld \$line -o app 2>&1 && ./app; rm -f app
	done
done
echo '##### bfd: -Map "Archive member included" + -y foo_leaf + --trace'
gcc -fuse-ld=bfd main.o -L. -lfoo -lbar -lfoo -Wl,-Map=a.map -Wl,-y,foo_leaf -Wl,--trace -o app 2>&1 | grep -vE '^/usr/lib|^/lib'
awk '/^Archive member included/{f=1} f&&/^Merging/{exit} f' a.map
echo '##### bfd: --start-group map (same members)'
gcc -fuse-ld=bfd main.o -L. -Wl,--start-group -lfoo -lbar -Wl,--end-group -Wl,-Map=g.map -o app
awk '/^Archive member included/{f=1} f&&/^Merging/{exit} f' g.map
echo '##### bfd: --whole-archive map (unused members pulled too)'
gcc -fuse-ld=bfd main.o -L. -Wl,--whole-archive -lfoo -lbar -Wl,--no-whole-archive -Wl,-Map=w.map -o app
awk '/^Archive member included/{f=1} f&&/^Merging/{exit} f' w.map
nm app | grep -E ' [TR] (foo|bar)_'
echo '##### lld: --why-extract'
gcc -fuse-ld=lld main.o -L. -lbar -lfoo -Wl,--why-extract=- -o app
echo '##### lld: --warn-backrefs (GNU-order lint)'
for line in "-lfoo -lbar" "-lbar -lfoo" "-lfoo -lbar -lfoo" "-lbar -lfoo -lbar"; do
	echo "== lld --warn-backrefs main.o \$line"; gcc -fuse-ld=lld -Wl,--warn-backrefs main.o -L. \$line -o app 2>&1; echo "rc=\$?"
done
echo '##### bfd: back-reference INSIDE one archive is fine (member order m1.o, m2.o; m2 needs m1)'
mkdir -p inner && cd inner
echo 'int leaf(int x){return x+2;}' >m1.c
echo 'int leaf(int); int entry(int x){return leaf(x)*10;}' >m2.c
echo 'int entry(int); int main(void){return entry(5)==70?0:1;}' >main2.c
for f in m1 m2 main2; do gcc -O2 -c \$f.c; done
ar rcs liba.a m1.o m2.o; ar t liba.a | tr '\n' ' '; echo
gcc -fuse-ld=bfd main2.o -L. -la -Wl,-Map=x.map -o x; echo "link rc=\$?"; ./x; echo "run rc=\$?"
awk '/^Archive member included/{f=1} f&&/^Merging/{exit} f' x.map
cd ..
echo '##### gcc itself: the implicit tail of the link line (gcc -v, collect2 args)'
gcc -fuse-ld=bfd -v main.o -L. -lfoo -lbar -lfoo -o app 2>&1 | grep collect2 | tr ' ' '\n' | grep -E '^-l|^--(push|pop|as-needed|no-as-needed)|^-lfoo|crt' | tr '\n' ' '; echo
VM
