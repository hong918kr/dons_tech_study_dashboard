/* BUILD: -pthread */
/* OS-M09 demo_pthread_rw.c — macOS pthread_rwlock_t 의 실제 동작 관찰 (체크 아님, 관찰 출력)
 *
 *  1) writer 선호인가?  R 을 쥔 상태에서 writer 가 wrlock 에서 잠들면, 새 tryrdlock 은 EBUSY 인가?
 *  2) 같은 스레드에서 R 쥔 채 wrlock (승격)        → EDEADLK 인가, 영원히 멈추나?
 *  3) R 쥔 채 writer 대기 중에 같은 스레드가 rdlock (재귀 읽기) → 멈추나?
 *  4) W 쥔 채 rdlock / wrlock                     → man 페이지의 EDEADLK 가 나오나?
 * 멈출 수 있는 실험은 전부 fork 한 자식에서 alarm(1) 을 걸고 돌린다 → 부모는 절대 멈추지 않는다.
 * "잠들었다" 는 판단은 50 ms sleep 으로 한다 (데모라서 허용. 체크에는 쓰지 않는다).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_pthread_rw.c -o demo_rw && ./demo_rw
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static void msleep(int ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
static const char *ename(int e) {
    switch (e) { case 0: return "0 (ok)"; case EBUSY: return "EBUSY"; case EDEADLK: return "EDEADLK";
                 case EAGAIN: return "EAGAIN"; case EINVAL: return "EINVAL"; default: return strerror(e); }
}
static void *writer_thread(void *arg) {
    (void)arg;
    pthread_rwlock_wrlock(&rw);        /* reader 가 쥐고 있으니 여기서 잠든다 */
    printf("    [writer] got the write lock\n");
    pthread_rwlock_unlock(&rw);
    return NULL;
}

/* fork 한 자식에서 f 를 돌린다. 1 초 안에 안 끝나면 SIGALRM 으로 죽는다 → "멈춤" 으로 보고. */
static void in_child(const char *title, void (*f)(void)) {
    printf("%s\n", title);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { alarm(1); f(); fflush(stdout); _exit(0); }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) printf("    => child killed by signal %d (%s) after 1 s: HUNG, no error returned\n",
                                WTERMSIG(st), WTERMSIG(st) == SIGALRM ? "SIGALRM" : "other");
    else printf("    => child exited %d (did not hang)\n", WEXITSTATUS(st));
}

static void exp1_writer_preference(void) {
    pthread_rwlock_rdlock(&rw);
    pthread_t w; pthread_create(&w, NULL, writer_thread, NULL);
    msleep(50);                                       /* writer 가 wrlock 안에서 잠들 시간 */
    int r = pthread_rwlock_tryrdlock(&rw);            /* 다른 reader(여기서는 같은 스레드)가 지금 들어올 수 있나 */
    printf("    tryrdlock while a writer is blocked: %s\n", ename(r));
    if (r == 0) pthread_rwlock_unlock(&rw);
    printf("    releasing the read lock\n");
    pthread_rwlock_unlock(&rw);
    pthread_join(w, NULL);
}
static void *tryrd_thread(void *arg) {                /* 다른 스레드에서 tryrdlock 한 결과를 돌려준다 */
    int *res = arg;
    *res = pthread_rwlock_tryrdlock(&rw);
    if (*res == 0) pthread_rwlock_unlock(&rw);
    return NULL;
}
static void exp1b_other_thread_tryrd(void) {          /* 1 의 변형: 새 reader 가 "다른 스레드" 일 때 */
    pthread_rwlock_rdlock(&rw);
    pthread_t w; pthread_create(&w, NULL, writer_thread, NULL);
    msleep(50);
    int res = -1;
    pthread_t r2; pthread_create(&r2, NULL, tryrd_thread, &res);
    pthread_join(r2, NULL);
    printf("    tryrdlock from ANOTHER thread while a writer is blocked: %s\n", ename(res));
    pthread_rwlock_unlock(&rw);
    pthread_join(w, NULL);
}
static void exp2_upgrade(void) {
    pthread_rwlock_rdlock(&rw);
    printf("    holding R; trywrlock -> %s\n", ename(pthread_rwlock_trywrlock(&rw)));
    printf("    holding R; calling wrlock ...\n");
    fflush(stdout);
    int r = pthread_rwlock_wrlock(&rw);
    printf("    wrlock returned %s\n", ename(r));
}
static void exp3_recursive_read_with_waiting_writer(void) {
    pthread_rwlock_rdlock(&rw);
    pthread_t w; pthread_create(&w, NULL, writer_thread, NULL);
    msleep(50);
    printf("    holding R, writer blocked; calling rdlock again ...\n");
    fflush(stdout);
    int r = pthread_rwlock_rdlock(&rw);
    printf("    second rdlock returned %s\n", ename(r));
}
static void exp4_write_then_lock_again(void) {
    pthread_rwlock_wrlock(&rw);
    printf("    holding W; tryrdlock -> %s, trywrlock -> %s\n",
           ename(pthread_rwlock_tryrdlock(&rw)), ename(pthread_rwlock_trywrlock(&rw)));
    printf("    holding W; rdlock -> ");
    fflush(stdout);
    printf("%s\n", ename(pthread_rwlock_rdlock(&rw)));
    printf("    holding W; wrlock -> ");
    fflush(stdout);
    printf("%s\n", ename(pthread_rwlock_wrlock(&rw)));
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    in_child("1) R held by main, writer blocked in wrlock, same thread tryrdlock:", exp1_writer_preference);
    in_child("1b) same, but tryrdlock from another thread:", exp1b_other_thread_tryrd);
    in_child("2) upgrade: holding R, call wrlock in the same thread:", exp2_upgrade);
    in_child("3) recursive read while a writer waits:", exp3_recursive_read_with_waiting_writer);
    in_child("4) holding W, lock again:", exp4_write_then_lock_again);
    return 0;
}
