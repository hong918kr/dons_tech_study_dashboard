/* practice (with bad_static_helper.c): link — the only definition is `static`,
   i.e. internal linkage: invisible outside its own translation unit */
unsigned crc_update(unsigned crc, unsigned char b);

int main(void)
{
    return (int)crc_update(0u, 0x42u);
}
