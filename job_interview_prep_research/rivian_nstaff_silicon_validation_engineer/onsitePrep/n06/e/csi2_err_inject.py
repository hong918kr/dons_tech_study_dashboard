#!/usr/bin/env python3
"""N06E E3 - "test of the test" for a CSI-2 receiver's error detection.

What a validation engineer wants to know before writing pass criteria:
  * header ECC (6-bit SEC-DED Hamming over the 24-bit header): which error
    patterns are corrected, detected, or SILENTLY MISCORRECTED?
  * payload CRC-16 (x^16+x^12+x^5+1, init 0xFFFF, LSB-first): which payload
    error patterns slip through?
This drives the error-injection rows of the test matrix: you inject exactly
the classes the code is guaranteed to catch, and you know which classes need
a second line of defence (word-count / frame-structure checks).

ECC equations are the public CSI-2 v1.x ones (same as hmScreenPrep V9).
Python stdlib only.  Run:  python3 -I csi2_err_inject.py
"""
import itertools
import random

# data bits feeding P0..P5 (CSI-2 v1.x, 24-bit header D[23:0])
P_BITS = [
    [0, 1, 2, 4, 5, 7, 10, 11, 13, 16, 20, 21, 22, 23],
    [0, 1, 3, 4, 6, 8, 10, 12, 14, 17, 20, 21, 22, 23],
    [0, 2, 3, 5, 6, 9, 11, 12, 15, 18, 20, 21, 22],
    [1, 2, 3, 7, 8, 9, 13, 14, 15, 19, 20, 21, 23],
    [4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 22, 23],
    [10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23],
]
MASK = [sum(1 << b for b in bits) for bits in P_BITS]
COL = None  # syndrome of a lone data bit i


def ecc(d24):
    return sum((bin(d24 & m).count("1") & 1) << i for i, m in enumerate(MASK))


COL = {ecc(1 << i): i for i in range(24)}


def decode(d24, e6):
    """receiver model: returns ('ok'|'corr'|'uncorr', corrected d24)"""
    syn = e6 ^ ecc(d24)
    if syn == 0:
        return "ok", d24
    if bin(syn).count("1") == 1:          # an ECC bit itself flipped
        return "corr", d24
    if syn in COL:                        # a data bit flipped -> fix it
        return "corr", d24 ^ (1 << COL[syn])
    return "uncorr", d24


def crc16(data):
    c = 0xFFFF
    for byte in data:
        c ^= byte
        for _ in range(8):
            c = (c >> 1) ^ 0x8408 if c & 1 else c >> 1
    return c


def flip(buf, bitpos):
    buf[bitpos >> 3] ^= 1 << (bitpos & 7)


def header_campaign(d24):
    e6 = ecc(d24)
    cw = d24 | (e6 << 24)                 # 30 meaningful bits
    res = {}
    for k in (1, 2, 3):
        tally = {"corrected_right": 0, "detected": 0, "MISCORRECTED": 0, "undetected": 0}
        for pos in itertools.combinations(range(30), k):
            bad = cw
            for p in pos:
                bad ^= 1 << p
            st, fixed = decode(bad & 0xFFFFFF, bad >> 24)
            if st == "uncorr":
                tally["detected"] += 1
            elif fixed == d24:
                tally["corrected_right"] += 1
            elif st == "ok":
                tally["undetected"] += 1
            else:
                tally["MISCORRECTED"] += 1
        res[k] = tally
    return res


def lin_step(x):
    return (x >> 1) ^ 0x8408 if x & 1 else x >> 1


def bit_syndromes(nbits):
    """CRC is linear: crc(p ^ e) = crc(p) ^ S(e), S(e) = XOR of per-bit syndromes.
    Flipping stream bit i changes the register by lin_step(1) at step i, then
    (nbits-1-i) more zero-input steps.  So s[n-1] = step(1), s[i] = step(s[i+1])."""
    s = [0] * nbits
    s[-1] = lin_step(1)
    for i in range(nbits - 2, -1, -1):
        s[i] = lin_step(s[i + 1])
    return s


def payload_campaign(nbytes, trials, rng):
    payload = bytearray(rng.randrange(256) for _ in range(nbytes))
    good = crc16(payload)
    nbits = nbytes * 8
    syn = bit_syndromes(nbits)
    # cross-check the linear model against the real CRC on 200 random 3-bit errors
    for _ in range(200):
        pos = rng.sample(range(nbits), 3)
        b = bytearray(payload)
        for p in pos:
            flip(b, p)
        x = 0
        for p in pos:
            x ^= syn[p]
        assert crc16(b) == good ^ x, "linear syndrome model wrong"
    out = {}
    for name, gen in (
        ("1-bit", lambda: [rng.randrange(nbits)]),
        ("2-bit", lambda: rng.sample(range(nbits), 2)),
        ("3-bit", lambda: rng.sample(range(nbits), 3)),
        ("burst<=16", lambda: burst(rng, nbits, 16)),
        ("burst 17-32", lambda: burst(rng, nbits, 32, lo=17)),
        ("4-bit random", lambda: rng.sample(range(nbits), 4)),
    ):
        missed = 0
        for _ in range(trials):
            x = 0
            for p in gen():
                x ^= syn[p]
            if x == 0:
                missed += 1
        out[name] = missed
    return out


def burst(rng, nbits, maxlen, lo=2):
    L = rng.randint(lo, maxlen)
    start = rng.randrange(nbits - L)
    pos = [start, start + L - 1]                 # burst = first and last bit flipped
    pos += [p for p in range(start + 1, start + L - 1) if rng.random() < 0.5]
    return pos


def main():
    print("== CSI-2 header ECC sanity ==")
    checks = [(0x000A2B, 0x2E), (0x000100, 0x1A), (0x09602B, 0x1D)]
    for d, want in checks:
        got = ecc(d)
        print(f"ecc(0x{d:06X}) = 0x{got:02X}  expect 0x{want:02X}  {'PASS' if got == want else 'FAIL'}")
    hdr = 0x0B402C                       # VC0, DT 0x2C RAW12, WC 0x0B40 = 2880 B (1920 px RAW12)
    print(f"\n== header error injection on DI/WC 0x{hdr:06X} (all patterns over 30 bits) ==")
    res = header_campaign(hdr)
    for k, t in res.items():
        total = sum(t.values())
        print(f"{k}-bit errors: {total:5d} patterns  " + "  ".join(f"{n}={v}" for n, v in t.items()))
    ok1 = res[1]["corrected_right"] == 30
    ok2 = res[2]["detected"] == 435
    print(f"PASS all 1-bit corrected: {ok1}   PASS all 2-bit detected: {ok2}")
    mis = res[3]["MISCORRECTED"]
    print(f"3-bit: {mis}/{sum(res[3].values())} = {100*mis/sum(res[3].values()):.1f}% silently MISCORRECTED"
          " -> header looks valid but VC/DT/WC is wrong")

    rng = random.Random(2026)
    trials = 200000
    print(f"\n== payload CRC-16 injection, 2880-byte RAW12 line, {trials} trials per class, seed 2026 ==")
    pc = payload_campaign(2880, trials, rng)
    for name, missed in pc.items():
        print(f"{name:13s} undetected {missed:5d}/{trials}")
    guaranteed = all(pc[n] == 0 for n in ("1-bit", "2-bit", "3-bit", "burst<=16"))
    print(f"PASS guaranteed classes (1/2/3-bit, burst<=16) all detected: {guaranteed}")
    print("model cross-check vs real CRC on 200 random 3-bit errors: PASS")
    print("note: random 4-bit / long-burst misses are possible at ~2^-16 = 1.5e-5 per bad line")


if __name__ == "__main__":
    main()
