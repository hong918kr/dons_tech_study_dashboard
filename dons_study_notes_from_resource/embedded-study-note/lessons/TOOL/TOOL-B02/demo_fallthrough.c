/* demo_fallthrough.c - TOOL-B02: a "fall through" comment silences GCC's
 * -Wimplicit-fallthrough (level 3 parses comments) but NOT clang's.
 * Also: 0b binary literals are an extension before C23 (-Wpedantic). */
int ft_comment(int x);
int ft_comment(int x)
{
    int r = 0;
    switch (x) {
    case 1:
        r += 1;
        /* fall through */
    case 2:
        r += 2;
        break;
    default:
        break;
    }
    return r + 0b101;
}
