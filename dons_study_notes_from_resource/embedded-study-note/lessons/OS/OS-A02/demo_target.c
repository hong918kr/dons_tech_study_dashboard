/* ENV: linux */
/* OS-A02 demo — demo_exec 가 execve 후 디버깅하는 "따로 빌드된" 프로그램 (PIE, 심볼 남김)
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -g demo_target.c -o target
 */
#include <stdio.h>
#include <string.h>

__attribute__((noinline)) unsigned checksum_packet(const char *name, int len) { /* FNV-1a */
    unsigned h = 2166136261u;
    for (int i = 0; i < len; i++) { h ^= (unsigned char)name[i]; h *= 16777619u; }
    return h;
}

int main(void) {
    const char *pk[] = { "imu", "gps", "baro" };
    unsigned acc = 0;
    for (int i = 0; i < 3; i++) {
        unsigned h = checksum_packet(pk[i], (int)strlen(pk[i]));
        printf("[target] %-4s %08x\n", pk[i], h);
        acc ^= h;
    }
    return (int)(acc & 0x3f);
}
