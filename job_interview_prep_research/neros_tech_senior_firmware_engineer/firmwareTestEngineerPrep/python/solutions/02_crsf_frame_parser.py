"""02 · CRSF 프레임 파서 — 모범답안

CRSF (Crossfire / ExpressLRS ↔ flight controller UART 프로토콜) 프레임:

  [sync/addr][len][type][payload ...][crc8]
   len  = type(1) + payload(N) + crc(1)  = N + 2      (sync, len 자신은 제외)
   crc8 = CRC-8/DVB-S2 (poly 0xD5, init 0x00) over type + payload
   전체 프레임 최대 64 bytes → len 최대 62

CrsfParser.feed(chunk) 는 아무 크기로 잘려 들어오는 바이트를 받아 완성된 프레임 리스트를 돌려준다.
CRC·길이가 틀리면 sync 1바이트만 버리고 다시 찾는다 (resync).
unpack_rc_channels() 는 RC_CHANNELS_PACKED (type 0x16, 22 bytes = 16 ch x 11 bit, LSB-first) 디코드.
"""
ADDRS = {0xC8, 0xEA, 0xEC, 0xEE}   # FC, radio(handset), receiver, TX module
MAX_LEN = 62
TYPE_RC_CHANNELS = 0x16


def crc8_dvb_s2(data, crc=0):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def build_frame(ftype, payload, addr=0xC8):
    body = bytes([ftype]) + bytes(payload)
    return bytes([addr, len(body) + 1]) + body + bytes([crc8_dvb_s2(body)])


class CrsfParser:
    def __init__(self):
        self.buf = bytearray()
        self.crc_errors = 0
        self.dropped = 0            # resync 로 버린 바이트 수

    def feed(self, chunk):
        self.buf += chunk
        frames = []
        while len(self.buf) >= 2:
            addr, n = self.buf[0], self.buf[1]
            if addr not in ADDRS or not 2 <= n <= MAX_LEN:
                self._drop()
                continue
            if len(self.buf) < n + 2:
                break                               # 나머지가 아직 안 옴 → 다음 feed
            body = bytes(self.buf[2:n + 1])         # type + payload
            if crc8_dvb_s2(body) != self.buf[n + 1]:
                self.crc_errors += 1
                self._drop()                        # 프레임 통째가 아니라 1바이트만
                continue
            frames.append((addr, body[0], body[1:]))
            del self.buf[:n + 2]
        return frames

    def _drop(self):
        del self.buf[0]
        self.dropped += 1


def unpack_rc_channels(payload):
    """22 bytes → 16 x 11-bit ticks (172..1811, 중앙 992)."""
    if len(payload) != 22:
        raise ValueError("RC_CHANNELS_PACKED payload must be 22 bytes")
    v = int.from_bytes(payload, "little")
    return [(v >> (11 * i)) & 0x7FF for i in range(16)]


def pack_rc_channels(ch):
    v = 0
    for i, c in enumerate(ch):
        v |= (c & 0x7FF) << (11 * i)
    return v.to_bytes(22, "little")


def ticks_to_us(t):
    return round((t - 992) * 5 / 8 + 1500)


# ------------------------------------------------------------------ tests
def test_crc_check_value():
    assert crc8_dvb_s2(b"123456789") == 0xBC       # CRC-8/DVB-S2 표준 check 값


def test_roundtrip_one_frame():
    f = build_frame(0x14, b"\x01\x02\x03")
    assert f[1] == 5 and len(f) == 7
    assert CrsfParser().feed(f) == [(0xC8, 0x14, b"\x01\x02\x03")]


def test_byte_by_byte_and_garbage():
    stream = b"\x00\xff\x13" + build_frame(0x14, b"A") + build_frame(0x08, b"BC")
    p, out = CrsfParser(), []
    for b in stream:
        out += p.feed(bytes([b]))
    assert out == [(0xC8, 0x14, b"A"), (0xC8, 0x08, b"BC")]
    assert p.dropped == 3


def test_bad_crc_resyncs_to_next_frame():
    bad = bytearray(build_frame(0x14, b"xyz"))
    bad[-1] ^= 0xFF
    p = CrsfParser()
    assert p.feed(bytes(bad) + build_frame(0x14, b"ok")) == [(0xC8, 0x14, b"ok")]
    assert p.crc_errors == 1


def test_bad_length_skipped():
    p = CrsfParser()
    assert p.feed(b"\xc8\xff" + build_frame(0x14, b"ok")) == [(0xC8, 0x14, b"ok")]


def test_rc_channels():
    ch = [992] * 16
    ch[0], ch[1], ch[15] = 172, 1811, 1500
    payload = pack_rc_channels(ch)
    frames = CrsfParser().feed(build_frame(TYPE_RC_CHANNELS, payload))
    assert unpack_rc_channels(frames[0][2]) == ch
    assert ticks_to_us(992) == 1500 and ticks_to_us(172) == 988


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
