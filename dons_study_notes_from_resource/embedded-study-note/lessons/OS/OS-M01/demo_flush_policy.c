/* OS-M01 데모: 무효화 정책 비교 — VA 별 무효화 vs ASID 전체 무효화 (ceiling = 0 / 33 / 64)
 *   TLB 64 엔트리를 a(ASID 1) 48 페이지 + b(ASID 2) 16 페이지로 꽉 채운 뒤, a 의 앞쪽 k 페이지를 unmap 한다.
 *   그다음 a 의 남은 페이지와 b 의 페이지를 다시 만지며 "다시 채우느라 생긴 TLB 미스 / 디스크립터 읽기" 를 센다.
 *   결정적 시뮬레이션이라 실행마다 같은 숫자가 나온다 (시간 측정 아님).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_flush_policy.c -o demo && ./demo   (solution.c 와 같은 폴더에서)
 */
#define OSM01_NO_MAIN
#include "solution.c"

int main(void) {
    (void)protect_range; (void)migrate_page; (void)as_destroy; (void)tlb_audit; (void)live_errors;
    (void)count_state; (void)wc_count; (void)wc_table; (void)tlb_count; (void)log_count; (void)state_of;
    static const unsigned CEIL[3] = {0, 33, 64};
    static const int K[7] = {1, 8, 32, 33, 34, 47, 48};
    const uint64_t RWU = D_R | D_W | D_U;
    puts("TLB = 64 entries: a(ASID 1) 48 pages + b(ASID 2) 16 pages, then unmap the first k pages of a");
    puts("ceiling   k  tlbi ops (kind)      a refill misses  b refill misses  desc reads  audit");
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < 7; i++) {
            machine_reset();
            set_ceiling(CEIL[c]);
            as_t a, b;
            (void)as_create(&a, 1); (void)as_create(&b, 2);
            (void)map_range(&a, 0x400000, PA_ANON, 48 * PAGE_SIZE, RWU, 0);
            (void)map_range(&b, 0x10000000, PA_ANON, 16 * PAGE_SIZE, RWU, 0);
            touch(&a, 0x400000, 48, ACC_R | ACC_USER);
            touch(&b, 0x10000000, 16, ACC_R | ACC_USER);
            log_reset();
            const int k = K[i];
            (void)unmap_range(&a, 0x400000, (uint64_t)k * PAGE_SIZE);
            const int ops = M.nlog;
            const tlbi_kind_t kind = ops ? M.log[0].kind : TLBI_VA_LEAF;
            uint64_t m0 = M.tlb_misses, r0 = M.desc_reads;
            touch(&a, 0x400000 + (uint64_t)k * PAGE_SIZE, 48 - k, ACC_R | ACC_USER);
            uint64_t ma = M.tlb_misses - m0;
            m0 = M.tlb_misses;
            touch(&b, 0x10000000, 16, ACC_R | ACC_USER);
            uint64_t mb = M.tlb_misses - m0, reads = M.desc_reads - r0;
            printf("%7u  %2d  %3d (%-7s)        %15llu  %15llu  %10llu  %5d\n", CEIL[c], k, ops, TLBI_NAME[kind],
                   (unsigned long long)ma, (unsigned long long)mb, (unsigned long long)reads, tlb_audit());
        }
    }
    return 0;
}
