/*
 * C43_lfs_cleaner.c — 장난감 LFS: 로그 append + imap + segment summary + cleaner
 *
 *  - 모든 쓰기는 "현재 세그먼트"의 다음 빈 칸에 append (덮어쓰기 없음)
 *  - 파일 하나 = 데이터 블록 1개 + inode 1개. 갱신 시 둘 다 새 위치에 append
 *  - imap[inum] = 최신 inode 주소, inode 내용(ptr) = 최신 데이터 블록 주소
 *  - segment summary(ss[addr]) = 이 블록이 (어떤 종류, 몇 번 inode) 였는지
 *  - liveness: 원문 의사코드 그대로  (N,T)=SS[A]; if inode[N][T]==A → live
 *  - free 세그먼트가 부족하면 cleaner 가 victim 세그먼트의 live 블록만 옮기고 해제
 *  - 정책 비교: greedy(가장 비어 있는 세그먼트) vs cost-benefit (Rosenblum: (1-u)*age/(1+u))
 *  - 워크로드: uniform vs hot/cold(쓰기 90% 가 파일 10% 에 몰림)
 *  - 지표: write amplification = (사용자 쓰기 + cleaner 복사) / 사용자 쓰기
 *
 * build: cc -Wall -Wextra -O0 code/C43_lfs_cleaner.c -o .work/bin/C43_lfs_cleaner
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NSEG 64
#define SEGB 16
#define NB (NSEG * SEGB)
#define MAXF 512

enum { T_NONE = 0, T_DATA = 1, T_INODE = 2 };
typedef struct { int type, inum; } ss_t;

static ss_t ss[NB];
static int imap[MAXF], dptr[MAXF], seg_free[NSEG], seg_time[NSEG];
static int head_seg[2], head_off[2], nfiles, now, segregate;
#define cur_seg head_seg[0]
static long user_w, clean_w, cleaned_segs, thrash;
static double cleaned_u_sum;

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return (rng >> 8) & 0xffffff; }

static void reset(int nf) {
    memset(ss, 0, sizeof ss);
    for (int s = 0; s < NSEG; s++) { seg_free[s] = 1; seg_time[s] = 0; }
    for (int i = 0; i < MAXF; i++) imap[i] = dptr[i] = -1;
    head_seg[0] = head_seg[1] = -1; head_off[0] = head_off[1] = SEGB; nfiles = nf; now = 0;
    user_w = clean_w = cleaned_segs = thrash = 0; cleaned_u_sum = 0;
}

static int nfree(void) { int n = 0; for (int s = 0; s < NSEG; s++) n += seg_free[s]; return n; }

/* h=0: 사용자 쓰기 로그 헤드, h=1: cleaner 전용 헤드(segregate=1 일 때만) */
static int alloc_h(int h) {
    if (!segregate) h = 0;
    if (head_off[h] == SEGB) {
        int s = 0;
        while (s < NSEG && !seg_free[s]) s++;
        if (s == NSEG) { fprintf(stderr, "disk full\n"); return -1; }
        seg_free[s] = 0; head_seg[h] = s; head_off[h] = 0;
        seg_time[s] = now;
    }
    if (h == 0) seg_time[head_seg[h]] = now; /* cleaner 가 옮긴 블록은 나이를 유지 */
    return head_seg[h] * SEGB + head_off[h]++;
}
static int alloc_blk(void) { return alloc_h(0); }

static int is_live(int a) {
    if (ss[a].type == T_DATA) return dptr[ss[a].inum] == a;   /* inode[N][T] == A ? */
    if (ss[a].type == T_INODE) return imap[ss[a].inum] == a;  /* imap[N] == A ? */
    return 0;
}

static void put(int a, int type, int inum) { ss[a].type = type; ss[a].inum = inum; }

static void write_file(int inum) {
    int a = alloc_blk(); put(a, T_DATA, inum); dptr[inum] = a;
    int b = alloc_blk(); put(b, T_INODE, inum); imap[inum] = b;
    user_w += 2;
}

static int live_in(int s) { int n = 0; for (int i = 0; i < SEGB; i++) n += is_live(s * SEGB + i); return n; }

static int clean_one(int costbenefit) {
    int best = -1;
    double bestv = 0;
    for (int s = 0; s < NSEG; s++) {
        if (seg_free[s] || s == head_seg[0] || s == head_seg[1]) continue;
        int l = live_in(s);
        if (l == SEGB) continue;
        double u = (double)l / SEGB, v;
        if (costbenefit) v = (1 - u) * (double)(now - seg_time[s] + 1) / (1 + u);
        else v = 1 - u;
        if (best < 0 || v > bestv) { best = s; bestv = v; }
    }
    if (best < 0) return 0;
    cleaned_u_sum += (double)live_in(best) / SEGB;
    cleaned_segs++;
    int dirty[SEGB], nd = 0;
    for (int i = 0; i < SEGB; i++) {
        int a = best * SEGB + i;
        if (!is_live(a)) continue;
        int inum = ss[a].inum;
        if (ss[a].type == T_DATA) { /* 데이터 복사 → inode 도 바뀌어야 함 */
            int na = alloc_h(1); put(na, T_DATA, inum); dptr[inum] = na; clean_w++;
        }
        int seen = 0;
        for (int k = 0; k < nd; k++) if (dirty[k] == inum) seen = 1;
        if (!seen) dirty[nd++] = inum;
    }
    for (int k = 0; k < nd; k++) {
        int inum = dirty[k];
        int nb = alloc_h(1); put(nb, T_INODE, inum); imap[inum] = nb; clean_w++;
    }
    for (int i = 0; i < SEGB; i++) put(best * SEGB + i, T_NONE, -1);
    seg_free[best] = 1;
    return 1;
}

static void run(const char *wname, int hotcold, int cb, int seg, int nf, long nupd) {
    reset(nf);
    segregate = seg;
    for (int i = 0; i < nf; i++) write_file(i); /* 초기 채우기 */
    user_w = clean_w = 0;
    for (long t = 0; t < nupd; t++) {
        now++;
        /* 64번 청소해도 free 가 안 늘면 "thrash" 로 세고 그냥 진행 (빈 세그먼트 3개 여유) */
        int guard = 0;
        while (nfree() <= 3) {
            if (guard++ == 64) { thrash++; break; }
            if (!clean_one(cb)) { printf("  (cleaner stuck at t=%ld)\n", t); return; }
        }
        int f;
        if (hotcold && rnd() % 100 < 90) f = (int)(rnd() % (uint32_t)(nf / 10));
        else f = (int)(rnd() % (uint32_t)nf);
        write_file(f);
    }
    printf("%-9s %-13s %-9s util=%4.1f%%  WA=%5.2f  cleaned=%6ld, victim u=%.2f, thrash=%ld\n", wname,
           cb ? "cost-benefit" : "greedy", seg ? "2 heads" : "1 head", 100.0 * nf * 2 / NB, (double)(user_w + clean_w) / user_w,
           cleaned_segs, cleaned_segs ? cleaned_u_sum / cleaned_segs : 0, thrash);
}

static void dump(int nblk) {
    for (int a = 0; a < nblk; a++) {
        if (ss[a].type == T_NONE) continue;
        printf("  A%-3d SS=(%s, inum %d)  -> %s\n", a, ss[a].type == T_DATA ? "data " : "inode",
               ss[a].inum, is_live(a) ? "LIVE" : "dead (garbage)");
    }
}

int main(void) {
    printf("== 1. tiny trace: write f0, f1, overwrite f0, overwrite f0 ==\n");
    reset(4);
    write_file(0); write_file(1); write_file(0); write_file(0);
    dump(16);
    printf("  imap: f0 -> A%d, f1 -> A%d  (segment 0 live = %d/16)\n", imap[0], imap[1], live_in(0));

    printf("\n== 2. how much to buffer: D = F/(1-F) * Rpeak * Tpos (Rpeak=100MB/s, T=10ms) ==\n");
    double Fs[] = { 0.5, 0.9, 0.95, 0.99 };
    for (int i = 0; i < 4; i++) printf("  F=%2.0f%% -> D = %6.1f MB\n", Fs[i] * 100, Fs[i] / (1 - Fs[i]) * 100 * 0.01);

    printf("\n== 3. cleaner policies (64 segs x 16 blks, 200k file updates) ==\n");
    int nfs[] = { 256, 384 };
    for (int k = 0; k < 2; k++) {
        rng = 12345; run("uniform", 0, 0, 0, nfs[k], 200000);
        rng = 12345; run("uniform", 0, 1, 0, nfs[k], 200000);
        rng = 12345; run("hot/cold", 1, 0, 0, nfs[k], 200000);
        rng = 12345; run("hot/cold", 1, 1, 0, nfs[k], 200000);
        rng = 12345; run("hot/cold", 1, 0, 1, nfs[k], 200000);
        rng = 12345; run("hot/cold", 1, 1, 1, nfs[k], 200000);
    }
    return 0;
}
