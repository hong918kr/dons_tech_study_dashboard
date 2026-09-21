#include <stdio.h>
#include <stdint.h>
#include <assert.h>

/* Function Signatures */
int count_set_bits(uint32_t val)
{
    int cnt = 0; 
    while (val)
    {
        if (val & 1 == 1)
        {
            cnt +=1;
        }
        val = val >> 1;
    }
    return cnt;
}
int count_set_bits_naive(uint32_t val)
{
    int cnt = 0;
    while (val)
    {
        val = val & (val - 1);
        cnt++;
    }
    return cnt;
}
static inline int log2_power8_naive(uint8_t v)
{
    while (v)
    {
        
    }

}

static inline int log2_power8_binary(uint8_t v)
{

}

/* Implementations */

// Brian Kernighan: clear lowest set bit each iteration
/*
int count_set_bits(uint32_t val) {
    int cnt = 0;
    while (val) {
        val &= (val - 1);
        ++cnt;
    }
    return cnt;
}
*/

// Naive: test each of 32 bits
/*
int count_set_bits_naive(uint32_t val) {
    int cnt = 0;
    for (int i = 0; i < 32; ++i) {
        if (val & (1u << i)) ++cnt;
    }
    return cnt;
}
*/

// Naive log2 for uint8_t when value is guaranteed power-of-two (O(8))
/*
static inline int log2_power8_naive(uint8_t v) {
    assert(v != 0 && (v & (v - 1)) == 0);
    int n = 0;
    while ((v & 1u) == 0u) {
        v >>= 1;
        ++n;
    }
    return n;
}
*/

// Binary-search style log2 for uint8_t (O(log 8) ~ 3 checks)
/*
static inline int log2_power8_binary(uint8_t v) {
    assert(v != 0 && (v & (v - 1)) == 0);
    int n = 0;
    if (v & 0xF0u) { n += 4; v >>= 4; }    // check high nibble
    if (v & 0x0Cu) { n += 2; v >>= 2; }    // check upper half of remaining nibble
    if (v & 0x02u) { n += 1; }             // check second bit
    return n;
}
*/

/* Test helpers */
static int run_case_kern(const char* name, uint32_t input, int expect) {
    int got = count_set_bits(input);
    int ok = (got == expect);
    printf("%s (Kernighan): %s | input=0x%08X got=%d expect=%d\n",
           name, ok ? "PASS" : "FAIL", (unsigned)input, got, expect);
    return ok;
}

static int run_case_naive(const char* name, uint32_t input, int expect) {
    int got = count_set_bits_naive(input);
    int ok = (got == expect);
    printf("%s (Naive):     %s | input=0x%08X got=%d expect=%d\n",
           name, ok ? "PASS" : "FAIL", (unsigned)input, got, expect);
    return ok;
}

static int run_log2_case(const char* name, uint8_t v, int expect) {
    int n1 = log2_power8_naive(v);
    int n2 = log2_power8_binary(v);
    int ok = (n1 == expect) && (n2 == expect);
    printf("%s: %s | v=0x%02X naive=%d binary=%d expect=%d\n",
           name, ok ? "PASS" : "FAIL", (unsigned)v, n1, n2, expect);
    return ok;
}

int main(void) {
    int passed = 0, total = 0;

    /* count_set_bits (Kernighan) tests */
    ++total; passed += run_case_kern("T1 zero", 0x00000000u, 0);
    ++total; passed += run_case_kern("T2 all ones", 0xFFFFFFFFu, 32);
    ++total; passed += run_case_kern("T3 pattern F0F0", 0xF0F0F0F0u, 16);
    ++total; passed += run_case_kern("T4 sample 0x12345678", 0x12345678u, 13);
    ++total; passed += run_case_kern("T5 single bit", 0x00000001u, 1);
    ++total; passed += run_case_kern("T6 high bit", 0x80000000u, 1);
    ++total; passed += run_case_kern("T7 alternating 0xAAAAAAAA", 0xAAAAAAAAu, 16);

    /* count_set_bits_naive tests */
    ++total; passed += run_case_naive("N1 zero", 0x00000000u, 0);
    ++total; passed += run_case_naive("N2 all ones", 0xFFFFFFFFu, 32);
    ++total; passed += run_case_naive("N3 pattern F0F0", 0xF0F0F0F0u, 16);
    ++total; passed += run_case_naive("N4 sample 0x12345678", 0x12345678u, 13);
    ++total; passed += run_case_naive("N5 single bit", 0x00000001u, 1);
    ++total; passed += run_case_naive("N6 high bit", 0x80000000u, 1);
    ++total; passed += run_case_naive("N7 alternating 0xAAAAAAAA", 0xAAAAAAAAu, 16);

    /* log2 power-of-two tests (both methods) */
    const uint8_t cases[8] = {1,2,4,8,16,32,64,128};
    for (int i = 0; i < 8; ++i) {
        ++total;
        char name[16];
        snprintf(name, sizeof(name), "L%d", i);
        passed += run_log2_case(name, cases[i], i);
    }

    printf("Passed %d/%d\n", passed, total);
    return (passed == total) ? 0 : 1;
}

/*
Build:
  gcc -std=c11 -O2 -Wall ayar_lab_self_prac.c -o ayar_lab_self_prac

Run:
  ./ayar_lab_self_prac
*/