/* OS-B11 demo — OSTEP paging-policy.py 출력과 내 시뮬레이터(solution.c)를 한 줄씩 대조한다.
 *
 *   python3 -I paging-policy.py -s 3 -n 40 -m 10 -C 4 -p LRU -c | ./demo_ostep
 *
 * stdin 에서 "ARG policy", "ARG cachesize", "Access: <p>  HIT|MISS ... Replaced:<v|->" 줄을 읽고,
 * 같은 참조 문자열을 solution.c 의 sim_access() 로 돌려 참조마다 비교한다.
 *   - HIT/MISS: 모든 정책에서 같아야 한다
 *   - 희생자: FIFO/LRU 는 같아야 한다. OPT 는 "둘 다 다시는 안 쓰이는 페이지" 일 때만 달라도 된다
 *     (동점 처리: OSTEP = 가장 최근에 적재된 쪽, 나 = 마지막 참조가 오래된 쪽). 그 경우 tie 로 센다.
 *   - CLOCK 은 대조하지 않고 hit 수만 나란히 보인다 (OSTEP 의 CLOCK 은 바늘 대신 무작위로 프레임을 고르고,
 *     use 값이 0..clockbits 카운터다 — 22.8 그림 22.9 의 변형. 내 것은 바늘 + 1비트).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_ostep.c -o demo_ostep   (solution.c 와 같은 폴더에서)
 */
#define OSB11_NO_MAIN
#include "solution.c"

#define MAXREF 4096

int main(void) {
    static int refs[MAXREF], o_hit[MAXREF], o_vic[MAXREF];
    char line[512], pol[32] = "", seed[32] = "?", addr[16] = "";
    int cache = 0, n = 0;
    long o_hits = -1, o_miss = -1;
    while (fgets(line, sizeof line, stdin)) {
        int p;
        char hm[8];
        if (sscanf(line, "ARG policy %31s", pol) == 1) continue;
        if (sscanf(line, "ARG seed %31s", seed) == 1) continue;
        if (sscanf(line, "ARG addresses %15s", addr) == 1) continue;
        if (sscanf(line, "ARG cachesize %d", &cache) == 1) continue;
        if (sscanf(line, "FINALSTATS hits %ld misses %ld", &o_hits, &o_miss) == 2) continue;
        if (sscanf(line, "Access: %d %7s", &p, hm) == 2 && n < MAXREF) {
            const char *r = strstr(line, "Replaced:");
            if (!r || p < 0 || p >= MAX_PAGE) continue;          /* -c 없이 돌린 출력이면 건너뜀 */
            refs[n] = p;
            o_hit[n] = strcmp(hm, "HIT") == 0;
            o_vic[n] = r[9] == '-' ? -1 : atoi(r + 9);
            n++;
        }
    }
    policy_t P;
    if      (!strcmp(pol, "FIFO"))  P = POL_FIFO;
    else if (!strcmp(pol, "LRU"))   P = POL_LRU;
    else if (!strcmp(pol, "OPT"))   P = POL_OPT;
    else if (!strcmp(pol, "CLOCK")) P = POL_CLOCK;
    else { fprintf(stderr, "unsupported policy '%s'\n", pol); return 2; }
    if (n == 0 || cache < 1 || cache > MAX_FRAMES) { fprintf(stderr, "no trace (forgot -c?)\n"); return 2; }

    static long next[MAXREF];
    build_next(refs, n, next);
    sim_t s;
    sim_init(&s, P, cache, refs, next, n);
    long hits = 0, hm_diff = 0, v_diff = 0, ties = 0;
    for (int t = 0; t < n; t++) {
        acc_t a = sim_access(&s);
        hits += a.hit;
        if (a.hit != (bool)o_hit[t]) hm_diff++;
        if (a.evicted != o_vic[t]) {
            bool dead_a = true, dead_o = true;                   /* t 이후 다시 나오나? */
            for (int u = t + 1; u < n; u++) {
                if (refs[u] == a.evicted) dead_a = false;
                if (refs[u] == o_vic[t])  dead_o = false;
            }
            if (P == POL_OPT && dead_a && dead_o) ties++; else v_diff++;
        }
    }
    if (strcmp(addr, "-1") != 0) strcpy(seed, "-a");            /* -a 로 문자열을 준 경우 */
    printf("%-5s s=%-2s C=%d n=%d  hits mine=%ld ostep=%ld", pol, seed, cache, n, hits, o_hits);
    if (P == POL_CLOCK) { printf("  (CLOCK: not compared — OSTEP picks frames at random)\n"); return 0; }
    printf("  hit/miss diff=%ld victim diff=%ld (OPT dead-page ties=%ld)  %s\n", hm_diff, v_diff, ties,
           (hm_diff == 0 && v_diff == 0 && hits == o_hits) ? "MATCH" : "MISMATCH");
    return (hm_diff == 0 && v_diff == 0 && hits == o_hits) ? 0 : 1;
}
