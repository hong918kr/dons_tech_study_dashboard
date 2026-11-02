#!/usr/bin/env python3
"""N07E mock DDR failure logs generator (stdlib only, deterministic).

Writes logs/M0..M5 *.log. Each log = header lines (#...) + one failure record per line:
    <offset> <expected64> <actual64> [temp=C] [age=ms] [t=s] [src=...]
offset = DRAM-relative byte address (base 0), 8-byte aligned (one 64-bit CPU word).
The generator knows the injected fault; the C decoder does not — it only sees records.
Run once: python3 -I gen_mocks.py   (run.sh does NOT regenerate; logs are checked in)
"""
import os
import random

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
GB4 = 1 << 32
M64 = (1 << 64) - 1


def rnd_addr(r, lo=0, hi=GB4):
    return r.randrange(lo, hi) & ~7


def write(name, header, recs):
    with open(os.path.join(OUT, name), "w") as f:
        for h in header:
            f.write(h + "\n")
        for rec in recs:
            f.write(rec + "\n")


def rec(a, e, x, **kw):
    s = f"0x{a:09x} 0x{e:016x} 0x{x:016x}"
    for k, v in kw.items():
        s += f" {k}={v}"
    return s


def main():
    os.makedirs(OUT, exist_ok=True)
    r = random.Random(7)
    common = ["#bus 32", "#map row=31:19 bank=18:16 col=15:6 burst=5:0  (mock map, 4 GB, x32 BL16 -> 64 B burst)"]

    # M0 control: clean run
    write("M0_clean.log", ["#case M0 clean control (no failures)", "#expect CLEAN",
                           "#test march-c- 6 backgrounds, 4 GB, 25C"] + common, [])

    # M1 (E1): DQ13 stuck-high -> logical bits 13 and 45 (beat0/beat1) rise 0->1 everywhere
    recs = []
    mask = (1 << 13) | (1 << 45)
    while len(recs) < 24:
        a = rnd_addr(r)
        e = r.getrandbits(64)
        x = e | mask
        if x != e:
            recs.append(rec(a, e, x, temp=25))
    write("M1_dq13_stuck.log", ["#case M1 memtest says 'bit 13' fails (scenario E1)",
                                "#expect DQ_STUCK dq=13 lane=1 dir=0to1",
                                "#test random-value + march, 4 GB, 25C", "#tested_at 25"] + common, recs)

    # M2 (E2): byte lane 2 (DQ16..23) intermittent, both directions, bursty in time
    recs = []
    t = 100.0
    for burst in range(4):
        t += r.uniform(300, 900)
        for _ in range(r.randint(5, 9)):
            a = rnd_addr(r)
            e = r.getrandbits(64)
            beat = r.choice([0, 32])
            flips = 0
            for _ in range(r.randint(1, 3)):
                flips |= 1 << (beat + 16 + r.randrange(8))
            recs.append(rec(a, e, e ^ flips, temp=45, t=f"{t + r.uniform(0, 2):.1f}"))
    write("M2_lane2_intermittent.log", ["#case M2 byte lane 2 intermittent (scenario E2)",
                                        "#expect LANE_MARGIN lane=2",
                                        "#test stressapptest-like random copy, 1 h, 45C", "#tested_at 45"] + common, recs)

    # M3 (E3): upper 2 GB reads return lower 2 GB data. address-as-data test, write pass descending.
    recs = []
    for _ in range(16):
        a = rnd_addr(r, 1 << 31, GB4)
        recs.append(rec(a, a, a & ~(1 << 31), temp=25))
    write("M3_upper_half_alias.log", ["#case M3 upper half is a copy of lower half (scenario E3)",
                                      "#expect ADDR_ALIAS bit=31 dir=hi_reads_lo",
                                      "#pattern addr   (address-as-data: expected = own offset)",
                                      "#test write pass descending, read pass ascending, 25C", "#tested_at 25"] + common, recs)

    # M4 (E4): retention at hot: only >=95C, mostly 1->0, after long age, random lanes
    recs = []
    for temp, n in ((95, 3), (105, 21)):
        for _ in range(n):
            a = rnd_addr(r)
            e = r.getrandbits(64)
            ones = [b for b in range(64) if (e >> b) & 1]
            zeros = [b for b in range(64) if not (e >> b) & 1]
            if r.random() < 0.92 and ones:
                b = r.choice(ones)
            else:
                b = r.choice(zeros)
            recs.append(rec(a, e, e ^ (1 << b), temp=temp, age=r.choice([128, 256, 512])))
    write("M4_hot_retention.log", ["#case M4 errors only at hot after long dwell (scenario E4)",
                                   "#expect TEMP_RETENTION hot_from=95",
                                   "#test bit-fade: write, wait age ms, read (refresh running)",
                                   "#tested_at 25 65 85 95 105"] + common, recs)

    # M5 (E7): one address, one bit, thousands of CEs/day -> raw (pre-correction) data shown
    recs = []
    a = 0x3A5F1C48
    e = 0x0123456789ABCDEF | (1 << 59)
    t = 0.0
    for _ in range(40):
        t += r.uniform(15, 25)
        recs.append(rec(a, e, e & ~(1 << 59), temp=r.choice([40, 55, 70]), t=f"{t:.1f}", src="edac-ce"))
    write("M5_same_addr_ce.log", ["#case M5 same-address CE storm (scenario E7)",
                                  "#expect SINGLE_CELL addr=0x03a5f1c48 dq=27",
                                  "#test EDAC CE records -> (addr, data-as-stored, raw read) reconstructed",
                                  "#tested_at 40 55 70"] + common, recs)


if __name__ == "__main__":
    main()
