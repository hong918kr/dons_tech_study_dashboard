/* RTOS-B03 demo: macOS arm64 에서 getcontext 가 "실제로" 저장하는 칸은 어디인가
 *
 *   ucontext_t 를 0xEE 로 채운 뒤 getcontext 를 부르고, mcontext 의 8 바이트 칸 중 무엇이 바뀌었는지 본다.
 *   x19 와 d8 에는 inline asm 으로 알아보기 쉬운 값을 넣어 둔다.
 *   (arm64 macOS 전용 — 다른 플랫폼에서는 아무것도 안 하고 끝난다)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_mcontext_layout.c -o demo_layout && ./demo_layout
 */
#define _XOPEN_SOURCE 600
#include <ucontext.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>     /* SS_DISABLE */

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(__APPLE__) && defined(__aarch64__)
__attribute__((noinline)) static void grab(ucontext_t *uc) {
    __asm__ volatile("mov x19, %0\n\tfmov d8, %1"
                     :: "r"(0x1919191919191919ull), "r"(0x0808080808080808ull) : "x19", "d8");
    getcontext(uc);
}

static int changed(const void *p) {           /* 8 바이트 칸이 0xEE 채움에서 바뀌었나 */
    static const uint8_t ee[8] = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE };
    return memcmp(p, ee, 8) != 0;
}

int main(void) {
    static ucontext_t uc;
    memset(&uc, 0xEE, sizeof uc);
    grab(&uc);
    int local;
    const struct __darwin_mcontext64 *mc = uc.uc_mcontext;
    const struct __darwin_arm_thread_state64 *ss = &mc->__ss;
    const struct __darwin_arm_neon_state64 *ns = &mc->__ns;

    printf("sizeof(ucontext_t)=%zu  uc_mcsize=%zu  uc_mcontext==&__mcontext_data: %s\n",
           sizeof uc, (size_t)uc.uc_mcsize, (const void *)mc == (const void *)&uc.__mcontext_data ? "yes" : "no");
    printf("mcontext: __es @%zu (%zu B), __ss @%zu (%zu B), __ns @%zu (%zu B)\n",
           offsetof(struct __darwin_mcontext64, __es), sizeof mc->__es,
           offsetof(struct __darwin_mcontext64, __ss), sizeof mc->__ss,
           offsetof(struct __darwin_mcontext64, __ns), sizeof mc->__ns);

    printf("written __ss slots:");
    for (int i = 0; i < 29; i++) if (changed(&ss->__x[i])) printf(" x%d", i);
    if (changed(&ss->__fp)) printf(" fp");
    if (changed(&ss->__lr)) printf(" lr");
    if (changed(&ss->__sp)) printf(" sp");
    if (changed(&ss->__pc)) printf(" pc");
    if (changed(&ss->__cpsr)) printf(" cpsr/flags");
    printf("\nwritten __ns slots:");
    for (int i = 0; i < 32; i++) {
        const uint8_t *v = (const uint8_t *)&ns->__v[i];
        if (changed(v))     printf(" v%d.lo", i);
        if (changed(v + 8)) printf(" v%d.hi", i);
    }
    if (changed(&ns->__fpsr)) printf(" fpsr/fpcr");
    printf("\nwritten __es: %s\n", changed(&mc->__es) ? "yes" : "no");

    uint64_t d8_lo, d8_hi;
    memcpy(&d8_lo, (const uint8_t *)&ns->__v[8], 8);
    memcpy(&d8_hi, (const uint8_t *)&ns->__v[8] + 8, 8);
    printf("x19 = 0x%016llx\n", (unsigned long long)ss->__x[19]);
    printf("v8  = lo 0x%016llx  hi 0x%016llx\n", (unsigned long long)d8_lo, (unsigned long long)d8_hi);
    printf("x0  = 0x%016llx   (getcontext 가 다시 return 할 때의 반환값 0)\n", (unsigned long long)ss->__x[0]);
    printf("sp  = 0x%016llx   &local(main) = %p\n", (unsigned long long)ss->__sp, (void *)&local);
    printf("fp  = 0x%016llx\n", (unsigned long long)ss->__fp);
    printf("lr  = 0x%016llx   grab = %p (재개 지점 = grab 안, getcontext 호출 다음)\n", (unsigned long long)ss->__lr, (void *)grab);
    printf("flags(__pad) = 0x%x\n", ss->__pad);
    printf("uc_stack after getcontext: ss_sp=%p ss_size=%zu ss_flags=%d (SS_DISABLE=%d)\n",
           uc.uc_stack.ss_sp, (size_t)uc.uc_stack.ss_size, uc.uc_stack.ss_flags, SS_DISABLE);
    return 0;
}
#else
int main(void) { puts("arm64 macOS only"); return 0; }
#endif
