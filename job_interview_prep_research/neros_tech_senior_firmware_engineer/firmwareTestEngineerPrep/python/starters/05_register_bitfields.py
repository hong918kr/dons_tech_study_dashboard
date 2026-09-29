"""05 · 레지스터 비트필드 & IMU 읽기 — starter (TODO를 채운다)

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
    # TODO: shift 만큼 내리고 width 비트 mask
    raise NotImplementedError


def set_field(value, shift, width, field):
    # TODO: field 가 width 비트에 안 들어가면 ValueError
    # TODO: 해당 자리만 지우고(& ~(mask << shift)) 새 값을 OR. 결과는 8비트
    raise NotImplementedError


def to_int16(b0, b1, big_endian=True):
    # TODO: hi/lo 를 endian 에 맞게 고르고 (hi << 8) | lo, 0x8000 비트가 켜져 있으면 - 0x10000
    raise NotImplementedError


class ImuError(Exception):
    pass


class Imu:
    def __init__(self, bus, addr=0x68):
        self.bus, self.addr = bus, addr

    def check_whoami(self):
        # TODO: read_byte_data(addr, WHO_AM_I) 가 0x68 이 아니면 ImuError (값을 메시지에)
        raise NotImplementedError

    def set_accel_range(self, g):
        # TODO: g 가 RANGES_G 에 없으면 ValueError
        # TODO: read → set_field(AFS_SHIFT, AFS_WIDTH) → write → 다시 읽어서 verify (다르면 ImuError)
        raise NotImplementedError

    def read_accel_g(self):
        # TODO: read_i2c_block_data(addr, ACCEL_XOUT_H, 6) → struct.unpack(">hhh")
        # TODO: ACCEL_CONFIG 에서 AFS 를 읽어 lsb_per_g = 16384 >> afs 로 나눈다
        raise NotImplementedError


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
