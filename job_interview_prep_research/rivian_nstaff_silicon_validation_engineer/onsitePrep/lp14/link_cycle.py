#!/usr/bin/env python3
"""link_cycle.py - PCIe link-training stress harness skeleton (LP14).

Loop N times:  trigger (reset / remove+rescan / link disable / external power hook)
               -> wait for the link + device to come back (poll, with timeout)
               -> read speed / width / DLActive / AER counters
               -> judge PASS/FAIL against expectations -> one CSV row
               -> on FAIL: run a log-collection hook (dmesg, lspci -vvv, ...)
Then print a summary: failure rate + exact 95% upper confidence bound
(Clopper-Pearson; with 0 failures this is the "rule of three" ~ 3/N).

Backends
  sysfs : real Linux (root). Uses /sys/bus/pci + config-space reads (no lspci needed).
  sim   : simulated link (runs anywhere, e.g. macOS) - for developing the harness
          and for proving that the harness itself catches failures (inject them!).

Examples
  python3 link_cycle.py --sim --cycles 2000 --sim-fail-rate 0.002 --seed 1
  sudo python3 link_cycle.py --bdf 0000:01:00.0 --method remove-rescan --cycles 1000 \
       --expect-speed 16 --expect-width 4 --csv run.csv --on-fail 'dmesg | tail -50'
  python3 link_cycle.py --stats 0 1000        # just the statistics
"""
import argparse
import csv
import math
import os
import random
import struct
import subprocess
import sys
import time

# ----------------------------------------------------------------------------- statistics


def binom_cdf(k, n, p):
    """P(X <= k), X ~ Binomial(n, p). Summed in log space (n up to ~1e6 is fine)."""
    if p <= 0:
        return 1.0
    if p >= 1:
        return 0.0 if k < n else 1.0
    total, lp, lq = 0.0, math.log(p), math.log1p(-p)
    for i in range(0, k + 1):
        logc = math.lgamma(n + 1) - math.lgamma(i + 1) - math.lgamma(n - i + 1)
        total += math.exp(logc + i * lp + (n - i) * lq)
    return min(total, 1.0)


def upper_bound(k, n, conf=0.95):
    """Exact (Clopper-Pearson one-sided) upper bound on the failure rate p:
    the largest p such that seeing <= k failures in n trials still has
    probability >= 1-conf. Found by bisection on binom_cdf (monotone in p)."""
    if n == 0:
        return 1.0
    alpha = 1.0 - conf
    if k >= n:
        return 1.0
    lo, hi = 0.0, 1.0
    for _ in range(200):
        mid = (lo + hi) / 2
        if binom_cdf(k, n, mid) > alpha:
            lo = mid
        else:
            hi = mid
    return hi


def cycles_needed(p_target, conf=0.95):
    """Zero-failure cycles needed to claim p < p_target at `conf` confidence:
    (1-p)^N <= 1-conf  ->  N >= ln(1-conf) / ln(1-p)  ~ 3/p for conf=0.95."""
    return math.ceil(math.log(1 - conf) / math.log1p(-p_target))


def stats_report(k, n, conf=0.95):
    ub = upper_bound(k, n, conf)
    lines = ["failures %d / %d cycles = %.4f%%" % (k, n, 100.0 * k / n if n else 0.0),
             "%.0f%% upper bound on failure rate: %.5f (= 1 in %.0f)"
             % (conf * 100, ub, 1 / ub if ub else float("inf"))]
    if k == 0:
        lines.append("rule of three check: 3/N = %.5f, exact 1-0.05^(1/N) = %.5f"
                     % (3.0 / n, 1 - (1 - conf) ** (1.0 / n)))
    return lines


# ----------------------------------------------------------------------------- backends
PCI_CAP_ID_EXP, PCI_EXP_LNKCTL, PCI_EXP_LNKSTA = 0x10, 0x10, 0x12
LNKSTA_DLLLA, LNKSTA_LT, LNKCTL_LD = 0x2000, 0x0800, 0x0010
SPEED = {1: 2.5, 2: 5.0, 3: 8.0, 4: 16.0, 5: 32.0, 6: 64.0}


class SysfsBackend:
    """Real hardware via sysfs. Needs root (config writes, remove/rescan, reset)."""

    def __init__(self, bdf, port=None, method="remove-rescan", hook=None, root="/sys/bus/pci"):
        self.root, self.bdf, self.method, self.hook = root, bdf, method, hook
        self.port = port or self._parent_of(bdf)
        if not self.port:
            raise SystemExit("cannot find the downstream port above %s (give --port)" % bdf)

    # -- helpers
    def _dev(self, bdf):
        return os.path.join(self.root, "devices", bdf)

    def _parent_of(self, bdf):
        up = os.path.basename(os.path.dirname(os.path.realpath(self._dev(bdf))))
        return up if up.count(":") == 2 else None

    def _write(self, path, text):
        with open(path, "w") as f:
            f.write(text)

    def _cfg_rw(self, bdf, off, size, value=None):
        with open(os.path.join(self._dev(bdf), "config"), "r+b" if value is not None else "rb") as f:
            f.seek(off)
            fmt = {1: "<B", 2: "<H", 4: "<I"}[size]
            if value is None:
                data = f.read(size)
                return struct.unpack(fmt, data)[0] if len(data) == size else None
            f.write(struct.pack(fmt, value))
            return value

    def _pcie_cap(self, bdf):
        ptr, guard = self._cfg_rw(bdf, 0x34, 1) or 0, 48
        while ptr and guard:
            if self._cfg_rw(bdf, ptr, 1) == PCI_CAP_ID_EXP:
                return ptr
            ptr, guard = (self._cfg_rw(bdf, ptr + 1, 1) or 0) & 0xFC, guard - 1
        return None

    # -- interface used by the harness
    def trigger(self):
        m, dev, port = self.method, self._dev(self.bdf), self._dev(self.port)
        if m == "remove-rescan":           # SW-only: drop the device and re-enumerate
            if os.path.exists(dev):
                self._write(os.path.join(dev, "remove"), "1")
            self._write(os.path.join(self.root, "rescan"), "1")
        elif m == "flr":                   # function reset via the kernel (reset_method order)
            self._write(os.path.join(dev, "reset"), "1")
        elif m == "sbr":                   # Secondary Bus Reset on the port -> hot reset of the link
            self._write(os.path.join(port, "reset_subordinate"), "1")
        elif m == "link-disable":          # LnkCtl.LinkDisable 1 -> 0 on the port, then rescan
            cap = self._pcie_cap(self.port)
            v = self._cfg_rw(self.port, cap + PCI_EXP_LNKCTL, 2)
            if os.path.exists(dev):
                self._write(os.path.join(dev, "remove"), "1")
            self._cfg_rw(self.port, cap + PCI_EXP_LNKCTL, 2, v | LNKCTL_LD)
            time.sleep(0.05)
            self._cfg_rw(self.port, cap + PCI_EXP_LNKCTL, 2, v & ~LNKCTL_LD)
            time.sleep(0.1)
            self._write(os.path.join(self.root, "rescan"), "1")
        elif m == "hook":                  # external: PDU power cycle, PERST# GPIO, relay board...
            subprocess.run(self.hook, shell=True, check=True)
            self._write(os.path.join(self.root, "rescan"), "1")
        else:
            raise ValueError(m)

    def read_state(self):
        st = {"present": os.path.exists(self._dev(self.bdf))}
        cap = self._pcie_cap(self.port)
        sta = self._cfg_rw(self.port, cap + PCI_EXP_LNKSTA, 2) if cap else None
        if sta is not None and sta != 0xFFFF:
            st.update(speed=SPEED.get(sta & 0xF), width=(sta >> 4) & 0x3F,
                      dl_active=bool(sta & LNKSTA_DLLLA), training=bool(sta & LNKSTA_LT))
        if st["present"]:
            vid = self._cfg_rw(self.bdf, 0, 2)
            st["vendor_ok"] = vid not in (None, 0xFFFF)
            aer = os.path.join(self._dev(self.port), "aer_dev_correctable")
            if os.path.exists(aer):
                for line in open(aer):
                    if line.startswith("TOTAL_ERR_COR"):
                        st["aer_cor"] = int(line.split()[-1])
        return st


class SimBackend:
    """Simulated link: trains to the expected speed/width, except with probability
    fail_rate it fails in one of the classic ways. Deterministic with --seed."""
    MODES = ("no_link", "downtrain_speed", "downtrain_width", "slow_train", "aer_burst")

    def __init__(self, exp_speed, exp_width, fail_rate, seed):
        self.rng = random.Random(seed)
        self.exp_speed, self.exp_width, self.fail_rate = exp_speed, exp_width, fail_rate
        self.aer = 0
        # initial state: link up and healthy (before the first trigger)
        self.state = {"present": True, "vendor_ok": True, "speed": exp_speed, "width": exp_width,
                      "dl_active": True, "training": False, "aer_cor": 0}
        self.t_up, self.t0 = 0.0, time.monotonic()

    def trigger(self):
        fail = self.rng.random() < self.fail_rate
        mode = self.rng.choice(self.MODES) if fail else None
        t_up = self.rng.uniform(0.002, 0.010)                 # "link up" time
        st = {"present": True, "vendor_ok": True, "speed": self.exp_speed,
              "width": self.exp_width, "dl_active": True, "training": False}
        if mode == "no_link":
            st.update(present=False, dl_active=False, width=0, training=True)
            t_up = None
        elif mode == "downtrain_speed":
            st["speed"] = 2.5
        elif mode == "downtrain_width":
            st["width"] = max(1, self.exp_width // 2)
        elif mode == "slow_train":
            t_up = 5.0                                         # longer than --timeout
        elif mode == "aer_burst":
            self.aer += self.rng.randint(5, 50)
        st["aer_cor"] = self.aer
        self.state, self.t_up, self.t0 = st, t_up, time.monotonic()

    def read_state(self):
        if self.t_up is None or time.monotonic() - self.t0 < self.t_up:
            return {"present": False, "dl_active": False, "width": 0, "training": True,
                    "aer_cor": self.aer}
        return dict(self.state)


# ----------------------------------------------------------------------------- harness


def wait_link(backend, timeout, poll):
    """Poll until device present + DLActive (+ vendor ID readable) or timeout."""
    t0 = time.monotonic()
    st = backend.read_state()
    while time.monotonic() - t0 < timeout:
        if st.get("present") and st.get("dl_active") and st.get("vendor_ok", True):
            return st, time.monotonic() - t0
        time.sleep(poll)
        st = backend.read_state()
    return st, None


def judge(st, t_up, exp_speed, exp_width, aer_before, aer_limit):
    reasons = []
    if t_up is None:
        reasons.append("timeout: present=%s DLActive=%s Train=%s"
                       % (st.get("present"), st.get("dl_active"), st.get("training")))
    else:
        if exp_speed and st.get("speed") != exp_speed:
            reasons.append("speed %s != %s" % (st.get("speed"), exp_speed))
        if exp_width and st.get("width") != exp_width:
            reasons.append("width x%s != x%s" % (st.get("width"), exp_width))
    d_aer = None
    if aer_before is not None and st.get("aer_cor") is not None:
        d_aer = st["aer_cor"] - aer_before
        if d_aer > aer_limit:
            reasons.append("AER correctable +%d > %d" % (d_aer, aer_limit))
    return ("PASS" if not reasons else "FAIL"), "; ".join(reasons), d_aer


def run(args):
    if args.sim:
        be = SimBackend(args.expect_speed or 16.0, args.expect_width or 4,
                        args.sim_fail_rate, args.seed)
    else:
        be = SysfsBackend(args.bdf, args.port, args.method, args.hook)
    out = open(args.csv, "w", newline="") if args.csv else None
    w = csv.writer(out) if out else None
    if w:
        w.writerow(["cycle", "verdict", "t_up_ms", "speed_gts", "width", "dl_active",
                    "aer_cor_delta", "reason"])
    fails, first_fail, t_ups = 0, None, []
    aer_prev = be.read_state().get("aer_cor")
    for c in range(1, args.cycles + 1):
        be.trigger()
        st, t_up = wait_link(be, args.timeout, args.poll)
        verdict, reason, d_aer = judge(st, t_up, args.expect_speed, args.expect_width,
                                       aer_prev, args.aer_limit)
        aer_prev = st.get("aer_cor", aer_prev)
        if t_up is not None:
            t_ups.append(t_up)
        if w:
            w.writerow([c, verdict, "%.2f" % (t_up * 1e3) if t_up is not None else "",
                        st.get("speed"), st.get("width"), st.get("dl_active"), d_aer, reason])
        if verdict == "FAIL":
            fails += 1
            first_fail = first_fail or c
            print("cycle %5d FAIL  %s" % (c, reason))
            if args.on_fail:   # collect evidence NOW, while the bad state still exists
                subprocess.run(args.on_fail.replace("{cycle}", str(c)), shell=True)
            if args.stop_on_fail:
                break
        elif args.verbose:
            print("cycle %5d PASS  %.1f GT/s x%s  t_up=%.1f ms"
                  % (c, st.get("speed"), st.get("width"), t_up * 1e3))
        if args.settle:
            time.sleep(args.settle)
    n = c
    if out:
        out.close()
    print("-" * 64)
    print("method=%s backend=%s cycles=%d" % (args.method if not args.sim else "sim",
                                              "sim" if args.sim else "sysfs", n))
    for line in stats_report(fails, n):
        print(line)
    if first_fail:
        print("first failure at cycle %d" % first_fail)
    if t_ups:
        t_ups.sort()
        print("link-up time ms: p50=%.2f p99=%.2f max=%.2f"
              % (t_ups[len(t_ups) // 2] * 1e3, t_ups[min(len(t_ups) - 1, int(len(t_ups) * .99))] * 1e3,
                 t_ups[-1] * 1e3))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description="PCIe link-training stress harness (skeleton)")
    ap.add_argument("--bdf", help="endpoint BDF, e.g. 0000:01:00.0")
    ap.add_argument("--port", help="downstream port above it (default: sysfs parent)")
    ap.add_argument("--method", default="remove-rescan",
                    choices=["remove-rescan", "flr", "sbr", "link-disable", "hook"])
    ap.add_argument("--hook", help="shell command for --method hook (power cycle / PERST#)")
    ap.add_argument("--cycles", type=int, default=100)
    ap.add_argument("--expect-speed", type=float, help="GT/s, e.g. 16")
    ap.add_argument("--expect-width", type=int, help="lanes, e.g. 4")
    ap.add_argument("--aer-limit", type=int, default=0, help="allowed AER correctable per cycle")
    ap.add_argument("--timeout", type=float, default=2.0, help="s to wait for link-up")
    ap.add_argument("--poll", type=float, default=0.001)
    ap.add_argument("--settle", type=float, default=0.0, help="s to sleep between cycles")
    ap.add_argument("--csv")
    ap.add_argument("--on-fail", help="shell command run on each failure ({cycle} expands)")
    ap.add_argument("--stop-on-fail", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--sim", "--dry-run", dest="sim", action="store_true",
                    help="simulated link backend (no hardware, runs on macOS)")
    ap.add_argument("--sim-fail-rate", type=float, default=0.002)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--stats", nargs=2, type=int, metavar=("FAILS", "CYCLES"),
                    help="only print the statistics for FAILS out of CYCLES")
    ap.add_argument("--need", type=float, metavar="P",
                    help="only print zero-failure cycles needed to claim rate < P (95%%)")
    a = ap.parse_args()
    if a.stats:
        print("\n".join(stats_report(*a.stats)))
        return 0
    if a.need:
        print("zero-failure cycles to claim p < %g at 95%%: %d (3/p = %.0f)"
              % (a.need, cycles_needed(a.need), 3 / a.need))
        return 0
    if not a.sim and not a.bdf:
        ap.error("--bdf is required unless --sim")
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
