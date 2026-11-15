/* ARM-B01 demo_printf.c — Apple arm64 에서 가변 인자가 어디로 가는지 실제로 본다
 * 빌드: cc -std=c11 -O2 demo_printf.c demo_printf.S -o /tmp/ARM-B01_printf && /tmp/ARM-B01_printf
 */
#include <stdio.h>
void print2_apple(long a, long b);
void print2_linux_style(long a, long b);
int main(void) {
    printf("print2_apple(11, 22):\n");        print2_apple(11, 22);
    printf("print2_linux_style(11, 22):\n");  print2_linux_style(11, 22);
    return 0;
}
