#!/usr/bin/env python3
"""crsf.py — CRSF (Crossfire / ExpressLRS serial) frame encoder, decoder, streaming parser.

Standard library only. Python 3.9+. Run: python3 crsf.py  -> prints PASS at the end.

Frame on the wire (UART, serial link RX->FC):

    [sync/addr 1][len 1][type 1][payload N][crc 1]
    len  = N + 2   (type + payload + crc)  -> valid 2..62 (spec), total frame <= 64 bytes
    crc  = CRC-8 poly 0xD5 (DVB-S2), init 0, over type + payload (NOT sync, NOT len)

Verified against (2026-10-24):
  - ExpressLRS  src/include/crsf_protocol.h  (CRSF_CRC_POLY 0xd5, CRSF_MAX_PACKET_LEN 64, 172/992/1811)
  - EdgeTX      radio/src/tests/crossfire.cpp (TEST(Crossfire, crc8) golden vector, used below)
  - EdgeTX      radio/src/pulses/crossfire.cpp _processFrames(): "CRC errors or invalid length errors
                MUST just advance the buffer by one" -> same resync policy as StreamParser below
  - TBS spec    github.com/tbs-fpv/tbs-crsf-spec crsf.md (us = (ticks-992)*5/8 + 1500)
"""

from __future__ import annotations

import random
import struct
from dataclasses import dataclass
from typing import Iterator, List, Optional, Tuple

# ---------------------------------------------------------------------------
# constants
# ---------------------------------------------------------------------------
SYNC_FC = 0xC8            # serial sync byte (also = flight controller address)
ADDR_BROADCAST = 0x00
ADDR_RADIO_TX = 0xEA      # handset
ADDR_CRSF_RX = 0xEC
ADDR_CRSF_TX = 0xEE       # TX module
VALID_START = {SYNC_FC, ADDR_RADIO_TX, ADDR_CRSF_RX, ADDR_CRSF_TX, ADDR_BROADCAST}

TYPE_GPS = 0x02
TYPE_BATTERY = 0x08
TYPE_LINK_STATS = 0x14
TYPE_RC_CHANNELS = 0x16
TYPE_ATTITUDE = 0x1E
TYPE_FLIGHT_MODE = 0x21

MAX_FRAME = 64            # sync + len + (type + payload + crc)
MIN_LEN_FIELD = 2         # type + crc (spec range 2..62)
MAX_LEN_FIELD = MAX_FRAME - 2

CH_MIN, CH_MID, CH_MAX = 172, 992, 1811   # 988us / 1500us / 2012us (ELRS crsf_protocol.h)


# ---------------------------------------------------------------------------
# CRC-8 DVB-S2 (poly 0xD5)
# ---------------------------------------------------------------------------
def _make_table(poly: int) -> List[int]:
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
        table.append(crc)
    return table


_CRC_TABLE = _make_table(0xD5)


def crc8_d5(data: bytes, crc: int = 0) -> int:
    for b in data:
        crc = _CRC_TABLE[crc ^ b]
    return crc


def crc8_d5_bitwise(data: bytes) -> int:
    """Slow reference implementation, used to cross-check the table version."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


# ---------------------------------------------------------------------------
# frame build / channel packing
# ---------------------------------------------------------------------------
def build_frame(ftype: int, payload: bytes, sync: int = SYNC_FC) -> bytes:
    if len(payload) + 2 > MAX_LEN_FIELD:
        raise ValueError("payload too long for CRSF (max 60 bytes)")
    body = bytes([ftype]) + payload
    return bytes([sync, len(body) + 1]) + body + bytes([crc8_d5(body)])


def pack_channels(ch: List[int]) -> bytes:
    """16 x 11-bit, little-endian bit order (ch0 in the lowest bits of byte 0) -> 22 bytes."""
    if len(ch) != 16:
        raise ValueError("need 16 channels")
    acc = 0
    for i, v in enumerate(ch):
        if not 0 <= v <= 0x7FF:
            raise ValueError(f"channel {i} out of 11-bit range: {v}")
        acc |= v << (11 * i)
    return acc.to_bytes(22, "little")


def unpack_channels(payload: bytes) -> List[int]:
    if len(payload) < 22:
        raise ValueError("RC payload must be >= 22 bytes")
    acc = int.from_bytes(payload[:22], "little")
    return [(acc >> (11 * i)) & 0x7FF for i in range(16)]


def ticks_to_us(t: int) -> float:
    """TBS spec formula. 172 -> 987.5, 992 -> 1500, 1811 -> 2011.875."""
    return (t - 992) * 5 / 8 + 1500


def us_to_ticks(us: float) -> int:
    return int(round((us - 1500) * 8 / 5 + 992))


def build_rc_frame(ch: List[int]) -> bytes:
    return build_frame(TYPE_RC_CHANNELS, pack_channels(ch))


@dataclass
class LinkStats:
    up_rssi1: int      # dBm * -1 (uint8)
    up_rssi2: int
    up_lq: int         # %
    up_snr: int        # dB, int8
    active_ant: int
    rf_mode: int
    up_tx_power: int   # enum
    dn_rssi: int       # dBm * -1
    dn_lq: int
    dn_snr: int        # int8

    _FMT = ">BBBbBBBBBb"   # 10 bytes

    def to_payload(self) -> bytes:
        return struct.pack(self._FMT, self.up_rssi1, self.up_rssi2, self.up_lq, self.up_snr,
                           self.active_ant, self.rf_mode, self.up_tx_power,
                           self.dn_rssi, self.dn_lq, self.dn_snr)

    @classmethod
    def from_payload(cls, p: bytes) -> "LinkStats":
        if len(p) < 10:
            raise ValueError("link stats payload must be >= 10 bytes")
        return cls(*struct.unpack(cls._FMT, p[:10]))


# ---------------------------------------------------------------------------
# streaming parser
# ---------------------------------------------------------------------------
@dataclass
class Frame:
    addr: int
    ftype: int
    payload: bytes
    crc: int


class StreamParser:
    """Byte-stream CRSF parser.

    Policy (same idea as EdgeTX _processFrames):
      - bad start byte            -> drop 1 byte
      - len field out of 2..62    -> drop 1 byte (this is the max-length guard)
      - not enough bytes yet      -> wait (partial frame kept)
      - CRC mismatch              -> drop 1 byte only (a real frame may start inside)
      - buffer never grows beyond max_buf (protects against a byte flood)
    """

    def __init__(self, max_buf: int = 256):
        self.buf = bytearray()
        self.max_buf = max_buf
        self.stats = {"ok": 0, "crc_err": 0, "len_err": 0, "skipped": 0, "overflow": 0}

    def feed(self, data: bytes) -> List[Frame]:
        self.buf += data
        if len(self.buf) > self.max_buf:
            drop = len(self.buf) - self.max_buf
            del self.buf[:drop]
            self.stats["overflow"] += drop
        out: List[Frame] = []
        while len(self.buf) >= 2:
            if self.buf[0] not in VALID_START:
                del self.buf[0]
                self.stats["skipped"] += 1
                continue
            ln = self.buf[1]
            if not MIN_LEN_FIELD <= ln <= MAX_LEN_FIELD:
                del self.buf[0]
                self.stats["len_err"] += 1
                continue
            total = ln + 2
            if len(self.buf) < total:
                break                      # partial: wait for more bytes
            body = bytes(self.buf[2:total - 1])
            if crc8_d5(body) != self.buf[total - 1]:
                del self.buf[0]            # resync by ONE byte
                self.stats["crc_err"] += 1
                continue
            out.append(Frame(self.buf[0], body[0], body[1:], self.buf[total - 1]))
            del self.buf[:total]
            self.stats["ok"] += 1
        return out


# ---------------------------------------------------------------------------
# self tests
# ---------------------------------------------------------------------------
def _chunks(data: bytes, rng: random.Random) -> Iterator[bytes]:
    i = 0
    while i < len(data):
        n = rng.randint(1, 7)
        yield data[i:i + n]
        i += n


def _parse_all(data: bytes, chunked: bool = False, seed: int = 0) -> Tuple[List[Frame], StreamParser]:
    p = StreamParser()
    frames: List[Frame] = []
    if chunked:
        for c in _chunks(data, random.Random(seed)):
            frames += p.feed(c)
    else:
        frames += p.feed(data)
    return frames, p


def self_test() -> None:
    rng = random.Random(1234)

    # 1. CRC: table == bitwise, plus golden vectors from EdgeTX radio/src/tests/crossfire.cpp
    for _ in range(200):
        d = bytes(rng.randrange(256) for _ in range(rng.randint(0, 62)))
        assert crc8_d5(d) == crc8_d5_bitwise(d)
    edgetx_ls = bytes([0x00, 0x0C, 0x14, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x01, 0x03, 0x00, 0x00, 0x00, 0xF4])
    assert crc8_d5(edgetx_ls[2:2 + edgetx_ls[1] - 1]) == 0xF4
    edgetx_ext = bytes([0xEA, 0x09, 0xFF, 0x11, 0xFD, 0x05, 0x00, 0x00, 0x13, 0x01, 0x8C])
    assert crc8_d5(edgetx_ext[2:-1]) == 0x8C

    # 2. RC channel round trip + fixed size (sync+len+type+22+crc = 26 bytes)
    for _ in range(200):
        ch = [rng.randint(0, 0x7FF) for _ in range(16)]
        fr = build_rc_frame(ch)
        assert len(fr) == 26 and fr[1] == 24
        got, _ = _parse_all(fr)
        assert len(got) == 1 and got[0].ftype == TYPE_RC_CHANNELS
        assert unpack_channels(got[0].payload) == ch
    # bit order check: only ch0 = 0x7FF -> first byte 0xFF, second byte low 3 bits 0b111
    p = pack_channels([0x7FF] + [0] * 15)
    assert p[0] == 0xFF and p[1] == 0x07 and not any(p[2:])

    # 3. us mapping
    assert ticks_to_us(992) == 1500
    assert abs(ticks_to_us(CH_MIN) - 988) < 1 and abs(ticks_to_us(CH_MAX) - 2012) < 1
    assert us_to_ticks(1500) == 992 and us_to_ticks(987.5) == 172 and us_to_ticks(2012) == 1811
    # off-by-one trap: with the spec formula 988us -> 172.8 -> 173, not 172 (ELRS labels 172 as '988us')
    assert us_to_ticks(988) == 173

    # 4. link statistics round trip (14 bytes on the wire) + signed SNR
    ls = LinkStats(up_rssi1=67, up_rssi2=70, up_lq=100, up_snr=-5, active_ant=1, rf_mode=7,
                   up_tx_power=3, dn_rssi=80, dn_lq=98, dn_snr=9)
    fr_ls = build_frame(TYPE_LINK_STATS, ls.to_payload())
    assert len(fr_ls) == 14
    got, _ = _parse_all(fr_ls)
    assert LinkStats.from_payload(got[0].payload) == ls

    # 5. stream of many frames, delivered in random small chunks (partial reads)
    stream = b"".join(build_rc_frame([rng.randint(172, 1811) for _ in range(16)]) if i % 3 else fr_ls
                      for i in range(60))
    got, p = _parse_all(stream, chunked=True, seed=7)
    assert len(got) == 60 and p.stats["crc_err"] == 0

    # 6. garbage before / between frames (including fake sync 0xC8 with a sane length)
    good = build_rc_frame([CH_MID] * 16)
    fake = bytes([0xC8, 0x18]) + bytes(5)          # looks like an RC frame start, then cut
    noisy = bytes([0x55, 0xAA, 0x00]) + fake + good + bytes([0xC8]) + good
    got, p = _parse_all(noisy)
    assert len(got) == 2, (len(got), p.stats)
    assert all(unpack_channels(f.payload) == [CH_MID] * 16 for f in got)
    assert p.stats["crc_err"] >= 1               # the fake frame failed CRC, then resynced by 1 byte

    # 7. corrupted frame (bit flip) is rejected, following frame still parsed
    bad = bytearray(good)
    bad[10] ^= 0x10
    got, p = _parse_all(bytes(bad) + good)
    assert len(got) == 1 and p.stats["crc_err"] >= 1

    # 8. dropped byte inside a frame (UART overrun) -> next frame survives
    dropped = good[:12] + good[13:]
    got, _ = _parse_all(dropped + good + good)
    assert len(got) == 2

    # 9. max-length guard: len field 0xFE / 0x3F / 0x01 never makes the parser wait forever
    for badlen in (0x00, 0x01, 0x3F, 0x40, 0xFE, 0xFF):
        got, p = _parse_all(bytes([0xC8, badlen]) + good)
        assert len(got) == 1, badlen
        assert p.stats["len_err"] >= 1

    # 10. byte flood: buffer stays bounded
    p = StreamParser(max_buf=256)
    for _ in range(100):
        p.feed(bytes([0xC8, 0x3E]) + bytes(60))  # each looks like a max frame, never valid
    assert len(p.buf) <= 256

    # 11. fuzz: random mutation never crashes and never returns a frame with a bad CRC
    base = b"".join(build_rc_frame([rng.randint(0, 2047) for _ in range(16)]) for _ in range(20))
    for _ in range(300):
        m = bytearray(base)
        for _ in range(rng.randint(1, 8)):
            op = rng.random()
            pos = rng.randrange(len(m))
            if op < 0.4:
                m[pos] ^= 1 << rng.randrange(8)             # bit flip
            elif op < 0.7:
                del m[pos]                                   # drop byte
            else:
                m.insert(pos, rng.choice([0xC8, 0x18, rng.randrange(256)]))  # insert (often fake sync)
        got, p = _parse_all(bytes(m), chunked=True, seed=rng.randrange(1 << 30))
        for f in got:
            assert crc8_d5(bytes([f.ftype]) + f.payload) == f.crc
        assert len(got) <= 20 + 2   # false positives from CRC-8 are possible but rare
        assert len(p.buf) <= p.max_buf

    # 12. timing math used in the chapter: 26-byte RC frame at 420000 baud, 8N1 = 10 bits/byte
    t_us = 26 * 10 / 420000 * 1e6
    assert 618 < t_us < 620     # ~619 us
    print(f"[info] RC frame airtime on UART @420k 8N1 = {t_us:.1f} us; max 64B frame = {64*10/420000*1e6:.1f} us")


if __name__ == "__main__":
    self_test()
    print("crsf.py self-test: PASS")
