"""09 · DShot codec + motor output check — starter

DShot = FC → ESC 디지털 모터 명령. 16비트 프레임, MSB 먼저:
    [ 11-bit value | 1-bit telemetry request | 4-bit CRC ]
    value 0      = motor stop (disarmed)
    value 1–47   = special commands (beep, spin direction, save settings ...)
    value 48–2047 = throttle (2000 단계)
    CRC  = (v ^ v>>4 ^ v>>8) & 0xF,  v = (value << 1) | telemetry  (12비트)
    bidirectional DShot 은 CRC 를 반전: (~(v ^ v>>4 ^ v>>8)) & 0xF

  python3 python/run.py 09          # 내 풀이 채점
  python3 python/run.py 09 --sol    # 모범답안
"""
from dataclasses import dataclass
from typing import Dict, List, Sequence

THROTTLE_MIN, THROTTLE_MAX = 48, 2047


def dshot_crc(v12: int, bidir: bool = False) -> int:
    # TODO: XOR 세 번, bidir 면 ~ 로 반전, 마지막에 & 0x0F (Python int 는 무한 비트 → 마스크 필수)
    raise NotImplementedError


def encode(value: int, telemetry: bool = False, bidir: bool = False) -> int:
    """0..2047 밖이면 ValueError."""
    # TODO: v12 = (value << 1) | telemetry → (v12 << 4) | crc
    raise NotImplementedError


@dataclass(frozen=True)
class DShotFrame:
    value: int
    telemetry: bool
    crc: int
    crc_ok: bool

    @property
    def is_command(self) -> bool:
        return self.value < THROTTLE_MIN


def decode(frame: int, bidir: bool = False) -> DShotFrame:
    """16비트 밖이면 ValueError. CRC 가 틀려도 예외 없이 crc_ok=False 로 돌려준다."""
    # TODO: v12 = frame >> 4, crc = frame & 0xF, value = v12 >> 1, telemetry = v12 & 1
    raise NotImplementedError


def throttle_pct_to_dshot(pct: float) -> int:
    """0% → 48, 100% → 2047 (round 사용). 범위 밖은 ValueError."""
    # TODO
    raise NotImplementedError


def frame_to_bits(frame: int) -> List[int]:
    """MSB 먼저 16개 비트."""
    # TODO
    raise NotImplementedError


def frame_from_high_times(high_times_us: Sequence[float], bit_period_us: float) -> int:
    """캡처한 16개 비트의 high 펄스 폭 → 프레임. high > period/2 이면 1.
    (DShot600: bit 1.67us, T1H 1.25us, T0H 0.625us). 16개가 아니면 ValueError."""
    # TODO
    raise NotImplementedError


def check_motor_outputs(captured: Dict[int, List[int]], commanded: Dict[int, int],
                        tolerance: int = 0, bidir: bool = False) -> List[str]:
    """captured: {motor: [frames...]}, commanded: {motor: dshot value}. 문제 목록 (빈 리스트 = PASS).
    모터 번호 오름차순, 프레임 순서대로. 메시지 형식은 테스트 참고:
      "motor 4: no frames captured"
      "motor 2 frame 1: CRC error (raw 0x....)"
      "motor 3 frame 0: value 1010 != commanded 1000 ±2"
    """
    # TODO
    raise NotImplementedError


# ------------------------------------------------------------------ tests
def test_known_vector():
    # 널리 인용되는 예: throttle 1046, telemetry 0 → 1000001011000110 = 0x82C6
    assert encode(1046) == 0x82C6
    assert format(encode(1046), "016b") == "1000001011000110"


def test_roundtrip_all_values_both_modes():
    for bidir in (False, True):
        for value in range(2048):
            for tel in (False, True):
                f = decode(encode(value, tel, bidir), bidir)
                assert (f.value, f.telemetry, f.crc_ok) == (value, tel, True)


def test_bidir_crc_is_inverted():
    v12 = 1046 << 1
    assert dshot_crc(v12, bidir=True) == (~dshot_crc(v12)) & 0xF
    assert not decode(encode(1046), bidir=True).crc_ok      # 모드가 다르면 CRC 불일치


def test_single_bit_error_detected():
    frame = encode(1500)
    for bit in range(16):
        assert not decode(frame ^ (1 << bit)).crc_ok         # 4-bit XOR CRC 는 1비트 오류를 항상 잡는다


def test_commands_and_ranges():
    assert decode(encode(0)).is_command                      # motor stop
    assert decode(encode(47)).is_command
    assert not decode(encode(48)).is_command
    for bad in (-1, 2048):
        try:
            encode(bad)
            assert False
        except ValueError:
            pass
    assert throttle_pct_to_dshot(0) == 48
    assert throttle_pct_to_dshot(100) == 2047
    assert throttle_pct_to_dshot(50) == 1048                 # 48 + round(999.5) → 1048 (banker's rounding 주의)


def test_decode_from_pulse_widths_with_jitter():
    period, t1, t0 = 1.67, 1.25, 0.625                       # DShot600
    frame = encode(1046)
    highs = [(t1 if b else t0) + (0.05 if i % 3 else -0.05) for i, b in enumerate(frame_to_bits(frame))]
    assert frame_from_high_times(highs, period) == frame


def test_check_motor_outputs():
    cmd = {1: 1000, 2: 1000, 3: 1000, 4: 1000}
    cap = {
        1: [encode(1000), encode(1001)],
        2: [encode(1000), encode(1000) ^ 0x1],               # CRC 깨짐
        3: [encode(1010)],                                   # 10 차이
    }                                                        # 4번 모터 캡처 없음
    issues = check_motor_outputs(cap, cmd, tolerance=2)
    assert len(issues) == 3
    assert "motor 2 frame 1: CRC error" in issues[0]
    assert "motor 3 frame 0: value 1010" in issues[1]
    assert issues[2] == "motor 4: no frames captured"
    assert check_motor_outputs({1: [encode(1000)]}, {1: 1000}) == []


if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
