/* ARM-A09 demo_dylib_main.c — scaled(5) = 5*3 + 3 + 100 = 118 */
#include <stdio.h>
long scaled(long raw);
extern long *ptr_to_bias, *ptr_to_gain;
int main(void) {
    printf("scaled(5)=%ld *ptr_to_bias=%ld *ptr_to_gain=%ld\n", scaled(5), *ptr_to_bias, *ptr_to_gain);
    return scaled(5) != 118;
}
