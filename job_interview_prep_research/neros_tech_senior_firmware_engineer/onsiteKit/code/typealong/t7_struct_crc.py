# T7 binary record -- "seq u16, 3x int16 accel mg, vbat mV u16, flags u8, CRC-8 (poly 0x07, init 0) over 11 bytes"
import struct

REC = struct.Struct("<HhhhHB")                      # 11 bytes, little-endian
SIZE = REC.size + 1


def crc8(data, poly=0x07):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def decode(blob):
    recs, problems = [], []
    if len(blob) % SIZE:
        problems.append((len(blob) // SIZE, "trailing bytes"))
    for i in range(len(blob) // SIZE):
        raw = blob[i * SIZE:(i + 1) * SIZE]
        if crc8(raw[:-1]) != raw[-1]:
            problems.append((i, "crc"))
            continue
        seq, ax, ay, az, mv, flags = REC.unpack(raw[:-1])
        if not 9000 <= mv <= 26000:
            problems.append((i, "vbat range"))
        if flags & ~0x07:
            problems.append((i, "unknown flags"))
        recs.append({"seq": seq, "accel": (ax, ay, az), "mv": mv, "armed": bool(flags & 1)})
    return recs, problems


def make(seq, ax, ay, az, mv, flags):
    body = REC.pack(seq, ax, ay, az, mv, flags)
    return body + bytes([crc8(body)])


if __name__ == "__main__":
    assert crc8(b"123456789") == 0xF4                    # standard CRC-8 check value
    good = make(1, 0, 0, 1000, 16800, 1)
    recs, probs = decode(good)
    assert probs == [] and recs[0]["accel"] == (0, 0, 1000) and recs[0]["armed"]
    bad = bytearray(make(2, -5, 0, 0, 16800, 0)); bad[3] ^= 1
    assert decode(bytes(bad))[1] == [(0, "crc")]
    assert decode(make(3, 0, 0, 0, 5000, 0x80))[1] == [(0, "vbat range"), (0, "unknown flags")]
    assert decode(good + b"\x00")[1] == [(1, "trailing bytes")]
    print("t7 OK")
