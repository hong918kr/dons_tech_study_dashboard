# ⌨️ D2 · 10-04 (일) 드릴 — 클래스 · 예외 · with · generator / C 메모리 · 함수 포인터 · 구조체

> 매일 아침 20분 + 저녁 20분. **정답을 보지 않고** 빈 파일에 친다. 브라우저: [드릴 트레이너](../site/drills.html) (Day 2 탭) · 에디터: `python3 drills/drill.py new 2` → `python3 drills/drill.py check 2`. 틀린 항목은 다음 날 다시.

## Python 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| P2-01 | class · __init__ · __repr__ | Sensor(name, value=0.0) 클래스. update(v)로 값 갱신, repr은 "Sensor(imu, 1.5)". |
| P2-02 | @dataclass | Frame(msg_id: int, payload: bytes = b"") dataclass와 길이를 돌려주는 size property. |
| P2-03 | 커스텀 예외 계층 | DeviceError(Exception), 그 자식 DeviceTimeout. check(code): 0이면 None, 1이면 DeviceTimeout, 그 외 DeviceError(f"code {code}"). |
| P2-04 | try / except | 정수로 바꿀 수 없으면 default를 돌려주는 parse_int(s, default=None). |
| P2-05 | context manager 클래스 | with Timer() as t: 블록이 끝나면 t.elapsed(초)가 채워지는 Timer. time.monotonic 사용. |
| P2-06 | @contextmanager | opened(log): 들어갈 때 log에 "open", 나올 때(예외가 나도) "close"를 append. |
| P2-07 | generator | data를 n바이트씩 잘라 yield 하는 chunks(data, n). |
| P2-08 | defaultdict | 첫 글자별로 단어를 묶는 group_by_first(words) → {첫글자: [단어...]}. |
| P2-09 | dunder: __len__ · __getitem__ | Registers(n): n개 0으로 시작, len()과 r[i] 읽기/쓰기(__setitem__) 지원. |
| P2-10 | @property + 검증 | Motor.throttle property: 0~100만 허용, 아니면 ValueError. |

### P2-01 · class · __init__ · __repr__

Sensor(name, value=0.0) 클래스. update(v)로 값 갱신, repr은 "Sensor(imu, 1.5)".

정답:

```python
class Sensor:
    def __init__(self, name, value=0.0):
        self.name = name
        self.value = value

    def update(self, v):
        self.value = v

    def __repr__(self):
        return f"Sensor({self.name}, {self.value})"
```

### P2-02 · @dataclass

Frame(msg_id: int, payload: bytes = b"") dataclass와 길이를 돌려주는 size property.

정답:

```python
from dataclasses import dataclass


@dataclass
class Frame:
    msg_id: int
    payload: bytes = b""

    @property
    def size(self):
        return len(self.payload)
```

### P2-03 · 커스텀 예외 계층

DeviceError(Exception), 그 자식 DeviceTimeout. check(code): 0이면 None, 1이면 DeviceTimeout, 그 외 DeviceError(f"code {code}").

정답:

```python
class DeviceError(Exception):
    pass


class DeviceTimeout(DeviceError):
    pass


def check(code):
    if code == 0:
        return None
    if code == 1:
        raise DeviceTimeout("timeout")
    raise DeviceError(f"code {code}")
```

### P2-04 · try / except

정수로 바꿀 수 없으면 default를 돌려주는 parse_int(s, default=None).

정답:

```python
def parse_int(s, default=None):
    try:
        return int(s, 0)
    except (ValueError, TypeError):
        return default
```

### P2-05 · context manager 클래스

with Timer() as t: 블록이 끝나면 t.elapsed(초)가 채워지는 Timer. time.monotonic 사용.

정답:

```python
import time


class Timer:
    def __enter__(self):
        self.start = time.monotonic()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.elapsed = time.monotonic() - self.start
        return False
```

### P2-06 · @contextmanager

opened(log): 들어갈 때 log에 "open", 나올 때(예외가 나도) "close"를 append.

정답:

```python
from contextlib import contextmanager


@contextmanager
def opened(log):
    log.append("open")
    try:
        yield log
    finally:
        log.append("close")
```

### P2-07 · generator

data를 n바이트씩 잘라 yield 하는 chunks(data, n).

정답:

```python
def chunks(data, n):
    for i in range(0, len(data), n):
        yield data[i:i + n]
```

### P2-08 · defaultdict

첫 글자별로 단어를 묶는 group_by_first(words) → {첫글자: [단어...]}.

정답:

```python
from collections import defaultdict


def group_by_first(words):
    groups = defaultdict(list)
    for w in words:
        groups[w[0]].append(w)
    return dict(groups)
```

### P2-09 · dunder: __len__ · __getitem__

Registers(n): n개 0으로 시작, len()과 r[i] 읽기/쓰기(__setitem__) 지원.

정답:

```python
class Registers:
    def __init__(self, n):
        self._regs = [0] * n

    def __len__(self):
        return len(self._regs)

    def __getitem__(self, i):
        return self._regs[i]

    def __setitem__(self, i, value):
        self._regs[i] = value & 0xFFFFFFFF
```

### P2-10 · @property + 검증

Motor.throttle property: 0~100만 허용, 아니면 ValueError.

정답:

```python
class Motor:
    def __init__(self):
        self._throttle = 0

    @property
    def throttle(self):
        return self._throttle

    @throttle.setter
    def throttle(self, value):
        if not 0 <= value <= 100:
            raise ValueError(f"throttle {value} out of range")
        self._throttle = value
```

## C 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| C2-01 | malloc / free | 0..n-1을 담은 int32_t 배열을 malloc 해서 돌려주는 make_seq(n). 실패 시 NULL. |
| C2-02 | 함수 안의 static | 호출할 때마다 1, 2, 3...을 돌려주는 next_id(void). |
| C2-03 | const char* 순회 · ctype | 문자열 안의 숫자 문자 개수를 세는 count_digits(s). isdigit 사용. |
| C2-04 | 함수 포인터 | binop_t typedef(int(int,int)), add / mul, 그리고 apply(f, a, b). |
| C2-05 | 연결 리스트 | node_t {int v; next}와 push_front(head, n) → 새 head, list_len(head). |
| C2-06 | float ↔ 비트 (memcpy) | float의 비트 패턴을 uint32_t로 돌려주는 float_bits(f). 포인터 캐스팅 대신 memcpy. |
| C2-07 | 안전한 매크로 | ARRAY_SIZE(a), MIN(a,b), CLAMP(x,lo,hi) — 괄호 빠짐없이. |
| C2-08 | 고정 버퍼에 안전 복사 | cfg_t {char name[16];}에 이름을 자르면서 복사하고 항상 NUL로 끝내는 cfg_set_name(c, s). 잘렸으면 false. |
| C2-09 | 에러 코드 + out 파라미터 | err_t {E_OK=0, E_RANGE=-1}. 0~100이면 *out에 넣고 E_OK, 아니면 *out은 그대로 두고 E_RANGE. parse_percent(v, out). |
| C2-10 | 2차원 배열 | 3x3 int32_t 행렬의 대각합 trace3(m). |

### C2-01 · malloc / free

0..n-1을 담은 int32_t 배열을 malloc 해서 돌려주는 make_seq(n). 실패 시 NULL.

정답:

```c
int32_t *make_seq(size_t n)
{
    int32_t *a = malloc(n * sizeof *a);
    if (a == NULL)
        return NULL;
    for (size_t i = 0; i < n; i++)
        a[i] = (int32_t)i;
    return a;
}
```

### C2-02 · 함수 안의 static

호출할 때마다 1, 2, 3...을 돌려주는 next_id(void).

정답:

```c
uint32_t next_id(void)
{
    static uint32_t id;
    return ++id;
}
```

### C2-03 · const char* 순회 · ctype

문자열 안의 숫자 문자 개수를 세는 count_digits(s). isdigit 사용.

정답:

```c
size_t count_digits(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (isdigit((unsigned char)*s))
            n++;
    return n;
}
```

### C2-04 · 함수 포인터

binop_t typedef(int(int,int)), add / mul, 그리고 apply(f, a, b).

정답:

```c
typedef int (*binop_t)(int, int);

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

int apply(binop_t f, int a, int b)
{
    return f(a, b);
}
```

### C2-05 · 연결 리스트

node_t {int v; next}와 push_front(head, n) → 새 head, list_len(head).

정답:

```c
typedef struct node {
    int v;
    struct node *next;
} node_t;

node_t *push_front(node_t *head, node_t *n)
{
    n->next = head;
    return n;
}

size_t list_len(const node_t *h)
{
    size_t n = 0;
    for (; h; h = h->next)
        n++;
    return n;
}
```

### C2-06 · float ↔ 비트 (memcpy)

float의 비트 패턴을 uint32_t로 돌려주는 float_bits(f). 포인터 캐스팅 대신 memcpy.

정답:

```c
uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}
```

### C2-07 · 안전한 매크로

ARRAY_SIZE(a), MIN(a,b), CLAMP(x,lo,hi) — 괄호 빠짐없이.

정답:

```c
#define ARRAY_SIZE(a)     (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)         ((a) < (b) ? (a) : (b))
#define MAX(a, b)         ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi)  (MIN(MAX((x), (lo)), (hi)))
```

### C2-08 · 고정 버퍼에 안전 복사

cfg_t {char name[16];}에 이름을 자르면서 복사하고 항상 NUL로 끝내는 cfg_set_name(c, s). 잘렸으면 false.

정답:

```c
typedef struct {
    char name[16];
} cfg_t;

bool cfg_set_name(cfg_t *c, const char *s)
{
    size_t n = strlen(s);
    size_t cap = sizeof c->name - 1;
    size_t k = n < cap ? n : cap;
    memcpy(c->name, s, k);
    c->name[k] = '\0';
    return n <= cap;
}
```

### C2-09 · 에러 코드 + out 파라미터

err_t {E_OK=0, E_RANGE=-1}. 0~100이면 *out에 넣고 E_OK, 아니면 *out은 그대로 두고 E_RANGE. parse_percent(v, out).

정답:

```c
typedef enum { E_OK = 0, E_RANGE = -1 } err_t;

err_t parse_percent(int v, uint8_t *out)
{
    if (v < 0 || v > 100)
        return E_RANGE;
    *out = (uint8_t)v;
    return E_OK;
}
```

### C2-10 · 2차원 배열

3x3 int32_t 행렬의 대각합 trace3(m).

정답:

```c
int32_t trace3(const int32_t m[3][3])
{
    int32_t t = 0;
    for (int i = 0; i < 3; i++)
        t += m[i][i];
    return t;
}
```

## 체크

- [ ] Python 10개를 정답 안 보고 → `python3 drills/drill.py check 2` 에서 ✓ 10
- [ ] C 10개를 정답 안 보고 → ✓ 10
- [ ] 틀린 것 ID를 적어 두고 다음 날 아침 첫 5분에 다시
