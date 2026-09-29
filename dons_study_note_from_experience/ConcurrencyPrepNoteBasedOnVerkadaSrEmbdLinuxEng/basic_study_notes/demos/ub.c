#include <stdio.h>
int main(void) {
    int buckets[8] = {0};
    int idx = 8;                    /* off-by-one */
    int shift = 33;
    printf("%d\n", buckets[idx]);
    printf("%d\n", 1 << shift);
    return 0;
}
