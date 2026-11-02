/*
 * BV04 PLL configuration calculator.
 *
 *   f_pfd = f_ref / M            (pre-divider; PFD = phase-frequency detector input)
 *   f_vco = f_pfd * N            (feedback divider / multiplier; N may be fractional)
 *   f_out = f_vco / P            (post-divider)
 *
 * Search every (M, N, P) that satisfies the PFD and VCO windows and print the best
 * candidates by |error ppm| (errors < GOOD_PPM count as equal), tie-break: higher
 * f_pfd (less multiplication of reference/PFD noise, faster loop), then lower N.
 *
 * Presets are "-like" models built from public limits (see BV04 note section 2);
 * they are NOT register-accurate drivers for any part.
 *
 * Build: cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 \
 *          -Wall -Wextra -Werror -fsanitize=address,undefined pll_calc.c -o /tmp/pll_calc
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_P 16
#define TOPK 4
#define GOOD_PPM 1.0       /* errors below this count as equal */

struct pll_limits {
    const char *name;
    double fref;               /* Hz */
    int m_min, m_max;          /* pre-divider */
    int n_min, n_max;          /* integer part of feedback divider */
    int frac_bits;             /* 0 = integer-N, else N = n + k / 2^frac_bits */
    int p_list[MAX_P];         /* allowed post-dividers, 0-terminated */
    double pfd_min, pfd_max;   /* Hz */
    double vco_min, vco_max;   /* Hz */
};

struct cand {
    int m, p;
    double n;                  /* effective (possibly fractional) N */
    double pfd, vco, fout, ppm;
};

static int better(const struct cand *a, const struct cand *b)
{
    double ea = fabs(a->ppm), eb = fabs(b->ppm);
    /* below GOOD_PPM both are "exact enough": decide on PFD instead of noise-level ppm */
    if (!(ea < GOOD_PPM && eb < GOOD_PPM) && fabs(ea - eb) > 1e-6) return ea < eb;
    if (fabs(a->pfd - b->pfd) > 1e-3) return a->pfd > b->pfd;
    return a->n < b->n;
}

static void insert_top(struct cand *top, int *ntop, const struct cand *c)
{
    int i;
    if (*ntop < TOPK) {
        i = (*ntop)++;
    } else {
        i = TOPK - 1;
        if (!better(c, &top[i])) return;
    }
    top[i] = *c;
    for (; i > 0 && better(&top[i], &top[i - 1]); i--) {
        struct cand t = top[i]; top[i] = top[i - 1]; top[i - 1] = t;
    }
}

static void solve(const struct pll_limits *L, double target)
{
    struct cand top[TOPK];
    int ntop = 0;
    long tried = 0, rej_pfd = 0, rej_vco = 0, rej_n = 0;
    double scale = L->frac_bits ? (double)(1L << L->frac_bits) : 1.0;

    printf("--- %s : f_ref %.6f MHz -> target %.6f MHz (%s-N) ---\n", L->name,
           L->fref / 1e6, target / 1e6, L->frac_bits ? "fractional" : "integer");
    printf("    limits: M %d..%d  N %d..%d  PFD %.3f..%.3f MHz  VCO %.0f..%.0f MHz  P {",
           L->m_min, L->m_max, L->n_min, L->n_max, L->pfd_min / 1e6, L->pfd_max / 1e6,
           L->vco_min / 1e6, L->vco_max / 1e6);
    for (int i = 0; L->p_list[i]; i++) printf("%s%d", i ? "," : "", L->p_list[i]);
    printf("}\n");

    for (int m = L->m_min; m <= L->m_max; m++) {
        double pfd = L->fref / m;
        for (int pi = 0; L->p_list[pi]; pi++) {
            int p = L->p_list[pi];
            tried++;
            if (pfd < L->pfd_min || pfd > L->pfd_max) { rej_pfd++; continue; }
            /* VCO must equal target * P; pick N closest to that (quantized). */
            double n_ideal = target * p / pfd;
            double n_q = floor(n_ideal * scale + 0.5) / scale;
            /* try the rounded N first; only if it is out of range try its neighbours */
            static const int order[3] = { 0, -1, 1 };
            int why = 0;                       /* 1 = N range, 2 = VCO window */
            for (int k = 0; k < 3; k++) {
                double n = n_q + order[k] / scale;
                if (n < L->n_min || n > L->n_max + (scale - 1) / scale) { why = 1; continue; }
                double vco = pfd * n;
                if (vco < L->vco_min || vco > L->vco_max) { why = 2; continue; }
                struct cand c = { m, p, n, pfd, vco, vco / p, 0 };
                c.ppm = (c.fout - target) / target * 1e6;
                insert_top(top, &ntop, &c);
                why = 0;
                break;
            }
            if (why == 1) rej_n++;
            if (why == 2) rej_vco++;
        }
    }

    printf("    tried %ld (M,P) pairs: rejected PFD %ld, N-range %ld, VCO %ld\n",
           tried, rej_pfd, rej_n, rej_vco);
    if (ntop == 0) {
        int pmin = L->p_list[0], pmax = L->p_list[0];
        for (int i = 0; L->p_list[i]; i++) {
            if (L->p_list[i] < pmin) pmin = L->p_list[i];
            if (L->p_list[i] > pmax) pmax = L->p_list[i];
        }
        printf("    NO SOLUTION: reachable f_out = VCO/P = [%.3f, %.3f] MHz\n\n",
               L->vco_min / 1e6 / pmax, L->vco_max / 1e6 / pmin);
        return;
    }
    printf("    rank   M  N            P   f_pfd(MHz)  f_vco(MHz)  f_out(MHz)      err(ppm)\n");
    for (int i = 0; i < ntop; i++) {
        const struct cand *c = &top[i];
        printf("    #%d   %2d  %-11.5f %2d  %10.4f  %10.3f  %12.6f  %+11.3f\n", i + 1,
               c->m, c->n, c->p, c->pfd / 1e6, c->vco / 1e6, c->fout / 1e6, c->ppm);
    }
    if (L->frac_bits) {
        const struct cand *c = &top[0];
        long frac = lround((c->n - floor(c->n)) * scale);
        printf("    best encodes as N_int=%ld, N_frac=%ld/%ld\n", (long)floor(c->n), frac,
               (long)scale);
    }
    printf("\n");
}

int main(void)
{
    /* 1) STM32F4-style integer PLL (RM0090 limits: VCO in 1..2 MHz, VCO out 100..432 MHz). */
    struct pll_limits stm = { "stm32f4-like", 8e6, 2, 63, 50, 432, 0, {2, 4, 6, 8, 0},
                              1e6, 2e6, 100e6, 432e6 };
    /* 2) Zynq UltraScale+ PS-PLL-like: VCO = ref * FBDIV (25..125), VCO 1.5..3.0 GHz,
     *    optional DIV2 on the output. 33.333 MHz is the common PS_REF_CLK. */
    struct pll_limits zu = { "zynqmp-ps-pll-like", 33.333e6, 1, 1, 25, 125, 0, {1, 2, 0},
                             27e6, 60e6, 1500e6, 3000e6 };
    /* 3) Generic SoC PLL with 24 MHz crystal (typical Arm SoC ref) [추정 limits]. */
    struct pll_limits gen = { "generic-soc-int", 24e6, 1, 8, 16, 320, 0,
                              {1, 2, 3, 4, 5, 6, 7, 8, 0}, 5e6, 50e6, 1000e6, 3200e6 };
    struct pll_limits gen25 = gen; gen25.name = "generic-soc-int"; gen25.fref = 25e6;
    struct pll_limits gen25f = gen25; gen25f.name = "generic-soc-frac"; gen25f.frac_bits = 16;

    solve(&stm, 168e6);         /* classic 168 MHz SYSCLK */
    solve(&zu, 1200e6);         /* APU 1.2 GHz from 33.333 MHz -> small ppm error */
    solve(&gen, 1000e6);        /* 1 GHz CPU from 24 MHz */
    solve(&gen25, 148.5e6);     /* 1080p60 pixel clock from 25 MHz: integer-N can't hit it */
    solve(&gen25f, 148.5e6);    /* same with 16-bit fractional N */
    solve(&gen, 5e6);           /* impossible: VCO >= 1 GHz, P <= 8 -> f_out >= 125 MHz */
    return 0;
}
