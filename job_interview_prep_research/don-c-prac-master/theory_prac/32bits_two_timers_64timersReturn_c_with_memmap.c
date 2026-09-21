#include <stdint.h>
#include <stdbool.h>
#include <stdio.h> // For printf (example)

// Define memory map addresses for the timers (REPLACE WITH YOUR ACTUAL ADDRESSES)
#define TIMER_A_VALUE_ADDR 0x40000000
#define TIMER_A_OVERFLOW_ADDR 0x40000004
#define TIMER_B_VALUE_ADDR 0x40000008

// Structure to hold the 64-bit timer value
typedef struct {
  uint32_t high;
  uint32_t low;
} timer64_t;

// Function to read a 32-bit register (using volatile pointers)
static inline uint32_t read_register(volatile uint32_t *addr) {
  return *addr;
}

// Function to write to a 32-bit register (using volatile pointers)
static inline void write_register(volatile uint32_t *addr, uint32_t value) {
    *addr = value;
}


// Function to read the 64-bit timer value
timer64_t read_timer64() {
  timer64_t result;
  uint32_t timer_a_val_1, timer_a_val_2;
  uint32_t timer_b_val_1, timer_b_val_2;

  timer_b_val_1 = read_register((volatile uint32_t *)TIMER_B_VALUE_ADDR);
  timer_a_val_1 = read_register((volatile uint32_t *)TIMER_A_VALUE_ADDR);

  timer_a_val_2 = read_register((volatile uint32_t *)TIMER_A_VALUE_ADDR);
  if (timer_a_val_1 != timer_a_val_2) {
    timer_b_val_2 = read_register((volatile uint32_t *)TIMER_B_VALUE_ADDR);
    if (timer_b_val_1 != timer_b_val_2) {
      timer_b_val_1 = timer_b_val_2;
    }
  } else {
    timer_b_val_2 = timer_b_val_1;
  }

  result.low = timer_a_val_2;
  result.high = timer_b_val_2;

  return result;
}

// Example usage (simulated - adapt to your hardware)
int main() {
  // Initialize timers (replace with hardware-specific initialization if needed)
    write_register((volatile uint32_t *)TIMER_A_VALUE_ADDR, 0);
    write_register((volatile uint32_t *)TIMER_A_OVERFLOW_ADDR, 0);
    write_register((volatile uint32_t *)TIMER_B_VALUE_ADDR, 0);


  // Simulate timer ticks (replace with actual timer interrupts or polling)
  for (int i = 0; i < 100000; i++) {
    uint32_t timer_a_val = read_register((volatile uint32_t *)TIMER_A_VALUE_ADDR);
    write_register((volatile uint32_t *)TIMER_A_VALUE_ADDR, timer_a_val + 1); // Increment Timer A

    if (timer_a_val + 1 == 0) { // Simulate overflow
      uint32_t overflow_count = read_register((volatile uint32_t *)TIMER_A_OVERFLOW_ADDR);
      write_register((volatile uint32_t *)TIMER_A_OVERFLOW_ADDR, overflow_count + 1);
      uint32_t timer_b_val = read_register((volatile uint32_t *)TIMER_B_VALUE_ADDR);
      write_register((volatile uint32_t *)TIMER_B_VALUE_ADDR, timer_b_val + 1);
    }

    if (i % 1000 == 0) { // Sample periodically
      timer64_t current_time = read_timer64();
      printf("Time: %u %u\n", current_time.high, current_time.low);
    }
  }

  return 0;
}