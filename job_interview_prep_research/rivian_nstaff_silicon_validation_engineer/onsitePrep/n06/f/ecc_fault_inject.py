#!/usr/bin/env python3
"""N06F F4 - fault-injection campaign on a SEC-DED (72,64) ECC model -> diagnostic coverage.

Code: extended Hamming. Bit positions 1..71 = Hamming(71,64) with check bits at 1,2,4,..,64,
data in the other 64 positions; bit 0 = overall parity. Decoder outcomes:
  OK   syndrome 0, parity ok          -> no error reported
  CE   parity odd                     -> single error, corrected (flip bit 'syndrome')
  DUE  syndrome != 0, parity even     -> detected uncorrectable (or syndrome points outside 0..71)
Campaign classifies each injection by comparing decoded data with what was written:
  corrected : CE and data correct
  detected  : DUE reported
  SDC       : silent data corruption = data wrong but reported OK or CE (miscorrection)
  no_effect : reported OK and data correct (e.g. flips only hit check bits of a dead checker)
DC (diagnostic coverage, per fault model) = (corrected + detected) / injections.
Also runs a BUGGY decoder (syndrome bit 2 stuck at 0, an RTL-style bug) and a 'dead checker'
(always OK) to show that the campaign itself can tell a broken mechanism. Python stdlib only.
"""
import random

N_POS = 72
CHECK = [1, 2, 4, 8, 16, 32, 64]
DATA_POS = [p for p in range(1, N_POS) if p not in CHECK]
assert len(DATA_POS) == 64


def encode(data):
    w = 0
    for i, p in enumerate(DATA_POS):
        if (data >> i) & 1:
            w |= 1 << p
    syn = 0
    for p in DATA_POS:
        if (w >> p) & 1:
            syn ^= p
    for c in CHECK:
        if syn & c:
            w |= 1 << c
    if bin(w).count('1') & 1:
        w |= 1                                   # overall parity -> even weight
    return w


def extract(w):
    d = 0
    for i, p in enumerate(DATA_POS):
        if (w >> p) & 1:
            d |= 1 << i
    return d


def syndrome(w):
    s = 0
    for p in range(1, N_POS):
        if (w >> p) & 1:
            s ^= p
    return s


def decode(w, bug=None):
    if bug == 'dead':
        return 'OK', extract(w)
    s = syndrome(w)
    if bug == 'syn2_stuck0':
        s &= ~4
    odd = bin(w).count('1') & 1
    if s == 0 and not odd:
        return 'OK', extract(w)
    if odd:
        if s >= N_POS:
            return 'DUE', None
        return 'CE', extract(w ^ (1 << s))       # s == 0 -> the parity bit itself
    return 'DUE', None


def campaign(nbits, n, rng, bug=None):
    res = {'corrected': 0, 'detected': 0, 'SDC': 0, 'no_effect': 0}
    for _ in range(n):
        data = rng.getrandbits(64)
        w = encode(data)
        for p in rng.sample(range(N_POS), nbits):
            w ^= 1 << p
        st, out = decode(w, bug)
        if st == 'DUE':
            res['detected'] += 1
        elif out != data:
            res['SDC'] += 1
        elif st == 'CE':
            res['corrected'] += 1
        else:
            res['no_effect'] += 1
    return res


def main():
    rng = random.Random(26262)
    # sanity: clean words decode OK
    for _ in range(1000):
        d = rng.getrandbits(64)
        assert decode(encode(d)) == ('OK', d)
    # exhaustive single-bit check on one word: every position must be corrected
    d = 0x0123456789ABCDEF
    w = encode(d)
    single_ok = sum(decode(w ^ (1 << p)) == ('CE', d) for p in range(N_POS))
    print(f"exhaustive 1-bit sweep on one word: {single_ok}/{N_POS} positions corrected")

    N = 20000
    for label, bug in (('reference decoder', None), ('BUG: syndrome bit2 stuck-at-0', 'syn2_stuck0'),
                       ('BUG: dead checker (always OK)', 'dead')):
        print(f"\n== {label} : {N} random injections per fault model ==")
        print(f"  {'fault':<6} {'corrected':>9} {'detected':>9} {'SDC':>7} {'no_eff':>7} {'DC':>8}")
        for nbits in (1, 2, 3):
            r = campaign(nbits, N, rng, bug)
            dc = (r['corrected'] + r['detected']) / N
            print(f"  {nbits}-bit  {r['corrected']:9d} {r['detected']:9d} {r['SDC']:7d} {r['no_effect']:7d} {100 * dc:7.2f}%")
    print("\nnote: DC here is per fault model of this one mechanism; ISO 26262 SPFM/LFM weight each"
          " mechanism's DC by the FIT rate of the logic it protects (FMEDA).")


if __name__ == '__main__':
    main()
