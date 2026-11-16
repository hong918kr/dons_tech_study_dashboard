#include "shell.h"
static int do_reboot(int c, char **v) { (void)c; (void)v; return 0; }
static int do_uptime(int c, char **v) { (void)c; (void)v; return 0; }
SHELL_CMD(uptime, do_uptime, "sys");
SHELL_CMD(reboot, do_reboot, "sys");
