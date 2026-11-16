#include "shell.h"
static int do_led(int c, char **v) { (void)c; (void)v; return 0; }
SHELL_CMD(led, do_led, "led");
