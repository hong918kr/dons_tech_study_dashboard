#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    open("/dev/urandom", O_RDONLY);
    open("/dev/null", O_WRONLY);
    char cmd[64];
    snprintf(cmd, sizeof cmd, "lsof -p %d | awk 'NR==1||$4 ~ /^[0-9]/'", (int)getpid());
    system(cmd);
    return 0;
}
