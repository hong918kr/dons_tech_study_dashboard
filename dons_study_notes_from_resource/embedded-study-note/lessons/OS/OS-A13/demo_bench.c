/* BUILD: -pthread */
/* OS-A13 demo_bench: 미니 RPC 의 왕복 지연(RTT) p50/p99 와 calls/s — 동시 호출자 1/4/16
 *   기준선: 같은 크기 프레임을 socketpair 위에서 바로 핑퐁 (다중화·워커 풀 없음)
 *   RPC 코드는 같은 레슨의 solution.c 를 그대로 가져다 쓴다 (OS_A13_NO_MAIN 으로 테스트 main 제외).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench
 * 시간 숫자는 기계 부하에 따라 크게 흔들린다 — 비율만 본다.
 */
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 스트림/테스트용 함수는 여기서 안 쓴다 */
#define OS_A13_NO_MAIN
#include "solution.c"

#define WARM_MS  200
#define RUN_MS   1000
#define MAXS     400000

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

typedef struct { rpc_client_t *c; uint64_t t_start, t_end; uint64_t *lat; size_t n, cap; int errs; } bench_t;

static void *bench_caller(void *p) {
    bench_t *b = p;
    uint32_t v = 1;
    while (now_ns() < b->t_end) {
        uint64_t t0 = now_ns();
        echo_res_t r;
        if (echo_call(b->c, v, 0, 3000, &r) != 0 || r.value != echo_fn(v)) b->errs++;
        uint64_t t1 = now_ns();
        if (t0 >= b->t_start && b->n < b->cap) b->lat[b->n++] = t1 - t0;   /* 워밍업 구간은 버린다 */
        v++;
    }
    return NULL;
}

static void report(const char *name, int callers, uint64_t *all, size_t n, int errs) {
    qsort(all, n, sizeof *all, cmp_u64);
    double p50 = n ? (double)all[n / 2] / 1000.0 : 0, p99 = n ? (double)all[(size_t)((double)n * 0.99)] / 1000.0 : 0;
    printf("| %-22s | %3d | %9.0f | %7.1f | %7.1f | %d |\n", name, callers, (double)n / (RUN_MS / 1000.0), p50, p99, errs);
}

static void run_rpc(int callers) {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    rpc_server_t *s = rpc_server_start(sv[1], 2, 16, 0);
    rpc_client_t *c = rpc_client_new(sv[0], 2, 0);
    bench_t b[16];
    pthread_t th[16];
    uint64_t t0 = now_ns();
    for (int i = 0; i < callers; i++) {
        b[i] = (bench_t){ c, t0 + WARM_MS * 1000000ull, t0 + (WARM_MS + RUN_MS) * 1000000ull, NULL, 0, MAXS / (size_t)callers, 0 };
        b[i].lat = malloc(b[i].cap * sizeof(uint64_t));
        pthread_create(&th[i], NULL, bench_caller, &b[i]);
    }
    uint64_t *all = malloc(MAXS * sizeof(uint64_t));
    size_t n = 0;
    int errs = 0;
    for (int i = 0; i < callers; i++) {
        pthread_join(th[i], NULL);
        memcpy(all + n, b[i].lat, b[i].n * sizeof(uint64_t));
        n += b[i].n;
        errs += b[i].errs;
        free(b[i].lat);
    }
    char name[64];
    snprintf(name, sizeof name, "mini-RPC (max pend %u)", c->max_pending);
    rpc_client_free(c);
    rpc_server_stop(s);
    report(name, callers, all, n, errs);
    free(all);
}

/* 기준선: 같은 54 B 응답 프레임 크기로 바로 주고받는다 (reader 스레드·pending 표·워커 풀 없음) */
static void *raw_echo(void *p) {
    int fd = *(int *)p;
    uint8_t buf[RPC_BUF];
    xport_t x = { .fd = fd };
    frame_t f;
    while (xp_read_frame(&x, buf, sizeof buf, &f) == 1) {
        uint8_t pl[64], out[128];
        tlvw_t w = { pl, sizeof pl, 0, false };
        uint32_t v = 0;
        tlv_get_u32(f.payload, f.plen, TAG_VALUE, &v);
        tlvw_u32(&w, TAG_VALUE, echo_fn(v));
        tlvw_put(&w, TAG_SERVER_INFO, "mini-rpc/2", 10);
        tlvw_u64(&w, TAG_TRACE_ID, 1);
        size_t n = frame_build(out, sizeof out, 2, T_RESP, f.method, f.req_id, 0, pl, w.n);
        if (xp_write_all(&x, out, n) != 0) break;
    }
    return NULL;
}
static void run_raw(void) {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    pthread_t th;
    pthread_create(&th, NULL, raw_echo, &sv[1]);
    xport_t x = { .fd = sv[0] };
    uint64_t *lat = malloc(MAXS * sizeof(uint64_t));
    size_t n = 0;
    int errs = 0;
    uint64_t t0 = now_ns(), ts = t0 + WARM_MS * 1000000ull, te = ts + RUN_MS * 1000000ull;
    uint8_t buf[RPC_BUF];
    for (uint32_t v = 1; now_ns() < te; v++) {
        uint8_t pl[64], out[128];
        tlvw_t w = { pl, sizeof pl, 0, false };
        tlvw_u32(&w, TAG_VALUE, v);
        tlvw_u64(&w, TAG_TRACE_ID, v);
        size_t fn = frame_build(out, sizeof out, 2, T_REQ, M_ECHO, v, 0, pl, w.n);
        uint64_t a = now_ns();
        frame_t f;
        uint32_t r = 0;
        if (xp_write_all(&x, out, fn) != 0 || xp_read_frame(&x, buf, sizeof buf, &f) != 1 ||
            !tlv_get_u32(f.payload, f.plen, TAG_VALUE, &r) || r != echo_fn(v)) errs++;
        uint64_t b = now_ns();
        if (a >= ts && n < MAXS) lat[n++] = b - a;
    }
    shutdown(sv[0], SHUT_RDWR);
    pthread_join(th, NULL);
    close(sv[0]);
    close(sv[1]);
    report("raw socketpair ping-pong", 1, lat, n, errs);
    free(lat);
}

int main(void) {
    printf("| 방식 | 호출자 | calls/s | p50 us | p99 us | 오류 |\n|---|---|---|---|---|---|\n");
    run_raw();
    run_rpc(1);
    run_rpc(4);
    run_rpc(16);
    return 0;
}
