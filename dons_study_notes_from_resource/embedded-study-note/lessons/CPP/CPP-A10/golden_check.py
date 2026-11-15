#!/usr/bin/env python3
"""CPP-A10 golden_check.py — C++ 와 독립적인 기준값(oracle) 생성기.

C++ 코드를 쓰지 않고 표준 도구로 같은 값을 만든다:
  - 패킹: struct.pack('<BBHI3h3hH')  (리틀 엔디언, 패딩 없음)
  - CRC : zlib.crc32                  (IEEE 802.3 / zlib — C++ 쪽 constexpr 표와 같은 정의여야 함)
  - COBS: 아래 cobs_encode            (위키백과 정의대로 따로 쓴 구현)
  - α   : alpha_q15(5, 104), Q16 배율 scale_q16(61)
실행: python3 golden_check.py   → solution.cpp 의 kGoldenWire / 상수와 비교할 값을 출력, 자체 검사 실패 시 exit 1
"""
import math
import struct
import sys
import zlib


def cobs_encode(data: bytes) -> bytes:
    out = bytearray()
    block = bytearray()
    for i, b in enumerate(data):
        if b == 0:
            out.append(len(block) + 1)
            out += block
            block = bytearray()
        else:
            block.append(b)
            if len(block) == 254:
                out.append(0xFF)
                out += block
                block = bytearray()
                if i + 1 == len(data):
                    return bytes(out)      # 254개 꽉 찬 블록으로 끝나면 꼬리 블록 없음
    out.append(len(block) + 1)
    out += block
    return bytes(out)


def main() -> int:
    ok = True
    # 위키백과 COBS 예제 몇 개 (자체 검사)
    vectors = [
        (b"\x00", b"\x01\x01"),
        (b"\x00\x00", b"\x01\x01\x01"),
        (b"\x11\x22\x00\x33", b"\x03\x11\x22\x02\x33"),
        (b"\x11\x22\x33\x44", b"\x05\x11\x22\x33\x44"),
        (b"\x11\x00\x00\x00", b"\x02\x11\x01\x01\x01"),
        (bytes(range(1, 255)), b"\xff" + bytes(range(1, 255))),
        (bytes(range(0, 255)), b"\x01\xff" + bytes(range(1, 255))),
        (bytes(range(1, 256)), b"\xff" + bytes(range(1, 255)) + b"\x02\xff"),
        (bytes(range(2, 256)) + b"\x00", b"\xff" + bytes(range(2, 256)) + b"\x01\x01"),
    ]
    for raw, enc in vectors:
        if cobs_encode(raw) != enc:
            print("COBS self-check FAIL", raw[:4].hex(), len(raw))
            ok = False
    print("crc32('123456789') = 0x%08X" % zlib.crc32(b"123456789"))

    body = struct.pack("<BBHI3h3hH", 1, 0, 0x1234, 0x89ABCDEF, 1000, -2, 16384, -32768, 32767, 0, 0x0102)
    assert len(body) == 22
    crc = zlib.crc32(body)
    frame = body + struct.pack("<I", crc)
    wire = cobs_encode(frame) + b"\x00"
    print("frame (%d B): %s" % (len(frame), frame.hex(" ")))
    print("crc32 = 0x%08X" % crc)
    print("wire  (%d B): %s" % (len(wire), wire.hex(" ")))
    print("C++  : {" + ", ".join("0x%02X" % b for b in wire) + "}")

    w = 2 * math.pi * 5.0 / 104.0
    print("alpha_q15(5,104) = %d  (alpha = %.6f)" % (int(w / (w + 1) * 32768 + 0.5), w / (w + 1)))
    print("scale_q16(61) = %d" % ((61 * 65536 + 500) // 1000))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
