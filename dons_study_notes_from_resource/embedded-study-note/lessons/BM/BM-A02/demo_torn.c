/* BM-A02 demo — torn erase / torn program 을 눈으로 본다 (모델 = solution.c 그대로)
 * 빌드 (레슨 폴더에서): cc -std=c11 -Wall -Wextra -Werror -O2 demo_torn.c -o demo_torn && ./demo_torn
 */
#define main bm_a02_solution_main
#include "solution.c"
#undef main

static void hex16(const char *tag, const uint8_t *p) {
    printf("    %-7s", tag);
    for (int i = 0; i < 16; i++) printf(" %02X", p[i]);
    printf("\n");
}

static uint32_t popc8(uint8_t v) { uint32_t n = 0; while (v) { n += v & 1u; v >>= 1; } return n; }

int main(void) {
    flash_t *f = &g_f;
    static const char *names[4] = { "phase0 (early)", "phase1 (middle)", "phase2 (late: reads blank, weak)",
                                    "phase3 (done, EOP not seen)" };
    printf("== 1. one torn erase per phase (sector 0, first 16 B; old data = pseudo-random) ==\n");
    for (uint32_t ph = 0; ph < 4; ph++) {
        for (uint32_t sd = 1; sd < 1000; sd++) {
            flash_init(f, 0x08000000u, NULL, 256, 1, 4);
            fill_raw(f, 0, 256, 99);
            uint8_t before[16];
            memcpy(before, f->mem, 16);
            f->rng = sd; f->cut_at = 1;
            (void)flash_erase_sector(f, 0);
            if (f->last_tear != ph) continue;
            printf("  %s  (seed %u)\n", names[ph], sd);
            hex16("before", before);
            hex16("after", f->mem);
            hex16("weak", f->weak);
            printf("    blank_check(sector)=%s", drv_blank_check(f, 0x08000000u, 256) ? "PASS" : "FAIL");
            flash_age(f);
            printf("   after age: blank_check=%s\n", drv_blank_check(f, 0x08000000u, 256) ? "PASS" : "FAIL");
            break;
        }
    }

    printf("\n== 2. 10000 torn erases of a 256 B sector with random old data ==\n");
    {
        uint32_t ph[4] = { 0 }, blank_now[4] = { 0 }, blank_aged[4] = { 0 };
        uint64_t weak_bits = 0;
        for (uint32_t sd = 1; sd <= 10000; sd++) {
            flash_init(f, 0x08000000u, NULL, 256, 1, 4);
            fill_raw(f, 0, 256, sd * 31u + 7u);
            f->rng = sd * 2654435761u; f->cut_at = 1;
            (void)flash_erase_sector(f, 0);
            uint32_t p = f->last_tear;
            ph[p]++;
            for (uint32_t i = 0; i < 256; i++) weak_bits += popc8(f->weak[i]);
            blank_now[p] += drv_blank_check(f, 0x08000000u, 256);
            flash_age(f);
            blank_aged[p] += drv_blank_check(f, 0x08000000u, 256);
        }
        printf("  phase  count  blank-check PASS now  PASS after age\n");
        for (int p = 0; p < 4; p++)
            printf("  %u      %5u  %5u                 %5u\n", p, ph[p], blank_now[p], blank_aged[p]);
        printf("  mean weak bits per phase-2 sector: %.1f (of 2048 bits)\n",
               ph[2] ? (double)weak_bits / ph[2] : 0.0);
    }

    printf("\n== 3. 10000 torn programs of 32 B (8 units of 4 B) onto an erased area ==\n");
    {
        uint32_t pos[8] = { 0 }, frac_hist[5] = { 0 };
        for (uint32_t sd = 1; sd <= 10000; sd++) {
            flash_init(f, 0x08000000u, NULL, 256, 1, 4);
            f->rng = sd * 40503u + 1u; f->cut_at = 1;
            uint8_t d[32];
            uint32_t s = sd;
            for (int i = 0; i < 32; i++) d[i] = (uint8_t)xorshift32(&s);
            (void)flash_program(f, 0x08000000u, d, 32);
            for (uint32_t u = 0; u < 8; u++) {
                if (memcmp(f->mem + u * 4, d + u * 4, 4) == 0) continue;     /* 완료 단위 */
                bool blank = true;
                for (int i = 0; i < 4; i++) if (f->mem[u * 4 + i] != 0xFF) blank = false;
                if (blank && (u == 0 || memcmp(f->mem + (u - 1) * 4, d + (u - 1) * 4, 4) != 0)) continue;
                pos[u]++;
                uint32_t need = 0, done = 0;
                for (int i = 0; i < 4; i++) {
                    need += popc8((uint8_t)~d[u * 4 + i]);
                    done += popc8((uint8_t)~f->mem[u * 4 + i]);
                }
                frac_hist[need ? done * 4 / need : 4]++;
                break;
            }
        }
        printf("  torn unit index : ");
        for (int u = 0; u < 8; u++) printf("%u:%u ", u, pos[u]);
        printf("\n  fraction of needed 1->0 transitions done in the torn unit: "
               "<25%% %u, 25-50%% %u, 50-75%% %u, 75-100%% %u, 100%% %u\n",
               frac_hist[0], frac_hist[1], frac_hist[2], frac_hist[3], frac_hist[4]);
    }

    printf("\n== 4. scripted sequence campaign with 512 seeds per cut index ==\n");
    for (uint32_t i = 0; i < REC_A_N; i++) g_pay_a[i] = (uint8_t)(0xA0 + i);
    for (uint32_t i = 0; i < REC_B_N; i++) g_pay_b[i] = (uint8_t)(0x30 + 7 * i);
    campaign_t good, bad;
    campaign(POL_ALWAYS_ERASE, 512, &good);
    campaign(POL_SKIP_IF_BLANK, 512, &bad);
    printf("  N=%u ops, %u scenarios: torn erase phases %u/%u/%u/%u, torn program %u\n", good.n_ops, good.scen,
           good.phase[0], good.phase[1], good.phase[2], good.phase[3], good.phase[4]);
    printf("  false-accept (CRC said valid but bytes wrong): %u\n", good.false_accept);
    printf("  blank check passed on a torn-erased sector: phase0/1 %u, phase2 %u (silent weak %u)\n",
           good.blank_pass_01, good.blank_pass_2, good.silent_weak);
    printf("  ALWAYS_ERASE : ok after age %u / %u\n", good.ok_after_age, good.scen);
    printf("  SKIP_IF_BLANK: corrupt after age %u / %u  (all of them among the %u silent-weak cases)\n",
           bad.bad_after_age, bad.scen, bad.silent_weak);
    return 0;
}
