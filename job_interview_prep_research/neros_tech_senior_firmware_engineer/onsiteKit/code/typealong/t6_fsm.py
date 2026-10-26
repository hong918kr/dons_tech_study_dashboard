# T6 arm / failsafe state machine -- "arm only if throttle low and link alive; 100 ms no radio -> safe mode; no flicker"
DISARMED, ARMED, FAILSAFE = "DISARMED", "ARMED", "FAILSAFE"


class Mode:
    def __init__(self, rx_timeout=100, recover=5, land_after=3000, thr_low=50):
        self.rx_timeout, self.recover = rx_timeout, recover   # all times: integer ms
        self.land_after, self.thr_low = land_after, thr_low
        self.state = DISARMED
        self.last_rx = None
        self.good = 0
        self.fs_since = None
        self.log = []                               # (t, from, to, why): the test artifact

    def _go(self, t, new, why):
        self.log.append((t, self.state, new, why))
        self.state = new

    def link_ok(self, t):
        return self.last_rx is not None and t - self.last_rx < self.rx_timeout

    def on_rx(self, t):
        if self.state == FAILSAFE:
            if not self.link_ok(t):
                self.good = 0                       # gap breaks the "in a row" count
            self.good += 1
            if self.good >= self.recover:
                self._go(t, ARMED, "link back")
        self.last_rx = t

    def arm(self, t, throttle):
        if self.state == DISARMED and throttle <= self.thr_low and self.link_ok(t):
            self._go(t, ARMED, "arm")
        return self.state == ARMED

    def tick(self, t):
        if self.state == ARMED and not self.link_ok(t):
            self.good, self.fs_since = 0, t
            self._go(t, FAILSAFE, "rx timeout")
        elif self.state == FAILSAFE and t - self.fs_since >= self.land_after:
            self._go(t, DISARMED, "landed")


if __name__ == "__main__":
    m = Mode()
    assert not m.arm(0, 0)                          # no link yet
    m.on_rx(0)
    assert not m.arm(10, 900)                       # throttle high
    assert m.arm(10, 0)
    m.tick(99); assert m.state == ARMED             # 99 ms: still fine
    m.tick(100); assert m.state == FAILSAFE         # exactly 100 ms -> failsafe
    for t in (110, 120, 130, 140):
        m.on_rx(t)
    assert m.state == FAILSAFE                      # 4 frames: not yet
    m.on_rx(150); assert m.state == ARMED           # 5 in a row -> back
    m.tick(400); m.tick(3400)
    assert m.state == DISARMED and m.log[-1][3] == "landed"
    print("t6 OK")
