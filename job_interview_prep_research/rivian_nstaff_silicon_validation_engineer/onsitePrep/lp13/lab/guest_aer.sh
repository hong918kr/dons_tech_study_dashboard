#!/bin/sh
s() { echo; echo "##### $*"; }
mount -t tmpfs none /tmp 2>/dev/null
insmod /lib/modules/nvme-core.ko; insmod /lib/modules/nvme.ko; sleep 1
s snapshot-before; python3 /pcie_snapshot.py --out /tmp/a.json
R=/sys/bus/pci/devices/0000:00:01.0
s aer-before; cat $R/aer_dev_correctable; echo "rootport_total_err_cor=$(cat $R/aer_rootport_total_err_cor)"
echo "READY_FOR_INJECT"; sleep 6
s aer-after; cat $R/aer_dev_correctable; echo "rootport_total_err_cor=$(cat $R/aer_rootport_total_err_cor)"; cat $R/aer_dev_nonfatal | grep -v " 0$"
echo "CESta via setpci: $(setpci -s 00:01.0 ECAP_AER+10.l)  UESta: $(setpci -s 00:01.0 ECAP_AER+4.l)"
s dmesg-aer; dmesg | grep -i aer | tail -20
s lspci-aer-rp; lspci -vvv -s 00:01.0 | grep -A3 -E "CESta|UESta:|RootSta: CE|ErrorSrc"
s snapshot-after; python3 /pcie_snapshot.py --out /tmp/b.json; python3 -c "import json;d=json.load(open(\"/tmp/b.json\"));print(json.dumps([x for x in d[\"devices\"] if x[\"bdf\"]==\"0000:00:01.0\"][0][\"aer\"],indent=1))"
s vvv-file; echo BEGIN_VVV; lspci -vvv; echo END_VVV
s dump-file; echo BEGIN_DUMP; lspci -xxxx; echo END_DUMP
s sysfs-config-dump; echo BEGIN_SYSDUMP; for d in /sys/bus/pci/devices/*; do b=${d##*/}; echo "${b#0000:} sysfs dump"; hexdump -v -e "16/1 \" %02x\" \"\n\"" $d/config | awk "{printf \"%03x:%s\n\", (NR-1)*16, \$0}"; echo; done; echo END_SYSDUMP
s done
