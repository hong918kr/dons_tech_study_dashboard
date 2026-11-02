/* H9 (UB part) · type punning through pointer casts.
 * Build WITHOUT -Werror; run with -fsanitize=undefined and plain -O0 / -O2. */
#include <stdint.h>
#include <stdio.h>

/* (1) strict aliasing: the compiler may assume an int* and a float* never alias */
__attribute__((noinline)) int alias(int *i, float *f) {
    *i = 1;
    *f = 0.0f;           /* if f == (float *)i this overwrote *i with 0 ... */
    return *i;           /* ... but -O2 may return the constant 1          */
}

/* (2) reading float bits through a uint32_t* (UB, "usually works") */
__attribute__((noinline)) uint32_t float_bits_cast(float *f) { return *(uint32_t *)f; }

/* (3)+(4) reading a wire buffer by casting: endian-dependent AND maybe misaligned */
__attribute__((noinline)) uint32_t load32_cast(const uint8_t *p) { return *(const uint32_t *)p; }

int main(void) {
    union { int i; float f; } u;                     /* same storage for both */
    printf("(1) alias(&u.i, &u.f) = %d\n", alias(&u.i, &u.f));

    float one = 1.0f;
    printf("(2) bits of 1.0f via cast = 0x%08X\n", (unsigned)float_bits_cast(&one));

    _Alignas(4) uint8_t buf[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    printf("(3) *(uint32_t *)buf       = 0x%08X  (bytes 12 34 56 78)\n", (unsigned)load32_cast(buf));
    printf("(4) *(uint32_t *)(buf + 1) = 0x%08X  (misaligned)\n", (unsigned)load32_cast(buf + 1));
    return 0;
}
