

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Function to read a register value
uint32_t mmio_read(uint32_t reg_address) {
    volatile uint32_t *reg_ptr = (volatile uint32_t *)(reg_address);
    return *reg_ptr;
}

// Function to write a register value
void mmio_write(uint32_t reg_address, uint32_t value) {
    volatile uint32_t *reg_ptr = (volatile uint32_t *)(reg_address);
    *reg_ptr = value;
}

// Function to set a bit in a register by index
void mmio_set_bit(uint32_t reg_address, uint8_t bit_index) {
    uint32_t mask = 1UL << bit_index;
    uint32_t current_value = mmio_read(reg_address);
    mmio_write(reg_address, current_value | mask);
}

// Function to clear a bit in a register by index
void mmio_clear_bit(uint32_t reg_address, uint8_t bit_index) {
    uint32_t mask = 1UL << bit_index;
    uint32_t current_value = mmio_read(reg_address);
    mmio_write(reg_address, current_value & (~mask));
}

// Function to toggle a bit in a register by index
void mmio_toggle_bit(uint32_t reg_address, uint8_t bit_index) {
    uint32_t mask = 1UL << bit_index;
    uint32_t current_value = mmio_read(reg_address);
    mmio_write(reg_address, current_value ^ mask);
}

// Function to get the value of a bit in a register by index
bool mmio_get_bit(uint32_t reg_address, uint8_t bit_index) {
    uint32_t mask = 1UL << bit_index;
    uint32_t current_value = mmio_read(reg_address);
    return (current_value & mask) != 0;
}

int main() {
    // Example usage (replace with actual register addresses and bit indices)
    uint32_t reg_address = 0x40000010; // Example register address
    uint8_t bit_index = 3; // Example bit index (4th bit)

    // Initial read
    uint32_t initial_value = mmio_read(reg_address);
    printf("Initial register value: 0x%08X\n", initial_value);

    // Set bit
    mmio_set_bit(reg_address, bit_index);
    printf("After setting bit %u: 0x%08X\n", bit_index, mmio_read(reg_address));

    // Get bit
    bool bit_value = mmio_get_bit(reg_address, bit_index);
    printf("Value of bit %u: %s\n", bit_index, bit_value ? "true" : "false");

    // Clear bit
    mmio_clear_bit(reg_address, bit_index);
    printf("After clearing bit %u: 0x%08X\n", bit_index, mmio_read(reg_address));

    // Toggle bit
    mmio_toggle_bit(reg_address, bit_index);
    printf("After toggling bit %u: 0x%08X\n", bit_index, mmio_read(reg_address));

    return 0;
}