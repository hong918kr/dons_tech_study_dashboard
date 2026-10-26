# T3 command / ACK / timeout / retry -- "no ACK in 50 ms -> resend, 3 tries total, ids are one byte"
class Sender:
    def __init__(self, send, timeout=50, max_tries=3, max_inflight=8):
        self.send, self.timeout = send, timeout
        self.max_tries, self.max_inflight = max_tries, max_inflight
        self.inflight = {}                          # id -> [payload, tries, deadline_ms]
        self.next_id = 0
        self.stale_acks = 0

    def submit(self, payload, now):
        if len(self.inflight) >= self.max_inflight:
            return None                             # back-pressure, caller decides
        while self.next_id in self.inflight:        # 8-bit wrap: skip ids still waiting
            self.next_id = (self.next_id + 1) & 0xFF
        cid = self.next_id
        self.next_id = (cid + 1) & 0xFF
        self.inflight[cid] = [payload, 1, now + self.timeout]
        self.send(cid, payload)
        return cid

    def on_ack(self, cid):
        if cid in self.inflight:
            del self.inflight[cid]
            return True
        self.stale_acks += 1                        # late / duplicate / unknown
        return False

    def tick(self, now):
        failed = []
        for cid in list(self.inflight):             # copy keys: we delete while looping
            payload, tries, deadline = self.inflight[cid]
            if now < deadline:
                continue
            if tries >= self.max_tries:
                del self.inflight[cid]
                failed.append(cid)
            else:
                self.inflight[cid] = [payload, tries + 1, now + self.timeout]
                self.send(cid, payload)
        return failed


if __name__ == "__main__":
    sent = []
    s = Sender(lambda c, p: sent.append(c))
    a = s.submit(b"x", 1000)
    assert s.tick(1049) == [] and len(sent) == 1        # just before deadline
    assert s.tick(1050) == [] and len(sent) == 2        # exactly at deadline -> resend
    assert s.tick(1100) == [] and len(sent) == 3
    assert s.tick(1150) == [a] and s.inflight == {}     # 3 tries total -> failed
    assert s.on_ack(a) is False and s.stale_acks == 1   # late ACK
    b = s.submit(b"y", 2000)
    assert s.on_ack(b) and not s.on_ack(b)              # duplicate ACK
    s2 = Sender(lambda c, p: None, max_inflight=2)
    s2.next_id = 255
    assert s2.submit(1, 0) == 255 and s2.submit(2, 0) == 0   # id wraps
    assert s2.submit(3, 0) is None                      # inflight full
    print("t3 OK")
