/* OS-A14 macOS Mach 메시지와 XPC: 포트 권한 기반 IPC   (macOS 전용 — Mach/XPC 는 다른 OS 에 없다)
 *
 *   Part A  port_create / server_reply / server_handle_one / rpc_call
 *           receive right + send right 를 만들고, 요청에 MAKE_SEND_ONCE 응답 권리를 실어 보내고,
 *           서버는 그 send-once right 로 딱 한 번 답한다. 타임아웃 뒤엔 응답 포트를 갈아 끼운다(MIG 방식).
 *   Part B  send_complex / recv_complex / arm_notify / wait_notify
 *           MACH_MSGH_BITS_COMPLEX 메시지: 포트 디스크립터(권리 전달) + OOL 메모리(가상 복사).
 *           no-senders · dead-name 알림, 수신 타임아웃, 잘못된 복합 메시지 거부.
 *   Part C  spawn_child_server (fork + TASK_BOOTSTRAP_PORT 트릭으로 send right 를 자식과 교환),
 *           자식 사망 → dead-name 알림. xpc_server_start / xpc_call: 익명 XPC 리스너 + 엔드포인트,
 *           딕셔너리 요청/응답, 취소 시 XPC_ERROR_CONNECTION_INVALID.
 *
 * 모든 mach_msg 수신에 MACH_RCV_TIMEOUT, 자식에 alarm() 워치독, 부모는 waitpid(WNOHANG) 폴링 + SIGKILL.
 * 전체 하네스에 alarm(60). 시간 측정은 없다 (demo_bench.c 에 있다). 파일·IPC 이름을 만들지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE
#include <dispatch/dispatch.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mig_errors.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xpc/xpc.h>

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
                      else printf("FAIL  %s   (line %d)\n", #c, __LINE__); } while (0)

/* ---- 메시지 형식 (MIG 관례: 응답 id = 요청 id + 100) ---- */
enum { OP_ADD = 1, OP_MUL = 2, OP_QUIT = 3, OP_DROP = 4 };
#define REQ_ID  1400
#define REP_ID  (REQ_ID + 100)
#define CX_ID   1600

typedef struct { mach_msg_header_t h; uint32_t op, seq; int64_t a, b; } req_msg_t;
typedef struct { mach_msg_header_t h; uint32_t seq; int32_t status; int64_t value; } rep_msg_t;
typedef struct { req_msg_t m; mach_msg_max_trailer_t t; } req_buf_t;
typedef union  { mach_msg_header_t h; rep_msg_t r; uint8_t raw[sizeof(rep_msg_t) + sizeof(mach_msg_max_trailer_t) + 64]; } rep_buf_t;

typedef struct { unsigned handled, bad, dropped, reply_failed; bool quit; } server_stats_t;
typedef struct { mach_port_t server; mach_port_t reply; uint32_t next_seq; unsigned stale; } rpc_client_t;

/* 복합 메시지: 포트 디스크립터 1 + OOL 디스크립터 1 + 인라인 tag */
typedef struct {
    mach_msg_header_t h;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t port;
    mach_msg_ool_descriptor_t ool;
    uint32_t tag;
} cx_msg_t;
typedef union { mach_msg_header_t h; cx_msg_t m; uint8_t raw[sizeof(cx_msg_t) + sizeof(mach_msg_max_trailer_t) + 64]; } cx_buf_t;
typedef struct { mach_port_t port; mach_msg_type_name_t port_type; void *buf; size_t len; uint32_t tag; } cx_got_t;

typedef struct { mach_msg_id_t id; mach_port_name_t dead; unsigned count; } notify_got_t;

/* ---- 하네스 도우미 ---- */
static mach_port_urefs_t refs(mach_port_t name, mach_port_right_t r) {
    mach_port_urefs_t n = 0;
    if (mach_port_get_refs(mach_task_self(), name, r, &n) != KERN_SUCCESS) return (mach_port_urefs_t)-1;
    return n;
}
static mach_port_type_t ptype(mach_port_t name, kern_return_t *kr) {
    mach_port_type_t t = 0;
    *kr = mach_port_type(mach_task_self(), name, &t);
    return t;
}
static unsigned name_count(void) {                  /* 이 태스크 이름 공간의 이름 개수 (누수 검사) */
    mach_port_name_array_t n; mach_msg_type_number_t nc;
    mach_port_type_array_t t; mach_msg_type_number_t tc;
    if (mach_port_names(mach_task_self(), &n, &nc, &t, &tc) != KERN_SUCCESS) return 0;
    vm_deallocate(mach_task_self(), (vm_address_t)n, nc * sizeof n[0]);
    vm_deallocate(mach_task_self(), (vm_address_t)t, tc * sizeof t[0]);
    return nc;
}
static void port_destroy(mach_port_t p) {           /* receive right + 같은 이름의 send right 전부 */
    mach_port_urefs_t s = refs(p, MACH_PORT_RIGHT_SEND);
    if (s != (mach_port_urefs_t)-1 && s > 0) mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_SEND, -(mach_port_delta_t)s);
    mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_RECEIVE, -1);
}
static uint64_t pattern(uint64_t i, uint32_t salt) { uint64_t x = i * 0x9E3779B97F4A7C15ull ^ salt; x ^= x >> 29; return x; }

/* ======================= Part A — 권리와 요청/응답 ======================= */

/* receive right 1 개 + 같은 이름에 send right 1 개(uref 1)를 만든다. */
static kern_return_t port_create(mach_port_t *out) {
    mach_port_t p = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p);
    if (kr != KERN_SUCCESS) return kr;
    kr = mach_port_insert_right(mach_task_self(), p, p, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) { mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_RECEIVE, -1); return kr; }
    *out = p;
    return KERN_SUCCESS;
}

/* send-once right(reply_so)로 응답 1 개를 보낸다 — 성공하면 그 권리는 소비되어 이름이 사라진다.
 * 상대 응답 포트가 이미 죽었으면 MACH_SEND_INVALID_DEST: 남은 dead name 을 지워 누수를 막고 kr 을 돌려준다. */
static kern_return_t server_reply(mach_port_t reply_so, uint32_t seq, int32_t status, int64_t value) {
    rep_msg_t r;
    memset(&r, 0, sizeof r);
    r.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    r.h.msgh_size = sizeof r;
    r.h.msgh_remote_port = reply_so;
    r.h.msgh_local_port = MACH_PORT_NULL;
    r.h.msgh_id = REP_ID;
    r.seq = seq; r.status = status; r.value = value;
    kern_return_t kr = mach_msg(&r.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof r, 0, MACH_PORT_NULL, 100, MACH_PORT_NULL);
    if (kr == MACH_SEND_INVALID_DEST) mach_port_deallocate(mach_task_self(), reply_so);
    return kr;
}

/* 요청 1 개를 받아 처리한다. 반환: 수신 kr (KERN_SUCCESS = 메시지 하나를 처리함, 형식 불량·DROP 포함).
 * 형식 검사: id, 크기, 비복합, 응답 권리가 send-once. 불량이면 mach_msg_destroy 로 권리를 돌려준다. */
static kern_return_t server_handle_one(mach_port_t port, mach_msg_timeout_t ms, server_stats_t *st) {
    req_buf_t b;
    memset(&b, 0, sizeof b);
    kern_return_t kr = mach_msg(&b.m.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof b, port, ms, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) return kr;
    mach_msg_header_t *h = &b.m.h;
    if (h->msgh_id != REQ_ID || h->msgh_size != sizeof(req_msg_t) || MACH_MSGH_BITS_IS_COMPLEX(h->msgh_bits) ||
        MACH_MSGH_BITS_REMOTE(h->msgh_bits) != MACH_MSG_TYPE_PORT_SEND_ONCE) {
        st->bad++;
        mach_msg_destroy(h);                       /* send-once 를 버리면 클라이언트는 SEND_ONCE 알림을 받는다 */
        return KERN_SUCCESS;
    }
    int64_t v = 0; int32_t status = 0;
    switch (b.m.op) {
    case OP_ADD:  v = (int64_t)((uint64_t)b.m.a + (uint64_t)b.m.b); break;   /* 상대 입력 — 오버플로 UB 회피 */
    case OP_MUL:  v = (int64_t)((uint64_t)b.m.a * (uint64_t)b.m.b); break;
    case OP_QUIT: st->quit = true; break;
    case OP_DROP:                                   /* 일부러 답하지 않는다 = "답하기 전에 죽은 서버" */
        st->dropped++;
        mach_port_deallocate(mach_task_self(), h->msgh_remote_port);
        return KERN_SUCCESS;
    default: status = MIG_BAD_ID; break;
    }
    st->handled++;
    if (server_reply(h->msgh_remote_port, b.m.seq, status, v) != KERN_SUCCESS) st->reply_failed++;
    return KERN_SUCCESS;
}

/* 요청을 보내고 응답을 기다린다.
 *   반환 KERN_SUCCESS(+*out) / 응답 status(MIG_BAD_ID 등) / MACH_SEND_* / MACH_RCV_TIMED_OUT /
 *   MIG_SERVER_DIED (서버가 응답 권리를 버림 = SEND_ONCE 알림) / MIG_REPLY_MISMATCH.
 * 타임아웃이면 응답 포트를 새것으로 바꾼다: 늦게 온 응답이 다음 호출의 응답으로 오인되지 않게. */
static kern_return_t rpc_call(rpc_client_t *c, uint32_t op, int64_t a, int64_t b, int64_t *out, mach_msg_timeout_t ms) {
    req_msg_t q;
    memset(&q, 0, sizeof q);
    uint32_t seq = ++c->next_seq;
    q.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    q.h.msgh_size = sizeof q;
    q.h.msgh_remote_port = c->server;
    q.h.msgh_local_port = c->reply;
    q.h.msgh_id = REQ_ID;
    q.op = op; q.seq = seq; q.a = a; q.b = b;
    kern_return_t kr = mach_msg(&q.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof q, 0, MACH_PORT_NULL, ms, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) return kr;
    for (;;) {
        rep_buf_t r;
        memset(&r, 0, sizeof r);
        kr = mach_msg(&r.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof r, c->reply, ms, MACH_PORT_NULL);
        if (kr == MACH_RCV_TIMED_OUT) {
            mach_port_t fresh = MACH_PORT_NULL;      /* 새것을 먼저 만들고 옛것을 부순다 → 이름이 반드시 다르다 */
            if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &fresh) == KERN_SUCCESS) {
                mach_port_mod_refs(mach_task_self(), c->reply, MACH_PORT_RIGHT_RECEIVE, -1);
                c->reply = fresh;
            }
            return kr;
        }
        if (kr != KERN_SUCCESS) return kr;
        if (r.h.msgh_id == MACH_NOTIFY_SEND_ONCE) return MIG_SERVER_DIED;
        if (r.h.msgh_id != REP_ID || r.h.msgh_size != sizeof(rep_msg_t) || MACH_MSGH_BITS_IS_COMPLEX(r.h.msgh_bits)) {
            mach_msg_destroy(&r.h);
            return MIG_REPLY_MISMATCH;
        }
        if (r.r.seq != seq) { c->stale++; continue; }   /* 방어: 다른 호출의 응답이면 버리고 계속 */
        if (r.r.status != 0) return r.r.status;
        *out = r.r.value;
        return KERN_SUCCESS;
    }
}

/* ======================= Part B — 복합 메시지와 알림 ======================= */

/* dest 로 [포트 디스크립터(right, disp) + OOL(buf, len, 가상 복사) + tag] 를 보낸다. right 가 NULL 이면 디스크립터만 NULL. */
static kern_return_t send_complex(mach_port_t dest, mach_port_t right, mach_msg_type_name_t disp,
                                  const void *buf, size_t len, uint32_t tag) {
    if (len > UINT32_MAX) return KERN_INVALID_ARGUMENT;
    cx_msg_t m;
    memset(&m, 0, sizeof m);
    m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
    m.h.msgh_size = sizeof m;
    m.h.msgh_remote_port = dest;
    m.h.msgh_id = CX_ID;
    m.body.msgh_descriptor_count = 2;
    m.port.name = right;
    m.port.disposition = disp;
    m.port.type = MACH_MSG_PORT_DESCRIPTOR;
    m.ool.address = (void *)(uintptr_t)buf;
    m.ool.size = (mach_msg_size_t)len;
    m.ool.deallocate = false;
    m.ool.copy = MACH_MSG_VIRTUAL_COPY;
    m.ool.type = MACH_MSG_OOL_DESCRIPTOR;
    m.tag = tag;
    return mach_msg(&m.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof m, 0, MACH_PORT_NULL, 1000, MACH_PORT_NULL);
}

/* 복합 메시지 1 개를 받는다. 형식이 다르면 mach_msg_destroy(권리·OOL 반납) 후 MIG_TYPE_ERROR.
 * 성공 시 out->port 는 받은 권리(호출자가 소유), out->buf 는 vm_deallocate 할 책임이 호출자에게 있다. */
static kern_return_t recv_complex(mach_port_t port, mach_msg_timeout_t ms, cx_got_t *out) {
    cx_buf_t b;
    memset(&b, 0, sizeof b);
    kern_return_t kr = mach_msg(&b.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof b, port, ms, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) return kr;
    if (!MACH_MSGH_BITS_IS_COMPLEX(b.h.msgh_bits) || b.h.msgh_id != CX_ID || b.h.msgh_size != sizeof(cx_msg_t) ||
        b.m.body.msgh_descriptor_count != 2 || b.m.port.type != MACH_MSG_PORT_DESCRIPTOR ||
        b.m.ool.type != MACH_MSG_OOL_DESCRIPTOR) {
        mach_msg_destroy(&b.h);
        return MIG_TYPE_ERROR;
    }
    out->port = b.m.port.name;
    out->port_type = b.m.port.disposition;          /* 받는 쪽에선 MACH_MSG_TYPE_PORT_SEND(17) 등 */
    out->buf = b.m.ool.address;
    out->len = b.m.ool.size;
    out->tag = b.m.tag;
    return KERN_SUCCESS;
}

/* name 에 알림을 건다: which = MACH_NOTIFY_NO_SENDERS (name 은 receive right) 또는
 * MACH_NOTIFY_DEAD_NAME (name 은 send right). 알림은 notify_port 로 send-once 메시지가 되어 온다. */
static kern_return_t arm_notify(mach_port_t name, mach_msg_id_t which, mach_port_t notify_port) {
    mach_port_t prev = MACH_PORT_NULL;
    kern_return_t kr = mach_port_request_notification(mach_task_self(), name, which, 0, notify_port,
                                                      MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);
    if (kr == KERN_SUCCESS && prev != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), prev);
    return kr;
}

/* 알림 1 개를 기다려 해석한다. DEAD_NAME 이면 out->dead (알림이 dead name uref 를 1 개 더 실어 온다). */
static kern_return_t wait_notify(mach_port_t notify_port, mach_msg_timeout_t ms, notify_got_t *out) {
    union { mach_msg_header_t h; mach_dead_name_notification_t d; mach_no_senders_notification_t n; uint8_t raw[256]; } b;
    memset(&b, 0, sizeof b);
    kern_return_t kr = mach_msg(&b.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof b, notify_port, ms, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) return kr;
    out->id = b.h.msgh_id;
    out->dead = MACH_PORT_NULL; out->count = 0;
    if (b.h.msgh_id == MACH_NOTIFY_DEAD_NAME) out->dead = b.d.not_port;
    else if (b.h.msgh_id == MACH_NOTIFY_NO_SENDERS) out->count = b.n.not_count;
    else mach_msg_destroy(&b.h);
    return KERN_SUCCESS;
}

/* ======================= Part C — 프로세스 경계 ======================= */

static int wait_child(pid_t pid, int ms) {           /* waitpid 폴링 + 시한 넘으면 SIGKILL. 반환 status */
    int st = 0;
    for (int t = 0; t < ms; t++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        usleep(1000);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return st;
}

/* 자식 몸체 (하네스): 상속받은 "bootstrap" 특수 포트가 사실은 부모 포트다.
 * 자기 서비스 포트를 만들어 MAKE_SEND 로 부모에게 보내고, OOL 로 64 KiB 패턴도 같이 보낸 뒤 요청을 처리한다. */
static _Noreturn void child_main(void) {
    alarm(10);                                       /* 워치독: 무슨 일이 있어도 10 초 안에 죽는다 */
    mach_port_t parent = MACH_PORT_NULL, mine = MACH_PORT_NULL;
    if (task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &parent) != KERN_SUCCESS) _exit(2);
    if (port_create(&mine) != KERN_SUCCESS) _exit(3);
    static uint64_t blob[8192];                      /* 64 KiB */
    for (size_t i = 0; i < 8192; i++) blob[i] = pattern(i, (uint32_t)getpid());
    if (send_complex(parent, mine, MACH_MSG_TYPE_MAKE_SEND, blob, sizeof blob, (uint32_t)getpid()) != KERN_SUCCESS) _exit(4);
    server_stats_t st = {0};
    while (!st.quit) {
        kern_return_t kr = server_handle_one(mine, 3000, &st);
        if (kr != KERN_SUCCESS) _exit(5);
    }
    _exit(0);
}

/* 자식을 띄우고 자식 서비스 포트의 send right 를 *child_send 로 받는다. 반환 pid (실패 -1).
 * 방법: 부모 포트를 TASK_BOOTSTRAP_PORT 로 잠깐 바꿔 둔 채 fork → 자식이 그것을 상속 → 즉시 원래 값 복원. */
static pid_t spawn_child_server(mach_port_t *child_send, cx_got_t *hello) {
    mach_port_t me = mach_task_self(), saved = MACH_PORT_NULL, rendezvous = MACH_PORT_NULL;
    if (task_get_special_port(me, TASK_BOOTSTRAP_PORT, &saved) != KERN_SUCCESS) return -1;
    if (port_create(&rendezvous) != KERN_SUCCESS) { mach_port_deallocate(me, saved); return -1; }
    if (task_set_special_port(me, TASK_BOOTSTRAP_PORT, rendezvous) != KERN_SUCCESS) {
        port_destroy(rendezvous); mach_port_deallocate(me, saved); return -1;
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) child_main();                      /* 돌아오지 않는다 */
    task_set_special_port(me, TASK_BOOTSTRAP_PORT, saved);   /* fork 성공/실패와 무관하게 항상 복원 */
    mach_port_deallocate(me, saved);
    if (pid < 0) { port_destroy(rendezvous); return -1; }
    cx_got_t g;
    memset(&g, 0, sizeof g);
    kern_return_t kr = recv_complex(rendezvous, 3000, &g);
    port_destroy(rendezvous);
    if (kr != KERN_SUCCESS || g.port_type != MACH_MSG_TYPE_PORT_SEND) {
        if (kr == KERN_SUCCESS) { mach_port_deallocate(me, g.port); vm_deallocate(me, (vm_address_t)g.buf, g.len); }
        kill(pid, SIGKILL); wait_child(pid, 1000);
        return -1;
    }
    *child_send = g.port;
    *hello = g;
    return pid;
}

/* ======================= Part C — XPC ======================= */

enum { XERR_INVALID = -2, XERR_INTERRUPTED = -3, XERR_REMOTE = -4, XERR_SHAPE = -1 };
typedef struct {
    xpc_connection_t listener;
    dispatch_queue_t q;
    atomic_int peers, peers_gone, msgs;
    dispatch_semaphore_t peer_gone;
} xsrv_t;

/* 익명 리스너(xpc_connection_create(NULL, q))를 만들고 피어마다 핸들러를 단다.
 * 요청 {"op": "add"|"mul"|"pid", "a", "b"} → 응답 {"value": int64} 또는 {"error": "bad op"}.
 * 피어가 XPC_ERROR_CONNECTION_INVALID 를 받으면 peers_gone++ 와 semaphore signal. 반환 0 / -1. */
static int xpc_server_start(xsrv_t *s) {
    s->q = dispatch_queue_create("os-a14.xpc.server", DISPATCH_QUEUE_SERIAL);
    if (!s->q) return -1;
    s->listener = xpc_connection_create(NULL, s->q);
    if (!s->listener) return -1;
    s->peer_gone = dispatch_semaphore_create(0);
    xpc_connection_set_event_handler(s->listener, ^(xpc_object_t peer) {
        if (xpc_get_type(peer) != XPC_TYPE_CONNECTION) return;     /* 리스너 자신의 오류(취소 등) */
        atomic_fetch_add(&s->peers, 1);
        xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
            if (xpc_get_type(msg) == XPC_TYPE_ERROR) {
                if (msg == XPC_ERROR_CONNECTION_INVALID) {
                    atomic_fetch_add(&s->peers_gone, 1);
                    dispatch_semaphore_signal(s->peer_gone);
                }
                return;
            }
            if (xpc_get_type(msg) != XPC_TYPE_DICTIONARY) return;
            atomic_fetch_add(&s->msgs, 1);
            xpc_object_t rep = xpc_dictionary_create_reply(msg);
            if (!rep) return;                        /* 응답을 기대하지 않는 메시지 */
            const char *op = xpc_dictionary_get_string(msg, "op");
            int64_t a = xpc_dictionary_get_int64(msg, "a"), b = xpc_dictionary_get_int64(msg, "b");
            if (op && strcmp(op, "add") == 0)      xpc_dictionary_set_int64(rep, "value", (int64_t)((uint64_t)a + (uint64_t)b));
            else if (op && strcmp(op, "mul") == 0) xpc_dictionary_set_int64(rep, "value", (int64_t)((uint64_t)a * (uint64_t)b));
            else if (op && strcmp(op, "pid") == 0) xpc_dictionary_set_int64(rep, "value", xpc_connection_get_pid(peer));
            else                                   xpc_dictionary_set_string(rep, "error", "bad op");
            xpc_connection_send_message(peer, rep);
            xpc_release(rep);
        });
        xpc_connection_activate(peer);
    });
    xpc_connection_activate(s->listener);
    return 0;
}

/* 딕셔너리 요청을 보내고 동기로 응답을 받는다. 반환 0(+*out) / XERR_*. */
static int xpc_call(xpc_connection_t c, const char *op, int64_t a, int64_t b, int64_t *out) {
    xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(m, "op", op);
    xpc_dictionary_set_int64(m, "a", a);
    xpc_dictionary_set_int64(m, "b", b);
    xpc_object_t r = xpc_connection_send_message_with_reply_sync(c, m);
    xpc_release(m);
    int rc;
    if (r == XPC_ERROR_CONNECTION_INVALID) rc = XERR_INVALID;
    else if (r == XPC_ERROR_CONNECTION_INTERRUPTED) rc = XERR_INTERRUPTED;
    else if (xpc_get_type(r) != XPC_TYPE_DICTIONARY) rc = XERR_SHAPE;
    else if (xpc_dictionary_get_string(r, "error")) rc = XERR_REMOTE;
    else if (!xpc_dictionary_get_value(r, "value") ||
             xpc_get_type(xpc_dictionary_get_value(r, "value")) != XPC_TYPE_INT64) rc = XERR_SHAPE;
    else { *out = xpc_dictionary_get_int64(r, "value"); rc = 0; }
    xpc_release(r);
    return rc;
}

/* ============================ 테스트 ============================ */

typedef struct { mach_port_t port; server_stats_t st; atomic_bool stop; } server_t;
static void *server_thread(void *arg) {
    server_t *s = arg;
    while (!atomic_load(&s->stop) && !s->st.quit) {
        kern_return_t kr = server_handle_one(s->port, 20, &s->st);
        if (kr != KERN_SUCCESS && kr != MACH_RCV_TIMED_OUT) break;   /* 스텁(미구현) 등 → 끝낸다 */
    }
    return NULL;
}
static bool server_start(server_t *s, pthread_t *th) {
    memset(s, 0, sizeof *s);
    if (port_create(&s->port) != KERN_SUCCESS) return false;
    pthread_create(th, NULL, server_thread, s);
    return true;
}
static void server_stop(server_t *s, pthread_t th) {
    atomic_store(&s->stop, true);
    pthread_join(th, NULL);
    port_destroy(s->port);
}
static void client_init(rpc_client_t *c, mach_port_t server) {
    memset(c, 0, sizeof *c);
    c->server = server;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &c->reply);
}
static void client_fini(rpc_client_t *c) { mach_port_mod_refs(mach_task_self(), c->reply, MACH_PORT_RIGHT_RECEIVE, -1); }

typedef struct { mach_port_t srv; int id, ok; pthread_t t; } worker_t;
enum { NT = 4, PER = 250 };
static void *rpc_worker(void *arg) {
    worker_t *w = arg;
    rpc_client_t c; client_init(&c, w->srv);
    for (int i = 0; i < PER; i++) {
        int64_t v = 0, a = w->id * 1000 + i;
        if (rpc_call(&c, OP_ADD, a, w->id, &v, 2000) == KERN_SUCCESS && v == a + w->id) w->ok++;
    }
    client_fini(&c);
    return NULL;
}

static void test_port_create(void) {
    printf("--- A1: port_create — 권리와 uref ---\n");
    mach_port_t p = MACH_PORT_NULL;
    kern_return_t kr = port_create(&p), tk;
    CHECK(kr == KERN_SUCCESS && p != MACH_PORT_NULL);
    mach_port_type_t t = ptype(p, &tk);
    CHECK(tk == KERN_SUCCESS && (t & MACH_PORT_TYPE_RECEIVE) && (t & MACH_PORT_TYPE_SEND));
    CHECK(refs(p, MACH_PORT_RIGHT_RECEIVE) == 1 && refs(p, MACH_PORT_RIGHT_SEND) == 1);
    printf("      name=0x%x type=0x%x recv=%d send=%d\n", p, t, (int)refs(p, MACH_PORT_RIGHT_RECEIVE), (int)refs(p, MACH_PORT_RIGHT_SEND));
    if (kr == KERN_SUCCESS) port_destroy(p);
    if (kr == KERN_SUCCESS) { ptype(p, &tk); CHECK(tk == KERN_INVALID_NAME); }   /* 다 놓으면 이름이 사라진다 */
    else CHECK(!"port_create 실패");
}

static void test_rpc_many(void) {
    printf("--- A2: 요청/응답 1000 회 + 클라이언트 스레드 %d 개 ---\n", NT);
    server_t s; pthread_t th;
    if (!server_start(&s, &th)) { for (int i = 0; i < 4; i++) CHECK(!"port_create 실패"); return; }
    rpc_client_t c; client_init(&c, s.port);
    int ok = 0;
    for (int i = 0; i < 1000; i++) {
        int64_t v = 0, want = (i & 1) ? (int64_t)i * 7 : (int64_t)i + 7;
        if (rpc_call(&c, (i & 1) ? OP_MUL : OP_ADD, i, 7, &v, 2000) == KERN_SUCCESS && v == want) ok++;
    }
    CHECK(ok == 1000);
    int64_t v = 0;
    CHECK(rpc_call(&c, 99, 1, 2, &v, 2000) == MIG_BAD_ID);   /* 모르는 op → 서버가 status 로 거절 */
    client_fini(&c);
    worker_t ws[NT];
    for (int k = 0; k < NT; k++) { ws[k].srv = s.port; ws[k].id = k + 1; ws[k].ok = 0; pthread_create(&ws[k].t, NULL, rpc_worker, &ws[k]); }
    int tot = 0;
    for (int k = 0; k < NT; k++) { pthread_join(ws[k].t, NULL); tot += ws[k].ok; }
    CHECK(tot == NT * PER);
    server_stop(&s, th);
    CHECK(s.st.handled == 1000u + 1u + NT * PER && s.st.reply_failed == 0);
    printf("      server handled=%u bad=%u reply_failed=%u\n", s.st.handled, s.st.bad, s.st.reply_failed);
}

typedef struct { rpc_client_t c; kern_return_t kr; int64_t v; } call_ctx_t;
static void *one_call(void *arg) { call_ctx_t *x = arg; x->kr = rpc_call(&x->c, OP_ADD, 40, 2, &x->v, 3000); return NULL; }

static void test_send_once(void) {
    printf("--- A3: send-once 응답 권리는 한 번 쓰면 사라진다 ---\n");
    mach_port_t srv;
    if (port_create(&srv) != KERN_SUCCESS) { for (int i = 0; i < 6; i++) CHECK(!"port_create 실패"); return; }
    call_ctx_t x; memset(&x, 0, sizeof x); client_init(&x.c, srv);
    mach_port_t reply_port = x.c.reply;
    pthread_t t; pthread_create(&t, NULL, one_call, &x);
    /* 테스트 스레드가 손으로 서버 노릇: 요청을 받는다 */
    req_buf_t b; memset(&b, 0, sizeof b);
    kern_return_t kr = mach_msg(&b.m.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof b, srv, 2000, MACH_PORT_NULL);
    mach_port_t so = b.m.h.msgh_remote_port;
    CHECK(kr == KERN_SUCCESS && MACH_MSGH_BITS_REMOTE(b.m.h.msgh_bits) == MACH_MSG_TYPE_PORT_SEND_ONCE);
    CHECK(refs(so, MACH_PORT_RIGHT_SEND_ONCE) == 1);
    kern_return_t r1 = server_reply(so, b.m.seq, 0, 42);
    kern_return_t tk; ptype(so, &tk);
    kern_return_t r2 = server_reply(so, b.m.seq, 0, 43);      /* 같은 이름으로 두 번째 응답 */
    CHECK(r1 == KERN_SUCCESS && tk == KERN_INVALID_NAME);     /* 소비 → 이름 자체가 사라짐 */
    CHECK(r2 == MACH_SEND_INVALID_DEST);
    printf("      first reply kr=0x%x, name after=%s, second reply kr=0x%x (%s)\n", r1,
           tk == KERN_INVALID_NAME ? "KERN_INVALID_NAME" : "still valid", r2, mach_error_string(r2));
    pthread_join(t, NULL);
    CHECK(x.kr == KERN_SUCCESS && x.v == 42);
    rep_buf_t rb;                                             /* 응답은 정확히 1 개: 다음 수신은 타임아웃 */
    kr = mach_msg(&rb.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof rb, reply_port, 20, MACH_PORT_NULL);
    CHECK(kr == MACH_RCV_TIMED_OUT);
    client_fini(&x.c);
    port_destroy(srv);
}

static void test_server_died(void) {
    printf("--- A4: 서버가 응답 권리를 버리면 SEND_ONCE 알림 → MIG_SERVER_DIED ---\n");
    server_t s; pthread_t th;
    if (!server_start(&s, &th)) { for (int i = 0; i < 3; i++) CHECK(!"port_create 실패"); return; }
    rpc_client_t c; client_init(&c, s.port);
    int64_t v = 0;
    CHECK(rpc_call(&c, OP_DROP, 1, 2, &v, 2000) == MIG_SERVER_DIED);
    /* 형식이 틀린 요청(크기 부족) → 서버가 mach_msg_destroy → 역시 SEND_ONCE 알림 */
    struct { mach_msg_header_t h; uint32_t op; } bad; memset(&bad, 0, sizeof bad);
    bad.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    bad.h.msgh_size = sizeof bad; bad.h.msgh_remote_port = s.port; bad.h.msgh_local_port = c.reply; bad.h.msgh_id = REQ_ID;
    kern_return_t kr = mach_msg(&bad.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof bad, 0, MACH_PORT_NULL, 100, MACH_PORT_NULL);
    rep_buf_t rb; memset(&rb, 0, sizeof rb);
    kern_return_t kr2 = mach_msg(&rb.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof rb, c.reply, 2000, MACH_PORT_NULL);
    CHECK(kr == KERN_SUCCESS && kr2 == KERN_SUCCESS && rb.h.msgh_id == MACH_NOTIFY_SEND_ONCE);
    CHECK(rpc_call(&c, OP_ADD, 2, 3, &v, 2000) == KERN_SUCCESS && v == 5);   /* 서버는 계속 산다 */
    server_stop(&s, th);
    printf("      server bad=%u dropped=%u\n", s.st.bad, s.st.dropped);
    client_fini(&c);
}

static void test_timeout_and_late_reply(void) {
    printf("--- A5: 수신 타임아웃 → 응답 포트 교체 → 늦은 응답은 버려진다 ---\n");
    mach_port_t srv;
    if (port_create(&srv) != KERN_SUCCESS) { for (int i = 0; i < 5; i++) CHECK(!"port_create 실패"); return; }
    rpc_client_t c; client_init(&c, srv);
    mach_port_t old_reply = c.reply;
    int64_t v = -1;
    kern_return_t kr = rpc_call(&c, OP_ADD, 1, 1, &v, 30);     /* 아무도 서비스하지 않는다 */
    CHECK(kr == MACH_RCV_TIMED_OUT);
    CHECK(c.reply != old_reply && c.reply != MACH_PORT_NULL);
    server_stats_t st = {0};                                   /* 이제야 서버가 옛 요청을 처리 */
    kr = server_handle_one(srv, 0, &st);
    CHECK(kr == KERN_SUCCESS && st.handled == 1 && st.reply_failed == 1);   /* 응답 포트가 죽었다 */
    server_t s; memset(&s, 0, sizeof s); s.port = srv;
    pthread_t th; pthread_create(&th, NULL, server_thread, &s);
    kr = rpc_call(&c, OP_ADD, 20, 22, &v, 2000);
    CHECK(kr == KERN_SUCCESS && v == 42 && c.stale == 0);
    atomic_store(&s.stop, true); pthread_join(th, NULL);
    kern_return_t tk; ptype(old_reply, &tk);
    CHECK(tk == KERN_INVALID_NAME);                            /* 옛 응답 포트 이름은 dead name 도 남지 않았다 */
    client_fini(&c);
    port_destroy(srv);
}

static void test_port_descriptor(void) {
    printf("--- B1: 포트 디스크립터 — 같은 태스크면 같은 이름, uref +1 ---\n");
    mach_port_t p, x;
    if (port_create(&p) != KERN_SUCCESS || port_create(&x) != KERN_SUCCESS) { for (int i = 0; i < 4; i++) CHECK(!"port_create 실패"); return; }
    mach_port_urefs_t before = refs(x, MACH_PORT_RIGHT_SEND);
    kern_return_t kr = send_complex(p, x, MACH_MSG_TYPE_COPY_SEND, NULL, 0, 7);
    mach_port_urefs_t inflight = refs(x, MACH_PORT_RIGHT_SEND);
    cx_got_t g; memset(&g, 0, sizeof g);
    kern_return_t kr2 = recv_complex(p, 1000, &g);
    CHECK(kr == KERN_SUCCESS && kr2 == KERN_SUCCESS && g.tag == 7);
    CHECK(g.port == x && g.port_type == MACH_MSG_TYPE_PORT_SEND);
    CHECK(before == 1 && inflight == 1 && refs(x, MACH_PORT_RIGHT_SEND) == 2);
    printf("      send urefs: before=%d in-flight=%d after-receive=%d (received name 0x%x == 0x%x)\n",
           (int)before, (int)inflight, (int)refs(x, MACH_PORT_RIGHT_SEND), g.port, x);
    if (kr2 == KERN_SUCCESS) mach_port_deallocate(mach_task_self(), g.port);
    CHECK(refs(x, MACH_PORT_RIGHT_SEND) == 1);
    port_destroy(x); port_destroy(p);
}

static void test_ool(void) {
    printf("--- B2: OOL 가상 복사 — 보낸 순간의 스냅샷 ---\n");
    mach_port_t p;
    if (port_create(&p) != KERN_SUCCESS) { for (int i = 0; i < 4; i++) CHECK(!"port_create 실패"); return; }
    size_t n = 1u << 20, words = n / 8;
    vm_address_t a = 0;
    vm_allocate(mach_task_self(), &a, n, VM_FLAGS_ANYWHERE);
    uint64_t *src = (uint64_t *)a;
    for (size_t i = 0; i < words; i++) src[i] = pattern(i, 1);
    kern_return_t kr = send_complex(p, MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND, src, n, 9);
    for (size_t i = 0; i < words; i += 512) src[i] = 0xDEAD;  /* 보낸 뒤, 받기 전에 원본을 고친다 */
    cx_got_t g; memset(&g, 0, sizeof g);
    kern_return_t kr2 = recv_complex(p, 1000, &g);
    CHECK(kr == KERN_SUCCESS && kr2 == KERN_SUCCESS && g.len == n && g.port == MACH_PORT_NULL);
    long pg = sysconf(_SC_PAGESIZE);
    CHECK(g.buf != NULL && (void *)g.buf != (void *)src && ((uintptr_t)g.buf % (uintptr_t)pg) == 0);
    size_t bad = 0;
    if (g.buf) for (size_t i = 0; i < words; i++) bad += ((uint64_t *)g.buf)[i] != pattern(i, 1);
    CHECK(g.buf != NULL && bad == 0);                          /* 수신자는 수정 전 내용을 본다 (COW) */
    printf("      sender=%p receiver=%p len=%zu page=%ld mismatched words=%zu\n", (void *)src, g.buf, g.len, pg, bad);
    if (g.buf) vm_deallocate(mach_task_self(), (vm_address_t)g.buf, g.len);
    vm_deallocate(mach_task_self(), a, n);
    /* 잘못된 복합 메시지: 디스크립터 1 개뿐 → 거부, 실려 온 권리는 반납된다 */
    mach_port_t x; port_create(&x);
    struct { mach_msg_header_t h; mach_msg_body_t body; mach_msg_port_descriptor_t port; } one;
    memset(&one, 0, sizeof one);
    one.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
    one.h.msgh_size = sizeof one; one.h.msgh_remote_port = p; one.h.msgh_id = CX_ID;
    one.body.msgh_descriptor_count = 1;
    one.port.name = x; one.port.disposition = MACH_MSG_TYPE_COPY_SEND; one.port.type = MACH_MSG_PORT_DESCRIPTOR;
    kr = mach_msg(&one.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof one, 0, MACH_PORT_NULL, 100, MACH_PORT_NULL);
    memset(&g, 0, sizeof g);
    kr2 = recv_complex(p, 1000, &g);
    CHECK(kr == KERN_SUCCESS && kr2 == MIG_TYPE_ERROR && refs(x, MACH_PORT_RIGHT_SEND) == 1);
    port_destroy(x); port_destroy(p);
}

static void test_no_senders(void) {
    printf("--- B3: no-senders 알림 — 마지막 send right 가 사라질 때만 ---\n");
    mach_port_t svc, nq;
    if (port_create(&svc) != KERN_SUCCESS || port_create(&nq) != KERN_SUCCESS) { for (int i = 0; i < 4; i++) CHECK(!"port_create 실패"); return; }
    mach_port_insert_right(mach_task_self(), svc, svc, MACH_MSG_TYPE_MAKE_SEND);   /* send uref 2 */
    CHECK(arm_notify(svc, MACH_NOTIFY_NO_SENDERS, nq) == KERN_SUCCESS);
    mach_port_deallocate(mach_task_self(), svc);               /* uref 2 → 1: 아직 보낼 수 있는 자가 있다 */
    notify_got_t n; memset(&n, 0, sizeof n);
    CHECK(wait_notify(nq, 20, &n) == MACH_RCV_TIMED_OUT);
    mach_port_deallocate(mach_task_self(), svc);               /* 마지막 */
    kern_return_t kr = wait_notify(nq, 1000, &n);
    CHECK(kr == KERN_SUCCESS && n.id == MACH_NOTIFY_NO_SENDERS);
    printf("      notification id=%d (MACH_NOTIFY_NO_SENDERS=%d) mscount=%u\n", n.id, MACH_NOTIFY_NO_SENDERS, n.count);
    CHECK(refs(svc, MACH_PORT_RIGHT_SEND) == 0 && refs(svc, MACH_PORT_RIGHT_RECEIVE) == 1);
    port_destroy(svc); port_destroy(nq);
}

static void test_dead_name(void) {
    printf("--- B4: dead-name 알림 + dead name uref 정리 ---\n");
    mach_port_t d, nq;
    if (port_create(&d) != KERN_SUCCESS || port_create(&nq) != KERN_SUCCESS) { for (int i = 0; i < 4; i++) CHECK(!"port_create 실패"); return; }
    CHECK(arm_notify(d, MACH_NOTIFY_DEAD_NAME, nq) == KERN_SUCCESS);
    mach_port_mod_refs(mach_task_self(), d, MACH_PORT_RIGHT_RECEIVE, -1);   /* "상대 프로세스가 죽었다" */
    notify_got_t n; memset(&n, 0, sizeof n);
    kern_return_t kr = wait_notify(nq, 1000, &n), tk;
    mach_port_type_t t = ptype(d, &tk);
    CHECK(kr == KERN_SUCCESS && n.id == MACH_NOTIFY_DEAD_NAME && n.dead == d);
    CHECK(tk == KERN_SUCCESS && t == MACH_PORT_TYPE_DEAD_NAME && refs(d, MACH_PORT_RIGHT_DEAD_NAME) == 2);
    printf("      dead name 0x%x urefs=%d (send right 1 + 알림이 실어 온 1)\n", d, (int)refs(d, MACH_PORT_RIGHT_DEAD_NAME));
    mach_port_deallocate(mach_task_self(), d);
    mach_port_deallocate(mach_task_self(), d);
    ptype(d, &tk);
    CHECK(tk == KERN_INVALID_NAME);
    port_destroy(nq);
}

static void test_rcv_timeout(void) {
    printf("--- B5: 빈 포트에서 MACH_RCV_TIMEOUT ---\n");
    mach_port_t p;
    if (port_create(&p) != KERN_SUCCESS) { CHECK(!"port_create 실패"); return; }
    notify_got_t n;
    CHECK(wait_notify(p, 10, &n) == MACH_RCV_TIMED_OUT);
    port_destroy(p);
}

static void test_child_process(void) {
    printf("--- C1: fork + TASK_BOOTSTRAP_PORT 로 자식과 send right 교환 ---\n");
    mach_port_t saved = MACH_PORT_NULL, after = MACH_PORT_NULL;
    task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &saved);
    mach_port_t cs = MACH_PORT_NULL; cx_got_t hello; memset(&hello, 0, sizeof hello);
    pid_t pid = spawn_child_server(&cs, &hello);
    task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &after);
    CHECK(after == saved);                                     /* 특수 포트는 복원됐다 */
    mach_port_deallocate(mach_task_self(), saved); mach_port_deallocate(mach_task_self(), after);
    if (pid <= 0) { for (int i = 0; i < 6; i++) CHECK(!"spawn_child_server 실패"); return; }
    size_t bad = 0, words = hello.len / 8;
    for (size_t i = 0; i < words; i++) bad += ((uint64_t *)hello.buf)[i] != pattern(i, (uint32_t)pid);
    CHECK(hello.tag == (uint32_t)pid && hello.len == 65536 && bad == 0);   /* 자식이 OOL 로 보낸 64 KiB */
    vm_deallocate(mach_task_self(), (vm_address_t)hello.buf, hello.len);
    rpc_client_t c; client_init(&c, cs);
    int ok = 0;
    for (int i = 0; i < 200; i++) { int64_t v = 0; if (rpc_call(&c, OP_MUL, i, 3, &v, 2000) == KERN_SUCCESS && v == i * 3) ok++; }
    CHECK(ok == 200);
    mach_port_t nq; port_create(&nq);
    CHECK(arm_notify(cs, MACH_NOTIFY_DEAD_NAME, nq) == KERN_SUCCESS);
    int64_t v = 0;
    kern_return_t kq = rpc_call(&c, OP_QUIT, 0, 0, &v, 2000);   /* 자식은 답하고 _exit(0) */
    notify_got_t n; memset(&n, 0, sizeof n);
    kern_return_t kr = wait_notify(nq, 3000, &n);
    int st = wait_child(pid, 3000);
    CHECK(kq == KERN_SUCCESS && kr == KERN_SUCCESS && n.id == MACH_NOTIFY_DEAD_NAME && n.dead == cs);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    CHECK(rpc_call(&c, OP_ADD, 1, 1, &v, 200) == MACH_SEND_INVALID_DEST);   /* 죽은 이름으로 보내기 */
    printf("      child pid=%d, child's port seen here as 0x%x, dead-name id=%d, exit=%d\n", (int)pid, cs, n.id, WEXITSTATUS(st));
    mach_port_deallocate(mach_task_self(), cs); mach_port_deallocate(mach_task_self(), cs);
    client_fini(&c); port_destroy(nq);
}

static void test_child_killed(void) {
    printf("--- C2: 자식을 SIGKILL — 커널이 dead-name 으로 알려 준다 ---\n");
    mach_port_t cs = MACH_PORT_NULL; cx_got_t hello; memset(&hello, 0, sizeof hello);
    pid_t pid = spawn_child_server(&cs, &hello);
    if (pid <= 0) { for (int i = 0; i < 3; i++) CHECK(!"spawn_child_server 실패"); return; }
    vm_deallocate(mach_task_self(), (vm_address_t)hello.buf, hello.len);
    rpc_client_t c; client_init(&c, cs);
    int64_t v = 0;
    CHECK(rpc_call(&c, OP_ADD, 2, 2, &v, 2000) == KERN_SUCCESS && v == 4);
    mach_port_t nq; port_create(&nq);
    arm_notify(cs, MACH_NOTIFY_DEAD_NAME, nq);
    kill(pid, SIGKILL);
    notify_got_t n; memset(&n, 0, sizeof n);
    kern_return_t kr = wait_notify(nq, 3000, &n);
    int st = wait_child(pid, 3000);
    CHECK(kr == KERN_SUCCESS && n.id == MACH_NOTIFY_DEAD_NAME && n.dead == cs);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    if (kr == KERN_SUCCESS) mach_port_deallocate(mach_task_self(), cs);
    mach_port_deallocate(mach_task_self(), cs);
    client_fini(&c); port_destroy(nq);
}

static xsrv_t g_xs;                                            /* 블록이 붙잡으므로 전역 (수명 = 프로세스) */
static xpc_connection_t xpc_client(xpc_endpoint_t ep) {
    xpc_connection_t c = xpc_connection_create_from_endpoint(ep);
    xpc_connection_set_event_handler(c, ^(xpc_object_t e) { (void)e; });
    xpc_connection_activate(c);
    return c;
}
typedef struct { xpc_connection_t c; int id, ok; pthread_t t; } xworker_t;
static void *xpc_worker(void *arg) {
    xworker_t *w = arg;
    for (int i = 0; i < 50; i++) { int64_t v = 0; if (xpc_call(w->c, "add", w->id, i, &v) == 0 && v == w->id + i) w->ok++; }
    return NULL;
}

static void test_xpc(void) {
    printf("--- C3: XPC 익명 리스너 + 엔드포인트 ---\n");
    int rc = xpc_server_start(&g_xs);
    CHECK(rc == 0 && g_xs.listener != NULL);
    if (rc != 0 || !g_xs.listener) { for (int i = 0; i < 7; i++) CHECK(!"xpc_server_start 실패"); return; }
    xpc_endpoint_t ep = xpc_endpoint_create(g_xs.listener);
    xpc_connection_t c = xpc_client(ep);
    int ok = 0;
    for (int i = 0; i < 100; i++) { int64_t v = 0; if (xpc_call(c, (i & 1) ? "mul" : "add", i, 5, &v) == 0 && v == ((i & 1) ? i * 5 : i + 5)) ok++; }
    CHECK(ok == 100);
    int64_t v = 0;
    CHECK(xpc_call(c, "pid", 0, 0, &v) == 0 && v == getpid());   /* 커널이 붙여 준 피어 신원 */
    CHECK(xpc_call(c, "nope", 0, 0, &v) == XERR_REMOTE);
    xworker_t ws[3];
    for (int k = 0; k < 3; k++) { ws[k].c = xpc_client(ep); ws[k].id = (k + 1) * 100; ws[k].ok = 0; pthread_create(&ws[k].t, NULL, xpc_worker, &ws[k]); }
    int tot = 0;
    for (int k = 0; k < 3; k++) { pthread_join(ws[k].t, NULL); tot += ws[k].ok; }
    CHECK(tot == 150);
    /* 클라이언트 취소 → 서버 피어 핸들러가 CONNECTION_INVALID, 이후 호출도 INVALID */
    xpc_connection_cancel(c);
    long w = dispatch_semaphore_wait(g_xs.peer_gone, dispatch_time(DISPATCH_TIME_NOW, 3 * (int64_t)NSEC_PER_SEC));
    CHECK(w == 0 && atomic_load(&g_xs.peers_gone) >= 1);
    CHECK(xpc_call(c, "add", 1, 1, &v) == XERR_INVALID);
    xpc_release(c);
    /* 리스너 취소 → 남은 연결도 INVALID (이름 없는 엔드포인트라 다시 찾을 수 없다) */
    xpc_connection_cancel(g_xs.listener);
    int got = 0;
    for (int tries = 0; tries < 300 && got != XERR_INVALID; tries++) {
        got = xpc_call(ws[0].c, "add", 1, 1, &v);
        if (got != XERR_INVALID) usleep(10000);
    }
    CHECK(got == XERR_INVALID);
    printf("      peers=%d peers_gone=%d msgs=%d\n", atomic_load(&g_xs.peers), atomic_load(&g_xs.peers_gone), atomic_load(&g_xs.msgs));
    for (int k = 0; k < 3; k++) { xpc_connection_cancel(ws[k].c); xpc_release(ws[k].c); }
    xpc_release(ep);
}

static void *warm_up(void *arg) { return arg; }

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    alarm(60);                                                 /* 하네스 전체 워치독 */
    pthread_t warm; pthread_create(&warm, NULL, warm_up, NULL); pthread_join(warm, NULL);   /* 런타임(TSan 등) 지연 초기화 */
    unsigned names0 = name_count();
    test_port_create();
    test_rpc_many();
    test_send_once();
    test_server_died();
    test_timeout_and_late_reply();
    test_port_descriptor();
    test_ool();
    test_no_senders();
    test_dead_name();
    test_rcv_timeout();
    test_child_process();
    test_child_killed();
    unsigned names1 = name_count();
    CHECK(names1 == names0);                                   /* Part A–C(Mach) 전체: 이름 누수 없음 */
    printf("      port names: start=%u end=%u\n", names0, names1);
    test_xpc();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
