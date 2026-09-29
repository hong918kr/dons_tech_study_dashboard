"""11 · HIL test design — arming / failsafe 테스트 작성 — starter

FakeFlightController 는 실제 FC 대신 쓰는 아주 작은 시뮬레이터다 (규칙은 아래 docstring).
이 문제의 본론은 시뮬레이터가 아니라 **테스트 케이스를 설계하고 쓰는 것**이다:
happy path · 거부 조건(parametrize) · 경계값(timeout 정확히 / +1ms) · 상태 전이 · 복구.

pytest 가 있으면 `pytest python/solutions/11_hil_test_design.py` 로도 돈다
(fixture 는 make_fc() 함수로 흉내 냈다 — md 에 진짜 pytest 버전이 있다).

이 starter 에서는 시뮬레이터는 완성돼 있고, 아래 test_* 7개를 **내가 쓴다**.
각 테스트의 raise NotImplementedError 를 지우고 assert 로 채운다.

  python3 python/run.py 11          # 내 테스트 실행
  python3 python/run.py 11 --sol    # 모범답안 테스트
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
    # TODO: make_fc() → arm() 이 (True, []) → state ARMED → motor_outputs 가 idle(48) x4
    # TODO: rx_frame(1500) 후 모터 값이 idle 보다 커지는지
    raise NotImplementedError


def test_arm_refused_conditions():
    # TODO: (이름, 준비 함수, 기대 blocker) 표를 만들고 루프 (= pytest.mark.parametrize)
    #   후보: 링크가 한 번도 없음 / throttle 높음 / throttle 이 LOW 보다 1us 높음 / 링크 stale / 이미 armed
    # TODO: 거부됐을 때 state 가 ARMED 가 아니고 모터가 [0]*4 인지까지 확인 (assert 에 이름을 메시지로)
    raise NotImplementedError


def test_arm_allowed_exactly_at_throttle_low():
    # TODO: 경계값 THROTTLE_LOW_US 정확히 → 허용
    raise NotImplementedError


def test_failsafe_boundary():
    # TODO: arm → clock.advance(timeout) → tick → 아직 ARMED
    # TODO: advance(1) → tick → FAILSAFE, 모터 0
    raise NotImplementedError


def test_steady_rx_never_failsafes():
    # TODO: 50 ms 마다 rx_frame 을 10 초 동안 → 한 번도 failsafe 가 아니어야 (false positive 테스트)
    raise NotImplementedError


def test_recovery_requires_rearm_and_low_throttle():
    # TODO: armed → 링크 끊김 → FAILSAFE → rx_frame(1600) 로 복구 → DISARMED, 모터 0
    # TODO: 그 상태에서 arm → THROTTLE_HIGH 로 거부 → throttle 내린 뒤 arm 성공
    raise NotImplementedError


def test_disarm_stops_motors_immediately():
    # TODO: armed + throttle 1800 → disarm → DISARMED, 모터 0
    raise NotImplementedError


if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
