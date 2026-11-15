/* ENV: linux */
/* BUILD: -pthread */
/* OS-M09 demo_glibc_rw.c — glibc pthread_rwlock_t 의 선호 정책 관찰 (Lima VM, 체크 아님)
 *
 *  pthread_rwlockattr_setkind_np 의 네 가지 kind 마다:
 *   a) R 을 쥔 상태에서 writer 가 wrlock 에서 잠들면, 다른 스레드의 tryrdlock 이 들어오나 (0) / 막히나 (EBUSY)?
 *   b) 같은 상황에서 R 을 쥔 스레드가 rdlock 을 한 번 더 하면 (재귀 읽기) 멈추나?
 *   c) R 을 쥔 채 wrlock (승격) 은?
 *  멈출 수 있는 실험은 fork 한 자식 + alarm(1). "잠들었다" 판단은 50 ms sleep (데모라서 허용).
 *
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_glibc_rw.c -o demo_glibc_rw && ./demo_glibc_rw
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pthread_rwlock_t rw;
static void msleep(int ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
static const char *ename(int e) {
    switch (e) { case 0: return "0 (ok)"; case EBUSY: return "EBUSY"; case EDEADLK: return "EDEADLK"; default: return strerror(e); }
}
static void *writer_thread(void *arg) { (void)arg; pthread_rwlock_wrlock(&rw); pthread_rwlock_unlock(&rw); return NULL; }
static void *tryrd_thread(void *arg) {
    int *res = arg;
    *res = pthread_rwlock_tryrdlock(&rw);
    if (*res == 0) pthread_rwlock_unlock(&rw);
    return NULL;
}
static void init_kind(int kind) {
    pthread_rwlockattr_t at;
    pthread_rwlockattr_init(&at);
    pthread_rwlockattr_setkind_np(&at, kind);
    pthread_rwlock_init(&rw, &at);
    pthread_rwlockattr_destroy(&at);
}
static int g_kind;
static void exp_a(void) {
    init_kind(g_kind);
    pthread_rwlock_rdlock(&rw);
    pthread_t w; pthread_create(&w, NULL, writer_thread, NULL);
    msleep(50);
    int res = -1; pthread_t r; pthread_create(&r, NULL, tryrd_thread, &res); pthread_join(r, NULL);
    printf("    a) other-thread tryrdlock while writer blocked: %s\n", ename(res));
    pthread_rwlock_unlock(&rw);
    pthread_join(w, NULL);
}
static void exp_b(void) {
    init_kind(g_kind);
    pthread_rwlock_rdlock(&rw);
    pthread_t w; pthread_create(&w, NULL, writer_thread, NULL);
    msleep(50);
    printf("    b) recursive rdlock while writer blocked: ");
    fflush(stdout);
    int r = pthread_rwlock_rdlock(&rw);
    printf("%s\n", ename(r));
    pthread_rwlock_unlock(&rw); pthread_rwlock_unlock(&rw);
    pthread_join(w, NULL);
}
static void exp_c(void) {
    init_kind(g_kind);
    pthread_rwlock_rdlock(&rw);
    printf("    c) holding R, wrlock: ");
    fflush(stdout);
    printf("%s\n", ename(pthread_rwlock_wrlock(&rw)));
}
static void in_child(void (*f)(void)) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { alarm(1); f(); fflush(stdout); _exit(0); }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) printf("HUNG (child killed by %s after 1 s)\n", WTERMSIG(st) == SIGALRM ? "SIGALRM" : "a signal");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const struct { int kind; const char *name; } kinds[] = {
        { PTHREAD_RWLOCK_DEFAULT_NP,                   "PTHREAD_RWLOCK_DEFAULT_NP" },
        { PTHREAD_RWLOCK_PREFER_READER_NP,             "PTHREAD_RWLOCK_PREFER_READER_NP" },
        { PTHREAD_RWLOCK_PREFER_WRITER_NP,             "PTHREAD_RWLOCK_PREFER_WRITER_NP" },
        { PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP, "PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP" },
    };
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        printf("%s (= %d):\n", kinds[i].name, kinds[i].kind);
        g_kind = kinds[i].kind;
        in_child(exp_a);
        in_child(exp_b);
        in_child(exp_c);
    }
    return 0;
}
