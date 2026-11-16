/* PCIE-B11 추가 연습 — ordering 검사기 응용
 *
 * 본문 solution.c 의 표·may_pass·ord_check·sched_pick 를 그대로 담고(자체 완결), 그 위에:
 *   연습 1: TLP DW0 4바이트(Fmt/Type, TC, Attr) → 순서 종류 + RO/IDO   (헤더 필드 자체는 PCIE-B10)
 *   연습 2: "data 보다 flag 가 먼저 보이는" 합법 도착 순서가 있는지 전수 탐색 (RO/IDO 버그 찾기)
 *   연습 3: Device Control(RELAX_EN) / Device Control 2(IDO_REQ_EN, IDO_CMP_EN) 가 허용할 때만 RO/IDO
 *   연습 4: 두 노드 데드락 — FIFO 송신은 멈추고, posted 가 NP 를 앞지르는 스케줄러는 다 비운다
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ORD_MAX 32u                      /* 검사기가 받는 최대 트랜잭션 수 (방문 비트맵 32비트) */

typedef enum { OC_P = 0, OC_NPR = 1, OC_NPW = 2, OC_CPL = 3, OC_COUNT = 4 } ord_cls;
typedef enum { V_NO = 0, V_YN = 1, V_YES = 2 } ord_val;
/* (b) 예외 종류 */
typedef enum {
    K_NONE = 0,
    K_RO_IDO,   /* A2b/C2b/D2b: 나중 것이 RO=1 이면 허용, IDO=1 이고 출발 ID 가 다르면 허용 */
    K_IDO,      /* B2b: IDO=1 이고 Requester ID 가 다르면 허용 (RO 는 해당 없음) */
    K_TXID      /* D5 : Transaction ID 가 같으면 No (D5b), 다르면 Y/N (D5a) */
} ord_cond;

typedef struct { ord_val v; ord_cond k; const char *ida; const char *idb; } ord_cell;

typedef struct {
    ord_cls  cls;
    uint8_t  tc;        /* Traffic Class 0..7 */
    bool     ro, ido;   /* Attr[1] Relaxed Ordering, Attr[2] ID-based Ordering */
    uint16_t rid;       /* Transaction ID 의 Requester ID (요청: 자기 RID, 완료: 원래 요청자의 RID) bb:dd.f */
    uint8_t  tag;       /* Transaction ID 의 Tag */
    uint16_t cid;       /* 완료만: Completer ID */
    const char *name;
} txn;

/* ---------------- Part A: 순서 표 ---------------- */

/* ORD_TABLE[나중(row)][먼저(col)] */
static const ord_cell ORD_TABLE[OC_COUNT][OC_COUNT] = {
    /*           col: P                               NPR                        NPW                        CPL                          */
    /* P   */ { { V_NO, K_RO_IDO, "A2a", "A2b" }, { V_YES, K_NONE, "A3", NULL }, { V_YES, K_NONE, "A4", NULL }, { V_YN,  K_NONE, "A5", NULL } },
    /* NPR */ { { V_NO, K_IDO,    "B2a", "B2b" }, { V_YN,  K_NONE, "B3", NULL }, { V_YN,  K_NONE, "B4", NULL }, { V_YN,  K_NONE, "B5", NULL } },
    /* NPW */ { { V_NO, K_RO_IDO, "C2a", "C2b" }, { V_YN,  K_NONE, "C3", NULL }, { V_YN,  K_NONE, "C4", NULL }, { V_YN,  K_NONE, "C5", NULL } },
    /* CPL */ { { V_NO, K_RO_IDO, "D2a", "D2b" }, { V_YES, K_NONE, "D3", NULL }, { V_YES, K_NONE, "D4", NULL }, { V_YN,  K_TXID, "D5a", "D5b" } },
};

/* 출발 ID: 요청은 Requester ID, 완료는 Completer ID (IDO 비교에 쓰는 값) */
static uint16_t src_id(const txn *t) { return t->cls == OC_CPL ? t->cid : t->rid; }

/* ---------------- Part B: 예외를 반영한 "앞질러도 되나" ---------------- */

/* later 가 earlier 를 앞질러 도착해도 되면 true. *rule 에 근거 칸 이름("A2a", "D5b", "TC" ...) */
static bool may_pass(const txn *later, const txn *earlier, const char **rule) {
    const ord_cell *c = &ORD_TABLE[later->cls][earlier->cls];
    if (later->tc != earlier->tc) { *rule = "TC"; return true; }        /* TC 가 다르면 순서 관계 없음 */
    if (c->v == V_NO) {
        bool ex = false;
        if (c->k == K_RO_IDO && later->ro) ex = true;
        if ((c->k == K_RO_IDO || c->k == K_IDO) && later->ido && src_id(later) != src_id(earlier)) ex = true;
        *rule = ex ? c->idb : c->ida;
        return ex;
    }
    if (c->k == K_TXID && later->rid == earlier->rid && later->tag == earlier->tag) {
        *rule = c->idb;                                                   /* D5b: 같은 요청의 분할 완료 */
        return false;
    }
    *rule = c->ida;
    return true;
}

/* 표의 Yes 칸: 앞지를 수 있어야 한다 (막으면 데드락 가능) */
static bool must_be_able_to_pass(ord_cls later, ord_cls earlier) {
    return ORD_TABLE[later][earlier].v == V_YES;
}

/* ---------------- Part A: 도착 순서 검사기 ---------------- */

enum { ORD_OK = 0, ORD_ILLEGAL = 1, ORD_ERR_ARG = -1 };
typedef struct { int later, earlier; const char *rule; } ord_viol;

/* issued[0..n) = 보낸 순서, arrival[0..m) = 도착 순서 (issued 의 인덱스).
 * 반환: ORD_OK, ORD_ILLEGAL(*v 에 첫 위반: 도착 순서로 처음 나온 later, 그 안에서 가장 먼저 보낸 earlier),
 *       ORD_ERR_ARG (NULL, n > ORD_MAX, m != n, 범위 밖 인덱스, 중복) — 이때 *v 는 건드리지 않는다. */
static int ord_check(const txn *issued, size_t n, const uint8_t *arrival, size_t m, ord_viol *v) {
    if (!issued || !arrival || !v || n > ORD_MAX || m != n) return ORD_ERR_ARG;
    uint32_t seen = 0;
    uint8_t pos[ORD_MAX];
    for (size_t a = 0; a < n; a++) {
        if (arrival[a] >= n || (seen >> arrival[a] & 1u)) return ORD_ERR_ARG;
        seen |= 1u << arrival[a];
        pos[arrival[a]] = (uint8_t)a;
    }
    for (size_t a = 0; a < n; a++) {
        size_t j = arrival[a];                         /* a 번째로 도착한 것 */
        for (size_t i = 0; i < j; i++) {               /* j 보다 먼저 보낸 것 중 */
            if (pos[i] <= a) continue;                 /* 아직 안 왔으면 → j 가 i 를 앞지름 */
            const char *r;
            if (!may_pass(&issued[j], &issued[i], &r)) {
                v->later = (int)j; v->earlier = (int)i; v->rule = r;
                return ORD_ILLEGAL;
            }
        }
    }
    return ORD_OK;
}

/* ---------------- Part C: credit 을 보는 송신 스케줄러 ---------------- */

typedef struct { uint8_t p, np, cpl; } credits;   /* 받는 쪽이 광고한 남은 헤더 credit (P / NP / CPL) */

static uint8_t *credit_slot(credits *cr, ord_cls c) {
    return c == OC_P ? &cr->p : c == OC_CPL ? &cr->cpl : &cr->np;
}

/* 비교용: 무조건 FIFO. 맨 앞이 credit 없으면 전부 멈춘다 */
static int fifo_pick(const txn *q, const bool *sent, size_t n, const credits *cr) {
    credits c = *cr;
    for (size_t i = 0; i < n; i++)
        if (!sent[i]) return *credit_slot(&c, q[i].cls) ? (int)i : -1;
    return -1;
}

/* 아직 안 보낸 것 중 (1) 해당 종류 credit 이 있고 (2) 앞에 남은 모든 것을 앞질러도 되는 첫 번째 인덱스. 없으면 -1 */
static int sched_pick(const txn *q, const bool *sent, size_t n, const credits *cr) {
    credits c = *cr;
    for (size_t i = 0; i < n; i++) {
        if (sent[i] || *credit_slot(&c, q[i].cls) == 0) continue;
        bool ok = true;
        for (size_t j = 0; j < i && ok; j++) {
            const char *r;
            if (!sent[j] && !may_pass(&q[i], &q[j], &r)) ok = false;
        }
        if (ok) return (int)i;
    }
    return -1;
}


/* ================= 추가 연습 ================= */

/* pci_regs.h 와 같은 이름·값 (Linux include/uapi/linux/pci_regs.h, PCIe Capability 안의 오프셋) */
#define PCI_EXP_DEVCTL              0x08
#define PCI_EXP_DEVCTL_RELAX_EN     0x0010   /* Enable relaxed ordering */
#define PCI_EXP_DEVCTL_NOSNOOP_EN   0x0800   /* Enable No Snoop */
#define PCI_EXP_DEVCTL_READRQ_512B  0x2000
#define PCI_EXP_DEVCTL2             0x28
#define PCI_EXP_DEVCTL2_IDO_REQ_EN  0x0100   /* Allow IDO for requests */
#define PCI_EXP_DEVCTL2_IDO_CMP_EN  0x0200   /* Allow IDO for completions */

/* ---- 연습 1: TLP DW0 4바이트 → 순서 종류 + TC + RO/IDO ----
 * byte0 = Fmt[2:0](7:5) | Type[4:0](4:0)
 * byte1 = T9(7) TC[2:0](6:4) T8(3) Attr[2]=IDO(2) LN(1) TH(0)
 * byte2 = TD(7) EP(6) Attr[1]=RO(5) Attr[0]=NS(4) AT[1:0](3:2) Length[9:8](1:0)
 * 반환 0, 모르는/예약 조합(TLP Prefix Fmt=100 포함)은 -1 */
static int classify_tlp(const uint8_t dw0[4], txn *out) {
    uint8_t fmt = dw0[0] >> 5, type = dw0[0] & 0x1Fu;
    bool data = (fmt & 0x2u) != 0;                 /* Fmt[1] = 데이터 있음 */
    ord_cls c;
    if (fmt >= 4) return -1;                       /* 1xx = TLP Prefix */
    if (type == 0x00 || type == 0x01) {            /* MRd / MRdLk / MWr (3DW·4DW) */
        if (type == 0x01 && data) return -1;
        c = data ? OC_P : OC_NPR;
    } else if (type == 0x02) {                     /* IORd / IOWr — 항상 3DW */
        if (fmt & 1u) return -1;
        c = data ? OC_NPW : OC_NPR;
    } else if (type == 0x04 || type == 0x05) {     /* CfgRd0/1 / CfgWr0/1 — 항상 3DW */
        if (fmt & 1u) return -1;
        c = data ? OC_NPW : OC_NPR;
    } else if ((type & 0x18u) == 0x10u) {          /* Msg / MsgD: Type 10rrr, 항상 4DW */
        if (!(fmt & 1u)) return -1;
        c = OC_P;
    } else if (type == 0x0A || type == 0x0B) {     /* Cpl / CplD / CplLk / CplDLk — 3DW */
        if (fmt & 1u) return -1;
        c = OC_CPL;
    } else if (type >= 0x0C && type <= 0x0E) {     /* FetchAdd / Swap / CAS — 데이터 필수 */
        if (!data) return -1;
        c = OC_NPW;
    } else return -1;
    memset(out, 0, sizeof *out);
    out->cls = c;
    out->tc  = (uint8_t)((dw0[1] >> 4) & 7u);
    out->ido = (dw0[1] >> 2) & 1u;
    out->ro  = (dw0[2] >> 5) & 1u;
    out->name = "tlp";
    return 0;
}

/* ---- 연습 2: "data 보다 flag 가 먼저 보이는" 합법 도착 순서가 있나 (n <= 8, 전수) ---- */
static bool next_perm(uint8_t *a, size_t n) {      /* 사전순 다음 순열 */
    if (n < 2) return false;
    size_t i = n - 1;
    while (i > 0 && a[i - 1] >= a[i]) i--;
    if (i == 0) return false;
    size_t j = n - 1;
    while (a[j] <= a[i - 1]) j--;
    uint8_t t = a[i - 1]; a[i - 1] = a[j]; a[j] = t;
    for (size_t l = i, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return true;
}

static bool flag_before_data_possible(const txn *s, size_t n, uint32_t data_mask, size_t flag) {
    uint8_t a[8]; ord_viol v;
    for (size_t i = 0; i < n; i++) a[i] = (uint8_t)i;
    do {
        if (ord_check(s, n, a, n, &v) != ORD_OK) continue;
        uint32_t seen = 0;
        for (size_t p = 0; p < n && a[p] != flag; p++) seen |= 1u << a[p];
        if ((seen & data_mask) != data_mask) return true;          /* flag 가 어떤 data 보다 먼저 */
    } while (next_perm(a, n));
    return false;
}

/* ---- 연습 3: Device Control 이 허용할 때만 RO/IDO 를 싣는다 ----
 * 요청: RO 는 DEVCTL_RELAX_EN, IDO 는 DEVCTL2_IDO_REQ_EN. 완료: RO 는 요청에서 복사(그대로), IDO 는 IDO_CMP_EN. */
static void sanitize_attrs(txn *t, uint16_t devctl, uint16_t devctl2) {
    if (t->cls == OC_CPL) {
        if (!(devctl2 & PCI_EXP_DEVCTL2_IDO_CMP_EN)) t->ido = false;
    } else {
        if (!(devctl & PCI_EXP_DEVCTL_RELAX_EN)) t->ro = false;
        if (!(devctl2 & PCI_EXP_DEVCTL2_IDO_REQ_EN)) t->ido = false;
    }
}

static size_t count_legal_n(const txn *s, size_t n) {
    uint8_t a[8]; ord_viol v; size_t cnt = 0;
    for (size_t i = 0; i < n; i++) a[i] = (uint8_t)i;
    do { if (ord_check(s, n, a, n, &v) == ORD_OK) cnt++; } while (next_perm(a, n));
    return cnt;
}

/* ---- 연습 4: 두 노드 데드락 시뮬레이션 ----
 * X = 스위치, Y = 그 아래 장치(내부에 함수 Y1, Y2). ACS redirect 로 Y1→Y2 P2P 쓰기 P0 가 X 로 올라왔다가
 * 같은 링크로 다시 내려가야 한다. 버퍼는 전부 1~2칸.
 *   X→Y 송신 큐(최대 2): [R (X 의 MRd → Y), P1]           Y 의 NP 수신 버퍼(1칸): R' 이 차 있음 (답 C' 를 보내야 비움)
 *   Y→X 송신 큐(최대 2): [P2 (Y 의 MWr), C' (R' 의 CplD)] X 의 P 수신 버퍼(1칸): P0 이 차 있음 (X→Y 큐에 자리가 나야 비움)
 * Y 는 P·CPL 을 받는 즉시 소비, X 는 CPL 을 즉시 소비. */
typedef int (*pick_fn)(const txn *q, const bool *sent, size_t n, const credits *cr);

#define QCAP 2
typedef struct { txn t[QCAP]; bool frees_np[QCAP]; size_t n; } squeue;

static void sq_push(squeue *q, txn t, bool frees_np) { q->t[q->n] = t; q->frees_np[q->n] = frees_np; q->n++; }
static void sq_remove(squeue *q, size_t k) {
    for (size_t i = k + 1; i < q->n; i++) { q->t[i - 1] = q->t[i]; q->frees_np[i - 1] = q->frees_np[i]; }
    q->n--;
}

typedef struct {
    squeue xq, yq;
    int x_rx_p;          /* X 의 P 수신 버퍼 점유 (P0 가 X→Y 로 나갈 차례를 기다림) */
    int y_rx_np;         /* Y 의 NP 수신 버퍼 점유 */
    int y_need_cpl;      /* Y 가 받았지만 아직 큐에 넣지 못한 읽기의 완료 수 */
    int delivered;       /* 목적지에 도착한 TLP 수 */
    char x_order[8][8];  /* X→Y 링크에 나간 순서 (이름) */
    int nx;
} sim2;

static int sim_step(sim2 *s, pick_fn pick) {
    int moved = 0;
    bool sent[QCAP] = { false, false };
    credits to_y = { 1, (uint8_t)(1 - s->y_rx_np), 1 };
    int k = pick(s->xq.t, sent, s->xq.n, &to_y);
    if (k >= 0) {
        txn t = s->xq.t[k]; sq_remove(&s->xq, (size_t)k); moved++;
        snprintf(s->x_order[s->nx++ & 7], 8, "%s", t.name);
        if (t.cls == OC_NPR) { s->y_rx_np++; s->y_need_cpl++; }
        s->delivered++;
    }
    credits to_x = { (uint8_t)(1 - s->x_rx_p), 1, 1 };
    k = pick(s->yq.t, sent, s->yq.n, &to_x);
    if (k >= 0) {
        txn t = s->yq.t[k]; bool f = s->yq.frees_np[k]; sq_remove(&s->yq, (size_t)k); moved++;
        if (t.cls == OC_P) s->x_rx_p++;            /* X 가 받아서 Y 쪽으로 되돌려 보내야 하는 P2P 쓰기 */
        else s->delivered++;
        if (f) s->y_rx_np--;
    }
    if (s->x_rx_p > 0 && s->xq.n < QCAP) {         /* X: P 수신 버퍼 → X→Y 송신 큐 */
        txn p = { OC_P, 0, false, false, 0x0101, 0, 0, "Pfwd" };
        sq_push(&s->xq, p, false); s->x_rx_p--; moved++;
    }
    if (s->y_need_cpl > 0 && s->yq.n < QCAP) {     /* Y: 받은 읽기의 완료를 큐에 넣는다 */
        txn c = { OC_CPL, 0, false, false, 0x0000, 2, 0x0100, "C_R" };
        sq_push(&s->yq, c, true); s->y_need_cpl--; moved++;
    }
    return moved;
}

static void sim_init(sim2 *s) {
    memset(s, 0, sizeof *s);
    txn R  = { OC_NPR, 0, false, false, 0x0000, 2, 0, "R" };
    txn P1 = { OC_P,   0, false, false, 0x0000, 0, 0, "P1" };
    txn P2 = { OC_P,   0, false, false, 0x0100, 0, 0, "P2" };
    txn C1 = { OC_CPL, 0, false, false, 0x0000, 1, 0x0100, "C'" };
    sq_push(&s->xq, R, false); sq_push(&s->xq, P1, false);
    sq_push(&s->yq, P2, false); sq_push(&s->yq, C1, true);
    s->x_rx_p = 1;       /* P0 */
    s->y_rx_np = 1;      /* R' */
}

/* 반환: 다 비우는 데 걸린 단계 수, 진전 없는 단계가 생기면 -1 (데드락) */
static int sim_run(sim2 *s, pick_fn pick) {
    for (int step = 1; step <= 50; step++) {
        if (sim_step(s, pick) == 0) return -1;
        if (s->xq.n == 0 && s->yq.n == 0 && s->x_rx_p == 0 && s->y_rx_np == 0 && s->y_need_cpl == 0) return step;
    }
    return -1;
}

/* ================= 테스트 하네스 ================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static txn mk(ord_cls c, const char *name) { txn t; memset(&t, 0, sizeof t); t.cls = c; t.name = name; return t; }

int main(void) {
    /* 연습 1 */
    {
        static const struct { uint8_t b0; int rc; ord_cls c; } V[] = {
            { 0x00, 0, OC_NPR }, { 0x20, 0, OC_NPR }, { 0x01, 0, OC_NPR },        /* MRd32, MRd64, MRdLk */
            { 0x40, 0, OC_P   }, { 0x60, 0, OC_P   },                             /* MWr32, MWr64 */
            { 0x02, 0, OC_NPR }, { 0x42, 0, OC_NPW },                             /* IORd, IOWr */
            { 0x04, 0, OC_NPR }, { 0x05, 0, OC_NPR }, { 0x44, 0, OC_NPW }, { 0x45, 0, OC_NPW }, /* Cfg */
            { 0x0A, 0, OC_CPL }, { 0x4A, 0, OC_CPL }, { 0x0B, 0, OC_CPL }, { 0x4B, 0, OC_CPL }, /* Cpl(D)(Lk) */
            { 0x30, 0, OC_P   }, { 0x34, 0, OC_P   }, { 0x70, 0, OC_P   },        /* Msg, Msg, MsgD */
            { 0x4C, 0, OC_NPW }, { 0x6D, 0, OC_NPW }, { 0x4E, 0, OC_NPW },        /* FetchAdd, Swap64, CAS */
            { 0x41, -1, OC_P }, { 0x24, -1, OC_P }, { 0x10, -1, OC_P }, { 0x0C, -1, OC_P },
            { 0x2A, -1, OC_P }, { 0x80, -1, OC_P }, { 0x1F, -1, OC_P },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
            uint8_t d[4] = { V[i].b0, 0, 0, 1 }; txn t;
            int rc = classify_tlp(d, &t);
            if (rc != V[i].rc || (rc == 0 && t.cls != V[i].c)) { bad++; printf("  bad b0=0x%02X\n", V[i].b0); }
        }
        CHECK(bad == 0);
        /* MWr32, TC=3, IDO=1, RO=1, NS=0, Length=16 → 40 34 20 10 */
        uint8_t d[4] = { 0x40, 0x34, 0x20, 0x10 }; txn t;
        CHECK(classify_tlp(d, &t) == 0 && t.cls == OC_P && t.tc == 3 && t.ido && t.ro);
        uint8_t d2[4] = { 0x4A, 0x00, 0x10, 0x01 };            /* CplD, NS 만 1 */
        CHECK(classify_tlp(d2, &t) == 0 && t.cls == OC_CPL && !t.ro && !t.ido && t.tc == 0);
    }

    /* 연습 2 */
    {
        txn s[4] = { mk(OC_P, "d0"), mk(OC_P, "d1"), mk(OC_P, "d2"), mk(OC_P, "CQE") };
        s[0].ro = s[1].ro = s[2].ro = true;
        CHECK(!flag_before_data_possible(s, 4, 0x7u, 3));            /* (a) CQE RO=0 → 안전 */
        s[3].ro = true;
        CHECK(flag_before_data_possible(s, 4, 0x7u, 3));             /* (b) CQE 에 RO → 위험 */

        txn w = mk(OC_P, "data by fn0"); w.rid = 0x0100;             /* 01:00.0 */
        txn c = mk(OC_CPL, "status CplD"); c.rid = 0x0000; c.tag = 4; c.cid = 0x0100; c.ido = true;
        txn sc[2] = { w, c };
        CHECK(!flag_before_data_possible(sc, 2, 0x1u, 1));           /* (c) 같은 함수가 상태 응답 → IDO 라도 안전 */
        sc[1].cid = 0x0101;                                          /* 01:00.1 이 fn0 의 일을 대신 보고 */
        CHECK(flag_before_data_possible(sc, 2, 0x1u, 1));            /* (d) IDO 가 producer/consumer 를 깬다 */
        sc[1].cid = 0x0100; sc[1].ido = false; sc[1].ro = true;
        CHECK(flag_before_data_possible(sc, 2, 0x1u, 1));            /* (e) 상태 읽기 완료에 RO → 위험 */
    }

    /* 연습 3 */
    {
        uint16_t devctl_on  = PCI_EXP_DEVCTL_READRQ_512B | PCI_EXP_DEVCTL_NOSNOOP_EN | PCI_EXP_DEVCTL_RELAX_EN; /* 0x2810 */
        uint16_t devctl_off = PCI_EXP_DEVCTL_READRQ_512B | PCI_EXP_DEVCTL_NOSNOOP_EN;                           /* 0x2800 */
        CHECK(devctl_on == 0x2810 && devctl_off == 0x2800);
        txn base[5] = { mk(OC_P, "d0"), mk(OC_P, "d1"), mk(OC_P, "d2"), mk(OC_P, "CQE"), mk(OC_P, "MSI-X") };
        base[0].ro = base[1].ro = base[2].ro = true;
        txn s[5];
        memcpy(s, base, sizeof s);
        for (int i = 0; i < 5; i++) sanitize_attrs(&s[i], devctl_on, 0);
        size_t on = count_legal_n(s, 5);
        memcpy(s, base, sizeof s);
        for (int i = 0; i < 5; i++) sanitize_attrs(&s[i], devctl_off, 0);
        size_t off = count_legal_n(s, 5);
        printf("practice 3: legal orders with RlxdOrd+ = %zu, RlxdOrd- = %zu\n", on, off);
        CHECK(on == 6 && off == 1 && !s[0].ro);
        txn r = mk(OC_NPR, "R"); r.ido = true;
        sanitize_attrs(&r, 0, PCI_EXP_DEVCTL2_IDO_CMP_EN);           /* 완료용 비트만 켬 → 요청 IDO 는 꺼짐 */
        CHECK(!r.ido);
        txn c = mk(OC_CPL, "C"); c.ido = true; c.ro = true;
        sanitize_attrs(&c, 0, PCI_EXP_DEVCTL2_IDO_CMP_EN);
        CHECK(c.ido && c.ro);                                        /* 완료 RO 는 요청에서 복사된 값 그대로 */
    }

    /* 연습 4 */
    {
        CHECK(must_be_able_to_pass(OC_P, OC_NPR) && must_be_able_to_pass(OC_CPL, OC_NPR) && !must_be_able_to_pass(OC_CPL, OC_P));
        sim2 s;
        sim_init(&s);
        int fifo = sim_run(&s, fifo_pick);
        printf("practice 4 FIFO : %s (xq=%zu yq=%zu x_rx_p=%d y_rx_np=%d)\n", fifo < 0 ? "DEADLOCK" : "drained",
               s.xq.n, s.yq.n, s.x_rx_p, s.y_rx_np);
        CHECK(fifo == -1 && s.xq.n == 2 && s.yq.n == 2 && s.x_rx_p == 1 && s.y_rx_np == 1);
        sim_init(&s);
        int ok = sim_run(&s, sched_pick);
        printf("practice 4 sched: drained in %d steps, X->Y order:", ok);
        for (int i = 0; i < s.nx; i++) printf(" %s", s.x_order[i]);
        printf("\n");
        CHECK(ok > 0 && strcmp(s.x_order[0], "P1") == 0 && s.delivered == 6);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
