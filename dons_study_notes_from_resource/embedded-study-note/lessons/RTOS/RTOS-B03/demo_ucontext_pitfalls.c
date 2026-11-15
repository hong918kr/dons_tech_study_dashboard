/* RTOS-B03 demo: ucontext 함정 7가지를 이 맥(macOS arm64)에서 직접 확인
 *
 *   P1 getcontext 는 uc_link / uc_stack 을 덮어쓴다 → 순서는 getcontext → 필드 설정 → makecontext
 *   P2 makecontext 는 스택 전체를 0 으로 지운다 → 칠하기(0xA5)는 makecontext 뒤에
 *   P3 makecontext 의 가변 인자는 int 다 → 64비트 포인터는 상위 32비트가 잘린다
 *   P4 uc_stack 없이 makecontext → swapcontext 가 -1 / EINVAL
 *   P5 끝난 문맥으로 다시 swapcontext → -1 / EINVAL (macOS: _ctx_done 이 uc_mcsize = 0)
 *   P6 ucontext_t 를 구조체 대입으로 복사 → uc_mcontext 포인터는 원본을 가리킨다
 *   P7 uc_link == NULL 인 문맥이 return → 프로세스가 _exit(0) (stdio 버퍼는 flush 되지 않는다)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_ucontext_pitfalls.c -o demo_pitfalls && ./demo_pitfalls
 *   (P7 의 "사라진 출력" 은 stdout 이 파일/파이프일 때 보인다: ./demo_pitfalls | cat)
 */
#define _XOPEN_SOURCE 600
#include <ucontext.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

static ucontext_t g_main, g_t;
static _Alignas(16) uint8_t g_stk[64 * 1024];
static void *g_seen_ptr;

static void hello(void) { }
static void take_ptr(void *p) { g_seen_ptr = p; }   /* makecontext 에 포인터를 "그냥" 넘긴 경우 */

static size_t count_byte(const uint8_t *p, size_t n, uint8_t b) {
    size_t c = 0; for (size_t i = 0; i < n; i++) c += (p[i] == b); return c;
}

int main(void) {
    /* P1 */
    g_t.uc_link = &g_main;
    g_t.uc_stack.ss_sp = g_stk; g_t.uc_stack.ss_size = sizeof g_stk;
    getcontext(&g_t);
    printf("P1 set uc_link=%p, uc_stack.ss_sp=%p BEFORE getcontext -> after: uc_link=%p ss_sp=%p ss_size=%zu\n",
           (void *)&g_main, (void *)g_stk, (void *)g_t.uc_link, g_t.uc_stack.ss_sp, (size_t)g_t.uc_stack.ss_size);

    /* P2 */
    memset(g_stk, 0xA5, sizeof g_stk);
    g_t.uc_stack.ss_sp = g_stk; g_t.uc_stack.ss_size = sizeof g_stk; g_t.uc_stack.ss_flags = 0;
    g_t.uc_link = &g_main;
    makecontext(&g_t, hello, 0);
    printf("P2 painted 0xA5 BEFORE makecontext -> 0xA5 bytes left: %zu / %zu (zero bytes: %zu)\n",
           count_byte(g_stk, sizeof g_stk, 0xA5), sizeof g_stk, count_byte(g_stk, sizeof g_stk, 0));
    swapcontext(&g_main, &g_t);

    /* P3 */
    void *p = &g_main;
    getcontext(&g_t);
    g_t.uc_stack.ss_sp = g_stk; g_t.uc_stack.ss_size = sizeof g_stk; g_t.uc_stack.ss_flags = 0;
    g_t.uc_link = &g_main;
    makecontext(&g_t, (void (*)(void))take_ptr, 1, p);
    swapcontext(&g_main, &g_t);
    printf("P3 passed pointer %p -> task saw %p\n", p, g_seen_ptr);

    /* P4 */
    ucontext_t u;
    getcontext(&u);
    u.uc_stack.ss_sp = NULL; u.uc_stack.ss_size = 0; u.uc_link = &g_main;
    makecontext(&u, hello, 0);
    errno = 0;
    int r = swapcontext(&g_main, &u);
    printf("P4 no stack: swapcontext=%d errno=%d (%s), uc_mcsize=%zu\n", r, errno, strerror(errno), (size_t)u.uc_mcsize);

    /* P5 */
    getcontext(&g_t);
    g_t.uc_stack.ss_sp = g_stk; g_t.uc_stack.ss_size = sizeof g_stk; g_t.uc_stack.ss_flags = 0;
    g_t.uc_link = &g_main;
    makecontext(&g_t, hello, 0);
    int r1 = swapcontext(&g_main, &g_t);
    errno = 0;
    int r2 = swapcontext(&g_main, &g_t);
    printf("P5 first swap=%d, swap to finished ctx=%d errno=%d, uc_mcsize=%zu\n", r1, r2, errno, (size_t)g_t.uc_mcsize);

    /* P6 */
    ucontext_t copy;
    getcontext(&u);
    copy = u;
    printf("P6 copy.uc_mcontext=%p  &copy.__mcontext_data=%p  &u.__mcontext_data=%p -> points to %s\n",
           (void *)copy.uc_mcontext, (void *)&copy.__mcontext_data, (void *)&u.__mcontext_data,
           (void *)copy.uc_mcontext == (void *)&u.__mcontext_data ? "ORIGINAL" : "copy");

    /* P7 */
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        getcontext(&g_t);
        g_t.uc_stack.ss_sp = g_stk; g_t.uc_stack.ss_size = sizeof g_stk; g_t.uc_stack.ss_flags = 0;
        g_t.uc_link = NULL;
        makecontext(&g_t, hello, 0);
        printf("P7 child: this line sits in the stdio buffer...\n");
        swapcontext(&g_main, &g_t);
        printf("P7 child: after swapcontext (never printed)\n");
        return 7;
    }
    int st = 0;
    waitpid(pid, &st, 0);
    printf("P7 uc_link=NULL: child exited=%d status=%d signaled=%d\n",
           WIFEXITED(st), WIFEXITED(st) ? WEXITSTATUS(st) : -1, WIFSIGNALED(st));
    return 0;
}
