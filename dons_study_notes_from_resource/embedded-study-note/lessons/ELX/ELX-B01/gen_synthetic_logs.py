#!/usr/bin/env python3
"""ELX-B01: writes the two SYNTHETIC (합성) i.MX8M Plus-style boot logs used by the tests.

NOT captured from hardware. Message formats follow real U-Boot / TF-A / Linux source strings
(e.g. "Moving Image from 0x%lx to 0x%lx, end=0x%lx" in u-boot cmd/booti.c, "Run %s as init process"
in linux init/main.c); timings, versions and addresses are invented but plausible.

UART model (why kernel lines arrive in bursts):
  * 115200 baud 8N1 = 11520 bytes/s  -> a line of n chars (+CRLF) takes (n+2)/11520 s on the wire
  * printk lines produced before the console driver registers are buffered and flushed at that moment
  * kernel printk clock 0 = KERNEL_T0 seconds after "Starting kernel ..." (head.S/early setup before sched_clock)

  python3 -I gen_synthetic_logs.py      # rewrites imx8mp_synthetic_boot.log, imx8mp_bootloop_synthetic.log
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
BAUD_BPS = 11520.0
KERNEL_T0 = 0.062
HEADER = "#### SYNTHETIC (합성) i.MX8MP-style log for ELX-B01 - hand-written, NOT captured from hardware ####"


class Uart:
    def __init__(self):
        self.t = 0.0
        self.last = 0.0
        self.out = [HEADER]

    def emit(self, text, at=None):
        """line leaves the SoC at max(at, wire free) and fully arrives after its wire time"""
        start = self.t if at is None else max(at, self.t)
        self.t = start + (len(text) + 2) / BAUD_BPS
        self.out.append("[%10.6f %9.6f] %s" % (self.t, self.t - self.last, text))
        self.last = self.t

    def block(self, t, lines, step=0.0):
        for i, s in enumerate(lines):
            self.emit(s, t + i * step)

    def kernel(self, t_handoff, klines, console_k):
        """klines = [(ktime, text)]; lines with ktime < console_k are flushed when the console registers"""
        t0 = t_handoff + KERNEL_T0
        for k, s in klines:
            at = t0 + max(k, console_k)
            self.emit("[%12.6f] %s" % (k, s), at)
        return t0


SPL = ["U-Boot SPL 2024.04-imx (Mar 12 2026 - 10:21:44 +0000)",
       "DDRINFO: start DRAM init",
       "DDRINFO: DRAM rate 4000MTS"]
SPL2 = ["DDRINFO:ddrphy calibration done",
        "DDRINFO: ddrmix config done",
        "SEC0:  RNG instantiated",
        "Normal Boot",
        "Trying to boot from MMC2"]
BL31 = ["NOTICE:  BL31: v2.10.0  (release):lf-6.6.36-2.1.0",
        "NOTICE:  BL31: Built : 10:21:40, Mar 12 2026"]
UBOOT = ["", "U-Boot 2024.04-imx (Mar 12 2026 - 10:21:44 +0000)", "",
         "CPU:   i.MX8MP[8] rev1.1 1600 MHz (running at 1200 MHz)",
         "CPU:   Industrial temperature grade (-40C to 105C) at 38C",
         "Reset cause: %s",
         "Model: NXP i.MX8MPlus EVK board",
         "DRAM:  6 GiB"]
UBOOT2 = ["Core:  92 devices, 28 uclasses, devicetree: separate",
          "MMC:   FSL_SDHC: 1, FSL_SDHC: 2",
          "Loading Environment from MMC... OK",
          "In:    serial@30890000",
          "Out:   serial@30890000",
          "Err:   serial@30890000",
          "Net:   eth1: ethernet@30bf0000, eth0: ethernet@30be0000 [PRIME]"]
LOAD = ["switch to partitions #0, OK",
        "mmc2(part 0) is current device",
        "37748736 bytes read in 165 ms (218.2 MiB/s)",
        "Booting from mmc ...",
        "61695 bytes read in 4 ms (14.7 MiB/s)",
        "Moving Image from 0x40480000 to 0x40600000, end=0x42a00000",
        "## Flattened Device Tree blob at 43000000",
        "   Booting using the fdt blob at 0x43000000",
        "Working FDT set to 43000000",
        "   Using Device Tree in place at 0000000043000000, end 00000000430120fe",
        "Working FDT set to 43000000",
        "",
        "Starting kernel ...",
        ""]
K_EARLY = [(0.0, "Booting Linux on physical CPU 0x0000000000 [0x410fd034]"),
           (0.0, "Linux version 6.6.36-lts-next-gc4a7f6b8e2d1 (oe-user@oe-host) (aarch64-poky-linux-gcc (GCC) 13.3.0, "
                 "GNU ld (GNU Binutils) 2.42.0.20240723) #1 SMP PREEMPT Fri Mar 13 08:12:01 UTC 2026"),
           (0.0, "KASLR disabled due to lack of seed"),
           (0.0, "Machine model: NXP i.MX8MPlus EVK board"),
           (0.0, "Reserved memory: created CMA memory pool at 0x00000000d6000000, size 640 MiB"),
           (0.0, "psci: probing for conduit method from DT."),
           (0.0, "psci: PSCIv1.1 detected in firmware."),
           (0.0, "Kernel command line: %s"),
           (0.000005, "sched_clock: 56 bits at 8MHz, resolution 125ns, wraps every 2199023255500ns"),
           (0.000330, "Console: colour dummy device 80x25"),
           (0.031512, "smp: Brought up 1 node, 4 CPUs"),
           (0.044781, "CPU: All CPU(s) started at EL2"),
           (0.208033, "30890000.serial: ttymxc1 at MMIO 0x30890000 (irq = 17, base_baud = 5000000) is a IMX"),
           (0.208112, "printk: console [ttymxc1] enabled")]
CONSOLE_K = 0.208112
K_MMC = [(0.912634, "mmc2: new HS400 Enhanced strobe MMC card at address 0001"),
         (0.913420, "mmcblk2: mmc2:0001 IB2932 29.1 GiB"),
         (0.915811, " mmcblk2: p1 p2")]
K_ROOT_OK = [(1.402271, "EXT4-fs (mmcblk2p2): mounted filesystem 8c1e5a2f-3b7d-4f0e-9a51-2d6c0e7b9f13 r/w with ordered data mode."),
             (1.402410, "VFS: Mounted root (ext4 filesystem) on device 179:2."),
             (1.410020, "devtmpfs: mounted"),
             (1.411734, "Freeing unused kernel memory: 4224K"),
             (1.412001, "Run /sbin/init as init process"),
             (1.871203, "systemd[1]: systemd 255.4^ running in system mode (+PAM +AUDIT -SELINUX -APPARMOR +IMA)"),
             (1.904315, "systemd[1]: Detected architecture arm64.")]
USER = [(2.2, ""),
        (2.2, "Welcome to \x1b[1mNXP i.MX Release Distro 6.6-scarthgap (scarthgap)\x1b[0m!"),
        (2.2, ""),
        (2.9, "[\x1b[0;32m  OK  \x1b[0m] Created slice Slice /system/getty."),
        (5.6, "[\x1b[0;32m  OK  \x1b[0m] Reached target Multi-User System."),
        (5.7, ""),
        (5.7, "NXP i.MX Release Distro 6.6-scarthgap imx8mpevk ttymxc1"),
        (5.7, ""),
        (5.71, "imx8mpevk login: ")]
ARGS_OK = "console=ttymxc1,115200 root=/dev/mmcblk2p2 rootwait rw"
ARGS_BAD = "console=ttymxc1,115200 root=/dev/mmcblk2p2 rw"


def good_boot(u, t, reset_cause, args):
    u.block(t, SPL)
    u.block(t + 0.657, SPL2, 0.004)
    u.block(t + 0.743, BL31)
    u.block(t + 0.798, [s % reset_cause if "%s" in s else s for s in UBOOT], 0.001)
    u.block(t + 0.876, UBOOT2, 0.012)
    u.emit("Hit any key to stop autoboot:  0", t + 2.004)       # bootdelay=1 counted down
    u.block(t + 2.030, LOAD[:2])
    u.block(t + 2.198, LOAD[2:5], 0.002)
    u.block(t + 2.240, LOAD[5:], 0.0005)
    t_handoff = u.t
    kl = [(k, s % args if "%s" in s else s) for k, s in K_EARLY]
    return t_handoff, kl


def write(name, lines):
    with open(os.path.join(HERE, name), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    # ---- one good boot (golden table) ----
    u = Uart()
    th, kl = good_boot(u, 0.212, "POR", ARGS_OK)
    t0 = u.kernel(th, kl + K_MMC + K_ROOT_OK, CONSOLE_K)
    for k, s in USER:
        u.emit(s, t0 + k)
    write("imx8mp_synthetic_boot.log", u.out)

    # ---- boot loop: (1) no 'rootwait' -> root mounted before eMMC probe -> panic -> WDOG reset,
    #      (2) SPL can't load -> hang -> power cycle, (3) OK ----
    # (1) rootfs panic -> WDOG reset, (2) SPL can't load -> hang -> power cycle, (3) OK ----
    u = Uart()
    th, kl = good_boot(u, 0.212, "POR", ARGS_BAD)
    t0 = u.kernel(th, kl + [
        (0.921400, "VFS: Cannot open root device \"/dev/mmcblk2p2\" or unknown-block(0,0): error -6"),
        (0.921455, "Please append a correct \"root=\" boot option; here are the available partitions:"),
        (0.921530, "Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)"),
        (0.921601, "CPU: 2 PID: 1 Comm: swapper/0 Not tainted 6.6.36-lts-next-gc4a7f6b8e2d1 #1"),
        (0.921870, "---[ end Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0) ]---")],
        CONSOLE_K)
    t2 = t0 + 0.92 + 60.0 + 0.212                           # i.MX WDOG (60 s) resets the SoC
    u.block(t2, SPL)
    u.block(t2 + 0.657, SPL2, 0.004)
    u.block(t2 + 0.912, ["SPL: failed to boot from all boot devices", "### ERROR ### Please RESET the board ###"])
    t3 = t2 + 31.4                                           # operator power-cycles the board
    th, kl = good_boot(u, t3, "POR", ARGS_OK)
    t0 = u.kernel(th, kl + K_MMC + K_ROOT_OK, CONSOLE_K)
    for k, s in USER[:2] + USER[4:]:
        u.emit(s, t0 + k)
    write("imx8mp_bootloop_synthetic.log", u.out)
    print("wrote imx8mp_synthetic_boot.log, imx8mp_bootloop_synthetic.log")


if __name__ == "__main__":
    main()
