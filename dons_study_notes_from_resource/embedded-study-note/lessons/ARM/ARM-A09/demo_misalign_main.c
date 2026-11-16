/* ARM-A09 demo_misalign_main.c — f2() 는 5 를 읽어야 한다 */
#include <stdio.h>
long f2(void);
extern long odd;
int main(void) { printf("f2()=0x%lx (expected 0x5)  &odd=%p\n", f2(), (void *)&odd); return f2() != 5; }
