/* ENV: qemu */
/* ARM-B13 demo — demo_bugs.S 의 틀린 함수를 하나씩 (-DBUG=n) 실행해 무엇이 보이는지 출력한다.
 *   run_qemu.sh bugs   (각 BUG 를 따로 빌드·실행, timeout 5 s) */
#include <stdint.h>

void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(int32_t v);
typedef void (*anyfn_t)(void);
int32_t rc_call4(anyfn_t fn, const int32_t args[4], uint32_t *bad);
uint32_t max_u32_lt(uint32_t a, uint32_t b);
int32_t sum_clamped_nosave(const int32_t *a, uint32_t n, int32_t lo, int32_t hi);
int32_t sum_clamped_nolr(const int32_t *a, uint32_t n, int32_t lo, int32_t hi);
int32_t sum_noguard(const int32_t *a, uint32_t n);
int32_t max_nothumb(int32_t a, int32_t b);
int32_t max_nothumb2(int32_t a, int32_t b);

__attribute__((unused)) static const int32_t adc[5] = { 100, 4095, 65535, -32768, 2048 };

int main(void) {
#if BUG == 1
    sh_puts("BUG1 max_u32_lt(0xFFFFFFFF, 1) = "); sh_hex(max_u32_lt(0xFFFFFFFFu, 1u));
    sh_puts("  (expected 0xffffffff)\n");
#elif BUG == 2
    uint32_t m = 0;
    const int32_t a[4] = { (int32_t)(uintptr_t)adc, 5, 0, 4095 };
    int32_t r = rc_call4((anyfn_t)sum_clamped_nosave, a, &m);
    sh_puts("BUG2 sum_clamped_nosave = "); sh_dec(r); sh_puts("  clobbered mask = "); sh_hex(m); sh_puts("\n");
#elif BUG == 3
    sh_puts("BUG3 sum_clamped_nolr(adc, 0, ...) = "); sh_dec(sum_clamped_nolr(adc, 0, 0, 4095));
    sh_puts("   (n == 0: bl 이 없어 lr 이 살아 있다)\n");
    sh_puts("BUG3 sum_clamped_nolr(adc, 5, ...) 호출 → ...\n");
    sh_dec(sum_clamped_nolr(adc, 5, 0, 4095));
    sh_puts(" returned?!\n");
#elif BUG == 4
    sh_puts("BUG4 sum_noguard(0xFFFFFFF0, 0) 호출 → ...\n");
    sh_dec(sum_noguard((const int32_t *)0xFFFFFFF0u, 0));
    sh_puts(" returned?!\n");
#elif BUG == 5
    int32_t (*volatile fp)(int32_t, int32_t) = max_nothumb;
    sh_puts("BUG5 &max_nothumb = "); sh_hex((uint32_t)(uintptr_t)fp);
    sh_puts("  (bit0 = 0 → Thumb 비트 없음). blx 로 호출 → ...\n");
    sh_dec(fp(3, 7));
    sh_puts(" returned?!\n");
#elif BUG == 7
    int32_t (*volatile fp)(int32_t, int32_t) = max_nothumb2;
    sh_puts("BUG7 &max_nothumb2 = "); sh_hex((uint32_t)(uintptr_t)fp);
    sh_puts("  (bit0 = 0, 그리고 4 의 배수도 아님). blx 로 호출 → ...\n");
    sh_dec(fp(3, 7));
    sh_puts(" returned?!\n");
#elif BUG == 6
    sh_puts("BUG6 직접 호출 bl max_nothumb(3, 7) = ");
    sh_dec(max_nothumb(3, 7));
    sh_puts("\n");
#endif
    return 0;
}
