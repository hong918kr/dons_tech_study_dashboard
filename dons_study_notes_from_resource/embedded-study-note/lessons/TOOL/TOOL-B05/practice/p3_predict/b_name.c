/* b_name.c → libb.a : 보드 B (board_rev 도 같은 멤버에 있다) */
const char *board_name(void);
int board_rev(void);
const char *board_name(void) { return "B"; }
int board_rev(void) { return 2; }
