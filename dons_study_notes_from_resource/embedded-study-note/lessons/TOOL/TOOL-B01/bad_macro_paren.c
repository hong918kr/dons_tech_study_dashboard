/* expect: compile — the macro itself is broken, but -E happily expands it;
   the error appears when the EXPANDED text is parsed */
#define SQUARE(x)  ((x) * (x)
int main(void)
{
    int n = 3;
    return SQUARE(n) - 9;
}
