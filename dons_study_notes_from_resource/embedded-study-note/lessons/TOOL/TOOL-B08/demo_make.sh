#!/usr/bin/env bash
# TOOL-B08 demos on the Mac (GNU Make 3.81). Read-only on the lesson dir; works in /tmp/TOOL-B08_demo.
#   bash demo_make.sh            -> all sections
#   bash demo_make.sh <n>        -> one section (1..8)
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
L=$(pwd)
D=/tmp/TOOL-B08_demo
MAKE=/usr/bin/make
fresh() {  # fresh <makefile> : new copy of the project with that Makefile
	rm -rf "$D" && mkdir -p "$D" && cp -R "$L/proj/." "$D/" && cp "$L/$1" "$D/Makefile" && cd "$D" || exit 2
}
old() { find src include -exec touch -t 202601010000 {} +; find build -exec touch -t 202601010100 {} +; }
sec() { [ -z "${ONLY:-}" ] || [ "$ONLY" = "$1" ]; }
ONLY=${1:-}

if sec 1; then
	echo "### 1. solution.mk: full build, .d file, -MMD vs -MD"
	fresh solution.mk
	$MAKE
	echo "--- build/main.d"; cat build/main.d
	echo "--- -MD (system headers too) vs -MMD: dependency lines for main.c"
	cc -std=c11 -Iinclude -MD -MF /tmp/TOOL-B08_md.d -c src/main.c -o /tmp/TOOL-B08_md.o
	echo "-MD : $(tr ' ' '\n' < /tmp/TOOL-B08_md.d | grep -c '\.h$') headers, e.g. $(tr ' ' '\n' < /tmp/TOOL-B08_md.d | grep -m1 'stdio.h')"
	echo "-MMD: $(tr ' ' '\n' < build/main.d | grep -c '^include/.*\.h$') headers (first line only)"
	echo "--- second make"; $MAKE
fi

if sec 2; then
	echo "### 2. touch include/dsp.h -> make -n, then make -d (why)"
	fresh solution.mk; $MAKE >/dev/null; old; touch include/dsp.h
	$MAKE -n
	echo "--- make -d | grep newer/must remake"
	$MAKE -d 2>&1 | grep -E "is newer than target|Must remake target" | head -12
fi

if sec 3; then
	echo "### 3. make -p: how make sees the pattern rule and a .d-generated rule"
	fresh solution.mk; $MAKE >/dev/null
	$MAKE -p -q 2>/dev/null | grep -A3 -E '^build/%\.o: src/%\.c' | head -6
	echo "..."
	$MAKE -p -q 2>/dev/null | grep -E '^build/main\.o:' | head -2
	$MAKE -p -q 2>/dev/null | grep -E '^include/hal\.h:' | head -1
	$MAKE -p -q 2>/dev/null | grep -E '^(SRCS|OBJS|DEPFLAGS) :=' | head -3
fi

if sec 4; then
	echo "### 4. starter.mk bugs"
	fresh starter.mk; $MAKE >/dev/null; old
	echo "--- (a) touch include/hal.h; make"; touch include/hal.h; $MAKE
	old
	echo "--- (b) new file in build/ ; make -n"; touch build/notes.txt; $MAKE -n | sed 's/^/  /'
	echo "--- (c) a file named clean; make clean"; touch clean; $MAKE clean; ls -d build
fi

if sec 5; then
	echo "### 5. -MMD without -MP: delete a header that is no longer included"
	fresh solution.mk; sed 's/^DEPFLAGS := -MMD -MP/DEPFLAGS := -MMD/' "$L/solution.mk" > Makefile
	printf '#define FW_VERSION "1.2.3"\n' > include/version.h
	awk '{ print } /#include "hal.h"/ { print "#include \"version.h\"" }' "$L/proj/src/log.c" > src/log.c
	$MAKE >/dev/null; cat build/log.d; old
	cp "$L/proj/src/log.c" src/log.c; rm include/version.h
	$MAKE; echo "exit=$?"
	echo "--- same with -MP (solution.mk)"
	cp "$L/solution.mk" Makefile; printf '#define FW_VERSION "1.2.3"\n' > include/version.h
	awk '{ print } /#include "hal.h"/ { print "#include \"version.h\"" }' "$L/proj/src/log.c" > src/log.c
	$MAKE >/dev/null; cat build/log.d; old
	cp "$L/proj/src/log.c" src/log.c; rm include/version.h
	$MAKE; echo "exit=$?"
fi

if sec 6; then
	echo "### 6. stale object after a header change (starter.mk): FILTER_TAPS 4 -> 16"
	fresh starter.mk; $MAKE >/dev/null; ./build/app | tail -1; old
	sed 's/FILTER_TAPS     4u/FILTER_TAPS     16u/' "$L/proj/include/board.h" > include/board.h
	touch src/filter.c                       # someone also edited filter.c
	$MAKE
	echo "--- run (main.o still thinks mavg_t has 4 taps, filter.o writes 16)"
	./build/app; echo "exit=$?"
	nm -n build/app | grep -E '_g_filt|_g_out'
	lldb -b -o run -k bt -k quit ./build/app </dev/null 2>&1 | grep -E 'stop reason|frame #0|^->'
	echo "--- clean rebuild"
	$MAKE clean >/dev/null; $MAKE >/dev/null; ./build/app | tail -1
fi

if sec 7; then
	echo "### 7. removing a source file does NOT relink (make tracks mtimes, not lists)"
	fresh solution.mk
	printf 'int watchdog_kick(void);\nint watchdog_kick(void) { return 1; }\n' > src/watchdog.c
	$MAKE >/dev/null; nm build/app | grep watchdog
	rm src/watchdog.c
	$MAKE; nm build/app | grep watchdog
	echo "--- make -B (or a link stamp of \$(OBJS)) fixes it"; $MAKE -B >/dev/null; nm build/app | grep -c watchdog
fi

if sec 8; then
	echo "### 8. make 3.81 timestamp resolution: same-second change is invisible"
	rm -rf "$D" && mkdir -p "$D" && cd "$D" || exit 2
	printf 'out: in\n\t@echo rebuilt out\n' > Makefile
	touch in; touch out; touch in       # 'in' is newer by microseconds
	stat -f '%N %Fm' in out
	$MAKE
	touch -t 202601010000 out; $MAKE
fi
