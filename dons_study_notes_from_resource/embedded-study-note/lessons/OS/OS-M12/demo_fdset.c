/* OS-M12 demo_fdset.c — fd >= FD_SETSIZE(1024) 를 select 에 넣으면 이 맥에서 실제로 무슨 일이 나는가 */
/*
 * 정의되지 않은 동작(man select: "undefined if a descriptor value is ... >= FD_SETSIZE")을 일부러 관찰한다.
 * 체크 파일이 아니라 관찰용이다. 두 가지로 빌드해서 비교한다:
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_fdset.c -o fd_strict && ./fd_strict
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -DUNLIMITED demo_fdset.c -o fd_unl && ./fd_unl
 * UNLIMITED 는 _DARWIN_UNLIMITED_SELECT (= _DARWIN_C_SOURCE 를 켜도 같은 효과) → select$DARWIN_EXTSN 변형.
 */
#ifdef UNLIMITED
#define _DARWIN_UNLIMITED_SELECT 1
#endif
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <unistd.h>

struct wrap { fd_set s; unsigned char guard[256]; };   /* fd_set 바로 뒤의 메모리가 바뀌는지 본다 */

static int changed(const unsigned char *g, size_t n) {
    int c = 0;
    for (size_t i = 0; i < n; i++) c += g[i] != 0;
    return c;
}

int main(void) {
    printf("build: %s, FD_SETSIZE=%d, sizeof(fd_set)=%zu bytes\n",
#ifdef UNLIMITED
           "_DARWIN_UNLIMITED_SELECT",
#else
           "strict POSIX",
#endif
           FD_SETSIZE, sizeof(fd_set));

    /* 1) nfds > FD_SETSIZE */
    fd_set e;
    FD_ZERO(&e);
    struct timeval tv = { 0, 0 };
    errno = 0;
    int r = select(FD_SETSIZE + 1, &e, NULL, NULL, &tv);
    printf("select(nfds=%d, empty sets, timeout 0) -> %d (%s)\n", FD_SETSIZE + 1, r, r < 0 ? strerror(errno) : "ok");

    /* 2) FD_SET(1050) on a stack fd_set */
    struct wrap w;
    memset(&w, 0, sizeof w);
    FD_SET(1050, &w.s);
    printf("stack fd_set: FD_SET(1050) -> FD_ISSET=%d, guard bytes after fd_set changed=%d\n",
           FD_ISSET(1050, &w.s) != 0, changed(w.guard, sizeof w.guard));

    /* 3) same on a heap object */
    struct wrap *h = calloc(1, sizeof *h);
    FD_SET(1050, &h->s);
    printf("heap  fd_set: FD_SET(1050) -> FD_ISSET=%d, guard bytes after fd_set changed=%d\n",
           FD_ISSET(1050, &h->s) != 0, changed(h->guard, sizeof h->guard));
    free(h);
    return 0;
}
