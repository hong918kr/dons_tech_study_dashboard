/* OS-A07 데모 — create+write 워크로드를 N 마다 끊어 본 표 (모드 none / ordered / writeback)
 * 각 N 에서: 잃어버린 첫 쓰기(#N), 복구가 replay 한 트랜잭션 수, fsck 발견, 상태 판정
 *   s0 = op 전, s1 = create 뒤, s2 = write 뒤, "META!" = 메타데이터가 어느 상태와도 다름, "DATA!" = 내용이 틀림
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_crash_table.c -o demo && ./demo   (solution.c 와 같은 폴더)
 */
#define A07_NO_MAIN
#include "solution.c"

static void one_mode(int mode) {
    J.mode = mode; J.barrier = 1; J.csum = 1; J.lazy = 0; D.wc_on = 0;
    const workload_t *w = &WL[0];
    long W = clean_run(w);
    /* 기준 실행의 쓰기 순서 (레이블) */
    restore_and_mount();
    run_ops(w->ops, w->nops);
    char trace[2048]; snprintf(trace, sizeof trace, "%s", D.tbuf);
    printf("== mode %s: W=%ld\n   writes: %s\n", MODE_NAME[mode], W, trace);
    for (long N = 0; N <= W; N++) {
        char lost[16] = "-";
        if (N < W) {                              /* trace 에서 N 번째 "W x" 찾기 (F 는 건너뜀) */
            long k = -1; const char *p = trace;
            while (*p) {
                if (p[0] == 'W' && p[1] == ' ') { k++; if (k == N) { sscanf(p + 2, "%15s", lost); break; } }
                while (*p && *p != ' ') p++;
                while (*p == ' ') p++;
            }
        }
        restore_and_mount(); D.crash_at = N;
        int j = run_ops(w->ops, w->nops); power_cut(); dev_reboot();
        long rep0 = J.replayed;
        fs_mount();
        fsck_t r; int e = fsck(&r);
        uint64_t m, f; int dg = digest(&m, &f);
        char st[16] = "?";
        if (dg) snprintf(st, sizeof st, "META!");
        else {
            int km = -1, kf = -1;
            for (int k = 0; k <= w->nops; k++) { if (ST_META[k] == m && km < 0) km = k; if (ST_FULL[k] == f && kf < 0) kf = k; }
            if (km < 0) snprintf(st, sizeof st, "META!");
            else if (kf < 0) snprintf(st, sizeof st, "DATA!");
            else snprintf(st, sizeof st, "s%d", kf);
        }
        printf("   N=%2ld lose=%-4s op=%d replay=%ld fsck=%d", N, lost, j > w->nops ? 0 : j, J.replayed - rep0, e);
        if (e) fsck_print(&r);
        printf("  -> %s\n", st);
    }
}

int main(void) {
    one_mode(M_NONE);
    one_mode(M_ORDERED);
    one_mode(M_WRITEBACK);
    return 0;
}
