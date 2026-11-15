/* OS-B01 프로세스 생성: fork/exec/wait로 미니 셸
 *
 *   Part A  tokenize() → spawn() (fork → execvp, 실패하면 자식이 _exit(127/126)) → wait_status()
 *           → status_code() (WIFEXITED → WEXITSTATUS, WIFSIGNALED → 128+WTERMSIG), 그리고 sh_line():
 *           한 줄 실행 + 내장 명령 cd / exit
 *   Part B  "cmd &" 백그라운드 + SIGCHLD 핸들러(플래그만 세움) + reap_jobs(): waitpid(-1, WNOHANG) 루프
 *           좀비(종료했지만 회수 안 됨)와 고아(부모가 먼저 끝나 launchd = pid 1 로 입양)를 체크로 확인
 *   Part C  미니 슈퍼바이저 (init / systemd 축소판): on-failure 재시작, 재시작 한도, 종료는 TERM → KILL
 *
 * 테스트는 진짜 프로그램을 실행한다: true / false (macOS 에는 /bin/true 가 없다 — /usr/bin/true),
 * sh -c 'exit 42', sh -c 'kill -9 $$', 없는 명령, 실행 권한 없는 파일. 모든 fork 는 회수하고,
 * 기다리는 곳에는 전부 제한 시간이 있어서 틀린 코드도 멈추지 않고 FAIL 로 끝난다.
 * 파이프·리다이렉션은 OS-B02, 시그널 핸들러 안전 규칙은 OS-B07.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static pid_t g_main_pid;              /* 하네스 프로세스 pid — 자식이 하네스 코드로 돌아오는 버그 감지용 */

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

/* ======================= Part A — 토크나이저와 포그라운드 실행 ======================= */
#define MAX_ARGS  16          /* argv 칸 수 (마지막 NULL 포함) */
#define LINE_MAX_ 256

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n'; }

/* line 을 제자리(in-place)에서 잘라 argv[] 를 채운다. argv[argc] = NULL.
 * 지원: 공백/탭/개행 구분, '...' 와 "..." (안의 글자는 그대로, 이스케이프·변수 확장 없음),
 *       붙은 따옴표 이어 붙이기 (a'b c'd → "ab cd"), 빈 따옴표 '' → 빈 인자,
 *       따옴표 밖의 & = 백그라운드 표시 — 줄의 마지막에만 ("sleep 1&" 도 됨). | < > 는 보통 글자 (OS-B02).
 * 반환: argc (빈 줄이면 0), 오류면 -1 (닫히지 않은 따옴표, & 뒤에 또 토큰, & 만 있음, 인자 max-1 개 초과). */
static int tokenize(char *line, char *argv[], int max, bool *bg) {
    char *r = line, *w = line;        /* r: 읽는 위치, w: 쓰는 위치. 따옴표를 빼므로 항상 w <= r */
    int argc = 0;
    *bg = false;
    for (;;) {
        while (is_ws(*r)) r++;
        if (*r == '\0') break;
        if (*bg) return -1;                         /* & 뒤에 또 무언가 */
        if (*r == '&') { *bg = true; r++; continue; }
        if (argc >= max - 1) return -1;             /* NULL 칸을 남긴다 */
        argv[argc++] = w;
        while (*r != '\0' && !is_ws(*r) && *r != '&') {
            if (*r == '\'' || *r == '"') {
                char q = *r++;
                while (*r != q) {
                    if (*r == '\0') return -1;      /* 닫히지 않은 따옴표 */
                    *w++ = *r++;
                }
                r++;                                /* 닫는 따옴표 건너뛰기 */
            } else {
                *w++ = *r++;
            }
        }
        char stop = *r;                             /* '\0', 공백, '&' — w 에 '\0' 을 쓰기 전에 읽어 둔다 */
        *w++ = '\0';
        if (stop == '&') *bg = true;
        if (stop != '\0') r++;
    }
    if (*bg && argc == 0) return -1;                /* "&" 만 있는 줄 */
    argv[argc] = NULL;
    return argc;
}

/* fork → (자식) execvp. 부모에게 자식 pid, fork 실패면 -1.
 * 자식에서 exec 가 실패하면 셸 관례대로 _exit(127: 못 찾음 / 126: 찾았지만 실행 불가). */
static pid_t spawn(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        sigset_t none;                              /* 시그널 마스크는 fork 와 exec 를 거쳐 상속된다 → 비운다 */
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        execvp(argv[0], argv);
        int e = errno;                              /* 여기 왔다 = exec 실패 */
        dprintf(STDERR_FILENO, "msh: %s: %s\n", argv[0],
                (e == ENOENT && strchr(argv[0], '/') == NULL) ? "command not found" : strerror(e));
        _exit(e == ENOENT || e == ENOTDIR ? 127 : 126);   /* exit() 가 아니라 _exit() */
    }
    return pid;
}

/* pid 하나를 기다려 raw status 를 돌려준다. 실패하면 -1. */
static int wait_status(pid_t pid) {
    int st;
    for (;;) {
        pid_t r = waitpid(pid, &st, 0);
        if (r == pid) return st;
        if (r < 0 && errno == EINTR) continue;      /* 시그널에 끊기면 다시 */
        return -1;
    }
}

/* raw status → 셸의 $? 값: 정상 종료면 종료 코드(0..255), 시그널로 죽었으면 128 + 시그널 번호. 그 외 -1. */
static int status_code(int status) {
    if (status < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/* ======================= Part B — 백그라운드 작업과 SIGCHLD ======================= */
#define MAX_JOBS 8
enum { JOB_FREE, JOB_RUNNING, JOB_DONE };
typedef struct { int jid; pid_t pid; int state; int status; char cmd[48]; } job_t;
typedef struct {
    job_t jobs[MAX_JOBS];
    int  next_jid;
    int  last_code;           /* $? */
    bool exit_requested;
    int  stray;               /* 표에 없는 pid 를 회수한 횟수 */
} shell_t;

static volatile sig_atomic_t g_sigchld_flag;    /* 핸들러가 세우고 메인 루프가 내린다 */
static volatile sig_atomic_t g_sigchld_count;   /* 테스트 계측: 핸들러가 실제로 몇 번 돌았나 */
static void on_sigchld(int sig) { (void)sig; g_sigchld_flag = 1; g_sigchld_count++; }

static void shell_init(shell_t *sh) { memset(sh, 0, sizeof *sh); sh->next_jid = 1; }

static int job_add(shell_t *sh, pid_t pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &sh->jobs[i];
        if (j->state == JOB_FREE) {
            j->jid = sh->next_jid++; j->pid = pid; j->state = JOB_RUNNING; j->status = -1;
            snprintf(j->cmd, sizeof j->cmd, "%s", cmd);
            return j->jid;
        }
    }
    return -1;
}
static job_t *job_by_pid(shell_t *sh, pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (sh->jobs[i].state != JOB_FREE && sh->jobs[i].pid == pid) return &sh->jobs[i];
    return NULL;
}
static job_t *job_by_jid(shell_t *sh, int jid) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (sh->jobs[i].state != JOB_FREE && sh->jobs[i].jid == jid) return &sh->jobs[i];
    return NULL;
}

/* 한 줄 실행. 반환 = 이 줄의 $? (sh->last_code 에도 저장).
 *   빈 줄 → $? 그대로 / 문법 오류 → 2 / exit [n] → exit_requested, n (없으면 $?)
 *   cd [dir] → 셸 자신의 chdir (dir 없으면 $HOME), 성공 0 실패 1
 *   cmd & → spawn + job_add, "[jid] pid" 출력, 0 (기다리지 않는다)
 *   cmd   → spawn + wait_status + status_code (fork 실패면 1) */
static int sh_line(shell_t *sh, const char *line) {
    char buf[LINE_MAX_];
    char *argv[MAX_ARGS];
    bool bg;
    int code;
    if (strlen(line) >= sizeof buf) {
        fprintf(stderr, "msh: line too long\n");
        code = 2;
    } else {
        memcpy(buf, line, strlen(line) + 1);
        int argc = tokenize(buf, argv, MAX_ARGS, &bg);
        if (argc == 0) return sh->last_code;
        if (argc < 0) {
            fprintf(stderr, "msh: syntax error: %s\n", line);
            code = 2;
        } else if (strcmp(argv[0], "exit") == 0) {
            sh->exit_requested = true;
            code = argc > 1 ? (atoi(argv[1]) & 0xff) : sh->last_code;
        } else if (strcmp(argv[0], "cd") == 0) {
            const char *dir = argc > 1 ? argv[1] : getenv("HOME");
            code = (dir != NULL && chdir(dir) == 0) ? 0 : 1;
            if (code) fprintf(stderr, "msh: cd: %s: %s\n", dir ? dir : "(no HOME)", strerror(errno));
        } else {
            pid_t pid = spawn(argv);
            if (pid < 0) {
                fprintf(stderr, "msh: fork: %s\n", strerror(errno));
                code = 1;
            } else if (bg) {
                int jid = job_add(sh, pid, line);
                printf("[%d] %d\n", jid, (int)pid);
                code = 0;
            } else {
                code = status_code(wait_status(pid));
            }
        }
    }
    sh->last_code = code;
    return code;
}

/* SIGCHLD 핸들러(on_sigchld)를 sigaction 으로 설치. SA_RESTART (read 등이 EINTR 로 끊기지 않게),
 * SA_NOCLDSTOP (자식이 멈출 때는 알리지 않음). 성공 0, 실패 -1. */
static int install_sigchld(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    return sigaction(SIGCHLD, &sa, NULL) == 0 ? 0 : -1;
}

/* 끝난 자식을 "전부" 회수한다: waitpid(-1, &st, WNOHANG) 를 0 이나 -1 이 나올 때까지.
 * 표에 있으면 JOB_DONE + status 기록 + "[jid] Done/Exit n/Killed" 출력, 없으면 stray++.
 * 반환: 이번에 회수한 자식 수. 플래그는 회수 "전에" 내린다. */
static int reap_jobs(shell_t *sh) {
    int n = 0, st;
    pid_t pid;
    g_sigchld_flag = 0;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        n++;
        job_t *j = job_by_pid(sh, pid);
        if (j == NULL) { sh->stray++; continue; }
        j->state = JOB_DONE;
        j->status = st;
        int code = status_code(st);
        if (code == 0)        printf("[%d] Done      %s\n", j->jid, j->cmd);
        else if (code < 128)  printf("[%d] Exit %d    %s\n", j->jid, code, j->cmd);
        else                  printf("[%d] Killed(%d) %s\n", j->jid, code - 128, j->cmd);
    }
    return n;
}

/* ======================= Part C — 미니 슈퍼바이저 ======================= */
enum { SVC_STOPPED, SVC_RUNNING, SVC_FAILED };
typedef struct {
    const char *name;
    char *argv[6];
    int  max_restarts;        /* on-failure 재시작 한도 */
    pid_t pid;
    int  state, starts, restarts, last_code;
} svc_t;
typedef struct { svc_t *svc; int n; int stray; } sup_t;

static void sup_start(svc_t *v) {
    v->pid = spawn(v->argv);
    if (v->pid < 0) { v->state = SVC_FAILED; return; }
    v->state = SVC_RUNNING;
    v->starts++;
}
static void sup_start_all(sup_t *s) { for (int i = 0; i < s->n; i++) sup_start(&s->svc[i]); }

/* 끝난 서비스를 전부 회수하고 정책을 적용한다 (systemd Restart=on-failure 축소판):
 *   종료 코드 0 → SVC_STOPPED (재시작 안 함)
 *   그 외(0 아닌 코드, 시그널) → restarts < max_restarts 이면 restarts++ 하고 sup_start, 아니면 SVC_FAILED
 * last_code 는 status_code 값. 표에 없는 pid 는 stray++. 반환: 회수한 수. */
static int sup_reap(sup_t *s) {
    int n = 0, st;
    pid_t pid;
    g_sigchld_flag = 0;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        n++;
        svc_t *v = NULL;
        for (int i = 0; i < s->n; i++)
            if (s->svc[i].state == SVC_RUNNING && s->svc[i].pid == pid) v = &s->svc[i];
        if (v == NULL) { s->stray++; continue; }
        v->pid = 0;
        v->last_code = status_code(st);
        if (v->last_code == 0) {
            v->state = SVC_STOPPED;
        } else if (v->restarts < v->max_restarts) {
            v->restarts++;
            sup_start(v);
        } else {
            v->state = SVC_FAILED;
        }
    }
    return n;
}

/* 아직 RUNNING 인 서비스에 SIGTERM → grace_ms 동안 각 pid 를 WNOHANG 으로 확인 → 남은 것은 SIGKILL 후 회수.
 * 끝나면 전부 SVC_STOPPED, last_code = status_code. 재시작하지 않는다. */
static void sup_shutdown(sup_t *s, int grace_ms) {
    for (int i = 0; i < s->n; i++)
        if (s->svc[i].state == SVC_RUNNING) kill(s->svc[i].pid, SIGTERM);
    for (int t = 0; t <= grace_ms; t++) {
        int alive = 0;
        for (int i = 0; i < s->n; i++) {
            svc_t *v = &s->svc[i];
            if (v->state != SVC_RUNNING) continue;
            int st;
            if (waitpid(v->pid, &st, WNOHANG) == v->pid) {
                v->state = SVC_STOPPED; v->last_code = status_code(st); v->pid = 0;
            } else {
                alive++;
            }
        }
        if (alive == 0) return;
        sleep_ms(1);
    }
    for (int i = 0; i < s->n; i++) {
        svc_t *v = &s->svc[i];
        if (v->state != SVC_RUNNING) continue;
        kill(v->pid, SIGKILL);
        v->last_code = status_code(wait_status(v->pid));
        v->state = SVC_STOPPED; v->pid = 0;
    }
}

/* ======================= 테스트 하네스 ======================= */
static int run_argv(char *const argv[]) {           /* spawn + wait, raw status (실패 -1) */
    pid_t p = spawn(argv);
    if (getpid() != g_main_pid) _exit(120);         /* spawn 이 자식 쪽에서 돌아왔다 = exec 실패 후 _exit 누락 */
    if (p < 0) return -1;
    return wait_status(p);
}

/* 회수하지 않고(WNOWAIT) 종료만 기다린다 — 끝나면 그 pid 는 좀비 상태로 남아 있다 */
static bool wait_zombie(pid_t pid, int ms) {
    if (pid <= 0) return false;
    for (int t = 0; t < ms; t++) {
        siginfo_t si;
        memset(&si, 0, sizeof si);
        if (waitid(P_PID, (id_t)pid, &si, WEXITED | WNOHANG | WNOWAIT) == 0 && si.si_pid == pid) return true;
        sleep_ms(1);
    }
    return false;
}

static bool tok_is(const char *line, int want_argc, bool want_bg, const char *const want[]) {
    char buf[LINE_MAX_];
    char *argv[MAX_ARGS];
    bool bg = !want_bg;
    snprintf(buf, sizeof buf, "%s", line);
    for (int i = 0; i < MAX_ARGS; i++) argv[i] = (char *)"?";
    int argc = tokenize(buf, argv, MAX_ARGS, &bg);
    if (argc != want_argc) return false;
    if (argc < 0) return true;
    if (bg != want_bg || argv[argc] != NULL) return false;
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], want[i]) != 0) return false;
    return true;
}

static void test_part_a(void) {
    printf("-- Part A: tokenize / spawn / status\n");
    /* tokenize */
    CHECK(tok_is("  ls   -l\t/tmp \n", 3, false, (const char *const[]){ "ls", "-l", "/tmp" }));
    CHECK(tok_is("", 0, false, NULL));
    CHECK(tok_is(" \t \n", 0, false, NULL));
    CHECK(tok_is("echo 'a  b' \"c d\"", 3, false, (const char *const[]){ "echo", "a  b", "c d" }));
    CHECK(tok_is("a'b c'd", 1, false, (const char *const[]){ "ab cd" }));
    CHECK(tok_is("echo '' x", 3, false, (const char *const[]){ "echo", "", "x" }));
    CHECK(tok_is("sleep 1 &", 2, true, (const char *const[]){ "sleep", "1" }));
    CHECK(tok_is("sleep 1& ", 2, true, (const char *const[]){ "sleep", "1" }));
    CHECK(tok_is("echo '&' \"a&b\"", 3, false, (const char *const[]){ "echo", "&", "a&b" }));
    CHECK(tok_is("echo a|b>c", 2, false, (const char *const[]){ "echo", "a|b>c" }));
    CHECK(tok_is("echo 'abc", -1, false, NULL));
    CHECK(tok_is("a & b", -1, false, NULL));
    CHECK(tok_is("  &  ", -1, false, NULL));
    CHECK(tok_is("a b c d e f g h i j k l m n o", 15, false,
                 (const char *const[]){ "a","b","c","d","e","f","g","h","i","j","k","l","m","n","o" }));
    CHECK(tok_is("a b c d e f g h i j k l m n o p", -1, false, NULL));   /* 16 개 → NULL 칸 없음 */

    /* spawn + wait_status + status_code: 진짜 프로그램 */
    int st;
    st = run_argv((char *const[]){ "true", NULL });
    CHECK(st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0 && status_code(st) == 0);
    st = run_argv((char *const[]){ "/usr/bin/false", NULL });
    CHECK(st >= 0 && WIFEXITED(st) && status_code(st) == 1);
    st = run_argv((char *const[]){ "sh", "-c", "exit 42", NULL });
    CHECK(status_code(st) == 42);
    st = run_argv((char *const[]){ "sh", "-c", "exit 300", NULL });
    CHECK(status_code(st) == 44);                    /* 300 & 0xff — 종료 코드는 8비트 */
    st = run_argv((char *const[]){ "sh", "-c", "kill -9 $$", NULL });
    CHECK(st >= 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL && !WIFEXITED(st));
    CHECK(status_code(st) == 137);
    st = run_argv((char *const[]){ "sh", "-c", "kill -TERM $$", NULL });
    CHECK(status_code(st) == 128 + SIGTERM);
    st = run_argv((char *const[]){ "no_such_cmd_os_b01", NULL });
    CHECK(st >= 0 && WIFEXITED(st) && status_code(st) == 127);
    st = run_argv((char *const[]){ "/bin/true", NULL });      /* macOS: /bin/true 없음 → 127 (리눅스는 0) */
    CHECK(status_code(st) == (access("/bin/true", X_OK) == 0 ? 0 : 127));

    /* 실행 권한 없는 파일 → 126 */
    char path[] = "/tmp/OS-B01_noexec_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(write(fd, "#!/bin/sh\nexit 0\n", 17) == 17);
        close(fd);
        chmod(path, 0644);
        st = run_argv((char *const[]){ path, NULL });
        CHECK(status_code(st) == 126);
        unlink(path);
    }

    /* 부모가 SIGTERM 을 막아 둔 채 spawn → 자식은 마스크를 비워야 TERM 으로 죽는다 (143). 상속되면 exit 7. */
    sigset_t term, old;
    sigemptyset(&term);
    sigaddset(&term, SIGTERM);
    sigprocmask(SIG_BLOCK, &term, &old);
    st = run_argv((char *const[]){ "sh", "-c", "kill -TERM $$; exit 7", NULL });
    sigprocmask(SIG_SETMASK, &old, NULL);
    CHECK(status_code(st) == 143);

    /* sh_line */
    shell_t sh;
    shell_init(&sh);
    CHECK(sh_line(&sh, "true") == 0 && sh.last_code == 0);
    CHECK(sh_line(&sh, "false") == 1 && sh.last_code == 1);
    CHECK(sh_line(&sh, "   ") == 1);                   /* 빈 줄: $? 그대로 */
    CHECK(sh_line(&sh, "sh -c 'exit 5'") == 5);
    CHECK(sh_line(&sh, "no_such_cmd_os_b01 arg") == 127);
    CHECK(sh_line(&sh, "echo 'oops") == 2 && sh.last_code == 2);
    char saved[PATH_MAX], now[PATH_MAX];
    if (getcwd(saved, sizeof saved) != NULL) {
        CHECK(sh_line(&sh, "cd /") == 0 && getcwd(now, sizeof now) != NULL && strcmp(now, "/") == 0);
        CHECK(sh_line(&sh, "cd /no/such/dir_os_b01") == 1 && getcwd(now, sizeof now) != NULL && strcmp(now, "/") == 0);
        CHECK(chdir(saved) == 0);
    }
    CHECK(!sh.exit_requested);
    CHECK(sh_line(&sh, "exit 3") == 3 && sh.exit_requested);
}

static void test_part_b(void) {
    printf("-- Part B: background jobs / SIGCHLD / zombie / orphan\n");
    CHECK(install_sigchld() == 0);
    struct sigaction cur;
    sigaction(SIGCHLD, NULL, &cur);
    CHECK(cur.sa_handler == on_sigchld);
    CHECK((cur.sa_flags & SA_RESTART) && (cur.sa_flags & SA_NOCLDSTOP));

    /* B1: SIGCHLD 를 막아 둔 채 3개를 띄우고, 셋 다 좀비가 될 때까지 기다린 뒤 푼다 */
    shell_t sh;
    shell_init(&sh);
    sigset_t chld, old;
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld, &old);
    int c0 = g_sigchld_count;
    CHECK(sh_line(&sh, "true &") == 0);
    CHECK(sh_line(&sh, "sh -c 'exit 3' &") == 0);
    CHECK(sh_line(&sh, "sh -c 'kill -9 $$' &") == 0);
    job_t *j1 = job_by_jid(&sh, 1), *j2 = job_by_jid(&sh, 2), *j3 = job_by_jid(&sh, 3);
    bool have = j1 && j2 && j3 && j1->pid > 0 && j2->pid > 0 && j3->pid > 0;
    CHECK(have);
    bool zombies = have && wait_zombie(j1->pid, 2000) && wait_zombie(j2->pid, 2000) && wait_zombie(j3->pid, 2000);
    CHECK(zombies);
    CHECK(have && kill(j1->pid, 0) == 0);           /* 끝났지만 프로세스 표에 남아 있다 = 좀비 */
    CHECK(have && j1->state == JOB_RUNNING);        /* 셸은 아직 모른다 */
    CHECK(g_sigchld_count == c0);                   /* 막혀 있는 동안은 핸들러가 안 돈다 */
    sigprocmask(SIG_SETMASK, &old, NULL);           /* 풀면 보류된 SIGCHLD 가 여기서 배달된다 */
    CHECK(g_sigchld_count == c0 + 1);               /* 자식 3명 종료 → 핸들러 1번 (표준 시그널은 쌓이지 않는다) */
    CHECK(g_sigchld_flag == 1);
    CHECK(reap_jobs(&sh) == 3);                     /* 그래서 한 번에 "전부" 회수해야 한다 */
    CHECK(g_sigchld_flag == 0);
    CHECK(have && j1->state == JOB_DONE && status_code(j1->status) == 0);
    CHECK(have && j2->state == JOB_DONE && status_code(j2->status) == 3);
    CHECK(have && j3->state == JOB_DONE && status_code(j3->status) == 137);
    errno = 0;
    CHECK(have && kill(j1->pid, 0) == -1 && errno == ESRCH);   /* 회수 후엔 pid 가 사라진다 */
    CHECK(reap_jobs(&sh) == 0 && sh.stray == 0);

    /* B2: 백그라운드가 도는 중에 포그라운드 실행 — 포그라운드 waitpid(pid) 는 자기 자식만 회수한다 */
    shell_init(&sh);
    CHECK(sh_line(&sh, "sleep 0.3 &") == 0);
    CHECK(sh_line(&sh, "sh -c 'exit 9'") == 9);
    j1 = job_by_jid(&sh, 1);
    CHECK(j1 != NULL && j1->state == JOB_RUNNING);
    for (int t = 0; t < 3000 && j1 && j1->state == JOB_RUNNING; t++) {
        if (g_sigchld_flag) reap_jobs(&sh);
        else sleep_ms(1);
    }
    CHECK(j1 != NULL && j1->state == JOB_DONE && status_code(j1->status) == 0);
    if (j1 && j1->state == JOB_RUNNING && j1->pid > 0) { kill(j1->pid, SIGKILL); waitpid(j1->pid, NULL, 0); }
    for (int i = 0; i < MAX_JOBS; i++)              /* 정리: 틀린 구현이 남긴 자식 */
        if (sh.jobs[i].state == JOB_RUNNING && sh.jobs[i].pid > 0) { kill(sh.jobs[i].pid, SIGKILL); waitpid(sh.jobs[i].pid, NULL, 0); }

    /* B3: 고아 — 자식 C 가 손자 G 를 만들고 먼저 끝난다. G 의 부모는 누구로 바뀌나 */
    int fd[2];
    CHECK(pipe(fd) == 0);
    pid_t c = fork();
    if (c == 0) {
        int ready[2];                               /* G 가 "처음 부모" 를 읽을 때까지 C 가 기다리게 */
        if (pipe(ready) != 0) _exit(1);
        pid_t g = fork();
        if (g == 0) {
            alarm(3);                               /* 워치독 */
            close(fd[0]);
            close(ready[0]);
            pid_t before = getppid();
            close(ready[1]);                        /* C 의 read 가 EOF 로 풀린다 */
            for (int t = 0; t < 2000 && getppid() == before; t++) sleep_ms(1);
            pid_t after = getppid();
            pid_t msg[2] = { before, after };
            ssize_t wr = write(fd[1], msg, sizeof msg);
            _exit(wr == (ssize_t)sizeof msg ? 0 : 1);
        }
        close(ready[1]);
        char ch;
        ssize_t rr = read(ready[0], &ch, 1);        /* EOF(0) = G 가 준비됨 */
        _exit(rr == 0 ? 0 : 1);                     /* G 를 기다리지(wait) 않고 끝난다 */
    }
    close(fd[1]);
    pid_t msg[2] = { 0, 0 };
    int cst = wait_status(c);
    ssize_t got = read(fd[0], msg, sizeof msg);     /* G 가 쓰거나 끝나면(alarm 3초) 돌아온다 */
    close(fd[0]);
    CHECK(status_code(cst) == 0);
    CHECK(got == (ssize_t)sizeof msg && msg[0] == c);
#ifdef __APPLE__
    CHECK(got == (ssize_t)sizeof msg && msg[1] == 1);   /* macOS: launchd (pid 1) 가 입양 */
#else
    CHECK(got == (ssize_t)sizeof msg && msg[1] != c);   /* Linux: pid 1 또는 subreaper (systemd --user 등) */
#endif
    printf("orphan: parent %d -> %d\n", (int)msg[0], (int)msg[1]);
}

static void test_part_c(void) {
    printf("-- Part C: supervisor\n");
    svc_t svc[] = {
        { .name = "oneshot",  .argv = { "true", NULL },                                   .max_restarts = 3 },
        { .name = "crashy",   .argv = { "sh", "-c", "kill -SEGV $$", NULL },              .max_restarts = 2 },
        { .name = "flaky",    .argv = { "sh", "-c", "exit 3", NULL },                     .max_restarts = 1 },
        { .name = "daemon",   .argv = { "sleep", "5", NULL },                             .max_restarts = 3 },
        { .name = "stubborn", .argv = { "sh", "-c", "trap '' TERM; exec sleep 5", NULL }, .max_restarts = 3 },
    };
    sup_t s = { .svc = svc, .n = 5, .stray = 0 };
    sup_start_all(&s);
    /* oneshot/crashy/flaky 가 자리 잡을 때까지 (최대 2초). SIGCHLD 플래그 + 50ms 마다 폴링 백업 */
    for (int t = 0; t < 2000; t++) {
        if (svc[0].state != SVC_RUNNING && svc[1].state != SVC_RUNNING && svc[2].state != SVC_RUNNING) break;
        if (g_sigchld_flag || t % 50 == 49) sup_reap(&s);
        else sleep_ms(1);
    }
    for (int i = 0; i < 5; i++)
        printf("  %-8s state=%d starts=%d restarts=%d last=%d\n", svc[i].name, svc[i].state,
               svc[i].starts, svc[i].restarts, svc[i].last_code);
    CHECK(svc[0].state == SVC_STOPPED && svc[0].starts == 1 && svc[0].last_code == 0);
    CHECK(svc[1].state == SVC_FAILED && svc[1].starts == 3 && svc[1].restarts == 2 && svc[1].last_code == 128 + SIGSEGV);
    CHECK(svc[2].state == SVC_FAILED && svc[2].starts == 2 && svc[2].last_code == 3);
    CHECK(svc[3].state == SVC_RUNNING && svc[4].state == SVC_RUNNING && s.stray == 0);
    sleep_ms(100);                                  /* stubborn 의 trap 이 걸릴 시간 */
    sup_shutdown(&s, 300);
    CHECK(svc[3].state == SVC_STOPPED && svc[3].last_code == 128 + SIGTERM);   /* TERM 에 죽음 */
    CHECK(svc[4].state == SVC_STOPPED && svc[4].last_code == 128 + SIGKILL);   /* TERM 무시 → KILL */
    for (int i = 0; i < 5; i++)                     /* 정리: 틀린 구현이 남긴 자식 */
        if (svc[i].state == SVC_RUNNING && svc[i].pid > 0) { kill(svc[i].pid, SIGKILL); waitpid(svc[i].pid, NULL, 0); }
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);   /* 남은 자식 0 */
}

int main(void) {
    g_main_pid = getpid();
    setvbuf(stdout, NULL, _IOLBF, 0);               /* 자식과 출력이 섞여도 줄 단위로 */
    test_part_a();
    test_part_b();
    test_part_c();
    while (waitpid(-1, NULL, WNOHANG) > 0) { }      /* 혹시 남은 좀비 정리 */
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
