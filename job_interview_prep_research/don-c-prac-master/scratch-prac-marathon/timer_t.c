//#include <stdint.h>
//#include <stdatomic.h>


void read_reg(uint32_t addr, uint32_t* read_val)
{
    *read_val = *(volatile uint32_t*)addr;
}




#define TIMER_A_READ_REG *(volatile uint32_t*)0x40000000
#define TIMER_B_READ_REG *(volatile uint32_t*)0x40000004

void read_reg(uint32_t addr, uint32_t* read_val)
{
    *read_val = *(volatile uint32_t*)addr;
}



uint64_t read_combined_timer(uint32_t timer_addr_a, uint32_t timber_addr_b)
{
    uint32_t timer_a_value_1;
    uint32_t timer_b_value;
    uint32_t timer_a_value_2;


    // timer_a_value_1 = TIMER_A_READ_REG;
    // timer_b_value = TIMER_B_READ_REG;
    // timer_a_value_2 = TIMER_A_READ_REG;

    read_reg(timer_addr_a, &timer_a_value_1);
    read_reg(timber_addr_b, &timer_b_value);
    read_reg(timer_addr_a, &timer_a_value_2);


    if (timer_a_value_1 != timer_a_value_2)
    {
        read_reg(timber_addr_b, &timer_b_value);
    }
    return ((uint64_t)timer_b_value << 32) | timer_a_value_2;
}


int main()
{
    uint64_t combined_time = read_combined_timer();
}