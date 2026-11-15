/* main2.c — board_name() 과 board_rev() 를 쓴다 (연습 3) */
#include <stdio.h>
const char *board_name(void);
int board_rev(void);
int main(void) { printf("%s rev%d\n", board_name(), board_rev()); return 0; }
