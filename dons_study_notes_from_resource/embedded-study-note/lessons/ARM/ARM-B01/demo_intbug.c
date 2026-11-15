/* ARM-B01 demo_intbug.c — "int 인자를 x 로 읽는 asm" 이 평범한 C 호출에서도 틀리는 것을 본다
 * 빌드: cc -std=c11 -O2 demo_intbug.c demo_intbug.S shared_hostile.S -o /tmp/ARM-B01_intbug && /tmp/ARM-B01_intbug
 */
#include <stdint.h>
#include <stdio.h>

void raw_regs(int a, int b, uint64_t out[2]);
int  max_int_bad(int a, int b);
long add_wide_bad(long a, int b);
typedef void (*anyfn)(void);
int  call2_dirty_i(anyfn fn, uint64_t r0, uint64_t r1);

__attribute__((noinline)) static void from_long(long big, long other, uint64_t out[2]) {
    raw_regs((int)big, (int)other, out);        /* long → int 는 레지스터에선 "아무것도 안 함" */
}

int main(void) {
    volatile int va = -5, vb = 3;               /* volatile: 상수 접기 막기 */
    volatile long vbig = 0x00000001FFFFFFFBL;   /* 아래 32비트 = -5, 윗 32비트 = 1 */
    uint64_t r[2];

    raw_regs(va, vb, r);
    printf("case 1  int 변수 -5, 3 을 그냥 넘김     : x0=%#018llx x1=%#018llx\n",
           (unsigned long long)r[0], (unsigned long long)r[1]);
    from_long(vbig, 3, r);
    printf("case 2  (int)0x1FFFFFFFB, 3 을 넘김     : x0=%#018llx x1=%#018llx\n",
           (unsigned long long)r[0], (unsigned long long)r[1]);

    printf("max_int_bad(-5, 3)        = %d   (정답 3)\n", max_int_bad(va, vb));
    printf("add_wide_bad(100, -1)     = %ld (정답 99)\n", add_wide_bad(100, va + 4));
    printf("hostile: max_int_bad(x0=0xDEADBEEF_00000007, x1=3) = %d (정답 7)\n",
           call2_dirty_i((anyfn)max_int_bad, 0xDEADBEEF00000007ull, 3));
    return 0;
}
