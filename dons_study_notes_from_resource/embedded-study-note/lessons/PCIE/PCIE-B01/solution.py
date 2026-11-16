#!/usr/bin/env python3
"""PCIE-B01 PCIe 계층 구조: Transaction / Data Link / Physical

TLP 하나가 계층을 내려가며 감싸지는 과정을 바이트 단위로 만든다 (송신) 그리고 거꾸로 푼다 (수신).
  Part A  [TL ] 3DW MRd / MWr(1-4 DW) TLP 헤더 + 데이터 + 선택적 ECRC (CRC-32)
  Part B  [DLL] 12비트 Sequence Number(2B) + LCRC(4B),  DLLP Ack(6B, CRC-16)
          [PHY] Gen1/2: STP ... END 심볼 (8b/10b → 심볼당 10비트)
                Gen3+ : 4B STP 토큰(길이·Frame CRC·Seq) + 128b/130b 블록(2비트 sync), END 없음
  Part C  수신기: PHY → DLL → TL 순서로 벗기며 검사, 비트 오류 주입 → 어느 계층이 잡는지 + AER 비트

모델 정의 (이 레슨의 약속 — 실리콘의 비트 순서를 주장하지 않는다):
  * TLP 헤더는 DW0 byte0 부터 big-endian 순서 (Fmt/Type 이 첫 바이트). 데이터는 주소 순서.
  * ECRC/LCRC = CRC-32 (poly 0x04C11DB7, 반사형, init/xorout 0xFFFFFFFF = zlib 와 같은 값),
    결과를 LSB 바이트부터 붙인다. 실제 선로 위 비트/바이트 매핑은 스펙 정의 — 스펙 확인 권장.
  * Gen3 STP 토큰 비트 배치, Frame CRC(여기선 CRC-4 x^4+x+1) 식, DLLP CRC-16 의 init/xorout/바이트 순서도
    이 모델의 정의다 (스펙 확인 권장). 테스트는 왕복·필드 위치·오류 검출만 확인한다.

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --show     # 바이트 단위 계층 표 출력 후 테스트

Python 3.9 표준 라이브러리만 (binascii 는 CRC 교차 검증용).
"""
import binascii
import sys

# ---------------------------------------------------------------- 상수
FMT_TYPE_MRD32 = 0x00      # Fmt=000 (3DW, no data)   Type=0_0000 (Memory Request)
FMT_TYPE_MWR32 = 0x40      # Fmt=010 (3DW, with data) Type=0_0000
K_STP, K_END, K_SDP, K_EDB = 0xFB, 0xFD, 0x5C, 0xFE    # K27.7 K29.7 K28.2 K30.7 (8b/10b 제어 심볼)
G3_IDL = 0x00                                          # Gen3 IDL 토큰 (빈 자리 채움)
G3_SDP = b"\xF0\xAC"                                   # Gen3 SDP 토큰 (DLLP 시작) — 스펙 확인 권장
SYNC_DATA, SYNC_OS = 0b10, 0b01                        # 128b/130b sync header: Data / Ordered Set 블록
DLLP_ACK, DLLP_NAK = 0x00, 0x10

# Linux include/uapi/linux/pci_regs.h 기준 AER 상태 비트
PCI_ERR_COR_RCVR = 0x00000001       # Receiver Error      (PHY: 프레이밍/심볼 오류)
PCI_ERR_COR_BAD_TLP = 0x00000040    # Bad TLP             (DLL: LCRC/Seq 오류 → Nak + replay)
PCI_ERR_COR_BAD_DLLP = 0x00000080   # Bad DLLP            (DLL: DLLP CRC-16 오류)
PCI_ERR_UNC_MALF_TLP = 0x00040000   # Malformed TLP       (TL: 헤더/길이 불일치)
PCI_ERR_UNC_ECRC = 0x00080000       # ECRC Error          (TL: 종단 간 CRC 오류)


class LinkError(Exception):
    """수신 중 검출된 오류. layer = PHY/DLL/TL, code = FRAMING/BAD_TLP/ECRC/MALF, aer = 위 비트."""

    def __init__(self, layer, code, aer, msg):
        super().__init__("%s %s: %s" % (layer, code, msg))
        self.layer, self.code, self.aer = layer, code, aer


# ================================================================ Part A: Transaction Layer
def crc32_pcie(data):
    """CRC-32, poly 0x04C11DB7 (반사형 0xEDB88320), init 0xFFFFFFFF, xorout 0xFFFFFFFF. 비트 단위 구현."""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def ecrc_of(tlp_wo_ecrc):
    """[TL] ECRC: 헤더+데이터의 CRC-32. 경로 중 바뀔 수 있는 variant 비트(Type[0], EP)는 1로 보고 계산."""
    b = bytearray(tlp_wo_ecrc)
    b[0] |= 0x01                     # Type[0] (config Type1→Type0 변환 시 바뀜)
    b[2] |= 0x40                     # EP (스위치가 poison 할 수 있음)
    return crc32_pcie(bytes(b))


def make_tlp(kind, req_id, tag, addr, length_dw=1, payload=b"", ecrc=False):
    """[TL] 3DW 메모리 요청 TLP 바이트. kind = "MRd"(length_dw 1..1024, payload 없음) / "MWr"(payload 1..4 DW).
    addr 는 4바이트 정렬, < 4GB (3DW). ecrc=True 면 TD=1 + ECRC 4B. 잘못된 인자 → ValueError."""
    if not (0 <= addr < (1 << 32)) or addr & 3:
        raise ValueError("addr must be DW-aligned and < 4GB for a 3DW header (4DW = PCIE-B10)")
    if not (0 <= req_id <= 0xFFFF and 0 <= tag <= 0xFF):
        raise ValueError("req_id 16-bit, tag 8-bit")
    if kind == "MRd":
        if payload or not (1 <= length_dw <= 1024):
            raise ValueError("MRd: 1..1024 DW, no payload")
        fmt_type, n = FMT_TYPE_MRD32, length_dw
    elif kind == "MWr":
        if len(payload) % 4 or not (4 <= len(payload) <= 16):
            raise ValueError("MWr: payload 1..4 DW in this lesson")
        fmt_type, n = FMT_TYPE_MWR32, len(payload) // 4
    else:
        raise ValueError("kind must be MRd or MWr")
    first_be, last_be = 0xF, (0xF if n > 1 else 0x0)       # 1 DW 이면 Last BE = 0 (규칙)
    lf = n & 0x3FF                                           # Length 10비트, 1024 → 0
    td = 1 if ecrc else 0
    dw0 = bytes([fmt_type, 0x00, (td << 7) | (lf >> 8), lf & 0xFF])   # TC/Attr/TH/EP/AT = 0
    dw1 = req_id.to_bytes(2, "big") + bytes([tag, (last_be << 4) | first_be])
    dw2 = addr.to_bytes(4, "big")                            # Addr[31:2], 하위 2비트 00
    tlp = dw0 + dw1 + dw2 + bytes(payload)
    if ecrc:
        tlp += ecrc_of(tlp).to_bytes(4, "little")            # 모델: LSB 바이트 먼저
    return tlp


HDR_NAMES = ["DW0 Fmt|Type", "DW0 TC|Attr2|TH", "DW0 TD|EP|Attr|AT|Len[9:8]", "DW0 Len[7:0]",
             "DW1 ReqID Bus", "DW1 ReqID Dev|Fn", "DW1 Tag", "DW1 LastBE|FirstBE",
             "DW2 Addr[31:24]", "DW2 Addr[23:16]", "DW2 Addr[15:8]", "DW2 Addr[7:2]|00"]


def tlp_field_names(tlp):
    """[TL] TLP 바이트마다 필드 이름: 헤더 12개(HDR_NAMES) + "Data DWi Bj" + "ECRC Bk"."""
    if len(tlp) < 12:
        return []
    names = list(HDR_NAMES)
    td = tlp[2] >> 7
    ndata = len(tlp) - 12 - (4 if td else 0)
    names += ["Data DW%d B%d" % (i // 4, i % 4) for i in range(max(ndata, 0))]
    if td:
        names += ["ECRC B%d" % k for k in range(4)]
    return names


# ================================================================ Part B: Data Link + Physical
def dll_wrap(tlp, seq):
    """[DLL] Seq(2B: 예약 4비트=0 | Seq[11:8], Seq[7:0]) + TLP + LCRC(4B, Seq+TLP 전체에 대한 CRC-32)."""
    if not 0 <= seq <= 0xFFF:
        raise ValueError("sequence number is 12-bit")
    hdr = bytes([(seq >> 8) & 0x0F, seq & 0xFF])
    return hdr + tlp + crc32_pcie(hdr + tlp).to_bytes(4, "little")


def phy_gen12_tlp(dll, nullify=False):
    """[PHY Gen1/2] 심볼 목록 [(is_k, byte)]: STP + DLL 바이트 + END.
    nullify=True: cut-through 전달 중 오류 발견 → LCRC 반전 + END 대신 EDB (수신기는 조용히 버림)."""
    body = bytearray(dll)
    if nullify:
        for i in range(len(body) - 4, len(body)):
            body[i] ^= 0xFF
    return [(True, K_STP)] + [(False, b) for b in body] + [(True, K_EDB if nullify else K_END)]


def wire_bits_gen12(symbols):
    """8b/10b: 심볼 하나(데이터든 K든) = 선로 위 10비트."""
    return 10 * len(symbols)


def frame_crc4(length):
    """[PHY Gen3] STP 토큰 Frame CRC — 모델: CRC-4 (x^4+x+1, init 0) over Length[10:0] MSB 먼저. 스펙 확인 권장."""
    crc = 0
    for i in range(10, -1, -1):
        fb = ((crc >> 3) & 1) ^ ((length >> i) & 1)
        crc = ((crc << 1) & 0xF) ^ (0x3 if fb else 0)
    return crc


def parity(x):
    return bin(x).count("1") & 1


def stp_token(length_dw, seq):
    """[PHY Gen3] 4바이트 STP 토큰 (모델 배치, 스펙 확인 권장):
       B0 = Len[3:0]<<4 | 1111b    B1 = FP<<7 | Len[10:4]
       B2 = FCRC<<4 | Seq[11:8]     B3 = Seq[7:0]
    Length = STP 토큰 + TLP + LCRC 전체의 DW 수 (11비트). FP = Length·FCRC 의 짝수 패리티."""
    if not (5 <= length_dw <= 0x7FF and 0 <= seq <= 0xFFF):
        raise ValueError("length 5..2047 DW, seq 12-bit")
    fcrc = frame_crc4(length_dw)
    fp = parity(length_dw) ^ parity(fcrc)
    return bytes([((length_dw & 0xF) << 4) | 0xF, (fp << 7) | ((length_dw >> 4) & 0x7F),
                  (fcrc << 4) | ((seq >> 8) & 0xF), seq & 0xFF])


def phy_gen3_tlp(dll):
    """[PHY Gen3] 바이트 스트림 = STP 토큰(DLL 의 Seq 2B 를 흡수) + TLP + LCRC. END 없음 (길이로 끝을 안다)."""
    if len(dll) < 18 or (len(dll) - 2) % 4 or dll[0] & 0xF0:
        raise ValueError("not a DLL TLP frame")
    seq = ((dll[0] & 0x0F) << 8) | dll[1]
    length_dw = 1 + (len(dll) - 2) // 4                      # 토큰 1 DW + TLP + LCRC
    return stp_token(length_dw, seq) + dll[2:]


def blocks_130(stream):
    """[PHY Gen3, x1] 16바이트씩 Data Block 으로 자르고 [(sync=10b, 16B)], 모자란 칸은 IDL(00h)로 채움."""
    out = []
    for i in range(0, len(stream), 16):
        chunk = bytes(stream[i:i + 16])
        out.append((SYNC_DATA, chunk + bytes([G3_IDL]) * (16 - len(chunk))))
    return out


def wire_bits_gen3(blocks):
    """128b/130b: 블록 하나 = 2비트 sync + 128비트 = 130비트 (x1)."""
    return 130 * len(blocks)


def crc16_dllp(data):
    """[DLL] DLLP CRC-16 — 모델: poly 0x100B, MSB 먼저, init 0xFFFF, xorout 0xFFFF. 스펙 확인 권장."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x100B) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc ^ 0xFFFF


def dllp_ack(seq):
    """[DLL] Ack DLLP 6B = Type(00h) + 예약 + 예약4|AckSeq[11:8] + AckSeq[7:0] + CRC-16(big-endian, 모델)."""
    if not 0 <= seq <= 0xFFF:
        raise ValueError("sequence number is 12-bit")
    body = bytes([DLLP_ACK, 0x00, (seq >> 8) & 0x0F, seq & 0xFF])
    return body + crc16_dllp(body).to_bytes(2, "big")


def phy_gen12_dllp(dllp):
    """[PHY Gen1/2] SDP + DLLP 6B + END = 8 심볼."""
    return [(True, K_SDP)] + [(False, b) for b in dllp] + [(True, K_END)]


def phy_gen3_dllp(dllp):
    """[PHY Gen3] SDP 토큰 2B + DLLP 6B = 8 바이트 (END 없음)."""
    return G3_SDP + bytes(dllp)


# ================================================================ Part C: 수신기 (PHY → DLL → TL)
def _check_lcrc(dll, nullified=False):
    """[DLL] LCRC 검사 공통부. 반환 {status, seq, tlp}. 실패 → LinkError(DLL, BAD_TLP)."""
    if len(dll) < 18 or (len(dll) - 2) % 4:
        raise LinkError("DLL", "BAD_TLP", PCI_ERR_COR_BAD_TLP, "bad TLP frame length %d" % len(dll))
    calc = crc32_pcie(dll[:-4])
    got = int.from_bytes(dll[-4:], "little")
    if nullified:
        if got == calc ^ 0xFFFFFFFF:
            return {"status": "NULLIFIED", "seq": None, "tlp": None}
        raise LinkError("DLL", "BAD_TLP", PCI_ERR_COR_BAD_TLP, "EDB but LCRC not inverted")
    if got != calc:
        raise LinkError("DLL", "BAD_TLP", PCI_ERR_COR_BAD_TLP,
                        "LCRC 0x%08X != 0x%08X -> Nak, replay" % (got, calc))
    return {"status": "OK", "seq": ((dll[0] & 0x0F) << 8) | dll[1], "tlp": bytes(dll[2:-4])}


def unwrap_gen12(symbols):
    """[PHY Gen1/2 → DLL] 심볼 목록 → {status, seq, tlp}. 프레이밍 오류 → LinkError(PHY, FRAMING)."""
    if len(symbols) < 2 or tuple(symbols[0]) != (True, K_STP):
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "frame does not start with STP")
    is_k, last = symbols[-1]
    if not is_k or last not in (K_END, K_EDB):
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "no END/EDB at frame end")
    mid = symbols[1:-1]
    if any(k for k, _ in mid):
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "K symbol inside TLP")
    return _check_lcrc(bytes(b for _, b in mid), nullified=(last == K_EDB))


def unwrap_gen3(blocks):
    """[PHY Gen3 → DLL] 블록 목록 → {status, seq, tlp}. sync/토큰/Frame CRC/길이 오류 → FRAMING."""
    stream = bytearray()
    for sync, data in blocks:
        if sync != SYNC_DATA or len(data) != 16:
            raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "bad sync header %s" % format(sync, "02b"))
        stream += data
    i = 0
    while i < len(stream) and stream[i] == G3_IDL:
        i += 1
    if i + 4 > len(stream) or stream[i] & 0x0F != 0x0F:
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "expected STP token at byte %d" % i)
    length = (stream[i] >> 4) | ((stream[i + 1] & 0x7F) << 4)
    fp, fcrc = stream[i + 1] >> 7, stream[i + 2] >> 4
    if fcrc != frame_crc4(length) or parity(length) ^ parity(fcrc) ^ fp:
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "STP Frame CRC/parity mismatch")
    end = i + 4 * length
    if length < 5 or end > len(stream):
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "STP length %d DW out of range" % length)
    if any(b != G3_IDL for b in stream[end:]):
        raise LinkError("PHY", "FRAMING", PCI_ERR_COR_RCVR, "unexpected token after TLP")
    seq = ((stream[i + 2] & 0x0F) << 8) | stream[i + 3]
    dll = bytes([seq >> 8, seq & 0xFF]) + bytes(stream[i + 4:end])     # DLL 시점의 Seq 2B 복원
    return _check_lcrc(dll)


def check_tlp(tlp):
    """[TL] 헤더 해석 + 길이 일관성(MALF) + ECRC. 반환 dict(kind, req_id, tag, addr, length, first_be,
    last_be, td, ep, payload). 이 레슨의 범위 밖(4DW, 다른 Type)도 MALF 로 거부."""
    def malf(msg):
        return LinkError("TL", "MALF", PCI_ERR_UNC_MALF_TLP, msg)
    if len(tlp) < 12:
        raise malf("TLP shorter than a 3DW header")
    fmt, typ = tlp[0] >> 5, tlp[0] & 0x1F
    if fmt not in (0b000, 0b010) or typ != 0:
        raise malf("Fmt/Type 0x%02X not a 3DW MRd/MWr" % tlp[0])
    td, ep = tlp[2] >> 7, (tlp[2] >> 6) & 1
    length = (((tlp[2] & 3) << 8) | tlp[3]) or 1024
    has_data = fmt == 0b010
    expect = 12 + (4 * length if has_data else 0) + (4 if td else 0)
    if len(tlp) != expect:
        raise malf("Length field %d DW / TD=%d needs %d bytes, got %d" % (length, td, expect, len(tlp)))
    first_be, last_be = tlp[7] & 0xF, tlp[7] >> 4
    if (length == 1) != (last_be == 0):
        raise malf("Last DW BE must be 0 iff Length == 1")
    if td:
        got = int.from_bytes(tlp[-4:], "little")
        calc = ecrc_of(tlp[:-4])
        if got != calc:
            raise LinkError("TL", "ECRC", PCI_ERR_UNC_ECRC, "ECRC 0x%08X != 0x%08X" % (got, calc))
    return {"kind": "MWr" if has_data else "MRd", "req_id": int.from_bytes(tlp[4:6], "big"),
            "tag": tlp[6], "addr": int.from_bytes(tlp[8:12], "big"), "length": length,
            "first_be": first_be, "last_be": last_be, "td": td, "ep": ep,
            "payload": bytes(tlp[12:12 + 4 * length]) if has_data else b""}


ACTION = {"FRAMING": "Receiver Error(cor): discard (Gen3+: LTSSM -> Recovery)",
          "BAD_TLP": "Bad TLP(cor): Nak, transmitter replays from replay buffer",
          "NULLIFIED": "discard silently: no Nak, no AER, seq not consumed",
          "ECRC": "ECRC Error(uncor): TLP dropped, AER header log",
          "MALF": "Malformed TLP(uncor): TLP dropped, AER header log"}


def receive(frame, gen):
    """주어진 glue: gen 1/2 → 심볼 목록, gen 3+ → 블록 목록. 결과 dict(ok, layer, code, aer, action, ...)."""
    try:
        r = unwrap_gen12(frame) if gen in (1, 2) else unwrap_gen3(frame)
        if r is None:
            raise NotImplementedError("unwrap returned None")
        if r["status"] == "NULLIFIED":
            return {"ok": False, "layer": "DLL", "code": "NULLIFIED", "aer": 0, "action": ACTION["NULLIFIED"]}
        t = check_tlp(r["tlp"])
        if t is None:
            raise NotImplementedError("check_tlp returned None")
        return {"ok": True, "layer": "-", "code": "OK", "aer": 0, "seq": r["seq"], "tlp": t,
                "action": "deliver to TL, schedule Ack(%d)" % r["seq"]}
    except LinkError as e:
        return {"ok": False, "layer": e.layer, "code": e.code, "aer": e.aer, "action": ACTION[e.code],
                "msg": str(e)}


def annotate_gen12(symbols):
    """Gen1/2 심볼마다 (계층, 필드): STP/END(PHY) · Seq 2B(DLL) · TLP(TL, tlp_field_names) · LCRC 4B(DLL)."""
    n = len(symbols)
    if n < 2 + 2 + 12 + 4:
        return []
    tlp = bytes(b for _, b in symbols[3:n - 5])
    end = {K_END: "END K29.7", K_EDB: "EDB K30.7 (nullified)"}.get(symbols[-1][1], "?")
    return ([("PHY", "STP K27.7"), ("DLL", "Seq rsvd|[11:8]"), ("DLL", "Seq[7:0]")]
            + [("TL", nm) for nm in tlp_field_names(tlp)]
            + [("DLL", "LCRC B%d" % k) for k in range(4)] + [("PHY", end)])


# ---------------------------------------------------------------- 주어진 코드: 표 출력
def annotate_gen3(stream):
    """Gen3 스트림 바이트마다 (계층, 필드). Seq 는 DLL 소유지만 PHY 토큰 안에 실려 간다."""
    tlp = bytes(stream[4:-4]) if len(stream) >= 20 else b""
    rows = [("PHY", "STP 1111b|Len[3:0]"), ("PHY", "FP|Len[10:4]"),
            ("PHY+DLL", "FCRC|Seq[11:8]"), ("PHY+DLL", "Seq[7:0]")]
    rows += [("TL", nm) for nm in tlp_field_names(tlp)]
    return rows + [("DLL", "LCRC B%d" % k) for k in range(4)]


def show():
    mrd = make_tlp("MRd", 0x0100, 0x05, 0x7FFE1000, length_dw=16)
    mwr = make_tlp("MWr", 0x0000, 0x00, 0xFE001000, payload=b"\x01\x00\x00\x00", ecrc=True)
    for title, tlp, seq in (("MRd 16DW (SSD 01:00.0 fetches a 64B SQ entry)", mrd, 0x123),
                            ("MWr 1DW + ECRC (host rings SQ0 tail doorbell)", mwr, 0x124)):
        dll = dll_wrap(tlp, seq)
        sy = phy_gen12_tlp(dll)
        print("== %s, seq=0x%03X ==" % (title, seq))
        print("-- Gen1/2: %d symbols x 10 = %d wire bits --" % (len(sy), wire_bits_gen12(sy)))
        print("off  sym    layer  field")
        for i, ((k, b), (lay, f)) in enumerate(zip(sy, annotate_gen12(sy))):
            print("%3d  %-5s  %-5s  %s" % (i, ("K:%02X" % b) if k else ("%02X" % b), lay, f))
        st = phy_gen3_tlp(dll)
        bl = blocks_130(st)
        print("-- Gen3: %d bytes in %d block(s) x 130 = %d wire bits (STP Length=%d DW) --"
              % (len(st), len(bl), wire_bits_gen3(bl), len(st) // 4))
        print("off  byte  layer    field")
        for i, (b, (lay, f)) in enumerate(zip(st, annotate_gen3(st))):
            print("%3d  %02X    %-7s  %s" % (i, b, lay, f))
        print("blocks: " + " | ".join("%s %s" % (format(s, "02b"), d.hex(" ").upper()) for s, d in bl))
        print()
    a = dllp_ack(0x123)
    print("== Ack DLLP seq=0x123: %s ==" % a.hex(" ").upper())
    g12 = phy_gen12_dllp(a)
    print("Gen1/2: %s  (%d symbols, %d wire bits)"
          % (" ".join(("K:%02X" % b) if k else "%02X" % b for k, b in g12), len(g12), wire_bits_gen12(g12)))
    print("Gen3  : %s  (%d bytes)" % (phy_gen3_dllp(a).hex(" ").upper(), len(phy_gen3_dllp(a))))
    print()


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def safe(fn, default=None):
    try:
        return fn()
    except Exception:  # noqa: BLE001
        return default


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


def polymod(value, poly):
    """GF(2) 다항식 나머지 (CRC-16 독립 교차 검증용)."""
    pl = poly.bit_length()
    while value.bit_length() >= pl:
        value ^= poly << (value.bit_length() - pl)
    return value


def crc16_ref(data):
    """crc16_dllp 의 독립 구현: init 0xFFFF = 앞 2바이트 반전, 그 뒤 M(x)·x^16 mod P, xorout."""
    m = bytes(b ^ 0xFF for b in data[:2]) + bytes(data[2:])
    return polymod(int.from_bytes(m, "big") << 16, 0x1100B) ^ 0xFFFF


def flip_gen12(sy, i, bit):
    """심볼 i 의 비트 하나 뒤집기. bit 0..7 = 데이터 바이트, bit 8 = K/D 구분 (10b 코드가 다른 종류로 깨짐)."""
    out = list(sy)
    k, b = out[i]
    out[i] = (not k, b) if bit == 8 else (k, b ^ (1 << bit))
    return out


def flip_gen3(bl, i, bit):
    """블록 i 의 130비트 중 하나 뒤집기. bit 0..1 = sync header, 2..129 = 데이터 비트."""
    out = list(bl)
    s, d = out[i]
    if bit < 2:
        out[i] = (s ^ (1 << bit), d)
    else:
        j = bit - 2
        dd = bytearray(d)
        dd[j // 8] ^= 1 << (j % 8)
        out[i] = (s, bytes(dd))
    return out


def run_tests():
    MRD = bytes.fromhex("00000010 010005FF 7FFE1000")
    MWR_HDR = bytes.fromhex("40008001 0000000F FE001000")
    doorbell = b"\x01\x00\x00\x00"

    print("-- Part A: Transaction Layer --")
    check("crc32_pcie(b'123456789') == 0xCBF43926 (check value)",
          lambda: crc32_pcie(b"123456789") == 0xCBF43926)
    check("crc32_pcie(b'') == 0", lambda: crc32_pcie(b"") == 0)

    def crc_vs_binascii():
        r = XorShift32(0x1B01)
        for n in range(200):
            buf = bytes(r.next() & 0xFF for _ in range(n % 67))
            if crc32_pcie(buf) != binascii.crc32(buf):
                return False
        return True
    check("crc32_pcie == binascii.crc32 on 200 random buffers", crc_vs_binascii)
    mrd = safe(lambda: make_tlp("MRd", 0x0100, 0x05, 0x7FFE1000, length_dw=16), b"")
    check("MRd 16DW bytes == 00 00 00 10 | 01 00 05 FF | 7F FE 10 00", lambda: mrd == MRD)
    check("MRd 1024 DW -> Length field 0 (0 means 1024)",
          lambda: make_tlp("MRd", 1, 2, 0x1000, length_dw=1024)[2:4] == b"\x00\x00")
    check("MRd 1 DW -> Last BE 0, First BE F (byte 7 = 0x0F)",
          lambda: make_tlp("MRd", 1, 2, 0x1000, length_dw=1)[7] == 0x0F)
    mwr = safe(lambda: make_tlp("MWr", 0, 0, 0xFE001000, payload=doorbell, ecrc=True), b"")
    check("MWr 1DW+ECRC header == 40 00 80 01 | 00 00 00 0F | FE 00 10 00", lambda: mwr[:12] == MWR_HDR)
    check("MWr 1DW+ECRC: payload at 12..15, ECRC(LE) at 16..19, total 20",
          lambda: len(mwr) == 20 and mwr[12:16] == doorbell
          and mwr[16:] == ecrc_of(mwr[:16]).to_bytes(4, "little"))
    check("MWr 4DW: Length 4, BE 0xFF, 28 bytes, TD=0",
          lambda: (lambda t: t[3] == 4 and t[7] == 0xFF and len(t) == 28 and t[2] == 0)(
              make_tlp("MWr", 0x0100, 1, 0x2000, payload=bytes(range(16)))))
    check("invalid args -> ValueError (unaligned, >=4GB, 5B, 20B, MRd+data, kind)",
          lambda: all(raises(ValueError, f) for f in (
              lambda: make_tlp("MRd", 0, 0, 0x1002),
              lambda: make_tlp("MRd", 0, 0, 1 << 32),
              lambda: make_tlp("MWr", 0, 0, 0x1000, payload=b"12345"),
              lambda: make_tlp("MWr", 0, 0, 0x1000, payload=bytes(20)),
              lambda: make_tlp("MRd", 0, 0, 0x1000, payload=b"abcd"),
              lambda: make_tlp("CplD", 0, 0, 0x1000))))

    def variant_bits():
        t = bytearray(mwr[:16])
        e0 = ecrc_of(bytes(t))
        t[2] |= 0x40                          # 스위치가 EP(poison) 를 켬
        e1 = ecrc_of(bytes(t))
        t[13] ^= 0x01                         # 데이터 1비트
        return e0 == e1 and ecrc_of(bytes(t)) != e0 and e0 != crc32_pcie(mwr[:16])
    check("ECRC ignores variant bits (EP) but not data; != plain CRC of bytes", variant_bits)
    check("tlp_field_names(MRd) = 12 header names, byte0 'DW0 Fmt|Type'",
          lambda: (lambda n: len(n) == 12 and n[0] == "DW0 Fmt|Type" and n[7] == "DW1 LastBE|FirstBE")(
              tlp_field_names(MRD)))
    check("tlp_field_names(MWr+ECRC) = 12 hdr + 4 data + 4 ECRC",
          lambda: (lambda n: len(n) == 20 and n[12] == "Data DW0 B0" and n[16] == "ECRC B0")(
              tlp_field_names(mwr)))

    print("-- Part B: Data Link + Physical --")
    dll = safe(lambda: dll_wrap(MRD, 0x123), b"")
    check("dll_wrap seq 0x123: 01 23 + TLP + LCRC(LE) over seq+TLP, 18 bytes",
          lambda: len(dll) == 18 and dll[:2] == b"\x01\x23" and dll[2:14] == MRD
          and dll[14:] == binascii.crc32(dll[:14]).to_bytes(4, "little"))
    check("dll_wrap seq 4096 -> ValueError", lambda: raises(ValueError, lambda: dll_wrap(MRD, 4096)))
    sy = safe(lambda: phy_gen12_tlp(dll), [])
    check("Gen1/2 MRd: STP + 18 D + END = 20 symbols = 200 wire bits",
          lambda: len(sy) == 20 and sy[0] == (True, K_STP) and sy[-1] == (True, K_END)
          and not any(k for k, _ in sy[1:-1]) and wire_bits_gen12(sy) == 200)
    check("annotate_gen12(MRd) layers = PHY, DLL x2, TL x12, DLL x4, PHY",
          lambda: [a for a, _ in annotate_gen12(sy)] == ["PHY"] + ["DLL"] * 2 + ["TL"] * 12 + ["DLL"] * 4 + ["PHY"])
    check("annotate_gen12 fields: [3] 'DW0 Fmt|Type', [15] 'LCRC B0', [19] END",
          lambda: (lambda a: a[3][1] == "DW0 Fmt|Type" and a[15][1] == "LCRC B0" and a[19][1].startswith("END"))(
              annotate_gen12(sy)))
    check("stp_token(5, 0x123) == 5F 00 F1 23 (model: FCRC(5)=0xF, FP=0)",
          lambda: stp_token(5, 0x123) == bytes.fromhex("5F00F123"))
    check("stp_token(7, 0x124) == 7F 80 91 24 (model: FCRC(7)=9, FP=1)",
          lambda: stp_token(7, 0x124) == bytes.fromhex("7F809124"))
    st = safe(lambda: phy_gen3_tlp(dll), b"")
    check("Gen3 MRd stream: token(Len=5, seq 0x123) + TLP + same LCRC = 20 bytes",
          lambda: len(st) == 20 and st[:4] == stp_token(5, 0x123) and st[4:16] == MRD and st[16:] == dll[14:])
    dllw = safe(lambda: dll_wrap(mwr, 0x124), b"")
    check("Gen3 MWr+ECRC: Length = 1+3+1+1+1 = 7 DW, 28 bytes",
          lambda: (lambda s: len(s) == 28 and ((s[0] >> 4) | ((s[1] & 0x7F) << 4)) == 7)(phy_gen3_tlp(dllw)))
    bl = safe(lambda: blocks_130(st), [])
    check("128b/130b MRd: 2 Data Blocks (sync 10b), 12 IDL pad, 260 wire bits",
          lambda: len(bl) == 2 and all(s == SYNC_DATA and len(d) == 16 for s, d in bl)
          and bl[1][1][4:] == bytes(12) and wire_bits_gen3(bl) == 260)
    check("blocks_130: 32B -> 2 blocks no pad, 0B -> 0 blocks",
          lambda: len(blocks_130(bytes(range(1, 33)))) == 2 and blocks_130(bytes(range(1, 33)))[1][1][-1] == 32
          and blocks_130(b"") == [])
    ack = safe(lambda: dllp_ack(0x123), b"")
    check("Ack DLLP seq 0x123 = 00 00 01 23 + CRC16, 6 bytes",
          lambda: len(ack) == 6 and ack[:4] == b"\x00\x00\x01\x23"
          and int.from_bytes(ack[4:], "big") == crc16_dllp(ack[:4]))

    def crc16_cross():
        r = XorShift32(0xD11F)
        for _ in range(300):
            buf = bytes(r.next() & 0xFF for _ in range(4))
            if crc16_dllp(buf) != crc16_ref(buf):
                return False
        return True
    check("crc16_dllp == GF(2) polynomial reference on 300 random DLLP bodies", crc16_cross)
    check("Ack framing: Gen1/2 SDP+6+END = 8 symbols (80 bits), Gen3 F0 AC + 6 = 8 bytes",
          lambda: (lambda g: len(g) == 8 and g[0] == (True, K_SDP) and g[-1] == (True, K_END)
                   and wire_bits_gen12(g) == 80)(phy_gen12_dllp(ack))
          and phy_gen3_dllp(ack) == b"\xF0\xAC" + ack)
    check("nullify: EDB instead of END, LCRC bytes inverted",
          lambda: (lambda n: n[-1] == (True, K_EDB) and bytes(b for _, b in n[-5:-1]) == bytes(x ^ 0xFF for x in dll[-4:]))(
              phy_gen12_tlp(dll, nullify=True)))

    print("-- Part C: receiver PHY -> DLL -> TL --")

    def rt(tlp, seq, gen):
        d = dll_wrap(tlp, seq)
        fr = phy_gen12_tlp(d) if gen < 3 else blocks_130(phy_gen3_tlp(d))
        return receive(fr, gen)
    check("Gen1/2 round trip MRd: ok, seq 0x123, MRd len 16 addr 0x7FFE1000 req 0x0100",
          lambda: (lambda r: r["ok"] and r["seq"] == 0x123 and r["tlp"]["kind"] == "MRd" and r["tlp"]["length"] == 16
                   and r["tlp"]["addr"] == 0x7FFE1000 and r["tlp"]["req_id"] == 0x0100)(rt(MRD, 0x123, 1)))
    check("Gen3 round trip MWr+ECRC: ok, payload == doorbell, td=1",
          lambda: (lambda r: r["ok"] and r["seq"] == 0x124 and r["tlp"]["payload"] == doorbell and r["tlp"]["td"] == 1)(
              rt(mwr, 0x124, 3)))

    def random_rt():
        r = XorShift32(0xB01B01)
        for _ in range(150):
            ec = bool(r.next() & 1)
            if r.next() & 1:
                n = 1 + r.next() % 4
                pl = bytes(r.next() & 0xFF for _ in range(4 * n))
                t = make_tlp("MWr", r.next() & 0xFFFF, r.next() & 0xFF, (r.next() & 0xFFFFFFFC), payload=pl, ecrc=ec)
            else:
                n = 1 + r.next() % 1024
                t = make_tlp("MRd", r.next() & 0xFFFF, r.next() & 0xFF, (r.next() & 0xFFFFFFFC), length_dw=n, ecrc=ec)
            seq = r.next() & 0xFFF
            for gen in (2, 4):
                got = rt(t, seq, gen)
                if not got["ok"] or got["seq"] != seq or got["tlp"]["length"] != n:
                    return False
        return True
    check("150 random MRd/MWr (random ECRC, seq) round trip on Gen2 and Gen4", random_rt)

    def sweep12():
        cnt = {}
        for i in range(len(sy)):
            for bit in range(9):
                c = receive(flip_gen12(sy, i, bit), 1)["code"]
                cnt[c] = cnt.get(c, 0) + 1
        return cnt
    c12 = safe(sweep12, {})
    check("Gen1/2 all 180 single flips detected: FRAMING 36, BAD_TLP 144", lambda: c12 == {"FRAMING": 36, "BAD_TLP": 144})

    def sweep3():
        cnt = {}
        for i in range(len(bl)):
            for bit in range(130):
                c = receive(flip_gen3(bl, i, bit), 3)["code"]
                cnt[c] = cnt.get(c, 0) + 1
        return cnt
    c3 = safe(sweep3, {})
    check("Gen3 all 260 single flips detected: FRAMING 120, BAD_TLP 140", lambda: c3 == {"FRAMING": 120, "BAD_TLP": 140})

    def switch_corrupts(ecrc):
        t = bytearray(make_tlp("MWr", 0, 0, 0xFE001000, payload=doorbell, ecrc=ecrc))
        t[12] ^= 0x04                         # 스위치 내부 SRAM 비트 오류: tail 1 → 5
        return receive(phy_gen12_tlp(dll_wrap(bytes(t), 7)), 2)   # 다음 링크로 새 LCRC 를 붙여 전달
    check("switch-internal corruption + fresh LCRC, ECRC on -> TL ECRC, aer PCI_ERR_UNC_ECRC",
          lambda: (lambda r: r["code"] == "ECRC" and r["layer"] == "TL" and r["aer"] == 0x00080000)(switch_corrupts(True)))
    check("same corruption, ECRC off -> accepted with WRONG data (silent corruption)",
          lambda: (lambda r: r["ok"] and r["tlp"]["payload"] == b"\x05\x00\x00\x00")(switch_corrupts(False)))

    def malformed(mod):
        t = bytearray(make_tlp("MWr", 0, 0, 0x3000, payload=doorbell))
        mod(t)
        return receive(phy_gen12_tlp(dll_wrap(bytes(t), 9)), 1)
    check("MALF: Length=2 but 1 DW of data", lambda: malformed(lambda t: t.__setitem__(3, 2))["code"] == "MALF")
    check("MALF: TD=1 but no ECRC bytes",
          lambda: (lambda r: r["code"] == "MALF" and r["aer"] == 0x00040000)(malformed(lambda t: t.__setitem__(2, 0x80))))
    check("MALF: 1 DW request with Last BE != 0", lambda: malformed(lambda t: t.__setitem__(7, 0xFF))["code"] == "MALF")
    check("nullified (EDB + inverted LCRC) -> NULLIFIED, no AER bit",
          lambda: (lambda r: r["code"] == "NULLIFIED" and r["aer"] == 0)(receive(phy_gen12_tlp(dll, nullify=True), 1)))
    check("truncated/garbage: no END, empty, Gen3 Length past stream -> FRAMING",
          lambda: receive(sy[:-1], 1)["code"] == "FRAMING" and receive([], 1)["code"] == "FRAMING"
          and receive(blocks_130(stp_token(9, 1) + st[4:]), 3)["code"] == "FRAMING")
    check("AER bits match pci_regs.h: RCVR 0x1, BAD_TLP 0x40, MALF 0x40000, ECRC 0x80000",
          lambda: receive(sy[:-1], 1)["aer"] == 0x1 and receive(flip_gen12(sy, 5, 0), 1)["aer"] == 0x40
          and PCI_ERR_UNC_MALF_TLP == 0x00040000 and PCI_ERR_UNC_ECRC == 0x00080000)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    if "--show" in sys.argv[1:]:
        show()
    sys.exit(0 if run_tests() else 1)
