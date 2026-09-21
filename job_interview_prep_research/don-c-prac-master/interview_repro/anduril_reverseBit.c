#include <stdio.h>
#include <stdint.h>

uint32_t reverseBits(uint32_t n) {
    uint32_t reversed = 0;
    for (int i = 0; i < 32; i++) {
        reversed <<= 1; // Shift reversed left by 1 bit
        reversed |= (n & 1); // OR the least significant bit of n into reversed
        n >>= 1; // Shift n right by 1 bit
    }
    return reversed;
}

// Another, possibly faster version, using bit manipulation tricks
uint32_t reverseBitsOptimized(uint32_t n) {
    n = (n >> 16) | (n << 16);
    n = ((n & 0xFF00FF00) >> 8) | ((n & 0x00FF00FF) << 8);
    n = ((n & 0xF0F0F0F0) >> 4) | ((n & 0x0F0F0F0F) << 4);
    n = ((n & 0xCCCCCCCC) >> 2) | ((n & 0x33333333) << 2);
    n = ((n & 0xAAAAAAAA) >> 1) | ((n & 0x55555555) << 1);
    return n;
}

int main() {
    uint32_t num = 43261596; // Example number
    uint32_t reversedNum = reverseBits(num);
    uint32_t reversedNumOptimized = reverseBitsOptimized(num);

    printf("Original number: %u\n", num);
    printf("Reversed bits (basic): %u\n", reversedNum);
    printf("Reversed bits (optimized): %u\n", reversedNumOptimized);

    //Example with a small number
    num = 5;
    reversedNum = reverseBits(num);
    reversedNumOptimized = reverseBitsOptimized(num);

    printf("Original number: %u\n", num);
    printf("Reversed bits (basic): %u\n", reversedNum);
    printf("Reversed bits (optimized): %u\n", reversedNumOptimized);

    return 0;
}