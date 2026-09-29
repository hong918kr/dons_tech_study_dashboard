"""03 · MSP v1 코덱 — starter (TODO를 채운다)

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
    # TODO: size ^ cmd ^ 모든 payload 바이트
    raise NotImplementedError


def _frame(direction, cmd, payload=b""):
    # TODO: payload 255 초과면 ValueError
    # TODO: b"$M" + direction + [size, cmd] + payload + [checksum]
    raise NotImplementedError


def encode_request(cmd, payload=b""):
    return _frame(b"<", cmd, payload)


def encode_response(cmd, payload=b"", error=False):     # FakeFC / 테스트용
    return _frame(b"!" if error else b">", cmd, payload)


def parse_responses(buf):
    # TODO: while 루프
    #   - buf.find(b"$M") 로 헤더를 찾고 그 앞은 버린다 (끝에 '$' 하나 남은 건 보존)
    #   - 5바이트('$M' dir size cmd)가 안 되거나 프레임 끝까지 안 왔으면 return (buf 에 남겨 둠)
    #   - direction 이 '>' / '!' 가 아니면 1바이트 버리고 continue
    #   - checksum 틀리면 1바이트 버리고 continue
    #   - 맞으면 buf 에서 프레임 제거, '!' 면 MspError(cmd), 아니면 (cmd, payload) 추가
    raise NotImplementedError


def decode_attitude(payload):
    # TODO: struct.unpack("<hhh", ...) → roll/10, pitch/10, float(yaw)
    raise NotImplementedError


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
