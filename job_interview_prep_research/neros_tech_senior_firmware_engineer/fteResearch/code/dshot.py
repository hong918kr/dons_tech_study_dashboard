#!/usr/bin/env python3
"""dshot.py — DShot frame encoder / decoder + bidirectional DShot (GCR eRPM telemetry) model.

Standard library only. Python 3.9+. Run: python3 dshot.py  -> prints PASS at the end.

FC -> ESC frame (16 bits, MSB first on the wire):

    bit 15 .............. 5 | 4 | 3 .. 0
        throttle/command 11 | T |  CRC 4
    value 0      = disarmed / motor stop (also command 0)
    value 1..47  = special commands (only acted on while motor stopped)
    value 48..2047 = throttle (2000 steps)
    CRC normal  = (v ^ v>>4 ^ v>>8) & 0xF        where v = (value<<1)|T  (12 bits)
    CRC bidir   = (~(v ^ v>>4 ^ v>>8)) & 0xF     (inverted -> ESC knows to answer with eRPM)

ESC -> FC answer in bidirectional mode (same wire, ~30 us later):
    16 bits = 12-bit data (eee mmmmmmmmm: period_us = m << e) + 4-bit CRC (xor of all 4 nibbles == 0xF)
    -> GCR: each nibble -> 5 bits (20 bits), prefix 0 -> 21 bits, NRZI-like: a 1 flips the line
    -> sent at 5/4 of the DShot bit rate (DShot600 -> 750 kbit/s)

Verified against (2026-10-24):
  - Betaflight src/main/drivers/dshot.c prepareDshotPacket() (csum loop, ~csum when useDshotTelemetry)
  - Betaflight src/main/drivers/dshot_bitbang_decode.c decode_bb_value() (GCR table, csum == 0xF check)
  - Betaflight src/main/drivers/dshot.c dshot_decode_eRPM_telemetry_value() (0x0fff -> 0, period -> eRPM/100)
  - Betaflight src/main/drivers/dshot_command.h (DSHOT_CMD_MAX = 47), dshot.h (MIN 48, MAX 2047)
  - brushlesswhoop.com/dshot-and-bidirectional-dshot (bit timing table, 30 us gap, 5/4 rate, EDT)
"""

from __future__ import annotations

import random
from typing import List, Optional, Tuple

DSHOT_MIN_THROTTLE = 48
DSHOT_MAX_THROTTLE = 2047
DSHOT_CMD_MAX = 47

RATES_KBPS = {150: 150, 300: 300, 600: 600, 1200: 1200}


# ---------------------------------------------------------------------------
# FC -> ESC frame
# ---------------------------------------------------------------------------
def dshot_crc(v12: int, bidir: bool = False) -> int:
    c = v12 ^ (v12 >> 4) ^ (v12 >> 8)
    if bidir:
        c = ~c
    return c & 0xF


def encode_frame(value: int, telemetry: bool = False, bidir: bool = False) -> int:
    if not 0 <= value <= DSHOT_MAX_THROTTLE:
        raise ValueError("DShot value must be 0..2047")
    v12 = (value << 1) | (1 if telemetry else 0)
    return (v12 << 4) | dshot_crc(v12, bidir)


def encode_frame_betaflight_style(value: int, telemetry: bool, bidir: bool) -> int:
    """Line-by-line port of Betaflight prepareDshotPacket(), for cross-checking."""
    packet = (value << 1) | (1 if telemetry else 0)
    csum = 0
    csum_data = packet
    for _ in range(3):
        csum ^= csum_data
        csum_data >>= 4
    if bidir:
        csum = ~csum
    csum &= 0xF
    return (packet << 4) | csum


def decode_frame(frame: int, bidir: bool = False) -> Optional[Tuple[int, bool]]:
    """Return (value, telemetry_bit) or None if CRC is wrong."""
    frame &= 0xFFFF
    v12 = frame >> 4
    if (frame & 0xF) != dshot_crc(v12, bidir):
        return None
    return v12 >> 1, bool(v12 & 1)


def classify(value: int) -> str:
    if value == 0:
        return "motor_stop"
    if value <= DSHOT_CMD_MAX:
        return "command"
    return "throttle"


# ---------------------------------------------------------------------------
# timing
# ---------------------------------------------------------------------------
def bit_timing_us(rate: int) -> dict:
    """Bit period and high times. T1H = 75 % of bit, T0H = 37.5 % (1 has twice the high time of 0)."""
    bit = 1000.0 / RATES_KBPS[rate]          # us
    return {"bit": bit, "t1h": bit * 0.75, "t0h": bit * 0.375, "frame": 16 * bit}


def frame_to_waveform(frame: int, rate: int, inverted: bool = False, step_ns: int = 50) -> List[int]:
    """Sample the 16-bit frame as a logic-analyzer-like level list (1 sample per step_ns).
    Normal DShot idles low and each bit starts high; bidirectional DShot is inverted."""
    t = bit_timing_us(rate)
    n_bit = int(round(t["bit"] * 1000 / step_ns))
    out: List[int] = []
    for i in range(15, -1, -1):
        high = t["t1h"] if (frame >> i) & 1 else t["t0h"]
        n_high = int(round(high * 1000 / step_ns))
        bits = [1] * n_high + [0] * (n_bit - n_high)
        out += [1 - b for b in bits] if inverted else bits
    return out


def waveform_to_frame(samples: List[int], rate: int, inverted: bool = False, step_ns: int = 50) -> int:
    """Decode the waveform back by measuring high time per bit slot (what a sigrok-style decoder does)."""
    t = bit_timing_us(rate)
    n_bit = int(round(t["bit"] * 1000 / step_ns))
    thresh = (t["t1h"] + t["t0h"]) / 2 * 1000 / step_ns
    frame = 0
    for i in range(16):
        slot = samples[i * n_bit:(i + 1) * n_bit]
        if inverted:
            slot = [1 - s for s in slot]
        frame = (frame << 1) | (1 if sum(slot) > thresh else 0)
    return frame


# ---------------------------------------------------------------------------
# bidirectional telemetry (ESC -> FC)
# ---------------------------------------------------------------------------
GCR_ENC = [0x19, 0x1B, 0x12, 0x13, 0x1D, 0x15, 0x16, 0x17,
           0x1A, 0x09, 0x0A, 0x0B, 0x1E, 0x0D, 0x0E, 0x0F]
_IV = None
GCR_DEC = [_IV] * 32
for _n, _g in enumerate(GCR_ENC):
    GCR_DEC[_g] = _n


def erpm_period_encode(period_us: int) -> int:
    """period (us per electrical revolution) -> 12-bit eee mmmmmmmmm (lossy, like the ESC)."""
    if period_us <= 0:
        raise ValueError("period must be > 0")
    e = 0
    m = period_us
    while m > 0x1FF:
        m >>= 1
        e += 1
    if e > 7:
        return 0xFFF   # too slow: ESC reports 0x0FFF = motor stopped
    return (e << 9) | m


def erpm_period_decode(v12: int) -> int:
    """Betaflight dshot_decode_eRPM_telemetry_value(): returns eRPM/100 (0 if stopped)."""
    if v12 == 0x0FFF:
        return 0
    period = (v12 & 0x1FF) << ((v12 & 0xE00) >> 9)
    if period == 0:
        return -1
    return (1000000 * 60 // 100 + period // 2) // period


def telem_word(v12: int) -> int:
    n2, n1, n0 = (v12 >> 8) & 0xF, (v12 >> 4) & 0xF, v12 & 0xF
    crc = (~(n2 ^ n1 ^ n0)) & 0xF            # makes xor of all 4 nibbles == 0xF
    return (v12 << 4) | crc


def telem_word_ok(w16: int) -> bool:
    c = w16 ^ (w16 >> 8)
    c ^= c >> 4
    return (c & 0xF) == 0xF


def gcr_encode_line(w16: int) -> int:
    """16-bit word -> 21-bit line levels (MSB first). Leading 0, then each GCR 1 toggles the level."""
    gcr = 0
    for shift in (12, 8, 4, 0):
        gcr = (gcr << 5) | GCR_ENC[(w16 >> shift) & 0xF]
    level = 0
    line = 0           # first (start) bit = 0
    for i in range(19, -1, -1):
        if (gcr >> i) & 1:
            level ^= 1
        line = (line << 1) | level
    return line        # 21 bits total (top bit is the 0 start bit)


def gcr_decode_line(line21: int) -> Optional[int]:
    """Inverse: gcr = line ^ (line >> 1), then 5b->4b table; returns 16-bit word or None."""
    gcr = (line21 ^ (line21 >> 1)) & 0xFFFFF
    w = 0
    for shift in (15, 10, 5, 0):
        nib = GCR_DEC[(gcr >> shift) & 0x1F]
        if nib is None:
            return None
        w = (w << 4) | nib
    return w


def gcr_decode_runlengths(line21: int) -> Optional[int]:
    """Betaflight-style: each run of n equal levels becomes '1' followed by n-1 zeros."""
    bits = [(line21 >> i) & 1 for i in range(20, -1, -1)]
    runs = []
    cur, n = bits[0], 0
    for b in bits:
        if b == cur:
            n += 1
        else:
            runs.append(n)
            cur, n = b, 1
    runs.append(n)
    value = 0
    for n in runs[1:]:          # first run contains the start bit level, as in decode_bb (start bit discarded)
        value = (value << n) | (1 << (n - 1))
    # the first run = start bit + (runs[0]-1) leading GCR zeros, so runs[1:] already
    # form the low bits of the 20-bit GCR word; no shifting needed
    assert (runs[0] - 1) + sum(runs[1:]) == 20
    w = 0
    for shift in (15, 10, 5, 0):
        nib = GCR_DEC[(value >> shift) & 0x1F]
        if nib is None:
            return None
        w = (w << 4) | nib
    return w


def edt_decode(v12: int) -> Tuple[str, int]:
    """Extended DShot Telemetry: if bit8 (lowest exponent bit) is 0 and e != 0 -> EDT frame."""
    t = (v12 >> 8) & 0xF
    if (t & 1) or t == 0:
        return "erpm", v12
    names = {0x2: "temp_C", 0x4: "voltage_0.25V", 0x6: "current_A", 0x8: "debug1",
             0xA: "debug2", 0xC: "debug3", 0xE: "state_event"}
    return names.get(t, "unknown"), v12 & 0xFF


# ---------------------------------------------------------------------------
# self tests
# ---------------------------------------------------------------------------
def self_test() -> None:
    rng = random.Random(42)

    # 1. fixed vectors: motor stop
    assert encode_frame(0) == 0x0000
    assert encode_frame(0, bidir=True) == 0x000F
    # value 48 (min throttle): v12 = 96 = 0x060 -> crc = 0x0^0x6^0x0 = 6
    assert encode_frame(48) == 0x0606
    # telemetry bit set changes CRC: v12 = 97 = 0x061 -> crc = 1^6 = 7
    assert encode_frame(48, telemetry=True) == 0x0617
    assert encode_frame(48, bidir=True) == 0x0609

    # 2. cross-check against BF-style implementation for all values, both modes
    for v in range(2048):
        for tlm in (False, True):
            for bd in (False, True):
                assert encode_frame(v, tlm, bd) == encode_frame_betaflight_style(v, tlm, bd)
                assert decode_frame(encode_frame(v, tlm, bd), bd) == (v, tlm)

    # 3. a normal frame is NOT valid as bidir and vice versa (CRC inverted) for every value
    for v in range(2048):
        assert decode_frame(encode_frame(v), bidir=True) is None
        assert decode_frame(encode_frame(v, bidir=True), bidir=False) is None

    # 4. every single-bit error is detected (4-bit XOR CRC catches all 1-bit flips)
    for v in rng.sample(range(2048), 200):
        f = encode_frame(v)
        for b in range(16):
            assert decode_frame(f ^ (1 << b)) is None
    # ...but some 2-bit errors are NOT detected: same bit position in two nibbles -> FTE must know
    undetected = sum(1 for v in range(2048) if decode_frame(encode_frame(v) ^ 0x0110) is not None)
    assert undetected == 2048

    # 5. classification
    assert classify(0) == "motor_stop" and classify(47) == "command" and classify(48) == "throttle"

    # 6. timing table (brushlesswhoop): DShot600 bit 1.667us, frame ~26.7us; DShot150 frame ~106.7us
    t600 = bit_timing_us(600)
    assert abs(t600["bit"] - 1.6667) < 1e-3 and abs(t600["frame"] - 26.667) < 1e-2
    assert abs(t600["t1h"] - 1.25) < 1e-3 and abs(t600["t0h"] - 0.625) < 1e-3
    assert abs(bit_timing_us(150)["frame"] - 106.67) < 1e-1
    assert abs(bit_timing_us(300)["frame"] - 53.33) < 1e-1

    # 7. waveform round trip (normal + inverted), DShot300/600
    for rate in (300, 600):
        for inv in (False, True):
            for _ in range(50):
                f = encode_frame(rng.randint(0, 2047), rng.random() < 0.5, inv)
                wf = frame_to_waveform(f, rate, inverted=inv, step_ns=25)
                assert waveform_to_frame(wf, rate, inverted=inv, step_ns=25) == f

    # 8. GCR telemetry round trip (two independent decoders) for all 12-bit values
    for v12 in range(4096):
        w = telem_word(v12)
        assert telem_word_ok(w)
        line = gcr_encode_line(w)
        assert line < (1 << 21) and (line >> 20) == 0
        assert gcr_decode_line(line) == w
        assert gcr_decode_runlengths(line) == w
    # GCR property: never more than 2 zeros in a row in the 20-bit gcr -> line toggles often
    for g in GCR_ENC:
        assert "000" not in format(g, "05b")

    # 9. corrupted telemetry: single bit flip in the line is caught by GCR table or CRC most of the time
    caught = 0
    total = 0
    for v12 in rng.sample(range(4096), 300):
        line = gcr_encode_line(telem_word(v12))
        for b in range(20):
            total += 1
            w = gcr_decode_line(line ^ (1 << b))
            if w is None or not telem_word_ok(w):
                caught += 1
    assert caught / total > 0.9, caught / total

    # 10. eRPM math: 14-pole motor (7 pole pairs) at 20000 mech RPM -> 140000 eRPM
    erpm = 140000
    period_us = round(60_000_000 / erpm)            # 429 us
    v12 = erpm_period_encode(period_us)
    got = erpm_period_decode(v12) * 100
    assert abs(got - erpm) / erpm < 0.01, (got, erpm)
    assert erpm_period_decode(0x0FFF) == 0
    assert erpm_period_encode(10_000_000) == 0xFFF          # far too slow -> reported as stopped

    # 11. EDT: temperature 45 C frame type 0x2 -> v12 = 0x22D
    kind, val = edt_decode(0x200 | 45)
    assert kind == "temp_C" and val == 45
    assert edt_decode(v12)[0] == "erpm"

    print(f"[info] DShot600 bit={t600['bit']:.3f}us T1H={t600['t1h']:.3f} T0H={t600['t0h']:.3f} "
          f"frame={t600['frame']:.2f}us; telemetry line rate = {600*5/4:.0f} kbit/s, "
          f"21 bits = {21/(600*5/4)*1000:.1f}us")


if __name__ == "__main__":
    self_test()
    print("dshot.py self-test: PASS")
