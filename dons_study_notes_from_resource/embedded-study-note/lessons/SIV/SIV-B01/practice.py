#!/usr/bin/env python3
"""SIV-B01 추가 연습 — 같은 regmap.json(단일 소스)에서 디버그 도구·변경 리포트·C 구조체·경고 lint 만들기

  연습 1  decode(reg, value)        : 레지스터 값 → 필드별 해석 문자열 (실패 리포트/덤프용)
  연습 2  diff_regmaps(old, new)    : 기술서 개정판 간 변경 목록 (RTL rev 가 바뀌면 무엇을 고칠지)
  연습 3  gen_struct(raw)           : CMSIS 스타일 레지스터 구조체 + offsetof _Static_assert
  연습 4  lint_warnings(raw)        : 에러는 아니지만 위험한 기술 (W1C+RW 혼합, RC 인데 volatile 아님)

  python3 -I practice.py                          # 테스트
  python3 -I practice.py --emit-struct out.h      # 연습 3 결과를 파일로 (cc 로 문법 검사용)
"""
import copy
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ACCESS_TYPES = ("RW", "RO", "WO", "W1C", "W1S", "RC")


def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


# ---- 본문(solution.py)의 Part A 를 축약해 넣었다 (레슨 디렉터리끼리 import 하지 않는다) ----
def parse_int(v):
    return v if isinstance(v, int) else int(str(v).strip().replace("_", ""), 0)


def parse_bits(s):
    m = re.match(r"^\s*\[?\s*(\d+)\s*(?::\s*(\d+)\s*)?\]?\s*$", str(s))
    if not m:
        raise ValueError("bad bit range %r" % (s,))
    msb = int(m.group(1))
    lsb = int(m.group(2)) if m.group(2) is not None else msb
    if msb < lsb:
        raise ValueError("msb < lsb in %r" % (s,))
    return lsb, msb - lsb + 1


def fmt_bits(lsb, width):
    return "%d" % lsb if width == 1 else "%d:%d" % (lsb + width - 1, lsb)


def normalize(raw):
    regs = []
    for r in raw["registers"]:
        fields = []
        for f in r["fields"]:
            lsb, width = parse_bits(f["bits"])
            fields.append({"name": f["name"], "lsb": lsb, "width": width,
                           "mask": ((1 << width) - 1) << lsb, "access": f["access"],
                           "reset": parse_int(f.get("reset", 0)),
                           "volatile": bool(f.get("volatile", False))})
        fields.sort(key=lambda f: f["lsb"])
        reset = defined = 0
        acc = {a: 0 for a in ACCESS_TYPES}
        for f in fields:
            reset |= (f["reset"] << f["lsb"]) & f["mask"]
            defined |= f["mask"]
            acc[f["access"]] |= f["mask"]
        regs.append({"name": r["name"], "offset": parse_int(r["offset"]), "fields": fields,
                     "reset": reset, "defined": defined, "acc": acc})
    regs.sort(key=lambda r: r["offset"])
    return {"block": raw["block"], "base": parse_int(raw["base"]),
            "size": parse_int(raw["size"]), "regs": regs}


# ---------------------------------------------------------------- 연습 1
def decode(raw, reg_name, value):
    b = normalize(raw)
    r = next(x for x in b["regs"] if x["name"] == reg_name)
    parts = ["%s=0x%08X" % (reg_name, value)]
    for f in r["fields"]:
        v = (value & f["mask"]) >> f["lsb"]
        parts.append("%s=%s" % (f["name"], ("%d" % v) if f["width"] == 1 else ("0x%X" % v)))
    rsvd = value & ~r["defined"] & 0xFFFFFFFF
    if rsvd:
        parts.append("RSVD=0x%08X" % rsvd)
    return " ".join(parts)


# ---------------------------------------------------------------- 연습 2
def diff_regmaps(old_raw, new_raw):
    old, new = normalize(old_raw), normalize(new_raw)
    O = {r["name"]: r for r in old["regs"]}
    N = {r["name"]: r for r in new["regs"]}
    out = []
    for name in sorted(set(O) | set(N)):
        if name not in N:
            out.append("-REG %s @0x%03X" % (name, O[name]["offset"]))
            continue
        if name not in O:
            out.append("+REG %s @0x%03X" % (name, N[name]["offset"]))
            continue
        o, n = O[name], N[name]
        if o["offset"] != n["offset"]:
            out.append("MOVED %s 0x%03X -> 0x%03X" % (name, o["offset"], n["offset"]))
        of = {f["name"]: f for f in o["fields"]}
        nf = {f["name"]: f for f in n["fields"]}
        for fname in sorted(set(of) | set(nf)):
            fn = "%s.%s" % (name, fname)
            if fname not in nf:
                out.append("-FIELD %s" % fn)
            elif fname not in of:
                out.append("+FIELD %s [%s]" % (fn, fmt_bits(nf[fname]["lsb"], nf[fname]["width"])))
            else:
                a, c = of[fname], nf[fname]
                if (a["lsb"], a["width"]) != (c["lsb"], c["width"]):
                    out.append("FIELD %s [%s] -> [%s]" % (fn, fmt_bits(a["lsb"], a["width"]),
                                                          fmt_bits(c["lsb"], c["width"])))
                if a["access"] != c["access"]:
                    out.append("ACCESS %s %s -> %s" % (fn, a["access"], c["access"]))
        if o["reset"] != n["reset"]:
            out.append("RESET %s 0x%08X -> 0x%08X" % (name, o["reset"], n["reset"]))
    return out


# ---------------------------------------------------------------- 연습 3
def gen_struct(raw):
    b = normalize(raw)
    P = b["block"]
    w = []
    w.append("/* %s register struct -- AUTO-GENERATED from regmap.json (SIV-B01 practice). DO NOT EDIT. */" % P)
    w.append("#ifndef %s_STRUCT_H" % P)
    w.append("#define %s_STRUCT_H" % P)
    w.append("#include <stddef.h>")
    w.append("#include <stdint.h>")
    w.append("typedef struct {")
    pos = 0
    for r in b["regs"]:
        if r["offset"] > pos:
            w.append("    uint32_t RESERVED_%03X[%d];" % (pos, (r["offset"] - pos) // 4))
        ro_only = (r["defined"] & ~(r["acc"]["RO"] | r["acc"]["RC"])) == 0
        q = "const volatile" if ro_only else "volatile"
        w.append("    %-14s uint32_t %s; /* 0x%03X */" % (q, r["name"], r["offset"]))
        pos = r["offset"] + 4
    if b["size"] > pos:
        w.append("    uint32_t RESERVED_%03X[%d];" % (pos, (b["size"] - pos) // 4))
    w.append("} %s_TypeDef;" % P)
    for r in b["regs"]:
        w.append('_Static_assert(offsetof(%s_TypeDef, %s) == 0x%03X, "%s offset");'
                 % (P, r["name"], r["offset"], r["name"]))
    w.append('_Static_assert(sizeof(%s_TypeDef) == 0x%03X, "%s size");' % (P, b["size"], P))
    w.append("#define %s ((%s_TypeDef *)0x%08Xu)" % (P, P, b["base"]))
    w.append("#endif")
    return "\n".join(w) + "\n"


# ---------------------------------------------------------------- 연습 4
def lint_warnings(raw):
    warns = []
    for r in raw["registers"]:
        accs = {f["access"] for f in r["fields"]}
        if "W1C" in accs and accs & {"RW", "W1S"}:
            warns.append("W_MIXED_W1C %s: W1C and %s fields share a register (RMW of the RW field "
                         "clears pending W1C flags)" % (r["name"], "/".join(sorted(accs & {"RW", "W1S"}))))
        for f in r["fields"]:
            if f["access"] == "RC" and not f.get("volatile", False):
                warns.append("W_RC_NOT_VOLATILE %s.%s: read-clear field must be volatile "
                             "(a second read differs)" % (r["name"], f["name"]))
    return warns


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok, why = bool(fn()), ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    g_pass += ok
    print("%s  %s%s" % ("PASS" if ok else "FAIL", name, why))


def mutate(raw, fn):
    r = copy.deepcopy(raw)
    fn(r)
    return r


def reg_of(raw, name):
    return next(r for r in raw["registers"] if r["name"] == name)


def run_tests():
    good = load("regmap.json")
    print("-- 연습 1: decode --")
    check("decode CTRL 0x00070303",
          lambda: decode(good, "CTRL", 0x00070303) ==
          "CTRL=0x00070303 EN=1 MSTR=1 CPOL=0 CPHA=0 LSBF=0 LOOPBACK=0 CLKDIV=0x3 FSIZE=0x7")
    check("decode reports reserved bits",
          lambda: decode(good, "CTRL", 0x80070302).endswith("RSVD=0x80000000"))
    check("decode INT_RAW 0x5",
          lambda: decode(good, "INT_RAW", 0x5) == "INT_RAW=0x00000005 TXDONE=1 RXOVF=0 MODF=1")

    print("-- 연습 2: diff --")
    check("diff(regmap, regmap) == []", lambda: diff_regmaps(good, good) == [])
    check("diff(regmap, bad_overlap) field + reset",
          lambda: diff_regmaps(good, load("bad_overlap.json")) ==
          ["FIELD CTRL.CLKDIV [11:8] -> [11:4]", "RESET CTRL 0x00070302 -> 0x00070032"])

    def rev_b(r):
        reg_of(r, "SCRATCH")["offset"] = "0x038"
        reg_of(r, "INT_EN")["fields"][2]["access"] = "RO"
        r["registers"] = [x for x in r["registers"] if x["name"] != "INT_FORCE"]
        r["registers"].append({"name": "VERSION2", "offset": "0x034", "fields": [
            {"name": "BUILD", "bits": "15:0", "access": "RO", "reset": 7}]})
        reg_of(r, "CTRL")["fields"].append({"name": "DUPLEX", "bits": "6", "access": "RW", "reset": 0})
    d = diff_regmaps(good, mutate(good, rev_b))
    check("rev B diff lists 5 changes", lambda: len(d) == 5)
    check("rev B: MOVED SCRATCH", lambda: "MOVED SCRATCH 0x03C -> 0x038" in d)
    check("rev B: -REG INT_FORCE / +REG VERSION2",
          lambda: "-REG INT_FORCE @0x018" in d and "+REG VERSION2 @0x034" in d)
    check("rev B: ACCESS INT_EN.MODF RW -> RO", lambda: "ACCESS INT_EN.MODF RW -> RO" in d)
    check("rev B: +FIELD CTRL.DUPLEX [6]", lambda: "+FIELD CTRL.DUPLEX [6]" in d)

    print("-- 연습 3: struct --")
    s = gen_struct(good)
    check("reserved gap 0x034..0x03B", lambda: "    uint32_t RESERVED_034[2];" in s)
    check("STATUS is const volatile", lambda: re.search(r"const volatile\s+uint32_t STATUS;", s))
    check("CTRL is volatile (not const)", lambda: re.search(r"^    volatile\s+uint32_t CTRL;", s, re.M))
    check("15 _Static_assert lines (14 offsetof + sizeof)", lambda: s.count("_Static_assert(") == 15)

    print("-- 연습 4: warnings --")
    check("regmap.json has no warnings", lambda: lint_warnings(good) == [])
    check("RW field in INT_RAW -> W_MIXED_W1C",
          lambda: any(x.startswith("W_MIXED_W1C INT_RAW") for x in lint_warnings(mutate(
              good, lambda r: reg_of(r, "INT_RAW")["fields"].append(
                  {"name": "IE", "bits": "8", "access": "RW", "reset": 0})))))
    check("RC without volatile -> W_RC_NOT_VOLATILE",
          lambda: lint_warnings(mutate(good, lambda r: reg_of(r, "ERR_CNT")["fields"][0].pop("volatile")))
          == ["W_RC_NOT_VOLATILE ERR_CNT.OVR: read-clear field must be volatile (a second read differs)"])

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--emit-struct":
        with open(sys.argv[2], "w", encoding="utf-8") as fo:
            fo.write(gen_struct(load("regmap.json")))
    sys.exit(0 if run_tests() else 1)
