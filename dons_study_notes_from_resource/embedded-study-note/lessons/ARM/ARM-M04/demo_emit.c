/* ARM-M04 demo_emit.c — 컴파일러는 C11 atomics 를 어떤 명령으로 내리나? (main 없음, -S 로 본다)
 *   cc -std=c11 -O2 -S demo_emit.c -o -                              Apple 기본 (-target-cpu apple-m1): LSE
 *   cc -std=c11 -O2 -mcpu=generic -S demo_emit.c -o -                ARMv8.0: LDAXR/STLXR 루프, CAS 실패 경로 CLREX
 *   clang --target=aarch64-linux-gnu -O2 -moutline-atomics -ffreestanding -S demo_emit.c -o -   bl __aarch64_*
 *   (Lima VM) gcc -O2 -S / gcc -O2 -mno-outline-atomics -S / gcc -O2 -march=armv8.1-a -S
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

atomic_uint cnt;
unsigned fa_seqcst(unsigned v)  { return atomic_fetch_add(&cnt, v); }
unsigned fa_relaxed(unsigned v) { return atomic_fetch_add_explicit(&cnt, v, memory_order_relaxed); }
bool cas_strong(unsigned *e, unsigned d) { return atomic_compare_exchange_strong(&cnt, e, d); }
unsigned xchg(unsigned d) { return atomic_exchange(&cnt, d); }
