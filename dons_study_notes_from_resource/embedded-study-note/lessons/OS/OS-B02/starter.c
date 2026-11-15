/* OS-B02 파일 디스크립터·dup2·파이프: 파이프라인과 리다이렉션
 *
 *   Part A  spawn_pipeline(): a | b | c  N단 파이프 — pipe() N-1개, fork() N번, dup2(),
 *           안 쓰는 파이프 fd 전부 close(), SIGPIPE 기본값 복원, execvp()
 *   Part B  apply_redirs(): <  >  2>&1  을 "왼쪽부터 순서대로" — 파이프 연결 "뒤에" 적용
 *   Part C  pipe_through(): 데몬이 파이프라인에 입력을 먹이고 출력을 모은다.
 *           입력을 다 쓰면 쓰기 끝을 닫아야 EOF 가 간다 / poll() 로 쓰기·읽기를 함께 /
 *           EPIPE 처리 / 부모 쪽 끝은 FD_CLOEXEC
 *
 * 하네스(수정 불필요): 토크나이저·파서, wait_all()(waitpid 폴링 + 시간 초과 시 SIGKILL),
 * count_open_fds()(fcntl(F_GETFD) 로 0..255 스캔), run_capture()(출력을 파이프로 받아 비교),
 * guarded_pipe_through()(Part C 를 감시자 프로세스 안에서 실행 — 틀린 코드가 막혀도 죽이고 FAIL).
 * 막힐 수 있는 곳은 전부 제한 시간이 있다. 시간 측정값은 체크하지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 실행에 필요한 외부 명령: printf sort head tr wc cat yes sh (macOS / Linux 기본 포함)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

/* ---------- 상수 ---------- */
#define MAXTOK    64
#define MAXSTAGE  8
#define MAXARG    16
#define MAXREDIR  4
#define LINEMAX   1024
#define TMO_MS    2000          /* 파이프라인 하나의 제한 시간 */
#define GUARD_MS  4000          /* Part C 감시자의 제한 시간 */
#define OUTMAX    (2u << 20)    /* 캡처 버퍼 2 MB */
#define HUNG      (-2)          /* 시간 초과로 죽인 단계의 코드 */
#define FD_SCAN   256

/* ---------- 토크나이저 (하네스) ----------
 * 공백으로 나눈다. '...' 작은따옴표 안은 글자 그대로 (이스케이프 없음).
 * 연산자 | < > 와 2>&1 은 붙어 있어도 떼어 낸다 ("a|b", ">out"). 따옴표 안의 | 는 그냥 글자.
 * 지원 안 함: 큰따옴표, $변수, 글로브, 백슬래시, >>, 2>file, &, ; (OS-B01 은 & 백그라운드). */
enum { T_WORD, T_PIPE, T_IN, T_OUT, T_ERR2OUT };
typedef struct { int kind; char *s; } tok_t;

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n'; }
static bool is_op(char c)    { return c == '|' || c == '<' || c == '>'; }

static int tokenize(const char *p, char *buf, size_t bufsz, tok_t *t, int maxtok) {
    size_t w = 0;
    int n = 0;
    for (;;) {
        while (is_space(*p)) p++;
        if (!*p) return n;
        if (n == maxtok) return -1;
        if (strncmp(p, "2>&1", 4) == 0) { t[n].kind = T_ERR2OUT; t[n++].s = NULL; p += 4; continue; }
        if (is_op(*p)) {
            t[n].kind = *p == '|' ? T_PIPE : *p == '<' ? T_IN : T_OUT;
            t[n++].s = NULL; p++; continue;
        }
        t[n].kind = T_WORD; t[n].s = buf + w;
        while (*p && !is_space(*p) && !is_op(*p)) {
            if (*p == '\'') {                       /* 'quoted text' */
                p++;
                while (*p && *p != '\'') { if (w + 1 >= bufsz) return -1; buf[w++] = *p++; }
                if (!*p) return -1;                 /* 닫는 따옴표 없음 */
                p++;
            } else {
                if (w + 1 >= bufsz) return -1;
                buf[w++] = *p++;
            }
        }
        if (w >= bufsz) return -1;
        buf[w++] = '\0';
        n++;
    }
}

/* ---------- 파서 (하네스) ----------
 * 리다이렉션은 단계마다 "나온 순서대로" 리스트에 담는다 → `> f 2>&1` 과 `2>&1 > f` 가 다르다 (Part B). */
typedef enum { R_IN, R_OUT, R_ERR2OUT } rkind_t;
typedef struct { rkind_t kind; const char *path; } redir_t;
typedef struct {
    char   *argv[MAXARG + 1];   /* NULL 로 끝남 */
    int     argc;
    redir_t r[MAXREDIR];
    int     nr;
} stage_t;
typedef struct { stage_t st[MAXSTAGE]; int n; char buf[LINEMAX]; } pipeline_t;

static int parse_line(const char *line, pipeline_t *pl) {     /* 0 = OK, -1 = 문법 오류 */
    tok_t t[MAXTOK];
    memset(pl, 0, sizeof *pl);
    int nt = tokenize(line, pl->buf, sizeof pl->buf, t, MAXTOK);
    if (nt <= 0) return -1;
    stage_t *st = &pl->st[0];
    pl->n = 1;
    for (int i = 0; i < nt; i++) {
        switch (t[i].kind) {
        case T_WORD:
            if (st->argc == MAXARG) return -1;
            st->argv[st->argc++] = t[i].s;
            break;
        case T_PIPE:                                 /* "| a", "a | | b" */
            if (st->argc == 0 || pl->n == MAXSTAGE) return -1;
            st = &pl->st[pl->n++];
            break;
        case T_ERR2OUT:
            if (st->nr == MAXREDIR) return -1;
            st->r[st->nr++] = (redir_t){ R_ERR2OUT, NULL };
            break;
        default:                                     /* T_IN / T_OUT: 다음 토큰은 파일 이름 */
            if (i + 1 >= nt || t[i + 1].kind != T_WORD || st->nr == MAXREDIR) return -1;
            st->r[st->nr++] = (redir_t){ t[i].kind == T_IN ? R_IN : R_OUT, t[i + 1].s };
            i++;
            break;
        }
    }
    return st->argc == 0 ? -1 : 0;                  /* "a |" */
}

/* ---------- 시간·대기·fd 세기 (하네스) ---------- */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* 열린 fd 개수: fcntl(fd, F_GETFD) 가 성공하면 열려 있다 (EBADF 면 닫힘). */
static int count_open_fds(void) {
    int n = 0;
    for (int fd = 0; fd < FD_SCAN; fd++)
        if (fcntl(fd, F_GETFD) != -1) n++;
    return n;
}

/* 셸 관례 종료 코드: 정상 종료 → 0..255, 시그널 → 128+번호 */
static int shell_code(int st) {
    if (WIFEXITED(st))   return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

/* 자식 n 개를 timeout_ms 안에 거둔다 (WNOHANG 폴링). 넘으면 남은 자식을 SIGKILL → codes = HUNG, 반환 -1. */
static int wait_all(const pid_t *pids, int n, int timeout_ms, int *codes) {
    bool done[MAXSTAGE] = { false };
    int left = n, rc = 0;
    double end = now_ms() + timeout_ms;
    for (int i = 0; i < n; i++) codes[i] = HUNG;
    while (left > 0) {
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            int st;
            pid_t r = waitpid(pids[i], &st, WNOHANG);
            if (r == pids[i])                  { done[i] = true; left--; codes[i] = shell_code(st); }
            else if (r < 0 && errno != EINTR)  { done[i] = true; left--; codes[i] = -1; }
        }
        if (left == 0) break;
        if (now_ms() >= end) {
            for (int i = 0; i < n; i++) {
                if (done[i]) continue;
                kill(pids[i], SIGKILL);
                waitpid(pids[i], NULL, 0);     /* 좀비를 남기지 않는다 */
                codes[i] = HUNG;
            }
            rc = -1;
            break;
        }
        sleep_ms(1);
    }
    return rc;
}

static void set_cloexec(int fd) { fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }

/* ============================ 여기부터 내가 쓰는 부분 ============================ */

static int apply_redirs(const stage_t *st);

/* Part A — pl 의 n 단계를 a | b | c 로 연결해 띄운다.
 *   첫 단계의 stdin = in_fd, 마지막 단계의 stdout = out_fd, stderr 는 그대로 (리다이렉션이 바꿀 수 있음).
 *   pids[i] = i 번째 자식. 반환: 띄운 자식 수 (fork/pipe 실패하면 그때까지 띄운 수).
 * 자식 i: (1) 읽을 fd → 0, 쓸 fd → 1 로 dup2  (2) 자기가 만든/물려받은 파이프 fd 원본 전부 close,
 *         in_fd/out_fd 원본도 2 보다 크면 close  (3) signal(SIGPIPE, SIG_DFL) — 하네스가 SIG_IGN 해 둠
 *         (4) apply_redirs() 실패 → _exit(1)  (5) execvp(); 실패 → "mysh: <cmd>: command not found",
 *         _exit(127) (ENOENT 가 아니면 126).
 * 부모: 자기가 만든 파이프 fd 를 전부 닫는다 (in_fd/out_fd 는 호출자 것이라 닫지 않는다). 기다리지 않는다. */
static int spawn_pipeline(const pipeline_t *pl, int in_fd, int out_fd, pid_t *pids) {
    /* TODO: i = 0..n-1 마다 (마지막 빼고) pipe → fork.
     *   자식: 읽을 fd/쓸 fd 를 0/1 로 dup2 → 파이프 fd 원본 전부 close → SIGPIPE 기본값 →
     *         apply_redirs → execvp → 실패하면 메시지 + _exit(127/126)
     *   부모: 이전 읽기 끝 close, 새 파이프 쓰기 끝 close, 읽기 끝은 다음 단계로 넘김 */
    (void)pl; (void)in_fd; (void)out_fd; (void)pids; (void)apply_redirs;
    return 0;
}

/* Part B — 자식 안에서(파이프 dup2 뒤, exec 전) 리다이렉션을 st->r[0..nr) 순서대로 적용.
 *   R_IN  path : open(O_RDONLY) → fd 0
 *   R_OUT path : open(O_WRONLY|O_CREAT|O_TRUNC, 0644) → fd 1
 *   R_ERR2OUT  : fd 2 = "지금" fd 1 이 가리키는 것 (dup2(1, 2))
 * open 한 fd 는 dup2 뒤 닫는다 (자식이 쓸데없는 fd 를 들고 exec 하지 않게).
 * open 실패: "mysh: <path>: <strerror>" 를 stderr 에 쓰고 -1. 성공: 0. */
static int apply_redirs(const stage_t *st) {
    /* TODO: st->r[k] 를 k 순서대로: R_IN/R_OUT 는 open → dup2 → close, R_ERR2OUT 은 dup2(1, 2) */
    (void)st;
    return 0;
}

/* Part C — 파이프라인 pl 의 stdin 에 in[0..inlen) 을 먹이고, stdout 을 out 에 모은다 (cap 넘는 건 버림).
 *   (1) 입력을 다 쓰면 쓰기 끝을 close — 안 닫으면 sort/wc 는 EOF 를 못 봐서 영원히 기다린다
 *   (2) 쓰기와 읽기를 poll() 로 함께 — 다 쓴 다음 읽으면 큰 입력에서 양쪽 다 막힌다 (파이프 버퍼 유한)
 *   (3) 파이프라인이 입력을 다 안 읽고 끝나면(head) write 가 EPIPE → 쓰기를 멈추고 계속 읽는다
 *       (SIGPIPE 는 main 에서 SIG_IGN — 아니면 이 프로세스가 죽는다)
 *   (4) 부모 쪽 끝(입력 쓰기 끝, 출력 읽기 끝)은 자식에게 새면 안 된다 → FD_CLOEXEC
 *   (5) 전체 TMO_MS 제한. 넘으면 남은 자식을 죽이고 HUNG. 끝에 wait_all() 로 거두고 codes 채움.
 * 반환: 0 정상 / HUNG 시간 초과 / -1 오류. 만든 fd 는 모두 닫고 나온다. */
static int pipe_through(const pipeline_t *pl, const char *in, size_t inlen,
                        char *out, size_t cap, size_t *outlen, int *codes) {
    /* TODO: 입력 파이프·출력 파이프 → 부모 쪽 끝 FD_CLOEXEC → spawn_pipeline → 자식 쪽 끝 close →
     *       poll() 루프(읽기 + 쓰기, 다 쓰면 쓰기 끝 close, EPIPE 면 쓰기 중단) → wait_all */
    (void)pl; (void)in; (void)inlen; (void)out; (void)cap; (void)codes;
    *outlen = 0;
    return -1;
}

/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

typedef struct {
    char   out[OUTMAX];
    size_t len;
    int    n;                  /* 띄운 단계 수 */
    int    codes[MAXSTAGE];    /* 단계별 셸 코드 (HUNG = 시간 초과로 죽임) */
    int    rc;                 /* 마지막 단계 코드, -1 = 파싱/띄우기 실패, HUNG */
    int    fd_delta;           /* 실행 후 열린 fd 수 - 실행 전 */
} cap_t;
static cap_t g;                /* 큰 버퍼라 전역 */

/* 한 줄을 파싱해 stdin=/dev/null, stdout=캡처 파이프로 실행하고 출력·코드를 모은다. 막히면 죽인다. */
static int run_capture(const char *line, cap_t *c) {
    pipeline_t pl;
    pid_t pids[MAXSTAGE];
    int fds0 = count_open_fds();
    c->len = 0; c->out[0] = '\0'; c->n = 0; c->rc = -1;
    for (int i = 0; i < MAXSTAGE; i++) c->codes[i] = -1;
    if (parse_line(line, &pl) < 0) { c->fd_delta = 0; return c->rc; }
    int devnull = open("/dev/null", O_RDONLY);
    int cp[2];
    if (devnull < 0 || pipe(cp) < 0) { perror("harness"); exit(2); }
    set_cloexec(devnull); set_cloexec(cp[0]); set_cloexec(cp[1]);   /* 하네스 fd 는 자식에게 안 샌다 */
    c->n = spawn_pipeline(&pl, devnull, cp[1], pids);
    close(devnull);
    close(cp[1]);
    double deadline = now_ms() + TMO_MS;
    bool killed = false;
    for (;;) {
        struct pollfd pf = { .fd = cp[0], .events = POLLIN };
        int left = (int)(deadline - now_ms());
        if (left <= 0) {
            if (killed) break;                  /* 죽였는데도 EOF 가 안 옴 (손자가 쥐고 있음) */
            wait_all(pids, c->n, 0, c->codes);  /* 시간 초과: 전부 SIGKILL */
            killed = true;
            deadline = now_ms() + 200;
            continue;
        }
        if (poll(&pf, 1, left) <= 0) continue;
        ssize_t k = read(cp[0], c->out + c->len, OUTMAX - 1 - c->len);
        if (k <= 0) break;                      /* EOF: 모든 쓰기 끝이 닫힘 */
        c->len += (size_t)k;
    }
    c->out[c->len] = '\0';
    close(cp[0]);
    int w = killed ? -1 : wait_all(pids, c->n, (int)(deadline - now_ms()) + 1, c->codes);
    if (c->n == pl.n && c->n > 0) c->rc = (killed || w < 0) ? HUNG : c->codes[c->n - 1];
    c->fd_delta = count_open_fds() - fds0;
    return c->rc;
}

/* Part C 를 감시자 프로세스 안에서 실행: 틀린 pipe_through 가 영원히 막혀도 GUARD_MS 뒤 프로세스 그룹째 죽인다. */
typedef struct { int rc, fd_delta, n; int codes[MAXSTAGE]; size_t len; } ghdr_t;

static bool read_full(int fd, void *buf, size_t len, double deadline) {
    size_t got = 0;
    while (got < len) {
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        int left = (int)(deadline - now_ms());
        if (left <= 0 || poll(&pf, 1, left) <= 0) return false;
        ssize_t k = read(fd, (char *)buf + got, len - got);
        if (k <= 0) return false;
        got += (size_t)k;
    }
    return true;
}
static void write_full(int fd, const void *buf, size_t len) {
    size_t put = 0;
    while (put < len) {
        ssize_t k = write(fd, (const char *)buf + put, len - put);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; return; }
        put += (size_t)k;
    }
}

static int guarded_pipe_through(const char *line, const char *in, size_t inlen, cap_t *c) {
    int rp[2];
    c->len = 0; c->out[0] = '\0'; c->rc = HUNG; c->fd_delta = 0; c->n = 0;
    for (int i = 0; i < MAXSTAGE; i++) c->codes[i] = -1;
    if (pipe(rp) < 0) { perror("harness"); exit(2); }
    set_cloexec(rp[0]); set_cloexec(rp[1]);
    fflush(stdout);
    pid_t gpid = fork();
    if (gpid < 0) { perror("fork"); exit(2); }
    if (gpid == 0) {                            /* 감시 대상 프로세스: 자기 프로세스 그룹 */
        setpgid(0, 0);
        close(rp[0]);
        pipeline_t pl;
        ghdr_t h = { .rc = -1 };
        int fds0 = count_open_fds();
        if (parse_line(line, &pl) == 0) {
            h.rc = pipe_through(&pl, in, inlen, c->out, OUTMAX - 1, &h.len, h.codes);
            h.n = pl.n;
        }
        h.fd_delta = count_open_fds() - fds0;
        write_full(rp[1], &h, sizeof h);
        write_full(rp[1], c->out, h.len);
        _exit(0);
    }
    setpgid(gpid, gpid);                        /* 경쟁 방지: 부모도 한 번 */
    close(rp[1]);
    double deadline = now_ms() + GUARD_MS;
    ghdr_t h;
    bool ok = read_full(rp[0], &h, sizeof h, deadline) && h.len < OUTMAX &&
              read_full(rp[0], c->out, h.len, deadline);
    close(rp[0]);
    if (!ok) {                                  /* 막힘: 감시 대상과 그 자식(파이프라인)을 그룹째 죽인다 */
        kill(-gpid, SIGKILL);
        kill(gpid, SIGKILL);
        waitpid(gpid, NULL, 0);
        printf("  (guard: pipe_through 가 %d ms 안에 끝나지 않아 죽임)\n", GUARD_MS);
        return c->rc;
    }
    waitpid(gpid, NULL, 0);
    c->len = h.len; c->out[h.len] = '\0';
    c->rc = h.rc; c->fd_delta = h.fd_delta; c->n = h.n;
    memcpy(c->codes, h.codes, sizeof h.codes);
    return c->rc;
}

/* ---------- 테스트 데이터 ---------- */
static char g_dir[64], g_in[96], g_out[96];
static char g_line[LINEMAX];

static const char *L(const char *fmt, const char *a, const char *b) {   /* 명령 줄에 작업 경로 끼우기 */
    snprintf(g_line, sizeof g_line, fmt, a, b);
    return g_line;
}
static void write_file(const char *path, const char *s) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(s, f);
    fclose(f);
}
static bool file_is(const char *path, const char *want) {
    char buf[256] = { 0 };
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = '\0';
    return strcmp(buf, want) == 0;
}
#define OUT_IS(s) (strcmp(g.out, (s)) == 0)

/* 자식의 0..2 외 열린 fd 를 3..9 범위에서 찾아 "open: 3 5" 처럼 출력 (없으면 "open:") */
#define PROBE "sh -c 'n=; for f in 3 4 5 6 7 8 9; do if (: >&$f) 2>/dev/null; then n=\"$n $f\"; fi; done; echo \"open:$n\"'"

int main(void) {
    signal(SIGPIPE, SIG_IGN);                   /* Part C: 독자가 떠난 파이프에 써도 죽지 않게 */
    alarm(90);                                  /* 마지막 안전망: 무엇이 막혀도 90 초 뒤 종료 */
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B02_work.%ld", (long)getpid());   /* 실행마다 다른 이름 */
    if (mkdir(g_dir, 0700) < 0) { perror(g_dir); return 2; }
    snprintf(g_in, sizeof g_in, "%s/in.txt", g_dir);
    snprintf(g_out, sizeof g_out, "%s/out.txt", g_dir);
    write_file(g_in, "pear\napple\nfig\n");

    /* ---- 하네스 확인: 토크나이저·파서 (시작 코드에서도 PASS) ---- */
    {
        pipeline_t pl;
        CHECK(parse_line("printf 'a|b'|sort>out 2>&1", &pl) == 0 && pl.n == 2 &&
              strcmp(pl.st[0].argv[1], "a|b") == 0 && pl.st[1].nr == 2 &&
              pl.st[1].r[0].kind == R_OUT && pl.st[1].r[1].kind == R_ERR2OUT);
        CHECK(parse_line("a 2>&1 > f", &pl) == 0 && pl.st[0].r[0].kind == R_ERR2OUT &&
              pl.st[0].r[1].kind == R_OUT);
        CHECK(parse_line("| a", &pl) < 0 && parse_line("a |", &pl) < 0 &&
              parse_line("a | | b", &pl) < 0 && parse_line("a >", &pl) < 0 &&
              parse_line("echo 'oops", &pl) < 0);
    }

    /* ---- Part A: N단 파이프 ---- */
    run_capture("echo hello", &g);
    CHECK(OUT_IS("hello\n") && g.rc == 0);
    run_capture("printf 'b\\na\\n' | sort | head -n 1", &g);
    CHECK(OUT_IS("a\n") && g.rc == 0);
    CHECK(g.fd_delta == 0);                                     /* 부모가 파이프 fd 를 다 닫았나 */
    run_capture("printf 'one two\\nthree\\n' | tr a-z A-Z | wc -l | tr -d ' '", &g);
    CHECK(OUT_IS("2\n") && g.n == 4 && g.fd_delta == 0);
    run_capture("printf 'x\\n' | cat | cat | cat | cat | cat | cat | cat", &g);
    CHECK(OUT_IS("x\n") && g.n == 8 && g.fd_delta == 0);
    run_capture("true | false", &g);
    CHECK(g.rc == 1);                                           /* 파이프라인 코드 = 마지막 단계 */
    run_capture("false | true", &g);
    CHECK(g.rc == 0 && g.codes[0] == 1);
    printf("-- 아래 'command not found' 한 줄은 테스트가 일부러 만든 것 --\n");
    fflush(stdout);
    run_capture("printf 'a\\n' | no_such_cmd_os_b02 | cat", &g);
    CHECK(OUT_IS("") && g.codes[1] == 127 && g.rc == 0);
    run_capture("yes | head -n 1", &g);                         /* 끝나려면 yes 가 SIGPIPE 로 죽어야 */
    CHECK(OUT_IS("y\n") && g.codes[0] == 128 + SIGPIPE && g.codes[1] == 0);
    run_capture(PROBE " | cat", &g);                            /* 첫 단계가 쥔 여분 fd */
    CHECK(OUT_IS("open:\n"));
    run_capture("printf x | " PROBE " | cat", &g);              /* 가운데 단계가 쥔 여분 fd */
    CHECK(OUT_IS("open:\n"));
    run_capture("printf x | cat | " PROBE, &g);                 /* 마지막 단계 */
    CHECK(OUT_IS("open:\n") && g.fd_delta == 0);

    /* ---- Part B: 리다이렉션 ---- */
    run_capture(L("sort < %s", g_in, NULL), &g);
    CHECK(OUT_IS("apple\nfig\npear\n") && g.rc == 0);
    run_capture(L("sort < %s | head -n 1 > %s", g_in, g_out), &g);
    CHECK(OUT_IS("") && g.rc == 0 && file_is(g_out, "apple\n"));
    run_capture(L("printf 'longer line\\n' > %s", g_in, NULL), &g);   /* g_in 을 덮어써도 됨: 아래서 다시 만든다 */
    run_capture(L("printf 'x\\n' > %s", g_in, NULL), &g);
    CHECK(file_is(g_in, "x\n"));                                /* O_TRUNC: 이전 내용이 남으면 안 됨 */
    write_file(g_in, "pear\napple\nfig\n");
    run_capture("sh -c 'echo out; echo err >&2' 2>&1 | sort", &g);
    CHECK(OUT_IS("err\nout\n"));                                /* stderr 도 파이프로 */
    run_capture(L("sh -c 'echo out; echo err >&2' > %s 2>&1", g_out, NULL), &g);
    CHECK(OUT_IS("") && file_is(g_out, "out\nerr\n"));          /* 둘 다 파일로 */
    run_capture(L("sh -c 'echo out; echo err >&2' 2>&1 > %s", g_out, NULL), &g);
    CHECK(OUT_IS("err\n") && file_is(g_out, "out\n"));          /* 순서가 바뀌면 결과도 바뀐다 */
    run_capture(L("printf 'a\\n' > %s | cat", g_out, NULL), &g);
    CHECK(OUT_IS("") && file_is(g_out, "a\n"));                 /* 리다이렉션이 파이프보다 나중 → 이긴다 */
    printf("-- 아래 'No such file' 한 줄은 테스트가 일부러 만든 것 --\n");
    fflush(stdout);
    run_capture("cat < /nonexistent_os_b02/x", &g);
    CHECK(OUT_IS("") && g.rc == 1);
    run_capture(L(PROBE " < %s > %s", g_in, g_out), &g);               /* open 한 fd 를 닫고 exec 했나 */
    CHECK(file_is(g_out, "open:\n") && g.fd_delta == 0);

    /* ---- Part C: 입력 먹이고 출력 모으기 ---- */
    {
        static char big[1200000];
        size_t blen = 0;
        for (int i = 0; i < 120000; i++) blen += (size_t)sprintf(big + blen, "L%07d\n", i);  /* 9 B × 120000 */

        guarded_pipe_through("sort -r", "hello\nworld\n", 12, &g);
        CHECK(g.rc == 0 && OUT_IS("world\nhello\n"));
        guarded_pipe_through("wc -l | tr -d ' '", big, blen, &g);   /* wc 는 EOF 를 봐야 답한다 */
        CHECK(g.rc == 0 && OUT_IS("120000\n"));
        guarded_pipe_through("cat", big, blen, &g);                 /* 1 MB: 다 쓰고 읽으면 교착 */
        CHECK(g.rc == 0 && g.len == blen && memcmp(g.out, big, blen) == 0);
        CHECK(g.fd_delta == 0);
        guarded_pipe_through("head -c 10", big, blen, &g);          /* 독자가 먼저 떠남 → EPIPE */
        CHECK(g.rc == 0 && OUT_IS("L0000000\nL") && g.codes[0] == 0);
        const char *csv = "s1,23\ns2,7\ns3,15\ns4,7\n";              /* 센서 id, 값 */
        guarded_pipe_through("sort -t, -k2,2n -k1,1 | head -n 2", csv, strlen(csv), &g);
        CHECK(g.rc == 0 && OUT_IS("s2,7\ns4,7\n"));
        guarded_pipe_through(PROBE, "", 0, &g);                     /* 부모 쪽 끝이 자식에게 샜나 */
        CHECK(g.rc == 0 && OUT_IS("open:\n") && g.fd_delta == 0);
    }

    unlink(g_in); unlink(g_out); rmdir(g_dir);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
