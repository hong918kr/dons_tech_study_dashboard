#!/usr/bin/env bash
# TOOL-M02 checker: map file analyzer vs. ground truth from the real ELFs.
#   bash check.sh <starter|solution|practice>
# 1) gen_maps.sh rebuilds v1/v2 firmware (Mac arm-none-eabi-gcc) into the work dir
# 2) ground truth: arm-none-eabi-size -A / nm -S --size-sort on the ELFs, size -A / nm
#    on each own .o, --print-gc-sections list, --print-memory-usage
# 3) runs <variant>.py (python3 -I) on the COMMITTED v1.map / v2.map and compares.
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M02_work/$V
rm -rf "$W"
mkdir -p "$W/truth" "$W/out"

bash "$LESSON/gen_maps.sh" "$W/gen" >"$W/gen.log" 2>&1 || { cat "$W/gen.log"; echo "FAIL build"; echo "0/1 passed"; exit 1; }

# ---- ground truth (binutils on the same ELFs / objects) ----
for v in v1 v2; do
	arm-none-eabi-size -A "$W/gen/$v.elf" >"$W/truth/size_A_$v.txt"
	arm-none-eabi-nm -S --size-sort "$W/gen/$v.elf" >"$W/truth/nm_$v.txt"
	for o in "$W/gen/$v"/*.o; do
		b=$(basename "$o")
		case "$b" in crc16.o | fmt.o | mem.o | selftest.o) n="libutil.a($b)" ;; *) n=$b ;; esac
		{
			echo "== $n"
			arm-none-eabi-size -A "$o" | awk 'NR>2 && NF==3 {print "S", $1, $2}'
			arm-none-eabi-nm -S --defined-only "$o" | awk 'NF==4 {print "N", $3, $4, $2}'
		} >>"$W/truth/objs_$v.txt"
	done
	cmp -s "$W/gen/$v.map" "$LESSON/$v.map" && echo same >"$W/truth/repro_$v.txt"
done

# ---- run the variant on the committed maps ----
cp "$LESSON/$V.py" "$W/mapstat.py"
run() { # run <outfile> <args...>
	local o=$1
	shift
	python3 -I "$W/mapstat.py" "$@" >"$W/out/$o" 2>"$W/out/$o.err"
	echo $? >"$W/out/$o.rc"
}
for v in v1 v2; do
	for c in sections objects symbols; do run "${c}_$v" "$c" "$LESSON/$v.map"; done
done
run report_v1 report "$LESSON/v1.map" -n 5
run diff_tsv diff "$LESSON/v1.map" "$LESSON/v2.map" --tsv
run diff_human diff "$LESSON/v1.map" "$LESSON/v2.map" -n 5
if [ "$V" = practice ]; then
	run objects_unmerge_v2 objects "$LESSON/v2.map" --unmerge
	for v in v1 v2; do run "regions_$v" regions "$LESSON/$v.map"; done
	run archives_v1 archives "$LESSON/v1.map"
	run why_udiv why "$LESSON/v1.map" _udivmoddi4.o
	run why_mem why "$LESSON/v1.map" mem.o
	run gate_4k gate "$LESSON/v1.map" "$LESSON/v2.map" --max-ram-growth 4096
	run gate_8k gate "$LESSON/v1.map" "$LESSON/v2.map" --max-ram-growth 8192
	cp "$W/gen/memusage_v1.txt" "$W/gen/memusage_v2.txt" "$W/truth/"
fi
cp "$W/gen/gc_v1.txt" "$W/gen/gc_v2.txt" "$W/truth/"
grep -c '^ \*fill\*' "$LESSON/v1.map" >"$W/truth/fill_lines_v1.txt"

# ---- grade ----
python3 -I - "$W" "$V" <<'PY' | tee "$W/results.txt"
import os, re, sys
W, V = sys.argv[1], sys.argv[2]
NONALLOC = re.compile(r"^\.(debug|comment|ARM\.attributes|stab|gnu\.attributes)")
res = []
def check(ok, msg):
    res.append(bool(ok))
    print(("PASS " if ok else "FAIL ") + msg)
def rd(p):
    try:
        with open(os.path.join(W, p)) as f:
            return f.read()
    except OSError:
        return ""
def out(name):
    return [l.split("\t") for l in rd("out/" + name).splitlines() if l.strip()]
def rc(name):
    return rd("out/%s.rc" % name).strip()

def truth_sections(v):  # alloc sections of the ELF: name -> size
    d = {}
    for l in rd("truth/size_A_%s.txt" % v).splitlines()[2:]:
        t = l.split()
        if len(t) == 3 and t[2] != "0" and int(t[1]) > 0:
            d[t[0]] = int(t[1])
    return d
def truth_nm(v):  # nm -S --size-sort -> name -> size (skip linker-script symbols)
    d = {}
    for l in rd("truth/nm_%s.txt" % v).splitlines():
        t = l.split()
        if len(t) == 4 and not t[3].startswith("_e") and not t[3].startswith("_s"):
            d[t[3]] = int(t[1], 16)
    return d
def gc_removed(v):
    return set(re.findall(r"removing unused section '([^']+)' in file '(?:.*/)?([^'/]+(?:\([^)]*\))?)'",
                          rd("truth/gc_%s.txt" % v)))
def truth_objects(v):  # own objects: (sum of kept alloc sections + COMMON, symbol names, string sections)
    objs, cur, name = {}, None, ""
    rm = gc_removed(v)
    for l in rd("truth/objs_%s.txt" % v).splitlines():
        t = l.split()
        if t[0] == "==":
            name = t[1]
            cur = objs.setdefault(name, {"bytes": 0, "syms": set(), "strs": 0})
        elif t[0] == "S" and not NONALLOC.match(t[1]) and (t[1], name) not in rm:
            cur["bytes"] += int(t[2])
            if t[1].startswith(".rodata.str"):
                cur["strs"] += int(t[2])
        elif t[0] == "N":
            cur["syms"].add(t[2])
            if t[1] == "C":
                cur["bytes"] += int(t[3], 16)
    return objs

for v in ("v1", "v2"):
    check(rd("truth/repro_%s.txt" % v).strip() == "same", "%s.map in the lesson dir == fresh gen_maps.sh build (reproducible)" % v)

# ---------- targeted checks (one per classic parser bug) ----------
sec1 = {r[0]: r for r in out("sections_v1") if len(r) == 6}
hs = sec1.get("._user_heap_stack")
check(hs is not None and int(hs[2]) == 3076,
      "wrapped output header: ._user_heap_stack = %s B (size -A: 3076)" % (hs[2] if hs else "missing"))
sym1 = {}
for r in out("symbols_v1"):
    if len(r) == 4:
        sym1.setdefault(r[0], r)
want = {"Default_Handler": ("2", "startup.o"), "g_banner": ("24", "main.o"), "g_adc_raw": ("4", "main.o")}
got = {k: (sym1[k][1], sym1[k][2]) if k in sym1 else None for k in want}
check(got == want, "wrapped input lines (name alone, addr/size on next line): %s" %
      ", ".join("%s=%s" % (k, "%s B %s" % got[k] if got[k] else "missing") for k in want))
bad = [r[0] for r in out("symbols_v1") if len(r) == 4 and (r[3] == "?" or r[0] == "bist" or "unused" in r[0])]
bad += [n for n in sec1 if n.startswith("*")]
check(not bad and rc("symbols_v1") == "0",
      "only the memory-map part parsed (Discarded input sections, Memory Configuration skipped): %s" %
      (", ".join(bad[:4]) or "ok"))
fills = int(rd("truth/fill_lines_v1.txt") or 0)
small = [sec1[n][4] if n in sec1 else "?" for n in (".text", ".rodata")]
fake = [r for r in out("symbols_v1") if len(r) == 4 and "*fill*" in r[0]]
objs1 = {r[0]: int(r[1]) for r in out("objects_v1") if len(r) == 2}
check(not fake and "" not in objs1 and small == ["4", "3"],
      "*fill* counted once, never as an input/object: .text/.rodata fill = %s B (expect 4/3), fake *fill* inputs: %d (map has %d *fill* lines)"
      % ("/".join(small), len(fake), fills))
rx = sym1.get("g_rx_buf")
tx = sec1.get(".text")
check(rx is not None and rx[1] == "1024" and tx is not None and tx[2] == "1408",
      "hex sizes: g_rx_buf 0x400 -> %s B, .text 0x580 -> %s B" % (rx[1] if rx else "?", tx[2] if tx else "?"))

# ---------- full comparisons vs binutils ----------
for v in ("v1", "v2"):
    ts = truth_sections(v)
    sec = {r[0]: r for r in out("sections_" + v) if len(r) == 6}
    wrong = [n for n in ts if n not in sec or int(sec[n][2]) != ts[n]]
    check(not wrong, "%s output sections == size -A (%d alloc sections)%s" % (v, len(ts), ": " + ",".join(wrong) if wrong else ""))
    acc = [n for n in ts if n not in sec or int(sec[n][3]) + int(sec[n][4]) != ts[n]]
    check(not acc, "%s every byte accounted: inputs + *fill* == section size%s" % (v, ": " + ",".join(acc) if acc else ""))
    objs = {r[0]: int(r[1]) for r in out("objects_" + v) if len(r) == 2}
    fill = sum(int(sec[n][4]) for n in ts if n in sec)
    check(objs and sum(objs.values()) + fill == sum(ts.values()),
          "%s conservation: sum(objects) %d + fill %d == size -A total %d" % (v, sum(objs.values()), fill, sum(ts.values())))
    to = truth_objects(v)
    nm = truth_nm(v)
    own = set().union(*[o["syms"] for o in to.values()])
    sy = {}
    for r in out("symbols_" + v):
        if len(r) == 4:
            sy.setdefault(r[0], int(r[1]))
    cmp_syms = [s for s in nm if s in own]
    badsym = ["%s map=%s nm=%d" % (s, sy.get(s), nm[s]) for s in cmp_syms if sy.get(s) != nm[s]]
    check(cmp_syms and not badsym, "%s symbol sizes == nm -S --size-sort (%d own symbols)%s" %
          (v, len(cmp_syms), ": " + "; ".join(badsym[:3]) if badsym else ""))
    merged = {k for k, o in to.items() if o["strs"]} if v == "v2" else set()
    linked = {k for k, o in to.items() if o["syms"] & set(nm)}  # archive members really pulled
    want_o = {k: o["bytes"] for k, o in to.items() if k in linked and k not in merged}
    bado = ["%s map=%s truth=%d" % (k, objs.get(k), b) for k, b in want_o.items() if objs.get(k) != b]
    absent = "libutil.a(selftest.o)" not in objs
    check(not bado and absent, "%s per-object bytes == .o sections - gc'd + COMMON (%d objects%s), selftest.o not pulled%s" %
          (v, len(want_o), "; skip merged-string objs " + ",".join(sorted(merged)) if merged else "",
           ": " + "; ".join(bado[:3]) if bado else ("" if absent else ": selftest.o present")))

# ---------- report & diff ----------
rep = rd("out/report_v1")
lines = rep.splitlines()
def first_after(h):
    for i, l in enumerate(lines):
        if l.startswith(h):
            return lines[i + 2].split()[0] if i + 2 < len(lines) else ""
    return ""
check(rc("report_v1") == "0" and first_after("-- Top 5 objects") == "comm.o" and first_after("-- Top 5 symbols") == "g_rx_buf",
      "report -n 5: top object '%s', top symbol '%s'" % (first_after("-- Top 5 objects"), first_after("-- Top 5 symbols")))
d = {(r[0], r[1]): (int(r[2]), int(r[3])) for r in out("diff_tsv") if len(r) == 5}
t1, t2 = truth_sections("v1"), truth_sections("v2")
ws = {n: (t1.get(n, 0), t2.get(n, 0)) for n in set(t1) | set(t2) if t1.get(n, 0) != t2.get(n, 0)}
gs = {k[1]: x for k, x in d.items() if k[0] == "sec"}
check(ws and gs == ws, "diff sections == size -A deltas: %s" % ", ".join("%s %+d" % (n, b - a) for n, (a, b) in sorted(gs.items())))
n1, n2 = truth_nm("v1"), truth_nm("v2")
own = set().union(*[o["syms"] for o in truth_objects("v2").values()])
wy = {s: (n1.get(s, 0), n2.get(s, 0)) for s in own if (s in n1 or s in n2) and n1.get(s, 0) != n2.get(s, 0)}
gy = {k[1]: x for k, x in d.items() if k[0] == "sym" and k[1] in own}
check(wy and gy == wy, "diff own symbols == nm deltas (%d changed): %s" %
      (len(wy), ", ".join("%s %+d" % (s, b - a) for s, (a, b) in sorted(gy.items(), key=lambda kv: -abs(kv[1][1] - kv[1][0]))[:4])))
o1, o2 = truth_objects("v1"), truth_objects("v2")
wo = {k: (o1.get(k, {}).get("bytes", 0), o2[k]["bytes"]) for k in o2 if not o2[k]["strs"]
      and o1.get(k, {}).get("bytes", 0) != o2[k]["bytes"]}
go = {k[1]: x for k, x in d.items() if k[0] == "obj" and k[1] in wo}
check(wo and go == wo, "diff objects == truth deltas (non-merged): %s" % ", ".join("%s %+d" % (k, b - a) for k, (a, b) in sorted(go.items())))
hd = rd("out/diff_human")
check(rc("diff_human") == "0" and "telemetry.o" in hd and "(new)" in hd, "human diff -n 5 shows telemetry.o (new)")

# ---------- practice extras ----------
if V == "practice":
    u = {r[0]: int(r[1]) for r in out("objects_unmerge_v2") if len(r) == 2}
    t = truth_objects("v2")
    pairs = {k: (u.get(k), t[k]["bytes"]) for k in ("comm.o", "telemetry.o")}
    check(all(a == b for a, b in pairs.values()) and sum(u.values()) == sum(objs_v2 for objs_v2 in
          [int(r[1]) for r in out("objects_v2") if len(r) == 2]),
          "P3 --unmerge: SHF_MERGE strings re-attributed: %s" % ", ".join("%s %s/%s" % (k, a, b) for k, (a, b) in pairs.items()))
    for v in ("v1", "v2"):
        mu = dict(re.findall(r"^\s*(\w+):\s+(\d+) B", rd("truth/memusage_%s.txt" % v), re.M))
        rg = {r[0]: r[1] for r in out("regions_" + v) if len(r) >= 2}
        check(mu and all(rg.get(k) == mu[k] for k in mu),
              "P1 regions %s == --print-memory-usage: %s" % (v, ", ".join("%s %s/%s" % (k, rg.get(k), mu[k]) for k in sorted(mu))))
    ar = {r[0]: int(r[1]) for r in out("archives_v1") if len(r) == 2}
    nm1 = truth_nm("v1")
    ol = {k: o for k, o in o1.items() if o["syms"] & set(nm1)}  # own objects really linked
    lu = sum(o["bytes"] for k, o in ol.items() if k.startswith("libutil.a("))
    lg = sum(truth_sections("v1").values()) - sum(int(r[4]) for r in out("sections_v1") if len(r) == 6 and r[5] == "1") \
        - sum(o["bytes"] for o in ol.values())
    check(ar.get("libutil.a") == lu and ar.get("libgcc.a") == lg,
          "P2 archives: libutil.a %s (truth %d), libgcc.a %s (truth %d)" % (ar.get("libutil.a"), lu, ar.get("libgcc.a"), lg))
    w1, w2 = rd("out/why_udiv").strip(), rd("out/why_mem").strip()
    check(w1 == "libgcc.a(_udivmoddi4.o) <- libgcc.a(_aeabi_uldivmod.o) (__udivmoddi4)" and
          w2 == "libutil.a(mem.o) <- startup.o (memcpy)", "P4 why: '%s' / '%s'" % (w1, w2))
    g4, g8 = rd("out/gate_4k"), rd("out/gate_8k")
    check(rc("gate_4k") == "1" and "g_rx_buf" in g4 and rc("gate_8k") == "0",
          "P5 gate RAM +5120 B: limit 4096 -> rc %s (names g_rx_buf: %s), limit 8192 -> rc %s" %
          (rc("gate_4k"), "g_rx_buf" in g4, rc("gate_8k")))

p = sum(res)
print("%d/%d passed" % (p, len(res)))
sys.exit(0 if p == len(res) else 1)
PY
exit "${PIPESTATUS[0]}"
