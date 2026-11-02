#!/usr/bin/env python3
"""BV08: parse '@@' result lines from a bring-up FW UART log.

Usage: python3 -I parse_results.py <uart_log>
- Accepts noise between lines (human '#' lines, shell echo, boot banners).
- Per @@BEGIN..@@END block: checks SUMMARY counts against the TEST lines it
  actually saw (a missing @@END or count mismatch = the FW hung/rebooted
  mid-run -> verdict INCOMPLETE).
- Exit code 0 only if every block is PASS.
"""
import re
import shlex
import sys

KV = re.compile(r'^@@(\w+)\s*(.*)$')


def kv(rest):
    out = {}
    for tok in shlex.split(rest):
        if '=' in tok:
            k, v = tok.split('=', 1)
            out[k] = v
    return out


def parse(lines):
    runs, cur = [], None
    for raw in lines:
        m = KV.match(raw.strip())
        if not m:
            continue
        tag, fields = m.group(1), kv(m.group(2))
        if tag == 'BEGIN':
            if cur is not None:          # previous run never ended
                cur['verdict'] = 'INCOMPLETE'
                runs.append(cur)
            cur = {'meta': fields, 'tests': [], 'summary': None}
        elif tag == 'TEST' and cur is not None:
            cur['tests'].append(fields)
        elif tag == 'SUMMARY' and cur is not None:
            cur['summary'] = fields
        elif tag == 'END' and cur is not None:
            s, t = cur['summary'], cur['tests']
            if s is None or int(s['total']) != len(t):
                cur['verdict'] = 'INCOMPLETE'
            else:
                bad = [x for x in t if x['result'] in ('FAIL', 'TIMEOUT')]
                cur['verdict'] = 'FAIL' if bad else 'PASS'
            runs.append(cur)
            cur = None
    if cur is not None:
        cur['verdict'] = 'INCOMPLETE'
        runs.append(cur)
    return runs


def main():
    with open(sys.argv[1], encoding='utf-8') as f:
        runs = parse(f)
    ok = True
    for i, r in enumerate(runs):
        meta = r['meta']
        print(f"run {i}: plat={meta.get('plat')} bugs={meta.get('bugs')} "
              f"tests_seen={len(r['tests'])} verdict={r['verdict']}")
        for t in r['tests']:
            if t['result'] != 'PASS':
                print(f"    {t['result']:<7} {t['name']:<20} {t.get('detail', '')}")
        ok &= r['verdict'] == 'PASS'
    print('OVERALL', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
