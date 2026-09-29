# 🐍 04 · 시리얼 DUT CLI 드라이버 — timeout · retry · context manager

> DUT의 UART CLI에 명령을 보내고 프롬프트가 나올 때까지 읽는 드라이버를 만든다. 부분 읽기, 전체 응답 deadline, 재시도 전 입력 버퍼 비우기, 에코 제거, `with`로 포트 정리, `key: value` 파싱. 시계를 주입받아서 테스트는 즉시 끝난다.

## 왜 나오나

- JD: "Build and enhance automated testing frameworks and **tools** that facilitate automated testing", "Hands-on experience building, setting up **HIL** test systems"
- HIL 프레임워크의 가장 아래층은 거의 항상 "DUT와 대화하는 드라이버"다. 테스트 코드는 `dut.get_status()`만 부르고, 시리얼의 지저분함(부분 읽기, 무응답, 찌꺼기)은 이 층이 흡수한다
- Don 경험과 직결: 레쥬메의 "automated all running **UART-based sequences** of test cases" (챔버 테스트 플랫폼 SDK). 면접에서 "그 SDK에서 UART timeout을 어떻게 처리했나?"로 이어질 수 있다 → (Don: 실제 SDK의 timeout·retry 방식 채우기)

## 문제

> "Write a small driver for our DUT's serial CLI. `send_command` should send a line and return the response once the prompt shows up. Serial reads come back in random-size pieces. If the DUT doesn't answer in time, retry a couple of times and then fail loudly. Make sure the port always gets closed, even if a test blows up. And give me a helper that turns `key: value` lines into a dict."

- 포트는 pyserial과 같은 모양이라고 가정: `write(bytes)`, `read(size)` (있는 만큼 돌려주고, 없으면 port timeout 동안 기다린 뒤 `b""`), `reset_input_buffer()`, `close()`
- DUT는 받은 명령을 **에코**하고, 응답 뒤에 프롬프트 `"> "`를 찍는다
- 시간은 `clock.now()`로만 읽는다 (테스트에서 `FakeClock` 주입)

실제 pyserial이라면:

```python
import serial
ser = serial.Serial("/dev/ttyUSB0", baudrate=115200, timeout=0.1)   # timeout = read() 한 번의 대기 한도
ser.write(b"status\n")
data = ser.read_until(b"> ")      # expected 가 나오거나 timeout 이면 반환 (그래서 결과를 반드시 확인)
ser.reset_input_buffer()
ser.close()
```

## 예시

```python
clk = FakeClock()
with DutCli(lambda: FakeSerial(clk, status_reply), clock=clk) as dut:
    dut.send_command("status")   # 'state: DISARMED\nvbat: 16.4\nnoise line'
    dut.get_status()             # {'state': 'DISARMED', 'vbat': '16.4'}
```

## 엣지 케이스

- read가 3바이트씩 온다 → 프롬프트가 두 번의 read에 걸쳐 잘릴 수 있다. 그래서 "이번 chunk에 프롬프트가 있나"가 아니라 **누적 버퍼가 프롬프트로 끝나나**를 본다
- DUT 무응답 → 시도당 `timeout` 후 `DutTimeout`, 총 `retries + 1`번 write
- 첫 명령이 유실되고 두 번째는 성공 → 결과는 정상 반환
- 이전 시도에 늦게 도착한 응답이 버퍼에 남아 있음 → 재전송 전에 `reset_input_buffer()`
- 테스트 본문에서 예외 → `__exit__`가 포트를 닫고, 예외는 그대로 전파 (`return False`)
- `vbat: 16.4:extra`처럼 값 안에 콜론 → `partition(":")`은 첫 콜론만 자른다

## 힌트

1. deadline 패턴: `deadline = clock.now() + timeout` 한 번 계산하고, 루프마다 `if clock.now() >= deadline: raise`. read 한 번의 timeout과 **응답 전체의 timeout**은 다른 개념이다
2. 재시도는 `for _ in range(retries + 1): try: return ... except DutTimeout as e: last = e` 후 루프 밖에서 `raise DutTimeout(...) from last`
3. context manager는 `__enter__`에서 열고 `self` 반환, `__exit__`에서 닫고 `False` 반환. (또는 `contextlib.contextmanager` + `try/finally`)

## 풀이 해설

- **층 분리**: `_read_until_prompt`(바이트·시간), `_strip_echo`(텍스트 정리), `send_command`(재시도 정책), `parse_kv`(의미)로 나눴다. 각 층을 따로 테스트할 수 있다
- **시계 주입이 핵심**: `time.monotonic()`을 직접 부르면 timeout 테스트가 실제로 3초 걸린다. CI에서 수백 개 테스트가 이러면 파이프라인이 느려진다. `FakeSerial.read()`가 빈 버퍼일 때 fake clock을 전진시키는 방식으로 "기다림"을 흉내 냈다
- **실제 코드에서는** `time.monotonic()`을 쓴다. `time.time()`은 NTP 보정으로 뒤로 갈 수 있다
- **재시도는 신중하게**: `status` 같은 읽기 명령은 재시도해도 안전하다. 그런데 `arm`, `motor 1 1200` 같은 **부작용 있는 명령**은 첫 명령이 실제로는 실행됐고 응답만 유실됐을 수 있다. 면접에서 이 구분을 먼저 말하면 좋다 (idempotency)
- **흔한 실수**
- chunk 단위로 `if prompt in chunk` 검사 → 경계에서 놓침
- `except Exception: pass`로 모든 에러를 삼켜서 진짜 버그를 숨김
- `__exit__`에서 `True`를 반환해 테스트 실패를 삼켜 버림
- 재시도 전에 입력 버퍼를 안 비워서, 이전 명령의 늦은 응답을 이번 응답으로 착각

## 말하면서 풀기

- "I'll separate the layers: reading bytes until the prompt with a deadline, cleaning up the echo, the retry policy, and parsing. Each piece is testable on its own."
- "There are two timeouts here: the port's per-read timeout, and an overall deadline for the whole response. The loop checks the overall deadline."
- "I inject the clock so timeout tests run instantly in CI. In production it would be time.monotonic, not time.time, because wall-clock time can jump."
- "Retries are fine for read-only commands. For commands with side effects, like arming, I wouldn't blindly retry. I'd check state first, because the command may have executed and only the reply got lost."

## 꼬리 질문

- DUT가 부팅 중이라 CLI가 아직 안 뜨면? → 전원 인가 후 "프롬프트 대기" 단계를 따로 두고, 부트 로그는 문제 01 파서로 저장
- 여러 DUT를 병렬로? → 포트당 객체 하나, `concurrent.futures.ThreadPoolExecutor` (시리얼 I/O는 GIL을 놓으므로 스레드로 충분). 또는 pytest-xdist로 DUT당 워커
- 응답이 프롬프트 없이 비동기로 튀어나오면 (예: 에러 로그)? → 백그라운드 reader 스레드가 줄 단위로 `queue.Queue`에 넣고, 명령 응답과 비동기 이벤트를 분리
- USB-UART가 테스트 중에 끊기면 (`/dev/ttyUSB0` 사라짐)? → `SerialException`을 잡아 포트 재열기, 실패하면 **인프라 오류**로 분류해서 제품 버그와 섞이지 않게 리포트
- 이 드라이버 자체의 테스트는? → 지금처럼 FakeSerial로 단위 테스트, 실제 보드로는 loopback(TX-RX 쇼트) 스모크 테스트

## 파일

- [starter](../starters/04_serial_dut_driver.py) · [모범답안](../solutions/04_serial_dut_driver.py)
- 채점: `python3 python/run.py 04` (내 풀이) · `python3 python/run.py 04 --sol` (답안)
