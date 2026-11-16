#!/usr/bin/env bash
# TOOL-M11 demo (Mac): core-dump prerequisites, signature/entitlement matrix, core anatomy.
#   bash demo_mac.sh > demo_mac.txt 2>&1      (~2 min: every core is ~7 GB and ~20 s to write)
# Every core this script creates is deleted right after it is measured.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M11_work/demo_mac
rm -rf "$W" && mkdir -p "$W" || exit 2

echo "== macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)), $(uname -m)"
echo "== ulimit -c soft/hard: $(ulimit -c) / $(ulimit -Hc)"
sysctl kern.coredump kern.corefile kern.sugid_coredump
ls -ld /cores
csrutil status
DevToolsSecurity -status

cc -std=c11 -g -O0 -Wall -Wextra -Werror crash.c -o "$W/crash.linker" || exit 1
cp "$W/crash.linker" "$W/crash.unsigned";  codesign --remove-signature "$W/crash.unsigned"
cp "$W/crash.linker" "$W/crash.gta";       codesign -s - -f --entitlements ent.plist "$W/crash.gta" 2>/dev/null
cp "$W/crash.linker" "$W/crash.rt";        codesign -s - -f -o runtime "$W/crash.rt" 2>/dev/null
cp "$W/crash.linker" "$W/crash.rt_gta";    codesign -s - -f -o runtime --entitlements ent.plist "$W/crash.rt_gta" 2>/dev/null

echo; echo "== signature of each variant"
for b in linker unsigned gta rt rt_gta; do
	f=$(codesign -dv "$W/crash.$b" 2>&1 | sed -n 's/.*flags=\(0x[0-9a-f]*([a-z,-]*)\).*/\1/p')
	e=$(codesign -d --entitlements - "$W/crash.$b" 2>/dev/null | grep -c get-task-allow)
	printf '%-9s flags=%-22s get-task-allow=%s\n' "$b" "${f:-none}" "$e"
done

# run <variant> <ulimit -c>: crash it, report rc / core size / time, keep the gta core for anatomy
run() {
	local t0 t1 p rc sz
	t0=$(date +%s)
	( cd "$W" && ulimit -c "$2" && exec "./crash.$1" ) >/dev/null 2>&1 &
	p=$!; wait "$p" 2>/dev/null; rc=$?
	t1=$(date +%s)
	if [ -f "/cores/core.$p" ]; then sz="$(stat -f %z "/cores/core.$p") bytes"; else sz="none"; fi
	printf '%-9s ulimit -c %-9s rc=%-3s core=%-18s %ss\n' "$1" "$2" "$rc" "$sz" "$((t1 - t0))"
	if [ "$1" = gta ] && [ "$2" = unlimited ]; then KEEP=/cores/core.$p; else rm -f "/cores/core.$p"; fi
}
echo; echo "== crash matrix"
KEEP=
trap '[ -n "$KEEP" ] && rm -f "$KEEP"' EXIT
run gta 0
run unsigned unlimited
run linker unlimited
run rt unlimited
run rt_gta unlimited
run gta unlimited

echo; echo "== anatomy of $KEEP (crash.gta)"
otool -h "$KEEP"
otool -l "$KEEP" > "$W/lc.txt"
echo "-- load commands:"; grep -E '^ +cmd ' "$W/lc.txt" | awk '{print $2}' | sort | uniq -c
python3 -I - "$W/lc.txt" <<'PY'
import sys
segs, cur = [], None
for line in open(sys.argv[1]):
    s = line.split()
    if len(s) < 2: continue
    if s[0] == "cmd": cur = {"cmd": s[1]}; segs.append(cur)
    elif cur is not None and s[0] in ("vmaddr", "vmsize", "filesize", "initprot"): cur[s[0]] = s[1]
segs = [c for c in segs if c["cmd"] == "LC_SEGMENT_64"]
for c in segs: c["fs"] = int(c["filesize"], 0)
tot = sum(c["fs"] for c in segs)
print("-- %d LC_SEGMENT_64, file bytes %d (%.0f MiB)" % (len(segs), tot, tot / 2**20))
by = {}
for c in segs: by[c["initprot"]] = by.get(c["initprot"], 0) + c["fs"]
for k, v in sorted(by.items(), key=lambda kv: -kv[1]):
    print("   initprot %s (%s): %6.0f MiB" % (k, {"0x00000001": "r--", "0x00000003": "rw-", "0x00000005": "r-x"}.get(k, "?"), v / 2**20))
print("-- 5 biggest segments:")
for c in sorted(segs, key=lambda c: -c["fs"])[:5]:
    print("   vmaddr %s  %8.1f MiB  initprot %s" % (c["vmaddr"], c["fs"] / 2**20, c["initprot"]))
PY
echo "-- LC_THREAD exception state per thread (far / esr):"
grep -E '^[[:space:]]+far ' "$W/lc.txt" | awk '{print "   thread", NR": far", $2, "esr", $4}'
echo; echo "== lldb: which image owns the biggest regions?"
lldb -b -c "$KEEP" "$W/crash.gta" -o 'image list -b -h libsystem_c.dylib dyld' \
	-o 'memory region 0x2e5be8000' -o 'memory region 0x1f875c000' 2>&1 | grep -v '^(lldb) target\|^Core file'
rm -f "$KEEP"; KEEP=
echo "== cores left from this script: $(ls /cores | grep -c "core\.")  (other writers' cores may exist)"

echo; echo "== the crash report (.ips) ReportCrash wrote for the same crash"
IPS=$(ls -t ~/Library/Logs/DiagnosticReports/crash.gta-*.ips 2>/dev/null | head -1)
if [ -n "$IPS" ]; then
	echo "$(basename "$IPS"): $(stat -f %z "$IPS") bytes"
	python3 -I - "$IPS" <<'PY'
import json, sys
t = open(sys.argv[1]).read()
hdr, body = t.split("\n", 1)
j = json.loads(body)
print("keys:", ", ".join(sorted(j.keys()))[:300])
print("exception:", j["exception"])
ft = j["threads"][j["faultingThread"]]
print("faultingThread:", j["faultingThread"], "| frames:", [f.get("symbol") for f in ft["frames"]])
print("has heap/memory contents:", any(k in j for k in ("memory", "heap", "regions")))
PY
else echo "no crash.gta-*.ips found"; fi
