#!/usr/bin/env python3
"""ELX-B01 부트 플로우 지도: ROM→SPL→U-Boot→Kernel→init — UART 부트 로그 파서

UART 로그 한 덩어리를 BootROM / SPL / BL31 / U-Boot / kernel / init 단계로 나누고,
단계별 시작·끝 시각, 로드 주소, bootargs, 커널 버전을 뽑아 타임라인 표로 보여 준다.
  Part A  한 줄 정리(CR 덮어쓰기·ESC·깨진 바이트) + 호스트/printk 시각 분리, 단계 상태 기계, 단계 시간표
  Part B  정보 추출(버전·bootargs·로드 주소·init 경로), 커널 t0 추정, 빠진 단계·printk 리셋·CRLF
  Part C  여러 번 부팅이 섞인 로그(부트 루프)를 부팅별로 나누고 "어디서 죽었나" 진단

  python3 -I solution.py                              # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py qemu_tfa_uboot_boot.log      # 타임라인 표 + 추출 정보 + 부팅별 진단

로그 줄 형식: [호스트 경과초 직전줄과의 차이] 원문   (capture_boot.py / grabserial 스타일, 없어도 됨)
원문 안의 커널 줄은 다시 [    0.123456] printk 시각을 가진다. Python 3.9 표준 라이브러리만 쓴다.
"""
import os
import re
import sys
from collections import namedtuple

HERE = os.path.dirname(os.path.abspath(__file__))
STAGES = ("BootROM", "SPL", "BL31", "U-Boot", "kernel", "init")
ROM, SPL, BL31, UBOOT, KERNEL, INIT = range(6)

Line = namedtuple("Line", "no host ktime text")        # no: 1부터, host/ktime: float 또는 None
Rec = namedtuple("Rec", "line stage boot handoff")     # walk() 결과 한 줄

HOST_RE = re.compile(r"^\[\s*(\d+\.\d+)\s+\d+\.\d+\] ?")    # [  0.175733  0.000040]  (캡처 도구가 붙임)
PRINTK_RE = re.compile(r"^\[\s*(\d+\.\d+)\] ?")              # [    0.000000]          (커널이 붙임)
ESC_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b[@-Z\\-_]")  # ANSI/VT100 제어열 (ESC[2K, ESC[?25h ...)

# (단계, 배너 키, 정규식) — 배너는 한 부팅에 한 번만 찍힌다. 같은 배너가 또 보이면 = 리셋 후 새 부팅
MARKERS = (
    (SPL, "TPL", re.compile(r"^U-Boot TPL \S+")),
    (SPL, "SPL", re.compile(r"^U-Boot SPL \S+")),
    (SPL, "TF-A", re.compile(r"^NOTICE:\s+Booting Trusted Firmware")),
    (SPL, None, re.compile(r"^NOTICE:\s+BL[12]:")),               # QEMU: TF-A BL1/BL2 = SPL 역할
    (BL31, None, re.compile(r"^NOTICE:\s+BL31:")),
    (UBOOT, "U-Boot", re.compile(r"^U-Boot \d{4}\.\d{2}")),
    (KERNEL, "Linux", re.compile(r"^Booting Linux on physical CPU")),
)
# (넘겨주는 단계, 받는 단계, 정규식) — 이 줄의 시각 = 다음 단계 시작 시각
HANDOFFS = (
    (SPL, BL31, re.compile(r"^NOTICE:\s+BL1: Booting BL31")),
    (UBOOT, KERNEL, re.compile(r"^Starting kernel \.\.\.")),
    (KERNEL, INIT, re.compile(r"^Run (\S+) as init process")),
)
FATAL_RE = re.compile(r"Kernel panic - not syncing|Attempted to kill init|Unable to handle kernel|"
                      r"\"Synchronous Abort\" handler|failed to boot from all boot devices|"
                      r"Please RESET the board|Bad Linux ARM64 Image magic")
PRINTK_RESET_TOL = 0.1      # printk 시각이 이보다 많이 뒤로 가면 = 커널이 새로 시작


def marker_stage(text):
    """정리된 한 줄 → (단계 번호, 배너 키) 또는 (None, None)"""
    for stage, key, rx in MARKERS:
        if rx.search(text):
            return stage, key
    return None, None


# ---------------------------------------------------------------- Part A: 한 줄 정리
def clean_text(s):
    """UART 원문(str/bytes) → 터미널에 실제로 보이는 글자열.
    ESC 제어열 제거, 줄 끝 CR/LF 제거, 줄 중간 CR = 0열로 돌아가 덮어쓰기, 제어문자·깨진 바이트(U+FFFD) 제거"""
    # TODO: bytes 면 utf-8 (errors="replace") 로 풀고 ESC_RE 를 지운다.
    #   끝의 "\r\n" 을 떼고, 남은 "\r" 로 나눈 조각을 차례로 0열부터 덮어쓴다 (screen = seg + screen[len(seg):])
    #   탭은 남기고, 인쇄 불가 문자와 "\ufffd" 는 버린다. 오른쪽 공백 제거
    raise NotImplementedError("clean_text")


def parse_line(raw, no=0):
    """원문 한 줄 → Line(no, host, ktime, text). host = 캡처 도구 시각, ktime = printk 시각 (없으면 None)"""
    # TODO: ① HOST_RE 는 정리 *전에* 맨 앞에서 뗀다 (CR 덮어쓰기가 접두어를 지우면 안 된다)
    #   ② 나머지를 clean_text ③ 정리된 글자열 맨 앞에서 PRINTK_RE 를 뗀다
    raise NotImplementedError("parse_line")


def read_lines(text):
    """로그 전체(str/bytes) → [Line]. "\n" 으로 나눈다 (CRLF 의 CR 은 clean_text 가 처리)"""
    if isinstance(text, bytes):
        text = text.decode("utf-8", "replace")
    return [parse_line(raw, i + 1) for i, raw in enumerate(text.split("\n"))]


# ---------------------------------------------------------------- Part A: 단계 상태 기계
def new_state():
    return {"cur": None, "seen": set(), "prev_k": None}


def step(state, line):
    """한 줄을 상태 기계에 넣는다 → (이 줄의 단계 또는 None, 새 부팅 시작?, 핸드오프로 넘어갈 단계 또는 None)
    규칙: 단계는 앞으로만 간다. 새 부팅 = ① 지금보다 앞 단계 표지 ② 이미 본 배너 키 ③ printk 시각 역행.
    표지 없는 printk 줄은 kernel 이전이면 kernel 로 본다. 핸드오프 줄 자체는 넘겨주는 단계에 속한다."""
    # TODO: marker_stage → new_boot 판단 (세 조건) → 새 부팅이면 seen/prev_k 초기화, cur = 표지 단계 (없으면 KERNEL)
    #   아니면 cur 를 앞으로만 옮긴다. 배너 키는 seen 에 넣고 printk 시각은 prev_k 에 기록
    #   HANDOFFS 중 (cur == 넘겨주는 단계) 이고 일치하면 state["cur"] 를 받는 단계로 (이 줄의 단계는 그대로)
    raise NotImplementedError("step")


def walk(lines):
    """[Line] → [Rec]. 빈 줄은 건너뛴다. 부팅 번호는 0부터"""
    state, boot, out = new_state(), 0, []
    for ln in lines:
        if not ln.text.strip():
            continue
        stage, new_boot, handoff = step(state, ln)
        if new_boot:
            boot += 1
        out.append(Rec(ln, stage, boot, handoff))
    return out


# ---------------------------------------------------------------- Part A+B: 단계 시간표
def parse_boot_log(text, boot=0):
    """로그 → 단계 6개(STAGES 순서)의 dict 목록 (boot 번째 부팅만).
    dict: stage, present, start, end, dur, lines, first_no, first_text, note
      start = 들어오는 핸드오프 줄 시각, 없으면 그 단계 첫 줄 시각. kernel 은 핸드오프가 없으면
              min(첫 줄 시각, kernel_t0 추정) — 콘솔이 켜질 때 몰아서 나온 줄의 도착 시각은 늦기 때문
      end   = 다음 '있는' 단계의 start, 마지막 단계는 자기 마지막 줄 시각
      BootROM 은 UART 출력이 없다: 첫 부팅이고 호스트 시각이 있고 첫 단계가 kernel 이전이면
              0.0 ~ 첫 단계 start 로 추정 (note="inferred")
      호스트 시각이 없으면 start/end/dur = None (줄 수는 센다)"""
    # TODO: walk 결과에서 이 boot 의 단계 줄만 모아 단계별 첫/마지막 호스트 시각·줄 수·첫 줄을 센다
    #   핸드오프 시각(rec.handoff → 그 줄의 host), kernel_t0 = kernel_t0_host(recs) 를 써서 start 결정
    #   있는 단계들을 순서대로 이어 end 를 정하고, 조건이 맞으면 BootROM 을 추정해 넣는다
    raise NotImplementedError("parse_boot_log")


def kernel_t0_host(recs):
    """커널 printk 시계 0 이 호스트 시계로 언제인가 = min(host - ktime) (둘 다 있는 kernel/init 줄).
    늦게 몰아서 도착한 줄은 host - ktime 이 크므로, 최솟값이 '실시간으로 나온 줄' 기준의 추정치"""
    vals = [r.line.host - r.line.ktime for r in recs
            if r.stage is not None and r.stage >= KERNEL and r.line.host is not None and r.line.ktime is not None]
    return min(vals) if vals else None


# ---------------------------------------------------------------- Part B: 정보 추출
INFO_RES = (
    ("tfa_version", re.compile(r"^NOTICE:\s+BL31: (v\d+\.\d+(?:\.\d+)?)")),
    ("uboot_version", re.compile(r"^U-Boot (\d{4}\.\d{2}\S*)")),
    ("kernel_version", re.compile(r"^Linux version (\S+)")),
    ("machine", re.compile(r"^Machine model: (.+)$")),
    ("bootargs", re.compile(r"^Kernel command line: (.*)$")),
    ("init", re.compile(r"^Run (\S+) as init process")),
)
ENV_RE = re.compile(r"^(kernel_addr_r|ramdisk_addr_r|fdt_addr_r|fdt_addr|fdtcontroladdr|loadaddr)=(?:0x)?([0-9a-fA-F]+)$")
BOOTI_RE = re.compile(r"^=> booti (\S+)")
ADDR_RES = (
    (re.compile(r"^Moving Image from 0x([0-9a-f]+) to 0x([0-9a-f]+), end=0x([0-9a-f]+)"),
     ("image_from", "kernel", "kernel_end")),
    (re.compile(r"Loading Ramdisk to ([0-9a-f]+), end ([0-9a-f]+)"), ("ramdisk", "ramdisk_end")),
    (re.compile(r"(?:Loading Device Tree to|Using Device Tree in place at) ([0-9a-f]+), end ([0-9a-f]+)"),
     ("fdt", "fdt_end")),
    (re.compile(r"^## Flattened Device Tree blob at ([0-9a-f]+)"), ("fdt_blob",)),
)


def extract_info(text, boot=0):
    """boot 번째 부팅에서 뽑는다 → dict
    tfa_version, uboot_version, kernel_version, machine, bootargs, init : 첫 일치 (없으면 None)
    env  : U-Boot 환경 출력 줄 (kernel_addr_r=0x40400000, fdtcontroladdr=8054cce0 — U-Boot 숫자는 0x 없어도 16진수)
    addr : image_from/kernel/kernel_end, ramdisk/ramdisk_end, fdt/fdt_end, fdt_blob (int).
           "Moving Image" 가 없으면 kernel = "=> booti <주소>" 의 첫 인자 (${var} 는 env 로 풀기)
    kernel_t0_host : kernel_t0_host() 를 소수 6자리로, printk_init : "Run ... init" 줄의 printk 시각"""
    # TODO: 이 boot 의 Rec 를 훑으며 INFO_RES(첫 일치만), ENV_RE, ADDR_RES, BOOTI_RE 를 적용한다
    #   booti 인자 "${kernel_addr_r}" → env["kernel_addr_r"], "0x40400000" → int. Moving Image 가 우선
    raise NotImplementedError("extract_info")


# ---------------------------------------------------------------- Part C: 부트 루프 진단
def split_boots(text):
    """로그 → [[Rec, ...], ...] 부팅별 목록 (단계가 붙은 줄만, 부팅 순서대로)"""
    # TODO: walk 결과를 boot 번호로 묶는다 (stage 가 None 인 줄 = 첫 부팅 전 잡음은 버린다)
    raise NotImplementedError("split_boots")


def diagnose(text):
    """부팅마다 dict: boot(1부터), last_stage(이름), reached_init, fatal(첫 치명 줄 글자 또는 None),
    bootargs, last_text(그 부팅의 마지막 줄)"""
    # TODO: split_boots 의 부팅마다 마지막 단계, INIT 도달 여부, FATAL_RE 첫 일치, extract_info(text, b) 의 bootargs
    raise NotImplementedError("diagnose")


# ---- 제공: 표 출력 ----
def fmt(v, spec):
    return "-" if v is None else spec % v


def format_timeline(rows):
    out = ["%-8s %9s %9s %9s %6s  %s" % ("stage", "start_s", "end_s", "dur_ms", "lines", "first line / note")]
    for r in rows:
        if not r["present"]:
            out.append("%-8s %9s %9s %9s %6s  %s" % (r["stage"], "-", "-", "-", "-", "(missing)"))
            continue
        what = (r["first_text"] or "")[:44]
        if r["note"]:
            what = ("(%s) " % r["note"]) + what
        out.append("%-8s %9s %9s %9s %6d  %s" % (r["stage"], fmt(r["start"], "%.3f"), fmt(r["end"], "%.3f"),
                                                 fmt(None if r["dur"] is None else r["dur"] * 1000, "%.1f"),
                                                 r["lines"], what))
    return "\n".join(out)


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8", newline="") as f:
        return f.read()


# ---------------------------------------------------------------- 테스트
g_pass = g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
    except Exception as e:                      # NotImplementedError 포함 → FAIL
        ok, name = False, "%s  [%s: %s]" % (name, type(e).__name__, e)
    g_pass += ok
    print("%s  %s" % ("PASS" if ok else "FAIL", name))


def safe(fn, *args):
    """시작 코드에서 NotImplementedError 가 나도 테스트가 멈추지 않게 (결과 None → 해당 CHECK 는 FAIL)"""
    try:
        return fn(*args)
    except Exception:
        return None


def table(rows):
    def r6(v):
        return None if v is None else round(v, 6)
    return [(r["stage"], r["present"], r6(r["start"]), r6(r["end"]), r["lines"]) for r in rows]


def run_tests():
    Q = read("qemu_tfa_uboot_boot.log")
    D = read("qemu_direct_kernel_boot.log")
    S = read("imx8mp_synthetic_boot.log")
    LOOP = read("imx8mp_bootloop_synthetic.log")

    # ---- Part A: 한 줄 정리 ----
    check("CR overwrite: autoboot countdown",
          lambda: clean_text("\x1b[2K\rHit any key to stop autoboot: 2\x1b[2K\rHit any key to stop autoboot: 0\r")
          == "Hit any key to stop autoboot: 0")
    check("CR overwrite: short segment keeps tail", lambda: clean_text("abcdef\rXY\r\n") == "XYcdef")
    check("ESC only line -> empty", lambda: clean_text("\x1b[?25h\r") == "")
    check("garbage bytes dropped", lambda: clean_text(b"\x00\xff\xfeU-Boot SPL 2024.04\r") == "U-Boot SPL 2024.04")
    check("parse_line host + printk",
          lambda: parse_line("[  0.917607  0.355736] [    0.000000] Booting Linux on physical CPU 0x0\r", 7)
          == Line(7, 0.917607, 0.0, "Booting Linux on physical CPU 0x0"))
    check("parse_line printk only, keeps inner space",
          lambda: parse_line("[    0.915811]  mmcblk2: p1 p2") == Line(0, None, 0.915811, " mmcblk2: p1 p2"))
    check("parse_line host prefix survives CR overwrite",
          lambda: parse_line("[  0.394029  0.052090] \x1b[2K\rHit any key: 2\x1b[2K\rHit any key: 0\r").host == 0.394029)
    check("parse_line '[  OK  ]' is not printk",
          lambda: parse_line("[\x1b[0;32m  OK  \x1b[0m] Reached target").ktime is None)
    check("marker: SPL / BL31 / U-Boot / kernel",
          lambda: [marker_stage(t)[0] for t in ("U-Boot SPL 2024.04-imx (Mar", "NOTICE:  BL2: v2.15.0",
                                               "NOTICE:  BL31: Built", "U-Boot 2026.10 (Oct",
                                               "Booting Linux on physical CPU 0x0", "Run /init as init process")]
          == [SPL, SPL, BL31, UBOOT, KERNEL, None])

    # ---- Part A: 골든 표 — 실제 QEMU TF-A + U-Boot 로그 ----
    q = safe(parse_boot_log, Q)
    check("QEMU golden timeline", lambda: table(q) == [
        ("BootROM", True, 0.0, 0.175733, 0), ("SPL", True, 0.175733, 0.19601, 7),
        ("BL31", True, 0.19601, 0.212499, 2), ("U-Boot", True, 0.212499, 0.561847, 25),
        ("kernel", True, 0.561847, 1.713714, 238), ("init", True, 1.713714, 1.857856, 5)])
    check("QEMU BootROM is inferred", lambda: q[ROM]["note"] == "inferred")
    check("QEMU 'BL1: Booting BL31' (line 7) stays in SPL, BL31 starts at line 8",
          lambda: q[SPL]["lines"] == 7 and q[BL31]["first_no"] == 8)
    check("QEMU: TF-A runtime 'ERROR: ... Power off' is init, not a new boot",
          lambda: len(split_boots(Q)) == 1 and split_boots(Q)[0][-1].stage == INIT)

    # ---- Part A: 골든 표 — 합성 i.MX8MP 로그 ----
    s = safe(parse_boot_log, S)
    check("i.MX8MP (synthetic) golden timeline", lambda: table(s) == [
        ("BootROM", True, 0.0, 0.216774, 0), ("SPL", True, 0.216774, 0.959427, 8),
        ("BL31", True, 0.959427, 1.015427, 2), ("U-Boot", True, 1.015427, 2.478215, 26),
        ("kernel", True, 2.478215, 3.970698, 22), ("init", True, 3.970698, 8.252038, 7)])
    check("i.MX8MP: DDR training dominates SPL", lambda: s[SPL]["dur"] > 0.7)

    # ---- Part B: 정보 추출 ----
    qi = safe(extract_info, Q)
    check("QEMU versions", lambda: (qi["tfa_version"], qi["uboot_version"], qi["kernel_version"])
          == ("v2.15.0", "2026.10", "6.18.55"))
    check("QEMU bootargs from 'Kernel command line:'",
          lambda: qi["bootargs"] == "console=ttyAMA0 b01_from=uboot LAB_CMD=cat~/proc/cmdline")
    check("QEMU env (fdtcontroladdr has no 0x)", lambda: qi["env"] == {
        "kernel_addr_r": 0x40400000, "ramdisk_addr_r": 0x44000000, "fdtcontroladdr": 0x8054cce0})
    check("QEMU addresses (booti ${kernel_addr_r} resolved)", lambda: qi["addr"] == {
        "kernel": 0x40400000, "ramdisk": 0x7f404000, "ramdisk_end": 0x7f52b7e2,
        "fdt": 0x7f3fe000, "fdt_end": 0x7f403087, "fdt_blob": 0x8054cce0})
    check("QEMU init + machine", lambda: (qi["init"], qi["machine"]) == ("/init", "linux,dummy-virt"))
    check("QEMU kernel t0 (host) = 0.693177, printk_init 1.016896",
          lambda: (qi["kernel_t0_host"], qi["printk_init"]) == (0.693177, 1.016896))
    si = safe(extract_info, S)
    check("i.MX8MP: Moving Image -> kernel 0x40600000, DTB in place, no ramdisk", lambda: si["addr"] == {
        "image_from": 0x40480000, "kernel": 0x40600000, "kernel_end": 0x42a00000,
        "fdt_blob": 0x43000000, "fdt": 0x43000000, "fdt_end": 0x430120fe})
    check("i.MX8MP: init /sbin/init, U-Boot 2024.04-imx, TF-A v2.10.0",
          lambda: (si["init"], si["uboot_version"], si["tfa_version"]) == ("/sbin/init", "2024.04-imx", "v2.10.0"))

    # ---- Part B: 가장자리 경우 ----
    d = safe(parse_boot_log, D)
    check("direct -kernel boot: firmware stages missing", lambda: [r["present"] for r in d]
          == [False, False, False, False, True, True])
    check("direct -kernel boot: kernel start = min(host-printk) 0.157495",
          lambda: round(d[KERNEL]["start"], 6) == 0.157495 and d[KERNEL]["note"] != "")
    check("CRLF vs LF: same table", lambda: table(parse_boot_log(Q.replace("\r\n", "\n"))) == table(q))
    nohost = "\n".join(HOST_RE.sub("", ln) for ln in Q.split("\n"))
    nh = safe(parse_boot_log, nohost)
    check("no host stamps: same line counts, times None, no BootROM",
          lambda: [r["lines"] for r in nh] == [0, 7, 2, 25, 238, 5] and nh[KERNEL]["start"] is None
          and not nh[ROM]["present"])
    check("no host stamps: printk still gives Run /init at 1.016896",
          lambda: extract_info(nohost)["printk_init"] == 1.016896)
    mid = "\n".join(D.split("\n")[60:])
    check("capture started mid-kernel: kernel+init only", lambda: [r["present"] for r in parse_boot_log(mid)]
          == [False, False, False, False, True, True])
    dl = D.split("\n")
    kexec = "\n".join(HOST_RE.sub("", ln) for ln in dl + dl[3:])      # 2nd boot lost its first 3 lines
    check("printk reset (no 2nd 'Booting Linux') -> 2 boots", lambda: len(split_boots(kexec)) == 2)
    check("garbage bytes at power-on are ignored",
          lambda: table(parse_boot_log(b"\x00\xff\xff\x00\n" + S.encode())) == table(s))

    # ---- Part C: 부트 루프 ----
    dg = safe(diagnose, LOOP)
    check("boot loop: 3 boots", lambda: len(dg) == 3)
    check("boot 1: kernel panic (no rootwait)", lambda: dg[0]["last_stage"] == "kernel" and not dg[0]["reached_init"]
          and dg[0]["fatal"].startswith("Kernel panic - not syncing: VFS: Unable to mount root fs")
          and "rootwait" not in dg[0]["bootargs"])
    check("boot 2: dies in SPL", lambda: dg[1]["last_stage"] == "SPL" and dg[1]["bootargs"] is None
          and dg[1]["fatal"] == "SPL: failed to boot from all boot devices"
          and dg[1]["last_text"] == "### ERROR ### Please RESET the board ###")
    check("boot 3: reaches init, no fatal", lambda: dg[2]["reached_init"] and dg[2]["fatal"] is None
          and "rootwait" in dg[2]["bootargs"] and dg[2]["last_text"] == "imx8mpevk login:")
    b3 = safe(parse_boot_log, LOOP, 2)
    check("boot 3: no inferred BootROM (reset instant unknown), SPL..init present",
          lambda: [r["present"] for r in b3] == [False, True, True, True, True, True])
    check("timeline text has 6 rows + header and '(missing)'",
          lambda: len(format_timeline(d).splitlines()) == 7 and "(missing)" in format_timeline(d))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) >= 2:
        path = argv[1] if os.path.isabs(argv[1]) else os.path.join(os.getcwd(), argv[1])
        with open(path, encoding="utf-8", errors="replace", newline="") as f:
            text = f.read()
        print(format_timeline(parse_boot_log(text)))
        info = extract_info(text)
        for k in ("tfa_version", "uboot_version", "kernel_version", "machine", "bootargs", "init",
                  "kernel_t0_host", "printk_init"):
            print("%-15s %s" % (k, info[k]))
        print("%-15s %s" % ("env", " ".join("%s=0x%x" % kv for kv in info["env"].items()) or "-"))
        print("%-15s %s" % ("addr", " ".join("%s=0x%x" % kv for kv in info["addr"].items()) or "-"))
        for d in diagnose(text):
            print("boot %d: last=%s init=%s fatal=%s" % (d["boot"], d["last_stage"], d["reached_init"], d["fatal"]))
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
