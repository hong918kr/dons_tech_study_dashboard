#include <stdint.h>

// Assuming these are defined elsewhere, like in your hardware headers
// or you'll need to replace them with the actual register access methods
typedef struct {
    volatile uint32_t CNT; // Timer Counter Register
    // ... other timer registers
} Timer_TypeDef;

extern Timer_TypeDef TimerA; // Base address of Timer A registers
extern Timer_TypeDef TimerB; // Base address of Timer B registers


// Structure to hold the combined 64-bit timer value
typedef struct {
    uint32_t timerA_value;
    uint32_t timerB_overflows; // Count of Timer B overflows
} CombinedTimer_t;


CombinedTimer_t get_64bit_timer_value() {
    static uint32_t timerB_overflow_count = 0; // Static to retain value between calls
    uint32_t timerA_current_value;
    uint32_t timerB_current_value;

    // 1. Read Timer B and check for overflow:
    timerB_current_value = TimerB.CNT; // Read Timer B value *first*

    // Check for overflow (implementation depends on your hardware):
    // Common approach: Check a flag in a status register.  This example is illustrative.
    // Replace with your actual overflow check.
    if (TimerB.some_status_register & OVERFLOW_FLAG) {  // Replace with your actual flag
        timerB_overflow_count++;
        // Clear the overflow flag (if needed by your hardware)
        TimerB.some_status_register &= ~OVERFLOW_FLAG; // Replace with your actual flag clear
    }

    // 2. Read Timer A *after* reading Timer B:
    timerA_current_value = TimerA.CNT; // Read Timer A value

    CombinedTimer_t combined_value;
    combined_value.timerA_value = timerA_current_value;
    combined_value.timerB_overflows = timerB_overflow_count;

    return combined_value;
}


// Example usage:
int main() {
    CombinedTimer_t timer_value;

    // ... your code ...

    timer_value = get_64bit_timer_value();

    uint64_t total_time = ((uint64_t)timer_value.timerB_overflows << 32) | timer_value.timerA_value;

    printf("Timer A: %u\n", timer_value.timerA_value);
    printf("Timer B Overflows: %u\n", timer_value.timerB_overflows);
    printf("Total Time (64-bit): %lu\n", total_time);


    // ... your code ...
    return 0;
}