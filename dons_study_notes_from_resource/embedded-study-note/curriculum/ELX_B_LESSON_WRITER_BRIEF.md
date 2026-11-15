# Lesson writer brief — track ELX (임베디드 리눅스), Basic level (ELX-B01–B13)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note
ELN  = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, prepping for
embedded/firmware/silicon-validation interviews (Rivian, Anduril, Apple, NVIDIA, Tesla...). He re-checks fundamentals
by DOING: every lesson = one interview-style coding problem whose answer is checked by a program, not by eye.
ELX-B is his **first hands-on embedded Linux track** in this site: boot flow, cross toolchain, kernel config, initramfs,
device tree, first modules/char driver/ioctl/sysfs, userspace GPIO/I2C, rootfs, systemd.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order, svg
   rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base brief: Output, Quality bar) — applies unless overridden here.
3. ROOT/curriculum/tracks/ELX.json — your item (title/goal/task/tags/env/env_note). Do the item's `task` completely
   (every tool, check, file it names), except where your lane below says what replaces a part the env can't do.
   Skim all ELX items so you stay in your lane.
4. Reference lessons: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed") and
   ROOT/lessons/ELX/ELX-M04/ (existing kernel-module lesson: md tone, `Makefile` with VARIANT, `check.sh`
   QEMU-guest recipe, dmesg grep verdict). Kernel lanes: copy and adapt M04's check.sh pattern.
5. Background facts already verified in THIS lab (read the 1–2 notes for your topic, cite facts, don't copy prose):
   ELN/C_boot/ (C1 boot map, C3 U-Boot, C5 TF-A, C6 kernel startup, C7 init), ELN/B_toolchain/ (B1 cross toolchain,
   B3 ELF), ELN/E_kernel_basics/ (E2 config/build, E3 modules, E6 user/kernel boundary), ELN/D_device_tree/ (D2 syntax),
   ELN/G_driver_model/ (G4 char device, G5 ioctl, G8 sysfs), ELN/H_hw_access_buses/ (H4 gpio, H5 i2c, H7 spi),
   ELN/K_rootfs_build/ (K1 rootfs, K2 BusyBox), ELN/L_system_update_security/2026-10-17_L1_systemd.md.
6. Already written in this site (cross-reference by id, don't re-teach): ELX-M04 (locking), ELX-M05 (hard/threaded IRQ,
   gpio-sim configfs from kernel side), ELX-M06 (bottom halves), C/DS/CPP/ALGO/ARCH/OS/COMM/RTOS/BM/SIV/ARM lessons
   (COMM-B covers I2C/SPI wire protocol; BM-B08 Cortex-M boot; OS-B process/syscall basics). `grep -l` your topic in
   ROOT/lessons. Other ELX-B items are being written IN PARALLEL right now → never depend on their files; embed a compact
   copy of anything you need (e.g. B08 embeds its own kecho driver, written to agree with B07's description below).

## Output
ROOT/lessons/ELX/<ID>/2026-11-15_<ID>_<english_snake_slug>.md + code files (below) (+ demo_*, helpers, captured
`*.txt` logs). Front matter `updated: 2026-11-15`, `time:` 50–80분. Title line `# <ID> <title exactly as in ELX.json>`.
md 450–650 lines. Q&A ≥ 6 (aim 7–8) with 꼬리질문, each `<details><summary>Q1. ...</summary>`. ≥ 2 inline ```svg figures
(viewBox, width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut only, no font-family except monospace, font-size 12–14,
nothing outside the viewBox, escape &lt; &gt; &amp;). Numbers in figures must match your code / real output.
Korean, plain words, English term in parentheses on first use. First paragraph of §1: one line on why it matters for
embedded-Linux interviews / bring-up work. Every command Don must type is given as ONE copy-pasteable line run from
the Mac (e.g. `limactl shell elinux -- bash ROOT/lessons/ELX/<ID>/check.sh solution`).

## Environments
**Mac** (B01, B05): Apple M6, macOS, Python 3.9 (`python3 -I`), Apple clang;
`export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` for C builds (27.2 SDK link broken).
Homebrew `dtc` and `qemu-system-aarch64` exist on the Mac.

**Lima VM `elinux`** (all other items): aarch64 Ubuntu 26.04, kernel 7.0, 6 vCPU, 12 GiB, running — other sessions
use it: never stop/restart it. `limactl shell elinux -- bash -lc '...'`. Passwordless sudo. The Mac home is mounted
READ-ONLY at the same path → copy to VM `/tmp/<ID>_work/` (VM /tmp is a ~2 GB tmpfs: big things go to
`/var/tmp/<ID>_work/`). Installed: gcc 15, clang, make, dtc, cross gcc `aarch64-linux-gnu-` (gcc 14/15),
`arm-linux-gnueabihf-`, `x86_64-linux-gnu-`, `riscv64-linux-gnu-` with sysroots under `/usr/<triplet>`, qemu-user
(`qemu-arm`, `qemu-aarch64`, `qemu-x86_64`, `qemu-riscv64`; no binfmt for armhf → use `qemu-arm -L /usr/arm-linux-gnueabihf`),
qemu-system-aarch64 (TCG only, no KVM), gpiod tools (libgpiod 2.2.1 runtime; **no libgpiod headers**), i2c-tools,
systemd, busybox, readelf/file/patchelf, mkfs.ext4, expect, trace-cmd. **No web installs** (no apt/pip/git clone).
**NEVER insmod anything into the VM's own running kernel** — modules load only inside a QEMU guest.

**QEMU lab** `/var/lab` (read-only; read `/var/lab/README.txt` first): Linux 6.18.55 kernels + matching build trees:
- `out/Image` + `out/linux-build` (6.18.55, defconfig + IKCONFIG_PROC, OF_OVERLAY, GPIO_SIM, DYNAMIC_DEBUG) — default for
  B04/B06–B09/B12.
- `out/Image-dbg` + `out/linux-build-dbg` (6.18.55-dbg: lockdep, DEBUG_ATOMIC_SLEEP, ftrace, GPIO_SIM=y, I2C_CHARDEV=y,
  SPI_SPIDEV=m) — use if you want lockdep/atomic-sleep checks in a driver lane (state which kernel in the md).
- `out/Image-dom` + `out/linux-build-dom` + `out/modules-dom/lib/modules/6.18.55-dom` (I2C_STUB=m, I2C_CHARDEV=y,
  SPI_SPIDEV=m, GPIO_MOCKUP=m, GPIO_SIM=y) — B11.
- `out/initramfs.cpio.gz` / `build/rootfs/` = BusyBox 1.37 static; kernel arg `LAB_CMD=cmd~with~tildes` runs cmd then
  powers off. `src/` has linux-6.18.55, busybox-1.37.0, u-boot, tfa sources. `out/u-boot.bin`, `out/flash.bin` (TF-A).
- Recipe (verified): `make -C /var/lab/out/linux-build M=$PWD modules` → copy rootfs → add .ko + static helpers
  (`gcc -static`) → cpio | gzip → `timeout N qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic
  -kernel /var/lab/out/Image -initrd i.gz -append "console=ttyAMA0 LAB_CMD=/test.sh"`. Don't mix vermagic between
  Images. 13 writers share the VM's 6 vCPUs: use `-smp 1` or `2`, `-m 512M`, generous `timeout`, small loops.

## Code & check convention (IMPORTANT — the coordinator's checker runs exactly this)
Every lesson has `starter.X`, `solution.X`, `practice.X` (X = c, py, sh as your lane says; solution = starter with only
the TODO bodies filled, same line structure, same header comment, so the diff highlights only changed lines).
- **Mac lanes (B01 Python, B05 C)**: Python 3.9 stdlib only, `python3 -I starter.py` → some FAIL, exit 1;
  `python3 -I solution.py` / `practice.py` → all PASS, exit 0; also `python3 -I -W error -X dev solution.py`.
  C: `cc -std=c11 -Wall -Wextra -Werror -O2 X.c -o /tmp/<ID>_X && /tmp/<ID>_X` (starter exit 1, solution/practice
  exit 0) + ASan/UBSan clean (`cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`).
  CHECK harness, `N/M passed`, return 1 on any failure, runs < 2 s.
- **VM lanes (all others)**: code files start with `/* ENV: linux */` (userspace in VM) or `/* ENV: qemu */` (kernel
  module / guest program) — shell/python files put the same tag in a comment on line 2 after the shebang.
  Provide `check.sh <starter|solution|practice>` that runs INSIDE the VM from any cwd: copies the lesson dir to a
  private `/tmp/<ID>_work/<variant>.$$` (or /var/tmp), builds (strict: modules with `ccflags-y := -Werror`, userspace
  `gcc -std=c11 -Wall -Wextra -Werror -O2`), runs the test (in a QEMU guest if your lane needs one, wrapped in
  `timeout`), prints PASS/FAIL lines + `N/M passed`, last line `RESULT: PASS (<variant>)` or `RESULT: FAIL (...)`,
  exit 0 / 1. starter → exit 1 within 3 minutes and never hangs; solution → exit 0 on 3 consecutive runs; practice →
  exit 0. Verdicts from counts, exact values, dmesg grep (`BUG:|WARNING:|Oops|Call trace|refcount_t`) — never timing.
  Clean up your work dir on PASS; leave no QEMU or daemon processes behind (trap EXIT). The checker runs:
  `limactl shell elinux -- bash ROOT/lessons/ELX/<ID>/check.sh starter` (and solution, practice).
- Kernel C: kernel style (tabs), `// SPDX-License-Identifier: GPL-2.0`, `MODULE_LICENSE("GPL")`, MODULE_DESCRIPTION.
  Userspace test programs use the CHECK/`N/M passed` harness and are built `gcc -static` for the guest.
- In the md: `<!-- code: starter.X -->` in §3, `<!-- code: solution.X diff=starter.X -->` in §6, `<!-- code: practice.X
  -->` in §9 (other files may get their own `<!-- code: file -->` markers, e.g. the user test program or check.sh).
  Show the exact build/run command under each.
- Paste ONLY real output you ran (dmesg, readelf, qemu console, journalctl, systemctl show, dtc). Trim with "…", never
  invent lines. Always state the kernel/guest you used ("Linux 6.18.55, QEMU TCG cortex-a57 -smp 1").
  TCG timings are not real hardware — label them.
- Private temp names only (/tmp/<ID>_*, /var/tmp/<ID>_*, on Mac and VM). Never write to a shared folder or another
  lesson dir.

## Lanes (written in parallel — stay in yours, cross-ref the others by id)
- **B01 (mac, Python)** Boot-log parser. starter/solution/practice.py: `parse_boot_log(text) -> stages` splitting a UART
  log into BootROM/SPL(=TF-A BL1/BL2 on QEMU)/BL31/U-Boot/kernel/init, extracting per-stage start/end time, load
  addresses, bootargs (`Kernel command line:`), kernel version, and printing a timeline table. Input logs: (1) a REAL log
  you capture in the VM from the TF-A+U-Boot path (`-machine virt,secure=on -m 1057 -bios /var/lab/out/flash.bin`,
  then at the U-Boot prompt load + boot the lab kernel+initramfs — e.g. `-device loader,file=...,addr=...` + `booti`,
  scripted with expect; if a full chain to init is impossible, say exactly where it stops and capture the kernel part
  with a direct `-kernel` boot as a second real log). Timestamps: U-Boot/TF-A lines have none, so record host
  arrival time per line with a small capture wrapper (grabserial-style `[ 0.123456]` prefix) — say so. (2) one
  realistic i.MX8/AM62-style log written by you, clearly labelled 합성(synthetic), for SPL/DDR-training lines the QEMU
  path lacks. Tests: golden tables for both logs, edge cases (missing stage, printk timestamp reset, CRLF, garbage
  bytes). Teach: who loads whom where (ROM→SRAM SPL→DRAM U-Boot→Image+DTB+initrd addresses→kernel→/init), what each
  passes (x0 = DTB address on arm64, bootargs via /chosen). Boundary: U-Boot env/FIT = ELX-M12, boot-time optimization
  = ELX-A08.
- **B02 (linux, C)** Cross toolchain. Task as written (shared lib + program, aarch64-linux-gnu-gcc; file/readelf
  -h -l -d; qemu-user with -L sysroot) PLUS do it for `arm-linux-gnueabihf` and `x86_64-linux-gnu` (real cross from this
  aarch64 VM; aarch64 is "native-ish" here — say so). Coding problem: `elfinfo.c` (starter/solution/practice.c) — a
  tiny ELF reader (32/64-bit, LE) printing class, machine, PT_INTERP, DT_NEEDED, DT_RUNPATH/RPATH, DT_SONAME; check.sh
  builds the lib+app for 3 triplets, runs elfinfo on each, compares with `readelf` output (parsed in the script),
  runs each app under qemu-user with `-L /usr/<triplet>` and -E LD_LIBRARY_PATH or rpath. Show the classic failures
  for real: missing interpreter (`No such file or directory` for an existing file), missing .so, wrong `-L`.
  Boundary: toolchain building/crosstool-NG = not here.
- **B03 (linux, Python)** Kconfig. A full arm64 defconfig build takes tens of minutes on this shared VM: do at most ONE
  real build only if it is cheap (e.g. `make O=/var/tmp/ELX-B03_work/b defconfig` + `scripts/config` + `olddefconfig`
  is fast and REQUIRED; a full `Image modules` build is optional, guide-only if too slow — say which you did). Use
  /var/lab/src/linux-6.18.55 read-only with `O=`. Coding problem: `diffconfig` in Python (parse `CONFIG_X=y|m|n|"str"|num`
  and `# CONFIG_X is not set`, report added/removed/changed like `scripts/diffconfig`, plus "which of my requested
  changes didn't stick after olddefconfig" — e.g. a symbol whose `depends on` is unmet). Tests on real .config files
  you generate (copy small fixtures into the lesson dir, ≤ 200 KB total) compared with the real `scripts/diffconfig`
  output. Verify in a guest: `/proc/config.gz` of out/Image (IKCONFIG_PROC) and `modinfo` (VM tool) on a lab `.ko`
  (vermagic, depends, intree). Explain Image vs vmlinux vs modules vs dtbs, y vs m, defconfig vs savedefconfig.
- **B04 (qemu, C)** initramfs boot. Build BusyBox static from /var/lab/src/busybox-1.37.0 copy (defconfig + static;
  README notes SHA*_HWACCEL must be off on arm64) or reuse /var/lab/build/busybox and say which. Coding problem:
  `init.c` = a tiny PID-1 in C (starter/solution/practice.c, built `gcc -static`): mount proc/sysfs/devtmpfs, check
  /dev/console, print self-tests (PID==1, mounts present via /proc/mounts, `/proc/cmdline` parsed, a child reaped — no
  zombies), run the BusyBox shell script given by `rdinit=`/cmdline arg non-interactively, then
  `reboot(RB_POWER_OFF)`. check.sh builds cpio (`find . | cpio -o -H newc`), boots under timeout, greps results.
  Show real failures: no /init → `Kernel panic - not syncing: No working init found` (or the exact 6.18 text you get),
  init exits → `Attempted to kill init!`, missing /dev/console. Interactive shell = shown in the md as a manual command.
- **B05 (mac, C)** Device tree. Write a virtual board DTS (cpus, memory, uart, i2c controller + sensor @0x48,
  interrupt-parent, #address-cells/#size-cells, a phandle reference); compile/decompile with Mac `dtc`. Coding problem:
  `fdtdump`-like C parser (starter/solution/practice.c) over the DTB: header fields (magic 0xd00dfeed, big-endian,
  off_dt_struct/strings, version 17), FDT_BEGIN_NODE/END_NODE/PROP/NOP/END tokens, 4-byte alignment, prop names via
  strings block, print the tree; tests compare against `dtc -I dtb -O dts` output and against hand-made malformed blobs
  (bad magic, truncated, misaligned, string offset out of range → error, never crash; ASan clean). Embed the DTB as a
  C byte array or read the file the lesson ships (say which). Boundary: overlays + schema = ELX-A05; driver matching =
  ELX-M01.
- **B06 (linux→qemu, kernel C)** Hello module: `hello` with `module_param(count, int, 0644)` (+ a string/array param if
  cheap) and a second module using the first's `EXPORT_SYMBOL_GPL` function. Guest checks: insmod order failure
  (`Unknown symbol` real text), modprobe resolving deps after `depmod -b` into the guest's /lib/modules/<ver>,
  `/sys/module/<name>/parameters/count` read/write, refcnt in `/sys/module/<name>/refcnt` / `lsmod` "Used by", rmmod
  refusal while in use, `MODULE_LICENSE` missing → taint/`GPL-only symbol` failure (real text), vermagic mismatch
  (load a module built against a different lab tree → real error). Starter/solution/practice = module sources; check.sh
  decides from guest output.
- **B07 (linux→qemu, kernel C)** `/dev/kecho` char driver: alloc_chrdev_region + cdev_add + class/device_create (or
  explain mknod if devtmpfs) with a 4096-byte buffer; write stores at *ppos, read returns from *ppos, correct partial
  read/write, `-ENOSPC` (or short write — pick and justify) past 4 KiB, llseek, `-EFAULT` on bad user pointers,
  per-device mutex for concurrent opens. User test (static, CHECK harness) in the guest: partial reads (e.g. 1/7/4096
  byte chunks), write beyond end, two fds/two processes, lseek SEEK_SET/CUR/END, bad pointer. Owner of the
  read/write/ppos semantics; B08 adds ioctl on its own copy.
- **B08 (linux→qemu, kernel C)** ioctl on a compact kecho copy: header `kecho_ioctl.h` with `_IOR/_IOW/_IO` numbers
  (magic + nr), fixed-width struct `kecho_stats` with explicit padding and `__u64` fields, `_IOC_TYPE/_IOC_NR/_IOC_SIZE`
  validation, `-ENOTTY` for unknown cmd, `-EFAULT` for bad pointer, `-EINVAL` for bad mode. User test decodes the
  ioctl numbers bit by bit (dir/size/type/nr table) and checks them against the macros. 32/64 ABI: if the guest kernel
  has CONFIG_COMPAT and cortex-a57 runs AArch32 EL0 under QEMU, build the test with `arm-linux-gnueabihf-gcc -static`
  and run it in the guest via `compat_ioctl = compat_ptr_ioctl` (show real results either way); show with `pahole`-style
  offsetof tables why `long`/pointer fields break compat.
- **B09 (linux→qemu, kernel C)** sysfs: a device (platform_device registered by the module itself, or a misc device —
  say why) with `threshold` (rw, range 0–1000, kstrtoint, -EINVAL on garbage/overflow/out-of-range) and `count` (ro)
  via `ATTRIBUTE_GROUPS`/`.dev_groups`, `sysfs_emit`, `sysfs_notify` on threshold crossing (poll() on the attribute
  from a user test if cheap). Guest test = shell script with echo/cat cases (normal, "abc", "1001", "-1", "  42\n",
  0x10, empty) + checks on the real error strings (`echo: write error: Invalid argument`). Why sysfs vs ioctl (one
  value per file, ABI docs in Documentation/ABI).
- **B10 (linux→qemu, C)** userspace GPIO. Create a gpio-sim chip via configfs in the guest (Image or Image-dbg, both
  GPIO_SIM=y; mount configfs yourself), set line names. libgpiod headers are NOT installed → write the program against
  the raw GPIO chardev **uAPI v2** ioctls (`<linux/gpio.h>`: GPIO_V2_GET_LINE_IOCTL, line config, edge events with
  timestamps, GPIO_V2_LINE_SET_VALUES) — that's what libgpiod wraps; show the equivalent libgpiod v2 code in the md
  as "참고 (이 VM엔 헤더가 없어 컴파일 안 함)". Tests: output toggle seen on the sim side (sysfs `value` of the sim
  line), input edge events when the test writes the sim `pull` attribute — count, ordering, rising/falling, seqno,
  monotonic timestamps; debounce if available. Why /sys/class/gpio is deprecated. Boundary: kernel-side IRQ = ELX-M05.
- **B11 (linux→qemu, C)** userspace I2C/SPI. Guest = Image-dom + modules-dom (`modprobe i2c-stub chip_addr=0x48`).
  Coding: `i2creg` util using `I2C_RDWR` with a 2-message write-then-read (repeated start), plus SMBus via `I2C_SMBUS`
  ioctl for comparison; tests compare against `i2cget`/`i2cset`/`i2cdump` (BusyBox applets if the lab BusyBox has
  them, else copy the VM's i2c-tools + needed libs, or build static — say which) on the same stub. Show what i2c-stub
  does and doesn't emulate (check drivers/i2c/i2c-stub.c in /var/lab/src). SPI: no SPI controller exists in QEMU virt →
  write `spi_xfer` helpers that build `struct spi_ioc_transfer[]` for SPI_IOC_MESSAGE(N) and test them on the Mac-side
  or in-VM by checking the encoding (`_IOC_SIZE(SPI_IOC_MESSAGE(2))`, field layout, cs_change, speed/bits) — label
  real-board steps as "보드가 있을 때". Boundary: wire protocol = COMM-B; kernel I2C client driver = ELX-M08.
- **B12 (qemu, Python or C)** rootfs by hand: dynamic BusyBox (build from /var/lab/src copy into /var/tmp) + glibc
  libs from the VM (`/lib/aarch64-linux-gnu`, interpreter `/lib/ld-linux-aarch64.so.1`), FHS dirs, /etc/inittab
  (BusyBox init syntax), /etc/fstab, `mkfs.ext4 -d` image (no root needed), boot `-kernel out/Image -drive
  file=X.ext4,if=virtio,format=raw -append "root=/dev/vda rw console=ttyAMA0"`. Coding problem: `libclosure` —
  compute the transitive DT_NEEDED closure + interpreter of a set of binaries by reading ELF (no `ldd`: it executes
  the binary, wrong for cross targets) and lay them out into a staging rootfs; tests compare with real `ldd` in the VM.
  The guest proves inittab sysinit/fstab mounts ran (marker lines). Show the real failure when a lib is missing.
  Boundary: Buildroot = ELX-M13; initramfs = ELX-B04.
- **B13 (linux VM systemd, C)** Type=notify fake sensor daemon in C implementing **sd_notify by hand** (AF_UNIX
  SOCK_DGRAM to `$NOTIFY_SOCKET`, abstract `@` sockets, READY=1, STATUS=, WATCHDOG=1 every WatchdogSec/2 using
  `$WATCHDOG_USEC`, STOPPING=1 on SIGTERM) — check whether libsystemd headers exist; if they do, mention `sd_notify(3)`
  but keep the hand-rolled one. Unit: After=/Wants= on a dummy dependency unit, Restart=on-failure, WatchdogSec=5,
  StartLimit*. Run on the VM's real systemd: unit files ONLY in `/run/systemd/system/esn-b13-*.service` (runtime, gone
  on reboot), unique names, `systemctl daemon-reload`, always stop + remove + daemon-reload in a trap — never touch
  other units. Hang injection (a flag file makes the daemon stop petting) → verify via `systemctl show -p
  NRestarts,Result,ActiveState,MainPID` and `journalctl -u ... --since` that systemd killed it with SIGABRT
  (`Watchdog timeout`) and restarted it (real lines). Starter daemon misses READY/WATCHDOG → check.sh FAIL (start
  timeout or watchdog kill); solution passes; keep each run < 60 s (use TimeoutStartSec small).

## Quality bar
- §1 Part A = core mechanic, B = edge cases/failures, C = bring-up-realistic scenario (board bring-up, BSP debug).
- 풀이 detailed: tables, step-by-step, figures; explain WHY with kernel source paths you actually read in
  /var/lab/src (e.g. `fs/char_dev.c`, `init/main.c`, `drivers/gpio/gpio-sim.c`).
- Accuracy over volume. Hedge and list anything from memory you couldn't run or check.

## When done
Reply (short): files written, starter/solution/practice results (exit codes + N/M), sanitizer results (Mac lanes),
how long check.sh takes, real failure texts captured, anything unverified/unsure as a list. Do NOT edit files
outside ROOT/lessons/ELX/<your ID>/ (don't run build_site.py).
