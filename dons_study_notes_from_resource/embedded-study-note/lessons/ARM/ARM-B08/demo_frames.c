/* ARM-B08 demo_frames.c — lldb 로 FP 체인을 직접 따라가 보는 최소 프로그램 (테스트 아님)
 * build: cc -std=c11 -g -O0 demo_frames.c solution.S -o /tmp/ARM-B08_frames
 * run  : lldb -b -s demo_frames.lldb /tmp/ARM-B08_frames
 */
#include <stdint.h>
#include <stdio.h>

uint64_t fact_asm(uint64_t n);

void base_hook(void);
void base_hook(void) {                 /* fact_asm 의 base case 가 bl _base_hook 으로 부른다 */
    printf("base_hook: fp=%p\n", __builtin_frame_address(0));
}

int main(void) {
    printf("fact_asm(4) = %llu\n", (unsigned long long)fact_asm(4));
    return 0;
}
