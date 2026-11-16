/* TOOL-M03 demo: ASan 이 커스텀 섹션 전역 사이에 레드존을 넣나? (Mac clang / VM gcc · clang)
 * 같은 섹션에 24 바이트 항목 3개를 넣고 섹션 전체 바이트 수와 항목 간격을 잰다.
 *   -DFIX : 항목에 __attribute__((no_sanitize("address")))
 */
#include <stdio.h>
struct cmd { const char *name; int (*fn)(int); const char *help; };
static int f(int x) { return x + 1; }
#ifdef FIX
#  define NOASAN __attribute__((no_sanitize("address")))
#else
#  define NOASAN
#endif
#if defined(__APPLE__)
#  define SEC_ID   "__DATA,__tbl"
#  define SEC_DOT  "__DATA,__tbl2"
extern const struct cmd b1[] __asm("section$start$__DATA$__tbl");
extern const struct cmd e1[] __asm("section$end$__DATA$__tbl");
extern const struct cmd b2[] __asm("section$start$__DATA$__tbl2");
extern const struct cmd e2[] __asm("section$end$__DATA$__tbl2");
#else
#  define SEC_ID   "tbl"          /* C 식별자 이름 */
#  define SEC_DOT  ".tbl.dot"     /* 점이 들어간 이름 → __start_ 없음, 주소 차이로만 잰다 */
extern const struct cmd __start_tbl[], __stop_tbl[];
#  define b1 __start_tbl
#  define e1 __stop_tbl
#endif
#define E(id, sec) static const struct cmd id __attribute__((used, section(sec))) NOASAN = { #id, f, "" }
E(x1, SEC_ID); E(x2, SEC_ID); E(x3, SEC_ID);
E(y1, SEC_DOT); E(y2, SEC_DOT); E(y3, SEC_DOT);
int main(void) {
    printf("sizeof=%zu  id-section: %td bytes for 3 entries, gap x1->x2 = %td\n", sizeof(struct cmd),
           (const char *)e1 - (const char *)b1, (const char *)&x2 - (const char *)&x1);
    printf("            dot-section: gap y1->y2 = %td\n", (const char *)&y2 - (const char *)&y1);
    return 0;
}
