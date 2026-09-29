# 🐍 N03 · 테스트 자동화를 위한 Python — 면접 속사포 Q&A

> 리크루터가 "Python을 물어본다"고 했다. C 펌웨어 엔지니어가 Python 테스트 코드를 쓸 때 **알아야 하는 것 + 자주 틀리는 것 + 30초 영어 답변**을 한 장에 모았다. 모든 코드 블록은 Python 3.9에서 실행해 확인했다.

## 이 노트 쓰는 법

- 1회독: 각 절의 **Q → 한 줄 답 → 코드**만 훑는다 (40분)
- 2회독: 맨 아래 **30초 영어 답변** 8개를 소리 내어 말한다 (20분)
- 코드는 복사해서 `python3`에 붙여 넣어 직접 돌려 본다. 전부 표준 라이브러리만 쓴다
- 면접 포인트는 "문법을 아느냐"보다 **"HW 테스트 코드에서 왜 이걸 쓰느냐"**를 말할 수 있느냐다. 각 절에 🔧 로 HIL 맥락을 붙였다

---

## 1. 자료형 · 가변성 · 복잡도

### Q. mutable vs immutable?

- immutable: `int`, `float`, `str`, `bytes`, `tuple`, `frozenset` — 값을 바꾸면 **새 객체**가 생긴다
- mutable: `list`, `dict`, `set`, `bytearray`, 대부분의 사용자 클래스 — 제자리에서 바뀐다
- dict key / set 원소는 **hashable**(보통 immutable)이어야 한다. 그래서 `(bus, addr)` 튜플은 key가 되지만 리스트는 안 된다
- 🔧 레지스터 맵을 `{(0x68, 0x75): "WHO_AM_I"}` 처럼 튜플 key로 두는 이유

```python
a = (1, 2)
b = a + (3,)          # 새 튜플
print(a, b)           # (1, 2) (1, 2, 3)

regs = {(0x68, 0x75): "WHO_AM_I"}
print(regs[(0x68, 0x75)])
try:
    {[1, 2]: "x"}
except TypeError as e:
    print("list is unhashable:", e)
```

### Q. 자료구조별 시간 복잡도?

| 연산 | list | dict / set | deque |
|---|---|---|---|
| 인덱스 접근 `x[i]` | O(1) | — | O(n) |
| 끝에 추가/삭제 | O(1) amortized | — | O(1) |
| 앞에 추가/삭제 | **O(n)** | — | **O(1)** |
| 포함 여부 `in` | **O(n)** | **O(1) 평균** | O(n) |
| key 조회/삽입 | — | O(1) 평균 | — |
| 정렬 | O(n log n) | — | — |

- 🔧 "최근 N개 샘플만 유지" → `collections.deque(maxlen=N)` (C의 ring buffer 그대로)
- 🔧 "이미 본 에러 코드인지" → `set`. 리스트로 `in` 하면 로그가 길 때 O(n²)가 된다

```python
from collections import deque, Counter, defaultdict

window = deque(maxlen=3)
for v in [10, 20, 30, 40]:
    window.append(v)
print(list(window))                 # [20, 30, 40] — 오래된 것 자동 폐기

errors = ["E_TIMEOUT", "E_CRC", "E_TIMEOUT"]
print(Counter(errors).most_common(1))   # [('E_TIMEOUT', 2)]

by_bus = defaultdict(list)
by_bus["i2c1"].append(0x68)
print(dict(by_bus))
```

### Q. mutable default argument 버그?

기본값은 **함수 정의 시 한 번만** 만들어진다. 그래서 리스트 기본값은 호출 사이에 공유된다. 면접 단골 1순위.

```python
def add_result_bad(r, results=[]):
    results.append(r)
    return results

print(add_result_bad("PASS"))   # ['PASS']
print(add_result_bad("FAIL"))   # ['PASS', 'FAIL']  ← 버그

def add_result(r, results=None):
    if results is None:
        results = []
    results.append(r)
    return results

print(add_result("PASS"), add_result("FAIL"))   # ['PASS'] ['FAIL']
```

### Q. `is` vs `==`?

- `==` 값 비교(`__eq__`), `is` **같은 객체인지**(id 비교)
- `is`는 `None`, `True/False`, sentinel 객체 비교에만 쓴다: `if x is None`
- 작은 정수·짧은 문자열이 `is`로 True가 나오는 건 CPython 캐싱 구현 디테일이라 믿으면 안 된다

```python
a = [1, 2]
b = [1, 2]
print(a == b, a is b)     # True False
x = None
print(x is None)          # True
```

### Q. shallow copy vs deep copy?

```python
import copy

cfg = {"imu": {"rate_hz": 1000}, "boards": ["A", "B"]}
shallow = dict(cfg)            # 또는 cfg.copy()
deep = copy.deepcopy(cfg)

cfg["imu"]["rate_hz"] = 8000
print(shallow["imu"]["rate_hz"])   # 8000 — 안쪽 dict 공유
print(deep["imu"]["rate_hz"])      # 1000 — 완전 복사
```

- 🔧 테스트마다 기본 설정을 조금씩 바꿔 쓰는 경우 `deepcopy` 안 하면 **테스트끼리 상태가 새서** flaky해진다

---

## 2. Comprehension · Generator · Iterator

### Q. list comprehension vs generator expression?

- `[x for x in ...]` 는 리스트를 **즉시 전부** 만든다 (메모리 O(n))
- `(x for x in ...)` 는 **필요할 때 하나씩** 만든다 (메모리 O(1)), 한 번만 순회 가능
- dict/set comprehension도 있다: `{k: v for ...}`, `{x for ...}`

```python
lines = ["I boot ok", "E crc fail", "I armed", "E timeout"]
errs = [l[2:] for l in lines if l.startswith("E")]
print(errs)                                   # ['crc fail', 'timeout']
lengths = {l[2:]: len(l) for l in lines}
print(sum(len(l) for l in lines))             # 제너레이터: 리스트 안 만든다
```

### Q. generator (`yield`)는 언제 쓰나?

- 함수가 `yield`를 가지면 호출 시 **generator 객체**를 돌려주고, `next()`마다 다음 `yield`까지 실행 후 멈춘다
- 🔧 **몇 GB 짜리 로그** 또는 **끝없는 UART 스트림**을 한 줄씩 처리할 때. 파이프라인처럼 연결 가능

```python
import io, re

def read_lines(f):
    for line in f:                       # 파일 객체도 iterator — 한 줄씩
        yield line.rstrip("\n")

def only_errors(lines):
    pat = re.compile(r"^\[(\d+\.\d+)\] E (\w+)")
    for line in lines:
        m = pat.match(line)
        if m:
            yield float(m.group(1)), m.group(2)

log = io.StringIO("[0.001] I boot\n[0.120] E CRC\n[0.300] I arm\n[0.450] E TIMEOUT\n")
for ts, code in only_errors(read_lines(log)):
    print(ts, code)
```

### Q. iterator protocol?

- iterable: `__iter__()`가 iterator를 돌려준다. iterator: `__next__()`가 다음 값, 끝나면 `StopIteration`
- `for`는 내부적으로 `iter()` → `next()` 반복

```python
class FrameCounter:
    def __init__(self, n):
        self.n, self.i = n, 0
    def __iter__(self):
        return self
    def __next__(self):
        if self.i >= self.n:
            raise StopIteration
        self.i += 1
        return self.i

print(list(FrameCounter(3)))      # [1, 2, 3]
it = iter([10, 20])
print(next(it), next(it), next(it, "done"))
```

- 유용한 도구: `enumerate`, `zip`, `itertools.islice`(앞 N개), `itertools.groupby`, `itertools.chain`, `any`/`all`

---

## 3. 함수 · `*args/**kwargs` · Closure · Decorator

### Q. `*args`, `**kwargs`?

```python
def send_cmd(cmd, *args, timeout=1.0, **opts):
    return cmd, args, timeout, opts

print(send_cmd("SET", "rate", 1000, timeout=0.5, retries=3))
# ('SET', ('rate', 1000), 0.5, {'retries': 3})

params = {"timeout": 2.0}
print(send_cmd("GET", **params))   # dict 풀어서 전달
```

- `timeout=`처럼 `*args` 뒤에 오는 인자는 **keyword-only** → 테스트 코드 가독성 좋다

### Q. closure?

안쪽 함수가 바깥 함수의 변수를 **기억**하는 것. 바깥 변수를 바꾸려면 `nonlocal`.

```python
def make_counter():
    count = 0
    def inc():
        nonlocal count
        count += 1
        return count
    return inc

c = make_counter()
c(); c()
print(c())        # 3
```

### Q. decorator란?

**함수를 받아 감싼 함수를 돌려주는 함수**. `@deco`는 `f = deco(f)`의 문법 설탕. `functools.wraps`로 원래 이름·docstring을 보존한다.
🔧 테스트에서: retry, timing, "하드웨어 없으면 skip", 로깅.

```python
import functools, time

def retry(times=3, exceptions=(IOError,), delay=0.0):
    def deco(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            last = None
            for attempt in range(1, times + 1):
                try:
                    return fn(*args, **kwargs)
                except exceptions as e:
                    last = e
                    time.sleep(delay)
            raise last
        return wrapper
    return deco

calls = {"n": 0}

@retry(times=3)
def flaky_read():
    """Read a register that fails twice."""
    calls["n"] += 1
    if calls["n"] < 3:
        raise IOError("NACK")
    return 0x68

print(hex(flaky_read()), calls["n"], flaky_read.__name__, flaky_read.__doc__)
```

- 주의: 인자를 받는 decorator(`@retry(times=3)`)는 **3단 중첩**이다. 인자 없는 decorator는 2단
- 흔한 실수: `wraps` 빼먹기 → pytest 리포트에 전부 `wrapper`로 찍힌다

---

## 4. Context Manager — HW 자원 정리

### Q. `with`는 뭘 보장하나?

블록을 어떻게 빠져나가든(정상, 예외, return) `__exit__`이 **반드시** 호출된다. C의 `goto cleanup` 패턴을 언어가 해 주는 것.
🔧 serial port close, 전원 relay OFF, DUT disarm, 임시 설정 복원 — **테스트가 실패해도 기체가 armed로 남으면 안 된다.**

```python
class PowerSupply:
    def __init__(self, name):
        self.name, self.on = name, False
    def __enter__(self):
        self.on = True
        print(f"{self.name}: ON")
        return self
    def __exit__(self, exc_type, exc, tb):
        self.on = False
        print(f"{self.name}: OFF (exc={exc_type.__name__ if exc_type else None})")
        return False           # True를 돌려주면 예외를 삼킨다 — 보통 False

try:
    with PowerSupply("PSU1") as psu:
        raise RuntimeError("test failed mid-way")
except RuntimeError:
    pass
print("still on?", psu.on)     # False
```

### Q. `contextlib.contextmanager`?

generator 하나로 context manager를 만든다. `yield` 앞 = setup, 뒤(`finally`) = teardown.

```python
from contextlib import contextmanager

@contextmanager
def temp_param(dut, key, value):
    old = dut[key]
    dut[key] = value
    try:
        yield dut
    finally:
        dut[key] = old          # 예외가 나도 원래 값 복원

dut = {"motor_idle_pct": 5}
with temp_param(dut, "motor_idle_pct", 10):
    print(dut["motor_idle_pct"])   # 10
print(dut["motor_idle_pct"])       # 5
```

- 이 패턴이 그대로 **pytest `yield` fixture**의 모양이다 (N04 참고)

---

## 5. 예외 처리

### Q. 예외 계층은 어떻게 설계하나?

```python
class DutError(Exception):
    """Base for everything the DUT layer raises."""

class DutTimeout(DutError):
    pass

class DutProtocolError(DutError):
    pass

def parse_frame(raw):
    try:
        length = raw[1]
        return raw[2:2 + length]
    except IndexError as e:
        raise DutProtocolError(f"short frame: {raw!r}") from e   # 원인 체인 보존

try:
    parse_frame(b"\xc8")
except DutError as e:                       # base로 한 번에 잡기
    print(type(e).__name__, "|", e, "| cause:", repr(e.__cause__))
finally:
    print("finally always runs")
```

- `try / except / else / finally`: `else`는 예외가 **없을 때만**, `finally`는 **항상**
- `raise X from e` → traceback에 "The above exception was the direct cause" 로 원인이 남는다. 디버깅용 증거 보존
- 안티패턴: `except:` 또는 `except Exception: pass` — 버그를 삼켜서 **테스트가 거짓 PASS** 한다. 테스트 인프라에서 가장 위험한 실수
- 🔧 timeout과 protocol 에러를 구분하면 "DUT가 죽었나 vs 링크가 깨졌나"를 리포트에서 바로 가를 수 있다

---

## 6. 클래스 · dataclass · property · dunder

### Q. `@dataclass`는 왜 쓰나?

`__init__`, `__repr__`, `__eq__`를 자동 생성. 테스트 결과·측정값 같은 **데이터 묶음**에 딱.

```python
from dataclasses import dataclass, field, asdict
from typing import List

@dataclass
class TestResult:
    name: str
    passed: bool
    duration_s: float = 0.0
    tags: List[str] = field(default_factory=list)   # mutable 기본값은 이렇게

r = TestResult("test_arm_disarm", True, 1.25)
print(r)
print(asdict(r))
print(r == TestResult("test_arm_disarm", True, 1.25))   # True

@dataclass(frozen=True)
class RegAddr:
    bus: int
    addr: int
print({RegAddr(1, 0x68): "imu"})                        # frozen → hashable
```

### Q. `@property`?

속성처럼 읽히지만 실제로는 메서드. 검증·계산값에 사용.

```python
class Motor:
    def __init__(self):
        self._throttle = 0
    @property
    def throttle(self):
        return self._throttle
    @throttle.setter
    def throttle(self, v):
        if not 0 <= v <= 2047:
            raise ValueError(f"DShot throttle out of range: {v}")
        self._throttle = v

m = Motor()
m.throttle = 1000
print(m.throttle)
try:
    m.throttle = 3000
except ValueError as e:
    print(e)
```

### Q. 자주 쓰는 dunder method?

| dunder | 언제 호출 | 테스트 코드 용도 |
|---|---|---|
| `__init__` | 생성 | 설정 |
| `__repr__` | `repr()`, 디버거, assert 실패 메시지 | 실패 로그를 읽기 좋게 |
| `__str__` | `print()`, `str()` | 사용자용 출력 |
| `__eq__` / `__hash__` | `==`, dict key | 결과 비교 |
| `__len__`, `__getitem__`, `__iter__` | `len()`, `x[i]`, `for` | 버퍼·레지스터 맵을 컨테이너처럼 |
| `__enter__` / `__exit__` | `with` | 자원 정리 |
| `__call__` | `obj()` | callable 객체 |

- 🔧 `__repr__`를 잘 만들면 assert 실패 시 `Frame(type=0x16, len=24, crc_ok=False)` 처럼 바로 원인이 보인다

### Q. 상속 vs 조합, ABC?

- HW 추상화: `abc.ABC` + `@abstractmethod`로 인터페이스를 정의하고, 실제 serial 구현과 Fake 구현을 **갈아 끼운다** (dependency injection)
- 🔧 그래서 CI에서는 Fake로, HIL 랙에서는 진짜 하드웨어로 같은 테스트가 돈다

```python
from abc import ABC, abstractmethod

class Transport(ABC):
    @abstractmethod
    def write(self, data: bytes) -> None: ...
    @abstractmethod
    def read(self, n: int) -> bytes: ...

class FakeTransport(Transport):
    def __init__(self, reply: bytes):
        self.sent, self.reply = b"", reply
    def write(self, data):
        self.sent += data
    def read(self, n):
        out, self.reply = self.reply[:n], self.reply[n:]
        return out

t = FakeTransport(b"OK\r\n")
t.write(b"PING\r\n")
print(t.sent, t.read(4))
try:
    Transport()
except TypeError as e:
    print("abstract:", e)
```

### Q. type hint는 실행에 영향 있나?

없다. 런타임에 검사하지 않는다. `mypy`/IDE가 정적으로 검사한다. 3.9에서는 `list[int]`는 되지만 `int | None`은 안 된다 → `Optional[int]` 사용.

```python
from typing import Optional, Dict, Callable

def find_dev(addrs: Dict[str, int], name: str) -> Optional[int]:
    return addrs.get(name)

Handler = Callable[[bytes], None]
print(find_dev({"imu": 0x68}, "baro"))   # None
```

---

## 7. bytes · struct · 비트 연산 — C 개발자 핵심 구역

### Q. `bytes` vs `bytearray` vs `memoryview`?

| 타입 | 가변 | 용도 |
|---|---|---|
| `bytes` | ❌ | 받은 프레임, 상수 |
| `bytearray` | ✅ | 수신 버퍼 누적, 제자리 수정 |
| `memoryview` | 원본 따라감 | **복사 없이** 슬라이스 (zero-copy) |

```python
buf = bytearray()
buf += b"\xc8\x04\x16"
buf += b"\x01\x02\x5a"
print(buf.hex(" "))              # c8 04 16 01 02 5a
print(buf[0], type(buf[0]))      # 200 <class 'int'> — 인덱스 하나는 int!
print(buf[0:1])                  # bytearray(b'\xc8') — 슬라이스는 bytes류
del buf[:3]                      # 처리한 앞부분 버리기
print(bytes(buf))

mv = memoryview(bytearray(b"\x00\x01\x02\x03"))
part = mv[1:3]                   # 복사 없음
part[0] = 0xFF
print(bytes(mv))                 # b'\x00\xff\x02\x03'
print(bytes.fromhex("DE AD BE EF"))
```

### Q. `struct`로 packed 구조체 읽기? endianness?

포맷 첫 글자: `<` little-endian, `>` big-endian(network), `=` native 무패딩, `@` native **패딩 있음**(C struct와 동일 정렬). 프로토콜 파싱엔 **항상 `<` 또는 `>`를 명시**.

| 코드 | C 타입 | 크기 |
|---|---|---|
| `b` / `B` | int8 / uint8 | 1 |
| `h` / `H` | int16 / uint16 | 2 |
| `i` / `I` | int32 / uint32 | 4 |
| `q` / `Q` | int64 / uint64 | 8 |
| `f` / `d` | float / double | 4 / 8 |

```python
import struct

# MSP_ATTITUDE 응답 payload: int16 roll(0.1deg), int16 pitch(0.1deg), int16 yaw(deg), little-endian
payload = struct.pack("<hhh", -123, 45, 270)
print(payload.hex(" "))                     # 85 ff 2d 00 0e 01
roll, pitch, yaw = struct.unpack("<hhh", payload)
print(roll / 10, pitch / 10, yaw)           # -12.3 4.5 270

print(struct.calcsize("<BHI"), struct.calcsize("@BHI"))   # 7 vs 8 (패딩)

rec = struct.Struct("<IHh")                 # 미리 컴파일 → 반복 파싱에 빠름
data = rec.pack(1000, 7, -1) * 2
for ts, seq, val in rec.iter_unpack(data):
    print(ts, seq, val)
```

### Q. `int.from_bytes` / `to_bytes`?

```python
raw = b"\xff\xfe"
print(int.from_bytes(raw, "big"))                   # 65534
print(int.from_bytes(raw, "big", signed=True))      # -2
print((0x1234).to_bytes(2, "little").hex())         # 3412
print((-2).to_bytes(2, "big", signed=True).hex())   # fffe
```

### Q. Python int로 비트 연산할 때 주의점?

Python `int`는 **크기 제한이 없다**(오버플로 없음). 그래서 C처럼 자동으로 잘리지 않는다 → **직접 마스킹**. `~x`는 `-x-1`이 된다.

```python
x = 0xFFFF
print(x + 1, (x + 1) & 0xFFFF)          # 65536 0 — 직접 wrap
print(~0x0F, ~0x0F & 0xFF)              # -16 240

def to_signed(v, bits):
    """2의 보수 해석: uint → int"""
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v

print(to_signed(0xFFFE, 16), to_signed(0x7FFF, 16), to_signed(0x800, 12))  # -2 32767 -2048

# 레지스터 필드: bits[5:3] 읽기 / 쓰기 (read-modify-write)
reg = 0b1010_1100
field = (reg >> 3) & 0b111
print(bin(field))                        # 0b101
reg = (reg & ~(0b111 << 3) & 0xFF) | (0b010 << 3)
print(f"{reg:#010b}")                    # 0b10010100

def rol8(v, n):
    return ((v << n) | (v >> (8 - n))) & 0xFF
print(hex(rol8(0x81, 1)))                # 0x3
print(bin(0xF0).count("1"), (0xF0).bit_length())   # popcount 4, 8
```

### Q. hex 포맷팅?

```python
v = 0x1A
print(f"{v:x} {v:X} {v:#x} {v:02x} {v:#06x} {v:08b}")   # 1a 1A 0x1a 1a 0x001a 00011010
print(" ".join(f"{b:02x}" for b in b"\x01\xab"))       # 01 ab
print(hex(255), int("ff", 16), int("0x1f", 0), int("0b101", 0))
```

### 예제: CRC-8 (poly 0xD5, CRSF가 쓰는 것)

```python
def crc8_d5(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc

print(hex(crc8_d5(b"123456789")))   # 0xbc (CRC-8/DVB-S2 check 값)
```

- C 버전과 다른 점은 딱 하나: `& 0xFF`로 **매번 8비트로 잘라야 한다**

---

## 8. 동시성 — GIL · threading · multiprocessing · asyncio

### Q. GIL이 뭐고 테스트 코드에 무슨 영향?

- CPython은 한 번에 **한 스레드만 Python 바이트코드를 실행**하게 막는 Global Interpreter Lock이 있다
- 그래서 **CPU-bound** 작업(큰 로그 파싱, FFT)은 스레드로 빨라지지 않는다 → `multiprocessing` / `concurrent.futures.ProcessPoolExecutor`
- **I/O-bound** 작업(serial read, socket, sleep)은 블로킹 I/O 동안 GIL을 놓아주므로 **스레드로 충분**하다
- 🔧 HIL 테스트 대부분은 I/O-bound: UART 수신, 전원 제어, CAN/Ethernet 수신 → thread 또는 asyncio

| 선택지 | 적합한 일 | 주의 |
|---|---|---|
| `threading` | 블로킹 I/O 여러 개 동시 (serial reader + 테스트 본체) | 공유 상태는 `Queue`/`Lock` |
| `multiprocessing` | CPU-bound 병렬, 격리 | pickle 가능한 데이터만, 기동 비용 |
| `asyncio` | 많은 I/O를 단일 스레드에서 (소켓 수십 개) | 블로킹 호출 하나가 전체를 멈춤 |

### Q. 테스트를 막지 않고 serial을 계속 읽으려면? (가장 실전적인 질문)

**reader thread + `queue.Queue`** producer-consumer. reader는 한 줄씩 큐에 넣고, 테스트는 `expect(pattern, timeout)`으로 기다린다. 동시에 전 로그를 파일로 남긴다.

```python
import queue, threading, time, re

class FakeSerial:
    """pyserial의 readline()을 흉내 — 주기적으로 한 줄씩."""
    def __init__(self, lines):
        self.lines = list(lines)
    def readline(self):
        time.sleep(0.01)
        return (self.lines.pop(0) + "\n").encode() if self.lines else b""

class SerialMonitor:
    def __init__(self, port):
        self.port = port
        self.q = queue.Queue()
        self.log = []
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._run, daemon=True)
    def __enter__(self):
        self._t.start()
        return self
    def __exit__(self, *exc):
        self._stop.set()
        self._t.join(timeout=1)
        return False
    def _run(self):
        while not self._stop.is_set():
            raw = self.port.readline()          # 블로킹 I/O (진짜 pyserial은 timeout 설정)
            if raw:
                line = raw.decode(errors="replace").rstrip()
                self.log.append(line)
                self.q.put(line)
    def expect(self, pattern, timeout=1.0):
        pat = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"no match for {pattern!r}")
            try:
                line = self.q.get(timeout=remaining)
            except queue.Empty:
                continue
            m = pat.search(line)
            if m:
                return m

port = FakeSerial(["boot v1.2.3", "imu ok", "ARMED"])
with SerialMonitor(port) as mon:
    print(mon.expect(r"boot v(\S+)").group(1))
    mon.expect(r"ARMED", timeout=0.5)
    try:
        mon.expect(r"FAILSAFE", timeout=0.1)
    except TimeoutError as e:
        print("timeout:", e)
print(mon.log)
```

- 핵심 포인트: `time.monotonic()` deadline, `Queue.get(timeout=...)`, `Event`로 종료, `daemon=True`, `with`로 반드시 join
- 진짜 pyserial은 `serial.Serial(port, 115200, timeout=0.1)` — timeout을 안 주면 `read`가 영원히 블록된다

### Q. asyncio 버전은?

```python
import asyncio

async def read_telemetry(name, n, period):
    for i in range(n):
        await asyncio.sleep(period)        # 여기서 다른 코루틴에 양보
    return f"{name}: {n} frames"

async def main():
    results = await asyncio.gather(
        read_telemetry("uart", 3, 0.01),
        read_telemetry("udp", 2, 0.02),
    )
    print(results)
    try:
        await asyncio.wait_for(read_telemetry("slow", 5, 0.1), timeout=0.05)
    except asyncio.TimeoutError:
        print("wait_for timed out")

asyncio.run(main())
```

- 주의: 코루틴 안에서 `time.sleep()` 같은 블로킹 호출을 하면 **이벤트 루프 전체가 멈춘다**. 블로킹 라이브러리는 `loop.run_in_executor`로 넘긴다

### Q. `Lock`이 필요한 경우?

`x += 1`도 원자적이지 않다(읽기-더하기-쓰기). 여러 스레드가 공유 카운터를 바꾸면 `threading.Lock`. 가능하면 공유 상태 대신 `Queue`로 메시지를 넘긴다.

```python
import threading
count = 0
lock = threading.Lock()
def worker():
    global count
    for _ in range(10000):
        with lock:
            count += 1
ts = [threading.Thread(target=worker) for _ in range(4)]
for t in ts: t.start()
for t in ts: t.join()
print(count)    # 40000
```

---

## 9. 외부 툴 · 로깅 · 파일

### Q. flash 툴을 Python에서 돌리려면? (subprocess)

```python
import subprocess, sys

# 실제로는 ["openocd", "-f", "board.cfg", "-c", "program fw.elf verify reset exit"] 같은 것
cmd = [sys.executable, "-c", "print('Programming Finished'); print('verified')"]
r = subprocess.run(cmd, capture_output=True, text=True, timeout=30, check=True)
print(r.returncode, r.stdout.splitlines())

try:
    subprocess.run([sys.executable, "-c", "import sys; sys.exit(3)"], check=True, timeout=5)
except subprocess.CalledProcessError as e:
    print("flash failed, rc =", e.returncode)

try:
    subprocess.run([sys.executable, "-c", "import time; time.sleep(5)"], timeout=0.2)
except subprocess.TimeoutExpired:
    print("flash tool hung → kill & mark DUT as infra failure")
```

- 규칙: **리스트 인자**(쉘 인젝션 방지, `shell=True` 피하기) · `timeout` 필수 · `check=True`로 실패를 예외로 · stdout을 artifact로 저장
- 🔧 flash 실패(infra 문제)와 테스트 실패(제품 문제)를 **다른 결과로 분류**해야 CI가 거짓 빨간불을 안 낸다

### Q. `print` 대신 `logging`을 쓰는 이유?

레벨(DEBUG/INFO/WARNING/ERROR), 타임스탬프, 모듈 이름, 여러 출력(콘솔 + 파일) 동시 지원. pytest도 logging을 캡처해서 실패 시 보여 준다.

```python
import logging, io

stream = io.StringIO()
log = logging.getLogger("hil.dut")
log.setLevel(logging.DEBUG)
h = logging.StreamHandler(stream)
h.setFormatter(logging.Formatter("%(levelname)s %(name)s: %(message)s"))
log.addHandler(h)

log.debug("tx %s", b"\x24\x4d\x3c".hex())    # %-포맷은 레벨이 꺼져 있으면 문자열을 안 만든다
log.error("no response after %d retries", 3)
print(stream.getvalue())
```

### Q. pathlib · json · csv · argparse?

```python
import json, csv, io, argparse, tempfile
from pathlib import Path

out = Path(tempfile.mkdtemp()) / "run_42"
out.mkdir(parents=True, exist_ok=True)
(out / "result.json").write_text(json.dumps({"test": "arm", "passed": True}, indent=2))
print(json.loads((out / "result.json").read_text())["passed"], [p.name for p in out.glob("*.json")])

rows = io.StringIO("t,roll,pitch\n0.0,1.5,-0.2\n0.1,1.7,-0.1\n")
data = list(csv.DictReader(rows))
print(max(float(r["roll"]) for r in data))

p = argparse.ArgumentParser(description="run HIL suite")
p.add_argument("--port", default="/dev/ttyACM0")
p.add_argument("--baud", type=int, default=115200)
p.add_argument("--board", choices=["A", "B"], required=True)
p.add_argument("-v", "--verbose", action="store_true")
args = p.parse_args(["--board", "B", "-v"])
print(args)
```

### Q. 정규식 (`re`)?

```python
import re
line = "[  12.345] FC: vbat=16.21V rssi=-71dBm lq=100%"
m = re.search(r"vbat=(?P<v>\d+\.\d+)V rssi=(?P<rssi>-?\d+)dBm", line)
print(float(m["v"]), int(m["rssi"]))
print(re.findall(r"(\w+)=(-?[\d.]+)", line))
```

- `match`는 문자열 **앞에서만**, `search`는 어디서든, `fullmatch`는 전체. 반복 사용은 `re.compile`. raw string `r"..."` 필수

### Q. `time.monotonic()` vs `time.time()`?

- `time.time()`은 벽시계 — NTP 보정·수동 변경으로 **뒤로 갈 수 있다**. 로그에 찍을 절대 시각용
- `time.monotonic()`은 절대 뒤로 안 간다 → **timeout·경과 시간 측정은 항상 monotonic**
- `time.perf_counter()`는 해상도가 가장 높은 monotonic 계열 → 짧은 구간 벤치마크

```python
import time
t0 = time.monotonic()
time.sleep(0.05)
print(f"elapsed {time.monotonic() - t0:.3f}s")
```

---

## 10. 환경 · 툴링 · 스타일

### Q. virtualenv / pip / requirements?

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install pyserial pytest
pip freeze > requirements.txt        # 버전 고정 → HIL 러너마다 같은 환경
pip install -r requirements.txt
```

- 🔧 HIL 러너가 여러 대면 **파이썬 환경도 재현 가능해야** 한다: requirements 고정(또는 lock 파일), Docker 이미지, 러너 셋업 스크립트
- 최신 도구: `pyproject.toml`, `uv`/`poetry` (들어는 봤다 수준이면 충분)

### Q. 코드 품질 도구?

| 도구 | 역할 |
|---|---|
| PEP 8 | 스타일 가이드 (snake_case, 4칸 들여쓰기, 79/88자) |
| `black` | 자동 포매터 — 스타일 논쟁 종료 |
| `ruff` | 초고속 linter (flake8 + isort 등 대체) |
| `mypy` | 타입 힌트 정적 검사 |
| `pytest` + `coverage` | 테스트 러너 + 커버리지 |
| `pre-commit` | 커밋 전에 위 도구 자동 실행 |

- 🔧 "테스트 프레임워크 코드도 제품 코드처럼": CI에서 `ruff` + `mypy` + 프레임워크 자체의 unit test를 먼저 돌린다. HIL 시간은 비싸니까 **프레임워크 버그로 랙 시간을 날리지 않게**

---

## 11. C 개발자가 Python에서 자주 틀리는 것

| 실수 | 증상 | 올바른 방법 |
|---|---|---|
| int가 8/16/32비트로 잘린다고 가정 | CRC·checksum 값이 커져서 틀림 | 매 단계 `& 0xFF` / `& 0xFFFF` |
| `~x`를 unsigned NOT으로 생각 | 음수가 나옴 | `~x & 0xFF` 또는 `x ^ 0xFF` |
| `b[0]`이 bytes라고 생각 | `b[0] == b"\x01"` 이 False | 인덱스는 `int`, 슬라이스 `b[0:1]`이 bytes |
| `str`과 `bytes` 섞기 | `TypeError: can't concat str to bytes` | 경계에서 `.encode()` / `.decode()` 한 번만 |
| `/` 를 정수 나눗셈으로 사용 | `7/2 == 3.5` | 정수 나눗셈은 `//`, 음수는 `-7//2 == -4` (내림) |
| `%`의 음수 결과 | `-1 % 256 == 255` | C와 다름. 오히려 wrap에 편리 |
| 리스트 기본 인자 | 호출 간 결과 누적 | `None` 기본값 |
| `a = b` 가 복사라고 생각 | 한쪽 수정이 다른 쪽에 반영 | `list(b)`, `b.copy()`, `deepcopy` |
| `struct` 포맷에 `<`/`>` 생략 | 패딩·네이티브 endian으로 파싱 | 항상 byte order 명시 |
| 루프 안에서 `lst.pop(0)` / `in list` | O(n²)로 느려짐 | `deque.popleft()`, `set` |
| 스레드면 병렬로 빨라진다 | CPU 작업은 그대로 | GIL: CPU는 multiprocessing |
| `time.time()`으로 timeout | 시계 보정 시 오동작 | `time.monotonic()` |
| `except:` 로 전부 잡기 | 버그를 삼켜 거짓 PASS | 구체 예외만, 나머지는 올린다 |
| `==`로 float 비교 | 센서 값 비교가 가끔 실패 | `math.isclose(a, b, rel_tol=, abs_tol=)` / `pytest.approx` |

```python
import math
print(7 // 2, -7 // 2, -1 % 256, 0.1 + 0.2 == 0.3, math.isclose(0.1 + 0.2, 0.3))
print(b"\x01"[0] == 1, b"\x01"[0:1] == b"\x01")
```

---

## 12. 30초 영어 답변 — 가장 나올 법한 8개

### 1) What's a decorator? Have you written one?

> A decorator is a function that takes a function and returns a wrapped version of it — `@retry` is just `f = retry(f)`. In test code I use them for cross-cutting behavior: retrying a flaky bus read with backoff, timing a step, or skipping a test when the required hardware isn't on the rig. I always use `functools.wraps` so the test report keeps the original function name.

### 2) What is the GIL, and does it matter for test automation?

> The GIL means only one thread executes Python bytecode at a time in CPython. It hurts CPU-bound work, so for heavy log parsing I'd use multiprocessing. But most HIL work is I/O-bound — serial, sockets, power control — and blocking I/O releases the GIL, so threads or asyncio work fine for that.

### 3) Generator vs list — when would you use each?

> A list materializes everything in memory; a generator produces values lazily, one at a time, and can only be iterated once. For a multi-gigabyte flight log or an endless UART stream I chain generators — read lines, filter errors, parse fields — so memory stays constant. If I need random access or multiple passes, I use a list.

### 4) How would you read a serial port continuously without blocking the test?

> I'd run a background reader thread that owns the port, reads lines with a short timeout, writes everything to a log file, and pushes each line onto a `queue.Queue`. The test calls something like `expect(pattern, timeout)`, which pulls from the queue against a monotonic deadline. A stop event plus a context manager guarantees the thread is joined and the port is closed even when the test fails.

### 5) How do you make sure hardware is left in a safe state if a test fails?

> Context managers and fixtures with teardown in a `finally` block. Setup arms or powers the DUT, the test runs, and teardown always disarms, cuts motor output, and restores parameters — regardless of how the test exits. With drones that's a safety requirement, not just hygiene.

### 6) How do you parse a binary telemetry frame in Python?

> I accumulate bytes in a `bytearray`, scan for the sync byte, check the length field, verify the CRC — masking to eight bits since Python ints don't overflow — and then decode the payload with `struct.unpack` using an explicit byte order like `'<hhh'`. On a bad CRC I drop one byte and resync instead of throwing away the whole buffer.

### 7) Mutable default argument — what's the problem?

> Default values are evaluated once, when the function is defined, so a list default is shared across every call and results leak between calls — which in a test framework shows up as tests contaminating each other. The fix is a `None` default and creating the list inside the function; in dataclasses, `field(default_factory=list)`.

### 8) Threads, multiprocessing, or asyncio — how do you choose?

> It depends on where the time goes. Blocking I/O on a few devices — threads, simplest to reason about with queues. Hundreds of concurrent sockets — asyncio, as long as nothing blocks the event loop. CPU-heavy analysis like FFTs over flight logs — multiprocessing, to get around the GIL.

---

## 13. 5분 자가 점검

- `[] is []` 는? → False (다른 객체)
- `struct.unpack("<H", b"\x34\x12")` → `(0x1234,)` = 4660
- `int.from_bytes(b"\x80", "big", signed=True)` → -128
- `(0xFF + 1) & 0xFF` → 0
- `-7 // 2` → -4
- `list(zip("ab", [1, 2, 3]))` → `[('a', 1), ('b', 2)]` (짧은 쪽에서 끝)
- `@retry(3)` 는 몇 단 중첩 함수? → 3단 (factory → decorator → wrapper)
- `with` 블록에서 예외가 나면 `__exit__`은? → 호출된다. `True`를 돌려주면 예외를 삼킨다
- timeout 측정 시계는? → `time.monotonic()`

```python
import struct
print([] is [], struct.unpack("<H", b"\x34\x12"), int.from_bytes(b"\x80", "big", signed=True),
      (0xFF + 1) & 0xFF, -7 // 2, list(zip("ab", [1, 2, 3])))
```

---

## 체크리스트

- [ ] 1~7절 코드 블록을 `python3`에 직접 붙여 넣어 돌려 봤다
- [ ] mutable default / `is` vs `==` / shallow vs deep copy 를 10초 안에 설명할 수 있다
- [ ] `retry` decorator를 **안 보고** 3단 중첩으로 쓸 수 있다
- [ ] `@contextmanager`로 "임시 파라미터 변경 후 복원"을 쓸 수 있다
- [ ] `struct` 포맷 문자(`<`, `>`, `b B h H i I f`)를 외웠다
- [ ] `to_signed(v, bits)` 와 CRC-8 루프를 `& 0xFF` 포함해서 쓸 수 있다
- [ ] SerialMonitor (reader thread + Queue + expect) 구조를 화이트보드로 그릴 수 있다
- [ ] GIL / threading vs multiprocessing vs asyncio 선택 기준을 영어로 말할 수 있다
- [ ] subprocess에 `timeout` + `check=True`를 쓰는 이유(infra 실패 분류)를 말할 수 있다
- [ ] 11절 "C 개발자 실수" 표를 한 번 소리 내어 읽었다
- [ ] 12절 30초 영어 답변 8개를 각각 두 번씩 소리 내어 말했다
