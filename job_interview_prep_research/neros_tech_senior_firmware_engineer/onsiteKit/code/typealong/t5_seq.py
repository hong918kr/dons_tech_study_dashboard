# T5 log check -- "how many packets lost?" ... "seq is 16-bit" ... "some lines corrupted" ... "out of order"
import re

LINE = re.compile(r"\bseq=(\d+)\b")
MOD, HALF = 1 << 16, 1 << 15


def analyze(lines):
    expected = None
    lost = dup = reordered = bad = 0
    missing = set()
    for line in lines:
        m = LINE.search(line)
        if not m or int(m.group(1)) >= MOD:
            bad += 1
            continue
        seq = int(m.group(1))
        if expected is None:
            expected = (seq + 1) % MOD
            continue
        diff = (seq - expected) % MOD               # wrap-safe
        if diff == 0:
            expected = (seq + 1) % MOD
        elif diff < HALF:                           # forward jump: diff missing
            for k in range(diff):
                missing.add((expected + k) % MOD)
            lost += diff
            expected = (seq + 1) % MOD
        elif seq in missing:                        # late arrival
            missing.discard(seq)
            lost -= 1
            reordered += 1
        else:
            dup += 1
    return {"lost": lost, "dup": dup, "reordered": reordered, "bad": bad}


if __name__ == "__main__":
    L = lambda *s: ["t=0 seq=%d" % x for x in s]
    assert analyze(L(1, 2, 3)) == {"lost": 0, "dup": 0, "reordered": 0, "bad": 0}
    assert analyze(L(65534, 65535, 0, 1))["lost"] == 0          # wrap is not a gap
    assert analyze(L(65535, 2))["lost"] == 2                    # 0, 1 lost across wrap
    assert analyze(L(1, 4, 2))["lost"] == 1                     # 2 came late
    assert analyze(L(1, 4, 2))["reordered"] == 1
    assert analyze(L(1, 2, 2))["dup"] == 1
    assert analyze(["seq=1", "garbage", "seq=x", "seq=70000", "seq=2"])["bad"] == 3
    print("t5 OK")
