/* TOOL-B11 demo — what ASan inserts before a load.
 *   cc -O1 -fsanitize=address -S demo_instr.c -o -        (Mac arm64)
 *   gcc -O1 -fsanitize=address -S demo_instr.c -o -       (Linux aarch64, VM)
 */
#include <stdint.h>

uint8_t get_byte(const uint8_t *p, long i) { return p[i]; }
