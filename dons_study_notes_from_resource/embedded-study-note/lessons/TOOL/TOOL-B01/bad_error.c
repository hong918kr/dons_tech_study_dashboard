/* expect: preprocess — #error fires before any C is parsed */
#ifndef BOARD_REV
#error "BOARD_REV not set: build with -DBOARD_REV=2"
#endif
int main(void) { return BOARD_REV; }
