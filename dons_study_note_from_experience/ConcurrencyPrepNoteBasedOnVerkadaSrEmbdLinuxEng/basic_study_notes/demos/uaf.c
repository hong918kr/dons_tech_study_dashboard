#include <stdio.h>
#include <stdlib.h>

struct Config { int refcnt; int timeout_ms; };

int main(void) {
    struct Config *c = malloc(sizeof *c);
    c->refcnt = 1; c->timeout_ms = 500;
    free(c);                       /* publish 하면서 해제 */
    printf("timeout=%d\n", c->timeout_ms);   /* 아직 읽는 reader */
    return 0;
}
