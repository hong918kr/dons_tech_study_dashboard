/* C-B15 demo: GNU statement expression + __typeof__ 가 어떤 모드에서 경고/에러가 나는가.
 *   cc -std=c11   -Wall -Wextra           -c demo_gnu.c   → 경고 없음
 *   cc -std=c11   -Wall -Wextra -pedantic -c demo_gnu.c   → MAX_RAW 에 경고, MAX_EXT 는 조용
 *   cc -std=c11   -DPLAIN_TYPEOF          -c demo_gnu.c   → 에러: c11 에는 typeof 키워드가 없다
 *   cc -std=gnu11 -DPLAIN_TYPEOF          -c demo_gnu.c   → OK (gnu 모드 / C23 에서는 typeof 키워드)
 */
#define MAX_RAW(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ > b_ ? a_ : b_; })
#define MAX_EXT(a, b) __extension__ ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ > b_ ? a_ : b_; })

int demo(int i, int j) {
    int r = MAX_RAW(i++, j);
    int s = MAX_EXT(i++, j);
#ifdef PLAIN_TYPEOF
    typeof(i) t = i;
    r += t;
#endif
    return r + s;
}
