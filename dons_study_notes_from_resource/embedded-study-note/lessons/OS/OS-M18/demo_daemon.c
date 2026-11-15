/* ENV: linux */
/* BUILD: -pthread */
/* OS-M18 demo_daemon — solution.c 의 데몬 뼈대를 진짜 프로세스로: 밖에서 kill -TERM 을 보내 깔끔한 종료를 본다.
 * 워커 4, 자식 3 (TERM 에 죽는 둘 + TERM 을 무시하는 하나), 작업 1000 개. 상한 20 s.
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_daemon.c -o daemon   (solution.c 와 같은 디렉터리)
 * run:   ./daemon & sleep 0.5; kill -TERM $!; wait $!; echo "exit=$?"
 */
#define main lesson_main
#include "solution.c"
#undef main

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    daemon_t *d = calloc(1, sizeof *d);
    if (!d || daemon_init(d, 4) != 0) { fprintf(stderr, "init failed\n"); return 1; }
    daemon_spawn(d, 1, 0, NULL);
    daemon_spawn(d, 1, 0, NULL);
    daemon_spawn(d, 2, 0, NULL);
    for (int j = 0; j < 1000; j++) daemon_submit(d, j);
    printf("[daemon %d] workers=%d children=%d,%d,%d jobs=%ld — SIGTERM 을 기다림\n", (int)getpid(), d->nw,
           (int)d->ch[0].pid, (int)d->ch[1].pid, (int)d->ch[2].pid, d->submitted);
    long t0 = now_ms();
    int rc = daemon_run(d, 300, 20000);
    printf("[daemon] run=%d after %ld ms: completed %ld/%ld, sigterm=%ld, eventfd wakeups=%ld, SIGCHLD records=%ld\n",
           rc, now_ms() - t0, d->completed, d->submitted, d->sigterm_seen, d->wakeups_event, d->sigchld_records);
    for (int i = 0; i < d->nch; i++)
        printf("[daemon] child %d pid %d: code=%d sig=%d (%s)%s\n", i, (int)d->ch[i].pid, d->ch[i].code, d->ch[i].sig,
               d->ch[i].sig ? strsignal(d->ch[i].sig) : "exit", d->ch[i].killed_hard ? " ← 유예 후 SIGKILL" : "");
    daemon_close(d);
    free(d);
    return rc == 0 ? 0 : 1;
}
