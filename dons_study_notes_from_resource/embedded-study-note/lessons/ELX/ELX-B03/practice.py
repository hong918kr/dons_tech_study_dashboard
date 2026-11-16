#!/usr/bin/env python3
# ENV: linux  (self-test also runs on the Mac: python3 -I solution.py)
"""ELX-B03 kconfdiff (practice) -- solution + P1..P5 from section 9 of the lesson.

  A. What changed between two .config files?      (clone of scripts/diffconfig, same output)
  B. Which of MY requested changes did not stick?  (fragment vs final .config after olddefconfig)
  C. Will a module built from this .config load?   (vermagic string, built-in vs module)

Usage:
  python3 -I practice.py                       self-test on fixtures/ -> "N/M passed", exit 0/1
  python3 -I practice.py diff A B [-m]         same output as scripts/diffconfig [-m] A B
  python3 -I practice.py stuck [--strict] REQ FINAL [KNOWN_SYMBOLS_FILE]
  python3 -I practice.py vermagic CONFIG RELEASE
  python3 -I practice.py kind MODNAME BUILD_DIR    -> y (built-in) / m (module) / n
  python3 -I practice.py redefined BASE FRAGMENT   (P2)   count CONFIG (P3)
  python3 -I practice.py explain MOD_VERMAGIC KERNEL_VERMAGIC (P4)   ikconfig IMAGE (P5)
"""
import gzip
import io
import os
import sys
import tarfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")


# ---------------------------------------------------------------- Part A ----
def parse_config(text):
    """Return {NAME: value} for a .config / fragment text.

    'CONFIG_FOO=y'               -> {'FOO': 'y'}     (value kept raw: y, m, 250, 0x10, "str")
    '# CONFIG_FOO is not set'    -> {'FOO': 'n'}
    other comments, blank lines, garbage -> ignored
    CRLF line ends and a missing final newline must not change any value.
    A symbol given twice: the last one wins (that is what Kconfig does).
    """
    cfg = {}
    for raw in text.split("\n"):
        line = raw.rstrip("\r")
        if line.startswith("CONFIG_") and "=" in line:
            name, val = line[7:].split("=", 1)
            if name:
                cfg[name] = val
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            name = line[9:-11]
            if name and " " not in name:
                cfg[name] = "n"
    return cfg


def format_diff(a, b, merge=False):
    """Return the lines scripts/diffconfig prints for old config a -> new config b.

    normal: '-NAME old' (only in a), ' NAME old -> new' (changed), '+NAME new' (only in b),
            each group sorted by NAME, groups in that order.
    merge (-m): changed and added items as config lines: 'CONFIG_NAME=new' or
            '# CONFIG_NAME is not set'; removed items print nothing.
    """
    removed = sorted(k for k in a if k not in b)
    changed = sorted(k for k in a if k in b and a[k] != b[k])
    added = sorted(k for k in b if k not in a)
    out = []
    if not merge:
        out += ["-%s %s" % (k, a[k]) for k in removed]
        out += [" %s %s -> %s" % (k, a[k], b[k]) for k in changed]
        out += ["+%s %s" % (k, b[k]) for k in added]
        return out
    for k in changed + added:
        out.append("# CONFIG_%s is not set" % k if b[k] == "n" else "CONFIG_%s=%s" % (k, b[k]))
    return out


# ---------------------------------------------------------------- Part B ----
ABSENT = "(absent)"


def check_requests(req, final, known=None, strict=False):
    """List the requested values that did not survive olddefconfig.

    req, final: dicts from parse_config. A symbol missing from final counts as 'n'.
    known: optional set of symbol names that exist in this kernel's Kconfig.
    Returns [(name, wanted, got, reason)] sorted by name; got is ABSENT if missing.
    reason is one of: 'unknown', 'dropped', 'capped', 'forced', 'changed'.
    P1 strict=True: like merge_config.sh, '# CONFIG_X is not set' must appear literally,
    so wanted n + absent is reported too (reason 'absent').
    """
    bad = []
    for name in sorted(req):
        want = req[name]
        got = final.get(name, ABSENT)
        if got == want or (want == "n" and got == ABSENT and not strict):
            continue
        if want == "n" and got == ABSENT:
            reason = "absent"
        elif known is not None and name not in known:
            reason = "unknown"
        elif got == ABSENT:
            reason = "dropped"
        elif want == "y" and got == "m":
            reason = "capped"
        elif want == "n" and got in ("y", "m"):
            reason = "forced"
        else:
            reason = "changed"
        bad.append((name, want, got, reason))
    return bad


# ---------------------------------------------------------------- Part C ----
def expected_vermagic(cfg, release, arch="aarch64"):
    """Rebuild VERMAGIC_STRING (include/linux/vermagic.h) from a .config dict.

    release + ' ' + 'SMP ' + 'preempt_rt ' | 'preempt ' + 'mod_unload ' + 'modversions ' + arch
    (each word only when its CONFIG_ symbol is y; PREEMPT_RT wins over PREEMPT_BUILD).
    """
    s = release + " "
    if cfg.get("SMP") == "y":
        s += "SMP "
    if cfg.get("PREEMPT_RT") == "y":
        s += "preempt_rt "
    elif cfg.get("PREEMPT_BUILD") == "y":
        s += "preempt "
    if cfg.get("MODULE_UNLOAD") == "y":
        s += "mod_unload "
    if cfg.get("MODVERSIONS") == "y":
        s += "modversions "
    return s + arch


def module_kind(modname, builtin_text, order_text):
    """'y' if modname is built into vmlinux, 'm' if it is built as a .ko, else 'n'.

    builtin_text: contents of modules.builtin  (lines like 'kernel/drivers/gpio/gpio-sim.ko')
    order_text:   contents of modules.order    (lines like 'drivers/gpio/gpio-aggregator.o')
    '-' and '_' are the same character in module names (gpio_sim == gpio-sim).
    """
    def norm(s):
        return s.replace("-", "_")

    want = norm(modname)
    for line in builtin_text.splitlines():
        base = line.strip().rsplit("/", 1)[-1]
        if base.endswith(".ko") and norm(base[:-3]) == want:
            return "y"
    for line in order_text.splitlines():
        base = line.strip().rsplit("/", 1)[-1]
        if base.endswith((".o", ".ko")) and norm(base.rsplit(".", 1)[0]) == want:
            return "m"
    return "n"


# ------------------------------------------------------------ practice P2-P5
def redefined(base, frag):
    """P2: names the fragment overrides with a different value (merge_config.sh's
    'Value of CONFIG_X is redefined by fragment'). Symbols not in base are not listed."""
    return sorted(k for k in frag if k in base and base[k] != frag[k])


def count_values(cfg):
    """P3: 'y=.. m=.. n=.. other=..' (other = int/hex/string values)."""
    c = {"y": 0, "m": 0, "n": 0, "other": 0}
    for v in cfg.values():
        c[v if v in ("y", "m", "n") else "other"] += 1
    return "y=%d m=%d n=%d other=%d" % (c["y"], c["m"], c["n"], c["other"])


def explain_vermagic(mod, kern):
    """P4: why would the loader print "version magic 'mod' should be 'kern'"?"""
    if mod == kern:
        return ["OK"]
    out = []
    mt, kt = mod.split(), kern.split()
    if mt[:1] != kt[:1]:
        out.append("release: module %s != kernel %s" % (mt[0], kt[0]))
    for w in kt[1:]:
        if w not in mt[1:]:
            out.append("missing in module: %s" % w)
    for w in mt[1:]:
        if w not in kt[1:]:
            out.append("extra in module: %s" % w)
    return out


def ikconfig(image):
    """P5: the .config text stored by CONFIG_IKCONFIG between 'IKCFG_ST' and 'IKCFG_ED'
    as a gzip stream (kernel/configs.c), or None. Same job as scripts/extract-ikconfig."""
    pos = image.find(b"IKCFG_ST\x1f\x8b\x08")
    if pos < 0:
        return None
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    try:
        return d.decompress(image[pos + 8:]).decode("utf-8", "replace")
    except zlib.error:
        return None


def parse_modinfo(text):
    """'key:   value' lines from modinfo -> {key: [values]} (given, not a TODO)."""
    info = {}
    for line in text.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            info.setdefault(k.strip(), []).append(v.strip())
    return info


# ---------------------------------------------------------------- harness ---
PASSED = 0
TOTAL = 0


def CHECK(name, cond):
    global PASSED, TOTAL
    TOTAL += 1
    if cond:
        PASSED += 1
        print("PASS: " + name)
    else:
        print("FAIL: " + name)


def load_fixture_configs():
    with tarfile.open(os.path.join(FIXTURES, "configs.tar.xz")) as t:
        return {m.name: t.extractfile(m).read().decode() for m in t.getmembers()}


def read(path):
    with io.open(path, encoding="utf-8", errors="replace", newline="") as f:
        return f.read()


def selftest():
    # A1 -- parser edge cases
    t = ('#\n# Automatically generated file; DO NOT EDIT.\n#\nCONFIG_HZ=250\r\n'
         '# CONFIG_GPIO_SIM is not set\nCONFIG_CMDLINE="console=ttyAMA0 root=/dev/vda"\n'
         'CONFIG_PHYS_OFFSET=0x40000000\n\n# just a comment\nGARBAGE \x00\xff line\n'
         'CONFIG_IKCONFIG=y\nCONFIG_IKCONFIG=m\nCONFIG_LOCALVERSION=""\nCONFIG_FF="a\x0cb\x85c"\n'
         'CONFIG_LAST=y')
    c = parse_config(t)
    CHECK("A1 int value, CRLF stripped", c.get("HZ") == "250")
    CHECK("A1 '# ... is not set' -> n", c.get("GPIO_SIM") == "n")
    CHECK("A1 string with '=' inside kept whole",
          c.get("CMDLINE") == '"console=ttyAMA0 root=/dev/vda"')
    CHECK("A1 hex and empty string kept raw",
          c.get("PHYS_OFFSET") == "0x40000000" and c.get("LOCALVERSION") == '""')
    CHECK("A1 duplicate: last wins", c.get("IKCONFIG") == "m")
    CHECK("A1 last line without newline", c.get("LAST") == "y")
    CHECK("A1 split on \\n only (\\x0c, \\x85 stay inside the value)", c.get("FF") == '"a\x0cb\x85c"')
    CHECK("A1 comments/garbage ignored", len(c) == 8)

    # A2 -- diff on small dicts
    a = {"A": "y", "B": "m", "C": "n", "S": '"x"'}
    b = {"B": "y", "C": "n", "D": "m", "S": '"y"', "E": "n"}
    CHECK("A2 normal format", format_diff(a, b) ==
          ["-A y", " B m -> y", ' S "x" -> "y"', "+D m", "+E n"])
    CHECK("A2 merge format (-m)", format_diff(a, b, merge=True) ==
          ["CONFIG_B=y", 'CONFIG_S="y"', "CONFIG_D=m", "# CONFIG_E is not set"])
    CHECK("A2 identical -> empty", format_diff(a, dict(a)) == [])

    # A3 -- golden: real 6.18.55 .config files vs real scripts/diffconfig output
    cfgs = load_fixture_configs()
    base = parse_config(cfgs["defconfig.config"])
    CHECK("A3 defconfig: 4685 set + 4951 'not set' = 9636 symbols", len(base) == 9636)
    finals = {}
    for f in ("naive", "good", "bringup"):
        finals[f] = parse_config(cfgs["final_%s.config" % f])
        gold = read(os.path.join(FIXTURES, "golden_diff_%s.txt" % f)).splitlines()
        CHECK("A3 diffconfig golden: %s" % f, format_diff(base, finals[f]) == gold)
        gold = read(os.path.join(FIXTURES, "golden_merge_%s.txt" % f)).splitlines()
        CHECK("A3 diffconfig -m golden: %s" % f, format_diff(base, finals[f], True) == gold)

    # B -- did my requests stick?
    reqs = {f: parse_config(read(os.path.join(HERE, "req_%s.config" % f)))
            for f in ("naive", "good", "bringup")}
    CHECK("B naive: only DEBUG_INFO forced back on",
          check_requests(reqs["naive"], finals["naive"]) == [("DEBUG_INFO", "n", "y", "forced")])
    CHECK("B good: everything stuck", check_requests(reqs["good"], finals["good"]) == [])
    known = set(base) | set(finals["bringup"])
    CHECK("B bringup: console dropped, stub capped, typo unknown",
          check_requests(reqs["bringup"], finals["bringup"], known) == [
              ("GPIO_SIMULATOR", "m", ABSENT, "unknown"),
              ("I2C_STUB", "y", "m", "capped"),
              ("SERIAL_AMBA_PL011_CONSOLE", "y", ABSENT, "dropped")])
    CHECK("B absent counts as n (DEBUG_INFO_REDUCED not reported)",
          "DEBUG_INFO_REDUCED" not in [r[0] for r in check_requests(reqs["bringup"],
                                                                    finals["bringup"])])
    CHECK("B without known set: typo looks like 'dropped'",
          ("GPIO_SIMULATOR", "m", ABSENT, "dropped") in
          check_requests(reqs["bringup"], finals["bringup"]))
    CHECK("B int changed by range", check_requests({"HZ": "999"}, {"HZ": "250"}) ==
          [("HZ", "999", "250", "changed")])

    # C -- vermagic and y/m
    CHECK("C1 vermagic of lab-like defconfig (= real modinfo of a lab .ko)",
          expected_vermagic(base, "6.18.55") == "6.18.55 SMP preempt mod_unload aarch64")
    CHECK("C1 vermagic with LOCALVERSION -b03",
          expected_vermagic(finals["good"], "6.18.55-b03") ==
          "6.18.55-b03 SMP preempt mod_unload aarch64")
    rt = dict(base, PREEMPT_RT="y", MODVERSIONS="y", SMP="n")
    CHECK("C1 RT + modversions, no SMP",
          expected_vermagic(rt, "6.18.55-rt") == "6.18.55-rt preempt_rt mod_unload modversions aarch64")
    bi = "kernel/kernel/configs.ko\nkernel/drivers/gpio/gpio-sim.ko\n"
    order = "drivers/gpio/gpio-aggregator.o\ndrivers/i2c/i2c-stub.o\n"
    CHECK("C2 gpio-sim built-in", module_kind("gpio-sim", bi, order) == "y")
    CHECK("C2 '_' == '-' (gpio_aggregator)", module_kind("gpio_aggregator", bi, order) == "m")
    CHECK("C2 not built at all", module_kind("gpio-mockup", bi, order) == "n")
    CHECK("C2 no substring match (sim != gpio-sim)", module_kind("sim", bi, order) == "n")
    mi = parse_modinfo("vermagic:       6.18.55 SMP preempt mod_unload aarch64\n"
                       "depends:        \nintree:         Y\nalias:  a\nalias:  b\n")
    CHECK("C3 modinfo vermagic matches .config",
          mi["vermagic"][0] == expected_vermagic(base, "6.18.55") and mi["alias"] == ["a", "b"])

    # P1..P5
    CHECK("P1 strict: DEBUG_INFO_REDUCED absent is reported like merge_config.sh",
          [r[0] for r in check_requests(reqs["bringup"], finals["bringup"], strict=True)] ==
          ["DEBUG_INFO_REDUCED", "GPIO_SIMULATOR", "I2C_STUB", "SERIAL_AMBA_PL011_CONSOLE"])
    CHECK("P2 redefined (naive)", redefined(base, reqs["naive"]) ==
          ["DEBUG_INFO", "GPIO_SIM", "IKCONFIG"])
    CHECK("P2 redefined (bringup): same value / new symbol not listed",
          redefined(base, reqs["bringup"]) == ["DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT",
                                               "DEBUG_INFO_NONE", "DEBUG_INFO_REDUCED",
                                               "I2C_STUB", "SERIAL_AMBA_PL011"])
    CHECK("P3 count defconfig", count_values(base) == "y=3295 m=1273 n=4951 other=117")
    CHECK("P4 release mismatch (real insmod error text)",
          explain_vermagic("6.18.55 SMP preempt mod_unload aarch64",
                           "6.18.55-sub SMP preempt mod_unload aarch64") ==
          ["release: module 6.18.55 != kernel 6.18.55-sub"])
    CHECK("P4 flags mismatch", explain_vermagic(
        "6.18.55 SMP preempt mod_unload modversions aarch64",
        "6.18.55 SMP preempt_rt mod_unload aarch64") ==
        ["missing in module: preempt_rt", "extra in module: preempt",
         "extra in module: modversions"])
    blob = (b"\x00junk" + b"IKCFG_ST" +
            gzip.compress(b"CONFIG_A=y\n# CONFIG_B is not set\n") +
            b"IKCFG_ED\x00tail")
    CHECK("P5 ikconfig finds the gzip after IKCFG_ST",
          parse_config(ikconfig(blob) or "") == {"A": "y", "B": "n"})
    CHECK("P5 no marker -> None (IKCONFIG=n or =m kernel)", ikconfig(b"\x00" * 64) is None)

    print("%d/%d passed" % (PASSED, TOTAL))
    return 0 if PASSED == TOTAL else 1


def main(argv):
    if len(argv) <= 1:
        return selftest()
    cmd = argv[1]
    if cmd == "diff":
        merge = "-m" in argv
        a, b = [x for x in argv[2:] if x != "-m"]
        for line in format_diff(parse_config(read(a)), parse_config(read(b)), merge):
            print(line)
        return 0
    if cmd == "stuck":
        strict = "--strict" in argv
        argv = [x for x in argv if x != "--strict"]
        known = set(read(argv[4]).split()) if len(argv) > 4 else None
        for name, want, got, why in check_requests(parse_config(read(argv[2])),
                                                   parse_config(read(argv[3])), known, strict):
            print("NOT STUCK %s wanted=%s got=%s reason=%s" % (name, want, got, why))
        return 0
    if cmd == "vermagic":
        print(expected_vermagic(parse_config(read(argv[2])), argv[3]))
        return 0
    if cmd == "kind":
        d = argv[3]
        print(module_kind(argv[2], read(os.path.join(d, "modules.builtin")),
                          read(os.path.join(d, "modules.order"))))
        return 0
    if cmd == "redefined":
        for name in redefined(parse_config(read(argv[2])), parse_config(read(argv[3]))):
            print(name)
        return 0
    if cmd == "count":
        print(count_values(parse_config(read(argv[2]))))
        return 0
    if cmd == "explain":
        print("\n".join(explain_vermagic(argv[2], argv[3])))
        return 0
    if cmd == "ikconfig":
        with open(argv[2], "rb") as f:
            text = ikconfig(f.read())
        if text is None:
            print("ikconfig: no IKCFG_ST marker", file=sys.stderr)
            return 1
        sys.stdout.write(text)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
