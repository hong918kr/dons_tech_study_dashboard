/* fw_bugs.c - TOOL-B02 practice: 6 firmware-flavoured bugs that the project
 * baseline (solution.flags) does NOT flag.  Each sits on a line tagged
 * // FWnn.  practice.flags must flag all six and still keep good.c clean. */
#include <stddef.h>
#include <stdint.h>

static const uint8_t k_gamma[4] = {0, 64, 128, 255};   /* lives in Flash */

int  fw_overtemp(float temp_c);
int  fw_sum_frame(const uint8_t *rx, size_t n);
uint32_t fw_word0(const uint8_t *rx);
void fw_patch_gamma(uint8_t v);
const void *fw_next(const void *p);
void fw_name_upcase(void);

int fw_overtemp(float temp_c)
{
    return temp_c > 85.0;                               // FW01
}

int fw_sum_frame(const uint8_t *rx, size_t n)
{
    uint8_t tmp[n];                                     // FW02
    int s = 0;
    for (size_t i = 0; i < n; i++) {
        tmp[i] = rx[i];
        s += tmp[i];
    }
    return s;
}

uint32_t fw_word0(const uint8_t *rx)
{
    return *(const uint32_t *)(rx + 1);                 // FW03
}

void fw_patch_gamma(uint8_t v)
{
    uint8_t *g = (uint8_t *)k_gamma;                    // FW04
    g[0] = v;
}

const void *fw_next(const void *p)
{
    return p + 4;                                       // FW05
}

void fw_name_upcase(void)
{
    char *name = "uart1";                               // FW06
    name[0] = 'U';
}
