#!/usr/bin/env python3
"""OS-A09 grader: boot xv6 once, type commands into its shell, check output.

usage (cwd = a built xv6 tree, check.sh does this):
    python3 run_xv6.py starter|solution|practice

- boots `make qemu CPUS=1` in its own process group (killed with SIGKILL at
  the end, whatever happens), stdout non-blocking, stdin = pipe = the UART;
- each command waits for the next "$ " prompt with a timeout, so a broken
  kernel makes the run FAIL, never hang;
- guest programs print "PASS: ..."/"FAIL: ..." lines; this script adds its
  own [host] checks on the kernel's trace lines; final line "N/M passed".
The console log is saved to ./console.log.
"""
import os, re, select, signal, subprocess, sys, time

VARIANT = sys.argv[1] if len(sys.argv) > 1 else "solution"
TRACE = re.compile(r"^(\d+): syscall (\w+)(\(.*\))? -> (-?\d+)$")

results = []  # (ok, name)


def check(ok, name, detail=""):
    results.append((bool(ok), name))
    print(("PASS: " if ok else "FAIL: ") + name + ("" if ok or not detail else "  [" + detail + "]"),
          flush=True)


class Xv6:
    def __init__(self):
        self.p = subprocess.Popen(["make", "qemu", "CPUS=1"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  start_new_session=True)
        os.set_blocking(self.p.stdout.fileno(), False)
        self.buf = bytearray()

    def text(self, start=0):
        return self.buf[start:].decode("utf-8", "replace").replace("\r", "")

    def pump(self, wait):
        r, _, _ = select.select([self.p.stdout], [], [], wait)
        if r:
            try:
                chunk = os.read(self.p.stdout.fileno(), 65536)
            except BlockingIOError:
                return True
            if not chunk:
                return False  # qemu/make exited
            self.buf.extend(chunk)
        return True

    def wait_for(self, pred, start, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if pred(self.text(start)):
                return True
            if re.search(r"(^|\n)panic: ", self.text(start)):
                return False
            if not self.pump(0.2):
                return pred(self.text(start))
        return False

    def boot(self, timeout=60):
        return self.wait_for(lambda t: "init: starting sh" in t and t.endswith("$ "), 0, timeout)

    def run(self, cmd, timeout=30):
        """type cmd, return (ok, output lines after the echoed command)."""
        start = len(self.buf)
        self.p.stdin.write((cmd + "\n").encode())
        self.p.stdin.flush()
        echo = cmd + "\n"
        ok = self.wait_for(lambda t: echo in t and t.endswith("$ ") and
                           len(t) > t.index(echo) + len(echo), start, timeout)
        t = self.text(start)
        body = t[t.index(echo) + len(echo):] if echo in t else t
        if ok:
            body = body[:-2]
        return ok, body.splitlines()

    def kill(self):
        try:
            os.killpg(self.p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.p.wait()
        with open("console.log", "w") as f:
            f.write(self.text())


def traces(lines):
    out = []
    for l in lines:
        m = TRACE.match(l)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3) or "", int(m.group(4))))
    return out


def guest(lines, tag):
    """count the guest program's own PASS/FAIL lines."""
    n = 0
    for l in lines:
        if l.startswith("PASS: ") or l.startswith("FAIL: "):
            check(l.startswith("PASS: "), "[guest " + tag + "] " + l[6:])
            n += 1
    return n


def field(lines, rex):
    for l in lines:
        m = re.search(rex, l)
        if m:
            return [int(g) for g in m.groups()]
    return None


class Stop(Exception):
    pass


def cmd(q, c, timeout=30):
    ok, lines = q.run(c, timeout)
    if not ok:
        check(False, "command finished: " + c, "no prompt within %ds (panic or hang)" % timeout)
        raise Stop()
    return lines


def t_trace_basic(q):
    lines = cmd(q, "tracetest basic")
    guest(lines, "tracetest basic")
    v = field(lines, r"basic: pid=(\d+) fd=(-?\d+)")
    got = [(p, n, r) for p, n, _, r in traces(lines)]
    if v is None:
        check(False, "[host] tracetest basic printed pid/fd")
        return
    pid, fd = v
    want = [(pid, "trace", 0), (pid, "open", fd), (pid, "write", 5), (pid, "close", 0),
            (pid, "open", -1), (pid, "getpid", pid)]
    check(got == want, "[host] trace lines of one process match exactly (6 lines)",
          "got %s" % got)


def t_trace_fork(q):
    lines = cmd(q, "tracetest fork")
    guest(lines, "tracetest fork")
    v = field(lines, r"fork: parent=(\d+) child=(-?\d+)")
    if v is None:
        check(False, "[host] tracetest fork printed pids")
        return
    par, ch = v
    tr = traces(lines)
    pseq = [(n, r) for p, n, _, r in tr if p == par]
    cseq = [(n, r) for p, n, _, r in tr if p == ch]
    other = [x for x in tr if x[0] not in (par, ch)]
    check(pseq == [("fork", ch), ("wait", ch), ("getpid", par)],
          "[host] parent: fork -> child, wait -> child, getpid -> parent", "got %s" % pseq)
    check(cseq == [("getpid", ch), ("getpid", ch)],
          "[host] child inherited the mask: 2 x getpid (and no 'fork -> 0' line)", "got %s" % cseq)
    check(len(tr) > 0 and not other, "[host] no trace lines from other processes", "got %s" % other)


def t_trace_cmd(q):
    size = os.path.getsize("README")
    want = [512] * (size // 512) + ([size % 512] if size % 512 else []) + [0]
    lines = cmd(q, "trace 32 wc README")  # 32 = 1 << SYS_read
    tr = traces(lines)
    pids = {p for p, _, _, _ in tr}
    got = [r for _, n, _, r in tr if n == "read"]
    check(len(pids) == 1 and got == want and len(tr) == len(want),
          "[host] trace 32 wc README: read -> %s" % want, "got %s" % [(p, n, r) for p, n, _, r in tr])
    check(any(l.endswith(" %d README" % size) for l in lines), "[host] wc still prints its count")
    lines = cmd(q, "wc README")
    check(not traces(lines) and any(l.endswith(" %d README" % size) for l in lines),
          "[host] mask stays in trace's process tree: plain wc prints no trace lines")


def t_sysinfo(q):
    lines = cmd(q, "sysinfotest")
    n = guest(lines, "sysinfotest")
    check(n > 0 and any(l == "sysinfotest: %d passed, 0 failed" % n for l in lines),
          "[host] sysinfotest finished with 0 failed")


def t_usertests(q):
    t0 = time.time()
    lines = cmd(q, "usertests -q", timeout=300)
    dt = time.time() - t0
    ok = any(l.startswith("ALL TESTS PASSED") for l in lines)
    print("usertests -q: %.0f s" % dt, flush=True)
    check(ok, "[host] usertests -q: ALL TESTS PASSED",
          "; ".join(l for l in lines if "FAIL" in l)[:300])


def t_practice(q):
    lines = cmd(q, "practicetest args")
    guest(lines, "practicetest args")
    v = field(lines, r"args: pid=(\d+) fd=(-?\d+) flags=(\d+)")
    if v is None:
        check(False, "[host] practicetest args printed pid/fd")
    else:
        pid, fd, fl = v
        want = ['%d: syscall open("pt.args", %d) -> %d' % (pid, fl, fd),
                '%d: syscall read(%d, 10) -> 0' % (pid, fd),
                '%d: syscall close -> 0' % pid,
                '%d: syscall unlink("pt.args") -> 0' % pid]
        got = [l for l in lines if TRACE.match(l)]
        check(got == want, "[host] P1 traceargs: exact lines with arguments", "got %s" % got)
    for c in ("files", "clock", "procs"):
        lines = cmd(q, "practicetest " + c)
        n = guest(lines, "practicetest " + c)
        check(n > 0 and any(l == "practicetest: %d passed, 0 failed" % n for l in lines),
              "[host] practicetest %s finished with 0 failed" % c)
    lines = cmd(q, "ps")
    rows = [l.split() for l in lines[1:] if l and l[0].isdigit()]
    names = {(r[0], r[1], r[2]) for r in rows if len(r) == 4}
    check(lines[:1] == ["PID STATE NAME SZ"] and ("1", "sleep", "init") in names and
          ("2", "sleep", "sh") in names and any(r[1] == "run" and r[2] == "ps" for r in rows if len(r) == 4),
          "[host] P4 ps: init sleep, sh sleep, ps run", "got %s" % lines)


def main():
    t0 = time.time()
    q = Xv6()
    try:
        if not q.boot():
            check(False, "boot to the shell prompt", "see console.log")
            raise Stop()
        tests = [t_trace_basic, t_trace_fork, t_trace_cmd, t_sysinfo]
        if VARIANT == "practice":
            tests.append(t_practice)
        if VARIANT in ("solution", "practice"):
            tests.append(t_usertests)  # ~1 min; regression only, starter skips it
        for t in tests:
            t(q)
    except Stop:
        print("stopping early: the guest did not return to the prompt", flush=True)
    finally:
        q.kill()
    passed = sum(ok for ok, _ in results)
    print("%d/%d passed (%s, %.0f s)" % (passed, len(results), VARIANT, time.time() - t0))
    sys.exit(0 if results and passed == len(results) else 1)


main()
