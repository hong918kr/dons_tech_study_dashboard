/* ARCH-B11 demo — solution.c 의 모델을 그대로 가져와 메모리 맵과 접근 로그를 찍는다 (체크 없음)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_fault_trace.c -o demo && ./demo
 */
#define main lesson_main                 /* solution.c 의 테스트 main 은 이름만 바꿔 둔다 */
#include "solution.c"
#undef main

static const char *const st_name[] = { "OK", "UNMAPPED", "SIZE", "UNALIGNED", "CROSS", "RO", "GATED", "SLVERR" };

static void access(bus_t *b, const char *what, uint32_t addr, unsigned size, bool write, uint32_t wval) {
    uint32_t v = wval;
    uint32_t nf = b->nfault;
    bus_st_t st = write ? bus_write(b, addr, size, wval) : bus_read(b, addr, size, &v);
    int idx = map_find(b, addr);
    printf("%-28s %c%u @0x%08X  %-7s → %-9s", what, write ? 'W' : 'R', size, (unsigned)addr,
           idx < 0 ? "-" : b->map[idx].name, st_name[st]);
    if (st == BUS_OK && !write) printf(" val=0x%0*X", (int)size * 2, (unsigned)v);
    if (b->nfault != nf) printf("  [fault #%u %s]", (unsigned)b->nfault, b->last.precise ? "precise" : "IMPRECISE");
    printf("\n");
}

int main(void) {
    const uint8_t vec[8] = { 0x00, 0x40, 0x00, 0x20, 0x01, 0x01, 0x00, 0x08 };
    memcpy(flash_mem, vec, sizeof vec);
    bus_t b = new_bus(stm32_map, R_N);
    int bad;

    printf("memory map (map_validate = %d)\n", (int)map_validate(stm32_map, R_N, &bad));
    printf("  %-7s %-23s %-6s %-5s %-3s %s\n", "name", "range", "sizes", "align", "rw", "gate");
    for (int i = 0; i < R_N; i++) {
        const region_t *r = &stm32_map[i];
        printf("  %-7s 0x%08X-0x%08X  %s/%s/%s  %-5s %-3s %s\n", r->name, (unsigned)r->base,
               (unsigned)((uint64_t)r->base + r->size - 1), r->sizes & SZ1 ? "1" : "-", r->sizes & SZ2 ? "2" : "-",
               r->sizes & SZ4 ? "4" : "-", r->align ? "yes" : "no", r->writable ? "rw" : "ro",
               r->gate == GATE_NONE ? "-" : r->gate == GATE_RAZWI ? "RAZ/WI" : "FAULT");
    }
    printf("\n");
    access(&b, "reset: initial SP (alias)", 0x00000000u, 4, false, 0);
    access(&b, "reset: Reset_Handler", 0x00000004u, 4, false, 0);
    access(&b, "stack push", 0x20003FFCu, 4, true, 0xDEADBEEFu);
    access(&b, "buf @+0x10 = 0x11223344", 0x20000010u, 4, true, 0x11223344u);
    access(&b, "buf @+0x14 = 0xAABBCCDD", 0x20000014u, 4, true, 0xAABBCCDDu);
    access(&b, "packed struct field", 0x20000011u, 4, false, 0);
    access(&b, "GPIOA->BSRR = 1<<5", 0x40020018u, 4, true, 1u << 5);
    access(&b, "GPIOA ODR byte 0", 0x40020014u, 1, false, 0);
    access(&b, "uint16_t* into GPIO, odd", 0x40020015u, 2, false, 0);
    access(&b, "USART2 DR as byte", 0x40004404u, 1, true, 'A');
    access(&b, "NULL deref (+0x30)", 0x00000030u, 4, true, 1);
    access(&b, "stray pointer", 0x30000000u, 4, false, 0);
    access(&b, "memcpy past SRAM end", 0x20003FFEu, 4, false, 0);
    printf("  BFAR-like: valid=%d addr=0x%08X, faults=%u\n\n", b.bfar_valid, (unsigned)b.bfar, (unsigned)b.nfault);

    fault_clear(&b);
    printf("posted write to unmapped address, then the next access:\n");
    bus_write_posted(&b, 0x30000004u, 4, 7);
    printf("%-28s W4 @0x30000004  (buffered) → returns OK, faults=%u\n", "*(uint32_t*)0x30000004 = 7", (unsigned)b.nfault);
    access(&b, "next: load from SRAM", 0x20000000u, 4, false, 0);
    printf("  last fault addr=0x%08X precise=%d  BFAR valid=%d  ← 주소는 기록에만, BFAR 는 비어 있다\n\n",
           (unsigned)b.last.addr, b.last.precise, b.bfar_valid);

    printf("USART2 clock forgotten (RCC enable bit not set):\n");
    stm32_map[R_USART2].clk_on = false;
    int rc = uart_putc(&b, 0x40004400u, 'A', 1000);
    printf("  gate=RAZ/WI : uart_putc → %d (timeout after 1000 polls, faults=%u, tx=\"%s\")\n",
           rc, (unsigned)b.nfault, usart2.tx);
    stm32_map[R_USART2].gate = GATE_FAULT;
    rc = uart_putc(&b, 0x40004400u, 'A', 1000);
    printf("  gate=FAULT  : uart_putc → %d (fault %s @0x%08X, faults=%u)\n",
           rc, st_name[b.last.why], (unsigned)b.last.addr, (unsigned)b.nfault);
    (void)lesson_main;
    return 0;
}
