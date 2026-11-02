/* H8 · the "as asked" snippets. All well-defined (no UB) but surprising.
 * Build WITHOUT -Werror so the warnings are visible (run.sh records them). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* (a) */ printf("(a) -1 < sizeof(int)        -> %d\n", -1 < sizeof(int));
    /* (b) */ int i = -1; unsigned u = 1;
              printf("(b) -1 < 1u                   -> %d\n", i < u);
    /* (c) */ int8_t sx = -1; uint8_t uy = 255; int ix = -1; unsigned iy = 0xFFFFFFFFu;
              printf("(c) (int8_t)-1 == (uint8_t)255 -> %d,  -1 == 0xFFFFFFFFu -> %d\n",
                     sx == uy, ix == iy);
    /* (d) */ size_t n = 3, visited = 0;
              printf("(d) for (size_t k = n-1; k >= 0; k--) visits:");
              for (size_t k = n - 1; k >= 0; k--) {          /* never false */
                  printf(" %zu", k);
                  if (++visited == 5) { printf(" ... (forced break)"); break; }
              }
              printf("\n");
    /* (e) */ const char *s = "abc";
              printf("(e) strlen(\"abc\") - 5 > 0     -> %d  (value %zu)\n",
                     strlen(s) - 5 > 0, strlen(s) - 5);
    /* (f) */ uint32_t deadline = 0xFFFFFFF0u, now = 0x00000010u;  /* 0x20 ticks late */
              printf("(f) now >= deadline           -> %d  (timer wrapped)\n", now >= deadline);
    /* (g) */ int len = -1; size_t sz = len;
              printf("(g) size_t sz = (int)-1       -> %zu\n", sz);
    return 0;
}
