/* ARM-M05 demo_jit.c — macOS 사용자 공간 JIT: 코드 쓰기 → (W^X 전환) → I-cache 동기화 → 실행
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_jit.c -o demo_jit && ./demo_jit
 *
 * 1) MAP_JIT 페이지에 "mov w0,#42 ; ret" 을 쓰고 실행 → 42
 * 2) 같은 주소를 "add w0,w0,w1 ; ret" 로 덮어쓰고 실행 (3,4) → 7, "sub" → -1
 * 3) 실험: sys_icache_invalidate 를 "빼고" 덮어쓴 직후 실행하면 무엇이 돌아오나 (관찰만)
 * 4) sys_icache_invalidate 자체의 기계어 앞부분을 덤프 (llvm-objdump 로 해석해 md 에 붙인다)
 */
#define _DARWIN_C_SOURCE
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#if !defined(__aarch64__) || !defined(__APPLE__)
#error "Apple arm64 전용"
#endif

typedef int (*fn2)(int, int);
static uint32_t movz_w(unsigned rd, unsigned imm) { return 0x52800000u | (imm & 0xFFFFu) << 5 | rd; }
static uint32_t add_w(unsigned rd, unsigned rn, unsigned rm) { return 0x0B000000u | rm << 16 | rn << 5 | rd; }
static uint32_t sub_w(unsigned rd, unsigned rn, unsigned rm) { return 0x4B000000u | rm << 16 | rn << 5 | rd; }
#define RET 0xD65F03C0u

static void load(uint32_t *page, const uint32_t *code, size_t n, int sync) {
    pthread_jit_write_protect_np(0);              /* 이 스레드에서 MAP_JIT 페이지 = RW (실행 불가) */
    memcpy(page, code, n * 4);
    pthread_jit_write_protect_np(1);              /* 다시 RX (쓰기 불가) */
    if (sync) sys_icache_invalidate(page, n * 4); /* D-side 의 새 명령을 I-side 가 보게 한다 */
}

int main(void) {
    printf("pthread_jit_write_protect_supported_np() = %d\n", pthread_jit_write_protect_supported_np());
    uint32_t *page = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (page == MAP_FAILED) { perror("mmap MAP_JIT"); return 1; }
    fn2 f = (fn2)(uintptr_t)page;

    uint32_t c1[] = { movz_w(0, 42), RET };
    uint32_t c2[] = { add_w(0, 0, 1), RET };
    uint32_t c3[] = { sub_w(0, 0, 1), RET };
    load(page, c1, 2, 1); printf("run 1  mov w0,#42 ; ret     -> %d\n", f(3, 4));
    load(page, c2, 2, 1); printf("run 2  add w0,w0,w1 ; ret   -> %d\n", f(3, 4));
    load(page, c3, 2, 1); printf("run 3  sub w0,w0,w1 ; ret   -> %d\n", f(3, 4));

    /* 실험: invalidate 없이 바꾼 직후 실행 — I-side 가 옛 명령을 가져와도 아키텍처 위반이 아니다 (관찰만) */
    int stale = 0, fresh = 0, other = 0;
    for (int k = 0; k < 100000; k++) {
        uint32_t a[] = { movz_w(0, 1), RET }, b[] = { movz_w(0, 2), RET };
        load(page, a, 2, 1);
        for (int j = 0; j < 8; j++) (void)f(0, 0);  /* 옛 명령을 I-cache 에 확실히 올린다 */
        load(page, b, 2, 0);                         /* sync 없이 덮어쓰기 */
        int r = f(0, 0);
        if (r == 1) stale++; else if (r == 2) fresh++; else other++;
    }
    printf("no-invalidate experiment x100000: old code ran %d, new code ran %d, other %d\n", stale, fresh, other);

    const uint32_t *p = (const uint32_t *)(uintptr_t)&sys_icache_invalidate;
    printf("sys_icache_invalidate @ %p first 48 words:\n", (const void *)p);
    for (int i = 0; i < 48; i++) printf(".inst 0x%08x\n", p[i]);
    munmap(page, 16384);
    return 0;
}
