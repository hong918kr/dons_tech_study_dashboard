#!/bin/sh
# ELX-M05 guest side.  guest.sh check <module>   |   guest.sh demo <mode>  |  guest.sh latency
what=$1; arg=$2
mount -t configfs none /sys/kernel/config
G=/sys/kernel/config/gpio-sim/m05
mkdir -p $G/bank0/line0
echo 8 > $G/bank0/num_lines
echo m05-sig > $G/bank0/line0/name
echo 1 > $G/live
DEV=$(cat $G/dev_name); CHIP=$(cat $G/bank0/chip_name)
PULL=/sys/devices/platform/$DEV/$CHIP/sim_gpio0/pull
echo "GUEST_START dev=$DEV chip=$CHIP pull=$PULL kernel=$(uname -r) cpus=$(nproc)"

case "$what" in
check)
	insmod /$arg.ko || echo "insmod failed"
	/m05_test "$PULL" "$arg"
	echo "--- /proc/interrupts ---"; grep -E 'CPU0|elx_m05' /proc/interrupts
	echo "--- stats ---"; cat /sys/devices/platform/elx_m05/stats /sys/devices/platform/elx_m05/latency 2>/dev/null
	echo "--- rmmod ---"; rmmod $arg && echo rmmod ok
	grep -c elx_m05 /proc/interrupts
	;;
demo)
	if [ "$arg" = latency ]; then
		insmod /solution.ko
		S=/sys/devices/platform/elx_m05
		echo 0 > $S/work_us
		run_load() {	# name hog_args
			echo "=== load: $1 (loadavg $(cat /proc/loadavg)) ==="
			echo 1 > $S/reset
			if [ -n "$2" ]; then /demo_hog $2 & HOG=$!; fi
			/m05_test "$PULL" latency 200 5000
			if [ -n "$2" ]; then wait $HOG; fi
			cat $S/stats $S/latency
		}
		run_load idle ""
		run_load other4 "other 0 2"
		run_load fifo98x4 "fifo 98 3"
		rmmod solution
	else
		M=/sys/module/demo_bugs/parameters
		insmod /demo_bugs.ko mode=$arg
		echo "request_threaded_irq ret: $(cat $M/req_ret 2>/dev/null)"
		grep -E 'CPU0|elx_m05' /proc/interrupts
		case "$arg" in
		lost)
			echo 1 > $M/gate
			echo pull-up > $PULL; usleep 100000; echo pull-down > $PULL
			i=2; while [ $i -le 20 ]; do
				echo pull-up > $PULL; usleep 2000; echo pull-down > $PULL
				i=$((i+1))
			done
			echo "while parked: hard=$(cat $M/hard) runs=$(cat $M/runs) handled=$(cat $M/handled)"
			echo 0 > $M/gate; usleep 300000
			echo "after release: hard=$(cat $M/hard) runs=$(cat $M/runs) handled=$(cat $M/handled)"
			;;
		nooneshot|badtype)
			;;
		fixoneshot)
			echo pull-up > $PULL; usleep 200000; echo pull-down > $PULL
			echo "runs=$(cat $M/runs) handled=$(cat $M/handled)"
			;;
		sleep_hard|getvalue)
			echo pull-up > $PULL; usleep 500000; echo pull-down > $PULL
			echo "hard=$(cat $M/hard) runs=$(cat $M/runs)"
			;;
		esac
		grep -E 'elx_m05' /proc/interrupts
		ps | grep -E '\[irq/|PID' | head
		rmmod demo_bugs && echo rmmod ok
	fi
	;;
esac
echo "--- dmesg (kernel log) ---"; dmesg | tail -60
echo GUEST_END
