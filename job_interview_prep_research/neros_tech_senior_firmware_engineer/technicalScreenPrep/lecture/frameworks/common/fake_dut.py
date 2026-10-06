"""모든 Python 프레임워크 예제가 공유하는 '가짜 DUT' — 실제로는 USB-UART 너머의 FC 펌웨어 CLI.

FakeSerial: pyserial 처럼 write(bytes) / readline() -> bytes. 안에 작은 펌웨어 흉내(FakeFirmware)가 있다.
DutClient : 테스트가 쓰는 드라이버. send("arm") -> "OK". 응답이 없으면 DutTimeout.
실제 리그에선 FakeSerial 대신 serial.Serial("/dev/ttyUSB0", 115200, timeout=0.5) 를 넘기면 된다.
"""


class DutTimeout(Exception):
    pass


class FakeFirmware:
    def __init__(self, temp_c=23.5):
        self.state = "DISARMED"
        self.temp_c = temp_c
        self.mute = False                 # True 면 응답 안 함 (고장 주입)

    def handle(self, cmd):
        if self.mute:
            return None
        if cmd == "ver":
            return "1.4.2"
        if cmd == "status":
            return self.state
        if cmd == "arm":
            if self.state != "DISARMED":
                return "ERR busy"
            self.state = "ARMED"
            return "OK"
        if cmd == "disarm":
            self.state = "DISARMED"
            return "OK"
        if cmd == "temp":
            return f"TEMP={self.temp_c:.1f}"
        return "ERR unknown"


class FakeSerial:
    def __init__(self, firmware=None):
        self.fw = firmware or FakeFirmware()
        self._rx = []

    def write(self, data):
        reply = self.fw.handle(data.decode().strip())
        if reply is not None:
            self._rx.append(reply.encode() + b"\r\n")

    def readline(self):
        return self._rx.pop(0) if self._rx else b""      # b"" = pyserial 의 timeout

    def close(self):
        pass


class DutClient:
    def __init__(self, port):
        self.port = port

    def send(self, cmd):
        self.port.write(cmd.encode() + b"\n")
        line = self.port.readline()
        if not line:
            raise DutTimeout(f"no reply to {cmd!r}")
        return line.decode().strip()

    def temperature(self):
        return float(self.send("temp").split("=")[1])
