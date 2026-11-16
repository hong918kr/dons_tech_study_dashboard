# TOOL-M11 practice: a robust post-mortem command for lldb (no hard-coded thread/frame numbers).
#   lldb -b -c /cores/core.<pid> <work>/crash -o 'command script import practice.py' -o pm_report
# Works on a core: it only READS memory/registers/variables, never runs code in the target.
import lldb

ESR_EC = {0x15: "SVC64 (syscall)", 0x20: "IABT_LOWER (instruction abort, EL0)",
          0x24: "DABT_LOWER (data abort, EL0)", 0x3c: "BRK64 (breakpoint)"}
DFSC = {0x04: "translation fault, level 0", 0x05: "translation fault, level 1",
        0x06: "translation fault, level 2", 0x07: "translation fault, level 3",
        0x0d: "permission fault, level 1", 0x0f: "permission fault, level 3",
        0x21: "alignment fault"}
SESSION_MAGIC = 0x5E55C0DE


def task_name(thread):
    """P1: walk the frames, find a `self` (struct task *) and read self->name."""
    for f in thread:
        v = f.FindVariable("self")
        if v.IsValid() and v.GetType().GetPointeeType().GetName() == "task":
            return v.GetChildMemberWithName("name").GetSummary().strip('"'), f
    return None, None


def find_frame(process, func):
    """first (thread, frame) whose function name is `func`."""
    for t in process:
        for f in t:
            if f.GetFunctionName() == func:
                return t, f
    return None, None


def reg(frame, name):
    r = frame.FindRegister(name)
    return r.GetValueAsUnsigned() if r.IsValid() else None


def pm_report(debugger, command, result, internal_dict):
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    out = []

    # P1 every thread -> task name (TCB-style struct in its entry frame)
    for t in process:
        name, _ = task_name(t)
        out.append("thread #%d -> %s" % (t.GetIndexID(), name or "(main: no task)"))

    # P2 the crashing thread = the one with an exception stop reason (not "thread #1")
    crashed = [t for t in process if t.GetStopReason() == lldb.eStopReasonException]
    t = crashed[0]
    f0 = t.GetFrameAtIndex(0)
    le = f0.GetLineEntry()
    out.append("crash: thread #%d task=%s func=%s at %s:%d" % (
        t.GetIndexID(), task_name(t)[0], f0.GetFunctionName(),
        le.GetFileSpec().GetFilename(), le.GetLine()))

    # P3 decode ESR/FAR of the crashing thread
    esr, far = reg(f0, "esr"), reg(f0, "far")
    ec, iss = esr >> 26, esr & 0x1ffffff
    wnr = "write" if iss & (1 << 6) else "read"
    out.append("esr=0x%x EC=0x%02x %s, DFSC=0x%02x %s, %s; far=0x%x" % (
        esr, ec, ESR_EC.get(ec, "?"), iss & 0x3f, DFSC.get(iss & 0x3f, "?"), wnr, far))

    # P4 the heap object: header intact? name terminated? pointer == FAR?
    s = target.FindFirstGlobalVariable("g_sess")
    magic = s.Dereference().GetChildMemberWithName("magic").GetValueAsUnsigned()
    tx = s.Dereference().GetChildMemberWithName("tx").GetValueAsUnsigned()
    err = lldb.SBError()
    raw = process.ReadMemory(s.GetValueAsUnsigned() + 8, 24, err)
    nul = raw.find(b"\0")
    out.append("session @0x%x: magic %s, name[16] %s, tx=0x%x %s, tx bytes as text '%s'" % (
        s.GetValueAsUnsigned(),
        "OK" if magic == SESSION_MAGIC else "BAD 0x%x" % magic,
        "unterminated" if nul < 0 or nul >= 16 else "ok",
        tx, "== far" if tx == far else "!= far",
        raw[16:24].decode("ascii", "replace")))

    # P5 breadcrumb ring in logical order (oldest -> newest)
    seq = target.FindFirstGlobalVariable("g_trace_seq").GetValueAsUnsigned()
    ring = target.FindFirstGlobalVariable("g_trace")
    n = ring.GetNumChildren()
    for i in range(max(0, seq - n), seq):
        e = ring.GetChildAtIndex(i % n)
        out.append("trace %d: %s %s %d" % (
            e.GetChildMemberWithName("seq").GetValueAsUnsigned(),
            e.GetChildMemberWithName("who").GetSummary().strip('"'),
            e.GetChildMemberWithName("what").GetSummary().strip('"'),
            e.GetChildMemberWithName("arg").GetValueAsUnsigned()))

    # P6 the writer: its thread is still parked in rx_parser_main with last_len on the stack
    wt, wf = find_frame(process, "rx_parser_main")
    last_len = wf.FindVariable("last_len").GetValueAsUnsigned()
    cap = wf.EvaluateExpression("sizeof(g_sess->name)").GetValueAsUnsigned()
    out.append("writer: thread #%d task=%s frame #%d %s last_len=%d > sizeof name=%d -> %d bytes into tx" % (
        wt.GetIndexID(), task_name(wt)[0], wf.GetFrameID(), wf.GetFunctionName(),
        last_len, cap, last_len - cap))
    out.append("VERDICT: %s overflowed session.name (%d > %d); %s crashed reading s->tx" % (
        task_name(wt)[0], last_len, cap, task_name(t)[0]))
    result.PutCString("\n".join(out))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -o -f %s.pm_report pm_report" % __name__)
