# ⌨️ D5 · 10-07 (수) 드릴 — pytest · fixture · mock / C 단위 테스트 · fake · DI

> 매일 아침 20분 + 저녁 20분. **정답을 보지 않고** 빈 파일에 친다. 브라우저: [드릴 트레이너](../site/drills.html) (Day 5 탭) · 에디터: `python3 drills/drill.py new 5` → `python3 drills/drill.py check 5`. 틀린 항목은 다음 날 다시.

## Python 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| P5-01 | assert · pytest.raises | given의 divide에 대해: 정상값 테스트 1개, b=0이면 ZeroDivisionError를 match로 확인하는 테스트 1개. |
| P5-02 | parametrize + ids | crc8(given)를 (data, expected) 3쌍으로 parametrize, ids도 붙인다. |
| P5-03 | yield fixture (teardown) | FakePort(given)를 열어 주고, 테스트가 끝나면(실패해도) close 하는 port fixture + 그걸 쓰는 테스트. |
| P5-04 | fixture 조합 + scope | module scope rig fixture(dict로 흉내)와, 그걸 받아 매번 새 dut 상태를 만드는 function scope dut fixture. 테스트 2개에서 rig는 같은 객체. |
| P5-05 | tmp_path | given의 save_log(path, lines)를 tmp_path로 테스트: 파일을 읽어 줄 수 확인. |
| P5-06 | monkeypatch | 환경변수 RIG를 읽는 rig_name()(given)을 monkeypatch.setenv / delenv로 두 경우 테스트. |
| P5-07 | Mock · side_effect · call_args_list | read_temp(port)(given)는 빈 응답이면 한 번 더 보낸다. Mock 포트로 [b"", b"T=21\n"] 대본을 주고, 결과와 write 호출 2번을 검증. |
| P5-08 | patch.object (time.sleep) | backoff_wait()(given)가 time.sleep을 0.1, 0.2로 부르는지, 실제로 기다리지 않고 patch.object로 검증. |
| P5-09 | skipif marker | 환경변수 RIG가 없으면 skip 되는 hw 테스트 하나 (pytest.mark.skipif), 그리고 항상 도는 테스트 하나. |
| P5-10 | caplog | connect()(given)가 재시도 때 WARNING 로그를 남기는지 caplog로 확인. |

### P5-01 · assert · pytest.raises

given의 divide에 대해: 정상값 테스트 1개, b=0이면 ZeroDivisionError를 match로 확인하는 테스트 1개.

주어진 코드 (치지 않음):

```python
import pytest


def divide(a, b):
    if b == 0:
        raise ZeroDivisionError("b must not be zero")
    return a / b
```

정답:

```python
def test_divide():
    assert divide(6, 3) == 2


def test_divide_by_zero():
    with pytest.raises(ZeroDivisionError, match="zero"):
        divide(1, 0)
```

### P5-02 · parametrize + ids

crc8(given)를 (data, expected) 3쌍으로 parametrize, ids도 붙인다.

주어진 코드 (치지 않음):

```python
import pytest


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc
```

정답:

```python
@pytest.mark.parametrize("data, expected", [
    (b"", 0x00),
    (b"123456789", 0xF4),
    (b"\x00", 0x00),
], ids=["empty", "check", "zero"])
def test_crc8(data, expected):
    assert crc8(data) == expected
```

### P5-03 · yield fixture (teardown)

FakePort(given)를 열어 주고, 테스트가 끝나면(실패해도) close 하는 port fixture + 그걸 쓰는 테스트.

주어진 코드 (치지 않음):

```python
import pytest


class FakePort:
    def __init__(self):
        self.closed = False
        self.sent = []

    def write(self, b):
        self.sent.append(b)

    def close(self):
        self.closed = True
```

정답:

```python
@pytest.fixture
def port():
    p = FakePort()
    yield p
    p.close()


def test_write(port):
    port.write(b"PING\n")
    assert port.sent == [b"PING\n"]
    assert not port.closed
```

### P5-04 · fixture 조합 + scope

module scope rig fixture(dict로 흉내)와, 그걸 받아 매번 새 dut 상태를 만드는 function scope dut fixture. 테스트 2개에서 rig는 같은 객체.

주어진 코드 (치지 않음):

```python
import pytest

seen = []
```

정답:

```python
@pytest.fixture(scope="module")
def rig():
    return {"name": "sim", "opened": 1}


@pytest.fixture
def dut(rig):
    seen.append(id(rig))
    return {"rig": rig["name"], "state": "DISARMED"}


def test_one(dut):
    dut["state"] = "ARMED"
    assert dut["rig"] == "sim"


def test_two(dut):
    assert dut["state"] == "DISARMED"
    assert len(set(seen)) == 1
```

### P5-05 · tmp_path

given의 save_log(path, lines)를 tmp_path로 테스트: 파일을 읽어 줄 수 확인.

주어진 코드 (치지 않음):

```python
def save_log(path, lines):
    path.write_text("\n".join(lines) + "\n")
```

정답:

```python
def test_save_log(tmp_path):
    p = tmp_path / "dut.log"
    save_log(p, ["boot", "armed"])
    assert p.read_text().splitlines() == ["boot", "armed"]
```

### P5-06 · monkeypatch

환경변수 RIG를 읽는 rig_name()(given)을 monkeypatch.setenv / delenv로 두 경우 테스트.

주어진 코드 (치지 않음):

```python
import os


def rig_name():
    return os.environ.get("RIG", "none")
```

정답:

```python
def test_rig_from_env(monkeypatch):
    monkeypatch.setenv("RIG", "sim")
    assert rig_name() == "sim"


def test_rig_default(monkeypatch):
    monkeypatch.delenv("RIG", raising=False)
    assert rig_name() == "none"
```

### P5-07 · Mock · side_effect · call_args_list

read_temp(port)(given)는 빈 응답이면 한 번 더 보낸다. Mock 포트로 [b"", b"T=21\n"] 대본을 주고, 결과와 write 호출 2번을 검증.

주어진 코드 (치지 않음):

```python
from unittest.mock import Mock, call


def read_temp(port):
    for _ in range(2):
        port.write(b"T?\n")
        line = port.readline()
        if line:
            return int(line.strip().split(b"=")[1])
    raise TimeoutError
```

정답:

```python
def test_read_temp_retries_once():
    port = Mock(spec=["write", "readline"])
    port.readline.side_effect = [b"", b"T=21\n"]
    assert read_temp(port) == 21
    assert port.write.call_args_list == [call(b"T?\n"), call(b"T?\n")]
```

### P5-08 · patch.object (time.sleep)

backoff_wait()(given)가 time.sleep을 0.1, 0.2로 부르는지, 실제로 기다리지 않고 patch.object로 검증.

주어진 코드 (치지 않음):

```python
import time
from unittest.mock import patch, call


def backoff_wait(n=2, base=0.1):
    for i in range(n):
        time.sleep(base * 2 ** i)
```

정답:

```python
def test_backoff_wait():
    with patch.object(time, "sleep") as fake_sleep:
        backoff_wait()
    assert fake_sleep.call_args_list == [call(0.1), call(0.2)]
```

### P5-09 · skipif marker

환경변수 RIG가 없으면 skip 되는 hw 테스트 하나 (pytest.mark.skipif), 그리고 항상 도는 테스트 하나.

주어진 코드 (치지 않음):

```python
import os
import pytest
```

정답:

```python
needs_rig = pytest.mark.skipif(not os.environ.get("RIG"), reason="no rig (set RIG=sim)")


@needs_rig
def test_on_rig():
    assert os.environ["RIG"]


def test_pure_logic():
    assert int("1F", 16) == 31
```

### P5-10 · caplog

connect()(given)가 재시도 때 WARNING 로그를 남기는지 caplog로 확인.

주어진 코드 (치지 않음):

```python
import logging

log = logging.getLogger("dut")


def connect(attempts):
    for i in range(attempts - 1):
        log.warning("retry %d", i + 1)
    return True
```

정답:

```python
def test_connect_logs_retries(caplog):
    with caplog.at_level(logging.WARNING, logger="dut"):
        assert connect(3)
    assert [r.getMessage() for r in caplog.records] == ["retry 1", "retry 2"]
```

## C 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| C5-01 | CHECK 매크로 (do-while 0) | 실패하면 파일:줄:식을 출력하고 g_fail을 올리는 CHECK(cond) 매크로. |
| C5-02 | 테이블 기반 테스트 | sext12(given)를 {raw, expected} 케이스 배열로 검사하는 test_sext12(void) → 실패 개수. |
| C5-03 | fake HAL (함수 포인터 주입) | i2c_ops_t {int (*read)(void *ctx, uint8_t reg, uint8_t *buf, size_t n); void *ctx;}로 온도(reg 0x00, 2바이트 BE, 단위 0.01도)를 읽는 temp_read(ops, out). 에러면 -1. |
| C5-04 | _Static_assert | wire 헤더 구조체 크기가 4바이트이고 RB_SIZE가 2의 거듭제곱인지 컴파일 타임에 확인. |
| C5-05 | 시간 주입 (now 함수 포인터) | now_fn 타입(uint32_t (*)(void))을 받아 pred가 참이 될 때까지 기다리는 wait_for(pred, now, timeout_ms) → bool. |
| C5-06 | wrap 경계 테스트 | rb_t + rb_push/rb_pop(given)에 대해 head=tail=UINT32_MAX-3에서 10개 넣고 빼며 순서와 개수를 확인하는 test_wrap(void) → 성공 시 true. |
| C5-07 | SWAP 매크로 | if/else 안에서도 안전한 SWAP(type, a, b) 매크로 (do-while 0). |
| C5-08 | qsort 비교 함수 | int32_t 배열을 오름차순 qsort 하는 비교 함수 cmp_i32 (빼기 오버플로 없이). |
| C5-09 | snprintf 잘림 검사 | "fw=<ver> rev=<c>"를 buf에 쓰고, 잘렸으면 false인 fmt_id(buf, cap, ver, rev). |
| C5-10 | 이진 탐색 | 정렬된 int 배열에서 key의 인덱스(없으면 -1) bin_search(a, n, key). mid 오버플로 피하기. |

### C5-01 · CHECK 매크로 (do-while 0)

실패하면 파일:줄:식을 출력하고 g_fail을 올리는 CHECK(cond) 매크로.

정답:

```c
static int g_fail;

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_fail++;                                                 \
        }                                                             \
    } while (0)
```

### C5-02 · 테이블 기반 테스트

sext12(given)를 {raw, expected} 케이스 배열로 검사하는 test_sext12(void) → 실패 개수.

주어진 코드 (치지 않음):

```c
int16_t sext12(uint16_t raw)
{
    raw &= 0x0FFF;
    return (raw & 0x0800) ? (int16_t)(raw - 0x1000) : (int16_t)raw;
}
```

정답:

```c
int test_sext12(void)
{
    static const struct { uint16_t raw; int16_t want; } cases[] = {
        {0x000, 0}, {0x7FF, 2047}, {0x800, -2048}, {0xFFF, -1},
    };
    int fails = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int16_t got = sext12(cases[i].raw);
        if (got != cases[i].want) {
            printf("case %zu: raw=0x%03X got %d want %d\n",
                   i, cases[i].raw, got, cases[i].want);
            fails++;
        }
    }
    return fails;
}
```

### C5-03 · fake HAL (함수 포인터 주입)

i2c_ops_t {int (*read)(void *ctx, uint8_t reg, uint8_t *buf, size_t n); void *ctx;}로 온도(reg 0x00, 2바이트 BE, 단위 0.01도)를 읽는 temp_read(ops, out). 에러면 -1.

정답:

```c
typedef struct {
    int (*read)(void *ctx, uint8_t reg, uint8_t *buf, size_t n);
    void *ctx;
} i2c_ops_t;

int temp_read(const i2c_ops_t *ops, int16_t *out_centi)
{
    uint8_t b[2];
    if (ops->read(ops->ctx, 0x00, b, 2) != 0)
        return -1;
    *out_centi = (int16_t)((b[0] << 8) | b[1]);
    return 0;
}
```

### C5-04 · _Static_assert

wire 헤더 구조체 크기가 4바이트이고 RB_SIZE가 2의 거듭제곱인지 컴파일 타임에 확인.

주어진 코드 (치지 않음):

```c
#define RB_SIZE 64u
typedef struct { uint8_t sync, type; uint16_t len; } wire_hdr_t;
```

정답:

```c
_Static_assert(sizeof(wire_hdr_t) == 4, "wire header must be 4 bytes");
_Static_assert((RB_SIZE & (RB_SIZE - 1u)) == 0, "RB_SIZE must be a power of 2");
```

### C5-05 · 시간 주입 (now 함수 포인터)

now_fn 타입(uint32_t (*)(void))을 받아 pred가 참이 될 때까지 기다리는 wait_for(pred, now, timeout_ms) → bool.

정답:

```c
typedef uint32_t (*now_fn)(void);

bool wait_for(bool (*pred)(void), now_fn now, uint32_t timeout_ms)
{
    uint32_t start = now();
    while (!pred()) {
        if ((uint32_t)(now() - start) >= timeout_ms)
            return false;
    }
    return true;
}
```

### C5-06 · wrap 경계 테스트

rb_t + rb_push/rb_pop(given)에 대해 head=tail=UINT32_MAX-3에서 10개 넣고 빼며 순서와 개수를 확인하는 test_wrap(void) → 성공 시 true.

주어진 코드 (치지 않음):

```c
#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;
static bool rb_push(rb_t *rb, uint8_t b) {
    if (rb->head - rb->tail == RB_SIZE) return false;
    rb->buf[rb->head++ & RB_MASK] = b; return true; }
static bool rb_pop(rb_t *rb, uint8_t *o) {
    if (rb->head == rb->tail) return false;
    *o = rb->buf[rb->tail++ & RB_MASK]; return true; }
```

정답:

```c
bool test_wrap(void)
{
    rb_t rb = {{0}, UINT32_MAX - 3, UINT32_MAX - 3};
    uint8_t v;
    for (uint8_t i = 0; i < 10; i++)
        if (!rb_push(&rb, i))
            return false;
    if (rb.head - rb.tail != 10)
        return false;
    for (uint8_t i = 0; i < 10; i++)
        if (!rb_pop(&rb, &v) || v != i)
            return false;
    return !rb_pop(&rb, &v);
}
```

### C5-07 · SWAP 매크로

if/else 안에서도 안전한 SWAP(type, a, b) 매크로 (do-while 0).

정답:

```c
#define SWAP(type, a, b) \
    do {                     \
        type t_ = (a);       \
        (a) = (b);           \
        (b) = t_;            \
    } while (0)
```

### C5-08 · qsort 비교 함수

int32_t 배열을 오름차순 qsort 하는 비교 함수 cmp_i32 (빼기 오버플로 없이).

정답:

```c
int cmp_i32(const void *pa, const void *pb)
{
    int32_t a = *(const int32_t *)pa;
    int32_t b = *(const int32_t *)pb;
    return (a > b) - (a < b);
}
```

### C5-09 · snprintf 잘림 검사

"fw=<ver> rev=<c>"를 buf에 쓰고, 잘렸으면 false인 fmt_id(buf, cap, ver, rev).

정답:

```c
bool fmt_id(char *buf, size_t cap, const char *ver, char rev)
{
    int n = snprintf(buf, cap, "fw=%s rev=%c", ver, rev);
    return n >= 0 && (size_t)n < cap;
}
```

### C5-10 · 이진 탐색

정렬된 int 배열에서 key의 인덱스(없으면 -1) bin_search(a, n, key). mid 오버플로 피하기.

정답:

```c
int bin_search(const int *a, int n, int key)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == key)
            return mid;
        if (a[mid] < key)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}
```

## 체크

- [ ] Python 10개를 정답 안 보고 → `python3 drills/drill.py check 5` 에서 ✓ 10
- [ ] C 10개를 정답 안 보고 → ✓ 10
- [ ] 틀린 것 ID를 적어 두고 다음 날 아침 첫 5분에 다시
