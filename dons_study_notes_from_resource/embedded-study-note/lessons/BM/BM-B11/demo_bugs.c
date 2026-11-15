/* BM-B11 demo: 흔한 GPIO 드라이버 버그 5종을 같은 mock 위에서 돌려 피해를 센다.
 * solution.c 의 mock·드라이버를 그대로 가져온다 (main 이름만 바꿔서). 레슨 폴더에서 빌드:
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o /tmp/BM-B11_bugs && /tmp/BM-B11_bugs
 */
#define main solution_main
#include "solution.c"
#undef main

enum { OK_REF, BUG_NO_CLEAR, BUG_POS_NOT_2PIN, BUG_OVERWRITE, BUG_AFRL_ONLY, BUG_NO_CLOCK, NVAR };
static const char *VNAME[NVAR] = {
    "reference (field_put, 2*pin)", "MODER |= mode << 2*pin   (no clear)", "shift by pin, not 2*pin",
    "MODER = mode << 2*pin    (overwrite)", "AF always into AFRL", "no RCC clock enable" };

/* MODER(+AFR) 만 쓰는 축소판 init. 변형마다 버그 하나 */
static void var_init(int v, uint32_t port, unsigned pin, unsigned mode, unsigned af) {
    if (v != BUG_NO_CLOCK) gpio_clock_enable(port);
    if (mode == GPIO_MODE_AF) {
        uint32_t afr = port + ((v == BUG_AFRL_ONLY || pin < 8u) ? GPIO_AFRL : GPIO_AFRH);
        REG_WRITE(afr, field_put(REG_READ(afr), 4u * (pin & 7u), 0xFu, af));
    }
    uint32_t m = REG_READ(port + GPIO_MODER);
    switch (v) {
    case BUG_NO_CLEAR:     m |= mode << (2u * pin); break;
    case BUG_POS_NOT_2PIN: m = (m & ~(3u << pin)) | (mode << pin); break;
    case BUG_OVERWRITE:    m = mode << (2u * pin); break;
    default:               m = field_put(m, 2u * pin, 3u, mode); break;
    }
    REG_WRITE(port + GPIO_MODER, m);
}

int main(void) {
    printf("%-38s %8s %12s %12s\n", "variant (GPIOA, pins 0-15 x 4 modes x 64 seeds)", "configs", "target wrong", "others hit");
    for (int v = 0; v < NVAR; v++) {
        long n = 0, wrong = 0, others = 0;
        t_rng = 0xC0FFEEu;
        for (int seed = 0; seed < 64; seed++)
        for (unsigned pin = 0; pin < 16u; pin++)
        for (unsigned mode = 0; mode < 4u; mode++) {
            model_reset();
            mock_poke(RCC_AHB1ENR, xs32());
            uint32_t m0 = xs32(), afl0 = xs32(), afh0 = xs32();
            mock_poke(GPIOA + GPIO_MODER, m0);
            mock_poke(GPIOA + GPIO_AFRL, afl0);
            mock_poke(GPIOA + GPIO_AFRH, afh0);
            unsigned af = 7u;
            var_init(v, GPIOA, pin, mode, af);
            n++;
            uint32_t m = mock_peek(GPIOA + GPIO_MODER);
            uint32_t afr = mock_peek(GPIOA + (pin < 8u ? GPIO_AFRL : GPIO_AFRH));
            bool tgt_ok = ((m >> (2u * pin)) & 3u) == mode &&
                          (mode != GPIO_MODE_AF || ((afr >> (4u * (pin & 7u))) & 0xFu) == af);
            uint32_t keep = ~(3u << (2u * pin));                     /* 대상 필드 밖만 비교 */
            uint32_t keep_l = pin < 8u ? ~(0xFu << (4u * pin)) : 0xFFFFFFFFu;
            uint32_t keep_h = pin >= 8u ? ~(0xFu << (4u * (pin - 8u))) : 0xFFFFFFFFu;
            bool oth_ok = (m & keep) == (m0 & keep) &&
                          (mock_peek(GPIOA + GPIO_AFRL) & keep_l) == (afl0 & keep_l) &&
                          (mock_peek(GPIOA + GPIO_AFRH) & keep_h) == (afh0 & keep_h);
            wrong += !tgt_ok;
            others += !oth_ok;
        }
        printf("%-38s %8ld %12ld %12ld\n", VNAME[v], n, wrong, others);
    }

    /* toggle 3종: 접근 로그와 결과 (PB9 open-drain, 외부에서 0 으로 눌림, ODR=1) */
    const char *tn[3] = { "BSRR from ODR (correct)", "ODR ^= (RMW whole ODR)", "BSRR from IDR (wrong source)" };
    for (int t = 0; t < 3; t++) {
        model_reset();
        gpio_init_output(GPIOB, 9, 1, GPIO_SPEED_LOW, GPIO_OTYPE_OD);
        ext_set(GPIOB, 9, EXT_LOW);
        mock_log_clear();
        if (t == 0) gpio_toggle(GPIOB, 9);
        if (t == 1) REG_WRITE(GPIOB + GPIO_ODR, REG_READ(GPIOB + GPIO_ODR) ^ (1u << 9));
        if (t == 2) { uint32_t idr = REG_READ(GPIOB + GPIO_IDR);
                      REG_WRITE(GPIOB + GPIO_BSRR, (idr & (1u << 9)) ? (1u << 25) : (1u << 9)); }
        printf("\ntoggle via %s -> ODR bit9 %u -> %u\n", tn[t], 1u, (unsigned)(mock_peek(GPIOB + GPIO_ODR) >> 9 & 1u));
        dump_log("access log");
    }
    return 0;
}
