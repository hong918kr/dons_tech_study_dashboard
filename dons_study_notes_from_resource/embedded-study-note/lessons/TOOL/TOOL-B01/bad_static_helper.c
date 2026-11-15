/* pair of bad_static_main.c */
static unsigned crc_update(unsigned crc, unsigned char b)
{
    return (crc << 1) ^ b;
}

unsigned crc_use(void) { return crc_update(1u, 2u); }   /* keeps the static fn referenced */
