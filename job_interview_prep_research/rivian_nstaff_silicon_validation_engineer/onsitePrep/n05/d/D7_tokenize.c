#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
static bool is_sep(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Split line in place: separators become '\0', argv[i] points into line.
 * Returns argc, or -1 if there are more than max_args tokens. */
int tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;
    for (;;) {
        while (is_sep(*p))
            p++;                          /* skip run of separators */
        if (*p == '\0')
            break;
        if (argc == max_args)
            return -1;
        argv[argc++] = p;                 /* token start */
        while (*p != '\0' && !is_sep(*p))
            p++;                          /* token body */
        if (*p == '\0')
            break;
        *p++ = '\0';                      /* terminate token, step past */
    }
    return argc;
}
/* ---- end model answer ---- */

int main(void)
{
    char *av[4];

    char l1[] = "wr 0x40000000 0x55";
    assert(tokenize(l1, av, 4) == 3);
    assert(strcmp(av[0], "wr") == 0 && strcmp(av[1], "0x40000000") == 0 &&
           strcmp(av[2], "0x55") == 0);
    assert(av[0] == l1 && av[1] == l1 + 3);                 /* points into the line */

    char l2[] = "  rd\t 0x10  \r\n";
    assert(tokenize(l2, av, 4) == 2);
    assert(strcmp(av[0], "rd") == 0 && strcmp(av[1], "0x10") == 0);

    char l3[] = "";
    assert(tokenize(l3, av, 4) == 0);
    char l4[] = "   \t\r\n";
    assert(tokenize(l4, av, 4) == 0);

    char l5[] = "help";
    assert(tokenize(l5, av, 4) == 1 && strcmp(av[0], "help") == 0);

    char l6[] = "a b c d";
    assert(tokenize(l6, av, 4) == 4 && strcmp(av[3], "d") == 0);
    char l7[] = "a b c d e";
    assert(tokenize(l7, av, 4) == -1);
    char l8[] = "x";
    assert(tokenize(l8, av, 0) == -1);
    char l9[] = "  ";
    assert(tokenize(l9, av, 0) == 0);

    puts("D7 PASS");
    return 0;
}
