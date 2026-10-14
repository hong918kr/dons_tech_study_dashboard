"""C04 command / ACK with timeout + retry: 8-bit ids that wrap, in-flight limit, late / duplicate / unknown ACKs.

Interviewer wording (no hint): "The test host sends commands to the flight controller
over a lossy link. Each command gets an ACK with the same id. If no ACK in 50 ms,
resend, up to 3 tries, then report it failed. Ids are one byte."

Time is passed in (`now`, integer milliseconds) instead of calling time.time(): the logic
becomes testable without sleeping, and the same class works with a real clock in production.
Integer ms on purpose: with float seconds, 1.10 + 0.05 == 1.1500000000000001, so a
deadline check at exactly 1.15 silently misses (this file's first version hit that).

Run: python3 c04_retry_queue.py
"""


class CommandSender:
    def __init__(self, send, timeout=50, max_tries=3, max_inflight=8):
        self.send = send                  # send(cmd_id, payload) -> None (the link)
        self.timeout = timeout
        self.max_tries = max_tries
        self.max_inflight = max_inflight
        self.inflight = {}                # id -> [payload, tries, deadline]
        self.next_id = 0
        self.stale_acks = 0               # ACK for an id we are not waiting on

    def submit(self, payload, now):
        """Send a new command. Return its id, or None if too many are waiting."""
        raise NotImplementedError("TODO")

    def on_ack(self, cid, now):
        """True if this ACK completed a waiting command; False for late / duplicate / unknown."""
        raise NotImplementedError("TODO")

    def tick(self, now):
        """Resend what timed out. Return the ids that ran out of tries (removed)."""
        raise NotImplementedError("TODO")


# ----------------------------------------------------------------- tests
def _tests():
    wire = []
    s = CommandSender(lambda i, p: wire.append((i, p)), timeout=50, max_tries=3, max_inflight=2)

    a = s.submit(b"ARM", now=0)
    assert a == 0 and wire == [(0, b"ARM")]
    assert s.on_ack(a, now=10) is True and not s.inflight         # happy path
    assert s.on_ack(a, now=20) is False and s.stale_acks == 1     # duplicate ACK ignored

    wire.clear()
    b = s.submit(b"B", now=1000)
    assert s.tick(1049) == [] and len(wire) == 1                   # not yet
    assert s.tick(1050) == [] and len(wire) == 2                    # try 2 (deadline is inclusive)
    assert s.tick(1100) == [] and len(wire) == 3                    # try 3
    assert s.tick(1150) == [b] and len(wire) == 3                   # give up, no 4th send
    assert s.on_ack(b, now=1200) is False                            # late ACK after giving up

    c = s.submit(b"C", now=2000)
    d = s.submit(b"D", now=2000)
    assert s.submit(b"E", now=2000) is None                          # in-flight limit
    assert s.on_ack(d, 2010) and s.on_ack(c, 2020)                  # out-of-order ACKs are fine
    assert s.submit(b"E", now=2030) is not None

    s = CommandSender(lambda i, p: None, max_inflight=4)            # id wrap skips ids still waiting
    s.next_id = 254
    ids = [s.submit(b"x", 0) for _ in range(3)]
    assert ids == [254, 255, 0]
    s.on_ack(255, 0)
    s.on_ack(0, 0)
    s.next_id = 254                                                 # 254 is still waiting
    assert s.submit(b"y", 0) == 255
    print("c04 OK")


if __name__ == "__main__":
    _tests()
