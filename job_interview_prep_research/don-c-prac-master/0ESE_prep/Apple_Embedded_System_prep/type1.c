/*
Type 1: Bit Manipulation
Core Skill: Controlling hardware registers.

Problem: Write a C function to set a specific bit-field 
(e.g., 3 bits starting at bit 5) in a 32-bit volatile register.

Key Points: Be ready to explain why the volatile keyword is essential (to prevent compiler optimization) 5, 
and the necessity of the read-modify-write operation. 
Use bitmasks, 
    AND (&) for clearing, 
    OR (|) for setting, and 
    shift operations (<<, >>).
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>




#define clear_bit(val, pos) (val &~(1U << pos))
#define set_bit(val, pos) (val | (1U << pos))

#define clear_bits_by_mask(val, mask) ((val) & ~(mask))
#define set_bits_by_mask(val, mask)   ((val) |  (mask))

volatile uint32_t fake_reg = 0;

#define HW_ADR 0x10081010

void example_bitfield_ops(uint32_t value){
    
    volatile uint32_t* reg = &fake_reg;
    // or
    volatile uint32_t* reg_ = (volatile uint32_t*)HW_ADR;

    uint32_t mask = 0x7 << 5; // 0b0111 << 5, starting 5, and 3 bits
    uint32_t val = *reg;

    val = clear_bits_by_mask(val, mask);
    val = set_bits_by_mask(val, mask);
    *reg = val;
}

// #define clear_bit(val, pos) ((val) & ~(1U << (pos)))
// #define set_bit(val, pos)   ((val) |  (1U << (pos)))

// #define HW_ADR 0x10081010

// void example_bit_ops() {
//     volatile uint32_t* reg = (volatile uint32_t*)HW_ADR;
//     uint32_t val = *reg;           // 레지스터 값 읽기

//     val = clear_bit(val, 5);       // 5번 비트 클리어
//     val = set_bit(val, 5);         // 5번 비트 세팅

//     *reg = val;                    // 레지스터에 다시 쓰기
// }