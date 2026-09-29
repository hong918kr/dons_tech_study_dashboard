"""03 · MSP v1 코덱 — 모범답안

MSP (MultiWii Serial Protocol) v1 — Betaflight Configurator 가 FC 와 대화하는 프로토콜.

  request  : '$' 'M' '<' size cmd payload[size] checksum
  response : '$' 'M' '>' size cmd payload[size] checksum
  error    : '$' 'M' '!' size cmd payload[size] checksum     (FC 가 명령을 모름/거부)
  checksum = size ^ cmd ^ payload[0] ^ ... ^ payload[n-1]

encode_request(cmd, payload)        → bytes
parse_responses(buf: bytearray)     → [(cmd, payload)], 완성된 프레임만 buf 에서 소비하고 나머지는 남긴다
                                       '$M!' 는 MspError 로 돌려준다 (예외 객체를 리스트에 넣음)
decode_attitude(payload)            → (roll_deg, pitch_deg, yaw_deg)  MSP_ATTITUDE = 108
"""
import struct
from functools import reduce

MSP_ATTITUDE = 108
HEADER = b"$M"


class MspError(Exception):
    def __init__(self, cmd):
        super().__init__(f"FC rejected MSP cmd {cmd}")
        self.cmd = cmd


def checksum(size, cmd, payload):
    return reduce(lambda a, b: a ^ b, payload, size ^ cmd)


def _frame(direction, cmd, payload=b""):
    if len(payload) > 255:
        raise ValueError("MSP v1 payload max 255 bytes (use MSP v2 / jumbo)")
    size = len(payload)
    return HEADER + direction + bytes([size, cmd]) + payload + bytes([checksum(size, cmd, payload)])


def encode_request(cmd, payload=b""):
    return _frame(b"<", cmd, payload)


def encode_response(cmd, payload=b"", error=False):     # FakeFC / 테스트용
    return _frame(b"!" if error else b">", cmd, payload)


def parse_responses(buf):
    out = []
    while True:
        i = buf.find(HEADER)
        if i < 0:
            keep = 1 if buf.endswith(b"$") else 0   # 끝의 '$' 는 다음 chunk 의 'M' 을 기다린다
            del buf[:len(buf) - keep]
            return out
        del buf[:i]
        if len(buf) < 5:
            return out                           # '$M' dir size cmd 가 아직 안 옴
        direction, size, cmd = buf[2:3], buf[3], buf[4]
        if direction not in (b">", b"!"):
            del buf[:1]                          # request 에코나 쓰레기 → 다음 '$' 로
            continue
        end = 5 + size + 1
        if len(buf) < end:
            return out
        payload = bytes(buf[5:5 + size])
        ok = checksum(size, cmd, payload) == buf[end - 1]
        if not ok:
            del buf[:1]                          # 깨진 프레임 → 1바이트 버리고 다시 '$M' 탐색
            continue
        del buf[:end]
        out.append(MspError(cmd) if direction == b"!" else (cmd, payload))


def decode_attitude(payload):
    roll, pitch, yaw = struct.unpack("<hhh", payload[:6])   # int16 LE
    return roll / 10.0, pitch / 10.0, float(yaw)            # 0.1 deg, 0.1 deg, deg


# ------------------------------------------------------------------ tests
def test_encode_request_no_payload():
    assert encode_request(MSP_ATTITUDE) == b"$M<\x00\x6c\x6c"      # 0 ^ 108 = 108


def test_parse_attitude_response():
    payload = struct.pack("<hhh", -125, 30, 270)                   # -12.5°, 3.0°, 270°
    buf = bytearray(encode_response(MSP_ATTITUDE, payload))
    [(cmd, p)] = parse_responses(buf)
    assert cmd == MSP_ATTITUDE and decode_attitude(p) == (-12.5, 3.0, 270.0)
    assert buf == bytearray()


def test_split_across_chunks_and_garbage():
    frame = encode_response(MSP_ATTITUDE, struct.pack("<hhh", 0, 0, 90))
    buf = bytearray(b"\x00junk$" + frame[:4])
    assert parse_responses(buf) == []
    buf += frame[4:]
    [(cmd, p)] = parse_responses(buf)
    assert decode_attitude(p)[2] == 90.0


def test_error_frame():
    buf = bytearray(encode_response(250, error=True))
    [err] = parse_responses(buf)
    assert isinstance(err, MspError) and err.cmd == 250


def test_bad_checksum_skipped():
    bad = bytearray(encode_response(1, b"\x01\x02"))
    bad[-1] ^= 0x55
    buf = bad + encode_response(2, b"\x07")
    assert parse_responses(buf) == [(2, b"\x07")]


def test_payload_too_big():
    try:
        encode_request(1, bytes(256))
    except ValueError:
        return
    raise AssertionError("expected ValueError")


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
