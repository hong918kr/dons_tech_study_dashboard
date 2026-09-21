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

// Function to set bits in a register
void mmio_set_bits(uint32_t reg_address, uint32_t mask) {
    uint32_t current_value = mmio_read(reg_address);
    mmio_write(reg_address, current_value | mask);
}

// Function to clear bits in a register
void mmio_clear_bits(uint32_t reg_address, uint32_t mask) {
    uint32_t current_value = mmio_read(reg_address);
    mmio_write(reg_address, current_value & (~mask));
}

// Function to toggle bits in a register
void mmio_toggle_bits(uint32_t reg_address, uint32_t mask) {
    uint32_t current_value = mmio_read(reg_address);
    mmio_write(reg_address, current_value ^ mask);
}

int main() {
    // Example usage (replace with actual register addresses and masks)
    uint32_t reg_address = 0x40000010; // Example register address
    uint32_t bit_mask = 0x0000000F; // Example bit mask

    // Initial read
    uint32_t initial_value = mmio_read(reg_address);
    printf("Initial register value: 0x%08X\n", initial_value);

    // Set bits
    mmio_set_bits(reg_address, bit_mask);
    printf("After setting bits: 0x%08X\n", mmio_read(reg_address));

    // Clear bits
    mmio_clear_bits(reg_address, bit_mask);
    printf("After clearing bits: 0x%08X\n", mmio_read(reg_address));

    // Toggle bits
    mmio_toggle_bits(reg_address, bit_mask);
    printf("After toggling bits: 0x%08X\n", mmio_read(reg_address));

    //writing a specific value
    mmio_write(reg_address, 0x12345678);
    printf("After writing specific value: 0x%08X\n", mmio_read(reg_address));

    return 0;
}

