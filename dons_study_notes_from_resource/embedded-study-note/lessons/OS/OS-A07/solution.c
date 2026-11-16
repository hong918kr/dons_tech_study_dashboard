/* OS-A07 크래시 일관성: write-ahead 저널링 + 크래시 주입
 *
 * OS-M14 VSFS 축약판(같은 배치·같은 fsck 용어, 크기만 줄임) 위에 OSTEP 42장의 저널을 얹는다.
 * 디스크 = 메모리 블록 배열. 파일 시스템은 bread/bwrite/bflush 로만 디스크에 닿고, 셋 다 횟수를 센다.
 *
 *   배치  [0 sb][1 js = 저널 superblock][2..65 저널 64 블록][66 ibm][67 dbm][68..71 inode 64 개][72.. 데이터 184]
 *   블록  1 KB, inode 64 B (direct 6 + 1단 indirect 256), dirent 32 B, 루트 inode 0
 *   저널  TxB{tid, n, 블록 목록} → 로그 블록 n 개 → (barrier) → TxE{tid, n, crc32} → (flush) → 커밋됨
 *         → checkpoint(제자리 쓰기) → flush → js{tail_tid, tail_off} 갱신(저널 비움) → flush
 *   모드  M_NONE(저널 없음, M14 처럼 바로 씀) · M_DATA(데이터도 로그) · M_ORDERED(데이터 먼저 제자리) ·
 *         M_WRITEBACK(데이터를 커밋 뒤에 씀 = 비순서 메타데이터 저널링, 버그를 보이려고)
 *   장치  crash_at = N → 쓰기 #N-1 과 #N 사이에 전원 차단: #N 부터의 쓰기도, 그 사이의 flush 도 완료되지 않는다.
 *         wc_on = 1 이면 휘발성 쓰기 캐시(최대 4 개)가
 *         있어 flush 전의 쓰기는 "아무 부분집합"만 살아남을 수 있다(crash_mask 로 고름).
 *
 *   Part A  저널: commit · checkpoint · recover(replay)
 *   Part B  모드별 데이터 경로 + 모든 N 에 대한 크래시 전수 스윕(fsck · 원자성 판정)
 *   Part C  쓰기 캐시 재정렬: barrier 를 빼면? TxE checksum 이면? (crc32 · 캐시 전원 차단 모델)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 연습·데모가 엔진만 쓸 때: #define A07_NO_MAIN 후 #include "solution.c"
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIVEN __attribute__((unused))
#ifndef A07_WC_MAX
#define A07_WC_MAX 4                            /* 쓰기 캐시 창 (연습 1 은 6 으로 키운다) */
#endif

/* ============================ 온디스크 형식 (주어짐) ============================ */
enum { BSIZE = 1024, NBLOCKS = 256, JSTART = 2, JBLKS = 64, IBM = JSTART + JBLKS, DBM = IBM + 1,
       ISTART = DBM + 1, NIBLK = 4, INODE_SIZE = 64, IPB = BSIZE / INODE_SIZE, NINODES = NIBLK * IPB,
       DSTART = ISTART + NIBLK, NDATA = NBLOCKS - DSTART,
       NDIRECT = 6, NINDIRECT = BSIZE / 4, MAXFILE = NDIRECT + NINDIRECT,
       DIRENT_SIZE = 32, DPB = BSIZE / DIRENT_SIZE, NAME_MAX_ = 27, ROOT_INO = 0,
       TX_MAX = 40, CK_MAX = JBLKS, WC_MAX = A07_WC_MAX };
enum { T_FREE = 0, T_FILE = 1, T_DIR = 2 };
#define FS_MAGIC  0x53465356u   /* "VSFS" */
#define JSB_MAGIC 0x4a53424bu
#define TXB_MAGIC 0x54784221u
#define TXE_MAGIC 0x54784521u

typedef struct { uint32_t magic, nblocks, jstart, jblks, ibm, dbm, istart, dstart; } superblock_t;
typedef struct {
    uint16_t type, nlink;
    uint32_t size;
    uint32_t direct[NDIRECT];
    uint32_t indirect;
    uint8_t  pad[INODE_SIZE - 36];
} dinode_t;
_Static_assert(sizeof(dinode_t) == INODE_SIZE, "inode = 64 B");
typedef struct { uint32_t inum; char name[NAME_MAX_ + 1]; } dirent_t;
_Static_assert(sizeof(dirent_t) == DIRENT_SIZE, "dirent = 32 B");

typedef struct { uint32_t magic, tail_tid, tail_off, csum; } jsb_t;          /* 저널 superblock */
typedef struct { uint32_t magic, tid, n, blk[TX_MAX]; } txb_t;              /* 트랜잭션 시작(디스크립터) */
typedef struct { uint32_t magic, tid, n, crc; } txe_t;                      /* 트랜잭션 끝(커밋 블록) */
_Static_assert(sizeof(txe_t) <= 512, "TxE 는 한 섹터 안");

/* ============================ 블록 장치 (주어짐, wc_crash 만 TODO) ============================ */
static struct {
    uint8_t media[NBLOCKS][BSIZE];              /* 전원이 나가도 남는 것 */
    long nread, nwrite, nflush;                 /* 카운터 (io_reset 으로 0) */
    long crash_at;                              /* ≥ 0: 이 번호(0 부터)의 쓰기부터 전원 차단 */
    int crashed, pend_at_crash;
    unsigned crash_mask;                        /* 전원 차단 순간 캐시 i 번째 쓰기가 살아남으면 bit i = 1 */
    int wc_on, wc_n;                            /* 휘발성 쓰기 캐시 (FIFO, WC_MAX 개) */
    uint32_t wc_blk[WC_MAX];
    uint8_t wc_d[WC_MAX][BSIZE];
    long nw_at_flush;                           /* 마지막 flush 때의 쓰기 수 */
    char tbuf[2048]; size_t tlen;               /* "W J0 W J1 F ..." */
} D = { .crash_at = -1 };

static void blk_label(uint32_t b, char *out, size_t cap) {
    if (b == 0) snprintf(out, cap, "sb");
    else if (b == 1) snprintf(out, cap, "js");
    else if (b < IBM) snprintf(out, cap, "J%u", b - JSTART);
    else if (b == IBM) snprintf(out, cap, "ibm");
    else if (b == DBM) snprintf(out, cap, "dbm");
    else if (b < DSTART) snprintf(out, cap, "I%u", b - ISTART);
    else snprintf(out, cap, "D%u", b - DSTART);
}
static void trace_add(const char *s) {
    if (D.tlen + 16 >= sizeof D.tbuf) return;
    D.tlen += (size_t)snprintf(D.tbuf + D.tlen, sizeof D.tbuf - D.tlen, "%s%s", D.tlen ? " " : "", s);
}
static void io_reset(void) { D.nread = D.nwrite = D.nflush = D.nw_at_flush = 0; D.tlen = 0; D.tbuf[0] = 0; }
static void dev_init(void) {                    /* 새 디스크: 전부 0 */
    memset(D.media, 0, sizeof D.media);
    D.crash_at = -1; D.crashed = 0; D.crash_mask = 0; D.pend_at_crash = 0; D.wc_n = 0;
    io_reset();
}
static void persist(int i) { memcpy(D.media[D.wc_blk[i]], D.wc_d[i], BSIZE); }

/* TODO C2: 전원 차단 순간의 쓰기 캐시 처리. 캐시 i 번째(오래된 것부터) 쓰기는 mask 의 bit i 가 1 일 때만
 * 매체에 닿는다(오래된 것부터 차례로 — 같은 블록이 두 번 있으면 나중 것이 이긴다). 나머지는 사라진다. 캐시를 비운다. */
static void wc_crash(unsigned mask) {
    for (int i = 0; i < D.wc_n; i++)
        if ((mask >> i) & 1u) persist(i);
    D.wc_n = 0;
}
static void power_cut(void) {
    if (D.crashed) return;
    D.crashed = 1;
    D.pend_at_crash = D.wc_n;
    wc_crash(D.crash_mask);
}
static void dev_reboot(void) { D.crashed = 0; D.crash_at = -1; D.crash_mask = 0; D.wc_n = 0; }

static int bread(uint32_t b, void *buf) {
    if (b >= NBLOCKS) return -EIO;
    D.nread++;
    for (int i = D.wc_n - 1; i >= 0; i--)       /* 캐시에 있으면 가장 새 것 */
        if (D.wc_blk[i] == b) { memcpy(buf, D.wc_d[i], BSIZE); return 0; }
    memcpy(buf, D.media[b], BSIZE);
    return 0;
}
static int bwrite(uint32_t b, const void *buf) {
    if (b >= NBLOCKS) return -EIO;
    if (D.crashed) return 0;                                    /* 전원이 나간 뒤: 조용히 사라짐 */
    if (D.crash_at >= 0 && D.nwrite >= D.crash_at) { power_cut(); return 0; }
    D.nwrite++;
    char l[16]; blk_label(b, l, sizeof l);
    char t[24]; snprintf(t, sizeof t, "W %s", l); trace_add(t);
    if (!D.wc_on) { memcpy(D.media[b], buf, BSIZE); return 0; }
    if (D.wc_n == WC_MAX) {                                     /* 꽉 차면 가장 오래된 것을 매체로 */
        persist(0);
        memmove(D.wc_blk, D.wc_blk + 1, sizeof D.wc_blk[0] * (WC_MAX - 1));
        memmove(D.wc_d, D.wc_d + 1, sizeof D.wc_d[0] * (WC_MAX - 1));
        D.wc_n--;
    }
    D.wc_blk[D.wc_n] = b; memcpy(D.wc_d[D.wc_n], buf, BSIZE); D.wc_n++;
    return 0;
}
static void bflush(void) {                      /* FLUSH / barrier: 캐시를 순서대로 매체에 */
    if (D.crashed) return;
    if (D.crash_at >= 0 && D.nwrite >= D.crash_at) { power_cut(); return; }   /* flush 도중 전원 차단 */
    D.nflush++; D.nw_at_flush = D.nwrite; trace_add("F");
    for (int i = 0; i < D.wc_n; i++) persist(i);
    D.wc_n = 0;
}

/* ============================ crc32 (TODO C1) ============================ */
/* TODO C1: zlib 과 같은 crc32 (반사형, 다항식 0xEDB88320, 초기값/최종 XOR 0xFFFFFFFF).
 * 이어서 계산할 수 있게: crc32_update(crc32_update(0, a, n), b, m) == crc32(a‖b). "123456789" → 0xCBF43926 */
static uint32_t crc32_update(uint32_t crc, const void *p, size_t n) {
    const uint8_t *s = p;
    uint32_t c = ~crc;
    for (size_t i = 0; i < n; i++) {
        c ^= s[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* ============================ 저널 상태 (주어짐) ============================ */
enum { M_NONE, M_DATA, M_ORDERED, M_WRITEBACK };
static const char *const MODE_NAME[] = { "none", "data", "ordered", "writeback" };
static struct {
    int mode, barrier, csum, lazy, dflush;      /* 마운트 옵션 (재부팅해도 유지). dflush: 연습 1 */
    uint32_t next_tid, head;                    /* 다음 tid, 다음 TxB 자리(저널 안 오프셋) */
    int depth;                                  /* op 중첩 (바깥 op 이 끝날 때 커밋) */
    int ntx; uint32_t tx_blk[TX_MAX]; uint8_t tx_d[TX_MAX][BSIZE];   /* 열린 트랜잭션 (같은 블록은 흡수) */
    int nck; uint32_t ck_blk[CK_MAX]; uint8_t ck_d[CK_MAX][BSIZE];   /* 커밋됐고 아직 제자리에 안 쓴 블록 */
    int nwb; uint32_t wb_blk[TX_MAX]; uint8_t wb_d[TX_MAX][BSIZE];   /* writeback: 커밋 뒤로 미룬 데이터 */
    long user_blocks, commits, replayed, skipped, crc_bad, wraps;
    int (*veto)(uint32_t b);                    /* 연습용: 이 블록은 아직 재할당하지 마라 (NULL = 없음) */
} J = { .mode = M_ORDERED, .barrier = 1, .csum = 1 };

static int find(const uint32_t *v, int n, uint32_t b) { for (int i = 0; i < n; i++) if (v[i] == b) return i; return -1; }
static void drop(uint32_t *v, uint8_t (*d)[BSIZE], int *n, int i) {
    memmove(v + i, v + i + 1, sizeof v[0] * (size_t)(*n - i - 1));
    memmove(d + i, d + i + 1, sizeof d[0] * (size_t)(*n - i - 1));
    (*n)--;
}
/* 블록 읽기: 열린 트랜잭션 → 미룬 데이터 → 체크포인트 대기 → 디스크 (커널의 버퍼 캐시 역할) */
static int rd(uint32_t b, void *buf) {
    int i;
    if ((i = find(J.tx_blk, J.ntx, b)) >= 0) { memcpy(buf, J.tx_d[i], BSIZE); return 0; }
    if ((i = find(J.wb_blk, J.nwb, b)) >= 0) { memcpy(buf, J.wb_d[i], BSIZE); return 0; }
    if ((i = find(J.ck_blk, J.nck, b)) >= 0) { memcpy(buf, J.ck_d[i], BSIZE); return 0; }
    return bread(b, buf);
}
static int tx_add(uint32_t b, const void *buf) {
    int i = find(J.tx_blk, J.ntx, b);
    if (i < 0) {
        if (J.ntx == TX_MAX) return -EFBIG;
        i = J.ntx++; J.tx_blk[i] = b;
    }
    memcpy(J.tx_d[i], buf, BSIZE);
    return 0;
}
GIVEN static void ck_put(uint32_t b, const void *buf) {            /* 커밋된 새 버전 (같은 블록이면 교체) */
    int i = find(J.ck_blk, J.nck, b);
    if (i < 0) { if (J.nck == CK_MAX) return; i = J.nck++; J.ck_blk[i] = b; }
    memcpy(J.ck_d[i], buf, BSIZE);
}
/* 메타데이터였던 블록이 이제 저널 안 하는 데이터가 됨 → 메모리의 옛 메타데이터 버전을 잊는다 (jbd2 forget 비슷) */
GIVEN static void forget(uint32_t b) {
    int i;
    if ((i = find(J.tx_blk, J.ntx, b)) >= 0) drop(J.tx_blk, J.tx_d, &J.ntx, i);
    if ((i = find(J.ck_blk, J.nck, b)) >= 0) drop(J.ck_blk, J.ck_d, &J.nck, i);
}
static int mput(uint32_t b, const void *buf) {               /* 메타데이터 쓰기 */
    return J.mode == M_NONE ? bwrite(b, buf) : tx_add(b, buf);
}
static void js_write(uint32_t tail_tid, uint32_t tail_off) {
    uint8_t blk[BSIZE] = { 0 };
    jsb_t s = { JSB_MAGIC, tail_tid, tail_off, (uint32_t)J.csum };
    memcpy(blk, &s, sizeof s);
    bwrite(1, blk);
}

/* ======================= Part A — 저널 ======================= */

/* TODO A1: 열린 트랜잭션(J.ntx 개 블록)을 저널 J.head 자리에 커밋한다.
 *  ① TxB 블록(txb_t: TXB_MAGIC, tid = J.next_tid, n, 블록 번호 목록; 나머지 0) → JSTART+head
 *  ② 로그 블록 n 개 → JSTART+head+1 ..   ③ J.barrier 면 bflush()   ④ TxE(txe_t: TXE_MAGIC, tid, n,
 *  crc = J.csum ? crc32(TxB 블록 전체 ‖ 로그 블록들) : 0) → JSTART+head+1+n   ⑤ bflush() → 이제 커밋됨.
 *  ⑥ 블록들을 체크포인트 목록으로(ck_put), head += n+2, next_tid++, ntx = 0, commits++.
 *  자리가 모자라면(wrap)은 호출자(op_exit)가 이미 처리했다. ntx == 0 이면 아무것도 안 한다. */
static int jnl_commit(void) {
    if (J.ntx == 0) return 0;
    uint8_t blk[BSIZE] = { 0 };
    txb_t h = { .magic = TXB_MAGIC, .tid = J.next_tid, .n = (uint32_t)J.ntx };
    memcpy(h.blk, J.tx_blk, sizeof h.blk[0] * (size_t)J.ntx);
    memcpy(blk, &h, sizeof h);
    uint32_t crc = crc32_update(0, blk, BSIZE);
    bwrite(JSTART + J.head, blk);                                       /* ① TxB */
    for (int i = 0; i < J.ntx; i++) {                                   /* ② 로그 */
        bwrite(JSTART + J.head + 1 + (uint32_t)i, J.tx_d[i]);
        crc = crc32_update(crc, J.tx_d[i], BSIZE);
    }
    if (J.barrier) bflush();                                            /* ③ */
    memset(blk, 0, BSIZE);
    txe_t e = { TXE_MAGIC, J.next_tid, (uint32_t)J.ntx, J.csum ? crc : 0 };
    memcpy(blk, &e, sizeof e);
    bwrite(JSTART + J.head + 1 + (uint32_t)J.ntx, blk);                 /* ④ TxE */
    bflush();                                                           /* ⑤ 커밋 지점 */
    for (int i = 0; i < J.ntx; i++) ck_put(J.tx_blk[i], J.tx_d[i]);     /* ⑥ */
    J.head += (uint32_t)J.ntx + 2;
    J.next_tid++; J.ntx = 0; J.commits++;
    return 0;
}

/* TODO A2: 체크포인트. 커밋된 블록(J.ck_*)을 제자리에 쓰고 → bflush → 저널 superblock 을
 * (tail_tid = next_tid, tail_off = head) 로 써서 저널을 비우고 → bflush. 목록을 비운다. 목록이 비었으면 아무것도 안 함. */
static void jnl_checkpoint(void) {
    if (J.nck == 0) return;
    for (int i = 0; i < J.nck; i++) bwrite(J.ck_blk[i], J.ck_d[i]);
    bflush();
    js_write(J.next_tid, J.head);
    bflush();
    J.nck = 0;
}

/* TODO A3: 마운트 때 복구(redo replay). js 를 읽고 (tail_off, tail_tid) 부터:
 *  - 자리 off 에 TxB(magic, tid == 기대값, 1 ≤ n ≤ TX_MAX, off+n+2 ≤ JBLKS)가 없으면 off ≠ 0 일 때 0 에서 다시 찾는다(wrap).
 *  - 로그 블록 n 개와 TxE 를 읽는다. TxE 의 magic/tid/n 이 맞고, js.csum 이면 crc 도 맞아야 "커밋된 트랜잭션".
 *    블록 목록의 번호가 저널·superblock 을 가리키면(< IBM) 손상 → 멈춤.
 *  - 커밋됐으면 블록들을 제자리에 쓰고(J.replayed++) 다음 tid·다음 자리로. 아니면 J.skipped++ 하고 멈춘다(찢어진 꼬리).
 *  끝나면: 하나라도 replay 했거나 찢어진 꼬리가 있었으면 bflush → js = (다음 tid, 0) → bflush, head = 0.
 *  (찢어진 꼬리가 있었다면 그 tid 는 건너뛴다: next_tid = 그 tid + 1.)  아무것도 없었으면 쓰지 않고
 *  head = tail_off, next_tid = tail_tid 로 이어 쓴다. 반환: replay 한 트랜잭션 수, js 가 없으면 -EINVAL. */
static int jnl_recover(void) {
    uint8_t blk[BSIZE];
    jsb_t s;
    if (bread(1, blk) < 0) return -EIO;
    memcpy(&s, blk, sizeof s);
    if (s.magic != JSB_MAGIC || s.tail_off >= JBLKS) return -EINVAL;
    static uint8_t lb[TX_MAX][BSIZE];
    uint32_t tid = s.tail_tid, off = s.tail_off;
    int nrep = 0, torn = 0;
    for (;;) {
        txb_t h; int found = 0;
        for (int pass = 0; pass < 2 && !found; pass++) {
            uint32_t o = pass ? 0 : off;
            if (pass && off == 0) break;
            if (o + 2 > JBLKS) continue;
            bread(JSTART + o, blk);
            memcpy(&h, blk, sizeof h);
            if (h.magic == TXB_MAGIC && h.tid == tid && h.n >= 1 && h.n <= TX_MAX && o + h.n + 2 <= JBLKS) {
                found = 1; off = o;
            }
        }
        if (!found) break;
        uint32_t crc = crc32_update(0, blk, BSIZE);
        int ok = 1;
        for (uint32_t i = 0; i < h.n; i++) {
            bread(JSTART + off + 1 + i, lb[i]);
            crc = crc32_update(crc, lb[i], BSIZE);
            if (h.blk[i] < IBM || h.blk[i] >= NBLOCKS) ok = 0;
        }
        txe_t e;
        bread(JSTART + off + 1 + h.n, blk);
        memcpy(&e, blk, sizeof e);
        if (e.magic != TXE_MAGIC || e.tid != tid || e.n != h.n) ok = 0;
        else if (s.csum && e.crc != crc) { ok = 0; J.crc_bad++; }
        if (!ok) { J.skipped++; torn = 1; break; }
        for (uint32_t i = 0; i < h.n; i++) bwrite(h.blk[i], lb[i]);
        J.replayed++; nrep++;
        off += h.n + 2; tid++;
    }
    if (nrep > 0 || torn) {
        bflush();
        J.next_tid = tid + (uint32_t)torn;
        J.head = 0;
        js_write(J.next_tid, 0);
        bflush();
    } else {
        J.next_tid = s.tail_tid;
        J.head = s.tail_off;
    }
    return nrep;
}

/* ======================= Part B — 데이터 경로 (모드별) ======================= */

/* TODO B1a: 일반 파일 데이터 블록 쓰기. 모드별로
 *   M_NONE      → 바로 제자리 (bwrite)
 *   M_DATA      → 트랜잭션에 (tx_add) — 데이터도 저널에 두 번 쓴다
 *   M_ORDERED   → forget(b) 후 바로 제자리 (bwrite). 커밋(op 끝)보다 "먼저" 디스크로 간다
 *   M_WRITEBACK → forget(b) 후 미룬 목록(J.wb_*, 같은 블록은 교체)에. 커밋 "뒤에" wb_writeout 이 쓴다
 * forget: 이 블록이 메모리에 메타데이터 버전(예: 해제된 indirect)으로 남아 있으면 그 버전이 데이터를 덮지 않게. */
static int dput(uint32_t b, const void *buf) {
    switch (J.mode) {
    case M_DATA: return tx_add(b, buf);
    case M_ORDERED: forget(b); return bwrite(b, buf);
    case M_WRITEBACK: {
        forget(b);
        int i = find(J.wb_blk, J.nwb, b);
        if (i < 0) { if (J.nwb == TX_MAX) return -EFBIG; i = J.nwb++; J.wb_blk[i] = b; }
        memcpy(J.wb_d[i], buf, BSIZE);
        return 0;
    }
    default: return bwrite(b, buf);
    }
}
/* TODO B1b: writeback 의 미룬 데이터를 제자리에 쓰고 목록을 비운다 (커밋 뒤, 체크포인트 전에 불림) */
static void wb_writeout(void) {
    for (int i = 0; i < J.nwb; i++) bwrite(J.wb_blk[i], J.wb_d[i]);
    J.nwb = 0;
}

/* ---- op 경계 (주어짐): 바깥 op 가 끝날 때 한 트랜잭션으로 커밋 ---- */
static void op_enter(void) { if (J.depth++ == 0) J.ntx = 0; }
static int op_exit(int rc) {
    if (--J.depth > 0 || J.mode == M_NONE) return rc;
    if (J.ntx > 0) {
        if (J.head + (uint32_t)J.ntx + 2 > JBLKS) {     /* 저널 끝: 비우고 처음으로 */
            jnl_checkpoint();
            J.head = 0; J.wraps++;
        }
        if (J.dflush && D.nwrite != D.nw_at_flush) bflush();    /* 연습 1: ordered 데이터를 커밋 전에 내림 */
        jnl_commit();
    }
    wb_writeout();
    if (!J.lazy) jnl_checkpoint();                      /* lazy: 저널이 찰 때(wrap)나 sync 때만 */
    if (D.nwrite != D.nw_at_flush) bflush();            /* op 이 끝나면 내구성 (fsync 처럼) */
    return rc;
}
static void fs_sync(void) { if (J.mode != M_NONE) jnl_checkpoint(); bflush(); }   /* umount / sync */

/* ============================ VSFS 축약판 (주어짐) ============================ */
static int iget(uint32_t inum, dinode_t *out) {
    if (inum >= NINODES) return -EINVAL;
    uint8_t b[BSIZE];
    int rc = rd(ISTART + inum / IPB, b);
    if (rc < 0) return rc;
    memcpy(out, b + (inum % IPB) * INODE_SIZE, sizeof *out);
    return 0;
}
static int iput(uint32_t inum, const dinode_t *in) {
    if (inum >= NINODES) return -EINVAL;
    uint8_t b[BSIZE];
    int rc = rd(ISTART + inum / IPB, b);
    if (rc < 0) return rc;
    memcpy(b + (inum % IPB) * INODE_SIZE, in, sizeof *in);
    return mput(ISTART + inum / IPB, b);
}
static int bm_alloc(uint32_t bmblk, uint32_t n, uint32_t base) {      /* first-fit, 쓰기는 메타데이터 */
    uint8_t p[BSIZE];
    int rc = rd(bmblk, p);
    if (rc < 0) return rc;
    for (uint32_t i = 0; i < n; i++)
        if (!((p[i / 8] >> (i % 8)) & 1) && !(J.veto && bmblk == DBM && J.veto(base + i))) {
            p[i / 8] |= (uint8_t)(1u << (i % 8));
            rc = mput(bmblk, p);
            return rc < 0 ? rc : (int)i;
        }
    return -ENOSPC;
}
static int bm_clear(uint32_t bmblk, uint32_t i) {
    uint8_t p[BSIZE];
    int rc = rd(bmblk, p);
    if (rc < 0) return rc;
    p[i / 8] &= (uint8_t)~(1u << (i % 8));
    return mput(bmblk, p);
}
static int ialloc(void) { return bm_alloc(IBM, NINODES, 0); }
static long balloc(void) { int d = bm_alloc(DBM, NDATA, DSTART); return d < 0 ? d : (long)DSTART + d; }
static int blk_ok(uint32_t b) { return b >= DSTART && b < NBLOCKS; }

/* fbn → 디스크 블록. alloc 이면 할당(indirect 블록과 그 칸은 메타데이터). *fresh = 새로 할당됨 */
static long bmap(dinode_t *ino, uint32_t fbn, int alloc, int *fresh) {
    if (fresh) *fresh = 0;
    if (fbn < NDIRECT) {
        if (ino->direct[fbn] == 0 && alloc) {
            long b = balloc();
            if (b < 0) return b;
            ino->direct[fbn] = (uint32_t)b;
            if (fresh) *fresh = 1;
        }
        return ino->direct[fbn];
    }
    fbn -= NDIRECT;
    if (fbn >= NINDIRECT) return -EFBIG;
    uint8_t p[BSIZE];
    if (ino->indirect == 0) {
        if (!alloc) return 0;
        long b = balloc();
        if (b < 0) return b;
        ino->indirect = (uint32_t)b;
        memset(p, 0, BSIZE);
    } else if (!blk_ok(ino->indirect) || rd(ino->indirect, p) < 0) return -EIO;
    uint32_t ent; memcpy(&ent, p + 4 * fbn, 4);
    if (ent != 0 || !alloc) return ent;
    long nb = balloc();
    if (nb < 0) return nb;
    ent = (uint32_t)nb; memcpy(p + 4 * fbn, &ent, 4);
    int rc = mput(ino->indirect, p);
    if (rc < 0) return rc;
    if (fresh) *fresh = 1;
    return nb;
}

static int dir_lookup(const dinode_t *dir, const char *name, uint32_t *slot_out) {
    dinode_t d = *dir;
    uint8_t p[BSIZE];
    uint32_t nent = d.size / DIRENT_SIZE;
    for (uint32_t s = 0; s < nent; s++) {
        if (s % DPB == 0) {
            long b = bmap(&d, s / DPB, 0, NULL);
            if (b <= 0 || !blk_ok((uint32_t)b) || rd((uint32_t)b, p) < 0) return -EIO;
        }
        dirent_t e; memcpy(&e, p + (s % DPB) * DIRENT_SIZE, sizeof e);
        if (e.name[0] && strncmp(e.name, name, NAME_MAX_ + 1) == 0) {
            if (slot_out) *slot_out = s;
            return (int)e.inum;
        }
    }
    return -ENOENT;
}
/* 칸 slot 을 (inum, name) 으로 (name == NULL 이면 칸 비우기) */
static int dir_set(uint32_t dinum, uint32_t slot, uint32_t inum, const char *name) {
    dinode_t d;
    int rc = iget(dinum, &d);
    if (rc < 0) return rc;
    long b = bmap(&d, slot / DPB, 0, NULL);
    uint8_t p[BSIZE];
    if (b <= 0 || rd((uint32_t)b, p) < 0) return -EIO;
    dirent_t e = { .inum = inum };
    if (name) strncpy(e.name, name, NAME_MAX_); else e.inum = 0;
    memcpy(p + (slot % DPB) * DIRENT_SIZE, &e, sizeof e);
    return mput((uint32_t)b, p);
}
static int dir_add(uint32_t dinum, const char *name, uint32_t inum, int nlink_delta) {
    dinode_t dir;
    int rc = iget(dinum, &dir);
    if (rc < 0) return rc;
    uint32_t nent = dir.size / DIRENT_SIZE, slot = nent;
    uint8_t p[BSIZE];
    for (uint32_t s = 0; s < nent; s++) {
        if (s % DPB == 0) {
            long b = bmap(&dir, s / DPB, 0, NULL);
            if (b <= 0 || rd((uint32_t)b, p) < 0) return -EIO;
        }
        if (p[(s % DPB) * DIRENT_SIZE + 4] == 0) { slot = s; break; }
    }
    int fresh = 0;
    long b = bmap(&dir, slot / DPB, slot == nent, &fresh);           /* 디렉터리 블록 = 메타데이터 */
    if (b <= 0) return b < 0 ? (int)b : -EIO;
    if (fresh) memset(p, 0, BSIZE); else if (rd((uint32_t)b, p) < 0) return -EIO;
    dirent_t e = { .inum = inum };
    strncpy(e.name, name, NAME_MAX_);
    memcpy(p + (slot % DPB) * DIRENT_SIZE, &e, sizeof e);
    if ((rc = mput((uint32_t)b, p)) < 0) return rc;
    if (slot == nent) dir.size += DIRENT_SIZE;
    dir.nlink = (uint16_t)(dir.nlink + nlink_delta);
    if (slot == nent || nlink_delta) return iput(dinum, &dir);
    return 0;
}
static int path_next(const char **pp, char name[NAME_MAX_ + 1]) {
    const char *p = *pp;
    while (*p == '/') p++;
    if (*p == 0) { *pp = p; return 0; }
    const char *s = p;
    while (*p && *p != '/') p++;
    size_t n = (size_t)(p - s);
    if (n > NAME_MAX_) return -ENAMETOOLONG;
    memcpy(name, s, n); name[n] = 0; *pp = p;
    return 1;
}
static int split_last(const char *path, char *dir, size_t cap, char name[NAME_MAX_ + 1]) {
    if (!path || path[0] != '/') return -EINVAL;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    size_t i = len;
    while (i > 0 && path[i - 1] != '/') i--;
    size_t nl = len - i;
    if (nl == 0) return -EINVAL;
    if (nl > NAME_MAX_ || i + 1 > cap) return -ENAMETOOLONG;
    memcpy(name, path + i, nl); name[nl] = 0;
    memcpy(dir, path, i); dir[i] = 0;
    return 0;
}
static int namei_(const char *path, dinode_t *out) {
    if (!path || path[0] != '/') return -EINVAL;
    uint32_t cur = ROOT_INO;
    dinode_t ino;
    int rc = iget(cur, &ino);
    if (rc < 0) return rc;
    char name[NAME_MAX_ + 1];
    const char *p = path;
    while ((rc = path_next(&p, name)) > 0) {
        if (ino.type != T_DIR) return -ENOTDIR;
        if ((rc = dir_lookup(&ino, name, NULL)) < 0) return rc;
        cur = (uint32_t)rc;
        if ((rc = iget(cur, &ino)) < 0) return rc;
    }
    if (rc < 0) return rc;
    if (out) *out = ino;
    return (int)cur;
}
int fs_namei(const char *path, dinode_t *out) { return namei_(path, out); }

int fs_mkfs(void) {                             /* 저널 모드와 무관하게 바로 쓴다 */
    uint8_t blk[BSIZE];
    superblock_t s = { FS_MAGIC, NBLOCKS, JSTART, JBLKS, IBM, DBM, ISTART, DSTART };
    memset(blk, 0, BSIZE); memcpy(blk, &s, sizeof s); bwrite(0, blk);
    js_write(1, 0);
    memset(blk, 0, BSIZE); blk[0] = 1; bwrite(IBM, blk); bwrite(DBM, blk);
    for (uint32_t k = 0; k < NIBLK; k++) {
        memset(blk, 0, BSIZE);
        if (k == 0) {
            dinode_t root = { .type = T_DIR, .nlink = 2, .size = 2 * DIRENT_SIZE };
            root.direct[0] = DSTART;
            memcpy(blk, &root, sizeof root);
        }
        bwrite(ISTART + k, blk);
    }
    memset(blk, 0, BSIZE);
    dirent_t dot = { ROOT_INO, "." }, dotdot = { ROOT_INO, ".." };
    memcpy(blk, &dot, sizeof dot); memcpy(blk + DIRENT_SIZE, &dotdot, sizeof dotdot);
    bwrite(DSTART, blk);
    bflush();
    return 0;
}
/* 마운트 = 메모리 상태를 버리고(재부팅) superblock 확인 → 저널 복구 */
int fs_mount(void) {
    J.depth = 0; J.ntx = J.nck = J.nwb = 0; J.next_tid = 1; J.head = 0;
    uint8_t blk[BSIZE];
    superblock_t s;
    if (bread(0, blk) < 0) return -EIO;
    memcpy(&s, blk, sizeof s);
    if (s.magic != FS_MAGIC || s.nblocks != NBLOCKS) return -EINVAL;
    if (J.mode == M_NONE) return 0;
    int r = jnl_recover();
    return r < 0 ? r : 0;
}

static int free_blocks(const dinode_t *dead) {
    uint32_t list[MAXFILE + 1]; int nl = 0;
    for (int k = 0; k < NDIRECT; k++) if (blk_ok(dead->direct[k])) list[nl++] = dead->direct[k];
    if (blk_ok(dead->indirect)) {
        uint8_t ip[BSIZE];
        if (rd(dead->indirect, ip) == 0)
            for (uint32_t k = 0; k < NINDIRECT; k++) {
                uint32_t e; memcpy(&e, ip + 4 * k, 4);
                if (blk_ok(e)) list[nl++] = e;
            }
        list[nl++] = dead->indirect;
    }
    if (nl == 0) return 0;
    uint8_t bm[BSIZE];
    if (rd(DBM, bm) < 0) return -EIO;
    for (int k = 0; k < nl; k++) { uint32_t d = list[k] - DSTART; bm[d / 8] &= (uint8_t)~(1u << (d % 8)); }
    return mput(DBM, bm);
}
/* inode 하나의 링크를 하나 줄이고 0 이면 반납 (M14 unlink 순서 ②~⑤) */
static int drop_link(uint32_t inum) {
    dinode_t ino;
    int rc = iget(inum, &ino);
    if (rc < 0) return rc;
    if (--ino.nlink > 0) return iput(inum, &ino);
    dinode_t dead = ino, z = { 0 };
    if ((rc = iput(inum, &z)) < 0) return rc;
    if ((rc = free_blocks(&dead)) < 0) return rc;
    return bm_clear(IBM, inum);
}
static int create_node(const char *path, int type) {
    char dpath[128], name[NAME_MAX_ + 1];
    int rc = split_last(path, dpath, sizeof dpath, name);
    if (rc < 0) return rc;
    dinode_t pd;
    int pinum = namei_(dpath, &pd);
    if (pinum < 0) return pinum;
    if (pd.type != T_DIR) return -ENOTDIR;
    rc = dir_lookup(&pd, name, NULL);
    if (rc >= 0) return -EEXIST;
    if (rc != -ENOENT) return rc;
    int inum = ialloc();
    if (inum < 0) return inum;
    dinode_t ino = { .type = (uint16_t)type, .nlink = type == T_DIR ? 2 : 1 };
    if (type == T_DIR) {
        long b = balloc();
        if (b < 0) return (int)b;
        uint8_t p[BSIZE] = { 0 };
        dirent_t dot = { (uint32_t)inum, "." }, dotdot = { (uint32_t)pinum, ".." };
        memcpy(p, &dot, sizeof dot); memcpy(p + DIRENT_SIZE, &dotdot, sizeof dotdot);
        if ((rc = mput((uint32_t)b, p)) < 0) return rc;
        ino.direct[0] = (uint32_t)b; ino.size = 2 * DIRENT_SIZE;
    }
    if ((rc = iput((uint32_t)inum, &ino)) < 0) return rc;
    if ((rc = dir_add((uint32_t)pinum, name, (uint32_t)inum, type == T_DIR)) < 0) return rc;
    return inum;
}
int fs_create(const char *path) { op_enter(); return op_exit(create_node(path, T_FILE)); }
int fs_mkdir(const char *path)  { op_enter(); return op_exit(create_node(path, T_DIR)); }

static long write_(uint32_t inum, uint32_t off, const void *buf, uint32_t n) {
    dinode_t ino, old;
    int rc = iget(inum, &ino);
    if (rc < 0) return rc;
    if (ino.type != T_FILE) return ino.type == T_DIR ? -EISDIR : -ENOENT;
    old = ino;
    const uint8_t *src = buf;
    uint8_t blk[BSIZE];
    uint32_t done = 0;
    long err = 0;
    while (done < n) {
        uint32_t pos = off + done, bo = pos % BSIZE;
        uint32_t m = BSIZE - bo < n - done ? BSIZE - bo : n - done;
        int fresh = 0;
        long b = bmap(&ino, pos / BSIZE, 1, &fresh);
        if (b <= 0) { err = b < 0 ? b : -EIO; break; }
        if (m == BSIZE) memcpy(blk, src + done, BSIZE);
        else {
            if (fresh) memset(blk, 0, BSIZE);
            else if ((err = rd((uint32_t)b, blk)) < 0) break;
            memcpy(blk + bo, src + done, m);
        }
        J.user_blocks++;
        if ((err = dput((uint32_t)b, blk)) < 0) break;
        done += m;
    }
    if (done > 0 && off + done > ino.size) ino.size = off + done;
    if (memcmp(&ino, &old, sizeof ino) != 0 && (rc = iput(inum, &ino)) < 0) return rc;
    return done > 0 ? (long)done : err;
}
long fs_write(uint32_t inum, uint32_t off, const void *buf, uint32_t n) {
    op_enter();
    return op_exit((int)write_(inum, off, buf, n));
}
long fs_read(uint32_t inum, uint32_t off, void *buf, uint32_t n) {
    dinode_t ino;
    int rc = iget(inum, &ino);
    if (rc < 0) return rc;
    if (ino.type != T_FILE) return -EISDIR;
    if (off >= ino.size) return 0;
    if (n > ino.size - off) n = ino.size - off;
    uint8_t *dst = buf, blk[BSIZE];
    for (uint32_t done = 0; done < n;) {
        uint32_t pos = off + done, bo = pos % BSIZE;
        uint32_t m = BSIZE - bo < n - done ? BSIZE - bo : n - done;
        long b = bmap(&ino, pos / BSIZE, 0, NULL);
        if (b < 0 || (b != 0 && !blk_ok((uint32_t)b))) return -EIO;
        if (b == 0) memset(dst + done, 0, m);
        else { if (rd((uint32_t)b, blk) < 0) return -EIO; memcpy(dst + done, blk + bo, m); }
        done += m;
    }
    return n;
}
static int unlink_(const char *path) {
    char dpath[128], name[NAME_MAX_ + 1];
    int rc = split_last(path, dpath, sizeof dpath, name);
    if (rc < 0) return rc;
    dinode_t pd, ino;
    int pinum = namei_(dpath, &pd);
    if (pinum < 0) return pinum;
    uint32_t slot;
    int inum = dir_lookup(&pd, name, &slot);
    if (inum < 0) return inum;
    if ((rc = iget((uint32_t)inum, &ino)) < 0) return rc;
    if (ino.type != T_FILE) return -EISDIR;
    if ((rc = dir_set((uint32_t)pinum, slot, 0, NULL)) < 0) return rc;    /* 이름 먼저 */
    return drop_link((uint32_t)inum);
}
int fs_unlink(const char *path) { op_enter(); return op_exit(unlink_(path)); }

/* rename(old, new): 파일만. new 가 있으면 그 칸을 old 의 inode 로 바꾸고(교체) 옛 대상의 링크를 하나 줄인다. */
static int rename_(const char *from, const char *to) {
    char fd_[128], fn[NAME_MAX_ + 1], td[128], tn[NAME_MAX_ + 1];
    int rc;
    if ((rc = split_last(from, fd_, sizeof fd_, fn)) < 0 || (rc = split_last(to, td, sizeof td, tn)) < 0) return rc;
    dinode_t fp, tp, src;
    int fpi = namei_(fd_, &fp), tpi = namei_(td, &tp);
    if (fpi < 0) return fpi;
    if (tpi < 0) return tpi;
    if (tp.type != T_DIR) return -ENOTDIR;
    uint32_t fslot, tslot;
    int si = dir_lookup(&fp, fn, &fslot);
    if (si < 0) return si;
    if ((rc = iget((uint32_t)si, &src)) < 0) return rc;
    if (src.type != T_FILE) return -EISDIR;
    int ti = dir_lookup(&tp, tn, &tslot);
    if (ti == si) return 0;
    if (ti >= 0) {
        dinode_t tgt;
        if ((rc = iget((uint32_t)ti, &tgt)) < 0) return rc;
        if (tgt.type != T_FILE) return -EISDIR;
        if ((rc = dir_set((uint32_t)tpi, tslot, (uint32_t)si, tn)) < 0) return rc;   /* 새 이름 → 새 inode */
        if ((rc = drop_link((uint32_t)ti)) < 0) return rc;                         /* 옛 대상 반납 */
    } else if (ti == -ENOENT) {
        if ((rc = dir_add((uint32_t)tpi, tn, (uint32_t)si, 0)) < 0) return rc;
    } else return ti;
    return dir_set((uint32_t)fpi, fslot, 0, NULL);                                 /* 옛 이름 지움 */
}
int fs_rename(const char *from, const char *to) { op_enter(); return op_exit(rename_(from, to)); }

/* ============================ fsck (주어짐, 검사만) ============================ */
enum { FK_BAD_TYPE, FK_BAD_PTR, FK_DUP_BLOCK, FK_DANGLING, FK_ORPHAN, FK_NLINK, FK_IBITMAP, FK_LEAKED, FK_UNMARKED, FK_N };
static const char *const FK_NAME[FK_N] = { "bad-type", "bad-ptr", "dup-block", "dangling-dirent", "orphan-inode",
                                           "nlink", "inode-bitmap", "leaked-block", "unmarked-block" };
typedef struct { int n[FK_N]; int total; } fsck_t;
static void fk(fsck_t *r, int k) { r->n[k]++; r->total++; }
static int fsck(fsck_t *r) {
    memset(r, 0, sizeof *r);
    static int32_t owner[NBLOCKS];
    static uint16_t refs[NINODES];
    static uint8_t alive[NINODES], seen[NINODES];
    uint32_t queue[NINODES], qh = 0, qt = 0;
    for (uint32_t b = 0; b < NBLOCKS; b++) owner[b] = -1;
    memset(refs, 0, sizeof refs); memset(alive, 0, sizeof alive); memset(seen, 0, sizeof seen);
    uint8_t p[BSIZE];
    for (uint32_t i = 0; i < NINODES; i++) {
        dinode_t ino;
        if (iget(i, &ino) < 0) return -EIO;
        if (ino.type == T_FREE) continue;
        if (ino.type > T_DIR) { fk(r, FK_BAD_TYPE); continue; }
        alive[i] = 1;
        uint32_t ptr[NDIRECT + 1 + NINDIRECT]; int np = 0;
        for (int k = 0; k < NDIRECT; k++) if (ino.direct[k]) ptr[np++] = ino.direct[k];
        if (ino.indirect) {
            if (!blk_ok(ino.indirect)) fk(r, FK_BAD_PTR);
            else {
                ptr[np++] = ino.indirect;
                if (rd(ino.indirect, p) < 0) return -EIO;
                for (uint32_t k = 0; k < NINDIRECT; k++) { uint32_t e; memcpy(&e, p + 4 * k, 4); if (e) ptr[np++] = e; }
            }
        }
        for (int k = 0; k < np; k++) {
            if (!blk_ok(ptr[k])) fk(r, FK_BAD_PTR);
            else if (owner[ptr[k]] >= 0) fk(r, FK_DUP_BLOCK);
            else owner[ptr[k]] = (int32_t)i;
        }
    }
    if (!alive[ROOT_INO]) return r->total + 1;
    queue[qt++] = ROOT_INO; seen[ROOT_INO] = 1;
    while (qh < qt) {
        uint32_t di = queue[qh++];
        dinode_t dir;
        if (iget(di, &dir) < 0) return -EIO;
        if (dir.type != T_DIR) continue;
        uint32_t nent = dir.size / DIRENT_SIZE;
        if (nent > MAXFILE * DPB) nent = 0;
        for (uint32_t s = 0; s < nent; s++) {
            if (s % DPB == 0) {
                long b = bmap(&dir, s / DPB, 0, NULL);
                if (b <= 0 || !blk_ok((uint32_t)b)) { s += DPB - 1; continue; }
                if (rd((uint32_t)b, p) < 0) return -EIO;
            }
            dirent_t e; memcpy(&e, p + (s % DPB) * DIRENT_SIZE, sizeof e);
            e.name[NAME_MAX_] = 0;
            if (!e.name[0]) continue;
            if (e.inum >= NINODES || !alive[e.inum]) { fk(r, FK_DANGLING); continue; }
            refs[e.inum]++;
            if (strcmp(e.name, ".") && strcmp(e.name, "..") && !seen[e.inum]) {
                dinode_t t;
                if (iget(e.inum, &t) == 0 && t.type == T_DIR) { seen[e.inum] = 1; queue[qt++] = e.inum; }
            }
        }
    }
    for (uint32_t i = 0; i < NINODES; i++) {
        if (!alive[i]) continue;
        dinode_t ino; iget(i, &ino);
        if (refs[i] == 0) fk(r, FK_ORPHAN);
        else if (ino.nlink != refs[i]) fk(r, FK_NLINK);
    }
    if (rd(IBM, p) < 0) return -EIO;
    for (uint32_t i = 0; i < NINODES; i++) if ((int)((p[i / 8] >> (i % 8)) & 1) != (alive[i] != 0)) fk(r, FK_IBITMAP);
    if (rd(DBM, p) < 0) return -EIO;
    for (uint32_t d = 0; d < NDATA; d++) {
        int have = (p[d / 8] >> (d % 8)) & 1, want = owner[DSTART + d] >= 0;
        if (have && !want) fk(r, FK_LEAKED);
        if (!have && want) fk(r, FK_UNMARKED);
    }
    return r->total;
}
GIVEN static void fsck_print(const fsck_t *r) {
    for (int k = 0; k < FK_N; k++) if (r->n[k]) printf(" %s=%d", FK_NAME[k], r->n[k]);
}

/* ============================ 논리 상태 요약 (주어짐) ============================ */
/* 트리를 걸으며 (경로, 종류, 크기, nlink) 와 (+ 내용) 의 64-bit 해시. 순서 무관(합). 읽기 실패 = 1 반환 */
static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *s = p;
    for (size_t i = 0; i < n; i++) { h ^= s[i]; h *= 0x100000001b3ull; }
    return h;
}
static int digest(uint64_t *meta, uint64_t *full) {
    *meta = *full = 0;
    struct { uint32_t inum; char path[96]; } q[NINODES];
    int qh = 0, qt = 0;
    uint8_t seen[NINODES] = { 0 };
    q[qt].inum = ROOT_INO; strcpy(q[qt].path, ""); qt++; seen[ROOT_INO] = 1;
    static uint8_t data[MAXFILE * BSIZE];
    while (qh < qt) {
        uint32_t di = q[qh].inum; char base[96]; strcpy(base, q[qh].path); qh++;
        dinode_t dir;
        if (iget(di, &dir) < 0 || dir.type != T_DIR || dir.size > 4 * BSIZE) return 1;
        uint8_t p[BSIZE];
        for (uint32_t s = 0; s < dir.size / DIRENT_SIZE; s++) {
            if (s % DPB == 0) {
                long b = bmap(&dir, s / DPB, 0, NULL);
                if (b <= 0 || !blk_ok((uint32_t)b) || rd((uint32_t)b, p) < 0) return 1;
            }
            dirent_t e; memcpy(&e, p + (s % DPB) * DIRENT_SIZE, sizeof e);
            e.name[NAME_MAX_] = 0;
            if (!e.name[0] || !strcmp(e.name, ".") || !strcmp(e.name, "..")) continue;
            dinode_t t;
            if (e.inum >= NINODES || iget(e.inum, &t) < 0 || t.type == T_FREE || t.type > T_DIR) return 1;
            char path[96];
            snprintf(path, sizeof path, "%.60s/%s", base, e.name);
            uint64_t h = fnv(0xcbf29ce484222325ull, path, strlen(path));
            uint32_t attrs[3] = { t.type, t.size, t.nlink };
            h = fnv(h, attrs, sizeof attrs);
            *meta += h;
            if (t.type == T_FILE) {
                if (t.size > sizeof data) return 1;
                long n = fs_read(e.inum, 0, data, t.size);
                if (n != (long)t.size) return 1;
                h = fnv(h, data, t.size);
            } else if (!seen[e.inum] && qt < NINODES) {
                seen[e.inum] = 1; q[qt].inum = e.inum; snprintf(q[qt].path, sizeof q[qt].path, "%s", path); qt++;
            }
            *full += h;
        }
    }
    return 0;
}

/* ============================ 워크로드와 크래시 스윕 ============================ */
enum { OP_CREATE, OP_MKDIR, OP_WRITE, OP_UNLINK, OP_RENAME };
typedef struct { int kind; const char *p, *q; uint32_t off, n, seed; } op_t;
typedef struct { const char *name; int nsetup; op_t setup[16]; int nops; op_t ops[4]; } workload_t;
enum { MAXOPS = 4 };

static void pattern(uint8_t *b, size_t n, uint32_t seed) {     /* xorshift32 바이트 */
    uint32_t x = seed ? seed : 1;
    for (size_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[i] = (uint8_t)x; }
}
static int run_op(const op_t *o) {
    static uint8_t buf[MAXFILE * BSIZE];
    switch (o->kind) {
    case OP_CREATE: return fs_create(o->p);
    case OP_MKDIR:  return fs_mkdir(o->p);
    case OP_UNLINK: return fs_unlink(o->p);
    case OP_RENAME: return fs_rename(o->p, o->q);
    default: {
        int inum = fs_namei(o->p, NULL);
        if (inum < 0) return inum;
        pattern(buf, o->n, o->seed);
        return (int)fs_write((uint32_t)inum, o->off, buf, o->n);
    }
    }
}
/* ops 를 차례로. 전원이 나가면 거기서 멈춘다. 반환 = 전원이 나갈 때 진행 중이던 op 번호(1 부터), 안 나갔으면 nops+1 */
static int run_ops(const op_t *ops, int n) {
    for (int i = 0; i < n; i++) {
        run_op(&ops[i]);
        if (D.crashed) return i + 1;
    }
    return n + 1;
}

static uint8_t SNAP[NBLOCKS][BSIZE];
static uint64_t ST_META[MAXOPS + 1], ST_FULL[MAXOPS + 1];
/* 새 디스크 → mkfs → mount → setup ops → sync. 그 매체를 SNAP 에 (스윕의 출발점) */
static int prepare(const workload_t *w) {
    dev_init();
    fs_mkfs();
    if (fs_mount() < 0) return -1;
    for (int i = 0; i < w->nsetup; i++) if (run_op(&w->setup[i]) < 0) return -1;
    fs_sync();
    memcpy(SNAP, D.media, sizeof SNAP);
    return 0;
}
static int restore_and_mount(void) {
    memcpy(D.media, SNAP, sizeof SNAP);
    dev_reboot(); io_reset();
    int rc = fs_mount();
    io_reset();
    return rc;
}
/* 크래시 없는 기준 실행: 상태 k (k 개 op 뒤)의 요약을 ST_* 에. 반환 = 총 쓰기 수 W */
GIVEN static long clean_run(const workload_t *w) {
    if (prepare(w) < 0 || restore_and_mount() < 0) return -1;
    if (digest(&ST_META[0], &ST_FULL[0])) return -1;
    for (int i = 0; i < w->nops; i++) {
        run_op(&w->ops[i]);
        if (digest(&ST_META[i + 1], &ST_FULL[i + 1])) return -1;
    }
    return D.nwrite;
}

typedef struct {
    long W; int runs, fsck_bad, meta_bad, data_bad, lost;
    long skipped, crc_bad;                      /* 복구가 버린 미완성 트랜잭션 수, 그중 checksum 이 잡은 수 */
    int bad_n[8], nbad;                         /* 내용이 틀린 N (처음 8 개) */
    int bad_mask[8];
} sweep_t;

/* TODO B2: 크래시 전수 스윕. W = clean_run(w) (실패면 -1 반환). N = 0..W 각각, 그리고 쓰기 캐시가 켜져 있으면
 * mask = 0..2^WC_MAX-1 각각에 대해:
 *   restore_and_mount() → D.crash_at = N, D.crash_mask = mask → j = run_ops() → power_cut() (끝까지 안 나갔어도 끊는다)
 *   → mask ≥ (1 << D.pend_at_crash) 이면 같은 결과의 중복이니 건너뛴다 → 아니면 runs++
 *   → dev_reboot() → fs_mount() (= 복구) → fsck 결과 > 0 이면 fsck_bad++
 *   → digest 가 실패하면 meta_bad++, data_bad++ (bad_n 기록). 아니면 허용 상태 k ∈ {j-1, j} ∩ [0, nops] 중
 *     meta 가 같은 k 가 없으면: 더 앞의 k (< j-1) 와 같으면 lost++ (커밋된 op 를 잃음), 아니면 meta_bad++.
 *     full(내용 포함) 이 같은 k 가 허용 집합에 없으면 data_bad++ 하고 처음 8 개의 N(과 mask)을 기록.
 * r->W, r->skipped / r->crc_bad (= 스윕 동안 늘어난 J.skipped / J.crc_bad) 도 채운다. 반환 0. */
GIVEN static int sweep(const workload_t *w, sweep_t *r) {
    memset(r, 0, sizeof *r);
    long W = clean_run(w);
    if (W < 0) return -1;
    r->W = W;
    long sk0 = J.skipped, cb0 = J.crc_bad;
    unsigned masks = D.wc_on ? 1u << WC_MAX : 1u;
    for (long N = 0; N <= W; N++)
        for (unsigned mask = 0; mask < masks; mask++) {
            if (restore_and_mount() < 0) return -1;
            D.crash_at = N; D.crash_mask = mask;
            int j = run_ops(w->ops, w->nops);
            power_cut();
            if (mask >= (1u << D.pend_at_crash)) continue;
            r->runs++;
            dev_reboot();
            fsck_t fr;
            if (fs_mount() < 0 || fsck(&fr) != 0) r->fsck_bad++;
            uint64_t m, f;
            int lo = j - 1, hi = j > w->nops ? w->nops : j;
            int bad_data = 0;
            if (digest(&m, &f)) { r->meta_bad++; bad_data = 1; }
            else {
                int okm = 0, okf = 0, early = 0;
                for (int k = lo; k <= hi; k++) { okm |= ST_META[k] == m; okf |= ST_FULL[k] == f; }
                for (int k = 0; k < lo; k++) early |= ST_META[k] == m && ST_FULL[k] == f;
                if (!okm) { if (early) r->lost++; else r->meta_bad++; }
                if (!okf && !early) bad_data = 1;
            }
            if (bad_data) {
                if (r->nbad < 8) { r->bad_n[r->nbad] = (int)N; r->bad_mask[r->nbad] = (int)mask; r->nbad++; }
                r->data_bad++;
            }
        }
    r->skipped = J.skipped - sk0; r->crc_bad = J.crc_bad - cb0;
    return 0;
}

static const workload_t WL[] = {
    { "create+write", 1, { { OP_MKDIR, "/d", 0, 0, 0, 0 } }, 2,
      { { OP_CREATE, "/d/f", 0, 0, 0, 0 }, { OP_WRITE, "/d/f", 0, 0, 3000, 11 } } },
    { "append->indirect", 2, { { OP_CREATE, "/f", 0, 0, 0, 0 }, { OP_WRITE, "/f", 0, 0, 5000, 21 } }, 2,
      { { OP_WRITE, "/f", 0, 5000, 2500, 22 }, { OP_WRITE, "/f", 0, 7500, 1200, 23 } } },
    { "unlink", 3, { { OP_CREATE, "/a", 0, 0, 0, 0 }, { OP_WRITE, "/a", 0, 0, 8000, 31 }, { OP_CREATE, "/b", 0, 0, 0, 0 } }, 1,
      { { OP_UNLINK, "/a", 0, 0, 0, 0 } } },
    { "mkdir+create", 0, { { 0, 0, 0, 0, 0, 0 } }, 2,
      { { OP_MKDIR, "/x", 0, 0, 0, 0 }, { OP_CREATE, "/x/y", 0, 0, 0, 0 } } },
    { "rename-over", 4, { { OP_CREATE, "/cfg", 0, 0, 0, 0 }, { OP_WRITE, "/cfg", 0, 0, 1500, 41 },
                          { OP_CREATE, "/cfg.tmp", 0, 0, 0, 0 }, { OP_WRITE, "/cfg.tmp", 0, 0, 1700, 42 } }, 1,
      { { OP_RENAME, "/cfg.tmp", "/cfg", 0, 0, 0 } } },
    { "reuse", 2, { { OP_CREATE, "/a", 0, 0, 0, 0 }, { OP_WRITE, "/a", 0, 0, 2048, 51 } }, 3,
      { { OP_UNLINK, "/a", 0, 0, 0, 0 }, { OP_CREATE, "/b", 0, 0, 0, 0 }, { OP_WRITE, "/b", 0, 0, 2048, 52 } } },
    { "overwrite", 2, { { OP_CREATE, "/f", 0, 0, 0, 0 }, { OP_WRITE, "/f", 0, 0, 3072, 61 } }, 1,
      { { OP_WRITE, "/f", 0, 0, 3072, 62 } } },
    { "wrap", 12, { { OP_CREATE, "/s0", 0, 0, 0, 0 }, { OP_CREATE, "/s1", 0, 0, 0, 0 }, { OP_CREATE, "/s2", 0, 0, 0, 0 },
                    { OP_CREATE, "/s3", 0, 0, 0, 0 }, { OP_CREATE, "/s4", 0, 0, 0, 0 }, { OP_CREATE, "/s5", 0, 0, 0, 0 },
                    { OP_CREATE, "/s6", 0, 0, 0, 0 }, { OP_CREATE, "/s7", 0, 0, 0, 0 }, { OP_CREATE, "/s8", 0, 0, 0, 0 },
                    { OP_CREATE, "/s9", 0, 0, 0, 0 }, { OP_CREATE, "/sa", 0, 0, 0, 0 }, { OP_CREATE, "/sb", 0, 0, 0, 0 } }, 2,
      { { OP_CREATE, "/z", 0, 0, 0, 0 }, { OP_WRITE, "/z", 0, 0, 2000, 71 } } },
};
enum { NWL = (int)(sizeof WL / sizeof WL[0]) };

#ifndef A07_NO_MAIN
/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void setmode(int mode, int barrier, int csum, int lazy, int wc) {
    J.mode = mode; J.barrier = barrier; J.csum = csum; J.lazy = lazy; D.wc_on = wc;
}
static int fresh(int mode) { setmode(mode, 1, 1, 0, 0); dev_init(); fs_mkfs(); int rc = fs_mount(); io_reset(); return rc; }
static int fsck_total(void) { fsck_t r; return fsck(&r); }

static void test_block_layer(void) {
    printf("-- 0: block device + crc32\n");
    uint8_t a[BSIZE], b[BSIZE];
    dev_init(); D.wc_on = 0;
    pattern(a, BSIZE, 7);
    CHECK(bwrite(80, a) == 0 && bread(80, b) == 0 && memcmp(a, b, BSIZE) == 0 && D.nwrite == 1);
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u);
    CHECK(crc32_update(crc32_update(0, "1234", 4), "56789", 5) == 0xCBF43926u);
    /* 쓰기 캐시: 쓰기 3 개가 캐시에 → 전원 차단 mask=0b101 → 1번째·3번째만 살아남는다 */
    dev_init(); D.wc_on = 1;
    uint8_t x[3][BSIZE];
    for (int i = 0; i < 3; i++) { memset(x[i], 'a' + i, BSIZE); bwrite((uint32_t)(100 + i), x[i]); }
    CHECK(D.media[100][0] == 0 && bread(101, b) == 0 && b[0] == 'b');        /* flush 전: 매체엔 없음, 읽기는 캐시 */
    D.crash_mask = 5u; power_cut();
    CHECK(D.pend_at_crash == 3 && D.media[100][0] == 'a' && D.media[101][0] == 0 && D.media[102][0] == 'c');
    dev_reboot(); D.wc_on = 0;
}

static void part_a(void) {
    printf("-- Part A: commit / checkpoint / recover\n");
    CHECK(fresh(M_ORDERED) == 0);
    CHECK(fs_create("/a") == 1);
    printf("  create /a (ordered): %s\n", D.tbuf);
    CHECK(strcmp(D.tbuf, "W J0 W J1 W J2 W J3 F W J4 F W ibm W I0 W D0 F W js F") == 0);
    CHECK(J.commits == 1 && J.head == 5 && J.next_tid == 2 && J.nck == 0);

    uint8_t buf[3000], got[3000];
    pattern(buf, sizeof buf, 5);
    io_reset();
    CHECK(fs_write(1, 0, buf, 3000) == 3000);
    printf("  write 3000 B (ordered): %s\n", D.tbuf);
    CHECK(strcmp(D.tbuf, "W D1 W D2 W D3 W J5 W J6 W J7 F W J8 F W dbm W I0 F W js F") == 0);
    CHECK(fs_read(1, 0, got, 3000) == 3000 && memcmp(buf, got, 3000) == 0);

    J.mode = M_DATA; io_reset();
    CHECK(fs_write(1, 3000, buf, 1000) == 1000);                 /* 블록 2(D3) 끝 + 새 블록 D4 */
    printf("  append 1000 B (data):  %s\n", D.tbuf);
    CHECK(strcmp(D.tbuf, "W J9 W J10 W J11 W J12 W J13 F W J14 F W D3 W dbm W D4 W I0 F W js F") == 0);

    J.mode = M_WRITEBACK; io_reset();
    CHECK(fs_write(1, 4000, buf, 1100) == 1100);
    printf("  append 1100 B (wb):    %s\n", D.tbuf);
    CHECK(strcmp(D.tbuf, "W J15 W J16 W J17 F W J18 F W D4 W D5 W dbm W I0 F W js F") == 0);
    J.mode = M_ORDERED;
    CHECK(fsck_total() == 0);

    /* 커밋 직후(체크포인트 전) 전원 차단 → replay 가 살린다. TxE 전 → 없던 일 */
    struct { long n; int replayed, exists; } cases[] = { { 5, 1, 1 }, { 4, 0, 0 }, { 0, 0, 0 }, { 7, 1, 1 } };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        fresh(M_ORDERED);
        D.crash_at = cases[c].n;
        fs_create("/b");
        power_cut(); dev_reboot();
        long rep0 = J.replayed;
        int mrc = fs_mount();
        printf("  crash at write #%ld of create /b → replayed %ld, /b %s, fsck %d\n", cases[c].n, J.replayed - rep0,
               fs_namei("/b", NULL) >= 0 ? "exists" : "absent", fsck_total());
        CHECK(mrc == 0 && J.replayed - rep0 == cases[c].replayed && (fs_namei("/b", NULL) >= 0) == cases[c].exists &&
              fsck_total() == 0);
    }
    /* 다시 마운트해도 같은 상태 (replay 는 멱등, 두 번째 마운트는 아무것도 안 함) */
    io_reset(); long rep0 = J.replayed;
    CHECK(fs_mount() == 0 && J.replayed == rep0 && D.nwrite == 0 && fs_namei("/b", NULL) >= 0);

    /* 찢어진 트랜잭션: 커밋된 저널의 로그 블록 하나를 망가뜨림 → checksum 이 잡아 건너뜀 */
    fresh(M_ORDERED);
    D.crash_at = 5; fs_create("/c"); power_cut(); dev_reboot();
    D.media[JSTART + 2][100] ^= 0xFF;                            /* I0 의 로그 사본 1 바이트 */
    long sk0 = J.skipped;
    CHECK(fs_mount() == 0 && J.skipped == sk0 + 1 && fs_namei("/c", NULL) == -ENOENT && fsck_total() == 0);
    CHECK(J.next_tid == 2);                                      /* tid 1 은 찢어졌으니 건너뜀 */
    CHECK(fs_create("/c") == 1 && fs_mount() == 0 && fs_namei("/c", NULL) == 1 && fsck_total() == 0);

    /* 저널 한 바퀴: 13 번 create → wrap 이 일어나도 마운트 후 fsck 0 */
    fresh(M_ORDERED);
    char nm[16];
    for (int i = 0; i < 13; i++) { snprintf(nm, sizeof nm, "/w%d", i); fs_create(nm); }
    CHECK(J.wraps >= 1 && J.head < 10 && fs_mount() == 0 && fs_namei("/w12", NULL) > 0 && fsck_total() == 0);
}

static void print_sweep(const char *wl, int mode, const sweep_t *r) {
    printf("  %-17s %-9s W=%3ld runs=%4d fsck_bad=%3d meta_bad=%3d data_bad=%3d lost=%d",
           wl, MODE_NAME[mode], r->W, r->runs, r->fsck_bad, r->meta_bad, r->data_bad, r->lost);
    if (r->nbad) { printf("  N="); for (int i = 0; i < r->nbad; i++) printf("%s%d", i ? "," : "", r->bad_n[i]); }
    printf("\n");
}

static void part_b(void) {
    printf("-- Part B: exhaustive crash sweep (crash at every write N, then mount+replay+fsck)\n");
    static sweep_t R[4][NWL];
    int ok_sweep = 1;
    for (int m = 0; m < 4; m++)
        for (int w = 0; w < NWL; w++) {
            setmode(m, 1, 1, 0, 0);
            if (sweep(&WL[w], &R[m][w]) != 0) ok_sweep = 0;
            print_sweep(WL[w].name, m, &R[m][w]);
        }
    CHECK(ok_sweep);
    int runs_ok = 1, none_fsck = 0, a_clean = 1, b_meta = 1, c_meta = 1, lost = 0;
    for (int w = 0; w < NWL; w++) {
        for (int m = 0; m < 4; m++) { runs_ok &= R[m][w].runs == R[m][w].W + 1 && R[m][w].W > 0; lost += R[m][w].lost; }
        none_fsck += R[M_NONE][w].fsck_bad > 0;
        a_clean &= R[M_DATA][w].fsck_bad == 0 && R[M_DATA][w].meta_bad == 0 && R[M_DATA][w].data_bad == 0;
        b_meta &= R[M_ORDERED][w].fsck_bad == 0 && R[M_ORDERED][w].meta_bad == 0;
        c_meta &= R[M_WRITEBACK][w].fsck_bad == 0 && R[M_WRITEBACK][w].meta_bad == 0;
    }
    CHECK(runs_ok);                                      /* 모든 N = 0..W 를 돌았다 */
    CHECK(none_fsck >= 5);                               /* 저널 없음: 대부분의 워크로드에서 fsck 가 뭔가 찾는다 */
    CHECK(R[M_NONE][4].fsck_bad > 0 && R[M_NONE][4].meta_bad > 0);   /* rename-over: 이름 둘/없음 같은 비원자 상태 */
    CHECK(a_clean);                                      /* (a) data: fsck 0, 메타·내용 모두 old-or-new */
    CHECK(b_meta && c_meta);                             /* (b)(c): 메타데이터는 언제나 일관 */
    CHECK(lost == 0);                                    /* 커밋된 op 를 잃은 적 없음 (op 마다 flush) */
    int b_data_other = 0;
    for (int w = 0; w < NWL; w++) if (w != 6) b_data_other += R[M_ORDERED][w].data_bad;
    CHECK(b_data_other == 0 && R[M_ORDERED][6].data_bad > 0);       /* ordered: 제자리 덮어쓰기만 섞인다 */
    CHECK(R[M_WRITEBACK][0].data_bad > 0 && R[M_WRITEBACK][1].data_bad > 0 && R[M_WRITEBACK][5].data_bad > 0);

    /* writeback 의 "reuse" 첫 위반 N 을 재현: /b 에 지워진 /a 의 바이트가 보인다 (fsck 는 0) */
    setmode(M_WRITEBACK, 1, 1, 0, 0);
    const sweep_t *rw = &R[M_WRITEBACK][5];
    int leak = 0, fz = -1;
    if (rw->nbad > 0 && prepare(&WL[5]) == 0 && restore_and_mount() == 0) {
        D.crash_at = rw->bad_n[0];
        run_ops(WL[5].ops, WL[5].nops); power_cut(); dev_reboot(); fs_mount();
        fz = fsck_total();
        uint8_t got[2048], olda[2048];
        dinode_t ino;
        int bi = fs_namei("/b", &ino);
        pattern(olda, sizeof olda, 51);
        if (bi > 0 && fs_read((uint32_t)bi, 0, got, sizeof got) == 2048) leak = memcmp(got, olda, sizeof got) == 0;
        printf("  writeback reuse, crash at N=%d: fsck=%d, /b size=%u, /b == old /a bytes? %s\n",
               rw->bad_n[0], fz, bi > 0 ? ino.size : 0, leak ? "YES (stale data exposure)" : "no");
    }
    CHECK(leak && fz == 0);

    /* 쓰기 증폭 표: /f 에 4 KB × 4 (= 16 블록), 그리고 1 KB 파일 8 개 만들기 */
    printf("-- Part B: write amplification (device writes / user data blocks)\n");
    printf("  %-10s %-5s | %-30s | %-30s\n", "mode", "ckpt", "seq 16 KB (16 user blk)", "8 small files (8 user blk)");
    static uint8_t big[4096];
    pattern(big, sizeof big, 9);
    double wa[4][2][2]; long writes[4][2][2], fl[4][2][2];
    for (int m = 0; m < 4; m++)
        for (int lazy = 0; lazy < 2; lazy++) {
            if (m == M_NONE && lazy) continue;
            for (int wl = 0; wl < 2; wl++) {
                fresh(m); J.lazy = lazy; J.user_blocks = 0;
                if (wl == 0) {
                    int fi = fs_create("/f");
                    for (int k = 0; k < 4; k++) fs_write((uint32_t)fi, (uint32_t)k * 4096, big, 4096);
                } else
                    for (int k = 0; k < 8; k++) {
                        char nm[16]; snprintf(nm, sizeof nm, "/s%d", k);
                        int fi = fs_create(nm);
                        fs_write((uint32_t)fi, 0, big, 1024);
                    }
                fs_sync();
                writes[m][lazy][wl] = D.nwrite; fl[m][lazy][wl] = D.nflush;
                wa[m][lazy][wl] = (double)D.nwrite / (double)J.user_blocks;
            }
            printf("  %-10s %-5s | writes %3ld  WA %5.2f  flush %2ld | writes %3ld  WA %5.2f  flush %2ld\n", MODE_NAME[m],
                   lazy ? "lazy" : "each", writes[m][lazy][0], wa[m][lazy][0], fl[m][lazy][0],
                   writes[m][lazy][1], wa[m][lazy][1], fl[m][lazy][1]);
        }
    CHECK(wa[M_DATA][0][0] > wa[M_ORDERED][0][0] && wa[M_ORDERED][0][0] > wa[M_NONE][0][0]);
    CHECK(writes[M_ORDERED][0][0] == writes[M_WRITEBACK][0][0] && writes[M_ORDERED][0][1] == writes[M_WRITEBACK][0][1]);
    CHECK(writes[M_DATA][1][0] < writes[M_DATA][0][0] && writes[M_ORDERED][1][1] < writes[M_ORDERED][0][1]);
    setmode(M_ORDERED, 1, 1, 0, 0);
}

static void part_c(void) {
    printf("-- Part C: volatile write cache (window %d) — every N × every surviving subset\n", WC_MAX);
    struct { const char *name; int barrier, csum; } V[] = { { "barrier, no csum", 1, 0 }, { "NO barrier, no csum", 0, 0 },
                                                          { "NO barrier + csum", 0, 1 } };
    static sweep_t R[3][2][2];
    const int wls[2] = { 0, 1 };
    for (int v = 0; v < 3; v++)
        for (int mi = 0; mi < 2; mi++)
            for (int k = 0; k < 2; k++) {
                int mode = mi ? M_ORDERED : M_DATA;
                setmode(mode, V[v].barrier, V[v].csum, 0, 1);
                sweep(&WL[wls[k]], &R[v][mi][k]);
                const sweep_t *r = &R[v][mi][k];
                printf("  %-20s %-8s %-17s runs=%4d fsck_bad=%3d meta_bad=%3d data_bad=%3d skipped=%3ld (crc %ld)\n",
                       V[v].name, MODE_NAME[mode], WL[wls[k]].name, r->runs, r->fsck_bad, r->meta_bad, r->data_bad,
                       r->skipped, r->crc_bad);
            }
    D.wc_on = 0;
    int safe0 = 1, unsafe1 = 0, safe2 = 1;
    for (int k = 0; k < 2; k++) {
        safe0 &= R[0][0][k].fsck_bad + R[0][0][k].meta_bad + R[0][0][k].data_bad == 0 &&
                 R[0][1][k].fsck_bad + R[0][1][k].meta_bad + R[0][1][k].data_bad == 0;
        unsafe1 += R[1][0][k].fsck_bad + R[1][0][k].meta_bad;
        safe2 &= R[2][0][k].fsck_bad + R[2][0][k].meta_bad + R[2][0][k].data_bad == 0 && R[2][0][k].crc_bad > 0;
    }
    CHECK(R[0][0][0].runs > R[0][0][0].W + 1);           /* 부분집합까지 돌았다 */
    CHECK(safe0);                                        /* barrier 가 있으면 재정렬돼도 안전 (data · ordered) */
    CHECK(unsafe1 > 0);                                  /* barrier 도 checksum 도 없으면 TxE 가 로그보다 먼저 닿아 쓰레기 replay */
    CHECK(safe2);                                        /* data 모드: checksum 이 찢어진 트랜잭션을 버린다 */
    setmode(M_ORDERED, 1, 1, 0, 0);
}

int main(void) {
    test_block_layer();
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
