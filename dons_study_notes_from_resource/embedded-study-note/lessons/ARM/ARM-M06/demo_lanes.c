/* ARM-M06 demo_lanes: SDOT 한 번 뒤 v0 를 lane 뷰별로 lldb 에서 본다
 *   cc -std=c11 -g -O0 demo_lanes.c solution.S -o /tmp/ARM-M06_lanes && lldb -b -s demo_lanes.lldb /tmp/ARM-M06_lanes
 */
#include <stdint.h>
#include <stdio.h>
extern int32_t dot_s8_asm(const int8_t *a, const int8_t *b, size_t n);
int main(void) {
    int8_t a[16], b[16];
    for (int i = 0; i < 16; i++) { a[i] = (int8_t)(i + 1); b[i] = (int8_t)(i < 8 ? 1 : -2); }
    printf("dot_s8_asm = %d\n", dot_s8_asm(a, b, 16));   /* 36 - 2*100 = -164 */
    return 0;
}
