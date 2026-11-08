/* SIV-B06 demo — 결함 × 테스트 매트릭스 (검출 개수 실측)
 *
 * 같은 디렉터리의 solution.c 를 그대로 가져와(main 만 이름을 바꿔) 각 결함에 대해 다섯 테스트의
 * 불일치/의심 개수를 찍는다. 비교용으로 SIV-B04 식 "레지스터 하나씩 즉시 write→read" 도 넣었다.
 *   cc -std=c11 -Wall -Wextra -O2 demo_fault_matrix.c -o demo_fault_matrix && ./demo_fault_matrix
 */
#define main solution_main
#include "solution.c"
#undef main

/* B04 식: 레지스터마다 PAT 쓰고 바로 읽고, ANTI 쓰고 바로 읽기 (aliasing 을 노린 테스트가 아님) */
static int rw_each_test(void) {
    int fails = 0;
    for (size_t i = 0; i < NREGS; i++) {
        if (regs[i].acc != AT_RW) continue;
        reg_write32(regs[i].off, PAT);
        fails += ((reg_read32(regs[i].off) ^ PAT) & regs[i].mask) != 0;
        reg_write32(regs[i].off, ANTI);
        fails += ((reg_read32(regs[i].off) ^ ANTI) & regs[i].mask) != 0;
    }
    return fails;
}

int main(void) {
    struct { const char *name; fault_t f; } sc[] = {
        {"good",                      {-1, -1, -1}},
        {"A[6] ignored",              { 6, -1, -1}},
        {"A[2] ignored",              { 2, -1, -1}},
        {"entry CH1_CCTRL->CH0_CCTRL",{-1, 0x0CC, 0x08C}},
        {"entry CH1_SRC->CH0_SRC",    {-1, 0x0C0, 0x080}},
        {"A[9] not decoded (mirror)", { 9, -1, -1}},
    };
    printf("%-28s %8s %8s %8s %8s %8s\n", "fault", "rw_each", "unique", "idxbits", "walk", "hole");
    for (size_t s = 0; s < sizeof sc / sizeof sc[0]; s++) {
        fail_log_t lg; int r[4]; uint32_t w;
        mock_reset(&sc[s].f); r[0] = rw_each_test();
        mock_reset(&sc[s].f); lg.n = 0; r[1] = alias_unique_test(&lg);
        mock_reset(&sc[s].f); lg.n = 0; r[2] = alias_indexbits_test(&lg);
        mock_reset(&sc[s].f); w = walk_addr_test();
        mock_reset(&sc[s].f); lg.n = 0; r[3] = hole_test(&lg);
        printf("%-28s %8d %8d %8d    0x%03X %8d\n", sc[s].name, r[0], r[1], r[2], w, r[3]);
    }
    return 0;
}
