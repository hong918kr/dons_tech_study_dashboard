/* expect: compile — missing ';' (parser error) */
int main(void)
{
    int sectors = 3
    return sectors - 3;
}
