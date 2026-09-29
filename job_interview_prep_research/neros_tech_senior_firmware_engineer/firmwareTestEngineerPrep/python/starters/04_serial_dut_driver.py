"""04 · 시리얼 DUT CLI 드라이버 — starter (TODO를 채운다)

DUT 의 UART CLI 에 명령을 보내고 프롬프트('> ')가 나올 때까지 읽는 드라이버.

  with DutCli(open_port, clock=clk) as dut:
      dut.send_command("status")   → "state: DISARMED\\nvbat: 16.4"
      dut.get_status()             → {"state": "DISARMED", "vbat": "16.4"}

- read() 는 pyserial 처럼 동작: 있는 만큼(최대 size) 돌려주고, 없으면 timeout 동안 기다렸다 b"" 반환
- 전체 응답 deadline 을 넘기면 DutTimeout. retries 만큼 입력 버퍼를 비우고 다시 보낸다
- DUT 는 명령을 에코한다 → 첫 줄이 명령과 같으면 제거
- clock 을 주입받아서 테스트가 실제로 기다리지 않는다
"""


class DutTimeout(Exception):
    pass


class DutCli:
    def __init__(self, open_port, prompt=b"> ", timeout=1.0, retries=2, clock=None):
        self.open_port, self.prompt = open_port, prompt
        self.timeout, self.retries, self.clock = timeout, retries, clock
        self.ser = None

    def __enter__(self):
        # TODO: open_port() 로 포트를 열어 self.ser 에 저장하고 self 반환
        raise NotImplementedError

    def __exit__(self, exc_type, exc, tb):
        # TODO: 포트가 열려 있으면 close(). 예외는 삼키지 않는다 (False 반환)
        raise NotImplementedError

    def send_command(self, cmd):
        # TODO: retries+1 번 시도:
        #   reset_input_buffer() → write(cmd + "\n") → _read_until_prompt() → _strip_echo()
        #   DutTimeout 이면 다음 시도. 전부 실패하면 DutTimeout (raise ... from last)
        raise NotImplementedError

    def _read_until_prompt(self):
        # TODO: deadline = clock.now() + timeout
        #   buf 가 prompt 로 끝날 때까지 ser.read(64) 를 이어 붙인다
        #   deadline 넘기면 DutTimeout. 반환은 prompt 를 뗀 str (decode errors="replace")
        raise NotImplementedError

    @staticmethod
    def _strip_echo(cmd, text):
        # TODO: 줄로 나누고 \r 제거, 첫 줄이 cmd 에코면 버리고, 빈 줄 제거 후 "\n".join
        raise NotImplementedError

    def get_status(self):
        return parse_kv(self.send_command("status"))


def parse_kv(text):
    # TODO: "key: value" 줄만 dict 로. 값 안의 ':' 는 보존 (str.partition 추천)
    raise NotImplementedError


# ------------------------------------------------------------ fakes (테스트용 하드웨어)
class FakeClock:
    def __init__(self):
        self.t = 0.0

    def now(self):
        return self.t


class FakeSerial:
    """responder(cmd) → 응답 bytes 또는 None(무응답). chunk 바이트씩 잘라서 돌려준다."""

    def __init__(self, clock, responder, chunk=3, read_timeout=0.1):
        self.clock, self.responder = clock, responder
        self.chunk, self.read_timeout = chunk, read_timeout
        self.rx, self.writes, self.closed = bytearray(), [], False

    def write(self, data):
        self.writes.append(data)
        cmd = data.decode().strip()
        reply = self.responder(cmd)
        if reply is not None:
            self.rx += cmd.encode() + b"\r\n" + reply + b"> "

    def read(self, size=1):
        if not self.rx:
            self.clock.t += self.read_timeout       # pyserial timeout 흉내
            return b""
        n = min(size, self.chunk)
        out, self.rx[:n] = bytes(self.rx[:n]), b""
        return out

    def reset_input_buffer(self):
        self.rx.clear()

    def close(self):
        self.closed = True


def status_reply(cmd):
    return b"state: DISARMED\r\nvbat: 16.4\r\nnoise line\r\n" if cmd == "status" else b"?\r\n"


# ------------------------------------------------------------------ tests
def test_status_with_partial_reads():
    clk = FakeClock()
    with DutCli(lambda: FakeSerial(clk, status_reply), clock=clk) as dut:
        assert dut.send_command("status") == "state: DISARMED\nvbat: 16.4\nnoise line"
        assert dut.get_status() == {"state": "DISARMED", "vbat": "16.4"}


def test_timeout_after_retries():
    clk = FakeClock()
    port = FakeSerial(clk, lambda cmd: None)
    dut = DutCli(lambda: port, timeout=1.0, retries=2, clock=clk)
    with dut:
        try:
            dut.send_command("status")
            raise AssertionError("expected DutTimeout")
        except DutTimeout:
            pass
    assert len(port.writes) == 3 and 3.0 <= clk.t < 3.5


def test_retry_recovers_from_dropped_command():
    clk, calls = FakeClock(), []

    def flaky(cmd):
        calls.append(cmd)
        return None if len(calls) == 1 else b"ok\r\n"

    port = FakeSerial(clk, flaky)
    with DutCli(lambda: port, clock=clk) as dut:
        assert dut.send_command("arm") == "ok"
    assert len(port.writes) == 2


class BodyFailed(Exception):
    pass


def test_port_closed_even_on_error():
    clk = FakeClock()
    port = FakeSerial(clk, status_reply)
    try:
        with DutCli(lambda: port, clock=clk):
            raise BodyFailed("test body failed")   # (NotImplementedError 는 RuntimeError 의 자식이라 안 씀)
    except BodyFailed:
        pass
    assert port.closed


def test_parse_kv():
    assert parse_kv("a: 1\nno colon\n b :x:y ") == {"a": "1", "b": "x:y"}


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
