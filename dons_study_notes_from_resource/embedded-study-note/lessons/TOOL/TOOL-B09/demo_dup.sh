#!/usr/bin/env bash
# TOOL-B09 demo: Part B -- the same symbol in two archives. First archive that is
# searched while the symbol is undefined wins, silently. Same-member trap at the end.
#   bash demo_dup.sh > dup.txt 2>&1
set -u
LESSON=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B09_demo_dup; rm -rf "$D"; mkdir -p "$D"; cd "$D" || exit 1
cp "$LESSON"/src/b/* .
for f in crc_fast crc_ref crc_vectors main_b; do cc -O2 -c $f.c; done
ar rcs libcrc_fast.a crc_fast.o; ar rcs libcrc_ref.a crc_ref.o crc_vectors.o
cat crc_ref.c crc_vectors.c >crc_ref_mono.c; cc -O2 -c crc_ref_mono.c; ar rcs libcrc_mono.a crc_ref_mono.o
echo '##### Mac ld64'
for line in "main_b.o -L. -lcrc_ref -lcrc_fast" "main_b.o -L. -lcrc_fast -lcrc_ref" "-L. -lcrc_ref -lcrc_fast main_b.o"; do
	echo "== cc $line"
	# shellcheck disable=SC2086
	cc $line -Wl,-why_load -o app 2>&1 && ./app; rm -f app
done
echo '== cc main_b.o -L. -lcrc_fast -lcrc_mono   (vectors live in the SAME member as ref crc32)'
cc main_b.o -L. -lcrc_fast -lcrc_mono -o app 2>&1; echo "rc=$?"

echo '##### Lima VM'
limactl shell elinux -- bash -s <<VM
set -u
D=/tmp/TOOL-B09_demo_dup; rm -rf \$D; mkdir -p \$D; cd \$D
cp $LESSON/src/b/* .
for f in crc_fast crc_ref crc_vectors main_b; do gcc -O2 -c \$f.c; done
ar rcs libcrc_fast.a crc_fast.o; ar rcs libcrc_ref.a crc_ref.o crc_vectors.o
cat crc_ref.c crc_vectors.c >crc_ref_mono.c; gcc -O2 -c crc_ref_mono.c; ar rcs libcrc_mono.a crc_ref_mono.o
for ld in bfd lld; do
	for line in "main_b.o -L. -lcrc_ref -lcrc_fast" "main_b.o -L. -lcrc_fast -lcrc_ref" "-L. -lcrc_ref -lcrc_fast main_b.o"; do
		echo "== [\$ld] gcc \$line"
		gcc -fuse-ld=\$ld \$line -o app 2>&1 | head -3; [ -x app ] && ./app; rm -f app
	done
done
echo '== [bfd] -Map of the fixed line'
gcc -fuse-ld=bfd main_b.o -L. -lcrc_fast -lcrc_ref -Wl,-Map=b.map -o app
awk '/^Archive member included/{f=1} f&&/^(Merging|As-needed)/{exit} f' b.map
for ld in bfd lld; do
	echo "== [\$ld] main_b.o -L. -lcrc_fast -lcrc_mono"
	gcc -fuse-ld=\$ld main_b.o -L. -lcrc_fast -lcrc_mono -o app 2>&1; echo "rc=\$?"
done
echo '== [bfd] --whole-archive both'
gcc -fuse-ld=bfd main_b.o -L. -Wl,--whole-archive -lcrc_fast -lcrc_ref -Wl,--no-whole-archive -o app 2>&1; echo "rc=\$?"
VM
