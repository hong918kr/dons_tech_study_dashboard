#!/usr/bin/env python3
"""HWLAB-B01 데모 — '빠르게 짠' 감사 스크립트가 rev A 에서 어떻게 거짓말하는가 (solution.py 와 비교)

  python3 -I demo_naive.py

  ① 정규식으로 넷 이름 뽑기      : 따옴표 안의 ')' 에서 잘린다
  ② 부분 문자열 'SCL' 로 I2C 판정 : SPI_SCLK 를 I2C 로 오인 → 거짓 FAIL
  ③ DNP 무시한 풀업 검사         : 실장 안 된 R4 를 풀업으로 인정 → 진짜 결함을 놓침
  ④ 노드 1개면 무조건 FAIL        : 의도된 NC 핀까지 FAIL → 경보 피로(alarm fatigue)
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("hwlab_b01_solution", os.path.join(HERE, "solution.py"))
S = importlib.util.module_from_spec(spec)
spec.loader.exec_module(S)

text = S.read("hwb01_revA.net")
nl = S.load_netlist(text)

print("① net names: naive regex vs S-expression parser")
naive_names = re.findall(r'\(name "?([^")]+)', text.split("(nets", 1)[1])
bad = sorted(set(naive_names) - set(nl["nets"]))
print("   naive found %d names, parser %d; mangled: %s" % (len(naive_names), len(nl["nets"]), bad))

print("② I2C by substring 'SCL'/'SDA' in net name")
naive_i2c = sorted(n for n in nl["nets"] if "SCL" in n or "SDA" in n)
real_i2c = sorted(n for n in nl["nets"] if "i2c" in S.categories(nl, n))
print("   naive: %s" % naive_i2c)
print("   words: %s" % real_i2c)
for n in sorted(set(naive_i2c) - set(real_i2c)):
    p = S.find_pulls(nl, n)
    print("   -> naive would FAIL %s 'missing I2C pull-up' (pulls up=%s) - it is SPI, push-pull" % (n, p["up"]))

print("③ pull-up check ignoring DNP on /I2C2_SDA")
naive_up = [nd["ref"] for nd in nl["nets"]["/I2C2_SDA"] if nd["ref"].startswith("R")]
print("   naive: pull-up present %s -> OK" % naive_up)
print("   real : %s" % S.find_pulls(nl, "/I2C2_SDA"))

print("④ every 1-node net = FAIL ?")
ones = sorted(n for n, nodes in nl["nets"].items() if len(nodes) == 1)
for n in ones:
    print("   %-28s naive FAIL   real %s" % (n, S.classify_single(nl, n)[0]))
