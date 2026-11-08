#!/usr/bin/env python3
"""SIV-B01 레지스터 기술서 파싱 → C 헤더 자동 생성

레지스터 맵의 단일 소스(single source of truth)는 regmap.json 이다. 이 스크립트는
  Part A  기술서를 읽어 필드 비트 범위·마스크·리셋값·access 별 마스크를 계산하고
  Part B  잘못된 기술서(필드 겹침, 32비트 폭 초과, 리셋값 넘침, 오프셋 충돌 ...)를 lint 로 거부하고
  Part C  통과한 기술서로 C 헤더(#define *_OFFSET/_MASK/_SHIFT/_RESET + 레지스터 기술 테이블)를 생성한다.

  python3 -I solution.py                    # 테스트만 실행 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --emit spi0_regs.h # 테스트 + 헤더 파일 생성
  python3 -I solution.py --src bad_overlap.json --force --emit /tmp/x.h   # lint 무시 (데모 전용)

Python 3 표준 라이브러리만 쓴다 (json, re, sys, os, copy).
"""
import copy
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REG_WIDTH = 32
ACCESS_TYPES = ("RW", "RO", "WO", "W1C", "W1S", "RC")
IDENT = re.compile(r"^[A-Z][A-Z0-9_]*$")


class LintError(Exception):
    """기술서가 lint 를 통과하지 못했을 때 gen_header 가 던진다."""


def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


def parse_int(v):
    """JSON 값 → int. 정수 또는 "0x1F" / "31" 같은 문자열을 받는다."""
    if isinstance(v, bool):
        raise ValueError("bool is not a number")
    if isinstance(v, int):
        return v
    return int(str(v).strip().replace("_", ""), 0)


def fmt_bits(lsb, width):
    msb = lsb + width - 1
    return "%d" % lsb if width == 1 else "%d:%d" % (msb, lsb)


# ---------------------------------------------------------------- Part A: 파싱
def parse_bits(s):
    """ "11:8" / "[11:8]" / "5" → (lsb, width). msb < lsb, 음수, 숫자 아님 → ValueError """
    # TODO: 정규식 또는 split(":") 로 msb/lsb 를 뽑는다. 한 숫자면 1비트 필드.
    #       대괄호 [ ] 허용, msb < lsb 이거나 숫자가 아니면 ValueError.
    return 0, 1


def field_mask(lsb, width):
    """하위에서 lsb 번째 비트부터 width 개가 1인 마스크"""
    return 0  # TODO: width 개의 1 을 lsb 만큼 왼쪽으로 민다


def reg_summary(fields):
    """정규화된 필드 목록 → 레지스터 단위 요약 dict
    reset   : 필드 리셋값을 제자리로 민 OR
    defined : 어떤 필드에든 속한 비트 (나머지는 reserved/undefined)
    rw ro wo w1c w1s rc : access 별 비트 마스크
    vol     : volatile=true 필드 비트 (HW가 바꾸므로 리셋값 비교에서 빼야 할 후보)"""
    s = {"reset": 0, "defined": 0, "vol": 0}
    for a in ACCESS_TYPES:
        s[a.lower()] = 0
    for f in fields:
        pass  # TODO: reset(lsb 만큼 밀고 mask 로 자름) / defined / access 별 / vol 을 OR 로 누적
    return s


def normalize(raw):
    """lint 를 통과한 raw JSON → 정수만 담은 dict (주어진 코드)"""
    regs = []
    for r in raw["registers"]:
        fields = []
        for f in r["fields"]:
            lsb, width = parse_bits(f["bits"])
            fields.append({"name": f["name"], "lsb": lsb, "width": width,
                           "mask": field_mask(lsb, width), "access": f["access"],
                           "reset": parse_int(f.get("reset", 0)),
                           "volatile": bool(f.get("volatile", False))})
        fields.sort(key=lambda f: f["lsb"])
        reg = {"name": r["name"], "offset": parse_int(r["offset"]),
               "desc": r.get("desc", ""), "fields": fields}
        reg.update(reg_summary(fields))
        regs.append(reg)
    regs.sort(key=lambda r: r["offset"])
    return {"block": raw["block"], "base": parse_int(raw["base"]),
            "size": parse_int(raw["size"]), "version": raw.get("version", ""),
            "desc": raw.get("desc", ""), "regs": regs}


# ---------------------------------------------------------------- Part B: lint
def lint_fields(reg, reg_width=REG_WIDTH):
    """레지스터 하나의 필드 검사 → 에러 문자열 목록. 각 문자열은 'E_코드 ...' 로 시작한다.
    E_IDENT E_DUPNAME E_BITS E_WIDTH E_ACCESS E_RESET E_OVERLAP E_RESETMISMATCH"""
    errs = []
    rn = reg.get("name", "?")
    used = []                       # (이름, lsb, width, mask) — 이미 자리 잡은 필드
    names = set()
    composed = 0                    # 필드 리셋값으로 만든 레지스터 리셋값
    for f in reg.get("fields", []):
        fn = "%s.%s" % (rn, f.get("name", "?"))
        if not IDENT.match(str(f.get("name", ""))):
            errs.append("E_IDENT %s: field name is not an UPPER_CASE C identifier" % fn)
        if f.get("name") in names:
            errs.append("E_DUPNAME %s: duplicate field name" % fn)
        names.add(f.get("name"))
        try:
            lsb, width = parse_bits(f.get("bits", ""))
        except ValueError as e:
            errs.append("E_BITS %s: %s" % (fn, e))
            continue
        # TODO ① lsb + width > reg_width        → E_WIDTH  (그 필드는 더 검사하지 말고 continue)
        #      ② access 가 ACCESS_TYPES 에 없음  → E_ACCESS
        #      ③ reset 이 숫자가 아니거나 width 비트에 안 들어감 → E_RESET
        #      ④ 앞서 자리 잡은 필드(used)와 마스크 AND != 0 → E_OVERLAP (겹친 비트 마스크까지 출력)
        #      ⑤ 루프가 끝난 뒤 reg 에 "reset" 이 있으면 필드로 조합한 값과 비교 → E_RESETMISMATCH
        pass
    return errs


def lint(raw, reg_width=REG_WIDTH):
    """기술서 전체 검사. 빈 목록 = 통과. 블록/레지스터 수준:
    E_IDENT E_DUPNAME E_ALIGN E_RANGE E_DUPOFF E_BITS(오프셋 숫자 아님) + lint_fields 결과"""
    errs = []
    blk = str(raw.get("block", ""))
    if not IDENT.match(blk):
        errs.append("E_IDENT block %r is not an UPPER_CASE C identifier" % blk)
    try:
        size = parse_int(raw.get("size", 0))
    except ValueError:
        size = 0
    step = reg_width // 8
    seen_off = {}
    seen_name = set()
    for r in raw.get("registers", []):
        rn = str(r.get("name", "?"))
        if not IDENT.match(rn):
            errs.append("E_IDENT register %r is not an UPPER_CASE C identifier" % rn)
        if rn in seen_name:
            errs.append("E_DUPNAME %s: duplicate register name" % rn)
        seen_name.add(rn)
        try:
            off = parse_int(r.get("offset"))
        except (ValueError, TypeError):
            errs.append("E_BITS %s: offset %r is not a number" % (rn, r.get("offset")))
            off = None
        # TODO: off 가 숫자면 ① step(4) 배수 아님 → E_ALIGN  ② off + step > size → E_RANGE
        #       ③ 이미 본 오프셋 → E_DUPOFF (먼저 그 자리를 차지한 레지스터 이름도 출력)
        errs.extend(lint_fields(r, reg_width))
    return errs


# ---------------------------------------------------------------- Part C: 코드 생성
def gen_header(raw, src_name="regmap.json"):
    """lint 통과 시 C 헤더 문자열을 돌려준다. 실패하면 LintError(에러 전부를 줄바꿈으로 연결).
    출력은 결정적(deterministic)이어야 한다: 타임스탬프·dict 순서·절대경로를 넣지 않는다."""
    return ""  # TODO: lint 에러가 있으면 LintError("\n".join(errs)), 없으면 _emit(normalize(raw), src_name)


def _emit(b, src_name):
    P = b["block"]
    out = []
    w = out.append

    def d(name, val):
        w("#define %-34s %s" % (name, val))

    w("/* %s register map -- AUTO-GENERATED from %s by SIV-B01 codegen. DO NOT EDIT." % (P, src_name))
    w(" * block %s v%s, base 0x%08X, size 0x%X, %d registers */"
      % (P, b["version"], b["base"], b["size"], len(b["regs"])))
    w("#ifndef %s_REGS_H" % P)
    w("#define %s_REGS_H" % P)
    w("")
    w("#include <stdint.h>")
    w("")
    d(P + "_BASE", "0x%08Xu" % b["base"])
    d(P + "_SIZE", "0x%03Xu" % b["size"])
    d(P + "_REG_COUNT", "%du" % len(b["regs"]))
    for r in b["regs"]:
        R = "%s_%s" % (P, r["name"])
        w("")
        w("/* ---- %s @ 0x%03X : %s ---- */" % (r["name"], r["offset"], r["desc"]))
        # TODO: 레지스터 매크로 _OFFSET _ADDR _RESET _DEFINED_MASK _RW/_RO/_WO/_W1C/_W1S/_RC_MASK _VOLATILE_MASK
        #       필드마다 _SHIFT _WIDTH _MASK _RESET _GET(r) _PREP(v)   (d(이름, 값) 헬퍼 사용)
        pass
    t = P.lower() + "_reg_desc_t"
    w("")
    w("typedef struct {")
    w("    const char *name;")
    w("    uint32_t offset, reset, defined, rw, ro, wo, w1c, w1s, rc, vol;")
    w("} %s;" % t)
    w("")
    w("#ifdef %s_REGS_WANT_TABLE   /* 테이블이 필요한 번역 단위 하나에서만 정의하고 include */" % P)
    w("static const %s %s_REGS[%s_REG_COUNT] = {" % (t, P, P))
    w("    /* name, offset, reset, defined, rw, ro, wo, w1c, w1s, rc, vol */")
    for r in b["regs"]:
        vals = ", ".join("0x%08Xu" % r[k] for k in
                         ("reset", "defined", "rw", "ro", "wo", "w1c", "w1s", "rc", "vol"))
        w('    { %-12s 0x%03Xu, %s },' % ('"%s",' % r["name"], r["offset"], vals))
    w("};")
    w("#endif")
    w("")
    w("#endif /* %s_REGS_H */" % P)
    return "\n".join(out) + "\n"


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


def has(errs, code, *words):
    """errs 중 code 로 시작하고 words 를 모두 포함하는 줄이 있는가"""
    return any(e.startswith(code + " ") and all(x in e for x in words) for e in errs)


def mutate(raw, fn):
    r = copy.deepcopy(raw)
    fn(r)
    return r


def reg_of(raw, name):
    return next(r for r in raw["registers"] if r["name"] == name)


def run_tests():
    good = load("regmap.json")

    print("-- Part A: parse --")
    check("parse_bits('11:8') == (8, 4)", lambda: parse_bits("11:8") == (8, 4))
    check("parse_bits('5') == (5, 1)", lambda: parse_bits("5") == (5, 1))
    check("parse_bits('[31:0]') == (0, 32)", lambda: parse_bits("[31:0]") == (0, 32))
    check("parse_bits('3:8') raises (msb < lsb)", lambda: raises(ValueError, lambda: parse_bits("3:8")))
    check("parse_bits('a:b') raises", lambda: raises(ValueError, lambda: parse_bits("a:b")))
    check("field_mask(8, 4) == 0xF00", lambda: field_mask(8, 4) == 0xF00)
    check("field_mask(0, 32) == 0xFFFFFFFF", lambda: field_mask(0, 32) == 0xFFFFFFFF)
    n = normalize(good)
    R = {r["name"]: r for r in n["regs"]}
    check("CTRL reset == 0x00070302 (from fields)", lambda: R["CTRL"]["reset"] == 0x00070302)
    check("CTRL defined == 0x001F0F3F", lambda: R["CTRL"]["defined"] == 0x001F0F3F)
    check("ID reset == 0x53500102, ro == 0xFFFFFFFF",
          lambda: R["ID"]["reset"] == 0x53500102 and R["ID"]["ro"] == 0xFFFFFFFF)
    check("INT_RAW w1c == vol == 0x7", lambda: R["INT_RAW"]["w1c"] == 0x7 and R["INT_RAW"]["vol"] == 0x7)
    check("ERR_CNT rc == 0xFFFF, rw == 0", lambda: R["ERR_CNT"]["rc"] == 0xFFFF and R["ERR_CNT"]["rw"] == 0)
    check("STATUS reset == 0x2 (TXE=1)", lambda: R["STATUS"]["reset"] == 0x2)

    print("-- Part B: lint --")
    check("good regmap: lint == []", lambda: lint(good) == [])
    bo = lint(load("bad_overlap.json"))
    check("bad_overlap: E_OVERLAP CTRL.CLKDIV vs LSBF",
          lambda: has(bo, "E_OVERLAP", "CTRL.CLKDIV[11:4]", "CTRL.LSBF[4]", "0x00000010"))
    check("bad_overlap: E_OVERLAP CTRL.CLKDIV vs LOOPBACK",
          lambda: has(bo, "E_OVERLAP", "CTRL.CLKDIV", "CTRL.LOOPBACK[5]", "0x00000020"))
    check("bad_overlap: E_RESETMISMATCH CTRL", lambda: has(bo, "E_RESETMISMATCH", "CTRL", "0x00070032"))
    check("bad_overlap: exactly 3 errors", lambda: len(bo) == 3)
    bw = lint(load("bad_width.json"))
    check("bad_width: E_WIDTH CTRL.FSIZE[36:32]", lambda: has(bw, "E_WIDTH", "CTRL.FSIZE[36:32]"))
    check("bad_width: E_RESET CTRL.CLKDIV 0x13 in 4 bits", lambda: has(bw, "E_RESET", "CTRL.CLKDIV", "0x13"))
    check("bad_width: E_ACCESS DMA_CTRL.MODE 'RWX'", lambda: has(bw, "E_ACCESS", "DMA_CTRL.MODE", "RWX"))
    check("bad_width: TXTHR[31:28] (msb 31) is legal",
          lambda: not any("TXTHR" in e for e in bw))
    check("dup offset -> E_DUPOFF",
          lambda: has(lint(mutate(good, lambda r: reg_of(r, "SCRATCH").update(offset="0x030"))),
                      "E_DUPOFF", "SCRATCH", "DMA_CTRL"))
    check("misaligned 0x02E -> E_ALIGN",
          lambda: has(lint(mutate(good, lambda r: reg_of(r, "SCRATCH").update(offset="0x02E"))),
                      "E_ALIGN", "SCRATCH"))
    check("offset 0x040 beyond size -> E_RANGE",
          lambda: has(lint(mutate(good, lambda r: reg_of(r, "SCRATCH").update(offset="0x040"))),
                      "E_RANGE", "SCRATCH"))
    check("lowercase name -> E_IDENT",
          lambda: has(lint(mutate(good, lambda r: reg_of(r, "CTRL")["fields"][0].update(name="en"))),
                      "E_IDENT", "CTRL.en"))
    check("dup register name -> E_DUPNAME",
          lambda: has(lint(mutate(good, lambda r: reg_of(r, "SCRATCH").update(name="CTRL"))),
                      "E_DUPNAME", "CTRL"))

    print("-- Part C: codegen --")
    h = gen_header(good)
    check("header has include guard", lambda: "#ifndef SPI0_REGS_H" in h and h.rstrip().endswith("#endif /* SPI0_REGS_H */"))
    check("CTRL_OFFSET 0x004u", lambda: re.search(r"^#define SPI0_CTRL_OFFSET\s+0x004u$", h, re.M))
    check("CTRL_CLKDIV_MASK 0x00000F00u", lambda: re.search(r"^#define SPI0_CTRL_CLKDIV_MASK\s+0x00000F00u$", h, re.M))
    check("CTRL_CLKDIV_SHIFT 8u", lambda: re.search(r"^#define SPI0_CTRL_CLKDIV_SHIFT\s+8u$", h, re.M))
    check("CTRL_RESET 0x00070302u", lambda: re.search(r"^#define SPI0_CTRL_RESET\s+0x00070302u$", h, re.M))
    check("INT_RAW_W1C_MASK 0x00000007u", lambda: re.search(r"^#define SPI0_INT_RAW_W1C_MASK\s+0x00000007u$", h, re.M))
    check("14 *_OFFSET defines", lambda: len(re.findall(r"^#define SPI0_\w+_OFFSET ", h, re.M)) == 14)
    check("gen twice -> identical (deterministic)", lambda: gen_header(load("regmap.json")) == h)
    check("gen(bad_overlap) raises LintError",
          lambda: raises(LintError, lambda: gen_header(load("bad_overlap.json"))))

    def stale():
        p = os.path.join(HERE, "spi0_regs.h")
        with open(p, encoding="utf-8") as f:
            return f.read() == h
    check("committed spi0_regs.h is up to date (CI stale check)", stale)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    src, emit, force = "regmap.json", None, False
    i = 1
    while i < len(argv):
        if argv[i] == "--src":
            src, i = argv[i + 1], i + 2
        elif argv[i] == "--emit":
            emit, i = argv[i + 1], i + 2
        elif argv[i] == "--force":
            force, i = True, i + 1
        else:
            print("usage: %s [--src regmap.json] [--emit out.h] [--force]" % argv[0])
            return 2
    if emit:
        raw = load(src)
        errs = lint(raw)
        for e in errs:
            print("lint: " + e, file=sys.stderr)
        if errs and not force:
            print("codegen: %s rejected (%d lint errors), header NOT written" % (src, len(errs)),
                  file=sys.stderr)
            return 1
        text = _emit(normalize(raw), os.path.basename(src)) if force else gen_header(raw, os.path.basename(src))
        with open(emit, "w", encoding="utf-8") as f:
            f.write(text)
        print("codegen: wrote %s (%d lines)" % (emit, text.count("\n")), file=sys.stderr)
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
