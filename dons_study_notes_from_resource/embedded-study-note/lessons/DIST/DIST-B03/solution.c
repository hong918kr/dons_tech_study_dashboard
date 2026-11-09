/* DIST-B03 Vector Clock과 동시성 판정
 *
 * ECU 3개 (0=BMS, 1=VCU, 2=GW) 가 CAN/Ethernet 으로 이야기한다. 벽시계(wall clock)는 동기화돼 있지 않다.
 *   Part A  vector clock 의 tick / merge / compare + Lamport 규칙 + ECU 의 local/send/recv 규칙.
 *   Part B  ECU별 로그 3개에 시계를 찍고(stamp_logs), 인과 순서를 지켜 병합하고(merge_logs),
 *           동시 이벤트를 표시한다(mark_concurrent). Lamport 값만으로는 동시성을 못 가른다는 반례를 찾는다.
 *   Part C  펌웨어 버그를 일부러 만든다: (1) 스큐 있는 벽시계로 정렬 → 결과가 원인보다 앞섬,
 *           (2) Lamport last-write-wins 설정 복제 → 안전 한도가 조용히 사라짐.
 *           검출 = count_causal_inversions, 수정 = VC 정렬 + 동시 쓰기를 형제(sibling)로 유지.
 *
 * 스레드는 없다. 실행 순서는 스크립트와 seed 로 정해지므로 모든 CHECK 는 매번 같은 결과다.
 * 정답 판정은 VC 와 무관하게 만든 "진짜 인과 그래프"(프로그램 순서 + 메시지 간선의 전이 폐포)와 비교한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

enum { ECU_BMS = 0, ECU_VCU = 1, ECU_GW = 2, N_ECU = 3 };
#define MAX_LOG 32
#define MAX_EV  (N_ECU * MAX_LOG)
#define MAX_MSG 64

typedef struct { uint32_t c[N_ECU]; } vclock_t;
typedef enum { VC_EQUAL, VC_BEFORE, VC_AFTER, VC_CONCURRENT } vc_order_t;

/* ECU 하나의 논리 시계 상태 */
typedef struct { int id; vclock_t vc; uint32_t lamport; } ecu_t;
/* 프레임 헤더에 실리는 시계 값 */
typedef struct { vclock_t vc; uint32_t lamport; } stamp_t;

typedef enum { EV_LOCAL, EV_SEND, EV_RECV } ev_kind_t;
typedef struct {
    int node, seq;        /* 어느 ECU 의 몇 번째 이벤트인가 */
    ev_kind_t kind;
    int msg_id;           /* SEND/RECV 를 잇는 메시지 번호, LOCAL 은 -1 */
    int32_t wall_ms;      /* 그 ECU 의 벽시계 (ECU끼리 비교하면 안 된다) */
    const char *label;
    vclock_t vc;          /* stamp_logs 가 채움 */
    uint32_t lamport;     /* stamp_logs 가 채움 */
    int n_concurrent;     /* mark_concurrent 가 채움: 나와 동시인 이벤트 수 */
} event_t;
typedef struct { event_t ev[MAX_LOG]; int n; } ecu_log_t;

/* ---------------- Part A: vector clock ---------------- */
/* 내 칸만 1 증가 */
void vc_tick(vclock_t *v, int me) {
    v->c[me]++;
}

/* into[i] = max(into[i], other[i]). 내 칸 증가는 여기서 하지 않는다 */
void vc_merge(vclock_t *into, const vclock_t *other) {
    for (int i = 0; i < N_ECU; i++)
        if (other->c[i] > into->c[i]) into->c[i] = other->c[i];
}

/* a 와 b 의 관계: EQUAL / BEFORE(a 가 먼저) / AFTER / CONCURRENT */
vc_order_t vc_compare(const vclock_t *a, const vclock_t *b) {
    bool a_has_smaller = false, a_has_larger = false;
    for (int i = 0; i < N_ECU; i++) {
        if (a->c[i] < b->c[i]) a_has_smaller = true;
        if (a->c[i] > b->c[i]) a_has_larger = true;
    }
    if (a_has_smaller && a_has_larger) return VC_CONCURRENT;
    if (a_has_smaller) return VC_BEFORE;
    if (a_has_larger) return VC_AFTER;
    return VC_EQUAL;
}

/* 내부 이벤트: VC 는 내 칸 +1, Lamport 는 +1 */
void on_local(ecu_t *e) {
    vc_tick(&e->vc, e->id);
    e->lamport++;
}

/* 송신 이벤트: local 과 같이 증가한 뒤, 증가한 값을 프레임에 싣는다 */
stamp_t on_send(ecu_t *e) {
    on_local(e);
    stamp_t s = { e->vc, e->lamport };
    return s;
}

/* 수신 이벤트: 프레임의 시계를 합친 뒤 증가. Lamport = max(내 값, 프레임 값) + 1 */
void on_recv(ecu_t *e, const stamp_t *m) {
    vc_merge(&e->vc, &m->vc);
    vc_tick(&e->vc, e->id);
    e->lamport = (m->lamport > e->lamport ? m->lamport : e->lamport) + 1;
}

/* ---------------- Part B: 3개 ECU 로그 병합 ---------------- */
/* 각 ECU 로그를 앞에서부터 재생해 event 의 vc/lamport 를 채운다.
 * RECV 는 짝이 되는 SEND 가 재생될 때까지 그 ECU 에서 기다린다.
 * 성공 0. 짝 없는 RECV, 순환 대기, 범위 밖 msg_id, 같은 msg_id 의 중복 SEND 는 -1. */
int stamp_logs(ecu_log_t logs[N_ECU]) {
    ecu_t ecu[N_ECU];
    memset(ecu, 0, sizeof ecu);
    for (int i = 0; i < N_ECU; i++) ecu[i].id = i;
    stamp_t wire[MAX_MSG];
    bool sent[MAX_MSG] = { false };
    int pos[N_ECU] = { 0 };
    for (;;) {
        bool progressed = false, done = true;
        for (int i = 0; i < N_ECU; i++) {
            while (pos[i] < logs[i].n) {
                event_t *ev = &logs[i].ev[pos[i]];
                if (ev->kind != EV_LOCAL && (ev->msg_id < 0 || ev->msg_id >= MAX_MSG)) return -1;
                if (ev->kind == EV_RECV) {
                    if (!sent[ev->msg_id]) break;      /* SEND 가 아직 → 이 ECU 는 잠시 멈춤 */
                    on_recv(&ecu[i], &wire[ev->msg_id]);
                } else if (ev->kind == EV_SEND) {
                    if (sent[ev->msg_id]) return -1;
                    wire[ev->msg_id] = on_send(&ecu[i]);
                    sent[ev->msg_id] = true;
                } else {
                    on_local(&ecu[i]);
                }
                ev->vc = ecu[i].vc;
                ev->lamport = ecu[i].lamport;
                pos[i]++;
                progressed = true;
            }
            if (pos[i] < logs[i].n) done = false;
        }
        if (done) return 0;
        if (!progressed) return -1;                    /* 아무도 못 나아감 = 잘못된 로그 */
    }
}

/* qsort 용: VC 원소 합 → node → seq. a→b 이면 합이 반드시 커지므로 인과 순서를 지키는 전순서가 된다 */
int merge_cmp(const void *pa, const void *pb) {
    const event_t *a = *(event_t *const *)pa, *b = *(event_t *const *)pb;
    uint64_t sa = 0, sb = 0;
    for (int i = 0; i < N_ECU; i++) { sa += a->vc.c[i]; sb += b->vc.c[i]; }
    if (sa != sb) return sa < sb ? -1 : 1;
    if (a->node != b->node) return a->node < b->node ? -1 : 1;
    return (a->seq > b->seq) - (a->seq < b->seq);
}

/* 세 로그의 모든 event 포인터를 out 에 모아 merge_cmp 로 정렬. 개수를 돌려준다 (cap 초과 시 -1) */
int merge_logs(ecu_log_t logs[N_ECU], event_t *out[], int cap) {
    int n = 0;
    for (int i = 0; i < N_ECU; i++) {
        for (int j = 0; j < logs[i].n; j++) {
            if (n == cap) return -1;
            out[n++] = &logs[i].ev[j];
        }
    }
    qsort(out, (size_t)n, sizeof out[0], merge_cmp);
    return n;
}

/* 각 event 의 n_concurrent 를 "나와 동시인 event 수" 로 채운다 */
void mark_concurrent(event_t *const ev[], int n) {
    for (int i = 0; i < n; i++) ev[i]->n_concurrent = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (vc_compare(&ev[i]->vc, &ev[j]->vc) == VC_CONCURRENT) {
                ev[i]->n_concurrent++;
                ev[j]->n_concurrent++;
            }
        }
    }
}

/* Lamport 반례: L(a)==L(c), L(b)==L(d), L(a)<L(b) 인데 a→b 이고 c∥d 인 인덱스 4개를 out 에.
 * 숫자 쌍이 완전히 같은데 답이 다르다 = Lamport 값의 어떤 함수로도 동시성을 가를 수 없다. */
bool find_lamport_counterexample(event_t *const ev[], int n, int out[4]) {
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
            if (ev[a]->lamport >= ev[b]->lamport) continue;
            if (vc_compare(&ev[a]->vc, &ev[b]->vc) != VC_BEFORE) continue;
            for (int c = 0; c < n; c++) {
                if (ev[c]->lamport != ev[a]->lamport) continue;
                for (int d = 0; d < n; d++) {
                    if (ev[d]->lamport != ev[b]->lamport) continue;
                    if (vc_compare(&ev[c]->vc, &ev[d]->vc) == VC_CONCURRENT) {
                        out[0] = a; out[1] = b; out[2] = c; out[3] = d;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* ---------------- Part C: 펌웨어 버그와 수정 ---------------- */
/* 검출 도구: 앞(i)에 놓인 event 가 뒤(j)의 event 보다 인과적으로 나중인 쌍의 수 */
int count_causal_inversions(event_t *const order[], int n) {
    int bad = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (vc_compare(&order[i]->vc, &order[j]->vc) == VC_AFTER) bad++;
    return bad;
}

/* 설정 복제: 게이트웨이가 "전류 한도" 쓰기를 받는다 */
#define GW_MAX_SIBLINGS 4
typedef struct { vclock_t vc; uint32_t lamport; int writer; int32_t limit_a; } cfg_write_t;
typedef struct { cfg_write_t sib[GW_MAX_SIBLINGS]; int n; uint32_t stale_dropped; } gw_store_t;

/* 들어온 쓰기가 기존 형제에 지배되거나 같으면 버린다(stale_dropped++, 반환 0).
 * 기존 형제 중 들어온 쓰기에 지배되는 것은 지운다. 동시인 것은 함께 보관(반환 1).
 * 자리가 없으면 상태를 바꾸지 않고 -1. */
int gw_apply_vc(gw_store_t *s, const cfg_write_t *w) {
    for (int i = 0; i < s->n; i++) {
        vc_order_t o = vc_compare(&w->vc, &s->sib[i].vc);
        if (o == VC_BEFORE || o == VC_EQUAL) { s->stale_dropped++; return 0; }
    }
    int kept = 0;
    for (int i = 0; i < s->n; i++)
        if (vc_compare(&w->vc, &s->sib[i].vc) != VC_AFTER) s->sib[kept++] = s->sib[i];
    if (kept == GW_MAX_SIBLINGS) return -1;   /* 지운 게 없을 때만 가능 → 상태 그대로 */
    s->n = kept;
    s->sib[s->n++] = *w;
    return 1;
}

/* 형제 중 가장 작은 한도 (안전 쪽). 비어 있으면 INT32_MAX */
int32_t gw_effective_limit(const gw_store_t *s) {
    int32_t lim = INT32_MAX;
    for (int i = 0; i < s->n; i++)
        if (s->sib[i].limit_a < lim) lim = s->sib[i].limit_a;
    return lim;
}

/* ---------------- 여기부터 테스트 하네스 (고칠 필요 없음) ---------------- */
/* 버그 재현용 1: ECU 벽시계를 그대로 비교해서 정렬한다 */
static int cmp_wall(const void *pa, const void *pb) {
    const event_t *a = *(event_t *const *)pa, *b = *(event_t *const *)pb;
    if (a->wall_ms != b->wall_ms) return a->wall_ms < b->wall_ms ? -1 : 1;
    if (a->node != b->node) return a->node < b->node ? -1 : 1;
    return (a->seq > b->seq) - (a->seq < b->seq);
}
static int cmp_lamport(const void *pa, const void *pb) {
    const event_t *a = *(event_t *const *)pa, *b = *(event_t *const *)pb;
    if (a->lamport != b->lamport) return a->lamport < b->lamport ? -1 : 1;
    if (a->node != b->node) return a->node < b->node ? -1 : 1;
    return (a->seq > b->seq) - (a->seq < b->seq);
}
/* 버그 재현용 2: Lamport (값, writer) 가 큰 쪽만 남기는 last-write-wins */
typedef struct { bool has; uint32_t lamport; int writer; int32_t limit_a; } gw_lww_t;
static void gw_apply_lww(gw_lww_t *s, const cfg_write_t *w) {
    if (!s->has || w->lamport > s->lamport || (w->lamport == s->lamport && w->writer > s->writer)) {
        s->has = true; s->lamport = w->lamport; s->writer = w->writer; s->limit_a = w->limit_a;
    }
}

static int flatten(ecu_log_t logs[N_ECU], event_t *out[]) {
    int n = 0;
    for (int i = 0; i < N_ECU; i++)
        for (int j = 0; j < logs[i].n; j++) out[n++] = &logs[i].ev[j];
    return n;
}
static void add_ev(ecu_log_t logs[N_ECU], int node, ev_kind_t k, int msg, int32_t wall, const char *label) {
    event_t *e = &logs[node].ev[logs[node].n];
    e->node = node; e->seq = logs[node].n; e->kind = k; e->msg_id = msg; e->wall_ms = wall; e->label = label;
    logs[node].n++;
}
/* BMS 벽시계는 기준, VCU 는 약 20 ms 느림, GW 는 약 90 ms 빠름 */
static void build_scenario(ecu_log_t logs[N_ECU]) {
    memset(logs, 0, sizeof(ecu_log_t) * N_ECU);
    add_ev(logs, ECU_BMS, EV_LOCAL, -1, 1000, "b1 overtemp");
    add_ev(logs, ECU_BMS, EV_SEND,   0, 1002, "b2 tx_derate");
    add_ev(logs, ECU_BMS, EV_LOCAL, -1, 1010, "b3 fan_on");
    add_ev(logs, ECU_BMS, EV_RECV,   2, 1020, "b4 rx_ack");
    add_ev(logs, ECU_VCU, EV_LOCAL, -1,  985, "v1 pedal");
    add_ev(logs, ECU_VCU, EV_RECV,   0,  987, "v2 rx_derate");
    add_ev(logs, ECU_VCU, EV_SEND,   1,  990, "v3 tx_torque");
    add_ev(logs, ECU_GW,  EV_LOCAL, -1, 1100, "g1 diag");
    add_ev(logs, ECU_GW,  EV_RECV,   1, 1103, "g2 rx_torque");
    add_ev(logs, ECU_GW,  EV_SEND,   2, 1106, "g3 tx_ack");
}
static event_t *by_label(ecu_log_t logs[N_ECU], const char *name) {
    size_t len = strlen(name);
    for (int i = 0; i < N_ECU; i++)
        for (int j = 0; j < logs[i].n; j++)
            if (!strncmp(logs[i].ev[j].label, name, len) && logs[i].ev[j].label[len] == ' ')
                return &logs[i].ev[j];
    return NULL;
}
static bool vc_is(const vclock_t *v, uint32_t a, uint32_t b, uint32_t c) {
    return v->c[0] == a && v->c[1] == b && v->c[2] == c;
}
static void order_string(event_t *const order[], int n, char *buf, size_t cap) {
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < n && off + 4 < cap; i++)
        off += (size_t)snprintf(buf + off, cap - off, "%s%.2s", i ? " " : "", order[i]->label);
}
static void print_table(event_t *const order[], int n) {
    printf("  %-14s %-11s %3s %6s  %s\n", "event", "vector", "L", "wall", "concurrent");
    for (int i = 0; i < n; i++) {
        char vc[32];
        snprintf(vc, sizeof vc, "[%u,%u,%u]", order[i]->vc.c[0], order[i]->vc.c[1], order[i]->vc.c[2]);
        printf("  %-14s %-11s %3u %6d  %s", order[i]->label, vc, order[i]->lamport, (int)order[i]->wall_ms,
               order[i]->n_concurrent ? "||" : "");
        if (order[i]->n_concurrent) printf(" x%d", order[i]->n_concurrent);
        printf("\n");
    }
}

static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
/* 전역 실행 하나를 seed 로 만들어 ECU별 로그로 나눈다. 벽시계 = 진짜 시각 + ECU별 스큐 */
void gen_random_logs(uint32_t seed, ecu_log_t logs[N_ECU], const int32_t skew_ms[N_ECU]) {
    uint32_t s = seed ? seed : 1;
    memset(logs, 0, sizeof(ecu_log_t) * N_ECU);
    int inbox[N_ECU][MAX_MSG], in_n[N_ECU] = { 0 };
    int32_t now = 0;
    int next_msg = 0;
    int steps = 18 + (int)(xs32(&s) % 13);
    for (int t = 0; t < steps; t++) {
        int node = (int)(xs32(&s) % N_ECU);
        now += 1 + (int32_t)(xs32(&s) % 4);
        int act = (int)(xs32(&s) % 3);
        if (act == 2 && in_n[node] > 0) {
            int k = (int)(xs32(&s) % (uint32_t)in_n[node]);
            int id = inbox[node][k];
            inbox[node][k] = inbox[node][--in_n[node]];
            add_ev(logs, node, EV_RECV, id, now + skew_ms[node], "rnd ");
        } else if (act == 1 && next_msg < MAX_MSG) {
            int to = (node + 1 + (int)(xs32(&s) % 2)) % N_ECU;
            inbox[to][in_n[to]++] = next_msg;
            add_ev(logs, node, EV_SEND, next_msg++, now + skew_ms[node], "rnd ");
        } else {
            add_ev(logs, node, EV_LOCAL, -1, now + skew_ms[node], "rnd ");
        }
    }
}

/* VC 를 쓰지 않는 진짜 인과: 프로그램 순서 + SEND→RECV 간선의 전이 폐포 */
static bool hb[MAX_EV][MAX_EV];
void ground_truth(event_t *const ev[], int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            hb[i][j] = (ev[i]->node == ev[j]->node && ev[j]->seq == ev[i]->seq + 1) ||
                       (ev[i]->kind == EV_SEND && ev[j]->kind == EV_RECV && ev[i]->msg_id == ev[j]->msg_id);
        }
    }
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if (hb[i][k] && hb[k][j]) hb[i][j] = true;
}

static bool next_perm(int *a, int n) {
    int i = n - 2;
    while (i >= 0 && a[i] >= a[i + 1]) i--;
    if (i < 0) return false;
    int j = n - 1;
    while (a[j] <= a[i]) j--;
    int t = a[i]; a[i] = a[j]; a[j] = t;
    for (int l = i + 1, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return true;
}

static void test_part_a(void) {
    vclock_t v = { { 0, 0, 0 } };
    vc_tick(&v, 1);
    CHECK(vc_is(&v, 0, 1, 0));
    vclock_t into = { { 1, 5, 2 } }, other = { { 3, 0, 2 } };
    vc_merge(&into, &other);
    CHECK(vc_is(&into, 3, 5, 2));          /* 칸별 최댓값, 추가 증가 없음 */
    CHECK(vc_is(&other, 3, 0, 2));         /* other 는 그대로 */

    vclock_t a = { { 1, 2, 3 } }, same = { { 1, 2, 3 } }, b = { { 1, 3, 3 } }, c = { { 2, 2, 2 } };
    vclock_t big = { { 0xFFFFFFFFu, 0, 0 } }, one = { { 1, 0, 0 } };
    CHECK(vc_compare(&a, &same) == VC_EQUAL);
    CHECK(vc_compare(&a, &b) == VC_BEFORE);
    CHECK(vc_compare(&b, &a) == VC_AFTER);
    CHECK(vc_compare(&a, &c) == VC_CONCURRENT);
    CHECK(vc_compare(&c, &a) == VC_CONCURRENT);
    CHECK(vc_compare(&big, &one) == VC_AFTER);   /* 뺄셈으로 비교하면 깨지는 값 */

    ecu_t p, q;
    memset(&p, 0, sizeof p); memset(&q, 0, sizeof q);
    p.id = 0; q.id = 1;
    on_local(&q); on_local(&q); on_local(&q);    /* q = [0,3,0], L=3 */
    stamp_t m = on_send(&p);                     /* p = [1,0,0], L=1 */
    CHECK(vc_is(&m.vc, 1, 0, 0) && m.lamport == 1);
    on_recv(&q, &m);
    CHECK(vc_is(&q.vc, 1, 4, 0));
    CHECK(q.lamport == 4);                       /* max(3,1)+1 */
    stamp_t m2 = on_send(&q);                    /* q = [1,5,0], L=5 */
    on_recv(&p, &m2);
    CHECK(vc_is(&p.vc, 2, 5, 0));
    CHECK(p.lamport == 6);                       /* max(1,5)+1 */
}

static void test_part_b(void) {
    ecu_log_t logs[N_ECU];
    build_scenario(logs);
    CHECK(stamp_logs(logs) == 0);
    CHECK(vc_is(&by_label(logs, "b2")->vc, 2, 0, 0) && by_label(logs, "b2")->lamport == 2);
    CHECK(vc_is(&by_label(logs, "v2")->vc, 2, 2, 0) && by_label(logs, "v2")->lamport == 3);
    CHECK(vc_is(&by_label(logs, "g2")->vc, 2, 3, 2) && by_label(logs, "g2")->lamport == 5);
    CHECK(vc_is(&by_label(logs, "b4")->vc, 4, 3, 3) && by_label(logs, "b4")->lamport == 7);

    event_t *merged[MAX_EV];
    int n = merge_logs(logs, merged, MAX_EV);
    CHECK(n == 10);
    char buf[64];
    order_string(merged, n, buf, sizeof buf);
    CHECK(strcmp(buf, "b1 v1 g1 b2 b3 v2 v3 g2 g3 b4") == 0);
    CHECK(merge_logs(logs, merged, 9) == -1);    /* 버퍼 부족은 잘라내지 말고 에러 */
    n = merge_logs(logs, merged, MAX_EV);

    mark_concurrent(merged, n);
    print_table(merged, n);
    CHECK(by_label(logs, "b1")->n_concurrent == 2);
    CHECK(by_label(logs, "b3")->n_concurrent == 6);
    CHECK(by_label(logs, "b4")->n_concurrent == 0);
    CHECK(by_label(logs, "v1")->n_concurrent == 4);
    CHECK(by_label(logs, "g1")->n_concurrent == 6);
    CHECK(by_label(logs, "g3")->n_concurrent == 1);
    int total = 0;
    for (int i = 0; i < n; i++) total += merged[i]->n_concurrent;
    CHECK(total == 26);                          /* 동시 쌍 13개 × 2 */

    /* Lamport 는 b3(L=3) 와 v3(L=4) 를 앞뒤로 놓지만 실제로는 동시다 */
    event_t *b3 = by_label(logs, "b3"), *v3 = by_label(logs, "v3");
    CHECK(b3->lamport < v3->lamport);
    CHECK(vc_compare(&b3->vc, &v3->vc) == VC_CONCURRENT);

    /* 숫자 쌍이 같은데 인과는 다른 두 쌍 */
    int w[4] = { 0, 0, 0, 0 };
    bool found = find_lamport_counterexample(merged, n, w);
    CHECK(found);
    if (found) {
        printf("  counterexample: (%s L=%u) -> (%s L=%u)  vs  (%s L=%u) || (%s L=%u)\n",
               merged[w[0]]->label, merged[w[0]]->lamport, merged[w[1]]->label, merged[w[1]]->lamport,
               merged[w[2]]->label, merged[w[2]]->lamport, merged[w[3]]->label, merged[w[3]]->lamport);
        CHECK(merged[w[0]]->lamport == merged[w[2]]->lamport);
        CHECK(merged[w[1]]->lamport == merged[w[3]]->lamport);
        CHECK(vc_compare(&merged[w[0]]->vc, &merged[w[1]]->vc) == VC_BEFORE);
        CHECK(vc_compare(&merged[w[2]]->vc, &merged[w[3]]->vc) == VC_CONCURRENT);
    }

    /* 잘못된 로그는 멈추지 말고 -1 */
    ecu_log_t bad[N_ECU];
    memset(bad, 0, sizeof bad);
    add_ev(bad, ECU_BMS, EV_RECV, 7, 0, "x1 orphan");
    CHECK(stamp_logs(bad) == -1);                /* 짝 없는 RECV */
    memset(bad, 0, sizeof bad);
    add_ev(bad, ECU_BMS, EV_RECV, 1, 0, "x1 wait");
    add_ev(bad, ECU_BMS, EV_SEND, 0, 0, "x2 send");
    add_ev(bad, ECU_VCU, EV_RECV, 0, 0, "y1 wait");
    add_ev(bad, ECU_VCU, EV_SEND, 1, 0, "y2 send");
    CHECK(stamp_logs(bad) == -1);                /* 서로의 SEND 를 기다리는 순환 */
    memset(bad, 0, sizeof bad);
    add_ev(bad, ECU_BMS, EV_SEND, MAX_MSG, 0, "x1 range");
    CHECK(stamp_logs(bad) == -1);                /* 범위 밖 msg_id */
    memset(bad, 0, sizeof bad);
    add_ev(bad, ECU_BMS, EV_SEND, 3, 0, "x1 dup");
    add_ev(bad, ECU_VCU, EV_SEND, 3, 0, "y1 dup");
    CHECK(stamp_logs(bad) == -1);                /* 같은 msg_id 를 두 번 SEND */
}

/* seed 300개: VC 판정이 진짜 인과와 모든 쌍에서 일치하는가 */
static void test_property(void) {
    static const int32_t no_skew[N_ECU] = { 0, 0, 0 };
    long pairs = 0, vc_wrong = 0, lamport_mono_bad = 0, conc = 0, conc_ordered = 0;
    long inv_merge = 0, inv_lamport = 0;
    int stamp_fail = 0, short_merge = 0, counter_found = 0, ran = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        ecu_log_t logs[N_ECU];
        gen_random_logs(seed * 2654435761u, logs, no_skew);
        if (stamp_logs(logs) != 0) { stamp_fail++; continue; }
        ran++;
        event_t *all[MAX_EV], *order[MAX_EV];
        int n = flatten(logs, all);
        ground_truth(all, n);
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (i == j) continue;
                pairs++;
                vc_order_t want = hb[i][j] ? VC_BEFORE : hb[j][i] ? VC_AFTER : VC_CONCURRENT;
                if (vc_compare(&all[i]->vc, &all[j]->vc) != want) vc_wrong++;
                if (hb[i][j] && !(all[i]->lamport < all[j]->lamport)) lamport_mono_bad++;
                if (want == VC_CONCURRENT) {
                    conc++;
                    if (all[i]->lamport < all[j]->lamport) conc_ordered++;
                }
            }
        }
        int m = merge_logs(logs, order, MAX_EV);
        if (m != n) short_merge++;
        else inv_merge += count_causal_inversions(order, m);
        memcpy(order, all, sizeof(event_t *) * (size_t)n);
        qsort(order, (size_t)n, sizeof order[0], cmp_lamport);
        inv_lamport += count_causal_inversions(order, n);
        int w[4];
        if (find_lamport_counterexample(all, n, w)) counter_found++;
    }
    printf("  property: seeds=%d pairs=%ld vc_wrong=%ld lamport_mono_bad=%ld concurrent=%ld "
           "concurrent_but_L_smaller=%ld counterexample_seeds=%d\n",
           ran, pairs, vc_wrong, lamport_mono_bad, conc, conc_ordered, counter_found);
    CHECK(stamp_fail == 0);
    CHECK(pairs > 0 && vc_wrong == 0);
    CHECK(lamport_mono_bad == 0);                /* a→b ⇒ L(a)<L(b) 는 항상 성립 */
    CHECK(short_merge == 0);
    CHECK(inv_merge == 0);
    CHECK(inv_lamport == 0);                     /* Lamport 정렬도 순서는 안 깨뜨린다 */
    CHECK(conc_ordered > 0);                     /* 그러나 동시 쌍에도 앞뒤를 매긴다 */
    CHECK(counter_found > 0);
}

static void test_part_c(void) {
    /* 버그 1: 벽시계 정렬 */
    ecu_log_t logs[N_ECU];
    build_scenario(logs);
    stamp_logs(logs);
    event_t *order[MAX_EV];
    int n = flatten(logs, order);
    qsort(order, (size_t)n, sizeof order[0], cmp_wall);
    int inv_wall = count_causal_inversions(order, n);
    event_t *merged[MAX_EV];
    int m = merge_logs(logs, merged, MAX_EV);
    int inv_vc = count_causal_inversions(merged, m);
    printf("  causal inversions: wall-clock sort=%d, vector-clock merge=%d\n", inv_wall, inv_vc);
    CHECK(inv_wall == 7);
    CHECK(m == 10 && inv_vc == 0);

    /* 버그 2: BMS 의 50 A 감량과 VCU 의 120 A 설정이 동시에 일어났다. VCU 는 일을 많이 해서 L 이 크다 */
    cfg_write_t w_bms = { { { 1, 0, 0 } }, 1, ECU_BMS, 50 };
    cfg_write_t w_vcu = { { { 0, 7, 0 } }, 7, ECU_VCU, 120 };
    CHECK(vc_compare(&w_bms.vc, &w_vcu.vc) == VC_CONCURRENT);
    gw_lww_t lww = { false, 0, 0, 0 };
    gw_apply_lww(&lww, &w_bms);
    gw_apply_lww(&lww, &w_vcu);
    CHECK(lww.limit_a == 120);                   /* 재현: 안전 한도가 조용히 사라짐 */

    cfg_write_t set2[2] = { w_bms, w_vcu };
    int perm2[2] = { 0, 1 }, bad2 = 0, runs2 = 0;
    do {
        gw_store_t s;
        memset(&s, 0, sizeof s);
        for (int i = 0; i < 2; i++) gw_apply_vc(&s, &set2[perm2[i]]);
        runs2++;
        if (s.n != 2 || gw_effective_limit(&s) != 50) bad2++;
    } while (next_perm(perm2, 2));
    CHECK(runs2 == 2 && bad2 == 0);

    /* VCU 가 둘 다 본 뒤 쓴 80 A 는 앞의 둘을 모두 지배한다: 어떤 전달 순서든 80 A 하나만 남는다 */
    cfg_write_t w_new = { { { 1, 8, 0 } }, 9, ECU_VCU, 80 };
    cfg_write_t set3[3] = { w_bms, w_vcu, w_new };
    int perm3[3] = { 0, 1, 2 }, bad3 = 0, runs3 = 0;
    do {
        gw_store_t s;
        memset(&s, 0, sizeof s);
        for (int i = 0; i < 3; i++) gw_apply_vc(&s, &set3[perm3[i]]);
        runs3++;
        if (s.n != 1 || gw_effective_limit(&s) != 80) bad3++;
    } while (next_perm(perm3, 3));
    CHECK(runs3 == 6 && bad3 == 0);

    /* 재전송·역전 도착 */
    gw_store_t s;
    memset(&s, 0, sizeof s);
    CHECK(gw_apply_vc(&s, &w_new) == 1);
    CHECK(gw_apply_vc(&s, &w_bms) == 0 && s.stale_dropped == 1);
    CHECK(gw_apply_vc(&s, &w_new) == 0 && s.stale_dropped == 2);
    CHECK(s.n == 1);
    CHECK(gw_effective_limit(&s) == 80);

    /* 꽉 찬 저장소: 동시 쓰기 5번째는 -1, 상태 불변 */
    memset(&s, 0, sizeof s);
    for (uint32_t i = 0; i < GW_MAX_SIBLINGS; i++) {
        cfg_write_t w = { { { i + 1, GW_MAX_SIBLINGS - i, 0 } }, 1, ECU_BMS, 100 + (int32_t)i };
        CHECK(gw_apply_vc(&s, &w) == 1);
    }
    cfg_write_t extra = { { { 0, 0, 1 } }, 1, ECU_GW, 10 };
    CHECK(gw_apply_vc(&s, &extra) == -1);
    CHECK(s.n == GW_MAX_SIBLINGS);
    CHECK(gw_effective_limit(&s) == 100);
    memset(&s, 0, sizeof s);
    CHECK(gw_effective_limit(&s) == INT32_MAX);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_property();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
