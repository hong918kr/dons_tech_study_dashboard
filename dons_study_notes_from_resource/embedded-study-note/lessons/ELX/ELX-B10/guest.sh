#!/bin/sh
# ENV: qemu
# ELX-B10 guest side (runs as LAB_CMD inside the QEMU guest, BusyBox sh).
#   guest.sh check   -> build a gpio-sim chip via configfs, run /b10_test on it
#   guest.sh demo    -> same chip, then the "observe it" experiments shown in the md
what=$1
mount -t configfs none /sys/kernel/config 2>/dev/null
G=/sys/kernel/config/gpio-sim/b10
mkdir $G $G/bank0
echo 8 > $G/bank0/num_lines
echo b10-sim > $G/bank0/label
i=0
for n in LED0 BTN0 IMU_RST_N IMU_DRDY BUS0 BUS1 BUS2 BUS3; do
	mkdir $G/bank0/line$i
	echo $n > $G/bank0/line$i/name
	i=$((i + 1))
done
echo 1 > $G/live
DEV=$(cat $G/dev_name)
CHIP=$(cat $G/bank0/chip_name)
SIM=/sys/devices/platform/$DEV/$CHIP
echo pull-up > $SIM/sim_gpio2/pull	# board pull-up on IMU_RST_N
echo "GUEST_START kernel=$(uname -r) dev=$DEV chip=$CHIP cpus=$(nproc)"

case "$what" in
check)
	/b10_test /dev/$CHIP $SIM
	echo "b10_test exit=$?"
	;;
demo)
	echo "=== configfs tree ==="
	ls $G $G/bank0 $G/bank0/line1 | tr '\n' ' '; echo
	echo "=== sim side of line 1 ==="
	ls $SIM/sim_gpio1 | tr '\n' ' '; echo
	echo "=== legacy sysfs GPIO? ==="
	ls /sys/class/gpio 2>&1
	zcat /proc/config.gz | grep -E 'CONFIG_GPIO_(CDEV|SIM)=|CONFIG_HTE='
	echo "lines mentioning GPIO_SYSFS in /proc/config.gz: $(zcat /proc/config.gz | grep -c GPIO_SYSFS)"
	echo "=== the sim 'value' file is read-only ==="
	echo 1 > $SIM/sim_gpio0/value
	echo "=== debugfs view while a line is held ==="
	/demo_burst /dev/$CHIP $SIM hold
	echo "=== open-loop bursts (no waiting between writes) ==="
	for k in 20 200 1000; do /demo_burst /dev/$CHIP $SIM burst $k; done
	echo "=== HTE clock flag without an HTE provider ==="
	/demo_burst /dev/$CHIP $SIM hte
	;;
esac
echo "--- dmesg (tail) ---"
dmesg | tail -n 8
echo 0 > $G/live
echo GUEST_END
