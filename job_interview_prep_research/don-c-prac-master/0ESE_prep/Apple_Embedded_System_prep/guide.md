Type 1: Bit Manipulation
Core Skill: Controlling hardware registers.
Problem: Write a C function to set a specific bit-field (e.g., 3 bits starting at bit 5) in a 32-bit volatile register.
Key Points: Be ready to explain why the volatile keyword is essential (to prevent compiler optimization) 5, and the necessity of the read-modify-write operation. Use bitmasks, AND (
&) for clearing, OR (|) for setting, and shift operations (<<, >>).
Type 2: Data Structures (Circular Buffer)
Core Skill: Managing data in a resource-constrained environment.
Problem: Implement a byte-addressable circular buffer (ring buffer) in C, providing push and pop functions.
Key Points: Manage head/tail pointers, handle full and empty conditions, and use the modulo operator (%) for index wrapping. Be prepared to discuss concurrency issues in a producer (ISR) and consumer (main loop) scenario.
Type 3: Low-Level Driver Logic (SPI/I2C)
Core Skill: Translating a datasheet into code.
Problem: Given a function spi_transfer(byte), write a function to read a 2-byte temperature value from a sensor.
Key Points: Explain Chip Select (CS) control, sending a command byte, sending dummy bytes to receive data (due to SPI's full-duplex nature), and combining the two received bytes into a 16-bit integer.
