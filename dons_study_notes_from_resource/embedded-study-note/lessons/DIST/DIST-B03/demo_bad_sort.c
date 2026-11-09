/* DIST-B03 demo: vc_compare 를 qsort 비교 함수로 쓰면 왜 틀리나
 * 동시(concurrent)를 0 으로 돌려주면 "0 = 같다" 가 전이적이지 않아 strict weak order 가 깨진다.
 * C11 7.22.5 는 비교 함수에 일관된 전순서를 요구한다. 어긋나면 결과는 보장되지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bad_sort.c -o d_bad && ./d_bad
 */
#define main solution_main
#include "solution.c"
#undef main

static int cmp_vc_bad(const void *pa, const void *pb) {
    const event_t *a = *(event_t *const *)pa, *b = *(event_t *const *)pb;
    vc_order_t o = vc_compare(&a->vc, &b->vc);
    return o == VC_BEFORE ? -1 : o == VC_AFTER ? 1 : 0;   /* 잘못: 동시 = 같다 */
}

int main(void) {
    event_t a, b, c;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&c, 0, sizeof c);
    a.vc.c[0] = 1; a.label = "a[1,0,0]";          /* a → c */
    c.vc.c[0] = 2; c.label = "c[2,0,0]";
    b.vc.c[1] = 1; b.label = "b[0,1,0]";          /* b 는 둘 다와 동시 */
    event_t *in[3] = { &a, &b, &c };
    int perm[3] = { 0, 1, 2 };
    puts("3개 이벤트, 입력 순서 6가지 (a->c, b 는 동시):");
    do {
        event_t *o[3] = { in[perm[0]], in[perm[1]], in[perm[2]] };
        printf("  in: %s %s %s", o[0]->label, o[1]->label, o[2]->label);
        qsort(o, 3, sizeof o[0], cmp_vc_bad);
        printf("  -> out: %s %s %s  inversions=%d\n", o[0]->label, o[1]->label, o[2]->label,
               count_causal_inversions(o, 3));
    } while (next_perm(perm, 3));

    static const int32_t no_skew[N_ECU] = { 0, 0, 0 };
    long inv_bad = 0, inv_sum = 0;
    int seeds_bad = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        ecu_log_t logs[N_ECU];
        gen_random_logs(seed * 2654435761u, logs, no_skew);
        stamp_logs(logs);
        event_t *o[MAX_EV];
        int n = flatten(logs, o);
        /* 입력을 ECU별 이어 붙인 순서가 아니라 seed 로 섞어 둔다 */
        uint32_t s = seed;
        for (int i = n - 1; i > 0; i--) {
            int j = (int)(xs32(&s) % (uint32_t)(i + 1));
            event_t *t = o[i]; o[i] = o[j]; o[j] = t;
        }
        event_t *o2[MAX_EV];
        memcpy(o2, o, sizeof(event_t *) * (size_t)n);
        qsort(o, (size_t)n, sizeof o[0], cmp_vc_bad);
        qsort(o2, (size_t)n, sizeof o2[0], merge_cmp);
        int bad = count_causal_inversions(o, n);
        inv_bad += bad;
        if (bad) seeds_bad++;
        inv_sum += count_causal_inversions(o2, n);
    }
    printf("무작위 실행 300개: vc_compare 비교함수 inversions=%ld (깨진 실행 %d개), merge_cmp inversions=%ld\n",
           inv_bad, seeds_bad, inv_sum);
    return 0;
}
