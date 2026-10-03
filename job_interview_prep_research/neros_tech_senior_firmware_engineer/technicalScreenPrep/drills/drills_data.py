"""매일 타이핑 드릴 데이터 — Python 10 + C 10 × 5일.

각 항목
  id      "P3-04" (P=Python, C=C, 가운데 숫자=Day)
  title   한 줄 주제
  prompt  무엇을 칠지 (한국어)
  given   주어진 코드 (치지 않음, 화면에 회색으로)
  answer  외워서 칠 코드
  check   채점 코드 (Python: answer 뒤에 실행되는 assert / C: main() 안 본문)
  support C 채점용 보조 코드 (fake 등, answer 뒤 main 앞에 들어감)
  pytest  True 면 answer 자체가 pytest 테스트 파일 (given + answer 를 pytest 로 실행)
"""

DAYS = {
    1: ("10-03 (토)", "기본 문법 — 컨테이너 · 루프 · 문자열 / C 타입 · 포인터 · 비트"),
    2: ("10-04 (일)", "클래스 · 예외 · with · generator / C 메모리 · 함수 포인터 · 구조체"),
    3: ("10-05 (월)", "bytes · struct · 비트 필드 · CRC / C 엔디언 · 레지스터 · 파싱"),
    4: ("10-06 (화)", "ring buffer · 스레드 · 상태 머신 · retry / C ring · atomics · 타이머"),
    5: ("10-07 (수)", "pytest · fixture · mock / C 단위 테스트 · fake · DI"),
}

ITEMS = [
# =========================================================== DAY 1 · Python
dict(id="P1-01", title="슬라이스와 뒤집기",
     prompt="리스트 xs의 마지막 n개를 거꾸로 돌려주는 last_n_reversed(xs, n).",
     answer=r'''def last_n_reversed(xs, n):
    return xs[-n:][::-1]''',
     check=r'''assert last_n_reversed([1, 2, 3, 4, 5], 3) == [5, 4, 3]
assert last_n_reversed([1], 1) == [1]'''),
dict(id="P1-02", title="dict로 개수 세기",
     prompt="공백으로 나눈 단어별 개수를 dict로 돌려주는 count_words(text). dict.get 사용.",
     answer=r'''def count_words(text):
    counts = {}
    for w in text.split():
        counts[w] = counts.get(w, 0) + 1
    return counts''',
     check=r'''assert count_words("a b a c a") == {"a": 3, "b": 1, "c": 1}
assert count_words("") == {}'''),
dict(id="P1-03", title="enumerate",
     prompt="target이 처음 나오는 인덱스, 없으면 -1을 돌려주는 find_index(xs, target). range(len()) 쓰지 말 것.",
     answer=r'''def find_index(xs, target):
    for i, x in enumerate(xs):
        if x == target:
            return i
    return -1''',
     check=r'''assert find_index([5, 7, 9], 9) == 2
assert find_index([5, 7], 1) == -1'''),
dict(id="P1-04", title="zip과 dict",
     prompt="두 리스트를 key → value dict로 묶는 pair_up(keys, values).",
     answer=r'''def pair_up(keys, values):
    return dict(zip(keys, values))''',
     check=r'''assert pair_up(["vbat", "rssi"], [16.4, -70]) == {"vbat": 16.4, "rssi": -70}'''),
dict(id="P1-05", title="list comprehension",
     prompt="짝수만 골라 제곱한 리스트를 돌려주는 even_squares(xs). 한 줄.",
     answer=r'''def even_squares(xs):
    return [x * x for x in xs if x % 2 == 0]''',
     check=r'''assert even_squares(range(7)) == [0, 4, 16, 36]'''),
dict(id="P1-06", title="f-string hex 포맷",
     prompt='fmt_reg(0x40, 0x1F) → "0x0040: 0x1F" (주소 4자리, 값 2자리, 대문자).',
     answer=r'''def fmt_reg(addr, val):
    return f"0x{addr:04X}: 0x{val:02X}"''',
     check=r'''assert fmt_reg(0x40, 0x1F) == "0x0040: 0x1F"
assert fmt_reg(0xABCD, 3) == "0xABCD: 0x03"'''),
dict(id="P1-07", title="문자열 split · strip",
     prompt='"key = value" 한 줄을 (key, value) 튜플로. 값 안의 = 는 보존. parse_kv(line).',
     answer=r'''def parse_kv(line):
    key, _, value = line.partition("=")
    return key.strip(), value.strip()''',
     check=r'''assert parse_kv(" rate = 50 ") == ("rate", "50")
assert parse_kv("expr=a=b") == ("expr", "a=b")'''),
dict(id="P1-08", title="sorted + key",
     prompt="길이 오름차순, 같으면 알파벳순으로 정렬하는 sort_words(words).",
     answer=r'''def sort_words(words):
    return sorted(words, key=lambda w: (len(w), w))''',
     check=r'''assert sort_words(["ccc", "b", "aa", "a"]) == ["a", "b", "aa", "ccc"]'''),
dict(id="P1-09", title="while + divmod",
     prompt="양의 정수 n의 자릿수 합 digit_sum(n). divmod 사용.",
     answer=r'''def digit_sum(n):
    total = 0
    while n:
        n, d = divmod(n, 10)
        total += d
    return total''',
     check=r'''assert digit_sum(1234) == 10
assert digit_sum(0) == 0'''),
dict(id="P1-10", title="set 연산",
     prompt="두 리스트에 공통으로 있는 값을 중복 없이 정렬해서 common(a, b).",
     answer=r'''def common(a, b):
    return sorted(set(a) & set(b))''',
     check=r'''assert common([3, 1, 2, 2], [2, 3, 5]) == [2, 3]'''),

# =========================================================== DAY 1 · C
dict(id="C1-01", title="stdint 배열 합",
     prompt="uint8_t 버퍼의 합을 uint32_t로 돌려주는 sum_u8(buf, n).",
     answer=r'''uint32_t sum_u8(const uint8_t *buf, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += buf[i];
    return sum;
}''',
     check=r'''uint8_t b[] = {255, 255, 2};
assert(sum_u8(b, 3) == 512);
assert(sum_u8(b, 0) == 0);'''),
dict(id="C1-02", title="포인터로 swap",
     prompt="두 int를 바꾸는 swap_int(a, b).",
     answer=r'''void swap_int(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}''',
     check=r'''int x = 1, y = 2;
swap_int(&x, &y);
assert(x == 2 && y == 1);'''),
dict(id="C1-03", title="typedef struct",
     prompt="int16_t x, y를 가진 point_t와, 두 점 거리의 제곱을 int32_t로 돌려주는 dist2(a, b).",
     answer=r'''typedef struct {
    int16_t x, y;
} point_t;

int32_t dist2(point_t a, point_t b)
{
    int32_t dx = (int32_t)a.x - b.x;
    int32_t dy = (int32_t)a.y - b.y;
    return dx * dx + dy * dy;
}''',
     check=r'''point_t p = {0, 0}, q = {3, -4};
assert(dist2(p, q) == 25);'''),
dict(id="C1-04", title="비트 set · clear · test",
     prompt="BIT(n) 매크로와 set_bit / clear_bit / test_bit (uint32_t *reg 대상).",
     answer=r'''#define BIT(n) (1u << (n))

void set_bit(uint32_t *reg, unsigned n)   { *reg |= BIT(n); }
void clear_bit(uint32_t *reg, unsigned n) { *reg &= ~BIT(n); }
bool test_bit(uint32_t reg, unsigned n)   { return (reg & BIT(n)) != 0; }''',
     check=r'''uint32_t r = 0;
set_bit(&r, 3); set_bit(&r, 31);
assert(r == 0x80000008u && test_bit(r, 3));
clear_bit(&r, 3);
assert(r == 0x80000000u && !test_bit(r, 3));'''),
dict(id="C1-05", title="strlen 직접",
     prompt="표준 함수 없이 문자열 길이 my_strlen(s).",
     answer=r'''size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}''',
     check=r'''assert(my_strlen("") == 0);
assert(my_strlen("neros") == 5);'''),
dict(id="C1-06", title="배열 제자리 뒤집기",
     prompt="uint8_t 배열을 제자리에서 뒤집는 reverse_u8(a, n). n=0도 안전하게.",
     answer=r'''void reverse_u8(uint8_t *a, size_t n)
{
    if (n < 2)
        return;
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        uint8_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}''',
     check=r'''uint8_t a[] = {1, 2, 3, 4};
reverse_u8(a, 4);
assert(a[0] == 4 && a[3] == 1);
reverse_u8(a, 0);'''),
dict(id="C1-07", title="enum + switch",
     prompt="ST_IDLE / ST_ARMED / ST_FLYING enum과 이름 문자열을 돌려주는 state_name(s). 모르는 값은 \"?\".",
     answer=r'''typedef enum { ST_IDLE, ST_ARMED, ST_FLYING } state_t;

const char *state_name(state_t s)
{
    switch (s) {
    case ST_IDLE:   return "IDLE";
    case ST_ARMED:  return "ARMED";
    case ST_FLYING: return "FLYING";
    default:        return "?";
    }
}''',
     check=r'''assert(strcmp(state_name(ST_ARMED), "ARMED") == 0);
assert(strcmp(state_name((state_t)9), "?") == 0);'''),
dict(id="C1-08", title="memcpy · memcmp · memset",
     prompt="src를 dst에 복사하고 같은지 확인해서 true, 그다음 src를 0으로 지우는 copy_wipe(dst, src, n).",
     answer=r'''bool copy_wipe(uint8_t *dst, uint8_t *src, size_t n)
{
    memcpy(dst, src, n);
    bool same = memcmp(dst, src, n) == 0;
    memset(src, 0, n);
    return same;
}''',
     check=r'''uint8_t s[3] = {1, 2, 3}, d[3];
assert(copy_wipe(d, s, 3));
assert(d[2] == 3 && s[0] == 0 && s[2] == 0);'''),
dict(id="C1-09", title="set bit 개수",
     prompt="v &= v - 1 트릭으로 1인 비트 수를 세는 popcount32(v).",
     answer=r'''int popcount32(uint32_t v)
{
    int n = 0;
    while (v) {
        v &= v - 1;
        n++;
    }
    return n;
}''',
     check=r'''assert(popcount32(0) == 0);
assert(popcount32(0xF0F0u) == 8);
assert(popcount32(0xFFFFFFFFu) == 32);'''),
dict(id="C1-10", title="포인터 순회로 최댓값",
     prompt="int32_t 배열(n>0)의 최댓값 max_i32(a, n). 포인터로 끝까지 순회.",
     answer=r'''int32_t max_i32(const int32_t *a, size_t n)
{
    const int32_t *end = a + n;
    int32_t m = *a++;
    for (; a < end; a++)
        if (*a > m)
            m = *a;
    return m;
}''',
     check=r'''int32_t a[] = {-5, 7, 3};
assert(max_i32(a, 3) == 7);
assert(max_i32(a, 1) == -5);'''),

# =========================================================== DAY 2 · Python
dict(id="P2-01", title="class · __init__ · __repr__",
     prompt='Sensor(name, value=0.0) 클래스. update(v)로 값 갱신, repr은 "Sensor(imu, 1.5)".',
     answer=r'''class Sensor:
    def __init__(self, name, value=0.0):
        self.name = name
        self.value = value

    def update(self, v):
        self.value = v

    def __repr__(self):
        return f"Sensor({self.name}, {self.value})"''',
     check=r'''s = Sensor("imu")
s.update(1.5)
assert repr(s) == "Sensor(imu, 1.5)"'''),
dict(id="P2-02", title="@dataclass",
     prompt="Frame(msg_id: int, payload: bytes = b\"\") dataclass와 길이를 돌려주는 size property.",
     answer=r'''from dataclasses import dataclass


@dataclass
class Frame:
    msg_id: int
    payload: bytes = b""

    @property
    def size(self):
        return len(self.payload)''',
     check=r'''f = Frame(3, b"\x01\x02")
assert f.size == 2 and Frame(3, b"\x01\x02") == f and Frame(1).payload == b""'''),
dict(id="P2-03", title="커스텀 예외 계층",
     prompt="DeviceError(Exception), 그 자식 DeviceTimeout. check(code): 0이면 None, 1이면 DeviceTimeout, 그 외 DeviceError(f\"code {code}\").",
     answer=r'''class DeviceError(Exception):
    pass


class DeviceTimeout(DeviceError):
    pass


def check(code):
    if code == 0:
        return None
    if code == 1:
        raise DeviceTimeout("timeout")
    raise DeviceError(f"code {code}")''',
     check=r'''assert check(0) is None
try:
    check(1); assert False
except DeviceError as e:
    assert isinstance(e, DeviceTimeout)
try:
    check(7); assert False
except DeviceError as e:
    assert str(e) == "code 7"'''),
dict(id="P2-04", title="try / except",
     prompt="정수로 바꿀 수 없으면 default를 돌려주는 parse_int(s, default=None).",
     answer=r'''def parse_int(s, default=None):
    try:
        return int(s, 0)
    except (ValueError, TypeError):
        return default''',
     check=r'''assert parse_int("0x1F") == 31 and parse_int("12") == 12
assert parse_int("abc", -1) == -1 and parse_int(None) is None'''),
dict(id="P2-05", title="context manager 클래스",
     prompt="with Timer() as t: 블록이 끝나면 t.elapsed(초)가 채워지는 Timer. time.monotonic 사용.",
     answer=r'''import time


class Timer:
    def __enter__(self):
        self.start = time.monotonic()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.elapsed = time.monotonic() - self.start
        return False''',
     check=r'''with Timer() as t:
    time.sleep(0.01)
assert t.elapsed >= 0.009'''),
dict(id="P2-06", title="@contextmanager",
     prompt='opened(log): 들어갈 때 log에 "open", 나올 때(예외가 나도) "close"를 append.',
     answer=r'''from contextlib import contextmanager


@contextmanager
def opened(log):
    log.append("open")
    try:
        yield log
    finally:
        log.append("close")''',
     check=r'''log = []
try:
    with opened(log):
        raise RuntimeError
except RuntimeError:
    pass
assert log == ["open", "close"]'''),
dict(id="P2-07", title="generator",
     prompt="data를 n바이트씩 잘라 yield 하는 chunks(data, n).",
     answer=r'''def chunks(data, n):
    for i in range(0, len(data), n):
        yield data[i:i + n]''',
     check=r'''assert list(chunks(b"abcde", 2)) == [b"ab", b"cd", b"e"]
assert list(chunks(b"", 3)) == []'''),
dict(id="P2-08", title="defaultdict",
     prompt="첫 글자별로 단어를 묶는 group_by_first(words) → {첫글자: [단어...]}.",
     answer=r'''from collections import defaultdict


def group_by_first(words):
    groups = defaultdict(list)
    for w in words:
        groups[w[0]].append(w)
    return dict(groups)''',
     check=r'''assert group_by_first(["arm", "abort", "boot"]) == {"a": ["arm", "abort"], "b": ["boot"]}'''),
dict(id="P2-09", title="dunder: __len__ · __getitem__",
     prompt="Registers(n): n개 0으로 시작, len()과 r[i] 읽기/쓰기(__setitem__) 지원.",
     answer=r'''class Registers:
    def __init__(self, n):
        self._regs = [0] * n

    def __len__(self):
        return len(self._regs)

    def __getitem__(self, i):
        return self._regs[i]

    def __setitem__(self, i, value):
        self._regs[i] = value & 0xFFFFFFFF''',
     check=r'''r = Registers(4)
r[1] = 0x1_0000_0005
assert len(r) == 4 and r[1] == 5 and list(r) == [0, 5, 0, 0]'''),
dict(id="P2-10", title="@property + 검증",
     prompt="Motor.throttle property: 0~100만 허용, 아니면 ValueError.",
     answer=r'''class Motor:
    def __init__(self):
        self._throttle = 0

    @property
    def throttle(self):
        return self._throttle

    @throttle.setter
    def throttle(self, value):
        if not 0 <= value <= 100:
            raise ValueError(f"throttle {value} out of range")
        self._throttle = value''',
     check=r'''m = Motor(); m.throttle = 40
assert m.throttle == 40
try:
    m.throttle = 101; assert False
except ValueError:
    pass'''),

# =========================================================== DAY 2 · C
dict(id="C2-01", title="malloc / free",
     prompt="0..n-1을 담은 int32_t 배열을 malloc 해서 돌려주는 make_seq(n). 실패 시 NULL.",
     answer=r'''int32_t *make_seq(size_t n)
{
    int32_t *a = malloc(n * sizeof *a);
    if (a == NULL)
        return NULL;
    for (size_t i = 0; i < n; i++)
        a[i] = (int32_t)i;
    return a;
}''',
     check=r'''int32_t *a = make_seq(5);
assert(a && a[0] == 0 && a[4] == 4);
free(a);'''),
dict(id="C2-02", title="함수 안의 static",
     prompt="호출할 때마다 1, 2, 3...을 돌려주는 next_id(void).",
     answer=r'''uint32_t next_id(void)
{
    static uint32_t id;
    return ++id;
}''',
     check=r'''assert(next_id() == 1);
assert(next_id() == 2);'''),
dict(id="C2-03", title="const char* 순회 · ctype",
     prompt="문자열 안의 숫자 문자 개수를 세는 count_digits(s). isdigit 사용.",
     answer=r'''size_t count_digits(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (isdigit((unsigned char)*s))
            n++;
    return n;
}''',
     check=r'''assert(count_digits("rev C2, fw 1.4.2") == 4);
assert(count_digits("") == 0);'''),
dict(id="C2-04", title="함수 포인터",
     prompt="binop_t typedef(int(int,int)), add / mul, 그리고 apply(f, a, b).",
     answer=r'''typedef int (*binop_t)(int, int);

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

int apply(binop_t f, int a, int b)
{
    return f(a, b);
}''',
     check=r'''binop_t ops[] = {add, mul};
assert(apply(ops[0], 3, 4) == 7);
assert(apply(ops[1], 3, 4) == 12);'''),
dict(id="C2-05", title="연결 리스트",
     prompt="node_t {int v; next}와 push_front(head, n) → 새 head, list_len(head).",
     answer=r'''typedef struct node {
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
}''',
     check=r'''node_t a = {1, NULL}, b = {2, NULL};
node_t *h = push_front(push_front(NULL, &a), &b);
assert(h == &b && h->next == &a && list_len(h) == 2 && list_len(NULL) == 0);'''),
dict(id="C2-06", title="float ↔ 비트 (memcpy)",
     prompt="float의 비트 패턴을 uint32_t로 돌려주는 float_bits(f). 포인터 캐스팅 대신 memcpy.",
     answer=r'''uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}''',
     check=r'''assert(float_bits(1.0f) == 0x3F800000u);
assert(float_bits(-2.0f) == 0xC0000000u);'''),
dict(id="C2-07", title="안전한 매크로",
     prompt="ARRAY_SIZE(a), MIN(a,b), CLAMP(x,lo,hi) — 괄호 빠짐없이.",
     answer=r'''#define ARRAY_SIZE(a)     (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)         ((a) < (b) ? (a) : (b))
#define MAX(a, b)         ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi)  (MIN(MAX((x), (lo)), (hi)))''',
     check=r'''int a[7];
(void)a;
assert(ARRAY_SIZE(a) == 7);
assert(MIN(2 + 1, 4) == 3);
assert(CLAMP(150, 0, 100) == 100 && CLAMP(-3, 0, 100) == 0);'''),
dict(id="C2-08", title="고정 버퍼에 안전 복사",
     prompt="cfg_t {char name[16];}에 이름을 자르면서 복사하고 항상 NUL로 끝내는 cfg_set_name(c, s). 잘렸으면 false.",
     answer=r'''typedef struct {
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
}''',
     check=r'''cfg_t c;
assert(cfg_set_name(&c, "archer") && strcmp(c.name, "archer") == 0);
assert(!cfg_set_name(&c, "0123456789abcdefXYZ") && strlen(c.name) == 15);'''),
dict(id="C2-09", title="에러 코드 + out 파라미터",
     prompt="err_t {E_OK=0, E_RANGE=-1}. 0~100이면 *out에 넣고 E_OK, 아니면 *out은 그대로 두고 E_RANGE. parse_percent(v, out).",
     answer=r'''typedef enum { E_OK = 0, E_RANGE = -1 } err_t;

err_t parse_percent(int v, uint8_t *out)
{
    if (v < 0 || v > 100)
        return E_RANGE;
    *out = (uint8_t)v;
    return E_OK;
}''',
     check=r'''uint8_t p = 7;
assert(parse_percent(101, &p) == E_RANGE && p == 7);
assert(parse_percent(55, &p) == E_OK && p == 55);'''),
dict(id="C2-10", title="2차원 배열",
     prompt="3x3 int32_t 행렬의 대각합 trace3(m).",
     answer=r'''int32_t trace3(const int32_t m[3][3])
{
    int32_t t = 0;
    for (int i = 0; i < 3; i++)
        t += m[i][i];
    return t;
}''',
     check=r'''const int32_t m[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
assert(trace3(m) == 15);'''),

# =========================================================== DAY 3 · Python
dict(id="P3-01", title="bytes → hex 문자열",
     prompt='to_hex(b"\\x01\\xab") → "01 AB" (공백 구분, 대문자).',
     answer=r'''def to_hex(b):
    return " ".join(f"{x:02X}" for x in b)''',
     check=r'''assert to_hex(b"\x01\xab") == "01 AB" and to_hex(b"") == ""'''),
dict(id="P3-02", title="hex 문자열 → bytes",
     prompt='from_hex("AA 05 01") → b"\\xaa\\x05\\x01". bytes.fromhex 사용.',
     answer=r'''def from_hex(s):
    return bytes.fromhex(s)''',
     check=r'''assert from_hex("AA 05 01") == b"\xaa\x05\x01"'''),
dict(id="P3-03", title="int.from_bytes / to_bytes",
     prompt="b[off:off+2]를 little-endian u16으로 읽는 u16le(b, off), v를 big-endian 4바이트로 pack_u32be(v).",
     answer=r'''def u16le(b, off):
    return int.from_bytes(b[off:off + 2], "little")


def pack_u32be(v):
    return v.to_bytes(4, "big")''',
     check=r'''assert u16le(b"\x00\x34\x12", 1) == 0x1234
assert pack_u32be(0x01020304) == b"\x01\x02\x03\x04"'''),
dict(id="P3-04", title="struct.unpack",
     prompt='헤더 4바이트 "<BBH" (sync, type, length)를 튜플로 parse_header(b). 4바이트 미만이면 ValueError.',
     answer=r'''import struct

HDR = struct.Struct("<BBH")


def parse_header(b):
    if len(b) < HDR.size:
        raise ValueError("short header")
    return HDR.unpack_from(b)''',
     check=r'''assert parse_header(b"\xaa\x02\x10\x00rest") == (0xAA, 2, 16)
try:
    parse_header(b"\xaa"); assert False
except ValueError:
    pass'''),
dict(id="P3-05", title="struct.pack",
     prompt='명령 프레임: cmd_id(u8), value(i16), seq(u32), little-endian. build_cmd(cmd_id, value, seq).',
     answer=r'''import struct


def build_cmd(cmd_id, value, seq):
    return struct.pack("<BhI", cmd_id, value, seq)''',
     check=r'''assert build_cmd(1, -2, 3) == b"\x01\xfe\xff\x03\x00\x00\x00"'''),
dict(id="P3-06", title="비트 필드 읽기",
     prompt="reg에서 shift 위치부터 width 비트를 꺼내는 get_field(reg, shift, width).",
     answer=r'''def get_field(reg, shift, width):
    return (reg >> shift) & ((1 << width) - 1)''',
     check=r'''assert get_field(0b1011_0000, 4, 4) == 0b1011
assert get_field(0xDEADBEEF, 0, 32) == 0xDEADBEEF'''),
dict(id="P3-07", title="비트 필드 쓰기",
     prompt="reg의 [shift, shift+width) 자리를 val로 바꾼 값을 돌려주는 set_field(reg, shift, width, val). val이 넘치면 ValueError.",
     answer=r'''def set_field(reg, shift, width, val):
    mask = (1 << width) - 1
    if val & ~mask:
        raise ValueError("value does not fit")
    return (reg & ~(mask << shift)) | (val << shift)''',
     check=r'''assert set_field(0xFF, 4, 4, 0x2) == 0x2F
try:
    set_field(0, 0, 2, 4); assert False
except ValueError:
    pass'''),
dict(id="P3-08", title="XOR 체크섬",
     prompt="모든 바이트를 XOR 한 값 xor_sum(data).",
     answer=r'''def xor_sum(data):
    x = 0
    for b in data:
        x ^= b
    return x''',
     check=r'''assert xor_sum(b"\x01\x02\x03") == 0 and xor_sum(b"") == 0 and xor_sum(b"\xf0\x0f") == 0xFF'''),
dict(id="P3-09", title="CRC-8 (poly 0x07)",
     prompt="MSB-first CRC-8/SMBUS crc8(data). check 값: crc8(b\"123456789\") == 0xF4.",
     answer=r'''def crc8(data, poly=0x07):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc''',
     check=r'''assert crc8(b"123456789") == 0xF4 and crc8(b"") == 0'''),
dict(id="P3-10", title="bytearray 슬라이스 대입",
     prompt="buf[off:]를 data로 덮어쓰고(길이 유지) buf를 돌려주는 patch(buf, off, data). 넘치면 ValueError.",
     answer=r'''def patch(buf, off, data):
    if off + len(data) > len(buf):
        raise ValueError("out of bounds")
    buf[off:off + len(data)] = data
    return buf''',
     check=r'''b = bytearray(4)
assert patch(b, 1, b"\x01\x02") == bytearray(b"\x00\x01\x02\x00")
try:
    patch(b, 3, b"xx"); assert False
except ValueError:
    pass'''),

# =========================================================== DAY 3 · C
dict(id="C3-01", title="little-endian 읽기",
     prompt="p[0]이 하위 바이트인 u16을 읽는 rd_le16(p).",
     answer=r'''uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}''',
     check=r'''uint8_t b[] = {0x34, 0x12};
assert(rd_le16(b) == 0x1234);'''),
dict(id="C3-02", title="big-endian 쓰기",
     prompt="v를 big-endian 4바이트로 쓰는 wr_be32(p, v).",
     answer=r'''void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}''',
     check=r'''uint8_t b[4];
wr_be32(b, 0x01020304u);
assert(b[0] == 1 && b[3] == 4);'''),
dict(id="C3-03", title="비트 필드 읽기 (width 32 안전)",
     prompt="get_field(reg, shift, width). width==32일 때 1u<<32 UB를 피할 것.",
     answer=r'''uint32_t get_field(uint32_t reg, unsigned shift, unsigned width)
{
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (reg >> shift) & mask;
}''',
     check=r'''assert(get_field(0xB0u, 4, 4) == 0xBu);
assert(get_field(0xDEADBEEFu, 0, 32) == 0xDEADBEEFu);'''),
dict(id="C3-04", title="비트 필드 쓰기 (read-modify-write)",
     prompt="set_field(reg, shift, width, val): 그 자리만 바꾼 값을 돌려준다 (width < 32 가정).",
     answer=r'''uint32_t set_field(uint32_t reg, unsigned shift, unsigned width, uint32_t val)
{
    uint32_t mask = ((1u << width) - 1u) << shift;
    return (reg & ~mask) | ((val << shift) & mask);
}''',
     check=r'''assert(set_field(0xFFu, 4, 4, 0x2u) == 0x2Fu);
assert(set_field(0u, 0, 2, 0x7u) == 0x3u);'''),
dict(id="C3-05", title="CRC-8 (poly 0x07)",
     prompt="crc8(data, n) — MSB-first, init 0. check: \"123456789\" → 0xF4.",
     answer=r'''uint8_t crc8(const uint8_t *data, size_t n)
{
    uint8_t crc = 0;
    while (n--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}''',
     check=r'''assert(crc8((const uint8_t *)"123456789", 9) == 0xF4);
assert(crc8(NULL, 0) == 0);'''),
dict(id="C3-06", title="합 체크섬 (8비트 2의 보수)",
     prompt="모든 바이트 합 + cs == 0 (mod 256)이 되게 하는 checksum8(data, n).",
     answer=r'''uint8_t checksum8(const uint8_t *data, size_t n)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += data[i];
    return (uint8_t)(0u - sum);
}''',
     check=r'''uint8_t b[] = {0x10, 0x20, 0xF0};
uint8_t cs = checksum8(b, 3);
assert((uint8_t)(0x10 + 0x20 + 0xF0 + cs) == 0);'''),
dict(id="C3-07", title="헤더 파싱 (packed struct 없이)",
     prompt="hdr_t {sync, type, len(u16 LE)}. 4바이트 미만이거나 sync != 0xAA면 false. parse_hdr(b, n, out).",
     answer=r'''typedef struct {
    uint8_t  sync;
    uint8_t  type;
    uint16_t len;
} hdr_t;

bool parse_hdr(const uint8_t *b, size_t n, hdr_t *out)
{
    if (n < 4 || b[0] != 0xAA)
        return false;
    out->sync = b[0];
    out->type = b[1];
    out->len  = (uint16_t)(b[2] | (b[3] << 8));
    return true;
}''',
     check=r'''uint8_t ok[] = {0xAA, 2, 0x10, 0x00}, bad[] = {0x55, 2, 0, 0};
hdr_t h;
assert(parse_hdr(ok, 4, &h) && h.type == 2 && h.len == 16);
assert(!parse_hdr(bad, 4, &h) && !parse_hdr(ok, 3, &h));'''),
dict(id="C3-08", title="volatile 레지스터 RMW",
     prompt="REG32(addr) 매크로, 그리고 volatile 레지스터에 mask 비트를 set/clear 하는 reg_set / reg_clear.",
     answer=r'''#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

void reg_set(volatile uint32_t *reg, uint32_t mask)   { *reg |= mask; }
void reg_clear(volatile uint32_t *reg, uint32_t mask) { *reg &= ~mask; }''',
     check=r'''static uint32_t fake_reg = 0x10;
reg_set(&fake_reg, 0x3);
reg_clear(&fake_reg, 0x10);
assert(fake_reg == 0x3);
REG32(&fake_reg) = 0xFF;
assert(fake_reg == 0xFF);'''),
dict(id="C3-09", title="12비트 부호 확장",
     prompt="ADC 12비트 2의 보수 raw를 int16_t로 sext12(raw).",
     answer=r'''int16_t sext12(uint16_t raw)
{
    raw &= 0x0FFF;
    return (raw & 0x0800) ? (int16_t)(raw - 0x1000) : (int16_t)raw;
}''',
     check=r'''assert(sext12(0x7FF) == 2047);
assert(sext12(0x800) == -2048);
assert(sext12(0xFFF) == -1);'''),
dict(id="C3-10", title="snprintf hex dump",
     prompt="바이트를 \"AA 01 FF\" 형태로 out에 쓰는 hex_dump(b, n, out, cap). 반환값은 쓴 길이.",
     answer=r'''size_t hex_dump(const uint8_t *b, size_t n, char *out, size_t cap)
{
    size_t len = 0;
    if (cap)
        out[0] = '\0';
    for (size_t i = 0; i < n && len + 3 < cap; i++)
        len += (size_t)snprintf(out + len, cap - len, i ? " %02X" : "%02X", b[i]);
    return len;
}''',
     check=r'''uint8_t b[] = {0xAA, 0x01, 0xFF};
char s[32];
assert(hex_dump(b, 3, s, sizeof s) == 8 && strcmp(s, "AA 01 FF") == 0);'''),

# =========================================================== DAY 4 · Python
dict(id="P4-01", title="RingBuffer 핵심 (drop-new)",
     prompt="RingBuffer(capacity): push(x) → bool (가득 차면 False), pop() (비면 IndexError), len(). head + count 방식.",
     answer=r'''class RingBuffer:
    def __init__(self, capacity):
        self._buf = [None] * capacity
        self._cap = capacity
        self._head = 0
        self._count = 0

    def __len__(self):
        return self._count

    def push(self, item):
        if self._count == self._cap:
            return False
        self._buf[self._head] = item
        self._head = (self._head + 1) % self._cap
        self._count += 1
        return True

    def pop(self):
        if self._count == 0:
            raise IndexError("empty")
        tail = (self._head - self._count) % self._cap
        item, self._buf[tail] = self._buf[tail], None
        self._count -= 1
        return item''',
     check=r'''r = RingBuffer(2)
assert r.push(1) and r.push(2) and not r.push(3)
assert r.pop() == 1 and r.push(3) and [r.pop(), r.pop()] == [2, 3] and len(r) == 0
try:
    r.pop(); assert False
except IndexError:
    pass'''),
dict(id="P4-02", title="deque(maxlen)",
     prompt="스트림의 마지막 n개만 리스트로 돌려주는 last_n(stream, n). deque 사용.",
     answer=r'''from collections import deque


def last_n(stream, n):
    return list(deque(stream, maxlen=n))''',
     check=r'''assert last_n(range(10), 3) == [7, 8, 9] and last_n([], 3) == []'''),
dict(id="P4-03", title="Lock",
     prompt="여러 스레드에서 안전한 SafeCounter: increment(), value 속성. with self._lock.",
     answer=r'''import threading


class SafeCounter:
    def __init__(self):
        self._lock = threading.Lock()
        self.value = 0

    def increment(self):
        with self._lock:
            self.value += 1''',
     check=r'''c = SafeCounter()
ts = [threading.Thread(target=lambda: [c.increment() for _ in range(10000)]) for _ in range(4)]
for t in ts: t.start()
for t in ts: t.join()
assert c.value == 40000'''),
dict(id="P4-04", title="queue.Queue producer/consumer",
     prompt="producer 스레드가 items를 넣고 끝에 None(sentinel), consumer가 모아서 돌려주는 run_pipeline(items).",
     answer=r'''import queue
import threading


def run_pipeline(items):
    q = queue.Queue(maxsize=4)
    out = []

    def producer():
        for x in items:
            q.put(x)
        q.put(None)

    t = threading.Thread(target=producer)
    t.start()
    while True:
        x = q.get(timeout=2)
        if x is None:
            break
        out.append(x)
    t.join()
    return out''',
     check=r'''assert run_pipeline(list(range(100))) == list(range(100))'''),
dict(id="P4-05", title="Condition + while wait",
     prompt="한 칸짜리 Mailbox: put(x), get(timeout) — 비어 있으면 기다리고, 시간 초과면 TimeoutError.",
     answer=r'''import threading


class Mailbox:
    def __init__(self):
        self._cond = threading.Condition()
        self._item = None
        self._full = False

    def put(self, x):
        with self._cond:
            self._item, self._full = x, True
            self._cond.notify()

    def get(self, timeout):
        with self._cond:
            if not self._cond.wait_for(lambda: self._full, timeout):
                raise TimeoutError("mailbox empty")
            self._full = False
            return self._item''',
     check=r'''m = Mailbox()
threading.Timer(0.02, m.put, args=(42,)).start()
assert m.get(1) == 42
try:
    m.get(0.02); assert False
except TimeoutError:
    pass'''),
dict(id="P4-06", title="dict 상태 머신",
     prompt='TRANSITIONS {("IDLE","arm"):"ARMED", ("ARMED","takeoff"):"FLYING", ("ARMED","disarm"):"IDLE", ("FLYING","land"):"ARMED"}. step(state, event), 없는 전이는 ValueError.',
     answer=r'''TRANSITIONS = {
    ("IDLE", "arm"): "ARMED",
    ("ARMED", "takeoff"): "FLYING",
    ("ARMED", "disarm"): "IDLE",
    ("FLYING", "land"): "ARMED",
}


def step(state, event):
    try:
        return TRANSITIONS[(state, event)]
    except KeyError:
        raise ValueError(f"{event} not allowed in {state}") from None''',
     check=r'''assert step("IDLE", "arm") == "ARMED"
try:
    step("IDLE", "takeoff"); assert False
except ValueError as e:
    assert "IDLE" in str(e)'''),
dict(id="P4-07", title="Enum",
     prompt="State(Enum) IDLE=0, ARMED=1, FLYING=2. 이름 문자열로 찾는 from_name(s) (대소문자 무시).",
     answer=r'''from enum import Enum


class State(Enum):
    IDLE = 0
    ARMED = 1
    FLYING = 2


def from_name(s):
    return State[s.upper()]''',
     check=r'''assert from_name("armed") is State.ARMED and State(2) is State.FLYING and State.IDLE.value == 0'''),
dict(id="P4-08", title="deadline polling (monotonic)",
     prompt="pred()가 True가 될 때까지 interval마다 확인, timeout 넘으면 False. wait_until(pred, timeout, interval=0.01).",
     answer=r'''import time


def wait_until(pred, timeout, interval=0.01):
    deadline = time.monotonic() + timeout
    while True:
        if pred():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)''',
     check=r'''t0 = time.monotonic()
assert wait_until(lambda: time.monotonic() - t0 > 0.03, 1)
assert not wait_until(lambda: False, 0.03)'''),
dict(id="P4-09", title="retry 데코레이터",
     prompt="예외가 나면 최대 times번까지 다시 부르는 데코레이터 retry(times). functools.wraps 사용.",
     answer=r'''import functools


def retry(times):
    def deco(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            last = None
            for _ in range(times):
                try:
                    return fn(*args, **kwargs)
                except Exception as e:
                    last = e
            raise last
        return wrapper
    return deco''',
     check=r'''calls = []

@retry(3)
def flaky():
    calls.append(1)
    if len(calls) < 3:
        raise IOError("nope")
    return "ok"

assert flaky() == "ok" and len(calls) == 3 and flaky.__name__ == "flaky"'''),
dict(id="P4-10", title="generator로 구분자 프레이밍",
     prompt="바이트 chunk 이터러블에서 0x00 구분자로 끝나는 프레임을 yield 하는 frames(chunks). 마지막 미완성은 버린다.",
     answer=r'''def frames(chunks):
    buf = bytearray()
    for chunk in chunks:
        buf += chunk
        while True:
            i = buf.find(0)
            if i < 0:
                break
            yield bytes(buf[:i])
            del buf[:i + 1]''',
     check=r'''assert list(frames([b"ab\x00c", b"d\x00\x00", b"ef"])) == [b"ab", b"cd", b""]'''),

# =========================================================== DAY 4 · C
dict(id="C4-01", title="ring buffer push / pop",
     prompt="RB_SIZE 16(2의 거듭제곱), free-running head/tail, rb_push / rb_pop (drop-new).",
     answer=r'''#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;
} rb_t;

bool rb_push(rb_t *rb, uint8_t b)
{
    if (rb->head - rb->tail == RB_SIZE)
        return false;
    rb->buf[rb->head & RB_MASK] = b;
    rb->head++;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *out)
{
    if (rb->head == rb->tail)
        return false;
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}''',
     check=r'''rb_t rb = {{0}, UINT32_MAX - 2, UINT32_MAX - 2};
uint8_t v;
for (int i = 0; i < 16; i++) assert(rb_push(&rb, (uint8_t)i));
assert(!rb_push(&rb, 99));
for (int i = 0; i < 16; i++) assert(rb_pop(&rb, &v) && v == i);
assert(!rb_pop(&rb, &v));'''),
dict(id="C4-02", title="ring count / space / empty / full",
     prompt="rb_t(위와 같은 모양, given)에 대해 rb_count, rb_space, rb_empty, rb_full.",
     given=r'''#define RB_SIZE 16u
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;''',
     answer=r'''static inline uint32_t rb_count(const rb_t *rb) { return rb->head - rb->tail; }
static inline uint32_t rb_space(const rb_t *rb) { return RB_SIZE - rb_count(rb); }
static inline bool rb_empty(const rb_t *rb)     { return rb_count(rb) == 0; }
static inline bool rb_full(const rb_t *rb)      { return rb_count(rb) == RB_SIZE; }''',
     check=r'''rb_t rb = {{0}, 2, UINT32_MAX - 1};
assert(rb_count(&rb) == 4 && rb_space(&rb) == 12 && !rb_empty(&rb) && !rb_full(&rb));
rb.tail = rb.head;
assert(rb_empty(&rb));'''),
dict(id="C4-03", title="SPSC push (C11 atomics)",
     prompt="ISR producer용 spsc_push: tail은 acquire로 읽고, 데이터를 쓴 뒤 head를 release로 publish.",
     answer=r'''#define Q_SIZE 8u

typedef struct {
    uint8_t buf[Q_SIZE];
    _Atomic uint32_t head, tail;
} spsc_t;

bool spsc_push(spsc_t *q, uint8_t b)
{
    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (head - tail == Q_SIZE)
        return false;
    q->buf[head & (Q_SIZE - 1u)] = b;
    atomic_store_explicit(&q->head, head + 1, memory_order_release);
    return true;
}''',
     check=r'''static spsc_t q;
for (int i = 0; i < 8; i++) assert(spsc_push(&q, (uint8_t)i));
assert(!spsc_push(&q, 9));
assert(atomic_load(&q.head) == 8 && q.buf[7] == 7);'''),
dict(id="C4-04", title="ISR 플래그 패턴",
     prompt="ISR이 세우는 volatile 플래그 g_rx_ready와 main loop에서 읽고 내리는 take_rx_ready(void). (단일 코어, 플래그 하나)",
     answer=r'''static volatile bool g_rx_ready;

void uart_rx_isr(void)
{
    g_rx_ready = true;
}

bool take_rx_ready(void)
{
    if (!g_rx_ready)
        return false;
    g_rx_ready = false;
    return true;
}''',
     check=r'''assert(!take_rx_ready());
uart_rx_isr();
assert(take_rx_ready() && !take_rx_ready());'''),
dict(id="C4-05", title="switch 상태 머신",
     prompt="state_t {IDLE, ARMED, FLYING}, event_t {EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND}. 허용 안 되는 이벤트는 상태 유지. sm_step(s, e).",
     answer=r'''typedef enum { IDLE, ARMED, FLYING } state_t;
typedef enum { EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND } event_t;

state_t sm_step(state_t s, event_t e)
{
    switch (s) {
    case IDLE:   return e == EV_ARM ? ARMED : s;
    case ARMED:  return e == EV_TAKEOFF ? FLYING : e == EV_DISARM ? IDLE : s;
    case FLYING: return e == EV_LAND ? ARMED : s;
    }
    return s;
}''',
     check=r'''assert(sm_step(IDLE, EV_ARM) == ARMED);
assert(sm_step(IDLE, EV_TAKEOFF) == IDLE);
assert(sm_step(sm_step(ARMED, EV_TAKEOFF), EV_DISARM) == FLYING);'''),
dict(id="C4-06", title="테이블 기반 상태 머신",
     prompt="transition_t {from, ev, to} 배열을 순회하는 sm_lookup(s, e). 없으면 s 그대로. (state_t/event_t는 given)",
     given=r'''typedef enum { IDLE, ARMED, FLYING } state_t;
typedef enum { EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND } event_t;''',
     answer=r'''typedef struct {
    state_t from;
    event_t ev;
    state_t to;
} transition_t;

static const transition_t TABLE[] = {
    {IDLE, EV_ARM, ARMED},
    {ARMED, EV_TAKEOFF, FLYING},
    {ARMED, EV_DISARM, IDLE},
    {FLYING, EV_LAND, ARMED},
};

state_t sm_lookup(state_t s, event_t e)
{
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
        if (TABLE[i].from == s && TABLE[i].ev == e)
            return TABLE[i].to;
    return s;
}''',
     check=r'''assert(sm_lookup(IDLE, EV_ARM) == ARMED && sm_lookup(FLYING, EV_ARM) == FLYING);'''),
dict(id="C4-07", title="디바운스",
     prompt="debounce_t {stable, count}. raw가 stable과 다른 값으로 N(=3)번 연속이면 stable을 바꾼다. debounce(d, raw) → stable.",
     answer=r'''#define DEBOUNCE_N 3

typedef struct {
    bool    stable;
    uint8_t count;
} debounce_t;

bool debounce(debounce_t *d, bool raw)
{
    if (raw == d->stable) {
        d->count = 0;
    } else if (++d->count >= DEBOUNCE_N) {
        d->stable = raw;
        d->count = 0;
    }
    return d->stable;
}''',
     check=r'''debounce_t d = {false, 0};
assert(!debounce(&d, true) && !debounce(&d, true));
assert(!debounce(&d, false) && !debounce(&d, true));
assert(!debounce(&d, true) && debounce(&d, true));'''),
dict(id="C4-08", title="이동 평균 (링 + 합)",
     prompt="avg_t {buf[8], idx, n, sum}. 새 값을 넣고 지금까지(최대 8개)의 평균을 돌려주는 avg_push(a, v). 합은 O(1)로 갱신.",
     answer=r'''#define AVG_N 8

typedef struct {
    int32_t buf[AVG_N];
    uint8_t idx, n;
    int32_t sum;
} avg_t;

int32_t avg_push(avg_t *a, int32_t v)
{
    if (a->n == AVG_N)
        a->sum -= a->buf[a->idx];
    else
        a->n++;
    a->buf[a->idx] = v;
    a->sum += v;
    a->idx = (uint8_t)((a->idx + 1) % AVG_N);
    return a->sum / a->n;
}''',
     check=r'''avg_t a = {{0}, 0, 0, 0};
assert(avg_push(&a, 10) == 10 && avg_push(&a, 20) == 15);
for (int i = 0; i < 8; i++) avg_push(&a, 4);
assert(a.sum == 32 && avg_push(&a, 12) == 5);'''),
dict(id="C4-09", title="tick wrap 안전한 timeout",
     prompt="32비트 tick이 wrap 해도 맞는 timed_out(now, start, timeout).",
     answer=r'''bool timed_out(uint32_t now, uint32_t start, uint32_t timeout)
{
    return (uint32_t)(now - start) >= timeout;
}''',
     check=r'''assert(!timed_out(105, 100, 10) && timed_out(110, 100, 10));
assert(timed_out(5, UINT32_MAX - 4, 10) && !timed_out(3, UINT32_MAX - 4, 10));
assert(!timed_out(UINT32_MAX - 1, UINT32_MAX - 4, 10));   /* naive now >= start + timeout fails here */'''),
dict(id="C4-10", title="bulk write (memcpy 두 번)",
     prompt="rb_t(given, RB_SIZE 16)에 최대 len바이트를 wrap 지점에서 두 번 memcpy로 쓰고 쓴 개수를 돌려주는 rb_write(rb, src, len).",
     given=r'''#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;''',
     answer=r'''size_t rb_write(rb_t *rb, const uint8_t *src, size_t len)
{
    uint32_t space = RB_SIZE - (rb->head - rb->tail);
    uint32_t n = len < space ? (uint32_t)len : space;
    uint32_t idx = rb->head & RB_MASK;
    uint32_t first = RB_SIZE - idx;
    if (first > n)
        first = n;
    memcpy(&rb->buf[idx], src, first);
    memcpy(&rb->buf[0], src + first, n - first);
    rb->head += n;
    return n;
}''',
     check=r'''rb_t rb = {{0}, 14, 14};
uint8_t in[20];
for (int i = 0; i < 20; i++) in[i] = (uint8_t)i;
assert(rb_write(&rb, in, 20) == 16);
assert(rb.buf[14] == 0 && rb.buf[15] == 1 && rb.buf[0] == 2 && rb.buf[13] == 15);'''),

# =========================================================== DAY 5 · Python (pytest)
dict(id="P5-01", title="assert · pytest.raises",
     prompt="given의 divide에 대해: 정상값 테스트 1개, b=0이면 ZeroDivisionError를 match로 확인하는 테스트 1개.",
     pytest=True,
     given=r'''import pytest


def divide(a, b):
    if b == 0:
        raise ZeroDivisionError("b must not be zero")
    return a / b''',
     answer=r'''def test_divide():
    assert divide(6, 3) == 2


def test_divide_by_zero():
    with pytest.raises(ZeroDivisionError, match="zero"):
        divide(1, 0)'''),
dict(id="P5-02", title="parametrize + ids",
     prompt="crc8(given)를 (data, expected) 3쌍으로 parametrize, ids도 붙인다.",
     pytest=True,
     given=r'''import pytest


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc''',
     answer=r'''@pytest.mark.parametrize("data, expected", [
    (b"", 0x00),
    (b"123456789", 0xF4),
    (b"\x00", 0x00),
], ids=["empty", "check", "zero"])
def test_crc8(data, expected):
    assert crc8(data) == expected'''),
dict(id="P5-03", title="yield fixture (teardown)",
     prompt="FakePort(given)를 열어 주고, 테스트가 끝나면(실패해도) close 하는 port fixture + 그걸 쓰는 테스트.",
     pytest=True,
     given=r'''import pytest


class FakePort:
    def __init__(self):
        self.closed = False
        self.sent = []

    def write(self, b):
        self.sent.append(b)

    def close(self):
        self.closed = True''',
     answer=r'''@pytest.fixture
def port():
    p = FakePort()
    yield p
    p.close()


def test_write(port):
    port.write(b"PING\n")
    assert port.sent == [b"PING\n"]
    assert not port.closed'''),
dict(id="P5-04", title="fixture 조합 + scope",
     prompt="module scope rig fixture(dict로 흉내)와, 그걸 받아 매번 새 dut 상태를 만드는 function scope dut fixture. 테스트 2개에서 rig는 같은 객체.",
     pytest=True,
     given=r'''import pytest

seen = []''',
     answer=r'''@pytest.fixture(scope="module")
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
    assert len(set(seen)) == 1'''),
dict(id="P5-05", title="tmp_path",
     prompt="given의 save_log(path, lines)를 tmp_path로 테스트: 파일을 읽어 줄 수 확인.",
     pytest=True,
     given=r'''def save_log(path, lines):
    path.write_text("\n".join(lines) + "\n")''',
     answer=r'''def test_save_log(tmp_path):
    p = tmp_path / "dut.log"
    save_log(p, ["boot", "armed"])
    assert p.read_text().splitlines() == ["boot", "armed"]'''),
dict(id="P5-06", title="monkeypatch",
     prompt="환경변수 RIG를 읽는 rig_name()(given)을 monkeypatch.setenv / delenv로 두 경우 테스트.",
     pytest=True,
     given=r'''import os


def rig_name():
    return os.environ.get("RIG", "none")''',
     answer=r'''def test_rig_from_env(monkeypatch):
    monkeypatch.setenv("RIG", "sim")
    assert rig_name() == "sim"


def test_rig_default(monkeypatch):
    monkeypatch.delenv("RIG", raising=False)
    assert rig_name() == "none"'''),
dict(id="P5-07", title="Mock · side_effect · call_args_list",
     prompt="read_temp(port)(given)는 빈 응답이면 한 번 더 보낸다. Mock 포트로 [b\"\", b\"T=21\\n\"] 대본을 주고, 결과와 write 호출 2번을 검증.",
     pytest=True,
     given=r'''from unittest.mock import Mock, call


def read_temp(port):
    for _ in range(2):
        port.write(b"T?\n")
        line = port.readline()
        if line:
            return int(line.strip().split(b"=")[1])
    raise TimeoutError''',
     answer=r'''def test_read_temp_retries_once():
    port = Mock(spec=["write", "readline"])
    port.readline.side_effect = [b"", b"T=21\n"]
    assert read_temp(port) == 21
    assert port.write.call_args_list == [call(b"T?\n"), call(b"T?\n")]'''),
dict(id="P5-08", title="patch.object (time.sleep)",
     prompt="backoff_wait()(given)가 time.sleep을 0.1, 0.2로 부르는지, 실제로 기다리지 않고 patch.object로 검증.",
     pytest=True,
     given=r'''import time
from unittest.mock import patch, call


def backoff_wait(n=2, base=0.1):
    for i in range(n):
        time.sleep(base * 2 ** i)''',
     answer=r'''def test_backoff_wait():
    with patch.object(time, "sleep") as fake_sleep:
        backoff_wait()
    assert fake_sleep.call_args_list == [call(0.1), call(0.2)]'''),
dict(id="P5-09", title="skipif marker",
     prompt="환경변수 RIG가 없으면 skip 되는 hw 테스트 하나 (pytest.mark.skipif), 그리고 항상 도는 테스트 하나.",
     pytest=True,
     given=r'''import os
import pytest''',
     answer=r'''needs_rig = pytest.mark.skipif(not os.environ.get("RIG"), reason="no rig (set RIG=sim)")


@needs_rig
def test_on_rig():
    assert os.environ["RIG"]


def test_pure_logic():
    assert int("1F", 16) == 31'''),
dict(id="P5-10", title="caplog",
     prompt="connect()(given)가 재시도 때 WARNING 로그를 남기는지 caplog로 확인.",
     pytest=True,
     given=r'''import logging

log = logging.getLogger("dut")


def connect(attempts):
    for i in range(attempts - 1):
        log.warning("retry %d", i + 1)
    return True''',
     answer=r'''def test_connect_logs_retries(caplog):
    with caplog.at_level(logging.WARNING, logger="dut"):
        assert connect(3)
    assert [r.getMessage() for r in caplog.records] == ["retry 1", "retry 2"]'''),

# =========================================================== DAY 5 · C (tests)
dict(id="C5-01", title="CHECK 매크로 (do-while 0)",
     prompt="실패하면 파일:줄:식을 출력하고 g_fail을 올리는 CHECK(cond) 매크로.",
     answer=r'''static int g_fail;

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_fail++;                                                 \
        }                                                             \
    } while (0)''',
     check=r'''CHECK(1 + 1 == 2);
if (g_fail == 0) CHECK(2 > 3); else CHECK(0);
assert(g_fail == 1);'''),
dict(id="C5-02", title="테이블 기반 테스트",
     prompt="sext12(given)를 {raw, expected} 케이스 배열로 검사하는 test_sext12(void) → 실패 개수.",
     given=r'''int16_t sext12(uint16_t raw)
{
    raw &= 0x0FFF;
    return (raw & 0x0800) ? (int16_t)(raw - 0x1000) : (int16_t)raw;
}''',
     answer=r'''int test_sext12(void)
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
}''',
     check=r'''assert(test_sext12() == 0);'''),
dict(id="C5-03", title="fake HAL (함수 포인터 주입)",
     prompt="i2c_ops_t {int (*read)(void *ctx, uint8_t reg, uint8_t *buf, size_t n); void *ctx;}로 온도(reg 0x00, 2바이트 BE, 단위 0.01도)를 읽는 temp_read(ops, out). 에러면 -1.",
     answer=r'''typedef struct {
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
}''',
     support=r'''struct fake { uint8_t data[2]; int fail; };

static int fake_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n)
{
    struct fake *f = ctx;
    if (f->fail || reg != 0x00)
        return -5;
    memcpy(buf, f->data, n);
    return 0;
}''',
     check=r'''static struct fake f = {{0x09, 0x60}, 0};
i2c_ops_t ops = {fake_read, &f};
int16_t t;
assert(temp_read(&ops, &t) == 0 && t == 2400);
f.fail = 1;
assert(temp_read(&ops, &t) == -1);'''),
dict(id="C5-04", title="_Static_assert",
     prompt="wire 헤더 구조체 크기가 4바이트이고 RB_SIZE가 2의 거듭제곱인지 컴파일 타임에 확인.",
     given=r'''#define RB_SIZE 64u
typedef struct { uint8_t sync, type; uint16_t len; } wire_hdr_t;''',
     answer=r'''_Static_assert(sizeof(wire_hdr_t) == 4, "wire header must be 4 bytes");
_Static_assert((RB_SIZE & (RB_SIZE - 1u)) == 0, "RB_SIZE must be a power of 2");''',
     check=r'''assert(sizeof(wire_hdr_t) == 4);'''),
dict(id="C5-05", title="시간 주입 (now 함수 포인터)",
     prompt="now_fn 타입(uint32_t (*)(void))을 받아 pred가 참이 될 때까지 기다리는 wait_for(pred, now, timeout_ms) → bool.",
     answer=r'''typedef uint32_t (*now_fn)(void);

bool wait_for(bool (*pred)(void), now_fn now, uint32_t timeout_ms)
{
    uint32_t start = now();
    while (!pred()) {
        if ((uint32_t)(now() - start) >= timeout_ms)
            return false;
    }
    return true;
}''',
     support=r'''static uint32_t fake_ms;
static uint32_t fake_now(void) { return fake_ms++; }
static bool never(void)  { return false; }
static bool always(void) { return true; }''',
     check=r'''assert(wait_for(always, fake_now, 10));
assert(!wait_for(never, fake_now, 10) && fake_ms >= 10);'''),
dict(id="C5-06", title="wrap 경계 테스트",
     prompt="rb_t + rb_push/rb_pop(given)에 대해 head=tail=UINT32_MAX-3에서 10개 넣고 빼며 순서와 개수를 확인하는 test_wrap(void) → 성공 시 true.",
     given=r'''#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;
static bool rb_push(rb_t *rb, uint8_t b) {
    if (rb->head - rb->tail == RB_SIZE) return false;
    rb->buf[rb->head++ & RB_MASK] = b; return true; }
static bool rb_pop(rb_t *rb, uint8_t *o) {
    if (rb->head == rb->tail) return false;
    *o = rb->buf[rb->tail++ & RB_MASK]; return true; }''',
     answer=r'''bool test_wrap(void)
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
}''',
     check=r'''assert(test_wrap());'''),
dict(id="C5-07", title="SWAP 매크로",
     prompt="if/else 안에서도 안전한 SWAP(type, a, b) 매크로 (do-while 0).",
     answer=r'''#define SWAP(type, a, b) \
    do {                     \
        type t_ = (a);       \
        (a) = (b);           \
        (b) = t_;            \
    } while (0)''',
     check=r'''int x = 1, y = 2;
if (x < y) SWAP(int, x, y); else x = 0;
assert(x == 2 && y == 1);'''),
dict(id="C5-08", title="qsort 비교 함수",
     prompt="int32_t 배열을 오름차순 qsort 하는 비교 함수 cmp_i32 (빼기 오버플로 없이).",
     answer=r'''int cmp_i32(const void *pa, const void *pb)
{
    int32_t a = *(const int32_t *)pa;
    int32_t b = *(const int32_t *)pb;
    return (a > b) - (a < b);
}''',
     check=r'''int32_t a[] = {3, INT32_MIN, 2, INT32_MAX, -1};
qsort(a, 5, sizeof a[0], cmp_i32);
assert(a[0] == INT32_MIN && a[1] == -1 && a[4] == INT32_MAX);'''),
dict(id="C5-09", title="snprintf 잘림 검사",
     prompt="\"fw=<ver> rev=<c>\"를 buf에 쓰고, 잘렸으면 false인 fmt_id(buf, cap, ver, rev).",
     answer=r'''bool fmt_id(char *buf, size_t cap, const char *ver, char rev)
{
    int n = snprintf(buf, cap, "fw=%s rev=%c", ver, rev);
    return n >= 0 && (size_t)n < cap;
}''',
     check=r'''char b[32], s[8];
assert(fmt_id(b, sizeof b, "1.4.2", 'C') && strcmp(b, "fw=1.4.2 rev=C") == 0);
assert(!fmt_id(s, sizeof s, "1.4.2", 'C') && strlen(s) == 7);'''),
dict(id="C5-10", title="이진 탐색",
     prompt="정렬된 int 배열에서 key의 인덱스(없으면 -1) bin_search(a, n, key). mid 오버플로 피하기.",
     answer=r'''int bin_search(const int *a, int n, int key)
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
}''',
     check=r'''int a[] = {1, 3, 5, 7, 9};
assert(bin_search(a, 5, 7) == 3 && bin_search(a, 5, 1) == 0 && bin_search(a, 5, 4) == -1 && bin_search(a, 0, 1) == -1);'''),
]

for _it in ITEMS:
    _it.setdefault("given", "")
    _it.setdefault("check", "")
    _it.setdefault("support", "")
    _it.setdefault("pytest", False)
    _it["lang"] = "py" if _it["id"][0] == "P" else "c"
    _it["day"] = int(_it["id"][1])
