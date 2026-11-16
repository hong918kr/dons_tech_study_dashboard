/* TOOL-M07 demo_wp.c — 워치포인트 실험용 작은 프로그램 (demo_lldb.sh 가 lldb 로 돌린다)
 *   ./demo_wp limits   : 8바이트 칸 8개에 차례로 쓰기 (하드웨어 워치포인트 개수 · 크기 실험)
 *   ./demo_wp modify   : 같은 값을 1000번 다시 쓰기 (write vs modify 타입)
 *   ./demo_wp kernel   : read(2) 로 커널이 버퍼에 쓰기 + 유저 코드가 쓰기
 *   ./demo_wp neighbour: 4바이트 필드 옆 필드에 쓰기 (expression 워치포인트 기본 크기 = 8)
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct {
    uint64_t w[8];
    uint8_t  big[64];
} g __attribute__((aligned(64)));

struct pair {
    uint32_t state;
    uint32_t counter;
} p;

volatile uint32_t same;
unsigned char kbuf[16];

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "limits";
    if (strcmp(mode, "limits") == 0) {
        for (int i = 0; i < 8; i++)
            g.w[i] = (uint64_t)i + 1;
        g.big[40] = 7;
        printf("limits %llu\n", (unsigned long long)(g.w[7] + g.big[40]));
    } else if (strcmp(mode, "modify") == 0) {
        for (int i = 0; i < 1000; i++)
            same = 5;                 /* 값이 안 바뀌는 쓰기 999번 */
        same = 6;
        printf("modify %u\n", same);
    } else if (strcmp(mode, "kernel") == 0) {
        int fd = open("/dev/urandom", O_RDONLY);
        ssize_t n = read(fd, kbuf, 8);           /* 커널이 kbuf 에 쓴다 */
        close(fd);
        printf("kernel wrote %zd bytes\n", n);
        memset(kbuf, 0x5a, 4);                   /* 유저 코드가 kbuf 에 쓴다 */
        printf("user wrote 0x%x\n", kbuf[0]);
    } else if (strcmp(mode, "neighbour") == 0) {
        for (int i = 0; i < 3; i++)
            p.counter++;              /* 옆 필드 */
        p.state = 2;
        printf("neighbour %u %u\n", p.state, p.counter);
    }
    return 0;
}
