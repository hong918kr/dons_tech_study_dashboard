#!/usr/bin/env python3
"""BM-A05 독립 교차 검증용 YMODEM "송신기" (stdlib 만: struct / zlib / binascii).

C 코드와 따로 짠 구현으로
  1) BM-A04 헤더 축약판 이미지(64 B 헤더 + payload, CRC-32 = zlib.crc32)를 만들고
  2) 오류 없는 YMODEM 세션에서 송신기가 내보내는 바이트열 전체를 만들고
     (block 0 SOH 128, 데이터 STX 1024 / 마지막 SOH 128 들, 0x1A 패딩, CRC-16 = binascii.crc_hqx,
      EOT, EOT, null block 0)
  3) 그때 수신기가 보내야 할 응답 바이트열 ('C', ACK 'C', ACK..., NAK, ACK 'C', ACK) 을 계산해
ymodem_fixture.bin 에 저장한다. practice.c 연습 2 가 이 스트림을 C 수신기에 흘려 넣고 비교한다.

실행 (레슨 폴더에서):  python3 -I send_ymodem.py
"""
import binascii
import struct
import sys
import zlib
from pathlib import Path

SOH, STX, EOT, ACK, NAK = 0x01, 0x02, 0x04, 0x06, 0x15
SLOT_ADDR = 0x08008000
MAGIC = 0x474D4942


def crc16(data: bytes) -> int:
    return binascii.crc_hqx(data, 0)          # CRC-16/XMODEM: poly 0x1021, init 0


def make_image(payload: bytes, version: int) -> bytes:
    body = struct.pack("<IHHIIIII32s", MAGIC, 1, 64, version, len(payload), SLOT_ADDR + 64, 0,
                       zlib.crc32(payload) & 0xFFFFFFFF, bytes(32))
    assert len(body) == 60
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF) + payload


def packet(seq: int, data: bytes, size: int) -> bytes:
    data = data.ljust(size, b"\x1a" if seq else b"\x00")
    return bytes([STX if size == 1024 else SOH, seq & 0xFF, 0xFF - (seq & 0xFF)]) + data + struct.pack(">H", crc16(data))


def session(name: str, image: bytes):
    mtime = 1700000000
    stream = packet(0, f"{name}\0{len(image)} {mtime:o} {0o100644:o}".encode(), 128)
    resp = b"C" + bytes([ACK]) + b"C"
    off, seq = 0, 1
    while off < len(image):
        size = 1024 if len(image) - off >= 1024 else 128
        stream += packet(seq, image[off:off + size], size)
        resp += bytes([ACK])
        off += size
        seq += 1
    stream += bytes([EOT, EOT]) + packet(0, b"", 128)
    resp += bytes([NAK, ACK]) + b"C" + bytes([ACK])
    return stream, resp, seq - 1


def main() -> int:
    assert crc16(b"123456789") == 0x31C3, "CRC-16/XMODEM check value"
    payload = bytes((i * 7 + (i >> 8) * 13 + 3) & 0xFF for i in range(2497)) + b"\x1a\x1a\x1a"
    image = make_image(payload, 0x01000002)
    stream, resp, nblocks = session("fixture.bin", image)
    out = Path(__file__).resolve().parent / "ymodem_fixture.bin"
    blob = b"YMFX" + b"".join(struct.pack("<I", len(x)) + x for x in (image, stream, resp))
    out.write_bytes(blob)
    print(f"crc16('123456789') = 0x{crc16(b'123456789'):04X}")
    print(f"image {len(image)} B (payload {len(payload)} B, crc32 0x{zlib.crc32(payload) & 0xFFFFFFFF:08X}), "
          f"{nblocks} data blocks, stream {len(stream)} B, responses {resp.hex(' ')}")
    print(f"block0 crc16 = 0x{struct.unpack('>H', stream[131:133])[0]:04X}, wrote {out.name} ({len(blob)} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
