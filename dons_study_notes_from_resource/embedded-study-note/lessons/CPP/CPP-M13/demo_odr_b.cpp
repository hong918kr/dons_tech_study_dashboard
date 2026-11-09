#include "demo_odr.hpp"
u32 lookup_b(u32 i) { return table.v[(i + 1u) & 0xFFu]; }
extern "C" [[noreturn]] void _start() {
    volatile u32 s = lookup_a(1) + lookup_b(2);
    (void)s;
    for (;;) {}
}
