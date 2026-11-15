/* ARM-M03 demo_sink.c — "잰 루프가 -O2 에서 살아남나?" 를 asm 으로 확인 (실행 안 함, -S 만)
 *   clang -std=c11 -O2 -S -fno-asynchronous-unwind-tables -o - demo_sink.c
 * 1) 결과를 안 쓰는 C 루프        → 루프 자체가 사라진다 (측정 구간이 텅 빈다)
 * 2) 결과를 돌려주는 C 루프       → 닫힌 식(x + 3n)으로 바뀐다 (사슬이 없다)
 * 3) 빈 asm 장벽 "+r"(x)          → 매 반복 add 가 남는다 (DoNotOptimize 패턴)
 * 4) .S 커널 호출, 결과 버림      → 호출은 남는다 (불투명한 외부 함수) — 그래도 싱크에 모으는 게 안전
 */
#include <stdint.h>

uint64_t lat_add(uint64_t iters, uint64_t x, uint64_t y);
volatile uint64_t sink;

void c_loop_unused(uint64_t n) {
    uint64_t x = 0;
    for (uint64_t i = 0; i < n; i++) x += 3;
    (void)x;
}

uint64_t c_loop_closed_form(uint64_t n) {
    uint64_t x = 0;
    for (uint64_t i = 0; i < n; i++) x += 3;
    return x;
}

uint64_t c_loop_barrier(uint64_t n) {
    uint64_t x = 0;
    for (uint64_t i = 0; i < n; i++) {
        x += 3;
        __asm__ volatile("" : "+r"(x));      /* "x 를 읽고 바꿀지도 모른다" → 접어 버릴 수 없다 */
    }
    return x;
}

void asm_kernel_unused(void) {
    lat_add(1000, 0, 0);                     /* 반환값 버림 — 그래도 호출은 남는다 */
}

void asm_kernel_sink(void) {
    sink ^= lat_add(1000, 0, 0);             /* volatile 저장: 결과가 "관찰된다" */
}
