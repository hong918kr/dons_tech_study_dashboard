/* expect: compile — call to an undeclared function.
   C89 allowed it (link error later); C99+ clang/gcc make it an error */
int main(void)
{
    return flash_init(0);
}
