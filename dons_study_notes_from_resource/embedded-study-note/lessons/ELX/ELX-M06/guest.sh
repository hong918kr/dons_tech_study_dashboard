#!/bin/sh
# guest.sh <module> <check|measure|splat> [K]  (runs inside the QEMU guest as LAB_CMD)
MOD=$1
MODE=$2
K=$3
MECHS="tasklet wq_sys wq_hi threaded wq_bh"
FAILS=0
chk() { # chk <name> <0=ok>
	if [ "$2" = "0" ]; then echo "CHECK $1 PASS"; else echo "CHECK $1 FAIL"; FAILS=$((FAILS + 1)); fi
}
setup_sim() {
	mount -t configfs none /sys/kernel/config || return 1
	C=/sys/kernel/config/gpio-sim/bhc
	mkdir $C $C/gpio-bank0
	echo 8 > $C/gpio-bank0/num_lines
	echo bhc-chip > $C/gpio-bank0/label
	echo 1 > $C/live
	CHIP=$(cat $C/gpio-bank0/chip_name)
	DEV=$(cat $C/dev_name)
	PULL=/sys/devices/platform/$DEV/$CHIP/sim_gpio0/pull
	[ -w "$PULL" ]
}
dmesg_clean() { # non-empty output => dirty
	dmesg | grep -E 'BUG:|WARNING:|lockdep:|possible .* deadlock|inconsistent lock' | grep -v 'Lock dependency validator'
}
mech_stat() { # mech_stat <mech> <field>
	grep "^$1 " /sys/kernel/bh_cmp/stats | tr ' ' '\n' | grep "^$2=" | cut -d= -f2
}
expected_ctx() {
	case $1 in tasklet|wq_bh) echo SOFT ;; *) echo TASK+SLEEP ;; esac
}
total_proc_irq() {
	awk -v I="$1:" '$1 == I { s = 0; for (i = 2; i <= NF && $i ~ /^[0-9]+$/; i++) s += $i; print s; found = 1 } END { if (!found) print -1 }' /proc/interrupts
}

mode_practice() {
	dmesg -c >/dev/null
	insmod /practice.ko
	chk practice_insmod $?
	dmesg | grep -E 'CHECK|PRACTICE'
	dmesg | grep -q 'PRACTICE RESULT.*fail=0' ; chk practice_all $?
	rmmod practice; chk practice_rmmod $?
	D=$(dmesg_clean); echo "$D"; [ -z "$D" ]; chk dmesg_clean $?
}

mode_check() {
	setup_sim; chk gpio_sim_setup $?
	dmesg -c >/dev/null
	insmod /$MOD.ko; chk insmod $?
	[ -d /sys/kernel/bh_cmp ] || return
	IRQ=$(cat /sys/kernel/bh_cmp/irq)
	echo 200 > /sys/kernel/bh_cmp/sleep_us
	echo 1 > /sys/kernel/bh_cmp/reset
	before=$(total_proc_irq "$IRQ")
	TOTAL=0
	for m in $MECHS; do
		echo $m > /sys/kernel/bh_cmp/mode
		/fire "$PULL" /sys/kernel/bh_cmp/done "$K" 1500
		chk "${m}_handled_all" $?
		n=$(mech_stat $m n)
		[ "$n" = "$K" ]; chk "${m}_count_eq_K" $?
		ctx=$(mech_stat $m ctx); stable=$(mech_stat $m stable)
		echo "  $m: n=$n ctx=$ctx stable=$stable comm=$(mech_stat $m comm)"
		[ "$ctx" = "$(expected_ctx $m)" ] && [ "$stable" = "1" ]; chk "${m}_context" $?
		TOTAL=$((TOTAL + K))
	done
	after=$(total_proc_irq "$IRQ")
	echo "  /proc/interrupts delta=$((after - before)) expected=$TOTAL"
	[ "$((after - before))" = "$TOTAL" ]; chk proc_interrupts_exact $?
	irqs=$(grep '^hard ' /sys/kernel/bh_cmp/stats | tr ' ' '\n' | grep '^irqs=' | cut -d= -f2)
	[ "$irqs" = "$TOTAL" ]; chk hard_irq_count_exact $?
	grep -q '^hard ctx=HARD ' /sys/kernel/bh_cmp/stats; chk hard_handler_context $?
	ps | grep -q "\[irq/$IRQ-bh_cmp\]"; chk irq_thread_exists $?
	cat /sys/kernel/bh_cmp/stats
	rmmod $MOD; chk rmmod $?
	D=$(dmesg_clean); echo "$D"; [ -z "$D" ]; chk dmesg_clean $?
}

mode_splat() {
	setup_sim || return
	dmesg -c >/dev/null
	insmod /$MOD.ko bad_tasklet_sleep=1 || return
	echo tasklet > /sys/kernel/bh_cmp/mode
	/fire "$PULL" /sys/kernel/bh_cmp/done 1 5000
	echo "fire exit=$?"
	dmesg | tail -60
}

mode_measure() {
	setup_sim || return
	insmod /$MOD.ko || return
	IRQ=$(cat /sys/kernel/bh_cmp/irq)
	mount -t tracefs none /sys/kernel/tracing || return
	T=/sys/kernel/tracing
	echo 0 > $T/tracing_on; echo nop > $T/current_tracer; echo mono > $T/trace_clock
	echo 2048 > $T/buffer_size_kb
	echo "name == \"bh_cmp\"" > $T/events/irq/irq_handler_entry/filter
	echo 'vec == 6' > $T/events/irq/softirq_entry/filter
	echo "next_comm ~ \"irq/*bh_cmp\"" > $T/events/sched/sched_switch/filter
	for e in irq/irq_handler_entry irq/softirq_entry workqueue/workqueue_execute_start sched/sched_switch; do
		echo 1 > $T/events/$e/enable
	done
	echo 0 > /sys/kernel/bh_cmp/sleep_us
	ps | grep "irq/$IRQ"
	cat /proc/$(ps | awk -v I="irq/$IRQ-bh_cmp" '$0 ~ I && !/grep/ {print $1; exit}')/stat 2>/dev/null | awk '{print "irq-thread comm="$2" prio="$18" nice="$19" rt_priority="$40" policy="$41}'
	for cond in idle load; do
		PIDS=""
		if [ $cond = load ]; then
			for c in 1 2 3 4; do (while :; do :; done) & PIDS="$PIDS $!"; done
			sleep 1
		fi
		echo "=== $cond (nproc=4, hogs=${PIDS:+4}) ==="
		cat /proc/loadavg
		for m in $MECHS; do
			echo 1 > /sys/kernel/bh_cmp/reset
			echo $m > /sys/kernel/bh_cmp/mode
			echo > $T/trace
			echo 1 > $T/tracing_on
			/fire "$PULL" /sys/kernel/bh_cmp/done "$K" 5000 300 >/dev/null
			rc=$?
			echo 0 > $T/tracing_on
			line=$(grep "^$m " /sys/kernel/bh_cmp/stats)
			parsed=$(awk -v mech=$m -f /parse_trace.awk $T/trace)
			[ "$cond" = idle ] && { echo "--- raw trace sample ($m):"; grep -v '^#' $T/trace | head -6; echo "---"; }
			echo "RESULT cond=$cond mech=$m fire_rc=$rc internal=[$line] ftrace=[$parsed]"
		done
		for p in $PIDS; do kill $p; done
		wait 2>/dev/null
	done
	cat /proc/interrupts | grep bh_cmp
	dmesg | grep -E 'BUG:|WARNING:|lockdep:' | head
}

echo "GUEST-START $MOD $MODE K=$K"
case "$MOD:$MODE" in
practice:*) mode_practice ;;
*:check) mode_check ;;
*:splat) mode_splat ;;
*:measure) mode_measure ;;
esac
echo "FAILS=$FAILS"
echo "GUEST-END"
sync
