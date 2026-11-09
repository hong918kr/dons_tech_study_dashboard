#!/usr/bin/env python3
"""SIV-A02 가상 보드 VAL-R2 데이터 생성기 (결정적: 난수를 쓰지 않는다)

  python3 -I gen_board.py      # u1_sns_soc.bsd, u2_io_fpga.bsd, u3_lvl_buf.bsd, board.net 을 이 디렉터리에 쓴다

BSDL 은 IEEE 1149.1 문법의 부분집합으로 쓴 '가상 부품' 이다. 실제 제품의 BSDL 이 아니다.
체인: TDI → U1(SNS_SOC) → U2(IO_FPGA) → U3(LVL_BUF) → TDO
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def emit_bsdl(name, ports, pinmap, ir_len, opcodes, cells):
    """ports: (이름, 방향, 폭 또는 None, downto 여부) · pinmap: (포트, 핀 또는 핀 리스트) · cells: 셀 문자열 (번호 제외)"""
    out = ["-- 가상 부품 %s (SIV-A02 실습용, gen_board.py 가 생성)" % name,
           "entity %s is" % name,
           '  generic (PHYSICAL_PIN_MAP : string := "PKG");',
           "  port ("]
    decl = []
    for pname, direction, width, down in ports:
        if width is None:
            decl.append("    %s : %s bit" % (pname, direction))
        elif down:
            decl.append("    %s : %s bit_vector(%d downto 0)" % (pname, direction, width - 1))
        else:
            decl.append("    %s : %s bit_vector(0 to %d)" % (pname, direction, width - 1))
    out.append(";\n".join(decl))
    out.append("  );")
    out.append("  use STD_1149_1_2001.all;")
    out.append("  attribute COMPONENT_CONFORMANCE of %s : entity is \"STD_1149_1_2001\";" % name)
    out.append("  attribute PIN_MAP of %s : entity is PHYSICAL_PIN_MAP;" % name)
    items = []
    for port, pin in pinmap:
        if isinstance(pin, list):
            items.append("%s:(%s)" % (port, ",".join(str(x) for x in pin)))
        else:
            items.append("%s:%s" % (port, pin))
    out.append("  constant PKG : PIN_MAP_STRING :=")
    lines = []
    for i in range(0, len(items), 6):
        lines.append('    "' + ", ".join(items[i:i + 6]) + (', " &' if i + 6 < len(items) else '"'))
    out.append("\n".join(lines) + ";")
    out.append('  attribute TAP_SCAN_IN of TDI : signal is true;')
    out.append('  attribute TAP_SCAN_OUT of TDO : signal is true;')
    out.append('  attribute TAP_SCAN_MODE of TMS : signal is true;')
    out.append('  attribute TAP_SCAN_CLOCK of TCK : signal is (10.0e6, BOTH);')
    out.append("  attribute INSTRUCTION_LENGTH of %s : entity is %d;" % (name, ir_len))
    out.append("  attribute INSTRUCTION_OPCODE of %s : entity is" % name)
    ops = list(opcodes.items())
    out.append("\n".join('    "%s (%s)%s"%s' % (k, v, "," if i + 1 < len(ops) else "", " &" if i + 1 < len(ops) else "")
                         for i, (k, v) in enumerate(ops)) + ";")
    out.append("  attribute BOUNDARY_LENGTH of %s : entity is %d;" % (name, len(cells)))
    out.append("  attribute BOUNDARY_REGISTER of %s : entity is" % name)
    out.append("    -- num (cell, port, function, safe [, ccell, disval, rslt])")
    body = []
    for i, c in enumerate(cells):
        body.append('    "%d (%s)%s"%s' % (i, c, "," if i + 1 < len(cells) else "", " &" if i + 1 < len(cells) else ""))
    out.append("\n".join(body) + ";")
    out.append("end %s;" % name)
    return "\n".join(out) + "\n"


def u1():
    ports = [("TCK", "in", None, False), ("TMS", "in", None, False), ("TDI", "in", None, False), ("TDO", "out", None, False),
             ("D", "inout", 16, False), ("A", "out", 8, False), ("nCS", "out", None, False), ("nWE", "out", None, False),
             ("nOE", "out", None, False), ("IRQ", "in", None, False), ("PMIC_EN", "out", None, False),
             ("RST_N", "in", None, False)]
    pinmap = [("TCK", 1), ("TMS", 2), ("TDI", 3), ("TDO", 4), ("D", list(range(10, 26))), ("A", list(range(30, 38))),
              ("nCS", 40), ("nWE", 41), ("nOE", 42), ("IRQ", 43), ("PMIC_EN", 44), ("RST_N", 45)]
    cells = []

    def add(s):
        cells.append(s)
        return len(cells) - 1
    # 데이터 버스: 바이트당 제어 셀 1개를 공유한다 (disable value 0)
    for lo, hi in ((0, 8), (8, 16)):
        c = add("BC_1, *, control, 0")
        for i in range(lo, hi):
            add("BC_1, D(%d), output3, X, %d, 0, Z" % (i, c))
            add("BC_1, D(%d), input, X" % i)
    c = add("BC_1, *, control, 0")
    for i in range(8):
        add("BC_1, A(%d), output3, X, %d, 0, Z" % (i, c))
    c = add("BC_1, *, control, 0")
    for p in ("nCS", "nWE", "nOE"):
        add("BC_1, %s, output3, X, %d, 0, Z" % (p, c))
    add("BC_1, IRQ, input, X")
    c = add("BC_1, *, control, 0")
    add("BC_1, PMIC_EN, output3, 1, %d, 0, Z" % c)
    add("BC_1, RST_N, input, X")
    add("BC_1, *, internal, X")
    return emit_bsdl("SNS_SOC", ports, pinmap, 4,
                     {"BYPASS": "1111", "EXTEST": "0000", "SAMPLE": "0001", "PRELOAD": "0001", "IDCODE": "0010"}, cells)


def u2():
    ports = [("TCK", "in", None, False), ("TMS", "in", None, False), ("TDI", "in", None, False), ("TDO", "out", None, False),
             ("D", "inout", 16, False), ("A", "in", 8, False), ("nCS", "in", None, False), ("nWE", "in", None, False),
             ("nOE", "in", None, False), ("Y", "in", 8, False), ("IRQ", "out", None, False),
             ("RST_OUT", "out", None, False), ("CLK", "in", None, False), ("SPARE", "inout", 4, False)]
    pinmap = [("TCK", "A1"), ("TMS", "A2"), ("TDI", "A3"), ("TDO", "A4"),
              ("D", ["B%d" % i for i in range(1, 17)]), ("A", ["C%d" % i for i in range(1, 9)]),
              ("nCS", "D1"), ("nWE", "D2"), ("nOE", "D3"), ("Y", ["E%d" % i for i in range(1, 9)]),
              ("IRQ", "F1"), ("RST_OUT", "F2"), ("CLK", "F3"), ("SPARE", ["G1", "G2", "G3", "G4"])]
    cells = []

    def add(s):
        cells.append(s)
        return len(cells) - 1
    # 핀마다 제어 셀 + BC_7 양방향 셀 1개. disable value 는 1 (U1 과 극성이 반대)
    for i in range(16):
        c = add("BC_1, *, control, 1")
        add("BC_7, D(%d), bidir, X, %d, 1, Z" % (i, c))
    for i in range(8):
        add("BC_1, A(%d), input, X" % i)
    for p in ("nCS", "nWE", "nOE"):
        add("BC_1, %s, input, X" % p)
    for i in range(8):
        add("BC_1, Y(%d), input, X" % i)
    add("BC_1, IRQ, output2, 1")
    c = add("BC_1, *, control, 1")
    add("BC_1, RST_OUT, output3, 1, %d, 1, Z" % c)
    add("BC_4, CLK, clock, X")
    for i in range(4):
        c = add("BC_1, *, control, 1")
        add("BC_7, SPARE(%d), bidir, X, %d, 1, Z" % (i, c))
    return emit_bsdl("IO_FPGA", ports, pinmap, 6,
                     {"BYPASS": "111111", "EXTEST": "100110", "SAMPLE": "000101", "PRELOAD": "000101",
                      "IDCODE": "001001"}, cells)


def u3():
    ports = [("TCK", "in", None, False), ("TMS", "in", None, False), ("TDI", "in", None, False), ("TDO", "out", None, False),
             ("A", "in", 8, True), ("B", "out", 8, True)]
    pinmap = [("TCK", 1), ("TMS", 2), ("TDI", 3), ("TDO", 4),
              ("A", list(range(12, 4, -1))), ("B", list(range(20, 12, -1)))]
    cells = []

    def add(s):
        cells.append(s)
        return len(cells) - 1
    c = add("BC_1, *, control, 0")  # B 8핀이 OE 하나를 공유
    for i in range(8):
        add("BC_1, B(%d), output3, X, %d, 0, Z" % (i, c))
    for i in range(8):
        add("BC_1, A(%d), input, X" % i)
    return emit_bsdl("LVL_BUF", ports, pinmap, 3,
                     {"BYPASS": "111", "EXTEST": "000", "SAMPLE": "001", "PRELOAD": "001"}, cells)


def netlist():
    ln = ["# VAL-R2 센서 캐리어 보드 넷리스트 (가상)",
          "# 형식: NET  ref.pin ...  [pull=0|1]   ·  !hold NET 값  = 패턴 금지 넷",
          "# U1/U2/U3 만 BSDL 이 있다. R*, X*, TP*, U7 은 스캔이 없는 부품."]
    for i in range(16):
        ln.append("DATA%d U1.%d U2.B%d pull=1" % (i, 10 + i, i + 1))
    for i in range(8):
        ln.append("ADDR%d U1.%d U2.C%d U3.%d" % (i, 30 + i, i + 1, 12 - i))
    ln.append("nCS U1.40 U2.D1 R10.1 pull=1")
    ln.append("nWE U1.41 U2.D2 R11.1 pull=1")
    ln.append("nOE U1.42 U2.D3 R12.1 pull=1")
    for i in range(8):
        ln.append("BUF%d U3.%d U2.E%d" % (i, 20 - i, i + 1))
    ln.append("IRQ_N U2.F1 U1.43 R21.1 pull=1")
    ln.append("RST_N U2.F2 U1.45 R22.1 pull=1")
    ln.append("PMIC_EN U1.44 U7.2")
    ln.append("CLK_25M X1.3 U2.F3")
    ln.append("TP_VSENSE TP4.1 R30.2")
    ln.append("!hold RST_N 1")
    ln.append("!hold PMIC_EN 1")
    return "\n".join(ln) + "\n"


def main():
    for fn, text in (("u1_sns_soc.bsd", u1()), ("u2_io_fpga.bsd", u2()), ("u3_lvl_buf.bsd", u3()), ("board.net", netlist())):
        with open(os.path.join(HERE, fn), "w", encoding="utf-8") as f:
            f.write(text)
        print("wrote %-16s %6d bytes" % (fn, len(text.encode("utf-8"))))


if __name__ == "__main__":
    main()
