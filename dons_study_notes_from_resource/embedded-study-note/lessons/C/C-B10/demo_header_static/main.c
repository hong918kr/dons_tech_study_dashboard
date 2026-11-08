#include <stdio.h>
#include "cnt.h"
int main(void) {
    inc_a();
    inc_a();
    printf("a.c counter=%d, main.c counter=%d\n", get_a(), counter);
    return 0;
}
