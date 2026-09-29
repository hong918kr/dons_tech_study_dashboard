"""05 · 레지스터 비트필드 & IMU 읽기 — 모범답안

MPU-6050/6000 계열 IMU 를 I2C 로 다루는 테스트 헬퍼 (레지스터 맵은 데이터시트 기준).

  WHO_AM_I      0x75  = 0x68
  ACCEL_CONFIG  0x1C  bits[4:3] AFS_SEL: 0=±2g 1=±4g 2=±8g 3=±16g   (bits 7:5 self-test, 나머지 보존)
  ACCEL_XOUT_H  0x3B  X_H X_L Y_H Y_L Z_H Z_L  — big-endian signed int16
  감도          ±2g → 16384 LSB/g, 범위가 두 배 될 때마다 절반

get_field / set_field : mask·shift 로 필드 읽기/쓰기 (다른 비트 보존)
to_int16              : 두 바이트 → signed int16 (big/little endian)
Imu                   : check_whoami, set_accel_range (read-modify-write), read_accel_g (burst read + struct)
bus 는 smbus2.SMBus 와 같은 메서드 이름을 쓴다 → 테스트는 FakeI2CBus
"""
import struct

WHO_AM_I, WHO_AM_I_VAL = 0x75, 0x68
ACCEL_CONFIG, ACCEL_XOUT_H = 0x1C, 0x3B
AFS_SHIFT, AFS_WIDTH = 3, 2
RANGES_G = {2: 0, 4: 1, 8: 2, 16: 3}


def get_field(value, shift, width):
    return (value >> shift) & ((1 << width) - 1)


def set_field(value, shift, width, field):
    mask = (1 << width) - 1
    if not 0 <= field <= mask:
        raise ValueError(f"field {field} does not fit in {width} bits")
    return (value & ~(mask << shift) & 0xFF) | (field << shift)


def to_int16(b0, b1, big_endian=True):
    hi, lo = (b0, b1) if big_endian else (b1, b0)
    v = (hi << 8) | lo
    return v - 0x10000 if v & 0x8000 else v


class ImuError(Exception):
    pass


class Imu:
    def __init__(self, bus, addr=0x68):
        self.bus, self.addr = bus, addr

    def check_whoami(self):
        got = self.bus.read_byte_data(self.addr, WHO_AM_I)
        if got != WHO_AM_I_VAL:
            raise ImuError(f"WHO_AM_I 0x{got:02X}, expected 0x{WHO_AM_I_VAL:02X}")

    def set_accel_range(self, g):
        if g not in RANGES_G:
            raise ValueError(f"unsupported range ±{g}g")
        old = self.bus.read_byte_data(self.addr, ACCEL_CONFIG)          # read
        new = set_field(old, AFS_SHIFT, AFS_WIDTH, RANGES_G[g])          # modify
        self.bus.write_byte_data(self.addr, ACCEL_CONFIG, new)           # write
        if self.bus.read_byte_data(self.addr, ACCEL_CONFIG) != new:      # verify
            raise ImuError("ACCEL_CONFIG write did not stick")

    def read_accel_g(self):
        raw = bytes(self.bus.read_i2c_block_data(self.addr, ACCEL_XOUT_H, 6))
        x, y, z = struct.unpack(">hhh", raw)
        afs = get_field(self.bus.read_byte_data(self.addr, ACCEL_CONFIG), AFS_SHIFT, AFS_WIDTH)
        lsb_per_g = 16384 >> afs
        return x / lsb_per_g, y / lsb_per_g, z / lsb_per_g


# ------------------------------------------------------------ fake (테스트용 하드웨어)
class FakeI2CBus:
    """smbus2.SMBus 와 같은 이름. regs = {addr: {reg: byte}}. 없는 addr 는 OSError (NACK 흉내)."""

    def __init__(self, regs):
        self.regs, self.writes = regs, []

    def _dev(self, addr):
        if addr not in self.regs:
            raise OSError(121, "Remote I/O error")        # Linux 에서 NACK 시 보이는 errno
        return self.regs[addr]

    def read_byte_data(self, addr, reg):
        return self._dev(addr).get(reg, 0)

    def write_byte_data(self, addr, reg, val):
        self.writes.append((addr, reg, val))
        self._dev(addr)[reg] = val & 0xFF

    def read_i2c_block_data(self, addr, reg, n):
        dev = self._dev(addr)
        return [dev.get(reg + i, 0) for i in range(n)]


def fake_imu(accel_raw=(0, 0, 16384), accel_config=0):
    regs = {WHO_AM_I: WHO_AM_I_VAL, ACCEL_CONFIG: accel_config}
    for i, b in enumerate(struct.pack(">hhh", *accel_raw)):
        regs[ACCEL_XOUT_H + i] = b
    return FakeI2CBus({0x68: regs})


# ------------------------------------------------------------------ tests
def test_fields():
    assert get_field(0b1111_0111, 3, 2) == 0b10
    assert set_field(0b1110_0111, 3, 2, 0b10) == 0b1111_0111
    try:
        set_field(0, 3, 2, 4)
        raise AssertionError("expected ValueError")
    except ValueError:
        pass


def test_to_int16():
    assert to_int16(0xFF, 0xFF) == -1
    assert to_int16(0x80, 0x00) == -32768
    assert to_int16(0x7F, 0xFF) == 32767
    assert to_int16(0x34, 0x12, big_endian=False) == 0x1234


def test_whoami():
    Imu(fake_imu()).check_whoami()
    bus = fake_imu()
    bus.regs[0x68][WHO_AM_I] = 0x00
    try:
        Imu(bus).check_whoami()
        raise AssertionError("expected ImuError")
    except ImuError:
        pass


def test_rmw_preserves_other_bits():
    bus = fake_imu(accel_config=0b1110_0111)      # self-test 비트 + 하위 비트 켜 둠
    Imu(bus).set_accel_range(8)
    assert bus.regs[0x68][ACCEL_CONFIG] == 0b1111_0111
    assert bus.writes == [(0x68, ACCEL_CONFIG, 0b1111_0111)]


def test_read_accel_scaled():
    bus = fake_imu(accel_raw=(-8192, 0, 16384))
    assert Imu(bus).read_accel_g() == (-0.5, 0.0, 1.0)          # ±2g
    Imu(bus).set_accel_range(16)
    assert Imu(bus).read_accel_g() == (-4.0, 0.0, 8.0)          # 같은 raw, 2048 LSB/g


def test_missing_device_nack():
    try:
        Imu(fake_imu(), addr=0x69).check_whoami()
        raise AssertionError("expected OSError")
    except OSError:
        pass


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
