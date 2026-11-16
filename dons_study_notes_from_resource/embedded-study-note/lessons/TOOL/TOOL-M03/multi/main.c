/* TOOL-M03 demo: 섹션을 순회하며 "링커가 모은 순서" 그대로 출력 */
#include <stdio.h>
#include <string.h>
#include "shell.h"
int main(void) {
    const struct shell_cmd *b = shell_begin, *e = shell_end;
    printf("table %p..%p = %td bytes, %td entries:", (const void *)b, (const void *)e,
           (const char *)e - (const char *)b, e - b);
    for (const struct shell_cmd *p = b; p < e; p++) printf(" %s(%s)", p->name, p->help);
    printf("\n");
    int dups = 0;
    for (const struct shell_cmd *p = b; p < e; p++)
        for (const struct shell_cmd *q = p + 1; q < e; q++)
            if (strcmp(p->name, q->name) == 0) { printf("duplicate: %s (%s, %s)\n", p->name, p->help, q->help); dups++; }
    return dups ? 3 : 0;
}
