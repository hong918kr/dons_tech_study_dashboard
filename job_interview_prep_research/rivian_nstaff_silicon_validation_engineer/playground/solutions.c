uint32_t field_mask(unsigned msb, unsigned lsb)
{
    return (0xFFFFFFFFu >> (31u - msb)) & (0xFFFFFFFFu << lsb);
}

uint32_t field_get(uint32_t reg, unsigned msb, unsigned lsb)
{
    /* mask the field out, then shift it down so it starts at bit 0 */
    return (reg & field_mask(msb, lsb)) >> lsb;
}

uint32_t field_prep(uint32_t val, unsigned msb, unsigned lsb)
{
    /* shift into place; the mask drops any bits of val that do not fit the field */
    return (val << lsb) & field_mask(msb, lsb);
}

int field_write(const RegDesc *rd, unsigned msb, unsigned lsb, uint32_t val)
{
    if (rd == NULL || msb > 31 || lsb > msb) return -EINVAL;

    /* largest value the field can hold, e.g. [18:16] -> 7 */
    uint32_t max = field_get(0xFFFFFFFFu, msb, lsb);
    if (val > max) return -ERANGE;

    uint32_t old  = reg_read(rd->off);                        /* 1 read */

    /* bits outside the field keep their value, except W1C and SC bits: write those as 0 */
    uint32_t keep = ~field_mask(msb, lsb) & ~(rd->w1c_mask | rd->sc_mask);

    uint32_t nv   = (old & keep) | field_prep(val, msb, lsb);
    reg_write(rd->off, nv);                                   /* 1 write */
    return 0;
}
