/* ARM-A04 demo_stale.c — "유지보수를 빼면 정말 옛 코드가 실행되나?" 를 이 Mac 에서 실측 (CHECK 없음, 시끄러운 실험)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_stale.c -o /tmp/ARM-A04_stale
 * run:   /tmp/ARM-A04_stale [alias|jit|fresh|cross|all]
 *
 * 공통: f(x) = x + imm (add x0,x0,#imm ; ret) 를 데이터로 고쳐 쓴 직후 f(0) 을 부르고, imm 이 아니면 "stale".
 *       stale 이면 새 값이 보일 때까지 f 를 최대 10^6 번 더 불러 "얼마나 오래 옛 코드가 남는가" 도 잰다.
 *       패치 전에 f 를 100 번 불러 I-cache 를 데워 둔다 (안 데우면 처음 실행은 메모리에서 새로 가져온다 → fresh 모드).
 * alias : 같은 물리 페이지를 RW 주소와 RX 주소 두 곳에 매핑 (mach_vm_remap). 쓰기는 RW 쪽, 실행은 RX 쪽.
 *         권한 전환·시스템콜이 하나도 끼지 않으므로 가장 순수한 I-cache 실험. 유지보수 방법 7가지 비교.
 * jit   : MAP_JIT + pthread_jit_write_protect_np 토글 (토글 자체에 ISB 가 들어 있다 — libsystem 디스어셈블).
 * fresh : 매번 새 MAP_JIT 영역에 쓰고 유지보수 없이 첫 실행 / munmap 후 같은 주소에 다시 받은 영역(재활용).
 * cross : 쓰는 스레드와 실행하는 스레드가 다르다 (멀티코어). writer 유지보수 유무 × executor ISB 유무.
 */
#define _DARWIN_C_SOURCE
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef uint64_t (*fn_t)(uint64_t);
#define RET 0xD65F03C0u
#define N 2000
#define PERSIST_CAP 1000000L
#define OFF 19                         /* 워드 19 = 바이트 76: 128 B 블록의 "두 번째 64 B 줄" 에 일부러 둔다 */
static uint32_t add_imm(unsigned imm) { return 0x91000000u | imm << 10; }   /* add x0, x0, #imm */
static inline void ic(const void *p)  { __asm__ volatile("ic ivau, %0" :: "r"(p) : "memory"); }
static inline void dc(const void *p)  { __asm__ volatile("dc cvau, %0" :: "r"(p) : "memory"); }
static inline void dsb(void)          { __asm__ volatile("dsb ish" ::: "memory"); }
static inline void isb(void)          { __asm__ volatile("isb" ::: "memory"); }

static long persist(fn_t f, unsigned imm) { long c = 0; while (f(0) != imm && c < PERSIST_CAP) c++; return c; }

/* ---------------- alias ---------------- */
enum { A_NONE, A_ISB, A_IC_NOISB, A_IC_STRIDE128, A_IC_ISB, A_DC_IC_ISB, A_SYS, A_N };
static const char *a_name[A_N] = {
    "no maintenance", "isb only", "dsb; ic ivau; dsb (no isb)", "ic ivau on 128B-aligned addr (stride 128)",
    "dsb; ic ivau; dsb; isb", "dc cvau; dsb; ic ivau; dsb; isb", "sys_icache_invalidate" };

static void run_alias(void) {
    uint32_t *w = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    mach_vm_address_t xa = 0; vm_prot_t cur, max;
    if (w == MAP_FAILED ||
        mach_vm_remap(mach_task_self(), &xa, 16384, 0, VM_FLAGS_ANYWHERE, mach_task_self(), (mach_vm_address_t)w, FALSE,
                      &cur, &max, VM_INHERIT_NONE) != KERN_SUCCESS ||
        mach_vm_protect(mach_task_self(), xa, 16384, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS) {
        printf("alias: mapping failed\n"); return;
    }
    uint32_t *x = (uint32_t *)(uintptr_t)xa;
    printf("alias: RW view %p, RX view %p (same physical page)\n", (void *)w, (void *)x);
    for (int m = 0; m < A_N; m++) {
        for (int i = 0; i < 4096; i++) w[i] = RET;
        w[OFF] = add_imm(0);
        sys_icache_invalidate(x, 16384);
        fn_t f = (fn_t)(uintptr_t)(x + OFF);
        long stale = 0, pmax = 0, never = 0;
        for (int i = 1; i <= N; i++) {
            for (int k = 0; k < 100; k++) (void)f(0);
            unsigned imm = (unsigned)i & 0xFFF;
            w[OFF] = add_imm(imm);                                   /* 데이터 쓰기 (RW 뷰) */
            switch (m) {
            case A_NONE: break;
            case A_ISB: isb(); break;
            case A_IC_NOISB: dsb(); ic(x + OFF); dsb(); break;
            case A_IC_STRIDE128: dsb(); ic((void *)((uintptr_t)(x + OFF) & ~(uintptr_t)127)); dsb(); isb(); break;
            case A_IC_ISB: dsb(); ic(x + OFF); dsb(); isb(); break;
            case A_DC_IC_ISB: dc(w + OFF); dsb(); ic(x + OFF); dsb(); isb(); break;
            case A_SYS: sys_icache_invalidate(x + OFF, 4); break;
            }
            if (f(0) != imm) { stale++; long c = persist(f, imm); if (c > pmax) pmax = c; if (c >= PERSIST_CAP) never++; }
        }
        printf("  %-44s stale %4ld/%d   max extra calls %7ld   still stale after 1e6 calls: %ld\n", a_name[m], stale, N, pmax, never);
    }
}

/* ---------------- jit (MAP_JIT 토글) ---------------- */
static void run_jit(void) {
    uint32_t *x = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (x == MAP_FAILED) { printf("jit: mmap failed\n"); return; }
    fn_t f = (fn_t)(uintptr_t)(x + OFF);
    for (int m = 0; m < 2; m++) {
        pthread_jit_write_protect_np(0);
        for (int i = 0; i < 4096; i++) x[i] = RET;
        x[OFF] = add_imm(0);
        pthread_jit_write_protect_np(1);
        sys_icache_invalidate(x, 16384);
        long stale = 0, never = 0;
        for (int i = 1; i <= N; i++) {
            for (int k = 0; k < 100; k++) (void)f(0);
            unsigned imm = (unsigned)i & 0xFFF;
            pthread_jit_write_protect_np(0); x[OFF] = add_imm(imm); pthread_jit_write_protect_np(1);
            if (m) sys_icache_invalidate(x + OFF, 4);
            if (f(0) != imm) { stale++; if (persist(f, imm) >= PERSIST_CAP) never++; }
        }
        printf("jit: toggle W/X + %-24s stale %4ld/%d   still stale after 1e6 calls: %ld\n",
               m ? "sys_icache_invalidate" : "nothing", stale, N, never);
    }
    munmap(x, 16384);
}

/* ---------------- fresh / recycled ---------------- */
static void run_fresh(void) {
    long stale_new = 0, stale_reuse = 0, same_va = 0;
    for (int i = 1; i <= 500; i++) {
        uint32_t *x = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if (x == MAP_FAILED) break;
        fn_t f = (fn_t)(uintptr_t)(x + OFF);
        pthread_jit_write_protect_np(0); x[OFF] = add_imm((unsigned)i); x[OFF + 1] = RET; pthread_jit_write_protect_np(1);
        if (f(0) != (unsigned)i) stale_new++;                         /* 처음 실행: 유지보수 없음 */
        for (int k = 0; k < 100; k++) (void)f(0);                     /* 데운 뒤 */
        uintptr_t old = (uintptr_t)x;
        munmap(x, 16384);
        x = mmap((void *)old, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if (x == MAP_FAILED) break;
        if ((uintptr_t)x == old) same_va++;
        f = (fn_t)(uintptr_t)(x + OFF);
        unsigned imm2 = (unsigned)i + 1000;
        pthread_jit_write_protect_np(0); x[OFF] = add_imm(imm2); x[OFF + 1] = RET; pthread_jit_write_protect_np(1);
        if (f(0) != imm2) stale_reuse++;                              /* 재활용 주소, 유지보수 없음 */
        munmap(x, 16384);
    }
    printf("fresh: brand-new MAP_JIT region, no maintenance, first call stale %ld/500\n", stale_new);
    printf("fresh: munmap+mmap (same VA %ld/500), no maintenance, first call stale %ld/500\n", same_va, stale_reuse);
}

/* ---------------- cross-thread ---------------- */
static uint32_t *cx; static int c_wmaint, c_eisb;
static _Atomic int c_seq, c_ack; static long c_stale, c_never;
static void *exec_thr(void *arg) {
    (void)arg; fn_t f = (fn_t)(uintptr_t)(cx + OFF);
    for (int i = 1; i <= N; i++) {
        while (atomic_load_explicit(&c_seq, memory_order_acquire) != i) (void)f(0);   /* 기다리며 계속 실행 (뜨거운 I-cache) */
        if (c_eisb) isb();
        unsigned imm = (unsigned)i & 0xFFF;
        if (f(0) != imm) { c_stale++; if (persist(f, imm) >= PERSIST_CAP) c_never++; }
        atomic_store_explicit(&c_ack, i, memory_order_release);
    }
    return NULL;
}
static void run_cross(void) {
    cx = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (cx == MAP_FAILED) { printf("cross: mmap failed\n"); return; }
    for (int combo = 0; combo < 4; combo++) {
        c_wmaint = combo >> 1; c_eisb = combo & 1; c_stale = c_never = 0;
        atomic_store(&c_seq, 0); atomic_store(&c_ack, 0);
        pthread_jit_write_protect_np(0);
        for (int i = 0; i < 4096; i++) cx[i] = RET;
        cx[OFF] = add_imm(0);
        pthread_jit_write_protect_np(1);
        sys_icache_invalidate(cx, 16384);
        pthread_t t; pthread_create(&t, NULL, exec_thr, NULL);
        for (int i = 1; i <= N; i++) {
            while (atomic_load_explicit(&c_ack, memory_order_acquire) != i - 1) { }
            for (volatile int d = 0; d < 2000; d++) { }
            pthread_jit_write_protect_np(0); cx[OFF] = add_imm((unsigned)i & 0xFFF); pthread_jit_write_protect_np(1);  /* writer 스레드만 RW */
            if (c_wmaint) sys_icache_invalidate(cx + OFF, 4);
            atomic_store_explicit(&c_seq, i, memory_order_release);
        }
        pthread_join(t, NULL);
        printf("cross: writer %-22s executor %-8s stale-first-call %4ld/%d   still stale after 1e6 calls: %ld\n",
               c_wmaint ? "sys_icache_invalidate," : "no maintenance,", c_eisb ? "isb" : "no isb", c_stale, N, c_never);
    }
    munmap(cx, 16384);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *m = argc > 1 ? argv[1] : "all";
    int all = !strcmp(m, "all");
    if (all || !strcmp(m, "alias")) run_alias();
    if (all || !strcmp(m, "jit")) run_jit();
    if (all || !strcmp(m, "fresh")) run_fresh();
    if (all || !strcmp(m, "cross")) run_cross();
    return 0;
}
