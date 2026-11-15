#!/bin/sh
# ENV: qemu
# ELX-B09 guest test (BusyBox sh): echo/cat cases on the thsense attributes.
#   sysfs_test.sh <module-name> <basic|practice>
# Prints PASS:/FAIL: lines; check.sh counts them.
MOD=$1
LEVEL=${2:-basic}
D=/sys/bus/platform/devices/thsense
DRV=/sys/bus/platform/drivers/thsense
P=0; F=0
ok()  { echo "PASS: $1"; P=$((P + 1)); }
bad() { echo "FAIL: $1"; F=$((F + 1)); }
check() { n=$1; shift; if "$@"; then ok "$n"; else bad "$n"; fi; }
eq() { [ "$1" = "$2" ] || { echo "  got '$1' want '$2'"; return 1; }; }
has() { case "$1" in *"$2"*) return 0 ;; esac; echo "  got '$1'"; return 1; }

# w FILE STRING: "echo STRING > FILE" (echo adds '\n'); sets RC, ERR
w() { ERR=$( (echo "$2" > "$D/$1") 2>&1 ); RC=$?; }
# wr FILE STRING: raw bytes, no newline
wr() { ERR=$( (printf '%s' "$2" > "$D/$1") 2>&1 ); RC=$?; }
rd() { cat "$D/$1" 2>&1; }

# bad_input NAME STRING: must fail with EINVAL and leave threshold alone
bad_input() {
	before=$(rd threshold)
	w threshold "$2"
	echo "  [$1] rc=$RC err: $ERR"
	check "reject $1: rc!=0" [ "$RC" -ne 0 ]
	check "reject $1: Invalid argument" has "$ERR" "Invalid argument"
	check "reject $1: threshold unchanged" eq "$(rd threshold)" "$before"
}

# ---------------- A: files, modes, defaults, normal writes ----------------
check "device dir exists" [ -d $D ]
check "driver bound (driver link)" [ -e $D/driver ]
check "threshold mode 644" eq "$(stat -c %a $D/threshold)" 644
check "count mode 444" eq "$(stat -c %a $D/count)" 444
check "sample mode 200" eq "$(stat -c %a $D/sample)" 200
check "default threshold 500" eq "$(rd threshold)" 500
check "default count 0" eq "$(rd count)" 0
w threshold 42
check "echo 42: rc 0" [ "$RC" -eq 0 ]
check "cat after 42" eq "$(rd threshold)" 42
check "show ends with newline" eq "$(od -An -c $D/threshold | tr -d ' ')" '42\n'
for pair in 1000:1000 +7:7 007:7 -0:0; do
	v=${pair%%:*}; want=${pair#*:}
	w threshold "$v"
	check "accept '$v' -> $want" eq "$RC:$(rd threshold)" "0:$want"
done
wr threshold 17
check "no trailing newline ok" eq "$RC:$(rd threshold)" "0:17"
w threshold 42

# ---------------- B: bad input ----------------
bad_input abc abc
bad_input 1001 1001
bad_input -1 -1
bad_input "spaces" "  42"
bad_input 0x10 0x10
bad_input empty ""
bad_input overflow 99999999999
bad_input 42abc 42abc
bad_input "4 2" "4 2"
ERR=$( (printf '42\n\n' > $D/threshold) 2>&1 ); RC=$?
check "reject two newlines" [ "$RC" -ne 0 ]
Z=$(/systest zero $D/threshold)
echo "  [zero-length write] $Z"
check "zero-length write returns 0" eq "$Z" "ZERO_RET=0"
check "zero-length write keeps 42" eq "$(rd threshold)" 42
ERR=$( (echo 1 > $D/count) 2>&1 ); RC=$?
echo "  [write count] rc=$RC err: $ERR"
check "count is read-only (EACCES)" has "$ERR" "Permission denied"
ERR=$(cat $D/sample 2>&1); RC=$?
echo "  [read sample] rc=$RC err: $ERR"
check "sample is write-only (EACCES)" has "$ERR" "Permission denied"
bad_input_sample() {
	ERR=$( (echo "$1" > $D/sample) 2>&1 ); RC=$?
	check "sample rejects '$1'" has "$ERR" "Invalid argument"
}
bad_input_sample 1001
bad_input_sample x

# ---------------- C: crossings + notify ----------------
# level is still 0 (no sample yet), so threshold "-0" above was already an edge
c0=$(rd count)
echo "  [count before C] $c0"
check "threshold 0 with level 0 counted as edges" eq "$c0" 1
w threshold 500
# sample:delta  (rising edge = level goes from < thr to >= thr)
for step in 100:0 600:1 700:1 499:1 500:2 1000:2 0:2 999:3; do
	s=${step%%:*}; want=$((c0 + ${step#*:}))
	w sample "$s"
	check "sample $s -> count $want" eq "$RC:$(rd count)" "0:$want"
done
w sample 100
w threshold 50
check "lowering threshold below level is a rising edge" eq "$(rd count)" $((c0 + 4))
w threshold 50
check "same threshold again: no new edge" eq "$(rd count)" $((c0 + 4))
w threshold 1000
w threshold 100
check "threshold 1000 -> 100 with level 100: edge" eq "$(rd count)" $((c0 + 5))
/systest poll $D || true

# ---------------- unbind / bind: dev_groups lifetime ----------------
echo thsense > $DRV/unbind
check "unbind: device dir stays" [ -d /sys/devices/platform/thsense ]
check "unbind: threshold gone" [ ! -e /sys/devices/platform/thsense/threshold ]
echo thsense > $DRV/bind
check "bind: threshold back at default" eq "$(rd threshold)" 500
check "bind: count reset" eq "$(rd count)" 0

# ---------------- practice extras ----------------
if [ "$LEVEL" = practice ]; then
	K=/sys/kernel/thsense_info
	# P1 hysteresis
	check "P1 hysteresis default 0" eq "$(rd hysteresis)" 0
	w hysteresis 101
	check "P1 hysteresis 101 rejected" has "$ERR" "Invalid argument"
	w threshold 500; w hysteresis 50; w sample 0
	for step in 600:1 480:1 600:1 450:1 449:1 500:2; do
		s=${step%%:*}; want=${step#*:}
		w sample "$s"
		check "P1 hyst=50 sample $s -> count $want" eq "$(rd count)" "$want"
	done
	w hysteresis 0
	# P2 edge (enum with [selected])
	check "P2 edge default" eq "$(rd edge)" "[rising] falling both"
	w edge both
	check "P2 edge both" eq "$RC:$(rd edge)" "0:rising falling [both]"
	wr edge falling
	check "P2 edge falling (no newline)" eq "$RC:$(rd edge)" "0:rising [falling] both"
	w edge bot
	check "P2 edge 'bot' rejected" has "$ERR" "Invalid argument"
	w edge "both "
	check "P2 edge 'both ' rejected" has "$ERR" "Invalid argument"
	w edge both
	w sample 0; c0=$(rd count); w sample 900; w sample 0
	check "P2 both: up+down = +2" eq "$(rd count)" $((c0 + 2))
	w edge falling
	c0=$(rd count); w sample 900; w sample 0
	check "P2 falling: up+down = +1" eq "$(rd count)" $((c0 + 1))
	w edge rising
	# P3 reset
	w reset 1
	check "P3 reset 1 -> count 0" eq "$RC:$(rd count)" "0:0"
	w reset yes
	check "P3 reset 'yes' rejected" has "$ERR" "Invalid argument"
	w reset 0
	check "P3 reset '0' rejected" has "$ERR" "Invalid argument"
	ERR=$(cat $D/reset 2>&1)
	check "P3 reset is write-only" has "$ERR" "Permission denied"
	# P4 raw kobject under /sys/kernel
	check "P4 kobject dir" [ -d $K ]
	check "P4 abi_version" eq "$(cat $K/abi_version 2>&1)" 1
	check "P4 abi_version mode 444" eq "$(stat -c %a $K/abi_version)" 444
	check "P4 kobject is not a device (no uevent file)" [ ! -e $K/uevent ]
fi

# ---------------- module removal ----------------
rmmod "$MOD"
check "rmmod: device gone" [ ! -e $D ]
[ "$LEVEL" = practice ] && check "P4 rmmod: kobject gone" [ ! -e /sys/kernel/thsense_info ]
echo "sysfs_test: $P/$((P + F)) passed"
