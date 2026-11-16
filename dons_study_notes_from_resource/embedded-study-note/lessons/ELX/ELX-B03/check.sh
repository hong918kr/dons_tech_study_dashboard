#!/usr/bin/env bash
# ENV: linux  (runs inside the Lima VM; the lab kernel boots in a QEMU guest)
# ELX-B03 check.sh <starter|solution|practice>
#   1. self-test of <variant>.py on fixtures/        (python3 -I, "N/M passed")
#   2. live Kconfig: defconfig -> + req_*.config -> olddefconfig  (O= dir, lab source read-only)
#      compare <variant>.py with the real scripts/diffconfig, and the "did it stick?" verdicts
#   3. QEMU guest (lab out/Image): /proc/config.gz == build-tree .config, vermagic, insmod, y/m
# Verdict = counts + exact text compares only. Prints "N/M passed" and "RESULT: PASS|FAIL (...)".
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KSRC=/var/lab/src/linux-6.18.55
LABB=/var/lab/out/linux-build
IMAGE=/var/lab/out/Image
ROOTFS=/var/lab/build/rootfs
KO=$LABB/drivers/gpio/gpio-aggregator.ko

V=${1:-}
case "$V" in
starter|solution|practice) ;;
*) echo "usage: check.sh <starter|solution|practice>"; exit 2 ;;
esac
W=/tmp/ELX-B03_work/$V.$$
mkdir -p "$W"
QPID=
cleanup() { [ -n "$QPID" ] && kill "$QPID" 2>/dev/null; wait 2>/dev/null; }
trap cleanup EXIT
cp -r "$SRC"/starter.py "$SRC"/solution.py "$SRC"/practice.py "$SRC"/req_*.config \
	"$SRC"/fixtures "$W/"
PY="timeout 60 python3 -I $W/$V.py"
P=0
F=0
pass() { echo "PASS: $*"; P=$((P + 1)); }
fail() { echo "FAIL: $*"; F=$((F + 1)); }
same() { # same <label> <file-expected> <file-got>
	if cmp -s "$2" "$3"; then pass "$1"; else fail "$1"; diff "$2" "$3" | head -5; fi
}

# ---- 1. self-test ---------------------------------------------------------
$PY >"$W/self.log" 2>&1
grep -E '^(PASS|FAIL):' "$W/self.log" | sed 's/^\(PASS\|FAIL\): /\1: self /'
P=$((P + $(grep -c '^PASS:' "$W/self.log")))
F=$((F + $(grep -c '^FAIL:' "$W/self.log")))
grep -q '^Traceback' "$W/self.log" && { fail "self-test crashed"; tail -3 "$W/self.log"; }

# ---- 2. live Kconfig on the real 6.18.55 tree (O= only) -------------------
O=$W/o
(cd "$KSRC" && make -s O="$O" defconfig) >"$W/kconfig.log" 2>&1 || fail "make defconfig"
cp "$O/.config" "$W/def.config"
find "$KSRC" -name 'Kconfig*' -type f -print0 | xargs -0 grep -hoE \
	'^[[:space:]]*(menu)?config[[:space:]]+[A-Za-z0-9_]+' | awk '{print $2}' | sort -u >"$W/known.txt"

exp_stuck() {
	case "$1" in
	naive) echo "NOT STUCK DEBUG_INFO wanted=n got=y reason=forced" ;;
	good) ;;
	bringup)
		echo "NOT STUCK GPIO_SIMULATOR wanted=m got=(absent) reason=unknown"
		echo "NOT STUCK I2C_STUB wanted=y got=m reason=capped"
		echo "NOT STUCK SERIAL_AMBA_PL011_CONSOLE wanted=y got=(absent) reason=dropped"
		;;
	esac
}
for f in naive good bringup; do
	cat "$W/def.config" "$W/req_$f.config" >"$O/.config"
	(cd "$KSRC" && make -s O="$O" olddefconfig) >>"$W/kconfig.log" 2>&1 || fail "olddefconfig $f"
	cp "$O/.config" "$W/final_$f.config"
	"$KSRC/scripts/diffconfig" "$W/def.config" "$W/final_$f.config" >"$W/real_$f.txt"
	$PY diff "$W/def.config" "$W/final_$f.config" >"$W/mine_$f.txt" 2>&1
	same "live diffconfig $f ($(wc -l <"$W/real_$f.txt") lines) == scripts/diffconfig" \
		"$W/real_$f.txt" "$W/mine_$f.txt"
	"$KSRC/scripts/diffconfig" -m "$W/def.config" "$W/final_$f.config" >"$W/realm_$f.txt"
	$PY diff "$W/def.config" "$W/final_$f.config" -m >"$W/minem_$f.txt" 2>&1
	same "live diffconfig -m $f == scripts/diffconfig -m" "$W/realm_$f.txt" "$W/minem_$f.txt"
	exp_stuck $f >"$W/exp_stuck_$f.txt"
	$PY stuck "$W/req_$f.config" "$W/final_$f.config" "$W/known.txt" >"$W/stuck_$f.txt" 2>&1
	same "live stuck-check $f ($(wc -l <"$W/exp_stuck_$f.txt") not stuck)" \
		"$W/exp_stuck_$f.txt" "$W/stuck_$f.txt"
done
grep 'override: reassigning' "$W/kconfig.log" | head -3

# ---- 3. QEMU guest: the lab kernel's own view ------------------------------
R=$W/rootfs
cp -a "$ROOTFS"/. "$R/"
cp "$KO" "$R/"
cat >"$R/test.sh" <<'G'
#!/bin/sh
echo "UNAME=$(uname -r)"
echo "CFGMD5=$(zcat /proc/config.gz | md5sum | cut -d' ' -f1)"
echo "===CFG BEGIN==="
zcat /proc/config.gz
echo "===CFG END==="
insmod /gpio-aggregator.ko && echo "INSMOD=ok" || echo "INSMOD=fail"
grep -q '^gpio_aggregator ' /proc/modules && echo "LOADED=yes" || echo "LOADED=no"
echo GUEST_DONE
G
chmod +x "$R/test.sh"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) >"$W/i.gz"
LOG=$W/console.log
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
	-kernel "$IMAGE" -initrd "$W/i.gz" \
	-append "console=ttyAMA0 loglevel=1 LAB_CMD=/test.sh" </dev/null >"$LOG" 2>&1 &
QPID=$!
wait "$QPID"
QPID=
tr -d '\r' <"$LOG" >"$W/console.txt"
UN=$(sed -n 's/^UNAME=//p' "$W/console.txt")
sed -n '/^===CFG BEGIN===$/,/^===CFG END===$/p' "$W/console.txt" | sed '1d;$d' >"$W/guest.config"
[ "$(sed -n 's/^CFGMD5=//p' "$W/console.txt")" = "$(md5sum <"$W/guest.config" | cut -d' ' -f1)" ] &&
	[ "$UN" = "6.18.55" ] && pass "guest booted ($UN), /proc/config.gz captured intact" ||
	fail "guest boot / config capture (uname='$UN')"
NSYM=$(grep -cE '^CONFIG_[A-Za-z0-9_]+=|^# CONFIG_[A-Za-z0-9_]+ is not set$' "$LABB/.config")
$PY diff "$LABB/.config" "$W/guest.config" >"$W/guest_vs_build.txt" 2>&1
$PY diff /dev/null "$W/guest.config" >"$W/guest_all.txt" 2>&1
[ ! -s "$W/guest_vs_build.txt" ] && [ "$(grep -c '^+' "$W/guest_all.txt")" = "$NSYM" ] &&
	pass "guest /proc/config.gz == $LABB/.config ($NSYM symbols, 0 diff lines)" ||
	fail "guest config vs build tree ($(wc -l <"$W/guest_vs_build.txt") diff lines)"
VM_KO=$(modinfo -F vermagic "$KO")
VM_PY=$($PY vermagic "$W/guest.config" "$UN" 2>&1)
[ "$VM_KO" = "$VM_PY" ] && pass "vermagic from guest .config == modinfo ($VM_KO)" ||
	fail "vermagic: modinfo='$VM_KO' predicted='$VM_PY'"
grep -q '^INSMOD=ok' "$W/console.txt" && grep -q '^LOADED=yes' "$W/console.txt" &&
	pass "guest insmod gpio-aggregator.ko (intree=$(modinfo -F intree "$KO"))" ||
	fail "guest insmod gpio-aggregator.ko"
for m in "gpio-sim GPIO_SIM y" "gpio_aggregator GPIO_AGGREGATOR m" "configs IKCONFIG y" \
	"gpio-mockup GPIO_MOCKUP n"; do
	set -- $m
	K=$($PY kind "$1" "$LABB" 2>&1)
	C=$(grep -E "^CONFIG_$2=|^# CONFIG_$2 is not set" "$W/guest.config" | sed 's/.*=//;s/.* is not set/n/')
	[ "$K" = "$3" ] && [ "$C" = "$3" ] && pass "kind $1 = $K (CONFIG_$2=$C)" ||
		fail "kind $1: got '$K' config '$C' want $3"
done

# ---- practice extras --------------------------------------------------------
if [ "$V" = practice ]; then
	# P1/P2 against the real merge_config.sh (run from inside the O= dir: it mktemps in cwd)
	for f in naive good bringup; do
		(cd "$O" && "$KSRC/scripts/kconfig/merge_config.sh" -O "$O" "$W/def.config" \
			"$W/req_$f.config") >"$W/merge_$f.log" 2>&1 || fail "merge_config.sh $f"
		sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)=.*/\1/p;s/^# \(CONFIG_[A-Za-z0-9_]*\) is not set$/\1/p' \
			"$W/req_$f.config" | sed 's/^CONFIG_//' | sort >"$W/frag_$f.txt"
		sed -n 's/^Value requested for CONFIG_\(.*\) not in final .config$/\1/p' "$W/merge_$f.log" |
			sort | comm -12 - "$W/frag_$f.txt" >"$W/mc_strict_$f.txt"
		$PY stuck --strict "$W/req_$f.config" "$O/.config" | awk '{print $3}' | sort >"$W/my_strict_$f.txt"
		same "P1 strict == merge_config.sh warnings for $f ($(wc -l <"$W/mc_strict_$f.txt"))" \
			"$W/mc_strict_$f.txt" "$W/my_strict_$f.txt"
		sed -n 's/^Value of CONFIG_\(.*\) is redefined by fragment.*/\1/p' "$W/merge_$f.log" |
			sort >"$W/mc_redef_$f.txt"
		$PY redefined "$W/def.config" "$W/req_$f.config" | sort >"$W/my_redef_$f.txt"
		same "P2 redefined == merge_config.sh for $f ($(wc -l <"$W/mc_redef_$f.txt"))" \
			"$W/mc_redef_$f.txt" "$W/my_redef_$f.txt"
	done
	# P3 counts vs grep
	G="y=$(grep -c '^CONFIG_.*=y$' "$W/def.config") m=$(grep -c '^CONFIG_.*=m$' "$W/def.config")"
	G="$G n=$(grep -c '^# CONFIG_.* is not set$' "$W/def.config")"
	G="$G other=$(grep '^CONFIG_' "$W/def.config" | grep -vc '=[ym]$')"
	M=$($PY count "$W/def.config" 2>&1)
	[ "$G" = "$M" ] && pass "P3 count defconfig: $M" || fail "P3 count: grep '$G' mine '$M'"
	# P4 vermagic explain: lab module vs the running guest kernel -> nothing to explain
	E=$($PY explain "$VM_KO" "$VM_PY" 2>&1)
	E2=$($PY explain "$VM_KO" "${VM_PY/6.18.55/6.18.55-sub}" 2>&1)
	[ "$E" = "OK" ] && [ "$E2" = "release: module 6.18.55 != kernel 6.18.55-sub" ] &&
		pass "P4 explain vermagic (match / release mismatch)" || fail "P4 explain: '$E' / '$E2'"
	# P5 extract the embedded config from the Image file itself
	"$KSRC/scripts/extract-ikconfig" "$IMAGE" >"$W/ik_real.config" 2>/dev/null
	$PY ikconfig "$IMAGE" >"$W/ik_mine.config" 2>&1
	same "P5 ikconfig(Image) == scripts/extract-ikconfig ($(wc -l <"$W/ik_real.config") lines)" \
		"$W/ik_real.config" "$W/ik_mine.config"
fi

T=$((P + F))
echo "$P/$T passed"
if [ "$F" -eq 0 ] && [ "$P" -gt 0 ] && grep -q '^GUEST_DONE' "$W/console.txt"; then
	echo "RESULT: PASS ($V)"
	rm -rf "$W"
	exit 0
fi
echo "RESULT: FAIL ($V) work dir: $W"
exit 1
