"""C01 byte stream -> packets: sync / length / payload / checksum, partial frames, resync, bounded buffer.

Interviewer wording (no hint): "Bytes come in from a serial port in random-sized chunks.
Each message is: 0xAA, a length byte, that many payload bytes, then a checksum
(sum of length + payload, low 8 bits). Write something I can call with each chunk
that gives me back the complete, valid messages."

Run: python3 c01_packet_stream.py
"""

SYNC = 0xAA


def checksum(data):
    return sum(data) & 0xFF


class FrameParser:
    def __init__(self, max_payload=64):
        if not 0 <= max_payload <= 255:
            raise ValueError("max_payload must fit in the length byte")
        self.max_payload = max_payload
        self.max_frame = 3 + max_payload       # SYNC + LEN + payload + CHK
        self.buf = bytearray()
        self.dropped = 0                       # bytes thrown away while hunting for sync
        self.bad_len = 0
        self.bad_chk = 0

    def feed(self, chunk):
        """Append a chunk, return the list of payloads (bytes) completed by it."""
        # >>> solution
        self.buf += chunk
        out = []
        while True:
            i = self.buf.find(SYNC)
            if i < 0:                          # garbage only: keep nothing -> buffer can't grow
                self.dropped += len(self.buf)
                self.buf.clear()
                break
            if i > 0:
                self.dropped += i
                del self.buf[:i]
            if len(self.buf) < 2:              # need the length byte
                break
            n = self.buf[1]
            if n > self.max_payload:           # impossible length -> this 0xAA was not a real sync
                self.bad_len += 1
                self.dropped += 1
                del self.buf[:1]
                continue
            total = 3 + n
            if len(self.buf) < total:          # partial frame: wait for more bytes
                break
            if checksum(self.buf[1:2 + n]) != self.buf[2 + n]:
                self.bad_chk += 1              # resync from the byte AFTER this sync,
                self.dropped += 1              # not after the whole fake frame
                del self.buf[:1]
                continue
            out.append(bytes(self.buf[2:2 + n]))
            del self.buf[:total]
        return out
        # <<< solution


# ----------------------------------------------------------------- tests
def frame(payload):
    body = bytes([len(payload)]) + bytes(payload)
    return bytes([SYNC]) + body + bytes([checksum(body)])


def _tests():
    p = FrameParser()
    assert p.feed(frame(b"\x01\x02\x03")) == [b"\x01\x02\x03"]            # happy path

    p = FrameParser()                                                    # one byte at a time
    got = []
    for b in frame(b"hello"):
        got += p.feed(bytes([b]))
    assert got == [b"hello"]

    p = FrameParser()                                                    # two frames in one chunk
    assert p.feed(frame(b"a") + frame(b"bc")) == [b"a", b"bc"]

    p = FrameParser()                                                    # garbage in front
    assert p.feed(b"\x00\x11\x22" + frame(b"x")) == [b"x"]
    assert p.dropped == 3

    p = FrameParser()                                                    # empty payload is legal
    assert p.feed(frame(b"")) == [b""]

    p = FrameParser()                                                    # 0xAA inside the payload
    assert p.feed(frame(b"\xAA\xAA")) == [b"\xAA\xAA"]

    p = FrameParser()                                                    # corrupt frame, then a good one
    bad = bytearray(frame(b"abc"))
    bad[-1] ^= 0xFF
    assert p.feed(bytes(bad) + frame(b"ok")) == [b"ok"]
    assert p.bad_chk == 1

    p = FrameParser(max_payload=8)                                       # fake sync with impossible length
    assert p.feed(b"\xAA\xF0" + frame(b"z")) == [b"z"]
    assert p.bad_len == 1

    p = FrameParser(max_payload=8)                                       # fake sync with a plausible length:
    assert p.feed(b"\xAA\x05" + frame(b"q")) == []                       # must wait (could be real) ...
    assert p.feed(b"\x00\x00") == [b"q"]                                 # ... then checksum fails -> resync finds it

    p = FrameParser(max_payload=8)                                       # garbage flood: buffer stays bounded
    import random
    rnd = random.Random(1)
    for _ in range(2000):
        p.feed(bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 40))))
        assert len(p.buf) < p.max_frame
    assert p.feed(b"\x00" * p.max_frame + frame(b"end"))[-1:] == [b"end"]

    p = FrameParser()                                                    # random split points never change the result
    stream = b"".join(frame(bytes([i] * (i % 7))) for i in range(50))
    want = [bytes([i] * (i % 7)) for i in range(50)]
    got, pos = [], 0
    while pos < len(stream):
        step = rnd.randrange(1, 9)
        got += p.feed(stream[pos:pos + step])
        pos += step
    assert got == want
    print("c01 OK")


if __name__ == "__main__":
    _tests()
