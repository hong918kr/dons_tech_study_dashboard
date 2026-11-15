/* OS-B14 IPC 비교: 파이프·FIFO·Unix 소켓·공유 메모리
 *
 *   Part A  바이트 스트림 3종 — pipe 2개 / named FIFO 2개 / AF_UNIX SOCK_STREAM socketpair.
 *           write_all·read_all (짧은 읽기, EINTR, EOF) + chan_create(만들기)·chan_attach(fork 뒤 나눠 갖기).
 *   Part B  shm_open + mmap 링 (방향당 슬롯 8 × 64 KB) + 이름 있는 세마포어 sem_open (방향당 items/space).
 *           macOS 에는 sem_init 이 없다(-1/ENOSYS) → 이름 있는 세마포어만 쓴다.
 *   Part C  상대가 죽으면 무엇이 보이나 (EOF / EPIPE / 영원히 대기) + 용도별 선택 규칙 pick_ipc().
 *
 * 같은 테스트(핑퐁 에코 1 B..64 KB + 8 MB 대량 전송 + 체크섬)를 4가지 수단에 똑같이 돌린다.
 * IPC 테스트는 전부 fork 한 "러너" 자식 안에서 돈다: 러너가 채널을 만들고 다시 fork 해 에코 서버를
 * 띄운 뒤 클라이언트 역할을 한다. 러너·서버 모두 setitimer 워치독(SIGALRM, 기본 동작 = 종료)을 걸고,
 * 하네스(부모)는 waitpid(WNOHANG) 폴링 + 시간 초과 시 프로세스 그룹 SIGKILL → 틀린 코드는 멈추지 않고
 * FAIL 로 끝난다. 이름(FIFO 경로, shm/sem 이름)은 테스트마다 하네스가 한 번 더 지운다.
 * 시간 측정은 하지 않는다 (demo_bench.c 가 한다).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ======================= 채널 ======================= */
enum { T_PIPE, T_FIFO, T_SOCK, T_SHM, T_N };
static const char *const T_NAME[T_N] = { "pipe", "fifo", "unix-sock", "shm+sem" };

#define SLOTS      8
#define SLOT_BYTES 65536u
typedef struct { uint32_t len; uint8_t data[SLOT_BYTES]; } slot_t;   /* len == 0 : EOF 표시 */
typedef struct { slot_t s[SLOTS]; } ring_t;
typedef struct { ring_t dir[2]; } shm_area_t;                        /* dir[0] 부모→자식, dir[1] 자식→부모 */

typedef struct {
    int  t;                   /* T_PIPE .. T_SHM */
    int  side;                /* -1: attach 전, 0: 부모(클라이언트), 1: 자식(서버) */
    int  rfd, wfd;            /* 스트림 3종: 이쪽이 읽고 쓰는 fd (소켓은 같은 fd) */
    bool wr_closed;           /* chan_shutdown_write() 를 했나 */
    int  fd[4];               /* pipe: [0][1] 부모→자식, [2][3] 자식→부모 / sock: [0] 부모 몫, [1] 자식 몫 */
    char fifo[2][96];         /* FIFO 경로: [0] 부모→자식, [1] 자식→부모 */
    char shm_name[32];        /* macOS: 이름은 '/' 포함 31 바이트까지 (PSHMNAMLEN) */
    char sem_name[4][32];     /* [2d] = dir d 의 items, [2d+1] = dir d 의 space */
    shm_area_t *area;
    sem_t *sem[4];
    ring_t *tx, *rx;          /* attach 뒤: 내가 쓰는 링 / 읽는 링 */
    sem_t *tx_items, *tx_space, *rx_items, *rx_space;
    uint32_t tx_tail, rx_head, rx_off;   /* 링 커서는 각 프로세스 지역 변수 (SPSC 라 공유할 필요 없음) */
    bool rx_have;             /* 읽던 슬롯을 붙들고 있나 */
} chan_t;

static pid_t g_tag;           /* 이름 충돌 방지용: 하네스 pid */
static char  g_dir[64];       /* /tmp/OS-B14_work/run.<pid> — FIFO 를 만드는 곳 */

static void chan_prepare(chan_t *c, int t, int id) {
    memset(c, 0, sizeof *c);
    c->t = t; c->side = -1; c->rfd = c->wfd = -1;
    for (int i = 0; i < 4; i++) { c->fd[i] = -1; c->sem[i] = SEM_FAILED; }
    c->tx_items = c->tx_space = c->rx_items = c->rx_space = SEM_FAILED;
    snprintf(c->fifo[0], sizeof c->fifo[0], "%s/f%d.p2c", g_dir, id);
    snprintf(c->fifo[1], sizeof c->fifo[1], "%s/f%d.c2p", g_dir, id);
    snprintf(c->shm_name, sizeof c->shm_name, "/osb14.%d.%d", (int)g_tag, id);
    for (int i = 0; i < 4; i++)
        snprintf(c->sem_name[i], sizeof c->sem_name[i], "/osb14.%d.%d.s%d", (int)g_tag, id, i);
}

static int sem_wait_r(sem_t *s) {          /* EINTR 이면 다시 기다린다 */
    int r;
    while ((r = sem_wait(s)) < 0 && errno == EINTR) {}
    return r;
}

/* ---------- TODO A1: 정확히 n 바이트 쓰기 / 읽기 ----------
 * write_all: n 바이트를 다 쓸 때까지 반복. EINTR 은 재시도. 성공이면 n, 오류면 -1 (errno 유지).
 * read_all : n 바이트를 다 읽을 때까지 반복. EINTR 은 재시도. EOF(read == 0) 를 만나면 거기까지 읽은
 *            바이트 수를 돌려준다 (처음부터 EOF 면 0). 오류면 -1.                                   */
static ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, p + done, n - done);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        done += (size_t)w;
    }
    return (ssize_t)done;
}

static ssize_t read_all(int fd, void *buf, size_t n) {
    char *p = buf;
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, p + done, n - done);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;                                   /* EOF: 쓰는 쪽이 전부 닫혔다 */
        done += (size_t)r;
    }
    return (ssize_t)done;
}

/* ---------- TODO B1: 공유 메모리 + 이름 있는 세마포어 만들기 ----------
 * shm_open(O_CREAT|O_EXCL) → ftruncate(sizeof(shm_area_t)) → mmap(MAP_SHARED) → close(fd),
 * sem_open ×4 (O_CREAT|O_EXCL, items = 0, space = SLOTS). 채운 것: c->area, c->sem[0..3].
 * fork 로 매핑과 세마포어를 물려주므로 이름은 만든 즉시 지운다 (shm_unlink, sem_unlink).  */
static int shm_create(chan_t *c) {
    int fd = shm_open(c->shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)sizeof(shm_area_t)) < 0) { close(fd); shm_unlink(c->shm_name); return -1; }
    void *p = mmap(NULL, sizeof(shm_area_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);                                   /* 매핑은 fd 가 없어도 유지된다 */
    shm_unlink(c->shm_name);                     /* 이름 즉시 삭제 — 죽어도 이름이 남지 않는다 */
    if (p == MAP_FAILED) return -1;
    c->area = p;
    for (int i = 0; i < 4; i++) {
        unsigned init = (i % 2) ? SLOTS : 0;     /* 짝수 = items(채워진 슬롯 0), 홀수 = space(빈 슬롯 8) */
        c->sem[i] = sem_open(c->sem_name[i], O_CREAT | O_EXCL, 0600, init);
        if (c->sem[i] == SEM_FAILED) return -1;
        sem_unlink(c->sem_name[i]);
    }
    return 0;
}

/* ---------- TODO A2: 채널 만들기 (fork 전) ----------
 * T_PIPE: pipe 2개 → c->fd[0..1] (부모→자식), c->fd[2..3] (자식→부모)
 * T_FIFO: mkfifo(c->fifo[0]), mkfifo(c->fifo[1]) (0600) — 열기는 fork 뒤 attach 에서
 * T_SOCK: socketpair(AF_UNIX, SOCK_STREAM) → c->fd[0], c->fd[1]
 * T_SHM : shm_create(c)
 * 성공 0, 실패 -1.                                                                              */
static int chan_create(chan_t *c) {
    switch (c->t) {
    case T_PIPE:
        if (pipe(&c->fd[0]) < 0) return -1;
        if (pipe(&c->fd[2]) < 0) return -1;
        return 0;
    case T_FIFO:
        if (mkfifo(c->fifo[0], 0600) < 0) return -1;
        if (mkfifo(c->fifo[1], 0600) < 0) return -1;
        return 0;
    case T_SOCK:
        return socketpair(AF_UNIX, SOCK_STREAM, 0, c->fd);
    case T_SHM:
        return shm_create(c);
    }
    return -1;
}

/* ---------- TODO A3: fork 뒤 내 몫 고르기 (side 0 = 부모, 1 = 자식) ----------
 * 스트림: c->rfd / c->wfd 를 정하고, 안 쓰는 fd 는 **닫고** -1 로. (EOF·EPIPE 가 이것에 달려 있다)
 * FIFO  : 두 쪽이 같은 순서로 연다 (fifo[0] 먼저). 부모는 둘 다 열고 나면 경로를 unlink.
 * SHM   : 내가 쓰는 방향 = side, 읽는 방향 = 1-side. tx/rx 링과 세마포어 포인터 4개를 정한다.  */
static int chan_attach(chan_t *c, int side) {
    c->side = side;
    switch (c->t) {
    case T_PIPE: {
        int r = side == 0 ? 2 : 0, w = side == 0 ? 1 : 3;   /* 부모: 자식→부모 파이프에서 읽고 부모→자식에 쓴다 */
        c->rfd = c->fd[r]; c->wfd = c->fd[w];
        for (int i = 0; i < 4; i++)
            if (i != r && i != w) { close(c->fd[i]); }
        for (int i = 0; i < 4; i++) c->fd[i] = -1;
        return 0;
    }
    case T_FIFO:
        if (side == 0) {
            c->wfd = open(c->fifo[0], O_WRONLY);   /* 자식이 읽기로 열 때까지 막힌다 */
            if (c->wfd < 0) return -1;
            c->rfd = open(c->fifo[1], O_RDONLY);   /* 자식이 쓰기로 열 때까지 막힌다 */
            if (c->rfd < 0) return -1;
            unlink(c->fifo[0]); unlink(c->fifo[1]);   /* 양쪽 다 열었다 → 이름은 이제 필요 없다 */
        } else {
            c->rfd = open(c->fifo[0], O_RDONLY);
            if (c->rfd < 0) return -1;
            c->wfd = open(c->fifo[1], O_WRONLY);
            if (c->wfd < 0) return -1;
        }
        return 0;
    case T_SOCK:
        c->rfd = c->wfd = c->fd[side];
        close(c->fd[1 - side]);
        c->fd[0] = c->fd[1] = -1;
        return 0;
    case T_SHM: {
        int tx = side, rx = 1 - side;
        c->tx = &c->area->dir[tx];   c->rx = &c->area->dir[rx];
        c->tx_items = c->sem[2 * tx]; c->tx_space = c->sem[2 * tx + 1];
        c->rx_items = c->sem[2 * rx]; c->rx_space = c->sem[2 * rx + 1];
        return 0;
    }
    }
    return -1;
}

/* ---------- TODO B2: 공유 메모리 링으로 보내기 / 받기 (바이트 스트림 의미) ----------
 * shm_send: SLOT_BYTES 씩 잘라 보낸다. 조각마다 sem_wait(tx_space) → 슬롯 [tx_tail % SLOTS] 에 복사,
 *           len 기록 → tx_tail++ → sem_post(tx_items). 성공이면 n, 오류면 -1.
 * shm_recv: n 바이트를 모을 때까지 슬롯을 소비한다. 슬롯 하나를 여러 recv 에 걸쳐 나눠 읽을 수 있다
 *           (rx_have / rx_off). 다 읽은 슬롯은 rx_head++ → sem_post(rx_space).
 *           len == 0 슬롯 = EOF: 거기까지 읽은 바이트 수를 돌려준다 (슬롯은 그대로 둬서 다음에도 EOF).  */
static ssize_t shm_send(chan_t *c, const void *buf, size_t n) {
    const uint8_t *p = buf;
    size_t done = 0;
    while (done < n) {
        if (sem_wait_r(c->tx_space) < 0) return -1;            /* 빈 슬롯 하나 확보 */
        slot_t *s = &c->tx->s[c->tx_tail % SLOTS];
        uint32_t k = (uint32_t)(n - done < SLOT_BYTES ? n - done : SLOT_BYTES);
        memcpy(s->data, p + done, k);
        s->len = k;
        c->tx_tail++;
        if (sem_post(c->tx_items) < 0) return -1;              /* 공개: sem_post 가 위 쓰기를 상대에게 보이게 한다 */
        done += k;
    }
    return (ssize_t)done;
}

static ssize_t shm_recv(chan_t *c, void *buf, size_t n) {
    uint8_t *p = buf;
    size_t done = 0;
    while (done < n) {
        if (!c->rx_have) {
            if (sem_wait_r(c->rx_items) < 0) return -1;        /* 채워진 슬롯이 생길 때까지 */
            c->rx_have = true; c->rx_off = 0;
        }
        slot_t *s = &c->rx->s[c->rx_head % SLOTS];
        if (s->len == 0) break;                                /* EOF 표시 */
        uint32_t k = s->len - c->rx_off;
        if (k > n - done) k = (uint32_t)(n - done);
        memcpy(p + done, s->data + c->rx_off, k);
        c->rx_off += k; done += k;
        if (c->rx_off == s->len) {                             /* 슬롯을 다 읽었다 → 돌려준다 */
            c->rx_have = false; c->rx_head++;
            if (sem_post(c->rx_space) < 0) return -1;
        }
    }
    return (ssize_t)done;
}

/* ---------- 하네스 제공: 보내기/받기/닫기 ---------- */
static ssize_t chan_send(chan_t *c, const void *b, size_t n) {
    return c->t == T_SHM ? shm_send(c, b, n) : write_all(c->wfd, b, n);
}
static ssize_t chan_recv(chan_t *c, void *b, size_t n) {
    return c->t == T_SHM ? shm_recv(c, b, n) : read_all(c->rfd, b, n);
}
static void shm_send_eof(chan_t *c) {            /* len 0 슬롯 = "더 안 보낸다" (파이프의 close 에 해당) */
    if (c->tx == NULL || c->tx_space == SEM_FAILED || sem_wait_r(c->tx_space) < 0) return;
    c->tx->s[c->tx_tail % SLOTS].len = 0;
    c->tx_tail++;
    sem_post(c->tx_items);
}
static void chan_shutdown_write(chan_t *c) {     /* 쓰기 방향만 닫기 (상대는 EOF 를 보고, 나는 계속 읽는다) */
    if (c->wr_closed || c->side < 0) return;
    c->wr_closed = true;
    if (c->t == T_SHM) shm_send_eof(c);
    else if (c->t == T_SOCK) shutdown(c->wfd, SHUT_WR);   /* 소켓은 fd 하나가 양방향 → close 하면 읽기도 잃는다 */
    else { close(c->wfd); c->wfd = -1; }
}
static void chan_close(chan_t *c) {
    chan_shutdown_write(c);
    if (c->t == T_SHM) {
        if (c->area) munmap(c->area, sizeof *c->area);
        for (int i = 0; i < 4; i++) if (c->sem[i] != SEM_FAILED) sem_close(c->sem[i]);   /* macOS: sem 도 fd 를 쓴다 */
        c->area = NULL;
    } else {
        if (c->rfd >= 0) close(c->rfd);
        if (c->wfd >= 0 && c->wfd != c->rfd) close(c->wfd);
        c->rfd = c->wfd = -1;
    }
}
static void chan_force_cleanup(const chan_t *c) {       /* 테스트가 죽었어도 이름을 남기지 않는다 */
    unlink(c->fifo[0]); unlink(c->fifo[1]);
    shm_unlink(c->shm_name);
    for (int i = 0; i < 4; i++) sem_unlink(c->sem_name[i]);
}
static bool names_gone(const chan_t *c) {
    struct stat st;
    if (stat(c->fifo[0], &st) == 0 || stat(c->fifo[1], &st) == 0) return false;
    int fd = shm_open(c->shm_name, O_RDONLY, 0);
    if (fd >= 0) { close(fd); return false; }
    for (int i = 0; i < 4; i++) {
        sem_t *s = sem_open(c->sem_name[i], 0);
        if (s != SEM_FAILED) { sem_close(s); return false; }
    }
    return true;
}
static int count_fds(void) {
    int n = 0;
    for (int fd = 0; fd < 256; fd++) if (fcntl(fd, F_GETFD) != -1) n++;
    return n;
}

/* ---------- 체크섬·페이로드 ---------- */
#define FNV0 2166136261u
static uint32_t fnv1a(uint32_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void fill_payload(uint8_t *b, size_t n, uint32_t *state) { for (size_t i = 0; i < n; i++) b[i] = (uint8_t)xs32(state); }

/* ---------- TODO C: 용도별 선택 규칙 ----------
 * 이 레슨의 규칙 (5. 풀이 Part C 표):
 *   1) 대용량: msg_bytes >= 1 MiB 또는 mb_per_s >= 2000          → PICK_SHM (알림·제어는 소켓으로 따로)
 *   2) 클라이언트가 여럿 붙었다 떨어진다                           → PICK_UNIX_LISTEN
 *   3) 양방향이거나 fd 전달 필요: 부모-자식이면 PICK_SOCKETPAIR, 아니면 PICK_UNIX_LISTEN
 *   4) 한 방향: 부모-자식이면 PICK_PIPE, 남남이면 PICK_FIFO                                    */
typedef enum { PICK_PIPE, PICK_FIFO, PICK_SOCKETPAIR, PICK_UNIX_LISTEN, PICK_SHM, PICK_NONE = -1 } pick_t;
typedef struct {
    bool   related;       /* 한쪽이 다른 쪽을 fork 했다 (fd 를 물려줄 수 있다) */
    bool   two_way;       /* 요청-응답 */
    bool   many_clients;  /* 서버 하나에 클라이언트 여럿이 붙었다 떨어진다 */
    bool   pass_fds;      /* fd 를 넘겨야 한다 (SCM_RIGHTS — OS-M16) */
    size_t msg_bytes;     /* 대표 메시지 크기 */
    double mb_per_s;      /* 필요한 처리량 */
} need_t;

static pick_t pick_ipc(const need_t *n) {
    if (n->msg_bytes >= (1u << 20) || n->mb_per_s >= 2000.0) return PICK_SHM;
    if (n->many_clients) return PICK_UNIX_LISTEN;
    if (n->two_way || n->pass_fds) return n->related ? PICK_SOCKETPAIR : PICK_UNIX_LISTEN;
    return n->related ? PICK_PIPE : PICK_FIFO;
}

#ifndef OSB14_NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int g_report_fd = -1;                 /* 러너 → 하네스 진행 상황 (러너가 죽어도 남는다) */
static void report(uint8_t bits) { if (g_report_fd >= 0) { ssize_t w = write(g_report_fd, &bits, 1); (void)w; } }

static void arm_watchdog(int ms) {           /* SIGALRM 기본 동작 = 프로세스 종료 */
    signal(SIGALRM, SIG_DFL);
    struct itimerval it = { { 0, 0 }, { ms / 1000, (ms % 1000) * 1000 } };
    setitimer(ITIMER_REAL, &it, NULL);
}

typedef struct { bool exited; int code; int sig; unsigned progress; } outcome_t;
#define ALL_BAD 0xFF

/* fn(t, id) 를 새 프로세스 그룹의 자식에서 돌린다. wd_ms 안에 안 끝나면 SIGALRM, 그래도 안 되면 SIGKILL. */
static outcome_t run_isolated(int (*fn)(int, int), int t, int id, int wd_ms) {
    outcome_t o = { false, ALL_BAD, 0, 0 };
    int rp[2];
    if (pipe(rp) < 0) return o;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        close(rp[0]); g_report_fd = rp[1];
        signal(SIGPIPE, SIG_IGN);            /* 죽은 상대에게 쓰면 시그널 대신 EPIPE 를 받는다 */
        arm_watchdog(wd_ms);
        _exit(fn(t, id) & 0xFF);
    }
    close(rp[1]);
    if (pid < 0) { close(rp[0]); return o; }
    setpgid(pid, pid);
    int st = 0, waited = 0;
    pid_t r;
    while ((r = waitpid(pid, &st, WNOHANG)) == 0 && waited < wd_ms + 1000) {
        struct timespec ms = { 0, 1000000 };
        nanosleep(&ms, NULL);
        waited++;
    }
    if (r == 0) { kill(-pid, SIGKILL); waitpid(pid, &st, 0); }
    kill(-pid, SIGKILL);                     /* 고아가 된 서버(손자)까지 정리 */
    if (WIFEXITED(st)) { o.exited = true; o.code = WEXITSTATUS(st); }
    else if (WIFSIGNALED(st)) o.sig = WTERMSIG(st);
    fcntl(rp[0], F_SETFL, O_NONBLOCK);
    uint8_t b;
    while (read(rp[0], &b, 1) == 1) o.progress |= b;
    close(rp[0]);
    chan_t c; chan_prepare(&c, t, id); chan_force_cleanup(&c);
    return o;
}

/* ---- Part A 단위 테스트: write_all / read_all (러너 안에서, 파이프 하나) ---- */
enum { U_BASIC = 1, U_EOF_MID = 2, U_EOF_ZERO = 4, U_EPIPE = 8, U_BIG = 16 };
static int unit_rw(int t, int id) {
    (void)t; (void)id;
    int code = 0, p[2];
    char in[16] = { 0 };
    if (pipe(p) < 0) return ALL_BAD;
    if (!(write_all(p[1], "0123456789", 10) == 10 && read_all(p[0], in, 10) == 10 && memcmp(in, "0123456789", 10) == 0))
        code |= U_BASIC;
    if (write_all(p[1], "abc", 3) != 3) code |= U_EOF_MID;
    close(p[1]);
    if (!(read_all(p[0], in, 10) == 3 && memcmp(in, "abc", 3) == 0)) code |= U_EOF_MID;   /* 3 바이트 뒤 EOF */
    if (read_all(p[0], in, 10) != 0) code |= U_EOF_ZERO;                                    /* 처음부터 EOF */
    close(p[0]);
    if (pipe(p) < 0) return ALL_BAD;
    close(p[0]);                                                                            /* 읽는 쪽이 없다 */
    errno = 0;
    if (!(write_all(p[1], "x", 1) == -1 && errno == EPIPE)) code |= U_EPIPE;
    close(p[1]);
    /* 200 KB 를 한 번에 read_all: 파이프 버퍼(이 맥 64 KB)보다 크므로 read 를 여러 번 해야 한다 */
    enum { BIG = 200000 };
    static uint8_t out[BIG], got[BIG];
    uint32_t s = 7;
    fill_payload(out, BIG, &s);
    if (pipe(p) < 0) return ALL_BAD;
    fflush(stdout);
    pid_t w = fork();
    if (w == 0) { arm_watchdog(1000); close(p[0]); _exit(write_all(p[1], out, BIG) == BIG ? 0 : 1); }
    close(p[1]);
    if (!(read_all(p[0], got, BIG) == BIG && memcmp(out, got, BIG) == 0)) code |= U_BIG;
    close(p[0]);
    int st;
    waitpid(w, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) code |= U_BIG;
    return code;
}

/* ---- 공통: 핑퐁 에코, 대량 전송 ---- */
enum { R_CREATE = 1, R_ATTACH = 2, R_DATA = 4, R_SERVER = 8, R_FDLEAK = 16, R_NAMES = 32 };
#define MAXMSG 65536u
#define MAGIC  0x4F534234u
typedef struct { uint32_t magic, seq, len, sum; } hdr_t;
static const uint32_t PP_SIZES[] = { 1, 7, 64, 512, 4096, 4097, 16384, 65535, 65536 };
#define PP_ROUNDS 20
#define BULK_BYTES (8u << 20)
#define BULK_WCHUNK 12345u        /* 슬롯(64 KB)·파이프 버퍼와 안 맞는 크기로 써서 경계 처리를 시험한다 */
#define BULK_RCHUNK 4096u
#define SERVER_WD 2000

static int echo_server(chan_t *c) {               /* 헤더 + 페이로드를 받아 체크섬 확인 후 그대로 돌려준다 */
    static uint8_t buf[MAXMSG];
    int bad = 0;
    for (;;) {
        hdr_t h;
        ssize_t r = chan_recv(c, &h, sizeof h);
        if (r == 0) break;                                                    /* 깔끔한 EOF */
        if (r != (ssize_t)sizeof h || h.magic != MAGIC || h.len > MAXMSG) { bad++; break; }
        if (chan_recv(c, buf, h.len) != (ssize_t)h.len) { bad++; break; }
        if (fnv1a(FNV0, buf, h.len) != h.sum) bad++;
        if (chan_send(c, &h, sizeof h) != (ssize_t)sizeof h || chan_send(c, buf, h.len) != (ssize_t)h.len) { bad++; break; }
    }
    return bad;
}

typedef struct { uint64_t bytes; uint32_t sum, pad; } bulk_ack_t;
static int bulk_server(chan_t *c) {               /* 4 KB 씩 받아 FNV, EOF 에서 {총 바이트, FNV} 를 돌려준다 */
    static uint8_t buf[BULK_RCHUNK];
    bulk_ack_t a = { 0, FNV0, 0 };
    for (;;) {
        ssize_t r = chan_recv(c, buf, sizeof buf);
        if (r < 0) return 1;
        a.sum = fnv1a(a.sum, buf, (size_t)r);
        a.bytes += (uint64_t)r;
        if ((size_t)r < sizeof buf) break;        /* 짧게 왔다 = EOF */
    }
    return chan_send(c, &a, sizeof a) == (ssize_t)sizeof a ? 0 : 1;
}

static int run_pair(int t, int id, bool bulk) {
    chan_t c;
    chan_prepare(&c, t, id);
    int fds0 = count_fds();
    if (chan_create(&c) < 0) { chan_force_cleanup(&c); return ALL_BAD; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return ALL_BAD;
    if (pid == 0) {
        arm_watchdog(SERVER_WD);
        if (chan_attach(&c, 1) < 0) _exit(100);
        int bad = bulk ? bulk_server(&c) : echo_server(&c);
        chan_close(&c);
        _exit(bad ? 101 : 0);
    }
    int code = 0;
    if (chan_attach(&c, 0) < 0) code |= R_ATTACH;
    else if (!bulk) {
        static uint8_t out[MAXMSG], in[MAXMSG];
        uint32_t seq = 0, st = 12345;
        for (size_t i = 0; i < sizeof PP_SIZES / sizeof PP_SIZES[0] && !code; i++)
            for (int k = 0; k < PP_ROUNDS && !code; k++, seq++) {
                uint32_t len = PP_SIZES[i];
                fill_payload(out, len, &st);
                hdr_t h = { MAGIC, seq, len, fnv1a(FNV0, out, len) }, back;
                if (chan_send(&c, &h, sizeof h) != (ssize_t)sizeof h || chan_send(&c, out, len) != (ssize_t)len ||
                    chan_recv(&c, &back, sizeof back) != (ssize_t)sizeof back ||
                    memcmp(&back, &h, sizeof h) != 0 ||                          /* 순서(seq)·길이·체크섬 */
                    chan_recv(&c, in, len) != (ssize_t)len || memcmp(in, out, len) != 0)
                    code |= R_DATA;
            }
        report(1);
    } else {
        static uint8_t chunk[BULK_WCHUNK];
        uint32_t st = 999, sum = FNV0;
        uint64_t sent = 0;
        while (sent < BULK_BYTES && !code) {
            size_t k = BULK_BYTES - sent < BULK_WCHUNK ? (size_t)(BULK_BYTES - sent) : BULK_WCHUNK;
            fill_payload(chunk, k, &st);
            sum = fnv1a(sum, chunk, k);
            if (chan_send(&c, chunk, k) != (ssize_t)k) code |= R_DATA;
            sent += k;
        }
        chan_shutdown_write(&c);                                     /* 상대에게 EOF, 나는 계속 읽는다 */
        bulk_ack_t a;
        if (code || chan_recv(&c, &a, sizeof a) != (ssize_t)sizeof a || a.bytes != BULK_BYTES || a.sum != sum)
            code |= R_DATA;
        report(1);
    }
    chan_close(&c);                                                  /* 서버는 EOF 를 보고 끝난다 */
    int st;
    waitpid(pid, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) code |= R_SERVER;
    if (count_fds() != fds0) code |= R_FDLEAK;
    if (!names_gone(&c)) code |= R_NAMES;
    return code;
}
static int run_pingpong(int t, int id) { return run_pair(t, id, false); }
static int run_bulk(int t, int id)     { return run_pair(t, id, true); }

/* ---- Part C: 상대가 죽으면 ---- */
enum { P_HELLO = 1, P_SEND_EPIPE = 2, P_SEND_OK = 4, P_RECV_EOF = 8 };
static int run_peer_death(int t, int id) {
    chan_t c;
    chan_prepare(&c, t, id);
    if (chan_create(&c) < 0) return ALL_BAD;
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return ALL_BAD;
    if (pid == 0) {                                  /* "hello" 를 보내고 SIGKILL 로 즉사 (정리 코드 없음) */
        arm_watchdog(SERVER_WD);
        if (chan_attach(&c, 1) == 0) chan_send(&c, "hello", 5);
        kill(getpid(), SIGKILL);
        _exit(1);
    }
    if (chan_attach(&c, 0) < 0) return ALL_BAD;
    char in[8] = { 0 };
    if (chan_recv(&c, in, 5) == 5 && memcmp(in, "hello", 5) == 0) report(P_HELLO);
    int st;
    waitpid(pid, &st, 0);                            /* 상대가 확실히 죽은 뒤 */
    errno = 0;
    ssize_t w = chan_send(&c, "ping", 4);
    if (w == -1 && errno == EPIPE) report(P_SEND_EPIPE);
    else if (w == 4) report(P_SEND_OK);
    if (chan_recv(&c, in, 1) == 0) report(P_RECV_EOF);   /* shm: 여기서 영원히 기다린다 → 워치독 */
    chan_close(&c);
    return 0;
}

static const char *pick_name(pick_t p) {
    switch (p) {
    case PICK_PIPE: return "pipe"; case PICK_FIFO: return "fifo"; case PICK_SOCKETPAIR: return "socketpair";
    case PICK_UNIX_LISTEN: return "unix-listen"; case PICK_SHM: return "shm"; default: return "?";
    }
}
static const char *code_str(const outcome_t *o) {
    static char s[64];
    if (!o->exited) { snprintf(s, sizeof s, "killed(sig %d)", o->sig); return s; }
    if (o->code == 0) return "OK";
    if (o->code == ALL_BAD) return "FAIL (create)";
    snprintf(s, sizeof s, "%s%s%s%s%s%s", o->code & R_CREATE ? "create " : "", o->code & R_ATTACH ? "attach " : "",
             o->code & R_DATA ? "data " : "", o->code & R_SERVER ? "server " : "",
             o->code & R_FDLEAK ? "fdleak " : "", o->code & R_NAMES ? "names " : "");
    return s;
}

int main(void) {
    g_tag = getpid();
    mkdir("/tmp/OS-B14_work", 0700);
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B14_work/run.%d", (int)g_tag);
    mkdir(g_dir, 0700);
    int id = 0;

    /* 하네스 자체 점검: FNV-1a 32 표준 벡터 */
    CHECK(fnv1a(FNV0, "a", 1) == 0xe40c292cu && fnv1a(FNV0, "", 0) == FNV0);

    /* ---- Part A: write_all / read_all ---- */
    outcome_t u = run_isolated(unit_rw, 0, id++, 1500);
    int uc = u.exited ? u.code : ALL_BAD;
    CHECK(!(uc & U_BASIC));
    CHECK(!(uc & U_EOF_MID));
    CHECK(!(uc & U_EOF_ZERO));
    CHECK(!(uc & U_EPIPE));
    CHECK(!(uc & U_BIG));

    /* ---- Part A/B: 4가지 수단에 같은 테스트 ---- */
    printf("%-10s %-14s %-14s %s\n", "transport", "pingpong", "bulk 8MB", "peer killed: send / recv");
    outcome_t pd[T_N];
    for (int t = 0; t < T_N; t++) {
        outcome_t pp = run_isolated(run_pingpong, t, id++, SERVER_WD + 500);
        outcome_t bk = run_isolated(run_bulk, t, id++, SERVER_WD + 500);
        pd[t] = run_isolated(run_peer_death, t, id++, t == T_SHM ? 300 : 1500);
        int a = pp.exited ? pp.code : ALL_BAD, b = bk.exited ? bk.code : ALL_BAD;
        char pds[64];
        unsigned g = pd[t].progress;
        snprintf(pds, sizeof pds, "%s / %s%s", g & P_SEND_EPIPE ? "EPIPE" : g & P_SEND_OK ? "ok (buffered)" : "?",
                 g & P_RECV_EOF ? "EOF" : "", !pd[t].exited && pd[t].sig == SIGALRM ? "HANG (watchdog SIGALRM)" : "");
        printf("%-10s %-14s ", T_NAME[t], code_str(&pp));
        printf("%-14s %s\n", code_str(&bk), pds);
        CHECK(!(a & (R_CREATE | R_ATTACH | R_DATA | R_SERVER)));   /* 핑퐁: 모든 메시지 순서대로·온전히 */
        CHECK(!(b & (R_CREATE | R_ATTACH | R_DATA | R_SERVER)));   /* 대량: 8 MB, 총 바이트·FNV 일치 */
        CHECK(!((a | b) & R_FDLEAK));                              /* fd 누수 없음 (sem 도 fd 를 쓴다) */
        CHECK(!((a | b) & R_NAMES));                               /* FIFO 경로·shm·sem 이름이 남지 않음 */
    }

    /* ---- Part C: 상대가 죽으면 ---- */
    for (int t = T_PIPE; t <= T_SOCK; t++)                         /* 스트림: 쓰면 EPIPE, 읽으면 EOF */
        CHECK(pd[t].exited && pd[t].code == 0 && pd[t].progress == (P_HELLO | P_SEND_EPIPE | P_RECV_EOF));
    CHECK(!pd[T_SHM].exited && pd[T_SHM].sig == SIGALRM &&         /* shm: 쓰기는 성공, 읽기는 영원히 */
          pd[T_SHM].progress == (P_HELLO | P_SEND_OK));

    /* ---- Part C: 선택 규칙 ---- */
    struct { const char *what; need_t n; pick_t want; } cases[] = {
        { "sensor proc | filter (fork, 1-way, 64 B)",   { true,  false, false, false, 64,        0.1 },    PICK_PIPE },
        { "any app -> watchdog daemon heartbeat",       { false, false, false, false, 16,        0.001 },  PICK_FIFO },
        { "parent <-> worker request/response",         { true,  true,  false, false, 256,       1 },      PICK_SOCKETPAIR },
        { "config daemon, many clients",                { false, true,  true,  false, 512,       1 },      PICK_UNIX_LISTEN },
        { "parent hands opened fds to a child",         { true,  false, false, true,  8,         0 },      PICK_SOCKETPAIR },
        { "camera 1080p frames 6 MB @ 30 fps",          { false, false, false, false, 6u << 20,  180 },    PICK_SHM },
        { "radar samples 4 KB, 3 GB/s",                 { true,  false, false, false, 4096,      3000 },   PICK_SHM },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        pick_t p = pick_ipc(&cases[i].n);
        printf("pick: %-44s -> %s\n", cases[i].what, pick_name(p));
        CHECK(p == cases[i].want);
    }

    rmdir(g_dir);
    rmdir("/tmp/OS-B14_work");                     /* 비어 있을 때만 지워진다 */
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* OSB14_NO_MAIN */
