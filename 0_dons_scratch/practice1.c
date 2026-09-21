uint32_t byteSwap(uint32_t value)
{
    uint32_t swapped = 0;
    for (int i = 0; i < 4; i++)
    {
        swapped = swapped | ((value >> (i * 8)) & 0xFF) << ((3-i) * 8);
    }
    return swapped;
}

uint32_t reverseBits(uint32_t n)
{
    uint32_t reversed = 0;
    for (int i = 0; i < 32; i++)
    {
        reversed = reversed | (n & 0x1);
        n = n <<1;
    }
    return reversed;
}