/* ENV: linux */
/* OS-M15 demo_limits.c — mq 한도 세 가지가 각각 어떤 errno 로 터지는지 실제로 본다
 *
 *   1) /proc/sys/fs/mqueue/{msg_max,msgsize_max,queues_max,msg_default,msgsize_default}, RLIMIT_MSGQUEUE,
 *      sysconf(_SC_MQ_PRIO_MAX)
 *   2) attr = NULL 로 연 큐의 실제 속성 (= msg_default × msgsize_default)
 *   3) 기본 크기 큐를 계속 만든다 → 사용자별 바이트 한도 RLIMIT_MSGQUEUE 에 먼저 걸린다 (EMFILE)
 *   4) 아주 작은 큐(1 × 1 B)를 계속 만든다 → 시스템 전체 개수 한도 queues_max 에 걸린다 (ENOSPC)
 *   만든 큐는 모두 mq_unlink + mq_close.
 *
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 demo_limits.c -o limits && ./limits
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static long rd(const char *n) {
    char p[128];
    snprintf(p, sizeof p, "/proc/sys/fs/mqueue/%s", n);
    FILE *f = fopen(p, "r");
    long v = -1;
    if (f) { if (fscanf(f, "%ld", &v) != 1) v = -1; fclose(f); }
    return v;
}

#define MAXN 600
static mqd_t g_q[MAXN];

static int fill(const char *tag, const struct mq_attr *a, int *err) {
    char nm[64];
    int n = 0;
    for (; n < MAXN; n++) {
        snprintf(nm, sizeof nm, "/OS-M15-lim-%d-%s-%d", (int)getpid(), tag, n);
        g_q[n] = mq_open(nm, O_CREAT | O_EXCL | O_RDWR, 0600, a);
        if (g_q[n] == (mqd_t)-1) { *err = errno; break; }
    }
    for (int i = 0; i < n; i++) {
        snprintf(nm, sizeof nm, "/OS-M15-lim-%d-%s-%d", (int)getpid(), tag, i);
        mq_unlink(nm);
        mq_close(g_q[i]);
    }
    return n;
}

int main(void) {
    struct rlimit rl;
    getrlimit(RLIMIT_MSGQUEUE, &rl);
    printf("msg_max %ld  msgsize_max %ld  queues_max %ld  msg_default %ld  msgsize_default %ld\n", rd("msg_max"),
           rd("msgsize_max"), rd("queues_max"), rd("msg_default"), rd("msgsize_default"));
    printf("RLIMIT_MSGQUEUE cur %ld max %ld   sysconf(_SC_MQ_PRIO_MAX) %ld   euid %d\n", (long)rl.rlim_cur,
           (long)rl.rlim_max, sysconf(_SC_MQ_PRIO_MAX), (int)geteuid());

    char nm[64];
    snprintf(nm, sizeof nm, "/OS-M15-lim-%d-null", (int)getpid());
    mqd_t q = mq_open(nm, O_CREAT | O_EXCL | O_RDWR, 0600, NULL);
    struct mq_attr a;
    if (q != (mqd_t)-1 && mq_getattr(q, &a) == 0)
        printf("attr=NULL 로 연 큐: maxmsg %ld msgsize %ld\n", a.mq_maxmsg, a.mq_msgsize);
    mq_unlink(nm);
    mq_close(q);

    int err = 0;
    int n = fill("def", NULL, &err);
    printf("기본 크기(10 × 8192 B) 큐: %d 개 만든 뒤 %s (errno %d)\n", n, strerror(err), err);

    struct mq_attr tiny = { .mq_maxmsg = 1, .mq_msgsize = 1 };
    err = 0;
    n = fill("tiny", &tiny, &err);
    printf("작은(1 × 1 B) 큐: %d 개 만든 뒤 %s (errno %d)\n", n, strerror(err), err);
    return 0;
}
