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
    raise NotImplementedError("TODO")


def decode(blob):
    """Return (records, problems). records: list of dicts. problems: list of (index, reason)."""
    raise NotImplementedError("TODO")


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
