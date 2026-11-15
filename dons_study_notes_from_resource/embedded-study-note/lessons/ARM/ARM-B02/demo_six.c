/* ARM-B02 demo: 6 C functions to read at -O0 vs -O2.
 *   clang -S -O0 -fno-asynchronous-unwind-tables -o demo_six_O0.s demo_six.c
 *   clang -S -O2 -fno-asynchronous-unwind-tables -o demo_six_O2.s demo_six.c
 * (gen_listings.sh regenerates both and strips .cfi lines)
 */
#include <stddef.h>
#include <stdint.h>

/* 1. loop sum */
long sum_loop(const long *a, long n) {
    long s = 0;
    for (long i = 0; i < n; i++)
        s += a[i];
    return s;
}

/* 2. switch: NVMe-admin-like opcode dispatcher (10 cases, some holes) */
int op_delete_sq(int), op_create_sq(int), op_get_log(int), op_delete_cq(int),
    op_create_cq(int), op_identify(int), op_abort(int), op_set_feat(int),
    op_get_feat(int), op_async_ev(int);
int dispatch(int opc, int arg) {
    switch (opc) {
    case 0x00: return op_delete_sq(arg);
    case 0x01: return op_create_sq(arg);
    case 0x02: return op_get_log(arg);
    case 0x04: return op_delete_cq(arg);
    case 0x05: return op_create_cq(arg);
    case 0x06: return op_identify(arg);
    case 0x08: return op_abort(arg);
    case 0x09: return op_set_feat(arg);
    case 0x0A: return op_get_feat(arg);
    case 0x0C: return op_async_ev(arg);
    default:   return -22; /* -EINVAL */
    }
}

/* 3. struct field access */
struct pkt {
    uint8_t  type;        /* offset 0          */
    uint16_t len;         /* offset 2 (pad 1)  */
    uint32_t seq;         /* offset 4          */
    uint64_t ts;          /* offset 8          */
    uint8_t  payload[16]; /* offset 16         */
};
uint32_t pkt_score(const struct pkt *p) {
    return p->seq + p->len + p->payload[3];
}

/* 4. call another function (non-leaf) */
long helper(long x);
long call_twice(long x) {
    long a = helper(x);
    long b = helper(x + 1);
    return a * b + x;
}

/* 5. bounds-checked array index */
int report_oob(size_t i);
int get_checked(const int *a, size_t n, size_t i) {
    if (i >= n)
        return report_oob(i);
    return a[i];
}

/* 6. multiply / divide by constants */
uint32_t const_math(uint32_t x, int32_t y) {
    return x * 9 + x / 10 + (uint32_t)(y / 4);
}
