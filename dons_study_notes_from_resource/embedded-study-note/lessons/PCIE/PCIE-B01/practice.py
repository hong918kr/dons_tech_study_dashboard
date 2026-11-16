#!/usr/bin/env python3
"""PCIE-B01 추가 연습 — 한 번에 검증

  연습 1  Gen3 x4 레인 스트라이핑 (128b/130b 블록을 레인마다)
  연습 2  12비트 Sequence Number 비교와 Ack/Nak replay 버퍼
  연습 3  LCRC 를 "나머지 상수(residue)" 로 검사하기
  연습 4  Gen1/2 DLLP 수신기: Ack/Nak 해석 + Bad DLLP
  연습 5  프레이밍 오버헤드 표: payload 크기 × Gen1/2 vs Gen3 (PCIE-B02 의 맛보기)

본문(solution.py)의 모델 정의를 그대로 따른다: CRC-32(zlib 값, LSB 바이트 먼저), CRC-16 poly 0x100B
(init/xorout 0xFFFF, big-endian), Gen3 STP 토큰 배치 — 모두 이 레슨의 약속이며 실리콘 비트 순서는 스펙 확인 권장.

  python3 -I practice.py           # 테스트
  python3 -I practice.py --table   # 연습 5 표 출력 후 테스트
"""
import binascii
import sys

K_STP, K_END, K_SDP = 0xFB, 0xFD, 0x5C
G3_IDL, SYNC_DATA = 0x00, 0b10
DLLP_ACK, DLLP_NAK = 0x00, 0x10
PCI_ERR_COR_RCVR = 0x00000001        # pci_regs.h 기준
PCI_ERR_COR_BAD_DLLP = 0x00000080


class LinkError(Exception):
    def __init__(self, layer, code, aer, msg):
        super().__init__("%s %s: %s" % (layer, code, msg))
        self.layer, self.code, self.aer = layer, code, aer


# ---------------------------------------------------------------- 본문 모델 축약판 (PCIE-B01 solution.py)
def crc32_pcie(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def crc16_dllp(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x100B) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc ^ 0xFFFF


def make_mwr(addr, payload, req_id=0, tag=0):
    """3DW MWr, payload 1..1024 DW (연습 5 를 위해 본문보다 넓게), ECRC 없음."""
    n = len(payload) // 4
    assert len(payload) % 4 == 0 and 1 <= n <= 1024 and addr % 4 == 0 and addr < 1 << 32
    lf = n & 0x3FF
    be = 0xFF if n > 1 else 0x0F
    return (bytes([0x40, 0, lf >> 8, lf & 0xFF]) + req_id.to_bytes(2, "big") + bytes([tag, be])
            + addr.to_bytes(4, "big") + bytes(payload))


def dll_wrap(tlp, seq):
    hdr = bytes([(seq >> 8) & 0xF, seq & 0xFF])
    return hdr + tlp + crc32_pcie(hdr + tlp).to_bytes(4, "little")


def frame_crc4(length):
    crc = 0
    for i in range(10, -1, -1):
        fb = ((crc >> 3) & 1) ^ ((length >> i) & 1)
        crc = ((crc << 1) & 0xF) ^ (0x3 if fb else 0)
    return crc


def gen3_stream(dll):
    seq = ((dll[0] & 0xF) << 8) | dll[1]
    L = 1 + (len(dll) - 2) // 4
    fcrc = frame_crc4(L)
    fp = (bin(L).count("1") + bin(fcrc).count("1")) & 1
    return bytes([((L & 0xF) << 4) | 0xF, (fp << 7) | (L >> 4), (fcrc << 4) | (seq >> 8), seq & 0xFF]) + dll[2:]


def dllp(kind, seq):
    body = bytes([kind, 0, (seq >> 8) & 0xF, seq & 0xFF])
    return body + crc16_dllp(body).to_bytes(2, "big")


# ================================================================ 연습 1: x4 레인 스트라이핑
def lane_blocks(stream, lanes):
    """바이트 i → 레인 i % lanes. 스트림을 lanes*16 의 배수까지 IDL 로 채운 뒤, 레인마다 (sync, 16B) 블록 목록.
    반환: [레인0 블록들, 레인1 블록들, ...] (모든 레인 블록 수 같음 — 같은 시각에 함께 나간다)."""
    unit = 16 * lanes
    padded = bytes(stream) + bytes([G3_IDL]) * ((-len(stream)) % unit)
    out = []
    for ln in range(lanes):
        lane = padded[ln::lanes]
        out.append([(SYNC_DATA, lane[i:i + 16]) for i in range(0, len(lane), 16)])
    return out


def destripe(lanes_blocks):
    lanes = len(lanes_blocks)
    per_lane = [b"".join(d for _, d in bl) for bl in lanes_blocks]
    return bytes(per_lane[i % lanes][i // lanes] for i in range(lanes * len(per_lane[0])))


def time_ns_gen3(lanes_blocks, gts=8.0):
    """레인은 병렬: 걸리는 시간 = (레인당 블록 수 × 130 비트) / (GT/s)."""
    return len(lanes_blocks[0]) * 130 / gts


# ================================================================ 연습 2: 12비트 seq 와 replay 버퍼
def seq_newer(a, b):
    """a 가 b 보다 '뒤' 인가 (mod 4096, 반 바퀴 = 2048 미만 앞섬만 인정)."""
    return 0 < ((a - b) & 0xFFF) < 2048


def on_ack(buf, ack_seq):
    """Ack(ack_seq) = ack_seq 까지(포함) 모두 받음 → 그보다 '뒤' 인 것만 남긴다 (순서 유지)."""
    return [(s, t) for s, t in buf if seq_newer(s, ack_seq)]


def on_nak(buf, nak_seq):
    """Nak(nak_seq) = nak_seq 까지는 잘 받았고 그다음부터 문제 → 확인된 것 지우고 남은 것을 전부 순서대로 재전송."""
    keep = on_ack(buf, nak_seq)
    return keep, [s for s, _ in keep]


# ================================================================ 연습 3: residue 검사
CRC32_RESIDUE = 0x2144DF1C


def lcrc_ok_residue(dll):
    """LCRC 를 LSB 바이트부터 붙였다면, 붙인 것까지 통째로 CRC 하면 데이터와 무관한 상수가 나온다."""
    return crc32_pcie(dll) == CRC32_RESIDUE


def lcrc_ok_compare(dll):
    return len(dll) >= 6 and crc32_pcie(dll[:-4]) == int.from_bytes(dll[-4:], "little")


# ================================================================ 연습 4: Gen1/2 DLLP 수신기
def rx_dllp_gen12(symbols):
    """SDP + 6 D + END → ("ACK"|"NAK", seq). 프레이밍 → FRAMING/RCVR, CRC-16 → BAD_DLLP/BAD_DLLP 비트."""
    if (len(symbols) != 8 or tuple(symbols[0]) != (True, K_SDP) or tuple(symbols[-1]) != (True, K_END)
            or any(k for k, _ in symbols[1:-1])):
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "bad DLLP framing")
    d = bytes(b for _, b in symbols[1:-1])
    if crc16_dllp(d[:4]) != int.from_bytes(d[4:], "big"):
        raise LinkError("DLL", "BAD_DLLP", PCI_ERR_COR_BAD_DLLP, "DLLP CRC-16 mismatch")
    kind = {DLLP_ACK: "ACK", DLLP_NAK: "NAK"}.get(d[0])
    if kind is None:
        raise LinkError("DLL", "BAD_DLLP", PCI_ERR_COR_BAD_DLLP, "unknown DLLP type 0x%02X" % d[0])
    return kind, ((d[2] & 0xF) << 8) | d[3]


# ================================================================ 연습 5: 오버헤드
def wire_bits(payload_bytes, gen, ecrc=False):
    """3DW MWr 하나의 선로 비트. Gen1/2: (STP 1 + Seq 2 + Hdr 12 + P + ECRC + LCRC 4 + END 1) × 10.
    Gen3: (STP 4 + Hdr 12 + P + ECRC + LCRC 4) × 8 × 130/128 (연속 스트림 가정, 블록 반올림 없음)."""
    e = 4 if ecrc else 0
    if gen in (1, 2):
        return (1 + 2 + 12 + payload_bytes + e + 4 + 1) * 10
    return (4 + 12 + payload_bytes + e + 4) * 8 * 130 / 128


def efficiency(payload_bytes, gen, ecrc=False):
    return payload_bytes * 8 / wire_bits(payload_bytes, gen, ecrc)


def print_table():
    print("payload  Gen1/2 bits  eff     Gen3 bits  eff")
    for p in (4, 64, 128, 256, 512):
        print("%5dB  %10d  %5.1f%%  %9.1f  %5.1f%%"
              % (p, wire_bits(p, 1), 100 * efficiency(p, 1), wire_bits(p, 3), 100 * efficiency(p, 3)))
    print()


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


class XorShift32:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x


def code_of(fn):
    try:
        fn()
        return "OK"
    except LinkError as e:
        return e.code


def run_tests():
    print("-- 연습 1: x4 striping --")
    mwr16 = make_mwr(0xFE001000, bytes(range(16)))
    st = gen3_stream(dll_wrap(mwr16, 0x200))
    check("MWr 4DW Gen3 stream = 4+12+16+4 = 36 bytes", lambda: len(st) == 36)
    x4 = lane_blocks(st, 4)
    x1 = lane_blocks(st, 1)
    check("x4: 9 bytes/lane -> 1 block per lane, lane0 starts 7F.. (STP byte0)",
          lambda: [len(l) for l in x4] == [1, 1, 1, 1] and x4[0][0][1][0] == st[0] and x4[1][0][1][0] == st[1])
    check("x1: 36 bytes -> 3 blocks", lambda: len(x1[0]) == 3)
    check("time @8GT/s: x1 48.75 ns, x4 16.25 ns", lambda: time_ns_gen3(x1) == 48.75 and time_ns_gen3(x4) == 16.25)
    check("destripe(x4) == stream + IDL padding",
          lambda: destripe(x4) == st + bytes(64 - 36) and destripe(lane_blocks(st, 8))[:36] == st)

    print("-- 연습 2: seq / replay --")
    check("seq_newer(0, 4095) and not seq_newer(4095, 0)", lambda: seq_newer(0, 4095) and not seq_newer(4095, 0))
    check("seq_newer: equal -> False, +2047 True, +2048 False",
          lambda: not seq_newer(5, 5) and seq_newer(2047, 0) and not seq_newer(2048, 0))
    buf = [(4094, "a"), (4095, "b"), (0, "c"), (1, "d")]
    check("Ack(4095) across wrap -> [0, 1] left", lambda: [s for s, _ in on_ack(buf, 4095)] == [0, 1])
    check("Nak(0) -> purge 4094..0, replay [1]", lambda: on_nak(buf, 0)[1] == [1])
    check("Nak(4093) (nothing acked yet) -> replay all 4", lambda: on_nak(buf, 4093)[1] == [4094, 4095, 0, 1])
    check("stale duplicate Ack(4090) -> nothing purged", lambda: len(on_ack(buf, 4090)) == 4)

    print("-- 연습 3: residue --")
    check("residue constant: crc32(data || LE(crc32(data))) == 0x2144DF1C for b'123456789'",
          lambda: binascii.crc32(b"123456789" + (0xCBF43926).to_bytes(4, "little")) == CRC32_RESIDUE)

    def residue_eq_compare():
        r = XorShift32(0x5E5)
        for _ in range(40):
            n = 1 + r.next() % 4
            d = dll_wrap(make_mwr((r.next() & 0xFFFF) * 4, bytes(r.next() & 0xFF for _ in range(4 * n))), r.next() & 0xFFF)
            if not (lcrc_ok_residue(d) and lcrc_ok_compare(d)):
                return False
            for bit in range(0, len(d) * 8, 3):
                bad = bytearray(d)
                bad[bit // 8] ^= 1 << (bit % 8)
                if lcrc_ok_residue(bytes(bad)) or lcrc_ok_compare(bytes(bad)):
                    return False
        return True
    check("residue check == compare check on 40 frames + every 3rd bit flipped", residue_eq_compare)

    print("-- 연습 4: DLLP receiver --")

    def frame(d):
        return [(True, K_SDP)] + [(False, b) for b in d] + [(True, K_END)]
    check("Ack 0x123 -> ('ACK', 0x123), Nak 0xFFF -> ('NAK', 0xFFF)",
          lambda: rx_dllp_gen12(frame(dllp(DLLP_ACK, 0x123))) == ("ACK", 0x123)
          and rx_dllp_gen12(frame(dllp(DLLP_NAK, 0xFFF))) == ("NAK", 0xFFF))

    def sweep():
        f = frame(dllp(DLLP_ACK, 0x123))
        cnt = {}
        for i in range(8):
            for bit in range(9):
                g = list(f)
                k, b = g[i]
                g[i] = (not k, b) if bit == 8 else (k, b ^ (1 << bit))
                c = code_of(lambda: rx_dllp_gen12(g))
                cnt[c] = cnt.get(c, 0) + 1
        return cnt
    check("72 single flips on Ack frame: FRAMING 24, BAD_DLLP 48", lambda: sweep() == {"FRAMING": 24, "BAD_DLLP": 48})
    check("Bad DLLP sets PCI_ERR_COR_BAD_DLLP (0x80)",
          lambda: (lambda e: e is not None and e.code == "BAD_DLLP" and e.aer == 0x80)(
              _catch(lambda: rx_dllp_gen12(frame(dllp(DLLP_ACK, 1)[:5] + b"\x00")))))

    print("-- 연습 5: overhead --")
    check("4B MWr: Gen1/2 240 bits (13.3%), Gen3 195.0 bits (16.4%)",
          lambda: wire_bits(4, 1) == 240 and wire_bits(4, 3) == 195.0
          and round(100 * efficiency(4, 1), 1) == 13.3 and round(100 * efficiency(4, 3), 1) == 16.4)
    check("256B MWr: Gen1/2 2760 bits (74.2%), Gen3 2242.5 bits (91.3%)",
          lambda: wire_bits(256, 2) == 2760 and wire_bits(256, 4) == 2242.5
          and round(100 * efficiency(256, 1), 1) == 74.2 and round(100 * efficiency(256, 3), 1) == 91.3)
    check("encoder agrees: len(Gen3 stream of 256B MWr) * 8 * 130/128 == wire_bits(256, 3)",
          lambda: len(gen3_stream(dll_wrap(make_mwr(0x1000, bytes(256)), 1))) * 8 * 130 / 128 == wire_bits(256, 3))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def _catch(fn):
    try:
        fn()
    except LinkError as e:
        return e
    return None


if __name__ == "__main__":
    if "--table" in sys.argv[1:]:
        print_table()
    sys.exit(0 if run_tests() else 1)
