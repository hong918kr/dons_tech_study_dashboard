/* ARM-A08 데모 — 스윕 두 개 (사이클, 모델 값 — 측정 아님).
 * solution.c 의 해석·시뮬레이터를 그대로 쓴다 (같은 레슨 안의 #include).
 *   (1) PRIMASK 섹션 길이 → 전류 루프 R (해석 상한 vs 시뮬 탐색 최대): 직선, 데드라인과 만나는 점 = 예산
 *   (2) CAN ISR 길이 → 엔코더 R: 해석은 계단(ceil), 시뮬은 그 계단을 다 밟지 않는다 = 상한의 비관성
 * 빌드 (레슨 디렉터리에서): cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/ARM-A08_demo && /tmp/ARM-A08_demo
 */
#define main solution_main
#include "solution.c"
#undef main

int main(void)
{
    variant_t v[12];
    int nv = motor_variants(v);
    (void)nv;
    const variant_t *v5 = &v[5];                       /* CAN 을 0x60 으로 내린 설계: CUR 앞에 CAN 없음 */
    printf("== (1) %s: LOG PRIMASK length -> CUR response (D = 4200)\n", v5->name);
    printf("   len  | R bound | R sim (crit+600 phasings) | verdict\n");
    for (uint32_t len = 0; len <= 4000; len += 400) {
        sys_t s = v5->s; s.sec[0].len = len;
        uint32_t R = 0; search_t sr;
        response_bound(&v5->p, &s, M_CUR, &R);
        g_rng = 0xD1ull + len;
        sim_search(&v5->p, &s, M_CUR, 600, 40000, R + 64, &sr);
        printf("   %4u | %7u | %5u                  | %s\n", (unsigned)len, (unsigned)R, (unsigned)sr.resp, R <= 4200 ? "meets" : "MISSES");
    }
    int64_t bud;
    { sys_t s = v5->s; s.nsec = 1; s.sec[0] = v5->s.sec[1];  /* LOG 빼고 KERN 만 남긴 뒤 PRIMASK 예산 */
      bud = section_budget(&v5->p, &s, SEC_PRIMASK, 0); }
    printf("   section_budget(PRIMASK) without LOG = %lld cycles\n", (long long)bud);

    const variant_t *v0 = &v[0];
    printf("== (2) %s: CAN ISR C -> ENC response (CAN T = 7896, ENC D = 16800)\n", v0->name);
    printf("   C_can | R bound | R sim (crit+600) | bound - sim\n");
    for (uint32_t c = 2400; c <= 2700; c += 30) {
        sys_t s = v0->s; s.irq[M_CAN].C = c;
        uint32_t R = 0; search_t sr;
        if (response_bound(&v0->p, &s, M_ENC, &R)) { printf("   %5u | diverge\n", (unsigned)c); continue; }
        g_rng = 0xD2ull + c;
        sim_search(&v0->p, &s, M_ENC, 600, 40000, R + 64, &sr);
        printf("   %5u | %7u | %5u         | %5u\n", (unsigned)c, (unsigned)R, (unsigned)sr.resp, (unsigned)(R - sr.resp));
    }
    return 0;
}
