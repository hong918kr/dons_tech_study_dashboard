/* OS-A14 demo_bench — 왕복 지연(RTT): mach_msg vs pipe vs Unix socket vs XPC, 그리고 inline vs OOL 복사 비용
 *
 *   1) 스레드 2 개 핑퐁 (16 B 페이로드). 각 방식 20000 회(+2000 워밍업) × 3 회, min / p50 / p99 (ns).
 *      mach-2call : 클라이언트 send 1 번 + receive 1 번, 서버 receive 1 번 + send 1 번 (mach_msg 4 번/왕복)
 *      mach-1call : 클라이언트 SEND|RCV 한 번, 서버도 "응답 + 다음 요청 받기" 를 SEND|RCV 한 번 (2 번/왕복)
 *      pipe       : 파이프 2 개, write 16 B / read 16 B
 *      socket     : socketpair(AF_UNIX, SOCK_STREAM)
 *      xpc        : 익명 리스너 + xpc_connection_send_message_with_reply_sync (딕셔너리 1 키)
 *   2) 한 스레드에서 보내고 바로 받기 (메시지 1 개짜리 큐): 크기별 inline / OOL / OOL+수신측이 전부 읽기 / memcpy.
 *
 * 숫자는 공유·부하 걸린 맥에서 잰 것 — 비율만 본다. 시간은 CHECK 에 쓰지 않는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _DARWIN_C_SOURCE
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <xpc/xpc.h>

enum { ITERS = 20000, WARM = 2000, RUNS = 3 };
static mach_timebase_info_data_t tb;
static inline uint64_t now_ns(void) { return mach_absolute_time() * tb.numer / tb.denom; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t g_lat[ITERS];
typedef struct { uint64_t min, p50, p99; } stat_t;
static stat_t stats(void) {
    qsort(g_lat, ITERS, sizeof g_lat[0], cmp_u64);
    return (stat_t){ g_lat[0], g_lat[ITERS / 2], g_lat[(size_t)ITERS * 99 / 100] };
}

/* ---------------- mach ---------------- */
typedef struct { mach_msg_header_t h; uint32_t seq, quit; uint64_t pad; } pmsg_t;   /* 헤더 + 16 B */
typedef union { mach_msg_header_t h; pmsg_t m; uint8_t raw[sizeof(pmsg_t) + sizeof(mach_msg_max_trailer_t)]; } pbuf_t;
static mach_port_t g_srv;
static int g_combined;

static void *mach_server(void *arg) {
    (void)arg;
    pbuf_t b;
    if (mach_msg(&b.h, MACH_RCV_MSG, 0, sizeof b, g_srv, 0, MACH_PORT_NULL) != KERN_SUCCESS) return NULL;
    for (;;) {
        int quit = b.m.quit;
        mach_port_t reply = b.h.msgh_remote_port;
        b.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
        b.h.msgh_size = sizeof(pmsg_t);
        b.h.msgh_remote_port = reply;
        b.h.msgh_local_port = MACH_PORT_NULL;
        if (quit) { mach_msg(&b.h, MACH_SEND_MSG, sizeof(pmsg_t), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL); return NULL; }
        if (g_combined) {           /* 응답 보내기 + 다음 요청 기다리기를 한 번의 트랩으로 */
            if (mach_msg(&b.h, MACH_SEND_MSG | MACH_RCV_MSG, sizeof(pmsg_t), sizeof b, g_srv, 0, MACH_PORT_NULL) != KERN_SUCCESS) return NULL;
        } else {
            if (mach_msg(&b.h, MACH_SEND_MSG, sizeof(pmsg_t), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL) != KERN_SUCCESS) return NULL;
            if (mach_msg(&b.h, MACH_RCV_MSG, 0, sizeof b, g_srv, 0, MACH_PORT_NULL) != KERN_SUCCESS) return NULL;
        }
    }
}
static stat_t bench_mach(int combined) {
    g_combined = combined;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_srv);
    mach_port_insert_right(mach_task_self(), g_srv, g_srv, MACH_MSG_TYPE_MAKE_SEND);
    mach_port_t reply; mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &reply);
    pthread_t th; pthread_create(&th, NULL, mach_server, NULL);
    pbuf_t b;
    for (int i = -WARM; i <= ITERS; i++) {
        memset(&b.m, 0, sizeof b.m);
        b.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
        b.h.msgh_size = sizeof(pmsg_t); b.h.msgh_remote_port = g_srv; b.h.msgh_local_port = reply;
        b.m.seq = (uint32_t)i; b.m.quit = (i == ITERS);
        uint64_t t0 = now_ns();
        if (combined) mach_msg(&b.h, MACH_SEND_MSG | MACH_RCV_MSG, sizeof(pmsg_t), sizeof b, reply, 0, MACH_PORT_NULL);
        else {
            mach_msg(&b.h, MACH_SEND_MSG, sizeof(pmsg_t), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
            mach_msg(&b.h, MACH_RCV_MSG, 0, sizeof b, reply, 0, MACH_PORT_NULL);
        }
        uint64_t t1 = now_ns();
        if (i >= 0 && i < ITERS) g_lat[i] = t1 - t0;
    }
    pthread_join(th, NULL);
    mach_port_mod_refs(mach_task_self(), g_srv, MACH_PORT_RIGHT_SEND, -1);
    mach_port_mod_refs(mach_task_self(), g_srv, MACH_PORT_RIGHT_RECEIVE, -1);
    mach_port_mod_refs(mach_task_self(), reply, MACH_PORT_RIGHT_RECEIVE, -1);
    return stats();
}

/* ---------------- fd 기반 (pipe / socketpair) ---------------- */
static int g_req[2], g_rep[2];
static ssize_t xfer(int fd, void *b, size_t n, int wr) {
    size_t d = 0;
    while (d < n) { ssize_t r = wr ? write(fd, (char *)b + d, n - d) : read(fd, (char *)b + d, n - d); if (r <= 0) return -1; d += (size_t)r; }
    return (ssize_t)d;
}
static void *fd_server(void *arg) {
    (void)arg;
    uint8_t m[16];
    for (;;) {
        if (xfer(g_req[0], m, 16, 0) < 0) return NULL;
        if (xfer(g_rep[1], m, 16, 1) < 0) return NULL;
        if (m[0] == 0xFF) return NULL;
    }
}
static stat_t bench_fd(int sock) {
    if (sock) { socketpair(AF_UNIX, SOCK_STREAM, 0, g_req); socketpair(AF_UNIX, SOCK_STREAM, 0, g_rep); }
    else { if (pipe(g_req) || pipe(g_rep)) exit(1); }
    pthread_t th; pthread_create(&th, NULL, fd_server, NULL);
    uint8_t m[16] = {0};
    for (int i = -WARM; i <= ITERS; i++) {
        m[0] = (i == ITERS) ? 0xFF : 1;
        uint64_t t0 = now_ns();
        xfer(g_req[1], m, 16, 1);
        xfer(g_rep[0], m, 16, 0);
        uint64_t t1 = now_ns();
        if (i >= 0 && i < ITERS) g_lat[i] = t1 - t0;
    }
    pthread_join(th, NULL);
    close(g_req[0]); close(g_req[1]); close(g_rep[0]); close(g_rep[1]);
    return stats();
}

/* ---------------- XPC ---------------- */
static stat_t bench_xpc(void) {
    dispatch_queue_t q = dispatch_queue_create("os-a14.bench", DISPATCH_QUEUE_SERIAL);
    xpc_connection_t lis = xpc_connection_create(NULL, q);
    xpc_connection_set_event_handler(lis, ^(xpc_object_t peer) {
        if (xpc_get_type(peer) != XPC_TYPE_CONNECTION) return;
        xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
            if (xpc_get_type(msg) != XPC_TYPE_DICTIONARY) return;
            xpc_object_t r = xpc_dictionary_create_reply(msg);
            xpc_dictionary_set_uint64(r, "v", xpc_dictionary_get_uint64(msg, "v"));
            xpc_connection_send_message(peer, r);
            xpc_release(r);
        });
        xpc_connection_activate(peer);
    });
    xpc_connection_activate(lis);
    xpc_endpoint_t ep = xpc_endpoint_create(lis);
    xpc_connection_t c = xpc_connection_create_from_endpoint(ep);
    xpc_connection_set_event_handler(c, ^(xpc_object_t e) { (void)e; });
    xpc_connection_activate(c);
    for (int i = -WARM; i < ITERS; i++) {
        uint64_t t0 = now_ns();
        xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(m, "v", (uint64_t)i);
        xpc_object_t r = xpc_connection_send_message_with_reply_sync(c, m);
        uint64_t t1 = now_ns();
        xpc_release(r); xpc_release(m);
        if (i >= 0) g_lat[i] = t1 - t0;
    }
    xpc_connection_cancel(c); xpc_release(c);
    xpc_connection_cancel(lis); xpc_release(lis); xpc_release(ep);
    return stats();
}

/* ---------------- inline vs OOL ---------------- */
typedef struct { mach_msg_header_t h; mach_msg_body_t body; mach_msg_ool_descriptor_t ool; } ool_msg_t;
typedef union { mach_msg_header_t h; ool_msg_t m; uint8_t raw[sizeof(ool_msg_t) + sizeof(mach_msg_max_trailer_t) + 64]; } ool_buf_t;
static volatile uint64_t g_sink;
static double us_per(uint64_t ns, int n) { return (double)ns / n / 1000.0; }
static void bench_sizes(void) {
    mach_port_t p; mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p);
    mach_port_insert_right(mach_task_self(), p, p, MACH_MSG_TYPE_MAKE_SEND);
    printf("\n%-9s %8s %10s %10s %14s %10s\n", "size", "iters", "inline", "OOL", "OOL+read-all", "memcpy");
    size_t sizes[] = { 4096, 65536, 1u << 20, 8u << 20 };
    for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        size_t n = sizes[si];
        int iters = (int)((256u << 20) / n); if (iters > 4000) iters = 4000; if (iters < 30) iters = 30;
        vm_address_t src = 0; vm_allocate(mach_task_self(), &src, n, VM_FLAGS_ANYWHERE);
        memset((void *)src, 0x5A, n);
        size_t cap = sizeof(mach_msg_header_t) + n + sizeof(mach_msg_max_trailer_t) + 64;
        uint8_t *sb = malloc(cap), *rb = malloc(cap), *dst = malloc(n);
        memset(rb, 0, cap); memset(dst, 0, n);
        /* inline: 헤더 뒤에 n 바이트를 그대로 싣는다 → 보낼 때 커널로 1 번, 받을 때 사용자로 1 번 복사 */
        mach_msg_header_t *h = (mach_msg_header_t *)sb;
        memcpy(sb + sizeof *h, (void *)src, n);
        uint64_t t0 = now_ns();
        for (int i = 0; i < iters; i++) {
            h->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0); h->msgh_size = (mach_msg_size_t)(sizeof *h + n);
            h->msgh_remote_port = p; h->msgh_local_port = MACH_PORT_NULL; h->msgh_id = 1;
            mach_msg(h, MACH_SEND_MSG, h->msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
            mach_msg((mach_msg_header_t *)rb, MACH_RCV_MSG, 0, (mach_msg_size_t)cap, p, 0, MACH_PORT_NULL);
        }
        uint64_t t_inline = now_ns() - t0;
        for (int touch = 0; touch < 2; touch++) {
            t0 = now_ns();
            for (int i = 0; i < iters; i++) {
                ool_msg_t m; memset(&m, 0, sizeof m);
                m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
                m.h.msgh_size = sizeof m; m.h.msgh_remote_port = p; m.h.msgh_id = 2;
                m.body.msgh_descriptor_count = 1;
                m.ool.address = (void *)src; m.ool.size = (mach_msg_size_t)n; m.ool.copy = MACH_MSG_VIRTUAL_COPY;
                m.ool.deallocate = 0; m.ool.type = MACH_MSG_OOL_DESCRIPTOR;
                mach_msg(&m.h, MACH_SEND_MSG, sizeof m, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
                ool_buf_t r;
                mach_msg(&r.h, MACH_RCV_MSG, 0, sizeof r, p, 0, MACH_PORT_NULL);
                if (touch) { const uint64_t *w = r.m.ool.address; uint64_t s = 0; for (size_t k = 0; k < n / 8; k++) s += w[k]; g_sink += s; }
                vm_deallocate(mach_task_self(), (vm_address_t)r.m.ool.address, r.m.ool.size);
            }
            uint64_t t = now_ns() - t0;
            if (!touch) printf("%-9zu %8d %8.2fus %8.2fus", n, iters, us_per(t_inline, iters), us_per(t, iters));
            else {
                printf(" %12.2fus", us_per(t, iters));
                t0 = now_ns();
                for (int i = 0; i < iters; i++) { memcpy(dst, (void *)src, n); g_sink += dst[i % n]; }
                printf(" %8.2fus\n", us_per(now_ns() - t0, iters));
            }
        }
        free(sb); free(rb); free(dst);
        vm_deallocate(mach_task_self(), src, n);
    }
    mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_SEND, -1);
    mach_port_mod_refs(mach_task_self(), p, MACH_PORT_RIGHT_RECEIVE, -1);
}

int main(void) {
    mach_timebase_info(&tb);
    printf("RTT, 2 threads, 16 B payload, %d iters (+%d warm-up) x %d runs  [ns]\n", ITERS, WARM, RUNS);
    printf("%-11s %26s %26s %26s\n", "", "run1 min/p50/p99", "run2 min/p50/p99", "run3 min/p50/p99");
    const char *names[] = { "mach-2call", "mach-1call", "pipe", "socket", "xpc" };
    for (int k = 0; k < 5; k++) {
        printf("%-11s", names[k]);
        for (int r = 0; r < RUNS; r++) {
            stat_t s = k == 0 ? bench_mach(0) : k == 1 ? bench_mach(1) : k == 2 ? bench_fd(0) : k == 3 ? bench_fd(1) : bench_xpc();
            printf("   %6llu/%6llu/%7llu", (unsigned long long)s.min, (unsigned long long)s.p50, (unsigned long long)s.p99);
        }
        printf("\n");
    }
    bench_sizes();
    return 0;
}
