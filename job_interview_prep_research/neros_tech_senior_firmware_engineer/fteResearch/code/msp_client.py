#!/usr/bin/env python3
"""MSP v1 / v2 client sketch for Betaflight-style flight controllers (stdlib only).

Framing (verified against betaflight src/main/msp/msp_serial.c, msp_serial.h, common/crc.c):

  MSP v1 request : '$' 'M' '<' size(u8) cmd(u8) payload[size] xor8(size, cmd, payload)
  MSP v1 reply   : '$' 'M' '>' ...   error reply: '$' 'M' '!' ...
                   (FC -> host only: size == 255 means a JUMBO frame; a u16 LE size follows cmd)
  MSP v2 native  : '$' 'X' '<' flags(u8) cmd(u16 LE) size(u16 LE) payload crc8_dvb_s2(flags..payload)
  MSP v2 over v1 : v1 frame with cmd = 255 (MSP_V2_FRAME_ID) whose payload is a whole v2 body
                   (flags, cmd, size, payload, crc8) and the outer v1 xor checksum on top.

CRC-8/DVB-S2: poly 0xD5, init 0x00, no reflection, no xorout. Check value of b"123456789" = 0xBC.

Transport is any object with sendall()/recv() (TCP socket to Betaflight SITL, or a pyserial
wrapper). Betaflight SITL binds UARTx to tcp://127.0.0.1:576x (src/platform/SIMULATOR/target/SITL/README.md);
the upstream harness src/test/sitl/sitl_harness.py uses port 5761.

Run `python3 -I msp_client.py` to execute the self-test (no hardware needed).
"""

import socket
import struct
import threading

# --- command IDs (betaflight src/main/msp/msp_protocol.h, API 1.49 at the time of writing) ---
MSP_API_VERSION = 1
MSP_FC_VARIANT = 2
MSP_STATUS = 101
MSP_MOTOR = 104
MSP_RC = 105
MSP_ATTITUDE = 108
MSP_STATUS_EX = 150
MSP_SET_RAW_RC = 200
MSP_SET_MOTOR = 214
MSP_V2_FRAME_ID = 255
# MSP v2-only IDs (msp_protocol_v2_betaflight.h)
MSP2_CLI_COMMAND = 0x3012


def crc8_dvb_s2(data, crc=0):
    """Bitwise CRC-8/DVB-S2 identical to betaflight crc8_calc(crc, a, 0xD5)."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def xor8(data):
    c = 0
    for b in data:
        c ^= b
    return c


# ----------------------------------------------------------------- encoders
def encode_v1(cmd, payload=b"", direction=b"<"):
    if not 0 <= cmd <= 255:
        raise ValueError("v1 cmd must fit in u8")
    if len(payload) > 254:
        raise ValueError("v1 request payload must be <= 254 bytes (255 means JUMBO)")
    body = bytes([len(payload), cmd]) + payload
    return b"$M" + direction + body + bytes([xor8(body)])


def encode_v2(cmd, payload=b"", flags=0, direction=b"<"):
    body = struct.pack("<BHH", flags, cmd, len(payload)) + payload
    return b"$X" + direction + body + bytes([crc8_dvb_s2(body)])


def encode_v2_over_v1(cmd, payload=b"", flags=0):
    inner = struct.pack("<BHH", flags, cmd, len(payload)) + payload
    inner += bytes([crc8_dvb_s2(inner)])
    return encode_v1(MSP_V2_FRAME_ID, inner)


# ----------------------------------------------------------------- stream parser
class MspFrame:
    __slots__ = ("version", "direction", "cmd", "flags", "payload")

    def __init__(self, version, direction, cmd, flags, payload):
        self.version, self.direction, self.cmd, self.flags, self.payload = version, direction, cmd, flags, payload

    @property
    def is_error(self):
        return self.direction == "!"

    def __repr__(self):
        return f"MspFrame(v{self.version}, '{self.direction}', cmd={self.cmd}, len={len(self.payload)})"


class MspParser:
    """Byte-stream parser. Feed arbitrary chunks; returns complete frames.

    Resync rule: on any bad header / bad checksum drop ONE byte and search for the next '$'
    (so a real frame hidden inside garbage is not lost). Counters let tests assert behaviour.
    """

    MAX_PAYLOAD = 4096

    def __init__(self):
        self.buf = bytearray()
        self.crc_errors = 0
        self.resync_bytes = 0

    def feed(self, data):
        self.buf += data
        frames = []
        while True:
            r = self._try_one()
            if r is None:
                break
            if r is not False:
                frames.append(r)
        return frames

    def _drop(self, n=1):
        del self.buf[:n]
        self.resync_bytes += n

    def _try_one(self):
        # returns frame, False (consumed garbage, keep going) or None (need more data)
        start = self.buf.find(b"$")
        if start < 0:
            if self.buf:
                self._drop(len(self.buf))
            return None
        if start:
            self._drop(start)
        if len(self.buf) < 3:
            return None
        proto, direction = self.buf[1:2], self.buf[2:3]
        if proto not in (b"M", b"X") or direction not in (b"<", b">", b"!"):
            self._drop(1)
            return False
        d = direction.decode()
        if proto == b"M":
            return self._v1(d)
        return self._v2(d)

    def _v1(self, d):
        if len(self.buf) < 5:
            return None
        size, cmd = self.buf[3], self.buf[4]
        hdr = 5
        if size == 255 and d != "<":           # JUMBO (FC -> host)
            if len(self.buf) < 7:
                return None
            size = struct.unpack_from("<H", self.buf, 5)[0]
            hdr = 7
        if size > self.MAX_PAYLOAD:
            self._drop(1)
            return False
        end = hdr + size + 1
        if len(self.buf) < end:
            return None
        payload = bytes(self.buf[hdr:hdr + size])
        if xor8(self.buf[3:hdr + size]) != self.buf[end - 1]:
            self.crc_errors += 1
            self._drop(1)
            return False
        del self.buf[:end]
        if cmd == MSP_V2_FRAME_ID and len(payload) >= 6:   # v2 over v1
            flags, cmd2, size2 = struct.unpack_from("<BHH", payload, 0)
            if 5 + size2 + 1 == len(payload) and crc8_dvb_s2(payload[:-1]) == payload[-1]:
                return MspFrame(2, d, cmd2, flags, payload[5:5 + size2])
            self.crc_errors += 1
            return False
        return MspFrame(1, d, cmd, 0, payload)

    def _v2(self, d):
        if len(self.buf) < 8:
            return None
        flags, cmd, size = struct.unpack_from("<BHH", self.buf, 3)
        if size > self.MAX_PAYLOAD:
            self._drop(1)
            return False
        end = 8 + size + 1
        if len(self.buf) < end:
            return None
        if crc8_dvb_s2(self.buf[3:8 + size]) != self.buf[end - 1]:
            self.crc_errors += 1
            self._drop(1)
            return False
        payload = bytes(self.buf[8:8 + size])
        del self.buf[:end]
        return MspFrame(2, d, cmd, flags, payload)


# ----------------------------------------------------------------- payload helpers
def decode_status(payload):
    """MSP_STATUS / MSP_STATUS_EX common prefix (betaflight msp.c case MSP_STATUS)."""
    cycle_us, i2c_err, sensors, mode_flags, pid_profile, load = struct.unpack_from("<HHHIBH", payload, 0)
    out = {"cycle_time_us": cycle_us, "i2c_errors": i2c_err, "sensors": sensors,
           "mode_flags_low32": mode_flags, "pid_profile": pid_profile, "cpu_load_pct": load}
    off = 13 + 2                      # 13-byte prefix (..., u16 load), then 2 bytes (STATUS: u16 gyro cycle / EX: 2x u8 counts)
    if len(payload) > off:
        extra = payload[off] & 0x0F   # number of extra flight-mode-flag bytes
        off += 1 + extra
        if len(payload) >= off + 5:
            out["arming_flag_count"] = payload[off]
            out["arming_disable_flags"] = struct.unpack_from("<I", payload, off + 1)[0]
    return out


def decode_u16_list(payload):
    return list(struct.unpack("<%dH" % (len(payload) // 2), payload[: len(payload) // 2 * 2]))


def decode_attitude(payload):
    roll, pitch, yaw = struct.unpack_from("<hhh", payload, 0)
    return {"roll_deg": roll / 10.0, "pitch_deg": pitch / 10.0, "yaw_deg": float(yaw)}


def encode_raw_rc(channels_us):
    return struct.pack("<%dH" % len(channels_us), *channels_us)


ARMING_DISABLE_NAMES = [  # betaflight src/main/fc/runtime_config.c armingDisableFlagNames[] order
    "NOGYRO", "FAILSAFE", "RXLOSS", "NOT_DISARMED", "BOXFAILSAFE", "RUNAWAY", "CRASH", "THROTTLE",
    "ANGLE", "BOOTGRACE", "NOPREARM", "LOAD", "CALIB", "CLI", "CMS", "BST", "MSP", "PARALYZE", "GPS",
    "RESCUE_SW", "DSHOT_TELEM", "REBOOT_REQD", "DSHOT_BBANG", "NO_ACC_CAL", "MOTOR_PROTO",
    "FLIP_SWITCH", "ALT_HOLD_SW", "POS_HOLD_SW", "AUTOPILOT_SW", "ARM_SWITCH",
]


def arming_flag_names(mask):
    return [n for i, n in enumerate(ARMING_DISABLE_NAMES) if mask & (1 << i)]


# ----------------------------------------------------------------- client
class MspClient:
    def __init__(self, transport, version=1, timeout=1.0):
        self.t = transport
        self.version = version
        self.timeout = timeout
        self.parser = MspParser()
        self.lock = threading.Lock()

    def request(self, cmd, payload=b""):
        if self.version == 2 or cmd > 255:
            frame = encode_v2(cmd, payload)
        else:
            frame = encode_v1(cmd, payload)
        with self.lock:
            self.t.sendall(frame)
            self.t.settimeout(self.timeout)
            while True:
                try:
                    data = self.t.recv(4096)
                except socket.timeout:
                    raise TimeoutError(f"no MSP reply for cmd {cmd}")
                if not data:
                    raise ConnectionError("transport closed")
                for f in self.parser.feed(data):
                    if f.cmd == cmd:
                        if f.is_error:
                            raise RuntimeError(f"MSP error reply for cmd {cmd}")
                        return f.payload

    def status(self):
        return decode_status(self.request(MSP_STATUS))

    def rc(self):
        return decode_u16_list(self.request(MSP_RC))

    def motors(self):
        return decode_u16_list(self.request(MSP_MOTOR))

    def set_raw_rc(self, channels_us):
        # Only takes effect when the RX provider is MSP (USE_RX_MSP) or MSP override is active.
        return self.request(MSP_SET_RAW_RC, encode_raw_rc(channels_us))

    def set_motor(self, values):
        # Writes motor_disarmed[] -> only spins motors while DISARMED (motor-test path). Props off!
        return self.request(MSP_SET_MOTOR, encode_raw_rc(values))


# ----------------------------------------------------------------- self-test
def _fake_fc(sock, stop):
    """Tiny FC emulator answering a few commands, replying in the same MSP version."""
    p = MspParser()
    status = struct.pack("<HHHIBH", 125, 0, 0x21, 0, 0, 7) + struct.pack("<H", 0)
    status += bytes([0]) + bytes([30]) + struct.pack("<I", (1 << 2) | (1 << 9)) + bytes([0])
    motors = [0] * 8
    while not stop.is_set():
        try:
            data = sock.recv(4096)
        except OSError:
            return
        if not data:
            return
        for f in p.feed(data):
            if f.cmd == MSP_STATUS:
                resp = status
            elif f.cmd == MSP_RC:
                resp = encode_raw_rc([1500, 1500, 1000, 1500])
            elif f.cmd == MSP_SET_MOTOR:
                motors[: len(f.payload) // 2] = decode_u16_list(f.payload)
                resp = b""
            elif f.cmd == MSP_MOTOR:
                resp = encode_raw_rc(motors)
            elif f.cmd == MSP2_CLI_COMMAND:
                resp = b"ok"
            else:
                out = encode_v1(f.cmd, b"", b"!") if f.version == 1 else encode_v2(f.cmd, b"", 0, b"!")
                sock.sendall(out)
                continue
            out = encode_v1(f.cmd, resp, b">") if f.version == 1 else encode_v2(f.cmd, resp, 0, b">")
            # noise before the reply to exercise resync
            sock.sendall(b"\x00$Z" + out)


def _self_test():
    # 1. CRC check value
    assert crc8_dvb_s2(b"123456789") == 0xBC, hex(crc8_dvb_s2(b"123456789"))
    # 2. Known v1 frame: MSP_STATUS request with empty payload -> checksum = 0 ^ 101 = 0x65
    assert encode_v1(MSP_STATUS) == b"$M<\x00\x65\x65"
    # 3. v2 encode/decode round trip, including split delivery
    f2 = encode_v2(MSP2_CLI_COMMAND, b"status", direction=b">")
    p = MspParser()
    assert p.feed(f2[:4]) == []
    got = p.feed(f2[4:])
    assert len(got) == 1 and got[0].version == 2 and got[0].cmd == MSP2_CLI_COMMAND and got[0].payload == b"status"
    # 4. v2-over-v1
    got = MspParser().feed(encode_v2_over_v1(0x3005, b"\x01\x02"))
    assert got[0].version == 2 and got[0].cmd == 0x3005 and got[0].payload == b"\x01\x02"
    # 5. corrupted checksum is dropped, a following good frame survives (1-byte resync)
    good = encode_v1(MSP_RC, b"\xdc\x05", b">")
    bad = bytearray(good)
    bad[-1] ^= 0xFF
    p = MspParser()
    got = p.feed(bytes(bad) + good)
    assert len(got) == 1 and got[0].payload == b"\xdc\x05" and p.crc_errors == 1
    # 6. a '$' inside garbage must not swallow the real frame
    got = MspParser().feed(b"$$M$X" + good)
    assert len(got) == 1
    # 7. JUMBO v1 reply
    payload = bytes(range(256)) * 2
    body = bytes([255, MSP_STATUS]) + struct.pack("<H", len(payload)) + payload
    jumbo = b"$M>" + body + bytes([xor8(body)])
    got = MspParser().feed(jumbo)
    assert got[0].payload == payload
    # 8. end-to-end over a socketpair with a fake FC
    a, b = socket.socketpair()
    stop = threading.Event()
    th = threading.Thread(target=_fake_fc, args=(b, stop), daemon=True)
    th.start()
    try:
        c = MspClient(a)
        st = c.status()
        assert st["cycle_time_us"] == 125 and st["cpu_load_pct"] == 7
        assert arming_flag_names(st["arming_disable_flags"]) == ["RXLOSS", "BOOTGRACE"], st
        assert c.rc() == [1500, 1500, 1000, 1500]
        c.set_motor([1000, 1100, 1200, 1300, 0, 0, 0, 0])
        assert c.motors()[:4] == [1000, 1100, 1200, 1300]
        c2 = MspClient(a, version=2)
        assert c2.request(MSP2_CLI_COMMAND, b"tasks") == b"ok"
        try:
            c.request(MSP_ATTITUDE)
            raise AssertionError("expected error reply")
        except RuntimeError:
            pass
    finally:
        stop.set()
        a.close()
        b.close()
    print("msp_client self-test: all 8 checks passed")


if __name__ == "__main__":
    _self_test()
