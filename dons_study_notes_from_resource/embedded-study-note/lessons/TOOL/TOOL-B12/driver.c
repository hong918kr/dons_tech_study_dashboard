/* TOOL-B12 test driver (not a TODO file). One case per process:
 *   ./ub --list        print case names
 *   ./ub <case>        run one case, print "got .. want ..", exit 0 if equal, 2 if not
 * check.sh builds this together with starter.c / solution.c under UBSan. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fwub.h"

static _Alignas(8) uint8_t g_frame[32];  /* aligned; packets are placed at an offset */

static int want_i(const char *what, long long got, long long want)
{
    printf("%s: got %lld want %lld\n", what, got, want);
    return got == want ? 0 : 2;
}

static int want_x(const char *what, unsigned long got, unsigned long want)
{
    printf("%s: got 0x%lX want 0x%lX\n", what, got, want);
    return got == want ? 0 : 2;
}

static int want_s(const char *what, const char *got, const char *want)
{
    printf("%s: got \"%s\" want \"%s\"\n", what, got, want);
    return strcmp(got, want) == 0 ? 0 : 2;
}

/* wire header: type, seq LE (4), ack, len LE (2) */
static int pkt_case(size_t off, size_t n, const uint8_t wire[8], int want_rc,
                    uint32_t want_seq, int want_ack, uint16_t want_len)
{
    struct pkt_hdr h = { 0, 0, false, 0 };
    memcpy(g_frame + off, wire, 8);
    int rc = pkt_parse(g_frame + off, n, &h);
    printf("pkt_parse(frame+%zu, n=%zu): rc %d seq 0x%08X ack %d len 0x%04X | want rc %d",
           off, n, rc, (unsigned)h.seq, h.ack ? 1 : 0, (unsigned)h.len, want_rc);
    if (want_rc == 0)
        printf(" seq 0x%08X ack %d len 0x%04X", (unsigned)want_seq, want_ack, (unsigned)want_len);
    printf("\n");
    if (rc != want_rc) return 2;
    if (rc != 0) return 0;
    return (h.seq == want_seq && (h.ack ? 1 : 0) == want_ack && h.len == want_len) ? 0 : 2;
}

static int run(const char *c)
{
    static const uint8_t w_ok[8]  = { 0x10, 0xEF, 0xBE, 0xAD, 0xDE, 0x01, 0x34, 0x12 };
    static const uint8_t w_ack[8] = { 0x10, 0x01, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00 };
    const struct cal cal_m5 = { -5 };

    /* Part A */
    if (!strcmp(c, "adc_ok"))       return want_i("adc_scale(1000,3300,4095)", adc_scale(1000, 3300, 4095), 805);
    if (!strcmp(c, "adc_mul_ovf"))  return want_i("adc_scale(30000,100000,3)", adc_scale(30000, 100000, 3), 1000000000);
    if (!strcmp(c, "adc_div0"))     return want_i("adc_scale(1000,3300,0)", adc_scale(1000, 3300, 0), 0);
    if (!strcmp(c, "adc_min_div"))  return want_i("adc_scale(INT32_MIN,1,-1)", adc_scale(INT32_MIN, 1, -1), INT32_MAX);
    if (!strcmp(c, "accum_ok"))     return want_i("accum_add(-5,3)", accum_add(-5, 3), -2);
    if (!strcmp(c, "accum_sat"))    return want_i("accum_add(2147483000,1000)", accum_add(2147483000, 1000), INT32_MAX);
    if (!strcmp(c, "accum_sat_lo")) return want_i("accum_add(INT32_MIN,-1)", accum_add(INT32_MIN, -1), INT32_MIN);
    if (!strcmp(c, "mag_min"))      return want_i("sample_mag(INT32_MIN)", sample_mag(INT32_MIN), 2147483648LL);
    if (!strcmp(c, "mask_ok"))      return want_x("reg_mask(4,3)", reg_mask(4, 3), 0x70);
    if (!strcmp(c, "mask_top"))     return want_x("reg_mask(28,4)", reg_mask(28, 4), 0xF0000000ul);
    if (!strcmp(c, "mask_31"))      return want_x("reg_mask(1,31)", reg_mask(1, 31), 0xFFFFFFFEul);
    if (!strcmp(c, "mask_32"))      return want_x("reg_mask(0,32)", reg_mask(0, 32), 0xFFFFFFFFul);
    if (!strcmp(c, "align12"))      return want_x("sample_to_u16(0xFFF,12)", sample_to_u16(0xFFF, 12), 0xFFF0);
    if (!strcmp(c, "align18"))      return want_x("sample_to_u16(0x3FFFF,18)", sample_to_u16(0x3FFFF, 18), 0xFFFF);
    /* Part B */
    if (!strcmp(c, "pkt_short"))    return pkt_case(0, 4, w_ok, -1, 0, 0, 0);
    if (!strcmp(c, "pkt_seq"))      return pkt_case(0, 8, w_ok, 0, 0xDEADBEEFu, 1, 0x1234);
    if (!strcmp(c, "pkt_ack"))      return pkt_case(3, 8, w_ack, -2, 0, 0, 0);
    if (!strcmp(c, "mode_ok"))      return want_s("pwr_mode_name(1)", pwr_mode_name(1), "SLEEP");
    if (!strcmp(c, "mode_bad"))     return want_s("pwr_mode_name(7)", pwr_mode_name(7), "?");
    if (!strcmp(c, "cal_ok"))       return want_i("cal_apply({-5},100)", cal_apply(&cal_m5, 100), 95);
    if (!strcmp(c, "cal_null"))     return want_i("cal_apply(NULL,100)", cal_apply(NULL, 100), 100);
    if (!strcmp(c, "therm_ok"))     return want_i("therm_lookup(0x234)", therm_lookup(0x234), 3420);
    if (!strcmp(c, "therm_glitch")) return want_i("therm_lookup(0x1000)", therm_lookup(0x1000), 690);
    /* Part C */
    if (!strcmp(c, "temp_ok"))      return want_i("temp_to_centi(-12.5)", temp_to_centi(-12.5f), -1250);
    if (!strcmp(c, "temp_hot"))     return want_i("temp_to_centi(400.0)", temp_to_centi(400.0f), INT16_MAX);
    if (!strcmp(c, "temp_nan"))     return want_i("temp_to_centi(NAN)", temp_to_centi(NAN), 0);
    if (!strcmp(c, "duty_ok"))      return want_i("duty_to_ccr(0.25,999)", duty_to_ccr(0.25f, 999), 249);
    if (!strcmp(c, "duty_neg"))     return want_i("duty_to_ccr(-0.25,999)", duty_to_ccr(-0.25f, 999), 0);
    if (!strcmp(c, "duty_over"))    return want_i("duty_to_ccr(1.5,999)", duty_to_ccr(1.5f, 999), 999);
    printf("unknown case %s\n", c);
    return 3;
}

static const char *const k_cases[] = {
    "adc_ok", "adc_mul_ovf", "adc_div0", "adc_min_div", "accum_ok", "accum_sat", "accum_sat_lo",
    "mag_min", "mask_ok", "mask_top", "mask_31", "mask_32", "align12", "align18",
    "pkt_short", "pkt_seq", "pkt_ack", "mode_ok", "mode_bad", "cal_ok", "cal_null",
    "therm_ok", "therm_glitch",
    "temp_ok", "temp_hot", "temp_nan", "duty_ok", "duty_neg", "duty_over",
};

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++)
            printf("%s\n", k_cases[i]);
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: %s --list | <case>\n", argv[0]);
        return 3;
    }
    setvbuf(stdout, NULL, _IONBF, 0);  /* keep stdout/stderr order in the log */
    return run(argv[1]);
}
