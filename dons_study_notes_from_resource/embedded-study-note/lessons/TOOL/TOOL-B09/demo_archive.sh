#!/usr/bin/env bash
# TOOL-B09 demo: what is inside a .a (ar / ranlib index) -- Mac then Lima VM.
#   bash demo_archive.sh > archive.txt 2>&1
set -u
LESSON=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B09_demo_archive; rm -rf "$D"; mkdir -p "$D"; cd "$D" || exit 1
for f in "$LESSON"/src/a/*.c; do cc -O2 -c "$f"; done
echo '##### Mac: ar rcs + ar t / ar tv'
ar rcs libfoo.a foo_a.o foo_b.o foo_unused.o
ar rcs libbar.a bar_a.o bar_unused.o
ar t libfoo.a
ar tv libfoo.a
echo '##### Mac: nm --print-armap libfoo.a  (Mac nm -s means "segment section", not armap)'
nm --print-armap libfoo.a
echo '##### Mac: archive WITHOUT a table of contents (ar -S), then ranlib'
ar rcS libnoidx.a foo_a.o foo_b.o foo_unused.o
ar t libnoidx.a
cc main.o -L. -lnoidx -lbar -o x; echo "link rc=$?"
ranlib libnoidx.a; ar t libnoidx.a | head -1
cc main.o -L. -lnoidx -lbar -o x; echo "link rc=$?"; ./x

echo '##### Lima VM (GNU ar 2.46)'
limactl shell elinux -- bash -s <<VM
set -u
D=/tmp/TOOL-B09_demo_archive; rm -rf \$D; mkdir -p \$D; cd \$D
for f in $LESSON/src/a/*.c; do gcc -O2 -c \$f; done
ar rcs libfoo.a foo_a.o foo_b.o foo_unused.o; ar rcs libbar.a bar_a.o bar_unused.o
echo '--- ar tv libfoo.a (deterministic mode: uid/gid 0, mtime 0)'
ar tv libfoo.a
echo '--- nm -s libfoo.a (GNU: -s = print archive index)'
nm -s libfoo.a
echo '--- no index (ar rcS), GNU ld.bfd then ld.lld'
ar rcS libnoidx.a foo_a.o foo_b.o foo_unused.o
nm -s libnoidx.a | head -2
gcc -fuse-ld=bfd main.o -L. -lnoidx -lbar -lnoidx -o x; echo "bfd rc=\$?"
gcc -fuse-ld=lld main.o -L. -lnoidx -lbar -lnoidx -o x; echo "lld rc=\$?"
ranlib libnoidx.a
gcc -fuse-ld=bfd main.o -L. -lnoidx -lbar -lnoidx -o x; echo "bfd after ranlib rc=\$?"; ./x
VM
