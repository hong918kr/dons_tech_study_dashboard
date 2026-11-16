#!/usr/bin/env python3
"""PCIE-B02 추가 연습 — 대역폭 모델을 실전 질문에 붙이기

  연습 1  kernel_mbps / available_bw / kernel_msg : 리눅스 dmesg "available PCIe bandwidth" 정수 계산과 병목 링크
  연습 2  read_bw_tag_limited / min_tags          : outstanding read 개수(Tag) × MRRS / RTT — Little's Law 상한
  연습 3  skp_factor                              : SKP ordered set 이 깎는 몫 (간격은 스펙 확인 권장)
  연습 4  nvme_4k_io                              : 4KB 랜덤 읽기 1건이 양방향에 쓰는 바이트와 PCIe 쪽 IOPS 상한

  python3 -I practice.py      # "N/M passed", 실패 시 exit 1
Python 3 표준 라이브러리만 쓴다.
"""
import sys
from fractions import Fraction as F

# ---- 본문(solution.py)의 모델 축약판 (레슨 파일끼리 import 하지 않는다) ----
GT_S = {1: F(5, 2), 2: F(5), 3: F(8), 4: F(16), 5: F(32), 6: F(64)}
WIDTHS = (1, 2, 4, 8, 16)


def enc_eff(gen):
    return F(8, 10) if gen <= 2 else (F(128, 130) if gen <= 5 else F(1))


def link_GBps(gen, width):
    """인코딩 후 한 방향 GB/s (Gen6 은 FLIT 손실 236/256 를 뺀 TLP 바이트율)"""
    if gen not in GT_S or width not in WIDTHS:
        raise ValueError("bad gen/width")
    g = GT_S[gen] * width * enc_eff(gen) / 8
    return g * F(236, 256) if gen == 6 else g


def tlp_wire(gen, payload, hdr=16):
    """payload 바이트 TLP 하나의 선로 바이트 (Gen1–5: hdr + 8, Gen6: hdr)"""
    return payload + hdr + (0 if gen == 6 else 8)


# ---------------------------------------------------------------- 연습 1
# Linux drivers/pci/pci.h 의 PCIE_SPEED2MBS_ENC 와 같은 정수 산술 (기억 기반 — 소스 확인 권장):
#   2.5GT 2500*8/10, 5GT 5000*8/10, 8GT 8000*128/130, 16GT 16000*128/130, 32GT 32000*128/130, 64GT 64000*1/1
KMBPS = {1: 2500 * 8 // 10, 2: 5000 * 8 // 10, 3: 8000 * 128 // 130,
         4: 16000 * 128 // 130, 5: 32000 * 128 // 130, 6: 64000 * 1 // 1}
SPEED_STR = {1: "2.5 GT/s", 2: "5.0 GT/s", 3: "8.0 GT/s", 4: "16.0 GT/s", 5: "32.0 GT/s", 6: "64.0 GT/s"}


def kernel_mbps(gen, width):
    """레인당 정수 Mb/s(내림) × width"""
    if gen not in KMBPS or width not in WIDTHS:
        raise ValueError("bad gen/width")
    return KMBPS[gen] * width


def available_bw(path):
    """path = [(gen, width), ...] 장치 쪽 링크부터 루트 쪽으로. → (최소 Mb/s, 그 링크의 index).
    값이 같으면 장치에 가까운 쪽(작은 index)을 병목으로 본다. 빈 path → ValueError."""
    if not path:
        raise ValueError("empty path")
    best = None
    for i, (g, w) in enumerate(path):
        v = kernel_mbps(g, w)
        if best is None or v < best[0]:
            best = (v, i)
    return best


def kernel_msg(path):
    """'63.008 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x8 link at index 1'
    (실제 dmesg 는 'at 0000:00:01.0 (capable of ...)' 처럼 BDF 와 장치 능력을 붙인다 — 형식 근사)"""
    v, i = available_bw(path)
    g, w = path[i]
    return "%u.%03u Gb/s available PCIe bandwidth, limited by %s PCIe x%d link at index %d" % (
        v // 1000, v % 1000, SPEED_STR[g], w, i)


# ---------------------------------------------------------------- 연습 2
def read_bw_tag_limited(tags, mrrs, rtt_ns, ceiling_GBps):
    """outstanding MRd 가 tags 개로 제한될 때 읽기 처리량 GB/s = min(ceiling, tags × mrrs / rtt).
    bytes/ns == GB/s 이므로 단위 변환이 필요 없다."""
    if tags < 1 or mrrs < 1 or rtt_ns <= 0:
        raise ValueError("tags, mrrs >= 1 and rtt > 0")
    return min(F(ceiling_GBps), F(tags * mrrs) / F(rtt_ns))


def min_tags(ceiling_GBps, mrrs, rtt_ns):
    """링크 상한을 채우는 데 필요한 최소 outstanding 수 = ceil(ceiling × rtt / mrrs)  (Little's Law)"""
    need = F(ceiling_GBps) * F(rtt_ns) / mrrs
    return -(-need.numerator // need.denominator)


# ---------------------------------------------------------------- 연습 3
def skp_factor(gen, interval=None):
    """SKP ordered set 이 남겨 주는 비율 (모델, 간격 값은 스펙 확인 권장).
    Gen1/2: 4 심볼(COM+SKP×3)짜리 SKP OS 를 interval 심볼 시간마다 (기본 1180) → (interval-4)/interval
    Gen3–5: SKP OS 블록 1개를 데이터 블록 interval 개마다 (기본 370)   → interval/(interval+1)
    Gen6  : FLIT 모드의 SKP 는 이 모델에서 생략 → 1"""
    if gen <= 2:
        n = 1180 if interval is None else interval
        if n <= 4:
            raise ValueError("interval too small")
        return F(n - 4, n)
    if gen <= 5:
        n = 370 if interval is None else interval
        if n < 1:
            raise ValueError("interval too small")
        return F(n, n + 1)
    return F(1)


# ---------------------------------------------------------------- 연습 4
def nvme_4k_io(gen, width, io_bytes=4096, mps=256, irq_per_io=F(1)):
    """랜덤 읽기 1건(io_bytes)에 링크가 나르는 바이트 (모델: 헤더 4DW, CplD 3DW, DLLP 무시).
      upstream   (SSD→호스트): SQE fetch MRd(0B) + 데이터 MWr(MPS 로 쪼갬) + CQE MWr 16B + MSI-X MWr 4B × irq_per_io
      downstream (호스트→SSD): SQ tail doorbell MWr 4B + SQE CplD 64B(3DW) + CQ head doorbell MWr 4B
    반환 {'up': 바이트, 'down': 바이트, 'iops': PCIe 쪽 IOPS 상한(두 방향 중 작은 쪽), 'bound': 'up'|'down'}"""
    n_full, rest = divmod(io_bytes, mps)
    data = n_full * tlp_wire(gen, mps) + (tlp_wire(gen, rest) if rest else 0)
    up = tlp_wire(gen, 0) + data + tlp_wire(gen, 16) + F(irq_per_io) * tlp_wire(gen, 4)
    down = tlp_wire(gen, 4) + tlp_wire(gen, 64, hdr=12) + tlp_wire(gen, 4)
    bps = link_GBps(gen, width) * 10 ** 9
    iops_up, iops_down = bps / up, bps / down
    return {"up": up, "down": down, "iops": min(iops_up, iops_down),
            "bound": "up" if iops_up <= iops_down else "down"}


# ---------------------------------------------------------------- 테스트 하네스
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


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def near(x, want, tol=0.0005):
    return abs(float(x) - want) <= tol


def main():
    print("-- 연습 1: dmesg available PCIe bandwidth --")
    check("per-lane Mb/s table 2000/4000/7876/15753/31507/64000",
          lambda: [KMBPS[g] for g in range(1, 7)] == [2000, 4000, 7876, 15753, 31507, 64000])
    check("Gen3 x4 == 31504 Mb/s", lambda: kernel_mbps(3, 4) == 31504)
    check("Gen4 x4 == 63012 Mb/s, Gen5 x16 == 504112", lambda: kernel_mbps(4, 4) == 63012
          and kernel_mbps(5, 16) == 504112)
    check("Gen4 x4 SSD behind Gen3 x8 uplink: limited by index 1 (63008 < 63012)",
          lambda: available_bw([(4, 4), (3, 8)]) == (63008, 1))
    check("tie Gen4 x2 vs Gen4 x2: device-side link reported",
          lambda: available_bw([(4, 2), (4, 2)]) == (31506, 0))
    check("kernel_msg Gen3 x4 alone",
          lambda: kernel_msg([(3, 4)]) == "31.504 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at index 0")
    check("empty path -> ValueError", lambda: raises(ValueError, lambda: available_bw([])))

    print("-- 연습 2: Tag 개수와 Little's Law --")
    ceil44 = F(512, 65) * F(512, 512 + 2 * (12 + 8 + 2))    # 본문 effective_read(4,4,512,256,256)
    check("ceiling (Gen4 x4, MRRS512, CplD256, DLLP 1/4) ≈ 7.254", lambda: near(ceil44, 7.254))
    check("32 tags, RTT 1us: 16.384 GB/s possible → link-bound 7.254",
          lambda: read_bw_tag_limited(32, 512, 1000, ceil44) == ceil44)
    check("32 tags, RTT 4us: tag-bound 4.096 GB/s", lambda: read_bw_tag_limited(32, 512, 4000, ceil44) == F(4096, 1000))
    check("min_tags RTT 1us == 15, RTT 4us == 57 (> 32 → Extended Tag 필요)",
          lambda: min_tags(ceil44, 512, 1000) == 15 and min_tags(ceil44, 512, 4000) == 57)
    check("tags 0 -> ValueError", lambda: raises(ValueError, lambda: read_bw_tag_limited(0, 512, 1000, ceil44)))

    print("-- 연습 3: SKP ordered set --")
    check("Gen3–5 factor 370/371, Gen1/2 1176/1180, Gen6 1",
          lambda: skp_factor(4) == F(370, 371) and skp_factor(2) == F(1176, 1180) and skp_factor(6) == 1)
    check("Gen4 x4 MPS256 read 7.151 → ×SKP ≈ 7.131 GB/s",
          lambda: near(F(512, 65) * F(256, 282) * skp_factor(4), 7.131))
    check("SKP costs < 0.35% at every gen", lambda: all(1 - skp_factor(g) < F(35, 10000) for g in range(1, 7)))

    print("-- 연습 4: 4KB 랜덤 읽기 IOPS 상한 (PCIe 쪽만) --")
    r = nvme_4k_io(4, 4)
    check("Gen4 x4 4KB MPS256: up 4572 B, down 140 B per IO",
          lambda: r["up"] == 24 + 16 * 280 + 40 + 28 and r["down"] == 28 + 84 + 28)
    check("Gen4 x4 PCIe IOPS ceiling ≈ 1.7229 M, upstream-bound",
          lambda: near(r["iops"] / 10 ** 6, 1.7229, 0.0001) and r["bound"] == "up")
    check("MSI-X coalescing 1/8 → up 4547.5 B", lambda: nvme_4k_io(4, 4, irq_per_io=F(1, 8))["up"] == F(9095, 2))
    check("512B IO: 2 data TLPs (560 B), up 652 B, IOPS ≈ 12.081 M",
          lambda: nvme_4k_io(4, 4, io_bytes=512)["up"] == 24 + 2 * 280 + 40 + 28
          and near(nvme_4k_io(4, 4, io_bytes=512)["iops"] / 10 ** 6, 12.081, 0.001))
    check("Gen3 x4 4KB ceiling == half of Gen4 x4", lambda: nvme_4k_io(3, 4)["iops"] * 2 == r["iops"])

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
