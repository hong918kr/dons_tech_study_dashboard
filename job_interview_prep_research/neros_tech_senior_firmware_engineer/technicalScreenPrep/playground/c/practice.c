#define TIMER_BASE_ADDR (0x00040000U)
#define REG_SECONDS_COUNTER (* (volatile uint32_t *) (TIMER_BASE_ADDR + 0x00U))
#define REG_MINUTES_COUNTER (* (volatile uint32_t *) (TIMER_BASE_ADDR + 0x04U))


4, 씩움직이는거 맞지?

uint64_t GetMergedTimestampBitwise(void)
{
    uint32_t min1, sec1, min2, sec2;
    uint64_t timestamp_64;
    do {
        min1 = REG_MINUTES_COUNTER;
        sec1 = REG_SECONDS_COUNTER;

        min2 = REG_MINUTES_COUNTER;
        sec2 = REG_SECONDS_COUNTER;


    } while ((min1 != min2)  || (sec1 != sec2));


    timestamp_64 = ((uint64_t)min1 << 32) | (uint64_t)sec1;
    return timestamp_64;
}