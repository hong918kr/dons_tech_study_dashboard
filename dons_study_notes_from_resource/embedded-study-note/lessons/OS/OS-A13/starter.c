/* BUILD: -pthread */
/* OS-A13 미니 RPC 계층: 요청/응답·타임아웃·취소·스트리밍
 *
 * Unix 스트림 소켓 위의 작은 RPC. 한 파일에 코덱·클라이언트·서버·테스트가 다 있다.
 *   프레임 = u32 길이(LE, 길이 필드 뒤 바이트 수)
 *          + 헤더 10 B {u8 version, u8 type, u16 method, u32 req_id, u16 flags}  (전부 little-endian)
 *          + TLV 페이로드: {u16 tag, u32 len, value[len]} 반복. 모르는 tag 는 건너뛴다.
 *
 *   Part A  코덱(tlv_next, frame_parse) + 부분 읽기/쓰기(xp_read_full, xp_write_all)
 *   Part B  클라이언트 다중화: reader 스레드 1 개 + pending 표(req_id → slot + condvar),
 *           타임아웃(wait_slot), 응답 배달(client_dispatch), 연결 끊김 → 전부 실패(client_fail_all)
 *   Part C  서버 스트리밍(server_do_count) + 취소(server_on_cancel, rpc_stream_cancel, rpc_stream_next),
 *           서버가 소켓을 닫음 / 서버 프로세스 SIGKILL → 대기 중인 모든 호출이 바로 오류로 끝난다
 *
 * 체크는 항상 성립하는 것만 본다 (결과 값, 오류 코드, 개수). 시간 숫자는 보지 않는다.
 * 소켓 이름: /tmp/OS-A13_work/rpc.<pid>.sock — connect 직후 unlink.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ===================================================================== 프로토콜 상수 */
#define RPC_LEN_LEN   4u
#define RPC_HDR_LEN   10u
#define RPC_MAX_FRAME 16384u                     /* 헤더+페이로드 상한: 길이 필드를 이보다 크게 믿지 않는다 */
#define RPC_BUF       (RPC_LEN_LEN + RPC_MAX_FRAME)

enum { T_REQ = 1, T_RESP = 2, T_ERR = 3, T_STREAM_ITEM = 4, T_STREAM_END = 5, T_CANCEL = 6 };
enum { M_ECHO = 1, M_COUNT = 2, M_GATE = 3, M_PING = 4 };
enum {                                           /* TLV tag: 한 번 쓴 번호는 의미를 바꾸지 않는다 */
    TAG_VALUE = 1, TAG_DELAY_MS = 2, TAG_COUNT = 3, TAG_START = 4, TAG_INDEX = 5, TAG_ERRCODE = 6,
    TAG_SERVER_INFO = 100,                       /* v2 추가: 서버 → 클라이언트 문자열 */
    TAG_TRACE_ID = 101,                          /* v2 추가: 클라이언트 → 서버 u64, v2 서버가 되돌려 준다 */
};
/* 선로 위 상태 코드: errno 숫자를 그대로 싣지 않는다 (ECANCELED 는 macOS 89, Linux 125). gRPC 코드 번호를 빌림 */
enum { ST_CANCELLED = 1, ST_RESOURCE_EXHAUSTED = 8, ST_UNIMPLEMENTED = 12 };

static int status_to_errno(uint32_t st) {
    switch (st) {
    case ST_CANCELLED:          return -ECANCELED;
    case ST_RESOURCE_EXHAUSTED: return -EBUSY;
    case ST_UNIMPLEMENTED:      return -ENOSYS;
    default:                    return -EIO;
    }
}

/* ===================================================================== 바이트 순서 (LE 고정) */
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

/* ===================================================================== 시간 */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
/* macOS 에는 pthread_condattr_setclock 이 없다 → timedwait 는 CLOCK_REALTIME 절대 시각만 받는다.
 * 그래서 마감은 MONOTONIC 으로 재고, 매 대기마다 "남은 시간"을 REALTIME 절대 시각으로 바꾼다. */
static struct timespec rt_after(uint64_t ns) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t nsec = (uint64_t)ts.tv_nsec + ns % 1000000000ull;
    ts.tv_sec += (time_t)(ns / 1000000000ull + nsec / 1000000000ull);
    ts.tv_nsec = (long)(nsec % 1000000000ull);
    return ts;
}
static void sleep_ms(unsigned ms) {
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

/* ===================================================================== 스레드 (살아 있는 수를 센다) */
static atomic_int g_threads_live;
typedef struct { void *(*fn)(void *); void *arg; } tramp_t;
static void *tramp(void *p) {
    tramp_t t = *(tramp_t *)p;
    free(p);
    void *r = t.fn(t.arg);
    atomic_fetch_sub(&g_threads_live, 1);
    return r;
}
static int spawn(pthread_t *th, void *(*fn)(void *), void *arg) {
    tramp_t *t = malloc(sizeof *t);
    if (!t) return -1;
    t->fn = fn; t->arg = arg;
    atomic_fetch_add(&g_threads_live, 1);
    if (pthread_create(th, NULL, tramp, t) != 0) { atomic_fetch_sub(&g_threads_live, 1); free(t); return -1; }
    return 0;
}

/* ===================================================================== TLV 쓰기 (주어짐) */
typedef struct { uint8_t *p; uint32_t cap, n; bool overflow; } tlvw_t;
static void tlvw_put(tlvw_t *w, uint16_t tag, const void *v, uint32_t len) {
    if (w->overflow || len > w->cap || w->cap - w->n < 6 + len) { w->overflow = true; return; }
    put16(w->p + w->n, tag);
    put32(w->p + w->n + 2, len);
    if (len) memcpy(w->p + w->n + 6, v, len);
    w->n += 6 + len;
}
static void tlvw_u32(tlvw_t *w, uint16_t tag, uint32_t v) { uint8_t b[4]; put32(b, v); tlvw_put(w, tag, b, 4); }
static void tlvw_u64(tlvw_t *w, uint16_t tag, uint64_t v) { uint8_t b[8]; put64(b, v); tlvw_put(w, tag, b, 8); }

/* ===================================================================== Part A: 코덱 */
typedef struct {
    uint8_t version, type;
    uint16_t method, flags;
    uint32_t req_id;
    const uint8_t *payload;                      /* 입력 버퍼 안을 가리킨다 (복사 안 함) */
    uint32_t plen;
} frame_t;

typedef struct { const uint8_t *p; uint32_t n, off; } tlv_it_t;
typedef struct { uint16_t tag; uint32_t len; const uint8_t *val; } tlv_t;

static void tlv_begin(tlv_it_t *it, const uint8_t *p, uint32_t n) { it->p = p; it->n = n; it->off = 0; }

/* TODO A1: 다음 TLV 하나. 1 = 하나 꺼냄, 0 = 정확히 끝, -EBADMSG = 잘린 머리 / 길이가 남은 바이트를 넘음.
 * 오류 뒤에 다시 불러도 계속 오류여야 한다 (off 를 움직이지 않는다). off + 6 + len 을 그대로 더하면
 * len = 0xFFFFFFFF 에서 u32 가 넘친다 → "남은 바이트"와 비교한다. */
static int tlv_next(tlv_it_t *it, tlv_t *t) {
    (void)it; (void)t; (void)get16;
    return 0;                                    /* TODO: 지금은 "TLV 없음" */
}

/* TODO A2: buf[0..n) 앞에서 프레임 하나를 해석한다.
 *   > 0       : 완전한 프레임, 소비한 바이트 수 (4 + 길이)
 *   0         : 아직 모자람 (더 읽어야 함)
 *   -EBADMSG  : 길이 < 10, 길이 > RPC_MAX_FRAME, version == 0, TLV 가 깨짐
 * 길이 범위는 몸통이 다 오기 전에 검사한다 (쓰레기 길이를 믿고 기다리거나 할당하지 않는다).
 * 모르는 type 과 모르는 tag 는 오류가 아니다 (상위가 버린다 → 앞으로의 확장을 위한 자리). */
static int frame_parse(const uint8_t *buf, size_t n, frame_t *f) {
    (void)buf; (void)n; (void)f;
    return -EBADMSG;                             /* TODO */
}

/* 프레임 만들기 (주어짐). 반환: 전체 바이트 수, 0 = 너무 큼 */
static size_t frame_build(uint8_t *out, size_t cap, uint8_t ver, uint8_t type, uint16_t method,
                          uint32_t id, uint16_t flags, const uint8_t *pl, uint32_t plen) {
    if (plen > RPC_MAX_FRAME - RPC_HDR_LEN) return 0;
    size_t total = RPC_LEN_LEN + RPC_HDR_LEN + (size_t)plen;
    if (total > cap) return 0;
    put32(out, RPC_HDR_LEN + plen);
    out[4] = ver;
    out[5] = type;
    put16(out + 6, method);
    put32(out + 8, id);
    put16(out + 12, flags);
    if (plen) memcpy(out + 14, pl, plen);
    return total;
}

/* 필드 하나 찾기 (주어짐): 1 = 찾음, 0 = 없음 (길이가 4 가 아니면 없음 취급) */
static int tlv_get_u32(const uint8_t *p, uint32_t n, uint16_t tag, uint32_t *out) {
    tlv_it_t it;
    tlv_t t;
    tlv_begin(&it, p, n);
    while (tlv_next(&it, &t) == 1)
        if (t.tag == tag && t.len == 4) { *out = get32(t.val); return 1; }
    return 0;
}

/* ===================================================================== Part A: 전송 (부분 읽기/쓰기) */
typedef struct {
    int fd;
    size_t max_chunk;                            /* 0 = 제한 없음, 1 = 한 번에 1 바이트만 (시험용 shim) */
    atomic_uint reads, writes;                   /* read()/write() 호출 횟수 */
} xport_t;

static ssize_t xp_read1(xport_t *x, void *b, size_t n) {
    if (x->max_chunk && n > x->max_chunk) n = x->max_chunk;
    atomic_fetch_add(&x->reads, 1);
    return read(x->fd, b, n);
}
static ssize_t xp_write1(xport_t *x, const void *b, size_t n) {
    if (x->max_chunk && n > x->max_chunk) n = x->max_chunk;
    atomic_fetch_add(&x->writes, 1);
    return write(x->fd, b, n);
}

/* TODO A3: 정확히 n 바이트를 읽는다. read 는 요청보다 적게 돌려줄 수 있다 (부분 읽기).
 *   1 = 다 읽음, 0 = 첫 바이트 전에 EOF (프레임 경계에서 깔끔한 종료),
 *   -ECONNRESET = 중간에 EOF (프레임이 잘림), -errno = 오류. EINTR 은 다시 시도. */
static int xp_read_full(xport_t *x, void *buf, size_t n) {
    (void)x; (void)buf; (void)n; (void)xp_read1;
    return -EIO;                                 /* TODO */
}

/* TODO A4: n 바이트를 전부 쓴다 (부분 쓰기 + EINTR). 0 = 성공, -errno = 실패 (EPIPE 등). */
static int xp_write_all(xport_t *x, const void *buf, size_t n) {
    (void)x; (void)buf; (void)n; (void)xp_write1;
    return -EIO;                                 /* TODO */
}

/* 프레임 하나 읽기 (주어짐): 1 = 프레임, 0 = 경계에서 EOF, <0 = 오류. 길이를 믿기 전에 범위부터 본다. */
static int xp_read_frame(xport_t *x, uint8_t *buf, size_t cap, frame_t *f) {
    int r = xp_read_full(x, buf, RPC_LEN_LEN);
    if (r <= 0) return r;
    uint32_t len = get32(buf);
    if (len < RPC_HDR_LEN || len > RPC_MAX_FRAME || (size_t)len + RPC_LEN_LEN > cap) return -EBADMSG;
    r = xp_read_full(x, buf + RPC_LEN_LEN, len);
    if (r == 0) return -ECONNRESET;
    if (r < 0) return r;
    return frame_parse(buf, RPC_LEN_LEN + (size_t)len, f) > 0 ? 1 : -EBADMSG;
}

/* 프레임 하나 보내기 (주어짐). 여러 스레드가 같은 소켓에 쓰므로 프레임 하나를 통째로 락 안에서 쓴다. */
static int send_frame(xport_t *x, pthread_mutex_t *wmu, uint8_t ver, uint8_t type, uint16_t method,
                      uint32_t id, const uint8_t *pl, uint32_t plen) {
    uint8_t buf[RPC_BUF];
    size_t n = frame_build(buf, sizeof buf, ver, type, method, id, 0, pl, plen);
    if (!n) return -EMSGSIZE;
    pthread_mutex_lock(wmu);
    int r = xp_write_all(x, buf, n);
    pthread_mutex_unlock(wmu);
    return r;
}

static void sock_nosigpipe(int fd) {           /* macOS: EPIPE 를 시그널 대신 오류로 받는다 */
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
}

/* ===================================================================== Part B: 클라이언트 */
#define MAX_PENDING 64
#define STREAM_QMAX 256
#define ITEM_MAX    256

typedef struct item { struct item *next; uint32_t len; uint8_t data[]; } item_t;

typedef struct {
    bool used, is_stream, done, cancel_sent;
    uint32_t req_id;
    int status;                                  /* done 일 때: 0 = 정상, <0 = 오류 */
    pthread_cond_t cv;                           /* 이 호출을 기다리는 스레드만 깨운다 */
    uint8_t *resp;                               /* 단항 호출: 호출자의 버퍼 (호출자가 slot 을 쥐는 동안만 유효) */
    uint32_t resp_cap, resp_len;
    item_t *qh, *qt;                             /* 스트림: 받은 항목 큐 */
    uint32_t qn, end_count, after_cancel;
} slot_t;

typedef struct {
    xport_t x;
    uint8_t version;                             /* 이 클라이언트가 말하는 프로토콜 버전 (1 또는 2) */
    pthread_mutex_t mu;                          /* slot 표, next_id, dead, 통계 */
    pthread_mutex_t wmu;                         /* 프레임 쓰기 직렬화 */
    slot_t slot[MAX_PENDING];
    uint32_t next_id;
    int dead;                                    /* 0 = 살아 있음, <0 = 연결이 죽은 이유 */
    pthread_t reader;
    uint8_t peer_version;
    uint32_t n_pending, max_pending, late_drops, unknown_type, sent_reqs;
} rpc_client_t;

static slot_t *slot_find(rpc_client_t *c, uint32_t id) {
    for (int i = 0; i < MAX_PENDING; i++)
        if (c->slot[i].used && c->slot[i].req_id == id) return &c->slot[i];
    return NULL;
}
static slot_t *slot_alloc(rpc_client_t *c, bool is_stream) {   /* mu 를 쥔 채 호출 */
    slot_t *s = NULL;
    for (int i = 0; i < MAX_PENDING && !s; i++)
        if (!c->slot[i].used) s = &c->slot[i];
    if (!s) return NULL;
    do { c->next_id++; } while (c->next_id == 0 || slot_find(c, c->next_id));   /* 0 은 예약, 쓰는 중인 id 는 건너뜀 */
    s->used = true; s->is_stream = is_stream; s->done = false; s->cancel_sent = false;
    s->req_id = c->next_id; s->status = 0;
    s->resp = NULL; s->resp_cap = s->resp_len = 0;
    s->qh = s->qt = NULL; s->qn = s->end_count = s->after_cancel = 0;
    if (++c->n_pending > c->max_pending) c->max_pending = c->n_pending;
    return s;
}
static void slot_release(rpc_client_t *c, slot_t *s) {        /* mu 를 쥔 채 호출 */
    while (s->qh) { item_t *n = s->qh->next; free(s->qh); s->qh = n; }
    s->qt = NULL; s->qn = 0;
    s->used = false;
    c->n_pending--;
}
static void finish(slot_t *s, int status) {
    s->status = status;
    s->done = true;
    pthread_cond_signal(&s->cv);
}

/* TODO B1: mu 를 쥔 채 호출. s->done 이 되거나 (want_item 이면) 큐에 항목이 생길 때까지 기다린다.
 * 0 = 조건 성립, -ETIMEDOUT = 마감 지남. 가짜 깨움(spurious wakeup)이 있으니 while 로 다시 본다.
 * 마감은 MONOTONIC 으로 한 번 정하고, 매 대기는 rt_after(남은 시간) 으로 (벽시계가 바뀌어도 안전). */
static int wait_slot(rpc_client_t *c, slot_t *s, bool want_item, int timeout_ms) {
    (void)c; (void)s; (void)want_item; (void)timeout_ms;
    return -ETIMEDOUT;                           /* TODO: 기다리지 않고 바로 타임아웃 */
}

/* TODO B2: reader 스레드가 받은 프레임 하나를 주인 slot 에 배달한다 (mu 안에서).
 *   - 모르는 type (RESP/ERR/STREAM_ITEM/STREAM_END 가 아님) → unknown_type++ 하고 버린다
 *   - req_id 의 slot 이 없거나 이미 done → late_drops++ (타임아웃으로 지운 호출의 늦은 응답)
 *   - RESP: 단항이면 payload 를 s->resp 로 복사 (cap 초과 → -EMSGSIZE), finish(s, 0)
 *   - ERR : TAG_ERRCODE → status_to_errno → finish
 *   - STREAM_ITEM: cancel_sent 면 after_cancel++ 하고 버림. 큐가 STREAM_QMAX 거나 plen > ITEM_MAX 면
 *     finish(-ENOBUFS) (reader 는 절대 막히면 안 된다). 아니면 malloc 해서 큐 끝에 넣고 signal.
 *   - STREAM_END: TAG_COUNT → end_count, finish(s, 0)
 *   - 종류가 안 맞으면 (단항에 ITEM, 스트림에 RESP) finish(-EPROTO)
 *   - peer_version = f->version 기록 */
static void client_dispatch(rpc_client_t *c, const frame_t *f) {
    (void)c; (void)f;
    (void)finish; (void)status_to_errno;         /* TODO: 쓸 도구들 */
}

/* TODO B3: 연결이 죽었다 (EOF, ECONNRESET, EPIPE, 깨진 프레임). 이후 호출은 즉시 err 로 끝나야 하고,
 * 지금 기다리는 모든 호출(done 아닌 slot)도 err 로 끝내고 깨운다. 첫 원인만 기록한다. */
static void client_fail_all(rpc_client_t *c, int err) {
    (void)c; (void)err;                          /* TODO */
}

static void *client_reader(void *arg) {         /* 소켓을 읽는 유일한 스레드 */
    rpc_client_t *c = arg;
    uint8_t buf[RPC_BUF];
    for (;;) {
        frame_t f;
        int r = xp_read_frame(&c->x, buf, sizeof buf, &f);
        if (r <= 0) {                            /* 깨진 프레임 뒤에서는 다음 프레임 경계를 알 수 없다 → 연결을 버린다 */
            client_fail_all(c, r == -EBADMSG ? -EPROTO : -ECONNRESET);
            return NULL;
        }
        client_dispatch(c, &f);
    }
}

static rpc_client_t *rpc_client_new(int fd, uint8_t version, size_t max_chunk) {
    rpc_client_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->x.fd = fd;
    c->x.max_chunk = max_chunk;
    c->version = version;
    pthread_mutex_init(&c->mu, NULL);
    pthread_mutex_init(&c->wmu, NULL);
    for (int i = 0; i < MAX_PENDING; i++) pthread_cond_init(&c->slot[i].cv, NULL);
    sock_nosigpipe(fd);
    if (spawn(&c->reader, client_reader, c) != 0) { close(fd); free(c); return NULL; }
    return c;
}

/* 모든 호출 스레드가 끝난 뒤 부른다: 소켓을 내려 reader 를 깨우고 join, fd 닫기 */
static void rpc_client_free(rpc_client_t *c) {
    shutdown(c->x.fd, SHUT_RDWR);
    pthread_join(c->reader, NULL);
    close(c->x.fd);
    for (int i = 0; i < MAX_PENDING; i++) {
        if (c->slot[i].used) slot_release(c, &c->slot[i]);
        pthread_cond_destroy(&c->slot[i].cv);
    }
    pthread_mutex_destroy(&c->mu);
    pthread_mutex_destroy(&c->wmu);
    free(c);
}

static void client_write_failed(rpc_client_t *c) {
    shutdown(c->x.fd, SHUT_RDWR);               /* 프레임이 반만 나갔을 수 있다 → 스트림을 더 믿을 수 없다 */
    client_fail_all(c, -ECONNRESET);
}

/* 단항 호출 (주어짐): 0 = 성공, <0 = -ETIMEDOUT / -ECONNRESET / -EPROTO / 서버 오류(-ENOSYS 등) */
static int rpc_call(rpc_client_t *c, uint16_t method, const uint8_t *req, uint32_t req_len,
                    uint8_t *resp, uint32_t resp_cap, uint32_t *resp_len, int timeout_ms) {
    pthread_mutex_lock(&c->mu);
    if (c->dead) { int e = c->dead; pthread_mutex_unlock(&c->mu); return e; }
    slot_t *s = slot_alloc(c, false);            /* 보내기 "전에" 등록: 응답이 아무리 빨라도 받을 자리가 있다 */
    if (!s) { pthread_mutex_unlock(&c->mu); return -EAGAIN; }
    s->resp = resp;
    s->resp_cap = resp_cap;
    uint32_t id = s->req_id;
    c->sent_reqs++;
    pthread_mutex_unlock(&c->mu);

    if (send_frame(&c->x, &c->wmu, c->version, T_REQ, method, id, req, req_len) < 0) client_write_failed(c);

    pthread_mutex_lock(&c->mu);
    int r = wait_slot(c, s, false, timeout_ms);
    if (r == 0) r = s->status;
    if (r == 0 && resp_len) *resp_len = s->resp_len;
    slot_release(c, s);                          /* 타임아웃이어도 여기서 지운다 → 늦은 응답은 late_drops */
    pthread_mutex_unlock(&c->mu);
    return r;
}

/* 스트림 열기 (주어짐): *sid = req_id. 항목은 rpc_stream_next 로, 끝나면 rpc_stream_close. */
static int rpc_stream_open(rpc_client_t *c, uint16_t method, const uint8_t *req, uint32_t len, uint32_t *sid) {
    pthread_mutex_lock(&c->mu);
    if (c->dead) { int e = c->dead; pthread_mutex_unlock(&c->mu); return e; }
    slot_t *s = slot_alloc(c, true);
    if (!s) { pthread_mutex_unlock(&c->mu); return -EAGAIN; }
    *sid = s->req_id;
    c->sent_reqs++;
    pthread_mutex_unlock(&c->mu);
    if (send_frame(&c->x, &c->wmu, c->version, T_REQ, method, *sid, req, len) < 0) client_write_failed(c);
    return 0;
}

/* TODO C1: 다음 스트림 항목. 1 = 항목 (out 에 복사, *len), 0 = STREAM_END, <0 = 오류/-ETIMEDOUT/-EINVAL.
 * 순서가 중요하다: done 이어도 큐에 남은 항목을 먼저 다 내준 뒤에 끝(0) 또는 오류를 알린다. */
static int rpc_stream_next(rpc_client_t *c, uint32_t sid, uint8_t *out, uint32_t cap, uint32_t *len,
                           int timeout_ms) {
    (void)c; (void)sid; (void)out; (void)cap; (void)len; (void)timeout_ms;
    return -ENOSYS;                              /* TODO */
}

/* TODO C2: 스트림 취소. 이미 done 이면 0. 아니면 cancel_sent 표시 + 큐 비우기 → CANCEL 프레임 전송 →
 * 서버의 마지막 프레임(ERR CANCELLED 또는 경주에서 이긴 STREAM_END)을 timeout 안에서 기다린다.
 * 0 = 서버가 멈췄음을 확인, <0 = -ETIMEDOUT / 연결 오류. 표시를 "보내기 전에" 해야 그 사이 도착한 항목을 센다. */
static int rpc_stream_cancel(rpc_client_t *c, uint32_t sid, int timeout_ms) {
    (void)c; (void)sid; (void)timeout_ms;
    return -ENOSYS;                              /* TODO */
}

static void rpc_stream_close(rpc_client_t *c, uint32_t sid) {
    pthread_mutex_lock(&c->mu);
    slot_t *s = slot_find(c, sid);
    if (s) slot_release(c, s);
    pthread_mutex_unlock(&c->mu);
}

/* ===================================================================== Part C: 서버 */
#define MAX_ACTIVE  64
#define MAX_WORKERS 16

typedef struct job { struct job *next; uint16_t method; uint32_t req_id, plen; uint8_t payload[]; } job_t;
typedef struct { bool used, cancelled; uint32_t req_id; } active_t;

typedef struct {
    xport_t x;
    uint8_t version;
    int nworkers;
    pthread_t reader, workers[MAX_WORKERS];
    pthread_mutex_t mu;                          /* 큐, active 표, 게이트, 통계 */
    pthread_mutex_t wmu;
    pthread_cond_t cv;                           /* 취소 / 종료 / 게이트 열림 (server_sleep, GATE 가 기다림) */
    pthread_cond_t qcv;                          /* 일감 도착 (쉬는 워커가 기다림) — cv 와 나눠야 signal 로 충분 */
    job_t *qh, *qt;
    bool closing, gate_open;
    active_t act[MAX_ACTIVE];
    uint32_t reqs, cancels_rcvd, cancel_unknown, stopped_by_cancel, items_sent, last_stream_items;
    uint32_t unknown_tlv, unknown_type, gate_waiting, n_active, inversions, last_done;
} rpc_server_t;

static active_t *act_find(rpc_server_t *s, uint32_t id) {
    for (int i = 0; i < MAX_ACTIVE; i++)
        if (s->act[i].used && s->act[i].req_id == id) return &s->act[i];
    return NULL;
}

static int server_send(rpc_server_t *s, const job_t *j, uint8_t type, const uint8_t *pl, uint32_t n) {
    return send_frame(&s->x, &s->wmu, s->version, type, j->method, j->req_id, pl, n);
}
static void server_send_err(rpc_server_t *s, const job_t *j, uint32_t st) {
    uint8_t pl[16];
    tlvw_t w = { pl, sizeof pl, 0, false };
    tlvw_u32(&w, TAG_ERRCODE, st);
    server_send(s, j, T_ERR, pl, w.n);
}

/* 지연 (주어짐): ms 동안 기다리되 취소/종료면 바로 깬다. 0 = 시간 다 됨, 1 = 취소됨, 2 = 연결 종료.
 * ms == 0 이어도 취소/종료를 먼저 확인한다. */
static int server_sleep(rpc_server_t *s, uint32_t id, uint32_t ms) {
    uint64_t deadline = now_ns() + (uint64_t)ms * 1000000ull;
    int rc = 0;
    pthread_mutex_lock(&s->mu);
    active_t *a = act_find(s, id);
    for (;;) {
        if (s->closing) { rc = 2; break; }
        if (a && a->cancelled) { rc = 1; break; }
        uint64_t now = now_ns();
        if (now >= deadline) break;
        struct timespec ts = rt_after(deadline - now);
        pthread_cond_timedwait(&s->cv, &s->mu, &ts);
    }
    pthread_mutex_unlock(&s->mu);
    return rc;
}

/* TODO C3: reader 스레드가 CANCEL 프레임을 받았다. cancels_rcvd++.
 * 그 req_id 가 처리 중(active)이면 cancelled 표시 + broadcast (server_sleep 이 바로 깬다).
 * 이미 끝난 호출이면 cancel_unknown++ 만 (취소와 정상 종료의 경주는 정상이다). */
static void server_on_cancel(rpc_server_t *s, uint32_t id) {
    (void)s; (void)id;                           /* TODO */
}

/* TODO C4: 서버 스트리밍. i = 0..n-1 마다: server_sleep(delay) →
 *   2(종료) 면 그냥 끝 (보낼 곳이 없다), 1(취소) 면 ERR CANCELLED 보내고 stopped_by_cancel++,
 *   last_stream_items = i 기록 후 끝. 아니면 STREAM_ITEM{INDEX=i, VALUE=start+i} 보내고 items_sent++.
 * 다 보냈으면 STREAM_END{COUNT=n}. 보내기 실패(상대가 끊김)면 바로 끝. 통계는 mu 안에서. */
static void server_do_count(rpc_server_t *s, const job_t *j, uint32_t n, uint32_t start, uint32_t delay) {
    (void)n; (void)start; (void)delay;           /* TODO: 지금은 항목 없이 바로 END */
    uint8_t pl[32];
    tlvw_t w = { pl, sizeof pl, 0, false };
    tlvw_u32(&w, TAG_COUNT, 0);
    server_send(s, j, T_STREAM_END, pl, w.n);
}

static uint32_t echo_fn(uint32_t v) { return v * 2654435761u + 7u; }

static void server_handle(rpc_server_t *s, job_t *j) {
    uint32_t value = 0, delay = 0, count = 0, start = 0, unknown = 0;
    uint64_t trace = 0;
    bool has_trace = false;
    tlv_it_t it;
    tlv_t t;
    tlv_begin(&it, j->payload, j->plen);
    while (tlv_next(&it, &t) == 1) {
        if (t.tag == TAG_VALUE && t.len == 4) value = get32(t.val);
        else if (t.tag == TAG_DELAY_MS && t.len == 4) delay = get32(t.val);
        else if (t.tag == TAG_COUNT && t.len == 4) count = get32(t.val);
        else if (t.tag == TAG_START && t.len == 4) start = get32(t.val);
        else if (t.tag == TAG_TRACE_ID && t.len == 8 && s->version >= 2) { trace = get64(t.val); has_trace = true; }
        else unknown++;                          /* v1 서버에게 TRACE_ID 는 모르는 필드: 건너뛴다 */
    }
    uint8_t pl[128];
    tlvw_t w = { pl, sizeof pl, 0, false };
    switch (j->method) {
    case M_PING:
        server_send(s, j, T_RESP, NULL, 0);
        break;
    case M_ECHO: {
        int rc = server_sleep(s, j->req_id, delay);
        if (rc == 2) break;
        if (rc == 1) { server_send_err(s, j, ST_CANCELLED); break; }
        tlvw_u32(&w, TAG_VALUE, echo_fn(value));
        if (s->version >= 2) {
            tlvw_put(&w, TAG_SERVER_INFO, "mini-rpc/2", 10);
            if (has_trace) tlvw_u64(&w, TAG_TRACE_ID, trace);
        }
        server_send(s, j, T_RESP, pl, w.n);
        break;
    }
    case M_COUNT:
        server_do_count(s, j, count, start, delay);
        break;
    case M_GATE: {                               /* 시험 훅: 게이트가 열릴 때까지 붙잡힌다 */
        pthread_mutex_lock(&s->mu);
        s->gate_waiting++;
        while (!s->gate_open && !s->closing) pthread_cond_wait(&s->cv, &s->mu);
        s->gate_waiting--;
        bool closing = s->closing;
        pthread_mutex_unlock(&s->mu);
        if (!closing) { tlvw_u32(&w, TAG_VALUE, echo_fn(value)); server_send(s, j, T_RESP, pl, w.n); }
        break;
    }
    default:
        server_send_err(s, j, ST_UNIMPLEMENTED);
    }
    pthread_mutex_lock(&s->mu);
    s->unknown_tlv += unknown;
    active_t *a = act_find(s, j->req_id);
    if (a) { a->used = false; s->n_active--; }
    if (j->req_id < s->last_done) s->inversions++;   /* 받은 순서와 다르게 끝남 = 순서 밖 응답 */
    s->last_done = j->req_id;
    pthread_mutex_unlock(&s->mu);
}

static void *server_worker(void *arg) {
    rpc_server_t *s = arg;
    for (;;) {
        pthread_mutex_lock(&s->mu);
        while (!s->qh && !s->closing) pthread_cond_wait(&s->qcv, &s->mu);
        if (s->closing) { pthread_mutex_unlock(&s->mu); return NULL; }
        job_t *j = s->qh;
        s->qh = j->next;
        if (!s->qh) s->qt = NULL;
        pthread_mutex_unlock(&s->mu);
        server_handle(s, j);
        free(j);
    }
}

static void *server_reader(void *arg) {
    rpc_server_t *s = arg;
    uint8_t buf[RPC_BUF];
    for (;;) {
        frame_t f;
        if (xp_read_frame(&s->x, buf, sizeof buf, &f) <= 0) break;   /* EOF / 오류 / 깨진 프레임 → 연결 종료 */
        if (f.type == T_CANCEL) { server_on_cancel(s, f.req_id); continue; }
        if (f.type != T_REQ) {
            pthread_mutex_lock(&s->mu); s->unknown_type++; pthread_mutex_unlock(&s->mu);
            continue;
        }
        job_t *j = malloc(sizeof *j + f.plen);
        if (!j) break;
        j->next = NULL; j->method = f.method; j->req_id = f.req_id; j->plen = f.plen;
        if (f.plen) memcpy(j->payload, f.payload, f.plen);
        pthread_mutex_lock(&s->mu);
        active_t *a = NULL;
        for (int i = 0; i < MAX_ACTIVE && !a; i++)
            if (!s->act[i].used) a = &s->act[i];
        if (!a) {                                /* 동시 처리 상한: 바로 거절 */
            pthread_mutex_unlock(&s->mu);
            server_send_err(s, j, ST_RESOURCE_EXHAUSTED);
            free(j);
            continue;
        }
        a->used = true; a->cancelled = false; a->req_id = f.req_id;
        s->n_active++;
        s->reqs++;
        if (s->qt) s->qt->next = j; else s->qh = j;
        s->qt = j;
        pthread_cond_signal(&s->qcv);           /* 쉬는 워커 하나만 깨운다 */
        pthread_mutex_unlock(&s->mu);
    }
    pthread_mutex_lock(&s->mu);
    s->closing = true;                           /* 연결이 끝났다: 붙잡힌 워커를 전부 깨워 끝낸다 */
    pthread_cond_broadcast(&s->cv);
    pthread_cond_broadcast(&s->qcv);
    pthread_mutex_unlock(&s->mu);
    return NULL;
}

static rpc_server_t *rpc_server_start(int fd, uint8_t version, int nworkers, size_t max_chunk) {
    rpc_server_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->x.fd = fd;
    s->x.max_chunk = max_chunk;
    s->version = version;
    s->nworkers = nworkers > MAX_WORKERS ? MAX_WORKERS : nworkers;
    pthread_mutex_init(&s->mu, NULL);
    pthread_mutex_init(&s->wmu, NULL);
    pthread_cond_init(&s->cv, NULL);
    pthread_cond_init(&s->qcv, NULL);
    sock_nosigpipe(fd);
    for (int i = 0; i < s->nworkers; i++) spawn(&s->workers[i], server_worker, s);
    spawn(&s->reader, server_reader, s);
    return s;
}
static void rpc_server_drop_conn(rpc_server_t *s) { shutdown(s->x.fd, SHUT_RDWR); }   /* 장애 주입: 서버가 끊는다 */
static void rpc_server_stop(rpc_server_t *s) {
    pthread_mutex_lock(&s->mu);
    s->closing = true;
    pthread_cond_broadcast(&s->cv);
    pthread_cond_broadcast(&s->qcv);
    pthread_mutex_unlock(&s->mu);
    shutdown(s->x.fd, SHUT_RDWR);
    pthread_join(s->reader, NULL);
    for (int i = 0; i < s->nworkers; i++) pthread_join(s->workers[i], NULL);
    while (s->qh) { job_t *n = s->qh->next; free(s->qh); s->qh = n; }
    close(s->x.fd);
    pthread_cond_destroy(&s->cv);
    pthread_cond_destroy(&s->qcv);
    pthread_mutex_destroy(&s->mu);
    pthread_mutex_destroy(&s->wmu);
    free(s);
}
static uint32_t sget(rpc_server_t *s, const uint32_t *field) {
    pthread_mutex_lock(&s->mu); uint32_t v = *field; pthread_mutex_unlock(&s->mu); return v;
}
static uint32_t cget(rpc_client_t *c, const uint32_t *field) {
    pthread_mutex_lock(&c->mu); uint32_t v = *field; pthread_mutex_unlock(&c->mu); return v;
}
static bool client_dead(rpc_client_t *c) {
    pthread_mutex_lock(&c->mu); bool d = c->dead != 0; pthread_mutex_unlock(&c->mu); return d;
}

/* ===================================================================== 메서드 스텁 (클라이언트 쪽, 주어짐) */
typedef struct { uint32_t value, unknown; bool has_info, has_trace; uint64_t trace; char info[32]; } echo_res_t;

static bool client_knows(uint8_t ver, uint16_t tag) {
    switch (tag) {
    case TAG_VALUE: case TAG_COUNT: case TAG_INDEX: case TAG_ERRCODE: return true;
    case TAG_SERVER_INFO: case TAG_TRACE_ID: return ver >= 2;
    default: return false;
    }
}
static int echo_call(rpc_client_t *c, uint32_t v, uint32_t delay_ms, int timeout_ms, echo_res_t *out) {
    uint8_t req[64], resp[256];
    uint32_t rl = 0;
    tlvw_t w = { req, sizeof req, 0, false };
    tlvw_u32(&w, TAG_VALUE, v);
    if (delay_ms) tlvw_u32(&w, TAG_DELAY_MS, delay_ms);
    if (c->version >= 2) tlvw_u64(&w, TAG_TRACE_ID, 0xC0FFEE0000000000ull | v);
    int r = rpc_call(c, M_ECHO, req, w.n, resp, sizeof resp, &rl, timeout_ms);
    if (r < 0) return r;
    memset(out, 0, sizeof *out);
    tlv_it_t it;
    tlv_t t;
    bool got = false;
    tlv_begin(&it, resp, rl);
    while ((r = tlv_next(&it, &t)) == 1) {
        if (!client_knows(c->version, t.tag)) { out->unknown++; continue; }   /* 앞으로 생길 필드: 건너뜀 */
        if (t.tag == TAG_VALUE && t.len == 4) { out->value = get32(t.val); got = true; }
        else if (t.tag == TAG_SERVER_INFO && t.len < sizeof out->info) {
            memcpy(out->info, t.val, t.len); out->info[t.len] = 0; out->has_info = true;
        } else if (t.tag == TAG_TRACE_ID && t.len == 8) { out->trace = get64(t.val); out->has_trace = true; }
    }
    return (r < 0 || !got) ? -EBADMSG : 0;
}

#ifndef OS_A13_NO_MAIN
/* ===================================================================== 테스트 하네스 */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
#define WAIT_FOR(ok, cond, ms) do { uint64_t end_ = now_ns() + (uint64_t)(ms) * 1000000ull; \
        while (!((ok) = (cond)) && now_ns() < end_) sleep_ms(1); } while (0)

static void on_watchdog(int sig) {
    (void)sig;
    static const char m[] = "FAIL  watchdog: 60 s 안에 끝나지 않음 (어딘가 멈춤)\n";
    write(STDOUT_FILENO, m, sizeof m - 1);
    _exit(1);
}

static uint32_t g_rng = 0x13A13u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static int count_fds(void) {
    int n = 0;
    for (int fd = 0; fd < 256; fd++) if (fcntl(fd, F_GETFD) != -1) n++;
    return n;
}

/* 참조 검증기: frame_parse 와 다르게 (바이트 하나씩 따라가며) 쓴 독립 구현. 퍼즈 비교용. */
static int ref_parse_len(const uint8_t *b, size_t n) {
    if (n < 4) return 0;
    uint64_t len = (uint64_t)b[0] | (uint64_t)b[1] << 8 | (uint64_t)b[2] << 16 | (uint64_t)b[3] << 24;
    if (len < 10 || len > RPC_MAX_FRAME) return -1;
    if ((uint64_t)n < 4 + len) return 0;
    if (b[4] == 0) return -1;
    uint64_t pos = 14, end = 4 + len;
    while (pos < end) {
        if (end - pos < 6) return -1;
        uint64_t l = (uint64_t)b[pos + 2] | (uint64_t)b[pos + 3] << 8 | (uint64_t)b[pos + 4] << 16 |
                     (uint64_t)b[pos + 5] << 24;
        pos += 6 + l;
        if (pos > end) return -1;
    }
    return (int)end;
}

static size_t make_sample(uint8_t *out, size_t cap, uint32_t id, uint32_t extra_len) {
    uint8_t pl[RPC_MAX_FRAME];
    tlvw_t w = { pl, sizeof pl - 16, 0, false };
    tlvw_u32(&w, TAG_VALUE, id * 7u);
    tlvw_put(&w, 777, "future", 6);             /* 모르는 tag */
    if (extra_len) {
        uint8_t big[4096];
        for (uint32_t i = 0; i < extra_len && i < sizeof big; i++) big[i] = (uint8_t)(i * 31u + id);
        tlvw_put(&w, TAG_SERVER_INFO, big, extra_len > sizeof big ? (uint32_t)sizeof big : extra_len);
    }
    return frame_build(out, cap, 1, T_REQ, M_ECHO, id, 0, pl, w.n);
}

/* ---------------- Part A ---------------- */
static int part_a(void) {
    int before = g_pass, tot = g_total;
    printf("--- Part A: 코덱 ---\n");
    uint8_t buf[RPC_BUF * 2];
    size_t n = make_sample(buf, sizeof buf, 42, 0);
    frame_t f;
    memset(&f, 0, sizeof f);
    int r = frame_parse(buf, n, &f);
    CHECK(r == (int)n);
    CHECK(f.version == 1 && f.type == T_REQ && f.method == M_ECHO && f.req_id == 42);
    tlv_it_t it;
    tlv_t t;
    tlv_begin(&it, f.payload, f.plen);
    CHECK(tlv_next(&it, &t) == 1 && t.tag == TAG_VALUE && t.len == 4 && get32(t.val) == 294);
    CHECK(tlv_next(&it, &t) == 1 && t.tag == 777 && t.len == 6 && memcmp(t.val, "future", 6) == 0);
    CHECK(tlv_next(&it, &t) == 0 && tlv_next(&it, &t) == 0);       /* 끝은 계속 끝 */

    int short_ok = 1;                            /* 모든 앞부분(prefix)은 "모자람" */
    for (size_t k = 0; k < n; k++) if (frame_parse(buf, k, &f) != 0) short_ok = 0;
    CHECK(short_ok);

    size_t n2 = make_sample(buf + n, sizeof buf - n, 43, 100);       /* 붙어 온 두 프레임 */
    r = frame_parse(buf, n + n2, &f);
    CHECK(r == (int)n && f.req_id == 42);
    r = frame_parse(buf + n, n2, &f);
    CHECK(r == (int)n2 && f.req_id == 43 && f.plen == n2 - 14);

    /* 나쁜 프레임 표 */
    struct { const char *name; uint8_t b[32]; size_t n; } bad[] = {
        { "len=0",            { 0, 0, 0, 0 }, 4 },
        { "len=9 (<hdr)",     { 9, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 }, 13 },
        { "len=16385",        { 0x01, 0x40, 0, 0 }, 4 },
        { "len=0xFFFFFFFF",   { 0xff, 0xff, 0xff, 0xff }, 4 },
        { "version=0",        { 10, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0 }, 14 },
        { "TLV head cut (3B)", { 13, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 4 }, 17 },
        { "TLV len > rest",   { 18, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 5, 0, 0, 0, 1, 2 }, 22 },
        { "TLV len=FFFFFFFF", { 18, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0xff, 0xff, 0xff, 0xff, 1, 2 }, 22 },
        { "2nd TLV cut",      { 22, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 9, 0, 1, 0, 0, 0 }, 26 },
    };
    int bad_ok = 0;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int rr = frame_parse(bad[i].b, bad[i].n, &f);
        if (rr == -EBADMSG) bad_ok++;
        else printf("      bad[%zu] %-18s → %d (기대 -EBADMSG)\n", i, bad[i].name, rr);
    }
    CHECK(bad_ok == (int)(sizeof bad / sizeof bad[0]));
    uint8_t unk[] = { 10, 0, 0, 0, 2, 99, 5, 0, 7, 0, 0, 0, 0, 0 };   /* 모르는 type 99: 오류 아님 */
    CHECK(frame_parse(unk, sizeof unk, &f) == 14 && f.type == 99 && f.version == 2);

    /* 퍼즈: 변형된 프레임 20000 개, 참조 검증기와 결과가 같아야 하고 범위 밖을 가리키면 안 된다 */
    int mismatch = 0, accepted = 0, rejected = 0, need_more = 0;
    for (int iter = 0; iter < 20000; iter++) {
        uint8_t fb[600];
        size_t fn = make_sample(fb, sizeof fb, xs32(), xs32() % 300);
        int kind = (int)(xs32() % 4);
        if (kind == 0) { int flips = 1 + (int)(xs32() % 4); while (flips--) fb[xs32() % fn] ^= (uint8_t)(1u << (xs32() % 8)); }
        else if (kind == 1) fn = xs32() % (fn + 1);
        else if (kind == 2) put32(fb + 14 + 2, xs32() % 2 ? xs32() : xs32() % 400);   /* 첫 TLV 길이 */
        else put32(fb, xs32() % 2 ? xs32() : xs32() % 700);                            /* 프레임 길이 */
        int a = frame_parse(fb, fn, &f), b = ref_parse_len(fb, fn);
        if ((a > 0) != (b > 0) || (a > 0 && a != b) || (a == 0) != (b == 0)) mismatch++;
        if (a > 0) {
            accepted++;
            if (f.payload < fb || f.payload + f.plen > fb + a) mismatch++;
            tlv_begin(&it, f.payload, f.plen);
            int rr;
            while ((rr = tlv_next(&it, &t)) == 1)
                if (t.val < f.payload || t.val + t.len > f.payload + f.plen) mismatch++;
            if (rr != 0) mismatch++;
        } else if (a == 0) need_more++;
        else rejected++;
    }
    printf("      fuzz: accepted=%d need_more=%d rejected=%d mismatch=%d\n", accepted, need_more, rejected, mismatch);
    CHECK(mismatch == 0 && accepted > 0 && rejected > 0);

    /* 조각난 스트림 재조립: 50 프레임을 1..17 바이트 조각으로 먹인다 */
    static uint8_t stream[50 * 600], acc[RPC_BUF * 2];
    size_t sn = 0, an = 0;
    for (uint32_t i = 0; i < 50; i++) sn += make_sample(stream + sn, sizeof stream - sn, 1000 + i, (i * 37) % 300);
    uint32_t got = 0, expect = 1000;
    int order_ok = 1, errs = 0;
    for (size_t pos = 0; pos < sn;) {
        size_t chunk = 1 + xs32() % 17;
        if (chunk > sn - pos) chunk = sn - pos;
        memcpy(acc + an, stream + pos, chunk);
        an += chunk;
        pos += chunk;
        for (;;) {
            int rr = frame_parse(acc, an, &f);
            if (rr == 0) break;
            if (rr < 0) { errs++; an = 0; break; }
            if (f.req_id != expect++) order_ok = 0;
            got++;
            memmove(acc, acc + rr, an - (size_t)rr);
            an -= (size_t)rr;
        }
    }
    CHECK(got == 50 && order_ok && errs == 0 && an == 0);

    /* 1 바이트 shim: 쓰기·읽기 모두 1 바이트씩 */
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    xport_t wx = { .fd = sv[0], .max_chunk = 1 }, rx = { .fd = sv[1], .max_chunk = 1 };
    size_t total = 0;
    int wfail = 0;
    for (uint32_t i = 0; i < 5; i++) {           /* 소켓 버퍼 안에 들어가는 양만 (쓰기 스레드 없이) */
        uint8_t fb[600];
        size_t fn = make_sample(fb, sizeof fb, 500 + i, i * 100);
        if (xp_write_all(&wx, fb, fn) != 0) wfail++;
        total += fn;
    }
    CHECK(wfail == 0 && atomic_load(&wx.writes) == total);
    int rd_ok = 1;
    for (uint32_t i = 0; i < 5; i++) {
        int rr = xp_read_frame(&rx, buf, sizeof buf, &f);
        if (rr != 1 || f.req_id != 500 + i || f.plen != 22 + (i ? 6 + i * 100 : 0)) rd_ok = 0;
    }
    CHECK(rd_ok && atomic_load(&rx.reads) == total);   /* read 호출 수 == 바이트 수: 부분 읽기가 실제로 일어났다 */
    uint8_t cut[9] = { 20, 0, 0, 0, 1, 1, 1, 0, 1 };      /* 길이 20 이라고 해 놓고 5 바이트 뒤 끊김 */
    write(sv[0], cut, sizeof cut);
    shutdown(sv[0], SHUT_WR);
    CHECK(xp_read_frame(&rx, buf, sizeof buf, &f) == -ECONNRESET);
    close(sv[0]); close(sv[1]);

    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    rx.fd = sv[1]; rx.max_chunk = 0;
    shutdown(sv[0], SHUT_WR);
    CHECK(xp_read_frame(&rx, buf, sizeof buf, &f) == 0);   /* 경계에서 EOF = 깔끔한 끝 */
    close(sv[0]); close(sv[1]);
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    rx.fd = sv[1];
    uint8_t huge[4] = { 0xff, 0xff, 0xff, 0xff };       /* 몸통은 오지 않는다: 길이만 보고 바로 거절해야 안 멈춘다 */
    write(sv[0], huge, 4);
    CHECK(xp_read_frame(&rx, buf, sizeof buf, &f) == -EBADMSG);
    close(sv[0]); close(sv[1]);
    return g_pass - before == g_total - tot;
}

/* ---------------- Part B ---------------- */
typedef struct { rpc_client_t *c; uint32_t v, delay; int timeout, rc; echo_res_t res; atomic_bool done; } call_arg_t;
static void *call_thread(void *p) {
    call_arg_t *a = p;
    a->rc = echo_call(a->c, a->v, a->delay, a->timeout, &a->res);
    atomic_store(&a->done, true);
    return NULL;
}
static void *gate_thread(void *p) {
    call_arg_t *a = p;
    uint8_t req[16], resp[64];
    uint32_t rl = 0;
    tlvw_t w = { req, sizeof req, 0, false };
    tlvw_u32(&w, TAG_VALUE, a->v);
    a->rc = rpc_call(a->c, M_GATE, req, w.n, resp, sizeof resp, &rl, a->timeout);
    if (a->rc == 0) tlv_get_u32(resp, rl, TAG_VALUE, &a->res.value);
    atomic_store(&a->done, true);
    return NULL;
}
typedef struct { rpc_client_t *c; int id, calls, bad; } stress_t;
static void *stress_thread(void *p) {
    stress_t *a = p;
    uint32_t x = 0x9E3779B9u * (uint32_t)(a->id + 1);
    for (int i = 0; i < a->calls; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        echo_res_t r;
        if (echo_call(a->c, x, x % 3, 5000, &r) != 0 || r.value != echo_fn(x)) a->bad++;
    }
    return NULL;
}

static void pair_new(int sv[2]) { socketpair(AF_UNIX, SOCK_STREAM, 0, sv); }

static void part_b(void) {
    printf("--- Part B: 다중화·타임아웃·버전 ---\n");
    int sv[2];
    pair_new(sv);
    rpc_server_t *s = rpc_server_start(sv[1], 2, 16, 0);
    rpc_client_t *c = rpc_client_new(sv[0], 2, 0);
    echo_res_t r;
    CHECK(echo_call(c, 7, 0, 3000, &r) == 0 && r.value == echo_fn(7));
    CHECK(cget(c, &c->n_pending) == 0);
    uint8_t resp[64];
    uint32_t rl;
    CHECK(rpc_call(c, 99, NULL, 0, resp, sizeof resp, &rl, 3000) == -ENOSYS);   /* 모르는 메서드 */

    /* 순서 밖 응답 (결정적): 게이트에 붙잡힌 호출 A 가 있어도 B 는 끝난다 */
    call_arg_t ga = { .c = c, .v = 11, .timeout = 5000 };
    pthread_t gt;
    spawn(&gt, gate_thread, &ga);
    bool ok;
    WAIT_FOR(ok, sget(s, &s->gate_waiting) == 1, 3000);
    CHECK(ok);
    CHECK(echo_call(c, 12, 0, 3000, &r) == 0 && r.value == echo_fn(12));
    CHECK(!atomic_load(&ga.done));                       /* A 는 아직 — 앞선 요청이 뒤 요청을 막지 않는다 */
    pthread_mutex_lock(&s->mu); s->gate_open = true; pthread_cond_broadcast(&s->cv); pthread_mutex_unlock(&s->mu);
    pthread_join(gt, NULL);
    CHECK(ga.rc == 0 && ga.res.value == echo_fn(11));

    /* 16 스레드 × 100 호출, 서버 지연 0~2 ms 무작위 → 응답이 섞여 와도 req_id 로 짝이 맞는다 */
    stress_t st[16];
    pthread_t th[16];
    for (int i = 0; i < 16; i++) { st[i] = (stress_t){ c, i, 100, 0 }; spawn(&th[i], stress_thread, &st[i]); }
    int bad = 0;
    for (int i = 0; i < 16; i++) { pthread_join(th[i], NULL); bad += st[i].bad; }
    CHECK(bad == 0);
    printf("      동시 호출 최대 pending=%u, 서버가 순서 밖으로 끝낸 횟수=%u\n",
           cget(c, &c->max_pending), sget(s, &s->inversions));
    CHECK(cget(c, &c->n_pending) == 0);

    /* 타임아웃: 서버 400 ms, 마감 50 ms → -ETIMEDOUT, pending 즉시 0, 늦은 응답은 버려지고 세어진다 */
    uint32_t late0 = cget(c, &c->late_drops);
    CHECK(echo_call(c, 5, 400, 50, &r) == -ETIMEDOUT);
    CHECK(cget(c, &c->n_pending) == 0);
    WAIT_FOR(ok, cget(c, &c->late_drops) == late0 + 1, 3000);
    CHECK(ok);
    CHECK(echo_call(c, 6, 0, 3000, &r) == 0 && r.value == echo_fn(6));         /* 연결은 멀쩡하다 */

    /* 버전: v2 ↔ v2 */
    CHECK(r.has_info && strcmp(r.info, "mini-rpc/2") == 0 && r.has_trace && r.trace == (0xC0FFEE0000000000ull | 6));
    rpc_client_free(c);
    rpc_server_stop(s);

    /* v1 클라이언트 ↔ v2 서버: 모르는 SERVER_INFO 를 건너뛰고 값은 맞다 */
    pair_new(sv);
    s = rpc_server_start(sv[1], 2, 4, 0);
    c = rpc_client_new(sv[0], 1, 0);
    CHECK(echo_call(c, 21, 0, 3000, &r) == 0 && r.value == echo_fn(21) && r.unknown == 1 && !r.has_info);
    CHECK(cget(c, &c->n_pending) == 0 && c->peer_version == 2);
    rpc_client_free(c);
    rpc_server_stop(s);

    /* v2 클라이언트 ↔ v1 서버: 서버가 TRACE_ID 를 건너뛰고, 클라이언트는 SERVER_INFO 없음 = 기본값 */
    pair_new(sv);
    s = rpc_server_start(sv[1], 1, 4, 0);
    c = rpc_client_new(sv[0], 2, 0);
    CHECK(echo_call(c, 22, 0, 3000, &r) == 0 && r.value == echo_fn(22) && !r.has_info && !r.has_trace);
    CHECK(sget(s, &s->unknown_tlv) == 1);
    rpc_client_free(c);
    rpc_server_stop(s);

    /* 1 바이트 shim 양쪽: 4 스레드 × 20 호출 */
    pair_new(sv);
    s = rpc_server_start(sv[1], 2, 4, 1);
    c = rpc_client_new(sv[0], 2, 1);
    for (int i = 0; i < 4; i++) { st[i] = (stress_t){ c, 100 + i, 20, 0 }; spawn(&th[i], stress_thread, &st[i]); }
    bad = 0;
    for (int i = 0; i < 4; i++) { pthread_join(th[i], NULL); bad += st[i].bad; }
    CHECK(bad == 0 && atomic_load(&c->x.reads) > 4 * 20 * 14);   /* 프레임당 바이트 수만큼 read */
    rpc_client_free(c);
    rpc_server_stop(s);

    /* 가짜 서버: 손으로 쓴 프레임으로 클라이언트의 규칙을 하나씩 본다 */
    pair_new(sv);
    c = rpc_client_new(sv[0], 1, 0);
    xport_t fx = { .fd = sv[1] };
    call_arg_t ca = { .c = c, .v = 3, .timeout = 3000 };
    pthread_t ct;
    spawn(&ct, call_thread, &ca);
    uint8_t buf[RPC_BUF], out[256], pl[64];
    frame_t f;
    int rr = xp_read_frame(&fx, buf, sizeof buf, &f);
    CHECK(rr == 1 && f.type == T_REQ && f.method == M_ECHO);
    uint32_t id = f.req_id;
    size_t n = frame_build(out, sizeof out, 3, 99, 0, id, 0, NULL, 0);          /* 모르는 type 99 */
    xp_write_all(&fx, out, n);
    n = frame_build(out, sizeof out, 1, T_RESP, M_ECHO, id + 1000, 0, NULL, 0); /* 아무도 안 기다리는 id */
    xp_write_all(&fx, out, n);
    tlvw_t w = { pl, sizeof pl, 0, false };
    tlvw_put(&w, 4242, "xyz", 3);                                               /* 모르는 tag 가 앞에 */
    tlvw_u32(&w, TAG_VALUE, echo_fn(3));
    n = frame_build(out, sizeof out, 1, T_RESP, M_ECHO, id, 0, pl, w.n);
    xp_write_all(&fx, out, n);
    pthread_join(ct, NULL);
    CHECK(ca.rc == 0 && ca.res.value == echo_fn(3) && ca.res.unknown == 1);
    CHECK(cget(c, &c->unknown_type) == 1 && cget(c, &c->late_drops) == 1);
    uint8_t junk[4] = { 3, 0, 0, 0 };                                           /* 길이 3: 깨진 프레임 */
    write(sv[1], junk, 4);
    bool dead;
    WAIT_FOR(dead, client_dead(c), 3000);
    CHECK(dead && echo_call(c, 1, 0, 3000, &r) == -EPROTO);                    /* 이후 호출은 즉시 실패 */
    rpc_client_free(c);
    close(sv[1]);
}

/* ---------------- Part C ---------------- */
static int count_req(uint8_t *req, size_t cap, uint32_t n, uint32_t start, uint32_t delay) {
    tlvw_t w = { req, (uint32_t)cap, 0, false };
    tlvw_u32(&w, TAG_COUNT, n);
    tlvw_u32(&w, TAG_START, start);
    if (delay) tlvw_u32(&w, TAG_DELAY_MS, delay);
    return (int)w.n;
}

static void part_c(void) {
    printf("--- Part C: 스트리밍·취소·장애 주입 ---\n");
    int sv[2];
    pair_new(sv);
    rpc_server_t *s = rpc_server_start(sv[1], 2, 16, 0);
    rpc_client_t *c = rpc_client_new(sv[0], 2, 0);
    uint8_t req[64], item[ITEM_MAX];
    uint32_t sid = 0, len = 0, v = 0, idx = 0;

    /* 정상 스트림: 50 개 + END */
    int rn = count_req(req, sizeof req, 50, 1000, 0);
    CHECK(rpc_stream_open(c, M_COUNT, req, (uint32_t)rn, &sid) == 0);
    int k = 0, in_order = 1, r;
    while ((r = rpc_stream_next(c, sid, item, sizeof item, &len, 3000)) == 1) {
        if (!tlv_get_u32(item, len, TAG_INDEX, &idx) || !tlv_get_u32(item, len, TAG_VALUE, &v) ||
            idx != (uint32_t)k || v != 1000u + (uint32_t)k) in_order = 0;
        k++;
    }
    CHECK(r == 0 && k == 50 && in_order);
    CHECK(rpc_stream_next(c, sid, item, sizeof item, &len, 100) == 0);         /* 끝은 계속 끝 */
    pthread_mutex_lock(&c->mu); uint32_t endc = slot_find(c, sid) ? slot_find(c, sid)->end_count : 0; pthread_mutex_unlock(&c->mu);
    CHECK(endc == 50);
    rpc_stream_close(c, sid);

    /* 항목 하나의 마감: 서버가 500 ms 마다 하나 → 30 ms 마감은 -ETIMEDOUT, 스트림은 살아 있다 */
    rn = count_req(req, sizeof req, 10, 0, 500);
    CHECK(rpc_stream_open(c, M_COUNT, req, (uint32_t)rn, &sid) == 0);
    CHECK(rpc_stream_next(c, sid, item, sizeof item, &len, 30) == -ETIMEDOUT);
    CHECK(rpc_stream_cancel(c, sid, 3000) == 0);
    rpc_stream_close(c, sid);

    /* 취소: 100000 개짜리(5 ms 간격)를 3 개 받고 CANCEL → 서버가 K 개 안에서 멈춘다 */
    uint32_t stop0 = sget(s, &s->stopped_by_cancel);
    rn = count_req(req, sizeof req, 100000, 0, 5);
    CHECK(rpc_stream_open(c, M_COUNT, req, (uint32_t)rn, &sid) == 0);
    int got3 = 0;
    for (int i = 0; i < 3; i++) if (rpc_stream_next(c, sid, item, sizeof item, &len, 3000) == 1) got3++;
    CHECK(got3 == 3);
    CHECK(rpc_stream_cancel(c, sid, 3000) == 0);
    pthread_mutex_lock(&c->mu);
    slot_t *ss = slot_find(c, sid);
    uint32_t after = ss ? ss->after_cancel : 999;
    int cst = ss ? ss->status : 0;
    pthread_mutex_unlock(&c->mu);
    rpc_stream_close(c, sid);
    bool ok;
    WAIT_FOR(ok, sget(s, &s->n_active) == 0, 3000);
    uint32_t sent_items = sget(s, &s->last_stream_items);
    printf("      취소: 서버가 보낸 항목=%u (받은 3 + 취소 뒤 도착 %u), 서버 stopped_by_cancel=%u\n",
           sent_items, after, sget(s, &s->stopped_by_cancel) - stop0);
    CHECK(sget(s, &s->stopped_by_cancel) == stop0 + 1 && cst == -ECANCELED);
    CHECK(ok && sent_items >= 3 && sent_items <= 3 + 64 && after <= 64);         /* K = 64 */

    /* 장애 1: 서버가 소켓을 닫는다 — 10 s 걸릴 호출 8 개가 기다리는 중 */
    call_arg_t ca[8];
    pthread_t th[8];
    uint32_t base = sget(s, &s->reqs);                 /* 스레드를 띄우기 "전에" 센다 */
    for (int i = 0; i < 8; i++) { ca[i] = (call_arg_t){ .c = c, .v = (uint32_t)i, .delay = 10000, .timeout = 5000 }; spawn(&th[i], call_thread, &ca[i]); }
    WAIT_FOR(ok, sget(s, &s->reqs) == base + 8, 3000);
    CHECK(ok);
    uint64_t t0 = now_ns();
    rpc_server_drop_conn(s);
    int reset = 0;
    for (int i = 0; i < 8; i++) { pthread_join(th[i], NULL); if (ca[i].rc == -ECONNRESET) reset++; }
    printf("      서버 끊김 → 대기 호출 8 개가 끝나기까지 %.1f ms (마감 5000 ms 보다 훨씬 전)\n",
           (double)(now_ns() - t0) / 1e6);
    CHECK(reset == 8);                                 /* -ETIMEDOUT 이 아니라 연결 오류로 끝났다 */
    echo_res_t er;
    CHECK(echo_call(c, 1, 0, 3000, &er) == -ECONNRESET);
    CHECK(rpc_stream_open(c, M_COUNT, req, (uint32_t)rn, &sid) == -ECONNRESET);
    rpc_client_free(c);
    rpc_server_stop(s);

    /* 장애 2: 다른 프로세스의 서버를 SIGKILL (이름 있는 소켓) */
    char dir[] = "/tmp/OS-A13_work";
    char path[104];
    mkdir(dir, 0700);
    snprintf(path, sizeof path, "%s/rpc.%d.sock", dir, (int)getpid());
    unlink(path);
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    CHECK(bind(ls, (struct sockaddr *)&sa, sizeof sa) == 0 && listen(ls, 4) == 0);
    fflush(stdout);                                    /* 자식이 버퍼를 두 번 찍지 않게 */
    pid_t pid = fork();
    if (pid == 0) {
        alarm(20);                                     /* 고아 방지 */
        int fd = accept(ls, NULL, NULL);
        close(ls);
        if (fd < 0) _exit(2);
        rpc_server_t *cs = rpc_server_start(fd, 2, 8, 0);
        pthread_join(cs->reader, NULL);                /* 연결이 끝날 때까지 */
        _exit(0);
    }
    close(ls);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
    unlink(path);                                      /* 연결된 뒤엔 이름이 필요 없다 */
    rmdir(dir);
    struct stat stt;
    CHECK(stat(path, &stt) == -1 && errno == ENOENT);
    c = rpc_client_new(fd, 2, 0);
    uint8_t resp[16];
    CHECK(rpc_call(c, M_PING, NULL, 0, resp, sizeof resp, &len, 3000) == 0);
    for (int i = 0; i < 4; i++) { ca[i] = (call_arg_t){ .c = c, .v = (uint32_t)i, .delay = 10000, .timeout = 5000 }; spawn(&th[i], call_thread, &ca[i]); }
    WAIT_FOR(ok, cget(c, &c->sent_reqs) == 5 && cget(c, &c->n_pending) == 4, 3000);
    CHECK(ok);
    kill(pid, SIGKILL);
    int wst = 0;
    waitpid(pid, &wst, 0);
    CHECK(WIFSIGNALED(wst) && WTERMSIG(wst) == SIGKILL);
    reset = 0;
    for (int i = 0; i < 4; i++) { pthread_join(th[i], NULL); if (ca[i].rc == -ECONNRESET) reset++; }
    CHECK(reset == 4);
    CHECK(echo_call(c, 1, 0, 3000, &er) == -ECONNRESET);
    rpc_client_free(c);
}

int main(void) {
    signal(SIGALRM, on_watchdog);
    alarm(60);
    setvbuf(stdout, NULL, _IOLBF, 0);
    int fds0 = count_fds();
    if (part_a()) {
        part_b();
        part_c();
        CHECK(count_fds() == fds0);                    /* fd 누수 없음 */
        CHECK(atomic_load(&g_threads_live) == 0);      /* 만든 스레드는 전부 join 됨 */
    } else {
        printf("SKIP  Part B, C (Part A 코덱/전송이 먼저 통과해야 한다)\n");
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* OS_A13_NO_MAIN */
