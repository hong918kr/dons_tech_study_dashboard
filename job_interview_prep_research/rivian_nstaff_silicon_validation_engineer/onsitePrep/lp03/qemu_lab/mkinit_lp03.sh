#!/bin/bash
# Build /tmp/LP03/i.gz = lab BusyBox rootfs + glibc lspci/setpci + pci.ids + /lab.sh (+ /extra.sh if present)
set -e
W=/tmp/LP03; mkdir -p $W; cd $W
rm -rf r; mkdir r; cp -a /var/lab/build/rootfs/. r/
mkdir -p r/usr/bin r/usr/share/misc r/lib/aarch64-linux-gnu
# busybox applet links: remove first so cp does not write THROUGH the symlink into busybox
rm -f r/usr/bin/lspci r/usr/bin/setpci r/bin/lspci r/bin/setpci r/sbin/lspci r/sbin/setpci r/usr/sbin/lspci r/usr/sbin/setpci
cp /usr/bin/lspci /usr/bin/setpci r/usr/bin/
for b in /usr/bin/lspci /usr/bin/setpci; do
  for l in $(ldd $b | awk '/=>/{print $3} /ld-linux/{print $1}'); do
    case $l in /lib/ld-linux*) cp -L $l r/lib/ ;; *) cp -L $l r/lib/aarch64-linux-gnu/ ;; esac
  done
done
cp /usr/share/misc/pci.ids r/usr/share/misc/pci.ids
cp $W/lab.sh r/lab.sh; chmod +x r/lab.sh
[ -f $W/extra.sh ] && cp $W/extra.sh r/extra.sh; [ -f $W/pcie_set_speed_retrain.sh ] && cp $W/pcie_set_speed_retrain.sh r/retrain.sh
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
file r/bin/busybox; ls -la i.gz
