/* expect: compile — but only at codegen (-S); -fsyntax-only passes */
__attribute__((error("debug_dump() must not be called in release builds")))
void debug_dump(void);

int main(void)
{
    debug_dump();
    return 0;
}
