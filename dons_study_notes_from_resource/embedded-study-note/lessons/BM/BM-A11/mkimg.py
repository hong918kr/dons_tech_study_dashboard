#!/usr/bin/env python3
"""BM-A11 mkimg.py — 링크된 ELF 안의 64 B 헤더 자리(.img_hdr)를 진짜 헤더로 채운다 (stdlib 만: struct/zlib).

헤더 형식 = BM-A04 헤더 축약판 (little-endian, 64 B; flags bit0 = 0 → SHA-256 칸은 0).
payload = 플래시 이미지 [load_addr, hdr_addr) — 벡터 테이블 + .text/.rodata + .data 초기값(LMA).
ELF 의 PT_LOAD 세그먼트를 물리 주소(p_paddr, = QEMU -kernel 이 올리는 주소)로 펼쳐 평평한 이미지를 만든다.

사용:
  python3 -I mkimg.py ELF HDR_ADDR --version 1.1.0 [--bin OUT.bin]
          [--flip-payload-bit N] [--flip-hdr-bit N] [--payload-size N]
  (--flip-* 는 헤더를 깨끗한 이미지로 계산한 "뒤에" 비트를 뒤집는다 = 서명 뒤 비트 썩음 / 전송 오류 흉내)
"""
import struct
import sys
import zlib

MAGIC = 0x474D4942
HDR_FMT = "<IHHIIIII32s"          # hdr_crc32 앞의 60 B
assert struct.calcsize(HDR_FMT) == 60


def load_segments(elf):
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    e_phoff, = struct.unpack_from("<I", elf, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", elf, 42)
    segs = []
    for i in range(e_phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", elf, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz > 0:                     # PT_LOAD with file contents
            segs.append((p_paddr, p_offset, p_filesz))
    return segs


def flat_image(elf, segs, start, end):
    img = bytearray(end - start)                              # 빈틈은 0 (objcopy -O binary 와 같게)
    for paddr, off, size in segs:
        lo, hi = max(paddr, start), min(paddr + size, end)
        if lo < hi:
            img[lo - start:hi - start] = elf[off + lo - paddr:off + hi - paddr]
    return img


def file_offset(segs, addr):
    for paddr, off, size in segs:
        if paddr <= addr < paddr + size:
            return off + addr - paddr
    raise SystemExit("address 0x%x is not in any loadable segment" % addr)


def main(argv):
    elf_path, hdr_addr = argv[0], int(argv[1], 0)
    opts = dict(zip(argv[2::2], argv[3::2]))
    major, minor, patch = (int(x) for x in opts.get("--version", "1.0.0").split("."))
    load_addr = 0
    with open(elf_path, "rb") as f:
        elf = bytearray(f.read())
    segs = load_segments(elf)
    payload = flat_image(elf, segs, load_addr, hdr_addr)
    hoff = file_offset(segs, hdr_addr)
    if any(elf[hoff:hoff + 64]):
        print("note: header slot was not blank (re-stamping)")
    psize = int(opts.get("--payload-size", str(len(payload))), 0)
    body = struct.pack(HDR_FMT, MAGIC, 1, 64, (major << 24) | (minor << 16) | patch, psize, load_addr, 0,
                       zlib.crc32(bytes(payload)) & 0xFFFFFFFF, bytes(32))
    hdr = bytearray(body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF))
    elf[hoff:hoff + 64] = hdr
    if "--flip-payload-bit" in opts:
        bit = int(opts["--flip-payload-bit"], 0)
        elf[file_offset(segs, load_addr + bit // 8)] ^= 1 << (bit % 8)
    if "--flip-hdr-bit" in opts:
        bit = int(opts["--flip-hdr-bit"], 0)
        elf[hoff + bit // 8] ^= 1 << (bit % 8)
    with open(elf_path, "wb") as f:
        f.write(elf)
    print("mkimg: payload [0x%08x, 0x%08x) %d B  crc32=0x%08x  hdr_crc32=0x%08x  ver=%d.%d.%d  hdr@file+0x%x"
          % (load_addr, hdr_addr, len(payload), zlib.crc32(bytes(payload)) & 0xFFFFFFFF,
             struct.unpack_from("<I", hdr, 60)[0], major, minor, patch, hoff))
    if "--bin" in opts:
        with open(opts["--bin"], "wb") as f:
            f.write(flat_image(elf, segs, load_addr, hdr_addr + 64))


if __name__ == "__main__":
    main(sys.argv[1:])
