/* C-B14 quiz_demo.c — 12개 퀴즈 스니펫을 "있는 그대로" 컴파일해 경고와 실제 결과를 본다.
 * 일부러 UB / 경고가 들어 있다 → -Werror 없이 빌드한다.
 *   cc -std=c11 -Wall -Wextra -O0 quiz_demo.c -o quiz_demo && ./quiz_demo
 *   cc -std=c11 -Wall -Wextra -DSHOW_Q6 -fsyntax-only quiz_demo.c   # Q6 컴파일 에러 보기
 */
#include <stdint.h>
#include <stdio.h>

#define MASK 0x20u
#define VAL  0x20u

static int order[2], n_order;
static int f(void) { order[n_order++] = 'f'; return 1; }
static int g(void) { order[n_order++] = 'g'; return 2; }

int main(void) {
    /* Q1 */ uint32_t reg = 0x20;
    printf("Q1  reg & MASK == VAL      -> %u\n", reg & MASK == VAL);
    /* Q2 */ unsigned a = 1;
    printf("Q2  a << 2 + 1             -> %u\n", a << 2 + 1);
    /* Q3 */ int arr[] = {10, 20, 30}; int *p = arr;
    int v = *p++;
    printf("Q3  v = *p++ -> v=%d, *p=%d | ", v, *p);
    p = arr; (*p)++;
    printf("(*p)++ -> arr[0]=%d, p==arr:%d\n", arr[0], p == arr);
    /* Q4 */ int x, y = 7, z = 7;
    x = y == z;
    printf("Q4  x = y == z             -> x=%d\n", x);
    /* Q5 */ unsigned flag = 0x02, mask = 0x04;
    printf("Q5  !flag & mask           -> %u\n", !flag & mask);
    /* Q6 */ int sel = 1, b = 2, c = 3, d = 9;
#ifdef SHOW_Q6
    sel ? b : c = d;           /* C: 컴파일 에러. C++: sel ? b : (c = d) */
#endif
    (void)sel; (void)b; (void)c; (void)d;
    /* Q7 */ int i = 5;
    i = i++;
    printf("Q7  i = i++ (i=5)          -> i=%d   [UB]\n", i);
    /* Q8 */ int s = f() + g();
    printf("Q8  f() + g()              -> %d, call order %c%c [unspecified]\n", s, order[0], order[1]);
    /* Q9 */ int t[4] = {0, 0, 0, 0}; int k = 1;
    t[k] = k++;
    printf("Q9  t[k] = k++ (k=1)       -> t={%d,%d,%d,%d} k=%d [UB]\n", t[0], t[1], t[2], t[3], k);
    /* Q10 */ int cnt = 0, pos = 1;
    if ((pos > 0) || (++cnt > 0)) { }
    int cnt_or = cnt;
    if ((pos > 0) && (++cnt > 0)) { }
    printf("Q10 || then &&             -> cnt after ||=%d, after &&=%d\n", cnt_or, cnt);
    /* Q11 */ int w;
    w = (1, 2, 3);
    int w2;
    w2 = 1, 2, 3;
    printf("Q11 (1,2,3) -> %d ; w2 = 1, 2, 3 -> %d\n", w, w2);
    /* Q12 */ int nn = 5;
    size_t sz = sizeof nn++;
    printf("Q12 sizeof nn++            -> %zu, nn=%d\n", sz, nn);
    return 0;
}
