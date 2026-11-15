/* TOOL-B12 demo: "bring your own" minimal UBSan runtime, firmware style.
 * With -fsanitize-minimal-runtime the compiler calls __ubsan_handle_<check>_minimal(void)
 * (no source-location data, no printf). A firmware image defines these itself:
 * record the caller PC + check kind in a small RAM log, keep running (or fault).
 *   cc -std=c11 -O1 -fsanitize=undefined -fsanitize-minimal-runtime \
 *      -fno-sanitize-link-runtime demo_minrt.c starter.c -o dm && ./dm
 * Then map each PC back to a line: atos -o dm -l <load addr> <pc>  (Mac)
 *                                  addr2line -e dm <pc>             (ELF; TOOL-M14) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fwub.h"

struct ub_event { const char *kind; uintptr_t pc; };
static struct ub_event g_ublog[16];
static unsigned g_ub_n;

static void ub_record(const char *kind, uintptr_t pc)
{
    for (unsigned i = 0; i < g_ub_n; i++)       /* report each site once */
        if (g_ublog[i].pc == pc) return;
    if (g_ub_n < 16) { g_ublog[g_ub_n].kind = kind; g_ublog[g_ub_n].pc = pc; g_ub_n++; }
}

#define UB_HANDLER(name)                                                          \
    void __ubsan_handle_##name##_minimal(void);                                  \
    void __ubsan_handle_##name##_minimal(void)                                   \
    { ub_record(#name, (uintptr_t)__builtin_return_address(0)); }                \
    void __ubsan_handle_##name##_minimal_abort(void);                            \
    void __ubsan_handle_##name##_minimal_abort(void)                             \
    { ub_record(#name, (uintptr_t)__builtin_return_address(0)); abort(); }

UB_HANDLER(add_overflow)        UB_HANDLER(sub_overflow)
UB_HANDLER(mul_overflow)        UB_HANDLER(negate_overflow)
UB_HANDLER(divrem_overflow)     UB_HANDLER(shift_out_of_bounds)
UB_HANDLER(out_of_bounds)       UB_HANDLER(load_invalid_value)
UB_HANDLER(type_mismatch)       UB_HANDLER(pointer_overflow)
UB_HANDLER(float_cast_overflow)

int main(void)
{
    volatile int32_t big = 30000;               /* volatile: keep values runtime */
    printf("adc_scale   -> %d\n", adc_scale(big, 100000, 3));
    printf("accum_add   -> %d\n", accum_add(2147483000, big));
    printf("reg_mask    -> 0x%X\n", reg_mask(0, 32));
    printf("therm       -> %u\n", therm_lookup(0x1000));
    printf("adc_scale   -> %d  (same site again)\n", adc_scale(big, 100000, 3));
    printf("--- ub log: %u distinct site(s)\n", g_ub_n);
    for (unsigned i = 0; i < g_ub_n; i++)
        printf("  #%u %-20s pc=0x%lx\n", i, g_ublog[i].kind, (unsigned long)g_ublog[i].pc);
    return g_ub_n ? 1 : 0;
}
