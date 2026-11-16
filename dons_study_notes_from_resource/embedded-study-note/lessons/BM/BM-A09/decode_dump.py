#!/usr/bin/env python3
"""BM-A09 decode_dump.py — UART 로 받은 크래시 덤프(@@ 줄 블록)를 해독한다. 표준 라이브러리만 (struct, zlib).

    python3 -I decode_dump.py <uart.txt> <nm.txt> [--check]

<uart.txt> : QEMU -serial file: 로 받은 UART0 출력 (또는 현장 로그)
<nm.txt>   : 같은 빌드의 `arm-none-eabi-nm -n firmware.elf` 출력 (VM 에서 만든 텍스트)
--check    : 이 레슨의 주입 시나리오별 기대값과 대조하고, 하나라도 틀리면 exit 1
"""
import struct
import sys
import zlib

FIELDS = (["magic", "version", "size", "boot_count", "reset_reason", "exc", "exc_return", "flags",
           "stage", "sp", "msp", "psp"] + ["r%d" % i for i in range(13)] +
          ["lr", "pc", "xpsr", "cfsr", "hfsr", "mmfar", "bfar", "shcsr", "nstack"] +
          ["stack%d" % i for i in range(16)] + ["crc"])
assert len(FIELDS) == 51
DUMP_MAGIC, DUMP_SIZE, FRAME_BAD = 0x504D5544, 204, 0xFFFFFFFF
EXC = {2: "NMI", 3: "HardFault", 4: "MemManage", 5: "BusFault", 6: "UsageFault"}
CFSR_BITS = [(0, "IACCVIOL"), (1, "DACCVIOL"), (3, "MUNSTKERR"), (4, "MSTKERR"), (5, "MLSPERR"),
             (7, "MMARVALID"), (8, "IBUSERR"), (9, "PRECISERR"), (10, "IMPRECISERR"), (11, "UNSTKERR"),
             (12, "STKERR"), (13, "LSPERR"), (15, "BFARVALID"), (16, "UNDEFINSTR"), (17, "INVSTATE"),
             (18, "INVPC"), (19, "NOCP"), (24, "UNALIGNED"), (25, "DIVBYZERO")]
HFSR_BITS = [(1, "VECTTBL"), (30, "FORCED"), (31, "DEBUGEVT")]
FLAG_BITS = [(0, "PSP"), (1, "FRAME_OK"), (2, "HANDLER"), (3, "FPFRAME"), (4, "STKERR"),
             (5, "BAD_EXCRET"), (6, "ALIGNPAD"), (7, "STACK_TRUNC")]


def bitnames(v, table):
    names = [n for b, n in table if v >> b & 1]
    rest = v & ~sum(1 << b for b, _ in table)
    if rest:
        names.append("+0x%08x" % rest)
    return "|".join(names) if names else "-"


class Symbols:
    def __init__(self, path):
        self.code, self.labels, self.all = [], {}, {}
        with open(path) as f:
            for line in f:
                p = line.split()
                if len(p) != 3:
                    continue
                addr, typ, name = int(p[0], 16), p[1], p[2]
                self.all[name] = addr
                if name.startswith("pc_"):
                    self.labels[addr & ~1] = name
                elif typ in "TtWw" and not name.startswith("_"):
                    self.code.append((addr & ~1, name))
        self.code.sort()
        self.lo = self.all.get("_stext", self.code[0][0] if self.code else 0)
        self.hi = self.all.get("_etext", 0)

    def func(self, addr):
        """addr 를 포함하는 함수 이름 (주소 이하에서 가장 가까운 코드 심볼)."""
        a = addr & ~1
        if not (self.lo <= a < self.hi):
            return None
        best = None
        for sa, name in self.code:
            if sa <= a:
                best = (sa, name)
            else:
                break
        return best

    def fmt(self, addr):
        if addr == FRAME_BAD:
            return "0x%08x (unknown: frame not readable)" % addr
        if addr >> 8 == 0xFFFFFF:
            return "0x%08x (EXC_RETURN)" % addr
        f = self.func(addr)
        s = "0x%08x" % addr
        if f:
            s += " = %s+0x%x" % (f[1], (addr & ~1) - f[0])
        if (addr & ~1) in self.labels:
            s += " [%s]" % self.labels[addr & ~1]
        return s


def parse_uart(path):
    """@@DUMP-BEGIN ... @@HEX ... @@DUMP-END 블록, @@NESTED, @@NODUMP 줄을 순서대로 뽑는다."""
    events, cur = [], None
    with open(path, errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("@@"):
                continue
            head, _, rest = line.partition(" ")
            kv = dict(t.split("=", 1) for t in rest.split() if "=" in t)
            if head == "@@DUMP-BEGIN":
                cur = {"kind": "dump", "kv": kv, "hex": ""}
            elif head == "@@HEX" and cur is not None:
                cur["hex"] += rest.strip()
            elif head == "@@DUMP-END" and cur is not None:
                events.append(cur)
                cur = None
            elif head == "@@NESTED":
                events.append({"kind": "nested", "kv": kv})
            elif head == "@@NODUMP":
                events.append({"kind": "nodump", "kv": kv})
    return events


def decode_dump(ev):
    """바이트 → 필드. 형식 오류는 문자열로 돌려준다 (예외로 죽지 않는다)."""
    try:
        b = bytes.fromhex(ev["hex"])
    except ValueError as e:
        return None, "bad hex: %s" % e
    if len(b) != DUMP_SIZE:
        return None, "length %d != %d" % (len(b), DUMP_SIZE)
    d = dict(zip(FIELDS, struct.unpack("<51I", b)))
    if d["magic"] != DUMP_MAGIC or d["version"] != 2 or d["size"] != DUMP_SIZE:
        return None, "bad header magic=0x%08x version=%d size=%d" % (d["magic"], d["version"], d["size"])
    crc = zlib.crc32(b[4:200]) & 0xFFFFFFFF
    if crc != d["crc"]:
        return None, "CRC mismatch: computed 0x%08x, dump says 0x%08x" % (crc, d["crc"])
    if d["stage"] not in (1, 2) or d["nstack"] > 16:
        return None, "bad fields stage=%d nstack=%d" % (d["stage"], d["nstack"])
    if int(ev["kv"].get("crc", "0"), 16) != d["crc"]:
        return None, "BEGIN line crc disagrees with HEX"
    return d, None


def report(d, tag, sym, nested):
    print("dump tag=%s  crash in boot #%d  CRC 0x%08x OK (zlib)" % (tag, d["boot_count"], d["crc"]))
    print("  exception : %s (%d)   EXC_RETURN 0x%08x   flags %s   stage %d" % (
        EXC.get(d["exc"], "?"), d["exc"], d["exc_return"], bitnames(d["flags"], FLAG_BITS), d["stage"]))
    print("  CFSR 0x%08x : %s" % (d["cfsr"], bitnames(d["cfsr"], CFSR_BITS)))
    if d["hfsr"]:
        print("  HFSR 0x%08x : %s" % (d["hfsr"], bitnames(d["hfsr"], HFSR_BITS)))
    if d["cfsr"] >> 7 & 1:
        print("  MMFAR 0x%08x  (MMARVALID)" % d["mmfar"])
    if d["cfsr"] >> 15 & 1:
        print("  BFAR  0x%08x  (BFARVALID)" % d["bfar"])
    print("  pc %s" % sym.fmt(d["pc"]))
    print("  lr %s" % sym.fmt(d["lr"]))
    stk = "PSP" if d["flags"] & 1 else "MSP"
    print("  sp 0x%08x (%s, %s mode)  msp 0x%08x psp 0x%08x  xpsr 0x%08x (IPSR %d)" % (
        d["sp"], stk, "handler" if d["flags"] & 4 else "thread", d["msp"], d["psp"], d["xpsr"],
        d["xpsr"] & 0x1FF if d["xpsr"] != FRAME_BAD else -1))
    print("  r0-r3  %s" % " ".join("%08x" % d["r%d" % i] for i in range(4)))
    print("  r4-r11 %s" % " ".join("%08x" % d["r%d" % i] for i in range(4, 12)))
    print("  r12    %08x" % d["r12"])
    words = [d["stack%d" % i] for i in range(d["nstack"])]
    if words:
        print("  stack  %s" % " ".join("%08x" % w for w in words))
        cands = [w for w in words if w & 1 and sym.lo <= (w & ~1) < sym.hi]
        for w in cands:   # 휴리스틱: 홀수 + .text 범위 = Thumb 복귀 주소일 "수도" 있음 (진짜 unwind 는 ARM-M12)
            print("    maybe return addr: %s" % sym.fmt(w))
    else:
        print("  stack  (none captured)")
    if nested:
        n = nested["kv"]
        print("  NESTED fault while dumping: %s, HFSR %s, CFSR %s, pc %s" % (
            EXC.get(int(n["exc"]), "?"), bitnames(int(n["hfsr"], 16), HFSR_BITS),
            bitnames(int(n["cfsr"], 16), CFSR_BITS), sym.fmt(int(n["pc"], 16))))


def check(tag, d, nested, sym):
    """이 레슨의 주입 시나리오별 기대값 (QEMU 10.2.1 mps2-an385 에서 관측한 값)."""
    S = sym.all
    cf = set(bitnames(d["cfsr"], CFSR_BITS).split("|"))
    func = lambda a: (sym.func(a) or (0, None))[1]
    lab = lambda a: sym.labels.get(a & ~1)
    exp = {
        "UDF_MSP": [d["exc"] == 6, cf == {"UNDEFINSTR"}, lab(d["pc"]) == "pc_udf", func(d["pc"]) == "inject_udf_regs",
                    not d["flags"] & 1, d["r4"] == 0xA0040004 and d["r11"] == 0xA00B000B, d["nstack"] == 16],
        "BUS_PRECISE": [d["exc"] == 5, cf == {"PRECISERR", "BFARVALID"}, d["bfar"] == 0x30000000,
                        lab(d["pc"]) == "pc_bus", d["r1"] == 0x30000000],
        "DIV0_PSP": [d["exc"] == 6, cf == {"DIVBYZERO"}, d["flags"] & 1 == 1, lab(d["pc"]) == "pc_div0",
                     func(d["pc"]) == "task_div0", d["stack0"] == 0x7A5C0001],
        "IRQ_UDF": [d["exc"] == 6, cf == {"UNDEFINSTR"}, d["exc_return"] == 0xFFFFFFF1, d["xpsr"] & 0x1FF == 23,
                    func(d["pc"]) == "Irq7_Handler"],
        "STACK_OVF": [d["exc"] == 4, cf == {"DACCVIOL", "MSTKERR", "MMARVALID"}, not d["flags"] & 2,
                      d["pc"] == FRAME_BAD, S["_sstack_guard"] <= d["mmfar"] < S["_estack_guard"], d["stage"] == 1],
        "BAD_SP": [d["exc"] == 5, cf == {"UNDEFINSTR", "STKERR"}, d["msp"] == 0x300000E0, d["pc"] == FRAME_BAD,
                   d["stage"] == 1],
        "NESTED": [d["exc"] == 6, d["stage"] == 1, nested is not None and int(nested["kv"]["exc"]) == 3,
                   nested is not None and int(nested["kv"]["hfsr"], 16) >> 30 & 1 == 1,
                   nested is not None and func(int(nested["kv"]["pc"], 16)) in ("fault_capture", "dump_capture")],
    }.get(tag)
    if exp is None:
        return ["unknown tag %s" % tag]
    return ["%s: expectation #%d failed" % (tag, i) for i, ok in enumerate(exp) if not ok]


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    sym = Symbols(argv[2])
    events = parse_uart(argv[1])
    errors, ndump, nodump = [], 0, []
    for i, ev in enumerate(events):
        if ev["kind"] == "nodump":
            nodump.append(ev["kv"].get("rc"))
            print("no valid dump: rc=%s (magic=%s ver=%s)" % (ev["kv"].get("rc"), ev["kv"].get("magic"),
                                                              ev["kv"].get("ver")))
            continue
        if ev["kind"] != "dump":
            continue
        nested = events[i + 1] if i + 1 < len(events) and events[i + 1]["kind"] == "nested" else None
        tag = ev["kv"].get("tag", "?")
        d, err = decode_dump(ev)
        if err:
            errors.append("dump tag=%s rejected: %s" % (tag, err))
            print(errors[-1])
            continue
        ndump += 1
        report(d, tag, sym, nested)
        if "--check" in argv:
            errs = check(tag, d, nested, sym)
            errors += errs
            print("  check: %s" % ("OK" if not errs else "; ".join(errs)))
    print("decoded %d dump(s), %d no-dump boot(s) %s" % (ndump, len(nodump), nodump))
    if "--check" in argv:
        if ndump != 7:
            errors.append("expected 7 dumps, got %d" % ndump)
        if nodump != ["NO_MAGIC", "BAD_VERSION", "BAD_CRC"]:
            errors.append("no-dump sequence %s != [NO_MAGIC, BAD_VERSION, BAD_CRC]" % nodump)
        print("check: %s" % ("ALL OK" if not errors else "%d problem(s): %s" % (len(errors), "; ".join(errors))))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
