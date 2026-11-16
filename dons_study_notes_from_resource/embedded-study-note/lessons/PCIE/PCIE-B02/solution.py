#!/usr/bin/env python3
"""PCIE-B02 링크 속도·폭과 실효 대역폭 계산: Gen1–Gen6

한 방향(per direction) 처리량을 단계별로 깎아 내려가는 계산기:
  Part A  GT/s × 레인 수 → raw Gb/s → 인코딩(8b/10b, 128b/130b, Gen6 1b/1b+FLIT) 후 Gb/s → GB/s, GiB/s
  Part B  TLP 하나당 오버헤드(프레이밍 + Seq + 헤더 12/16B + LCRC (+ECRC)) × MPS, DLLP 비율 가정,
          MPS 경계 분할, 읽기 완료(CplD) 경로(MRRS / RCB / 완료 크기)
  Part C  "Gen4 x4 NVMe 이론 최대 순차 읽기는?" 답 + 측정값으로 다운트레이닝 후보 찾기

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --tables   # 테스트 + 레슨 md 에 붙인 표 출력

모델의 가정(이 레슨의 정의 — 실리콘 사양이 아님):
  * 모든 수치는 한 방향. PCIe 는 full duplex 라 반대 방향도 같은 대역폭이 따로 있다.
  * Gen1/2 TLP: STP(1) + Seq(2) + 헤더 + 데이터 (+ECRC 4) + LCRC(4) + END(1)
  * Gen3–5 TLP: STP 토큰(4, Length·FCRC·FP·Seq 포함) + 헤더 + 데이터 (+ECRC 4) + LCRC(4)
  * DLLP 한 개 = 선로 8바이트 (Gen1/2: SDP+6+END, Gen3–5: SDP 토큰 2 + 6). TLP 당 몇 개인지는 인자(가정).
  * Gen6 = FLIT 모드: 256B flit = TLP 236 + DLP 6 + CRC 8 + FEC 6 (스펙 확인 권장).
    TLP 마다의 프레이밍/Seq/LCRC 없음, DLLP 는 DLP 바이트로 다님. 헤더 크기는 12/16B 로 근사.
  * SKP ordered set, 레인 정렬 패딩, 링크 재훈련, 흐름 제어 정지는 무시 (추가 연습 3 에서 SKP 만 반영).
Python 3 표준 라이브러리만 쓴다 (fractions, sys). 정확한 분수로 계산하고 출력할 때만 float 로 바꾼다.
"""
import sys
from fractions import Fraction as F

GT_S = {1: F(5, 2), 2: F(5), 3: F(8), 4: F(16), 5: F(32), 6: F(64)}   # 레인당 GT/s
WIDTHS = (1, 2, 4, 8, 16)                    # 이 레슨이 다루는 폭 (x12/x32 는 생략)
SIZES = (128, 256, 512, 1024, 2048, 4096)    # MPS/MRRS 인코딩 가능한 값 (DevCtl 3비트 000..101)
HDR_OK = (12, 16)                            # 3DW / 4DW 헤더
DLLP_WIRE = 8                                # DLLP 선로 바이트 (모델)
CPL_HDR = 12                                 # Completion 헤더는 항상 3DW
MRD_HDR = 16                                 # NVMe 가 호스트 메모리(>4GB 가능)를 읽는 MRd 는 4DW 로 가정
FLIT_BYTES, FLIT_TLP, FLIT_DLP, FLIT_CRC, FLIT_FEC = 256, 236, 6, 8, 6
GiB = 2 ** 30


def check_gen(gen):
    if gen not in GT_S:
        raise ValueError("gen must be 1..6, got %r" % (gen,))


def check_width(width):
    if width not in WIDTHS:
        raise ValueError("width must be one of %s, got %r" % (WIDTHS, width))


def check_size(name, v):
    if v not in SIZES:
        raise ValueError("%s must be one of %s, got %r" % (name, SIZES, v))


def fmt_gbs(x):
    """GB/s 분수 → '7.877 GB/s (7.336 GiB/s)'"""
    return "%.3f GB/s (%.3f GiB/s)" % (float(x), float(x * 10 ** 9 / GiB))


# ---------------------------------------------------------------- Part A: 링크 속도와 인코딩
def encoding(gen):
    """세대 → (인코딩 이름, 효율 Fraction).
    Gen1/2 '8b/10b' 8/10, Gen3–5 '128b/130b' 128/130, Gen6 '1b/1b PAM4 FLIT' 1 (FLIT 손실은 link_bw 에서)"""
    check_gen(gen)
    if gen <= 2:
        return "8b/10b", F(8, 10)
    if gen <= 5:
        return "128b/130b", F(128, 130)
    return "1b/1b PAM4 FLIT", F(1)


def link_bw(gen, width):
    """한 방향 링크 대역폭. 모든 값은 Fraction.
    gt_s      레인당 GT/s
    raw_gbps  GT/s × 레인 (선로 비트율, 인코딩 포함)
    enc_gbps  인코딩 제거 후 Gb/s
    GBps      enc_gbps / 8  (1 GB = 10^9 B)
    GiBps     GBps × 10^9 / 2^30
    tlp_GBps  TLP 바이트가 쓸 수 있는 속도 (Gen6: × 236/256, Gen1–5: GBps 와 같음 — 프레이밍은 TLP 별로 계산)"""
    check_gen(gen)
    check_width(width)
    gt = GT_S[gen]
    raw = gt * width
    enc = raw * encoding(gen)[1]
    gbps = enc / 8
    tlp = gbps * F(FLIT_TLP, FLIT_BYTES) if gen == 6 else gbps
    return {"gt_s": gt, "raw_gbps": raw, "enc_gbps": enc, "GBps": gbps,
            "GiBps": gbps * 10 ** 9 / GiB, "tlp_GBps": tlp}


# ---------------------------------------------------------------- Part B: TLP/DLLP 오버헤드
def tlp_overhead(gen, hdr=16, ecrc=False):
    """TLP 하나에 데이터 말고 붙는 선로 바이트 수.
    Gen1/2: STP 1 + Seq 2 + hdr + LCRC 4 + END 1  (= hdr + 8)
    Gen3–5: STP 토큰 4(Seq 포함) + hdr + LCRC 4   (= hdr + 8)
    Gen6  : hdr 만 (프레이밍·Seq·LCRC 없음, flit 손실은 link_bw 의 236/256)
    ECRC 가 있으면 +4. hdr 는 12 또는 16 이 아니면 ValueError."""
    check_gen(gen)
    if hdr not in HDR_OK:
        raise ValueError("hdr must be 12 or 16 bytes, got %r" % (hdr,))
    e = 4 if ecrc else 0
    if gen <= 2:
        return 1 + 2 + hdr + 4 + 1 + e
    if gen <= 5:
        return 4 + hdr + 4 + e
    return hdr + e


def tlp_efficiency(gen, payload, hdr=16, ecrc=False, dllp_per_tlp=F(0)):
    """payload 바이트짜리 TLP 를 연속으로 보낼 때 (데이터 바이트) / (선로 바이트).
    DLLP 는 TLP 당 dllp_per_tlp 개 × 8 바이트가 같은 방향에 섞인다고 가정 (Gen6 은 DLP 에 실려 0).
    payload < 1 이면 ValueError."""
    if payload < 1:
        raise ValueError("payload must be >= 1 byte")
    ovh = tlp_overhead(gen, hdr, ecrc)
    dllp = 0 if gen == 6 else F(dllp_per_tlp) * DLLP_WIRE
    return F(payload) / (payload + ovh + dllp)


def effective_write(gen, width, mps, hdr=16, ecrc=False, dllp_per_tlp=F(1, 4)):
    """긴 MWr 스트림(모든 TLP 가 MPS 꽉 참)의 실효 처리량 GB/s (Fraction).
    MPS 가 128..4096 의 2의 거듭제곱이 아니면 ValueError."""
    check_size("mps", mps)
    return link_bw(gen, width)["tlp_GBps"] * tlp_efficiency(gen, mps, hdr, ecrc, dllp_per_tlp)


def write_tlps(addr, nbytes, mps):
    """[addr, addr+nbytes) 를 MWr TLP 들로 자른 payload 크기 목록.
    모델: TLP 는 MPS 정렬 경계를 넘지 않는다 (흔한 DMA 엔진 동작. 스펙의 필수 조건은 4KB 경계 — PCIE-B10).
    MPS ≤ 4096 이므로 4KB 경계도 자동으로 지켜진다. nbytes 0 → []."""
    check_size("mps", mps)
    if addr < 0 or nbytes < 0:
        raise ValueError("addr/nbytes must be >= 0")
    out = []
    end = addr + nbytes
    a = addr
    while a < end:
        nxt = min((a // mps + 1) * mps, end)
        out.append(nxt - a)
        a = nxt
    return out


def effective_read(gen, width, mrrs, cpl_bytes, mps, rcb=64, dllp_per_tlp=F(1, 4)):
    """요청자가 MRRS 바이트짜리 MRd 를 계속 내고, 완료자가 cpl_bytes 짜리 CplD 들로 돌려줄 때.
    반환 dict:
      GBps            완료 방향(데이터 방향) 실효 처리량 (Fraction)
      cpl_per_req     MRd 하나당 CplD 개수 = ceil(mrrs / cpl_bytes)
      req_bytes       MRd 하나의 선로 바이트 (반대 방향, 4DW 헤더 + 오버헤드 + DLLP 몫)
      req_link_share  그 MRd 들이 반대 방향 링크를 차지하는 비율
    규칙: rcb ∈ {64, 128}; cpl_bytes 는 rcb 의 배수, ≤ MPS, ≤ MRRS. 아니면 ValueError.
    (정렬된 시작 주소 가정: 완료는 RCB 경계에서만 잘린다)"""
    check_size("mrrs", mrrs)
    check_size("mps", mps)
    if rcb not in (64, 128):
        raise ValueError("rcb must be 64 or 128")
    if cpl_bytes < rcb or cpl_bytes % rcb or cpl_bytes > mps or cpl_bytes > mrrs:
        raise ValueError("cpl_bytes %r: need multiple of RCB %d, <= MPS %d, <= MRRS %d"
                         % (cpl_bytes, rcb, mps, mrrs))
    n = -(-mrrs // cpl_bytes)
    d = 0 if gen == 6 else F(dllp_per_tlp) * DLLP_WIRE
    wire = mrrs + n * (tlp_overhead(gen, CPL_HDR) + d)
    lb = link_bw(gen, width)["tlp_GBps"]
    gbps = lb * F(mrrs) / wire
    req = tlp_overhead(gen, MRD_HDR) + d
    share = (gbps / mrrs) * req / lb
    return {"GBps": gbps, "cpl_per_req": n, "req_bytes": req, "req_link_share": share}


# ---------------------------------------------------------------- Part C: 면접 질문과 브링업 진단
def nvme_seq(gen, width, op, mps=256, mrrs=512, cpl_bytes=None, hdr=16, rcb=64, dllp_per_tlp=F(1, 4)):
    """NVMe 순차 I/O 의 PCIe 상한.
    op='read'  : 호스트가 읽는다 = SSD 가 호스트 메모리에 MWr (upstream) → MPS 가 지배
    op='write' : 호스트가 쓴다   = SSD 가 MRd, 루트 컴플렉스가 CplD (downstream) → MRRS·완료 크기가 지배
                 cpl_bytes 기본값 = min(MPS, MRRS)
    반환 {'path': 'MWr upstream' | 'CplD downstream', 'GBps': Fraction, 'link_GBps': Fraction}"""
    lk = link_bw(gen, width)["GBps"]
    if op == "read":
        return {"path": "MWr upstream", "link_GBps": lk,
                "GBps": effective_write(gen, width, mps, hdr, False, dllp_per_tlp)}
    if op == "write":
        c = min(mps, mrrs) if cpl_bytes is None else cpl_bytes
        r = effective_read(gen, width, mrrs, c, mps, rcb, dllp_per_tlp)
        return {"path": "CplD downstream", "link_GBps": lk, "GBps": r["GBps"]}
    raise ValueError("op must be 'read' or 'write'")


def link_suspects(measured_GBps, cap_gen, cap_width, mps=256, hdr=16, lo=F(85, 100)):
    """측정한 순차 읽기 처리량이 '어떤 (gen, width) 상한에 붙어 있나' 후보 목록.
    후보 = gen ≤ cap_gen, width ≤ cap_width, (gen, width) ≠ cap 이고
           lo × ceiling ≤ measured ≤ ceiling  (ceiling = nvme_seq(...,'read') 상한).
    (gen, width) 오름차순 정렬. 빈 목록 = 다운그레이드로 설명되는 정체가 아님."""
    check_gen(cap_gen)
    check_width(cap_width)
    m = F(measured_GBps).limit_denominator(10 ** 9) if isinstance(measured_GBps, float) else F(measured_GBps)
    out = []
    for g in range(1, cap_gen + 1):
        for w in WIDTHS:
            if w > cap_width or (g, w) == (cap_gen, cap_width):
                continue
            c = nvme_seq(g, w, "read", mps=mps, hdr=hdr)["GBps"]
            if lo * c <= m <= c:
                out.append((g, w))
    return sorted(out)


# ---------------------------------------------------------------- 표 출력 (--tables)
def print_tables():
    print("== 표 1. 인코딩 후 한 방향 GB/s (Gen6 은 1b/1b; 마지막 줄 = FLIT 후 TLP 바이트율) ==")
    print("%-6s %-9s %-16s" % ("gen", "GT/s", "encoding") + "".join("%10s" % ("x%d" % w) for w in WIDTHS))
    for g in range(1, 7):
        cells = []
        for w in WIDTHS:
            b = link_bw(g, w)
            cells.append("%10.3f" % float(b["GBps"]))
        print("%-6s %-9s %-16s" % ("Gen%d" % g, "%g" % float(GT_S[g]), encoding(g)[0]) + "".join(cells))
    print("       Gen6 FLIT 후 TLP 바이트율:   " + "".join("%10.3f" % float(link_bw(6, w)["tlp_GBps"]) for w in WIDTHS))
    print()
    print("== 표 2. TLP 효율 (Gen1–5 공통 오버헤드 hdr+8; DLLP 0 / TLP당 1/4개) ==")
    print("%-6s %-6s %12s %12s" % ("MPS", "hdr", "DLLP 0", "DLLP 1/4"))
    for m in (128, 256, 512):
        for h in HDR_OK:
            print("%-6d %-6s %11.2f%% %11.2f%%" % (m, "%dB" % h, 100 * float(tlp_efficiency(4, m, h)),
                                                   100 * float(tlp_efficiency(4, m, h, False, F(1, 4)))))
    print()
    print("== 표 3. x4 순차 읽기 상한 (MWr upstream, 4DW, DLLP 1/4) GB/s ==")
    print("%-6s %10s" % ("gen", "link") + "".join("%10s" % ("MPS%d" % m) for m in (128, 256, 512)))
    for g in range(1, 7):
        print("%-6s %10.3f" % ("Gen%d" % g, float(link_bw(g, 4)["GBps"]))
              + "".join("%10.3f" % float(nvme_seq(g, 4, "read", mps=m)["GBps"]) for m in (128, 256, 512)))
    print()
    print("== 표 4. Gen4 x4 순차 쓰기 상한 (CplD downstream, MRRS 512, MPS 256, DLLP 1/4) ==")
    print("%-10s %12s %12s %12s" % ("CplD 크기", "CplD/MRd", "GB/s", "MRd 몫"))
    for c in (64, 128, 256):
        r = effective_read(4, 4, 512, c, 256)
        print("%-10s %12d %12.3f %11.2f%%" % ("%dB" % c, r["cpl_per_req"], float(r["GBps"]),
                                              100 * float(r["req_link_share"])))
    print()
    print("== 표 5. 측정 순차 읽기 → 다운트레이닝 후보 (cap Gen4 x4, MPS 256) ==")
    for meas in (6.9, 3.45, 1.7, 0.8):
        s = link_suspects(meas, 4, 4)
        print("%5.2f GB/s → %s" % (meas, ", ".join("Gen%d x%d" % gw for gw in s) if s else "(없음)"))


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


def near(x, want, tol=0.0005):
    return x is not None and abs(float(x) - want) <= tol


def xorshift32(s):
    s ^= (s << 13) & 0xFFFFFFFF
    s ^= s >> 17
    s ^= (s << 5) & 0xFFFFFFFF
    return s & 0xFFFFFFFF


def run_tests():
    print("-- Part A: GT/s → 인코딩 → GB/s --")
    check("encoding(1) == ('8b/10b', 8/10)", lambda: encoding(1) == ("8b/10b", F(8, 10)))
    check("encoding(4) == ('128b/130b', 128/130)", lambda: encoding(4) == ("128b/130b", F(128, 130)))
    check("encoding(6) efficiency == 1 (PAM4 1b/1b)", lambda: encoding(6)[1] == 1)
    check("Gen1 x1 == 0.25 GB/s exactly (250 MB/s)", lambda: link_bw(1, 1)["GBps"] == F(1, 4))
    check("Gen2 x1: raw 5 Gb/s, after 8b/10b 4 Gb/s",
          lambda: link_bw(2, 1)["raw_gbps"] == 5 and link_bw(2, 1)["enc_gbps"] == 4)
    check("Gen3 x1 == 128/130 GB/s ≈ 985 MB/s",
          lambda: link_bw(3, 1)["GBps"] == F(64, 65) and round(float(link_bw(3, 1)["GBps"]) * 1000) == 985)
    check("Gen4 x4 == 512/65 ≈ 7.877 GB/s", lambda: link_bw(4, 4)["GBps"] == F(512, 65)
          and near(link_bw(4, 4)["GBps"], 7.877))
    check("Gen4 x4 ≈ 7.336 GiB/s (GB ≠ GiB)", lambda: near(link_bw(4, 4)["GiBps"], 7.336))
    check("Gen5 x16 ≈ 63.015 GB/s", lambda: near(link_bw(5, 16)["GBps"], 63.015))
    check("Gen6 x16: 128 GB/s, FLIT TLP bytes 118 GB/s",
          lambda: link_bw(6, 16)["GBps"] == 128 and link_bw(6, 16)["tlp_GBps"] == 118)

    def all_combos():
        for g in range(1, 7):
            for w in WIDTHS:
                b = link_bw(g, w)
                if b["GBps"] != link_bw(g, 1)["GBps"] * w:         # 폭은 정확히 선형
                    return False
                if b["raw_gbps"] != GT_S[g] * w or b["GBps"] * 8 != b["enc_gbps"]:
                    return False
        return True
    check("30 combos: width linear, GBps*8 == enc_gbps", all_combos)
    check("Gen4/Gen3 == Gen5/Gen4 == 2 exactly; Gen3/Gen2 == 128/65 ≈ 1.97 (not 2)",
          lambda: link_bw(4, 1)["GBps"] / link_bw(3, 1)["GBps"] == 2
          and link_bw(5, 1)["GBps"] / link_bw(4, 1)["GBps"] == 2
          and link_bw(3, 1)["GBps"] / link_bw(2, 1)["GBps"] == F(128, 65))
    check("Gen4 x2 == Gen3 x4 (same encoded GB/s)", lambda: link_bw(4, 2)["GBps"] == link_bw(3, 4)["GBps"])
    check("bad gen/width -> ValueError", lambda: raises(ValueError, lambda: link_bw(7, 4))
          and raises(ValueError, lambda: link_bw(0, 4)) and raises(ValueError, lambda: link_bw(4, 3))
          and raises(ValueError, lambda: link_bw(4, 12)))

    print("-- Part B: TLP/DLLP 오버헤드, MPS, 읽기 완료 --")
    check("tlp_overhead Gen1 hdr16 == 24, Gen2 hdr12 == 20",
          lambda: tlp_overhead(1, 16) == 24 and tlp_overhead(2, 12) == 20)
    check("tlp_overhead Gen3 hdr16 == 24, Gen5 hdr16+ECRC == 28",
          lambda: tlp_overhead(3, 16) == 24 and tlp_overhead(5, 16, True) == 28)
    check("tlp_overhead Gen6 hdr16 == 16 (no framing/seq/LCRC in FLIT)",
          lambda: tlp_overhead(6, 16) == 16 and tlp_overhead(6, 12, True) == 16)
    check("hdr 20 -> ValueError", lambda: raises(ValueError, lambda: tlp_overhead(4, 20)))
    check("efficiency MPS256 4DW == 256/280; +DLLP 1/4 == 256/282",
          lambda: tlp_efficiency(4, 256, 16) == F(256, 280)
          and tlp_efficiency(4, 256, 16, False, F(1, 4)) == F(256, 282))
    check("payload 0 -> ValueError", lambda: raises(ValueError, lambda: tlp_efficiency(4, 0)))
    check("Gen4 x4 write MPS128/256/512 ≈ 6.547/7.151/7.496 GB/s",
          lambda: near(effective_write(4, 4, 128), 6.547) and near(effective_write(4, 4, 256), 7.151)
          and near(effective_write(4, 4, 512), 7.496))
    check("Gen4 x4 write MPS256 == 512/65 * 256/282 exactly",
          lambda: effective_write(4, 4, 256) == F(512, 65) * F(256, 282))
    check("Gen6 x4 write MPS256 == 29.5 * 256/272 ≈ 27.765",
          lambda: effective_write(6, 4, 256) == F(59, 2) * F(256, 272) and near(effective_write(6, 4, 256), 27.765))
    check("bad MPS (100, 8192, 0) -> ValueError",
          lambda: all(raises(ValueError, lambda m=m: effective_write(4, 4, m)) for m in (100, 8192, 0)))
    check("write_tlps(0x1000, 4096, 256) == 16 x 256", lambda: write_tlps(0x1000, 4096, 256) == [256] * 16)
    check("write_tlps(0x1010, 512, 256) == [240, 256, 16]", lambda: write_tlps(0x1010, 512, 256) == [240, 256, 16])
    check("write_tlps(0x10FE, 4, 256) == [2, 2]; nbytes 0 == []",
          lambda: write_tlps(0x10FE, 4, 256) == [2, 2] and write_tlps(0x2000, 0, 128) == [])

    def random_split():
        s = 0x2C0FFEE
        for _ in range(2000):
            s = xorshift32(s)
            mps = SIZES[s % 6]
            s = xorshift32(s)
            addr = s % (1 << 20)
            s = xorshift32(s)
            n = s % 20000
            parts = write_tlps(addr, n, mps)
            if sum(parts) != n or any(p < 1 or p > mps for p in parts):
                return False
            a = addr
            for p in parts:                    # 어떤 TLP 도 MPS 경계를 넘지 않는다
                if a // mps != (a + p - 1) // mps:
                    return False
                a += p
            if len(parts) != ((addr + n - 1) // mps - addr // mps + 1 if n else 0):
                return False
        return True
    check("2000 random splits: sum, ≤ MPS, no boundary crossing, minimal count", random_split)
    check("read Gen4 x4 MRRS512 CplD256: 2 CplD/MRd, ≈ 7.254 GB/s",
          lambda: effective_read(4, 4, 512, 256, 256)["cpl_per_req"] == 2
          and near(effective_read(4, 4, 512, 256, 256)["GBps"], 7.254))
    check("read CplD 64B: 8 CplD/MRd, ≈ 5.862 GB/s",
          lambda: effective_read(4, 4, 512, 64, 256)["cpl_per_req"] == 8
          and near(effective_read(4, 4, 512, 64, 256)["GBps"], 5.862))
    check("read MRd on reverse link: 26B each (24 + DLLP share), ≈ 4.68% of it",
          lambda: effective_read(4, 4, 512, 256, 256)["req_bytes"] == 26
          and near(effective_read(4, 4, 512, 256, 256)["req_link_share"], 0.0468, 0.0005))
    check("CplD > MPS / not RCB multiple / RCB 32 -> ValueError",
          lambda: raises(ValueError, lambda: effective_read(4, 4, 512, 512, 256))
          and raises(ValueError, lambda: effective_read(4, 4, 512, 96, 256))
          and raises(ValueError, lambda: effective_read(4, 4, 512, 64, 256, rcb=32)))
    check("CplD 64 (RCB) read slower than MWr MPS256 write on same link",
          lambda: effective_read(4, 4, 512, 64, 256)["GBps"] < effective_write(4, 4, 256))

    print("-- Part C: NVMe 질문 / 다운트레이닝 진단 --")
    check("Gen4 x4 NVMe seq read: MWr upstream, link 7.877, ≈ 7.151 GB/s @MPS256",
          lambda: nvme_seq(4, 4, "read")["path"] == "MWr upstream"
          and near(nvme_seq(4, 4, "read")["link_GBps"], 7.877) and near(nvme_seq(4, 4, "read")["GBps"], 7.151))
    check("Gen4 x4 NVMe seq read @MPS512 ≈ 7.496 GB/s", lambda: near(nvme_seq(4, 4, "read", mps=512)["GBps"], 7.496))
    check("Gen4 x4 NVMe seq write: CplD downstream, CplD64 ≈ 5.862 / default(256) ≈ 7.254",
          lambda: nvme_seq(4, 4, "write")["path"] == "CplD downstream"
          and near(nvme_seq(4, 4, "write", cpl_bytes=64)["GBps"], 5.862)
          and near(nvme_seq(4, 4, "write")["GBps"], 7.254))
    check("Gen4 x2 seq read == Gen3 x4 seq read (throughput can't tell them apart)",
          lambda: nvme_seq(4, 2, "read")["GBps"] == nvme_seq(3, 4, "read")["GBps"])
    check("op 'trim' -> ValueError", lambda: raises(ValueError, lambda: nvme_seq(4, 4, "trim")))
    check("suspects(3.45 GB/s, cap Gen4 x4) == [Gen3 x4, Gen4 x2]",
          lambda: link_suspects(3.45, 4, 4) == [(3, 4), (4, 2)])
    check("suspects(6.9) == [] (near full ceiling, no downgrade)", lambda: link_suspects(6.9, 4, 4) == [])
    check("suspects(1.7) == [Gen2 x4, Gen3 x2, Gen4 x1]", lambda: link_suspects(1.7, 4, 4) == [(2, 4), (3, 2), (4, 1)])

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) > 1 and argv[1] not in ("--tables",):
        print("usage: %s [--tables]" % argv[0])
        return 2
    if len(argv) > 1:
        try:
            print_tables()
        except Exception as e:  # noqa: BLE001 — starter 의 미구현 함수로 표가 안 나와도 테스트는 돈다
            print("(표 생략: %s: %s)" % (type(e).__name__, e))
        print()
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
