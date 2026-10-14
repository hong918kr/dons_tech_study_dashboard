"""C07 binary telemetry record: struct unpack (little-endian), bit flags, range checks, CRC-8 written by hand.

Interviewer wording (no hint): "The flight controller dumps 12-byte records:
seq u16, three int16 accel values in milli-g, battery millivolts u16, a flags byte,
and a CRC-8 (poly 0x07, init 0) over the first 11 bytes. Decode a dump and flag
anything suspicious."

Run: python3 c07_binary_telemetry.py
"""
import struct

REC = struct.Struct("<HhhhHB")            # 11 bytes; + 1 CRC byte = 12
REC_SIZE = REC.size + 1
FLAG_ARMED = 1 << 0
FLAG_FAILSAFE = 1 << 1
FLAG_GPS_FIX = 1 << 2


def crc8(data, poly=0x07, init=0x00):
    # >>> solution
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc
    # <<< solution


def decode(blob):
    """Return (records, problems). records: list of dicts. problems: list of (index, reason)."""
    # >>> solution
    records, problems = [], []
    if len(blob) % REC_SIZE:
        problems.append((len(blob) // REC_SIZE, "trailing %d bytes" % (len(blob) % REC_SIZE)))
    for i in range(len(blob) // REC_SIZE):
        raw = blob[i * REC_SIZE:(i + 1) * REC_SIZE]
        if crc8(raw[:-1]) != raw[-1]:
            problems.append((i, "crc"))
            continue
        seq, ax, ay, az, mv, flags = REC.unpack(raw[:-1])
        r = {"seq": seq, "accel_mg": (ax, ay, az), "vbat_v": mv / 1000.0,
             "armed": bool(flags & FLAG_ARMED), "failsafe": bool(flags & FLAG_FAILSAFE),
             "gps_fix": bool(flags & FLAG_GPS_FIX)}
        if not 9.0 <= r["vbat_v"] <= 26.0:                 # 3S..6S pack, anything else is a sensor problem
            problems.append((i, "vbat out of range"))
        if any(abs(a) > 16000 for a in (ax, ay, az)):       # near +/-16 g full scale -> clipping
            problems.append((i, "accel clipping"))
        if flags & ~0x07:
            problems.append((i, "unknown flag bits"))
        records.append(r)
    return records, problems
    # <<< solution


# ----------------------------------------------------------------- tests
def encode(seq, accel, mv, flags):
    body = REC.pack(seq, *accel, mv, flags)
    return body + bytes([crc8(body)])


def _tests():
    assert crc8(b"123456789") == 0xF4                     # CRC-8/SMBUS check value: proves the CRC first
    assert crc8(b"") == 0

    good = encode(1, (0, 0, 1000), 16800, FLAG_ARMED | FLAG_GPS_FIX)
    assert len(good) == 12
    recs, probs = decode(good)
    assert probs == [] and recs[0]["seq"] == 1 and recs[0]["vbat_v"] == 16.8
    assert recs[0]["armed"] and recs[0]["gps_fix"] and not recs[0]["failsafe"]
    assert recs[0]["accel_mg"] == (0, 0, 1000)

    neg = encode(2, (-1000, -32768, 5), 16000, 0)        # signed values survive
    assert decode(neg)[0][0]["accel_mg"] == (-1000, -32768, 5)

    bad = bytearray(good)
    bad[3] ^= 0x01                                        # one flipped bit -> CRC catches it
    recs, probs = decode(bytes(bad) + good)
    assert len(recs) == 1 and probs == [(0, "crc")]

    recs, probs = decode(encode(3, (0, 0, 0), 4200, 0x80) + good[:5])
    reasons = [p[1] for p in probs]
    assert "vbat out of range" in reasons and "unknown flag bits" in reasons
    assert any(r.startswith("trailing") for r in reasons)
    print("c07 OK")


if __name__ == "__main__":
    _tests()
