#!/usr/bin/env bash
# ARM-A09 check_reloc.sh — 실제 도구 출력 ↔ 내 리로케이션 적용기/디코더 대조.
#   bash check_reloc.sh [solution|starter]        (기본 solution → 모두 맞으면 exit 0)
# 1) 맥: shared_pic.S 를 <v>.c 와 링크한 실행 파일 + demo_sensor.c dylib/app 을 새로 빌드하고
#        /usr/bin/objdump -d/-r, nm, dyld_info -fixups 에서 사실(fact)을 뽑는다.
# 2) VM: limactl shell elinux 로 demo_elf.sh full 을 돌려 ld.bfd / ld.lld 출력(readelf -r, objdump -d, nm,
#        .rela.dyn, .got)을 받는다.  (VM 없이 맥만: ARM_A09_NO_VM=1 bash check_reloc.sh)
# 사실 한 줄 = "D 종류 P 워드 기대값" (디코드) / "A 타입 P S+A 원래워드 링크된워드" (적용) /
#              "X P S 원래워드0 원래워드1 링크된워드0 링크된워드1" (lld 완화) → <v> 의 tool 모드가 PASS/FAIL 판정.
# 도구끼리의 일관성(GOT 칸 = dyld_info bind / RELATIVE addend / .got 내용)은 파이썬이 따로 확인한다.
# 쓰는 곳: /tmp/ARM-A09_work/check 만.
set -u
V=${1:-solution}
case "$V" in solution|starter) ;; *) echo "usage: check_reloc.sh [solution|starter]"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/ARM-A09_work/check
mkdir -p "$W" && cd "$W" || exit 2
OBJDUMP=/usr/bin/objdump
DYLD_INFO=/Library/Developer/CommandLineTools/usr/bin/dyld_info

# ---- 1. 맥 빌드 + 덤프 ----
cc -std=c11 -Wall -Wextra -Werror -O2 "$SRC/$V.c" "$SRC/shared_pic.S" -o tool || exit 2
cc -c "$SRC/shared_pic.S" -o pic.o || exit 2
cc -O2 -dynamiclib "$SRC/demo_sensor.c" -o libsensor.dylib -install_name @rpath/libsensor.dylib || exit 2
cc -c "$SRC/demo_dylib_app.S" -o app.o || exit 2
cc -O2 "$SRC/demo_dylib_main.c" app.o -L. -lsensor -Wl,-rpath,@executable_path -o app || exit 2
./app >/dev/null || { echo "FAIL app run"; exit 1; }
PICFN=_pic_addr_local,_pic_load_tab2,_pic_addr_local_got,_pic_addr_ext_got,_pic_call_abs
for x in pic app; do
  $OBJDUMP -d $x.o > $x.o.dis; $OBJDUMP -r $x.o > $x.o.rel; nm $x.o > $x.o.nm
done
$OBJDUMP -d --disassemble-symbols=$PICFN tool > tool.dis; $OBJDUMP -d --section=__stubs tool >> tool.dis
$OBJDUMP -d --disassemble-symbols=_scaled app > app.dis; $OBJDUMP -d --section=__stubs app >> app.dis
nm tool > tool.nm; nm app > app.nm
$DYLD_INFO -fixups tool > tool.fix; $DYLD_INFO -fixups app > app.fix

# ---- 2. VM (ELF) 덤프 ----
if [ "${ARM_A09_NO_VM:-0}" = 1 ]; then
  echo "SKIP ELF lane (ARM_A09_NO_VM=1)"; : > elf.txt
else
  limactl shell elinux -- bash -lc "bash '$SRC/demo_elf.sh' full" > elf.txt 2>&1 || { echo "FAIL VM demo_elf.sh"; exit 1; }
fi

# ---- 3. 사실 추출 (파이썬은 숫자를 옮길 뿐, 비트 계산은 C 쪽) ----
python3 -I - "$W" > facts.txt <<'PY' || { echo "FAIL fact extraction"; cat facts.txt; exit 1; }
import re, sys, os
W = sys.argv[1]
def rd(n): return open(os.path.join(W, n)).read().splitlines()
INS = re.compile(r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)$')
out, cons = [], []          # cons = (설명, 참/거짓) 도구 사이 일관성
def dis(lines):
    m = {}
    for l in lines:
        g = INS.match(l)
        if g: m[int(g.group(1), 16)] = (int(g.group(2), 16), g.group(3), g.group(4))
    return m
def nm(lines):
    d = {}
    for l in lines:
        p = l.split()
        if len(p) == 3: d[p[2]] = (int(p[0], 16), p[1])
    return d
def num(s): return int(s, 0) if s.startswith('0x') else int(s)
def decode_lines(m):
    """objdump 가 찍은 대상/즉시값 → D 줄 (내 디코더가 같은 값을 내야 한다)"""
    for a, (w, mn, ops) in sorted(m.items()):
        if mn == 'adrp':
            t = re.search(r',\s*(?:0x)?([0-9a-f]+)', ops); out.append(f'D ADRP {a:x} {w:08x} {t.group(1)}')
        elif mn in ('b', 'bl'):
            t = re.match(r'(?:0x)?([0-9a-f]+)', ops); out.append(f'D B26 {a:x} {w:08x} {t.group(1)}')
        elif mn == 'add' and '#' in ops and 'lsl' not in ops:
            out.append(f'D LO12 {a:x} {w:08x} {num(ops.split("#")[1].split()[0]):x}')
        elif mn in ('ldr', 'ldrh', 'ldrb', 'str') and '[' in ops and '!' not in ops and not re.search(r'\],', ops):
            t = re.search(r'#(\S+?)\]', ops); out.append(f'D LO12 {a:x} {w:08x} {num(t.group(1)) if t else 0:x}')
def is_add(w): return (w & 0x7F800000) == 0x11000000

# ---------- Mach-O ----------
def macho(obj, exe):
    od, xd = dis(rd(obj + '.o.dis')), dis(rd(exe + '.dis'))
    onm, xnm = nm(rd(obj + '.o.nm')), nm(rd(exe + '.nm'))
    fix = {}
    for l in rd(exe + '.fix'):
        p = l.split()
        if len(p) == 5 and p[3] == 'bind': fix[int(p[2], 16)] = p[4].split('/')[-1]
    rel, addend = [], {}
    for l in rd(obj + '.o.rel'):
        p = l.split()
        if len(p) == 3 and p[1].startswith('ARM64_RELOC_'):
            off, t = int(p[0], 16), p[1][len('ARM64_RELOC_'):]
            if t == 'ADDEND': addend[off] = int(p[2], 16)
            else: rel.append((off, t, p[2]))
    rel.sort()
    funcs = sorted((v[0], k) for k, v in onm.items() if v[1] in 'Tt' and k in xnm and xnm[k][1] in 'Tt')
    def P_of(off):
        base = max((v, k) for v, k in funcs if v <= off)
        return xnm[base[1]][0] + off - base[0]
    pend = {}
    for off, t, s in rel:
        P = P_of(off); orig = od[off][0]; lw, mn, ops = xd[P]
        A = addend.get(off, 0)
        if t in ('PAGE21', 'PAGEOFF12'):
            out.append(f'A {t} {P:x} {xnm[s][0] + A:x} {orig:08x} {lw:08x}')
        elif t == 'GOT_LOAD_PAGE21':
            pend[s] = (P, orig, lw)
        elif t == 'GOT_LOAD_PAGEOFF12':
            P1, o1, w1 = pend.pop(s)
            if is_add(lw):          # 링커가 GOT 를 없앴다 (같은 이미지)
                S = xnm[s][0]
                cons.append((f'{exe}: {s} GOT load relaxed to ADD, symbol defined in image', s in xnm and xnm[s][1] != 'U'))
                out.append(f'A PAGE21 {P1:x} {S:x} {o1:08x} {w1:08x}')
                out.append(f'A RELAX_ADD {P:x} {S:x} {orig:08x} {lw:08x}')
            else:
                G = int(re.search(r',\s*0x([0-9a-f]+)', xd[P1][2]).group(1), 16)
                t2 = re.search(r'#(0x[0-9a-f]+)\]', ops); G += int(t2.group(1), 16) if t2 else 0
                cons.append((f'{exe}: GOT slot 0x{G:x} is dyld_info bind -> {s}', fix.get(G) == s))
                out.append(f'A GOT_LOAD_PAGE21 {P1:x} {G:x} {o1:08x} {w1:08x}')
                out.append(f'A GOT_LOAD_PAGEOFF12 {P:x} {G:x} {orig:08x} {lw:08x}')
        elif t == 'BRANCH26':
            if s in xnm and xnm[s][1] in 'Tt': S = xnm[s][0]
            else:                   # dylib 함수 → stub. stub 이 읽는 GOT 칸이 그 심볼의 bind 인지 확인
                S = int(re.match(r'0x([0-9a-f]+)', ops).group(1), 16)
                sa, sb = xd[S], xd[S + 4]
                G = int(re.search(r',\s*0x([0-9a-f]+)', sa[2]).group(1), 16)
                t2 = re.search(r'#(0x[0-9a-f]+)\]', sb[2]); G += int(t2.group(1), 16) if t2 else 0
                cons.append((f'{exe}: stub 0x{S:x} -> GOT 0x{G:x} bind -> {s}', fix.get(G) == s and sa[1] == 'adrp' and sb[1] == 'ldr'))
            out.append(f'A BRANCH26 {P:x} {S:x} {orig:08x} {lw:08x}')
    decode_lines(xd)
macho('pic', 'tool')
macho('app', 'app')

# ---------- ELF (VM) ----------
elf = rd('elf.txt')
if elf:
    sec, cur = {}, None
    for l in elf:
        if l.startswith('== '): cur = l[3:].strip(); sec[cur] = []
        elif cur: sec[cur].append(l)
    rel = []
    for l in sec['readelf -rW demo_elf.o']:
        p = l.split()
        if len(p) >= 5 and p[2].startswith('R_AARCH64_') and '+' in p:
            rel.append((int(p[0], 16), p[2][len('R_AARCH64_'):], p[4], int(p[p.index('+') + 1], 16)))
        if l.startswith("Relocation section '.rela.data'"): break
    od = dis(sec['objdump -d demo_elf.o'])
    def reladyn(name):
        r = {}
        for l in sec.get(name, []):
            p = l.split()
            if len(p) >= 4 and p[2] == 'R_AARCH64_RELATIVE': r[int(p[0], 16)] = int(p[3], 16)
        return r
    got = {}
    for l in sec.get('objdump -s -j .got static_bfd', []):
        p = l.split()
        if len(p) >= 5 and re.fullmatch(r'[0-9a-f]+', p[0]):
            b = bytes.fromhex(''.join(p[1:5]))
            for i in range(0, len(b) - 7, 8): got[int(p[0], 16) + i] = int.from_bytes(b[i:i + 8], 'little')
    dyn = {'pie_bfd': reladyn('readelf -rW pie_bfd (.rela.dyn)'),
           'pie_lld_norelax': reladyn('readelf -rW pie_lld_norelax (.rela.dyn)')}
    for B in ('pie_bfd', 'static_bfd', 'pie_lld_norelax', 'pie_lld'):
        xnm, xd = nm(sec['nm ' + B]), dis(sec['objdump -d %s use_all' % B])
        base = xnm['use_all'][0]
        i = 0
        while i < len(rel):
            off, t, s, A = rel[i]
            P = base + off; orig = od[off][0]; lw = xd[P][0]; S = xnm[s][0] + A
            if B == 'pie_lld' and lw == 0xd503201f:          # 완화된 쌍: NOP + ADR
                o2, l2 = od[off + 4][0], xd[P + 4][0]
                out.append(f'X {P:x} {S:x} {orig:08x} {o2:08x} {lw:08x} {l2:08x}')
                i += 2; continue
            if t in ('ADR_GOT_PAGE', 'LD64_GOT_LO12_NC'):
                Pa = P if t == 'ADR_GOT_PAGE' else P - 4
                G = int(re.search(r',\s*([0-9a-f]+)', xd[Pa][2]).group(1), 16)
                G += int(re.search(r'#(\d+)\]', xd[Pa + 4][2]).group(1))
                if B == 'static_bfd': ok = got.get(G) == S
                else: ok = dyn.get(B, {}).get(G) == S
                if t == 'ADR_GOT_PAGE':
                    cons.append((f'{B}: GOT slot 0x{G:x} holds/relocates to {s}=0x{S:x}', ok))
                out.append(f'A {t} {P:x} {G:x} {orig:08x} {lw:08x}')
            else:
                out.append(f'A {t} {P:x} {S:x} {orig:08x} {lw:08x}')
            i += 1
        decode_lines(xd)
    rng, on = [], False
    for l in elf:
        if l.startswith('== range'): on = True
        elif l.startswith('== ') and on: break
        elif on: rng.append(l)
    decode_lines(dis(rng))

seen = set()
for l in out:
    if l not in seen: seen.add(l); print(l)
for d, ok in cons: print(f'# CONS {"PASS" if ok else "FAIL"} {d}')
PY

# ---- 4. 판정 ----
grep '^# CONS' facts.txt | sed 's/^# CONS /cross-tool /'
CONS_FAIL=$(grep -c '^# CONS FAIL' facts.txt)
./tool tool < facts.txt > verdict.txt; RC=$?
grep -v '^PASS' verdict.txt
echo "($(grep -c '^PASS' verdict.txt) PASS lines from $V decoder; full list: $W/verdict.txt)"
if [ $RC -eq 0 ] && [ "$CONS_FAIL" -eq 0 ]; then echo "check_reloc: OK"; exit 0; fi
echo "check_reloc: MISMATCH"; exit 1
