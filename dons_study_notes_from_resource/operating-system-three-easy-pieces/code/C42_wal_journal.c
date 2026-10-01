/*
 * C42_wal_journal.c — 아주 작은 write-ahead log(저널) + 크래시 복구 시뮬레이터
 *
 * "디스크" = 메모리 배열. 모든 디스크 쓰기를 순서대로 리스트에 담고,
 * "k번째 쓰기 직후 전원이 나갔다" = 초기 이미지에 앞의 k개 쓰기만 적용한 상태.
 * 그 상태에서 recover()(저널 재생)를 돌리고 mini-fsck(check)로 결과를 판정한다.
 *
 * 시나리오 (OSTEP 42장 예제: 파일에 블록 하나 append → I[v2], B[v2], Db)
 *   B. 저널 없이 3개 블록 중 일부만 기록된 8가지 경우
 *   A. data journaling: 매 단계마다 크래시
 *   C. TxB..TxE 를 한 번에 내보냈는데 디스크가 순서를 바꿈 (체크섬 없음/있음)
 *   D. ordered(metadata) journaling + "데이터를 커밋 뒤에 쓰는" 버그 버전
 *   E. 블록 재사용 + revoke 레코드 (ext3 의 그 악명 높은 케이스)
 *
 * build: cc -Wall -Wextra -O0 code/C42_wal_journal.c -o .work/bin/C42_wal_journal
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BS 64      /* 블록 크기(바이트) — 장난감이라 작게 */
#define NBLK 40
#define JSB 1      /* 저널 슈퍼블록 */
#define JSTART 2   /* 저널 영역 2..17 */
#define JLEN 16
#define DBMAP 19   /* data bitmap: '0'/'1' 문자 8개 */
#define INODE 20   /* inode 블록: inode 4개 */
#define DATA0 24   /* data block 0..7 = 디스크 블록 24..31 */

typedef struct { uint8_t b[NBLK][BS]; } disk_t;

typedef struct { int8_t used, size, ptr[4]; char name[10]; } inode_t; /* 16B */
typedef struct { char magic[4]; int32_t tid, n; int8_t addr[6], kind[6]; } txb_t;
typedef struct { char magic[4]; int32_t tid; uint32_t csum; } txe_t;
typedef struct { char magic[4]; int32_t start, tid; } jsb_t;

enum { K_COPY = 0, K_REVOKE = 1 };

typedef struct { int addr; uint8_t data[BS]; char label[28]; } wr_t;
typedef struct { wr_t w[64]; int n; } wlist_t;

static void add(wlist_t *wl, int addr, const uint8_t *buf, const char *label) {
    wr_t *w = &wl->w[wl->n++];
    w->addr = addr;
    memcpy(w->data, buf, BS);
    snprintf(w->label, sizeof w->label, "%s", label);
}

static uint32_t fnv1a(const uint8_t *p, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* ---------- 블록 내용 만들기 ---------- */
static void fill(uint8_t *blk, char c) { memset(blk, c, BS); }

static void mk_jsb(uint8_t *blk, int start, int tid) {
    jsb_t j = { "JSB", start, tid };
    memset(blk, 0, BS);
    memcpy(blk, &j, sizeof j);
}

static void mk_inode_blk(uint8_t *blk, const inode_t ino[4]) {
    memset(blk, 0, BS);
    memcpy(blk, ino, 4 * sizeof(inode_t));
}

static void get_inodes(const disk_t *d, inode_t ino[4]) {
    memcpy(ino, d->b[INODE], 4 * sizeof(inode_t));
}

/* 트랜잭션 하나를 저널에 기록하는 쓰기들을 wl 에 추가. 다음 저널 위치를 반환 */
typedef struct { int addr, kind; const uint8_t *data; const char *name; } item_t;

static int journal_tx(wlist_t *wl, int pos, int tid, const item_t *it, int n) {
    uint8_t blk[BS], content[6 * BS];
    txb_t b;
    memset(&b, 0, sizeof b);
    memcpy(b.magic, "TXB", 4);
    b.tid = tid;
    b.n = n;
    for (int i = 0; i < n; i++) { b.addr[i] = (int8_t)it[i].addr; b.kind[i] = (int8_t)it[i].kind; }
    memset(blk, 0, BS);
    memcpy(blk, &b, sizeof b);
    char lab[28];
    snprintf(lab, sizeof lab, "J: TxB(tid=%d)", tid);
    add(wl, pos, blk, lab);
    for (int i = 0; i < n; i++) {
        memcpy(content + i * BS, it[i].data, BS);
        snprintf(lab, sizeof lab, "J: %s", it[i].name);
        add(wl, pos + 1 + i, it[i].data, lab);
    }
    txe_t e = { "TXE", tid, fnv1a(content, (size_t)n * BS) };
    memset(blk, 0, BS);
    memcpy(blk, &e, sizeof e);
    snprintf(lab, sizeof lab, "J: TxE(tid=%d)", tid);
    add(wl, pos + 1 + n, blk, lab);
    return pos + 2 + n;
}

/* ---------- 복구: 저널 스캔 → (revoke 수집) → 재생 ---------- */
static int recover(disk_t *d, int verify_csum, char *log, size_t loglen) {
    jsb_t j;
    memcpy(&j, d->b[JSB], sizeof j);
    int pos = j.start, expect = j.tid, ntx = 0, nrev = 0, replayed = 0, skipped = 0;
    int txpos[8], txtid[8], revaddr[16], revtid[16];
    const char *why = "end of log";
    log[0] = 0;
    while (pos < JSTART + JLEN) {
        txb_t b;
        txe_t e;
        memcpy(&b, d->b[pos], sizeof b);
        if (memcmp(b.magic, "TXB", 4) != 0 || b.tid != expect) { why = "no more TxB"; break; }
        if (pos + 1 + b.n >= JSTART + JLEN) { why = "truncated"; break; }
        memcpy(&e, d->b[pos + 1 + b.n], sizeof e);
        if (memcmp(e.magic, "TXE", 4) != 0 || e.tid != b.tid) { why = "TxE missing (uncommitted)"; break; }
        if (verify_csum && fnv1a(d->b[pos + 1], (size_t)b.n * BS) != e.csum) {
            why = "checksum MISMATCH -> discard";
            break;
        }
        txpos[ntx] = pos; txtid[ntx] = b.tid; ntx++;
        for (int i = 0; i < b.n; i++)
            if (b.kind[i] == K_REVOKE) {
                int32_t a;
                memcpy(&a, d->b[pos + 1 + i], sizeof a);
                revaddr[nrev] = a; revtid[nrev] = b.tid; nrev++;
            }
        pos += b.n + 2;
        expect++;
    }
    for (int t = 0; t < ntx; t++) {
        txb_t b;
        memcpy(&b, d->b[txpos[t]], sizeof b);
        for (int i = 0; i < b.n; i++) {
            if (b.kind[i] != K_COPY) continue;
            int revoked = 0;
            for (int r = 0; r < nrev; r++)
                if (revaddr[r] == b.addr[i] && revtid[r] > txtid[t]) revoked = 1;
            if (revoked) { skipped++; continue; }
            memcpy(d->b[b.addr[i]], d->b[txpos[t] + 1 + i], BS);
            replayed++;
        }
    }
    mk_jsb(d->b[JSB], pos, expect); /* 재생 끝 → 저널 비움 */
    snprintf(log, loglen, "%d tx committed, %d blk replayed%s%s (stop: %s)", ntx, replayed,
             skipped ? ", revoked-skip=" : "", skipped ? (skipped == 1 ? "1" : "n") : "", why);
    return ntx;
}

/* ---------- mini fsck: append 예제용 (inode 2 + data bitmap + data) ---------- */
static void check(const disk_t *d, char *out, size_t len) {
    inode_t ino[4];
    get_inodes(d, ino);
    const inode_t *f = &ino[2];
    char issues[160] = "";
    int bad = 0, garbage = 0;
    for (int i = 0; i < 4; i++) {
        int p = f->ptr[i];
        if (p < 0) continue;
        if (d->b[DBMAP][p] != '1') {
            snprintf(issues + strlen(issues), sizeof issues - strlen(issues),
                     " [inode->blk%d but bitmap=0]", p);
            bad = 1;
        }
        char c = (char)d->b[DATA0 + p][0];
        if (c != 'a' && c != 'b') garbage = 1;
    }
    for (int k = 0; k < 8; k++) {
        if (d->b[DBMAP][k] != '1') continue;
        int pointed = 0;
        for (int i = 0; i < 4; i++) if (f->ptr[i] == k) pointed = 1;
        if (!pointed) {
            snprintf(issues + strlen(issues), sizeof issues - strlen(issues),
                     " [bitmap blk%d=1 but no inode -> leak]", k);
            bad = 1;
        }
    }
    if (bad) snprintf(out, len, "INCONSISTENT%s", issues);
    else if (garbage) snprintf(out, len, "consistent but GARBAGE (blk5='%c')", d->b[DATA0 + 5][0]);
    else if (f->size == 1) snprintf(out, len, "OK  old state (Da)");
    else snprintf(out, len, "OK  new state (Da+Db)");
}

/* ---------- 초기 디스크 이미지 & 새 블록들 ---------- */
static disk_t base;
static uint8_t Iv2[BS], Bv2[BS], Db[BS];

static void init_base(void) {
    memset(&base, 0, sizeof base);
    for (int i = 0; i < JLEN; i++) fill(base.b[JSTART + i], '?'); /* 예전 저널 찌꺼기 */
    mk_jsb(base.b[JSB], JSTART, 1);
    memset(base.b[DBMAP], 0, BS);
    memcpy(base.b[DBMAP], "00001000", 8);
    inode_t ino[4];
    memset(ino, 0, sizeof ino);
    for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) ino[i].ptr[k] = -1;
    ino[2] = (inode_t){ 1, 1, { 4, -1, -1, -1 }, "file" };
    mk_inode_blk(base.b[INODE], ino);
    for (int i = 0; i < 8; i++) fill(base.b[DATA0 + i], 'x'); /* 쓰레기(이전 내용) */
    fill(base.b[DATA0 + 4], 'a');                          /* Da */

    ino[2] = (inode_t){ 1, 2, { 4, 5, -1, -1 }, "file" };
    mk_inode_blk(Iv2, ino);
    memset(Bv2, 0, BS);
    memcpy(Bv2, "00001100", 8);
    fill(Db, 'b');
}

static void apply_prefix(disk_t *d, const wlist_t *wl, int k) {
    *d = base;
    for (int i = 0; i < k; i++) memcpy(d->b[wl->w[i].addr], wl->w[i].data, BS);
}

static void crash_sweep(const char *title, const wlist_t *wl) {
    printf("\n== %s ==\n", title);
    printf("%-3s %-22s %-64s %s\n", "k", "last write persisted", "recovery", "fsck result");
    for (int k = 0; k <= wl->n; k++) {
        disk_t d;
        char rlog[120], res[200];
        apply_prefix(&d, wl, k);
        recover(&d, 1, rlog, sizeof rlog);
        check(&d, res, sizeof res);
        printf("%-3d %-22s %-64s %s\n", k, k ? wl->w[k - 1].label : "(nothing)", rlog, res);
    }
}

int main(void) {
    init_base();
    uint8_t blk[BS];

    /* ---- B. 저널 없음: {Db, I[v2], B[v2]} 의 8가지 부분집합 ---- */
    printf("== B. no journal: which of the 3 writes reached disk? ==\n");
    for (int m = 0; m < 8; m++) {
        disk_t d = base;
        char res[200];
        if (m & 1) memcpy(d.b[DATA0 + 5], Db, BS);
        if (m & 2) memcpy(d.b[INODE], Iv2, BS);
        if (m & 4) memcpy(d.b[DBMAP], Bv2, BS);
        check(&d, res, sizeof res);
        printf("Db=%d I[v2]=%d B[v2]=%d  -> %s\n", m & 1, (m >> 1) & 1, (m >> 2) & 1, res);
    }

    /* ---- A. data journaling ---- */
    wlist_t A = { .n = 0 };
    item_t items[3] = {
        { INODE, K_COPY, Iv2, "I[v2]" },
        { DBMAP, K_COPY, Bv2, "B[v2]" },
        { DATA0 + 5, K_COPY, Db, "Db" },
    };
    int next = journal_tx(&A, JSTART, 1, items, 3);
    add(&A, INODE, Iv2, "CP: I[v2]");
    add(&A, DBMAP, Bv2, "CP: B[v2]");
    add(&A, DATA0 + 5, Db, "CP: Db");
    mk_jsb(blk, next, 2);
    add(&A, JSB, blk, "free: JSB.start++");
    crash_sweep("A. data journaling, crash after k-th write", &A);

    /* ---- C. 5블록을 한꺼번에 issue → 디스크가 Db(저널본)를 늦게 씀 ---- */
    printf("\n== C. TxB,I,B,Db,TxE issued together; disk persisted TxE before J:Db ==\n");
    {
        disk_t d = base;
        char rlog[120], res[200];
        /* A 리스트의 0..4 중 J:Db(index 3) 만 빠진 채 전원 차단 */
        for (int i = 0; i < 5; i++)
            if (i != 3) memcpy(d.b[A.w[i].addr], A.w[i].data, BS);
        disk_t d2 = d;
        recover(&d, 0, rlog, sizeof rlog);
        check(&d, res, sizeof res);
        printf("no checksum : %-46s -> %s\n", rlog, res);
        recover(&d2, 1, rlog, sizeof rlog);
        check(&d2, res, sizeof res);
        printf("checksum    : %-46s -> %s\n", rlog, res);
    }

    /* ---- D. ordered (metadata) journaling ---- */
    wlist_t D = { .n = 0 };
    add(&D, DATA0 + 5, Db, "Db -> final location");
    next = journal_tx(&D, JSTART, 1, items, 2); /* I[v2], B[v2] 만 저널 */
    add(&D, INODE, Iv2, "CP: I[v2]");
    add(&D, DBMAP, Bv2, "CP: B[v2]");
    mk_jsb(blk, next, 2);
    add(&D, JSB, blk, "free: JSB.start++");
    crash_sweep("D1. ordered journaling (data first), crash after k-th write", &D);

    wlist_t Dbug = { .n = 0 };
    next = journal_tx(&Dbug, JSTART, 1, items, 2);
    add(&Dbug, DATA0 + 5, Db, "Db (AFTER commit!)");
    add(&Dbug, INODE, Iv2, "CP: I[v2]");
    add(&Dbug, DBMAP, Bv2, "CP: B[v2]");
    crash_sweep("D2. BUGGY metadata journaling (data after commit)", &Dbug);

    /* ---- E. 블록 재사용 + revoke ---- */
    printf("\n== E. block reuse: dir foo's block (data6) freed, reused by file foobar ==\n");
    for (int use_revoke = 0; use_revoke <= 1; use_revoke++) {
        wlist_t E = { .n = 0 };
        inode_t ino[4];
        uint8_t ib1[BS], ib2[BS], ib3[BS], dfoo[BS], dfoobar[BS], rv[BS];
        memset(ino, 0, sizeof ino);
        for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) ino[i].ptr[k] = -1;
        ino[1] = (inode_t){ 1, 1, { 6, -1, -1, -1 }, "foo/" };
        mk_inode_blk(ib1, ino);
        fill(dfoo, 'D'); /* 디렉터리 내용 = 메타데이터 → 저널에 들어감 */
        item_t t1[2] = { { INODE, K_COPY, ib1, "I[foo]" }, { DATA0 + 6, K_COPY, dfoo, "D[foo]" } };
        int p = journal_tx(&E, JSTART, 1, t1, 2);
        add(&E, INODE, ib1, "CP: I[foo]");
        add(&E, DATA0 + 6, dfoo, "CP: D[foo]");
        /* rm -r foo : 블록 6 해제 (+ revoke 레코드) */
        ino[1] = (inode_t){ 0, 0, { -1, -1, -1, -1 }, "" };
        mk_inode_blk(ib2, ino);
        memset(rv, 0, BS);
        int32_t a = DATA0 + 6;
        memcpy(rv, &a, sizeof a);
        item_t t2[2] = { { INODE, K_COPY, ib2, "I[-foo]" }, { 0, K_REVOKE, rv, "REVOKE blk" } };
        p = journal_tx(&E, p, 2, t2, use_revoke ? 2 : 1);
        add(&E, INODE, ib2, "CP: I[-foo]");
        /* foobar 생성: 블록 6 재사용, 사용자 데이터는 저널 안 함(ordered) */
        fill(dfoobar, 'F');
        add(&E, DATA0 + 6, dfoobar, "foobar data");
        ino[3] = (inode_t){ 1, 1, { 6, -1, -1, -1 }, "foobar" };
        mk_inode_blk(ib3, ino);
        item_t t3[1] = { { INODE, K_COPY, ib3, "I[foobar]" } };
        journal_tx(&E, p, 3, t3, 1);
        add(&E, INODE, ib3, "CP: I[foobar]");
        /* JSB 는 아직 tx1 을 가리킴 (체크포인트 후 free 전에) → 크래시 */
        disk_t d;
        char rlog[120];
        apply_prefix(&d, &E, E.n);
        printf("before crash : data6='%c' (foobar)\n", d.b[DATA0 + 6][0]);
        recover(&d, 1, rlog, sizeof rlog);
        inode_t after[4];
        get_inodes(&d, after);
        printf("revoke=%s : %s\n              -> data6='%c' owner=%s  %s\n", use_revoke ? "ON " : "OFF", rlog,
               d.b[DATA0 + 6][0], after[3].name,
               d.b[DATA0 + 6][0] == 'F' ? "OK" : "CORRUPTED: old dir bytes replayed over foobar!");
    }
    return 0;
}
