#!/usr/bin/env python3
"""MAVLink v2 frame encoder/decoder (stdlib only) — R04 companion example.

What this shows (the same things pymavlink / MAVSDK do under the hood):
  1. CRC-16/MCRF4XX (a.k.a. X.25 "accumulate" used by MAVLink)
  2. CRC_EXTRA: a per-message seed byte derived from the XML message definition
     (name + wire-ordered field types/names). It makes both ends reject frames
     whose message *definition* differs, not only corrupted bytes.
  3. MAVLink v2 framing: 0xFD STX, len, incompat, compat, seq, sysid, compid,
     msgid (24-bit little endian), payload (trailing zeros truncated), crc16.
  4. A byte-stream parser with resync (garbage, split reads, bad CRC).

Not covered: message signing (incompat flag 0x01), MAVLink v1 frames.

Run:  python3 mavlink_v2_heartbeat_codec.py      (runs the self-test)
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

STX_V2 = 0xFD
HEADER_LEN = 10  # STX..msgid inclusive
CRC_LEN = 2

# ---------------------------------------------------------------- CRC ------

def crc_accumulate(byte: int, crc: int) -> int:
    """One step of MAVLink's crc_accumulate (CRC-16/MCRF4XX, poly 0x1021 reflected)."""
    tmp = byte ^ (crc & 0xFF)
    tmp = (tmp ^ (tmp << 4)) & 0xFF
    return ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF


def crc16_mcrf4xx(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc = crc_accumulate(b, crc)
    return crc


# Size of each MAVLink base type, used to compute wire order (largest first).
TYPE_SIZE = {
    "uint64_t": 8, "int64_t": 8, "double": 8,
    "uint32_t": 4, "int32_t": 4, "float": 4,
    "uint16_t": 2, "int16_t": 2,
    "uint8_t": 1, "int8_t": 1, "char": 1,
}
STRUCT_CODE = {
    "uint64_t": "Q", "int64_t": "q", "double": "d",
    "uint32_t": "I", "int32_t": "i", "float": "f",
    "uint16_t": "H", "int16_t": "h",
    "uint8_t": "B", "int8_t": "b", "char": "c",
}


@dataclass(frozen=True)
class Field:
    type: str      # base type, e.g. "uint8_t"
    name: str
    array_len: int = 0


@dataclass(frozen=True)
class MessageDef:
    msgid: int
    name: str
    fields_xml_order: Tuple[Field, ...]   # order as written in common.xml (no extensions)

    @property
    def wire_fields(self) -> List[Field]:
        # Stable sort by element size, descending (this is MAVLink's wire order rule).
        return sorted(self.fields_xml_order, key=lambda f: TYPE_SIZE[f.type], reverse=True)

    @property
    def crc_extra(self) -> int:
        crc = crc16_mcrf4xx((self.name + " ").encode())
        for f in self.wire_fields:
            crc = crc16_mcrf4xx((f.type + " ").encode(), crc)
            crc = crc16_mcrf4xx((f.name + " ").encode(), crc)
            if f.array_len:
                crc = crc_accumulate(f.array_len, crc)
        return (crc & 0xFF) ^ (crc >> 8)

    @property
    def struct_fmt(self) -> str:
        parts = []
        for f in self.wire_fields:
            code = STRUCT_CODE[f.type]
            parts.append(f"{f.array_len}{code}" if f.array_len else code)
        return "<" + "".join(parts)

    @property
    def payload_len(self) -> int:
        return struct.calcsize(self.struct_fmt)


# Definitions copied from mavlink common.xml / minimal.xml (base fields only).
HEARTBEAT = MessageDef(0, "HEARTBEAT", (
    Field("uint8_t", "type"),
    Field("uint8_t", "autopilot"),
    Field("uint8_t", "base_mode"),
    Field("uint32_t", "custom_mode"),
    Field("uint8_t", "system_status"),
    Field("uint8_t", "mavlink_version"),   # uint8_t_mavlink_version in XML; CRC uses "uint8_t"
))
COMMAND_LONG = MessageDef(76, "COMMAND_LONG", (
    Field("uint8_t", "target_system"),
    Field("uint8_t", "target_component"),
    Field("uint16_t", "command"),
    Field("uint8_t", "confirmation"),
    Field("float", "param1"), Field("float", "param2"), Field("float", "param3"),
    Field("float", "param4"), Field("float", "param5"), Field("float", "param6"),
    Field("float", "param7"),
))
MESSAGES: Dict[int, MessageDef] = {m.msgid: m for m in (HEARTBEAT, COMMAND_LONG)}

# ------------------------------------------------------------- encode ------

def encode(msg: MessageDef, values: Dict[str, object], seq: int = 0,
           sysid: int = 1, compid: int = 1) -> bytes:
    ordered = [values[f.name] for f in msg.wire_fields]
    payload = struct.pack(msg.struct_fmt, *ordered)
    # MAVLink 2: strip trailing zero bytes, but keep at least one byte.
    trimmed = payload.rstrip(b"\x00") or payload[:1]
    header = bytes([
        STX_V2, len(trimmed), 0, 0, seq & 0xFF, sysid, compid,
        msg.msgid & 0xFF, (msg.msgid >> 8) & 0xFF, (msg.msgid >> 16) & 0xFF,
    ])
    crc = crc16_mcrf4xx(header[1:] + trimmed)          # STX is excluded
    crc = crc_accumulate(msg.crc_extra, crc)
    return header + trimmed + struct.pack("<H", crc)

# ------------------------------------------------------------- decode ------

@dataclass
class Decoded:
    msgid: int
    name: str
    seq: int
    sysid: int
    compid: int
    fields: Dict[str, object]


class StreamParser:
    """Feed arbitrary byte chunks; collect complete, CRC-valid frames."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.stats = {"ok": 0, "bad_crc": 0, "unknown_msgid": 0, "skipped_bytes": 0}

    def feed(self, chunk: bytes) -> List[Decoded]:
        self.buf += chunk
        out: List[Decoded] = []
        while True:
            # 1) find STX
            idx = self.buf.find(bytes([STX_V2]))
            if idx < 0:
                self.stats["skipped_bytes"] += len(self.buf)
                self.buf.clear()
                return out
            if idx > 0:
                self.stats["skipped_bytes"] += idx
                del self.buf[:idx]
            # 2) need full header
            if len(self.buf) < HEADER_LEN:
                return out
            plen = self.buf[1]
            incompat = self.buf[2]
            sig_len = 13 if incompat & 0x01 else 0
            total = HEADER_LEN + plen + CRC_LEN + sig_len
            # 3) partial read: wait for more bytes
            if len(self.buf) < total:
                return out
            frame = bytes(self.buf[:total])
            msgid = frame[7] | (frame[8] << 8) | (frame[9] << 16)
            mdef = MESSAGES.get(msgid)
            if mdef is None:
                # Can't verify CRC without CRC_EXTRA: drop 1 byte and resync.
                self.stats["unknown_msgid"] += 1
                del self.buf[:1]
                continue
            crc_rx = struct.unpack_from("<H", frame, HEADER_LEN + plen)[0]
            crc = crc16_mcrf4xx(frame[1:HEADER_LEN + plen])
            crc = crc_accumulate(mdef.crc_extra, crc)
            if crc != crc_rx:
                # Bad CRC: drop only the STX byte (a real frame may start inside).
                self.stats["bad_crc"] += 1
                del self.buf[:1]
                continue
            payload = frame[HEADER_LEN:HEADER_LEN + plen]
            payload = payload + b"\x00" * (mdef.payload_len - len(payload))  # undo truncation
            vals = struct.unpack(mdef.struct_fmt, payload[:mdef.payload_len])
            fields = {f.name: v for f, v in zip(mdef.wire_fields, vals)}
            out.append(Decoded(msgid, mdef.name, frame[4], frame[5], frame[6], fields))
            self.stats["ok"] += 1
            del self.buf[:total]

# ----------------------------------------------------------- self-test -----

def _self_test() -> None:
    # CRC-16/MCRF4XX catalogue check value.
    assert crc16_mcrf4xx(b"123456789") == 0x6F91, hex(crc16_mcrf4xx(b"123456789"))
    # CRC_EXTRA values published in MAVLink headers: HEARTBEAT=50, COMMAND_LONG=152.
    assert HEARTBEAT.crc_extra == 50, HEARTBEAT.crc_extra
    assert COMMAND_LONG.crc_extra == 152, COMMAND_LONG.crc_extra
    assert HEARTBEAT.payload_len == 9 and COMMAND_LONG.payload_len == 33

    hb_vals = {"type": 2, "autopilot": 12, "base_mode": 0x81, "custom_mode": 0x00030000,
               "system_status": 4, "mavlink_version": 3}   # quadrotor, PX4, armed|custom
    hb = encode(HEARTBEAT, hb_vals, seq=7, sysid=1, compid=1)
    assert hb[0] == 0xFD and hb[1] == 9 and len(hb) == 10 + 9 + 2

    # COMMAND_LONG arm (MAV_CMD_COMPONENT_ARM_DISARM=400, param1=1): trailing zeros get trimmed
    arm_vals = {"target_system": 1, "target_component": 1, "command": 400, "confirmation": 0,
                "param1": 1.0, "param2": 0.0, "param3": 0.0, "param4": 0.0,
                "param5": 0.0, "param6": 0.0, "param7": 0.0}
    arm = encode(COMMAND_LONG, arm_vals, seq=8, sysid=255, compid=190)
    assert arm[1] < 33, "payload should be truncated"

    # Stream: garbage + split frame + corrupted frame + good frame.
    bad = bytearray(hb)
    bad[12] ^= 0xFF
    stream = b"\x00\x11\xfd\x22" + hb[:5]
    p = StreamParser()
    got = p.feed(stream)
    assert got == []                                  # partial read: nothing yet
    got += p.feed(hb[5:] + bytes(bad) + arm)
    names = [d.name for d in got]
    assert names == ["HEARTBEAT", "COMMAND_LONG"], names
    assert got[0].fields["custom_mode"] == 0x00030000 and got[0].seq == 7
    assert got[1].fields["command"] == 400 and got[1].fields["param1"] == 1.0
    assert got[1].fields["param7"] == 0.0             # restored after truncation
    assert p.stats["bad_crc"] >= 1
    print("hb  :", hb.hex(" "))
    print("arm :", arm.hex(" "))
    print("stats:", p.stats)
    print("self-test OK")


if __name__ == "__main__":
    _self_test()
