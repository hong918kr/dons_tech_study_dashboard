#include <stdint.h> // uint32_t 

// write
#define writel(val, addr) (*(volatile uint32_t*) (addr) = (uint32_t)(val))

// Read
#define readl(addr) (*(volatile uint32_t *)(addr))


/*
why use these macro instead of direct use of pointer

1. readability and consistency
2. Safety
- including volatile keyword helps prevent compiler optimization issues and 
explicitly specifies the data size 
(writeb for 8 bit)
(writew for 16 bit)
(writel for 32 bit)

3. Cross-Platform Compatibility
- OS(linux kernel) and bootloader (U-BOOT) need to support various architecture(ARM, x86)

*/