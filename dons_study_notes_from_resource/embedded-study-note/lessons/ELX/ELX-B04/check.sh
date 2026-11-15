#!/usr/bin/env bash
# ENV: qemu
# ELX-B04: build /init (PID 1, static C), pack a tiny cpio initramfs with BusyBox, boot QEMU virt, decide.
#   check.sh <starter|solution|practice>   -> PASS/FAIL lines, N/M passed, RESULT, exit 0/1
#   check.sh demo <noinit|dyninit|initexit|noconsole|noconsole_starter>   -> real failure logs
#   check.sh shell                          -> interactive: /init runs /bin/sh on the console (exit = power off)
#   KEEP=1 check.sh ...                     -> keep the work dir (console.log, i.gz, r/) on PASS / demo
# Runs inside the Lima VM. Nothing is loaded into the VM's own kernel; the guest is QEMU TCG.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
IMAGE=/var/lab/out/Image                       # Linux 6.18.55 arm64 defconfig (lab)
BB=${BB:-/var/tmp/ELX-B04_work/bb/busybox}     # from build_busybox.sh ...
[ -x "$BB" ] || BB=/var/lab/build/busybox/busybox   # ... else the lab's identical static build
ROOT_WORK=/tmp/ELX-B04_work

ARG=${1:-}
SCEN=${2:-}
case "$ARG" in
starter|solution|practice) VARIANT=$ARG ;;
demo) VARIANT=solution; [ "$SCEN" = noconsole_starter ] && VARIANT=starter ;;
shell) VARIANT=solution ;;
*) echo "usage: check.sh <starter|solution|practice> | demo <scenario> | shell"; exit 2 ;;
esac

W=$ROOT_WORK/$ARG${SCEN:+_$SCEN}.$$
QPID=
cleanup() { [ -n "$QPID" ] && kill "$QPID" 2>/dev/null; }
trap cleanup EXIT
mkdir -p "$W/r"/{bin,dev,etc,proc,sys,tmp,root}
cp "$SRC"/{starter.c,solution.c,practice.c,selftest.sh,practice.sh} "$W/"

# ---- build: static PID 1 (strict flags) --------------------------------
if ! gcc -std=c11 -Wall -Wextra -Werror -O2 -static "$W/$VARIANT.c" -o "$W/init" 2>"$W/build.log"; then
	cat "$W/build.log"; echo "FAIL: build"; echo "RESULT: FAIL ($ARG, build)"; exit 1
fi

# ---- rootfs: /init + BusyBox + applet links + scripts ------------------
R=$W/r
cp "$BB" "$R/bin/busybox"
for a in $("$BB" --list); do ln -s busybox "$R/bin/$a"; done
ln -s bin "$R/sbin"
cp "$W/selftest.sh" "$W/practice.sh" "$R/etc/"
cp "$W/init" "$R/init"
APPEND="console=ttyAMA0 quiet panic=-1 rdinit=/init"
case "$ARG" in
starter|solution) APPEND="$APPEND B04_SCRIPT=/etc/selftest.sh -- b04arg" ;;
practice)         APPEND="$APPEND B04_SCRIPT=/etc/practice.sh b04.dot=1 -- b04arg" ;;
shell)            APPEND="$APPEND -- b04arg" ;;
demo)
	APPEND="console=ttyAMA0 panic=-1 rdinit=/init B04_SCRIPT=/etc/selftest.sh -- b04arg"
	case "$SCEN" in
	noinit)   rm "$R/init" ;;                                  # no /init at all
	dyninit)  rm -rf "$R"/*                                    # /init = dynamic ELF, nothing else
	          gcc -std=c11 -O2 "$W/solution.c" -o "$R/init" ;;
	initexit) APPEND="console=ttyAMA0 panic=-1 rdinit=/bin/true" ;;   # PID 1 exits with 0
	noconsole|noconsole_starter) ln -s /nonexistent "$R/dev/console" ;;  # replaces the built-in node
	*) echo "unknown scenario '$SCEN'"; exit 2 ;;
	esac ;;
esac
# newc = the only cpio format the kernel unpacks; -R 0:0 = files owned by root in the guest
(cd "$R" && find . | cpio -o -H newc -R 0:0 --quiet | gzip -9) >"$W/i.gz"

QEMU=(qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic -no-reboot
	-kernel "$IMAGE" -initrd "$W/i.gz" -append "$APPEND")
if [ "$ARG" = shell ]; then
	echo "initramfs $(stat -c %s "$W/i.gz") bytes; Ctrl-a x quits QEMU, 'exit' in the shell powers off"
	"${QEMU[@]}"
	rm -rf "$W"
	exit 0
fi

LOG=$W/console.log
timeout 150 "${QEMU[@]}" </dev/null >"$LOG" 2>&1 &
QPID=$!
wait "$QPID"
QRC=$?
QPID=

if [ "$ARG" = demo ]; then
	echo "=== demo $SCEN: initramfs $(stat -c %s "$W/i.gz") bytes, -append \"$APPEND\""
	grep -vE '^\s*$' "$LOG" | grep -A14 -E 'Freeing unused kernel memory|Run /|Warning: unable|check access|Failed to execute|B04 init' | head -60
	echo "=== qemu exit $QRC (124 = timeout)"
	if [ "${KEEP:-0}" = 1 ]; then echo "kept: $LOG"; else rm -rf "$W"; fi
	exit 0
fi

# ---- verdict: counts + exact strings, never timing ----------------------
tr -d '\r' <"$LOG" >"$LOG.txt"
grep -E '^(PASS:|FAIL:|SCRIPT:|B04 init:|[0-9]+/[0-9]+ passed)' "$LOG.txt"
grep -E 'Kernel panic|Attempted to kill init|No working init|unable to open an initial console' "$LOG.txt" | head -3
P=$(grep -c '^PASS:' "$LOG.txt")
F=$(grep -c '^FAIL:' "$LOG.txt")
case "$VARIANT" in practice) EXPECT=36 ;; *) EXPECT=27 ;; esac
OK=1
[ "$F" -eq 0 ] && [ "$P" -eq "$EXPECT" ] || OK=0
grep -q '^INIT_DONE' "$LOG.txt" || { OK=0; echo "no INIT_DONE (qemu rc=$QRC)"; }
grep -q 'reboot: Power down' "$LOG.txt" || { OK=0; echo "no clean 'reboot: Power down'"; }
grep -q 'Kernel panic' "$LOG.txt" && { OK=0; echo "kernel panicked"; }
echo "$P/$EXPECT passed (FAIL lines: $F)"
if [ "$OK" = 1 ]; then
	echo "RESULT: PASS ($ARG)"
	if [ "${KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi
	exit 0
fi
echo "RESULT: FAIL ($ARG) log: $LOG"
exit 1
