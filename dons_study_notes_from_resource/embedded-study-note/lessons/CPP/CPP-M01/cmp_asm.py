#!/usr/bin/env python3
# CPP-M01: objdump 출력에서 cpp_X 와 c_X 함수 본문을 명령 단위로 비교한다.
#   bash asm_target.sh asm -O2 | python3 cmp_asm.py
import re,sys
txt=sys.stdin.read()
funcs={}
cur=None
for l in txt.splitlines():
    m=re.match(r'^[0-9a-f]+ <([^>]+)>:',l)
    if m: cur=m.group(1); funcs[cur]=[]; continue
    if cur is None: continue
    m=re.match(r'^\s*[0-9a-f]+:\s+(.*)$',l)
    if not m: continue
    ins=m.group(1).split('@')[0].strip()
    if ins in('nop',) or ins.startswith('.word'): continue
    ins=re.sub(r'0x[0-9a-f]+ <',"<",ins); ins=re.sub(r'<[^>]*>','<>',ins)
    ins=re.sub(r'^b(\w*)(\.\w)?\s+[0-9a-f]+', r'b\1 ',ins)
    funcs[cur].append(ins)
names=sorted(n for n in funcs if n.startswith('cpp_') and 'c_'+n[4:] in funcs)
for n in names:
    c='c_'+n[4:]
    print(f"{n[4:]:12} cpp={len(funcs[n]):2} c={len(funcs[c]):2} same={funcs[n]==funcs[c]}")
