"""COMM-A11: solution.c 가 찍은 텔레메트리 프레임을 독립적으로 다시 만든다 (stdlib 만).
실행: python3 -I verify_frame.py"""
import struct


def crc16_ccitt_false(data: bytes) -> int:
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def cobs(data: bytes) -> bytes:
    """블록 단위로 0 을 찾아 나누는 방식 (solution.c 의 한 번 훑기와 다른 구현)"""
    out, i = bytearray(), 0
    while True:
        j = i
        while j < len(data) and data[j] != 0 and j - i < 254:
            j += 1
        out.append(j - i + 1)
        out += data[i:j]
        if j >= len(data):
            break
        i = j + 1 if data[j] == 0 else j
        if data[j] != 0 and i >= len(data):
            out.append(1)
            break
    return bytes(out)


assert crc16_ccitt_false(b"123456789") == 0x29B1
# DATA = [01][seq][id LE32][sensor][flags][t LE32][value LE16]
plain = struct.pack("<BBIBBIh", 1, 7, 0x01020304, 1, 0, 1234, -2)
c = crc16_ccitt_false(plain)
frame = cobs(plain + bytes([c >> 8, c & 0xFF])) + b"\x00"
print("plain :", plain.hex(" ").upper())
print("crc16 : %04X" % c)
print("frame :", frame.hex(" ").upper(), "(%d B)" % len(frame))
