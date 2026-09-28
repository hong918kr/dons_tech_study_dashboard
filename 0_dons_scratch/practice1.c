uint32_t bit_set(unit32_t value, pos)
{
    uint32_t ret = 0;
    return value | (1U << pos);
}

// value & ~(1U << pos)
// value ^ (1U << pos)
// return (value >> pos) & 0x1 != 0


uint32_t byteswap(uint32_t value)
{
    uint32_t swapped = 0;
    for (int i = 0; i < 4; ++i)
    {
        
        swapped = swapped | ((value >> (i * 8)) & 0xFF) << ((3-i) * 8);
    }
    return swapped;
}

uint32_t reverseBits(uint32_t n)
{
    uint32_t reversed = 0;
    for (int i = 0 ; i < 32; i++)
    {
        reversed = reversed | (n & 0x1) << (32 -i);
        n = n >> 1;
    }
}



