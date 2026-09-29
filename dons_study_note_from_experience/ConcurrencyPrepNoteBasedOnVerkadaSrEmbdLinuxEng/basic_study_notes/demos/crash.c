#include <stdio.h>
struct Fix { double lat; };
static double read_lat(const struct Fix *f) { return f->lat; }
int main(void) {
    struct Fix *f = NULL;
    printf("%.3f\n", read_lat(f));
    return 0;
}
