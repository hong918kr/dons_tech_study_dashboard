#!/usr/bin/env python3
# ENV: linux  (Lima VM elinux: runs qemu-system-aarch64 from the /var/lab QEMU lab)
"""ELX-B01 capture helper: boot QEMU, timestamp every UART line, answer U-Boot prompts (expect-like).

Output (stdout) = the raw serial bytes, one line at a time, each prefixed with a grabserial-style
host timestamp "[  abs_s  delta_s] " (abs = seconds since QEMU was spawned = our "power on",
delta = seconds since the previous line). The line content is NOT cleaned: CR, ESC sequences
and odd bytes stay as the UART sent them (the parser in solution.py must cope).

  python3 -I capture_boot.py tfa-uboot > qemu_tfa_uboot_boot.log    # BL1 -> BL2 -> BL31 -> U-Boot -> booti -> /init
  python3 -I capture_boot.py direct    > qemu_direct_kernel_boot.log # -kernel: QEMU loads Image itself (no firmware)

QEMU lab: /var/lab/out/{flash.bin,Image,initramfs.cpio.gz} (TF-A v2.15.0, U-Boot 2026.10, Linux 6.18.55).
"""
import os
import select
import subprocess
import sys
import time

LAB = "/var/lab/out"
KADDR, RDADDR = 0x40400000, 0x44000000          # = U-Boot env kernel_addr_r / ramdisk_addr_r (qemu_arm64)
INITRD = LAB + "/initramfs.cpio.gz"
BOOTARGS = "console=ttyAMA0 b01_from=%s LAB_CMD=cat~/proc/cmdline"


def qemu_cmd(mode):
    base = ["qemu-system-aarch64", "-cpu", "cortex-a57", "-smp", "1", "-nographic", "-no-reboot"]
    if mode == "tfa-uboot":
        # secure=on + -bios flash.bin: CPU resets into TF-A BL1 at flash address 0.
        # -device loader only copies bytes into RAM before reset (it does NOT set the PC):
        # this plays the role of "fatload mmc ..." so U-Boot finds Image/initrd already in DRAM.
        return base + ["-machine", "virt,secure=on", "-m", "1057", "-bios", LAB + "/flash.bin",
                       "-device", "loader,file=%s/Image,addr=0x%x" % (LAB, KADDR),
                       "-device", "loader,file=%s,addr=0x%x" % (INITRD, RDADDR)]
    if mode == "direct":
        return base + ["-machine", "virt", "-m", "512M", "-kernel", LAB + "/Image", "-initrd", INITRD,
                       "-append", BOOTARGS % "qemu"]
    raise SystemExit("usage: capture_boot.py tfa-uboot|direct")


def uboot_script():
    size = os.path.getsize(INITRD)
    return [("Hit any key", " "),                       # stop autoboot (bootcmd=bootflow scan -lb)
            ("=> ", "printenv kernel_addr_r ramdisk_addr_r fdtcontroladdr\n"),
            ("=> ", "setenv bootargs \"%s\"\n" % (BOOTARGS % "uboot")),
            ("=> ", "booti ${kernel_addr_r} ${ramdisk_addr_r}:0x%x ${fdtcontroladdr}\n" % size)]


def main(argv):
    mode = argv[1] if len(argv) > 1 else ""
    cmd = qemu_cmd(mode)
    rules = uboot_script() if mode == "tfa-uboot" else []
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = sys.stdout.buffer
    buf, since, ri, last = b"", b"", 0, 0.0
    deadline = t0 + 240
    try:
        while time.monotonic() < deadline:
            r, _, _ = select.select([p.stdout], [], [], 0.1)
            if r:
                chunk = os.read(p.stdout.fileno(), 4096)
                if not chunk:
                    break
                buf += chunk
                since += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    now = time.monotonic() - t0
                    out.write(b"[%10.6f %9.6f] " % (now, now - last) + line + b"\n")
                    out.flush()
                    last = now
            if ri < len(rules) and rules[ri][0].encode() in since:
                time.sleep(0.05)
                p.stdin.write(rules[ri][1].encode())
                p.stdin.flush()
                ri += 1
                since = b""
            if p.poll() is not None and not r:
                break
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        if buf:
            now = time.monotonic() - t0
            out.write(b"[%10.6f %9.6f] " % (now, now - last) + buf + b"\n")
        out.flush()
    sys.stderr.write("capture: mode=%s qemu exit=%s total %.3f s\n" % (mode, p.returncode, time.monotonic() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
