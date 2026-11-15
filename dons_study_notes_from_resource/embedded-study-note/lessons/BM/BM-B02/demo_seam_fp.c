/* BM-B02 demo: 함수 포인터 이음매(bus_ops_t)의 타깃 비용 — 매크로 이음매(solution.c)와 비교
 *
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -std=c11 \
 *       -Wall -Wextra -Werror -c demo_seam_fp.c -o /tmp/BM-B02_work/fp.o
 * xcrun llvm-objdump -d /tmp/BM-B02_work/fp.o        (target_build.sh 의 5번이 이걸 돌린다)
 */
#include <stdint.h>

typedef struct { uint32_t (*rd)(uint32_t addr); void (*wr)(uint32_t addr, uint32_t val); } bus_ops_t;

static uint32_t mmio_rd(uint32_t a)             { return *(volatile uint32_t *)(uintptr_t)a; }
static void     mmio_wr(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
const bus_ops_t g_mmio_bus = { mmio_rd, mmio_wr };

/* 버스를 인자로 받는 드라이버: 컴파일러는 b->rd 가 무엇인지 모른다 → 간접 호출 */
int uart_putc_ops(const bus_ops_t *b, uint32_t base, uint8_t c) {
    for (uint32_t n = 0; n < 100u; n++) {
        if (b->rd(base + 0x00u) & (1u << 7)) {
            b->wr(base + 0x04u, c);
            return 0;
        }
    }
    return -1;
}
