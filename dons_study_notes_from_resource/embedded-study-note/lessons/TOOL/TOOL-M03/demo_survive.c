/* TOOL-M03 demo (Mac): 어떤 등록 항목이 최종 바이너리까지 살아남나?
 *   c_used   : static + used            c_unused : static + unused (경고만 끔)
 *   c_global : 외부 링키지, 속성 없음     (c_plain = static + 속성 없음 → -DPLAIN 일 때만, -Wunused-const-variable)
 * 섹션 이름 오타도 같이: __shelcmds (s 하나 빠짐) 경계는 링커가 "빈 섹션" 으로 만들어 준다.
 */
#include <stdio.h>
struct cmd { const char *name; int (*fn)(int); const char *help; };
static int f(int x) { return x + 1; }
#define SEC __attribute__((section("__DATA,__shellcmds")))
static const struct cmd c_used   SEC __attribute__((used))   = { "used", f, "h" };
static const struct cmd c_unused SEC __attribute__((unused)) = { "unused", f, "h" };
#ifdef PLAIN
static const struct cmd c_plain  SEC = { "plain", f, "h" };
#endif
const struct cmd c_global SEC = { "global", f, "h" };
extern const struct cmd sb[] __asm("section$start$__DATA$__shellcmds");
extern const struct cmd se[] __asm("section$end$__DATA$__shellcmds");
extern const struct cmd tb[] __asm("section$start$__DATA$__shelcmds");   /* 오타 */
extern const struct cmd te[] __asm("section$end$__DATA$__shelcmds");
int main(void) {
    printf("entries:");
    for (const struct cmd *p = sb; p < se; p++) printf(" %s", p->name);
    printf("  (n=%td)   typo-section n=%td\n", se - sb, te - tb);
    return 0;
}
