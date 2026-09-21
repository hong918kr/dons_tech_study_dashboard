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