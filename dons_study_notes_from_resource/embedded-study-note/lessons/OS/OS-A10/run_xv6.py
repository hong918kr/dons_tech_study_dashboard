#!/usr/bin/env python3
"""OS-A10 driver: boot xv6 in QEMU, run test programs at the shell, judge output.

usage: run_xv6.py <xv6-dir> <program>[,<program>...] [--usertests] [--cpus N]

Boots `make qemu CPUS=N` in <xv6-dir> with stdin/stdout on pipes (stdout
non-blocking), waits for the "$ " prompt, then runs each program and expects
lines "PASS <name>" / "FAIL <name>: why" and a final "<program>: done".
Expected test names are listed below, so a test that never printed counts as
FAIL. --usertests also runs `usertests -q` (PASS iff "ALL TESTS PASSED").
A kernel "panic" or a timeout fails everything still pending. QEMU is always
killed (its whole process group). Prints PASS/FAIL lines, then "N/M passed";
exit status 0 only if everything passed. Python stdlib only.
"""
import os
import re
import signal
import subprocess
import sys
import time

EXPECTED = {
    "cowtest": [
        "simple", "fork-cheap", "write-cost", "fork-no-leak", "isolation",
        "isolation-no-leak", "pipe-copyout", "wait-copyout", "copyout-readonly",
        "reuse-refcount-1", "reuse-at-oom", "oom-kill", "kill-beyond-sz", "kill-text-write",
        "kill-trampoline", "kill-maxva", "kill-high-va", "fork-loop-no-leak",
    ],
    "cowpractice": [
        "stat-copies", "stat-reuses", "fork-exec-copies", "zero-page-read",
        "zero-page-write", "zero-page-fork", "audit-clean", "audit-detects",
        "audit-after-exit",
    ],
}
BOOT_TIMEOUT = 30
PROG_TIMEOUT = 120
USERTESTS_TIMEOUT = 600


class Xv6:
    def __init__(self, xv6dir, cpus):
        self.log = open(os.path.join(xv6dir, "console.log"), "wb")
        self.proc = subprocess.Popen(
            ["make", "-s", "qemu", "CPUS=%d" % cpus], cwd=xv6dir,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=True)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.buf = bytearray()
        self.mark = 0  # start of the output that belongs to the current command

    def text(self):
        return self.buf.decode("utf-8", "replace")

    def pump(self):
        while True:
            try:
                b = os.read(self.proc.stdout.fileno(), 65536)
            except BlockingIOError:
                return True
            if not b:
                return False  # QEMU (or make) exited
            self.buf.extend(b)
            self.log.write(b)
            self.log.flush()

    def wait_for(self, regex, timeout):
        """Wait until regex matches output since mark. Returns 'ok', 'panic',
        'timeout' or 'exited'."""
        deadline = time.monotonic() + timeout
        rx = re.compile(regex, re.M)
        while True:
            alive = self.pump()
            out = self.buf[self.mark:].decode("utf-8", "replace")
            if rx.search(out):
                return "ok"
            if re.search(r"^panic", out, re.M) or "\npanic:" in out:
                return "panic"
            if not alive:
                return "exited"
            if time.monotonic() > deadline:
                return "timeout"
            time.sleep(0.05)

    def run(self, cmd):
        self.mark = len(self.buf)
        self.proc.stdin.write((cmd + "\n").encode())
        self.proc.stdin.flush()

    def since_mark(self):
        return self.buf[self.mark:].decode("utf-8", "replace")

    def kill(self):
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.proc.wait()
        self.log.close()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 2
    xv6dir, progs = args[0], args[1].split(",")
    cpus = 3
    for f in flags:
        if f.startswith("--cpus="):
            cpus = int(f.split("=", 1)[1])
    results = []  # (name, ok, note)
    t0 = time.monotonic()
    q = Xv6(xv6dir, cpus)
    try:
        st = q.wait_for(r"^\$ ", BOOT_TIMEOUT)
        if st != "ok":
            print("boot failed: %s" % st)
            print(q.text()[-2000:])
            results.append(("boot", False, st))
        else:
            fatal = None
            for prog in progs:
                names = EXPECTED[prog]
                if fatal:
                    results += [(prog + "/" + n, False, fatal) for n in names]
                    continue
                t1 = time.monotonic()
                q.run(prog)
                st = q.wait_for(r"^%s: done" % re.escape(prog), PROG_TIMEOUT)
                out = q.since_mark()
                for line in out.splitlines():
                    if line.startswith("  ") or line.startswith(prog + ":"):
                        print("    | " + line.strip())
                passed = set(re.findall(r"^PASS (\S+)", out, re.M))
                why = dict(re.findall(r"^FAIL (\S+): (.*)$", out, re.M))
                for n in names:
                    note = why.get(n, "no PASS line (%s)" % st if st != "ok" else "")
                    results.append((prog + "/" + n, n in passed, note))
                print("    (%s took %.1f s)" % (prog, time.monotonic() - t1))
                if st != "ok":
                    fatal = "%s during %s" % (st, prog)
                    print(out[-1500:])
                else:
                    q.wait_for(r"^\$ ", 10)
            if "--usertests" in flags:
                if fatal:
                    results.append(("usertests -q", False, fatal))
                else:
                    t1 = time.monotonic()
                    q.run("usertests -q")
                    st = q.wait_for(r"^(ALL TESTS PASSED|SOME TESTS FAILED)",
                                    USERTESTS_TIMEOUT)
                    out = q.since_mark()
                    ok = st == "ok" and "ALL TESTS PASSED" in out
                    if not ok:
                        print(out[-2500:])
                    results.append(("usertests -q", ok,
                                    "" if ok else "%s / see console.log" % st))
                    print("    (usertests -q took %.1f s)" % (time.monotonic() - t1))
    finally:
        q.kill()
    npass = 0
    for name, ok, note in results:
        npass += ok
        print("%s %s%s" % ("PASS" if ok else "FAIL", name,
                           "" if ok or not note else "  (" + note + ")"))
    print("%d/%d passed  (QEMU CPUS=%d, wall %.1f s)" %
          (npass, len(results), cpus, time.monotonic() - t0))
    return 0 if npass == len(results) and results else 1


if __name__ == "__main__":
    sys.exit(main())
