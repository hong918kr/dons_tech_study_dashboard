#include "shell.h"
static int do_reboot(int c, char **v) { (void)c; (void)v; return 1; }
SHELL_CMD(reboot, do_reboot, "pwr");   /* 다른 모듈이 같은 이름 — 링크는 성공한다 (둘 다 static) */
