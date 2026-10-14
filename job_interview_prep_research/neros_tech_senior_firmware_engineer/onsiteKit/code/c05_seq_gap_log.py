"""C05 telemetry log check: 16-bit sequence numbers that wrap -> lost / duplicate / reordered, bad lines.

Interviewer wording (no hint): "Here is a log from a 10-minute flight test. Every
telemetry line has a seq field. Tell me how many packets we lost. Then: the counter
is 16-bit. Then: some lines are corrupted."

Run: python3 c05_seq_gap_log.py
"""
import re

LINE = re.compile(r"\bseq=(\d+)\b")


class SeqTracker:
    def __init__(self, bits=16, memory=1024):
        self.mod = 1 << bits
        self.half = self.mod >> 1
        self.memory = memory          # how many missing ids we remember (bounded)
        self.expected = None
        self.received = 0
        self.lost = 0
        self.duplicates = 0
        self.reordered = 0
        self.missing = {}             # seq -> True, insertion ordered (oldest first)

    def add(self, seq):
        # >>> solution
        self.received += 1
        if self.expected is None:
            self.expected = (seq + 1) % self.mod
            return
        diff = (seq - self.expected) % self.mod
        if diff == 0:                                   # in order
            self.expected = (seq + 1) % self.mod
        elif diff < self.half:                          # jumped forward: diff packets missing
            for k in range(diff):
                self._remember((self.expected + k) % self.mod)
            self.lost += diff
            self.expected = (seq + 1) % self.mod
        elif seq in self.missing:                       # an old one we counted as lost arrived late
            del self.missing[seq]
            self.lost -= 1
            self.reordered += 1
        else:                                           # old and not missing -> seen already
            self.duplicates += 1
        # <<< solution

    def _remember(self, seq):
        self.missing[seq] = True
        while len(self.missing) > self.memory:          # forget the oldest; it stays counted as lost
            del self.missing[next(iter(self.missing))]


def analyze(lines, bits=16):
    """Return a dict summary. Lines without a valid seq are counted, not fatal."""
    # >>> solution
    t = SeqTracker(bits)
    bad = 0
    for line in lines:
        m = LINE.search(line)
        if not m or int(m.group(1)) >= t.mod:
            bad += 1
            continue
        t.add(int(m.group(1)))
    return {"received": t.received, "lost": t.lost, "duplicates": t.duplicates,
            "reordered": t.reordered, "bad_lines": bad}
    # <<< solution


# ----------------------------------------------------------------- tests
def _tests():
    def run(seqs, bits=16):
        return analyze(["t=0 seq=%d vbat=15.2" % s for s in seqs], bits)

    assert run([0, 1, 2, 3])["lost"] == 0
    r = run([0, 1, 4, 5])
    assert r["lost"] == 2 and r["received"] == 4
    r = run([65534, 65535, 0, 1])                                    # wrap is not a gap
    assert r["lost"] == 0
    r = run([65535, 2])                                              # gap across the wrap: 0, 1
    assert r["lost"] == 2
    r = run([5, 6, 6, 7])
    assert r["duplicates"] == 1 and r["lost"] == 0
    r = run([1, 3, 2, 4])                                            # late, not lost
    assert r["lost"] == 0 and r["reordered"] == 1
    r = run([1, 3, 2, 2])                                            # late, then a duplicate of it
    assert r["reordered"] == 1 and r["duplicates"] == 1
    r = run([7])                                                     # first packet sets the baseline
    assert r["lost"] == 0
    r = run([250, 255, 3], bits=8)
    assert r["lost"] == 4 + 3

    lines = ["t=0 seq=10", "garbage \x00\xff", "t=1 seq=", "t=2 seq=99999", "t=3 seq=11"]
    r = analyze(lines)
    assert r["bad_lines"] == 3 and r["received"] == 2 and r["lost"] == 0

    t = SeqTracker(memory=4)                                         # bounded memory
    t.add(0)
    t.add(100)
    assert t.lost == 99 and len(t.missing) == 4
    print("c05 OK")


if __name__ == "__main__":
    _tests()
