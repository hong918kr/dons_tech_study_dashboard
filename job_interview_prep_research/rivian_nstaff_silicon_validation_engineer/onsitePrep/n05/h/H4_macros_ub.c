/* H4 (UB / compile-error part). Build WITHOUT -Werror, with -fsanitize=undefined.
 *   SQUARE(i++) -> ((i++) * (i++)): two unsequenced modifications of i = UB.
 *   -DSHOW_ELSE_ERROR -> BAD_RESET before else does not compile. */
#include <stdio.h>

#define SQUARE(x)      ((x) * (x))
#define BAD_RESET(s)   (s).len = 0; (s).err = 0

struct chan { int len, err; };

int main(void) {
    int i = 3;
    int r = SQUARE(i++);
    printf("SQUARE(i++) = %d, i = %d   (UB: any result is 'correct')\n", r, i);
#ifdef SHOW_ELSE_ERROR
    struct chan c = {1, 1};
    if (r) BAD_RESET(c); else c.len = 2;
#endif
    return 0;
}
