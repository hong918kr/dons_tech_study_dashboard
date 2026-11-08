/* C-M03 데모: "그냥 int" 비트필드의 부호는 구현 정의 (C11 6.7.2p5) — 호스트에서 실행해 관찰한다.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_plain_int.c -o demo_plain_int && ./demo_plain_int
 *   cc -std=c11 -Wall -Wextra -O2 -funsigned-bitfields demo_plain_int.c -o d2 && ./d2   # GCC 옵션, clang 은?
 *   cc -std=c11 -Wall -Wextra -DSHOW_WARNING -c demo_plain_int.c -o /dev/null           # 상수 대입 경고
 */
#include <stdio.h>

struct flags {
    int          plain : 3;   /* signed? unsigned? → 구현 정의 */
    signed int   sgn   : 3;   /* 항상 signed: -4..3  */
    unsigned int uns   : 3;   /* 항상 unsigned: 0..7 */
    int          ready : 1;   /* clang/GCC 에선 signed 1비트 → 값은 0 또는 -1 뿐 */
};

int main(int argc, char **argv) {
    (void)argv;
    struct flags f = { 0, 0, 0, 0 };
    int seven = 6 + argc;                 /* 런타임 값 7 (상수면 컴파일러가 경고를 낸다) */
    unsigned useven = (unsigned)seven;
    f.plain = seven;                      /* 0b111 을 3비트 signed 에 → 범위 밖, 결과는 구현 정의 (6.3.1.3p3) */
    f.sgn = seven;
    f.uns = useven;
    f.ready = argc;                       /* 1 을 저장 */
    printf("plain=%d  signed=%d  unsigned=%u\n", f.plain, f.sgn, (unsigned)f.uns);
    printf("plain is %s on this compiler\n", f.plain < 0 ? "SIGNED" : "UNSIGNED");
    printf("ready=%d  (ready == 1) -> %s   (ready != 0) -> %s\n",
           f.ready, f.ready == 1 ? "true" : "false", f.ready != 0 ? "true" : "false");
    return 0;
}

#ifdef SHOW_WARNING
struct one { int plain : 3; int ready : 1; };
int show(void) { struct one o = { .plain = 7, .ready = 1 }; return o.plain + o.ready; }
#endif
