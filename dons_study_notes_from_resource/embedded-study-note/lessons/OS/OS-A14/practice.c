/* OS-A14 추가 연습 — 한 번에 검증   (macOS 전용) */
/* BUILD: -lbsm */
/*
 *   연습 1  포트 셋: 한 스레드가 두 서비스 포트를 함께 기다리고 msgh_local_port 로 어느 서비스인지 구별
 *   연습 2  큐 한도(qlimit) 역압: 기본 5 → MACH_SEND_TIMED_OUT, 1 로 줄이기, send-once 는 한도를 무시
 *   연습 3  보낸 사람 신원: audit trailer → audit_token_to_pid (자기 자신 / fork 한 자식)
 *   연습 4  MIG 흉내: msgh_id 디스패치 표 + libSystem mach_msg_server_once, 모르는 id → MIG_BAD_ID
 *   연습 5  XPC 비동기 응답 + 데드라인: 늦은 응답이 와도 해제된 메모리를 만지지 않게(참조 카운트)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -lbsm -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <bsm/libbsm.h>
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

static mach_port_t port_new(void) {
    mach_port_t p = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p);
    mach_port_insert_right(mach_task_self(), p, p, MACH_MSG_TYPE_MAKE_SEND);
    return p;
}
static void port_kill(mach_port_t p) {
    mach_port_urefs_t s = 0;
    mach_port_get_refs(mach_task_self(), p, MACH_PORT_RIGHT_SEND, &s);
    if (s) mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_SEND, -(mach_port_delta_t)s);
    mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_RECEIVE, -1);
}
typedef struct { mach_msg_header_t h; int32_t v; } small_t;
static kern_return_t send_small(mach_port_t dest, mach_msg_type_name_t disp, mach_port_t reply, mach_msg_id_t id, int32_t v, mach_msg_timeout_t ms) {
    small_t m; memset(&m, 0, sizeof m);
    m.h.msgh_bits = MACH_MSGH_BITS(disp, reply ? MACH_MSG_TYPE_MAKE_SEND_ONCE : 0);
    m.h.msgh_size = sizeof m; m.h.msgh_remote_port = dest; m.h.msgh_local_port = reply; m.h.msgh_id = id; m.v = v;
    return mach_msg(&m.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof m, 0, MACH_PORT_NULL, ms, MACH_PORT_NULL);
}
typedef union { mach_msg_header_t h; small_t s; uint8_t raw[sizeof(small_t) + sizeof(mach_msg_max_trailer_t) + 64]; } small_buf_t;

/* ---------- 연습 1: 포트 셋 ---------- */
static void ex1_port_set(void) {
    printf("--- 연습 1: 포트 셋 ---\n");
    mach_port_t a = port_new(), b = port_new(), set = MACH_PORT_NULL;
    CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &set) == KERN_SUCCESS);
    CHECK(mach_port_insert_member(mach_task_self(), a, set) == KERN_SUCCESS &&
          mach_port_insert_member(mach_task_self(), b, set) == KERN_SUCCESS);
    for (int i = 0; i < 6; i++) send_small((i % 3) ? a : b, MACH_MSG_TYPE_COPY_SEND, MACH_PORT_NULL, 10, i, 100);
    int from_a = 0, from_b = 0, other = 0;
    for (int i = 0; i < 6; i++) {
        small_buf_t r; memset(&r, 0, sizeof r);
        if (mach_msg(&r.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof r, set, 500, MACH_PORT_NULL) != KERN_SUCCESS) { other++; continue; }
        if (r.h.msgh_local_port == a) from_a++; else if (r.h.msgh_local_port == b) from_b++; else other++;
    }
    CHECK(from_a == 4 && from_b == 2 && other == 0);
    small_buf_t r;
    CHECK(mach_msg(&r.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof r, set, 10, MACH_PORT_NULL) == MACH_RCV_TIMED_OUT);
    port_kill(a); port_kill(b);
    mach_port_mod_refs(mach_task_self(), set, MACH_PORT_RIGHT_PORT_SET, -1);
}

/* ---------- 연습 2: 큐 한도 ---------- */
static mach_port_msgcount_t qstat(mach_port_t p, mach_port_msgcount_t *qlimit) {
    mach_port_status_t ps; mach_msg_type_number_t c = MACH_PORT_RECEIVE_STATUS_COUNT;
    memset(&ps, 0, sizeof ps);
    mach_port_get_attributes(mach_task_self(), p, MACH_PORT_RECEIVE_STATUS, (mach_port_info_t)&ps, &c);
    *qlimit = ps.mps_qlimit;
    return ps.mps_msgcount;
}
static void drain(mach_port_t p) {
    small_buf_t r;
    while (mach_msg(&r.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof r, p, 0, MACH_PORT_NULL) == KERN_SUCCESS) mach_msg_destroy(&r.h);
}
static void ex2_qlimit(void) {
    printf("--- 연습 2: 큐 한도 역압 ---\n");
    mach_port_t p = port_new();
    mach_port_msgcount_t ql = 0;
    int ok = 0; kern_return_t last = KERN_SUCCESS;
    for (int i = 0; i < 8 && last == KERN_SUCCESS; i++) { last = send_small(p, MACH_MSG_TYPE_COPY_SEND, MACH_PORT_NULL, 20, i, 0); if (last == KERN_SUCCESS) ok++; }
    mach_port_msgcount_t n = qstat(p, &ql);
    CHECK(ok == MACH_PORT_QLIMIT_DEFAULT && last == MACH_SEND_TIMED_OUT && ql == MACH_PORT_QLIMIT_DEFAULT && n == 5);
    /* 꽉 찬 큐에도 send-once 메시지는 들어간다 (응답이 막히면 교착이 되므로) */
    mach_port_t so = MACH_PORT_NULL; mach_msg_type_name_t t;
    mach_port_extract_right(mach_task_self(), p, MACH_MSG_TYPE_MAKE_SEND_ONCE, &so, &t);
    kern_return_t kso = send_small(so, MACH_MSG_TYPE_MOVE_SEND_ONCE, MACH_PORT_NULL, 21, 0, 0);
    n = qstat(p, &ql);
    CHECK(kso == KERN_SUCCESS && n == 6);
    printf("      default qlimit=%u, accepted=%d, then 0x%x (MACH_SEND_TIMED_OUT); send-once → msgcount=%u\n", ql, ok, last, n);
    drain(p);
    mach_port_limits_t lim = { .mpl_qlimit = 1 };
    CHECK(mach_port_set_attributes(mach_task_self(), p, MACH_PORT_LIMITS_INFO, (mach_port_info_t)&lim, MACH_PORT_LIMITS_INFO_COUNT) == KERN_SUCCESS);
    kern_return_t k1 = send_small(p, MACH_MSG_TYPE_COPY_SEND, MACH_PORT_NULL, 22, 0, 0);
    kern_return_t k2 = send_small(p, MACH_MSG_TYPE_COPY_SEND, MACH_PORT_NULL, 22, 1, 0);
    CHECK(k1 == KERN_SUCCESS && k2 == MACH_SEND_TIMED_OUT);
    drain(p);
    port_kill(p);
}

/* ---------- 연습 3: audit trailer 로 보낸 사람 pid ---------- */
typedef struct { small_t s; mach_msg_audit_trailer_t t; uint8_t pad[64]; } audit_buf_t;
static kern_return_t recv_with_audit(mach_port_t p, mach_msg_timeout_t ms, int32_t *v, pid_t *sender) {
    audit_buf_t b; memset(&b, 0, sizeof b);
    kern_return_t kr = mach_msg(&b.s.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT |
                                MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
                                0, sizeof b, p, ms, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) return kr;
    if (b.t.msgh_trailer_size < sizeof(mach_msg_audit_trailer_t)) return MIG_TRAILER_ERROR;
    *v = b.s.v;
    *sender = audit_token_to_pid(b.t.msgh_audit);     /* 커널이 채운다 — 보낸 쪽이 위조할 수 없다 */
    return KERN_SUCCESS;
}
static void ex3_audit(void) {
    printf("--- 연습 3: audit trailer ---\n");
    mach_port_t p = port_new();
    int32_t v = 0; pid_t who = -1;
    send_small(p, MACH_MSG_TYPE_COPY_SEND, MACH_PORT_NULL, 30, 123, 100);
    CHECK(recv_with_audit(p, 500, &v, &who) == KERN_SUCCESS && v == 123 && who == getpid());
    /* fork 한 자식이 보낸 메시지: TASK_BOOTSTRAP_PORT 트릭으로 send right 를 물려준다 (본문 Part C) */
    mach_port_t saved = MACH_PORT_NULL;
    task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &saved);
    task_set_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, p);
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        alarm(5);
        mach_port_t bs = MACH_PORT_NULL;
        task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bs);
        _exit(send_small(bs, MACH_MSG_TYPE_COPY_SEND, MACH_PORT_NULL, 31, 456, 1000) == KERN_SUCCESS ? 0 : 1);
    }
    task_set_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, saved);
    mach_port_deallocate(mach_task_self(), saved);
    kern_return_t kr = c > 0 ? recv_with_audit(p, 2000, &v, &who) : KERN_FAILURE;
    int st = 0;
    if (c > 0) waitpid(c, &st, 0);
    CHECK(kr == KERN_SUCCESS && v == 456 && who == c && who != getpid());
    CHECK(c > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    printf("      me=%d child=%d audit pid=%d\n", (int)getpid(), (int)c, (int)who);
    port_kill(p);
}

/* ---------- 연습 4: MIG 흉내 ---------- */
#define SVC_BASE 2000
typedef struct { mach_msg_header_t h; NDR_record_t ndr; int32_t a, b; } add_req_t;
typedef struct { mach_msg_header_t h; NDR_record_t ndr; kern_return_t ret; int32_t sum; } add_rep_t;
static void routine_add(mach_msg_header_t *in, mach_msg_header_t *out) {
    add_req_t *q = (add_req_t *)in; add_rep_t *r = (add_rep_t *)out;
    if (in->msgh_size != sizeof *q) { ((mig_reply_error_t *)out)->RetCode = MIG_BAD_ARGUMENTS; return; }
    r->ndr = NDR_record; r->ret = KERN_SUCCESS; r->sum = (int32_t)((uint32_t)q->a + (uint32_t)q->b);
    out->msgh_size = sizeof *r;
}
typedef void (*routine_t)(mach_msg_header_t *, mach_msg_header_t *);
static const routine_t g_routines[] = { routine_add };       /* id = SVC_BASE + index */

/* MIG 가 만드는 *_server 함수와 같은 모양: 응답 헤더를 채우고, 모르는 id 는 MIG_BAD_ID 로 답한다 */
static boolean_t demux(mach_msg_header_t *in, mach_msg_header_t *out) {
    mig_reply_error_t *e = (mig_reply_error_t *)out;
    out->msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(in->msgh_bits), 0);
    out->msgh_remote_port = in->msgh_remote_port;
    out->msgh_local_port = MACH_PORT_NULL;
    out->msgh_voucher_port = MACH_PORT_NULL;
    out->msgh_id = in->msgh_id + 100;
    out->msgh_size = sizeof *e;
    e->NDR = NDR_record;
    mach_msg_id_t idx = in->msgh_id - SVC_BASE;
    if (idx < 0 || idx >= (mach_msg_id_t)(sizeof g_routines / sizeof g_routines[0])) { e->RetCode = MIG_BAD_ID; return FALSE; }
    e->RetCode = KERN_SUCCESS;
    g_routines[idx](in, out);
    return TRUE;
}
static void ex4_mig(void) {
    printf("--- 연습 4: MIG 흉내 (mach_msg_server_once) ---\n");
    mach_port_t svc = port_new(), reply = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &reply);
    add_req_t q; memset(&q, 0, sizeof q);
    q.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    q.h.msgh_size = sizeof q; q.h.msgh_remote_port = svc; q.h.msgh_local_port = reply; q.h.msgh_id = SVC_BASE;
    q.ndr = NDR_record; q.a = 30; q.b = 12;
    kern_return_t ks = mach_msg(&q.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof q, 0, MACH_PORT_NULL, 100, MACH_PORT_NULL);
    /* 요청이 이미 큐에 있으므로 server_once 는 막히지 않는다 */
    kern_return_t kr = mach_msg_server_once(demux, 256, svc, 0);
    union { add_rep_t r; mig_reply_error_t e; uint8_t raw[256]; } rb; memset(&rb, 0, sizeof rb);
    kern_return_t kr2 = mach_msg(&rb.r.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof rb, reply, 500, MACH_PORT_NULL);
    CHECK(ks == KERN_SUCCESS && kr == KERN_SUCCESS && kr2 == KERN_SUCCESS && rb.r.h.msgh_id == SVC_BASE + 100 && rb.r.ret == 0 && rb.r.sum == 42);
    int32_t sum = rb.r.sum;
    q.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    q.h.msgh_size = sizeof q; q.h.msgh_remote_port = svc; q.h.msgh_local_port = reply; q.h.msgh_id = SVC_BASE + 7;
    mach_msg(&q.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof q, 0, MACH_PORT_NULL, 100, MACH_PORT_NULL);
    kr = mach_msg_server_once(demux, 256, svc, 0);
    memset(&rb, 0, sizeof rb);
    kr2 = mach_msg(&rb.r.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof rb, reply, 500, MACH_PORT_NULL);
    CHECK(kr == KERN_SUCCESS && kr2 == KERN_SUCCESS && rb.e.RetCode == MIG_BAD_ID);
    printf("      add → sum=%d; unknown id → RetCode=%d (MIG_BAD_ID=%d)\n", sum, rb.e.RetCode, MIG_BAD_ID);
    port_kill(svc);
    mach_port_mod_refs(mach_task_self(), reply, MACH_PORT_RIGHT_RECEIVE, -1);
}

/* ---------- 연습 5: XPC 비동기 응답 + 데드라인 ---------- */
typedef struct { atomic_int refs; atomic_int late; int64_t value; dispatch_semaphore_t done, late_done; } call_t;
static void call_put(call_t *c) {
    if (atomic_fetch_sub(&c->refs, 1) == 1) { dispatch_release(c->done); dispatch_release(c->late_done); free(c); }
}
static atomic_int g_late_replies;
/* 반환 0 = 시한 안에 응답, 1 = 시한 초과 (응답 핸들러는 나중에 와도 안전) */
static int call_with_deadline(xpc_connection_t conn, dispatch_queue_t rq, const char *op, int64_t ms, int64_t *out) {
    call_t *c = calloc(1, sizeof *c);
    atomic_init(&c->refs, 2);                         /* 호출자 1 + 응답 핸들러 1 */
    c->done = dispatch_semaphore_create(0);
    c->late_done = dispatch_semaphore_create(0);
    xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(m, "op", op);
    xpc_connection_send_message_with_reply(conn, m, rq, ^(xpc_object_t r) {
        if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) c->value = xpc_dictionary_get_int64(r, "value");
        if (atomic_exchange(&c->late, 1) == 1) {      /* 호출자는 이미 포기했다 */
            atomic_fetch_add(&g_late_replies, 1);
            dispatch_semaphore_signal(c->late_done);
        } else dispatch_semaphore_signal(c->done);
        call_put(c);
    });
    xpc_release(m);
    long w = dispatch_semaphore_wait(c->done, dispatch_time(DISPATCH_TIME_NOW, ms * (int64_t)NSEC_PER_MSEC));
    int rc;
    if (w == 0) { *out = c->value; rc = 0; }
    else if (atomic_exchange(&c->late, 1) == 1) {     /* 시한과 응답이 동시에: 응답이 이겼다 */
        dispatch_semaphore_wait(c->done, DISPATCH_TIME_FOREVER);
        *out = c->value; rc = 0;
    } else rc = 1;
    call_put(c);
    return rc;
}
static void ex5_xpc_deadline(void) {
    printf("--- 연습 5: XPC 비동기 응답 + 데드라인 ---\n");
    dispatch_queue_t sq = dispatch_queue_create("os-a14.pr.server", DISPATCH_QUEUE_SERIAL);
    dispatch_queue_t rq = dispatch_queue_create("os-a14.pr.reply", DISPATCH_QUEUE_SERIAL);
    dispatch_semaphore_t slow_replied = dispatch_semaphore_create(0);
    xpc_connection_t lis = xpc_connection_create(NULL, sq);
    xpc_connection_set_event_handler(lis, ^(xpc_object_t peer) {
        if (xpc_get_type(peer) != XPC_TYPE_CONNECTION) return;
        xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
            if (xpc_get_type(msg) != XPC_TYPE_DICTIONARY) return;
            const char *op = xpc_dictionary_get_string(msg, "op");
            xpc_object_t rep = xpc_dictionary_create_reply(msg);
            if (!rep) return;
            xpc_dictionary_set_int64(rep, "value", 7);
            if (op && strcmp(op, "slow") == 0) {      /* 300 ms 뒤에 답한다 (큐를 막지 않고) */
                xpc_retain(peer);
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * (int64_t)NSEC_PER_MSEC), sq, ^{
                    xpc_connection_send_message(peer, rep);
                    xpc_release(rep); xpc_release(peer);
                    dispatch_semaphore_signal(slow_replied);
                });
            } else { xpc_connection_send_message(peer, rep); xpc_release(rep); }
        });
        xpc_connection_activate(peer);
    });
    xpc_connection_activate(lis);
    xpc_endpoint_t ep = xpc_endpoint_create(lis);
    xpc_connection_t c = xpc_connection_create_from_endpoint(ep);
    xpc_connection_set_event_handler(c, ^(xpc_object_t e) { (void)e; });
    xpc_connection_activate(c);
    int64_t v = 0;
    CHECK(call_with_deadline(c, rq, "fast", 2000, &v) == 0 && v == 7);
    v = 0;
    CHECK(call_with_deadline(c, rq, "slow", 50, &v) == 1 && v == 0);      /* 시한 초과 */
    long w1 = dispatch_semaphore_wait(slow_replied, dispatch_time(DISPATCH_TIME_NOW, 3 * (int64_t)NSEC_PER_SEC));
    /* 늦은 응답 핸들러가 돌았는지: 전역 카운터로 확인 (call_t 는 핸들러가 마지막으로 해제) */
    int late = 0;
    for (int i = 0; i < 300 && (late = atomic_load(&g_late_replies)) == 0; i++) usleep(10000);
    CHECK(w1 == 0 && late == 1);
    CHECK(call_with_deadline(c, rq, "fast", 2000, &v) == 0 && v == 7);   /* 연결은 계속 쓸 수 있다 */
    xpc_connection_cancel(c); xpc_release(c);
    xpc_connection_cancel(lis); xpc_release(lis); xpc_release(ep);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    ex1_port_set();
    ex2_qlimit();
    ex3_audit();
    ex4_mig();
    ex5_xpc_deadline();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
