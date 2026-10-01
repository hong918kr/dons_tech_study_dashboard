/* C09_lottery_stride.c — 비례 배분 스케줄러 두 가지를 직접 구현.
 *  Part 1: 로또(lottery) — Figure 9.1 의 리스트 순회 코드 그대로. 75:25 가 시간이 갈수록 맞아 가는지.
 *  Part 2: Figure 9.2 재현 — 같은 티켓/같은 길이 R 인 두 job 의 불공정도 U (30회 평균)
 *  Part 3: 보폭(stride) — Figure 9.3 트레이스 재현 (A=100, B=50, C=250 tickets)
 *  Part 4: stride 에 새 job 이 중간에 들어올 때 pass=0 으로 넣으면 생기는 독점 문제 */
#include <stdio.h>
#include <stdint.h>

/* 재현 가능한 작은 PRNG (xorshift32) */
static uint32_t rng = 2463534242u;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

typedef struct node { char name; int tickets; int runs; struct node *next; } node_t;

/* Figure 9.1: counter 가 winner 를 넘는 순간의 노드가 당첨 */
static node_t *lottery_pick(node_t *head, int totaltickets) {
    int counter = 0;
    int winner = (int)(xr() % (uint32_t)totaltickets);   /* 0 .. total-1 */
    node_t *current = head;
    while (current) {
        counter += current->tickets;
        if (counter > winner) break;
        current = current->next;
    }
    return current;
}

/* 두 job(티켓 100씩, 길이 R)이 경쟁할 때 U = 먼저 끝난 시각 / 나중 끝난 시각 */
static double unfairness(int R) {
    node_t b = { 'B', 100, 0, NULL }, a = { 'A', 100, 0, &b };
    int t = 0, first_done = -1;
    while (a.runs < R || b.runs < R) {
        node_t *head = (a.runs < R) ? &a : &b;          /* 끝난 job 은 리스트에서 뺀다 */
        if (a.runs < R && b.runs < R) a.next = &b; else head->next = NULL;
        int total = (a.runs < R ? 100 : 0) + (b.runs < R ? 100 : 0);
        node_t *w = lottery_pick(head, total);
        w->runs++; t++;
        if (w->runs == R && first_done < 0) first_done = t;
    }
    return (double)first_done / t;
}

int main(void) {
    /* ---------- Part 1 ---------- */
    puts("== Part 1: lottery A=75, B=25 tickets ==");
    node_t B = { 'B', 25, 0, NULL }, A = { 'A', 75, 0, &B };
    char first20[21];
    int checkpoints[] = { 20, 100, 1000, 100000 }, ci = 0;
    for (int slice = 1; slice <= 100000; slice++) {
        node_t *w = lottery_pick(&A, 100);
        w->runs++;
        if (slice <= 20) first20[slice - 1] = w->name;
        if (slice == checkpoints[ci]) {
            if (slice == 20) { first20[20] = '\0'; printf("  first 20 slices: %s\n", first20); }
            printf("  after %6d slices: A=%5.1f%%  B=%5.1f%%\n", slice,
                   100.0 * A.runs / slice, 100.0 * B.runs / slice);
            ci++;
        }
    }

    /* ---------- Part 2 ---------- */
    puts("\n== Part 2: unfairness U vs job length R (30 trials avg, Fig 9.2) ==");
    int Rs[] = { 1, 2, 5, 10, 20, 50, 100, 1000 };
    for (int k = 0; k < 8; k++) {
        double sum = 0;
        for (int trial = 0; trial < 30; trial++) sum += unfairness(Rs[k]);
        printf("  R=%5d  U=%.3f\n", Rs[k], sum / 30);
    }

    /* ---------- Part 3 ---------- */
    puts("\n== Part 3: stride trace (BIG=10000) — Fig 9.3 ==");
    const char nm[3] = { 'A', 'B', 'C' };
    int tix[3] = { 100, 50, 250 }, stride[3], pass[3] = { 0, 0, 0 }, cnt[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) stride[i] = 10000 / tix[i];
    printf("  stride: A=%d B=%d C=%d\n", stride[0], stride[1], stride[2]);
    printf("  pass(A) pass(B) pass(C)  runs\n");
    for (int step = 0; step < 8; step++) {
        int m = 0;
        for (int i = 1; i < 3; i++) if (pass[i] < pass[m]) m = i;   /* 동률이면 앞쪽 */
        printf("  %7d %7d %7d   %c\n", pass[0], pass[1], pass[2], nm[m]);
        pass[m] += stride[m]; cnt[m]++;
    }
    printf("  %7d %7d %7d   (cycle done: A=%d B=%d C=%d runs)\n",
           pass[0], pass[1], pass[2], cnt[0], cnt[1], cnt[2]);

    /* ---------- Part 4 ---------- */
    puts("\n== Part 4: new job D (100 tix) joins stride at t=1000 slices ==");
    for (int mode = 0; mode < 2; mode++) {
        int tk[4] = { 100, 50, 250, 100 }, st[4], ps[4] = { 0 }, run[4] = { 0 };
        for (int i = 0; i < 4; i++) st[i] = 10000 / tk[i];
        int dslices = 0, n = 3;
        for (int t = 0; t < 1200; t++) {
            if (t == 1000) {
                int minp = ps[0];
                for (int i = 1; i < 3; i++) if (ps[i] < minp) minp = ps[i];
                ps[3] = (mode == 0) ? 0 : minp;   /* 0 으로 넣기 vs 현재 최소 pass 로 넣기 */
                n = 4;
            }
            int m = 0;
            for (int i = 1; i < n; i++) if (ps[i] < ps[m]) m = i;
            ps[m] += st[m]; run[m]++;
            if (t >= 1000 && t < 1100 && m == 3) dslices++;
        }
        printf("  D.pass=%s : D got %3d of the first 100 slices after joining (fair share = 20)\n",
               mode == 0 ? "0      " : "min(pass)", dslices);
    }
    return 0;
}
