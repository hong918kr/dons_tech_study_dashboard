/* ARCH-M08 demo_asm.c — 벤치마크의 세 가지 "증가" 가 실제로 어떤 명령이 되는지 본다 (-S 로만 본다, 실행 X).
 * freestanding: <stdatomic.h> <stdint.h> 만 → arm64 / x86_64 / thumbv7em(Cortex-M7) 모두 같은 소스.
 *
 *   cc -std=c11 -O2 -S demo_asm.c -o -                                          # arm64 (Apple M6)
 *   cc -std=c11 -O2 -S -arch x86_64 demo_asm.c -o -                             # x86_64 (Rosetta 로 돌릴 코드)
 *   clang -std=c11 -O2 -S --target=thumbv7em-none-eabi -mcpu=cortex-m7 demo_asm.c -o -
 */
#include <stdatomic.h>
#include <stdint.h>

/* (1) relaxed RMW, 결과를 안 씀 — 벤치마크 모드 "atomic" */
void inc_atomic(_Atomic uint32_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) atomic_fetch_add_explicit(p, 1, memory_order_relaxed);
}

/* (2) 지역 변수를 올리고 volatile 로 저장만 — 벤치마크 모드 "vstore" (읽기 없음) */
void inc_vstore(volatile uint32_t *p, uint32_t n) {
    uint32_t local = 0;
    for (uint32_t i = 0; i < n; i++) *p = ++local;
}

/* (3) volatile 읽고-더하고-쓰기 (비원자, 칸 주인이 한 스레드라 정확) — 벤치마크 모드 "vrmw" */
void inc_vrmw(volatile uint32_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) *p = *p + 1;
}

/* (4) 함정: volatile 도 atomic 도 아닌 평범한 증가 → 컴파일러가 루프를 통째로 없앤다 */
void inc_plain(uint32_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) *p = *p + 1;
}
