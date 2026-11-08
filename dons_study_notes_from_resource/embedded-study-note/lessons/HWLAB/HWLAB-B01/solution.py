#!/usr/bin/env python3
"""HWLAB-B01 회로도 넷리스트 감사: 레일·풀업·테스트 포인트 (Netlist Audit)

KiCad 넷리스트(.net, S-expression)를 읽어 bring-up 관점의 위험을 기계적으로 뽑는다.
  Part A  S-expression 파서, 부품 값("4k7", "100n", "R47") 파서, 레일 이름 → 전압
  Part B  풀업/풀다운/직렬 저항 구분(DNP 제외), 넷 분류(I2C·RESET·strap·open-drain), 단일 노드 넷
  Part C  감사 리포트: 레일별 TP·디커플링·부하, 풀 저항 판정, 결함 심은 rev A 검출, rev B 통과

  python3 -I solution.py                                  # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --audit hwb01_revA.net           # 리포트 출력, FAIL 이 있으면 exit 1
  python3 -I solution.py --audit hwb01_revA.net --waive "/BOOT_SEL=SoC 내부 풀다운 (DS 7.3)"

Python 3 표준 라이브러리만 쓴다. 데이터: gen_netlist.py 가 만든 hwb01_revA.net / hwb01_revB.net
"""
import copy
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SI = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "R": 1.0, "r": 1.0,
      "k": 1e3, "K": 1e3, "M": 1e6, "G": 1e9}
GND_RE = re.compile(r"^(GND|AGND|DGND|PGND|SGND|GNDA|GNDD|GNDPWR|VSS|0V)\w*$", re.I)
RAIL_RE = re.compile(r"^(\+|VDD|VCC|VBUS|VBAT|VSYS|VIN|VREF|AVDD|DVDD|VIO|PP\d)", re.I)
RES_REF = re.compile(r"^R\d+$")
RESET_WORDS = {"RST", "NRST", "RESET", "NRESET", "RESETN", "RSTN", "POR", "PORN"}


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------- Part A: 파싱
def parse_sexpr(text):
    """S-expression 텍스트 → 중첩 리스트. 원자(atom)는 모두 str.
    "..." 안의 괄호·공백은 문자 그대로, \\" 와 \\\\ 는 이스케이프. 괄호 불균형·빈 입력 → ValueError"""
    stack, cur, top = [], None, None
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch.isspace():
            i += 1
        elif ch == "(":
            new = []
            if cur is not None:
                cur.append(new)
                stack.append(cur)
            elif top is not None:
                raise ValueError("more than one top-level expression at %d" % i)
            cur = new
            i += 1
        elif ch == ")":
            if cur is None:
                raise ValueError("unbalanced ')' at %d" % i)
            if stack:
                cur = stack.pop()
            else:
                top, cur = cur, None
            i += 1
        elif ch == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 1
                    buf.append({"n": "\n", "t": "\t"}.get(text[j], text[j]))
                else:
                    buf.append(text[j])
                j += 1
            if j >= n:
                raise ValueError("unterminated string at %d" % i)
            if cur is None:
                raise ValueError("atom outside list at %d" % i)
            cur.append("".join(buf))
            i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '()"':
                j += 1
            if cur is None:
                raise ValueError("atom outside list at %d" % i)
            cur.append(text[i:j])
            i = j
    if cur is not None or top is None:
        raise ValueError("unbalanced '(' or empty input")
    return top


def parse_value(s):
    """부품 값 문자열 → float (Ω 또는 F). "4k7"=4700, "4.7k"=4700, "R47"=0.47, "0R"=0, "1M"=1e6,
    "1m"=1e-3(션트), "100n"=1e-7, "2u2"=2.2e-6, "10uF"/"4.7k 1%" 처럼 단위·꼬리 허용. DNP/빈 값 → None"""
    tok = re.split(r"[\s/]+", str(s).strip())[0] if str(s).strip() else ""
    tok = re.sub(r"(?i)(ohms?|Ω|Ω|f)$", "", tok)     # 단위 꼬리 (Ω, F)
    m = re.match(r"^(\d*)([pnuµmRrkKMG])(\d+)$", tok)          # RKM: 4k7, R47, 2u2
    if m and (m.group(1) or m.group(2) in "Rr"):
        return float((m.group(1) or "0") + "." + m.group(3)) * SI[m.group(2)]
    m = re.match(r"^(\d+(?:\.\d+)?)([pnuµmRrkKMG]?)$", tok)      # 4.7k, 10k, 33R, 100n, 1M
    if m:
        return float(m.group(1)) * (SI[m.group(2)] if m.group(2) else 1.0)
    return None


def rail_voltage(name):
    """레일 넷 이름 → 전압(float). "+3V3"=3.3, "VDD_1V8"=1.8, "+5V"=5.0, "+3.3V"=3.3, "PP1V2_S0"=1.2.
    계층 경로는 무시. 숫자 전압이 없으면(VBUS, GND, 신호) None"""
    m = re.search(r"(\d+(?:\.\d+)?)V(\d*)", net_base(name).upper())
    if not m:
        return None
    whole, frac = m.group(1), m.group(2)
    return float(whole + "." + frac) if frac and "." not in whole else float(whole)


# ---- 제공: 넷리스트 → dict ----
def find(expr, key):
    return next((e for e in expr[1:] if isinstance(e, list) and e and e[0] == key), None)


def findall(expr, key):
    return [e for e in expr[1:] if isinstance(e, list) and e and e[0] == key] if expr else []


def sval(expr, key, default=""):
    e = find(expr, key) if expr else None
    return e[1] if e is not None and len(e) > 1 and isinstance(e[1], str) else default


def load_netlist(text):
    """KiCad .net (version "E" KiCad 6~8, 또는 따옴표 없는 KiCad 5 "D") → dict
    {"title","rev","comps":{ref:{value,part,lib,footprint,dnp}},"nets":{name:[node]},"pins":{ref:{pin:net}}}"""
    root = parse_sexpr(text)
    if root[0] != "export":
        raise ValueError("not a KiCad netlist (root=%r)" % root[0])
    tb = find(find(find(root, "design") or ["design"], "sheet") or ["sheet"], "title_block")
    nl = {"title": sval(tb, "title"), "rev": sval(tb, "rev"), "comps": {}, "nets": {}, "pins": {}}
    for c in findall(find(root, "components"), "comp"):
        ref, value = sval(c, "ref"), sval(c, "value")
        lsrc = find(c, "libsource") or ["libsource"]
        dnp = any(sval(p, "name").lower() == "dnp" for p in findall(c, "property"))
        dnp = dnp or bool(re.search(r"\b(DNP|DNI|DNF)\b", value.upper()))
        nl["comps"][ref] = {"value": value, "part": sval(lsrc, "part"), "lib": sval(lsrc, "lib"),
                            "footprint": sval(c, "footprint"), "dnp": dnp}
    for net in findall(find(root, "nets"), "net"):
        name = sval(net, "name")
        nodes = [{"ref": sval(nd, "ref"), "pin": sval(nd, "pin"), "func": sval(nd, "pinfunction"),
                  "type": sval(nd, "pintype")} for nd in findall(net, "node")]
        nl["nets"][name] = nodes
        for nd in nodes:
            nl["pins"].setdefault(nd["ref"], {})[nd["pin"]] = name
    return nl


def net_base(name):
    """계층 경로 제거: "/sensors/I2C2_SDA" → "I2C2_SDA", "+3V3" → "+3V3" """
    return name.rstrip("/").rsplit("/", 1)[-1] if "/" in name else name


def is_gnd(name):
    return bool(GND_RE.match(net_base(name)))


def is_rail(name):
    b = net_base(name)
    return not is_gnd(name) and bool(RAIL_RE.match(b)) and not b.lower().startswith("unconnected")


def natkey(ref):
    m = re.match(r"^([A-Za-z_#]*)(\d*)(.*)$", ref)
    return (m.group(1), int(m.group(2)) if m.group(2) else -1, m.group(3))


def parallel(rs):
    rs = [r for r in rs if r]
    return 1.0 / sum(1.0 / r for r in rs) if rs else None


def fmt_ohm(r):
    if r is None:
        return "?"
    for div, suf in ((1e6, "M"), (1e3, "k")):
        if r >= div:
            return "%.3g%s" % (r / div, suf)
    return "%.3g" % r


def rp_min(vdd):
    """I2C Std/Fast-mode 최소 풀업 (UM10204): VDD>2V → (VDD-0.4V)/3mA, VDD<=2V → 0.8*VDD/2mA (HWLAB-B03 참고)"""
    return (vdd - 0.4) / 0.003 if vdd > 2.0 else (0.8 * vdd) / 0.002


def tokens(s):
    s = s.replace("~{", "").replace("}", "").upper()
    return [w for w in re.split(r"[^A-Z0-9]+", s) if w]


def node_str(nd):
    f = "(%s)" % nd["func"] if nd["func"] else ""
    return "%s.%s%s" % (nd["ref"], nd["pin"], f)


# ---------------------------------------------------------------- Part B: 풀 저항·넷 분류
def find_pulls(nl, net):
    """net 에 붙은 2핀 저항(R\\d+)을 반대쪽 넷으로 분류.
    → {"up":[(ref,ohms,rail)], "down":[(ref,ohms,gnd)], "series":[(ref,ohms,other)], "dnp":[(ref,ohms,other)]}
    DNP 저항은 dnp 에만 (풀로 치지 않는다). 각 목록은 ref 자연 정렬."""
    res = {"up": [], "down": [], "series": [], "dnp": []}
    for nd in nl["nets"].get(net, []):
        ref = nd["ref"]
        if not RES_REF.match(ref) or ref not in nl["comps"]:
            continue
        others = [n for p, n in nl["pins"].get(ref, {}).items() if p != nd["pin"]]
        if len(others) != 1:
            continue
        comp, other = nl["comps"][ref], others[0]
        entry = (ref, parse_value(comp["value"]), other)
        if comp["dnp"]:
            res["dnp"].append(entry)
        elif is_gnd(other):
            res["down"].append(entry)
        elif is_rail(other):
            res["up"].append(entry)
        else:
            res["series"].append(entry)
    for k in res:
        res[k].sort(key=lambda e: natkey(e[0]))
    return res


def categories(nl, net):
    """넷 이름(경로 제외)과 붙은 핀 이름(pinfunction)을 단어로 쪼개 분류 → {"i2c","reset","strap","od"} 부분집합.
    SDA/SCL 은 단어 일치 (SPI 의 SCLK 는 I2C 가 아님). 레일·GND 는 빈 집합."""
    if is_rail(net) or is_gnd(net):
        return set()
    nodes = nl["nets"].get(net, [])
    words = tokens(net_base(net))
    for nd in nodes:
        words += tokens(nd["func"])
    cats = set()
    if any(w in ("SDA", "SCL") for w in words):
        cats.add("i2c")
    if any(w in RESET_WORDS for w in words):
        cats.add("reset")
    if any(w.startswith(("BOOT", "STRAP", "CFG")) or w == "MODE" for w in words):
        cats.add("strap")
    if any(nd["type"].split("+")[0] == "open_collector" for nd in nodes):
        cats.add("od")
    return cats


def classify_single(nl, net):
    """노드가 정확히 1개인 넷 → (sev, msg). 아니면 None.
    unconnected-* + 입력 핀 → WARN(플로팅 입력), unconnected-* + 그 외 → INFO, 이름 있는/자동 넷 → FAIL"""
    nodes = nl["nets"].get(net, [])
    if len(nodes) != 1:
        return None
    nd = nodes[0]
    base = nd["type"].split("+")[0]
    if net.startswith("unconnected-"):
        if base == "input":
            return ("WARN", "floating input %s: CMOS input must not float - tie per datasheet" % node_str(nd))
        return ("INFO", "unconnected %s [%s]: OK if intended (FW: set output/analog or pull)"
                % (node_str(nd), base or "?"))
    return ("FAIL", "only 1 node %s: dangling label/wire (typo or missing connection)" % node_str(nd))


# ---------------------------------------------------------------- Part C: 감사 리포트
def audit_rails(nl):
    """레일마다 TP·디커플링·부하 → finding (sev, "rail", net, msg). GND 는 TP 가 1개 이상인지.
    TP 없는 레일 → FAIL + 프로브할 곳 제안(가장 큰 디커플링 캡의 레일 쪽 핀)."""
    out = []
    for net in sorted(nl["nets"]):
        nodes = nl["nets"][net]
        tps = sorted({nd["ref"] for nd in nodes if nd["ref"].startswith("TP")}, key=natkey)
        if is_gnd(net):
            if tps:
                out.append(("OK", "rail", net, "ground tp=%s" % ",".join(tps)))
            else:
                out.append(("FAIL", "rail", net, "no GND test point: no place for probe ground/spring"))
            continue
        if not is_rail(net):
            continue
        v = rail_voltage(net)
        vs = "%.2fV" % v if v is not None else "?V"
        caps = sorted([nd for nd in nodes if re.match(r"^C\d+$", nd["ref"])], key=lambda nd: natkey(nd["ref"]))
        loads = sorted({nd["ref"] for nd in nodes if nd["ref"].startswith("U") and
                        nd["type"].split("+")[0] == "power_in"}, key=natkey)
        src = sorted({nd["ref"] for nd in nodes if nd["type"].split("+")[0] == "power_out"}, key=natkey)
        info = "src %s; caps %s; loads %s" % (",".join(src) or "-", ",".join(c["ref"] for c in caps) or "-",
                                              ",".join(loads) or "-")
        if tps:
            out.append(("OK", "rail", net, "%s tp=%s (%s)" % (vs, ",".join(tps), info)))
        else:
            big = sorted(caps, key=lambda c: (-(parse_value(nl["comps"][c["ref"]]["value"]) or 0), natkey(c["ref"])))
            best = big[0] if big else None
            hint = "probe %s.%s or add TP" % (best["ref"], best["pin"]) if best else "add TP"
            out.append(("FAIL", "rail", net, "%s no test point (%s) -> %s" % (vs, info, hint)))
    return out


def audit_signals(nl):
    out = []
    for net in sorted(nl["nets"]):
        cats = categories(nl, net)
        if not cats:
            continue
        p = find_pulls(nl, net)
        up, down = p["up"], p["down"]
        desc = "; ".join(["up %s %s->%s" % (r, fmt_ohm(o), t) for r, o, t in up] +
                         ["down %s %s->%s" % (r, fmt_ohm(o), t) for r, o, t in down] +
                         ["DNP %s %s->%s" % (r, fmt_ohm(o), t) for r, o, t in p["dnp"]]) or "no pull"
        active_low = ("~{" in net or net_base(net).upper().endswith("_N") or
                      any(w in ("NRST", "NRESET", "RESETN", "RSTN", "PORN") for w in tokens(net_base(net))))
        need_up = "i2c" in cats or "od" in cats or ("reset" in cats and active_low)
        tag = "/".join(sorted(cats))
        sev, why = "OK", ""
        if need_up and not up:
            sev, why = "FAIL", "needs pull-up, none effective"
        elif "strap" in cats and not up and not down:
            sev, why = "FAIL", "floating strap: boot mode undefined at reset"
        elif "reset" in cats and not up and not down:
            sev, why = "FAIL", "reset line has no pull"
        elif "strap" in cats and up and down:
            sev, why = "WARN", "both pull-up and pull-down: check strap table (divider?)"
        elif "i2c" in cats and down:
            sev, why = "WARN", "pull-down on I2C line"
        if "i2c" in cats and up and sev == "OK":
            eff = parallel([o for _r, o, _t in up])
            vmin = min([rail_voltage(t) for _r, _o, t in up if rail_voltage(t)] or [0])
            if vmin and eff is not None and eff < rp_min(vmin):
                sev, why = "FAIL", "Rp_eff %s < Rp_min %s @%.1fV (3mA sink, UM10204)" % (
                    fmt_ohm(eff), fmt_ohm(rp_min(vmin)), vmin)
            elif len(up) > 1:
                sev, why = "INFO", "%d pull-ups in parallel -> Rp_eff %s (check rise time, HWLAB-B03)" % (
                    len(up), fmt_ohm(eff))
        out.append((sev, tag, net, desc + (" | " + why if why else "")))
    return out


def audit_single(nl):
    out = []
    for net in sorted(nl["nets"]):
        r = classify_single(nl, net)
        if r:
            out.append((r[0], "1-node", net, r[1]))
    return out


def audit(nl, waivers=None):
    """전체 감사 → finding 목록. waivers {net: 사유} 에 있는 FAIL/WARN 은 WAIVED 로 바뀐다."""
    waivers = waivers or {}
    out = []
    for sev, kind, net, msg in audit_rails(nl) + audit_signals(nl) + audit_single(nl):
        if net in waivers and sev in ("FAIL", "WARN"):
            sev, msg = "WAIVED", msg + " [waived: %s]" % waivers[net]
        out.append((sev, kind, net, msg))
    return out


SEV_ORDER = ("FAIL", "WARN", "WAIVED", "INFO", "OK")


def report(nl, findings):
    lines = ["== %s rev %s - netlist audit (%d comps, %d nets) ==" % (
        nl["title"] or "?", nl["rev"] or "?", len(nl["comps"]), len(nl["nets"]))]
    for sev in SEV_ORDER:
        for s, kind, net, msg in findings:
            if s == sev:
                lines.append("%-6s %-14s %-27s %s" % (s, kind, net, msg))
    counts = ", ".join("%d %s" % (sum(1 for f in findings if f[0] == s), s) for s in SEV_ORDER)
    lines.append("summary: %s -> exit %d" % (counts, exit_code(findings)))
    return "\n".join(lines)


def exit_code(findings):
    return 1 if any(f[0] == "FAIL" for f in findings) else 0


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


def attempt(fn):
    try:
        return fn()
    except Exception:  # noqa: BLE001
        return None


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def close(a, b, rel=1e-9):
    return a is not None and abs(a - b) <= rel * max(abs(b), 1e-15)


def sevs(findings, sev):
    return {f[2] for f in findings if f[0] == sev}


def msg_of(findings, net):
    return " ".join(f[3] for f in findings if f[2] == net)


MINI_V8 = '''(export (version "E") (design (sheet (number "1") (name "/") (title_block (title "mini") (rev "1"))))
 (components (comp (ref "R1") (value "4k7") (libsource (lib "Device") (part "R")))
             (comp (ref "U1") (value "MCU") (libsource (lib "x") (part "MCU"))))
 (nets (net (code "1") (name "+3V3") (node (ref "R1") (pin "2") (pintype "passive")))
       (net (code "2") (name "/SDA") (node (ref "R1") (pin "1") (pintype "passive"))
            (node (ref "U1") (pin "7") (pinfunction "SDA") (pintype "bidirectional")))))'''
MINI_V5 = '''(export (version D) (design (sheet (number 1) (name /) (title_block (title mini) (rev 1))))
 (components (comp (ref R1) (value 4k7) (libsource (lib Device) (part R)))
             (comp (ref U1) (value MCU) (libsource (lib x) (part MCU))))
 (nets (net (code 1) (name +3V3) (node (ref R1) (pin 2) (pintype passive)))
       (net (code 2) (name /SDA) (node (ref R1) (pin 1) (pintype passive))
            (node (ref U1) (pin 7) (pinfunction SDA) (pintype bidirectional)))))'''


def rename_net(nl, old, new):
    n = copy.deepcopy(nl)
    n["nets"][new] = n["nets"].pop(old)
    for pins in n["pins"].values():
        for p, v in pins.items():
            if v == old:
                pins[p] = new
    return n


def run_tests():
    A = attempt(lambda: load_netlist(read("hwb01_revA.net")))
    B = attempt(lambda: load_netlist(read("hwb01_revB.net")))

    print("-- 제공 함수 --")
    check("net_base('/sensors/I2C2_SDA') == 'I2C2_SDA'", lambda: net_base("/sensors/I2C2_SDA") == "I2C2_SDA")
    check("is_gnd('GNDA') and not is_gnd('VDD_1V8')", lambda: is_gnd("GNDA") and not is_gnd("VDD_1V8"))
    check("parallel([4700, 4700]) == 2350", lambda: close(parallel([4700, 4700]), 2350))
    check("rp_min(3.3) = 967R, rp_min(1.8) = 720R",
          lambda: abs(rp_min(3.3) - 966.67) < 0.01 and abs(rp_min(1.8) - 720) < 1e-9)

    print("-- Part A: parse --")
    check("parse_sexpr('(a (b \"x y\") c)')", lambda: parse_sexpr('(a (b "x y") c)') == ["a", ["b", "x y"], "c"])
    check("quoted parens: unconnected-(U1-PA7-Pad17)",
          lambda: parse_sexpr('(name "unconnected-(U1-PA7-Pad17)")') == ["name", "unconnected-(U1-PA7-Pad17)"])
    check("escaped quote and empty list", lambda: parse_sexpr(r'(v "4\"7" (company))') == ["v", '4"7', ["company"]])
    check("unbalanced '(a (b)' raises ValueError", lambda: raises(ValueError, lambda: parse_sexpr("(a (b)")))
    check("unbalanced '(a))' raises ValueError", lambda: raises(ValueError, lambda: parse_sexpr("(a))")))
    vals = [("4k7", 4700), ("4.7k", 4700), ("4K7", 4700), ("10k", 1e4), ("33R", 33), ("R47", 0.47), ("0R", 0),
            ("1M", 1e6), ("1m", 1e-3), ("100n", 1e-7), ("2u2", 2.2e-6), ("10uF", 1e-5), ("4.7k 1%", 4700),
            ("2.2kΩ", 2200)]
    for s, v in vals:
        check("parse_value(%r) == %g" % (s, v), lambda s=s, v=v: close(parse_value(s), v) or (v == 0 and parse_value(s) == 0))
    check("parse_value('DNP') is None and parse_value('') is None",
          lambda: parse_value("DNP") is None and parse_value("") is None)
    rails = [("+3V3", 3.3), ("VDD_1V8", 1.8), ("+5V", 5.0), ("/pwr/PP1V2_S0", 1.2), ("+3.3V", 3.3), ("+12V", 12.0)]
    for s, v in rails:
        check("rail_voltage(%r) == %g" % (s, v), lambda s=s, v=v: close(rail_voltage(s), v))
    check("rail_voltage('VBUS') / ('GND') is None", lambda: rail_voltage("VBUS") is None and rail_voltage("GND") is None)
    check("revA loads: 43 comps, 26 nets", lambda: len(A["comps"]) == 43 and len(A["nets"]) == 26)
    check("revA R4 is DNP (property dnp), R3 is not", lambda: A["comps"]["R4"]["dnp"] and not A["comps"]["R3"]["dnp"])
    check("revA +3V3 has 28 nodes", lambda: len(A["nets"]["+3V3"]) == 28)
    check("revA title/rev", lambda: A["title"] == "HWB-01 bring-up board" and A["rev"] == "A")
    check("KiCad5 unquoted == KiCad8 quoted (mini)",
          lambda: load_netlist(MINI_V5)["nets"] == load_netlist(MINI_V8)["nets"] and
          load_netlist(MINI_V5)["comps"]["R1"]["value"] == "4k7")

    print("-- Part B: pulls / classify --")
    check("revB /I2C2_SDA up == [R4 2.2k -> VDD_1V8]",
          lambda: find_pulls(B, "/I2C2_SDA")["up"] == [("R4", 2200.0, "VDD_1V8")])
    check("revA /I2C2_SDA: no up, R4 in dnp",
          lambda: find_pulls(A, "/I2C2_SDA")["up"] == [] and [e[0] for e in find_pulls(A, "/I2C2_SDA")["dnp"]] == ["R4"])
    check("revA /I2C1_SDA: R2, R15 up (parallel 2.35k)",
          lambda: [e[0] for e in find_pulls(A, "/I2C1_SDA")["up"]] == ["R2", "R15"] and
          close(parallel([e[1] for e in find_pulls(A, "/I2C1_SDA")["up"]]), 2350))
    check("revB /BOOT0 down == [R6 10k -> GND]", lambda: find_pulls(B, "/BOOT0")["down"] == [("R6", 1e4, "GND")])
    check("revB /UART_TX: R10 33R is series, not a pull",
          lambda: find_pulls(B, "/UART_TX") == {"up": [], "down": [], "series": [("R10", 33.0, "/UART_TX_J")], "dnp": []})
    check("categories /I2C1_SCL == {i2c}", lambda: categories(B, "/I2C1_SCL") == {"i2c"})
    check("categories /SPI_SCLK == {} (SCLK is not SCL)", lambda: categories(B, "/SPI_SCLK") == set())
    check("categories /NRST == {reset}", lambda: categories(B, "/NRST") == {"reset"})
    check("categories /BOOT_SEL == {strap}", lambda: categories(B, "/BOOT_SEL") == {"strap"})
    check("categories /TEMP_ALERT, /PG_3V3 == {od}",
          lambda: categories(B, "/TEMP_ALERT") == {"od"} and categories(B, "/PG_3V3") == {"od"})
    check("categories GND / +3V3 == {}", lambda: categories(B, "GND") == set() and categories(B, "+3V3") == set())
    check("auto-named Net-(U7-SDA) still i2c (by pinfunction)",
          lambda: categories(rename_net(B, "/I2C2_SDA", "Net-(U7-SDA)"), "Net-(U7-SDA)") == {"i2c"})
    check("classify_single /IMU_INT -> FAIL", lambda: classify_single(A, "/IMU_INT")[0] == "FAIL")
    check("classify_single U6 WP input -> WARN", lambda: classify_single(A, "unconnected-(U6-WP-Pad7)")[0] == "WARN")
    check("classify_single U1 PA7 bidir -> INFO", lambda: classify_single(A, "unconnected-(U1-PA7-Pad17)")[0] == "INFO")
    check("classify_single /NRST (5 nodes) -> None", lambda: classify_single(A, "/NRST") is None)

    print("-- Part C: audit --")
    fa = attempt(lambda: audit(A))
    fb = attempt(lambda: audit(B))
    check("revA FAIL set == 5 planted", lambda: sevs(fa, "FAIL") ==
          {"VDD_1V2", "/I2C2_SDA", "/BOOT_SEL", "/IMU_INT", "/IMU_INT1"})
    check("revA WARN == {U6 WP floating}", lambda: sevs(fa, "WARN") == {"unconnected-(U6-WP-Pad7)"})
    check("VDD_1V2 hint 'probe C4.1'", lambda: "probe C4.1" in msg_of(fa, "VDD_1V2"))
    check("/I2C2_SDA msg names DNP R4", lambda: "DNP R4 2.2k->VDD_1V8" in msg_of(fa, "/I2C2_SDA"))
    check("/I2C1_SDA INFO Rp_eff 2.35k", lambda: "/I2C1_SDA" in sevs(fa, "INFO") and "2.35k" in msg_of(fa, "/I2C1_SDA"))
    check("rails +5V/+3V3/VDD_1V8/GND OK in revA", lambda: {"+5V", "+3V3", "VDD_1V8", "GND"} <= sevs(fa, "OK"))
    check("revB: no FAIL, no WARN, exit 0",
          lambda: not sevs(fb, "FAIL") and not sevs(fb, "WARN") and exit_code(fb) == 0)
    check("revA exit 1", lambda: exit_code(fa) == 1)
    check("waiver /BOOT_SEL -> 4 FAIL, 1 WAIVED",
          lambda: len(sevs(audit(A, {"/BOOT_SEL": "SoC internal pull-down"}), "FAIL")) == 4 and
          sevs(audit(A, {"/BOOT_SEL": "x"}), "WAIVED") == {"/BOOT_SEL"})

    def strong():
        n = copy.deepcopy(B)
        n["comps"]["R1"]["value"] = "680"
        return audit(n)
    check("R1=680R on 3.3V I2C -> FAIL Rp_min", lambda: "/I2C1_SCL" in sevs(strong(), "FAIL") and
          "Rp_min 967" in msg_of(strong(), "/I2C1_SCL"))

    def no_gnd_tp():
        n = copy.deepcopy(B)
        n["nets"]["GND"] = [nd for nd in n["nets"]["GND"] if not nd["ref"].startswith("TP")]
        return audit(n)
    check("GND without TP -> FAIL", lambda: "GND" in sevs(no_gnd_tp(), "FAIL"))
    check("report summary line", lambda: report(A, fa).splitlines()[-1].startswith("summary: 5 FAIL, 1 WARN"))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) >= 3 and argv[1] == "--audit":
        waivers = {}
        rest = argv[3:]
        while rest[:1] == ["--waive"] and len(rest) >= 2:
            k, _, v = rest[1].partition("=")
            waivers[k] = v or "waived"
            rest = rest[2:]
        path = argv[2] if os.path.isabs(argv[2]) else os.path.join(os.getcwd(), argv[2])
        with open(path, encoding="utf-8") as f:
            nl = load_netlist(f.read())
        findings = audit(nl, waivers)
        print(report(nl, findings))
        return exit_code(findings)
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
