/* C-M14 demo_pending_merge.c — 막혀 있는 동안 같은 시그널이 3번 와도 핸들러는 1번 (NVIC 펜딩 비트와 같은 성질)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_pending_merge.c -o demo_pending_merge && ./demo_pending_merge
 * 표준 시그널을 여러 번 큐잉할지는 POSIX 상 구현 정의 — macOS 는 합친다.
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t runs;
static void h(int s) { (void)s; runs = runs + 1; }   /* raise() 로 생긴 시그널이라 읽기도 정의됨 */

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = h;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    sigset_t b, old;
    sigemptyset(&b);
    sigaddset(&b, SIGALRM);
    sigprocmask(SIG_BLOCK, &b, &old);
    raise(SIGALRM); raise(SIGALRM); raise(SIGALRM);
    printf("blocked, raised 3x: runs=%d\n", (int)runs);
    sigprocmask(SIG_SETMASK, &old, NULL);
    printf("after unblock:      runs=%d\n", (int)runs);
    return 0;
}
