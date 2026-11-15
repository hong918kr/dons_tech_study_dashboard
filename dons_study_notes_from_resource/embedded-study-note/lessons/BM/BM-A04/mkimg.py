#!/usr/bin/env python3
"""BM-A04 이미지 패키저 + 독립 검증기 (stdlib 만: argparse, hashlib, struct, sys, zlib).

C 코드(solution.c)와 코드를 공유하지 않고 같은 헤더 규격을 다시 구현한다.
  python3 -I mkimg.py pack --ver 1.2.3 [--load 0x08020000] [--no-sha] payload.bin out.img
  python3 -I mkimg.py info   file.img
  python3 -I mkimg.py verify file.img [--min 1.2.0] [--slot-len N]
  python3 -I mkimg.py batch  records.bin        # C 가 만든 변이 이미지 N 개 → python 판정과 C 판정 비교
  python3 -I mkimg.py vectors                   # C 하네스에 박는 SHA-256 / CRC-32 교차검증 상수
  python3 -I mkimg.py fixture                   # C 하네스에 박는 "python 이 만든 이미지" (C 배열)

헤더 (little-endian, 64 B, 슬롯 맨 앞, payload 가 바로 뒤):
  0 magic u32 = 0x474D4942 ("BIMG")   4 hdr_version u16 = 1   6 hdr_size u16 = 64
  8 img_version u32 = major<<24|minor<<16|patch   12 payload_size u32   16 load_addr u32
  20 flags u32 (bit0 = SHA-256 present)   24 payload_crc32 u32 (zlib CRC-32)
  28 sha256[32]   60 hdr_crc32 u32 = CRC-32(bytes[0:60])
"""
import argparse
import hashlib
import struct
import sys
import zlib

HDR = struct.Struct("<IHHIIIII32sI")
assert HDR.size == 64
MAGIC = 0x474D4942
HDR_VERSION = 1
HDR_SIZE = 64
FLAG_SHA256 = 1
FLAGS_KNOWN = FLAG_SHA256
EXEC_ADDR = 0x08020000      # 이 보드의 실행 영역 (solution.c 와 같은 정책 값)
EXEC_MAX = 0x00060000       # 384 KB
MIN_PAYLOAD = 8             # 최소: 초기 SP + 리셋 벡터

# C 의 img_rc_t 와 같은 순서·이름
NAMES = ["OK", "ERR_ARG", "EMPTY", "BAD_MAGIC", "BAD_HDR_VERSION", "BAD_HDR_CRC", "BAD_FLAGS",
         "BAD_SIZE", "TOO_BIG", "BAD_LOAD_ADDR", "ROLLBACK", "BAD_CRC", "BAD_SHA"]
M32 = 0xFFFFFFFF


def ver_pack(text):
    parts = text.split(".")
    if len(parts) != 3:
        raise ValueError("version must be MAJOR.MINOR.PATCH")
    ma, mi, pa = (int(p, 10) for p in parts)
    if not (0 <= ma <= 255 and 0 <= mi <= 255 and 0 <= pa <= 0xFFFF):
        raise ValueError("version field out of range (255.255.65535)")
    return (ma << 24) | (mi << 16) | pa


def ver_str(v):
    return "v%d.%d.%d" % (v >> 24, (v >> 16) & 0xFF, v & 0xFFFF)


def pack(payload, version, load_addr=EXEC_ADDR, sha=True):
    flags = FLAG_SHA256 if sha else 0
    digest = hashlib.sha256(payload).digest() if sha else bytes(32)
    first60 = HDR.pack(MAGIC, HDR_VERSION, HDR_SIZE, version, len(payload), load_addr, flags,
                       zlib.crc32(payload) & M32, digest, 0)[:60]
    return first60 + struct.pack("<I", zlib.crc32(first60) & M32) + payload


def verify(slot, min_version=0):
    """(이름, 헤더 dict 또는 None). 검사 순서는 C img_verify 와 같은 '규격' — 구현은 별개."""
    if len(slot) < HDR_SIZE:
        return "BAD_SIZE", None
    if slot[:HDR_SIZE] == b"\xff" * HDR_SIZE:
        return "EMPTY", None
    (magic, hver, hsize, ver, size, load, flags, pcrc, digest, hcrc) = HDR.unpack_from(slot, 0)
    if magic != MAGIC:
        return "BAD_MAGIC", None
    if hver != HDR_VERSION or hsize != HDR_SIZE:
        return "BAD_HDR_VERSION", None
    if zlib.crc32(slot[:60]) & M32 != hcrc:
        return "BAD_HDR_CRC", None
    info = {"version": ver, "size": size, "load": load, "crc": pcrc, "sha": digest.hex()}
    if flags & ~FLAGS_KNOWN or not flags & FLAG_SHA256:
        return "BAD_FLAGS", info
    # 파이썬 정수는 넘치지 않는다 — C 는 slot_len - 64 쪽으로 빼서 같은 결과를 낸다
    if size < MIN_PAYLOAD or HDR_SIZE + size > len(slot):
        return "BAD_SIZE", info
    if size > EXEC_MAX:
        return "TOO_BIG", info
    if load != EXEC_ADDR:
        return "BAD_LOAD_ADDR", info
    if ver < min_version:
        return "ROLLBACK", info
    payload = slot[HDR_SIZE:HDR_SIZE + size]
    if zlib.crc32(payload) & M32 != pcrc:
        return "BAD_CRC", info
    if hashlib.sha256(payload).digest() != digest:
        return "BAD_SHA", info
    return "OK", info


class XS32:
    """C 하네스와 같은 xorshift32 (13, 17, 5)."""

    def __init__(self, seed):
        self.x = seed & M32

    def next(self):
        x = self.x
        x ^= (x << 13) & M32
        x ^= x >> 17
        x ^= (x << 5) & M32
        self.x = x
        return x

    def bytes(self, n):
        return bytes(self.next() & 0xFF for _ in range(n))


def make_payload(n, seed):
    """C make_payload 와 같은 '가짜 앱': word0 = 초기 SP, word1 = 리셋 벡터(홀수, Thumb), 나머지 난수."""
    rng = XS32(seed)
    body = struct.pack("<II", 0x20020000, EXEC_ADDR + 0x1C1)
    return body + bytes(rng.next() & 0xFF for _ in range(n - 8))


def cmd_pack(a):
    payload = open(a.payload, "rb").read()
    img = pack(payload, ver_pack(a.ver), int(a.load, 0), not a.no_sha)
    open(a.out, "wb").write(img)
    print("packed %s: %d B (hdr 64 + payload %d), %s, crc32=0x%08X, sha256=%s" % (
        a.out, len(img), len(payload), ver_str(ver_pack(a.ver)), zlib.crc32(payload) & M32,
        hashlib.sha256(payload).hexdigest() if not a.no_sha else "-"))


def cmd_info(a):
    data = open(a.img, "rb").read()
    f = HDR.unpack_from(data, 0)
    print("magic=0x%08X hdr_version=%d hdr_size=%d img_version=%s payload_size=%d load_addr=0x%08X "
          "flags=0x%X payload_crc32=0x%08X hdr_crc32=0x%08X" % (f[0], f[1], f[2], ver_str(f[3]), f[4], f[5],
                                                               f[6], f[7], f[9]))
    print("sha256=%s" % f[8].hex())


def cmd_verify(a):
    data = open(a.img, "rb").read()
    if a.slot_len is not None:          # 슬롯이 파일보다 크면 나머지는 지워진(0xFF) 플래시
        data = data[:a.slot_len] + b"\xff" * max(0, a.slot_len - len(data))
    name, info = verify(data, ver_pack(a.min) if a.min else 0)
    extra = ""
    if info:
        extra = " (%s size=%d crc=0x%08X sha=%s..)" % (ver_str(info["version"]), info["size"], info["crc"],
                                                       info["sha"][:16])
    print("python verify %s: %s%s" % (a.img, name, extra))
    return 0 if name == "OK" else 1


def cmd_batch(a):
    data = open(a.records, "rb").read()
    if data[:4] != b"A4MR":
        print("bad records file")
        return 2
    (count,) = struct.unpack_from("<I", data, 4)
    off, agree, hist = 8, 0, {}
    for i in range(count):
        min_v, slot_len, c_code = struct.unpack_from("<III", data, off)
        off += 12
        slot = data[off:off + slot_len]
        off += slot_len
        name, _ = verify(slot, min_v)
        hist[name] = hist.get(name, 0) + 1
        if NAMES[c_code] == name:
            agree += 1
        elif agree + 5 > i:                       # 처음 몇 개만 자세히
            print("DISAGREE #%d: C=%s python=%s" % (i, NAMES[c_code], name))
    print("python batch: %d/%d agree with C" % (agree, count))
    print("  " + " ".join("%s=%d" % (k, hist[k]) for k in NAMES if k in hist))
    return 0 if agree == count else 1


def vectors():
    rng = XS32(0xA04C0DE5)
    digests, crcs = b"", b""
    for n in range(301):                          # 길이 0..300: 패딩 경계 55/56/63/64/119/120 를 전부 지난다
        buf = rng.bytes(n)
        digests += hashlib.sha256(buf).digest()
        crcs += struct.pack("<I", zlib.crc32(buf) & M32)
    big = XS32(0x5EED0A04)
    bigd = b""
    for n in (1000, 4095, 4096, 4097, 65536, 100003):
        bigd += hashlib.sha256(big.bytes(n)).digest()
    return (hashlib.sha256(digests).hexdigest(), zlib.crc32(crcs) & M32, hashlib.sha256(bigd).hexdigest())


def cmd_vectors(_a):
    s, c, b = vectors()
    print("small (len 0..300) sha256 chain = %s" % s)
    print("small (len 0..300) crc32 chain  = 0x%08X" % c)
    print("big (6 sizes)      sha256 chain = %s" % b)
    for msg in (b"abc", b"", b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", b"a" * 1000000):
        print("sha256(%s) = %s" % (repr(msg[:12]) + ("..x%d" % len(msg) if len(msg) > 12 else ""),
                                   hashlib.sha256(msg).hexdigest()))
    print("crc32(b'123456789') = 0x%08X" % (zlib.crc32(b"123456789") & M32))


def cmd_fixture(_a):
    img = pack(make_payload(200, 0xF1C7A04), ver_pack("1.2.3"))
    print("/* python3 -I mkimg.py fixture 출력 그대로: make_payload(200, 0xF1C7A04), v1.2.3, %d B */" % len(img))
    print("static const uint8_t PY_FIXTURE[%d] = {" % len(img))
    for i in range(0, len(img), 16):
        print("    " + " ".join("0x%02X," % b for b in img[i:i + 16]))
    print("};")


def main():
    p = argparse.ArgumentParser(description="BM-A04 image tool")
    sub = p.add_subparsers(dest="cmd")
    s = sub.add_parser("pack")
    s.add_argument("--ver", required=True)
    s.add_argument("--load", default=hex(EXEC_ADDR))
    s.add_argument("--no-sha", action="store_true")
    s.add_argument("payload")
    s.add_argument("out")
    s = sub.add_parser("info")
    s.add_argument("img")
    s = sub.add_parser("verify")
    s.add_argument("img")
    s.add_argument("--min")
    s.add_argument("--slot-len", type=lambda t: int(t, 0))
    s = sub.add_parser("batch")
    s.add_argument("records")
    sub.add_parser("vectors")
    sub.add_parser("fixture")
    a = p.parse_args()
    fn = {"pack": cmd_pack, "info": cmd_info, "verify": cmd_verify, "batch": cmd_batch,
          "vectors": cmd_vectors, "fixture": cmd_fixture}.get(a.cmd)
    if fn is None:
        p.print_help()
        return 2
    return fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())
