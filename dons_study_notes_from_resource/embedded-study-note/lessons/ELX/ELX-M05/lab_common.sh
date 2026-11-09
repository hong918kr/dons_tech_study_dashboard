#!/bin/bash
# ELX-M05 shared: build module + helpers in /tmp/ELX-M05_work/<name>, boot Image-dbg under timeout.
# Sourced by check.sh and demo.sh. Runs in the Lima VM, never insmods into the VM's own kernel.
LAB=/var/lab
KDIR=$LAB/out/linux-build-dbg
IMAGE=$LAB/out/Image-dbg
ROOT_WORK=/tmp/ELX-M05_work

# prepare <src_dir> <name> <module.c-basename>
prepare() {
	local src=$1 name=$2 mod=$3
	WORK=$ROOT_WORK/$name
	rm -rf "$WORK"; mkdir -p "$WORK"
	cp "$src"/*.c "$src"/*.sh "$src"/Makefile "$WORK"/
	( cd "$WORK" && make -s -C "$KDIR" M="$WORK" MOD="$mod" modules ) || return 2
	gcc -static -std=c11 -Wall -Wextra -Werror -O2 -pthread \
		"$WORK/m05_test.c" -o "$WORK/m05_test" || return 2
	gcc -static -std=c11 -Wall -Wextra -Werror -O2 -pthread \
		"$WORK/demo_hog.c" -o "$WORK/demo_hog" || return 2
	local r=$WORK/r
	mkdir -p "$r"; cp -a $LAB/build/rootfs/. "$r"/
	cp "$WORK/$mod.ko" "$WORK/m05_test" "$WORK/demo_hog" "$WORK/guest.sh" "$r"/
	chmod +x "$r/guest.sh"
	( cd "$r" && find . | cpio -o -H newc --quiet | gzip -1 ) > "$WORK/i.gz"
}

# boot <guest_cmd_with_tildes> <timeout_s>  -> guest serial output on stdout
boot() {
	timeout --foreground "$2" qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 512M \
		-nographic -no-reboot -kernel "$IMAGE" -initrd "$WORK/i.gz" \
		-append "console=ttyAMA0 loglevel=4 LAB_CMD=$1" </dev/null 2>&1
}
