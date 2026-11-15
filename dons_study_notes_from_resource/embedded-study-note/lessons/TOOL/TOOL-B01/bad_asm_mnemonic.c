/* expect: compile with clang (integrated assembler checks inline asm during -S);
           assemble with gcc (gcc -S copies the text, as(1) rejects it) */
int main(void)
{
    __asm__ volatile("frobnicate x0, x1");
    return 0;
}
