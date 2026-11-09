/* DIST-B03 demo: ECU 벽시계 스큐를 키우면 "시각 정렬" 로그가 얼마나 거짓말을 하나
 * ECU 0 은 기준, ECU 1 은 +skew, ECU 2 는 -skew. 이벤트 사이 진짜 간격은 1~4 ms (seed 고정).
 * 시간 측정 없음: 결과는 seed 에 의해 결정되므로 부하와 무관하다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_skew_sweep.c -o d_skew && ./d_skew
 */
#define main solution_main
#include "solution.c"
#undef main

int main(void) {
    static const int skews[] = { 0, 1, 2, 5, 10, 20, 50 };
    printf("%-9s %-13s %-11s %-7s %-11s %-9s\n", "skew(ms)", "causal_pairs", "wall_inv", "wall%", "lamport_inv", "vc_inv");
    for (size_t k = 0; k < sizeof skews / sizeof skews[0]; k++) {
        int32_t skew[N_ECU] = { 0, skews[k], -skews[k] };
        long causal = 0, inv_wall = 0, inv_lam = 0, inv_vc = 0;
        for (uint32_t seed = 1; seed <= 300; seed++) {
            ecu_log_t logs[N_ECU];
            gen_random_logs(seed * 2654435761u, logs, skew);
            stamp_logs(logs);
            event_t *o[MAX_EV];
            int n = flatten(logs, o);
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++)
                    if (i != j && vc_compare(&o[i]->vc, &o[j]->vc) == VC_BEFORE) causal++;
            qsort(o, (size_t)n, sizeof o[0], cmp_wall);
            inv_wall += count_causal_inversions(o, n);
            qsort(o, (size_t)n, sizeof o[0], cmp_lamport);
            inv_lam += count_causal_inversions(o, n);
            event_t *m[MAX_EV];
            int cnt = merge_logs(logs, m, MAX_EV);
            inv_vc += count_causal_inversions(m, cnt);
        }
        printf("%-9d %-13ld %-11ld %-7.2f %-11ld %-9ld\n", skews[k], causal, inv_wall,
               100.0 * (double)inv_wall / (double)causal, inv_lam, inv_vc);
    }
    return 0;
}
