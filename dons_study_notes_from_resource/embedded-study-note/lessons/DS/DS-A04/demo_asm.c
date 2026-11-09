/* DS-A04 demo_asm.c — 같은 Treiber pop 이 타깃마다 어떤 명령이 되나 (freestanding: libc 헤더 없음)
 *
 *   arm64 macOS (기본 apple-m1 계열, LSE/LSE2): cc -std=c11 -O2 -S demo_asm.c -o -
 *   ARMv8.0 (LSE 없음, LL/SC):  clang --target=aarch64-linux-gnu -mcpu=cortex-a53 -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 *   Cortex-M4 (ARMv7E-M):       clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 *   Cortex-M0+ (ARMv6-M):       clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 *   x86-64:                     clang --target=x86_64-linux-gnu [-mcx16] -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
 * (Cortex-M/x86 에서는 16바이트·8바이트 atomic 이 "lock-free 아님" 경고(-Watomic-alignment)가 뜬다 — 관찰 대상이다.)
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NIL 0xFFFFFFFFu
extern _Atomic uint32_t pool_next[64];

/* (1) 인덱스만: ABA 취약. 컴파일러 CAS 가 LL/SC 로 내려가도 마찬가지 — 비교값 old 는 CAS 밖에서 읽었다. */
_Atomic uint32_t top32;
uint32_t naive_pop(void) {
    uint32_t old = atomic_load_explicit(&top32, memory_order_acquire);
    while (old != NIL) {
        uint32_t next = atomic_load_explicit(&pool_next[old], memory_order_relaxed);
        if (atomic_compare_exchange_weak_explicit(&top32, &old, next, memory_order_acquire, memory_order_acquire))
            return old;
    }
    return NIL;
}

/* (2) 64비트 (태그 32 | 인덱스 32) */
_Atomic uint64_t top64;
uint32_t tag64_pop(void) {
    uint64_t old = atomic_load_explicit(&top64, memory_order_acquire);
    while ((uint32_t)old != NIL) {
        uint32_t next = atomic_load_explicit(&pool_next[(uint32_t)old], memory_order_relaxed);
        uint64_t nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | next;
        if (atomic_compare_exchange_weak_explicit(&top64, &old, nw, memory_order_acquire, memory_order_acquire))
            return (uint32_t)old;
    }
    return NIL;
}

/* (3) 32비트에 (태그 16 | 인덱스 16) — 64비트 CAS 가 없는 Cortex-M3/M4 용 */
_Atomic uint32_t top16;
uint32_t tag16_pop(void) {
    uint32_t old = atomic_load_explicit(&top16, memory_order_acquire);
    while ((old & 0xFFFFu) != 0xFFFFu) {
        uint32_t next = atomic_load_explicit(&pool_next[old & 0xFFFFu], memory_order_relaxed) & 0xFFFFu;
        uint32_t nw = ((((old >> 16) + 1u) & 0xFFFFu) << 16) | next;
        if (atomic_compare_exchange_weak_explicit(&top16, &old, nw, memory_order_acquire, memory_order_acquire))
            return old & 0xFFFFu;
    }
    return 0xFFFFu;
}

/* (4) 16바이트 {포인터, 태그} */
struct node { _Atomic(struct node *) next; };
typedef struct { struct node *ptr; uintptr_t tag; } tagptr_t;
_Atomic tagptr_t top128;
struct node *dw_pop(void) {
    tagptr_t old = atomic_load_explicit(&top128, memory_order_acquire);
    while (old.ptr) {
        tagptr_t nw = { atomic_load_explicit(&old.ptr->next, memory_order_relaxed), old.tag + 1u };
        if (atomic_compare_exchange_weak_explicit(&top128, &old, nw, memory_order_acquire, memory_order_acquire))
            return old.ptr;
    }
    return NULL;
}

#if defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__)
/* (5) Cortex-M3/M4: 진짜 LL/SC pop — LDREX 로 top 을 읽고, next 를 읽고, STREX. 그 사이 다른 쓰기나
 *     예외(ISR 진입/복귀)가 있었으면 STREX 가 실패 → 값이 A 로 돌아왔어도 다시 시도한다 (태그 불필요). */
uint32_t llsc_pop_m4(void) {
    for (;;) {
        uint32_t old = __builtin_arm_ldrex((volatile uint32_t *)&top32);
        if (old == NIL) { __builtin_arm_clrex(); return NIL; }
        uint32_t next = atomic_load_explicit(&pool_next[old], memory_order_relaxed);
        if (__builtin_arm_strex(next, (volatile uint32_t *)&top32) == 0) {
            __builtin_arm_dmb(0xF);                 /* acquire 대용: 이후 노드 접근이 앞서지 않게 (DMB SY) */
            return old;
        }
    }
}
#endif
