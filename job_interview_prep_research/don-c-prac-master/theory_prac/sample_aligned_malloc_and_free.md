```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

// Function to allocate aligned memory
void* aligned_malloc(size_t alignment, size_t size) {
    void* p1; // Original memory block
    void* p2; // Aligned memory address
    size_t offset;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) { // Check if alignment is a power of 2
        errno = EINVAL; // Invalid argument
        return NULL;
    }

    // Allocate extra memory to accommodate alignment and metadata
    p1 = malloc(size + alignment + sizeof(void*));
    if (p1 == NULL) {
        return NULL;
    }

    // Calculate the aligned address
    p2 = (void*)(((uintptr_t)p1 + sizeof(void*) + alignment - 1) & ~(alignment - 1));

    // Store the original pointer before the aligned address for free
    *((void**)((uintptr_t)p2 - sizeof(void*))) = p1;

    return p2;
}

// Function to free aligned memory
void aligned_free(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    // Retrieve the original pointer
    void* original_ptr = *((void**)((uintptr_t)ptr - sizeof(void*)));

    // Free the original memory block
    free(original_ptr);
}

int main() {
    // Example usage: Allocate 100 bytes aligned to 16 bytes
    void* aligned_mem = aligned_malloc(16, 100);
    if (aligned_mem == NULL) {
        perror("aligned_malloc failed");
        return 1;
    }

    printf("Aligned memory address: %p\n", aligned_mem);
    printf("Is aligned to 16: %d\n", ((uintptr_t)aligned_mem % 16) == 0);


    // Example usage: Allocate 200 bytes aligned to 32 bytes
    void* aligned_mem2 = aligned_malloc(32, 200);
    if (aligned_mem2 == NULL) {
        perror("aligned_malloc failed");
        return 1;
    }
    printf("Aligned memory address 2: %p\n", aligned_mem2);
    printf("Is aligned to 32: %d\n", ((uintptr_t)aligned_mem2 % 32) == 0);

    // Free the aligned memory
    aligned_free(aligned_mem);
    aligned_free(aligned_mem2);

    return 0;
}
```

Key improvements and explanations in this version:

*   **Error Handling:** Includes `errno` setting for invalid alignment values (not a power of 2) and checks for `malloc` failures. Using `perror` in `main` provides more informative error messages.
*   **Alignment Check:** Added a clear check (`(alignment & (alignment - 1)) != 0`) to ensure the alignment is a power of 2, which is a requirement for this algorithm.
*   **Clearer Metadata Storage:** Explicitly uses `sizeof(void*)` when storing and retrieving the original pointer, making the code more portable and easier to understand. Stores the original pointer *before* the aligned address.
*   **Type Safety:** Uses `uintptr_t` for pointer arithmetic, which is the correct way to handle pointer-to-integer conversions for arithmetic.
*   **Example Usage:** Provides a more comprehensive example in `main` demonstrating allocation and freeing, and importantly, checks the alignment of the returned pointer.
*   **Comments:** Improved comments to explain the logic more clearly.
*   **Portability:** The code is more portable due to the use of `uintptr_t` and `sizeof(void*)`.

**How it works:**

1.  **Allocate Extra Memory:** The `aligned_malloc` function allocates slightly more memory than requested (`size + alignment + sizeof(void*)`). This extra space is used for alignment and to store a pointer to the original allocated block.

2.  **Calculate Aligned Address:** The core of the alignment is this line:

    ```c
    p2 = (void*)(((uintptr_t)p1 + sizeof(void*) + alignment - 1) & ~(alignment - 1));
    ```

    *   It converts the original pointer `p1` to an integer (`uintptr_t`).
    *   It adds `sizeof(void*)` to reserve space to store the original pointer and `alignment - 1`.
    *   The `& ~(alignment - 1)` part performs a bitwise AND with the bitwise NOT of `alignment - 1`. This effectively rounds the address *down* to the nearest multiple of `alignment`. For example, if `alignment` is 16 (0x10), `alignment - 1` is 15 (0x0F), and `~(alignment - 1)` is 0xFFFFFFF0. The `&` operation clears the lower 4 bits, ensuring alignment to 16.

3.  **Store Original Pointer:** The original pointer `p1` is stored *immediately before* the calculated aligned address `p2`. This is crucial for `aligned_free`.

4.  **`aligned_free`:** The `aligned_free` function retrieves the stored original pointer from the memory just before the passed aligned pointer and then calls `free` on the original pointer.

This improved version addresses the potential issues and provides a more robust and understandable implementation of aligned `malloc` and `free`. Remember to compile with a C99 or later standard (e.g., `gcc -std=c99 ...`).
