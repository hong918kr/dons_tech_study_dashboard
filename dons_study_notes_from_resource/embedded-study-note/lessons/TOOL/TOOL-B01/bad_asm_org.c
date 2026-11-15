/* expect: assemble — .org going backwards is only detected at layout time,
   i.e. when the assembler actually places bytes into an object file */
int main(void)
{
    __asm__ volatile(".org 0x100\n\tnop\n\t.org 0x10");
    return 0;
}
