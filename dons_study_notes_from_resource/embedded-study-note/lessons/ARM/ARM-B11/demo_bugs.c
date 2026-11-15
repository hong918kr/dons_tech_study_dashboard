/* ARM-B11 demo: demo_bugs.S 의 버그 ema 를 solution 과 같은 검사기(cs_check_call)로 돌린다
 *   cc -std=c11 -O2 demo_bugs.c demo_bugs.S shared_abi.S -o db && ./db
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

typedef float (*ema_fn)(float (*)(void), float, float);
extern float ema_bug_scratch(float (*read)(void), float y, float alpha);
extern float ema_bug_nosave(float (*read)(void), float y, float alpha);
extern void  clobber_fp_scratch(void);
extern float cs_check_call(ema_fn fn, float (*read)(void), float y, float alpha, uint32_t *mask);

static float sensor_read(void) { clobber_fp_scratch(); return 1.0f; }

static void run(const char *name, ema_fn fn) {
    uint32_t mask = 0;
    float got = cs_check_call(fn, sensor_read, 0.5f, 0.25f, &mask);
    float want = fmaf(0.25f, 1.0f - 0.5f, 0.5f);
    printf("%-16s got=%-14a want=%a  d8-d15 clobbered=0x%02x  upper=0x%02x\n",
           name, (double)got, (double)want, mask & 0xFFu, mask >> 8);
}

int main(void) {
    run("ema_bug_scratch", ema_bug_scratch);
    run("ema_bug_nosave", ema_bug_nosave);
    return 0;
}
