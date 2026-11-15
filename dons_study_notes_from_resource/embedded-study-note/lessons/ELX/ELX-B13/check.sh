#!/usr/bin/env bash
# ENV: linux
# ELX-B13 check.sh <starter|solution|practice>
#   1) builds esn_sensord from <variant>.c (gcc -Werror) and runs its --selftest (fake PID 1, no systemd)
#   2) drives the VM's REAL systemd with runtime units only:
#        /run/systemd/system/esn-b13-sensord-<variant>-<pid>.service  (+ esn-b13-dep-..., + .d/ for practice)
#      they are stopped, reset, removed and daemon-reload'ed on exit (trap). No other unit is touched.
# Run from the Mac:  limactl shell elinux -- bash <lesson dir>/check.sh solution
#   SHOWJ=1 -> also print this run's journal at the end; KEEP=1 -> keep the work dir on FAIL
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: check.sh <starter|solution|practice>"; exit 2 ;; esac

W=/tmp/ELX-B13_work/$V.$$
TAG=$V-$$
U=esn-b13-sensord-$TAG.service
D=esn-b13-dep-$TAG.service
RUN=/run/systemd/system
PASS=0 TOTAL=0 OK=0 T0=$(date +%s)

pass() { TOTAL=$((TOTAL + 1)); PASS=$((PASS + 1)); echo "PASS: $*"; }
fail() { TOTAL=$((TOTAL + 1)); echo "FAIL: $*"; }
ck() { local d=$1; shift; if "$@"; then pass "$d"; else fail "$d"; fi; }
show() { systemctl show -p "$2" --value "$1"; }
jr() { journalctl -u "$U" --since "@$T0" --no-pager -o cat 2>/dev/null; }
jgrep() { # journald may lag a little: wait up to 5 s for a fixed-string line of THIS run's unit
	local i
	for i in $(seq 1 25); do jr | grep -qF -- "$1" && return 0; sleep 0.2; done
	return 1
}
wait_for() { # wait_for <seconds> <cmd...>   (polls every 0.2 s)
	local i n=$(($1 * 5)); shift
	for i in $(seq 1 "$n"); do "$@" && return 0; sleep 0.2; done
	return 1
}
eq() { [ "$1" = "$2" ]; }

cleanup() {
	sudo systemctl stop "$U" "$D" >/dev/null 2>&1
	sudo systemctl reset-failed "$U" "$D" >/dev/null 2>&1
	sudo rm -rf "$RUN/$U" "$RUN/$U.d" "$RUN/$D"
	sudo systemctl daemon-reload
	if [ "$OK" = 1 ] || [ "${KEEP:-0}" = 0 ]; then rm -rf "$W"; else echo "(work dir kept: $W)"; fi
	rmdir /tmp/ELX-B13_work 2>/dev/null
}
trap cleanup EXIT
trap 'exit 130' INT TERM

finish() {
	if [ "${SHOWJ:-0}" = 1 ]; then # SHOWJ=1 check.sh ...: dump this run's journal (used for the md)
		echo "---- journalctl -u $U -o short-monotonic ----"
		journalctl -u "$U" --since "@$T0" --no-pager -o short-monotonic
	fi
	echo "$PASS/$TOTAL passed"
	if [ "$PASS" = "$TOTAL" ]; then OK=1; echo "RESULT: PASS ($V)"; exit 0; fi
	echo "RESULT: FAIL ($V, $((TOTAL - PASS)) failed)"; exit 1
}

# ---- 1. build + selftest ------------------------------------------------
mkdir -p "$W" && chmod 755 "$W"
if gcc -std=c11 -Wall -Wextra -Werror -O2 "$SRC/$V.c" -o "$W/esn_sensord" 2>"$W/build.log"; then
	pass "build: gcc -std=c11 -Wall -Wextra -Werror -O2 $V.c"
else
	cat "$W/build.log"; fail "build"; finish
fi
timeout 60 "$W/esn_sensord" --selftest >"$W/selftest.txt" 2>&1
rc=$?
grep -E '^FAIL' "$W/selftest.txt" | sed 's#^FAIL [^ ]*/#selftest FAIL #'
ck "selftest (fake PID 1): $(tail -1 "$W/selftest.txt")" eq "$rc" 0

# ---- 2. install runtime units -------------------------------------------
echo 23500 >"$W/temp1_input"
sed -e "s#@W@#$W#g" -e "s#@TAG@#$TAG#g" "$SRC/esn-b13-sensord.service" | sudo tee "$RUN/$U" >/dev/null
sed -e "s#@TAG@#$TAG#g" "$SRC/esn-b13-dep.service" | sudo tee "$RUN/$D" >/dev/null
if [ "$V" = practice ]; then
	sudo mkdir -p "$RUN/$U.d"
	sed -e "s#@TAG@#$TAG#g" "$SRC/practice.conf" | sudo tee "$RUN/$U.d/practice.conf" >/dev/null
fi
sudo systemctl daemon-reload
va=$(systemd-analyze verify "$RUN/$U" "$RUN/$D" 2>&1); rc=$?
ck "systemd-analyze verify: no complaint about our units" eval '[ $rc = 0 ] && ! grep -q esn-b13 <<<"$va"'
echo "units: $U (Wants/After $D)"

# ---- 3. start: Type=notify waits for READY=1 ----------------------------
t_start=$(date +%s.%N)
timeout 40 sudo systemctl start "$U"; rc=$?
t_end=$(date +%s.%N)
echo "systemctl start rc=$rc took $(awk -v a="$t_start" -v b="$t_end" 'BEGIN{printf "%.2f", b-a}') s"
ck "systemctl start returns 0 (READY=1 arrived before TimeoutStartSec=4)" eq "$rc" 0
if [ "$rc" != 0 ]; then
	echo "---- journal (why the start failed) ----"
	jr | head -12
	echo "---- systemctl show ----"
	systemctl show "$U" -p ActiveState -p SubState -p Result -p NRestarts | paste -sd' '
	fail "remaining systemd checks skipped: unit never became active"
	finish
fi
P1=$(show "$U" MainPID)
ck "ActiveState=active SubState=running" eq "$(show "$U" ActiveState)/$(show "$U" SubState)" active/running
ck "Type=notify WatchdogUSec=5s" eq "$(show "$U" Type)/$(show "$U" WatchdogUSec)" notify/5s
ck "Wants= pulled in $D (we never started it)" eq "$(show "$D" ActiveState)" active
a=$(show "$D" ActiveEnterTimestampMonotonic); b=$(show "$U" ExecMainStartTimestampMonotonic)
echo "dep active at ${a} us, daemon exec at ${b} us (monotonic)"
ck "After= ordering: dep active before daemon exec" eval '[ "$a" -gt 0 ] && [ "$a" -le "$b" ]'
envs=$(sudo cat "/proc/$P1/environ" | tr '\0' '\n' | grep -E '^(NOTIFY_SOCKET|WATCHDOG_USEC|WATCHDOG_PID)=' | sort)
echo "$envs" | sed 's/^/  env: /'
ck "env: NOTIFY_SOCKET, WATCHDOG_USEC=5000000, WATCHDOG_PID=MainPID" eq "$envs" \
	"$(printf 'NOTIFY_SOCKET=/run/systemd/notify\nWATCHDOG_PID=%s\nWATCHDOG_USEC=5000000' "$P1")"

if [ "$V" = practice ]; then
	# ---- practice P2: 6 s calibration under TimeoutStartSec=4 ----------
	ck "P2: journal shows 6000 ms calibration, no start timeout" eval \
		'jgrep "calibration done after 6000 ms" && ! jr | grep -q "start operation timed out"'
	# ---- practice P5: helper pid sends STATUS=, NotifyAccess=main drops it
	seen=0
	for i in $(seq 1 10); do case $(show "$U" StatusText) in helper*) seen=1 ;; esac; sleep 0.2; done
	ck "P5: NotifyAccess=main ignores STATUS= from the helper pid (StatusText=$(show "$U" StatusText))" eq "$seen" 0
	# ---- practice P3: WATCHDOG=trigger --------------------------------
	touch "$W/hang.fault"
	ck "P3: restarted after WATCHDOG=trigger (NRestarts=1, new MainPID)" wait_for 25 eval \
		'[ "$(show "$U" NRestarts)" = 1 ] && [ "$(show "$U" ActiveState)" = active ] && [ "$(show "$U" MainPID)" != "$P1" ]'
	ck "P3: journal 'Watchdog request (last status: sensor fault: self-triggered watchdog)!'" \
		jgrep "Watchdog request (last status: sensor fault: self-triggered watchdog)!"
	ck "P3: journal Failed with result 'watchdog'" jgrep "Failed with result 'watchdog'."
	# ---- practice P4: self-initiated STOPPING=1 ------------------------
	P2=$(show "$U" MainPID); touch "$W/hang.quit"; deact=0
	for i in $(seq 1 40); do
		s=$(show "$U" ActiveState)
		[ "$s" = deactivating ] && [ -d "/proc/$P2" ] && deact=1
		[ "$s" = inactive ] && break
		sleep 0.1
	done
	ck "P4: STOPPING=1 -> ActiveState=deactivating while pid $P2 still runs" eq "$deact" 1
	sleep 1.5
	ck "P4: clean exit 0 -> inactive, not restarted (Restart=on-failure)" eval \
		'[ "$(show "$U" ActiveState)" = inactive ] && jgrep "Deactivated successfully."'
	# ---- practice P1: RestartPreventExitStatus=3 -----------------------
	mv "$W/temp1_input" "$W/temp1_input.off"
	sudo systemctl reset-failed "$U" >/dev/null 2>&1
	timeout 20 sudo systemctl start "$U" 2>/dev/null; rc=$?
	ck "P1: start fails (sensor missing, exit 3)" eval '[ $rc != 0 ]'
	ck "P1: failed at once, NRestarts=0, ExecMainStatus=3, StatusErrno=19" wait_for 5 eval \
		'[ "$(show "$U" ActiveState)/$(show "$U" NRestarts)/$(show "$U" ExecMainStatus)/$(show "$U" StatusErrno)" = failed/0/3/19 ]'
	# ---- practice P5b: NotifyAccess=all accepts the helper ---------------
	mv "$W/temp1_input.off" "$W/temp1_input"
	printf '[Service]\nEnvironment=ESN_CALIB_MS=300\nNotifyAccess=all\n' | sudo tee "$RUN/$U.d/zz-p5b.conf" >/dev/null
	sudo systemctl daemon-reload; sudo systemctl reset-failed "$U" >/dev/null 2>&1
	timeout 20 sudo systemctl start "$U"
	ck "P5b: NotifyAccess=all -> StatusText from the helper pid shows up" wait_for 2 eval \
		'case $(show "$U" StatusText) in "helper pid "*) true ;; *) false ;; esac'
	echo "  StatusText=$(show "$U" StatusText)  MainPID=$(show "$U" MainPID)"
	finish
fi

# ---- 4. healthy: pets keep it alive past WatchdogSec --------------------
sleep 6.5
st=$(show "$U" StatusText)
echo "after 6.5 s: MainPID=$(show "$U" MainPID) NRestarts=$(show "$U" NRestarts) StatusText=$st"
ck "alive past WatchdogSec=5: same MainPID, NRestarts=0" eq "$(show "$U" MainPID)/$(show "$U" NRestarts)" "$P1/0"
ck "STATUS= updated by pets (pets >= 2)" eval '[ "${st##*pets=}" -ge 2 ] 2>/dev/null'

# ---- 5. hang injection -> watchdog SIGABRT -> restart --------------------
touch "$W/hang"
seen_wd=0
for i in $(seq 1 125); do # <= 25 s
	r=$(systemctl show "$U" -p ActiveState -p SubState -p Result -p NRestarts -p MainPID | paste -sd' ')
	[ "$r" != "${last:-}" ] && echo "  $(date +%T.%N | cut -c1-12) $r"
	last=$r
	case "$r" in *Result=watchdog*) seen_wd=1 ;; esac
	case "$r" in *ActiveState=active*NRestarts=1*) break ;; esac
	sleep 0.2
done
P2=$(show "$U" MainPID)
ck "restarted: NRestarts=1, active, new MainPID ($P1 -> $P2)" eval \
	'[ "$(show "$U" NRestarts)/$(show "$U" ActiveState)" = 1/active ] && [ "$P2" != "$P1" ]'
ck "Result=watchdog seen during auto-restart" eq "$seen_wd" 1
ck "hang flag consumed by the daemon" eval '[ ! -e "$W/hang" ]'
ck "journal: daemon logged the hang" jgrep "hang injected: main loop stuck"
ck "journal: Watchdog timeout (limit 5s)!" jgrep "Watchdog timeout (limit 5s)!"
ck "journal: Killing process $P1 (esn_sensord) with signal SIGABRT." \
	jgrep "Killing process $P1 (esn_sensord) with signal SIGABRT."
ck "journal: Main process exited, ... status=6/ABRT" jgrep "status=6/ABRT"
ck "journal: Failed with result 'watchdog'." jgrep "Failed with result 'watchdog'."
ck "journal: Scheduled restart job, restart counter is at 1." jgrep "Scheduled restart job, restart counter is at 1."

# ---- 6. stop: SIGTERM -> STOPPING=1 -> exit 0, no restart ----------------
timeout 20 sudo systemctl stop "$U"; rc=$?
ck "systemctl stop returns 0" eq "$rc" 0
ck "journal: daemon handled SIGTERM and exited 0" jgrep "esn_sensord: SIGTERM -> exit 0"
ck "journal: Deactivated successfully." jgrep "Deactivated successfully."
sleep 1.5
ck "stop is not a failure: still inactive after RestartSec=1" eq "$(show "$U" ActiveState)" inactive

# ---- 7. sensor missing -> restart loop -> StartLimitBurst=4 ----------------
mv "$W/temp1_input" "$W/temp1_input.off"
sudo systemctl reset-failed "$U" >/dev/null 2>&1 # also resets the start rate limiter
timeout 20 sudo systemctl start "$U" 2>/dev/null; rc=$?
ck "start fails while the sensor is missing (rc=$rc)" eval '[ $rc != 0 ]'
ck "gives up: ActiveState=failed within 15 s" wait_for 15 eval '[ "$(show "$U" ActiveState)" = failed ]'
r=$(echo "$(show "$U" NRestarts)/$(show "$U" ExecMainStatus)/$(show "$U" StatusErrno)/$(show "$U" StatusText)")
echo "  NRestarts/ExecMainStatus/StatusErrno/StatusText = $r"
ck "NRestarts=4 ExecMainStatus=3 StatusErrno=19 StatusText='sensor missing'" eq "$r" "4/3/19/sensor missing"
ck "journal: Start request repeated too quickly." jgrep "Start request repeated too quickly."
n_exec=$(jr | grep -c "missing -> exit 3"); n_lim=$(jr | grep -c "Start request repeated too quickly.")
timeout 20 sudo systemctl start "$U"; rc=$?
sleep 0.5
n_exec2=$(jr | grep -c "missing -> exit 3"); n_lim2=$(jr | grep -c "Start request repeated too quickly.")
echo "  manual start: rc=$rc Result=$(show "$U" Result) daemon runs $n_exec -> $n_exec2, limiter lines $n_lim -> $n_lim2"
ck "manual start refused by the limiter: rc!=0, daemon NOT executed, one more 'repeated too quickly'" eval \
	'[ $rc != 0 ] && [ "$n_exec2" = "$n_exec" ] && [ "$n_lim2" = $((n_lim + 1)) ]'
mv "$W/temp1_input.off" "$W/temp1_input"
sudo systemctl reset-failed "$U"
timeout 20 sudo systemctl start "$U"; rc=$?
ck "reset-failed clears the limiter: start works again" eval '[ $rc = 0 ] && [ "$(show "$U" ActiveState)" = active ]'
finish
