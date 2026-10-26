# T2 byte stream -> packets -- "0xAA, len, payload, checksum = low 8 bits of sum(len+payload)"
SYNC = 0xAA

class Parser:
    def __init__(self, max_payload=64):
        self.max_payload = max_payload
        self.buf = bytearray()
        self.dropped = self.bad_chk = 0

    def feed(self, chunk):
        self.buf += chunk
        out = []
        while True:
            i = self.buf.find(SYNC)
            if i < 0:                               # only garbage: keep nothing
                self.dropped += len(self.buf)
                self.buf.clear()
                break
            self.dropped += i
            del self.buf[:i]
            if len(self.buf) < 2:                   # need the length byte
                break
            n = self.buf[1]
            if n > self.max_payload:                # fake sync -> drop ONE byte, retry
                del self.buf[:1]
                self.dropped += 1
                continue
            if len(self.buf) < 3 + n:               # partial: wait for more
                break
            if sum(self.buf[1:2 + n]) & 0xFF != self.buf[2 + n]:
                del self.buf[:1]                    # resync from the NEXT byte
                self.bad_chk += 1
                self.dropped += 1
                continue
            out.append(bytes(self.buf[2:2 + n]))
            del self.buf[:3 + n]
        return out


def frame(payload):
    return bytes([SYNC, len(payload)]) + payload + bytes([(len(payload) + sum(payload)) & 0xFF])


if __name__ == "__main__":
    p = Parser()
    assert p.feed(frame(b"\x10\x20")) == [b"\x10\x20"]          # happy
    data = b"\x00\x01" + frame(b"A") + frame(b"") + frame(b"BC")
    assert Parser().feed(data) == [b"A", b"", b"BC"]            # garbage, len 0, many per chunk
    q, got = Parser(), []
    for b in data:                                              # one byte at a time
        got += q.feed(bytes([b]))
    assert got == [b"A", b"", b"BC"]
    bad = bytearray(frame(b"XY")); bad[-1] ^= 0xFF
    assert Parser().feed(bytes(bad) + frame(b"Z")) == [b"Z"]    # bad checksum, then recover
    r = Parser()
    assert r.feed(b"\x55" * 1000) == [] and len(r.buf) == 0     # garbage never grows the buffer
    print("t2 OK")
