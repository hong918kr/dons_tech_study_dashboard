#!/usr/bin/env python3
"""COMM-M04 독립 검증기: C 코드와 다른 방법으로 CAN 2.0 프레임 비트열을 만든다.

- CRC-15: 레지스터 시프트가 아니라 "메시지 · x^15 를 생성 다항식으로 긴 나눗셈" (리스트 XOR)
- 스터핑: 정규식 없이 문자열을 앞에서부터 훑으며 연속 길이를 센다 (C 와 같은 규칙, 다른 코드)
- 출력: 송신기 관점 와이어 비트열 (ACK 슬롯 = 1 recessive), 0 = dominant

실행: python3 verify_frame.py   (표준 라이브러리만 사용)
"""

GEN = "1100010110011001"  # x^15+x^14+x^10+x^8+x^7+x^4+x^3+1  (0x4599 + 최고차항)


def crc15_longdiv(bits: str) -> str:
    """bits(문자열 '0'/'1') · x^15 mod G 를 긴 나눗셈으로. 15자리 문자열 반환."""
    work = [int(c) for c in bits] + [0] * 15
    g = [int(c) for c in GEN]
    for i in range(len(bits)):
        if work[i]:
            for j in range(16):
                work[i + j] ^= g[j]
    return "".join(str(b) for b in work[-15:])


def fields(ident: int, ext: bool, rtr: bool, dlc: int, data: bytes) -> str:
    n = 0 if rtr else min(dlc, 8)
    assert len(data) >= n
    s = "0"                                         # SOF (dominant)
    if not ext:
        s += format(ident, "011b") + ("1" if rtr else "0") + "0" + "0"   # ID RTR IDE r0
    else:
        s += format(ident >> 18, "011b") + "1" + "1"                     # ID-A SRR IDE
        s += format(ident & 0x3FFFF, "018b") + ("1" if rtr else "0") + "00"  # ID-B RTR r1 r0
    s += format(dlc, "04b")
    s += "".join(format(b, "08b") for b in data[:n])
    return s + crc15_longdiv(s)


def stuff(s: str) -> str:
    out, prev, cnt = [], None, 0
    for c in s:
        out.append(c)
        cnt = cnt + 1 if c == prev else 1
        prev = c
        if cnt == 5:
            prev = "1" if c == "0" else "0"
            out.append(prev)
            cnt = 1
    return "".join(out)


def frame(ident, ext, rtr, dlc, data=b""):
    return stuff(fields(ident, ext, rtr, dlc, data)) + "1" + "1" + "1" + "1111111"  # CRCdel ACK ACKdel EOF


def main():
    chk = "".join(format(b, "08b") for b in b"123456789")
    print("CRC-15/CAN check(\"123456789\") = 0x%04X" % int(crc15_longdiv(chk), 2))
    cases = [
        ("K1 std 0x123 data DLC2 11 22", 0x123, False, False, 2, bytes([0x11, 0x22])),
        ("K2 std 0x000 data DLC8 00x8", 0x000, False, False, 8, bytes(8)),
        ("K3 std 0x555 remote DLC8", 0x555, False, True, 8, b""),
        ("K4 ext 0x18DAF110 data DLC8", 0x18DAF110, True, False, 8,
         bytes([0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00])),
        ("K5 ext 0x1FFFFFFF data DLC0", 0x1FFFFFFF, True, False, 0, b""),
    ]
    for name, i, e, r, d, data in cases:
        raw = fields(i, e, r, d, data)
        wire = frame(i, e, r, d, data)
        nst = len(stuff(raw)) - len(raw)
        print("%s: unstuffed=%d stuff=%d wire=%d crc=0x%04X" % (name, len(raw), nst, len(wire), int(raw[-15:], 2)))
        print("  " + wire)


if __name__ == "__main__":
    main()
