"""C06 arm / failsafe state machine: RX timeout, arming guards, hysteresis on link recovery, table of transitions.

Interviewer wording (no hint): "The drone must only arm when throttle is low and the
radio link is alive. If we stop hearing the radio for 100 ms while armed, go to a
safe mode. Coming back should not flicker. Model it so I can test it."

Assumptions to SAY OUT LOUD (the interviewer may change them):
- failsafe -> ARMED again only after `recover_frames` good frames in a row
- failsafe longer than `land_after` -> DISARMED (we assume it landed)

Run: python3 c06_failsafe_fsm.py
"""

DISARMED, ARMED, FAILSAFE = "DISARMED", "ARMED", "FAILSAFE"


class FlightMode:
    def __init__(self, rx_timeout=0.1, recover_frames=5, land_after=3.0, throttle_low=0.05):
        self.rx_timeout = rx_timeout
        self.recover_frames = recover_frames
        self.land_after = land_after
        self.throttle_low = throttle_low
        self.state = DISARMED
        self.last_rx = None
        self.good_run = 0
        self.failsafe_since = None
        self.log = []                     # (time, from, to, reason) -- the test artifact

    def _go(self, now, new, why):
        self.log.append((now, self.state, new, why))
        self.state = new

    def link_ok(self, now):
        return self.last_rx is not None and now - self.last_rx < self.rx_timeout

    def on_rx_frame(self, now):
        raise NotImplementedError("TODO")

    def request_arm(self, now, throttle):
        """Return True if armed after the call."""
        raise NotImplementedError("TODO")

    def request_disarm(self, now):
        raise NotImplementedError("TODO")

    def tick(self, now):
        raise NotImplementedError("TODO")


# ----------------------------------------------------------------- tests
def _tests():
    m = FlightMode()
    assert m.request_arm(0.0, throttle=0.0) is False            # no link yet
    m.on_rx_frame(0.0)
    assert m.request_arm(0.01, throttle=0.5) is False           # throttle high
    assert m.request_arm(0.01, throttle=0.0) is True and m.state == ARMED

    m.tick(0.099)
    assert m.state == ARMED                                     # 99 ms of silence is fine
    m.tick(0.100)
    assert m.state == FAILSAFE                                  # 100 ms boundary -> failsafe

    for k in range(4):                                          # 4 good frames: still failsafe
        m.on_rx_frame(0.11 + k * 0.01)
    assert m.state == FAILSAFE
    m.tick(0.15)                                                # a tick during recovery changes nothing
    m.on_rx_frame(0.15)
    assert m.state == ARMED                                     # 5th frame in a row

    m.tick(0.30)
    assert m.state == FAILSAFE
    m.on_rx_frame(0.31)
    m.on_rx_frame(0.50)                                         # 190 ms gap mid-recovery...
    assert m.good_run == 1                                      # ...restarts the count
    m.on_rx_frame(0.51)
    assert m.good_run == 2
    m.tick(3.30)
    assert m.state == DISARMED and m.log[-1][3] == "failsafe landed"

    m = FlightMode()
    m.on_rx_frame(0.0)
    m.request_arm(0.0, 0.0)
    m.request_disarm(0.05)
    assert m.state == DISARMED
    assert [e[2] for e in m.log] == [ARMED, DISARMED]
    print("c06 OK")


if __name__ == "__main__":
    _tests()
