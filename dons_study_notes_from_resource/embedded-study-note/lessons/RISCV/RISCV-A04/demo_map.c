/* ENV: qemu */
/* RISCV-A04 demo_map.c — C11 atomics → RISC-V 명령 매핑을 memory_order 별로 뽑는다 (freestanding, libc 없음)
 * Lima VM elinux 안에서 (맥 clang 에는 RISC-V 백엔드가 없음):
 *   clang --target=riscv64-linux-gnu -march=rv64gc      -O2 -S demo_map.c -o map_clang.s
 *   clang --target=riscv64-linux-gnu -march=rv64gc_ztso -O2 -S demo_map.c -o map_clang_ztso.s
 *   riscv64-linux-gnu-gcc            -march=rv64gc      -O2 -S demo_map.c -o map_gcc.s
 *   riscv64-linux-gnu-gcc            -march=rv64gc_ztso -O2 -S demo_map.c -o map_gcc_ztso.s
 * 함수 이름 = <연산>_<순서>. 각 함수 본문만 보면 그 순서의 매핑이 나온다.
 */
#include <stdatomic.h>
#include <stdint.h>

atomic_int g;      /* 32-bit: .w 명령 */
atomic_long g64;   /* 64-bit: .d 명령 (rv64) */

/* ---- load ---- */
int ld_relaxed(void) { return atomic_load_explicit(&g, memory_order_relaxed); }
int ld_acquire(void) { return atomic_load_explicit(&g, memory_order_acquire); }
int ld_seq_cst(void) { return atomic_load_explicit(&g, memory_order_seq_cst); }

/* ---- store ---- */
void st_relaxed(int v) { atomic_store_explicit(&g, v, memory_order_relaxed); }
void st_release(int v) { atomic_store_explicit(&g, v, memory_order_release); }
void st_seq_cst(int v) { atomic_store_explicit(&g, v, memory_order_seq_cst); }

/* ---- RMW: fetch_add (AMO 한 개로 내려가는 대표) ---- */
int fadd_relaxed(int v) { return atomic_fetch_add_explicit(&g, v, memory_order_relaxed); }
int fadd_acquire(int v) { return atomic_fetch_add_explicit(&g, v, memory_order_acquire); }
int fadd_release(int v) { return atomic_fetch_add_explicit(&g, v, memory_order_release); }
int fadd_acq_rel(int v) { return atomic_fetch_add_explicit(&g, v, memory_order_acq_rel); }
int fadd_seq_cst(int v) { return atomic_fetch_add_explicit(&g, v, memory_order_seq_cst); }
long fadd64_seq_cst(long v) { return atomic_fetch_add_explicit(&g64, v, memory_order_seq_cst); }

/* ---- RMW: exchange ---- */
int xchg_relaxed(int v) { return atomic_exchange_explicit(&g, v, memory_order_relaxed); }
int xchg_acquire(int v) { return atomic_exchange_explicit(&g, v, memory_order_acquire); }
int xchg_release(int v) { return atomic_exchange_explicit(&g, v, memory_order_release); }
int xchg_seq_cst(int v) { return atomic_exchange_explicit(&g, v, memory_order_seq_cst); }

/* ---- RMW: compare_exchange_strong (A 확장엔 CAS 명령이 없음 → LR/SC 루프) ---- */
_Bool cas_relaxed(int *e, int d) { return atomic_compare_exchange_strong_explicit(&g, e, d, memory_order_relaxed, memory_order_relaxed); }
_Bool cas_acquire(int *e, int d) { return atomic_compare_exchange_strong_explicit(&g, e, d, memory_order_acquire, memory_order_acquire); }
_Bool cas_release(int *e, int d) { return atomic_compare_exchange_strong_explicit(&g, e, d, memory_order_release, memory_order_relaxed); }
_Bool cas_acq_rel(int *e, int d) { return atomic_compare_exchange_strong_explicit(&g, e, d, memory_order_acq_rel, memory_order_acquire); }
_Bool cas_seq_cst(int *e, int d) { return atomic_compare_exchange_strong_explicit(&g, e, d, memory_order_seq_cst, memory_order_seq_cst); }

/* ---- fence ---- */
void fn_acquire(void) { atomic_thread_fence(memory_order_acquire); }
void fn_release(void) { atomic_thread_fence(memory_order_release); }
void fn_acq_rel(void) { atomic_thread_fence(memory_order_acq_rel); }
void fn_seq_cst(void) { atomic_thread_fence(memory_order_seq_cst); }
void sigfn_seq_cst(void) { atomic_signal_fence(memory_order_seq_cst); }   /* 컴파일러 배리어만 */
