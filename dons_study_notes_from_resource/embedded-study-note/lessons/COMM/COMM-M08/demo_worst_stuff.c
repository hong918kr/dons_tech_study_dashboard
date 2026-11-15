/* COMM-M08 demo: CAN FD 프레임의 "정확한" 최악 동적 스터프 비트 수 (DP) vs 상한식 ⌊(L−1)/4⌋
 *
 * FD 는 CRC 필드가 고정 스터핑이라 동적 스터핑 구간(SOF..마지막 데이터 비트)에 CRC 가 없다.
 * 그 구간의 비트는 "고정"(RRS=0, IDE, FDF=1, res=0, DLC) 아니면 "자유"(ID, BRS, ESI, 데이터)라서,
 * 비트마다 (마지막 비트 값, 같은 값 연속 길이 1..5) 상태 DP 로 모든 프레임 중 최대 스터프 수를 정확히 구한다.
 * 확인: DLC 0, 1 (표준 ID) 은 실제 인코더로 전수 조사(2^13, 2^21 프레임)해서 DP 와 같은지 본다.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_worst_stuff.c -o demo_worst_stuff && ./demo_worst_stuff
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { FREE = 2 };
static const unsigned fd_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

/* 동적 구간 패턴: pat[i] = 0 / 1 / FREE. 반환 길이. pre = SOF..BRS 비트 수 (17 / 36). */
static int fd_pattern(int ide, int dlc, uint8_t *pat, int *pre) {
    int n = 0;
    pat[n++] = 0;                                         /* SOF */
    for (int i = 0; i < 11; i++) pat[n++] = FREE;         /* ID / ID-A */
    if (ide) {
        pat[n++] = 1; pat[n++] = 1;                       /* SRR IDE */
        for (int i = 0; i < 18; i++) pat[n++] = FREE;     /* ID-B */
        pat[n++] = 0; pat[n++] = 1; pat[n++] = 0;         /* RRS FDF res */
    } else {
        pat[n++] = 0; pat[n++] = 0; pat[n++] = 1; pat[n++] = 0;   /* RRS IDE FDF res */
    }
    pat[n++] = FREE;                                      /* BRS */
    *pre = n;
    pat[n++] = FREE;                                      /* ESI */
    for (int i = 3; i >= 0; i--) pat[n++] = (uint8_t)((dlc >> i) & 1);
    for (unsigned i = 0; i < 8 * fd_len[dlc]; i++) pat[n++] = FREE;
    return n;
}

/* DP: 최대 Σ weight. 스터프 비트가 u[i] 앞에 들어가면 weight = (i < pre ? wn : wd).
 * 마지막 비트 뒤 스터프는 없음 (FD: 고정 스터프 비트가 그 자리). count 모드: wn = wd = 1. */
static double dp_max(const uint8_t *pat, int n, int pre, double wn, double wd, int *s_nom, int *s_dat) {
    double best[2][6]; int bn[2][6], bd[2][6];
    for (int v = 0; v < 2; v++) for (int r = 0; r < 6; r++) { best[v][r] = -1; bn[v][r] = bd[v][r] = 0; }
    best[0][1] = 0;                                       /* SOF = 0 */
    for (int i = 1; i < n; i++) {
        double nb[2][6]; int nn[2][6], nd[2][6];
        for (int v = 0; v < 2; v++) for (int r = 0; r < 6; r++) { nb[v][r] = -1; nn[v][r] = nd[v][r] = 0; }
        for (int v = 0; v < 2; v++) for (int r = 1; r <= 5; r++) {
            if (best[v][r] < 0) continue;
            double base = best[v][r]; int a = bn[v][r], b = bd[v][r];
            int lv = v, lr = r;
            if (r == 5) {                                 /* 스터프 비트 */
                if (i < pre) { base += wn; a++; } else { base += wd; b++; }
                lv = !v; lr = 1;
            }
            for (int x = 0; x < 2; x++) {
                if (pat[i] != FREE && pat[i] != x) continue;
                int nv = x, nr = (x == lv) ? lr + 1 : 1;
                if (base > nb[nv][nr]) { nb[nv][nr] = base; nn[nv][nr] = a; nd[nv][nr] = b; }
            }
        }
        memcpy(best, nb, sizeof best); memcpy(bn, nn, sizeof bn); memcpy(bd, nd, sizeof bd);
    }
    double m = -1;
    for (int v = 0; v < 2; v++) for (int r = 1; r <= 5; r++)
        if (best[v][r] > m) { m = best[v][r]; *s_nom = bn[v][r]; *s_dat = bd[v][r]; }
    return m;
}

/* 실제 인코더와 같은 규칙으로 동적 스터프 비트 수만 센다 (마지막 비트 뒤 없음). */
static int count_stuff(const uint8_t *u, int n) {
    int s = 0, run = 0; uint8_t last = 2;
    for (int i = 0; i < n; i++) {
        if (run == 5) { s++; last = (uint8_t)!last; run = 1; }
        if (u[i] == last) run++; else { last = u[i]; run = 1; }
    }
    return s;
}

static int brute_max(int dlc) {   /* 표준 ID, 자유 비트 전수 조사 */
    uint8_t pat[600], u[600];
    int pre, n = fd_pattern(0, dlc, pat, &pre);
    int nfree = 0;
    for (int i = 0; i < n; i++) nfree += pat[i] == FREE;
    int best = 0;
    for (uint32_t x = 0; x < (1u << nfree); x++) {
        int k = 0;
        for (int i = 0; i < n; i++) u[i] = pat[i] == FREE ? (uint8_t)((x >> k++) & 1) : pat[i];
        int s = count_stuff(u, n);
        if (s > best) best = s;
    }
    return best;
}

int main(void) {
    uint8_t pat[600];
    int pre, sn, sd;
    printf("검증: DP vs 전수 조사 (FD 표준 ID)\n");
    for (int dlc = 0; dlc <= 1; dlc++) {
        int n = fd_pattern(0, dlc, pat, &pre);
        int dp = (int)dp_max(pat, n, pre, 1, 1, &sn, &sd);
        printf("  DLC %d: 동적 구간 %d비트, DP 최대 %d, 전수 최대 %d → %s\n", dlc, n, dp, brute_max(dlc),
               dp == brute_max(dlc) ? "일치" : "불일치!");
    }
    for (int ide = 0; ide < 2; ide++) {
        printf("\nFD %s ID: 동적 스터프 비트 최대값 (DP, 정확) vs 상한식\n", ide ? "확장" : "표준");
        printf("  len | 구간 L | DP 최대 | ⌊(L-1)/4⌋ | ⌊(L-2)/4⌋ | nominal 위상 최대 | 시간가중 최악(500k/2M) s_nom+s_dat\n");
        for (int dlc = 0; dlc < 16; dlc++) {
            int n = fd_pattern(ide, dlc, pat, &pre);
            int dp = (int)dp_max(pat, n, pre, 1, 1, &sn, &sd);
            dp_max(pat, n, pre, 1, 0, &sn, &sd);            /* nominal 쪽만 최대화 */
            int nom_max = sn;
            dp_max(pat, n, pre, 4, 1, &sn, &sd);            /* T_nom = 4·T_data (500k/2M) 가중 최대 */
            printf("  %3u | %4d   | %5d   | %6d    | %6d    | %8d          | %d + %d\n", fd_len[dlc], n, dp,
                   (n - 1) / 4, (n - 2) / 4, nom_max, sn, sd);
        }
    }
    return 0;
}
