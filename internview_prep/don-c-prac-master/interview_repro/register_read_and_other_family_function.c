/*
    DRAM Firmware Engineer Hiring manager (Romi)

1. Implement linked list structure
2. Implement read_reg

*/

struct ListNode {
    int val;
    struct ListNode* next;
};

// typedef struct Node_ {
//     int val;
//     struct Node_* next;
// } Node;


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Function to set a specific bit in a uint32_t variable
bool set_bit(uint32_t *variable, uint8_t bit_position) {
    if (bit_position >= 32) {
        return false; // Invalid bit position
    }
    *variable |= (1UL << bit_position); // Set the bit using bitwise OR
    return true;
}

// Function to clear a specific bit in a uint32_t variable
bool clear_bit(uint32_t *variable, uint8_t bit_position) {
    if (bit_position >= 32) {
        return false; // Invalid bit position
    }
    *variable &= ~(1UL << bit_position); // Clear the bit using bitwise AND and NOT
    return true;
}

// Function to get the value of a specific bit in a uint32_t variable
bool get_bit(uint32_t variable, uint8_t bit_position) {
    if (bit_position >= 32) {
        return false; // Invalid bit position
    }
    return (variable >> bit_position) & 1; // Get the bit using right shift and bitwise AND
}

int main() {
    uint32_t my_variable = 0;

    printf("Initial value: %u (binary: ", my_variable);
    for (int i = 31; i >= 0; i--) {
        printf("%d", get_bit(my_variable, i));
    }
    printf(")\n");

    if (set_bit(&my_variable, 5)) {
        printf("Set bit 5: %u (binary: ", my_variable);
        for (int i = 31; i >= 0; i--) {
            printf("%d", get_bit(my_variable, i));
        }
        printf(")\n");
    } else {
        printf("Error setting bit.\n");
    }

    if (set_bit(&my_variable, 10)) {
        printf("Set bit 10: %u (binary: ", my_variable);
        for (int i = 31; i >= 0; i--) {
            printf("%d", get_bit(my_variable, i));
        }
        printf(")\n");
    } else {
        printf("Error setting bit.\n");
    }

    if (clear_bit(&my_variable, 5)) {
        printf("Cleared bit 5: %u (binary: ", my_variable);
        for (int i = 31; i >= 0; i--) {
            printf("%d", get_bit(my_variable, i));
        }
        printf(")\n");
    } else {
        printf("Error clearing bit.\n");
    }
    
    if(get_bit(my_variable,10)){
        printf("Bit 10 is set\n");
    }else{
        printf("Bit 10 is not set\n");
    }

    if(!get_bit(my_variable,5)){
        printf("Bit 5 is not set\n");
    }else{
        printf("Bit 5 is set\n");
    }

    if(!set_bit(&my_variable, 32)){
        printf("Bit 32 can not be set, it is out of range\n");
    }

    return 0;
}






#if 0


#include <stdio.h>
#include <stdint.h>  // this is for the uint32_t 
#define myReg (0x40000000)

#define REGISTER_ADDRESS 0x40001000UL 
#define REGISTER_ADDRESS_UINT (uintptr_t)0x40001000
uint32_t get_register(uint32_t addr)
{   
    volatile uint32_t *ptr = (volatile uint32_t*)addr;
    return *ptr;    
}

void set_register(uint32_t addr, uint32_t val)
{
    volatile uint32_t *ptr = (volatile uint32_t*)addr;
    *ptr = val;
}
uint32_t read_uint32_from_address(uint32_t address) {
    // VERY IMPORTANT: Check for null pointers!
    // if (address == 0) {
    //     fprintf(stderr, "Error: Null pointer provided.\n");
    //     return 0; // Or handle the error in a more appropriate way (e.g., return an error code).
    // }

    // IMPORTANT: Use volatile to prevent compiler optimizations.
    printf("inside address :%x\n", address);
    printf("inside address :%x\n", (volatile uint32_t *)address);
    volatile uint64_t *ptr = (volatile uint32_t *)address;
    printf("inside ptr address :%x\n", ptr);
    printf("inside *ptr  :%x\n", *ptr);
    printf("2\n");
    // Dereference the pointer to read the value.
    return *ptr;
}

int main () {


    // 1. Cast the address to a pointer of the appropriate type.
    // Assuming the register holds a 32-bit unsigned integer:
    uint32_t *register_ptr = (uint32_t *)REGISTER_ADDRESS;

    // OR using uintptr_t for casting (more robust):
    uint32_t *register_ptr_uint = (uint32_t *)REGISTER_ADDRESS_UINT;


    // 2. Dereference the pointer to get the value.
    uint32_t register_value = *register_ptr;
    uint32_t register_value_uint = *register_ptr_uint;

    printf("Register Value (UL cast): 0x%X\n", register_value);
    printf("Register Value (uintptr_t cast): 0x%X\n", register_value_uint);


    // uint32_t my_value = 0x12345678;

    // uint32_t address_of_value = 0x10000000;
    // printf("Address of my_value: 0x%x\n", address_of_value);
    // printf("my_value by dereferenceing: 0x%x\n", (uint32_t*)address_of_value);
    //printf("my_value on address: 0x%08x\n", *((uint32_t*)address_of_value));
    // uint32_t read_value = read_uint32_from_address(address_of_value);
    // printf("Value at that address: 0x%08x\n", read_value);

    // printf("1\n");
    // uint32_t val = 0xABCD1111;
    // uint32_t* address_ = &val;
    // uint32_t addr = address_;

    
    // printf("address: %x,  \n", myReg);
    // printf("get_register(addr): %x", get_register(myReg));

    //printf("Register at 0x%08X: 0x%08X\n", register_address_1, value1);
    //printf("Register at 0x%08X: 0x%08X\n", register_address_2, value2);

    return 0;
}

#endif