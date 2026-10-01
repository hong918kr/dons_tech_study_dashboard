// C0I_toy_ftl.c — OSTEP 부록 I: page-mapped, log-structured toy FTL + greedy GC + wear leveling
//
//  NAND 규칙을 코드로 강제한다:
//    * erase 단위 = block, program 단위 = page
//    * ERASED 페이지에만 program 가능, block 안에서는 낮은 page → 높은 page 순서로만 (program disturb 회피)
//  FTL:
//    * L2P(logical→physical) 테이블은 RAM, 각 page 의 OOB 에 (LPN, seq) 를 같이 기록
//    * host write 는 "host open block" 에 append, GC 복사는 별도 "GC open block" 에 append
//    * free block 이 low-water 아래로 내려가면 greedy GC (valid page 가 가장 적은 block 을 victim)
//    * free block 할당 시 erase count 가 가장 작은 것 선택 (dynamic wear leveling)
//    * 옵션: static wear leveling (오래 안 바뀌는 cold block 을 강제로 옮겨 erase 풀에 다시 넣기)
//  실험:
//    [1] 책 I.7~I.8 예제 재현 (4 page/block, LBA 100,101,2000,2001 → overwrite → GC)
//    [2] uniform random 4KB write: over-provisioning(OP) 별 WAF
//    [3] 80/20 hot/cold: static wear leveling 유무에 따른 erase count 편차
//    [4] power-loss: L2P 를 버리고 OOB scan 으로 재구성 (+ TRIM 이 로그되지 않으면 생기는 문제)
//    [5] mapping table 크기 계산
//
// build: cc -Wall -Wextra -O0 code/C0I_toy_ftl.c -o .work/bin/C0I_toy_ftl
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PG_ERASED = 0, PG_PROGRAMMED = 1 };
enum { BLK_FREE = 0, BLK_OPEN = 1, BLK_CLOSED = 2 };
#define NONE (-1)

typedef struct {               // physical page = data + OOB(spare area)
    uint8_t  state;
    int32_t  oob_lpn;          // 이 page 에 들어 있는 logical page 번호
    uint32_t oob_seq;          // 전역 증가 시퀀스 (복구 시 최신본 판별)
    uint32_t data;             // 4KB 대신 32bit 토큰 (검증용)
} page_t;

typedef struct {
    int ppb, nblk, nlpn;
    page_t  *pg;               // nblk*ppb
    int32_t *l2p;              // nlpn
    int     *valid, *erase_cnt, *bstate, *next_pg;
    int     host_open, gc_open, nfree;
    uint32_t seq;
    uint32_t *ver;             // lpn 별 최신 버전 (검증용, 호스트 쪽 진실)
    // stats
    long host_w, nand_w, nand_r, erases, gc_runs, gc_copied, wl_moves;
    double victim_valid_sum;
    int static_wl, wl_thresh;
    int verbose, gc_same_stream;
} ftl_t;

static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

// ------------------------------------------------------------ raw NAND ops
static int nand_erase(ftl_t *f, int b) {
    for (int i = 0; i < f->ppb; i++) { page_t *p = &f->pg[b * f->ppb + i]; p->state = PG_ERASED; p->oob_lpn = NONE; }
    f->next_pg[b] = 0; f->erase_cnt[b]++; f->erases++; f->valid[b] = 0;
    return 0;
}
static int nand_program(ftl_t *f, int ppn, int32_t lpn, uint32_t data) {
    int b = ppn / f->ppb, off = ppn % f->ppb;
    if (f->pg[ppn].state != PG_ERASED) { fprintf(stderr, "PROGRAM ERROR: page %d not erased\n", ppn); exit(2); }
    if (off != f->next_pg[b])           { fprintf(stderr, "PROGRAM ERROR: out-of-order page %d\n", ppn); exit(2); }
    f->pg[ppn] = (page_t){ PG_PROGRAMMED, lpn, ++f->seq, data };
    f->next_pg[b]++; f->nand_w++;
    return 0;
}

// ------------------------------------------------------------ FTL
static ftl_t *ftl_new(int nblk, int ppb, int nlpn) {
    ftl_t *f = calloc(1, sizeof *f);
    f->nblk = nblk; f->ppb = ppb; f->nlpn = nlpn;
    f->pg = calloc((size_t)nblk * ppb, sizeof(page_t));
    f->l2p = malloc(sizeof(int32_t) * nlpn); f->ver = calloc(nlpn, sizeof(uint32_t));
    f->valid = calloc(nblk, sizeof(int)); f->erase_cnt = calloc(nblk, sizeof(int));
    f->bstate = calloc(nblk, sizeof(int)); f->next_pg = calloc(nblk, sizeof(int));
    for (int i = 0; i < nlpn; i++) f->l2p[i] = NONE;
    for (int i = 0; i < nblk * ppb; i++) { f->pg[i].state = PG_PROGRAMMED; f->pg[i].oob_lpn = NONE; } // 공장 출하: INVALID
    f->nfree = nblk; f->host_open = f->gc_open = NONE;
    return f;
}
static void ftl_free(ftl_t *f) {
    free(f->pg); free(f->l2p); free(f->ver); free(f->valid); free(f->erase_cnt);
    free(f->bstate); free(f->next_pg); free(f);
}

static int alloc_block(ftl_t *f) {       // erase count 최소인 free block (dynamic WL)
    int best = NONE;
    for (int b = 0; b < f->nblk; b++)
        if (f->bstate[b] == BLK_FREE && (best == NONE || f->erase_cnt[b] < f->erase_cnt[best])) best = b;
    if (best == NONE) { fprintf(stderr, "FATAL: no free block\n"); exit(3); }
    nand_erase(f, best);                 // 쓰기 직전에 erase (OSTEP 예제와 같은 순서)
    f->bstate[best] = BLK_OPEN; f->nfree--;
    if (f->verbose) printf("    erase(block %d)\n", best);
    return best;
}

static void invalidate(ftl_t *f, int32_t lpn) {
    if (f->l2p[lpn] != NONE) { f->valid[f->l2p[lpn] / f->ppb]--; f->l2p[lpn] = NONE; }
}

// stream: 0 = host, 1 = GC
static int append(ftl_t *f, int stream, int32_t lpn, uint32_t data) {
    int *open = stream ? &f->gc_open : &f->host_open;
    if (*open == NONE || f->next_pg[*open] == f->ppb) {
        if (*open != NONE) f->bstate[*open] = BLK_CLOSED;
        *open = alloc_block(f);
    }
    int ppn = *open * f->ppb + f->next_pg[*open];
    nand_program(f, ppn, lpn, data);
    invalidate(f, lpn);
    f->l2p[lpn] = ppn; f->valid[*open]++;
    return ppn;
}

static void collect_block(ftl_t *f, int victim) {
    f->gc_runs++; f->victim_valid_sum += (double)f->valid[victim] / f->ppb;
    if (f->verbose) printf("    GC victim block %d (valid %d/%d)\n", victim, f->valid[victim], f->ppb);
    for (int i = 0; i < f->ppb; i++) {
        int ppn = victim * f->ppb + i; page_t *p = &f->pg[ppn];
        if (p->state == PG_PROGRAMMED && p->oob_lpn != NONE && f->l2p[p->oob_lpn] == ppn) { // live?
            f->nand_r++; f->gc_copied++;
            int to = append(f, f->gc_same_stream ? 0 : 1, p->oob_lpn, p->data);
            if (f->verbose) printf("    copy live LPN %d: page %d -> page %d\n", p->oob_lpn, ppn, to);
        }
    }
    f->bstate[victim] = BLK_FREE; f->nfree++;   // erase 는 다음 할당 때 (lazy erase)
}

static void maybe_gc(ftl_t *f) {
    while (f->nfree < 2) {                        // low-water mark: free block 2개 유지
        int victim = NONE;
        for (int b = 0; b < f->nblk; b++)
            if (f->bstate[b] == BLK_CLOSED && (victim == NONE || f->valid[b] < f->valid[victim])) victim = b;
        if (victim == NONE) { fprintf(stderr, "FATAL: nothing to collect\n"); exit(4); }
        collect_block(f, victim);
    }
    if (f->static_wl) {                           // static WL: 가장 덜 닳은 closed block 이 너무 뒤처지면 강제 이주
        int maxe = 0, cold = NONE;
        for (int b = 0; b < f->nblk; b++) if (f->erase_cnt[b] > maxe) maxe = f->erase_cnt[b];
        for (int b = 0; b < f->nblk; b++)
            if (f->bstate[b] == BLK_CLOSED && (cold == NONE || f->erase_cnt[b] < f->erase_cnt[cold])) cold = b;
        if (cold != NONE && maxe - f->erase_cnt[cold] > f->wl_thresh) { f->wl_moves++; collect_block(f, cold); }
    }
}

static void ftl_write(ftl_t *f, int32_t lpn) {
    maybe_gc(f);
    f->host_w++; f->ver[lpn]++;
    int ppn = append(f, 0, lpn, (uint32_t)lpn * 2654435761u ^ f->ver[lpn]);
    if (f->verbose) printf("  write(LBA %d) -> page %d\n", lpn, ppn);
}
static void ftl_trim(ftl_t *f, int32_t lpn) { invalidate(f, lpn); f->ver[lpn] = 0; }

static int ftl_verify(ftl_t *f) {             // 모든 LPN 을 읽어서 최신 버전인지 확인
    int bad = 0;
    for (int l = 0; l < f->nlpn; l++) {
        if (f->ver[l] == 0) { if (f->l2p[l] != NONE) bad++; continue; }
        if (f->l2p[l] == NONE || f->pg[f->l2p[l]].data != ((uint32_t)l * 2654435761u ^ f->ver[l])) bad++;
    }
    return bad;
}

static void erase_stats(ftl_t *f, int *mn, int *mx, double *avg) {
    *mn = 1 << 30; *mx = 0; long s = 0;
    for (int b = 0; b < f->nblk; b++) { int e = f->erase_cnt[b]; s += e; if (e < *mn) *mn = e; if (e > *mx) *mx = e; }
    *avg = (double)s / f->nblk;
}

// ------------------------------------------------------------ experiments
static void exp_book_example(void) {
    printf("[1] OSTEP I.7-I.8 example: 4 pages/block, write 100,101,2000,2001 then overwrite 100,101\n");
    ftl_t *f = ftl_new(4, 4, 4096); f->verbose = 1; f->gc_same_stream = 1;  // 책처럼 로그 하나
    int seq[] = { 100, 101, 2000, 2001, 100, 101 };
    for (int i = 0; i < 6; i++) { f->ver[seq[i]]++; f->host_w++; int p = append(f, 0, seq[i], (uint32_t)seq[i] * 2654435761u ^ f->ver[seq[i]]); printf("  write(LBA %d) -> page %d\n", seq[i], p); }
    printf("  map: 100->%d 101->%d 2000->%d 2001->%d ; block0 valid=%d (pages 0,1 are garbage)\n",
           f->l2p[100], f->l2p[101], f->l2p[2000], f->l2p[2001], f->valid[0]);
    collect_block(f, 0);
    printf("  after GC: 2000->%d 2001->%d ; block0 free again, verify errors = %d\n\n",
           f->l2p[2000], f->l2p[2001], ftl_verify(f));
    ftl_free(f);
}

static void run_random(int nblk, int ppb, double op, int skew, int static_wl, long writes_mult,
                       const char *label, int print_wear) {
    int phys = nblk * ppb;
    int nlpn = (int)(phys / (1.0 + op));       // OP = (phys - user) / user
    ftl_t *f = ftl_new(nblk, ppb, nlpn);
    f->static_wl = static_wl; f->wl_thresh = 20;
    for (int l = 0; l < nlpn; l++) ftl_write(f, l);       // 1회 순차 채우기 (preconditioning)
    long w0 = f->host_w, n0 = f->nand_w;
    long total = writes_mult * nlpn;
    for (long i = 0; i < total; i++) {
        int32_t lpn;
        if (skew) lpn = (rnd() % 100 < 80) ? (int32_t)(rnd() % (nlpn / 5)) : (int32_t)(nlpn / 5 + rnd() % (nlpn - nlpn / 5));
        else      lpn = (int32_t)(rnd() % nlpn);
        ftl_write(f, lpn);
    }
    double waf = (double)(f->nand_w - n0) / (f->host_w - w0);
    double u = f->gc_runs ? f->victim_valid_sum / f->gc_runs : 0;
    int mn, mx; double avg; erase_stats(f, &mn, &mx, &avg);
    // MLC 숫자(Figure I.2): read 50us, program 600us, erase 3000us. 한 die 직렬 가정
    double us_per_host_w = (f->nand_w * 600.0 + f->nand_r * 50.0 + f->erases * 3000.0) / f->host_w;
    printf("  %-26s OP=%5.1f%%  WAF=%5.2f  victim valid u=%.2f  1/(1-u)=%5.2f  ~%5.0f us/host-write",
           label, op * 100, waf, u, u < 1 ? 1 / (1 - u) : 0.0, us_per_host_w);
    if (print_wear) printf("\n      erase count min/avg/max = %d / %.1f / %d   static-WL moves = %ld",
                           mn, avg, mx, f->wl_moves);
    printf("   verify=%s\n", ftl_verify(f) ? "FAIL" : "ok");
    ftl_free(f);
}

static void exp_recovery(void) {
    printf("[4] power loss: throw away L2P, rebuild from OOB scan (latest seq wins)\n");
    int nblk = 64, ppb = 32; int nlpn = (int)(nblk * ppb / 1.25);
    ftl_t *f = ftl_new(nblk, ppb, nlpn);
    for (long i = 0; i < 5L * nlpn; i++) ftl_write(f, (int32_t)(rnd() % nlpn));
    int trimmed = 0;
    for (int l = 0; l < nlpn; l += 37) if (f->ver[l]) { ftl_trim(f, l); trimmed++; }
    int32_t *rebuilt = malloc(sizeof(int32_t) * nlpn); uint32_t *best = calloc(nlpn, sizeof(uint32_t));
    for (int l = 0; l < nlpn; l++) rebuilt[l] = NONE;
    long scanned = 0;
    for (int p = 0; p < nblk * ppb; p++) {
        page_t *pg = &f->pg[p];
        if (pg->state != PG_PROGRAMMED || pg->oob_lpn == NONE) continue;
        scanned++;
        if (pg->oob_seq > best[pg->oob_lpn]) { best[pg->oob_lpn] = pg->oob_seq; rebuilt[pg->oob_lpn] = p; }
    }
    int diff = 0, resurrected = 0;
    for (int l = 0; l < nlpn; l++) {
        if (rebuilt[l] != f->l2p[l]) { diff++; if (f->l2p[l] == NONE) resurrected++; }
    }
    printf("  scanned %ld programmed pages' OOB, L2P entries differing = %d, of which TRIMmed-but-resurrected = %d (trimmed %d)\n",
           scanned, diff, resurrected, trimmed);
    printf("  -> 덮어쓴 데이터는 seq 로 정확히 복구되지만, TRIM 은 NAND 에 흔적이 없어서 되살아난다.\n");
    printf("     실제 FW 는 TRIM/unmap 도 journal(또는 L2P checkpoint+delta log)에 남긴다.\n\n");
    free(rebuilt); free(best); ftl_free(f);
}

int main(void) {
    exp_book_example();

    printf("[2] uniform random 4KB overwrite, 256 blocks x 64 pages, 8x capacity after fill\n");
    double ops[] = { 0.07, 0.125, 0.28, 0.50, 1.00 };
    for (int i = 0; i < 5; i++) run_random(256, 64, ops[i], 0, 0, 8, "uniform", 0);

    printf("\n[3] 80/20 hot/cold skew, OP=12.5%%, 256 x 64, 30x capacity\n");
    run_random(256, 64, 0.125, 1, 0, 30, "80/20, dynamic WL only", 1);
    run_random(256, 64, 0.125, 1, 1, 30, "80/20, + static WL(th=20)", 1);
    run_random(256, 64, 0.125, 0, 0, 30, "uniform (reference)", 1);
    printf("\n");

    exp_recovery();

    printf("[5] mapping table size (4-byte entries)\n");
    double cap = 1024.0 * 1024 * 1024 * 1024;   // 1 TiB
    printf("  page-level  (4KB) : %6.0f MiB\n", cap / 4096 * 4 / (1 << 20));
    printf("  block-level (256KB): %6.0f MiB\n", cap / (256 * 1024) * 4 / (1 << 20));
    printf("  4TB SSD page-level: %6.0f MiB  -> \"DRAM 1GB per 1TB\" 경험칙\n", 4 * cap / 4096 * 4 / (1 << 20));
    return 0;
}
