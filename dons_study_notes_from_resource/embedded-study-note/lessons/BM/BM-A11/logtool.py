#!/usr/bin/env python3
"""BM-A11 logtool.py — 로거 UART 로그의 호스트 쪽 도구 (stdlib 만, python3 -I 로 실행).

  expected            : 5 부팅 시나리오의 기대 UART 로그를 출력 (가짜 센서를 펌웨어와 "따로" 다시 구현한 모델).
                        주소처럼 빌드마다 바뀌는 값은 {*} (공백 없는 토큰 하나와 매치).
  check EXP ACT       : 줄 단위 비교. 다르면 첫 차이와 함께 exit 1.
  decode ACT NM.txt   : WDOG/DUMP 줄을 사람이 읽게 풀기 — CFSR/HFSR 비트, EXC_RETURN, IPSR, PC/LR 심볼화
                        (NM.txt = VM 에서 `arm-none-eabi-nm -n solution.elf` 결과).
"""
import re
import sys

N_SAMPLES = 12
M32 = 0xFFFFFFFF


def sensor_value(tick):
    x = (tick * 2654435761) & M32
    x ^= x >> 15
    x = (x * 0x2C1B3C6D) & M32
    x ^= x >> 12
    ph = tick % 50
    tri = ph if ph < 25 else 50 - ph
    return 2000 + tri * 8 + (x & 7)


def expected():
    plan = [("POWER_ON", None, "INJECT main-hang"),
            ("WATCHDOG", "WDOG diag=MAIN_STUCK boot=1 since_kick={*} pc={*} excret=0xfffffff9", "INJECT tick-stall"),
            ("WATCHDOG", "WDOG diag=TICK_DEAD boot=2 since_kick=0 pc={*} excret=0xfffffff9", "INJECT bus-fault"),
            ("FAULT", "DUMP v=1 boot=3 exc=3 excret=0xfffffff9 pc={*} lr={*} xpsr={*} cfsr=0x00008200 "
                      "hfsr=0x40000000 bfar=0x30000000 crc={*}", "INJECT isr-fault"),
            ("FAULT", "DUMP v=1 boot=4 exc=3 excret=0xfffffff1 pc={*} lr={*} xpsr={*} cfsr=0x00010000 "
                      "hfsr=0x40000000 bfar=0x00000000 crc={*}", "DONE boots=5")]
    out = []
    for b, (cause, report, last) in enumerate(plan, 1):
        out.append("BOOT %d cause=%s" % (b, cause))
        out.append("IMG OK ver=1.1.0")
        if report:
            out.append(report)
        s = 0
        for i in range(1, N_SAMPLES + 1):
            v = sensor_value(i)
            s += v
            out.append("S %d %02d t=%04d v=%d" % (b, i, i, v))
        out.append("SUM %d n=%d sum=%d drops=0" % (b, N_SAMPLES, s))
        out.append(last)
    return out


def pattern(line):
    return re.compile("^" + re.escape(line).replace(re.escape("{*}"), r"\S+") + "$")


def check(exp_path, act_path):
    exp = open(exp_path).read().splitlines()
    act = open(act_path, errors="replace").read().replace("\r", "").splitlines()
    for i in range(max(len(exp), len(act))):
        e = exp[i] if i < len(exp) else "<end of file>"
        a = act[i] if i < len(act) else "<end of file>"
        if i >= len(exp) or i >= len(act) or not pattern(e).match(a):
            print("UART CHECK FAIL at line %d\n  expected: %s\n  actual:   %s" % (i + 1, e, a))
            print("UART lines: expected %d, actual %d" % (len(exp), len(act)))
            return 1
    print("UART CHECK PASS: %d lines match %s" % (len(act), exp_path))
    return 0


CFSR_BITS = {0: "IACCVIOL", 1: "DACCVIOL", 3: "MUNSTKERR", 4: "MSTKERR", 7: "MMARVALID", 8: "IBUSERR",
             9: "PRECISERR", 10: "IMPRECISERR", 11: "UNSTKERR", 12: "STKERR", 15: "BFARVALID",
             16: "UNDEFINSTR", 17: "INVSTATE", 18: "INVPC", 19: "NOCP", 24: "UNALIGNED", 25: "DIVBYZERO"}
HFSR_BITS = {1: "VECTTBL", 30: "FORCED", 31: "DEBUGEVT"}
EXC_RETURN = {0xFFFFFFF1: "Handler mode, MSP (fault inside an ISR)",
              0xFFFFFFF9: "Thread mode, MSP", 0xFFFFFFFD: "Thread mode, PSP"}


def bits(v, table):
    return " ".join(n for b, n in sorted(table.items()) if v >> b & 1) or "-"


def symbolize(addr, syms):
    if addr >= 0xFFFFFFE0:
        return "EXC_RETURN(%s)" % EXC_RETURN.get(addr, "?")
    best = None
    for a, name in syms:
        if a <= addr:
            best = (a, name)
        else:
            break
    return "%s+0x%x" % (best[1], addr - best[0]) if best else "?"


def decode(act_path, nm_path):
    syms = []
    for ln in open(nm_path):
        p = ln.split()
        if len(p) == 3 and p[1] in "TtWw" and not p[2].startswith("$"):
            syms.append((int(p[0], 16) & ~1, p[2]))
    syms.sort()
    for ln in open(act_path, errors="replace").read().replace("\r", "").splitlines():
        kv = dict(t.split("=", 1) for t in ln.split()[1:] if "=" in t)
        if ln.startswith("WDOG"):
            pc = int(kv["pc"], 16)
            print("%s\n  -> watchdog reset after boot %s: %s; ticks since last kick = %s; "
                  "NMI interrupted %s at %s" % (ln, kv["boot"], "tick ISR dead (main alive)"
                  if kv["diag"] == "TICK_DEAD" else "main loop stuck (ticks still running)", kv["since_kick"],
                  EXC_RETURN.get(int(kv["excret"], 16), "?"), symbolize(pc, syms)))
        elif ln.startswith("DUMP"):
            cfsr, hfsr, xpsr = int(kv["cfsr"], 16), int(kv["hfsr"], 16), int(kv["xpsr"], 16)
            ipsr = xpsr & 0x1FF
            ctx = "Thread" if ipsr == 0 else ("IRQ %d" % (ipsr - 16) if ipsr >= 16 else "exception %d" % ipsr)
            print("%s\n  -> exc %s, CFSR[%s] HFSR[%s] BFAR=%s\n     %s; faulting context %s (IPSR=%d)\n"
                  "     pc %s   lr %s" % (ln, kv["exc"], bits(cfsr, CFSR_BITS), bits(hfsr, HFSR_BITS), kv["bfar"],
                  EXC_RETURN.get(int(kv["excret"], 16), "?"), ctx, ipsr, symbolize(int(kv["pc"], 16), syms),
                  symbolize(int(kv["lr"], 16), syms)))
    return 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "expected":
        print("\n".join(expected()))
    elif cmd == "check":
        sys.exit(check(sys.argv[2], sys.argv[3]))
    elif cmd == "decode":
        sys.exit(decode(sys.argv[2], sys.argv[3]))
    else:
        print(__doc__)
        sys.exit(2)
