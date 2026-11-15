/* demo_u32fmt.c - TOOL-B02: uint32_t is 'unsigned int' on some targets and
 * 'unsigned long' on others, so "%u" is only right by luck. */
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
void show(uint32_t ticks);
void show(uint32_t ticks)
{
    printf("ticks=%u\n", ticks);                 /* F1 */
    printf("ticks=%lu\n", (unsigned long)ticks); /* F2: portable */
    printf("ticks=%" PRIu32 "\n", ticks);        /* F3: portable */
}
