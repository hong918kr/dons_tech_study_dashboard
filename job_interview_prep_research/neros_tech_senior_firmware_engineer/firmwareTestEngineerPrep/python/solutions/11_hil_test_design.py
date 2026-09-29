"""11 · HIL test design — arming / failsafe 테스트 작성 — 모범답안

FakeFlightController 는 실제 FC 대신 쓰는 아주 작은 시뮬레이터다 (규칙은 아래 docstring).
이 문제의 본론은 시뮬레이터가 아니라 **테스트 케이스를 설계하고 쓰는 것**이다:
happy path · 거부 조건(parametrize) · 경계값(timeout 정확히 / +1ms) · 상태 전이 · 복구.

pytest 가 있으면 `pytest python/solutions/11_hil_test_design.py` 로도 돈다
(fixture 는 make_fc() 함수로 흉내 냈다 — md 에 진짜 pytest 버전이 있다).

  python3 python/solutions/11_hil_test_design.py
"""
from enum import Enum
from typing import List, Optional, Tuple


class State(Enum):
    DISARMED = "DISARMED"
    ARMED = "ARMED"
    FAILSAFE = "FAILSAFE"


class FakeClock:
    """ms 정수 시계. 테스트가 시간을 직접 움직인다 → 결정적, 0초 만에 끝남."""

    def __init__(self):
        self.now_ms = 0

    def advance(self, ms: int):
        self.now_ms += ms


class FakeFlightController:
    """규칙 (Betaflight 를 단순화한 모델 — 실제 규칙과 같다고 가정하지 말 것)

    - RX 링크는 마지막 RC 프레임 후 failsafe_timeout_ms 이내면 up (경계 포함: elapsed <= timeout)
    - arm() 은 다음을 모두 만족할 때만 성공. 아니면 (False, blockers)
        NO_RX_LINK     링크 down
        THROTTLE_HIGH  throttle > THROTTLE_LOW_US
        FAILSAFE       failsafe 상태
        ALREADY_ARMED  이미 armed
    - ARMED 인데 링크가 끊기면 → FAILSAFE, 모터 즉시 0
    - FAILSAFE 에서 링크가 돌아오면 → DISARMED (자동 재arm 없음. 다시 arm 해야 함)
    - motor_outputs(): ARMED 가 아니면 [0,0,0,0]. ARMED 면 throttle 1000–2000us → DShot 48–2047
    """

    THROTTLE_LOW_US = 1050
    MOTORS = 4

    def __init__(self, clock: FakeClock, failsafe_timeout_ms: int = 100):
        self.clock = clock
        self.failsafe_timeout_ms = failsafe_timeout_ms
        self.state = State.DISARMED
        self.last_rx_ms: Optional[int] = None
        self.throttle_us = 1000

    # --- 입력 ---------------------------------------------------------
    def rx_frame(self, throttle_us: int):
        """RC 프레임 1개 수신 (CRSF 등에서 온 throttle 채널)."""
        self.last_rx_ms = self.clock.now_ms
        self.throttle_us = max(1000, min(2000, throttle_us))
        self._update()

    def tick(self):
        """FC 메인 루프 한 바퀴. 시간 경과에 따른 failsafe 판정."""
        self._update()

    def arm(self) -> Tuple[bool, List[str]]:
        self._update()
        blockers = self.arming_blockers()
        if blockers:
            return False, blockers
        self.state = State.ARMED
        return True, []

    def disarm(self):
        if self.state == State.ARMED:
            self.state = State.DISARMED

    # --- 관측 ---------------------------------------------------------
    def link_up(self) -> bool:
        return (self.last_rx_ms is not None
                and self.clock.now_ms - self.last_rx_ms <= self.failsafe_timeout_ms)

    def arming_blockers(self) -> List[str]:
        b = []
        if self.state == State.ARMED:
            b.append("ALREADY_ARMED")
        if self.state == State.FAILSAFE:
            b.append("FAILSAFE")
        if not self.link_up():
            b.append("NO_RX_LINK")
        if self.throttle_us > self.THROTTLE_LOW_US:
            b.append("THROTTLE_HIGH")
        return b

    def motor_outputs(self) -> List[int]:
        if self.state != State.ARMED:
            return [0] * self.MOTORS
        dshot = 48 + round((self.throttle_us - 1000) / 1000 * 1999)
        return [dshot] * self.MOTORS

    # --- 내부 ---------------------------------------------------------
    def _update(self):
        up = self.link_up()
        if self.state == State.ARMED and not up:
            self.state = State.FAILSAFE
        elif self.state == State.FAILSAFE and up:
            self.state = State.DISARMED


# ------------------------------------------------------------------ tests
def make_fc(timeout_ms: int = 100, throttle_us: int = 1000):
    """pytest fixture 역할: 매 테스트마다 새 시계 + 새 FC, 링크 up·throttle low 인 '정상 시작 상태'."""
    clock = FakeClock()
    fc = FakeFlightController(clock, failsafe_timeout_ms=timeout_ms)
    fc.rx_frame(throttle_us)
    return clock, fc


def test_arm_happy_path():
    clock, fc = make_fc()
    ok, blockers = fc.arm()
    assert ok and blockers == []
    assert fc.state == State.ARMED
    assert fc.motor_outputs() == [48] * 4                # armed idle
    fc.rx_frame(1500)
    assert all(m > 48 for m in fc.motor_outputs())


def test_arm_refused_conditions():
    # (이름, 준비 동작, 기대 blocker) — pytest.mark.parametrize 로 쓰는 표
    def never_linked():
        clock = FakeClock()
        return FakeFlightController(clock)

    def throttle_high():
        return make_fc(throttle_us=1300)[1]

    def throttle_just_above_low():
        return make_fc(throttle_us=FakeFlightController.THROTTLE_LOW_US + 1)[1]

    def link_stale():
        clock, fc = make_fc()
        clock.advance(101)
        return fc

    def already_armed():
        fc = make_fc()[1]
        fc.arm()
        return fc

    cases = [
        ("never_linked", never_linked, "NO_RX_LINK"),
        ("throttle_high", throttle_high, "THROTTLE_HIGH"),
        ("throttle_low+1", throttle_just_above_low, "THROTTLE_HIGH"),
        ("link_stale", link_stale, "NO_RX_LINK"),
        ("already_armed", already_armed, "ALREADY_ARMED"),
    ]
    for name, setup, expected in cases:
        fc = setup()
        was_armed = fc.state == State.ARMED
        ok, blockers = fc.arm()
        assert not ok, name
        assert expected in blockers, (name, blockers)
        if not was_armed:
            assert fc.state != State.ARMED, name
            assert fc.motor_outputs() == [0] * 4, name   # 거부됐으면 모터는 절대 안 돈다


def test_arm_allowed_exactly_at_throttle_low():
    _, fc = make_fc(throttle_us=FakeFlightController.THROTTLE_LOW_US)   # 경계값 = 허용
    assert fc.arm() == (True, [])


def test_failsafe_boundary():
    clock, fc = make_fc(timeout_ms=100)
    fc.arm()
    fc.rx_frame(1600)
    clock.advance(100)
    fc.tick()
    assert fc.state == State.ARMED                       # 정확히 timeout → 아직 up
    clock.advance(1)
    fc.tick()
    assert fc.state == State.FAILSAFE                    # timeout + 1ms → failsafe
    assert fc.motor_outputs() == [0] * 4


def test_steady_rx_never_failsafes():
    clock, fc = make_fc(timeout_ms=100)
    fc.arm()
    for _ in range(200):                                 # 50 ms 간격 = 20 Hz, 10 초
        clock.advance(50)
        fc.rx_frame(1400)
        fc.tick()
        assert fc.state == State.ARMED


def test_recovery_requires_rearm_and_low_throttle():
    clock, fc = make_fc()
    fc.arm()
    fc.rx_frame(1600)
    clock.advance(500)
    fc.tick()
    assert fc.state == State.FAILSAFE
    fc.rx_frame(1600)                                    # 링크 복구, 스틱은 여전히 높음
    assert fc.state == State.DISARMED                    # 자동 재arm 금지
    assert fc.motor_outputs() == [0] * 4
    ok, blockers = fc.arm()
    assert not ok and blockers == ["THROTTLE_HIGH"]      # 복구 순간 모터가 튀면 안 된다
    fc.rx_frame(1000)
    assert fc.arm() == (True, [])


def test_disarm_stops_motors_immediately():
    _, fc = make_fc()
    fc.arm()
    fc.rx_frame(1800)
    fc.disarm()
    assert fc.state == State.DISARMED
    assert fc.motor_outputs() == [0] * 4


if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
