/* ARM-B04 demo_modes.c — 다섯 가지 모드의 before/after 를 실제로 찍는다
 * 빌드: cc -std=c11 -O2 demo_modes.c demo_modes.S -o /tmp/ARM-B04_modes && /tmp/ARM-B04_modes */
#include <stdint.h>
#include <stdio.h>
uint64_t m_off(const uint64_t *p, const uint64_t **after);
uint64_t m_pre(const uint64_t *p, const uint64_t **after);
uint64_t m_post(const uint64_t *p, const uint64_t **after);
uint64_t m_reg(const uint64_t *p, const uint64_t **after, uint64_t i);
uint64_t m_sxtw(const uint64_t *p, const uint64_t **after, int32_t k);

static void show(const char *mode, const uint64_t *a, const uint64_t *p, const uint64_t *after, uint64_t v) {
    printf("%-26s base before = a+%2td B   value = %2llu   base after = a+%2td B  (%+td)\n",
           mode, (const char *)p - (const char *)a, (unsigned long long)v,
           (const char *)after - (const char *)a, (const char *)after - (const char *)p);
}
int main(void) {
    const uint64_t a[4] = { 10, 20, 30, 40 };
    const uint64_t *after; uint64_t v;
    v = m_off(a, &after);            show("ldr x2, [x0, #8]",         a, a, after, v);
    v = m_pre(a, &after);            show("ldr x2, [x0, #8]!",        a, a, after, v);
    v = m_post(a, &after);           show("ldr x2, [x0], #8",         a, a, after, v);
    v = m_reg(a, &after, 2);         show("ldr x3, [x0, x2, lsl #3]", a, a, after, v);
    v = m_sxtw(a + 2, &after, -1);   show("ldr x3, [x0, w2, sxtw #3]", a, a + 2, after, v);
    return 0;
}
