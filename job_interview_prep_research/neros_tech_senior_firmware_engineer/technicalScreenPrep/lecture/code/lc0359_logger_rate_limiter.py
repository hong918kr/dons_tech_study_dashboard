# --- solution ---
class Logger:
    def __init__(self):
        self.next_ok = {}                  # message → 다시 찍어도 되는 가장 이른 시각

    def shouldPrintMessage(self, timestamp: int, message: str) -> bool:
        if timestamp < self.next_ok.get(message, 0):
            return False
        self.next_ok[message] = timestamp + 10
        return True
# --- end ---


def test_lc359():
    lg = Logger()
    got = [lg.shouldPrintMessage(t, m) for t, m in
           [(1, "foo"), (2, "bar"), (3, "foo"), (8, "bar"), (10, "foo"), (11, "foo")]]
    assert got == [True, True, False, False, False, True]
