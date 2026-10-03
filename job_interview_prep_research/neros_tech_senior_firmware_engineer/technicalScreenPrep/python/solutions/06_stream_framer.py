"""06 · 바이트 스트림 → 프레임 (ring buffer 위에서 resync) — 모범답안

UART 는 프레임 경계를 모른다. read() 가 돌려주는 조각(chunk)은 프레임 중간에서 잘릴 수 있고,
노이즈 바이트나 CRC 가 틀린 프레임이 섞인다. 프레임:

    0xAA | LEN (1B, payload 길이 0..64) | PAYLOAD (LEN B) | CRC8 (poly 0x07, LEN+PAYLOAD 에 대해)

Framer.feed(chunk) -> [payload(bytes), ...]   완성된 프레임만 돌려주고, 나머지는 내부 버퍼에 남긴다.
통계: frames, crc_errors, resync_bytes (sync 를 찾느라 버린 바이트 수).
CRC 가 틀리면 0xAA 한 바이트만 버리고 그 다음부터 다시 sync 를 찾는다 (그 안에 진짜 프레임이 있을 수 있다).
"""
import pytest

SYNC, MAX_LEN = 0xAA, 64


def crc8(data, poly=0x07, init=0x00):
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def encode(payload):
    body = bytes([len(payload)]) + bytes(payload)
    return bytes([SYNC]) + body + bytes([crc8(body)])


class Framer:
    def __init__(self, max_buffer=1024):
        self._buf = bytearray()
        self.max_buffer = max_buffer
        self.frames = self.crc_errors = self.resync_bytes = 0

    def feed(self, chunk):
        self._buf += chunk
        out = []
        while True:
            i = self._buf.find(SYNC)
            if i < 0:
                self.resync_bytes += len(self._buf)
                self._buf.clear()
                break
            if i:
                self.resync_bytes += i
                del self._buf[:i]
            if len(self._buf) < 2:
                break                                   # LEN 아직 안 옴
            n = self._buf[1]
            if n > MAX_LEN:                             # 말이 안 되는 길이 → 가짜 sync
                self.resync_bytes += 1
                del self._buf[0]
                continue
            if len(self._buf) < n + 3:
                break                                   # 프레임 나머지 기다림
            body, crc = bytes(self._buf[1:n + 2]), self._buf[n + 2]
            if crc8(body) != crc:
                self.crc_errors += 1
                del self._buf[0]                        # sync 한 바이트만 버리고 다시 찾는다
                continue
            out.append(body[1:])
            self.frames += 1
            del self._buf[:n + 3]
        if len(self._buf) > self.max_buffer:            # 메모리 상한 (C 라면 고정 크기 링)
            drop = len(self._buf) - self.max_buffer
            self.resync_bytes += drop
            del self._buf[:drop]
        return out


# ------------------------------------------------------------------ tests
PAYLOADS = [b"", b"\x01", b"hello", bytes(range(64)), b"\xaa\xaa\xaa"]   # payload 안의 0xAA 도 포함
STREAM = b"".join(encode(p) for p in PAYLOADS)


def chunks(data, size):
    return [data[i:i + size] for i in range(0, len(data), size)]


def test_crc8_known_vector():
    assert crc8(b"123456789") == 0xF4                   # CRC-8/SMBUS 표준 check 값


@pytest.mark.parametrize("size", [1, 2, 3, 7, 64, len(STREAM)])
def test_any_chunking_gives_same_frames(size):
    f, got = Framer(), []
    for c in chunks(STREAM, size):
        got += f.feed(c)
    assert got == PAYLOADS
    assert f.crc_errors == 0 and f.resync_bytes == 0


def test_leading_garbage_is_skipped():
    f = Framer()
    assert f.feed(b"\x00\x13\x37" + encode(b"ok")) == [b"ok"]
    assert f.resync_bytes == 3


def test_bad_crc_dropped_and_next_frame_recovered():
    bad = bytearray(encode(b"bad"))
    bad[-1] ^= 0xFF
    f = Framer()
    assert f.feed(bytes(bad) + encode(b"good")) == [b"good"]
    assert f.crc_errors == 1


def test_frame_hidden_inside_corrupt_frame_is_found():
    inner = encode(b"in")
    fake = bytes([SYNC, len(inner)]) + inner + b"\x00"   # 가짜 헤더 + 진짜 프레임 + 틀린 CRC
    f = Framer()
    assert f.feed(fake) == [b"in"]


def test_impossible_length_is_resync_not_wait():
    f = Framer()
    assert f.feed(bytes([SYNC, 200]) + encode(b"x")) == [b"x"]


def test_partial_frame_waits():
    data = encode(b"split")
    f = Framer()
    assert f.feed(data[:4]) == []
    assert f.feed(data[4:]) == [b"split"]


def test_buffer_is_bounded():
    f = Framer(max_buffer=16)
    f.feed(bytes([SYNC, 60]) + b"\x00" * 40)            # 끝나지 않는 프레임
    assert len(f._buf) <= 16
