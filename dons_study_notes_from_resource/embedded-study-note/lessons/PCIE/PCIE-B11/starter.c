/* PCIE-B11 Posted/Non-posted/Completion과 Ordering 규칙 — ordering 검사기
 *
 * 트랜잭션 4종류 (열/행 이름):
 *   P   = Posted Request          (MWr, Message — MSI/MSI-X 도 MWr)
 *   NPR = Non-Posted Read Request  (MRd, IORd, CfgRd, MRdLk)
 *   NPW = Non-Posted Request with Data (IOWr, CfgWr, AtomicOp)
 *   CPL = Completion               (Cpl, CplD, CplLk, CplDLk)
 *
 * 순서 표 (PCIe Base Spec r3.0+ "Ordering Rules Summary" 표를 이 레슨이 옮긴 것 — md 5장 표 참고):
 *   행(row) = 나중에 보낸 트랜잭션, 열(column) = 먼저 보낸 트랜잭션. "행이 열을 앞질러도 되나?"
 *   Yes = 앞지를 수 있어야 한다(데드락 방지, 필수)  Y/N = 앞질러도 되고 안 해도 됨  No = 앞지르면 안 됨
 *   No 칸 중 일부는 (b) 예외가 있다: RO(Relaxed Ordering, Attr[1]) / IDO(ID-based Ordering, Attr[2]).
 *
 * Part A: 표 ORD_TABLE 채우기, 도착 순서 검사기 ord_check (순열 검사 + 모든 역전 쌍 검사)
 * Part B: may_pass 의 예외 — 다른 TC, RO, IDO(ID 비교), 같은 Transaction ID 완료끼리(D5b)
 * Part C: credit 이 부족할 때 "앞질러도 되는 것"을 먼저 보내는 송신 스케줄러 sched_pick (데드락 회피),
 *         드라이버 시나리오 — writel 도어벨 + readl read-back, 장치 쪽 data → CQE → MSI-X
 *
 * 모델 범위: 같은 링크·같은 방향으로 흐르는 TLP 열 하나. 1 credit = TLP 헤더 1개 (data credit 생략).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
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
    /* TODO A1: 16칸을 채운다 — 값(V_NO/V_YN/V_YES), 예외 종류(K_*), 칸 이름("A2a", 예외면 "A2b"). 지금은 전부 Y/N */
    /* P   */ { { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL } },
    /* NPR */ { { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL } },
    /* NPW */ { { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL } },
    /* CPL */ { { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL }, { V_YN, K_NONE, "?", NULL } },
};

/* 출발 ID: 요청은 Requester ID, 완료는 Completer ID (IDO 비교에 쓰는 값) */
static uint16_t src_id(const txn *t) { return t->cls == OC_CPL ? t->cid : t->rid; }

/* ---------------- Part B: 예외를 반영한 "앞질러도 되나" ---------------- */

/* later 가 earlier 를 앞질러 도착해도 되면 true. *rule 에 근거 칸 이름("A2a", "D5b", "TC" ...) */
static bool may_pass(const txn *later, const txn *earlier, const char **rule) {
    const ord_cell *c = &ORD_TABLE[later->cls][earlier->cls];
    (void)src_id;
    /* TODO B1: (1) TC 가 다르면 "TC", true.
     *          (2) No 칸: K_RO_IDO 면 later->ro, K_RO_IDO/K_IDO 면 later->ido && src_id 다름 → idb, true / 아니면 ida, false.
     *          (3) K_TXID 칸에서 Transaction ID(rid, tag)가 같으면 idb, false (D5b). 지금은 표 값만 본다. */
    *rule = c->ida;
    return c->v != V_NO;
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
    (void)issued; (void)n; (void)arrival; (void)m; (void)v;
    /* TODO A2: 인자 검사 → arrival 이 0..n-1 의 순열인지(방문 비트맵) + pos[] 만들기
     *          → 도착 순서 a 마다 j = arrival[a], 먼저 보낸 i < j 중 아직 안 온 것(pos[i] > a)에 대해 may_pass 검사 */
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
    /* TODO C1: credit 이 있고, 앞에 남은(안 보낸) 모든 것을 may_pass 로 앞질러도 되는 첫 인덱스. 지금은 FIFO 와 같다 */
    return fifo_pick(q, sent, n, cr);
}

/* ================= 테스트 하네스 ================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

#define BDF(b, d, f) ((uint16_t)(((b) << 8) | ((d) << 3) | (f)))

static txn mk(ord_cls c, const char *name) {
    txn t; memset(&t, 0, sizeof t); t.cls = c; t.name = name; return t;
}
static txn mk_cpl(uint16_t req_rid, uint8_t tag, uint16_t cid, const char *name) {
    txn t = mk(OC_CPL, name); t.rid = req_rid; t.tag = tag; t.cid = cid; return t;
}

/* 2개짜리: "b 가 a 를 앞질러 도착" 이 합법인가 */
static int swap2(txn a, txn b, ord_viol *v) {
    txn s[2] = { a, b }; const uint8_t arr[2] = { 1, 0 };
    return ord_check(s, 2, arr, 2, v);
}

/* 모든 도착 순열 중 합법인 것의 수 (n <= 8, 하네스 도우미) */
static size_t count_legal(const txn *s, size_t n) {
    uint8_t a[8], c[8] = {0}; size_t cnt = 0; ord_viol v;
    for (size_t i = 0; i < n; i++) a[i] = (uint8_t)i;
    if (ord_check(s, n, a, n, &v) == ORD_OK) cnt++;
    size_t i = 1;
    while (i < n) {                                     /* Heap's algorithm (반복형) */
        if (c[i] < i) {
            size_t k = (i % 2 == 0) ? 0 : c[i];
            uint8_t t = a[k]; a[k] = a[i]; a[i] = t;
            if (ord_check(s, n, a, n, &v) == ORD_OK) cnt++;
            c[i]++; i = 1;
        } else { c[i] = 0; i++; }
    }
    return cnt;
}

static const char *CLS_NAME[OC_COUNT] = { "P", "NPR", "NPW", "CPL" };

static void print_table(void) {
    static const char *VN[3] = { "No", "Y/N", "Yes" };
    printf("ordering table (row=later passes column=earlier?), RO=IDO=0, same TC, same IDs:\n      ");
    for (int c = 0; c < OC_COUNT; c++) printf("%-14s", CLS_NAME[c]);
    printf("\n");
    for (int r = 0; r < OC_COUNT; r++) {
        printf("%-6s", CLS_NAME[r]);
        for (int c = 0; c < OC_COUNT; c++) {
            char buf[16];
            snprintf(buf, sizeof buf, "%s %s", ORD_TABLE[r][c].ida ? ORD_TABLE[r][c].ida : "-", VN[ORD_TABLE[r][c].v]);
            printf("%-14s", buf);
        }
        printf("\n");
    }
}

int main(void) {
    ord_viol v; const char *r;
    print_table();

    /* ---- Part A: 16칸 기본값 (RO=IDO=0, 같은 TC, 같은 ID, 같은 Transaction ID 아님) ---- */
    {
        static const ord_val want[4][4] = {
            { V_NO, V_YES, V_YES, V_YN },
            { V_NO, V_YN,  V_YN,  V_YN },
            { V_NO, V_YN,  V_YN,  V_YN },
            { V_NO, V_YES, V_YES, V_YN },
        };
        int bad = 0;
        for (int rr = 0; rr < 4; rr++)
            for (int cc = 0; cc < 4; cc++) {
                txn later = mk((ord_cls)rr, "L"), earlier = mk((ord_cls)cc, "E");
                later.tag = 1; earlier.tag = 2;                 /* D5: 다른 Transaction ID */
                bool ok = may_pass(&later, &earlier, &r);
                if (ORD_TABLE[rr][cc].v != want[rr][cc] || ok != (want[rr][cc] != V_NO)) bad++;
                if (must_be_able_to_pass((ord_cls)rr, (ord_cls)cc) != (want[rr][cc] == V_YES)) bad++;
            }
        CHECK(bad == 0);
    }

    /* A: MWr(doorbell) → MRd(read-back). 읽기가 쓰기를 앞지르면 안 된다 = read-back 이 posted write 를 flush */
    {
        txn wr = mk(OC_P, "MWr doorbell"), rd = mk(OC_NPR, "MRd status");
        CHECK(swap2(wr, rd, &v) == ORD_ILLEGAL && v.later == 1 && v.earlier == 0 && strcmp(v.rule, "B2a") == 0);
        CHECK(swap2(rd, wr, &v) == ORD_OK);              /* 반대 순서로 보냈다면 쓰기가 읽기를 앞질러도 됨 (A3) */
    }
    /* A: posted write 끼리는 순서 유지 (A2a) */
    {
        txn s[3] = { mk(OC_P, "W0"), mk(OC_P, "W1"), mk(OC_P, "W2") };
        const uint8_t in_order[3] = { 0, 1, 2 }, swapped[3] = { 0, 2, 1 };
        CHECK(ord_check(s, 3, in_order, 3, &v) == ORD_OK);
        CHECK(ord_check(s, 3, swapped, 3, &v) == ORD_ILLEGAL && v.later == 2 && v.earlier == 1 && strcmp(v.rule, "A2a") == 0);
        CHECK(count_legal(s, 3) == 1);
    }
    /* A: CplD 는 앞서 보낸 posted write 를 앞지르면 안 된다 (D2a) — 장치가 data MWr 후 상태 읽기에 응답 */
    {
        txn data = mk(OC_P, "MWr data"), cpl = mk_cpl(BDF(0, 0, 0), 7, BDF(1, 0, 0), "CplD flag");
        data.rid = BDF(1, 0, 0);
        CHECK(swap2(data, cpl, &v) == ORD_ILLEGAL && strcmp(v.rule, "D2a") == 0);
    }
    /* A: config write(NPW) 는 posted 를 앞지르면 안 됨(C2a), posted 는 NPW 를 앞질러야 함(A4 Yes) */
    {
        txn mw = mk(OC_P, "MWr"), cw = mk(OC_NPW, "CfgWr");
        CHECK(swap2(mw, cw, &v) == ORD_ILLEGAL && strcmp(v.rule, "C2a") == 0);
        CHECK(swap2(cw, mw, &v) == ORD_OK);
    }
    /* A: 손으로 센 합법 도착 순서 수 */
    {
        txn s1[3] = { mk(OC_P, "W0"), mk(OC_P, "W1"), mk(OC_NPR, "R") };          /* R 는 두 W 뒤에만 */
        txn s2[3] = { mk(OC_NPR, "R"), mk(OC_P, "W0"), mk(OC_P, "W1") };          /* R 의 자리 3곳 */
        txn s3[3] = { mk(OC_NPR, "R"), mk(OC_NPW, "CW"), mk(OC_P, "W") };         /* 전부 허용 6 */
        CHECK(count_legal(s1, 3) == 1);
        CHECK(count_legal(s2, 3) == 3);
        CHECK(count_legal(s3, 3) == 6);
    }
    /* A: 인자 검사 */
    {
        txn s[3] = { mk(OC_P, "a"), mk(OC_P, "b"), mk(OC_P, "c") };
        const uint8_t dup[3] = { 0, 1, 1 }, oob[3] = { 0, 1, 3 }, ok3[3] = { 0, 1, 2 };
        v.later = 99;
        CHECK(ord_check(s, 3, dup, 3, &v) == ORD_ERR_ARG && v.later == 99);
        CHECK(ord_check(s, 3, oob, 3, &v) == ORD_ERR_ARG);
        CHECK(ord_check(s, 3, ok3, 2, &v) == ORD_ERR_ARG);              /* 도착 수 ≠ 보낸 수 (손실) */
        CHECK(ord_check(NULL, 3, ok3, 3, &v) == ORD_ERR_ARG);
        CHECK(ord_check(s, 3, ok3, 3, NULL) == ORD_ERR_ARG);
        txn big[ORD_MAX + 1]; uint8_t ab[ORD_MAX + 1];
        for (size_t i = 0; i <= ORD_MAX; i++) { big[i] = mk(OC_P, "w"); ab[i] = (uint8_t)i; }
        CHECK(ord_check(big, ORD_MAX + 1, ab, ORD_MAX + 1, &v) == ORD_ERR_ARG);
        CHECK(ord_check(big, ORD_MAX, ab, ORD_MAX, &v) == ORD_OK);       /* 32개는 받는다 */
        CHECK(ord_check(s, 0, ok3, 0, &v) == ORD_OK);                    /* 빈 열은 합법 */
    }

    /* ---- Part B: 예외 (RO / IDO / TC / Transaction ID) ---- */
    {
        txn w0 = mk(OC_P, "W0"), w1 = mk(OC_P, "W1");
        w1.ro = true;                                             /* 나중 것이 RO → 앞질러도 됨 */
        CHECK(swap2(w0, w1, &v) == ORD_OK && may_pass(&w1, &w0, &r) && strcmp(r, "A2b") == 0);
        w1.ro = false; w0.ro = true;                              /* 먼저 것만 RO → 소용없음 */
        CHECK(swap2(w0, w1, &v) == ORD_ILLEGAL && strcmp(v.rule, "A2a") == 0);
    }
    {   /* IDO: Requester ID 가 달라야 */
        txn wa = mk(OC_P, "W fn0"), wb = mk(OC_P, "W fn1");
        wa.rid = BDF(1, 0, 0); wb.rid = BDF(1, 0, 1); wb.ido = true;
        CHECK(swap2(wa, wb, &v) == ORD_OK);
        wb.rid = BDF(1, 0, 0);
        CHECK(swap2(wa, wb, &v) == ORD_ILLEGAL && strcmp(v.rule, "A2a") == 0);
    }
    {   /* B2b: 읽기는 IDO 로만, RO 로는 posted 를 못 앞지른다 */
        txn w = mk(OC_P, "W fn0"), rd = mk(OC_NPR, "R fn1");
        w.rid = BDF(1, 0, 0); rd.rid = BDF(1, 0, 1);
        rd.ro = true;
        CHECK(swap2(w, rd, &v) == ORD_ILLEGAL && strcmp(v.rule, "B2a") == 0);
        rd.ro = false; rd.ido = true;
        CHECK(swap2(w, rd, &v) == ORD_OK && may_pass(&rd, &w, &r) && strcmp(r, "B2b") == 0);
        rd.rid = BDF(1, 0, 0);                                    /* 같은 함수의 읽기 → IDO 도 소용없음 */
        CHECK(swap2(w, rd, &v) == ORD_ILLEGAL);
    }
    {   /* C2b (이 레슨 모델: RO 또는 IDO+다른 RID) */
        txn w = mk(OC_P, "W"), aw = mk(OC_NPW, "AtomicOp");
        aw.ro = true;
        CHECK(swap2(w, aw, &v) == ORD_OK && may_pass(&aw, &w, &r) && strcmp(r, "C2b") == 0);
    }
    {   /* D2b: 완료의 IDO 는 Completer ID 와 posted 의 Requester ID 비교 */
        txn w = mk(OC_P, "MWr from 01:00.0"); w.rid = BDF(1, 0, 0);
        txn c = mk_cpl(BDF(0, 0, 0), 3, BDF(1, 0, 0), "CplD from 01:00.0");
        c.ido = true;
        CHECK(swap2(w, c, &v) == ORD_ILLEGAL && strcmp(v.rule, "D2a") == 0);   /* 같은 장치 → No */
        c.cid = BDF(2, 0, 0);
        CHECK(swap2(w, c, &v) == ORD_OK);                                     /* 다른 장치 → 허용 */
        c.ido = false; c.ro = true;
        CHECK(swap2(w, c, &v) == ORD_OK && may_pass(&c, &w, &r) && strcmp(r, "D2b") == 0);
    }
    {   /* D5: 같은 Transaction ID(RID+Tag)의 분할 완료끼리는 순서 유지, 다르면 자유 */
        txn c0 = mk_cpl(BDF(0, 0, 0), 5, BDF(1, 0, 0), "CplD tag5 #0");
        txn c1 = mk_cpl(BDF(0, 0, 0), 5, BDF(1, 0, 0), "CplD tag5 #1");
        txn c2 = mk_cpl(BDF(0, 0, 0), 6, BDF(1, 0, 0), "CplD tag6");
        CHECK(swap2(c0, c1, &v) == ORD_ILLEGAL && strcmp(v.rule, "D5b") == 0);
        CHECK(swap2(c0, c2, &v) == ORD_OK);
        c2.tag = 5; c2.rid = BDF(0, 0, 1);                        /* Tag 같아도 RID 가 다르면 다른 트랜잭션 */
        CHECK(swap2(c0, c2, &v) == ORD_OK);
        txn s[3] = { c0, c1, mk_cpl(BDF(0, 0, 0), 6, BDF(1, 0, 0), "tag6") };
        CHECK(count_legal(s, 3) == 3);
    }
    {   /* TC 가 다르면 어떤 쌍도 순서 무관 */
        txn w = mk(OC_P, "W tc0"), rd = mk(OC_NPR, "R tc1");
        rd.tc = 1;
        CHECK(swap2(w, rd, &v) == ORD_OK && may_pass(&rd, &w, &r) && strcmp(r, "TC") == 0);
        txn w1 = mk(OC_P, "W1 tc7"); w1.tc = 7;
        CHECK(swap2(w, w1, &v) == ORD_OK);
    }
    {   /* 첫 위반 보고 규칙: 도착 순서로 먼저 나온 later, 그 안에서 가장 먼저 보낸 earlier */
        txn s[4] = { mk(OC_P, "W0"), mk(OC_P, "W1"), mk(OC_NPR, "R2"), mk(OC_P, "W3") };
        const uint8_t arr[4] = { 2, 3, 0, 1 };
        CHECK(ord_check(s, 4, arr, 4, &v) == ORD_ILLEGAL && v.later == 2 && v.earlier == 0 && strcmp(v.rule, "B2a") == 0);
    }
    {   /* 랜덤: 위반이 보고되면 그 쌍은 실제로 역전돼 있고 may_pass 가 거짓, OK 면 모든 역전 쌍이 허용 */
        uint32_t seed = 0x1234567u; int bad = 0, illegal = 0;
        for (int it = 0; it < 3000; it++) {
            txn s[8]; uint8_t arr[8], pos[8];
            size_t n = 2 + xs32(&seed) % 7;
            for (size_t i = 0; i < n; i++) {
                s[i] = mk((ord_cls)(xs32(&seed) % 4), "x");
                s[i].ro = xs32(&seed) % 4 == 0; s[i].ido = xs32(&seed) % 4 == 0;
                s[i].tc = (uint8_t)(xs32(&seed) % 8 == 0); s[i].rid = (uint16_t)(xs32(&seed) % 2);
                s[i].cid = (uint16_t)(xs32(&seed) % 2); s[i].tag = (uint8_t)(xs32(&seed) % 2);
                arr[i] = (uint8_t)i;
            }
            for (size_t i = n - 1; i > 0; i--) { size_t k = xs32(&seed) % (i + 1); uint8_t t = arr[i]; arr[i] = arr[k]; arr[k] = t; }
            for (size_t a = 0; a < n; a++) pos[arr[a]] = (uint8_t)a;
            bool any_bad = false;
            for (size_t i = 0; i < n; i++)
                for (size_t j = i + 1; j < n; j++)
                    if (pos[j] < pos[i] && !may_pass(&s[j], &s[i], &r)) any_bad = true;
            int rc = ord_check(s, n, arr, n, &v);
            if (rc == ORD_ILLEGAL) {
                illegal++;
                if (!any_bad || v.earlier >= v.later || pos[v.later] >= pos[v.earlier] ||
                    may_pass(&s[v.later], &s[v.earlier], &r)) bad++;
            } else if (rc != ORD_OK || any_bad) bad++;
        }
        printf("B random: 3000 sequences (n=2..8), illegal=%d, mismatches=%d\n", illegal, bad);
        CHECK(bad == 0 && illegal > 1000 && illegal < 3000);
    }

    /* ---- Part C: credit 스케줄러와 드라이버 시나리오 ---- */
    {   /* NP credit 0: FIFO 는 맨 앞 읽기에 막혀 멈춤, 순서 인식 스케줄러는 뒤의 posted 를 보낸다 (A3 Yes) */
        txn q[3] = { mk(OC_NPR, "MRd"), mk(OC_P, "MWr0"), mk(OC_P, "MWr1") };
        bool sent[3] = { false, false, false };
        credits cr = { 2, 0, 0 };
        CHECK(fifo_pick(q, sent, 3, &cr) == -1);
        CHECK(sched_pick(q, sent, 3, &cr) == 1);
        sent[1] = true; cr.p--;
        CHECK(sched_pick(q, sent, 3, &cr) == 2);
        sent[2] = true; cr.p--;
        CHECK(sched_pick(q, sent, 3, &cr) == -1);                 /* 읽기는 NP credit 을 기다림 */
        cr.np = 1;
        CHECK(sched_pick(q, sent, 3, &cr) == 0);
    }
    {   /* P credit 0: 쓰기 뒤의 읽기는 기다려야 한다 (B2a) — 스케줄러도 읽기를 먼저 보내지 않는다 */
        txn q[2] = { mk(OC_P, "MWr doorbell"), mk(OC_NPR, "MRd read-back") };
        bool sent[2] = { false, false };
        credits cr = { 0, 4, 0 };
        CHECK(sched_pick(q, sent, 2, &cr) == -1);
        q[1].ido = true; q[1].rid = BDF(1, 0, 1); q[0].rid = BDF(1, 0, 0);   /* 다른 함수 + IDO 면 먼저 감 */
        CHECK(sched_pick(q, sent, 2, &cr) == 1);
    }
    {   /* 완료는 NP credit 이 막혀도 나가야 한다 (D3 Yes), 같은 Transaction ID 의 분할 완료 순서는 지킨다 */
        txn q[4] = { mk(OC_NPR, "MRd"), mk_cpl(0, 9, 0x100, "CplD t9 #0"), mk_cpl(0, 9, 0x100, "CplD t9 #1"), mk(OC_NPW, "CfgWr") };
        bool sent[4] = { false, false, false, false };
        credits cr = { 0, 0, 1 };
        CHECK(fifo_pick(q, sent, 4, &cr) == -1 && sched_pick(q, sent, 4, &cr) == 1);
        sent[1] = true;
        CHECK(sched_pick(q, sent, 4, &cr) == 2);
    }
    {   /* 랜덤 드레인: 스케줄러가 만든 도착 순서는 항상 검사기를 통과하고, 언젠가 전부 나간다 */
        uint32_t seed = 0xC0FFEEu; int bad = 0, reorders = 0;
        for (int it = 0; it < 2000; it++) {
            txn q[10]; bool sent[10] = { false }; uint8_t arr[10]; size_t na = 0;
            size_t n = 10;
            for (size_t i = 0; i < n; i++) {
                q[i] = mk((ord_cls)(xs32(&seed) % 4), "x");
                q[i].ro = xs32(&seed) % 3 == 0; q[i].ido = xs32(&seed) % 3 == 0;
                q[i].rid = (uint16_t)(xs32(&seed) % 3); q[i].cid = (uint16_t)(xs32(&seed) % 3);
                q[i].tag = (uint8_t)(xs32(&seed) % 3);
            }
            credits cr = { 0, 0, 0 };
            for (int step = 0; step < 400 && na < n; step++) {
                switch (xs32(&seed) % 3) {                    /* 받는 쪽이 무작위로 credit 반환 */
                case 0: if (cr.p < 2) cr.p++; break;
                case 1: if (cr.np < 2) cr.np++; break;
                default: if (cr.cpl < 2) cr.cpl++; break;
                }
                int k = sched_pick(q, sent, n, &cr);
                if (k >= 0) { sent[k] = true; (*credit_slot(&cr, q[k].cls))--; arr[na++] = (uint8_t)k; }
            }
            if (na != n || ord_check(q, n, arr, n, &v) != ORD_OK) bad++;
            for (size_t a = 0; a < na; a++) if (arr[a] != a) { reorders++; break; }
        }
        printf("C drain: 2000 queues x 10 TLPs, reordered=%d, illegal-or-stuck=%d\n", reorders, bad);
        CHECK(bad == 0 && reorders > 1000);
    }
    {   /* 드라이버: 장치 → 호스트. data MWr ×3 → CQE MWr → MSI-X MWr (NVMe 완료 경로) */
        txn s[5] = { mk(OC_P, "data0"), mk(OC_P, "data1"), mk(OC_P, "data2"), mk(OC_P, "CQE"), mk(OC_P, "MSI-X") };
        CHECK(count_legal(s, 5) == 1);                             /* RO 없으면 보낸 순서 그대로 */
        s[0].ro = s[1].ro = s[2].ro = true;                        /* data 만 RO */
        CHECK(count_legal(s, 5) == 6);                             /* data 끼리만 섞임, CQE·MSI-X 는 마지막 */
        const uint8_t cqe_first[5] = { 3, 0, 1, 2, 4 };
        CHECK(ord_check(s, 5, cqe_first, 5, &v) == ORD_ILLEGAL && v.later == 3 && strcmp(v.rule, "A2a") == 0);
        s[3].ro = true;                                            /* 버그: CQE 에도 RO */
        CHECK(ord_check(s, 5, cqe_first, 5, &v) == ORD_OK);        /* 호스트가 data 전에 CQE 를 볼 수 있다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
