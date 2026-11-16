#!/usr/bin/env python3
"""TOOL-M02 mapstat - who eats the memory? (GNU ld -Map analyzer, python3 stdlib only)

usage (run with `python3 -I`):
  mapstat.py sections MAP              output sections: name addr size inputs fill alloc (TSV)
  mapstat.py objects  MAP              alloc bytes per object, archive members as lib.a(m.o) (TSV)
  mapstat.py symbols  MAP              alloc bytes per symbol, largest first (TSV)
  mapstat.py report   MAP [-n N]       human Top-N report (sections / objects / symbols)
  mapstat.py diff OLD NEW [-n N] [--tsv]   size diff per section / object / symbol
PRACTICE extras:
  mapstat.py objects MAP --unmerge     (3) SHF_MERGE strings back to their own objects
  mapstat.py regions MAP               (1) MEMORY region usage like --print-memory-usage
  mapstat.py archives MAP              (2) bytes per archive (libutil.a, libgcc.a, *.o)
  mapstat.py why MAP MEMBER            (4) why was this archive member linked in?
  mapstat.py gate OLD NEW [--max-ram-growth N] [--max-flash-growth N]   (5) CI size gate

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
R_BEFORE = re.compile(r"^\s+0x([0-9a-fA-F]+) \(size before relaxing\)\s*$")
R_MEM = re.compile(r"^(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(?:\s+(\S+))?\s*$")
R_NAME_ONLY = re.compile(r"^( ?)([^\s*(][^\s(]*)\s*$")  # `.long.name` or ` .long.name` alone


def hexnum(tok):
    """size field: '0x400' -> 1024 (addresses use int(tok, 16) directly)."""
    return int(tok, 16)


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
        self.before = None  # "(size before relaxing)": own bytes before SHF_MERGE merging

    @property
    def secname(self):
        return self.sec.name if self.sec is not None else "?"


class MapFile:
    def __init__(self, text):
        self.sections = OrderedDict()
        self.inputs = []
        self.fills = []
        self.regions = OrderedDict()  # name -> (origin, length)
        self.why = OrderedDict()  # member -> "file (symbol)"
        self.parse(text.splitlines())
        self.parse_head(text.splitlines())

    def parse(self, lines):
        in_map = False  # becomes True where the memory-map part starts
        cur_out = None
        cur_in = None
        pending = None  # (indent, name) of a wrapped name line
        for line in lines:
            if line.startswith("Linker script and memory map"):
                in_map = True
                continue
            if not in_map:
                continue
            if pending is not None:
                m = R_CONT.match(line)
                indent, name = pending
                pending = None
                if m:
                    addr, size = int(m.group(1), 16), hexnum(m.group(2))
                    if indent == "":
                        load = int(m.group(3), 16) if m.group(3) else None
                        cur_out = self.add_out(name, addr, size, load)
                        cur_in = None
                    else:
                        cur_in = self.add_in(cur_out, name, addr, size, m.group(4) or "")
                    continue
            m = R_FILL.match(line)
            if m:
                if cur_out is not None:
                    fill = (int(m.group(1), 16), hexnum(m.group(2)))
                    cur_out.fills.append(fill)
                    self.fills.append((cur_out.name,) + fill)
                continue
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
            m = R_BEFORE.match(line)
            if m and cur_in is not None:
                cur_in.before = hexnum(m.group(1))
                continue
            m = R_SYM.match(line)
            if m and cur_in is not None:
                cur_in.syms.append((int(m.group(1), 16), m.group(2)))
                continue
            m = R_NAME_ONLY.match(line)
            if m and not line.startswith(("LOAD ", "OUTPUT(")):
                pending = (m.group(1), m.group(2))

    def parse_head(self, lines):
        """Archive-member table and Memory Configuration (both before the memory map)."""
        part, member = None, None
        for line in lines:
            if line.startswith("Archive member included"):
                part = "why"
                continue
            if line.startswith("Memory Configuration"):
                part = "mem"
                continue
            if line.startswith("Linker script and memory map"):
                break
            if line.startswith(("Discarded input sections", "Allocating common symbols", "Merging")):
                part = None
                continue
            if part == "why" and line.strip():
                if not line.startswith(" "):
                    head, _, rest = line.partition(" ")
                    member = short_obj(head)
                    if rest.strip():
                        self.why[member] = rest.strip()
                elif member is not None:
                    self.why[member] = line.strip()
            elif part == "mem":
                m = R_MEM.match(line)
                if m and m.group(1) not in ("Name", "*default*"):
                    self.regions[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))

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

    def objects(self, unmerge=False):
        tot = {}
        for i in self.alloc_inputs():
            tot[i.obj] = tot.get(i.obj, 0) + i.size
        if unmerge:  # merged string sections: credit each object its own pre-merge bytes
            groups = {}
            for i in self.inputs:
                if i.before is not None and not NONALLOC.match(i.secname):
                    groups.setdefault((i.secname, i.name), []).append(i)
            for grp in groups.values():
                merged = sum(i.size for i in grp)
                for i in grp:
                    tot[i.obj] = tot.get(i.obj, 0) - i.size + i.before
                saved = merged - sum(i.before for i in grp)
                if saved:
                    tot["*merge-saved*"] = tot.get("*merge-saved*", 0) + saved
        return {k: v for k, v in tot.items() if v}

    def region_of(self, addr):
        for name, (org, ln) in self.regions.items():
            if org <= addr < org + ln:
                return name
        return None

    def region_usage(self):
        """VMA bytes in each region + LMA copy for sections with contents (.data)."""
        use = OrderedDict((r, 0) for r in self.regions)
        for s in self.sections.values():
            if not s.alloc or s.size == 0:
                continue
            vr = self.region_of(s.addr)
            if vr:
                use[vr] += s.size
            contents = any(i.size and not re.match(r"^(\.bss|COMMON|\.noinit)", i.name) for i in s.inputs)
            lr = self.region_of(s.load) if s.load is not None else None
            if contents and lr and lr != vr:
                use[lr] += s.size
        return use

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
    so = {s.name: s.size for s in old.sections.values() if s.alloc}
    sn = {s.name: s.size for s in new.sections.values() if s.alloc}
    for k in sorted(set(so) | set(sn)):
        rows.append(("sec", k, so.get(k, 0), sn.get(k, 0)))
    oo, on = old.objects(), new.objects()
    for k in sorted(set(oo) | set(on)):
        rows.append(("obj", k, oo.get(k, 0), on.get(k, 0)))
    yo, yn = {}, {}
    for d, src in ((yo, old), (yn, new)):
        for name, size, obj, _ in src.symbols():
            d[name] = d.get(name, 0) + size
    for k in sorted(set(yo) | set(yn)):
        rows.append(("sym", k, yo.get(k, 0), yn.get(k, 0)))
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


def cmd_regions(mp):
    for name, used in mp.region_usage().items():
        print("%s\t%d\t%d" % (name, used, mp.regions[name][1]))


def cmd_archives(mp):
    tot = {}
    for obj, size in mp.objects().items():
        key = obj.split("(")[0] if "(" in obj else "*.o"
        tot[key] = tot.get(key, 0) + size
    for k, v in sorted(tot.items(), key=lambda kv: -kv[1]):
        print("%s\t%d" % (k, v))


def cmd_why(mp, member):
    hits = [m for m in mp.why if m == member or m.endswith("(" + member + ")")]
    for m in hits:
        ref = mp.why[m]
        head, _, sym = ref.rpartition(" ")
        print("%s <- %s %s" % (m, short_obj(head), sym))
    return 0 if hits else 1


def cmd_gate(old, new, limits):
    uo, un = old.region_usage(), new.region_usage()
    rc = 0
    for region, limit in limits.items():
        grow = un.get(region, 0) - uo.get(region, 0)
        ok = grow <= limit
        print("GATE %s %s %+d B (limit %+d B)" % ("OK  " if ok else "FAIL", region, grow, limit))
        if not ok:
            rc = 1
            secs = {s.name: new.region_of(s.addr) for s in new.sections.values()}
            news = {y[0]: y for y in new.symbols()}
            rows = [r for r in diff_maps(old, new) if r[0] == "sym" and r[3] > r[2]
                    and r[1] in news and secs.get(news[r[1]][3]) == region]
            for _, name, a, b in sorted(rows, key=lambda r: r[2] - r[3])[:5]:
                print("     %-28s %+d B  (%s)" % (name, b - a, news[name][2]))
    return rc


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
    args = [a for a in argv[1:] if a not in ("--tsv", "--unmerge")]
    tsv = "--tsv" in argv
    n = 10
    limits = {}
    for opt, region in (("--max-ram-growth", "RAM"), ("--max-flash-growth", "FLASH")):
        if opt in args:
            k = args.index(opt)
            limits[region] = int(args[k + 1], 0)
            del args[k:k + 2]
    if "-n" in args:
        k = args.index("-n")
        n = int(args[k + 1])
        del args[k:k + 2]
    if len(args) == 2 and args[0] in ("sections", "objects", "symbols", "report", "regions", "archives"):
        mp = load(args[1])
        if args[0] == "objects" and "--unmerge" in argv:
            for obj, size in sorted(mp.objects(unmerge=True).items(), key=lambda kv: (-kv[1], kv[0])):
                print("%s\t%d" % (obj, size))
        elif args[0] == "regions":
            cmd_regions(mp)
        elif args[0] == "archives":
            cmd_archives(mp)
        elif args[0] == "report":
            cmd_report(mp, args[1], n)
        else:
            {"sections": cmd_sections, "objects": cmd_objects, "symbols": cmd_symbols}[args[0]](mp)
        return 0
    if len(args) == 3 and args[0] == "why":
        return cmd_why(load(args[1]), args[2])
    if len(args) == 3 and args[0] == "gate":
        return cmd_gate(load(args[1]), load(args[2]), limits or {"RAM": 0, "FLASH": 0})
    if len(args) == 3 and args[0] == "diff":
        cmd_diff(load(args[1]), load(args[2]), args[1], args[2], n, tsv)
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
