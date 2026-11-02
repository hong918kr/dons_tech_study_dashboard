#!/bin/bash
# Build a BusyBox initramfs + Ubuntu lspci/setpci/pcilmr/python3/trace-cmd + nvme modules (lab kernel Image-trace)
set -e
W=/tmp/LP13; R=$W/r; rm -rf $R; mkdir -p $R
cp -a /var/lab/build/rootfs/. $R/
copy_bin() { for b in "$@"; do install -D "$b" "$R$b"; for l in $(ldd "$b" | awk "/=>/{print \$3} /ld-linux/{print \$1}"); do [ -f "$l" ] && install -D "$l" "$R$l"; done; done; }
copy_bin /usr/bin/lspci /usr/bin/setpci /usr/bin/pcilmr /usr/bin/python3 /usr/bin/trace-cmd
mkdir -p $R/lib && [ -e $R/lib/ld-linux-aarch64.so.1 ] || cp -L /lib/ld-linux-aarch64.so.1 $R/lib/
install -D /usr/share/misc/pci.ids $R/usr/share/misc/pci.ids
mkdir -p $R/usr/lib/python3.14
(cd /usr/lib/python3.14 && tar cf - --exclude=test --exclude=idlelib --exclude=tkinter --exclude=turtledemo --exclude=ensurepip --exclude=__pycache__ --exclude=lib2to3 --exclude=site-packages --exclude=config-3.14-aarch64-linux-gnu .) | (cd $R/usr/lib/python3.14 && tar xf -)
M=/var/lab/out/modules-trace/lib/modules/6.18.55-trace
mkdir -p $R/lib/modules
cp $M/kernel/drivers/nvme/host/nvme-core.ko $M/kernel/drivers/nvme/host/nvme.ko $R/lib/modules/
cp $W/pcie_snapshot.py $R/
cp ${GUEST:-$W/guest.sh} $R/guest.sh
for f in $EXTRA; do cp "$f" $R/; done   # extra files (e.g. LP14 link_cycle.py) -> guest /
(cd $R && find . | cpio -o -H newc --quiet | gzip -1) > $W/i.gz
ls -la $W/i.gz
