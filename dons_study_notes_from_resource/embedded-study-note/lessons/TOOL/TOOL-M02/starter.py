#!/usr/bin/env python3
"""TOOL-M02 mapstat - who eats the memory? (GNU ld -Map analyzer, python3 stdlib only)

STARTER: this script runs, but its numbers disagree with arm-none-eabi-size / nm in four
places (bash check.sh starter tells you where), and `diff` is still a TODO.

usage (run with `python3 -I`):
  mapstat.py sections MAP              output sections: name addr size inputs fill alloc (TSV)
  mapstat.py objects  MAP              alloc bytes per object, archive members as lib.a(m.o) (TSV)
  mapstat.py symbols  MAP              alloc bytes per symbol, largest first (TSV)
  mapstat.py report   MAP [-n N]       human Top-N report (sections / objects / symbols)
  mapstat.py diff OLD NEW [-n N] [--tsv]   size diff per section / object / symbol

Parses only the "Linker script and memory map" part of a GNU ld (bfd) map:
  output section   `.text           0x080000d8      0x580`   (+ `load address 0x...`)
  input section    ` .text.fmt_u32  0x080005d0       0x44 libutil.a(fmt.o)`
  wrapped forms    name alone on its line, `0xADDR 0xSIZE [object]` on the next
  fill             ` *fill*         0x08000436        0x2 `
  symbol           `                0x080005d0                fmt_u32`
"""
import os
import re
import sys
from collections import OrderedDict

NONALLOC = re.compile(r"^\.(debug|comment|ARM\.attributes|stab|gnu\.attributes|gnu_debuglink)")
RAM_LIKE = re.compile(r"^(\.data|\.bss|\.noinit|\._user_heap_stack|\.heap|\.stack|COMMON)")

R_OUT = re.compile(r"^(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(?:\s+load address 0x([0-9a-fA-F]+))?\s*$")
R_IN = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*(.*?)\s*$")
R_FILL = re.compile(r"^ \*fill\*\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*$")
R_CONT = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(?:\s+load address 0x([0-9a-fA-F]+)|\s+(.*?))?\s*$")
R_SYM = re.compile(r"^\s{16,}0x([0-9a-fA-F]+)\s+([A-Za-z_.$][\w.$@]*)\s*$")
R_NAME_ONLY = re.compile(r"^( ?)([^\s*(][^\s(]*)\s*$")  # `.long.name` or ` .long.name` alone


def hexnum(tok):
    """size field: '0x400' -> 1024 (addresses use int(tok, 16) directly)."""
    return int(tok.strip("0x") or "0", 16)


def short_obj(path):
    """'/opt/.../libgcc.a(_udivmoddi4.o)' -> 'libgcc.a(_udivmoddi4.o)'."""
    m = re.match(r"^(.*?)(\([^()]*\))?$", path)
    return os.path.basename(m.group(1)) + (m.group(2) or "")


class OutSec:
    def __init__(self, name, addr, size, load):
        self.name, self.addr, self.size, self.load = name, addr, size, load
        self.alloc = not NONALLOC.match(name)
        self.inputs, self.fills = [], []


class Input:
    def __init__(self, sec, name, addr, size, obj):
        self.sec, self.name, self.addr, self.size, self.obj = sec, name, addr, size, obj
        self.syms = []  # (addr, name)

    @property
    def secname(self):
        return self.sec.name if self.sec is not None else "?"


class MapFile:
    def __init__(self, text):
        self.sections = OrderedDict()
        self.inputs = []
        self.fills = []
        self.parse(text.splitlines())

    def parse(self, lines):
        in_map = True  # becomes True where the memory-map part starts
        cur_out = None
        cur_in = None
        pending = None  # (indent, name) of a wrapped name line
        for line in lines:
            if line.startswith("Linker script and memory map"):
                in_map = True
                continue
            if not in_map:
                continue
            m = R_FILL.match(line)
            if m:
                if cur_out is not None:
                    fill = (int(m.group(1), 16), hexnum(m.group(2)))
                    cur_out.fills.append(fill)
                    self.fills.append((cur_out.name,) + fill)
            m = R_IN.match(line)
            if m:
                cur_in = self.add_in(cur_out, m.group(1), int(m.group(2), 16), hexnum(m.group(3)), m.group(4))
                continue
            m = R_OUT.match(line)
            if m:
                load = int(m.group(4), 16) if m.group(4) else None
                cur_out = self.add_out(m.group(1), int(m.group(2), 16), hexnum(m.group(3)), load)
                cur_in = None
                continue
            m = R_SYM.match(line)
            if m and cur_in is not None:
                cur_in.syms.append((int(m.group(1), 16), m.group(2)))
                continue

    def add_out(self, name, addr, size, load):
        sec = OutSec(name, addr, size, load)
        self.sections[name] = sec
        return sec

    def add_in(self, out, name, addr, size, obj):
        inp = Input(out, name, addr, size, short_obj(obj) if obj else "")
        if out is not None:
            out.inputs.append(inp)
        self.inputs.append(inp)
        return inp

    # ---------- aggregation ----------
    def alloc_inputs(self):
        return [i for i in self.inputs if i.size > 0 and not NONALLOC.match(i.secname)]

    def objects(self):
        tot = {}
        for i in self.alloc_inputs():
            tot[i.obj] = tot.get(i.obj, 0) + i.size
        return tot

    def object_split(self):
        """obj -> [flash, ram]; .data counts in both (stored in Flash, lives in RAM)."""
        out = {}
        for i in self.alloc_inputs():
            fr = out.setdefault(i.obj, [0, 0])
            if i.secname.startswith(".data"):
                fr[0] += i.size
                fr[1] += i.size
            elif RAM_LIKE.match(i.secname):
                fr[1] += i.size
            else:
                fr[0] += i.size
        return out

    def symbols(self):
        """[(name, size, obj, section)]: size = distance to the next symbol address in the
        same input section (or its end). Unnamed inputs get a derived/bracketed name."""
        res = []
        for i in self.alloc_inputs():
            if i.syms:
                addrs = sorted(set(a for a, _ in i.syms)) + [i.addr + i.size]
                for a, n in i.syms:
                    nxt = addrs[addrs.index(a) + 1]
                    res.append((n, nxt - a, i.obj, i.secname))
            else:
                m = re.match(r"^\.(?:text|data|bss|rodata)\.(?!str\d)(.+)$", i.name)
                name = m.group(1) if m else "[%s %s]" % (i.obj, i.name)
                res.append((name, i.size, i.obj, i.secname))
        res.sort(key=lambda r: (-r[1], r[0]))
        return res


def diff_maps(old, new):
    """[(kind, name, old, new)] for kind in sec/obj/sym, only entries that changed."""
    rows = []
    # TODO: compare alloc output sections, objects() and symbols() of old vs new.
    #       A name missing on one side counts as 0 there (new / gone).
    return [r for r in rows if r[2] != r[3]]


# ---------- output ----------
def load(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return MapFile(f.read())


def cmd_sections(mp):
    for s in mp.sections.values():
        print("%s\t0x%08x\t%d\t%d\t%d\t%d" % (s.name, s.addr, s.size, sum(i.size for i in s.inputs),
                                            sum(f[1] for f in s.fills), int(s.alloc)))


def cmd_objects(mp):
    for obj, size in sorted(mp.objects().items(), key=lambda kv: (-kv[1], kv[0])):
        print("%s\t%d" % (obj, size))


def cmd_symbols(mp):
    for name, size, obj, sec in mp.symbols():
        print("%s\t%d\t%s\t%s" % (name, size, obj, sec))


def cmd_report(mp, path, n):
    secs = [s for s in mp.sections.values() if s.alloc and s.size]
    print("== %s: %d alloc output sections, %d input sections, %d objects ==" %
          (os.path.basename(path), len(secs), len(mp.alloc_inputs()), len(mp.objects())))
    print("-- Top %d output sections --" % n)
    print("  %-20s %8s %8s %6s" % ("section", "size", "inputs", "fill"))
    for s in sorted(secs, key=lambda s: -s.size)[:n]:
        print("  %-20s %8d %8d %6d" % (s.name, s.size, sum(i.size for i in s.inputs), sum(f[1] for f in s.fills)))
    print("-- Top %d objects (flash = text+rodata+data, ram = data+bss) --" % n)
    split = mp.object_split()
    print("  %-26s %7s %7s %7s" % ("object", "total", "flash", "ram"))
    for obj, size in sorted(mp.objects().items(), key=lambda kv: (-kv[1], kv[0]))[:n]:
        print("  %-26s %7d %7d %7d" % (obj, size, split[obj][0], split[obj][1]))
    print("-- Top %d symbols --" % n)
    print("  %-26s %7s  %-22s %s" % ("symbol", "size", "object", "section"))
    for name, size, obj, sec in mp.symbols()[:n]:
        print("  %-26s %7d  %-22s %s" % (name, size, obj, sec))


def cmd_diff(old, new, po, pn, n, tsv):
    rows = diff_maps(old, new)
    if tsv:
        for kind, name, a, b in rows:
            print("%s\t%s\t%d\t%d\t%+d" % (kind, name, a, b, b - a))
        return
    print("== diff %s -> %s ==" % (os.path.basename(po), os.path.basename(pn)))
    for kind, title in (("sec", "output sections"), ("obj", "objects"), ("sym", "symbols")):
        part = sorted([r for r in rows if r[0] == kind], key=lambda r: (-abs(r[3] - r[2]), r[1]))
        print("-- %s (top %d of %d changed, by |delta|) --" % (title, min(n, len(part)), len(part)))
        for _, name, a, b in part[:n]:
            tag = "  (new)" if a == 0 else "  (gone)" if b == 0 else ""
            print("  %-28s %7d -> %7d  %+7d%s" % (name, a, b, b - a, tag))


def main(argv):
    args = [a for a in argv[1:] if a != "--tsv"]
    tsv = "--tsv" in argv
    n = 10
    if "-n" in args:
        k = args.index("-n")
        n = int(args[k + 1])
        del args[k:k + 2]
    if len(args) == 2 and args[0] in ("sections", "objects", "symbols", "report"):
        mp = load(args[1])
        if args[0] == "report":
            cmd_report(mp, args[1], n)
        else:
            {"sections": cmd_sections, "objects": cmd_objects, "symbols": cmd_symbols}[args[0]](mp)
        return 0
    if len(args) == 3 and args[0] == "diff":
        cmd_diff(load(args[1]), load(args[2]), args[1], args[2], n, tsv)
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
