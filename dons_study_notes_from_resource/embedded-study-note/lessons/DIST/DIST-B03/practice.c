/* DIST-B03 추가 연습 — 답 전부 + 한 번에 검증
 *   연습 1  O(1) 인과 판정: b.vc[a.node] >= a.vc[a.node]
 *   연습 2  인과 순서 전달 버퍼 (Birman-Schiper-Stephenson 조건)
 *   연습 3  CAN 프레임에 싣기: 16비트 3칸 6바이트 + 랩어라운드 비교
 *   연습 4  ECU 재부팅: 시계가 0 으로 돌아갈 때 boot epoch
 *   연습 5  형제 해소 쓰기(resolution write)
 *   연습 6  vector clock 으로 만드는 mini data race 검출기 (TOOL-M09 의 TSan 과 같은 뿌리)
 * 스레드 없음, 모든 CHECK 결정적.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

enum { N = 3 };
typedef struct { uint32_t c[N]; } vclock_t;
typedef enum { VC_EQUAL, VC_BEFORE, VC_AFTER, VC_CONCURRENT } vc_order_t;

static void vc_tick(vclock_t *v, int me) { v->c[me]++; }
static void vc_merge(vclock_t *into, const vclock_t *o) {
    for (int i = 0; i < N; i++) if (o->c[i] > into->c[i]) into->c[i] = o->c[i];
}
static vc_order_t vc_compare(const vclock_t *a, const vclock_t *b) {
    bool lt = false, gt = false;
    for (int i = 0; i < N; i++) { if (a->c[i] < b->c[i]) lt = true; if (a->c[i] > b->c[i]) gt = true; }
    return lt && gt ? VC_CONCURRENT : lt ? VC_BEFORE : gt ? VC_AFTER : VC_EQUAL;
}
static bool vc_leq(const vclock_t *a, const vclock_t *b) {
    for (int i = 0; i < N; i++) if (a->c[i] > b->c[i]) return false;
    return true;
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- 연습 1: 이벤트의 출신 ECU 를 알면 칸 하나만 보면 된다 ---- */
typedef struct { int node; vclock_t vc; } ev_t;
static bool hb_fast(const ev_t *a, const ev_t *b) { return b->vc.c[a->node] >= a->vc.c[a->node]; }

static void ex1(void) {
    long wrong = 0, pairs = 0;
    for (uint32_t seed = 1; seed <= 100; seed++) {
        uint32_t s = seed * 2654435761u;
        vclock_t now[N], inbox[N][32];
        int in_n[N] = { 0 }, n = 0;
        ev_t ev[60];
        memset(now, 0, sizeof now);
        for (int t = 0; t < 40; t++) {
            int me = (int)(xs32(&s) % N), act = (int)(xs32(&s) % 3);
            if (act == 2 && in_n[me] > 0) vc_merge(&now[me], &inbox[me][--in_n[me]]);
            vc_tick(&now[me], me);
            if (act == 1) {
                int to = (me + 1 + (int)(xs32(&s) % 2)) % N;
                if (in_n[to] < 32) inbox[to][in_n[to]++] = now[me];
            }
            ev[n].node = me; ev[n].vc = now[me]; n++;
        }
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (i == j) continue;
                pairs++;
                if (hb_fast(&ev[i], &ev[j]) != (vc_compare(&ev[i].vc, &ev[j].vc) == VC_BEFORE)) wrong++;
            }
        }
    }
    CHECK(pairs == 100 * 40 * 39);
    CHECK(wrong == 0);
}

/* ---- 연습 2: 인과 순서 전달. 여기 vc 는 "브로드캐스트 전달 수"만 센다 (Part A 와 다르다) ---- */
typedef struct { int id, sender; vclock_t vc; } cd_msg_t;
static bool cd_deliverable(const vclock_t *local, const cd_msg_t *m) {
    for (int k = 0; k < N; k++) {
        if (k == m->sender) { if (m->vc.c[k] != local->c[k] + 1) return false; }
        else if (m->vc.c[k] > local->c[k]) return false;
    }
    return true;
}
/* arrival 순서로 도착한 메시지를 버퍼에 두고 전달 가능한 것부터 내보낸다. 전달 수를 돌려준다 */
static int cd_run(const cd_msg_t *const arrival[], int n, int delivered_ids[]) {
    const cd_msg_t *buf[8];
    int nb = 0, nd = 0;
    vclock_t local = { { 0, 0, 0 } };
    for (int a = 0; a < n; a++) {
        buf[nb++] = arrival[a];
        for (bool again = true; again; ) {
            again = false;
            for (int i = 0; i < nb; i++) {
                if (cd_deliverable(&local, buf[i])) {
                    local.c[buf[i]->sender]++;
                    delivered_ids[nd++] = buf[i]->id;
                    buf[i] = buf[--nb];
                    again = true;
                    break;
                }
            }
        }
    }
    return nd;
}
static void ex2(void) {
    cd_msg_t m1 = { 1, 0, { { 1, 0, 0 } } };  /* ECU0 의 첫 방송 */
    cd_msg_t m2 = { 2, 1, { { 1, 1, 0 } } };  /* ECU1 이 m1 을 본 뒤 방송 */
    cd_msg_t m3 = { 3, 0, { { 2, 0, 0 } } };  /* ECU0 의 둘째 방송, m2 와 동시 */
    const cd_msg_t *all[3] = { &m1, &m2, &m3 };
    static const int P[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    int bad = 0;
    for (int p = 0; p < 6; p++) {
        const cd_msg_t *arrival[3] = { all[P[p][0]], all[P[p][1]], all[P[p][2]] };
        int d[3] = { 0, 0, 0 };
        int nd = cd_run(arrival, 3, d);
        int pos[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < nd; i++) pos[d[i]] = i;
        if (nd != 3 || pos[1] > pos[2] || pos[1] > pos[3]) bad++;
    }
    CHECK(bad == 0);
    /* m1 이 영영 안 오면 m2 는 버퍼에 남는다 (전달되면 안 된다) */
    const cd_msg_t *only[1] = { &m2 };
    int d[1] = { 0 };
    CHECK(cd_run(only, 1, d) == 0);
}

/* ---- 연습 3: CAN 프레임 (Classic CAN 8바이트 중 6바이트) ---- */
static void vc16_encode(const uint16_t v[N], uint8_t out[6]) {
    for (int i = 0; i < N; i++) { out[2 * i] = (uint8_t)(v[i] >> 8); out[2 * i + 1] = (uint8_t)v[i]; }
}
static void vc16_decode(const uint8_t in[6], uint16_t v[N]) {
    for (int i = 0; i < N; i++) v[i] = (uint16_t)((uint16_t)(in[2 * i] << 8) | in[2 * i + 1]);
}
/* 순환 비교: 두 값의 차이가 2^15 미만일 때만 뜻이 있다 */
static vc_order_t vc16_compare(const uint16_t a[N], const uint16_t b[N]) {
    bool lt = false, gt = false;
    for (int i = 0; i < N; i++) {
        uint16_t d = (uint16_t)(a[i] - b[i]);
        if (d == 0) continue;
        if (d < 0x8000u) gt = true; else lt = true;
    }
    return lt && gt ? VC_CONCURRENT : lt ? VC_BEFORE : gt ? VC_AFTER : VC_EQUAL;
}
static void ex3(void) {
    uint16_t v[N] = { 0x0102, 0x0304, 0x0506 }, back[N];
    uint8_t wire[6];
    vc16_encode(v, wire);
    CHECK(wire[0] == 0x01 && wire[1] == 0x02 && wire[4] == 0x05 && wire[5] == 0x06);
    vc16_decode(wire, back);
    CHECK(memcmp(v, back, sizeof v) == 0);
    uint16_t a[N] = { 65535, 10, 7 }, b[N] = { 0, 10, 7 }, c[N] = { 65534, 11, 7 };
    CHECK(vc16_compare(a, b) == VC_BEFORE);       /* 65535 → 0 은 앞으로 간 것 */
    CHECK(vc16_compare(b, a) == VC_AFTER);
    CHECK(vc16_compare(a, c) == VC_CONCURRENT);
    CHECK(vc16_compare(a, a) == VC_EQUAL);
}

/* ---- 연습 4: 재부팅 epoch. 칸의 상위 8비트 = 부팅 번호 ---- */
#define EPOCH_SHIFT 24
static uint32_t ep_entry(uint32_t epoch, uint32_t cnt) { return (epoch << EPOCH_SHIFT) | cnt; }
static void reboot_with_epoch(vclock_t *v, int me, uint32_t new_epoch) {
    memset(v, 0, sizeof *v);
    v->c[me] = ep_entry(new_epoch, 0);
}
static void ex4(void) {
    vclock_t pre = { { 3, 5, 0 } };               /* ECU0 부팅 0 의 마지막 이벤트: ECU1 의 5 번까지 봄 */
    vclock_t naive = { { 0, 0, 0 } };
    vc_tick(&naive, 0);                           /* epoch 없이 RAM 만 초기화된 뒤 첫 이벤트 */
    CHECK(vc_compare(&pre, &naive) == VC_AFTER);  /* 재현: 재부팅 전 이벤트가 "나중"으로 보임 */

    vclock_t post;
    reboot_with_epoch(&post, 0, 1);
    vc_tick(&post, 0);
    CHECK(post.c[0] == ep_entry(1, 1));
    vclock_t pre_own = { { 3, 0, 0 } };
    CHECK(vc_compare(&pre_own, &post) == VC_BEFORE);  /* 자기 칸은 순서가 복구된다 */
    CHECK(vc_compare(&pre, &post) == VC_CONCURRENT);  /* 잊어버린 ECU1 의 지식 → 보수적으로 동시 */
}

/* ---- 연습 5: 동시 쓰기를 하나로 접는 쓰기 ---- */
#define MAX_SIB 4
typedef struct { vclock_t vc; int32_t limit_a; } wr_t;
typedef struct { wr_t sib[MAX_SIB]; int n; } store_t;
static int apply(store_t *s, const wr_t *w) {
    for (int i = 0; i < s->n; i++) {
        vc_order_t o = vc_compare(&w->vc, &s->sib[i].vc);
        if (o == VC_BEFORE || o == VC_EQUAL) return 0;
    }
    int kept = 0;
    for (int i = 0; i < s->n; i++)
        if (vc_compare(&w->vc, &s->sib[i].vc) != VC_AFTER) s->sib[kept++] = s->sib[i];
    if (kept == MAX_SIB) return -1;
    s->n = kept;
    s->sib[s->n++] = *w;
    return 1;
}
static wr_t resolve(const store_t *s, int me) {
    wr_t out;
    memset(&out, 0, sizeof out);
    out.limit_a = INT32_MAX;
    for (int i = 0; i < s->n; i++) {
        vc_merge(&out.vc, &s->sib[i].vc);
        if (s->sib[i].limit_a < out.limit_a) out.limit_a = s->sib[i].limit_a;
    }
    vc_tick(&out.vc, me);
    return out;
}
static void ex5(void) {
    wr_t a = { { { 1, 0, 0 } }, 50 }, b = { { { 0, 7, 0 } }, 120 };
    store_t s;
    memset(&s, 0, sizeof s);
    apply(&s, &a);
    apply(&s, &b);
    CHECK(s.n == 2);
    wr_t r = resolve(&s, 2);
    CHECK(r.vc.c[0] == 1 && r.vc.c[1] == 7 && r.vc.c[2] == 1);
    CHECK(r.limit_a == 50);
    CHECK(apply(&s, &r) == 1 && s.n == 1 && s.sib[0].limit_a == 50);
    CHECK(apply(&s, &a) == 0 && s.n == 1);        /* 늦게 온 옛 쓰기는 버려진다 */
    wr_t later = { { { 1, 8, 0 } }, 90 };         /* r 을 못 본 VCU 의 새 쓰기: 다시 동시 */
    CHECK(apply(&s, &later) == 1 && s.n == 2);
}

/* ---- 연습 6: mini data race 검출기 ---- */
typedef enum { OP_RD, OP_WR, OP_ACQ, OP_REL } rop_t;
typedef struct { int t; rop_t op; int obj; } rev_t;
enum { N_OBJ = 2 };
typedef struct { vclock_t C[N], L[N_OBJ], W[N_OBJ], R[N_OBJ]; int races; } racedet_t;

static void rd_init(racedet_t *d) {
    memset(d, 0, sizeof *d);
    for (int t = 0; t < N; t++) d->C[t].c[t] = 1;
}
static void rd_step(racedet_t *d, const rev_t *e) {
    vclock_t *c = &d->C[e->t];
    switch (e->op) {
    case OP_ACQ: vc_merge(c, &d->L[e->obj]); break;
    case OP_REL: d->L[e->obj] = *c; vc_tick(c, e->t); break;
    case OP_RD:
        if (!vc_leq(&d->W[e->obj], c)) d->races++;
        d->R[e->obj].c[e->t] = c->c[e->t];
        break;
    case OP_WR:
        if (!vc_leq(&d->W[e->obj], c) || !vc_leq(&d->R[e->obj], c)) d->races++;
        d->W[e->obj].c[e->t] = c->c[e->t];
        break;
    }
}
static int rd_run(const rev_t *tr, int n) {
    racedet_t d;
    rd_init(&d);
    for (int i = 0; i < n; i++) rd_step(&d, &tr[i]);
    return d.races;
}
#define RUN(tr) rd_run((tr), (int)(sizeof(tr) / sizeof((tr)[0])))
static void ex6(void) {
    enum { X = 0, M = 0, K = 1 };
    static const rev_t no_lock[] = { {0, OP_WR, X}, {1, OP_WR, X} };
    static const rev_t same_lock[] = { {0, OP_ACQ, M}, {0, OP_WR, X}, {0, OP_REL, M},
                                       {1, OP_ACQ, M}, {1, OP_WR, X}, {1, OP_REL, M} };
    static const rev_t diff_lock[] = { {0, OP_ACQ, M}, {0, OP_WR, X}, {0, OP_REL, M},
                                       {1, OP_ACQ, K}, {1, OP_WR, X}, {1, OP_REL, K} };
    static const rev_t two_reads[] = { {0, OP_RD, X}, {1, OP_RD, X} };
    static const rev_t read_then_write[] = { {0, OP_RD, X}, {1, OP_WR, X} };
    static const rev_t handoff[] = { {0, OP_ACQ, M}, {0, OP_WR, X}, {0, OP_REL, M},
                                     {1, OP_ACQ, M}, {1, OP_REL, M}, {1, OP_WR, X} };
    CHECK(RUN(no_lock) == 1);
    CHECK(RUN(same_lock) == 0);
    CHECK(RUN(diff_lock) == 1);
    CHECK(RUN(two_reads) == 0);
    CHECK(RUN(read_then_write) == 1);
    CHECK(RUN(handoff) == 0);                     /* 락을 스쳐 가도 happens-before 가 생긴다 */
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    ex6();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
