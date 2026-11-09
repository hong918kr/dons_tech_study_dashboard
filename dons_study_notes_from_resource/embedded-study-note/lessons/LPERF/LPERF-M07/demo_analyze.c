/* ENV: linux */
/* LPERF-M07 demo_analyze — run_trace.sh 의 출력(서버의 LOCK/TID 줄 + bpftrace 맵 줄)을 solution.c 의 Part A 함수로 합친다.
 *
 * usage: ./analyze <trace.txt>
 * build: gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread demo_analyze.c -o analyze
 */
#define LPERF_M07_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"
#include "solution.c"

#define MAXROWS 4096
static bt_row_t rows[MAXROWS];
static lock_reg_t regs[16];
static tid_role_t tids[64];
static char names[16][32], roles[64][32];

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("open"); return 2; }
    char line[512];
    int nr = 0, ng = 0, nt = 0;
    while (fgets(line, sizeof line, f)) {
        char nm[32]; uint64_t base; unsigned sz, cnt; int tid;
        if (sscanf(line, "LOCK %31s %" SCNu64 " %u %u", nm, &base, &sz, &cnt) == 4 && ng < 16) {
            strcpy(names[ng], nm);
            regs[ng] = (lock_reg_t){ names[ng], base, sz, cnt };
            ng++;
        } else if (sscanf(line, "TID %31s %d", nm, &tid) == 2 && nt < 64) {
            strcpy(roles[nt], nm);
            tids[nt] = (tid_role_t){ tid, roles[nt] };
            nt++;
        } else if (nr < MAXROWS && bt_parse_line(line, &rows[nr])) {
            nr++;
        }
    }
    fclose(f);
    tids[nt++] = (tid_role_t){ 0, "(owner=0)" };   /* 잠들 때 __owner 가 0: 이미 풀렸거나 새 주인이 아직 안 적음 */

    uint64_t total_ns = 0, total_cnt = 0;
    for (int i = 0; i < nr; i++) {
        if (!strcmp(rows[i].map, "wait_ns")) total_ns += rows[i].value;
        if (!strcmp(rows[i].map, "wait_cnt")) total_cnt += rows[i].value;
    }
    printf("rows=%d  total futex-wait = %.1f ms  in %" PRIu64 " sleeps\n", nr, (double)total_ns / 1e6, total_cnt);

    lock_rank_t rk[17];
    int n = rank_locks(rows, nr, regs, ng, rk, 17);
    printf("%-14s %12s %8s %12s\n", "lock", "wait_ms", "share", "top holder");
    for (int i = 0; i < n; i++) {
        unsigned sh = 0;
        const char *who = rk[i].reg >= 0 ? top_holder(rows, nr, regs, rk[i].reg, tids, nt, &sh) : NULL;
        char holder[48] = "-";
        if (who) snprintf(holder, sizeof holder, "%s (%u.%u %%)", who, sh / 10, sh % 10);
        printf("%-14s %12.1f %6u.%u%% %s\n", rk[i].reg >= 0 ? regs[rk[i].reg].name : "(unknown)",
               (double)rk[i].ns / 1e6, rk[i].share_permille / 10, rk[i].share_permille % 10, holder);
    }
    return 0;
}
