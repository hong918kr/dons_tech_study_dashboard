#!/usr/bin/env python3
"""ELX-B01 추가 연습 — 부트 로그·부트 이미지로 하는 bring-up 점검 5가지 (한 번에 검증)

  1 over_budget      단계별 부팅 시간 예산 초과 찾기
  2 parse_image_hdr  arm64 Image 헤더 64바이트 해석 + U-Boot booti 가 커널을 옮길 주소 계산
  3 find_overlaps    커널·initrd·DTB 로드 영역 겹침 검사 (bring-up 단골 버그)
  4 slowest_gaps     로그에서 가장 긴 '침묵' 구간 찾기
  5 bootargs_trace   U-Boot 가 넣은 bootargs → 커널이 받은 cmdline → 사용자 공간으로 넘어간 인자

  python3 -I practice.py      # "N/M passed", 실패 시 exit 1   (Python 3.9 표준 라이브러리만)
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HOST_RE = re.compile(r"^\[\s*(\d+\.\d+)\s+\d+\.\d+\] ?")
PRINTK_RE = re.compile(r"^\[\s*(\d+\.\d+)\] ?")
ESC_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b[@-Z\\-_]")
SZ_2M = 2 << 20


def lines_of(text):
    """→ [(lineno, host or None, text)] 빈 줄 제외 (CR 덮어쓰기는 '마지막 조각'으로 단순화)"""
    out = []
    for i, raw in enumerate(text.split("\n")):
        m = HOST_RE.match(raw)
        host = float(m.group(1)) if m else None
        body = ESC_RE.sub("", raw[m.end():] if m else raw).rstrip("\r")
        body = body.split("\r")[-1]
        k = PRINTK_RE.match(body)
        body = body[k.end():] if k else body
        if body.strip():
            out.append((i + 1, host, body))
    return out


# ---------------------------------------------------------------- 1. 예산
def over_budget(durations_ms, budget_ms):
    """{단계: ms}, {단계: 예산 ms} → [(단계, 초과 ms)] 초과량 큰 순. 예산 없는 단계는 무시"""
    over = [(s, round(d - budget_ms[s], 1)) for s, d in durations_ms.items()
            if s in budget_ms and d > budget_ms[s]]
    return sorted(over, key=lambda x: -x[1])


# ---------------------------------------------------------------- 2. Image 헤더
def parse_image_hdr(b):
    """arm64 Image 헤더 (Documentation/arch/arm64/booting.rst, 리틀 엔디언 64바이트)
    0x08 text_offset u64, 0x10 image_size u64, 0x18 flags u64, 0x38 magic u32 = 0x644d5241 ("ARM\\x64")
    flags bit0 = 커널 엔디언(1=BE), bit1-2 = 페이지 크기(1=4K 2=16K 3=64K), bit3 = 2MB 정렬만 맞으면 어디든 OK"""
    if len(b) < 64:
        raise ValueError("short header")
    text_offset, image_size, flags = struct.unpack_from("<QQQ", b, 0x08)
    magic, = struct.unpack_from("<I", b, 0x38)
    return {"text_offset": text_offset, "image_size": image_size, "big_endian": bool(flags & 1),
            "page_kb": {1: 4, 2: 16, 3: 64}.get((flags >> 1) & 3), "anywhere": bool(flags & 8),
            "magic_ok": magic == 0x644d5241}


def booti_dest(load, hdr, dram_start):
    """U-Boot arch/arm/lib/image.c booti_setup() 와 같은 계산: 커널이 실제로 실행될 주소"""
    dst = load - hdr["text_offset"] if hdr["anywhere"] else dram_start
    return ((dst + SZ_2M - 1) & ~(SZ_2M - 1)) + hdr["text_offset"]


# ---------------------------------------------------------------- 3. 겹침
def find_overlaps(regions):
    """{이름: (시작, 크기)} → 겹치는 (이름a, 이름b) 목록 (이름 순서 정렬). [시작, 시작+크기) 반열린 구간"""
    names = sorted(regions)
    out = []
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            (sa, za), (sb, zb) = regions[a], regions[b]
            if sa < sb + zb and sb < sa + za:
                out.append((a, b))
    return out


# ---------------------------------------------------------------- 4. 침묵 구간
def slowest_gaps(text, n):
    """연속한 (빈 줄 아닌) 두 줄의 호스트 시각 차이 상위 n개 → [(gap_s 소수 6자리, 끝낸 줄 번호, 끝낸 줄)]
    gap 은 '그 줄이 느리다'가 아니라 '그 줄 *앞에서* 아무것도 안 찍힌 시간' 이다"""
    ls = [x for x in lines_of(text) if x[1] is not None]
    gaps = [(round(b[1] - a[1], 6), b[0], b[2]) for a, b in zip(ls, ls[1:])]
    return sorted(gaps, key=lambda g: -g[0])[:n]


# ---------------------------------------------------------------- 5. bootargs 추적
def bootargs_trace(text):
    """→ {"uboot": setenv bootargs 값 또는 None, "kernel": Kernel command line, "to_user": 커널이 모르는 인자 목록,
          "same": uboot == kernel}"""
    r = {"uboot": None, "kernel": None, "to_user": [], "same": False}
    for _, _, t in lines_of(text):
        m = re.match(r'^=> setenv bootargs "?(.*?)"?$', t)
        if m:
            r["uboot"] = m.group(1)
        m = re.match(r"^Kernel command line: (.*)$", t)
        if m and r["kernel"] is None:
            r["kernel"] = m.group(1)
        m = re.match(r'^Unknown kernel command line parameters "(.*)", will be passed to user space', t)
        if m:
            r["to_user"] = m.group(1).split()
    r["same"] = r["uboot"] is not None and r["uboot"] == r["kernel"]
    return r


# ---------------------------------------------------------------- 테스트
g_pass = g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
    except Exception as e:
        ok, name = False, "%s  [%s: %s]" % (name, type(e).__name__, e)
    g_pass += ok
    print("%s  %s" % ("PASS" if ok else "FAIL", name))


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8", newline="") as f:
        return f.read()


# /var/lab/out/Image (Linux 6.18.55 arm64) 앞 64바이트 — VM 에서 `xxd -l 64 /var/lab/out/Image` 로 읽은 실물
LAB_IMAGE_HDR = bytes.fromhex(
    "4d5a40fa3ba87f14" "0000000000000000" "0000980200000000" "0a00000000000000"
    "0000000000000000" "0000000000000000" "0000000000000000" "41524d6440000000")


def run_tests():
    Q = read("qemu_tfa_uboot_boot.log")
    # 1. 예산 — solution.py 의 골든 표 값 (ms)
    qemu = {"SPL": 20.3, "BL31": 16.5, "U-Boot": 349.3, "kernel": 1151.9, "init": 144.1}
    imx = {"SPL": 742.7, "BL31": 56.0, "U-Boot": 1462.8, "kernel": 1492.5, "init": 4281.3}
    budget = {"SPL": 500, "BL31": 50, "U-Boot": 1000, "kernel": 1500, "init": 3000}
    check("budget: QEMU fits", lambda: over_budget(qemu, budget) == [])
    check("budget: i.MX8MP over in init > U-Boot > SPL > BL31",
          lambda: over_budget(imx, budget) == [("init", 1281.3), ("U-Boot", 462.8), ("SPL", 242.7), ("BL31", 6.0)])

    # 2. Image 헤더 + booti 이동 주소
    h = parse_image_hdr(LAB_IMAGE_HDR)
    check("lab Image header", lambda: h == {"text_offset": 0, "image_size": 0x2980000, "big_endian": False,
                                            "page_kb": 4, "anywhere": True, "magic_ok": True})
    check("booti: 0x40400000 already 2MB aligned -> no move", lambda: booti_dest(0x40400000, h, 0x40000000) == 0x40400000)
    check("booti: 0x40480000 -> 0x40600000 (i.MX8MP 'Moving Image')", lambda: booti_dest(0x40480000, h, 0x40000000)
          == 0x40600000)
    old = dict(h, anywhere=False)
    check("booti: flags bit3 clear -> base of DRAM", lambda: booti_dest(0x48000000, old, 0x40000000) == 0x40000000)
    check("bad magic detected", lambda: not parse_image_hdr(b"\0" * 64)["magic_ok"])

    # 3. 겹침 — QEMU 실측 주소와 크기
    qemu_regions = {"kernel": (0x40400000, h["image_size"]), "initrd_load": (0x44000000, 0x1277e2),
                    "fdt_qemu": (0x40000000, 0x100000)}
    check("QEMU load regions: no overlap", lambda: find_overlaps(qemu_regions) == [])
    bad = dict(qemu_regions, initrd_load=(0x42000000, 0x1277e2))
    check("ramdisk_addr_r=0x42000000 overlaps kernel", lambda: find_overlaps(bad) == [("initrd_load", "kernel")])
    imx_regions = {"kernel": (0x40600000, 0x2400000), "fdt": (0x43000000, 0x120ff)}
    check("i.MX8MP: 36 MiB Image ends 0x42a00000 < DTB", lambda: find_overlaps(imx_regions) == [])
    grown = dict(imx_regions, kernel=(0x40600000, 0x2b00000))
    check("i.MX8MP: Image grows to 43 MiB -> tramples DTB at 0x43000000",
          lambda: find_overlaps(grown) == [("fdt", "kernel")])
    check("touching regions do not overlap", lambda: find_overlaps({"a": (0, 16), "b": (16, 16)}) == [])

    # 4. 침묵 구간 — 실제 QEMU 로그
    g = slowest_gaps(Q, 3)
    check("longest silence: before 'Booting Linux' (line 42)",
          lambda: g[0][:2] == (0.35576, 42) and g[0][2].startswith("Booting Linux"))
    check("next: line 202 'Key type asymmetric', line 280 'lab initramfs'",
          lambda: [x[1] for x in g[1:]] == [202, 280])

    # 5. bootargs 추적
    t = bootargs_trace(Q)
    check("bootargs U-Boot == kernel", lambda: t["same"] and t["kernel"].startswith("console=ttyAMA0 b01_from=uboot"))
    check("unknown params go to user space", lambda: t["to_user"] == ["b01_from=uboot", "LAB_CMD=cat~/proc/cmdline"])
    check("direct boot: no U-Boot setenv -> same False",
          lambda: not bootargs_trace(read("qemu_direct_kernel_boot.log"))["same"])
    tampered = Q.replace("Kernel command line: console=ttyAMA0", "Kernel command line: console=ttyS0")
    check("mismatch detected (e.g. bootcmd overrode bootargs)", lambda: not bootargs_trace(tampered)["same"])

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
