#!/usr/bin/env python3
"""BV03 PMBus status/telemetry decoder (stdlib only).

Bit names/positions follow the Linux kernel drivers/hwmon/pmbus/pmbus.h
(STATUS_WORD 0x79, STATUS_VOUT 0x7A, STATUS_IOUT 0x7B). Numeric formats:
  LINEAR11 (READ_IOUT 0x8C, READ_TEMPERATURE_1 0x8D, READ_VIN 0x88):
      bits[15:11] = signed 5-bit exponent N, bits[10:0] = signed 11-bit mantissa Y, X = Y * 2^N
  LINEAR16 (READ_VOUT 0x8B): X = unsigned16 * 2^N, N = signed 5-bit from VOUT_MODE (0x20) bits[4:0]

The register values in DUMPS are an [예시 출력] (made up) — on a real board they would come
from `i2ctransfer`/`i2cget -y <bus> <addr> 0x79 w` or the BMC/PMIC tool.
Usage: python3 -I pmbus_decode.py
"""

STATUS_WORD_BITS = {
    0: "NONE_OF_THE_ABOVE", 1: "CML (comm/memory/logic fault)", 2: "TEMPERATURE",
    3: "VIN_UV", 4: "IOUT_OC", 5: "VOUT_OV", 6: "OFF (output is off)", 7: "BUSY",
    8: "UNKNOWN", 9: "OTHER", 10: "FANS", 11: "POWER_GOOD# (PGOOD is NOT good)",
    12: "MFR specific", 13: "INPUT", 14: "IOUT/POUT", 15: "VOUT (see STATUS_VOUT)",
}
STATUS_VOUT_BITS = {3: "VIN_OFF/TON_MAX", 4: "VOUT_UV_FAULT", 5: "VOUT_UV_WARNING",
                    6: "VOUT_OV_WARNING", 7: "VOUT_OV_FAULT"}
STATUS_IOUT_BITS = {0: "POUT_OP_WARNING", 1: "POUT_OP_FAULT", 2: "POWER_LIMITING",
                    3: "CURRENT_SHARE_FAULT", 4: "IOUT_UC_FAULT", 5: "IOUT_OC_WARNING",
                    6: "IOUT_OC_LV_FAULT", 7: "IOUT_OC_FAULT"}


def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def linear11(raw):
    return sx(raw & 0x7FF, 11) * 2.0 ** sx(raw >> 11, 5)


def linear16(raw, vout_mode):
    return raw * 2.0 ** sx(vout_mode & 0x1F, 5)


def bits(val, table):
    return [f"b{b}:{n}" for b, n in sorted(table.items()) if val >> b & 1] or ["(none)"]


DUMPS = [  # [예시 출력]
    dict(rail="VDD_CORE_0V75 (healthy)", STATUS_WORD=0x0000, STATUS_VOUT=0x00, STATUS_IOUT=0x00,
         VOUT_MODE=0x14, READ_VOUT=0x0C00, READ_IOUT=0xDA4C, READ_TEMP=0xE9B8),
    dict(rail="VDD_DDR_1V05 (tripped)", STATUS_WORD=0x8850, STATUS_VOUT=0x10, STATUS_IOUT=0x80,
         VOUT_MODE=0x14, READ_VOUT=0x0000, READ_IOUT=0x0000, READ_TEMP=0xE9F0),
]

for d in DUMPS:
    print(f"== {d['rail']} ==")
    print(f"  STATUS_WORD 0x{d['STATUS_WORD']:04X}: {', '.join(bits(d['STATUS_WORD'], STATUS_WORD_BITS))}")
    print(f"  STATUS_VOUT 0x{d['STATUS_VOUT']:02X}  : {', '.join(bits(d['STATUS_VOUT'], STATUS_VOUT_BITS))}")
    print(f"  STATUS_IOUT 0x{d['STATUS_IOUT']:02X}  : {', '.join(bits(d['STATUS_IOUT'], STATUS_IOUT_BITS))}")
    n = sx(d["VOUT_MODE"] & 0x1F, 5)
    print(f"  VOUT_MODE 0x{d['VOUT_MODE']:02X} -> LINEAR16 exponent N={n}")
    print(f"  READ_VOUT 0x{d['READ_VOUT']:04X} -> {linear16(d['READ_VOUT'], d['VOUT_MODE']):.4f} V")
    print(f"  READ_IOUT 0x{d['READ_IOUT']:04X} -> {linear11(d['READ_IOUT']):.3f} A")
    print(f"  READ_TEMP 0x{d['READ_TEMP']:04X} -> {linear11(d['READ_TEMP']):.2f} C")
