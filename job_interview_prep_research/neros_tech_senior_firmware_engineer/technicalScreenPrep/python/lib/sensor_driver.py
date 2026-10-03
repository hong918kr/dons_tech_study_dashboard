"""UART 온도 센서 드라이버 — 문제 03(mock 으로 테스트 쓰기)의 피검 코드.

프로토콜 (줄 단위 ASCII, 요청 끝은 b"\\n", 응답 끝은 b"\\r\\n"):
  b"TEMP?\\n"     -> b"TEMP=23.50\\r\\n"   (섭씨, 부호 있음)
                  -> b"ERR 3\\r\\n"         (센서 고장: 재시도하지 않는다)
                  -> b""                  (readline 타임아웃: 재시도한다)
  b"RATE 10\\n"   -> b"OK\\r\\n"           (허용: 1, 10, 50, 100 Hz)

port 는 pyserial 의 Serial 처럼 write(bytes), readline() -> bytes, reset_input_buffer(), close() 를 가진 객체.
sleep 은 주입받는다 → 테스트가 실제로 기다리지 않고, 백오프 호출을 검사할 수 있다.
"""
import re
import time

VALID_RATES = (1, 10, 50, 100)
TEMP_MIN, TEMP_MAX = -40.0, 125.0
_TEMP_RE = re.compile(rb"^TEMP=(-?\d+(?:\.\d+)?)\r?\n?$")


class SensorError(Exception):
    """센서가 에러를 보고했거나 값이 말이 안 됨 — 재시도해도 소용없음."""


class SensorTimeout(SensorError):
    """재시도를 다 써도 응답이 없음."""


class SensorDriver:
    def __init__(self, port, retries=2, backoff=0.1, sleep=time.sleep):
        self.port, self.retries, self.backoff, self.sleep = port, retries, backoff, sleep

    def _transact(self, cmd):
        """명령 하나 보내고 한 줄 받기. 빈 응답(타임아웃)이면 지수 백오프로 재시도."""
        for attempt in range(self.retries + 1):
            if attempt:
                self.sleep(self.backoff * 2 ** (attempt - 1))   # 0.1, 0.2, 0.4 ...
            self.port.reset_input_buffer()                       # 이전 시도의 늦은 응답 버리기
            self.port.write(cmd + b"\n")
            line = self.port.readline()
            if line:
                return line
        raise SensorTimeout(f"{cmd!r}: no response after {self.retries + 1} tries")

    def read_temperature(self):
        line = self._transact(b"TEMP?")
        if line.startswith(b"ERR"):
            raise SensorError(line.strip().decode(errors="replace"))
        m = _TEMP_RE.match(line)
        if not m:
            raise SensorError(f"malformed response {line!r}")
        t = float(m.group(1))
        if not TEMP_MIN <= t <= TEMP_MAX:
            raise SensorError(f"out of range: {t}")
        return t

    def set_rate(self, hz):
        if hz not in VALID_RATES:
            raise ValueError(f"rate {hz} not in {VALID_RATES}")       # 잘못된 값은 아예 보내지 않는다
        line = self._transact(b"RATE %d" % hz)
        if line.strip() != b"OK":
            raise SensorError(f"RATE {hz} rejected: {line!r}")

    def close(self):
        self.port.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False
