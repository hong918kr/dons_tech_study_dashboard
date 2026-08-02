#include <stdio.h>
#include <stdbool.h>

#define BUFFER_SIZE (1024 * 1024) // 1MB buffer
#define MAX_ALLOCATIONS 100       // Maximum number of simultaneous allocations

// Structure to represent a memory block
typedef struct {
    size_t size;
    bool free;
    size_t offset; // Offset from the beginning of the buffer
} MemoryBlock;

static char buffer[BUFFER_SIZE];
static MemoryBlock blocks[MAX_ALLOCATIONS];
static size_t num_blocks = 0;

// Initialize the memory management
void memory_init() {
    num_blocks = 0;
    blocks[0].offset = 0;
    blocks[0].size = BUFFER_SIZE;
    blocks[0].free = true;
}

// Allocate memory
void* my_malloc(size_t size) {
    if (size == 0) {
        return NULL; // Or handle as an error
    }

    for (size_t i = 0; i < num_blocks; i++) {
        if (blocks[i].free && blocks[i].size >= size) {
            // Found a suitable free block

            if (blocks[i].size > size) {
                // Split the block if it's larger than needed
                if (num_blocks < MAX_ALLOCATIONS) {
                  MemoryBlock new_block;
                  new_block.offset = blocks[i].offset + size;
                  new_block.size = blocks[i].size - size;
                  new_block.free = true;

                  // shift elements to insert the new block
                  for (size_t j = num_blocks; j > i + 1; j--) {
                    blocks[j] = blocks[j - 1];
                  }
                  blocks[i+1] = new_block;
                  num_blocks++;
                } else {
                  // No space to split, but allocate the whole block
                }
            }
            blocks[i].size = size;
            blocks[i].free = false;
            return buffer + blocks[i].offset;
        }
    }

    return NULL; // No suitable block found
}

// Free memory
void my_free(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    size_t offset = (char*)ptr - buffer;

    for (size_t i = 0; i < num_blocks; i++) {
        if (blocks[i].offset == offset) {
            blocks[i].free = true;

            // Coalesce with adjacent free blocks (important for fragmentation)
            if (i > 0 && blocks[i - 1].free) {
                blocks[i - 1].size += blocks[i].size;
                // Shift elements to remove the coalesced block
                for (size_t j = i; j < num_blocks - 1; j++) {
                    blocks[j] = blocks[j + 1];
                }
                num_blocks--;
                i--; // Recheck the new i
            }
            if (i < num_blocks - 1 && blocks[i + 1].free) {
                blocks[i].size += blocks[i + 1].size;
                // Shift elements to remove the coalesced block
                for (size_t j = i + 1; j < num_blocks - 1; j++) {
                    blocks[j] = blocks[j + 1];
                }
                num_blocks--;
            }
            return;
        }
    }
    // if not found, do nothing.
}

int main() {
    memory_init();

    void* ptr1 = my_malloc(100);
    void* ptr2 = my_malloc(200);
    void* ptr3 = my_malloc(50);
    void* ptr4 = my_malloc(1000);

    printf("ptr1: %p\n", ptr1);
    printf("ptr2: %p\n", ptr2);
    printf("ptr3: %p\n", ptr3);
    printf("ptr4: %p\n", ptr4);

    my_free(ptr2);
    my_free(ptr1);
    my_free(ptr4);
    my_free(ptr3);

    void* ptr5 = my_malloc(500);
    printf("ptr5: %p\n", ptr5);
    my_free(ptr5);

    return 0;
}