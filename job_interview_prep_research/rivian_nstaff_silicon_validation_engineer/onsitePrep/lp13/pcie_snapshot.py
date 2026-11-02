#!/usr/bin/env python3
"""pcie_snapshot.py - one-shot PCIe health snapshot from Linux sysfs (LP13).

Walks /sys/bus/pci/devices and, for every PCI function, records:
  * identity: BDF, vendor/device/class/revision, bound driver, parent port
  * link: current_link_speed/width vs max_link_speed/width (sysfs, PCIe only)
  * expected link = min(own max, parent port max)   -> "downgraded" warning
  * raw PCIe capability decode from the binary 'config' file when readable
    (root needed for >64 bytes): port type, LnkSta Train / DLActive bits
  * AER counters: aer_dev_correctable / nonfatal / fatal, aer_rootport_total_err_*
Writes JSON (stdout or --out) and prints human-readable warnings to stderr.

No third-party modules. Works on any Linux with sysfs; on macOS use --selftest
(builds a fake sysfs tree in a temp dir and checks the warnings).

Exit code: 0 = no warnings, 1 = warnings found and --strict given, 2 = error.
"""
import argparse
import datetime
import json
import os
import platform
import struct
import sys
import tempfile

SYSFS_DEFAULT = "/sys/bus/pci/devices"

# Offsets / bits from include/uapi/linux/pci_regs.h
PCI_STATUS = 0x06
PCI_STATUS_CAP_LIST = 0x10
PCI_CAPABILITY_LIST = 0x34
PCI_CAP_ID_EXP = 0x10
PCI_EXP_FLAGS = 0x02
PCI_EXP_LNKCAP = 0x0C
PCI_EXP_LNKSTA = 0x12
PCI_EXP_LNKCAP_DLLLARC = 0x00100000
PCI_EXP_LNKSTA_LT = 0x0800
PCI_EXP_LNKSTA_DLLLA = 0x2000
PCI_EXP_LNKSTA_LBMS = 0x4000
PCI_EXP_LNKSTA_LABS = 0x8000
PORT_TYPES = {
    0x0: "Endpoint", 0x1: "Legacy Endpoint", 0x4: "Root Port",
    0x5: "Upstream Port", 0x6: "Downstream Port", 0x7: "PCIe-to-PCI Bridge",
    0x8: "PCI-to-PCIe Bridge", 0x9: "RC Integrated Endpoint", 0xA: "RC Event Collector",
}
SPEED_CODE_GTS = {1: 2.5, 2: 5.0, 3: 8.0, 4: 16.0, 5: 32.0, 6: 64.0}
AER_FILES = ("aer_dev_correctable", "aer_dev_nonfatal", "aer_dev_fatal")
AER_RP_FILES = ("aer_rootport_total_err_cor", "aer_rootport_total_err_nonfatal",
                "aer_rootport_total_err_fatal")


def read_text(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def parse_speed(s):
    """'16.0 GT/s PCIe' -> 16.0 ; 'Unknown' / None -> None."""
    if not s:
        return None
    tok = s.split()[0]
    try:
        return float(tok)
    except ValueError:
        return None


def parse_int(s, base=10):
    try:
        return int(s, base)
    except (TypeError, ValueError):
        return None


def parse_aer(text):
    """'Receiver Error 2\\nBad TLP 0\\n...TOTAL_ERR_COR 2' -> {name: count}."""
    out = {}
    if not text:
        return out
    for line in text.splitlines():
        name, _, val = line.rpartition(" ")
        if name and val.isdigit():
            out[name] = int(val)
    return out


def decode_config(cfg):
    """Walk the capability list in a config-space blob; decode the PCIe cap."""
    res = {"config_bytes": len(cfg)}
    if len(cfg) < 0x40 or struct.unpack_from("<H", cfg, 0)[0] == 0xFFFF:
        res["note"] = "config unreadable or all-ones (device gone?)"
        return res
    if not struct.unpack_from("<H", cfg, PCI_STATUS)[0] & PCI_STATUS_CAP_LIST:
        return res
    ptr, guard = cfg[PCI_CAPABILITY_LIST] & 0xFC, 48
    while ptr and guard:
        if ptr + 2 > len(cfg):
            res["note"] = "capabilities beyond readable range (run as root)"
            return res
        if cfg[ptr] == PCI_CAP_ID_EXP:
            if ptr + PCI_EXP_LNKSTA + 2 > len(cfg):
                res["note"] = "PCIe cap beyond readable range (run as root)"
                return res
            flags = struct.unpack_from("<H", cfg, ptr + PCI_EXP_FLAGS)[0]
            lnkcap = struct.unpack_from("<I", cfg, ptr + PCI_EXP_LNKCAP)[0]
            lnksta = struct.unpack_from("<H", cfg, ptr + PCI_EXP_LNKSTA)[0]
            res.update({
                "pcie_cap_offset": hex(ptr),
                "port_type": PORT_TYPES.get((flags >> 4) & 0xF, "type %#x" % ((flags >> 4) & 0xF)),
                "lnkcap_raw": "%#010x" % lnkcap,
                "lnksta_raw": "%#06x" % lnksta,
                "lnksta_speed_gts": SPEED_CODE_GTS.get(lnksta & 0xF),
                "lnksta_width": (lnksta >> 4) & 0x3F,
                "dll_link_active_reporting": bool(lnkcap & PCI_EXP_LNKCAP_DLLLARC),
                "train": bool(lnksta & PCI_EXP_LNKSTA_LT),
                "dl_active": bool(lnksta & PCI_EXP_LNKSTA_DLLLA),
                "bw_mgmt": bool(lnksta & PCI_EXP_LNKSTA_LBMS),
                "auto_bw": bool(lnksta & PCI_EXP_LNKSTA_LABS),
            })
            return res
        ptr = cfg[ptr + 1] & 0xFC
        guard -= 1
    return res


def parent_bdf(devdir):
    """Parent in the sysfs device tree, if it is a PCI function (= upstream port)."""
    real = os.path.realpath(devdir)
    up = os.path.basename(os.path.dirname(real))
    # PCI function dirs look like 0000:00:01.0 ; host bridges look like pci0000:00
    return up if up.count(":") == 2 and "." in up else None


def snapshot_device(root, bdf):
    d = os.path.join(root, bdf)
    dev = {
        "bdf": bdf,
        "vendor": read_text(os.path.join(d, "vendor")),
        "device": read_text(os.path.join(d, "device")),
        "class": read_text(os.path.join(d, "class")),
        "revision": read_text(os.path.join(d, "revision")),
        "driver": None,
        "parent": parent_bdf(d),
        "numa_node": parse_int(read_text(os.path.join(d, "numa_node"))),
        "power_state": read_text(os.path.join(d, "power_state")),
        "msi_irqs": None,
    }
    drv = os.path.join(d, "driver")
    if os.path.islink(drv):
        dev["driver"] = os.path.basename(os.readlink(drv))
    msi = os.path.join(d, "msi_irqs")
    if os.path.isdir(msi):
        dev["msi_irqs"] = len(os.listdir(msi))

    link = {}
    for k in ("current_link_speed", "max_link_speed"):
        v = read_text(os.path.join(d, k))
        if v is not None:
            link[k] = v
            link[k + "_gts"] = parse_speed(v)
    for k in ("current_link_width", "max_link_width"):
        v = read_text(os.path.join(d, k))
        if v is not None:
            link[k] = parse_int(v)
    dev["link"] = link or None  # None = not a PCIe function (no link attrs)

    aer = {}
    for k in AER_FILES:
        v = parse_aer(read_text(os.path.join(d, k)))
        if v:
            aer[k] = v
    for k in AER_RP_FILES:
        v = parse_int(read_text(os.path.join(d, k)))
        if v is not None:
            aer[k] = v
    dev["aer"] = aer or None

    try:
        with open(os.path.join(d, "config"), "rb") as f:
            dev["config"] = decode_config(f.read())
    except OSError as e:
        dev["config"] = {"error": str(e)}
    return dev


def find_warnings(devs):
    """Judge each LINK, not each function.

    A link = (downstream-facing port above, function below). The function below
    (endpoint or switch upstream port) is the one we judge: expected speed/width =
    min(its own max, the parent port's max). A root/downstream port's own
    current < max is NOT a warning by itself - the partner may simply be slower
    (lspci also prints '(downgraded)' only on the upstream side of a link).
    A downstream port with no child is checked for DLActive- / x0.
    """
    by_bdf = {x["bdf"]: x for x in devs}
    children = {}
    for x in devs:
        if x["parent"]:
            children.setdefault(x["parent"], []).append(x["bdf"])
    warns = []
    for x in devs:
        lk, cfg = x["link"], x.get("config") or {}
        par = by_bdf.get(x["parent"]) if x["parent"] else None
        plk = par["link"] if par else None
        if lk and plk:  # x is the upstream end of a link below port `par`
            cur_s, cur_w = lk.get("current_link_speed_gts"), lk.get("current_link_width")
            exp_s = min([v for v in (lk.get("max_link_speed_gts"), plk.get("max_link_speed_gts")) if v],
                        default=None)
            exp_w = min([v for v in (lk.get("max_link_width"), plk.get("max_link_width")) if v],
                        default=None)
            tag = "%s->%s" % (par["bdf"], x["bdf"])
            if cur_w == 0:
                warns.append("%s: link width x0 (link down / not trained)" % tag)
            else:
                if cur_s and exp_s and cur_s < exp_s:
                    warns.append("%s: speed downgraded %.1f GT/s < expected %.1f GT/s"
                                 % (tag, cur_s, exp_s))
                if cur_w and exp_w and cur_w < exp_w:
                    warns.append("%s: width downgraded x%d < expected x%d" % (tag, cur_w, exp_w))
            if plk.get("current_link_speed_gts") and cur_s and plk["current_link_speed_gts"] != cur_s:
                warns.append("%s: both ends disagree on speed (%.1f vs %.1f)"
                             % (tag, plk["current_link_speed_gts"], cur_s))
        is_dsp = cfg.get("port_type") in ("Root Port", "Downstream Port")
        if is_dsp and not children.get(x["bdf"]):
            if cfg.get("dll_link_active_reporting") and cfg.get("dl_active") is False:
                warns.append("%s: empty downstream port, DLActive- (no device / link not up)" % x["bdf"])
        elif is_dsp and cfg.get("dll_link_active_reporting") and cfg.get("dl_active") is False:
            warns.append("%s: DLActive- but a child is enumerated (link dropped after boot?)" % x["bdf"])
        if cfg.get("train"):
            warns.append("%s: LnkSta Link Training bit set" % x["bdf"])
        if cfg.get("note", "").startswith("config unreadable"):
            warns.append("%s: %s" % (x["bdf"], cfg["note"]))
        for k, v in (x["aer"] or {}).items():
            tot = v.get("TOTAL_ERR_COR", v.get("TOTAL_ERR_NONFATAL", v.get("TOTAL_ERR_FATAL", 0))) \
                if isinstance(v, dict) else v
            if tot:
                warns.append("%s: %s total=%d" % (x["bdf"], k, tot))
    return warns


def snapshot(root):
    bdfs = sorted(os.listdir(root))
    devs = [snapshot_device(root, b) for b in bdfs]
    return {
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "host": platform.node(),
        "kernel": platform.release(),
        "euid": os.geteuid(),
        "sysfs_root": root,
        "devices": devs,
        "warnings": find_warnings(devs),
    }


# ---------------------------------------------------------------- selftest
def _cfg_blob(port_type, lnkcap_speed, lnkcap_width, sta_speed, sta_width, dlla, train=False):
    cfg = bytearray(256)
    struct.pack_into("<HH", cfg, 0, 0x1234, 0x5678)
    struct.pack_into("<H", cfg, PCI_STATUS, PCI_STATUS_CAP_LIST)
    cfg[PCI_CAPABILITY_LIST] = 0x40
    cfg[0x40], cfg[0x41] = 0x01, 0x60          # PM cap -> next 0x60
    cfg[0x60], cfg[0x61] = PCI_CAP_ID_EXP, 0x00
    struct.pack_into("<H", cfg, 0x60 + PCI_EXP_FLAGS, 0x0002 | (port_type << 4))
    struct.pack_into("<I", cfg, 0x60 + PCI_EXP_LNKCAP,
                     lnkcap_speed | (lnkcap_width << 4) | PCI_EXP_LNKCAP_DLLLARC)
    sta = sta_speed | (sta_width << 4) | (PCI_EXP_LNKSTA_DLLLA if dlla else 0) \
        | (PCI_EXP_LNKSTA_LT if train else 0)
    struct.pack_into("<H", cfg, 0x60 + PCI_EXP_LNKSTA, sta)
    return bytes(cfg)


def _mkdev(base, devices_dir, path_parts, attrs, cfg=None, driver=None):
    real = os.path.join(base, "devices", *path_parts)
    os.makedirs(real, exist_ok=True)
    for k, v in attrs.items():
        with open(os.path.join(real, k), "w") as f:
            f.write(v + "\n")
    if cfg is not None:
        with open(os.path.join(real, "config"), "wb") as f:
            f.write(cfg)
    if driver:
        drvdir = os.path.join(base, "drivers", driver)
        os.makedirs(drvdir, exist_ok=True)
        os.symlink(drvdir, os.path.join(real, "driver"))
    os.symlink(real, os.path.join(devices_dir, path_parts[-1]))


def selftest():
    names = {16.0: "16.0 GT/s PCIe", 8.0: "8.0 GT/s PCIe", 32.0: "32.0 GT/s PCIe"}
    with tempfile.TemporaryDirectory() as base:
        dd = os.path.join(base, "bus_pci_devices")
        os.makedirs(dd)
        hb = "pci0000:00"
        rp = {"vendor": "0x1b36", "device": "0x000c", "class": "0x060400",
              "max_link_speed": names[16.0], "max_link_width": "4"}
        # Root port 00:01.0 ok link Gen4 x4; endpoint 01:00.0 trained at Gen3 x4 -> downgraded
        _mkdev(base, dd, [hb, "0000:00:01.0"],
               dict(rp, current_link_speed=names[8.0], current_link_width="4"),
               _cfg_blob(4, 4, 4, 3, 4, True), "pcieport")
        _mkdev(base, dd, [hb, "0000:00:01.0", "0000:01:00.0"],
               {"vendor": "0x1b36", "device": "0x0010", "class": "0x010802",
                "max_link_speed": names[32.0], "max_link_width": "4",
                "current_link_speed": names[8.0], "current_link_width": "4",
                "aer_dev_correctable": "Receiver Error 3\nBad TLP 0\nTOTAL_ERR_COR 3"},
               _cfg_blob(0, 5, 4, 3, 4, False), "nvme")
        # Root port 00:02.0 link down (x0, DLActive-), nothing below it
        _mkdev(base, dd, [hb, "0000:00:02.0"],
               dict(rp, current_link_speed=names[16.0], current_link_width="0"),
               _cfg_blob(4, 4, 4, 1, 0, False), "pcieport")
        # Conventional PCI device: no link attributes, 64-byte config only
        _mkdev(base, dd, [hb, "0000:00:03.0"],
               {"vendor": "0x1234", "device": "0x11e8", "class": "0x00ff00"},
               bytes(64))
        snap = snapshot(dd)
    w = snap["warnings"]
    checks = [
        ("endpoint speed downgraded vs min(own 32, parent 16)=16",
         any("0000:00:01.0->0000:01:00.0: speed downgraded 8.0 GT/s < expected 16.0" in s for s in w)),
        ("root port itself NOT flagged for current<max (partner decides)",
         not any(s.startswith("0000:00:01.0: speed") for s in w)),
        ("AER correctable total reported", any("aer_dev_correctable total=3" in s for s in w)),
        ("empty root port with DLActive- flagged",
         any("0000:00:02.0: empty downstream port, DLActive-" in s for s in w)),
        ("endpoint DLActive- NOT flagged (only downstream ports)",
         not any(s.startswith("0000:01:00.0: DLActive") for s in w)),
        ("conventional PCI device has link=None",
         [x for x in snap["devices"] if x["bdf"] == "0000:00:03.0"][0]["link"] is None),
        ("parent of 01:00.0 is 00:01.0",
         [x for x in snap["devices"] if x["bdf"] == "0000:01:00.0"][0]["parent"] == "0000:00:01.0"),
        ("exactly 3 warnings", len(w) == 3),
    ]
    ok = True
    for name, res in checks:
        print("%-4s %s" % ("PASS" if res else "FAIL", name))
        ok &= bool(res)
    print("warnings:", *w, sep="\n  ")
    print("ALL PASS" if ok else "SOME FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sysfs", default=SYSFS_DEFAULT, help="devices dir (default %(default)s)")
    ap.add_argument("--out", help="write JSON here instead of stdout")
    ap.add_argument("--strict", action="store_true", help="exit 1 if any warning")
    ap.add_argument("--selftest", action="store_true", help="run against a fake sysfs tree")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not os.path.isdir(a.sysfs):
        print("no %s (not Linux?) - try --selftest" % a.sysfs, file=sys.stderr)
        return 2
    snap = snapshot(a.sysfs)
    text = json.dumps(snap, indent=2)
    if a.out:
        with open(a.out, "w") as f:
            f.write(text + "\n")
    else:
        print(text)
    for w in snap["warnings"]:
        print("WARNING:", w, file=sys.stderr)
    print("%d devices, %d warnings" % (len(snap["devices"]), len(snap["warnings"])), file=sys.stderr)
    return 1 if (a.strict and snap["warnings"]) else 0


if __name__ == "__main__":
    sys.exit(main())
