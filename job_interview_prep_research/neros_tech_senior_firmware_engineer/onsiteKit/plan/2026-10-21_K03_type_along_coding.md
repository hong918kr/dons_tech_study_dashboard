# ⌨️ K03 따라 치는 코딩 — 7문제, 우선순위 순서

> 바쁜 와중에 손에 붙이기용. 🆕 10-21: 문제마다 **C 버전**도 붙였다 (`tN_*.c`, `-Wall -Wextra -Werror` + ASan/UBSan으로 7/7 통과). 코딩은 Python이 기본이고, C는 "C로 하면?" 후속 질문 · Python이 막힐 때 대비. 각 문제는 **면접관 문장 → 먼저 물을 질문 → 따라 칠 코드(풀이 + 테스트) → 말할 한 줄** 순서다. 코드는 C01~C07 정답을 **면접 길이(40~60줄)로 줄인 것**이고 전부 `python3 tN_*.py` → `tN OK` 확인했다. 원본 정답(더 자세함)은 `code/c0N_*.py`.

## 0. 이렇게 쓴다

| 회차 | 방법 | 시간 |
|---|---|---|
| 1 | 코드를 **보면서 그대로** 빈 파일에 친다 → `python3 파일.py` | 문제당 10~15분 |
| 2 | 면접관 문장과 질문만 보고 **안 보고** 친다 → 막히면 그 줄만 보기 | 문제당 15~20분 |
| 3 (10-22 아침) | T1만 빈 파일에서 한 번 | 10분 |

- 시간이 정말 없으면: **T1 → T2 → T3** 세 개만. 이 셋이 S등급이다
- 칠 때 **주석까지** 친다. 주석이 면접에서 소리 내어 말할 내용이다
- 테스트 블록(`if __name__ == "__main__":`)도 반드시 친다. 지난번 피드백이 바로 **overflow 테스트**였다
- 연습 폴더: `onsiteKit/code/typealong/` (정답 파일) · 빈 파일은 아무 곳에나 `my_t1.py`로

## 1. 모든 문제의 첫 3분 — 이것부터 말한다

```text
1. "Let me repeat it back: ..."                      (문제를 내 말로)
2. Questions: size limit? what when FULL? bad input? time units?
3. "Edge cases I'll test: empty, full, exactly one, wrap-around, garbage."
4. "I'll write it, then tests at the bottom with plain asserts."
```
> Jon 세션 시작에 한 번: "Last time I missed the full-buffer case, so I'll call out overflow behavior up front."

## 2. T1 고정 크기 FIFO — 지난번 놓친 overflow

> **우선순위 S · 1순위 · 15분** · 파일 `code/typealong/t1_fifo.py`

**면접관 문장**

> "An interrupt hands us one byte at a time and the main loop drains them later. Memory is fixed at startup. Give me put and get, and tell me what happens when the main loop falls behind."

**먼저 물을 질문**

1. "When it's full — reject the new byte, or overwrite the oldest?"
2. "Do you want to know how many we lost?"
3. "Bulk put that doesn't fit — all-or-nothing?"

**함정**: 덮어쓰기에서 **tail을 안 밀면** 다음 get이 새 값을 준다 · `count`로 full/empty 구분 · bulk는 **쓰기 전에** 검사

**따라 칠 코드**

```python
# T1 fixed FIFO -- "ISR hands bytes, main loop drains, memory fixed, what if main falls behind?"
class Fifo:
    def __init__(self, cap, overwrite=False):
        if cap <= 0:
            raise ValueError("cap must be > 0")
        self.buf = [None] * cap
        self.cap, self.overwrite = cap, overwrite
        self.head = self.tail = self.count = 0     # head: next write, tail: next read
        self.dropped = self.overwritten = 0

    def put(self, x):
        if self.count == self.cap:                  # OVERFLOW: decide, then count
            if not self.overwrite:
                self.dropped += 1
                return False
            self.tail = (self.tail + 1) % self.cap  # drop oldest: move tail too
            self.count -= 1
            self.overwritten += 1
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def get(self):
        if self.count == 0:
            return None
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x

    def put_all(self, items):                       # all-or-nothing
        if len(items) > self.cap - self.count:
            self.dropped += len(items)
            return False
        for x in items:
            self.put(x)
        return True


if __name__ == "__main__":
    f = Fifo(3)
    assert f.get() is None                              # empty
    assert f.put(1) and f.put(2) and f.put(3)
    assert f.put(4) is False and f.dropped == 1         # full -> reject + count
    assert [f.get(), f.get(), f.get()] == [1, 2, 3]     # old data and order kept
    g = Fifo(3, overwrite=True)
    for i in range(1, 6):
        g.put(i)
    assert g.overwritten == 2 and [g.get(), g.get(), g.get()] == [3, 4, 5]
    h = Fifo(5)
    assert h.put_all([1, 2, 3]) and not h.put_all([4, 5, 6]) and h.count == 3
    for i in range(20):                                 # wrap many laps
        h.get(); h.put(i)
    print("t1 OK")
```

**C 버전** — `code/typealong/t1_fifo.c` · C에서 다른 점: `len > cap - count` (빼기, `count + len`은 wrap) · 저장소는 호출자가 준다

```c
/* T1 fixed FIFO -- "ISR hands bytes, main loop drains, memory fixed, what if main falls behind?" */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t *buf;            /* caller-owned storage, no malloc */
    size_t cap;
    size_t head, tail, count; /* head: next write, tail: next read */
    bool overwrite;
    uint32_t dropped, overwritten;
} fifo_t;

void fifo_init(fifo_t *f, uint8_t *storage, size_t cap, bool overwrite)
{
    f->buf = storage;
    f->cap = cap;
    f->head = f->tail = f->count = 0;
    f->overwrite = overwrite;
    f->dropped = f->overwritten = 0;
}

bool fifo_put(fifo_t *f, uint8_t x)
{
    if (f->count == f->cap) {                 /* OVERFLOW: decide, then count */
        if (!f->overwrite) {
            f->dropped++;
            return false;
        }
        f->tail = (f->tail + 1) % f->cap;     /* drop oldest: move tail too */
        f->count--;
        f->overwritten++;
    }
    f->buf[f->head] = x;
    f->head = (f->head + 1) % f->cap;
    f->count++;
    return true;
}

bool fifo_get(fifo_t *f, uint8_t *out)
{
    if (f->count == 0)
        return false;
    *out = f->buf[f->tail];
    f->tail = (f->tail + 1) % f->cap;
    f->count--;
    return true;
}

bool fifo_put_all(fifo_t *f, const uint8_t *items, size_t len)
{
    if (len > f->cap - f->count) {            /* subtract, never "count + len" (can wrap) */
        f->dropped += (uint32_t)len;
        return false;
    }
    for (size_t i = 0; i < len; i++)
        fifo_put(f, items[i]);
    return true;
}

int main(void)
{
    uint8_t mem[3], b;
    fifo_t f;
    fifo_init(&f, mem, sizeof mem, false);
    assert(!fifo_get(&f, &b));                               /* empty */
    assert(fifo_put(&f, 1) && fifo_put(&f, 2) && fifo_put(&f, 3));
    assert(!fifo_put(&f, 4) && f.dropped == 1);              /* full -> reject + count */
    for (uint8_t want = 1; want <= 3; want++)
        assert(fifo_get(&f, &b) && b == want);               /* old data and order kept */

    fifo_init(&f, mem, sizeof mem, true);
    for (uint8_t i = 1; i <= 5; i++)
        fifo_put(&f, i);
    assert(f.overwritten == 2);
    for (uint8_t want = 3; want <= 5; want++)
        assert(fifo_get(&f, &b) && b == want);

    uint8_t mem5[5];
    const uint8_t abc[] = {1, 2, 3}, xyz[] = {4, 5, 6};
    fifo_init(&f, mem5, sizeof mem5, false);
    assert(fifo_put_all(&f, abc, 3) && !fifo_put_all(&f, xyz, 3) && f.count == 3);
    assert(!fifo_put_all(&f, abc, SIZE_MAX));                /* huge len must not wrap the check */
    for (uint8_t i = 0; i < 20; i++) {                       /* wrap many laps */
        assert(fifo_get(&f, &b));
        assert(fifo_put(&f, i));
    }
    puts("t1 OK");
    return 0;
}
```

**말할 한 줄**: "Full is the case I care most about: I reject and count, or overwrite and move the tail too — never lose data silently."

## 3. T2 바이트 스트림 → 패킷

> **우선순위 S · 2순위 · 20분** · 파일 `code/typealong/t2_packets.py`

**면접관 문장**

> "Bytes come in from a serial port in random-sized chunks. Each message is 0xAA, a length byte, that many payload bytes, then a checksum — the low 8 bits of the sum of the length and payload bytes. Give me back the complete, valid messages."

**먼저 물을 질문**

1. "Is there a maximum payload length?"
2. "Can 0xAA appear inside the payload?"
3. "On a bad checksum — just drop, or count?"

**함정**: 깨지면 **1바이트만** 버리고 재동기 · 잘린 패킷은 기다림 · 쓰레기만 와도 버퍼가 안 커짐 · 한 바이트씩 넣어도 같은 결과

**따라 칠 코드**

```python
# T2 byte stream -> packets -- "0xAA, len, payload, checksum = low 8 bits of sum(len+payload)"
SYNC = 0xAA

class Parser:
    def __init__(self, max_payload=64):
        self.max_payload = max_payload
        self.buf = bytearray()
        self.dropped = self.bad_chk = 0

    def feed(self, chunk):
        self.buf += chunk
        out = []
        while True:
            i = self.buf.find(SYNC)
            if i < 0:                               # only garbage: keep nothing
                self.dropped += len(self.buf)
                self.buf.clear()
                break
            self.dropped += i
            del self.buf[:i]
            if len(self.buf) < 2:                   # need the length byte
                break
            n = self.buf[1]
            if n > self.max_payload:                # fake sync -> drop ONE byte, retry
                del self.buf[:1]
                self.dropped += 1
                continue
            if len(self.buf) < 3 + n:               # partial: wait for more
                break
            if sum(self.buf[1:2 + n]) & 0xFF != self.buf[2 + n]:
                del self.buf[:1]                    # resync from the NEXT byte
                self.bad_chk += 1
                self.dropped += 1
                continue
            out.append(bytes(self.buf[2:2 + n]))
            del self.buf[:3 + n]
        return out


def frame(payload):
    return bytes([SYNC, len(payload)]) + payload + bytes([(len(payload) + sum(payload)) & 0xFF])


if __name__ == "__main__":
    p = Parser()
    assert p.feed(frame(b"\x10\x20")) == [b"\x10\x20"]          # happy
    data = b"\x00\x01" + frame(b"A") + frame(b"") + frame(b"BC")
    assert Parser().feed(data) == [b"A", b"", b"BC"]            # garbage, len 0, many per chunk
    q, got = Parser(), []
    for b in data:                                              # one byte at a time
        got += q.feed(bytes([b]))
    assert got == [b"A", b"", b"BC"]
    bad = bytearray(frame(b"XY")); bad[-1] ^= 0xFF
    assert Parser().feed(bytes(bad) + frame(b"Z")) == [b"Z"]    # bad checksum, then recover
    r = Parser()
    assert r.feed(b"\x55" * 1000) == [] and len(r.buf) == 0     # garbage never grows the buffer
    print("t2 OK")
```

**C 버전** — `code/typealong/t2_packets.c` · C에서 다른 점: 고정 버퍼 = 프레임 하나 크기, 한 바이트 넣고 처리 → 절대 안 넘침 · `uint8_t sum`은 자동으로 하위 8비트 · 결과는 콜백으로

```c
/* T2 byte stream -> packets -- "0xAA, len, payload, checksum = low 8 bits of sum(len+payload)" */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYNC 0xAA
#define MAX_PAYLOAD 64
#define MAX_FRAME (3 + MAX_PAYLOAD)

typedef void (*on_packet_fn)(const uint8_t *payload, size_t len, void *ctx);

typedef struct {
    uint8_t buf[MAX_FRAME];   /* never holds more than one frame */
    size_t len;
    uint32_t dropped, bad_chk;
} parser_t;

static void drop_front(parser_t *p, size_t n)
{
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

static void process(parser_t *p, on_packet_fn cb, void *ctx)
{
    for (;;) {
        size_t i = 0;
        while (i < p->len && p->buf[i] != SYNC)
            i++;
        p->dropped += (uint32_t)i;           /* garbage before sync */
        drop_front(p, i);
        if (p->len < 2)                      /* need the length byte */
            return;
        size_t n = p->buf[1];
        if (n > MAX_PAYLOAD) {               /* fake sync -> drop ONE byte, retry */
            drop_front(p, 1);
            p->dropped++;
            continue;
        }
        if (p->len < 3 + n)                  /* partial: wait for more */
            return;
        uint8_t sum = 0;
        for (size_t k = 1; k < 2 + n; k++)
            sum += p->buf[k];                /* uint8_t wraps = low 8 bits */
        if (sum != p->buf[2 + n]) {
            drop_front(p, 1);                /* resync from the NEXT byte */
            p->bad_chk++;
            p->dropped++;
            continue;
        }
        cb(p->buf + 2, n, ctx);
        drop_front(p, 3 + n);
    }
}

void parser_feed(parser_t *p, const uint8_t *chunk, size_t n, on_packet_fn cb, void *ctx)
{
    for (size_t i = 0; i < n; i++) {        /* one byte in, then process: buf can't overflow */
        p->buf[p->len++] = chunk[i];
        process(p, cb, ctx);
    }
}

/* ---- tests ---- */
typedef struct { uint8_t data[256]; size_t n; int packets; } sink_t;

static void collect(const uint8_t *payload, size_t len, void *ctx)
{
    sink_t *s = ctx;
    s->data[s->n++] = (uint8_t)len;          /* record "len, payload..." */
    memcpy(s->data + s->n, payload, len);
    s->n += len;
    s->packets++;
}

static size_t frame(uint8_t *out, const uint8_t *payload, uint8_t n)
{
    uint8_t sum = n;
    out[0] = SYNC;
    out[1] = n;
    for (uint8_t i = 0; i < n; i++) {
        out[2 + i] = payload[i];
        sum += payload[i];
    }
    out[2 + n] = sum;
    return 3u + n;
}

int main(void)
{
    uint8_t stream[64];
    size_t len = 0;
    stream[len++] = 0x00;                                  /* garbage */
    len += frame(stream + len, (const uint8_t *)"A", 1);
    len += frame(stream + len, NULL, 0);                   /* len 0 */
    len += frame(stream + len, (const uint8_t *)"BC", 2);
    const uint8_t want[] = {1, 'A', 0, 2, 'B', 'C'};

    parser_t p = {0};
    sink_t s = {0};
    parser_feed(&p, stream, len, collect, &s);             /* all in one chunk */
    assert(s.packets == 3 && s.n == sizeof want && memcmp(s.data, want, s.n) == 0);

    parser_t q = {0};
    sink_t t = {0};
    for (size_t i = 0; i < len; i++)                       /* one byte at a time */
        parser_feed(&q, stream + i, 1, collect, &t);
    assert(t.packets == 3 && memcmp(t.data, want, t.n) == 0);

    uint8_t bad[16];
    size_t bl = frame(bad, (const uint8_t *)"XY", 2);
    bad[bl - 1] ^= 0xFF;                                    /* bad checksum */
    bl += frame(bad + bl, (const uint8_t *)"Z", 1);
    parser_t r = {0};
    sink_t u = {0};
    parser_feed(&r, bad, bl, collect, &u);
    assert(u.packets == 1 && u.data[1] == 'Z' && r.bad_chk == 1);

    uint8_t junk[1000];
    memset(junk, 0x55, sizeof junk);
    parser_feed(&r, junk, sizeof junk, collect, &u);        /* garbage never grows the buffer */
    assert(r.len == 0);
    puts("t2 OK");
    return 0;
}
```

**말할 한 줄**: "On a bad frame I drop only the sync byte and search again, because the real frame may start inside the fake one."

## 4. T3 명령 · ACK · timeout · retry

> **우선순위 S · 3순위 · 20분** · 파일 `code/typealong/t3_retry.py`

**면접관 문장**

> "The test host sends commands to the flight controller over a lossy link. Each command gets an ACK with the same id. If there's no ACK in 50 ms, resend, up to 3 tries total, then report it failed. Ids are one byte."

**먼저 물을 질문**

1. "How many can be outstanding? What if we hit the limit?"
2. "3 tries total, or 3 retries after the first?"
3. "Can I pass the current time in? It makes this testable without sleeping."

**함정**: `now` 주입 · **정수 ms** · 정확히 마감 시각에 재전송 · id 255 → 0 wrap 때 아직 기다리는 id 건너뛰기 · 늦은/중복 ACK 카운트 · 순회 중 삭제는 `list(dict)`

**따라 칠 코드**

```python
# T3 command / ACK / timeout / retry -- "no ACK in 50 ms -> resend, 3 tries total, ids are one byte"
class Sender:
    def __init__(self, send, timeout=50, max_tries=3, max_inflight=8):
        self.send, self.timeout = send, timeout
        self.max_tries, self.max_inflight = max_tries, max_inflight
        self.inflight = {}                          # id -> [payload, tries, deadline_ms]
        self.next_id = 0
        self.stale_acks = 0

    def submit(self, payload, now):
        if len(self.inflight) >= self.max_inflight:
            return None                             # back-pressure, caller decides
        while self.next_id in self.inflight:        # 8-bit wrap: skip ids still waiting
            self.next_id = (self.next_id + 1) & 0xFF
        cid = self.next_id
        self.next_id = (cid + 1) & 0xFF
        self.inflight[cid] = [payload, 1, now + self.timeout]
        self.send(cid, payload)
        return cid

    def on_ack(self, cid):
        if cid in self.inflight:
            del self.inflight[cid]
            return True
        self.stale_acks += 1                        # late / duplicate / unknown
        return False

    def tick(self, now):
        failed = []
        for cid in list(self.inflight):             # copy keys: we delete while looping
            payload, tries, deadline = self.inflight[cid]
            if now < deadline:
                continue
            if tries >= self.max_tries:
                del self.inflight[cid]
                failed.append(cid)
            else:
                self.inflight[cid] = [payload, tries + 1, now + self.timeout]
                self.send(cid, payload)
        return failed


if __name__ == "__main__":
    sent = []
    s = Sender(lambda c, p: sent.append(c))
    a = s.submit(b"x", 1000)
    assert s.tick(1049) == [] and len(sent) == 1        # just before deadline
    assert s.tick(1050) == [] and len(sent) == 2        # exactly at deadline -> resend
    assert s.tick(1100) == [] and len(sent) == 3
    assert s.tick(1150) == [a] and s.inflight == {}     # 3 tries total -> failed
    assert s.on_ack(a) is False and s.stale_acks == 1   # late ACK
    b = s.submit(b"y", 2000)
    assert s.on_ack(b) and not s.on_ack(b)              # duplicate ACK
    s2 = Sender(lambda c, p: None, max_inflight=2)
    s2.next_id = 255
    assert s2.submit(1, 0) == 255 and s2.submit(2, 0) == 0   # id wraps
    assert s2.submit(3, 0) is None                      # inflight full
    print("t3 OK")
```

**C 버전** — `code/typealong/t3_retry.c` · C에서 다른 점: id로 바로 인덱싱하는 `slot[256]` · `uint8_t next_id++`는 255 → 0 자동 · 시계 wrap: `(int32_t)(now - deadline) >= 0`

```c
/* T3 command / ACK / timeout / retry -- "no ACK in 50 ms -> resend, 3 tries total, ids are one byte" */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TIMEOUT_MS 50u
#define MAX_TRIES 3u
#define MAX_INFLIGHT 8u
#define NO_ID (-1)

typedef void (*send_fn)(uint8_t id, uint8_t payload);

typedef struct {
    bool used;
    uint8_t payload, tries;
    uint32_t deadline;        /* ms */
} slot_t;

typedef struct {
    slot_t slot[256];         /* indexed by the 8-bit id: no search, no malloc */
    unsigned inflight;
    uint8_t next_id;          /* uint8_t: 255 + 1 wraps to 0 by itself */
    uint32_t stale_acks;
    send_fn send;
} sender_t;

/* wrap-safe "now >= deadline" for a 32-bit ms clock (wraps after 49.7 days) */
static bool expired(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

int submit(sender_t *s, uint8_t payload, uint32_t now)
{
    if (s->inflight >= MAX_INFLIGHT)
        return NO_ID;                         /* back-pressure, caller decides */
    while (s->slot[s->next_id].used)          /* skip ids still waiting */
        s->next_id++;
    uint8_t id = s->next_id++;
    s->slot[id] = (slot_t){true, payload, 1, now + TIMEOUT_MS};
    s->inflight++;
    s->send(id, payload);
    return id;
}

bool on_ack(sender_t *s, uint8_t id)
{
    if (!s->slot[id].used) {
        s->stale_acks++;                      /* late / duplicate / unknown */
        return false;
    }
    s->slot[id].used = false;
    s->inflight--;
    return true;
}

/* resend what timed out; write ids that ran out of tries to failed[], return how many */
unsigned tick(sender_t *s, uint32_t now, uint8_t *failed)
{
    unsigned nf = 0;
    for (unsigned id = 0; id < 256; id++) {
        slot_t *c = &s->slot[id];
        if (!c->used || !expired(now, c->deadline))
            continue;
        if (c->tries >= MAX_TRIES) {
            c->used = false;
            s->inflight--;
            failed[nf++] = (uint8_t)id;
        } else {
            c->tries++;
            c->deadline = now + TIMEOUT_MS;
            s->send((uint8_t)id, c->payload);
        }
    }
    return nf;
}

/* ---- tests ---- */
static int sent;
static void fake_send(uint8_t id, uint8_t payload) { (void)id; (void)payload; sent++; }

int main(void)
{
    static sender_t s = {.send = fake_send};
    uint8_t failed[256];
    int a = submit(&s, 7, 1000);
    assert(tick(&s, 1049, failed) == 0 && sent == 1);      /* just before deadline */
    assert(tick(&s, 1050, failed) == 0 && sent == 2);      /* exactly at deadline -> resend */
    assert(tick(&s, 1100, failed) == 0 && sent == 3);
    assert(tick(&s, 1150, failed) == 1 && failed[0] == a && s.inflight == 0);
    assert(!on_ack(&s, (uint8_t)a) && s.stale_acks == 1);   /* late ACK */
    int b = submit(&s, 8, 2000);
    assert(on_ack(&s, (uint8_t)b) && !on_ack(&s, (uint8_t)b)); /* duplicate ACK */

    static sender_t w = {.send = fake_send};
    w.next_id = 255;
    assert(submit(&w, 1, 0) == 255 && submit(&w, 2, 0) == 0);  /* id wraps */
    for (int i = 0; i < 6; i++)
        submit(&w, 0, 0);
    assert(submit(&w, 0, 0) == NO_ID);                      /* inflight full */

    static sender_t z = {.send = fake_send};
    submit(&z, 1, 0xFFFFFFC0u);                              /* deadline 0xFFFFFFF2, clock about to wrap */
    assert(tick(&z, 0xFFFFFFF1u, failed) == 0 && z.slot[0].tries == 1);  /* 1 ms early: wait */
    assert(tick(&z, 0x10, failed) == 0 && z.slot[0].tries == 2);  /* after wrap: naive now >= deadline misses this */
    puts("t3 OK");
    return 0;
}
```

**말할 한 줄**: "Time comes in as an argument in integer milliseconds, so tests don't sleep and there's no float boundary bug."

## 5. T4 최근 N개 평균 + K-of-N 알람

> **우선순위 A · 15분** · 파일 `code/typealong/t4_window.py`

**면접관 문장**

> "We get a battery-voltage reading every 10 ms. Give me a smoothed value over the last N readings, and raise an alarm if at least K of the last N were below a threshold. Sometimes the sensor returns garbage."

**먼저 물을 질문**

1. "Before we have N readings — average what we have?"
2. "Garbage means None, NaN, or out-of-range? Count it as bad?"
3. "Should the alarm latch?"

**함정**: 첫 입력이 None이면 0으로 나누기 없음 · NaN 먼저 검사 · 빠져나가는 값을 합/카운터에서 빼기

**따라 칠 코드**

```python
# T4 last-N moving average + K-of-N alarm -- "battery reading every 10 ms, sometimes garbage"
import math
from collections import deque


def valid(x):
    return x is not None and not (isinstance(x, float) and math.isnan(x))


class Monitor:
    def __init__(self, n, k, low):
        if not 0 < k <= n:
            raise ValueError("need 0 < k <= n")
        self.n, self.k, self.low = n, k, low
        self.vals = deque()                         # valid values for the average
        self.total = 0.0
        self.flags = deque(maxlen=n)                # last n "bad?" flags for the alarm
        self.bad = 0
        self.invalid = 0

    def add(self, x):
        ok = valid(x)
        if not ok:
            self.invalid += 1
        else:
            self.vals.append(x)
            self.total += x
            if len(self.vals) > self.n:
                self.total -= self.vals.popleft()   # O(1): subtract what falls out
        is_bad = (not ok) or x < self.low           # NaN check FIRST
        if len(self.flags) == self.n and self.flags[0]:
            self.bad -= 1                           # oldest flag is about to fall out
        self.flags.append(is_bad)
        self.bad += is_bad
        avg = self.total / len(self.vals) if self.vals else None
        return avg, self.bad >= self.k


if __name__ == "__main__":
    m = Monitor(n=3, k=2, low=10.0)
    assert m.add(None) == (None, False) and m.invalid == 1     # no divide by zero
    assert m.add(12.0) == (12.0, False)                        # warm-up
    assert m.add(9.0)[1] is True                               # None + 9.0 = 2 bad of last 3
    avg, alarm = m.add(12.0)
    assert avg == 11.0 and alarm is False                      # None fell out -> 1 bad
    assert m.add(float("nan"))[1] is True                      # NaN counts as bad
    for _ in range(3):
        avg, alarm = m.add(12.0)
    assert avg == 12.0 and alarm is False                      # window fully replaced
    print("t4 OK")
```

**C 버전** — `code/typealong/t4_window.c` · C에서 다른 점: None 대신 NaN · `isnan` 먼저 · 고정 크기 링 두 개 (`MAX_N`)

```c
/* T4 last-N moving average + K-of-N alarm -- "battery reading every 10 ms, sometimes garbage" */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define MAX_N 32

typedef struct {
    unsigned n, k;
    float low;
    float vals[MAX_N];        /* ring of valid values */
    unsigned vhead, vcount;
    float total;
    bool flags[MAX_N];        /* ring of "bad?" flags */
    unsigned fhead, fcount, bad;
    unsigned invalid;
} monitor_t;

bool monitor_init(monitor_t *m, unsigned n, unsigned k, float low)
{
    if (n == 0 || n > MAX_N || k == 0 || k > n)
        return false;
    *m = (monitor_t){.n = n, .k = k, .low = low};
    return true;
}

/* returns alarm; *avg = NAN when no valid sample yet */
bool monitor_add(monitor_t *m, float x, float *avg)
{
    bool ok = !isnan(x);                      /* NaN check FIRST: NaN < low is false */
    if (!ok) {
        m->invalid++;
    } else {
        if (m->vcount == m->n) {
            m->total -= m->vals[m->vhead];    /* O(1): subtract what falls out */
            m->vcount--;
        }
        m->vals[m->vhead] = x;
        m->vhead = (m->vhead + 1) % m->n;
        m->vcount++;
        m->total += x;
    }
    bool is_bad = !ok || x < m->low;
    if (m->fcount == m->n) {
        if (m->flags[m->fhead])
            m->bad--;                         /* oldest flag falls out */
        m->fcount--;
    }
    m->flags[m->fhead] = is_bad;
    m->fhead = (m->fhead + 1) % m->n;
    m->fcount++;
    m->bad += is_bad;
    *avg = m->vcount ? m->total / (float)m->vcount : NAN;
    return m->bad >= m->k;
}

int main(void)
{
    monitor_t m;
    float avg;
    assert(!monitor_init(&m, 3, 4, 10.0f));                 /* k > n rejected */
    assert(monitor_init(&m, 3, 2, 10.0f));
    assert(!monitor_add(&m, NAN, &avg) && isnan(avg) && m.invalid == 1);  /* no divide by zero */
    assert(!monitor_add(&m, 12.0f, &avg) && avg == 12.0f);  /* warm-up */
    assert(monitor_add(&m, 9.0f, &avg));                     /* NaN + 9.0 = 2 bad of last 3 */
    assert(!monitor_add(&m, 12.0f, &avg) && avg == 11.0f);   /* NaN fell out -> 1 bad */
    for (int i = 0; i < 3; i++)
        monitor_add(&m, 12.0f, &avg);
    assert(avg == 12.0f);                                    /* window fully replaced */
    puts("t4 OK");
    return 0;
}
```

**말할 한 줄**: "I keep a running total so each sample is O(1), and I check for NaN first because NaN compares false to everything."

## 6. T5 로그에서 유실 · 중복 · 순서 (16비트 wrap)

> **우선순위 A · 15분** · 파일 `code/typealong/t5_seq.py`

**면접관 문장**

> "Here's a log from a flight test. Every telemetry line has a seq field. Tell me how many packets we lost. … The counter is 16-bit. … Some lines are corrupted. … Sometimes packets arrive out of order."

**먼저 물을 질문**

1. "Does the first line start at 0?"
2. "A packet that arrives late — lost or not?"
3. "Corrupted lines — skip and count?"

**함정**: `(seq - expected) % 65536` · 65535 → 0은 gap 아님 · 32768 기준 앞/뒤 · 깨진 줄은 세고 넘어감

**따라 칠 코드**

```python
# T5 log check -- "how many packets lost?" ... "seq is 16-bit" ... "some lines corrupted" ... "out of order"
import re

LINE = re.compile(r"\bseq=(\d+)\b")
MOD, HALF = 1 << 16, 1 << 15


def analyze(lines):
    expected = None
    lost = dup = reordered = bad = 0
    missing = set()
    for line in lines:
        m = LINE.search(line)
        if not m or int(m.group(1)) >= MOD:
            bad += 1
            continue
        seq = int(m.group(1))
        if expected is None:
            expected = (seq + 1) % MOD
            continue
        diff = (seq - expected) % MOD               # wrap-safe
        if diff == 0:
            expected = (seq + 1) % MOD
        elif diff < HALF:                           # forward jump: diff missing
            for k in range(diff):
                missing.add((expected + k) % MOD)
            lost += diff
            expected = (seq + 1) % MOD
        elif seq in missing:                        # late arrival
            missing.discard(seq)
            lost -= 1
            reordered += 1
        else:
            dup += 1
    return {"lost": lost, "dup": dup, "reordered": reordered, "bad": bad}


if __name__ == "__main__":
    L = lambda *s: ["t=0 seq=%d" % x for x in s]
    assert analyze(L(1, 2, 3)) == {"lost": 0, "dup": 0, "reordered": 0, "bad": 0}
    assert analyze(L(65534, 65535, 0, 1))["lost"] == 0          # wrap is not a gap
    assert analyze(L(65535, 2))["lost"] == 2                    # 0, 1 lost across wrap
    assert analyze(L(1, 4, 2))["lost"] == 1                     # 2 came late
    assert analyze(L(1, 4, 2))["reordered"] == 1
    assert analyze(L(1, 2, 2))["dup"] == 1
    assert analyze(["seq=1", "garbage", "seq=x", "seq=70000", "seq=2"])["bad"] == 3
    print("t5 OK")
```

**C 버전** — `code/typealong/t5_seq.c` · C에서 다른 점: `uint16_t diff = seq - expected`가 곧 mod 65536 · missing은 8 KB 비트맵 · `strtoul` + 끝 문자 검사

```c
/* T5 log check -- "how many lost?" ... "seq is 16-bit" ... "corrupted lines" ... "out of order" */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool started;
    uint16_t expected;
    long lost;
    unsigned dup, reordered, bad;
    uint8_t missing[65536 / 8];   /* 8 KB bitmap: one bit per seq */
} seqlog_t;

static bool bit_get(const uint8_t *m, uint16_t s) { return m[s >> 3] & (1u << (s & 7)); }
static void bit_set(uint8_t *m, uint16_t s)       { m[s >> 3] |= (uint8_t)(1u << (s & 7)); }
static void bit_clr(uint8_t *m, uint16_t s)       { m[s >> 3] &= (uint8_t)~(1u << (s & 7)); }

/* "... seq=123 ..." -> true and *out, false for anything malformed */
static bool parse_seq(const char *line, uint16_t *out)
{
    const char *p = strstr(line, "seq=");
    if (!p || p[4] < '0' || p[4] > '9')
        return false;
    char *end;
    unsigned long v = strtoul(p + 4, &end, 10);
    if (v > 0xFFFF || (*end != '\0' && *end != ' '))
        return false;
    *out = (uint16_t)v;
    return true;
}

void seqlog_line(seqlog_t *t, const char *line)
{
    uint16_t seq;
    if (!parse_seq(line, &seq)) {
        t->bad++;
        return;
    }
    if (!t->started) {
        t->started = true;
        t->expected = (uint16_t)(seq + 1);
        return;
    }
    uint16_t diff = (uint16_t)(seq - t->expected);   /* uint16_t math = mod 65536 */
    if (diff == 0) {
        t->expected = (uint16_t)(seq + 1);
    } else if (diff < 0x8000) {                       /* forward jump: diff missing */
        for (uint16_t k = 0; k < diff; k++)
            bit_set(t->missing, (uint16_t)(t->expected + k));
        t->lost += diff;
        t->expected = (uint16_t)(seq + 1);
    } else if (bit_get(t->missing, seq)) {            /* late arrival */
        bit_clr(t->missing, seq);
        t->lost--;
        t->reordered++;
    } else {
        t->dup++;
    }
}

static seqlog_t *run(const char **lines, size_t n)
{
    static seqlog_t t;
    memset(&t, 0, sizeof t);
    for (size_t i = 0; i < n; i++)
        seqlog_line(&t, lines[i]);
    return &t;
}

int main(void)
{
    const char *a[] = {"seq=1", "seq=2", "seq=3"};
    assert(run(a, 3)->lost == 0);
    const char *w[] = {"seq=65534", "seq=65535", "seq=0", "seq=1"};
    assert(run(w, 4)->lost == 0);                           /* wrap is not a gap */
    const char *g[] = {"seq=65535", "seq=2"};
    assert(run(g, 2)->lost == 2);                           /* 0, 1 lost across wrap */
    const char *r[] = {"seq=1", "seq=4", "seq=2"};
    seqlog_t *t = run(r, 3);
    assert(t->lost == 1 && t->reordered == 1);              /* 2 came late */
    const char *d[] = {"seq=1", "seq=2", "seq=2"};
    assert(run(d, 3)->dup == 1);
    const char *b[] = {"t=0 seq=1", "garbage", "seq=x", "seq=70000", "seq=2"};
    t = run(b, 5);
    assert(t->bad == 3 && t->lost == 0);
    puts("t5 OK");
    return 0;
}
```

**말할 한 줄**: "I take the difference mod 2^16 and use half the range to decide forward gap versus old packet."

## 7. T6 arm / failsafe 상태 머신

> **우선순위 A · 15분** · 파일 `code/typealong/t6_fsm.py`

**면접관 문장**

> "The drone must only arm when throttle is low and the radio link is alive. If we stop hearing the radio for 100 ms while armed, go to a safe mode. Coming back should not flicker. Model it so I can test it."

**먼저 물을 질문**

1. "Is exactly 100 ms a timeout?"
2. "From failsafe — back to armed when the link returns, or always disarm?"
3. "Can I take time as an argument?"

**함정**: 경계 99 / 100 ms · 복구는 **연속** N 프레임 · 전이 로그 · 무장 거절 조건 둘 다

**따라 칠 코드**

```python
# T6 arm / failsafe state machine -- "arm only if throttle low and link alive; 100 ms no radio -> safe mode; no flicker"
DISARMED, ARMED, FAILSAFE = "DISARMED", "ARMED", "FAILSAFE"


class Mode:
    def __init__(self, rx_timeout=100, recover=5, land_after=3000, thr_low=50):
        self.rx_timeout, self.recover = rx_timeout, recover   # all times: integer ms
        self.land_after, self.thr_low = land_after, thr_low
        self.state = DISARMED
        self.last_rx = None
        self.good = 0
        self.fs_since = None
        self.log = []                               # (t, from, to, why): the test artifact

    def _go(self, t, new, why):
        self.log.append((t, self.state, new, why))
        self.state = new

    def link_ok(self, t):
        return self.last_rx is not None and t - self.last_rx < self.rx_timeout

    def on_rx(self, t):
        if self.state == FAILSAFE:
            if not self.link_ok(t):
                self.good = 0                       # gap breaks the "in a row" count
            self.good += 1
            if self.good >= self.recover:
                self._go(t, ARMED, "link back")
        self.last_rx = t

    def arm(self, t, throttle):
        if self.state == DISARMED and throttle <= self.thr_low and self.link_ok(t):
            self._go(t, ARMED, "arm")
        return self.state == ARMED

    def tick(self, t):
        if self.state == ARMED and not self.link_ok(t):
            self.good, self.fs_since = 0, t
            self._go(t, FAILSAFE, "rx timeout")
        elif self.state == FAILSAFE and t - self.fs_since >= self.land_after:
            self._go(t, DISARMED, "landed")


if __name__ == "__main__":
    m = Mode()
    assert not m.arm(0, 0)                          # no link yet
    m.on_rx(0)
    assert not m.arm(10, 900)                       # throttle high
    assert m.arm(10, 0)
    m.tick(99); assert m.state == ARMED             # 99 ms: still fine
    m.tick(100); assert m.state == FAILSAFE         # exactly 100 ms -> failsafe
    for t in (110, 120, 130, 140):
        m.on_rx(t)
    assert m.state == FAILSAFE                      # 4 frames: not yet
    m.on_rx(150); assert m.state == ARMED           # 5 in a row -> back
    m.tick(400); m.tick(3400)
    assert m.state == DISARMED and m.log[-1][3] == "landed"
    print("t6 OK")
```

**C 버전** — `code/typealong/t6_fsm.c` · C에서 다른 점: `enum` 상태 · 시간은 `uint32_t` ms (빼기는 wrap-safe) · 전이 로그는 고정 배열

```c
/* T6 arm / failsafe state machine -- "arm only if throttle low and link alive; 100 ms no radio -> safe; no flicker" */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX_TIMEOUT_MS 100u
#define RECOVER_FRAMES 5u
#define LAND_AFTER_MS 3000u
#define THROTTLE_LOW 50u
#define LOG_MAX 16u

typedef enum { DISARMED, ARMED, FAILSAFE } state_t;

typedef struct { uint32_t t; state_t from, to; const char *why; } transition_t;

typedef struct {
    state_t state;
    bool have_rx;
    uint32_t last_rx, fs_since;
    unsigned good;
    transition_t log[LOG_MAX];   /* the test artifact */
    unsigned nlog;
} mode_t_;

static void go(mode_t_ *m, uint32_t t, state_t to, const char *why)
{
    if (m->nlog < LOG_MAX)
        m->log[m->nlog++] = (transition_t){t, m->state, to, why};
    m->state = to;
}

static bool link_ok(const mode_t_ *m, uint32_t t)
{
    return m->have_rx && t - m->last_rx < RX_TIMEOUT_MS;
}

void on_rx(mode_t_ *m, uint32_t t)
{
    if (m->state == FAILSAFE) {
        if (!link_ok(m, t))
            m->good = 0;                       /* gap breaks the "in a row" count */
        if (++m->good >= RECOVER_FRAMES)
            go(m, t, ARMED, "link back");
    }
    m->have_rx = true;
    m->last_rx = t;
}

bool request_arm(mode_t_ *m, uint32_t t, unsigned throttle)
{
    if (m->state == DISARMED && throttle <= THROTTLE_LOW && link_ok(m, t))
        go(m, t, ARMED, "arm");
    return m->state == ARMED;
}

void tick(mode_t_ *m, uint32_t t)
{
    if (m->state == ARMED && !link_ok(m, t)) {
        m->good = 0;
        m->fs_since = t;
        go(m, t, FAILSAFE, "rx timeout");
    } else if (m->state == FAILSAFE && t - m->fs_since >= LAND_AFTER_MS) {
        go(m, t, DISARMED, "landed");
    }
}

int main(void)
{
    mode_t_ m = {0};
    assert(!request_arm(&m, 0, 0));                         /* no link yet */
    on_rx(&m, 0);
    assert(!request_arm(&m, 10, 900));                      /* throttle high */
    assert(request_arm(&m, 10, 0));
    tick(&m, 99);  assert(m.state == ARMED);                /* 99 ms: still fine */
    tick(&m, 100); assert(m.state == FAILSAFE);             /* exactly 100 ms -> failsafe */
    for (uint32_t t = 110; t <= 140; t += 10)
        on_rx(&m, t);
    assert(m.state == FAILSAFE);                            /* 4 frames: not yet */
    on_rx(&m, 150); assert(m.state == ARMED);               /* 5 in a row -> back */
    tick(&m, 400); tick(&m, 3400);
    assert(m.state == DISARMED && strcmp(m.log[m.nlog - 1].why, "landed") == 0);
    puts("t6 OK");
    return 0;
}
```

**말할 한 줄**: "Every transition goes through one function that logs time, from, to and reason — that log is what the tests check."

## 8. T7 바이너리 레코드 + CRC-8

> **우선순위 B · 15분** · 파일 `code/typealong/t7_struct_crc.py`

**면접관 문장**

> "The flight controller dumps 12-byte records: seq u16, three int16 accel values in milli-g, battery millivolts u16, a flags byte, and a CRC-8 (poly 0x07, init 0) over the first 11 bytes. Decode a dump and flag anything suspicious."

**먼저 물을 질문**

1. "Little-endian?"
2. "What range is a valid battery voltage?"
3. "Partial record at the end — report or ignore?"

**함정**: `struct.Struct("<HhhhHB")` · `crc8(b"123456789") == 0xF4` · 남는 바이트 보고 · 모르는 flag 비트

**따라 칠 코드**

```python
# T7 binary record -- "seq u16, 3x int16 accel mg, vbat mV u16, flags u8, CRC-8 (poly 0x07, init 0) over 11 bytes"
import struct

REC = struct.Struct("<HhhhHB")                      # 11 bytes, little-endian
SIZE = REC.size + 1


def crc8(data, poly=0x07):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def decode(blob):
    recs, problems = [], []
    if len(blob) % SIZE:
        problems.append((len(blob) // SIZE, "trailing bytes"))
    for i in range(len(blob) // SIZE):
        raw = blob[i * SIZE:(i + 1) * SIZE]
        if crc8(raw[:-1]) != raw[-1]:
            problems.append((i, "crc"))
            continue
        seq, ax, ay, az, mv, flags = REC.unpack(raw[:-1])
        if not 9000 <= mv <= 26000:
            problems.append((i, "vbat range"))
        if flags & ~0x07:
            problems.append((i, "unknown flags"))
        recs.append({"seq": seq, "accel": (ax, ay, az), "mv": mv, "armed": bool(flags & 1)})
    return recs, problems


def make(seq, ax, ay, az, mv, flags):
    body = REC.pack(seq, ax, ay, az, mv, flags)
    return body + bytes([crc8(body)])


if __name__ == "__main__":
    assert crc8(b"123456789") == 0xF4                    # standard CRC-8 check value
    good = make(1, 0, 0, 1000, 16800, 1)
    recs, probs = decode(good)
    assert probs == [] and recs[0]["accel"] == (0, 0, 1000) and recs[0]["armed"]
    bad = bytearray(make(2, -5, 0, 0, 16800, 0)); bad[3] ^= 1
    assert decode(bytes(bad))[1] == [(0, "crc")]
    assert decode(make(3, 0, 0, 0, 5000, 0x80))[1] == [(0, "vbat range"), (0, "unknown flags")]
    assert decode(good + b"\x00")[1] == [(1, "trailing bytes")]
    print("t7 OK")
```

**C 버전** — `code/typealong/t7_struct_crc.c` · C에서 다른 점: 구조체에 memcpy 금지 (패딩 · 엔디안) → `le16()`로 직접 조립 · `(int16_t)` 캐스트로 부호 유지

```c
/* T7 binary record -- "seq u16, 3x int16 accel mg, vbat mV u16, flags u8, CRC-8 (poly 0x07, init 0) over 11 bytes" */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BODY 11
#define SIZE 12

typedef struct {
    uint16_t seq;
    int16_t accel[3];
    uint16_t mv;
    uint8_t flags;
} rec_t;

enum { P_OK = 0, P_CRC = 1, P_VBAT = 2, P_FLAGS = 4 };

uint8_t crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

/* little-endian by hand: never memcpy the bytes onto a struct (padding, host endianness) */
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int decode_one(const uint8_t *raw, rec_t *r)
{
    if (crc8(raw, BODY) != raw[BODY])
        return P_CRC;                          /* don't trust any field */
    r->seq = le16(raw);
    for (int i = 0; i < 3; i++)
        r->accel[i] = (int16_t)le16(raw + 2 + 2 * i);
    r->mv = le16(raw + 8);
    r->flags = raw[10];
    int p = P_OK;
    if (r->mv < 9000 || r->mv > 26000)
        p |= P_VBAT;
    if (r->flags & ~0x07)
        p |= P_FLAGS;
    return p;
}

static void make(uint8_t *out, uint16_t seq, int16_t ax, int16_t ay, int16_t az, uint16_t mv, uint8_t fl)
{
    uint16_t v[5] = {seq, (uint16_t)ax, (uint16_t)ay, (uint16_t)az, mv};
    for (int i = 0; i < 5; i++) {
        out[2 * i] = (uint8_t)(v[i] & 0xFF);
        out[2 * i + 1] = (uint8_t)(v[i] >> 8);
    }
    out[10] = fl;
    out[BODY] = crc8(out, BODY);
}

int main(void)
{
    assert(crc8((const uint8_t *)"123456789", 9) == 0xF4);  /* standard CRC-8 check value */
    uint8_t raw[SIZE];
    rec_t r;
    make(raw, 1, 0, -5, 1000, 16800, 1);
    assert(decode_one(raw, &r) == P_OK);
    assert(r.accel[1] == -5 && r.accel[2] == 1000 && (r.flags & 1));   /* sign kept */
    raw[3] ^= 1;
    assert(decode_one(raw, &r) == P_CRC);
    make(raw, 3, 0, 0, 0, 5000, 0x80);
    assert(decode_one(raw, &r) == (P_VBAT | P_FLAGS));
    size_t blob_len = 25;                                    /* 2 records + 1 trailing byte */
    assert(blob_len / SIZE == 2 && blob_len % SIZE == 1);   /* report the trailing byte */
    puts("t7 OK");
    return 0;
}
```

**말할 한 줄**: "I check the CRC before trusting any field, and I verify my CRC with the standard check string first."

## 9. 실행 · 확인

```text
cd onsiteKit/code/typealong
for f in t*.py; do python3 $f; done      # t1 OK ... t7 OK
./run_c.sh                               # C: build with sanitizers, t1 OK ... t7 OK
```

## 10. 관련 페이지

[M2 코딩](../modules/2026-10-12_M2_coding_python.md) · [Q02 overflow](../qa/2026-10-20_Q02_screen_missed_overflow_case.md) · [K02 압축 정리](2026-10-20_K02_final_cram_three_1on1s.md) · [C01](../coding/2026-10-12_C01_packet_stream.md) · [C02](../coding/2026-10-12_C02_fixed_fifo.md) · [C04](../coding/2026-10-12_C04_retry_queue.md)
