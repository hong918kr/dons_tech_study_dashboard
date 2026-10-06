# --- solution ---
import time


class SensorTimeout(Exception):
    pass


def read_temperature(port, retries=2, sleep=time.sleep):
    """b"TEMP?\\n" 를 보내고 b"TEMP=23.5\\r\\n" 을 받는다. 빈 응답(타임아웃)이면 0.1, 0.2 초 쉬고 재시도."""
    for attempt in range(retries + 1):
        if attempt:
            sleep(0.1 * 2 ** (attempt - 1))
        port.write(b"TEMP?\n")
        line = port.readline()
        if line:
            return float(line.strip().split(b"=")[1])
    raise SensorTimeout("no response")
# --- end ---
