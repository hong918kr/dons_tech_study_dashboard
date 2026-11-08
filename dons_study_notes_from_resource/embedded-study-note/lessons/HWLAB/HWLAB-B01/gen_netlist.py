#!/usr/bin/env python3
"""HWLAB-B01 합성 넷리스트 생성기 — 가상 보드 HWB-01 rev A(결함 심음) / rev B(수정판)

KiCad 8 의 S-expression 넷리스트(`(export (version "E") ...)`) 모양을 흉내 낸 파일을 만든다.
실제 KiCad 로 export 한 파일이 아니다 (이 맥에는 KiCad 가 없다). 부품·핀 이름은 가상이며,
24C02 EEPROM / 25-시리즈 SPI 플래시의 SOIC-8 핀 배치만 실제 부품과 같다.

  python3 -I gen_netlist.py        # hwb01_revA.net, hwb01_revB.net 를 다시 쓴다 (결정적 출력)

rev A 에 심은 결함 (감사 도구가 잡아야 하는 것):
  1. VDD_1V2 레일에 테스트 포인트 없음 (rev B: TP6 추가)
  2. /I2C2_SDA 풀업 R4 가 DNP (넷리스트엔 연결돼 있지만 실장 안 됨)
  3. /BOOT_SEL 스트랩 플로팅 (풀다운 R7 삭제, TP5 만 남음)
  4. INT 라벨 오타: U1 쪽 /IMU_INT, U7 쪽 /IMU_INT1 → 단일 노드 넷 두 개
  5. U6(EEPROM) WP 입력 미연결 (rev B: GND)
  +  /I2C1_SDA 에 확장 헤더용 풀업 R15 가 하나 더 (병렬) — 결함은 아님, 수치로 판정
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 부품 라이브러리: part -> (lib, description, [(pin, name, type)]) ----
PASSIVE2 = [("1", "~", "passive"), ("2", "~", "passive")]
LIB = {
    "R": ("Device", "Resistor", PASSIVE2),
    "C": ("Device", "Unpolarized capacitor", PASSIVE2),
    "L": ("Device", "Inductor", PASSIVE2),
    "TestPoint": ("Connector", "test point", [("1", "1", "passive")]),
    "SW_Push": ("Switch", "Push button switch", PASSIVE2),
    "Conn_01x02": ("Connector_Generic", "Generic connector", [("1", "Pin_1", "passive"), ("2", "Pin_2", "passive")]),
    "Conn_01x03": ("Connector_Generic", "Generic connector",
                   [("1", "Pin_1", "passive"), ("2", "Pin_2", "passive"), ("3", "Pin_3", "passive")]),
    "Conn_01x04": ("Connector_Generic", "Generic connector",
                   [("1", "Pin_1", "passive"), ("2", "Pin_2", "passive"),
                    ("3", "Pin_3", "passive"), ("4", "Pin_4", "passive")]),
    "HWB_SOC": ("hwb01", "Bring-up SoC (fictional)", [
        ("1", "VDDIO", "power_in"), ("2", "VDDIO", "power_in"), ("3", "VDDCORE", "power_in"),
        ("4", "VDDCORE", "power_in"), ("5", "VDDIO2", "power_in"), ("6", "VSS", "power_in"),
        ("7", "VSS", "power_in"), ("8", "NRST", "input"), ("9", "BOOT0", "input"),
        ("10", "BOOT_SEL", "input"), ("11", "I2C1_SCL", "bidirectional"), ("12", "I2C1_SDA", "bidirectional"),
        ("13", "I2C2_SCL", "bidirectional"), ("14", "I2C2_SDA", "bidirectional"), ("15", "IMU_INT", "input"),
        ("16", "TEMP_ALERT", "input"), ("17", "PA7", "bidirectional"), ("18", "SPI_SCLK", "output"),
        ("19", "SPI_MOSI", "output"), ("20", "SPI_MISO", "input"), ("21", "SPI_CS", "output"),
        ("22", "UART_TX", "output"), ("23", "UART_RX", "input"), ("24", "PG_IN", "input")]),
    "BUCK_5V_3V3": ("hwb01", "Buck 5V->3.3V (fictional)", [
        ("1", "VIN", "power_in"), ("2", "GND", "power_in"), ("3", "EN", "input"),
        ("4", "SW", "output"), ("5", "FB", "input"), ("6", "PG", "open_collector")]),
    "LDO_FIXED": ("hwb01", "Fixed-output LDO (fictional)", [
        ("1", "VIN", "power_in"), ("2", "GND", "power_in"), ("3", "EN", "input"), ("4", "VOUT", "power_out")]),
    "TEMP_I2C": ("hwb01", "I2C temperature sensor (fictional)", [
        ("1", "SCL", "input"), ("2", "GND", "power_in"), ("3", "ALERT", "open_collector"),
        ("4", "ADD0", "input"), ("5", "V+", "power_in"), ("6", "SDA", "bidirectional")]),
    "EEPROM_24C02": ("hwb01", "I2C EEPROM, 24C02 SOIC-8 pinout", [
        ("1", "A0", "input"), ("2", "A1", "input"), ("3", "A2", "input"), ("4", "GND", "power_in"),
        ("5", "SDA", "bidirectional"), ("6", "SCL", "input"), ("7", "WP", "input"), ("8", "VCC", "power_in")]),
    "IMU_1V8": ("hwb01", "6-axis IMU, 1.8V only (fictional)", [
        ("1", "VDD", "power_in"), ("2", "VDDIO", "power_in"), ("3", "GND", "power_in"), ("4", "SCL", "input"),
        ("5", "SDA", "bidirectional"), ("6", "INT1", "output"), ("7", "SDO_SA0", "input")]),
    "FLASH_SPI": ("hwb01", "SPI NOR flash, 25-series SOIC-8 pinout", [
        ("1", "~{CS}", "input"), ("2", "DO", "output"), ("3", "~{WP}", "input"), ("4", "GND", "power_in"),
        ("5", "DI", "input"), ("6", "CLK", "input"), ("7", "~{HOLD}", "input"), ("8", "VCC", "power_in")]),
}

FP = {"R": "Resistor_SMD:R_0402_1005Metric", "C": "Capacitor_SMD:C_0402_1005Metric",
      "L": "Inductor_SMD:L_0805_2012Metric", "TestPoint": "TestPoint:TestPoint_Pad_D1.0mm",
      "SW_Push": "Button_Switch_SMD:SW_SPST_PTS810", "Conn_01x02": "Connector_PinHeader_2.54mm:PinHeader_1x02",
      "Conn_01x03": "Connector_PinHeader_2.54mm:PinHeader_1x03",
      "Conn_01x04": "Connector_PinHeader_2.54mm:PinHeader_1x04"}


def board(rev):
    """(components, nets) 를 돌려준다. components: ref -> (part, value, dnp). nets: name -> [(ref, pin)]"""
    a = rev == "A"
    comps = {
        "U1": ("HWB_SOC", "HWB_SOC", False), "U2": ("BUCK_5V_3V3", "BUCK_5V_3V3", False),
        "U3": ("LDO_FIXED", "LDO_1V8", False), "U4": ("LDO_FIXED", "LDO_1V2", False),
        "U5": ("TEMP_I2C", "TEMP_I2C", False), "U6": ("EEPROM_24C02", "24C02", False),
        "U7": ("IMU_1V8", "IMU_1V8", False), "U8": ("FLASH_SPI", "W25Q_SPI", False),
        "J1": ("Conn_01x02", "PWR_IN_5V", False), "J2": ("Conn_01x04", "I2C1_EXP", False),
        "J3": ("Conn_01x03", "UART_DBG", False), "SW1": ("SW_Push", "RESET", False),
        "L1": ("L", "2.2u", False),
        "C1": ("C", "10u", False), "C2": ("C", "22u", False), "C3": ("C", "1u", False), "C4": ("C", "1u", False),
        "C5": ("C", "100n", False), "C6": ("C", "100n", False), "C7": ("C", "100n", False),
        "C8": ("C", "100n", False), "C9": ("C", "100n", False), "C10": ("C", "100n", False),
        "C11": ("C", "100n", False), "C12": ("C", "100n", False),
        "R1": ("R", "4k7", False), "R2": ("R", "4k7", False), "R3": ("R", "2k2", False),
        "R4": ("R", "2k2", a),                       # 결함 2: rev A 에선 DNP
        "R5": ("R", "10k", False), "R6": ("R", "10k", False), "R8": ("R", "10k", False),
        "R9": ("R", "100k", False), "R10": ("R", "33R", False), "R12": ("R", "10k", False),
        "TP1": ("TestPoint", "TP_5V", False), "TP2": ("TestPoint", "TP_3V3", False),
        "TP3": ("TestPoint", "TP_1V8", False), "TP4": ("TestPoint", "TP_NRST", False),
        "TP5": ("TestPoint", "TP_BOOT_SEL", False),
        "TP7": ("TestPoint", "TP_GND", False), "TP8": ("TestPoint", "TP_GND", False),
    }
    if a:
        comps["R15"] = ("R", "4k7", False)           # 확장 헤더 옆 중복 풀업
    else:
        comps["R7"] = ("R", "10k", False)            # BOOT_SEL 풀다운 복원
        comps["TP6"] = ("TestPoint", "TP_1V2", False)

    nets = {
        "+5V": [("J1", "1"), ("U2", "1"), ("U2", "3"), ("C1", "1"), ("TP1", "1")],
        "GND": [("J1", "2"), ("U2", "2"), ("U3", "2"), ("U4", "2"), ("U1", "6"), ("U1", "7"),
                ("U5", "2"), ("U5", "4"), ("U6", "1"), ("U6", "2"), ("U6", "3"), ("U6", "4"),
                ("U7", "3"), ("U7", "7"), ("U8", "4"), ("J2", "4"), ("J3", "3"), ("SW1", "2"),
                ("R6", "2"), ("TP7", "1"), ("TP8", "1")] + [("C%d" % i, "2") for i in range(1, 13)],
        "Net-(U2-SW)": [("U2", "4"), ("L1", "1")],
        "+3V3": [("L1", "2"), ("U2", "5"), ("C2", "1"), ("U3", "1"), ("U3", "3"), ("U4", "1"), ("U4", "3"),
                 ("U1", "1"), ("U1", "2"), ("C6", "1"), ("C7", "1"), ("U5", "5"), ("C9", "1"),
                 ("U6", "8"), ("C10", "1"), ("U8", "8"), ("U8", "3"), ("U8", "7"), ("C12", "1"),
                 ("R1", "2"), ("R2", "2"), ("R5", "2"), ("R8", "2"), ("R9", "2"), ("R12", "2"), ("J2", "1"),
                 ("TP2", "1")],
        "VDD_1V8": [("U3", "4"), ("C3", "1"), ("U1", "5"), ("U7", "1"), ("U7", "2"), ("C11", "1"),
                    ("R3", "2"), ("R4", "2"), ("TP3", "1")],
        "VDD_1V2": [("U4", "4"), ("C4", "1"), ("U1", "3"), ("U1", "4"), ("C8", "1")],
        "/NRST": [("U1", "8"), ("R5", "1"), ("C5", "1"), ("SW1", "1"), ("TP4", "1")],
        "/BOOT0": [("U1", "9"), ("R6", "1")],
        "/BOOT_SEL": [("U1", "10"), ("TP5", "1")],
        "/I2C1_SCL": [("U1", "11"), ("U5", "1"), ("U6", "6"), ("R1", "1"), ("J2", "2")],
        "/I2C1_SDA": [("U1", "12"), ("U5", "6"), ("U6", "5"), ("R2", "1"), ("J2", "3")],
        "/I2C2_SCL": [("U1", "13"), ("U7", "4"), ("R3", "1")],
        "/I2C2_SDA": [("U1", "14"), ("U7", "5"), ("R4", "1")],
        "/TEMP_ALERT": [("U1", "16"), ("U5", "3"), ("R8", "1")],
        "/PG_3V3": [("U2", "6"), ("U1", "24"), ("R9", "1")],
        "/SPI_SCLK": [("U1", "18"), ("U8", "6")],
        "/SPI_MOSI": [("U1", "19"), ("U8", "5")],
        "/SPI_MISO": [("U1", "20"), ("U8", "2")],
        "/SPI_CS": [("U1", "21"), ("U8", "1"), ("R12", "1")],
        "/UART_TX": [("U1", "22"), ("R10", "1")],
        "/UART_TX_J": [("R10", "2"), ("J3", "1")],
        "/UART_RX": [("U1", "23"), ("J3", "2")],
    }
    if a:
        nets["/IMU_INT"] = [("U1", "15")]            # 결함 4: 라벨 오타로 갈라진 넷
        nets["/IMU_INT1"] = [("U7", "6")]
        nets["+3V3"].append(("R15", "2"))
        nets["/I2C1_SDA"].append(("R15", "1"))
    else:
        nets["/IMU_INT"] = [("U1", "15"), ("U7", "6")]
        nets["VDD_1V2"].append(("TP6", "1"))
        nets["GND"] += [("U6", "7"), ("R7", "2")]
        nets["/BOOT_SEL"].append(("R7", "1"))
    return comps, nets


def q(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def natkey(ref):
    i = 0
    while i < len(ref) and not ref[i].isdigit():
        i += 1
    return (ref[:i], int(ref[i:]) if ref[i:].isdigit() else 0)


def render(rev):
    comps, nets = board(rev)
    pins_of = {}
    for ref, (part, _v, _d) in comps.items():
        pins_of[ref] = {num: (name, typ) for num, name, typ in LIB[part][2]}
    # 어떤 넷에도 안 붙은 핀 → KiCad 처럼 unconnected-(REF-NAME-PadN) 단일 노드 넷
    used = {(r, p) for nodes in nets.values() for r, p in nodes}
    for ref in sorted(comps, key=natkey):
        for num, (name, _t) in sorted(pins_of[ref].items(), key=lambda kv: int(kv[0])):
            if (ref, num) not in used:
                label = "%s-%s-Pad%s" % (ref, name, num) if name != "~" else "%s-Pad%s" % (ref, num)
                nets["unconnected-(%s)" % label] = [(ref, num)]

    out = ['(export (version "E")', "  (design",
           '    (source "hwb01.kicad_sch")', '    (date "2026-11-08T09:00:00")',
           '    (tool "gen_netlist.py (KiCad 8 E-format imitation)")',
           '    (sheet (number "1") (name "/") (tstamps "/")',
           "      (title_block (title %s) (company) (rev %s) (date) (source \"hwb01.kicad_sch\"))))"
           % (q("HWB-01 bring-up board"), q(rev)),
           "  (components"]
    for ref in sorted(comps, key=natkey):
        part, value, dnp = comps[ref]
        lib, desc, _pins = LIB[part]
        fp = FP.get(part, "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm" if ref.startswith("U") else "")
        out.append("    (comp (ref %s)" % q(ref))
        out.append("      (value %s)" % q(value))
        out.append("      (footprint %s)" % q(fp))
        out.append("      (libsource (lib %s) (part %s) (description %s))" % (q(lib), q(part), q(desc)))
        if dnp:
            out.append('      (property (name "dnp"))')
        out.append('      (property (name "Sheetname") (value "Root"))')
        out.append('      (sheetpath (names "/") (tstamps "/")))')
    out[-1] += ")"
    out.append("  (libparts)")
    out.append("  (nets")
    for code, name in enumerate(sorted(nets), start=1):
        out.append("    (net (code %s) (name %s) (class \"Default\")" % (q(str(code)), q(name)))
        nodes = sorted(nets[name], key=lambda rp: (natkey(rp[0]), int(rp[1])))
        for ref, pin in nodes:
            pname, ptype = pins_of[ref][pin]
            if name.startswith("unconnected-"):
                ptype += "+no_connect"
            fn = "" if pname == "~" else " (pinfunction %s)" % q(pname)
            out.append("      (node (ref %s) (pin %s)%s (pintype %s))" % (q(ref), q(pin), fn, q(ptype)))
        out[-1] += ")"
    out[-1] += "))"
    return "\n".join(out) + "\n"


def main():
    for rev in ("A", "B"):
        path = os.path.join(HERE, "hwb01_rev%s.net" % rev)
        text = render(rev)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print("wrote %s (%d bytes, %d lines)" % (os.path.basename(path), len(text.encode()), text.count("\n")))


if __name__ == "__main__":
    main()
