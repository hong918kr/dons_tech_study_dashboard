/* H2 · sizeof array vs pointer · struct padding/alignment · packed
 * Values asserted here are for LP64 arm64/x86-64 (int=4, pointer=8,
 * double/uint64_t aligned to 8). */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct A { char a; int b; char c; };                 /* 1+3pad, 4, 1+3pad = 12 */
struct B { int b; char a; char c; };                 /* 4, 1, 1, 2pad     = 8  */
struct C { char a; double d; char c; };              /* 1+7, 8, 1+7       = 24 */
struct D { uint8_t a; uint16_t b; uint32_t c; uint64_t d; }; /* 1+1,2,4,8 = 16 */
struct E { char tag[5]; int v; };                    /* 5+3pad, 4         = 12 */
struct F { uint8_t a; uint32_t b; } __attribute__((packed));  /* 1, 4      = 5  */
struct G { uint8_t n; struct A inner; };             /* 1+3pad, 12        = 16 */

/* clang flags this by default (-Wsizeof-array-argument) — the warning IS the
 * answer; silenced here only so the -Werror build can demonstrate the value. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsizeof-array-argument"
static size_t param_size(const uint8_t buf[64]) {    /* buf is really a pointer */
    return sizeof buf;
}
#pragma clang diagnostic pop

int main(void) {
    uint8_t frame[64];
    const char *s = "hello";
    char t[] = "hello";

    printf("sizeof frame=%zu  param_size(frame)=%zu  count=%zu\n",
           sizeof frame, param_size(frame), sizeof frame / sizeof frame[0]);
    printf("sizeof s=%zu strlen(s)=%zu  sizeof t=%zu  sizeof \"abc\"=%zu\n",
           sizeof s, strlen(s), sizeof t, sizeof "abc");
    assert(sizeof frame == 64 && param_size(frame) == sizeof(uint8_t *));
    assert(sizeof s == 8 && strlen(s) == 5 && sizeof t == 6 && sizeof "abc" == 4);

    printf("A=%zu (b@%zu c@%zu)  B=%zu  C=%zu (d@%zu)  D=%zu (b@%zu c@%zu d@%zu)\n",
           sizeof(struct A), offsetof(struct A, b), offsetof(struct A, c),
           sizeof(struct B), sizeof(struct C), offsetof(struct C, d),
           sizeof(struct D), offsetof(struct D, b), offsetof(struct D, c),
           offsetof(struct D, d));
    printf("E=%zu (v@%zu)  F(packed)=%zu (b@%zu, align %zu)  G=%zu (inner@%zu)\n",
           sizeof(struct E), offsetof(struct E, v), sizeof(struct F),
           offsetof(struct F, b), _Alignof(struct F), sizeof(struct G),
           offsetof(struct G, inner));

    assert(sizeof(struct A) == 12 && offsetof(struct A, b) == 4 && offsetof(struct A, c) == 8);
    assert(sizeof(struct B) == 8);
    assert(sizeof(struct C) == 24 && offsetof(struct C, d) == 8);
    assert(sizeof(struct D) == 16 && offsetof(struct D, b) == 2 &&
           offsetof(struct D, c) == 4 && offsetof(struct D, d) == 8);
    assert(sizeof(struct E) == 12 && offsetof(struct E, v) == 8);
    assert(sizeof(struct F) == 5 && offsetof(struct F, b) == 1 && _Alignof(struct F) == 1);
    assert(sizeof(struct G) == 16 && offsetof(struct G, inner) == 4);

    /* the wire-safe way to read F.b out of a packed byte buffer: memcpy */
    uint8_t wire[5] = {0xAA, 0x78, 0x56, 0x34, 0x12};
    uint32_t b;
    memcpy(&b, wire + 1, sizeof b);              /* no unaligned deref */
    assert(b == 0x12345678u);                    /* little-endian host */

    puts("H2 PASS");
    return 0;
}
