```c
#include <stdio.h>
#include <stdint.h>

uint32_t byteSwap32Loop(uint32_t value) {
    uint32_t swapped = 0;
    for (int i = 0; i < 4; i++) {
        swapped |= ((value >> (i * 8)) & 0xFF) << ((3 - i) * 8);
    }
    return swapped;
}

int main() {
    uint32_t num32 = 0x12345678;

    printf("Original 32-bit: 0x%08X\n", num32);
    printf("Swapped 32-bit (loop): 0x%08X\n", byteSwap32Loop(num32));

    return 0;
}
```

**Explanation of the Looping Approach:**

1.  **Initialization:**
    * `uint32_t swapped = 0;`: Initializes the `swapped` variable to 0, which will store the byte-swapped result.

2.  **Looping Through Bytes:**
    * `for (int i = 0; i < 4; i++)`: The loop iterates four times, once for each byte in the 32-bit value.

3.  **Extracting a Byte:**
    * `((value >> (i * 8)) & 0xFF)`:
        * `value >> (i * 8)`: Right-shifts the `value` by `i * 8` bits. This brings the `i`-th byte to the least significant byte position.
        * `& 0xFF`: Performs a bitwise AND with `0xFF` (255), which extracts the least significant byte (the byte we shifted into place).

4.  **Placing the Byte in the Swapped Value:**
    * `<< ((3 - i) * 8)`: Left-shifts the extracted byte by `(3 - i) * 8` bits. This places the byte in its reversed position in the `swapped` value.
    * `swapped |= ...`: Performs a bitwise OR with the `swapped` value. This combines the extracted and shifted byte with the already swapped bytes.

5.  **Return:**
    * `return swapped;`: Returns the `swapped` value, which now contains the byte-swapped result.

**Advantages of the Looping Approach:**

* **Readability:** The looping approach can be easier to understand for some people, as it explicitly shows the byte-by-byte swapping process.
* **Maintainability:** If you needed to adapt this to a different size of variable, the loop approach is easier to modify.

**Disadvantages of the Looping Approach:**

* **Potential Performance Overhead:** In some cases, the loop may introduce a slight performance overhead compared to the direct bit manipulation approach, especially on platforms where the compiler can optimize the direct approach very well. However, compilers are extremely good at optimizing loops, so the difference may be negligible.
