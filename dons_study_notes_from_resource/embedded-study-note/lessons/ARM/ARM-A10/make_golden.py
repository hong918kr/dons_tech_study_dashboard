#!/usr/bin/env python3
"""ARM-A10 make_golden.py — gen_mac.txt / gen_vm.txt (실제 objdump 출력) → C 초기화 줄.
   python3 make_golden.py   # 출력 줄을 solution.c 의 golden[] 표에 붙여 넣었다 (starter/practice 도 같은 표)
   AArch64 변형만 (cortex_m85 는 Thumb 이라 제외)."""
import re, sys
FUNCS = ("leaf_add", "nonleaf", "early_exit", "tail_call", "call_ptr")
def parse(path, prefix):
    out, var, fn = [], None, None
    for line in open(path):
        m = re.match(r"##### (?:variant=)?([\w ]+?)(?::|  flags)", line)
        if m:
            var = prefix + m.group(1).strip().replace(" ", "_"); fn = None; continue
        m = re.match(r"[0-9a-f]+ <_?(\w+)>:", line)
        if m:
            fn = m.group(1) if m.group(1) in FUNCS else None
            if fn and var and "cortex" not in var: out.append([var, fn, []])
            continue
        m = re.match(r"\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", line)
        if m and fn and var and "cortex" not in var: out[-1][2].append(m.group(1))
    return out
rows = parse("gen_mac.txt", "mac_") + parse("gen_vm.txt", "vm_")
for var, fn, ws in rows:
    assert len(ws) <= 24, (var, fn, len(ws))
    print('    {"%s", "%s", %d, {%s}},' % (var, fn, len(ws), ", ".join("0x" + w for w in ws)))
print("/* %d functions */" % len(rows), file=sys.stderr)
